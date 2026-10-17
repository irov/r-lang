module runtime.reactor;
import runtime.types;
import runtime.types::{SourceSpan};
import runtime.system;
import runtime.system::{Mutex, Thread, TimeSpec, Timebase, Event};
import runtime.panics;
import runtime.stacks;

// The reactor of the runtime (r_runtime_reactor.h): one thread that owns the kernel event queue,
// arms and disarms the alarms of timers and deadlines, and runs their callbacks. Other threads only
// post commands, which the reactor takes when its wakeup event fires, so a native registration is
// changed and an event delivered on one thread: a cancellation and an event that fired meanwhile
// are ordered there, and an alarm is settled exactly once.
@header("r_runtime_reactor.h")
extern "C" {
}

@link(name = "r.runtime", kind = "static")
@header("r_runtime_reactor.h")
@abi("r-runtime-abi")
extern "C" {
    @repr(C)
    @c_type(name = "RRuntimeReactorAlarm", kind = "typedef")
    struct Alarm {
        raw Alarm*? next;
        raw fn?(raw Alarm*?) -> void fire;
        raw fn?(raw Alarm*?) -> void settle;
        raw void*? context;
        c_uint64 deadline;
        c_uint32 command;
        c_uint32 state;
    };
}

// Commands an alarm may carry; a later command replaces an earlier one not yet taken.
const u32 command_none = 0u32;
const u32 command_arm = 1u32;
const u32 command_disarm = 2u32;

// States, written by the reactor thread under the lock. An alarm with a settle callback ends
// settled, exactly once; one without returns to idle after it fires and may be armed again.
const u32 state_idle = 0u32;
const u32 state_armed = 1u32;
const u32 state_fired = 2u32;
const u32 state_settled = 3u32;

const usize batch = 64usize;
const u64 u64_maximum = 18446744073709551615u64;
const u64 i64_maximum = 9223372036854775807u64;

Mutex reactor_mutex = {.signature = runtime.system::mutex_signature as c_long, .bytes = {}};
raw Alarm*? commands = null;
c_int kernel_queue = -1i32 as c_int;
bool started = false;

protected never fail() {
    unsafe {
        runtime.system::abort();
    }
}

protected SourceSpan no_span() {
    return SourceSpan {.module_id = 0u32 as c_uint32, .start = 0u32 as c_uint32,
                       .end = 0u32 as c_uint32};
}

protected void reactor_lock() {
    unsafe {
        runtime.system::require_zero(
            runtime.system::pthread_mutex_lock(&reactor_mutex as raw Mutex*));
    }
}

protected void reactor_unlock() {
    unsafe {
        runtime.system::require_zero(
            runtime.system::pthread_mutex_unlock(&reactor_mutex as raw Mutex*));
    }
}

// The monotonic clock of std.time::instant: continuous Mach time in nanoseconds, the exact
// quotient of ticks times numer by denom, saturated.
u64 now() {
    unsafe {
        Timebase timebase = {.numer = 0u32 as c_uint32, .denom = 0u32 as c_uint32};
        if (runtime.system::mach_timebase_info(&timebase as raw Timebase*) != 0i32 as c_int) {
            fail();
        }
        u64 numer = timebase.numer as u64;
        u64 denom = timebase.denom as u64;
        if (denom == 0u64) {
            fail();
        }
        u64 ticks = runtime.system::mach_continuous_time() as u64;
        // ticks = quotient * denom + remainder, so ticks * numer / denom is
        // quotient * numer + remainder * numer / denom; remainder * numer < 2^64.
        u64 quotient = ticks / denom;
        u64 remainder = ticks % denom;
        u64 tail = (remainder * numer) / denom;
        auto (low, high) = core::widening_mul(quotient, numer);
        bool overflow = high != 0u64;
        bool beyond = low > u64_maximum - tail;
        if ((overflow == true) || (beyond == true)) {
            return u64_maximum;
        }
        return low + tail;
    }
}

// Submits one change of the kernel queue on the reactor thread, or posts its wakeup.
protected c_int change(c_uintptr ident, c_int filter, c_int flags, c_int fflags, c_intptr data,
                       raw void*? udata) {
    unsafe {
        Event entry = {.ident = ident, .filter = filter as c_int16, .flags = flags as c_uint16,
                       .fflags = fflags as c_uint32, .data = data, .udata = udata};
        TimeSpec zero = {.seconds = 0i64 as c_long, .nanoseconds = 0i64 as c_long};
        return runtime.system::kevent(kernel_queue, &entry as raw const Event*, 1i32 as c_int,
                                      null, 0i32 as c_int, &zero as raw const TimeSpec*);
    }
}

protected void wake() {
    if (change(0u64 as c_uintptr, runtime.system::filter_user, 0i32 as c_int,
               runtime.system::note_trigger, 0i64 as c_intptr, null) != 0i32 as c_int) {
        fail();
    }
}

// An alarm owned by the reactor until its command is taken: armed for the deadline, or removed.
protected void arm_native(raw Alarm*? alarm, u64 deadline) {
    u64 current = now();
    u64 remaining = 1u64;
    if (deadline > current) {
        remaining = deadline - current;
    }
    if (remaining > i64_maximum) {
        remaining = i64_maximum;
    }
    unsafe {
        if (change(core::pointer_address(alarm) as c_uintptr, runtime.system::filter_timer,
                   runtime.system::event_add | runtime.system::event_oneshot,
                   runtime.system::note_nanoseconds, remaining as c_intptr,
                   alarm as raw void*?) != 0i32 as c_int) {
            fail();
        }
    }
}

protected void disarm_native(raw Alarm*? alarm) {
    unsafe {
        // ENOENT: the one-shot registration fired and is gone; its event was handled before.
        change(core::pointer_address(alarm) as c_uintptr, runtime.system::filter_timer,
               runtime.system::event_delete, 0i32 as c_int, 0i64 as c_intptr, null) as void;
    }
}

protected void call(raw fn?(raw Alarm*?) -> void callback, raw Alarm*? alarm) {
    unsafe {
        if (callback == null) {
            fail();
        }
        callback(alarm);
    }
}

// Settles an alarm that will not fire again, unless a command was posted meanwhile: that command
// settles it when the reactor takes it. Deciding under the lock that no command is linked keeps
// the storage, which the settle callback may release, out of the command list.
protected void try_settle(raw Alarm*? alarm) {
    unsafe {
        raw fn?(raw Alarm*?) -> void callback = alarm->settle;
        reactor_lock();
        bool waiting = (alarm->command as u32) != command_none;
        if (waiting == true) {
            alarm->state = state_fired as c_uint32;
        } else {
            alarm->state = state_settled as c_uint32;
        }
        reactor_unlock();
        if (waiting == false) {
            call(callback, alarm);
        }
    }
}

// The timer of an armed alarm fired.
protected void fired(raw Alarm*? alarm) {
    unsafe {
        if ((alarm->state as u32) != state_armed) {
            return;
        }
        bool settles = alarm->settle != null;
        reactor_lock();
        if (settles == true) {
            alarm->state = state_fired as c_uint32;
        } else {
            alarm->state = state_idle as c_uint32;
        }
        reactor_unlock();
        call(alarm->fire, alarm);
        if (settles == true) {
            try_settle(alarm);
        }
    }
}

// Takes every posted command, the latest of each alarm. An alarm without a settle callback is
// only armed; one with a settle callback is armed once and settles after it fires or when it is
// disarmed, whichever the reactor sees first.
protected void take_commands() {
    reactor_lock();
    unsafe {
        raw Alarm*? alarm = commands;
        commands = null;
        reactor_unlock();
        while (alarm != null) {
            reactor_lock();
            raw Alarm*? following = alarm->next;
            u32 command = alarm->command as u32;
            u64 deadline = alarm->deadline as u64;
            u32 state = alarm->state as u32;
            bool settles = alarm->settle != null;
            alarm->next = null;
            alarm->command = command_none as c_uint32;
            bool arming = (command == command_arm) &&
                          ((state == state_idle) || (state == state_armed));
            if (arming == true) {
                alarm->state = state_armed as c_uint32;
            }
            reactor_unlock();
            if (arming == true) {
                arm_native(alarm, deadline);
            } else {
                if ((settles == true) && (state != state_settled)) {
                    if (state == state_armed) {
                        disarm_native(alarm);
                    }
                    try_settle(alarm);
                }
            }
            alarm = following;
        }
    }
}

protected c_int wait_events(raw Event* events) {
    unsafe {
        return runtime.system::kevent(kernel_queue, null, 0i32 as c_int, events, batch as c_int,
                                      null);
    }
}

@callback
@safety("R-RUNTIME-REACTOR-MAIN", "runs as the entry of the reactor thread")
protected extern "C" raw void*? reactor_main(raw void*? context) {
    unsafe {
        runtime.system::pthread_setname_np(runtime.system::c_string("r.reactor\0")) as void;
    }
    if (runtime.types::is_true(runtime.stacks::r_runtime_stack_initialize_current_thread()) ==
        false) {
        runtime.panics::r_runtime_panic(12u32 as c_uint, no_span());
    }
    Event[64] events = {};
    while (true) {
        unsafe {
            c_int count = wait_events(&events[0usize] as raw Event*);
            if ((count as i32) < 0i32) {
                // EINTR: wait again.
                continue;
            }
            bool woken = false;
            usize index = 0usize;
            usize total = count as usize;
            // Timer events first: a command taken afterwards finds what they did.
            while (index < total) {
                Event entry = events[index];
                if ((entry.filter as i32) == (runtime.system::filter_user as i32)) {
                    woken = true;
                } else {
                    fired(entry.udata as raw Alarm*?);
                }
                index += 1usize;
            }
            if (woken == true) {
                take_commands();
            }
        }
    }
    return context;
}

// Starts the reactor thread once; false when the kernel queue or the thread cannot be made.
bool start() {
    reactor_lock();
    unsafe {
        if (started == true) {
            reactor_unlock();
            return true;
        }
        c_int queue = runtime.system::kqueue();
        if ((queue as i32) < 0i32) {
            reactor_unlock();
            return false;
        }
        kernel_queue = queue;
        if (change(0u64 as c_uintptr, runtime.system::filter_user,
                   runtime.system::event_add | runtime.system::event_clear, 0i32 as c_int,
                   0i64 as c_intptr, null) != 0i32 as c_int) {
            reactor_unlock();
            return false;
        }
        raw Thread*? thread = null;
        if (runtime.system::pthread_create(&thread as raw (raw Thread*?)*, null, reactor_main,
                                           null) != 0i32 as c_int) {
            reactor_unlock();
            return false;
        }
        runtime.system::pthread_detach(thread) as void;
        started = true;
    }
    reactor_unlock();
    return true;
}

// Prepares an alarm; its callbacks run on the reactor thread with the alarm, whose context names
// their owner. Without settle the alarm may be armed again after it fires.
void initialize(raw Alarm*? alarm, raw fn?(raw Alarm*?) -> void fire,
                raw fn?(raw Alarm*?) -> void settle_callback, raw void*? context) {
    unsafe {
        *alarm = Alarm {.next = null, .fire = fire, .settle = settle_callback, .context = context,
                        .deadline = 0u64 as c_uint64, .command = command_none as c_uint32,
                        .state = state_idle as c_uint32};
    }
}

// Posts a command; false when the alarm has settled, which ends its use.
protected bool post(raw Alarm*? alarm, u32 command, u64 deadline) {
    reactor_lock();
    unsafe {
        if ((alarm->state as u32) == state_settled) {
            reactor_unlock();
            return false;
        }
        if (command == command_arm) {
            alarm->deadline = deadline as c_uint64;
        }
        bool linked = (alarm->command as u32) != command_none;
        alarm->command = command as c_uint32;
        if (linked == false) {
            alarm->next = commands;
            commands = alarm;
        }
    }
    reactor_unlock();
    wake();
    return true;
}

// Arms an alarm for a deadline of the monotonic clock, or moves the deadline of an armed one.
bool arm(raw Alarm*? alarm, u64 deadline) {
    return post(alarm, command_arm, deadline);
}

// Disarms an alarm with a settle callback: it settles once, at once if it has not fired.
bool disarm(raw Alarm*? alarm) {
    return post(alarm, command_disarm, 0u64);
}

// Whether a command of the alarm waits for the reactor.
bool pending(raw Alarm*? alarm) {
    reactor_lock();
    unsafe {
        bool waiting = (alarm->command as u32) != command_none;
        reactor_unlock();
        return waiting;
    }
}

// Whether an alarm with a settle callback has settled.
bool settled(raw Alarm*? alarm) {
    reactor_lock();
    unsafe {
        bool done = (alarm->state as u32) == state_settled;
        reactor_unlock();
        return done;
    }
}

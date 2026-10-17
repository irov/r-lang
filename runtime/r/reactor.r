module runtime.reactor;
import runtime.types;
import runtime.types::{SourceSpan};
import runtime.system;
import runtime.system::{Mutex, Thread, TimeSpec, Timebase, Event};
import runtime.panics;
import runtime.stacks;

// The reactor of the runtime (r_runtime_reactor.h): one thread that owns the kernel event queue,
// registers and removes the sources of timers, deadlines and descriptors, and runs their callbacks.
// Other threads only post commands, which the reactor takes when its wakeup event fires, so a
// native registration is changed and an event delivered on one thread: a cancellation and an
// event that fired meanwhile are ordered there, and a source is settled exactly once.
@header("r_runtime_reactor.h")
extern "C" {
}

@link(name = "r.runtime", kind = "static")
@header("r_runtime_reactor.h")
@abi("r-runtime-abi")
extern "C" {
    @repr(C)
    @c_type(name = "RRuntimeReactorSource", kind = "typedef")
    struct Source {
        raw Source*? next;
        raw fn?(raw Source*?) -> void fire;
        raw fn?(raw Source*?) -> void settle;
        raw void*? context;
        c_uint64 deadline;
        c_uintptr ident;
        c_int filter;
        c_uint32 command;
        c_uint32 state;
    };

    // A job of the worker pool (runtime/r/task.r): run is called once, on a worker, with the
    // job, whose context names its owner; run may release the storage of the job.
    @repr(C)
    @c_type(name = "RRuntimeJob", kind = "typedef")
    struct Job {
        raw Job*? next;
        raw fn?(raw Job*?) -> void run;
        raw void*? context;
    };
}

// Commands a source may carry; a later command replaces an earlier one not yet taken.
const u32 command_none = 0u32;
const u32 command_arm = 1u32;
const u32 command_disarm = 2u32;

// States, written by the reactor thread under the lock. A source with a settle callback ends
// settled, exactly once; an alarm without one returns to idle after it fires and may be armed
// again. A descriptor watch stays fired after its event, unregistered, until it is armed again
// or disarmed.
const u32 state_idle = 0u32;
const u32 state_armed = 1u32;
const u32 state_fired = 2u32;
const u32 state_settled = 3u32;

const usize batch = 64usize;
const u64 u64_maximum = 18446744073709551615u64;
const u64 i64_maximum = 9223372036854775807u64;

Mutex reactor_mutex = {.signature = runtime.system::mutex_signature as c_long, .bytes = {}};
raw Source*? commands = null;
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

protected bool is_watch(raw Source*? source) {
    unsafe {
        return (source->filter as i32) != (runtime.system::filter_timer as i32);
    }
}

// Registers a source taken by the reactor: an alarm for its deadline, a watch for one readiness
// event of its descriptor. Each registration is specific to its source (EV_UDATA_SPECIFIC), so
// sources on one descriptor never replace or remove each other, as independent Dispatch sources
// did. False when the kernel refuses a watch, a descriptor that is not open: its owner then
// learns the error from its native call.
protected bool arm_native(raw Source*? source, u64 deadline) {
    unsafe {
        c_int specific = runtime.system::event_add | runtime.system::event_oneshot |
                         runtime.system::event_udata_specific;
        if (is_watch(source) == true) {
            return change(source->ident, source->filter, specific, 0i32 as c_int,
                          0i64 as c_intptr, source as raw void*?) == 0i32 as c_int;
        }
        u64 current = now();
        u64 remaining = 1u64;
        if (deadline > current) {
            remaining = deadline - current;
        }
        if (remaining > i64_maximum) {
            remaining = i64_maximum;
        }
        if (change(core::pointer_address(source) as c_uintptr, runtime.system::filter_timer,
                   specific, runtime.system::note_nanoseconds, remaining as c_intptr,
                   source as raw void*?) != 0i32 as c_int) {
            fail();
        }
        return true;
    }
}

protected void disarm_native(raw Source*? source) {
    unsafe {
        c_uintptr ident = source->ident;
        if (is_watch(source) == false) {
            ident = core::pointer_address(source) as c_uintptr;
        }
        // ENOENT: the one-shot registration fired and is gone, its event handled before; for a
        // watch also a descriptor closed meanwhile, which removed it.
        change(ident, source->filter,
               runtime.system::event_delete | runtime.system::event_udata_specific,
               0i32 as c_int, 0i64 as c_intptr, source as raw void*?) as void;
    }
}

protected void call(raw fn?(raw Source*?) -> void callback, raw Source*? source) {
    unsafe {
        if (callback == null) {
            fail();
        }
        callback(source);
    }
}

// Settles a source that will not fire again, unless a command was posted meanwhile: that command
// settles it when the reactor takes it. Deciding under the lock that no command is linked keeps
// the storage, which the settle callback may release, out of the command list.
protected void try_settle(raw Source*? source) {
    unsafe {
        raw fn?(raw Source*?) -> void callback = source->settle;
        reactor_lock();
        bool waiting = (source->command as u32) != command_none;
        if (waiting == true) {
            source->state = state_fired as c_uint32;
        } else {
            source->state = state_settled as c_uint32;
        }
        reactor_unlock();
        if (waiting == false) {
            call(callback, source);
        }
    }
}

// The registration of an armed source fired, or a watch could not be registered. An alarm with
// a settle callback then settles; a watch waits, unregistered, for its owner to arm it again or
// to disarm it.
protected void fired(raw Source*? source) {
    unsafe {
        if ((source->state as u32) != state_armed) {
            return;
        }
        bool watching = is_watch(source);
        bool settles = false;
        if (watching == false) {
            settles = source->settle != null;
        }
        reactor_lock();
        if ((watching == true) || (settles == true)) {
            source->state = state_fired as c_uint32;
        } else {
            source->state = state_idle as c_uint32;
        }
        reactor_unlock();
        call(source->fire, source);
        if (settles == true) {
            try_settle(source);
        }
    }
}

// Takes every posted command, the latest of each source. An alarm without a settle callback is
// only armed; one with a settle callback is armed once and settles after it fires or when it is
// disarmed, whichever the reactor sees first. A watch is armed again after each event until it
// is disarmed, and settles then.
protected void take_commands() {
    reactor_lock();
    unsafe {
        raw Source*? source = commands;
        commands = null;
        reactor_unlock();
        while (source != null) {
            reactor_lock();
            raw Source*? following = source->next;
            u32 command = source->command as u32;
            u64 deadline = source->deadline as u64;
            u32 state = source->state as u32;
            bool settles = source->settle != null;
            bool watching = is_watch(source);
            source->next = null;
            source->command = command_none as c_uint32;
            bool rearm = (watching == true) && (state == state_fired);
            bool arming = (command == command_arm) &&
                          ((state == state_idle) || (state == state_armed) || (rearm == true));
            if (arming == true) {
                source->state = state_armed as c_uint32;
            }
            reactor_unlock();
            if (arming == true) {
                if (arm_native(source, deadline) == false) {
                    fired(source);
                }
            } else {
                if ((settles == true) && (state != state_settled)) {
                    if (state == state_armed) {
                        disarm_native(source);
                    }
                    try_settle(source);
                }
            }
            source = following;
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
            // Source events first: a command taken afterwards finds what they did.
            while (index < total) {
                Event entry = events[index];
                if ((entry.filter as i32) == (runtime.system::filter_user as i32)) {
                    woken = true;
                } else {
                    fired(entry.udata as raw Source*?);
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

// Prepares an alarm; its callbacks run on the reactor thread with the source, whose context
// names their owner. Without settle the alarm may be armed again after it fires.
void initialize(raw Source*? source, raw fn?(raw Source*?) -> void fire,
                raw fn?(raw Source*?) -> void settle_callback, raw void*? context) {
    unsafe {
        *source = Source {.next = null, .fire = fire, .settle = settle_callback,
                          .context = context, .deadline = 0u64 as c_uint64,
                          .ident = 0u64 as c_uintptr, .filter = runtime.system::filter_timer,
                          .command = command_none as c_uint32, .state = state_idle as c_uint32};
    }
}

// Prepares a watch of the read or write readiness of an open descriptor, which stays open until
// the watch settles. Each arm waits for one event, after which fire runs on the reactor thread;
// disarm settles the watch once, after which no callback runs.
void initialize_watch(raw Source*? source, c_int descriptor, c_int filter,
                      raw fn?(raw Source*?) -> void fire,
                      raw fn?(raw Source*?) -> void settle_callback, raw void*? context) {
    unsafe {
        if (((filter as i32) != (runtime.system::filter_read as i32)) &&
            ((filter as i32) != (runtime.system::filter_write as i32))) {
            fail();
        }
        if (((descriptor as i32) < 0i32) || (settle_callback == null)) {
            fail();
        }
        *source = Source {.next = null, .fire = fire, .settle = settle_callback,
                          .context = context, .deadline = 0u64 as c_uint64,
                          .ident = descriptor as c_uintptr, .filter = filter,
                          .command = command_none as c_uint32, .state = state_idle as c_uint32};
    }
}

// Posts a command; false when the source has settled, which ends its use.
protected bool post(raw Source*? source, u32 command, u64 deadline) {
    reactor_lock();
    unsafe {
        if ((source->state as u32) == state_settled) {
            reactor_unlock();
            return false;
        }
        if (command == command_arm) {
            source->deadline = deadline as c_uint64;
        }
        bool linked = (source->command as u32) != command_none;
        source->command = command as c_uint32;
        if (linked == false) {
            source->next = commands;
            commands = source;
        }
    }
    reactor_unlock();
    wake();
    return true;
}

// Arms an alarm for a deadline of the monotonic clock, or moves the deadline of an armed one.
bool arm(raw Source*? source, u64 deadline) {
    return post(source, command_arm, deadline);
}

// Arms a watch for its next readiness event.
bool watch(raw Source*? source) {
    return post(source, command_arm, 0u64);
}

// Disarms a source with a settle callback: it settles once, at once if it is not registered.
bool disarm(raw Source*? source) {
    return post(source, command_disarm, 0u64);
}

// Whether a command of the source waits for the reactor.
bool pending(raw Source*? source) {
    reactor_lock();
    unsafe {
        bool waiting = (source->command as u32) != command_none;
        reactor_unlock();
        return waiting;
    }
}

// Whether a source with a settle callback has settled.
bool settled(raw Source*? source) {
    reactor_lock();
    unsafe {
        bool done = (source->state as u32) == state_settled;
        reactor_unlock();
        return done;
    }
}

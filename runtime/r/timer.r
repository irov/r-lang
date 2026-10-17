module runtime.timers;
import runtime.types;
import runtime.types::{ExternalHandle};
import runtime.system;
import runtime.system::{Mutex, Condition};
import runtime.reactor;
import runtime.reactor::{Source};
import runtime.tasks;

// One-shot timers of the darwin adapter (r_runtime_darwin_timer.h): a reactor alarm whose deadline
// prepare fixes before the task commits, bound to the committed external execution and armed
// once. Its firing selects completion; its settlement, after it fired or was disarmed, acknowledges
// the task (runtime/r/reactor.r).
@header("r_runtime_darwin_timer.h")
extern "C" {
}

@link(name = "r.runtime", kind = "static")
@header("r_runtime_darwin_timer.h")
@header("r_runtime_darwin_event.h")
@abi("r-runtime-abi")
extern "C" {
    @repr(C)
    @c_type(name = "RRuntimeDarwinTimer", kind = "typedef")
    struct Timer {
        Source alarm;
        c_bool prepared;
        raw ExternalHandle*? execution;
    };

    @repr(C)
    @c_type(name = "RRuntimeDarwinTimerPrepareStatus", kind = "typedef")
    enum PrepareStatus : c_uint {
        R_RUNTIME_DARWIN_TIMER_PREPARE_OK = 0,
        R_RUNTIME_DARWIN_TIMER_PREPARE_INVALID = 1,
        R_RUNTIME_DARWIN_TIMER_PREPARE_ALLOCATION_FAILED = 2,
        R_RUNTIME_DARWIN_TIMER_PREPARE_UNAVAILABLE = 3,
    };
}

const u64 nanoseconds_per_second = 1000000000u64;
const u64 u64_maximum = 18446744073709551615u64;
// The horizon of a delay: the signed 64-bit nanoseconds a native timer takes.
const u64 delay_maximum = 9223372036854775807u64;

@safety("R-RUNTIME-TIMER-CANCEL", "timer is null or initialized")
extern "C" void r_runtime_darwin_timer_cancel(raw Timer*? timer) {
    unsafe {
        if ((timer == null) || (timer->execution == null)) {
            return;
        }
        // A settled alarm refuses the command: the task was acknowledged already.
        runtime.reactor::disarm(&timer->alarm as raw Source*) as void;
    }
}

// ---- Testing: a pause of the next cancel handler after it takes its source (always present;
// an unarmed hook costs one atomic load) ----

atomic bool pause_armed_hint = false;
Mutex pause_mutex = {.signature = runtime.system::mutex_signature as c_long, .bytes = {}};
Condition pause_condition = {.signature = runtime.system::condition_signature as c_long,
                             .bytes = {}};
raw Timer*? paused_timer = null;
bool pause_armed = false;
bool pause_reached = false;
bool pause_released = false;

protected raw Mutex* pause_lock() {
    unsafe {
        raw Mutex* mutex = &pause_mutex as raw Mutex*;
        runtime.system::require_zero(runtime.system::pthread_mutex_lock(mutex));
        return mutex;
    }
}

protected void pause_unlock(raw Mutex* mutex) {
    unsafe {
        runtime.system::require_zero(runtime.system::pthread_mutex_unlock(mutex));
    }
}

protected void pause_wait(raw Mutex* mutex) {
    unsafe {
        runtime.system::require_zero(runtime.system::pthread_cond_wait(
            &pause_condition as raw Condition*, mutex));
    }
}

protected void pause_broadcast() {
    unsafe {
        runtime.system::require_zero(
            runtime.system::pthread_cond_broadcast(&pause_condition as raw Condition*));
    }
}

protected void pause_fail(raw Mutex* mutex) {
    pause_unlock(mutex);
    unsafe {
        runtime.system::abort();
    }
}

@safety("R-RUNTIME-TIMER-TESTING-PAUSE", "None")
extern "C" void r_runtime_darwin_timer_testing_pause_next_cancel_handler() {
    raw Mutex* mutex = pause_lock();
    unsafe {
        if (pause_armed == true) {
            pause_fail(mutex);
        }
        paused_timer = null;
        pause_armed = true;
        pause_reached = false;
        pause_released = false;
    }
    core::atomic_store(&pause_armed_hint, true, core::memory_order::release);
    pause_unlock(mutex);
}

@safety("R-RUNTIME-TIMER-TESTING-WAIT", "a pause is armed")
extern "C" void r_runtime_darwin_timer_testing_wait_for_cancel_handler() {
    raw Mutex* mutex = pause_lock();
    unsafe {
        if (pause_armed == false) {
            pause_fail(mutex);
        }
        while (pause_reached == false) {
            pause_wait(mutex);
        }
    }
    pause_unlock(mutex);
}

@safety("R-RUNTIME-TIMER-TESTING-CANCEL", "a cancel handler waits in its pause")
extern "C" void r_runtime_darwin_timer_testing_cancel_paused_timer() {
    raw Mutex* mutex = pause_lock();
    unsafe {
        if ((pause_armed == false) || (pause_reached == false) || (pause_released == true) ||
            (paused_timer == null)) {
            pause_fail(mutex);
        }
        raw Timer*? timer = paused_timer;
        pause_unlock(mutex);
        r_runtime_darwin_timer_cancel(timer);
    }
}

@safety("R-RUNTIME-TIMER-TESTING-RELEASE", "a cancel handler waits in its pause")
extern "C" void r_runtime_darwin_timer_testing_release_cancel_handler() {
    raw Mutex* mutex = pause_lock();
    unsafe {
        if ((pause_armed == false) || (pause_reached == false) || (pause_released == true)) {
            pause_fail(mutex);
        }
        pause_released = true;
    }
    pause_broadcast();
    pause_unlock(mutex);
}

protected void testing_pause_cancel_handler(raw Timer*? timer) {
    if (core::atomic_load(&pause_armed_hint, core::memory_order::acquire) == false) {
        return;
    }
    raw Mutex* mutex = pause_lock();
    unsafe {
        if ((pause_armed == true) && (pause_reached == false)) {
            paused_timer = timer;
            pause_reached = true;
            pause_broadcast();
            while (pause_released == false) {
                pause_wait(mutex);
            }
            paused_timer = null;
            pause_armed = false;
            pause_reached = false;
            pause_released = false;
            core::atomic_store(&pause_armed_hint, false, core::memory_order::release);
        }
    }
    pause_unlock(mutex);
}

// ---- Callbacks of the alarm, on the reactor thread ----

@callback
@safety("R-RUNTIME-TIMER-FIRED", "context is a bound timer")
protected extern "C" void timer_fired(raw Source*? alarm) {
    unsafe {
        raw Timer*? timer = alarm->context as raw Timer*?;
        c_uint64 sequence = runtime.types::r_runtime_darwin_event_sequence_next();
        runtime.tasks::r_runtime_task_external_try_select_completion_at(timer->execution,
                                                                         sequence) as void;
    }
}

@callback
@safety("R-RUNTIME-TIMER-SETTLED", "context is a bound timer")
protected extern "C" void timer_settled(raw Source*? alarm) {
    unsafe {
        raw Timer*? timer = alarm->context as raw Timer*?;
        testing_pause_cancel_handler(timer);
        runtime.tasks::r_runtime_task_external_acknowledge(timer->execution);
    }
}

// ---- Exports of r_runtime_darwin_timer.h ----

@safety("R-RUNTIME-TIMER-INITIALIZE", "timer is null or writable")
extern "C" void r_runtime_darwin_timer_initialize(raw Timer*? timer) {
    unsafe {
        if (timer == null) {
            return;
        }
        runtime.reactor::initialize(&timer->alarm as raw Source*, timer_fired, timer_settled,
                                    timer as raw void*?);
        timer->prepared = runtime.types::c_bool_of(false);
        timer->execution = null;
    }
}

@safety("R-RUNTIME-TIMER-MOVE", "both timers are initialized; source is not bound")
extern "C" void r_runtime_darwin_timer_move(raw Timer*? destination, raw Timer*? source) {
    unsafe {
        // A prepared timer is not yet known to the reactor: its deadline moves with it.
        destination->alarm.deadline = source->alarm.deadline;
        destination->prepared = source->prepared;
        source->prepared = runtime.types::c_bool_of(false);
    }
}

@safety("R-RUNTIME-TIMER-PREPARE", "timer is null or initialized")
extern "C" PrepareStatus r_runtime_darwin_timer_prepare(raw Timer*? timer, c_uint64 seconds,
                                                        c_uint32 nanoseconds) {
    unsafe {
        if (timer == null) {
            return PrepareStatus::R_RUNTIME_DARWIN_TIMER_PREPARE_INVALID;
        }
        bool busy = (runtime.types::is_true(timer->prepared) == true) || (timer->execution != null);
        if ((busy == true) || ((nanoseconds as u64) >= nanoseconds_per_second)) {
            return PrepareStatus::R_RUNTIME_DARWIN_TIMER_PREPARE_INVALID;
        }
        u64 whole = seconds as u64;
        if (whole > delay_maximum / nanoseconds_per_second) {
            return PrepareStatus::R_RUNTIME_DARWIN_TIMER_PREPARE_UNAVAILABLE;
        }
        u64 delay = whole * nanoseconds_per_second;
        if ((nanoseconds as u64) > delay_maximum - delay) {
            return PrepareStatus::R_RUNTIME_DARWIN_TIMER_PREPARE_UNAVAILABLE;
        }
        delay += nanoseconds as u64;
        u64 current = runtime.reactor::now();
        if (delay > u64_maximum - current) {
            return PrepareStatus::R_RUNTIME_DARWIN_TIMER_PREPARE_UNAVAILABLE;
        }
        if (runtime.reactor::start() == false) {
            return PrepareStatus::R_RUNTIME_DARWIN_TIMER_PREPARE_ALLOCATION_FAILED;
        }
        timer->alarm.deadline = (current + delay) as c_uint64;
        timer->prepared = runtime.types::c_bool_of(true);
    }
    return PrepareStatus::R_RUNTIME_DARWIN_TIMER_PREPARE_OK;
}

@safety("R-RUNTIME-TIMER-BIND", "timer is prepared; execution is its committed external task")
extern "C" void r_runtime_darwin_timer_bind_and_activate(raw Timer*? timer,
                                                         raw ExternalHandle*? execution) {
    unsafe {
        if ((timer == null) || (execution == null) || (timer->execution != null) ||
            (runtime.types::is_true(timer->prepared) == false)) {
            runtime.system::abort();
        }
        timer->execution = execution;
        timer->prepared = runtime.types::c_bool_of(false);
        u64 deadline = timer->alarm.deadline as u64;
        runtime.reactor::initialize(&timer->alarm as raw Source*, timer_fired, timer_settled,
                                    timer as raw void*?);
        runtime.tasks::r_runtime_task_external_start_ready(execution);
        runtime.reactor::arm(&timer->alarm as raw Source*, deadline) as void;
    }
}

@safety("R-RUNTIME-TIMER-DISPOSE", "timer is null or initialized")
extern "C" void r_runtime_darwin_timer_dispose_unsubmitted(raw Timer*? timer) {
    unsafe {
        if ((timer == null) || (timer->execution != null)) {
            return;
        }
        timer->prepared = runtime.types::c_bool_of(false);
    }
}

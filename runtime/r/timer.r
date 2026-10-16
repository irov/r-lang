module runtime.timers;
import runtime.types;
import runtime.types::{ExternalHandle};
import runtime.system;
import runtime.system::{Mutex, Condition, Source, Object};
import runtime.tasks;

// One-shot timers of the darwin adapter (r_runtime_darwin_timer.h): a suspended Dispatch timer
// source reserved before the task commits, bound to the committed external execution, activated
// once; its event selects completion, and its cancel handler, which owns the source, acknowledges
// the task.
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
        atomic raw void*? native_source;
        atomic c_bool activated;
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
const u64 delta_maximum = 9223372036854775807u64;

protected raw Source*? source_of(raw Timer*? timer) {
    unsafe {
        return core::atomic_load(&timer->native_source, core::memory_order::acquire) as raw Source*?;
    }
}

protected void activate_source(raw Timer*? timer, raw Source*? source) {
    unsafe {
        core::atomic_compare_exchange_result<c_bool> activated = core::atomic_compare_exchange(
            &timer->activated, 0i32 as c_bool, 1i32 as c_bool, core::memory_order::acq_rel,
            core::memory_order::acquire);
        switch (move activated) {
        case variant core::atomic_compare_exchange_result::exchanged(move previous):
            previous as void;
            runtime.system::dispatch_activate(Object {.source = source});
            break;
        case variant core::atomic_compare_exchange_result::unchanged(move previous):
            previous as void;
            break;
        }
    }
}

@safety("R-RUNTIME-TIMER-CANCEL", "timer is null or initialized")
extern "C" void r_runtime_darwin_timer_cancel(raw Timer*? timer) {
    unsafe {
        if ((timer == null) || (timer->execution == null)) {
            return;
        }
        raw Source*? source = source_of(timer);
        if (source == null) {
            // The cancel handler owns acknowledgement once the timer has been bound.
            return;
        }
        activate_source(timer, source);
        runtime.system::dispatch_source_cancel(source);
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

// ---- Dispatch handlers ----

@callback
@safety("R-RUNTIME-TIMER-EVENT", "context is a bound timer")
protected extern "C" void timer_event(raw void* context) {
    unsafe {
        raw Timer*? timer = context as raw Timer*?;
        c_uint64 sequence = runtime.types::r_runtime_darwin_event_sequence_next();
        if (runtime.types::is_true(runtime.tasks::r_runtime_task_external_try_select_completion_at(
                timer->execution, sequence)) == true) {
            r_runtime_darwin_timer_cancel(timer);
        }
    }
}

@callback
@safety("R-RUNTIME-TIMER-CANCELLED", "context is a bound timer")
protected extern "C" void timer_cancelled(raw void* context) {
    unsafe {
        raw Timer*? timer = context as raw Timer*?;
        raw void*? taken = core::atomic_exchange(&timer->native_source, null,
                                                 core::memory_order::acq_rel);
        if (taken == null) {
            return;
        }
        testing_pause_cancel_handler(timer);
        runtime.system::dispatch_release(Object {.source = taken as raw Source*?});
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
        core::atomic_store(&timer->native_source, null, core::memory_order::relaxed);
        core::atomic_store(&timer->activated, 0i32 as c_bool, core::memory_order::relaxed);
        timer->execution = null;
    }
}

@safety("R-RUNTIME-TIMER-MOVE", "both timers are initialized")
extern "C" void r_runtime_darwin_timer_move(raw Timer*? destination, raw Timer*? source) {
    unsafe {
        raw void*? taken = core::atomic_exchange(&source->native_source, null,
                                                 core::memory_order::acq_rel);
        core::atomic_store(&destination->native_source, taken, core::memory_order::release);
    }
}

@safety("R-RUNTIME-TIMER-PREPARE", "timer is null or initialized")
extern "C" PrepareStatus r_runtime_darwin_timer_prepare(raw Timer*? timer, c_uint64 seconds,
                                                        c_uint32 nanoseconds) {
    unsafe {
        if (timer == null) {
            return PrepareStatus::R_RUNTIME_DARWIN_TIMER_PREPARE_INVALID;
        }
        bool busy = (source_of(timer) != null) || (timer->execution != null);
        bool activated =
            core::atomic_load(&timer->activated, core::memory_order::acquire) != 0i32 as c_bool;
        if ((busy == true) || (activated == true) ||
            ((nanoseconds as u64) >= nanoseconds_per_second)) {
            return PrepareStatus::R_RUNTIME_DARWIN_TIMER_PREPARE_INVALID;
        }
        u64 whole = seconds as u64;
        if (whole > delta_maximum / nanoseconds_per_second) {
            return PrepareStatus::R_RUNTIME_DARWIN_TIMER_PREPARE_UNAVAILABLE;
        }
        u64 delay = whole * nanoseconds_per_second;
        if ((nanoseconds as u64) > delta_maximum - delay) {
            return PrepareStatus::R_RUNTIME_DARWIN_TIMER_PREPARE_UNAVAILABLE;
        }
        delay += nanoseconds as u64;
        c_uint64 deadline = runtime.system::dispatch_time(runtime.system::time_now as c_uint64,
                                                          delay as c_int64);
        if ((deadline as u64) == (runtime.system::time_forever as u64)) {
            return PrepareStatus::R_RUNTIME_DARWIN_TIMER_PREPARE_UNAVAILABLE;
        }
        raw runtime.system::Queue*? queue = runtime.system::dispatch_get_global_queue(
            runtime.system::priority_default as c_intptr, 0u64 as c_uintptr);
        if (queue == null) {
            return PrepareStatus::R_RUNTIME_DARWIN_TIMER_PREPARE_UNAVAILABLE;
        }
        raw Source*? source = runtime.system::dispatch_source_create(
            runtime.system::_dispatch_source_type_timer, 0u64 as c_uintptr,
            runtime.system::timer_strict as c_uintptr, queue);
        if (source == null) {
            return PrepareStatus::R_RUNTIME_DARWIN_TIMER_PREPARE_ALLOCATION_FAILED;
        }
        runtime.system::dispatch_source_set_timer(
            source, deadline, runtime.system::time_forever as c_uint64, 0u64 as c_uint64);
        core::atomic_store(&timer->native_source, source as raw void*?,
                           core::memory_order::release);
    }
    return PrepareStatus::R_RUNTIME_DARWIN_TIMER_PREPARE_OK;
}

@safety("R-RUNTIME-TIMER-BIND", "timer is prepared; execution is its committed external task")
extern "C" void r_runtime_darwin_timer_bind_and_activate(raw Timer*? timer,
                                                         raw ExternalHandle*? execution) {
    unsafe {
        if ((timer == null) || (execution == null) || (timer->execution != null)) {
            runtime.system::abort();
        }
        raw Source*? source = source_of(timer);
        if (source == null) {
            runtime.system::abort();
        }
        timer->execution = execution;
        runtime.system::dispatch_set_context(Object {.source = source}, timer as raw void*?);
        runtime.system::dispatch_source_set_event_handler_f(source, timer_event);
        runtime.system::dispatch_source_set_cancel_handler_f(source, timer_cancelled);
        runtime.tasks::r_runtime_task_external_start_ready(execution);
        raw Source*? current = source_of(timer);
        if (current != null) {
            activate_source(timer, current);
        }
    }
}

@safety("R-RUNTIME-TIMER-DISPOSE", "timer is null or initialized")
extern "C" void r_runtime_darwin_timer_dispose_unsubmitted(raw Timer*? timer) {
    unsafe {
        if ((timer == null) || (timer->execution != null)) {
            return;
        }
        raw Source*? source = core::atomic_exchange(&timer->native_source, null,
                                                    core::memory_order::acq_rel) as raw Source*?;
        if (source == null) {
            return;
        }
        activate_source(timer, source);
        runtime.system::dispatch_source_cancel(source);
        runtime.system::dispatch_release(Object {.source = source});
    }
}

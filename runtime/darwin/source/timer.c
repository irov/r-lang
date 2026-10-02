#include "r_runtime_darwin_timer.h"

#include "r_runtime_darwin_event.h"

#include <dispatch/dispatch.h>
#include <limits.h>
#if defined(R_RUNTIME_DARWIN_TIMER_TESTING)
#include <pthread.h>
#endif
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#if defined(R_RUNTIME_DARWIN_TIMER_TESTING)
static pthread_mutex_t cancel_handler_test_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cancel_handler_test_condition = PTHREAD_COND_INITIALIZER;
static RRuntimeDarwinTimer *cancel_handler_test_timer;
static _Bool cancel_handler_test_armed;
static _Bool cancel_handler_test_reached;
static _Bool cancel_handler_test_released;

void r_runtime_darwin_timer_testing_pause_next_cancel_handler(void) {
    if (pthread_mutex_lock(&cancel_handler_test_mutex) != 0) {
        abort();
    }
    if (cancel_handler_test_armed) {
        (void)pthread_mutex_unlock(&cancel_handler_test_mutex);
        abort();
    }
    cancel_handler_test_timer = NULL;
    cancel_handler_test_armed = 1;
    cancel_handler_test_reached = 0;
    cancel_handler_test_released = 0;
    if (pthread_mutex_unlock(&cancel_handler_test_mutex) != 0) {
        abort();
    }
}

void r_runtime_darwin_timer_testing_wait_for_cancel_handler(void) {
    if (pthread_mutex_lock(&cancel_handler_test_mutex) != 0) {
        abort();
    }
    if (!cancel_handler_test_armed) {
        (void)pthread_mutex_unlock(&cancel_handler_test_mutex);
        abort();
    }
    while (!cancel_handler_test_reached) {
        if (pthread_cond_wait(&cancel_handler_test_condition, &cancel_handler_test_mutex) != 0) {
            (void)pthread_mutex_unlock(&cancel_handler_test_mutex);
            abort();
        }
    }
    if (pthread_mutex_unlock(&cancel_handler_test_mutex) != 0) {
        abort();
    }
}

void r_runtime_darwin_timer_testing_cancel_paused_timer(void) {
    RRuntimeDarwinTimer *timer;

    if (pthread_mutex_lock(&cancel_handler_test_mutex) != 0) {
        abort();
    }
    if (!cancel_handler_test_armed || !cancel_handler_test_reached ||
        cancel_handler_test_released || cancel_handler_test_timer == NULL) {
        (void)pthread_mutex_unlock(&cancel_handler_test_mutex);
        abort();
    }
    timer = cancel_handler_test_timer;
    if (pthread_mutex_unlock(&cancel_handler_test_mutex) != 0) {
        abort();
    }
    r_runtime_darwin_timer_cancel(timer);
}

void r_runtime_darwin_timer_testing_release_cancel_handler(void) {
    if (pthread_mutex_lock(&cancel_handler_test_mutex) != 0) {
        abort();
    }
    if (!cancel_handler_test_armed || !cancel_handler_test_reached ||
        cancel_handler_test_released) {
        (void)pthread_mutex_unlock(&cancel_handler_test_mutex);
        abort();
    }
    cancel_handler_test_released = 1;
    if (pthread_cond_broadcast(&cancel_handler_test_condition) != 0) {
        (void)pthread_mutex_unlock(&cancel_handler_test_mutex);
        abort();
    }
    if (pthread_mutex_unlock(&cancel_handler_test_mutex) != 0) {
        abort();
    }
}

static void timer_testing_pause_cancel_handler(RRuntimeDarwinTimer *timer) {
    if (pthread_mutex_lock(&cancel_handler_test_mutex) != 0) {
        abort();
    }
    if (cancel_handler_test_armed && !cancel_handler_test_reached) {
        cancel_handler_test_timer = timer;
        cancel_handler_test_reached = 1;
        if (pthread_cond_broadcast(&cancel_handler_test_condition) != 0) {
            (void)pthread_mutex_unlock(&cancel_handler_test_mutex);
            abort();
        }
        while (!cancel_handler_test_released) {
            if (pthread_cond_wait(&cancel_handler_test_condition, &cancel_handler_test_mutex) !=
                0) {
                (void)pthread_mutex_unlock(&cancel_handler_test_mutex);
                abort();
            }
        }
        cancel_handler_test_timer = NULL;
        cancel_handler_test_armed = 0;
        cancel_handler_test_reached = 0;
        cancel_handler_test_released = 0;
    }
    if (pthread_mutex_unlock(&cancel_handler_test_mutex) != 0) {
        abort();
    }
}
#else
static void timer_testing_pause_cancel_handler(RRuntimeDarwinTimer *timer) {
    (void)timer;
}
#endif

static dispatch_source_t timer_source(const RRuntimeDarwinTimer *timer) {
    return (dispatch_source_t)atomic_load_explicit(&timer->native_source, memory_order_acquire);
}

static void timer_activate_source(RRuntimeDarwinTimer *timer, dispatch_source_t source) {
    _Bool expected = 0;

    if (atomic_compare_exchange_strong_explicit(
            &timer->activated, &expected, 1, memory_order_acq_rel, memory_order_acquire)) {
        dispatch_activate(source);
    }
}

static void timer_event(void *context) {
    RRuntimeDarwinTimer *timer = context;
    const uint64_t event_sequence = r_runtime_darwin_event_sequence_next();

    if (r_runtime_task_external_try_select_completion_at(timer->execution, event_sequence)) {
        r_runtime_darwin_timer_cancel(timer);
    }
}

static void timer_cancelled(void *context) {
    RRuntimeDarwinTimer *timer = context;
    const uintptr_t source_value =
        atomic_exchange_explicit(&timer->native_source, (uintptr_t)0U, memory_order_acq_rel);

    if (source_value == (uintptr_t)0U) {
        return;
    }
    timer_testing_pause_cancel_handler(timer);
    dispatch_release((dispatch_source_t)source_value);
    r_runtime_task_external_acknowledge(timer->execution);
}

void r_runtime_darwin_timer_initialize(RRuntimeDarwinTimer *timer) {
    if (timer == NULL) {
        return;
    }
    atomic_init(&timer->native_source, (uintptr_t)0U);
    atomic_init(&timer->activated, 0);
    timer->execution = NULL;
}

void r_runtime_darwin_timer_move(RRuntimeDarwinTimer *destination, RRuntimeDarwinTimer *source) {
    uintptr_t source_value;

    source_value =
        atomic_exchange_explicit(&source->native_source, (uintptr_t)0U, memory_order_acq_rel);
    atomic_store_explicit(&destination->native_source, source_value, memory_order_release);
}

RRuntimeDarwinTimerPrepareStatus
r_runtime_darwin_timer_prepare(RRuntimeDarwinTimer *timer, uint64_t seconds, uint32_t nanoseconds) {
    const uint64_t nanoseconds_per_second = UINT64_C(1000000000);
    dispatch_queue_t queue;
    dispatch_source_t source;
    dispatch_time_t deadline;
    uint64_t delay;

    if (timer == NULL || timer_source(timer) != NULL || timer->execution != NULL ||
        atomic_load_explicit(&timer->activated, memory_order_acquire) ||
        nanoseconds >= UINT32_C(1000000000)) {
        return R_RUNTIME_DARWIN_TIMER_PREPARE_INVALID;
    }
    if (seconds > ((uint64_t)INT64_MAX / nanoseconds_per_second)) {
        return R_RUNTIME_DARWIN_TIMER_PREPARE_UNAVAILABLE;
    }
    delay = seconds * nanoseconds_per_second;
    if ((uint64_t)nanoseconds > ((uint64_t)INT64_MAX - delay)) {
        return R_RUNTIME_DARWIN_TIMER_PREPARE_UNAVAILABLE;
    }
    delay += (uint64_t)nanoseconds;
    deadline = dispatch_time(DISPATCH_TIME_NOW, (int64_t)delay);
    if (deadline == DISPATCH_TIME_FOREVER) {
        return R_RUNTIME_DARWIN_TIMER_PREPARE_UNAVAILABLE;
    }
    queue = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0U);
    if (queue == NULL) {
        return R_RUNTIME_DARWIN_TIMER_PREPARE_UNAVAILABLE;
    }
    source = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0U, DISPATCH_TIMER_STRICT, queue);
    if (source == NULL) {
        return R_RUNTIME_DARWIN_TIMER_PREPARE_ALLOCATION_FAILED;
    }
    dispatch_source_set_timer(source, deadline, DISPATCH_TIME_FOREVER, 0U);
    atomic_store_explicit(&timer->native_source, (uintptr_t)source, memory_order_release);
    return R_RUNTIME_DARWIN_TIMER_PREPARE_OK;
}

void r_runtime_darwin_timer_bind_and_activate(RRuntimeDarwinTimer *timer,
                                              RRuntimeTaskExternalExecution *execution) {
    dispatch_source_t source;

    if (timer == NULL || execution == NULL || timer->execution != NULL) {
        abort();
    }
    source = timer_source(timer);
    if (source == NULL) {
        abort();
    }
    timer->execution = execution;
    dispatch_set_context(source, timer);
    dispatch_source_set_event_handler_f(source, timer_event);
    dispatch_source_set_cancel_handler_f(source, timer_cancelled);
    r_runtime_task_external_start_ready(execution);
    source = timer_source(timer);
    if (source != NULL) {
        timer_activate_source(timer, source);
    }
}

void r_runtime_darwin_timer_cancel(RRuntimeDarwinTimer *timer) {
    dispatch_source_t source;

    if (timer == NULL || timer->execution == NULL) {
        return;
    }
    source = timer_source(timer);
    if (source == NULL) {
        /* The cancel handler owns acknowledgement once the timer has been bound. */
        return;
    }
    timer_activate_source(timer, source);
    dispatch_source_cancel(source);
}

void r_runtime_darwin_timer_dispose_unsubmitted(RRuntimeDarwinTimer *timer) {
    dispatch_source_t source;

    if (timer == NULL || timer->execution != NULL) {
        return;
    }
    source = (dispatch_source_t)atomic_exchange_explicit(
        &timer->native_source, (uintptr_t)0U, memory_order_acq_rel);
    if (source == NULL) {
        return;
    }
    timer_activate_source(timer, source);
    dispatch_source_cancel(source);
    dispatch_release(source);
}

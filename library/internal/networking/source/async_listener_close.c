#include "r_library_net_internal.h"

#include "r_library_clock_internal.h"
#include "r_runtime_0_1.h"
#include "r_runtime_darwin_event.h"
#include "r_runtime_task.h"
#include "r_runtime_type.h"

#include <dispatch/dispatch.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef enum RLibraryNetListenerCloseOutcome {
    R_LIBRARY_NET_LISTENER_CLOSE_OUTCOME_NONE = 0,
    R_LIBRARY_NET_LISTENER_CLOSE_OUTCOME_SUCCESS,
    R_LIBRARY_NET_LISTENER_CLOSE_OUTCOME_ERROR,
    R_LIBRARY_NET_LISTENER_CLOSE_OUTCOME_DEADLINE,
    R_LIBRARY_NET_LISTENER_CLOSE_OUTCOME_TASK_CANCEL
} RLibraryNetListenerCloseOutcome;

typedef struct RLibraryNetListenerClosePayload RLibraryNetListenerClosePayload;

typedef struct RLibraryNetListenerCloseDeadlineControl {
    _Atomic size_t references;
    RRuntimeAllocator *allocator;
    pthread_mutex_t mutex;
    dispatch_source_t source;
    RLibraryNetListenerClosePayload *payload;
    RStdTimeInstant deadline;
    _Bool activated;
    _Bool cancel_requested;
    _Bool cancel_acknowledged;
    _Bool expiration_recorded;
    uint64_t expiration_sequence;
} RLibraryNetListenerCloseDeadlineControl;

typedef struct RLibraryNetListenerCloseControl {
    RRuntimeAllocator *allocator;
    pthread_mutex_t mutex;
    RRuntimeTaskExternalExecution *execution;
    RStdNetVoidResult *result;
    RStdNetVoidResult selected_result;
    uint64_t outcome_sequence;
    RLibraryNetListenerCloseOutcome outcome;
    _Bool drained;
    _Bool deadline_acknowledged;
    _Bool cancel_reported;
    _Bool finalized;
} RLibraryNetListenerCloseControl;

struct RLibraryNetListenerClosePayload {
    RStdNetTcpListener *staged_listener;
    RStdNetTcpListenerStorage *storage;
    RLibraryNetListenerCloseControl *control;
    RLibraryNetListenerCloseDeadlineControl *deadline_control;
    RStdNetDeadline deadline;
    RStdNetError preexisting_error;
    uint64_t preexisting_sequence;
    _Bool listener_owned;
    _Bool preexisting_failure;
    RStdNetError forced_error;
    uint64_t forced_sequence;
    _Bool forced_deadline;
    _Bool has_forced_error;
};

typedef struct RLibraryNetListenerCloseFinalizeAction {
    RStdNetVoidResult result;
    _Bool ready;
    _Bool completion_selected;
} RLibraryNetListenerCloseFinalizeAction;

static const uint64_t R_LIBRARY_NET_NANOSECONDS_PER_SECOND = UINT64_C(1000000000);
static const uint64_t R_LIBRARY_NET_CLOCK_RETRY_NANOSECONDS = UINT64_C(1000000000);

#if defined(R_LIBRARY_NET_TESTING)
static pthread_mutex_t close_testing_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t close_testing_condition = PTHREAD_COND_INITIALIZER;
static _Bool close_testing_pause_before_drain_selection;
static _Bool close_testing_before_drain_selection_reached;
static _Bool close_testing_pause_after_close_selection;
static _Bool close_testing_after_close_selection_reached;
static _Bool close_testing_pause_before_start_selection;
static _Bool close_testing_before_start_selection_reached;
static RRuntimeTaskExternalExecution *close_testing_prestart_execution;
static _Bool close_testing_deadline_reported;
static _Bool close_testing_cancel_reported;
static unsigned int close_testing_selected_outcome;

_Static_assert((unsigned int)R_LIBRARY_NET_LISTENER_CLOSE_OUTCOME_NONE ==
                   R_LIBRARY_NET_LISTENER_CLOSE_TESTING_OUTCOME_NONE,
               "testing outcome ABI mismatch");
_Static_assert((unsigned int)R_LIBRARY_NET_LISTENER_CLOSE_OUTCOME_SUCCESS ==
                   R_LIBRARY_NET_LISTENER_CLOSE_TESTING_OUTCOME_SUCCESS,
               "testing outcome ABI mismatch");
_Static_assert((unsigned int)R_LIBRARY_NET_LISTENER_CLOSE_OUTCOME_ERROR ==
                   R_LIBRARY_NET_LISTENER_CLOSE_TESTING_OUTCOME_ERROR,
               "testing outcome ABI mismatch");
_Static_assert((unsigned int)R_LIBRARY_NET_LISTENER_CLOSE_OUTCOME_DEADLINE ==
                   R_LIBRARY_NET_LISTENER_CLOSE_TESTING_OUTCOME_DEADLINE,
               "testing outcome ABI mismatch");
_Static_assert((unsigned int)R_LIBRARY_NET_LISTENER_CLOSE_OUTCOME_TASK_CANCEL ==
                   R_LIBRARY_NET_LISTENER_CLOSE_TESTING_OUTCOME_TASK_CANCEL,
               "testing outcome ABI mismatch");

static void close_testing_broadcast(void) {
    if (pthread_cond_broadcast(&close_testing_condition) != 0) {
        abort();
    }
}

static void close_testing_pause(_Bool *pause, _Bool *reached) {
    if (pthread_mutex_lock(&close_testing_mutex) != 0) {
        abort();
    }
    *reached = 1;
    close_testing_broadcast();
    while (*pause) {
        if (pthread_cond_wait(&close_testing_condition, &close_testing_mutex) != 0) {
            abort();
        }
    }
    if (pthread_mutex_unlock(&close_testing_mutex) != 0) {
        abort();
    }
}

static void close_testing_pause_start(RRuntimeTaskExternalExecution *execution) {
    if (pthread_mutex_lock(&close_testing_mutex) != 0) {
        abort();
    }
    close_testing_prestart_execution = execution;
    close_testing_before_start_selection_reached = 1;
    close_testing_broadcast();
    while (close_testing_pause_before_start_selection) {
        if (pthread_cond_wait(&close_testing_condition, &close_testing_mutex) != 0) {
            abort();
        }
    }
    close_testing_prestart_execution = NULL;
    if (pthread_mutex_unlock(&close_testing_mutex) != 0) {
        abort();
    }
}

static void close_testing_record_outcome(RLibraryNetListenerCloseOutcome outcome) {
    if (pthread_mutex_lock(&close_testing_mutex) != 0) {
        abort();
    }
    close_testing_selected_outcome = (unsigned int)outcome;
    close_testing_broadcast();
    if (pthread_mutex_unlock(&close_testing_mutex) != 0) {
        abort();
    }
}

static void close_testing_record_deadline(void) {
    if (pthread_mutex_lock(&close_testing_mutex) != 0) {
        abort();
    }
    close_testing_deadline_reported = 1;
    close_testing_broadcast();
    if (pthread_mutex_unlock(&close_testing_mutex) != 0) {
        abort();
    }
}

static void close_testing_record_cancel(void) {
    if (pthread_mutex_lock(&close_testing_mutex) != 0) {
        abort();
    }
    close_testing_cancel_reported = 1;
    close_testing_broadcast();
    if (pthread_mutex_unlock(&close_testing_mutex) != 0) {
        abort();
    }
}

static void close_testing_set_pause(_Bool *pause, _Bool *reached, _Bool enabled) {
    if (pthread_mutex_lock(&close_testing_mutex) != 0) {
        abort();
    }
    *pause = enabled;
    if (enabled) {
        *reached = 0;
    }
    close_testing_broadcast();
    if (pthread_mutex_unlock(&close_testing_mutex) != 0) {
        abort();
    }
}

static void close_testing_wait(_Bool *value) {
    if (pthread_mutex_lock(&close_testing_mutex) != 0) {
        abort();
    }
    while (!*value) {
        if (pthread_cond_wait(&close_testing_condition, &close_testing_mutex) != 0) {
            abort();
        }
    }
    if (pthread_mutex_unlock(&close_testing_mutex) != 0) {
        abort();
    }
}

void r_library_internal_net_listener_close_testing_reset(void) {
    if (pthread_mutex_lock(&close_testing_mutex) != 0) {
        abort();
    }
    if (close_testing_pause_before_drain_selection || close_testing_pause_after_close_selection ||
        close_testing_pause_before_start_selection || close_testing_prestart_execution != NULL) {
        (void)pthread_mutex_unlock(&close_testing_mutex);
        abort();
    }
    close_testing_before_drain_selection_reached = 0;
    close_testing_after_close_selection_reached = 0;
    close_testing_before_start_selection_reached = 0;
    close_testing_deadline_reported = 0;
    close_testing_cancel_reported = 0;
    close_testing_selected_outcome = R_LIBRARY_NET_LISTENER_CLOSE_TESTING_OUTCOME_NONE;
    if (pthread_mutex_unlock(&close_testing_mutex) != 0) {
        abort();
    }
}

void r_library_internal_net_listener_close_testing_pause_before_drain_selection(_Bool enabled) {
    close_testing_set_pause(&close_testing_pause_before_drain_selection,
                            &close_testing_before_drain_selection_reached,
                            enabled);
}

void r_library_internal_net_listener_close_testing_wait_before_drain_selection(void) {
    close_testing_wait(&close_testing_before_drain_selection_reached);
}

void r_library_internal_net_listener_close_testing_pause_after_close_selection(_Bool enabled) {
    close_testing_set_pause(&close_testing_pause_after_close_selection,
                            &close_testing_after_close_selection_reached,
                            enabled);
}

void r_library_internal_net_listener_close_testing_wait_after_close_selection(void) {
    close_testing_wait(&close_testing_after_close_selection_reached);
}

void r_library_internal_net_listener_close_testing_pause_before_start_selection(_Bool enabled) {
    close_testing_set_pause(&close_testing_pause_before_start_selection,
                            &close_testing_before_start_selection_reached,
                            enabled);
}

void r_library_internal_net_listener_close_testing_wait_before_start_selection(void) {
    close_testing_wait(&close_testing_before_start_selection_reached);
}

_Bool r_library_internal_net_listener_close_testing_wait_prestart_cancellation(void) {
    size_t attempt;

    for (attempt = 0U; attempt < 1000000U; ++attempt) {
        RRuntimeTaskExternalExecution *execution;

        if (pthread_mutex_lock(&close_testing_mutex) != 0) {
            abort();
        }
        execution = close_testing_prestart_execution;
        if (pthread_mutex_unlock(&close_testing_mutex) != 0) {
            abort();
        }
        if (execution != NULL &&
            r_runtime_task_external_cancellation_sequence(execution) != UINT64_C(0)) {
            return 1;
        }
        (void)sched_yield();
    }
    return 0;
}

void r_library_internal_net_listener_close_testing_wait_deadline_reported(void) {
    close_testing_wait(&close_testing_deadline_reported);
}

void r_library_internal_net_listener_close_testing_wait_cancel_reported(void) {
    close_testing_wait(&close_testing_cancel_reported);
}

unsigned int r_library_internal_net_listener_close_testing_selected_outcome(void) {
    unsigned int outcome;

    if (pthread_mutex_lock(&close_testing_mutex) != 0) {
        abort();
    }
    outcome = close_testing_selected_outcome;
    if (pthread_mutex_unlock(&close_testing_mutex) != 0) {
        abort();
    }
    return outcome;
}
#else
static void close_testing_record_outcome(RLibraryNetListenerCloseOutcome outcome) {
    (void)outcome;
}
#endif

_Noreturn static void close_panic(void) {
    r_runtime_panic(R_RUNTIME_PANIC_CONTRACT_VIOLATION, (RRuntimeSourceSpan){0U, 0U, 0U});
}

static RStdNetTaskStartResult start_failure(RRuntimeTaskStartStatus status) {
    RStdNetTaskStartResult result = {0};

    switch (status) {
    case R_RUNTIME_TASK_START_ALLOCATION_FAILED:
        result.error = R_STD_ASYNC_START_REFUSAL();
        return result;
    case R_RUNTIME_TASK_START_RUNTIME_STOPPING:
        result.error = R_STD_ASYNC_START_RUNTIME_STOPPING;
        return result;
    case R_RUNTIME_TASK_START_OK:
    case R_RUNTIME_TASK_START_INVALID:
        close_panic();
    }
    close_panic();
}

static int compare_instant(RStdTimeInstant left, RStdTimeInstant right) {
    if (left.storage_seconds != right.storage_seconds) {
        return left.storage_seconds < right.storage_seconds ? -1 : 1;
    }
    if (left.storage_nanoseconds != right.storage_nanoseconds) {
        return left.storage_nanoseconds < right.storage_nanoseconds ? -1 : 1;
    }
    return 0;
}

static uint64_t clamped_remaining_nanoseconds(RStdTimeInstant deadline, RStdTimeInstant now) {
    uint64_t seconds;
    uint64_t nanoseconds;

    if (compare_instant(deadline, now) <= 0) {
        return 0U;
    }
    seconds = (uint64_t)(deadline.storage_seconds - now.storage_seconds);
    if (deadline.storage_nanoseconds < now.storage_nanoseconds) {
        --seconds;
        nanoseconds = R_LIBRARY_NET_NANOSECONDS_PER_SECOND +
                      (uint64_t)deadline.storage_nanoseconds - (uint64_t)now.storage_nanoseconds;
    } else {
        nanoseconds = (uint64_t)deadline.storage_nanoseconds - (uint64_t)now.storage_nanoseconds;
    }
    if (seconds > (UINT64_MAX - nanoseconds) / R_LIBRARY_NET_NANOSECONDS_PER_SECOND) {
        return UINT64_MAX;
    }
    return seconds * R_LIBRARY_NET_NANOSECONDS_PER_SECOND + nanoseconds;
}

static dispatch_time_t dispatch_target(uint64_t remaining_nanoseconds) {
    uint64_t chunk = remaining_nanoseconds;
    dispatch_time_t target = DISPATCH_TIME_FOREVER;

    if (chunk > (uint64_t)INT64_MAX) {
        chunk = (uint64_t)INT64_MAX;
    }
    while (target == DISPATCH_TIME_FOREVER && chunk != 0U) {
        target = dispatch_time(DISPATCH_TIME_NOW, (int64_t)chunk);
        chunk /= 2U;
    }
    return target == DISPATCH_TIME_FOREVER ? DISPATCH_TIME_NOW : target;
}

static void arm_deadline_timer(dispatch_source_t source, uint64_t remaining_nanoseconds) {
    dispatch_source_set_timer(
        source, dispatch_target(remaining_nanoseconds), DISPATCH_TIME_FOREVER, 0U);
}

static RLibraryNetListenerCloseControl *close_control_create(RRuntimeAllocator *allocator) {
    RLibraryNetListenerCloseControl *control = NULL;

    if (allocator == NULL ||
        r_runtime_allocator_allocate(allocator,
                                     sizeof(*control),
                                     _Alignof(RLibraryNetListenerCloseControl),
                                     (void **)&control) != R_RUNTIME_ALLOCATION_OK) {
        return NULL;
    }
    *control = (RLibraryNetListenerCloseControl){0};
    control->allocator = allocator;
    control->deadline_acknowledged = 1;
    if (pthread_mutex_init(&control->mutex, NULL) != 0) {
        r_runtime_allocator_deallocate(control, _Alignof(RLibraryNetListenerCloseControl));
        return NULL;
    }
    return control;
}

static void close_control_destroy(RLibraryNetListenerCloseControl *control) {
    if (control == NULL) {
        return;
    }
    if (pthread_mutex_destroy(&control->mutex) != 0) {
        close_panic();
    }
    r_runtime_allocator_deallocate(control, _Alignof(RLibraryNetListenerCloseControl));
}

static void close_deadline_control_retain(RLibraryNetListenerCloseDeadlineControl *control) {
    size_t references = atomic_load_explicit(&control->references, memory_order_relaxed);

    for (;;) {
        if (references == 0U || references == SIZE_MAX) {
            close_panic();
        }
        if (atomic_compare_exchange_weak_explicit(&control->references,
                                                  &references,
                                                  references + 1U,
                                                  memory_order_relaxed,
                                                  memory_order_relaxed)) {
            return;
        }
    }
}

static void close_deadline_control_release(RLibraryNetListenerCloseDeadlineControl *control) {
    size_t previous;

    if (control == NULL) {
        return;
    }
    previous = atomic_fetch_sub_explicit(&control->references, 1U, memory_order_acq_rel);
    if (previous == 0U) {
        close_panic();
    }
    if (previous != 1U) {
        return;
    }
    if (control->source != NULL || !control->cancel_requested || !control->cancel_acknowledged ||
        control->payload != NULL || pthread_mutex_destroy(&control->mutex) != 0) {
        close_panic();
    }
    r_runtime_allocator_deallocate(control, _Alignof(RLibraryNetListenerCloseDeadlineControl));
}

static RLibraryNetListenerCloseFinalizeAction
close_finalize_action_locked(RLibraryNetListenerCloseControl *control) {
    RLibraryNetListenerCloseFinalizeAction action = {0};
    const uint64_t cancellation_sequence =
        r_runtime_task_external_cancellation_sequence(control->execution);

    if (control->drained && control->deadline_acknowledged &&
        (cancellation_sequence == UINT64_C(0) || control->cancel_reported) &&
        control->outcome != R_LIBRARY_NET_LISTENER_CLOSE_OUTCOME_NONE && !control->finalized) {
        control->finalized = 1;
        action.result = control->selected_result;
        action.ready = 1;
        action.completion_selected =
            control->outcome != R_LIBRARY_NET_LISTENER_CLOSE_OUTCOME_TASK_CANCEL;
    }
    return action;
}

static void close_finalize(RLibraryNetListenerClosePayload *payload,
                           RLibraryNetListenerCloseFinalizeAction action) {
    RLibraryNetListenerCloseControl *control;
    RRuntimeTaskExternalExecution *execution;

    if (!action.ready) {
        return;
    }
    control = payload->control;
    execution = control->execution;
    if (action.completion_selected) {
        *control->result = action.result;
    }
    r_runtime_task_external_acknowledge(execution);
}

static _Bool close_select_completion(RLibraryNetListenerClosePayload *payload,
                                     RLibraryNetListenerCloseOutcome outcome,
                                     RStdNetVoidResult result,
                                     uint64_t event_sequence) {
    RLibraryNetListenerCloseControl *control = payload->control;
    uint64_t cancellation_sequence;
    RLibraryNetListenerCloseOutcome recorded_outcome;
    _Bool selected = 1;

    if (control == NULL || control->execution == NULL || event_sequence == UINT64_C(0) ||
        outcome == R_LIBRARY_NET_LISTENER_CLOSE_OUTCOME_NONE ||
        outcome == R_LIBRARY_NET_LISTENER_CLOSE_OUTCOME_TASK_CANCEL ||
        pthread_mutex_lock(&control->mutex) != 0) {
        close_panic();
    }
    if (control->outcome_sequence != UINT64_C(0) && control->outcome_sequence <= event_sequence) {
        if (pthread_mutex_unlock(&control->mutex) != 0) {
            close_panic();
        }
        return 0;
    }
    if (control->outcome == R_LIBRARY_NET_LISTENER_CLOSE_OUTCOME_NONE ||
        control->outcome == R_LIBRARY_NET_LISTENER_CLOSE_OUTCOME_TASK_CANCEL) {
        selected =
            r_runtime_task_external_try_select_completion_at(control->execution, event_sequence);
        if (!selected) {
            cancellation_sequence =
                r_runtime_task_external_cancellation_sequence(control->execution);
            if (cancellation_sequence == UINT64_C(0)) {
                (void)pthread_mutex_unlock(&control->mutex);
                close_panic();
            }
            if (control->outcome_sequence == UINT64_C(0) ||
                cancellation_sequence < control->outcome_sequence) {
                control->outcome = R_LIBRARY_NET_LISTENER_CLOSE_OUTCOME_TASK_CANCEL;
                control->outcome_sequence = cancellation_sequence;
            }
            recorded_outcome = control->outcome;
            if (pthread_mutex_unlock(&control->mutex) != 0) {
                close_panic();
            }
            close_testing_record_outcome(recorded_outcome);
            return 0;
        }
    }
    control->outcome = outcome;
    control->outcome_sequence = event_sequence;
    control->selected_result = result;
    recorded_outcome = control->outcome;
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        close_panic();
    }
    close_testing_record_outcome(recorded_outcome);
    return selected;
}

static void close_select_task_cancel(RLibraryNetListenerClosePayload *payload) {
    RLibraryNetListenerCloseControl *control = payload->control;
    const uint64_t event_sequence =
        r_runtime_task_external_cancellation_sequence(control->execution);
    RLibraryNetListenerCloseOutcome recorded_outcome;

    if (event_sequence == UINT64_C(0) || pthread_mutex_lock(&control->mutex) != 0) {
        close_panic();
    }
    if (control->outcome_sequence == UINT64_C(0) || event_sequence < control->outcome_sequence) {
        control->outcome = R_LIBRARY_NET_LISTENER_CLOSE_OUTCOME_TASK_CANCEL;
        control->outcome_sequence = event_sequence;
    }
    recorded_outcome = control->outcome;
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        close_panic();
    }
    close_testing_record_outcome(recorded_outcome);
}

static void close_acknowledge_replaced_prestart_cancel(RLibraryNetListenerClosePayload *payload) {
    RLibraryNetListenerCloseControl *control = payload->control;
    const uint64_t cancellation_sequence =
        r_runtime_task_external_cancellation_sequence(control->execution);

    if (cancellation_sequence == UINT64_C(0)) {
        return;
    }
    if (pthread_mutex_lock(&control->mutex) != 0) {
        close_panic();
    }
    if (control->outcome != R_LIBRARY_NET_LISTENER_CLOSE_OUTCOME_TASK_CANCEL) {
        if (control->outcome == R_LIBRARY_NET_LISTENER_CLOSE_OUTCOME_NONE ||
            control->cancel_reported) {
            (void)pthread_mutex_unlock(&control->mutex);
            close_panic();
        }
        control->cancel_reported = 1;
    }
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        close_panic();
    }
}

static void close_deadline_expired(RLibraryNetListenerClosePayload *payload,
                                   uint64_t event_sequence) {
    const RStdNetVoidResult result = {
        .r_tag = UINT32_C(1),
        .r_payload.r_error_00000001 =
            {
                R_STD_NET_ERROR_TIMED_OUT,
                INT64_C(0),
            },
    };

    (void)close_select_completion(
        payload, R_LIBRARY_NET_LISTENER_CLOSE_OUTCOME_DEADLINE, result, event_sequence);
#if defined(R_LIBRARY_NET_TESTING)
    close_testing_record_deadline();
#endif
}

static void close_deadline_timer_fired(void *context) {
    RLibraryNetListenerCloseDeadlineControl *control = context;
    RLibraryNetListenerClosePayload *payload = NULL;
    RStdTimeInstantResult now;
    uint64_t event_sequence = UINT64_C(0);

    now = r_library_internal_time_monotonic_now(r_library_internal_time_darwin_clock_hooks());
    if (pthread_mutex_lock(&control->mutex) != 0) {
        close_panic();
    }
    if (!control->cancel_requested && !control->expiration_recorded && control->source != NULL) {
        if (!now.is_ok || now.value.storage_seconds < 0 ||
            now.value.storage_nanoseconds >= R_STD_TIME_NANOSECONDS_PER_SECOND) {
            arm_deadline_timer(control->source, R_LIBRARY_NET_CLOCK_RETRY_NANOSECONDS);
        } else if (compare_instant(now.value, control->deadline) >= 0) {
            event_sequence = r_runtime_darwin_event_sequence_next();
            control->expiration_recorded = 1;
            control->expiration_sequence = event_sequence;
            payload = control->payload;
            if (payload == NULL) {
                (void)pthread_mutex_unlock(&control->mutex);
                close_panic();
            }
        } else {
            arm_deadline_timer(control->source,
                               clamped_remaining_nanoseconds(control->deadline, now.value));
        }
    }
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        close_panic();
    }
    if (payload != NULL) {
        close_deadline_expired(payload, event_sequence);
    }
}

static void close_deadline_acknowledged(RLibraryNetListenerClosePayload *payload) {
    RLibraryNetListenerCloseControl *control = payload->control;
    RLibraryNetListenerCloseFinalizeAction action;

    if (pthread_mutex_lock(&control->mutex) != 0) {
        close_panic();
    }
    if (control->deadline_acknowledged) {
        (void)pthread_mutex_unlock(&control->mutex);
        close_panic();
    }
    control->deadline_acknowledged = 1;
    action = close_finalize_action_locked(control);
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        close_panic();
    }
    close_finalize(payload, action);
}

static void close_deadline_timer_cancelled(void *context) {
    RLibraryNetListenerCloseDeadlineControl *control = context;
    RLibraryNetListenerClosePayload *payload;

    if (pthread_mutex_lock(&control->mutex) != 0) {
        close_panic();
    }
    if (!control->cancel_requested || control->cancel_acknowledged) {
        (void)pthread_mutex_unlock(&control->mutex);
        close_panic();
    }
    payload = control->payload;
    control->payload = NULL;
    control->cancel_acknowledged = 1;
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        close_panic();
    }
    if (payload != NULL) {
        close_deadline_acknowledged(payload);
    }
    close_deadline_control_release(control);
}

static RLibraryNetListenerCloseDeadlineControl *
close_deadline_control_create(RRuntimeAllocator *allocator, RStdTimeInstant deadline) {
    RLibraryNetListenerCloseDeadlineControl *control = NULL;
    dispatch_queue_t queue;
    dispatch_source_t source;

    if (allocator == NULL ||
        r_runtime_allocator_allocate(allocator,
                                     sizeof(*control),
                                     _Alignof(RLibraryNetListenerCloseDeadlineControl),
                                     (void **)&control) != R_RUNTIME_ALLOCATION_OK) {
        return NULL;
    }
    (void)memset(control, 0, sizeof(*control));
    control->allocator = allocator;
    control->deadline = deadline;
    if (pthread_mutex_init(&control->mutex, NULL) != 0) {
        r_runtime_allocator_deallocate(control, _Alignof(RLibraryNetListenerCloseDeadlineControl));
        return NULL;
    }
    atomic_init(&control->references, 1U);
    queue = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0U);
    if (queue == NULL) {
        control->cancel_requested = 1;
        control->cancel_acknowledged = 1;
        close_deadline_control_release(control);
        return NULL;
    }
    source = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0U, DISPATCH_TIMER_STRICT, queue);
    if (source == NULL) {
        control->cancel_requested = 1;
        control->cancel_acknowledged = 1;
        close_deadline_control_release(control);
        return NULL;
    }
    close_deadline_control_retain(control);
    control->source = source;
    dispatch_set_context(source, control);
    dispatch_source_set_event_handler_f(source, close_deadline_timer_fired);
    dispatch_source_set_cancel_handler_f(source, close_deadline_timer_cancelled);
    return control;
}

static void close_deadline_control_bind_and_check(RLibraryNetListenerCloseDeadlineControl *control,
                                                  RLibraryNetListenerClosePayload *payload) {
    RStdTimeInstantResult now;
    uint64_t event_sequence = UINT64_C(0);

    if (control == NULL || payload == NULL) {
        close_panic();
    }
    now = r_library_internal_time_monotonic_now(r_library_internal_time_darwin_clock_hooks());
    if (pthread_mutex_lock(&control->mutex) != 0) {
        close_panic();
    }
    if (control->source == NULL || control->activated || control->cancel_requested ||
        control->payload != NULL || control->expiration_recorded) {
        (void)pthread_mutex_unlock(&control->mutex);
        close_panic();
    }
    control->payload = payload;
    if (now.is_ok && now.value.storage_seconds >= 0 &&
        now.value.storage_nanoseconds < R_STD_TIME_NANOSECONDS_PER_SECOND &&
        compare_instant(now.value, control->deadline) >= 0) {
        event_sequence = r_runtime_darwin_event_sequence_next();
        control->expiration_recorded = 1;
        control->expiration_sequence = event_sequence;
    }
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        close_panic();
    }
    if (event_sequence != UINT64_C(0)) {
        close_deadline_expired(payload, event_sequence);
    }
}

static void close_deadline_control_activate(RLibraryNetListenerCloseDeadlineControl *control) {
    RStdTimeInstantResult now;

    if (control == NULL) {
        close_panic();
    }
    now = r_library_internal_time_monotonic_now(r_library_internal_time_darwin_clock_hooks());
    if (pthread_mutex_lock(&control->mutex) != 0) {
        close_panic();
    }
    if (control->source == NULL || control->activated || control->cancel_requested ||
        control->payload == NULL) {
        (void)pthread_mutex_unlock(&control->mutex);
        close_panic();
    }
    if (control->expiration_recorded) {
        dispatch_source_set_timer(
            control->source, DISPATCH_TIME_FOREVER, DISPATCH_TIME_FOREVER, 0U);
    } else if (!now.is_ok || now.value.storage_seconds < 0 ||
               now.value.storage_nanoseconds >= R_STD_TIME_NANOSECONDS_PER_SECOND) {
        arm_deadline_timer(control->source, R_LIBRARY_NET_CLOCK_RETRY_NANOSECONDS);
    } else {
        arm_deadline_timer(control->source,
                           clamped_remaining_nanoseconds(control->deadline, now.value));
    }
    control->activated = 1;
    dispatch_activate(control->source);
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        close_panic();
    }
}

static void close_deadline_control_stop(RLibraryNetListenerCloseDeadlineControl *control) {
    dispatch_source_t source;
    _Bool activated;

    if (control == NULL || pthread_mutex_lock(&control->mutex) != 0) {
        close_panic();
    }
    if (control->cancel_requested || control->source == NULL) {
        (void)pthread_mutex_unlock(&control->mutex);
        close_panic();
    }
    source = control->source;
    activated = control->activated;
    control->source = NULL;
    control->cancel_requested = 1;
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        close_panic();
    }
    if (!activated) {
        dispatch_activate(source);
    }
    dispatch_source_cancel(source);
    dispatch_release(source);
}

static void close_deadline_control_abort(RLibraryNetListenerCloseDeadlineControl *control) {
    if (control == NULL) {
        return;
    }
    close_deadline_control_stop(control);
    close_deadline_control_release(control);
}

static void close_payload_move(void *destination_pointer, void *source_pointer) {
    RLibraryNetListenerClosePayload *destination = destination_pointer;
    RLibraryNetListenerClosePayload *source = source_pointer;

    if (source->staged_listener == NULL || source->storage == NULL ||
        source->staged_listener->storage != source->storage || source->control == NULL) {
        close_panic();
    }
    *destination = *source;
    destination->staged_listener = NULL;
    destination->listener_owned = 1;
    source->staged_listener->storage = NULL;
    source->storage = NULL;
    source->control = NULL;
    source->deadline_control = NULL;
}

static void close_payload_drop(void *value) {
    RLibraryNetListenerClosePayload *payload = value;

    if (payload->listener_owned) {
        r_library_internal_net_handle_release(payload->storage == NULL ? NULL
                                                                       : &payload->storage->handle);
        payload->storage = NULL;
        payload->listener_owned = 0;
    }
    close_deadline_control_release(payload->deadline_control);
    payload->deadline_control = NULL;
    close_control_destroy(payload->control);
    payload->control = NULL;
}

static void close_release_listener(RLibraryNetListenerClosePayload *payload) {
    RStdNetTcpListenerStorage *storage;

    if (!payload->listener_owned || payload->storage == NULL) {
        close_panic();
    }
    storage = payload->storage;
    payload->storage = NULL;
    payload->listener_owned = 0;
    r_library_internal_net_handle_release(&storage->handle);
}

static void close_operations_drained(void *context) {
    RLibraryNetListenerClosePayload *payload = context;
    RLibraryNetListenerCloseControl *control = payload->control;
    RLibraryNetListenerCloseFinalizeAction action;
    RStdNetVoidResult close_result = {0};
    RLibraryNetListenerCloseOutcome close_outcome = R_LIBRARY_NET_LISTENER_CLOSE_OUTCOME_SUCCESS;
    uint64_t event_sequence;
    int descriptor;
    int close_status;
    int native_error;

#if defined(R_LIBRARY_NET_TESTING)
    close_testing_pause(&close_testing_pause_before_drain_selection,
                        &close_testing_before_drain_selection_reached);
#endif
    descriptor = r_library_internal_net_tcp_listener_take_close_descriptor(payload->storage);
    errno = 0;
    close_status = close(descriptor);
    native_error = close_status == 0 ? 0 : errno;
    event_sequence = r_runtime_darwin_event_sequence_next();
    if (native_error != 0) {
        close_outcome = R_LIBRARY_NET_LISTENER_CLOSE_OUTCOME_ERROR;
        close_result = (RStdNetVoidResult){
            .r_tag = UINT32_C(1),
            .r_payload.r_error_00000001 = r_library_internal_net_error_from_native(native_error),
        };
    }
    (void)close_select_completion(payload, close_outcome, close_result, event_sequence);
#if defined(R_LIBRARY_NET_TESTING)
    close_testing_pause(&close_testing_pause_after_close_selection,
                        &close_testing_after_close_selection_reached);
#endif
    if (payload->deadline_control != NULL) {
        close_deadline_control_stop(payload->deadline_control);
    }
    close_release_listener(payload);

    if (pthread_mutex_lock(&control->mutex) != 0) {
        close_panic();
    }
    if (control->drained || control->finalized) {
        (void)pthread_mutex_unlock(&control->mutex);
        close_panic();
    }
    control->drained = 1;
    action = close_finalize_action_locked(control);
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        close_panic();
    }
    close_finalize(payload, action);
}

static void close_external_cancel(RRuntimeTaskExternalExecution *execution, void *payload_pointer) {
    RLibraryNetListenerClosePayload *payload = payload_pointer;
    RLibraryNetListenerCloseControl *control = payload->control;
    RLibraryNetListenerCloseFinalizeAction action;

    if (control == NULL || control->execution != execution ||
        r_runtime_task_external_cancellation_sequence(execution) == UINT64_C(0)) {
        close_panic();
    }
    close_select_task_cancel(payload);
#if defined(R_LIBRARY_NET_TESTING)
    close_testing_record_cancel();
#endif
    if (pthread_mutex_lock(&control->mutex) != 0) {
        close_panic();
    }
    if (control->cancel_reported) {
        (void)pthread_mutex_unlock(&control->mutex);
        close_panic();
    }
    control->cancel_reported = 1;
    action = close_finalize_action_locked(control);
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        close_panic();
    }
    close_finalize(payload, action);
}

static void close_external_start(RRuntimeTaskExternalExecution *execution,
                                 void *payload_pointer,
                                 void *result_pointer) {
    RLibraryNetListenerClosePayload *payload = payload_pointer;
    RLibraryNetListenerCloseControl *control = payload->control;

    if (control == NULL || payload->storage == NULL || !payload->listener_owned) {
        close_panic();
    }
    control->execution = execution;
    control->result = result_pointer;
#if defined(R_LIBRARY_NET_TESTING)
    close_testing_pause_start(execution);
#endif
    if (payload->preexisting_failure) {
        (void)close_select_completion(payload,
                                      R_LIBRARY_NET_LISTENER_CLOSE_OUTCOME_ERROR,
                                      (RStdNetVoidResult){
                                          .r_tag = UINT32_C(1),
                                          .r_payload.r_error_00000001 = payload->preexisting_error,
                                      },
                                      payload->preexisting_sequence);
    } else if (payload->has_forced_error) {
        (void)close_select_completion(payload,
                                      payload->forced_deadline
                                          ? R_LIBRARY_NET_LISTENER_CLOSE_OUTCOME_DEADLINE
                                          : R_LIBRARY_NET_LISTENER_CLOSE_OUTCOME_ERROR,
                                      (RStdNetVoidResult){
                                          .r_tag = UINT32_C(1),
                                          .r_payload.r_error_00000001 = payload->forced_error,
                                      },
                                      payload->forced_sequence);
#if defined(R_LIBRARY_NET_TESTING)
        if (payload->forced_deadline) {
            close_testing_record_deadline();
        }
#endif
    }
    if (payload->deadline_control != NULL) {
        close_deadline_control_bind_and_check(payload->deadline_control, payload);
    }
    close_acknowledge_replaced_prestart_cancel(payload);
    if (!r_library_internal_net_tcp_listener_mark_closing(payload->storage)) {
        close_panic();
    }
    r_runtime_task_external_start_ready(execution);
    if (payload->deadline_control != NULL) {
        close_deadline_control_activate(payload->deadline_control);
    }
    r_library_internal_net_tcp_listener_drain_accepts(
        payload->storage, close_operations_drained, payload);
}

static void close_preflight(RLibraryNetListenerClosePayload *payload) {
    RLibraryNetHandleStorage *handle = &payload->storage->handle;
    int descriptor_status;
    int native_error;

    if (pthread_mutex_lock(&handle->mutex) != 0) {
        close_panic();
    }
    if (handle->kind != R_LIBRARY_NET_HANDLE_TCP_LISTENER || handle->terminal ||
        handle->close_reserved || handle->descriptor < 0) {
        (void)pthread_mutex_unlock(&handle->mutex);
        close_panic();
    }
    do {
        errno = 0;
        descriptor_status = fcntl(handle->descriptor, F_GETFD);
        native_error = descriptor_status < 0 ? errno : 0;
    } while (descriptor_status < 0 && native_error == EINTR);
    if (descriptor_status < 0) {
        payload->preexisting_failure = 1;
        payload->preexisting_error = r_library_internal_net_error_from_native(native_error);
        payload->preexisting_sequence = r_runtime_darwin_event_sequence_next();
    }
    if (pthread_mutex_unlock(&handle->mutex) != 0) {
        close_panic();
    }
}

static void close_check_deadline(RLibraryNetListenerClosePayload *payload) {
    RStdNetError deadline_error = {0};
    uint64_t ignored_timeout = 0U;
    RLibraryNetDeadlineStatus deadline_status;

    if (payload->preexisting_failure) {
        return;
    }
    deadline_status = r_library_internal_net_deadline_timeout(
        payload->deadline, &ignored_timeout, &deadline_error);
    if (deadline_status != R_LIBRARY_NET_DEADLINE_READY) {
        payload->has_forced_error = 1;
        payload->forced_deadline = deadline_status == R_LIBRARY_NET_DEADLINE_EXPIRED;
        payload->forced_error = deadline_error;
        payload->forced_sequence = r_runtime_darwin_event_sequence_next();
    }
}

RStdNetTaskStartResult r_library_internal_net_tcp_listener_close(RStdNetTcpListener *listener,
                                                                 RStdNetDeadline deadline) {
    const RRuntimeTypeInfo payload_type = {
        sizeof(RLibraryNetListenerClosePayload),
        _Alignof(RLibraryNetListenerClosePayload),
        close_payload_move,
        close_payload_drop,
    };
    const RRuntimeTypeInfo result_type = {
        sizeof(RStdNetVoidResult),
        _Alignof(RStdNetVoidResult),
        NULL,
        NULL,
    };
    RLibraryNetListenerClosePayload payload = {0};
    RRuntimeTaskPrepareResult task_preparation;
    RRuntimeTaskStartResult started;
    RRuntimeAllocator *allocator;
    RStdNetTaskStartResult result = {0};

    payload.staged_listener = listener;
    payload.storage = listener->storage;
    payload.deadline = deadline;
    close_preflight(&payload);
    close_check_deadline(&payload);
    task_preparation = r_runtime_task_external_start_prepare(
        payload_type, result_type, close_external_start, close_external_cancel);
    if (task_preparation.status != R_RUNTIME_TASK_START_OK) {
        return start_failure(task_preparation.status);
    }
    allocator = r_runtime_task_start_allocator(task_preparation.transaction);
    if (allocator == NULL) {
        r_runtime_task_start_abort(&task_preparation.transaction);
        close_panic();
    }
    payload.control = close_control_create(allocator);
    if (payload.control == NULL) {
        r_runtime_task_start_abort(&task_preparation.transaction);
        return start_failure(R_RUNTIME_TASK_START_ALLOCATION_FAILED);
    }
    if (!payload.preexisting_failure && !payload.has_forced_error && deadline.has_value) {
        payload.deadline_control = close_deadline_control_create(allocator, deadline.value);
        if (payload.deadline_control == NULL) {
            close_control_destroy(payload.control);
            payload.control = NULL;
            r_runtime_task_start_abort(&task_preparation.transaction);
            return start_failure(R_RUNTIME_TASK_START_ALLOCATION_FAILED);
        }
        payload.control->deadline_acknowledged = 0;
    }
    started = r_runtime_task_start_commit(&task_preparation.transaction, &payload);
    if (started.status != R_RUNTIME_TASK_START_OK) {
        close_deadline_control_abort(payload.deadline_control);
        payload.deadline_control = NULL;
        close_control_destroy(payload.control);
        payload.control = NULL;
        return start_failure(started.status);
    }
    result.is_ok = 1;
    result.task = started.task;
    return result;
}

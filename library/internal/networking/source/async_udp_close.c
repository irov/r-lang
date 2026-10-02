#include "r_library_net_internal.h"

#include "r_runtime_0_1.h"
#include "r_runtime_darwin_event.h"
#include "r_runtime_task.h"
#include "r_runtime_type.h"

#include <dispatch/dispatch.h>
#include <errno.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

typedef enum RLibraryNetUdpCloseOutcome {
    R_LIBRARY_NET_UDP_CLOSE_OUTCOME_NONE = 0,
    R_LIBRARY_NET_UDP_CLOSE_OUTCOME_SUCCESS,
    R_LIBRARY_NET_UDP_CLOSE_OUTCOME_ERROR,
    R_LIBRARY_NET_UDP_CLOSE_OUTCOME_DEADLINE
} RLibraryNetUdpCloseOutcome;

typedef struct RLibraryNetUdpClosePayload RLibraryNetUdpClosePayload;

typedef struct RLibraryNetUdpCloseControl {
    RRuntimeAllocator *allocator;
    pthread_mutex_t mutex;
    dispatch_source_t timer;
    RLibraryNetUdpClosePayload *payload;
    RRuntimeTaskExternalExecution *execution;
    RStdNetVoidResult *result;
    RStdNetVoidResult selected_result;
    uint64_t outcome_sequence;
    RLibraryNetUdpCloseOutcome outcome;
    _Bool timer_activated;
    _Bool timer_acknowledged;
    _Bool drained;
    _Bool cancel_reported;
    _Bool finalized;
} RLibraryNetUdpCloseControl;

struct RLibraryNetUdpClosePayload {
    RStdNetUdpSocket *staged_socket;
    RStdNetUdpSocketStorage *storage;
    RLibraryNetUdpCloseControl *control;
    RStdNetError preexisting_error;
    RStdNetError forced_error;
    uint64_t preexisting_sequence;
    uint64_t forced_sequence;
    _Bool socket_owned;
    _Bool preexisting_failure;
    _Bool has_forced_error;
    _Bool forced_deadline;
};

typedef struct RLibraryNetUdpCloseFinalizeAction {
    RStdNetVoidResult result;
    _Bool ready;
    _Bool completion_selected;
} RLibraryNetUdpCloseFinalizeAction;

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

static RLibraryNetUdpCloseControl *close_control_create(RRuntimeAllocator *allocator,
                                                        uint64_t timeout_nanoseconds) {
    RLibraryNetUdpCloseControl *control = NULL;
    dispatch_queue_t queue;
    dispatch_time_t target;

    if (allocator == NULL ||
        r_runtime_allocator_allocate(
            allocator, sizeof(*control), _Alignof(RLibraryNetUdpCloseControl), (void **)&control) !=
            R_RUNTIME_ALLOCATION_OK) {
        return NULL;
    }
    *control = (RLibraryNetUdpCloseControl){0};
    control->allocator = allocator;
    control->timer_acknowledged = timeout_nanoseconds == 0U;
    if (pthread_mutex_init(&control->mutex, NULL) != 0) {
        r_runtime_allocator_deallocate(control, _Alignof(RLibraryNetUdpCloseControl));
        return NULL;
    }
    if (timeout_nanoseconds == 0U) {
        return control;
    }
    queue = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0U);
    target = dispatch_time(DISPATCH_TIME_NOW, (int64_t)timeout_nanoseconds);
    if (queue == NULL || target == DISPATCH_TIME_FOREVER) {
        (void)pthread_mutex_destroy(&control->mutex);
        r_runtime_allocator_deallocate(control, _Alignof(RLibraryNetUdpCloseControl));
        return NULL;
    }
    control->timer =
        dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0U, DISPATCH_TIMER_STRICT, queue);
    if (control->timer == NULL) {
        (void)pthread_mutex_destroy(&control->mutex);
        r_runtime_allocator_deallocate(control, _Alignof(RLibraryNetUdpCloseControl));
        return NULL;
    }
    dispatch_source_set_timer(control->timer, target, DISPATCH_TIME_FOREVER, 0U);
    return control;
}

static void close_control_destroy(RLibraryNetUdpCloseControl *control) {
    dispatch_source_t timer;

    if (control == NULL) {
        return;
    }
    timer = control->timer;
    control->timer = NULL;
    if (timer != NULL) {
        if (control->timer_activated || control->payload != NULL || control->execution != NULL) {
            close_panic();
        }
        dispatch_source_set_event_handler_f(timer, NULL);
        dispatch_source_set_cancel_handler_f(timer, NULL);
        dispatch_set_context(timer, NULL);
        dispatch_source_cancel(timer);
        dispatch_activate(timer);
        dispatch_release(timer);
    } else if (!control->timer_acknowledged) {
        close_panic();
    }
    if (pthread_mutex_destroy(&control->mutex) != 0) {
        close_panic();
    }
    r_runtime_allocator_deallocate(control, _Alignof(RLibraryNetUdpCloseControl));
}

static void close_payload_move(void *destination_pointer, void *source_pointer) {
    RLibraryNetUdpClosePayload *destination = destination_pointer;
    RLibraryNetUdpClosePayload *source = source_pointer;

    if (source->staged_socket == NULL || source->storage == NULL || source->control == NULL ||
        source->staged_socket->storage != source->storage) {
        close_panic();
    }
    *destination = *source;
    destination->staged_socket = NULL;
    destination->socket_owned = 1;
    source->staged_socket->storage = NULL;
    source->storage = NULL;
    source->control = NULL;
}

static void close_payload_drop(void *value) {
    RLibraryNetUdpClosePayload *payload = value;

    if (payload->socket_owned) {
        if (payload->storage == NULL) {
            close_panic();
        }
        r_library_internal_net_handle_release(&payload->storage->handle);
        payload->storage = NULL;
        payload->socket_owned = 0;
    }
    close_control_destroy(payload->control);
    payload->control = NULL;
}

static RLibraryNetUdpCloseFinalizeAction
close_finalize_action_locked(RLibraryNetUdpCloseControl *control) {
    RLibraryNetUdpCloseFinalizeAction action = {0};
    const uint64_t cancellation_sequence =
        r_runtime_task_external_cancellation_sequence(control->execution);

    if (control->drained && control->timer_acknowledged &&
        (cancellation_sequence == UINT64_C(0) || control->cancel_reported) &&
        control->outcome != R_LIBRARY_NET_UDP_CLOSE_OUTCOME_NONE && !control->finalized) {
        control->finalized = 1;
        action.result = control->selected_result;
        action.ready = 1;
        action.completion_selected = cancellation_sequence == UINT64_C(0) ||
                                     control->outcome_sequence < cancellation_sequence;
        if (action.completion_selected && !r_runtime_task_external_try_select_completion_at(
                                              control->execution, control->outcome_sequence)) {
            close_panic();
        }
    }
    return action;
}

static void close_finalize(RLibraryNetUdpCloseControl *control,
                           RLibraryNetUdpCloseFinalizeAction action) {
    if (!action.ready) {
        return;
    }
    if (action.completion_selected) {
        *control->result = action.result;
    }
    r_runtime_task_external_acknowledge(control->execution);
}

static void close_select_completion(RLibraryNetUdpCloseControl *control,
                                    RLibraryNetUdpCloseOutcome outcome,
                                    RStdNetVoidResult result,
                                    uint64_t event_sequence) {
    if (control == NULL || control->execution == NULL || event_sequence == UINT64_C(0) ||
        outcome == R_LIBRARY_NET_UDP_CLOSE_OUTCOME_NONE ||
        pthread_mutex_lock(&control->mutex) != 0) {
        close_panic();
    }
    if (control->outcome_sequence == UINT64_C(0) || event_sequence < control->outcome_sequence) {
        control->outcome = outcome;
        control->outcome_sequence = event_sequence;
        control->selected_result = result;
    }
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        close_panic();
    }
}

static void close_cancel_timer_locked(RLibraryNetUdpCloseControl *control) {
    if (control->timer != NULL) {
        dispatch_source_cancel(control->timer);
    }
}

static void close_timer_fired(void *context_pointer) {
    RLibraryNetUdpCloseControl *control = context_pointer;
    const uint64_t event_sequence = r_runtime_darwin_event_sequence_next();

    close_select_completion(control,
                            R_LIBRARY_NET_UDP_CLOSE_OUTCOME_DEADLINE,
                            (RStdNetVoidResult){
                                .r_tag = UINT32_C(1),
                                .r_payload.r_error_00000001 =
                                    {
                                        R_STD_NET_ERROR_TIMED_OUT,
                                        INT64_C(0),
                                    },
                            },
                            event_sequence);
    if (pthread_mutex_lock(&control->mutex) != 0) {
        close_panic();
    }
    close_cancel_timer_locked(control);
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        close_panic();
    }
}

static void close_timer_cancelled(void *context_pointer) {
    RLibraryNetUdpCloseControl *control = context_pointer;
    RLibraryNetUdpCloseFinalizeAction action;
    dispatch_source_t timer;

    if (pthread_mutex_lock(&control->mutex) != 0) {
        close_panic();
    }
    timer = control->timer;
    if (timer == NULL || control->timer_acknowledged) {
        (void)pthread_mutex_unlock(&control->mutex);
        close_panic();
    }
    control->timer = NULL;
    control->timer_acknowledged = 1;
    action = close_finalize_action_locked(control);
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        close_panic();
    }
    dispatch_release(timer);
    close_finalize(control, action);
}

static void close_drained(void *context_pointer) {
    RLibraryNetUdpClosePayload *payload = context_pointer;
    RLibraryNetUdpCloseControl *control = payload->control;
    RLibraryNetUdpCloseFinalizeAction action;
    RStdNetVoidResult close_result = {0};
    RLibraryNetUdpCloseOutcome close_outcome = R_LIBRARY_NET_UDP_CLOSE_OUTCOME_SUCCESS;
    RStdNetUdpSocketStorage *storage;
    uint64_t close_sequence;
    int descriptor;

    if (!payload->socket_owned || payload->storage == NULL || control == NULL) {
        close_panic();
    }
    storage = payload->storage;
    descriptor = r_library_internal_net_udp_socket_take_close_descriptor(storage);
    errno = 0;
    if (close(descriptor) != 0) {
        close_result = (RStdNetVoidResult){
            .r_tag = UINT32_C(1),
            .r_payload.r_error_00000001 = r_library_internal_net_error_from_native(errno),
        };
        close_outcome = R_LIBRARY_NET_UDP_CLOSE_OUTCOME_ERROR;
    }
    payload->storage = NULL;
    payload->socket_owned = 0;
    r_library_internal_net_handle_release(&storage->handle);
    close_sequence = r_runtime_darwin_event_sequence_next();
    if (payload->preexisting_failure) {
        close_select_completion(control,
                                R_LIBRARY_NET_UDP_CLOSE_OUTCOME_ERROR,
                                (RStdNetVoidResult){
                                    .r_tag = UINT32_C(1),
                                    .r_payload.r_error_00000001 = payload->preexisting_error,
                                },
                                payload->preexisting_sequence);
    } else if (payload->has_forced_error) {
        close_select_completion(control,
                                payload->forced_deadline ? R_LIBRARY_NET_UDP_CLOSE_OUTCOME_DEADLINE
                                                         : R_LIBRARY_NET_UDP_CLOSE_OUTCOME_ERROR,
                                (RStdNetVoidResult){
                                    .r_tag = UINT32_C(1),
                                    .r_payload.r_error_00000001 = payload->forced_error,
                                },
                                payload->forced_sequence);
    } else {
        close_select_completion(control, close_outcome, close_result, close_sequence);
    }
    if (pthread_mutex_lock(&control->mutex) != 0) {
        close_panic();
    }
    if (control->drained) {
        (void)pthread_mutex_unlock(&control->mutex);
        close_panic();
    }
    control->drained = 1;
    close_cancel_timer_locked(control);
    action = close_finalize_action_locked(control);
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        close_panic();
    }
    close_finalize(control, action);
}

static void close_external_cancel(RRuntimeTaskExternalExecution *execution, void *payload_pointer) {
    RLibraryNetUdpClosePayload *payload = payload_pointer;
    RLibraryNetUdpCloseControl *control = payload->control;
    RLibraryNetUdpCloseFinalizeAction action;
    const uint64_t cancellation_sequence = r_runtime_task_external_cancellation_sequence(execution);

    if (control == NULL || control->execution != execution ||
        cancellation_sequence == UINT64_C(0) || pthread_mutex_lock(&control->mutex) != 0) {
        close_panic();
    }
    if (control->cancel_reported) {
        (void)pthread_mutex_unlock(&control->mutex);
        close_panic();
    }
    control->cancel_reported = 1;
    close_cancel_timer_locked(control);
    action = close_finalize_action_locked(control);
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        close_panic();
    }
    close_finalize(control, action);
}

static void close_external_start(RRuntimeTaskExternalExecution *execution,
                                 void *payload_pointer,
                                 void *result_pointer) {
    RLibraryNetUdpClosePayload *payload = payload_pointer;
    RLibraryNetUdpCloseControl *control = payload->control;
    dispatch_source_t timer;

    if (control == NULL || payload->storage == NULL || !payload->socket_owned ||
        !r_library_internal_net_udp_socket_mark_closing(payload->storage) ||
        pthread_mutex_lock(&control->mutex) != 0) {
        close_panic();
    }
    control->payload = payload;
    control->execution = execution;
    control->result = result_pointer;
    timer = control->timer;
    if (timer != NULL) {
        dispatch_set_context(timer, control);
        dispatch_source_set_event_handler_f(timer, close_timer_fired);
        dispatch_source_set_cancel_handler_f(timer, close_timer_cancelled);
        control->timer_activated = 1;
    }
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        close_panic();
    }
    r_runtime_task_external_start_ready(execution);
    if (timer != NULL) {
        dispatch_activate(timer);
    }
    r_library_internal_net_udp_socket_drain(payload->storage, close_drained, payload);
}

RStdNetTaskStartResult r_library_internal_net_udp_close(RStdNetUdpSocket *socket,
                                                        RStdNetDeadline deadline) {
    const RRuntimeTypeInfo payload_type = {
        sizeof(RLibraryNetUdpClosePayload),
        _Alignof(RLibraryNetUdpClosePayload),
        close_payload_move,
        close_payload_drop,
    };
    const RRuntimeTypeInfo result_type = {
        sizeof(RStdNetVoidResult),
        _Alignof(RStdNetVoidResult),
        NULL,
        NULL,
    };
    RLibraryNetUdpClosePayload payload = {0};
    RRuntimeTaskPrepareResult task_preparation;
    RRuntimeTaskStartResult started;
    RRuntimeAllocator *allocator;
    RLibraryNetDeadlineStatus deadline_status;
    RStdNetError deadline_error = {0};
    RStdNetTaskStartResult result = {0};
    uint64_t timeout_nanoseconds = 0U;

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
    payload.staged_socket = socket;
    payload.storage = socket->storage;
    if (!r_library_internal_net_udp_socket_preflight_close(
            payload.storage, &payload.preexisting_error, &payload.preexisting_failure)) {
        r_runtime_task_start_abort(&task_preparation.transaction);
        close_panic();
    }
    if (payload.preexisting_failure) {
        payload.preexisting_sequence = r_runtime_darwin_event_sequence_next();
    } else {
        deadline_status = r_library_internal_net_deadline_timeout(
            deadline, &timeout_nanoseconds, &deadline_error);
        if (deadline_status != R_LIBRARY_NET_DEADLINE_READY) {
            payload.has_forced_error = 1;
            payload.forced_deadline = deadline_status == R_LIBRARY_NET_DEADLINE_EXPIRED;
            payload.forced_error = deadline_error;
            payload.forced_sequence = r_runtime_darwin_event_sequence_next();
            timeout_nanoseconds = 0U;
        }
    }
    payload.control = close_control_create(allocator, timeout_nanoseconds);
    if (payload.control == NULL) {
        r_runtime_task_start_abort(&task_preparation.transaction);
        return start_failure(R_RUNTIME_TASK_START_ALLOCATION_FAILED);
    }
    started = r_runtime_task_start_commit(&task_preparation.transaction, &payload);
    if (started.status != R_RUNTIME_TASK_START_OK) {
        close_payload_drop(&payload);
        return start_failure(started.status);
    }
    result.is_ok = 1;
    result.task = started.task;
    return result;
}

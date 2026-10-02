#include "r_library_signal_internal.h"

#include "r_library_process_internal.h"
#include "r_runtime_0_1.h"
#include "r_runtime_darwin_event.h"
#include "r_runtime_task.h"
#include "r_runtime_type.h"

#include <stdint.h>

/* R-SLIB-SIGNAL-0002: one wait for the next deliveries of a listener. A wait that the deadline
   or an invalid deadline ends before submission completes immediately with its error. */
typedef struct RLibrarySignalNextPayload {
    RRuntimeDarwinSignalWait *wait;
    RRuntimeTaskExternalExecution *execution;
    RStdSignalNextResult *result;
    RStdProcessError immediate_error;
    _Bool has_immediate_error;
    _Bool wait_bound;
} RLibrarySignalNextPayload;

_Noreturn static void next_panic(void) {
    r_runtime_panic(R_RUNTIME_PANIC_CONTRACT_VIOLATION,
                    (RRuntimeSourceSpan){UINT32_C(0), UINT32_C(0), UINT32_C(0)});
}

static RStdProcessTaskStartResult next_start_failure(RRuntimeTaskStartStatus status) {
    RStdProcessTaskStartResult result = {0};

    switch (status) {
    case R_RUNTIME_TASK_START_ALLOCATION_FAILED:
        result.error = R_STD_ASYNC_START_REFUSAL();
        return result;
    case R_RUNTIME_TASK_START_RUNTIME_STOPPING:
        result.error = R_STD_ASYNC_START_RUNTIME_STOPPING;
        return result;
    case R_RUNTIME_TASK_START_OK:
    case R_RUNTIME_TASK_START_INVALID:
        next_panic();
    }
    next_panic();
}

static void next_payload_move(void *destination_pointer, void *source_pointer) {
    RLibrarySignalNextPayload *destination = destination_pointer;
    RLibrarySignalNextPayload *source = source_pointer;

    *destination = *source;
    source->wait = NULL;
}

static void next_payload_drop(void *value) {
    RLibrarySignalNextPayload *payload = value;

    r_runtime_darwin_signal_wait_abort(&payload->wait);
}

static void next_result_error(RStdSignalNextResult *result, RStdProcessError error) {
    *result = (RStdSignalNextResult){0};
    result->r_tag = UINT32_C(1);
    result->r_payload.r_error_00000001 = error;
}

static void next_completed(RRuntimeDarwinSignalWait *wait, void *context) {
    RLibrarySignalNextPayload *payload = context;
    const RRuntimeDarwinSignalWaitResult native = r_runtime_darwin_signal_wait_result(wait);
    RRuntimeTaskExternalExecution *execution = payload->execution;

    if (payload->wait != wait || execution == NULL) {
        next_panic();
    }
    switch (native.terminal_event) {
    case R_RUNTIME_DARWIN_SIGNAL_TERMINAL_DELIVERED:
        if (r_runtime_task_external_try_select_completion_at(execution,
                                                             native.terminal_event_sequence)) {
            *payload->result = (RStdSignalNextResult){0};
            payload->result->r_tag = UINT32_C(0);
            payload->result->r_payload.r_ok = native.count;
        } else {
            /* The task was cancelled first: the deliveries stay with the listener. */
            r_runtime_darwin_signal_wait_return_count(wait);
        }
        break;
    case R_RUNTIME_DARWIN_SIGNAL_TERMINAL_TIMED_OUT:
        if (r_runtime_task_external_try_select_completion_at(execution,
                                                             native.terminal_event_sequence)) {
            next_result_error(
                payload->result,
                r_library_internal_process_error(R_STD_PROCESS_ERROR_TIMED_OUT, INT64_C(0)));
        }
        break;
    case R_RUNTIME_DARWIN_SIGNAL_TERMINAL_CANCELLED:
        break;
    }
    r_runtime_darwin_signal_wait_release(&payload->wait);
    r_runtime_task_external_acknowledge(execution);
}

static void next_external_cancel(RRuntimeTaskExternalExecution *execution, void *payload_pointer) {
    RLibrarySignalNextPayload *payload = payload_pointer;

    if (payload->execution != execution) {
        next_panic();
    }
    if (!payload->wait_bound) {
        /* An immediate outcome was not selected: the cancellation is acknowledged here. */
        r_runtime_task_external_acknowledge(execution);
        return;
    }
    /* A completion that an earlier delivery or deadline selected may already have released the
       wait; the slot is then empty and the completion acknowledges. */
    r_runtime_darwin_signal_wait_cancel(&payload->wait,
                                        r_runtime_task_external_cancellation_sequence(execution));
}

static void next_external_start(RRuntimeTaskExternalExecution *execution,
                                void *payload_pointer,
                                void *result_pointer) {
    RLibrarySignalNextPayload *payload = payload_pointer;
    RStdSignalNextResult *result = result_pointer;

    payload->execution = execution;
    payload->result = result;
    if (payload->has_immediate_error) {
        const uint64_t event_sequence = r_runtime_darwin_event_sequence_next();
        const _Bool selected =
            r_runtime_task_external_try_select_completion_at(execution, event_sequence);

        r_runtime_task_external_start_ready(execution);
        if (selected) {
            next_result_error(result, payload->immediate_error);
            r_runtime_task_external_acknowledge(execution);
        }
        return;
    }
    if (payload->wait == NULL) {
        next_panic();
    }
    payload->wait_bound = 1;
    r_runtime_darwin_signal_wait_bind(payload->wait, next_completed, payload);
    r_runtime_task_external_start_ready(execution);
    if (!r_runtime_task_external_cancel_requested(execution)) {
        r_runtime_darwin_signal_wait_activate(payload->wait);
    }
}

RStdProcessTaskStartResult r_library_internal_signal_next(const RStdSignalListener *listener,
                                                          RStdProcessDeadline deadline) {
    RLibrarySignalNextPayload payload = {0};
    RRuntimeTaskPrepareResult preparation;
    RRuntimeTaskStartResult started;
    RStdProcessTaskStartResult result = {0};
    RLibraryProcessDeadlineStatus deadline_status;
    uint64_t timeout = UINT64_C(0);

    if (listener == NULL || listener->storage == NULL) {
        next_panic();
    }
    preparation = r_runtime_task_external_start_prepare(
        (RRuntimeTypeInfo){sizeof(RLibrarySignalNextPayload),
                           _Alignof(RLibrarySignalNextPayload),
                           next_payload_move,
                           next_payload_drop},
        (RRuntimeTypeInfo){
            sizeof(RStdSignalNextResult), _Alignof(RStdSignalNextResult), NULL, NULL},
        next_external_start,
        next_external_cancel);
    if (preparation.status != R_RUNTIME_TASK_START_OK) {
        return next_start_failure(preparation.status);
    }
    deadline_status =
        r_library_internal_process_deadline_timeout(deadline, &timeout, &payload.immediate_error);
    if (deadline_status == R_LIBRARY_PROCESS_DEADLINE_READY) {
        const RRuntimeDarwinSignalWaitPrepareResult prepared = r_runtime_darwin_signal_wait_prepare(
            r_runtime_task_start_allocator(preparation.transaction),
            listener->storage,
            deadline.has_value,
            timeout);

        if (prepared.status != R_RUNTIME_DARWIN_SIGNAL_OK) {
            r_runtime_task_start_abort(&preparation.transaction);
            return next_start_failure(R_RUNTIME_TASK_START_ALLOCATION_FAILED);
        }
        payload.wait = prepared.wait;
    } else {
        payload.has_immediate_error = 1;
    }
    started = r_runtime_task_start_commit(&preparation.transaction, &payload);
    if (started.status != R_RUNTIME_TASK_START_OK) {
        r_runtime_darwin_signal_wait_abort(&payload.wait);
        return next_start_failure(started.status);
    }
    result.is_ok = 1;
    result.task = started.task;
    return result;
}

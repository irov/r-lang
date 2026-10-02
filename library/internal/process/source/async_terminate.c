#include "r_library_process_internal.h"

#include "r_runtime_0_1.h"
#include "r_runtime_darwin_event.h"
#include "r_runtime_task.h"
#include "r_runtime_type.h"

#include <stdint.h>
#include <stdlib.h>

typedef struct RLibraryProcessTerminatePayload {
    RRuntimeDarwinProcessChild *child;
    RRuntimeTaskExternalExecution *execution;
    RStdProcessVoidResult *result;
    RStdProcessDeadline deadline;
    RStdProcessError forced_error;
    _Bool has_forced_error;
} RLibraryProcessTerminatePayload;

_Noreturn static void terminate_panic(void) {
    r_runtime_panic(R_RUNTIME_PANIC_CONTRACT_VIOLATION,
                    (RRuntimeSourceSpan){UINT32_C(0), UINT32_C(0), UINT32_C(0)});
}

static RStdProcessTaskStartResult task_start_failure(RRuntimeTaskStartStatus status) {
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
        terminate_panic();
    }
    terminate_panic();
}

static void terminate_payload_move(void *destination_pointer, void *source_pointer) {
    RLibraryProcessTerminatePayload *destination = destination_pointer;
    RLibraryProcessTerminatePayload *source = source_pointer;

    *destination = *source;
    source->child = NULL;
}

static void terminate_payload_drop(void *value) {
    RLibraryProcessTerminatePayload *payload = value;

    r_runtime_darwin_process_child_operation_release(payload->child);
    payload->child = NULL;
}

static RRuntimeTypeInfo terminate_payload_type(void) {
    return (RRuntimeTypeInfo){
        sizeof(RLibraryProcessTerminatePayload),
        _Alignof(RLibraryProcessTerminatePayload),
        terminate_payload_move,
        terminate_payload_drop,
    };
}

static void terminate_release_child(RLibraryProcessTerminatePayload *payload) {
    RRuntimeDarwinProcessChild *child = payload->child;

    if (child == NULL) {
        terminate_panic();
    }
    payload->child = NULL;
    r_runtime_darwin_process_child_operation_release(child);
}

static void terminate_external_cancel(RRuntimeTaskExternalExecution *execution,
                                      void *payload_pointer) {
    RLibraryProcessTerminatePayload *payload = payload_pointer;

    if (payload->execution != execution || payload->child == NULL) {
        terminate_panic();
    }
    terminate_release_child(payload);
    r_runtime_task_external_acknowledge(execution);
}

static RStdProcessError terminate_native_error(RRuntimeDarwinProcessTerminateResult native_result) {
    if (native_result.already_terminal) {
        return r_library_internal_process_error(R_STD_PROCESS_ERROR_NOT_RUNNING,
                                                (int64_t)native_result.native_error);
    }
    return r_library_internal_process_control_error_from_native(native_result.native_error);
}

static void terminate_external_start(RRuntimeTaskExternalExecution *execution,
                                     void *payload_pointer,
                                     void *result_pointer) {
    RLibraryProcessTerminatePayload *payload = payload_pointer;
    RStdProcessVoidResult *result = result_pointer;
    RRuntimeDarwinProcessTerminateResult native_result = {0};
    RStdProcessError operation_error = {0};
    RLibraryProcessDeadlineStatus deadline_status = R_LIBRARY_PROCESS_DEADLINE_READY;
    uint64_t ignored_timeout = UINT64_C(0);
    uint64_t event_sequence;
    _Bool native_committed = 0;
    _Bool selected;

    if (payload->child == NULL) {
        terminate_panic();
    }
    payload->execution = execution;
    payload->result = result;
    if (payload->has_forced_error) {
        operation_error = payload->forced_error;
        event_sequence = r_runtime_darwin_event_sequence_next();
    } else {
        deadline_status = r_library_internal_process_deadline_timeout(
            payload->deadline, &ignored_timeout, &operation_error);
        if (deadline_status == R_LIBRARY_PROCESS_DEADLINE_READY) {
            native_result = r_runtime_darwin_process_child_force_terminate(payload->child);
            if (native_result.terminal_event_sequence == UINT64_C(0)) {
                terminate_panic();
            }
            event_sequence = native_result.terminal_event_sequence;
            native_committed = native_result.accepted;
            if (!native_result.accepted) {
                operation_error = terminate_native_error(native_result);
            }
        } else {
            event_sequence = r_runtime_darwin_event_sequence_next();
        }
    }
    selected = r_runtime_task_external_try_select_completion_at(execution, event_sequence);
    if (native_committed && !selected) {
        selected = r_runtime_task_external_select_terminal_completion(execution);
    }
    r_runtime_task_external_start_ready(execution);
    if (!selected) {
        return;
    }
    *result = (RStdProcessVoidResult){0};
    if (native_committed) {
        result->status = R_STD_PROCESS_CALL_SUCCESS;
    } else {
        result->status = R_STD_PROCESS_CALL_ERROR;
        result->error = operation_error;
    }
    terminate_release_child(payload);
    r_runtime_task_external_acknowledge(execution);
}

RStdProcessTaskStartResult r_library_internal_process_terminate(const RStdProcessChild *child,
                                                                RStdProcessDeadline deadline) {
    RLibraryProcessTerminatePayload payload = {0};
    RRuntimeTaskPrepareResult preparation;
    RRuntimeTaskStartResult started;
    RStdProcessTaskStartResult result = {0};
    RLibraryProcessDeadlineStatus deadline_status;
    uint64_t ignored_timeout = UINT64_C(0);

    if (child == NULL || child->storage == NULL || child->storage->native == NULL) {
        terminate_panic();
    }
    preparation = r_runtime_task_external_start_prepare(
        terminate_payload_type(),
        (RRuntimeTypeInfo){
            sizeof(RStdProcessVoidResult), _Alignof(RStdProcessVoidResult), NULL, NULL},
        terminate_external_start,
        terminate_external_cancel);
    if (preparation.status != R_RUNTIME_TASK_START_OK) {
        return task_start_failure(preparation.status);
    }
    payload.child = r_runtime_darwin_process_child_operation_retain(child->storage->native);
    if (payload.child == NULL) {
        r_runtime_task_start_abort(&preparation.transaction);
        return task_start_failure(R_RUNTIME_TASK_START_RUNTIME_STOPPING);
    }
    payload.deadline = deadline;
    deadline_status = r_library_internal_process_deadline_timeout(
        deadline, &ignored_timeout, &payload.forced_error);
    payload.has_forced_error = deadline_status != R_LIBRARY_PROCESS_DEADLINE_READY;
    started = r_runtime_task_start_commit(&preparation.transaction, &payload);
    if (started.status != R_RUNTIME_TASK_START_OK) {
        r_runtime_darwin_process_child_operation_release(payload.child);
        payload.child = NULL;
        return task_start_failure(started.status);
    }
    result.is_ok = 1;
    result.task = started.task;
    return result;
}

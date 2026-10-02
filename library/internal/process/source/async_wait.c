#include "r_library_process_internal.h"

#include "r_runtime_0_1.h"
#include "r_runtime_darwin_event.h"
#include "r_runtime_task.h"
#include "r_runtime_type.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

enum {
    R_LIBRARY_PROCESS_WAIT_CANCEL_REPORTED = 1U,
    R_LIBRARY_PROCESS_WAIT_CALLBACK_RELEASED = 2U,
    R_LIBRARY_PROCESS_WAIT_COMPLETION_SELECTED = 4U
};

typedef struct RLibraryProcessWaitPayload {
    RStdProcessChild *staged_child;
    RStdProcessChild child;
    RRuntimeDarwinProcessWait *prepared;
    RRuntimeDarwinProcessWait *request;
    RRuntimeTaskExternalExecution *execution;
    RStdProcessWaitResult *result;
    RStdProcessError immediate_error;
    RRuntimeDarwinProcessWaitResult native_result;
    _Atomic unsigned int cancellation_state;
} RLibraryProcessWaitPayload;

_Noreturn static void wait_panic(void) {
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
        wait_panic();
    }
    wait_panic();
}

static RStdProcessTaskStartResult native_start_failure(RRuntimeDarwinProcessStartStatus status) {
    switch (status) {
    case R_RUNTIME_DARWIN_PROCESS_START_ALLOCATION_FAILED:
        return task_start_failure(R_RUNTIME_TASK_START_ALLOCATION_FAILED);
    case R_RUNTIME_DARWIN_PROCESS_START_RUNTIME_STOPPING:
        return task_start_failure(R_RUNTIME_TASK_START_RUNTIME_STOPPING);
    case R_RUNTIME_DARWIN_PROCESS_START_OK:
    case R_RUNTIME_DARWIN_PROCESS_START_INVALID:
        wait_panic();
    }
    wait_panic();
}

static void wait_result_move(void *destination_pointer, void *source_pointer) {
    RStdProcessWaitResult *destination = destination_pointer;
    RStdProcessWaitResult *source = source_pointer;

    *destination = *source;
    source->child.storage = NULL;
    source->child.identity = UINT64_C(0);
}

static void wait_result_drop(void *value) {
    RStdProcessWaitResult *result = value;

    r_library_internal_process_child_destroy(&result->child);
}

static RRuntimeTypeInfo wait_result_type(void) {
    return (RRuntimeTypeInfo){
        sizeof(RStdProcessWaitResult),
        _Alignof(RStdProcessWaitResult),
        wait_result_move,
        wait_result_drop,
    };
}

static void wait_payload_move(void *destination_pointer, void *source_pointer) {
    RLibraryProcessWaitPayload *destination = destination_pointer;
    RLibraryProcessWaitPayload *source = source_pointer;

    (void)memset(destination, 0, sizeof(*destination));
    if (source->staged_child == NULL) {
        wait_panic();
    }
    r_library_internal_process_child_move(&destination->child, source->staged_child);
    source->staged_child = NULL;
    destination->prepared = source->prepared;
    source->prepared = NULL;
    destination->immediate_error = source->immediate_error;
    atomic_init(&destination->cancellation_state, 0U);
}

static void wait_payload_drop(void *value) {
    RLibraryProcessWaitPayload *payload = value;

    if (payload->prepared != NULL) {
        r_runtime_darwin_process_wait_abort(&payload->prepared);
    }
    if (payload->request != NULL) {
        wait_panic();
    }
    r_library_internal_process_child_destroy(&payload->child);
}

static RRuntimeTypeInfo wait_payload_type(void) {
    return (RRuntimeTypeInfo){
        sizeof(RLibraryProcessWaitPayload),
        _Alignof(RLibraryProcessWaitPayload),
        wait_payload_move,
        wait_payload_drop,
    };
}

static RStdProcessExitStatus exit_status_from_native(int wait_status) {
    RStdProcessExitStatus result = {0};

    if (WIFEXITED(wait_status)) {
        result.kind = R_STD_PROCESS_TERMINATION_EXITED;
        result.code = (int32_t)WEXITSTATUS(wait_status);
        result.success = result.code == INT32_C(0);
    } else if (WIFSIGNALED(wait_status)) {
        result.kind = R_STD_PROCESS_TERMINATION_SIGNALLED;
        result.code = (int32_t)WTERMSIG(wait_status);
    } else {
        result.kind = R_STD_PROCESS_TERMINATION_OTHER;
        result.code = (int32_t)wait_status;
    }
    return result;
}

static void fill_wait_result(RLibraryProcessWaitPayload *payload) {
    RStdProcessWaitResult *result = payload->result;

    *result = (RStdProcessWaitResult){0};
    if (payload->native_result.terminal_event == R_RUNTIME_DARWIN_PROCESS_TERMINAL_NATIVE &&
        payload->native_result.reaped) {
        result->kind = R_STD_PROCESS_WAIT_RESULT_EXITED;
        result->status = exit_status_from_native(payload->native_result.wait_status);
        r_library_internal_process_child_destroy(&payload->child);
        return;
    }
    result->kind = R_STD_PROCESS_WAIT_RESULT_FAILED;
    if (payload->native_result.terminal_event == R_RUNTIME_DARWIN_PROCESS_TERMINAL_TIMED_OUT) {
        result->error = r_library_internal_process_error(R_STD_PROCESS_ERROR_TIMED_OUT, INT64_C(0));
    } else if (payload->native_result.terminal_event == R_RUNTIME_DARWIN_PROCESS_TERMINAL_NATIVE &&
               payload->native_result.native_error != 0) {
        result->error = r_library_internal_process_control_error_from_native(
            payload->native_result.native_error);
    } else {
        wait_panic();
    }
    r_library_internal_process_child_move(&result->child, &payload->child);
}

static void release_wait_request(RLibraryProcessWaitPayload *payload) {
    RRuntimeDarwinProcessWait *request = payload->request;

    if (request == NULL) {
        wait_panic();
    }
    payload->request = NULL;
    r_runtime_darwin_process_wait_release(request);
}

static void finalize_cancelled_wait(RLibraryProcessWaitPayload *payload) {
    release_wait_request(payload);
    r_runtime_task_external_acknowledge(payload->execution);
}

static void finalize_selected_wait(RLibraryProcessWaitPayload *payload) {
    fill_wait_result(payload);
    release_wait_request(payload);
    r_runtime_task_external_acknowledge(payload->execution);
}

static void wait_native_completed(RRuntimeDarwinProcessWait *request, void *context) {
    RLibraryProcessWaitPayload *payload = context;
    RRuntimeDarwinProcessWaitResult native_result;
    uint64_t cancellation_sequence;
    unsigned int previous_state;

    if (payload == NULL || payload->request != request) {
        wait_panic();
    }
    native_result = r_runtime_darwin_process_wait_result(request);
    if (native_result.terminal_event_sequence == UINT64_C(0)) {
        wait_panic();
    }
    if (native_result.terminal_event != R_RUNTIME_DARWIN_PROCESS_TERMINAL_CANCELLED &&
        r_runtime_task_external_try_select_completion_at(payload->execution,
                                                         native_result.terminal_event_sequence)) {
        cancellation_sequence = r_runtime_task_external_cancellation_sequence(payload->execution);
        payload->native_result = native_result;
        if (cancellation_sequence == UINT64_C(0)) {
            finalize_selected_wait(payload);
            return;
        }
        previous_state = atomic_fetch_or_explicit(&payload->cancellation_state,
                                                  R_LIBRARY_PROCESS_WAIT_CALLBACK_RELEASED |
                                                      R_LIBRARY_PROCESS_WAIT_COMPLETION_SELECTED,
                                                  memory_order_acq_rel);
        if ((previous_state & (R_LIBRARY_PROCESS_WAIT_CALLBACK_RELEASED |
                               R_LIBRARY_PROCESS_WAIT_COMPLETION_SELECTED)) != 0U) {
            wait_panic();
        }
        if ((previous_state & R_LIBRARY_PROCESS_WAIT_CANCEL_REPORTED) != 0U) {
            finalize_selected_wait(payload);
        }
        return;
    }
    if (r_runtime_task_external_cancellation_sequence(payload->execution) == UINT64_C(0)) {
        wait_panic();
    }
    previous_state = atomic_fetch_or_explicit(&payload->cancellation_state,
                                              R_LIBRARY_PROCESS_WAIT_CALLBACK_RELEASED,
                                              memory_order_acq_rel);
    if ((previous_state & (R_LIBRARY_PROCESS_WAIT_CALLBACK_RELEASED |
                           R_LIBRARY_PROCESS_WAIT_COMPLETION_SELECTED)) != 0U) {
        wait_panic();
    }
    if ((previous_state & R_LIBRARY_PROCESS_WAIT_CANCEL_REPORTED) != 0U) {
        finalize_cancelled_wait(payload);
    }
}

static void wait_external_cancel(RRuntimeTaskExternalExecution *execution, void *payload_pointer) {
    RLibraryProcessWaitPayload *payload = payload_pointer;
    unsigned int previous_state;
    const uint64_t cancellation_sequence = r_runtime_task_external_cancellation_sequence(execution);

    if (payload->execution != execution || payload->request == NULL ||
        cancellation_sequence == UINT64_C(0)) {
        wait_panic();
    }
    (void)r_runtime_darwin_process_wait_cancel(payload->request, cancellation_sequence);
    previous_state = atomic_fetch_or_explicit(
        &payload->cancellation_state, R_LIBRARY_PROCESS_WAIT_CANCEL_REPORTED, memory_order_acq_rel);
    if ((previous_state & R_LIBRARY_PROCESS_WAIT_CANCEL_REPORTED) != 0U) {
        wait_panic();
    }
    if ((previous_state & R_LIBRARY_PROCESS_WAIT_CALLBACK_RELEASED) != 0U) {
        if ((previous_state & R_LIBRARY_PROCESS_WAIT_COMPLETION_SELECTED) != 0U) {
            finalize_selected_wait(payload);
        } else {
            finalize_cancelled_wait(payload);
        }
    }
}

static void wait_external_start(RRuntimeTaskExternalExecution *execution,
                                void *payload_pointer,
                                void *result_pointer) {
    RLibraryProcessWaitPayload *payload = payload_pointer;

    if (payload->prepared == NULL || payload->request != NULL) {
        wait_panic();
    }
    payload->request = payload->prepared;
    payload->prepared = NULL;
    payload->execution = execution;
    payload->result = result_pointer;
    if (!r_runtime_darwin_process_wait_bind(payload->request, wait_native_completed, payload)) {
        wait_panic();
    }
    r_runtime_task_external_start_ready(execution);
    if (!r_runtime_darwin_process_wait_activate(payload->request)) {
        wait_panic();
    }
}

static void
wait_immediate_body(RRuntimeTaskExecution *execution, void *payload_pointer, void *result_pointer) {
    RLibraryProcessWaitPayload *payload = payload_pointer;
    RStdProcessWaitResult *result = result_pointer;

    (void)execution;
    *result = (RStdProcessWaitResult){0};
    result->kind = R_STD_PROCESS_WAIT_RESULT_FAILED;
    result->error = payload->immediate_error;
    r_library_internal_process_child_move(&result->child, &payload->child);
}

static RStdProcessTaskStartResult start_immediate_wait(RStdProcessChild *child,
                                                       RStdProcessError error) {
    RLibraryProcessWaitPayload payload = {0};
    RRuntimeTaskPrepareResult preparation;
    RRuntimeTaskStartResult started;
    RStdProcessTaskStartResult result = {0};

    payload.staged_child = child;
    payload.immediate_error = error;
    preparation =
        r_runtime_task_start_prepare(wait_payload_type(), wait_result_type(), wait_immediate_body);
    if (preparation.status != R_RUNTIME_TASK_START_OK) {
        return task_start_failure(preparation.status);
    }
    started = r_runtime_task_start_commit(&preparation.transaction, &payload);
    if (started.status != R_RUNTIME_TASK_START_OK) {
        return task_start_failure(started.status);
    }
    result.is_ok = 1;
    result.task = started.task;
    return result;
}

RStdProcessTaskStartResult r_library_internal_process_wait(RStdProcessChild *child,
                                                           RStdProcessDeadline deadline) {
    RLibraryProcessWaitPayload payload = {0};
    RRuntimeTaskPrepareResult task_preparation;
    RRuntimeDarwinProcessWaitPrepareResult native_preparation;
    RRuntimeTaskStartResult started;
    RStdProcessTaskStartResult result = {0};
    RStdProcessError deadline_error = {0};
    RLibraryProcessDeadlineStatus deadline_status;
    uint64_t timeout_nanoseconds = UINT64_C(0);

    if (child == NULL || child->storage == NULL || child->storage->native == NULL) {
        wait_panic();
    }
    deadline_status = r_library_internal_process_deadline_timeout(
        deadline, &timeout_nanoseconds, &deadline_error);
    if (deadline_status != R_LIBRARY_PROCESS_DEADLINE_READY) {
        return start_immediate_wait(child, deadline_error);
    }
    task_preparation = r_runtime_task_external_start_prepare(
        wait_payload_type(), wait_result_type(), wait_external_start, wait_external_cancel);
    if (task_preparation.status != R_RUNTIME_TASK_START_OK) {
        return task_start_failure(task_preparation.status);
    }
    native_preparation = r_runtime_darwin_process_wait_prepare(
        r_runtime_task_start_allocator(task_preparation.transaction),
        child->storage->native,
        timeout_nanoseconds);
    if (native_preparation.status != R_RUNTIME_DARWIN_PROCESS_START_OK) {
        r_runtime_task_start_abort(&task_preparation.transaction);
        return native_start_failure(native_preparation.status);
    }
    if (native_preparation.request == NULL) {
        wait_panic();
    }
    payload.staged_child = child;
    payload.prepared = native_preparation.request;
    started = r_runtime_task_start_commit(&task_preparation.transaction, &payload);
    if (started.status != R_RUNTIME_TASK_START_OK) {
        r_runtime_darwin_process_wait_abort(&payload.prepared);
        return task_start_failure(started.status);
    }
    result.is_ok = 1;
    result.task = started.task;
    return result;
}

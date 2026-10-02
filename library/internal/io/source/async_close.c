#include "r_library_io_internal.h"

#include "r_library_clock_internal.h"
#include "r_library_duration_internal.h"
#include "r_runtime_0_1.h"
#include "r_runtime_darwin_io.h"
#include "r_runtime_task.h"
#include "r_runtime_type.h"

#include <errno.h>
#include <limits.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef enum RLibraryIoCloseDeadlineStatus {
    R_LIBRARY_IO_CLOSE_DEADLINE_READY = 0,
    R_LIBRARY_IO_CLOSE_DEADLINE_EXPIRED,
    R_LIBRARY_IO_CLOSE_DEADLINE_ERROR
} RLibraryIoCloseDeadlineStatus;

enum {
    R_LIBRARY_IO_CLOSE_CANCEL_REPORTED = 1U,
    R_LIBRARY_IO_CLOSE_CALLBACK_RELEASED = 2U,
    R_LIBRARY_IO_CLOSE_COMPLETION_SELECTED = 4U
};

typedef struct RLibraryIoClosePayload {
    RRuntimeDarwinIoHandle **staged_handle;
    RRuntimeDarwinIoHandle *handle;
    RRuntimeDarwinIoPreparedRequest *prepared;
    RRuntimeDarwinIoRequest *request;
    RRuntimeDarwinIoResult native_result;
    RRuntimeTaskExternalExecution *execution;
    RStdIoVoidResult *result;
    RStdIoDeadline deadline;
    RStdIoError forced_error;
    _Atomic unsigned int cancellation_state;
    _Bool handle_owned;
    _Bool has_forced_error;
    _Bool preexisting_failure;
} RLibraryIoClosePayload;

_Noreturn static void close_panic(void) {
    r_runtime_panic(R_RUNTIME_PANIC_CONTRACT_VIOLATION,
                    (RRuntimeSourceSpan){UINT32_C(0), UINT32_C(0), UINT32_C(0)});
}

static RStdIoError io_error(RStdIoErrorCode code, int64_t native_code) {
    return (RStdIoError){code, native_code};
}

static RStdIoError classify_native_error(int native_error) {
    RStdIoErrorCode code = R_STD_IO_ERROR_OTHER;

    if (native_error == EBADF) {
        code = R_STD_IO_ERROR_CLOSED;
    } else if (native_error == EPIPE) {
        code = R_STD_IO_ERROR_BROKEN_PIPE;
    } else if (native_error == EACCES || native_error == EPERM) {
        code = R_STD_IO_ERROR_PERMISSION_DENIED;
    } else if (native_error == ENOMEM || native_error == ENOBUFS || native_error == EMFILE ||
               native_error == ENFILE || native_error == ENOSPC || native_error == EDQUOT ||
               native_error == EAGAIN) {
        code = R_STD_IO_ERROR_RESOURCE_EXHAUSTED;
    } else if (native_error == EINVAL || native_error == ESPIPE || native_error == ENOTSUP) {
        code = R_STD_IO_ERROR_INVALID_OPERATION;
    }
    return io_error(code, (int64_t)native_error);
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

static RLibraryIoCloseDeadlineStatus
deadline_timeout(RStdIoDeadline deadline, uint64_t *timeout_nanoseconds, RStdIoError *error) {
    const uint64_t nanoseconds_per_second = UINT64_C(1000000000);
    RStdTimeInstantResult now;
    RStdTimeDurationTimeResult remaining;
    uint64_t seconds;

    *timeout_nanoseconds = 0U;
    if (!deadline.has_value) {
        return R_LIBRARY_IO_CLOSE_DEADLINE_READY;
    }
    if (deadline.value.storage_seconds < 0 ||
        deadline.value.storage_nanoseconds >= R_STD_TIME_NANOSECONDS_PER_SECOND) {
        *error = io_error(R_STD_IO_ERROR_INVALID_OPERATION, INT64_C(0));
        return R_LIBRARY_IO_CLOSE_DEADLINE_ERROR;
    }
    now = r_library_internal_time_monotonic_now(r_library_internal_time_darwin_clock_hooks());
    if (!now.is_ok) {
        *error = io_error(R_STD_IO_ERROR_OTHER, now.error.native_code);
        return R_LIBRARY_IO_CLOSE_DEADLINE_ERROR;
    }
    if (compare_instant(deadline.value, now.value) <= 0) {
        *error = io_error(R_STD_IO_ERROR_TIMED_OUT, INT64_C(0));
        return R_LIBRARY_IO_CLOSE_DEADLINE_EXPIRED;
    }
    remaining = r_library_internal_time_instant_duration(deadline.value, now.value);
    if (!remaining.is_ok || remaining.value.seconds < 0) {
        *error = io_error(R_STD_IO_ERROR_OTHER, INT64_C(0));
        return R_LIBRARY_IO_CLOSE_DEADLINE_ERROR;
    }
    seconds = (uint64_t)remaining.value.seconds;
    if (seconds > ((uint64_t)INT64_MAX / nanoseconds_per_second)) {
        *error = io_error(R_STD_IO_ERROR_OTHER, INT64_C(0));
        return R_LIBRARY_IO_CLOSE_DEADLINE_ERROR;
    }
    *timeout_nanoseconds = seconds * nanoseconds_per_second;
    if ((uint64_t)remaining.value.nanoseconds > ((uint64_t)INT64_MAX - *timeout_nanoseconds)) {
        *error = io_error(R_STD_IO_ERROR_OTHER, INT64_C(0));
        return R_LIBRARY_IO_CLOSE_DEADLINE_ERROR;
    }
    *timeout_nanoseconds += (uint64_t)remaining.value.nanoseconds;
    return R_LIBRARY_IO_CLOSE_DEADLINE_READY;
}

static void close_payload_move(void *destination_pointer, void *source_pointer) {
    RLibraryIoClosePayload *destination = destination_pointer;
    RLibraryIoClosePayload *source = source_pointer;

    (void)memset(destination, 0, sizeof(*destination));
    destination->handle = *source->staged_handle;
    *source->staged_handle = NULL;
    destination->handle_owned = 1;
    destination->prepared = source->prepared;
    source->prepared = NULL;
    destination->deadline = source->deadline;
    destination->forced_error = source->forced_error;
    destination->has_forced_error = source->has_forced_error;
    destination->preexisting_failure = source->preexisting_failure;
    atomic_init(&destination->cancellation_state, 0U);
}

static void close_payload_drop(void *value) {
    RLibraryIoClosePayload *payload = value;

    if (payload->prepared != NULL) {
        r_runtime_darwin_io_prepared_abort(&payload->prepared);
    }
    if (payload->request != NULL) {
        close_panic();
    }
    if (payload->handle_owned) {
        r_runtime_darwin_io_handle_release(payload->handle);
        payload->handle = NULL;
        payload->handle_owned = 0;
    }
}

static RStdIoTaskStartResult task_start_failure(RRuntimeTaskStartStatus status) {
    RStdIoTaskStartResult result = {0};

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

static void release_close_ownership(RLibraryIoClosePayload *payload) {
    RRuntimeDarwinIoRequest *request;

    if (payload->request == NULL || !payload->handle_owned) {
        close_panic();
    }
    request = payload->request;
    payload->request = NULL;
    r_runtime_darwin_io_request_release(request);
    r_runtime_darwin_io_handle_release(payload->handle);
    payload->handle = NULL;
    payload->handle_owned = 0;
}

static void fill_close_result(RLibraryIoClosePayload *payload,
                              RRuntimeDarwinIoResult native_result) {
    if (payload->has_forced_error) {
        *payload->result = (RStdIoVoidResult){UINT32_C(1), {.r_err = payload->forced_error}};
    } else if (native_result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_TIMED_OUT) {
        *payload->result = (RStdIoVoidResult){
            UINT32_C(1),
            {.r_err = io_error(R_STD_IO_ERROR_TIMED_OUT, INT64_C(0))},
        };
    } else if (native_result.native_error != 0) {
        *payload->result = (RStdIoVoidResult){
            UINT32_C(1),
            {.r_err = classify_native_error(native_result.native_error)},
        };
    } else {
        *payload->result = (RStdIoVoidResult){0};
    }
}

static void finalize_cancelled_close(RLibraryIoClosePayload *payload) {
    release_close_ownership(payload);
    r_runtime_task_external_acknowledge(payload->execution);
}

static void finalize_selected_close(RLibraryIoClosePayload *payload) {
    fill_close_result(payload, payload->native_result);
    release_close_ownership(payload);
    r_runtime_task_external_acknowledge(payload->execution);
}

static void close_completed(RRuntimeDarwinIoRequest *request, void *context) {
    RLibraryIoClosePayload *payload = context;
    RRuntimeDarwinIoResult native_result;
    uint64_t cancellation_sequence;
    unsigned int previous_state;

    if (payload->request != request) {
        close_panic();
    }
    native_result = r_runtime_darwin_io_request_wait(request);
    if (native_result.terminal_event_sequence == UINT64_C(0)) {
        close_panic();
    }
    if (r_runtime_task_external_try_select_completion_at(payload->execution,
                                                         native_result.terminal_event_sequence)) {
        cancellation_sequence = r_runtime_task_external_cancellation_sequence(payload->execution);
        if (cancellation_sequence == UINT64_C(0)) {
            payload->native_result = native_result;
            finalize_selected_close(payload);
            return;
        }
        payload->native_result = native_result;
        previous_state = atomic_fetch_or_explicit(&payload->cancellation_state,
                                                  R_LIBRARY_IO_CLOSE_CALLBACK_RELEASED |
                                                      R_LIBRARY_IO_CLOSE_COMPLETION_SELECTED,
                                                  memory_order_acq_rel);
        if ((previous_state & (R_LIBRARY_IO_CLOSE_CALLBACK_RELEASED |
                               R_LIBRARY_IO_CLOSE_COMPLETION_SELECTED)) != 0U) {
            close_panic();
        }
        if ((previous_state & R_LIBRARY_IO_CLOSE_CANCEL_REPORTED) != 0U) {
            finalize_selected_close(payload);
        }
        return;
    }
    if (r_runtime_task_external_cancellation_sequence(payload->execution) == UINT64_C(0)) {
        close_panic();
    }
    previous_state = atomic_fetch_or_explicit(
        &payload->cancellation_state, R_LIBRARY_IO_CLOSE_CALLBACK_RELEASED, memory_order_acq_rel);
    if ((previous_state &
         (R_LIBRARY_IO_CLOSE_CALLBACK_RELEASED | R_LIBRARY_IO_CLOSE_COMPLETION_SELECTED)) != 0U) {
        close_panic();
    }
    if ((previous_state & R_LIBRARY_IO_CLOSE_CANCEL_REPORTED) != 0U) {
        finalize_cancelled_close(payload);
    }
}

static void close_external_cancel(RRuntimeTaskExternalExecution *execution, void *payload_pointer) {
    RLibraryIoClosePayload *payload = payload_pointer;
    unsigned int previous_state;

    if (payload->execution != execution) {
        close_panic();
    }
    previous_state = atomic_fetch_or_explicit(
        &payload->cancellation_state, R_LIBRARY_IO_CLOSE_CANCEL_REPORTED, memory_order_acq_rel);
    if ((previous_state & R_LIBRARY_IO_CLOSE_CANCEL_REPORTED) != 0U) {
        close_panic();
    }
    if ((previous_state & R_LIBRARY_IO_CLOSE_CALLBACK_RELEASED) != 0U) {
        if ((previous_state & R_LIBRARY_IO_CLOSE_COMPLETION_SELECTED) != 0U) {
            finalize_selected_close(payload);
        } else {
            finalize_cancelled_close(payload);
        }
    }
}

static void close_external_start(RRuntimeTaskExternalExecution *execution,
                                 void *payload_pointer,
                                 void *result_pointer) {
    RLibraryIoClosePayload *payload = payload_pointer;
    RRuntimeDarwinIoSubmitResult submission;
    RLibraryIoCloseDeadlineStatus deadline_status;
    RStdIoError deadline_error = {0};
    uint64_t ignored_timeout = 0U;
    _Bool deadline_expired = 0;

    payload->execution = execution;
    payload->result = result_pointer;
    if (!payload->preexisting_failure && !payload->has_forced_error) {
        deadline_status = deadline_timeout(payload->deadline, &ignored_timeout, &deadline_error);
        if (deadline_status == R_LIBRARY_IO_CLOSE_DEADLINE_EXPIRED) {
            deadline_expired = 1;
        } else if (deadline_status == R_LIBRARY_IO_CLOSE_DEADLINE_ERROR) {
            payload->has_forced_error = 1;
            payload->forced_error = deadline_error;
        }
    }
    submission = r_runtime_darwin_io_prepared_activate_close(&payload->prepared, deadline_expired);
    if (submission.status != R_RUNTIME_DARWIN_IO_START_OK || submission.request == NULL ||
        payload->prepared != NULL) {
        close_panic();
    }
    payload->request = submission.request;
    r_runtime_task_external_start_ready(execution);
    if (!r_runtime_darwin_io_request_set_completion(payload->request, close_completed, payload)) {
        close_panic();
    }
}

static RStdIoTaskStartResult close_start(RRuntimeDarwinIoHandle **staged_handle,
                                         RStdIoDeadline deadline) {
    const RRuntimeTypeInfo payload_type = {
        sizeof(RLibraryIoClosePayload),
        _Alignof(RLibraryIoClosePayload),
        close_payload_move,
        close_payload_drop,
    };
    const RRuntimeTypeInfo result_type = {
        sizeof(RStdIoVoidResult),
        _Alignof(RStdIoVoidResult),
        NULL,
        NULL,
    };
    RLibraryIoClosePayload payload;
    RRuntimeDarwinIoPrepareResult native_preparation;
    RRuntimeTaskPrepareResult task_preparation;
    RRuntimeTaskStartResult started;
    RLibraryIoCloseDeadlineStatus deadline_status;
    RStdIoError deadline_error = {0};
    RStdIoTaskStartResult result = {0};
    uint64_t timeout_nanoseconds = 0U;
    int preexisting_error = 0;

    (void)memset(&payload, 0, sizeof(payload));
    payload.staged_handle = staged_handle;
    payload.deadline = deadline;
    payload.preexisting_failure =
        r_runtime_darwin_io_handle_terminal_close_failure(*staged_handle, &preexisting_error);
    if (payload.preexisting_failure) {
        timeout_nanoseconds = 0U;
    } else {
        deadline_status = deadline_timeout(deadline, &timeout_nanoseconds, &deadline_error);
        if (deadline_status == R_LIBRARY_IO_CLOSE_DEADLINE_EXPIRED) {
            timeout_nanoseconds = 0U;
        } else if (deadline_status == R_LIBRARY_IO_CLOSE_DEADLINE_ERROR) {
            payload.has_forced_error = 1;
            payload.forced_error = deadline_error;
            timeout_nanoseconds = 0U;
        }
    }
    native_preparation = r_runtime_darwin_io_prepare_close(*staged_handle, timeout_nanoseconds);
    if (native_preparation.status == R_RUNTIME_DARWIN_IO_START_ALLOCATION_FAILED ||
        native_preparation.status == R_RUNTIME_DARWIN_IO_START_NATIVE_RETAIN_FAILED) {
        return task_start_failure(R_RUNTIME_TASK_START_ALLOCATION_FAILED);
    }
    if (native_preparation.status != R_RUNTIME_DARWIN_IO_START_OK ||
        native_preparation.prepared == NULL) {
        close_panic();
    }
    payload.prepared = native_preparation.prepared;
    task_preparation = r_runtime_task_external_start_prepare(
        payload_type, result_type, close_external_start, close_external_cancel);
    if (task_preparation.status != R_RUNTIME_TASK_START_OK) {
        r_runtime_darwin_io_prepared_abort(&payload.prepared);
        return task_start_failure(task_preparation.status);
    }
    started = r_runtime_task_start_commit(&task_preparation.transaction, &payload);
    if (started.status != R_RUNTIME_TASK_START_OK) {
        r_runtime_darwin_io_prepared_abort(&payload.prepared);
        return task_start_failure(started.status);
    }
    result.is_ok = 1;
    result.task = started.task;
    return result;
}

RStdIoTaskStartResult r_library_internal_io_close_input(RStdIoInput *stream,
                                                        RStdIoDeadline deadline) {
    return close_start(&stream->handle, deadline);
}

RStdIoTaskStartResult r_library_internal_io_close_output(RStdIoOutput *stream,
                                                         RStdIoDeadline deadline) {
    return close_start(&stream->handle, deadline);
}

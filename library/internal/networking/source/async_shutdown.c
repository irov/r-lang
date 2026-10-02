#include "r_library_net_internal.h"

#include "r_runtime_0_1.h"
#include "r_runtime_darwin_event.h"
#include "r_runtime_darwin_io.h"
#include "r_runtime_task.h"
#include "r_runtime_type.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef enum RLibraryNetShutdownImmediate {
    R_LIBRARY_NET_SHUTDOWN_IMMEDIATE_NONE = 0,
    R_LIBRARY_NET_SHUTDOWN_IMMEDIATE_SUCCESS,
    R_LIBRARY_NET_SHUTDOWN_IMMEDIATE_FAILED
} RLibraryNetShutdownImmediate;

enum {
    R_LIBRARY_NET_SHUTDOWN_CANCEL_REPORTED = 1U,
    R_LIBRARY_NET_SHUTDOWN_CALLBACK_RELEASED = 2U,
    R_LIBRARY_NET_SHUTDOWN_COMPLETION_SELECTED = 4U,
    R_LIBRARY_NET_SHUTDOWN_FINALIZED = 8U,
    R_LIBRARY_NET_SHUTDOWN_ENTRY_SELECTED = 16U
};

typedef struct RLibraryNetShutdownPayload {
    RLibraryNetShutdownImmediate immediate;
    RStdNetDeadline deadline;
    RStdNetError immediate_error;
    uint64_t immediate_event_sequence;
    RStdNetTcpStreamStorage *storage;
    RRuntimeDarwinIoPreparedRequest *prepared;
    RRuntimeDarwinIoRequest *request;
    RRuntimeDarwinIoResult native_result;
    RRuntimeTaskExternalExecution *execution;
    RStdNetVoidResult *result;
    _Atomic unsigned int state;
    _Bool storage_owned;
} RLibraryNetShutdownPayload;

_Noreturn static void shutdown_panic(void) {
    r_runtime_panic(R_RUNTIME_PANIC_CONTRACT_VIOLATION, (RRuntimeSourceSpan){0U, 0U, 0U});
}

static RStdNetError net_error(RStdNetErrorCode code, int64_t native_code) {
    return (RStdNetError){code, native_code};
}

static void shutdown_payload_move(void *destination_pointer, void *source_pointer) {
    RLibraryNetShutdownPayload *destination = destination_pointer;
    RLibraryNetShutdownPayload *source = source_pointer;

    if (source->storage == NULL && source->immediate != R_LIBRARY_NET_SHUTDOWN_IMMEDIATE_FAILED) {
        shutdown_panic();
    }
    (void)memset(destination, 0, sizeof(*destination));
    destination->immediate = source->immediate;
    destination->deadline = source->deadline;
    destination->immediate_error = source->immediate_error;
    destination->immediate_event_sequence = source->immediate_event_sequence;
    destination->storage = source->storage;
    destination->storage_owned = source->storage != NULL;
    destination->prepared = source->prepared;
    source->storage = NULL;
    source->prepared = NULL;
    atomic_init(&destination->state, 0U);
}

static void shutdown_payload_drop(void *value) {
    RLibraryNetShutdownPayload *payload = value;

    if (payload->prepared != NULL) {
        r_runtime_darwin_io_prepared_abort(&payload->prepared);
    }
    if (payload->request != NULL) {
        shutdown_panic();
    }
    if (payload->storage_owned) {
        r_library_internal_net_handle_release(&payload->storage->handle);
        payload->storage = NULL;
        payload->storage_owned = 0;
    }
}

static RStdNetError native_result_error(RRuntimeDarwinIoResult result) {
    if (result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_CANCELLED) {
        return net_error(R_STD_NET_ERROR_CANCELLED, INT64_C(0));
    }
    if (result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_TIMED_OUT) {
        return net_error(R_STD_NET_ERROR_TIMED_OUT, INT64_C(0));
    }
    if (result.native_error != 0) {
        return r_library_internal_net_error_from_native(result.native_error);
    }
    return net_error(R_STD_NET_ERROR_OTHER, INT64_C(0));
}

static void fill_result(RLibraryNetShutdownPayload *payload) {
    if (payload->request == NULL) {
        *payload->result = payload->immediate == R_LIBRARY_NET_SHUTDOWN_IMMEDIATE_SUCCESS
                               ? (RStdNetVoidResult){0}
                               : (RStdNetVoidResult){
                                     .r_tag = UINT32_C(1),
                                     .r_payload.r_error_00000001 = payload->immediate_error,
                                 };
        return;
    }
    if (payload->native_result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE &&
        payload->native_result.native_error == 0) {
        *payload->result = (RStdNetVoidResult){0};
    } else {
        *payload->result = (RStdNetVoidResult){
            .r_tag = UINT32_C(1),
            .r_payload.r_error_00000001 = native_result_error(payload->native_result),
        };
    }
}

static void release_resources(RLibraryNetShutdownPayload *payload) {
    if (payload->request != NULL) {
        RRuntimeDarwinIoRequest *request = payload->request;

        payload->request = NULL;
        r_runtime_darwin_io_request_release(request);
    }
    if (payload->storage_owned) {
        if (payload->storage == NULL) {
            shutdown_panic();
        }
        r_library_internal_net_handle_release(&payload->storage->handle);
        payload->storage = NULL;
        payload->storage_owned = 0;
    }
}

static void shutdown_try_finalize(RLibraryNetShutdownPayload *payload) {
    unsigned int state;

    for (;;) {
        unsigned int desired;
        const uint64_t cancellation_sequence =
            r_runtime_task_external_cancellation_sequence(payload->execution);

        state = atomic_load_explicit(&payload->state, memory_order_acquire);
        if ((state & R_LIBRARY_NET_SHUTDOWN_FINALIZED) != 0U ||
            (state & R_LIBRARY_NET_SHUTDOWN_CALLBACK_RELEASED) == 0U ||
            (cancellation_sequence != UINT64_C(0) &&
             (state & R_LIBRARY_NET_SHUTDOWN_CANCEL_REPORTED) == 0U)) {
            return;
        }
        desired = state | R_LIBRARY_NET_SHUTDOWN_FINALIZED;
        if (atomic_compare_exchange_weak_explicit(
                &payload->state, &state, desired, memory_order_acq_rel, memory_order_acquire)) {
            state = desired;
            break;
        }
    }
    if ((state & R_LIBRARY_NET_SHUTDOWN_COMPLETION_SELECTED) != 0U) {
        fill_result(payload);
    }
    release_resources(payload);
    r_runtime_task_external_acknowledge(payload->execution);
}

static void shutdown_completed(RRuntimeDarwinIoRequest *request, void *context) {
    RLibraryNetShutdownPayload *payload = context;
    RRuntimeDarwinIoResult native_result;
    unsigned int bits = R_LIBRARY_NET_SHUTDOWN_CALLBACK_RELEASED;
    unsigned int previous;

    if (payload->request != request) {
        shutdown_panic();
    }
    native_result = r_runtime_darwin_io_request_wait(request);
    if (native_result.terminal_event_sequence == UINT64_C(0)) {
        shutdown_panic();
    }
    if ((atomic_load_explicit(&payload->state, memory_order_acquire) &
         R_LIBRARY_NET_SHUTDOWN_ENTRY_SELECTED) != 0U ||
        r_runtime_task_external_try_select_completion_at(payload->execution,
                                                         native_result.terminal_event_sequence)) {
        bits |= R_LIBRARY_NET_SHUTDOWN_COMPLETION_SELECTED;
    }
    payload->native_result = native_result;
    previous = atomic_fetch_or_explicit(&payload->state, bits, memory_order_acq_rel);
    if ((previous &
         (R_LIBRARY_NET_SHUTDOWN_CALLBACK_RELEASED | R_LIBRARY_NET_SHUTDOWN_COMPLETION_SELECTED |
          R_LIBRARY_NET_SHUTDOWN_FINALIZED)) != 0U) {
        shutdown_panic();
    }
    shutdown_try_finalize(payload);
}

/* The worker asks before it enters the native half-close. Task cancellation reaches the request
   asynchronously, so a cancellation selected earlier refuses entry and nothing is published; an
   admitted entry selects completion, and a later cancel cannot replace the outcome of the
   half-close it commits (R-SLIB-NET-0006, R-SLIB-ASYNC-0007). */
static _Bool shutdown_entry(void *context) {
    RLibraryNetShutdownPayload *payload = context;

    if (!r_runtime_task_external_try_select_completion_at(payload->execution,
                                                          r_runtime_darwin_event_sequence_next())) {
        return 0;
    }
    (void)atomic_fetch_or_explicit(
        &payload->state, R_LIBRARY_NET_SHUTDOWN_ENTRY_SELECTED, memory_order_acq_rel);
    return 1;
}

static void shutdown_external_cancel(RRuntimeTaskExternalExecution *execution,
                                     void *payload_pointer) {
    RLibraryNetShutdownPayload *payload = payload_pointer;
    unsigned int previous;

    if (payload->execution != execution) {
        shutdown_panic();
    }
    if (payload->request != NULL) {
        (void)r_runtime_darwin_io_request_cancel(payload->request);
    }
    previous = atomic_fetch_or_explicit(
        &payload->state, R_LIBRARY_NET_SHUTDOWN_CANCEL_REPORTED, memory_order_acq_rel);
    if ((previous & (R_LIBRARY_NET_SHUTDOWN_CANCEL_REPORTED | R_LIBRARY_NET_SHUTDOWN_FINALIZED)) !=
        0U) {
        shutdown_panic();
    }
    shutdown_try_finalize(payload);
}

static void complete_immediately(RLibraryNetShutdownPayload *payload,
                                 RRuntimeTaskExternalExecution *execution) {
    uint64_t cancellation_before_ready;
    unsigned int bits = R_LIBRARY_NET_SHUTDOWN_CALLBACK_RELEASED;
    unsigned int previous;

    if (payload->immediate == R_LIBRARY_NET_SHUTDOWN_IMMEDIATE_NONE ||
        payload->immediate_event_sequence == UINT64_C(0)) {
        shutdown_panic();
    }
    if (r_runtime_task_external_try_select_completion_at(execution,
                                                         payload->immediate_event_sequence)) {
        bits |= R_LIBRARY_NET_SHUTDOWN_COMPLETION_SELECTED;
    }
    previous = atomic_fetch_or_explicit(&payload->state, bits, memory_order_acq_rel);
    if (previous != 0U) {
        shutdown_panic();
    }
    cancellation_before_ready = r_runtime_task_external_cancellation_sequence(execution);
    r_runtime_task_external_start_ready(execution);
    if (cancellation_before_ready != UINT64_C(0) &&
        (bits & R_LIBRARY_NET_SHUTDOWN_COMPLETION_SELECTED) != 0U) {
        previous = atomic_fetch_or_explicit(
            &payload->state, R_LIBRARY_NET_SHUTDOWN_CANCEL_REPORTED, memory_order_acq_rel);
        if ((previous &
             (R_LIBRARY_NET_SHUTDOWN_CANCEL_REPORTED | R_LIBRARY_NET_SHUTDOWN_FINALIZED)) != 0U) {
            shutdown_panic();
        }
    }
    shutdown_try_finalize(payload);
}

static void shutdown_external_start(RRuntimeTaskExternalExecution *execution,
                                    void *payload_pointer,
                                    void *result_pointer) {
    RLibraryNetShutdownPayload *payload = payload_pointer;
    RRuntimeDarwinIoSubmitResult submitted;
    RStdNetError deadline_error = {0};
    uint64_t ignored_timeout = 0U;

    payload->execution = execution;
    payload->result = result_pointer;
    if (payload->immediate == R_LIBRARY_NET_SHUTDOWN_IMMEDIATE_NONE &&
        payload->deadline.has_value &&
        r_library_internal_net_deadline_timeout(
            payload->deadline, &ignored_timeout, &deadline_error) != R_LIBRARY_NET_DEADLINE_READY) {
        r_runtime_darwin_io_prepared_abort(&payload->prepared);
        payload->immediate = R_LIBRARY_NET_SHUTDOWN_IMMEDIATE_FAILED;
        payload->immediate_error = deadline_error;
        payload->immediate_event_sequence = r_runtime_darwin_event_sequence_next();
    }
    if (payload->immediate != R_LIBRARY_NET_SHUTDOWN_IMMEDIATE_NONE ||
        r_runtime_task_external_cancel_requested(execution)) {
        if (payload->prepared != NULL) {
            r_runtime_darwin_io_prepared_abort(&payload->prepared);
        }
        if (payload->immediate == R_LIBRARY_NET_SHUTDOWN_IMMEDIATE_NONE) {
            payload->immediate = R_LIBRARY_NET_SHUTDOWN_IMMEDIATE_FAILED;
            payload->immediate_error = net_error(R_STD_NET_ERROR_CANCELLED, INT64_C(0));
            payload->immediate_event_sequence = r_runtime_darwin_event_sequence_next();
        }
        complete_immediately(payload, execution);
        return;
    }
    if (!r_runtime_darwin_io_prepared_set_shutdown_entry(
            payload->prepared, shutdown_entry, payload)) {
        shutdown_panic();
    }
    submitted = r_runtime_darwin_io_prepared_activate(&payload->prepared, NULL);
    if (submitted.status != R_RUNTIME_DARWIN_IO_START_OK || submitted.request == NULL ||
        payload->prepared != NULL) {
        shutdown_panic();
    }
    payload->request = submitted.request;
    r_runtime_task_external_start_ready(execution);
    if (!r_runtime_darwin_io_request_set_completion(
            payload->request, shutdown_completed, payload)) {
        shutdown_panic();
    }
}

static RStdNetTaskStartResult task_start_failure(RRuntimeTaskStartStatus status) {
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
        shutdown_panic();
    }
    shutdown_panic();
}

static RStdNetTaskStartResult start_task(RLibraryNetShutdownPayload *payload) {
    const RRuntimeTypeInfo payload_type = {
        sizeof(RLibraryNetShutdownPayload),
        _Alignof(RLibraryNetShutdownPayload),
        shutdown_payload_move,
        shutdown_payload_drop,
    };
    const RRuntimeTypeInfo result_type = {
        sizeof(RStdNetVoidResult),
        _Alignof(RStdNetVoidResult),
        NULL,
        NULL,
    };
    RRuntimeTaskPrepareResult prepared = r_runtime_task_external_start_prepare(
        payload_type, result_type, shutdown_external_start, shutdown_external_cancel);
    RRuntimeTaskStartResult started;
    RStdNetTaskStartResult result = {0};

    if (prepared.status != R_RUNTIME_TASK_START_OK) {
        if (payload->prepared != NULL) {
            r_runtime_darwin_io_prepared_abort(&payload->prepared);
        }
        if (payload->storage != NULL) {
            r_library_internal_net_handle_release(&payload->storage->handle);
            payload->storage = NULL;
        }
        return task_start_failure(prepared.status);
    }
    started = r_runtime_task_start_commit(&prepared.transaction, payload);
    if (started.status != R_RUNTIME_TASK_START_OK) {
        if (payload->prepared != NULL) {
            r_runtime_darwin_io_prepared_abort(&payload->prepared);
        }
        if (payload->storage != NULL) {
            r_library_internal_net_handle_release(&payload->storage->handle);
            payload->storage = NULL;
        }
        return task_start_failure(started.status);
    }
    result.is_ok = 1;
    result.task = started.task;
    return result;
}

static RStdNetTaskStartResult native_start_failure(RRuntimeDarwinIoStartStatus status) {
    switch (status) {
    case R_RUNTIME_DARWIN_IO_START_ALLOCATION_FAILED:
    case R_RUNTIME_DARWIN_IO_START_NATIVE_RETAIN_FAILED:
        return task_start_failure(R_RUNTIME_TASK_START_ALLOCATION_FAILED);
    case R_RUNTIME_DARWIN_IO_START_OK:
    case R_RUNTIME_DARWIN_IO_START_INVALID_ARGUMENT:
    case R_RUNTIME_DARWIN_IO_START_HANDLE_CLOSED:
    case R_RUNTIME_DARWIN_IO_START_ORDERING_BLOCKED:
        shutdown_panic();
    }
    shutdown_panic();
}

RStdNetTaskStartResult r_library_internal_net_tcp_shutdown(const RStdNetTcpStream *stream,
                                                           RStdNetShutdownDirection direction,
                                                           RStdNetDeadline deadline) {
    RLibraryNetShutdownPayload payload = {0};
    RLibraryNetTcpShutdownPrepareResult native_preparation;
    RLibraryNetDeadlineStatus deadline_status;
    RStdNetError deadline_error = {0};
    uint64_t timeout_nanoseconds = 0U;

    payload.deadline = deadline;
    deadline_status =
        r_library_internal_net_deadline_timeout(deadline, &timeout_nanoseconds, &deadline_error);
    native_preparation = r_library_internal_net_tcp_stream_prepare_shutdown(
        stream->storage,
        direction,
        timeout_nanoseconds,
        deadline_status == R_LIBRARY_NET_DEADLINE_READY);
    if (native_preparation.status == R_RUNTIME_DARWIN_IO_START_HANDLE_CLOSED) {
        payload.immediate = R_LIBRARY_NET_SHUTDOWN_IMMEDIATE_FAILED;
        payload.immediate_error = net_error(R_STD_NET_ERROR_CLOSED, INT64_C(0));
        payload.immediate_event_sequence = r_runtime_darwin_event_sequence_next();
        return start_task(&payload);
    }
    if (native_preparation.status != R_RUNTIME_DARWIN_IO_START_OK) {
        return native_start_failure(native_preparation.status);
    }
    if (native_preparation.storage == NULL) {
        shutdown_panic();
    }
    payload.storage = native_preparation.storage;
    payload.prepared = native_preparation.prepared;
    if (native_preparation.already_shutdown) {
        payload.immediate = R_LIBRARY_NET_SHUTDOWN_IMMEDIATE_SUCCESS;
        payload.immediate_event_sequence = r_runtime_darwin_event_sequence_next();
    } else if (deadline_status != R_LIBRARY_NET_DEADLINE_READY) {
        payload.immediate = R_LIBRARY_NET_SHUTDOWN_IMMEDIATE_FAILED;
        payload.immediate_error = deadline_error;
        payload.immediate_event_sequence = r_runtime_darwin_event_sequence_next();
    } else if (payload.prepared == NULL) {
        shutdown_panic();
    }
    return start_task(&payload);
}

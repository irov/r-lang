#include "r_library_net_internal.h"

#include "r_runtime_0_1.h"
#include "r_runtime_darwin_event.h"
#include "r_runtime_darwin_io.h"
#include "r_runtime_task.h"
#include "r_runtime_type.h"

#include <errno.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(R_LIBRARY_NET_TESTING)
#include <pthread.h>
#endif

enum {
    R_LIBRARY_NET_STREAM_CLOSE_CANCEL_REPORTED = 1U,
    R_LIBRARY_NET_STREAM_CLOSE_CALLBACK_RELEASED = 2U,
    R_LIBRARY_NET_STREAM_CLOSE_COMPLETION_SELECTED = 4U,
    R_LIBRARY_NET_STREAM_CLOSE_FINALIZED = 8U
};

typedef struct RLibraryNetStreamClosePayload {
    RStdNetTcpStream *staged_stream;
    RStdNetTcpStreamStorage *storage;
    RRuntimeDarwinIoHandle *data_io;
    RRuntimeDarwinIoPreparedRequest *prepared;
    RRuntimeDarwinIoRequest *request;
    RRuntimeDarwinIoResult native_result;
    RRuntimeTaskExternalExecution *execution;
    RStdNetVoidResult *result;
    RStdNetDeadline deadline;
    RStdNetError preexisting_error;
    RStdNetError forced_error;
    uint64_t preexisting_sequence;
    uint64_t forced_sequence;
    int descriptor_close_error;
    _Atomic unsigned int state;
    _Bool stream_owned;
    _Bool preexisting_failure;
    _Bool has_forced_error;
    _Bool forced_deadline;
} RLibraryNetStreamClosePayload;

#if defined(R_LIBRARY_NET_TESTING)
static pthread_mutex_t close_testing_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t close_testing_condition = PTHREAD_COND_INITIALIZER;
static _Bool close_testing_cancel_acknowledgement_armed;
static _Bool close_testing_cancel_acknowledgement_reached;
static _Bool close_testing_cancel_reported;

void r_library_internal_net_tcp_close_testing_arm_cancel_acknowledgement(void) {
    if (pthread_mutex_lock(&close_testing_mutex) != 0) {
        abort();
    }
    if (close_testing_cancel_acknowledgement_armed ||
        close_testing_cancel_acknowledgement_reached || close_testing_cancel_reported) {
        (void)pthread_mutex_unlock(&close_testing_mutex);
        abort();
    }
    close_testing_cancel_acknowledgement_armed = 1;
    if (pthread_mutex_unlock(&close_testing_mutex) != 0) {
        abort();
    }
}

void r_library_internal_net_tcp_close_testing_wait_cancel_reported(void) {
    if (pthread_mutex_lock(&close_testing_mutex) != 0) {
        abort();
    }
    while (!close_testing_cancel_reported) {
        if (pthread_cond_wait(&close_testing_condition, &close_testing_mutex) != 0) {
            abort();
        }
    }
    close_testing_cancel_reported = 0;
    if (pthread_mutex_unlock(&close_testing_mutex) != 0) {
        abort();
    }
}

void r_library_internal_net_tcp_close_testing_wait_cancel_acknowledgement(void) {
    if (pthread_mutex_lock(&close_testing_mutex) != 0) {
        abort();
    }
    while (!close_testing_cancel_acknowledgement_reached) {
        if (pthread_cond_wait(&close_testing_condition, &close_testing_mutex) != 0) {
            abort();
        }
    }
    close_testing_cancel_acknowledgement_reached = 0;
    if (pthread_mutex_unlock(&close_testing_mutex) != 0) {
        abort();
    }
}

static void close_testing_record_cancel_acknowledgement(void) {
    if (pthread_mutex_lock(&close_testing_mutex) != 0) {
        abort();
    }
    if (close_testing_cancel_acknowledgement_armed) {
        close_testing_cancel_acknowledgement_armed = 0;
        close_testing_cancel_acknowledgement_reached = 1;
        if (pthread_cond_broadcast(&close_testing_condition) != 0) {
            (void)pthread_mutex_unlock(&close_testing_mutex);
            abort();
        }
    }
    if (pthread_mutex_unlock(&close_testing_mutex) != 0) {
        abort();
    }
}

static void close_testing_record_cancel_reported(void) {
    if (pthread_mutex_lock(&close_testing_mutex) != 0) {
        abort();
    }
    close_testing_cancel_reported = 1;
    if (pthread_cond_broadcast(&close_testing_condition) != 0) {
        (void)pthread_mutex_unlock(&close_testing_mutex);
        abort();
    }
    if (pthread_mutex_unlock(&close_testing_mutex) != 0) {
        abort();
    }
}
#else
static void close_testing_record_cancel_acknowledgement(void) {
}

static void close_testing_record_cancel_reported(void) {
}
#endif

_Noreturn static void close_panic(void) {
    r_runtime_panic(R_RUNTIME_PANIC_CONTRACT_VIOLATION, (RRuntimeSourceSpan){0U, 0U, 0U});
}

static RStdNetError net_error(RStdNetErrorCode code, int64_t native_code) {
    return (RStdNetError){code, native_code};
}

static void close_payload_move(void *destination_pointer, void *source_pointer) {
    RLibraryNetStreamClosePayload *destination = destination_pointer;
    RLibraryNetStreamClosePayload *source = source_pointer;

    if (source->staged_stream == NULL || source->storage == NULL ||
        source->staged_stream->storage != source->storage) {
        close_panic();
    }
    *destination = *source;
    destination->staged_stream = NULL;
    destination->stream_owned = 1;
    source->staged_stream->storage = NULL;
    source->storage = NULL;
    source->data_io = NULL;
    source->prepared = NULL;
    atomic_init(&destination->state, 0U);
}

static void close_payload_drop(void *value) {
    RLibraryNetStreamClosePayload *payload = value;

    if (payload->prepared != NULL) {
        r_runtime_darwin_io_prepared_abort(&payload->prepared);
    }
    if (payload->request != NULL || payload->data_io != NULL) {
        close_panic();
    }
    if (payload->stream_owned) {
        r_library_internal_net_handle_release(&payload->storage->handle);
        payload->storage = NULL;
        payload->stream_owned = 0;
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

static void fill_close_result(RLibraryNetStreamClosePayload *payload) {
    if (payload->preexisting_failure) {
        *payload->result = (RStdNetVoidResult){
            .r_tag = UINT32_C(1),
            .r_payload.r_error_00000001 = payload->preexisting_error,
        };
    } else if (payload->has_forced_error) {
        *payload->result = (RStdNetVoidResult){
            .r_tag = UINT32_C(1),
            .r_payload.r_error_00000001 = payload->forced_error,
        };
    } else if (payload->native_result.terminal_event_sequence != UINT64_C(0) &&
               (payload->native_result.terminal_event != R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE ||
                payload->native_result.native_error != 0)) {
        *payload->result = (RStdNetVoidResult){
            .r_tag = UINT32_C(1),
            .r_payload.r_error_00000001 = native_result_error(payload->native_result),
        };
    } else if (payload->descriptor_close_error != 0) {
        *payload->result = (RStdNetVoidResult){
            .r_tag = UINT32_C(1),
            .r_payload.r_error_00000001 =
                r_library_internal_net_error_from_native(payload->descriptor_close_error),
        };
    } else {
        *payload->result = (RStdNetVoidResult){0};
    }
}

static void finish_terminal_cleanup(RLibraryNetStreamClosePayload *payload) {
    RRuntimeDarwinIoHandle *detached_data_io = NULL;
    RRuntimeDarwinIoRequest *request = payload->request;
    RStdNetTcpStreamStorage *storage;
    int descriptor;

    if (!payload->stream_owned || payload->storage == NULL) {
        close_panic();
    }
    storage = payload->storage;
    descriptor =
        r_library_internal_net_tcp_stream_take_close_descriptor(storage, &detached_data_io);
    if (detached_data_io != payload->data_io) {
        close_panic();
    }
    errno = 0;
    if (close(descriptor) != 0) {
        payload->descriptor_close_error = errno;
    }
    payload->request = NULL;
    payload->data_io = NULL;
    payload->storage = NULL;
    payload->stream_owned = 0;
    r_runtime_darwin_io_request_release(request);
    r_runtime_darwin_io_handle_release(detached_data_io);
    r_library_internal_net_handle_release(&storage->handle);
}

static void close_try_finalize(RLibraryNetStreamClosePayload *payload) {
    unsigned int state;

    for (;;) {
        unsigned int desired;
        const uint64_t cancellation_sequence =
            r_runtime_task_external_cancellation_sequence(payload->execution);

        state = atomic_load_explicit(&payload->state, memory_order_acquire);
        if ((state & R_LIBRARY_NET_STREAM_CLOSE_FINALIZED) != 0U ||
            (state & R_LIBRARY_NET_STREAM_CLOSE_CALLBACK_RELEASED) == 0U ||
            (cancellation_sequence != UINT64_C(0) &&
             (state & R_LIBRARY_NET_STREAM_CLOSE_CANCEL_REPORTED) == 0U)) {
            return;
        }
        desired = state | R_LIBRARY_NET_STREAM_CLOSE_FINALIZED;
        if (atomic_compare_exchange_weak_explicit(
                &payload->state, &state, desired, memory_order_acq_rel, memory_order_acquire)) {
            state = desired;
            break;
        }
    }
    if ((state & R_LIBRARY_NET_STREAM_CLOSE_COMPLETION_SELECTED) != 0U) {
        fill_close_result(payload);
    }
    if ((state & R_LIBRARY_NET_STREAM_CLOSE_COMPLETION_SELECTED) == 0U &&
        r_runtime_task_external_cancellation_sequence(payload->execution) != UINT64_C(0)) {
        close_testing_record_cancel_acknowledgement();
    }
    r_runtime_task_external_acknowledge(payload->execution);
}

static uint64_t close_outcome_sequence(const RLibraryNetStreamClosePayload *payload) {
    if (payload->preexisting_failure) {
        return payload->preexisting_sequence;
    }
    if (payload->has_forced_error) {
        return payload->forced_sequence;
    }
    return payload->native_result.terminal_event_sequence;
}

static void close_cleanup_completed(RLibraryNetStreamClosePayload *payload) {
    const uint64_t outcome_sequence = close_outcome_sequence(payload);
    unsigned int bits = R_LIBRARY_NET_STREAM_CLOSE_CALLBACK_RELEASED;
    unsigned int previous;

    if (outcome_sequence == UINT64_C(0)) {
        close_panic();
    }
    finish_terminal_cleanup(payload);
    if (r_runtime_task_external_try_select_completion_at(payload->execution, outcome_sequence)) {
        bits |= R_LIBRARY_NET_STREAM_CLOSE_COMPLETION_SELECTED;
    }
    previous = atomic_fetch_or_explicit(&payload->state, bits, memory_order_acq_rel);
    if ((previous & (R_LIBRARY_NET_STREAM_CLOSE_CALLBACK_RELEASED |
                     R_LIBRARY_NET_STREAM_CLOSE_COMPLETION_SELECTED |
                     R_LIBRARY_NET_STREAM_CLOSE_FINALIZED)) != 0U) {
        close_panic();
    }
    close_try_finalize(payload);
}

static void close_completed(RRuntimeDarwinIoRequest *request, void *context) {
    RLibraryNetStreamClosePayload *payload = context;

    if (payload->request != request) {
        close_panic();
    }
    payload->native_result = r_runtime_darwin_io_request_wait(request);
    if (payload->native_result.terminal_event_sequence == UINT64_C(0)) {
        close_panic();
    }
    close_cleanup_completed(payload);
}

static void close_external_cancel(RRuntimeTaskExternalExecution *execution, void *payload_pointer) {
    RLibraryNetStreamClosePayload *payload = payload_pointer;
    unsigned int previous;

    if (payload->execution != execution) {
        close_panic();
    }
    previous = atomic_fetch_or_explicit(
        &payload->state, R_LIBRARY_NET_STREAM_CLOSE_CANCEL_REPORTED, memory_order_acq_rel);
    if ((previous & (R_LIBRARY_NET_STREAM_CLOSE_CANCEL_REPORTED |
                     R_LIBRARY_NET_STREAM_CLOSE_FINALIZED)) != 0U) {
        close_panic();
    }
    close_testing_record_cancel_reported();
    close_try_finalize(payload);
}

static void close_external_start(RRuntimeTaskExternalExecution *execution,
                                 void *payload_pointer,
                                 void *result_pointer) {
    RLibraryNetStreamClosePayload *payload = payload_pointer;
    RRuntimeDarwinIoSubmitResult submitted;

    payload->execution = execution;
    payload->result = result_pointer;
    if (!r_library_internal_net_tcp_stream_mark_closing(payload->storage)) {
        close_panic();
    }
    if (payload->prepared == NULL) {
        r_runtime_task_external_start_ready(execution);
        close_cleanup_completed(payload);
        return;
    }
    submitted =
        r_runtime_darwin_io_prepared_activate_close(&payload->prepared, payload->forced_deadline);
    if (submitted.status != R_RUNTIME_DARWIN_IO_START_OK || submitted.request == NULL ||
        payload->prepared != NULL) {
        close_panic();
    }
    payload->request = submitted.request;
    r_runtime_task_external_start_ready(execution);
    if (!r_runtime_darwin_io_request_set_completion(payload->request, close_completed, payload)) {
        close_panic();
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
        close_panic();
    }
    close_panic();
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
        close_panic();
    }
    close_panic();
}

RStdNetTaskStartResult r_library_internal_net_tcp_close(RStdNetTcpStream *stream,
                                                        RStdNetDeadline deadline) {
    const RRuntimeTypeInfo payload_type = {
        sizeof(RLibraryNetStreamClosePayload),
        _Alignof(RLibraryNetStreamClosePayload),
        close_payload_move,
        close_payload_drop,
    };
    const RRuntimeTypeInfo result_type = {
        sizeof(RStdNetVoidResult),
        _Alignof(RStdNetVoidResult),
        NULL,
        NULL,
    };
    RLibraryNetStreamClosePayload payload = {0};
    RLibraryNetTcpClosePrepareResult native_preparation;
    RLibraryNetDeadlineStatus deadline_status;
    RStdNetError deadline_error = {0};
    RRuntimeTaskPrepareResult task_preparation;
    RRuntimeTaskStartResult started;
    RStdNetTaskStartResult result = {0};
    uint64_t timeout_nanoseconds = 0U;

    payload.staged_stream = stream;
    payload.storage = stream->storage;
    payload.deadline = deadline;
    deadline_status =
        r_library_internal_net_deadline_timeout(deadline, &timeout_nanoseconds, &deadline_error);
    if (deadline_status != R_LIBRARY_NET_DEADLINE_READY) {
        payload.has_forced_error = 1;
        payload.forced_deadline = deadline_status == R_LIBRARY_NET_DEADLINE_EXPIRED;
        payload.forced_error = deadline_error;
        payload.forced_sequence = r_runtime_darwin_event_sequence_next();
        timeout_nanoseconds = 0U;
    }
    native_preparation =
        r_library_internal_net_tcp_stream_prepare_close(stream->storage, timeout_nanoseconds);
    if (native_preparation.status != R_RUNTIME_DARWIN_IO_START_OK) {
        return native_start_failure(native_preparation.status);
    }
    payload.prepared = native_preparation.prepared;
    payload.data_io = native_preparation.data_io;
    payload.preexisting_failure = native_preparation.preexisting_failure;
    payload.preexisting_error = native_preparation.preexisting_error;
    if (payload.preexisting_failure) {
        payload.preexisting_sequence = r_runtime_darwin_event_sequence_next();
        payload.has_forced_error = 0;
        payload.forced_deadline = 0;
    }
    if (payload.prepared == NULL && !payload.preexisting_failure) {
        close_panic();
    }
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

#include "r_library_net_internal.h"

#include "r_runtime_0_1.h"
#include "r_runtime_darwin_event.h"
#include "r_runtime_darwin_io.h"
#include "r_runtime_task.h"
#include "r_runtime_type.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(R_LIBRARY_NET_TESTING)
#include <pthread.h>
#endif

typedef enum RLibraryNetReadImmediate {
    R_LIBRARY_NET_READ_IMMEDIATE_NONE = 0,
    R_LIBRARY_NET_READ_IMMEDIATE_READ,
    R_LIBRARY_NET_READ_IMMEDIATE_END,
    R_LIBRARY_NET_READ_IMMEDIATE_FAILED
} RLibraryNetReadImmediate;

enum {
    R_LIBRARY_NET_READ_CANCEL_REPORTED = 1U,
    R_LIBRARY_NET_READ_CALLBACK_RELEASED = 2U,
    R_LIBRARY_NET_READ_COMPLETION_SELECTED = 4U,
    R_LIBRARY_NET_READ_FINALIZED = 8U
};

typedef struct RLibraryNetReadPayload {
    RLibraryNetReadImmediate immediate;
    RStdNetDeadline deadline;
    RStdNetError immediate_error;
    uint64_t immediate_event_sequence;
    RRuntimeArray *staged_buffer;
    RRuntimeArray buffer;
    /*
     * Borrowed mode (tcp_read_into): the caller keeps target alive until the result is published.
     * The view is submitted with a NULL allocator, reclaimed through request_take_buffer before
     * the request is released, and never destroyed or moved. buffer_owned then records whether
     * the payload (rather than the native request) currently holds the borrow.
     */
    uint8_t *borrowed_data;
    size_t borrowed_length;
    RRuntimeDarwinIoPreparedRequest *prepared;
    RRuntimeDarwinIoRequest *request;
    RRuntimeDarwinIoResult native_result;
    RRuntimeTaskExternalExecution *execution;
    void *result;
    _Atomic unsigned int state;
    _Bool buffer_owned;
    _Bool borrowed;
} RLibraryNetReadPayload;

#if defined(R_LIBRARY_NET_TESTING)
static pthread_mutex_t read_testing_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t read_testing_condition = PTHREAD_COND_INITIALIZER;
static _Bool read_testing_cancel_acknowledgement_armed;
static _Bool read_testing_cancel_acknowledgement_reached;

void r_library_internal_net_tcp_read_testing_arm_cancel_acknowledgement(void) {
    if (pthread_mutex_lock(&read_testing_mutex) != 0) {
        abort();
    }
    if (read_testing_cancel_acknowledgement_armed || read_testing_cancel_acknowledgement_reached) {
        (void)pthread_mutex_unlock(&read_testing_mutex);
        abort();
    }
    read_testing_cancel_acknowledgement_armed = 1;
    if (pthread_mutex_unlock(&read_testing_mutex) != 0) {
        abort();
    }
}

void r_library_internal_net_tcp_read_testing_wait_cancel_acknowledgement(void) {
    if (pthread_mutex_lock(&read_testing_mutex) != 0) {
        abort();
    }
    while (!read_testing_cancel_acknowledgement_reached) {
        if (pthread_cond_wait(&read_testing_condition, &read_testing_mutex) != 0) {
            abort();
        }
    }
    read_testing_cancel_acknowledgement_reached = 0;
    if (pthread_mutex_unlock(&read_testing_mutex) != 0) {
        abort();
    }
}

static void read_testing_record_cancel_acknowledgement(void) {
    if (pthread_mutex_lock(&read_testing_mutex) != 0) {
        abort();
    }
    if (read_testing_cancel_acknowledgement_armed) {
        read_testing_cancel_acknowledgement_armed = 0;
        read_testing_cancel_acknowledgement_reached = 1;
        if (pthread_cond_broadcast(&read_testing_condition) != 0) {
            (void)pthread_mutex_unlock(&read_testing_mutex);
            abort();
        }
    }
    if (pthread_mutex_unlock(&read_testing_mutex) != 0) {
        abort();
    }
}
#else
static void read_testing_record_cancel_acknowledgement(void) {
}
#endif

_Noreturn static void read_panic(void) {
    r_runtime_panic(R_RUNTIME_PANIC_CONTRACT_VIOLATION, (RRuntimeSourceSpan){0U, 0U, 0U});
}

static RStdNetError net_error(RStdNetErrorCode code, int64_t native_code) {
    return (RStdNetError){code, native_code};
}

static void clear_array(RRuntimeArray *array) {
    (void)memset(array, 0, sizeof(*array));
}

static void move_array(RRuntimeArray *destination, RRuntimeArray *source) {
    *destination = *source;
    clear_array(source);
}

static void read_payload_move(void *destination_pointer, void *source_pointer) {
    RLibraryNetReadPayload *destination = destination_pointer;
    RLibraryNetReadPayload *source = source_pointer;

    (void)memset(destination, 0, sizeof(*destination));
    destination->immediate = source->immediate;
    destination->deadline = source->deadline;
    destination->immediate_error = source->immediate_error;
    destination->immediate_event_sequence = source->immediate_event_sequence;
    destination->prepared = source->prepared;
    source->prepared = NULL;
    destination->borrowed = source->borrowed;
    if (source->borrowed) {
        destination->borrowed_data = source->borrowed_data;
        destination->borrowed_length = source->borrowed_length;
        destination->buffer_owned = 1;
    } else {
        if (source->staged_buffer == NULL) {
            read_panic();
        }
        move_array(&destination->buffer, source->staged_buffer);
        destination->buffer_owned = 1;
        source->staged_buffer = NULL;
    }
    atomic_init(&destination->state, 0U);
}

static void read_payload_drop(void *value) {
    RLibraryNetReadPayload *payload = value;

    if (payload->prepared != NULL) {
        r_runtime_darwin_io_prepared_abort(&payload->prepared);
    }
    if (payload->request != NULL) {
        read_panic();
    }
    if (payload->buffer_owned) {
        if (!payload->borrowed) {
            r_runtime_array_destroy(&payload->buffer);
        }
        payload->buffer_owned = 0;
    }
}

static void read_result_move(void *destination_pointer, void *source_pointer) {
    RStdNetTcpReadResult *destination = destination_pointer;
    RStdNetTcpReadResult *source = source_pointer;

    *destination = *source;
    clear_array(&source->buffer);
}

static void read_result_drop(void *value) {
    RStdNetTcpReadResult *result = value;

    r_runtime_array_destroy(&result->buffer);
}

static RRuntimeTypeInfo read_result_type(_Bool borrowed) {
    if (borrowed) {
        return (RRuntimeTypeInfo){
            sizeof(RStdNetCountResult),
            _Alignof(RStdNetCountResult),
            NULL,
            NULL,
        };
    }
    return (RRuntimeTypeInfo){
        sizeof(RStdNetTcpReadResult),
        _Alignof(RStdNetTcpReadResult),
        read_result_move,
        read_result_drop,
    };
}

static void move_payload_buffer(RLibraryNetReadPayload *payload, RRuntimeArray *destination) {
    if (!payload->buffer_owned || payload->borrowed) {
        read_panic();
    }
    move_array(destination, &payload->buffer);
    payload->buffer_owned = 0;
}

static void fill_count_result(RLibraryNetReadPayload *payload,
                              _Bool success,
                              size_t count,
                              RStdNetError error) {
    RStdNetCountResult *result = payload->result;

    if (!payload->buffer_owned) {
        read_panic();
    }
    (void)memset(result, 0, sizeof(*result));
    if (success) {
        result->r_payload.r_value = count;
    } else {
        result->r_tag = UINT32_C(1);
        result->r_payload.r_error_00000001 = error;
    }
    payload->buffer_owned = 0;
}

static void fill_immediate_result(RLibraryNetReadPayload *payload) {
    RStdNetTcpReadResult *result = payload->result;

    if (payload->borrowed) {
        if (payload->immediate == R_LIBRARY_NET_READ_IMMEDIATE_NONE) {
            read_panic();
        }
        fill_count_result(payload,
                          payload->immediate != R_LIBRARY_NET_READ_IMMEDIATE_FAILED,
                          0U,
                          payload->immediate_error);
        return;
    }
    (void)memset(result, 0, sizeof(*result));
    switch (payload->immediate) {
    case R_LIBRARY_NET_READ_IMMEDIATE_READ:
        result->kind = R_STD_NET_TCP_READ_RESULT_READ;
        break;
    case R_LIBRARY_NET_READ_IMMEDIATE_END:
        result->kind = R_STD_NET_TCP_READ_RESULT_END;
        break;
    case R_LIBRARY_NET_READ_IMMEDIATE_FAILED:
        result->kind = R_STD_NET_TCP_READ_RESULT_FAILED;
        result->error = payload->immediate_error;
        break;
    case R_LIBRARY_NET_READ_IMMEDIATE_NONE:
        read_panic();
    }
    move_payload_buffer(payload, &result->buffer);
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

static RRuntimeDarwinIoBuffer native_buffer(const RLibraryNetReadPayload *payload) {
    if (payload->borrowed) {
        return (RRuntimeDarwinIoBuffer){
            NULL,
            payload->borrowed_data,
            payload->borrowed_length,
            0U,
        };
    }
    return (RRuntimeDarwinIoBuffer){
        payload->buffer.allocator,
        payload->buffer.data,
        payload->buffer.length,
        0U,
    };
}

static void restore_native_buffer(RLibraryNetReadPayload *payload) {
    const RRuntimeDarwinIoBuffer expected = native_buffer(payload);
    RRuntimeDarwinIoBuffer buffer;

    if (payload->request == NULL || payload->buffer_owned) {
        read_panic();
    }
    buffer = r_runtime_darwin_io_request_take_buffer(payload->request);
    if (buffer.allocator != expected.allocator || buffer.data != expected.data ||
        buffer.capacity != expected.capacity || buffer.size > expected.capacity) {
        read_panic();
    }
    payload->buffer_owned = 1;
}

static void fill_native_result(RLibraryNetReadPayload *payload) {
    const RRuntimeDarwinIoResult native_result = payload->native_result;
    const _Bool success = native_result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE &&
                          native_result.native_error == 0;
    RStdNetTcpReadResult *result = payload->result;

    restore_native_buffer(payload);
    if (payload->borrowed) {
        const _Bool read_success =
            success && (native_result.bytes_transferred != 0U || native_result.eof);

        fill_count_result(payload,
                          read_success,
                          read_success ? native_result.bytes_transferred : 0U,
                          read_success ? net_error(R_STD_NET_ERROR_OTHER, INT64_C(0))
                                       : native_result_error(native_result));
        return;
    }
    (void)memset(result, 0, sizeof(*result));
    if (success && native_result.bytes_transferred != 0U) {
        result->kind = R_STD_NET_TCP_READ_RESULT_READ;
    } else if (success && native_result.eof) {
        result->kind = R_STD_NET_TCP_READ_RESULT_END;
    } else {
        result->kind = R_STD_NET_TCP_READ_RESULT_FAILED;
        result->error = native_result_error(native_result);
    }
    result->count = native_result.bytes_transferred;
    move_payload_buffer(payload, &result->buffer);
}

static void release_native_request(RLibraryNetReadPayload *payload) {
    RRuntimeDarwinIoRequest *request = payload->request;

    if (request == NULL) {
        return;
    }
    payload->request = NULL;
    r_runtime_darwin_io_request_release(request);
}

static void read_try_finalize(RLibraryNetReadPayload *payload) {
    unsigned int state;
    _Bool cancelled_without_result;

    for (;;) {
        unsigned int desired;
        const uint64_t cancellation_sequence =
            r_runtime_task_external_cancellation_sequence(payload->execution);

        state = atomic_load_explicit(&payload->state, memory_order_acquire);
        if ((state & R_LIBRARY_NET_READ_FINALIZED) != 0U ||
            (state & R_LIBRARY_NET_READ_CALLBACK_RELEASED) == 0U ||
            (cancellation_sequence != UINT64_C(0) &&
             (state & R_LIBRARY_NET_READ_CANCEL_REPORTED) == 0U)) {
            return;
        }
        desired = state | R_LIBRARY_NET_READ_FINALIZED;
        if (atomic_compare_exchange_weak_explicit(
                &payload->state, &state, desired, memory_order_acq_rel, memory_order_acquire)) {
            state = desired;
            break;
        }
    }
    if ((state & R_LIBRARY_NET_READ_COMPLETION_SELECTED) != 0U) {
        if (payload->request == NULL) {
            fill_immediate_result(payload);
        } else {
            fill_native_result(payload);
        }
    } else if (payload->request != NULL) {
        restore_native_buffer(payload);
    }
    cancelled_without_result =
        (state & R_LIBRARY_NET_READ_COMPLETION_SELECTED) == 0U &&
        r_runtime_task_external_cancellation_sequence(payload->execution) != UINT64_C(0);
    release_native_request(payload);
    if (cancelled_without_result) {
        read_testing_record_cancel_acknowledgement();
    }
    r_runtime_task_external_acknowledge(payload->execution);
}

static void native_completed(RRuntimeDarwinIoRequest *request, void *context) {
    RLibraryNetReadPayload *payload = context;
    RRuntimeDarwinIoResult native_result;
    unsigned int bits = R_LIBRARY_NET_READ_CALLBACK_RELEASED;
    unsigned int previous;

    if (payload->request != request) {
        read_panic();
    }
    native_result = r_runtime_darwin_io_request_wait(request);
    if (native_result.terminal_event_sequence == UINT64_C(0)) {
        read_panic();
    }
    if (r_runtime_task_external_try_select_completion_at(payload->execution,
                                                         native_result.terminal_event_sequence)) {
        bits |= R_LIBRARY_NET_READ_COMPLETION_SELECTED;
    }
    payload->native_result = native_result;
    previous = atomic_fetch_or_explicit(&payload->state, bits, memory_order_acq_rel);
    if ((previous & (R_LIBRARY_NET_READ_CALLBACK_RELEASED | R_LIBRARY_NET_READ_COMPLETION_SELECTED |
                     R_LIBRARY_NET_READ_FINALIZED)) != 0U) {
        read_panic();
    }
    read_try_finalize(payload);
}

static void external_cancel(RRuntimeTaskExternalExecution *execution, void *payload_pointer) {
    RLibraryNetReadPayload *payload = payload_pointer;
    unsigned int previous;

    if (payload->execution != execution) {
        read_panic();
    }
    if (payload->request != NULL) {
        (void)r_runtime_darwin_io_request_cancel(payload->request);
    }
    previous = atomic_fetch_or_explicit(
        &payload->state, R_LIBRARY_NET_READ_CANCEL_REPORTED, memory_order_acq_rel);
    if ((previous & (R_LIBRARY_NET_READ_CANCEL_REPORTED | R_LIBRARY_NET_READ_FINALIZED)) != 0U) {
        read_panic();
    }
    read_try_finalize(payload);
}

static void complete_immediately(RLibraryNetReadPayload *payload,
                                 RRuntimeTaskExternalExecution *execution) {
    uint64_t cancellation_before_ready;
    unsigned int bits = R_LIBRARY_NET_READ_CALLBACK_RELEASED;
    unsigned int previous;

    if (payload->immediate != R_LIBRARY_NET_READ_IMMEDIATE_NONE) {
        if (payload->immediate_event_sequence == UINT64_C(0)) {
            read_panic();
        }
        if (r_runtime_task_external_try_select_completion_at(execution,
                                                             payload->immediate_event_sequence)) {
            bits |= R_LIBRARY_NET_READ_COMPLETION_SELECTED;
        }
    }
    previous = atomic_fetch_or_explicit(&payload->state, bits, memory_order_acq_rel);
    if (previous != 0U) {
        read_panic();
    }
    cancellation_before_ready = r_runtime_task_external_cancellation_sequence(execution);
    r_runtime_task_external_start_ready(execution);
    if (cancellation_before_ready != UINT64_C(0) &&
        (bits & R_LIBRARY_NET_READ_COMPLETION_SELECTED) != 0U) {
        previous = atomic_fetch_or_explicit(
            &payload->state, R_LIBRARY_NET_READ_CANCEL_REPORTED, memory_order_acq_rel);
        if ((previous & (R_LIBRARY_NET_READ_CANCEL_REPORTED | R_LIBRARY_NET_READ_FINALIZED)) !=
            0U) {
            read_panic();
        }
    }
    read_try_finalize(payload);
}

static void external_start(RRuntimeTaskExternalExecution *execution,
                           void *payload_pointer,
                           void *result_pointer) {
    RLibraryNetReadPayload *payload = payload_pointer;
    RRuntimeDarwinIoSubmitResult submitted;
    RRuntimeDarwinIoBuffer buffer;
    RStdNetError deadline_error = {0};
    uint64_t ignored_timeout = 0U;

    payload->execution = execution;
    payload->result = result_pointer;
    if (payload->immediate == R_LIBRARY_NET_READ_IMMEDIATE_NONE && payload->deadline.has_value &&
        r_library_internal_net_deadline_timeout(
            payload->deadline, &ignored_timeout, &deadline_error) != R_LIBRARY_NET_DEADLINE_READY) {
        r_runtime_darwin_io_prepared_abort(&payload->prepared);
        payload->immediate = R_LIBRARY_NET_READ_IMMEDIATE_FAILED;
        payload->immediate_error = deadline_error;
        payload->immediate_event_sequence = r_runtime_darwin_event_sequence_next();
    }
    if (payload->immediate != R_LIBRARY_NET_READ_IMMEDIATE_NONE ||
        r_runtime_task_external_cancel_requested(execution)) {
        if (payload->prepared != NULL) {
            r_runtime_darwin_io_prepared_abort(&payload->prepared);
        }
        complete_immediately(payload, execution);
        return;
    }
    buffer = native_buffer(payload);
    submitted = r_runtime_darwin_io_prepared_activate(&payload->prepared, &buffer);
    if (submitted.status != R_RUNTIME_DARWIN_IO_START_OK || submitted.request == NULL ||
        payload->prepared != NULL) {
        read_panic();
    }
    payload->request = submitted.request;
    payload->buffer_owned = 0;
    r_runtime_task_external_start_ready(execution);
    if (!r_runtime_darwin_io_request_set_completion(payload->request, native_completed, payload)) {
        read_panic();
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
        read_panic();
    }
    read_panic();
}

static RStdNetTaskStartResult start_task(RLibraryNetReadPayload *payload) {
    const RRuntimeTypeInfo payload_type = {
        sizeof(RLibraryNetReadPayload),
        _Alignof(RLibraryNetReadPayload),
        read_payload_move,
        read_payload_drop,
    };
    RRuntimeTaskPrepareResult prepared = r_runtime_task_external_start_prepare(
        payload_type, read_result_type(payload->borrowed), external_start, external_cancel);
    RRuntimeTaskStartResult started;
    RStdNetTaskStartResult result = {0};

    if (prepared.status != R_RUNTIME_TASK_START_OK) {
        r_runtime_darwin_io_prepared_abort(&payload->prepared);
        return task_start_failure(prepared.status);
    }
    started = r_runtime_task_start_commit(&prepared.transaction, payload);
    if (started.status != R_RUNTIME_TASK_START_OK) {
        r_runtime_darwin_io_prepared_abort(&payload->prepared);
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
        read_panic();
    }
    read_panic();
}

/*
 * view describes the staged storage exactly as it is submitted natively (capacity = readable
 * length, size = 0). The payload already names either the staged owner or the borrowed view.
 */
static RStdNetTaskStartResult start_read(const RStdNetTcpStream *stream,
                                         RLibraryNetReadPayload *payload,
                                         RRuntimeDarwinIoBuffer view,
                                         RStdNetDeadline deadline) {
    RLibraryNetTcpReadPrepareResult native_preparation;
    RStdNetError deadline_error = {0};
    uint64_t timeout_nanoseconds = 0U;

    payload->deadline = deadline;
    if (view.capacity == 0U) {
        payload->immediate = R_LIBRARY_NET_READ_IMMEDIATE_READ;
        payload->immediate_event_sequence = r_runtime_darwin_event_sequence_next();
        return start_task(payload);
    }
    if (r_library_internal_net_deadline_timeout(deadline, &timeout_nanoseconds, &deadline_error) !=
        R_LIBRARY_NET_DEADLINE_READY) {
        payload->immediate = R_LIBRARY_NET_READ_IMMEDIATE_FAILED;
        payload->immediate_error = deadline_error;
        payload->immediate_event_sequence = r_runtime_darwin_event_sequence_next();
        return start_task(payload);
    }
    native_preparation =
        r_library_internal_net_tcp_stream_prepare_read(stream->storage, &view, timeout_nanoseconds);
    if (native_preparation.end) {
        payload->immediate = R_LIBRARY_NET_READ_IMMEDIATE_END;
        payload->immediate_event_sequence = r_runtime_darwin_event_sequence_next();
        return start_task(payload);
    }
    if (native_preparation.status == R_RUNTIME_DARWIN_IO_START_HANDLE_CLOSED) {
        payload->immediate = R_LIBRARY_NET_READ_IMMEDIATE_FAILED;
        payload->immediate_error = net_error(R_STD_NET_ERROR_CLOSED, INT64_C(0));
        payload->immediate_event_sequence = r_runtime_darwin_event_sequence_next();
        return start_task(payload);
    }
    if (native_preparation.status != R_RUNTIME_DARWIN_IO_START_OK) {
        return native_start_failure(native_preparation.status);
    }
    if (native_preparation.prepared == NULL) {
        read_panic();
    }
    payload->prepared = native_preparation.prepared;
    return start_task(payload);
}

RStdNetTaskStartResult r_library_internal_net_tcp_read(const RStdNetTcpStream *stream,
                                                       RRuntimeArray *buffer,
                                                       RStdNetDeadline deadline) {
    RLibraryNetReadPayload payload = {0};
    const RRuntimeDarwinIoBuffer view = {
        buffer->allocator,
        buffer->data,
        buffer->length,
        0U,
    };

    payload.staged_buffer = buffer;
    return start_read(stream, &payload, view, deadline);
}

RStdNetTaskStartResult r_library_internal_net_tcp_read_into(const RStdNetTcpStream *stream,
                                                            RStdNetMutableBytes target,
                                                            RStdNetDeadline deadline) {
    RLibraryNetReadPayload payload = {0};
    RRuntimeDarwinIoBuffer view;

    if (target.length != 0U && target.data == NULL) {
        read_panic();
    }
    payload.borrowed = 1;
    payload.borrowed_data = target.data;
    payload.borrowed_length = target.length;
    view = native_buffer(&payload);
    return start_read(stream, &payload, view, deadline);
}

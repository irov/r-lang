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

typedef enum RLibraryNetWriteMode {
    R_LIBRARY_NET_WRITE_SOME = 0,
    R_LIBRARY_NET_WRITE_ALL
} RLibraryNetWriteMode;

typedef enum RLibraryNetWriteImmediate {
    R_LIBRARY_NET_WRITE_IMMEDIATE_NONE = 0,
    R_LIBRARY_NET_WRITE_IMMEDIATE_WRITTEN,
    R_LIBRARY_NET_WRITE_IMMEDIATE_FAILED
} RLibraryNetWriteImmediate;

enum {
    R_LIBRARY_NET_WRITE_CANCEL_REPORTED = 1U,
    R_LIBRARY_NET_WRITE_CALLBACK_RELEASED = 2U,
    R_LIBRARY_NET_WRITE_COMPLETION_SELECTED = 4U,
    R_LIBRARY_NET_WRITE_FINALIZED = 8U
};

typedef struct RLibraryNetWritePayload {
    RLibraryNetWriteMode mode;
    RLibraryNetWriteImmediate immediate;
    RStdNetDeadline deadline;
    RStdNetError immediate_error;
    uint64_t immediate_event_sequence;
    RRuntimeArray *staged_buffer;
    RRuntimeArray buffer;
    /*
     * Borrowed mode (tcp_write_from/tcp_write_all_from): the caller keeps source alive until the
     * result is published. The immutable view is submitted with a NULL allocator, reclaimed
     * through request_take_buffer before the request is released, and never destroyed or moved.
     * buffer_owned then records whether the payload (rather than the native request) holds it.
     */
    const uint8_t *borrowed_data;
    size_t borrowed_length;
    RRuntimeDarwinIoPreparedRequest *prepared;
    RRuntimeDarwinIoRequest *request;
    RRuntimeDarwinIoResult native_result;
    RRuntimeTaskExternalExecution *execution;
    void *result;
    _Atomic unsigned int state;
    _Bool buffer_owned;
    _Bool borrowed;
} RLibraryNetWritePayload;

#if defined(R_LIBRARY_NET_TESTING)
static pthread_mutex_t write_testing_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t write_testing_condition = PTHREAD_COND_INITIALIZER;
static _Bool write_testing_cancel_acknowledgement_armed;
static _Bool write_testing_cancel_acknowledgement_reached;
static _Bool write_testing_cancel_reported;

void r_library_internal_net_tcp_write_testing_arm_cancel_acknowledgement(void) {
    if (pthread_mutex_lock(&write_testing_mutex) != 0) {
        abort();
    }
    if (write_testing_cancel_acknowledgement_armed ||
        write_testing_cancel_acknowledgement_reached || write_testing_cancel_reported) {
        (void)pthread_mutex_unlock(&write_testing_mutex);
        abort();
    }
    write_testing_cancel_acknowledgement_armed = 1;
    if (pthread_mutex_unlock(&write_testing_mutex) != 0) {
        abort();
    }
}

void r_library_internal_net_tcp_write_testing_wait_cancel_reported(void) {
    if (pthread_mutex_lock(&write_testing_mutex) != 0) {
        abort();
    }
    while (!write_testing_cancel_reported) {
        if (pthread_cond_wait(&write_testing_condition, &write_testing_mutex) != 0) {
            abort();
        }
    }
    write_testing_cancel_reported = 0;
    if (pthread_mutex_unlock(&write_testing_mutex) != 0) {
        abort();
    }
}

void r_library_internal_net_tcp_write_testing_wait_cancel_acknowledgement(void) {
    if (pthread_mutex_lock(&write_testing_mutex) != 0) {
        abort();
    }
    while (!write_testing_cancel_acknowledgement_reached) {
        if (pthread_cond_wait(&write_testing_condition, &write_testing_mutex) != 0) {
            abort();
        }
    }
    write_testing_cancel_acknowledgement_reached = 0;
    if (pthread_mutex_unlock(&write_testing_mutex) != 0) {
        abort();
    }
}

static void write_testing_record_cancel_acknowledgement(void) {
    if (pthread_mutex_lock(&write_testing_mutex) != 0) {
        abort();
    }
    if (write_testing_cancel_acknowledgement_armed) {
        write_testing_cancel_acknowledgement_armed = 0;
        write_testing_cancel_acknowledgement_reached = 1;
        if (pthread_cond_broadcast(&write_testing_condition) != 0) {
            (void)pthread_mutex_unlock(&write_testing_mutex);
            abort();
        }
    }
    if (pthread_mutex_unlock(&write_testing_mutex) != 0) {
        abort();
    }
}

static void write_testing_record_cancel_reported(void) {
    if (pthread_mutex_lock(&write_testing_mutex) != 0) {
        abort();
    }
    write_testing_cancel_reported = 1;
    if (pthread_cond_broadcast(&write_testing_condition) != 0) {
        (void)pthread_mutex_unlock(&write_testing_mutex);
        abort();
    }
    if (pthread_mutex_unlock(&write_testing_mutex) != 0) {
        abort();
    }
}
#else
static void write_testing_record_cancel_acknowledgement(void) {
}

static void write_testing_record_cancel_reported(void) {
}
#endif

_Noreturn static void write_panic(void) {
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

static void write_payload_move(void *destination_pointer, void *source_pointer) {
    RLibraryNetWritePayload *destination = destination_pointer;
    RLibraryNetWritePayload *source = source_pointer;

    (void)memset(destination, 0, sizeof(*destination));
    destination->mode = source->mode;
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
            write_panic();
        }
        move_array(&destination->buffer, source->staged_buffer);
        destination->buffer_owned = 1;
        source->staged_buffer = NULL;
    }
    atomic_init(&destination->state, 0U);
}

static void write_payload_drop(void *value) {
    RLibraryNetWritePayload *payload = value;

    if (payload->prepared != NULL) {
        r_runtime_darwin_io_prepared_abort(&payload->prepared);
    }
    if (payload->request != NULL) {
        write_panic();
    }
    if (payload->buffer_owned) {
        if (!payload->borrowed) {
            r_runtime_array_destroy(&payload->buffer);
        }
        payload->buffer_owned = 0;
    }
}

static void write_result_move(void *destination_pointer, void *source_pointer) {
    RStdNetTcpWriteResult *destination = destination_pointer;
    RStdNetTcpWriteResult *source = source_pointer;

    *destination = *source;
    clear_array(&source->buffer);
}

static void write_result_drop(void *value) {
    RStdNetTcpWriteResult *result = value;

    r_runtime_array_destroy(&result->buffer);
}

static void write_all_result_move(void *destination_pointer, void *source_pointer) {
    RStdNetTcpWriteAllResult *destination = destination_pointer;
    RStdNetTcpWriteAllResult *source = source_pointer;

    *destination = *source;
    clear_array(&source->buffer);
}

static void write_all_result_drop(void *value) {
    RStdNetTcpWriteAllResult *result = value;

    r_runtime_array_destroy(&result->buffer);
}

static RRuntimeTypeInfo write_result_type(RLibraryNetWriteMode mode, _Bool borrowed) {
    switch (mode) {
    case R_LIBRARY_NET_WRITE_SOME:
        if (borrowed) {
            return (RRuntimeTypeInfo){
                sizeof(RStdNetCountResult),
                _Alignof(RStdNetCountResult),
                NULL,
                NULL,
            };
        }
        return (RRuntimeTypeInfo){
            sizeof(RStdNetTcpWriteResult),
            _Alignof(RStdNetTcpWriteResult),
            write_result_move,
            write_result_drop,
        };
    case R_LIBRARY_NET_WRITE_ALL:
        if (borrowed) {
            return (RRuntimeTypeInfo){
                sizeof(RStdNetVoidResult),
                _Alignof(RStdNetVoidResult),
                NULL,
                NULL,
            };
        }
        return (RRuntimeTypeInfo){
            sizeof(RStdNetTcpWriteAllResult),
            _Alignof(RStdNetTcpWriteAllResult),
            write_all_result_move,
            write_all_result_drop,
        };
    }
    write_panic();
}

static void move_payload_buffer(RLibraryNetWritePayload *payload, RRuntimeArray *destination) {
    if (!payload->buffer_owned || payload->borrowed) {
        write_panic();
    }
    move_array(destination, &payload->buffer);
    payload->buffer_owned = 0;
}

static size_t payload_length(const RLibraryNetWritePayload *payload) {
    return payload->borrowed ? payload->borrowed_length : payload->buffer.length;
}

/* Borrowed completion: write_from carries a count, write_all_from the void carrier. */
static void fill_borrowed_result(RLibraryNetWritePayload *payload,
                                 _Bool success,
                                 size_t count,
                                 RStdNetError error) {
    if (!payload->buffer_owned || !payload->borrowed) {
        write_panic();
    }
    if (payload->mode == R_LIBRARY_NET_WRITE_ALL) {
        RStdNetVoidResult *result = payload->result;

        (void)memset(result, 0, sizeof(*result));
        if (!success) {
            result->r_tag = UINT32_C(1);
            result->r_payload.r_error_00000001 = error;
        }
    } else {
        RStdNetCountResult *result = payload->result;

        (void)memset(result, 0, sizeof(*result));
        if (success) {
            result->r_payload.r_value = count;
        } else {
            result->r_tag = UINT32_C(1);
            result->r_payload.r_error_00000001 = error;
        }
    }
    payload->buffer_owned = 0;
}

static void fill_immediate_result(RLibraryNetWritePayload *payload) {
    const _Bool success = payload->immediate == R_LIBRARY_NET_WRITE_IMMEDIATE_WRITTEN;

    if (payload->borrowed) {
        fill_borrowed_result(payload, success, 0U, payload->immediate_error);
        return;
    }
    switch (payload->mode) {
    case R_LIBRARY_NET_WRITE_SOME: {
        RStdNetTcpWriteResult *result = payload->result;

        (void)memset(result, 0, sizeof(*result));
        result->kind =
            success ? R_STD_NET_TCP_WRITE_RESULT_WRITTEN : R_STD_NET_TCP_WRITE_RESULT_FAILED;
        result->error = payload->immediate_error;
        move_payload_buffer(payload, &result->buffer);
        return;
    }
    case R_LIBRARY_NET_WRITE_ALL: {
        RStdNetTcpWriteAllResult *result = payload->result;

        (void)memset(result, 0, sizeof(*result));
        result->kind = success ? R_STD_NET_TCP_WRITE_ALL_RESULT_WRITTEN
                               : R_STD_NET_TCP_WRITE_ALL_RESULT_FAILED;
        result->error = payload->immediate_error;
        move_payload_buffer(payload, &result->buffer);
        return;
    }
    }
    write_panic();
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

/* The runtime reads borrowed write sources through an immutable Dispatch data view. */
static unsigned char *borrowed_write_data(const uint8_t *data) {
    return (unsigned char *)(uintptr_t)data;
}

static RRuntimeDarwinIoBuffer native_buffer(const RLibraryNetWritePayload *payload) {
    if (payload->borrowed) {
        return (RRuntimeDarwinIoBuffer){
            NULL,
            borrowed_write_data(payload->borrowed_data),
            payload->borrowed_length,
            payload->borrowed_length,
        };
    }
    return (RRuntimeDarwinIoBuffer){
        payload->buffer.allocator,
        payload->buffer.data,
        payload->buffer.capacity,
        payload->buffer.length,
    };
}

static void restore_native_buffer(RLibraryNetWritePayload *payload) {
    const RRuntimeDarwinIoBuffer expected = native_buffer(payload);
    RRuntimeDarwinIoBuffer buffer;

    if (payload->request == NULL || payload->buffer_owned) {
        write_panic();
    }
    buffer = r_runtime_darwin_io_request_take_buffer(payload->request);
    if (buffer.allocator != expected.allocator || buffer.data != expected.data ||
        buffer.capacity != expected.capacity || buffer.size != expected.size) {
        write_panic();
    }
    payload->buffer_owned = 1;
}

static void fill_native_result(RLibraryNetWritePayload *payload) {
    const RRuntimeDarwinIoResult native_result = payload->native_result;
    const _Bool native_success =
        native_result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE &&
        native_result.native_error == 0;
    const _Bool success =
        native_success &&
        ((payload->mode == R_LIBRARY_NET_WRITE_SOME && native_result.bytes_transferred != 0U) ||
         (payload->mode == R_LIBRARY_NET_WRITE_ALL &&
          native_result.bytes_transferred == payload_length(payload)));

    restore_native_buffer(payload);
    if (payload->borrowed) {
        fill_borrowed_result(payload,
                             success,
                             success ? native_result.bytes_transferred : 0U,
                             success ? net_error(R_STD_NET_ERROR_OTHER, INT64_C(0))
                                     : native_result_error(native_result));
        return;
    }
    switch (payload->mode) {
    case R_LIBRARY_NET_WRITE_SOME: {
        RStdNetTcpWriteResult *result = payload->result;

        (void)memset(result, 0, sizeof(*result));
        result->kind =
            success ? R_STD_NET_TCP_WRITE_RESULT_WRITTEN : R_STD_NET_TCP_WRITE_RESULT_FAILED;
        result->written = native_result.bytes_transferred;
        if (!success) {
            result->error = native_result_error(native_result);
        }
        move_payload_buffer(payload, &result->buffer);
        return;
    }
    case R_LIBRARY_NET_WRITE_ALL: {
        RStdNetTcpWriteAllResult *result = payload->result;

        (void)memset(result, 0, sizeof(*result));
        result->kind = success ? R_STD_NET_TCP_WRITE_ALL_RESULT_WRITTEN
                               : R_STD_NET_TCP_WRITE_ALL_RESULT_FAILED;
        result->written = native_result.bytes_transferred;
        if (!success) {
            result->error = native_result_error(native_result);
        }
        move_payload_buffer(payload, &result->buffer);
        return;
    }
    }
    write_panic();
}

static void release_native_request(RLibraryNetWritePayload *payload) {
    RRuntimeDarwinIoRequest *request = payload->request;

    if (request == NULL) {
        return;
    }
    payload->request = NULL;
    r_runtime_darwin_io_request_release(request);
}

static void write_try_finalize(RLibraryNetWritePayload *payload) {
    unsigned int state;
    _Bool cancelled_without_result;

    for (;;) {
        unsigned int desired;
        const uint64_t cancellation_sequence =
            r_runtime_task_external_cancellation_sequence(payload->execution);

        state = atomic_load_explicit(&payload->state, memory_order_acquire);
        if ((state & R_LIBRARY_NET_WRITE_FINALIZED) != 0U ||
            (state & R_LIBRARY_NET_WRITE_CALLBACK_RELEASED) == 0U ||
            (cancellation_sequence != UINT64_C(0) &&
             (state & R_LIBRARY_NET_WRITE_CANCEL_REPORTED) == 0U)) {
            return;
        }
        desired = state | R_LIBRARY_NET_WRITE_FINALIZED;
        if (atomic_compare_exchange_weak_explicit(
                &payload->state, &state, desired, memory_order_acq_rel, memory_order_acquire)) {
            state = desired;
            break;
        }
    }
    if ((state & R_LIBRARY_NET_WRITE_COMPLETION_SELECTED) != 0U) {
        if (payload->request == NULL) {
            fill_immediate_result(payload);
        } else {
            fill_native_result(payload);
        }
    } else if (payload->request != NULL) {
        restore_native_buffer(payload);
    }
    cancelled_without_result =
        (state & R_LIBRARY_NET_WRITE_COMPLETION_SELECTED) == 0U &&
        r_runtime_task_external_cancellation_sequence(payload->execution) != UINT64_C(0);
    release_native_request(payload);
    if (cancelled_without_result) {
        write_testing_record_cancel_acknowledgement();
    }
    r_runtime_task_external_acknowledge(payload->execution);
}

static void native_completed(RRuntimeDarwinIoRequest *request, void *context) {
    RLibraryNetWritePayload *payload = context;
    RRuntimeDarwinIoResult native_result;
    unsigned int bits = R_LIBRARY_NET_WRITE_CALLBACK_RELEASED;
    unsigned int previous;

    if (payload->request != request) {
        write_panic();
    }
    native_result = r_runtime_darwin_io_request_wait(request);
    if (native_result.terminal_event_sequence == UINT64_C(0)) {
        write_panic();
    }
    if (r_runtime_task_external_try_select_completion_at(payload->execution,
                                                         native_result.terminal_event_sequence)) {
        bits |= R_LIBRARY_NET_WRITE_COMPLETION_SELECTED;
    }
    payload->native_result = native_result;
    previous = atomic_fetch_or_explicit(&payload->state, bits, memory_order_acq_rel);
    if ((previous & (R_LIBRARY_NET_WRITE_CALLBACK_RELEASED |
                     R_LIBRARY_NET_WRITE_COMPLETION_SELECTED | R_LIBRARY_NET_WRITE_FINALIZED)) !=
        0U) {
        write_panic();
    }
    write_try_finalize(payload);
}

static void external_cancel(RRuntimeTaskExternalExecution *execution, void *payload_pointer) {
    RLibraryNetWritePayload *payload = payload_pointer;
    unsigned int previous;

    if (payload->execution != execution) {
        write_panic();
    }
    if (payload->request != NULL) {
        (void)r_runtime_darwin_io_request_cancel(payload->request);
    }
    previous = atomic_fetch_or_explicit(
        &payload->state, R_LIBRARY_NET_WRITE_CANCEL_REPORTED, memory_order_acq_rel);
    if ((previous & (R_LIBRARY_NET_WRITE_CANCEL_REPORTED | R_LIBRARY_NET_WRITE_FINALIZED)) != 0U) {
        write_panic();
    }
    write_testing_record_cancel_reported();
    write_try_finalize(payload);
}

static void complete_immediately(RLibraryNetWritePayload *payload,
                                 RRuntimeTaskExternalExecution *execution) {
    uint64_t cancellation_before_ready;
    unsigned int bits = R_LIBRARY_NET_WRITE_CALLBACK_RELEASED;
    unsigned int previous;

    if (payload->immediate != R_LIBRARY_NET_WRITE_IMMEDIATE_NONE) {
        if (payload->immediate_event_sequence == UINT64_C(0)) {
            write_panic();
        }
        if (r_runtime_task_external_try_select_completion_at(execution,
                                                             payload->immediate_event_sequence)) {
            bits |= R_LIBRARY_NET_WRITE_COMPLETION_SELECTED;
        }
    }
    previous = atomic_fetch_or_explicit(&payload->state, bits, memory_order_acq_rel);
    if (previous != 0U) {
        write_panic();
    }
    cancellation_before_ready = r_runtime_task_external_cancellation_sequence(execution);
    r_runtime_task_external_start_ready(execution);
    if (cancellation_before_ready != UINT64_C(0) &&
        (bits & R_LIBRARY_NET_WRITE_COMPLETION_SELECTED) != 0U) {
        previous = atomic_fetch_or_explicit(
            &payload->state, R_LIBRARY_NET_WRITE_CANCEL_REPORTED, memory_order_acq_rel);
        if ((previous & (R_LIBRARY_NET_WRITE_CANCEL_REPORTED | R_LIBRARY_NET_WRITE_FINALIZED)) !=
            0U) {
            write_panic();
        }
    }
    write_try_finalize(payload);
}

static void external_start(RRuntimeTaskExternalExecution *execution,
                           void *payload_pointer,
                           void *result_pointer) {
    RLibraryNetWritePayload *payload = payload_pointer;
    RRuntimeDarwinIoSubmitResult submitted;
    RRuntimeDarwinIoBuffer buffer;
    RStdNetError deadline_error = {0};
    uint64_t ignored_timeout = 0U;

    payload->execution = execution;
    payload->result = result_pointer;
    if (payload->immediate == R_LIBRARY_NET_WRITE_IMMEDIATE_NONE && payload->deadline.has_value &&
        r_library_internal_net_deadline_timeout(
            payload->deadline, &ignored_timeout, &deadline_error) != R_LIBRARY_NET_DEADLINE_READY) {
        r_runtime_darwin_io_prepared_abort(&payload->prepared);
        payload->immediate = R_LIBRARY_NET_WRITE_IMMEDIATE_FAILED;
        payload->immediate_error = deadline_error;
        payload->immediate_event_sequence = r_runtime_darwin_event_sequence_next();
    }
    if (payload->immediate != R_LIBRARY_NET_WRITE_IMMEDIATE_NONE ||
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
        write_panic();
    }
    payload->request = submitted.request;
    payload->buffer_owned = 0;
    r_runtime_task_external_start_ready(execution);
    if (!r_runtime_darwin_io_request_set_completion(payload->request, native_completed, payload)) {
        write_panic();
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
        write_panic();
    }
    write_panic();
}

static RStdNetTaskStartResult start_task(RLibraryNetWritePayload *payload) {
    const RRuntimeTypeInfo payload_type = {
        sizeof(RLibraryNetWritePayload),
        _Alignof(RLibraryNetWritePayload),
        write_payload_move,
        write_payload_drop,
    };
    RRuntimeTaskPrepareResult prepared =
        r_runtime_task_external_start_prepare(payload_type,
                                              write_result_type(payload->mode, payload->borrowed),
                                              external_start,
                                              external_cancel);
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
        write_panic();
    }
    write_panic();
}

/*
 * view describes the staged storage exactly as it is submitted natively (size = writable length).
 * The payload already names either the staged owner or the borrowed view.
 */
static RStdNetTaskStartResult start_write(const RStdNetTcpStream *stream,
                                          RLibraryNetWritePayload *payload,
                                          RRuntimeDarwinIoBuffer view,
                                          RStdNetDeadline deadline,
                                          RLibraryNetWriteMode mode) {
    RLibraryNetTcpWritePrepareResult native_preparation;
    RStdNetError deadline_error = {0};
    uint64_t timeout_nanoseconds = 0U;

    payload->mode = mode;
    payload->deadline = deadline;
    if (view.size == 0U) {
        payload->immediate = R_LIBRARY_NET_WRITE_IMMEDIATE_WRITTEN;
        payload->immediate_event_sequence = r_runtime_darwin_event_sequence_next();
        return start_task(payload);
    }
    if (r_library_internal_net_deadline_timeout(deadline, &timeout_nanoseconds, &deadline_error) !=
        R_LIBRARY_NET_DEADLINE_READY) {
        payload->immediate = R_LIBRARY_NET_WRITE_IMMEDIATE_FAILED;
        payload->immediate_error = deadline_error;
        payload->immediate_event_sequence = r_runtime_darwin_event_sequence_next();
        return start_task(payload);
    }
    native_preparation = r_library_internal_net_tcp_stream_prepare_write(
        stream->storage, &view, timeout_nanoseconds, mode == R_LIBRARY_NET_WRITE_ALL);
    if (native_preparation.write_shutdown) {
        payload->immediate = R_LIBRARY_NET_WRITE_IMMEDIATE_FAILED;
        payload->immediate_error = net_error(R_STD_NET_ERROR_NOT_CONNECTED, INT64_C(0));
        payload->immediate_event_sequence = r_runtime_darwin_event_sequence_next();
        return start_task(payload);
    }
    if (native_preparation.status == R_RUNTIME_DARWIN_IO_START_HANDLE_CLOSED) {
        payload->immediate = R_LIBRARY_NET_WRITE_IMMEDIATE_FAILED;
        payload->immediate_error = net_error(R_STD_NET_ERROR_CLOSED, INT64_C(0));
        payload->immediate_event_sequence = r_runtime_darwin_event_sequence_next();
        return start_task(payload);
    }
    if (native_preparation.status != R_RUNTIME_DARWIN_IO_START_OK) {
        return native_start_failure(native_preparation.status);
    }
    if (native_preparation.prepared == NULL) {
        write_panic();
    }
    payload->prepared = native_preparation.prepared;
    return start_task(payload);
}

static RStdNetTaskStartResult start_owned_write(const RStdNetTcpStream *stream,
                                                RRuntimeArray *buffer,
                                                RStdNetDeadline deadline,
                                                RLibraryNetWriteMode mode) {
    RLibraryNetWritePayload payload = {0};
    const RRuntimeDarwinIoBuffer view = {
        buffer->allocator,
        buffer->data,
        buffer->capacity,
        buffer->length,
    };

    payload.staged_buffer = buffer;
    return start_write(stream, &payload, view, deadline, mode);
}

static RStdNetTaskStartResult start_borrowed_write(const RStdNetTcpStream *stream,
                                                   RStdNetConstBytes source,
                                                   RStdNetDeadline deadline,
                                                   RLibraryNetWriteMode mode) {
    RLibraryNetWritePayload payload = {0};
    RRuntimeDarwinIoBuffer view;

    if (source.length != 0U && source.data == NULL) {
        write_panic();
    }
    payload.borrowed = 1;
    payload.borrowed_data = source.data;
    payload.borrowed_length = source.length;
    view = native_buffer(&payload);
    return start_write(stream, &payload, view, deadline, mode);
}

RStdNetTaskStartResult r_library_internal_net_tcp_write(const RStdNetTcpStream *stream,
                                                        RRuntimeArray *buffer,
                                                        RStdNetDeadline deadline) {
    return start_owned_write(stream, buffer, deadline, R_LIBRARY_NET_WRITE_SOME);
}

RStdNetTaskStartResult r_library_internal_net_tcp_write_all(const RStdNetTcpStream *stream,
                                                            RRuntimeArray *buffer,
                                                            RStdNetDeadline deadline) {
    return start_owned_write(stream, buffer, deadline, R_LIBRARY_NET_WRITE_ALL);
}

RStdNetTaskStartResult r_library_internal_net_tcp_write_from(const RStdNetTcpStream *stream,
                                                             RStdNetConstBytes source,
                                                             RStdNetDeadline deadline) {
    return start_borrowed_write(stream, source, deadline, R_LIBRARY_NET_WRITE_SOME);
}

RStdNetTaskStartResult r_library_internal_net_tcp_write_all_from(const RStdNetTcpStream *stream,
                                                                 RStdNetConstBytes source,
                                                                 RStdNetDeadline deadline) {
    return start_borrowed_write(stream, source, deadline, R_LIBRARY_NET_WRITE_ALL);
}

#include "r_library_io_internal.h"

#include "r_library_clock_internal.h"
#include "r_library_duration_internal.h"
#include "r_runtime_0_1.h"
#include "r_runtime_darwin_event.h"
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

typedef enum RLibraryIoMode {
    R_LIBRARY_IO_READ = 0,
    R_LIBRARY_IO_WRITE,
    R_LIBRARY_IO_WRITE_ALL,
    R_LIBRARY_IO_WRITE_SHARED,
    R_LIBRARY_IO_FLUSH
} RLibraryIoMode;

typedef enum RLibraryIoImmediate {
    R_LIBRARY_IO_IMMEDIATE_NONE = 0,
    R_LIBRARY_IO_IMMEDIATE_SUCCESS,
    R_LIBRARY_IO_IMMEDIATE_ERROR
} RLibraryIoImmediate;

typedef enum RLibraryIoDeadlineStatus {
    R_LIBRARY_IO_DEADLINE_READY = 0,
    R_LIBRARY_IO_DEADLINE_EXPIRED,
    R_LIBRARY_IO_DEADLINE_ERROR
} RLibraryIoDeadlineStatus;

enum {
    R_LIBRARY_IO_CANCEL_REPORTED = 1U,
    R_LIBRARY_IO_CALLBACK_RELEASED = 2U,
    R_LIBRARY_IO_COMPLETION_SELECTED = 4U
};

typedef struct RLibraryIoPayload {
    RLibraryIoMode mode;
    RLibraryIoImmediate immediate;
    RStdIoDeadline deadline;
    RStdIoError immediate_error;
    uint64_t immediate_event_sequence;
    RRuntimeArray *staged_buffer;
    RRuntimeArray buffer;
    /*
     * Borrowed mode (read_into/write_from/write_all_from): the caller keeps the view alive until
     * the result is published. It is submitted with a NULL allocator, reclaimed through
     * request_take_buffer before the request is released, and never destroyed or moved.
     * buffer_owned then records whether the payload (rather than the native request) holds it.
     */
    uint8_t *borrowed_data;
    size_t borrowed_length;
    RRuntimeArc *staged_shared_buffer;
    RRuntimeArc shared_buffer;
    size_t shared_offset;
    size_t shared_length;
    RRuntimeDarwinIoPreparedRequest *prepared;
    RRuntimeDarwinIoRequest *request;
    RRuntimeDarwinIoResult native_result;
    RRuntimeTaskExternalExecution *execution;
    void *result;
    _Atomic unsigned int cancellation_state;
    _Bool buffer_owned;
    _Bool shared_buffer_owned;
    _Bool borrowed;
} RLibraryIoPayload;

_Noreturn static void io_panic(void) {
    r_runtime_panic(R_RUNTIME_PANIC_CONTRACT_VIOLATION, (RRuntimeSourceSpan){0U, 0U, 0U});
}

static void clear_array(RRuntimeArray *array) {
    (void)memset(array, 0, sizeof(*array));
}

static void move_array(RRuntimeArray *destination, RRuntimeArray *source) {
    *destination = *source;
    clear_array(source);
}

static _Bool mode_has_array_buffer(RLibraryIoMode mode) {
    return mode == R_LIBRARY_IO_READ || mode == R_LIBRARY_IO_WRITE ||
           mode == R_LIBRARY_IO_WRITE_ALL;
}

static void payload_move(void *destination_pointer, void *source_pointer) {
    RLibraryIoPayload *destination = destination_pointer;
    RLibraryIoPayload *source = source_pointer;

    (void)memset(destination, 0, sizeof(*destination));
    destination->mode = source->mode;
    destination->immediate = source->immediate;
    destination->deadline = source->deadline;
    destination->immediate_error = source->immediate_error;
    destination->immediate_event_sequence = source->immediate_event_sequence;
    destination->shared_offset = source->shared_offset;
    destination->shared_length = source->shared_length;
    destination->prepared = source->prepared;
    source->prepared = NULL;
    destination->borrowed = source->borrowed;
    if (source->borrowed) {
        destination->borrowed_data = source->borrowed_data;
        destination->borrowed_length = source->borrowed_length;
        destination->buffer_owned = 1;
    } else if (source->staged_buffer != NULL) {
        move_array(&destination->buffer, source->staged_buffer);
        destination->buffer_owned = 1;
        source->staged_buffer = NULL;
    }
    if (source->staged_shared_buffer != NULL) {
        destination->shared_buffer = *source->staged_shared_buffer;
        source->staged_shared_buffer->control = NULL;
        source->staged_shared_buffer = NULL;
        destination->shared_buffer_owned = 1;
    }
    atomic_init(&destination->cancellation_state, 0U);
}

static void payload_drop(void *value) {
    RLibraryIoPayload *payload = value;

    if (payload->prepared != NULL) {
        r_runtime_darwin_io_prepared_abort(&payload->prepared);
    }
    if (payload->request != NULL) {
        io_panic();
    }
    if (payload->buffer_owned) {
        if (!payload->borrowed) {
            r_runtime_array_destroy(&payload->buffer);
        }
        payload->buffer_owned = 0;
    }
    if (payload->shared_buffer_owned) {
        r_runtime_arc_release(&payload->shared_buffer);
        payload->shared_buffer_owned = 0;
    }
}

void r_library_internal_io_read_result_move(RStdIoReadResult *destination,
                                            RStdIoReadResult *source) {
    *destination = *source;
    clear_array(&source->buffer);
}

void r_library_internal_io_read_result_drop(RStdIoReadResult *result) {
    r_runtime_array_destroy(&result->buffer);
}

static void read_result_move(void *destination, void *source) {
    r_library_internal_io_read_result_move(destination, source);
}

static void read_result_drop(void *result) {
    r_library_internal_io_read_result_drop(result);
}

void r_library_internal_io_write_result_move(RStdIoWriteResult *destination,
                                             RStdIoWriteResult *source) {
    *destination = *source;
    clear_array(&source->buffer);
}

void r_library_internal_io_write_result_drop(RStdIoWriteResult *result) {
    r_runtime_array_destroy(&result->buffer);
}

static void write_result_move(void *destination, void *source) {
    r_library_internal_io_write_result_move(destination, source);
}

static void write_result_drop(void *result) {
    r_library_internal_io_write_result_drop(result);
}

void r_library_internal_io_write_all_result_move(RStdIoWriteAllResult *destination,
                                                 RStdIoWriteAllResult *source) {
    *destination = *source;
    clear_array(&source->buffer);
}

void r_library_internal_io_write_all_result_drop(RStdIoWriteAllResult *result) {
    r_runtime_array_destroy(&result->buffer);
}

static void write_all_result_move(void *destination, void *source) {
    r_library_internal_io_write_all_result_move(destination, source);
}

static void write_all_result_drop(void *result) {
    r_library_internal_io_write_all_result_drop(result);
}

void r_library_internal_io_shared_write_result_move(RStdIoSharedWriteResult *destination,
                                                    RStdIoSharedWriteResult *source) {
    *destination = *source;
    source->buffer.control = NULL;
}

void r_library_internal_io_shared_write_result_drop(RStdIoSharedWriteResult *result) {
    r_runtime_arc_release(&result->buffer);
}

static void shared_write_result_move(void *destination, void *source) {
    r_library_internal_io_shared_write_result_move(destination, source);
}

static void shared_write_result_drop(void *result) {
    r_library_internal_io_shared_write_result_drop(result);
}

static RRuntimeTypeInfo borrowed_result_type(RLibraryIoMode mode) {
    switch (mode) {
    case R_LIBRARY_IO_READ:
    case R_LIBRARY_IO_WRITE:
        return (RRuntimeTypeInfo){
            sizeof(RStdIoCountResult),
            _Alignof(RStdIoCountResult),
            NULL,
            NULL,
        };
    case R_LIBRARY_IO_WRITE_ALL:
        return (RRuntimeTypeInfo){
            sizeof(RStdIoVoidResult),
            _Alignof(RStdIoVoidResult),
            NULL,
            NULL,
        };
    case R_LIBRARY_IO_WRITE_SHARED:
    case R_LIBRARY_IO_FLUSH:
        io_panic();
    }
    io_panic();
}

static RRuntimeTypeInfo result_type(RLibraryIoMode mode, _Bool borrowed) {
    if (borrowed) {
        return borrowed_result_type(mode);
    }
    switch (mode) {
    case R_LIBRARY_IO_READ:
        return (RRuntimeTypeInfo){
            sizeof(RStdIoReadResult),
            _Alignof(RStdIoReadResult),
            read_result_move,
            read_result_drop,
        };
    case R_LIBRARY_IO_WRITE:
        return (RRuntimeTypeInfo){
            sizeof(RStdIoWriteResult),
            _Alignof(RStdIoWriteResult),
            write_result_move,
            write_result_drop,
        };
    case R_LIBRARY_IO_WRITE_ALL:
        return (RRuntimeTypeInfo){
            sizeof(RStdIoWriteAllResult),
            _Alignof(RStdIoWriteAllResult),
            write_all_result_move,
            write_all_result_drop,
        };
    case R_LIBRARY_IO_WRITE_SHARED:
        return (RRuntimeTypeInfo){
            sizeof(RStdIoSharedWriteResult),
            _Alignof(RStdIoSharedWriteResult),
            shared_write_result_move,
            shared_write_result_drop,
        };
    case R_LIBRARY_IO_FLUSH:
        return (RRuntimeTypeInfo){
            sizeof(RStdIoVoidResult),
            _Alignof(RStdIoVoidResult),
            NULL,
            NULL,
        };
    }
    io_panic();
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

static RLibraryIoDeadlineStatus
deadline_timeout(RStdIoDeadline deadline, uint64_t *timeout_nanoseconds, RStdIoError *error) {
    const uint64_t nanoseconds_per_second = UINT64_C(1000000000);
    RStdTimeInstantResult now;
    RStdTimeDurationTimeResult remaining;
    uint64_t seconds;

    *timeout_nanoseconds = 0U;
    if (!deadline.has_value) {
        return R_LIBRARY_IO_DEADLINE_READY;
    }
    if (deadline.value.storage_seconds < 0 ||
        deadline.value.storage_nanoseconds >= R_STD_TIME_NANOSECONDS_PER_SECOND) {
        *error = io_error(R_STD_IO_ERROR_INVALID_OPERATION, INT64_C(0));
        return R_LIBRARY_IO_DEADLINE_ERROR;
    }
    now = r_library_internal_time_monotonic_now(r_library_internal_time_darwin_clock_hooks());
    if (!now.is_ok) {
        *error = io_error(R_STD_IO_ERROR_OTHER, now.error.native_code);
        return R_LIBRARY_IO_DEADLINE_ERROR;
    }
    if (compare_instant(deadline.value, now.value) <= 0) {
        *error = io_error(R_STD_IO_ERROR_TIMED_OUT, INT64_C(0));
        return R_LIBRARY_IO_DEADLINE_EXPIRED;
    }
    remaining = r_library_internal_time_instant_duration(deadline.value, now.value);
    if (!remaining.is_ok || remaining.value.seconds < 0) {
        *error = io_error(R_STD_IO_ERROR_OTHER, INT64_C(0));
        return R_LIBRARY_IO_DEADLINE_ERROR;
    }
    seconds = (uint64_t)remaining.value.seconds;
    if (seconds > ((uint64_t)INT64_MAX / nanoseconds_per_second)) {
        *error = io_error(R_STD_IO_ERROR_OTHER, INT64_C(0));
        return R_LIBRARY_IO_DEADLINE_ERROR;
    }
    *timeout_nanoseconds = seconds * nanoseconds_per_second;
    if ((uint64_t)remaining.value.nanoseconds > ((uint64_t)INT64_MAX - *timeout_nanoseconds)) {
        *error = io_error(R_STD_IO_ERROR_OTHER, INT64_C(0));
        return R_LIBRARY_IO_DEADLINE_ERROR;
    }
    *timeout_nanoseconds += (uint64_t)remaining.value.nanoseconds;
    return R_LIBRARY_IO_DEADLINE_READY;
}

static void move_payload_buffer(RLibraryIoPayload *payload, RRuntimeArray *destination) {
    if (!payload->buffer_owned || payload->borrowed) {
        io_panic();
    }
    move_array(destination, &payload->buffer);
    payload->buffer_owned = 0;
}

/* Borrowed completion: read_into/write_from carry a count, write_all_from the void carrier. */
static void
fill_borrowed_result(RLibraryIoPayload *payload, _Bool success, size_t count, RStdIoError error) {
    if (!payload->buffer_owned || !payload->borrowed) {
        io_panic();
    }
    if (payload->mode == R_LIBRARY_IO_WRITE_ALL) {
        RStdIoVoidResult *result = payload->result;

        (void)memset(result, 0, sizeof(*result));
        if (!success) {
            result->r_tag = UINT32_C(1);
            result->r_payload.r_err = error;
        }
    } else {
        RStdIoCountResult *result = payload->result;

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

static void move_payload_shared_buffer(RLibraryIoPayload *payload, RRuntimeArc *destination) {
    if (!payload->shared_buffer_owned) {
        io_panic();
    }
    *destination = payload->shared_buffer;
    payload->shared_buffer.control = NULL;
    payload->shared_buffer_owned = 0;
}

static void fill_immediate_result(RLibraryIoPayload *payload) {
    const _Bool success = payload->immediate == R_LIBRARY_IO_IMMEDIATE_SUCCESS;

    if (payload->borrowed) {
        fill_borrowed_result(payload, success, 0U, payload->immediate_error);
        return;
    }
    switch (payload->mode) {
    case R_LIBRARY_IO_READ: {
        RStdIoReadResult *result = payload->result;

        (void)memset(result, 0, sizeof(*result));
        result->kind = success ? R_STD_IO_READ_RESULT_READ : R_STD_IO_READ_RESULT_FAILED;
        result->error = payload->immediate_error;
        move_payload_buffer(payload, &result->buffer);
        return;
    }
    case R_LIBRARY_IO_WRITE: {
        RStdIoWriteResult *result = payload->result;

        (void)memset(result, 0, sizeof(*result));
        result->kind = success ? R_STD_IO_WRITE_RESULT_WRITTEN : R_STD_IO_WRITE_RESULT_FAILED;
        result->error = payload->immediate_error;
        move_payload_buffer(payload, &result->buffer);
        return;
    }
    case R_LIBRARY_IO_WRITE_ALL: {
        RStdIoWriteAllResult *result = payload->result;

        (void)memset(result, 0, sizeof(*result));
        result->kind =
            success ? R_STD_IO_WRITE_ALL_RESULT_WRITTEN : R_STD_IO_WRITE_ALL_RESULT_FAILED;
        result->error = payload->immediate_error;
        move_payload_buffer(payload, &result->buffer);
        return;
    }
    case R_LIBRARY_IO_WRITE_SHARED: {
        RStdIoSharedWriteResult *result = payload->result;

        (void)memset(result, 0, sizeof(*result));
        result->kind =
            success ? R_STD_IO_SHARED_WRITE_RESULT_WRITTEN : R_STD_IO_SHARED_WRITE_RESULT_FAILED;
        result->error = payload->immediate_error;
        move_payload_shared_buffer(payload, &result->buffer);
        return;
    }
    case R_LIBRARY_IO_FLUSH: {
        RStdIoVoidResult *result = payload->result;

        (void)memset(result, 0, sizeof(*result));
        if (!success) {
            result->r_tag = UINT32_C(1);
            result->r_payload.r_err = payload->immediate_error;
        }
        return;
    }
    }
    io_panic();
}

static RStdIoError result_error(RRuntimeDarwinIoResult result) {
    if (result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_CANCELLED) {
        return io_error(R_STD_IO_ERROR_CANCELLED, INT64_C(0));
    }
    if (result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_TIMED_OUT) {
        return io_error(R_STD_IO_ERROR_TIMED_OUT, INT64_C(0));
    }
    if (result.native_error != 0) {
        return classify_native_error(result.native_error);
    }
    return io_error(R_STD_IO_ERROR_OTHER, INT64_C(0));
}

/* Staged storage exactly as submitted natively: the owner's array or the borrowed view. */
static RRuntimeDarwinIoBuffer staged_native_view(RLibraryIoMode mode,
                                                 const RRuntimeArray *buffer,
                                                 uint8_t *borrowed_data,
                                                 size_t borrowed_length,
                                                 _Bool borrowed) {
    RRuntimeDarwinIoBuffer view = {0};

    if (borrowed) {
        view.data = borrowed_data;
        view.capacity = borrowed_length;
        view.size = mode == R_LIBRARY_IO_READ ? 0U : borrowed_length;
        return view;
    }
    view.allocator = buffer->allocator;
    view.data = buffer->data;
    view.capacity = mode == R_LIBRARY_IO_READ ? buffer->length : buffer->capacity;
    view.size = mode == R_LIBRARY_IO_READ ? 0U : buffer->length;
    return view;
}

static RRuntimeDarwinIoBuffer native_buffer(const RLibraryIoPayload *payload) {
    return staged_native_view(payload->mode,
                              &payload->buffer,
                              payload->borrowed_data,
                              payload->borrowed_length,
                              payload->borrowed);
}

static void restore_native_buffer(RLibraryIoPayload *payload) {
    const RRuntimeDarwinIoBuffer expected = native_buffer(payload);
    RRuntimeDarwinIoBuffer buffer;

    if (!mode_has_array_buffer(payload->mode) || payload->buffer_owned) {
        io_panic();
    }
    buffer = r_runtime_darwin_io_request_take_buffer(payload->request);
    if (buffer.allocator != expected.allocator || buffer.data != expected.data ||
        buffer.capacity != expected.capacity) {
        io_panic();
    }
    payload->buffer_owned = 1;
}

static void fill_native_result(RLibraryIoPayload *payload, RRuntimeDarwinIoResult native_result) {
    const _Bool success = native_result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE &&
                          native_result.native_error == 0;

    if (mode_has_array_buffer(payload->mode)) {
        restore_native_buffer(payload);
    }
    if (payload->borrowed) {
        const _Bool borrowed_success =
            success && (payload->mode != R_LIBRARY_IO_READ ||
                        native_result.bytes_transferred != 0U || native_result.eof);

        fill_borrowed_result(payload,
                             borrowed_success,
                             borrowed_success ? native_result.bytes_transferred : 0U,
                             borrowed_success ? io_error(R_STD_IO_ERROR_OTHER, INT64_C(0))
                                              : result_error(native_result));
        return;
    }
    switch (payload->mode) {
    case R_LIBRARY_IO_READ: {
        RStdIoReadResult *result = payload->result;

        (void)memset(result, 0, sizeof(*result));
        if (success && native_result.bytes_transferred != 0U) {
            result->kind = R_STD_IO_READ_RESULT_READ;
        } else if (success && native_result.eof) {
            result->kind = R_STD_IO_READ_RESULT_END;
        } else {
            result->kind = R_STD_IO_READ_RESULT_FAILED;
            result->error = result_error(native_result);
        }
        result->count = native_result.bytes_transferred;
        move_payload_buffer(payload, &result->buffer);
        return;
    }
    case R_LIBRARY_IO_WRITE: {
        RStdIoWriteResult *result = payload->result;

        (void)memset(result, 0, sizeof(*result));
        result->kind = success ? R_STD_IO_WRITE_RESULT_WRITTEN : R_STD_IO_WRITE_RESULT_FAILED;
        result->count = native_result.bytes_transferred;
        if (!success) {
            result->error = result_error(native_result);
        }
        move_payload_buffer(payload, &result->buffer);
        return;
    }
    case R_LIBRARY_IO_WRITE_ALL: {
        RStdIoWriteAllResult *result = payload->result;

        (void)memset(result, 0, sizeof(*result));
        result->kind =
            success ? R_STD_IO_WRITE_ALL_RESULT_WRITTEN : R_STD_IO_WRITE_ALL_RESULT_FAILED;
        result->written = native_result.bytes_transferred;
        if (!success) {
            result->error = result_error(native_result);
        }
        move_payload_buffer(payload, &result->buffer);
        return;
    }
    case R_LIBRARY_IO_WRITE_SHARED: {
        RStdIoSharedWriteResult *result = payload->result;

        (void)memset(result, 0, sizeof(*result));
        result->kind =
            success ? R_STD_IO_SHARED_WRITE_RESULT_WRITTEN : R_STD_IO_SHARED_WRITE_RESULT_FAILED;
        result->written = native_result.bytes_transferred;
        if (!success) {
            result->error = result_error(native_result);
        }
        move_payload_shared_buffer(payload, &result->buffer);
        return;
    }
    case R_LIBRARY_IO_FLUSH: {
        RStdIoVoidResult *result = payload->result;

        (void)memset(result, 0, sizeof(*result));
        if (!success) {
            result->r_tag = UINT32_C(1);
            result->r_payload.r_err = result_error(native_result);
        }
        return;
    }
    }
    io_panic();
}

static void release_native_request(RLibraryIoPayload *payload) {
    RRuntimeDarwinIoRequest *request = payload->request;

    if (request == NULL) {
        io_panic();
    }
    payload->request = NULL;
    r_runtime_darwin_io_request_release(request);
}

static void finalize_cancellation(RLibraryIoPayload *payload) {
    if (payload->request != NULL) {
        if (mode_has_array_buffer(payload->mode)) {
            restore_native_buffer(payload);
        }
        release_native_request(payload);
    }
    r_runtime_task_external_acknowledge(payload->execution);
}

static void finalize_selected_completion(RLibraryIoPayload *payload) {
    fill_native_result(payload, payload->native_result);
    release_native_request(payload);
    r_runtime_task_external_acknowledge(payload->execution);
}

static void native_completed(RRuntimeDarwinIoRequest *request, void *context) {
    RLibraryIoPayload *payload = context;
    RRuntimeDarwinIoResult result;
    uint64_t cancellation_sequence;
    unsigned int previous_state;

    if (payload->request != request) {
        io_panic();
    }
    result = r_runtime_darwin_io_request_wait(request);
    if (result.terminal_event_sequence == UINT64_C(0)) {
        io_panic();
    }
    if (r_runtime_task_external_try_select_completion_at(payload->execution,
                                                         result.terminal_event_sequence)) {
        cancellation_sequence = r_runtime_task_external_cancellation_sequence(payload->execution);
        if (cancellation_sequence == UINT64_C(0)) {
            payload->native_result = result;
            finalize_selected_completion(payload);
            return;
        }
        payload->native_result = result;
        previous_state = atomic_fetch_or_explicit(&payload->cancellation_state,
                                                  R_LIBRARY_IO_CALLBACK_RELEASED |
                                                      R_LIBRARY_IO_COMPLETION_SELECTED,
                                                  memory_order_acq_rel);
        if ((previous_state &
             (R_LIBRARY_IO_CALLBACK_RELEASED | R_LIBRARY_IO_COMPLETION_SELECTED)) != 0U) {
            io_panic();
        }
        if ((previous_state & R_LIBRARY_IO_CANCEL_REPORTED) != 0U) {
            finalize_selected_completion(payload);
        }
        return;
    }
    if (r_runtime_task_external_cancellation_sequence(payload->execution) == UINT64_C(0)) {
        io_panic();
    }
    previous_state = atomic_fetch_or_explicit(
        &payload->cancellation_state, R_LIBRARY_IO_CALLBACK_RELEASED, memory_order_acq_rel);
    if ((previous_state & (R_LIBRARY_IO_CALLBACK_RELEASED | R_LIBRARY_IO_COMPLETION_SELECTED)) !=
        0U) {
        io_panic();
    }
    if ((previous_state & R_LIBRARY_IO_CANCEL_REPORTED) != 0U) {
        finalize_cancellation(payload);
    }
}

static void external_cancel(RRuntimeTaskExternalExecution *execution, void *payload_pointer) {
    RLibraryIoPayload *payload = payload_pointer;
    unsigned int previous_state;

    if (payload->execution != execution) {
        io_panic();
    }
    if (payload->request == NULL) {
        previous_state = atomic_fetch_or_explicit(
            &payload->cancellation_state, R_LIBRARY_IO_CANCEL_REPORTED, memory_order_acq_rel);
        if ((previous_state & R_LIBRARY_IO_CANCEL_REPORTED) != 0U) {
            io_panic();
        }
        if ((previous_state & R_LIBRARY_IO_CALLBACK_RELEASED) != 0U) {
            if ((previous_state & R_LIBRARY_IO_COMPLETION_SELECTED) == 0U) {
                io_panic();
            }
            finalize_selected_completion(payload);
        } else {
            finalize_cancellation(payload);
        }
        return;
    }
    (void)r_runtime_darwin_io_request_cancel(payload->request);
    previous_state = atomic_fetch_or_explicit(
        &payload->cancellation_state, R_LIBRARY_IO_CANCEL_REPORTED, memory_order_acq_rel);
    if ((previous_state & R_LIBRARY_IO_CANCEL_REPORTED) != 0U) {
        io_panic();
    }
    if ((previous_state & R_LIBRARY_IO_CALLBACK_RELEASED) != 0U) {
        if ((previous_state & R_LIBRARY_IO_COMPLETION_SELECTED) != 0U) {
            finalize_selected_completion(payload);
        } else {
            finalize_cancellation(payload);
        }
    }
}

static void complete_immediately(RLibraryIoPayload *payload,
                                 RRuntimeTaskExternalExecution *execution) {
    _Bool selected = 0;

    if (payload->immediate != R_LIBRARY_IO_IMMEDIATE_NONE) {
        if (payload->immediate_event_sequence == UINT64_C(0)) {
            io_panic();
        }
        selected = r_runtime_task_external_try_select_completion_at(
            execution, payload->immediate_event_sequence);
    }

    r_runtime_task_external_start_ready(execution);
    if (selected) {
        fill_immediate_result(payload);
        r_runtime_task_external_acknowledge(execution);
    }
}

static void external_start(RRuntimeTaskExternalExecution *execution,
                           void *payload_pointer,
                           void *result_pointer) {
    RLibraryIoPayload *payload = payload_pointer;
    RRuntimeDarwinIoSubmitResult submission;
    RRuntimeDarwinIoBuffer buffer;
    RStdIoError deadline_error = {0};
    uint64_t ignored_timeout = 0U;

    payload->execution = execution;
    payload->result = result_pointer;
    if (payload->immediate == R_LIBRARY_IO_IMMEDIATE_NONE && payload->deadline.has_value &&
        deadline_timeout(payload->deadline, &ignored_timeout, &deadline_error) !=
            R_LIBRARY_IO_DEADLINE_READY) {
        r_runtime_darwin_io_prepared_abort(&payload->prepared);
        payload->immediate = R_LIBRARY_IO_IMMEDIATE_ERROR;
        payload->immediate_error = deadline_error;
        payload->immediate_event_sequence = r_runtime_darwin_event_sequence_next();
    }
    if (payload->immediate != R_LIBRARY_IO_IMMEDIATE_NONE ||
        r_runtime_task_external_cancel_requested(execution)) {
        if (payload->prepared != NULL) {
            r_runtime_darwin_io_prepared_abort(&payload->prepared);
        }
        complete_immediately(payload, execution);
        return;
    }
    if (mode_has_array_buffer(payload->mode)) {
        buffer = native_buffer(payload);
        submission = r_runtime_darwin_io_prepared_activate(&payload->prepared, &buffer);
    } else {
        submission = r_runtime_darwin_io_prepared_activate(&payload->prepared, NULL);
    }
    if (submission.status != R_RUNTIME_DARWIN_IO_START_OK || submission.request == NULL ||
        payload->prepared != NULL) {
        io_panic();
    }
    payload->request = submission.request;
    if (mode_has_array_buffer(payload->mode)) {
        payload->buffer_owned = 0;
    }
    r_runtime_task_external_start_ready(execution);
    if (!r_runtime_darwin_io_request_set_completion(payload->request, native_completed, payload)) {
        io_panic();
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
        io_panic();
    }
    io_panic();
}

static RStdIoTaskStartResult start_task(RLibraryIoPayload *payload) {
    const RRuntimeTypeInfo payload_type = {
        sizeof(RLibraryIoPayload),
        _Alignof(RLibraryIoPayload),
        payload_move,
        payload_drop,
    };
    RRuntimeTaskPrepareResult preparation =
        r_runtime_task_external_start_prepare(payload_type,
                                              result_type(payload->mode, payload->borrowed),
                                              external_start,
                                              external_cancel);
    RRuntimeTaskStartResult started;
    RStdIoTaskStartResult result = {0};

    if (preparation.status != R_RUNTIME_TASK_START_OK) {
        r_runtime_darwin_io_prepared_abort(&payload->prepared);
        return task_start_failure(preparation.status);
    }
    started = r_runtime_task_start_commit(&preparation.transaction, payload);
    if (started.status != R_RUNTIME_TASK_START_OK) {
        r_runtime_darwin_io_prepared_abort(&payload->prepared);
        return task_start_failure(started.status);
    }
    result.is_ok = 1;
    result.task = started.task;
    return result;
}

static RStdIoError prepare_status_error(RRuntimeDarwinIoPrepareResult preparation) {
    switch (preparation.status) {
    case R_RUNTIME_DARWIN_IO_START_HANDLE_CLOSED:
        return io_error(R_STD_IO_ERROR_CLOSED, (int64_t)preparation.native_error);
    case R_RUNTIME_DARWIN_IO_START_INVALID_ARGUMENT:
    case R_RUNTIME_DARWIN_IO_START_ORDERING_BLOCKED:
        return io_error(R_STD_IO_ERROR_INVALID_OPERATION, INT64_C(0));
    case R_RUNTIME_DARWIN_IO_START_OK:
    case R_RUNTIME_DARWIN_IO_START_ALLOCATION_FAILED:
    case R_RUNTIME_DARWIN_IO_START_NATIVE_RETAIN_FAILED:
        io_panic();
    }
    io_panic();
}

typedef struct RLibraryIoBorrowedBytes {
    uint8_t *data;
    size_t length;
} RLibraryIoBorrowedBytes;

/*
 * borrowed is NULL for the owned/shared/flush forms. A borrowed view with a nonzero length and a
 * NULL pointer violates the caller contract; the runtime never sees such a view.
 */
static RStdIoTaskStartResult start(RLibraryIoMode mode,
                                   RRuntimeDarwinIoHandle *handle,
                                   RRuntimeArray *buffer,
                                   const RLibraryIoBorrowedBytes *borrowed,
                                   RRuntimeArc *shared_buffer,
                                   size_t shared_offset,
                                   size_t shared_length,
                                   RStdIoDeadline deadline) {
    RLibraryIoPayload payload;
    RRuntimeDarwinIoPrepareResult native_preparation;
    RLibraryIoDeadlineStatus deadline_status;
    RStdIoError deadline_error = {0};
    uint64_t timeout_nanoseconds = 0U;
    size_t staged_length = 0U;

    (void)memset(&payload, 0, sizeof(payload));
    payload.mode = mode;
    payload.deadline = deadline;
    payload.staged_buffer = buffer;
    payload.staged_shared_buffer = shared_buffer;
    payload.shared_offset = shared_offset;
    payload.shared_length = shared_length;
    if (borrowed != NULL) {
        if (!mode_has_array_buffer(mode) || (borrowed->length != 0U && borrowed->data == NULL)) {
            io_panic();
        }
        payload.borrowed = 1;
        payload.borrowed_data = borrowed->data;
        payload.borrowed_length = borrowed->length;
        staged_length = borrowed->length;
    } else if (mode_has_array_buffer(mode)) {
        staged_length = buffer->length;
    }
    if (mode == R_LIBRARY_IO_WRITE_SHARED) {
        const RRuntimeArray *shared_array;

        shared_array = r_runtime_arc_get(shared_buffer);
        if (shared_offset > shared_array->length ||
            shared_length > shared_array->length - shared_offset) {
            payload.immediate = R_LIBRARY_IO_IMMEDIATE_ERROR;
            payload.immediate_error = io_error(R_STD_IO_ERROR_INVALID_OPERATION, INT64_C(0));
            payload.immediate_event_sequence = r_runtime_darwin_event_sequence_next();
            return start_task(&payload);
        }
    }
    if ((mode_has_array_buffer(mode) && staged_length == 0U) ||
        (mode == R_LIBRARY_IO_WRITE_SHARED && shared_length == 0U)) {
        payload.immediate = R_LIBRARY_IO_IMMEDIATE_SUCCESS;
        payload.immediate_event_sequence = r_runtime_darwin_event_sequence_next();
        return start_task(&payload);
    }
    deadline_status = deadline_timeout(deadline, &timeout_nanoseconds, &deadline_error);
    if (deadline_status != R_LIBRARY_IO_DEADLINE_READY) {
        payload.immediate = R_LIBRARY_IO_IMMEDIATE_ERROR;
        payload.immediate_error = deadline_error;
        payload.immediate_event_sequence = r_runtime_darwin_event_sequence_next();
        return start_task(&payload);
    }
    switch (mode) {
    case R_LIBRARY_IO_READ: {
        RRuntimeDarwinIoBuffer view = staged_native_view(
            mode, buffer, payload.borrowed_data, payload.borrowed_length, payload.borrowed);

        native_preparation =
            r_runtime_darwin_io_prepare_read_some(handle, 0, &view, timeout_nanoseconds);
        break;
    }
    case R_LIBRARY_IO_WRITE: {
        RRuntimeDarwinIoBuffer view = staged_native_view(
            mode, buffer, payload.borrowed_data, payload.borrowed_length, payload.borrowed);

        native_preparation =
            r_runtime_darwin_io_prepare_write_some(handle, 0, &view, timeout_nanoseconds);
        break;
    }
    case R_LIBRARY_IO_WRITE_ALL: {
        RRuntimeDarwinIoBuffer view = staged_native_view(
            mode, buffer, payload.borrowed_data, payload.borrowed_length, payload.borrowed);

        native_preparation =
            r_runtime_darwin_io_prepare_write(handle, 0, &view, timeout_nanoseconds);
        break;
    }
    case R_LIBRARY_IO_WRITE_SHARED: {
        const RRuntimeArray *shared_array = r_runtime_arc_get(shared_buffer);
        const unsigned char *bytes = shared_array->data;

        native_preparation = r_runtime_darwin_io_prepare_shared_write(
            handle, 0, bytes + shared_offset, shared_length, timeout_nanoseconds);
        break;
    }
    case R_LIBRARY_IO_FLUSH:
        native_preparation = r_runtime_darwin_io_prepare_flush(handle, timeout_nanoseconds);
        break;
    }
    if (native_preparation.status == R_RUNTIME_DARWIN_IO_START_ALLOCATION_FAILED ||
        native_preparation.status == R_RUNTIME_DARWIN_IO_START_NATIVE_RETAIN_FAILED) {
        return task_start_failure(R_RUNTIME_TASK_START_ALLOCATION_FAILED);
    }
    if (native_preparation.status != R_RUNTIME_DARWIN_IO_START_OK) {
        payload.immediate = R_LIBRARY_IO_IMMEDIATE_ERROR;
        payload.immediate_error = prepare_status_error(native_preparation);
        payload.immediate_event_sequence = r_runtime_darwin_event_sequence_next();
    } else {
        payload.prepared = native_preparation.prepared;
    }
    return start_task(&payload);
}

RStdIoTaskStartResult r_library_internal_io_read(const RStdIoInput *stream,
                                                 RRuntimeArray *buffer,
                                                 RStdIoDeadline deadline) {
    return start(R_LIBRARY_IO_READ, stream->handle, buffer, NULL, NULL, 0U, 0U, deadline);
}

RStdIoTaskStartResult r_library_internal_io_write(const RStdIoOutput *stream,
                                                  RRuntimeArray *buffer,
                                                  RStdIoDeadline deadline) {
    return start(R_LIBRARY_IO_WRITE, stream->handle, buffer, NULL, NULL, 0U, 0U, deadline);
}

RStdIoTaskStartResult r_library_internal_io_write_all(const RStdIoOutput *stream,
                                                      RRuntimeArray *buffer,
                                                      RStdIoDeadline deadline) {
    return start(R_LIBRARY_IO_WRITE_ALL, stream->handle, buffer, NULL, NULL, 0U, 0U, deadline);
}

/* The runtime reads borrowed write sources through an immutable Dispatch data view. */
static uint8_t *borrowed_write_data(const uint8_t *data) {
    return (uint8_t *)(uintptr_t)data;
}

RStdIoTaskStartResult r_library_internal_io_read_into(const RStdIoInput *stream,
                                                      RStdIoMutableBytes target,
                                                      RStdIoDeadline deadline) {
    const RLibraryIoBorrowedBytes borrowed = {target.data, target.length};

    return start(R_LIBRARY_IO_READ, stream->handle, NULL, &borrowed, NULL, 0U, 0U, deadline);
}

RStdIoTaskStartResult r_library_internal_io_write_from(const RStdIoOutput *stream,
                                                       RStdIoConstBytes source,
                                                       RStdIoDeadline deadline) {
    const RLibraryIoBorrowedBytes borrowed = {borrowed_write_data(source.data), source.length};

    return start(R_LIBRARY_IO_WRITE, stream->handle, NULL, &borrowed, NULL, 0U, 0U, deadline);
}

RStdIoTaskStartResult r_library_internal_io_write_all_from(const RStdIoOutput *stream,
                                                           RStdIoConstBytes source,
                                                           RStdIoDeadline deadline) {
    const RLibraryIoBorrowedBytes borrowed = {borrowed_write_data(source.data), source.length};

    return start(R_LIBRARY_IO_WRITE_ALL, stream->handle, NULL, &borrowed, NULL, 0U, 0U, deadline);
}

RStdIoTaskStartResult r_library_internal_io_write_shared(const RStdIoOutput *stream,
                                                         RRuntimeArc *buffer,
                                                         size_t offset,
                                                         size_t length,
                                                         RStdIoDeadline deadline) {
    return start(
        R_LIBRARY_IO_WRITE_SHARED, stream->handle, NULL, NULL, buffer, offset, length, deadline);
}

RStdIoTaskStartResult r_library_internal_io_flush(const RStdIoOutput *stream,
                                                  RStdIoDeadline deadline) {
    return start(R_LIBRARY_IO_FLUSH, stream->handle, NULL, NULL, NULL, 0U, 0U, deadline);
}

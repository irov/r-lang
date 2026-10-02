#include "r_runtime_darwin_io.h"

#include "io_internal.h"
#include <dispatch/dispatch.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define R_RUNTIME_DARWIN_IO_CHUNK_SIZE ((size_t)65536U)

#if defined(R_RUNTIME_DARWIN_IO_TESTING)
static pthread_mutex_t read_progress_testing_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t read_progress_testing_condition = PTHREAD_COND_INITIALIZER;
static _Bool read_progress_testing_armed;
static _Bool read_progress_testing_reached;
static _Bool read_progress_testing_release;
static RRuntimeDarwinIoRequest *read_progress_testing_request;

void r_runtime_darwin_io_testing_pause_next_read_after_progress(void) {
    if (pthread_mutex_lock(&read_progress_testing_mutex) != 0) {
        abort();
    }
    if (read_progress_testing_armed || read_progress_testing_reached) {
        (void)pthread_mutex_unlock(&read_progress_testing_mutex);
        abort();
    }
    read_progress_testing_armed = 1;
    read_progress_testing_release = 0;
    if (pthread_mutex_unlock(&read_progress_testing_mutex) != 0) {
        abort();
    }
}

void r_runtime_darwin_io_testing_wait_for_read_after_progress(void) {
    if (pthread_mutex_lock(&read_progress_testing_mutex) != 0) {
        abort();
    }
    while (!read_progress_testing_reached) {
        if (pthread_cond_wait(&read_progress_testing_condition, &read_progress_testing_mutex) !=
            0) {
            abort();
        }
    }
    if (pthread_mutex_unlock(&read_progress_testing_mutex) != 0) {
        abort();
    }
}

static RRuntimeDarwinIoRequest *testing_paused_read(void) {
    RRuntimeDarwinIoRequest *request;

    if (pthread_mutex_lock(&read_progress_testing_mutex) != 0) {
        abort();
    }
    request = read_progress_testing_reached ? read_progress_testing_request : NULL;
    if (pthread_mutex_unlock(&read_progress_testing_mutex) != 0) {
        abort();
    }
    return request;
}

_Bool r_runtime_darwin_io_testing_cancel_read_after_progress(void) {
    return r_runtime_darwin_io_request_cancel(testing_paused_read());
}

_Bool r_runtime_darwin_io_testing_expire_read_after_progress(void) {
    return r_runtime_darwin_io_request_deadline_expired(testing_paused_read());
}

void r_runtime_darwin_io_testing_release_read_after_progress(void) {
    if (pthread_mutex_lock(&read_progress_testing_mutex) != 0) {
        abort();
    }
    if (!read_progress_testing_reached) {
        (void)pthread_mutex_unlock(&read_progress_testing_mutex);
        abort();
    }
    read_progress_testing_release = 1;
    if (pthread_cond_broadcast(&read_progress_testing_condition) != 0) {
        (void)pthread_mutex_unlock(&read_progress_testing_mutex);
        abort();
    }
    while (read_progress_testing_reached) {
        if (pthread_cond_wait(&read_progress_testing_condition, &read_progress_testing_mutex) !=
            0) {
            abort();
        }
    }
    if (pthread_mutex_unlock(&read_progress_testing_mutex) != 0) {
        abort();
    }
}

static void testing_pause_read_after_progress(RRuntimeDarwinIoRequest *request) {
    if (pthread_mutex_lock(&read_progress_testing_mutex) != 0) {
        abort();
    }
    if (!read_progress_testing_armed) {
        if (pthread_mutex_unlock(&read_progress_testing_mutex) != 0) {
            abort();
        }
        return;
    }
    read_progress_testing_armed = 0;
    read_progress_testing_reached = 1;
    read_progress_testing_request = request;
    if (pthread_cond_broadcast(&read_progress_testing_condition) != 0) {
        (void)pthread_mutex_unlock(&read_progress_testing_mutex);
        abort();
    }
    while (!read_progress_testing_release) {
        if (pthread_cond_wait(&read_progress_testing_condition, &read_progress_testing_mutex) !=
            0) {
            abort();
        }
    }
    read_progress_testing_reached = 0;
    read_progress_testing_release = 0;
    read_progress_testing_request = NULL;
    if (pthread_cond_broadcast(&read_progress_testing_condition) != 0) {
        (void)pthread_mutex_unlock(&read_progress_testing_mutex);
        abort();
    }
    if (pthread_mutex_unlock(&read_progress_testing_mutex) != 0) {
        abort();
    }
}
#else
static void testing_pause_read_after_progress(RRuntimeDarwinIoRequest *request) {
    (void)request;
}
#endif

static _Bool read_arguments_valid(RRuntimeDarwinIoHandle *handle,
                                  off_t offset,
                                  const RRuntimeDarwinIoBuffer *buffer,
                                  uint64_t timeout_nanoseconds) {
    if (buffer->capacity == 0U || buffer->size != 0U || timeout_nanoseconds > (uint64_t)INT64_MAX) {
        return 0;
    }
    return (handle->type == R_RUNTIME_DARWIN_IO_STREAM && offset == 0) ||
           (handle->type == R_RUNTIME_DARWIN_IO_RANDOM && offset >= 0);
}

static size_t
copy_read_data(RRuntimeDarwinIoRequest *request, dispatch_data_t data, _Bool *stop_after_progress) {
    dispatch_data_t mapped;
    const void *bytes = NULL;
    size_t size = 0U;
    size_t total;

    if (data == NULL || dispatch_data_get_size(data) == 0U) {
        return r_runtime_darwin_io_request_progress(request);
    }
    mapped = dispatch_data_create_map(data, &bytes, &size);
    if (mapped == NULL || bytes == NULL) {
        abort();
    }
    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (size > request->requested_size - request->bytes_transferred) {
        abort();
    }
    (void)memcpy(request->buffer.data + request->bytes_transferred, bytes, size);
    request->bytes_transferred += size;
    request->bytes_remaining = request->requested_size - request->bytes_transferred;
    request->buffer.size = request->bytes_transferred;
    total = request->bytes_transferred;
    if (request->complete_after_progress && total != 0U && !request->progress_stop_requested) {
        request->progress_stop_requested = 1;
        *stop_after_progress = 1;
    }
    (void)pthread_cond_broadcast(&request->condition);
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    dispatch_release(mapped);
    if (*stop_after_progress) {
        testing_pause_read_after_progress(request);
    }
    return total;
}

static RRuntimeDarwinIoPrepareResult prepare_read(RRuntimeDarwinIoHandle *handle,
                                                  off_t offset,
                                                  const RRuntimeDarwinIoBuffer *buffer,
                                                  uint64_t timeout_nanoseconds,
                                                  _Bool complete_after_progress) {
    RRuntimeDarwinIoRequest *request;
    RRuntimeDarwinIoStartStatus status;
    dispatch_io_t channel;
    int native_error;

    if (pthread_mutex_lock(&handle->mutex) != 0) {
        return r_runtime_darwin_io_internal_prepare_failure(
            R_RUNTIME_DARWIN_IO_START_INVALID_ARGUMENT, 0);
    }
    if (!read_arguments_valid(handle, offset, buffer, timeout_nanoseconds)) {
        (void)pthread_mutex_unlock(&handle->mutex);
        return r_runtime_darwin_io_internal_prepare_failure(
            R_RUNTIME_DARWIN_IO_START_INVALID_ARGUMENT, 0);
    }
    if (handle->closed) {
        (void)pthread_mutex_unlock(&handle->mutex);
        return r_runtime_darwin_io_internal_prepare_failure(R_RUNTIME_DARWIN_IO_START_HANDLE_CLOSED,
                                                            0);
    }
    if (r_runtime_darwin_io_internal_testing_should_fail_prepare(
            R_RUNTIME_DARWIN_IO_PREPARE_FAIL_REQUEST)) {
        (void)pthread_mutex_unlock(&handle->mutex);
        return r_runtime_darwin_io_internal_prepare_failure(
            R_RUNTIME_DARWIN_IO_START_ALLOCATION_FAILED, ENOMEM);
    }
    request = r_runtime_darwin_io_internal_request_create_locked(
        handle, R_RUNTIME_DARWIN_IO_READ, NULL, &status, &native_error);
    if (request == NULL) {
        (void)pthread_mutex_unlock(&handle->mutex);
        return r_runtime_darwin_io_internal_prepare_failure(status, native_error);
    }
    request->state = R_RUNTIME_DARWIN_IO_REQUEST_PREPARED;
    request->requested_size = buffer->capacity;
    request->bytes_remaining = request->requested_size;
    request->complete_after_progress = complete_after_progress;
    request->buffer_transfer_required = 1;
    request->prepared_buffer_data = buffer->data;
    request->prepared_buffer_allocator = buffer->allocator;
    request->prepared_buffer_capacity = buffer->capacity;
    request->prepared_buffer_size = buffer->size;
    request->prepared_offset = offset;
    if ((timeout_nanoseconds != 0U && r_runtime_darwin_io_internal_testing_should_fail_prepare(
                                          R_RUNTIME_DARWIN_IO_PREPARE_FAIL_DEADLINE)) ||
        !r_runtime_darwin_io_internal_prepare_deadline(request, timeout_nanoseconds)) {
        (void)pthread_mutex_unlock(&handle->mutex);
        r_runtime_darwin_io_prepared_abort(&request);
        return r_runtime_darwin_io_internal_prepare_failure(
            R_RUNTIME_DARWIN_IO_START_NATIVE_RETAIN_FAILED, ENOMEM);
    }
    if (r_runtime_darwin_io_internal_testing_should_fail_prepare(
            R_RUNTIME_DARWIN_IO_PREPARE_FAIL_CHANNEL)) {
        channel = NULL;
    } else {
        channel = r_runtime_darwin_io_internal_create_operation_channel(request);
    }
    if (channel == NULL) {
        (void)pthread_mutex_unlock(&handle->mutex);
        r_runtime_darwin_io_prepared_abort(&request);
        return r_runtime_darwin_io_internal_prepare_failure(
            R_RUNTIME_DARWIN_IO_START_NATIVE_RETAIN_FAILED, ENOMEM);
    }
    dispatch_io_set_low_water(channel, 1U);
    dispatch_io_set_high_water(channel, R_RUNTIME_DARWIN_IO_CHUNK_SIZE);
    if (pthread_mutex_unlock(&handle->mutex) != 0) {
        abort();
    }
    return r_runtime_darwin_io_internal_prepare_success(request);
}

static void submit_read_locked(RRuntimeDarwinIoRequest *request) {
    RRuntimeDarwinIoHandle *handle = request->handle;
    dispatch_io_t channel = request->operation_channel;

    if (channel == NULL) {
        abort();
    }
    dispatch_io_read(channel,
                     request->prepared_offset,
                     request->requested_size,
                     handle->callback_queue,
                     ^(bool done, dispatch_data_t data, int error) {
                       _Bool stop_after_progress = 0;
                       size_t transferred = copy_read_data(request, data, &stop_after_progress);

                       if (!done && stop_after_progress) {
                           r_runtime_darwin_io_internal_close_operation_channel(request,
                                                                                DISPATCH_IO_STOP);
                       }

                       if (done) {
                           size_t remaining = request->requested_size - transferred;
                           _Bool eof;

                           if (request->progress_stop_requested && error == ECANCELED) {
                               error = 0;
                           }
                           eof = error == 0 && !request->progress_stop_requested &&
                                 transferred < request->requested_size;

                           r_runtime_darwin_io_internal_operation_done(
                               request, transferred, remaining, error, eof);
                       }
                     });
}

_Bool r_runtime_darwin_io_internal_activate_read(RRuntimeDarwinIoRequest *request) {
    _Bool positioned;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (request->state != R_RUNTIME_DARWIN_IO_REQUEST_ACTIVE || request->native_scheduled) {
        if (pthread_mutex_unlock(&request->mutex) != 0) {
            abort();
        }
        return 0;
    }
    if (request->operation_channel == NULL || request->references == SIZE_MAX) {
        (void)pthread_mutex_unlock(&request->mutex);
        abort();
    }
    request->native_scheduled = 1;
    /* This native retain keeps both request and handle alive through barrier and payload. */
    request->references += 1U;
    positioned = request->stream_position_enabled;
    if (positioned) {
        request->stream_position_barrier_pending = 1;
        r_runtime_darwin_io_internal_schedule_stream_position_barrier(request);
    } else {
        submit_read_locked(request);
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    return 1;
}

void r_runtime_darwin_io_internal_read_stream_position_barrier_done(
    RRuntimeDarwinIoRequest *request, int native_error) {
    size_t transferred;
    size_t remaining;
    _Bool submit_payload;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (request->operation != R_RUNTIME_DARWIN_IO_READ ||
        request->state != R_RUNTIME_DARWIN_IO_REQUEST_ACTIVE || !request->native_scheduled ||
        !request->stream_position_barrier_pending || request->operation_done) {
        (void)pthread_mutex_unlock(&request->mutex);
        abort();
    }
    request->stream_position_barrier_pending = 0;
    submit_payload = native_error == 0 && request->pending_event == 0;
    transferred = request->bytes_transferred;
    remaining = request->requested_size - transferred;
    if (submit_payload) {
        if (request->operation_channel == NULL) {
            (void)pthread_mutex_unlock(&request->mutex);
            abort();
        }
        submit_read_locked(request);
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    if (!submit_payload) {
        r_runtime_darwin_io_internal_operation_done(
            request, transferred, remaining, native_error, 0);
    }
}

RRuntimeDarwinIoPrepareResult r_runtime_darwin_io_prepare_read(RRuntimeDarwinIoHandle *handle,
                                                               off_t offset,
                                                               const RRuntimeDarwinIoBuffer *buffer,
                                                               uint64_t timeout_nanoseconds) {
    return prepare_read(handle, offset, buffer, timeout_nanoseconds, 0);
}

RRuntimeDarwinIoPrepareResult
r_runtime_darwin_io_prepare_read_some(RRuntimeDarwinIoHandle *handle,
                                      off_t offset,
                                      const RRuntimeDarwinIoBuffer *buffer,
                                      uint64_t timeout_nanoseconds) {
    return prepare_read(handle, offset, buffer, timeout_nanoseconds, 1);
}

static RRuntimeDarwinIoSubmitResult submit_read(RRuntimeDarwinIoHandle *handle,
                                                off_t offset,
                                                RRuntimeDarwinIoBuffer *buffer,
                                                uint64_t timeout_nanoseconds,
                                                _Bool complete_after_progress) {
    RRuntimeDarwinIoPrepareResult preparation =
        prepare_read(handle, offset, buffer, timeout_nanoseconds, complete_after_progress);
    RRuntimeDarwinIoSubmitResult submission;

    if (preparation.status != R_RUNTIME_DARWIN_IO_START_OK) {
        return r_runtime_darwin_io_internal_submit_failure(preparation.status,
                                                           preparation.native_error);
    }
    submission = r_runtime_darwin_io_prepared_activate(&preparation.prepared, buffer);
    if (submission.status != R_RUNTIME_DARWIN_IO_START_OK) {
        r_runtime_darwin_io_prepared_abort(&preparation.prepared);
    }
    return submission;
}

RRuntimeDarwinIoSubmitResult r_runtime_darwin_io_submit_read(RRuntimeDarwinIoHandle *handle,
                                                             off_t offset,
                                                             RRuntimeDarwinIoBuffer *buffer,
                                                             uint64_t timeout_nanoseconds) {
    return submit_read(handle, offset, buffer, timeout_nanoseconds, 0);
}

RRuntimeDarwinIoSubmitResult r_runtime_darwin_io_submit_read_some(RRuntimeDarwinIoHandle *handle,
                                                                  off_t offset,
                                                                  RRuntimeDarwinIoBuffer *buffer,
                                                                  uint64_t timeout_nanoseconds) {
    return submit_read(handle, offset, buffer, timeout_nanoseconds, 1);
}

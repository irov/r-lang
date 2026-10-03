#include "r_runtime_darwin_io.h"

#include "io_internal.h"
#include <dispatch/dispatch.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>

#define R_RUNTIME_DARWIN_IO_CHUNK_SIZE ((size_t)65536U)

#if defined(R_RUNTIME_DARWIN_IO_TESTING)
static pthread_mutex_t write_progress_testing_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t write_progress_testing_condition = PTHREAD_COND_INITIALIZER;
static _Bool write_progress_testing_armed;
static _Bool write_progress_testing_reached;
static _Bool write_progress_testing_release;
static RRuntimeDarwinIoRequest *write_progress_testing_request;
/* Unarmed hooks stay off the hot path: the hint is read without the mutex, and the mutex
   state stays authoritative once it is set. */
static _Atomic _Bool write_progress_testing_hint;

void r_runtime_darwin_io_testing_pause_next_write_after_progress(void) {
    if (pthread_mutex_lock(&write_progress_testing_mutex) != 0) {
        abort();
    }
    if (write_progress_testing_armed || write_progress_testing_reached) {
        (void)pthread_mutex_unlock(&write_progress_testing_mutex);
        abort();
    }
    write_progress_testing_armed = 1;
    atomic_store_explicit(&write_progress_testing_hint, 1, memory_order_release);
    write_progress_testing_release = 0;
    if (pthread_mutex_unlock(&write_progress_testing_mutex) != 0) {
        abort();
    }
}

void r_runtime_darwin_io_testing_wait_for_write_after_progress(void) {
    if (pthread_mutex_lock(&write_progress_testing_mutex) != 0) {
        abort();
    }
    while (!write_progress_testing_reached) {
        if (pthread_cond_wait(&write_progress_testing_condition, &write_progress_testing_mutex) !=
            0) {
            abort();
        }
    }
    if (pthread_mutex_unlock(&write_progress_testing_mutex) != 0) {
        abort();
    }
}

static RRuntimeDarwinIoRequest *testing_paused_write(void) {
    RRuntimeDarwinIoRequest *request;

    if (pthread_mutex_lock(&write_progress_testing_mutex) != 0) {
        abort();
    }
    request = write_progress_testing_reached ? write_progress_testing_request : NULL;
    if (pthread_mutex_unlock(&write_progress_testing_mutex) != 0) {
        abort();
    }
    return request;
}

_Bool r_runtime_darwin_io_testing_cancel_write_after_progress(void) {
    return r_runtime_darwin_io_request_cancel(testing_paused_write());
}

_Bool r_runtime_darwin_io_testing_expire_write_after_progress(void) {
    return r_runtime_darwin_io_request_deadline_expired(testing_paused_write());
}

void r_runtime_darwin_io_testing_release_write_after_progress(void) {
    if (pthread_mutex_lock(&write_progress_testing_mutex) != 0) {
        abort();
    }
    if (!write_progress_testing_reached) {
        (void)pthread_mutex_unlock(&write_progress_testing_mutex);
        abort();
    }
    write_progress_testing_release = 1;
    if (pthread_cond_broadcast(&write_progress_testing_condition) != 0) {
        (void)pthread_mutex_unlock(&write_progress_testing_mutex);
        abort();
    }
    while (write_progress_testing_reached) {
        if (pthread_cond_wait(&write_progress_testing_condition, &write_progress_testing_mutex) !=
            0) {
            abort();
        }
    }
    if (pthread_mutex_unlock(&write_progress_testing_mutex) != 0) {
        abort();
    }
}

static void testing_pause_write_after_progress(RRuntimeDarwinIoRequest *request) {
    if (!atomic_load_explicit(&write_progress_testing_hint, memory_order_acquire)) {
        return;
    }
    if (pthread_mutex_lock(&write_progress_testing_mutex) != 0) {
        abort();
    }
    if (!write_progress_testing_armed) {
        if (pthread_mutex_unlock(&write_progress_testing_mutex) != 0) {
            abort();
        }
        return;
    }
    write_progress_testing_armed = 0;
    atomic_store_explicit(&write_progress_testing_hint, 0, memory_order_release);
    write_progress_testing_reached = 1;
    write_progress_testing_request = request;
    if (pthread_cond_broadcast(&write_progress_testing_condition) != 0) {
        (void)pthread_mutex_unlock(&write_progress_testing_mutex);
        abort();
    }
    while (!write_progress_testing_release) {
        if (pthread_cond_wait(&write_progress_testing_condition, &write_progress_testing_mutex) !=
            0) {
            abort();
        }
    }
    write_progress_testing_reached = 0;
    write_progress_testing_release = 0;
    write_progress_testing_request = NULL;
    if (pthread_cond_broadcast(&write_progress_testing_condition) != 0) {
        (void)pthread_mutex_unlock(&write_progress_testing_mutex);
        abort();
    }
    if (pthread_mutex_unlock(&write_progress_testing_mutex) != 0) {
        abort();
    }
}

_Bool r_runtime_darwin_io_internal_testing_write_pause_armed(void) {
    return atomic_load_explicit(&write_progress_testing_hint, memory_order_acquire);
}
#else
static void testing_pause_write_after_progress(RRuntimeDarwinIoRequest *request) {
    (void)request;
}

_Bool r_runtime_darwin_io_internal_testing_write_pause_armed(void) {
    return 0;
}
#endif

void r_runtime_darwin_io_internal_testing_pause_write_after_progress(
    RRuntimeDarwinIoRequest *request) {
    testing_pause_write_after_progress(request);
}

static _Bool write_arguments_valid(RRuntimeDarwinIoHandle *handle,
                                   off_t offset,
                                   const RRuntimeDarwinIoBuffer *buffer,
                                   uint64_t timeout_nanoseconds) {
    if (buffer->size == 0U || buffer->size > buffer->capacity ||
        timeout_nanoseconds > (uint64_t)INT64_MAX) {
        return 0;
    }
    return (handle->type == R_RUNTIME_DARWIN_IO_STREAM && offset == 0) ||
           (handle->type == R_RUNTIME_DARWIN_IO_RANDOM && offset >= 0);
}

static RRuntimeDarwinIoPrepareResult prepare_write(RRuntimeDarwinIoHandle *handle,
                                                   off_t offset,
                                                   const unsigned char *bytes,
                                                   size_t size,
                                                   RRuntimeAllocator *buffer_allocator,
                                                   size_t buffer_capacity,
                                                   _Bool transfer_buffer,
                                                   uint64_t timeout_nanoseconds,
                                                   _Bool complete_after_progress) {
    RRuntimeDarwinIoRequest *request;
    RRuntimeDarwinIoStartStatus status;
    dispatch_data_t write_data;
    dispatch_io_t channel;
    int native_error;

    if (pthread_mutex_lock(&handle->mutex) != 0) {
        return r_runtime_darwin_io_internal_prepare_failure(
            R_RUNTIME_DARWIN_IO_START_INVALID_ARGUMENT, 0);
    }
    if (size == 0U || size > buffer_capacity || timeout_nanoseconds > (uint64_t)INT64_MAX ||
        !((handle->type == R_RUNTIME_DARWIN_IO_STREAM && offset == 0) ||
          (handle->type == R_RUNTIME_DARWIN_IO_RANDOM && offset >= 0))) {
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
        handle, R_RUNTIME_DARWIN_IO_WRITE, NULL, &status, &native_error);
    if (request == NULL) {
        (void)pthread_mutex_unlock(&handle->mutex);
        return r_runtime_darwin_io_internal_prepare_failure(status, native_error);
    }
    request->state = R_RUNTIME_DARWIN_IO_REQUEST_PREPARED;
    request->requested_size = size;
    request->bytes_remaining = request->requested_size;
    request->complete_after_progress = complete_after_progress;
    request->buffer_transfer_required = transfer_buffer;
    request->prepared_buffer_data = bytes;
    request->prepared_buffer_allocator = buffer_allocator;
    request->prepared_buffer_capacity = buffer_capacity;
    request->prepared_buffer_size = size;
    request->prepared_offset = offset;
    if ((timeout_nanoseconds != 0U && r_runtime_darwin_io_internal_testing_should_fail_prepare(
                                          R_RUNTIME_DARWIN_IO_PREPARE_FAIL_DEADLINE)) ||
        !r_runtime_darwin_io_internal_prepare_deadline(request, timeout_nanoseconds)) {
        (void)pthread_mutex_unlock(&handle->mutex);
        r_runtime_darwin_io_prepared_abort(&request);
        return r_runtime_darwin_io_internal_prepare_failure(
            R_RUNTIME_DARWIN_IO_START_NATIVE_RETAIN_FAILED, ENOMEM);
    }
    /* The channel and write-data failure stages also cover the native reservation of a direct
       engine, which needs neither object: it writes from the request buffer itself. */
    if (r_runtime_darwin_io_internal_testing_should_fail_prepare(
            R_RUNTIME_DARWIN_IO_PREPARE_FAIL_CHANNEL)) {
        channel = NULL;
    } else if (handle->engine != R_RUNTIME_DARWIN_IO_ENGINE_DISPATCH) {
        channel = NULL;
        if (!r_runtime_darwin_io_internal_testing_should_fail_prepare(
                R_RUNTIME_DARWIN_IO_PREPARE_FAIL_WRITE_DATA)) {
            if (pthread_mutex_unlock(&handle->mutex) != 0) {
                abort();
            }
            return r_runtime_darwin_io_internal_prepare_success(request);
        }
    } else {
        channel = r_runtime_darwin_io_internal_create_operation_channel(request);
    }
    if (channel == NULL) {
        (void)pthread_mutex_unlock(&handle->mutex);
        r_runtime_darwin_io_prepared_abort(&request);
        return r_runtime_darwin_io_internal_prepare_failure(
            R_RUNTIME_DARWIN_IO_START_NATIVE_RETAIN_FAILED, ENOMEM);
    }
    if (r_runtime_darwin_io_internal_testing_should_fail_prepare(
            R_RUNTIME_DARWIN_IO_PREPARE_FAIL_WRITE_DATA)) {
        write_data = NULL;
    } else {
        write_data = dispatch_data_create(bytes,
                                          request->requested_size,
                                          handle->callback_queue,
                                          ^{
                                          });
    }
    if (write_data == NULL) {
        (void)pthread_mutex_unlock(&handle->mutex);
        r_runtime_darwin_io_prepared_abort(&request);
        return r_runtime_darwin_io_internal_prepare_failure(
            R_RUNTIME_DARWIN_IO_START_NATIVE_RETAIN_FAILED, ENOMEM);
    }
    request->prepared_write_data = write_data;
    dispatch_io_set_low_water(channel, 1U);
    dispatch_io_set_high_water(channel, R_RUNTIME_DARWIN_IO_CHUNK_SIZE);
    if (pthread_mutex_unlock(&handle->mutex) != 0) {
        abort();
    }
    return r_runtime_darwin_io_internal_prepare_success(request);
}

static dispatch_data_t submit_write_locked(RRuntimeDarwinIoRequest *request) {
    RRuntimeDarwinIoHandle *handle = request->handle;
    dispatch_data_t data = request->prepared_write_data;
    dispatch_io_t channel = request->operation_channel;

    if (channel == NULL || data == NULL) {
        abort();
    }
    request->prepared_write_data = NULL;
    dispatch_io_write(
        channel,
        request->prepared_offset,
        data,
        handle->callback_queue,
        ^(bool done, dispatch_data_t remaining_data, int error) {
          size_t remaining = remaining_data == NULL ? 0U : dispatch_data_get_size(remaining_data);
          size_t transferred;

          if (remaining > request->requested_size) {
              abort();
          }
          transferred = request->requested_size - remaining;
          if (!done && request->complete_after_progress && transferred != 0U) {
              if (pthread_mutex_lock(&request->mutex) != 0) {
                  abort();
              }
              if (!request->progress_stop_requested) {
                  request->progress_stop_requested = 1;
              }
              if (pthread_mutex_unlock(&request->mutex) != 0) {
                  abort();
              }
              r_runtime_darwin_io_internal_close_operation_channel(request, DISPATCH_IO_STOP);
          }
          if (done) {
              if (request->progress_stop_requested && error == ECANCELED) {
                  error = 0;
              }
              r_runtime_darwin_io_internal_operation_done(
                  request, transferred, remaining, error, 0);
          } else {
              r_runtime_darwin_io_internal_progress(request, transferred, remaining);
              if (transferred != 0U) {
                  testing_pause_write_after_progress(request);
              }
          }
        });
    return data;
}

_Bool r_runtime_darwin_io_internal_activate_write(RRuntimeDarwinIoRequest *request) {
    dispatch_data_t data = NULL;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (request->state != R_RUNTIME_DARWIN_IO_REQUEST_ACTIVE || request->native_scheduled) {
        if (pthread_mutex_unlock(&request->mutex) != 0) {
            abort();
        }
        return 0;
    }
    if (request->operation_channel == NULL || request->prepared_write_data == NULL ||
        request->references == SIZE_MAX) {
        (void)pthread_mutex_unlock(&request->mutex);
        abort();
    }
    request->native_scheduled = 1;
    /* This native retain keeps both request and handle alive through barrier and payload. */
    request->references += 1U;
    if (request->stream_position_enabled) {
        request->stream_position_barrier_pending = 1;
        r_runtime_darwin_io_internal_schedule_stream_position_barrier(request);
    } else {
        data = submit_write_locked(request);
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    if (data != NULL) {
        dispatch_release(data);
    }
    return 1;
}

void r_runtime_darwin_io_internal_write_stream_position_barrier_done(
    RRuntimeDarwinIoRequest *request, int native_error) {
    dispatch_data_t data = NULL;
    size_t transferred;
    size_t remaining;
    _Bool submit_payload;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (request->operation != R_RUNTIME_DARWIN_IO_WRITE ||
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
        data = submit_write_locked(request);
    } else {
        data = request->prepared_write_data;
        request->prepared_write_data = NULL;
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    if (data != NULL) {
        dispatch_release(data);
    }
    if (!submit_payload) {
        r_runtime_darwin_io_internal_operation_done(
            request, transferred, remaining, native_error, 0);
    }
}

RRuntimeDarwinIoPrepareResult
r_runtime_darwin_io_prepare_write(RRuntimeDarwinIoHandle *handle,
                                  off_t offset,
                                  const RRuntimeDarwinIoBuffer *buffer,
                                  uint64_t timeout_nanoseconds) {
    if (!write_arguments_valid(handle, offset, buffer, timeout_nanoseconds)) {
        return r_runtime_darwin_io_internal_prepare_failure(
            R_RUNTIME_DARWIN_IO_START_INVALID_ARGUMENT, 0);
    }
    return prepare_write(handle,
                         offset,
                         buffer->data,
                         buffer->size,
                         buffer->allocator,
                         buffer->capacity,
                         1,
                         timeout_nanoseconds,
                         0);
}

RRuntimeDarwinIoPrepareResult
r_runtime_darwin_io_prepare_write_some(RRuntimeDarwinIoHandle *handle,
                                       off_t offset,
                                       const RRuntimeDarwinIoBuffer *buffer,
                                       uint64_t timeout_nanoseconds) {
    if (!write_arguments_valid(handle, offset, buffer, timeout_nanoseconds)) {
        return r_runtime_darwin_io_internal_prepare_failure(
            R_RUNTIME_DARWIN_IO_START_INVALID_ARGUMENT, 0);
    }
    return prepare_write(handle,
                         offset,
                         buffer->data,
                         buffer->size,
                         buffer->allocator,
                         buffer->capacity,
                         1,
                         timeout_nanoseconds,
                         1);
}

RRuntimeDarwinIoPrepareResult
r_runtime_darwin_io_prepare_shared_write(RRuntimeDarwinIoHandle *handle,
                                         off_t offset,
                                         const unsigned char *data,
                                         size_t size,
                                         uint64_t timeout_nanoseconds) {
    return prepare_write(handle, offset, data, size, NULL, size, 0, timeout_nanoseconds, 0);
}

RRuntimeDarwinIoPrepareResult
r_runtime_darwin_io_prepare_borrowed_random_shared_write(RRuntimeAllocator *allocator,
                                                         off_t offset,
                                                         const unsigned char *data,
                                                         size_t size,
                                                         uint64_t timeout_nanoseconds) {
    RRuntimeDarwinIoHandleCreateResult handle_created;
    RRuntimeDarwinIoHandle *handle;
    RRuntimeDarwinIoRequest *request;
    RRuntimeDarwinIoStartStatus status;
    int native_error;

    if (offset < 0 || size == 0U || timeout_nanoseconds > (uint64_t)INT64_MAX) {
        return r_runtime_darwin_io_internal_prepare_failure(
            R_RUNTIME_DARWIN_IO_START_INVALID_ARGUMENT, 0);
    }
    handle_created = r_runtime_darwin_io_internal_handle_prepare_borrowed_random(allocator);
    if (handle_created.status != R_RUNTIME_DARWIN_IO_START_OK || handle_created.handle == NULL) {
        return r_runtime_darwin_io_internal_prepare_failure(handle_created.status,
                                                            handle_created.native_error);
    }
    handle = handle_created.handle;
    if (pthread_mutex_lock(&handle->mutex) != 0) {
        abort();
    }
    if (r_runtime_darwin_io_internal_testing_should_fail_prepare(
            R_RUNTIME_DARWIN_IO_PREPARE_FAIL_REQUEST)) {
        request = NULL;
        status = R_RUNTIME_DARWIN_IO_START_ALLOCATION_FAILED;
        native_error = ENOMEM;
    } else {
        request = r_runtime_darwin_io_internal_request_create_locked(
            handle, R_RUNTIME_DARWIN_IO_WRITE, NULL, &status, &native_error);
    }
    if (request != NULL) {
        request->state = R_RUNTIME_DARWIN_IO_REQUEST_PREPARED;
        request->requested_size = size;
        request->bytes_remaining = size;
        request->prepared_buffer_data = data;
        request->prepared_buffer_capacity = size;
        request->prepared_buffer_size = size;
        request->prepared_offset = offset;
    }
    if (pthread_mutex_unlock(&handle->mutex) != 0) {
        abort();
    }
    if (request == NULL) {
        r_runtime_darwin_io_internal_handle_release_borrowed_root(handle);
        return r_runtime_darwin_io_internal_prepare_failure(status, native_error);
    }
    if ((timeout_nanoseconds != 0U && r_runtime_darwin_io_internal_testing_should_fail_prepare(
                                          R_RUNTIME_DARWIN_IO_PREPARE_FAIL_DEADLINE)) ||
        !r_runtime_darwin_io_internal_prepare_deadline(request, timeout_nanoseconds)) {
        r_runtime_darwin_io_prepared_abort(&request);
        return r_runtime_darwin_io_internal_prepare_failure(
            R_RUNTIME_DARWIN_IO_START_NATIVE_RETAIN_FAILED, ENOMEM);
    }
    /* The file payload adapter writes straight from the borrowed bytes; the write-data stage
       keeps its failure injection as the reservation of that borrow. */
    if (r_runtime_darwin_io_internal_testing_should_fail_prepare(
            R_RUNTIME_DARWIN_IO_PREPARE_FAIL_WRITE_DATA)) {
        r_runtime_darwin_io_prepared_abort(&request);
        return r_runtime_darwin_io_internal_prepare_failure(
            R_RUNTIME_DARWIN_IO_START_NATIVE_RETAIN_FAILED, ENOMEM);
    }
    return r_runtime_darwin_io_internal_prepare_success(request);
}

RRuntimeDarwinIoSubmitResult
r_runtime_darwin_io_prepared_bind_activate_borrowed_random_shared_write(
    RRuntimeDarwinIoPreparedRequest **prepared_slot,
    int descriptor,
    RRuntimeDarwinIoHandleCleanupFn cleanup,
    void *context) {
    RRuntimeDarwinIoRequest *request;
    RRuntimeDarwinIoHandle *handle;
    RRuntimeDarwinIoSubmitResult submission;
    RRuntimeDarwinIoStartStatus status;
    int native_error = 0;
    if (descriptor < 0) {
        return r_runtime_darwin_io_internal_submit_failure(
            R_RUNTIME_DARWIN_IO_START_INVALID_ARGUMENT, 0);
    }
    request = *prepared_slot;
    handle = request->handle;
    status = r_runtime_darwin_io_internal_handle_bind_borrowed_random(
        handle, descriptor, cleanup, context, &native_error);
    if (status != R_RUNTIME_DARWIN_IO_START_OK) {
        return r_runtime_darwin_io_internal_submit_failure(status, native_error);
    }
    /* The channel failure stage also covers the native reservation of the file payload
       adapter, which needs no further object. */
    if (r_runtime_darwin_io_internal_testing_should_fail_prepare(
            R_RUNTIME_DARWIN_IO_PREPARE_FAIL_CHANNEL)) {
        r_runtime_darwin_io_prepared_abort(prepared_slot);
        return r_runtime_darwin_io_internal_submit_failure(
            R_RUNTIME_DARWIN_IO_START_NATIVE_RETAIN_FAILED, ENOMEM);
    }
    submission = r_runtime_darwin_io_prepared_activate(prepared_slot, NULL);
    if (submission.status != R_RUNTIME_DARWIN_IO_START_OK) {
        r_runtime_darwin_io_prepared_abort(prepared_slot);
        return submission;
    }
    if (submission.request != request) {
        abort();
    }
    return submission;
}

static RRuntimeDarwinIoSubmitResult submit_write(RRuntimeDarwinIoHandle *handle,
                                                 off_t offset,
                                                 RRuntimeDarwinIoBuffer *buffer,
                                                 uint64_t timeout_nanoseconds,
                                                 _Bool complete_after_progress) {
    RRuntimeDarwinIoPrepareResult preparation =
        complete_after_progress
            ? r_runtime_darwin_io_prepare_write_some(handle, offset, buffer, timeout_nanoseconds)
            : r_runtime_darwin_io_prepare_write(handle, offset, buffer, timeout_nanoseconds);
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

RRuntimeDarwinIoSubmitResult r_runtime_darwin_io_submit_write(RRuntimeDarwinIoHandle *handle,
                                                              off_t offset,
                                                              RRuntimeDarwinIoBuffer *buffer,
                                                              uint64_t timeout_nanoseconds) {
    return submit_write(handle, offset, buffer, timeout_nanoseconds, 0);
}

RRuntimeDarwinIoSubmitResult r_runtime_darwin_io_submit_write_some(RRuntimeDarwinIoHandle *handle,
                                                                   off_t offset,
                                                                   RRuntimeDarwinIoBuffer *buffer,
                                                                   uint64_t timeout_nanoseconds) {
    return submit_write(handle, offset, buffer, timeout_nanoseconds, 1);
}

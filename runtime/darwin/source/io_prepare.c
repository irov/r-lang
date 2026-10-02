#include "r_runtime_darwin_io.h"

#include "io_internal.h"
#include "r_runtime_darwin_event.h"

#include <dispatch/dispatch.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#if defined(R_RUNTIME_DARWIN_IO_TESTING)
static _Atomic int r_runtime_darwin_io_prepare_failure_stage;
#endif

RRuntimeDarwinIoPrepareResult
r_runtime_darwin_io_internal_prepare_failure(RRuntimeDarwinIoStartStatus status, int native_error) {
    RRuntimeDarwinIoPrepareResult result;

    result.prepared = NULL;
    result.status = status;
    result.native_error = native_error;
    return result;
}

RRuntimeDarwinIoPrepareResult
r_runtime_darwin_io_internal_prepare_success(RRuntimeDarwinIoPreparedRequest *prepared) {
    RRuntimeDarwinIoPrepareResult result;

    result.prepared = prepared;
    result.status = R_RUNTIME_DARWIN_IO_START_OK;
    result.native_error = 0;
    return result;
}

_Bool r_runtime_darwin_io_internal_testing_should_fail_prepare(
    RRuntimeDarwinIoPrepareFailureStage stage) {
#if defined(R_RUNTIME_DARWIN_IO_TESTING)
    int expected = (int)stage;

    return atomic_compare_exchange_strong_explicit(&r_runtime_darwin_io_prepare_failure_stage,
                                                   &expected,
                                                   (int)R_RUNTIME_DARWIN_IO_PREPARE_FAIL_NONE,
                                                   memory_order_relaxed,
                                                   memory_order_relaxed);
#else
    (void)stage;
    return 0;
#endif
}

#if defined(R_RUNTIME_DARWIN_IO_TESTING)
void r_runtime_darwin_io_testing_fail_prepare_stage(RRuntimeDarwinIoPrepareFailureStage stage) {
    atomic_store_explicit(
        &r_runtime_darwin_io_prepare_failure_stage, (int)stage, memory_order_relaxed);
}
#endif

RRuntimeDarwinIoSubmitResult
r_runtime_darwin_io_prepared_activate(RRuntimeDarwinIoPreparedRequest **prepared_slot,
                                      RRuntimeDarwinIoBuffer *buffer) {
    RRuntimeDarwinIoRequest *request;
    RRuntimeDarwinIoSubmitResult result;
    RRuntimeDarwinIoHandle *handle;
    _Bool expects_buffer;
    _Bool cancelled_before_submission;

    request = *prepared_slot;
    handle = request->handle;
    expects_buffer = request->buffer_transfer_required;
    if (pthread_mutex_lock(&handle->mutex) != 0) {
        return r_runtime_darwin_io_internal_submit_failure(
            R_RUNTIME_DARWIN_IO_START_INVALID_ARGUMENT, 0);
    }
    if (pthread_mutex_lock(&request->mutex) != 0) {
        (void)pthread_mutex_unlock(&handle->mutex);
        return r_runtime_darwin_io_internal_submit_failure(
            R_RUNTIME_DARWIN_IO_START_INVALID_ARGUMENT, 0);
    }
    if (expects_buffer) {
        request->buffer = *buffer;
        (void)memset(buffer, 0, sizeof(*buffer));
    }
    if (handle->closed && request->pending_event == 0) {
        request->pending_event = R_RUNTIME_DARWIN_IO_TERMINAL_CANCELLED;
        request->pending_event_sequence = r_runtime_darwin_event_sequence_next();
    }
    request->state = R_RUNTIME_DARWIN_IO_REQUEST_ACTIVE;
    cancelled_before_submission = request->pending_event != 0;
    *prepared_slot = NULL;
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        (void)pthread_mutex_unlock(&handle->mutex);
        abort();
    }
    if (pthread_mutex_unlock(&handle->mutex) != 0) {
        abort();
    }

    if (cancelled_before_submission) {
        r_runtime_darwin_io_internal_request_complete_without_submission(request);
    } else {
        r_runtime_darwin_io_internal_activate_deadline(request);
        r_runtime_darwin_io_internal_schedule_ready_requests(handle);
    }
    result = r_runtime_darwin_io_internal_submit_success(request);
    return result;
}

_Bool r_runtime_darwin_io_prepared_set_offset(RRuntimeDarwinIoPreparedRequest *prepared,
                                              off_t relative_offset) {
    _Bool changed = 0;

    if (relative_offset < 0 || pthread_mutex_lock(&prepared->mutex) != 0) {
        return 0;
    }
    if ((prepared->operation == R_RUNTIME_DARWIN_IO_READ ||
         prepared->operation == R_RUNTIME_DARWIN_IO_WRITE) &&
        prepared->handle->type == R_RUNTIME_DARWIN_IO_RANDOM &&
        prepared->state == R_RUNTIME_DARWIN_IO_REQUEST_PREPARED) {
        prepared->prepared_offset = relative_offset;
        changed = 1;
    }
    if (pthread_mutex_unlock(&prepared->mutex) != 0) {
        abort();
    }
    return changed;
}

_Bool r_runtime_darwin_io_prepared_set_stream_position(RRuntimeDarwinIoPreparedRequest *prepared,
                                                       off_t absolute_position) {
    _Bool changed = 0;

    if (absolute_position < 0 || pthread_mutex_lock(&prepared->mutex) != 0) {
        return 0;
    }
    if ((prepared->operation == R_RUNTIME_DARWIN_IO_READ ||
         prepared->operation == R_RUNTIME_DARWIN_IO_WRITE) &&
        prepared->handle->type == R_RUNTIME_DARWIN_IO_STREAM &&
        prepared->state == R_RUNTIME_DARWIN_IO_REQUEST_PREPARED) {
        prepared->prepared_stream_position = absolute_position;
        prepared->stream_position_enabled = 1;
        changed = 1;
    }
    if (pthread_mutex_unlock(&prepared->mutex) != 0) {
        abort();
    }
    return changed;
}

void r_runtime_darwin_io_prepared_abort(RRuntimeDarwinIoPreparedRequest **prepared_slot) {
    RRuntimeDarwinIoRequest *request;
    RRuntimeDarwinIoHandle *handle;
    dispatch_data_t write_data = NULL;

    if (prepared_slot == NULL || *prepared_slot == NULL) {
        return;
    }
    request = *prepared_slot;
    handle = request->handle;
    *prepared_slot = NULL;
    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (request->state != R_RUNTIME_DARWIN_IO_REQUEST_PREPARED) {
        (void)pthread_mutex_unlock(&request->mutex);
        abort();
    }
    write_data = request->prepared_write_data;
    request->prepared_write_data = NULL;
    request->operation_done = 1;
    request->cleanup_done = 1;
    request->terminal_event = R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE;
    request->state = R_RUNTIME_DARWIN_IO_REQUEST_TERMINAL;
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    r_runtime_darwin_io_internal_handle_notify_request_terminal(request->handle);
    if (write_data != NULL) {
        dispatch_release(write_data);
    }
    r_runtime_darwin_io_internal_close_operation_channel(request, 0);
    if (handle->borrowed_shared_write_pipeline) {
        r_runtime_darwin_io_internal_handle_release_borrowed_root(handle);
    }
    r_runtime_darwin_io_internal_disarm_deadline(request);
    r_runtime_darwin_io_internal_schedule_ready_requests(handle);
    r_runtime_darwin_io_internal_request_release(request);
}

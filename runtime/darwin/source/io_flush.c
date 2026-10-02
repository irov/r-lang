#include "r_runtime_darwin_io.h"

#include "io_internal.h"
#include <dispatch/dispatch.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>

_Bool r_runtime_darwin_io_internal_activate_flush(RRuntimeDarwinIoRequest *request) {
    RRuntimeDarwinIoHandle *handle = request->handle;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (request->state != R_RUNTIME_DARWIN_IO_REQUEST_ACTIVE || request->native_scheduled) {
        if (pthread_mutex_unlock(&request->mutex) != 0) {
            abort();
        }
        return 0;
    }
    if (handle->root_channel == NULL || handle->root_released || request->references == SIZE_MAX) {
        (void)pthread_mutex_unlock(&request->mutex);
        abort();
    }
    request->native_scheduled = 1;
    request->references += 1U;
    dispatch_io_barrier(handle->root_channel, ^{
      r_runtime_darwin_io_internal_operation_done(request, 0U, 0U, 0, 0);
    });
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    return 1;
}

RRuntimeDarwinIoPrepareResult r_runtime_darwin_io_prepare_flush(RRuntimeDarwinIoHandle *handle,
                                                                uint64_t timeout_nanoseconds) {
    RRuntimeDarwinIoRequest *request;
    RRuntimeDarwinIoStartStatus status;
    int native_error;

    if (timeout_nanoseconds > (uint64_t)INT64_MAX || pthread_mutex_lock(&handle->mutex) != 0) {
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
        handle, R_RUNTIME_DARWIN_IO_FLUSH, NULL, &status, &native_error);
    if (request == NULL) {
        (void)pthread_mutex_unlock(&handle->mutex);
        return r_runtime_darwin_io_internal_prepare_failure(status, native_error);
    }
    request->state = R_RUNTIME_DARWIN_IO_REQUEST_PREPARED;
    if ((timeout_nanoseconds != 0U && r_runtime_darwin_io_internal_testing_should_fail_prepare(
                                          R_RUNTIME_DARWIN_IO_PREPARE_FAIL_DEADLINE)) ||
        !r_runtime_darwin_io_internal_prepare_deadline(request, timeout_nanoseconds)) {
        (void)pthread_mutex_unlock(&handle->mutex);
        r_runtime_darwin_io_prepared_abort(&request);
        return r_runtime_darwin_io_internal_prepare_failure(
            R_RUNTIME_DARWIN_IO_START_NATIVE_RETAIN_FAILED, ENOMEM);
    }
    if (pthread_mutex_unlock(&handle->mutex) != 0) {
        abort();
    }
    return r_runtime_darwin_io_internal_prepare_success(request);
}

RRuntimeDarwinIoSubmitResult r_runtime_darwin_io_submit_flush(RRuntimeDarwinIoHandle *handle,
                                                              uint64_t timeout_nanoseconds) {
    RRuntimeDarwinIoPrepareResult preparation =
        r_runtime_darwin_io_prepare_flush(handle, timeout_nanoseconds);
    RRuntimeDarwinIoSubmitResult submission;

    if (preparation.status != R_RUNTIME_DARWIN_IO_START_OK) {
        return r_runtime_darwin_io_internal_submit_failure(preparation.status,
                                                           preparation.native_error);
    }
    submission = r_runtime_darwin_io_prepared_activate(&preparation.prepared, NULL);
    if (submission.status != R_RUNTIME_DARWIN_IO_START_OK) {
        r_runtime_darwin_io_prepared_abort(&preparation.prepared);
    }
    return submission;
}

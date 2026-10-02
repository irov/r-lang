#include "r_runtime_darwin_io.h"

#include "io_internal.h"
#include "r_runtime_darwin_event.h"

#include <dispatch/dispatch.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>

static RRuntimeDarwinIoRequest *find_uncancelled_earlier_request_locked(
    RRuntimeDarwinIoHandle *handle, uint64_t close_sequence, _Bool include_all) {
    RRuntimeDarwinIoRequest *request;

    for (request = handle->requests; request != NULL; request = request->next) {
        _Bool selected = 0;

        if (request->operation == R_RUNTIME_DARWIN_IO_CLOSE ||
            (!include_all && request->submission_sequence >= close_sequence)) {
            continue;
        }
        if (pthread_mutex_lock(&request->mutex) != 0) {
            abort();
        }
        if ((request->state == R_RUNTIME_DARWIN_IO_REQUEST_PREPARED ||
             request->state == R_RUNTIME_DARWIN_IO_REQUEST_ACTIVE) &&
            request->pending_event == 0) {
            if (request->references == SIZE_MAX) {
                (void)pthread_mutex_unlock(&request->mutex);
                abort();
            }
            request->references += 1U;
            selected = 1;
        }
        if (pthread_mutex_unlock(&request->mutex) != 0) {
            abort();
        }
        if (selected) {
            return request;
        }
    }
    return NULL;
}

static void
cancel_requests(RRuntimeDarwinIoHandle *handle, uint64_t close_sequence, _Bool include_all) {
    for (;;) {
        RRuntimeDarwinIoRequest *request;

        if (pthread_mutex_lock(&handle->mutex) != 0) {
            abort();
        }
        request = find_uncancelled_earlier_request_locked(handle, close_sequence, include_all);
        if (pthread_mutex_unlock(&handle->mutex) != 0) {
            abort();
        }
        if (request == NULL) {
            return;
        }
        (void)r_runtime_darwin_io_internal_request_cancel_for_close(request);
        r_runtime_darwin_io_internal_request_release(request);
    }
}

void r_runtime_darwin_io_internal_cancel_all_requests(RRuntimeDarwinIoHandle *handle) {
    cancel_requests(handle, UINT64_C(0), 1);
}

static _Bool earlier_native_request_pending_locked(RRuntimeDarwinIoHandle *handle,
                                                   uint64_t close_sequence) {
    RRuntimeDarwinIoRequest *request;

    for (request = handle->requests; request != NULL; request = request->next) {
        RRuntimeDarwinIoRequestState state;

        if (request->operation == R_RUNTIME_DARWIN_IO_CLOSE ||
            request->submission_sequence >= close_sequence) {
            continue;
        }
        if (pthread_mutex_lock(&request->mutex) != 0) {
            abort();
        }
        state = request->state;
        if (pthread_mutex_unlock(&request->mutex) != 0) {
            abort();
        }
        if (state == R_RUNTIME_DARWIN_IO_REQUEST_ACTIVE) {
            return 1;
        }
    }
    return 0;
}

static RRuntimeDarwinIoRequest *
next_console_close_to_report_locked(RRuntimeDarwinIoHandle *handle) {
    RRuntimeDarwinIoRequest *request;

    for (request = handle->requests; request != NULL; request = request->next) {
        _Bool ready = 0;

        if (request->operation != R_RUNTIME_DARWIN_IO_CLOSE) {
            continue;
        }
        if (pthread_mutex_lock(&request->mutex) != 0) {
            abort();
        }
        if (request->state == R_RUNTIME_DARWIN_IO_REQUEST_ACTIVE &&
            request->close_completion_owned && !request->close_completion_reported &&
            !earlier_native_request_pending_locked(handle, request->submission_sequence)) {
            request->close_completion_reported = 1;
            ready = 1;
        }
        if (pthread_mutex_unlock(&request->mutex) != 0) {
            abort();
        }
        if (ready) {
            return request;
        }
    }
    return NULL;
}

void r_runtime_darwin_io_internal_try_complete_console_closes(RRuntimeDarwinIoHandle *handle) {
    for (;;) {
        RRuntimeDarwinIoRequest *request;

        if (pthread_mutex_lock(&handle->mutex) != 0) {
            abort();
        }
        if (!handle->console_identity) {
            if (pthread_mutex_unlock(&handle->mutex) != 0) {
                abort();
            }
            return;
        }
        request = next_console_close_to_report_locked(handle);
        if (pthread_mutex_unlock(&handle->mutex) != 0) {
            abort();
        }
        if (request == NULL) {
            return;
        }
        r_runtime_darwin_io_internal_close_done(request, 0);
        r_runtime_darwin_io_internal_request_release(request);
    }
}

static void complete_closes_after_root_cleanup(RRuntimeDarwinIoHandle *handle) {
    for (;;) {
        RRuntimeDarwinIoRequest *request = NULL;
        int native_error;

        if (pthread_mutex_lock(&handle->mutex) != 0) {
            abort();
        }
        if (!handle->root_cleanup_done) {
            if (pthread_mutex_unlock(&handle->mutex) != 0) {
                abort();
            }
            return;
        }
        native_error = handle->root_cleanup_error;
        for (request = handle->requests; request != NULL; request = request->next) {
            _Bool selected = 0;

            if (request->operation != R_RUNTIME_DARWIN_IO_CLOSE) {
                continue;
            }
            if (pthread_mutex_lock(&request->mutex) != 0) {
                abort();
            }
            if (request->state == R_RUNTIME_DARWIN_IO_REQUEST_ACTIVE &&
                request->close_completion_owned && !request->close_completion_reported) {
                request->close_completion_reported = 1;
                selected = 1;
            }
            if (pthread_mutex_unlock(&request->mutex) != 0) {
                abort();
            }
            if (selected) {
                break;
            }
        }
        if (pthread_mutex_unlock(&handle->mutex) != 0) {
            abort();
        }
        if (request == NULL) {
            return;
        }
        r_runtime_darwin_io_internal_close_done(request, native_error);
        r_runtime_darwin_io_internal_request_release(request);
    }
}

RRuntimeDarwinIoPrepareResult r_runtime_darwin_io_prepare_close(RRuntimeDarwinIoHandle *handle,
                                                                uint64_t timeout_nanoseconds) {
    RRuntimeDarwinIoRequest *request;
    RRuntimeDarwinIoStartStatus status;
    int native_error;

    if (timeout_nanoseconds > (uint64_t)INT64_MAX || pthread_mutex_lock(&handle->mutex) != 0) {
        return r_runtime_darwin_io_internal_prepare_failure(
            R_RUNTIME_DARWIN_IO_START_INVALID_ARGUMENT, 0);
    }
    if (r_runtime_darwin_io_internal_testing_should_fail_prepare(
            R_RUNTIME_DARWIN_IO_PREPARE_FAIL_REQUEST)) {
        (void)pthread_mutex_unlock(&handle->mutex);
        return r_runtime_darwin_io_internal_prepare_failure(
            R_RUNTIME_DARWIN_IO_START_ALLOCATION_FAILED, ENOMEM);
    }
    request = r_runtime_darwin_io_internal_request_create_locked(
        handle, R_RUNTIME_DARWIN_IO_CLOSE, NULL, &status, &native_error);
    if (request == NULL) {
        (void)pthread_mutex_unlock(&handle->mutex);
        return r_runtime_darwin_io_internal_prepare_failure(status, native_error);
    }
    request->state = R_RUNTIME_DARWIN_IO_REQUEST_PREPARED;
    request->close_deadline_enabled = timeout_nanoseconds != 0U;
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

RRuntimeDarwinIoSubmitResult
r_runtime_darwin_io_prepared_activate_close(RRuntimeDarwinIoPreparedRequest **prepared_slot,
                                            _Bool deadline_expired) {
    RRuntimeDarwinIoRequest *request;
    RRuntimeDarwinIoHandle *handle;
    dispatch_io_t root_channel = NULL;
    _Bool console_identity;

    request = *prepared_slot;
    handle = request->handle;
    if (pthread_mutex_lock(&handle->mutex) != 0) {
        return r_runtime_darwin_io_internal_submit_failure(
            R_RUNTIME_DARWIN_IO_START_INVALID_ARGUMENT, 0);
    }
    if (pthread_mutex_lock(&request->mutex) != 0) {
        (void)pthread_mutex_unlock(&handle->mutex);
        return r_runtime_darwin_io_internal_submit_failure(
            R_RUNTIME_DARWIN_IO_START_INVALID_ARGUMENT, 0);
    }
    request->state = R_RUNTIME_DARWIN_IO_REQUEST_ACTIVE;
    request->references += 1U;
    request->close_completion_owned = 1;
    if (deadline_expired) {
        request->pending_event = R_RUNTIME_DARWIN_IO_TERMINAL_TIMED_OUT;
        request->pending_event_sequence = r_runtime_darwin_event_sequence_next();
    }
    *prepared_slot = NULL;
    console_identity = handle->console_identity;
    if (!console_identity) {
        handle->closed = 1;
        if (!handle->root_released) {
            root_channel = handle->root_channel;
            handle->root_channel = NULL;
            handle->root_released = 1;
        }
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        (void)pthread_mutex_unlock(&handle->mutex);
        abort();
    }
    if (pthread_mutex_unlock(&handle->mutex) != 0) {
        abort();
    }

    r_runtime_darwin_io_internal_activate_deadline(request);
    cancel_requests(handle, request->submission_sequence, 0);
    if (root_channel != NULL) {
        dispatch_io_close(root_channel, DISPATCH_IO_STOP);
        dispatch_release(root_channel);
    }
    if (console_identity) {
        r_runtime_darwin_io_internal_try_complete_console_closes(handle);
    } else {
        complete_closes_after_root_cleanup(handle);
    }
    return r_runtime_darwin_io_internal_submit_success(request);
}

RRuntimeDarwinIoSubmitResult r_runtime_darwin_io_submit_close(RRuntimeDarwinIoHandle *handle) {
    RRuntimeDarwinIoPrepareResult preparation = r_runtime_darwin_io_prepare_close(handle, 0U);
    RRuntimeDarwinIoSubmitResult submission;

    if (preparation.status != R_RUNTIME_DARWIN_IO_START_OK) {
        return r_runtime_darwin_io_internal_submit_failure(preparation.status,
                                                           preparation.native_error);
    }
    submission = r_runtime_darwin_io_prepared_activate_close(&preparation.prepared, 0);
    if (submission.status != R_RUNTIME_DARWIN_IO_START_OK) {
        r_runtime_darwin_io_prepared_abort(&preparation.prepared);
    }
    return submission;
}

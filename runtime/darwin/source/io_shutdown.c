#include "r_runtime_darwin_io.h"

#include "io_internal.h"
#include "r_runtime_darwin_event.h"
#include <dispatch/dispatch.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/socket.h>

#if defined(R_RUNTIME_DARWIN_IO_TESTING)
static _Atomic int shutdown_testing_native_error;
static pthread_mutex_t shutdown_entry_test_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t shutdown_entry_test_condition = PTHREAD_COND_INITIALIZER;
static _Bool shutdown_entry_test_armed;
static _Bool shutdown_entry_test_reached;
static _Bool shutdown_entry_test_released;

void r_runtime_darwin_io_testing_fail_next_shutdown_native(int native_error) {
    atomic_store_explicit(&shutdown_testing_native_error, native_error, memory_order_relaxed);
}

static int shutdown_testing_take_native_error(void) {
    return atomic_exchange_explicit(&shutdown_testing_native_error, 0, memory_order_relaxed);
}

void r_runtime_darwin_io_testing_pause_next_shutdown_entry(void) {
    if (pthread_mutex_lock(&shutdown_entry_test_mutex) != 0) {
        abort();
    }
    if (shutdown_entry_test_armed) {
        (void)pthread_mutex_unlock(&shutdown_entry_test_mutex);
        abort();
    }
    shutdown_entry_test_armed = 1;
    shutdown_entry_test_reached = 0;
    shutdown_entry_test_released = 0;
    if (pthread_mutex_unlock(&shutdown_entry_test_mutex) != 0) {
        abort();
    }
}

void r_runtime_darwin_io_testing_wait_for_shutdown_entry(void) {
    if (pthread_mutex_lock(&shutdown_entry_test_mutex) != 0) {
        abort();
    }
    if (!shutdown_entry_test_armed) {
        (void)pthread_mutex_unlock(&shutdown_entry_test_mutex);
        abort();
    }
    while (!shutdown_entry_test_reached) {
        if (pthread_cond_wait(&shutdown_entry_test_condition, &shutdown_entry_test_mutex) != 0) {
            (void)pthread_mutex_unlock(&shutdown_entry_test_mutex);
            abort();
        }
    }
    if (pthread_mutex_unlock(&shutdown_entry_test_mutex) != 0) {
        abort();
    }
}

void r_runtime_darwin_io_testing_release_shutdown_entry(void) {
    if (pthread_mutex_lock(&shutdown_entry_test_mutex) != 0) {
        abort();
    }
    if (!shutdown_entry_test_armed || !shutdown_entry_test_reached ||
        shutdown_entry_test_released) {
        (void)pthread_mutex_unlock(&shutdown_entry_test_mutex);
        abort();
    }
    shutdown_entry_test_released = 1;
    if (pthread_cond_broadcast(&shutdown_entry_test_condition) != 0) {
        (void)pthread_mutex_unlock(&shutdown_entry_test_mutex);
        abort();
    }
    if (pthread_mutex_unlock(&shutdown_entry_test_mutex) != 0) {
        abort();
    }
}

static void testing_pause_shutdown_entry(void) {
    if (pthread_mutex_lock(&shutdown_entry_test_mutex) != 0) {
        abort();
    }
    if (shutdown_entry_test_armed) {
        shutdown_entry_test_reached = 1;
        if (pthread_cond_broadcast(&shutdown_entry_test_condition) != 0) {
            (void)pthread_mutex_unlock(&shutdown_entry_test_mutex);
            abort();
        }
        while (!shutdown_entry_test_released) {
            if (pthread_cond_wait(&shutdown_entry_test_condition, &shutdown_entry_test_mutex) !=
                0) {
                (void)pthread_mutex_unlock(&shutdown_entry_test_mutex);
                abort();
            }
        }
        shutdown_entry_test_armed = 0;
        shutdown_entry_test_reached = 0;
        shutdown_entry_test_released = 0;
    }
    if (pthread_mutex_unlock(&shutdown_entry_test_mutex) != 0) {
        abort();
    }
}
#else
static int shutdown_testing_take_native_error(void) {
    return 0;
}

static void testing_pause_shutdown_entry(void) {
}
#endif

static unsigned int shutdown_bits(RRuntimeDarwinIoShutdownDirection direction) {
    switch (direction) {
    case R_RUNTIME_DARWIN_IO_SHUTDOWN_INPUT:
        return 1U;
    case R_RUNTIME_DARWIN_IO_SHUTDOWN_OUTPUT:
        return 2U;
    case R_RUNTIME_DARWIN_IO_SHUTDOWN_BOTH:
        return 3U;
    }
    abort();
}

static RRuntimeDarwinIoShutdownDirection shutdown_direction(unsigned int bits) {
    switch (bits) {
    case 1U:
        return R_RUNTIME_DARWIN_IO_SHUTDOWN_INPUT;
    case 2U:
        return R_RUNTIME_DARWIN_IO_SHUTDOWN_OUTPUT;
    case 3U:
        return R_RUNTIME_DARWIN_IO_SHUTDOWN_BOTH;
    default:
        abort();
    }
}

static int shutdown_how(unsigned int bits) {
    switch (bits) {
    case 1U:
        return SHUT_RD;
    case 2U:
        return SHUT_WR;
    case 3U:
        return SHUT_RDWR;
    default:
        abort();
    }
}

/* Darwin fails a read-side shutdown with ENOTCONN once the peer's end-of-stream has arrived,
   although the connection keeps its peer association. Its input then receives nothing more,
   which is the effect a read shutdown publishes (R-SLIB-NET-0006); a write side that is still
   open is published alone. Any other ENOTCONN, such as after a reset, stays a failure. */
static int shutdown_after_peer_end(int descriptor, unsigned int effective) {
    struct sockaddr_storage peer;
    socklen_t length = (socklen_t)sizeof(peer);
    int status;
    int native_error;

    if (((effective & 1U) == 0U) ||
        (getpeername(descriptor, (struct sockaddr *)&peer, &length) != 0)) {
        return ENOTCONN;
    }
    if ((effective & 2U) == 0U) {
        return 0;
    }
    do {
        status = shutdown(descriptor, SHUT_WR);
        native_error = status == 0 ? 0 : errno;
    } while (status != 0 && native_error == EINTR);
    return native_error;
}

/* A refused entry is a cancellation event recorded before the native completion, so the request
   finalizes as cancelled with no half-close; an event already pending keeps its own sequence. */
static void shutdown_refuse_entry(RRuntimeDarwinIoRequest *request) {
    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (request->pending_event == 0) {
        request->pending_event = R_RUNTIME_DARWIN_IO_TERMINAL_CANCELLED;
        request->pending_event_sequence = r_runtime_darwin_event_sequence_next();
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
}

static void shutdown_worker(void *context) {
    RRuntimeDarwinIoRequest *request = context;
    RRuntimeDarwinIoHandle *handle = request->handle;
    RRuntimeDarwinIoShutdownCommitFn commit = NULL;
    void *commit_context = NULL;
    RRuntimeDarwinIoShutdownEntryFn entry = NULL;
    void *entry_context = NULL;
    unsigned int requested;
    unsigned int committed;
    unsigned int effective;
    int descriptor;
    int native_error;
    int status;

    if (pthread_mutex_lock(&handle->mutex) != 0) {
        abort();
    }
    if (pthread_mutex_lock(&request->mutex) != 0) {
        (void)pthread_mutex_unlock(&handle->mutex);
        abort();
    }
    if (request->operation != R_RUNTIME_DARWIN_IO_SHUTDOWN ||
        request->state != R_RUNTIME_DARWIN_IO_REQUEST_ACTIVE || !request->native_scheduled ||
        request->operation_done || request->shutdown_committed ||
        request->shutdown_commit == NULL || request->shutdown_context == NULL) {
        (void)pthread_mutex_unlock(&request->mutex);
        (void)pthread_mutex_unlock(&handle->mutex);
        abort();
    }
    if (request->pending_event != 0) {
        if (pthread_mutex_unlock(&request->mutex) != 0 ||
            pthread_mutex_unlock(&handle->mutex) != 0) {
            abort();
        }
        r_runtime_darwin_io_internal_operation_done(request, 0U, 0U, 0, 0);
        return;
    }
    requested = shutdown_bits(request->shutdown_direction);
    committed = (handle->input_shutdown ? 1U : 0U) | (handle->output_shutdown ? 2U : 0U);
    effective = requested & ~committed;
    descriptor = handle->retained_descriptor;
    commit = request->shutdown_commit;
    commit_context = request->shutdown_context;
    entry = request->shutdown_entry;
    entry_context = request->shutdown_entry_context;
    if (pthread_mutex_unlock(&request->mutex) != 0 || pthread_mutex_unlock(&handle->mutex) != 0) {
        abort();
    }

    testing_pause_shutdown_entry();
    if (entry != NULL && !entry(entry_context)) {
        shutdown_refuse_entry(request);
        r_runtime_darwin_io_internal_operation_done(request, 0U, 0U, 0, 0);
        return;
    }
    native_error = effective == 0U ? 0 : shutdown_testing_take_native_error();
    if (native_error == 0 && effective != 0U) {
        do {
            status = shutdown(descriptor, shutdown_how(effective));
            native_error = status == 0 ? 0 : errno;
        } while (status != 0 && native_error == EINTR);
        if (native_error == ENOTCONN) {
            native_error = shutdown_after_peer_end(descriptor, effective);
        }
    }
    if (native_error == 0) {
        if (pthread_mutex_lock(&handle->mutex) != 0) {
            abort();
        }
        handle->input_shutdown = handle->input_shutdown || (effective & 1U) != 0U;
        handle->output_shutdown = handle->output_shutdown || (effective & 2U) != 0U;
        if (pthread_mutex_unlock(&handle->mutex) != 0) {
            abort();
        }
        if (effective != 0U) {
            commit(commit_context, shutdown_direction(effective));
        }
        if (pthread_mutex_lock(&request->mutex) != 0) {
            abort();
        }
        request->shutdown_committed = 1;
        if (pthread_mutex_unlock(&request->mutex) != 0) {
            abort();
        }
    }
    r_runtime_darwin_io_internal_operation_done(request, 0U, 0U, native_error, 0);
}

_Bool r_runtime_darwin_io_internal_activate_shutdown(RRuntimeDarwinIoRequest *request) {
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
    if (handle->type != R_RUNTIME_DARWIN_IO_STREAM || handle->root_channel == NULL ||
        handle->root_released || request->shutdown_commit == NULL ||
        request->shutdown_context == NULL || request->references == SIZE_MAX) {
        (void)pthread_mutex_unlock(&request->mutex);
        abort();
    }
    request->native_scheduled = 1;
    request->references += 1U;
    dispatch_async_f(handle->callback_queue, request, shutdown_worker);
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    return 1;
}

_Bool r_runtime_darwin_io_prepared_set_shutdown_entry(RRuntimeDarwinIoPreparedRequest *prepared,
                                                      RRuntimeDarwinIoShutdownEntryFn entry,
                                                      void *context) {
    _Bool changed = 0;

    if (prepared == NULL || entry == NULL || context == NULL ||
        pthread_mutex_lock(&prepared->mutex) != 0) {
        return 0;
    }
    if (prepared->operation == R_RUNTIME_DARWIN_IO_SHUTDOWN &&
        prepared->state == R_RUNTIME_DARWIN_IO_REQUEST_PREPARED) {
        prepared->shutdown_entry = entry;
        prepared->shutdown_entry_context = context;
        changed = 1;
    }
    if (pthread_mutex_unlock(&prepared->mutex) != 0) {
        abort();
    }
    return changed;
}

RRuntimeDarwinIoPrepareResult
r_runtime_darwin_io_prepare_shutdown(RRuntimeDarwinIoHandle *handle,
                                     RRuntimeDarwinIoShutdownDirection direction,
                                     uint64_t timeout_nanoseconds,
                                     RRuntimeDarwinIoShutdownCommitFn commit,
                                     void *context) {
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
        handle, R_RUNTIME_DARWIN_IO_SHUTDOWN, NULL, &status, &native_error);
    if (request == NULL) {
        (void)pthread_mutex_unlock(&handle->mutex);
        return r_runtime_darwin_io_internal_prepare_failure(status, native_error);
    }
    request->state = R_RUNTIME_DARWIN_IO_REQUEST_PREPARED;
    request->shutdown_direction = direction;
    request->shutdown_commit = commit;
    request->shutdown_context = context;
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

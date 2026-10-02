#include "r_runtime_darwin_io.h"

#include "io_internal.h"
#include <dispatch/dispatch.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(R_RUNTIME_DARWIN_IO_TESTING)
static _Atomic int forced_root_cleanup_error;
static _Atomic int forced_handle_create_failure_stage;
static _Atomic int forced_handle_create_native_error;
static pthread_mutex_t root_cleanup_test_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t root_cleanup_test_condition = PTHREAD_COND_INITIALIZER;
static _Bool root_cleanup_test_armed;
static _Bool root_cleanup_test_reached;
static _Bool root_cleanup_test_released;

void r_runtime_darwin_io_testing_pause_next_root_cleanup_before_report(void) {
    if (pthread_mutex_lock(&root_cleanup_test_mutex) != 0) {
        abort();
    }
    if (root_cleanup_test_armed) {
        (void)pthread_mutex_unlock(&root_cleanup_test_mutex);
        abort();
    }
    root_cleanup_test_armed = 1;
    root_cleanup_test_reached = 0;
    root_cleanup_test_released = 0;
    if (pthread_mutex_unlock(&root_cleanup_test_mutex) != 0) {
        abort();
    }
}

void r_runtime_darwin_io_testing_wait_for_root_cleanup_before_report(void) {
    if (pthread_mutex_lock(&root_cleanup_test_mutex) != 0) {
        abort();
    }
    if (!root_cleanup_test_armed) {
        (void)pthread_mutex_unlock(&root_cleanup_test_mutex);
        abort();
    }
    while (!root_cleanup_test_reached) {
        if (pthread_cond_wait(&root_cleanup_test_condition, &root_cleanup_test_mutex) != 0) {
            (void)pthread_mutex_unlock(&root_cleanup_test_mutex);
            abort();
        }
    }
    if (pthread_mutex_unlock(&root_cleanup_test_mutex) != 0) {
        abort();
    }
}

void r_runtime_darwin_io_testing_release_root_cleanup_before_report(void) {
    if (pthread_mutex_lock(&root_cleanup_test_mutex) != 0) {
        abort();
    }
    if (!root_cleanup_test_armed || !root_cleanup_test_reached || root_cleanup_test_released) {
        (void)pthread_mutex_unlock(&root_cleanup_test_mutex);
        abort();
    }
    root_cleanup_test_released = 1;
    if (pthread_cond_broadcast(&root_cleanup_test_condition) != 0) {
        (void)pthread_mutex_unlock(&root_cleanup_test_mutex);
        abort();
    }
    if (pthread_mutex_unlock(&root_cleanup_test_mutex) != 0) {
        abort();
    }
}

static void testing_pause_root_cleanup_before_report(void) {
    if (pthread_mutex_lock(&root_cleanup_test_mutex) != 0) {
        abort();
    }
    if (root_cleanup_test_armed) {
        root_cleanup_test_reached = 1;
        if (pthread_cond_broadcast(&root_cleanup_test_condition) != 0) {
            (void)pthread_mutex_unlock(&root_cleanup_test_mutex);
            abort();
        }
        while (!root_cleanup_test_released) {
            if (pthread_cond_wait(&root_cleanup_test_condition, &root_cleanup_test_mutex) != 0) {
                (void)pthread_mutex_unlock(&root_cleanup_test_mutex);
                abort();
            }
        }
        root_cleanup_test_armed = 0;
        root_cleanup_test_reached = 0;
        root_cleanup_test_released = 0;
    }
    if (pthread_mutex_unlock(&root_cleanup_test_mutex) != 0) {
        abort();
    }
}

void r_runtime_darwin_io_testing_force_root_cleanup_error(int native_error) {
    atomic_store_explicit(&forced_root_cleanup_error, native_error, memory_order_relaxed);
}

void r_runtime_darwin_io_testing_force_handle_root_cleanup_error(RRuntimeDarwinIoHandle *handle,
                                                                 int native_error) {
    if (handle == NULL || pthread_mutex_lock(&handle->mutex) != 0) {
        abort();
    }
    handle->forced_root_cleanup_error = native_error;
    if (pthread_mutex_unlock(&handle->mutex) != 0) {
        abort();
    }
}

void r_runtime_darwin_io_testing_fail_handle_create_stage(
    RRuntimeDarwinIoHandleCreateFailureStage stage) {
    atomic_store_explicit(&forced_handle_create_failure_stage, (int)stage, memory_order_relaxed);
}

void r_runtime_darwin_io_testing_fail_next_handle_create_native(int native_error) {
    atomic_store_explicit(&forced_handle_create_native_error, native_error, memory_order_relaxed);
}

static _Bool testing_should_fail_handle_create(RRuntimeDarwinIoHandleCreateFailureStage stage) {
    int expected = (int)stage;

    return atomic_compare_exchange_strong_explicit(&forced_handle_create_failure_stage,
                                                   &expected,
                                                   (int)R_RUNTIME_DARWIN_IO_HANDLE_CREATE_FAIL_NONE,
                                                   memory_order_relaxed,
                                                   memory_order_relaxed);
}

static int testing_take_handle_create_native_error(void) {
    return atomic_exchange_explicit(&forced_handle_create_native_error, 0, memory_order_relaxed);
}
#else
static void testing_pause_root_cleanup_before_report(void) {
}
#endif

static RRuntimeDarwinIoHandleCreateResult handle_create_failure(RRuntimeDarwinIoStartStatus status,
                                                                int native_error) {
    RRuntimeDarwinIoHandleCreateResult result;

    result.handle = NULL;
    result.status = status;
    result.native_error = native_error;
    return result;
}

static void handle_deallocate(RRuntimeDarwinIoHandle *handle) {
    if (handle->callback_queue != NULL) {
        dispatch_release(handle->callback_queue);
    }
    if (pthread_cond_destroy(&handle->condition) != 0) {
        abort();
    }
    if (pthread_mutex_destroy(&handle->mutex) != 0) {
        abort();
    }
    r_runtime_allocator_deallocate(handle, _Alignof(RRuntimeDarwinIoHandle));
}

static dispatch_io_t handle_release_reference_locked(RRuntimeDarwinIoHandle *handle,
                                                     _Bool *destroy) {
    dispatch_io_t root_channel = NULL;

    if (handle->references == 0U) {
        abort();
    }
    handle->references -= 1U;
    if (handle->references == 1U && handle->view_count == 0U && !handle->runtime_root_owned &&
        !handle->root_released) {
        root_channel = handle->root_channel;
        handle->root_channel = NULL;
        handle->root_released = 1;
        handle->closed = 1;
    }
    *destroy = handle->references == 0U;
    if (*destroy &&
        (handle->view_count != 0U || handle->request_count != 0U || handle->runtime_root_owned ||
         !handle->root_released || !handle->root_cleanup_done || handle->requests != NULL ||
         handle->retained_descriptor >= 0)) {
        abort();
    }
    return root_channel;
}

static void
handle_finish_release(RRuntimeDarwinIoHandle *handle, dispatch_io_t root_channel, _Bool destroy) {
    if (root_channel != NULL) {
        dispatch_io_close(root_channel, 0);
        dispatch_release(root_channel);
    }
    if (destroy) {
        handle_deallocate(handle);
    }
}

void r_runtime_darwin_io_internal_handle_release_reference(RRuntimeDarwinIoHandle *handle) {
    dispatch_io_t root_channel;
    _Bool destroy;

    if (pthread_mutex_lock(&handle->mutex) != 0) {
        abort();
    }
    root_channel = handle_release_reference_locked(handle, &destroy);
    if (pthread_mutex_unlock(&handle->mutex) != 0) {
        abort();
    }
    handle_finish_release(handle, root_channel, destroy);
}

void r_runtime_darwin_io_internal_handle_release_request(RRuntimeDarwinIoHandle *handle) {
    dispatch_io_t root_channel;
    _Bool destroy;

    if (pthread_mutex_lock(&handle->mutex) != 0) {
        abort();
    }
    if (handle->request_count == 0U) {
        (void)pthread_mutex_unlock(&handle->mutex);
        abort();
    }
    handle->request_count -= 1U;
    root_channel = handle_release_reference_locked(handle, &destroy);
    if (pthread_mutex_unlock(&handle->mutex) != 0) {
        abort();
    }
    handle_finish_release(handle, root_channel, destroy);
}

static RRuntimeDarwinIoRequest *next_close_to_report_locked(RRuntimeDarwinIoHandle *handle) {
    RRuntimeDarwinIoRequest *request;

    for (request = handle->requests; request != NULL; request = request->next) {
        if (pthread_mutex_lock(&request->mutex) != 0) {
            abort();
        }
        if (request->operation == R_RUNTIME_DARWIN_IO_CLOSE &&
            request->state == R_RUNTIME_DARWIN_IO_REQUEST_ACTIVE &&
            request->close_completion_owned && !request->close_completion_reported) {
            request->close_completion_reported = 1;
            if (pthread_mutex_unlock(&request->mutex) != 0) {
                abort();
            }
            return request;
        }
        if (pthread_mutex_unlock(&request->mutex) != 0) {
            abort();
        }
    }
    return NULL;
}

static void handle_report_root_cleanup(RRuntimeDarwinIoHandle *handle, int native_error) {
    for (;;) {
        RRuntimeDarwinIoRequest *request;

        if (pthread_mutex_lock(&handle->mutex) != 0) {
            abort();
        }
        request = next_close_to_report_locked(handle);
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

void r_runtime_darwin_io_internal_handle_root_cleanup(RRuntimeDarwinIoHandle *handle,
                                                      int native_error) {
    RRuntimeDarwinIoHandleCleanupFn cleanup_observer;
    void *cleanup_observer_context;
    _Bool descriptor_owned;
    int descriptor;
    int close_error = 0;
#if defined(R_RUNTIME_DARWIN_IO_TESTING)
    int handle_forced_error;
#endif

    testing_pause_root_cleanup_before_report();

    if (pthread_mutex_lock(&handle->mutex) != 0) {
        abort();
    }
    descriptor = handle->retained_descriptor;
    handle->retained_descriptor = -1;
    descriptor_owned = handle->retained_descriptor_owned;
    handle->retained_descriptor_owned = 0;
    handle->closed = 1;
#if defined(R_RUNTIME_DARWIN_IO_TESTING)
    handle_forced_error = handle->forced_root_cleanup_error;
    handle->forced_root_cleanup_error = 0;
#endif
    if (pthread_mutex_unlock(&handle->mutex) != 0) {
        abort();
    }
    if (descriptor_owned && descriptor >= 0 && close(descriptor) != 0) {
        close_error = errno;
    }
    if (native_error == 0) {
        native_error = close_error;
    }
#if defined(R_RUNTIME_DARWIN_IO_TESTING)
    {
        const int forced_error =
            handle_forced_error != 0
                ? handle_forced_error
                : atomic_exchange_explicit(&forced_root_cleanup_error, 0, memory_order_relaxed);

        if (forced_error != 0) {
            native_error = forced_error;
        }
    }
#endif
    if (pthread_mutex_lock(&handle->mutex) != 0) {
        abort();
    }
    if (handle->root_cleanup_done) {
        (void)pthread_mutex_unlock(&handle->mutex);
        abort();
    }
    handle->root_cleanup_error = native_error;
    handle->root_cleanup_done = 1;
    cleanup_observer = handle->cleanup_observer;
    cleanup_observer_context = handle->cleanup_observer_context;
    handle->cleanup_observer = NULL;
    handle->cleanup_observer_context = NULL;
    if (pthread_cond_broadcast(&handle->condition) != 0) {
        (void)pthread_mutex_unlock(&handle->mutex);
        abort();
    }
    if (pthread_mutex_unlock(&handle->mutex) != 0) {
        abort();
    }
    handle_report_root_cleanup(handle, native_error);
    if (cleanup_observer != NULL) {
        cleanup_observer(cleanup_observer_context, native_error);
    }
    r_runtime_darwin_io_internal_handle_release_reference(handle);
}

static RRuntimeDarwinIoHandleCreateResult handle_create(RRuntimeAllocator *allocator,
                                                        int descriptor,
                                                        RRuntimeDarwinIoType type,
                                                        _Bool runtime_root) {
    RRuntimeDarwinIoHandleCreateResult result;
    RRuntimeDarwinIoHandle *handle = NULL;
    RRuntimeAllocationStatus allocation_status;
    dispatch_io_type_t dispatch_type;
    int condition_result;
    int native_error;

    if (descriptor < 0) {
        return handle_create_failure(R_RUNTIME_DARWIN_IO_START_INVALID_ARGUMENT, 0);
    }
#if defined(R_RUNTIME_DARWIN_IO_TESTING)
    native_error = testing_take_handle_create_native_error();
    if (native_error != 0) {
        return handle_create_failure(R_RUNTIME_DARWIN_IO_START_NATIVE_RETAIN_FAILED, native_error);
    }
    if (testing_should_fail_handle_create(R_RUNTIME_DARWIN_IO_HANDLE_CREATE_FAIL_ALLOCATION)) {
        return handle_create_failure(R_RUNTIME_DARWIN_IO_START_ALLOCATION_FAILED, ENOMEM);
    }
#endif
    allocation_status = r_runtime_allocator_allocate(
        allocator, sizeof(*handle), _Alignof(RRuntimeDarwinIoHandle), (void **)&handle);
    if (allocation_status != R_RUNTIME_ALLOCATION_OK) {
        return handle_create_failure(R_RUNTIME_DARWIN_IO_START_ALLOCATION_FAILED, ENOMEM);
    }
    (void)memset(handle, 0, sizeof(*handle));
    handle->allocator = allocator;
    handle->retained_descriptor = -1;
    native_error = pthread_mutex_init(&handle->mutex, NULL);
    if (native_error != 0) {
        r_runtime_allocator_deallocate(handle, _Alignof(RRuntimeDarwinIoHandle));
        return handle_create_failure(R_RUNTIME_DARWIN_IO_START_NATIVE_RETAIN_FAILED, native_error);
    }
    condition_result = pthread_cond_init(&handle->condition, NULL);
    if (condition_result != 0) {
        (void)pthread_mutex_destroy(&handle->mutex);
        r_runtime_allocator_deallocate(handle, _Alignof(RRuntimeDarwinIoHandle));
        return handle_create_failure(R_RUNTIME_DARWIN_IO_START_NATIVE_RETAIN_FAILED,
                                     condition_result);
    }
    handle->retained_descriptor = fcntl(descriptor, F_DUPFD_CLOEXEC, 0);
    if (handle->retained_descriptor < 0) {
        native_error = errno;
        (void)pthread_cond_destroy(&handle->condition);
        (void)pthread_mutex_destroy(&handle->mutex);
        r_runtime_allocator_deallocate(handle, _Alignof(RRuntimeDarwinIoHandle));
        return handle_create_failure(R_RUNTIME_DARWIN_IO_START_NATIVE_RETAIN_FAILED, native_error);
    }
    handle->retained_descriptor_owned = 1;
#if defined(R_RUNTIME_DARWIN_IO_TESTING)
    if (testing_should_fail_handle_create(R_RUNTIME_DARWIN_IO_HANDLE_CREATE_FAIL_AFTER_DUPLICATE)) {
        (void)close(handle->retained_descriptor);
        handle->retained_descriptor = -1;
        (void)pthread_cond_destroy(&handle->condition);
        (void)pthread_mutex_destroy(&handle->mutex);
        r_runtime_allocator_deallocate(handle, _Alignof(RRuntimeDarwinIoHandle));
        return handle_create_failure(R_RUNTIME_DARWIN_IO_START_NATIVE_RETAIN_FAILED, ENOMEM);
    }
#endif
    handle->callback_queue = dispatch_queue_create("r.io.payload", DISPATCH_QUEUE_SERIAL);
    if (handle->callback_queue == NULL) {
        (void)close(handle->retained_descriptor);
        (void)pthread_cond_destroy(&handle->condition);
        (void)pthread_mutex_destroy(&handle->mutex);
        r_runtime_allocator_deallocate(handle, _Alignof(RRuntimeDarwinIoHandle));
        return handle_create_failure(R_RUNTIME_DARWIN_IO_START_NATIVE_RETAIN_FAILED, ENOMEM);
    }
#if defined(R_RUNTIME_DARWIN_IO_TESTING)
    if (testing_should_fail_handle_create(R_RUNTIME_DARWIN_IO_HANDLE_CREATE_FAIL_AFTER_QUEUE)) {
        dispatch_release(handle->callback_queue);
        handle->callback_queue = NULL;
        (void)close(handle->retained_descriptor);
        handle->retained_descriptor = -1;
        (void)pthread_cond_destroy(&handle->condition);
        (void)pthread_mutex_destroy(&handle->mutex);
        r_runtime_allocator_deallocate(handle, _Alignof(RRuntimeDarwinIoHandle));
        return handle_create_failure(R_RUNTIME_DARWIN_IO_START_NATIVE_RETAIN_FAILED, ENOMEM);
    }
#endif
    handle->references = 2U;
    handle->view_count = runtime_root ? 0U : 1U;
    handle->runtime_root_owned = runtime_root;
    handle->console_identity = runtime_root;
    handle->type = type;
    dispatch_type = type == R_RUNTIME_DARWIN_IO_STREAM ? DISPATCH_IO_STREAM : DISPATCH_IO_RANDOM;
    handle->root_channel = dispatch_io_create(
        dispatch_type, handle->retained_descriptor, handle->callback_queue, ^(int error) {
          r_runtime_darwin_io_internal_handle_root_cleanup(handle, error);
        });
    if (handle->root_channel == NULL) {
        dispatch_release(handle->callback_queue);
        handle->callback_queue = NULL;
        (void)close(handle->retained_descriptor);
        handle->retained_descriptor = -1;
        (void)pthread_cond_destroy(&handle->condition);
        (void)pthread_mutex_destroy(&handle->mutex);
        r_runtime_allocator_deallocate(handle, _Alignof(RRuntimeDarwinIoHandle));
        return handle_create_failure(R_RUNTIME_DARWIN_IO_START_NATIVE_RETAIN_FAILED, EINVAL);
    }
    result.handle = handle;
    result.status = R_RUNTIME_DARWIN_IO_START_OK;
    result.native_error = 0;
    return result;
}

RRuntimeDarwinIoHandleCreateResult
r_runtime_darwin_io_internal_handle_prepare_borrowed_random(RRuntimeAllocator *allocator) {
    RRuntimeDarwinIoHandleCreateResult result;
    RRuntimeDarwinIoHandle *handle = NULL;
    RRuntimeAllocationStatus allocation_status;
    int condition_result;
    int native_error;

#if defined(R_RUNTIME_DARWIN_IO_TESTING)
    if (testing_should_fail_handle_create(R_RUNTIME_DARWIN_IO_HANDLE_CREATE_FAIL_ALLOCATION)) {
        return handle_create_failure(R_RUNTIME_DARWIN_IO_START_ALLOCATION_FAILED, ENOMEM);
    }
#endif
    allocation_status = r_runtime_allocator_allocate(
        allocator, sizeof(*handle), _Alignof(RRuntimeDarwinIoHandle), (void **)&handle);
    if (allocation_status != R_RUNTIME_ALLOCATION_OK) {
        return handle_create_failure(R_RUNTIME_DARWIN_IO_START_ALLOCATION_FAILED, ENOMEM);
    }
    (void)memset(handle, 0, sizeof(*handle));
    handle->allocator = allocator;
    handle->retained_descriptor = -1;
    native_error = pthread_mutex_init(&handle->mutex, NULL);
    if (native_error != 0) {
        r_runtime_allocator_deallocate(handle, _Alignof(RRuntimeDarwinIoHandle));
        return handle_create_failure(R_RUNTIME_DARWIN_IO_START_NATIVE_RETAIN_FAILED, native_error);
    }
    condition_result = pthread_cond_init(&handle->condition, NULL);
    if (condition_result != 0) {
        (void)pthread_mutex_destroy(&handle->mutex);
        r_runtime_allocator_deallocate(handle, _Alignof(RRuntimeDarwinIoHandle));
        return handle_create_failure(R_RUNTIME_DARWIN_IO_START_NATIVE_RETAIN_FAILED,
                                     condition_result);
    }
    handle->callback_queue = dispatch_queue_create("r.io.borrowed-write", DISPATCH_QUEUE_SERIAL);
    if (handle->callback_queue == NULL) {
        (void)pthread_cond_destroy(&handle->condition);
        (void)pthread_mutex_destroy(&handle->mutex);
        r_runtime_allocator_deallocate(handle, _Alignof(RRuntimeDarwinIoHandle));
        return handle_create_failure(R_RUNTIME_DARWIN_IO_START_NATIVE_RETAIN_FAILED, ENOMEM);
    }
#if defined(R_RUNTIME_DARWIN_IO_TESTING)
    if (testing_should_fail_handle_create(R_RUNTIME_DARWIN_IO_HANDLE_CREATE_FAIL_AFTER_QUEUE)) {
        dispatch_release(handle->callback_queue);
        handle->callback_queue = NULL;
        (void)pthread_cond_destroy(&handle->condition);
        (void)pthread_mutex_destroy(&handle->mutex);
        r_runtime_allocator_deallocate(handle, _Alignof(RRuntimeDarwinIoHandle));
        return handle_create_failure(R_RUNTIME_DARWIN_IO_START_NATIVE_RETAIN_FAILED, ENOMEM);
    }
#endif
    /* One reference is reserved for the future Dispatch root cleanup callback. */
    handle->references = 1U;
    handle->type = R_RUNTIME_DARWIN_IO_RANDOM;
    handle->borrowed_shared_write_pipeline = 1;
    result.handle = handle;
    result.status = R_RUNTIME_DARWIN_IO_START_OK;
    result.native_error = 0;
    return result;
}

RRuntimeDarwinIoStartStatus
r_runtime_darwin_io_internal_handle_bind_borrowed_random(RRuntimeDarwinIoHandle *handle,
                                                         int descriptor,
                                                         RRuntimeDarwinIoHandleCleanupFn cleanup,
                                                         void *context,
                                                         int *native_error) {
    dispatch_io_t root_channel;

    if (descriptor < 0 || pthread_mutex_lock(&handle->mutex) != 0) {
        return R_RUNTIME_DARWIN_IO_START_INVALID_ARGUMENT;
    }
    handle->retained_descriptor = descriptor;
    handle->retained_descriptor_owned = 0;
    handle->cleanup_observer = cleanup;
    handle->cleanup_observer_context = context;
    root_channel =
        dispatch_io_create(DISPATCH_IO_RANDOM, descriptor, handle->callback_queue, ^(int error) {
          r_runtime_darwin_io_internal_handle_root_cleanup(handle, error);
        });
    if (root_channel == NULL) {
        handle->retained_descriptor = -1;
        handle->cleanup_observer = NULL;
        handle->cleanup_observer_context = NULL;
        *native_error = EINVAL;
        if (pthread_mutex_unlock(&handle->mutex) != 0) {
            abort();
        }
        return R_RUNTIME_DARWIN_IO_START_NATIVE_RETAIN_FAILED;
    }
    handle->root_channel = root_channel;
    *native_error = 0;
    if (pthread_mutex_unlock(&handle->mutex) != 0) {
        abort();
    }
    return R_RUNTIME_DARWIN_IO_START_OK;
}

void r_runtime_darwin_io_internal_handle_release_borrowed_root(RRuntimeDarwinIoHandle *handle) {
    dispatch_io_t root_channel = NULL;
    _Bool release_unbound_reservation = 0;

    if (pthread_mutex_lock(&handle->mutex) != 0) {
        abort();
    }
    if (!handle->borrowed_shared_write_pipeline) {
        (void)pthread_mutex_unlock(&handle->mutex);
        abort();
    }
    if (!handle->root_released) {
        root_channel = handle->root_channel;
        handle->root_channel = NULL;
        handle->root_released = 1;
        handle->closed = 1;
        if (root_channel == NULL) {
            if (handle->retained_descriptor >= 0 || handle->root_cleanup_done) {
                (void)pthread_mutex_unlock(&handle->mutex);
                abort();
            }
            handle->root_cleanup_done = 1;
            handle->cleanup_observer = NULL;
            handle->cleanup_observer_context = NULL;
            release_unbound_reservation = 1;
            if (pthread_cond_broadcast(&handle->condition) != 0) {
                (void)pthread_mutex_unlock(&handle->mutex);
                abort();
            }
        }
    }
    if (pthread_mutex_unlock(&handle->mutex) != 0) {
        abort();
    }
    if (root_channel != NULL) {
        dispatch_io_close(root_channel, 0);
        dispatch_release(root_channel);
    } else if (release_unbound_reservation) {
        r_runtime_darwin_io_internal_handle_release_reference(handle);
    }
}

RRuntimeDarwinIoHandleCreateResult r_runtime_darwin_io_handle_create(RRuntimeAllocator *allocator,
                                                                     int descriptor,
                                                                     RRuntimeDarwinIoType type) {
    return handle_create(allocator, descriptor, type, 0);
}

RRuntimeDarwinIoHandleCreateResult r_runtime_darwin_io_internal_handle_create_runtime_root(
    RRuntimeAllocator *allocator, int descriptor, RRuntimeDarwinIoType type) {
    return handle_create(allocator, descriptor, type, 1);
}

_Bool r_runtime_darwin_io_handle_retain_view(RRuntimeDarwinIoHandle *handle) {
    _Bool retained = 0;

    if (handle == NULL || pthread_mutex_lock(&handle->mutex) != 0) {
        return 0;
    }
    if (handle->root_channel != NULL && !handle->root_released && !handle->closed &&
        handle->references != SIZE_MAX && handle->view_count != SIZE_MAX) {
        handle->references += 1U;
        handle->view_count += 1U;
        retained = 1;
    }
    if (pthread_mutex_unlock(&handle->mutex) != 0) {
        abort();
    }
    return retained;
}

void r_runtime_darwin_io_handle_release(RRuntimeDarwinIoHandle *handle) {
    dispatch_io_t root_channel;
    _Bool destroy;

    if (handle == NULL) {
        return;
    }
    if (pthread_mutex_lock(&handle->mutex) != 0) {
        abort();
    }
    if (handle->view_count == 0U) {
        (void)pthread_mutex_unlock(&handle->mutex);
        abort();
    }
    handle->view_count -= 1U;
    root_channel = handle_release_reference_locked(handle, &destroy);
    if (pthread_mutex_unlock(&handle->mutex) != 0) {
        abort();
    }
    handle_finish_release(handle, root_channel, destroy);
}

_Bool r_runtime_darwin_io_handle_release_with_cleanup(RRuntimeDarwinIoHandle *handle,
                                                      RRuntimeDarwinIoHandleCleanupFn cleanup,
                                                      void *context) {
    dispatch_io_t root_channel;
    _Bool cleanup_done;
    _Bool destroy;
    int cleanup_error;

    if (handle == NULL || pthread_mutex_lock(&handle->mutex) != 0) {
        return 0;
    }
    if (handle->view_count == 0U || handle->cleanup_observer != NULL) {
        if (pthread_mutex_unlock(&handle->mutex) != 0) {
            abort();
        }
        return 0;
    }
    cleanup_done = handle->root_cleanup_done;
    cleanup_error = handle->root_cleanup_error;
    if (!cleanup_done) {
        handle->cleanup_observer = cleanup;
        handle->cleanup_observer_context = context;
    }
    handle->view_count -= 1U;
    root_channel = handle_release_reference_locked(handle, &destroy);
    if (pthread_mutex_unlock(&handle->mutex) != 0) {
        abort();
    }
    if (cleanup_done) {
        cleanup(context, cleanup_error);
    }
    handle_finish_release(handle, root_channel, destroy);
    return 1;
}

_Bool r_runtime_darwin_io_handle_terminal_close_failure(RRuntimeDarwinIoHandle *handle,
                                                        int *native_error) {
    _Bool failed;

    if (handle == NULL || pthread_mutex_lock(&handle->mutex) != 0) {
        return 0;
    }
    failed = handle->root_cleanup_done && handle->root_cleanup_error != 0;
    *native_error = failed ? handle->root_cleanup_error : 0;
    if (pthread_mutex_unlock(&handle->mutex) != 0) {
        abort();
    }
    return failed;
}

void r_runtime_darwin_io_internal_handle_stop_accepting(RRuntimeDarwinIoHandle *handle) {
    if (pthread_mutex_lock(&handle->mutex) != 0) {
        abort();
    }
    handle->closed = 1;
    if (pthread_mutex_unlock(&handle->mutex) != 0) {
        abort();
    }
}

void r_runtime_darwin_io_internal_handle_wait_for_requests(RRuntimeDarwinIoHandle *handle) {
    if (pthread_mutex_lock(&handle->mutex) != 0) {
        abort();
    }
    while (r_runtime_darwin_io_internal_handle_has_active_request_locked(handle)) {
        if (pthread_cond_wait(&handle->condition, &handle->mutex) != 0) {
            (void)pthread_mutex_unlock(&handle->mutex);
            abort();
        }
    }
    if (pthread_mutex_unlock(&handle->mutex) != 0) {
        abort();
    }
}

void r_runtime_darwin_io_internal_handle_notify_request_terminal(RRuntimeDarwinIoHandle *handle) {
    if (pthread_mutex_lock(&handle->mutex) != 0) {
        abort();
    }
    if (pthread_cond_broadcast(&handle->condition) != 0) {
        (void)pthread_mutex_unlock(&handle->mutex);
        abort();
    }
    if (pthread_mutex_unlock(&handle->mutex) != 0) {
        abort();
    }
}

void r_runtime_darwin_io_internal_handle_release_runtime_root_and_wait(
    RRuntimeDarwinIoHandle *handle, _Bool stop_pending) {
    dispatch_io_t root_channel;
    _Bool destroy;

    if (pthread_mutex_lock(&handle->mutex) != 0) {
        abort();
    }
    if (!handle->runtime_root_owned || handle->references == SIZE_MAX) {
        (void)pthread_mutex_unlock(&handle->mutex);
        abort();
    }
    handle->references += 1U;
    handle->runtime_root_owned = 0;
    if (handle->references < 2U) {
        (void)pthread_mutex_unlock(&handle->mutex);
        abort();
    }
    handle->references -= 1U;
    root_channel = handle->root_channel;
    handle->root_channel = NULL;
    handle->root_released = 1;
    handle->closed = 1;
    if (pthread_mutex_unlock(&handle->mutex) != 0) {
        abort();
    }
    if (root_channel != NULL) {
        dispatch_io_close(root_channel, stop_pending ? DISPATCH_IO_STOP : 0);
        dispatch_release(root_channel);
    }
    if (pthread_mutex_lock(&handle->mutex) != 0) {
        abort();
    }
    while (!handle->root_cleanup_done) {
        if (pthread_cond_wait(&handle->condition, &handle->mutex) != 0) {
            (void)pthread_mutex_unlock(&handle->mutex);
            abort();
        }
    }
    root_channel = handle_release_reference_locked(handle, &destroy);
    if (pthread_mutex_unlock(&handle->mutex) != 0) {
        abort();
    }
    handle_finish_release(handle, root_channel, destroy);
}

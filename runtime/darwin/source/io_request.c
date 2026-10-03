#include "r_runtime_darwin_io.h"

#include "io_internal.h"
#include "r_runtime_darwin_event.h"

#include <dispatch/dispatch.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(R_RUNTIME_DARWIN_IO_TESTING)
static pthread_mutex_t native_completion_test_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t native_completion_test_condition = PTHREAD_COND_INITIALIZER;
static _Bool native_completion_test_armed;
static _Bool native_completion_test_reached;
static _Bool native_completion_test_released;
/* Unarmed hooks stay off the hot path: the hint is read without the mutex, and the mutex
   state stays authoritative once it is set. */
static _Atomic _Bool native_completion_test_hint;

void r_runtime_darwin_io_testing_pause_next_native_completion(void) {
    if (pthread_mutex_lock(&native_completion_test_mutex) != 0) {
        abort();
    }
    if (native_completion_test_armed) {
        (void)pthread_mutex_unlock(&native_completion_test_mutex);
        abort();
    }
    native_completion_test_armed = 1;
    atomic_store_explicit(&native_completion_test_hint, 1, memory_order_release);
    native_completion_test_reached = 0;
    native_completion_test_released = 0;
    if (pthread_mutex_unlock(&native_completion_test_mutex) != 0) {
        abort();
    }
}

void r_runtime_darwin_io_testing_wait_for_native_completion(void) {
    if (pthread_mutex_lock(&native_completion_test_mutex) != 0) {
        abort();
    }
    if (!native_completion_test_armed) {
        (void)pthread_mutex_unlock(&native_completion_test_mutex);
        abort();
    }
    while (!native_completion_test_reached) {
        if (pthread_cond_wait(&native_completion_test_condition, &native_completion_test_mutex) !=
            0) {
            (void)pthread_mutex_unlock(&native_completion_test_mutex);
            abort();
        }
    }
    if (pthread_mutex_unlock(&native_completion_test_mutex) != 0) {
        abort();
    }
}

void r_runtime_darwin_io_testing_release_native_completion(void) {
    if (pthread_mutex_lock(&native_completion_test_mutex) != 0) {
        abort();
    }
    if (!native_completion_test_armed || !native_completion_test_reached ||
        native_completion_test_released) {
        (void)pthread_mutex_unlock(&native_completion_test_mutex);
        abort();
    }
    native_completion_test_released = 1;
    if (pthread_cond_broadcast(&native_completion_test_condition) != 0) {
        (void)pthread_mutex_unlock(&native_completion_test_mutex);
        abort();
    }
    if (pthread_mutex_unlock(&native_completion_test_mutex) != 0) {
        abort();
    }
}

static void testing_pause_native_completion(void) {
    if (!atomic_load_explicit(&native_completion_test_hint, memory_order_acquire)) {
        return;
    }
    if (pthread_mutex_lock(&native_completion_test_mutex) != 0) {
        abort();
    }
    if (native_completion_test_armed) {
        native_completion_test_reached = 1;
        if (pthread_cond_broadcast(&native_completion_test_condition) != 0) {
            (void)pthread_mutex_unlock(&native_completion_test_mutex);
            abort();
        }
        while (!native_completion_test_released) {
            if (pthread_cond_wait(&native_completion_test_condition,
                                  &native_completion_test_mutex) != 0) {
                (void)pthread_mutex_unlock(&native_completion_test_mutex);
                abort();
            }
        }
        native_completion_test_armed = 0;
        atomic_store_explicit(&native_completion_test_hint, 0, memory_order_release);
        native_completion_test_reached = 0;
        native_completion_test_released = 0;
    }
    if (pthread_mutex_unlock(&native_completion_test_mutex) != 0) {
        abort();
    }
}

_Bool r_runtime_darwin_io_internal_testing_native_completion_pause_armed(void) {
    return atomic_load_explicit(&native_completion_test_hint, memory_order_acquire);
}
#else
static void testing_pause_native_completion(void) {
}

_Bool r_runtime_darwin_io_internal_testing_native_completion_pause_armed(void) {
    return 0;
}
#endif

void r_runtime_darwin_io_internal_testing_pause_native_completion(void) {
    testing_pause_native_completion();
}

#if defined(R_RUNTIME_DARWIN_IO_TESTING)
void r_runtime_darwin_io_internal_test_pause_arm(RRuntimeDarwinIoTestPause *pause) {
    if (pthread_mutex_lock(&pause->mutex) != 0) {
        abort();
    }
    if (pause->armed) {
        (void)pthread_mutex_unlock(&pause->mutex);
        abort();
    }
    pause->armed = 1;
    pause->reached = 0;
    pause->released = 0;
    atomic_store_explicit(&pause->hint, 1, memory_order_release);
    if (pthread_mutex_unlock(&pause->mutex) != 0) {
        abort();
    }
}

void r_runtime_darwin_io_internal_test_pause_wait(RRuntimeDarwinIoTestPause *pause) {
    if (pthread_mutex_lock(&pause->mutex) != 0) {
        abort();
    }
    if (!pause->armed) {
        (void)pthread_mutex_unlock(&pause->mutex);
        abort();
    }
    while (!pause->reached) {
        if (pthread_cond_wait(&pause->condition, &pause->mutex) != 0) {
            (void)pthread_mutex_unlock(&pause->mutex);
            abort();
        }
    }
    if (pthread_mutex_unlock(&pause->mutex) != 0) {
        abort();
    }
}

void r_runtime_darwin_io_internal_test_pause_release(RRuntimeDarwinIoTestPause *pause) {
    if (pthread_mutex_lock(&pause->mutex) != 0) {
        abort();
    }
    if (!pause->armed || !pause->reached || pause->released) {
        (void)pthread_mutex_unlock(&pause->mutex);
        abort();
    }
    pause->released = 1;
    if (pthread_cond_broadcast(&pause->condition) != 0) {
        (void)pthread_mutex_unlock(&pause->mutex);
        abort();
    }
    while (pause->armed) {
        if (pthread_cond_wait(&pause->condition, &pause->mutex) != 0) {
            (void)pthread_mutex_unlock(&pause->mutex);
            abort();
        }
    }
    if (pthread_mutex_unlock(&pause->mutex) != 0) {
        abort();
    }
}

void r_runtime_darwin_io_internal_test_pause_point(RRuntimeDarwinIoTestPause *pause) {
    if (!atomic_load_explicit(&pause->hint, memory_order_acquire)) {
        return;
    }
    if (pthread_mutex_lock(&pause->mutex) != 0) {
        abort();
    }
    if (pause->armed && !pause->reached) {
        pause->reached = 1;
        if (pthread_cond_broadcast(&pause->condition) != 0) {
            (void)pthread_mutex_unlock(&pause->mutex);
            abort();
        }
        while (!pause->released) {
            if (pthread_cond_wait(&pause->condition, &pause->mutex) != 0) {
                (void)pthread_mutex_unlock(&pause->mutex);
                abort();
            }
        }
        pause->armed = 0;
        pause->reached = 0;
        pause->released = 0;
        atomic_store_explicit(&pause->hint, 0, memory_order_release);
        if (pthread_cond_broadcast(&pause->condition) != 0) {
            (void)pthread_mutex_unlock(&pause->mutex);
            abort();
        }
    }
    if (pthread_mutex_unlock(&pause->mutex) != 0) {
        abort();
    }
}

static RRuntimeDarwinIoTestPause inline_registration_test_pause =
    R_RUNTIME_DARWIN_IO_TEST_PAUSE_INITIALIZER;

void r_runtime_darwin_io_testing_pause_next_inline_registration(void) {
    r_runtime_darwin_io_internal_test_pause_arm(&inline_registration_test_pause);
}

void r_runtime_darwin_io_testing_wait_for_inline_registration(void) {
    r_runtime_darwin_io_internal_test_pause_wait(&inline_registration_test_pause);
}

void r_runtime_darwin_io_testing_release_inline_registration(void) {
    r_runtime_darwin_io_internal_test_pause_release(&inline_registration_test_pause);
}

static void testing_pause_inline_registration(void) {
    r_runtime_darwin_io_internal_test_pause_point(&inline_registration_test_pause);
}
#else
static void testing_pause_inline_registration(void) {
}
#endif

static RRuntimeDarwinIoBuffer empty_buffer(void) {
    RRuntimeDarwinIoBuffer buffer;

    (void)memset(&buffer, 0, sizeof(buffer));
    return buffer;
}

static void completion_worker(void *context) {
    RRuntimeDarwinIoRequest *request = context;
    RRuntimeDarwinIoCompletionFn completion;
    void *completion_context;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    completion = request->completion;
    completion_context = request->completion_context;
    if (completion == NULL || !request->completion_scheduled ||
        request->state != R_RUNTIME_DARWIN_IO_REQUEST_TERMINAL) {
        (void)pthread_mutex_unlock(&request->mutex);
        abort();
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    completion(request, completion_context);
    r_runtime_darwin_io_internal_request_release(request);
}

void r_runtime_darwin_io_internal_schedule_completion(RRuntimeDarwinIoRequest *request) {
    _Bool schedule = 0;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (request->state == R_RUNTIME_DARWIN_IO_REQUEST_TERMINAL && request->completion != NULL &&
        !request->completion_scheduled) {
        if (request->references == SIZE_MAX) {
            (void)pthread_mutex_unlock(&request->mutex);
            abort();
        }
        request->references += 1U;
        request->completion_scheduled = 1;
        schedule = 1;
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    if (schedule) {
        dispatch_async_f(request->handle->callback_queue, request, completion_worker);
    }
}

RRuntimeDarwinIoSubmitResult
r_runtime_darwin_io_internal_submit_failure(RRuntimeDarwinIoStartStatus status, int native_error) {
    RRuntimeDarwinIoSubmitResult result;

    result.request = NULL;
    result.status = status;
    result.native_error = native_error;
    return result;
}

RRuntimeDarwinIoSubmitResult
r_runtime_darwin_io_internal_submit_success(RRuntimeDarwinIoRequest *request) {
    RRuntimeDarwinIoSubmitResult result;

    result.request = request;
    result.status = R_RUNTIME_DARWIN_IO_START_OK;
    result.native_error = 0;
    return result;
}

RRuntimeDarwinIoRequest *
r_runtime_darwin_io_internal_request_create_locked(RRuntimeDarwinIoHandle *handle,
                                                   RRuntimeDarwinIoOperation operation,
                                                   RRuntimeDarwinIoBuffer *buffer,
                                                   RRuntimeDarwinIoStartStatus *status,
                                                   int *native_error) {
    RRuntimeDarwinIoRequest *request = NULL;
    RRuntimeAllocationStatus allocation_status;
    int mutex_result;
    int condition_result;

    allocation_status = r_runtime_allocator_allocate(
        handle->allocator, sizeof(*request), _Alignof(RRuntimeDarwinIoRequest), (void **)&request);
    if (allocation_status != R_RUNTIME_ALLOCATION_OK) {
        *status = R_RUNTIME_DARWIN_IO_START_ALLOCATION_FAILED;
        *native_error = ENOMEM;
        return NULL;
    }
    (void)memset(request, 0, sizeof(*request));
    mutex_result = pthread_mutex_init(&request->mutex, NULL);
    if (mutex_result != 0) {
        r_runtime_allocator_deallocate(request, _Alignof(RRuntimeDarwinIoRequest));
        *status = R_RUNTIME_DARWIN_IO_START_NATIVE_RETAIN_FAILED;
        *native_error = mutex_result;
        return NULL;
    }
    condition_result = pthread_cond_init(&request->condition, NULL);
    if (condition_result != 0) {
        (void)pthread_mutex_destroy(&request->mutex);
        r_runtime_allocator_deallocate(request, _Alignof(RRuntimeDarwinIoRequest));
        *status = R_RUNTIME_DARWIN_IO_START_NATIVE_RETAIN_FAILED;
        *native_error = condition_result;
        return NULL;
    }
    request->references = 1U;
    request->handle = handle;
    request->operation = operation;
    request->state = R_RUNTIME_DARWIN_IO_REQUEST_ACTIVE;
    if (handle->references == SIZE_MAX || handle->request_count == SIZE_MAX) {
        (void)pthread_cond_destroy(&request->condition);
        (void)pthread_mutex_destroy(&request->mutex);
        r_runtime_allocator_deallocate(request, _Alignof(RRuntimeDarwinIoRequest));
        *status = R_RUNTIME_DARWIN_IO_START_NATIVE_RETAIN_FAILED;
        *native_error = EOVERFLOW;
        return NULL;
    }
    if (handle->next_submission_sequence == UINT64_MAX) {
        abort();
    }
    handle->next_submission_sequence += UINT64_C(1);
    request->submission_sequence = handle->next_submission_sequence;
    if (buffer != NULL) {
        request->buffer = *buffer;
        *buffer = empty_buffer();
    }
    handle->references += 1U;
    handle->request_count += 1U;
    request->next = handle->requests;
    handle->requests = request;
    *status = R_RUNTIME_DARWIN_IO_START_OK;
    *native_error = 0;
    return request;
}

void r_runtime_darwin_io_internal_request_retain(RRuntimeDarwinIoRequest *request) {
    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (request->references == SIZE_MAX) {
        (void)pthread_mutex_unlock(&request->mutex);
        abort();
    }
    request->references += 1U;
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
}

_Bool r_runtime_darwin_io_internal_handle_has_active_request_locked(
    RRuntimeDarwinIoHandle *handle) {
    RRuntimeDarwinIoRequest *request;

    for (request = handle->requests; request != NULL; request = request->next) {
        _Bool active;

        if (pthread_mutex_lock(&request->mutex) != 0) {
            abort();
        }
        active = request->state != R_RUNTIME_DARWIN_IO_REQUEST_TERMINAL;
        if (pthread_mutex_unlock(&request->mutex) != 0) {
            abort();
        }
        if (active) {
            return 1;
        }
    }
    return 0;
}

typedef enum RRuntimeDarwinIoDirection {
    R_RUNTIME_DARWIN_IO_DIRECTION_INPUT = 1,
    R_RUNTIME_DARWIN_IO_DIRECTION_OUTPUT
} RRuntimeDarwinIoDirection;

static _Bool request_has_direction(const RRuntimeDarwinIoRequest *request,
                                   RRuntimeDarwinIoDirection direction) {
    if (direction == R_RUNTIME_DARWIN_IO_DIRECTION_INPUT) {
        return request->operation == R_RUNTIME_DARWIN_IO_READ ||
               (request->operation == R_RUNTIME_DARWIN_IO_SHUTDOWN &&
                (request->shutdown_direction == R_RUNTIME_DARWIN_IO_SHUTDOWN_INPUT ||
                 request->shutdown_direction == R_RUNTIME_DARWIN_IO_SHUTDOWN_BOTH));
    }
    return request->operation == R_RUNTIME_DARWIN_IO_WRITE ||
           request->operation == R_RUNTIME_DARWIN_IO_FLUSH ||
           (request->operation == R_RUNTIME_DARWIN_IO_SHUTDOWN &&
            (request->shutdown_direction == R_RUNTIME_DARWIN_IO_SHUTDOWN_OUTPUT ||
             request->shutdown_direction == R_RUNTIME_DARWIN_IO_SHUTDOWN_BOTH));
}

static RRuntimeDarwinIoRequest *first_pending_request_locked(RRuntimeDarwinIoHandle *handle,
                                                             RRuntimeDarwinIoDirection direction) {
    RRuntimeDarwinIoRequest *first = NULL;
    RRuntimeDarwinIoRequest *request;

    for (request = handle->requests; request != NULL; request = request->next) {
        RRuntimeDarwinIoRequestState state;

        if (!request_has_direction(request, direction)) {
            continue;
        }
        if (pthread_mutex_lock(&request->mutex) != 0) {
            abort();
        }
        state = request->state;
        if (pthread_mutex_unlock(&request->mutex) != 0) {
            abort();
        }
        if (state != R_RUNTIME_DARWIN_IO_REQUEST_TERMINAL &&
            (first == NULL || request->submission_sequence < first->submission_sequence)) {
            first = request;
        }
    }
    return first;
}

static void schedule_direction_locked(RRuntimeDarwinIoHandle *handle,
                                      RRuntimeDarwinIoDirection direction,
                                      RRuntimeDarwinIoRequest **finished) {
    for (;;) {
        RRuntimeDarwinIoRequest *request = first_pending_request_locked(handle, direction);
        RRuntimeDarwinIoRequestState state;
        _Bool native_scheduled;
        _Bool activated;

        if (request == NULL) {
            return;
        }
        if (pthread_mutex_lock(&request->mutex) != 0) {
            abort();
        }
        state = request->state;
        native_scheduled = request->native_scheduled;
        if (pthread_mutex_unlock(&request->mutex) != 0) {
            abort();
        }
        if (state == R_RUNTIME_DARWIN_IO_REQUEST_PREPARED ||
            (state == R_RUNTIME_DARWIN_IO_REQUEST_ACTIVE && native_scheduled)) {
            return;
        }
        if (state == R_RUNTIME_DARWIN_IO_REQUEST_TERMINAL) {
            continue;
        }
        if (request->operation == R_RUNTIME_DARWIN_IO_SHUTDOWN &&
            request->shutdown_direction == R_RUNTIME_DARWIN_IO_SHUTDOWN_BOTH) {
            const RRuntimeDarwinIoDirection other_direction =
                direction == R_RUNTIME_DARWIN_IO_DIRECTION_INPUT
                    ? R_RUNTIME_DARWIN_IO_DIRECTION_OUTPUT
                    : R_RUNTIME_DARWIN_IO_DIRECTION_INPUT;

            if (first_pending_request_locked(handle, other_direction) != request) {
                return;
            }
        }
        switch (request->operation) {
        case R_RUNTIME_DARWIN_IO_READ:
            activated = handle->engine == R_RUNTIME_DARWIN_IO_ENGINE_DISPATCH
                            ? r_runtime_darwin_io_internal_activate_read(request)
                            : r_runtime_darwin_io_internal_direct_activate(request, finished);
            break;
        case R_RUNTIME_DARWIN_IO_WRITE:
            activated = handle->engine == R_RUNTIME_DARWIN_IO_ENGINE_DISPATCH
                            ? r_runtime_darwin_io_internal_activate_write(request)
                            : r_runtime_darwin_io_internal_direct_activate(request, finished);
            break;
        case R_RUNTIME_DARWIN_IO_FLUSH:
            activated = r_runtime_darwin_io_internal_activate_flush(request);
            break;
        case R_RUNTIME_DARWIN_IO_SHUTDOWN:
            activated = r_runtime_darwin_io_internal_activate_shutdown(request);
            break;
        case R_RUNTIME_DARWIN_IO_CLOSE:
            abort();
        }
        if (activated) {
            return;
        }
    }
}

/* A direct request whose transfer ended during activation is finished after the handle mutex is
   released, and scheduling repeats so the next same-direction request starts. */
void r_runtime_darwin_io_internal_schedule_ready_requests(RRuntimeDarwinIoHandle *handle) {
    for (;;) {
        RRuntimeDarwinIoRequest *finished_input = NULL;
        RRuntimeDarwinIoRequest *finished_output = NULL;

        if (pthread_mutex_lock(&handle->mutex) != 0) {
            abort();
        }
        schedule_direction_locked(handle, R_RUNTIME_DARWIN_IO_DIRECTION_INPUT, &finished_input);
        schedule_direction_locked(handle, R_RUNTIME_DARWIN_IO_DIRECTION_OUTPUT, &finished_output);
        if (pthread_mutex_unlock(&handle->mutex) != 0) {
            abort();
        }
        if (finished_input == NULL && finished_output == NULL) {
            return;
        }
        if (finished_input != NULL) {
            r_runtime_darwin_io_internal_direct_finish_activated(finished_input);
        }
        if (finished_output != NULL) {
            r_runtime_darwin_io_internal_direct_finish_activated(finished_output);
        }
    }
}

static void remove_request_from_handle(RRuntimeDarwinIoRequest *request) {
    RRuntimeDarwinIoHandle *handle = request->handle;
    RRuntimeDarwinIoRequest *previous = NULL;
    RRuntimeDarwinIoRequest *cursor;

    if (pthread_mutex_lock(&handle->mutex) != 0) {
        abort();
    }
    cursor = handle->requests;
    while (cursor != NULL && cursor != request) {
        previous = cursor;
        cursor = cursor->next;
    }
    if (cursor == NULL) {
        abort();
    }
    if (previous == NULL) {
        handle->requests = request->next;
    } else {
        previous->next = request->next;
    }
    request->next = NULL;
    if (pthread_mutex_unlock(&handle->mutex) != 0) {
        abort();
    }
}

void r_runtime_darwin_io_internal_request_release(RRuntimeDarwinIoRequest *request) {
    RRuntimeDarwinIoHandle *handle;
    _Bool destroy;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (request->references == 0U) {
        abort();
    }
    request->references -= 1U;
    destroy = request->references == 0U;
    if (destroy &&
        (request->operation_channel != NULL || request->deadline_timer != NULL ||
         request->prepared_write_data != NULL || request->stream_position_barrier_pending ||
         request->state != R_RUNTIME_DARWIN_IO_REQUEST_TERMINAL)) {
        abort();
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    if (!destroy) {
        return;
    }
    handle = request->handle;
    remove_request_from_handle(request);
    r_runtime_darwin_io_buffer_release(&request->buffer);
    (void)pthread_cond_destroy(&request->condition);
    (void)pthread_mutex_destroy(&request->mutex);
    r_runtime_allocator_deallocate(request, _Alignof(RRuntimeDarwinIoRequest));
    r_runtime_darwin_io_internal_handle_release_request(handle);
}

void r_runtime_darwin_io_internal_request_abort_start(RRuntimeDarwinIoRequest *request,
                                                      RRuntimeDarwinIoBuffer *buffer) {
    RRuntimeDarwinIoHandle *handle = request->handle;

    r_runtime_darwin_io_internal_disarm_deadline(request);
    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (request->operation_channel != NULL) {
        abort();
    }
    if (buffer != NULL) {
        *buffer = request->buffer;
        request->buffer = empty_buffer();
    }
    request->operation_done = 1;
    request->cleanup_done = 1;
    request->terminal_event = R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE;
    request->state = R_RUNTIME_DARWIN_IO_REQUEST_TERMINAL;
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    r_runtime_darwin_io_internal_handle_notify_request_terminal(handle);
    r_runtime_darwin_io_internal_schedule_ready_requests(handle);
    r_runtime_darwin_io_internal_request_release(request);
}

static _Bool request_has_committed_locked(const RRuntimeDarwinIoRequest *request) {
    if (request->operation_error != 0) {
        return 0;
    }
    switch (request->operation) {
    case R_RUNTIME_DARWIN_IO_READ:
    case R_RUNTIME_DARWIN_IO_WRITE:
        return request->bytes_transferred == request->requested_size;
    case R_RUNTIME_DARWIN_IO_FLUSH:
        return request->pending_event == 0 && request->operation_done;
    case R_RUNTIME_DARWIN_IO_SHUTDOWN:
        return request->shutdown_committed;
    case R_RUNTIME_DARWIN_IO_CLOSE:
        return 0;
    }
}

static _Bool request_finalize_locked(RRuntimeDarwinIoRequest *request) {
    RRuntimeDarwinIoTerminalEvent event = R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE;

    if (!request->operation_done || !request->cleanup_done ||
        request->state == R_RUNTIME_DARWIN_IO_REQUEST_TERMINAL) {
        return 0;
    }
    if (request->pending_event != 0 &&
        (request->native_event_sequence == UINT64_C(0) ||
         request->pending_event_sequence < request->native_event_sequence) &&
        !request_has_committed_locked(request)) {
        event = request->pending_event;
    }
    request->terminal_event = event;
    if (event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE) {
        if (request->operation_error == 0) {
            request->operation_error = request->cleanup_error;
        }
        request->terminal_event_sequence = request->native_event_sequence;
    } else {
        request->operation_error = 0;
        request->terminal_event_sequence = request->pending_event_sequence;
    }
    request->partial =
        request->bytes_transferred != 0U &&
        (request->bytes_remaining != 0U || request->eof || request->operation_error != 0 ||
         event != R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE);
    request->state = R_RUNTIME_DARWIN_IO_REQUEST_TERMINAL;
    (void)pthread_cond_broadcast(&request->condition);
    return 1;
}

_Bool r_runtime_darwin_io_internal_request_finalize_locked(RRuntimeDarwinIoRequest *request) {
    return request_finalize_locked(request);
}

void r_runtime_darwin_io_internal_progress(RRuntimeDarwinIoRequest *request,
                                           size_t bytes_transferred,
                                           size_t bytes_remaining) {
    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (bytes_transferred < request->bytes_transferred ||
        bytes_transferred > request->requested_size || bytes_remaining > request->requested_size) {
        abort();
    }
    request->bytes_transferred = bytes_transferred;
    request->bytes_remaining = bytes_remaining;
    (void)pthread_cond_broadcast(&request->condition);
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
}

void r_runtime_darwin_io_internal_operation_done(RRuntimeDarwinIoRequest *request,
                                                 size_t bytes_transferred,
                                                 size_t bytes_remaining,
                                                 int native_error,
                                                 _Bool eof) {
    _Bool finalized;
    uint64_t native_event_sequence = r_runtime_darwin_event_sequence_next();

    testing_pause_native_completion();

    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (request->operation_done || bytes_transferred < request->bytes_transferred ||
        bytes_transferred > request->requested_size || bytes_remaining > request->requested_size) {
        abort();
    }
    request->bytes_transferred = bytes_transferred;
    request->bytes_remaining = bytes_remaining;
    request->operation_error = native_error;
    request->operation_native_error = native_error;
    request->native_event_sequence = native_event_sequence;
    request->eof = eof;
    request->operation_done = 1;
    /* The final Dispatch I/O handler is the native completion/cancellation acknowledgement. */
    request->cleanup_done = 1;
    finalized = request_finalize_locked(request);
    (void)pthread_cond_broadcast(&request->condition);
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    if (finalized) {
        r_runtime_darwin_io_internal_handle_notify_request_terminal(request->handle);
    }
    r_runtime_darwin_io_internal_close_operation_channel(request, 0);
    if (finalized) {
        r_runtime_darwin_io_internal_disarm_deadline(request);
        r_runtime_darwin_io_internal_schedule_completion(request);
    }
    r_runtime_darwin_io_internal_schedule_ready_requests(request->handle);
    r_runtime_darwin_io_internal_try_complete_console_closes(request->handle);
    r_runtime_darwin_io_internal_request_release(request);
}

void r_runtime_darwin_io_internal_close_done(RRuntimeDarwinIoRequest *request, int native_error) {
    _Bool finalized;
    uint64_t native_event_sequence = r_runtime_darwin_event_sequence_next();

    testing_pause_native_completion();

    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (request->operation_done || request->cleanup_done) {
        abort();
    }
    request->operation_done = 1;
    request->cleanup_done = 1;
    request->operation_native_error = native_error;
    request->cleanup_error = native_error;
    request->native_event_sequence = native_event_sequence;
    finalized = request_finalize_locked(request);
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    if (!finalized) {
        abort();
    }
    r_runtime_darwin_io_internal_handle_notify_request_terminal(request->handle);
    r_runtime_darwin_io_internal_disarm_deadline(request);
    r_runtime_darwin_io_internal_schedule_completion(request);
}

static _Bool request_signal(RRuntimeDarwinIoRequest *request,
                            RRuntimeDarwinIoTerminalEvent event,
                            _Bool include_prepared) {
    _Bool accepted = 0;
    _Bool finalized_without_submission = 0;
    _Bool direct = request->handle->engine != R_RUNTIME_DARWIN_IO_ENGINE_DISPATCH;
    dispatch_data_t prepared_write_data = NULL;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (request->operation == R_RUNTIME_DARWIN_IO_CLOSE &&
        request->state == R_RUNTIME_DARWIN_IO_REQUEST_ACTIVE && request->pending_event == 0 &&
        !request->operation_done && event == R_RUNTIME_DARWIN_IO_TERMINAL_TIMED_OUT) {
        request->pending_event = event;
        request->pending_event_sequence = r_runtime_darwin_event_sequence_next();
        accepted = 1;
    } else if (request->operation != R_RUNTIME_DARWIN_IO_CLOSE &&
               request->state == R_RUNTIME_DARWIN_IO_REQUEST_PREPARED &&
               request->pending_event == 0 && event == R_RUNTIME_DARWIN_IO_TERMINAL_CANCELLED &&
               include_prepared) {
        request->pending_event = event;
        request->pending_event_sequence = r_runtime_darwin_event_sequence_next();
        accepted = 1;
    } else if (request->operation != R_RUNTIME_DARWIN_IO_CLOSE &&
               request->state == R_RUNTIME_DARWIN_IO_REQUEST_ACTIVE &&
               request->pending_event == 0 && !request->operation_done) {
        request->pending_event = event;
        request->pending_event_sequence = r_runtime_darwin_event_sequence_next();
        accepted = 1;
        if (!request->native_scheduled) {
            prepared_write_data = request->prepared_write_data;
            request->prepared_write_data = NULL;
            request->operation_done = 1;
            request->cleanup_done = 1;
            finalized_without_submission = request_finalize_locked(request);
        }
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    if (accepted) {
        if (finalized_without_submission) {
            r_runtime_darwin_io_internal_handle_notify_request_terminal(request->handle);
            if (prepared_write_data != NULL) {
                dispatch_release(prepared_write_data);
            }
            r_runtime_darwin_io_internal_close_operation_channel(request, 0);
            r_runtime_darwin_io_internal_disarm_deadline(request);
            r_runtime_darwin_io_internal_schedule_completion(request);
            r_runtime_darwin_io_internal_schedule_ready_requests(request->handle);
            r_runtime_darwin_io_internal_try_complete_console_closes(request->handle);
        } else if (direct && (request->operation == R_RUNTIME_DARWIN_IO_READ ||
                              request->operation == R_RUNTIME_DARWIN_IO_WRITE)) {
            r_runtime_darwin_io_internal_direct_signal(request);
        } else if (request->operation == R_RUNTIME_DARWIN_IO_READ ||
                   request->operation == R_RUNTIME_DARWIN_IO_WRITE) {
            r_runtime_darwin_io_internal_close_operation_channel(request, DISPATCH_IO_STOP);
        }
    }
    return accepted;
}

void r_runtime_darwin_io_internal_request_complete_without_submission(
    RRuntimeDarwinIoRequest *request) {
    dispatch_data_t prepared_write_data;
    _Bool finalized;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (request->state != R_RUNTIME_DARWIN_IO_REQUEST_ACTIVE || request->pending_event == 0 ||
        request->native_scheduled || request->operation_done) {
        (void)pthread_mutex_unlock(&request->mutex);
        abort();
    }
    prepared_write_data = request->prepared_write_data;
    request->prepared_write_data = NULL;
    request->operation_done = 1;
    request->cleanup_done = 1;
    finalized = request_finalize_locked(request);
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    if (!finalized) {
        abort();
    }
    r_runtime_darwin_io_internal_handle_notify_request_terminal(request->handle);
    if (prepared_write_data != NULL) {
        dispatch_release(prepared_write_data);
    }
    r_runtime_darwin_io_internal_close_operation_channel(request, 0);
    r_runtime_darwin_io_internal_disarm_deadline(request);
    r_runtime_darwin_io_internal_schedule_completion(request);
    r_runtime_darwin_io_internal_schedule_ready_requests(request->handle);
    r_runtime_darwin_io_internal_try_complete_console_closes(request->handle);
}

_Bool r_runtime_darwin_io_request_cancel(RRuntimeDarwinIoRequest *request) {
    return request_signal(request, R_RUNTIME_DARWIN_IO_TERMINAL_CANCELLED, 0);
}

_Bool r_runtime_darwin_io_request_deadline_expired(RRuntimeDarwinIoRequest *request) {
    return request_signal(request, R_RUNTIME_DARWIN_IO_TERMINAL_TIMED_OUT, 0);
}

_Bool r_runtime_darwin_io_internal_request_cancel_for_close(RRuntimeDarwinIoRequest *request) {
    return request_signal(request, R_RUNTIME_DARWIN_IO_TERMINAL_CANCELLED, 1);
}

RRuntimeDarwinIoRequestState r_runtime_darwin_io_request_state(RRuntimeDarwinIoRequest *request) {
    RRuntimeDarwinIoRequestState state;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    state = request->state;
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    return state;
}

size_t r_runtime_darwin_io_request_progress(RRuntimeDarwinIoRequest *request) {
    size_t progress;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    progress = request->bytes_transferred;
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    return progress;
}

RRuntimeDarwinIoResult r_runtime_darwin_io_request_wait(RRuntimeDarwinIoRequest *request) {
    RRuntimeDarwinIoResult result;

    (void)memset(&result, 0, sizeof(result));
    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    while (request->state != R_RUNTIME_DARWIN_IO_REQUEST_TERMINAL) {
        (void)pthread_cond_wait(&request->condition, &request->mutex);
    }
    result.operation = request->operation;
    result.terminal_event = request->terminal_event;
    result.native_error = request->operation_error;
    result.operation_native_error = request->operation_native_error;
    result.terminal_event_sequence = request->terminal_event_sequence;
    result.native_event_sequence = request->native_event_sequence;
    result.bytes_transferred = request->bytes_transferred;
    result.bytes_remaining = request->bytes_remaining;
    result.eof = request->eof;
    result.partial = request->partial;
    result.cleanup_acknowledged = request->cleanup_done;
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    return result;
}

_Bool r_runtime_darwin_io_internal_deliver_completion(RRuntimeDarwinIoRequest *request) {
    RRuntimeDarwinIoCompletionFn completion = NULL;
    void *completion_context = NULL;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (request->state == R_RUNTIME_DARWIN_IO_REQUEST_TERMINAL && request->completion != NULL &&
        !request->completion_scheduled) {
        if (request->references == SIZE_MAX) {
            (void)pthread_mutex_unlock(&request->mutex);
            abort();
        }
        request->references += 1U;
        request->completion_scheduled = 1;
        completion = request->completion;
        completion_context = request->completion_context;
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    if (completion == NULL) {
        return 0;
    }
    completion(request, completion_context);
    r_runtime_darwin_io_internal_request_release(request);
    return 1;
}

_Bool r_runtime_darwin_io_request_set_completion_inline(RRuntimeDarwinIoRequest *request,
                                                        RRuntimeDarwinIoCompletionFn completion,
                                                        void *context) {
    _Bool accepted = 0;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    /* Once registered, the completion may run on a native thread and release the caller's
       reference, so the delivery attempt below holds a reference of its own. */
    if (request->completion == NULL && !request->completion_scheduled) {
        if (request->references == SIZE_MAX) {
            (void)pthread_mutex_unlock(&request->mutex);
            abort();
        }
        request->references += 1U;
        request->completion = completion;
        request->completion_context = context;
        accepted = 1;
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    if (accepted) {
        testing_pause_inline_registration();
        (void)r_runtime_darwin_io_internal_deliver_completion(request);
        r_runtime_darwin_io_internal_request_release(request);
    }
    return accepted;
}

_Bool r_runtime_darwin_io_request_set_completion(RRuntimeDarwinIoRequest *request,
                                                 RRuntimeDarwinIoCompletionFn completion,
                                                 void *context) {
    _Bool accepted = 0;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (request->completion == NULL && !request->completion_scheduled) {
        if (request->references == SIZE_MAX) {
            (void)pthread_mutex_unlock(&request->mutex);
            abort();
        }
        request->references += 1U;
        request->completion = completion;
        request->completion_context = context;
        accepted = 1;
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    if (accepted) {
        r_runtime_darwin_io_internal_schedule_completion(request);
        r_runtime_darwin_io_internal_request_release(request);
    }
    return accepted;
}

RRuntimeDarwinIoBuffer r_runtime_darwin_io_request_take_buffer(RRuntimeDarwinIoRequest *request) {
    RRuntimeDarwinIoBuffer buffer = empty_buffer();

    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (request->state == R_RUNTIME_DARWIN_IO_REQUEST_TERMINAL &&
        (request->operation == R_RUNTIME_DARWIN_IO_READ ||
         request->operation == R_RUNTIME_DARWIN_IO_WRITE)) {
        buffer = request->buffer;
        request->buffer = empty_buffer();
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    return buffer;
}

void r_runtime_darwin_io_request_release(RRuntimeDarwinIoRequest *request) {
    if (request == NULL) {
        return;
    }
    (void)r_runtime_darwin_io_request_cancel(request);
    r_runtime_darwin_io_internal_request_release(request);
}

#if defined(R_RUNTIME_DARWIN_IO_TESTING)
void r_runtime_darwin_io_request_testing_wait_for_progress(RRuntimeDarwinIoRequest *request,
                                                           size_t minimum_progress) {
    if (request == NULL) {
        return;
    }
    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    while (request->bytes_transferred < minimum_progress &&
           request->state != R_RUNTIME_DARWIN_IO_REQUEST_TERMINAL) {
        (void)pthread_cond_wait(&request->condition, &request->mutex);
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
}
#endif

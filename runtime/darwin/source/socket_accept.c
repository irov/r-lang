#include "r_runtime_darwin_socket.h"

#include "r_runtime_darwin_event.h"

#include <dispatch/dispatch.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef enum RRuntimeDarwinSocketAcceptSourceKind {
    R_RUNTIME_DARWIN_SOCKET_ACCEPT_SOURCE_READ = 0,
    R_RUNTIME_DARWIN_SOCKET_ACCEPT_SOURCE_TIMER
} RRuntimeDarwinSocketAcceptSourceKind;

typedef struct RRuntimeDarwinSocketAcceptSourceContext {
    RRuntimeDarwinSocketAccept *request;
    RRuntimeDarwinSocketAcceptSourceKind kind;
} RRuntimeDarwinSocketAcceptSourceContext;

struct RRuntimeDarwinSocketAccept {
    RRuntimeAllocator *allocator;
    pthread_mutex_t mutex;
    int listener_descriptor;
    int accepted_descriptor;
    dispatch_source_t read_source;
    dispatch_source_t timer_source;
    RRuntimeDarwinSocketAcceptSourceContext read_context;
    RRuntimeDarwinSocketAcceptSourceContext timer_context;
    RRuntimeDarwinSocketAcceptCompletionFn completion;
    void *completion_context;
    RRuntimeDarwinSocketAcceptResult result;
    size_t pending_cancel_handlers;
    _Bool read_activated;
    _Bool timer_activated;
    _Bool terminal_selected;
    _Bool completion_delivered;
    _Bool descriptor_taken;
};

static RRuntimeDarwinSocketAcceptPrepareResult
accept_prepare_result(RRuntimeDarwinSocketAccept *request,
                      RRuntimeDarwinSocketPrepareStatus status,
                      int native_error) {
    return (RRuntimeDarwinSocketAcceptPrepareResult){request, status, native_error};
}

static void socket_accept_close_result_locked(RRuntimeDarwinSocketAccept *request) {
    if (request->accepted_descriptor >= 0) {
        (void)close(request->accepted_descriptor);
        request->accepted_descriptor = -1;
    }
}

static void socket_accept_activate_read_locked(RRuntimeDarwinSocketAccept *request) {
    if (!request->read_activated) {
        request->read_activated = 1;
        dispatch_activate(request->read_source);
    }
}

static void socket_accept_activate_timer_locked(RRuntimeDarwinSocketAccept *request) {
    if (request->timer_source != NULL && !request->timer_activated) {
        request->timer_activated = 1;
        dispatch_activate(request->timer_source);
    }
}

static void socket_accept_cancel_sources_locked(RRuntimeDarwinSocketAccept *request) {
    socket_accept_activate_read_locked(request);
    socket_accept_activate_timer_locked(request);
    if (request->read_source != NULL) {
        dispatch_source_cancel(request->read_source);
    }
    if (request->timer_source != NULL) {
        dispatch_source_cancel(request->timer_source);
    }
}

static _Bool socket_accept_select_locked(RRuntimeDarwinSocketAccept *request,
                                         RRuntimeDarwinSocketTerminalEvent event,
                                         uint64_t event_sequence,
                                         int native_error,
                                         const struct sockaddr_storage *peer,
                                         socklen_t peer_length,
                                         _Bool accepted) {
    if (event_sequence == UINT64_C(0)) {
        abort();
    }
    if (request->completion_delivered) {
        return 0;
    }
    if (request->terminal_selected && event_sequence >= request->result.terminal_event_sequence) {
        return 0;
    }
    if (request->terminal_selected && request->result.accepted) {
        socket_accept_close_result_locked(request);
    }
    request->terminal_selected = 1;
    request->result = (RRuntimeDarwinSocketAcceptResult){0};
    request->result.terminal_event = event;
    request->result.native_error = native_error;
    request->result.terminal_event_sequence = event_sequence;
    request->result.accepted = accepted;
    if (accepted) {
        if (peer == NULL || (size_t)peer_length > sizeof(request->result.peer)) {
            abort();
        }
        request->result.peer = *peer;
        request->result.peer_length = peer_length;
    }
    socket_accept_cancel_sources_locked(request);
    return 1;
}

static _Bool socket_accept_configure_descriptor(int descriptor, int *native_error) {
    int descriptor_flags;
    int status_flags;
#if defined(SO_NOSIGPIPE)
    const int enabled = 1;
#endif

    descriptor_flags = fcntl(descriptor, F_GETFD);
    if (descriptor_flags < 0) {
        *native_error = errno;
        return 0;
    }
    status_flags = fcntl(descriptor, F_GETFL);
    if (status_flags < 0) {
        *native_error = errno;
        return 0;
    }
    if (fcntl(descriptor, F_SETFD, descriptor_flags | FD_CLOEXEC) != 0 ||
        fcntl(descriptor, F_SETFL, status_flags | O_NONBLOCK) != 0) {
        *native_error = errno;
        return 0;
    }
#if defined(SO_NOSIGPIPE)
    if (setsockopt(descriptor, SOL_SOCKET, SO_NOSIGPIPE, &enabled, (socklen_t)sizeof(enabled)) !=
        0) {
        *native_error = errno;
        return 0;
    }
#endif
    return 1;
}

static void socket_accept_event(void *context_pointer) {
    RRuntimeDarwinSocketAcceptSourceContext *context = context_pointer;
    RRuntimeDarwinSocketAccept *request = context->request;
    struct sockaddr_storage peer = {0};
    socklen_t peer_length = (socklen_t)sizeof(peer);
    uint64_t event_sequence;
    int accepted_descriptor;
    int native_error;

    if (context->kind != R_RUNTIME_DARWIN_SOCKET_ACCEPT_SOURCE_READ) {
        abort();
    }
    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (request->terminal_selected) {
        if (pthread_mutex_unlock(&request->mutex) != 0) {
            abort();
        }
        return;
    }
    event_sequence = r_runtime_darwin_event_sequence_next();
    errno = 0;
    accepted_descriptor =
        accept(request->listener_descriptor, (struct sockaddr *)&peer, &peer_length);
    native_error = accepted_descriptor < 0 ? errno : 0;
    if (accepted_descriptor < 0 &&
        (native_error == EAGAIN || native_error == EWOULDBLOCK || native_error == EINTR)) {
        if (pthread_mutex_unlock(&request->mutex) != 0) {
            abort();
        }
        return;
    }
    if (accepted_descriptor >= 0 &&
        !socket_accept_configure_descriptor(accepted_descriptor, &native_error)) {
        (void)close(accepted_descriptor);
        accepted_descriptor = -1;
    }
    if (accepted_descriptor >= 0) {
        request->accepted_descriptor = accepted_descriptor;
        (void)socket_accept_select_locked(request,
                                          R_RUNTIME_DARWIN_SOCKET_TERMINAL_NATIVE,
                                          event_sequence,
                                          0,
                                          &peer,
                                          peer_length,
                                          1);
    } else {
        (void)socket_accept_select_locked(request,
                                          R_RUNTIME_DARWIN_SOCKET_TERMINAL_NATIVE,
                                          event_sequence,
                                          native_error,
                                          NULL,
                                          0U,
                                          0);
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
}

static void socket_accept_timer(void *context_pointer) {
    RRuntimeDarwinSocketAcceptSourceContext *context = context_pointer;
    RRuntimeDarwinSocketAccept *request = context->request;
    uint64_t event_sequence;

    if (context->kind != R_RUNTIME_DARWIN_SOCKET_ACCEPT_SOURCE_TIMER) {
        abort();
    }
    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    event_sequence = r_runtime_darwin_event_sequence_next();
    (void)socket_accept_select_locked(
        request, R_RUNTIME_DARWIN_SOCKET_TERMINAL_TIMED_OUT, event_sequence, 0, NULL, 0U, 0);
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
}

static void socket_accept_source_cancelled(void *context_pointer) {
    RRuntimeDarwinSocketAcceptSourceContext *context = context_pointer;
    RRuntimeDarwinSocketAccept *request = context->request;
    RRuntimeDarwinSocketAcceptCompletionFn completion = NULL;
    void *completion_context = NULL;
    dispatch_source_t source = NULL;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (context->kind == R_RUNTIME_DARWIN_SOCKET_ACCEPT_SOURCE_READ) {
        source = request->read_source;
        request->read_source = NULL;
    } else if (context->kind == R_RUNTIME_DARWIN_SOCKET_ACCEPT_SOURCE_TIMER) {
        source = request->timer_source;
        request->timer_source = NULL;
    } else {
        (void)pthread_mutex_unlock(&request->mutex);
        abort();
    }
    if (source == NULL || request->pending_cancel_handlers == 0U) {
        (void)pthread_mutex_unlock(&request->mutex);
        abort();
    }
    request->pending_cancel_handlers -= 1U;
    if (request->pending_cancel_handlers == 0U) {
        if (!request->terminal_selected || request->completion == NULL ||
            request->completion_delivered) {
            (void)pthread_mutex_unlock(&request->mutex);
            abort();
        }
        if (!request->result.accepted) {
            socket_accept_close_result_locked(request);
        }
        request->completion_delivered = 1;
        completion = request->completion;
        completion_context = request->completion_context;
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    dispatch_release(source);
    if (completion != NULL) {
        completion(request, completion_context);
    }
}

static void socket_accept_dispose_unsubmitted(RRuntimeDarwinSocketAccept *request) {
    dispatch_source_t read_source;
    dispatch_source_t timer_source;

    if (request == NULL) {
        return;
    }
    read_source = request->read_source;
    timer_source = request->timer_source;
    request->read_source = NULL;
    request->timer_source = NULL;
    socket_accept_close_result_locked(request);
    if (read_source != NULL) {
        dispatch_source_set_event_handler_f(read_source, NULL);
        dispatch_source_set_cancel_handler_f(read_source, NULL);
        dispatch_set_context(read_source, NULL);
        dispatch_source_cancel(read_source);
        dispatch_activate(read_source);
        dispatch_release(read_source);
    }
    if (timer_source != NULL) {
        dispatch_source_set_event_handler_f(timer_source, NULL);
        dispatch_source_set_cancel_handler_f(timer_source, NULL);
        dispatch_set_context(timer_source, NULL);
        dispatch_source_cancel(timer_source);
        dispatch_activate(timer_source);
        dispatch_release(timer_source);
    }
    if (pthread_mutex_destroy(&request->mutex) != 0) {
        abort();
    }
    r_runtime_allocator_deallocate(request, _Alignof(RRuntimeDarwinSocketAccept));
}

RRuntimeDarwinSocketAcceptPrepareResult r_runtime_darwin_socket_accept_prepare(
    RRuntimeAllocator *allocator, int listener_descriptor, uint64_t timeout_nanoseconds) {
    RRuntimeDarwinSocketAccept *request = NULL;
    dispatch_queue_t queue;
    dispatch_time_t timer_deadline;
    socklen_t option_length;
    int descriptor_flags;
    int status_flags;
    int socket_type = 0;

    if (listener_descriptor < 0 || timeout_nanoseconds > (uint64_t)INT64_MAX) {
        return accept_prepare_result(NULL, R_RUNTIME_DARWIN_SOCKET_PREPARE_INVALID, 0);
    }
    errno = 0;
    descriptor_flags = fcntl(listener_descriptor, F_GETFD);
    if (descriptor_flags < 0) {
        return accept_prepare_result(NULL, R_RUNTIME_DARWIN_SOCKET_PREPARE_NATIVE_FAILED, errno);
    }
    status_flags = fcntl(listener_descriptor, F_GETFL);
    if (status_flags < 0) {
        return accept_prepare_result(NULL, R_RUNTIME_DARWIN_SOCKET_PREPARE_NATIVE_FAILED, errno);
    }
    option_length = (socklen_t)sizeof(socket_type);
    if (getsockopt(listener_descriptor, SOL_SOCKET, SO_TYPE, &socket_type, &option_length) != 0) {
        return accept_prepare_result(NULL, R_RUNTIME_DARWIN_SOCKET_PREPARE_NATIVE_FAILED, errno);
    }
    if ((descriptor_flags & FD_CLOEXEC) == 0 || (status_flags & O_NONBLOCK) == 0 ||
        socket_type != SOCK_STREAM) {
        return accept_prepare_result(NULL, R_RUNTIME_DARWIN_SOCKET_PREPARE_INVALID, 0);
    }
    if (r_runtime_allocator_allocate(
            allocator, sizeof(*request), _Alignof(RRuntimeDarwinSocketAccept), (void **)&request) !=
        R_RUNTIME_ALLOCATION_OK) {
        return accept_prepare_result(
            NULL, R_RUNTIME_DARWIN_SOCKET_PREPARE_ALLOCATION_FAILED, ENOMEM);
    }
    (void)memset(request, 0, sizeof(*request));
    request->allocator = allocator;
    request->listener_descriptor = listener_descriptor;
    request->accepted_descriptor = -1;
    if (pthread_mutex_init(&request->mutex, NULL) != 0) {
        r_runtime_allocator_deallocate(request, _Alignof(RRuntimeDarwinSocketAccept));
        return accept_prepare_result(
            NULL, R_RUNTIME_DARWIN_SOCKET_PREPARE_ALLOCATION_FAILED, ENOMEM);
    }
    queue = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0U);
    if (queue == NULL) {
        socket_accept_dispose_unsubmitted(request);
        return accept_prepare_result(
            NULL, R_RUNTIME_DARWIN_SOCKET_PREPARE_ALLOCATION_FAILED, ENOMEM);
    }
    request->read_source = dispatch_source_create(
        DISPATCH_SOURCE_TYPE_READ, (uintptr_t)listener_descriptor, 0U, queue);
    if (request->read_source == NULL) {
        socket_accept_dispose_unsubmitted(request);
        return accept_prepare_result(
            NULL, R_RUNTIME_DARWIN_SOCKET_PREPARE_ALLOCATION_FAILED, ENOMEM);
    }
    request->read_context = (RRuntimeDarwinSocketAcceptSourceContext){
        request, R_RUNTIME_DARWIN_SOCKET_ACCEPT_SOURCE_READ};
    dispatch_set_context(request->read_source, &request->read_context);
    dispatch_source_set_event_handler_f(request->read_source, socket_accept_event);
    dispatch_source_set_cancel_handler_f(request->read_source, socket_accept_source_cancelled);
    request->pending_cancel_handlers = 1U;
    if (timeout_nanoseconds != 0U) {
        timer_deadline = dispatch_time(DISPATCH_TIME_NOW, (int64_t)timeout_nanoseconds);
        if (timer_deadline == DISPATCH_TIME_FOREVER) {
            socket_accept_dispose_unsubmitted(request);
            return accept_prepare_result(NULL, R_RUNTIME_DARWIN_SOCKET_PREPARE_INVALID, 0);
        }
        request->timer_source =
            dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0U, DISPATCH_TIMER_STRICT, queue);
        if (request->timer_source == NULL) {
            socket_accept_dispose_unsubmitted(request);
            return accept_prepare_result(
                NULL, R_RUNTIME_DARWIN_SOCKET_PREPARE_ALLOCATION_FAILED, ENOMEM);
        }
        request->timer_context = (RRuntimeDarwinSocketAcceptSourceContext){
            request, R_RUNTIME_DARWIN_SOCKET_ACCEPT_SOURCE_TIMER};
        dispatch_set_context(request->timer_source, &request->timer_context);
        dispatch_source_set_event_handler_f(request->timer_source, socket_accept_timer);
        dispatch_source_set_cancel_handler_f(request->timer_source, socket_accept_source_cancelled);
        dispatch_source_set_timer(request->timer_source, timer_deadline, DISPATCH_TIME_FOREVER, 0U);
        request->pending_cancel_handlers += 1U;
    }
    return accept_prepare_result(request, R_RUNTIME_DARWIN_SOCKET_PREPARE_OK, 0);
}

_Bool r_runtime_darwin_socket_accept_bind(RRuntimeDarwinSocketAccept *request,
                                          RRuntimeDarwinSocketAcceptCompletionFn completion,
                                          void *context) {
    _Bool bound = 0;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        return 0;
    }
    if (!request->read_activated && !request->timer_activated && !request->terminal_selected &&
        request->completion == NULL && request->listener_descriptor >= 0 &&
        request->read_source != NULL) {
        request->completion = completion;
        request->completion_context = context;
        bound = 1;
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    return bound;
}

_Bool r_runtime_darwin_socket_accept_arm_deadline(RRuntimeDarwinSocketAccept *request) {
    if (pthread_mutex_lock(&request->mutex) != 0) {
        return 0;
    }
    if (request->terminal_selected || request->completion == NULL ||
        request->listener_descriptor < 0 || request->read_source == NULL) {
        (void)pthread_mutex_unlock(&request->mutex);
        return 0;
    }
    socket_accept_activate_timer_locked(request);
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    return 1;
}

_Bool r_runtime_darwin_socket_accept_activate(RRuntimeDarwinSocketAccept *request) {
    if (pthread_mutex_lock(&request->mutex) != 0) {
        return 0;
    }
    if (request->terminal_selected || request->read_activated) {
        if (pthread_mutex_unlock(&request->mutex) != 0) {
            abort();
        }
        return 1;
    }
    if (request->completion == NULL || request->listener_descriptor < 0 ||
        request->read_source == NULL) {
        (void)pthread_mutex_unlock(&request->mutex);
        return 0;
    }
    socket_accept_activate_timer_locked(request);
    socket_accept_activate_read_locked(request);
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    return 1;
}

_Bool r_runtime_darwin_socket_accept_cancel(RRuntimeDarwinSocketAccept *request,
                                            uint64_t cancellation_sequence) {
    _Bool selected = 0;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        return 0;
    }
    selected = socket_accept_select_locked(
        request, R_RUNTIME_DARWIN_SOCKET_TERMINAL_CANCELLED, cancellation_sequence, 0, NULL, 0U, 0);
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    return selected;
}

RRuntimeDarwinSocketAcceptResult
r_runtime_darwin_socket_accept_result(RRuntimeDarwinSocketAccept *request) {
    RRuntimeDarwinSocketAcceptResult result = {0};

    if (pthread_mutex_lock(&request->mutex) != 0) {
        return result;
    }
    if (request->completion_delivered) {
        result = request->result;
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    return result;
}

int r_runtime_darwin_socket_accept_take_descriptor(RRuntimeDarwinSocketAccept *request) {
    int descriptor = -1;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        return -1;
    }
    if (request->completion_delivered && request->result.accepted && !request->descriptor_taken &&
        request->accepted_descriptor >= 0) {
        descriptor = request->accepted_descriptor;
        request->accepted_descriptor = -1;
        request->descriptor_taken = 1;
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    return descriptor;
}

void r_runtime_darwin_socket_accept_release(RRuntimeDarwinSocketAccept *request) {
    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (!request->completion_delivered || request->read_source != NULL ||
        request->timer_source != NULL || request->pending_cancel_handlers != 0U) {
        (void)pthread_mutex_unlock(&request->mutex);
        abort();
    }
    socket_accept_close_result_locked(request);
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    if (pthread_mutex_destroy(&request->mutex) != 0) {
        abort();
    }
    r_runtime_allocator_deallocate(request, _Alignof(RRuntimeDarwinSocketAccept));
}

void r_runtime_darwin_socket_accept_abort(RRuntimeDarwinSocketAccept **request_slot) {
    RRuntimeDarwinSocketAccept *request;

    if (request_slot == NULL || *request_slot == NULL) {
        return;
    }
    request = *request_slot;
    *request_slot = NULL;
    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (request->read_activated || request->timer_activated || request->terminal_selected ||
        request->completion != NULL) {
        if (pthread_mutex_unlock(&request->mutex) != 0) {
            abort();
        }
        abort();
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    socket_accept_dispose_unsubmitted(request);
}

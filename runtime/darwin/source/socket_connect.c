#include "r_runtime_darwin_socket.h"

#include "r_runtime_darwin_event.h"

#include <dispatch/dispatch.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

typedef enum RRuntimeDarwinSocketSourceKind {
    R_RUNTIME_DARWIN_SOCKET_SOURCE_WRITE = 0,
    R_RUNTIME_DARWIN_SOCKET_SOURCE_TIMER
} RRuntimeDarwinSocketSourceKind;

typedef struct RRuntimeDarwinSocketSourceContext {
    RRuntimeDarwinSocketConnect *request;
    RRuntimeDarwinSocketSourceKind kind;
} RRuntimeDarwinSocketSourceContext;

struct RRuntimeDarwinSocketConnect {
    RRuntimeAllocator *allocator;
    pthread_mutex_t mutex;
    struct sockaddr_storage remote;
    socklen_t remote_length;
    int descriptor;
    dispatch_source_t write_source;
    dispatch_source_t timer_source;
    RRuntimeDarwinSocketSourceContext write_context;
    RRuntimeDarwinSocketSourceContext timer_context;
    RRuntimeDarwinSocketConnectCompletionFn completion;
    void *completion_context;
    RRuntimeDarwinSocketConnectResult result;
    size_t pending_cancel_handlers;
    _Bool activated;
    _Bool terminal_selected;
    _Bool completion_delivered;
    _Bool descriptor_taken;
};

static RRuntimeDarwinSocketPrepareResult prepare_result(RRuntimeDarwinSocketConnect *request,
                                                        RRuntimeDarwinSocketPrepareStatus status,
                                                        int native_error) {
    return (RRuntimeDarwinSocketPrepareResult){request, status, native_error};
}

static void socket_connect_close_locked(RRuntimeDarwinSocketConnect *request) {
    if (request->descriptor >= 0) {
        (void)close(request->descriptor);
        request->descriptor = -1;
    }
}

static void socket_connect_activate_sources_locked(RRuntimeDarwinSocketConnect *request) {
    if (request->activated) {
        return;
    }
    request->activated = 1;
    dispatch_activate(request->write_source);
    if (request->timer_source != NULL) {
        dispatch_activate(request->timer_source);
    }
}

static void socket_connect_cancel_sources_locked(RRuntimeDarwinSocketConnect *request) {
    socket_connect_activate_sources_locked(request);
    if (request->write_source != NULL) {
        dispatch_source_cancel(request->write_source);
    }
    if (request->timer_source != NULL) {
        dispatch_source_cancel(request->timer_source);
    }
}

static _Bool socket_connect_select_locked(RRuntimeDarwinSocketConnect *request,
                                          RRuntimeDarwinSocketTerminalEvent event,
                                          uint64_t event_sequence,
                                          int native_error,
                                          _Bool connected) {
    if (event_sequence == UINT64_C(0)) {
        abort();
    }
    if (request->completion_delivered) {
        return 0;
    }
    if (request->terminal_selected && event_sequence >= request->result.terminal_event_sequence) {
        return 0;
    }
    request->terminal_selected = 1;
    request->result.terminal_event = event;
    request->result.native_error = native_error;
    request->result.terminal_event_sequence = event_sequence;
    request->result.connected = connected;
    socket_connect_cancel_sources_locked(request);
    return 1;
}

static void socket_connect_event(void *context_pointer) {
    RRuntimeDarwinSocketSourceContext *context = context_pointer;
    RRuntimeDarwinSocketConnect *request = context->request;
    uint64_t event_sequence;
    int native_error = 0;
    socklen_t native_error_size = (socklen_t)sizeof(native_error);

    if (context->kind != R_RUNTIME_DARWIN_SOCKET_SOURCE_WRITE) {
        abort();
    }
    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    event_sequence = r_runtime_darwin_event_sequence_next();
    if (!request->terminal_selected || event_sequence < request->result.terminal_event_sequence) {
        if (request->descriptor < 0 ||
            getsockopt(
                request->descriptor, SOL_SOCKET, SO_ERROR, &native_error, &native_error_size) !=
                0) {
            native_error = errno;
        }
        (void)socket_connect_select_locked(request,
                                           R_RUNTIME_DARWIN_SOCKET_TERMINAL_NATIVE,
                                           event_sequence,
                                           native_error,
                                           native_error == 0);
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
}

static void socket_connect_timer(void *context_pointer) {
    RRuntimeDarwinSocketSourceContext *context = context_pointer;
    RRuntimeDarwinSocketConnect *request = context->request;
    uint64_t event_sequence;

    if (context->kind != R_RUNTIME_DARWIN_SOCKET_SOURCE_TIMER) {
        abort();
    }
    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    event_sequence = r_runtime_darwin_event_sequence_next();
    (void)socket_connect_select_locked(
        request, R_RUNTIME_DARWIN_SOCKET_TERMINAL_TIMED_OUT, event_sequence, 0, 0);
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
}

static void socket_connect_source_cancelled(void *context_pointer) {
    RRuntimeDarwinSocketSourceContext *context = context_pointer;
    RRuntimeDarwinSocketConnect *request = context->request;
    RRuntimeDarwinSocketConnectCompletionFn completion = NULL;
    void *completion_context = NULL;
    dispatch_source_t source = NULL;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (context->kind == R_RUNTIME_DARWIN_SOCKET_SOURCE_WRITE) {
        source = request->write_source;
        request->write_source = NULL;
    } else if (context->kind == R_RUNTIME_DARWIN_SOCKET_SOURCE_TIMER) {
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
        if (!request->result.connected) {
            socket_connect_close_locked(request);
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

static int create_socket(int domain, int *native_error) {
    const int enabled = 1;
    int descriptor;
    int descriptor_flags;
    int status_flags;

    errno = 0;
    descriptor = socket(domain, SOCK_STREAM, 0);
    if (descriptor < 0) {
        *native_error = errno;
        return -1;
    }
    descriptor_flags = fcntl(descriptor, F_GETFD);
    if (descriptor_flags < 0) {
        *native_error = errno;
        (void)close(descriptor);
        return -1;
    }
    status_flags = fcntl(descriptor, F_GETFL);
    if (status_flags < 0) {
        *native_error = errno;
        (void)close(descriptor);
        return -1;
    }
    if (fcntl(descriptor, F_SETFD, descriptor_flags | FD_CLOEXEC) != 0 ||
        fcntl(descriptor, F_SETFL, status_flags | O_NONBLOCK) != 0) {
        *native_error = errno;
        (void)close(descriptor);
        return -1;
    }
#if defined(SO_NOSIGPIPE)
    if (setsockopt(descriptor, SOL_SOCKET, SO_NOSIGPIPE, &enabled, (socklen_t)sizeof(enabled)) !=
        0) {
        *native_error = errno;
        (void)close(descriptor);
        return -1;
    }
#else
    (void)enabled;
#endif
    return descriptor;
}

static void dispose_unsubmitted(RRuntimeDarwinSocketConnect *request) {
    dispatch_source_t write_source;
    dispatch_source_t timer_source;

    if (request == NULL) {
        return;
    }
    write_source = request->write_source;
    timer_source = request->timer_source;
    request->write_source = NULL;
    request->timer_source = NULL;
    socket_connect_close_locked(request);
    if (write_source != NULL) {
        dispatch_source_set_event_handler_f(write_source, NULL);
        dispatch_source_set_cancel_handler_f(write_source, NULL);
        dispatch_set_context(write_source, NULL);
        dispatch_source_cancel(write_source);
        dispatch_activate(write_source);
        dispatch_release(write_source);
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
    r_runtime_allocator_deallocate(request, _Alignof(RRuntimeDarwinSocketConnect));
}

RRuntimeDarwinSocketPrepareResult
r_runtime_darwin_socket_connect_prepare(RRuntimeAllocator *allocator,
                                        const struct sockaddr *remote,
                                        socklen_t remote_length,
                                        uint64_t timeout_nanoseconds) {
    RRuntimeDarwinSocketConnect *request = NULL;
    dispatch_queue_t queue;
    dispatch_time_t timer_deadline;
    int domain;
    int native_error = 0;

    if ((remote->sa_family != AF_INET && remote->sa_family != AF_INET6 &&
         remote->sa_family != AF_UNIX) ||
        (remote->sa_family == AF_INET && (size_t)remote_length != sizeof(struct sockaddr_in)) ||
        (remote->sa_family == AF_INET6 && (size_t)remote_length != sizeof(struct sockaddr_in6)) ||
        (remote->sa_family == AF_UNIX &&
         ((size_t)remote_length <= offsetof(struct sockaddr_un, sun_path) ||
          (size_t)remote_length > sizeof(struct sockaddr_un))) ||
        (size_t)remote_length > sizeof(request->remote) ||
        timeout_nanoseconds > (uint64_t)INT64_MAX) {
        return prepare_result(NULL, R_RUNTIME_DARWIN_SOCKET_PREPARE_INVALID, 0);
    }
    if (r_runtime_allocator_allocate(allocator,
                                     sizeof(*request),
                                     _Alignof(RRuntimeDarwinSocketConnect),
                                     (void **)&request) != R_RUNTIME_ALLOCATION_OK) {
        return prepare_result(NULL, R_RUNTIME_DARWIN_SOCKET_PREPARE_ALLOCATION_FAILED, ENOMEM);
    }
    (void)memset(request, 0, sizeof(*request));
    request->allocator = allocator;
    request->descriptor = -1;
    if (pthread_mutex_init(&request->mutex, NULL) != 0) {
        r_runtime_allocator_deallocate(request, _Alignof(RRuntimeDarwinSocketConnect));
        return prepare_result(NULL, R_RUNTIME_DARWIN_SOCKET_PREPARE_ALLOCATION_FAILED, ENOMEM);
    }
    domain = remote->sa_family;
    request->descriptor = create_socket(domain, &native_error);
    if (request->descriptor < 0) {
        dispose_unsubmitted(request);
        return prepare_result(NULL, R_RUNTIME_DARWIN_SOCKET_PREPARE_NATIVE_FAILED, native_error);
    }
    (void)memcpy(&request->remote, remote, (size_t)remote_length);
    request->remote_length = remote_length;
    queue = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0U);
    if (queue == NULL) {
        dispose_unsubmitted(request);
        return prepare_result(NULL, R_RUNTIME_DARWIN_SOCKET_PREPARE_ALLOCATION_FAILED, ENOMEM);
    }
    request->write_source = dispatch_source_create(
        DISPATCH_SOURCE_TYPE_WRITE, (uintptr_t)request->descriptor, 0U, queue);
    if (request->write_source == NULL) {
        dispose_unsubmitted(request);
        return prepare_result(NULL, R_RUNTIME_DARWIN_SOCKET_PREPARE_ALLOCATION_FAILED, ENOMEM);
    }
    request->write_context =
        (RRuntimeDarwinSocketSourceContext){request, R_RUNTIME_DARWIN_SOCKET_SOURCE_WRITE};
    dispatch_set_context(request->write_source, &request->write_context);
    dispatch_source_set_event_handler_f(request->write_source, socket_connect_event);
    dispatch_source_set_cancel_handler_f(request->write_source, socket_connect_source_cancelled);
    request->pending_cancel_handlers = 1U;
    if (timeout_nanoseconds != 0U) {
        timer_deadline = dispatch_time(DISPATCH_TIME_NOW, (int64_t)timeout_nanoseconds);
        if (timer_deadline == DISPATCH_TIME_FOREVER) {
            dispose_unsubmitted(request);
            return prepare_result(NULL, R_RUNTIME_DARWIN_SOCKET_PREPARE_INVALID, 0);
        }
        request->timer_source =
            dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0U, DISPATCH_TIMER_STRICT, queue);
        if (request->timer_source == NULL) {
            dispose_unsubmitted(request);
            return prepare_result(NULL, R_RUNTIME_DARWIN_SOCKET_PREPARE_ALLOCATION_FAILED, ENOMEM);
        }
        request->timer_context =
            (RRuntimeDarwinSocketSourceContext){request, R_RUNTIME_DARWIN_SOCKET_SOURCE_TIMER};
        dispatch_set_context(request->timer_source, &request->timer_context);
        dispatch_source_set_event_handler_f(request->timer_source, socket_connect_timer);
        dispatch_source_set_cancel_handler_f(request->timer_source,
                                             socket_connect_source_cancelled);
        dispatch_source_set_timer(request->timer_source, timer_deadline, DISPATCH_TIME_FOREVER, 0U);
        request->pending_cancel_handlers += 1U;
    }
    return prepare_result(request, R_RUNTIME_DARWIN_SOCKET_PREPARE_OK, 0);
}

_Bool r_runtime_darwin_socket_connect_bind(RRuntimeDarwinSocketConnect *request,
                                           RRuntimeDarwinSocketConnectCompletionFn completion,
                                           void *context) {
    _Bool bound = 0;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        return 0;
    }
    if (!request->activated && !request->terminal_selected && request->completion == NULL &&
        request->descriptor >= 0 && request->write_source != NULL) {
        request->completion = completion;
        request->completion_context = context;
        bound = 1;
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    return bound;
}

_Bool r_runtime_darwin_socket_connect_activate(RRuntimeDarwinSocketConnect *request) {
    uint64_t event_sequence;
    int connect_status;
    int native_error;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        return 0;
    }
    if (request->activated || request->terminal_selected || request->completion == NULL ||
        request->descriptor < 0 || request->write_source == NULL) {
        (void)pthread_mutex_unlock(&request->mutex);
        return 0;
    }
    errno = 0;
    connect_status = connect(
        request->descriptor, (const struct sockaddr *)&request->remote, request->remote_length);
    native_error = connect_status == 0 ? 0 : errno;
    if (connect_status == 0 || native_error == EISCONN) {
        event_sequence = r_runtime_darwin_event_sequence_next();
        (void)socket_connect_select_locked(
            request, R_RUNTIME_DARWIN_SOCKET_TERMINAL_NATIVE, event_sequence, 0, 1);
    } else if (native_error == EINPROGRESS || native_error == EALREADY || native_error == EINTR) {
        socket_connect_activate_sources_locked(request);
    } else {
        event_sequence = r_runtime_darwin_event_sequence_next();
        (void)socket_connect_select_locked(
            request, R_RUNTIME_DARWIN_SOCKET_TERMINAL_NATIVE, event_sequence, native_error, 0);
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    return 1;
}

_Bool r_runtime_darwin_socket_connect_cancel(RRuntimeDarwinSocketConnect *request,
                                             uint64_t cancellation_sequence) {
    _Bool selected = 0;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        return 0;
    }
    selected = socket_connect_select_locked(
        request, R_RUNTIME_DARWIN_SOCKET_TERMINAL_CANCELLED, cancellation_sequence, 0, 0);
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    return selected;
}

RRuntimeDarwinSocketConnectResult
r_runtime_darwin_socket_connect_result(RRuntimeDarwinSocketConnect *request) {
    RRuntimeDarwinSocketConnectResult result = {0};

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

int r_runtime_darwin_socket_connect_take_descriptor(RRuntimeDarwinSocketConnect *request) {
    int descriptor = -1;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        return -1;
    }
    if (request->completion_delivered && request->result.connected && !request->descriptor_taken &&
        request->descriptor >= 0) {
        descriptor = request->descriptor;
        request->descriptor = -1;
        request->descriptor_taken = 1;
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    return descriptor;
}

void r_runtime_darwin_socket_connect_release(RRuntimeDarwinSocketConnect *request) {
    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (!request->completion_delivered || request->write_source != NULL ||
        request->timer_source != NULL || request->pending_cancel_handlers != 0U) {
        (void)pthread_mutex_unlock(&request->mutex);
        abort();
    }
    socket_connect_close_locked(request);
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    if (pthread_mutex_destroy(&request->mutex) != 0) {
        abort();
    }
    r_runtime_allocator_deallocate(request, _Alignof(RRuntimeDarwinSocketConnect));
}

void r_runtime_darwin_socket_connect_abort(RRuntimeDarwinSocketConnect **request_slot) {
    RRuntimeDarwinSocketConnect *request;

    if (request_slot == NULL || *request_slot == NULL) {
        return;
    }
    request = *request_slot;
    *request_slot = NULL;
    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (request->activated || request->terminal_selected || request->completion != NULL) {
        if (pthread_mutex_unlock(&request->mutex) != 0) {
            abort();
        }
        abort();
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    dispose_unsubmitted(request);
}

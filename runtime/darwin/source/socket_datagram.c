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
#include <sys/uio.h>

typedef enum RRuntimeDarwinSocketDatagramSourceKind {
    R_RUNTIME_DARWIN_SOCKET_DATAGRAM_SOURCE_READY = 0,
    R_RUNTIME_DARWIN_SOCKET_DATAGRAM_SOURCE_TIMER
} RRuntimeDarwinSocketDatagramSourceKind;

typedef struct RRuntimeDarwinSocketDatagramSourceContext {
    RRuntimeDarwinSocketDatagram *request;
    RRuntimeDarwinSocketDatagramSourceKind kind;
} RRuntimeDarwinSocketDatagramSourceContext;

struct RRuntimeDarwinSocketDatagram {
    RRuntimeAllocator *allocator;
    pthread_mutex_t mutex;
    int descriptor;
    RRuntimeDarwinSocketDatagramOperation operation;
    struct sockaddr_storage peer;
    socklen_t peer_length;
    const void *send_data;
    void *receive_data;
    size_t size;
    dispatch_source_t ready_source;
    dispatch_source_t timer_source;
    RRuntimeDarwinSocketDatagramSourceContext ready_context;
    RRuntimeDarwinSocketDatagramSourceContext timer_context;
    RRuntimeDarwinSocketDatagramCompletionFn completion;
    void *completion_context;
    RRuntimeDarwinSocketDatagramResult result;
    size_t pending_cancel_handlers;
    _Bool ready_activated;
    _Bool timer_activated;
    _Bool terminal_selected;
    _Bool completion_delivered;
};

#if defined(R_RUNTIME_DARWIN_SOCKET_TESTING)
static pthread_mutex_t datagram_testing_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t datagram_testing_condition = PTHREAD_COND_INITIALIZER;
static _Bool datagram_testing_pause_next;
static _Bool datagram_testing_paused;
static _Bool datagram_testing_release;
static int datagram_testing_native_error;

void r_runtime_darwin_socket_testing_pause_next_datagram_before_native(void) {
    if (pthread_mutex_lock(&datagram_testing_mutex) != 0) {
        abort();
    }
    if (datagram_testing_pause_next || datagram_testing_paused || datagram_testing_release) {
        (void)pthread_mutex_unlock(&datagram_testing_mutex);
        abort();
    }
    datagram_testing_pause_next = 1;
    if (pthread_mutex_unlock(&datagram_testing_mutex) != 0) {
        abort();
    }
}

void r_runtime_darwin_socket_testing_wait_for_datagram_before_native(void) {
    if (pthread_mutex_lock(&datagram_testing_mutex) != 0) {
        abort();
    }
    while (!datagram_testing_paused) {
        if (pthread_cond_wait(&datagram_testing_condition, &datagram_testing_mutex) != 0) {
            abort();
        }
    }
    if (pthread_mutex_unlock(&datagram_testing_mutex) != 0) {
        abort();
    }
}

void r_runtime_darwin_socket_testing_release_datagram_before_native(void) {
    if (pthread_mutex_lock(&datagram_testing_mutex) != 0) {
        abort();
    }
    if (!datagram_testing_paused || datagram_testing_release) {
        (void)pthread_mutex_unlock(&datagram_testing_mutex);
        abort();
    }
    datagram_testing_release = 1;
    if (pthread_cond_broadcast(&datagram_testing_condition) != 0) {
        (void)pthread_mutex_unlock(&datagram_testing_mutex);
        abort();
    }
    if (pthread_mutex_unlock(&datagram_testing_mutex) != 0) {
        abort();
    }
}

void r_runtime_darwin_socket_testing_fail_next_datagram_native(int native_error) {
    if (native_error == 0 || pthread_mutex_lock(&datagram_testing_mutex) != 0) {
        abort();
    }
    if (datagram_testing_native_error != 0) {
        (void)pthread_mutex_unlock(&datagram_testing_mutex);
        abort();
    }
    datagram_testing_native_error = native_error;
    if (pthread_mutex_unlock(&datagram_testing_mutex) != 0) {
        abort();
    }
}

static int datagram_testing_before_native(void) {
    int native_error;

    if (pthread_mutex_lock(&datagram_testing_mutex) != 0) {
        abort();
    }
    if (datagram_testing_pause_next) {
        datagram_testing_pause_next = 0;
        datagram_testing_paused = 1;
        if (pthread_cond_broadcast(&datagram_testing_condition) != 0) {
            (void)pthread_mutex_unlock(&datagram_testing_mutex);
            abort();
        }
        while (!datagram_testing_release) {
            if (pthread_cond_wait(&datagram_testing_condition, &datagram_testing_mutex) != 0) {
                abort();
            }
        }
        datagram_testing_release = 0;
        datagram_testing_paused = 0;
    }
    native_error = datagram_testing_native_error;
    datagram_testing_native_error = 0;
    if (pthread_mutex_unlock(&datagram_testing_mutex) != 0) {
        abort();
    }
    return native_error;
}
#else
static int datagram_testing_before_native(void) {
    return 0;
}
#endif

static RRuntimeDarwinSocketDatagramPrepareResult
datagram_prepare_result(RRuntimeDarwinSocketDatagram *request,
                        RRuntimeDarwinSocketPrepareStatus status,
                        int native_error) {
    return (RRuntimeDarwinSocketDatagramPrepareResult){request, status, native_error};
}

static void datagram_activate_ready_locked(RRuntimeDarwinSocketDatagram *request) {
    if (!request->ready_activated) {
        request->ready_activated = 1;
        dispatch_activate(request->ready_source);
    }
}

static void datagram_activate_timer_locked(RRuntimeDarwinSocketDatagram *request) {
    if (request->timer_source != NULL && !request->timer_activated) {
        request->timer_activated = 1;
        dispatch_activate(request->timer_source);
    }
}

static void datagram_cancel_sources_locked(RRuntimeDarwinSocketDatagram *request) {
    datagram_activate_ready_locked(request);
    datagram_activate_timer_locked(request);
    if (request->ready_source != NULL) {
        dispatch_source_cancel(request->ready_source);
    }
    if (request->timer_source != NULL) {
        dispatch_source_cancel(request->timer_source);
    }
}

static _Bool datagram_select_locked(RRuntimeDarwinSocketDatagram *request,
                                    RRuntimeDarwinSocketTerminalEvent event,
                                    uint64_t event_sequence,
                                    int native_error,
                                    const struct sockaddr_storage *peer,
                                    socklen_t peer_length,
                                    size_t count,
                                    _Bool completed,
                                    _Bool truncated) {
    if (event_sequence == UINT64_C(0)) {
        abort();
    }
    if (request->completion_delivered) {
        return 0;
    }
    if (request->terminal_selected) {
        if (request->operation == R_RUNTIME_DARWIN_SOCKET_DATAGRAM_SEND &&
            request->result.completed) {
            return 0;
        }
        if (event_sequence >= request->result.terminal_event_sequence) {
            return 0;
        }
    }
    request->terminal_selected = 1;
    request->result = (RRuntimeDarwinSocketDatagramResult){0};
    request->result.operation = request->operation;
    request->result.terminal_event = event;
    request->result.native_error = native_error;
    request->result.terminal_event_sequence = event_sequence;
    request->result.count = count;
    request->result.completed = completed;
    request->result.truncated = truncated;
    if (completed && request->operation == R_RUNTIME_DARWIN_SOCKET_DATAGRAM_RECEIVE) {
        if (peer == NULL || (size_t)peer_length > sizeof(request->result.peer)) {
            abort();
        }
        request->result.peer = *peer;
        request->result.peer_length = peer_length;
    }
    datagram_cancel_sources_locked(request);
    return 1;
}

static void datagram_ready(void *context_pointer) {
    RRuntimeDarwinSocketDatagramSourceContext *context = context_pointer;
    RRuntimeDarwinSocketDatagram *request = context->request;
    struct sockaddr_storage peer = {0};
    struct msghdr message = {0};
    struct iovec vector = {0};
    socklen_t peer_length = (socklen_t)sizeof(peer);
    uint64_t event_sequence;
    ssize_t transferred;
    int native_error;
    _Bool truncated = 0;
    uint8_t empty_receive_probe = 0U;

    if (context->kind != R_RUNTIME_DARWIN_SOCKET_DATAGRAM_SOURCE_READY) {
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
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    native_error = datagram_testing_before_native();
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
    if (native_error != 0) {
        transferred = -1;
    } else if (request->operation == R_RUNTIME_DARWIN_SOCKET_DATAGRAM_SEND) {
        transferred = sendto(request->descriptor,
                             request->send_data,
                             request->size,
                             0,
                             request->peer_length == 0U ? NULL
                                                        : (const struct sockaddr *)&request->peer,
                             request->peer_length);
        native_error = transferred < 0 ? errno : 0;
    } else {
        /* Darwin marks even an empty datagram truncated for a zero-sized iovec.
         * A one-byte scratch receive distinguishes it from a nonempty datagram,
         * while the published count remains bounded by the caller's zero capacity. */
        vector.iov_base = request->size == 0U ? &empty_receive_probe : request->receive_data;
        vector.iov_len = request->size == 0U ? 1U : request->size;
        message.msg_name = &peer;
        message.msg_namelen = peer_length;
        message.msg_iov = &vector;
        message.msg_iovlen = 1;
        transferred = recvmsg(request->descriptor, &message, 0);
        native_error = transferred < 0 ? errno : 0;
        peer_length = message.msg_namelen;
        truncated = (message.msg_flags & MSG_TRUNC) != 0;
    }
    if (transferred < 0 &&
        (native_error == EAGAIN || native_error == EWOULDBLOCK || native_error == EINTR)) {
        if (pthread_mutex_unlock(&request->mutex) != 0) {
            abort();
        }
        return;
    }
    if (transferred >= 0 && request->operation == R_RUNTIME_DARWIN_SOCKET_DATAGRAM_SEND &&
        (size_t)transferred != request->size) {
        transferred = -1;
        native_error = EMSGSIZE;
    }
    if (transferred >= 0) {
        const size_t count =
            request->operation == R_RUNTIME_DARWIN_SOCKET_DATAGRAM_RECEIVE
                ? ((size_t)transferred > request->size ? request->size : (size_t)transferred)
                : request->size;

        (void)datagram_select_locked(request,
                                     R_RUNTIME_DARWIN_SOCKET_TERMINAL_NATIVE,
                                     event_sequence,
                                     0,
                                     &peer,
                                     peer_length,
                                     count,
                                     1,
                                     truncated || (size_t)transferred > request->size);
    } else {
        (void)datagram_select_locked(request,
                                     R_RUNTIME_DARWIN_SOCKET_TERMINAL_NATIVE,
                                     event_sequence,
                                     native_error,
                                     NULL,
                                     0U,
                                     0U,
                                     0,
                                     0);
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
}

static void datagram_timer(void *context_pointer) {
    RRuntimeDarwinSocketDatagramSourceContext *context = context_pointer;
    RRuntimeDarwinSocketDatagram *request = context->request;
    uint64_t event_sequence;

    if (context->kind != R_RUNTIME_DARWIN_SOCKET_DATAGRAM_SOURCE_TIMER) {
        abort();
    }
    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    event_sequence = r_runtime_darwin_event_sequence_next();
    (void)datagram_select_locked(
        request, R_RUNTIME_DARWIN_SOCKET_TERMINAL_TIMED_OUT, event_sequence, 0, NULL, 0U, 0U, 0, 0);
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
}

static void datagram_source_cancelled(void *context_pointer) {
    RRuntimeDarwinSocketDatagramSourceContext *context = context_pointer;
    RRuntimeDarwinSocketDatagram *request = context->request;
    RRuntimeDarwinSocketDatagramCompletionFn completion = NULL;
    void *completion_context = NULL;
    dispatch_source_t source = NULL;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (context->kind == R_RUNTIME_DARWIN_SOCKET_DATAGRAM_SOURCE_READY) {
        source = request->ready_source;
        request->ready_source = NULL;
    } else if (context->kind == R_RUNTIME_DARWIN_SOCKET_DATAGRAM_SOURCE_TIMER) {
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

static void datagram_dispose_unsubmitted(RRuntimeDarwinSocketDatagram *request) {
    dispatch_source_t ready_source;
    dispatch_source_t timer_source;

    if (request == NULL) {
        return;
    }
    ready_source = request->ready_source;
    timer_source = request->timer_source;
    request->ready_source = NULL;
    request->timer_source = NULL;
    if (ready_source != NULL) {
        dispatch_source_set_event_handler_f(ready_source, NULL);
        dispatch_source_set_cancel_handler_f(ready_source, NULL);
        dispatch_set_context(ready_source, NULL);
        dispatch_source_cancel(ready_source);
        dispatch_activate(ready_source);
        dispatch_release(ready_source);
    }
    if (timer_source != NULL) {
        dispatch_source_set_event_handler_f(timer_source, NULL);
        dispatch_source_set_cancel_handler_f(timer_source, NULL);
        dispatch_set_context(timer_source, NULL);
        dispatch_source_cancel(timer_source);
        dispatch_activate(timer_source);
        dispatch_release(timer_source);
    }
    r_runtime_allocator_deallocate(request->receive_data, _Alignof(uint8_t));
    if (pthread_mutex_destroy(&request->mutex) != 0) {
        abort();
    }
    r_runtime_allocator_deallocate(request, _Alignof(RRuntimeDarwinSocketDatagram));
}

static RRuntimeDarwinSocketDatagramPrepareResult
datagram_prepare(RRuntimeAllocator *allocator,
                 int descriptor,
                 RRuntimeDarwinSocketDatagramOperation operation,
                 const struct sockaddr *peer,
                 socklen_t peer_length,
                 const void *data,
                 size_t size,
                 uint64_t timeout_nanoseconds) {
    RRuntimeDarwinSocketDatagram *request = NULL;
    dispatch_queue_t queue;
    dispatch_source_type_t ready_type;
    dispatch_time_t timer_deadline;
    socklen_t option_length;
    int descriptor_flags;
    int status_flags;
    int socket_type = 0;

    if (descriptor < 0 || timeout_nanoseconds > (uint64_t)INT64_MAX ||
        (operation == R_RUNTIME_DARWIN_SOCKET_DATAGRAM_SEND &&
         (peer == NULL ? peer_length != 0U
                       : (peer_length == 0U || (size_t)peer_length > sizeof(request->peer))))) {
        return datagram_prepare_result(NULL, R_RUNTIME_DARWIN_SOCKET_PREPARE_INVALID, 0);
    }
    errno = 0;
    descriptor_flags = fcntl(descriptor, F_GETFD);
    if (descriptor_flags < 0) {
        return datagram_prepare_result(NULL, R_RUNTIME_DARWIN_SOCKET_PREPARE_NATIVE_FAILED, errno);
    }
    status_flags = fcntl(descriptor, F_GETFL);
    if (status_flags < 0) {
        return datagram_prepare_result(NULL, R_RUNTIME_DARWIN_SOCKET_PREPARE_NATIVE_FAILED, errno);
    }
    option_length = (socklen_t)sizeof(socket_type);
    if (getsockopt(descriptor, SOL_SOCKET, SO_TYPE, &socket_type, &option_length) != 0) {
        return datagram_prepare_result(NULL, R_RUNTIME_DARWIN_SOCKET_PREPARE_NATIVE_FAILED, errno);
    }
    if ((descriptor_flags & FD_CLOEXEC) == 0 || (status_flags & O_NONBLOCK) == 0 ||
        socket_type != SOCK_DGRAM) {
        return datagram_prepare_result(NULL, R_RUNTIME_DARWIN_SOCKET_PREPARE_INVALID, 0);
    }
    if (r_runtime_allocator_allocate(allocator,
                                     sizeof(*request),
                                     _Alignof(RRuntimeDarwinSocketDatagram),
                                     (void **)&request) != R_RUNTIME_ALLOCATION_OK) {
        return datagram_prepare_result(
            NULL, R_RUNTIME_DARWIN_SOCKET_PREPARE_ALLOCATION_FAILED, ENOMEM);
    }
    (void)memset(request, 0, sizeof(*request));
    request->allocator = allocator;
    request->descriptor = descriptor;
    request->operation = operation;
    request->send_data = data;
    request->size = size;
    if (operation == R_RUNTIME_DARWIN_SOCKET_DATAGRAM_SEND) {
        if (peer != NULL) {
            (void)memcpy(&request->peer, peer, peer_length);
        }
        request->peer_length = peer_length;
    } else if (r_runtime_allocator_allocate(
                   allocator, size == 0U ? 1U : size, _Alignof(uint8_t), &request->receive_data) !=
               R_RUNTIME_ALLOCATION_OK) {
        r_runtime_allocator_deallocate(request, _Alignof(RRuntimeDarwinSocketDatagram));
        return datagram_prepare_result(
            NULL, R_RUNTIME_DARWIN_SOCKET_PREPARE_ALLOCATION_FAILED, ENOMEM);
    }
    if (pthread_mutex_init(&request->mutex, NULL) != 0) {
        r_runtime_allocator_deallocate(request->receive_data, _Alignof(uint8_t));
        r_runtime_allocator_deallocate(request, _Alignof(RRuntimeDarwinSocketDatagram));
        return datagram_prepare_result(
            NULL, R_RUNTIME_DARWIN_SOCKET_PREPARE_ALLOCATION_FAILED, ENOMEM);
    }
    queue = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0U);
    if (queue == NULL) {
        datagram_dispose_unsubmitted(request);
        return datagram_prepare_result(
            NULL, R_RUNTIME_DARWIN_SOCKET_PREPARE_ALLOCATION_FAILED, ENOMEM);
    }
    ready_type = operation == R_RUNTIME_DARWIN_SOCKET_DATAGRAM_SEND ? DISPATCH_SOURCE_TYPE_WRITE
                                                                    : DISPATCH_SOURCE_TYPE_READ;
    request->ready_source = dispatch_source_create(ready_type, (uintptr_t)descriptor, 0U, queue);
    if (request->ready_source == NULL) {
        datagram_dispose_unsubmitted(request);
        return datagram_prepare_result(
            NULL, R_RUNTIME_DARWIN_SOCKET_PREPARE_ALLOCATION_FAILED, ENOMEM);
    }
    request->ready_context = (RRuntimeDarwinSocketDatagramSourceContext){
        request, R_RUNTIME_DARWIN_SOCKET_DATAGRAM_SOURCE_READY};
    dispatch_set_context(request->ready_source, &request->ready_context);
    dispatch_source_set_event_handler_f(request->ready_source, datagram_ready);
    dispatch_source_set_cancel_handler_f(request->ready_source, datagram_source_cancelled);
    request->pending_cancel_handlers = 1U;
    if (timeout_nanoseconds != 0U) {
        timer_deadline = dispatch_time(DISPATCH_TIME_NOW, (int64_t)timeout_nanoseconds);
        if (timer_deadline == DISPATCH_TIME_FOREVER) {
            datagram_dispose_unsubmitted(request);
            return datagram_prepare_result(NULL, R_RUNTIME_DARWIN_SOCKET_PREPARE_INVALID, 0);
        }
        request->timer_source =
            dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0U, DISPATCH_TIMER_STRICT, queue);
        if (request->timer_source == NULL) {
            datagram_dispose_unsubmitted(request);
            return datagram_prepare_result(
                NULL, R_RUNTIME_DARWIN_SOCKET_PREPARE_ALLOCATION_FAILED, ENOMEM);
        }
        request->timer_context = (RRuntimeDarwinSocketDatagramSourceContext){
            request, R_RUNTIME_DARWIN_SOCKET_DATAGRAM_SOURCE_TIMER};
        dispatch_set_context(request->timer_source, &request->timer_context);
        dispatch_source_set_event_handler_f(request->timer_source, datagram_timer);
        dispatch_source_set_cancel_handler_f(request->timer_source, datagram_source_cancelled);
        dispatch_source_set_timer(request->timer_source, timer_deadline, DISPATCH_TIME_FOREVER, 0U);
        request->pending_cancel_handlers += 1U;
    }
    return datagram_prepare_result(request, R_RUNTIME_DARWIN_SOCKET_PREPARE_OK, 0);
}

RRuntimeDarwinSocketDatagramPrepareResult
r_runtime_darwin_socket_datagram_send_prepare(RRuntimeAllocator *allocator,
                                              int descriptor,
                                              const struct sockaddr *peer,
                                              socklen_t peer_length,
                                              const void *data,
                                              size_t size,
                                              uint64_t timeout_nanoseconds) {
    return datagram_prepare(allocator,
                            descriptor,
                            R_RUNTIME_DARWIN_SOCKET_DATAGRAM_SEND,
                            peer,
                            peer_length,
                            data,
                            size,
                            timeout_nanoseconds);
}

RRuntimeDarwinSocketDatagramPrepareResult r_runtime_darwin_socket_datagram_receive_prepare(
    RRuntimeAllocator *allocator, int descriptor, size_t capacity, uint64_t timeout_nanoseconds) {
    return datagram_prepare(allocator,
                            descriptor,
                            R_RUNTIME_DARWIN_SOCKET_DATAGRAM_RECEIVE,
                            NULL,
                            0U,
                            NULL,
                            capacity,
                            timeout_nanoseconds);
}

_Bool r_runtime_darwin_socket_datagram_bind(RRuntimeDarwinSocketDatagram *request,
                                            RRuntimeDarwinSocketDatagramCompletionFn completion,
                                            void *context) {
    _Bool bound = 0;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        return 0;
    }
    if (!request->ready_activated && !request->timer_activated && !request->terminal_selected &&
        request->completion == NULL && request->descriptor >= 0 && request->ready_source != NULL) {
        request->completion = completion;
        request->completion_context = context;
        bound = 1;
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    return bound;
}

_Bool r_runtime_darwin_socket_datagram_arm_deadline(RRuntimeDarwinSocketDatagram *request) {
    if (pthread_mutex_lock(&request->mutex) != 0) {
        return 0;
    }
    if (request->terminal_selected || request->completion == NULL || request->descriptor < 0 ||
        request->ready_source == NULL) {
        (void)pthread_mutex_unlock(&request->mutex);
        return 0;
    }
    datagram_activate_timer_locked(request);
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    return 1;
}

_Bool r_runtime_darwin_socket_datagram_activate(RRuntimeDarwinSocketDatagram *request) {
    if (pthread_mutex_lock(&request->mutex) != 0) {
        return 0;
    }
    if (request->terminal_selected || request->ready_activated) {
        if (pthread_mutex_unlock(&request->mutex) != 0) {
            abort();
        }
        return 1;
    }
    if (request->completion == NULL || request->descriptor < 0 || request->ready_source == NULL) {
        (void)pthread_mutex_unlock(&request->mutex);
        return 0;
    }
    datagram_activate_timer_locked(request);
    datagram_activate_ready_locked(request);
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    return 1;
}

_Bool r_runtime_darwin_socket_datagram_cancel(RRuntimeDarwinSocketDatagram *request,
                                              uint64_t cancellation_sequence) {
    _Bool selected;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        return 0;
    }
    selected = datagram_select_locked(request,
                                      R_RUNTIME_DARWIN_SOCKET_TERMINAL_CANCELLED,
                                      cancellation_sequence,
                                      0,
                                      NULL,
                                      0U,
                                      0U,
                                      0,
                                      0);
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    return selected;
}

RRuntimeDarwinSocketDatagramResult
r_runtime_darwin_socket_datagram_result(RRuntimeDarwinSocketDatagram *request) {
    RRuntimeDarwinSocketDatagramResult result = {0};

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

_Bool r_runtime_darwin_socket_datagram_copy_received(RRuntimeDarwinSocketDatagram *request,
                                                     void *destination,
                                                     size_t capacity) {
    _Bool copied = 0;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        return 0;
    }
    if (request->completion_delivered &&
        request->operation == R_RUNTIME_DARWIN_SOCKET_DATAGRAM_RECEIVE &&
        request->result.completed && request->result.count <= capacity &&
        (request->result.count == 0U || destination != NULL)) {
        if (request->result.count != 0U) {
            (void)memcpy(destination, request->receive_data, request->result.count);
        }
        copied = 1;
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    return copied;
}

void r_runtime_darwin_socket_datagram_release(RRuntimeDarwinSocketDatagram *request) {
    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (!request->completion_delivered || request->ready_source != NULL ||
        request->timer_source != NULL || request->pending_cancel_handlers != 0U) {
        (void)pthread_mutex_unlock(&request->mutex);
        abort();
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    r_runtime_allocator_deallocate(request->receive_data, _Alignof(uint8_t));
    if (pthread_mutex_destroy(&request->mutex) != 0) {
        abort();
    }
    r_runtime_allocator_deallocate(request, _Alignof(RRuntimeDarwinSocketDatagram));
}

void r_runtime_darwin_socket_datagram_abort(RRuntimeDarwinSocketDatagram **request_slot) {
    RRuntimeDarwinSocketDatagram *request;

    if (request_slot == NULL || *request_slot == NULL) {
        return;
    }
    request = *request_slot;
    *request_slot = NULL;
    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (request->ready_activated || request->timer_activated || request->terminal_selected ||
        request->completion != NULL) {
        if (pthread_mutex_unlock(&request->mutex) != 0) {
            abort();
        }
        abort();
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    datagram_dispose_unsubmitted(request);
}

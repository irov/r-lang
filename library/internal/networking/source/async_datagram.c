#include "r_library_net_internal.h"

#include "r_runtime_0_1.h"
#include "r_runtime_darwin_event.h"
#include "r_runtime_darwin_socket.h"
#include "r_runtime_task.h"
#include "r_runtime_type.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef enum RLibraryNetDatagramMode {
    R_LIBRARY_NET_DATAGRAM_SEND = 0,
    R_LIBRARY_NET_DATAGRAM_RECEIVE
} RLibraryNetDatagramMode;

typedef enum RLibraryNetDatagramImmediate {
    R_LIBRARY_NET_DATAGRAM_IMMEDIATE_NONE = 0,
    R_LIBRARY_NET_DATAGRAM_IMMEDIATE_FAILED
} RLibraryNetDatagramImmediate;

enum {
    R_LIBRARY_NET_DATAGRAM_CANCEL_REPORTED = 1U,
    R_LIBRARY_NET_DATAGRAM_CALLBACK_RELEASED = 2U,
    R_LIBRARY_NET_DATAGRAM_COMPLETION_SELECTED = 4U,
    R_LIBRARY_NET_DATAGRAM_FINALIZED = 8U
};

typedef struct RLibraryNetDatagramPayload RLibraryNetDatagramPayload;

struct RLibraryNetUdpQueueNode {
    RRuntimeAllocator *allocator;
    pthread_mutex_t mutex;
    RStdNetUdpSocketStorage *socket;
    RLibraryNetDatagramPayload *payload;
    RLibraryNetUdpQueueNode *previous;
    RLibraryNetUdpQueueNode *next;
    uint64_t close_sequence;
    RLibraryNetDatagramMode mode;
    _Bool reserved;
    _Bool enqueued;
    _Bool ready;
    _Bool activation_claimed;
    _Bool close_cancel_dispatched;
};

struct RLibraryNetDatagramPayload {
    RLibraryNetDatagramMode mode;
    RLibraryNetDatagramImmediate immediate;
    RStdNetDeadline deadline;
    RStdNetError immediate_error;
    uint64_t immediate_event_sequence;
    RRuntimeArray *staged_buffer;
    RRuntimeArray buffer;
    /*
     * Borrowed mode (udp_send_from/udp_receive_into): the caller keeps the view alive until the
     * result is published. Send borrows it natively through completion; receive lands in runtime
     * bounce storage and is copied into the view only on selected success. It is never freed.
     */
    uint8_t *borrowed_data;
    size_t borrowed_length;
    RStdNetUdpSocketStorage *socket_storage;
    RLibraryNetHandleStorage *socket_handle;
    RLibraryNetUdpQueueNode *queue_node;
    RRuntimeDarwinSocketDatagram *request;
    RRuntimeDarwinSocketDatagramResult native_result;
    RRuntimeTaskExternalExecution *execution;
    void *result;
    _Atomic unsigned int state;
    _Bool buffer_owned;
    _Bool borrowed;
    /* R-SLIB-NET-0017: a Unix-domain socket sends to its connected peer and reports no sender. */
    _Bool unix_domain;
};

typedef struct RLibraryNetUdpDrainAction {
    RLibraryNetUdpDrainFn drain;
    void *context;
} RLibraryNetUdpDrainAction;

#if defined(R_LIBRARY_NET_TESTING)
static pthread_mutex_t datagram_testing_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t datagram_testing_condition = PTHREAD_COND_INITIALIZER;
static _Bool datagram_testing_cancel_acknowledgement_armed;
static _Bool datagram_testing_cancel_acknowledgement_reached;
static _Bool datagram_testing_cancel_reported;

void r_library_internal_net_udp_testing_arm_cancel_acknowledgement(void) {
    if (pthread_mutex_lock(&datagram_testing_mutex) != 0) {
        abort();
    }
    if (datagram_testing_cancel_acknowledgement_armed ||
        datagram_testing_cancel_acknowledgement_reached || datagram_testing_cancel_reported) {
        (void)pthread_mutex_unlock(&datagram_testing_mutex);
        abort();
    }
    datagram_testing_cancel_acknowledgement_armed = 1;
    if (pthread_mutex_unlock(&datagram_testing_mutex) != 0) {
        abort();
    }
}

void r_library_internal_net_udp_testing_wait_cancel_reported(void) {
    if (pthread_mutex_lock(&datagram_testing_mutex) != 0) {
        abort();
    }
    while (!datagram_testing_cancel_reported) {
        if (pthread_cond_wait(&datagram_testing_condition, &datagram_testing_mutex) != 0) {
            abort();
        }
    }
    datagram_testing_cancel_reported = 0;
    if (pthread_mutex_unlock(&datagram_testing_mutex) != 0) {
        abort();
    }
}

void r_library_internal_net_udp_testing_wait_cancel_acknowledgement(void) {
    if (pthread_mutex_lock(&datagram_testing_mutex) != 0) {
        abort();
    }
    while (!datagram_testing_cancel_acknowledgement_reached) {
        if (pthread_cond_wait(&datagram_testing_condition, &datagram_testing_mutex) != 0) {
            abort();
        }
    }
    datagram_testing_cancel_acknowledgement_reached = 0;
    if (pthread_mutex_unlock(&datagram_testing_mutex) != 0) {
        abort();
    }
}

static void datagram_testing_record_cancel_reported(void) {
    if (pthread_mutex_lock(&datagram_testing_mutex) != 0) {
        abort();
    }
    datagram_testing_cancel_reported = 1;
    if (pthread_cond_broadcast(&datagram_testing_condition) != 0) {
        (void)pthread_mutex_unlock(&datagram_testing_mutex);
        abort();
    }
    if (pthread_mutex_unlock(&datagram_testing_mutex) != 0) {
        abort();
    }
}

static void datagram_testing_record_cancel_acknowledgement(void) {
    if (pthread_mutex_lock(&datagram_testing_mutex) != 0) {
        abort();
    }
    if (datagram_testing_cancel_acknowledgement_armed) {
        datagram_testing_cancel_acknowledgement_armed = 0;
        datagram_testing_cancel_acknowledgement_reached = 1;
        if (pthread_cond_broadcast(&datagram_testing_condition) != 0) {
            (void)pthread_mutex_unlock(&datagram_testing_mutex);
            abort();
        }
    }
    if (pthread_mutex_unlock(&datagram_testing_mutex) != 0) {
        abort();
    }
}
#else
static void datagram_testing_record_cancel_reported(void) {
}
static void datagram_testing_record_cancel_acknowledgement(void) {
}
#endif

_Noreturn static void datagram_panic(void) {
    r_runtime_panic(R_RUNTIME_PANIC_CONTRACT_VIOLATION, (RRuntimeSourceSpan){0U, 0U, 0U});
}

static RStdNetError net_error(RStdNetErrorCode code, int64_t native_code) {
    return (RStdNetError){code, native_code};
}

static RStdNetTaskStartResult task_start_failure(RRuntimeTaskStartStatus status) {
    RStdNetTaskStartResult result = {0};

    switch (status) {
    case R_RUNTIME_TASK_START_ALLOCATION_FAILED:
        result.error = R_STD_ASYNC_START_REFUSAL();
        return result;
    case R_RUNTIME_TASK_START_RUNTIME_STOPPING:
        result.error = R_STD_ASYNC_START_RUNTIME_STOPPING;
        return result;
    case R_RUNTIME_TASK_START_OK:
    case R_RUNTIME_TASK_START_INVALID:
        datagram_panic();
    }
    datagram_panic();
}

static void clear_array(RRuntimeArray *array) {
    (void)memset(array, 0, sizeof(*array));
}

static void move_array(RRuntimeArray *destination, RRuntimeArray *source) {
    *destination = *source;
    clear_array(source);
}

static void send_result_move(void *destination_pointer, void *source_pointer) {
    RStdNetUdpSendResult *destination = destination_pointer;
    RStdNetUdpSendResult *source = source_pointer;

    *destination = *source;
    clear_array(&source->buffer);
}

static void send_result_drop(void *value) {
    RStdNetUdpSendResult *result = value;

    r_runtime_array_destroy(&result->buffer);
}

static void receive_result_move(void *destination_pointer, void *source_pointer) {
    RStdNetUdpReceiveResult *destination = destination_pointer;
    RStdNetUdpReceiveResult *source = source_pointer;

    *destination = *source;
    clear_array(&source->buffer);
}

static void receive_result_drop(void *value) {
    RStdNetUdpReceiveResult *result = value;

    r_runtime_array_destroy(&result->buffer);
}

static RRuntimeTypeInfo
datagram_result_type(RLibraryNetDatagramMode mode, _Bool borrowed, _Bool unix_domain) {
    switch (mode) {
    case R_LIBRARY_NET_DATAGRAM_SEND:
        if (borrowed) {
            return (RRuntimeTypeInfo){
                sizeof(RStdNetVoidResult),
                _Alignof(RStdNetVoidResult),
                NULL,
                NULL,
            };
        }
        return (RRuntimeTypeInfo){
            sizeof(RStdNetUdpSendResult),
            _Alignof(RStdNetUdpSendResult),
            send_result_move,
            send_result_drop,
        };
    case R_LIBRARY_NET_DATAGRAM_RECEIVE:
        if (borrowed && unix_domain) {
            return (RRuntimeTypeInfo){
                sizeof(RStdNetUnixMessageResult),
                _Alignof(RStdNetUnixMessageResult),
                NULL,
                NULL,
            };
        }
        if (borrowed) {
            return (RRuntimeTypeInfo){
                sizeof(RStdNetDatagramResult),
                _Alignof(RStdNetDatagramResult),
                NULL,
                NULL,
            };
        }
        return (RRuntimeTypeInfo){
            sizeof(RStdNetUdpReceiveResult),
            _Alignof(RStdNetUdpReceiveResult),
            receive_result_move,
            receive_result_drop,
        };
    }
    datagram_panic();
}

static void datagram_payload_move(void *destination_pointer, void *source_pointer) {
    RLibraryNetDatagramPayload *destination = destination_pointer;
    RLibraryNetDatagramPayload *source = source_pointer;

    (void)memset(destination, 0, sizeof(*destination));
    destination->mode = source->mode;
    destination->immediate = source->immediate;
    destination->deadline = source->deadline;
    destination->immediate_error = source->immediate_error;
    destination->immediate_event_sequence = source->immediate_event_sequence;
    destination->socket_storage = source->socket_storage;
    destination->socket_handle = source->socket_handle;
    destination->queue_node = source->queue_node;
    destination->request = source->request;
    source->socket_storage = NULL;
    source->socket_handle = NULL;
    source->queue_node = NULL;
    source->request = NULL;
    destination->borrowed = source->borrowed;
    destination->unix_domain = source->unix_domain;
    if (source->borrowed) {
        destination->borrowed_data = source->borrowed_data;
        destination->borrowed_length = source->borrowed_length;
        destination->buffer_owned = 1;
    } else {
        if (source->staged_buffer == NULL) {
            datagram_panic();
        }
        move_array(&destination->buffer, source->staged_buffer);
        destination->buffer_owned = 1;
        source->staged_buffer = NULL;
    }
    atomic_init(&destination->state, 0U);
}

static _Bool udp_drained_locked(const RStdNetUdpSocketStorage *socket) {
    return socket->send_reservations == 0U && socket->receive_reservations == 0U &&
           socket->send_head == NULL && socket->send_tail == NULL && socket->receive_head == NULL &&
           socket->receive_tail == NULL;
}

static RLibraryNetUdpDrainAction udp_take_drain_locked(RStdNetUdpSocketStorage *socket) {
    RLibraryNetUdpDrainAction action = {0};

    if (socket->handle.terminal && udp_drained_locked(socket) && socket->drain != NULL) {
        action.drain = socket->drain;
        action.context = socket->drain_context;
        socket->drain = NULL;
        socket->drain_context = NULL;
    }
    return action;
}

static void udp_run_drain(RLibraryNetUdpDrainAction action) {
    if (action.drain != NULL) {
        action.drain(action.context);
    }
}

static void udp_queue_node_destroy(RLibraryNetUdpQueueNode *node) {
    RLibraryNetUdpDrainAction drain_action = {0};

    if (node == NULL) {
        return;
    }
    if (node->reserved) {
        RStdNetUdpSocketStorage *socket = node->socket;
        size_t *reservations;

        if (socket == NULL || pthread_mutex_lock(&socket->handle.mutex) != 0) {
            datagram_panic();
        }
        reservations = node->mode == R_LIBRARY_NET_DATAGRAM_SEND ? &socket->send_reservations
                                                                 : &socket->receive_reservations;
        if (!node->reserved || node->enqueued || *reservations == 0U) {
            (void)pthread_mutex_unlock(&socket->handle.mutex);
            datagram_panic();
        }
        *reservations -= 1U;
        node->reserved = 0;
        drain_action = udp_take_drain_locked(socket);
        if (pthread_mutex_unlock(&socket->handle.mutex) != 0) {
            datagram_panic();
        }
    }
    if (node->reserved || node->enqueued || node->previous != NULL || node->next != NULL) {
        datagram_panic();
    }
    if (pthread_mutex_destroy(&node->mutex) != 0) {
        datagram_panic();
    }
    r_runtime_allocator_deallocate(node, _Alignof(RLibraryNetUdpQueueNode));
    udp_run_drain(drain_action);
}

static void datagram_payload_drop(void *value) {
    RLibraryNetDatagramPayload *payload = value;

    if (payload->request != NULL) {
        if (payload->queue_node == NULL || pthread_mutex_lock(&payload->queue_node->mutex) != 0) {
            datagram_panic();
        }
        r_runtime_darwin_socket_datagram_abort(&payload->request);
        if (pthread_mutex_unlock(&payload->queue_node->mutex) != 0) {
            datagram_panic();
        }
    }
    udp_queue_node_destroy(payload->queue_node);
    payload->queue_node = NULL;
    r_library_internal_net_handle_release(payload->socket_handle);
    payload->socket_storage = NULL;
    payload->socket_handle = NULL;
    if (payload->buffer_owned) {
        if (!payload->borrowed) {
            r_runtime_array_destroy(&payload->buffer);
        }
        payload->buffer_owned = 0;
    }
}

static RLibraryNetUdpQueueNode *udp_queue_node_create(RRuntimeAllocator *allocator,
                                                      RLibraryNetDatagramMode mode) {
    RLibraryNetUdpQueueNode *node = NULL;

    if (allocator == NULL ||
        r_runtime_allocator_allocate(
            allocator, sizeof(*node), _Alignof(RLibraryNetUdpQueueNode), (void **)&node) !=
            R_RUNTIME_ALLOCATION_OK) {
        return NULL;
    }
    *node = (RLibraryNetUdpQueueNode){0};
    node->allocator = allocator;
    node->mode = mode;
    if (pthread_mutex_init(&node->mutex, NULL) != 0) {
        r_runtime_allocator_deallocate(node, _Alignof(RLibraryNetUdpQueueNode));
        return NULL;
    }
    return node;
}

static _Bool
udp_queue_reserve(RStdNetUdpSocketStorage *socket, RLibraryNetUdpQueueNode *node, int *descriptor) {
    size_t *reservations;
    size_t references;
    _Bool retained = 0;

    if (socket == NULL || node == NULL || descriptor == NULL ||
        pthread_mutex_lock(&socket->handle.mutex) != 0) {
        return 0;
    }
    reservations = node->mode == R_LIBRARY_NET_DATAGRAM_SEND ? &socket->send_reservations
                                                             : &socket->receive_reservations;
    references = atomic_load_explicit(&socket->handle.references, memory_order_relaxed);
    if (!socket->handle.terminal && !socket->handle.close_reserved &&
        socket->handle.descriptor >= 0 && *reservations != SIZE_MAX) {
        while (references != 0U && references != SIZE_MAX) {
            if (atomic_compare_exchange_weak_explicit(&socket->handle.references,
                                                      &references,
                                                      references + 1U,
                                                      memory_order_relaxed,
                                                      memory_order_relaxed)) {
                *reservations += 1U;
                node->socket = socket;
                node->reserved = 1;
                *descriptor = socket->handle.descriptor;
                retained = 1;
                break;
            }
        }
    }
    if (pthread_mutex_unlock(&socket->handle.mutex) != 0) {
        datagram_panic();
    }
    return retained;
}

static RLibraryNetUdpQueueNode **udp_head(RStdNetUdpSocketStorage *socket,
                                          RLibraryNetDatagramMode mode) {
    return mode == R_LIBRARY_NET_DATAGRAM_SEND ? &socket->send_head : &socket->receive_head;
}

static RLibraryNetUdpQueueNode **udp_tail(RStdNetUdpSocketStorage *socket,
                                          RLibraryNetDatagramMode mode) {
    return mode == R_LIBRARY_NET_DATAGRAM_SEND ? &socket->send_tail : &socket->receive_tail;
}

static void udp_queue_cancel_for_close_locked(RLibraryNetUdpQueueNode *node) {
    RLibraryNetDatagramPayload *payload = node->payload;

    if (node->close_sequence == UINT64_C(0) || node->close_cancel_dispatched) {
        return;
    }
    node->close_cancel_dispatched = 1;
    if (payload != NULL) {
        if (pthread_mutex_lock(&node->mutex) != 0) {
            datagram_panic();
        }
        if (payload->request != NULL) {
            (void)r_runtime_darwin_socket_datagram_cancel(payload->request, node->close_sequence);
        }
        if (pthread_mutex_unlock(&node->mutex) != 0) {
            datagram_panic();
        }
    }
}

static void udp_queue_activate_head_locked(RStdNetUdpSocketStorage *socket,
                                           RLibraryNetDatagramMode mode) {
    RLibraryNetUdpQueueNode *node = *udp_head(socket, mode);
    RLibraryNetDatagramPayload *payload;

    if (socket->handle.terminal || node == NULL || !node->ready || node->activation_claimed) {
        return;
    }
    payload = node->payload;
    if (payload == NULL || payload->execution == NULL) {
        datagram_panic();
    }
    node->activation_claimed = 1;
    if (pthread_mutex_lock(&node->mutex) != 0) {
        datagram_panic();
    }
    if (payload->request == NULL ||
        (!r_runtime_task_external_cancel_requested(payload->execution) &&
         !r_runtime_darwin_socket_datagram_activate(payload->request))) {
        (void)pthread_mutex_unlock(&node->mutex);
        datagram_panic();
    }
    if (pthread_mutex_unlock(&node->mutex) != 0) {
        datagram_panic();
    }
}

static void udp_queue_enqueue(RLibraryNetDatagramPayload *payload) {
    RStdNetUdpSocketStorage *socket = payload->socket_storage;
    RLibraryNetUdpQueueNode *node = payload->queue_node;
    RLibraryNetUdpQueueNode **head;
    RLibraryNetUdpQueueNode **tail;
    size_t *reservations;

    if (socket == NULL || node == NULL || node->socket != socket ||
        pthread_mutex_lock(&socket->handle.mutex) != 0) {
        datagram_panic();
    }
    head = udp_head(socket, node->mode);
    tail = udp_tail(socket, node->mode);
    reservations = node->mode == R_LIBRARY_NET_DATAGRAM_SEND ? &socket->send_reservations
                                                             : &socket->receive_reservations;
    if (!node->reserved || node->enqueued || node->previous != NULL || node->next != NULL ||
        *reservations == 0U || socket->handle.close_reserved || socket->handle.descriptor < 0 ||
        (socket->handle.terminal && socket->close_sequence == UINT64_C(0)) ||
        (*head == NULL) != (*tail == NULL)) {
        (void)pthread_mutex_unlock(&socket->handle.mutex);
        datagram_panic();
    }
    *reservations -= 1U;
    node->reserved = 0;
    node->payload = payload;
    if (socket->handle.terminal) {
        node->close_sequence = socket->close_sequence;
    }
    node->previous = *tail;
    if (*tail == NULL) {
        *head = node;
    } else {
        (*tail)->next = node;
    }
    *tail = node;
    node->enqueued = 1;
    if (pthread_mutex_unlock(&socket->handle.mutex) != 0) {
        datagram_panic();
    }
}

static void udp_queue_mark_ready(RLibraryNetDatagramPayload *payload) {
    RStdNetUdpSocketStorage *socket = payload->socket_storage;
    RLibraryNetUdpQueueNode *node = payload->queue_node;

    if (socket == NULL || node == NULL || pthread_mutex_lock(&socket->handle.mutex) != 0) {
        datagram_panic();
    }
    if (!node->enqueued || node->socket != socket || node->payload != payload || node->ready) {
        (void)pthread_mutex_unlock(&socket->handle.mutex);
        datagram_panic();
    }
    node->ready = 1;
    if (node->close_sequence != UINT64_C(0)) {
        udp_queue_cancel_for_close_locked(node);
    } else {
        if (pthread_mutex_lock(&node->mutex) != 0) {
            (void)pthread_mutex_unlock(&socket->handle.mutex);
            datagram_panic();
        }
        if (payload->request == NULL ||
            (!r_runtime_task_external_cancel_requested(payload->execution) &&
             !r_runtime_darwin_socket_datagram_arm_deadline(payload->request))) {
            (void)pthread_mutex_unlock(&node->mutex);
            (void)pthread_mutex_unlock(&socket->handle.mutex);
            datagram_panic();
        }
        if (pthread_mutex_unlock(&node->mutex) != 0) {
            (void)pthread_mutex_unlock(&socket->handle.mutex);
            datagram_panic();
        }
    }
    udp_queue_activate_head_locked(socket, node->mode);
    if (pthread_mutex_unlock(&socket->handle.mutex) != 0) {
        datagram_panic();
    }
}

static void udp_queue_remove_and_advance(RLibraryNetDatagramPayload *payload) {
    RStdNetUdpSocketStorage *socket = payload->socket_storage;
    RLibraryNetUdpQueueNode *node = payload->queue_node;
    RLibraryNetUdpQueueNode **head;
    RLibraryNetUdpQueueNode **tail;
    RLibraryNetUdpDrainAction drain_action;
    _Bool was_head;

    if (socket == NULL || node == NULL || pthread_mutex_lock(&socket->handle.mutex) != 0) {
        datagram_panic();
    }
    head = udp_head(socket, node->mode);
    tail = udp_tail(socket, node->mode);
    was_head = *head == node;
    if (!node->enqueued || node->socket != socket || node->payload != payload ||
        (node->previous == NULL && *head != node) || (node->next == NULL && *tail != node)) {
        (void)pthread_mutex_unlock(&socket->handle.mutex);
        datagram_panic();
    }
    if (node->previous == NULL) {
        *head = node->next;
    } else {
        node->previous->next = node->next;
    }
    if (node->next == NULL) {
        *tail = node->previous;
    } else {
        node->next->previous = node->previous;
    }
    node->previous = NULL;
    node->next = NULL;
    node->enqueued = 0;
    if (was_head) {
        udp_queue_activate_head_locked(socket, node->mode);
    }
    drain_action = udp_take_drain_locked(socket);
    if (pthread_mutex_unlock(&socket->handle.mutex) != 0) {
        datagram_panic();
    }
    udp_run_drain(drain_action);
}

static RStdNetError native_result_error(RRuntimeDarwinSocketDatagramResult result) {
    if (result.terminal_event == R_RUNTIME_DARWIN_SOCKET_TERMINAL_CANCELLED) {
        return net_error(R_STD_NET_ERROR_CANCELLED, INT64_C(0));
    }
    if (result.terminal_event == R_RUNTIME_DARWIN_SOCKET_TERMINAL_TIMED_OUT) {
        return net_error(R_STD_NET_ERROR_TIMED_OUT, INT64_C(0));
    }
    if (result.native_error != 0) {
        return r_library_internal_net_error_from_native(result.native_error);
    }
    return net_error(R_STD_NET_ERROR_OTHER, INT64_C(0));
}

static void move_payload_buffer(RLibraryNetDatagramPayload *payload, RRuntimeArray *destination) {
    if (!payload->buffer_owned || payload->borrowed) {
        datagram_panic();
    }
    move_array(destination, &payload->buffer);
    payload->buffer_owned = 0;
}

/* Staged storage as submitted natively: the owner's bytes or the borrowed view. */
static void *staged_data(const RLibraryNetDatagramPayload *payload) {
    return payload->borrowed ? payload->borrowed_data : payload->staged_buffer->data;
}

static size_t staged_length(const RLibraryNetDatagramPayload *payload) {
    return payload->borrowed ? payload->borrowed_length : payload->staged_buffer->length;
}

static void fill_borrowed_failure(RLibraryNetDatagramPayload *payload, RStdNetError error) {
    if (!payload->buffer_owned || !payload->borrowed) {
        datagram_panic();
    }
    if (payload->mode == R_LIBRARY_NET_DATAGRAM_SEND) {
        RStdNetVoidResult *result = payload->result;

        *result = (RStdNetVoidResult){0};
        result->r_tag = UINT32_C(1);
        result->r_payload.r_error_00000001 = error;
    } else if (payload->unix_domain) {
        RStdNetUnixMessageResult *result = payload->result;

        *result = (RStdNetUnixMessageResult){0};
        result->r_tag = UINT32_C(1);
        result->r_payload.r_error_00000001 = error;
    } else {
        RStdNetDatagramResult *result = payload->result;

        *result = (RStdNetDatagramResult){0};
        result->r_tag = UINT32_C(1);
        result->r_payload.r_error_00000001 = error;
    }
    payload->buffer_owned = 0;
}

static void fill_immediate_result(RLibraryNetDatagramPayload *payload) {
    if (payload->borrowed) {
        fill_borrowed_failure(payload, payload->immediate_error);
        return;
    }
    if (payload->mode == R_LIBRARY_NET_DATAGRAM_SEND) {
        RStdNetUdpSendResult *result = payload->result;

        *result = (RStdNetUdpSendResult){0};
        result->kind = R_STD_NET_UDP_SEND_RESULT_FAILED;
        result->error = payload->immediate_error;
        move_payload_buffer(payload, &result->buffer);
    } else {
        RStdNetUdpReceiveResult *result = payload->result;

        *result = (RStdNetUdpReceiveResult){0};
        result->kind = R_STD_NET_UDP_RECEIVE_RESULT_FAILED;
        result->error = payload->immediate_error;
        move_payload_buffer(payload, &result->buffer);
    }
}

static void fill_borrowed_native_result(RLibraryNetDatagramPayload *payload, _Bool success) {
    const RRuntimeDarwinSocketDatagramResult native_result = payload->native_result;
    RStdNetSocketAddress peer = {0};

    if (!success) {
        fill_borrowed_failure(payload, native_result_error(native_result));
        return;
    }
    if (payload->mode == R_LIBRARY_NET_DATAGRAM_SEND) {
        RStdNetVoidResult *result = payload->result;

        if (!payload->buffer_owned) {
            datagram_panic();
        }
        *result = (RStdNetVoidResult){0};
        payload->buffer_owned = 0;
        return;
    }
    if (payload->unix_domain) {
        RStdNetUnixMessageResult *result = payload->result;

        if (!payload->buffer_owned ||
            !r_runtime_darwin_socket_datagram_copy_received(
                payload->request, payload->borrowed_data, payload->borrowed_length)) {
            datagram_panic();
        }
        *result = (RStdNetUnixMessageResult){0};
        result->r_payload.r_value.count = native_result.count;
        result->r_payload.r_value.truncated = native_result.truncated;
        payload->buffer_owned = 0;
        return;
    }
    if (!r_library_internal_net_address_from_native(
            (const struct sockaddr *)&native_result.peer, native_result.peer_length, &peer)) {
        fill_borrowed_failure(payload, net_error(R_STD_NET_ERROR_INVALID_ADDRESS, INT64_C(0)));
        return;
    }
    {
        RStdNetDatagramResult *result = payload->result;

        if (!payload->buffer_owned) {
            datagram_panic();
        }
        if (!r_runtime_darwin_socket_datagram_copy_received(
                payload->request, payload->borrowed_data, payload->borrowed_length)) {
            datagram_panic();
        }
        *result = (RStdNetDatagramResult){0};
        result->r_payload.r_value.count = native_result.count;
        result->r_payload.r_value.peer = peer;
        result->r_payload.r_value.truncated = native_result.truncated;
        payload->buffer_owned = 0;
    }
}

static void fill_native_result(RLibraryNetDatagramPayload *payload) {
    const RRuntimeDarwinSocketDatagramResult native_result = payload->native_result;
    const _Bool success = native_result.terminal_event == R_RUNTIME_DARWIN_SOCKET_TERMINAL_NATIVE &&
                          native_result.native_error == 0 && native_result.completed;

    if (payload->borrowed) {
        fill_borrowed_native_result(payload, success);
        return;
    }
    if (payload->mode == R_LIBRARY_NET_DATAGRAM_SEND) {
        RStdNetUdpSendResult *result = payload->result;

        *result = (RStdNetUdpSendResult){0};
        result->kind = success ? R_STD_NET_UDP_SEND_RESULT_SENT : R_STD_NET_UDP_SEND_RESULT_FAILED;
        if (!success) {
            result->error = native_result_error(native_result);
        }
        move_payload_buffer(payload, &result->buffer);
        return;
    }
    {
        RStdNetUdpReceiveResult *result = payload->result;
        RStdNetSocketAddress peer = {0};
        _Bool valid_peer = 0;

        if (success) {
            valid_peer = r_library_internal_net_address_from_native(
                (const struct sockaddr *)&native_result.peer, native_result.peer_length, &peer);
        }
        *result = (RStdNetUdpReceiveResult){0};
        if (success && valid_peer) {
            if (!r_runtime_darwin_socket_datagram_copy_received(
                    payload->request, payload->buffer.data, payload->buffer.length)) {
                datagram_panic();
            }
            result->kind = R_STD_NET_UDP_RECEIVE_RESULT_RECEIVED;
            result->count = native_result.count;
            result->peer = peer;
            result->truncated = native_result.truncated;
        } else {
            result->kind = R_STD_NET_UDP_RECEIVE_RESULT_FAILED;
            result->error = success ? net_error(R_STD_NET_ERROR_INVALID_ADDRESS, INT64_C(0))
                                    : native_result_error(native_result);
        }
        move_payload_buffer(payload, &result->buffer);
    }
}

static void datagram_try_finalize(RLibraryNetDatagramPayload *payload) {
    unsigned int state;

    for (;;) {
        unsigned int desired;
        const uint64_t cancellation_sequence =
            r_runtime_task_external_cancellation_sequence(payload->execution);

        state = atomic_load_explicit(&payload->state, memory_order_acquire);
        if ((state & R_LIBRARY_NET_DATAGRAM_FINALIZED) != 0U ||
            (state & R_LIBRARY_NET_DATAGRAM_CALLBACK_RELEASED) == 0U ||
            (cancellation_sequence != UINT64_C(0) &&
             (state & R_LIBRARY_NET_DATAGRAM_CANCEL_REPORTED) == 0U)) {
            return;
        }
        desired = state | R_LIBRARY_NET_DATAGRAM_FINALIZED;
        if (atomic_compare_exchange_weak_explicit(
                &payload->state, &state, desired, memory_order_acq_rel, memory_order_acquire)) {
            state = desired;
            break;
        }
    }
    if ((state & R_LIBRARY_NET_DATAGRAM_COMPLETION_SELECTED) == 0U &&
        r_runtime_task_external_cancellation_sequence(payload->execution) != UINT64_C(0)) {
        datagram_testing_record_cancel_acknowledgement();
    }
    r_runtime_task_external_acknowledge(payload->execution);
}

static void datagram_native_completed(RRuntimeDarwinSocketDatagram *request,
                                      void *context_pointer) {
    RLibraryNetDatagramPayload *payload = context_pointer;
    RRuntimeDarwinSocketDatagramResult native_result;
    unsigned int bits = R_LIBRARY_NET_DATAGRAM_CALLBACK_RELEASED;
    unsigned int previous;
    _Bool completion_selected;

    native_result = r_runtime_darwin_socket_datagram_result(request);
    if (native_result.terminal_event_sequence == UINT64_C(0) ||
        (payload->mode == R_LIBRARY_NET_DATAGRAM_SEND &&
         native_result.operation != R_RUNTIME_DARWIN_SOCKET_DATAGRAM_SEND) ||
        (payload->mode == R_LIBRARY_NET_DATAGRAM_RECEIVE &&
         native_result.operation != R_RUNTIME_DARWIN_SOCKET_DATAGRAM_RECEIVE)) {
        datagram_panic();
    }
    completion_selected = r_runtime_task_external_try_select_completion_at(
        payload->execution, native_result.terminal_event_sequence);
    if (!completion_selected && payload->mode == R_LIBRARY_NET_DATAGRAM_SEND &&
        native_result.terminal_event == R_RUNTIME_DARWIN_SOCKET_TERMINAL_NATIVE &&
        native_result.native_error == 0 && native_result.completed) {
        completion_selected =
            r_runtime_task_external_select_terminal_completion(payload->execution);
    }
    if (payload->queue_node == NULL || pthread_mutex_lock(&payload->queue_node->mutex) != 0) {
        datagram_panic();
    }
    if (payload->request != request) {
        (void)pthread_mutex_unlock(&payload->queue_node->mutex);
        datagram_panic();
    }
    payload->native_result = native_result;
    if (completion_selected) {
        fill_native_result(payload);
        bits |= R_LIBRARY_NET_DATAGRAM_COMPLETION_SELECTED;
    }
    payload->request = NULL;
    if (pthread_mutex_unlock(&payload->queue_node->mutex) != 0) {
        datagram_panic();
    }
    r_runtime_darwin_socket_datagram_release(request);
    udp_queue_remove_and_advance(payload);
    previous = atomic_fetch_or_explicit(&payload->state, bits, memory_order_acq_rel);
    if ((previous &
         (R_LIBRARY_NET_DATAGRAM_CALLBACK_RELEASED | R_LIBRARY_NET_DATAGRAM_COMPLETION_SELECTED |
          R_LIBRARY_NET_DATAGRAM_FINALIZED)) != 0U) {
        datagram_panic();
    }
    datagram_try_finalize(payload);
}

static void datagram_complete_immediate(RLibraryNetDatagramPayload *payload,
                                        RRuntimeTaskExternalExecution *execution,
                                        void *result_pointer) {
    RLibraryNetHandleStorage *socket_handle;
    _Bool selected;

    if (payload->immediate_event_sequence == UINT64_C(0) || payload->request != NULL) {
        datagram_panic();
    }
    payload->execution = execution;
    payload->result = result_pointer;
    udp_queue_node_destroy(payload->queue_node);
    payload->queue_node = NULL;
    socket_handle = payload->socket_handle;
    payload->socket_storage = NULL;
    payload->socket_handle = NULL;
    r_library_internal_net_handle_release(socket_handle);
    selected = r_runtime_task_external_try_select_completion_at(execution,
                                                                payload->immediate_event_sequence);
    r_runtime_task_external_start_ready(execution);
    if (selected) {
        fill_immediate_result(payload);
        atomic_store_explicit(&payload->state,
                              R_LIBRARY_NET_DATAGRAM_CALLBACK_RELEASED |
                                  R_LIBRARY_NET_DATAGRAM_COMPLETION_SELECTED |
                                  R_LIBRARY_NET_DATAGRAM_FINALIZED,
                              memory_order_release);
        r_runtime_task_external_acknowledge(execution);
    }
}

static void datagram_external_start(RRuntimeTaskExternalExecution *execution,
                                    void *payload_pointer,
                                    void *result_pointer) {
    RLibraryNetDatagramPayload *payload = payload_pointer;

    if (payload->immediate != R_LIBRARY_NET_DATAGRAM_IMMEDIATE_NONE) {
        datagram_complete_immediate(payload, execution, result_pointer);
        return;
    }
    if (payload->request == NULL || payload->queue_node == NULL ||
        payload->socket_storage == NULL) {
        datagram_panic();
    }
    payload->execution = execution;
    payload->result = result_pointer;
    if (!r_runtime_darwin_socket_datagram_bind(
            payload->request, datagram_native_completed, payload)) {
        datagram_panic();
    }
    udp_queue_enqueue(payload);
    r_runtime_task_external_start_ready(execution);
    udp_queue_mark_ready(payload);
}

static void datagram_external_cancel(RRuntimeTaskExternalExecution *execution,
                                     void *payload_pointer) {
    RLibraryNetDatagramPayload *payload = payload_pointer;
    unsigned int previous;
    const uint64_t cancellation_sequence = r_runtime_task_external_cancellation_sequence(execution);

    if (payload->execution != execution || cancellation_sequence == UINT64_C(0)) {
        datagram_panic();
    }
    if (payload->immediate != R_LIBRARY_NET_DATAGRAM_IMMEDIATE_NONE) {
        datagram_testing_record_cancel_reported();
        r_runtime_task_external_acknowledge(execution);
        return;
    }
    if (payload->queue_node == NULL || pthread_mutex_lock(&payload->queue_node->mutex) != 0) {
        datagram_panic();
    }
    if (payload->request != NULL) {
        (void)r_runtime_darwin_socket_datagram_cancel(payload->request, cancellation_sequence);
    }
    if (pthread_mutex_unlock(&payload->queue_node->mutex) != 0) {
        datagram_panic();
    }
    previous = atomic_fetch_or_explicit(
        &payload->state, R_LIBRARY_NET_DATAGRAM_CANCEL_REPORTED, memory_order_acq_rel);
    if ((previous & (R_LIBRARY_NET_DATAGRAM_CANCEL_REPORTED | R_LIBRARY_NET_DATAGRAM_FINALIZED)) !=
        0U) {
        datagram_panic();
    }
    datagram_testing_record_cancel_reported();
    datagram_try_finalize(payload);
}

static void datagram_set_immediate(RLibraryNetDatagramPayload *payload, RStdNetError error) {
    payload->immediate = R_LIBRARY_NET_DATAGRAM_IMMEDIATE_FAILED;
    payload->immediate_error = error;
    payload->immediate_event_sequence = r_runtime_darwin_event_sequence_next();
}

static RRuntimeTaskStartStatus datagram_reserve_native(RLibraryNetDatagramPayload *payload,
                                                       RRuntimeAllocator *allocator,
                                                       int descriptor,
                                                       RStdNetSocketAddress peer) {
    struct sockaddr_storage native_peer = {0};
    RRuntimeDarwinSocketDatagramPrepareResult prepared;
    RLibraryNetDeadlineStatus deadline_status;
    RStdNetError deadline_error = {0};
    socklen_t native_peer_length = 0U;
    uint64_t timeout_nanoseconds = 0U;
    int domain = 0;

    deadline_status = r_library_internal_net_deadline_timeout(
        payload->deadline, &timeout_nanoseconds, &deadline_error);
    if (deadline_status != R_LIBRARY_NET_DEADLINE_READY) {
        datagram_set_immediate(payload, deadline_error);
        return R_RUNTIME_TASK_START_OK;
    }
    if (payload->mode == R_LIBRARY_NET_DATAGRAM_SEND && !payload->unix_domain &&
        !r_library_internal_net_address_to_native(
            peer, &native_peer, &native_peer_length, &domain)) {
        datagram_set_immediate(payload, net_error(R_STD_NET_ERROR_INVALID_ADDRESS, INT64_C(0)));
        return R_RUNTIME_TASK_START_OK;
    }
    (void)domain;
    if (payload->mode == R_LIBRARY_NET_DATAGRAM_SEND) {
        /* A Unix-domain send goes to the connected peer: no address, length zero. */
        prepared = r_runtime_darwin_socket_datagram_send_prepare(
            allocator,
            descriptor,
            payload->unix_domain ? NULL : (const struct sockaddr *)&native_peer,
            payload->unix_domain ? 0U : native_peer_length,
            staged_data(payload),
            staged_length(payload),
            timeout_nanoseconds);
    } else {
        prepared = r_runtime_darwin_socket_datagram_receive_prepare(
            allocator, descriptor, staged_length(payload), timeout_nanoseconds);
    }
    switch (prepared.status) {
    case R_RUNTIME_DARWIN_SOCKET_PREPARE_OK:
        payload->request = prepared.request;
        return R_RUNTIME_TASK_START_OK;
    case R_RUNTIME_DARWIN_SOCKET_PREPARE_ALLOCATION_FAILED:
        return R_RUNTIME_TASK_START_ALLOCATION_FAILED;
    case R_RUNTIME_DARWIN_SOCKET_PREPARE_NATIVE_FAILED:
        datagram_set_immediate(payload,
                               r_library_internal_net_error_from_native(prepared.native_error));
        return R_RUNTIME_TASK_START_OK;
    case R_RUNTIME_DARWIN_SOCKET_PREPARE_INVALID:
        datagram_panic();
    }
    datagram_panic();
}

/*
 * The staged payload already names either the owner (staged_buffer) or the borrowed view; every
 * failure before commit leaves that storage untouched because buffer_owned is still clear.
 */
static RStdNetTaskStartResult datagram_start(RStdNetUdpSocketStorage *socket_storage,
                                             RStdNetSocketAddress peer,
                                             RLibraryNetDatagramPayload *staged,
                                             RStdNetDeadline deadline,
                                             RLibraryNetDatagramMode mode) {
    const RRuntimeTypeInfo payload_type = {
        sizeof(RLibraryNetDatagramPayload),
        _Alignof(RLibraryNetDatagramPayload),
        datagram_payload_move,
        datagram_payload_drop,
    };
    RLibraryNetDatagramPayload payload = *staged;
    RRuntimeTaskPrepareResult task_preparation;
    RRuntimeTaskStartResult started;
    RRuntimeAllocator *allocator;
    RRuntimeTaskStartStatus reserve_status;
    RStdNetTaskStartResult result = {0};
    int descriptor = -1;

    task_preparation = r_runtime_task_external_start_prepare(
        payload_type,
        datagram_result_type(mode, payload.borrowed, payload.unix_domain),
        datagram_external_start,
        datagram_external_cancel);
    if (task_preparation.status != R_RUNTIME_TASK_START_OK) {
        return task_start_failure(task_preparation.status);
    }
    allocator = r_runtime_task_start_allocator(task_preparation.transaction);
    if (allocator == NULL) {
        r_runtime_task_start_abort(&task_preparation.transaction);
        datagram_panic();
    }
    payload.mode = mode;
    payload.deadline = deadline;
    payload.queue_node = udp_queue_node_create(allocator, mode);
    if (payload.queue_node == NULL) {
        r_runtime_task_start_abort(&task_preparation.transaction);
        return task_start_failure(R_RUNTIME_TASK_START_ALLOCATION_FAILED);
    }
    if (!udp_queue_reserve(socket_storage, payload.queue_node, &descriptor)) {
        datagram_set_immediate(&payload, net_error(R_STD_NET_ERROR_CLOSED, INT64_C(0)));
    } else {
        payload.socket_storage = socket_storage;
        payload.socket_handle = &socket_storage->handle;
        reserve_status = datagram_reserve_native(&payload, allocator, descriptor, peer);
        if (reserve_status != R_RUNTIME_TASK_START_OK) {
            r_runtime_task_start_abort(&task_preparation.transaction);
            datagram_payload_drop(&payload);
            return task_start_failure(reserve_status);
        }
    }
    started = r_runtime_task_start_commit(&task_preparation.transaction, &payload);
    if (started.status != R_RUNTIME_TASK_START_OK) {
        datagram_payload_drop(&payload);
        return task_start_failure(started.status);
    }
    result.is_ok = 1;
    result.task = started.task;
    return result;
}

RStdNetTaskStartResult r_library_internal_net_udp_send_to(const RStdNetUdpSocket *socket,
                                                          RStdNetSocketAddress peer,
                                                          RRuntimeArray *buffer,
                                                          RStdNetDeadline deadline) {
    RLibraryNetDatagramPayload staged = {0};

    staged.staged_buffer = buffer;
    return datagram_start(socket->storage, peer, &staged, deadline, R_LIBRARY_NET_DATAGRAM_SEND);
}

RStdNetTaskStartResult r_library_internal_net_udp_receive_from(const RStdNetUdpSocket *socket,
                                                               RRuntimeArray *buffer,
                                                               RStdNetDeadline deadline) {
    RLibraryNetDatagramPayload staged = {0};

    staged.staged_buffer = buffer;
    return datagram_start(socket->storage,
                          (RStdNetSocketAddress){0},
                          &staged,
                          deadline,
                          R_LIBRARY_NET_DATAGRAM_RECEIVE);
}

/* The runtime borrows the send source through an immutable native view and never mutates it. */
static uint8_t *borrowed_send_data(const uint8_t *data) {
    return (uint8_t *)(uintptr_t)data;
}

RStdNetTaskStartResult r_library_internal_net_udp_send_from(const RStdNetUdpSocket *socket,
                                                            RStdNetSocketAddress peer,
                                                            RStdNetConstBytes source,
                                                            RStdNetDeadline deadline) {
    RLibraryNetDatagramPayload staged = {0};

    if (source.length != 0U && source.data == NULL) {
        datagram_panic();
    }
    staged.borrowed = 1;
    staged.borrowed_data = borrowed_send_data(source.data);
    staged.borrowed_length = source.length;
    return datagram_start(socket->storage, peer, &staged, deadline, R_LIBRARY_NET_DATAGRAM_SEND);
}

RStdNetTaskStartResult r_library_internal_net_udp_receive_into(const RStdNetUdpSocket *socket,
                                                               RStdNetMutableBytes target,
                                                               RStdNetDeadline deadline) {
    RLibraryNetDatagramPayload staged = {0};

    if (target.length != 0U && target.data == NULL) {
        datagram_panic();
    }
    staged.borrowed = 1;
    staged.borrowed_data = target.data;
    staged.borrowed_length = target.length;
    return datagram_start(socket->storage,
                          (RStdNetSocketAddress){0},
                          &staged,
                          deadline,
                          R_LIBRARY_NET_DATAGRAM_RECEIVE);
}

RStdNetTaskStartResult r_library_internal_net_unix_send_from(const RStdNetUnixDatagram *socket,
                                                             RStdNetConstBytes source,
                                                             RStdNetDeadline deadline) {
    RLibraryNetDatagramPayload staged = {0};

    if (source.length != 0U && source.data == NULL) {
        datagram_panic();
    }
    staged.borrowed = 1;
    staged.unix_domain = 1;
    staged.borrowed_data = borrowed_send_data(source.data);
    staged.borrowed_length = source.length;
    return datagram_start(
        socket->storage, (RStdNetSocketAddress){0}, &staged, deadline, R_LIBRARY_NET_DATAGRAM_SEND);
}

RStdNetTaskStartResult r_library_internal_net_unix_receive_into(const RStdNetUnixDatagram *socket,
                                                                RStdNetMutableBytes target,
                                                                RStdNetDeadline deadline) {
    RLibraryNetDatagramPayload staged = {0};

    if (target.length != 0U && target.data == NULL) {
        datagram_panic();
    }
    staged.borrowed = 1;
    staged.unix_domain = 1;
    staged.borrowed_data = target.data;
    staged.borrowed_length = target.length;
    return datagram_start(socket->storage,
                          (RStdNetSocketAddress){0},
                          &staged,
                          deadline,
                          R_LIBRARY_NET_DATAGRAM_RECEIVE);
}

_Bool r_library_internal_net_udp_socket_preflight_close(RStdNetUdpSocketStorage *socket,
                                                        RStdNetError *preexisting_error,
                                                        _Bool *preexisting_failure) {
    int status;
    int native_code;

    if (socket == NULL || preexisting_error == NULL || preexisting_failure == NULL ||
        pthread_mutex_lock(&socket->handle.mutex) != 0) {
        return 0;
    }
    if (socket->handle.kind != R_LIBRARY_NET_HANDLE_UDP_SOCKET || socket->handle.terminal ||
        socket->handle.close_reserved || socket->handle.descriptor < 0) {
        (void)pthread_mutex_unlock(&socket->handle.mutex);
        return 0;
    }
    do {
        errno = 0;
        status = fcntl(socket->handle.descriptor, F_GETFD);
        native_code = status < 0 ? errno : 0;
    } while (status < 0 && native_code == EINTR);
    *preexisting_failure = status < 0;
    *preexisting_error =
        status < 0 ? r_library_internal_net_error_from_native(native_code) : (RStdNetError){0};
    if (pthread_mutex_unlock(&socket->handle.mutex) != 0) {
        datagram_panic();
    }
    return 1;
}

_Bool r_library_internal_net_udp_socket_mark_closing(RStdNetUdpSocketStorage *socket) {
    RLibraryNetUdpQueueNode *node;
    uint64_t close_sequence;

    if (socket == NULL || pthread_mutex_lock(&socket->handle.mutex) != 0) {
        return 0;
    }
    if (socket->handle.kind != R_LIBRARY_NET_HANDLE_UDP_SOCKET || socket->handle.terminal ||
        socket->handle.close_reserved || socket->handle.descriptor < 0 ||
        socket->close_sequence != UINT64_C(0) || socket->drain != NULL) {
        (void)pthread_mutex_unlock(&socket->handle.mutex);
        return 0;
    }
    close_sequence = r_runtime_darwin_event_sequence_next();
    socket->handle.close_reserved = 1;
    socket->handle.terminal = 1;
    socket->close_sequence = close_sequence;
    for (node = socket->send_head; node != NULL; node = node->next) {
        if (node->close_sequence != UINT64_C(0)) {
            (void)pthread_mutex_unlock(&socket->handle.mutex);
            datagram_panic();
        }
        node->close_sequence = close_sequence;
    }
    for (node = socket->receive_head; node != NULL; node = node->next) {
        if (node->close_sequence != UINT64_C(0)) {
            (void)pthread_mutex_unlock(&socket->handle.mutex);
            datagram_panic();
        }
        node->close_sequence = close_sequence;
    }
    if (pthread_mutex_unlock(&socket->handle.mutex) != 0) {
        datagram_panic();
    }
    return 1;
}

void r_library_internal_net_udp_socket_drain(RStdNetUdpSocketStorage *socket,
                                             RLibraryNetUdpDrainFn drain,
                                             void *context) {
    RLibraryNetUdpQueueNode *node;
    _Bool already_drained;

    if (socket == NULL || drain == NULL || pthread_mutex_lock(&socket->handle.mutex) != 0) {
        datagram_panic();
    }
    if (!socket->handle.terminal || !socket->handle.close_reserved ||
        socket->close_sequence == UINT64_C(0) || socket->drain != NULL) {
        (void)pthread_mutex_unlock(&socket->handle.mutex);
        datagram_panic();
    }
    already_drained = udp_drained_locked(socket);
    if (!already_drained) {
        socket->drain = drain;
        socket->drain_context = context;
        for (node = socket->send_head; node != NULL; node = node->next) {
            if (node->close_sequence != socket->close_sequence) {
                (void)pthread_mutex_unlock(&socket->handle.mutex);
                datagram_panic();
            }
            udp_queue_cancel_for_close_locked(node);
        }
        for (node = socket->receive_head; node != NULL; node = node->next) {
            if (node->close_sequence != socket->close_sequence) {
                (void)pthread_mutex_unlock(&socket->handle.mutex);
                datagram_panic();
            }
            udp_queue_cancel_for_close_locked(node);
        }
    }
    if (pthread_mutex_unlock(&socket->handle.mutex) != 0) {
        datagram_panic();
    }
    if (already_drained) {
        drain(context);
    }
}

int r_library_internal_net_udp_socket_take_close_descriptor(RStdNetUdpSocketStorage *socket) {
    int descriptor;

    if (socket == NULL || pthread_mutex_lock(&socket->handle.mutex) != 0) {
        datagram_panic();
    }
    if (!socket->handle.terminal || !socket->handle.close_reserved ||
        socket->close_sequence == UINT64_C(0) || !udp_drained_locked(socket) ||
        socket->drain != NULL || socket->handle.descriptor < 0) {
        (void)pthread_mutex_unlock(&socket->handle.mutex);
        datagram_panic();
    }
    descriptor = socket->handle.descriptor;
    socket->handle.descriptor = -1;
    if (pthread_mutex_unlock(&socket->handle.mutex) != 0) {
        datagram_panic();
    }
    return descriptor;
}

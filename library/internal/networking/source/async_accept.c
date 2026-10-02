#include "r_library_net_internal.h"

#include "r_runtime_0_1.h"
#include "r_runtime_darwin_event.h"
#include "r_runtime_darwin_socket.h"
#include "r_runtime_task.h"
#include "r_runtime_type.h"

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

typedef struct RLibraryNetAcceptControl RLibraryNetAcceptControl;

struct RLibraryNetAcceptQueueNode {
    RRuntimeAllocator *allocator;
    RStdNetTcpListenerStorage *listener;
    RLibraryNetAcceptControl *control;
    RLibraryNetAcceptQueueNode *previous;
    RLibraryNetAcceptQueueNode *next;
    uint64_t listener_close_sequence;
    _Bool reserved;
    _Bool enqueued;
    _Bool ready;
    _Bool activation_claimed;
    _Bool listener_close_cancel_dispatched;
};

struct RLibraryNetAcceptControl {
    RRuntimeAllocator *allocator;
    pthread_mutex_t mutex;
    RRuntimeDarwinSocketAccept *request;
    RRuntimeTaskExternalExecution *execution;
    void *result;
    RRuntimeDarwinSocketAcceptResult native_result;
    int accepted_descriptor;
    _Bool callback_released;
    _Bool cancel_reported;
    _Bool completion_selected;
    _Bool finalized;
};

typedef struct RLibraryNetAcceptPayload {
    RStdNetTcpStreamStorage *stream_storage;
    RStdNetTcpListenerStorage *listener_storage;
    RLibraryNetHandleStorage *listener_handle;
    RLibraryNetAcceptControl *control;
    RLibraryNetAcceptQueueNode *queue_node;
    RStdNetError immediate_error;
    uint64_t immediate_event_sequence;
    _Bool immediate;
    /* R-SLIB-NET-0014: a Unix-domain accept publishes a stream without a peer address. */
    _Bool unix_domain;
} RLibraryNetAcceptPayload;

typedef struct RLibraryNetAcceptFinalizeAction {
    RRuntimeDarwinSocketAcceptResult native_result;
    int accepted_descriptor;
    _Bool ready;
    _Bool completion_selected;
} RLibraryNetAcceptFinalizeAction;

typedef struct RLibraryNetAcceptDrainAction {
    RLibraryNetListenerDrainFn drain;
    void *context;
} RLibraryNetAcceptDrainAction;

static void accept_panic(void) {
    r_runtime_panic(R_RUNTIME_PANIC_CONTRACT_VIOLATION, (RRuntimeSourceSpan){0U, 0U, 0U});
}

static RStdNetError net_error(RStdNetErrorCode code, int64_t native_code) {
    return (RStdNetError){code, native_code};
}

static RStdNetTaskStartResult start_failure(RRuntimeTaskStartStatus status) {
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
        accept_panic();
    }
    accept_panic();
    return result;
}

static void connection_result_move(void *destination_pointer, void *source_pointer) {
    RStdNetTcpConnectionResult *destination = destination_pointer;
    RStdNetTcpConnectionResult *source = source_pointer;

    *destination = *source;
    if (source->r_tag == UINT32_C(0)) {
        source->r_payload.r_ok.stream.storage = NULL;
    }
}

static void connection_result_drop(void *value) {
    RStdNetTcpConnectionResult *result = value;

    if (result->r_tag == UINT32_C(0)) {
        r_library_internal_net_tcp_stream_drop(&result->r_payload.r_ok.stream);
    }
}

static void unix_stream_result_move(void *destination_pointer, void *source_pointer) {
    RStdNetUnixStreamResult *destination = destination_pointer;
    RStdNetUnixStreamResult *source = source_pointer;

    *destination = *source;
    if (source->r_tag == UINT32_C(0)) {
        source->r_payload.r_ok.storage = NULL;
    }
}

static void unix_stream_result_drop(void *value) {
    RStdNetUnixStreamResult *result = value;

    if (result->r_tag == UINT32_C(0)) {
        r_library_internal_net_unix_stream_drop(&result->r_payload.r_ok);
    }
}

static void accept_result_error(void *result_pointer, _Bool unix_domain, RStdNetError error) {
    if (unix_domain) {
        RStdNetUnixStreamResult *result = result_pointer;

        *result = (RStdNetUnixStreamResult){0};
        result->r_tag = UINT32_C(1);
        result->r_payload.r_error_00000001 = error;
    } else {
        RStdNetTcpConnectionResult *result = result_pointer;

        *result = (RStdNetTcpConnectionResult){0};
        result->r_tag = UINT32_C(1);
        result->r_payload.r_error_00000001 = error;
    }
}

static void accept_control_destroy(RLibraryNetAcceptControl *control) {
    RRuntimeDarwinSocketAccept *request = NULL;

    if (control == NULL) {
        return;
    }
    if (pthread_mutex_lock(&control->mutex) != 0) {
        accept_panic();
    }
    request = control->request;
    control->request = NULL;
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        accept_panic();
    }
    if (request != NULL) {
        r_runtime_darwin_socket_accept_abort(&request);
    }
    if (pthread_mutex_destroy(&control->mutex) != 0) {
        accept_panic();
    }
    r_runtime_allocator_deallocate(control, _Alignof(RLibraryNetAcceptControl));
}

static void accept_payload_move(void *destination_pointer, void *source_pointer) {
    RLibraryNetAcceptPayload *destination = destination_pointer;
    RLibraryNetAcceptPayload *source = source_pointer;

    *destination = *source;
    source->stream_storage = NULL;
    source->listener_storage = NULL;
    source->listener_handle = NULL;
    source->control = NULL;
    source->queue_node = NULL;
}

static void accept_queue_node_destroy(RLibraryNetAcceptQueueNode *node) {
    RLibraryNetAcceptDrainAction drain_action = {0};

    if (node == NULL) {
        return;
    }
    if (node->reserved) {
        RStdNetTcpListenerStorage *listener = node->listener;

        if (listener == NULL || pthread_mutex_lock(&listener->handle.mutex) != 0) {
            accept_panic();
        }
        if (!node->reserved || node->enqueued || listener->accept_reservations == 0U) {
            (void)pthread_mutex_unlock(&listener->handle.mutex);
            accept_panic();
        }
        listener->accept_reservations -= 1U;
        node->reserved = 0;
        if (listener->handle.terminal && listener->accept_reservations == 0U &&
            listener->accept_head == NULL && listener->accept_tail == NULL &&
            listener->accept_drain != NULL) {
            drain_action.drain = listener->accept_drain;
            drain_action.context = listener->accept_drain_context;
            listener->accept_drain = NULL;
            listener->accept_drain_context = NULL;
        }
        if (pthread_mutex_unlock(&listener->handle.mutex) != 0) {
            accept_panic();
        }
    }
    if (node->reserved || node->enqueued || node->previous != NULL || node->next != NULL) {
        accept_panic();
    }
    r_runtime_allocator_deallocate(node, _Alignof(RLibraryNetAcceptQueueNode));
    if (drain_action.drain != NULL) {
        drain_action.drain(drain_action.context);
    }
}

static void accept_payload_drop(void *value) {
    RLibraryNetAcceptPayload *payload = value;

    accept_control_destroy(payload->control);
    payload->control = NULL;
    accept_queue_node_destroy(payload->queue_node);
    payload->queue_node = NULL;
    r_library_internal_net_handle_release(payload->listener_handle);
    payload->listener_storage = NULL;
    payload->listener_handle = NULL;
    r_library_internal_net_handle_release(
        payload->stream_storage == NULL ? NULL : &payload->stream_storage->handle);
    payload->stream_storage = NULL;
}

static RLibraryNetAcceptControl *accept_control_create(RRuntimeAllocator *allocator) {
    RLibraryNetAcceptControl *control = NULL;

    if (allocator == NULL ||
        r_runtime_allocator_allocate(
            allocator, sizeof(*control), _Alignof(RLibraryNetAcceptControl), (void **)&control) !=
            R_RUNTIME_ALLOCATION_OK) {
        return NULL;
    }
    *control = (RLibraryNetAcceptControl){0};
    control->allocator = allocator;
    control->accepted_descriptor = -1;
    if (pthread_mutex_init(&control->mutex, NULL) != 0) {
        r_runtime_allocator_deallocate(control, _Alignof(RLibraryNetAcceptControl));
        return NULL;
    }
    return control;
}

static RLibraryNetAcceptQueueNode *accept_queue_node_create(RRuntimeAllocator *allocator) {
    RLibraryNetAcceptQueueNode *node = NULL;

    if (allocator == NULL ||
        r_runtime_allocator_allocate(
            allocator, sizeof(*node), _Alignof(RLibraryNetAcceptQueueNode), (void **)&node) !=
            R_RUNTIME_ALLOCATION_OK) {
        return NULL;
    }
    *node = (RLibraryNetAcceptQueueNode){0};
    node->allocator = allocator;
    return node;
}

static _Bool accept_queue_reserve(RStdNetTcpListenerStorage *listener,
                                  RLibraryNetAcceptQueueNode *node,
                                  int *descriptor) {
    size_t references;
    _Bool retained = 0;

    if (listener == NULL || node == NULL || descriptor == NULL ||
        pthread_mutex_lock(&listener->handle.mutex) != 0) {
        return 0;
    }
    references = atomic_load_explicit(&listener->handle.references, memory_order_relaxed);
    if (!listener->handle.terminal && !listener->handle.close_reserved &&
        listener->handle.descriptor >= 0 && listener->accept_reservations != SIZE_MAX) {
        while (references != 0U && references != SIZE_MAX) {
            if (atomic_compare_exchange_weak_explicit(&listener->handle.references,
                                                      &references,
                                                      references + 1U,
                                                      memory_order_relaxed,
                                                      memory_order_relaxed)) {
                listener->accept_reservations += 1U;
                node->listener = listener;
                node->reserved = 1;
                *descriptor = listener->handle.descriptor;
                retained = 1;
                break;
            }
        }
    }
    if (pthread_mutex_unlock(&listener->handle.mutex) != 0) {
        accept_panic();
    }
    return retained;
}

static void accept_queue_cancel_for_listener_close_locked(RLibraryNetAcceptQueueNode *node) {
    RLibraryNetAcceptControl *control = node->control;

    if (node->listener_close_sequence == UINT64_C(0) || node->listener_close_cancel_dispatched) {
        return;
    }
    if (control == NULL || pthread_mutex_lock(&control->mutex) != 0) {
        accept_panic();
    }
    node->listener_close_cancel_dispatched = 1;
    if (control->request != NULL) {
        (void)r_runtime_darwin_socket_accept_cancel(control->request,
                                                    node->listener_close_sequence);
    }
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        accept_panic();
    }
}

static void accept_queue_activate_head_locked(RStdNetTcpListenerStorage *listener) {
    RLibraryNetAcceptQueueNode *node = listener->accept_head;
    RLibraryNetAcceptControl *control;

    if (listener->handle.terminal || node == NULL || !node->ready || node->activation_claimed) {
        return;
    }
    control = node->control;
    if (control == NULL || pthread_mutex_lock(&control->mutex) != 0) {
        accept_panic();
    }
    node->activation_claimed = 1;
    if (control->request != NULL && !r_runtime_task_external_cancel_requested(control->execution)) {
        if (!r_runtime_darwin_socket_accept_activate(control->request)) {
            (void)pthread_mutex_unlock(&control->mutex);
            accept_panic();
        }
    }
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        accept_panic();
    }
}

static void accept_queue_enqueue(RLibraryNetAcceptPayload *payload) {
    RStdNetTcpListenerStorage *listener = payload->listener_storage;
    RLibraryNetAcceptQueueNode *node = payload->queue_node;

    if (listener == NULL || node == NULL || node->listener != listener ||
        node->control != payload->control || pthread_mutex_lock(&listener->handle.mutex) != 0) {
        accept_panic();
    }
    if (!node->reserved || node->enqueued || node->previous != NULL || node->next != NULL ||
        listener->accept_reservations == 0U || listener->handle.close_reserved ||
        listener->handle.descriptor < 0 ||
        (listener->handle.terminal && listener->close_sequence == UINT64_C(0)) ||
        (listener->accept_head == NULL) != (listener->accept_tail == NULL)) {
        (void)pthread_mutex_unlock(&listener->handle.mutex);
        accept_panic();
    }
    listener->accept_reservations -= 1U;
    node->reserved = 0;
    if (listener->handle.terminal) {
        node->listener_close_sequence = listener->close_sequence;
    }
    node->previous = listener->accept_tail;
    if (listener->accept_tail == NULL) {
        listener->accept_head = node;
    } else {
        listener->accept_tail->next = node;
    }
    listener->accept_tail = node;
    node->enqueued = 1;
    if (pthread_mutex_unlock(&listener->handle.mutex) != 0) {
        accept_panic();
    }
}

static void accept_queue_mark_ready(RLibraryNetAcceptPayload *payload) {
    RStdNetTcpListenerStorage *listener = payload->listener_storage;
    RLibraryNetAcceptQueueNode *node = payload->queue_node;
    RLibraryNetAcceptControl *control = payload->control;

    if (listener == NULL || node == NULL || control == NULL ||
        pthread_mutex_lock(&listener->handle.mutex) != 0) {
        accept_panic();
    }
    if (!node->enqueued || node->listener != listener || node->control != control || node->ready ||
        pthread_mutex_lock(&control->mutex) != 0) {
        (void)pthread_mutex_unlock(&listener->handle.mutex);
        accept_panic();
    }
    node->ready = 1;
    if (control->request == NULL) {
        (void)pthread_mutex_unlock(&control->mutex);
        (void)pthread_mutex_unlock(&listener->handle.mutex);
        accept_panic();
    }
    if (node->listener_close_sequence != UINT64_C(0)) {
        if (pthread_mutex_unlock(&control->mutex) != 0) {
            (void)pthread_mutex_unlock(&listener->handle.mutex);
            accept_panic();
        }
        accept_queue_cancel_for_listener_close_locked(node);
    } else {
        if (!r_runtime_task_external_cancel_requested(control->execution) &&
            !r_runtime_darwin_socket_accept_arm_deadline(control->request)) {
            (void)pthread_mutex_unlock(&control->mutex);
            (void)pthread_mutex_unlock(&listener->handle.mutex);
            accept_panic();
        }
        if (pthread_mutex_unlock(&control->mutex) != 0) {
            (void)pthread_mutex_unlock(&listener->handle.mutex);
            accept_panic();
        }
    }
    accept_queue_activate_head_locked(listener);
    if (pthread_mutex_unlock(&listener->handle.mutex) != 0) {
        accept_panic();
    }
}

static void accept_queue_remove_and_advance(RLibraryNetAcceptPayload *payload) {
    RStdNetTcpListenerStorage *listener = payload->listener_storage;
    RLibraryNetAcceptQueueNode *node = payload->queue_node;
    _Bool was_head;
    RLibraryNetAcceptDrainAction drain_action = {0};

    if (listener == NULL || node == NULL || pthread_mutex_lock(&listener->handle.mutex) != 0) {
        accept_panic();
    }
    was_head = listener->accept_head == node;
    if (!node->enqueued || node->listener != listener ||
        (node->previous == NULL && listener->accept_head != node) ||
        (node->next == NULL && listener->accept_tail != node)) {
        (void)pthread_mutex_unlock(&listener->handle.mutex);
        accept_panic();
    }
    if (node->previous == NULL) {
        listener->accept_head = node->next;
    } else {
        node->previous->next = node->next;
    }
    if (node->next == NULL) {
        listener->accept_tail = node->previous;
    } else {
        node->next->previous = node->previous;
    }
    node->previous = NULL;
    node->next = NULL;
    node->enqueued = 0;
    if (was_head) {
        accept_queue_activate_head_locked(listener);
    }
    if (listener->handle.terminal && listener->accept_reservations == 0U &&
        listener->accept_head == NULL && listener->accept_tail == NULL &&
        listener->accept_drain != NULL) {
        drain_action.drain = listener->accept_drain;
        drain_action.context = listener->accept_drain_context;
        listener->accept_drain = NULL;
        listener->accept_drain_context = NULL;
    }
    if (pthread_mutex_unlock(&listener->handle.mutex) != 0) {
        accept_panic();
    }
    if (drain_action.drain != NULL) {
        drain_action.drain(drain_action.context);
    }
}

static uint64_t accept_queue_listener_close_sequence(RLibraryNetAcceptPayload *payload) {
    RStdNetTcpListenerStorage *listener = payload->listener_storage;
    RLibraryNetAcceptQueueNode *node = payload->queue_node;
    uint64_t sequence;

    if (listener == NULL || node == NULL || pthread_mutex_lock(&listener->handle.mutex) != 0) {
        accept_panic();
    }
    if (!node->enqueued || node->listener != listener) {
        (void)pthread_mutex_unlock(&listener->handle.mutex);
        accept_panic();
    }
    sequence = node->listener_close_sequence;
    if (pthread_mutex_unlock(&listener->handle.mutex) != 0) {
        accept_panic();
    }
    return sequence;
}

static RLibraryNetAcceptFinalizeAction
accept_finalize_action_locked(RLibraryNetAcceptControl *control) {
    RLibraryNetAcceptFinalizeAction action = {0};
    const uint64_t cancellation_sequence =
        r_runtime_task_external_cancellation_sequence(control->execution);

    if (control->callback_released &&
        (cancellation_sequence == UINT64_C(0) || control->cancel_reported) && !control->finalized) {
        control->finalized = 1;
        action.native_result = control->native_result;
        action.accepted_descriptor = control->accepted_descriptor;
        action.ready = 1;
        action.completion_selected = control->completion_selected;
        control->accepted_descriptor = -1;
    }
    return action;
}

static void accept_fill_result(RLibraryNetAcceptPayload *payload,
                               RLibraryNetAcceptFinalizeAction action) {
    RLibraryNetAcceptControl *control = payload->control;
    RStdNetSocketAddress peer = {0};
    RStdNetTcpConnectionResult *connection;

    if (!action.completion_selected) {
        return;
    }
    if (action.native_result.terminal_event == R_RUNTIME_DARWIN_SOCKET_TERMINAL_TIMED_OUT) {
        accept_result_error(control->result,
                            payload->unix_domain,
                            net_error(R_STD_NET_ERROR_TIMED_OUT, INT64_C(0)));
        return;
    }
    if (action.native_result.terminal_event == R_RUNTIME_DARWIN_SOCKET_TERMINAL_CANCELLED) {
        accept_result_error(control->result,
                            payload->unix_domain,
                            net_error(R_STD_NET_ERROR_CANCELLED, INT64_C(0)));
        return;
    }
    if (action.native_result.terminal_event != R_RUNTIME_DARWIN_SOCKET_TERMINAL_NATIVE) {
        accept_panic();
    }
    if (!action.native_result.accepted) {
        accept_result_error(
            control->result,
            payload->unix_domain,
            r_library_internal_net_error_from_native(action.native_result.native_error));
        return;
    }
    if (action.accepted_descriptor < 0 || payload->stream_storage == NULL) {
        accept_panic();
    }
    if (payload->unix_domain) {
        RStdNetUnixStreamResult *stream = control->result;

        r_library_internal_net_tcp_stream_publish(payload->stream_storage,
                                                  action.accepted_descriptor);
        *stream = (RStdNetUnixStreamResult){0};
        stream->r_payload.r_ok.storage = payload->stream_storage;
        payload->stream_storage = NULL;
        return;
    }
    if (!r_library_internal_net_address_from_native(
            (const struct sockaddr *)&action.native_result.peer,
            action.native_result.peer_length,
            &peer)) {
        (void)close(action.accepted_descriptor);
        accept_result_error(
            control->result, 0, net_error(R_STD_NET_ERROR_INVALID_ADDRESS, INT64_C(0)));
        return;
    }
    r_library_internal_net_tcp_stream_publish(payload->stream_storage, action.accepted_descriptor);
    connection = control->result;
    *connection = (RStdNetTcpConnectionResult){0};
    connection->r_payload.r_ok.stream.storage = payload->stream_storage;
    connection->r_payload.r_ok.peer = peer;
    payload->stream_storage = NULL;
}

static void accept_finalize(RLibraryNetAcceptPayload *payload,
                            RLibraryNetAcceptFinalizeAction action) {
    RLibraryNetAcceptControl *control;
    RRuntimeTaskExternalExecution *execution;

    if (!action.ready) {
        return;
    }
    control = payload->control;
    execution = control->execution;
    accept_fill_result(payload, action);
    r_runtime_task_external_acknowledge(execution);
}

static void accept_native_completed(RRuntimeDarwinSocketAccept *request, void *context_pointer) {
    RLibraryNetAcceptPayload *payload = context_pointer;
    RLibraryNetAcceptControl *control = payload->control;
    RRuntimeDarwinSocketAcceptResult native_result = r_runtime_darwin_socket_accept_result(request);
    RLibraryNetAcceptFinalizeAction action;
    const uint64_t listener_close_sequence = accept_queue_listener_close_sequence(payload);
    _Bool completion_selected = 0;
    int accepted_descriptor = -1;

    if (listener_close_sequence != UINT64_C(0) &&
        ((native_result.terminal_event == R_RUNTIME_DARWIN_SOCKET_TERMINAL_CANCELLED &&
          listener_close_sequence <= native_result.terminal_event_sequence) ||
         (native_result.terminal_event != R_RUNTIME_DARWIN_SOCKET_TERMINAL_CANCELLED &&
          listener_close_sequence < native_result.terminal_event_sequence))) {
        native_result = (RRuntimeDarwinSocketAcceptResult){0};
        native_result.terminal_event = R_RUNTIME_DARWIN_SOCKET_TERMINAL_CANCELLED;
        native_result.terminal_event_sequence = listener_close_sequence;
        completion_selected = r_runtime_task_external_try_select_completion_at(
            control->execution, listener_close_sequence);
    } else if (native_result.terminal_event != R_RUNTIME_DARWIN_SOCKET_TERMINAL_CANCELLED) {
        completion_selected = r_runtime_task_external_try_select_completion_at(
            control->execution, native_result.terminal_event_sequence);
    }
    if (native_result.accepted && completion_selected) {
        accepted_descriptor = r_runtime_darwin_socket_accept_take_descriptor(request);
        if (accepted_descriptor < 0) {
            accept_panic();
        }
    }
    if (pthread_mutex_lock(&control->mutex) != 0) {
        accept_panic();
    }
    if (control->request != request || control->callback_released) {
        (void)pthread_mutex_unlock(&control->mutex);
        accept_panic();
    }
    control->request = NULL;
    control->native_result = native_result;
    control->accepted_descriptor = accepted_descriptor;
    control->completion_selected = completion_selected;
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        accept_panic();
    }
    r_runtime_darwin_socket_accept_release(request);
    accept_queue_remove_and_advance(payload);
    if (pthread_mutex_lock(&control->mutex) != 0) {
        accept_panic();
    }
    if (control->callback_released || control->request != NULL) {
        (void)pthread_mutex_unlock(&control->mutex);
        accept_panic();
    }
    control->callback_released = 1;
    action = accept_finalize_action_locked(control);
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        accept_panic();
    }
    accept_finalize(payload, action);
}

static void accept_external_cancel(RRuntimeTaskExternalExecution *execution,
                                   void *payload_pointer) {
    RLibraryNetAcceptPayload *payload = payload_pointer;
    RLibraryNetAcceptControl *control = payload->control;
    RLibraryNetAcceptFinalizeAction action;
    const uint64_t cancellation_sequence = r_runtime_task_external_cancellation_sequence(execution);

    if (control == NULL || control->execution != execution ||
        cancellation_sequence == UINT64_C(0) || pthread_mutex_lock(&control->mutex) != 0) {
        accept_panic();
    }
    if (control->cancel_reported) {
        (void)pthread_mutex_unlock(&control->mutex);
        accept_panic();
    }
    if (control->request != NULL) {
        (void)r_runtime_darwin_socket_accept_cancel(control->request, cancellation_sequence);
    }
    control->cancel_reported = 1;
    action = accept_finalize_action_locked(control);
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        accept_panic();
    }
    accept_finalize(payload, action);
}

static void accept_complete_immediate(RLibraryNetAcceptPayload *payload,
                                      RRuntimeTaskExternalExecution *execution,
                                      void *result_pointer) {
    RLibraryNetHandleStorage *listener_handle;
    _Bool selected;

    if (payload->immediate_event_sequence == UINT64_C(0)) {
        accept_panic();
    }
    listener_handle = payload->listener_handle;
    accept_queue_node_destroy(payload->queue_node);
    payload->queue_node = NULL;
    payload->listener_storage = NULL;
    payload->listener_handle = NULL;
    r_library_internal_net_handle_release(listener_handle);
    selected = r_runtime_task_external_try_select_completion_at(execution,
                                                                payload->immediate_event_sequence);
    r_runtime_task_external_start_ready(execution);
    if (selected) {
        accept_result_error(result_pointer, payload->unix_domain, payload->immediate_error);
        r_runtime_task_external_acknowledge(execution);
    }
}

static void accept_external_start(RRuntimeTaskExternalExecution *execution,
                                  void *payload_pointer,
                                  void *result_pointer) {
    RLibraryNetAcceptPayload *payload = payload_pointer;
    RLibraryNetAcceptControl *control = payload->control;

    if (payload->immediate) {
        accept_complete_immediate(payload, execution, result_pointer);
        return;
    }
    if (control == NULL || control->request == NULL) {
        accept_panic();
    }
    control->execution = execution;
    control->result = result_pointer;
    if (!r_runtime_darwin_socket_accept_bind(control->request, accept_native_completed, payload)) {
        accept_panic();
    }
    accept_queue_enqueue(payload);
    r_runtime_task_external_start_ready(execution);
    accept_queue_mark_ready(payload);
}

static RRuntimeTaskStartStatus accept_reserve_native(RLibraryNetAcceptPayload *payload,
                                                     RRuntimeAllocator *allocator,
                                                     int listener_descriptor,
                                                     RStdNetDeadline deadline) {
    uint64_t timeout_nanoseconds = 0U;
    RStdNetError deadline_error = {0};
    RLibraryNetDeadlineStatus deadline_status;
    RRuntimeDarwinSocketAcceptPrepareResult preparation;

    deadline_status =
        r_library_internal_net_deadline_timeout(deadline, &timeout_nanoseconds, &deadline_error);
    if (deadline_status != R_LIBRARY_NET_DEADLINE_READY) {
        payload->immediate = 1;
        payload->immediate_error = deadline_error;
        payload->immediate_event_sequence = r_runtime_darwin_event_sequence_next();
        return R_RUNTIME_TASK_START_OK;
    }
    preparation =
        r_runtime_darwin_socket_accept_prepare(allocator, listener_descriptor, timeout_nanoseconds);
    switch (preparation.status) {
    case R_RUNTIME_DARWIN_SOCKET_PREPARE_OK:
        payload->control->request = preparation.request;
        return R_RUNTIME_TASK_START_OK;
    case R_RUNTIME_DARWIN_SOCKET_PREPARE_ALLOCATION_FAILED:
        return R_RUNTIME_TASK_START_ALLOCATION_FAILED;
    case R_RUNTIME_DARWIN_SOCKET_PREPARE_NATIVE_FAILED:
        payload->immediate = 1;
        payload->immediate_error =
            r_library_internal_net_error_from_native(preparation.native_error);
        payload->immediate_event_sequence = r_runtime_darwin_event_sequence_next();
        return R_RUNTIME_TASK_START_OK;
    case R_RUNTIME_DARWIN_SOCKET_PREPARE_INVALID:
        accept_panic();
    }
    accept_panic();
    return R_RUNTIME_TASK_START_INVALID;
}

static RStdNetTaskStartResult accept_start(RStdNetTcpListenerStorage *listener_storage,
                                           RStdNetDeadline deadline,
                                           _Bool unix_domain) {
    const RRuntimeTypeInfo payload_type = {
        sizeof(RLibraryNetAcceptPayload),
        _Alignof(RLibraryNetAcceptPayload),
        accept_payload_move,
        accept_payload_drop,
    };
    const RRuntimeTypeInfo result_type = {
        unix_domain ? sizeof(RStdNetUnixStreamResult) : sizeof(RStdNetTcpConnectionResult),
        unix_domain ? _Alignof(RStdNetUnixStreamResult) : _Alignof(RStdNetTcpConnectionResult),
        unix_domain ? unix_stream_result_move : connection_result_move,
        unix_domain ? unix_stream_result_drop : connection_result_drop,
    };
    RLibraryNetAcceptPayload payload = {0};
    RRuntimeTaskPrepareResult task_preparation;
    RRuntimeTaskStartResult started;
    RRuntimeAllocator *allocator;
    RRuntimeTaskStartStatus reserve_status;
    RStdNetTaskStartResult result = {0};
    int listener_descriptor = -1;

    payload.unix_domain = unix_domain;
    task_preparation = r_runtime_task_external_start_prepare(
        payload_type, result_type, accept_external_start, accept_external_cancel);
    if (task_preparation.status != R_RUNTIME_TASK_START_OK) {
        return start_failure(task_preparation.status);
    }
    allocator = r_runtime_task_start_allocator(task_preparation.transaction);
    if (allocator == NULL) {
        r_runtime_task_start_abort(&task_preparation.transaction);
        accept_panic();
    }
    payload.stream_storage = r_library_internal_net_tcp_stream_reserve(allocator);
    if (payload.stream_storage == NULL) {
        r_runtime_task_start_abort(&task_preparation.transaction);
        return start_failure(R_RUNTIME_TASK_START_ALLOCATION_FAILED);
    }
    payload.control = accept_control_create(allocator);
    if (payload.control == NULL) {
        r_runtime_task_start_abort(&task_preparation.transaction);
        accept_payload_drop(&payload);
        return start_failure(R_RUNTIME_TASK_START_ALLOCATION_FAILED);
    }
    payload.queue_node = accept_queue_node_create(allocator);
    if (payload.queue_node == NULL) {
        r_runtime_task_start_abort(&task_preparation.transaction);
        accept_payload_drop(&payload);
        return start_failure(R_RUNTIME_TASK_START_ALLOCATION_FAILED);
    }
    payload.queue_node->control = payload.control;
    if (!accept_queue_reserve(listener_storage, payload.queue_node, &listener_descriptor)) {
        payload.immediate = 1;
        payload.immediate_error = net_error(R_STD_NET_ERROR_CLOSED, INT64_C(0));
        payload.immediate_event_sequence = r_runtime_darwin_event_sequence_next();
    } else {
        payload.listener_storage = listener_storage;
        payload.listener_handle = &listener_storage->handle;
        payload.queue_node->listener = listener_storage;
        reserve_status = accept_reserve_native(&payload, allocator, listener_descriptor, deadline);
        if (reserve_status != R_RUNTIME_TASK_START_OK) {
            r_runtime_task_start_abort(&task_preparation.transaction);
            accept_payload_drop(&payload);
            return start_failure(reserve_status);
        }
    }
    started = r_runtime_task_start_commit(&task_preparation.transaction, &payload);
    if (started.status != R_RUNTIME_TASK_START_OK) {
        accept_payload_drop(&payload);
        return start_failure(started.status);
    }
    result.is_ok = 1;
    result.task = started.task;
    return result;
}

RStdNetTaskStartResult r_library_internal_net_tcp_accept(const RStdNetTcpListener *listener,
                                                         RStdNetDeadline deadline) {
    return accept_start(listener->storage, deadline, 0);
}

RStdNetTaskStartResult r_library_internal_net_unix_accept(const RStdNetUnixListener *listener,
                                                          RStdNetDeadline deadline) {
    return accept_start(listener->storage, deadline, 1);
}

_Bool r_library_internal_net_tcp_listener_mark_closing(RStdNetTcpListenerStorage *listener) {
    RLibraryNetAcceptQueueNode *node;
    uint64_t close_sequence;

    if (listener == NULL || pthread_mutex_lock(&listener->handle.mutex) != 0) {
        return 0;
    }
    if (listener->handle.kind != R_LIBRARY_NET_HANDLE_TCP_LISTENER || listener->handle.terminal ||
        listener->handle.close_reserved || listener->handle.descriptor < 0 ||
        listener->close_sequence != UINT64_C(0) || listener->accept_drain != NULL) {
        (void)pthread_mutex_unlock(&listener->handle.mutex);
        return 0;
    }
    close_sequence = r_runtime_darwin_event_sequence_next();
    listener->handle.terminal = 1;
    listener->close_sequence = close_sequence;
    for (node = listener->accept_head; node != NULL; node = node->next) {
        if (node->listener_close_sequence != UINT64_C(0)) {
            (void)pthread_mutex_unlock(&listener->handle.mutex);
            accept_panic();
        }
        node->listener_close_sequence = close_sequence;
    }
    if (pthread_mutex_unlock(&listener->handle.mutex) != 0) {
        accept_panic();
    }
    return 1;
}

void r_library_internal_net_tcp_listener_drain_accepts(RStdNetTcpListenerStorage *listener,
                                                       RLibraryNetListenerDrainFn drain,
                                                       void *context) {
    RLibraryNetAcceptQueueNode *node;
    _Bool already_drained;

    if (listener == NULL || drain == NULL || pthread_mutex_lock(&listener->handle.mutex) != 0) {
        accept_panic();
    }
    if (!listener->handle.terminal || listener->close_sequence == UINT64_C(0) ||
        listener->accept_drain != NULL) {
        (void)pthread_mutex_unlock(&listener->handle.mutex);
        accept_panic();
    }
    already_drained = listener->accept_reservations == 0U && listener->accept_head == NULL;
    if (!already_drained) {
        listener->accept_drain = drain;
        listener->accept_drain_context = context;
        for (node = listener->accept_head; node != NULL; node = node->next) {
            if (node->listener_close_sequence != listener->close_sequence) {
                (void)pthread_mutex_unlock(&listener->handle.mutex);
                accept_panic();
            }
            accept_queue_cancel_for_listener_close_locked(node);
        }
    }
    if (pthread_mutex_unlock(&listener->handle.mutex) != 0) {
        accept_panic();
    }
    if (already_drained) {
        drain(context);
    }
}

int r_library_internal_net_tcp_listener_take_close_descriptor(RStdNetTcpListenerStorage *listener) {
    int descriptor;

    if (listener == NULL || pthread_mutex_lock(&listener->handle.mutex) != 0) {
        accept_panic();
    }
    if (!listener->handle.terminal || listener->close_sequence == UINT64_C(0) ||
        listener->accept_reservations != 0U || listener->accept_head != NULL ||
        listener->accept_tail != NULL || listener->accept_drain != NULL ||
        listener->handle.descriptor < 0) {
        (void)pthread_mutex_unlock(&listener->handle.mutex);
        accept_panic();
    }
    descriptor = listener->handle.descriptor;
    listener->handle.descriptor = -1;
    if (pthread_mutex_unlock(&listener->handle.mutex) != 0) {
        accept_panic();
    }
    return descriptor;
}

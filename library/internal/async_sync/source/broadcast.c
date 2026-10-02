#include "r_library_async_sync_internal.h"

#include "r_runtime_0_1.h"
#include "r_runtime_core.h"

#include <dispatch/dispatch.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * std.async::broadcast<T> (R-SLIB-ASYNC-0016). Every published value lives once in a node; the
 * ring holds the newest capacity nodes, and each receiver reads through its own cursor. A node
 * carries a reference count, so overwriting a slot never frees a node that a receive is cloning.
 * Clones and destructions run outside the lock. A receiver whose cursor fell behind the oldest
 * node learns how many values it missed, then continues from the oldest. Receives wait only when
 * their receiver has read every published value; the last broadcast handle closes them.
 */
typedef struct RLibraryAsyncBroadcastNode {
    size_t references;
} RLibraryAsyncBroadcastNode;

struct RLibraryAsyncBroadcastState {
    pthread_mutex_t lock;
    RRuntimeAllocator *allocator;
    RRuntimeTypeInfo element;
    RStdAsyncCloneFn clone;
    RLibraryAsyncBroadcastNode **ring;
    RLibraryAsyncWaitList waiters;
    size_t capacity;
    size_t value_offset;
    size_t node_size;
    size_t node_alignment;
    size_t allocation_alignment;
    size_t references;
    size_t senders;
    size_t subscribers;
    uint64_t tail;
};

struct RLibraryAsyncBroadcastQueue {
    RLibraryAsyncBroadcastState *state;
    size_t references;
    uint64_t next;
};

enum {
    R_ASYNC_BROADCAST_RECEIVED = 0,
    R_ASYNC_BROADCAST_LAGGED = 1,
    R_ASYNC_BROADCAST_CLOSED = 2
};

static void broadcast_lock(RLibraryAsyncBroadcastState *state) {
    if (pthread_mutex_lock(&state->lock) != 0) {
        r_library_internal_async_contract_violation();
    }
}

static void broadcast_unlock(RLibraryAsyncBroadcastState *state) {
    if (pthread_mutex_unlock(&state->lock) != 0) {
        r_library_internal_async_contract_violation();
    }
}

static void retain_locked(RLibraryAsyncBroadcastState *state) {
    if (state->references >= (SIZE_MAX / 2U)) {
        broadcast_unlock(state);
        r_library_internal_async_count_overflow();
    }
    state->references += 1U;
}

static void *node_value(RLibraryAsyncBroadcastState *state, RLibraryAsyncBroadcastNode *node) {
    return (unsigned char *)node + state->value_offset;
}

static void node_free(RLibraryAsyncBroadcastState *state, RLibraryAsyncBroadcastNode *node) {
    if (node == NULL) {
        return;
    }
    if (state->element.drop != NULL) {
        state->element.drop(node_value(state, node));
    }
    r_runtime_allocator_deallocate(node, state->node_alignment);
}

/* Under the lock: returns the node when this was its last reference. */
static RLibraryAsyncBroadcastNode *node_unref_locked(RLibraryAsyncBroadcastNode *node) {
    if (node == NULL) {
        return NULL;
    }
    node->references -= 1U;
    return node->references == 0U ? node : NULL;
}

static void state_release(RLibraryAsyncBroadcastState *state) {
    _Bool destroy;

    broadcast_lock(state);
    state->references -= 1U;
    destroy = state->references == 0U;
    broadcast_unlock(state);
    if (!destroy) {
        return;
    }
    for (size_t index = 0U; index < state->capacity; ++index) {
        RLibraryAsyncBroadcastNode *node = state->ring[index];

        if ((node != NULL) && (node_unref_locked(node) != NULL)) {
            node_free(state, node);
        }
    }
    if (pthread_mutex_destroy(&state->lock) != 0) {
        r_library_internal_async_contract_violation();
    }
    r_runtime_allocator_deallocate(state, state->allocation_alignment);
}

static void clone_into(RLibraryAsyncBroadcastState *state,
                       void *destination,
                       RLibraryAsyncBroadcastNode *node) {
    RStdAllocError error = R_STD_ALLOC_ERROR_OUT_OF_MEMORY;

    if (state->clone == NULL) {
        (void)memcpy(destination, node_value(state, node), state->element.size);
        return;
    }
    if (!state->clone(destination, node_value(state, node), &error)) {
        (void)error;
        r_runtime_panic(R_RUNTIME_PANIC_ALLOCATION_FAILURE,
                        (RRuntimeSourceSpan){UINT32_C(0), UINT32_C(0), UINT32_C(0)});
    }
}

/*
 * The payload of one receive. After selection, node names the value to clone (pinned by one
 * reference) and outcome the result alternative; lag is the payload of lagged.
 */
typedef struct RLibraryAsyncBroadcastReceive {
    RLibraryAsyncWaiter waiter;
    RLibraryAsyncBroadcastQueue *queue;
    RLibraryAsyncBroadcastNode *node;
    uint64_t lag;
    uint32_t outcome;
    size_t tag_offset;
    size_t value_offset;
    size_t lag_offset;
} RLibraryAsyncBroadcastReceive;

static RLibraryAsyncBroadcastReceive *receive_of(RLibraryAsyncWaiter *waiter) {
    return (RLibraryAsyncBroadcastReceive *)(void *)waiter;
}

static void queue_release(RLibraryAsyncBroadcastQueue *queue) {
    RLibraryAsyncBroadcastState *state = queue->state;
    _Bool destroy;

    broadcast_lock(state);
    queue->references -= 1U;
    destroy = queue->references == 0U;
    broadcast_unlock(state);
    if (!destroy) {
        return;
    }
    r_runtime_allocator_deallocate(queue, _Alignof(RLibraryAsyncBroadcastQueue));
    state_release(state);
}

/*
 * Under the lock, for a selected receive: decides its outcome from the receiver's cursor and pins
 * the node to clone. Returns false when the receiver has read every value and the broadcast is
 * still open, so the receive keeps waiting.
 */
static _Bool settle_locked(RLibraryAsyncBroadcastState *state,
                           RLibraryAsyncBroadcastReceive *receive) {
    RLibraryAsyncBroadcastQueue *queue = receive->queue;
    const uint64_t oldest = state->tail > (uint64_t)state->capacity
                                ? state->tail - (uint64_t)state->capacity
                                : UINT64_C(0);

    if (queue->next < oldest) {
        receive->outcome = R_ASYNC_BROADCAST_LAGGED;
        receive->lag = oldest - queue->next;
        queue->next = oldest;
        return 1;
    }
    if (queue->next < state->tail) {
        RLibraryAsyncBroadcastNode *node = state->ring[queue->next % state->capacity];

        node->references += 1U;
        receive->node = node;
        receive->outcome = R_ASYNC_BROADCAST_RECEIVED;
        queue->next += 1U;
        return 1;
    }
    if (state->senders == 0U) {
        receive->outcome = R_ASYNC_BROADCAST_CLOSED;
        return 1;
    }
    return 0;
}

static _Bool available_locked(const RLibraryAsyncBroadcastState *state,
                              const RLibraryAsyncBroadcastReceive *receive) {
    return (receive->queue->next < state->tail) || (state->senders == 0U);
}

/* Outside the lock: writes the settled outcome and releases the pinned node. */
static void write_outcome(RLibraryAsyncBroadcastState *state,
                          RLibraryAsyncBroadcastReceive *receive) {
    const uint32_t tag = receive->outcome;
    RLibraryAsyncBroadcastNode *release = NULL;

    if (tag == R_ASYNC_BROADCAST_RECEIVED) {
        clone_into(state, receive->waiter.result + receive->value_offset, receive->node);
        broadcast_lock(state);
        release = node_unref_locked(receive->node);
        broadcast_unlock(state);
        receive->node = NULL;
        node_free(state, release);
    } else if (tag == R_ASYNC_BROADCAST_LAGGED) {
        (void)memcpy(receive->waiter.result + receive->lag_offset, &receive->lag, sizeof(uint64_t));
    }
    (void)memcpy(receive->waiter.result + receive->tag_offset, &tag, sizeof(tag));
}

static void receive_move(void *destination, void *source) {
    RLibraryAsyncBroadcastReceive *destination_value = destination;
    RLibraryAsyncBroadcastReceive *source_value = source;

    *destination_value = *source_value;
    source_value->queue = NULL;
}

static void receive_drop(void *value) {
    RLibraryAsyncBroadcastReceive *receive = value;

    if (receive->queue != NULL) {
        queue_release(receive->queue);
        receive->queue = NULL;
    }
}

static void receive_external_start(RRuntimeTaskExternalExecution *execution,
                                   void *payload_pointer,
                                   void *result_pointer) {
    RLibraryAsyncBroadcastReceive *receive = payload_pointer;
    RLibraryAsyncBroadcastState *state = receive->queue->state;

    receive->waiter.execution = execution;
    receive->waiter.result = result_pointer;
    r_runtime_task_external_start_ready(execution);
    broadcast_lock(state);
    if (!available_locked(state, receive)) {
        r_library_internal_async_wait_push(&state->waiters, &receive->waiter);
        broadcast_unlock(state);
        return;
    }
    if (!r_library_internal_async_waiter_select(&receive->waiter)) {
        broadcast_unlock(state);
        return;
    }
    (void)settle_locked(state, receive);
    broadcast_unlock(state);
    write_outcome(state, receive);
    r_library_internal_async_waiter_finish(&receive->waiter);
}

static void receive_external_cancel(RRuntimeTaskExternalExecution *execution,
                                    void *payload_pointer) {
    RLibraryAsyncBroadcastReceive *receive = payload_pointer;
    RLibraryAsyncBroadcastState *state = receive->queue->state;

    broadcast_lock(state);
    if (receive->waiter.linked) {
        r_library_internal_async_wait_remove(&state->waiters, &receive->waiter);
    }
    broadcast_unlock(state);
    r_runtime_task_external_acknowledge(execution);
}

/* Under the lock: settles every waiting receive that can complete now, in start order. */
static RLibraryAsyncWaiter *wake_locked(RLibraryAsyncBroadcastState *state) {
    RLibraryAsyncFinished finished = {NULL, NULL};
    RLibraryAsyncWaiter *waiter = state->waiters.head;

    while (waiter != NULL) {
        RLibraryAsyncWaiter *next = waiter->next;
        RLibraryAsyncBroadcastReceive *receive = receive_of(waiter);

        if (available_locked(state, receive)) {
            r_library_internal_async_wait_remove(&state->waiters, waiter);
            if (r_library_internal_async_waiter_select(waiter)) {
                (void)settle_locked(state, receive);
                r_library_internal_async_finished_append(&finished, waiter);
            }
        }
        waiter = next;
    }
    return finished.head;
}

static void complete_all(RLibraryAsyncBroadcastState *state, RLibraryAsyncWaiter *finished) {
    while (finished != NULL) {
        RLibraryAsyncWaiter *next = finished->finished_next;

        write_outcome(state, receive_of(finished));
        r_library_internal_async_waiter_finish(finished);
        finished = next;
    }
}

RStdAsyncBroadcastNewResult r_library_internal_async_broadcast_new(RRuntimeAllocator *allocator,
                                                                   RRuntimeTypeInfo element,
                                                                   RStdAsyncCloneFn clone,
                                                                   size_t capacity) {
    RStdAsyncBroadcastNewResult result = {0};
    RLibraryAsyncBroadcastState *state = NULL;
    RRuntimeAllocationStatus status;
    const size_t element_alignment = element.alignment == 0U ? 1U : element.alignment;
    size_t value_offset = sizeof(RLibraryAsyncBroadcastNode);
    size_t ring_offset = sizeof(RLibraryAsyncBroadcastState);

    if ((allocator == NULL) || ((element_alignment & (element_alignment - 1U)) != 0U)) {
        r_library_internal_async_contract_violation();
    }
    if (capacity == 0U) {
        r_library_internal_async_contract_violation();
    }
    ring_offset = (ring_offset + _Alignof(RLibraryAsyncBroadcastNode *) - 1U) &
                  ~(_Alignof(RLibraryAsyncBroadcastNode *) - 1U);
    value_offset = (value_offset + element_alignment - 1U) & ~(element_alignment - 1U);
    if ((capacity > (SIZE_MAX - ring_offset) / sizeof(RLibraryAsyncBroadcastNode *)) ||
        (element.size > SIZE_MAX - value_offset)) {
        status = R_RUNTIME_ALLOCATION_SIZE_OVERFLOW;
    } else {
        status = r_runtime_allocator_allocate(allocator,
                                              ring_offset +
                                                  capacity * sizeof(RLibraryAsyncBroadcastNode *),
                                              _Alignof(RLibraryAsyncBroadcastState),
                                              (void **)&state);
    }
    if ((status == R_RUNTIME_ALLOCATION_OK) && (pthread_mutex_init(&state->lock, NULL) != 0)) {
        r_runtime_allocator_deallocate(state, _Alignof(RLibraryAsyncBroadcastState));
        status = R_RUNTIME_ALLOCATION_EXHAUSTED;
    }
    if (status != R_RUNTIME_ALLOCATION_OK) {
        result.status = R_STD_ASYNC_CALL_ALLOCATION_ERROR;
        result.error = r_library_internal_async_allocation_error(status);
        return result;
    }
    state->allocator = allocator;
    state->element = element;
    state->clone = clone;
    state->ring = (RLibraryAsyncBroadcastNode **)(void *)((unsigned char *)state + ring_offset);
    for (size_t index = 0U; index < capacity; ++index) {
        state->ring[index] = NULL;
    }
    state->waiters = (RLibraryAsyncWaitList){NULL, NULL, 0U};
    state->capacity = capacity;
    state->value_offset = value_offset;
    state->node_size = value_offset + element.size;
    state->node_alignment = element_alignment > _Alignof(RLibraryAsyncBroadcastNode)
                                ? element_alignment
                                : _Alignof(RLibraryAsyncBroadcastNode);
    state->allocation_alignment = _Alignof(RLibraryAsyncBroadcastState);
    state->references = 1U;
    state->senders = 1U;
    state->subscribers = 0U;
    state->tail = UINT64_C(0);
    result.status = R_STD_ASYNC_CALL_SUCCESS;
    result.value.state = state;
    return result;
}

RStdAsyncBroadcast r_library_internal_async_broadcast_clone(const RStdAsyncBroadcast *sender) {
    RLibraryAsyncBroadcastState *state;

    if ((sender == NULL) || (sender->state == NULL)) {
        r_library_internal_async_contract_violation();
    }
    state = sender->state;
    broadcast_lock(state);
    retain_locked(state);
    state->senders += 1U;
    broadcast_unlock(state);
    return (RStdAsyncBroadcast){state};
}

/* The last broadcast handle closes every waiting receive. */
void r_library_internal_async_broadcast_release(RLibraryAsyncBroadcastState *state) {
    RLibraryAsyncWaiter *finished = NULL;

    broadcast_lock(state);
    state->senders -= 1U;
    if (state->senders == 0U) {
        finished = wake_locked(state);
    }
    broadcast_unlock(state);
    complete_all(state, finished);
    state_release(state);
}

RStdAsyncSubscribeResult
r_library_internal_async_broadcast_subscribe(const RStdAsyncBroadcast *sender) {
    RStdAsyncSubscribeResult result = {0};
    RLibraryAsyncBroadcastState *state;
    RLibraryAsyncBroadcastQueue *queue = NULL;
    RRuntimeAllocationStatus status;

    if ((sender == NULL) || (sender->state == NULL)) {
        r_library_internal_async_contract_violation();
    }
    state = sender->state;
    status = r_runtime_allocator_allocate(state->allocator,
                                          sizeof(RLibraryAsyncBroadcastQueue),
                                          _Alignof(RLibraryAsyncBroadcastQueue),
                                          (void **)&queue);
    if (status != R_RUNTIME_ALLOCATION_OK) {
        result.status = R_STD_ASYNC_CALL_ALLOCATION_ERROR;
        result.error = r_library_internal_async_allocation_error(status);
        return result;
    }
    broadcast_lock(state);
    retain_locked(state);
    if (state->subscribers >= (SIZE_MAX / 2U)) {
        broadcast_unlock(state);
        r_library_internal_async_count_overflow();
    }
    state->subscribers += 1U;
    queue->state = state;
    queue->references = 1U;
    queue->next = state->tail;
    broadcast_unlock(state);
    result.status = R_STD_ASYNC_CALL_SUCCESS;
    result.value.queue = queue;
    return result;
}

void r_library_internal_async_broadcast_unsubscribe(RLibraryAsyncBroadcastQueue *queue) {
    RLibraryAsyncBroadcastState *state = queue->state;

    broadcast_lock(state);
    state->subscribers -= 1U;
    broadcast_unlock(state);
    queue_release(queue);
}

RStdAsyncPublishResult r_library_internal_async_broadcast_publish(const RStdAsyncBroadcast *sender,
                                                                  void *staged_value) {
    RStdAsyncPublishResult result = {0};
    RLibraryAsyncBroadcastState *state;
    RLibraryAsyncBroadcastNode *node = NULL;
    RLibraryAsyncBroadcastNode *overwritten = NULL;
    RLibraryAsyncWaiter *finished;
    RRuntimeAllocationStatus status;

    if ((sender == NULL) || (sender->state == NULL)) {
        r_library_internal_async_contract_violation();
    }
    state = sender->state;
    status = r_runtime_allocator_allocate(
        state->allocator, state->node_size, state->node_alignment, (void **)&node);
    if (status != R_RUNTIME_ALLOCATION_OK) {
        if (state->element.drop != NULL) {
            state->element.drop(staged_value);
        }
        result.status = R_STD_ASYNC_CALL_ALLOCATION_ERROR;
        result.error = r_library_internal_async_allocation_error(status);
        return result;
    }
    node->references = 1U;
    if (state->element.move_initialize != NULL) {
        state->element.move_initialize(node_value(state, node), staged_value);
    } else if (state->element.size != 0U) {
        (void)memcpy(node_value(state, node), staged_value, state->element.size);
    }
    broadcast_lock(state);
    result.count = state->subscribers;
    if (state->subscribers == 0U) {
        broadcast_unlock(state);
        node_free(state, node);
        result.status = R_STD_ASYNC_CALL_SUCCESS;
        return result;
    }
    overwritten = node_unref_locked(state->ring[state->tail % state->capacity]);
    state->ring[state->tail % state->capacity] = node;
    state->tail += 1U;
    finished = wake_locked(state);
    broadcast_unlock(state);
    complete_all(state, finished);
    node_free(state, overwritten);
    result.status = R_STD_ASYNC_CALL_SUCCESS;
    return result;
}

RStdAsyncStartResult
r_library_internal_async_broadcast_receive(const RStdAsyncBroadcastReceiver *receiver,
                                           RStdAsyncBroadcastReceiveLayout layout) {
    const RRuntimeTypeInfo payload_type = {
        sizeof(RLibraryAsyncBroadcastReceive),
        _Alignof(RLibraryAsyncBroadcastReceive),
        receive_move,
        receive_drop,
    };
    RLibraryAsyncBroadcastReceive payload;
    RLibraryAsyncBroadcastState *state;
    RRuntimeTaskPrepareResult prepared;
    RRuntimeTaskStartResult started;
    RStdAsyncStartResult result = {0};

    if ((receiver == NULL) || (receiver->queue == NULL) ||
        (layout.result.size < sizeof(uint32_t)) ||
        (layout.tag_offset > layout.result.size - sizeof(uint32_t)) ||
        (layout.lag_offset > layout.result.size - sizeof(uint64_t)) ||
        (layout.value_offset > layout.result.size)) {
        r_library_internal_async_contract_violation();
    }
    state = receiver->queue->state;
    prepared = r_runtime_task_external_start_prepare(
        payload_type, layout.result, receive_external_start, receive_external_cancel);
    if (prepared.status != R_RUNTIME_TASK_START_OK) {
        result.error = prepared.status == R_RUNTIME_TASK_START_RUNTIME_STOPPING
                           ? R_STD_ASYNC_START_RUNTIME_STOPPING
                           : R_STD_ASYNC_START_REFUSAL();
        return result;
    }
    (void)memset(&payload, 0, sizeof(payload));
    payload.queue = receiver->queue;
    payload.tag_offset = layout.tag_offset;
    payload.value_offset = layout.value_offset;
    payload.lag_offset = layout.lag_offset;
    broadcast_lock(state);
    if (receiver->queue->references >= (SIZE_MAX / 2U)) {
        broadcast_unlock(state);
        r_library_internal_async_count_overflow();
    }
    receiver->queue->references += 1U;
    broadcast_unlock(state);
    started = r_runtime_task_start_commit(&prepared.transaction, &payload);
    if (started.status != R_RUNTIME_TASK_START_OK) {
        receive_drop(&payload);
        result.error = started.status == R_RUNTIME_TASK_START_RUNTIME_STOPPING
                           ? R_STD_ASYNC_START_RUNTIME_STOPPING
                           : R_STD_ASYNC_START_REFUSAL();
        return result;
    }
    result.is_ok = 1;
    result.task = started.task;
    return result;
}

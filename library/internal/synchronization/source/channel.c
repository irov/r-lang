#include "r_library_sync_internal.h"

#include "r_runtime_0_1.h"

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct RLibrarySyncChannelNode {
    struct RLibrarySyncChannelNode *next;
} RLibrarySyncChannelNode;

typedef struct RLibrarySyncRendezvousSender {
    struct RLibrarySyncRendezvousSender *next;
    void *staged_value;
    _Bool matched;
    _Bool disconnected;
} RLibrarySyncRendezvousSender;

typedef struct RLibrarySyncRendezvousReceiver {
    void *result_storage;
    _Bool matched;
} RLibrarySyncRendezvousReceiver;

struct RLibrarySyncChannelState {
    pthread_mutex_t mutex;
    pthread_cond_t send_condition;
    pthread_cond_t receive_condition;
    RRuntimeAllocator *allocator;
    RRuntimeTypeInfo element;
    RLibrarySyncChannelNode *node_head;
    RLibrarySyncChannelNode *node_tail;
    RLibrarySyncRendezvousSender *rendezvous_head;
    RLibrarySyncRendezvousSender *rendezvous_tail;
    RLibrarySyncRendezvousReceiver *rendezvous_receiver;
    /* Asynchronous receivers (std.sync::receive), oldest first; linked only while no value is
     * queued. */
    RLibrarySyncChannelWaiter *waiter_head;
    RLibrarySyncChannelWaiter *waiter_tail;
    /* Asynchronous reservations (std.sync::reserve), oldest first; linked only while every slot
     * is queued or reserved. */
    RLibrarySyncChannelWaiter *reserve_head;
    RLibrarySyncChannelWaiter *reserve_tail;
    unsigned char *ring;
    size_t allocation_alignment;
    size_t references;
    size_t sender_count;
    size_t capacity;
    size_t length;
    /* Outstanding permits: each holds one slot and counts as a sender endpoint. */
    size_t reserved;
    size_t head;
    size_t tail;
    _Bool bounded;
    _Bool receiver_alive;
};

typedef struct RLibrarySyncDetachedValues {
    RLibrarySyncChannelNode *nodes;
    size_t ring_head;
    size_t ring_count;
    RLibrarySyncChannelWaiter *finished;
} RLibrarySyncDetachedValues;

static void channel_lock(RLibrarySyncChannelState *state) {
    if (pthread_mutex_lock(&state->mutex) != 0) {
        r_library_internal_sync_contract_violation();
    }
}

static void channel_unlock(RLibrarySyncChannelState *state) {
    if (pthread_mutex_unlock(&state->mutex) != 0) {
        r_library_internal_sync_contract_violation();
    }
}

static void channel_wait(pthread_cond_t *condition, RLibrarySyncChannelState *state) {
    if (pthread_cond_wait(condition, &state->mutex) != 0) {
        r_library_internal_sync_contract_violation();
    }
}

static void channel_signal(pthread_cond_t *condition) {
    if (pthread_cond_signal(condition) != 0) {
        r_library_internal_sync_contract_violation();
    }
}

static void channel_broadcast(pthread_cond_t *condition) {
    if (pthread_cond_broadcast(condition) != 0) {
        r_library_internal_sync_contract_violation();
    }
}

static _Bool align_up(size_t value, size_t alignment, size_t *result) {
    const size_t mask = alignment - 1U;

    if (value > (SIZE_MAX - mask)) {
        return 0;
    }
    *result = (value + mask) & ~mask;
    return 1;
}

static RStdAllocError allocation_error(RRuntimeAllocationStatus status) {
    switch (status) {
    case R_RUNTIME_ALLOCATION_EXHAUSTED:
        return R_STD_ALLOC_REFUSAL();
    case R_RUNTIME_ALLOCATION_SIZE_OVERFLOW:
        return R_STD_ALLOC_ERROR_SIZE_OVERFLOW;
    case R_RUNTIME_ALLOCATION_UNSUPPORTED_ALIGNMENT:
        return R_STD_ALLOC_ERROR_UNSUPPORTED_ALIGNMENT;
    case R_RUNTIME_ALLOCATION_OK:
    case R_RUNTIME_ALLOCATION_INVALID:
        r_library_internal_sync_contract_violation();
    }
    r_library_internal_sync_contract_violation();
}

static void state_deallocate(RLibrarySyncChannelState *state) {
    size_t alignment;

    alignment = state->allocation_alignment;
    if (pthread_cond_destroy(&state->receive_condition) != 0) {
        r_library_internal_sync_contract_violation();
    }
    if (pthread_cond_destroy(&state->send_condition) != 0) {
        r_library_internal_sync_contract_violation();
    }
    if (pthread_mutex_destroy(&state->mutex) != 0) {
        r_library_internal_sync_contract_violation();
    }
    r_runtime_allocator_deallocate(state, alignment);
}

static void state_release(RLibrarySyncChannelState *state) {
    _Bool destroy;

    channel_lock(state);
    state->references -= 1U;
    destroy = state->references == 0U;
    channel_unlock(state);
    if (destroy) {
        state_deallocate(state);
    }
}

static void list_link(RLibrarySyncChannelWaiter **head,
                      RLibrarySyncChannelWaiter **tail,
                      RLibrarySyncChannelWaiter *waiter) {
    waiter->next = NULL;
    waiter->previous = *tail;
    if (*tail == NULL) {
        *head = waiter;
    } else {
        (*tail)->next = waiter;
    }
    *tail = waiter;
    waiter->linked = 1;
}

static void list_unlink(RLibrarySyncChannelWaiter **head,
                        RLibrarySyncChannelWaiter **tail,
                        RLibrarySyncChannelWaiter *waiter) {
    if (waiter->previous == NULL) {
        *head = waiter->next;
    } else {
        waiter->previous->next = waiter->next;
    }
    if (waiter->next == NULL) {
        *tail = waiter->previous;
    } else {
        waiter->next->previous = waiter->previous;
    }
    waiter->next = NULL;
    waiter->previous = NULL;
    waiter->linked = 0;
}

static void waiter_link(RLibrarySyncChannelState *state, RLibrarySyncChannelWaiter *waiter) {
    list_link(&state->waiter_head, &state->waiter_tail, waiter);
}

static void waiter_unlink(RLibrarySyncChannelState *state, RLibrarySyncChannelWaiter *waiter) {
    list_unlink(&state->waiter_head, &state->waiter_tail, waiter);
}

/* Under the lock: whether a bounded channel has a slot that is neither queued nor reserved. */
static _Bool slot_free_locked(const RLibrarySyncChannelState *state) {
    return state->length + state->reserved < state->capacity;
}

/* Under the lock: a permit holds one slot and counts as a sender endpoint. */
static void take_permit_locked(RLibrarySyncChannelState *state) {
    if ((state->sender_count >= (SIZE_MAX / 2U)) || (state->references == SIZE_MAX)) {
        channel_unlock(state);
        r_runtime_panic(R_RUNTIME_PANIC_REFERENCE_COUNT_OVERFLOW,
                        (RRuntimeSourceSpan){UINT32_C(0), UINT32_C(0), UINT32_C(0)});
    }
    state->reserved += 1U;
    state->sender_count += 1U;
    state->references += 1U;
}

/* Under the lock: free slots go to the oldest reservations that can still complete. */
static RLibrarySyncChannelWaiter *grant_reservations_locked(RLibrarySyncChannelState *state) {
    RLibrarySyncChannelWaiter *finished = NULL;
    RLibrarySyncChannelWaiter *finished_tail = NULL;

    while ((state->reserve_head != NULL) && state->receiver_alive && slot_free_locked(state)) {
        RLibrarySyncChannelWaiter *waiter = state->reserve_head;
        const RStdSyncPermit permit = {state};

        list_unlink(&state->reserve_head, &state->reserve_tail, waiter);
        if (!waiter->select(waiter)) {
            continue;
        }
        take_permit_locked(state);
        (void)memcpy(waiter->value, &permit, sizeof(permit));
        waiter->received = 1;
        waiter->finished_next = NULL;
        if (finished_tail == NULL) {
            finished = waiter;
        } else {
            finished_tail->finished_next = waiter;
        }
        finished_tail = waiter;
    }
    return finished;
}

/* Under the lock: hands the staged value to the oldest waiter that can still complete. */
static RLibrarySyncChannelWaiter *deliver_locked(RLibrarySyncChannelState *state,
                                                 void *staged_value) {
    while (state->waiter_head != NULL) {
        RLibrarySyncChannelWaiter *waiter = state->waiter_head;

        waiter_unlink(state, waiter);
        if (waiter->select(waiter)) {
            r_library_internal_sync_move_initialize(state->element, waiter->value, staged_value);
            waiter->received = 1;
            return waiter;
        }
    }
    return NULL;
}

/* Under the lock: every waiter that can still complete learns that no value will come. */
static RLibrarySyncChannelWaiter *disconnect_waiters_locked(RLibrarySyncChannelState *state) {
    RLibrarySyncChannelWaiter *finished = NULL;

    while (state->waiter_head != NULL) {
        RLibrarySyncChannelWaiter *waiter = state->waiter_head;

        waiter_unlink(state, waiter);
        if (waiter->select(waiter)) {
            waiter->received = 0;
            waiter->finished_next = finished;
            finished = waiter;
        }
    }
    return finished;
}

/* Under the lock: every reservation that can still complete learns that the receiver is gone. */
static RLibrarySyncChannelWaiter *
disconnect_reservations_locked(RLibrarySyncChannelState *state,
                               RLibrarySyncChannelWaiter *finished) {
    while (state->reserve_head != NULL) {
        RLibrarySyncChannelWaiter *waiter = state->reserve_head;

        list_unlink(&state->reserve_head, &state->reserve_tail, waiter);
        if (waiter->select(waiter)) {
            waiter->received = 0;
            waiter->finished_next = finished;
            finished = waiter;
        }
    }
    return finished;
}

/* After the lock: a finished waiter may be released by its own finish. */
static void finish_waiters(RLibrarySyncChannelWaiter *finished) {
    while (finished != NULL) {
        RLibrarySyncChannelWaiter *next = finished->finished_next;

        finished->finish(finished);
        finished = next;
    }
}

static RStdSyncChannelCallStatus state_create(RRuntimeAllocator *allocator,
                                              RRuntimeTypeInfo element,
                                              _Bool bounded,
                                              size_t capacity,
                                              RLibrarySyncChannelState **result,
                                              RStdAllocError *error) {
    RRuntimeAllocationStatus allocation_status;
    RLibrarySyncChannelState *state;
    size_t allocation_alignment = _Alignof(RLibrarySyncChannelState);
    size_t ring_offset = sizeof(RLibrarySyncChannelState);
    size_t allocation_size = sizeof(RLibrarySyncChannelState);
    int native_error;

    *result = NULL;
    if (bounded && (capacity != 0U)) {
        if (!align_up(sizeof(RLibrarySyncChannelState), element.alignment, &ring_offset) ||
            (capacity > ((SIZE_MAX - ring_offset) / element.size))) {
            *error = R_STD_ALLOC_ERROR_SIZE_OVERFLOW;
            return R_STD_SYNC_CHANNEL_CALL_ALLOCATION_ERROR;
        }
        allocation_size = ring_offset + (capacity * element.size);
        if (element.alignment > allocation_alignment) {
            allocation_alignment = element.alignment;
        }
    }
    allocation_status = r_runtime_allocator_allocate(
        allocator, allocation_size, allocation_alignment, (void **)&state);
    if (allocation_status != R_RUNTIME_ALLOCATION_OK) {
        *error = allocation_error(allocation_status);
        return R_STD_SYNC_CHANNEL_CALL_ALLOCATION_ERROR;
    }
    (void)memset(state, 0, sizeof(*state));
    native_error = pthread_mutex_init(&state->mutex, NULL);
    if (native_error != 0) {
        r_runtime_allocator_deallocate(state, allocation_alignment);
        *error = R_STD_ALLOC_ERROR_OUT_OF_MEMORY;
        return R_STD_SYNC_CHANNEL_CALL_ALLOCATION_ERROR;
    }
    native_error = pthread_cond_init(&state->send_condition, NULL);
    if (native_error != 0) {
        (void)pthread_mutex_destroy(&state->mutex);
        r_runtime_allocator_deallocate(state, allocation_alignment);
        *error = R_STD_ALLOC_ERROR_OUT_OF_MEMORY;
        return R_STD_SYNC_CHANNEL_CALL_ALLOCATION_ERROR;
    }
    native_error = pthread_cond_init(&state->receive_condition, NULL);
    if (native_error != 0) {
        (void)pthread_cond_destroy(&state->send_condition);
        (void)pthread_mutex_destroy(&state->mutex);
        r_runtime_allocator_deallocate(state, allocation_alignment);
        *error = R_STD_ALLOC_ERROR_OUT_OF_MEMORY;
        return R_STD_SYNC_CHANNEL_CALL_ALLOCATION_ERROR;
    }
    state->allocator = allocator;
    state->element = element;
    state->allocation_alignment = allocation_alignment;
    state->references = 1U;
    state->capacity = capacity;
    state->bounded = bounded;
    state->receiver_alive = 1;
    if (bounded && (capacity != 0U)) {
        state->ring = (unsigned char *)state + ring_offset;
    }
    *result = state;
    return R_STD_SYNC_CHANNEL_CALL_SUCCESS;
}

RStdSyncChannelCreateResult r_library_internal_sync_channel_create(RRuntimeAllocator *allocator,
                                                                   RRuntimeTypeInfo element) {
    RStdSyncChannelCreateResult result = {0};

    result.status = state_create(allocator, element, 0, 0U, &result.value.state, &result.error);
    return result;
}

RStdSyncSyncChannelCreateResult r_library_internal_sync_sync_channel_create(
    RRuntimeAllocator *allocator, RRuntimeTypeInfo element, size_t capacity) {
    RStdSyncSyncChannelCreateResult result = {0};

    result.status =
        state_create(allocator, element, 1, capacity, &result.value.state, &result.error);
    return result;
}

static void retain_sender(RLibrarySyncChannelState *state, _Bool bounded) {
    (void)bounded;
    channel_lock(state);
    if ((state->sender_count >= (SIZE_MAX / 2U)) || (state->references == SIZE_MAX)) {
        channel_unlock(state);
        r_runtime_panic(R_RUNTIME_PANIC_REFERENCE_COUNT_OVERFLOW,
                        (RRuntimeSourceSpan){UINT32_C(0), UINT32_C(0), UINT32_C(0)});
    }
    state->sender_count += 1U;
    state->references += 1U;
    channel_unlock(state);
}

RStdSyncSender r_library_internal_sync_channel_sender(const RLibrarySyncChannelState *state) {
    RLibrarySyncChannelState *mutable_state = (RLibrarySyncChannelState *)state;

    retain_sender(mutable_state, 0);
    return (RStdSyncSender){mutable_state};
}

RStdSyncSyncSender
r_library_internal_sync_channel_sync_sender(const RLibrarySyncChannelState *state) {
    RLibrarySyncChannelState *mutable_state = (RLibrarySyncChannelState *)state;

    retain_sender(mutable_state, 1);
    return (RStdSyncSyncSender){mutable_state};
}

RStdSyncReceiver r_library_internal_sync_channel_receiver(RLibrarySyncChannelState **factory_state,
                                                          _Bool bounded) {
    RLibrarySyncChannelState *state;

    state = *factory_state;
    (void)bounded;
    *factory_state = NULL;
    return (RStdSyncReceiver){state};
}

RStdSyncSender r_library_internal_sync_channel_clone_sender(const RLibrarySyncChannelState *state) {
    return r_library_internal_sync_channel_sender(state);
}

RStdSyncSyncSender
r_library_internal_sync_channel_clone_sync_sender(const RLibrarySyncChannelState *state) {
    return r_library_internal_sync_channel_sync_sender(state);
}

static _Bool node_geometry(const RLibrarySyncChannelState *state,
                           size_t *value_offset,
                           size_t *allocation_size,
                           size_t *allocation_alignment) {
    if (!align_up(sizeof(RLibrarySyncChannelNode), state->element.alignment, value_offset) ||
        (state->element.size > (SIZE_MAX - *value_offset))) {
        return 0;
    }
    *allocation_size = *value_offset + state->element.size;
    *allocation_alignment = _Alignof(RLibrarySyncChannelNode);
    if (state->element.alignment > *allocation_alignment) {
        *allocation_alignment = state->element.alignment;
    }
    return 1;
}

static void *node_value(RLibrarySyncChannelNode *node, size_t value_offset) {
    return (unsigned char *)node + value_offset;
}

static RStdSyncSendResult unbounded_send(RLibrarySyncChannelState *state, void *staged_value) {
    RStdSyncSendResult result;
    RLibrarySyncChannelNode *node;
    RRuntimeAllocationStatus allocation_status;
    size_t value_offset;
    size_t allocation_size;
    size_t allocation_alignment;

    RLibrarySyncChannelWaiter *waiter;

    channel_lock(state);
    if (!state->receiver_alive) {
        channel_unlock(state);
        return (RStdSyncSendResult){R_STD_SYNC_SEND_RESULT_DISCONNECTED};
    }
    waiter = deliver_locked(state, staged_value);
    if (waiter != NULL) {
        channel_unlock(state);
        waiter->finish(waiter);
        return (RStdSyncSendResult){R_STD_SYNC_SEND_RESULT_SENT};
    }
    if (state->length == SIZE_MAX) {
        channel_unlock(state);
        return (RStdSyncSendResult){R_STD_SYNC_SEND_RESULT_ALLOCATION_FAILED};
    }
    channel_unlock(state);
    if (!node_geometry(state, &value_offset, &allocation_size, &allocation_alignment)) {
        return (RStdSyncSendResult){R_STD_SYNC_SEND_RESULT_ALLOCATION_FAILED};
    }
    allocation_status = r_runtime_allocator_allocate(
        state->allocator, allocation_size, allocation_alignment, (void **)&node);
    if (allocation_status != R_RUNTIME_ALLOCATION_OK) {
        return (RStdSyncSendResult){R_STD_SYNC_SEND_RESULT_ALLOCATION_FAILED};
    }
    node->next = NULL;
    channel_lock(state);
    if (!state->receiver_alive) {
        channel_unlock(state);
        r_runtime_allocator_deallocate(node, allocation_alignment);
        return (RStdSyncSendResult){R_STD_SYNC_SEND_RESULT_DISCONNECTED};
    }
    /* A receive may have started waiting while the node was allocated. */
    waiter = deliver_locked(state, staged_value);
    if (waiter != NULL) {
        channel_unlock(state);
        r_runtime_allocator_deallocate(node, allocation_alignment);
        waiter->finish(waiter);
        return (RStdSyncSendResult){R_STD_SYNC_SEND_RESULT_SENT};
    }
    if (state->length == SIZE_MAX) {
        channel_unlock(state);
        r_runtime_allocator_deallocate(node, allocation_alignment);
        return (RStdSyncSendResult){R_STD_SYNC_SEND_RESULT_ALLOCATION_FAILED};
    }
    r_library_internal_sync_move_initialize(
        state->element, node_value(node, value_offset), staged_value);
    if (state->node_tail == NULL) {
        state->node_head = node;
    } else {
        state->node_tail->next = node;
    }
    state->node_tail = node;
    state->length += 1U;
    channel_signal(&state->receive_condition);
    channel_unlock(state);
    result.kind = R_STD_SYNC_SEND_RESULT_SENT;
    return result;
}

static void ring_push(RLibrarySyncChannelState *state, void *staged_value) {
    void *destination = state->ring + (state->tail * state->element.size);

    r_library_internal_sync_move_initialize(state->element, destination, staged_value);
    state->tail = (state->tail + 1U) % state->capacity;
    state->length += 1U;
}

static RStdSyncSendResult rendezvous_send(RLibrarySyncChannelState *state, void *staged_value) {
    RLibrarySyncRendezvousSender waiter = {NULL, staged_value, 0, 0};

    if (state->rendezvous_receiver != NULL) {
        RLibrarySyncRendezvousReceiver *receiver = state->rendezvous_receiver;

        state->rendezvous_receiver = NULL;
        r_library_internal_sync_move_initialize(
            state->element, receiver->result_storage, staged_value);
        receiver->matched = 1;
        channel_signal(&state->receive_condition);
        return (RStdSyncSendResult){R_STD_SYNC_SEND_RESULT_SENT};
    }
    if (state->rendezvous_tail == NULL) {
        state->rendezvous_head = &waiter;
    } else {
        state->rendezvous_tail->next = &waiter;
    }
    state->rendezvous_tail = &waiter;
    while (!waiter.matched && !waiter.disconnected) {
        channel_wait(&state->send_condition, state);
    }
    if (waiter.matched) {
        return (RStdSyncSendResult){R_STD_SYNC_SEND_RESULT_SENT};
    }
    return (RStdSyncSendResult){R_STD_SYNC_SEND_RESULT_DISCONNECTED};
}

RStdSyncSendResult r_library_internal_sync_channel_send(RLibrarySyncChannelState *state,
                                                        void *staged_value,
                                                        _Bool bounded) {
    RStdSyncSendResult result;
    RLibrarySyncChannelWaiter *waiter = NULL;

    if (!bounded) {
        return unbounded_send(state, staged_value);
    }
    channel_lock(state);
    if (state->capacity == 0U) {
        if (!state->receiver_alive) {
            result.kind = R_STD_SYNC_SEND_RESULT_DISCONNECTED;
        } else {
            if (state->rendezvous_receiver == NULL) {
                waiter = deliver_locked(state, staged_value);
            }
            result = waiter != NULL ? (RStdSyncSendResult){R_STD_SYNC_SEND_RESULT_SENT}
                                    : rendezvous_send(state, staged_value);
        }
        channel_unlock(state);
        if (waiter != NULL) {
            waiter->finish(waiter);
        }
        return result;
    }
    while (!slot_free_locked(state) && state->receiver_alive) {
        channel_wait(&state->send_condition, state);
    }
    if (!state->receiver_alive) {
        result.kind = R_STD_SYNC_SEND_RESULT_DISCONNECTED;
    } else {
        waiter = deliver_locked(state, staged_value);
        if (waiter == NULL) {
            ring_push(state, staged_value);
            channel_signal(&state->receive_condition);
        }
        result.kind = R_STD_SYNC_SEND_RESULT_SENT;
    }
    channel_unlock(state);
    if (waiter != NULL) {
        waiter->finish(waiter);
    }
    return result;
}

RStdSyncTrySendResult r_library_internal_sync_channel_try_send(RLibrarySyncChannelState *state,
                                                               void *staged_value) {
    RStdSyncTrySendResult result;
    RLibrarySyncChannelWaiter *waiter = NULL;

    channel_lock(state);
    if (!state->receiver_alive) {
        result.kind = R_STD_SYNC_TRY_SEND_RESULT_DISCONNECTED;
    } else if (state->capacity == 0U) {
        if (state->rendezvous_receiver == NULL) {
            waiter = deliver_locked(state, staged_value);
            result.kind =
                waiter != NULL ? R_STD_SYNC_TRY_SEND_RESULT_SENT : R_STD_SYNC_TRY_SEND_RESULT_FULL;
        } else {
            RLibrarySyncRendezvousReceiver *receiver = state->rendezvous_receiver;

            state->rendezvous_receiver = NULL;
            r_library_internal_sync_move_initialize(
                state->element, receiver->result_storage, staged_value);
            receiver->matched = 1;
            result.kind = R_STD_SYNC_TRY_SEND_RESULT_SENT;
            channel_signal(&state->receive_condition);
        }
    } else if (!slot_free_locked(state)) {
        result.kind = R_STD_SYNC_TRY_SEND_RESULT_FULL;
    } else {
        waiter = deliver_locked(state, staged_value);
        if (waiter == NULL) {
            ring_push(state, staged_value);
            channel_signal(&state->receive_condition);
        }
        result.kind = R_STD_SYNC_TRY_SEND_RESULT_SENT;
    }
    channel_unlock(state);
    if (waiter != NULL) {
        waiter->finish(waiter);
    }
    return result;
}

static RStdSyncRecvResult
rendezvous_recv(RLibrarySyncChannelState *state, void *result_storage, _Bool blocking) {
    RLibrarySyncRendezvousSender *sender = state->rendezvous_head;

    if (sender != NULL) {
        state->rendezvous_head = sender->next;
        if (state->rendezvous_head == NULL) {
            state->rendezvous_tail = NULL;
        }
        sender->next = NULL;
        r_library_internal_sync_move_initialize(
            state->element, result_storage, sender->staged_value);
        sender->matched = 1;
        channel_broadcast(&state->send_condition);
        return (RStdSyncRecvResult){R_STD_SYNC_RECV_RESULT_RECEIVED};
    }
    if (state->sender_count == 0U) {
        return (RStdSyncRecvResult){R_STD_SYNC_RECV_RESULT_DISCONNECTED};
    }
    if (!blocking) {
        return (RStdSyncRecvResult){R_STD_SYNC_RECV_RESULT_DISCONNECTED};
    }
    {
        RLibrarySyncRendezvousReceiver receiver = {result_storage, 0};

        state->rendezvous_receiver = &receiver;
        while (!receiver.matched && (state->sender_count != 0U)) {
            channel_wait(&state->receive_condition, state);
        }
        if (receiver.matched) {
            return (RStdSyncRecvResult){R_STD_SYNC_RECV_RESULT_RECEIVED};
        }
        state->rendezvous_receiver = NULL;
        return (RStdSyncRecvResult){R_STD_SYNC_RECV_RESULT_DISCONNECTED};
    }
}

static RStdSyncRecvResult queued_recv(RLibrarySyncChannelState *state,
                                      void *result_storage,
                                      _Bool blocking,
                                      RLibrarySyncChannelWaiter **granted) {
    RLibrarySyncChannelNode *node = NULL;
    size_t node_value_offset = 0U;
    size_t node_allocation_size = 0U;
    size_t node_alignment = 0U;

    while ((state->length == 0U) && (state->sender_count != 0U) && blocking) {
        channel_wait(&state->receive_condition, state);
    }
    if (state->length == 0U) {
        if (state->sender_count == 0U) {
            return (RStdSyncRecvResult){R_STD_SYNC_RECV_RESULT_DISCONNECTED};
        }
        return (RStdSyncRecvResult){R_STD_SYNC_RECV_RESULT_DISCONNECTED};
    }
    if (state->bounded) {
        void *source = state->ring + (state->head * state->element.size);

        r_library_internal_sync_move_initialize(state->element, result_storage, source);
        state->head = (state->head + 1U) % state->capacity;
    } else {
        _Bool geometry_valid;

        node = state->node_head;
        state->node_head = node->next;
        if (state->node_head == NULL) {
            state->node_tail = NULL;
        }
        geometry_valid =
            node_geometry(state, &node_value_offset, &node_allocation_size, &node_alignment);
        (void)geometry_valid;
        r_library_internal_sync_move_initialize(
            state->element, result_storage, node_value(node, node_value_offset));
    }
    state->length -= 1U;
    if (state->bounded) {
        *granted = grant_reservations_locked(state);
    }
    channel_signal(&state->send_condition);
    channel_unlock(state);
    if (node != NULL) {
        r_runtime_allocator_deallocate(node, node_alignment);
    }
    channel_lock(state);
    (void)node_allocation_size;
    return (RStdSyncRecvResult){R_STD_SYNC_RECV_RESULT_RECEIVED};
}

RStdSyncRecvResult r_library_internal_sync_channel_recv(RLibrarySyncChannelState *state,
                                                        void *result_storage,
                                                        _Bool blocking) {
    RStdSyncRecvResult result;
    RLibrarySyncChannelWaiter *granted = NULL;

    channel_lock(state);
    if (state->bounded && (state->capacity == 0U)) {
        result = rendezvous_recv(state, result_storage, blocking);
    } else {
        result = queued_recv(state, result_storage, blocking, &granted);
    }
    channel_unlock(state);
    finish_waiters(granted);
    return result;
}

RStdSyncTryRecvResult r_library_internal_sync_channel_try_recv(RLibrarySyncChannelState *state,
                                                               void *result_storage) {
    RStdSyncRecvResult received;
    RStdSyncTryRecvResult result;
    RLibrarySyncChannelWaiter *granted = NULL;

    channel_lock(state);
    if (state->bounded && (state->capacity == 0U)) {
        received = rendezvous_recv(state, result_storage, 0);
    } else {
        received = queued_recv(state, result_storage, 0, &granted);
    }
    if (received.kind == R_STD_SYNC_RECV_RESULT_RECEIVED) {
        result.kind = R_STD_SYNC_TRY_RECV_RESULT_RECEIVED;
    } else {
        result.kind = state->sender_count == 0U ? R_STD_SYNC_TRY_RECV_RESULT_DISCONNECTED
                                                : R_STD_SYNC_TRY_RECV_RESULT_EMPTY;
    }
    channel_unlock(state);
    finish_waiters(granted);
    return result;
}

void r_library_internal_sync_channel_retain(RLibrarySyncChannelState *state) {
    channel_lock(state);
    if (state->references == SIZE_MAX) {
        channel_unlock(state);
        r_runtime_panic(R_RUNTIME_PANIC_REFERENCE_COUNT_OVERFLOW,
                        (RRuntimeSourceSpan){UINT32_C(0), UINT32_C(0), UINT32_C(0)});
    }
    state->references += 1U;
    channel_unlock(state);
}

void r_library_internal_sync_channel_release(RLibrarySyncChannelState *state) {
    state_release(state);
}

RLibrarySyncReceiveBegin
r_library_internal_sync_channel_receive_begin(RLibrarySyncChannelState *state,
                                              RLibrarySyncChannelWaiter *waiter) {
    RLibrarySyncChannelWaiter *granted = NULL;
    RLibrarySyncChannelNode *node = NULL;
    size_t node_value_offset = 0U;
    size_t node_allocation_size = 0U;
    size_t node_alignment = 0U;

    waiter->next = NULL;
    waiter->previous = NULL;
    waiter->finished_next = NULL;
    waiter->linked = 0;
    waiter->received = 0;
    channel_lock(state);
    if (state->bounded && (state->capacity == 0U) && (state->rendezvous_head != NULL)) {
        RLibrarySyncRendezvousSender *sender = state->rendezvous_head;

        if (!waiter->select(waiter)) {
            channel_unlock(state);
            return R_LIBRARY_SYNC_RECEIVE_REJECTED;
        }
        state->rendezvous_head = sender->next;
        if (state->rendezvous_head == NULL) {
            state->rendezvous_tail = NULL;
        }
        sender->next = NULL;
        r_library_internal_sync_move_initialize(
            state->element, waiter->value, sender->staged_value);
        sender->matched = 1;
        waiter->received = 1;
        channel_broadcast(&state->send_condition);
        channel_unlock(state);
        return R_LIBRARY_SYNC_RECEIVE_FINISHED;
    }
    if (state->length != 0U) {
        if (!waiter->select(waiter)) {
            channel_unlock(state);
            return R_LIBRARY_SYNC_RECEIVE_REJECTED;
        }
        if (state->bounded) {
            void *source = state->ring + (state->head * state->element.size);

            r_library_internal_sync_move_initialize(state->element, waiter->value, source);
            state->head = (state->head + 1U) % state->capacity;
        } else {
            _Bool geometry_valid;

            node = state->node_head;
            state->node_head = node->next;
            if (state->node_head == NULL) {
                state->node_tail = NULL;
            }
            geometry_valid =
                node_geometry(state, &node_value_offset, &node_allocation_size, &node_alignment);
            (void)geometry_valid;
            r_library_internal_sync_move_initialize(
                state->element, waiter->value, node_value(node, node_value_offset));
        }
        state->length -= 1U;
        waiter->received = 1;
        if (state->bounded) {
            granted = grant_reservations_locked(state);
        }
        channel_signal(&state->send_condition);
        channel_unlock(state);
        finish_waiters(granted);
        if (node != NULL) {
            r_runtime_allocator_deallocate(node, node_alignment);
        }
        (void)node_allocation_size;
        return R_LIBRARY_SYNC_RECEIVE_FINISHED;
    }
    if ((state->sender_count == 0U) || !state->receiver_alive) {
        const _Bool selected = waiter->select(waiter);

        channel_unlock(state);
        return selected ? R_LIBRARY_SYNC_RECEIVE_FINISHED : R_LIBRARY_SYNC_RECEIVE_REJECTED;
    }
    waiter_link(state, waiter);
    channel_unlock(state);
    return R_LIBRARY_SYNC_RECEIVE_PENDING;
}

void r_library_internal_sync_channel_receive_withdraw(RLibrarySyncChannelState *state,
                                                      RLibrarySyncChannelWaiter *waiter) {
    channel_lock(state);
    if (waiter->linked) {
        waiter_unlink(state, waiter);
    }
    channel_unlock(state);
}

static RLibrarySyncDetachedValues disconnect_receiver(RLibrarySyncChannelState *state) {
    RLibrarySyncDetachedValues detached = {0};
    RLibrarySyncRendezvousSender *sender;

    channel_lock(state);
    state->receiver_alive = 0;
    if (!state->bounded) {
        detached.nodes = state->node_head;
        state->node_head = NULL;
        state->node_tail = NULL;
    } else if (state->capacity != 0U) {
        detached.ring_head = state->head;
        detached.ring_count = state->length;
        state->head = 0U;
        state->tail = 0U;
    }
    state->length = 0U;
    sender = state->rendezvous_head;
    while (sender != NULL) {
        sender->disconnected = 1;
        sender = sender->next;
    }
    state->rendezvous_head = NULL;
    state->rendezvous_tail = NULL;
    detached.finished = disconnect_reservations_locked(state, disconnect_waiters_locked(state));
    channel_broadcast(&state->send_condition);
    channel_broadcast(&state->receive_condition);
    channel_unlock(state);
    return detached;
}

static void drop_detached_values(RLibrarySyncChannelState *state,
                                 RLibrarySyncDetachedValues detached) {
    RLibrarySyncChannelNode *node = detached.nodes;
    size_t value_offset = 0U;
    size_t allocation_size = 0U;
    size_t allocation_alignment = 0U;
    size_t index;

    if (node != NULL) {
        const _Bool geometry_valid =
            node_geometry(state, &value_offset, &allocation_size, &allocation_alignment);

        (void)geometry_valid;
    }
    while (node != NULL) {
        RLibrarySyncChannelNode *next = node->next;

        if (state->element.drop != NULL) {
            state->element.drop(node_value(node, value_offset));
        }
        r_runtime_allocator_deallocate(node, allocation_alignment);
        node = next;
    }
    for (index = 0U; index < detached.ring_count; index += 1U) {
        const size_t slot = (detached.ring_head + index) % state->capacity;

        if (state->element.drop != NULL) {
            state->element.drop(state->ring + (slot * state->element.size));
        }
    }
    (void)allocation_size;
}

static void destroy_receiver_right(RLibrarySyncChannelState **state_slot) {
    RLibrarySyncChannelState *state;
    RLibrarySyncDetachedValues detached;

    state = *state_slot;
    if (state == NULL) {
        return;
    }
    *state_slot = NULL;
    detached = disconnect_receiver(state);
    finish_waiters(detached.finished);
    drop_detached_values(state, detached);
    state_release(state);
}

static void destroy_sender(RLibrarySyncChannelState **state_slot, _Bool bounded) {
    RLibrarySyncChannelState *state;
    RLibrarySyncChannelWaiter *finished = NULL;
    _Bool destroy;

    state = *state_slot;
    if (state == NULL) {
        return;
    }
    *state_slot = NULL;
    (void)bounded;
    channel_lock(state);
    state->sender_count -= 1U;
    state->references -= 1U;
    if (state->sender_count == 0U) {
        finished = disconnect_waiters_locked(state);
        channel_broadcast(&state->receive_condition);
    }
    destroy = state->references == 0U;
    channel_unlock(state);
    finish_waiters(finished);
    if (destroy) {
        state_deallocate(state);
    }
}

static void move_state(RLibrarySyncChannelState **destination, RLibrarySyncChannelState **source) {
    *destination = *source;
    *source = NULL;
}

void r_library_internal_sync_channel_move(RStdSyncChannel *destination, RStdSyncChannel *source) {
    move_state(&destination->state, &source->state);
}

void r_library_internal_sync_channel_destroy(RStdSyncChannel *factory) {
    destroy_receiver_right(&factory->state);
}

void r_library_internal_sync_sync_channel_move(RStdSyncSyncChannel *destination,
                                               RStdSyncSyncChannel *source) {
    move_state(&destination->state, &source->state);
}

void r_library_internal_sync_sync_channel_destroy(RStdSyncSyncChannel *factory) {
    destroy_receiver_right(&factory->state);
}

void r_library_internal_sync_sender_move(RStdSyncSender *destination, RStdSyncSender *source) {
    move_state(&destination->state, &source->state);
}

void r_library_internal_sync_sender_destroy(RStdSyncSender *sender) {
    destroy_sender(&sender->state, 0);
}

void r_library_internal_sync_sync_sender_move(RStdSyncSyncSender *destination,
                                              RStdSyncSyncSender *source) {
    move_state(&destination->state, &source->state);
}

void r_library_internal_sync_sync_sender_destroy(RStdSyncSyncSender *sender) {
    destroy_sender(&sender->state, 1);
}

void r_library_internal_sync_receiver_move(RStdSyncReceiver *destination,
                                           RStdSyncReceiver *source) {
    move_state(&destination->state, &source->state);
}

void r_library_internal_sync_receiver_destroy(RStdSyncReceiver *receiver) {
    destroy_receiver_right(&receiver->state);
}

/* R-LIB-0016 (L30): a rendezvous channel has no slot to reserve. */
static void require_slots(RLibrarySyncChannelState *state) {
    if (!state->bounded || (state->capacity == 0U)) {
        channel_unlock(state);
        r_library_internal_sync_contract_violation();
    }
}

RLibrarySyncReceiveBegin
r_library_internal_sync_channel_reserve_begin(RLibrarySyncChannelState *state,
                                              RLibrarySyncChannelWaiter *waiter) {
    const RStdSyncPermit permit = {state};

    waiter->next = NULL;
    waiter->previous = NULL;
    waiter->finished_next = NULL;
    waiter->linked = 0;
    waiter->received = 0;
    channel_lock(state);
    require_slots(state);
    if (!state->receiver_alive) {
        const _Bool selected = waiter->select(waiter);

        channel_unlock(state);
        return selected ? R_LIBRARY_SYNC_RECEIVE_FINISHED : R_LIBRARY_SYNC_RECEIVE_REJECTED;
    }
    if ((state->reserve_head == NULL) && slot_free_locked(state)) {
        if (!waiter->select(waiter)) {
            channel_unlock(state);
            return R_LIBRARY_SYNC_RECEIVE_REJECTED;
        }
        take_permit_locked(state);
        (void)memcpy(waiter->value, &permit, sizeof(permit));
        waiter->received = 1;
        channel_unlock(state);
        return R_LIBRARY_SYNC_RECEIVE_FINISHED;
    }
    list_link(&state->reserve_head, &state->reserve_tail, waiter);
    channel_unlock(state);
    return R_LIBRARY_SYNC_RECEIVE_PENDING;
}

void r_library_internal_sync_channel_reserve_withdraw(RLibrarySyncChannelState *state,
                                                      RLibrarySyncChannelWaiter *waiter) {
    channel_lock(state);
    if (waiter->linked) {
        list_unlink(&state->reserve_head, &state->reserve_tail, waiter);
    }
    channel_unlock(state);
}

RStdSyncTryReserveResult
r_library_internal_sync_channel_try_reserve(RLibrarySyncChannelState *state) {
    RStdSyncTryReserveResult result = {0};

    channel_lock(state);
    require_slots(state);
    if (!state->receiver_alive) {
        result.kind = R_STD_SYNC_TRY_RESERVE_RESULT_DISCONNECTED;
    } else if ((state->reserve_head != NULL) || !slot_free_locked(state)) {
        result.kind = R_STD_SYNC_TRY_RESERVE_RESULT_FULL;
    } else {
        take_permit_locked(state);
        result.kind = R_STD_SYNC_TRY_RESERVE_RESULT_RESERVED;
        result.permit.state = state;
    }
    channel_unlock(state);
    return result;
}

/* Under the lock: the permit's sender endpoint ends; the last one disconnects the receiver. */
static RLibrarySyncChannelWaiter *end_permit_sender_locked(RLibrarySyncChannelState *state,
                                                           _Bool *destroy) {
    RLibrarySyncChannelWaiter *finished = NULL;

    state->sender_count -= 1U;
    state->references -= 1U;
    if (state->sender_count == 0U) {
        finished = disconnect_waiters_locked(state);
        channel_broadcast(&state->receive_condition);
    }
    *destroy = state->references == 0U;
    return finished;
}

void r_library_internal_sync_permit_move(RStdSyncPermit *destination, RStdSyncPermit *source) {
    move_state(&destination->state, &source->state);
}

void r_library_internal_sync_permit_destroy(RStdSyncPermit *permit) {
    RLibrarySyncChannelState *state = permit->state;
    RLibrarySyncChannelWaiter *granted;
    RLibrarySyncChannelWaiter *disconnected;
    _Bool destroy;

    if (state == NULL) {
        return;
    }
    permit->state = NULL;
    channel_lock(state);
    if (state->reserved == 0U) {
        channel_unlock(state);
        r_library_internal_sync_contract_violation();
    }
    state->reserved -= 1U;
    granted = grant_reservations_locked(state);
    channel_signal(&state->send_condition);
    disconnected = end_permit_sender_locked(state, &destroy);
    channel_unlock(state);
    finish_waiters(granted);
    finish_waiters(disconnected);
    if (destroy) {
        state_deallocate(state);
    }
}

/* The reserved slot takes the value at once: a waiting receiver, else the queue. After the
 * receiver is gone the value is destroyed, so a send through a permit neither waits nor fails. */
void r_library_internal_sync_permit_send(RStdSyncPermit *permit, void *staged_value) {
    RLibrarySyncChannelState *state = permit->state;
    RLibrarySyncChannelWaiter *delivered = NULL;
    RLibrarySyncChannelWaiter *granted = NULL;
    RLibrarySyncChannelWaiter *disconnected;
    _Bool discard = 0;
    _Bool destroy;

    if (state == NULL) {
        r_library_internal_sync_contract_violation();
    }
    permit->state = NULL;
    channel_lock(state);
    if (state->reserved == 0U) {
        channel_unlock(state);
        r_library_internal_sync_contract_violation();
    }
    state->reserved -= 1U;
    if (!state->receiver_alive) {
        discard = 1;
    } else {
        delivered = deliver_locked(state, staged_value);
        if (delivered == NULL) {
            ring_push(state, staged_value);
            channel_signal(&state->receive_condition);
        } else {
            granted = grant_reservations_locked(state);
            channel_signal(&state->send_condition);
        }
    }
    disconnected = end_permit_sender_locked(state, &destroy);
    channel_unlock(state);
    if (delivered != NULL) {
        delivered->finish(delivered);
    }
    finish_waiters(granted);
    finish_waiters(disconnected);
    if (discard && (state->element.drop != NULL)) {
        state->element.drop(staged_value);
    }
    if (destroy) {
        state_deallocate(state);
    }
}

#include "r_std_async.h"
#include "r_std_sync.h"

#include "r_runtime_allocator.h"
#include "r_runtime_task.h"
#include "r_runtime_type.h"

#include <sched.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* R-SLIB-ASYNC-0013..0015 (L30): FIFO hand-off, try_* never overtaking a waiter, cancellation that
 * never loses the resource, and destruction of the protected value exactly once. */

#define R_TEST_CHECK(expression)                                                                   \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            (void)fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expression);   \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static _Atomic unsigned value_drops;

typedef struct RTestValue {
    int32_t number;
} RTestValue;

static void value_move(void *destination, void *source) {
    (void)memcpy(destination, source, sizeof(RTestValue));
}

static void value_drop(void *value) {
    (void)value;
    atomic_fetch_add_explicit(&value_drops, 1U, memory_order_relaxed);
}

static const RRuntimeTypeInfo value_type = {
    sizeof(RTestValue), _Alignof(RTestValue), value_move, value_drop};

static void mutex_guard_move(void *destination, void *source) {
    r_std_async_mutex_guard_move_initialize(destination, source);
}

static void mutex_guard_drop(void *value) {
    r_std_async_mutex_guard_destroy(value);
}

static void read_guard_move(void *destination, void *source) {
    r_std_async_rw_read_guard_move_initialize(destination, source);
}

static void read_guard_drop(void *value) {
    r_std_async_rw_read_guard_destroy(value);
}

static void write_guard_move(void *destination, void *source) {
    r_std_async_rw_write_guard_move_initialize(destination, source);
}

static void write_guard_drop(void *value) {
    r_std_async_rw_write_guard_destroy(value);
}

static void permit_move(void *destination, void *source) {
    r_std_async_semaphore_permit_move_initialize(destination, source);
}

static void permit_drop(void *value) {
    r_std_async_semaphore_permit_destroy(value);
}

static const RRuntimeTypeInfo mutex_guard_type = {
    sizeof(RStdAsyncMutexGuard), _Alignof(RStdAsyncMutexGuard), mutex_guard_move, mutex_guard_drop};
static const RRuntimeTypeInfo read_guard_type = {
    sizeof(RStdAsyncRwReadGuard), _Alignof(RStdAsyncRwReadGuard), read_guard_move, read_guard_drop};
static const RRuntimeTypeInfo write_guard_type = {sizeof(RStdAsyncRwWriteGuard),
                                                  _Alignof(RStdAsyncRwWriteGuard),
                                                  write_guard_move,
                                                  write_guard_drop};
static const RRuntimeTypeInfo permit_type = {
    sizeof(RStdAsyncSemaphorePermit), _Alignof(RStdAsyncSemaphorePermit), permit_move, permit_drop};

/* Waits up to one second for a task to reach its terminal state. */
static _Bool completes(RRuntimeTask *task) {
    const clock_t start = clock();

    while (r_runtime_task_state(task) != R_RUNTIME_TASK_COMPLETED) {
        if ((clock() - start) > CLOCKS_PER_SEC) {
            return 0;
        }
        (void)sched_yield();
    }
    return 1;
}

/* A task that stays pending for a while has not been handed the resource. */
static _Bool stays_pending(RRuntimeTask *task) {
    for (unsigned attempt = 0U; attempt < 2000U; ++attempt) {
        if (r_runtime_task_state(task) == R_RUNTIME_TASK_COMPLETED) {
            return 0;
        }
        (void)sched_yield();
    }
    return 1;
}

static int test_mutex(RRuntimeAllocator *allocator) {
    RTestValue initial = {5};
    RStdAsyncMutexNewResult created = r_std_async_mutex_new(allocator, value_type, &initial);
    RStdAsyncMutex mutex;
    RStdAsyncMutex second;
    RStdAsyncMutexGuard first_guard = {NULL};
    RStdAsyncMutexGuard guard = {NULL};
    RStdAsyncStartResult waiting_first;
    RStdAsyncStartResult waiting_cancelled;
    RStdAsyncStartResult waiting_second;

    atomic_store(&value_drops, 0U);
    R_TEST_CHECK(created.status == R_STD_ASYNC_CALL_SUCCESS);
    mutex = created.value;
    second = r_std_async_clone_mutex(&mutex);
    R_TEST_CHECK(r_std_async_try_lock(&mutex, &first_guard));
    R_TEST_CHECK(!r_std_async_try_lock(&second, &guard));
    waiting_first = r_std_async_lock(&mutex, mutex_guard_type);
    waiting_cancelled = r_std_async_lock(&second, mutex_guard_type);
    waiting_second = r_std_async_lock(&second, mutex_guard_type);
    R_TEST_CHECK(waiting_first.is_ok && waiting_cancelled.is_ok && waiting_second.is_ok);
    R_TEST_CHECK(stays_pending(waiting_first.task));
    r_std_async_cancel(&waiting_cancelled.task);
    ((RTestValue *)r_std_async_mutex_guard_mut(&first_guard))->number += 1;
    r_std_async_unlock(&first_guard, R_STD_ASYNC_GUARD_MUTEX);
    R_TEST_CHECK(first_guard.state == NULL);
    R_TEST_CHECK(completes(waiting_first.task));
    R_TEST_CHECK(stays_pending(waiting_second.task));
    R_TEST_CHECK(!r_std_async_try_lock(&mutex, &guard));
    R_TEST_CHECK(r_runtime_task_await(&waiting_first.task, &guard) == R_RUNTIME_TASK_AWAIT_OK);
    R_TEST_CHECK(((const RTestValue *)r_std_async_mutex_guard_ref(&guard))->number == 6);
    r_std_async_mutex_guard_destroy(&guard);
    R_TEST_CHECK(completes(waiting_second.task));
    R_TEST_CHECK(r_runtime_task_await(&waiting_second.task, &guard) == R_RUNTIME_TASK_AWAIT_OK);
    r_std_async_mutex_destroy(&mutex);
    r_std_async_mutex_destroy(&second);
    R_TEST_CHECK(atomic_load(&value_drops) == 0U);
    r_std_async_mutex_guard_destroy(&guard);
    R_TEST_CHECK(atomic_load(&value_drops) == 1U);
    return 0;
}

/* Many waiters, every other one cancelled while it waits; the lock always reaches the rest. */
static int test_mutex_cancellation_stress(RRuntimeAllocator *allocator) {
    enum {
        R_TEST_WAITERS = 64
    };
    RTestValue initial = {0};
    RStdAsyncMutexNewResult created = r_std_async_mutex_new(allocator, value_type, &initial);
    RStdAsyncMutexGuard guard = {NULL};
    RStdAsyncStartResult waiting[R_TEST_WAITERS];

    atomic_store(&value_drops, 0U);
    R_TEST_CHECK(created.status == R_STD_ASYNC_CALL_SUCCESS);
    R_TEST_CHECK(r_std_async_try_lock(&created.value, &guard));
    for (unsigned index = 0U; index < R_TEST_WAITERS; ++index) {
        waiting[index] = r_std_async_lock(&created.value, mutex_guard_type);
        R_TEST_CHECK(waiting[index].is_ok);
    }
    for (unsigned index = 0U; index < R_TEST_WAITERS; index += 2U) {
        r_std_async_cancel(&waiting[index].task);
    }
    r_std_async_mutex_guard_destroy(&guard);
    for (unsigned index = 1U; index < R_TEST_WAITERS; index += 2U) {
        R_TEST_CHECK(r_runtime_task_await(&waiting[index].task, &guard) == R_RUNTIME_TASK_AWAIT_OK);
        ((RTestValue *)r_std_async_mutex_guard_mut(&guard))->number += 1;
        r_std_async_mutex_guard_destroy(&guard);
    }
    R_TEST_CHECK(r_std_async_try_lock(&created.value, &guard));
    R_TEST_CHECK(((const RTestValue *)r_std_async_mutex_guard_ref(&guard))->number ==
                 R_TEST_WAITERS / 2);
    r_std_async_mutex_guard_destroy(&guard);
    r_std_async_mutex_destroy(&created.value);
    R_TEST_CHECK(atomic_load(&value_drops) == 1U);
    return 0;
}

static int test_rw_lock(RRuntimeAllocator *allocator) {
    RTestValue initial = {7};
    RStdAsyncRwLockNewResult created = r_std_async_rwlock_new(allocator, value_type, &initial);
    RStdAsyncRwReadGuard first = {NULL};
    RStdAsyncRwReadGuard second = {NULL};
    RStdAsyncRwReadGuard late = {NULL};
    RStdAsyncRwWriteGuard writer = {NULL};
    RStdAsyncStartResult writing;
    RStdAsyncStartResult reading;

    atomic_store(&value_drops, 0U);
    R_TEST_CHECK(created.status == R_STD_ASYNC_CALL_SUCCESS);
    R_TEST_CHECK(r_std_async_try_read(&created.value, &first));
    R_TEST_CHECK(r_std_async_try_read(&created.value, &second));
    R_TEST_CHECK(!r_std_async_try_write(&created.value, &writer));
    writing = r_std_async_write(&created.value, write_guard_type);
    R_TEST_CHECK(writing.is_ok && stays_pending(writing.task));
    /* A queued writer holds back later readers. */
    R_TEST_CHECK(!r_std_async_try_read(&created.value, &late));
    reading = r_std_async_read(&created.value, read_guard_type);
    R_TEST_CHECK(reading.is_ok);
    R_TEST_CHECK(((const RTestValue *)r_std_async_rw_read_guard_ref(&first))->number == 7);
    r_std_async_unlock(&first, R_STD_ASYNC_GUARD_RW_READ);
    R_TEST_CHECK(stays_pending(writing.task));
    r_std_async_rw_read_guard_destroy(&second);
    R_TEST_CHECK(completes(writing.task));
    R_TEST_CHECK(stays_pending(reading.task));
    R_TEST_CHECK(r_runtime_task_await(&writing.task, &writer) == R_RUNTIME_TASK_AWAIT_OK);
    ((RTestValue *)r_std_async_rw_write_guard_mut(&writer))->number = 8;
    r_std_async_unlock(&writer, R_STD_ASYNC_GUARD_RW_WRITE);
    R_TEST_CHECK(r_runtime_task_await(&reading.task, &late) == R_RUNTIME_TASK_AWAIT_OK);
    R_TEST_CHECK(((const RTestValue *)r_std_async_rw_read_guard_ref(&late))->number == 8);
    r_std_async_rw_lock_destroy(&created.value);
    r_std_async_rw_read_guard_destroy(&late);
    R_TEST_CHECK(atomic_load(&value_drops) == 1U);
    return 0;
}

static int test_semaphore(RRuntimeAllocator *allocator) {
    RStdAsyncSemaphoreNewResult created = r_std_async_semaphore_new(allocator, 2U);
    RStdAsyncSemaphorePermit first = {NULL};
    RStdAsyncSemaphorePermit second = {NULL};
    RStdAsyncSemaphorePermit third = {NULL};
    RStdAsyncStartResult waiting;
    RStdAsyncStartResult cancelled;

    R_TEST_CHECK(created.status == R_STD_ASYNC_CALL_SUCCESS);
    R_TEST_CHECK(r_std_async_available_permits(&created.value) == 2U);
    R_TEST_CHECK(r_std_async_try_acquire(&created.value, &first));
    waiting = r_std_async_acquire(&created.value, permit_type);
    R_TEST_CHECK(waiting.is_ok && completes(waiting.task));
    R_TEST_CHECK(r_runtime_task_await(&waiting.task, &second) == R_RUNTIME_TASK_AWAIT_OK);
    R_TEST_CHECK(r_std_async_available_permits(&created.value) == 0U);
    cancelled = r_std_async_acquire(&created.value, permit_type);
    waiting = r_std_async_acquire(&created.value, permit_type);
    R_TEST_CHECK(cancelled.is_ok && waiting.is_ok && stays_pending(waiting.task));
    r_std_async_cancel(&cancelled.task);
    r_std_async_release(&first);
    R_TEST_CHECK(completes(waiting.task));
    R_TEST_CHECK(r_runtime_task_await(&waiting.task, &third) == R_RUNTIME_TASK_AWAIT_OK);
    R_TEST_CHECK(!r_std_async_try_acquire(&created.value, &first));
    r_std_async_add_permits(&created.value, 3U);
    R_TEST_CHECK(r_std_async_available_permits(&created.value) == 3U);
    r_std_async_semaphore_destroy(&created.value);
    r_std_async_semaphore_permit_destroy(&second);
    r_std_async_semaphore_permit_destroy(&third);
    return 0;
}

static int test_notify(RRuntimeAllocator *allocator) {
    RStdAsyncNotifyNewResult created = r_std_async_notify_new(allocator);
    RStdAsyncNotify clone;
    RStdAsyncStartResult first;
    RStdAsyncStartResult second;
    RStdAsyncStartResult cancelled;

    R_TEST_CHECK(created.status == R_STD_ASYNC_CALL_SUCCESS);
    clone = r_std_async_clone_notify(&created.value);
    /* A notification without a waiter is stored once. */
    r_std_async_notify_one(&created.value);
    r_std_async_notify_one(&created.value);
    first = r_std_async_notified(&clone);
    R_TEST_CHECK(first.is_ok && completes(first.task));
    R_TEST_CHECK(r_runtime_task_await(&first.task, NULL) == R_RUNTIME_TASK_AWAIT_OK);
    first = r_std_async_notified(&clone);
    R_TEST_CHECK(first.is_ok && stays_pending(first.task));
    second = r_std_async_notified(&created.value);
    R_TEST_CHECK(second.is_ok && stays_pending(second.task));
    r_std_async_notify_all(&created.value);
    R_TEST_CHECK(completes(first.task) && completes(second.task));
    R_TEST_CHECK(r_runtime_task_await(&first.task, NULL) == R_RUNTIME_TASK_AWAIT_OK);
    R_TEST_CHECK(r_runtime_task_await(&second.task, NULL) == R_RUNTIME_TASK_AWAIT_OK);
    /* notify_all stores nothing; a cancelled waiter does not absorb notify_one. */
    cancelled = r_std_async_notified(&created.value);
    R_TEST_CHECK(cancelled.is_ok && stays_pending(cancelled.task));
    r_std_async_cancel(&cancelled.task);
    r_std_async_notify_one(&created.value);
    first = r_std_async_notified(&clone);
    R_TEST_CHECK(first.is_ok && completes(first.task));
    R_TEST_CHECK(r_runtime_task_await(&first.task, NULL) == R_RUNTIME_TASK_AWAIT_OK);
    r_std_async_notify_destroy(&created.value);
    r_std_async_notify_destroy(&clone);
    return 0;
}

typedef struct RTestReserveResult {
    uint32_t tag;
    RStdSyncPermit permit;
} RTestReserveResult;

static void reserve_result_move(void *destination, void *source) {
    RTestReserveResult *to = destination;
    RTestReserveResult *from = source;

    to->tag = from->tag;
    r_std_sync_permit_move_initialize(&to->permit, &from->permit);
}

static void reserve_result_drop(void *value) {
    RTestReserveResult *result = value;

    if (result->tag == 0U) {
        r_std_sync_permit_destroy(&result->permit);
    }
}

static RStdSyncReserveLayout reserve_layout(void) {
    const RStdSyncReserveLayout layout = {
        {sizeof(RTestReserveResult),
         _Alignof(RTestReserveResult),
         reserve_result_move,
         reserve_result_drop},
        offsetof(RTestReserveResult, tag),
        offsetof(RTestReserveResult, permit),
    };

    return layout;
}

static int test_reserve(RRuntimeAllocator *allocator) {
    RStdSyncSyncChannelCreateResult created = r_std_sync_sync_channel(allocator, value_type, 2U);
    RStdSyncSyncSender sender;
    RStdSyncReceiver receiver;
    RStdSyncTryReserveResult first;
    RStdSyncTryReserveResult second;
    RStdSyncTryReserveResult third;
    RStdAsyncStartResult cancelled;
    RStdAsyncStartResult waiting;
    RTestReserveResult reserved = {0};
    RTestValue value = {41};
    RTestValue received = {0};

    atomic_store(&value_drops, 0U);
    R_TEST_CHECK(created.status == R_STD_SYNC_CHANNEL_CALL_SUCCESS);
    sender = r_std_sync_sync_sender(&created.value);
    receiver = r_std_sync_sync_receiver(&created.value);
    first = r_std_sync_try_reserve(&sender);
    second = r_std_sync_try_reserve(&sender);
    third = r_std_sync_try_reserve(&sender);
    R_TEST_CHECK(first.kind == R_STD_SYNC_TRY_RESERVE_RESULT_RESERVED);
    R_TEST_CHECK(second.kind == R_STD_SYNC_TRY_RESERVE_RESULT_RESERVED);
    R_TEST_CHECK(third.kind == R_STD_SYNC_TRY_RESERVE_RESULT_FULL);
    R_TEST_CHECK(r_std_sync_try_send(&sender, &value).kind == R_STD_SYNC_TRY_SEND_RESULT_FULL);
    cancelled = r_std_sync_reserve(&sender, reserve_layout());
    waiting = r_std_sync_reserve(&sender, reserve_layout());
    R_TEST_CHECK(cancelled.is_ok && waiting.is_ok && stays_pending(waiting.task));
    r_std_async_cancel(&cancelled.task);
    /* A dropped permit returns its slot to the next reservation. */
    r_std_sync_permit_destroy(&second.permit);
    R_TEST_CHECK(completes(waiting.task));
    R_TEST_CHECK(r_runtime_task_await(&waiting.task, &reserved) == R_RUNTIME_TASK_AWAIT_OK);
    R_TEST_CHECK(reserved.tag == 0U);
    r_std_sync_send_permit(&first.permit, &value);
    R_TEST_CHECK(r_std_sync_recv(&receiver, &received).kind == R_STD_SYNC_RECV_RESULT_RECEIVED);
    R_TEST_CHECK(received.number == 41);
    /* Senders are gone, but the outstanding permit still counts as one. */
    r_std_sync_sync_sender_destroy(&sender);
    R_TEST_CHECK(r_std_sync_try_recv(&receiver, &received).kind ==
                 R_STD_SYNC_TRY_RECV_RESULT_EMPTY);
    value.number = 42;
    r_std_sync_send_permit(&reserved.permit, &value);
    R_TEST_CHECK(r_std_sync_recv(&receiver, &received).kind == R_STD_SYNC_RECV_RESULT_RECEIVED);
    R_TEST_CHECK(received.number == 42);
    R_TEST_CHECK(r_std_sync_try_recv(&receiver, &received).kind ==
                 R_STD_SYNC_TRY_RECV_RESULT_DISCONNECTED);
    r_std_sync_receiver_destroy(&receiver);
    return 0;
}

/* A reservation waiting when the receiver goes learns disconnection; a later permit send destroys
 * its value instead of failing. */
static int test_reserve_disconnect(RRuntimeAllocator *allocator) {
    RStdSyncSyncChannelCreateResult created = r_std_sync_sync_channel(allocator, value_type, 1U);
    RStdSyncSyncSender sender;
    RStdSyncReceiver receiver;
    RStdSyncTryReserveResult held;
    RStdAsyncStartResult waiting;
    RTestReserveResult reserved = {0};
    RTestValue value = {7};

    atomic_store(&value_drops, 0U);
    R_TEST_CHECK(created.status == R_STD_SYNC_CHANNEL_CALL_SUCCESS);
    sender = r_std_sync_sync_sender(&created.value);
    receiver = r_std_sync_sync_receiver(&created.value);
    held = r_std_sync_try_reserve(&sender);
    R_TEST_CHECK(held.kind == R_STD_SYNC_TRY_RESERVE_RESULT_RESERVED);
    waiting = r_std_sync_reserve(&sender, reserve_layout());
    R_TEST_CHECK(waiting.is_ok && stays_pending(waiting.task));
    r_std_sync_receiver_destroy(&receiver);
    R_TEST_CHECK(completes(waiting.task));
    R_TEST_CHECK(r_runtime_task_await(&waiting.task, &reserved) == R_RUNTIME_TASK_AWAIT_OK);
    R_TEST_CHECK(reserved.tag == 1U);
    R_TEST_CHECK(r_std_sync_try_reserve(&sender).kind ==
                 R_STD_SYNC_TRY_RESERVE_RESULT_DISCONNECTED);
    r_std_sync_send_permit(&held.permit, &value);
    R_TEST_CHECK(atomic_load(&value_drops) == 1U);
    r_std_sync_sync_sender_destroy(&sender);
    return 0;
}

/* A slow consumer: every send first reserves, so the channel never holds more than its capacity
 * of values while the producer runs far ahead. */
static int test_reserve_backpressure(RRuntimeAllocator *allocator) {
    enum {
        R_TEST_CAPACITY = 4,
        R_TEST_MESSAGES = 2000
    };
    RStdSyncSyncChannelCreateResult created =
        r_std_sync_sync_channel(allocator, value_type, R_TEST_CAPACITY);
    RStdSyncSyncSender sender;
    RStdSyncReceiver receiver;
    RTestValue received = {0};
    int32_t next_expected = 0;

    atomic_store(&value_drops, 0U);
    R_TEST_CHECK(created.status == R_STD_SYNC_CHANNEL_CALL_SUCCESS);
    sender = r_std_sync_sync_sender(&created.value);
    receiver = r_std_sync_sync_receiver(&created.value);
    for (int32_t message = 0; message < R_TEST_MESSAGES; ++message) {
        RStdSyncTryReserveResult slot = r_std_sync_try_reserve(&sender);
        RTestValue value = {message};

        if (slot.kind == R_STD_SYNC_TRY_RESERVE_RESULT_FULL) {
            RTestReserveResult reserved = {0};
            RStdAsyncStartResult waiting = r_std_sync_reserve(&sender, reserve_layout());

            R_TEST_CHECK(waiting.is_ok && stays_pending(waiting.task));
            R_TEST_CHECK(r_std_sync_recv(&receiver, &received).kind ==
                         R_STD_SYNC_RECV_RESULT_RECEIVED);
            R_TEST_CHECK(received.number == next_expected);
            next_expected += 1;
            R_TEST_CHECK(r_runtime_task_await(&waiting.task, &reserved) == R_RUNTIME_TASK_AWAIT_OK);
            R_TEST_CHECK(reserved.tag == 0U);
            r_std_sync_send_permit(&reserved.permit, &value);
            continue;
        }
        R_TEST_CHECK(slot.kind == R_STD_SYNC_TRY_RESERVE_RESULT_RESERVED);
        r_std_sync_send_permit(&slot.permit, &value);
    }
    r_std_sync_sync_sender_destroy(&sender);
    while (r_std_sync_recv(&receiver, &received).kind == R_STD_SYNC_RECV_RESULT_RECEIVED) {
        R_TEST_CHECK(received.number == next_expected);
        next_expected += 1;
    }
    R_TEST_CHECK(next_expected == R_TEST_MESSAGES);
    r_std_sync_receiver_destroy(&receiver);
    return 0;
}

/* A heap-owning element: every live copy owns one allocation, so clones and destructions balance.
 */
static _Atomic int live_boxes;

typedef struct RTestBox {
    int32_t *number;
} RTestBox;

static void box_move(void *destination, void *source) {
    (void)memcpy(destination, source, sizeof(RTestBox));
    ((RTestBox *)source)->number = NULL;
}

static void box_drop(void *value) {
    RTestBox *box = value;

    if (box->number != NULL) {
        free(box->number);
        box->number = NULL;
        atomic_fetch_sub_explicit(&live_boxes, 1, memory_order_relaxed);
    }
}

static _Bool box_clone(void *destination, const void *source, RStdAllocError *error) {
    const RTestBox *from = source;
    RTestBox copy = {malloc(sizeof(int32_t))};

    if (copy.number == NULL) {
        *error = R_STD_ALLOC_ERROR_OUT_OF_MEMORY;
        return 0;
    }
    *copy.number = *from->number;
    atomic_fetch_add_explicit(&live_boxes, 1, memory_order_relaxed);
    (void)memcpy(destination, &copy, sizeof(copy));
    return 1;
}

static const RRuntimeTypeInfo box_type = {sizeof(RTestBox), _Alignof(RTestBox), box_move, box_drop};

static RTestBox make_box(int32_t number) {
    RTestBox box = {malloc(sizeof(int32_t))};

    if (box.number != NULL) {
        *box.number = number;
        atomic_fetch_add_explicit(&live_boxes, 1, memory_order_relaxed);
    }
    return box;
}

typedef struct RTestBroadcastResult {
    uint32_t tag;
    union {
        RTestBox received;
        uint64_t lagged;
    } payload;
} RTestBroadcastResult;

static void broadcast_result_move(void *destination, void *source) {
    RTestBroadcastResult *to = destination;
    RTestBroadcastResult *from = source;

    to->tag = from->tag;
    if (from->tag == 0U) {
        box_move(&to->payload.received, &from->payload.received);
    } else {
        to->payload.lagged = from->payload.lagged;
    }
}

static void broadcast_result_drop(void *value) {
    RTestBroadcastResult *result = value;

    if (result->tag == 0U) {
        box_drop(&result->payload.received);
    }
}

static RStdAsyncBroadcastReceiveLayout broadcast_layout(void) {
    const RStdAsyncBroadcastReceiveLayout layout = {
        {sizeof(RTestBroadcastResult),
         _Alignof(RTestBroadcastResult),
         broadcast_result_move,
         broadcast_result_drop},
        offsetof(RTestBroadcastResult, tag),
        offsetof(RTestBroadcastResult, payload.received),
        offsetof(RTestBroadcastResult, payload.lagged),
    };

    return layout;
}

static int receive_now(const RStdAsyncBroadcastReceiver *receiver, RTestBroadcastResult *result) {
    RStdAsyncStartResult started = r_std_async_broadcast_receive(receiver, broadcast_layout());

    R_TEST_CHECK(started.is_ok && completes(started.task));
    R_TEST_CHECK(r_runtime_task_await(&started.task, result) == R_RUNTIME_TASK_AWAIT_OK);
    return 0;
}

static int test_broadcast(RRuntimeAllocator *allocator) {
    RStdAsyncBroadcastNewResult created = r_std_async_broadcast(allocator, box_type, box_clone, 2U);
    RStdAsyncBroadcast second_sender;
    RStdAsyncSubscribeResult fast;
    RStdAsyncSubscribeResult slow;
    RStdAsyncStartResult waiting;
    RTestBroadcastResult result = {0};
    RStdAsyncPublishResult published;
    RTestBox box;

    atomic_store(&live_boxes, 0);
    R_TEST_CHECK(created.status == R_STD_ASYNC_CALL_SUCCESS);
    box = make_box(0);
    published = r_std_async_publish(&created.value, &box);
    R_TEST_CHECK(published.status == R_STD_ASYNC_CALL_SUCCESS && published.count == 0U);
    R_TEST_CHECK(atomic_load(&live_boxes) == 0);
    fast = r_std_async_subscribe(&created.value);
    slow = r_std_async_subscribe(&created.value);
    R_TEST_CHECK(fast.status == R_STD_ASYNC_CALL_SUCCESS &&
                 slow.status == R_STD_ASYNC_CALL_SUCCESS);
    waiting = r_std_async_broadcast_receive(&fast.value, broadcast_layout());
    R_TEST_CHECK(waiting.is_ok && stays_pending(waiting.task));
    for (int32_t number = 1; number <= 5; ++number) {
        box = make_box(number);
        published = r_std_async_publish(&created.value, &box);
        R_TEST_CHECK(published.status == R_STD_ASYNC_CALL_SUCCESS && published.count == 2U);
        if (number == 1) {
            R_TEST_CHECK(completes(waiting.task));
            R_TEST_CHECK(r_runtime_task_await(&waiting.task, &result) == R_RUNTIME_TASK_AWAIT_OK);
            R_TEST_CHECK(result.tag == 0U && *result.payload.received.number == 1);
            broadcast_result_drop(&result);
        } else {
            R_TEST_CHECK(receive_now(&fast.value, &result) == 0);
            R_TEST_CHECK(result.tag == 0U && *result.payload.received.number == number);
            broadcast_result_drop(&result);
        }
    }
    /* Five values, capacity two: the slow receiver missed three, then reads the newest two. */
    R_TEST_CHECK(receive_now(&slow.value, &result) == 0);
    R_TEST_CHECK(result.tag == 1U && result.payload.lagged == 3U);
    R_TEST_CHECK(receive_now(&slow.value, &result) == 0);
    R_TEST_CHECK(result.tag == 0U && *result.payload.received.number == 4);
    broadcast_result_drop(&result);
    R_TEST_CHECK(receive_now(&slow.value, &result) == 0);
    R_TEST_CHECK(result.tag == 0U && *result.payload.received.number == 5);
    broadcast_result_drop(&result);
    /* The ring keeps at most capacity values alive. */
    R_TEST_CHECK(atomic_load(&live_boxes) == 2);
    second_sender = r_std_async_clone_broadcast(&created.value);
    waiting = r_std_async_broadcast_receive(&slow.value, broadcast_layout());
    R_TEST_CHECK(waiting.is_ok && stays_pending(waiting.task));
    r_std_async_broadcast_destroy(&created.value);
    R_TEST_CHECK(stays_pending(waiting.task));
    r_std_async_broadcast_destroy(&second_sender);
    R_TEST_CHECK(completes(waiting.task));
    R_TEST_CHECK(r_runtime_task_await(&waiting.task, &result) == R_RUNTIME_TASK_AWAIT_OK);
    R_TEST_CHECK(result.tag == 2U);
    r_std_async_broadcast_receiver_destroy(&fast.value);
    r_std_async_broadcast_receiver_destroy(&slow.value);
    R_TEST_CHECK(atomic_load(&live_boxes) == 0);
    return 0;
}

/* A subscriber that keeps up never lags; one that stops lags by exactly what it missed. */
static int test_broadcast_volume(RRuntimeAllocator *allocator) {
    enum {
        R_TEST_CAPACITY = 8,
        R_TEST_MESSAGES = 5000
    };
    RStdAsyncBroadcastNewResult created =
        r_std_async_broadcast(allocator, box_type, box_clone, R_TEST_CAPACITY);
    RStdAsyncSubscribeResult reader;
    RStdAsyncSubscribeResult idle;
    RTestBroadcastResult result = {0};

    atomic_store(&live_boxes, 0);
    R_TEST_CHECK(created.status == R_STD_ASYNC_CALL_SUCCESS);
    reader = r_std_async_subscribe(&created.value);
    idle = r_std_async_subscribe(&created.value);
    R_TEST_CHECK(reader.status == R_STD_ASYNC_CALL_SUCCESS &&
                 idle.status == R_STD_ASYNC_CALL_SUCCESS);
    for (int32_t number = 0; number < R_TEST_MESSAGES; ++number) {
        RTestBox box = make_box(number);

        R_TEST_CHECK(r_std_async_publish(&created.value, &box).count == 2U);
        R_TEST_CHECK(receive_now(&reader.value, &result) == 0);
        R_TEST_CHECK(result.tag == 0U && *result.payload.received.number == number);
        broadcast_result_drop(&result);
        R_TEST_CHECK(atomic_load(&live_boxes) <= R_TEST_CAPACITY);
    }
    R_TEST_CHECK(receive_now(&idle.value, &result) == 0);
    R_TEST_CHECK(result.tag == 1U && result.payload.lagged == R_TEST_MESSAGES - R_TEST_CAPACITY);
    r_std_async_broadcast_receiver_destroy(&reader.value);
    r_std_async_broadcast_receiver_destroy(&idle.value);
    r_std_async_broadcast_destroy(&created.value);
    R_TEST_CHECK(atomic_load(&live_boxes) == 0);
    return 0;
}

int main(void) {
    RRuntimeAllocator allocator;

    r_runtime_allocator_initialize(&allocator);
    R_TEST_CHECK(r_runtime_executor_lifecycle_start(&allocator) == R_RUNTIME_EXECUTOR_START_OK);
    R_TEST_CHECK(test_mutex(&allocator) == 0);
    R_TEST_CHECK(test_mutex_cancellation_stress(&allocator) == 0);
    R_TEST_CHECK(test_rw_lock(&allocator) == 0);
    R_TEST_CHECK(test_semaphore(&allocator) == 0);
    R_TEST_CHECK(test_notify(&allocator) == 0);
    R_TEST_CHECK(test_reserve(&allocator) == 0);
    R_TEST_CHECK(test_reserve_disconnect(&allocator) == 0);
    R_TEST_CHECK(test_reserve_backpressure(&allocator) == 0);
    R_TEST_CHECK(test_broadcast(&allocator) == 0);
    R_TEST_CHECK(test_broadcast_volume(&allocator) == 0);
    R_TEST_CHECK(r_runtime_executor_lifecycle_stop());
    return 0;
}

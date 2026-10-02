#include "r_std_sync.h"

#include "r_library_sync_internal.h"
#include "r_runtime_allocator.h"

#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

#define R_TEST_CHECK(condition)                                                                    \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            (void)fprintf(stderr, "sync test failed at line %d: %s\n", __LINE__, #condition);      \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

typedef struct RTestOwnedValue {
    int value;
    _Bool active;
} RTestOwnedValue;

typedef struct RTestOwnedMutex {
    RStdSyncMutex runtime;
    RTestOwnedValue value;
} RTestOwnedMutex;

typedef struct RTestIntMutex {
    RStdSyncMutex runtime;
    int value;
} RTestIntMutex;

typedef struct RTestOwnedRwLock {
    RStdSyncRwLock runtime;
    RTestOwnedValue value;
} RTestOwnedRwLock;

typedef struct RTestIntRwLock {
    RStdSyncRwLock runtime;
    int value;
} RTestIntRwLock;

typedef struct RTestOwnedOnceLock {
    RStdSyncOnceLock runtime;
    RTestOwnedValue value;
} RTestOwnedOnceLock;

typedef struct RTestIntOnceLock {
    RStdSyncOnceLock runtime;
    int value;
} RTestIntOnceLock;

typedef struct RTestContentionContext {
    RStdSyncMutex *mutex;
    _Atomic _Bool ready;
    _Atomic _Bool begin;
    _Atomic _Bool tried;
    _Atomic _Bool finished;
    RStdSyncTryLockResultKind try_kind;
    RStdSyncLockResultKind lock_kind;
    int observed;
} RTestContentionContext;

enum {
    R_TEST_BARRIER_THREADS = 4,
    R_TEST_BARRIER_GENERATIONS = 256
};

typedef struct RTestBarrierContext {
    RStdSyncBarrier *barrier;
    size_t thread_index;
    int *values;
    _Atomic unsigned int *leaders;
    _Atomic _Bool *failed;
} RTestBarrierContext;

typedef struct RTestCondvarContext {
    RStdSyncCondvar *condition;
    RStdSyncMutex *mutex;
    _Atomic unsigned int *ready_count;
    _Atomic unsigned int *completed_count;
    int observed;
    RStdSyncLockResultKind result_kind;
} RTestCondvarContext;

typedef struct RTestOnceContext {
    RStdSyncOnce *once;
    _Atomic _Bool *failed;
} RTestOnceContext;

typedef struct RTestOnceLockContext {
    RStdSyncOnceLock *lock;
    _Atomic _Bool *failed;
} RTestOnceLockContext;

typedef struct RTestCheckedOnceContext {
    _Bool succeed;
    unsigned int calls;
} RTestCheckedOnceContext;

typedef struct RTestCheckedOnceLockContext {
    _Bool succeed;
    unsigned int calls;
    int value;
} RTestCheckedOnceLockContext;

typedef struct RTestConcurrentCheckedOnceContext {
    RStdSyncOnce *once;
    _Atomic unsigned int attempts;
    _Atomic unsigned int failures;
    _Atomic unsigned int successes;
    _Atomic _Bool invalid_value;
} RTestConcurrentCheckedOnceContext;

typedef struct RTestRwReaderHoldContext {
    RStdSyncRwLock *lock;
    _Atomic unsigned int *ready_count;
    _Atomic _Bool *release;
    _Atomic _Bool *failed;
    int observed;
} RTestRwReaderHoldContext;

typedef struct RTestRwWriterHoldContext {
    RStdSyncRwLock *lock;
    _Atomic _Bool *ready;
    _Atomic _Bool *release;
    _Atomic _Bool *failed;
} RTestRwWriterHoldContext;

typedef struct RTestRwStressContext {
    RStdSyncRwLock *lock;
    size_t iterations;
    _Atomic _Bool *failed;
} RTestRwStressContext;

typedef struct RTestRwTryWriteContext {
    RStdSyncRwLock *lock;
    RStdSyncTryWriteLockResultKind kind;
} RTestRwTryWriteContext;

typedef struct RTestChannelValue {
    int value;
    _Bool active;
} RTestChannelValue;

typedef struct RTestChannelSendContext {
    const RStdSyncSyncSender *sender;
    RTestChannelValue value;
    RStdSyncSendResult result;
    _Atomic _Bool ready;
    _Atomic _Bool finished;
} RTestChannelSendContext;

typedef struct RTestChannelRecvContext {
    RStdSyncReceiver receiver;
    RTestChannelValue value;
    RStdSyncRecvResult result;
    _Atomic _Bool ready;
} RTestChannelRecvContext;

static _Atomic unsigned int r_test_moves;
static _Atomic unsigned int r_test_drops;
static _Atomic unsigned int r_test_once_calls;
static int r_test_once_value;
static _Atomic unsigned int r_test_once_lock_calls;
static _Atomic unsigned int r_test_channel_moves;
static _Atomic unsigned int r_test_channel_drops;
static int r_test_channel_drop_order[8];

static void r_test_owned_move(void *destination, void *source) {
    RTestOwnedValue *target = destination;
    RTestOwnedValue *origin = source;

    *target = *origin;
    origin->active = 0;
    (void)atomic_fetch_add_explicit(&r_test_moves, 1U, memory_order_relaxed);
}

static void r_test_owned_drop(void *value) {
    RTestOwnedValue *owned = value;

    if (owned->active) {
        owned->active = 0;
        (void)atomic_fetch_add_explicit(&r_test_drops, 1U, memory_order_relaxed);
    }
}

static RRuntimeTypeInfo r_test_owned_type(void) {
    const RRuntimeTypeInfo type = {
        sizeof(RTestOwnedValue), _Alignof(RTestOwnedValue), r_test_owned_move, r_test_owned_drop};
    return type;
}

static RRuntimeTypeInfo r_test_int_type(void) {
    const RRuntimeTypeInfo type = {sizeof(int), _Alignof(int), NULL, NULL};
    return type;
}

static void r_test_channel_value_move(void *destination, void *source) {
    RTestChannelValue *target = destination;
    RTestChannelValue *origin = source;

    *target = *origin;
    origin->active = 0;
    (void)atomic_fetch_add_explicit(&r_test_channel_moves, 1U, memory_order_relaxed);
}

static void r_test_channel_value_drop(void *value) {
    RTestChannelValue *owned = value;

    if (owned->active) {
        const unsigned int index =
            atomic_fetch_add_explicit(&r_test_channel_drops, 1U, memory_order_relaxed);

        if (index < (sizeof(r_test_channel_drop_order) / sizeof(r_test_channel_drop_order[0]))) {
            r_test_channel_drop_order[index] = owned->value;
        }
        owned->active = 0;
    }
}

static RRuntimeTypeInfo r_test_channel_value_type(void) {
    const RRuntimeTypeInfo type = {sizeof(RTestChannelValue),
                                   _Alignof(RTestChannelValue),
                                   r_test_channel_value_move,
                                   r_test_channel_value_drop};
    return type;
}

static void r_test_wait(const _Atomic _Bool *value) {
    while (!atomic_load_explicit(value, memory_order_acquire)) {
        (void)sched_yield();
    }
}

static void *r_test_contention_worker(void *context_value) {
    RTestContentionContext *context = context_value;
    RStdSyncTryLockResult tried;
    RStdSyncLockResult locked;

    atomic_store_explicit(&context->ready, 1, memory_order_release);
    r_test_wait(&context->begin);

    tried = r_std_sync_try_lock(context->mutex);
    context->try_kind = tried.kind;
    atomic_store_explicit(&context->tried, 1, memory_order_release);

    locked = r_std_sync_lock(context->mutex);
    context->lock_kind = locked.kind;
    if ((locked.kind == R_STD_SYNC_LOCK_RESULT_LOCKED) ||
        (locked.kind == R_STD_SYNC_LOCK_RESULT_POISONED)) {
        int *value = r_std_sync_mutex_guard_mut(&locked.guard);
        context->observed = *value;
        *value += 1;
        r_std_sync_unlock(&locked.guard);
    }
    atomic_store_explicit(&context->finished, 1, memory_order_release);
    return NULL;
}

static void *r_test_barrier_worker(void *context_value) {
    RTestBarrierContext *context = context_value;
    size_t generation;

    for (generation = 0U; generation < R_TEST_BARRIER_GENERATIONS; ++generation) {
        RStdSyncBarrierWaitResult first;
        RStdSyncBarrierWaitResult second;
        size_t index;

        context->values[context->thread_index] = (int)generation;
        first = r_std_sync_barrier_wait(context->barrier);
        if (first == R_STD_SYNC_BARRIER_WAIT_LEADER) {
            (void)atomic_fetch_add_explicit(
                &context->leaders[generation * 2U], 1U, memory_order_relaxed);
        }
        for (index = 0U; index < R_TEST_BARRIER_THREADS; ++index) {
            if (context->values[index] != (int)generation) {
                atomic_store_explicit(context->failed, 1, memory_order_relaxed);
            }
        }
        second = r_std_sync_barrier_wait(context->barrier);
        if (second == R_STD_SYNC_BARRIER_WAIT_LEADER) {
            (void)atomic_fetch_add_explicit(
                &context->leaders[(generation * 2U) + 1U], 1U, memory_order_relaxed);
        }
    }
    return NULL;
}

static void *r_test_condvar_worker(void *context_value) {
    RTestCondvarContext *context = context_value;
    RStdSyncLockResult locked = r_std_sync_lock(context->mutex);
    RStdSyncLockResult waited;

    if ((locked.kind != R_STD_SYNC_LOCK_RESULT_LOCKED) &&
        (locked.kind != R_STD_SYNC_LOCK_RESULT_POISONED)) {
        context->result_kind = locked.kind;
        return NULL;
    }
    (void)atomic_fetch_add_explicit(context->ready_count, 1U, memory_order_release);
    waited = r_std_sync_wait(context->condition, &locked.guard);
    context->result_kind = waited.kind;
    context->observed = *(const int *)r_std_sync_mutex_guard_ref(&waited.guard);
    (void)atomic_fetch_add_explicit(context->completed_count, 1U, memory_order_release);
    r_std_sync_unlock(&waited.guard);
    return NULL;
}

static _Bool r_test_once_initializer(void *context) {
    (void)context;
    (void)atomic_fetch_add_explicit(&r_test_once_calls, 1U, memory_order_relaxed);
    r_test_once_value = 73;
    (void)sched_yield();
    return 1;
}

static _Bool r_test_checked_once_initializer(void *context_value) {
    RTestCheckedOnceContext *context = context_value;

    context->calls += 1U;
    return context->succeed;
}

static _Bool r_test_concurrent_checked_once_initializer(void *context_value) {
    RTestConcurrentCheckedOnceContext *context = context_value;
    const unsigned int attempt =
        atomic_fetch_add_explicit(&context->attempts, 1U, memory_order_relaxed);

    if (attempt == 0U) {
        (void)sched_yield();
        return 0;
    }
    r_test_once_value = 101;
    return 1;
}

static void *r_test_concurrent_checked_once_worker(void *context_value) {
    RTestConcurrentCheckedOnceContext *context = context_value;

    if (r_std_sync_call_once(context->once, r_test_concurrent_checked_once_initializer, context)) {
        if (r_test_once_value != 101) {
            atomic_store_explicit(&context->invalid_value, 1, memory_order_relaxed);
        }
        (void)atomic_fetch_add_explicit(&context->successes, 1U, memory_order_relaxed);
    } else {
        (void)atomic_fetch_add_explicit(&context->failures, 1U, memory_order_relaxed);
    }
    return NULL;
}

static void *r_test_once_worker(void *context_value) {
    RTestOnceContext *context = context_value;

    if (!r_std_sync_call_once(context->once, r_test_once_initializer, NULL)) {
        atomic_store_explicit(context->failed, 1, memory_order_relaxed);
        return NULL;
    }
    if (r_test_once_value != 73) {
        atomic_store_explicit(context->failed, 1, memory_order_relaxed);
    }
    return NULL;
}

static _Bool r_test_once_lock_initializer(void *result, void *context) {
    int *value = result;

    (void)context;
    (void)atomic_fetch_add_explicit(&r_test_once_lock_calls, 1U, memory_order_relaxed);
    *value = 91;
    (void)sched_yield();
    return 1;
}

static _Bool r_test_checked_once_lock_initializer(void *result, void *context_value) {
    RTestCheckedOnceLockContext *context = context_value;

    context->calls += 1U;
    if (!context->succeed) {
        return 0;
    }
    *(int *)result = context->value;
    return 1;
}

static void *r_test_once_lock_worker(void *context_value) {
    RTestOnceLockContext *context = context_value;
    const int *value = r_std_sync_get_or_init(context->lock, r_test_once_lock_initializer, NULL);

    if ((value == NULL) || (*value != 91)) {
        atomic_store_explicit(context->failed, 1, memory_order_relaxed);
    }
    return NULL;
}

static void *r_test_rw_reader_hold_worker(void *context_value) {
    RTestRwReaderHoldContext *context = context_value;
    RStdSyncReadLockResult result;

    r_std_sync_read(&result, context->lock);
    if (result.kind != R_STD_SYNC_READ_LOCK_RESULT_LOCKED) {
        atomic_store_explicit(context->failed, 1, memory_order_relaxed);
        (void)atomic_fetch_add_explicit(context->ready_count, 1U, memory_order_release);
        return NULL;
    }
    context->observed = *(const int *)r_std_sync_rw_read_guard_ref(&result.guard);
    (void)atomic_fetch_add_explicit(context->ready_count, 1U, memory_order_release);
    r_test_wait(context->release);
    r_std_sync_unlock(&result.guard);
    return NULL;
}

static void *r_test_rw_writer_hold_worker(void *context_value) {
    RTestRwWriterHoldContext *context = context_value;
    RStdSyncWriteLockResult result;

    r_std_sync_write(&result, context->lock);
    if (result.kind != R_STD_SYNC_WRITE_LOCK_RESULT_LOCKED) {
        atomic_store_explicit(context->failed, 1, memory_order_relaxed);
        atomic_store_explicit(context->ready, 1, memory_order_release);
        return NULL;
    }
    *(int *)r_std_sync_rw_write_guard_mut(&result.guard) = 71;
    atomic_store_explicit(context->ready, 1, memory_order_release);
    r_test_wait(context->release);
    r_std_sync_unlock(&result.guard);
    return NULL;
}

static void *r_test_rw_stress_reader(void *context_value) {
    RTestRwStressContext *context = context_value;
    int previous = -1;
    size_t index;

    for (index = 0U; index < context->iterations; ++index) {
        RStdSyncReadLockResult result;
        int observed;

        r_std_sync_read(&result, context->lock);
        if (result.kind != R_STD_SYNC_READ_LOCK_RESULT_LOCKED) {
            atomic_store_explicit(context->failed, 1, memory_order_relaxed);
            return NULL;
        }
        observed = *(const int *)r_std_sync_rw_read_guard_ref(&result.guard);
        if (observed < previous) {
            atomic_store_explicit(context->failed, 1, memory_order_relaxed);
        }
        previous = observed;
        r_std_sync_unlock(&result.guard);
    }
    return NULL;
}

static void *r_test_rw_stress_writer(void *context_value) {
    RTestRwStressContext *context = context_value;
    size_t index;

    for (index = 0U; index < context->iterations; ++index) {
        RStdSyncWriteLockResult result;
        int *value;

        r_std_sync_write(&result, context->lock);
        if (result.kind != R_STD_SYNC_WRITE_LOCK_RESULT_LOCKED) {
            atomic_store_explicit(context->failed, 1, memory_order_relaxed);
            return NULL;
        }
        value = r_std_sync_rw_write_guard_mut(&result.guard);
        *value += 1;
        r_std_sync_unlock(&result.guard);
    }
    return NULL;
}

static void *r_test_rw_try_write_worker(void *context_value) {
    RTestRwTryWriteContext *context = context_value;
    RStdSyncTryWriteLockResult result;

    r_std_sync_try_write(&result, context->lock);
    context->kind = result.kind;
    if (result.guard.header.active) {
        r_std_sync_unlock(&result.guard);
    }
    return NULL;
}

static void *r_test_channel_send_worker(void *context_value) {
    RTestChannelSendContext *context = context_value;

    atomic_store_explicit(&context->ready, 1, memory_order_release);
    context->result = r_std_sync_sync_send(context->sender, &context->value);
    atomic_store_explicit(&context->finished, 1, memory_order_release);
    return NULL;
}

static void *r_test_channel_recv_worker(void *context_value) {
    RTestChannelRecvContext *context = context_value;

    atomic_store_explicit(&context->ready, 1, memory_order_release);
    context->result = r_std_sync_recv(&context->receiver, &context->value);
    return NULL;
}

static int r_test_lifecycle_and_move(void) {
    RRuntimeAllocator allocator;
    RTestOwnedValue staged = {17, 1};
    RTestOwnedMutex source;
    RTestOwnedMutex destination;
    RStdSyncLockResult locked;
    RStdSyncTryLockResult recursive;

    atomic_store_explicit(&r_test_moves, 0U, memory_order_relaxed);
    atomic_store_explicit(&r_test_drops, 0U, memory_order_relaxed);
    r_runtime_allocator_initialize(&allocator);
    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));

    r_std_sync_mutex_new(&source.runtime, &source.value, r_test_owned_type(), &staged);
    R_TEST_CHECK(!staged.active);
    R_TEST_CHECK(source.value.active && (source.value.value == 17));
    R_TEST_CHECK(atomic_load_explicit(&r_test_moves, memory_order_relaxed) == 1U);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(0));

    locked = r_std_sync_lock(&source.runtime);
    R_TEST_CHECK(locked.kind == R_STD_SYNC_LOCK_RESULT_LOCKED);
    R_TEST_CHECK(((const RTestOwnedValue *)r_std_sync_mutex_guard_ref(&locked.guard))->value == 17);
    ((RTestOwnedValue *)r_std_sync_mutex_guard_mut(&locked.guard))->value = 29;
    recursive = r_std_sync_try_lock(&source.runtime);
    R_TEST_CHECK(recursive.kind == R_STD_SYNC_TRY_LOCK_RESULT_WOULD_DEADLOCK);
    R_TEST_CHECK(!recursive.guard.header.active);
    r_std_sync_unlock(&locked.guard);
    R_TEST_CHECK(!locked.guard.header.active);
    r_std_sync_mutex_guard_destroy(&locked.guard);

    r_library_internal_sync_mutex_move(&destination.runtime, &destination.value, &source.runtime);
    R_TEST_CHECK(!source.runtime.initialized);
    R_TEST_CHECK(destination.value.active && (destination.value.value == 29));
    R_TEST_CHECK(atomic_load_explicit(&r_test_moves, memory_order_relaxed) == 2U);

    locked = r_std_sync_lock(&destination.runtime);
    R_TEST_CHECK(locked.kind == R_STD_SYNC_LOCK_RESULT_LOCKED);
    r_std_sync_mutex_guard_destroy(&locked.guard);
    r_std_sync_mutex_destroy(&source.runtime);
    r_std_sync_mutex_destroy(&destination.runtime);
    R_TEST_CHECK(atomic_load_explicit(&r_test_drops, memory_order_relaxed) == 1U);
    return 0;
}

static int r_test_contention_and_publication(void) {
    RTestIntMutex mutex;
    RTestContentionContext context = {0};
    RStdSyncLockResult held;
    RStdSyncLockResult observed;
    pthread_t worker;
    int staged = 1;

    r_std_sync_mutex_new(&mutex.runtime, &mutex.value, r_test_int_type(), &staged);
    held = r_std_sync_lock(&mutex.runtime);
    R_TEST_CHECK(held.kind == R_STD_SYNC_LOCK_RESULT_LOCKED);

    context.mutex = &mutex.runtime;
    R_TEST_CHECK(pthread_create(&worker, NULL, r_test_contention_worker, &context) == 0);
    r_test_wait(&context.ready);
    atomic_store_explicit(&context.begin, 1, memory_order_release);
    r_test_wait(&context.tried);
    R_TEST_CHECK(context.try_kind == R_STD_SYNC_TRY_LOCK_RESULT_WOULD_BLOCK);
    *(int *)r_std_sync_mutex_guard_mut(&held.guard) = 41;
    r_std_sync_unlock(&held.guard);

    r_test_wait(&context.finished);
    R_TEST_CHECK(pthread_join(worker, NULL) == 0);
    R_TEST_CHECK(context.lock_kind == R_STD_SYNC_LOCK_RESULT_LOCKED);
    R_TEST_CHECK(context.observed == 41);

    observed = r_std_sync_lock(&mutex.runtime);
    R_TEST_CHECK(observed.kind == R_STD_SYNC_LOCK_RESULT_LOCKED);
    R_TEST_CHECK(*(const int *)r_std_sync_mutex_guard_ref(&observed.guard) == 42);
    r_std_sync_unlock(&observed.guard);
    r_std_sync_mutex_destroy(&mutex.runtime);
    return 0;
}

static int r_test_poison_and_recovery(void) {
    RTestIntMutex source;
    RTestIntMutex destination;
    RStdSyncLockResult first;
    RStdSyncLockResult poisoned;
    RStdSyncTryLockResult still_poisoned;
    int staged = 7;

    r_std_sync_mutex_new(&source.runtime, &source.value, r_test_int_type(), &staged);
    first = r_std_sync_lock(&source.runtime);
    R_TEST_CHECK(first.kind == R_STD_SYNC_LOCK_RESULT_LOCKED);
    *(int *)r_std_sync_mutex_guard_mut(&first.guard) = 9;

    /* This is the generated unwind-drop hook. The locked abort profile cannot return from panic. */
    r_library_internal_sync_mutex_guard_destroy(&first.guard, 1);
    poisoned = r_std_sync_lock(&source.runtime);
    R_TEST_CHECK(poisoned.kind == R_STD_SYNC_LOCK_RESULT_POISONED);
    R_TEST_CHECK(*(const int *)r_std_sync_mutex_guard_ref(&poisoned.guard) == 9);
    *(int *)r_std_sync_mutex_guard_mut(&poisoned.guard) = 11;
    r_std_sync_unlock(&poisoned.guard);

    r_library_internal_sync_mutex_move(&destination.runtime, &destination.value, &source.runtime);
    still_poisoned = r_std_sync_try_lock(&destination.runtime);
    R_TEST_CHECK(still_poisoned.kind == R_STD_SYNC_TRY_LOCK_RESULT_POISONED);
    R_TEST_CHECK(*(const int *)r_std_sync_mutex_guard_ref(&still_poisoned.guard) == 11);
    r_std_sync_mutex_guard_destroy(&still_poisoned.guard);
    r_std_sync_mutex_destroy(&source.runtime);
    r_std_sync_mutex_destroy(&destination.runtime);
    return 0;
}

static int r_test_barrier(void) {
    RStdSyncBarrier source;
    RStdSyncBarrier barrier;
    RStdSyncBarrierCreateResult created;
    RStdSyncBarrierWaitResult waited;
    int values[R_TEST_BARRIER_THREADS] = {0};
    _Atomic unsigned int leaders[R_TEST_BARRIER_GENERATIONS * 2U];
    _Atomic _Bool failed;
    pthread_t threads[R_TEST_BARRIER_THREADS];
    RTestBarrierContext contexts[R_TEST_BARRIER_THREADS];
    size_t index;

    created = r_std_sync_barrier_new(&source, 0U);
    R_TEST_CHECK(created.status == R_STD_SYNC_BARRIER_CREATE_ERROR);
    R_TEST_CHECK(created.error == R_STD_SYNC_BARRIER_ERROR_ZERO_PARTICIPANTS);

    created = r_std_sync_barrier_new(&source, 1U);
    R_TEST_CHECK(created.status == R_STD_SYNC_BARRIER_CREATE_SUCCESS);
    waited = r_std_sync_barrier_wait(&source);
    R_TEST_CHECK(waited == R_STD_SYNC_BARRIER_WAIT_LEADER);
    waited = r_std_sync_barrier_wait(&source);
    R_TEST_CHECK(waited == R_STD_SYNC_BARRIER_WAIT_LEADER);
    r_library_internal_sync_barrier_move(&barrier, &source);
    R_TEST_CHECK(!source.initialized);
    R_TEST_CHECK(barrier.initialized);
    R_TEST_CHECK(barrier.participants == 1U);
    r_std_sync_barrier_destroy(&source);
    r_std_sync_barrier_destroy(&barrier);

    created = r_std_sync_barrier_new(&barrier, R_TEST_BARRIER_THREADS);
    R_TEST_CHECK(created.status == R_STD_SYNC_BARRIER_CREATE_SUCCESS);
    atomic_init(&failed, 0);
    for (index = 0U; index < R_TEST_BARRIER_GENERATIONS * 2U; ++index) {
        atomic_init(&leaders[index], 0U);
    }
    for (index = 0U; index < R_TEST_BARRIER_THREADS; ++index) {
        contexts[index].barrier = &barrier;
        contexts[index].thread_index = index;
        contexts[index].values = values;
        contexts[index].leaders = leaders;
        contexts[index].failed = &failed;
        R_TEST_CHECK(
            pthread_create(&threads[index], NULL, r_test_barrier_worker, &contexts[index]) == 0);
    }
    for (index = 0U; index < R_TEST_BARRIER_THREADS; ++index) {
        R_TEST_CHECK(pthread_join(threads[index], NULL) == 0);
    }
    R_TEST_CHECK(!atomic_load_explicit(&failed, memory_order_relaxed));
    for (index = 0U; index < R_TEST_BARRIER_GENERATIONS * 2U; ++index) {
        R_TEST_CHECK(atomic_load_explicit(&leaders[index], memory_order_relaxed) == 1U);
    }
    r_std_sync_barrier_destroy(&barrier);
    return 0;
}

static int r_test_condvar(void) {
    RTestIntMutex mutex;
    RStdSyncCondvar source;
    RStdSyncCondvar condition;
    RStdSyncLockResult locked;
    _Atomic unsigned int ready_count;
    _Atomic unsigned int completed_count;
    pthread_t threads[2];
    RTestCondvarContext contexts[2];
    unsigned int first_value_count = 0U;
    unsigned int second_value_count = 0U;
    int staged = 0;
    size_t index;

    r_std_sync_mutex_new(&mutex.runtime, &mutex.value, r_test_int_type(), &staged);
    r_std_sync_condvar_new(&source);
    r_std_sync_notify_one(&source);
    r_std_sync_notify_all(&source);
    r_library_internal_sync_condvar_move(&condition, &source);
    R_TEST_CHECK(!source.initialized && condition.initialized);
    atomic_init(&ready_count, 0U);
    atomic_init(&completed_count, 0U);

    for (index = 0U; index < 2U; ++index) {
        contexts[index].condition = &condition;
        contexts[index].mutex = &mutex.runtime;
        contexts[index].ready_count = &ready_count;
        contexts[index].completed_count = &completed_count;
        contexts[index].observed = 0;
        contexts[index].result_kind = R_STD_SYNC_LOCK_RESULT_WOULD_DEADLOCK;
        R_TEST_CHECK(
            pthread_create(&threads[index], NULL, r_test_condvar_worker, &contexts[index]) == 0);
    }

    while (atomic_load_explicit(&ready_count, memory_order_acquire) != 2U) {
        (void)sched_yield();
    }
    locked = r_std_sync_lock(&mutex.runtime);
    R_TEST_CHECK(locked.kind == R_STD_SYNC_LOCK_RESULT_LOCKED);
    *(int *)r_std_sync_mutex_guard_mut(&locked.guard) = 41;
    r_std_sync_notify_one(&condition);
    r_std_sync_unlock(&locked.guard);
    while (atomic_load_explicit(&completed_count, memory_order_acquire) == 0U) {
        (void)sched_yield();
    }
    R_TEST_CHECK(atomic_load_explicit(&completed_count, memory_order_acquire) == 1U);

    locked = r_std_sync_lock(&mutex.runtime);
    R_TEST_CHECK(locked.kind == R_STD_SYNC_LOCK_RESULT_LOCKED);
    *(int *)r_std_sync_mutex_guard_mut(&locked.guard) = 42;
    r_std_sync_notify_all(&condition);
    r_std_sync_unlock(&locked.guard);

    for (index = 0U; index < 2U; ++index) {
        R_TEST_CHECK(pthread_join(threads[index], NULL) == 0);
        R_TEST_CHECK(contexts[index].result_kind == R_STD_SYNC_LOCK_RESULT_LOCKED);
        first_value_count += contexts[index].observed == 41 ? 1U : 0U;
        second_value_count += contexts[index].observed == 42 ? 1U : 0U;
    }
    R_TEST_CHECK(first_value_count == 1U && second_value_count == 1U);
    r_std_sync_condvar_destroy(&source);
    r_std_sync_condvar_destroy(&condition);
    r_std_sync_mutex_destroy(&mutex.runtime);
    return 0;
}

static int r_test_once(void) {
    enum {
        R_TEST_ONCE_THREADS = 8
    };
    RStdSyncOnce source;
    RStdSyncOnce once;
    RStdSyncOnce completed;
    RStdSyncOnce retryable;
    RStdSyncOnce concurrent_retryable;
    RTestCheckedOnceContext attempt = {0, 0U};
    RTestConcurrentCheckedOnceContext concurrent_attempt;
    _Atomic _Bool failed;
    pthread_t threads[R_TEST_ONCE_THREADS];
    RTestOnceContext contexts[R_TEST_ONCE_THREADS];
    size_t index;

    atomic_store_explicit(&r_test_once_calls, 0U, memory_order_relaxed);
    r_test_once_value = 0;
    atomic_init(&failed, 0);
    r_std_sync_once_new(&source);
    r_library_internal_sync_once_move(&once, &source);
    R_TEST_CHECK(!source.initialized && once.initialized);

    for (index = 0U; index < R_TEST_ONCE_THREADS; ++index) {
        contexts[index].once = &once;
        contexts[index].failed = &failed;
        R_TEST_CHECK(pthread_create(&threads[index], NULL, r_test_once_worker, &contexts[index]) ==
                     0);
    }
    for (index = 0U; index < R_TEST_ONCE_THREADS; ++index) {
        R_TEST_CHECK(pthread_join(threads[index], NULL) == 0);
    }
    R_TEST_CHECK(!atomic_load_explicit(&failed, memory_order_relaxed));
    R_TEST_CHECK(atomic_load_explicit(&r_test_once_calls, memory_order_relaxed) == 1U);
    R_TEST_CHECK(r_std_sync_call_once(&once, r_test_once_initializer, NULL));
    R_TEST_CHECK(r_std_sync_call_once_force(&once, r_test_once_initializer, NULL));
    R_TEST_CHECK(atomic_load_explicit(&r_test_once_calls, memory_order_relaxed) == 1U);

    r_library_internal_sync_once_move(&completed, &once);
    R_TEST_CHECK(r_std_sync_call_once(&completed, r_test_once_initializer, NULL));
    R_TEST_CHECK(atomic_load_explicit(&r_test_once_calls, memory_order_relaxed) == 1U);

    r_std_sync_once_new(&retryable);
    R_TEST_CHECK(!r_std_sync_call_once(&retryable, r_test_checked_once_initializer, &attempt));
    R_TEST_CHECK(attempt.calls == 1U);
    attempt.succeed = 1;
    R_TEST_CHECK(r_std_sync_call_once(&retryable, r_test_checked_once_initializer, &attempt));
    R_TEST_CHECK(attempt.calls == 2U);
    R_TEST_CHECK(r_std_sync_call_once_force(&retryable, r_test_checked_once_initializer, &attempt));
    R_TEST_CHECK(attempt.calls == 2U);

    r_test_once_value = 0;
    r_std_sync_once_new(&concurrent_retryable);
    concurrent_attempt.once = &concurrent_retryable;
    atomic_init(&concurrent_attempt.attempts, 0U);
    atomic_init(&concurrent_attempt.failures, 0U);
    atomic_init(&concurrent_attempt.successes, 0U);
    atomic_init(&concurrent_attempt.invalid_value, 0);
    for (index = 0U; index < R_TEST_ONCE_THREADS; ++index) {
        R_TEST_CHECK(pthread_create(&threads[index],
                                    NULL,
                                    r_test_concurrent_checked_once_worker,
                                    &concurrent_attempt) == 0);
    }
    for (index = 0U; index < R_TEST_ONCE_THREADS; ++index) {
        R_TEST_CHECK(pthread_join(threads[index], NULL) == 0);
    }
    R_TEST_CHECK(atomic_load_explicit(&concurrent_attempt.attempts, memory_order_relaxed) == 2U);
    R_TEST_CHECK(atomic_load_explicit(&concurrent_attempt.failures, memory_order_relaxed) == 1U);
    R_TEST_CHECK(atomic_load_explicit(&concurrent_attempt.successes, memory_order_relaxed) ==
                 R_TEST_ONCE_THREADS - 1U);
    R_TEST_CHECK(!atomic_load_explicit(&concurrent_attempt.invalid_value, memory_order_relaxed));
    r_std_sync_once_destroy(&source);
    r_std_sync_once_destroy(&once);
    r_std_sync_once_destroy(&completed);
    r_std_sync_once_destroy(&retryable);
    r_std_sync_once_destroy(&concurrent_retryable);
    return 0;
}

static int r_test_once_lock(void) {
    enum {
        R_TEST_ONCE_LOCK_THREADS = 8
    };
    RTestOwnedValue staged = {17, 1};
    RTestOwnedValue occupied = {23, 1};
    RTestOwnedOnceLock source;
    RTestOwnedOnceLock destination;
    RTestIntOnceLock concurrent;
    RTestIntOnceLock retryable;
    RTestCheckedOnceLockContext attempt = {0, 0U, 117};
    RStdSyncSetResult set_result;
    _Atomic _Bool failed;
    pthread_t threads[R_TEST_ONCE_LOCK_THREADS];
    RTestOnceLockContext contexts[R_TEST_ONCE_LOCK_THREADS];
    size_t index;

    atomic_store_explicit(&r_test_moves, 0U, memory_order_relaxed);
    atomic_store_explicit(&r_test_drops, 0U, memory_order_relaxed);
    r_std_sync_once_lock(&source.runtime, &source.value, r_test_owned_type());
    R_TEST_CHECK(r_std_sync_get(&source.runtime) == NULL);
    set_result = r_std_sync_set(&source.runtime, &staged);
    R_TEST_CHECK(set_result == R_STD_SYNC_SET_RESULT_STORED);
    R_TEST_CHECK(!staged.active && source.value.active && (source.value.value == 17));
    R_TEST_CHECK(atomic_load_explicit(&r_test_moves, memory_order_relaxed) == 1U);
    set_result = r_std_sync_set(&source.runtime, &occupied);
    R_TEST_CHECK(set_result == R_STD_SYNC_SET_RESULT_OCCUPIED);
    R_TEST_CHECK(occupied.active && (occupied.value == 23));
    R_TEST_CHECK(((const RTestOwnedValue *)r_std_sync_get(&source.runtime))->value == 17);
    r_library_internal_sync_once_lock_move(
        &destination.runtime, &destination.value, &source.runtime);
    R_TEST_CHECK(!source.runtime.initialized && destination.value.active &&
                 (destination.value.value == 17));
    R_TEST_CHECK(atomic_load_explicit(&r_test_moves, memory_order_relaxed) == 2U);
    r_test_owned_drop(&occupied);
    r_std_sync_once_lock_destroy(&source.runtime);
    r_std_sync_once_lock_destroy(&destination.runtime);
    R_TEST_CHECK(atomic_load_explicit(&r_test_drops, memory_order_relaxed) == 2U);

    atomic_store_explicit(&r_test_once_lock_calls, 0U, memory_order_relaxed);
    atomic_init(&failed, 0);
    r_std_sync_once_lock(&concurrent.runtime, &concurrent.value, r_test_int_type());
    for (index = 0U; index < R_TEST_ONCE_LOCK_THREADS; ++index) {
        contexts[index].lock = &concurrent.runtime;
        contexts[index].failed = &failed;
        R_TEST_CHECK(
            pthread_create(&threads[index], NULL, r_test_once_lock_worker, &contexts[index]) == 0);
    }
    for (index = 0U; index < R_TEST_ONCE_LOCK_THREADS; ++index) {
        R_TEST_CHECK(pthread_join(threads[index], NULL) == 0);
    }
    R_TEST_CHECK(!atomic_load_explicit(&failed, memory_order_relaxed));
    R_TEST_CHECK(atomic_load_explicit(&r_test_once_lock_calls, memory_order_relaxed) == 1U);
    R_TEST_CHECK(*(const int *)r_std_sync_get(&concurrent.runtime) == 91);
    r_std_sync_once_lock_destroy(&concurrent.runtime);

    r_std_sync_once_lock(&retryable.runtime, &retryable.value, r_test_int_type());
    R_TEST_CHECK(r_std_sync_get_or_init(
                     &retryable.runtime, r_test_checked_once_lock_initializer, &attempt) == NULL);
    R_TEST_CHECK((attempt.calls == 1U) && (r_std_sync_get(&retryable.runtime) == NULL));
    attempt.succeed = 1;
    R_TEST_CHECK(*(const int *)r_std_sync_get_or_init(
                     &retryable.runtime, r_test_checked_once_lock_initializer, &attempt) == 117);
    R_TEST_CHECK(attempt.calls == 2U);
    R_TEST_CHECK(*(const int *)r_std_sync_get_or_init(
                     &retryable.runtime, r_test_checked_once_lock_initializer, &attempt) == 117);
    R_TEST_CHECK(attempt.calls == 2U);
    r_std_sync_once_lock_destroy(&retryable.runtime);
    return 0;
}

static int r_test_rw_lifecycle_move_and_same_thread(void) {
    RRuntimeAllocator allocator;
    RTestOwnedValue staged = {17, 1};
    RTestOwnedRwLock source;
    RTestOwnedRwLock destination;
    RStdSyncReadLockResult read_result;
    RStdSyncReadLockResult recursive_read;
    RStdSyncTryReadLockResult recursive_try_read;
    RStdSyncWriteLockResult recursive_write;
    RStdSyncTryWriteLockResult recursive_try_write;
    RStdSyncRwReadGuard moved_read;
    RStdSyncWriteLockResult write_result;
    RStdSyncRwWriteGuard moved_write;

    atomic_store_explicit(&r_test_moves, 0U, memory_order_relaxed);
    atomic_store_explicit(&r_test_drops, 0U, memory_order_relaxed);
    r_runtime_allocator_initialize(&allocator);
    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));

    r_std_sync_rwlock_new(&source.runtime, &source.value, r_test_owned_type(), &staged);
    R_TEST_CHECK(!staged.active);
    R_TEST_CHECK(source.value.active && (source.value.value == 17));
    R_TEST_CHECK(atomic_load_explicit(&r_test_moves, memory_order_relaxed) == 1U);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(0));

    r_std_sync_read(&read_result, &source.runtime);
    R_TEST_CHECK(read_result.kind == R_STD_SYNC_READ_LOCK_RESULT_LOCKED);
    R_TEST_CHECK(
        ((const RTestOwnedValue *)r_std_sync_rw_read_guard_ref(&read_result.guard))->value == 17);
    r_std_sync_read(&recursive_read, &source.runtime);
    R_TEST_CHECK(recursive_read.kind == R_STD_SYNC_READ_LOCK_RESULT_WOULD_DEADLOCK);
    R_TEST_CHECK(!recursive_read.guard.header.active);
    r_std_sync_try_read(&recursive_try_read, &source.runtime);
    R_TEST_CHECK(recursive_try_read.kind == R_STD_SYNC_TRY_READ_LOCK_RESULT_WOULD_DEADLOCK);
    R_TEST_CHECK(!recursive_try_read.guard.header.active);
    r_std_sync_write(&recursive_write, &source.runtime);
    R_TEST_CHECK(recursive_write.kind == R_STD_SYNC_WRITE_LOCK_RESULT_WOULD_DEADLOCK);
    R_TEST_CHECK(!recursive_write.guard.header.active);
    r_std_sync_try_write(&recursive_try_write, &source.runtime);
    R_TEST_CHECK(recursive_try_write.kind == R_STD_SYNC_TRY_WRITE_LOCK_RESULT_WOULD_DEADLOCK);
    R_TEST_CHECK(!recursive_try_write.guard.header.active);

    r_library_internal_sync_rw_read_guard_move(&moved_read, &read_result.guard);
    R_TEST_CHECK(!read_result.guard.header.active && moved_read.header.active);
    r_std_sync_rw_read_guard_destroy(&read_result.guard);
    r_std_sync_unlock(&moved_read);
    R_TEST_CHECK(!moved_read.header.active);

    r_library_internal_sync_rw_lock_move(&destination.runtime, &destination.value, &source.runtime);
    R_TEST_CHECK(!source.runtime.initialized);
    R_TEST_CHECK(destination.value.active && (destination.value.value == 17));
    R_TEST_CHECK(atomic_load_explicit(&r_test_moves, memory_order_relaxed) == 2U);

    r_std_sync_write(&write_result, &destination.runtime);
    R_TEST_CHECK(write_result.kind == R_STD_SYNC_WRITE_LOCK_RESULT_LOCKED);
    R_TEST_CHECK(
        ((const RTestOwnedValue *)r_std_sync_rw_write_guard_ref(&write_result.guard))->value == 17);
    ((RTestOwnedValue *)r_std_sync_rw_write_guard_mut(&write_result.guard))->value = 29;
    r_library_internal_sync_rw_write_guard_move(&moved_write, &write_result.guard);
    R_TEST_CHECK(!write_result.guard.header.active && moved_write.header.active);
    r_std_sync_rw_write_guard_destroy(&write_result.guard);

    r_std_sync_read(&recursive_read, &destination.runtime);
    R_TEST_CHECK(recursive_read.kind == R_STD_SYNC_READ_LOCK_RESULT_WOULD_DEADLOCK);
    r_std_sync_try_read(&recursive_try_read, &destination.runtime);
    R_TEST_CHECK(recursive_try_read.kind == R_STD_SYNC_TRY_READ_LOCK_RESULT_WOULD_DEADLOCK);
    r_std_sync_write(&recursive_write, &destination.runtime);
    R_TEST_CHECK(recursive_write.kind == R_STD_SYNC_WRITE_LOCK_RESULT_WOULD_DEADLOCK);
    r_std_sync_try_write(&recursive_try_write, &destination.runtime);
    R_TEST_CHECK(recursive_try_write.kind == R_STD_SYNC_TRY_WRITE_LOCK_RESULT_WOULD_DEADLOCK);
    r_std_sync_rw_write_guard_destroy(&moved_write);

    r_std_sync_rw_lock_destroy(&source.runtime);
    r_std_sync_rw_lock_destroy(&destination.runtime);
    R_TEST_CHECK(atomic_load_explicit(&r_test_drops, memory_order_relaxed) == 1U);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(0));
    return 0;
}

static int r_test_rw_multiple_readers_and_writer_exclusion(void) {
    enum {
        R_TEST_RW_HELD_READERS = 2
    };
    RTestIntRwLock lock;
    _Atomic unsigned int ready_count;
    _Atomic _Bool release;
    _Atomic _Bool failed;
    RTestRwReaderHoldContext contexts[R_TEST_RW_HELD_READERS];
    pthread_t readers[R_TEST_RW_HELD_READERS];
    RStdSyncTryWriteLockResult blocked_write;
    RStdSyncTryReadLockResult joined_readers;
    RStdSyncWriteLockResult exclusive;
    size_t index;
    int staged = 41;

    atomic_init(&ready_count, 0U);
    atomic_init(&release, 0);
    atomic_init(&failed, 0);
    r_std_sync_rwlock_new(&lock.runtime, &lock.value, r_test_int_type(), &staged);
    for (index = 0U; index < R_TEST_RW_HELD_READERS; ++index) {
        contexts[index].lock = &lock.runtime;
        contexts[index].ready_count = &ready_count;
        contexts[index].release = &release;
        contexts[index].failed = &failed;
        contexts[index].observed = 0;
        R_TEST_CHECK(
            pthread_create(&readers[index], NULL, r_test_rw_reader_hold_worker, &contexts[index]) ==
            0);
    }
    while (atomic_load_explicit(&ready_count, memory_order_acquire) != R_TEST_RW_HELD_READERS) {
        (void)sched_yield();
    }
    R_TEST_CHECK(!atomic_load_explicit(&failed, memory_order_relaxed));
    R_TEST_CHECK(lock.runtime.reader_count == R_TEST_RW_HELD_READERS);

    r_std_sync_try_write(&blocked_write, &lock.runtime);
    R_TEST_CHECK(blocked_write.kind == R_STD_SYNC_TRY_WRITE_LOCK_RESULT_WOULD_BLOCK);
    R_TEST_CHECK(!blocked_write.guard.header.active);
    r_std_sync_try_read(&joined_readers, &lock.runtime);
    R_TEST_CHECK(joined_readers.kind == R_STD_SYNC_TRY_READ_LOCK_RESULT_LOCKED);
    R_TEST_CHECK(*(const int *)r_std_sync_rw_read_guard_ref(&joined_readers.guard) == 41);
    r_std_sync_unlock(&joined_readers.guard);

    atomic_store_explicit(&release, 1, memory_order_release);
    for (index = 0U; index < R_TEST_RW_HELD_READERS; ++index) {
        R_TEST_CHECK(pthread_join(readers[index], NULL) == 0);
        R_TEST_CHECK(contexts[index].observed == 41);
    }
    R_TEST_CHECK(lock.runtime.reader_count == 0U);

    r_std_sync_write(&exclusive, &lock.runtime);
    R_TEST_CHECK(exclusive.kind == R_STD_SYNC_WRITE_LOCK_RESULT_LOCKED);
    *(int *)r_std_sync_rw_write_guard_mut(&exclusive.guard) = 43;
    r_std_sync_unlock(&exclusive.guard);
    r_std_sync_rw_lock_destroy(&lock.runtime);
    return 0;
}

static int r_test_rw_writer_publication(void) {
    RTestIntRwLock lock;
    _Atomic _Bool ready;
    _Atomic _Bool release;
    _Atomic _Bool failed;
    RTestRwWriterHoldContext context;
    pthread_t writer;
    RStdSyncTryReadLockResult blocked_read;
    RStdSyncTryWriteLockResult blocked_write;
    RStdSyncReadLockResult observed;
    int staged = 1;

    atomic_init(&ready, 0);
    atomic_init(&release, 0);
    atomic_init(&failed, 0);
    r_std_sync_rwlock_new(&lock.runtime, &lock.value, r_test_int_type(), &staged);
    context.lock = &lock.runtime;
    context.ready = &ready;
    context.release = &release;
    context.failed = &failed;
    R_TEST_CHECK(pthread_create(&writer, NULL, r_test_rw_writer_hold_worker, &context) == 0);
    r_test_wait(&ready);
    R_TEST_CHECK(!atomic_load_explicit(&failed, memory_order_relaxed));

    r_std_sync_try_read(&blocked_read, &lock.runtime);
    R_TEST_CHECK(blocked_read.kind == R_STD_SYNC_TRY_READ_LOCK_RESULT_WOULD_BLOCK);
    R_TEST_CHECK(!blocked_read.guard.header.active);
    r_std_sync_try_write(&blocked_write, &lock.runtime);
    R_TEST_CHECK(blocked_write.kind == R_STD_SYNC_TRY_WRITE_LOCK_RESULT_WOULD_BLOCK);
    R_TEST_CHECK(!blocked_write.guard.header.active);

    atomic_store_explicit(&release, 1, memory_order_release);
    R_TEST_CHECK(pthread_join(writer, NULL) == 0);
    r_std_sync_read(&observed, &lock.runtime);
    R_TEST_CHECK(observed.kind == R_STD_SYNC_READ_LOCK_RESULT_LOCKED);
    R_TEST_CHECK(*(const int *)r_std_sync_rw_read_guard_ref(&observed.guard) == 71);
    r_std_sync_rw_read_guard_destroy(&observed.guard);
    r_std_sync_rw_lock_destroy(&lock.runtime);
    return 0;
}

static int r_test_rw_poison(void) {
    RTestIntRwLock source;
    RTestIntRwLock destination;
    RTestIntRwLock read_only_panic;
    RStdSyncWriteLockResult write_result;
    RStdSyncRwWriteGuard moved_write;
    RStdSyncReadLockResult poisoned_read;
    RStdSyncTryWriteLockResult poisoned_write;
    RStdSyncTryWriteLockResult same_thread_conflict;
    RTestRwTryWriteContext cross_thread_contention;
    pthread_t contender;
    RStdSyncReadLockResult clean_read;
    int staged = 7;
    int clean_staged = 3;

    r_std_sync_rwlock_new(&source.runtime, &source.value, r_test_int_type(), &staged);
    r_std_sync_write(&write_result, &source.runtime);
    R_TEST_CHECK(write_result.kind == R_STD_SYNC_WRITE_LOCK_RESULT_LOCKED);
    *(int *)r_std_sync_rw_write_guard_mut(&write_result.guard) = 9;
    r_library_internal_sync_rw_write_guard_move(&moved_write, &write_result.guard);
    r_library_internal_sync_rw_write_guard_destroy(&moved_write, 1);

    r_std_sync_read(&poisoned_read, &source.runtime);
    R_TEST_CHECK(poisoned_read.kind == R_STD_SYNC_READ_LOCK_RESULT_POISONED);
    R_TEST_CHECK(*(const int *)r_std_sync_rw_read_guard_ref(&poisoned_read.guard) == 9);
    r_std_sync_try_write(&same_thread_conflict, &source.runtime);
    R_TEST_CHECK(same_thread_conflict.kind == R_STD_SYNC_TRY_WRITE_LOCK_RESULT_WOULD_DEADLOCK);
    cross_thread_contention.lock = &source.runtime;
    cross_thread_contention.kind = R_STD_SYNC_TRY_WRITE_LOCK_RESULT_LOCKED;
    R_TEST_CHECK(pthread_create(
                     &contender, NULL, r_test_rw_try_write_worker, &cross_thread_contention) == 0);
    R_TEST_CHECK(pthread_join(contender, NULL) == 0);
    R_TEST_CHECK(cross_thread_contention.kind == R_STD_SYNC_TRY_WRITE_LOCK_RESULT_WOULD_BLOCK);
    r_std_sync_unlock(&poisoned_read.guard);
    r_library_internal_sync_rw_lock_move(&destination.runtime, &destination.value, &source.runtime);
    r_std_sync_try_write(&poisoned_write, &destination.runtime);
    R_TEST_CHECK(poisoned_write.kind == R_STD_SYNC_TRY_WRITE_LOCK_RESULT_POISONED);
    r_std_sync_unlock(&poisoned_write.guard);

    r_std_sync_rwlock_new(
        &read_only_panic.runtime, &read_only_panic.value, r_test_int_type(), &clean_staged);
    r_std_sync_read(&clean_read, &read_only_panic.runtime);
    R_TEST_CHECK(clean_read.kind == R_STD_SYNC_READ_LOCK_RESULT_LOCKED);
    r_library_internal_sync_rw_read_guard_destroy(&clean_read.guard, 1);
    r_std_sync_write(&write_result, &read_only_panic.runtime);
    R_TEST_CHECK(write_result.kind == R_STD_SYNC_WRITE_LOCK_RESULT_LOCKED);
    r_std_sync_unlock(&write_result.guard);

    r_std_sync_rw_lock_destroy(&source.runtime);
    r_std_sync_rw_lock_destroy(&destination.runtime);
    r_std_sync_rw_lock_destroy(&read_only_panic.runtime);
    return 0;
}

static int r_test_rw_stress(void) {
    enum {
        R_TEST_RW_STRESS_READERS = 4,
        R_TEST_RW_STRESS_WRITERS = 2,
        R_TEST_RW_STRESS_ITERATIONS = 1000
    };
    RTestIntRwLock lock;
    _Atomic _Bool failed;
    RTestRwStressContext reader_contexts[R_TEST_RW_STRESS_READERS];
    RTestRwStressContext writer_contexts[R_TEST_RW_STRESS_WRITERS];
    pthread_t readers[R_TEST_RW_STRESS_READERS];
    pthread_t writers[R_TEST_RW_STRESS_WRITERS];
    RStdSyncReadLockResult final_result;
    size_t index;
    int staged = 0;

    atomic_init(&failed, 0);
    r_std_sync_rwlock_new(&lock.runtime, &lock.value, r_test_int_type(), &staged);
    for (index = 0U; index < R_TEST_RW_STRESS_READERS; ++index) {
        reader_contexts[index].lock = &lock.runtime;
        reader_contexts[index].iterations = R_TEST_RW_STRESS_ITERATIONS;
        reader_contexts[index].failed = &failed;
        R_TEST_CHECK(pthread_create(
                         &readers[index], NULL, r_test_rw_stress_reader, &reader_contexts[index]) ==
                     0);
    }
    for (index = 0U; index < R_TEST_RW_STRESS_WRITERS; ++index) {
        writer_contexts[index].lock = &lock.runtime;
        writer_contexts[index].iterations = R_TEST_RW_STRESS_ITERATIONS;
        writer_contexts[index].failed = &failed;
        R_TEST_CHECK(pthread_create(
                         &writers[index], NULL, r_test_rw_stress_writer, &writer_contexts[index]) ==
                     0);
    }
    for (index = 0U; index < R_TEST_RW_STRESS_READERS; ++index) {
        R_TEST_CHECK(pthread_join(readers[index], NULL) == 0);
    }
    for (index = 0U; index < R_TEST_RW_STRESS_WRITERS; ++index) {
        R_TEST_CHECK(pthread_join(writers[index], NULL) == 0);
    }
    R_TEST_CHECK(!atomic_load_explicit(&failed, memory_order_relaxed));
    r_std_sync_read(&final_result, &lock.runtime);
    R_TEST_CHECK(final_result.kind == R_STD_SYNC_READ_LOCK_RESULT_LOCKED);
    R_TEST_CHECK(*(const int *)r_std_sync_rw_read_guard_ref(&final_result.guard) ==
                 R_TEST_RW_STRESS_WRITERS * R_TEST_RW_STRESS_ITERATIONS);
    r_std_sync_unlock(&final_result.guard);
    r_std_sync_rw_lock_destroy(&lock.runtime);
    return 0;
}

static int r_test_unbounded_channel(void) {
    RRuntimeAllocator allocator;
    RStdSyncChannelCreateResult created;
    RStdSyncSender first_sender;
    RStdSyncSender second_sender;
    RStdSyncReceiver receiver;
    RStdSyncTryRecvResult tried_receive;
    RStdSyncSendResult sent;
    RTestChannelValue first = {11, 1};
    RTestChannelValue second = {22, 1};
    RTestChannelValue failed = {33, 1};
    RTestChannelValue disconnected = {44, 1};
    RTestChannelValue received = {0, 0};

    r_runtime_allocator_initialize(&allocator);
    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    created = r_std_sync_channel(&allocator, r_test_channel_value_type());
    R_TEST_CHECK(created.status == R_STD_SYNC_CHANNEL_CALL_ALLOCATION_ERROR);
    R_TEST_CHECK(created.error == R_STD_ALLOC_ERROR_OUT_OF_MEMORY);
    R_TEST_CHECK(created.value.state == NULL);

    r_runtime_allocator_set_failure(&allocator, UINT64_C(0));
    created = r_std_sync_channel(&allocator, r_test_channel_value_type());
    R_TEST_CHECK(created.status == R_STD_SYNC_CHANNEL_CALL_SUCCESS);
    first_sender = r_std_sync_sender(&created.value);
    second_sender = r_std_sync_clone_sender(&first_sender);
    receiver = r_std_sync_receiver(&created.value);
    R_TEST_CHECK(created.value.state == NULL);

    atomic_store_explicit(&r_test_channel_moves, 0U, memory_order_relaxed);
    sent = r_std_sync_send(&first_sender, &first);
    R_TEST_CHECK(sent.kind == R_STD_SYNC_SEND_RESULT_SENT);
    R_TEST_CHECK(!first.active);
    tried_receive = r_std_sync_try_recv(&receiver, &received);
    R_TEST_CHECK(tried_receive.kind == R_STD_SYNC_TRY_RECV_RESULT_RECEIVED);
    R_TEST_CHECK(received.active && (received.value == 11));
    R_TEST_CHECK(atomic_load_explicit(&r_test_channel_moves, memory_order_relaxed) == 2U);
    r_test_channel_value_drop(&received);

    tried_receive = r_std_sync_try_recv(&receiver, &received);
    R_TEST_CHECK(tried_receive.kind == R_STD_SYNC_TRY_RECV_RESULT_EMPTY);
    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    sent = r_std_sync_send(&second_sender, &failed);
    R_TEST_CHECK(sent.kind == R_STD_SYNC_SEND_RESULT_ALLOCATION_FAILED);
    R_TEST_CHECK(failed.active && (failed.value == 33));
    r_runtime_allocator_set_failure(&allocator, UINT64_C(0));

    sent = r_std_sync_send(&first_sender, &second);
    R_TEST_CHECK(sent.kind == R_STD_SYNC_SEND_RESULT_SENT);
    R_TEST_CHECK(!second.active);
    r_std_sync_receiver_destroy(&receiver);
    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    sent = r_std_sync_send(&second_sender, &disconnected);
    R_TEST_CHECK(sent.kind == R_STD_SYNC_SEND_RESULT_DISCONNECTED);
    R_TEST_CHECK(disconnected.active && (disconnected.value == 44));
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(0));

    r_test_channel_value_drop(&failed);
    r_test_channel_value_drop(&disconnected);
    r_std_sync_sender_destroy(&first_sender);
    r_std_sync_sender_destroy(&second_sender);
    return 0;
}

static int r_test_channel_factory_fifo_cleanup(void) {
    RRuntimeAllocator allocator;
    RStdSyncChannelCreateResult created;
    RStdSyncSender sender;
    RTestChannelValue first = {101, 1};
    RTestChannelValue second = {202, 1};

    r_runtime_allocator_initialize(&allocator);
    created = r_std_sync_channel(&allocator, r_test_channel_value_type());
    R_TEST_CHECK(created.status == R_STD_SYNC_CHANNEL_CALL_SUCCESS);
    sender = r_std_sync_sender(&created.value);
    atomic_store_explicit(&r_test_channel_drops, 0U, memory_order_relaxed);
    R_TEST_CHECK(r_std_sync_send(&sender, &first).kind == R_STD_SYNC_SEND_RESULT_SENT);
    R_TEST_CHECK(r_std_sync_send(&sender, &second).kind == R_STD_SYNC_SEND_RESULT_SENT);
    r_std_sync_channel_destroy(&created.value);
    R_TEST_CHECK(atomic_load_explicit(&r_test_channel_drops, memory_order_relaxed) == 2U);
    R_TEST_CHECK((r_test_channel_drop_order[0] == 101) && (r_test_channel_drop_order[1] == 202));
    r_std_sync_sender_destroy(&sender);
    return 0;
}

static int r_test_bounded_channel(void) {
    RRuntimeAllocator allocator;
    RStdSyncSyncChannelCreateResult oversized;
    RStdSyncSyncChannelCreateResult created;
    RStdSyncSyncSender sender;
    RStdSyncReceiver receiver;
    RStdSyncTrySendResult tried_send;
    RStdSyncTryRecvResult tried_receive;
    RStdSyncRecvResult received_result;
    RTestChannelValue first = {7, 1};
    RTestChannelValue second = {8, 1};
    RTestChannelValue received = {0, 0};
    uint64_t allocation_attempts;

    r_runtime_allocator_initialize(&allocator);
    oversized = r_std_sync_sync_channel(&allocator, r_test_channel_value_type(), SIZE_MAX);
    R_TEST_CHECK(oversized.status == R_STD_SYNC_CHANNEL_CALL_ALLOCATION_ERROR);
    R_TEST_CHECK(oversized.error == R_STD_ALLOC_ERROR_SIZE_OVERFLOW);
    R_TEST_CHECK(oversized.value.state == NULL);

    created = r_std_sync_sync_channel(&allocator, r_test_channel_value_type(), 1U);
    R_TEST_CHECK(created.status == R_STD_SYNC_CHANNEL_CALL_SUCCESS);
    allocation_attempts = r_runtime_allocator_attempt_count(&allocator);
    sender = r_std_sync_sync_sender(&created.value);
    receiver = r_std_sync_sync_receiver(&created.value);

    tried_send = r_std_sync_try_send(&sender, &first);
    R_TEST_CHECK(tried_send.kind == R_STD_SYNC_TRY_SEND_RESULT_SENT);
    R_TEST_CHECK(!first.active);
    tried_send = r_std_sync_try_send(&sender, &second);
    R_TEST_CHECK(tried_send.kind == R_STD_SYNC_TRY_SEND_RESULT_FULL);
    R_TEST_CHECK(second.active);
    received_result = r_std_sync_recv(&receiver, &received);
    R_TEST_CHECK(received_result.kind == R_STD_SYNC_RECV_RESULT_RECEIVED);
    R_TEST_CHECK(received.active && (received.value == 7));
    r_test_channel_value_drop(&received);
    R_TEST_CHECK(r_std_sync_sync_send(&sender, &second).kind == R_STD_SYNC_SEND_RESULT_SENT);
    R_TEST_CHECK(!second.active);
    received_result = r_std_sync_recv(&receiver, &received);
    R_TEST_CHECK(received_result.kind == R_STD_SYNC_RECV_RESULT_RECEIVED);
    R_TEST_CHECK(received.active && (received.value == 8));
    r_test_channel_value_drop(&received);
    tried_receive = r_std_sync_try_recv(&receiver, &received);
    R_TEST_CHECK(tried_receive.kind == R_STD_SYNC_TRY_RECV_RESULT_EMPTY);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == allocation_attempts);

    r_std_sync_sync_sender_destroy(&sender);
    received_result = r_std_sync_recv(&receiver, &received);
    R_TEST_CHECK(received_result.kind == R_STD_SYNC_RECV_RESULT_DISCONNECTED);
    r_std_sync_receiver_destroy(&receiver);
    return 0;
}

static int r_test_bounded_factory_fifo_cleanup(void) {
    RRuntimeAllocator allocator;
    RStdSyncSyncChannelCreateResult created;
    RStdSyncSyncSender sender;
    RTestChannelValue first = {301, 1};
    RTestChannelValue second = {302, 1};
    RTestChannelValue third = {303, 1};

    r_runtime_allocator_initialize(&allocator);
    created = r_std_sync_sync_channel(&allocator, r_test_channel_value_type(), 3U);
    R_TEST_CHECK(created.status == R_STD_SYNC_CHANNEL_CALL_SUCCESS);
    sender = r_std_sync_sync_sender(&created.value);
    R_TEST_CHECK(r_std_sync_sync_send(&sender, &first).kind == R_STD_SYNC_SEND_RESULT_SENT);
    R_TEST_CHECK(r_std_sync_sync_send(&sender, &second).kind == R_STD_SYNC_SEND_RESULT_SENT);
    R_TEST_CHECK(r_std_sync_sync_send(&sender, &third).kind == R_STD_SYNC_SEND_RESULT_SENT);
    atomic_store_explicit(&r_test_channel_drops, 0U, memory_order_relaxed);
    r_std_sync_sync_channel_destroy(&created.value);
    R_TEST_CHECK(atomic_load_explicit(&r_test_channel_drops, memory_order_relaxed) == 3U);
    R_TEST_CHECK((r_test_channel_drop_order[0] == 301) && (r_test_channel_drop_order[1] == 302) &&
                 (r_test_channel_drop_order[2] == 303));
    r_std_sync_sync_sender_destroy(&sender);
    return 0;
}

static int r_test_bounded_blocking_and_rendezvous(void) {
    RRuntimeAllocator allocator;
    RStdSyncSyncChannelCreateResult created;
    RStdSyncSyncSender sender;
    RStdSyncReceiver receiver;
    RTestChannelValue initial = {1, 1};
    RTestChannelValue received = {0, 0};
    RTestChannelSendContext send_context;
    pthread_t thread;
    size_t spin;

    r_runtime_allocator_initialize(&allocator);
    created = r_std_sync_sync_channel(&allocator, r_test_channel_value_type(), 1U);
    R_TEST_CHECK(created.status == R_STD_SYNC_CHANNEL_CALL_SUCCESS);
    sender = r_std_sync_sync_sender(&created.value);
    receiver = r_std_sync_sync_receiver(&created.value);
    R_TEST_CHECK(r_std_sync_sync_send(&sender, &initial).kind == R_STD_SYNC_SEND_RESULT_SENT);

    send_context.sender = &sender;
    send_context.value = (RTestChannelValue){2, 1};
    send_context.result = (RStdSyncSendResult){R_STD_SYNC_SEND_RESULT_DISCONNECTED};
    atomic_init(&send_context.ready, 0);
    atomic_init(&send_context.finished, 0);
    R_TEST_CHECK(pthread_create(&thread, NULL, r_test_channel_send_worker, &send_context) == 0);
    r_test_wait(&send_context.ready);
    for (spin = 0U; spin < 1000U; spin += 1U) {
        (void)sched_yield();
    }
    R_TEST_CHECK(!atomic_load_explicit(&send_context.finished, memory_order_acquire));
    R_TEST_CHECK(r_std_sync_recv(&receiver, &received).kind == R_STD_SYNC_RECV_RESULT_RECEIVED);
    R_TEST_CHECK(received.value == 1);
    r_test_channel_value_drop(&received);
    R_TEST_CHECK(pthread_join(thread, NULL) == 0);
    R_TEST_CHECK(send_context.result.kind == R_STD_SYNC_SEND_RESULT_SENT);
    R_TEST_CHECK(!send_context.value.active);
    R_TEST_CHECK(r_std_sync_recv(&receiver, &received).kind == R_STD_SYNC_RECV_RESULT_RECEIVED);
    R_TEST_CHECK(received.value == 2);
    r_test_channel_value_drop(&received);
    r_std_sync_sync_sender_destroy(&sender);
    r_std_sync_receiver_destroy(&receiver);

    created = r_std_sync_sync_channel(&allocator, r_test_channel_value_type(), 0U);
    R_TEST_CHECK(created.status == R_STD_SYNC_CHANNEL_CALL_SUCCESS);
    sender = r_std_sync_sync_sender(&created.value);
    receiver = r_std_sync_sync_receiver(&created.value);
    send_context.sender = &sender;
    send_context.value = (RTestChannelValue){3, 1};
    send_context.result = (RStdSyncSendResult){R_STD_SYNC_SEND_RESULT_DISCONNECTED};
    atomic_init(&send_context.ready, 0);
    atomic_init(&send_context.finished, 0);
    R_TEST_CHECK(pthread_create(&thread, NULL, r_test_channel_send_worker, &send_context) == 0);
    r_test_wait(&send_context.ready);
    for (spin = 0U; spin < 100000U; spin += 1U) {
        RStdSyncTryRecvResult result = r_std_sync_try_recv(&receiver, &received);

        if (result.kind == R_STD_SYNC_TRY_RECV_RESULT_RECEIVED) {
            break;
        }
        R_TEST_CHECK(result.kind == R_STD_SYNC_TRY_RECV_RESULT_EMPTY);
        (void)sched_yield();
    }
    R_TEST_CHECK(spin != 100000U);
    R_TEST_CHECK(received.active && (received.value == 3));
    r_test_channel_value_drop(&received);
    R_TEST_CHECK(pthread_join(thread, NULL) == 0);
    R_TEST_CHECK(send_context.result.kind == R_STD_SYNC_SEND_RESULT_SENT);
    r_std_sync_sync_sender_destroy(&sender);
    r_std_sync_receiver_destroy(&receiver);
    return 0;
}

static int r_test_rendezvous_waiting_receiver_and_disconnect(void) {
    RRuntimeAllocator allocator;
    RStdSyncSyncChannelCreateResult created;
    RStdSyncSyncSender sender;
    RTestChannelRecvContext receive_context;
    RTestChannelSendContext send_context;
    RTestChannelValue value = {77, 1};
    RStdSyncTrySendResult tried_send = {R_STD_SYNC_TRY_SEND_RESULT_FULL};
    pthread_t thread;
    size_t spin;

    r_runtime_allocator_initialize(&allocator);
    created = r_std_sync_sync_channel(&allocator, r_test_channel_value_type(), 0U);
    R_TEST_CHECK(created.status == R_STD_SYNC_CHANNEL_CALL_SUCCESS);
    sender = r_std_sync_sync_sender(&created.value);
    receive_context.receiver = r_std_sync_sync_receiver(&created.value);
    receive_context.value = (RTestChannelValue){0, 0};
    receive_context.result = (RStdSyncRecvResult){R_STD_SYNC_RECV_RESULT_DISCONNECTED};
    atomic_init(&receive_context.ready, 0);
    R_TEST_CHECK(pthread_create(&thread, NULL, r_test_channel_recv_worker, &receive_context) == 0);
    r_test_wait(&receive_context.ready);
    for (spin = 0U; spin < 100000U; spin += 1U) {
        tried_send = r_std_sync_try_send(&sender, &value);
        if (tried_send.kind == R_STD_SYNC_TRY_SEND_RESULT_SENT) {
            break;
        }
        R_TEST_CHECK(tried_send.kind == R_STD_SYNC_TRY_SEND_RESULT_FULL);
        (void)sched_yield();
    }
    R_TEST_CHECK(spin != 100000U);
    R_TEST_CHECK(!value.active);
    R_TEST_CHECK(pthread_join(thread, NULL) == 0);
    R_TEST_CHECK(receive_context.result.kind == R_STD_SYNC_RECV_RESULT_RECEIVED);
    R_TEST_CHECK(receive_context.value.active && (receive_context.value.value == 77));
    r_test_channel_value_drop(&receive_context.value);
    r_std_sync_sync_sender_destroy(&sender);
    r_std_sync_receiver_destroy(&receive_context.receiver);

    created = r_std_sync_sync_channel(&allocator, r_test_channel_value_type(), 0U);
    R_TEST_CHECK(created.status == R_STD_SYNC_CHANNEL_CALL_SUCCESS);
    sender = r_std_sync_sync_sender(&created.value);
    send_context.sender = &sender;
    send_context.value = (RTestChannelValue){88, 1};
    send_context.result = (RStdSyncSendResult){R_STD_SYNC_SEND_RESULT_SENT};
    atomic_init(&send_context.ready, 0);
    atomic_init(&send_context.finished, 0);
    R_TEST_CHECK(pthread_create(&thread, NULL, r_test_channel_send_worker, &send_context) == 0);
    r_test_wait(&send_context.ready);
    r_std_sync_sync_channel_destroy(&created.value);
    R_TEST_CHECK(pthread_join(thread, NULL) == 0);
    R_TEST_CHECK(send_context.result.kind == R_STD_SYNC_SEND_RESULT_DISCONNECTED);
    R_TEST_CHECK(send_context.value.active && (send_context.value.value == 88));
    r_test_channel_value_drop(&send_context.value);
    r_std_sync_sync_sender_destroy(&sender);
    return 0;
}

int main(void) {
    if (r_test_lifecycle_and_move() != 0) {
        return 1;
    }
    if (r_test_contention_and_publication() != 0) {
        return 1;
    }
    if (r_test_poison_and_recovery() != 0) {
        return 1;
    }
    if (r_test_barrier() != 0) {
        return 1;
    }
    if (r_test_condvar() != 0) {
        return 1;
    }
    if (r_test_once() != 0) {
        return 1;
    }
    if (r_test_once_lock() != 0) {
        return 1;
    }
    if (r_test_rw_lifecycle_move_and_same_thread() != 0) {
        return 1;
    }
    if (r_test_rw_multiple_readers_and_writer_exclusion() != 0) {
        return 1;
    }
    if (r_test_rw_writer_publication() != 0) {
        return 1;
    }
    if (r_test_rw_poison() != 0) {
        return 1;
    }
    if (r_test_rw_stress() != 0) {
        return 1;
    }
    if (r_test_unbounded_channel() != 0) {
        return 1;
    }
    if (r_test_channel_factory_fifo_cleanup() != 0) {
        return 1;
    }
    if (r_test_bounded_channel() != 0) {
        return 1;
    }
    if (r_test_bounded_factory_fifo_cleanup() != 0) {
        return 1;
    }
    if (r_test_bounded_blocking_and_rendezvous() != 0) {
        return 1;
    }
    if (r_test_rendezvous_waiting_receiver_and_disconnect() != 0) {
        return 1;
    }
    return 0;
}

#include "r_library_sync_internal.h"

#include <sched.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    R_LIBRARY_SYNC_MUTEX_LOCKED = 1U,
    R_LIBRARY_SYNC_MUTEX_POISONED = 2U
};

static void guard_clear(RStdSyncMutexGuard *guard) {
    guard->header.kind = R_STD_SYNC_GUARD_NONE;
    guard->header.active = 0;
    guard->mutex = NULL;
    guard->owner_token = (uintptr_t)0U;
}

void r_library_internal_sync_mutex_initialize(RStdSyncMutex *result,
                                              void *value_storage,
                                              RRuntimeTypeInfo value_type,
                                              void *staged_value) {

    atomic_init(&result->state, 0U);
    atomic_init(&result->owner_token, (uintptr_t)0U);
    result->value_type = value_type;
    result->value = value_storage;
    result->initialized = 1;
    r_library_internal_sync_move_initialize(value_type, value_storage, staged_value);
}

RLibrarySyncAcquireResult r_library_internal_sync_mutex_acquire(const RStdSyncMutex *source,
                                                                _Bool blocking) {
    RLibrarySyncAcquireResult result = {0};
    RStdSyncMutex *mutex = (RStdSyncMutex *)source;
    const uintptr_t token = r_library_internal_sync_current_thread_token();

    guard_clear(&result.guard);

    for (;;) {
        unsigned int state = atomic_load_explicit(&mutex->state, memory_order_acquire);
        if ((state & R_LIBRARY_SYNC_MUTEX_LOCKED) != 0U) {
            if (atomic_load_explicit(&mutex->owner_token, memory_order_acquire) == token) {
                result.kind = R_LIBRARY_SYNC_ACQUIRE_WOULD_DEADLOCK;
                return result;
            }
            if (!blocking) {
                result.kind = R_LIBRARY_SYNC_ACQUIRE_WOULD_BLOCK;
                return result;
            }
            (void)sched_yield();
            continue;
        }

        {
            unsigned int expected = state;
            const unsigned int desired = state | R_LIBRARY_SYNC_MUTEX_LOCKED;
            if (!atomic_compare_exchange_weak_explicit(&mutex->state,
                                                       &expected,
                                                       desired,
                                                       memory_order_acquire,
                                                       memory_order_relaxed)) {
                continue;
            }
        }

        atomic_store_explicit(&mutex->owner_token, token, memory_order_release);
        result.guard.header.kind = R_STD_SYNC_GUARD_MUTEX;
        result.guard.header.active = 1;
        result.guard.mutex = mutex;
        result.guard.owner_token = token;
        result.kind = (state & R_LIBRARY_SYNC_MUTEX_POISONED) != 0U
                          ? R_LIBRARY_SYNC_ACQUIRE_POISONED
                          : R_LIBRARY_SYNC_ACQUIRE_LOCKED;
        return result;
    }
}

void r_library_internal_sync_mutex_unlock(RStdSyncMutexGuard *guard, _Bool panicking) {
    RStdSyncMutex *mutex = guard->mutex;
    unsigned int state = atomic_load_explicit(&mutex->state, memory_order_relaxed);

    if (panicking) {
        state |= R_LIBRARY_SYNC_MUTEX_POISONED;
    }
    state &= ~(unsigned int)R_LIBRARY_SYNC_MUTEX_LOCKED;
    guard_clear(guard);
    atomic_store_explicit(&mutex->owner_token, (uintptr_t)0U, memory_order_relaxed);
    atomic_store_explicit(&mutex->state, state, memory_order_release);
}

const void *r_library_internal_sync_mutex_guard_ref(const RStdSyncMutexGuard *guard) {
    return guard->mutex->value;
}

void *r_library_internal_sync_mutex_guard_mut(RStdSyncMutexGuard *guard) {
    return guard->mutex->value;
}

void r_library_internal_sync_mutex_move(RStdSyncMutex *destination,
                                        void *destination_storage,
                                        RStdSyncMutex *source) {
    unsigned int state;

    state = atomic_load_explicit(&source->state, memory_order_acquire);

    atomic_init(&destination->state, state & R_LIBRARY_SYNC_MUTEX_POISONED);
    atomic_init(&destination->owner_token, (uintptr_t)0U);
    destination->value_type = source->value_type;
    destination->value = destination_storage;
    destination->initialized = 1;
    r_library_internal_sync_move_initialize(source->value_type, destination_storage, source->value);

    source->initialized = 0;
    source->value = NULL;
}

void r_library_internal_sync_mutex_destroy(RStdSyncMutex *mutex) {
    RRuntimeTypeInfo value_type;
    void *value;

    if (!mutex->initialized) {
        return;
    }

    value_type = mutex->value_type;
    value = mutex->value;
    mutex->initialized = 0;
    mutex->value = NULL;
    if ((value_type.size != 0U) && (value_type.drop != NULL)) {
        value_type.drop(value);
    }
}

void r_library_internal_sync_mutex_guard_move(RStdSyncMutexGuard *destination,
                                              RStdSyncMutexGuard *source) {
    *destination = *source;
    guard_clear(source);
}

void r_library_internal_sync_mutex_guard_destroy(RStdSyncMutexGuard *guard, _Bool panicking) {
    if (!guard->header.active) {
        return;
    }
    r_library_internal_sync_mutex_unlock(guard, panicking);
}

#include "r_library_sync_internal.h"

#include <sched.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

enum {
    R_LIBRARY_SYNC_ONCE_LOCK_EMPTY = 0U,
    R_LIBRARY_SYNC_ONCE_LOCK_RUNNING = 1U,
    R_LIBRARY_SYNC_ONCE_LOCK_INITIALIZED = 2U
};

static void wait_for_initialization(RStdSyncOnceLock *lock, uintptr_t token) {
    for (;;) {
        unsigned int state = atomic_load_explicit(&lock->state, memory_order_acquire);

        if (state != R_LIBRARY_SYNC_ONCE_LOCK_RUNNING) {
            return;
        }
        if (atomic_load_explicit(&lock->owner_token, memory_order_acquire) == token) {
            r_library_internal_sync_contract_violation();
        }
        (void)sched_yield();
    }
}

static _Bool begin_initialization(RStdSyncOnceLock *lock, uintptr_t token) {
    unsigned int expected = R_LIBRARY_SYNC_ONCE_LOCK_EMPTY;

    if (!atomic_compare_exchange_strong_explicit(&lock->state,
                                                 &expected,
                                                 R_LIBRARY_SYNC_ONCE_LOCK_RUNNING,
                                                 memory_order_acq_rel,
                                                 memory_order_acquire)) {
        return 0;
    }
    atomic_store_explicit(&lock->owner_token, token, memory_order_release);
    return 1;
}

static void finish_initialization(RStdSyncOnceLock *lock) {
    lock->value_initialized = 1;
    atomic_store_explicit(&lock->owner_token, (uintptr_t)0U, memory_order_relaxed);
    atomic_store_explicit(&lock->state, R_LIBRARY_SYNC_ONCE_LOCK_INITIALIZED, memory_order_release);
}

static void abandon_initialization(RStdSyncOnceLock *lock) {
    atomic_store_explicit(&lock->owner_token, (uintptr_t)0U, memory_order_relaxed);
    atomic_store_explicit(&lock->state, R_LIBRARY_SYNC_ONCE_LOCK_EMPTY, memory_order_release);
}

void r_library_internal_sync_once_lock_initialize(RStdSyncOnceLock *result,
                                                  void *value_storage,
                                                  RRuntimeTypeInfo value_type) {
    atomic_init(&result->state, R_LIBRARY_SYNC_ONCE_LOCK_EMPTY);
    atomic_init(&result->owner_token, (uintptr_t)0U);
    result->value_type = value_type;
    result->value = value_storage;
    result->initialized = 1;
    result->value_initialized = 0;
}

const void *r_library_internal_sync_once_lock_get(const RStdSyncOnceLock *source) {
    RStdSyncOnceLock *lock = (RStdSyncOnceLock *)source;

    if (atomic_load_explicit(&lock->state, memory_order_acquire) !=
        R_LIBRARY_SYNC_ONCE_LOCK_INITIALIZED) {
        return NULL;
    }
    return lock->value;
}

const void *r_library_internal_sync_once_lock_get_or_init(const RStdSyncOnceLock *source,
                                                          RStdSyncOnceLockInitializer initializer,
                                                          void *context) {
    RStdSyncOnceLock *lock = (RStdSyncOnceLock *)source;
    const uintptr_t token = r_library_internal_sync_current_thread_token();

    for (;;) {
        const void *value = r_library_internal_sync_once_lock_get(lock);

        if (value != NULL) {
            return value;
        }
        if (begin_initialization(lock, token)) {
            if (!initializer(lock->value, context)) {
                abandon_initialization(lock);
                return NULL;
            }
            finish_initialization(lock);
            return lock->value;
        }
        wait_for_initialization(lock, token);
    }
}

RStdSyncSetResult r_library_internal_sync_once_lock_set(const RStdSyncOnceLock *source,
                                                        void *staged_value) {
    RStdSyncOnceLock *lock = (RStdSyncOnceLock *)source;
    const uintptr_t token = r_library_internal_sync_current_thread_token();

    for (;;) {
        unsigned int state = atomic_load_explicit(&lock->state, memory_order_acquire);

        if (state == R_LIBRARY_SYNC_ONCE_LOCK_INITIALIZED) {
            return R_STD_SYNC_SET_RESULT_OCCUPIED;
        }
        if (state == R_LIBRARY_SYNC_ONCE_LOCK_RUNNING) {
            wait_for_initialization(lock, token);
            continue;
        }
        if (begin_initialization(lock, token)) {
            r_library_internal_sync_move_initialize(lock->value_type, lock->value, staged_value);
            finish_initialization(lock);
            return R_STD_SYNC_SET_RESULT_STORED;
        }
    }
}

void r_library_internal_sync_once_lock_move(RStdSyncOnceLock *destination,
                                            void *destination_storage,
                                            RStdSyncOnceLock *source) {
    unsigned int state;

    state = atomic_load_explicit(&source->state, memory_order_acquire);
    atomic_init(&destination->state, state);
    atomic_init(&destination->owner_token, (uintptr_t)0U);
    destination->value_type = source->value_type;
    destination->value = destination_storage;
    destination->initialized = 1;
    destination->value_initialized = source->value_initialized;
    if (source->value_initialized) {
        r_library_internal_sync_move_initialize(
            source->value_type, destination_storage, source->value);
        source->value_initialized = 0;
    }
    source->initialized = 0;
    source->value = NULL;
}

void r_library_internal_sync_once_lock_destroy(RStdSyncOnceLock *lock) {
    RRuntimeTypeInfo value_type;
    void *value;
    _Bool value_initialized;

    if (!lock->initialized) {
        return;
    }
    value_type = lock->value_type;
    value = lock->value;
    value_initialized = lock->value_initialized;
    lock->initialized = 0;
    lock->value_initialized = 0;
    lock->value = NULL;
    if (value_initialized && (value_type.size != 0U) && (value_type.drop != NULL)) {
        value_type.drop(value);
    }
}

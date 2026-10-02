#include "r_library_sync_internal.h"

#include <sched.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

enum {
    R_LIBRARY_SYNC_RW_METADATA_LOCKED = 1U,
    R_LIBRARY_SYNC_RW_WRITER_ACTIVE = 2U,
    R_LIBRARY_SYNC_RW_POISONED = 4U
};

static void read_guard_clear(RStdSyncRwReadGuard *guard) {
    guard->header.kind = R_STD_SYNC_GUARD_NONE;
    guard->header.active = 0;
    guard->lock = NULL;
    guard->owner_token = (uintptr_t)0U;
    guard->previous = NULL;
    guard->next = NULL;
}

static void write_guard_clear(RStdSyncRwWriteGuard *guard) {
    guard->header.kind = R_STD_SYNC_GUARD_NONE;
    guard->header.active = 0;
    guard->lock = NULL;
    guard->owner_token = (uintptr_t)0U;
}

static unsigned int metadata_lock(RStdSyncRwLock *lock) {
    for (;;) {
        unsigned int state = atomic_load_explicit(&lock->state, memory_order_relaxed);
        unsigned int expected;

        if ((state & R_LIBRARY_SYNC_RW_METADATA_LOCKED) != 0U) {
            (void)sched_yield();
            continue;
        }
        expected = state;
        if (atomic_compare_exchange_weak_explicit(&lock->state,
                                                  &expected,
                                                  state | R_LIBRARY_SYNC_RW_METADATA_LOCKED,
                                                  memory_order_acquire,
                                                  memory_order_relaxed)) {
            return state | R_LIBRARY_SYNC_RW_METADATA_LOCKED;
        }
    }
}

static void metadata_unlock(RStdSyncRwLock *lock, unsigned int state) {
    atomic_store_explicit(&lock->state,
                          state & ~(unsigned int)R_LIBRARY_SYNC_RW_METADATA_LOCKED,
                          memory_order_release);
}

static _Bool thread_holds_guard(const RStdSyncRwLock *lock, unsigned int state, uintptr_t token) {
    const RStdSyncRwReadGuard *reader;

    if (((state & R_LIBRARY_SYNC_RW_WRITER_ACTIVE) != 0U) && (lock->writer_token == token)) {
        return 1;
    }
    for (reader = lock->reader_head; reader != NULL; reader = reader->next) {
        if (reader->owner_token == token) {
            return 1;
        }
    }
    return 0;
}

static RStdSyncRwLock *read_guard_lock(RStdSyncRwReadGuard *guard, unsigned int *locked_state) {
    RStdSyncRwLock *lock;

    lock = guard->lock;
    *locked_state = metadata_lock(lock);
    return lock;
}

static RStdSyncRwLock *write_guard_lock(RStdSyncRwWriteGuard *guard, unsigned int *locked_state) {
    RStdSyncRwLock *lock;

    lock = guard->lock;
    *locked_state = metadata_lock(lock);
    return lock;
}

void r_library_internal_sync_rw_lock_initialize(RStdSyncRwLock *result,
                                                void *value_storage,
                                                RRuntimeTypeInfo value_type,
                                                void *staged_value) {

    atomic_init(&result->state, 0U);
    result->writer_token = (uintptr_t)0U;
    result->reader_head = NULL;
    result->reader_count = 0U;
    result->value_type = value_type;
    result->value = value_storage;
    result->initialized = 1;
    r_library_internal_sync_move_initialize(value_type, value_storage, staged_value);
}

RLibrarySyncAcquireKind r_library_internal_sync_rw_read_acquire(const RStdSyncRwLock *source,
                                                                _Bool blocking,
                                                                RStdSyncRwReadGuard *guard) {
    RStdSyncRwLock *lock = (RStdSyncRwLock *)source;
    const uintptr_t token = r_library_internal_sync_current_thread_token();

    read_guard_clear(guard);

    for (;;) {
        unsigned int state = metadata_lock(lock);

        if (thread_holds_guard(lock, state, token)) {
            metadata_unlock(lock, state);
            return R_LIBRARY_SYNC_ACQUIRE_WOULD_DEADLOCK;
        }
        if ((state & R_LIBRARY_SYNC_RW_WRITER_ACTIVE) == 0U) {
            if (lock->reader_count == SIZE_MAX) {
                metadata_unlock(lock, state);
                r_library_internal_sync_contract_violation();
            }
            guard->header.kind = R_STD_SYNC_GUARD_RW_READ;
            guard->header.active = 1;
            guard->lock = lock;
            guard->owner_token = token;
            guard->previous = NULL;
            guard->next = lock->reader_head;
            if (lock->reader_head != NULL) {
                lock->reader_head->previous = guard;
            }
            lock->reader_head = guard;
            lock->reader_count += 1U;
            metadata_unlock(lock, state);
            return (state & R_LIBRARY_SYNC_RW_POISONED) != 0U ? R_LIBRARY_SYNC_ACQUIRE_POISONED
                                                              : R_LIBRARY_SYNC_ACQUIRE_LOCKED;
        }
        metadata_unlock(lock, state);
        if (!blocking) {
            return R_LIBRARY_SYNC_ACQUIRE_WOULD_BLOCK;
        }
        (void)sched_yield();
    }
}

RLibrarySyncAcquireKind r_library_internal_sync_rw_write_acquire(const RStdSyncRwLock *source,
                                                                 _Bool blocking,
                                                                 RStdSyncRwWriteGuard *guard) {
    RStdSyncRwLock *lock = (RStdSyncRwLock *)source;
    const uintptr_t token = r_library_internal_sync_current_thread_token();

    write_guard_clear(guard);

    for (;;) {
        unsigned int state = metadata_lock(lock);

        if (thread_holds_guard(lock, state, token)) {
            metadata_unlock(lock, state);
            return R_LIBRARY_SYNC_ACQUIRE_WOULD_DEADLOCK;
        }
        if (((state & R_LIBRARY_SYNC_RW_WRITER_ACTIVE) == 0U) && (lock->reader_count == 0U)) {
            state |= R_LIBRARY_SYNC_RW_WRITER_ACTIVE;
            lock->writer_token = token;
            guard->header.kind = R_STD_SYNC_GUARD_RW_WRITE;
            guard->header.active = 1;
            guard->lock = lock;
            guard->owner_token = token;
            metadata_unlock(lock, state);
            return (state & R_LIBRARY_SYNC_RW_POISONED) != 0U ? R_LIBRARY_SYNC_ACQUIRE_POISONED
                                                              : R_LIBRARY_SYNC_ACQUIRE_LOCKED;
        }
        metadata_unlock(lock, state);
        if (!blocking) {
            return R_LIBRARY_SYNC_ACQUIRE_WOULD_BLOCK;
        }
        (void)sched_yield();
    }
}

void r_library_internal_sync_rw_read_unlock(RStdSyncRwReadGuard *guard) {
    unsigned int state;
    RStdSyncRwLock *lock = read_guard_lock(guard, &state);

    if (guard->previous != NULL) {
        guard->previous->next = guard->next;
    } else {
        lock->reader_head = guard->next;
    }
    if (guard->next != NULL) {
        guard->next->previous = guard->previous;
    }
    lock->reader_count -= 1U;
    read_guard_clear(guard);
    metadata_unlock(lock, state);
}

void r_library_internal_sync_rw_write_unlock(RStdSyncRwWriteGuard *guard, _Bool panicking) {
    unsigned int state;
    RStdSyncRwLock *lock = write_guard_lock(guard, &state);

    if (panicking) {
        state |= R_LIBRARY_SYNC_RW_POISONED;
    }
    state &= ~(unsigned int)R_LIBRARY_SYNC_RW_WRITER_ACTIVE;
    lock->writer_token = (uintptr_t)0U;
    write_guard_clear(guard);
    metadata_unlock(lock, state);
}

const void *r_library_internal_sync_rw_read_guard_ref(const RStdSyncRwReadGuard *source) {
    return source->lock->value;
}

const void *r_library_internal_sync_rw_write_guard_ref(const RStdSyncRwWriteGuard *source) {
    return source->lock->value;
}

void *r_library_internal_sync_rw_write_guard_mut(RStdSyncRwWriteGuard *guard) {
    return guard->lock->value;
}

void r_library_internal_sync_rw_lock_move(RStdSyncRwLock *destination,
                                          void *destination_storage,
                                          RStdSyncRwLock *source) {
    unsigned int state;

    state = metadata_lock(source);

    atomic_init(&destination->state, state & R_LIBRARY_SYNC_RW_POISONED);
    destination->writer_token = (uintptr_t)0U;
    destination->reader_head = NULL;
    destination->reader_count = 0U;
    destination->value_type = source->value_type;
    destination->value = destination_storage;
    destination->initialized = 1;
    r_library_internal_sync_move_initialize(source->value_type, destination_storage, source->value);
    source->initialized = 0;
    source->value = NULL;
    metadata_unlock(source, state);
}

void r_library_internal_sync_rw_lock_destroy(RStdSyncRwLock *lock) {
    RRuntimeTypeInfo value_type;
    unsigned int state;
    void *value;

    if (!lock->initialized) {
        return;
    }
    state = metadata_lock(lock);

    value_type = lock->value_type;
    value = lock->value;
    lock->initialized = 0;
    lock->value = NULL;
    metadata_unlock(lock, state);
    if ((value_type.size != 0U) && (value_type.drop != NULL)) {
        value_type.drop(value);
    }
}

void r_library_internal_sync_rw_read_guard_move(RStdSyncRwReadGuard *destination,
                                                RStdSyncRwReadGuard *source) {
    unsigned int state;
    RStdSyncRwLock *lock;

    lock = read_guard_lock(source, &state);
    *destination = *source;
    if (destination->previous != NULL) {
        destination->previous->next = destination;
    } else {
        lock->reader_head = destination;
    }
    if (destination->next != NULL) {
        destination->next->previous = destination;
    }
    read_guard_clear(source);
    metadata_unlock(lock, state);
}

void r_library_internal_sync_rw_read_guard_destroy(RStdSyncRwReadGuard *guard, _Bool panicking) {
    (void)panicking;
    if (!guard->header.active) {
        return;
    }
    r_library_internal_sync_rw_read_unlock(guard);
}

void r_library_internal_sync_rw_write_guard_move(RStdSyncRwWriteGuard *destination,
                                                 RStdSyncRwWriteGuard *source) {
    unsigned int state;
    RStdSyncRwLock *lock;

    lock = write_guard_lock(source, &state);
    *destination = *source;
    write_guard_clear(source);
    metadata_unlock(lock, state);
}

void r_library_internal_sync_rw_write_guard_destroy(RStdSyncRwWriteGuard *guard, _Bool panicking) {
    if (!guard->header.active) {
        return;
    }
    r_library_internal_sync_rw_write_unlock(guard, panicking);
}

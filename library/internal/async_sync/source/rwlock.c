#include "r_library_async_sync_internal.h"

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * std.async::rw_lock<T> (R-SLIB-ASYNC-0013). Waiters are served in start order: a writer at the
 * head waits for every reader and writer to leave, and the readers at the head enter together
 * once no writer holds the lock. A reader that arrives while any waiter is linked queues behind
 * it, so writers are not starved. Every guard and started task holds one reference.
 */
enum {
    R_ASYNC_RWLOCK_READ = 0,
    R_ASYNC_RWLOCK_WRITE = 1
};

struct RLibraryAsyncRwLockState {
    pthread_mutex_t lock;
    RLibraryAsyncWaitList waiters;
    RRuntimeTypeInfo value_type;
    unsigned char *value;
    size_t allocation_alignment;
    size_t references;
    size_t readers;
    _Bool writer;
};

static void rw_lock(RLibraryAsyncRwLockState *state) {
    if (pthread_mutex_lock(&state->lock) != 0) {
        r_library_internal_async_contract_violation();
    }
}

static void rw_unlock(RLibraryAsyncRwLockState *state) {
    if (pthread_mutex_unlock(&state->lock) != 0) {
        r_library_internal_async_contract_violation();
    }
}

static void rw_retain_locked(RLibraryAsyncRwLockState *state) {
    if (state->references >= (SIZE_MAX / 2U)) {
        rw_unlock(state);
        r_library_internal_async_count_overflow();
    }
    state->references += 1U;
}

static void rw_retain(void *resource) {
    RLibraryAsyncRwLockState *state = resource;

    rw_lock(state);
    rw_retain_locked(state);
    rw_unlock(state);
}

void r_library_internal_async_rwlock_release(RLibraryAsyncRwLockState *state) {
    _Bool destroy;

    rw_lock(state);
    state->references -= 1U;
    destroy = state->references == 0U;
    rw_unlock(state);
    if (!destroy) {
        return;
    }
    if (state->value_type.drop != NULL) {
        state->value_type.drop(state->value);
    }
    if (pthread_mutex_destroy(&state->lock) != 0) {
        r_library_internal_async_contract_violation();
    }
    r_runtime_allocator_deallocate(state, state->allocation_alignment);
}

static void rw_release(void *resource) {
    r_library_internal_async_rwlock_release(resource);
}

static void write_guard(RLibraryAsyncWaiter *waiter, RLibraryAsyncRwLockState *state) {
    if (waiter->mode == R_ASYNC_RWLOCK_READ) {
        const RStdAsyncRwReadGuard guard = {state};

        (void)memcpy(waiter->result, &guard, sizeof(guard));
    } else {
        const RStdAsyncRwWriteGuard guard = {state};

        (void)memcpy(waiter->result, &guard, sizeof(guard));
    }
}

static _Bool compatible_locked(const RLibraryAsyncRwLockState *state, uint32_t mode) {
    return mode == R_ASYNC_RWLOCK_READ ? !state->writer : (!state->writer && state->readers == 0U);
}

static void enter_locked(RLibraryAsyncRwLockState *state, uint32_t mode) {
    if (mode == R_ASYNC_RWLOCK_READ) {
        state->readers += 1U;
    } else {
        state->writer = 1;
    }
    rw_retain_locked(state);
}

/* Under the lock: admits the waiters at the head that the current holders allow. */
static RLibraryAsyncWaiter *grant_locked(RLibraryAsyncRwLockState *state) {
    RLibraryAsyncFinished finished = {NULL, NULL};

    while (state->waiters.head != NULL) {
        RLibraryAsyncWaiter *waiter = state->waiters.head;

        if (!compatible_locked(state, waiter->mode)) {
            break;
        }
        r_library_internal_async_wait_remove(&state->waiters, waiter);
        if (!r_library_internal_async_waiter_select(waiter)) {
            continue;
        }
        enter_locked(state, waiter->mode);
        write_guard(waiter, state);
        r_library_internal_async_finished_append(&finished, waiter);
        if (waiter->mode == R_ASYNC_RWLOCK_WRITE) {
            break;
        }
    }
    return finished.head;
}

static RLibraryAsyncBegin rw_begin(void *resource, RLibraryAsyncWaiter *waiter) {
    RLibraryAsyncRwLockState *state = resource;

    rw_lock(state);
    if ((state->waiters.head == NULL) && compatible_locked(state, waiter->mode)) {
        if (!r_library_internal_async_waiter_select(waiter)) {
            rw_unlock(state);
            return R_LIBRARY_ASYNC_BEGIN_REJECTED;
        }
        enter_locked(state, waiter->mode);
        write_guard(waiter, state);
        rw_unlock(state);
        return R_LIBRARY_ASYNC_BEGIN_FINISHED;
    }
    r_library_internal_async_wait_push(&state->waiters, waiter);
    rw_unlock(state);
    return R_LIBRARY_ASYNC_BEGIN_PENDING;
}

/* A withdrawn waiter at the head may have been holding back compatible waiters behind it. */
static void rw_withdraw(void *resource, RLibraryAsyncWaiter *waiter) {
    RLibraryAsyncRwLockState *state = resource;
    RLibraryAsyncWaiter *finished = NULL;

    rw_lock(state);
    if (waiter->linked) {
        r_library_internal_async_wait_remove(&state->waiters, waiter);
        finished = grant_locked(state);
    }
    rw_unlock(state);
    r_library_internal_async_finish_all(finished);
}

static const RLibraryAsyncOperation rw_operation = {
    rw_begin,
    rw_withdraw,
    rw_retain,
    rw_release,
};

static void leave(RLibraryAsyncRwLockState *state, uint32_t mode) {
    RLibraryAsyncWaiter *finished;

    rw_lock(state);
    if (mode == R_ASYNC_RWLOCK_READ) {
        if (state->readers == 0U) {
            rw_unlock(state);
            r_library_internal_async_contract_violation();
        }
        state->readers -= 1U;
    } else {
        if (!state->writer) {
            rw_unlock(state);
            r_library_internal_async_contract_violation();
        }
        state->writer = 0;
    }
    finished = grant_locked(state);
    rw_unlock(state);
    r_library_internal_async_finish_all(finished);
    r_library_internal_async_rwlock_release(state);
}

void r_library_internal_async_rwlock_read_unlock(RLibraryAsyncRwLockState *state) {
    leave(state, R_ASYNC_RWLOCK_READ);
}

void r_library_internal_async_rwlock_write_unlock(RLibraryAsyncRwLockState *state) {
    leave(state, R_ASYNC_RWLOCK_WRITE);
}

RStdAsyncRwLockNewResult r_library_internal_async_rwlock_new(RRuntimeAllocator *allocator,
                                                             RRuntimeTypeInfo value_type,
                                                             void *staged_value) {
    RStdAsyncRwLockNewResult result = {0};
    RRuntimeAllocationStatus status;
    RLibraryAsyncRwLockState *state = NULL;
    size_t value_offset = sizeof(RLibraryAsyncRwLockState);
    size_t alignment = _Alignof(RLibraryAsyncRwLockState);
    const size_t value_alignment = value_type.alignment == 0U ? 1U : value_type.alignment;

    if ((allocator == NULL) || ((value_alignment & (value_alignment - 1U)) != 0U)) {
        r_library_internal_async_contract_violation();
    }
    if (value_alignment > alignment) {
        alignment = value_alignment;
    }
    value_offset = (value_offset + value_alignment - 1U) & ~(value_alignment - 1U);
    if (value_type.size > SIZE_MAX - value_offset) {
        status = R_RUNTIME_ALLOCATION_SIZE_OVERFLOW;
    } else {
        status = r_runtime_allocator_allocate(
            allocator, value_offset + value_type.size, alignment, (void **)&state);
    }
    if ((status == R_RUNTIME_ALLOCATION_OK) && (pthread_mutex_init(&state->lock, NULL) != 0)) {
        r_runtime_allocator_deallocate(state, alignment);
        status = R_RUNTIME_ALLOCATION_EXHAUSTED;
    }
    if (status != R_RUNTIME_ALLOCATION_OK) {
        if (value_type.drop != NULL) {
            value_type.drop(staged_value);
        }
        result.status = R_STD_ASYNC_CALL_ALLOCATION_ERROR;
        result.error = r_library_internal_async_allocation_error(status);
        return result;
    }
    state->waiters = (RLibraryAsyncWaitList){NULL, NULL, 0U};
    state->value_type = value_type;
    state->value = (unsigned char *)state + value_offset;
    state->allocation_alignment = alignment;
    state->references = 1U;
    state->readers = 0U;
    state->writer = 0;
    if (value_type.move_initialize != NULL) {
        value_type.move_initialize(state->value, staged_value);
    } else if (value_type.size != 0U) {
        (void)memcpy(state->value, staged_value, value_type.size);
    }
    result.status = R_STD_ASYNC_CALL_SUCCESS;
    result.value.state = state;
    return result;
}

RStdAsyncRwLock r_library_internal_async_rwlock_clone(const RStdAsyncRwLock *lock) {
    if ((lock == NULL) || (lock->state == NULL)) {
        r_library_internal_async_contract_violation();
    }
    rw_retain(lock->state);
    return (RStdAsyncRwLock){lock->state};
}

RStdAsyncStartResult r_library_internal_async_rwlock_start(const RStdAsyncRwLock *lock,
                                                           _Bool write,
                                                           RRuntimeTypeInfo result) {
    if ((lock == NULL) || (lock->state == NULL) || (result.size != sizeof(RStdAsyncRwReadGuard))) {
        r_library_internal_async_contract_violation();
    }
    return r_library_internal_async_start(
        &rw_operation, lock->state, write ? R_ASYNC_RWLOCK_WRITE : R_ASYNC_RWLOCK_READ, result);
}

_Bool r_library_internal_async_rwlock_try(const RStdAsyncRwLock *lock, _Bool write) {
    RLibraryAsyncRwLockState *state;
    const uint32_t mode = write ? R_ASYNC_RWLOCK_WRITE : R_ASYNC_RWLOCK_READ;

    if ((lock == NULL) || (lock->state == NULL)) {
        r_library_internal_async_contract_violation();
    }
    state = lock->state;
    rw_lock(state);
    if ((state->waiters.head != NULL) || !compatible_locked(state, mode)) {
        rw_unlock(state);
        return 0;
    }
    enter_locked(state, mode);
    rw_unlock(state);
    return 1;
}

void *r_library_internal_async_rwlock_value(RLibraryAsyncRwLockState *state) {
    if (state == NULL) {
        r_library_internal_async_contract_violation();
    }
    return state->value;
}

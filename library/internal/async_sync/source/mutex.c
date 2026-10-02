#include "r_library_async_sync_internal.h"

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * std.async::mutex<T> (R-SLIB-ASYNC-0013). One allocation holds the state and the protected value.
 * references counts handles, guards and started lock tasks; the value is destroyed when the last
 * reference ends. Unlocking hands the lock to the oldest waiter that can still complete, so the
 * mutex is never free while a waiter is linked and try_lock never overtakes a waiter.
 */
struct RLibraryAsyncMutexState {
    pthread_mutex_t lock;
    RLibraryAsyncWaitList waiters;
    RRuntimeTypeInfo value_type;
    unsigned char *value;
    size_t allocation_alignment;
    size_t references;
    _Bool locked;
};

static void mutex_lock(RLibraryAsyncMutexState *state) {
    if (pthread_mutex_lock(&state->lock) != 0) {
        r_library_internal_async_contract_violation();
    }
}

static void mutex_unlock(RLibraryAsyncMutexState *state) {
    if (pthread_mutex_unlock(&state->lock) != 0) {
        r_library_internal_async_contract_violation();
    }
}

static void mutex_retain_locked(RLibraryAsyncMutexState *state) {
    if (state->references >= (SIZE_MAX / 2U)) {
        mutex_unlock(state);
        r_library_internal_async_count_overflow();
    }
    state->references += 1U;
}

static void mutex_retain(void *resource) {
    RLibraryAsyncMutexState *state = resource;

    mutex_lock(state);
    mutex_retain_locked(state);
    mutex_unlock(state);
}

void r_library_internal_async_mutex_release(RLibraryAsyncMutexState *state) {
    _Bool destroy;

    mutex_lock(state);
    state->references -= 1U;
    destroy = state->references == 0U;
    mutex_unlock(state);
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

static void mutex_release(void *resource) {
    r_library_internal_async_mutex_release(resource);
}

static void write_guard(RLibraryAsyncWaiter *waiter, RLibraryAsyncMutexState *state) {
    const RStdAsyncMutexGuard guard = {state};

    (void)memcpy(waiter->result, &guard, sizeof(guard));
}

static RLibraryAsyncBegin lock_begin(void *resource, RLibraryAsyncWaiter *waiter) {
    RLibraryAsyncMutexState *state = resource;

    mutex_lock(state);
    if (!state->locked && (state->waiters.head == NULL)) {
        if (!r_library_internal_async_waiter_select(waiter)) {
            mutex_unlock(state);
            return R_LIBRARY_ASYNC_BEGIN_REJECTED;
        }
        state->locked = 1;
        mutex_retain_locked(state);
        write_guard(waiter, state);
        mutex_unlock(state);
        return R_LIBRARY_ASYNC_BEGIN_FINISHED;
    }
    r_library_internal_async_wait_push(&state->waiters, waiter);
    mutex_unlock(state);
    return R_LIBRARY_ASYNC_BEGIN_PENDING;
}

static void lock_withdraw(void *resource, RLibraryAsyncWaiter *waiter) {
    RLibraryAsyncMutexState *state = resource;

    mutex_lock(state);
    if (waiter->linked) {
        r_library_internal_async_wait_remove(&state->waiters, waiter);
    }
    mutex_unlock(state);
}

static const RLibraryAsyncOperation lock_operation = {
    lock_begin,
    lock_withdraw,
    mutex_retain,
    mutex_release,
};

/* The guard's reference passes to the next holder, or is released when none can complete. */
void r_library_internal_async_mutex_unlock(RLibraryAsyncMutexState *state) {
    RLibraryAsyncWaiter *selected = NULL;

    mutex_lock(state);
    if (!state->locked) {
        mutex_unlock(state);
        r_library_internal_async_contract_violation();
    }
    while ((selected == NULL) && (state->waiters.head != NULL)) {
        RLibraryAsyncWaiter *waiter = state->waiters.head;

        r_library_internal_async_wait_remove(&state->waiters, waiter);
        if (r_library_internal_async_waiter_select(waiter)) {
            write_guard(waiter, state);
            selected = waiter;
        }
    }
    if (selected == NULL) {
        state->locked = 0;
    }
    mutex_unlock(state);
    if (selected != NULL) {
        r_library_internal_async_waiter_finish(selected);
        return;
    }
    r_library_internal_async_mutex_release(state);
}

RStdAsyncMutexNewResult r_library_internal_async_mutex_new(RRuntimeAllocator *allocator,
                                                           RRuntimeTypeInfo value_type,
                                                           void *staged_value) {
    RStdAsyncMutexNewResult result = {0};
    RRuntimeAllocationStatus status;
    RLibraryAsyncMutexState *state = NULL;
    size_t value_offset = sizeof(RLibraryAsyncMutexState);
    size_t alignment = _Alignof(RLibraryAsyncMutexState);
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
    state->locked = 0;
    if (value_type.move_initialize != NULL) {
        value_type.move_initialize(state->value, staged_value);
    } else if (value_type.size != 0U) {
        (void)memcpy(state->value, staged_value, value_type.size);
    }
    result.status = R_STD_ASYNC_CALL_SUCCESS;
    result.value.state = state;
    return result;
}

RStdAsyncMutex r_library_internal_async_mutex_clone(const RStdAsyncMutex *mutex) {
    if ((mutex == NULL) || (mutex->state == NULL)) {
        r_library_internal_async_contract_violation();
    }
    mutex_retain(mutex->state);
    return (RStdAsyncMutex){mutex->state};
}

RStdAsyncStartResult r_library_internal_async_mutex_lock(const RStdAsyncMutex *mutex,
                                                         RRuntimeTypeInfo result) {
    if ((mutex == NULL) || (mutex->state == NULL) || (result.size != sizeof(RStdAsyncMutexGuard))) {
        r_library_internal_async_contract_violation();
    }
    return r_library_internal_async_start(&lock_operation, mutex->state, 0U, result);
}

_Bool r_library_internal_async_mutex_try_lock(const RStdAsyncMutex *mutex,
                                              RStdAsyncMutexGuard *guard) {
    RLibraryAsyncMutexState *state;

    if ((mutex == NULL) || (mutex->state == NULL) || (guard == NULL)) {
        r_library_internal_async_contract_violation();
    }
    state = mutex->state;
    mutex_lock(state);
    if (state->locked || (state->waiters.head != NULL)) {
        mutex_unlock(state);
        return 0;
    }
    state->locked = 1;
    mutex_retain_locked(state);
    mutex_unlock(state);
    guard->state = state;
    return 1;
}

void *r_library_internal_async_mutex_value(const RStdAsyncMutexGuard *guard) {
    if ((guard == NULL) || (guard->state == NULL)) {
        r_library_internal_async_contract_violation();
    }
    return guard->state->value;
}

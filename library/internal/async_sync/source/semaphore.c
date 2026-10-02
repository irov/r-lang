#include "r_library_async_sync_internal.h"

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * std.async::semaphore (R-SLIB-ASYNC-0014). available counts the permits no holder owns; a
 * returned or added permit goes to the oldest waiter that can still complete, so try_acquire
 * never overtakes a waiter. Every permit and started task holds one reference.
 */
struct RLibraryAsyncSemaphoreState {
    pthread_mutex_t lock;
    RLibraryAsyncWaitList waiters;
    size_t references;
    size_t available;
    size_t outstanding;
    size_t allocation_alignment;
};

static void semaphore_lock(RLibraryAsyncSemaphoreState *state) {
    if (pthread_mutex_lock(&state->lock) != 0) {
        r_library_internal_async_contract_violation();
    }
}

static void semaphore_unlock(RLibraryAsyncSemaphoreState *state) {
    if (pthread_mutex_unlock(&state->lock) != 0) {
        r_library_internal_async_contract_violation();
    }
}

static void semaphore_retain_locked(RLibraryAsyncSemaphoreState *state) {
    if (state->references >= (SIZE_MAX / 2U)) {
        semaphore_unlock(state);
        r_library_internal_async_count_overflow();
    }
    state->references += 1U;
}

static void semaphore_retain(void *resource) {
    RLibraryAsyncSemaphoreState *state = resource;

    semaphore_lock(state);
    semaphore_retain_locked(state);
    semaphore_unlock(state);
}

void r_library_internal_async_semaphore_release(RLibraryAsyncSemaphoreState *state) {
    _Bool destroy;

    semaphore_lock(state);
    state->references -= 1U;
    destroy = state->references == 0U;
    semaphore_unlock(state);
    if (!destroy) {
        return;
    }
    if (pthread_mutex_destroy(&state->lock) != 0) {
        r_library_internal_async_contract_violation();
    }
    r_runtime_allocator_deallocate(state, state->allocation_alignment);
}

static void semaphore_release(void *resource) {
    r_library_internal_async_semaphore_release(resource);
}

static void take_locked(RLibraryAsyncSemaphoreState *state) {
    state->available -= 1U;
    state->outstanding += 1U;
    semaphore_retain_locked(state);
}

static void write_permit(RLibraryAsyncWaiter *waiter, RLibraryAsyncSemaphoreState *state) {
    const RStdAsyncSemaphorePermit permit = {state};

    (void)memcpy(waiter->result, &permit, sizeof(permit));
}

/* Under the lock: gives the available permits to the waiters in start order. */
static RLibraryAsyncWaiter *grant_locked(RLibraryAsyncSemaphoreState *state) {
    RLibraryAsyncFinished finished = {NULL, NULL};

    while ((state->available != 0U) && (state->waiters.head != NULL)) {
        RLibraryAsyncWaiter *waiter = state->waiters.head;

        r_library_internal_async_wait_remove(&state->waiters, waiter);
        if (!r_library_internal_async_waiter_select(waiter)) {
            continue;
        }
        take_locked(state);
        write_permit(waiter, state);
        r_library_internal_async_finished_append(&finished, waiter);
    }
    return finished.head;
}

static RLibraryAsyncBegin acquire_begin(void *resource, RLibraryAsyncWaiter *waiter) {
    RLibraryAsyncSemaphoreState *state = resource;

    semaphore_lock(state);
    if ((state->available != 0U) && (state->waiters.head == NULL)) {
        if (!r_library_internal_async_waiter_select(waiter)) {
            semaphore_unlock(state);
            return R_LIBRARY_ASYNC_BEGIN_REJECTED;
        }
        take_locked(state);
        write_permit(waiter, state);
        semaphore_unlock(state);
        return R_LIBRARY_ASYNC_BEGIN_FINISHED;
    }
    r_library_internal_async_wait_push(&state->waiters, waiter);
    semaphore_unlock(state);
    return R_LIBRARY_ASYNC_BEGIN_PENDING;
}

static void acquire_withdraw(void *resource, RLibraryAsyncWaiter *waiter) {
    RLibraryAsyncSemaphoreState *state = resource;

    semaphore_lock(state);
    if (waiter->linked) {
        r_library_internal_async_wait_remove(&state->waiters, waiter);
    }
    semaphore_unlock(state);
}

static const RLibraryAsyncOperation acquire_operation = {
    acquire_begin,
    acquire_withdraw,
    semaphore_retain,
    semaphore_release,
};

void r_library_internal_async_semaphore_return(RLibraryAsyncSemaphoreState *state) {
    RLibraryAsyncWaiter *finished;

    semaphore_lock(state);
    if (state->outstanding == 0U) {
        semaphore_unlock(state);
        r_library_internal_async_contract_violation();
    }
    state->outstanding -= 1U;
    state->available += 1U;
    finished = grant_locked(state);
    semaphore_unlock(state);
    r_library_internal_async_finish_all(finished);
    r_library_internal_async_semaphore_release(state);
}

RStdAsyncSemaphoreNewResult r_library_internal_async_semaphore_new(RRuntimeAllocator *allocator,
                                                                   size_t permits) {
    RStdAsyncSemaphoreNewResult result = {0};
    RLibraryAsyncSemaphoreState *state = NULL;
    RRuntimeAllocationStatus status;

    if (allocator == NULL) {
        r_library_internal_async_contract_violation();
    }
    if (permits > (SIZE_MAX / 2U)) {
        r_library_internal_async_count_overflow();
    }
    status = r_runtime_allocator_allocate(allocator,
                                          sizeof(RLibraryAsyncSemaphoreState),
                                          _Alignof(RLibraryAsyncSemaphoreState),
                                          (void **)&state);
    if ((status == R_RUNTIME_ALLOCATION_OK) && (pthread_mutex_init(&state->lock, NULL) != 0)) {
        r_runtime_allocator_deallocate(state, _Alignof(RLibraryAsyncSemaphoreState));
        status = R_RUNTIME_ALLOCATION_EXHAUSTED;
    }
    if (status != R_RUNTIME_ALLOCATION_OK) {
        result.status = R_STD_ASYNC_CALL_ALLOCATION_ERROR;
        result.error = r_library_internal_async_allocation_error(status);
        return result;
    }
    state->waiters = (RLibraryAsyncWaitList){NULL, NULL, 0U};
    state->references = 1U;
    state->available = permits;
    state->outstanding = 0U;
    state->allocation_alignment = _Alignof(RLibraryAsyncSemaphoreState);
    result.status = R_STD_ASYNC_CALL_SUCCESS;
    result.value.state = state;
    return result;
}

RStdAsyncSemaphore r_library_internal_async_semaphore_clone(const RStdAsyncSemaphore *semaphore) {
    if ((semaphore == NULL) || (semaphore->state == NULL)) {
        r_library_internal_async_contract_violation();
    }
    semaphore_retain(semaphore->state);
    return (RStdAsyncSemaphore){semaphore->state};
}

RStdAsyncStartResult r_library_internal_async_semaphore_acquire(const RStdAsyncSemaphore *semaphore,
                                                                RRuntimeTypeInfo result) {
    if ((semaphore == NULL) || (semaphore->state == NULL) ||
        (result.size != sizeof(RStdAsyncSemaphorePermit))) {
        r_library_internal_async_contract_violation();
    }
    return r_library_internal_async_start(&acquire_operation, semaphore->state, 0U, result);
}

_Bool r_library_internal_async_semaphore_try_acquire(const RStdAsyncSemaphore *semaphore,
                                                     RStdAsyncSemaphorePermit *permit) {
    RLibraryAsyncSemaphoreState *state;

    if ((semaphore == NULL) || (semaphore->state == NULL) || (permit == NULL)) {
        r_library_internal_async_contract_violation();
    }
    state = semaphore->state;
    semaphore_lock(state);
    if ((state->available == 0U) || (state->waiters.head != NULL)) {
        semaphore_unlock(state);
        return 0;
    }
    take_locked(state);
    semaphore_unlock(state);
    permit->state = state;
    return 1;
}

void r_library_internal_async_semaphore_add(const RStdAsyncSemaphore *semaphore, size_t count) {
    RLibraryAsyncSemaphoreState *state;
    RLibraryAsyncWaiter *finished;

    if ((semaphore == NULL) || (semaphore->state == NULL)) {
        r_library_internal_async_contract_violation();
    }
    state = semaphore->state;
    semaphore_lock(state);
    if ((count > (SIZE_MAX / 2U)) ||
        (state->available + state->outstanding > (SIZE_MAX / 2U) - count)) {
        semaphore_unlock(state);
        r_library_internal_async_count_overflow();
    }
    state->available += count;
    finished = grant_locked(state);
    semaphore_unlock(state);
    r_library_internal_async_finish_all(finished);
}

size_t r_library_internal_async_semaphore_available(const RStdAsyncSemaphore *semaphore) {
    RLibraryAsyncSemaphoreState *state;
    size_t available;

    if ((semaphore == NULL) || (semaphore->state == NULL)) {
        r_library_internal_async_contract_violation();
    }
    state = semaphore->state;
    semaphore_lock(state);
    available = state->available;
    semaphore_unlock(state);
    return available;
}

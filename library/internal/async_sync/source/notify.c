#include "r_library_async_sync_internal.h"

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

/*
 * std.async::notify (R-SLIB-ASYNC-0015). notify_one completes the oldest waiter that can still
 * complete, or stores one notification when none can; the next notified then completes at once.
 * notify_all completes every linked waiter and stores nothing. A notification is never lost to a
 * cancelled waiter, because select precedes the completion.
 */
struct RLibraryAsyncNotifyState {
    pthread_mutex_t lock;
    RLibraryAsyncWaitList waiters;
    size_t references;
    _Bool stored;
};

static void notify_lock(RLibraryAsyncNotifyState *state) {
    if (pthread_mutex_lock(&state->lock) != 0) {
        r_library_internal_async_contract_violation();
    }
}

static void notify_unlock(RLibraryAsyncNotifyState *state) {
    if (pthread_mutex_unlock(&state->lock) != 0) {
        r_library_internal_async_contract_violation();
    }
}

static void notify_retain(void *resource) {
    RLibraryAsyncNotifyState *state = resource;

    notify_lock(state);
    if (state->references >= (SIZE_MAX / 2U)) {
        notify_unlock(state);
        r_library_internal_async_count_overflow();
    }
    state->references += 1U;
    notify_unlock(state);
}

void r_library_internal_async_notify_release(RLibraryAsyncNotifyState *state) {
    _Bool destroy;

    notify_lock(state);
    state->references -= 1U;
    destroy = state->references == 0U;
    notify_unlock(state);
    if (!destroy) {
        return;
    }
    if (pthread_mutex_destroy(&state->lock) != 0) {
        r_library_internal_async_contract_violation();
    }
    r_runtime_allocator_deallocate(state, _Alignof(RLibraryAsyncNotifyState));
}

static void notify_release(void *resource) {
    r_library_internal_async_notify_release(resource);
}

static RLibraryAsyncBegin notified_begin(void *resource, RLibraryAsyncWaiter *waiter) {
    RLibraryAsyncNotifyState *state = resource;

    notify_lock(state);
    if (state->stored) {
        if (!r_library_internal_async_waiter_select(waiter)) {
            notify_unlock(state);
            return R_LIBRARY_ASYNC_BEGIN_REJECTED;
        }
        state->stored = 0;
        notify_unlock(state);
        return R_LIBRARY_ASYNC_BEGIN_FINISHED;
    }
    r_library_internal_async_wait_push(&state->waiters, waiter);
    notify_unlock(state);
    return R_LIBRARY_ASYNC_BEGIN_PENDING;
}

static void notified_withdraw(void *resource, RLibraryAsyncWaiter *waiter) {
    RLibraryAsyncNotifyState *state = resource;

    notify_lock(state);
    if (waiter->linked) {
        r_library_internal_async_wait_remove(&state->waiters, waiter);
    }
    notify_unlock(state);
}

static const RLibraryAsyncOperation notified_operation = {
    notified_begin,
    notified_withdraw,
    notify_retain,
    notify_release,
};

RStdAsyncNotifyNewResult r_library_internal_async_notify_new(RRuntimeAllocator *allocator) {
    RStdAsyncNotifyNewResult result = {0};
    RLibraryAsyncNotifyState *state = NULL;
    RRuntimeAllocationStatus status;

    if (allocator == NULL) {
        r_library_internal_async_contract_violation();
    }
    status = r_runtime_allocator_allocate(allocator,
                                          sizeof(RLibraryAsyncNotifyState),
                                          _Alignof(RLibraryAsyncNotifyState),
                                          (void **)&state);
    if ((status == R_RUNTIME_ALLOCATION_OK) && (pthread_mutex_init(&state->lock, NULL) != 0)) {
        r_runtime_allocator_deallocate(state, _Alignof(RLibraryAsyncNotifyState));
        status = R_RUNTIME_ALLOCATION_EXHAUSTED;
    }
    if (status != R_RUNTIME_ALLOCATION_OK) {
        result.status = R_STD_ASYNC_CALL_ALLOCATION_ERROR;
        result.error = r_library_internal_async_allocation_error(status);
        return result;
    }
    state->waiters = (RLibraryAsyncWaitList){NULL, NULL, 0U};
    state->references = 1U;
    state->stored = 0;
    result.status = R_STD_ASYNC_CALL_SUCCESS;
    result.value.state = state;
    return result;
}

RStdAsyncNotify r_library_internal_async_notify_clone(const RStdAsyncNotify *notify) {
    if ((notify == NULL) || (notify->state == NULL)) {
        r_library_internal_async_contract_violation();
    }
    notify_retain(notify->state);
    return (RStdAsyncNotify){notify->state};
}

void r_library_internal_async_notify_wake(const RStdAsyncNotify *notify, _Bool all) {
    RLibraryAsyncNotifyState *state;
    RLibraryAsyncFinished finished = {NULL, NULL};

    if ((notify == NULL) || (notify->state == NULL)) {
        r_library_internal_async_contract_violation();
    }
    state = notify->state;
    notify_lock(state);
    while (state->waiters.head != NULL) {
        RLibraryAsyncWaiter *waiter = state->waiters.head;

        r_library_internal_async_wait_remove(&state->waiters, waiter);
        if (!r_library_internal_async_waiter_select(waiter)) {
            continue;
        }
        r_library_internal_async_finished_append(&finished, waiter);
        if (!all) {
            break;
        }
    }
    if (!all && (finished.head == NULL)) {
        state->stored = 1;
    }
    notify_unlock(state);
    r_library_internal_async_finish_all(finished.head);
}

RStdAsyncStartResult r_library_internal_async_notify_wait(const RStdAsyncNotify *notify) {
    const RRuntimeTypeInfo result = {0U, 1U, NULL, NULL};

    if ((notify == NULL) || (notify->state == NULL)) {
        r_library_internal_async_contract_violation();
    }
    return r_library_internal_async_start(&notified_operation, notify->state, 0U, result);
}

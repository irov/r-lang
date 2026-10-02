#ifndef R_LIBRARY_ASYNC_SYNC_INTERNAL_H
#define R_LIBRARY_ASYNC_SYNC_INTERNAL_H

#include "r_runtime_allocator.h"
#include "r_runtime_task.h"
#include "r_runtime_type.h"
#include "r_std_async.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Asynchronous waiters shared by the std.async resources (R-SLIB-ASYNC-0013..0016). A waiter is the
 * payload head of one external task. A resource links waiters in start order under its own lock
 * and serves the oldest first: it calls select before it transfers anything to a waiter, and a
 * waiter whose select fails is unlinked and left to its cancellation, so the resource passes to
 * the next waiter and nothing is lost. After the lock is released the resource calls finish for
 * every selected waiter; finish acknowledges the external execution on a native queue, because
 * the releasing thread may be an executor worker in the middle of its step.
 */
typedef struct RLibraryAsyncWaiter RLibraryAsyncWaiter;
struct RLibraryAsyncWaiter {
    RLibraryAsyncWaiter *next;
    RLibraryAsyncWaiter *previous;
    RLibraryAsyncWaiter *finished_next;
    RRuntimeTaskExternalExecution *execution;
    unsigned char *result;
    uint32_t mode;
    _Bool linked;
};

typedef struct RLibraryAsyncWaitList {
    RLibraryAsyncWaiter *head;
    RLibraryAsyncWaiter *tail;
    size_t count;
} RLibraryAsyncWaitList;

void r_library_internal_async_wait_push(RLibraryAsyncWaitList *list, RLibraryAsyncWaiter *waiter);
void r_library_internal_async_wait_remove(RLibraryAsyncWaitList *list, RLibraryAsyncWaiter *waiter);
_Bool r_library_internal_async_waiter_select(RLibraryAsyncWaiter *waiter);
void r_library_internal_async_waiter_finish(RLibraryAsyncWaiter *waiter);
void r_library_internal_async_finish_all(RLibraryAsyncWaiter *finished);

/* Selected waiters in the order they were served, finished after the resource lock is released. */
typedef struct RLibraryAsyncFinished {
    RLibraryAsyncWaiter *head;
    RLibraryAsyncWaiter *tail;
} RLibraryAsyncFinished;

static inline void r_library_internal_async_finished_append(RLibraryAsyncFinished *finished,
                                                            RLibraryAsyncWaiter *waiter) {
    waiter->finished_next = NULL;
    if (finished->tail == NULL) {
        finished->head = waiter;
    } else {
        finished->tail->finished_next = waiter;
    }
    finished->tail = waiter;
}

typedef enum RLibraryAsyncBegin {
    /* Linked: a later release, notification or disconnection finishes the waiter. */
    R_LIBRARY_ASYNC_BEGIN_PENDING = 0,
    /* Selected at once with its result written; the caller finishes the waiter. */
    R_LIBRARY_ASYNC_BEGIN_FINISHED,
    /* select failed because cancellation won; nothing was transferred. */
    R_LIBRARY_ASYNC_BEGIN_REJECTED
} RLibraryAsyncBegin;

/*
 * One waiting operation of a resource. begin runs from the external start with the waiter bound
 * to its execution and result storage; withdraw runs from the cancellation and unlinks a waiter
 * that is still linked. retain and release keep the resource alive while the task holds it.
 */
typedef struct RLibraryAsyncOperation {
    RLibraryAsyncBegin (*begin)(void *resource, RLibraryAsyncWaiter *waiter);
    void (*withdraw)(void *resource, RLibraryAsyncWaiter *waiter);
    void (*retain)(void *resource);
    void (*release)(void *resource);
} RLibraryAsyncOperation;

/* Starts one waiting operation; result describes the task value, which the operation writes. */
RStdAsyncStartResult r_library_internal_async_start(const RLibraryAsyncOperation *operation,
                                                    void *resource,
                                                    uint32_t mode,
                                                    RRuntimeTypeInfo result);

_Noreturn void r_library_internal_async_contract_violation(void);
_Noreturn void r_library_internal_async_count_overflow(void);
RStdAllocError r_library_internal_async_allocation_error(RRuntimeAllocationStatus status);

RStdAsyncMutexNewResult r_library_internal_async_mutex_new(RRuntimeAllocator *allocator,
                                                           RRuntimeTypeInfo value_type,
                                                           void *staged_value);
RStdAsyncMutex r_library_internal_async_mutex_clone(const RStdAsyncMutex *mutex);
RStdAsyncStartResult r_library_internal_async_mutex_lock(const RStdAsyncMutex *mutex,
                                                         RRuntimeTypeInfo result);
_Bool r_library_internal_async_mutex_try_lock(const RStdAsyncMutex *mutex,
                                              RStdAsyncMutexGuard *guard);
void *r_library_internal_async_mutex_value(const RStdAsyncMutexGuard *guard);

RStdAsyncRwLockNewResult r_library_internal_async_rwlock_new(RRuntimeAllocator *allocator,
                                                             RRuntimeTypeInfo value_type,
                                                             void *staged_value);
RStdAsyncRwLock r_library_internal_async_rwlock_clone(const RStdAsyncRwLock *lock);
RStdAsyncStartResult r_library_internal_async_rwlock_start(const RStdAsyncRwLock *lock,
                                                           _Bool write,
                                                           RRuntimeTypeInfo result);
_Bool r_library_internal_async_rwlock_try(const RStdAsyncRwLock *lock, _Bool write);
void *r_library_internal_async_rwlock_value(RLibraryAsyncRwLockState *state);

RStdAsyncSemaphoreNewResult r_library_internal_async_semaphore_new(RRuntimeAllocator *allocator,
                                                                   size_t permits);
RStdAsyncSemaphore r_library_internal_async_semaphore_clone(const RStdAsyncSemaphore *semaphore);
RStdAsyncStartResult r_library_internal_async_semaphore_acquire(const RStdAsyncSemaphore *semaphore,
                                                                RRuntimeTypeInfo result);
_Bool r_library_internal_async_semaphore_try_acquire(const RStdAsyncSemaphore *semaphore,
                                                     RStdAsyncSemaphorePermit *permit);
void r_library_internal_async_semaphore_add(const RStdAsyncSemaphore *semaphore, size_t count);
size_t r_library_internal_async_semaphore_available(const RStdAsyncSemaphore *semaphore);

RStdAsyncNotifyNewResult r_library_internal_async_notify_new(RRuntimeAllocator *allocator);
RStdAsyncNotify r_library_internal_async_notify_clone(const RStdAsyncNotify *notify);
void r_library_internal_async_notify_wake(const RStdAsyncNotify *notify, _Bool all);
RStdAsyncStartResult r_library_internal_async_notify_wait(const RStdAsyncNotify *notify);

RStdAsyncBroadcastNewResult r_library_internal_async_broadcast_new(RRuntimeAllocator *allocator,
                                                                   RRuntimeTypeInfo element,
                                                                   RStdAsyncCloneFn clone,
                                                                   size_t capacity);
RStdAsyncBroadcast r_library_internal_async_broadcast_clone(const RStdAsyncBroadcast *sender);
RStdAsyncSubscribeResult
r_library_internal_async_broadcast_subscribe(const RStdAsyncBroadcast *sender);
RStdAsyncPublishResult r_library_internal_async_broadcast_publish(const RStdAsyncBroadcast *sender,
                                                                  void *staged_value);
RStdAsyncStartResult
r_library_internal_async_broadcast_receive(const RStdAsyncBroadcastReceiver *receiver,
                                           RStdAsyncBroadcastReceiveLayout layout);

#endif

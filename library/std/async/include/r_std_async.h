#ifndef R_STD_ASYNC_H
#define R_STD_ASYNC_H

#include "r_runtime_allocator.h"
#include "r_runtime_task.h"
#include "r_runtime_type.h"
#include "r_std_alloc.h"

#include <stddef.h>
#include <stdint.h>

typedef enum RStdAsyncStartError {
    R_STD_ASYNC_START_ALLOCATION_FAILED = 0,
    R_STD_ASYNC_START_RUNTIME_STOPPING,
    R_STD_ASYNC_START_SCOPE_FULL,
    R_STD_ASYNC_START_BUDGET_EXHAUSTED
} RStdAsyncStartError;

/* The start_error of a start refused for allocation, read right after it on the same thread:
 * budget_exhausted when a budget refused the frame or the task (Core R-STMT-0020),
 * allocation_failed otherwise. */
#define R_STD_ASYNC_START_REFUSAL()                                                                \
    (r_runtime_allocation_refused_by_budget() ? R_STD_ASYNC_START_BUDGET_EXHAUSTED                 \
                                              : R_STD_ASYNC_START_ALLOCATION_FAILED)

typedef struct RStdAsyncStartResult {
    _Bool is_ok;
    RRuntimeTask *task;
    RStdAsyncStartError error;
} RStdAsyncStartResult;

/*
 * Ownership: consumes the sole task observation, requests cancellation, and leaves operation
 * empty. Runtime ownership retains the frame and result storage through acknowledgement.
 */
void r_std_async_cancel(RRuntimeTask **operation);

/*
 * Ownership: consumes the sole task observation without requesting cancellation and leaves
 * operation empty. An eventual unobserved result is dropped exactly once by the runtime.
 */
void r_std_async_detach(RRuntimeTask **operation);

/* std.async::task_id (R-SLIB-ASYNC-0018): the identifier of the running task, or zero outside every
 * task. Allocation-free and lock-free. */
uint64_t r_std_async_task_id(void);

/*
 * std.async::blocking (R-SLIB-ASYNC-0017): runs entry exactly once on the blocking call pool of
 * Core R-TERM-0015. entry is the compiler's entry trampoline: it moves the arguments out of payload
 * and writes the completion carrier that result describes. A successful start consumes
 * staged_payload; a failure leaves every staged argument with the caller. Cancellation removes a
 * call that no pool thread has taken; a taken call runs to its return, after which its result is
 * destroyed and the task reports cancellation.
 */
typedef void (*RStdAsyncBlockingEntryFn)(void *payload, void *result);

RStdAsyncStartResult r_std_async_blocking(RRuntimeTypeInfo payload_type,
                                          RRuntimeTypeInfo result_type,
                                          RStdAsyncBlockingEntryFn entry,
                                          void *staged_payload);

/*
 * Asynchronous resources (R-SLIB-ASYNC-0013..0016). Every handle is a Move-only reference to one
 * reference-counted shared state; clone adds a reference and destruction releases one. A guard,
 * permit or subscription also holds a reference, so it never borrows its handle and may remain
 * live across a suspension. Waiters are served in start order, a resource is handed to the next
 * waiter when a selected waiter lost to its cancellation, and no resource is ever lost.
 *
 * Compiler wrapper contract:
 *
 * - handles, guards and permits are one pointer; generated Move glue calls the *_move_initialize
 *   functions below and generated drop glue the *_destroy functions, never a byte copy;
 * - a constructor consumes staged_value in every outcome: on failure the library destroys it;
 * - an asynchronous operation borrows its handle only for the start, retains the shared state
 *   until the task is terminal, and writes its result into storage that result describes;
 * - Send and Sync of every schema are derived by the compiler from R-LIB-0017 and
 *   R-SLIB-ASYNC-0013..0016; no runtime bit can elevate them.
 */
typedef struct RLibraryAsyncMutexState RLibraryAsyncMutexState;
typedef struct RLibraryAsyncRwLockState RLibraryAsyncRwLockState;
typedef struct RLibraryAsyncSemaphoreState RLibraryAsyncSemaphoreState;
typedef struct RLibraryAsyncNotifyState RLibraryAsyncNotifyState;
typedef struct RLibraryAsyncBroadcastState RLibraryAsyncBroadcastState;
typedef struct RLibraryAsyncBroadcastQueue RLibraryAsyncBroadcastQueue;

typedef enum RStdAsyncCallStatus {
    R_STD_ASYNC_CALL_SUCCESS = 0,
    R_STD_ASYNC_CALL_ALLOCATION_ERROR = 1,
    R_STD_ASYNC_CALL_CONTRACT_VIOLATION = 2
} RStdAsyncCallStatus;

typedef enum RStdAsyncGuardKind {
    R_STD_ASYNC_GUARD_MUTEX = 1,
    R_STD_ASYNC_GUARD_RW_READ = 2,
    R_STD_ASYNC_GUARD_RW_WRITE = 3
} RStdAsyncGuardKind;

typedef struct RStdAsyncMutex {
    RLibraryAsyncMutexState *state;
} RStdAsyncMutex;

typedef struct RStdAsyncMutexGuard {
    RLibraryAsyncMutexState *state;
} RStdAsyncMutexGuard;

typedef struct RStdAsyncRwLock {
    RLibraryAsyncRwLockState *state;
} RStdAsyncRwLock;

typedef struct RStdAsyncRwReadGuard {
    RLibraryAsyncRwLockState *state;
} RStdAsyncRwReadGuard;

typedef struct RStdAsyncRwWriteGuard {
    RLibraryAsyncRwLockState *state;
} RStdAsyncRwWriteGuard;

typedef struct RStdAsyncSemaphore {
    RLibraryAsyncSemaphoreState *state;
} RStdAsyncSemaphore;

typedef struct RStdAsyncSemaphorePermit {
    RLibraryAsyncSemaphoreState *state;
} RStdAsyncSemaphorePermit;

typedef struct RStdAsyncNotify {
    RLibraryAsyncNotifyState *state;
} RStdAsyncNotify;

typedef struct RStdAsyncBroadcast {
    RLibraryAsyncBroadcastState *state;
} RStdAsyncBroadcast;

typedef struct RStdAsyncBroadcastReceiver {
    RLibraryAsyncBroadcastQueue *queue;
} RStdAsyncBroadcastReceiver;

typedef struct RStdAsyncMutexNewResult {
    RStdAsyncCallStatus status;
    RStdAllocError error;
    RStdAsyncMutex value;
} RStdAsyncMutexNewResult;

typedef struct RStdAsyncRwLockNewResult {
    RStdAsyncCallStatus status;
    RStdAllocError error;
    RStdAsyncRwLock value;
} RStdAsyncRwLockNewResult;

typedef struct RStdAsyncSemaphoreNewResult {
    RStdAsyncCallStatus status;
    RStdAllocError error;
    RStdAsyncSemaphore value;
} RStdAsyncSemaphoreNewResult;

typedef struct RStdAsyncNotifyNewResult {
    RStdAsyncCallStatus status;
    RStdAllocError error;
    RStdAsyncNotify value;
} RStdAsyncNotifyNewResult;

typedef struct RStdAsyncBroadcastNewResult {
    RStdAsyncCallStatus status;
    RStdAllocError error;
    RStdAsyncBroadcast value;
} RStdAsyncBroadcastNewResult;

typedef struct RStdAsyncSubscribeResult {
    RStdAsyncCallStatus status;
    RStdAllocError error;
    RStdAsyncBroadcastReceiver value;
} RStdAsyncSubscribeResult;

typedef struct RStdAsyncPublishResult {
    RStdAsyncCallStatus status;
    RStdAllocError error;
    size_t count;
} RStdAsyncPublishResult;

/* The alternatives of std.async::broadcast_result<T>; the compiler lays out the payloads. */
typedef enum RStdAsyncBroadcastResultKind {
    R_STD_ASYNC_BROADCAST_RESULT_RECEIVED = 0,
    R_STD_ASYNC_BROADCAST_RESULT_LAGGED = 1,
    R_STD_ASYNC_BROADCAST_RESULT_CLOSED = 2
} RStdAsyncBroadcastResultKind;

typedef struct RStdAsyncBroadcastResult {
    RStdAsyncBroadcastResultKind kind;
    uint64_t lagged;
} RStdAsyncBroadcastResult;

/*
 * Builds an independent copy of the value at source in uninitialized destination storage. On
 * failure it builds nothing, stores the allocation error and returns false. A null clone function
 * copies the bytes of a Copy value.
 */
typedef _Bool (*RStdAsyncCloneFn)(void *destination, const void *source, RStdAllocError *error);

/*
 * std.async::broadcast_receive: the generated broadcast_result<T> layout. result describes the
 * whole value; its uint32_t tag at tag_offset is 0 for received, 1 for lagged and 2 for closed,
 * the T payload of received is at value_offset and the uint64_t payload of lagged at lag_offset.
 */
typedef struct RStdAsyncBroadcastReceiveLayout {
    RRuntimeTypeInfo result;
    size_t tag_offset;
    size_t value_offset;
    size_t lag_offset;
} RStdAsyncBroadcastReceiveLayout;

RStdAsyncMutexNewResult r_std_async_mutex_new(RRuntimeAllocator *allocator,
                                              RRuntimeTypeInfo value_type,
                                              void *staged_value);
RStdAsyncMutex r_std_async_clone_mutex(const RStdAsyncMutex *mutex);
RStdAsyncStartResult r_std_async_lock(const RStdAsyncMutex *mutex, RRuntimeTypeInfo result);
_Bool r_std_async_try_lock(const RStdAsyncMutex *mutex, RStdAsyncMutexGuard *guard);
const void *r_std_async_mutex_guard_ref(const RStdAsyncMutexGuard *guard);
void *r_std_async_mutex_guard_mut(RStdAsyncMutexGuard *guard);
void r_std_async_unlock(void *guard, RStdAsyncGuardKind kind);

RStdAsyncRwLockNewResult r_std_async_rwlock_new(RRuntimeAllocator *allocator,
                                                RRuntimeTypeInfo value_type,
                                                void *staged_value);
RStdAsyncRwLock r_std_async_clone_rw_lock(const RStdAsyncRwLock *lock);
RStdAsyncStartResult r_std_async_read(const RStdAsyncRwLock *lock, RRuntimeTypeInfo result);
RStdAsyncStartResult r_std_async_write(const RStdAsyncRwLock *lock, RRuntimeTypeInfo result);
_Bool r_std_async_try_read(const RStdAsyncRwLock *lock, RStdAsyncRwReadGuard *guard);
_Bool r_std_async_try_write(const RStdAsyncRwLock *lock, RStdAsyncRwWriteGuard *guard);
const void *r_std_async_rw_read_guard_ref(const RStdAsyncRwReadGuard *guard);
const void *r_std_async_rw_write_guard_ref(const RStdAsyncRwWriteGuard *guard);
void *r_std_async_rw_write_guard_mut(RStdAsyncRwWriteGuard *guard);

RStdAsyncSemaphoreNewResult r_std_async_semaphore_new(RRuntimeAllocator *allocator, size_t permits);
RStdAsyncSemaphore r_std_async_clone_semaphore(const RStdAsyncSemaphore *semaphore);
RStdAsyncStartResult r_std_async_acquire(const RStdAsyncSemaphore *semaphore,
                                         RRuntimeTypeInfo result);
_Bool r_std_async_try_acquire(const RStdAsyncSemaphore *semaphore,
                              RStdAsyncSemaphorePermit *permit);
void r_std_async_release(RStdAsyncSemaphorePermit *permit);
void r_std_async_add_permits(const RStdAsyncSemaphore *semaphore, size_t count);
size_t r_std_async_available_permits(const RStdAsyncSemaphore *semaphore);

RStdAsyncNotifyNewResult r_std_async_notify_new(RRuntimeAllocator *allocator);
RStdAsyncNotify r_std_async_clone_notify(const RStdAsyncNotify *notify);
void r_std_async_notify_one(const RStdAsyncNotify *notify);
void r_std_async_notify_all(const RStdAsyncNotify *notify);
RStdAsyncStartResult r_std_async_notified(const RStdAsyncNotify *notify);

RStdAsyncBroadcastNewResult r_std_async_broadcast(RRuntimeAllocator *allocator,
                                                  RRuntimeTypeInfo element,
                                                  RStdAsyncCloneFn clone,
                                                  size_t capacity);
RStdAsyncBroadcast r_std_async_clone_broadcast(const RStdAsyncBroadcast *sender);
RStdAsyncSubscribeResult r_std_async_subscribe(const RStdAsyncBroadcast *sender);
RStdAsyncPublishResult r_std_async_publish(const RStdAsyncBroadcast *sender, void *staged_value);
RStdAsyncStartResult r_std_async_broadcast_receive(const RStdAsyncBroadcastReceiver *receiver,
                                                   RStdAsyncBroadcastReceiveLayout layout);

void r_library_internal_async_mutex_release(RLibraryAsyncMutexState *state);
void r_library_internal_async_mutex_unlock(RLibraryAsyncMutexState *state);
void r_library_internal_async_rwlock_release(RLibraryAsyncRwLockState *state);
void r_library_internal_async_rwlock_read_unlock(RLibraryAsyncRwLockState *state);
void r_library_internal_async_rwlock_write_unlock(RLibraryAsyncRwLockState *state);
void r_library_internal_async_semaphore_release(RLibraryAsyncSemaphoreState *state);
void r_library_internal_async_semaphore_return(RLibraryAsyncSemaphoreState *state);
void r_library_internal_async_notify_release(RLibraryAsyncNotifyState *state);
void r_library_internal_async_broadcast_release(RLibraryAsyncBroadcastState *state);
void r_library_internal_async_broadcast_unsubscribe(RLibraryAsyncBroadcastQueue *queue);

static inline void r_std_async_mutex_destroy(RStdAsyncMutex *value) {
    if (value->state != NULL) {
        r_library_internal_async_mutex_release(value->state);
        value->state = NULL;
    }
}

static inline void r_std_async_mutex_move_initialize(RStdAsyncMutex *destination,
                                                     RStdAsyncMutex *source) {
    destination->state = source->state;
    source->state = NULL;
}

static inline void r_std_async_mutex_guard_destroy(RStdAsyncMutexGuard *value) {
    if (value->state != NULL) {
        r_library_internal_async_mutex_unlock(value->state);
        value->state = NULL;
    }
}

static inline void r_std_async_mutex_guard_move_initialize(RStdAsyncMutexGuard *destination,
                                                           RStdAsyncMutexGuard *source) {
    destination->state = source->state;
    source->state = NULL;
}

static inline void r_std_async_rw_lock_destroy(RStdAsyncRwLock *value) {
    if (value->state != NULL) {
        r_library_internal_async_rwlock_release(value->state);
        value->state = NULL;
    }
}

static inline void r_std_async_rw_lock_move_initialize(RStdAsyncRwLock *destination,
                                                       RStdAsyncRwLock *source) {
    destination->state = source->state;
    source->state = NULL;
}

static inline void r_std_async_rw_read_guard_destroy(RStdAsyncRwReadGuard *value) {
    if (value->state != NULL) {
        r_library_internal_async_rwlock_read_unlock(value->state);
        value->state = NULL;
    }
}

static inline void r_std_async_rw_read_guard_move_initialize(RStdAsyncRwReadGuard *destination,
                                                             RStdAsyncRwReadGuard *source) {
    destination->state = source->state;
    source->state = NULL;
}

static inline void r_std_async_rw_write_guard_destroy(RStdAsyncRwWriteGuard *value) {
    if (value->state != NULL) {
        r_library_internal_async_rwlock_write_unlock(value->state);
        value->state = NULL;
    }
}

static inline void r_std_async_rw_write_guard_move_initialize(RStdAsyncRwWriteGuard *destination,
                                                              RStdAsyncRwWriteGuard *source) {
    destination->state = source->state;
    source->state = NULL;
}

static inline void r_std_async_semaphore_destroy(RStdAsyncSemaphore *value) {
    if (value->state != NULL) {
        r_library_internal_async_semaphore_release(value->state);
        value->state = NULL;
    }
}

static inline void r_std_async_semaphore_move_initialize(RStdAsyncSemaphore *destination,
                                                         RStdAsyncSemaphore *source) {
    destination->state = source->state;
    source->state = NULL;
}

static inline void r_std_async_semaphore_permit_destroy(RStdAsyncSemaphorePermit *value) {
    if (value->state != NULL) {
        r_library_internal_async_semaphore_return(value->state);
        value->state = NULL;
    }
}

static inline void
r_std_async_semaphore_permit_move_initialize(RStdAsyncSemaphorePermit *destination,
                                             RStdAsyncSemaphorePermit *source) {
    destination->state = source->state;
    source->state = NULL;
}

static inline void r_std_async_notify_destroy(RStdAsyncNotify *value) {
    if (value->state != NULL) {
        r_library_internal_async_notify_release(value->state);
        value->state = NULL;
    }
}

static inline void r_std_async_notify_move_initialize(RStdAsyncNotify *destination,
                                                      RStdAsyncNotify *source) {
    destination->state = source->state;
    source->state = NULL;
}

static inline void r_std_async_broadcast_destroy(RStdAsyncBroadcast *value) {
    if (value->state != NULL) {
        r_library_internal_async_broadcast_release(value->state);
        value->state = NULL;
    }
}

static inline void r_std_async_broadcast_move_initialize(RStdAsyncBroadcast *destination,
                                                         RStdAsyncBroadcast *source) {
    destination->state = source->state;
    source->state = NULL;
}

static inline void r_std_async_broadcast_receiver_destroy(RStdAsyncBroadcastReceiver *value) {
    if (value->queue != NULL) {
        r_library_internal_async_broadcast_unsubscribe(value->queue);
        value->queue = NULL;
    }
}

static inline void
r_std_async_broadcast_receiver_move_initialize(RStdAsyncBroadcastReceiver *destination,
                                               RStdAsyncBroadcastReceiver *source) {
    destination->queue = source->queue;
    source->queue = NULL;
}

#endif

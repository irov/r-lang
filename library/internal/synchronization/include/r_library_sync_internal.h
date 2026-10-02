#ifndef R_LIBRARY_SYNC_INTERNAL_H
#define R_LIBRARY_SYNC_INTERNAL_H

#include "r_std_sync.h"

typedef enum RLibrarySyncAcquireKind {
    R_LIBRARY_SYNC_ACQUIRE_LOCKED = 0,
    R_LIBRARY_SYNC_ACQUIRE_POISONED = 1,
    R_LIBRARY_SYNC_ACQUIRE_WOULD_DEADLOCK = 2,
    R_LIBRARY_SYNC_ACQUIRE_WOULD_BLOCK = 3
} RLibrarySyncAcquireKind;

typedef struct RLibrarySyncAcquireResult {
    RLibrarySyncAcquireKind kind;
    RStdSyncMutexGuard guard;
} RLibrarySyncAcquireResult;

void r_library_internal_sync_mutex_initialize(RStdSyncMutex *result,
                                              void *value_storage,
                                              RRuntimeTypeInfo value_type,
                                              void *staged_value);
RLibrarySyncAcquireResult r_library_internal_sync_mutex_acquire(const RStdSyncMutex *mutex,
                                                                _Bool blocking);
void r_library_internal_sync_mutex_unlock(RStdSyncMutexGuard *guard, _Bool panicking);
const void *r_library_internal_sync_mutex_guard_ref(const RStdSyncMutexGuard *guard);
void *r_library_internal_sync_mutex_guard_mut(RStdSyncMutexGuard *guard);

void r_library_internal_sync_rw_lock_initialize(RStdSyncRwLock *result,
                                                void *value_storage,
                                                RRuntimeTypeInfo value_type,
                                                void *staged_value);
RLibrarySyncAcquireKind r_library_internal_sync_rw_read_acquire(const RStdSyncRwLock *lock,
                                                                _Bool blocking,
                                                                RStdSyncRwReadGuard *guard);
RLibrarySyncAcquireKind r_library_internal_sync_rw_write_acquire(const RStdSyncRwLock *lock,
                                                                 _Bool blocking,
                                                                 RStdSyncRwWriteGuard *guard);
void r_library_internal_sync_rw_read_unlock(RStdSyncRwReadGuard *guard);
void r_library_internal_sync_rw_write_unlock(RStdSyncRwWriteGuard *guard, _Bool panicking);
const void *r_library_internal_sync_rw_read_guard_ref(const RStdSyncRwReadGuard *guard);
const void *r_library_internal_sync_rw_write_guard_ref(const RStdSyncRwWriteGuard *guard);
void *r_library_internal_sync_rw_write_guard_mut(RStdSyncRwWriteGuard *guard);
_Noreturn void r_library_internal_sync_contract_violation(void);
uintptr_t r_library_internal_sync_current_thread_token(void);
void r_library_internal_sync_move_initialize(RRuntimeTypeInfo type,
                                             void *destination,
                                             void *source);

void r_library_internal_sync_condvar_initialize(RStdSyncCondvar *result);
RLibrarySyncAcquireResult r_library_internal_sync_condvar_wait(const RStdSyncCondvar *condition,
                                                               RStdSyncMutexGuard *guard);
void r_library_internal_sync_condvar_notify(const RStdSyncCondvar *condition, _Bool notify_all);

void r_library_internal_sync_once_initialize(RStdSyncOnce *result);
_Bool r_library_internal_sync_once_call(const RStdSyncOnce *once,
                                        RStdSyncOnceInitializer initializer,
                                        void *context,
                                        _Bool force);

void r_library_internal_sync_once_lock_initialize(RStdSyncOnceLock *result,
                                                  void *value_storage,
                                                  RRuntimeTypeInfo value_type);
const void *r_library_internal_sync_once_lock_get(const RStdSyncOnceLock *lock);
const void *r_library_internal_sync_once_lock_get_or_init(const RStdSyncOnceLock *lock,
                                                          RStdSyncOnceLockInitializer initializer,
                                                          void *context);
RStdSyncSetResult r_library_internal_sync_once_lock_set(const RStdSyncOnceLock *lock,
                                                        void *staged_value);

void r_library_internal_sync_barrier_move(RStdSyncBarrier *destination, RStdSyncBarrier *source);
void r_library_internal_sync_barrier_destroy(RStdSyncBarrier *barrier);

RStdSyncChannelCreateResult r_library_internal_sync_channel_create(RRuntimeAllocator *allocator,
                                                                   RRuntimeTypeInfo element);
RStdSyncSyncChannelCreateResult r_library_internal_sync_sync_channel_create(
    RRuntimeAllocator *allocator, RRuntimeTypeInfo element, size_t capacity);
RStdSyncSender r_library_internal_sync_channel_sender(const RLibrarySyncChannelState *state);
RStdSyncSyncSender
r_library_internal_sync_channel_sync_sender(const RLibrarySyncChannelState *state);
RStdSyncReceiver r_library_internal_sync_channel_receiver(RLibrarySyncChannelState **factory_state,
                                                          _Bool bounded);
RStdSyncSender r_library_internal_sync_channel_clone_sender(const RLibrarySyncChannelState *state);
RStdSyncSyncSender
r_library_internal_sync_channel_clone_sync_sender(const RLibrarySyncChannelState *state);
RStdSyncSendResult r_library_internal_sync_channel_send(RLibrarySyncChannelState *state,
                                                        void *staged_value,
                                                        _Bool bounded);
RStdSyncTrySendResult r_library_internal_sync_channel_try_send(RLibrarySyncChannelState *state,
                                                               void *staged_value);
RStdSyncRecvResult r_library_internal_sync_channel_recv(RLibrarySyncChannelState *state,
                                                        void *result_storage,
                                                        _Bool blocking);
RStdSyncTryRecvResult r_library_internal_sync_channel_try_recv(RLibrarySyncChannelState *state,
                                                               void *result_storage);

/*
 * An asynchronous receiver waiting on a channel (std.sync::receive). The channel links a waiter
 * only while no value is queued. Under the channel lock it calls select before it moves a value
 * into value or reports disconnection; a waiter whose select fails is unlinked and left to its
 * cancellation. After the lock is released the channel calls finish once for every waiter whose
 * select succeeded; received tells whether a value was moved.
 */
typedef struct RLibrarySyncChannelWaiter RLibrarySyncChannelWaiter;
struct RLibrarySyncChannelWaiter {
    RLibrarySyncChannelWaiter *next;
    RLibrarySyncChannelWaiter *previous;
    RLibrarySyncChannelWaiter *finished_next;
    _Bool (*select)(RLibrarySyncChannelWaiter *waiter);
    void (*finish)(RLibrarySyncChannelWaiter *waiter);
    void *value;
    _Bool linked;
    _Bool received;
};

typedef enum RLibrarySyncReceiveBegin {
    /* Linked: a later send or the disconnection finishes the waiter. */
    R_LIBRARY_SYNC_RECEIVE_PENDING = 0,
    /* Selected at once; the caller finishes the waiter itself. */
    R_LIBRARY_SYNC_RECEIVE_FINISHED,
    /* select failed because cancellation won; nothing was moved. */
    R_LIBRARY_SYNC_RECEIVE_REJECTED
} RLibrarySyncReceiveBegin;

void r_library_internal_sync_channel_retain(RLibrarySyncChannelState *state);
void r_library_internal_sync_channel_release(RLibrarySyncChannelState *state);
RLibrarySyncReceiveBegin
r_library_internal_sync_channel_receive_begin(RLibrarySyncChannelState *state,
                                              RLibrarySyncChannelWaiter *waiter);
void r_library_internal_sync_channel_receive_withdraw(RLibrarySyncChannelState *state,
                                                      RLibrarySyncChannelWaiter *waiter);

/*
 * Reservations (std.sync::reserve) use the waiter protocol above: a selected reservation has a
 * permit moved into value and received set; a disconnected one has received clear.
 */
RLibrarySyncReceiveBegin
r_library_internal_sync_channel_reserve_begin(RLibrarySyncChannelState *state,
                                              RLibrarySyncChannelWaiter *waiter);
void r_library_internal_sync_channel_reserve_withdraw(RLibrarySyncChannelState *state,
                                                      RLibrarySyncChannelWaiter *waiter);
RStdSyncTryReserveResult
r_library_internal_sync_channel_try_reserve(RLibrarySyncChannelState *state);
void r_library_internal_sync_permit_send(RStdSyncPermit *permit, void *staged_value);

#endif

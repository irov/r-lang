#ifndef R_STD_SYNC_H
#define R_STD_SYNC_H

#include "r_runtime_task.h"
#include "r_runtime_type.h"
#include "r_std_alloc.h"
#include "r_std_async.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Compiler wrapper contract:
 *
 * - mutex(T) is represented by one RStdSyncMutex descriptor and compiler-owned aligned T storage;
 * - mutex_new initializes both through an out parameter, so no active atomic object is copied by
 *   a C return or assignment;
 * - generated Move glue calls r_library_internal_sync_mutex_move and never byte-copies a mutex;
 * - generated drop glue calls r_library_internal_sync_mutex_destroy exactly once for a live mutex;
 * - guard and result wrappers preserve the hidden creating-lock region required by R-LIB-0014;
 * - rw_lock acquisitions initialize their final result storage in place because a live read guard
 *   is an intrusive allocation-free reader record and shall never be byte-copied;
 * - generated structural move/drop glue for every rw_lock result dispatches to the active guard's
 *   move/drop glue; moving a result by C assignment or memcpy is a runtime-contract violation;
 * - mutex(T)/rw_lock(T), result and guard Send/Sync capabilities are derived structurally by the
 *   compiler from T exactly as required by R-LIB-0017; no runtime bit can elevate them.
 */

typedef enum RStdSyncGuardKind {
    R_STD_SYNC_GUARD_NONE = 0,
    R_STD_SYNC_GUARD_MUTEX = 1,
    R_STD_SYNC_GUARD_RW_READ = 2,
    R_STD_SYNC_GUARD_RW_WRITE = 3
} RStdSyncGuardKind;

typedef struct RStdSyncGuardHeader {
    RStdSyncGuardKind kind;
    _Bool active;
} RStdSyncGuardHeader;

typedef struct RStdSyncMutex {
    _Atomic unsigned int state;
    _Atomic uintptr_t owner_token;
    RRuntimeTypeInfo value_type;
    void *value;
    _Bool initialized;
} RStdSyncMutex;

typedef struct RStdSyncMutexGuard {
    RStdSyncGuardHeader header;
    RStdSyncMutex *mutex;
    uintptr_t owner_token;
} RStdSyncMutexGuard;

typedef enum RStdSyncLockResultKind {
    R_STD_SYNC_LOCK_RESULT_LOCKED = 0,
    R_STD_SYNC_LOCK_RESULT_POISONED = 1,
    R_STD_SYNC_LOCK_RESULT_WOULD_DEADLOCK = 2
} RStdSyncLockResultKind;

typedef struct RStdSyncLockResult {
    RStdSyncLockResultKind kind;
    RStdSyncMutexGuard guard;
} RStdSyncLockResult;

typedef enum RStdSyncTryLockResultKind {
    R_STD_SYNC_TRY_LOCK_RESULT_LOCKED = 0,
    R_STD_SYNC_TRY_LOCK_RESULT_POISONED = 1,
    R_STD_SYNC_TRY_LOCK_RESULT_WOULD_DEADLOCK = 2,
    R_STD_SYNC_TRY_LOCK_RESULT_WOULD_BLOCK = 3
} RStdSyncTryLockResultKind;

typedef struct RStdSyncTryLockResult {
    RStdSyncTryLockResultKind kind;
    RStdSyncMutexGuard guard;
} RStdSyncTryLockResult;

typedef struct RStdSyncRwLock RStdSyncRwLock;
typedef struct RStdSyncRwReadGuard RStdSyncRwReadGuard;

struct RStdSyncRwLock {
    _Atomic unsigned int state;
    uintptr_t writer_token;
    RStdSyncRwReadGuard *reader_head;
    size_t reader_count;
    RRuntimeTypeInfo value_type;
    void *value;
    _Bool initialized;
};

struct RStdSyncRwReadGuard {
    RStdSyncGuardHeader header;
    RStdSyncRwLock *lock;
    uintptr_t owner_token;
    RStdSyncRwReadGuard *previous;
    RStdSyncRwReadGuard *next;
};

typedef struct RStdSyncRwWriteGuard {
    RStdSyncGuardHeader header;
    RStdSyncRwLock *lock;
    uintptr_t owner_token;
} RStdSyncRwWriteGuard;

_Static_assert(offsetof(RStdSyncMutexGuard, header) == 0U,
               "mutex guard header must lead the generic unlock ABI");
_Static_assert(offsetof(RStdSyncRwReadGuard, header) == 0U,
               "rw read guard header must lead the generic unlock ABI");
_Static_assert(offsetof(RStdSyncRwWriteGuard, header) == 0U,
               "rw write guard header must lead the generic unlock ABI");

typedef enum RStdSyncReadLockResultKind {
    R_STD_SYNC_READ_LOCK_RESULT_LOCKED = 0,
    R_STD_SYNC_READ_LOCK_RESULT_POISONED = 1,
    R_STD_SYNC_READ_LOCK_RESULT_WOULD_DEADLOCK = 2
} RStdSyncReadLockResultKind;

typedef struct RStdSyncReadLockResult {
    RStdSyncReadLockResultKind kind;
    RStdSyncRwReadGuard guard;
} RStdSyncReadLockResult;

typedef enum RStdSyncTryReadLockResultKind {
    R_STD_SYNC_TRY_READ_LOCK_RESULT_LOCKED = 0,
    R_STD_SYNC_TRY_READ_LOCK_RESULT_POISONED = 1,
    R_STD_SYNC_TRY_READ_LOCK_RESULT_WOULD_DEADLOCK = 2,
    R_STD_SYNC_TRY_READ_LOCK_RESULT_WOULD_BLOCK = 3
} RStdSyncTryReadLockResultKind;

typedef struct RStdSyncTryReadLockResult {
    RStdSyncTryReadLockResultKind kind;
    RStdSyncRwReadGuard guard;
} RStdSyncTryReadLockResult;

typedef enum RStdSyncWriteLockResultKind {
    R_STD_SYNC_WRITE_LOCK_RESULT_LOCKED = 0,
    R_STD_SYNC_WRITE_LOCK_RESULT_POISONED = 1,
    R_STD_SYNC_WRITE_LOCK_RESULT_WOULD_DEADLOCK = 2
} RStdSyncWriteLockResultKind;

typedef struct RStdSyncWriteLockResult {
    RStdSyncWriteLockResultKind kind;
    RStdSyncRwWriteGuard guard;
} RStdSyncWriteLockResult;

typedef enum RStdSyncTryWriteLockResultKind {
    R_STD_SYNC_TRY_WRITE_LOCK_RESULT_LOCKED = 0,
    R_STD_SYNC_TRY_WRITE_LOCK_RESULT_POISONED = 1,
    R_STD_SYNC_TRY_WRITE_LOCK_RESULT_WOULD_DEADLOCK = 2,
    R_STD_SYNC_TRY_WRITE_LOCK_RESULT_WOULD_BLOCK = 3
} RStdSyncTryWriteLockResultKind;

typedef struct RStdSyncTryWriteLockResult {
    RStdSyncTryWriteLockResultKind kind;
    RStdSyncRwWriteGuard guard;
} RStdSyncTryWriteLockResult;

typedef struct RStdSyncCondvar {
    _Atomic unsigned int gate;
    _Atomic uint64_t wake_through;
    uint64_t next_ticket;
    size_t waiter_count;
    _Bool initialized;
} RStdSyncCondvar;

typedef struct RStdSyncOnce {
    _Atomic unsigned int state;
    _Atomic uintptr_t owner_token;
    _Bool initialized;
} RStdSyncOnce;

/*
 * The callback returns true only after its logical `void` result has completed successfully.
 * False represents a compiler-owned checked-error payload retained in context; the once remains
 * unpublished and a later call may retry. The runtime never inspects context.
 */
typedef _Bool (*RStdSyncOnceInitializer)(void *context);

typedef struct RStdSyncOnceLock {
    _Atomic unsigned int state;
    _Atomic uintptr_t owner_token;
    RRuntimeTypeInfo value_type;
    void *value;
    _Bool initialized;
    _Bool value_initialized;
} RStdSyncOnceLock;

/*
 * The callback returns true only after result is fully initialized. False leaves result
 * uninitialized and represents a compiler-owned checked-error payload retained in context.
 */
typedef _Bool (*RStdSyncOnceLockInitializer)(void *result, void *context);

typedef enum RStdSyncSetResult {
    R_STD_SYNC_SET_RESULT_STORED = 0,
    R_STD_SYNC_SET_RESULT_OCCUPIED = 1
} RStdSyncSetResult;

typedef struct RStdSyncBarrier {
    _Atomic size_t arrived;
    _Atomic size_t generation;
    size_t participants;
    _Bool initialized;
} RStdSyncBarrier;

typedef enum RStdSyncBarrierError {
    R_STD_SYNC_BARRIER_ERROR_ZERO_PARTICIPANTS = 0
} RStdSyncBarrierError;

typedef enum RStdSyncBarrierCreateStatus {
    R_STD_SYNC_BARRIER_CREATE_SUCCESS = 0,
    R_STD_SYNC_BARRIER_CREATE_ERROR = 1,
    R_STD_SYNC_BARRIER_CREATE_CONTRACT_VIOLATION = 2
} RStdSyncBarrierCreateStatus;

typedef struct RStdSyncBarrierCreateResult {
    RStdSyncBarrierCreateStatus status;
    RStdSyncBarrierError error;
} RStdSyncBarrierCreateResult;

typedef enum RStdSyncBarrierWaitResult {
    R_STD_SYNC_BARRIER_WAIT_LEADER = 0,
    R_STD_SYNC_BARRIER_WAIT_FOLLOWER = 1
} RStdSyncBarrierWaitResult;

typedef struct RLibrarySyncChannelState RLibrarySyncChannelState;

typedef struct RStdSyncChannel {
    RLibrarySyncChannelState *state;
} RStdSyncChannel;

typedef struct RStdSyncSyncChannel {
    RLibrarySyncChannelState *state;
} RStdSyncSyncChannel;

typedef struct RStdSyncSender {
    RLibrarySyncChannelState *state;
} RStdSyncSender;

typedef struct RStdSyncSyncSender {
    RLibrarySyncChannelState *state;
} RStdSyncSyncSender;

typedef struct RStdSyncReceiver {
    RLibrarySyncChannelState *state;
} RStdSyncReceiver;

typedef enum RStdSyncChannelCallStatus {
    R_STD_SYNC_CHANNEL_CALL_SUCCESS = 0,
    R_STD_SYNC_CHANNEL_CALL_ALLOCATION_ERROR = 1,
    R_STD_SYNC_CHANNEL_CALL_CONTRACT_VIOLATION = 2
} RStdSyncChannelCallStatus;

typedef struct RStdSyncChannelCreateResult {
    RStdSyncChannelCallStatus status;
    RStdAllocError error;
    RStdSyncChannel value;
} RStdSyncChannelCreateResult;

typedef struct RStdSyncSyncChannelCreateResult {
    RStdSyncChannelCallStatus status;
    RStdAllocError error;
    RStdSyncSyncChannel value;
} RStdSyncSyncChannelCreateResult;

typedef enum RStdSyncSendResultKind {
    R_STD_SYNC_SEND_RESULT_SENT = 0,
    R_STD_SYNC_SEND_RESULT_DISCONNECTED = 1,
    R_STD_SYNC_SEND_RESULT_ALLOCATION_FAILED = 2
} RStdSyncSendResultKind;

typedef struct RStdSyncSendResult {
    RStdSyncSendResultKind kind;
} RStdSyncSendResult;

typedef enum RStdSyncTrySendResultKind {
    R_STD_SYNC_TRY_SEND_RESULT_SENT = 0,
    R_STD_SYNC_TRY_SEND_RESULT_FULL = 1,
    R_STD_SYNC_TRY_SEND_RESULT_DISCONNECTED = 2
} RStdSyncTrySendResultKind;

typedef struct RStdSyncTrySendResult {
    RStdSyncTrySendResultKind kind;
} RStdSyncTrySendResult;

typedef enum RStdSyncRecvResultKind {
    R_STD_SYNC_RECV_RESULT_RECEIVED = 0,
    R_STD_SYNC_RECV_RESULT_DISCONNECTED = 1
} RStdSyncRecvResultKind;

typedef struct RStdSyncRecvResult {
    RStdSyncRecvResultKind kind;
} RStdSyncRecvResult;

typedef enum RStdSyncTryRecvResultKind {
    R_STD_SYNC_TRY_RECV_RESULT_RECEIVED = 0,
    R_STD_SYNC_TRY_RECV_RESULT_EMPTY = 1,
    R_STD_SYNC_TRY_RECV_RESULT_DISCONNECTED = 2
} RStdSyncTryRecvResultKind;

typedef struct RStdSyncTryRecvResult {
    RStdSyncTryRecvResultKind kind;
} RStdSyncTryRecvResult;

/*
 * Compiler wrapper contract for R-LIB-0016/R-MEM-0024..0025:
 *
 * - the factory owns the sole receiver-creation right and is neither Send nor Sync;
 * - sender endpoints are explicit independently retained owners and the receiver is unique;
 * - staged_value remains initialized for every non-sent outcome and is consumed exactly once
 *   only at the successful-send linearization point;
 * - result_storage is uninitialized aligned T storage and is initialized exactly for RECEIVED;
 * - generated wrappers build the payload-bearing R variants from these type-erased statuses;
 * - generated Move/drop glue uses the channel move/destroy functions declared below and never
 *   copies an endpoint or factory descriptor byte-for-byte.
 */
RStdSyncChannelCreateResult r_std_sync_channel(RRuntimeAllocator *allocator,
                                               RRuntimeTypeInfo element);
RStdSyncSyncChannelCreateResult
r_std_sync_sync_channel(RRuntimeAllocator *allocator, RRuntimeTypeInfo element, size_t capacity);
RStdSyncSender r_std_sync_sender(const RStdSyncChannel *factory);
RStdSyncSyncSender r_std_sync_sync_sender(const RStdSyncSyncChannel *factory);
RStdSyncReceiver r_std_sync_receiver(RStdSyncChannel *factory);
RStdSyncReceiver r_std_sync_sync_receiver(RStdSyncSyncChannel *factory);
RStdSyncSender r_std_sync_clone_sender(const RStdSyncSender *source);
RStdSyncSyncSender r_std_sync_clone_sync_sender(const RStdSyncSyncSender *source);
RStdSyncSendResult r_std_sync_send(const RStdSyncSender *endpoint, void *staged_value);
RStdSyncSendResult r_std_sync_sync_send(const RStdSyncSyncSender *endpoint, void *staged_value);
RStdSyncTrySendResult r_std_sync_try_send(const RStdSyncSyncSender *endpoint, void *staged_value);
RStdSyncRecvResult r_std_sync_recv(const RStdSyncReceiver *endpoint, void *result_storage);

/*
 * std.sync::receive (R-LIB-0016): the generated o<T> result layout. result describes o<T>; its
 * uint32_t tag at tag_offset is 0 for none and 1 for some, and the T payload is at value_offset.
 */
typedef struct RStdSyncReceiveLayout {
    RRuntimeTypeInfo result;
    size_t tag_offset;
    size_t value_offset;
} RStdSyncReceiveLayout;

typedef struct RStdSyncReceiveStartResult {
    _Bool is_ok;
    RRuntimeTask *task;
    RStdAsyncStartError error;
} RStdSyncReceiveStartResult;

/*
 * Ownership: borrows the receiver only for the call and retains its channel until the task is
 * terminal. The task completes with the oldest queued or next sent value, or with none once
 * every sender is gone and the queue is empty. Pending receives are served in start order;
 * cancelling a pending one leaves the value queued, whereas one selected before its cancellation
 * has taken its value, which is destroyed with its unobserved result.
 */
RStdSyncReceiveStartResult r_std_sync_receive(const RStdSyncReceiver *endpoint,
                                              RStdSyncReceiveLayout layout);
RStdSyncTryRecvResult r_std_sync_try_recv(const RStdSyncReceiver *endpoint, void *result_storage);

/*
 * std.sync::permit<T> (R-LIB-0016, L30): one reserved slot of a bounded channel. A permit counts as
 * a sender endpoint until it sends or is destroyed; destruction returns its slot.
 */
typedef struct RStdSyncPermit {
    RLibrarySyncChannelState *state;
} RStdSyncPermit;

/*
 * std.sync::reserve: the generated reserve_result<T> layout. result describes the whole value;
 * its uint32_t tag at tag_offset is 0 for reserved and 1 for disconnected, and the permit payload
 * of reserved is at value_offset.
 */
typedef struct RStdSyncReserveLayout {
    RRuntimeTypeInfo result;
    size_t tag_offset;
    size_t value_offset;
} RStdSyncReserveLayout;

typedef enum RStdSyncReserveResultKind {
    R_STD_SYNC_RESERVE_RESULT_RESERVED = 0,
    R_STD_SYNC_RESERVE_RESULT_DISCONNECTED = 1
} RStdSyncReserveResultKind;

/* The alternatives of std.sync::reserve_result<T>; the compiler lays out the payload. */
typedef struct RStdSyncReserveResult {
    RStdSyncReserveResultKind kind;
    RStdSyncPermit permit;
} RStdSyncReserveResult;

typedef enum RStdSyncTryReserveResultKind {
    R_STD_SYNC_TRY_RESERVE_RESULT_RESERVED = 0,
    R_STD_SYNC_TRY_RESERVE_RESULT_FULL = 1,
    R_STD_SYNC_TRY_RESERVE_RESULT_DISCONNECTED = 2
} RStdSyncTryReserveResultKind;

typedef struct RStdSyncTryReserveResult {
    RStdSyncTryReserveResultKind kind;
    RStdSyncPermit permit;
} RStdSyncTryReserveResult;

/*
 * Ownership: borrows the sender only for the call and retains its channel until the task is
 * terminal. The task completes with a permit once a slot is neither queued nor reserved, or with
 * disconnected once the receiver is gone. Reservations are served in start order, and cancelling
 * one never loses a slot. A channel of capacity zero has no slot and causes contract_violation.
 */
RStdAsyncStartResult r_std_sync_reserve(const RStdSyncSyncSender *endpoint,
                                        RStdSyncReserveLayout layout);
RStdSyncTryReserveResult r_std_sync_try_reserve(const RStdSyncSyncSender *endpoint);

/*
 * Ownership: consumes permit and staged_value. The value goes to the oldest waiting receiver or
 * into the reserved slot; after the receiver is gone it is destroyed.
 */
void r_std_sync_send_permit(RStdSyncPermit *permit, void *staged_value);

/*
 * Ownership: result and value_storage are uninitialized compiler-owned storage. staged_value is
 * one initialized T. This allocation-free operation always move-initializes value_storage,
 * consumes staged_value, and initializes the sole mutex owner.
 */
void r_std_sync_mutex_new(RStdSyncMutex *result,
                          void *value_storage,
                          RRuntimeTypeInfo value_type,
                          void *staged_value);

/*
 * Ownership: mutex is a shared call-bounded borrow. An acquired outcome owns one non-Send guard;
 * guardless outcomes retain no lock identity or value borrow.
 */
RStdSyncLockResult r_std_sync_lock(const RStdSyncMutex *mutex);
RStdSyncTryLockResult r_std_sync_try_lock(const RStdSyncMutex *mutex);

/*
 * Ownership: guard is one of the three standardized guard types. It is consumed and releases its
 * creating lock exactly once. The compiler type-checks G before calling this type-erased C bridge.
 */
void r_std_sync_unlock(void *guard);

/*
 * Ownership: guard is borrowed. The returned pointer has the intersection of that borrow and the
 * hidden creating-mutex region; it is never retained by the runtime.
 */
const void *r_std_sync_mutex_guard_ref(const RStdSyncMutexGuard *guard);
void *r_std_sync_mutex_guard_mut(RStdSyncMutexGuard *guard);

/*
 * Ownership: result and value_storage are uninitialized compiler-owned storage. staged_value is
 * one initialized T. This allocation-free operation move-initializes T and consumes staged_value.
 */
void r_std_sync_rwlock_new(RStdSyncRwLock *result,
                           void *value_storage,
                           RRuntimeTypeInfo value_type,
                           void *staged_value);

/*
 * Ownership: result is uninitialized final storage and lock is a shared call-bounded borrow.
 * Acquired results own one non-Send guard tied to the hidden creating-lock region. These operations
 * allocate nothing; the final read guard storage is its intrusive ownership record.
 */
void r_std_sync_read(RStdSyncReadLockResult *result, const RStdSyncRwLock *lock);
void r_std_sync_try_read(RStdSyncTryReadLockResult *result, const RStdSyncRwLock *lock);
void r_std_sync_write(RStdSyncWriteLockResult *result, const RStdSyncRwLock *lock);
void r_std_sync_try_write(RStdSyncTryWriteLockResult *result, const RStdSyncRwLock *lock);

/* Ownership: guard is borrowed; the returned borrow is also bounded by its hidden lock region. */
const void *r_std_sync_rw_read_guard_ref(const RStdSyncRwReadGuard *guard);
const void *r_std_sync_rw_write_guard_ref(const RStdSyncRwWriteGuard *guard);
void *r_std_sync_rw_write_guard_mut(RStdSyncRwWriteGuard *guard);

/* Ownership: result is uninitialized compiler-owned storage. This operation allocates nothing. */
void r_std_sync_condvar_new(RStdSyncCondvar *result);

/*
 * Ownership: condition is a shared call-bounded borrow. guard is consumed, its mutex is released
 * after the wait is registered, and the returned result owns the newly acquired guard.
 */
RStdSyncLockResult r_std_sync_wait(const RStdSyncCondvar *condition, RStdSyncMutexGuard *guard);

/* Ownership: condition is a shared call-bounded borrow and is never retained. */
void r_std_sync_notify_one(const RStdSyncCondvar *condition);
void r_std_sync_notify_all(const RStdSyncCondvar *condition);

/* Ownership: result is uninitialized compiler-owned storage. This operation allocates nothing. */
void r_std_sync_once_new(RStdSyncOnce *result);

/*
 * Ownership: once is a shared call-bounded borrow. initializer is a direct safe function
 * designator and is neither stored nor retained after this call.
 */
/*
 * Returns true when initialization was already published or this attempt published it. False
 * preserves a checked failure in callback context and leaves the once retryable without poison.
 */
_Bool r_std_sync_call_once(const RStdSyncOnce *once,
                           RStdSyncOnceInitializer initializer,
                           void *context);
_Bool r_std_sync_call_once_force(const RStdSyncOnce *once,
                                 RStdSyncOnceInitializer initializer,
                                 void *context);

/*
 * Compiler type-constructor ABI: result and value_storage are uninitialized aligned storage.
 * The empty once_lock retains only type metadata and allocates nothing.
 */
void r_std_sync_once_lock(RStdSyncOnceLock *result,
                          void *value_storage,
                          RRuntimeTypeInfo value_type);

/* Ownership: lock is a shared call-bounded borrow; returned borrows share its hidden region. */
const void *r_std_sync_get(const RStdSyncOnceLock *lock);
const void *r_std_sync_get_or_init(const RStdSyncOnceLock *lock,
                                   RStdSyncOnceLockInitializer initializer,
                                   void *context);

/*
 * Ownership: staged_value remains initialized for OCCUPIED and is consumed only for STORED. The
 * compiler wrapper moves the still-live staged value into the occupied(T) result variant.
 */
RStdSyncSetResult r_std_sync_set(const RStdSyncOnceLock *lock, void *staged_value);

/*
 * Ownership: result is uninitialized compiler-owned storage. Success initializes its sole
 * Move-only barrier owner without allocation. Zero count leaves result uninitialized.
 */
RStdSyncBarrierCreateResult r_std_sync_barrier_new(RStdSyncBarrier *result, size_t count);

/* Ownership: barrier is a shared call-bounded borrow and is never retained. */
RStdSyncBarrierWaitResult r_std_sync_barrier_wait(const RStdSyncBarrier *barrier);

/* Private generated-glue ABI; these declarations are not public R operations. */
void r_library_internal_sync_mutex_move(RStdSyncMutex *destination,
                                        void *destination_storage,
                                        RStdSyncMutex *source);
void r_library_internal_sync_mutex_destroy(RStdSyncMutex *mutex);
void r_library_internal_sync_mutex_guard_move(RStdSyncMutexGuard *destination,
                                              RStdSyncMutexGuard *source);
void r_library_internal_sync_mutex_guard_destroy(RStdSyncMutexGuard *guard, _Bool panicking);
void r_library_internal_sync_rw_lock_move(RStdSyncRwLock *destination,
                                          void *destination_storage,
                                          RStdSyncRwLock *source);
void r_library_internal_sync_rw_lock_destroy(RStdSyncRwLock *lock);
void r_library_internal_sync_rw_read_guard_move(RStdSyncRwReadGuard *destination,
                                                RStdSyncRwReadGuard *source);
void r_library_internal_sync_rw_read_guard_destroy(RStdSyncRwReadGuard *guard, _Bool panicking);
void r_library_internal_sync_rw_write_guard_move(RStdSyncRwWriteGuard *destination,
                                                 RStdSyncRwWriteGuard *source);
void r_library_internal_sync_rw_write_guard_destroy(RStdSyncRwWriteGuard *guard, _Bool panicking);
void r_library_internal_sync_condvar_move(RStdSyncCondvar *destination, RStdSyncCondvar *source);
void r_library_internal_sync_condvar_destroy(RStdSyncCondvar *condition);
void r_library_internal_sync_once_move(RStdSyncOnce *destination, RStdSyncOnce *source);
void r_library_internal_sync_once_destroy(RStdSyncOnce *once);
void r_library_internal_sync_once_lock_move(RStdSyncOnceLock *destination,
                                            void *destination_storage,
                                            RStdSyncOnceLock *source);
void r_library_internal_sync_once_lock_destroy(RStdSyncOnceLock *lock);
void r_library_internal_sync_barrier_move(RStdSyncBarrier *destination, RStdSyncBarrier *source);
void r_library_internal_sync_barrier_destroy(RStdSyncBarrier *barrier);
void r_library_internal_sync_channel_move(RStdSyncChannel *destination, RStdSyncChannel *source);
void r_library_internal_sync_channel_destroy(RStdSyncChannel *factory);
void r_library_internal_sync_sync_channel_move(RStdSyncSyncChannel *destination,
                                               RStdSyncSyncChannel *source);
void r_library_internal_sync_sync_channel_destroy(RStdSyncSyncChannel *factory);
void r_library_internal_sync_sender_move(RStdSyncSender *destination, RStdSyncSender *source);
void r_library_internal_sync_sender_destroy(RStdSyncSender *sender);
void r_library_internal_sync_sync_sender_move(RStdSyncSyncSender *destination,
                                              RStdSyncSyncSender *source);
void r_library_internal_sync_sync_sender_destroy(RStdSyncSyncSender *sender);
void r_library_internal_sync_receiver_move(RStdSyncReceiver *destination, RStdSyncReceiver *source);
void r_library_internal_sync_receiver_destroy(RStdSyncReceiver *receiver);
void r_library_internal_sync_permit_move(RStdSyncPermit *destination, RStdSyncPermit *source);
void r_library_internal_sync_permit_destroy(RStdSyncPermit *permit);

static inline void r_std_sync_mutex_destroy(RStdSyncMutex *mutex) {
    r_library_internal_sync_mutex_destroy(mutex);
}

static inline void r_std_sync_mutex_guard_destroy(RStdSyncMutexGuard *guard) {
    r_library_internal_sync_mutex_guard_destroy(guard, 0);
}

static inline void r_std_sync_rw_lock_destroy(RStdSyncRwLock *lock) {
    r_library_internal_sync_rw_lock_destroy(lock);
}

static inline void r_std_sync_rw_read_guard_destroy(RStdSyncRwReadGuard *guard) {
    r_library_internal_sync_rw_read_guard_destroy(guard, 0);
}

static inline void r_std_sync_rw_write_guard_destroy(RStdSyncRwWriteGuard *guard) {
    r_library_internal_sync_rw_write_guard_destroy(guard, 0);
}

static inline void r_std_sync_condvar_destroy(RStdSyncCondvar *condition) {
    r_library_internal_sync_condvar_destroy(condition);
}

static inline void r_std_sync_condvar_move_initialize(RStdSyncCondvar *destination,
                                                      RStdSyncCondvar *source) {
    r_library_internal_sync_condvar_move(destination, source);
}

static inline void r_std_sync_once_destroy(RStdSyncOnce *once) {
    r_library_internal_sync_once_destroy(once);
}

static inline void r_std_sync_once_move_initialize(RStdSyncOnce *destination,
                                                   RStdSyncOnce *source) {
    r_library_internal_sync_once_move(destination, source);
}

static inline void r_std_sync_once_lock_destroy(RStdSyncOnceLock *lock) {
    r_library_internal_sync_once_lock_destroy(lock);
}

static inline void r_std_sync_barrier_destroy(RStdSyncBarrier *barrier) {
    r_library_internal_sync_barrier_destroy(barrier);
}

static inline void r_std_sync_barrier_move_initialize(RStdSyncBarrier *destination,
                                                      RStdSyncBarrier *source) {
    r_library_internal_sync_barrier_move(destination, source);
}

static inline void r_std_sync_channel_destroy(RStdSyncChannel *factory) {
    r_library_internal_sync_channel_destroy(factory);
}

static inline void r_std_sync_channel_move_initialize(RStdSyncChannel *destination,
                                                      RStdSyncChannel *source) {
    r_library_internal_sync_channel_move(destination, source);
}

static inline void r_std_sync_sync_channel_destroy(RStdSyncSyncChannel *factory) {
    r_library_internal_sync_sync_channel_destroy(factory);
}

static inline void r_std_sync_sync_channel_move_initialize(RStdSyncSyncChannel *destination,
                                                           RStdSyncSyncChannel *source) {
    r_library_internal_sync_sync_channel_move(destination, source);
}

static inline void r_std_sync_sender_destroy(RStdSyncSender *sender) {
    r_library_internal_sync_sender_destroy(sender);
}

static inline void r_std_sync_sender_move_initialize(RStdSyncSender *destination,
                                                     RStdSyncSender *source) {
    r_library_internal_sync_sender_move(destination, source);
}

static inline void r_std_sync_sync_sender_destroy(RStdSyncSyncSender *sender) {
    r_library_internal_sync_sync_sender_destroy(sender);
}

static inline void r_std_sync_sync_sender_move_initialize(RStdSyncSyncSender *destination,
                                                          RStdSyncSyncSender *source) {
    r_library_internal_sync_sync_sender_move(destination, source);
}

static inline void r_std_sync_receiver_destroy(RStdSyncReceiver *receiver) {
    r_library_internal_sync_receiver_destroy(receiver);
}

static inline void r_std_sync_receiver_move_initialize(RStdSyncReceiver *destination,
                                                       RStdSyncReceiver *source) {
    r_library_internal_sync_receiver_move(destination, source);
}

static inline void r_std_sync_permit_destroy(RStdSyncPermit *permit) {
    r_library_internal_sync_permit_destroy(permit);
}

static inline void r_std_sync_permit_move_initialize(RStdSyncPermit *destination,
                                                     RStdSyncPermit *source) {
    r_library_internal_sync_permit_move(destination, source);
}

static inline void r_std_sync_lock_result_destroy(RStdSyncLockResult *result) {
    r_std_sync_mutex_guard_destroy(&result->guard);
    *result = (RStdSyncLockResult){0};
}

static inline void r_std_sync_try_lock_result_destroy(RStdSyncTryLockResult *result) {
    r_std_sync_mutex_guard_destroy(&result->guard);
    *result = (RStdSyncTryLockResult){0};
}

static inline void r_std_sync_read_lock_result_destroy(RStdSyncReadLockResult *result) {
    r_std_sync_rw_read_guard_destroy(&result->guard);
    *result = (RStdSyncReadLockResult){0};
}

static inline void r_std_sync_try_read_lock_result_destroy(RStdSyncTryReadLockResult *result) {
    r_std_sync_rw_read_guard_destroy(&result->guard);
    *result = (RStdSyncTryReadLockResult){0};
}

static inline void r_std_sync_write_lock_result_destroy(RStdSyncWriteLockResult *result) {
    r_std_sync_rw_write_guard_destroy(&result->guard);
    *result = (RStdSyncWriteLockResult){0};
}

static inline void r_std_sync_try_write_lock_result_destroy(RStdSyncTryWriteLockResult *result) {
    r_std_sync_rw_write_guard_destroy(&result->guard);
    *result = (RStdSyncTryWriteLockResult){0};
}

#endif

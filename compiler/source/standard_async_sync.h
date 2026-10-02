#ifndef R_COMPILER_STANDARD_ASYNC_SYNC_H
#define R_COMPILER_STANDARD_ASYNC_SYNC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * R-SLIB-ASYNC-0013..0016 and R-LIB-0016 (L30): the operations of the asynchronous locks,
 * semaphore, notify, broadcast and channel reservations. The shape fixes the operands, the result
 * and the checked effects; subject names the standard type that the first operand borrows or
 * consumes, and result the standard type the operation produces.
 */
typedef enum RAsyncSyncShape {
    /* (T value) -> subject<T> throws alloc_error */
    R_ASYNC_SYNC_CONSTRUCT_VALUE = 0,
    /* (usize count) -> subject throws alloc_error */
    R_ASYNC_SYNC_CONSTRUCT_COUNT,
    /* () -> subject throws alloc_error */
    R_ASYNC_SYNC_CONSTRUCT_EMPTY,
    /* ::<T>(usize capacity) -> subject<T> throws alloc_error */
    R_ASYNC_SYNC_CONSTRUCT_TYPED,
    /* (const subject<T>*) -> result<T> throws alloc_error */
    R_ASYNC_SYNC_SUBSCRIBE,
    /* (const subject<T>*, T value) -> usize throws alloc_error */
    R_ASYNC_SYNC_PUBLISH,
    /* (const subject<T>*) -> subject<T> */
    R_ASYNC_SYNC_CLONE,
    /* (const subject<T>*) -> o<result<T>> */
    R_ASYNC_SYNC_TRY,
    /* (const subject<T>*) -> const T* */
    R_ASYNC_SYNC_ACCESS_REF,
    /* (subject<T>*) -> T* */
    R_ASYNC_SYNC_ACCESS_MUT,
    /* (subject<T> value) -> void; unlock takes any guard */
    R_ASYNC_SYNC_CONSUME,
    /* (const subject*) -> void */
    R_ASYNC_SYNC_HANDLE_VOID,
    /* (const subject*, usize count) -> void */
    R_ASYNC_SYNC_HANDLE_COUNT,
    /* (const subject*) -> usize */
    R_ASYNC_SYNC_HANDLE_USIZE,
    /* async (const subject<T>*) -> result<T> */
    R_ASYNC_SYNC_START,
    /* async (const subject*) -> void */
    R_ASYNC_SYNC_START_VOID,
    /* async (const subject<T>*) -> result<T>: an outcome whose layout the library receives */
    R_ASYNC_SYNC_START_OUTCOME,
    /* (const subject<T>*) -> result<T>: a std.sync outcome with a permit payload */
    R_ASYNC_SYNC_TRY_OUTCOME,
    /* (subject<T> permit, T value) -> void */
    R_ASYNC_SYNC_SEND_PERMIT,
    /* () -> u64: a query of the running task (R-SLIB-ASYNC-0018) or of the allocator
       (R-SLIB-TEST-0003) */
    R_ASYNC_SYNC_QUERY_U64,
    /* (u64 value) -> void: a setting of the allocator for tests (R-SLIB-TEST-0003) */
    R_ASYNC_SYNC_SET_U64
} RAsyncSyncShape;

typedef struct RAsyncSyncDescriptor {
    RStandardCallOperation operation;
    const char *module;
    const char *name;
    const char *native;
    RAsyncSyncShape shape;
    const char *subject;
    const char *result;
    const char *rule;
} RAsyncSyncDescriptor;

static inline const RAsyncSyncDescriptor *
r_async_sync_descriptor(RStandardCallOperation operation) {
    static const RAsyncSyncDescriptor descriptors[] = {
        {R_STANDARD_CALL_ASYNC_MUTEX_NEW,
         "async",
         "mutex_new",
         "r_std_async_mutex_new",
         R_ASYNC_SYNC_CONSTRUCT_VALUE,
         "std.async::mutex",
         NULL,
         "R-SLIB-ASYNC-0013"},
        {R_STANDARD_CALL_ASYNC_CLONE_MUTEX,
         "async",
         "clone_mutex",
         "r_std_async_clone_mutex",
         R_ASYNC_SYNC_CLONE,
         "std.async::mutex",
         "std.async::mutex",
         "R-SLIB-ASYNC-0013"},
        {R_STANDARD_CALL_ASYNC_LOCK,
         "async",
         "lock",
         "r_std_async_lock",
         R_ASYNC_SYNC_START,
         "std.async::mutex",
         "std.async::mutex_guard",
         "R-SLIB-ASYNC-0013"},
        {R_STANDARD_CALL_ASYNC_TRY_LOCK,
         "async",
         "try_lock",
         "r_std_async_try_lock",
         R_ASYNC_SYNC_TRY,
         "std.async::mutex",
         "std.async::mutex_guard",
         "R-SLIB-ASYNC-0013"},
        {R_STANDARD_CALL_ASYNC_MUTEX_GUARD_REF,
         "async",
         "mutex_guard_ref",
         "r_std_async_mutex_guard_ref",
         R_ASYNC_SYNC_ACCESS_REF,
         "std.async::mutex_guard",
         NULL,
         "R-SLIB-ASYNC-0013"},
        {R_STANDARD_CALL_ASYNC_MUTEX_GUARD_MUT,
         "async",
         "mutex_guard_mut",
         "r_std_async_mutex_guard_mut",
         R_ASYNC_SYNC_ACCESS_MUT,
         "std.async::mutex_guard",
         NULL,
         "R-SLIB-ASYNC-0013"},
        {R_STANDARD_CALL_ASYNC_UNLOCK,
         "async",
         "unlock",
         "r_std_async_unlock",
         R_ASYNC_SYNC_CONSUME,
         NULL,
         NULL,
         "R-SLIB-ASYNC-0013"},
        {R_STANDARD_CALL_ASYNC_RWLOCK_NEW,
         "async",
         "rwlock_new",
         "r_std_async_rwlock_new",
         R_ASYNC_SYNC_CONSTRUCT_VALUE,
         "std.async::rw_lock",
         NULL,
         "R-SLIB-ASYNC-0013"},
        {R_STANDARD_CALL_ASYNC_CLONE_RW_LOCK,
         "async",
         "clone_rw_lock",
         "r_std_async_clone_rw_lock",
         R_ASYNC_SYNC_CLONE,
         "std.async::rw_lock",
         "std.async::rw_lock",
         "R-SLIB-ASYNC-0013"},
        {R_STANDARD_CALL_ASYNC_READ,
         "async",
         "read",
         "r_std_async_read",
         R_ASYNC_SYNC_START,
         "std.async::rw_lock",
         "std.async::rw_read_guard",
         "R-SLIB-ASYNC-0013"},
        {R_STANDARD_CALL_ASYNC_WRITE,
         "async",
         "write",
         "r_std_async_write",
         R_ASYNC_SYNC_START,
         "std.async::rw_lock",
         "std.async::rw_write_guard",
         "R-SLIB-ASYNC-0013"},
        {R_STANDARD_CALL_ASYNC_TRY_READ,
         "async",
         "try_read",
         "r_std_async_try_read",
         R_ASYNC_SYNC_TRY,
         "std.async::rw_lock",
         "std.async::rw_read_guard",
         "R-SLIB-ASYNC-0013"},
        {R_STANDARD_CALL_ASYNC_TRY_WRITE,
         "async",
         "try_write",
         "r_std_async_try_write",
         R_ASYNC_SYNC_TRY,
         "std.async::rw_lock",
         "std.async::rw_write_guard",
         "R-SLIB-ASYNC-0013"},
        {R_STANDARD_CALL_ASYNC_RW_READ_GUARD_REF,
         "async",
         "rw_read_guard_ref",
         "r_std_async_rw_read_guard_ref",
         R_ASYNC_SYNC_ACCESS_REF,
         "std.async::rw_read_guard",
         NULL,
         "R-SLIB-ASYNC-0013"},
        {R_STANDARD_CALL_ASYNC_RW_WRITE_GUARD_REF,
         "async",
         "rw_write_guard_ref",
         "r_std_async_rw_write_guard_ref",
         R_ASYNC_SYNC_ACCESS_REF,
         "std.async::rw_write_guard",
         NULL,
         "R-SLIB-ASYNC-0013"},
        {R_STANDARD_CALL_ASYNC_RW_WRITE_GUARD_MUT,
         "async",
         "rw_write_guard_mut",
         "r_std_async_rw_write_guard_mut",
         R_ASYNC_SYNC_ACCESS_MUT,
         "std.async::rw_write_guard",
         NULL,
         "R-SLIB-ASYNC-0013"},
        {R_STANDARD_CALL_ASYNC_SEMAPHORE_NEW,
         "async",
         "semaphore_new",
         "r_std_async_semaphore_new",
         R_ASYNC_SYNC_CONSTRUCT_COUNT,
         "std.async::semaphore",
         NULL,
         "R-SLIB-ASYNC-0014"},
        {R_STANDARD_CALL_ASYNC_CLONE_SEMAPHORE,
         "async",
         "clone_semaphore",
         "r_std_async_clone_semaphore",
         R_ASYNC_SYNC_CLONE,
         "std.async::semaphore",
         "std.async::semaphore",
         "R-SLIB-ASYNC-0014"},
        {R_STANDARD_CALL_ASYNC_ACQUIRE,
         "async",
         "acquire",
         "r_std_async_acquire",
         R_ASYNC_SYNC_START,
         "std.async::semaphore",
         "std.async::semaphore_permit",
         "R-SLIB-ASYNC-0014"},
        {R_STANDARD_CALL_ASYNC_TRY_ACQUIRE,
         "async",
         "try_acquire",
         "r_std_async_try_acquire",
         R_ASYNC_SYNC_TRY,
         "std.async::semaphore",
         "std.async::semaphore_permit",
         "R-SLIB-ASYNC-0014"},
        {R_STANDARD_CALL_ASYNC_RELEASE,
         "async",
         "release",
         "r_std_async_release",
         R_ASYNC_SYNC_CONSUME,
         "std.async::semaphore_permit",
         NULL,
         "R-SLIB-ASYNC-0014"},
        {R_STANDARD_CALL_ASYNC_ADD_PERMITS,
         "async",
         "add_permits",
         "r_std_async_add_permits",
         R_ASYNC_SYNC_HANDLE_COUNT,
         "std.async::semaphore",
         NULL,
         "R-SLIB-ASYNC-0014"},
        {R_STANDARD_CALL_ASYNC_AVAILABLE_PERMITS,
         "async",
         "available_permits",
         "r_std_async_available_permits",
         R_ASYNC_SYNC_HANDLE_USIZE,
         "std.async::semaphore",
         NULL,
         "R-SLIB-ASYNC-0014"},
        {R_STANDARD_CALL_ASYNC_NOTIFY_NEW,
         "async",
         "notify_new",
         "r_std_async_notify_new",
         R_ASYNC_SYNC_CONSTRUCT_EMPTY,
         "std.async::notify",
         NULL,
         "R-SLIB-ASYNC-0015"},
        {R_STANDARD_CALL_ASYNC_CLONE_NOTIFY,
         "async",
         "clone_notify",
         "r_std_async_clone_notify",
         R_ASYNC_SYNC_CLONE,
         "std.async::notify",
         "std.async::notify",
         "R-SLIB-ASYNC-0015"},
        {R_STANDARD_CALL_ASYNC_NOTIFY_ONE,
         "async",
         "notify_one",
         "r_std_async_notify_one",
         R_ASYNC_SYNC_HANDLE_VOID,
         "std.async::notify",
         NULL,
         "R-SLIB-ASYNC-0015"},
        {R_STANDARD_CALL_ASYNC_NOTIFY_ALL,
         "async",
         "notify_all",
         "r_std_async_notify_all",
         R_ASYNC_SYNC_HANDLE_VOID,
         "std.async::notify",
         NULL,
         "R-SLIB-ASYNC-0015"},
        {R_STANDARD_CALL_ASYNC_NOTIFIED,
         "async",
         "notified",
         "r_std_async_notified",
         R_ASYNC_SYNC_START_VOID,
         "std.async::notify",
         NULL,
         "R-SLIB-ASYNC-0015"},
        {R_STANDARD_CALL_ASYNC_BROADCAST,
         "async",
         "broadcast",
         "r_std_async_broadcast",
         R_ASYNC_SYNC_CONSTRUCT_TYPED,
         "std.async::broadcast",
         NULL,
         "R-SLIB-ASYNC-0016"},
        {R_STANDARD_CALL_ASYNC_CLONE_BROADCAST,
         "async",
         "clone_broadcast",
         "r_std_async_clone_broadcast",
         R_ASYNC_SYNC_CLONE,
         "std.async::broadcast",
         "std.async::broadcast",
         "R-SLIB-ASYNC-0016"},
        {R_STANDARD_CALL_ASYNC_SUBSCRIBE,
         "async",
         "subscribe",
         "r_std_async_subscribe",
         R_ASYNC_SYNC_SUBSCRIBE,
         "std.async::broadcast",
         "std.async::broadcast_receiver",
         "R-SLIB-ASYNC-0016"},
        {R_STANDARD_CALL_ASYNC_PUBLISH,
         "async",
         "publish",
         "r_std_async_publish",
         R_ASYNC_SYNC_PUBLISH,
         "std.async::broadcast",
         NULL,
         "R-SLIB-ASYNC-0016"},
        {R_STANDARD_CALL_ASYNC_BROADCAST_RECEIVE,
         "async",
         "broadcast_receive",
         "r_std_async_broadcast_receive",
         R_ASYNC_SYNC_START_OUTCOME,
         "std.async::broadcast_receiver",
         "std.async::broadcast_result",
         "R-SLIB-ASYNC-0016"},
        {R_STANDARD_CALL_SYNC_RESERVE,
         "sync",
         "reserve",
         "r_std_sync_reserve",
         R_ASYNC_SYNC_START_OUTCOME,
         "std.sync::sync_sender",
         "std.sync::reserve_result",
         "R-LIB-0016"},
        {R_STANDARD_CALL_SYNC_TRY_RESERVE,
         "sync",
         "try_reserve",
         "r_std_sync_try_reserve",
         R_ASYNC_SYNC_TRY_OUTCOME,
         "std.sync::sync_sender",
         "std.sync::try_reserve_result",
         "R-LIB-0016"},
        {R_STANDARD_CALL_SYNC_SEND_PERMIT,
         "sync",
         "send_permit",
         "r_std_sync_send_permit",
         R_ASYNC_SYNC_SEND_PERMIT,
         "std.sync::permit",
         NULL,
         "R-LIB-0016"},
        {R_STANDARD_CALL_ASYNC_TASK_ID,
         "async",
         "task_id",
         "r_std_async_task_id",
         R_ASYNC_SYNC_QUERY_U64,
         NULL,
         NULL,
         "R-SLIB-ASYNC-0018"},
        {R_STANDARD_CALL_TEST_ALLOCATION_ATTEMPTS,
         "test",
         "allocation_attempts",
         "r_std_test_allocation_attempts",
         R_ASYNC_SYNC_QUERY_U64,
         NULL,
         NULL,
         "R-SLIB-TEST-0003"},
        {R_STANDARD_CALL_TEST_FAIL_ALLOCATION_AT,
         "test",
         "fail_allocation_at",
         "r_std_test_fail_allocation_at",
         R_ASYNC_SYNC_SET_U64,
         NULL,
         NULL,
         "R-SLIB-TEST-0003"},
    };
    for (size_t index = 0U; index < sizeof(descriptors) / sizeof(descriptors[0]); ++index) {
        if (descriptors[index].operation == operation) {
            return &descriptors[index];
        }
    }
    return NULL;
}

/* The descriptor of `module::name`, or NULL. */
static inline const RAsyncSyncDescriptor *
r_async_sync_descriptor_named(const char *module, const char *name, size_t name_length) {
    for (RStandardCallOperation operation = R_STANDARD_CALL_ASYNC_MUTEX_NEW;
         operation <= R_STANDARD_CALL_SYNC_SEND_PERMIT;
         operation = (RStandardCallOperation)(operation + 1)) {
        const RAsyncSyncDescriptor *descriptor = r_async_sync_descriptor(operation);
        if ((descriptor != NULL) && (strcmp(descriptor->module, module) == 0) &&
            (strlen(descriptor->name) == name_length) &&
            (memcmp(descriptor->name, name, name_length) == 0)) {
            return descriptor;
        }
    }
    return NULL;
}

static inline bool r_async_sync_operation(RStandardCallOperation operation) {
    return (operation >= R_STANDARD_CALL_ASYNC_MUTEX_NEW) &&
           (operation <= R_STANDARD_CALL_SYNC_SEND_PERMIT);
}

/* Whether the operation starts a task. */
static inline bool r_async_sync_starts(const RAsyncSyncDescriptor *descriptor) {
    return (descriptor != NULL) && ((descriptor->shape == R_ASYNC_SYNC_START) ||
                                    (descriptor->shape == R_ASYNC_SYNC_START_VOID) ||
                                    (descriptor->shape == R_ASYNC_SYNC_START_OUTCOME));
}

/* Whether the first operand borrows the resource. */
static inline bool r_async_sync_borrows(const RAsyncSyncDescriptor *descriptor) {
    return (descriptor != NULL) && (descriptor->shape != R_ASYNC_SYNC_CONSTRUCT_VALUE) &&
           (descriptor->shape != R_ASYNC_SYNC_CONSTRUCT_COUNT) &&
           (descriptor->shape != R_ASYNC_SYNC_CONSTRUCT_EMPTY) &&
           (descriptor->shape != R_ASYNC_SYNC_CONSTRUCT_TYPED) &&
           (descriptor->shape != R_ASYNC_SYNC_CONSUME) &&
           (descriptor->shape != R_ASYNC_SYNC_SEND_PERMIT) &&
           (descriptor->shape != R_ASYNC_SYNC_QUERY_U64) &&
           (descriptor->shape != R_ASYNC_SYNC_SET_U64);
}

/* The number of operands of the call. */
static inline uint32_t r_async_sync_operand_count(const RAsyncSyncDescriptor *descriptor) {
    if (descriptor == NULL) {
        return UINT32_C(0);
    }
    switch (descriptor->shape) {
    case R_ASYNC_SYNC_CONSTRUCT_EMPTY:
    case R_ASYNC_SYNC_QUERY_U64:
        return UINT32_C(0);
    case R_ASYNC_SYNC_PUBLISH:
    case R_ASYNC_SYNC_HANDLE_COUNT:
    case R_ASYNC_SYNC_SEND_PERMIT:
        return UINT32_C(2);
    default:
        return UINT32_C(1);
    }
}

/* Whether the operation may throw std.alloc::alloc_error. */
static inline bool r_async_sync_allocates(const RAsyncSyncDescriptor *descriptor) {
    return (descriptor != NULL) && ((descriptor->shape == R_ASYNC_SYNC_CONSTRUCT_VALUE) ||
                                    (descriptor->shape == R_ASYNC_SYNC_CONSTRUCT_COUNT) ||
                                    (descriptor->shape == R_ASYNC_SYNC_CONSTRUCT_EMPTY) ||
                                    (descriptor->shape == R_ASYNC_SYNC_CONSTRUCT_TYPED) ||
                                    (descriptor->shape == R_ASYNC_SYNC_SUBSCRIBE) ||
                                    (descriptor->shape == R_ASYNC_SYNC_PUBLISH));
}

#endif

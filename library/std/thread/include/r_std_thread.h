#ifndef R_STD_THREAD_H
#define R_STD_THREAD_H

#include "r_runtime_0_1.h"
#include "r_runtime_allocator.h"
#include "r_runtime_string.h"
#include "r_runtime_type.h"
#include "r_std_string.h"

#include <stdint.h>

typedef struct RStdThreadDescriptor RStdThreadDescriptor;

typedef enum RStdThreadError {
    R_STD_THREAD_ERROR_UNAVAILABLE = 0,
    R_STD_THREAD_ERROR_RESOURCE_EXHAUSTED = 1,
    R_STD_THREAD_ERROR_PERMISSION_DENIED = 2
} RStdThreadError;

typedef struct RStdThread {
    RStdThreadDescriptor *descriptor;
} RStdThread;

typedef struct RStdThreadJoinHandle {
    RStdThreadDescriptor *descriptor;
} RStdThreadJoinHandle;

typedef RStdThreadJoinHandle RStdThreadScopedJoinHandle;

typedef struct RStdThreadPanicReport {
    RRuntimePanicCategory category;
    RRuntimeString text;
} RStdThreadPanicReport;

typedef enum RStdThreadJoinResultKind {
    R_STD_THREAD_JOIN_COMPLETED = 0,
    R_STD_THREAD_JOIN_RETURNED = 1,
    R_STD_THREAD_JOIN_PANICKED = 2
} RStdThreadJoinResultKind;

/*
 * Private compiler/runtime ABI for a thread completion value. storage_type describes the complete
 * erased value. error_count == 0 denotes an infallible completion and leaves both offsets zero.
 * A checked completion uses the canonical generated carrier: a uint32_t tag at tag_offset, with
 * success tag 0 and checked-error tags 1..error_count, followed by the carrier union at
 * payload_offset. The runtime moves or drops the complete storage value and never interprets an
 * individual payload.
 */
typedef struct RStdThreadCompletionTypeInfo {
    RRuntimeTypeInfo storage_type;
    size_t tag_offset;
    size_t payload_offset;
    uint32_t error_count;
} RStdThreadCompletionTypeInfo;

typedef struct RStdThreadJoinResult {
    RStdThreadJoinResultKind kind;
    RRuntimeAllocator *allocator;
    RRuntimeTypeInfo value_type;
    void *value;
    RStdThreadPanicReport panic;
    RStdThreadCompletionTypeInfo completion_type;
} RStdThreadJoinResult;

typedef void (*RStdThreadEntryFn)(void *payload, void *result);

typedef struct RStdThreadSpawnResult {
    _Bool is_ok;
    RStdThreadJoinHandle value;
    RStdThreadError error;
} RStdThreadSpawnResult;

typedef struct RStdThreadScopedSpawnResult {
    _Bool is_ok;
    RStdThreadScopedJoinHandle value;
    RStdThreadError error;
} RStdThreadScopedSpawnResult;

/*
 * Ownership: allocator remains borrowed until the target exits and every identity, join handle,
 * and unconsumed result is destroyed. A failed start does not read, move, or drop staged_payload.
 * Success move-initializes the thread payload and therefore commits staged_payload exactly once.
 */
RStdThreadSpawnResult r_std_thread_spawn(RRuntimeAllocator *allocator,
                                         RRuntimeTypeInfo payload_type,
                                         RRuntimeTypeInfo result_type,
                                         RStdThreadEntryFn entry,
                                         void *staged_payload);

/*
 * Ownership: identical two-phase start contract to spawn. The returned handle carries the scoped
 * observation right; its implicit destruction waits for completion and drops an unobserved result.
 */
RStdThreadScopedSpawnResult r_std_thread_spawn_scoped(RRuntimeAllocator *allocator,
                                                      RRuntimeTypeInfo payload_type,
                                                      RRuntimeTypeInfo result_type,
                                                      RStdThreadEntryFn entry,
                                                      void *staged_payload);

/*
 * Ownership: consumes either active join-handle kind. A returned value is owned by the result and
 * is dropped exactly once unless compiler lowering first moves it into its typed join_result.
 */
RStdThreadJoinResult r_std_thread_join(RStdThreadJoinHandle *handle);

/* Ownership: consumes an unscoped handle. The target and result cleanup continue independently. */
void r_std_thread_detach(RStdThreadJoinHandle *handle);

/* Ownership: current and clone_thread each return one independently owned identity reference. */
RStdThread r_std_thread_current(void);
RStdThread r_std_thread_clone_thread(const RStdThread *source);

/* Ownership: target identities are shared call-bounded borrows and are never retained. */
void r_std_thread_unpark(const RStdThread *target);

void r_std_thread_park(void);
void r_std_thread_yield_now(void);
void r_std_thread_sleep_nanoseconds(uint64_t nanoseconds);

/* Both observers borrow report for the call and return non-owning UTF-8 views. */
RStdStringView r_std_thread_panic_category(const RStdThreadPanicReport *report);
RStdStringView r_std_thread_panic_text(const RStdThreadPanicReport *report);

/* Private drop ABI used by generated wrappers; these are not public R operations. */
void r_library_internal_thread_identity_destroy(RStdThread *thread);
void r_library_internal_thread_handle_destroy(RStdThreadJoinHandle *handle);
void r_library_internal_thread_join_result_destroy(RStdThreadJoinResult *result);
void r_library_internal_thread_panic_report_destroy(RStdThreadPanicReport *report);
/* Copies the runtime report of a panic into an owned report (R-ERR-0009, std.async::join). */
void r_library_internal_thread_panic_report_from(RStdThreadPanicReport *report,
                                                 const RRuntimePanicReportData *data);
/* Takes the pending panic of this thread into report (std.async::join, R-SLIB-ASYNC-0020). */
void r_library_internal_thread_panic_report_take(RStdThreadPanicReport *report);
void r_library_internal_thread_join_result_move(RStdThreadJoinResult *result, void *destination);

/*
 * Private compiler ABI for an entry with checked completion effects. Failed creation preserves
 * staged_payload exactly as the public spawn forms do. On success, completion_type is retained by
 * the descriptor and returned unchanged by join; detach drops its complete carrier exactly once.
 */
RStdThreadSpawnResult
r_library_internal_thread_spawn_checked(RRuntimeAllocator *allocator,
                                        RRuntimeTypeInfo payload_type,
                                        RStdThreadCompletionTypeInfo completion_type,
                                        RStdThreadEntryFn entry,
                                        void *staged_payload);
RStdThreadScopedSpawnResult
r_library_internal_thread_spawn_scoped_checked(RRuntimeAllocator *allocator,
                                               RRuntimeTypeInfo payload_type,
                                               RStdThreadCompletionTypeInfo completion_type,
                                               RStdThreadEntryFn entry,
                                               void *staged_payload);

static inline void r_std_thread_thread_destroy(RStdThread *thread) {
    r_library_internal_thread_identity_destroy(thread);
}

static inline void r_std_thread_thread_move_initialize(RStdThread *destination,
                                                       RStdThread *source) {
    *destination = *source;
    *source = (RStdThread){0};
}

static inline void r_std_thread_join_handle_destroy(RStdThreadJoinHandle *handle) {
    r_library_internal_thread_handle_destroy(handle);
}

static inline void r_std_thread_scoped_join_handle_destroy(RStdThreadScopedJoinHandle *handle) {
    r_library_internal_thread_handle_destroy(handle);
}

static inline void r_std_thread_join_result_destroy(RStdThreadJoinResult *result) {
    r_library_internal_thread_join_result_destroy(result);
}

static inline void r_std_thread_panic_report_destroy(RStdThreadPanicReport *report) {
    r_library_internal_thread_panic_report_destroy(report);
}

static inline void r_std_thread_panic_report_move_initialize(RStdThreadPanicReport *destination,
                                                             RStdThreadPanicReport *source) {
    *destination = *source;
    *source = (RStdThreadPanicReport){0};
}

#endif

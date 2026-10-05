#ifndef R_RUNTIME_0_1_H
#define R_RUNTIME_0_1_H

#include "r_runtime_allocator.h"
#include "r_runtime_core.h"
#include "r_runtime_target_abi.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RRuntimeStartResult {
    _Bool started;
    int process_status;
} RRuntimeStartResult;

typedef struct RRuntimeStringView {
    const uint8_t *data;
    size_t length;
} RRuntimeStringView;

typedef struct RRuntimeArgumentSnapshotView {
    const uint8_t *data;
    size_t byte_length;
    size_t count;
    const RRuntimeStringView *arguments;
} RRuntimeArgumentSnapshotView;

/*
 * Stable hosted startup statuses. The runtime emits the matching allocation-free
 * emergency category before returning either status.
 */
enum {
    R_RUNTIME_ARGUMENT_ENCODING_STATUS = 125,
    R_RUNTIME_ALLOCATION_FAILURE_STATUS = 126,
    R_RUNTIME_ASYNC_ROOT_START_FAILURE_STATUS = 127
};

RRuntimeStartResult r_runtime_hosted_start(int argc, char *argv[]);
int r_runtime_hosted_finish(int32_t result);
/*
 * R-SLIB-PROC-0007: the process status for std.process::exit, after the orderly shutdown that
 * async-main completion performs. On an executor worker the calling task and the tasks waiting on
 * it cannot finish, so every other task is cancelled and drained instead; the caller then invokes
 * C exit on the calling thread.
 */
int r_runtime_hosted_exit(int32_t result);

/*
 * Panic unwind (Core R-ERR-0005..0009). Generated code does not abort on a panic: it records the
 * panic in the state of its thread, runs the cleanup of each frame and returns to its caller,
 * which tests the state after the call. A panic is pending while it propagates and in cleanup
 * while drops and finally blocks run; a panic that begins during cleanup is a second panic and
 * aborts (R-ERR-0008). Panics inside runtime and library C abort as before (R-ERR-0006).
 * r_runtime_unwinding_threads counts the threads with a pending panic, so the test after a call
 * is one relaxed load until some thread panics.
 */
enum {
    R_RUNTIME_PANIC_TEXT_CAPACITY = 256
};

typedef struct RRuntimePanicReportData {
    RRuntimePanicCategory category;
    RRuntimeSourceSpan span;
    uint32_t text_length;
    char text[R_RUNTIME_PANIC_TEXT_CAPACITY];
} RRuntimePanicReportData;

extern _Atomic uint32_t r_runtime_unwinding_threads;

_Bool r_runtime_unwinding_current_thread(void);

static inline _Bool r_runtime_unwinding(void) {
    return atomic_load_explicit(&r_runtime_unwinding_threads, memory_order_relaxed) != 0U &&
           r_runtime_unwinding_current_thread();
}

/* Whether a panic of this thread is pending or in cleanup: a lock guard dropped meanwhile poisons
   its lock (R-LIB-0014). */
_Bool r_runtime_panicking(void);

/* Begins a panic on this thread; it returns, and the caller leaves through its cleanup. The name
   has the length of r_runtime_panic, so generated calls keep the layout of the abort strategy. */
void r_runtime_raise(RRuntimePanicCategory category, RRuntimeSourceSpan span);
/* The same with the diagnostic text of an explicit panic, truncated to the report capacity. */
void r_runtime_raise_text(RRuntimePanicCategory category,
                          RRuntimeSourceSpan span,
                          const uint8_t *text,
                          size_t length);
/* Brackets the drops and finally blocks that a frame runs for a pending panic. */
void r_runtime_unwind_cleanup_enter(void);
void r_runtime_unwind_cleanup_leave(void);
/* Takes the pending panic of this thread at a task or thread boundary; false when none. */
_Bool r_runtime_panic_take(RRuntimePanicReportData *report);
/* Makes report the pending panic of this thread again, as await re-raises an observed panic. */
void r_runtime_panic_resume(const RRuntimePanicReportData *report);
/* Writes the diagnostic of report and aborts: a panic that reached the initial thread's root. */
_Noreturn void r_runtime_panic_terminate(const RRuntimePanicReportData *report);
/* The same for the pending panic of this thread, after the synchronous main returned with it. */
_Noreturn void r_runtime_unwind_terminate(void);
/* R-ERR-0008: a panic that begins while first unwinds; both diagnostics, then abort. */
_Noreturn void r_runtime_panic_second(const RRuntimePanicReportData *first,
                                      RRuntimePanicCategory category,
                                      RRuntimeSourceSpan span);
/* Delivers an unobserved report to the panic hook (R-ERR-0009, R-MEM-0017). */
void r_runtime_panic_deliver(const RRuntimePanicReportData *report);

/* M44 (Library R-SLIB-LOG-0005): the sink that takes an unobserved report before the line of
   r_runtime_panic_deliver; it returns whether it took the report. It runs on the thread that
   delivers the report and neither blocks nor runs R code. NULL removes it. */
typedef _Bool (*RRuntimePanicSink)(const RRuntimePanicReportData *report);
void r_runtime_panic_set_sink(RRuntimePanicSink sink);

/* Allocation-free emergency diagnostic sink; strings are borrowed for this call only. */
void r_runtime_emergency_write(const char *bytes, size_t length);
void r_runtime_report_main_error(const char *domain,
                                 size_t domain_length,
                                 const uint8_t *name,
                                 size_t name_length,
                                 uint32_t code,
                                 int64_t native_code);

/* R-AM-0013: drain participating work without tearing down services needed by destructors. */
void r_runtime_hosted_drain(void);
/* Internal work reservations include preparation, execution and final cleanup. */
void r_runtime_hosted_work_begin(void);
void r_runtime_hosted_work_end(void);
void r_runtime_hosted_work_drain(void);
/* Only initial-thread destruction performs a drain between individual objects. */
void r_runtime_hosted_after_object_drop(void);

/*
 * Borrows the allocator for the active hosted runtime generation. The pointer remains valid until
 * r_runtime_hosted_finish begins. Returns null before successful hosted startup and after finish.
 * Compiler-generated allocation expressions use this service only while executing an R entry.
 */
RRuntimeAllocator *r_runtime_hosted_allocator(void);

/*
 * Runs the installed thread-local destruction entry (r_runtime_core.h) for the current thread.
 * Runtime-managed R entry points call it before detaching or returning a worker thread to its
 * native executor.
 */
void r_runtime_thread_local_cleanup_current(void);

/*
 * Called only after hosted_start succeeded and the compiler-generated async root start failed.
 * Reports the stable async-root category, drains every started hosted service exactly once, and
 * returns R_RUNTIME_ASYNC_ROOT_START_FAILURE_STATUS.
 */
int r_runtime_hosted_async_root_start_failure(void);

/*
 * Borrows the immutable NUL-separated hosted argument snapshot and its array of
 * validated string views. Both are held in the snapshot's single backing
 * allocation. The view is valid until r_runtime_hosted_finish begins. A
 * generated asynchronous main may use it only during its initial execution;
 * every other escaping use first copies it into independently owned storage.
 */
_Bool r_runtime_hosted_argument_snapshot(RRuntimeArgumentSnapshotView *view);

#if defined(R_RUNTIME_TESTING)
void r_runtime_testing_set_snapshot_allocation_failure(_Bool enabled);
void r_runtime_testing_set_executor_allocation_failure(_Bool enabled);
#endif

/*
 * The hosted r_runtime_panic (r_runtime_core.h) emits one bounded diagnostic directly to file
 * descriptor 2 without allocation, stdio or locking, then aborts. Interrupted and partial writes
 * are retried within a fixed attempt budget. The hosted stack preflight derives each thread's
 * bounds from the Darwin pthread stack; a failed initialization leaves require in conservative
 * stack-exhaustion mode.
 */

#ifdef __cplusplus
}
#endif

#endif

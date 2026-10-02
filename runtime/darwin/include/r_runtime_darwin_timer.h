#ifndef R_RUNTIME_DARWIN_TIMER_H
#define R_RUNTIME_DARWIN_TIMER_H

#include "r_runtime_task.h"

#include <stdatomic.h>
#include <stdint.h>

typedef enum RRuntimeDarwinTimerPrepareStatus {
    R_RUNTIME_DARWIN_TIMER_PREPARE_OK = 0,
    R_RUNTIME_DARWIN_TIMER_PREPARE_INVALID,
    R_RUNTIME_DARWIN_TIMER_PREPARE_ALLOCATION_FAILED,
    R_RUNTIME_DARWIN_TIMER_PREPARE_UNAVAILABLE
} RRuntimeDarwinTimerPrepareStatus;

typedef struct RRuntimeDarwinTimer {
    _Atomic uintptr_t native_source;
    _Atomic _Bool activated;
    RRuntimeTaskExternalExecution *execution;
} RRuntimeDarwinTimer;

void r_runtime_darwin_timer_initialize(RRuntimeDarwinTimer *timer);
void r_runtime_darwin_timer_move(RRuntimeDarwinTimer *destination, RRuntimeDarwinTimer *source);

/*
 * Ownership: prepare reserves a suspended one-shot DispatchSourceTimer before task commit. The
 * delay is an exact nonnegative interval; values outside Dispatch's signed delta horizon report
 * unavailable. A successful timer shall be moved into the external task payload or disposed.
 */
RRuntimeDarwinTimerPrepareStatus
r_runtime_darwin_timer_prepare(RRuntimeDarwinTimer *timer, uint64_t seconds, uint32_t nanoseconds);

/* Binds committed frame state, publishes readiness, then activates the source exactly once. */
void r_runtime_darwin_timer_bind_and_activate(RRuntimeDarwinTimer *timer,
                                              RRuntimeTaskExternalExecution *execution);

/* Requests native cancellation. The Dispatch cancel handler performs terminal acknowledgement. */
void r_runtime_darwin_timer_cancel(RRuntimeDarwinTimer *timer);

/* Releases a successfully prepared source which was not transferred into a committed task. */
void r_runtime_darwin_timer_dispose_unsubmitted(RRuntimeDarwinTimer *timer);

#if defined(R_RUNTIME_DARWIN_TIMER_TESTING)
/* Pauses the next cancel handler after it takes native-source ownership. */
void r_runtime_darwin_timer_testing_pause_next_cancel_handler(void);
/* Waits until the armed cancel handler has entered its test pause. */
void r_runtime_darwin_timer_testing_wait_for_cancel_handler(void);
/* Issues a competing cancellation against the timer held by the paused handler. */
void r_runtime_darwin_timer_testing_cancel_paused_timer(void);
/* Releases the cancel handler observed by the preceding wait call. */
void r_runtime_darwin_timer_testing_release_cancel_handler(void);
#endif

#endif

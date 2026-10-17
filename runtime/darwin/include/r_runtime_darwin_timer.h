#ifndef R_RUNTIME_DARWIN_TIMER_H
#define R_RUNTIME_DARWIN_TIMER_H

#include "r_runtime_reactor.h"
#include "r_runtime_task.h"

#include <stdint.h>

typedef enum RRuntimeDarwinTimerPrepareStatus {
    R_RUNTIME_DARWIN_TIMER_PREPARE_OK = 0,
    R_RUNTIME_DARWIN_TIMER_PREPARE_INVALID,
    R_RUNTIME_DARWIN_TIMER_PREPARE_ALLOCATION_FAILED,
    R_RUNTIME_DARWIN_TIMER_PREPARE_UNAVAILABLE
} RRuntimeDarwinTimerPrepareStatus;

typedef struct RRuntimeDarwinTimer {
    /* The alarm of the reactor that fires at the deadline fixed by prepare. */
    RRuntimeReactorSource alarm;
    _Bool prepared;
    RRuntimeTaskExternalExecution *execution;
} RRuntimeDarwinTimer;

void r_runtime_darwin_timer_initialize(RRuntimeDarwinTimer *timer);
void r_runtime_darwin_timer_move(RRuntimeDarwinTimer *destination, RRuntimeDarwinTimer *source);

/*
 * Ownership: prepare fixes the deadline of a one-shot reactor alarm before task commit and starts
 * the reactor; it reserves no native state. The delay is an exact nonnegative interval; one
 * beyond the monotonic clock's signed horizon reports unavailable. A successful timer shall be
 * moved into the external task payload or disposed.
 */
RRuntimeDarwinTimerPrepareStatus
r_runtime_darwin_timer_prepare(RRuntimeDarwinTimer *timer, uint64_t seconds, uint32_t nanoseconds);

/* Binds committed frame state, publishes readiness, then arms the alarm exactly once. */
void r_runtime_darwin_timer_bind_and_activate(RRuntimeDarwinTimer *timer,
                                              RRuntimeTaskExternalExecution *execution);

/* Requests cancellation. The reactor settles the alarm, which acknowledges the task. */
void r_runtime_darwin_timer_cancel(RRuntimeDarwinTimer *timer);

/* Forgets a successfully prepared timer which was not transferred into a committed task. */
void r_runtime_darwin_timer_dispose_unsubmitted(RRuntimeDarwinTimer *timer);

/* Testing hooks (always present: an unarmed hook costs one atomic load). */
/* Pauses the next settlement of an alarm, before it acknowledges its task. */
void r_runtime_darwin_timer_testing_pause_next_cancel_handler(void);
/* Waits until the armed cancel handler has entered its test pause. */
void r_runtime_darwin_timer_testing_wait_for_cancel_handler(void);
/* Issues a competing cancellation against the timer held by the paused handler. */
void r_runtime_darwin_timer_testing_cancel_paused_timer(void);
/* Releases the cancel handler observed by the preceding wait call. */
void r_runtime_darwin_timer_testing_release_cancel_handler(void);

#endif

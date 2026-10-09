#include "r_runtime_own.h"
#include "r_runtime_task.h"
#include "r_std_async.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

/*
 * Cancellation observed when a suspended step resumes (r_runtime_task.h, resumable step
 * contract): `suspended_cleanup` suspends on the await of `child` inside two finally blocks. The
 * step that suspends polls no cancellation and runs no finally. `child` then completes with 7,
 * and the program cancels `suspended_cleanup` (R-SLIB-ASYNC-0006) before its resumed step
 * observes the await. That step receives the value and consumes the child's observation, polls
 * the cancellation once, skips the rest of the body, runs the inner and then the outer finally
 * (trace 0 -> 1 -> 12) and returns CANCELLED; no observation is destroyed a second time.
 *
 * The wrapper tells the tasks apart by the order in which the program prepares them and wraps
 * their steps: it holds `hold` until `suspended_cleanup` has suspended and the resumed step of
 * `suspended_cleanup` until the program has cancelled it.
 */
RRuntimeTaskPrepareResult r_test_prepare(RRuntimeTypeInfo payload_type,
                                         RRuntimeTypeInfo result_type,
                                         RRuntimeTaskStepFn step);
RRuntimeTaskExecutionAwaitStatus
r_test_await(RRuntimeTaskExecution *execution, RRuntimeTask **task, void *result_storage);
_Bool r_test_cancel_requested(const RRuntimeTaskExecution *execution);
RRuntimeOwnStatus r_test_own_create(RRuntimeAllocator *allocator,
                                    RRuntimeTypeInfo type,
                                    void *value,
                                    RRuntimeOwn *result);
void r_test_own_release(RRuntimeOwn *owner);
void r_test_task_destroy(RRuntimeTask **task);
void r_test_async_cancel(RRuntimeTask **operation);

#define r_runtime_task_resumable_start_prepare r_test_prepare
#define r_runtime_task_execution_await r_test_await
#define r_runtime_task_execution_cancel_requested r_test_cancel_requested
#define r_runtime_own_create r_test_own_create
#define r_runtime_own_release r_test_own_release
#define r_runtime_task_destroy r_test_task_destroy
#define r_std_async_cancel r_test_async_cancel
#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_PROGRAM_PRELUDE
#undef main
#undef r_std_async_cancel
#undef r_runtime_task_destroy
#undef r_runtime_own_release
#undef r_runtime_own_create
#undef r_runtime_task_execution_cancel_requested
#undef r_runtime_task_execution_await
#undef r_runtime_task_resumable_start_prepare

/* The tasks of the program in the order in which they are prepared. */
enum {
    R_TEST_MAIN = 0,
    R_TEST_CLEANUP,
    R_TEST_CHILD,
    R_TEST_HOLD,
    R_TEST_TASKS
};

enum {
    R_TEST_ATTEMPTS = 5000,
    R_TEST_STEPS = 2
};

static RRuntimeTaskStepFn r_test_steps[R_TEST_TASKS];
static _Atomic unsigned r_test_prepared;
static _Atomic _Bool r_test_valid = 1;
/* The execution of the running step of suspended_cleanup, to attribute awaits and polls. */
static RRuntimeTaskExecution *_Atomic r_test_cleanup_execution;
static _Atomic unsigned r_test_cleanup_steps;
static _Atomic unsigned r_test_cleanup_suspended;
static _Atomic int r_test_step_status[R_TEST_STEPS];
static _Atomic unsigned r_test_step_awaits[R_TEST_STEPS];
static _Atomic int r_test_step_await_status[R_TEST_STEPS];
static _Atomic unsigned r_test_step_polls[R_TEST_STEPS];
static _Atomic _Bool r_test_poll_result;
static int32_t *_Atomic r_test_trace;
static _Atomic int32_t r_test_trace_at_suspension = -1;
static _Atomic int32_t r_test_trace_at_release = -1;
static _Atomic unsigned r_test_releases;
static _Atomic unsigned r_test_destroyed;
static _Atomic unsigned r_test_cancels;

static void r_test_wait(_Atomic unsigned *counter, unsigned value) {
    const struct timespec interval = {0, 1000000L};
    unsigned attempts = 0U;
    while (atomic_load(counter) < value) {
        if (attempts++ >= R_TEST_ATTEMPTS) {
            atomic_store(&r_test_valid, 0);
            return;
        }
        (void)nanosleep(&interval, NULL);
    }
}

/* The index of the running step of suspended_cleanup. */
static unsigned r_test_cleanup_step_index(void) {
    const unsigned steps = atomic_load(&r_test_cleanup_steps);
    if (steps == 0U || steps > R_TEST_STEPS) {
        atomic_store(&r_test_valid, 0);
        return 0U;
    }
    return steps - 1U;
}

static RRuntimeTaskStepStatus
r_test_cleanup_step(RRuntimeTaskExecution *execution, void *payload, void *result) {
    const unsigned step = atomic_fetch_add(&r_test_cleanup_steps, 1U);
    RRuntimeTaskStepStatus status;
    if (step >= R_TEST_STEPS) {
        atomic_store(&r_test_valid, 0);
        return r_test_steps[R_TEST_CLEANUP](execution, payload, result);
    }
    if (step == 1U) {
        /* The cancellation is requested before the resumed step observes its await. */
        r_test_wait(&r_test_cancels, 1U);
    }
    atomic_store(&r_test_cleanup_execution, execution);
    status = r_test_steps[R_TEST_CLEANUP](execution, payload, result);
    atomic_store(&r_test_cleanup_execution, NULL);
    atomic_store(&r_test_step_status[step], (int)status);
    if (step == 0U && status == R_RUNTIME_TASK_STEP_SUSPENDED) {
        /* Nothing resumes the task before the flag below releases hold. */
        const int32_t *trace = atomic_load(&r_test_trace);
        if (trace != NULL) {
            atomic_store(&r_test_trace_at_suspension, *trace);
        }
        atomic_store(&r_test_cleanup_suspended, 1U);
    }
    return status;
}

static RRuntimeTaskStepStatus
r_test_hold_step(RRuntimeTaskExecution *execution, void *payload, void *result) {
    r_test_wait(&r_test_cleanup_suspended, 1U);
    return r_test_steps[R_TEST_HOLD](execution, payload, result);
}

RRuntimeTaskPrepareResult r_test_prepare(RRuntimeTypeInfo payload_type,
                                         RRuntimeTypeInfo result_type,
                                         RRuntimeTaskStepFn step) {
    const unsigned task = atomic_fetch_add(&r_test_prepared, 1U);
    if (task < R_TEST_TASKS) {
        r_test_steps[task] = step;
        if (task == R_TEST_CLEANUP) {
            step = r_test_cleanup_step;
        } else if (task == R_TEST_HOLD) {
            step = r_test_hold_step;
        }
    }
    return r_runtime_task_resumable_start_prepare(payload_type, result_type, step);
}

RRuntimeTaskExecutionAwaitStatus
r_test_await(RRuntimeTaskExecution *execution, RRuntimeTask **task, void *result_storage) {
    const RRuntimeTaskExecutionAwaitStatus status =
        r_runtime_task_execution_await(execution, task, result_storage);
    if (execution == atomic_load(&r_test_cleanup_execution)) {
        const unsigned step = r_test_cleanup_step_index();
        atomic_fetch_add(&r_test_step_awaits[step], 1U);
        atomic_store(&r_test_step_await_status[step], (int)status);
        if (status == R_RUNTIME_TASK_EXECUTION_AWAIT_OK &&
            (*task != NULL || *(const int32_t *)result_storage != INT32_C(7))) {
            atomic_store(&r_test_valid, 0);
        }
    }
    return status;
}

_Bool r_test_cancel_requested(const RRuntimeTaskExecution *execution) {
    const _Bool requested = r_runtime_task_execution_cancel_requested(execution);
    if (execution == atomic_load(&r_test_cleanup_execution)) {
        atomic_fetch_add(&r_test_step_polls[r_test_cleanup_step_index()], 1U);
        atomic_store(&r_test_poll_result, requested);
    }
    return requested;
}

RRuntimeOwnStatus r_test_own_create(RRuntimeAllocator *allocator,
                                    RRuntimeTypeInfo type,
                                    void *value,
                                    RRuntimeOwn *result) {
    const RRuntimeOwnStatus status = r_runtime_own_create(allocator, type, value, result);
    /* The trace of suspended_cleanup is the only owner of the program. */
    if (status == R_RUNTIME_OWN_OK && type.size == sizeof(int32_t)) {
        atomic_store(&r_test_trace, (int32_t *)result->allocation);
    }
    return status;
}

void r_test_own_release(RRuntimeOwn *owner) {
    if (owner->allocation != NULL && owner->allocation == (void *)atomic_load(&r_test_trace)) {
        atomic_store(&r_test_trace_at_release, *(const int32_t *)owner->allocation);
    }
    atomic_fetch_add(&r_test_releases, 1U);
    r_runtime_own_release(owner);
}

void r_test_task_destroy(RRuntimeTask **task) {
    /* Every observation is consumed by await or cancel; none is left to destroy. */
    if (*task != NULL) {
        atomic_fetch_add(&r_test_destroyed, 1U);
    }
    r_runtime_task_destroy(task);
}

void r_test_async_cancel(RRuntimeTask **operation) {
    /* suspended_cleanup has resumed because child completed, before it observes the await. */
    r_test_wait(&r_test_cleanup_steps, 2U);
    r_std_async_cancel(operation);
    if (*operation != NULL) {
        atomic_store(&r_test_valid, 0);
    }
    atomic_fetch_add(&r_test_cancels, 1U);
}

int main(int argc, char *argv[]) {
    const int status = r_generated_main(argc, argv);
    int failure = 0;
    if (status != 0 || !atomic_load(&r_test_valid) || atomic_load(&r_test_prepared) != 4U) {
        failure = 1;
    } else if (atomic_load(&r_test_cleanup_steps) != 2U ||
               atomic_load(&r_test_step_status[0]) != (int)R_RUNTIME_TASK_STEP_SUSPENDED ||
               atomic_load(&r_test_step_awaits[0]) != 1U ||
               atomic_load(&r_test_step_await_status[0]) !=
                   (int)R_RUNTIME_TASK_EXECUTION_AWAIT_SUSPENDED ||
               atomic_load(&r_test_step_polls[0]) != 0U ||
               atomic_load(&r_test_trace_at_suspension) != INT32_C(0)) {
        failure = 2;
    } else if (atomic_load(&r_test_step_status[1]) != (int)R_RUNTIME_TASK_STEP_CANCELLED ||
               atomic_load(&r_test_step_awaits[1]) != 1U ||
               atomic_load(&r_test_step_await_status[1]) !=
                   (int)R_RUNTIME_TASK_EXECUTION_AWAIT_OK ||
               atomic_load(&r_test_step_polls[1]) != 1U || !atomic_load(&r_test_poll_result)) {
        failure = 3;
    } else if (atomic_load(&r_test_trace_at_release) != INT32_C(12) ||
               atomic_load(&r_test_releases) != 1U || atomic_load(&r_test_destroyed) != 0U ||
               atomic_load(&r_test_cancels) != 1U) {
        failure = 4;
    }
    if (failure != 0) {
        (void)fprintf(stderr,
                      "cancelled finally check %d failed: status %d, steps %u (%d, %d), awaits "
                      "%u/%u (%d, %d), polls %u/%u, trace %d -> %d, releases %u, destroyed %u\n",
                      failure,
                      status,
                      atomic_load(&r_test_cleanup_steps),
                      atomic_load(&r_test_step_status[0]),
                      atomic_load(&r_test_step_status[1]),
                      atomic_load(&r_test_step_awaits[0]),
                      atomic_load(&r_test_step_awaits[1]),
                      atomic_load(&r_test_step_await_status[0]),
                      atomic_load(&r_test_step_await_status[1]),
                      atomic_load(&r_test_step_polls[0]),
                      atomic_load(&r_test_step_polls[1]),
                      (int)atomic_load(&r_test_trace_at_suspension),
                      (int)atomic_load(&r_test_trace_at_release),
                      atomic_load(&r_test_releases),
                      atomic_load(&r_test_destroyed));
    }
    return failure;
}

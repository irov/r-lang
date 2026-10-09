#include "r_runtime_0_1.h"
#include "r_runtime_allocator.h"
#include "r_runtime_array.h"
#include "r_runtime_task.h"
#include "r_std_fs.h"
#include "r_std_thread.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

/*
 * R-SLIB-ASYNC-0002, R-SLIB-ASYNC-0003: async starts with named Move arguments. The programs of
 * async_move_args.r (named task values) and codegen_await_move_args.r (direct awaits) run the
 * same scenario, so the resumable starts arrive in the same order; the wrapper refuses some of
 * them and observes every drop of an argument:
 *
 *   start  program step                                      outcome
 *   1      main                                              started
 *   2, 3   resume_effect_probe and its child                 started; the child is held
 *   4      consume_array(17)                                 frame allocation fails
 *   5      consume_array(17) again                           started, drops the array
 *   6, 7   launch_array(23) and its first consume_array      the second fails to allocate
 *   8      the retry of launch_array                         started, drops the array
 *   9      consume_paths(first, second)                      frame allocation fails
 *   10     consume_paths(first, second) again                started, drops both paths
 *   11, 12 launch_paths(third, fourth) and its consume_paths started
 *   13-15  forward_array(41), (43, throws), (59, cancelled)  started
 *   16     consume_array(73)                                 prepare reports runtime stopping
 *   17     consume_array(79)                                 prepared; commit reports stopping
 *
 * A refused start shall leave its arguments with the caller (the program checks their contents)
 * and every array and path shall be dropped exactly once on every path.
 */
RRuntimeTaskPrepareResult r_test_prepare(RRuntimeTypeInfo payload_type,
                                         RRuntimeTypeInfo result_type,
                                         RRuntimeTaskStepFn step);
RRuntimeTaskStartResult r_test_commit(RRuntimeTask **transaction,
                                      RRuntimeTaskPayloadInitializeFn initialize,
                                      const void *context);
RRuntimeTaskStartResult r_test_commit_inline(RRuntimeTask **transaction,
                                             RRuntimeTaskPayloadInitializeFn initialize,
                                             const void *context,
                                             size_t step_stack_bytes);
RRuntimeTaskExecutionAwaitStatus r_test_task_execution_await(RRuntimeTaskExecution *execution,
                                                             RRuntimeTask **task,
                                                             void *result_storage);
void r_test_array_destroy(RRuntimeArray *array);
void r_test_path_destroy(RStdFsPath *path);
void r_test_sleep_nanoseconds(uint64_t nanoseconds);
void r_test_yield_now(void);

#define r_runtime_array_destroy r_test_array_destroy
#define r_runtime_task_execution_await r_test_task_execution_await
#define r_runtime_task_resumable_start_prepare r_test_prepare
#define r_runtime_task_start_commit_initialize r_test_commit
#define r_runtime_task_start_commit_initialize_inline r_test_commit_inline
#define r_std_fs_path_destroy r_test_path_destroy
#define r_std_thread_sleep_nanoseconds r_test_sleep_nanoseconds
#define r_std_thread_yield_now r_test_yield_now
#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_PROGRAM_PRELUDE
#undef main
#undef r_std_thread_yield_now
#undef r_std_thread_sleep_nanoseconds
#undef r_std_fs_path_destroy
#undef r_runtime_task_start_commit_initialize_inline
#undef r_runtime_task_start_commit_initialize
#undef r_runtime_task_resumable_start_prepare
#undef r_runtime_task_execution_await
#undef r_runtime_array_destroy

enum {
    R_TEST_STARTS = 17,
    R_TEST_STOPPED_START = 16,
    R_TEST_UNCOMMITTED_START = 17
};

static const uint8_t r_test_markers[] = {17U, 23U, 41U, 43U, 59U, 73U, 79U};

static atomic_uint r_test_starts;
static atomic_uint r_test_refused_allocations;
static atomic_uint r_test_drops[sizeof(r_test_markers)];
static atomic_uint r_test_path_drops;
static atomic_uint r_test_yields;
static atomic_uint r_test_held_children;
static atomic_uint r_test_suspended_probes;
static atomic_bool r_test_invalid;
static atomic_bool r_test_refuse_commit;
static _Thread_local _Bool r_test_probe_awaits;

static pthread_mutex_t r_test_hold_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t r_test_hold_condition = PTHREAD_COND_INITIALIZER;
static _Bool r_test_child_released;

static void r_test_fail(void) {
    atomic_store_explicit(&r_test_invalid, 1, memory_order_relaxed);
}

RRuntimeTaskPrepareResult r_test_prepare(RRuntimeTypeInfo payload_type,
                                         RRuntimeTypeInfo result_type,
                                         RRuntimeTaskStepFn step) {
    const unsigned int start =
        atomic_fetch_add_explicit(&r_test_starts, 1U, memory_order_relaxed) + 1U;
    RRuntimeAllocator *const allocator = r_runtime_hosted_allocator();
    const _Bool refuse = (start == 4U) || (start == 7U) || (start == 9U);
    RRuntimeTaskPrepareResult result;

    if (start == R_TEST_STOPPED_START) {
        result.transaction = NULL;
        result.status = R_RUNTIME_TASK_START_RUNTIME_STOPPING;
        return result;
    }
    if (refuse) {
        /* The program runs nothing else meanwhile, so the frame is the first attempt. */
        r_runtime_allocator_set_failure(allocator, UINT64_C(1));
    }
    result = r_runtime_task_resumable_start_prepare(payload_type, result_type, step);
    if (refuse) {
        if ((result.status != R_RUNTIME_TASK_START_ALLOCATION_FAILED) ||
            (result.transaction != NULL) ||
            (r_runtime_allocator_attempt_count(allocator) != UINT64_C(1))) {
            r_test_fail();
        }
        r_runtime_allocator_set_failure(allocator, UINT64_C(0));
        (void)atomic_fetch_add_explicit(&r_test_refused_allocations, 1U, memory_order_relaxed);
    }
    if (start == R_TEST_UNCOMMITTED_START) {
        atomic_store_explicit(&r_test_refuse_commit, 1, memory_order_relaxed);
    }
    return result;
}

/* A runtime that stops between prepare and commit rolls the reserved frame back and runs no
   payload initialization, so the staged arguments stay with the caller. */
static _Bool r_test_commit_refused(RRuntimeTask **transaction, RRuntimeTaskStartResult *result) {
    if (!atomic_exchange_explicit(&r_test_refuse_commit, 0, memory_order_relaxed)) {
        return 0;
    }
    r_runtime_task_start_abort(transaction);
    result->task = NULL;
    result->status = R_RUNTIME_TASK_START_RUNTIME_STOPPING;
    return 1;
}

RRuntimeTaskStartResult r_test_commit(RRuntimeTask **transaction,
                                      RRuntimeTaskPayloadInitializeFn initialize,
                                      const void *context) {
    RRuntimeTaskStartResult result;

    if (r_test_commit_refused(transaction, &result)) {
        return result;
    }
    return r_runtime_task_start_commit_initialize(transaction, initialize, context);
}

RRuntimeTaskStartResult r_test_commit_inline(RRuntimeTask **transaction,
                                             RRuntimeTaskPayloadInitializeFn initialize,
                                             const void *context,
                                             size_t step_stack_bytes) {
    RRuntimeTaskStartResult result;

    if (r_test_commit_refused(transaction, &result)) {
        return result;
    }
    return r_runtime_task_start_commit_initialize_inline(
        transaction, initialize, context, step_stack_bytes);
}

static void r_test_release_child(void) {
    if (pthread_mutex_lock(&r_test_hold_mutex) != 0) {
        r_test_fail();
        return;
    }
    r_test_child_released = 1;
    if (pthread_cond_broadcast(&r_test_hold_condition) != 0) {
        r_test_fail();
    }
    (void)pthread_mutex_unlock(&r_test_hold_mutex);
}

/* The await that follows the yield of resume_effect_probe on the same thread is the await of
   its held child, which cannot have completed, so the probe shall suspend there. Releasing the
   child only after that registration makes the resumed step deterministic. */
RRuntimeTaskExecutionAwaitStatus r_test_task_execution_await(RRuntimeTaskExecution *execution,
                                                             RRuntimeTask **task,
                                                             void *result_storage) {
    const _Bool probe = r_test_probe_awaits;
    RRuntimeTaskExecutionAwaitStatus status;

    r_test_probe_awaits = 0;
    status = r_runtime_task_execution_await(execution, task, result_storage);
    if (probe) {
        if (status == R_RUNTIME_TASK_EXECUTION_AWAIT_SUSPENDED) {
            (void)atomic_fetch_add_explicit(&r_test_suspended_probes, 1U, memory_order_relaxed);
        } else {
            r_test_fail();
        }
        r_test_release_child();
    }
    return status;
}

void r_test_yield_now(void) {
    (void)atomic_fetch_add_explicit(&r_test_yields, 1U, memory_order_relaxed);
    r_test_probe_awaits = 1;
    r_std_thread_yield_now();
}

/* Holds the child of the probe until the probe has suspended on it; the bound only turns a
   lowering that never reaches the await into a failure instead of a hang. */
void r_test_sleep_nanoseconds(uint64_t nanoseconds) {
    struct timespec deadline;
    int wait_status = 0;

    (void)atomic_fetch_add_explicit(&r_test_held_children, 1U, memory_order_relaxed);
    if ((timespec_get(&deadline, TIME_UTC) != TIME_UTC) ||
        (pthread_mutex_lock(&r_test_hold_mutex) != 0)) {
        r_test_fail();
        return;
    }
    deadline.tv_sec += 30;
    while (!r_test_child_released && (wait_status == 0)) {
        wait_status = pthread_cond_timedwait(&r_test_hold_condition, &r_test_hold_mutex, &deadline);
    }
    if (!r_test_child_released) {
        r_test_fail();
    }
    (void)pthread_mutex_unlock(&r_test_hold_mutex);
    r_std_thread_sleep_nanoseconds(nanoseconds);
}

void r_test_array_destroy(RRuntimeArray *array) {
    if ((array != NULL) && (array->data != NULL)) {
        size_t index;

        for (index = 0U; index < sizeof(r_test_markers); ++index) {
            if ((array->length == 1U) && (*(const uint8_t *)array->data == r_test_markers[index])) {
                (void)atomic_fetch_add_explicit(&r_test_drops[index], 1U, memory_order_relaxed);
            }
        }
    }
    r_runtime_array_destroy(array);
}

void r_test_path_destroy(RStdFsPath *path) {
    if ((path != NULL) && (path->storage != NULL)) {
        (void)atomic_fetch_add_explicit(&r_test_path_drops, 1U, memory_order_relaxed);
    }
    r_std_fs_path_destroy(path);
}

int main(int argc, char *argv[]) {
    const int status = r_generated_main(argc, argv);
    size_t index;

    if (status != 0) {
        return status;
    }
    if (atomic_load(&r_test_invalid) || atomic_load(&r_test_refuse_commit)) {
        return 71;
    }
    if ((atomic_load(&r_test_starts) != (unsigned int)R_TEST_STARTS) ||
        (atomic_load(&r_test_refused_allocations) != 3U)) {
        return 72;
    }
    /* The effects between the start and the await of the probe ran once although it resumed. */
    if ((atomic_load(&r_test_yields) != 1U) || (atomic_load(&r_test_held_children) != 1U) ||
        (atomic_load(&r_test_suspended_probes) != 1U)) {
        return 73;
    }
    /* The hosted drain has waited for the cancelled task, so its array is dropped too. */
    for (index = 0U; index < sizeof(r_test_markers); ++index) {
        if (atomic_load(&r_test_drops[index]) != 1U) {
            return 74;
        }
    }
    if (atomic_load(&r_test_path_drops) != 4U) {
        return 75;
    }
    return 0;
}

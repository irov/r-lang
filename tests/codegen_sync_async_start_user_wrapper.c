/*
 * A synchronous caller starts a user async function with a named owned buffer (Core
 * R-FUNC-0010). The fixture's main calls start_with_owned_buffer twice; the wrapper refuses the
 * frame allocation of the first start and stops the executor before the second. Each start then
 * reports its failure without running the child, the caller still owns the buffer unchanged
 * when its catch clause drops it, and neither the failed start nor the catch clause allocates:
 * the refused attempt is the only allocation attempt of the first call, and the second makes
 * none.
 */
#include "r_runtime_0_1.h"
#include "r_runtime_allocator.h"
#include "r_runtime_array.h"
#include "r_runtime_task.h"
#include "r_runtime_type.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

RRuntimeTaskPrepareResult r_test_resumable_start_prepare(RRuntimeTypeInfo payload_type,
                                                         RRuntimeTypeInfo result_type,
                                                         RRuntimeTaskStepFn step);
void r_test_array_destroy(RRuntimeArray *array);

#define r_runtime_task_resumable_start_prepare r_test_resumable_start_prepare
#define r_runtime_array_destroy r_test_array_destroy
#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_PROGRAM_PRELUDE
#undef main
#undef r_runtime_array_destroy
#undef r_runtime_task_resumable_start_prepare

/* The program's main runs synchronously on the initial thread, so the hooks need no
   synchronization. */
static RRuntimeTaskStepFn r_test_child_step;
static size_t r_test_child_step_count;
static size_t r_test_start_count;
static size_t r_test_destroy_count;
static int r_test_failed_line;

#define R_TEST_REQUIRE(condition)                                                                  \
    do {                                                                                           \
        if (!(condition) && r_test_failed_line == 0) {                                             \
            r_test_failed_line = __LINE__;                                                         \
        }                                                                                          \
    } while (0)

static RRuntimeTaskStepStatus
r_test_counted_child_step(RRuntimeTaskExecution *execution, void *payload, void *result) {
    r_test_child_step_count += 1U;
    return r_test_child_step(execution, payload, result);
}

RRuntimeTaskPrepareResult r_test_resumable_start_prepare(RRuntimeTypeInfo payload_type,
                                                         RRuntimeTypeInfo result_type,
                                                         RRuntimeTaskStepFn step) {
    RRuntimeAllocator *allocator = r_runtime_hosted_allocator();
    RRuntimeTaskPrepareResult prepared;

    r_test_start_count += 1U;
    r_test_child_step = step;
    if (r_test_start_count == 1U) {
        /* The frame reservation is the first allocation attempt and is refused. */
        r_runtime_allocator_set_failure(allocator, UINT64_C(1));
        prepared = r_runtime_task_resumable_start_prepare(
            payload_type, result_type, r_test_counted_child_step);
        R_TEST_REQUIRE(prepared.transaction == NULL &&
                       prepared.status == R_RUNTIME_TASK_START_ALLOCATION_FAILED);
        R_TEST_REQUIRE(r_runtime_allocator_attempt_count(allocator) == UINT64_C(1));
        return prepared;
    }
    /* A stopped executor refuses the start before it allocates. The executor is started again
       right away, since the hosted finish of the program stops it; the attempt count is reset
       after that restart so that the catch clause is measured alone. */
    R_TEST_REQUIRE(r_test_start_count == 2U);
    R_TEST_REQUIRE(r_runtime_executor_lifecycle_stop());
    r_runtime_allocator_set_failure(allocator, UINT64_C(0));
    prepared = r_runtime_task_resumable_start_prepare(
        payload_type, result_type, r_test_counted_child_step);
    R_TEST_REQUIRE(prepared.transaction == NULL &&
                   prepared.status == R_RUNTIME_TASK_START_RUNTIME_STOPPING);
    R_TEST_REQUIRE(r_runtime_allocator_attempt_count(allocator) == UINT64_C(0));
    R_TEST_REQUIRE(r_runtime_executor_lifecycle_start(allocator) == R_RUNTIME_EXECUTOR_START_OK);
    r_runtime_allocator_set_failure(allocator, UINT64_C(0));
    return prepared;
}

/* The catch clause of start_with_owned_buffer drops the retained buffer: it still holds the one
   byte main stored, and nothing was allocated since the failed start. */
void r_test_array_destroy(RRuntimeArray *array) {
    const uint8_t expected = r_test_destroy_count == 0U ? UINT8_C(17) : UINT8_C(29);
    const uint64_t attempts = r_test_destroy_count == 0U ? UINT64_C(1) : UINT64_C(0);
    const uint8_t *first = array->length == 1U ? r_runtime_array_get(array, 0U) : NULL;

    r_test_destroy_count += 1U;
    R_TEST_REQUIRE(r_test_destroy_count == r_test_start_count);
    R_TEST_REQUIRE(first != NULL && *first == expected);
    R_TEST_REQUIRE(r_runtime_allocator_attempt_count(array->allocator) == attempts);
    r_runtime_array_destroy(array);
}

int main(int argc, char *argv[]) {
    const int status = r_generated_main(argc, argv);

    if (r_test_failed_line != 0) {
        (void)fprintf(stderr, "async start check failed at line %d\n", r_test_failed_line);
        return r_test_failed_line;
    }
    if (status != 0 || r_test_start_count != 2U || r_test_destroy_count != 2U ||
        r_test_child_step_count != 0U) {
        (void)fprintf(stderr, "async start check failed: status %d\n", status);
        return __LINE__;
    }
    return 0;
}

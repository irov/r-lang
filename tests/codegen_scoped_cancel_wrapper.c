#include "r_runtime_0_1.h"
#include "r_runtime_own.h"
#include "r_runtime_task.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#ifndef R_TEST_EXPECTED_OWN_DROPS
#define R_TEST_EXPECTED_OWN_DROPS 1U
#endif

#ifndef R_TEST_ROOT_START
#define R_TEST_ROOT_START r_async_start_00000003_gate
#define R_TEST_ROOT_INITIALIZE r_async_frame_initialize_00000003_gate
#endif

static RRuntimeAllocator allocator;
static _Atomic unsigned entered, draining, released;
static _Atomic _Bool proceed, valid = 1;
static RRuntimeAllocator *r_test_allocator(void) {
    return &allocator;
}
static RRuntimeTaskExecutionAwaitStatus
r_test_await(RRuntimeTaskExecution *execution, RRuntimeTask **task, void *result);
static void r_test_cancel(RRuntimeTaskScope *scope);
static void r_test_release(RRuntimeOwn *owner);
#define r_runtime_hosted_allocator r_test_allocator
#define r_runtime_task_execution_await r_test_await
#define r_runtime_task_scope_cancel r_test_cancel
#define r_runtime_own_release r_test_release
#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_GENERATED_C
#undef main
#undef r_runtime_own_release
#undef r_runtime_task_scope_cancel
#undef r_runtime_task_execution_await
#undef r_runtime_hosted_allocator

static void r_test_pause(void) {
    const struct timespec interval = {0, 1000000L};
    (void)nanosleep(&interval, NULL);
}
static RRuntimeTaskExecutionAwaitStatus
r_test_await(RRuntimeTaskExecution *execution, RRuntimeTask **task, void *result) {
    atomic_fetch_add(&entered, 1U);
    /* A native operation retains the borrowed frame until its explicit acknowledgement. */
    while (!atomic_load(&proceed))
        r_test_pause();
    return r_runtime_task_execution_await(execution, task, result);
}
static void r_test_cancel(RRuntimeTaskScope *scope) {
    r_runtime_task_scope_cancel(scope);
    atomic_fetch_add(&draining, 1U);
}
static void r_test_release(RRuntimeOwn *owner) {
    if (!atomic_load(&proceed) || owner->allocation == NULL ||
        *(const int32_t *)owner->allocation != INT32_C(9))
        atomic_store(&valid, 0);
    atomic_fetch_add(&released, 1U);
    r_runtime_own_release(owner);
}
#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            (void)fprintf(stderr, "scoped cancellation check failed at line %d\n", __LINE__);      \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    CHECK(r_runtime_stack_initialize_current_thread());
    r_runtime_allocator_initialize(&allocator);
    CHECK(r_runtime_executor_lifecycle_start(&allocator) == R_RUNTIME_EXECUTOR_START_OK);
    RRuntimeTaskStartResult root = R_TEST_ROOT_START(R_TEST_ROOT_INITIALIZE, NULL, UINT32_C(0));
    CHECK(root.status == R_RUNTIME_TASK_START_OK);
    unsigned attempts = 0U;
    while ((atomic_load(&entered) == 0U ||
            r_runtime_task_state(root.task) != R_RUNTIME_TASK_SUSPENDED) &&
           attempts++ < 5000U)
        r_test_pause();
    CHECK(attempts < 5000U && atomic_load(&released) == 0U);
    r_runtime_task_cancel(&root.task);
    CHECK(root.task == NULL);
    attempts = 0U;
    while (atomic_load(&draining) == 0U && attempts++ < 5000U)
        r_test_pause();
    CHECK(attempts < 5000U && atomic_load(&released) == 0U);
    atomic_store(&proceed, 1);
    CHECK(r_runtime_executor_lifecycle_stop());
    CHECK(atomic_load(&valid) && atomic_load(&released) == R_TEST_EXPECTED_OWN_DROPS &&
          atomic_load(&entered) == 1U);
    return 0;
}

#include "r_runtime_own.h"
#include "r_runtime_task.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static RRuntimeTaskExecutionAwaitStatus
r_test_await(RRuntimeTaskExecution *execution, RRuntimeTask **task, void *result);
static _Bool r_test_cancel_requested(const RRuntimeTaskExecution *execution);
static void r_test_release(RRuntimeOwn *owner);
static RRuntimeAllocator *r_test_allocator(void);

#define r_runtime_task_execution_await r_test_await
#define r_runtime_task_execution_cancel_requested r_test_cancel_requested
#define r_runtime_own_release r_test_release
#define r_runtime_hosted_allocator r_test_allocator
#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_GENERATED_C
#undef main
#undef r_runtime_hosted_allocator
extern RRuntimeAllocator *r_runtime_hosted_allocator(void);
#undef r_runtime_own_release
#undef r_runtime_task_execution_cancel_requested
#undef r_runtime_task_execution_await

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            (void)fprintf(stderr, "expression suspension check failed at line %d\n", __LINE__);    \
            return __LINE__;                                                                       \
        }                                                                                          \
    } while (0)

static RRuntimeAllocator r_test_local_allocator;
static _Bool r_test_manual = 1;
static RRuntimeAllocator *r_test_allocator(void) {
    return r_test_manual ? &r_test_local_allocator : r_runtime_hosted_allocator();
}

static max_align_t r_test_execution;
static max_align_t r_test_child;
static unsigned r_test_await_count;
static unsigned r_test_release_count;
static _Bool r_test_cancelled;
static _Bool r_test_drop_order = 1;

static void r_test_release(RRuntimeOwn *owner) {
    if (owner->allocation == NULL || *(const int32_t *)owner->allocation != 9)
        r_test_drop_order = 0;
    r_test_release_count += 1U;
    r_runtime_own_release(owner);
}

static _Bool r_test_cancel_requested(const RRuntimeTaskExecution *execution) {
    if (execution == (const RRuntimeTaskExecution *)(const void *)&r_test_execution)
        return 0;
    return r_runtime_task_execution_cancel_requested(execution);
}

static RRuntimeTaskExecutionAwaitStatus
r_test_await(RRuntimeTaskExecution *execution, RRuntimeTask **task, void *result) {
    if (execution != (RRuntimeTaskExecution *)(void *)&r_test_execution)
        return r_runtime_task_execution_await(execution, task, result);
    if (*task != (RRuntimeTask *)(void *)&r_test_child || result == NULL)
        return R_RUNTIME_TASK_EXECUTION_AWAIT_INVALID;
    r_test_await_count += 1U;
    if (r_test_await_count == 1U)
        return R_RUNTIME_TASK_EXECUTION_AWAIT_SUSPENDED;
    *task = NULL;
    if (r_test_cancelled)
        return R_RUNTIME_TASK_EXECUTION_AWAIT_CANCELLED;
    *(int32_t *)result = 20;
    return R_RUNTIME_TASK_EXECUTION_AWAIT_OK;
}

static int r_test_resume(_Bool cancelled) {
    r_async_frame_00000005 frame = {0};
    RRuntimeTaskExecution *execution = (RRuntimeTaskExecution *)(void *)&r_test_execution;
    int32_t result = -1;
    const unsigned released = r_test_release_count;
    frame.r_state = 1U;
    frame.r_p00000000 = (RRuntimeTask *)(void *)&r_test_child;
    frame.r_p00000000_initialized = 1;
    r_test_await_count = 0U;
    r_test_cancelled = cancelled;
    CHECK(r_async_step_00000005(execution, &frame, &result) == R_RUNTIME_TASK_STEP_SUSPENDED);
    CHECK(frame.r_p00000000_initialized);
    CHECK(r_test_release_count == released && result == -1);
    CHECK(r_async_step_00000005(execution, &frame, &result) ==
          (cancelled ? R_RUNTIME_TASK_STEP_CANCELLED : R_RUNTIME_TASK_STEP_COMPLETED));
    CHECK(!frame.r_p00000000_initialized && frame.r_p00000000 == NULL);
    CHECK(result == (cancelled ? -1 : 42));
    CHECK(r_test_await_count == 2U);
    r_async_frame_drop_00000005(&frame);
    CHECK(r_test_release_count == released + 1U && r_test_drop_order);
    return 0;
}

int main(int argc, char *argv[]) {
    CHECK(r_runtime_stack_initialize_current_thread());
    r_runtime_allocator_initialize(&r_test_local_allocator);
    CHECK(r_test_resume(0) == 0);
    CHECK(r_test_resume(1) == 0);
    r_test_manual = 0;
    CHECK(r_generated_main(argc, argv) == 0);
    CHECK(r_test_release_count == 3U && r_test_drop_order);
    return 0;
}

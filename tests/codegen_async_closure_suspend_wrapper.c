#include "r_runtime_own.h"
#include "r_runtime_task.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static RRuntimeTaskExecutionAwaitStatus
r_test_await(RRuntimeTaskExecution *execution, RRuntimeTask **task, void *result);
static _Bool r_test_cancel_requested(const RRuntimeTaskExecution *execution);
static void r_test_release(RRuntimeOwn *owner);

#define r_runtime_task_execution_await r_test_await
#define r_runtime_task_execution_cancel_requested r_test_cancel_requested
#define r_runtime_own_release r_test_release
#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_GENERATED_C
#undef main
#undef r_runtime_own_release
#undef r_runtime_task_execution_cancel_requested
#undef r_runtime_task_execution_await

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            (void)fprintf(stderr, "closure suspension check failed at line %d\n", __LINE__);       \
            return __LINE__;                                                                       \
        }                                                                                          \
    } while (0)

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
    r_async_frame_00000004 frame = {0};
    RRuntimeAllocator allocator;
    RRuntimeTaskExecution *execution = (RRuntimeTaskExecution *)(void *)&r_test_execution;
    const RRuntimeTypeInfo type = {sizeof(int32_t), _Alignof(int32_t), NULL, NULL};
    int32_t captured = 22;
    int32_t result = -1;
    const unsigned released = r_test_release_count;
    r_runtime_allocator_initialize(&allocator);
    CHECK(r_runtime_own_create(
              &allocator, type, &captured, &frame.r_p00000000.r_m00000001.r_m00000001) ==
          R_RUNTIME_OWN_OK);
    frame.r_state = 1U;
    frame.r_p00000000_initialized = 1;
    frame.r_p00000001 = (RRuntimeTask *)(void *)&r_test_child;
    frame.r_p00000001_initialized = 1;
    r_test_await_count = 0U;
    r_test_cancelled = cancelled;
    CHECK(r_async_step_00000004(execution, &frame, &result) == R_RUNTIME_TASK_STEP_SUSPENDED);
    CHECK(frame.r_p00000000_initialized && frame.r_p00000001_initialized);
    CHECK(r_test_release_count == released && result == -1);
    CHECK(r_async_step_00000004(execution, &frame, &result) ==
          (cancelled ? R_RUNTIME_TASK_STEP_CANCELLED : R_RUNTIME_TASK_STEP_COMPLETED));
    CHECK(!frame.r_p00000001_initialized && frame.r_p00000001 == NULL);
    CHECK(frame.r_p00000000_initialized == cancelled);
    CHECK(result == (cancelled ? -1 : 42));
    CHECK(r_test_await_count == 2U);
    r_async_frame_drop_00000004(&frame);
    CHECK(r_test_release_count == released + 1U && r_test_drop_order);
    return 0;
}

int main(int argc, char *argv[]) {
    CHECK(r_runtime_stack_initialize_current_thread());
    CHECK(r_test_resume(0) == 0);
    CHECK(r_test_resume(1) == 0);
    CHECK(r_generated_main(argc, argv) == 0);
    CHECK(r_test_release_count == 3U && r_test_drop_order);
    return 0;
}

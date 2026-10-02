#include "r_runtime_task.h"
#include "r_std_async.h"

#include <stddef.h>
#include <stdint.h>

#ifndef R_TEST_TASK_SLOT
#define R_TEST_TASK_SLOT r_l00000001
#define R_TEST_TASK_INITIALIZED r_l00000001_initialized
#endif

static _Bool r_test_cancel_requested(const RRuntimeTaskExecution *execution);
static RRuntimeTaskExecutionAwaitStatus
r_test_await(RRuntimeTaskExecution *execution, RRuntimeTask **task, void *result_storage);

#define r_runtime_task_execution_await r_test_await
#define r_runtime_task_execution_cancel_requested r_test_cancel_requested
#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_GENERATED_C
#undef main
#undef r_runtime_task_execution_cancel_requested
#undef r_runtime_task_execution_await

static max_align_t r_test_execution_storage;
static max_align_t r_test_task_storage;
static size_t r_test_await_count;
static size_t r_test_cancel_poll_count;

static _Bool r_test_cancel_requested(const RRuntimeTaskExecution *execution) {
    (void)execution;
    r_test_cancel_poll_count += 1U;
    return 1;
}

static RRuntimeTaskExecutionAwaitStatus
r_test_await(RRuntimeTaskExecution *execution, RRuntimeTask **task, void *result_storage) {
    RRuntimeTask *const fake_task = (RRuntimeTask *)(void *)&r_test_task_storage;

    (void)execution;
    if ((task == NULL) || (*task != fake_task) || (result_storage == NULL)) {
        return R_RUNTIME_TASK_EXECUTION_AWAIT_INVALID;
    }
    r_test_await_count += 1U;
    if (r_test_await_count == 1U) {
        return R_RUNTIME_TASK_EXECUTION_AWAIT_SUSPENDED;
    }
    if (r_test_await_count == 2U) {
        *(int32_t *)result_storage = INT32_C(7);
        *task = NULL;
        return R_RUNTIME_TASK_EXECUTION_AWAIT_OK;
    }
    return R_RUNTIME_TASK_EXECUTION_AWAIT_INVALID;
}

int main(void) {
    RRuntimeTaskExecution *const fake_execution =
        (RRuntimeTaskExecution *)(void *)&r_test_execution_storage;
    RRuntimeTask *const fake_task = (RRuntimeTask *)(void *)&r_test_task_storage;
    r_async_frame_00000003 frame = {0};
    r_d00000001 completion = {0};
    RRuntimeTaskStepStatus status;

    frame.r_state = UINT32_C(10);
    frame.r_l00000000 = INT32_C(0);
    frame.R_TEST_TASK_SLOT = fake_task;
    frame.R_TEST_TASK_INITIALIZED = 1;
    frame.r_finally_depth = UINT32_C(2);
    frame.r_finally_stack[0] = UINT32_C(1);
    frame.r_finally_stack[1] = UINT32_C(2);
    frame.r_pending_depth = UINT32_C(0);
    completion.r_tag = UINT32_MAX;
    r_test_await_count = 0U;
    r_test_cancel_poll_count = 0U;

    status = r_async_step_00000003(fake_execution, &frame, &completion);
    if ((status != R_RUNTIME_TASK_STEP_SUSPENDED) || (frame.r_state != UINT32_C(10)) ||
        (frame.r_l00000000 != INT32_C(0)) || (frame.R_TEST_TASK_SLOT != fake_task) ||
        !frame.R_TEST_TASK_INITIALIZED || (frame.r_finally_depth != UINT32_C(2)) ||
        (frame.r_pending_depth != UINT32_C(0)) || (completion.r_tag != UINT32_MAX) ||
        (r_test_await_count != 1U) || (r_test_cancel_poll_count != 0U)) {
        return 1;
    }

    status = r_async_step_00000003(fake_execution, &frame, &completion);
    if ((status != R_RUNTIME_TASK_STEP_CANCELLED) || (frame.r_state != UINT32_C(13)) ||
        (frame.r_l00000000 != INT32_C(12)) || (frame.R_TEST_TASK_SLOT != NULL) ||
        frame.R_TEST_TASK_INITIALIZED || (frame.r_finally_depth != UINT32_C(0)) ||
        (frame.r_pending_depth != UINT32_C(0)) || (completion.r_tag != UINT32_MAX) ||
        (r_test_await_count != 2U) || (r_test_cancel_poll_count != 1U)) {
        return 2;
    }
    r_async_frame_drop_00000003(&frame);
    return (frame.r_l00000000 == INT32_C(12)) && (r_test_await_count == 2U) &&
                   (r_test_cancel_poll_count == 1U)
               ? 0
               : 3;
}

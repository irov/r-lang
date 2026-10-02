#include "r_runtime_allocator.h"
#include "r_runtime_array.h"
#include "r_runtime_task.h"
#include "r_runtime_type.h"
#include "r_std_fs.h"

#include <stddef.h>
#include <stdint.h>

static RRuntimeTaskExecutionAwaitStatus r_test_task_execution_await(
    RRuntimeTaskExecution *execution, RRuntimeTask **task, void *result_storage);
static _Bool r_test_task_execution_cancel_requested(const RRuntimeTaskExecution *execution);

#define r_runtime_task_execution_await r_test_task_execution_await
#define r_runtime_task_execution_cancel_requested r_test_task_execution_cancel_requested
#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_GENERATED_C
#undef main
#undef r_runtime_task_execution_cancel_requested
#undef r_runtime_task_execution_await

static max_align_t r_test_fake_execution_storage;
static max_align_t r_test_fake_task_storage;
static size_t r_test_fake_await_count;

static _Bool r_test_task_execution_cancel_requested(const RRuntimeTaskExecution *execution) {
    if (execution == (const RRuntimeTaskExecution *)(const void *)&r_test_fake_execution_storage) {
        return 0;
    }
    return r_runtime_task_execution_cancel_requested(execution);
}

static RRuntimeTaskExecutionAwaitStatus r_test_task_execution_await(
    RRuntimeTaskExecution *execution, RRuntimeTask **task, void *result_storage) {
    RRuntimeTask *const fake_task = (RRuntimeTask *)(void *)&r_test_fake_task_storage;

    if ((task != NULL) && (*task == fake_task)) {
        (void)execution;
        (void)result_storage;
        r_test_fake_await_count += 1U;
        if (r_test_fake_await_count == 1U) {
            return R_RUNTIME_TASK_EXECUTION_AWAIT_SUSPENDED;
        }
        if (r_test_fake_await_count == 2U) {
            *task = NULL;
            return R_RUNTIME_TASK_EXECUTION_AWAIT_OK;
        }
        return R_RUNTIME_TASK_EXECUTION_AWAIT_INVALID;
    }
    return r_runtime_task_execution_await(execution, task, result_storage);
}

static _Bool r_test_resume_does_not_repeat_effect(void) {
    RRuntimeTaskExecution *const fake_execution =
        (RRuntimeTaskExecution *)(void *)&r_test_fake_execution_storage;
    RRuntimeTask *const fake_task = (RRuntimeTask *)(void *)&r_test_fake_task_storage;
    r_async_frame_00000008 frame = {0};
    int32_t result = INT32_C(-1);
    RRuntimeTaskStepStatus status;

    frame.r_state = UINT32_C(6);
    frame.r_l00000000 = INT32_C(1);
    frame.r_l00000001 = fake_task;
    frame.r_l00000001_initialized = 1;
    r_test_fake_await_count = 0U;

    status = r_async_step_00000008(fake_execution, &frame, &result);
    if ((status != R_RUNTIME_TASK_STEP_SUSPENDED) || (frame.r_state != UINT32_C(6)) ||
        (frame.r_l00000000 != INT32_C(1)) || (frame.r_l00000001 != fake_task) ||
        !frame.r_l00000001_initialized || (r_test_fake_await_count != 1U)) {
        return 0;
    }

    status = r_async_step_00000008(fake_execution, &frame, &result);
    return (status == R_RUNTIME_TASK_STEP_COMPLETED) && (result == INT32_C(1)) &&
           (frame.r_l00000000 == INT32_C(1)) && (frame.r_l00000001 == NULL) &&
           !frame.r_l00000001_initialized && (r_test_fake_await_count == 2U);
}

static RRuntimeTypeInfo r_test_u8_type(void) {
    const RRuntimeTypeInfo type = {
        sizeof(uint8_t),
        _Alignof(uint8_t),
        NULL,
        NULL,
    };

    return type;
}

static _Bool r_test_array_create(RRuntimeAllocator *allocator, uint8_t byte, RRuntimeArray *array) {
    if (r_runtime_array_with_capacity(array, allocator, r_test_u8_type(), 1U) !=
        R_RUNTIME_ARRAY_OK) {
        return 0;
    }
    if (r_runtime_array_push(array, &byte) != R_RUNTIME_ARRAY_OK) {
        r_runtime_array_destroy(array);
        return 0;
    }
    return 1;
}

static _Bool
r_test_array_matches(const RRuntimeArray *array, const RRuntimeArray *snapshot, uint8_t byte) {
    return (array->allocator == snapshot->allocator) &&
           (array->element.size == snapshot->element.size) &&
           (array->element.alignment == snapshot->element.alignment) &&
           (array->element.move_initialize == snapshot->element.move_initialize) &&
           (array->element.drop == snapshot->element.drop) && (array->data == snapshot->data) &&
           (array->length == snapshot->length) && (array->capacity == snapshot->capacity) &&
           (array->length == 1U) && (*(const uint8_t *)array->data == byte);
}

static _Bool r_test_array_is_moved(const RRuntimeArray *array) {
    return (array->allocator == NULL) && (array->element.size == 0U) &&
           (array->element.alignment == 0U) && (array->element.move_initialize == NULL) &&
           (array->element.drop == NULL) && (array->data == NULL) && (array->length == 0U) &&
           (array->capacity == 0U);
}

static _Bool r_test_path_create(RRuntimeAllocator *allocator, RStdFsPath *path) {
    void *storage = NULL;

    if (r_runtime_allocator_allocate(allocator, 1U, _Alignof(max_align_t), &storage) !=
        R_RUNTIME_ALLOCATION_OK) {
        return 0;
    }
    path->storage = storage;
    return 1;
}

static _Bool r_test_allocation_failure_preserves_array(RRuntimeAllocator *allocator) {
    RRuntimeArray array = {0};
    RRuntimeArray snapshot;
    r_d00000005 started = {0};
    const uint8_t byte = UINT8_C(17);

    r_runtime_allocator_set_failure(allocator, UINT64_C(0));
    if (!r_test_array_create(allocator, byte, &array)) {
        return 0;
    }
    snapshot = array;
    r_runtime_allocator_set_failure(allocator, UINT64_C(1));
    r_f00000001(&started, &array);
    if ((started.r_tag != UINT32_C(1)) ||
        (started.r_payload.r_error_00000001 != R_STD_ASYNC_START_ALLOCATION_FAILED) ||
        (r_runtime_allocator_attempt_count(allocator) != UINT64_C(1)) ||
        !r_test_array_matches(&array, &snapshot, byte)) {
        r_type_drop_d00000005(&started);
        r_runtime_array_destroy(&array);
        return 0;
    }

    r_runtime_allocator_set_failure(allocator, UINT64_C(0));
    r_f00000001(&started, &array);
    if ((started.r_tag != UINT32_C(0)) || (started.r_payload.r_ok == NULL) ||
        !r_test_array_is_moved(&array) ||
        (r_runtime_task_await(&started.r_payload.r_ok, NULL) != R_RUNTIME_TASK_AWAIT_OK) ||
        (started.r_payload.r_ok != NULL)) {
        r_type_drop_d00000005(&started);
        r_runtime_array_destroy(&array);
        return 0;
    }
    return 1;
}

static _Bool r_test_err_first_retry(RRuntimeAllocator *allocator) {
    RRuntimeArray array = {0};
    r_d00000003 started = {0};
    int32_t result = INT32_C(-1);

    r_runtime_allocator_set_failure(allocator, UINT64_C(0));
    if (!r_test_array_create(allocator, UINT8_C(23), &array)) {
        return 0;
    }
    r_runtime_allocator_set_failure(allocator, UINT64_C(2));
    r_f00000004(&started, &array);
    if ((started.r_tag != UINT32_C(0)) || (started.r_payload.r_ok == NULL) ||
        !r_test_array_is_moved(&array) ||
        (r_runtime_task_await(&started.r_payload.r_ok, &result) != R_RUNTIME_TASK_AWAIT_OK) ||
        (started.r_payload.r_ok != NULL) || (result != INT32_C(1))) {
        r_type_drop_d00000003(&started);
        r_runtime_array_destroy(&array);
        return 0;
    }
    return 1;
}

static _Bool r_test_two_paths(RRuntimeAllocator *allocator) {
    RStdFsPath first = {0};
    RStdFsPath second = {0};
    RStdFsPathStorage *first_storage;
    RStdFsPathStorage *second_storage;
    r_d00000003 started = {0};
    int32_t result = INT32_C(-1);

    r_runtime_allocator_set_failure(allocator, UINT64_C(0));
    if (!r_test_path_create(allocator, &first) || !r_test_path_create(allocator, &second)) {
        r_std_fs_path_destroy(&first);
        r_std_fs_path_destroy(&second);
        return 0;
    }
    first_storage = first.storage;
    second_storage = second.storage;
    r_runtime_allocator_set_failure(allocator, UINT64_C(1));
    r_f00000005(&started, &first, &second);
    if ((started.r_tag != UINT32_C(1)) ||
        (started.r_payload.r_error_00000001 != R_STD_ASYNC_START_ALLOCATION_FAILED) ||
        (first.storage != first_storage) || (second.storage != second_storage)) {
        r_type_drop_d00000003(&started);
        r_std_fs_path_destroy(&first);
        r_std_fs_path_destroy(&second);
        return 0;
    }

    r_runtime_allocator_set_failure(allocator, UINT64_C(0));
    r_f00000005(&started, &first, &second);
    if ((started.r_tag != UINT32_C(0)) || (started.r_payload.r_ok == NULL) ||
        (first.storage != NULL) || (second.storage != NULL) ||
        (r_runtime_task_await(&started.r_payload.r_ok, &result) != R_RUNTIME_TASK_AWAIT_OK) ||
        (started.r_payload.r_ok != NULL) || (result != INT32_C(7))) {
        r_type_drop_d00000003(&started);
        r_std_fs_path_destroy(&first);
        r_std_fs_path_destroy(&second);
        return 0;
    }
    return 1;
}

static _Bool r_test_result_array(RRuntimeAllocator *allocator, _Bool succeed) {
    RRuntimeArray array = {0};
    r_d00000002 result = {0};
    r_d00000006 started = {0};

    r_runtime_allocator_set_failure(allocator, UINT64_C(0));
    if (!r_test_array_create(allocator, UINT8_C(41), &array)) {
        return 0;
    }
    r_f00000003(&started, &array, succeed);
    if ((started.r_tag != UINT32_C(0)) || (started.r_payload.r_ok == NULL) ||
        !r_test_array_is_moved(&array) ||
        (r_runtime_task_await(&started.r_payload.r_ok, &result) != R_RUNTIME_TASK_AWAIT_OK) ||
        (started.r_payload.r_ok != NULL)) {
        if (started.r_tag == UINT32_C(0)) {
            r_runtime_task_destroy(&started.r_payload.r_ok);
        }
        r_runtime_array_destroy(&array);
        return 0;
    }
    if (succeed) {
        if ((result.r_tag != UINT32_C(0)) || (result.r_payload.r_ok.length != 1U) ||
            (*(const uint8_t *)result.r_payload.r_ok.data != UINT8_C(41))) {
            r_type_drop_d00000002(&result);
            return 0;
        }
    } else if ((result.r_tag != UINT32_C(1)) ||
               (result.r_payload.r_error_00000001.r_m00000001 != INT32_C(9))) {
        r_type_drop_d00000002(&result);
        return 0;
    }
    r_type_drop_d00000002(&result);
    return 1;
}

static _Bool r_test_cancel_drops_array(RRuntimeAllocator *allocator) {
    RRuntimeArray array = {0};
    r_d00000006 started = {0};

    r_runtime_allocator_set_failure(allocator, UINT64_C(0));
    if (!r_test_array_create(allocator, UINT8_C(59), &array)) {
        return 0;
    }
    r_f00000003(&started, &array, 1);
    if ((started.r_tag != UINT32_C(0)) || (started.r_payload.r_ok == NULL) ||
        !r_test_array_is_moved(&array)) {
        if (started.r_tag == UINT32_C(0)) {
            r_runtime_task_destroy(&started.r_payload.r_ok);
        }
        r_runtime_array_destroy(&array);
        return 0;
    }
    r_runtime_task_destroy(&started.r_payload.r_ok);
    return started.r_payload.r_ok == NULL;
}

static _Bool r_test_runtime_stopping_preserves_array(RRuntimeAllocator *allocator) {
    RRuntimeArray array = {0};
    RRuntimeArray snapshot;
    r_d00000005 started = {0};
    const uint8_t byte = UINT8_C(73);

    r_runtime_allocator_set_failure(allocator, UINT64_C(0));
    if (!r_test_array_create(allocator, byte, &array)) {
        return 0;
    }
    snapshot = array;
    r_f00000001(&started, &array);
    if ((started.r_tag != UINT32_C(1)) ||
        (started.r_payload.r_error_00000001 != R_STD_ASYNC_START_RUNTIME_STOPPING) ||
        !r_test_array_matches(&array, &snapshot, byte)) {
        r_type_drop_d00000005(&started);
        r_runtime_array_destroy(&array);
        return 0;
    }
    r_runtime_array_destroy(&array);
    return 1;
}

int main(void) {
    RRuntimeAllocator allocator;

    if (!r_runtime_stack_initialize_current_thread()) {
        return __LINE__;
    }
    if (!r_test_resume_does_not_repeat_effect()) {
        return __LINE__;
    }
    r_runtime_allocator_initialize(&allocator);
    if (r_runtime_executor_lifecycle_start(&allocator) != R_RUNTIME_EXECUTOR_START_OK) {
        return __LINE__;
    }
    if (!r_test_allocation_failure_preserves_array(&allocator) ||
        !r_test_err_first_retry(&allocator) || !r_test_two_paths(&allocator) ||
        !r_test_result_array(&allocator, 1) || !r_test_result_array(&allocator, 0) ||
        !r_test_cancel_drops_array(&allocator)) {
        (void)r_runtime_executor_lifecycle_stop();
        return __LINE__;
    }
    if (!r_runtime_executor_lifecycle_stop()) {
        return __LINE__;
    }
    if (!r_test_runtime_stopping_preserves_array(&allocator)) {
        return __LINE__;
    }
    return 0;
}

#include "r_runtime_allocator.h"
#include "r_runtime_array.h"
#include "r_runtime_task.h"
#include "r_runtime_type.h"

#include <stddef.h>
#include <stdint.h>

static RRuntimeTaskPrepareResult r_test_resumable_start_prepare(RRuntimeTypeInfo payload_type,
                                                                RRuntimeTypeInfo result_type,
                                                                RRuntimeTaskStepFn step);

#define r_runtime_task_resumable_start_prepare r_test_resumable_start_prepare
#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_GENERATED_C
#undef main
#undef r_runtime_task_resumable_start_prepare

static RRuntimeTaskStepFn r_test_child_step;
static size_t r_test_child_step_count;

static RRuntimeTaskStepStatus
r_test_counted_child_step(RRuntimeTaskExecution *execution, void *payload, void *result) {
    r_test_child_step_count += 1U;
    return r_test_child_step(execution, payload, result);
}

static RRuntimeTaskPrepareResult r_test_resumable_start_prepare(RRuntimeTypeInfo payload_type,
                                                                RRuntimeTypeInfo result_type,
                                                                RRuntimeTaskStepFn step) {
    r_test_child_step = step;
    return r_runtime_task_resumable_start_prepare(
        payload_type, result_type, r_test_counted_child_step);
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

static _Bool r_test_allocation_failure(RRuntimeAllocator *allocator) {
    RRuntimeArray array = {0};
    size_t retained_size;

    r_runtime_allocator_set_failure(allocator, UINT64_C(0));
    if (!r_test_array_create(allocator, UINT8_C(17), &array)) {
        return 0;
    }
    r_runtime_allocator_set_failure(allocator, UINT64_C(1));
    r_test_child_step_count = 0U;
    retained_size = r_f00000004(array);
    array = (RRuntimeArray){0};
    return (retained_size == 1U) && (r_runtime_allocator_attempt_count(allocator) == UINT64_C(1)) &&
           (r_test_child_step_count == 0U);
}

static _Bool r_test_runtime_stopping(RRuntimeAllocator *allocator) {
    RRuntimeArray array = {0};
    size_t retained_size;

    r_runtime_allocator_set_failure(allocator, UINT64_C(0));
    if (!r_test_array_create(allocator, UINT8_C(29), &array)) {
        return 0;
    }
    r_runtime_allocator_set_failure(allocator, UINT64_C(0));
    r_test_child_step_count = 0U;
    retained_size = r_f00000004(array);
    array = (RRuntimeArray){0};
    return (retained_size == 1U) && (r_runtime_allocator_attempt_count(allocator) == UINT64_C(0)) &&
           (r_test_child_step_count == 0U);
}

int main(void) {
    RRuntimeAllocator allocator;

    if (!r_runtime_stack_initialize_current_thread()) {
        return __LINE__;
    }
    r_runtime_allocator_initialize(&allocator);
    if (r_runtime_executor_lifecycle_start(&allocator) != R_RUNTIME_EXECUTOR_START_OK) {
        return __LINE__;
    }
    if (!r_test_allocation_failure(&allocator)) {
        (void)r_runtime_executor_lifecycle_stop();
        return __LINE__;
    }
    if (!r_runtime_executor_lifecycle_stop()) {
        return __LINE__;
    }
    if (!r_test_runtime_stopping(&allocator)) {
        return __LINE__;
    }
    return 0;
}

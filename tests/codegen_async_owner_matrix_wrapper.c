#include "r_runtime_task.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static RRuntimeTaskExecutionAwaitStatus
r_test_await(RRuntimeTaskExecution *execution, RRuntimeTask **task, void *result);
static _Bool r_test_cancel_requested(const RRuntimeTaskExecution *execution);

#define r_runtime_task_execution_await r_test_await
#define r_runtime_task_execution_cancel_requested r_test_cancel_requested
#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_GENERATED_C
#undef main
#undef r_runtime_task_execution_cancel_requested
#undef r_runtime_task_execution_await

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            (void)fprintf(stderr, "owner matrix check failed at line %d\n", __LINE__);             \
            return __LINE__;                                                                       \
        }                                                                                          \
    } while (0)

static max_align_t r_test_execution;
static max_align_t r_test_child;
static unsigned r_test_await_count;
static _Bool r_test_cancelled;

static _Bool r_test_cancel_requested(const RRuntimeTaskExecution *execution) {
    if (execution == (const RRuntimeTaskExecution *)(const void *)&r_test_execution) {
        return 0;
    }
    return r_runtime_task_execution_cancel_requested(execution);
}

static RRuntimeTaskExecutionAwaitStatus
r_test_await(RRuntimeTaskExecution *execution, RRuntimeTask **task, void *result) {
    if (execution != (RRuntimeTaskExecution *)(void *)&r_test_execution) {
        return r_runtime_task_execution_await(execution, task, result);
    }
    if (*task != (RRuntimeTask *)(void *)&r_test_child || result == NULL) {
        return R_RUNTIME_TASK_EXECUTION_AWAIT_INVALID;
    }
    r_test_await_count += 1U;
    if (r_test_await_count == 1U) {
        return R_RUNTIME_TASK_EXECUTION_AWAIT_SUSPENDED;
    }
    *task = NULL;
    if (r_test_cancelled) {
        return R_RUNTIME_TASK_EXECUTION_AWAIT_CANCELLED;
    }
    *(int32_t *)result = INT32_C(42);
    return R_RUNTIME_TASK_EXECUTION_AWAIT_OK;
}

static int r_test_parameter_resume(_Bool cancelled) {
    r_async_frame_00000009 frame = {0};
    RRuntimeTaskExecution *execution = (RRuntimeTaskExecution *)(void *)&r_test_execution;
    int32_t result = -1;

    frame.r_state = UINT32_C(1);
    frame.r_p00000000 = (RRuntimeTask *)(void *)&r_test_child;
    frame.r_p00000000_initialized = 1;
    r_test_await_count = 0U;
    r_test_cancelled = cancelled;
    CHECK(r_async_step_00000009(execution, &frame, &result) == R_RUNTIME_TASK_STEP_SUSPENDED);
    CHECK(frame.r_p00000000_initialized && frame.r_p00000000 != NULL);
    CHECK(result == -1 && r_test_await_count == 1U);
    CHECK(r_async_step_00000009(execution, &frame, &result) ==
          (cancelled ? R_RUNTIME_TASK_STEP_CANCELLED : R_RUNTIME_TASK_STEP_COMPLETED));
    CHECK(!frame.r_p00000000_initialized && frame.r_p00000000 == NULL);
    CHECK(result == (cancelled ? -1 : 42) && r_test_await_count == 2U);
    r_async_frame_drop_00000009(&frame);
    return 0;
}

static RRuntimeTypeInfo r_test_tracked_type(void) {
    return (RRuntimeTypeInfo){sizeof(r_a00000005),
                              _Alignof(r_a00000005),
                              r_type_move_a00000005_gate,
                              r_type_drop_a00000005_gate};
}

static int r_test_item(RRuntimeAllocator *allocator, RRuntimeArc *counter, r_a00000005 *item) {
    int32_t value = 71;
    const RRuntimeTypeInfo type = {sizeof(value), _Alignof(int32_t), NULL, NULL};
    CHECK(r_runtime_arc_clone(counter, &item->r_m00000001) == R_RUNTIME_ARC_OK);
    CHECK(r_runtime_own_create(allocator, type, &value, &item->r_m00000002) == R_RUNTIME_OWN_OK);
    return 0;
}

static uint32_t r_test_drop_count(const RRuntimeArc *counter) {
    const r_a00000001 *value = r_runtime_arc_get(counter);
    return atomic_load_explicit(&value->r_m00000001, memory_order_relaxed);
}

static int r_test_list(RRuntimeAllocator *allocator, RRuntimeArc *counter, _Bool cancel) {
    RRuntimeList source = {0};
    RRuntimeList result = {0};
    r_a00000005 item = {0};
    r_d00000019 started = {0};
    uint32_t before = r_test_drop_count(counter);
    void *stored = NULL;

    r_runtime_allocator_set_failure(allocator, 0U);
    CHECK(r_test_item(allocator, counter, &item) == 0);
    CHECK(r_runtime_list_initialize(&source, allocator, r_test_tracked_type()) ==
          R_RUNTIME_LIST_OK);
    CHECK(r_runtime_list_push_back(&source, &item, &stored) == R_RUNTIME_LIST_OK);
    const RRuntimeListNode *first = source.first;
    r_runtime_allocator_set_failure(allocator, 1U);
    r_f00000007(&started, &source);
    CHECK(started.r_tag == 1U &&
          started.r_payload.r_error_00000001 == R_STD_ASYNC_START_ALLOCATION_FAILED);
    CHECK(source.first == first && source.length == 1U && r_test_drop_count(counter) == before);
    r_runtime_allocator_set_failure(allocator, 0U);
    r_f00000007(&started, &source);
    CHECK(started.r_tag == 0U && source.first == NULL && source.length == 0U);
    if (cancel) {
        r_runtime_task_destroy(&started.r_payload.r_ok);
        CHECK(r_runtime_executor_lifecycle_stop());
        CHECK(r_runtime_executor_lifecycle_start(allocator) == R_RUNTIME_EXECUTOR_START_OK);
    } else {
        CHECK(r_runtime_task_await(&started.r_payload.r_ok, &result) == R_RUNTIME_TASK_AWAIT_OK);
        CHECK(result.first == first && result.length == 1U);
        CHECK(r_test_drop_count(counter) == before);
        r_runtime_list_destroy(&result);
    }
    CHECK(r_test_drop_count(counter) == before + 1U);
    return 0;
}

static int r_test_dict(RRuntimeAllocator *allocator, RRuntimeArc *counter, _Bool cancel) {
    RRuntimeDict source = {0};
    r_a00000005 item = {0};
    r_d00000018 started = {0};
    r_d00000014 result = {0};
    const RRuntimeDictKeyInfo key_type = {
        {sizeof(int32_t), _Alignof(int32_t), NULL, NULL}, r_kh_4_0, r_ke_4_0};
    int32_t key = 11;
    _Bool replaced = 0;
    uint32_t before = r_test_drop_count(counter);

    r_runtime_allocator_set_failure(allocator, 0U);
    CHECK(r_test_item(allocator, counter, &item) == 0);
    CHECK(r_runtime_dict_initialize(&source, allocator, key_type, r_test_tracked_type(), 0U) ==
          R_RUNTIME_DICT_OK);
    CHECK(r_runtime_dict_insert(&source, &key, &item, NULL, &replaced) == R_RUNTIME_DICT_OK);
    const void *entry = r_runtime_dict_get(&source, &key);
    r_runtime_allocator_set_failure(allocator, 1U);
    r_f00000005(&started, &source);
    CHECK(started.r_tag == 1U &&
          started.r_payload.r_error_00000001 == R_STD_ASYNC_START_ALLOCATION_FAILED);
    CHECK(source.length == 1U && r_runtime_dict_get(&source, &key) == entry);
    CHECK(r_test_drop_count(counter) == before);
    r_runtime_allocator_set_failure(allocator, 0U);
    r_f00000005(&started, &source);
    CHECK(started.r_tag == 0U && source.entries == NULL && source.length == 0U);
    if (cancel) {
        r_runtime_task_destroy(&started.r_payload.r_ok);
        CHECK(r_runtime_executor_lifecycle_stop());
        CHECK(r_runtime_executor_lifecycle_start(allocator) == R_RUNTIME_EXECUTOR_START_OK);
    } else {
        CHECK(r_runtime_task_await(&started.r_payload.r_ok, &result) == R_RUNTIME_TASK_AWAIT_OK);
        CHECK(result.r_tag == 1U && r_test_drop_count(counter) == before);
        CHECK(r_runtime_dict_get(&result.r_payload.r_error_00000001.r_m00000001, &key) == entry);
        r_type_drop_d00000014(&result);
    }
    CHECK(r_test_drop_count(counter) == before + 1U);
    return 0;
}

static int r_test_thread_start(RRuntimeAllocator *allocator, RRuntimeArc *counter) {
    RRuntimeList source = {0};
    r_a00000005 item = {0};
    void *stored = NULL;
    const uint32_t before = r_test_drop_count(counter);
    const RRuntimeTypeInfo payload_type = {sizeof(r_thread_payload_00000002),
                                           _Alignof(r_thread_payload_00000002),
                                           r_thread_payload_move_00000002,
                                           r_thread_payload_drop_00000002};
    const RStdThreadCompletionTypeInfo completion_type = {
        {sizeof(int32_t), _Alignof(int32_t), NULL, NULL}, 0U, 0U, 0U};

    r_runtime_allocator_set_failure(allocator, 0U);
    CHECK(r_test_item(allocator, counter, &item) == 0);
    CHECK(r_runtime_list_initialize(&source, allocator, r_test_tracked_type()) ==
          R_RUNTIME_LIST_OK);
    CHECK(r_runtime_list_push_back(&source, &item, &stored) == R_RUNTIME_LIST_OK);
    const RRuntimeListNode *first = source.first;
    for (uint64_t failure = 1U; failure < 16U; ++failure) {
        r_thread_stage_00000002 stage = {&source};
        r_runtime_allocator_set_failure(allocator, failure);
        RStdThreadSpawnResult started = r_library_internal_thread_spawn_checked(
            allocator, payload_type, completion_type, r_thread_entry_00000002, &stage);
        if (started.is_ok) {
            int32_t count = 0;
            r_runtime_allocator_set_failure(allocator, 0U);
            CHECK(source.first == NULL && source.length == 0U);
            RStdThreadJoinResult joined = r_std_thread_join(&started.value);
            CHECK(joined.kind == R_STD_THREAD_JOIN_RETURNED);
            r_library_internal_thread_join_result_move(&joined, &count);
            r_library_internal_thread_join_result_destroy(&joined);
            CHECK(count == 1 && r_test_drop_count(counter) == before + 1U);
            return 0;
        }
        CHECK(started.error == R_STD_THREAD_ERROR_RESOURCE_EXHAUSTED);
        CHECK(r_runtime_allocator_attempt_count(allocator) == failure);
        CHECK(source.first == first && source.length == 1U);
        CHECK(r_test_drop_count(counter) == before);
    }
    r_runtime_list_destroy(&source);
    return __LINE__;
}

static int r_test_task_start(RRuntimeAllocator *allocator) {
    r_d00000016 child = {0};
    r_d00000016 parent = {0};
    int32_t result = -1;
    r_runtime_allocator_set_failure(allocator, 0U);
    r_f00000004(&child);
    CHECK(child.r_tag == 0U);
    r_task snapshot = child.r_payload.r_ok;
    r_runtime_allocator_set_failure(allocator, 1U);
    r_f00000009(&parent, &child.r_payload.r_ok);
    CHECK(parent.r_tag == 1U && child.r_payload.r_ok == snapshot);
    r_runtime_allocator_set_failure(allocator, 0U);
    r_f00000009(&parent, &child.r_payload.r_ok);
    CHECK(parent.r_tag == 0U && child.r_payload.r_ok == NULL);
    CHECK(r_runtime_task_await(&parent.r_payload.r_ok, &result) == R_RUNTIME_TASK_AWAIT_OK);
    CHECK(result == 42);
    return 0;
}

int main(int argc, char *argv[]) {
    RRuntimeAllocator allocator;
    RRuntimeArc counter = {0};
    r_a00000001 counter_value = {0};
    const RRuntimeTypeInfo counter_type = {sizeof(counter_value),
                                           _Alignof(r_a00000001),
                                           r_type_move_a00000001_gate,
                                           r_type_drop_a00000001_gate};
    CHECK(r_runtime_stack_initialize_current_thread());
    CHECK(r_test_parameter_resume(0) == 0);
    CHECK(r_test_parameter_resume(1) == 0);
    r_runtime_allocator_initialize(&allocator);
    CHECK(r_runtime_executor_lifecycle_start(&allocator) == R_RUNTIME_EXECUTOR_START_OK);
    CHECK(r_runtime_arc_create(&allocator, counter_type, &counter_value, &counter) ==
          R_RUNTIME_ARC_OK);
    CHECK(r_test_list(&allocator, &counter, 0) == 0);
    CHECK(r_test_list(&allocator, &counter, 1) == 0);
    CHECK(r_test_dict(&allocator, &counter, 0) == 0);
    CHECK(r_test_dict(&allocator, &counter, 1) == 0);
    CHECK(r_test_task_start(&allocator) == 0);
    CHECK(r_test_thread_start(&allocator, &counter) == 0);
    CHECK(r_test_drop_count(&counter) == 5U && r_runtime_arc_strong_count(&counter) == 1U);
    r_runtime_arc_release(&counter);
    CHECK(r_runtime_executor_lifecycle_stop());
    return r_generated_main(argc, argv);
}

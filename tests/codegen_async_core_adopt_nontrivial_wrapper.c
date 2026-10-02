#include "r_runtime_0_1.h"
#include "r_runtime_allocator.h"
#include "r_runtime_own.h"
#include "r_runtime_task.h"

#include <stddef.h>
#include <stdint.h>

#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_GENERATED_C
#undef main

static int32_t r_test_drop_values[2];
static size_t r_test_drop_count;

static void r_test_tracked_i32_drop(void *value_pointer) {
    const int32_t *value = value_pointer;

    if (r_test_drop_count < (sizeof(r_test_drop_values) / sizeof(r_test_drop_values[0]))) {
        r_test_drop_values[r_test_drop_count] = *value;
        ++r_test_drop_count;
    }
}

static RRuntimeTypeInfo r_test_tracked_i32_type(void) {
    const RRuntimeTypeInfo type = {
        sizeof(int32_t),
        _Alignof(int32_t),
        NULL,
        r_test_tracked_i32_drop,
    };

    return type;
}

static _Bool
r_test_allocate_owned_i32(RRuntimeAllocator *allocator, int32_t value, RRuntimeOwn *owner) {
    void *allocation = NULL;

    if (r_runtime_allocator_allocate(allocator, sizeof(int32_t), _Alignof(int32_t), &allocation) !=
        R_RUNTIME_ALLOCATION_OK) {
        return 0;
    }
    *(int32_t *)allocation = value;
    if (r_runtime_own_adopt(r_test_tracked_i32_type(), allocation, owner) != R_RUNTIME_OWN_OK) {
        r_runtime_allocator_deallocate(allocation, _Alignof(int32_t));
        return 0;
    }
    return 1;
}

static _Bool r_test_allocate_box(RRuntimeAllocator *allocator, r_a00000001 **box) {
    void *allocation = NULL;
    r_a00000001 *value;

    *box = NULL;
    if (r_runtime_allocator_allocate(
            allocator, sizeof(r_a00000001), _Alignof(r_a00000001), &allocation) !=
        R_RUNTIME_ALLOCATION_OK) {
        return 0;
    }
    value = allocation;
    *value = (r_a00000001){0};
    if (!r_test_allocate_owned_i32(allocator, INT32_C(31), &value->r_m00000001)) {
        r_runtime_allocator_deallocate(allocation, _Alignof(r_a00000001));
        return 0;
    }
    if (!r_test_allocate_owned_i32(allocator, INT32_C(47), &value->r_m00000002)) {
        r_runtime_own_release(&value->r_m00000001);
        r_runtime_allocator_deallocate(allocation, _Alignof(r_a00000001));
        return 0;
    }
    *box = value;
    return 1;
}

int main(void) {
    RRuntimeAllocator allocator;
    RRuntimeOwn owner = {0};
    r_async_frame_00000001 frame = {0};
    r_a00000001 *box = NULL;
    r_d00000005 started = {0};
    /* The implicit main error set (R-FUNC-0008) makes the task result a carrier. */
    r_d00000004 result = {0};
    char program_name[] = "r-adopt-async-test";
    char *arguments[] = {program_name, NULL};
    const RRuntimeStartResult runtime_start = r_runtime_hosted_start(1, arguments);

    if (!runtime_start.started) {
        return __LINE__;
    }
    r_f00000001(&started);
    if ((started.r_tag != UINT32_C(0)) || (started.r_payload.r_ok == NULL) ||
        (r_runtime_task_await(&started.r_payload.r_ok, &result) != R_RUNTIME_TASK_AWAIT_OK) ||
        (result.r_tag != UINT32_C(0)) || (result.r_payload.r_ok != INT32_C(0))) {
        return __LINE__;
    }

    r_runtime_allocator_initialize(&allocator);
    if (!r_test_allocate_box(&allocator, &box) || (r_runtime_own_adopt(
                                                       (RRuntimeTypeInfo){
                                                           sizeof(r_a00000001),
                                                           _Alignof(r_a00000001),
                                                           r_type_move_a00000001,
                                                           r_type_drop_a00000001,
                                                       },
                                                       box,
                                                       &owner) != R_RUNTIME_OWN_OK)) {
        return __LINE__;
    }
    r_async_frame_initialize_00000001(&frame, NULL);
    frame.r_l00000002 = owner;
    frame.r_l00000002_initialized = 1;
    owner = (RRuntimeOwn){0};
    r_async_frame_drop_00000001(&frame);
    if ((frame.r_l00000002_initialized != 0) || (r_test_drop_count != 2U) ||
        (r_test_drop_values[0] != INT32_C(47)) || (r_test_drop_values[1] != INT32_C(31))) {
        return __LINE__;
    }
    return r_runtime_hosted_finish(INT32_C(0));
}

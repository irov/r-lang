#include "r_runtime_allocator.h"
#include "r_runtime_own.h"

#include <stddef.h>
#include <stdint.h>

#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_GENERATED_C
#undef main

static int32_t r_test_drop_values[8];
static size_t r_test_drop_count;
static _Bool r_test_drop_overflow;

static void r_test_tracked_i32_drop(void *value_pointer) {
    const int32_t *value = value_pointer;

    if (r_test_drop_count < (sizeof(r_test_drop_values) / sizeof(r_test_drop_values[0]))) {
        r_test_drop_values[r_test_drop_count] = *value;
        ++r_test_drop_count;
    } else {
        r_test_drop_overflow = 1;
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
    if (!r_test_allocate_owned_i32(allocator, INT32_C(11), &value->r_m00000001)) {
        r_runtime_allocator_deallocate(allocation, _Alignof(r_a00000001));
        return 0;
    }
    if (!r_test_allocate_owned_i32(allocator, INT32_C(22), &value->r_m00000002)) {
        r_runtime_own_release(&value->r_m00000001);
        r_runtime_allocator_deallocate(allocation, _Alignof(r_a00000001));
        return 0;
    }
    *box = value;
    return 1;
}

static void r_test_reset_drops(void) {
    r_test_drop_count = 0U;
    r_test_drop_overflow = 0;
}

static _Bool r_test_dropped_box_once(void) {
    return !r_test_drop_overflow && (r_test_drop_count == 2U) &&
           (r_test_drop_values[0] == INT32_C(22)) && (r_test_drop_values[1] == INT32_C(11));
}

int main(void) {
    RRuntimeAllocator allocator;
    r_a00000001 *box = NULL;
    r_a00000001 *returned;

    if (!r_runtime_stack_initialize_current_thread()) {
        return __LINE__;
    }
    r_runtime_allocator_initialize(&allocator);

    r_test_reset_drops();
    if (!r_test_allocate_box(&allocator, &box)) {
        return __LINE__;
    }
    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    r_f00000001(box, 0);
    if ((r_runtime_allocator_attempt_count(&allocator) != UINT64_C(0)) ||
        !r_test_dropped_box_once()) {
        return __LINE__;
    }

    r_runtime_allocator_set_failure(&allocator, UINT64_C(0));
    r_test_reset_drops();
    if (!r_test_allocate_box(&allocator, &box)) {
        return __LINE__;
    }
    r_f00000001(box, 1);
    if (!r_test_dropped_box_once()) {
        return __LINE__;
    }

    r_test_reset_drops();
    if (!r_test_allocate_box(&allocator, &box)) {
        return __LINE__;
    }
    returned = r_f00000003(box);
    if ((returned != box) || (r_test_drop_count != 0U)) {
        return __LINE__;
    }
    r_type_drop_a00000001(returned);
    r_runtime_allocator_deallocate(returned, _Alignof(r_a00000001));
    if (!r_test_dropped_box_once()) {
        return __LINE__;
    }
    return 0;
}

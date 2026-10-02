#include "r_runtime_allocator.h"
#include "r_runtime_own.h"

#include <stddef.h>
#include <stdint.h>

#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_GENERATED_C
#undef main

static uint64_t r_test_drop_values[8];
static size_t r_test_drop_count;
static _Bool r_test_drop_overflow;

static void r_test_record_drop(uint64_t value) {
    if (r_test_drop_count < (sizeof(r_test_drop_values) / sizeof(r_test_drop_values[0]))) {
        r_test_drop_values[r_test_drop_count] = value;
        ++r_test_drop_count;
    } else {
        r_test_drop_overflow = 1;
    }
}

static void r_test_i32_drop(void *value_pointer) {
    const int32_t *value = value_pointer;

    r_test_record_drop((uint64_t)*value);
}

static void r_test_u32_drop(void *value_pointer) {
    const uint32_t *value = value_pointer;

    r_test_record_drop((uint64_t)*value);
}

static RRuntimeTypeInfo r_test_i32_type(void) {
    const RRuntimeTypeInfo type = {
        sizeof(int32_t),
        _Alignof(int32_t),
        NULL,
        r_test_i32_drop,
    };

    return type;
}

static RRuntimeTypeInfo r_test_u32_type(void) {
    const RRuntimeTypeInfo type = {
        sizeof(uint32_t),
        _Alignof(uint32_t),
        NULL,
        r_test_u32_drop,
    };

    return type;
}

static _Bool r_test_allocate_owner(RRuntimeAllocator *allocator,
                                   RRuntimeTypeInfo type,
                                   uint64_t value,
                                   _Bool is_unsigned,
                                   RRuntimeOwn *owner) {
    void *allocation = NULL;

    if (r_runtime_allocator_allocate(allocator, type.size, type.alignment, &allocation) !=
        R_RUNTIME_ALLOCATION_OK) {
        return 0;
    }
    if (is_unsigned) {
        *(uint32_t *)allocation = (uint32_t)value;
    } else {
        *(int32_t *)allocation = (int32_t)value;
    }
    if (r_runtime_own_adopt(type, allocation, owner) != R_RUNTIME_OWN_OK) {
        r_runtime_allocator_deallocate(allocation, type.alignment);
        return 0;
    }
    return 1;
}

static _Bool r_test_allocate_box(RRuntimeAllocator *allocator,
                                 uint64_t base,
                                 uint32_t selected_option,
                                 r_a00000001 **box) {
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
    value->r_m00000002.r_tag = UINT32_C(1);
    value->r_m00000003.r_tag = selected_option == UINT32_C(0) ? UINT32_C(1) : UINT32_C(0);
    value->r_m00000004.r_tag = selected_option == UINT32_C(0) ? UINT32_C(0) : UINT32_C(1);
    if (!r_test_allocate_owner(allocator,
                               r_test_i32_type(),
                               base + UINT64_C(10),
                               0,
                               &value->r_m00000001.r_data[0].r_m00000001) ||
        !r_test_allocate_owner(allocator,
                               r_test_i32_type(),
                               base + UINT64_C(20),
                               0,
                               &value->r_m00000001.r_data[1].r_m00000001) ||
        !r_test_allocate_owner(allocator,
                               r_test_i32_type(),
                               base + UINT64_C(30),
                               0,
                               &value->r_m00000002.r_payload.r_some) ||
        (selected_option == UINT32_C(0)
             ? !r_test_allocate_owner(allocator,
                                      r_test_i32_type(),
                                      base + UINT64_C(40),
                                      0,
                                      &value->r_m00000003.r_payload.r_some)
             : !r_test_allocate_owner(allocator,
                                      r_test_u32_type(),
                                      base + UINT64_C(40),
                                      1,
                                      &value->r_m00000004.r_payload.r_some))) {
        r_type_drop_a00000001(value);
        r_runtime_allocator_deallocate(value, _Alignof(r_a00000001));
        return 0;
    }
    *box = value;
    return 1;
}

static void r_test_reset_drops(void) {
    r_test_drop_count = 0U;
    r_test_drop_overflow = 0;
}

static _Bool r_test_expected_drops(uint64_t base) {
    return !r_test_drop_overflow && (r_test_drop_count == 4U) &&
           (r_test_drop_values[0] == base + UINT64_C(40)) &&
           (r_test_drop_values[1] == base + UINT64_C(30)) &&
           (r_test_drop_values[2] == base + UINT64_C(20)) &&
           (r_test_drop_values[3] == base + UINT64_C(10));
}

int main(void) {
    RRuntimeAllocator allocator;
    r_a00000001 *box = NULL;
    r_a00000001 moved = {0};

    if (!r_runtime_stack_initialize_current_thread()) {
        return __LINE__;
    }
    r_runtime_allocator_initialize(&allocator);
    r_test_reset_drops();
    if (!r_test_allocate_box(&allocator, UINT64_C(0), UINT32_C(0), &box)) {
        return __LINE__;
    }
    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    r_f00000001(box);
    if ((r_runtime_allocator_attempt_count(&allocator) != UINT64_C(0)) ||
        !r_test_expected_drops(UINT64_C(0))) {
        return __LINE__;
    }

    r_runtime_allocator_set_failure(&allocator, UINT64_C(0));
    r_test_reset_drops();
    if (!r_test_allocate_box(&allocator, UINT64_C(100), UINT32_C(1), &box)) {
        return __LINE__;
    }
    r_f00000001(box);
    if (!r_test_expected_drops(UINT64_C(100))) {
        return __LINE__;
    }

    r_test_reset_drops();
    if (!r_test_allocate_box(&allocator, UINT64_C(200), UINT32_C(0), &box)) {
        return __LINE__;
    }
    r_type_move_a00000001(&moved, box);
    r_type_drop_a00000001(box);
    if (r_test_drop_count != 0U) {
        return __LINE__;
    }
    r_runtime_allocator_deallocate(box, _Alignof(r_a00000001));
    r_type_drop_a00000001(&moved);
    if (!r_test_expected_drops(UINT64_C(200))) {
        return __LINE__;
    }
    return 0;
}

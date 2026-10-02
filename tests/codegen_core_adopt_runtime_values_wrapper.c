#include "r_runtime_allocator.h"
#include "r_runtime_arc.h"
#include "r_runtime_array.h"
#include "r_runtime_dict.h"
#include "r_runtime_list.h"
#include "r_runtime_rc.h"
#include "r_runtime_type.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_GENERATED_C
#undef main

static int32_t r_test_drop_values[16];
static size_t r_test_drop_count;

static void r_test_record_drop(void *value_pointer) {
    const int32_t *value = value_pointer;

    if (r_test_drop_count < (sizeof(r_test_drop_values) / sizeof(r_test_drop_values[0]))) {
        r_test_drop_values[r_test_drop_count] = *value;
    }
    ++r_test_drop_count;
}

static RRuntimeTypeInfo r_test_i32_type(void) {
    return (RRuntimeTypeInfo){
        sizeof(int32_t),
        _Alignof(int32_t),
        NULL,
        r_test_record_drop,
    };
}

static uint64_t r_test_hash_i32(const void *value_pointer) {
    const int32_t value = *(const int32_t *)value_pointer;

    return (uint64_t)(uint32_t)value;
}

static _Bool r_test_equal_i32(const void *left_pointer, const void *right_pointer) {
    return *(const int32_t *)left_pointer == *(const int32_t *)right_pointer;
}

static _Bool r_test_initialize_runtime_values(RRuntimeAllocator *allocator, r_a00000001 *value) {
    const RRuntimeTypeInfo type = r_test_i32_type();
    const RRuntimeDictKeyInfo key_info = {type, r_test_hash_i32, r_test_equal_i32};
    int32_t element;
    int32_t key;
    int32_t mapped;
    void *stored_value = NULL;
    _Bool did_replace = 0;

    *value = (r_a00000001){0};
    r_runtime_array_initialize(&value->r_m00000001, allocator, type);
    if (r_runtime_list_initialize(&value->r_m00000002, allocator, type) != R_RUNTIME_LIST_OK ||
        r_runtime_dict_initialize(
            &value->r_m00000003, allocator, key_info, type, UINT64_C(0x12345678)) !=
            R_RUNTIME_DICT_OK) {
        return 0;
    }
    atomic_init(&value->r_m00000008, UINT32_C(60));

    element = INT32_C(10);
    if (r_runtime_array_push(&value->r_m00000001, &element) != R_RUNTIME_ARRAY_OK) {
        return 0;
    }
    element = INT32_C(20);
    if (r_runtime_list_push_back(&value->r_m00000002, &element, &stored_value) !=
        R_RUNTIME_LIST_OK) {
        return 0;
    }
    key = INT32_C(30);
    mapped = INT32_C(31);
    if (r_runtime_dict_insert(&value->r_m00000003, &key, &mapped, NULL, &did_replace) !=
            R_RUNTIME_DICT_OK ||
        did_replace) {
        return 0;
    }
    element = INT32_C(40);
    if (r_runtime_arc_create(allocator, type, &element, &value->r_m00000004) != R_RUNTIME_ARC_OK ||
        r_runtime_arc_downgrade(&value->r_m00000004, &value->r_m00000006) != R_RUNTIME_ARC_OK) {
        return 0;
    }
    element = INT32_C(50);
    if (r_runtime_rc_create(allocator, type, &element, &value->r_m00000005) != R_RUNTIME_RC_OK ||
        r_runtime_rc_downgrade(&value->r_m00000005, &value->r_m00000007) != R_RUNTIME_RC_OK) {
        return 0;
    }
    return 1;
}

static r_a00000001 *r_test_allocate_runtime_values(RRuntimeAllocator *allocator) {
    void *allocation = NULL;
    r_a00000001 *value;

    if (r_runtime_allocator_allocate(
            allocator, sizeof(r_a00000001), _Alignof(r_a00000001), &allocation) !=
        R_RUNTIME_ALLOCATION_OK) {
        return NULL;
    }
    value = allocation;
    if (!r_test_initialize_runtime_values(allocator, value)) {
        r_type_drop_a00000001(value);
        r_runtime_allocator_deallocate(value, _Alignof(r_a00000001));
        return NULL;
    }
    return value;
}

static _Bool r_test_expected_drops(void) {
    static const int32_t expected[] = {
        INT32_C(50),
        INT32_C(40),
        INT32_C(31),
        INT32_C(30),
        INT32_C(20),
        INT32_C(10),
    };
    size_t index;

    if (r_test_drop_count != (sizeof(expected) / sizeof(expected[0]))) {
        return 0;
    }
    for (index = 0U; index < (sizeof(expected) / sizeof(expected[0])); ++index) {
        if (r_test_drop_values[index] != expected[index]) {
            return 0;
        }
    }
    return 1;
}

int main(void) {
    RRuntimeAllocator allocator;
    r_a00000001 *value;
    r_a00000001 *released;
    r_a00000001 moved = {0};

    if (!r_runtime_stack_initialize_current_thread()) {
        return __LINE__;
    }
    r_runtime_allocator_initialize(&allocator);
    value = r_test_allocate_runtime_values(&allocator);
    if (value == NULL) {
        return __LINE__;
    }
    r_test_drop_count = 0U;
    r_f00000001(value);
    if (!r_test_expected_drops()) {
        return __LINE__;
    }

    value = r_test_allocate_runtime_values(&allocator);
    if (value == NULL) {
        return __LINE__;
    }
    r_test_drop_count = 0U;
    released = r_f00000003(value);
    if ((released != value) || (r_test_drop_count != 0U)) {
        return __LINE__;
    }
    r_type_drop_a00000001(released);
    r_runtime_allocator_deallocate(released, _Alignof(r_a00000001));
    if (!r_test_expected_drops()) {
        return __LINE__;
    }

    value = r_test_allocate_runtime_values(&allocator);
    if (value == NULL) {
        return __LINE__;
    }
    r_test_drop_count = 0U;
    r_type_move_a00000001(&moved, value);
    if (atomic_load_explicit(&moved.r_m00000008, memory_order_relaxed) != UINT32_C(60)) {
        return __LINE__;
    }
    r_type_drop_a00000001(value);
    r_runtime_allocator_deallocate(value, _Alignof(r_a00000001));
    if (r_test_drop_count != 0U) {
        return __LINE__;
    }
    r_type_drop_a00000001(&moved);
    if (!r_test_expected_drops()) {
        return __LINE__;
    }
    return 0;
}

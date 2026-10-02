#include "r_runtime_0_1.h"
#include "r_runtime_allocator.h"
#include "r_runtime_array.h"

#include <stdint.h>

#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_GENERATED_C
#undef main

static _Bool r_test_pair_equal(r_a00000001 left, r_a00000001 right) {
    return (left.r_m00000001 == right.r_m00000001) && (left.r_m00000002 == right.r_m00000002);
}

static _Bool r_test_array_descriptor_equal(const RRuntimeArray *left, const RRuntimeArray *right) {
    return (left->allocator == right->allocator) && (left->element.size == right->element.size) &&
           (left->element.alignment == right->element.alignment) &&
           (left->element.move_initialize == right->element.move_initialize) &&
           (left->element.drop == right->element.drop) && (left->data == right->data) &&
           (left->length == right->length) && (left->capacity == right->capacity);
}

int main(int argc, char *argv[]) {
    const RRuntimeStartResult runtime_start = r_runtime_hosted_start(argc, argv);
    RRuntimeAllocator *allocator;
    RRuntimeArray array = {0};
    RRuntimeArray snapshot;
    r_d00000005 created = {0};
    r_d00000005 failed_create = {0};
    r_d00000004 pushed = {0};
    r_d00000004 failed_push = {0};
    const r_a00000001 first = {UINT32_C(11), UINT16_C(7)};
    const r_a00000001 second = {UINT32_C(29), UINT16_C(13)};
    int32_t status = INT32_C(0);

    if (!runtime_start.started) {
        return runtime_start.process_status;
    }
    allocator = r_runtime_hosted_allocator();
    if (allocator == NULL) {
        status = (int32_t)__LINE__;
        goto cleanup;
    }
    r_runtime_allocator_set_failure(allocator, UINT64_C(1));
    r_f00000002(&failed_create, 1U);
    if (failed_create.r_tag == UINT32_C(0)) {
        r_type_move_array(&array, &failed_create.r_payload.r_ok);
    }
    if ((r_runtime_allocator_attempt_count(allocator) != UINT64_C(1)) ||
        (failed_create.r_tag != UINT32_C(1)) ||
        (failed_create.r_payload.r_error_00000001 != R_STD_ALLOC_ERROR_OUT_OF_MEMORY)) {
        status = (int32_t)__LINE__;
        goto cleanup;
    }

    r_runtime_allocator_set_failure(allocator, UINT64_C(0));
    r_f00000002(&created, 1U);
    if (created.r_tag != UINT32_C(0)) {
        status = (int32_t)__LINE__;
        goto cleanup;
    }
    r_type_move_array(&array, &created.r_payload.r_ok);
    if ((array.data == NULL) || (array.length != 0U) || (array.capacity != 1U)) {
        status = (int32_t)__LINE__;
        goto cleanup;
    }

    r_f00000003(&pushed, &array, first);
    if ((pushed.r_tag != UINT32_C(0)) || (array.length != 1U) ||
        !r_test_pair_equal(((const r_a00000001 *)array.data)[0], first)) {
        status = (int32_t)__LINE__;
        goto cleanup;
    }
    snapshot = array;

    r_runtime_allocator_set_failure(allocator, UINT64_C(1));
    r_f00000003(&failed_push, &array, second);
    if ((r_runtime_allocator_attempt_count(allocator) != UINT64_C(1)) ||
        (failed_push.r_tag != UINT32_C(1)) ||
        (failed_push.r_payload.r_error_00000001.r_tag != UINT32_C(0)) ||
        (failed_push.r_payload.r_error_00000001.r_payload.r_allocation_failed.r_reason !=
         R_STD_ALLOC_ERROR_OUT_OF_MEMORY) ||
        !r_test_pair_equal(
            failed_push.r_payload.r_error_00000001.r_payload.r_allocation_failed.r_value, second) ||
        !r_test_array_descriptor_equal(&array, &snapshot) ||
        !r_test_pair_equal(((const r_a00000001 *)array.data)[0], first)) {
        status = (int32_t)__LINE__;
        goto cleanup;
    }

cleanup:
    r_runtime_array_destroy(&array);
    return r_runtime_hosted_finish(status);
}

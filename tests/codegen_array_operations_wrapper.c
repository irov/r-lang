#include "r_runtime_allocator.h"
#include "r_runtime_array.h"
#include "r_std_alloc.h"
#include "r_std_array.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

RStdArrayAllocValueResult
r_test_array_with_capacity(RRuntimeAllocator *allocator, RRuntimeTypeInfo element, size_t capacity);
RStdArrayPushResult r_test_array_push(RStdArray *target, void *staged_value);

#define r_std_array_push r_test_array_push
#define r_std_array_with_capacity r_test_array_with_capacity
#define main r_generated_main
int main(int argc, char *argv[]);
#include R_TEST_PROGRAM_PRELUDE
#undef main
#undef r_std_array_with_capacity
#undef r_std_array_push

/* The fixture calls with_capacity twice (make_pairs, then main) and push twice (main into spare
 * capacity, then push_pair, which needs growth); each hook checks what the library side of one
 * call must leave behind, and the program checks what R observes (Library R-LIB-0019: a failed
 * push hands its value back). A push into spare capacity may complete without the library call,
 * so the wrapper requires the growing push only. */
static size_t create_calls;
static size_t fitting_pushes;
static size_t growing_pushes;
static bool valid = true;

static bool r_test_same_array(const RStdArray *left, const RStdArray *right) {
    return (left->allocator == right->allocator) && (left->element.size == right->element.size) &&
           (left->element.alignment == right->element.alignment) &&
           (left->element.move_initialize == right->element.move_initialize) &&
           (left->element.drop == right->element.drop) && (left->data == right->data) &&
           (left->length == right->length) && (left->capacity == right->capacity);
}

/* The first call fails its only allocation and reports OUT_OF_MEMORY without a partial value; the
 * second creates an empty array with room for exactly the requested element. */
RStdArrayAllocValueResult r_test_array_with_capacity(RRuntimeAllocator *allocator,
                                                     RRuntimeTypeInfo element,
                                                     size_t capacity) {
    RStdArrayAllocValueResult result;

    ++create_calls;
    if (create_calls == 1U) {
        r_runtime_allocator_set_failure(allocator, UINT64_C(1));
        result = r_std_array_with_capacity(allocator, element, capacity);
        if ((r_runtime_allocator_attempt_count(allocator) != UINT64_C(1)) ||
            (result.status != R_STD_ARRAY_CALL_ERROR) ||
            (result.error != R_STD_ALLOC_ERROR_OUT_OF_MEMORY)) {
            valid = false;
        }
        r_runtime_allocator_set_failure(allocator, UINT64_C(0));
        return result;
    }
    result = r_std_array_with_capacity(allocator, element, capacity);
    if ((create_calls != 2U) || (capacity != 1U) || (result.status != R_STD_ARRAY_CALL_SUCCESS) ||
        (result.value.data == NULL) || (result.value.length != 0U) ||
        (result.value.capacity != 1U)) {
        valid = false;
    }
    return result;
}

/* A push into spare capacity succeeds in place. The push that needs growth fails its allocation:
 * it reports OUT_OF_MEMORY after exactly one attempt, leaves the descriptor untouched and keeps
 * the staged value intact for the error payload, whose fields the program checks. */
RStdArrayPushResult r_test_array_push(RStdArray *target, void *staged_value) {
    const RStdArray snapshot = *target;
    unsigned char staged[16];
    RStdArrayPushResult result;

    if (target->length < target->capacity) {
        ++fitting_pushes;
        result = r_std_array_push(target, staged_value);
        if ((result.status != R_STD_ARRAY_CALL_SUCCESS) ||
            (target->length != snapshot.length + 1U) || (target->data != snapshot.data) ||
            (target->capacity != snapshot.capacity)) {
            valid = false;
        }
        return result;
    }
    ++growing_pushes;
    if ((growing_pushes != 1U) || (target->element.size > sizeof(staged))) {
        valid = false;
        return r_std_array_push(target, staged_value);
    }
    (void)memcpy(staged, staged_value, target->element.size);
    r_runtime_allocator_set_failure(target->allocator, UINT64_C(1));
    result = r_std_array_push(target, staged_value);
    if ((r_runtime_allocator_attempt_count(target->allocator) != UINT64_C(1)) ||
        (result.status != R_STD_ARRAY_CALL_ERROR) ||
        (result.reason != R_STD_ALLOC_ERROR_OUT_OF_MEMORY) ||
        !r_test_same_array(target, &snapshot) ||
        (memcmp(staged, staged_value, snapshot.element.size) != 0)) {
        valid = false;
    }
    r_runtime_allocator_set_failure(target->allocator, UINT64_C(0));
    return result;
}

int main(int argc, char *argv[]) {
    const int status = r_generated_main(argc, argv);

    if (status != 0) {
        return status;
    }
    return valid && (create_calls == 2U) && (fitting_pushes <= 1U) && (growing_pushes == 1U) ? 0
                                                                                             : 100;
}

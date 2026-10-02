#include "r_std_alloc.h"

#include <stdint.h>
#include <stdio.h>

#define R_TEST_CHECK(expression)                                                                   \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            (void)fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, #expression); \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

typedef struct RTestOwnedValue {
    int32_t value;
    unsigned int *move_count;
    unsigned int *drop_count;
    _Bool moved;
} RTestOwnedValue;

static void r_test_value_move(void *destination, void *source) {
    RTestOwnedValue *destination_value = destination;
    RTestOwnedValue *source_value = source;

    *destination_value = *source_value;
    *source_value->move_count += 1U;
    source_value->moved = 1;
}

static void r_test_value_drop(void *value) {
    RTestOwnedValue *owned_value = value;

    *owned_value->drop_count += 1U;
}

static RRuntimeTypeInfo r_test_value_type(void) {
    RRuntimeTypeInfo type = {
        sizeof(RTestOwnedValue),
        _Alignof(RTestOwnedValue),
        r_test_value_move,
        r_test_value_drop,
    };

    return type;
}

static int r_test_try_new_success_and_release(void) {
    RRuntimeAllocator allocator;
    unsigned int move_count = 0U;
    unsigned int drop_count = 0U;
    RTestOwnedValue staged = {42, &move_count, &drop_count, 0};
    RStdAllocTryNewResult result;
    const RTestOwnedValue *stored;

    r_runtime_allocator_initialize(&allocator);
    result = r_std_alloc_try_new(&allocator, r_test_value_type(), &staged);
    R_TEST_CHECK(result.status == R_STD_ALLOC_CALL_SUCCESS);
    R_TEST_CHECK(result.object.allocation != NULL);
    R_TEST_CHECK(staged.moved);
    R_TEST_CHECK(move_count == 1U);
    R_TEST_CHECK(drop_count == 0U);
    stored = r_runtime_own_get(&result.object);
    R_TEST_CHECK(stored != NULL && stored->value == 42 && !stored->moved);

    r_runtime_own_release(&result.object);
    r_runtime_own_release(&result.object);
    R_TEST_CHECK(result.object.allocation == NULL);
    R_TEST_CHECK(drop_count == 1U);
    return 0;
}

static int r_test_try_new_failure_preserves_staged_value(void) {
    RRuntimeAllocator allocator;
    unsigned int move_count = 0U;
    unsigned int drop_count = 0U;
    RTestOwnedValue staged = {73, &move_count, &drop_count, 0};
    RStdAllocTryNewResult result;

    r_runtime_allocator_initialize(&allocator);
    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    result = r_std_alloc_try_new(&allocator, r_test_value_type(), &staged);
    R_TEST_CHECK(result.status == R_STD_ALLOC_CALL_ALLOCATION_ERROR);
    R_TEST_CHECK(result.error == R_STD_ALLOC_ERROR_OUT_OF_MEMORY);
    R_TEST_CHECK(result.object.allocation == NULL);
    R_TEST_CHECK(staged.value == 73);
    R_TEST_CHECK(staged.move_count == &move_count);
    R_TEST_CHECK(staged.drop_count == &drop_count);
    R_TEST_CHECK(!staged.moved);
    R_TEST_CHECK(move_count == 0U && drop_count == 0U);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(1));
    return 0;
}

static int r_test_try_new_size_and_alignment_outcomes(void) {
    RRuntimeAllocator allocator;
    unsigned int move_count = 0U;
    unsigned int drop_count = 0U;
    RTestOwnedValue staged = {11, &move_count, &drop_count, 0};
    RRuntimeTypeInfo oversized = {SIZE_MAX, 1U, r_test_value_move, r_test_value_drop};
    RRuntimeTypeInfo unsupported_alignment = {
        sizeof(staged),
        R_RUNTIME_ALLOCATOR_MAX_ALIGNMENT * 2U,
        r_test_value_move,
        r_test_value_drop,
    };
    RRuntimeTypeInfo oversized_with_unsupported_alignment = {
        R_RUNTIME_ALLOCATOR_MAX_OBJECT_SIZE + 1U,
        R_RUNTIME_ALLOCATOR_MAX_ALIGNMENT * 2U,
        r_test_value_move,
        r_test_value_drop,
    };
    RStdAllocTryNewResult result;

    r_runtime_allocator_initialize(&allocator);
    result = r_std_alloc_try_new(&allocator, oversized, &staged);
    R_TEST_CHECK(result.status == R_STD_ALLOC_CALL_ALLOCATION_ERROR);
    R_TEST_CHECK(result.error == R_STD_ALLOC_ERROR_SIZE_OVERFLOW);
    R_TEST_CHECK(!staged.moved && move_count == 0U && drop_count == 0U);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(0));

    result = r_std_alloc_try_new(&allocator, unsupported_alignment, &staged);
    R_TEST_CHECK(result.status == R_STD_ALLOC_CALL_ALLOCATION_ERROR);
    R_TEST_CHECK(result.error == R_STD_ALLOC_ERROR_UNSUPPORTED_ALIGNMENT);
    R_TEST_CHECK(!staged.moved && move_count == 0U && drop_count == 0U);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(0));

    result = r_std_alloc_try_new(&allocator, oversized_with_unsupported_alignment, &staged);
    R_TEST_CHECK(result.status == R_STD_ALLOC_CALL_ALLOCATION_ERROR);
    R_TEST_CHECK(result.error == R_STD_ALLOC_ERROR_SIZE_OVERFLOW);
    R_TEST_CHECK(!staged.moved && move_count == 0U && drop_count == 0U);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(0));

    return 0;
}

static int r_test_into_value(void) {
    RRuntimeAllocator allocator;
    unsigned int move_count = 0U;
    unsigned int drop_count = 0U;
    RTestOwnedValue staged = {91, &move_count, &drop_count, 0};
    RTestOwnedValue extracted = {0};
    RStdAllocTryNewResult allocated;
    RStdAllocCallStatus status;

    r_runtime_allocator_initialize(&allocator);
    allocated = r_std_alloc_try_new(&allocator, r_test_value_type(), &staged);
    R_TEST_CHECK(allocated.status == R_STD_ALLOC_CALL_SUCCESS);
    status = r_std_alloc_into_value(&allocated.object, &extracted);
    R_TEST_CHECK(status == R_STD_ALLOC_CALL_SUCCESS);
    R_TEST_CHECK(allocated.object.allocation == NULL);
    R_TEST_CHECK(extracted.value == 91 && !extracted.moved);
    R_TEST_CHECK(move_count == 2U);
    R_TEST_CHECK(drop_count == 0U);
    r_test_value_drop(&extracted);
    R_TEST_CHECK(drop_count == 1U);
    return 0;
}

static int r_test_bytes(void) {
    RRuntimeAllocator allocator;
    RStdAllocBytesResult result;
    const uint8_t *bytes;
    size_t index;

    r_runtime_allocator_initialize(&allocator);
    result = r_std_alloc_bytes(&allocator, 8U, UINT8_C(0xA5));
    R_TEST_CHECK(result.status == R_STD_ALLOC_CALL_SUCCESS);
    R_TEST_CHECK(result.value.length == 8U);
    R_TEST_CHECK(result.value.capacity >= result.value.length);
    bytes = result.value.data;
    R_TEST_CHECK(bytes != NULL);
    for (index = 0U; index < result.value.length; ++index) {
        R_TEST_CHECK(bytes[index] == UINT8_C(0xA5));
    }
    r_runtime_array_destroy(&result.value);

    r_runtime_allocator_initialize(&allocator);
    result = r_std_alloc_bytes(&allocator, 0U, UINT8_C(0xFF));
    R_TEST_CHECK(result.status == R_STD_ALLOC_CALL_SUCCESS);
    R_TEST_CHECK(result.value.data == NULL);
    R_TEST_CHECK(result.value.length == 0U && result.value.capacity == 0U);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(0));
    r_runtime_array_destroy(&result.value);

    r_runtime_allocator_initialize(&allocator);
    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    result = r_std_alloc_bytes(&allocator, 4U, 7U);
    R_TEST_CHECK(result.status == R_STD_ALLOC_CALL_ALLOCATION_ERROR);
    R_TEST_CHECK(result.error == R_STD_ALLOC_ERROR_OUT_OF_MEMORY);
    R_TEST_CHECK(result.value.data == NULL);
    R_TEST_CHECK(result.value.length == 0U && result.value.capacity == 0U);
    r_runtime_array_destroy(&result.value);

    return 0;
}

int main(void) {
    R_TEST_CHECK(R_RUNTIME_ALLOCATOR_MAX_OBJECT_SIZE == (size_t)UINT64_C(2305843009213693951));
    R_TEST_CHECK(R_STD_ALLOC_ERROR_OUT_OF_MEMORY == 0);
    R_TEST_CHECK(R_STD_ALLOC_ERROR_SIZE_OVERFLOW == 1);
    R_TEST_CHECK(R_STD_ALLOC_ERROR_UNSUPPORTED_ALIGNMENT == 2);
    if (r_test_try_new_success_and_release() != 0) {
        return 1;
    }
    if (r_test_try_new_failure_preserves_staged_value() != 0) {
        return 1;
    }
    if (r_test_try_new_size_and_alignment_outcomes() != 0) {
        return 1;
    }
    if (r_test_into_value() != 0) {
        return 1;
    }
    if (r_test_bytes() != 0) {
        return 1;
    }
    return 0;
}

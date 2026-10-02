#include "r_std_string.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define R_TEST_CHECK(expression)                                                                   \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            (void)fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, #expression); \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static RStdStringView r_test_view(const uint8_t *data, size_t length) {
    RStdStringView view = {data, length};
    return view;
}

static _Bool
r_test_string_equals(const RStdString *string, const uint8_t *expected, size_t length) {
    RStdStringView view = r_std_string_as_bytes(string);

    return (view.length == length) &&
           ((length == 0U) || (memcmp(view.data, expected, length) == 0));
}

static int r_test_empty_construction_and_capacity(void) {
    RRuntimeAllocator allocator;
    RStdStringCreateResult empty;
    RStdStringAllocValueResult reserved;
    void *allocation;
    size_t capacity;

    r_runtime_allocator_initialize(&allocator);
    empty = r_std_string_create(&allocator);
    R_TEST_CHECK(empty.status == R_STD_STRING_CALL_SUCCESS);
    R_TEST_CHECK(r_std_string_len(&empty.value) == 0U);
    R_TEST_CHECK(r_std_string_capacity(&empty.value) == 0U);
    R_TEST_CHECK(r_std_string_as_str(&empty.value).data == NULL);
    R_TEST_CHECK(r_std_string_as_bytes(&empty.value).length == 0U);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(0));
    r_runtime_string_destroy(&empty.value);

    reserved = r_std_string_with_capacity(&allocator, 11U);
    R_TEST_CHECK(reserved.status == R_STD_STRING_CALL_SUCCESS);
    R_TEST_CHECK(r_std_string_len(&reserved.value) == 0U);
    R_TEST_CHECK(r_std_string_capacity(&reserved.value) >= 11U);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(1));
    allocation = reserved.value.bytes.data;
    capacity = reserved.value.bytes.capacity;
    R_TEST_CHECK(allocation != NULL);
    r_std_string_clear(&reserved.value);
    R_TEST_CHECK(reserved.value.bytes.data == allocation);
    R_TEST_CHECK(reserved.value.bytes.capacity == capacity);
    R_TEST_CHECK(reserved.value.bytes.length == 0U);
    r_runtime_string_destroy(&reserved.value);
    return 0;
}

static int r_test_construction_validation_and_allocation_order(void) {
    const uint8_t valid_with_nul[] = {
        UINT8_C(0x52),
        UINT8_C(0x00),
        UINT8_C(0xc2),
        UINT8_C(0xa2),
    };
    const uint8_t invalid[] = {
        UINT8_C(0x61),
        UINT8_C(0xe2),
        UINT8_C(0x28),
        UINT8_C(0xa1),
    };
    RRuntimeAllocator allocator;
    RStdStringAllocValueResult from_str;
    RStdStringUtf8ValueResult from_utf8;

    r_runtime_allocator_initialize(&allocator);
    from_str =
        r_std_string_from_str(&allocator, r_test_view(valid_with_nul, sizeof(valid_with_nul)));
    R_TEST_CHECK(from_str.status == R_STD_STRING_CALL_SUCCESS);
    R_TEST_CHECK(r_test_string_equals(&from_str.value, valid_with_nul, sizeof(valid_with_nul)));
    R_TEST_CHECK(r_std_string_as_str(&from_str.value).data != valid_with_nul);
    r_runtime_string_destroy(&from_str.value);

    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    from_utf8 = r_std_string_from_utf8(&allocator, r_test_view(invalid, sizeof(invalid)));
    R_TEST_CHECK(from_utf8.status == R_STD_STRING_CALL_ERROR);
    R_TEST_CHECK(from_utf8.error.kind == R_STD_STRING_ERROR_INVALID_UTF8);
    R_TEST_CHECK(from_utf8.error.invalid_index == 1U);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(0));
    R_TEST_CHECK(from_utf8.value.bytes.data == NULL);
    r_runtime_string_destroy(&from_utf8.value);

    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    from_utf8 =
        r_std_string_from_utf8(&allocator, r_test_view(valid_with_nul, sizeof(valid_with_nul)));
    R_TEST_CHECK(from_utf8.status == R_STD_STRING_CALL_ERROR);
    R_TEST_CHECK(from_utf8.error.kind == R_STD_STRING_ERROR_ALLOCATION_FAILED);
    R_TEST_CHECK(from_utf8.error.allocation_error == R_STD_ALLOC_ERROR_OUT_OF_MEMORY);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(1));
    R_TEST_CHECK(from_utf8.value.bytes.data == NULL);
    r_runtime_string_destroy(&from_utf8.value);

    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    from_utf8 = r_std_string_from_utf8(&allocator, r_test_view(NULL, 0U));
    R_TEST_CHECK(from_utf8.status == R_STD_STRING_CALL_SUCCESS);
    R_TEST_CHECK(r_std_string_len(&from_utf8.value) == 0U);
    R_TEST_CHECK(r_std_string_capacity(&from_utf8.value) == 0U);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(0));
    r_runtime_string_destroy(&from_utf8.value);
    return 0;
}

static int r_test_from_bytes_and_into_bytes_transfer_identity(void) {
    const uint8_t valid[] = {
        UINT8_C(0x52),
        UINT8_C(0x00),
        UINT8_C(0xc2),
        UINT8_C(0xa2),
    };
    const uint8_t invalid[] = {
        UINT8_C(0x78),
        UINT8_C(0xe2),
        UINT8_C(0x28),
        UINT8_C(0xa1),
    };
    RRuntimeAllocator allocator;
    RStdAllocBytesResult bytes;
    RStdStringFromBytesCallResult converted;
    RRuntimeArray recovered = {0};
    void *allocation;
    size_t capacity;
    uint64_t attempts;

    r_runtime_allocator_initialize(&allocator);
    bytes = r_std_alloc_bytes(&allocator, sizeof(valid), UINT8_C(0));
    R_TEST_CHECK(bytes.status == R_STD_ALLOC_CALL_SUCCESS);
    (void)memcpy(bytes.value.data, valid, sizeof(valid));
    allocation = bytes.value.data;
    capacity = bytes.value.capacity;
    attempts = r_runtime_allocator_attempt_count(&allocator);

    converted = r_std_string_from_bytes(&bytes.value);
    R_TEST_CHECK(converted.status == R_STD_STRING_CALL_SUCCESS);
    R_TEST_CHECK(converted.outcome.kind == R_STD_STRING_FROM_BYTES_VALID);
    R_TEST_CHECK(bytes.value.data == NULL);
    R_TEST_CHECK(bytes.value.length == 0U && bytes.value.capacity == 0U);
    R_TEST_CHECK(converted.outcome.valid.bytes.data == allocation);
    R_TEST_CHECK(converted.outcome.valid.bytes.capacity == capacity);
    R_TEST_CHECK(r_test_string_equals(&converted.outcome.valid, valid, sizeof(valid)));
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == attempts);

    R_TEST_CHECK(r_std_string_into_bytes(&converted.outcome.valid, &recovered) ==
                 R_STD_STRING_CALL_SUCCESS);
    R_TEST_CHECK(recovered.data == allocation);
    R_TEST_CHECK(recovered.length == sizeof(valid));
    R_TEST_CHECK(recovered.capacity == capacity);
    R_TEST_CHECK(memcmp(recovered.data, valid, sizeof(valid)) == 0);
    R_TEST_CHECK(converted.outcome.valid.bytes.data == NULL);
    R_TEST_CHECK(converted.outcome.valid.bytes.length == 0U);
    R_TEST_CHECK(converted.outcome.valid.bytes.capacity == 0U);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == attempts);
    r_runtime_array_destroy(&recovered);
    r_runtime_array_destroy(&bytes.value);
    r_runtime_string_destroy(&converted.outcome.valid);

    bytes = r_std_alloc_bytes(&allocator, sizeof(invalid), UINT8_C(0));
    R_TEST_CHECK(bytes.status == R_STD_ALLOC_CALL_SUCCESS);
    (void)memcpy(bytes.value.data, invalid, sizeof(invalid));
    allocation = bytes.value.data;
    capacity = bytes.value.capacity;
    attempts = r_runtime_allocator_attempt_count(&allocator);
    converted = r_std_string_from_bytes(&bytes.value);
    R_TEST_CHECK(converted.status == R_STD_STRING_CALL_SUCCESS);
    R_TEST_CHECK(converted.outcome.kind == R_STD_STRING_FROM_BYTES_INVALID);
    R_TEST_CHECK(converted.outcome.invalid_index == 1U);
    R_TEST_CHECK(converted.outcome.invalid_bytes.data == allocation);
    R_TEST_CHECK(converted.outcome.invalid_bytes.capacity == capacity);
    R_TEST_CHECK(converted.outcome.invalid_bytes.length == sizeof(invalid));
    R_TEST_CHECK(memcmp(converted.outcome.invalid_bytes.data, invalid, sizeof(invalid)) == 0);
    R_TEST_CHECK(bytes.value.data == NULL);
    R_TEST_CHECK(bytes.value.length == 0U && bytes.value.capacity == 0U);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == attempts);
    r_runtime_array_destroy(&converted.outcome.invalid_bytes);
    r_runtime_array_destroy(&bytes.value);
    r_runtime_string_destroy(&converted.outcome.valid);
    return 0;
}

static int r_test_mutation_validation_and_oom_preservation(void) {
    const uint8_t initial[] = {UINT8_C('a'), UINT8_C('b'), UINT8_C('c')};
    const uint8_t invalid_suffix[] = {
        UINT8_C('d'),
        UINT8_C(0xe2),
        UINT8_C(0x28),
        UINT8_C(0xa1),
    };
    const uint8_t valid_suffix[] = {
        UINT8_C('d'),
        UINT8_C('e'),
        UINT8_C('f'),
        UINT8_C('g'),
    };
    RRuntimeAllocator allocator;
    RStdStringUtf8ValueResult created;
    RStdStringErrorResult utf8_result;
    RStdStringAllocResult allocation_result;
    void *allocation;
    size_t capacity;

    r_runtime_allocator_initialize(&allocator);
    created = r_std_string_from_utf8(&allocator, r_test_view(initial, sizeof(initial)));
    R_TEST_CHECK(created.status == R_STD_STRING_CALL_SUCCESS);
    allocation = created.value.bytes.data;
    capacity = created.value.bytes.capacity;

    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    utf8_result = r_std_string_append_utf8(&created.value,
                                           r_test_view(invalid_suffix, sizeof(invalid_suffix)));
    R_TEST_CHECK(utf8_result.status == R_STD_STRING_CALL_ERROR);
    R_TEST_CHECK(utf8_result.error.kind == R_STD_STRING_ERROR_INVALID_UTF8);
    R_TEST_CHECK(utf8_result.error.invalid_index == 1U);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(0));
    R_TEST_CHECK(created.value.bytes.data == allocation);
    R_TEST_CHECK(created.value.bytes.capacity == capacity);
    R_TEST_CHECK(r_test_string_equals(&created.value, initial, sizeof(initial)));

    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    utf8_result =
        r_std_string_append_utf8(&created.value, r_test_view(valid_suffix, sizeof(valid_suffix)));
    R_TEST_CHECK(utf8_result.status == R_STD_STRING_CALL_ERROR);
    R_TEST_CHECK(utf8_result.error.kind == R_STD_STRING_ERROR_ALLOCATION_FAILED);
    R_TEST_CHECK(utf8_result.error.allocation_error == R_STD_ALLOC_ERROR_OUT_OF_MEMORY);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(1));
    R_TEST_CHECK(created.value.bytes.data == allocation);
    R_TEST_CHECK(created.value.bytes.capacity == capacity);
    R_TEST_CHECK(r_test_string_equals(&created.value, initial, sizeof(initial)));

    r_runtime_allocator_set_failure(&allocator, UINT64_C(0));
    allocation_result = r_std_string_reserve(&created.value, SIZE_MAX);
    R_TEST_CHECK(allocation_result.status == R_STD_STRING_CALL_ERROR);
    R_TEST_CHECK(allocation_result.error == R_STD_ALLOC_ERROR_SIZE_OVERFLOW);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(0));
    R_TEST_CHECK(created.value.bytes.data == allocation);
    R_TEST_CHECK(created.value.bytes.capacity == capacity);
    R_TEST_CHECK(r_test_string_equals(&created.value, initial, sizeof(initial)));

    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    allocation_result =
        r_std_string_append_str(&created.value, r_test_view(valid_suffix, sizeof(valid_suffix)));
    R_TEST_CHECK(allocation_result.status == R_STD_STRING_CALL_ERROR);
    R_TEST_CHECK(allocation_result.error == R_STD_ALLOC_ERROR_OUT_OF_MEMORY);
    R_TEST_CHECK(created.value.bytes.data == allocation);
    R_TEST_CHECK(created.value.bytes.capacity == capacity);
    R_TEST_CHECK(r_test_string_equals(&created.value, initial, sizeof(initial)));
    r_runtime_string_destroy(&created.value);
    return 0;
}

static int r_test_scalar_boundaries_truncate_and_clear(void) {
    const uint8_t expected[] = {
        UINT8_C(0x7f),
        UINT8_C(0xc2),
        UINT8_C(0x80),
        UINT8_C(0xdf),
        UINT8_C(0xbf),
        UINT8_C(0xe0),
        UINT8_C(0xa0),
        UINT8_C(0x80),
        UINT8_C(0xf4),
        UINT8_C(0x8f),
        UINT8_C(0xbf),
        UINT8_C(0xbf),
    };
    const uint32_t scalars[] = {
        UINT32_C(0x7f),
        UINT32_C(0x80),
        UINT32_C(0x7ff),
        UINT32_C(0x800),
        UINT32_C(0x10ffff),
    };
    RRuntimeAllocator allocator;
    RStdStringCreateResult created;
    RStdStringAllocResult pushed;
    RStdStringBoundaryResult truncated;
    size_t index;
    void *allocation;
    size_t capacity;

    r_runtime_allocator_initialize(&allocator);
    created = r_std_string_create(&allocator);
    R_TEST_CHECK(created.status == R_STD_STRING_CALL_SUCCESS);
    for (index = 0U; index < (sizeof(scalars) / sizeof(scalars[0])); ++index) {
        pushed = r_std_string_push_scalar(&created.value, scalars[index]);
        R_TEST_CHECK(pushed.status == R_STD_STRING_CALL_SUCCESS);
    }
    R_TEST_CHECK(r_test_string_equals(&created.value, expected, sizeof(expected)));
    allocation = created.value.bytes.data;
    capacity = created.value.bytes.capacity;

    truncated = r_std_string_truncate(&created.value, sizeof(expected) + 1U);
    R_TEST_CHECK(truncated.status == R_STD_STRING_CALL_ERROR);
    R_TEST_CHECK(truncated.error == R_STD_STRING_BOUNDARY_ERROR_OUT_OF_BOUNDS);
    R_TEST_CHECK(r_test_string_equals(&created.value, expected, sizeof(expected)));

    truncated = r_std_string_truncate(&created.value, 2U);
    R_TEST_CHECK(truncated.status == R_STD_STRING_CALL_ERROR);
    R_TEST_CHECK(truncated.error == R_STD_STRING_BOUNDARY_ERROR_NOT_SCALAR_BOUNDARY);
    R_TEST_CHECK(created.value.bytes.capacity == capacity);
    R_TEST_CHECK(r_test_string_equals(&created.value, expected, sizeof(expected)));

    truncated = r_std_string_truncate(&created.value, 3U);
    R_TEST_CHECK(truncated.status == R_STD_STRING_CALL_SUCCESS);
    R_TEST_CHECK(created.value.bytes.data == allocation);
    R_TEST_CHECK(created.value.bytes.capacity == capacity);
    R_TEST_CHECK(r_test_string_equals(&created.value, expected, 3U));

    r_std_string_clear(&created.value);
    R_TEST_CHECK(created.value.bytes.data == allocation);
    R_TEST_CHECK(created.value.bytes.capacity == capacity);
    R_TEST_CHECK(r_std_string_len(&created.value) == 0U);
    R_TEST_CHECK(r_std_string_as_bytes(&created.value).data == NULL);
    r_runtime_string_destroy(&created.value);
    return 0;
}

int main(void) {
    R_TEST_CHECK(R_STD_STRING_ERROR_INVALID_UTF8 == 0);
    R_TEST_CHECK(R_STD_STRING_ERROR_ALLOCATION_FAILED == 1);
    R_TEST_CHECK(R_STD_STRING_BOUNDARY_ERROR_OUT_OF_BOUNDS == 0);
    R_TEST_CHECK(R_STD_STRING_BOUNDARY_ERROR_NOT_SCALAR_BOUNDARY == 1);
    if (r_test_empty_construction_and_capacity() != 0) {
        return 1;
    }
    if (r_test_construction_validation_and_allocation_order() != 0) {
        return 1;
    }
    if (r_test_from_bytes_and_into_bytes_transfer_identity() != 0) {
        return 1;
    }
    if (r_test_mutation_validation_and_oom_preservation() != 0) {
        return 1;
    }
    if (r_test_scalar_boundaries_truncate_and_clear() != 0) {
        return 1;
    }
    return 0;
}

#include "r_std_bytes.h"

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

static int r_test_copy(void) {
    const uint8_t source_bytes[] = {1U, 2U, 3U};
    uint8_t destination_bytes[] = {9U, 9U, 9U, 9U, 9U};
    uint8_t short_destination[] = {7U, 8U};
    const uint8_t short_before[] = {7U, 8U};
    RStdBytesSizeResult result;

    result = r_std_bytes_copy((RStdBytesMutSlice){destination_bytes, sizeof(destination_bytes)},
                              (RStdBytesSlice){source_bytes, sizeof(source_bytes)});
    R_TEST_CHECK(result.is_ok);
    R_TEST_CHECK(result.value == sizeof(source_bytes));
    R_TEST_CHECK(destination_bytes[0] == 1U);
    R_TEST_CHECK(destination_bytes[1] == 2U);
    R_TEST_CHECK(destination_bytes[2] == 3U);
    R_TEST_CHECK(destination_bytes[3] == 9U);

    result = r_std_bytes_copy((RStdBytesMutSlice){short_destination, sizeof(short_destination)},
                              (RStdBytesSlice){source_bytes, sizeof(source_bytes)});
    R_TEST_CHECK(!result.is_ok);
    R_TEST_CHECK(result.error == R_STD_BYTES_ERROR_OUT_OF_BOUNDS);
    R_TEST_CHECK(memcmp(short_destination, short_before, sizeof(short_before)) == 0);

    result = r_std_bytes_copy((RStdBytesMutSlice){NULL, 0U}, (RStdBytesSlice){NULL, 0U});
    R_TEST_CHECK(result.is_ok);
    R_TEST_CHECK(result.value == 0U);
    return 0;
}

static int r_test_copy_within(void) {
    uint8_t bytes[] = {0U, 1U, 2U, 3U, 4U, 5U};
    const uint8_t overlap_right[] = {0U, 0U, 1U, 2U, 3U, 4U};
    const uint8_t overlap_left[] = {1U, 2U, 3U, 4U, 5U, 5U};
    const uint8_t original[] = {0U, 1U, 2U, 3U, 4U, 5U};
    RStdBytesSizeResult result;

    result = r_std_bytes_copy_within((RStdBytesMutSlice){bytes, sizeof(bytes)}, 1U, 0U, 5U);
    R_TEST_CHECK(result.is_ok && result.value == 5U);
    R_TEST_CHECK(memcmp(bytes, overlap_right, sizeof(bytes)) == 0);

    (void)memcpy(bytes, original, sizeof(bytes));
    result = r_std_bytes_copy_within((RStdBytesMutSlice){bytes, sizeof(bytes)}, 0U, 1U, 5U);
    R_TEST_CHECK(result.is_ok && result.value == 5U);
    R_TEST_CHECK(memcmp(bytes, overlap_left, sizeof(bytes)) == 0);

    (void)memcpy(bytes, original, sizeof(bytes));
    result = r_std_bytes_copy_within((RStdBytesMutSlice){bytes, sizeof(bytes)}, SIZE_MAX, 0U, 1U);
    R_TEST_CHECK(!result.is_ok && result.error == R_STD_BYTES_ERROR_RANGE_OVERFLOW);
    R_TEST_CHECK(memcmp(bytes, original, sizeof(bytes)) == 0);

    result = r_std_bytes_copy_within((RStdBytesMutSlice){bytes, sizeof(bytes)}, 0U, SIZE_MAX, 1U);
    R_TEST_CHECK(!result.is_ok && result.error == R_STD_BYTES_ERROR_RANGE_OVERFLOW);
    R_TEST_CHECK(memcmp(bytes, original, sizeof(bytes)) == 0);

    result = r_std_bytes_copy_within((RStdBytesMutSlice){bytes, sizeof(bytes)}, 5U, 0U, 2U);
    R_TEST_CHECK(!result.is_ok && result.error == R_STD_BYTES_ERROR_OUT_OF_BOUNDS);
    R_TEST_CHECK(memcmp(bytes, original, sizeof(bytes)) == 0);

    result = r_std_bytes_copy_within((RStdBytesMutSlice){bytes, sizeof(bytes)}, 0U, 5U, 2U);
    R_TEST_CHECK(!result.is_ok && result.error == R_STD_BYTES_ERROR_OUT_OF_BOUNDS);
    R_TEST_CHECK(memcmp(bytes, original, sizeof(bytes)) == 0);

    result = r_std_bytes_copy_within((RStdBytesMutSlice){NULL, 0U}, 0U, 0U, 0U);
    R_TEST_CHECK(result.is_ok && result.value == 0U);
    return 0;
}

static int r_test_fill_compare_equal(void) {
    uint8_t destination[] = {1U, 2U, 3U, 4U};
    const uint8_t filled[] = {0xA5U, 0xA5U, 0xA5U, 0xA5U};
    const uint8_t lower[] = {0U, 255U};
    const uint8_t higher[] = {1U, 0U};
    const uint8_t prefix[] = {0U};

    r_std_bytes_fill((RStdBytesMutSlice){destination, sizeof(destination)}, UINT8_C(0xA5));
    R_TEST_CHECK(memcmp(destination, filled, sizeof(destination)) == 0);
    r_std_bytes_fill((RStdBytesMutSlice){NULL, 0U}, 0U);

    R_TEST_CHECK(r_std_bytes_compare((RStdBytesSlice){NULL, 0U}, (RStdBytesSlice){NULL, 0U}) == 0);
    R_TEST_CHECK(r_std_bytes_compare((RStdBytesSlice){lower, sizeof(lower)},
                                     (RStdBytesSlice){higher, sizeof(higher)}) == -1);
    R_TEST_CHECK(r_std_bytes_compare((RStdBytesSlice){higher, sizeof(higher)},
                                     (RStdBytesSlice){lower, sizeof(lower)}) == 1);
    R_TEST_CHECK(r_std_bytes_compare((RStdBytesSlice){prefix, sizeof(prefix)},
                                     (RStdBytesSlice){lower, sizeof(lower)}) == -1);
    R_TEST_CHECK(r_std_bytes_equal((RStdBytesSlice){lower, sizeof(lower)},
                                   (RStdBytesSlice){lower, sizeof(lower)}));
    R_TEST_CHECK(!r_std_bytes_equal((RStdBytesSlice){lower, sizeof(lower)},
                                    (RStdBytesSlice){higher, sizeof(higher)}));
    R_TEST_CHECK(!r_std_bytes_equal((RStdBytesSlice){prefix, sizeof(prefix)},
                                    (RStdBytesSlice){lower, sizeof(lower)}));
    return 0;
}

static int r_test_find(void) {
    const uint8_t source[] = {8U, 4U, 8U, 2U, 4U};
    const uint8_t found_pattern[] = {8U, 2U};
    const uint8_t missing_pattern[] = {2U, 8U};
    const uint8_t long_pattern[] = {8U, 4U, 8U, 2U, 4U, 0U};
    RStdBytesIndexOption result;

    result = r_std_bytes_find((RStdBytesSlice){source, sizeof(source)}, 4U);
    R_TEST_CHECK(result.has_value && result.value == 1U);
    result = r_std_bytes_find((RStdBytesSlice){source, sizeof(source)}, 1U);
    R_TEST_CHECK(!result.has_value);
    result = r_std_bytes_find((RStdBytesSlice){NULL, 0U}, 1U);
    R_TEST_CHECK(!result.has_value);

    result = r_std_bytes_find_slice((RStdBytesSlice){source, sizeof(source)},
                                    (RStdBytesSlice){found_pattern, sizeof(found_pattern)});
    R_TEST_CHECK(result.has_value && result.value == 2U);
    result = r_std_bytes_find_slice((RStdBytesSlice){source, sizeof(source)},
                                    (RStdBytesSlice){missing_pattern, sizeof(missing_pattern)});
    R_TEST_CHECK(!result.has_value);
    result = r_std_bytes_find_slice((RStdBytesSlice){source, sizeof(source)},
                                    (RStdBytesSlice){long_pattern, sizeof(long_pattern)});
    R_TEST_CHECK(!result.has_value);
    result = r_std_bytes_find_slice((RStdBytesSlice){source, sizeof(source)},
                                    (RStdBytesSlice){NULL, 0U});
    R_TEST_CHECK(result.has_value && result.value == 0U);
    return 0;
}

static int r_test_prefix_suffix(void) {
    const uint8_t source[] = {1U, 2U, 3U, 4U};
    const uint8_t prefix[] = {1U, 2U};
    const uint8_t suffix[] = {3U, 4U};
    const uint8_t wrong[] = {1U, 3U};
    const uint8_t long_value[] = {1U, 2U, 3U, 4U, 5U};
    RStdBytesSlice source_slice = {source, sizeof(source)};

    R_TEST_CHECK(r_std_bytes_starts_with(source_slice, (RStdBytesSlice){prefix, sizeof(prefix)}));
    R_TEST_CHECK(!r_std_bytes_starts_with(source_slice, (RStdBytesSlice){wrong, sizeof(wrong)}));
    R_TEST_CHECK(
        !r_std_bytes_starts_with(source_slice, (RStdBytesSlice){long_value, sizeof(long_value)}));
    R_TEST_CHECK(r_std_bytes_starts_with(source_slice, (RStdBytesSlice){NULL, 0U}));

    R_TEST_CHECK(r_std_bytes_ends_with(source_slice, (RStdBytesSlice){suffix, sizeof(suffix)}));
    R_TEST_CHECK(!r_std_bytes_ends_with(source_slice, (RStdBytesSlice){wrong, sizeof(wrong)}));
    R_TEST_CHECK(
        !r_std_bytes_ends_with(source_slice, (RStdBytesSlice){long_value, sizeof(long_value)}));
    R_TEST_CHECK(r_std_bytes_ends_with(source_slice, (RStdBytesSlice){NULL, 0U}));
    return 0;
}

static int r_test_owned_bytes(void) {
    RRuntimeAllocator allocator;
    const uint8_t prefix[] = {UINT8_C(0xaa), UINT8_C(0x55)};
    const uint8_t expected[] = {
        UINT8_C(0xaa), UINT8_C(0x55), UINT8_C(0x11), UINT8_C(0x33), UINT8_C(0x22),
        UINT8_C(0x77), UINT8_C(0x66), UINT8_C(0x55), UINT8_C(0x44), UINT8_C(0xff),
        UINT8_C(0xee), UINT8_C(0xdd), UINT8_C(0xcc), UINT8_C(0xbb), UINT8_C(0xaa),
        UINT8_C(0x99), UINT8_C(0x88), UINT8_C(0x01), UINT8_C(0x00),
    };
    RStdBytesArrayResult empty;
    RStdBytesArrayResult created;
    RStdBytesAllocResult appended;

    r_runtime_allocator_initialize(&allocator);
    empty = r_std_bytes_with_capacity(&allocator, 0U);
    R_TEST_CHECK(empty.status == R_STD_ALLOC_CALL_SUCCESS);
    R_TEST_CHECK(empty.value.data == NULL);
    R_TEST_CHECK(empty.value.length == 0U);
    R_TEST_CHECK(empty.value.capacity == 0U);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(0));
    r_runtime_array_destroy(&empty.value);

    created = r_std_bytes_with_capacity(&allocator, 1U);
    R_TEST_CHECK(created.status == R_STD_ALLOC_CALL_SUCCESS);
    R_TEST_CHECK(created.value.length == 0U);
    R_TEST_CHECK(created.value.capacity >= 1U);
    R_TEST_CHECK(created.value.element.size == sizeof(uint8_t));
    R_TEST_CHECK(created.value.element.alignment == _Alignof(uint8_t));

    appended = r_std_bytes_append(&created.value, (RStdBytesSlice){prefix, sizeof(prefix)});
    R_TEST_CHECK(appended.status == R_STD_ALLOC_CALL_SUCCESS);
    appended = r_std_bytes_append_u8(&created.value, UINT8_C(0x11));
    R_TEST_CHECK(appended.status == R_STD_ALLOC_CALL_SUCCESS);
    appended = r_std_bytes_append_u16_le(&created.value, UINT16_C(0x2233));
    R_TEST_CHECK(appended.status == R_STD_ALLOC_CALL_SUCCESS);
    appended = r_std_bytes_append_u32_le(&created.value, UINT32_C(0x44556677));
    R_TEST_CHECK(appended.status == R_STD_ALLOC_CALL_SUCCESS);
    appended = r_std_bytes_append_u64_le(&created.value, UINT64_C(0x8899aabbccddeeff));
    R_TEST_CHECK(appended.status == R_STD_ALLOC_CALL_SUCCESS);
    appended = r_std_bytes_append_u16_le(&created.value, UINT16_C(0x0001));
    R_TEST_CHECK(appended.status == R_STD_ALLOC_CALL_SUCCESS);
    R_TEST_CHECK(created.value.length == sizeof(expected));
    R_TEST_CHECK(memcmp(created.value.data, expected, sizeof(expected)) == 0);

    appended = r_std_bytes_append(&created.value, (RStdBytesSlice){NULL, 0U});
    R_TEST_CHECK(appended.status == R_STD_ALLOC_CALL_SUCCESS);
    R_TEST_CHECK(created.value.length == sizeof(expected));
    r_runtime_array_destroy(&created.value);
    return 0;
}

static int r_test_owned_bytes_failures(void) {
    RRuntimeAllocator allocator;
    const uint8_t initial[] = {1U, 2U, 3U, 4U};
    const uint8_t overflow_source = 0U;
    RStdBytesArrayResult created;
    RStdBytesArrayResult failed_create;
    RStdBytesAllocResult appended;
    void *original_data;
    size_t original_capacity;

    r_runtime_allocator_initialize(&allocator);
    created = r_std_bytes_with_capacity(&allocator, sizeof(initial));
    R_TEST_CHECK(created.status == R_STD_ALLOC_CALL_SUCCESS);
    appended = r_std_bytes_append(&created.value, (RStdBytesSlice){initial, sizeof(initial)});
    R_TEST_CHECK(appended.status == R_STD_ALLOC_CALL_SUCCESS);
    original_data = created.value.data;
    original_capacity = created.value.capacity;

    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    appended = r_std_bytes_append(&created.value, (RStdBytesSlice){NULL, 0U});
    R_TEST_CHECK(appended.status == R_STD_ALLOC_CALL_SUCCESS);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(0));
    appended = r_std_bytes_append_u8(&created.value, UINT8_C(5));
    R_TEST_CHECK(appended.status == R_STD_ALLOC_CALL_ALLOCATION_ERROR);
    R_TEST_CHECK(appended.error == R_STD_ALLOC_ERROR_OUT_OF_MEMORY);
    R_TEST_CHECK(created.value.data == original_data);
    R_TEST_CHECK(created.value.length == sizeof(initial));
    R_TEST_CHECK(created.value.capacity == original_capacity);
    R_TEST_CHECK(memcmp(created.value.data, initial, sizeof(initial)) == 0);

    appended = r_std_bytes_append(&created.value, (RStdBytesSlice){&overflow_source, SIZE_MAX});
    R_TEST_CHECK(appended.status == R_STD_ALLOC_CALL_ALLOCATION_ERROR);
    R_TEST_CHECK(appended.error == R_STD_ALLOC_ERROR_SIZE_OVERFLOW);
    R_TEST_CHECK(created.value.data == original_data);
    R_TEST_CHECK(created.value.length == sizeof(initial));
    R_TEST_CHECK(created.value.capacity == original_capacity);
    R_TEST_CHECK(memcmp(created.value.data, initial, sizeof(initial)) == 0);
    r_runtime_array_destroy(&created.value);

    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    failed_create = r_std_bytes_with_capacity(&allocator, 8U);
    R_TEST_CHECK(failed_create.status == R_STD_ALLOC_CALL_ALLOCATION_ERROR);
    R_TEST_CHECK(failed_create.error == R_STD_ALLOC_ERROR_OUT_OF_MEMORY);
    R_TEST_CHECK(failed_create.value.data == NULL);
    R_TEST_CHECK(failed_create.value.length == 0U);
    R_TEST_CHECK(failed_create.value.capacity == 0U);

    r_runtime_allocator_set_failure(&allocator, UINT64_C(0));
    failed_create = r_std_bytes_with_capacity(&allocator, R_RUNTIME_ALLOCATOR_MAX_OBJECT_SIZE + 1U);
    R_TEST_CHECK(failed_create.status == R_STD_ALLOC_CALL_ALLOCATION_ERROR);
    R_TEST_CHECK(failed_create.error == R_STD_ALLOC_ERROR_SIZE_OVERFLOW);
    R_TEST_CHECK(failed_create.value.data == NULL);
    return 0;
}

int main(void) {
    if (r_test_copy() != 0) {
        return 1;
    }
    if (r_test_copy_within() != 0) {
        return 1;
    }
    if (r_test_fill_compare_equal() != 0) {
        return 1;
    }
    if (r_test_find() != 0) {
        return 1;
    }
    if (r_test_prefix_suffix() != 0) {
        return 1;
    }
    if (r_test_owned_bytes() != 0) {
        return 1;
    }
    if (r_test_owned_bytes_failures() != 0) {
        return 1;
    }
    return 0;
}

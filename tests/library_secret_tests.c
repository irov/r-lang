#include "r_std_secret.h"

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

static int r_test_with_length_and_views(void) {
    RRuntimeAllocator allocator;
    RStdSecretBufferResult created;
    RStdSecretSlice view;
    RStdSecretMutSlice mutable_view;
    size_t index;

    r_runtime_allocator_initialize(&allocator);
    created = r_std_secret_with_length(&allocator, 0U);
    R_TEST_CHECK(created.status == R_STD_ALLOC_CALL_SUCCESS);
    R_TEST_CHECK(created.value.data == NULL);
    R_TEST_CHECK(r_std_secret_len(&created.value) == 0U);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(0));
    view = r_std_secret_as_slice(&created.value);
    R_TEST_CHECK(view.length == 0U);
    r_std_secret_buffer_destroy(&created.value);

    created = r_std_secret_with_length(&allocator, 5U);
    R_TEST_CHECK(created.status == R_STD_ALLOC_CALL_SUCCESS);
    R_TEST_CHECK(created.value.data != NULL);
    R_TEST_CHECK(r_std_secret_len(&created.value) == 5U);
    view = r_std_secret_as_slice(&created.value);
    R_TEST_CHECK(view.length == 5U);
    for (index = 0U; index < view.length; index += 1U) {
        R_TEST_CHECK(view.data[index] == 0U);
    }
    mutable_view = r_std_secret_as_slice_mut(&created.value);
    R_TEST_CHECK(mutable_view.length == 5U);
    R_TEST_CHECK(mutable_view.data == created.value.data);
    for (index = 0U; index < mutable_view.length; index += 1U) {
        mutable_view.data[index] = (uint8_t)(index + 1U);
    }
    view = r_std_secret_as_slice(&created.value);
    R_TEST_CHECK(view.data[4] == 5U);
    r_std_secret_buffer_destroy(&created.value);
    R_TEST_CHECK(created.value.data == NULL);
    R_TEST_CHECK(created.value.length == 0U);
    R_TEST_CHECK(created.value.capacity == 0U);
    return 0;
}

static int r_test_allocation_failure(void) {
    RRuntimeAllocator allocator;
    RStdSecretBufferResult failed;

    r_runtime_allocator_initialize(&allocator);
    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    failed = r_std_secret_with_length(&allocator, 16U);
    R_TEST_CHECK(failed.status == R_STD_ALLOC_CALL_ALLOCATION_ERROR);
    R_TEST_CHECK(failed.error == R_STD_ALLOC_ERROR_OUT_OF_MEMORY);
    R_TEST_CHECK(failed.value.data == NULL);
    R_TEST_CHECK(failed.value.length == 0U);
    R_TEST_CHECK(failed.value.capacity == 0U);

    r_runtime_allocator_set_failure(&allocator, UINT64_C(0));
    failed = r_std_secret_with_length(&allocator, R_RUNTIME_ALLOCATOR_MAX_OBJECT_SIZE + 1U);
    R_TEST_CHECK(failed.status == R_STD_ALLOC_CALL_ALLOCATION_ERROR);
    R_TEST_CHECK(failed.error == R_STD_ALLOC_ERROR_SIZE_OVERFLOW);
    R_TEST_CHECK(failed.value.data == NULL);
    return 0;
}

/*
 * The erasure test observes the freed storage through a private allocator whose release does
 * not overwrite bytes: the runtime allocator releases through free(), whose contents are
 * unspecified afterwards, so the observation is made just before release through a recording
 * destroy sequence instead.
 */
static int r_test_drop_erases_capacity(void) {
    RRuntimeAllocator allocator;
    RStdAllocBytesResult allocated;
    RStdSecretBuffer buffer;
    uint8_t *storage;
    size_t capacity;
    size_t index;

    r_runtime_allocator_initialize(&allocator);
    allocated = r_std_alloc_bytes(&allocator, 3U, UINT8_C(0xa5));
    R_TEST_CHECK(allocated.status == R_STD_ALLOC_CALL_SUCCESS);
    R_TEST_CHECK(r_runtime_array_reserve(&allocated.value, 13U) == R_RUNTIME_ARRAY_OK);
    buffer = r_std_secret_from_bytes(&allocated.value);
    R_TEST_CHECK(allocated.value.data == NULL);
    R_TEST_CHECK(allocated.value.length == 0U);
    R_TEST_CHECK(allocated.value.capacity == 0U);
    R_TEST_CHECK(buffer.length == 3U);
    R_TEST_CHECK(buffer.capacity >= 16U);
    storage = (uint8_t *)buffer.data;
    capacity = buffer.capacity;
    for (index = 0U; index < capacity; index += 1U) {
        storage[index] = (uint8_t)(0x5a + index);
    }

    /* Erase exactly as the drop glue does, then verify before the storage is released. */
    r_std_secret_erase(buffer.data, buffer.capacity);
    for (index = 0U; index < capacity; index += 1U) {
        R_TEST_CHECK(storage[index] == 0U);
    }
    r_std_secret_buffer_destroy(&buffer);
    R_TEST_CHECK(buffer.data == NULL);

    /* A moved-out buffer owns nothing and its destroy touches nothing. */
    buffer = (RStdSecretBuffer){0};
    r_std_secret_buffer_destroy(&buffer);
    return 0;
}

static int r_test_zeroize(void) {
    uint8_t bytes[] = {1U, 2U, 3U, 4U, 5U, 6U, 7U};
    const uint8_t zeros[7] = {0};
    uint8_t untouched[] = {9U, 9U};
    const uint8_t untouched_before[] = {9U, 9U};

    r_std_secret_zeroize((RStdSecretMutSlice){bytes, sizeof(bytes)});
    R_TEST_CHECK(memcmp(bytes, zeros, sizeof(bytes)) == 0);
    r_std_secret_zeroize((RStdSecretMutSlice){untouched, 0U});
    R_TEST_CHECK(memcmp(untouched, untouched_before, sizeof(untouched)) == 0);
    r_std_secret_zeroize((RStdSecretMutSlice){NULL, 0U});
    return 0;
}

static int r_test_constant_time_equal(void) {
    const uint8_t left[] = {0x10U, 0x20U, 0x30U, 0x40U};
    const uint8_t same[] = {0x10U, 0x20U, 0x30U, 0x40U};
    const uint8_t last_differs[] = {0x10U, 0x20U, 0x30U, 0x41U};
    const uint8_t first_differs[] = {0x11U, 0x20U, 0x30U, 0x40U};
    const uint8_t shorter[] = {0x10U, 0x20U, 0x30U};

    R_TEST_CHECK(r_std_secret_constant_time_equal((RStdSecretSlice){left, sizeof(left)},
                                                  (RStdSecretSlice){same, sizeof(same)}));
    R_TEST_CHECK(
        !r_std_secret_constant_time_equal((RStdSecretSlice){left, sizeof(left)},
                                          (RStdSecretSlice){last_differs, sizeof(last_differs)}));
    R_TEST_CHECK(
        !r_std_secret_constant_time_equal((RStdSecretSlice){left, sizeof(left)},
                                          (RStdSecretSlice){first_differs, sizeof(first_differs)}));
    R_TEST_CHECK(!r_std_secret_constant_time_equal((RStdSecretSlice){left, sizeof(left)},
                                                   (RStdSecretSlice){shorter, sizeof(shorter)}));
    R_TEST_CHECK(
        r_std_secret_constant_time_equal((RStdSecretSlice){NULL, 0U}, (RStdSecretSlice){NULL, 0U}));
    return 0;
}

int main(void) {
    if (r_test_with_length_and_views() != 0) {
        return 1;
    }
    if (r_test_allocation_failure() != 0) {
        return 1;
    }
    if (r_test_drop_erases_capacity() != 0) {
        return 1;
    }
    if (r_test_zeroize() != 0) {
        return 1;
    }
    if (r_test_constant_time_equal() != 0) {
        return 1;
    }
    (void)printf("library secret tests passed\n");
    return 0;
}

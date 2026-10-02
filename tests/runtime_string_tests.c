#include "r_runtime_allocator.h"
#include "r_runtime_array.h"
#include "r_runtime_string.h"
#include "r_runtime_type.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int r_runtime_string_fail(const char *message) {
    (void)fprintf(stderr, "runtime string test failure: %s\n", message);
    return 1;
}

int main(void) {
    static const uint8_t initial[] = {UINT8_C(0x68), UINT8_C(0x69)};
    static const uint8_t invalid[] = {UINT8_C(0xe2), UINT8_C(0x82)};
    static const uint8_t euro[] = {UINT8_C(0xe2), UINT8_C(0x82), UINT8_C(0xac)};
    uint8_t allocation_failure_suffix[64];
    RRuntimeAllocator allocator;
    RRuntimeString string;
    RRuntimeArray array;
    RRuntimeArray transferred;
    RRuntimeTypeInfo byte_type = {sizeof(uint8_t), _Alignof(uint8_t), NULL, NULL};
    size_t invalid_index = SIZE_MAX;
    size_t old_capacity;
    void *original_allocation;

    r_runtime_allocator_initialize(&allocator);
    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    if ((r_runtime_string_from_utf8(
             &string, &allocator, invalid, sizeof(invalid), &invalid_index) !=
         R_RUNTIME_STRING_INVALID_UTF8) ||
        (invalid_index != 0U) || (r_runtime_allocator_attempt_count(&allocator) != 0U)) {
        return r_runtime_string_fail("validation precedes allocation");
    }
    if ((r_runtime_string_from_utf8(
             &string, &allocator, initial, sizeof(initial), &invalid_index) !=
         R_RUNTIME_STRING_ALLOCATION_FAILED) ||
        (r_runtime_allocator_attempt_count(&allocator) != 1U)) {
        return r_runtime_string_fail("allocation failure classification");
    }
    r_runtime_allocator_set_failure(&allocator, UINT64_C(0));
    if ((r_runtime_string_from_utf8(
             &string, &allocator, initial, sizeof(initial), &invalid_index) !=
         R_RUNTIME_STRING_OK) ||
        (r_runtime_string_length(&string) != sizeof(initial)) ||
        (memcmp(r_runtime_string_bytes(&string), initial, sizeof(initial)) != 0)) {
        return r_runtime_string_fail("from UTF-8");
    }
    old_capacity = r_runtime_string_capacity(&string);
    if (old_capacity >= sizeof(allocation_failure_suffix)) {
        r_runtime_string_destroy(&string);
        return r_runtime_string_fail("unexpected initial string capacity");
    }
    (void)memset(allocation_failure_suffix, 'x', sizeof(allocation_failure_suffix));
    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    if ((r_runtime_string_append(&string, allocation_failure_suffix, old_capacity + 1U) !=
         R_RUNTIME_STRING_ALLOCATION_FAILED) ||
        (r_runtime_string_length(&string) != sizeof(initial)) ||
        (memcmp(r_runtime_string_bytes(&string), initial, sizeof(initial)) != 0)) {
        r_runtime_string_destroy(&string);
        return r_runtime_string_fail("transactional append failure");
    }
    r_runtime_allocator_set_failure(&allocator, UINT64_C(0));
    if ((r_runtime_string_append_utf8(&string, invalid, sizeof(invalid), &invalid_index) !=
         R_RUNTIME_STRING_INVALID_UTF8) ||
        (r_runtime_string_length(&string) != sizeof(initial)) ||
        (r_runtime_string_push_scalar(&string, UINT32_C(0x20ac)) != R_RUNTIME_STRING_OK) ||
        (r_runtime_string_length(&string) != (sizeof(initial) + sizeof(euro))) ||
        (memcmp(r_runtime_string_bytes(&string) + sizeof(initial), euro, sizeof(euro)) != 0)) {
        r_runtime_string_destroy(&string);
        return r_runtime_string_fail("append validation and scalar");
    }
    old_capacity = r_runtime_string_capacity(&string);
    if ((r_runtime_string_truncate(&string, sizeof(initial) + 1U) !=
         R_RUNTIME_STRING_NOT_SCALAR_BOUNDARY) ||
        (r_runtime_string_truncate(&string, sizeof(initial)) != R_RUNTIME_STRING_OK) ||
        (r_runtime_string_capacity(&string) != old_capacity)) {
        r_runtime_string_destroy(&string);
        return r_runtime_string_fail("truncate boundary and capacity");
    }
    r_runtime_string_destroy(&string);

    if (r_runtime_array_with_capacity(&array, &allocator, byte_type, sizeof(euro)) !=
        R_RUNTIME_ARRAY_OK) {
        return r_runtime_string_fail("byte array setup");
    }
    (void)memcpy(array.data, euro, sizeof(euro));
    array.length = sizeof(euro);
    original_allocation = array.data;
    if ((r_runtime_string_from_bytes(&string, &array, &invalid_index) != R_RUNTIME_STRING_OK) ||
        (array.data != NULL) || (r_runtime_string_bytes(&string) != original_allocation) ||
        (r_runtime_string_into_bytes(&string, &transferred) != R_RUNTIME_STRING_OK) ||
        (transferred.data != original_allocation) || (transferred.length != sizeof(euro))) {
        r_runtime_string_destroy(&string);
        r_runtime_array_destroy(&array);
        return r_runtime_string_fail("allocation-preserving byte transfer");
    }
    r_runtime_array_destroy(&transferred);
    (void)puts("runtime_string_ok");
    return 0;
}

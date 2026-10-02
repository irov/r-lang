#include "r_runtime_string.h"

#include "r_runtime_type.h"
#include "r_runtime_utf8.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static RRuntimeTypeInfo r_runtime_string_byte_type(void) {
    RRuntimeTypeInfo type = {sizeof(uint8_t), _Alignof(uint8_t), NULL, NULL};
    return type;
}

static RRuntimeStringStatus r_runtime_string_array_status(RRuntimeArrayStatus status) {
    switch (status) {
    case R_RUNTIME_ARRAY_OK:
        return R_RUNTIME_STRING_OK;
    case R_RUNTIME_ARRAY_ALLOCATION_FAILED:
        return R_RUNTIME_STRING_ALLOCATION_FAILED;
    case R_RUNTIME_ARRAY_SIZE_OVERFLOW:
        return R_RUNTIME_STRING_SIZE_OVERFLOW;
    case R_RUNTIME_ARRAY_UNSUPPORTED_ALIGNMENT:
        return R_RUNTIME_STRING_UNSUPPORTED_ALIGNMENT;
    case R_RUNTIME_ARRAY_INVALID:
        return R_RUNTIME_STRING_INVALID;
    }
    return R_RUNTIME_STRING_INVALID;
}

static RRuntimeStringStatus r_runtime_string_copy(RRuntimeString *string,
                                                  RRuntimeAllocator *allocator,
                                                  const uint8_t *bytes,
                                                  size_t length) {
    RRuntimeStringStatus status = r_runtime_string_with_capacity(string, allocator, length);

    if (status != R_RUNTIME_STRING_OK) {
        return status;
    }
    if (length != 0U) {
        (void)memcpy(string->bytes.data, bytes, length);
        string->bytes.length = length;
    }
    return R_RUNTIME_STRING_OK;
}

RRuntimeStringStatus r_runtime_string_initialize(RRuntimeString *string,
                                                 RRuntimeAllocator *allocator) {
    r_runtime_array_initialize(&string->bytes, allocator, r_runtime_string_byte_type());
    return R_RUNTIME_STRING_OK;
}

RRuntimeStringStatus r_runtime_string_with_capacity(RRuntimeString *string,
                                                    RRuntimeAllocator *allocator,
                                                    size_t capacity) {
    return r_runtime_string_array_status(r_runtime_array_with_capacity(
        &string->bytes, allocator, r_runtime_string_byte_type(), capacity));
}

RRuntimeStringStatus r_runtime_string_from_utf8(RRuntimeString *string,
                                                RRuntimeAllocator *allocator,
                                                const uint8_t *bytes,
                                                size_t length,
                                                size_t *invalid_index) {
    RRuntimeStringStatus status;

    if (!r_runtime_utf8_validate(bytes, length, invalid_index)) {
        return R_RUNTIME_STRING_INVALID_UTF8;
    }
    status = r_runtime_string_copy(string, allocator, bytes, length);
    return status;
}

RRuntimeStringStatus r_runtime_string_from_valid_utf8(RRuntimeString *string,
                                                      RRuntimeAllocator *allocator,
                                                      const uint8_t *bytes,
                                                      size_t length) {
    return r_runtime_string_copy(string, allocator, bytes, length);
}

RRuntimeStringStatus
r_runtime_string_from_bytes(RRuntimeString *string, RRuntimeArray *bytes, size_t *invalid_index) {
    if (!r_runtime_utf8_validate(bytes->data, bytes->length, invalid_index)) {
        return R_RUNTIME_STRING_INVALID_UTF8;
    }
    string->bytes = *bytes;
    bytes->data = NULL;
    bytes->length = 0U;
    bytes->capacity = 0U;
    return R_RUNTIME_STRING_OK;
}

size_t r_runtime_string_length(const RRuntimeString *string) {
    return string->bytes.length;
}

size_t r_runtime_string_capacity(const RRuntimeString *string) {
    return string->bytes.capacity;
}

const uint8_t *r_runtime_string_bytes(const RRuntimeString *string) {
    if (string->bytes.length == 0U) {
        return NULL;
    }
    return string->bytes.data;
}

RRuntimeStringStatus r_runtime_string_reserve(RRuntimeString *string, size_t additional) {
    return r_runtime_string_array_status(r_runtime_array_reserve(&string->bytes, additional));
}

RRuntimeStringStatus
r_runtime_string_append(RRuntimeString *string, const uint8_t *bytes, size_t length) {
    RRuntimeStringStatus status;

    status = r_runtime_string_reserve(string, length);
    if (status != R_RUNTIME_STRING_OK) {
        return status;
    }
    if (length != 0U) {
        (void)memcpy((uint8_t *)string->bytes.data + string->bytes.length, bytes, length);
        string->bytes.length += length;
    }
    return R_RUNTIME_STRING_OK;
}

RRuntimeStringStatus r_runtime_string_append_utf8(RRuntimeString *string,
                                                  const uint8_t *bytes,
                                                  size_t length,
                                                  size_t *invalid_index) {
    if (!r_runtime_utf8_validate(bytes, length, invalid_index)) {
        return R_RUNTIME_STRING_INVALID_UTF8;
    }
    return r_runtime_string_append(string, bytes, length);
}

RRuntimeStringStatus r_runtime_string_push_scalar(RRuntimeString *string, uint32_t scalar) {
    uint8_t bytes[4];
    size_t length;

    if (!r_runtime_utf8_encode(scalar, bytes, &length)) {
        return R_RUNTIME_STRING_INVALID;
    }
    return r_runtime_string_append(string, bytes, length);
}

RRuntimeStringStatus r_runtime_string_truncate(RRuntimeString *string, size_t byte_length) {
    if (byte_length > string->bytes.length) {
        return R_RUNTIME_STRING_OUT_OF_BOUNDS;
    }
    if (!r_runtime_utf8_is_scalar_boundary(string->bytes.data, string->bytes.length, byte_length)) {
        return R_RUNTIME_STRING_NOT_SCALAR_BOUNDARY;
    }
    string->bytes.length = byte_length;
    return R_RUNTIME_STRING_OK;
}

void r_runtime_string_clear(RRuntimeString *string) {
    string->bytes.length = 0U;
}

RRuntimeStringStatus r_runtime_string_into_bytes(RRuntimeString *string, RRuntimeArray *bytes) {
    *bytes = string->bytes;
    string->bytes.data = NULL;
    string->bytes.length = 0U;
    string->bytes.capacity = 0U;
    return R_RUNTIME_STRING_OK;
}

void r_runtime_string_destroy(RRuntimeString *string) {
    if (string->bytes.allocator == NULL) {
        return;
    }
    r_runtime_array_destroy(&string->bytes);
}

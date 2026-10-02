#ifndef R_RUNTIME_STRING_H
#define R_RUNTIME_STRING_H

#include "r_runtime_allocator.h"
#include "r_runtime_array.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RRuntimeString {
    RRuntimeArray bytes;
} RRuntimeString;

typedef enum RRuntimeStringStatus {
    R_RUNTIME_STRING_OK = 0,
    R_RUNTIME_STRING_INVALID,
    R_RUNTIME_STRING_ALLOCATION_FAILED,
    R_RUNTIME_STRING_SIZE_OVERFLOW,
    R_RUNTIME_STRING_UNSUPPORTED_ALIGNMENT,
    R_RUNTIME_STRING_INVALID_UTF8,
    R_RUNTIME_STRING_OUT_OF_BOUNDS,
    R_RUNTIME_STRING_NOT_SCALAR_BOUNDARY
} RRuntimeStringStatus;

RRuntimeStringStatus r_runtime_string_initialize(RRuntimeString *string,
                                                 RRuntimeAllocator *allocator);
RRuntimeStringStatus r_runtime_string_with_capacity(RRuntimeString *string,
                                                    RRuntimeAllocator *allocator,
                                                    size_t capacity);
RRuntimeStringStatus r_runtime_string_from_utf8(RRuntimeString *string,
                                                RRuntimeAllocator *allocator,
                                                const uint8_t *bytes,
                                                size_t length,
                                                size_t *invalid_index);
/*
 * Trusted compiler/runtime entry. bytes already satisfy the R str UTF-8 invariant. Checked builds
 * verify that invariant; production builds copy without rescanning the input.
 */
RRuntimeStringStatus r_runtime_string_from_valid_utf8(RRuntimeString *string,
                                                      RRuntimeAllocator *allocator,
                                                      const uint8_t *bytes,
                                                      size_t length);
RRuntimeStringStatus
r_runtime_string_from_bytes(RRuntimeString *string, RRuntimeArray *bytes, size_t *invalid_index);
size_t r_runtime_string_length(const RRuntimeString *string);
size_t r_runtime_string_capacity(const RRuntimeString *string);
const uint8_t *r_runtime_string_bytes(const RRuntimeString *string);
RRuntimeStringStatus r_runtime_string_reserve(RRuntimeString *string, size_t additional);
RRuntimeStringStatus
r_runtime_string_append(RRuntimeString *string, const uint8_t *bytes, size_t length);
RRuntimeStringStatus r_runtime_string_append_utf8(RRuntimeString *string,
                                                  const uint8_t *bytes,
                                                  size_t length,
                                                  size_t *invalid_index);
RRuntimeStringStatus r_runtime_string_push_scalar(RRuntimeString *string, uint32_t scalar);
RRuntimeStringStatus r_runtime_string_truncate(RRuntimeString *string, size_t byte_length);
void r_runtime_string_clear(RRuntimeString *string);
RRuntimeStringStatus r_runtime_string_into_bytes(RRuntimeString *string, RRuntimeArray *bytes);
void r_runtime_string_destroy(RRuntimeString *string);

#ifdef __cplusplus
}
#endif

#endif

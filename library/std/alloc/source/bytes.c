#include "r_std_alloc.h"

#include <string.h>

RStdAllocBytesResult r_std_alloc_bytes(RRuntimeAllocator *allocator, size_t length, uint8_t fill) {
    const RRuntimeTypeInfo byte_type = {
        sizeof(uint8_t),
        _Alignof(uint8_t),
        NULL,
        NULL,
    };
    RStdAllocBytesResult result = {0};
    RRuntimeArrayStatus status;

    status = r_runtime_array_with_capacity(&result.value, allocator, byte_type, length);
    if (status == R_RUNTIME_ARRAY_ALLOCATION_FAILED) {
        result.status = R_STD_ALLOC_CALL_ALLOCATION_ERROR;
        result.error = R_STD_ALLOC_REFUSAL();
        return result;
    }
    if (status == R_RUNTIME_ARRAY_SIZE_OVERFLOW) {
        result.status = R_STD_ALLOC_CALL_ALLOCATION_ERROR;
        result.error = R_STD_ALLOC_ERROR_SIZE_OVERFLOW;
        return result;
    }
    if (status == R_RUNTIME_ARRAY_UNSUPPORTED_ALIGNMENT) {
        result.status = R_STD_ALLOC_CALL_ALLOCATION_ERROR;
        result.error = R_STD_ALLOC_ERROR_UNSUPPORTED_ALIGNMENT;
        return result;
    }

    /* The capacity is exactly length: the bytes are written in place. */
    if (length != 0U) {
        memset(result.value.data, fill, length);
        result.value.length = length;
    }
    result.status = R_STD_ALLOC_CALL_SUCCESS;
    return result;
}

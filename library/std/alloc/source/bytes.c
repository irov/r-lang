#include "r_std_alloc.h"

RStdAllocBytesResult r_std_alloc_bytes(RRuntimeAllocator *allocator, size_t length, uint8_t fill) {
    const RRuntimeTypeInfo byte_type = {
        sizeof(uint8_t),
        _Alignof(uint8_t),
        NULL,
        NULL,
    };
    RStdAllocBytesResult result = {0};
    RRuntimeArrayStatus status;
    size_t index;

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

    for (index = 0U; index < length; ++index) {
        uint8_t staged_byte = fill;

        (void)r_runtime_array_push(&result.value, &staged_byte);
    }
    result.status = R_STD_ALLOC_CALL_SUCCESS;
    return result;
}

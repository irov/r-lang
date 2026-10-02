#include "r_library_bytes_internal.h"

#include <string.h>

RStdAllocCallStatus r_library_internal_bytes_map_allocation_status(RRuntimeArrayStatus status,
                                                                   RStdAllocError *error) {
    if (status == R_RUNTIME_ARRAY_OK) {
        return R_STD_ALLOC_CALL_SUCCESS;
    }
    if (status == R_RUNTIME_ARRAY_ALLOCATION_FAILED) {
        *error = R_STD_ALLOC_REFUSAL();
        return R_STD_ALLOC_CALL_ALLOCATION_ERROR;
    }
    if (status == R_RUNTIME_ARRAY_SIZE_OVERFLOW) {
        *error = R_STD_ALLOC_ERROR_SIZE_OVERFLOW;
        return R_STD_ALLOC_CALL_ALLOCATION_ERROR;
    }
    *error = R_STD_ALLOC_ERROR_UNSUPPORTED_ALIGNMENT;
    return R_STD_ALLOC_CALL_ALLOCATION_ERROR;
}

RStdBytesAllocResult
r_library_internal_bytes_append(RStdBytes *target, const uint8_t *source, size_t length) {
    RStdBytesAllocResult result = {0};
    RRuntimeArrayStatus status;

    status = r_runtime_array_reserve(target, length);
    result.status = r_library_internal_bytes_map_allocation_status(status, &result.error);
    if (result.status != R_STD_ALLOC_CALL_SUCCESS) {
        return result;
    }
    if (length != 0U) {
        (void)memcpy((uint8_t *)target->data + target->length, source, length);
        target->length += length;
    }
    return result;
}

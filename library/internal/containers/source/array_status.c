#include "r_library_array_internal.h"

RStdArrayCallStatus r_library_internal_array_map_allocation_status(RRuntimeArrayStatus status,
                                                                   RStdAllocError *error) {
    if (status == R_RUNTIME_ARRAY_OK) {
        return R_STD_ARRAY_CALL_SUCCESS;
    }
    if (status == R_RUNTIME_ARRAY_ALLOCATION_FAILED) {
        *error = R_STD_ALLOC_REFUSAL();
        return R_STD_ARRAY_CALL_ERROR;
    }
    if (status == R_RUNTIME_ARRAY_SIZE_OVERFLOW) {
        *error = R_STD_ALLOC_ERROR_SIZE_OVERFLOW;
        return R_STD_ARRAY_CALL_ERROR;
    }
    if (status == R_RUNTIME_ARRAY_UNSUPPORTED_ALIGNMENT) {
        *error = R_STD_ALLOC_ERROR_UNSUPPORTED_ALIGNMENT;
        return R_STD_ARRAY_CALL_ERROR;
    }
    return R_STD_ARRAY_CALL_CONTRACT_VIOLATION;
}

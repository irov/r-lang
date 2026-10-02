#include "r_library_string_internal.h"

RStdStringCallStatus r_library_internal_string_map_allocation_status(RRuntimeStringStatus status,
                                                                     RStdAllocError *error) {
    if (status == R_RUNTIME_STRING_OK) {
        return R_STD_STRING_CALL_SUCCESS;
    }
    if (status == R_RUNTIME_STRING_ALLOCATION_FAILED) {
        *error = R_STD_ALLOC_REFUSAL();
        return R_STD_STRING_CALL_ERROR;
    }
    if (status == R_RUNTIME_STRING_SIZE_OVERFLOW) {
        *error = R_STD_ALLOC_ERROR_SIZE_OVERFLOW;
        return R_STD_STRING_CALL_ERROR;
    }
    *error = R_STD_ALLOC_ERROR_UNSUPPORTED_ALIGNMENT;
    return R_STD_STRING_CALL_ERROR;
}

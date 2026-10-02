#include "r_library_list_internal.h"

RStdListCallStatus r_library_internal_list_map_allocation_status(RRuntimeListStatus status,
                                                                 RStdAllocError *error) {
    if (status == R_RUNTIME_LIST_OK) {
        return R_STD_LIST_CALL_SUCCESS;
    }
    if (status == R_RUNTIME_LIST_ALLOCATION_FAILED) {
        *error = R_STD_ALLOC_REFUSAL();
        return R_STD_LIST_CALL_ERROR;
    }
    if (status == R_RUNTIME_LIST_SIZE_OVERFLOW) {
        *error = R_STD_ALLOC_ERROR_SIZE_OVERFLOW;
        return R_STD_LIST_CALL_ERROR;
    }
    if (status == R_RUNTIME_LIST_UNSUPPORTED_ALIGNMENT) {
        *error = R_STD_ALLOC_ERROR_UNSUPPORTED_ALIGNMENT;
        return R_STD_LIST_CALL_ERROR;
    }
    return R_STD_LIST_CALL_CONTRACT_VIOLATION;
}

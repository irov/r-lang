#include "r_library_dict_internal.h"

RStdDictCallStatus r_library_internal_dict_map_allocation_status(RRuntimeDictStatus status,
                                                                 RStdAllocError *error) {
    if (status == R_RUNTIME_DICT_OK) {
        return R_STD_DICT_CALL_SUCCESS;
    }
    if (status == R_RUNTIME_DICT_ALLOCATION_FAILED) {
        *error = R_STD_ALLOC_REFUSAL();
        return R_STD_DICT_CALL_ERROR;
    }
    if (status == R_RUNTIME_DICT_SIZE_OVERFLOW) {
        *error = R_STD_ALLOC_ERROR_SIZE_OVERFLOW;
        return R_STD_DICT_CALL_ERROR;
    }
    if (status == R_RUNTIME_DICT_UNSUPPORTED_ALIGNMENT) {
        *error = R_STD_ALLOC_ERROR_UNSUPPORTED_ALIGNMENT;
        return R_STD_DICT_CALL_ERROR;
    }
    return R_STD_DICT_CALL_CONTRACT_VIOLATION;
}

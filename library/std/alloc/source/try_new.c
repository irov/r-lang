#include "r_std_alloc.h"

RStdAllocTryNewResult
r_std_alloc_try_new(RRuntimeAllocator *allocator, RRuntimeTypeInfo type, void *staged_value) {
    RStdAllocTryNewResult result = {0};
    RRuntimeOwnStatus status;

    status = r_runtime_own_create(allocator, type, staged_value, &result.object);
    if (status == R_RUNTIME_OWN_OK) {
        result.status = R_STD_ALLOC_CALL_SUCCESS;
        return result;
    }
    if (status == R_RUNTIME_OWN_ALLOCATION_FAILED) {
        result.status = R_STD_ALLOC_CALL_ALLOCATION_ERROR;
        result.error = R_STD_ALLOC_REFUSAL();
        return result;
    }
    if (status == R_RUNTIME_OWN_SIZE_OVERFLOW) {
        result.status = R_STD_ALLOC_CALL_ALLOCATION_ERROR;
        result.error = R_STD_ALLOC_ERROR_SIZE_OVERFLOW;
        return result;
    }
    if (status == R_RUNTIME_OWN_UNSUPPORTED_ALIGNMENT) {
        result.status = R_STD_ALLOC_CALL_ALLOCATION_ERROR;
        result.error = R_STD_ALLOC_ERROR_UNSUPPORTED_ALIGNMENT;
        return result;
    }
    result.status = R_STD_ALLOC_CALL_SUCCESS;
    return result;
}

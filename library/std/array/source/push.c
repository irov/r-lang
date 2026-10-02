#include "r_std_array.h"

#include "r_library_array_internal.h"

RStdArrayPushResult r_std_array_push(RStdArray *target, void *staged_value) {
    RStdArrayPushResult result = {0};
    RRuntimeArrayStatus status;

    status = r_runtime_array_push(target, staged_value);
    if (status == R_RUNTIME_ARRAY_OK) {
        result.status = R_STD_ARRAY_CALL_SUCCESS;
        return result;
    }
    if (status == R_RUNTIME_ARRAY_OK) {
        result.status = R_STD_ARRAY_CALL_SUCCESS;
    } else {
        result.status = r_library_internal_array_map_allocation_status(status, &result.reason);
    }
    return result;
}

#include "r_std_array.h"

#include "r_library_array_internal.h"

RStdArrayAllocResult r_std_array_reserve(RStdArray *target, size_t additional) {
    RStdArrayAllocResult result = {0};
    RRuntimeArrayStatus status;

    status = r_runtime_array_reserve(target, additional);
    if (status == R_RUNTIME_ARRAY_OK) {
        result.status = R_STD_ARRAY_CALL_SUCCESS;
    } else {
        result.status = r_library_internal_array_map_allocation_status(status, &result.error);
    }
    return result;
}

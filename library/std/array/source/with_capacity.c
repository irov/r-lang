#include "r_std_array.h"

#include "r_library_array_internal.h"

RStdArrayAllocValueResult
r_std_array_with_capacity(RRuntimeAllocator *allocator, RRuntimeTypeInfo element, size_t capacity) {
    RStdArrayAllocValueResult result = {0};
    RRuntimeArrayStatus status;

    status = r_runtime_array_with_capacity(&result.value, allocator, element, capacity);
    if (status == R_RUNTIME_ARRAY_OK) {
        result.status = R_STD_ARRAY_CALL_SUCCESS;
    } else {
        result.status = r_library_internal_array_map_allocation_status(status, &result.error);
    }
    return result;
}

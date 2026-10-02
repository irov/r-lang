#include "r_std_string.h"

#include "r_library_string_internal.h"

RStdStringAllocValueResult r_std_string_with_capacity(RRuntimeAllocator *allocator,
                                                      size_t capacity) {
    RStdStringAllocValueResult result = {0};
    RRuntimeStringStatus status;

    status = r_runtime_string_with_capacity(&result.value, allocator, capacity);
    if (status == R_RUNTIME_STRING_OK) {
        result.status = R_STD_STRING_CALL_SUCCESS;
    } else {
        result.status = r_library_internal_string_map_allocation_status(status, &result.error);
    }
    return result;
}

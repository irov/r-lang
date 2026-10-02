#include "r_std_string.h"

#include "r_library_string_internal.h"

RStdStringUtf8ValueResult r_std_string_from_utf8(RRuntimeAllocator *allocator,
                                                 RStdStringView source) {
    RStdStringUtf8ValueResult result = {0};
    RRuntimeStringStatus status;
    RStdAllocError allocation_error = R_STD_ALLOC_ERROR_OUT_OF_MEMORY;
    size_t invalid_index = 0U;

    status = r_runtime_string_from_utf8(
        &result.value, allocator, source.data, source.length, &invalid_index);
    if (status == R_RUNTIME_STRING_INVALID_UTF8) {
        result.status = R_STD_STRING_CALL_ERROR;
        result.error.kind = R_STD_STRING_ERROR_INVALID_UTF8;
        result.error.invalid_index = invalid_index;
        return result;
    }
    if (status == R_RUNTIME_STRING_OK) {
        result.status = R_STD_STRING_CALL_SUCCESS;
    } else {
        result.status = r_library_internal_string_map_allocation_status(status, &allocation_error);
    }
    if (result.status == R_STD_STRING_CALL_ERROR) {
        result.error.kind = R_STD_STRING_ERROR_ALLOCATION_FAILED;
        result.error.allocation_error = allocation_error;
    }
    return result;
}

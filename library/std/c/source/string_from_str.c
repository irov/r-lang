#include "r_std_c.h"

#include "r_runtime_utf8.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

RStdCStringResult r_std_c_string_from_str(RRuntimeAllocator *allocator, RStdStringView source) {
    RStdCStringResult result = {0};
    RStdAllocBytesResult allocated;
    size_t index;

    for (index = 0U; index < source.length; index += 1U) {
        if (source.data[index] == UINT8_C(0)) {
            result.status = R_STD_C_CALL_ERROR;
            result.error.kind = R_STD_C_STRING_ERROR_EMBEDDED_NUL;
            result.error.index = index;
            return result;
        }
    }
    if (source.length == SIZE_MAX) {
        result.status = R_STD_C_CALL_ERROR;
        result.error.kind = R_STD_C_STRING_ERROR_ALLOCATION_FAILED;
        result.error.allocation_error = R_STD_ALLOC_ERROR_SIZE_OVERFLOW;
        return result;
    }
    allocated = r_std_alloc_bytes(allocator, source.length + 1U, UINT8_C(0));
    if (allocated.status == R_STD_ALLOC_CALL_ALLOCATION_ERROR) {
        result.status = R_STD_C_CALL_ERROR;
        result.error.kind = R_STD_C_STRING_ERROR_ALLOCATION_FAILED;
        result.error.allocation_error = allocated.error;
        return result;
    }
    if (source.length != 0U) {
        (void)memcpy(allocated.value.data, source.data, source.length);
    }
    result.status = R_STD_C_CALL_SUCCESS;
    result.value = allocated.value;
    return result;
}

#include "r_std_format.h"

RStdFormatBuilderResult r_std_format_with_capacity(RRuntimeAllocator *allocator, size_t capacity) {
    RStdStringAllocValueResult created = r_std_string_with_capacity(allocator, capacity);
    RStdFormatBuilderResult result = {0};

    if (created.status == R_STD_STRING_CALL_SUCCESS) {
        result.status = R_STD_FORMAT_CALL_SUCCESS;
        result.value.output = created.value;
    } else {
        result.status = R_STD_FORMAT_CALL_ERROR;
        result.error = created.error;
    }
    return result;
}

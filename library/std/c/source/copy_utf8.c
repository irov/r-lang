#include "r_std_c.h"

RStdCCopyUtf8Result r_std_c_copy_utf8(RRuntimeAllocator *allocator, RStdCCharSlice storage) {
    RStdCCopyUtf8Result result = {0};
    RStdCValidateUtf8Result validated = r_std_c_validate_utf8(storage);
    RStdStringAllocValueResult copied;

    if (validated.status != R_STD_C_CALL_SUCCESS) {
        result.status = validated.status;
        result.error = validated.error;
        return result;
    }
    copied = r_std_string_from_str(allocator, validated.value);
    if (copied.status == R_STD_STRING_CALL_SUCCESS) {
        result.status = R_STD_C_CALL_SUCCESS;
        result.value = copied.value;
        return result;
    }
    result.status = R_STD_C_CALL_ERROR;
    result.error.kind = R_STD_C_STRING_ERROR_ALLOCATION_FAILED;
    result.error.allocation_error = copied.error;
    return result;
}

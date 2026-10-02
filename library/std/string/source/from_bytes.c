#include "r_std_string.h"

RStdStringFromBytesCallResult r_std_string_from_bytes(RRuntimeArray *source) {
    RStdStringFromBytesCallResult result = {0};
    RRuntimeStringStatus status;
    size_t invalid_index = 0U;

    status = r_runtime_string_from_bytes(&result.outcome.valid, source, &invalid_index);
    if (status == R_RUNTIME_STRING_INVALID_UTF8) {
        result.status = R_STD_STRING_CALL_SUCCESS;
        result.outcome.kind = R_STD_STRING_FROM_BYTES_INVALID;
        result.outcome.invalid_bytes = *source;
        result.outcome.invalid_index = invalid_index;
        source->data = NULL;
        source->length = 0U;
        source->capacity = 0U;
        return result;
    }
    result.status = R_STD_STRING_CALL_SUCCESS;
    result.outcome.kind = R_STD_STRING_FROM_BYTES_VALID;
    return result;
}

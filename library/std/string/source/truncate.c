#include "r_std_string.h"

RStdStringBoundaryResult r_std_string_truncate(RStdString *target, size_t byte_length) {
    RStdStringBoundaryResult result = {0};
    RRuntimeStringStatus status;

    status = r_runtime_string_truncate(target, byte_length);
    if (status == R_RUNTIME_STRING_OUT_OF_BOUNDS) {
        result.status = R_STD_STRING_CALL_ERROR;
        result.error = R_STD_STRING_BOUNDARY_ERROR_OUT_OF_BOUNDS;
        return result;
    }
    if (status == R_RUNTIME_STRING_NOT_SCALAR_BOUNDARY) {
        result.status = R_STD_STRING_CALL_ERROR;
        result.error = R_STD_STRING_BOUNDARY_ERROR_NOT_SCALAR_BOUNDARY;
        return result;
    }
    result.status = R_STD_STRING_CALL_SUCCESS;
    return result;
}

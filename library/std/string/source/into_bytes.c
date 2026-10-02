#include "r_std_string.h"

RStdStringCallStatus r_std_string_into_bytes(RStdString *source, RRuntimeArray *result) {
    RRuntimeStringStatus status;

    status = r_runtime_string_into_bytes(source, result);
    (void)status;
    return R_STD_STRING_CALL_SUCCESS;
}

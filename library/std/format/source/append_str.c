#include "r_std_format.h"

RStdFormatAllocResult r_std_format_append_str(RStdFormatBuilder *target, RStdStringView value) {
    RStdStringAllocResult appended;
    RStdFormatAllocResult result = {0};

    appended = r_std_string_append_str(&target->output, value);
    if (appended.status == R_STD_STRING_CALL_SUCCESS) {
        result.status = R_STD_FORMAT_CALL_SUCCESS;
    } else {
        result.status = R_STD_FORMAT_CALL_ERROR;
        result.error = appended.error;
    }
    return result;
}

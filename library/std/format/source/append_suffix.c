#include "r_std_format.h"

#include <stddef.h>
#include <stdint.h>

RStdFormatAppendResult r_std_format_append_suffix(RStdFormatBuilder *target,
                                                  RStdConvertParsedInteger value,
                                                  uint32_t radix) {
    static const uint8_t digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    uint8_t reversed[65];
    uint8_t output[65];
    uint64_t magnitude = value.magnitude;
    size_t count = 0U;
    size_t output_length = 0U;
    RStdStringAllocResult appended;
    RStdFormatAppendResult result = {0};

    if ((radix < UINT32_C(2)) || (radix > UINT32_C(36))) {
        result.status = R_STD_FORMAT_CALL_ERROR;
        result.error.kind = R_STD_FORMAT_ERROR_INVALID_RADIX;
        return result;
    }
    do {
        reversed[count] = digits[magnitude % (uint64_t)radix];
        count += 1U;
        magnitude /= (uint64_t)radix;
    } while (magnitude != UINT64_C(0));

    if (value.negative) {
        output[output_length] = UINT8_C('-');
        output_length += 1U;
    }
    while (count != 0U) {
        count -= 1U;
        output[output_length] = reversed[count];
        output_length += 1U;
    }
    appended = r_std_string_append_str(&target->output, (RStdStringView){output, output_length});
    if (appended.status == R_STD_STRING_CALL_SUCCESS) {
        result.status = R_STD_FORMAT_CALL_SUCCESS;
    } else {
        result.status = R_STD_FORMAT_CALL_ERROR;
        result.error.kind = R_STD_FORMAT_ERROR_ALLOCATION_FAILED;
        result.error.allocation_error = appended.error;
    }
    return result;
}

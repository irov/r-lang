#include "r_std_convert.h"

#include <stddef.h>
#include <stdint.h>

static RStdConvertParseIntegerResult r_std_convert_parse_failure(RStdConvertParseErrorCode code,
                                                                 size_t index) {
    RStdConvertParseIntegerResult result = {0};
    result.status = R_STD_CONVERT_CALL_ERROR;
    result.error.code = code;
    result.error.index = index;
    return result;
}

static int32_t r_std_convert_digit_value(uint8_t byte) {
    if ((byte >= UINT8_C('0')) && (byte <= UINT8_C('9'))) {
        return (int32_t)(byte - UINT8_C('0'));
    }
    if ((byte >= UINT8_C('a')) && (byte <= UINT8_C('z'))) {
        return (int32_t)(byte - UINT8_C('a')) + INT32_C(10);
    }
    if ((byte >= UINT8_C('A')) && (byte <= UINT8_C('Z'))) {
        return (int32_t)(byte - UINT8_C('A')) + INT32_C(10);
    }
    return INT32_C(-1);
}

static _Bool r_std_convert_has_prefix(RStdStringView source, size_t index, uint8_t marker) {
    return ((source.length - index) >= 2U) && (source.data[index] == UINT8_C('0')) &&
           (source.data[index + 1U] == marker);
}

RStdConvertParseIntegerResult
r_std_convert_parse_suffix(RStdStringView source, uint32_t radix, RStdConvertIntegerBounds bounds) {
    RStdConvertParseIntegerResult result = {0};
    uint64_t magnitude = UINT64_C(0);
    uint64_t limit;
    uint32_t actual_radix = radix;
    size_t index = 0U;
    _Bool negative = 0;
    _Bool overflow = 0;
    _Bool found_digit = 0;

    if ((radix != UINT32_C(0)) && ((radix < UINT32_C(2)) || (radix > UINT32_C(36)))) {
        return r_std_convert_parse_failure(R_STD_CONVERT_PARSE_ERROR_INVALID_RADIX, 0U);
    }
    if (source.length == 0U) {
        return r_std_convert_parse_failure(R_STD_CONVERT_PARSE_ERROR_EMPTY, 0U);
    }
    if (source.data[index] == UINT8_C('+')) {
        index += 1U;
    } else if (source.data[index] == UINT8_C('-')) {
        if (bounds.max_negative_magnitude == UINT64_C(0)) {
            return r_std_convert_parse_failure(R_STD_CONVERT_PARSE_ERROR_INVALID_DIGIT, index);
        }
        negative = 1;
        index += 1U;
    }

    if (actual_radix == UINT32_C(0)) {
        actual_radix = UINT32_C(10);
        if (index < source.length) {
            if (r_std_convert_has_prefix(source, index, UINT8_C('b'))) {
                actual_radix = UINT32_C(2);
                index += 2U;
            } else if (r_std_convert_has_prefix(source, index, UINT8_C('o'))) {
                actual_radix = UINT32_C(8);
                index += 2U;
            } else if (r_std_convert_has_prefix(source, index, UINT8_C('x'))) {
                actual_radix = UINT32_C(16);
                index += 2U;
            }
        }
    }

    while (index < source.length) {
        int32_t digit = r_std_convert_digit_value(source.data[index]);

        if (digit < INT32_C(0)) {
            return r_std_convert_parse_failure(found_digit
                                                   ? R_STD_CONVERT_PARSE_ERROR_TRAILING_CHARACTER
                                                   : R_STD_CONVERT_PARSE_ERROR_INVALID_DIGIT,
                                               index);
        }
        if ((uint32_t)digit >= actual_radix) {
            return r_std_convert_parse_failure(R_STD_CONVERT_PARSE_ERROR_INVALID_DIGIT, index);
        }
        found_digit = 1;
        if (!overflow) {
            uint64_t unsigned_digit = (uint64_t)(uint32_t)digit;
            if (magnitude > ((UINT64_MAX - unsigned_digit) / (uint64_t)actual_radix)) {
                overflow = 1;
            } else {
                magnitude = (magnitude * (uint64_t)actual_radix) + unsigned_digit;
            }
        }
        index += 1U;
    }
    if (!found_digit) {
        return r_std_convert_parse_failure(R_STD_CONVERT_PARSE_ERROR_INVALID_DIGIT, source.length);
    }

    limit = negative ? bounds.max_negative_magnitude : bounds.max_positive;
    if (overflow || (magnitude > limit)) {
        return r_std_convert_parse_failure(negative ? R_STD_CONVERT_PARSE_ERROR_BELOW_MINIMUM
                                                    : R_STD_CONVERT_PARSE_ERROR_ABOVE_MAXIMUM,
                                           source.length);
    }
    result.status = R_STD_CONVERT_CALL_SUCCESS;
    result.value.negative = negative;
    result.value.magnitude = magnitude;
    return result;
}

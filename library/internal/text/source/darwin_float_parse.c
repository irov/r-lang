#include "r_library_float_parse_internal.h"

#include <errno.h>
#include <fenv.h>
#include <float.h>
#include <locale.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <xlocale.h>

/*
 * The target binary64 precision and exponent limits imply that every rounding boundary has a
 * terminating decimal expansion with fewer than 2048 significant digits: the largest denominator
 * is 2^1075 and the largest integral boundary has 309 digits. A nonzero discarded suffix is
 * represented by one sticky digit.
 */
#define R_LIBRARY_FLOAT_STORED_DIGITS 2048U
#define R_LIBRARY_FLOAT_CANONICAL_CAPACITY 2080U
#define R_LIBRARY_FLOAT_EXPONENT_LIMIT INT64_C(1000000)

_Static_assert(FLT_RADIX == 2, "arm64-apple-darwin requires binary floating point");
_Static_assert(FLT_MANT_DIG == 24, "arm64-apple-darwin float format changed");
_Static_assert(FLT_MAX_EXP == 128, "arm64-apple-darwin float exponent changed");
_Static_assert(DBL_MANT_DIG == 53, "arm64-apple-darwin double format changed");
_Static_assert(DBL_MAX_EXP == 1024, "arm64-apple-darwin double exponent changed");
_Static_assert(LDBL_MANT_DIG == 53, "arm64-apple-darwin long double format changed");
_Static_assert(LDBL_MAX_EXP == 1024, "arm64-apple-darwin long double exponent changed");

typedef enum RLibraryFloatSpecial {
    R_LIBRARY_FLOAT_SPECIAL_NONE = 0,
    R_LIBRARY_FLOAT_SPECIAL_INFINITY = 1,
    R_LIBRARY_FLOAT_SPECIAL_NAN = 2
} RLibraryFloatSpecial;

typedef struct RLibraryFloatToken {
    _Bool negative;
    RLibraryFloatSpecial special;
    uint8_t digits[R_LIBRARY_FLOAT_STORED_DIGITS];
    size_t stored_digit_count;
    int64_t scientific_exponent;
    _Bool discarded_nonzero;
} RLibraryFloatToken;

static const uint8_t r_library_float_max_f32[] = "340282346638528859811704183484516925440";
static const uint8_t r_library_float_max_f64[] =
    "179769313486231570814527423731704356798070567525844996598917476803157260780028538760"
    "589558632766878171540458953514382464234321326889464182768467546703537516986049910576"
    "551282076245490090389328944075868508455133942304583236903222948165808559332123348274"
    "797826204144723168738177180919299881250404026184124858368";

static RLibraryFloatParseResult r_library_float_failure(RStdConvertParseErrorCode code,
                                                        size_t index) {
    RLibraryFloatParseResult result = {0};

    result.status = R_STD_CONVERT_CALL_ERROR;
    result.error.code = code;
    result.error.index = index;
    return result;
}

static RLibraryFloatParseResult r_library_float_contract_violation(void) {
    RLibraryFloatParseResult result = {0};

    result.status = R_STD_CONVERT_CALL_CONTRACT_VIOLATION;
    return result;
}

static RLibraryFloatParseResult r_library_float_success(RLibraryFloatValue value) {
    RLibraryFloatParseResult result = {0};

    result.status = R_STD_CONVERT_CALL_SUCCESS;
    result.value = value;
    return result;
}

static _Bool r_library_float_is_digit(uint8_t byte) {
    return (byte >= UINT8_C('0')) && (byte <= UINT8_C('9'));
}

static RLibraryFloatParseResult r_library_float_parse_special(RStdStringView source,
                                                              size_t start,
                                                              const char *literal,
                                                              size_t literal_length,
                                                              _Bool negative,
                                                              RLibraryFloatSpecial special,
                                                              RLibraryFloatToken *token) {
    size_t offset;

    for (offset = 0U; offset < literal_length; offset += 1U) {
        size_t index = start + offset;

        if (index >= source.length) {
            return r_library_float_failure(R_STD_CONVERT_PARSE_ERROR_INVALID_DIGIT, source.length);
        }
        if (source.data[index] != (uint8_t)literal[offset]) {
            return r_library_float_failure(R_STD_CONVERT_PARSE_ERROR_INVALID_DIGIT, index);
        }
    }
    if ((start + literal_length) != source.length) {
        return r_library_float_failure(R_STD_CONVERT_PARSE_ERROR_TRAILING_CHARACTER,
                                       start + literal_length);
    }
    token->negative = negative;
    token->special = special;
    return r_library_float_success((RLibraryFloatValue){0});
}

static void r_library_float_store_digit(RLibraryFloatToken *token, uint8_t digit) {
    if (token->stored_digit_count < R_LIBRARY_FLOAT_STORED_DIGITS) {
        token->digits[token->stored_digit_count] = digit;
        token->stored_digit_count += 1U;
    } else if (digit != UINT8_C('0')) {
        token->discarded_nonzero = 1;
    }
}

static size_t r_library_float_exponent_parse_limit(size_t source_length) {
    size_t remaining = SIZE_MAX - source_length;

    /* Preserve every cancellation that can bring the mantissa offset back into this limit. */
    if (remaining <= ((size_t)R_LIBRARY_FLOAT_EXPONENT_LIMIT + 1U)) {
        return SIZE_MAX;
    }
    return source_length + (size_t)R_LIBRARY_FLOAT_EXPONENT_LIMIT + 1U;
}

static int64_t r_library_float_combine_exponents(_Bool explicit_negative,
                                                 size_t explicit_magnitude,
                                                 size_t integer_digits,
                                                 size_t leading_zeros) {
    _Bool decimal_negative;
    size_t decimal_magnitude;
    _Bool result_negative;
    size_t result_magnitude;

    if (integer_digits > leading_zeros) {
        decimal_negative = 0;
        decimal_magnitude = integer_digits - leading_zeros - 1U;
    } else {
        decimal_negative = 1;
        decimal_magnitude = (leading_zeros - integer_digits) + 1U;
    }
    if (explicit_magnitude == 0U) {
        explicit_negative = 0;
    }
    if (decimal_magnitude == 0U) {
        decimal_negative = 0;
    }
    if (explicit_negative == decimal_negative) {
        result_negative = explicit_negative;
        if ((explicit_magnitude >= (size_t)R_LIBRARY_FLOAT_EXPONENT_LIMIT) ||
            (decimal_magnitude >= ((size_t)R_LIBRARY_FLOAT_EXPONENT_LIMIT - explicit_magnitude))) {
            result_magnitude = (size_t)R_LIBRARY_FLOAT_EXPONENT_LIMIT;
        } else {
            result_magnitude = explicit_magnitude + decimal_magnitude;
        }
    } else if (explicit_magnitude >= decimal_magnitude) {
        result_negative = explicit_negative;
        result_magnitude = explicit_magnitude - decimal_magnitude;
    } else {
        result_negative = decimal_negative;
        result_magnitude = decimal_magnitude - explicit_magnitude;
    }
    if (result_magnitude > (size_t)R_LIBRARY_FLOAT_EXPONENT_LIMIT) {
        result_magnitude = (size_t)R_LIBRARY_FLOAT_EXPONENT_LIMIT;
    }
    if (result_negative) {
        return -(int64_t)result_magnitude;
    }
    return (int64_t)result_magnitude;
}

static RLibraryFloatParseResult r_library_float_parse_decimal(RStdStringView source,
                                                              RLibraryFloatToken *token) {
    size_t index = 0U;
    size_t integer_digits = 0U;
    size_t leading_zeros = 0U;
    size_t mantissa_digits = 0U;
    size_t explicit_exponent_magnitude = 0U;
    _Bool explicit_exponent_negative = 0;
    _Bool found_nonzero = 0;

    if ((source.data[index] == UINT8_C('+')) || (source.data[index] == UINT8_C('-'))) {
        token->negative = source.data[index] == UINT8_C('-');
        index += 1U;
    }
    while ((index < source.length) && r_library_float_is_digit(source.data[index])) {
        uint8_t digit = source.data[index];

        integer_digits += 1U;
        mantissa_digits += 1U;
        if (!found_nonzero) {
            if (digit == UINT8_C('0')) {
                leading_zeros += 1U;
            } else {
                found_nonzero = 1;
                r_library_float_store_digit(token, digit);
            }
        } else {
            r_library_float_store_digit(token, digit);
        }
        index += 1U;
    }
    if ((index < source.length) && (source.data[index] == UINT8_C('.'))) {
        index += 1U;
        while ((index < source.length) && r_library_float_is_digit(source.data[index])) {
            uint8_t digit = source.data[index];

            mantissa_digits += 1U;
            if (!found_nonzero) {
                if (digit == UINT8_C('0')) {
                    leading_zeros += 1U;
                } else {
                    found_nonzero = 1;
                    r_library_float_store_digit(token, digit);
                }
            } else {
                r_library_float_store_digit(token, digit);
            }
            index += 1U;
        }
    }
    if (mantissa_digits == 0U) {
        return r_library_float_failure(R_STD_CONVERT_PARSE_ERROR_INVALID_DIGIT, index);
    }
    if ((index < source.length) &&
        ((source.data[index] == UINT8_C('e')) || (source.data[index] == UINT8_C('E')))) {
        size_t magnitude = 0U;
        size_t magnitude_limit = r_library_float_exponent_parse_limit(source.length);
        _Bool found_exponent_digit = 0;

        index += 1U;
        if ((index < source.length) &&
            ((source.data[index] == UINT8_C('+')) || (source.data[index] == UINT8_C('-')))) {
            explicit_exponent_negative = source.data[index] == UINT8_C('-');
            index += 1U;
        }
        while ((index < source.length) && r_library_float_is_digit(source.data[index])) {
            size_t digit = (size_t)(source.data[index] - UINT8_C('0'));

            found_exponent_digit = 1;
            if (magnitude <= ((magnitude_limit - digit) / 10U)) {
                magnitude = (magnitude * 10U) + digit;
            } else {
                magnitude = magnitude_limit;
            }
            index += 1U;
        }
        if (!found_exponent_digit) {
            return r_library_float_failure(R_STD_CONVERT_PARSE_ERROR_INVALID_DIGIT, index);
        }
        explicit_exponent_magnitude = magnitude;
    }
    if (index != source.length) {
        return r_library_float_failure(R_STD_CONVERT_PARSE_ERROR_TRAILING_CHARACTER, index);
    }
    if (found_nonzero) {
        token->scientific_exponent = r_library_float_combine_exponents(
            explicit_exponent_negative, explicit_exponent_magnitude, integer_digits, leading_zeros);
    }
    return r_library_float_success((RLibraryFloatValue){0});
}

static RLibraryFloatParseResult r_library_float_lex(RStdStringView source,
                                                    RLibraryFloatToken *token) {
    if (source.length == 0U) {
        return r_library_float_failure(R_STD_CONVERT_PARSE_ERROR_EMPTY, 0U);
    }
    if (source.data[0] == UINT8_C('i')) {
        return r_library_float_parse_special(
            source, 0U, "inf", 3U, 0, R_LIBRARY_FLOAT_SPECIAL_INFINITY, token);
    }
    if (source.data[0] == UINT8_C('n')) {
        return r_library_float_parse_special(
            source, 0U, "nan", 3U, 0, R_LIBRARY_FLOAT_SPECIAL_NAN, token);
    }
    if ((source.length > 1U) && (source.data[0] == UINT8_C('-')) &&
        (source.data[1] == UINT8_C('i'))) {
        return r_library_float_parse_special(
            source, 1U, "inf", 3U, 1, R_LIBRARY_FLOAT_SPECIAL_INFINITY, token);
    }
    return r_library_float_parse_decimal(source, token);
}

static _Bool r_library_float_exceeds_maximum(const RLibraryFloatToken *token,
                                             RLibraryFloatDestination destination) {
    const uint8_t *maximum;
    size_t maximum_length;
    int64_t maximum_exponent;
    size_t index;

    if ((destination == R_LIBRARY_FLOAT_DESTINATION_F32) ||
        (destination == R_LIBRARY_FLOAT_DESTINATION_C_FLOAT)) {
        maximum = r_library_float_max_f32;
        maximum_length = sizeof(r_library_float_max_f32) - 1U;
        maximum_exponent = INT64_C(38);
    } else {
        maximum = r_library_float_max_f64;
        maximum_length = sizeof(r_library_float_max_f64) - 1U;
        maximum_exponent = INT64_C(308);
    }
    if (token->scientific_exponent != maximum_exponent) {
        return token->scientific_exponent > maximum_exponent;
    }
    for (index = 0U; index < maximum_length; index += 1U) {
        uint8_t input_digit =
            index < token->stored_digit_count ? token->digits[index] : UINT8_C('0');

        if (input_digit != maximum[index]) {
            return input_digit > maximum[index];
        }
    }
    for (index = maximum_length; index < token->stored_digit_count; index += 1U) {
        if (token->digits[index] != UINT8_C('0')) {
            return 1;
        }
    }
    return token->discarded_nonzero;
}

static size_t r_library_float_append_exponent(char *output, size_t index, int64_t exponent) {
    char reversed[32];
    size_t count = 0U;
    uint64_t magnitude;

    if (exponent < INT64_C(0)) {
        output[index] = '-';
        index += 1U;
        magnitude = (uint64_t)(-exponent);
    } else {
        magnitude = (uint64_t)exponent;
    }
    do {
        reversed[count] = (char)('0' + (char)(magnitude % UINT64_C(10)));
        count += 1U;
        magnitude /= UINT64_C(10);
    } while (magnitude != UINT64_C(0));
    while (count != 0U) {
        count -= 1U;
        output[index] = reversed[count];
        index += 1U;
    }
    return index;
}

static void r_library_float_build_canonical(const RLibraryFloatToken *token, char *canonical) {
    size_t index = 0U;
    size_t digit_index;

    if (token->negative) {
        canonical[index] = '-';
        index += 1U;
    }
    canonical[index] = (char)token->digits[0];
    index += 1U;
    if ((token->stored_digit_count > 1U) || token->discarded_nonzero) {
        canonical[index] = '.';
        index += 1U;
        for (digit_index = 1U; digit_index < token->stored_digit_count; digit_index += 1U) {
            canonical[index] = (char)token->digits[digit_index];
            index += 1U;
        }
        if (token->discarded_nonzero) {
            canonical[index] = '1';
            index += 1U;
        }
    }
    canonical[index] = 'e';
    index += 1U;
    index = r_library_float_append_exponent(canonical, index, token->scientific_exponent);
    canonical[index] = '\0';
}

static _Bool r_library_float_convert(const char *canonical,
                                     RLibraryFloatDestination destination,
                                     RLibraryFloatValue *value) {
    fenv_t environment;
    int saved_errno = errno;
    _Bool success = 1;

    if (feholdexcept(&environment) != 0) {
        errno = saved_errno;
        return 0;
    }
    if (fesetround(FE_TONEAREST) != 0) {
        (void)fesetenv(&environment);
        errno = saved_errno;
        return 0;
    }
    errno = 0;
    if (destination == R_LIBRARY_FLOAT_DESTINATION_F32) {
        value->f32 = strtof_l(canonical, NULL, LC_C_LOCALE);
    } else if (destination == R_LIBRARY_FLOAT_DESTINATION_F64) {
        value->f64 = strtod_l(canonical, NULL, LC_C_LOCALE);
    } else if (destination == R_LIBRARY_FLOAT_DESTINATION_C_FLOAT) {
        value->c_float = strtof_l(canonical, NULL, LC_C_LOCALE);
    } else if (destination == R_LIBRARY_FLOAT_DESTINATION_C_DOUBLE) {
        value->c_double = strtod_l(canonical, NULL, LC_C_LOCALE);
    } else if (destination == R_LIBRARY_FLOAT_DESTINATION_C_LONG_DOUBLE) {
        value->c_long_double = strtold_l(canonical, NULL, LC_C_LOCALE);
    } else {
        success = 0;
    }
    if (fesetenv(&environment) != 0) {
        success = 0;
    }
    errno = saved_errno;
    return success;
}

static RLibraryFloatValue r_library_float_zero(RLibraryFloatDestination destination,
                                               _Bool negative) {
    RLibraryFloatValue value = {0};

    if (destination == R_LIBRARY_FLOAT_DESTINATION_F32) {
        value.f32 = negative ? -0.0F : 0.0F;
    } else if (destination == R_LIBRARY_FLOAT_DESTINATION_F64) {
        value.f64 = negative ? -0.0 : 0.0;
    } else if (destination == R_LIBRARY_FLOAT_DESTINATION_C_FLOAT) {
        value.c_float = negative ? -0.0F : 0.0F;
    } else if (destination == R_LIBRARY_FLOAT_DESTINATION_C_DOUBLE) {
        value.c_double = negative ? -0.0 : 0.0;
    } else {
        value.c_long_double = negative ? -0.0L : 0.0L;
    }
    return value;
}

static RLibraryFloatValue r_library_float_special_value(RLibraryFloatDestination destination,
                                                        RLibraryFloatSpecial special,
                                                        _Bool negative) {
    RLibraryFloatValue value = {0};
    long double base =
        special == R_LIBRARY_FLOAT_SPECIAL_NAN ? (long double)NAN : (long double)INFINITY;

    if (negative) {
        base = -base;
    }
    if (destination == R_LIBRARY_FLOAT_DESTINATION_F32) {
        value.f32 = (float)base;
    } else if (destination == R_LIBRARY_FLOAT_DESTINATION_F64) {
        value.f64 = (double)base;
    } else if (destination == R_LIBRARY_FLOAT_DESTINATION_C_FLOAT) {
        value.c_float = (float)base;
    } else if (destination == R_LIBRARY_FLOAT_DESTINATION_C_DOUBLE) {
        value.c_double = (double)base;
    } else {
        value.c_long_double = base;
    }
    return value;
}

RLibraryFloatParseResult r_library_internal_float_parse(RStdStringView source,
                                                        RLibraryFloatDestination destination) {
    RLibraryFloatToken token = {0};
    RLibraryFloatParseResult lexical;
    RLibraryFloatValue value = {0};
    char canonical[R_LIBRARY_FLOAT_CANONICAL_CAPACITY];

    lexical = r_library_float_lex(source, &token);
    if (lexical.status != R_STD_CONVERT_CALL_SUCCESS) {
        return lexical;
    }
    if (token.special != R_LIBRARY_FLOAT_SPECIAL_NONE) {
        return r_library_float_success(
            r_library_float_special_value(destination, token.special, token.negative));
    }
    if (token.stored_digit_count == 0U) {
        return r_library_float_success(r_library_float_zero(destination, token.negative));
    }
    if (r_library_float_exceeds_maximum(&token, destination)) {
        return r_library_float_failure(token.negative ? R_STD_CONVERT_PARSE_ERROR_BELOW_MINIMUM
                                                      : R_STD_CONVERT_PARSE_ERROR_ABOVE_MAXIMUM,
                                       source.length);
    }
    r_library_float_build_canonical(&token, canonical);
    if (!r_library_float_convert(canonical, destination, &value)) {
        return r_library_float_contract_violation();
    }
    return r_library_float_success(value);
}

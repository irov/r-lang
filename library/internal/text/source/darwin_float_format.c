#include "r_library_float_format_internal.h"

#include "r_library_string_internal.h"

#include <errno.h>
#include <fenv.h>
#include <float.h>
#include <locale.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xlocale.h>

#pragma STDC FENV_ACCESS ON

#define R_LIBRARY_FLOAT_EXACT_CAPACITY 1536U
#define R_LIBRARY_FLOAT_RENDER_CAPACITY 1536U
#define R_LIBRARY_FLOAT_RESULT_CAPACITY 64U
#define R_LIBRARY_FLOAT_EXACT_PRECISION 1074
#define R_LIBRARY_FLOAT_CANDIDATE_DIGITS 24U
#define R_LIBRARY_FLOAT_COEFFICIENT_CAPACITY 26U

_Static_assert(FLT_RADIX == 2, "arm64-apple-darwin requires binary floating point");
_Static_assert(FLT_MANT_DIG == 24, "arm64-apple-darwin float format changed");
_Static_assert(FLT_MAX_EXP == 128, "arm64-apple-darwin float exponent changed");
_Static_assert(DBL_MANT_DIG == 53, "arm64-apple-darwin double format changed");
_Static_assert(DBL_MAX_EXP == 1024, "arm64-apple-darwin double exponent changed");
_Static_assert(LDBL_MANT_DIG == 53, "arm64-apple-darwin long double format changed");
_Static_assert(LDBL_MAX_EXP == 1024, "arm64-apple-darwin long double exponent changed");

typedef struct RLibraryFloatExact {
    uint8_t digits[R_LIBRARY_FLOAT_EXACT_CAPACITY];
    size_t length;
    int64_t scientific_exponent;
} RLibraryFloatExact;

typedef struct RLibraryFloatCoefficient {
    uint8_t digits[R_LIBRARY_FLOAT_COEFFICIENT_CAPACITY];
    size_t length;
    int64_t decimal_power;
} RLibraryFloatCoefficient;

typedef struct RLibraryFloatBest {
    uint8_t bytes[R_LIBRARY_FLOAT_RESULT_CAPACITY];
    size_t length;
    uint8_t distance[R_LIBRARY_FLOAT_EXACT_CAPACITY];
    size_t distance_length;
    _Bool even_last_digit;
    _Bool fixed_notation;
    _Bool present;
} RLibraryFloatBest;

typedef struct RLibraryFloatEnvironment {
    fenv_t floating_environment;
    int saved_errno;
    _Bool floating_environment_held;
} RLibraryFloatEnvironment;

static _Bool r_library_float_environment_enter(RLibraryFloatEnvironment *environment) {
    environment->saved_errno = errno;
    if (feholdexcept(&environment->floating_environment) != 0) {
        errno = environment->saved_errno;
        return 0;
    }
    environment->floating_environment_held = 1;
    if (fesetround(FE_TONEAREST) != 0) {
        (void)fesetenv(&environment->floating_environment);
        environment->floating_environment_held = 0;
        errno = environment->saved_errno;
        return 0;
    }
    return 1;
}

static _Bool r_library_float_environment_leave(RLibraryFloatEnvironment *environment) {
    _Bool success = 1;

    if (environment->floating_environment_held) {
        if (fesetenv(&environment->floating_environment) != 0) {
            success = 0;
        }
        environment->floating_environment_held = 0;
    }
    errno = environment->saved_errno;
    return success;
}

static _Bool r_library_float_properties(RLibraryFloatFormatValue value,
                                        RLibraryFloatFormatDestination destination,
                                        _Bool *negative,
                                        _Bool *not_a_number,
                                        _Bool *infinite,
                                        _Bool *zero) {
    if (destination == R_LIBRARY_FLOAT_FORMAT_F32) {
        *negative = signbit(value.f32) != 0;
        *not_a_number = isnan(value.f32) != 0;
        *infinite = isinf(value.f32) != 0;
        *zero = value.f32 == 0.0F;
    } else if (destination == R_LIBRARY_FLOAT_FORMAT_F64) {
        *negative = signbit(value.f64) != 0;
        *not_a_number = isnan(value.f64) != 0;
        *infinite = isinf(value.f64) != 0;
        *zero = value.f64 == 0.0;
    } else if (destination == R_LIBRARY_FLOAT_FORMAT_C_FLOAT) {
        *negative = signbit(value.c_float) != 0;
        *not_a_number = isnan(value.c_float) != 0;
        *infinite = isinf(value.c_float) != 0;
        *zero = value.c_float == 0.0F;
    } else if (destination == R_LIBRARY_FLOAT_FORMAT_C_DOUBLE) {
        *negative = signbit(value.c_double) != 0;
        *not_a_number = isnan(value.c_double) != 0;
        *infinite = isinf(value.c_double) != 0;
        *zero = value.c_double == 0.0;
    } else if (destination == R_LIBRARY_FLOAT_FORMAT_C_LONG_DOUBLE) {
        *negative = signbit(value.c_long_double) != 0;
        *not_a_number = isnan(value.c_long_double) != 0;
        *infinite = isinf(value.c_long_double) != 0;
        *zero = value.c_long_double == 0.0L;
    } else {
        return 0;
    }
    return 1;
}

static int r_library_float_exact_snprintf(char *output,
                                          size_t capacity,
                                          RLibraryFloatFormatValue value,
                                          RLibraryFloatFormatDestination destination,
                                          _Bool negative) {
    if (destination == R_LIBRARY_FLOAT_FORMAT_F32) {
        double magnitude = negative ? -(double)value.f32 : (double)value.f32;

        return snprintf_l(
            output, capacity, LC_C_LOCALE, "%.*f", R_LIBRARY_FLOAT_EXACT_PRECISION, magnitude);
    }
    if (destination == R_LIBRARY_FLOAT_FORMAT_F64) {
        double magnitude = negative ? -value.f64 : value.f64;

        return snprintf_l(
            output, capacity, LC_C_LOCALE, "%.*f", R_LIBRARY_FLOAT_EXACT_PRECISION, magnitude);
    }
    if (destination == R_LIBRARY_FLOAT_FORMAT_C_FLOAT) {
        double magnitude = negative ? -(double)value.c_float : (double)value.c_float;

        return snprintf_l(
            output, capacity, LC_C_LOCALE, "%.*f", R_LIBRARY_FLOAT_EXACT_PRECISION, magnitude);
    }
    if (destination == R_LIBRARY_FLOAT_FORMAT_C_DOUBLE) {
        double magnitude = negative ? -value.c_double : value.c_double;

        return snprintf_l(
            output, capacity, LC_C_LOCALE, "%.*f", R_LIBRARY_FLOAT_EXACT_PRECISION, magnitude);
    }
    {
        long double magnitude = negative ? -value.c_long_double : value.c_long_double;

        return snprintf_l(
            output, capacity, LC_C_LOCALE, "%.*Lf", R_LIBRARY_FLOAT_EXACT_PRECISION, magnitude);
    }
}

static _Bool r_library_float_extract_exact(RLibraryFloatFormatValue value,
                                           RLibraryFloatFormatDestination destination,
                                           _Bool negative,
                                           RLibraryFloatExact *exact) {
    char output[R_LIBRARY_FLOAT_EXACT_CAPACITY];
    int formatted =
        r_library_float_exact_snprintf(output, sizeof(output), value, destination, negative);
    size_t output_length;
    size_t decimal_point = SIZE_MAX;
    size_t trimmed_length;
    size_t source_index;
    size_t digit_index = 0U;
    size_t leading_zeros = 0U;
    _Bool found_nonzero = 0;

    if ((formatted <= 0) || ((size_t)formatted >= sizeof(output))) {
        return 0;
    }
    output_length = (size_t)formatted;
    for (source_index = 0U; source_index < output_length; source_index += 1U) {
        if (output[source_index] == '.') {
            decimal_point = source_index;
            break;
        }
    }
    if (decimal_point == SIZE_MAX) {
        return 0;
    }
    trimmed_length = output_length;
    while ((trimmed_length > (decimal_point + 1U)) && (output[trimmed_length - 1U] == '0')) {
        trimmed_length -= 1U;
    }
    if (trimmed_length == (decimal_point + 1U)) {
        trimmed_length = decimal_point;
    }
    for (source_index = 0U; source_index < trimmed_length; source_index += 1U) {
        uint8_t byte;

        if (source_index == decimal_point) {
            continue;
        }
        byte = (uint8_t)output[source_index];
        if (!found_nonzero && (byte == UINT8_C('0'))) {
            leading_zeros += 1U;
            continue;
        }
        found_nonzero = 1;
        if (digit_index >= sizeof(exact->digits)) {
            return 0;
        }
        exact->digits[digit_index] = byte;
        digit_index += 1U;
    }
    if (!found_nonzero || (leading_zeros > (size_t)INT64_MAX) ||
        (decimal_point > (size_t)INT64_MAX)) {
        return 0;
    }
    exact->length = digit_index;
    exact->scientific_exponent = (int64_t)decimal_point - (int64_t)leading_zeros - INT64_C(1);
    return 1;
}

static size_t r_library_float_append_exponent(uint8_t *output, size_t index, int64_t exponent) {
    uint8_t reversed[32];
    size_t count = 0U;
    uint64_t magnitude;

    if (exponent < INT64_C(0)) {
        output[index] = UINT8_C('-');
        index += 1U;
        magnitude = (uint64_t)(-exponent);
    } else {
        magnitude = (uint64_t)exponent;
    }
    do {
        reversed[count] = (uint8_t)(UINT8_C('0') + (uint8_t)(magnitude % UINT64_C(10)));
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

static size_t r_library_float_render_scientific(const RLibraryFloatCoefficient *coefficient,
                                                _Bool negative,
                                                uint8_t *output,
                                                size_t capacity,
                                                _Bool *even_last_digit) {
    int64_t exponent = coefficient->decimal_power + (int64_t)coefficient->length - INT64_C(1);
    size_t required = (negative ? 1U : 0U) + 1U +
                      (coefficient->length > 1U ? coefficient->length : 0U) + 2U + 20U;
    size_t index = 0U;
    size_t digit_index;

    if (required > capacity) {
        return 0U;
    }
    if (negative) {
        output[index] = UINT8_C('-');
        index += 1U;
    }
    output[index] = coefficient->digits[0];
    index += 1U;
    if (coefficient->length > 1U) {
        output[index] = UINT8_C('.');
        index += 1U;
        for (digit_index = 1U; digit_index < coefficient->length; digit_index += 1U) {
            output[index] = coefficient->digits[digit_index];
            index += 1U;
        }
    }
    output[index] = UINT8_C('e');
    index += 1U;
    index = r_library_float_append_exponent(output, index, exponent);
    *even_last_digit =
        ((coefficient->digits[coefficient->length - 1U] - UINT8_C('0')) % UINT8_C(2)) == UINT8_C(0);
    return index;
}

static size_t r_library_float_render_fixed(const RLibraryFloatCoefficient *coefficient,
                                           _Bool negative,
                                           uint8_t *output,
                                           size_t capacity,
                                           _Bool *even_last_digit) {
    int64_t decimal_position = (int64_t)coefficient->length + coefficient->decimal_power;
    size_t sign_length = negative ? 1U : 0U;
    size_t required;
    size_t index = 0U;
    size_t digit_index;

    if (decimal_position <= INT64_C(0)) {
        uint64_t leading_zeros = (uint64_t)(-decimal_position);

        if (leading_zeros > (uint64_t)(SIZE_MAX - sign_length - 2U - coefficient->length)) {
            return 0U;
        }
        required = sign_length + 2U + (size_t)leading_zeros + coefficient->length;
    } else if ((uint64_t)decimal_position >= (uint64_t)coefficient->length) {
        if ((uint64_t)decimal_position > (uint64_t)(SIZE_MAX - sign_length)) {
            return 0U;
        }
        required = sign_length + (size_t)decimal_position;
    } else {
        required = sign_length + coefficient->length + 1U;
    }
    if (required > capacity) {
        return 0U;
    }
    if (negative) {
        output[index] = UINT8_C('-');
        index += 1U;
    }
    if (decimal_position <= INT64_C(0)) {
        size_t leading_zeros = (size_t)(-decimal_position);

        output[index] = UINT8_C('0');
        output[index + 1U] = UINT8_C('.');
        index += 2U;
        for (digit_index = 0U; digit_index < leading_zeros; digit_index += 1U) {
            output[index] = UINT8_C('0');
            index += 1U;
        }
        (void)memcpy(output + index, coefficient->digits, coefficient->length);
        index += coefficient->length;
    } else if ((size_t)decimal_position >= coefficient->length) {
        size_t trailing_zeros = (size_t)decimal_position - coefficient->length;

        (void)memcpy(output + index, coefficient->digits, coefficient->length);
        index += coefficient->length;
        for (digit_index = 0U; digit_index < trailing_zeros; digit_index += 1U) {
            output[index] = UINT8_C('0');
            index += 1U;
        }
    } else {
        size_t integer_digits = (size_t)decimal_position;

        (void)memcpy(output + index, coefficient->digits, integer_digits);
        index += integer_digits;
        output[index] = UINT8_C('.');
        index += 1U;
        (void)memcpy(output + index,
                     coefficient->digits + integer_digits,
                     coefficient->length - integer_digits);
        index += coefficient->length - integer_digits;
    }
    *even_last_digit =
        ((coefficient->digits[coefficient->length - 1U] - UINT8_C('0')) % UINT8_C(2)) == UINT8_C(0);
    return index;
}

static _Bool r_library_float_round_trips(const uint8_t *text,
                                         size_t length,
                                         RLibraryFloatFormatValue value,
                                         RLibraryFloatFormatDestination destination) {
    char terminated[R_LIBRARY_FLOAT_RENDER_CAPACITY];
    char *end = NULL;

    if (length >= sizeof(terminated)) {
        return 0;
    }
    (void)memcpy(terminated, text, length);
    terminated[length] = '\0';
    if (destination == R_LIBRARY_FLOAT_FORMAT_F32) {
        float parsed = strtof_l(terminated, &end, LC_C_LOCALE);

        return (end == (terminated + length)) && (parsed == value.f32);
    }
    if (destination == R_LIBRARY_FLOAT_FORMAT_F64) {
        double parsed = strtod_l(terminated, &end, LC_C_LOCALE);

        return (end == (terminated + length)) && (parsed == value.f64);
    }
    if (destination == R_LIBRARY_FLOAT_FORMAT_C_FLOAT) {
        float parsed = strtof_l(terminated, &end, LC_C_LOCALE);

        return (end == (terminated + length)) && (parsed == value.c_float);
    }
    if (destination == R_LIBRARY_FLOAT_FORMAT_C_DOUBLE) {
        double parsed = strtod_l(terminated, &end, LC_C_LOCALE);

        return (end == (terminated + length)) && (parsed == value.c_double);
    }
    {
        long double parsed = strtold_l(terminated, &end, LC_C_LOCALE);

        return (end == (terminated + length)) && (parsed == value.c_long_double);
    }
}

static int r_library_float_compare_distance(const uint8_t *left,
                                            size_t left_length,
                                            const uint8_t *right,
                                            size_t right_length) {
    int comparison;

    if (left_length != right_length) {
        return left_length < right_length ? -1 : 1;
    }
    comparison = memcmp(left, right, left_length);
    if (comparison < 0) {
        return -1;
    }
    if (comparison > 0) {
        return 1;
    }
    return 0;
}

static _Bool r_library_float_candidate_better(const RLibraryFloatBest *best,
                                              const uint8_t *text,
                                              size_t length,
                                              const uint8_t *distance,
                                              size_t distance_length,
                                              _Bool even_last_digit,
                                              _Bool fixed_notation) {
    int distance_order;
    int lexical_order;

    if (!best->present) {
        return 1;
    }
    if (length != best->length) {
        return length < best->length;
    }
    distance_order = r_library_float_compare_distance(
        distance, distance_length, best->distance, best->distance_length);
    if (distance_order != 0) {
        return distance_order < 0;
    }
    if (even_last_digit != best->even_last_digit) {
        return even_last_digit;
    }
    if (fixed_notation != best->fixed_notation) {
        return fixed_notation;
    }
    lexical_order = memcmp(text, best->bytes, length);
    return lexical_order < 0;
}

static void r_library_float_consider(RLibraryFloatBest *best,
                                     const uint8_t *text,
                                     size_t length,
                                     const uint8_t *distance,
                                     size_t distance_length,
                                     _Bool even_last_digit,
                                     _Bool fixed_notation,
                                     RLibraryFloatFormatValue value,
                                     RLibraryFloatFormatDestination destination) {
    if ((length == 0U) || (length > sizeof(best->bytes)) ||
        !r_library_float_round_trips(text, length, value, destination) ||
        !r_library_float_candidate_better(
            best, text, length, distance, distance_length, even_last_digit, fixed_notation)) {
        return;
    }
    (void)memcpy(best->bytes, text, length);
    best->length = length;
    (void)memcpy(best->distance, distance, distance_length);
    best->distance_length = distance_length;
    best->even_last_digit = even_last_digit;
    best->fixed_notation = fixed_notation;
    best->present = 1;
}

static void r_library_float_normalize_coefficient(RLibraryFloatCoefficient *coefficient) {
    while ((coefficient->length > 1U) &&
           (coefficient->digits[coefficient->length - 1U] == UINT8_C('0'))) {
        coefficient->length -= 1U;
        coefficient->decimal_power += INT64_C(1);
    }
}

static void r_library_float_consider_coefficient(RLibraryFloatBest *best,
                                                 RLibraryFloatCoefficient coefficient,
                                                 const uint8_t *distance,
                                                 size_t distance_length,
                                                 _Bool negative,
                                                 RLibraryFloatFormatValue value,
                                                 RLibraryFloatFormatDestination destination) {
    uint8_t rendered[R_LIBRARY_FLOAT_RENDER_CAPACITY];
    size_t rendered_length;
    _Bool even_last_digit = 0;

    r_library_float_normalize_coefficient(&coefficient);
    rendered_length = r_library_float_render_scientific(
        &coefficient, negative, rendered, sizeof(rendered), &even_last_digit);
    r_library_float_consider(best,
                             rendered,
                             rendered_length,
                             distance,
                             distance_length,
                             even_last_digit,
                             0,
                             value,
                             destination);
    rendered_length = r_library_float_render_fixed(
        &coefficient, negative, rendered, sizeof(rendered), &even_last_digit);
    r_library_float_consider(best,
                             rendered,
                             rendered_length,
                             distance,
                             distance_length,
                             even_last_digit,
                             1,
                             value,
                             destination);
}

static void r_library_float_normalize_distance(const uint8_t **distance, size_t *distance_length) {
    while ((*distance_length > 1U) && (**distance == UINT8_C('0'))) {
        *distance += 1U;
        *distance_length -= 1U;
    }
}

static _Bool r_library_float_suffix_nonzero(const RLibraryFloatExact *exact, size_t start) {
    size_t index;

    for (index = start; index < exact->length; index += 1U) {
        if (exact->digits[index] != UINT8_C('0')) {
            return 1;
        }
    }
    return 0;
}

static size_t
r_library_float_upper_distance(const RLibraryFloatExact *exact, size_t start, uint8_t *distance) {
    size_t length = exact->length - start;
    size_t index = length;
    uint8_t carry = UINT8_C(1);

    while (index != 0U) {
        uint8_t complement;
        uint8_t sum;

        index -= 1U;
        complement = UINT8_C('9') - exact->digits[start + index];
        sum = complement + carry;
        if (sum >= UINT8_C(10)) {
            distance[index] = UINT8_C('0');
            carry = UINT8_C(1);
        } else {
            distance[index] = UINT8_C('0') + sum;
            carry = UINT8_C(0);
        }
    }
    return length;
}

static void r_library_float_increment_coefficient(RLibraryFloatCoefficient *coefficient) {
    size_t index = coefficient->length;

    while (index != 0U) {
        index -= 1U;
        if (coefficient->digits[index] != UINT8_C('9')) {
            coefficient->digits[index] += UINT8_C(1);
            return;
        }
        coefficient->digits[index] = UINT8_C('0');
    }
    (void)memmove(coefficient->digits + 1U, coefficient->digits, coefficient->length);
    coefficient->digits[0] = UINT8_C('1');
    coefficient->length += 1U;
}

static _Bool r_library_float_find_best(const RLibraryFloatExact *exact,
                                       _Bool negative,
                                       RLibraryFloatFormatValue value,
                                       RLibraryFloatFormatDestination destination,
                                       RLibraryFloatBest *best) {
    uint8_t upper_distance[R_LIBRARY_FLOAT_EXACT_CAPACITY];
    /*
     * A binary32/binary64 value has a round-trip scientific spelling of at most 24 bytes,
     * including sign and exponent. More than 24 nonzero-ending coefficient digits cannot produce
     * a fixed or scientific spelling of equal or smaller length. Keeping the full 24-digit lane
     * is nevertheless required: equal-length fixed integer candidates may improve exact decimal
     * distance beyond max_digits10.
     */
    size_t limit = exact->length < R_LIBRARY_FLOAT_CANDIDATE_DIGITS
                       ? exact->length
                       : R_LIBRARY_FLOAT_CANDIDATE_DIGITS;
    size_t significant_digits;

    for (significant_digits = 1U; significant_digits <= limit; significant_digits += 1U) {
        RLibraryFloatCoefficient lower = {0};
        const uint8_t zero_distance[] = "0";
        const uint8_t *lower_distance;
        size_t lower_distance_length;
        _Bool has_remainder = r_library_float_suffix_nonzero(exact, significant_digits);

        (void)memcpy(lower.digits, exact->digits, significant_digits);
        lower.length = significant_digits;
        lower.decimal_power = exact->scientific_exponent - (int64_t)significant_digits + INT64_C(1);
        if (significant_digits == exact->length) {
            lower_distance = zero_distance;
            lower_distance_length = 1U;
        } else {
            lower_distance = exact->digits + significant_digits;
            lower_distance_length = exact->length - significant_digits;
            r_library_float_normalize_distance(&lower_distance, &lower_distance_length);
        }
        r_library_float_consider_coefficient(
            best, lower, lower_distance, lower_distance_length, negative, value, destination);
        if (has_remainder) {
            RLibraryFloatCoefficient upper = lower;
            const uint8_t *normalized_upper_distance = upper_distance;
            size_t upper_distance_length =
                r_library_float_upper_distance(exact, significant_digits, upper_distance);

            r_library_float_increment_coefficient(&upper);
            r_library_float_normalize_distance(&normalized_upper_distance, &upper_distance_length);
            r_library_float_consider_coefficient(best,
                                                 upper,
                                                 normalized_upper_distance,
                                                 upper_distance_length,
                                                 negative,
                                                 value,
                                                 destination);
        }
    }
    return best->present;
}

static RStdFormatAppendResult
r_library_float_append_literal(RStdFormatBuilder *target, const uint8_t *bytes, size_t length) {
    RStdFormatAppendResult result = {0};
    RRuntimeStringStatus runtime_status = r_runtime_string_append(&target->output, bytes, length);
    RStdAllocError allocation_error = {0};
    RStdStringCallStatus status =
        r_library_internal_string_map_allocation_status(runtime_status, &allocation_error);

    if (status == R_STD_STRING_CALL_SUCCESS) {
        result.status = R_STD_FORMAT_CALL_SUCCESS;
    } else {
        result.status = R_STD_FORMAT_CALL_ERROR;
        result.error.kind = R_STD_FORMAT_ERROR_ALLOCATION_FAILED;
        result.error.allocation_error = allocation_error;
    }
    return result;
}

RStdFormatAppendResult r_library_internal_float_append(RStdFormatBuilder *target,
                                                       RLibraryFloatFormatValue value,
                                                       RLibraryFloatFormatDestination destination) {
    static const uint8_t positive_zero[] = "0";
    static const uint8_t negative_zero[] = "-0";
    static const uint8_t positive_infinity[] = "inf";
    static const uint8_t negative_infinity[] = "-inf";
    static const uint8_t not_a_number_literal[] = "nan";
    RStdFormatAppendResult result = {0};
    RLibraryFloatEnvironment environment = {0};
    RLibraryFloatExact exact = {0};
    RLibraryFloatBest best = {0};
    _Bool negative = 0;
    _Bool not_a_number = 0;
    _Bool infinite = 0;
    _Bool zero = 0;
    _Bool extracted;
    _Bool found;
    _Bool restored;
    const uint8_t *literal = NULL;
    size_t literal_length = 0U;

    /* Classification itself may raise FE_INVALID for a signaling NaN. Mask first. */
    if (!r_library_float_environment_enter(&environment)) {
        result.status = R_STD_FORMAT_CALL_CONTRACT_VIOLATION;
        return result;
    }
    if (!r_library_float_properties(
            value, destination, &negative, &not_a_number, &infinite, &zero)) {
        (void)r_library_float_environment_leave(&environment);
        result.status = R_STD_FORMAT_CALL_CONTRACT_VIOLATION;
        return result;
    }
    if (not_a_number) {
        literal = not_a_number_literal;
        literal_length = sizeof(not_a_number_literal) - 1U;
    } else if (infinite) {
        literal = negative ? negative_infinity : positive_infinity;
        literal_length = negative ? sizeof(negative_infinity) - 1U : sizeof(positive_infinity) - 1U;
    } else if (zero) {
        literal = negative ? negative_zero : positive_zero;
        literal_length = negative ? sizeof(negative_zero) - 1U : sizeof(positive_zero) - 1U;
    }
    if (literal != NULL) {
        if (!r_library_float_environment_leave(&environment)) {
            result.status = R_STD_FORMAT_CALL_CONTRACT_VIOLATION;
            return result;
        }
        return r_library_float_append_literal(target, literal, literal_length);
    }
    extracted = r_library_float_extract_exact(value, destination, negative, &exact);
    found = extracted && r_library_float_find_best(&exact, negative, value, destination, &best);
    restored = r_library_float_environment_leave(&environment);
    if (!extracted || !found || !restored) {
        result.status = R_STD_FORMAT_CALL_CONTRACT_VIOLATION;
        return result;
    }
    return r_library_float_append_literal(target, best.bytes, best.length);
}

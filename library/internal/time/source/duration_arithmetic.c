#include "r_library_duration_internal.h"

#include <limits.h>
#include <stdint.h>

typedef struct RLibraryDurationMagnitude {
    _Bool negative;
    uint64_t whole;
    uint32_t fraction;
} RLibraryDurationMagnitude;

static RStdTimeDurationResult invalid_nanoseconds(void) {
    RStdTimeDurationResult result = {0};

    result.error = R_STD_TIME_DURATION_ERROR_INVALID_NANOSECONDS;
    return result;
}

static RStdTimeDurationResult overflow(void) {
    RStdTimeDurationResult result = {0};

    result.error = R_STD_TIME_DURATION_ERROR_OVERFLOW;
    return result;
}

static RStdTimeDurationResult success(RStdTimeDuration value) {
    RStdTimeDurationResult result = {0};

    result.is_ok = 1;
    result.value = value;
    return result;
}

static _Bool magnitude_from_duration(RStdTimeDuration value, RLibraryDurationMagnitude *result) {
    if (value.nanoseconds >= R_STD_TIME_NANOSECONDS_PER_SECOND) {
        return 0;
    }
    result->negative = value.seconds < 0;
    if (!result->negative) {
        result->whole = (uint64_t)value.seconds;
        result->fraction = value.nanoseconds;
        return 1;
    }
    if (value.nanoseconds == 0U) {
        result->whole = (uint64_t)(-(value.seconds + INT64_C(1))) + UINT64_C(1);
        result->fraction = 0U;
    } else {
        result->whole = (uint64_t)(-(value.seconds + INT64_C(1)));
        result->fraction = R_STD_TIME_NANOSECONDS_PER_SECOND - value.nanoseconds;
    }
    return 1;
}

static int magnitude_compare(RLibraryDurationMagnitude left, RLibraryDurationMagnitude right) {
    if (left.whole < right.whole) {
        return -1;
    }
    if (left.whole > right.whole) {
        return 1;
    }
    if (left.fraction < right.fraction) {
        return -1;
    }
    if (left.fraction > right.fraction) {
        return 1;
    }
    return 0;
}

static _Bool magnitude_in_range(RLibraryDurationMagnitude value) {
    const uint64_t negative_limit = UINT64_C(1) << 63U;
    const uint64_t positive_limit = (uint64_t)INT64_MAX;
    const uint64_t limit = value.negative ? negative_limit : positive_limit;

    return (value.whole < limit) ||
           ((value.whole == limit) && (!value.negative || (value.fraction == 0U)));
}

static RStdTimeDuration duration_from_magnitude(RLibraryDurationMagnitude value) {
    RStdTimeDuration result;

    if ((value.whole == 0U) && (value.fraction == 0U)) {
        result.seconds = 0;
        result.nanoseconds = 0U;
        return result;
    }
    if (!value.negative) {
        result.seconds = (int64_t)value.whole;
        result.nanoseconds = value.fraction;
        return result;
    }
    if (value.fraction == 0U) {
        if (value.whole == (UINT64_C(1) << 63U)) {
            result.seconds = INT64_MIN;
        } else {
            result.seconds = -(int64_t)value.whole;
        }
        result.nanoseconds = 0U;
        return result;
    }
    if (value.whole == (uint64_t)INT64_MAX) {
        result.seconds = INT64_MIN;
    } else {
        result.seconds = -(int64_t)(value.whole + UINT64_C(1));
    }
    result.nanoseconds = R_STD_TIME_NANOSECONDS_PER_SECOND - value.fraction;
    return result;
}

static _Bool magnitude_add(RLibraryDurationMagnitude left,
                           RLibraryDurationMagnitude right,
                           RLibraryDurationMagnitude *result) {
    uint64_t whole;
    uint32_t fraction = left.fraction + right.fraction;
    uint64_t carry = 0U;

    if (fraction >= R_STD_TIME_NANOSECONDS_PER_SECOND) {
        fraction -= R_STD_TIME_NANOSECONDS_PER_SECOND;
        carry = 1U;
    }
    if (left.whole > (UINT64_MAX - right.whole)) {
        return 0;
    }
    whole = left.whole + right.whole;
    if (whole > (UINT64_MAX - carry)) {
        return 0;
    }
    result->negative = left.negative;
    result->whole = whole + carry;
    result->fraction = fraction;
    return magnitude_in_range(*result);
}

static RLibraryDurationMagnitude magnitude_subtract(RLibraryDurationMagnitude larger,
                                                    RLibraryDurationMagnitude smaller) {
    RLibraryDurationMagnitude result;

    result.negative = larger.negative;
    if (larger.fraction < smaller.fraction) {
        result.whole = larger.whole - smaller.whole - UINT64_C(1);
        result.fraction = larger.fraction + R_STD_TIME_NANOSECONDS_PER_SECOND - smaller.fraction;
    } else {
        result.whole = larger.whole - smaller.whole;
        result.fraction = larger.fraction - smaller.fraction;
    }
    if ((result.whole == 0U) && (result.fraction == 0U)) {
        result.negative = 0;
    }
    return result;
}

RStdTimeDurationResult
r_library_internal_duration_add(RStdTimeDuration left, RStdTimeDuration right, _Bool subtract) {
    RLibraryDurationMagnitude left_magnitude;
    RLibraryDurationMagnitude right_magnitude;
    RLibraryDurationMagnitude result_magnitude;
    int comparison;

    if (!magnitude_from_duration(left, &left_magnitude) ||
        !magnitude_from_duration(right, &right_magnitude)) {
        return invalid_nanoseconds();
    }
    if (subtract && ((right_magnitude.whole != 0U) || (right_magnitude.fraction != 0U))) {
        right_magnitude.negative = !right_magnitude.negative;
    }
    if (left_magnitude.negative == right_magnitude.negative) {
        if (!magnitude_add(left_magnitude, right_magnitude, &result_magnitude)) {
            return overflow();
        }
    } else {
        comparison = magnitude_compare(left_magnitude, right_magnitude);
        if (comparison >= 0) {
            result_magnitude = magnitude_subtract(left_magnitude, right_magnitude);
        } else {
            result_magnitude = magnitude_subtract(right_magnitude, left_magnitude);
        }
    }
    return success(duration_from_magnitude(result_magnitude));
}

RStdTimeDurationResult r_library_internal_duration_multiply(RStdTimeDuration value,
                                                            int64_t factor) {
    RLibraryDurationMagnitude magnitude;
    RLibraryDurationMagnitude result_magnitude;
    uint64_t factor_magnitude;
    uint64_t quotient;
    uint64_t remainder;
    uint64_t fraction_product;
    uint64_t fraction_whole;
    uint64_t whole_product;

    if (!magnitude_from_duration(value, &magnitude)) {
        return invalid_nanoseconds();
    }
    if ((factor == 0) || ((magnitude.whole == 0U) && (magnitude.fraction == 0U))) {
        return success((RStdTimeDuration){0, 0U});
    }
    factor_magnitude =
        factor < 0 ? (uint64_t)(-(factor + INT64_C(1))) + UINT64_C(1) : (uint64_t)factor;
    result_magnitude.negative = magnitude.negative != (factor < 0);
    quotient = factor_magnitude / (uint64_t)R_STD_TIME_NANOSECONDS_PER_SECOND;
    remainder = factor_magnitude % (uint64_t)R_STD_TIME_NANOSECONDS_PER_SECOND;
    fraction_product = (uint64_t)magnitude.fraction * remainder;
    fraction_whole = (uint64_t)magnitude.fraction * quotient +
                     fraction_product / (uint64_t)R_STD_TIME_NANOSECONDS_PER_SECOND;
    result_magnitude.fraction =
        (uint32_t)(fraction_product % (uint64_t)R_STD_TIME_NANOSECONDS_PER_SECOND);
    if ((magnitude.whole != 0U) && (factor_magnitude > (UINT64_MAX / magnitude.whole))) {
        return overflow();
    }
    whole_product = magnitude.whole * factor_magnitude;
    if (whole_product > (UINT64_MAX - fraction_whole)) {
        return overflow();
    }
    result_magnitude.whole = whole_product + fraction_whole;
    if (!magnitude_in_range(result_magnitude)) {
        return overflow();
    }
    return success(duration_from_magnitude(result_magnitude));
}

RStdTimeInstantResult r_library_internal_time_instant_add(RStdTimeInstant base,
                                                          RStdTimeDuration delta) {
    RStdTimeInstantResult result = {0};
    RStdTimeDurationResult sum;

    if ((base.storage_seconds < 0) ||
        (base.storage_nanoseconds >= R_STD_TIME_NANOSECONDS_PER_SECOND)) {
        result.error = (RStdTimeError){R_STD_TIME_ERROR_INVALID_VALUE, INT64_C(0)};
        return result;
    }
    sum = r_library_internal_duration_add(
        (RStdTimeDuration){base.storage_seconds, base.storage_nanoseconds}, delta, 0);
    if (!sum.is_ok) {
        result.error.code = sum.error == R_STD_TIME_DURATION_ERROR_INVALID_NANOSECONDS
                                ? R_STD_TIME_ERROR_INVALID_VALUE
                                : R_STD_TIME_ERROR_OVERFLOW;
        return result;
    }
    if (sum.value.seconds < 0) {
        result.error.code = R_STD_TIME_ERROR_OVERFLOW;
        return result;
    }
    result.is_ok = 1;
    result.value = (RStdTimeInstant){sum.value.seconds, sum.value.nanoseconds};
    return result;
}

RStdTimeDurationTimeResult r_library_internal_time_instant_duration(RStdTimeInstant later,
                                                                    RStdTimeInstant earlier) {
    RStdTimeDurationTimeResult result = {0};
    RStdTimeDurationResult difference;

    if ((later.storage_seconds < 0) || (earlier.storage_seconds < 0) ||
        (later.storage_nanoseconds >= R_STD_TIME_NANOSECONDS_PER_SECOND) ||
        (earlier.storage_nanoseconds >= R_STD_TIME_NANOSECONDS_PER_SECOND)) {
        result.error = (RStdTimeError){R_STD_TIME_ERROR_INVALID_VALUE, INT64_C(0)};
        return result;
    }
    if ((later.storage_seconds < earlier.storage_seconds) ||
        ((later.storage_seconds == earlier.storage_seconds) &&
         (later.storage_nanoseconds < earlier.storage_nanoseconds))) {
        result.error = (RStdTimeError){R_STD_TIME_ERROR_INVALID_VALUE, INT64_C(0)};
        return result;
    }
    difference = r_library_internal_duration_add(
        (RStdTimeDuration){later.storage_seconds, later.storage_nanoseconds},
        (RStdTimeDuration){earlier.storage_seconds, earlier.storage_nanoseconds},
        1);
    if (!difference.is_ok) {
        result.error.code = difference.error == R_STD_TIME_DURATION_ERROR_INVALID_NANOSECONDS
                                ? R_STD_TIME_ERROR_INVALID_VALUE
                                : R_STD_TIME_ERROR_OVERFLOW;
        return result;
    }
    result.is_ok = 1;
    result.value = difference.value;
    return result;
}

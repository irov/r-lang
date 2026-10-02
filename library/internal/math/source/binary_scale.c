#include "r_library_math_binary_scale.h"

#include <float.h>
#include <stdint.h>
#include <string.h>

_Static_assert(FLT_RADIX == 2, "R Darwin math requires binary floating point");
_Static_assert(sizeof(float) == sizeof(uint32_t), "R f32 requires binary32 storage");
_Static_assert(FLT_MANT_DIG == 24, "R f32 requires binary32 precision");
_Static_assert(FLT_MAX_EXP == 128, "R f32 requires binary32 exponent range");
_Static_assert(sizeof(double) == sizeof(uint64_t), "R f64 requires binary64 storage");
_Static_assert(DBL_MANT_DIG == 53, "R f64 requires binary64 precision");
_Static_assert(DBL_MAX_EXP == 1024, "R f64 requires binary64 exponent range");
_Static_assert(sizeof(long double) == sizeof(uint64_t),
               "arm64 Darwin c_long_double requires binary64 storage");
_Static_assert(LDBL_MANT_DIG == 53, "arm64 Darwin c_long_double requires binary64 precision");
_Static_assert(LDBL_MAX_EXP == 1024, "arm64 Darwin c_long_double requires binary64 exponent range");

static uint32_t r_library_internal_math_trailing_zero_count_u32(uint32_t value) {
    uint32_t count = UINT32_C(0);

    while ((value & UINT32_C(1)) == UINT32_C(0)) {
        value >>= UINT32_C(1);
        count += UINT32_C(1);
    }
    return count;
}

static uint32_t r_library_internal_math_trailing_zero_count_u64(uint64_t value) {
    uint32_t count = UINT32_C(0);

    while ((value & UINT64_C(1)) == UINT64_C(0)) {
        value >>= UINT32_C(1);
        count += UINT32_C(1);
    }
    return count;
}

static _Bool r_library_internal_math_binary32_scale_underflows(uint32_t bits,
                                                               int32_t exponent,
                                                               uint32_t result_bits) {
    uint32_t encoded_exponent = (bits >> UINT32_C(23)) & UINT32_C(0xff);
    uint32_t significand = bits & UINT32_C(0x007fffff);
    uint32_t result_magnitude = result_bits & UINT32_C(0x7fffffff);
    int64_t unit_exponent;

    if (encoded_exponent == UINT32_C(0xff)) {
        return 0;
    }
    if (encoded_exponent == UINT32_C(0)) {
        if (significand == UINT32_C(0)) {
            return 0;
        }
        unit_exponent = INT64_C(-149);
    } else {
        significand |= UINT32_C(0x00800000);
        unit_exponent = (int64_t)encoded_exponent - INT64_C(127) - INT64_C(23);
    }
    if (result_magnitude == UINT32_C(0)) {
        return 1;
    }
    if ((result_magnitude & UINT32_C(0x7f800000)) != UINT32_C(0)) {
        return 0;
    }
    unit_exponent += (int64_t)r_library_internal_math_trailing_zero_count_u32(significand);
    return unit_exponent + (int64_t)exponent < INT64_C(-149);
}

static _Bool r_library_internal_math_binary64_scale_underflows(uint64_t bits,
                                                               int32_t exponent,
                                                               uint64_t result_bits) {
    uint64_t encoded_exponent = (bits >> UINT32_C(52)) & UINT64_C(0x7ff);
    uint64_t significand = bits & UINT64_C(0x000fffffffffffff);
    uint64_t result_magnitude = result_bits & UINT64_C(0x7fffffffffffffff);
    int64_t unit_exponent;

    if (encoded_exponent == UINT64_C(0x7ff)) {
        return 0;
    }
    if (encoded_exponent == UINT64_C(0)) {
        if (significand == UINT64_C(0)) {
            return 0;
        }
        unit_exponent = INT64_C(-1074);
    } else {
        significand |= UINT64_C(0x0010000000000000);
        unit_exponent = (int64_t)encoded_exponent - INT64_C(1023) - INT64_C(52);
    }
    if (result_magnitude == UINT64_C(0)) {
        return 1;
    }
    if ((result_magnitude & UINT64_C(0x7ff0000000000000)) != UINT64_C(0)) {
        return 0;
    }
    unit_exponent += (int64_t)r_library_internal_math_trailing_zero_count_u64(significand);
    return unit_exponent + (int64_t)exponent < INT64_C(-1074);
}

_Bool r_library_internal_math_f32_scale_underflows(float value, int32_t exponent, float result) {
    uint32_t bits;
    uint32_t result_bits;

    (void)memcpy(&bits, &value, sizeof(bits));
    (void)memcpy(&result_bits, &result, sizeof(result_bits));
    return r_library_internal_math_binary32_scale_underflows(bits, exponent, result_bits);
}

_Bool r_library_internal_math_f64_scale_underflows(double value, int32_t exponent, double result) {
    uint64_t bits;
    uint64_t result_bits;

    (void)memcpy(&bits, &value, sizeof(bits));
    (void)memcpy(&result_bits, &result, sizeof(result_bits));
    return r_library_internal_math_binary64_scale_underflows(bits, exponent, result_bits);
}

_Bool r_library_internal_math_c_long_double_scale_underflows(long double value,
                                                             int32_t exponent,
                                                             long double result) {
    uint64_t bits;
    uint64_t result_bits;

    (void)memcpy(&bits, &value, sizeof(bits));
    (void)memcpy(&result_bits, &result, sizeof(result_bits));
    return r_library_internal_math_binary64_scale_underflows(bits, exponent, result_bits);
}

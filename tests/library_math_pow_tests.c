#include "r_std_math.h"

#include "r_runtime_allocator.h"

#include <errno.h>
#include <fenv.h>
#include <float.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#pragma STDC FENV_ACCESS ON

#define R_TEST_CHECK(expression)                                                                   \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            (void)fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, #expression); \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

#define R_TEST_CHECK_ERROR(result, expected_code, expected_native_code)                            \
    do {                                                                                           \
        R_TEST_CHECK((result).status == R_STD_MATH_CALL_ERROR);                                    \
        R_TEST_CHECK((result).error.code == (expected_code));                                      \
        R_TEST_CHECK((result).error.native_code == (int64_t)(expected_native_code));               \
    } while (0)

_Static_assert(LDBL_MANT_DIG == 53, "target long double uses a binary64 significand");
_Static_assert(LDBL_MAX_EXP == 1024, "target long double uses a binary64 exponent");
_Static_assert(sizeof(long double) == sizeof(uint64_t),
               "arm64-apple-darwin long double must use binary64 storage");

typedef struct RTestPowThreadContext {
    int rounding;
    int exceptions;
    int native_errno;
    int64_t domain_native_code;
    int64_t pole_native_code;
    int64_t underflow_native_code;
    int64_t overflow_native_code;
    int failed;
} RTestPowThreadContext;

static float r_test_signaling_nan_f32(void) {
    uint32_t bits = UINT32_C(0x7f800001);
    float value;

    (void)memcpy(&value, &bits, sizeof(value));
    return value;
}

static double r_test_signaling_nan_f64(void) {
    uint64_t bits = UINT64_C(0x7ff0000000000001);
    double value;

    (void)memcpy(&value, &bits, sizeof(value));
    return value;
}

static long double r_test_signaling_nan_c_long_double(void) {
    uint64_t bits = UINT64_C(0x7ff0000000000001);
    long double value;

    (void)memcpy(&value, &bits, sizeof(value));
    return value;
}

static uint32_t r_test_bits_f32(float value) {
    uint32_t bits;

    (void)memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static uint64_t r_test_bits_f64(double value) {
    uint64_t bits;

    (void)memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static uint64_t r_test_bits_c_long_double(long double value) {
    uint64_t bits;

    (void)memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static _Bool r_test_same_f32(float left, float right) {
    return r_test_bits_f32(left) == r_test_bits_f32(right);
}

static _Bool r_test_same_f64(double left, double right) {
    return r_test_bits_f64(left) == r_test_bits_f64(right);
}

static _Bool r_test_same_c_long_double(long double left, long double right) {
    return r_test_bits_c_long_double(left) == r_test_bits_c_long_double(right);
}

static _Bool r_test_is_zero_f32(float value) {
    return (r_test_bits_f32(value) & UINT32_C(0x7fffffff)) == UINT32_C(0);
}

static _Bool r_test_is_zero_f64(double value) {
    return (r_test_bits_f64(value) & UINT64_C(0x7fffffffffffffff)) == UINT64_C(0);
}

static _Bool r_test_is_zero_c_long_double(long double value) {
    return (r_test_bits_c_long_double(value) & UINT64_C(0x7fffffffffffffff)) == UINT64_C(0);
}

static _Bool r_test_is_subnormal_f32(float value) {
    uint32_t magnitude = r_test_bits_f32(value) & UINT32_C(0x7fffffff);

    return magnitude != UINT32_C(0) && magnitude < UINT32_C(0x00800000);
}

static _Bool r_test_is_subnormal_f64(double value) {
    uint64_t magnitude = r_test_bits_f64(value) & UINT64_C(0x7fffffffffffffff);

    return magnitude != UINT64_C(0) && magnitude < UINT64_C(0x0010000000000000);
}

static _Bool r_test_is_subnormal_c_long_double(long double value) {
    uint64_t magnitude = r_test_bits_c_long_double(value) & UINT64_C(0x7fffffffffffffff);

    return magnitude != UINT64_C(0) && magnitude < UINT64_C(0x0010000000000000);
}

static _Bool r_test_is_infinite_f32(float value) {
    return (r_test_bits_f32(value) & UINT32_C(0x7fffffff)) == UINT32_C(0x7f800000);
}

static _Bool r_test_is_infinite_f64(double value) {
    return (r_test_bits_f64(value) & UINT64_C(0x7fffffffffffffff)) == UINT64_C(0x7ff0000000000000);
}

static _Bool r_test_is_infinite_c_long_double(long double value) {
    return (r_test_bits_c_long_double(value) & UINT64_C(0x7fffffffffffffff)) ==
           UINT64_C(0x7ff0000000000000);
}

static _Bool r_test_is_nan_f32(float value) {
    uint32_t bits = r_test_bits_f32(value);

    return (bits & UINT32_C(0x7f800000)) == UINT32_C(0x7f800000) &&
           (bits & UINT32_C(0x007fffff)) != UINT32_C(0);
}

static _Bool r_test_is_nan_f64(double value) {
    uint64_t bits = r_test_bits_f64(value);

    return (bits & UINT64_C(0x7ff0000000000000)) == UINT64_C(0x7ff0000000000000) &&
           (bits & UINT64_C(0x000fffffffffffff)) != UINT64_C(0);
}

static _Bool r_test_is_nan_c_long_double(long double value) {
    uint64_t bits = r_test_bits_c_long_double(value);

    return (bits & UINT64_C(0x7ff0000000000000)) == UINT64_C(0x7ff0000000000000) &&
           (bits & UINT64_C(0x000fffffffffffff)) != UINT64_C(0);
}

static int r_test_seed_environment(int rounding, int exceptions, int native_errno) {
    R_TEST_CHECK(fesetround(rounding) == 0);
    R_TEST_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    R_TEST_CHECK(feraiseexcept(exceptions) == 0);
    errno = native_errno;
    return 0;
}

static _Bool r_test_environment_is(int rounding, int exceptions, int native_errno) {
    return fegetround() == rounding &&
           (fetestexcept(FE_ALL_EXCEPT) & FE_ALL_EXCEPT) == exceptions && errno == native_errno;
}

static int
r_test_native_f32(float base, float exponent, float *result, int *native_errno, int *exceptions) {
    fenv_t caller_environment;
    fenv_t held_environment;
    int caller_errno = errno;
    volatile float canonical_base;
    volatile float canonical_exponent;

    if (fegetenv(&caller_environment) != 0) {
        return 1;
    }
    if ((feholdexcept(&held_environment) != 0) || (fesetround(FE_TONEAREST) != 0)) {
        (void)fesetenv(&caller_environment);
        errno = caller_errno;
        return 1;
    }
    errno = 0;
    canonical_base = base;
    canonical_exponent = exponent;
    *result = powf(canonical_base, canonical_exponent);
    *native_errno = errno;
    *exceptions = fetestexcept(FE_ALL_EXCEPT);
    if (fesetenv(&caller_environment) != 0) {
        errno = caller_errno;
        return 1;
    }
    errno = caller_errno;
    return 0;
}

static int r_test_native_f64(
    double base, double exponent, double *result, int *native_errno, int *exceptions) {
    fenv_t caller_environment;
    fenv_t held_environment;
    int caller_errno = errno;
    volatile double canonical_base;
    volatile double canonical_exponent;

    if (fegetenv(&caller_environment) != 0) {
        return 1;
    }
    if ((feholdexcept(&held_environment) != 0) || (fesetround(FE_TONEAREST) != 0)) {
        (void)fesetenv(&caller_environment);
        errno = caller_errno;
        return 1;
    }
    errno = 0;
    canonical_base = base;
    canonical_exponent = exponent;
    *result = pow(canonical_base, canonical_exponent);
    *native_errno = errno;
    *exceptions = fetestexcept(FE_ALL_EXCEPT);
    if (fesetenv(&caller_environment) != 0) {
        errno = caller_errno;
        return 1;
    }
    errno = caller_errno;
    return 0;
}

static int r_test_native_c_long_double(long double base,
                                       long double exponent,
                                       long double *result,
                                       int *native_errno,
                                       int *exceptions) {
    fenv_t caller_environment;
    fenv_t held_environment;
    int caller_errno = errno;
    volatile long double canonical_base;
    volatile long double canonical_exponent;

    if (fegetenv(&caller_environment) != 0) {
        return 1;
    }
    if ((feholdexcept(&held_environment) != 0) || (fesetround(FE_TONEAREST) != 0)) {
        (void)fesetenv(&caller_environment);
        errno = caller_errno;
        return 1;
    }
    errno = 0;
    canonical_base = base;
    canonical_exponent = exponent;
    *result = powl(canonical_base, canonical_exponent);
    *native_errno = errno;
    *exceptions = fetestexcept(FE_ALL_EXCEPT);
    if (fesetenv(&caller_environment) != 0) {
        errno = caller_errno;
        return 1;
    }
    errno = caller_errno;
    return 0;
}

static int r_test_pair_f32(float base, float exponent) {
    float expected;
    int native_errno;
    int exceptions;
    RStdMathF32Result f32;
    RStdMathCFloatResult c_float;

    R_TEST_CHECK(r_test_native_f32(base, exponent, &expected, &native_errno, &exceptions) == 0);
    f32 = r_std_math_pow_f32(base, exponent);
    c_float = r_std_math_pow_c_float(base, exponent);
    R_TEST_CHECK(f32.status == R_STD_MATH_CALL_SUCCESS && r_test_same_f32(f32.value, expected));
    R_TEST_CHECK(c_float.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_same_f32(c_float.value, expected));
    return 0;
}

static int r_test_pair_f64(double base, double exponent) {
    double expected;
    int native_errno;
    int exceptions;
    RStdMathF64Result f64;
    RStdMathCDoubleResult c_double;

    R_TEST_CHECK(r_test_native_f64(base, exponent, &expected, &native_errno, &exceptions) == 0);
    f64 = r_std_math_pow_f64(base, exponent);
    c_double = r_std_math_pow_c_double(base, exponent);
    R_TEST_CHECK(f64.status == R_STD_MATH_CALL_SUCCESS && r_test_same_f64(f64.value, expected));
    R_TEST_CHECK(c_double.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_same_f64(c_double.value, expected));
    return 0;
}

static int r_test_pair_c_long_double(long double base, long double exponent) {
    long double expected;
    int native_errno;
    int exceptions;
    RStdMathCLongDoubleResult result;

    R_TEST_CHECK(
        r_test_native_c_long_double(base, exponent, &expected, &native_errno, &exceptions) == 0);
    result = r_std_math_pow_c_long_double(base, exponent);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_same_c_long_double(result.value, expected));
    return 0;
}

static int r_test_regular_and_signed_values(void) {
    static const float f32_pairs[][2] = {
        {2.0F, 10.0F},
        {-2.0F, 3.0F},
        {-2.0F, 4.0F},
        {4.0F, -0.5F},
        {0.0F, 3.0F},
        {-0.0F, 3.0F},
        {-0.0F, 2.0F},
        {2.0F, -126.0F},
    };
    static const double f64_pairs[][2] = {
        {2.0, 10.0},
        {-2.0, 3.0},
        {-2.0, 4.0},
        {4.0, -0.5},
        {0.0, 3.0},
        {-0.0, 3.0},
        {-0.0, 2.0},
        {2.0, -1022.0},
    };
    size_t index;

    for (index = 0U; index < sizeof(f32_pairs) / sizeof(f32_pairs[0]); index += 1U) {
        R_TEST_CHECK(r_test_pair_f32(f32_pairs[index][0], f32_pairs[index][1]) == 0);
    }
    for (index = 0U; index < sizeof(f64_pairs) / sizeof(f64_pairs[0]); index += 1U) {
        R_TEST_CHECK(r_test_pair_f64(f64_pairs[index][0], f64_pairs[index][1]) == 0);
        R_TEST_CHECK(r_test_pair_c_long_double((long double)f64_pairs[index][0],
                                               (long double)f64_pairs[index][1]) == 0);
    }
    return 0;
}

static int r_test_precedence_successes(void) {
    float signaling_f32 = r_test_signaling_nan_f32();
    double signaling_f64 = r_test_signaling_nan_f64();
    long double signaling_long = r_test_signaling_nan_c_long_double();
    RStdMathF32Result f32_nan_zero = r_std_math_pow_f32(signaling_f32, 0.0F);
    RStdMathCFloatResult c_float_zero_zero = r_std_math_pow_c_float(-0.0F, -0.0F);
    RStdMathF64Result f64_infinity_zero = r_std_math_pow_f64(-INFINITY, 0.0);
    RStdMathCDoubleResult c_double_one_nan = r_std_math_pow_c_double(1.0, signaling_f64);
    RStdMathCLongDoubleResult long_nan_zero = r_std_math_pow_c_long_double(signaling_long, -0.0L);
    RStdMathCLongDoubleResult long_one_nan = r_std_math_pow_c_long_double(1.0L, signaling_long);

    R_TEST_CHECK(f32_nan_zero.status == R_STD_MATH_CALL_SUCCESS && f32_nan_zero.value == 1.0F);
    R_TEST_CHECK(c_float_zero_zero.status == R_STD_MATH_CALL_SUCCESS &&
                 c_float_zero_zero.value == 1.0F);
    R_TEST_CHECK(f64_infinity_zero.status == R_STD_MATH_CALL_SUCCESS &&
                 f64_infinity_zero.value == 1.0);
    R_TEST_CHECK(c_double_one_nan.status == R_STD_MATH_CALL_SUCCESS &&
                 c_double_one_nan.value == 1.0);
    R_TEST_CHECK(long_nan_zero.status == R_STD_MATH_CALL_SUCCESS && long_nan_zero.value == 1.0L);
    R_TEST_CHECK(long_one_nan.status == R_STD_MATH_CALL_SUCCESS && long_one_nan.value == 1.0L);
    return 0;
}

static int r_test_domain_f32(float base, float exponent) {
    float native_result;
    int native_errno;
    int exceptions;
    RStdMathF32Result f32;
    RStdMathCFloatResult c_float;

    R_TEST_CHECK(r_test_native_f32(base, exponent, &native_result, &native_errno, &exceptions) ==
                 0);
    R_TEST_CHECK(r_test_is_nan_f32(native_result));
    f32 = r_std_math_pow_f32(base, exponent);
    c_float = r_std_math_pow_c_float(base, exponent);
    R_TEST_CHECK_ERROR(f32, R_STD_MATH_ERROR_DOMAIN, native_errno);
    R_TEST_CHECK_ERROR(c_float, R_STD_MATH_ERROR_DOMAIN, native_errno);
    return 0;
}

static int r_test_domain_f64(double base, double exponent) {
    double native_result;
    int native_errno;
    int exceptions;
    RStdMathF64Result f64;
    RStdMathCDoubleResult c_double;

    R_TEST_CHECK(r_test_native_f64(base, exponent, &native_result, &native_errno, &exceptions) ==
                 0);
    R_TEST_CHECK(r_test_is_nan_f64(native_result));
    f64 = r_std_math_pow_f64(base, exponent);
    c_double = r_std_math_pow_c_double(base, exponent);
    R_TEST_CHECK_ERROR(f64, R_STD_MATH_ERROR_DOMAIN, native_errno);
    R_TEST_CHECK_ERROR(c_double, R_STD_MATH_ERROR_DOMAIN, native_errno);
    return 0;
}

static int r_test_domain_c_long_double(long double base, long double exponent) {
    long double native_result;
    int native_errno;
    int exceptions;
    RStdMathCLongDoubleResult result;

    R_TEST_CHECK(r_test_native_c_long_double(
                     base, exponent, &native_result, &native_errno, &exceptions) == 0);
    R_TEST_CHECK(r_test_is_nan_c_long_double(native_result));
    result = r_std_math_pow_c_long_double(base, exponent);
    R_TEST_CHECK_ERROR(result, R_STD_MATH_ERROR_DOMAIN, native_errno);
    return 0;
}

static int r_test_domain(void) {
    R_TEST_CHECK(r_test_domain_f32(-2.0F, 0.5F) == 0);
    R_TEST_CHECK(r_test_domain_f32(-2.0F, -0.5F) == 0);
    R_TEST_CHECK(r_test_domain_f32(-FLT_MAX, 0.5F) == 0);
    R_TEST_CHECK(r_test_domain_f64(-2.0, 0.5) == 0);
    R_TEST_CHECK(r_test_domain_f64(-2.0, -0.5) == 0);
    R_TEST_CHECK(r_test_domain_f64(-DBL_MAX, 0.5) == 0);
    R_TEST_CHECK(r_test_domain_c_long_double(-2.0L, 0.5L) == 0);
    R_TEST_CHECK(r_test_domain_c_long_double(-2.0L, -0.5L) == 0);
    R_TEST_CHECK(r_test_domain_c_long_double(-LDBL_MAX, 0.5L) == 0);
    return 0;
}

static int r_test_pole_f32(float base, float exponent) {
    float native_result;
    int native_errno;
    int exceptions;
    RStdMathF32Result f32;
    RStdMathCFloatResult c_float;

    R_TEST_CHECK(r_test_native_f32(base, exponent, &native_result, &native_errno, &exceptions) ==
                 0);
    R_TEST_CHECK(r_test_is_infinite_f32(native_result));
    f32 = r_std_math_pow_f32(base, exponent);
    c_float = r_std_math_pow_c_float(base, exponent);
    R_TEST_CHECK_ERROR(f32, R_STD_MATH_ERROR_POLE, native_errno);
    R_TEST_CHECK_ERROR(c_float, R_STD_MATH_ERROR_POLE, native_errno);
    return 0;
}

static int r_test_pole_f64(double base, double exponent) {
    double native_result;
    int native_errno;
    int exceptions;
    RStdMathF64Result f64;
    RStdMathCDoubleResult c_double;

    R_TEST_CHECK(r_test_native_f64(base, exponent, &native_result, &native_errno, &exceptions) ==
                 0);
    R_TEST_CHECK(r_test_is_infinite_f64(native_result));
    f64 = r_std_math_pow_f64(base, exponent);
    c_double = r_std_math_pow_c_double(base, exponent);
    R_TEST_CHECK_ERROR(f64, R_STD_MATH_ERROR_POLE, native_errno);
    R_TEST_CHECK_ERROR(c_double, R_STD_MATH_ERROR_POLE, native_errno);
    return 0;
}

static int r_test_pole_c_long_double(long double base, long double exponent) {
    long double native_result;
    int native_errno;
    int exceptions;
    RStdMathCLongDoubleResult result;

    R_TEST_CHECK(r_test_native_c_long_double(
                     base, exponent, &native_result, &native_errno, &exceptions) == 0);
    R_TEST_CHECK(r_test_is_infinite_c_long_double(native_result));
    result = r_std_math_pow_c_long_double(base, exponent);
    R_TEST_CHECK_ERROR(result, R_STD_MATH_ERROR_POLE, native_errno);
    return 0;
}

static int r_test_pole(void) {
    R_TEST_CHECK(r_test_pole_f32(0.0F, -2.0F) == 0);
    R_TEST_CHECK(r_test_pole_f32(-0.0F, -3.0F) == 0);
    R_TEST_CHECK(r_test_pole_f32(-0.0F, -INFINITY) == 0);
    R_TEST_CHECK(r_test_pole_f64(0.0, -2.0) == 0);
    R_TEST_CHECK(r_test_pole_f64(-0.0, -3.0) == 0);
    R_TEST_CHECK(r_test_pole_f64(-0.0, -INFINITY) == 0);
    R_TEST_CHECK(r_test_pole_c_long_double(0.0L, -2.0L) == 0);
    R_TEST_CHECK(r_test_pole_c_long_double(-0.0L, -3.0L) == 0);
    R_TEST_CHECK(r_test_pole_c_long_double(-0.0L, -INFINITY) == 0);
    return 0;
}

static int r_test_f32_range(void) {
    float native_exact;
    float native_inexact;
    float native_zero;
    float native_overflow;
    int exact_errno;
    int exact_exceptions;
    int inexact_errno;
    int inexact_exceptions;
    int zero_errno;
    int zero_exceptions;
    int overflow_errno;
    int overflow_exceptions;
    RStdMathF32Result f32_exact;
    RStdMathCFloatResult c_float_exact;
    RStdMathF32Result f32_inexact;
    RStdMathCFloatResult c_float_zero;
    RStdMathF32Result f32_overflow;
    RStdMathCFloatResult c_float_overflow;

    R_TEST_CHECK(r_test_native_f32(2.0F, -149.0F, &native_exact, &exact_errno, &exact_exceptions) ==
                 0);
    R_TEST_CHECK(r_test_native_f32(
                     2.0F, -148.5F, &native_inexact, &inexact_errno, &inexact_exceptions) == 0);
    R_TEST_CHECK(r_test_native_f32(2.0F, -150.0F, &native_zero, &zero_errno, &zero_exceptions) ==
                 0);
    R_TEST_CHECK(r_test_native_f32(
                     2.0F, 128.0F, &native_overflow, &overflow_errno, &overflow_exceptions) == 0);
    R_TEST_CHECK(r_test_is_subnormal_f32(native_exact) &&
                 r_test_same_f32(native_exact, FLT_TRUE_MIN));
    R_TEST_CHECK((exact_exceptions & FE_INEXACT) == 0);
    R_TEST_CHECK(r_test_is_subnormal_f32(native_inexact) && (inexact_exceptions & FE_INEXACT) != 0);
    R_TEST_CHECK(r_test_is_zero_f32(native_zero) && (zero_exceptions & FE_INEXACT) != 0);
    R_TEST_CHECK(r_test_is_infinite_f32(native_overflow));
    f32_exact = r_std_math_pow_f32(2.0F, -149.0F);
    c_float_exact = r_std_math_pow_c_float(-2.0F, -149.0F);
    R_TEST_CHECK(f32_exact.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_same_f32(f32_exact.value, FLT_TRUE_MIN));
    R_TEST_CHECK(c_float_exact.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_same_f32(c_float_exact.value, -FLT_TRUE_MIN));
    f32_inexact = r_std_math_pow_f32(2.0F, -148.5F);
    c_float_zero = r_std_math_pow_c_float(2.0F, -150.0F);
    R_TEST_CHECK_ERROR(f32_inexact, R_STD_MATH_ERROR_UNDERFLOW, inexact_errno);
    R_TEST_CHECK_ERROR(c_float_zero, R_STD_MATH_ERROR_UNDERFLOW, zero_errno);
    f32_overflow = r_std_math_pow_f32(2.0F, 128.0F);
    c_float_overflow = r_std_math_pow_c_float(-2.0F, 128.0F);
    R_TEST_CHECK_ERROR(f32_overflow, R_STD_MATH_ERROR_OVERFLOW, overflow_errno);
    R_TEST_CHECK_ERROR(c_float_overflow, R_STD_MATH_ERROR_OVERFLOW, overflow_errno);
    return 0;
}

static int r_test_f64_range(void) {
    double native_exact;
    double native_inexact;
    double native_zero;
    double native_overflow;
    int exact_errno;
    int exact_exceptions;
    int inexact_errno;
    int inexact_exceptions;
    int zero_errno;
    int zero_exceptions;
    int overflow_errno;
    int overflow_exceptions;
    RStdMathF64Result f64_exact;
    RStdMathCDoubleResult c_double_exact;
    RStdMathF64Result f64_inexact;
    RStdMathCDoubleResult c_double_zero;
    RStdMathF64Result f64_overflow;
    RStdMathCDoubleResult c_double_overflow;

    R_TEST_CHECK(r_test_native_f64(2.0, -1074.0, &native_exact, &exact_errno, &exact_exceptions) ==
                 0);
    R_TEST_CHECK(
        r_test_native_f64(2.0, -1073.5, &native_inexact, &inexact_errno, &inexact_exceptions) == 0);
    R_TEST_CHECK(r_test_native_f64(2.0, -1075.0, &native_zero, &zero_errno, &zero_exceptions) == 0);
    R_TEST_CHECK(r_test_native_f64(
                     2.0, 1024.0, &native_overflow, &overflow_errno, &overflow_exceptions) == 0);
    R_TEST_CHECK(r_test_is_subnormal_f64(native_exact) &&
                 r_test_same_f64(native_exact, DBL_TRUE_MIN));
    R_TEST_CHECK((exact_exceptions & FE_INEXACT) == 0);
    R_TEST_CHECK(r_test_is_subnormal_f64(native_inexact) && (inexact_exceptions & FE_INEXACT) != 0);
    R_TEST_CHECK(r_test_is_zero_f64(native_zero) && (zero_exceptions & FE_INEXACT) != 0);
    R_TEST_CHECK(r_test_is_infinite_f64(native_overflow));
    f64_exact = r_std_math_pow_f64(2.0, -1074.0);
    c_double_exact = r_std_math_pow_c_double(-2.0, -1073.0);
    R_TEST_CHECK(f64_exact.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_same_f64(f64_exact.value, DBL_TRUE_MIN));
    R_TEST_CHECK(c_double_exact.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_same_f64(c_double_exact.value, -0x1p-1073));
    f64_inexact = r_std_math_pow_f64(2.0, -1073.5);
    c_double_zero = r_std_math_pow_c_double(2.0, -1075.0);
    R_TEST_CHECK_ERROR(f64_inexact, R_STD_MATH_ERROR_UNDERFLOW, inexact_errno);
    R_TEST_CHECK_ERROR(c_double_zero, R_STD_MATH_ERROR_UNDERFLOW, zero_errno);
    f64_overflow = r_std_math_pow_f64(2.0, 1024.0);
    c_double_overflow = r_std_math_pow_c_double(-2.0, 1024.0);
    R_TEST_CHECK_ERROR(f64_overflow, R_STD_MATH_ERROR_OVERFLOW, overflow_errno);
    R_TEST_CHECK_ERROR(c_double_overflow, R_STD_MATH_ERROR_OVERFLOW, overflow_errno);
    return 0;
}

static int r_test_c_long_double_range(void) {
    long double native_exact;
    long double native_inexact;
    long double native_zero;
    long double native_overflow;
    int exact_errno;
    int exact_exceptions;
    int inexact_errno;
    int inexact_exceptions;
    int zero_errno;
    int zero_exceptions;
    int overflow_errno;
    int overflow_exceptions;
    RStdMathCLongDoubleResult exact;
    RStdMathCLongDoubleResult negative_exact;
    RStdMathCLongDoubleResult inexact;
    RStdMathCLongDoubleResult zero;
    RStdMathCLongDoubleResult overflow;

    R_TEST_CHECK(r_test_native_c_long_double(
                     2.0L, -1074.0L, &native_exact, &exact_errno, &exact_exceptions) == 0);
    R_TEST_CHECK(r_test_native_c_long_double(
                     2.0L, -1073.5L, &native_inexact, &inexact_errno, &inexact_exceptions) == 0);
    R_TEST_CHECK(r_test_native_c_long_double(
                     2.0L, -1075.0L, &native_zero, &zero_errno, &zero_exceptions) == 0);
    R_TEST_CHECK(r_test_native_c_long_double(
                     2.0L, 1024.0L, &native_overflow, &overflow_errno, &overflow_exceptions) == 0);
    R_TEST_CHECK(r_test_is_subnormal_c_long_double(native_exact) &&
                 r_test_same_c_long_double(native_exact, LDBL_TRUE_MIN));
    R_TEST_CHECK((exact_exceptions & FE_INEXACT) == 0);
    R_TEST_CHECK(r_test_is_subnormal_c_long_double(native_inexact) &&
                 (inexact_exceptions & FE_INEXACT) != 0);
    R_TEST_CHECK(r_test_is_zero_c_long_double(native_zero) && (zero_exceptions & FE_INEXACT) != 0);
    R_TEST_CHECK(r_test_is_infinite_c_long_double(native_overflow));
    exact = r_std_math_pow_c_long_double(2.0L, -1074.0L);
    negative_exact = r_std_math_pow_c_long_double(-2.0L, -1073.0L);
    R_TEST_CHECK(exact.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_same_c_long_double(exact.value, LDBL_TRUE_MIN));
    R_TEST_CHECK(negative_exact.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_same_c_long_double(negative_exact.value, -0x1p-1073L));
    inexact = r_std_math_pow_c_long_double(2.0L, -1073.5L);
    zero = r_std_math_pow_c_long_double(2.0L, -1075.0L);
    overflow = r_std_math_pow_c_long_double(2.0L, 1024.0L);
    R_TEST_CHECK_ERROR(inexact, R_STD_MATH_ERROR_UNDERFLOW, inexact_errno);
    R_TEST_CHECK_ERROR(zero, R_STD_MATH_ERROR_UNDERFLOW, zero_errno);
    R_TEST_CHECK_ERROR(overflow, R_STD_MATH_ERROR_OVERFLOW, overflow_errno);
    return 0;
}

static int r_test_nan_and_infinity(void) {
    RStdMathF32Result f32_nan = r_std_math_pow_f32(NAN, 2.0F);
    RStdMathCFloatResult c_float_nan = r_std_math_pow_c_float(-1.0F, NAN);
    RStdMathF64Result f64_nan = r_std_math_pow_f64(2.0, NAN);
    RStdMathCDoubleResult c_double_infinity = r_std_math_pow_c_double(-2.0, INFINITY);
    RStdMathF64Result f64_zero = r_std_math_pow_f64(-2.0, -INFINITY);
    RStdMathCLongDoubleResult long_negative_infinity =
        r_std_math_pow_c_long_double(-INFINITY, 3.0L);
    RStdMathCLongDoubleResult long_negative_zero = r_std_math_pow_c_long_double(-INFINITY, -3.0L);
    RStdMathCLongDoubleResult long_nan = r_std_math_pow_c_long_double(2.0L, NAN);

    R_TEST_CHECK(f32_nan.status == R_STD_MATH_CALL_SUCCESS && r_test_is_nan_f32(f32_nan.value));
    R_TEST_CHECK(c_float_nan.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_is_nan_f32(c_float_nan.value));
    R_TEST_CHECK(f64_nan.status == R_STD_MATH_CALL_SUCCESS && r_test_is_nan_f64(f64_nan.value));
    R_TEST_CHECK(c_double_infinity.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_is_infinite_f64(c_double_infinity.value));
    R_TEST_CHECK(f64_zero.status == R_STD_MATH_CALL_SUCCESS && r_test_is_zero_f64(f64_zero.value));
    R_TEST_CHECK(long_negative_infinity.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_is_infinite_c_long_double(long_negative_infinity.value) &&
                 (r_test_bits_c_long_double(long_negative_infinity.value) >> UINT32_C(63)) != 0U);
    R_TEST_CHECK(long_negative_zero.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_is_zero_c_long_double(long_negative_zero.value) &&
                 (r_test_bits_c_long_double(long_negative_zero.value) >> UINT32_C(63)) != 0U);
    R_TEST_CHECK(long_nan.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_is_nan_c_long_double(long_nan.value));
    return 0;
}

static void *r_test_thread_main(void *opaque_context) {
    RTestPowThreadContext *context = opaque_context;
    int iteration;

    if (r_test_seed_environment(context->rounding, context->exceptions, context->native_errno) !=
        0) {
        context->failed = 1;
        return NULL;
    }
    for (iteration = 0; iteration < 500; iteration += 1) {
        RStdMathF32Result regular_f32 = r_std_math_pow_f32(-2.0F, 3.0F);
        RStdMathF64Result regular_f64 = r_std_math_pow_f64(2.0, 10.0);
        RStdMathCFloatResult domain = r_std_math_pow_c_float(-2.0F, 0.5F);
        RStdMathCDoubleResult pole = r_std_math_pow_c_double(-0.0, -3.0);
        RStdMathF32Result underflow = r_std_math_pow_f32(2.0F, -148.5F);
        RStdMathF64Result overflow = r_std_math_pow_f64(2.0, 1024.0);
        RStdMathCLongDoubleResult exact = r_std_math_pow_c_long_double(2.0L, -1074.0L);
        RStdMathCFloatResult nan_zero = r_std_math_pow_c_float(r_test_signaling_nan_f32(), 0.0F);
        RStdMathCDoubleResult one_nan = r_std_math_pow_c_double(1.0, r_test_signaling_nan_f64());
        RStdMathCLongDoubleResult infinity = r_std_math_pow_c_long_double(-2.0L, INFINITY);

        if ((regular_f32.status != R_STD_MATH_CALL_SUCCESS) || (regular_f32.value != -8.0F) ||
            (regular_f64.status != R_STD_MATH_CALL_SUCCESS) || (regular_f64.value != 1024.0) ||
            (domain.status != R_STD_MATH_CALL_ERROR) ||
            (domain.error.code != R_STD_MATH_ERROR_DOMAIN) ||
            (domain.error.native_code != context->domain_native_code) ||
            (pole.status != R_STD_MATH_CALL_ERROR) || (pole.error.code != R_STD_MATH_ERROR_POLE) ||
            (pole.error.native_code != context->pole_native_code) ||
            (underflow.status != R_STD_MATH_CALL_ERROR) ||
            (underflow.error.code != R_STD_MATH_ERROR_UNDERFLOW) ||
            (underflow.error.native_code != context->underflow_native_code) ||
            (overflow.status != R_STD_MATH_CALL_ERROR) ||
            (overflow.error.code != R_STD_MATH_ERROR_OVERFLOW) ||
            (overflow.error.native_code != context->overflow_native_code) ||
            (exact.status != R_STD_MATH_CALL_SUCCESS) ||
            !r_test_same_c_long_double(exact.value, LDBL_TRUE_MIN) ||
            (nan_zero.status != R_STD_MATH_CALL_SUCCESS) || (nan_zero.value != 1.0F) ||
            (one_nan.status != R_STD_MATH_CALL_SUCCESS) || (one_nan.value != 1.0) ||
            (infinity.status != R_STD_MATH_CALL_SUCCESS) ||
            !r_test_is_infinite_c_long_double(infinity.value) ||
            !r_test_environment_is(context->rounding, context->exceptions, context->native_errno)) {
            context->failed = 1;
            return NULL;
        }
    }
    return NULL;
}

static int r_test_environment_and_concurrency(void) {
    RStdMathCFloatResult domain = r_std_math_pow_c_float(-2.0F, 0.5F);
    RStdMathCDoubleResult pole = r_std_math_pow_c_double(-0.0, -3.0);
    RStdMathF32Result underflow = r_std_math_pow_f32(2.0F, -148.5F);
    RStdMathF64Result overflow = r_std_math_pow_f64(2.0, 1024.0);
    RTestPowThreadContext first;
    RTestPowThreadContext second;
    pthread_t first_thread;
    pthread_t second_thread;

    R_TEST_CHECK(domain.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(pole.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(underflow.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(overflow.status == R_STD_MATH_CALL_ERROR);
    first = (RTestPowThreadContext){FE_DOWNWARD,
                                    FE_DIVBYZERO,
                                    EDOM,
                                    domain.error.native_code,
                                    pole.error.native_code,
                                    underflow.error.native_code,
                                    overflow.error.native_code,
                                    0};
    second = (RTestPowThreadContext){FE_UPWARD,
                                     FE_INVALID,
                                     ERANGE,
                                     domain.error.native_code,
                                     pole.error.native_code,
                                     underflow.error.native_code,
                                     overflow.error.native_code,
                                     0};
    R_TEST_CHECK(pthread_create(&first_thread, NULL, r_test_thread_main, &first) == 0);
    R_TEST_CHECK(pthread_create(&second_thread, NULL, r_test_thread_main, &second) == 0);
    R_TEST_CHECK(pthread_join(first_thread, NULL) == 0);
    R_TEST_CHECK(pthread_join(second_thread, NULL) == 0);
    R_TEST_CHECK(first.failed == 0 && second.failed == 0);
    return 0;
}

static int r_test_allocation_free(void) {
    RRuntimeAllocator allocator;
    void *allocation = NULL;

    r_runtime_allocator_initialize(&allocator);
    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    R_TEST_CHECK(r_std_math_pow_f32(2.0F, 10.0F).status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_std_math_pow_f64(-2.0, 0.5).status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(r_std_math_pow_c_float(-0.0F, -3.0F).status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(r_std_math_pow_c_double(2.0, 1024.0).status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(r_std_math_pow_c_long_double(2.0L, -1074.0L).status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(0));
    R_TEST_CHECK(
        r_runtime_allocator_allocate(&allocator, 1U, _Alignof(unsigned char), &allocation) ==
        R_RUNTIME_ALLOCATION_EXHAUSTED);
    R_TEST_CHECK(allocation == NULL);
    return 0;
}

int main(void) {
    int original_errno = errno;
    fenv_t original_environment;

    R_TEST_CHECK(fegetenv(&original_environment) == 0);
    R_TEST_CHECK(r_test_seed_environment(FE_UPWARD, FE_DIVBYZERO, E2BIG) == 0);
    R_TEST_CHECK(r_test_regular_and_signed_values() == 0);
    R_TEST_CHECK(r_test_precedence_successes() == 0);
    R_TEST_CHECK(r_test_domain() == 0);
    R_TEST_CHECK(r_test_pole() == 0);
    R_TEST_CHECK(r_test_f32_range() == 0);
    R_TEST_CHECK(r_test_f64_range() == 0);
    R_TEST_CHECK(r_test_c_long_double_range() == 0);
    R_TEST_CHECK(r_test_nan_and_infinity() == 0);
    R_TEST_CHECK(r_test_environment_is(FE_UPWARD, FE_DIVBYZERO, E2BIG));
    R_TEST_CHECK(r_test_environment_and_concurrency() == 0);
    R_TEST_CHECK(r_test_environment_is(FE_UPWARD, FE_DIVBYZERO, E2BIG));
    R_TEST_CHECK(r_test_allocation_free() == 0);
    R_TEST_CHECK(fesetenv(&original_environment) == 0);
    errno = original_errno;
    (void)fprintf(stdout, "library_math_pow_tests: ok\n");
    return 0;
}

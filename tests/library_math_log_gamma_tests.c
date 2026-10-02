#if !defined(__APPLE__)
#error "std.math::log_gamma tests require Darwin lgamma_r primitives"
#endif

#if !defined(_REENTRANT)
#define _REENTRANT 1
#endif

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

typedef struct RTestLogGammaThreadContext {
    int rounding;
    int exceptions;
    int native_errno;
    float expected_float;
    double expected_double;
    long double expected_long_double;
    int64_t expected_pole_native_code;
    int64_t expected_overflow_native_code;
    int failed;
} RTestLogGammaThreadContext;

static float r_test_f32_from_bits(uint32_t bits) {
    float value;

    (void)memcpy(&value, &bits, sizeof(value));
    return value;
}

static double r_test_f64_from_bits(uint64_t bits) {
    double value;

    (void)memcpy(&value, &bits, sizeof(value));
    return value;
}

static long double r_test_c_long_double_from_bits(uint64_t bits) {
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

static _Bool r_test_is_normal_f32(float value) {
    uint32_t exponent = r_test_bits_f32(value) & UINT32_C(0x7f800000);

    return exponent != UINT32_C(0) && exponent != UINT32_C(0x7f800000);
}

static _Bool r_test_is_normal_f64(double value) {
    uint64_t exponent = r_test_bits_f64(value) & UINT64_C(0x7ff0000000000000);

    return exponent != UINT64_C(0) && exponent != UINT64_C(0x7ff0000000000000);
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
r_test_native_f32(float value, float *result, int *native_errno, int *exceptions, int *gamma_sign) {
    fenv_t caller_environment;
    fenv_t held_environment;
    int caller_errno = errno;
    volatile float canonical_operand;

    if (fegetenv(&caller_environment) != 0) {
        return 1;
    }
    if ((feholdexcept(&held_environment) != 0) || (fesetround(FE_TONEAREST) != 0)) {
        (void)fesetenv(&caller_environment);
        errno = caller_errno;
        return 1;
    }
    errno = 0;
    canonical_operand = value;
    *result = lgammaf_r(canonical_operand, gamma_sign);
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
    double value, double *result, int *native_errno, int *exceptions, int *gamma_sign) {
    fenv_t caller_environment;
    fenv_t held_environment;
    int caller_errno = errno;
    volatile double canonical_operand;

    if (fegetenv(&caller_environment) != 0) {
        return 1;
    }
    if ((feholdexcept(&held_environment) != 0) || (fesetround(FE_TONEAREST) != 0)) {
        (void)fesetenv(&caller_environment);
        errno = caller_errno;
        return 1;
    }
    errno = 0;
    canonical_operand = value;
    *result = lgamma_r(canonical_operand, gamma_sign);
    *native_errno = errno;
    *exceptions = fetestexcept(FE_ALL_EXCEPT);
    if (fesetenv(&caller_environment) != 0) {
        errno = caller_errno;
        return 1;
    }
    errno = caller_errno;
    return 0;
}

static int r_test_native_c_long_double(
    long double value, long double *result, int *native_errno, int *exceptions, int *gamma_sign) {
    fenv_t caller_environment;
    fenv_t held_environment;
    int caller_errno = errno;
    volatile long double canonical_operand;

    if (fegetenv(&caller_environment) != 0) {
        return 1;
    }
    if ((feholdexcept(&held_environment) != 0) || (fesetround(FE_TONEAREST) != 0)) {
        (void)fesetenv(&caller_environment);
        errno = caller_errno;
        return 1;
    }
    errno = 0;
    canonical_operand = value;
    *result = lgammal_r(canonical_operand, gamma_sign);
    *native_errno = errno;
    *exceptions = fetestexcept(FE_ALL_EXCEPT);
    if (fesetenv(&caller_environment) != 0) {
        errno = caller_errno;
        return 1;
    }
    errno = caller_errno;
    return 0;
}

static int r_test_pair_f32(float value, int expected_gamma_sign) {
    float expected;
    int native_errno;
    int exceptions;
    int gamma_sign;
    RStdMathF32Result f32;
    RStdMathCFloatResult c_float;

    R_TEST_CHECK(r_test_native_f32(value, &expected, &native_errno, &exceptions, &gamma_sign) == 0);
    R_TEST_CHECK(gamma_sign == expected_gamma_sign);
    f32 = r_std_math_log_gamma_f32(value);
    c_float = r_std_math_log_gamma_c_float(value);
    R_TEST_CHECK(f32.status == R_STD_MATH_CALL_SUCCESS && r_test_same_f32(f32.value, expected));
    R_TEST_CHECK(c_float.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_same_f32(c_float.value, expected));
    return 0;
}

static int r_test_pair_f64(double value, int expected_gamma_sign) {
    double expected;
    int native_errno;
    int exceptions;
    int gamma_sign;
    RStdMathF64Result f64;
    RStdMathCDoubleResult c_double;

    R_TEST_CHECK(r_test_native_f64(value, &expected, &native_errno, &exceptions, &gamma_sign) == 0);
    R_TEST_CHECK(gamma_sign == expected_gamma_sign);
    f64 = r_std_math_log_gamma_f64(value);
    c_double = r_std_math_log_gamma_c_double(value);
    R_TEST_CHECK(f64.status == R_STD_MATH_CALL_SUCCESS && r_test_same_f64(f64.value, expected));
    R_TEST_CHECK(c_double.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_same_f64(c_double.value, expected));
    return 0;
}

static int r_test_pair_c_long_double(long double value, int expected_gamma_sign) {
    long double expected;
    int native_errno;
    int exceptions;
    int gamma_sign;
    RStdMathCLongDoubleResult result;

    R_TEST_CHECK(r_test_native_c_long_double(
                     value, &expected, &native_errno, &exceptions, &gamma_sign) == 0);
    R_TEST_CHECK(gamma_sign == expected_gamma_sign);
    result = r_std_math_log_gamma_c_long_double(value);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_same_c_long_double(result.value, expected));
    return 0;
}

static int r_test_regular_values_and_sign(void) {
    R_TEST_CHECK(r_test_pair_f32(-0.5F, -1) == 0);
    R_TEST_CHECK(r_test_pair_f32(-1.5F, 1) == 0);
    R_TEST_CHECK(r_test_pair_f32(0.5F, 1) == 0);
    R_TEST_CHECK(r_test_pair_f32(1.5F, 1) == 0);
    R_TEST_CHECK(r_test_pair_f32(5.0F, 1) == 0);
    R_TEST_CHECK(r_test_pair_f64(-0.5, -1) == 0);
    R_TEST_CHECK(r_test_pair_f64(-1.5, 1) == 0);
    R_TEST_CHECK(r_test_pair_f64(0.5, 1) == 0);
    R_TEST_CHECK(r_test_pair_f64(1.5, 1) == 0);
    R_TEST_CHECK(r_test_pair_f64(5.0, 1) == 0);
    R_TEST_CHECK(r_test_pair_c_long_double(-0.5L, -1) == 0);
    R_TEST_CHECK(r_test_pair_c_long_double(-1.5L, 1) == 0);
    R_TEST_CHECK(r_test_pair_c_long_double(0.5L, 1) == 0);
    R_TEST_CHECK(r_test_pair_c_long_double(1.5L, 1) == 0);
    R_TEST_CHECK(r_test_pair_c_long_double(5.0L, 1) == 0);
    return 0;
}

static int r_test_pole_f32(float value) {
    float native_result;
    int native_errno;
    int exceptions;
    int gamma_sign;
    RStdMathF32Result f32;
    RStdMathCFloatResult c_float;

    R_TEST_CHECK(
        r_test_native_f32(value, &native_result, &native_errno, &exceptions, &gamma_sign) == 0);
    R_TEST_CHECK(r_test_is_infinite_f32(native_result));
    f32 = r_std_math_log_gamma_f32(value);
    c_float = r_std_math_log_gamma_c_float(value);
    R_TEST_CHECK_ERROR(f32, R_STD_MATH_ERROR_POLE, native_errno);
    R_TEST_CHECK_ERROR(c_float, R_STD_MATH_ERROR_POLE, native_errno);
    return 0;
}

static int r_test_pole_f64(double value) {
    double native_result;
    int native_errno;
    int exceptions;
    int gamma_sign;
    RStdMathF64Result f64;
    RStdMathCDoubleResult c_double;

    R_TEST_CHECK(
        r_test_native_f64(value, &native_result, &native_errno, &exceptions, &gamma_sign) == 0);
    R_TEST_CHECK(r_test_is_infinite_f64(native_result));
    f64 = r_std_math_log_gamma_f64(value);
    c_double = r_std_math_log_gamma_c_double(value);
    R_TEST_CHECK_ERROR(f64, R_STD_MATH_ERROR_POLE, native_errno);
    R_TEST_CHECK_ERROR(c_double, R_STD_MATH_ERROR_POLE, native_errno);
    return 0;
}

static int r_test_pole_c_long_double(long double value) {
    long double native_result;
    int native_errno;
    int exceptions;
    int gamma_sign;
    RStdMathCLongDoubleResult result;

    R_TEST_CHECK(r_test_native_c_long_double(
                     value, &native_result, &native_errno, &exceptions, &gamma_sign) == 0);
    R_TEST_CHECK(r_test_is_infinite_c_long_double(native_result));
    result = r_std_math_log_gamma_c_long_double(value);
    R_TEST_CHECK_ERROR(result, R_STD_MATH_ERROR_POLE, native_errno);
    return 0;
}

static int r_test_poles(void) {
    R_TEST_CHECK(r_test_pole_f32(0.0F) == 0);
    R_TEST_CHECK(r_test_pole_f32(-0.0F) == 0);
    R_TEST_CHECK(r_test_pole_f32(-1.0F) == 0);
    R_TEST_CHECK(r_test_pole_f32(-2.0F) == 0);
    R_TEST_CHECK(r_test_pole_f32(-FLT_MAX) == 0);
    R_TEST_CHECK(r_test_pole_f64(0.0) == 0);
    R_TEST_CHECK(r_test_pole_f64(-0.0) == 0);
    R_TEST_CHECK(r_test_pole_f64(-1.0) == 0);
    R_TEST_CHECK(r_test_pole_f64(-2.0) == 0);
    R_TEST_CHECK(r_test_pole_f64(-DBL_MAX) == 0);
    R_TEST_CHECK(r_test_pole_c_long_double(0.0L) == 0);
    R_TEST_CHECK(r_test_pole_c_long_double(-0.0L) == 0);
    R_TEST_CHECK(r_test_pole_c_long_double(-1.0L) == 0);
    R_TEST_CHECK(r_test_pole_c_long_double(-2.0L) == 0);
    R_TEST_CHECK(r_test_pole_c_long_double(-LDBL_MAX) == 0);
    return 0;
}

static int r_test_exact_zero_and_neighborhood(void) {
    float native_f32_one;
    float native_f32_two;
    double native_f64_one;
    double native_f64_two;
    long double native_long_one;
    long double native_long_two;
    int native_errno;
    int exceptions_one;
    int exceptions_two;
    int gamma_sign;
    RStdMathF32Result f32_one;
    RStdMathCFloatResult c_float_two;
    RStdMathF64Result f64_one;
    RStdMathCDoubleResult c_double_two;
    RStdMathCLongDoubleResult long_one;
    RStdMathCLongDoubleResult long_two;

    R_TEST_CHECK(
        r_test_native_f32(1.0F, &native_f32_one, &native_errno, &exceptions_one, &gamma_sign) == 0);
    R_TEST_CHECK(
        r_test_native_f32(2.0F, &native_f32_two, &native_errno, &exceptions_two, &gamma_sign) == 0);
    R_TEST_CHECK(r_test_is_zero_f32(native_f32_one) && r_test_is_zero_f32(native_f32_two));
    R_TEST_CHECK((exceptions_one & FE_INEXACT) == 0 && (exceptions_two & FE_INEXACT) == 0);
    f32_one = r_std_math_log_gamma_f32(1.0F);
    c_float_two = r_std_math_log_gamma_c_float(2.0F);
    R_TEST_CHECK(f32_one.status == R_STD_MATH_CALL_SUCCESS && r_test_is_zero_f32(f32_one.value));
    R_TEST_CHECK(c_float_two.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_is_zero_f32(c_float_two.value));

    R_TEST_CHECK(
        r_test_native_f64(1.0, &native_f64_one, &native_errno, &exceptions_one, &gamma_sign) == 0);
    R_TEST_CHECK(
        r_test_native_f64(2.0, &native_f64_two, &native_errno, &exceptions_two, &gamma_sign) == 0);
    R_TEST_CHECK(r_test_is_zero_f64(native_f64_one) && r_test_is_zero_f64(native_f64_two));
    R_TEST_CHECK((exceptions_one & FE_INEXACT) == 0 && (exceptions_two & FE_INEXACT) == 0);
    f64_one = r_std_math_log_gamma_f64(1.0);
    c_double_two = r_std_math_log_gamma_c_double(2.0);
    R_TEST_CHECK(f64_one.status == R_STD_MATH_CALL_SUCCESS && r_test_is_zero_f64(f64_one.value));
    R_TEST_CHECK(c_double_two.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_is_zero_f64(c_double_two.value));

    R_TEST_CHECK(r_test_native_c_long_double(
                     1.0L, &native_long_one, &native_errno, &exceptions_one, &gamma_sign) == 0);
    R_TEST_CHECK(r_test_native_c_long_double(
                     2.0L, &native_long_two, &native_errno, &exceptions_two, &gamma_sign) == 0);
    R_TEST_CHECK(r_test_is_zero_c_long_double(native_long_one) &&
                 r_test_is_zero_c_long_double(native_long_two));
    R_TEST_CHECK((exceptions_one & FE_INEXACT) == 0 && (exceptions_two & FE_INEXACT) == 0);
    long_one = r_std_math_log_gamma_c_long_double(1.0L);
    long_two = r_std_math_log_gamma_c_long_double(2.0L);
    R_TEST_CHECK(long_one.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_is_zero_c_long_double(long_one.value));
    R_TEST_CHECK(long_two.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_is_zero_c_long_double(long_two.value));

    R_TEST_CHECK(r_test_pair_f32(nextafterf(1.0F, 0.0F), 1) == 0);
    R_TEST_CHECK(r_test_pair_f32(nextafterf(1.0F, INFINITY), 1) == 0);
    R_TEST_CHECK(r_test_pair_f64(nextafter(1.0, 0.0), 1) == 0);
    R_TEST_CHECK(r_test_pair_f64(nextafter(1.0, INFINITY), 1) == 0);
    R_TEST_CHECK(r_test_pair_c_long_double(nextafterl(1.0L, 0.0L), 1) == 0);
    R_TEST_CHECK(r_test_pair_c_long_double(nextafterl(1.0L, INFINITY), 1) == 0);
    R_TEST_CHECK(r_test_is_normal_f32(r_std_math_log_gamma_f32(nextafterf(1.0F, 0.0F)).value));
    R_TEST_CHECK(r_test_is_normal_f64(r_std_math_log_gamma_f64(nextafter(1.0, 0.0)).value));
    return 0;
}

static int r_test_overflow(void) {
    float native_f32;
    double native_f64;
    long double native_long;
    int native_errno_f32;
    int native_errno_f64;
    int native_errno_long;
    int exceptions;
    int gamma_sign;
    RStdMathF32Result f32;
    RStdMathCFloatResult c_float;
    RStdMathF64Result f64;
    RStdMathCDoubleResult c_double;
    RStdMathCLongDoubleResult c_long_double;

    R_TEST_CHECK(
        r_test_native_f32(FLT_MAX, &native_f32, &native_errno_f32, &exceptions, &gamma_sign) == 0);
    R_TEST_CHECK(
        r_test_native_f64(DBL_MAX, &native_f64, &native_errno_f64, &exceptions, &gamma_sign) == 0);
    R_TEST_CHECK(r_test_native_c_long_double(
                     LDBL_MAX, &native_long, &native_errno_long, &exceptions, &gamma_sign) == 0);
    R_TEST_CHECK(r_test_is_infinite_f32(native_f32));
    R_TEST_CHECK(r_test_is_infinite_f64(native_f64));
    R_TEST_CHECK(r_test_is_infinite_c_long_double(native_long));
    f32 = r_std_math_log_gamma_f32(FLT_MAX);
    c_float = r_std_math_log_gamma_c_float(FLT_MAX);
    f64 = r_std_math_log_gamma_f64(DBL_MAX);
    c_double = r_std_math_log_gamma_c_double(DBL_MAX);
    c_long_double = r_std_math_log_gamma_c_long_double(LDBL_MAX);
    R_TEST_CHECK_ERROR(f32, R_STD_MATH_ERROR_OVERFLOW, native_errno_f32);
    R_TEST_CHECK_ERROR(c_float, R_STD_MATH_ERROR_OVERFLOW, native_errno_f32);
    R_TEST_CHECK_ERROR(f64, R_STD_MATH_ERROR_OVERFLOW, native_errno_f64);
    R_TEST_CHECK_ERROR(c_double, R_STD_MATH_ERROR_OVERFLOW, native_errno_f64);
    R_TEST_CHECK_ERROR(c_long_double, R_STD_MATH_ERROR_OVERFLOW, native_errno_long);
    return 0;
}

static int r_test_infinity_nan_and_absent_domain(void) {
    float signaling_f32 = r_test_f32_from_bits(UINT32_C(0x7f800001));
    double signaling_f64 = r_test_f64_from_bits(UINT64_C(0x7ff0000000000001));
    long double signaling_long = r_test_c_long_double_from_bits(UINT64_C(0x7ff0000000000001));
    RStdMathF32Result f32_negative_infinity = r_std_math_log_gamma_f32(-INFINITY);
    RStdMathCFloatResult c_float_positive_infinity = r_std_math_log_gamma_c_float(INFINITY);
    RStdMathF64Result f64_negative_infinity = r_std_math_log_gamma_f64(-INFINITY);
    RStdMathCDoubleResult c_double_positive_infinity = r_std_math_log_gamma_c_double(INFINITY);
    RStdMathCLongDoubleResult long_negative_infinity =
        r_std_math_log_gamma_c_long_double(-INFINITY);
    RStdMathF32Result f32_nan = r_std_math_log_gamma_f32(signaling_f32);
    RStdMathCFloatResult c_float_nan = r_std_math_log_gamma_c_float(NAN);
    RStdMathF64Result f64_nan = r_std_math_log_gamma_f64(signaling_f64);
    RStdMathCDoubleResult c_double_nan = r_std_math_log_gamma_c_double(NAN);
    RStdMathCLongDoubleResult long_nan = r_std_math_log_gamma_c_long_double(signaling_long);

    R_TEST_CHECK(f32_negative_infinity.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_is_infinite_f32(f32_negative_infinity.value));
    R_TEST_CHECK(c_float_positive_infinity.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_is_infinite_f32(c_float_positive_infinity.value));
    R_TEST_CHECK(f64_negative_infinity.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_is_infinite_f64(f64_negative_infinity.value));
    R_TEST_CHECK(c_double_positive_infinity.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_is_infinite_f64(c_double_positive_infinity.value));
    R_TEST_CHECK(long_negative_infinity.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_is_infinite_c_long_double(long_negative_infinity.value));
    R_TEST_CHECK(f32_nan.status == R_STD_MATH_CALL_SUCCESS && r_test_is_nan_f32(f32_nan.value));
    R_TEST_CHECK(c_float_nan.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_is_nan_f32(c_float_nan.value));
    R_TEST_CHECK(f64_nan.status == R_STD_MATH_CALL_SUCCESS && r_test_is_nan_f64(f64_nan.value));
    R_TEST_CHECK(c_double_nan.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_is_nan_f64(c_double_nan.value));
    R_TEST_CHECK(long_nan.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_is_nan_c_long_double(long_nan.value));
    return 0;
}

static int r_test_signgam_unchanged(void) {
    int original_signgam = signgam;

    signgam = 173;
    R_TEST_CHECK(r_std_math_log_gamma_f32(-0.5F).status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_std_math_log_gamma_f64(-1.5).status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_std_math_log_gamma_c_float(0.5F).status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_std_math_log_gamma_c_double(5.0).status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_std_math_log_gamma_c_long_double(-0.5L).status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(signgam == 173);
    signgam = original_signgam;
    return 0;
}

static void *r_test_thread_main(void *opaque_context) {
    RTestLogGammaThreadContext *context = opaque_context;
    int iteration;

    if (r_test_seed_environment(context->rounding, context->exceptions, context->native_errno) !=
        0) {
        context->failed = 1;
        return NULL;
    }
    for (iteration = 0; iteration < 500; iteration += 1) {
        RStdMathF32Result f32 = r_std_math_log_gamma_f32(-0.5F);
        RStdMathF64Result f64 = r_std_math_log_gamma_f64(-1.5);
        RStdMathCFloatResult c_float = r_std_math_log_gamma_c_float(-0.5F);
        RStdMathCDoubleResult c_double = r_std_math_log_gamma_c_double(-1.5);
        RStdMathCLongDoubleResult c_long_double = r_std_math_log_gamma_c_long_double(-0.5L);
        RStdMathCDoubleResult pole = r_std_math_log_gamma_c_double(-2.0);
        RStdMathF64Result overflow = r_std_math_log_gamma_f64(DBL_MAX);
        RStdMathCFloatResult negative_infinity = r_std_math_log_gamma_c_float(-INFINITY);
        RStdMathCLongDoubleResult nan = r_std_math_log_gamma_c_long_double(NAN);

        if ((f32.status != R_STD_MATH_CALL_SUCCESS) ||
            !r_test_same_f32(f32.value, context->expected_float) ||
            (f64.status != R_STD_MATH_CALL_SUCCESS) ||
            !r_test_same_f64(f64.value, context->expected_double) ||
            (c_float.status != R_STD_MATH_CALL_SUCCESS) ||
            !r_test_same_f32(c_float.value, context->expected_float) ||
            (c_double.status != R_STD_MATH_CALL_SUCCESS) ||
            !r_test_same_f64(c_double.value, context->expected_double) ||
            (c_long_double.status != R_STD_MATH_CALL_SUCCESS) ||
            !r_test_same_c_long_double(c_long_double.value, context->expected_long_double) ||
            (pole.status != R_STD_MATH_CALL_ERROR) || (pole.error.code != R_STD_MATH_ERROR_POLE) ||
            (pole.error.native_code != context->expected_pole_native_code) ||
            (overflow.status != R_STD_MATH_CALL_ERROR) ||
            (overflow.error.code != R_STD_MATH_ERROR_OVERFLOW) ||
            (overflow.error.native_code != context->expected_overflow_native_code) ||
            (negative_infinity.status != R_STD_MATH_CALL_SUCCESS) ||
            !r_test_is_infinite_f32(negative_infinity.value) ||
            (nan.status != R_STD_MATH_CALL_SUCCESS) || !r_test_is_nan_c_long_double(nan.value) ||
            !r_test_environment_is(context->rounding, context->exceptions, context->native_errno)) {
            context->failed = 1;
            return NULL;
        }
    }
    return NULL;
}

static int r_test_environment_and_concurrency(void) {
    float expected_float;
    double expected_double;
    long double expected_long_double;
    int native_errno;
    int exceptions;
    int gamma_sign;
    RStdMathCDoubleResult pole = r_std_math_log_gamma_c_double(-2.0);
    RStdMathF64Result overflow = r_std_math_log_gamma_f64(DBL_MAX);
    RTestLogGammaThreadContext first;
    RTestLogGammaThreadContext second;
    pthread_t first_thread;
    pthread_t second_thread;
    int original_signgam = signgam;

    R_TEST_CHECK(
        r_test_native_f32(-0.5F, &expected_float, &native_errno, &exceptions, &gamma_sign) == 0);
    R_TEST_CHECK(
        r_test_native_f64(-1.5, &expected_double, &native_errno, &exceptions, &gamma_sign) == 0);
    R_TEST_CHECK(r_test_native_c_long_double(
                     -0.5L, &expected_long_double, &native_errno, &exceptions, &gamma_sign) == 0);
    R_TEST_CHECK(pole.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(overflow.status == R_STD_MATH_CALL_ERROR);
    first = (RTestLogGammaThreadContext){FE_DOWNWARD,
                                         FE_DIVBYZERO,
                                         EDOM,
                                         expected_float,
                                         expected_double,
                                         expected_long_double,
                                         pole.error.native_code,
                                         overflow.error.native_code,
                                         0};
    second = (RTestLogGammaThreadContext){FE_UPWARD,
                                          FE_INVALID,
                                          ERANGE,
                                          expected_float,
                                          expected_double,
                                          expected_long_double,
                                          pole.error.native_code,
                                          overflow.error.native_code,
                                          0};
    signgam = 419;
    R_TEST_CHECK(pthread_create(&first_thread, NULL, r_test_thread_main, &first) == 0);
    R_TEST_CHECK(pthread_create(&second_thread, NULL, r_test_thread_main, &second) == 0);
    R_TEST_CHECK(pthread_join(first_thread, NULL) == 0);
    R_TEST_CHECK(pthread_join(second_thread, NULL) == 0);
    R_TEST_CHECK(first.failed == 0 && second.failed == 0);
    R_TEST_CHECK(signgam == 419);
    signgam = original_signgam;
    return 0;
}

static int r_test_allocation_free(void) {
    RRuntimeAllocator allocator;
    void *allocation = NULL;

    r_runtime_allocator_initialize(&allocator);
    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    R_TEST_CHECK(r_std_math_log_gamma_f32(-0.5F).status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_std_math_log_gamma_f64(-2.0).status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(r_std_math_log_gamma_c_float(1.0F).status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_std_math_log_gamma_c_double(DBL_MAX).status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(r_std_math_log_gamma_c_long_double(-INFINITY).status == R_STD_MATH_CALL_SUCCESS);
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
    R_TEST_CHECK(r_test_regular_values_and_sign() == 0);
    R_TEST_CHECK(r_test_poles() == 0);
    R_TEST_CHECK(r_test_exact_zero_and_neighborhood() == 0);
    R_TEST_CHECK(r_test_overflow() == 0);
    R_TEST_CHECK(r_test_infinity_nan_and_absent_domain() == 0);
    R_TEST_CHECK(r_test_signgam_unchanged() == 0);
    R_TEST_CHECK(r_test_environment_is(FE_UPWARD, FE_DIVBYZERO, E2BIG));
    R_TEST_CHECK(r_test_environment_and_concurrency() == 0);
    R_TEST_CHECK(r_test_environment_is(FE_UPWARD, FE_DIVBYZERO, E2BIG));
    R_TEST_CHECK(r_test_allocation_free() == 0);
    R_TEST_CHECK(fesetenv(&original_environment) == 0);
    errno = original_errno;
    (void)fprintf(stdout, "library_math_log_gamma_tests: ok\n");
    return 0;
}

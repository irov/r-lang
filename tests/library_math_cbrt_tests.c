#include "r_std_math.h"

#include <errno.h>
#include <fenv.h>
#include <float.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>

#pragma STDC FENV_ACCESS ON

#define R_TEST_CHECK(expression)                                                                   \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            (void)fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, #expression); \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int r_test_seed_environment(int rounding, int exceptions, int native_errno) {
    R_TEST_CHECK(fesetround(rounding) == 0);
    R_TEST_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    R_TEST_CHECK(feraiseexcept(exceptions) == 0);
    errno = native_errno;
    return 0;
}

static _Bool r_test_environment_is(int rounding, int exceptions, int native_errno) {
    return (fegetround() == rounding) &&
           ((fetestexcept(FE_ALL_EXCEPT) & FE_ALL_EXCEPT) == exceptions) && (errno == native_errno);
}

static int r_test_environment_matches(int rounding, int exceptions, int native_errno) {
    R_TEST_CHECK(r_test_environment_is(rounding, exceptions, native_errno));
    return 0;
}

static int r_test_native_cbrt_f32(float value, float *result) {
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
    *result = cbrtf(canonical_operand);
    if (fesetenv(&caller_environment) != 0) {
        errno = caller_errno;
        return 1;
    }
    errno = caller_errno;
    return 0;
}

static int r_test_native_cbrt_f64(double value, double *result) {
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
    *result = cbrt(canonical_operand);
    if (fesetenv(&caller_environment) != 0) {
        errno = caller_errno;
        return 1;
    }
    errno = caller_errno;
    return 0;
}

static int r_test_native_cbrt_c_long_double(long double value, long double *result) {
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
    *result = cbrtl(canonical_operand);
    if (fesetenv(&caller_environment) != 0) {
        errno = caller_errno;
        return 1;
    }
    errno = caller_errno;
    return 0;
}

static int r_test_cbrt_f32(void) {
    float native_reference;
    RStdMathF32Result result;

    R_TEST_CHECK(r_test_seed_environment(FE_UPWARD, FE_DIVBYZERO, E2BIG) == 0);

    R_TEST_CHECK(r_test_native_cbrt_f32(2.0F, &native_reference) == 0);
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, E2BIG) == 0);
    result = r_std_math_cbrt_f32(2.0F);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == native_reference);
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, E2BIG) == 0);

    result = r_std_math_cbrt_f32(27.0F);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == 3.0F);
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, E2BIG) == 0);

    result = r_std_math_cbrt_f32(-8.0F);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == -2.0F);
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, E2BIG) == 0);

    result = r_std_math_cbrt_f32(0.0F);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == 0.0F);
    R_TEST_CHECK(!signbit(result.value));
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, E2BIG) == 0);

    result = r_std_math_cbrt_f32(-0.0F);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == 0.0F);
    R_TEST_CHECK(signbit(result.value));
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, E2BIG) == 0);

    result = r_std_math_cbrt_f32(INFINITY);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isinf(result.value));
    R_TEST_CHECK(!signbit(result.value));
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, E2BIG) == 0);

    result = r_std_math_cbrt_f32(-INFINITY);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isinf(result.value));
    R_TEST_CHECK(signbit(result.value));
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, E2BIG) == 0);

    result = r_std_math_cbrt_f32(NAN);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isnan(result.value));
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, E2BIG) == 0);

    result = r_std_math_cbrt_f32(FLT_TRUE_MIN);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isfinite(result.value));
    R_TEST_CHECK(result.value > FLT_TRUE_MIN);
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, E2BIG) == 0);

    result = r_std_math_cbrt_f32(-FLT_TRUE_MIN);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isfinite(result.value));
    R_TEST_CHECK(result.value < -FLT_TRUE_MIN);
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, E2BIG) == 0);

    result = r_std_math_cbrt_f32(FLT_MAX);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isfinite(result.value));
    R_TEST_CHECK(result.value > 0.0F);
    R_TEST_CHECK(result.value < FLT_MAX);
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, E2BIG) == 0);

    result = r_std_math_cbrt_f32(-FLT_MAX);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isfinite(result.value));
    R_TEST_CHECK(result.value < 0.0F);
    R_TEST_CHECK(result.value > -FLT_MAX);
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, E2BIG) == 0);
    return 0;
}

static int r_test_cbrt_f64(void) {
    double native_reference;
    RStdMathF64Result result;

    R_TEST_CHECK(r_test_seed_environment(FE_DOWNWARD, FE_INVALID, EDOM) == 0);

    R_TEST_CHECK(r_test_native_cbrt_f64(2.0, &native_reference) == 0);
    R_TEST_CHECK(r_test_environment_matches(FE_DOWNWARD, FE_INVALID, EDOM) == 0);
    result = r_std_math_cbrt_f64(2.0);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == native_reference);
    R_TEST_CHECK(r_test_environment_matches(FE_DOWNWARD, FE_INVALID, EDOM) == 0);

    result = r_std_math_cbrt_f64(27.0);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == 3.0);
    R_TEST_CHECK(r_test_environment_matches(FE_DOWNWARD, FE_INVALID, EDOM) == 0);

    result = r_std_math_cbrt_f64(-8.0);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == -2.0);
    R_TEST_CHECK(r_test_environment_matches(FE_DOWNWARD, FE_INVALID, EDOM) == 0);

    result = r_std_math_cbrt_f64(0.0);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == 0.0);
    R_TEST_CHECK(!signbit(result.value));
    R_TEST_CHECK(r_test_environment_matches(FE_DOWNWARD, FE_INVALID, EDOM) == 0);

    result = r_std_math_cbrt_f64(-0.0);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == 0.0);
    R_TEST_CHECK(signbit(result.value));
    R_TEST_CHECK(r_test_environment_matches(FE_DOWNWARD, FE_INVALID, EDOM) == 0);

    result = r_std_math_cbrt_f64(INFINITY);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isinf(result.value));
    R_TEST_CHECK(!signbit(result.value));
    R_TEST_CHECK(r_test_environment_matches(FE_DOWNWARD, FE_INVALID, EDOM) == 0);

    result = r_std_math_cbrt_f64(-INFINITY);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isinf(result.value));
    R_TEST_CHECK(signbit(result.value));
    R_TEST_CHECK(r_test_environment_matches(FE_DOWNWARD, FE_INVALID, EDOM) == 0);

    result = r_std_math_cbrt_f64(NAN);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isnan(result.value));
    R_TEST_CHECK(r_test_environment_matches(FE_DOWNWARD, FE_INVALID, EDOM) == 0);

    result = r_std_math_cbrt_f64(DBL_TRUE_MIN);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isfinite(result.value));
    R_TEST_CHECK(result.value > DBL_TRUE_MIN);
    R_TEST_CHECK(r_test_environment_matches(FE_DOWNWARD, FE_INVALID, EDOM) == 0);

    result = r_std_math_cbrt_f64(-DBL_TRUE_MIN);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isfinite(result.value));
    R_TEST_CHECK(result.value < -DBL_TRUE_MIN);
    R_TEST_CHECK(r_test_environment_matches(FE_DOWNWARD, FE_INVALID, EDOM) == 0);

    result = r_std_math_cbrt_f64(DBL_MAX);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isfinite(result.value));
    R_TEST_CHECK(result.value > 0.0);
    R_TEST_CHECK(result.value < DBL_MAX);
    R_TEST_CHECK(r_test_environment_matches(FE_DOWNWARD, FE_INVALID, EDOM) == 0);

    result = r_std_math_cbrt_f64(-DBL_MAX);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isfinite(result.value));
    R_TEST_CHECK(result.value < 0.0);
    R_TEST_CHECK(result.value > -DBL_MAX);
    R_TEST_CHECK(r_test_environment_matches(FE_DOWNWARD, FE_INVALID, EDOM) == 0);
    return 0;
}

static int r_test_cbrt_c_float(void) {
    float native_reference;
    RStdMathCFloatResult result;

    R_TEST_CHECK(r_test_seed_environment(FE_UPWARD, FE_DIVBYZERO, ERANGE) == 0);

    R_TEST_CHECK(r_test_native_cbrt_f32((float)2.0, &native_reference) == 0);
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, ERANGE) == 0);
    result = r_std_math_cbrt_c_float((float)2.0);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == native_reference);
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, ERANGE) == 0);

    result = r_std_math_cbrt_c_float((float)27.0);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == (float)3.0);
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, ERANGE) == 0);

    result = r_std_math_cbrt_c_float((float)-8.0);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == (float)-2.0);
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, ERANGE) == 0);

    result = r_std_math_cbrt_c_float((float)0.0);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == (float)0.0);
    R_TEST_CHECK(!signbit(result.value));
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, ERANGE) == 0);

    result = r_std_math_cbrt_c_float((float)-0.0);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == (float)0.0);
    R_TEST_CHECK(signbit(result.value));
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, ERANGE) == 0);

    result = r_std_math_cbrt_c_float((float)INFINITY);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isinf(result.value));
    R_TEST_CHECK(!signbit(result.value));
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, ERANGE) == 0);

    result = r_std_math_cbrt_c_float((float)-INFINITY);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isinf(result.value));
    R_TEST_CHECK(signbit(result.value));
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, ERANGE) == 0);

    result = r_std_math_cbrt_c_float((float)NAN);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isnan(result.value));
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, ERANGE) == 0);

    result = r_std_math_cbrt_c_float((float)FLT_TRUE_MIN);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isfinite(result.value));
    R_TEST_CHECK(result.value > (float)FLT_TRUE_MIN);
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, ERANGE) == 0);

    result = r_std_math_cbrt_c_float((float)-FLT_TRUE_MIN);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isfinite(result.value));
    R_TEST_CHECK(result.value < (float)-FLT_TRUE_MIN);
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, ERANGE) == 0);

    result = r_std_math_cbrt_c_float((float)FLT_MAX);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isfinite(result.value));
    R_TEST_CHECK(result.value > (float)0.0);
    R_TEST_CHECK(result.value < (float)FLT_MAX);
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, ERANGE) == 0);

    result = r_std_math_cbrt_c_float((float)-FLT_MAX);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isfinite(result.value));
    R_TEST_CHECK(result.value < (float)0.0);
    R_TEST_CHECK(result.value > (float)-FLT_MAX);
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, ERANGE) == 0);
    return 0;
}

static int r_test_cbrt_c_double(void) {
    double native_reference;
    RStdMathCDoubleResult result;

    R_TEST_CHECK(r_test_seed_environment(FE_DOWNWARD, FE_INVALID, EILSEQ) == 0);

    R_TEST_CHECK(r_test_native_cbrt_f64((double)2.0, &native_reference) == 0);
    R_TEST_CHECK(r_test_environment_matches(FE_DOWNWARD, FE_INVALID, EILSEQ) == 0);
    result = r_std_math_cbrt_c_double((double)2.0);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == native_reference);
    R_TEST_CHECK(r_test_environment_matches(FE_DOWNWARD, FE_INVALID, EILSEQ) == 0);

    result = r_std_math_cbrt_c_double((double)27.0);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == (double)3.0);
    R_TEST_CHECK(r_test_environment_matches(FE_DOWNWARD, FE_INVALID, EILSEQ) == 0);

    result = r_std_math_cbrt_c_double((double)-8.0);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == (double)-2.0);
    R_TEST_CHECK(r_test_environment_matches(FE_DOWNWARD, FE_INVALID, EILSEQ) == 0);

    result = r_std_math_cbrt_c_double((double)0.0);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == (double)0.0);
    R_TEST_CHECK(!signbit(result.value));
    R_TEST_CHECK(r_test_environment_matches(FE_DOWNWARD, FE_INVALID, EILSEQ) == 0);

    result = r_std_math_cbrt_c_double((double)-0.0);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == (double)0.0);
    R_TEST_CHECK(signbit(result.value));
    R_TEST_CHECK(r_test_environment_matches(FE_DOWNWARD, FE_INVALID, EILSEQ) == 0);

    result = r_std_math_cbrt_c_double((double)INFINITY);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isinf(result.value));
    R_TEST_CHECK(!signbit(result.value));
    R_TEST_CHECK(r_test_environment_matches(FE_DOWNWARD, FE_INVALID, EILSEQ) == 0);

    result = r_std_math_cbrt_c_double((double)-INFINITY);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isinf(result.value));
    R_TEST_CHECK(signbit(result.value));
    R_TEST_CHECK(r_test_environment_matches(FE_DOWNWARD, FE_INVALID, EILSEQ) == 0);

    result = r_std_math_cbrt_c_double((double)NAN);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isnan(result.value));
    R_TEST_CHECK(r_test_environment_matches(FE_DOWNWARD, FE_INVALID, EILSEQ) == 0);

    result = r_std_math_cbrt_c_double((double)DBL_TRUE_MIN);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isfinite(result.value));
    R_TEST_CHECK(result.value > (double)DBL_TRUE_MIN);
    R_TEST_CHECK(r_test_environment_matches(FE_DOWNWARD, FE_INVALID, EILSEQ) == 0);

    result = r_std_math_cbrt_c_double((double)-DBL_TRUE_MIN);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isfinite(result.value));
    R_TEST_CHECK(result.value < (double)-DBL_TRUE_MIN);
    R_TEST_CHECK(r_test_environment_matches(FE_DOWNWARD, FE_INVALID, EILSEQ) == 0);

    result = r_std_math_cbrt_c_double((double)DBL_MAX);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isfinite(result.value));
    R_TEST_CHECK(result.value > (double)0.0);
    R_TEST_CHECK(result.value < (double)DBL_MAX);
    R_TEST_CHECK(r_test_environment_matches(FE_DOWNWARD, FE_INVALID, EILSEQ) == 0);

    result = r_std_math_cbrt_c_double((double)-DBL_MAX);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isfinite(result.value));
    R_TEST_CHECK(result.value < (double)0.0);
    R_TEST_CHECK(result.value > (double)-DBL_MAX);
    R_TEST_CHECK(r_test_environment_matches(FE_DOWNWARD, FE_INVALID, EILSEQ) == 0);
    return 0;
}

static int r_test_cbrt_c_long_double(void) {
    long double native_reference;
    RStdMathCLongDoubleResult result;

    R_TEST_CHECK(r_test_seed_environment(FE_UPWARD, FE_DIVBYZERO, ENOMEM) == 0);

    R_TEST_CHECK(r_test_native_cbrt_c_long_double((long double)2.0, &native_reference) == 0);
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, ENOMEM) == 0);
    result = r_std_math_cbrt_c_long_double((long double)2.0);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == native_reference);
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, ENOMEM) == 0);

    result = r_std_math_cbrt_c_long_double((long double)27.0);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == (long double)3.0);
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, ENOMEM) == 0);

    result = r_std_math_cbrt_c_long_double((long double)-8.0);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == (long double)-2.0);
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, ENOMEM) == 0);

    result = r_std_math_cbrt_c_long_double((long double)0.0);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == (long double)0.0);
    R_TEST_CHECK(!signbit(result.value));
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, ENOMEM) == 0);

    result = r_std_math_cbrt_c_long_double((long double)-0.0);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == (long double)0.0);
    R_TEST_CHECK(signbit(result.value));
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, ENOMEM) == 0);

    result = r_std_math_cbrt_c_long_double((long double)INFINITY);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isinf(result.value));
    R_TEST_CHECK(!signbit(result.value));
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, ENOMEM) == 0);

    result = r_std_math_cbrt_c_long_double((long double)-INFINITY);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isinf(result.value));
    R_TEST_CHECK(signbit(result.value));
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, ENOMEM) == 0);

    result = r_std_math_cbrt_c_long_double((long double)NAN);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isnan(result.value));
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, ENOMEM) == 0);

    result = r_std_math_cbrt_c_long_double((long double)LDBL_TRUE_MIN);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isfinite(result.value));
    R_TEST_CHECK(result.value > (long double)LDBL_TRUE_MIN);
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, ENOMEM) == 0);

    result = r_std_math_cbrt_c_long_double((long double)-LDBL_TRUE_MIN);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isfinite(result.value));
    R_TEST_CHECK(result.value < (long double)-LDBL_TRUE_MIN);
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, ENOMEM) == 0);

    result = r_std_math_cbrt_c_long_double((long double)LDBL_MAX);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isfinite(result.value));
    R_TEST_CHECK(result.value > (long double)0.0);
    R_TEST_CHECK(result.value < (long double)LDBL_MAX);
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, ENOMEM) == 0);

    result = r_std_math_cbrt_c_long_double((long double)-LDBL_MAX);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isfinite(result.value));
    R_TEST_CHECK(result.value < (long double)0.0);
    R_TEST_CHECK(result.value > (long double)-LDBL_MAX);
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, ENOMEM) == 0);
    return 0;
}

typedef struct RTestCbrtThreadContext {
    int rounding;
    int exceptions;
    int native_errno;
    int failed;
} RTestCbrtThreadContext;

static void *r_test_cbrt_thread_main(void *opaque_context) {
    RTestCbrtThreadContext *context = opaque_context;
    int iteration;

    if (r_test_seed_environment(context->rounding, context->exceptions, context->native_errno) !=
        0) {
        context->failed = 1;
        return NULL;
    }
    for (iteration = 0; iteration < 500; ++iteration) {
        RStdMathF32Result f32 = r_std_math_cbrt_f32(8.0F);
        RStdMathF64Result f64 = r_std_math_cbrt_f64(-27.0);
        RStdMathCFloatResult c_float = r_std_math_cbrt_c_float((float)64.0);
        RStdMathCDoubleResult c_double = r_std_math_cbrt_c_double((double)-125.0);
        RStdMathCLongDoubleResult c_long_double = r_std_math_cbrt_c_long_double((long double)216.0);

        if ((f32.status != R_STD_MATH_CALL_SUCCESS) || (f32.value != 2.0F) ||
            (f64.status != R_STD_MATH_CALL_SUCCESS) || (f64.value != -3.0) ||
            (c_float.status != R_STD_MATH_CALL_SUCCESS) || (c_float.value != (float)4.0) ||
            (c_double.status != R_STD_MATH_CALL_SUCCESS) || (c_double.value != (double)-5.0) ||
            (c_long_double.status != R_STD_MATH_CALL_SUCCESS) ||
            (c_long_double.value != (long double)6.0) ||
            !r_test_environment_is(context->rounding, context->exceptions, context->native_errno)) {
            context->failed = 1;
            return NULL;
        }
    }
    return NULL;
}

static int r_test_concurrent_environments(void) {
    pthread_t first_thread;
    pthread_t second_thread;
    RTestCbrtThreadContext first = {FE_DOWNWARD, FE_DIVBYZERO, EDOM, 0};
    RTestCbrtThreadContext second = {FE_UPWARD, FE_INVALID, ERANGE, 0};

    R_TEST_CHECK(pthread_create(&first_thread, NULL, r_test_cbrt_thread_main, &first) == 0);
    R_TEST_CHECK(pthread_create(&second_thread, NULL, r_test_cbrt_thread_main, &second) == 0);
    R_TEST_CHECK(pthread_join(first_thread, NULL) == 0);
    R_TEST_CHECK(pthread_join(second_thread, NULL) == 0);
    R_TEST_CHECK(first.failed == 0);
    R_TEST_CHECK(second.failed == 0);
    return 0;
}

int main(void) {
    int original_errno = errno;
    fenv_t original_environment;

    R_TEST_CHECK(fegetenv(&original_environment) == 0);
    R_TEST_CHECK(r_test_cbrt_f32() == 0);
    R_TEST_CHECK(r_test_cbrt_f64() == 0);
    R_TEST_CHECK(r_test_cbrt_c_float() == 0);
    R_TEST_CHECK(r_test_cbrt_c_double() == 0);
    R_TEST_CHECK(r_test_cbrt_c_long_double() == 0);
    R_TEST_CHECK(r_test_concurrent_environments() == 0);
    R_TEST_CHECK(fesetenv(&original_environment) == 0);
    errno = original_errno;
    (void)fprintf(stdout, "library_math_cbrt_tests: ok\n");
    return 0;
}

#include "r_std_math.h"

#include <errno.h>
#include <fenv.h>
#include <math.h>
#include <stdint.h>
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

static int r_test_environment_matches(int rounding, int exceptions, int native_errno) {
    R_TEST_CHECK(fegetround() == rounding);
    R_TEST_CHECK((fetestexcept(FE_ALL_EXCEPT) & FE_ALL_EXCEPT) == exceptions);
    R_TEST_CHECK(errno == native_errno);
    return 0;
}

static int r_test_sqrt_f32(void) {
    RStdMathF32Result result;

    R_TEST_CHECK(r_test_seed_environment(FE_UPWARD, FE_DIVBYZERO, E2BIG) == 0);

    result = r_std_math_sqrt_f32(2.0F);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, E2BIG) == 0);
    R_TEST_CHECK(result.value == 0x1.6a09e6p+0F);

    result = r_std_math_sqrt_f32(-1.0F);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_DOMAIN);
    R_TEST_CHECK(result.error.native_code == INT64_C(0));
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, E2BIG) == 0);

    result = r_std_math_sqrt_f32(-INFINITY);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_DOMAIN);
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, E2BIG) == 0);

    result = r_std_math_sqrt_f32(-0.0F);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, E2BIG) == 0);
    R_TEST_CHECK(signbit(result.value));

    result = r_std_math_sqrt_f32(NAN);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, E2BIG) == 0);
    R_TEST_CHECK(isnan(result.value));

    result = r_std_math_sqrt_f32(INFINITY);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, E2BIG) == 0);
    R_TEST_CHECK(isinf(result.value));
    R_TEST_CHECK(!signbit(result.value));
    return 0;
}

static int r_test_sqrt_f64(void) {
    RStdMathF64Result result;

    R_TEST_CHECK(r_test_seed_environment(FE_DOWNWARD, FE_INVALID, EDOM) == 0);

    result = r_std_math_sqrt_f64(2.0);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_environment_matches(FE_DOWNWARD, FE_INVALID, EDOM) == 0);
    R_TEST_CHECK(result.value == 0x1.6a09e667f3bcdp+0);

    result = r_std_math_sqrt_f64(-1.0);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_DOMAIN);
    R_TEST_CHECK(result.error.native_code == INT64_C(0));
    R_TEST_CHECK(r_test_environment_matches(FE_DOWNWARD, FE_INVALID, EDOM) == 0);

    result = r_std_math_sqrt_f64(-INFINITY);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_DOMAIN);
    R_TEST_CHECK(r_test_environment_matches(FE_DOWNWARD, FE_INVALID, EDOM) == 0);

    result = r_std_math_sqrt_f64(-0.0);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_environment_matches(FE_DOWNWARD, FE_INVALID, EDOM) == 0);
    R_TEST_CHECK(signbit(result.value));

    result = r_std_math_sqrt_f64(NAN);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_environment_matches(FE_DOWNWARD, FE_INVALID, EDOM) == 0);
    R_TEST_CHECK(isnan(result.value));

    result = r_std_math_sqrt_f64(INFINITY);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_environment_matches(FE_DOWNWARD, FE_INVALID, EDOM) == 0);
    R_TEST_CHECK(isinf(result.value));
    R_TEST_CHECK(!signbit(result.value));
    return 0;
}

static int r_test_sqrt_c_float(void) {
    RStdMathCFloatResult result;

    R_TEST_CHECK(r_test_seed_environment(FE_UPWARD, FE_DIVBYZERO, ERANGE) == 0);

    result = r_std_math_sqrt_c_float((float)2.0);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, ERANGE) == 0);
    R_TEST_CHECK(result.value == (float)0x1.6a09e6p+0);

    result = r_std_math_sqrt_c_float((float)-1.0);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_DOMAIN);
    R_TEST_CHECK(result.error.native_code == INT64_C(0));
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, ERANGE) == 0);

    result = r_std_math_sqrt_c_float((float)-INFINITY);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_DOMAIN);
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, ERANGE) == 0);

    result = r_std_math_sqrt_c_float((float)-0.0);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, ERANGE) == 0);
    R_TEST_CHECK(signbit(result.value));

    result = r_std_math_sqrt_c_float((float)NAN);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, ERANGE) == 0);
    R_TEST_CHECK(isnan(result.value));

    result = r_std_math_sqrt_c_float((float)INFINITY);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, ERANGE) == 0);
    R_TEST_CHECK(isinf(result.value));
    R_TEST_CHECK(!signbit(result.value));
    return 0;
}

static int r_test_sqrt_c_double(void) {
    RStdMathCDoubleResult result;

    R_TEST_CHECK(r_test_seed_environment(FE_DOWNWARD, FE_INVALID, EILSEQ) == 0);

    result = r_std_math_sqrt_c_double((double)2.0);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_environment_matches(FE_DOWNWARD, FE_INVALID, EILSEQ) == 0);
    R_TEST_CHECK(result.value == (double)0x1.6a09e667f3bcdp+0);

    result = r_std_math_sqrt_c_double((double)-1.0);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_DOMAIN);
    R_TEST_CHECK(result.error.native_code == INT64_C(0));
    R_TEST_CHECK(r_test_environment_matches(FE_DOWNWARD, FE_INVALID, EILSEQ) == 0);

    result = r_std_math_sqrt_c_double((double)-INFINITY);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_DOMAIN);
    R_TEST_CHECK(r_test_environment_matches(FE_DOWNWARD, FE_INVALID, EILSEQ) == 0);

    result = r_std_math_sqrt_c_double((double)-0.0);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_environment_matches(FE_DOWNWARD, FE_INVALID, EILSEQ) == 0);
    R_TEST_CHECK(signbit(result.value));

    result = r_std_math_sqrt_c_double((double)NAN);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_environment_matches(FE_DOWNWARD, FE_INVALID, EILSEQ) == 0);
    R_TEST_CHECK(isnan(result.value));

    result = r_std_math_sqrt_c_double((double)INFINITY);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_environment_matches(FE_DOWNWARD, FE_INVALID, EILSEQ) == 0);
    R_TEST_CHECK(isinf(result.value));
    R_TEST_CHECK(!signbit(result.value));
    return 0;
}

static int r_test_sqrt_c_long_double(void) {
    RStdMathCLongDoubleResult result;

    R_TEST_CHECK(r_test_seed_environment(FE_UPWARD, FE_DIVBYZERO, ENOMEM) == 0);

    result = r_std_math_sqrt_c_long_double((long double)2.0);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, ENOMEM) == 0);
    R_TEST_CHECK(result.value == (long double)0x1.6a09e667f3bcdp+0);

    result = r_std_math_sqrt_c_long_double((long double)-1.0);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_DOMAIN);
    R_TEST_CHECK(result.error.native_code == INT64_C(0));
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, ENOMEM) == 0);

    result = r_std_math_sqrt_c_long_double((long double)-INFINITY);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_DOMAIN);
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, ENOMEM) == 0);

    result = r_std_math_sqrt_c_long_double((long double)-0.0);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, ENOMEM) == 0);
    R_TEST_CHECK(signbit(result.value));

    result = r_std_math_sqrt_c_long_double((long double)NAN);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, ENOMEM) == 0);
    R_TEST_CHECK(isnan(result.value));

    result = r_std_math_sqrt_c_long_double((long double)INFINITY);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_environment_matches(FE_UPWARD, FE_DIVBYZERO, ENOMEM) == 0);
    R_TEST_CHECK(isinf(result.value));
    R_TEST_CHECK(!signbit(result.value));
    return 0;
}

int main(void) {
    int original_errno = errno;
    fenv_t original_environment;

    R_TEST_CHECK(fegetenv(&original_environment) == 0);
    R_TEST_CHECK(r_test_sqrt_f32() == 0);
    R_TEST_CHECK(r_test_sqrt_f64() == 0);
    R_TEST_CHECK(r_test_sqrt_c_float() == 0);
    R_TEST_CHECK(r_test_sqrt_c_double() == 0);
    R_TEST_CHECK(r_test_sqrt_c_long_double() == 0);
    R_TEST_CHECK(fesetenv(&original_environment) == 0);
    errno = original_errno;
    (void)fprintf(stdout, "library_math_sqrt_tests: ok\n");
    return 0;
}

#include "r_std_math.h"

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

_Static_assert(LDBL_MANT_DIG == 53, "target long double uses a binary64 significand");
_Static_assert(LDBL_MAX_EXP == 1024, "target long double uses a binary64 exponent");
_Static_assert(sizeof(long double) == sizeof(uint64_t),
               "arm64-apple-darwin long double must use binary64 storage");

typedef struct RTestFloatResult {
    RStdMathCallStatus status;
    float value;
    RStdMathError error;
} RTestFloatResult;

typedef struct RTestDoubleResult {
    RStdMathCallStatus status;
    double value;
    RStdMathError error;
} RTestDoubleResult;

typedef struct RTestLongDoubleResult {
    RStdMathCallStatus status;
    long double value;
    RStdMathError error;
} RTestLongDoubleResult;

typedef RTestFloatResult (*RTestFloatOperation)(float value);
typedef RTestDoubleResult (*RTestDoubleOperation)(double value);
typedef RTestLongDoubleResult (*RTestLongDoubleOperation)(long double value);

typedef struct RTestLog10ThreadContext {
    int rounding;
    int exceptions;
    int native_errno;
    int failed;
} RTestLog10ThreadContext;

static RTestFloatResult r_test_log10_f32(float value) {
    RStdMathF32Result public_result = r_std_math_log10_f32(value);
    RTestFloatResult result = {public_result.status, public_result.value, public_result.error};

    return result;
}

static RTestFloatResult r_test_log10_c_float(float value) {
    RStdMathCFloatResult public_result = r_std_math_log10_c_float(value);
    RTestFloatResult result = {public_result.status, public_result.value, public_result.error};

    return result;
}

static RTestDoubleResult r_test_log10_f64(double value) {
    RStdMathF64Result public_result = r_std_math_log10_f64(value);
    RTestDoubleResult result = {public_result.status, public_result.value, public_result.error};

    return result;
}

static RTestDoubleResult r_test_log10_c_double(double value) {
    RStdMathCDoubleResult public_result = r_std_math_log10_c_double(value);
    RTestDoubleResult result = {public_result.status, public_result.value, public_result.error};

    return result;
}

static RTestLongDoubleResult r_test_log10_c_long_double(long double value) {
    RStdMathCLongDoubleResult public_result = r_std_math_log10_c_long_double(value);
    RTestLongDoubleResult result = {public_result.status, public_result.value, public_result.error};

    return result;
}

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

static _Bool r_test_same_f32(float left, float right) {
    uint32_t left_bits;
    uint32_t right_bits;

    (void)memcpy(&left_bits, &left, sizeof(left_bits));
    (void)memcpy(&right_bits, &right, sizeof(right_bits));
    return left_bits == right_bits;
}

static _Bool r_test_same_f64(double left, double right) {
    uint64_t left_bits;
    uint64_t right_bits;

    (void)memcpy(&left_bits, &left, sizeof(left_bits));
    (void)memcpy(&right_bits, &right, sizeof(right_bits));
    return left_bits == right_bits;
}

static _Bool r_test_same_c_long_double(long double left, long double right) {
    uint64_t left_bits;
    uint64_t right_bits;

    (void)memcpy(&left_bits, &left, sizeof(left_bits));
    (void)memcpy(&right_bits, &right, sizeof(right_bits));
    return left_bits == right_bits;
}

static int r_test_native_f32(float value, float *result, int *native_errno) {
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
    *result = log10f(canonical_operand);
    *native_errno = errno;
    if (fesetenv(&caller_environment) != 0) {
        errno = caller_errno;
        return 1;
    }
    errno = caller_errno;
    return 0;
}

static int r_test_native_f64(double value, double *result, int *native_errno) {
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
    *result = log10(canonical_operand);
    *native_errno = errno;
    if (fesetenv(&caller_environment) != 0) {
        errno = caller_errno;
        return 1;
    }
    errno = caller_errno;
    return 0;
}

static int r_test_native_c_long_double(long double value, long double *result, int *native_errno) {
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
    *result = log10l(canonical_operand);
    *native_errno = errno;
    if (fesetenv(&caller_environment) != 0) {
        errno = caller_errno;
        return 1;
    }
    errno = caller_errno;
    return 0;
}

static int r_test_float_operation(RTestFloatOperation operation) {
    static const float success_inputs[] = {1.0F, 2.0F, FLT_TRUE_MIN, INFINITY};
    size_t index;

    for (index = 0U; index < sizeof(success_inputs) / sizeof(success_inputs[0]); ++index) {
        float expected;
        int native_errno;
        RTestFloatResult result;

        R_TEST_CHECK(r_test_native_f32(success_inputs[index], &expected, &native_errno) == 0);
        result = operation(success_inputs[index]);
        R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
        R_TEST_CHECK(r_test_same_f32(result.value, expected));
    }
    {
        float expected;
        int native_errno;
        RTestFloatResult result;

        R_TEST_CHECK(r_test_native_f32(-1.0F, &expected, &native_errno) == 0);
        result = operation(-1.0F);
        R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
        R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_DOMAIN);
        R_TEST_CHECK(result.error.native_code == (int64_t)native_errno);
        R_TEST_CHECK(r_test_native_f32(-INFINITY, &expected, &native_errno) == 0);
        result = operation(-INFINITY);
        R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
        R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_DOMAIN);
        R_TEST_CHECK(result.error.native_code == (int64_t)native_errno);
        R_TEST_CHECK(r_test_native_f32(-0.0F, &expected, &native_errno) == 0);
        result = operation(-0.0F);
        R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
        R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_POLE);
        R_TEST_CHECK(result.error.native_code == (int64_t)native_errno);
        R_TEST_CHECK(r_test_native_f32(0.0F, &expected, &native_errno) == 0);
        result = operation(0.0F);
        R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
        R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_POLE);
        R_TEST_CHECK(result.error.native_code == (int64_t)native_errno);
        result = operation(NAN);
        R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
        R_TEST_CHECK(isnan(result.value));
        result = operation(r_test_signaling_nan_f32());
        R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
        R_TEST_CHECK(isnan(result.value));
    }
    return 0;
}

static int r_test_double_operation(RTestDoubleOperation operation) {
    static const double success_inputs[] = {1.0, 2.0, DBL_TRUE_MIN, INFINITY};
    size_t index;

    for (index = 0U; index < sizeof(success_inputs) / sizeof(success_inputs[0]); ++index) {
        double expected;
        int native_errno;
        RTestDoubleResult result;

        R_TEST_CHECK(r_test_native_f64(success_inputs[index], &expected, &native_errno) == 0);
        result = operation(success_inputs[index]);
        R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
        R_TEST_CHECK(r_test_same_f64(result.value, expected));
    }
    {
        double expected;
        int native_errno;
        RTestDoubleResult result;

        R_TEST_CHECK(r_test_native_f64(-1.0, &expected, &native_errno) == 0);
        result = operation(-1.0);
        R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
        R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_DOMAIN);
        R_TEST_CHECK(result.error.native_code == (int64_t)native_errno);
        R_TEST_CHECK(r_test_native_f64(-INFINITY, &expected, &native_errno) == 0);
        result = operation(-INFINITY);
        R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
        R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_DOMAIN);
        R_TEST_CHECK(result.error.native_code == (int64_t)native_errno);
        R_TEST_CHECK(r_test_native_f64(-0.0, &expected, &native_errno) == 0);
        result = operation(-0.0);
        R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
        R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_POLE);
        R_TEST_CHECK(result.error.native_code == (int64_t)native_errno);
        R_TEST_CHECK(r_test_native_f64(0.0, &expected, &native_errno) == 0);
        result = operation(0.0);
        R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
        R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_POLE);
        R_TEST_CHECK(result.error.native_code == (int64_t)native_errno);
        result = operation(NAN);
        R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
        R_TEST_CHECK(isnan(result.value));
        result = operation(r_test_signaling_nan_f64());
        R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
        R_TEST_CHECK(isnan(result.value));
    }
    return 0;
}

static int r_test_long_double_operation(RTestLongDoubleOperation operation) {
    static const long double success_inputs[] = {1.0L, 2.0L, LDBL_TRUE_MIN, INFINITY};
    size_t index;

    for (index = 0U; index < sizeof(success_inputs) / sizeof(success_inputs[0]); ++index) {
        long double expected;
        int native_errno;
        RTestLongDoubleResult result;

        R_TEST_CHECK(r_test_native_c_long_double(success_inputs[index], &expected, &native_errno) ==
                     0);
        result = operation(success_inputs[index]);
        R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
        R_TEST_CHECK(r_test_same_c_long_double(result.value, expected));
    }
    {
        long double expected;
        int native_errno;
        RTestLongDoubleResult result;

        R_TEST_CHECK(r_test_native_c_long_double(-1.0L, &expected, &native_errno) == 0);
        result = operation(-1.0L);
        R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
        R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_DOMAIN);
        R_TEST_CHECK(result.error.native_code == (int64_t)native_errno);
        R_TEST_CHECK(r_test_native_c_long_double(-INFINITY, &expected, &native_errno) == 0);
        result = operation(-INFINITY);
        R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
        R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_DOMAIN);
        R_TEST_CHECK(result.error.native_code == (int64_t)native_errno);
        R_TEST_CHECK(r_test_native_c_long_double(-0.0L, &expected, &native_errno) == 0);
        result = operation(-0.0L);
        R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
        R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_POLE);
        R_TEST_CHECK(result.error.native_code == (int64_t)native_errno);
        R_TEST_CHECK(r_test_native_c_long_double(0.0L, &expected, &native_errno) == 0);
        result = operation(0.L);
        R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
        R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_POLE);
        R_TEST_CHECK(result.error.native_code == (int64_t)native_errno);
        result = operation(NAN);
        R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
        R_TEST_CHECK(isnan(result.value));
        result = operation(r_test_signaling_nan_c_long_double());
        R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
        R_TEST_CHECK(isnan(result.value));
    }
    return 0;
}

static _Bool r_test_environment_is(int rounding, int exceptions, int native_errno) {
    return (fegetround() == rounding) &&
           ((fetestexcept(FE_ALL_EXCEPT) & FE_ALL_EXCEPT) == exceptions) && (errno == native_errno);
}

static void *r_test_log10_thread_main(void *opaque_context) {
    RTestLog10ThreadContext *context = opaque_context;
    int iteration;

    if ((fesetround(context->rounding) != 0) || (feclearexcept(FE_ALL_EXCEPT) != 0) ||
        (feraiseexcept(context->exceptions) != 0)) {
        context->failed = 1;
        return NULL;
    }
    errno = context->native_errno;
    for (iteration = 0; iteration < 500; ++iteration) {
        RStdMathF32Result f32_result = r_std_math_log10_f32(2.0F);
        RStdMathF64Result f64_result = r_std_math_log10_f64(-1.0);
        RStdMathCFloatResult c_float_result = r_std_math_log10_c_float(0.0F);
        RStdMathCDoubleResult c_double_result = r_std_math_log10_c_double(INFINITY);
        RStdMathCLongDoubleResult c_long_double_result =
            r_std_math_log10_c_long_double(LDBL_TRUE_MIN);

        if ((f32_result.status != R_STD_MATH_CALL_SUCCESS) ||
            (f64_result.status != R_STD_MATH_CALL_ERROR) ||
            (f64_result.error.code != R_STD_MATH_ERROR_DOMAIN) ||
            (c_float_result.status != R_STD_MATH_CALL_ERROR) ||
            (c_float_result.error.code != R_STD_MATH_ERROR_POLE) ||
            (c_double_result.status != R_STD_MATH_CALL_SUCCESS) || !isinf(c_double_result.value) ||
            (c_long_double_result.status != R_STD_MATH_CALL_SUCCESS) ||
            !r_test_environment_is(context->rounding, context->exceptions, context->native_errno)) {
            context->failed = 1;
            return NULL;
        }
    }
    return NULL;
}

static int r_test_environment_and_threads(void) {
    int original_rounding = fegetround();
    pthread_t first_thread;
    pthread_t second_thread;
    RTestLog10ThreadContext first = {FE_DOWNWARD, FE_DIVBYZERO, EDOM, 0};
    RTestLog10ThreadContext second = {FE_UPWARD, FE_INVALID, ERANGE, 0};

    R_TEST_CHECK(original_rounding != -1);
    R_TEST_CHECK(fesetround(FE_TOWARDZERO) == 0);
    R_TEST_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    R_TEST_CHECK(feraiseexcept(FE_DIVBYZERO) == 0);
    errno = E2BIG;
    R_TEST_CHECK(r_std_math_log10_f32(-1.0F).status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(r_std_math_log10_f64(0.0).status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(r_std_math_log10_c_float(2.0F).status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_std_math_log10_c_double(NAN).status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_std_math_log10_c_long_double(INFINITY).status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_environment_is(FE_TOWARDZERO, FE_DIVBYZERO, E2BIG));

    R_TEST_CHECK(pthread_create(&first_thread, NULL, r_test_log10_thread_main, &first) == 0);
    R_TEST_CHECK(pthread_create(&second_thread, NULL, r_test_log10_thread_main, &second) == 0);
    R_TEST_CHECK(pthread_join(first_thread, NULL) == 0);
    R_TEST_CHECK(pthread_join(second_thread, NULL) == 0);
    R_TEST_CHECK(first.failed == 0);
    R_TEST_CHECK(second.failed == 0);
    R_TEST_CHECK(r_test_environment_is(FE_TOWARDZERO, FE_DIVBYZERO, E2BIG));
    R_TEST_CHECK(fesetround(original_rounding) == 0);
    R_TEST_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    errno = 0;
    return 0;
}

int main(void) {
    R_TEST_CHECK(r_test_float_operation(r_test_log10_f32) == 0);
    R_TEST_CHECK(r_test_float_operation(r_test_log10_c_float) == 0);
    R_TEST_CHECK(r_test_double_operation(r_test_log10_f64) == 0);
    R_TEST_CHECK(r_test_double_operation(r_test_log10_c_double) == 0);
    R_TEST_CHECK(r_test_long_double_operation(r_test_log10_c_long_double) == 0);
    R_TEST_CHECK(r_test_environment_and_threads() == 0);
    return 0;
}

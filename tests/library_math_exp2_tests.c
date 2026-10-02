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

typedef struct RTestExp2ThreadContext {
    int rounding;
    int exceptions;
    int native_errno;
    float expected_float;
    double expected_double;
    long double expected_long_double;
    int failed;
} RTestExp2ThreadContext;

static RTestFloatResult r_test_exp2_f32(float value) {
    RStdMathF32Result public_result = r_std_math_exp2_f32(value);
    RTestFloatResult result = {public_result.status, public_result.value, public_result.error};

    return result;
}

static RTestDoubleResult r_test_exp2_f64(double value) {
    RStdMathF64Result public_result = r_std_math_exp2_f64(value);
    RTestDoubleResult result = {public_result.status, public_result.value, public_result.error};

    return result;
}

static RTestFloatResult r_test_exp2_c_float(float value) {
    RStdMathCFloatResult public_result = r_std_math_exp2_c_float(value);
    RTestFloatResult result = {public_result.status, public_result.value, public_result.error};

    return result;
}

static RTestDoubleResult r_test_exp2_c_double(double value) {
    RStdMathCDoubleResult public_result = r_std_math_exp2_c_double(value);
    RTestDoubleResult result = {public_result.status, public_result.value, public_result.error};

    return result;
}

static RTestLongDoubleResult r_test_exp2_c_long_double(long double value) {
    RStdMathCLongDoubleResult public_result = r_std_math_exp2_c_long_double(value);
    RTestLongDoubleResult result = {public_result.status, public_result.value, public_result.error};

    return result;
}

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

static float r_test_signaling_nan_f32(void) {
    return r_test_f32_from_bits(UINT32_C(0x7f800001));
}

static double r_test_signaling_nan_f64(void) {
    return r_test_f64_from_bits(UINT64_C(0x7ff0000000000001));
}

static long double r_test_signaling_nan_c_long_double(void) {
    return r_test_c_long_double_from_bits(UINT64_C(0x7ff0000000000001));
}

static _Bool r_test_is_quiet_nan_f32(float value) {
    uint32_t bits;

    (void)memcpy(&bits, &value, sizeof(bits));
    return isnan(value) && ((bits & UINT32_C(0x00400000)) != 0U);
}

static _Bool r_test_is_quiet_nan_f64(double value) {
    uint64_t bits;

    (void)memcpy(&bits, &value, sizeof(bits));
    return isnan(value) && ((bits & UINT64_C(0x0008000000000000)) != 0U);
}

static _Bool r_test_is_quiet_nan_c_long_double(long double value) {
    uint64_t bits;

    (void)memcpy(&bits, &value, sizeof(bits));
    return isnan(value) && ((bits & UINT64_C(0x0008000000000000)) != 0U);
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

static int r_test_native_f32(float value, float *result, int *native_errno, int *exceptions) {
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
    *result = exp2f(canonical_operand);
    if (native_errno != NULL) {
        *native_errno = errno;
    }
    if (exceptions != NULL) {
        *exceptions = fetestexcept(FE_ALL_EXCEPT);
    }
    if (fesetenv(&caller_environment) != 0) {
        errno = caller_errno;
        return 1;
    }
    errno = caller_errno;
    return 0;
}

static int r_test_native_f64(double value, double *result, int *native_errno, int *exceptions) {
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
    *result = exp2(canonical_operand);
    if (native_errno != NULL) {
        *native_errno = errno;
    }
    if (exceptions != NULL) {
        *exceptions = fetestexcept(FE_ALL_EXCEPT);
    }
    if (fesetenv(&caller_environment) != 0) {
        errno = caller_errno;
        return 1;
    }
    errno = caller_errno;
    return 0;
}

static int r_test_native_c_long_double(long double value,
                                       long double *result,
                                       int *native_errno,
                                       int *exceptions) {
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
    *result = exp2l(canonical_operand);
    if (native_errno != NULL) {
        *native_errno = errno;
    }
    if (exceptions != NULL) {
        *exceptions = fetestexcept(FE_ALL_EXCEPT);
    }
    if (fesetenv(&caller_environment) != 0) {
        errno = caller_errno;
        return 1;
    }
    errno = caller_errno;
    return 0;
}

static int
r_test_float_success(RTestFloatOperation operation, float value, int expected_classification) {
    float expected;
    RTestFloatResult result;

    R_TEST_CHECK(r_test_native_f32(value, &expected, NULL, NULL) == 0);
    R_TEST_CHECK(fpclassify(expected) == expected_classification);
    result = operation(value);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_same_f32(result.value, expected));
    return 0;
}

static int
r_test_double_success(RTestDoubleOperation operation, double value, int expected_classification) {
    double expected;
    RTestDoubleResult result;

    R_TEST_CHECK(r_test_native_f64(value, &expected, NULL, NULL) == 0);
    R_TEST_CHECK(fpclassify(expected) == expected_classification);
    result = operation(value);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_same_f64(result.value, expected));
    return 0;
}

static int r_test_long_double_success(RTestLongDoubleOperation operation,
                                      long double value,
                                      int expected_classification) {
    long double expected;
    RTestLongDoubleResult result;

    R_TEST_CHECK(r_test_native_c_long_double(value, &expected, NULL, NULL) == 0);
    R_TEST_CHECK(fpclassify(expected) == expected_classification);
    result = operation(value);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_same_c_long_double(result.value, expected));
    return 0;
}

static int r_test_float_error(RTestFloatOperation operation,
                              float value,
                              RStdMathErrorCode expected_code,
                              int expected_classification) {
    float native_result;
    int native_errno;
    RTestFloatResult result;

    R_TEST_CHECK(r_test_native_f32(value, &native_result, &native_errno, NULL) == 0);
    R_TEST_CHECK(fpclassify(native_result) == expected_classification);
    result = operation(value);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == expected_code);
    R_TEST_CHECK(result.error.native_code == (int64_t)native_errno);
    return 0;
}

static int r_test_double_error(RTestDoubleOperation operation,
                               double value,
                               RStdMathErrorCode expected_code,
                               int expected_classification) {
    double native_result;
    int native_errno;
    RTestDoubleResult result;

    R_TEST_CHECK(r_test_native_f64(value, &native_result, &native_errno, NULL) == 0);
    R_TEST_CHECK(fpclassify(native_result) == expected_classification);
    result = operation(value);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == expected_code);
    R_TEST_CHECK(result.error.native_code == (int64_t)native_errno);
    return 0;
}

static int r_test_long_double_error(RTestLongDoubleOperation operation,
                                    long double value,
                                    RStdMathErrorCode expected_code,
                                    int expected_classification) {
    long double native_result;
    int native_errno;
    RTestLongDoubleResult result;

    R_TEST_CHECK(r_test_native_c_long_double(value, &native_result, &native_errno, NULL) == 0);
    R_TEST_CHECK(fpclassify(native_result) == expected_classification);
    result = operation(value);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == expected_code);
    R_TEST_CHECK(result.error.native_code == (int64_t)native_errno);
    return 0;
}

static int r_test_regular_and_infinity_values(void) {
    R_TEST_CHECK(r_test_float_success(r_test_exp2_f32, -10.0F, FP_NORMAL) == 0);
    R_TEST_CHECK(r_test_float_success(r_test_exp2_c_float, -0.0F, FP_NORMAL) == 0);
    R_TEST_CHECK(r_test_float_success(r_test_exp2_f32, 0.0F, FP_NORMAL) == 0);
    R_TEST_CHECK(r_test_float_success(r_test_exp2_c_float, 10.0F, FP_NORMAL) == 0);
    R_TEST_CHECK(r_test_float_success(r_test_exp2_f32, 127.0F, FP_NORMAL) == 0);
    R_TEST_CHECK(r_test_double_success(r_test_exp2_f64, -10.0, FP_NORMAL) == 0);
    R_TEST_CHECK(r_test_double_success(r_test_exp2_c_double, -0.0, FP_NORMAL) == 0);
    R_TEST_CHECK(r_test_double_success(r_test_exp2_f64, 0.0, FP_NORMAL) == 0);
    R_TEST_CHECK(r_test_double_success(r_test_exp2_c_double, 10.0, FP_NORMAL) == 0);
    R_TEST_CHECK(r_test_double_success(r_test_exp2_f64, 1023.0, FP_NORMAL) == 0);
    R_TEST_CHECK(r_test_long_double_success(r_test_exp2_c_long_double, -10.0L, FP_NORMAL) == 0);
    R_TEST_CHECK(r_test_long_double_success(r_test_exp2_c_long_double, 1023.0L, FP_NORMAL) == 0);
    R_TEST_CHECK(r_test_float_success(r_test_exp2_f32, INFINITY, FP_INFINITE) == 0);
    R_TEST_CHECK(r_test_float_success(r_test_exp2_c_float, -INFINITY, FP_ZERO) == 0);
    R_TEST_CHECK(r_test_double_success(r_test_exp2_f64, INFINITY, FP_INFINITE) == 0);
    R_TEST_CHECK(r_test_double_success(r_test_exp2_c_double, -INFINITY, FP_ZERO) == 0);
    R_TEST_CHECK(r_test_long_double_success(r_test_exp2_c_long_double, INFINITY, FP_INFINITE) == 0);
    R_TEST_CHECK(r_test_long_double_success(r_test_exp2_c_long_double, -INFINITY, FP_ZERO) == 0);
    return 0;
}

static int r_test_nan(void) {
    RTestFloatResult f32_quiet = r_test_exp2_f32(NAN);
    RTestFloatResult f32_signaling = r_test_exp2_f32(r_test_signaling_nan_f32());
    RTestFloatResult c_float_quiet = r_test_exp2_c_float(NAN);
    RTestFloatResult c_float_signaling = r_test_exp2_c_float(r_test_signaling_nan_f32());
    RTestDoubleResult f64_quiet = r_test_exp2_f64(NAN);
    RTestDoubleResult f64_signaling = r_test_exp2_f64(r_test_signaling_nan_f64());
    RTestDoubleResult c_double_quiet = r_test_exp2_c_double(NAN);
    RTestDoubleResult c_double_signaling = r_test_exp2_c_double(r_test_signaling_nan_f64());
    RTestLongDoubleResult c_long_double_quiet = r_test_exp2_c_long_double(NAN);
    RTestLongDoubleResult c_long_double_signaling =
        r_test_exp2_c_long_double(r_test_signaling_nan_c_long_double());

    R_TEST_CHECK(f32_quiet.status == R_STD_MATH_CALL_SUCCESS && isnan(f32_quiet.value));
    R_TEST_CHECK(f32_signaling.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_is_quiet_nan_f32(f32_signaling.value));
    R_TEST_CHECK(c_float_quiet.status == R_STD_MATH_CALL_SUCCESS && isnan(c_float_quiet.value));
    R_TEST_CHECK(c_float_signaling.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_is_quiet_nan_f32(c_float_signaling.value));
    R_TEST_CHECK(f64_quiet.status == R_STD_MATH_CALL_SUCCESS && isnan(f64_quiet.value));
    R_TEST_CHECK(f64_signaling.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_is_quiet_nan_f64(f64_signaling.value));
    R_TEST_CHECK(c_double_quiet.status == R_STD_MATH_CALL_SUCCESS && isnan(c_double_quiet.value));
    R_TEST_CHECK(c_double_signaling.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_is_quiet_nan_f64(c_double_signaling.value));
    R_TEST_CHECK(c_long_double_quiet.status == R_STD_MATH_CALL_SUCCESS &&
                 isnan(c_long_double_quiet.value));
    R_TEST_CHECK(c_long_double_signaling.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_is_quiet_nan_c_long_double(c_long_double_signaling.value));
    return 0;
}

static int r_test_exact_subnormal_success(void) {
    R_TEST_CHECK(r_test_float_success(r_test_exp2_f32, -127.0F, FP_SUBNORMAL) == 0);
    R_TEST_CHECK(r_test_float_success(r_test_exp2_c_float, -148.0F, FP_SUBNORMAL) == 0);
    R_TEST_CHECK(r_test_float_success(r_test_exp2_f32, -149.0F, FP_SUBNORMAL) == 0);
    R_TEST_CHECK(r_test_double_success(r_test_exp2_f64, -1023.0, FP_SUBNORMAL) == 0);
    R_TEST_CHECK(r_test_double_success(r_test_exp2_c_double, -1073.0, FP_SUBNORMAL) == 0);
    R_TEST_CHECK(r_test_double_success(r_test_exp2_f64, -1074.0, FP_SUBNORMAL) == 0);
    R_TEST_CHECK(r_test_long_double_success(r_test_exp2_c_long_double, -1023.0L, FP_SUBNORMAL) ==
                 0);
    R_TEST_CHECK(r_test_long_double_success(r_test_exp2_c_long_double, -1074.0L, FP_SUBNORMAL) ==
                 0);
    return 0;
}

static int r_test_underflow(void) {
    R_TEST_CHECK(r_test_float_error(
                     r_test_exp2_f32, -126.5F, R_STD_MATH_ERROR_UNDERFLOW, FP_SUBNORMAL) == 0);
    R_TEST_CHECK(r_test_float_error(
                     r_test_exp2_c_float, -148.5F, R_STD_MATH_ERROR_UNDERFLOW, FP_SUBNORMAL) == 0);
    R_TEST_CHECK(r_test_float_error(
                     r_test_exp2_f32, -149.5F, R_STD_MATH_ERROR_UNDERFLOW, FP_SUBNORMAL) == 0);
    R_TEST_CHECK(
        r_test_float_error(r_test_exp2_c_float, -150.0F, R_STD_MATH_ERROR_UNDERFLOW, FP_ZERO) == 0);
    R_TEST_CHECK(
        r_test_float_error(r_test_exp2_f32, -FLT_MAX, R_STD_MATH_ERROR_UNDERFLOW, FP_ZERO) == 0);
    R_TEST_CHECK(r_test_double_error(
                     r_test_exp2_f64, -1022.5, R_STD_MATH_ERROR_UNDERFLOW, FP_SUBNORMAL) == 0);
    R_TEST_CHECK(r_test_double_error(
                     r_test_exp2_c_double, -1073.5, R_STD_MATH_ERROR_UNDERFLOW, FP_SUBNORMAL) == 0);
    R_TEST_CHECK(r_test_double_error(
                     r_test_exp2_f64, -1074.5, R_STD_MATH_ERROR_UNDERFLOW, FP_SUBNORMAL) == 0);
    R_TEST_CHECK(r_test_double_error(
                     r_test_exp2_c_double, -1075.0, R_STD_MATH_ERROR_UNDERFLOW, FP_ZERO) == 0);
    R_TEST_CHECK(
        r_test_double_error(r_test_exp2_f64, -DBL_MAX, R_STD_MATH_ERROR_UNDERFLOW, FP_ZERO) == 0);
    R_TEST_CHECK(
        r_test_long_double_error(
            r_test_exp2_c_long_double, -1022.5L, R_STD_MATH_ERROR_UNDERFLOW, FP_SUBNORMAL) == 0);
    R_TEST_CHECK(r_test_long_double_error(
                     r_test_exp2_c_long_double, -1075.0L, R_STD_MATH_ERROR_UNDERFLOW, FP_ZERO) ==
                 0);
    return 0;
}

static int r_test_overflow_boundary(void) {
    const float last_finite_f32 = 0x1.fffffep+6F;
    const double last_finite_f64 = 0x1.fffffffffffffp+9;
    const long double last_finite_long_double = 0x1.fffffffffffffp+9L;

    R_TEST_CHECK(r_test_float_success(r_test_exp2_f32, last_finite_f32, FP_NORMAL) == 0);
    R_TEST_CHECK(r_test_float_success(r_test_exp2_c_float, last_finite_f32, FP_NORMAL) == 0);
    R_TEST_CHECK(
        r_test_float_error(r_test_exp2_f32, 128.0F, R_STD_MATH_ERROR_OVERFLOW, FP_INFINITE) == 0);
    R_TEST_CHECK(r_test_float_error(
                     r_test_exp2_c_float, FLT_MAX, R_STD_MATH_ERROR_OVERFLOW, FP_INFINITE) == 0);
    R_TEST_CHECK(r_test_double_success(r_test_exp2_f64, last_finite_f64, FP_NORMAL) == 0);
    R_TEST_CHECK(r_test_double_success(r_test_exp2_c_double, last_finite_f64, FP_NORMAL) == 0);
    R_TEST_CHECK(
        r_test_double_error(r_test_exp2_f64, 1024.0, R_STD_MATH_ERROR_OVERFLOW, FP_INFINITE) == 0);
    R_TEST_CHECK(r_test_double_error(
                     r_test_exp2_c_double, DBL_MAX, R_STD_MATH_ERROR_OVERFLOW, FP_INFINITE) == 0);
    R_TEST_CHECK(r_test_long_double_success(
                     r_test_exp2_c_long_double, last_finite_long_double, FP_NORMAL) == 0);
    R_TEST_CHECK(r_test_long_double_error(
                     r_test_exp2_c_long_double, 1024.0L, R_STD_MATH_ERROR_OVERFLOW, FP_INFINITE) ==
                 0);
    return 0;
}

static void *r_test_thread_main(void *opaque_context) {
    RTestExp2ThreadContext *context = opaque_context;
    int iteration;

    if (r_test_seed_environment(context->rounding, context->exceptions, context->native_errno) !=
        0) {
        context->failed = 1;
        return NULL;
    }
    for (iteration = 0; iteration < 500; iteration += 1) {
        RTestFloatResult f32 = r_test_exp2_f32(0.5F);
        RTestDoubleResult f64 = r_test_exp2_f64(0.5);
        RTestFloatResult c_float = r_test_exp2_c_float(0.5F);
        RTestDoubleResult c_double = r_test_exp2_c_double(0.5);
        RTestLongDoubleResult c_long_double = r_test_exp2_c_long_double(0.5L);
        RTestFloatResult f32_exact = r_test_exp2_f32(-149.0F);
        RTestDoubleResult f64_exact = r_test_exp2_f64(-1074.0);
        RTestFloatResult f32_underflow = r_test_exp2_f32(-149.5F);
        RTestDoubleResult f64_underflow = r_test_exp2_f64(-1074.5);
        RTestLongDoubleResult c_long_double_underflow = r_test_exp2_c_long_double(-1075.0L);
        RTestFloatResult f32_overflow = r_test_exp2_c_float(128.0F);
        RTestDoubleResult f64_overflow = r_test_exp2_c_double(1024.0);
        RTestDoubleResult infinity = r_test_exp2_f64(INFINITY);
        RTestFloatResult f32_signaling = r_test_exp2_f32(r_test_signaling_nan_f32());
        RTestDoubleResult f64_signaling = r_test_exp2_f64(r_test_signaling_nan_f64());
        RTestLongDoubleResult c_long_double_signaling =
            r_test_exp2_c_long_double(r_test_signaling_nan_c_long_double());

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
            (f32_exact.status != R_STD_MATH_CALL_SUCCESS) ||
            !r_test_same_f32(f32_exact.value, FLT_TRUE_MIN) ||
            (f64_exact.status != R_STD_MATH_CALL_SUCCESS) ||
            !r_test_same_f64(f64_exact.value, DBL_TRUE_MIN) ||
            (f32_underflow.status != R_STD_MATH_CALL_ERROR) ||
            (f32_underflow.error.code != R_STD_MATH_ERROR_UNDERFLOW) ||
            (f64_underflow.status != R_STD_MATH_CALL_ERROR) ||
            (f64_underflow.error.code != R_STD_MATH_ERROR_UNDERFLOW) ||
            (c_long_double_underflow.status != R_STD_MATH_CALL_ERROR) ||
            (c_long_double_underflow.error.code != R_STD_MATH_ERROR_UNDERFLOW) ||
            (f32_overflow.status != R_STD_MATH_CALL_ERROR) ||
            (f32_overflow.error.code != R_STD_MATH_ERROR_OVERFLOW) ||
            (f64_overflow.status != R_STD_MATH_CALL_ERROR) ||
            (f64_overflow.error.code != R_STD_MATH_ERROR_OVERFLOW) ||
            (infinity.status != R_STD_MATH_CALL_SUCCESS) || !isinf(infinity.value) ||
            (f32_signaling.status != R_STD_MATH_CALL_SUCCESS) ||
            !r_test_is_quiet_nan_f32(f32_signaling.value) ||
            (f64_signaling.status != R_STD_MATH_CALL_SUCCESS) ||
            !r_test_is_quiet_nan_f64(f64_signaling.value) ||
            (c_long_double_signaling.status != R_STD_MATH_CALL_SUCCESS) ||
            !r_test_is_quiet_nan_c_long_double(c_long_double_signaling.value) ||
            !r_test_environment_is(context->rounding, context->exceptions, context->native_errno)) {
            context->failed = 1;
            return NULL;
        }
    }
    return NULL;
}

static int r_test_environment_and_concurrency(void) {
    RTestExp2ThreadContext first;
    RTestExp2ThreadContext second;
    pthread_t first_thread;
    pthread_t second_thread;
    float expected_float;
    double expected_double;
    long double expected_long_double;

    R_TEST_CHECK(r_test_native_f32(0.5F, &expected_float, NULL, NULL) == 0);
    R_TEST_CHECK(r_test_native_f64(0.5, &expected_double, NULL, NULL) == 0);
    R_TEST_CHECK(r_test_native_c_long_double(0.5L, &expected_long_double, NULL, NULL) == 0);
    first = (RTestExp2ThreadContext){
        FE_DOWNWARD, FE_DIVBYZERO, EDOM, expected_float, expected_double, expected_long_double, 0};
    second = (RTestExp2ThreadContext){
        FE_UPWARD, FE_INEXACT, ERANGE, expected_float, expected_double, expected_long_double, 0};
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
    R_TEST_CHECK(r_std_math_exp2_f32(0.5F).status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_std_math_exp2_f64(-1074.0).status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_std_math_exp2_c_float(-149.5F).status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(r_std_math_exp2_c_double(1024.0).status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(r_std_math_exp2_c_long_double(INFINITY).status == R_STD_MATH_CALL_SUCCESS);
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
    R_TEST_CHECK(r_test_regular_and_infinity_values() == 0);
    R_TEST_CHECK(r_test_nan() == 0);
    R_TEST_CHECK(r_test_exact_subnormal_success() == 0);
    R_TEST_CHECK(r_test_underflow() == 0);
    R_TEST_CHECK(r_test_overflow_boundary() == 0);
    R_TEST_CHECK(r_test_environment_is(FE_UPWARD, FE_DIVBYZERO, E2BIG));
    R_TEST_CHECK(r_test_environment_and_concurrency() == 0);
    R_TEST_CHECK(r_test_allocation_free() == 0);
    R_TEST_CHECK(r_test_environment_is(FE_UPWARD, FE_DIVBYZERO, E2BIG));
    R_TEST_CHECK(fesetenv(&original_environment) == 0);
    errno = original_errno;
    (void)fprintf(stdout, "library_math_exp2_tests: ok\n");
    return 0;
}

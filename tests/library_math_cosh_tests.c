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

#define R_TEST_CHECK_OVERFLOW(result, expected_native_code)                                        \
    do {                                                                                           \
        R_TEST_CHECK((result).status == R_STD_MATH_CALL_ERROR);                                    \
        R_TEST_CHECK((result).error.code == R_STD_MATH_ERROR_OVERFLOW);                            \
        R_TEST_CHECK((result).error.native_code == (int64_t)(expected_native_code));               \
    } while (0)

_Static_assert(LDBL_MANT_DIG == 53, "target long double uses a binary64 significand");
_Static_assert(LDBL_MAX_EXP == 1024, "target long double uses a binary64 exponent");
_Static_assert(sizeof(long double) == sizeof(uint64_t),
               "arm64-apple-darwin long double must use binary64 storage");

typedef struct RTestCoshThreadContext {
    int rounding;
    int exceptions;
    int native_errno;
    float expected_float;
    double expected_double;
    long double expected_long_double;
    int failed;
} RTestCoshThreadContext;

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
    *result = coshf(canonical_operand);
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
    *result = cosh(canonical_operand);
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
    *result = coshl(canonical_operand);
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

static int r_test_pair_f32(float value) {
    float expected;
    RStdMathF32Result f32;
    RStdMathCFloatResult c_float;

    R_TEST_CHECK(r_test_native_f32(value, &expected, NULL, NULL) == 0);
    f32 = r_std_math_cosh_f32(value);
    c_float = r_std_math_cosh_c_float(value);
    R_TEST_CHECK(f32.status == R_STD_MATH_CALL_SUCCESS && r_test_same_f32(f32.value, expected));
    R_TEST_CHECK(c_float.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_same_f32(c_float.value, expected));
    return 0;
}

static int r_test_pair_f64(double value) {
    double expected;
    RStdMathF64Result f64;
    RStdMathCDoubleResult c_double;

    R_TEST_CHECK(r_test_native_f64(value, &expected, NULL, NULL) == 0);
    f64 = r_std_math_cosh_f64(value);
    c_double = r_std_math_cosh_c_double(value);
    R_TEST_CHECK(f64.status == R_STD_MATH_CALL_SUCCESS && r_test_same_f64(f64.value, expected));
    R_TEST_CHECK(c_double.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_same_f64(c_double.value, expected));
    return 0;
}

static int r_test_pair_c_long_double(long double value) {
    long double expected;
    RStdMathCLongDoubleResult result;

    R_TEST_CHECK(r_test_native_c_long_double(value, &expected, NULL, NULL) == 0);
    result = r_std_math_cosh_c_long_double(value);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_same_c_long_double(result.value, expected));
    return 0;
}

static int r_test_regular_and_infinity_values(void) {
    static const double inputs[] = {
        -20.0,
        -2.0,
        -1.0,
        -0.5,
        -0.0,
        0.0,
        0.5,
        1.0,
        2.0,
        20.0,
        -INFINITY,
        INFINITY,
    };
    size_t index;

    for (index = 0U; index < sizeof(inputs) / sizeof(inputs[0]); index += 1U) {
        R_TEST_CHECK(r_test_pair_f32((float)inputs[index]) == 0);
        R_TEST_CHECK(r_test_pair_f64(inputs[index]) == 0);
        R_TEST_CHECK(r_test_pair_c_long_double((long double)inputs[index]) == 0);
    }
    return 0;
}

static int r_test_nan(void) {
    RStdMathF32Result f32_quiet = r_std_math_cosh_f32(NAN);
    RStdMathF32Result f32_signaling = r_std_math_cosh_f32(r_test_signaling_nan_f32());
    RStdMathCFloatResult c_float_quiet = r_std_math_cosh_c_float(NAN);
    RStdMathCFloatResult c_float_signaling = r_std_math_cosh_c_float(r_test_signaling_nan_f32());
    RStdMathF64Result f64_quiet = r_std_math_cosh_f64(NAN);
    RStdMathF64Result f64_signaling = r_std_math_cosh_f64(r_test_signaling_nan_f64());
    RStdMathCDoubleResult c_double_quiet = r_std_math_cosh_c_double(NAN);
    RStdMathCDoubleResult c_double_signaling = r_std_math_cosh_c_double(r_test_signaling_nan_f64());
    RStdMathCLongDoubleResult c_long_double_quiet = r_std_math_cosh_c_long_double(NAN);
    RStdMathCLongDoubleResult c_long_double_signaling =
        r_std_math_cosh_c_long_double(r_test_signaling_nan_c_long_double());

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

static int r_test_overflow_f32(float value) {
    float expected;
    int native_errno;
    int exceptions;
    RStdMathF32Result f32;
    RStdMathCFloatResult c_float;

    R_TEST_CHECK(r_test_native_f32(value, &expected, &native_errno, &exceptions) == 0);
    R_TEST_CHECK(isinf(expected));
    R_TEST_CHECK((exceptions & FE_OVERFLOW) != 0);
    f32 = r_std_math_cosh_f32(value);
    c_float = r_std_math_cosh_c_float(value);
    R_TEST_CHECK_OVERFLOW(f32, native_errno);
    R_TEST_CHECK_OVERFLOW(c_float, native_errno);
    return 0;
}

static int r_test_overflow_f64(double value) {
    double expected;
    int native_errno;
    int exceptions;
    RStdMathF64Result f64;
    RStdMathCDoubleResult c_double;

    R_TEST_CHECK(r_test_native_f64(value, &expected, &native_errno, &exceptions) == 0);
    R_TEST_CHECK(isinf(expected));
    R_TEST_CHECK((exceptions & FE_OVERFLOW) != 0);
    f64 = r_std_math_cosh_f64(value);
    c_double = r_std_math_cosh_c_double(value);
    R_TEST_CHECK_OVERFLOW(f64, native_errno);
    R_TEST_CHECK_OVERFLOW(c_double, native_errno);
    return 0;
}

static int r_test_overflow_c_long_double(long double value) {
    long double expected;
    int native_errno;
    int exceptions;
    RStdMathCLongDoubleResult result;

    R_TEST_CHECK(r_test_native_c_long_double(value, &expected, &native_errno, &exceptions) == 0);
    R_TEST_CHECK(isinf(expected));
    R_TEST_CHECK((exceptions & FE_OVERFLOW) != 0);
    result = r_std_math_cosh_c_long_double(value);
    R_TEST_CHECK_OVERFLOW(result, native_errno);
    return 0;
}

static int r_test_overflow_boundaries(void) {
    const float last_finite_f32 = 0x1.65a9f8p+6F;
    const float first_overflow_f32 = 0x1.65a9fap+6F;
    const double last_finite_f64 = 0x1.633ce8fb9f87dp+9;
    const double first_overflow_f64 = 0x1.633ce8fb9f87ep+9;
    const long double last_finite_long_double = 0x1.633ce8fb9f87dp+9L;
    const long double first_overflow_long_double = 0x1.633ce8fb9f87ep+9L;

    R_TEST_CHECK(r_test_pair_f32(last_finite_f32) == 0);
    R_TEST_CHECK(r_test_pair_f32(-last_finite_f32) == 0);
    R_TEST_CHECK(r_test_pair_f64(last_finite_f64) == 0);
    R_TEST_CHECK(r_test_pair_f64(-last_finite_f64) == 0);
    R_TEST_CHECK(r_test_pair_c_long_double(last_finite_long_double) == 0);
    R_TEST_CHECK(r_test_pair_c_long_double(-last_finite_long_double) == 0);
    R_TEST_CHECK(r_test_overflow_f32(first_overflow_f32) == 0);
    R_TEST_CHECK(r_test_overflow_f32(-first_overflow_f32) == 0);
    R_TEST_CHECK(r_test_overflow_f32(FLT_MAX) == 0);
    R_TEST_CHECK(r_test_overflow_f32(-FLT_MAX) == 0);
    R_TEST_CHECK(r_test_overflow_f64(first_overflow_f64) == 0);
    R_TEST_CHECK(r_test_overflow_f64(-first_overflow_f64) == 0);
    R_TEST_CHECK(r_test_overflow_f64(DBL_MAX) == 0);
    R_TEST_CHECK(r_test_overflow_f64(-DBL_MAX) == 0);
    R_TEST_CHECK(r_test_overflow_c_long_double(first_overflow_long_double) == 0);
    R_TEST_CHECK(r_test_overflow_c_long_double(-first_overflow_long_double) == 0);
    R_TEST_CHECK(r_test_overflow_c_long_double(LDBL_MAX) == 0);
    R_TEST_CHECK(r_test_overflow_c_long_double(-LDBL_MAX) == 0);
    return 0;
}

static void *r_test_thread_main(void *opaque_context) {
    RTestCoshThreadContext *context = opaque_context;
    int iteration;

    if (r_test_seed_environment(context->rounding, context->exceptions, context->native_errno) !=
        0) {
        context->failed = 1;
        return NULL;
    }
    for (iteration = 0; iteration < 500; iteration += 1) {
        RStdMathF32Result f32 = r_std_math_cosh_f32(0.5F);
        RStdMathF64Result f64 = r_std_math_cosh_f64(0.5);
        RStdMathCFloatResult c_float = r_std_math_cosh_c_float(0.5F);
        RStdMathCDoubleResult c_double = r_std_math_cosh_c_double(0.5);
        RStdMathCLongDoubleResult c_long_double = r_std_math_cosh_c_long_double(0.5L);
        RStdMathF32Result f32_overflow = r_std_math_cosh_f32(0x1.65a9fap+6F);
        RStdMathF64Result f64_overflow = r_std_math_cosh_f64(0x1.633ce8fb9f87ep+9);
        RStdMathCFloatResult c_float_overflow = r_std_math_cosh_c_float(0x1.65a9fap+6F);
        RStdMathCDoubleResult c_double_overflow = r_std_math_cosh_c_double(0x1.633ce8fb9f87ep+9);
        RStdMathCLongDoubleResult c_long_double_overflow =
            r_std_math_cosh_c_long_double(0x1.633ce8fb9f87ep+9L);
        RStdMathF32Result f32_signaling = r_std_math_cosh_f32(r_test_signaling_nan_f32());
        RStdMathF64Result f64_signaling = r_std_math_cosh_f64(r_test_signaling_nan_f64());
        RStdMathCFloatResult c_float_signaling =
            r_std_math_cosh_c_float(r_test_signaling_nan_f32());
        RStdMathCDoubleResult c_double_signaling =
            r_std_math_cosh_c_double(r_test_signaling_nan_f64());
        RStdMathCLongDoubleResult c_long_double_signaling =
            r_std_math_cosh_c_long_double(r_test_signaling_nan_c_long_double());

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
            (f32_overflow.status != R_STD_MATH_CALL_ERROR) ||
            (f32_overflow.error.code != R_STD_MATH_ERROR_OVERFLOW) ||
            (f64_overflow.status != R_STD_MATH_CALL_ERROR) ||
            (f64_overflow.error.code != R_STD_MATH_ERROR_OVERFLOW) ||
            (c_float_overflow.status != R_STD_MATH_CALL_ERROR) ||
            (c_float_overflow.error.code != R_STD_MATH_ERROR_OVERFLOW) ||
            (c_double_overflow.status != R_STD_MATH_CALL_ERROR) ||
            (c_double_overflow.error.code != R_STD_MATH_ERROR_OVERFLOW) ||
            (c_long_double_overflow.status != R_STD_MATH_CALL_ERROR) ||
            (c_long_double_overflow.error.code != R_STD_MATH_ERROR_OVERFLOW) ||
            (f32_signaling.status != R_STD_MATH_CALL_SUCCESS) ||
            !r_test_is_quiet_nan_f32(f32_signaling.value) ||
            (f64_signaling.status != R_STD_MATH_CALL_SUCCESS) ||
            !r_test_is_quiet_nan_f64(f64_signaling.value) ||
            (c_float_signaling.status != R_STD_MATH_CALL_SUCCESS) ||
            !r_test_is_quiet_nan_f32(c_float_signaling.value) ||
            (c_double_signaling.status != R_STD_MATH_CALL_SUCCESS) ||
            !r_test_is_quiet_nan_f64(c_double_signaling.value) ||
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
    RTestCoshThreadContext first;
    RTestCoshThreadContext second;
    pthread_t first_thread;
    pthread_t second_thread;
    float expected_float;
    double expected_double;
    long double expected_long_double;

    R_TEST_CHECK(r_test_native_f32(0.5F, &expected_float, NULL, NULL) == 0);
    R_TEST_CHECK(r_test_native_f64(0.5, &expected_double, NULL, NULL) == 0);
    R_TEST_CHECK(r_test_native_c_long_double(0.5L, &expected_long_double, NULL, NULL) == 0);
    first = (RTestCoshThreadContext){
        FE_DOWNWARD, FE_DIVBYZERO, EDOM, expected_float, expected_double, expected_long_double, 0};
    second = (RTestCoshThreadContext){
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
    R_TEST_CHECK(r_std_math_cosh_f32(0.5F).status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_std_math_cosh_f64(0.5).status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_std_math_cosh_c_float(0.5F).status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_std_math_cosh_c_double(0.5).status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_std_math_cosh_c_long_double(0.5L).status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_std_math_cosh_f32(FLT_MAX).status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(r_std_math_cosh_f64(DBL_MAX).status == R_STD_MATH_CALL_ERROR);
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
    R_TEST_CHECK(r_test_environment_is(FE_UPWARD, FE_DIVBYZERO, E2BIG));
    R_TEST_CHECK(r_test_overflow_boundaries() == 0);
    R_TEST_CHECK(r_test_environment_is(FE_UPWARD, FE_DIVBYZERO, E2BIG));
    R_TEST_CHECK(r_test_environment_and_concurrency() == 0);
    R_TEST_CHECK(r_test_allocation_free() == 0);
    R_TEST_CHECK(r_test_environment_is(FE_UPWARD, FE_DIVBYZERO, E2BIG));
    R_TEST_CHECK(fesetenv(&original_environment) == 0);
    errno = original_errno;
    (void)fprintf(stdout, "library_math_cosh_tests: ok\n");
    return 0;
}

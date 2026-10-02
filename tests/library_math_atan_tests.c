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

#define R_TEST_CHECK_UNDERFLOW(result)                                                             \
    do {                                                                                           \
        R_TEST_CHECK((result).status == R_STD_MATH_CALL_ERROR);                                    \
        R_TEST_CHECK((result).error.code == R_STD_MATH_ERROR_UNDERFLOW);                           \
        R_TEST_CHECK((result).error.native_code == 0);                                             \
    } while (0)

_Static_assert(LDBL_MANT_DIG == 53, "target long double uses a binary64 significand");
_Static_assert(LDBL_MAX_EXP == 1024, "target long double uses a binary64 exponent");
_Static_assert(sizeof(long double) == sizeof(uint64_t),
               "arm64-apple-darwin long double must use binary64 storage");

typedef struct RTestAtanThreadContext {
    int rounding;
    int exceptions;
    int native_errno;
    float expected_float;
    double expected_double;
    long double expected_long_double;
    int failed;
} RTestAtanThreadContext;

static float r_test_maximum_subnormal_f32(void) {
    uint32_t bits = UINT32_C(0x007fffff);
    float value;

    (void)memcpy(&value, &bits, sizeof(value));
    return value;
}

static double r_test_maximum_subnormal_f64(void) {
    uint64_t bits = UINT64_C(0x000fffffffffffff);
    double value;

    (void)memcpy(&value, &bits, sizeof(value));
    return value;
}

static long double r_test_maximum_subnormal_c_long_double(void) {
    uint64_t bits = UINT64_C(0x000fffffffffffff);
    long double value;

    (void)memcpy(&value, &bits, sizeof(value));
    return value;
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

static int r_test_native_f32(float value, float *result) {
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
    *result = atanf(canonical_operand);
    if (fesetenv(&caller_environment) != 0) {
        errno = caller_errno;
        return 1;
    }
    errno = caller_errno;
    return 0;
}

static int r_test_native_f64(double value, double *result) {
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
    *result = atan(canonical_operand);
    if (fesetenv(&caller_environment) != 0) {
        errno = caller_errno;
        return 1;
    }
    errno = caller_errno;
    return 0;
}

static int r_test_native_c_long_double(long double value, long double *result) {
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
    *result = atanl(canonical_operand);
    if (fesetenv(&caller_environment) != 0) {
        errno = caller_errno;
        return 1;
    }
    errno = caller_errno;
    return 0;
}

static int r_test_f32_and_c_float(void) {
    const float inputs[] = {-FLT_MAX, -1.0F, -0.5F, 0.5F, 1.0F, FLT_MAX};
    size_t index;

    for (index = 0U; index < sizeof(inputs) / sizeof(inputs[0]); index += 1U) {
        float expected;
        RStdMathF32Result f32;
        RStdMathCFloatResult c_float;

        R_TEST_CHECK(r_test_native_f32(inputs[index], &expected) == 0);
        f32 = r_std_math_atan_f32(inputs[index]);
        c_float = r_std_math_atan_c_float(inputs[index]);
        R_TEST_CHECK(f32.status == R_STD_MATH_CALL_SUCCESS && f32.value == expected);
        R_TEST_CHECK(c_float.status == R_STD_MATH_CALL_SUCCESS && c_float.value == expected);
    }
    {
        float expected_positive_infinity;
        float expected_negative_infinity;
        RStdMathF32Result positive_zero = r_std_math_atan_f32(0.0F);
        RStdMathF32Result negative_zero = r_std_math_atan_f32(-0.0F);
        RStdMathF32Result positive_infinity = r_std_math_atan_f32(INFINITY);
        RStdMathF32Result negative_infinity = r_std_math_atan_f32(-INFINITY);
        RStdMathF32Result quiet_nan = r_std_math_atan_f32(NAN);
        RStdMathF32Result signaling_nan = r_std_math_atan_f32(r_test_signaling_nan_f32());
        RStdMathCFloatResult c_positive_zero = r_std_math_atan_c_float(0.0F);
        RStdMathCFloatResult c_negative_zero = r_std_math_atan_c_float(-0.0F);
        RStdMathCFloatResult c_positive_infinity = r_std_math_atan_c_float(INFINITY);
        RStdMathCFloatResult c_negative_infinity = r_std_math_atan_c_float(-INFINITY);
        RStdMathCFloatResult c_quiet_nan = r_std_math_atan_c_float(NAN);
        RStdMathCFloatResult c_signaling_nan = r_std_math_atan_c_float(r_test_signaling_nan_f32());

        R_TEST_CHECK(r_test_native_f32(INFINITY, &expected_positive_infinity) == 0);
        R_TEST_CHECK(r_test_native_f32(-INFINITY, &expected_negative_infinity) == 0);

        R_TEST_CHECK(positive_zero.status == R_STD_MATH_CALL_SUCCESS &&
                     positive_zero.value == 0.0F && !signbit(positive_zero.value));
        R_TEST_CHECK(negative_zero.status == R_STD_MATH_CALL_SUCCESS &&
                     negative_zero.value == 0.0F && signbit(negative_zero.value));
        R_TEST_CHECK(positive_infinity.status == R_STD_MATH_CALL_SUCCESS &&
                     positive_infinity.value == expected_positive_infinity);
        R_TEST_CHECK(negative_infinity.status == R_STD_MATH_CALL_SUCCESS &&
                     negative_infinity.value == expected_negative_infinity);
        R_TEST_CHECK(quiet_nan.status == R_STD_MATH_CALL_SUCCESS && isnan(quiet_nan.value));
        R_TEST_CHECK(signaling_nan.status == R_STD_MATH_CALL_SUCCESS &&
                     r_test_is_quiet_nan_f32(signaling_nan.value));
        R_TEST_CHECK(c_positive_zero.status == R_STD_MATH_CALL_SUCCESS &&
                     c_positive_zero.value == 0.0F && !signbit(c_positive_zero.value));
        R_TEST_CHECK(c_negative_zero.status == R_STD_MATH_CALL_SUCCESS &&
                     c_negative_zero.value == 0.0F && signbit(c_negative_zero.value));
        R_TEST_CHECK(c_positive_infinity.status == R_STD_MATH_CALL_SUCCESS &&
                     c_positive_infinity.value == expected_positive_infinity);
        R_TEST_CHECK(c_negative_infinity.status == R_STD_MATH_CALL_SUCCESS &&
                     c_negative_infinity.value == expected_negative_infinity);
        R_TEST_CHECK(c_quiet_nan.status == R_STD_MATH_CALL_SUCCESS && isnan(c_quiet_nan.value));
        R_TEST_CHECK(c_signaling_nan.status == R_STD_MATH_CALL_SUCCESS &&
                     r_test_is_quiet_nan_f32(c_signaling_nan.value));
    }
    {
        const float maximum_subnormal = r_test_maximum_subnormal_f32();
        RStdMathF32Result positive_true_min = r_std_math_atan_f32(FLT_TRUE_MIN);
        RStdMathF32Result negative_maximum = r_std_math_atan_f32(-maximum_subnormal);
        RStdMathF32Result positive_normal = r_std_math_atan_f32(FLT_MIN);
        RStdMathF32Result negative_normal = r_std_math_atan_f32(-FLT_MIN);
        RStdMathCFloatResult c_negative_true_min = r_std_math_atan_c_float(-FLT_TRUE_MIN);
        RStdMathCFloatResult c_positive_maximum = r_std_math_atan_c_float(maximum_subnormal);
        RStdMathCFloatResult c_positive_normal = r_std_math_atan_c_float(FLT_MIN);
        RStdMathCFloatResult c_negative_normal = r_std_math_atan_c_float(-FLT_MIN);

        R_TEST_CHECK_UNDERFLOW(positive_true_min);
        R_TEST_CHECK_UNDERFLOW(negative_maximum);
        R_TEST_CHECK_UNDERFLOW(c_negative_true_min);
        R_TEST_CHECK_UNDERFLOW(c_positive_maximum);
        R_TEST_CHECK(positive_normal.status == R_STD_MATH_CALL_SUCCESS &&
                     positive_normal.value == FLT_MIN);
        R_TEST_CHECK(negative_normal.status == R_STD_MATH_CALL_SUCCESS &&
                     negative_normal.value == -FLT_MIN);
        R_TEST_CHECK(c_positive_normal.status == R_STD_MATH_CALL_SUCCESS &&
                     c_positive_normal.value == FLT_MIN);
        R_TEST_CHECK(c_negative_normal.status == R_STD_MATH_CALL_SUCCESS &&
                     c_negative_normal.value == -FLT_MIN);
    }
    return 0;
}

static int r_test_f64_and_c_double(void) {
    const double inputs[] = {-DBL_MAX, -1.0, -0.5, 0.5, 1.0, DBL_MAX};
    size_t index;

    for (index = 0U; index < sizeof(inputs) / sizeof(inputs[0]); index += 1U) {
        double expected;
        RStdMathF64Result f64;
        RStdMathCDoubleResult c_double;

        R_TEST_CHECK(r_test_native_f64(inputs[index], &expected) == 0);
        f64 = r_std_math_atan_f64(inputs[index]);
        c_double = r_std_math_atan_c_double(inputs[index]);
        R_TEST_CHECK(f64.status == R_STD_MATH_CALL_SUCCESS && f64.value == expected);
        R_TEST_CHECK(c_double.status == R_STD_MATH_CALL_SUCCESS && c_double.value == expected);
    }
    {
        double expected_positive_infinity;
        double expected_negative_infinity;
        RStdMathF64Result positive_zero = r_std_math_atan_f64(0.0);
        RStdMathF64Result negative_zero = r_std_math_atan_f64(-0.0);
        RStdMathF64Result positive_infinity = r_std_math_atan_f64(INFINITY);
        RStdMathF64Result negative_infinity = r_std_math_atan_f64(-INFINITY);
        RStdMathF64Result quiet_nan = r_std_math_atan_f64(NAN);
        RStdMathF64Result signaling_nan = r_std_math_atan_f64(r_test_signaling_nan_f64());
        RStdMathCDoubleResult c_positive_zero = r_std_math_atan_c_double(0.0);
        RStdMathCDoubleResult c_negative_zero = r_std_math_atan_c_double(-0.0);
        RStdMathCDoubleResult c_positive_infinity = r_std_math_atan_c_double(INFINITY);
        RStdMathCDoubleResult c_negative_infinity = r_std_math_atan_c_double(-INFINITY);
        RStdMathCDoubleResult c_quiet_nan = r_std_math_atan_c_double(NAN);
        RStdMathCDoubleResult c_signaling_nan =
            r_std_math_atan_c_double(r_test_signaling_nan_f64());

        R_TEST_CHECK(r_test_native_f64(INFINITY, &expected_positive_infinity) == 0);
        R_TEST_CHECK(r_test_native_f64(-INFINITY, &expected_negative_infinity) == 0);

        R_TEST_CHECK(positive_zero.status == R_STD_MATH_CALL_SUCCESS &&
                     positive_zero.value == 0.0 && !signbit(positive_zero.value));
        R_TEST_CHECK(negative_zero.status == R_STD_MATH_CALL_SUCCESS &&
                     negative_zero.value == 0.0 && signbit(negative_zero.value));
        R_TEST_CHECK(positive_infinity.status == R_STD_MATH_CALL_SUCCESS &&
                     positive_infinity.value == expected_positive_infinity);
        R_TEST_CHECK(negative_infinity.status == R_STD_MATH_CALL_SUCCESS &&
                     negative_infinity.value == expected_negative_infinity);
        R_TEST_CHECK(quiet_nan.status == R_STD_MATH_CALL_SUCCESS && isnan(quiet_nan.value));
        R_TEST_CHECK(signaling_nan.status == R_STD_MATH_CALL_SUCCESS &&
                     r_test_is_quiet_nan_f64(signaling_nan.value));
        R_TEST_CHECK(c_positive_zero.status == R_STD_MATH_CALL_SUCCESS &&
                     c_positive_zero.value == 0.0 && !signbit(c_positive_zero.value));
        R_TEST_CHECK(c_negative_zero.status == R_STD_MATH_CALL_SUCCESS &&
                     c_negative_zero.value == 0.0 && signbit(c_negative_zero.value));
        R_TEST_CHECK(c_positive_infinity.status == R_STD_MATH_CALL_SUCCESS &&
                     c_positive_infinity.value == expected_positive_infinity);
        R_TEST_CHECK(c_negative_infinity.status == R_STD_MATH_CALL_SUCCESS &&
                     c_negative_infinity.value == expected_negative_infinity);
        R_TEST_CHECK(c_quiet_nan.status == R_STD_MATH_CALL_SUCCESS && isnan(c_quiet_nan.value));
        R_TEST_CHECK(c_signaling_nan.status == R_STD_MATH_CALL_SUCCESS &&
                     r_test_is_quiet_nan_f64(c_signaling_nan.value));
    }
    {
        const double maximum_subnormal = r_test_maximum_subnormal_f64();
        RStdMathF64Result positive_true_min = r_std_math_atan_f64(DBL_TRUE_MIN);
        RStdMathF64Result negative_maximum = r_std_math_atan_f64(-maximum_subnormal);
        RStdMathF64Result positive_normal = r_std_math_atan_f64(DBL_MIN);
        RStdMathF64Result negative_normal = r_std_math_atan_f64(-DBL_MIN);
        RStdMathCDoubleResult c_negative_true_min = r_std_math_atan_c_double(-DBL_TRUE_MIN);
        RStdMathCDoubleResult c_positive_maximum = r_std_math_atan_c_double(maximum_subnormal);
        RStdMathCDoubleResult c_positive_normal = r_std_math_atan_c_double(DBL_MIN);
        RStdMathCDoubleResult c_negative_normal = r_std_math_atan_c_double(-DBL_MIN);

        R_TEST_CHECK_UNDERFLOW(positive_true_min);
        R_TEST_CHECK_UNDERFLOW(negative_maximum);
        R_TEST_CHECK_UNDERFLOW(c_negative_true_min);
        R_TEST_CHECK_UNDERFLOW(c_positive_maximum);
        R_TEST_CHECK(positive_normal.status == R_STD_MATH_CALL_SUCCESS &&
                     positive_normal.value == DBL_MIN);
        R_TEST_CHECK(negative_normal.status == R_STD_MATH_CALL_SUCCESS &&
                     negative_normal.value == -DBL_MIN);
        R_TEST_CHECK(c_positive_normal.status == R_STD_MATH_CALL_SUCCESS &&
                     c_positive_normal.value == DBL_MIN);
        R_TEST_CHECK(c_negative_normal.status == R_STD_MATH_CALL_SUCCESS &&
                     c_negative_normal.value == -DBL_MIN);
    }
    return 0;
}

static int r_test_c_long_double(void) {
    const long double inputs[] = {-LDBL_MAX, -1.0L, -0.5L, 0.5L, 1.0L, LDBL_MAX};
    size_t index;

    for (index = 0U; index < sizeof(inputs) / sizeof(inputs[0]); index += 1U) {
        long double expected;
        RStdMathCLongDoubleResult result;

        R_TEST_CHECK(r_test_native_c_long_double(inputs[index], &expected) == 0);
        result = r_std_math_atan_c_long_double(inputs[index]);
        R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS && result.value == expected);
    }
    {
        long double expected_positive_infinity;
        long double expected_negative_infinity;
        RStdMathCLongDoubleResult positive_zero = r_std_math_atan_c_long_double(0.0L);
        RStdMathCLongDoubleResult negative_zero = r_std_math_atan_c_long_double(-0.0L);
        RStdMathCLongDoubleResult positive_infinity = r_std_math_atan_c_long_double(INFINITY);
        RStdMathCLongDoubleResult negative_infinity = r_std_math_atan_c_long_double(-INFINITY);
        RStdMathCLongDoubleResult quiet_nan = r_std_math_atan_c_long_double(NAN);
        RStdMathCLongDoubleResult signaling_nan =
            r_std_math_atan_c_long_double(r_test_signaling_nan_c_long_double());

        R_TEST_CHECK(r_test_native_c_long_double(INFINITY, &expected_positive_infinity) == 0);
        R_TEST_CHECK(r_test_native_c_long_double(-INFINITY, &expected_negative_infinity) == 0);

        R_TEST_CHECK(positive_zero.status == R_STD_MATH_CALL_SUCCESS &&
                     positive_zero.value == 0.0L && !signbit(positive_zero.value));
        R_TEST_CHECK(negative_zero.status == R_STD_MATH_CALL_SUCCESS &&
                     negative_zero.value == 0.0L && signbit(negative_zero.value));
        R_TEST_CHECK(positive_infinity.status == R_STD_MATH_CALL_SUCCESS &&
                     positive_infinity.value == expected_positive_infinity);
        R_TEST_CHECK(negative_infinity.status == R_STD_MATH_CALL_SUCCESS &&
                     negative_infinity.value == expected_negative_infinity);
        R_TEST_CHECK(quiet_nan.status == R_STD_MATH_CALL_SUCCESS && isnan(quiet_nan.value));
        R_TEST_CHECK(signaling_nan.status == R_STD_MATH_CALL_SUCCESS &&
                     r_test_is_quiet_nan_c_long_double(signaling_nan.value));
    }
    {
        const long double maximum_subnormal = r_test_maximum_subnormal_c_long_double();
        RStdMathCLongDoubleResult positive_true_min = r_std_math_atan_c_long_double(LDBL_TRUE_MIN);
        RStdMathCLongDoubleResult negative_maximum =
            r_std_math_atan_c_long_double(-maximum_subnormal);
        RStdMathCLongDoubleResult positive_normal = r_std_math_atan_c_long_double(LDBL_MIN);
        RStdMathCLongDoubleResult negative_normal = r_std_math_atan_c_long_double(-LDBL_MIN);

        R_TEST_CHECK_UNDERFLOW(positive_true_min);
        R_TEST_CHECK_UNDERFLOW(negative_maximum);
        R_TEST_CHECK(positive_normal.status == R_STD_MATH_CALL_SUCCESS &&
                     positive_normal.value == LDBL_MIN);
        R_TEST_CHECK(negative_normal.status == R_STD_MATH_CALL_SUCCESS &&
                     negative_normal.value == -LDBL_MIN);
    }
    return 0;
}

static void *r_test_thread_main(void *opaque_context) {
    RTestAtanThreadContext *context = opaque_context;
    int iteration;

    if (r_test_seed_environment(context->rounding, context->exceptions, context->native_errno) !=
        0) {
        context->failed = 1;
        return NULL;
    }
    for (iteration = 0; iteration < 500; iteration += 1) {
        RStdMathF32Result f32 = r_std_math_atan_f32(0.5F);
        RStdMathF64Result f64 = r_std_math_atan_f64(0.5);
        RStdMathCFloatResult c_float = r_std_math_atan_c_float(0.5F);
        RStdMathCDoubleResult c_double = r_std_math_atan_c_double(0.5);
        RStdMathCLongDoubleResult c_long_double = r_std_math_atan_c_long_double(0.5L);

        if (f32.status != R_STD_MATH_CALL_SUCCESS || f32.value != context->expected_float ||
            f64.status != R_STD_MATH_CALL_SUCCESS || f64.value != context->expected_double ||
            c_float.status != R_STD_MATH_CALL_SUCCESS || c_float.value != context->expected_float ||
            c_double.status != R_STD_MATH_CALL_SUCCESS ||
            c_double.value != context->expected_double ||
            c_long_double.status != R_STD_MATH_CALL_SUCCESS ||
            c_long_double.value != context->expected_long_double ||
            !r_test_environment_is(context->rounding, context->exceptions, context->native_errno)) {
            context->failed = 1;
            return NULL;
        }
    }
    return NULL;
}

static int r_test_environment_and_concurrency(void) {
    RTestAtanThreadContext first;
    RTestAtanThreadContext second;
    pthread_t first_thread;
    pthread_t second_thread;
    float expected_float;
    double expected_double;
    long double expected_long_double;

    R_TEST_CHECK(r_test_native_f32(0.5F, &expected_float) == 0);
    R_TEST_CHECK(r_test_native_f64(0.5, &expected_double) == 0);
    R_TEST_CHECK(r_test_native_c_long_double(0.5L, &expected_long_double) == 0);
    first = (RTestAtanThreadContext){
        FE_DOWNWARD, FE_DIVBYZERO, EDOM, expected_float, expected_double, expected_long_double, 0};
    second = (RTestAtanThreadContext){
        FE_UPWARD, FE_INVALID, ERANGE, expected_float, expected_double, expected_long_double, 0};
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
    R_TEST_CHECK(r_std_math_atan_f32(0.5F).status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_std_math_atan_f64(0.5).status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_std_math_atan_c_float(0.5F).status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_std_math_atan_c_double(0.5).status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_std_math_atan_c_long_double(0.5L).status == R_STD_MATH_CALL_SUCCESS);
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
    R_TEST_CHECK(r_test_f32_and_c_float() == 0);
    R_TEST_CHECK(r_test_f64_and_c_double() == 0);
    R_TEST_CHECK(r_test_c_long_double() == 0);
    R_TEST_CHECK(r_test_environment_is(FE_UPWARD, FE_DIVBYZERO, E2BIG));
    R_TEST_CHECK(r_test_environment_and_concurrency() == 0);
    R_TEST_CHECK(r_test_allocation_free() == 0);
    R_TEST_CHECK(fesetenv(&original_environment) == 0);
    errno = original_errno;
    (void)fprintf(stdout, "library_math_atan_tests: ok\n");
    return 0;
}

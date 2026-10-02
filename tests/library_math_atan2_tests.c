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
    } while (0)

_Static_assert(LDBL_MANT_DIG == 53, "target long double uses a binary64 significand");
_Static_assert(LDBL_MAX_EXP == 1024, "target long double uses a binary64 exponent");
_Static_assert(sizeof(long double) == sizeof(uint64_t),
               "arm64-apple-darwin long double must use binary64 storage");

typedef struct RTestAtan2ThreadContext {
    int rounding;
    int exceptions;
    int native_errno;
    float expected_float;
    double expected_double;
    long double expected_long_double;
    int failed;
} RTestAtan2ThreadContext;

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

static int r_test_native_f32(float left, float right, float *result) {
    fenv_t caller_environment;
    fenv_t held_environment;
    int caller_errno = errno;
    volatile float canonical_left;
    volatile float canonical_right;

    if (fegetenv(&caller_environment) != 0) {
        return 1;
    }
    if ((feholdexcept(&held_environment) != 0) || (fesetround(FE_TONEAREST) != 0)) {
        (void)fesetenv(&caller_environment);
        errno = caller_errno;
        return 1;
    }
    errno = 0;
    canonical_left = left;
    canonical_right = right;
    *result = atan2f(canonical_left, canonical_right);
    if (fesetenv(&caller_environment) != 0) {
        errno = caller_errno;
        return 1;
    }
    errno = caller_errno;
    return 0;
}

static int r_test_native_f64(double left, double right, double *result) {
    fenv_t caller_environment;
    fenv_t held_environment;
    int caller_errno = errno;
    volatile double canonical_left;
    volatile double canonical_right;

    if (fegetenv(&caller_environment) != 0) {
        return 1;
    }
    if ((feholdexcept(&held_environment) != 0) || (fesetround(FE_TONEAREST) != 0)) {
        (void)fesetenv(&caller_environment);
        errno = caller_errno;
        return 1;
    }
    errno = 0;
    canonical_left = left;
    canonical_right = right;
    *result = atan2(canonical_left, canonical_right);
    if (fesetenv(&caller_environment) != 0) {
        errno = caller_errno;
        return 1;
    }
    errno = caller_errno;
    return 0;
}

static int r_test_native_c_long_double(long double left, long double right, long double *result) {
    fenv_t caller_environment;
    fenv_t held_environment;
    int caller_errno = errno;
    volatile long double canonical_left;
    volatile long double canonical_right;

    if (fegetenv(&caller_environment) != 0) {
        return 1;
    }
    if ((feholdexcept(&held_environment) != 0) || (fesetround(FE_TONEAREST) != 0)) {
        (void)fesetenv(&caller_environment);
        errno = caller_errno;
        return 1;
    }
    errno = 0;
    canonical_left = left;
    canonical_right = right;
    *result = atan2l(canonical_left, canonical_right);
    if (fesetenv(&caller_environment) != 0) {
        errno = caller_errno;
        return 1;
    }
    errno = caller_errno;
    return 0;
}

static int r_test_pair_f32(float left, float right) {
    float expected;
    RStdMathF32Result f32;
    RStdMathCFloatResult c_float;

    R_TEST_CHECK(r_test_native_f32(left, right, &expected) == 0);
    f32 = r_std_math_atan2_f32(left, right);
    c_float = r_std_math_atan2_c_float(left, right);
    R_TEST_CHECK(f32.status == R_STD_MATH_CALL_SUCCESS && r_test_same_f32(f32.value, expected));
    R_TEST_CHECK(c_float.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_same_f32(c_float.value, expected));
    return 0;
}

static int r_test_pair_f64(double left, double right) {
    double expected;
    RStdMathF64Result f64;
    RStdMathCDoubleResult c_double;

    R_TEST_CHECK(r_test_native_f64(left, right, &expected) == 0);
    f64 = r_std_math_atan2_f64(left, right);
    c_double = r_std_math_atan2_c_double(left, right);
    R_TEST_CHECK(f64.status == R_STD_MATH_CALL_SUCCESS && r_test_same_f64(f64.value, expected));
    R_TEST_CHECK(c_double.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_same_f64(c_double.value, expected));
    return 0;
}

static int r_test_pair_c_long_double(long double left, long double right) {
    long double expected;
    RStdMathCLongDoubleResult result;

    R_TEST_CHECK(r_test_native_c_long_double(left, right, &expected) == 0);
    result = r_std_math_atan2_c_long_double(left, right);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_same_c_long_double(result.value, expected));
    return 0;
}

static int r_test_quadrants_and_signed_zero(void) {
    static const float f32_pairs[][2] = {{1.0F, 1.0F},
                                         {1.0F, -1.0F},
                                         {-1.0F, -1.0F},
                                         {-1.0F, 1.0F},
                                         {2.0F, 0.0F},
                                         {-2.0F, 0.0F},
                                         {0.0F, 2.0F},
                                         {-0.0F, 2.0F},
                                         {0.0F, -2.0F},
                                         {-0.0F, -2.0F},
                                         {0.0F, 0.0F},
                                         {-0.0F, -0.0F}};
    static const double f64_pairs[][2] = {{1.0, 1.0},
                                          {1.0, -1.0},
                                          {-1.0, -1.0},
                                          {-1.0, 1.0},
                                          {2.0, 0.0},
                                          {-2.0, 0.0},
                                          {0.0, 2.0},
                                          {-0.0, 2.0},
                                          {0.0, -2.0},
                                          {-0.0, -2.0},
                                          {0.0, 0.0},
                                          {-0.0, -0.0}};
    static const long double long_double_pairs[][2] = {
        {1.0L, 1.0L},
        {1.0L, -1.0L},
        {-1.0L, -1.0L},
        {-1.0L, 1.0L},
        {2.0L, 0.0L},
        {-2.0L, 0.0L},
        {0.0L, 2.0L},
        {-0.0L, 2.0L},
        {0.0L, -2.0L},
        {-0.0L, -2.0L},
        {0.0L, 0.0L},
        {-0.0L, -0.0L},
    };
    size_t index;

    for (index = 0U; index < sizeof(f32_pairs) / sizeof(f32_pairs[0]); index += 1U) {
        R_TEST_CHECK(r_test_pair_f32(f32_pairs[index][0], f32_pairs[index][1]) == 0);
    }
    for (index = 0U; index < sizeof(f64_pairs) / sizeof(f64_pairs[0]); index += 1U) {
        R_TEST_CHECK(r_test_pair_f64(f64_pairs[index][0], f64_pairs[index][1]) == 0);
    }
    for (index = 0U; index < sizeof(long_double_pairs) / sizeof(long_double_pairs[0]);
         index += 1U) {
        R_TEST_CHECK(r_test_pair_c_long_double(long_double_pairs[index][0],
                                               long_double_pairs[index][1]) == 0);
    }
    return 0;
}

static int r_test_infinities(void) {
    static const double pairs[][2] = {
        {1.0, INFINITY},
        {-1.0, INFINITY},
        {1.0, -INFINITY},
        {-1.0, -INFINITY},
        {INFINITY, 1.0},
        {-INFINITY, 1.0},
        {INFINITY, -1.0},
        {-INFINITY, -1.0},
        {INFINITY, INFINITY},
        {INFINITY, -INFINITY},
        {-INFINITY, INFINITY},
        {-INFINITY, -INFINITY},
    };
    size_t index;

    for (index = 0U; index < sizeof(pairs) / sizeof(pairs[0]); index += 1U) {
        R_TEST_CHECK(r_test_pair_f32((float)pairs[index][0], (float)pairs[index][1]) == 0);
        R_TEST_CHECK(r_test_pair_f64(pairs[index][0], pairs[index][1]) == 0);
        R_TEST_CHECK(r_test_pair_c_long_double((long double)pairs[index][0],
                                               (long double)pairs[index][1]) == 0);
    }
    return 0;
}

static int r_test_nan(void) {
    const float signaling_f32 = r_test_signaling_nan_f32();
    const double signaling_f64 = r_test_signaling_nan_f64();
    const long double signaling_long_double = r_test_signaling_nan_c_long_double();
    RStdMathF32Result f32_left = r_std_math_atan2_f32(NAN, 1.0F);
    RStdMathF32Result f32_right = r_std_math_atan2_f32(1.0F, NAN);
    RStdMathF32Result f32_signaling_left = r_std_math_atan2_f32(signaling_f32, 1.0F);
    RStdMathF32Result f32_signaling_right = r_std_math_atan2_f32(1.0F, signaling_f32);
    RStdMathCFloatResult c_float_left = r_std_math_atan2_c_float(NAN, 1.0F);
    RStdMathCFloatResult c_float_right = r_std_math_atan2_c_float(1.0F, NAN);
    RStdMathCFloatResult c_float_signaling_left = r_std_math_atan2_c_float(signaling_f32, 1.0F);
    RStdMathCFloatResult c_float_signaling_right = r_std_math_atan2_c_float(1.0F, signaling_f32);
    RStdMathF64Result f64_left = r_std_math_atan2_f64(NAN, 1.0);
    RStdMathF64Result f64_right = r_std_math_atan2_f64(1.0, NAN);
    RStdMathF64Result f64_signaling_left = r_std_math_atan2_f64(signaling_f64, 1.0);
    RStdMathF64Result f64_signaling_right = r_std_math_atan2_f64(1.0, signaling_f64);
    RStdMathCDoubleResult c_double_left = r_std_math_atan2_c_double(NAN, 1.0);
    RStdMathCDoubleResult c_double_right = r_std_math_atan2_c_double(1.0, NAN);
    RStdMathCDoubleResult c_double_signaling_left = r_std_math_atan2_c_double(signaling_f64, 1.0);
    RStdMathCDoubleResult c_double_signaling_right = r_std_math_atan2_c_double(1.0, signaling_f64);
    RStdMathCLongDoubleResult long_double_left = r_std_math_atan2_c_long_double(NAN, 1.0L);
    RStdMathCLongDoubleResult long_double_right = r_std_math_atan2_c_long_double(1.0L, NAN);
    RStdMathCLongDoubleResult long_double_signaling_left =
        r_std_math_atan2_c_long_double(signaling_long_double, 1.0L);
    RStdMathCLongDoubleResult long_double_signaling_right =
        r_std_math_atan2_c_long_double(1.0L, signaling_long_double);

    R_TEST_CHECK(f32_left.status == R_STD_MATH_CALL_SUCCESS && isnan(f32_left.value));
    R_TEST_CHECK(f32_right.status == R_STD_MATH_CALL_SUCCESS && isnan(f32_right.value));
    R_TEST_CHECK(f32_signaling_left.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_is_quiet_nan_f32(f32_signaling_left.value));
    R_TEST_CHECK(f32_signaling_right.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_is_quiet_nan_f32(f32_signaling_right.value));
    R_TEST_CHECK(c_float_left.status == R_STD_MATH_CALL_SUCCESS && isnan(c_float_left.value));
    R_TEST_CHECK(c_float_right.status == R_STD_MATH_CALL_SUCCESS && isnan(c_float_right.value));
    R_TEST_CHECK(c_float_signaling_left.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_is_quiet_nan_f32(c_float_signaling_left.value));
    R_TEST_CHECK(c_float_signaling_right.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_is_quiet_nan_f32(c_float_signaling_right.value));
    R_TEST_CHECK(f64_left.status == R_STD_MATH_CALL_SUCCESS && isnan(f64_left.value));
    R_TEST_CHECK(f64_right.status == R_STD_MATH_CALL_SUCCESS && isnan(f64_right.value));
    R_TEST_CHECK(f64_signaling_left.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_is_quiet_nan_f64(f64_signaling_left.value));
    R_TEST_CHECK(f64_signaling_right.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_is_quiet_nan_f64(f64_signaling_right.value));
    R_TEST_CHECK(c_double_left.status == R_STD_MATH_CALL_SUCCESS && isnan(c_double_left.value));
    R_TEST_CHECK(c_double_right.status == R_STD_MATH_CALL_SUCCESS && isnan(c_double_right.value));
    R_TEST_CHECK(c_double_signaling_left.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_is_quiet_nan_f64(c_double_signaling_left.value));
    R_TEST_CHECK(c_double_signaling_right.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_is_quiet_nan_f64(c_double_signaling_right.value));
    R_TEST_CHECK(long_double_left.status == R_STD_MATH_CALL_SUCCESS &&
                 isnan(long_double_left.value));
    R_TEST_CHECK(long_double_right.status == R_STD_MATH_CALL_SUCCESS &&
                 isnan(long_double_right.value));
    R_TEST_CHECK(long_double_signaling_left.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_is_quiet_nan_c_long_double(long_double_signaling_left.value));
    R_TEST_CHECK(long_double_signaling_right.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_is_quiet_nan_c_long_double(long_double_signaling_right.value));
    return 0;
}

static int r_test_underflow(void) {
    const float maximum_subnormal_f32 = r_test_maximum_subnormal_f32();
    const double maximum_subnormal_f64 = r_test_maximum_subnormal_f64();
    const long double maximum_subnormal_long_double = r_test_maximum_subnormal_c_long_double();
    RStdMathF32Result f32_zero = r_std_math_atan2_f32(FLT_TRUE_MIN, FLT_MAX);
    RStdMathF32Result f32_subnormal = r_std_math_atan2_f32(maximum_subnormal_f32, 2.0F);
    RStdMathCFloatResult c_float_zero = r_std_math_atan2_c_float(-FLT_TRUE_MIN, FLT_MAX);
    RStdMathCFloatResult c_float_subnormal = r_std_math_atan2_c_float(-maximum_subnormal_f32, 2.0F);
    RStdMathF64Result f64_zero = r_std_math_atan2_f64(DBL_TRUE_MIN, DBL_MAX);
    RStdMathF64Result f64_subnormal = r_std_math_atan2_f64(maximum_subnormal_f64, 2.0);
    RStdMathCDoubleResult c_double_zero = r_std_math_atan2_c_double(-DBL_TRUE_MIN, DBL_MAX);
    RStdMathCDoubleResult c_double_subnormal =
        r_std_math_atan2_c_double(-maximum_subnormal_f64, 2.0);
    RStdMathCLongDoubleResult long_double_zero =
        r_std_math_atan2_c_long_double(LDBL_TRUE_MIN, LDBL_MAX);
    RStdMathCLongDoubleResult long_double_subnormal =
        r_std_math_atan2_c_long_double(-maximum_subnormal_long_double, 2.0L);

    R_TEST_CHECK_UNDERFLOW(f32_zero);
    R_TEST_CHECK_UNDERFLOW(f32_subnormal);
    R_TEST_CHECK_UNDERFLOW(c_float_zero);
    R_TEST_CHECK_UNDERFLOW(c_float_subnormal);
    R_TEST_CHECK_UNDERFLOW(f64_zero);
    R_TEST_CHECK_UNDERFLOW(f64_subnormal);
    R_TEST_CHECK_UNDERFLOW(c_double_zero);
    R_TEST_CHECK_UNDERFLOW(c_double_subnormal);
    R_TEST_CHECK_UNDERFLOW(long_double_zero);
    R_TEST_CHECK_UNDERFLOW(long_double_subnormal);
    R_TEST_CHECK(r_test_pair_f32(FLT_MIN, 1.0F) == 0);
    R_TEST_CHECK(r_test_pair_f32(FLT_TRUE_MIN, INFINITY) == 0);
    R_TEST_CHECK(r_test_pair_f64(DBL_MIN, 1.0) == 0);
    R_TEST_CHECK(r_test_pair_f64(DBL_TRUE_MIN, INFINITY) == 0);
    R_TEST_CHECK(r_test_pair_c_long_double(LDBL_MIN, 1.0L) == 0);
    R_TEST_CHECK(r_test_pair_c_long_double(LDBL_TRUE_MIN, INFINITY) == 0);
    return 0;
}

static void *r_test_thread_main(void *opaque_context) {
    RTestAtan2ThreadContext *context = opaque_context;
    int iteration;

    if (r_test_seed_environment(context->rounding, context->exceptions, context->native_errno) !=
        0) {
        context->failed = 1;
        return NULL;
    }
    for (iteration = 0; iteration < 500; iteration += 1) {
        RStdMathF32Result f32 = r_std_math_atan2_f32(1.0F, -1.0F);
        RStdMathF64Result f64 = r_std_math_atan2_f64(1.0, -1.0);
        RStdMathCFloatResult c_float = r_std_math_atan2_c_float(1.0F, -1.0F);
        RStdMathCDoubleResult c_double = r_std_math_atan2_c_double(1.0, -1.0);
        RStdMathCLongDoubleResult c_long_double = r_std_math_atan2_c_long_double(1.0L, -1.0L);

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
            !r_test_environment_is(context->rounding, context->exceptions, context->native_errno)) {
            context->failed = 1;
            return NULL;
        }
    }
    return NULL;
}

static int r_test_environment_and_concurrency(void) {
    RTestAtan2ThreadContext first;
    RTestAtan2ThreadContext second;
    pthread_t first_thread;
    pthread_t second_thread;
    float expected_float;
    double expected_double;
    long double expected_long_double;

    R_TEST_CHECK(r_test_native_f32(1.0F, -1.0F, &expected_float) == 0);
    R_TEST_CHECK(r_test_native_f64(1.0, -1.0, &expected_double) == 0);
    R_TEST_CHECK(r_test_native_c_long_double(1.0L, -1.0L, &expected_long_double) == 0);
    first = (RTestAtan2ThreadContext){
        FE_DOWNWARD, FE_DIVBYZERO, EDOM, expected_float, expected_double, expected_long_double, 0};
    second = (RTestAtan2ThreadContext){
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
    R_TEST_CHECK(r_std_math_atan2_f32(1.0F, -1.0F).status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_std_math_atan2_f64(1.0, -1.0).status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_std_math_atan2_c_float(1.0F, -1.0F).status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_std_math_atan2_c_double(1.0, -1.0).status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_std_math_atan2_c_long_double(1.0L, -1.0L).status == R_STD_MATH_CALL_SUCCESS);
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
    R_TEST_CHECK(r_test_quadrants_and_signed_zero() == 0);
    R_TEST_CHECK(r_test_infinities() == 0);
    R_TEST_CHECK(r_test_nan() == 0);
    R_TEST_CHECK(r_test_underflow() == 0);
    R_TEST_CHECK(r_test_environment_is(FE_UPWARD, FE_DIVBYZERO, E2BIG));
    R_TEST_CHECK(r_test_environment_and_concurrency() == 0);
    R_TEST_CHECK(r_test_allocation_free() == 0);
    R_TEST_CHECK(fesetenv(&original_environment) == 0);
    errno = original_errno;
    (void)fprintf(stdout, "library_math_atan2_tests: ok\n");
    return 0;
}

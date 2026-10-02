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

static int r_test_caller_environment_matches(int rounding, int exceptions, int native_errno) {
    R_TEST_CHECK(fegetround() == rounding);
    R_TEST_CHECK((fetestexcept(FE_ALL_EXCEPT) & FE_ALL_EXCEPT) == exceptions);
    R_TEST_CHECK(errno == native_errno);
    return 0;
}

static float r_test_signaling_nan_f32_value(void) {
    uint32_t bits = UINT32_C(0x7f800001);
    float value;

    (void)memcpy(&value, &bits, sizeof(value));
    return value;
}

static float r_test_f32_value_from_bits(uint32_t bits) {
    float value;

    (void)memcpy(&value, &bits, sizeof(value));
    return value;
}

static double r_test_signaling_nan_f64_value(void) {
    uint64_t bits = UINT64_C(0x7ff0000000000001);
    double value;

    (void)memcpy(&value, &bits, sizeof(value));
    return value;
}

static double r_test_f64_value_from_bits(uint64_t bits) {
    double value;

    (void)memcpy(&value, &bits, sizeof(value));
    return value;
}

static long double r_test_c_long_double_value_from_bits(uint64_t bits) {
    long double value;

    _Static_assert(sizeof(value) == sizeof(bits), "binary64 long double size");
    (void)memcpy(&value, &bits, sizeof(value));
    return value;
}

static long double r_test_signaling_nan_c_long_double_value(void) {
#if (LDBL_MANT_DIG == 53) && (LDBL_MAX_EXP == 1024)
    uint64_t bits = UINT64_C(0x7ff0000000000001);
    long double value;

    _Static_assert(sizeof(value) == sizeof(bits), "binary64 long double size");
    (void)memcpy(&value, &bits, sizeof(value));
    return value;
#else
    return nanl("1");
#endif
}

static _Bool r_test_is_nan_f32_bits(float value) {
    uint32_t bits;

    (void)memcpy(&bits, &value, sizeof(bits));
    return ((bits & UINT32_C(0x7f800000)) == UINT32_C(0x7f800000)) &&
           ((bits & UINT32_C(0x007fffff)) != UINT32_C(0));
}

static _Bool r_test_is_nan_f64_bits(double value) {
    uint64_t bits;

    (void)memcpy(&bits, &value, sizeof(bits));
    return ((bits & UINT64_C(0x7ff0000000000000)) == UINT64_C(0x7ff0000000000000)) &&
           ((bits & UINT64_C(0x000fffffffffffff)) != UINT64_C(0));
}

static _Bool r_test_is_nan_c_long_double_bits(long double value) {
#if (LDBL_MANT_DIG == 53) && (LDBL_MAX_EXP == 1024)
    uint64_t bits;

    _Static_assert(sizeof(value) == sizeof(bits), "binary64 long double size");
    (void)memcpy(&bits, &value, sizeof(bits));
    return ((bits & UINT64_C(0x7ff0000000000000)) == UINT64_C(0x7ff0000000000000)) &&
           ((bits & UINT64_C(0x000fffffffffffff)) != UINT64_C(0));
#else
    return isnan(value) != 0;
#endif
}

static _Bool r_test_same_f32_bits(float left, float right) {
    uint32_t left_bits;
    uint32_t right_bits;

    (void)memcpy(&left_bits, &left, sizeof(left_bits));
    (void)memcpy(&right_bits, &right, sizeof(right_bits));
    return left_bits == right_bits;
}

static _Bool r_test_same_f64_bits(double left, double right) {
    uint64_t left_bits;
    uint64_t right_bits;

    (void)memcpy(&left_bits, &left, sizeof(left_bits));
    (void)memcpy(&right_bits, &right, sizeof(right_bits));
    return left_bits == right_bits;
}

static _Bool r_test_same_c_long_double_bits(long double left, long double right) {
    uint64_t left_bits;
    uint64_t right_bits;

    _Static_assert(sizeof(left) == sizeof(left_bits), "binary64 long double size");
    (void)memcpy(&left_bits, &left, sizeof(left_bits));
    (void)memcpy(&right_bits, &right, sizeof(right_bits));
    return left_bits == right_bits;
}

typedef struct RTestMathThreadContext {
    int rounding;
    int exceptions;
    int native_errno;
    int failed;
} RTestMathThreadContext;

static void *r_test_math_thread_main(void *opaque_context) {
    RTestMathThreadContext *context = opaque_context;
    int iteration;

    if ((fesetround(context->rounding) != 0) || (feclearexcept(FE_ALL_EXCEPT) != 0) ||
        (feraiseexcept(context->exceptions) != 0)) {
        context->failed = 1;
        return NULL;
    }
    errno = context->native_errno;
    for (iteration = 0; iteration < 500; ++iteration) {
        long double adjacent = r_std_math_next_after_c_long_double(LDBL_MAX, (long double)INFINITY);
        RStdMathBinaryPartsF64 binary_parts = r_std_math_split_binary_f64(DBL_TRUE_MIN);
        RStdMathF64Result composed =
            r_std_math_compose_binary_f64(binary_parts.fraction, binary_parts.exponent);

        if ((r_std_math_round_c_float(2.5F) != 3.0F) || (r_std_math_round_c_double(-2.5) != -3.0) ||
            (r_std_math_min_f64(NAN, 4.0) != 4.0) || !r_std_math_is_nan_c_double(NAN) ||
            !isinf(adjacent) || signbit(adjacent) || (composed.status != R_STD_MATH_CALL_SUCCESS) ||
            (composed.value != DBL_TRUE_MIN) || (fegetround() != context->rounding) ||
            ((fetestexcept(FE_ALL_EXCEPT) & FE_ALL_EXCEPT) != context->exceptions) ||
            (errno != context->native_errno)) {
            context->failed = 1;
            return NULL;
        }
    }
    return NULL;
}

static int r_test_concurrent_environments(void) {
    pthread_t first_thread;
    pthread_t second_thread;
    RTestMathThreadContext first = {FE_DOWNWARD, FE_DIVBYZERO, EDOM, 0};
    RTestMathThreadContext second = {FE_UPWARD, FE_INVALID, ERANGE, 0};

    R_TEST_CHECK(pthread_create(&first_thread, NULL, r_test_math_thread_main, &first) == 0);
    R_TEST_CHECK(pthread_create(&second_thread, NULL, r_test_math_thread_main, &second) == 0);
    R_TEST_CHECK(pthread_join(first_thread, NULL) == 0);
    R_TEST_CHECK(pthread_join(second_thread, NULL) == 0);
    R_TEST_CHECK(first.failed == 0);
    R_TEST_CHECK(second.failed == 0);
    return 0;
}

static int r_test_non_failing_c_float(void) {
    int original_rounding = fegetround();
    float adjacent;
    float result;

    R_TEST_CHECK(original_rounding != -1);
    R_TEST_CHECK(fesetround(FE_DOWNWARD) == 0);
    R_TEST_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    R_TEST_CHECK(feraiseexcept(FE_DIVBYZERO) == 0);
    errno = E2BIG;

    R_TEST_CHECK(r_std_math_abs_c_float((float)-3.5) == (float)3.5);
    R_TEST_CHECK(r_std_math_floor_c_float((float)-1.25) == (float)-2.0);
    R_TEST_CHECK(r_std_math_ceil_c_float((float)-1.25) == (float)-1.0);
    R_TEST_CHECK(r_std_math_trunc_c_float((float)-1.75) == (float)-1.0);
    R_TEST_CHECK(r_std_math_round_c_float((float)2.5) == (float)3.0);
    R_TEST_CHECK(r_std_math_round_c_float((float)-2.5) == (float)-3.0);
    R_TEST_CHECK(signbit(r_std_math_copy_sign_c_float((float)1.0, (float)-0.0)));

    R_TEST_CHECK(r_std_math_min_c_float((float)NAN, (float)4.0) == (float)4.0);
    R_TEST_CHECK(r_std_math_min_c_float((float)4.0, (float)NAN) == (float)4.0);
    R_TEST_CHECK(r_std_math_max_c_float((float)NAN, (float)4.0) == (float)4.0);
    R_TEST_CHECK(r_std_math_max_c_float((float)4.0, (float)NAN) == (float)4.0);
    R_TEST_CHECK(isnan(r_std_math_min_c_float((float)NAN, (float)NAN)));
    R_TEST_CHECK(isnan(r_std_math_max_c_float((float)NAN, (float)NAN)));
    R_TEST_CHECK(signbit(r_std_math_min_c_float((float)-0.0, (float)0.0)));
    R_TEST_CHECK(signbit(r_std_math_min_c_float((float)0.0, (float)-0.0)));
    R_TEST_CHECK(!signbit(r_std_math_max_c_float((float)-0.0, (float)0.0)));
    R_TEST_CHECK(!signbit(r_std_math_max_c_float((float)0.0, (float)-0.0)));

    adjacent = r_std_math_next_after_c_float((float)1.0, (float)2.0);
    R_TEST_CHECK(adjacent > (float)1.0);
    R_TEST_CHECK(r_std_math_next_after_c_float((float)1.0, (float)1.0) == (float)1.0);
    result = r_std_math_next_after_c_float((float)FLT_MAX, (float)INFINITY);
    R_TEST_CHECK(isinf(result));
    R_TEST_CHECK(!signbit(result));
    result = r_std_math_next_after_c_float((float)-FLT_MAX, (float)-INFINITY);
    R_TEST_CHECK(isinf(result));
    R_TEST_CHECK(signbit(result));
    result = r_std_math_next_after_c_float((float)0.0, (float)-0.0);
    R_TEST_CHECK(signbit(result));

    R_TEST_CHECK(r_std_math_is_finite_c_float((float)1.0));
    R_TEST_CHECK(!r_std_math_is_finite_c_float((float)INFINITY));
    R_TEST_CHECK(r_std_math_is_infinite_c_float((float)-INFINITY));
    R_TEST_CHECK(r_std_math_is_nan_c_float((float)NAN));
    R_TEST_CHECK(r_std_math_is_normal_c_float((float)FLT_MIN));
    R_TEST_CHECK(!r_std_math_is_normal_c_float((float)FLT_TRUE_MIN));
    R_TEST_CHECK(r_std_math_sign_bit_c_float((float)-0.0));
    R_TEST_CHECK(!r_std_math_sign_bit_c_float((float)0.0));

    R_TEST_CHECK(r_test_caller_environment_matches(FE_DOWNWARD, FE_DIVBYZERO, E2BIG) == 0);
    R_TEST_CHECK(fesetround(original_rounding) == 0);
    R_TEST_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    return 0;
}

static int r_test_non_failing_c_double(void) {
    int original_rounding = fegetround();
    double adjacent;
    double result;

    R_TEST_CHECK(original_rounding != -1);
    R_TEST_CHECK(fesetround(FE_DOWNWARD) == 0);
    R_TEST_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    R_TEST_CHECK(feraiseexcept(FE_DIVBYZERO) == 0);
    errno = E2BIG;

    R_TEST_CHECK(r_std_math_abs_c_double((double)-3.5) == (double)3.5);
    R_TEST_CHECK(r_std_math_floor_c_double((double)-1.25) == (double)-2.0);
    R_TEST_CHECK(r_std_math_ceil_c_double((double)-1.25) == (double)-1.0);
    R_TEST_CHECK(r_std_math_trunc_c_double((double)-1.75) == (double)-1.0);
    R_TEST_CHECK(r_std_math_round_c_double((double)2.5) == (double)3.0);
    R_TEST_CHECK(r_std_math_round_c_double((double)-2.5) == (double)-3.0);
    R_TEST_CHECK(signbit(r_std_math_copy_sign_c_double((double)1.0, (double)-0.0)));

    R_TEST_CHECK(r_std_math_min_c_double((double)NAN, (double)4.0) == (double)4.0);
    R_TEST_CHECK(r_std_math_min_c_double((double)4.0, (double)NAN) == (double)4.0);
    R_TEST_CHECK(r_std_math_max_c_double((double)NAN, (double)4.0) == (double)4.0);
    R_TEST_CHECK(r_std_math_max_c_double((double)4.0, (double)NAN) == (double)4.0);
    R_TEST_CHECK(isnan(r_std_math_min_c_double((double)NAN, (double)NAN)));
    R_TEST_CHECK(isnan(r_std_math_max_c_double((double)NAN, (double)NAN)));
    R_TEST_CHECK(signbit(r_std_math_min_c_double((double)-0.0, (double)0.0)));
    R_TEST_CHECK(signbit(r_std_math_min_c_double((double)0.0, (double)-0.0)));
    R_TEST_CHECK(!signbit(r_std_math_max_c_double((double)-0.0, (double)0.0)));
    R_TEST_CHECK(!signbit(r_std_math_max_c_double((double)0.0, (double)-0.0)));

    adjacent = r_std_math_next_after_c_double((double)1.0, (double)2.0);
    R_TEST_CHECK(adjacent > (double)1.0);
    R_TEST_CHECK(r_std_math_next_after_c_double((double)1.0, (double)1.0) == (double)1.0);
    result = r_std_math_next_after_c_double((double)DBL_MAX, (double)INFINITY);
    R_TEST_CHECK(isinf(result));
    R_TEST_CHECK(!signbit(result));
    result = r_std_math_next_after_c_double((double)-DBL_MAX, (double)-INFINITY);
    R_TEST_CHECK(isinf(result));
    R_TEST_CHECK(signbit(result));
    result = r_std_math_next_after_c_double((double)0.0, (double)-0.0);
    R_TEST_CHECK(signbit(result));

    R_TEST_CHECK(r_std_math_is_finite_c_double((double)1.0));
    R_TEST_CHECK(!r_std_math_is_finite_c_double((double)INFINITY));
    R_TEST_CHECK(r_std_math_is_infinite_c_double((double)-INFINITY));
    R_TEST_CHECK(r_std_math_is_nan_c_double((double)NAN));
    R_TEST_CHECK(r_std_math_is_normal_c_double((double)DBL_MIN));
    R_TEST_CHECK(!r_std_math_is_normal_c_double((double)DBL_TRUE_MIN));
    R_TEST_CHECK(r_std_math_sign_bit_c_double((double)-0.0));
    R_TEST_CHECK(!r_std_math_sign_bit_c_double((double)0.0));

    R_TEST_CHECK(r_test_caller_environment_matches(FE_DOWNWARD, FE_DIVBYZERO, E2BIG) == 0);
    R_TEST_CHECK(fesetround(original_rounding) == 0);
    R_TEST_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    return 0;
}

static int r_test_non_failing_c_long_double(void) {
    int original_rounding = fegetround();
    long double adjacent;
    long double result;

    R_TEST_CHECK(original_rounding != -1);
    R_TEST_CHECK(fesetround(FE_DOWNWARD) == 0);
    R_TEST_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    R_TEST_CHECK(feraiseexcept(FE_DIVBYZERO) == 0);
    errno = E2BIG;

    R_TEST_CHECK(r_std_math_abs_c_long_double((long double)-3.5) == (long double)3.5);
    R_TEST_CHECK(r_std_math_floor_c_long_double((long double)-1.25) == (long double)-2.0);
    R_TEST_CHECK(r_std_math_ceil_c_long_double((long double)-1.25) == (long double)-1.0);
    R_TEST_CHECK(r_std_math_trunc_c_long_double((long double)-1.75) == (long double)-1.0);
    R_TEST_CHECK(r_std_math_round_c_long_double((long double)2.5) == (long double)3.0);
    R_TEST_CHECK(r_std_math_round_c_long_double((long double)-2.5) == (long double)-3.0);
    R_TEST_CHECK(signbit(r_std_math_copy_sign_c_long_double((long double)1.0, (long double)-0.0)));

    R_TEST_CHECK(r_std_math_min_c_long_double((long double)NAN, (long double)4.0) ==
                 (long double)4.0);
    R_TEST_CHECK(r_std_math_min_c_long_double((long double)4.0, (long double)NAN) ==
                 (long double)4.0);
    R_TEST_CHECK(r_std_math_max_c_long_double((long double)NAN, (long double)4.0) ==
                 (long double)4.0);
    R_TEST_CHECK(r_std_math_max_c_long_double((long double)4.0, (long double)NAN) ==
                 (long double)4.0);
    R_TEST_CHECK(isnan(r_std_math_min_c_long_double((long double)NAN, (long double)NAN)));
    R_TEST_CHECK(isnan(r_std_math_max_c_long_double((long double)NAN, (long double)NAN)));
    R_TEST_CHECK(signbit(r_std_math_min_c_long_double((long double)-0.0, (long double)0.0)));
    R_TEST_CHECK(signbit(r_std_math_min_c_long_double((long double)0.0, (long double)-0.0)));
    R_TEST_CHECK(!signbit(r_std_math_max_c_long_double((long double)-0.0, (long double)0.0)));
    R_TEST_CHECK(!signbit(r_std_math_max_c_long_double((long double)0.0, (long double)-0.0)));

    adjacent = r_std_math_next_after_c_long_double((long double)1.0, (long double)2.0);
    R_TEST_CHECK(adjacent > (long double)1.0);
    R_TEST_CHECK(r_std_math_next_after_c_long_double((long double)1.0, (long double)1.0) ==
                 (long double)1.0);
    result = r_std_math_next_after_c_long_double((long double)LDBL_MAX, (long double)INFINITY);
    R_TEST_CHECK(isinf(result));
    R_TEST_CHECK(!signbit(result));
    result = r_std_math_next_after_c_long_double((long double)-LDBL_MAX, (long double)-INFINITY);
    R_TEST_CHECK(isinf(result));
    R_TEST_CHECK(signbit(result));
    result = r_std_math_next_after_c_long_double((long double)0.0, (long double)-0.0);
    R_TEST_CHECK(signbit(result));

    R_TEST_CHECK(r_std_math_is_finite_c_long_double((long double)1.0));
    R_TEST_CHECK(!r_std_math_is_finite_c_long_double((long double)INFINITY));
    R_TEST_CHECK(r_std_math_is_infinite_c_long_double((long double)-INFINITY));
    R_TEST_CHECK(r_std_math_is_nan_c_long_double((long double)NAN));
    R_TEST_CHECK(r_std_math_is_normal_c_long_double((long double)LDBL_MIN));
    R_TEST_CHECK(!r_std_math_is_normal_c_long_double((long double)LDBL_TRUE_MIN));
    R_TEST_CHECK(r_std_math_sign_bit_c_long_double((long double)-0.0));
    R_TEST_CHECK(!r_std_math_sign_bit_c_long_double((long double)0.0));

    R_TEST_CHECK(r_test_caller_environment_matches(FE_DOWNWARD, FE_DIVBYZERO, E2BIG) == 0);
    R_TEST_CHECK(fesetround(original_rounding) == 0);
    R_TEST_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    return 0;
}

static int r_test_signaling_nan_f32(void) {
    int original_rounding = fegetround();
    float signaling_nan = r_test_signaling_nan_f32_value();
    float result;
    _Bool predicate;

    R_TEST_CHECK(original_rounding != -1);
    R_TEST_CHECK(fesetround(FE_TOWARDZERO) == 0);
    R_TEST_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    R_TEST_CHECK(feraiseexcept(FE_DIVBYZERO) == 0);
    errno = EILSEQ;

    result = r_std_math_abs_f32(signaling_nan);
    R_TEST_CHECK(r_test_caller_environment_matches(FE_TOWARDZERO, FE_DIVBYZERO, EILSEQ) == 0);
    R_TEST_CHECK(r_test_is_nan_f32_bits(result));
    result = r_std_math_floor_f32(signaling_nan);
    R_TEST_CHECK(r_test_is_nan_f32_bits(result));
    result = r_std_math_ceil_f32(signaling_nan);
    R_TEST_CHECK(r_test_is_nan_f32_bits(result));
    result = r_std_math_trunc_f32(signaling_nan);
    R_TEST_CHECK(r_test_is_nan_f32_bits(result));
    result = r_std_math_round_f32(signaling_nan);
    R_TEST_CHECK(r_test_is_nan_f32_bits(result));
    result = r_std_math_copy_sign_f32(signaling_nan, (float)-1.0);
    R_TEST_CHECK(r_test_is_nan_f32_bits(result));

    result = r_std_math_min_f32(signaling_nan, (float)4.0);
    R_TEST_CHECK(result == (float)4.0);
    result = r_std_math_min_f32((float)4.0, signaling_nan);
    R_TEST_CHECK(result == (float)4.0);
    result = r_std_math_max_f32(signaling_nan, (float)4.0);
    R_TEST_CHECK(result == (float)4.0);
    result = r_std_math_max_f32((float)4.0, signaling_nan);
    R_TEST_CHECK(result == (float)4.0);
    result = r_std_math_min_f32(signaling_nan, signaling_nan);
    R_TEST_CHECK(r_test_is_nan_f32_bits(result));
    result = r_std_math_max_f32(signaling_nan, signaling_nan);
    R_TEST_CHECK(r_test_is_nan_f32_bits(result));
    result = r_std_math_next_after_f32(signaling_nan, (float)1.0);
    R_TEST_CHECK(r_test_is_nan_f32_bits(result));

    predicate = r_std_math_is_finite_f32(signaling_nan);
    R_TEST_CHECK(!predicate);
    predicate = r_std_math_is_infinite_f32(signaling_nan);
    R_TEST_CHECK(!predicate);
    predicate = r_std_math_is_nan_f32(signaling_nan);
    R_TEST_CHECK(predicate);
    predicate = r_std_math_is_normal_f32(signaling_nan);
    R_TEST_CHECK(!predicate);
    predicate = r_std_math_sign_bit_f32(signaling_nan);
    R_TEST_CHECK(!predicate);

    R_TEST_CHECK(r_test_caller_environment_matches(FE_TOWARDZERO, FE_DIVBYZERO, EILSEQ) == 0);
    R_TEST_CHECK(fesetround(original_rounding) == 0);
    R_TEST_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    return 0;
}

static int r_test_signaling_nan_f64(void) {
    int original_rounding = fegetround();
    double signaling_nan = r_test_signaling_nan_f64_value();
    double result;
    _Bool predicate;

    R_TEST_CHECK(original_rounding != -1);
    R_TEST_CHECK(fesetround(FE_TOWARDZERO) == 0);
    R_TEST_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    R_TEST_CHECK(feraiseexcept(FE_DIVBYZERO) == 0);
    errno = EILSEQ;

    result = r_std_math_abs_f64(signaling_nan);
    R_TEST_CHECK(r_test_caller_environment_matches(FE_TOWARDZERO, FE_DIVBYZERO, EILSEQ) == 0);
    R_TEST_CHECK(r_test_is_nan_f64_bits(result));
    result = r_std_math_floor_f64(signaling_nan);
    R_TEST_CHECK(r_test_is_nan_f64_bits(result));
    result = r_std_math_ceil_f64(signaling_nan);
    R_TEST_CHECK(r_test_is_nan_f64_bits(result));
    result = r_std_math_trunc_f64(signaling_nan);
    R_TEST_CHECK(r_test_is_nan_f64_bits(result));
    result = r_std_math_round_f64(signaling_nan);
    R_TEST_CHECK(r_test_is_nan_f64_bits(result));
    result = r_std_math_copy_sign_f64(signaling_nan, (double)-1.0);
    R_TEST_CHECK(r_test_is_nan_f64_bits(result));

    result = r_std_math_min_f64(signaling_nan, (double)4.0);
    R_TEST_CHECK(result == (double)4.0);
    result = r_std_math_min_f64((double)4.0, signaling_nan);
    R_TEST_CHECK(result == (double)4.0);
    result = r_std_math_max_f64(signaling_nan, (double)4.0);
    R_TEST_CHECK(result == (double)4.0);
    result = r_std_math_max_f64((double)4.0, signaling_nan);
    R_TEST_CHECK(result == (double)4.0);
    result = r_std_math_min_f64(signaling_nan, signaling_nan);
    R_TEST_CHECK(r_test_is_nan_f64_bits(result));
    result = r_std_math_max_f64(signaling_nan, signaling_nan);
    R_TEST_CHECK(r_test_is_nan_f64_bits(result));
    result = r_std_math_next_after_f64(signaling_nan, (double)1.0);
    R_TEST_CHECK(r_test_is_nan_f64_bits(result));

    predicate = r_std_math_is_finite_f64(signaling_nan);
    R_TEST_CHECK(!predicate);
    predicate = r_std_math_is_infinite_f64(signaling_nan);
    R_TEST_CHECK(!predicate);
    predicate = r_std_math_is_nan_f64(signaling_nan);
    R_TEST_CHECK(predicate);
    predicate = r_std_math_is_normal_f64(signaling_nan);
    R_TEST_CHECK(!predicate);
    predicate = r_std_math_sign_bit_f64(signaling_nan);
    R_TEST_CHECK(!predicate);

    R_TEST_CHECK(r_test_caller_environment_matches(FE_TOWARDZERO, FE_DIVBYZERO, EILSEQ) == 0);
    R_TEST_CHECK(fesetround(original_rounding) == 0);
    R_TEST_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    return 0;
}

static int r_test_signaling_nan_c_float(void) {
    int original_rounding = fegetround();
    float signaling_nan = r_test_signaling_nan_f32_value();
    float result;
    _Bool predicate;

    R_TEST_CHECK(original_rounding != -1);
    R_TEST_CHECK(fesetround(FE_TOWARDZERO) == 0);
    R_TEST_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    R_TEST_CHECK(feraiseexcept(FE_DIVBYZERO) == 0);
    errno = EILSEQ;

    result = r_std_math_abs_c_float(signaling_nan);
    R_TEST_CHECK(r_test_caller_environment_matches(FE_TOWARDZERO, FE_DIVBYZERO, EILSEQ) == 0);
    R_TEST_CHECK(r_test_is_nan_f32_bits(result));
    result = r_std_math_floor_c_float(signaling_nan);
    R_TEST_CHECK(r_test_is_nan_f32_bits(result));
    result = r_std_math_ceil_c_float(signaling_nan);
    R_TEST_CHECK(r_test_is_nan_f32_bits(result));
    result = r_std_math_trunc_c_float(signaling_nan);
    R_TEST_CHECK(r_test_is_nan_f32_bits(result));
    result = r_std_math_round_c_float(signaling_nan);
    R_TEST_CHECK(r_test_is_nan_f32_bits(result));
    result = r_std_math_copy_sign_c_float(signaling_nan, (float)-1.0);
    R_TEST_CHECK(r_test_is_nan_f32_bits(result));

    result = r_std_math_min_c_float(signaling_nan, (float)4.0);
    R_TEST_CHECK(result == (float)4.0);
    result = r_std_math_min_c_float((float)4.0, signaling_nan);
    R_TEST_CHECK(result == (float)4.0);
    result = r_std_math_max_c_float(signaling_nan, (float)4.0);
    R_TEST_CHECK(result == (float)4.0);
    result = r_std_math_max_c_float((float)4.0, signaling_nan);
    R_TEST_CHECK(result == (float)4.0);
    result = r_std_math_min_c_float(signaling_nan, signaling_nan);
    R_TEST_CHECK(r_test_is_nan_f32_bits(result));
    result = r_std_math_max_c_float(signaling_nan, signaling_nan);
    R_TEST_CHECK(r_test_is_nan_f32_bits(result));
    result = r_std_math_next_after_c_float(signaling_nan, (float)1.0);
    R_TEST_CHECK(r_test_is_nan_f32_bits(result));

    predicate = r_std_math_is_finite_c_float(signaling_nan);
    R_TEST_CHECK(!predicate);
    predicate = r_std_math_is_infinite_c_float(signaling_nan);
    R_TEST_CHECK(!predicate);
    predicate = r_std_math_is_nan_c_float(signaling_nan);
    R_TEST_CHECK(predicate);
    predicate = r_std_math_is_normal_c_float(signaling_nan);
    R_TEST_CHECK(!predicate);
    predicate = r_std_math_sign_bit_c_float(signaling_nan);
    R_TEST_CHECK(!predicate);

    R_TEST_CHECK(r_test_caller_environment_matches(FE_TOWARDZERO, FE_DIVBYZERO, EILSEQ) == 0);
    R_TEST_CHECK(fesetround(original_rounding) == 0);
    R_TEST_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    return 0;
}

static int r_test_signaling_nan_c_double(void) {
    int original_rounding = fegetround();
    double signaling_nan = r_test_signaling_nan_f64_value();
    double result;
    _Bool predicate;

    R_TEST_CHECK(original_rounding != -1);
    R_TEST_CHECK(fesetround(FE_TOWARDZERO) == 0);
    R_TEST_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    R_TEST_CHECK(feraiseexcept(FE_DIVBYZERO) == 0);
    errno = EILSEQ;

    result = r_std_math_abs_c_double(signaling_nan);
    R_TEST_CHECK(r_test_caller_environment_matches(FE_TOWARDZERO, FE_DIVBYZERO, EILSEQ) == 0);
    R_TEST_CHECK(r_test_is_nan_f64_bits(result));
    result = r_std_math_floor_c_double(signaling_nan);
    R_TEST_CHECK(r_test_is_nan_f64_bits(result));
    result = r_std_math_ceil_c_double(signaling_nan);
    R_TEST_CHECK(r_test_is_nan_f64_bits(result));
    result = r_std_math_trunc_c_double(signaling_nan);
    R_TEST_CHECK(r_test_is_nan_f64_bits(result));
    result = r_std_math_round_c_double(signaling_nan);
    R_TEST_CHECK(r_test_is_nan_f64_bits(result));
    result = r_std_math_copy_sign_c_double(signaling_nan, (double)-1.0);
    R_TEST_CHECK(r_test_is_nan_f64_bits(result));

    result = r_std_math_min_c_double(signaling_nan, (double)4.0);
    R_TEST_CHECK(result == (double)4.0);
    result = r_std_math_min_c_double((double)4.0, signaling_nan);
    R_TEST_CHECK(result == (double)4.0);
    result = r_std_math_max_c_double(signaling_nan, (double)4.0);
    R_TEST_CHECK(result == (double)4.0);
    result = r_std_math_max_c_double((double)4.0, signaling_nan);
    R_TEST_CHECK(result == (double)4.0);
    result = r_std_math_min_c_double(signaling_nan, signaling_nan);
    R_TEST_CHECK(r_test_is_nan_f64_bits(result));
    result = r_std_math_max_c_double(signaling_nan, signaling_nan);
    R_TEST_CHECK(r_test_is_nan_f64_bits(result));
    result = r_std_math_next_after_c_double(signaling_nan, (double)1.0);
    R_TEST_CHECK(r_test_is_nan_f64_bits(result));

    predicate = r_std_math_is_finite_c_double(signaling_nan);
    R_TEST_CHECK(!predicate);
    predicate = r_std_math_is_infinite_c_double(signaling_nan);
    R_TEST_CHECK(!predicate);
    predicate = r_std_math_is_nan_c_double(signaling_nan);
    R_TEST_CHECK(predicate);
    predicate = r_std_math_is_normal_c_double(signaling_nan);
    R_TEST_CHECK(!predicate);
    predicate = r_std_math_sign_bit_c_double(signaling_nan);
    R_TEST_CHECK(!predicate);

    R_TEST_CHECK(r_test_caller_environment_matches(FE_TOWARDZERO, FE_DIVBYZERO, EILSEQ) == 0);
    R_TEST_CHECK(fesetround(original_rounding) == 0);
    R_TEST_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    return 0;
}

static int r_test_signaling_nan_c_long_double(void) {
    int original_rounding = fegetround();
    long double signaling_nan = r_test_signaling_nan_c_long_double_value();
    long double result;
    _Bool predicate;

    R_TEST_CHECK(original_rounding != -1);
    R_TEST_CHECK(fesetround(FE_TOWARDZERO) == 0);
    R_TEST_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    R_TEST_CHECK(feraiseexcept(FE_DIVBYZERO) == 0);
    errno = EILSEQ;

    result = r_std_math_abs_c_long_double(signaling_nan);
    R_TEST_CHECK(r_test_caller_environment_matches(FE_TOWARDZERO, FE_DIVBYZERO, EILSEQ) == 0);
    R_TEST_CHECK(r_test_is_nan_c_long_double_bits(result));
    result = r_std_math_floor_c_long_double(signaling_nan);
    R_TEST_CHECK(r_test_is_nan_c_long_double_bits(result));
    result = r_std_math_ceil_c_long_double(signaling_nan);
    R_TEST_CHECK(r_test_is_nan_c_long_double_bits(result));
    result = r_std_math_trunc_c_long_double(signaling_nan);
    R_TEST_CHECK(r_test_is_nan_c_long_double_bits(result));
    result = r_std_math_round_c_long_double(signaling_nan);
    R_TEST_CHECK(r_test_is_nan_c_long_double_bits(result));
    result = r_std_math_copy_sign_c_long_double(signaling_nan, (long double)-1.0);
    R_TEST_CHECK(r_test_is_nan_c_long_double_bits(result));

    result = r_std_math_min_c_long_double(signaling_nan, (long double)4.0);
    R_TEST_CHECK(result == (long double)4.0);
    result = r_std_math_min_c_long_double((long double)4.0, signaling_nan);
    R_TEST_CHECK(result == (long double)4.0);
    result = r_std_math_max_c_long_double(signaling_nan, (long double)4.0);
    R_TEST_CHECK(result == (long double)4.0);
    result = r_std_math_max_c_long_double((long double)4.0, signaling_nan);
    R_TEST_CHECK(result == (long double)4.0);
    result = r_std_math_min_c_long_double(signaling_nan, signaling_nan);
    R_TEST_CHECK(r_test_is_nan_c_long_double_bits(result));
    result = r_std_math_max_c_long_double(signaling_nan, signaling_nan);
    R_TEST_CHECK(r_test_is_nan_c_long_double_bits(result));
    result = r_std_math_next_after_c_long_double(signaling_nan, (long double)1.0);
    R_TEST_CHECK(r_test_is_nan_c_long_double_bits(result));

    predicate = r_std_math_is_finite_c_long_double(signaling_nan);
    R_TEST_CHECK(!predicate);
    predicate = r_std_math_is_infinite_c_long_double(signaling_nan);
    R_TEST_CHECK(!predicate);
    predicate = r_std_math_is_nan_c_long_double(signaling_nan);
    R_TEST_CHECK(predicate);
    predicate = r_std_math_is_normal_c_long_double(signaling_nan);
    R_TEST_CHECK(!predicate);
    predicate = r_std_math_sign_bit_c_long_double(signaling_nan);
    R_TEST_CHECK(!predicate);

    R_TEST_CHECK(r_test_caller_environment_matches(FE_TOWARDZERO, FE_DIVBYZERO, EILSEQ) == 0);
    R_TEST_CHECK(fesetround(original_rounding) == 0);
    R_TEST_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    return 0;
}

static int r_test_parts_f32(void) {
    int original_rounding = fegetround();
    float signaling_nan = r_test_signaling_nan_f32_value();
    float negative_zero = (float)-0.0;
    RStdMathFractionPartsF32 fraction_parts;
    RStdMathFractionPartsF32 fraction_copy;
    RStdMathBinaryPartsF32 binary_parts;
    RStdMathBinaryPartsF32 binary_copy;
    RStdMathF32Result composed;

    R_TEST_CHECK(original_rounding != -1);
    R_TEST_CHECK(fesetround(FE_UPWARD) == 0);
    R_TEST_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    R_TEST_CHECK(feraiseexcept(FE_DIVBYZERO) == 0);
    errno = EILSEQ;

    fraction_parts = r_std_math_split_fraction_f32((float)3.75);
    R_TEST_CHECK(fraction_parts.whole == (float)3.0);
    R_TEST_CHECK(fraction_parts.fraction == (float)0.75);
    fraction_copy = fraction_parts;
    R_TEST_CHECK(fraction_copy.whole == fraction_parts.whole);
    R_TEST_CHECK(fraction_copy.fraction == fraction_parts.fraction);
    fraction_parts = r_std_math_split_fraction_f32((float)-3.75);
    R_TEST_CHECK(fraction_parts.whole == (float)-3.0);
    R_TEST_CHECK(fraction_parts.fraction == (float)-0.75);
    fraction_parts = r_std_math_split_fraction_f32(negative_zero);
    R_TEST_CHECK(r_test_same_f32_bits(fraction_parts.whole, negative_zero));
    R_TEST_CHECK(r_test_same_f32_bits(fraction_parts.fraction, negative_zero));
    fraction_parts = r_std_math_split_fraction_f32((float)-INFINITY);
    R_TEST_CHECK(isinf(fraction_parts.whole));
    R_TEST_CHECK(signbit(fraction_parts.whole));
    R_TEST_CHECK(fraction_parts.fraction == (float)0.0);
    R_TEST_CHECK(signbit(fraction_parts.fraction));
    fraction_parts = r_std_math_split_fraction_f32((float)NAN);
    R_TEST_CHECK(r_test_is_nan_f32_bits(fraction_parts.whole));
    R_TEST_CHECK(r_test_is_nan_f32_bits(fraction_parts.fraction));
    fraction_parts = r_std_math_split_fraction_f32(signaling_nan);
    R_TEST_CHECK(r_test_is_nan_f32_bits(fraction_parts.whole));
    R_TEST_CHECK(r_test_is_nan_f32_bits(fraction_parts.fraction));

    binary_parts = r_std_math_split_binary_f32((float)6.0);
    R_TEST_CHECK(binary_parts.fraction == (float)0.75);
    R_TEST_CHECK(binary_parts.exponent == INT32_C(3));
    binary_copy = binary_parts;
    R_TEST_CHECK(binary_copy.fraction == binary_parts.fraction);
    R_TEST_CHECK(binary_copy.exponent == binary_parts.exponent);
    binary_parts = r_std_math_split_binary_f32((float)-6.0);
    R_TEST_CHECK(binary_parts.fraction == (float)-0.75);
    R_TEST_CHECK(binary_parts.exponent == INT32_C(3));
    binary_parts = r_std_math_split_binary_f32(negative_zero);
    R_TEST_CHECK(r_test_same_f32_bits(binary_parts.fraction, negative_zero));
    R_TEST_CHECK(binary_parts.exponent == INT32_C(0));
    binary_parts = r_std_math_split_binary_f32((float)-INFINITY);
    R_TEST_CHECK(isinf(binary_parts.fraction));
    R_TEST_CHECK(signbit(binary_parts.fraction));
    R_TEST_CHECK(binary_parts.exponent == INT32_C(0));
    binary_parts = r_std_math_split_binary_f32((float)NAN);
    R_TEST_CHECK(r_test_is_nan_f32_bits(binary_parts.fraction));
    R_TEST_CHECK(binary_parts.exponent == INT32_C(0));
    binary_parts = r_std_math_split_binary_f32(signaling_nan);
    R_TEST_CHECK(r_test_same_f32_bits(binary_parts.fraction, signaling_nan));
    R_TEST_CHECK(binary_parts.exponent == INT32_C(0));

    composed = r_std_math_compose_binary_f32((float)0.75, INT32_C(3));
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(composed.value == (float)6.0);
    composed = r_std_math_compose_binary_f32((float)1.0, (int32_t)-149);
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(composed.value == (float)FLT_TRUE_MIN);
    binary_parts = r_std_math_split_binary_f32((float)FLT_TRUE_MIN);
    composed = r_std_math_compose_binary_f32(binary_parts.fraction, binary_parts.exponent);
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(composed.value == (float)FLT_TRUE_MIN);
    binary_parts = r_std_math_split_binary_f32((float)FLT_MAX);
    composed = r_std_math_compose_binary_f32(binary_parts.fraction, binary_parts.exponent);
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(composed.value == (float)FLT_MAX);

    composed = r_std_math_compose_binary_f32((float)1.5, (int32_t)-149);
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(composed.error.code == R_STD_MATH_ERROR_UNDERFLOW);
    R_TEST_CHECK(composed.error.native_code == INT64_C(0));
    composed = r_std_math_compose_binary_f32(r_test_f32_value_from_bits(UINT32_C(0x00ffffff)),
                                             INT32_C(-1));
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_same_f32_bits(composed.value, (float)FLT_MIN));
    composed = r_std_math_compose_binary_f32((float)FLT_TRUE_MIN, INT32_C(-1));
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(composed.error.code == R_STD_MATH_ERROR_UNDERFLOW);
    R_TEST_CHECK(composed.error.native_code == INT64_C(0));
    composed = r_std_math_compose_binary_f32((float)FLT_MAX, INT32_C(1));
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(composed.error.code == R_STD_MATH_ERROR_OVERFLOW);
    R_TEST_CHECK(composed.error.native_code == INT64_C(0));

    composed = r_std_math_compose_binary_f32(signaling_nan, INT32_MAX);
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_same_f32_bits(composed.value, signaling_nan));
    composed = r_std_math_compose_binary_f32((float)INFINITY, INT32_MIN);
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isinf(composed.value));
    R_TEST_CHECK(!signbit(composed.value));
    composed = r_std_math_compose_binary_f32((float)-INFINITY, INT32_MAX);
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isinf(composed.value));
    R_TEST_CHECK(signbit(composed.value));
    composed = r_std_math_compose_binary_f32((float)0.0, INT32_MAX);
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(!signbit(composed.value));
    composed = r_std_math_compose_binary_f32(negative_zero, INT32_MIN);
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_same_f32_bits(composed.value, negative_zero));

    R_TEST_CHECK(r_test_caller_environment_matches(FE_UPWARD, FE_DIVBYZERO, EILSEQ) == 0);
    R_TEST_CHECK(fesetround(original_rounding) == 0);
    R_TEST_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    return 0;
}

static int r_test_parts_f64(void) {
    int original_rounding = fegetround();
    double signaling_nan = r_test_signaling_nan_f64_value();
    double negative_zero = (double)-0.0;
    RStdMathFractionPartsF64 fraction_parts;
    RStdMathFractionPartsF64 fraction_copy;
    RStdMathBinaryPartsF64 binary_parts;
    RStdMathBinaryPartsF64 binary_copy;
    RStdMathF64Result composed;

    R_TEST_CHECK(original_rounding != -1);
    R_TEST_CHECK(fesetround(FE_UPWARD) == 0);
    R_TEST_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    R_TEST_CHECK(feraiseexcept(FE_DIVBYZERO) == 0);
    errno = EILSEQ;

    fraction_parts = r_std_math_split_fraction_f64((double)3.75);
    R_TEST_CHECK(fraction_parts.whole == (double)3.0);
    R_TEST_CHECK(fraction_parts.fraction == (double)0.75);
    fraction_copy = fraction_parts;
    R_TEST_CHECK(fraction_copy.whole == fraction_parts.whole);
    R_TEST_CHECK(fraction_copy.fraction == fraction_parts.fraction);
    fraction_parts = r_std_math_split_fraction_f64((double)-3.75);
    R_TEST_CHECK(fraction_parts.whole == (double)-3.0);
    R_TEST_CHECK(fraction_parts.fraction == (double)-0.75);
    fraction_parts = r_std_math_split_fraction_f64(negative_zero);
    R_TEST_CHECK(r_test_same_f64_bits(fraction_parts.whole, negative_zero));
    R_TEST_CHECK(r_test_same_f64_bits(fraction_parts.fraction, negative_zero));
    fraction_parts = r_std_math_split_fraction_f64((double)-INFINITY);
    R_TEST_CHECK(isinf(fraction_parts.whole));
    R_TEST_CHECK(signbit(fraction_parts.whole));
    R_TEST_CHECK(fraction_parts.fraction == (double)0.0);
    R_TEST_CHECK(signbit(fraction_parts.fraction));
    fraction_parts = r_std_math_split_fraction_f64((double)NAN);
    R_TEST_CHECK(r_test_is_nan_f64_bits(fraction_parts.whole));
    R_TEST_CHECK(r_test_is_nan_f64_bits(fraction_parts.fraction));
    fraction_parts = r_std_math_split_fraction_f64(signaling_nan);
    R_TEST_CHECK(r_test_is_nan_f64_bits(fraction_parts.whole));
    R_TEST_CHECK(r_test_is_nan_f64_bits(fraction_parts.fraction));

    binary_parts = r_std_math_split_binary_f64((double)6.0);
    R_TEST_CHECK(binary_parts.fraction == (double)0.75);
    R_TEST_CHECK(binary_parts.exponent == INT32_C(3));
    binary_copy = binary_parts;
    R_TEST_CHECK(binary_copy.fraction == binary_parts.fraction);
    R_TEST_CHECK(binary_copy.exponent == binary_parts.exponent);
    binary_parts = r_std_math_split_binary_f64((double)-6.0);
    R_TEST_CHECK(binary_parts.fraction == (double)-0.75);
    R_TEST_CHECK(binary_parts.exponent == INT32_C(3));
    binary_parts = r_std_math_split_binary_f64(negative_zero);
    R_TEST_CHECK(r_test_same_f64_bits(binary_parts.fraction, negative_zero));
    R_TEST_CHECK(binary_parts.exponent == INT32_C(0));
    binary_parts = r_std_math_split_binary_f64((double)-INFINITY);
    R_TEST_CHECK(isinf(binary_parts.fraction));
    R_TEST_CHECK(signbit(binary_parts.fraction));
    R_TEST_CHECK(binary_parts.exponent == INT32_C(0));
    binary_parts = r_std_math_split_binary_f64((double)NAN);
    R_TEST_CHECK(r_test_is_nan_f64_bits(binary_parts.fraction));
    R_TEST_CHECK(binary_parts.exponent == INT32_C(0));
    binary_parts = r_std_math_split_binary_f64(signaling_nan);
    R_TEST_CHECK(r_test_same_f64_bits(binary_parts.fraction, signaling_nan));
    R_TEST_CHECK(binary_parts.exponent == INT32_C(0));

    composed = r_std_math_compose_binary_f64((double)0.75, INT32_C(3));
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(composed.value == (double)6.0);
    composed = r_std_math_compose_binary_f64((double)1.0, (int32_t)-1074);
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(composed.value == (double)DBL_TRUE_MIN);
    binary_parts = r_std_math_split_binary_f64((double)DBL_TRUE_MIN);
    composed = r_std_math_compose_binary_f64(binary_parts.fraction, binary_parts.exponent);
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(composed.value == (double)DBL_TRUE_MIN);
    binary_parts = r_std_math_split_binary_f64((double)DBL_MAX);
    composed = r_std_math_compose_binary_f64(binary_parts.fraction, binary_parts.exponent);
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(composed.value == (double)DBL_MAX);

    composed = r_std_math_compose_binary_f64((double)1.5, (int32_t)-1074);
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(composed.error.code == R_STD_MATH_ERROR_UNDERFLOW);
    R_TEST_CHECK(composed.error.native_code == INT64_C(0));
    composed = r_std_math_compose_binary_f64(
        r_test_f64_value_from_bits(UINT64_C(0x001fffffffffffff)), INT32_C(-1));
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_same_f64_bits(composed.value, (double)DBL_MIN));
    composed = r_std_math_compose_binary_f64((double)DBL_TRUE_MIN, INT32_C(-1));
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(composed.error.code == R_STD_MATH_ERROR_UNDERFLOW);
    R_TEST_CHECK(composed.error.native_code == INT64_C(0));
    composed = r_std_math_compose_binary_f64((double)DBL_MAX, INT32_C(1));
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(composed.error.code == R_STD_MATH_ERROR_OVERFLOW);
    R_TEST_CHECK(composed.error.native_code == INT64_C(0));

    composed = r_std_math_compose_binary_f64(signaling_nan, INT32_MAX);
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_same_f64_bits(composed.value, signaling_nan));
    composed = r_std_math_compose_binary_f64((double)INFINITY, INT32_MIN);
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isinf(composed.value));
    R_TEST_CHECK(!signbit(composed.value));
    composed = r_std_math_compose_binary_f64((double)-INFINITY, INT32_MAX);
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isinf(composed.value));
    R_TEST_CHECK(signbit(composed.value));
    composed = r_std_math_compose_binary_f64((double)0.0, INT32_MAX);
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(!signbit(composed.value));
    composed = r_std_math_compose_binary_f64(negative_zero, INT32_MIN);
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_same_f64_bits(composed.value, negative_zero));

    R_TEST_CHECK(r_test_caller_environment_matches(FE_UPWARD, FE_DIVBYZERO, EILSEQ) == 0);
    R_TEST_CHECK(fesetround(original_rounding) == 0);
    R_TEST_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    return 0;
}

static int r_test_parts_c_float(void) {
    int original_rounding = fegetround();
    float signaling_nan = r_test_signaling_nan_f32_value();
    float negative_zero = (float)-0.0;
    RStdMathFractionPartsCFloat fraction_parts;
    RStdMathFractionPartsCFloat fraction_copy;
    RStdMathBinaryPartsCFloat binary_parts;
    RStdMathBinaryPartsCFloat binary_copy;
    RStdMathCFloatResult composed;

    R_TEST_CHECK(original_rounding != -1);
    R_TEST_CHECK(fesetround(FE_UPWARD) == 0);
    R_TEST_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    R_TEST_CHECK(feraiseexcept(FE_DIVBYZERO) == 0);
    errno = EILSEQ;

    fraction_parts = r_std_math_split_fraction_c_float((float)3.75);
    R_TEST_CHECK(fraction_parts.whole == (float)3.0);
    R_TEST_CHECK(fraction_parts.fraction == (float)0.75);
    fraction_copy = fraction_parts;
    R_TEST_CHECK(fraction_copy.whole == fraction_parts.whole);
    R_TEST_CHECK(fraction_copy.fraction == fraction_parts.fraction);
    fraction_parts = r_std_math_split_fraction_c_float((float)-3.75);
    R_TEST_CHECK(fraction_parts.whole == (float)-3.0);
    R_TEST_CHECK(fraction_parts.fraction == (float)-0.75);
    fraction_parts = r_std_math_split_fraction_c_float(negative_zero);
    R_TEST_CHECK(r_test_same_f32_bits(fraction_parts.whole, negative_zero));
    R_TEST_CHECK(r_test_same_f32_bits(fraction_parts.fraction, negative_zero));
    fraction_parts = r_std_math_split_fraction_c_float((float)-INFINITY);
    R_TEST_CHECK(isinf(fraction_parts.whole));
    R_TEST_CHECK(signbit(fraction_parts.whole));
    R_TEST_CHECK(fraction_parts.fraction == (float)0.0);
    R_TEST_CHECK(signbit(fraction_parts.fraction));
    fraction_parts = r_std_math_split_fraction_c_float((float)NAN);
    R_TEST_CHECK(r_test_is_nan_f32_bits(fraction_parts.whole));
    R_TEST_CHECK(r_test_is_nan_f32_bits(fraction_parts.fraction));
    fraction_parts = r_std_math_split_fraction_c_float(signaling_nan);
    R_TEST_CHECK(r_test_is_nan_f32_bits(fraction_parts.whole));
    R_TEST_CHECK(r_test_is_nan_f32_bits(fraction_parts.fraction));

    binary_parts = r_std_math_split_binary_c_float((float)6.0);
    R_TEST_CHECK(binary_parts.fraction == (float)0.75);
    R_TEST_CHECK(binary_parts.exponent == INT32_C(3));
    binary_copy = binary_parts;
    R_TEST_CHECK(binary_copy.fraction == binary_parts.fraction);
    R_TEST_CHECK(binary_copy.exponent == binary_parts.exponent);
    binary_parts = r_std_math_split_binary_c_float((float)-6.0);
    R_TEST_CHECK(binary_parts.fraction == (float)-0.75);
    R_TEST_CHECK(binary_parts.exponent == INT32_C(3));
    binary_parts = r_std_math_split_binary_c_float(negative_zero);
    R_TEST_CHECK(r_test_same_f32_bits(binary_parts.fraction, negative_zero));
    R_TEST_CHECK(binary_parts.exponent == INT32_C(0));
    binary_parts = r_std_math_split_binary_c_float((float)-INFINITY);
    R_TEST_CHECK(isinf(binary_parts.fraction));
    R_TEST_CHECK(signbit(binary_parts.fraction));
    R_TEST_CHECK(binary_parts.exponent == INT32_C(0));
    binary_parts = r_std_math_split_binary_c_float((float)NAN);
    R_TEST_CHECK(r_test_is_nan_f32_bits(binary_parts.fraction));
    R_TEST_CHECK(binary_parts.exponent == INT32_C(0));
    binary_parts = r_std_math_split_binary_c_float(signaling_nan);
    R_TEST_CHECK(r_test_same_f32_bits(binary_parts.fraction, signaling_nan));
    R_TEST_CHECK(binary_parts.exponent == INT32_C(0));

    composed = r_std_math_compose_binary_c_float((float)0.75, INT32_C(3));
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(composed.value == (float)6.0);
    composed = r_std_math_compose_binary_c_float((float)1.0, (int32_t)-149);
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(composed.value == (float)FLT_TRUE_MIN);
    binary_parts = r_std_math_split_binary_c_float((float)FLT_TRUE_MIN);
    composed = r_std_math_compose_binary_c_float(binary_parts.fraction, binary_parts.exponent);
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(composed.value == (float)FLT_TRUE_MIN);
    binary_parts = r_std_math_split_binary_c_float((float)FLT_MAX);
    composed = r_std_math_compose_binary_c_float(binary_parts.fraction, binary_parts.exponent);
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(composed.value == (float)FLT_MAX);

    composed = r_std_math_compose_binary_c_float((float)1.5, (int32_t)-149);
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(composed.error.code == R_STD_MATH_ERROR_UNDERFLOW);
    R_TEST_CHECK(composed.error.native_code == INT64_C(0));
    composed = r_std_math_compose_binary_c_float(r_test_f32_value_from_bits(UINT32_C(0x00ffffff)),
                                                 INT32_C(-1));
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_same_f32_bits(composed.value, (float)FLT_MIN));
    composed = r_std_math_compose_binary_c_float((float)FLT_TRUE_MIN, INT32_C(-1));
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(composed.error.code == R_STD_MATH_ERROR_UNDERFLOW);
    R_TEST_CHECK(composed.error.native_code == INT64_C(0));
    composed = r_std_math_compose_binary_c_float((float)FLT_MAX, INT32_C(1));
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(composed.error.code == R_STD_MATH_ERROR_OVERFLOW);
    R_TEST_CHECK(composed.error.native_code == INT64_C(0));

    composed = r_std_math_compose_binary_c_float(signaling_nan, INT32_MAX);
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_same_f32_bits(composed.value, signaling_nan));
    composed = r_std_math_compose_binary_c_float((float)INFINITY, INT32_MIN);
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isinf(composed.value));
    R_TEST_CHECK(!signbit(composed.value));
    composed = r_std_math_compose_binary_c_float((float)-INFINITY, INT32_MAX);
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isinf(composed.value));
    R_TEST_CHECK(signbit(composed.value));
    composed = r_std_math_compose_binary_c_float((float)0.0, INT32_MAX);
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(!signbit(composed.value));
    composed = r_std_math_compose_binary_c_float(negative_zero, INT32_MIN);
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_same_f32_bits(composed.value, negative_zero));

    R_TEST_CHECK(r_test_caller_environment_matches(FE_UPWARD, FE_DIVBYZERO, EILSEQ) == 0);
    R_TEST_CHECK(fesetround(original_rounding) == 0);
    R_TEST_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    return 0;
}

static int r_test_parts_c_double(void) {
    int original_rounding = fegetround();
    double signaling_nan = r_test_signaling_nan_f64_value();
    double negative_zero = (double)-0.0;
    RStdMathFractionPartsCDouble fraction_parts;
    RStdMathFractionPartsCDouble fraction_copy;
    RStdMathBinaryPartsCDouble binary_parts;
    RStdMathBinaryPartsCDouble binary_copy;
    RStdMathCDoubleResult composed;

    R_TEST_CHECK(original_rounding != -1);
    R_TEST_CHECK(fesetround(FE_UPWARD) == 0);
    R_TEST_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    R_TEST_CHECK(feraiseexcept(FE_DIVBYZERO) == 0);
    errno = EILSEQ;

    fraction_parts = r_std_math_split_fraction_c_double((double)3.75);
    R_TEST_CHECK(fraction_parts.whole == (double)3.0);
    R_TEST_CHECK(fraction_parts.fraction == (double)0.75);
    fraction_copy = fraction_parts;
    R_TEST_CHECK(fraction_copy.whole == fraction_parts.whole);
    R_TEST_CHECK(fraction_copy.fraction == fraction_parts.fraction);
    fraction_parts = r_std_math_split_fraction_c_double((double)-3.75);
    R_TEST_CHECK(fraction_parts.whole == (double)-3.0);
    R_TEST_CHECK(fraction_parts.fraction == (double)-0.75);
    fraction_parts = r_std_math_split_fraction_c_double(negative_zero);
    R_TEST_CHECK(r_test_same_f64_bits(fraction_parts.whole, negative_zero));
    R_TEST_CHECK(r_test_same_f64_bits(fraction_parts.fraction, negative_zero));
    fraction_parts = r_std_math_split_fraction_c_double((double)-INFINITY);
    R_TEST_CHECK(isinf(fraction_parts.whole));
    R_TEST_CHECK(signbit(fraction_parts.whole));
    R_TEST_CHECK(fraction_parts.fraction == (double)0.0);
    R_TEST_CHECK(signbit(fraction_parts.fraction));
    fraction_parts = r_std_math_split_fraction_c_double((double)NAN);
    R_TEST_CHECK(r_test_is_nan_f64_bits(fraction_parts.whole));
    R_TEST_CHECK(r_test_is_nan_f64_bits(fraction_parts.fraction));
    fraction_parts = r_std_math_split_fraction_c_double(signaling_nan);
    R_TEST_CHECK(r_test_is_nan_f64_bits(fraction_parts.whole));
    R_TEST_CHECK(r_test_is_nan_f64_bits(fraction_parts.fraction));

    binary_parts = r_std_math_split_binary_c_double((double)6.0);
    R_TEST_CHECK(binary_parts.fraction == (double)0.75);
    R_TEST_CHECK(binary_parts.exponent == INT32_C(3));
    binary_copy = binary_parts;
    R_TEST_CHECK(binary_copy.fraction == binary_parts.fraction);
    R_TEST_CHECK(binary_copy.exponent == binary_parts.exponent);
    binary_parts = r_std_math_split_binary_c_double((double)-6.0);
    R_TEST_CHECK(binary_parts.fraction == (double)-0.75);
    R_TEST_CHECK(binary_parts.exponent == INT32_C(3));
    binary_parts = r_std_math_split_binary_c_double(negative_zero);
    R_TEST_CHECK(r_test_same_f64_bits(binary_parts.fraction, negative_zero));
    R_TEST_CHECK(binary_parts.exponent == INT32_C(0));
    binary_parts = r_std_math_split_binary_c_double((double)-INFINITY);
    R_TEST_CHECK(isinf(binary_parts.fraction));
    R_TEST_CHECK(signbit(binary_parts.fraction));
    R_TEST_CHECK(binary_parts.exponent == INT32_C(0));
    binary_parts = r_std_math_split_binary_c_double((double)NAN);
    R_TEST_CHECK(r_test_is_nan_f64_bits(binary_parts.fraction));
    R_TEST_CHECK(binary_parts.exponent == INT32_C(0));
    binary_parts = r_std_math_split_binary_c_double(signaling_nan);
    R_TEST_CHECK(r_test_same_f64_bits(binary_parts.fraction, signaling_nan));
    R_TEST_CHECK(binary_parts.exponent == INT32_C(0));

    composed = r_std_math_compose_binary_c_double((double)0.75, INT32_C(3));
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(composed.value == (double)6.0);
    composed = r_std_math_compose_binary_c_double((double)1.0, (int32_t)-1074);
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(composed.value == (double)DBL_TRUE_MIN);
    binary_parts = r_std_math_split_binary_c_double((double)DBL_TRUE_MIN);
    composed = r_std_math_compose_binary_c_double(binary_parts.fraction, binary_parts.exponent);
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(composed.value == (double)DBL_TRUE_MIN);
    binary_parts = r_std_math_split_binary_c_double((double)DBL_MAX);
    composed = r_std_math_compose_binary_c_double(binary_parts.fraction, binary_parts.exponent);
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(composed.value == (double)DBL_MAX);

    composed = r_std_math_compose_binary_c_double((double)1.5, (int32_t)-1074);
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(composed.error.code == R_STD_MATH_ERROR_UNDERFLOW);
    R_TEST_CHECK(composed.error.native_code == INT64_C(0));
    composed = r_std_math_compose_binary_c_double(
        r_test_f64_value_from_bits(UINT64_C(0x001fffffffffffff)), INT32_C(-1));
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_same_f64_bits(composed.value, (double)DBL_MIN));
    composed = r_std_math_compose_binary_c_double((double)DBL_TRUE_MIN, INT32_C(-1));
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(composed.error.code == R_STD_MATH_ERROR_UNDERFLOW);
    R_TEST_CHECK(composed.error.native_code == INT64_C(0));
    composed = r_std_math_compose_binary_c_double((double)DBL_MAX, INT32_C(1));
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(composed.error.code == R_STD_MATH_ERROR_OVERFLOW);
    R_TEST_CHECK(composed.error.native_code == INT64_C(0));

    composed = r_std_math_compose_binary_c_double(signaling_nan, INT32_MAX);
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_same_f64_bits(composed.value, signaling_nan));
    composed = r_std_math_compose_binary_c_double((double)INFINITY, INT32_MIN);
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isinf(composed.value));
    R_TEST_CHECK(!signbit(composed.value));
    composed = r_std_math_compose_binary_c_double((double)-INFINITY, INT32_MAX);
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isinf(composed.value));
    R_TEST_CHECK(signbit(composed.value));
    composed = r_std_math_compose_binary_c_double((double)0.0, INT32_MAX);
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(!signbit(composed.value));
    composed = r_std_math_compose_binary_c_double(negative_zero, INT32_MIN);
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_same_f64_bits(composed.value, negative_zero));

    R_TEST_CHECK(r_test_caller_environment_matches(FE_UPWARD, FE_DIVBYZERO, EILSEQ) == 0);
    R_TEST_CHECK(fesetround(original_rounding) == 0);
    R_TEST_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    return 0;
}

static int r_test_parts_c_long_double(void) {
    int original_rounding = fegetround();
    long double signaling_nan = r_test_signaling_nan_c_long_double_value();
    long double negative_zero = (long double)-0.0;
    RStdMathFractionPartsCLongDouble fraction_parts;
    RStdMathFractionPartsCLongDouble fraction_copy;
    RStdMathBinaryPartsCLongDouble binary_parts;
    RStdMathBinaryPartsCLongDouble binary_copy;
    RStdMathCLongDoubleResult composed;

    R_TEST_CHECK(original_rounding != -1);
    R_TEST_CHECK(fesetround(FE_UPWARD) == 0);
    R_TEST_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    R_TEST_CHECK(feraiseexcept(FE_DIVBYZERO) == 0);
    errno = EILSEQ;

    fraction_parts = r_std_math_split_fraction_c_long_double((long double)3.75);
    R_TEST_CHECK(fraction_parts.whole == (long double)3.0);
    R_TEST_CHECK(fraction_parts.fraction == (long double)0.75);
    fraction_copy = fraction_parts;
    R_TEST_CHECK(fraction_copy.whole == fraction_parts.whole);
    R_TEST_CHECK(fraction_copy.fraction == fraction_parts.fraction);
    fraction_parts = r_std_math_split_fraction_c_long_double((long double)-3.75);
    R_TEST_CHECK(fraction_parts.whole == (long double)-3.0);
    R_TEST_CHECK(fraction_parts.fraction == (long double)-0.75);
    fraction_parts = r_std_math_split_fraction_c_long_double(negative_zero);
    R_TEST_CHECK(r_test_same_c_long_double_bits(fraction_parts.whole, negative_zero));
    R_TEST_CHECK(r_test_same_c_long_double_bits(fraction_parts.fraction, negative_zero));
    fraction_parts = r_std_math_split_fraction_c_long_double((long double)-INFINITY);
    R_TEST_CHECK(isinf(fraction_parts.whole));
    R_TEST_CHECK(signbit(fraction_parts.whole));
    R_TEST_CHECK(fraction_parts.fraction == (long double)0.0);
    R_TEST_CHECK(signbit(fraction_parts.fraction));
    fraction_parts = r_std_math_split_fraction_c_long_double((long double)NAN);
    R_TEST_CHECK(r_test_is_nan_c_long_double_bits(fraction_parts.whole));
    R_TEST_CHECK(r_test_is_nan_c_long_double_bits(fraction_parts.fraction));
    fraction_parts = r_std_math_split_fraction_c_long_double(signaling_nan);
    R_TEST_CHECK(r_test_is_nan_c_long_double_bits(fraction_parts.whole));
    R_TEST_CHECK(r_test_is_nan_c_long_double_bits(fraction_parts.fraction));

    binary_parts = r_std_math_split_binary_c_long_double((long double)6.0);
    R_TEST_CHECK(binary_parts.fraction == (long double)0.75);
    R_TEST_CHECK(binary_parts.exponent == INT32_C(3));
    binary_copy = binary_parts;
    R_TEST_CHECK(binary_copy.fraction == binary_parts.fraction);
    R_TEST_CHECK(binary_copy.exponent == binary_parts.exponent);
    binary_parts = r_std_math_split_binary_c_long_double((long double)-6.0);
    R_TEST_CHECK(binary_parts.fraction == (long double)-0.75);
    R_TEST_CHECK(binary_parts.exponent == INT32_C(3));
    binary_parts = r_std_math_split_binary_c_long_double(negative_zero);
    R_TEST_CHECK(r_test_same_c_long_double_bits(binary_parts.fraction, negative_zero));
    R_TEST_CHECK(binary_parts.exponent == INT32_C(0));
    binary_parts = r_std_math_split_binary_c_long_double((long double)-INFINITY);
    R_TEST_CHECK(isinf(binary_parts.fraction));
    R_TEST_CHECK(signbit(binary_parts.fraction));
    R_TEST_CHECK(binary_parts.exponent == INT32_C(0));
    binary_parts = r_std_math_split_binary_c_long_double((long double)NAN);
    R_TEST_CHECK(r_test_is_nan_c_long_double_bits(binary_parts.fraction));
    R_TEST_CHECK(binary_parts.exponent == INT32_C(0));
    binary_parts = r_std_math_split_binary_c_long_double(signaling_nan);
    R_TEST_CHECK(r_test_same_c_long_double_bits(binary_parts.fraction, signaling_nan));
    R_TEST_CHECK(binary_parts.exponent == INT32_C(0));

    composed = r_std_math_compose_binary_c_long_double((long double)0.75, INT32_C(3));
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(composed.value == (long double)6.0);
    composed = r_std_math_compose_binary_c_long_double((long double)1.0, (int32_t)-1074);
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(composed.value == (long double)LDBL_TRUE_MIN);
    binary_parts = r_std_math_split_binary_c_long_double((long double)LDBL_TRUE_MIN);
    composed =
        r_std_math_compose_binary_c_long_double(binary_parts.fraction, binary_parts.exponent);
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(composed.value == (long double)LDBL_TRUE_MIN);
    binary_parts = r_std_math_split_binary_c_long_double((long double)LDBL_MAX);
    composed =
        r_std_math_compose_binary_c_long_double(binary_parts.fraction, binary_parts.exponent);
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(composed.value == (long double)LDBL_MAX);

    composed = r_std_math_compose_binary_c_long_double((long double)1.5, (int32_t)-1074);
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(composed.error.code == R_STD_MATH_ERROR_UNDERFLOW);
    R_TEST_CHECK(composed.error.native_code == INT64_C(0));
    composed = r_std_math_compose_binary_c_long_double(
        r_test_c_long_double_value_from_bits(UINT64_C(0x001fffffffffffff)), INT32_C(-1));
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_same_c_long_double_bits(composed.value, (long double)LDBL_MIN));
    composed = r_std_math_compose_binary_c_long_double((long double)LDBL_TRUE_MIN, INT32_C(-1));
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(composed.error.code == R_STD_MATH_ERROR_UNDERFLOW);
    R_TEST_CHECK(composed.error.native_code == INT64_C(0));
    composed = r_std_math_compose_binary_c_long_double((long double)LDBL_MAX, INT32_C(1));
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(composed.error.code == R_STD_MATH_ERROR_OVERFLOW);
    R_TEST_CHECK(composed.error.native_code == INT64_C(0));

    composed = r_std_math_compose_binary_c_long_double(signaling_nan, INT32_MAX);
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_same_c_long_double_bits(composed.value, signaling_nan));
    composed = r_std_math_compose_binary_c_long_double((long double)INFINITY, INT32_MIN);
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isinf(composed.value));
    R_TEST_CHECK(!signbit(composed.value));
    composed = r_std_math_compose_binary_c_long_double((long double)-INFINITY, INT32_MAX);
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isinf(composed.value));
    R_TEST_CHECK(signbit(composed.value));
    composed = r_std_math_compose_binary_c_long_double((long double)0.0, INT32_MAX);
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(!signbit(composed.value));
    composed = r_std_math_compose_binary_c_long_double(negative_zero, INT32_MIN);
    R_TEST_CHECK(composed.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_same_c_long_double_bits(composed.value, negative_zero));

    R_TEST_CHECK(r_test_caller_environment_matches(FE_UPWARD, FE_DIVBYZERO, EILSEQ) == 0);
    R_TEST_CHECK(fesetround(original_rounding) == 0);
    R_TEST_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    return 0;
}

static int r_test_sqrt(void) {
    int original_rounding = fegetround();
    RStdMathF32Result result_f32;
    RStdMathF64Result result;

    R_TEST_CHECK(original_rounding != -1);
    R_TEST_CHECK(fesetround(FE_DOWNWARD) == 0);
    R_TEST_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    R_TEST_CHECK(feraiseexcept(FE_DIVBYZERO) == 0);
    errno = EDOM;

    result = r_std_math_sqrt_f64(4.0);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == 2.0);
    R_TEST_CHECK(fegetround() == FE_DOWNWARD);
    R_TEST_CHECK((fetestexcept(FE_DIVBYZERO) & FE_DIVBYZERO) != 0);
    R_TEST_CHECK(errno == EDOM);

    result = r_std_math_sqrt_f64(-1.0);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_DOMAIN);
    R_TEST_CHECK(result.error.native_code == INT64_C(0));
    R_TEST_CHECK(fegetround() == FE_DOWNWARD);

    result = r_std_math_sqrt_f64(-0.0);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(signbit(result.value));
    result = r_std_math_sqrt_f64(NAN);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isnan(result.value));

    result_f32 = r_std_math_sqrt_f32(9.0F);
    R_TEST_CHECK(result_f32.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result_f32.value == 3.0F);
    R_TEST_CHECK(fegetround() == FE_DOWNWARD);
    R_TEST_CHECK((fetestexcept(FE_DIVBYZERO) & FE_DIVBYZERO) != 0);
    R_TEST_CHECK(errno == EDOM);

    result_f32 = r_std_math_sqrt_f32(-1.0F);
    R_TEST_CHECK(result_f32.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result_f32.error.code == R_STD_MATH_ERROR_DOMAIN);
    R_TEST_CHECK(result_f32.error.native_code == INT64_C(0));
    result_f32 = r_std_math_sqrt_f32(-0.0F);
    R_TEST_CHECK(result_f32.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(signbit(result_f32.value));
    result_f32 = r_std_math_sqrt_f32(NAN);
    R_TEST_CHECK(result_f32.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isnan(result_f32.value));

    R_TEST_CHECK(fesetround(original_rounding) == 0);
    R_TEST_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    return 0;
}

static int r_test_non_failing_f64(void) {
    int original_rounding = fegetround();
    double adjacent;
    double result;

    R_TEST_CHECK(original_rounding != -1);
    R_TEST_CHECK(fesetround(FE_DOWNWARD) == 0);
    R_TEST_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    R_TEST_CHECK(feraiseexcept(FE_DIVBYZERO) == 0);
    errno = ERANGE;

    R_TEST_CHECK(r_std_math_abs_f64(-3.5) == 3.5);
    R_TEST_CHECK(r_std_math_floor_f64(-1.25) == -2.0);
    R_TEST_CHECK(r_std_math_ceil_f64(-1.25) == -1.0);
    R_TEST_CHECK(r_std_math_trunc_f64(-1.75) == -1.0);
    R_TEST_CHECK(r_std_math_round_f64(2.5) == 3.0);
    R_TEST_CHECK(r_std_math_round_f64(-2.5) == -3.0);
    R_TEST_CHECK(signbit(r_std_math_copy_sign_f64(1.0, -0.0)));
    R_TEST_CHECK(r_std_math_min_f64(NAN, 4.0) == 4.0);
    R_TEST_CHECK(r_std_math_min_f64(4.0, NAN) == 4.0);
    R_TEST_CHECK(r_std_math_max_f64(NAN, 4.0) == 4.0);
    R_TEST_CHECK(r_std_math_max_f64(4.0, NAN) == 4.0);
    R_TEST_CHECK(isnan(r_std_math_min_f64(NAN, NAN)));
    R_TEST_CHECK(isnan(r_std_math_max_f64(NAN, NAN)));
    R_TEST_CHECK(signbit(r_std_math_min_f64(-0.0, 0.0)));
    R_TEST_CHECK(signbit(r_std_math_min_f64(0.0, -0.0)));
    R_TEST_CHECK(!signbit(r_std_math_max_f64(-0.0, 0.0)));
    R_TEST_CHECK(!signbit(r_std_math_max_f64(0.0, -0.0)));
    adjacent = r_std_math_next_after_f64(1.0, 2.0);
    R_TEST_CHECK(adjacent > 1.0);
    R_TEST_CHECK(r_std_math_next_after_f64(1.0, 1.0) == 1.0);
    result = r_std_math_next_after_f64(DBL_MAX, INFINITY);
    R_TEST_CHECK(isinf(result));
    R_TEST_CHECK(!signbit(result));
    result = r_std_math_next_after_f64(-DBL_MAX, -INFINITY);
    R_TEST_CHECK(isinf(result));
    R_TEST_CHECK(signbit(result));
    result = r_std_math_next_after_f64(0.0, -0.0);
    R_TEST_CHECK(signbit(result));
    R_TEST_CHECK(r_std_math_is_finite_f64(1.0));
    R_TEST_CHECK(!r_std_math_is_finite_f64(INFINITY));
    R_TEST_CHECK(r_std_math_is_infinite_f64(-INFINITY));
    R_TEST_CHECK(r_std_math_is_nan_f64(NAN));
    R_TEST_CHECK(r_std_math_is_normal_f64(DBL_MIN));
    R_TEST_CHECK(!r_std_math_is_normal_f64(DBL_TRUE_MIN));
    R_TEST_CHECK(r_std_math_sign_bit_f64(-0.0));
    R_TEST_CHECK(!r_std_math_sign_bit_f64(0.0));

    R_TEST_CHECK(fegetround() == FE_DOWNWARD);
    R_TEST_CHECK((fetestexcept(FE_DIVBYZERO) & FE_DIVBYZERO) != 0);
    R_TEST_CHECK(errno == ERANGE);
    R_TEST_CHECK(fesetround(original_rounding) == 0);
    R_TEST_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    return 0;
}

static int r_test_non_failing_f32(void) {
    int original_rounding = fegetround();
    float adjacent;
    float result;

    R_TEST_CHECK(original_rounding != -1);
    R_TEST_CHECK(fesetround(FE_UPWARD) == 0);
    R_TEST_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    R_TEST_CHECK(feraiseexcept(FE_OVERFLOW) == 0);
    errno = EDOM;

    R_TEST_CHECK(r_std_math_abs_f32(-3.5F) == 3.5F);
    R_TEST_CHECK(r_std_math_floor_f32(-1.25F) == -2.0F);
    R_TEST_CHECK(r_std_math_ceil_f32(-1.25F) == -1.0F);
    R_TEST_CHECK(r_std_math_trunc_f32(-1.75F) == -1.0F);
    R_TEST_CHECK(r_std_math_round_f32(2.5F) == 3.0F);
    R_TEST_CHECK(r_std_math_round_f32(-2.5F) == -3.0F);
    R_TEST_CHECK(signbit(r_std_math_copy_sign_f32(1.0F, -0.0F)));
    R_TEST_CHECK(r_std_math_min_f32(NAN, 4.0F) == 4.0F);
    R_TEST_CHECK(r_std_math_min_f32(4.0F, NAN) == 4.0F);
    R_TEST_CHECK(r_std_math_max_f32(NAN, 4.0F) == 4.0F);
    R_TEST_CHECK(r_std_math_max_f32(4.0F, NAN) == 4.0F);
    R_TEST_CHECK(isnan(r_std_math_min_f32(NAN, NAN)));
    R_TEST_CHECK(isnan(r_std_math_max_f32(NAN, NAN)));
    R_TEST_CHECK(signbit(r_std_math_min_f32(-0.0F, 0.0F)));
    R_TEST_CHECK(signbit(r_std_math_min_f32(0.0F, -0.0F)));
    R_TEST_CHECK(!signbit(r_std_math_max_f32(-0.0F, 0.0F)));
    R_TEST_CHECK(!signbit(r_std_math_max_f32(0.0F, -0.0F)));
    adjacent = r_std_math_next_after_f32(1.0F, 2.0F);
    R_TEST_CHECK(adjacent > 1.0F);
    R_TEST_CHECK(r_std_math_next_after_f32(1.0F, 1.0F) == 1.0F);
    result = r_std_math_next_after_f32(FLT_MAX, INFINITY);
    R_TEST_CHECK(isinf(result));
    R_TEST_CHECK(!signbit(result));
    result = r_std_math_next_after_f32(-FLT_MAX, -INFINITY);
    R_TEST_CHECK(isinf(result));
    R_TEST_CHECK(signbit(result));
    result = r_std_math_next_after_f32(0.0F, -0.0F);
    R_TEST_CHECK(signbit(result));
    R_TEST_CHECK(r_std_math_is_finite_f32(1.0F));
    R_TEST_CHECK(!r_std_math_is_finite_f32(INFINITY));
    R_TEST_CHECK(r_std_math_is_infinite_f32(-INFINITY));
    R_TEST_CHECK(r_std_math_is_nan_f32(NAN));
    R_TEST_CHECK(r_std_math_is_normal_f32(FLT_MIN));
    R_TEST_CHECK(!r_std_math_is_normal_f32(FLT_TRUE_MIN));
    R_TEST_CHECK(r_std_math_sign_bit_f32(-0.0F));
    R_TEST_CHECK(!r_std_math_sign_bit_f32(0.0F));

    R_TEST_CHECK(fegetround() == FE_UPWARD);
    R_TEST_CHECK((fetestexcept(FE_OVERFLOW) & FE_OVERFLOW) != 0);
    R_TEST_CHECK(errno == EDOM);
    R_TEST_CHECK(fesetround(original_rounding) == 0);
    R_TEST_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    return 0;
}

static int r_test_error_conversion(void) {
    RStdError converted =
        r_std_math_as_error((RStdMathError){R_STD_MATH_ERROR_UNDERFLOW, INT64_C(1234)});

    R_TEST_CHECK(converted.domain == R_STD_ERROR_DOMAIN_MATH);
    R_TEST_CHECK(converted.code == UINT32_C(3));
    R_TEST_CHECK(converted.native_code == INT64_C(1234));
    return 0;
}

int main(void) {
    R_TEST_CHECK(r_test_parts_f32() == 0);
    R_TEST_CHECK(r_test_parts_f64() == 0);
    R_TEST_CHECK(r_test_parts_c_float() == 0);
    R_TEST_CHECK(r_test_parts_c_double() == 0);
    R_TEST_CHECK(r_test_parts_c_long_double() == 0);
    R_TEST_CHECK(r_test_sqrt() == 0);
    R_TEST_CHECK(r_test_non_failing_f64() == 0);
    R_TEST_CHECK(r_test_non_failing_f32() == 0);
    R_TEST_CHECK(r_test_non_failing_c_float() == 0);
    R_TEST_CHECK(r_test_non_failing_c_double() == 0);
    R_TEST_CHECK(r_test_non_failing_c_long_double() == 0);
    R_TEST_CHECK(r_test_signaling_nan_f32() == 0);
    R_TEST_CHECK(r_test_signaling_nan_f64() == 0);
    R_TEST_CHECK(r_test_signaling_nan_c_float() == 0);
    R_TEST_CHECK(r_test_signaling_nan_c_double() == 0);
    R_TEST_CHECK(r_test_signaling_nan_c_long_double() == 0);
    R_TEST_CHECK(r_test_concurrent_environments() == 0);
    R_TEST_CHECK(r_test_error_conversion() == 0);
    (void)fprintf(stdout, "library_math_tests: ok\n");
    return 0;
}

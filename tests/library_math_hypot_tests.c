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

typedef struct RTestHypotThreadContext {
    int rounding;
    int exceptions;
    int native_errno;
    int64_t overflow_native_code;
    int64_t underflow_native_code;
    int failed;
} RTestHypotThreadContext;

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
r_test_native_f32(float left, float right, float *result, int *native_errno, int *exceptions) {
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
    *result = hypotf(canonical_left, canonical_right);
    *native_errno = errno;
    *exceptions = fetestexcept(FE_ALL_EXCEPT);
    if (fesetenv(&caller_environment) != 0) {
        errno = caller_errno;
        return 1;
    }
    errno = caller_errno;
    return 0;
}

static int
r_test_native_f64(double left, double right, double *result, int *native_errno, int *exceptions) {
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
    *result = hypot(canonical_left, canonical_right);
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
    long double left, long double right, long double *result, int *native_errno, int *exceptions) {
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
    *result = hypotl(canonical_left, canonical_right);
    *native_errno = errno;
    *exceptions = fetestexcept(FE_ALL_EXCEPT);
    if (fesetenv(&caller_environment) != 0) {
        errno = caller_errno;
        return 1;
    }
    errno = caller_errno;
    return 0;
}

static int r_test_regular_values(void) {
    static const float f32_pairs[][2] = {
        {3.0F, 4.0F},
        {-3.0F, 4.0F},
        {3.0F, -4.0F},
        {-3.0F, -4.0F},
        {0.0F, 0.0F},
        {-0.0F, -0.0F},
        {FLT_MAX, 0.0F},
        {FLT_MIN, FLT_MIN},
    };
    static const double f64_pairs[][2] = {
        {3.0, 4.0},
        {-3.0, 4.0},
        {3.0, -4.0},
        {-3.0, -4.0},
        {0.0, 0.0},
        {-0.0, -0.0},
        {DBL_MAX, 0.0},
        {DBL_MIN, DBL_MIN},
    };
    size_t index;

    for (index = 0U; index < sizeof(f32_pairs) / sizeof(f32_pairs[0]); index += 1U) {
        float expected;
        int native_errno;
        int exceptions;
        RStdMathF32Result f32;
        RStdMathCFloatResult c_float;

        R_TEST_CHECK(
            r_test_native_f32(
                f32_pairs[index][0], f32_pairs[index][1], &expected, &native_errno, &exceptions) ==
            0);
        f32 = r_std_math_hypot_f32(f32_pairs[index][0], f32_pairs[index][1]);
        c_float = r_std_math_hypot_c_float(f32_pairs[index][0], f32_pairs[index][1]);
        R_TEST_CHECK(f32.status == R_STD_MATH_CALL_SUCCESS && r_test_same_f32(f32.value, expected));
        R_TEST_CHECK(c_float.status == R_STD_MATH_CALL_SUCCESS &&
                     r_test_same_f32(c_float.value, expected));
    }
    for (index = 0U; index < sizeof(f64_pairs) / sizeof(f64_pairs[0]); index += 1U) {
        double expected;
        long double expected_long_double;
        int native_errno;
        int exceptions;
        RStdMathF64Result f64;
        RStdMathCDoubleResult c_double;
        RStdMathCLongDoubleResult c_long_double;

        R_TEST_CHECK(
            r_test_native_f64(
                f64_pairs[index][0], f64_pairs[index][1], &expected, &native_errno, &exceptions) ==
            0);
        R_TEST_CHECK(r_test_native_c_long_double((long double)f64_pairs[index][0],
                                                 (long double)f64_pairs[index][1],
                                                 &expected_long_double,
                                                 &native_errno,
                                                 &exceptions) == 0);
        f64 = r_std_math_hypot_f64(f64_pairs[index][0], f64_pairs[index][1]);
        c_double = r_std_math_hypot_c_double(f64_pairs[index][0], f64_pairs[index][1]);
        c_long_double = r_std_math_hypot_c_long_double((long double)f64_pairs[index][0],
                                                       (long double)f64_pairs[index][1]);
        R_TEST_CHECK(f64.status == R_STD_MATH_CALL_SUCCESS && r_test_same_f64(f64.value, expected));
        R_TEST_CHECK(c_double.status == R_STD_MATH_CALL_SUCCESS &&
                     r_test_same_f64(c_double.value, expected));
        R_TEST_CHECK(c_long_double.status == R_STD_MATH_CALL_SUCCESS &&
                     r_test_same_c_long_double(c_long_double.value, expected_long_double));
    }
    return 0;
}

static int r_test_f32_range_edges(void) {
    const float exact_left = 3.0F * FLT_TRUE_MIN;
    const float exact_right = 4.0F * FLT_TRUE_MIN;
    float native_exact_axis;
    float native_exact_triangle;
    float native_inexact;
    float native_overflow;
    int axis_errno;
    int axis_exceptions;
    int triangle_errno;
    int triangle_exceptions;
    int inexact_errno;
    int inexact_exceptions;
    int overflow_errno;
    int overflow_exceptions;
    RStdMathF32Result f32_axis;
    RStdMathCFloatResult c_float_triangle;
    RStdMathF32Result f32_inexact;
    RStdMathCFloatResult c_float_inexact;
    RStdMathF32Result f32_overflow;
    RStdMathCFloatResult c_float_overflow;

    R_TEST_CHECK(r_test_native_f32(
                     FLT_TRUE_MIN, 0.0F, &native_exact_axis, &axis_errno, &axis_exceptions) == 0);
    R_TEST_CHECK(r_test_native_f32(exact_left,
                                   exact_right,
                                   &native_exact_triangle,
                                   &triangle_errno,
                                   &triangle_exceptions) == 0);
    R_TEST_CHECK(
        r_test_native_f32(
            FLT_TRUE_MIN, FLT_TRUE_MIN, &native_inexact, &inexact_errno, &inexact_exceptions) == 0);
    R_TEST_CHECK(r_test_native_f32(
                     FLT_MAX, FLT_MAX, &native_overflow, &overflow_errno, &overflow_exceptions) ==
                 0);
    R_TEST_CHECK(r_test_is_subnormal_f32(native_exact_axis) &&
                 r_test_same_f32(native_exact_axis, FLT_TRUE_MIN));
    R_TEST_CHECK((axis_exceptions & FE_INEXACT) == 0);
    R_TEST_CHECK(r_test_is_subnormal_f32(native_exact_triangle) &&
                 r_test_same_f32(native_exact_triangle, 5.0F * FLT_TRUE_MIN));
    R_TEST_CHECK((triangle_exceptions & FE_INEXACT) == 0);
    R_TEST_CHECK(r_test_is_subnormal_f32(native_inexact));
    R_TEST_CHECK((inexact_exceptions & FE_INEXACT) != 0);
    R_TEST_CHECK(r_test_is_infinite_f32(native_overflow));

    f32_axis = r_std_math_hypot_f32(FLT_TRUE_MIN, 0.0F);
    c_float_triangle = r_std_math_hypot_c_float(exact_left, exact_right);
    R_TEST_CHECK(f32_axis.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_same_f32(f32_axis.value, native_exact_axis));
    R_TEST_CHECK(c_float_triangle.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_same_f32(c_float_triangle.value, native_exact_triangle));
    f32_inexact = r_std_math_hypot_f32(FLT_TRUE_MIN, FLT_TRUE_MIN);
    c_float_inexact = r_std_math_hypot_c_float(FLT_TRUE_MIN, FLT_TRUE_MIN);
    R_TEST_CHECK_ERROR(f32_inexact, R_STD_MATH_ERROR_UNDERFLOW, inexact_errno);
    R_TEST_CHECK_ERROR(c_float_inexact, R_STD_MATH_ERROR_UNDERFLOW, inexact_errno);
    f32_overflow = r_std_math_hypot_f32(FLT_MAX, FLT_MAX);
    c_float_overflow = r_std_math_hypot_c_float(FLT_MAX, FLT_MAX);
    R_TEST_CHECK_ERROR(f32_overflow, R_STD_MATH_ERROR_OVERFLOW, overflow_errno);
    R_TEST_CHECK_ERROR(c_float_overflow, R_STD_MATH_ERROR_OVERFLOW, overflow_errno);
    return 0;
}

static int r_test_f64_range_edges(void) {
    const double exact_left = 3.0 * DBL_TRUE_MIN;
    const double exact_right = 4.0 * DBL_TRUE_MIN;
    double native_exact_axis;
    double native_exact_triangle;
    double native_inexact;
    double native_overflow;
    int axis_errno;
    int axis_exceptions;
    int triangle_errno;
    int triangle_exceptions;
    int inexact_errno;
    int inexact_exceptions;
    int overflow_errno;
    int overflow_exceptions;
    RStdMathF64Result f64_axis;
    RStdMathCDoubleResult c_double_triangle;
    RStdMathF64Result f64_inexact;
    RStdMathCDoubleResult c_double_inexact;
    RStdMathF64Result f64_overflow;
    RStdMathCDoubleResult c_double_overflow;

    R_TEST_CHECK(r_test_native_f64(
                     DBL_TRUE_MIN, 0.0, &native_exact_axis, &axis_errno, &axis_exceptions) == 0);
    R_TEST_CHECK(r_test_native_f64(exact_left,
                                   exact_right,
                                   &native_exact_triangle,
                                   &triangle_errno,
                                   &triangle_exceptions) == 0);
    R_TEST_CHECK(
        r_test_native_f64(
            DBL_TRUE_MIN, DBL_TRUE_MIN, &native_inexact, &inexact_errno, &inexact_exceptions) == 0);
    R_TEST_CHECK(r_test_native_f64(
                     DBL_MAX, DBL_MAX, &native_overflow, &overflow_errno, &overflow_exceptions) ==
                 0);
    R_TEST_CHECK(r_test_is_subnormal_f64(native_exact_axis) &&
                 r_test_same_f64(native_exact_axis, DBL_TRUE_MIN));
    R_TEST_CHECK((axis_exceptions & FE_INEXACT) == 0);
    R_TEST_CHECK(r_test_is_subnormal_f64(native_exact_triangle) &&
                 r_test_same_f64(native_exact_triangle, 5.0 * DBL_TRUE_MIN));
    R_TEST_CHECK((triangle_exceptions & FE_INEXACT) == 0);
    R_TEST_CHECK(r_test_is_subnormal_f64(native_inexact));
    R_TEST_CHECK((inexact_exceptions & FE_INEXACT) != 0);
    R_TEST_CHECK(r_test_is_infinite_f64(native_overflow));

    f64_axis = r_std_math_hypot_f64(DBL_TRUE_MIN, 0.0);
    c_double_triangle = r_std_math_hypot_c_double(exact_left, exact_right);
    R_TEST_CHECK(f64_axis.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_same_f64(f64_axis.value, native_exact_axis));
    R_TEST_CHECK(c_double_triangle.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_same_f64(c_double_triangle.value, native_exact_triangle));
    f64_inexact = r_std_math_hypot_f64(DBL_TRUE_MIN, DBL_TRUE_MIN);
    c_double_inexact = r_std_math_hypot_c_double(DBL_TRUE_MIN, DBL_TRUE_MIN);
    R_TEST_CHECK_ERROR(f64_inexact, R_STD_MATH_ERROR_UNDERFLOW, inexact_errno);
    R_TEST_CHECK_ERROR(c_double_inexact, R_STD_MATH_ERROR_UNDERFLOW, inexact_errno);
    f64_overflow = r_std_math_hypot_f64(DBL_MAX, DBL_MAX);
    c_double_overflow = r_std_math_hypot_c_double(DBL_MAX, DBL_MAX);
    R_TEST_CHECK_ERROR(f64_overflow, R_STD_MATH_ERROR_OVERFLOW, overflow_errno);
    R_TEST_CHECK_ERROR(c_double_overflow, R_STD_MATH_ERROR_OVERFLOW, overflow_errno);
    return 0;
}

static int r_test_c_long_double_range_edges(void) {
    const long double exact_left = 3.0L * LDBL_TRUE_MIN;
    const long double exact_right = 4.0L * LDBL_TRUE_MIN;
    long double native_exact_axis;
    long double native_exact_triangle;
    long double native_inexact;
    long double native_overflow;
    int axis_errno;
    int axis_exceptions;
    int triangle_errno;
    int triangle_exceptions;
    int inexact_errno;
    int inexact_exceptions;
    int overflow_errno;
    int overflow_exceptions;
    RStdMathCLongDoubleResult exact_axis;
    RStdMathCLongDoubleResult exact_triangle;
    RStdMathCLongDoubleResult inexact;
    RStdMathCLongDoubleResult overflow;

    R_TEST_CHECK(r_test_native_c_long_double(
                     LDBL_TRUE_MIN, 0.0L, &native_exact_axis, &axis_errno, &axis_exceptions) == 0);
    R_TEST_CHECK(r_test_native_c_long_double(exact_left,
                                             exact_right,
                                             &native_exact_triangle,
                                             &triangle_errno,
                                             &triangle_exceptions) == 0);
    R_TEST_CHECK(
        r_test_native_c_long_double(
            LDBL_TRUE_MIN, LDBL_TRUE_MIN, &native_inexact, &inexact_errno, &inexact_exceptions) ==
        0);
    R_TEST_CHECK(r_test_native_c_long_double(
                     LDBL_MAX, LDBL_MAX, &native_overflow, &overflow_errno, &overflow_exceptions) ==
                 0);
    R_TEST_CHECK(r_test_is_subnormal_c_long_double(native_exact_axis) &&
                 r_test_same_c_long_double(native_exact_axis, LDBL_TRUE_MIN));
    R_TEST_CHECK((axis_exceptions & FE_INEXACT) == 0);
    R_TEST_CHECK(r_test_is_subnormal_c_long_double(native_exact_triangle) &&
                 r_test_same_c_long_double(native_exact_triangle, 5.0L * LDBL_TRUE_MIN));
    R_TEST_CHECK((triangle_exceptions & FE_INEXACT) == 0);
    R_TEST_CHECK(r_test_is_subnormal_c_long_double(native_inexact));
    R_TEST_CHECK((inexact_exceptions & FE_INEXACT) != 0);
    R_TEST_CHECK(r_test_is_infinite_c_long_double(native_overflow));

    exact_axis = r_std_math_hypot_c_long_double(LDBL_TRUE_MIN, 0.0L);
    exact_triangle = r_std_math_hypot_c_long_double(exact_left, exact_right);
    R_TEST_CHECK(exact_axis.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_same_c_long_double(exact_axis.value, native_exact_axis));
    R_TEST_CHECK(exact_triangle.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_same_c_long_double(exact_triangle.value, native_exact_triangle));
    inexact = r_std_math_hypot_c_long_double(LDBL_TRUE_MIN, LDBL_TRUE_MIN);
    overflow = r_std_math_hypot_c_long_double(LDBL_MAX, LDBL_MAX);
    R_TEST_CHECK_ERROR(inexact, R_STD_MATH_ERROR_UNDERFLOW, inexact_errno);
    R_TEST_CHECK_ERROR(overflow, R_STD_MATH_ERROR_OVERFLOW, overflow_errno);
    return 0;
}

static int r_test_infinity_and_nan(void) {
    float signaling_f32 = r_test_signaling_nan_f32();
    double signaling_f64 = r_test_signaling_nan_f64();
    long double signaling_long_double = r_test_signaling_nan_c_long_double();
    RStdMathF32Result f32_infinity_nan = r_std_math_hypot_f32(INFINITY, NAN);
    RStdMathCFloatResult c_float_nan_infinity = r_std_math_hypot_c_float(NAN, -INFINITY);
    RStdMathF64Result f64_infinity_nan = r_std_math_hypot_f64(-INFINITY, NAN);
    RStdMathCDoubleResult c_double_nan_infinity = r_std_math_hypot_c_double(NAN, INFINITY);
    RStdMathCLongDoubleResult long_infinity_nan = r_std_math_hypot_c_long_double(INFINITY, NAN);
    RStdMathF32Result f32_nan = r_std_math_hypot_f32(signaling_f32, 2.0F);
    RStdMathCFloatResult c_float_nan = r_std_math_hypot_c_float(2.0F, NAN);
    RStdMathF64Result f64_nan = r_std_math_hypot_f64(signaling_f64, 2.0);
    RStdMathCDoubleResult c_double_nan = r_std_math_hypot_c_double(2.0, NAN);
    RStdMathCLongDoubleResult long_nan =
        r_std_math_hypot_c_long_double(2.0L, signaling_long_double);

    R_TEST_CHECK(f32_infinity_nan.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_is_infinite_f32(f32_infinity_nan.value));
    R_TEST_CHECK(c_float_nan_infinity.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_is_infinite_f32(c_float_nan_infinity.value));
    R_TEST_CHECK(f64_infinity_nan.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_is_infinite_f64(f64_infinity_nan.value));
    R_TEST_CHECK(c_double_nan_infinity.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_is_infinite_f64(c_double_nan_infinity.value));
    R_TEST_CHECK(long_infinity_nan.status == R_STD_MATH_CALL_SUCCESS &&
                 r_test_is_infinite_c_long_double(long_infinity_nan.value));
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

static void *r_test_thread_main(void *opaque_context) {
    RTestHypotThreadContext *context = opaque_context;
    int iteration;

    if (r_test_seed_environment(context->rounding, context->exceptions, context->native_errno) !=
        0) {
        context->failed = 1;
        return NULL;
    }
    for (iteration = 0; iteration < 500; iteration += 1) {
        RStdMathF32Result f32 = r_std_math_hypot_f32(3.0F, 4.0F);
        RStdMathF64Result f64 = r_std_math_hypot_f64(-5.0, 12.0);
        RStdMathCFloatResult c_float_overflow = r_std_math_hypot_c_float(FLT_MAX, FLT_MAX);
        RStdMathCDoubleResult c_double_underflow =
            r_std_math_hypot_c_double(DBL_TRUE_MIN, DBL_TRUE_MIN);
        RStdMathCLongDoubleResult long_exact = r_std_math_hypot_c_long_double(LDBL_TRUE_MIN, 0.0L);
        RStdMathCLongDoubleResult long_infinity = r_std_math_hypot_c_long_double(INFINITY, NAN);

        if ((f32.status != R_STD_MATH_CALL_SUCCESS) || (f32.value != 5.0F) ||
            (f64.status != R_STD_MATH_CALL_SUCCESS) || (f64.value != 13.0) ||
            (c_float_overflow.status != R_STD_MATH_CALL_ERROR) ||
            (c_float_overflow.error.code != R_STD_MATH_ERROR_OVERFLOW) ||
            (c_float_overflow.error.native_code != context->overflow_native_code) ||
            (c_double_underflow.status != R_STD_MATH_CALL_ERROR) ||
            (c_double_underflow.error.code != R_STD_MATH_ERROR_UNDERFLOW) ||
            (c_double_underflow.error.native_code != context->underflow_native_code) ||
            (long_exact.status != R_STD_MATH_CALL_SUCCESS) ||
            !r_test_same_c_long_double(long_exact.value, LDBL_TRUE_MIN) ||
            (long_infinity.status != R_STD_MATH_CALL_SUCCESS) ||
            !r_test_is_infinite_c_long_double(long_infinity.value) ||
            !r_test_environment_is(context->rounding, context->exceptions, context->native_errno)) {
            context->failed = 1;
            return NULL;
        }
    }
    return NULL;
}

static int r_test_environment_and_concurrency(void) {
    RStdMathCFloatResult overflow = r_std_math_hypot_c_float(FLT_MAX, FLT_MAX);
    RStdMathCDoubleResult underflow = r_std_math_hypot_c_double(DBL_TRUE_MIN, DBL_TRUE_MIN);
    RTestHypotThreadContext first;
    RTestHypotThreadContext second;
    pthread_t first_thread;
    pthread_t second_thread;

    R_TEST_CHECK(overflow.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(underflow.status == R_STD_MATH_CALL_ERROR);
    first = (RTestHypotThreadContext){FE_DOWNWARD,
                                      FE_DIVBYZERO,
                                      EDOM,
                                      overflow.error.native_code,
                                      underflow.error.native_code,
                                      0};
    second = (RTestHypotThreadContext){
        FE_UPWARD, FE_INVALID, ERANGE, overflow.error.native_code, underflow.error.native_code, 0};
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
    R_TEST_CHECK(r_std_math_hypot_f32(3.0F, 4.0F).status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_std_math_hypot_f64(3.0, 4.0).status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_std_math_hypot_c_float(3.0F, 4.0F).status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_std_math_hypot_c_double(3.0, 4.0).status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_std_math_hypot_c_long_double(3.0L, 4.0L).status == R_STD_MATH_CALL_SUCCESS);
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
    R_TEST_CHECK(r_test_regular_values() == 0);
    R_TEST_CHECK(r_test_f32_range_edges() == 0);
    R_TEST_CHECK(r_test_f64_range_edges() == 0);
    R_TEST_CHECK(r_test_c_long_double_range_edges() == 0);
    R_TEST_CHECK(r_test_infinity_and_nan() == 0);
    R_TEST_CHECK(r_test_environment_is(FE_UPWARD, FE_DIVBYZERO, E2BIG));
    R_TEST_CHECK(r_test_environment_and_concurrency() == 0);
    R_TEST_CHECK(r_test_environment_is(FE_UPWARD, FE_DIVBYZERO, E2BIG));
    R_TEST_CHECK(r_test_allocation_free() == 0);
    R_TEST_CHECK(fesetenv(&original_environment) == 0);
    errno = original_errno;
    (void)fprintf(stdout, "library_math_hypot_tests: ok\n");
    return 0;
}

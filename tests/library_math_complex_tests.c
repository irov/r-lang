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

typedef RStdMathComplexF32Result (*RTestComplexUnaryF32)(RStdMathComplexF32 value);
typedef RStdMathComplexF64Result (*RTestComplexUnaryF64)(RStdMathComplexF64 value);

typedef struct RTestComplexThreadContext {
    int rounding;
    int exceptions;
    int native_errno;
    int failed;
} RTestComplexThreadContext;

static _Bool r_test_f32_is_nan(float value) {
    uint32_t bits;

    (void)memcpy(&bits, &value, sizeof(bits));
    return ((bits & UINT32_C(0x7f800000)) == UINT32_C(0x7f800000)) &&
           ((bits & UINT32_C(0x007fffff)) != UINT32_C(0));
}

static _Bool r_test_f64_is_nan(double value) {
    uint64_t bits;

    (void)memcpy(&bits, &value, sizeof(bits));
    return ((bits & UINT64_C(0x7ff0000000000000)) == UINT64_C(0x7ff0000000000000)) &&
           ((bits & UINT64_C(0x000fffffffffffff)) != UINT64_C(0));
}

static _Bool r_test_f32_is_infinite(float value) {
    uint32_t bits;

    (void)memcpy(&bits, &value, sizeof(bits));
    return (bits & UINT32_C(0x7fffffff)) == UINT32_C(0x7f800000);
}

static _Bool r_test_f64_is_infinite(double value) {
    uint64_t bits;

    (void)memcpy(&bits, &value, sizeof(bits));
    return (bits & UINT64_C(0x7fffffffffffffff)) == UINT64_C(0x7ff0000000000000);
}

static _Bool r_test_f32_sign_bit(float value) {
    uint32_t bits;

    (void)memcpy(&bits, &value, sizeof(bits));
    return (bits & UINT32_C(0x80000000)) != UINT32_C(0);
}

static _Bool r_test_f64_sign_bit(double value) {
    uint64_t bits;

    (void)memcpy(&bits, &value, sizeof(bits));
    return (bits & UINT64_C(0x8000000000000000)) != UINT64_C(0);
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

static int r_test_environment(int rounding, int exceptions, int native_errno) {
    R_TEST_CHECK(fegetround() == rounding);
    R_TEST_CHECK((fetestexcept(FE_ALL_EXCEPT) & FE_ALL_EXCEPT) == exceptions);
    R_TEST_CHECK(errno == native_errno);
    return 0;
}

static int r_test_complex_f32(void) {
    static const RTestComplexUnaryF32 unary_operations[] = {
        r_std_math_exp_complex_f32,
        r_std_math_log_complex_f32,
        r_std_math_sqrt_complex_f32,
        r_std_math_sin_complex_f32,
        r_std_math_cos_complex_f32,
        r_std_math_tan_complex_f32,
        r_std_math_sinh_complex_f32,
        r_std_math_cosh_complex_f32,
        r_std_math_tanh_complex_f32,
    };
    float signaling_nan = r_test_f32_from_bits(UINT32_C(0x7f800001));
    float phase;
    RStdMathComplexF32 zero = {0.0F, 0.0F};
    RStdMathComplexF32 one = {1.0F, 0.0F};
    RStdMathComplexF32 value = {1.0F, 2.0F};
    RStdMathComplexF32 other = {3.0F, -1.0F};
    RStdMathComplexF32 complex_result;
    RStdMathComplexF32 complex_copy;
    RStdMathComplexF32Result result;
    RStdMathF32Result magnitude;
    int original_rounding = fegetround();
    size_t index;

    R_TEST_CHECK(original_rounding != -1);
    R_TEST_CHECK(fesetround(FE_DOWNWARD) == 0);
    R_TEST_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    R_TEST_CHECK(feraiseexcept(FE_DIVBYZERO) == 0);
    errno = EDOM;

    complex_copy = value;
    R_TEST_CHECK(complex_copy.real == 1.0F);
    R_TEST_CHECK(complex_copy.imag == 2.0F);
    complex_result = r_std_math_add_complex_f32(value, other);
    R_TEST_CHECK(complex_result.real == 4.0F);
    R_TEST_CHECK(complex_result.imag == 1.0F);
    complex_result = r_std_math_sub_complex_f32(value, other);
    R_TEST_CHECK(complex_result.real == -2.0F);
    R_TEST_CHECK(complex_result.imag == 3.0F);
    complex_result = r_std_math_mul_complex_f32(value, other);
    R_TEST_CHECK(complex_result.real == 5.0F);
    R_TEST_CHECK(complex_result.imag == 5.0F);
    complex_result = r_std_math_conjugate_complex_f32(value);
    R_TEST_CHECK(complex_result.real == 1.0F);
    R_TEST_CHECK(complex_result.imag == -2.0F);
    complex_result = r_std_math_conjugate_complex_f32((RStdMathComplexF32){0.0F, 0.0F});
    R_TEST_CHECK(!r_test_f32_sign_bit(complex_result.real));
    R_TEST_CHECK(r_test_f32_sign_bit(complex_result.imag));
    phase = r_std_math_phase_complex_f32((RStdMathComplexF32){-1.0F, 0.0F});
    R_TEST_CHECK((phase > 3.1415F) && (phase < 3.1416F));
    phase = r_std_math_phase_complex_f32((RStdMathComplexF32){-1.0F, -0.0F});
    R_TEST_CHECK((phase < -3.1415F) && (phase > -3.1416F));

    magnitude = r_std_math_magnitude_complex_f32((RStdMathComplexF32){3.0F, 4.0F});
    R_TEST_CHECK(magnitude.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(magnitude.value == 5.0F);
    magnitude = r_std_math_magnitude_complex_f32((RStdMathComplexF32){FLT_TRUE_MIN, 0.0F});
    R_TEST_CHECK(magnitude.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(magnitude.value == FLT_TRUE_MIN);
    magnitude = r_std_math_magnitude_complex_f32((RStdMathComplexF32){FLT_TRUE_MIN, FLT_TRUE_MIN});
    R_TEST_CHECK(magnitude.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(magnitude.error.code == R_STD_MATH_ERROR_UNDERFLOW);
    R_TEST_CHECK(magnitude.error.native_code == INT64_C(0));
    magnitude = r_std_math_magnitude_complex_f32((RStdMathComplexF32){FLT_MAX, FLT_MAX});
    R_TEST_CHECK(magnitude.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(magnitude.error.code == R_STD_MATH_ERROR_OVERFLOW);
    R_TEST_CHECK(magnitude.error.native_code == INT64_C(0));
    magnitude = r_std_math_magnitude_complex_f32((RStdMathComplexF32){INFINITY, 0.0F});
    R_TEST_CHECK(magnitude.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_f32_is_infinite(magnitude.value));
    magnitude = r_std_math_magnitude_complex_f32((RStdMathComplexF32){signaling_nan, 0.0F});
    R_TEST_CHECK(magnitude.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_f32_is_nan(magnitude.value));

    result = r_std_math_exp_complex_f32(zero);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value.real == 1.0F);
    R_TEST_CHECK(result.value.imag == 0.0F);
    result = r_std_math_log_complex_f32(one);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value.real == 0.0F);
    R_TEST_CHECK(result.value.imag == 0.0F);
    result = r_std_math_sqrt_complex_f32((RStdMathComplexF32){4.0F, 0.0F});
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value.real == 2.0F);
    R_TEST_CHECK(result.value.imag == 0.0F);
    result = r_std_math_sin_complex_f32(zero);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value.real == 0.0F);
    R_TEST_CHECK(result.value.imag == 0.0F);
    result = r_std_math_cos_complex_f32(zero);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value.real == 1.0F);
    R_TEST_CHECK(result.value.imag == 0.0F);
    result = r_std_math_tan_complex_f32(zero);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value.real == 0.0F);
    R_TEST_CHECK(result.value.imag == 0.0F);
    result = r_std_math_sinh_complex_f32(zero);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value.real == 0.0F);
    R_TEST_CHECK(result.value.imag == 0.0F);
    result = r_std_math_cosh_complex_f32(zero);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value.real == 1.0F);
    R_TEST_CHECK(result.value.imag == 0.0F);
    result = r_std_math_tanh_complex_f32(zero);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value.real == 0.0F);
    R_TEST_CHECK(result.value.imag == 0.0F);

    result = r_std_math_div_complex_f32((RStdMathComplexF32){4.0F, 2.0F},
                                        (RStdMathComplexF32){2.0F, 0.0F});
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value.real == 2.0F);
    R_TEST_CHECK(result.value.imag == 1.0F);
    result = r_std_math_pow_complex_f32(one, (RStdMathComplexF32){3.0F, 0.0F});
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value.real == 1.0F);
    R_TEST_CHECK(result.value.imag == 0.0F);

    result = r_std_math_log_complex_f32((RStdMathComplexF32){-1.0F, 0.0F});
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value.real == 0.0F);
    R_TEST_CHECK((result.value.imag > 3.1415F) && (result.value.imag < 3.1416F));
    result = r_std_math_log_complex_f32((RStdMathComplexF32){-1.0F, -0.0F});
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value.real == 0.0F);
    R_TEST_CHECK((result.value.imag < -3.1415F) && (result.value.imag > -3.1416F));
    result = r_std_math_sqrt_complex_f32((RStdMathComplexF32){-4.0F, 0.0F});
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value.real == 0.0F);
    R_TEST_CHECK(result.value.imag == 2.0F);
    result = r_std_math_sqrt_complex_f32((RStdMathComplexF32){-4.0F, -0.0F});
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value.real == 0.0F);
    R_TEST_CHECK(result.value.imag == -2.0F);
    result = r_std_math_pow_complex_f32((RStdMathComplexF32){-4.0F, 0.0F},
                                        (RStdMathComplexF32){0.5F, 0.0F});
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK((result.value.real > -0.001F) && (result.value.real < 0.001F));
    R_TEST_CHECK((result.value.imag > 1.999F) && (result.value.imag < 2.001F));
    result = r_std_math_pow_complex_f32((RStdMathComplexF32){-4.0F, -0.0F},
                                        (RStdMathComplexF32){0.5F, 0.0F});
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK((result.value.real > -0.001F) && (result.value.real < 0.001F));
    R_TEST_CHECK((result.value.imag < -1.999F) && (result.value.imag > -2.001F));

    result = r_std_math_div_complex_f32(zero, zero);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_DOMAIN);
    result = r_std_math_div_complex_f32(one, zero);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_POLE);
    result = r_std_math_log_complex_f32(zero);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_POLE);
    result = r_std_math_pow_complex_f32(zero, (RStdMathComplexF32){-1.0F, 0.0F});
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_POLE);
    result = r_std_math_pow_complex_f32(zero, (RStdMathComplexF32){-0.5F, 0.0F});
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_f32_is_infinite(result.value.real));
    R_TEST_CHECK(r_test_f32_is_nan(result.value.imag));
    result = r_std_math_pow_complex_f32(zero, (RStdMathComplexF32){-1.0F, 1.0F});
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_f32_is_nan(result.value.real));
    R_TEST_CHECK(r_test_f32_is_nan(result.value.imag));
    result = r_std_math_pow_complex_f32(zero, one);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value.real == 0.0F);
    R_TEST_CHECK(result.value.imag == 0.0F);
    result = r_std_math_pow_complex_f32(zero, zero);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_f32_is_nan(result.value.real));

    result = r_std_math_exp_complex_f32((RStdMathComplexF32){100.0F, 0.0F});
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_OVERFLOW);
    result = r_std_math_sin_complex_f32((RStdMathComplexF32){0.0F, 100.0F});
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_OVERFLOW);
    result = r_std_math_exp_complex_f32((RStdMathComplexF32){-104.0F, 0.0F});
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_UNDERFLOW);
    result = r_std_math_tanh_complex_f32((RStdMathComplexF32){FLT_TRUE_MIN, 0.0F});
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_UNDERFLOW);
    result = r_std_math_exp_complex_f32((RStdMathComplexF32){1.0F, FLT_TRUE_MIN});
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_UNDERFLOW);
    result = r_std_math_div_complex_f32((RStdMathComplexF32){FLT_TRUE_MIN, 0.0F}, one);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value.real == FLT_TRUE_MIN);
    R_TEST_CHECK(result.value.imag == 0.0F);
    R_TEST_CHECK(!r_test_f32_sign_bit(result.value.imag));

    for (index = 0; index < sizeof(unary_operations) / sizeof(unary_operations[0]); ++index) {
        result = unary_operations[index]((RStdMathComplexF32){INFINITY, 0.0F});
        R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
        result = unary_operations[index]((RStdMathComplexF32){signaling_nan, 0.0F});
        R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    }
    complex_result = r_std_math_add_complex_f32((RStdMathComplexF32){signaling_nan, 0.0F}, one);
    R_TEST_CHECK(r_test_f32_is_nan(complex_result.real));
    result = r_std_math_div_complex_f32((RStdMathComplexF32){INFINITY, 0.0F}, one);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    result = r_std_math_div_complex_f32((RStdMathComplexF32){signaling_nan, 0.0F}, one);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    result = r_std_math_pow_complex_f32((RStdMathComplexF32){INFINITY, 0.0F}, one);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    result = r_std_math_pow_complex_f32((RStdMathComplexF32){signaling_nan, 0.0F}, one);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);

    R_TEST_CHECK(r_test_environment(FE_DOWNWARD, FE_DIVBYZERO, EDOM) == 0);
    R_TEST_CHECK(fesetround(original_rounding) == 0);
    R_TEST_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    return 0;
}

static int r_test_complex_f64(void) {
    static const RTestComplexUnaryF64 unary_operations[] = {
        r_std_math_exp_complex_f64,
        r_std_math_log_complex_f64,
        r_std_math_sqrt_complex_f64,
        r_std_math_sin_complex_f64,
        r_std_math_cos_complex_f64,
        r_std_math_tan_complex_f64,
        r_std_math_sinh_complex_f64,
        r_std_math_cosh_complex_f64,
        r_std_math_tanh_complex_f64,
    };
    const double positive_pi = 0x1.921fb54442d18p+1;
    double signaling_nan = r_test_f64_from_bits(UINT64_C(0x7ff0000000000001));
    RStdMathComplexF64 zero = {0.0, 0.0};
    RStdMathComplexF64 one = {1.0, 0.0};
    RStdMathComplexF64 value = {1.0, 2.0};
    RStdMathComplexF64 other = {3.0, -1.0};
    RStdMathComplexF64 complex_result;
    RStdMathComplexF64 complex_copy;
    RStdMathComplexF64Result result;
    RStdMathF64Result magnitude;
    int original_rounding = fegetround();
    size_t index;

    R_TEST_CHECK(original_rounding != -1);
    R_TEST_CHECK(fesetround(FE_UPWARD) == 0);
    R_TEST_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    R_TEST_CHECK(feraiseexcept(FE_INVALID) == 0);
    errno = EILSEQ;

    complex_copy = value;
    R_TEST_CHECK(complex_copy.real == 1.0);
    R_TEST_CHECK(complex_copy.imag == 2.0);
    complex_result = r_std_math_add_complex_f64(value, other);
    R_TEST_CHECK(complex_result.real == 4.0);
    R_TEST_CHECK(complex_result.imag == 1.0);
    complex_result = r_std_math_sub_complex_f64(value, other);
    R_TEST_CHECK(complex_result.real == -2.0);
    R_TEST_CHECK(complex_result.imag == 3.0);
    complex_result = r_std_math_mul_complex_f64(value, other);
    R_TEST_CHECK(complex_result.real == 5.0);
    R_TEST_CHECK(complex_result.imag == 5.0);
    complex_result = r_std_math_conjugate_complex_f64(value);
    R_TEST_CHECK(complex_result.real == 1.0);
    R_TEST_CHECK(complex_result.imag == -2.0);
    complex_result = r_std_math_conjugate_complex_f64((RStdMathComplexF64){0.0, 0.0});
    R_TEST_CHECK(!r_test_f64_sign_bit(complex_result.real));
    R_TEST_CHECK(r_test_f64_sign_bit(complex_result.imag));
    R_TEST_CHECK(r_std_math_phase_complex_f64((RStdMathComplexF64){-1.0, 0.0}) == positive_pi);
    R_TEST_CHECK(r_std_math_phase_complex_f64((RStdMathComplexF64){-1.0, -0.0}) == -positive_pi);

    magnitude = r_std_math_magnitude_complex_f64((RStdMathComplexF64){3.0, 4.0});
    R_TEST_CHECK(magnitude.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(magnitude.value == 5.0);
    magnitude = r_std_math_magnitude_complex_f64((RStdMathComplexF64){DBL_TRUE_MIN, 0.0});
    R_TEST_CHECK(magnitude.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(magnitude.value == DBL_TRUE_MIN);
    magnitude = r_std_math_magnitude_complex_f64((RStdMathComplexF64){DBL_TRUE_MIN, DBL_TRUE_MIN});
    R_TEST_CHECK(magnitude.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(magnitude.error.code == R_STD_MATH_ERROR_UNDERFLOW);
    R_TEST_CHECK(magnitude.error.native_code == INT64_C(0));
    magnitude = r_std_math_magnitude_complex_f64((RStdMathComplexF64){DBL_MAX, DBL_MAX});
    R_TEST_CHECK(magnitude.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(magnitude.error.code == R_STD_MATH_ERROR_OVERFLOW);
    R_TEST_CHECK(magnitude.error.native_code == INT64_C(0));
    magnitude = r_std_math_magnitude_complex_f64((RStdMathComplexF64){INFINITY, 0.0});
    R_TEST_CHECK(magnitude.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_f64_is_infinite(magnitude.value));
    magnitude = r_std_math_magnitude_complex_f64((RStdMathComplexF64){signaling_nan, 0.0});
    R_TEST_CHECK(magnitude.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_f64_is_nan(magnitude.value));

    result = r_std_math_exp_complex_f64(zero);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value.real == 1.0);
    R_TEST_CHECK(result.value.imag == 0.0);
    result = r_std_math_log_complex_f64(one);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value.real == 0.0);
    R_TEST_CHECK(result.value.imag == 0.0);
    result = r_std_math_sqrt_complex_f64((RStdMathComplexF64){4.0, 0.0});
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value.real == 2.0);
    R_TEST_CHECK(result.value.imag == 0.0);
    result = r_std_math_sin_complex_f64(zero);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value.real == 0.0);
    R_TEST_CHECK(result.value.imag == 0.0);
    result = r_std_math_cos_complex_f64(zero);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value.real == 1.0);
    R_TEST_CHECK(result.value.imag == 0.0);
    result = r_std_math_tan_complex_f64(zero);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value.real == 0.0);
    R_TEST_CHECK(result.value.imag == 0.0);
    result = r_std_math_sinh_complex_f64(zero);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value.real == 0.0);
    R_TEST_CHECK(result.value.imag == 0.0);
    result = r_std_math_cosh_complex_f64(zero);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value.real == 1.0);
    R_TEST_CHECK(result.value.imag == 0.0);
    result = r_std_math_tanh_complex_f64(zero);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value.real == 0.0);
    R_TEST_CHECK(result.value.imag == 0.0);

    result =
        r_std_math_div_complex_f64((RStdMathComplexF64){4.0, 2.0}, (RStdMathComplexF64){2.0, 0.0});
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value.real == 2.0);
    R_TEST_CHECK(result.value.imag == 1.0);
    result = r_std_math_pow_complex_f64(one, (RStdMathComplexF64){3.0, 0.0});
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value.real == 1.0);
    R_TEST_CHECK(result.value.imag == 0.0);

    result = r_std_math_log_complex_f64((RStdMathComplexF64){-1.0, 0.0});
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value.real == 0.0);
    R_TEST_CHECK(result.value.imag == positive_pi);
    result = r_std_math_log_complex_f64((RStdMathComplexF64){-1.0, -0.0});
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value.real == 0.0);
    R_TEST_CHECK(result.value.imag == -positive_pi);
    result = r_std_math_sqrt_complex_f64((RStdMathComplexF64){-4.0, 0.0});
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value.real == 0.0);
    R_TEST_CHECK(result.value.imag == 2.0);
    result = r_std_math_sqrt_complex_f64((RStdMathComplexF64){-4.0, -0.0});
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value.real == 0.0);
    R_TEST_CHECK(result.value.imag == -2.0);
    result =
        r_std_math_pow_complex_f64((RStdMathComplexF64){-4.0, 0.0}, (RStdMathComplexF64){0.5, 0.0});
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK((result.value.real > -0.000000000001) && (result.value.real < 0.000000000001));
    R_TEST_CHECK((result.value.imag > 1.999999999999) && (result.value.imag < 2.000000000001));
    result = r_std_math_pow_complex_f64((RStdMathComplexF64){-4.0, -0.0},
                                        (RStdMathComplexF64){0.5, 0.0});
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK((result.value.real > -0.000000000001) && (result.value.real < 0.000000000001));
    R_TEST_CHECK((result.value.imag < -1.999999999999) && (result.value.imag > -2.000000000001));

    result = r_std_math_div_complex_f64(zero, zero);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_DOMAIN);
    result = r_std_math_div_complex_f64(one, zero);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_POLE);
    result = r_std_math_log_complex_f64(zero);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_POLE);
    result = r_std_math_pow_complex_f64(zero, (RStdMathComplexF64){-1.0, 0.0});
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_POLE);
    result = r_std_math_pow_complex_f64(zero, (RStdMathComplexF64){-0.5, 0.0});
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_f64_is_infinite(result.value.real));
    R_TEST_CHECK(r_test_f64_is_nan(result.value.imag));
    result = r_std_math_pow_complex_f64(zero, (RStdMathComplexF64){-1.0, 1.0});
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_f64_is_nan(result.value.real));
    R_TEST_CHECK(r_test_f64_is_nan(result.value.imag));
    result = r_std_math_pow_complex_f64(zero, one);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value.real == 0.0);
    R_TEST_CHECK(result.value.imag == 0.0);
    result = r_std_math_pow_complex_f64(zero, zero);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_f64_is_nan(result.value.real));

    result = r_std_math_exp_complex_f64((RStdMathComplexF64){1000.0, 0.0});
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_OVERFLOW);
    result = r_std_math_sin_complex_f64((RStdMathComplexF64){0.0, 1000.0});
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_OVERFLOW);
    result = r_std_math_exp_complex_f64((RStdMathComplexF64){-746.0, 0.0});
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_UNDERFLOW);
    result = r_std_math_tanh_complex_f64((RStdMathComplexF64){DBL_TRUE_MIN, 0.0});
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_UNDERFLOW);
    result = r_std_math_exp_complex_f64((RStdMathComplexF64){1.0, DBL_TRUE_MIN});
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_UNDERFLOW);
    result = r_std_math_div_complex_f64((RStdMathComplexF64){DBL_TRUE_MIN, 0.0}, one);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value.real == DBL_TRUE_MIN);
    R_TEST_CHECK(result.value.imag == 0.0);
    R_TEST_CHECK(!r_test_f64_sign_bit(result.value.imag));

    for (index = 0; index < sizeof(unary_operations) / sizeof(unary_operations[0]); ++index) {
        result = unary_operations[index]((RStdMathComplexF64){INFINITY, 0.0});
        R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
        result = unary_operations[index]((RStdMathComplexF64){signaling_nan, 0.0});
        R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    }
    complex_result = r_std_math_add_complex_f64((RStdMathComplexF64){signaling_nan, 0.0}, one);
    R_TEST_CHECK(r_test_f64_is_nan(complex_result.real));
    result = r_std_math_div_complex_f64((RStdMathComplexF64){INFINITY, 0.0}, one);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    result = r_std_math_div_complex_f64((RStdMathComplexF64){signaling_nan, 0.0}, one);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    result = r_std_math_pow_complex_f64((RStdMathComplexF64){INFINITY, 0.0}, one);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    result = r_std_math_pow_complex_f64((RStdMathComplexF64){signaling_nan, 0.0}, one);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);

    R_TEST_CHECK(r_test_environment(FE_UPWARD, FE_INVALID, EILSEQ) == 0);
    R_TEST_CHECK(fesetround(original_rounding) == 0);
    R_TEST_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    return 0;
}

static void *r_test_complex_thread(void *opaque_context) {
    RTestComplexThreadContext *context = opaque_context;
    int iteration;

    if ((fesetround(context->rounding) != 0) || (feclearexcept(FE_ALL_EXCEPT) != 0) ||
        (feraiseexcept(context->exceptions) != 0)) {
        context->failed = 1;
        return NULL;
    }
    errno = context->native_errno;
    for (iteration = 0; iteration < 300; ++iteration) {
        RStdMathComplexF64 sum = r_std_math_add_complex_f64((RStdMathComplexF64){1.0, 2.0},
                                                            (RStdMathComplexF64){3.0, -1.0});
        RStdMathComplexF64Result square_root =
            r_std_math_sqrt_complex_f64((RStdMathComplexF64){-4.0, -0.0});
        RStdMathComplexF64Result exponential =
            r_std_math_exp_complex_f64((RStdMathComplexF64){0.0, 0.0});
        RStdMathF64Result magnitude =
            r_std_math_magnitude_complex_f64((RStdMathComplexF64){3.0, 4.0});

        if ((sum.real != 4.0) || (sum.imag != 1.0) ||
            (square_root.status != R_STD_MATH_CALL_SUCCESS) || (square_root.value.imag != -2.0) ||
            (exponential.status != R_STD_MATH_CALL_SUCCESS) || (exponential.value.real != 1.0) ||
            (magnitude.status != R_STD_MATH_CALL_SUCCESS) || (magnitude.value != 5.0) ||
            (fegetround() != context->rounding) ||
            ((fetestexcept(FE_ALL_EXCEPT) & FE_ALL_EXCEPT) != context->exceptions) ||
            (errno != context->native_errno)) {
            context->failed = 1;
            return NULL;
        }
    }
    return NULL;
}

static int r_test_concurrent_environment(void) {
    pthread_t first_thread;
    pthread_t second_thread;
    RTestComplexThreadContext first = {FE_DOWNWARD, FE_DIVBYZERO, EDOM, 0};
    RTestComplexThreadContext second = {FE_UPWARD, FE_INVALID, EILSEQ, 0};

    R_TEST_CHECK(pthread_create(&first_thread, NULL, r_test_complex_thread, &first) == 0);
    R_TEST_CHECK(pthread_create(&second_thread, NULL, r_test_complex_thread, &second) == 0);
    R_TEST_CHECK(pthread_join(first_thread, NULL) == 0);
    R_TEST_CHECK(pthread_join(second_thread, NULL) == 0);
    R_TEST_CHECK(first.failed == 0);
    R_TEST_CHECK(second.failed == 0);
    return 0;
}

int main(void) {
    R_TEST_CHECK(r_test_complex_f32() == 0);
    R_TEST_CHECK(r_test_complex_f64() == 0);
    R_TEST_CHECK(r_test_concurrent_environment() == 0);
    (void)fprintf(stdout, "library_math_complex_tests: ok\n");
    return 0;
}

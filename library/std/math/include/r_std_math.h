#ifndef R_STD_MATH_H
#define R_STD_MATH_H

#include "r_std_error.h"

#include <stdint.h>

typedef enum RStdMathErrorCode {
    R_STD_MATH_ERROR_DOMAIN = 0,
    R_STD_MATH_ERROR_POLE = 1,
    R_STD_MATH_ERROR_OVERFLOW = 2,
    R_STD_MATH_ERROR_UNDERFLOW = 3
} RStdMathErrorCode;

typedef struct RStdMathError {
    RStdMathErrorCode code;
    int64_t native_code;
} RStdMathError;

typedef enum RStdMathCallStatus {
    R_STD_MATH_CALL_SUCCESS = 0,
    R_STD_MATH_CALL_ERROR = 1,
    R_STD_MATH_CALL_CONTRACT_VIOLATION = 2
} RStdMathCallStatus;

typedef struct RStdMathF64Result {
    RStdMathCallStatus status;
    double value;
    RStdMathError error;
} RStdMathF64Result;

typedef struct RStdMathF32Result {
    RStdMathCallStatus status;
    float value;
    RStdMathError error;
} RStdMathF32Result;

typedef struct RStdMathCFloatResult {
    RStdMathCallStatus status;
    float value;
    RStdMathError error;
} RStdMathCFloatResult;

typedef struct RStdMathCDoubleResult {
    RStdMathCallStatus status;
    double value;
    RStdMathError error;
} RStdMathCDoubleResult;

typedef struct RStdMathCLongDoubleResult {
    RStdMathCallStatus status;
    long double value;
    RStdMathError error;
} RStdMathCLongDoubleResult;

typedef struct RStdMathComplexF32 {
    float real;
    float imag;
} RStdMathComplexF32;

typedef struct RStdMathComplexF64 {
    double real;
    double imag;
} RStdMathComplexF64;

typedef struct RStdMathComplexF32Result {
    RStdMathCallStatus status;
    RStdMathComplexF32 value;
    RStdMathError error;
} RStdMathComplexF32Result;

typedef struct RStdMathComplexF64Result {
    RStdMathCallStatus status;
    RStdMathComplexF64 value;
    RStdMathError error;
} RStdMathComplexF64Result;

typedef struct RStdMathFractionPartsF32 {
    float whole;
    float fraction;
} RStdMathFractionPartsF32;

typedef struct RStdMathFractionPartsF64 {
    double whole;
    double fraction;
} RStdMathFractionPartsF64;

typedef struct RStdMathFractionPartsCFloat {
    float whole;
    float fraction;
} RStdMathFractionPartsCFloat;

typedef struct RStdMathFractionPartsCDouble {
    double whole;
    double fraction;
} RStdMathFractionPartsCDouble;

typedef struct RStdMathFractionPartsCLongDouble {
    long double whole;
    long double fraction;
} RStdMathFractionPartsCLongDouble;

typedef struct RStdMathBinaryPartsF32 {
    float fraction;
    int32_t exponent;
} RStdMathBinaryPartsF32;

typedef struct RStdMathBinaryPartsF64 {
    double fraction;
    int32_t exponent;
} RStdMathBinaryPartsF64;

typedef struct RStdMathBinaryPartsCFloat {
    float fraction;
    int32_t exponent;
} RStdMathBinaryPartsCFloat;

typedef struct RStdMathBinaryPartsCDouble {
    double fraction;
    int32_t exponent;
} RStdMathBinaryPartsCDouble;

typedef struct RStdMathBinaryPartsCLongDouble {
    long double fraction;
    int32_t exponent;
} RStdMathBinaryPartsCLongDouble;

RStdError r_std_math_as_error(RStdMathError value);

float r_std_math_abs_f32(float value);
double r_std_math_abs_f64(double value);
float r_std_math_abs_c_float(float value);
double r_std_math_abs_c_double(double value);
long double r_std_math_abs_c_long_double(long double value);
float r_std_math_floor_f32(float value);
double r_std_math_floor_f64(double value);
float r_std_math_floor_c_float(float value);
double r_std_math_floor_c_double(double value);
long double r_std_math_floor_c_long_double(long double value);
float r_std_math_ceil_f32(float value);
double r_std_math_ceil_f64(double value);
float r_std_math_ceil_c_float(float value);
double r_std_math_ceil_c_double(double value);
long double r_std_math_ceil_c_long_double(long double value);
float r_std_math_trunc_f32(float value);
double r_std_math_trunc_f64(double value);
float r_std_math_trunc_c_float(float value);
double r_std_math_trunc_c_double(double value);
long double r_std_math_trunc_c_long_double(long double value);
float r_std_math_round_f32(float value);
double r_std_math_round_f64(double value);
float r_std_math_round_c_float(float value);
double r_std_math_round_c_double(double value);
long double r_std_math_round_c_long_double(long double value);
float r_std_math_copy_sign_f32(float magnitude, float sign);
double r_std_math_copy_sign_f64(double magnitude, double sign);
float r_std_math_copy_sign_c_float(float magnitude, float sign);
double r_std_math_copy_sign_c_double(double magnitude, double sign);
long double r_std_math_copy_sign_c_long_double(long double magnitude, long double sign);
float r_std_math_min_f32(float left, float right);
double r_std_math_min_f64(double left, double right);
float r_std_math_min_c_float(float left, float right);
double r_std_math_min_c_double(double left, double right);
long double r_std_math_min_c_long_double(long double left, long double right);
float r_std_math_max_f32(float left, float right);
double r_std_math_max_f64(double left, double right);
float r_std_math_max_c_float(float left, float right);
double r_std_math_max_c_double(double left, double right);
long double r_std_math_max_c_long_double(long double left, long double right);
float r_std_math_next_after_f32(float from, float toward);
double r_std_math_next_after_f64(double from, double toward);
float r_std_math_next_after_c_float(float from, float toward);
double r_std_math_next_after_c_double(double from, double toward);
long double r_std_math_next_after_c_long_double(long double from, long double toward);
_Bool r_std_math_is_finite_f32(float value);
_Bool r_std_math_is_finite_f64(double value);
_Bool r_std_math_is_finite_c_float(float value);
_Bool r_std_math_is_finite_c_double(double value);
_Bool r_std_math_is_finite_c_long_double(long double value);
_Bool r_std_math_is_infinite_f32(float value);
_Bool r_std_math_is_infinite_f64(double value);
_Bool r_std_math_is_infinite_c_float(float value);
_Bool r_std_math_is_infinite_c_double(double value);
_Bool r_std_math_is_infinite_c_long_double(long double value);
_Bool r_std_math_is_nan_f32(float value);
_Bool r_std_math_is_nan_f64(double value);
_Bool r_std_math_is_nan_c_float(float value);
_Bool r_std_math_is_nan_c_double(double value);
_Bool r_std_math_is_nan_c_long_double(long double value);
_Bool r_std_math_is_normal_f32(float value);
_Bool r_std_math_is_normal_f64(double value);
_Bool r_std_math_is_normal_c_float(float value);
_Bool r_std_math_is_normal_c_double(double value);
_Bool r_std_math_is_normal_c_long_double(long double value);
_Bool r_std_math_sign_bit_f32(float value);
_Bool r_std_math_sign_bit_f64(double value);
_Bool r_std_math_sign_bit_c_float(float value);
_Bool r_std_math_sign_bit_c_double(double value);
_Bool r_std_math_sign_bit_c_long_double(long double value);

RStdMathFractionPartsF32 r_std_math_split_fraction_f32(float value);
RStdMathFractionPartsF64 r_std_math_split_fraction_f64(double value);
RStdMathFractionPartsCFloat r_std_math_split_fraction_c_float(float value);
RStdMathFractionPartsCDouble r_std_math_split_fraction_c_double(double value);
RStdMathFractionPartsCLongDouble r_std_math_split_fraction_c_long_double(long double value);
RStdMathBinaryPartsF32 r_std_math_split_binary_f32(float value);
RStdMathBinaryPartsF64 r_std_math_split_binary_f64(double value);
RStdMathBinaryPartsCFloat r_std_math_split_binary_c_float(float value);
RStdMathBinaryPartsCDouble r_std_math_split_binary_c_double(double value);
RStdMathBinaryPartsCLongDouble r_std_math_split_binary_c_long_double(long double value);
RStdMathF32Result r_std_math_compose_binary_f32(float fraction, int32_t exponent);
RStdMathF64Result r_std_math_compose_binary_f64(double fraction, int32_t exponent);
RStdMathCFloatResult r_std_math_compose_binary_c_float(float fraction, int32_t exponent);
RStdMathCDoubleResult r_std_math_compose_binary_c_double(double fraction, int32_t exponent);
RStdMathCLongDoubleResult r_std_math_compose_binary_c_long_double(long double fraction,
                                                                  int32_t exponent);

RStdMathF32Result r_std_math_sqrt_f32(float value);
RStdMathF64Result r_std_math_sqrt_f64(double value);
RStdMathCFloatResult r_std_math_sqrt_c_float(float value);
RStdMathCDoubleResult r_std_math_sqrt_c_double(double value);
RStdMathCLongDoubleResult r_std_math_sqrt_c_long_double(long double value);
RStdMathF32Result r_std_math_cbrt_f32(float value);
RStdMathF64Result r_std_math_cbrt_f64(double value);
RStdMathCFloatResult r_std_math_cbrt_c_float(float value);
RStdMathCDoubleResult r_std_math_cbrt_c_double(double value);
RStdMathCLongDoubleResult r_std_math_cbrt_c_long_double(long double value);
RStdMathF32Result r_std_math_erf_f32(float value);
RStdMathF64Result r_std_math_erf_f64(double value);
RStdMathCFloatResult r_std_math_erf_c_float(float value);
RStdMathCDoubleResult r_std_math_erf_c_double(double value);
RStdMathCLongDoubleResult r_std_math_erf_c_long_double(long double value);
RStdMathF32Result r_std_math_erfc_f32(float value);
RStdMathF64Result r_std_math_erfc_f64(double value);
RStdMathCFloatResult r_std_math_erfc_c_float(float value);
RStdMathCDoubleResult r_std_math_erfc_c_double(double value);
RStdMathCLongDoubleResult r_std_math_erfc_c_long_double(long double value);
RStdMathF32Result r_std_math_exp2_f32(float value);
RStdMathF64Result r_std_math_exp2_f64(double value);
RStdMathCFloatResult r_std_math_exp2_c_float(float value);
RStdMathCDoubleResult r_std_math_exp2_c_double(double value);
RStdMathCLongDoubleResult r_std_math_exp2_c_long_double(long double value);
RStdMathF32Result r_std_math_exp_f32(float value);
RStdMathF64Result r_std_math_exp_f64(double value);
RStdMathCFloatResult r_std_math_exp_c_float(float value);
RStdMathCDoubleResult r_std_math_exp_c_double(double value);
RStdMathCLongDoubleResult r_std_math_exp_c_long_double(long double value);
RStdMathF32Result r_std_math_expm1_f32(float value);
RStdMathF64Result r_std_math_expm1_f64(double value);
RStdMathCFloatResult r_std_math_expm1_c_float(float value);
RStdMathCDoubleResult r_std_math_expm1_c_double(double value);
RStdMathCLongDoubleResult r_std_math_expm1_c_long_double(long double value);
RStdMathF32Result r_std_math_gamma_f32(float value);
RStdMathF64Result r_std_math_gamma_f64(double value);
RStdMathCFloatResult r_std_math_gamma_c_float(float value);
RStdMathCDoubleResult r_std_math_gamma_c_double(double value);
RStdMathCLongDoubleResult r_std_math_gamma_c_long_double(long double value);
RStdMathF32Result r_std_math_log_gamma_f32(float value);
RStdMathF64Result r_std_math_log_gamma_f64(double value);
RStdMathCFloatResult r_std_math_log_gamma_c_float(float value);
RStdMathCDoubleResult r_std_math_log_gamma_c_double(double value);
RStdMathCLongDoubleResult r_std_math_log_gamma_c_long_double(long double value);
RStdMathF32Result r_std_math_log_f32(float value);
RStdMathF64Result r_std_math_log_f64(double value);
RStdMathCFloatResult r_std_math_log_c_float(float value);
RStdMathCDoubleResult r_std_math_log_c_double(double value);
RStdMathCLongDoubleResult r_std_math_log_c_long_double(long double value);
RStdMathF32Result r_std_math_log2_f32(float value);
RStdMathF64Result r_std_math_log2_f64(double value);
RStdMathCFloatResult r_std_math_log2_c_float(float value);
RStdMathCDoubleResult r_std_math_log2_c_double(double value);
RStdMathCLongDoubleResult r_std_math_log2_c_long_double(long double value);
RStdMathF32Result r_std_math_log10_f32(float value);
RStdMathF64Result r_std_math_log10_f64(double value);
RStdMathCFloatResult r_std_math_log10_c_float(float value);
RStdMathCDoubleResult r_std_math_log10_c_double(double value);
RStdMathCLongDoubleResult r_std_math_log10_c_long_double(long double value);
RStdMathF32Result r_std_math_log1p_f32(float value);
RStdMathF64Result r_std_math_log1p_f64(double value);
RStdMathCFloatResult r_std_math_log1p_c_float(float value);
RStdMathCDoubleResult r_std_math_log1p_c_double(double value);
RStdMathCLongDoubleResult r_std_math_log1p_c_long_double(long double value);
RStdMathF32Result r_std_math_sin_f32(float value);
RStdMathF64Result r_std_math_sin_f64(double value);
RStdMathCFloatResult r_std_math_sin_c_float(float value);
RStdMathCDoubleResult r_std_math_sin_c_double(double value);
RStdMathCLongDoubleResult r_std_math_sin_c_long_double(long double value);
RStdMathF32Result r_std_math_sinh_f32(float value);
RStdMathF64Result r_std_math_sinh_f64(double value);
RStdMathCFloatResult r_std_math_sinh_c_float(float value);
RStdMathCDoubleResult r_std_math_sinh_c_double(double value);
RStdMathCLongDoubleResult r_std_math_sinh_c_long_double(long double value);
RStdMathF32Result r_std_math_tan_f32(float value);
RStdMathF64Result r_std_math_tan_f64(double value);
RStdMathCFloatResult r_std_math_tan_c_float(float value);
RStdMathCDoubleResult r_std_math_tan_c_double(double value);
RStdMathCLongDoubleResult r_std_math_tan_c_long_double(long double value);
RStdMathF32Result r_std_math_cos_f32(float value);
RStdMathF64Result r_std_math_cos_f64(double value);
RStdMathCFloatResult r_std_math_cos_c_float(float value);
RStdMathCDoubleResult r_std_math_cos_c_double(double value);
RStdMathCLongDoubleResult r_std_math_cos_c_long_double(long double value);
RStdMathF32Result r_std_math_cosh_f32(float value);
RStdMathF64Result r_std_math_cosh_f64(double value);
RStdMathCFloatResult r_std_math_cosh_c_float(float value);
RStdMathCDoubleResult r_std_math_cosh_c_double(double value);
RStdMathCLongDoubleResult r_std_math_cosh_c_long_double(long double value);
RStdMathF32Result r_std_math_acos_f32(float value);
RStdMathF64Result r_std_math_acos_f64(double value);
RStdMathCFloatResult r_std_math_acos_c_float(float value);
RStdMathCDoubleResult r_std_math_acos_c_double(double value);
RStdMathCLongDoubleResult r_std_math_acos_c_long_double(long double value);
RStdMathF32Result r_std_math_acosh_f32(float value);
RStdMathF64Result r_std_math_acosh_f64(double value);
RStdMathCFloatResult r_std_math_acosh_c_float(float value);
RStdMathCDoubleResult r_std_math_acosh_c_double(double value);
RStdMathCLongDoubleResult r_std_math_acosh_c_long_double(long double value);
RStdMathF32Result r_std_math_asin_f32(float value);
RStdMathF64Result r_std_math_asin_f64(double value);
RStdMathCFloatResult r_std_math_asin_c_float(float value);
RStdMathCDoubleResult r_std_math_asin_c_double(double value);
RStdMathCLongDoubleResult r_std_math_asin_c_long_double(long double value);
RStdMathF32Result r_std_math_asinh_f32(float value);
RStdMathF64Result r_std_math_asinh_f64(double value);
RStdMathCFloatResult r_std_math_asinh_c_float(float value);
RStdMathCDoubleResult r_std_math_asinh_c_double(double value);
RStdMathCLongDoubleResult r_std_math_asinh_c_long_double(long double value);
RStdMathF32Result r_std_math_atan_f32(float value);
RStdMathF64Result r_std_math_atan_f64(double value);
RStdMathCFloatResult r_std_math_atan_c_float(float value);
RStdMathCDoubleResult r_std_math_atan_c_double(double value);
RStdMathCLongDoubleResult r_std_math_atan_c_long_double(long double value);
RStdMathF32Result r_std_math_atan2_f32(float left, float right);
RStdMathF64Result r_std_math_atan2_f64(double left, double right);
RStdMathCFloatResult r_std_math_atan2_c_float(float left, float right);
RStdMathCDoubleResult r_std_math_atan2_c_double(double left, double right);
RStdMathCLongDoubleResult r_std_math_atan2_c_long_double(long double left, long double right);
RStdMathF32Result r_std_math_pow_f32(float base, float exponent);
RStdMathF64Result r_std_math_pow_f64(double base, double exponent);
RStdMathCFloatResult r_std_math_pow_c_float(float base, float exponent);
RStdMathCDoubleResult r_std_math_pow_c_double(double base, double exponent);
RStdMathCLongDoubleResult r_std_math_pow_c_long_double(long double base, long double exponent);
RStdMathF32Result r_std_math_hypot_f32(float left, float right);
RStdMathF64Result r_std_math_hypot_f64(double left, double right);
RStdMathCFloatResult r_std_math_hypot_c_float(float left, float right);
RStdMathCDoubleResult r_std_math_hypot_c_double(double left, double right);
RStdMathCLongDoubleResult r_std_math_hypot_c_long_double(long double left, long double right);
RStdMathF32Result r_std_math_remainder_f32(float left, float right);
RStdMathF64Result r_std_math_remainder_f64(double left, double right);
RStdMathCFloatResult r_std_math_remainder_c_float(float left, float right);
RStdMathCDoubleResult r_std_math_remainder_c_double(double left, double right);
RStdMathCLongDoubleResult r_std_math_remainder_c_long_double(long double left, long double right);
RStdMathF32Result r_std_math_atanh_f32(float value);
RStdMathF64Result r_std_math_atanh_f64(double value);
RStdMathCFloatResult r_std_math_atanh_c_float(float value);
RStdMathCDoubleResult r_std_math_atanh_c_double(double value);
RStdMathCLongDoubleResult r_std_math_atanh_c_long_double(long double value);
RStdMathF32Result r_std_math_tanh_f32(float value);
RStdMathF64Result r_std_math_tanh_f64(double value);
RStdMathCFloatResult r_std_math_tanh_c_float(float value);
RStdMathCDoubleResult r_std_math_tanh_c_double(double value);
RStdMathCLongDoubleResult r_std_math_tanh_c_long_double(long double value);

RStdMathComplexF32 r_std_math_add_complex_f32(RStdMathComplexF32 left, RStdMathComplexF32 right);
RStdMathComplexF64 r_std_math_add_complex_f64(RStdMathComplexF64 left, RStdMathComplexF64 right);
RStdMathComplexF32 r_std_math_sub_complex_f32(RStdMathComplexF32 left, RStdMathComplexF32 right);
RStdMathComplexF64 r_std_math_sub_complex_f64(RStdMathComplexF64 left, RStdMathComplexF64 right);
RStdMathComplexF32 r_std_math_mul_complex_f32(RStdMathComplexF32 left, RStdMathComplexF32 right);
RStdMathComplexF64 r_std_math_mul_complex_f64(RStdMathComplexF64 left, RStdMathComplexF64 right);
RStdMathComplexF32 r_std_math_conjugate_complex_f32(RStdMathComplexF32 value);
RStdMathComplexF64 r_std_math_conjugate_complex_f64(RStdMathComplexF64 value);
float r_std_math_phase_complex_f32(RStdMathComplexF32 value);
double r_std_math_phase_complex_f64(RStdMathComplexF64 value);
RStdMathF32Result r_std_math_magnitude_complex_f32(RStdMathComplexF32 value);
RStdMathF64Result r_std_math_magnitude_complex_f64(RStdMathComplexF64 value);
RStdMathComplexF32Result r_std_math_exp_complex_f32(RStdMathComplexF32 value);
RStdMathComplexF64Result r_std_math_exp_complex_f64(RStdMathComplexF64 value);
RStdMathComplexF32Result r_std_math_log_complex_f32(RStdMathComplexF32 value);
RStdMathComplexF64Result r_std_math_log_complex_f64(RStdMathComplexF64 value);
RStdMathComplexF32Result r_std_math_sqrt_complex_f32(RStdMathComplexF32 value);
RStdMathComplexF64Result r_std_math_sqrt_complex_f64(RStdMathComplexF64 value);
RStdMathComplexF32Result r_std_math_sin_complex_f32(RStdMathComplexF32 value);
RStdMathComplexF64Result r_std_math_sin_complex_f64(RStdMathComplexF64 value);
RStdMathComplexF32Result r_std_math_cos_complex_f32(RStdMathComplexF32 value);
RStdMathComplexF64Result r_std_math_cos_complex_f64(RStdMathComplexF64 value);
RStdMathComplexF32Result r_std_math_tan_complex_f32(RStdMathComplexF32 value);
RStdMathComplexF64Result r_std_math_tan_complex_f64(RStdMathComplexF64 value);
RStdMathComplexF32Result r_std_math_sinh_complex_f32(RStdMathComplexF32 value);
RStdMathComplexF64Result r_std_math_sinh_complex_f64(RStdMathComplexF64 value);
RStdMathComplexF32Result r_std_math_cosh_complex_f32(RStdMathComplexF32 value);
RStdMathComplexF64Result r_std_math_cosh_complex_f64(RStdMathComplexF64 value);
RStdMathComplexF32Result r_std_math_tanh_complex_f32(RStdMathComplexF32 value);
RStdMathComplexF64Result r_std_math_tanh_complex_f64(RStdMathComplexF64 value);
RStdMathComplexF32Result r_std_math_div_complex_f32(RStdMathComplexF32 left,
                                                    RStdMathComplexF32 right);
RStdMathComplexF64Result r_std_math_div_complex_f64(RStdMathComplexF64 left,
                                                    RStdMathComplexF64 right);
RStdMathComplexF32Result r_std_math_pow_complex_f32(RStdMathComplexF32 left,
                                                    RStdMathComplexF32 right);
RStdMathComplexF64Result r_std_math_pow_complex_f64(RStdMathComplexF64 left,
                                                    RStdMathComplexF64 right);

#endif

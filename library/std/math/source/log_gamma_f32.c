#if !defined(__APPLE__)
#error "std.math::log_gamma_f32 requires the Darwin reentrant lgamma API"
#endif

#if !defined(_REENTRANT)
#define _REENTRANT 1
#endif

#include "r_std_math.h"

#include "r_library_math_environment.h"

#include <fenv.h>
#include <math.h>
#include <stdint.h>

#pragma STDC FENV_ACCESS ON

RStdMathF32Result r_std_math_log_gamma_f32(float value) {
    RStdMathF32Result result = {0};
    RLibraryMathEnvironment environment = {0};
    RLibraryMathIndicators indicators;
    volatile float canonical_operand;
    float computed;
    int gamma_sign;
    int classification;
    _Bool finite_operand;
    _Bool negative_integer;
    _Bool pole;
    _Bool overflow;
    _Bool underflow;

    r_library_internal_math_environment_begin(&environment);
    canonical_operand = value;
    computed = lgammaf_r(canonical_operand, &gamma_sign);
    classification = fpclassify(computed);
    finite_operand = isfinite(canonical_operand) != 0;
    negative_integer = finite_operand && (canonical_operand < 0.0F) &&
                       (truncf(canonical_operand) == canonical_operand);
    pole = (canonical_operand == 0.0F) || negative_integer;
    indicators = r_library_internal_math_environment_end(&environment);
    overflow = finite_operand && !pole && (classification == FP_INFINITE);
    underflow = finite_operand && !pole &&
                ((classification == FP_ZERO) || (classification == FP_SUBNORMAL)) &&
                ((indicators.floating_exceptions & FE_INEXACT) != 0);
    (void)gamma_sign;
    if (pole) {
        result.status = R_STD_MATH_CALL_ERROR;
        result.error.code = R_STD_MATH_ERROR_POLE;
        result.error.native_code = (int64_t)indicators.native_errno;
    } else if (overflow) {
        result.status = R_STD_MATH_CALL_ERROR;
        result.error.code = R_STD_MATH_ERROR_OVERFLOW;
        result.error.native_code = (int64_t)indicators.native_errno;
    } else if (underflow) {
        result.status = R_STD_MATH_CALL_ERROR;
        result.error.code = R_STD_MATH_ERROR_UNDERFLOW;
        result.error.native_code = (int64_t)indicators.native_errno;
    } else {
        result.status = R_STD_MATH_CALL_SUCCESS;
        result.value = computed;
    }
    return result;
}

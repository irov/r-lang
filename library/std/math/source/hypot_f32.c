#include "r_std_math.h"

#include "r_library_math_environment.h"

#include <fenv.h>
#include <math.h>
#include <stdint.h>

#pragma STDC FENV_ACCESS ON

RStdMathF32Result r_std_math_hypot_f32(float left, float right) {
    RStdMathF32Result result = {0};
    RLibraryMathEnvironment environment = {0};
    RLibraryMathIndicators indicators;
    volatile float canonical_left;
    volatile float canonical_right;
    float computed;
    int classification;
    _Bool finite_operands;
    _Bool nonzero_operand;
    _Bool overflow;
    _Bool underflow;

    r_library_internal_math_environment_begin(&environment);
    canonical_left = left;
    canonical_right = right;
    computed = hypotf(canonical_left, canonical_right);
    classification = fpclassify(computed);
    finite_operands = isfinite(canonical_left) && isfinite(canonical_right);
    nonzero_operand = (canonical_left != 0.0F) || (canonical_right != 0.0F);
    indicators = r_library_internal_math_environment_end(&environment);
    overflow = finite_operands && (classification == FP_INFINITE);
    underflow =
        finite_operands && nonzero_operand && !overflow &&
        ((classification == FP_ZERO) || ((classification == FP_SUBNORMAL) &&
                                         ((indicators.floating_exceptions & FE_INEXACT) != 0)));
    if (overflow) {
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

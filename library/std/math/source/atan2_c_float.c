#include "r_std_math.h"

#include "r_library_math_environment.h"

#include <fenv.h>
#include <math.h>
#include <stdint.h>

#pragma STDC FENV_ACCESS ON

RStdMathCFloatResult r_std_math_atan2_c_float(float left, float right) {
    RStdMathCFloatResult result = {0};
    RLibraryMathEnvironment environment = {0};
    RLibraryMathIndicators indicators;
    volatile float canonical_left;
    volatile float canonical_right;
    float computed;
    int classification;
    _Bool finite_operands;
    _Bool underflow;

    r_library_internal_math_environment_begin(&environment);
    canonical_left = left;
    canonical_right = right;
    computed = atan2f(canonical_left, canonical_right);
    classification = fpclassify(computed);
    finite_operands = isfinite(canonical_left) && isfinite(canonical_right);
    indicators = r_library_internal_math_environment_end(&environment);
    underflow =
        finite_operands && (canonical_left != 0.0F) &&
        (((classification == FP_ZERO) || ((classification == FP_SUBNORMAL) &&
                                          ((indicators.floating_exceptions & FE_INEXACT) != 0))));
    if (underflow) {
        result.status = R_STD_MATH_CALL_ERROR;
        result.error.code = R_STD_MATH_ERROR_UNDERFLOW;
        result.error.native_code = (int64_t)indicators.native_errno;
    } else {
        result.status = R_STD_MATH_CALL_SUCCESS;
        result.value = computed;
    }
    return result;
}

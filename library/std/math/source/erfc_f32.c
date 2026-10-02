#include "r_std_math.h"

#include "r_library_math_environment.h"

#include <fenv.h>
#include <math.h>
#include <stdint.h>

#pragma STDC FENV_ACCESS ON

RStdMathF32Result r_std_math_erfc_f32(float value) {
    RStdMathF32Result result = {0};
    RLibraryMathEnvironment environment = {0};
    RLibraryMathIndicators indicators;
    volatile float canonical_operand;
    float computed;
    int classification;
    _Bool finite_operand;
    _Bool underflow;

    r_library_internal_math_environment_begin(&environment);
    canonical_operand = value;
    computed = erfcf(canonical_operand);
    classification = fpclassify(computed);
    finite_operand = isfinite(canonical_operand) != 0;
    indicators = r_library_internal_math_environment_end(&environment);
    underflow = finite_operand && ((classification == FP_ZERO) ||
                                   ((classification == FP_SUBNORMAL) &&
                                    ((indicators.floating_exceptions & FE_INEXACT) != 0)));
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

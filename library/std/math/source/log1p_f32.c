#include "r_std_math.h"

#include "r_library_math_environment.h"

#include <math.h>
#include <stdint.h>

#pragma STDC FENV_ACCESS ON

RStdMathF32Result r_std_math_log1p_f32(float value) {
    RStdMathF32Result result = {0};
    RLibraryMathEnvironment environment = {0};
    RLibraryMathIndicators indicators;
    volatile float canonical_operand;
    float computed;
    int classification;
    _Bool finite_operand;
    _Bool nonzero_operand;
    _Bool domain;
    _Bool pole;
    _Bool underflow;

    r_library_internal_math_environment_begin(&environment);
    canonical_operand = value;
    computed = log1pf(canonical_operand);
    classification = fpclassify(computed);
    finite_operand = isfinite(canonical_operand) != 0;
    nonzero_operand = canonical_operand != 0.0F;
    domain = canonical_operand < -1.0F;
    pole = canonical_operand == -1.0F;
    indicators = r_library_internal_math_environment_end(&environment);
    underflow = finite_operand && !domain && !pole && nonzero_operand &&
                ((classification == FP_ZERO) || (classification == FP_SUBNORMAL));
    if (domain) {
        result.status = R_STD_MATH_CALL_ERROR;
        result.error.code = R_STD_MATH_ERROR_DOMAIN;
        result.error.native_code = (int64_t)indicators.native_errno;
    } else if (pole) {
        result.status = R_STD_MATH_CALL_ERROR;
        result.error.code = R_STD_MATH_ERROR_POLE;
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

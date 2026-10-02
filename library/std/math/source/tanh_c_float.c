#include "r_std_math.h"

#include "r_library_math_environment.h"

#include <fenv.h>
#include <math.h>
#include <stdint.h>

#pragma STDC FENV_ACCESS ON

RStdMathCFloatResult r_std_math_tanh_c_float(float value) {
    RStdMathCFloatResult result = {0};
    RLibraryMathEnvironment environment = {0};
    RLibraryMathIndicators indicators;
    volatile float canonical_operand;
    float computed;
    int classification;
    _Bool nonzero_operand;
    _Bool underflow;

    r_library_internal_math_environment_begin(&environment);
    canonical_operand = value;
    computed = tanhf(canonical_operand);
    classification = fpclassify(computed);
    nonzero_operand = canonical_operand != 0.0F;
    indicators = r_library_internal_math_environment_end(&environment);
    underflow =
        (classification == FP_SUBNORMAL) || ((classification == FP_ZERO) && nonzero_operand);
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

#include "r_std_math.h"

#include "r_library_math_environment.h"

#include <math.h>
#include <stdint.h>

#pragma STDC FENV_ACCESS ON

RStdMathCLongDoubleResult r_std_math_exp_c_long_double(long double value) {
    RStdMathCLongDoubleResult result = {0};
    RLibraryMathEnvironment environment = {0};
    RLibraryMathIndicators indicators;
    volatile long double canonical_operand;
    long double computed;
    int classification;
    _Bool finite_operand;
    _Bool infinite_result;
    _Bool underflow;

    r_library_internal_math_environment_begin(&environment);
    canonical_operand = value;
    computed = expl(canonical_operand);
    classification = fpclassify(computed);
    finite_operand = isfinite(canonical_operand) != 0;
    infinite_result = isinf(computed) != 0;
    indicators = r_library_internal_math_environment_end(&environment);
    underflow = finite_operand && ((classification == FP_ZERO) || (classification == FP_SUBNORMAL));
    if (finite_operand && infinite_result) {
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

#include "r_std_math.h"

#include "r_library_math_environment.h"

#include <fenv.h>
#include <math.h>
#include <stdint.h>

#pragma STDC FENV_ACCESS ON

RStdMathCLongDoubleResult r_std_math_cos_c_long_double(long double value) {
    RStdMathCLongDoubleResult result = {0};
    RLibraryMathEnvironment environment = {0};
    RLibraryMathIndicators indicators;
    volatile long double canonical_operand;
    long double computed;
    int classification;
    _Bool underflow;

    r_library_internal_math_environment_begin(&environment);
    canonical_operand = value;
    if (isinf(canonical_operand)) {
        (void)r_library_internal_math_environment_end(&environment);
        result.status = R_STD_MATH_CALL_ERROR;
        result.error.code = R_STD_MATH_ERROR_DOMAIN;
        result.error.native_code = INT64_C(0);
        return result;
    }

    computed = cosl(canonical_operand);
    classification = fpclassify(computed);
    indicators = r_library_internal_math_environment_end(&environment);
    underflow =
        (classification == FP_ZERO) ||
        ((classification == FP_SUBNORMAL) && ((indicators.floating_exceptions & FE_INEXACT) != 0));
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

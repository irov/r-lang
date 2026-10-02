#include "r_std_math.h"

#include "r_library_math_environment.h"

#include <fenv.h>
#include <math.h>
#include <stdint.h>

#pragma STDC FENV_ACCESS ON

RStdMathF64Result r_std_math_atan_f64(double value) {
    RStdMathF64Result result = {0};
    RLibraryMathEnvironment environment = {0};
    RLibraryMathIndicators indicators;
    volatile double canonical_operand;
    double computed;
    int classification;

    r_library_internal_math_environment_begin(&environment);
    canonical_operand = value;
    computed = atan(canonical_operand);
    classification = fpclassify(computed);
    indicators = r_library_internal_math_environment_end(&environment);
    if ((classification == FP_SUBNORMAL) && ((indicators.floating_exceptions & FE_INEXACT) != 0)) {
        result.status = R_STD_MATH_CALL_ERROR;
        result.error.code = R_STD_MATH_ERROR_UNDERFLOW;
        result.error.native_code = (int64_t)indicators.native_errno;
    } else {
        result.status = R_STD_MATH_CALL_SUCCESS;
        result.value = computed;
    }
    return result;
}

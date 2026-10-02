#include "r_std_math.h"

#include "r_library_math_environment.h"

#include <math.h>
#include <stdint.h>

#pragma STDC FENV_ACCESS ON

RStdMathCFloatResult r_std_math_log_c_float(float value) {
    RStdMathCFloatResult result = {0};
    RLibraryMathEnvironment environment = {0};
    RLibraryMathIndicators indicators;
    volatile float canonical_operand;
    float computed;
    _Bool negative;
    _Bool pole;

    r_library_internal_math_environment_begin(&environment);
    canonical_operand = value;
    computed = logf(canonical_operand);
    negative = canonical_operand < 0.0F;
    pole = canonical_operand == 0.0F;
    indicators = r_library_internal_math_environment_end(&environment);
    if (negative) {
        result.status = R_STD_MATH_CALL_ERROR;
        result.error.code = R_STD_MATH_ERROR_DOMAIN;
        result.error.native_code = (int64_t)indicators.native_errno;
    } else if (pole) {
        result.status = R_STD_MATH_CALL_ERROR;
        result.error.code = R_STD_MATH_ERROR_POLE;
        result.error.native_code = (int64_t)indicators.native_errno;
    } else {
        result.status = R_STD_MATH_CALL_SUCCESS;
        result.value = computed;
    }
    return result;
}

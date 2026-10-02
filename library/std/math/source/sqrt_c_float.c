#include "r_std_math.h"

#include "r_library_math_environment.h"

#include <math.h>
#include <stdint.h>

#pragma STDC FENV_ACCESS ON

RStdMathCFloatResult r_std_math_sqrt_c_float(float value) {
    RStdMathCFloatResult result = {0};
    RLibraryMathEnvironment environment = {0};
    volatile float canonical_operand;

    r_library_internal_math_environment_begin(&environment);
    canonical_operand = value;
    if (!isnan(canonical_operand) && signbit(canonical_operand) && (canonical_operand != 0.0F)) {
        (void)r_library_internal_math_environment_end(&environment);
        result.status = R_STD_MATH_CALL_ERROR;
        result.error.code = R_STD_MATH_ERROR_DOMAIN;
        result.error.native_code = INT64_C(0);
        return result;
    }

    result.value = sqrtf(canonical_operand);
    (void)r_library_internal_math_environment_end(&environment);
    result.status = R_STD_MATH_CALL_SUCCESS;
    return result;
}

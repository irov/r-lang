#include "r_std_math.h"

#include "r_library_math_environment.h"

#include <math.h>
#include <stdint.h>

#pragma STDC FENV_ACCESS ON

RStdMathCLongDoubleResult r_std_math_acos_c_long_double(long double value) {
    RStdMathCLongDoubleResult result = {0};
    RLibraryMathEnvironment environment = {0};
    volatile long double canonical_operand;

    r_library_internal_math_environment_begin(&environment);
    canonical_operand = value;
    if (!isnan(canonical_operand) && (fabsl(canonical_operand) > 1.0L)) {
        (void)r_library_internal_math_environment_end(&environment);
        result.status = R_STD_MATH_CALL_ERROR;
        result.error.code = R_STD_MATH_ERROR_DOMAIN;
        result.error.native_code = INT64_C(0);
        return result;
    }

    result.value = acosl(canonical_operand);
    (void)r_library_internal_math_environment_end(&environment);
    result.status = R_STD_MATH_CALL_SUCCESS;
    return result;
}

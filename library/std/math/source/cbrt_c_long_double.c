#include "r_std_math.h"

#include "r_library_math_environment.h"

#include <math.h>

#pragma STDC FENV_ACCESS ON

RStdMathCLongDoubleResult r_std_math_cbrt_c_long_double(long double value) {
    RStdMathCLongDoubleResult result = {0};
    RLibraryMathEnvironment environment = {0};
    volatile long double canonical_operand;

    r_library_internal_math_environment_begin(&environment);
    canonical_operand = value;
    result.value = cbrtl(canonical_operand);
    (void)r_library_internal_math_environment_end(&environment);
    result.status = R_STD_MATH_CALL_SUCCESS;
    return result;
}

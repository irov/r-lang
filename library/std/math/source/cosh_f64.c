#include "r_std_math.h"

#include "r_library_math_environment.h"

#include <math.h>
#include <stdint.h>

#pragma STDC FENV_ACCESS ON

RStdMathF64Result r_std_math_cosh_f64(double value) {
    RStdMathF64Result result = {0};
    RLibraryMathEnvironment environment = {0};
    RLibraryMathIndicators indicators;
    volatile double canonical_operand;
    double computed;
    _Bool finite_operand;
    _Bool infinite_result;

    r_library_internal_math_environment_begin(&environment);
    canonical_operand = value;
    computed = cosh(canonical_operand);
    finite_operand = isfinite(canonical_operand) != 0;
    infinite_result = isinf(computed) != 0;
    indicators = r_library_internal_math_environment_end(&environment);
    if (finite_operand && infinite_result) {
        result.status = R_STD_MATH_CALL_ERROR;
        result.error.code = R_STD_MATH_ERROR_OVERFLOW;
        result.error.native_code = (int64_t)indicators.native_errno;
    } else {
        result.status = R_STD_MATH_CALL_SUCCESS;
        result.value = computed;
    }
    return result;
}

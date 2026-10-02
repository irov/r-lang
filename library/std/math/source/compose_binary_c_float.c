#include "r_std_math.h"

#include "r_library_math_binary_scale.h"
#include "r_library_math_environment.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#pragma STDC FENV_ACCESS ON

RStdMathCFloatResult r_std_math_compose_binary_c_float(float fraction, int32_t exponent) {
    RStdMathCFloatResult result = {0};
    RLibraryMathEnvironment environment = {0};
    RLibraryMathIndicators indicators;
    volatile float canonical_fraction;
    float computed;
    _Bool overflow;
    _Bool underflow;

    r_library_internal_math_environment_begin(&environment);
    canonical_fraction = fraction;
    if (!isfinite(canonical_fraction) || (canonical_fraction == 0.0F)) {
        (void)memcpy(&result.value, &fraction, sizeof(result.value));
        (void)r_library_internal_math_environment_end(&environment);
        result.status = R_STD_MATH_CALL_SUCCESS;
        return result;
    }

    computed = scalbnf(canonical_fraction, exponent);
    overflow = isinf(computed) != 0;
    underflow =
        !overflow && r_library_internal_math_f32_scale_underflows(fraction, exponent, computed);
    indicators = r_library_internal_math_environment_end(&environment);
    if (overflow) {
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

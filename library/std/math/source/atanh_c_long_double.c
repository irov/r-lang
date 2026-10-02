#include "r_std_math.h"

#include "r_library_math_environment.h"

#include <math.h>
#include <stdint.h>

#pragma STDC FENV_ACCESS ON

RStdMathCLongDoubleResult r_std_math_atanh_c_long_double(long double value) {
    RStdMathCLongDoubleResult result = {0};
    RLibraryMathEnvironment environment = {0};
    RLibraryMathIndicators indicators;
    volatile long double canonical_operand;
    long double computed;
    long double magnitude;
    int classification;

    r_library_internal_math_environment_begin(&environment);
    canonical_operand = value;
    magnitude = fabsl(canonical_operand);
    if (!isnan(canonical_operand) && (magnitude > 1.0L)) {
        (void)r_library_internal_math_environment_end(&environment);
        result.status = R_STD_MATH_CALL_ERROR;
        result.error.code = R_STD_MATH_ERROR_DOMAIN;
        result.error.native_code = INT64_C(0);
        return result;
    }
    if (magnitude == 1.0L) {
        (void)r_library_internal_math_environment_end(&environment);
        result.status = R_STD_MATH_CALL_ERROR;
        result.error.code = R_STD_MATH_ERROR_POLE;
        result.error.native_code = INT64_C(0);
        return result;
    }

    computed = atanhl(canonical_operand);
    classification = fpclassify(computed);
    indicators = r_library_internal_math_environment_end(&environment);
    if ((classification == FP_SUBNORMAL) || ((computed == 0.0L) && (canonical_operand != 0.0L))) {
        result.status = R_STD_MATH_CALL_ERROR;
        result.error.code = R_STD_MATH_ERROR_UNDERFLOW;
        result.error.native_code = (int64_t)indicators.native_errno;
    } else {
        result.status = R_STD_MATH_CALL_SUCCESS;
        result.value = computed;
    }
    return result;
}

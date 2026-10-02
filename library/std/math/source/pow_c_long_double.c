#include "r_std_math.h"

#include "r_library_math_environment.h"

#include <fenv.h>
#include <math.h>
#include <stdint.h>

#pragma STDC FENV_ACCESS ON

RStdMathCLongDoubleResult r_std_math_pow_c_long_double(long double base, long double exponent) {
    RStdMathCLongDoubleResult result = {0};
    RLibraryMathEnvironment environment = {0};
    RLibraryMathIndicators indicators;
    volatile long double canonical_base;
    volatile long double canonical_exponent;
    long double computed;
    int classification;
    _Bool finite_base;
    _Bool finite_exponent;
    _Bool negative_base;
    _Bool integral_exponent;
    _Bool domain;
    _Bool pole;
    _Bool overflow;
    _Bool underflow;

    r_library_internal_math_environment_begin(&environment);
    canonical_base = base;
    canonical_exponent = exponent;
    if ((canonical_exponent == 0.0L) || (canonical_base == 1.0L)) {
        (void)r_library_internal_math_environment_end(&environment);
        result.status = R_STD_MATH_CALL_SUCCESS;
        result.value = 1.0L;
        return result;
    }
    finite_base = isfinite(canonical_base) != 0;
    finite_exponent = isfinite(canonical_exponent) != 0;
    negative_base = finite_base && (canonical_base < 0.0L);
    integral_exponent = finite_exponent && (truncl(canonical_exponent) == canonical_exponent);
    domain = negative_base && finite_exponent && !integral_exponent;
    pole = (canonical_base == 0.0L) && (canonical_exponent < 0.0L);
    computed = powl(canonical_base, canonical_exponent);
    classification = fpclassify(computed);
    indicators = r_library_internal_math_environment_end(&environment);
    overflow =
        finite_base && finite_exponent && !domain && !pole && (classification == FP_INFINITE);
    underflow =
        finite_base && finite_exponent && (canonical_base != 0.0L) && !domain && !pole &&
        ((classification == FP_ZERO) || ((classification == FP_SUBNORMAL) &&
                                         ((indicators.floating_exceptions & FE_INEXACT) != 0)));
    if (domain) {
        result.status = R_STD_MATH_CALL_ERROR;
        result.error.code = R_STD_MATH_ERROR_DOMAIN;
        result.error.native_code = (int64_t)indicators.native_errno;
    } else if (pole) {
        result.status = R_STD_MATH_CALL_ERROR;
        result.error.code = R_STD_MATH_ERROR_POLE;
        result.error.native_code = (int64_t)indicators.native_errno;
    } else if (overflow) {
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

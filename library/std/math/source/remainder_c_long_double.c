#include "r_std_math.h"

#include "r_library_math_environment.h"

#include <math.h>
#include <stdint.h>

#pragma STDC FENV_ACCESS ON

RStdMathCLongDoubleResult r_std_math_remainder_c_long_double(long double left, long double right) {
    RStdMathCLongDoubleResult result = {0};
    RLibraryMathEnvironment environment = {0};
    RLibraryMathIndicators indicators;
    volatile long double canonical_left;
    volatile long double canonical_right;
    long double computed;
    _Bool zero_divisor;
    _Bool infinite_dividend;

    r_library_internal_math_environment_begin(&environment);
    canonical_left = left;
    canonical_right = right;
    computed = remainderl(canonical_left, canonical_right);
    zero_divisor = canonical_right == 0.0L;
    infinite_dividend = isinf(canonical_left) != 0;
    indicators = r_library_internal_math_environment_end(&environment);
    if (zero_divisor || infinite_dividend) {
        result.status = R_STD_MATH_CALL_ERROR;
        result.error.code = R_STD_MATH_ERROR_DOMAIN;
        result.error.native_code = (int64_t)indicators.native_errno;
    } else {
        result.status = R_STD_MATH_CALL_SUCCESS;
        result.value = computed;
    }
    return result;
}

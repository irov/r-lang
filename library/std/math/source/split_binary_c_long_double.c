#include "r_std_math.h"

#include "r_library_math_environment.h"

#include <math.h>
#include <string.h>

#pragma STDC FENV_ACCESS ON

RStdMathBinaryPartsCLongDouble r_std_math_split_binary_c_long_double(long double value) {
    RStdMathBinaryPartsCLongDouble result = {0};
    RLibraryMathEnvironment environment = {0};
    volatile long double canonical_value;

    r_library_internal_math_environment_begin(&environment);
    canonical_value = value;
    if (!isfinite(canonical_value) || (canonical_value == 0.0L)) {
        (void)memcpy(&result.fraction, &value, sizeof(result.fraction));
    } else {
        result.fraction = frexpl(canonical_value, &result.exponent);
    }
    (void)r_library_internal_math_environment_end(&environment);
    return result;
}

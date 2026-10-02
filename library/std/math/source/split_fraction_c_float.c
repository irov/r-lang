#include "r_std_math.h"

#include "r_library_math_environment.h"

#include <math.h>

#pragma STDC FENV_ACCESS ON

RStdMathFractionPartsCFloat r_std_math_split_fraction_c_float(float value) {
    RStdMathFractionPartsCFloat result = {0};
    RLibraryMathEnvironment environment = {0};
    volatile float canonical_value;

    r_library_internal_math_environment_begin(&environment);
    canonical_value = value;
    result.fraction = modff(canonical_value, &result.whole);
    (void)r_library_internal_math_environment_end(&environment);
    return result;
}

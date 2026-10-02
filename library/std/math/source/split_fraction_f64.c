#include "r_std_math.h"

#include "r_library_math_environment.h"

#include <math.h>

#pragma STDC FENV_ACCESS ON

RStdMathFractionPartsF64 r_std_math_split_fraction_f64(double value) {
    RStdMathFractionPartsF64 result = {0};
    RLibraryMathEnvironment environment = {0};
    volatile double canonical_value;

    r_library_internal_math_environment_begin(&environment);
    canonical_value = value;
    result.fraction = modf(canonical_value, &result.whole);
    (void)r_library_internal_math_environment_end(&environment);
    return result;
}

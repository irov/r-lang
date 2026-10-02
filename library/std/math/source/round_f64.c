#include "r_std_math.h"

#include "r_library_math_environment.h"

#include <math.h>

#pragma STDC FENV_ACCESS ON

double r_std_math_round_f64(double value) {
    RLibraryMathEnvironment environment = {0};
    volatile double canonical_value;
    double result;

    r_library_internal_math_environment_begin(&environment);
    canonical_value = value;
    result = round(canonical_value);
    (void)r_library_internal_math_environment_end(&environment);
    return result;
}

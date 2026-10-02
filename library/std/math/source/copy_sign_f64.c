#include "r_std_math.h"

#include "r_library_math_environment.h"

#include <math.h>

#pragma STDC FENV_ACCESS ON

double r_std_math_copy_sign_f64(double magnitude, double sign) {
    RLibraryMathEnvironment environment = {0};
    volatile double canonical_magnitude;
    volatile double canonical_sign;
    double result;

    r_library_internal_math_environment_begin(&environment);
    canonical_magnitude = magnitude;
    canonical_sign = sign;
    result = copysign(canonical_magnitude, canonical_sign);
    (void)r_library_internal_math_environment_end(&environment);
    return result;
}

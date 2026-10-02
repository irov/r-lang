#include "r_std_math.h"

#include "r_library_math_environment.h"

#include <math.h>

#pragma STDC FENV_ACCESS ON

long double r_std_math_copy_sign_c_long_double(long double magnitude, long double sign) {
    RLibraryMathEnvironment environment = {0};
    volatile long double canonical_magnitude;
    volatile long double canonical_sign;
    long double result;

    r_library_internal_math_environment_begin(&environment);
    canonical_magnitude = magnitude;
    canonical_sign = sign;
    result = copysignl(canonical_magnitude, canonical_sign);
    (void)r_library_internal_math_environment_end(&environment);
    return result;
}

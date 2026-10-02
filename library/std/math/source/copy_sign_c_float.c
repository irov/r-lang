#include "r_std_math.h"

#include "r_library_math_environment.h"

#include <math.h>

#pragma STDC FENV_ACCESS ON

float r_std_math_copy_sign_c_float(float magnitude, float sign) {
    RLibraryMathEnvironment environment = {0};
    volatile float canonical_magnitude;
    volatile float canonical_sign;
    float result;

    r_library_internal_math_environment_begin(&environment);
    canonical_magnitude = magnitude;
    canonical_sign = sign;
    result = copysignf(canonical_magnitude, canonical_sign);
    (void)r_library_internal_math_environment_end(&environment);
    return result;
}

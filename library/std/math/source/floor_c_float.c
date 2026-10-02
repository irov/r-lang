#include "r_std_math.h"

#include "r_library_math_environment.h"

#include <math.h>

#pragma STDC FENV_ACCESS ON

float r_std_math_floor_c_float(float value) {
    RLibraryMathEnvironment environment = {0};
    volatile float canonical_value;
    float result;

    r_library_internal_math_environment_begin(&environment);
    canonical_value = value;
    result = floorf(canonical_value);
    (void)r_library_internal_math_environment_end(&environment);
    return result;
}

#include "r_std_math.h"

#include "r_library_math_environment.h"

#include <math.h>

#pragma STDC FENV_ACCESS ON

float r_std_math_next_after_c_float(float from, float toward) {
    RLibraryMathEnvironment environment = {0};
    volatile float canonical_from;
    volatile float canonical_toward;
    float result;

    r_library_internal_math_environment_begin(&environment);
    canonical_from = from;
    canonical_toward = toward;
    result = nextafterf(canonical_from, canonical_toward);
    (void)r_library_internal_math_environment_end(&environment);
    return result;
}

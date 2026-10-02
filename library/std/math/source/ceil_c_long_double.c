#include "r_std_math.h"

#include "r_library_math_environment.h"

#include <math.h>

#pragma STDC FENV_ACCESS ON

long double r_std_math_ceil_c_long_double(long double value) {
    RLibraryMathEnvironment environment = {0};
    volatile long double canonical_value;
    long double result;

    r_library_internal_math_environment_begin(&environment);
    canonical_value = value;
    result = ceill(canonical_value);
    (void)r_library_internal_math_environment_end(&environment);
    return result;
}

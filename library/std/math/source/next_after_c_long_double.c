#include "r_std_math.h"

#include "r_library_math_environment.h"

#include <math.h>

#pragma STDC FENV_ACCESS ON

long double r_std_math_next_after_c_long_double(long double from, long double toward) {
    RLibraryMathEnvironment environment = {0};
    volatile long double canonical_from;
    volatile long double canonical_toward;
    long double result;

    r_library_internal_math_environment_begin(&environment);
    canonical_from = from;
    canonical_toward = toward;
    result = nextafterl(canonical_from, canonical_toward);
    (void)r_library_internal_math_environment_end(&environment);
    return result;
}

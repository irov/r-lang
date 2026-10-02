#include "r_std_math.h"

#include "r_library_math_environment.h"

#include <math.h>

#pragma STDC FENV_ACCESS ON

double r_std_math_next_after_f64(double from, double toward) {
    RLibraryMathEnvironment environment = {0};
    volatile double canonical_from;
    volatile double canonical_toward;
    double result;

    r_library_internal_math_environment_begin(&environment);
    canonical_from = from;
    canonical_toward = toward;
    result = nextafter(canonical_from, canonical_toward);
    (void)r_library_internal_math_environment_end(&environment);
    return result;
}

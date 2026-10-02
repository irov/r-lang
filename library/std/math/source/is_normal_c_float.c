#include "r_std_math.h"

#include "r_library_math_environment.h"

#include <math.h>

#pragma STDC FENV_ACCESS ON

_Bool r_std_math_is_normal_c_float(float value) {
    RLibraryMathEnvironment environment = {0};
    volatile float canonical_value;
    _Bool result;

    r_library_internal_math_environment_begin(&environment);
    canonical_value = value;
    result = isnormal(canonical_value) != 0;
    (void)r_library_internal_math_environment_end(&environment);
    return result;
}

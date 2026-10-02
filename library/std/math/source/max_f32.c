#include "r_std_math.h"

#include "r_library_math_environment.h"

#include <math.h>

#pragma STDC FENV_ACCESS ON

float r_std_math_max_f32(float left, float right) {
    RLibraryMathEnvironment environment = {0};
    volatile float canonical_left;
    volatile float canonical_right;
    float result;
    _Bool left_is_nan;
    _Bool right_is_nan;

    r_library_internal_math_environment_begin(&environment);
    canonical_left = left;
    canonical_right = right;
    left_is_nan = isnan(canonical_left) != 0;
    right_is_nan = isnan(canonical_right) != 0;
    if (left_is_nan && right_is_nan) {
        result = canonical_left + canonical_right;
    } else if (left_is_nan) {
        result = canonical_right;
    } else if (right_is_nan) {
        result = canonical_left;
    } else if (canonical_left > canonical_right) {
        result = canonical_left;
    } else if (canonical_right > canonical_left) {
        result = canonical_right;
    } else if (canonical_left == 0.0F) {
        result = signbit(canonical_left) ? canonical_right : canonical_left;
    } else {
        result = canonical_left;
    }
    (void)r_library_internal_math_environment_end(&environment);
    return result;
}

#include "r_std_math.h"

#include "r_library_math_environment.h"

#include <math.h>
#include <string.h>

#pragma STDC FENV_ACCESS ON

RStdMathBinaryPartsF32 r_std_math_split_binary_f32(float value) {
    RStdMathBinaryPartsF32 result = {0};
    RLibraryMathEnvironment environment = {0};
    volatile float canonical_value;

    r_library_internal_math_environment_begin(&environment);
    canonical_value = value;
    if (!isfinite(canonical_value) || (canonical_value == 0.0F)) {
        (void)memcpy(&result.fraction, &value, sizeof(result.fraction));
    } else {
        result.fraction = frexpf(canonical_value, &result.exponent);
    }
    (void)r_library_internal_math_environment_end(&environment);
    return result;
}

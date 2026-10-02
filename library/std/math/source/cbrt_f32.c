#include "r_std_math.h"

#include "r_library_math_environment.h"

#include <math.h>

#pragma STDC FENV_ACCESS ON

RStdMathF32Result r_std_math_cbrt_f32(float value) {
    RStdMathF32Result result = {0};
    RLibraryMathEnvironment environment = {0};
    volatile float canonical_operand;

    r_library_internal_math_environment_begin(&environment);
    canonical_operand = value;
    result.value = cbrtf(canonical_operand);
    (void)r_library_internal_math_environment_end(&environment);
    result.status = R_STD_MATH_CALL_SUCCESS;
    return result;
}

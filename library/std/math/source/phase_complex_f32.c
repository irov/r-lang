#include "r_std_math.h"

#include "r_library_math_complex.h"

float r_std_math_phase_complex_f32(RStdMathComplexF32 value) {
    return r_library_internal_math_complex_phase_f32(value);
}

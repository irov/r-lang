#include "r_std_math.h"

#include "r_library_math_complex.h"

RStdMathF32Result r_std_math_magnitude_complex_f32(RStdMathComplexF32 value) {
    return r_library_internal_math_complex_magnitude_f32(value);
}

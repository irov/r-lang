#include "r_std_math.h"

#include "r_library_math_complex.h"

RStdMathComplexF32 r_std_math_conjugate_complex_f32(RStdMathComplexF32 value) {
    return r_library_internal_math_complex_conjugate_f32(value);
}

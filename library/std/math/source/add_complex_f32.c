#include "r_std_math.h"

#include "r_library_math_complex.h"

RStdMathComplexF32 r_std_math_add_complex_f32(RStdMathComplexF32 left, RStdMathComplexF32 right) {
    return r_library_internal_math_complex_arithmetic_f32(
        R_LIBRARY_MATH_COMPLEX_ARITHMETIC_ADD, left, right);
}

#include "r_std_math.h"

#include "r_library_math_complex.h"

RStdMathComplexF32Result r_std_math_div_complex_f32(RStdMathComplexF32 left,
                                                    RStdMathComplexF32 right) {
    return r_library_internal_math_complex_binary_f32(
        R_LIBRARY_MATH_COMPLEX_BINARY_DIV, left, right);
}

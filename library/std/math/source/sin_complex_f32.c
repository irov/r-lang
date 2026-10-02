#include "r_std_math.h"

#include "r_library_math_complex.h"

RStdMathComplexF32Result r_std_math_sin_complex_f32(RStdMathComplexF32 value) {
    return r_library_internal_math_complex_unary_f32(R_LIBRARY_MATH_COMPLEX_UNARY_SIN, value);
}

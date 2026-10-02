#include "r_std_math.h"

#include "r_library_math_complex.h"

RStdMathComplexF64 r_std_math_mul_complex_f64(RStdMathComplexF64 left, RStdMathComplexF64 right) {
    return r_library_internal_math_complex_arithmetic_f64(
        R_LIBRARY_MATH_COMPLEX_ARITHMETIC_MUL, left, right);
}

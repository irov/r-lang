#include "r_std_math.h"

#include "r_library_math_complex.h"

RStdMathComplexF64Result r_std_math_sinh_complex_f64(RStdMathComplexF64 value) {
    return r_library_internal_math_complex_unary_f64(R_LIBRARY_MATH_COMPLEX_UNARY_SINH, value);
}

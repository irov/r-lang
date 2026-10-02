#include "r_std_math.h"

#include "r_library_math_complex.h"

RStdMathComplexF64 r_std_math_conjugate_complex_f64(RStdMathComplexF64 value) {
    return r_library_internal_math_complex_conjugate_f64(value);
}

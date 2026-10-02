#include "r_std_math.h"

#include "r_library_math_complex.h"

double r_std_math_phase_complex_f64(RStdMathComplexF64 value) {
    return r_library_internal_math_complex_phase_f64(value);
}

#include "r_std_math.h"

#include "r_library_math_complex.h"

RStdMathF64Result r_std_math_magnitude_complex_f64(RStdMathComplexF64 value) {
    return r_library_internal_math_complex_magnitude_f64(value);
}

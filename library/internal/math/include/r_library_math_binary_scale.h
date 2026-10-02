#ifndef R_LIBRARY_MATH_BINARY_SCALE_H
#define R_LIBRARY_MATH_BINARY_SCALE_H

#include <stdint.h>

_Bool r_library_internal_math_f32_scale_underflows(float value, int32_t exponent, float result);
_Bool r_library_internal_math_f64_scale_underflows(double value, int32_t exponent, double result);
_Bool r_library_internal_math_c_long_double_scale_underflows(long double value,
                                                             int32_t exponent,
                                                             long double result);

#endif

#ifndef R_LIBRARY_MATH_COMPLEX_H
#define R_LIBRARY_MATH_COMPLEX_H

#include "r_std_math.h"

typedef enum RLibraryMathComplexArithmeticOperation {
    R_LIBRARY_MATH_COMPLEX_ARITHMETIC_ADD = 0,
    R_LIBRARY_MATH_COMPLEX_ARITHMETIC_SUB = 1,
    R_LIBRARY_MATH_COMPLEX_ARITHMETIC_MUL = 2
} RLibraryMathComplexArithmeticOperation;

typedef enum RLibraryMathComplexUnaryOperation {
    R_LIBRARY_MATH_COMPLEX_UNARY_EXP = 0,
    R_LIBRARY_MATH_COMPLEX_UNARY_LOG = 1,
    R_LIBRARY_MATH_COMPLEX_UNARY_SQRT = 2,
    R_LIBRARY_MATH_COMPLEX_UNARY_SIN = 3,
    R_LIBRARY_MATH_COMPLEX_UNARY_COS = 4,
    R_LIBRARY_MATH_COMPLEX_UNARY_TAN = 5,
    R_LIBRARY_MATH_COMPLEX_UNARY_SINH = 6,
    R_LIBRARY_MATH_COMPLEX_UNARY_COSH = 7,
    R_LIBRARY_MATH_COMPLEX_UNARY_TANH = 8
} RLibraryMathComplexUnaryOperation;

typedef enum RLibraryMathComplexBinaryOperation {
    R_LIBRARY_MATH_COMPLEX_BINARY_DIV = 0,
    R_LIBRARY_MATH_COMPLEX_BINARY_POW = 1
} RLibraryMathComplexBinaryOperation;

RStdMathComplexF32
r_library_internal_math_complex_arithmetic_f32(RLibraryMathComplexArithmeticOperation operation,
                                               RStdMathComplexF32 left,
                                               RStdMathComplexF32 right);
RStdMathComplexF64
r_library_internal_math_complex_arithmetic_f64(RLibraryMathComplexArithmeticOperation operation,
                                               RStdMathComplexF64 left,
                                               RStdMathComplexF64 right);
RStdMathComplexF32 r_library_internal_math_complex_conjugate_f32(RStdMathComplexF32 value);
RStdMathComplexF64 r_library_internal_math_complex_conjugate_f64(RStdMathComplexF64 value);
float r_library_internal_math_complex_phase_f32(RStdMathComplexF32 value);
double r_library_internal_math_complex_phase_f64(RStdMathComplexF64 value);
RStdMathF32Result r_library_internal_math_complex_magnitude_f32(RStdMathComplexF32 value);
RStdMathF64Result r_library_internal_math_complex_magnitude_f64(RStdMathComplexF64 value);
RStdMathComplexF32Result
r_library_internal_math_complex_unary_f32(RLibraryMathComplexUnaryOperation operation,
                                          RStdMathComplexF32 value);
RStdMathComplexF64Result
r_library_internal_math_complex_unary_f64(RLibraryMathComplexUnaryOperation operation,
                                          RStdMathComplexF64 value);
RStdMathComplexF32Result
r_library_internal_math_complex_binary_f32(RLibraryMathComplexBinaryOperation operation,
                                           RStdMathComplexF32 left,
                                           RStdMathComplexF32 right);
RStdMathComplexF64Result
r_library_internal_math_complex_binary_f64(RLibraryMathComplexBinaryOperation operation,
                                           RStdMathComplexF64 left,
                                           RStdMathComplexF64 right);

#endif

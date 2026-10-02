#include "r_library_math_complex.h"

#include "r_library_math_environment.h"

#include <complex.h>
#include <fenv.h>
#include <float.h>
#include <stdint.h>
#include <string.h>

#pragma STDC FENV_ACCESS ON

_Static_assert(FLT_RADIX == 2, "R Darwin complex math requires binary floating point");
_Static_assert(sizeof(float) == sizeof(uint32_t), "R complex_f32 requires binary32 storage");
_Static_assert(FLT_MANT_DIG == 24, "R complex_f32 requires binary32 precision");
_Static_assert(FLT_MAX_EXP == 128, "R complex_f32 requires binary32 exponent range");
_Static_assert(sizeof(double) == sizeof(uint64_t), "R complex_f64 requires binary64 storage");
_Static_assert(DBL_MANT_DIG == 53, "R complex_f64 requires binary64 precision");
_Static_assert(DBL_MAX_EXP == 1024, "R complex_f64 requires binary64 exponent range");

typedef enum RLibraryMathFloatingClass {
    R_LIBRARY_MATH_FLOATING_ZERO = 0,
    R_LIBRARY_MATH_FLOATING_SUBNORMAL = 1,
    R_LIBRARY_MATH_FLOATING_NORMAL = 2,
    R_LIBRARY_MATH_FLOATING_INFINITY = 3,
    R_LIBRARY_MATH_FLOATING_NAN = 4
} RLibraryMathFloatingClass;

static RLibraryMathFloatingClass r_library_internal_math_classify_f32(float value) {
    uint32_t bits;
    uint32_t exponent;
    uint32_t fraction;

    (void)memcpy(&bits, &value, sizeof(bits));
    exponent = bits & UINT32_C(0x7f800000);
    fraction = bits & UINT32_C(0x007fffff);
    if (exponent == UINT32_C(0)) {
        return fraction == UINT32_C(0) ? R_LIBRARY_MATH_FLOATING_ZERO
                                       : R_LIBRARY_MATH_FLOATING_SUBNORMAL;
    }
    if (exponent != UINT32_C(0x7f800000)) {
        return R_LIBRARY_MATH_FLOATING_NORMAL;
    }
    return fraction == UINT32_C(0) ? R_LIBRARY_MATH_FLOATING_INFINITY : R_LIBRARY_MATH_FLOATING_NAN;
}

static RLibraryMathFloatingClass r_library_internal_math_classify_f64(double value) {
    uint64_t bits;
    uint64_t exponent;
    uint64_t fraction;

    (void)memcpy(&bits, &value, sizeof(bits));
    exponent = bits & UINT64_C(0x7ff0000000000000);
    fraction = bits & UINT64_C(0x000fffffffffffff);
    if (exponent == UINT64_C(0)) {
        return fraction == UINT64_C(0) ? R_LIBRARY_MATH_FLOATING_ZERO
                                       : R_LIBRARY_MATH_FLOATING_SUBNORMAL;
    }
    if (exponent != UINT64_C(0x7ff0000000000000)) {
        return R_LIBRARY_MATH_FLOATING_NORMAL;
    }
    return fraction == UINT64_C(0) ? R_LIBRARY_MATH_FLOATING_INFINITY : R_LIBRARY_MATH_FLOATING_NAN;
}

static _Bool r_library_internal_math_complex_f32_is_zero(RStdMathComplexF32 value) {
    return (r_library_internal_math_classify_f32(value.real) == R_LIBRARY_MATH_FLOATING_ZERO) &&
           (r_library_internal_math_classify_f32(value.imag) == R_LIBRARY_MATH_FLOATING_ZERO);
}

static _Bool r_library_internal_math_complex_f64_is_zero(RStdMathComplexF64 value) {
    return (r_library_internal_math_classify_f64(value.real) == R_LIBRARY_MATH_FLOATING_ZERO) &&
           (r_library_internal_math_classify_f64(value.imag) == R_LIBRARY_MATH_FLOATING_ZERO);
}

static _Bool r_library_internal_math_complex_f32_is_finite(RStdMathComplexF32 value) {
    RLibraryMathFloatingClass real_class = r_library_internal_math_classify_f32(value.real);
    RLibraryMathFloatingClass imag_class = r_library_internal_math_classify_f32(value.imag);

    return (real_class <= R_LIBRARY_MATH_FLOATING_NORMAL) &&
           (imag_class <= R_LIBRARY_MATH_FLOATING_NORMAL);
}

static _Bool r_library_internal_math_complex_f64_is_finite(RStdMathComplexF64 value) {
    RLibraryMathFloatingClass real_class = r_library_internal_math_classify_f64(value.real);
    RLibraryMathFloatingClass imag_class = r_library_internal_math_classify_f64(value.imag);

    return (real_class <= R_LIBRARY_MATH_FLOATING_NORMAL) &&
           (imag_class <= R_LIBRARY_MATH_FLOATING_NORMAL);
}

static _Bool r_library_internal_math_complex_f32_is_nonzero(RStdMathComplexF32 value) {
    RLibraryMathFloatingClass real_class = r_library_internal_math_classify_f32(value.real);
    RLibraryMathFloatingClass imag_class = r_library_internal_math_classify_f32(value.imag);

    return ((real_class != R_LIBRARY_MATH_FLOATING_ZERO) &&
            (real_class != R_LIBRARY_MATH_FLOATING_NAN)) ||
           ((imag_class != R_LIBRARY_MATH_FLOATING_ZERO) &&
            (imag_class != R_LIBRARY_MATH_FLOATING_NAN));
}

static _Bool r_library_internal_math_complex_f64_is_nonzero(RStdMathComplexF64 value) {
    RLibraryMathFloatingClass real_class = r_library_internal_math_classify_f64(value.real);
    RLibraryMathFloatingClass imag_class = r_library_internal_math_classify_f64(value.imag);

    return ((real_class != R_LIBRARY_MATH_FLOATING_ZERO) &&
            (real_class != R_LIBRARY_MATH_FLOATING_NAN)) ||
           ((imag_class != R_LIBRARY_MATH_FLOATING_ZERO) &&
            (imag_class != R_LIBRARY_MATH_FLOATING_NAN));
}

static _Bool r_library_internal_math_complex_f32_has_infinity(RStdMathComplexF32 value) {
    return (r_library_internal_math_classify_f32(value.real) == R_LIBRARY_MATH_FLOATING_INFINITY) ||
           (r_library_internal_math_classify_f32(value.imag) == R_LIBRARY_MATH_FLOATING_INFINITY);
}

static _Bool r_library_internal_math_complex_f64_has_infinity(RStdMathComplexF64 value) {
    return (r_library_internal_math_classify_f64(value.real) == R_LIBRARY_MATH_FLOATING_INFINITY) ||
           (r_library_internal_math_classify_f64(value.imag) == R_LIBRARY_MATH_FLOATING_INFINITY);
}

static _Bool r_library_internal_math_complex_f32_has_nan(RStdMathComplexF32 value) {
    return (r_library_internal_math_classify_f32(value.real) == R_LIBRARY_MATH_FLOATING_NAN) ||
           (r_library_internal_math_classify_f32(value.imag) == R_LIBRARY_MATH_FLOATING_NAN);
}

static _Bool r_library_internal_math_complex_f64_has_nan(RStdMathComplexF64 value) {
    return (r_library_internal_math_classify_f64(value.real) == R_LIBRARY_MATH_FLOATING_NAN) ||
           (r_library_internal_math_classify_f64(value.imag) == R_LIBRARY_MATH_FLOATING_NAN);
}

static _Bool r_library_internal_math_complex_f32_has_subnormal(RStdMathComplexF32 value) {
    return (r_library_internal_math_classify_f32(value.real) ==
            R_LIBRARY_MATH_FLOATING_SUBNORMAL) ||
           (r_library_internal_math_classify_f32(value.imag) == R_LIBRARY_MATH_FLOATING_SUBNORMAL);
}

static _Bool r_library_internal_math_complex_f64_has_subnormal(RStdMathComplexF64 value) {
    return (r_library_internal_math_classify_f64(value.real) ==
            R_LIBRARY_MATH_FLOATING_SUBNORMAL) ||
           (r_library_internal_math_classify_f64(value.imag) == R_LIBRARY_MATH_FLOATING_SUBNORMAL);
}

static _Bool r_library_internal_math_complex_f32_has_zero(RStdMathComplexF32 value) {
    return (r_library_internal_math_classify_f32(value.real) == R_LIBRARY_MATH_FLOATING_ZERO) ||
           (r_library_internal_math_classify_f32(value.imag) == R_LIBRARY_MATH_FLOATING_ZERO);
}

static _Bool r_library_internal_math_complex_f64_has_zero(RStdMathComplexF64 value) {
    return (r_library_internal_math_classify_f64(value.real) == R_LIBRARY_MATH_FLOATING_ZERO) ||
           (r_library_internal_math_classify_f64(value.imag) == R_LIBRARY_MATH_FLOATING_ZERO);
}

static _Bool r_library_internal_math_f32_is_negative_integer(float value) {
    uint32_t bits;
    uint32_t exponent;
    uint32_t fraction_bits;
    int32_t unbiased_exponent;

    (void)memcpy(&bits, &value, sizeof(bits));
    if ((bits & UINT32_C(0x80000000)) == UINT32_C(0)) {
        return 0;
    }
    exponent = (bits >> UINT32_C(23)) & UINT32_C(0xff);
    fraction_bits = bits & UINT32_C(0x007fffff);
    if ((exponent == UINT32_C(0)) || (exponent == UINT32_C(0xff))) {
        return 0;
    }
    unbiased_exponent = (int32_t)exponent - INT32_C(127);
    if (unbiased_exponent < INT32_C(0)) {
        return 0;
    }
    if (unbiased_exponent >= INT32_C(23)) {
        return 1;
    }
    return (fraction_bits & ((UINT32_C(1) << (UINT32_C(23) - (uint32_t)unbiased_exponent)) -
                             UINT32_C(1))) == UINT32_C(0);
}

static _Bool r_library_internal_math_f64_is_negative_integer(double value) {
    uint64_t bits;
    uint64_t exponent;
    uint64_t fraction_bits;
    int32_t unbiased_exponent;

    (void)memcpy(&bits, &value, sizeof(bits));
    if ((bits & UINT64_C(0x8000000000000000)) == UINT64_C(0)) {
        return 0;
    }
    exponent = (bits >> UINT32_C(52)) & UINT64_C(0x7ff);
    fraction_bits = bits & UINT64_C(0x000fffffffffffff);
    if ((exponent == UINT64_C(0)) || (exponent == UINT64_C(0x7ff))) {
        return 0;
    }
    unbiased_exponent = (int32_t)exponent - INT32_C(1023);
    if (unbiased_exponent < INT32_C(0)) {
        return 0;
    }
    if (unbiased_exponent >= INT32_C(52)) {
        return 1;
    }
    return (fraction_bits & ((UINT64_C(1) << (UINT32_C(52) - (uint32_t)unbiased_exponent)) -
                             UINT64_C(1))) == UINT64_C(0);
}

static float complex r_library_internal_math_make_complex_f32(RStdMathComplexF32 value) {
    return CMPLXF(value.real, value.imag);
}

static double complex r_library_internal_math_make_complex_f64(RStdMathComplexF64 value) {
    return CMPLX(value.real, value.imag);
}

static RStdMathComplexF32 r_library_internal_math_extract_complex_f32(float complex value) {
    return (RStdMathComplexF32){crealf(value), cimagf(value)};
}

static RStdMathComplexF64 r_library_internal_math_extract_complex_f64(double complex value) {
    return (RStdMathComplexF64){creal(value), cimag(value)};
}

static _Bool r_library_internal_math_complex_f32_underflows(RStdMathComplexF32 result,
                                                            _Bool finite_inputs,
                                                            int floating_exceptions) {
    if (!finite_inputs || r_library_internal_math_complex_f32_has_nan(result) ||
        r_library_internal_math_complex_f32_has_infinity(result)) {
        return 0;
    }
    if (r_library_internal_math_complex_f32_has_subnormal(result)) {
        return (floating_exceptions & FE_INEXACT) != 0;
    }
    return r_library_internal_math_complex_f32_has_zero(result) &&
           ((floating_exceptions & FE_UNDERFLOW) != 0);
}

static _Bool r_library_internal_math_complex_f64_underflows(RStdMathComplexF64 result,
                                                            _Bool finite_inputs,
                                                            int floating_exceptions) {
    if (!finite_inputs || r_library_internal_math_complex_f64_has_nan(result) ||
        r_library_internal_math_complex_f64_has_infinity(result)) {
        return 0;
    }
    if (r_library_internal_math_complex_f64_has_subnormal(result)) {
        return (floating_exceptions & FE_INEXACT) != 0;
    }
    return r_library_internal_math_complex_f64_has_zero(result) &&
           ((floating_exceptions & FE_UNDERFLOW) != 0);
}

static RStdMathComplexF32Result
r_library_internal_math_complex_result_f32(RStdMathComplexF32 value,
                                           RStdMathErrorCode error_code,
                                           _Bool has_error,
                                           RLibraryMathIndicators indicators) {
    RStdMathComplexF32Result result = {0};

    if (has_error) {
        result.status = R_STD_MATH_CALL_ERROR;
        result.error.code = error_code;
        result.error.native_code = (int64_t)indicators.native_errno;
    } else {
        result.status = R_STD_MATH_CALL_SUCCESS;
        result.value = value;
    }
    return result;
}

static RStdMathComplexF64Result
r_library_internal_math_complex_result_f64(RStdMathComplexF64 value,
                                           RStdMathErrorCode error_code,
                                           _Bool has_error,
                                           RLibraryMathIndicators indicators) {
    RStdMathComplexF64Result result = {0};

    if (has_error) {
        result.status = R_STD_MATH_CALL_ERROR;
        result.error.code = error_code;
        result.error.native_code = (int64_t)indicators.native_errno;
    } else {
        result.status = R_STD_MATH_CALL_SUCCESS;
        result.value = value;
    }
    return result;
}

RStdMathComplexF32
r_library_internal_math_complex_arithmetic_f32(RLibraryMathComplexArithmeticOperation operation,
                                               RStdMathComplexF32 left,
                                               RStdMathComplexF32 right) {
    RLibraryMathEnvironment environment = {0};
    volatile float complex canonical_left;
    volatile float complex canonical_right;
    float complex computed = CMPLXF(0.0F, 0.0F);

    r_library_internal_math_environment_begin(&environment);
    canonical_left = r_library_internal_math_make_complex_f32(left);
    canonical_right = r_library_internal_math_make_complex_f32(right);
    switch (operation) {
    case R_LIBRARY_MATH_COMPLEX_ARITHMETIC_ADD:
        computed = canonical_left + canonical_right;
        break;
    case R_LIBRARY_MATH_COMPLEX_ARITHMETIC_SUB:
        computed = canonical_left - canonical_right;
        break;
    case R_LIBRARY_MATH_COMPLEX_ARITHMETIC_MUL:
        computed = canonical_left * canonical_right;
        break;
    }
    (void)r_library_internal_math_environment_end(&environment);
    return r_library_internal_math_extract_complex_f32(computed);
}

RStdMathComplexF64
r_library_internal_math_complex_arithmetic_f64(RLibraryMathComplexArithmeticOperation operation,
                                               RStdMathComplexF64 left,
                                               RStdMathComplexF64 right) {
    RLibraryMathEnvironment environment = {0};
    volatile double complex canonical_left;
    volatile double complex canonical_right;
    double complex computed = CMPLX(0.0, 0.0);

    r_library_internal_math_environment_begin(&environment);
    canonical_left = r_library_internal_math_make_complex_f64(left);
    canonical_right = r_library_internal_math_make_complex_f64(right);
    switch (operation) {
    case R_LIBRARY_MATH_COMPLEX_ARITHMETIC_ADD:
        computed = canonical_left + canonical_right;
        break;
    case R_LIBRARY_MATH_COMPLEX_ARITHMETIC_SUB:
        computed = canonical_left - canonical_right;
        break;
    case R_LIBRARY_MATH_COMPLEX_ARITHMETIC_MUL:
        computed = canonical_left * canonical_right;
        break;
    }
    (void)r_library_internal_math_environment_end(&environment);
    return r_library_internal_math_extract_complex_f64(computed);
}

RStdMathComplexF32 r_library_internal_math_complex_conjugate_f32(RStdMathComplexF32 value) {
    RLibraryMathEnvironment environment = {0};
    volatile float complex canonical_value;
    float complex computed;

    r_library_internal_math_environment_begin(&environment);
    canonical_value = r_library_internal_math_make_complex_f32(value);
    computed = conjf(canonical_value);
    (void)r_library_internal_math_environment_end(&environment);
    return r_library_internal_math_extract_complex_f32(computed);
}

RStdMathComplexF64 r_library_internal_math_complex_conjugate_f64(RStdMathComplexF64 value) {
    RLibraryMathEnvironment environment = {0};
    volatile double complex canonical_value;
    double complex computed;

    r_library_internal_math_environment_begin(&environment);
    canonical_value = r_library_internal_math_make_complex_f64(value);
    computed = conj(canonical_value);
    (void)r_library_internal_math_environment_end(&environment);
    return r_library_internal_math_extract_complex_f64(computed);
}

float r_library_internal_math_complex_phase_f32(RStdMathComplexF32 value) {
    RLibraryMathEnvironment environment = {0};
    volatile float complex canonical_value;
    float computed;

    r_library_internal_math_environment_begin(&environment);
    canonical_value = r_library_internal_math_make_complex_f32(value);
    computed = cargf(canonical_value);
    (void)r_library_internal_math_environment_end(&environment);
    return computed;
}

double r_library_internal_math_complex_phase_f64(RStdMathComplexF64 value) {
    RLibraryMathEnvironment environment = {0};
    volatile double complex canonical_value;
    double computed;

    r_library_internal_math_environment_begin(&environment);
    canonical_value = r_library_internal_math_make_complex_f64(value);
    computed = carg(canonical_value);
    (void)r_library_internal_math_environment_end(&environment);
    return computed;
}

RStdMathF32Result r_library_internal_math_complex_magnitude_f32(RStdMathComplexF32 value) {
    RStdMathF32Result result = {0};
    RLibraryMathEnvironment environment = {0};
    RLibraryMathIndicators indicators;
    volatile float complex canonical_value;
    float computed;
    RLibraryMathFloatingClass computed_class;
    _Bool finite_input;
    _Bool nonzero_input;

    r_library_internal_math_environment_begin(&environment);
    canonical_value = r_library_internal_math_make_complex_f32(value);
    computed = cabsf(canonical_value);
    computed_class = r_library_internal_math_classify_f32(computed);
    finite_input = r_library_internal_math_complex_f32_is_finite(value);
    nonzero_input = r_library_internal_math_complex_f32_is_nonzero(value);
    indicators = r_library_internal_math_environment_end(&environment);
    if (finite_input && (computed_class == R_LIBRARY_MATH_FLOATING_INFINITY)) {
        result.status = R_STD_MATH_CALL_ERROR;
        result.error.code = R_STD_MATH_ERROR_OVERFLOW;
        result.error.native_code = (int64_t)indicators.native_errno;
    } else if (finite_input && nonzero_input &&
               ((computed_class == R_LIBRARY_MATH_FLOATING_ZERO) ||
                ((computed_class == R_LIBRARY_MATH_FLOATING_SUBNORMAL) &&
                 ((indicators.floating_exceptions & FE_INEXACT) != 0)))) {
        result.status = R_STD_MATH_CALL_ERROR;
        result.error.code = R_STD_MATH_ERROR_UNDERFLOW;
        result.error.native_code = (int64_t)indicators.native_errno;
    } else {
        result.status = R_STD_MATH_CALL_SUCCESS;
        result.value = computed;
    }
    return result;
}

RStdMathF64Result r_library_internal_math_complex_magnitude_f64(RStdMathComplexF64 value) {
    RStdMathF64Result result = {0};
    RLibraryMathEnvironment environment = {0};
    RLibraryMathIndicators indicators;
    volatile double complex canonical_value;
    double computed;
    RLibraryMathFloatingClass computed_class;
    _Bool finite_input;
    _Bool nonzero_input;

    r_library_internal_math_environment_begin(&environment);
    canonical_value = r_library_internal_math_make_complex_f64(value);
    computed = cabs(canonical_value);
    computed_class = r_library_internal_math_classify_f64(computed);
    finite_input = r_library_internal_math_complex_f64_is_finite(value);
    nonzero_input = r_library_internal_math_complex_f64_is_nonzero(value);
    indicators = r_library_internal_math_environment_end(&environment);
    if (finite_input && (computed_class == R_LIBRARY_MATH_FLOATING_INFINITY)) {
        result.status = R_STD_MATH_CALL_ERROR;
        result.error.code = R_STD_MATH_ERROR_OVERFLOW;
        result.error.native_code = (int64_t)indicators.native_errno;
    } else if (finite_input && nonzero_input &&
               ((computed_class == R_LIBRARY_MATH_FLOATING_ZERO) ||
                ((computed_class == R_LIBRARY_MATH_FLOATING_SUBNORMAL) &&
                 ((indicators.floating_exceptions & FE_INEXACT) != 0)))) {
        result.status = R_STD_MATH_CALL_ERROR;
        result.error.code = R_STD_MATH_ERROR_UNDERFLOW;
        result.error.native_code = (int64_t)indicators.native_errno;
    } else {
        result.status = R_STD_MATH_CALL_SUCCESS;
        result.value = computed;
    }
    return result;
}

RStdMathComplexF32Result
r_library_internal_math_complex_unary_f32(RLibraryMathComplexUnaryOperation operation,
                                          RStdMathComplexF32 value) {
    RLibraryMathEnvironment environment = {0};
    RLibraryMathIndicators indicators;
    volatile float complex canonical_value;
    float complex computed = CMPLXF(0.0F, 0.0F);
    RStdMathComplexF32 extracted;
    _Bool finite_input;
    _Bool pole;
    _Bool overflow;
    _Bool underflow;

    r_library_internal_math_environment_begin(&environment);
    canonical_value = r_library_internal_math_make_complex_f32(value);
    switch (operation) {
    case R_LIBRARY_MATH_COMPLEX_UNARY_EXP:
        computed = cexpf(canonical_value);
        break;
    case R_LIBRARY_MATH_COMPLEX_UNARY_LOG:
        computed = clogf(canonical_value);
        break;
    case R_LIBRARY_MATH_COMPLEX_UNARY_SQRT:
        computed = csqrtf(canonical_value);
        break;
    case R_LIBRARY_MATH_COMPLEX_UNARY_SIN:
        computed = csinf(canonical_value);
        break;
    case R_LIBRARY_MATH_COMPLEX_UNARY_COS:
        computed = ccosf(canonical_value);
        break;
    case R_LIBRARY_MATH_COMPLEX_UNARY_TAN:
        computed = ctanf(canonical_value);
        break;
    case R_LIBRARY_MATH_COMPLEX_UNARY_SINH:
        computed = csinhf(canonical_value);
        break;
    case R_LIBRARY_MATH_COMPLEX_UNARY_COSH:
        computed = ccoshf(canonical_value);
        break;
    case R_LIBRARY_MATH_COMPLEX_UNARY_TANH:
        computed = ctanhf(canonical_value);
        break;
    }
    extracted = r_library_internal_math_extract_complex_f32(computed);
    finite_input = r_library_internal_math_complex_f32_is_finite(value);
    pole = (operation == R_LIBRARY_MATH_COMPLEX_UNARY_LOG) &&
           r_library_internal_math_complex_f32_is_zero(value);
    overflow = finite_input && r_library_internal_math_complex_f32_has_infinity(extracted);
    indicators = r_library_internal_math_environment_end(&environment);
    underflow = r_library_internal_math_complex_f32_underflows(
        extracted, finite_input, indicators.floating_exceptions);
    if (pole) {
        return r_library_internal_math_complex_result_f32(
            extracted, R_STD_MATH_ERROR_POLE, 1, indicators);
    }
    if (overflow) {
        return r_library_internal_math_complex_result_f32(
            extracted, R_STD_MATH_ERROR_OVERFLOW, 1, indicators);
    }
    return r_library_internal_math_complex_result_f32(
        extracted, R_STD_MATH_ERROR_UNDERFLOW, underflow, indicators);
}

RStdMathComplexF64Result
r_library_internal_math_complex_unary_f64(RLibraryMathComplexUnaryOperation operation,
                                          RStdMathComplexF64 value) {
    RLibraryMathEnvironment environment = {0};
    RLibraryMathIndicators indicators;
    volatile double complex canonical_value;
    double complex computed = CMPLX(0.0, 0.0);
    RStdMathComplexF64 extracted;
    _Bool finite_input;
    _Bool pole;
    _Bool overflow;
    _Bool underflow;

    r_library_internal_math_environment_begin(&environment);
    canonical_value = r_library_internal_math_make_complex_f64(value);
    switch (operation) {
    case R_LIBRARY_MATH_COMPLEX_UNARY_EXP:
        computed = cexp(canonical_value);
        break;
    case R_LIBRARY_MATH_COMPLEX_UNARY_LOG:
        computed = clog(canonical_value);
        break;
    case R_LIBRARY_MATH_COMPLEX_UNARY_SQRT:
        computed = csqrt(canonical_value);
        break;
    case R_LIBRARY_MATH_COMPLEX_UNARY_SIN:
        computed = csin(canonical_value);
        break;
    case R_LIBRARY_MATH_COMPLEX_UNARY_COS:
        computed = ccos(canonical_value);
        break;
    case R_LIBRARY_MATH_COMPLEX_UNARY_TAN:
        computed = ctan(canonical_value);
        break;
    case R_LIBRARY_MATH_COMPLEX_UNARY_SINH:
        computed = csinh(canonical_value);
        break;
    case R_LIBRARY_MATH_COMPLEX_UNARY_COSH:
        computed = ccosh(canonical_value);
        break;
    case R_LIBRARY_MATH_COMPLEX_UNARY_TANH:
        computed = ctanh(canonical_value);
        break;
    }
    extracted = r_library_internal_math_extract_complex_f64(computed);
    finite_input = r_library_internal_math_complex_f64_is_finite(value);
    pole = (operation == R_LIBRARY_MATH_COMPLEX_UNARY_LOG) &&
           r_library_internal_math_complex_f64_is_zero(value);
    overflow = finite_input && r_library_internal_math_complex_f64_has_infinity(extracted);
    indicators = r_library_internal_math_environment_end(&environment);
    underflow = r_library_internal_math_complex_f64_underflows(
        extracted, finite_input, indicators.floating_exceptions);
    if (pole) {
        return r_library_internal_math_complex_result_f64(
            extracted, R_STD_MATH_ERROR_POLE, 1, indicators);
    }
    if (overflow) {
        return r_library_internal_math_complex_result_f64(
            extracted, R_STD_MATH_ERROR_OVERFLOW, 1, indicators);
    }
    return r_library_internal_math_complex_result_f64(
        extracted, R_STD_MATH_ERROR_UNDERFLOW, underflow, indicators);
}

RStdMathComplexF32Result
r_library_internal_math_complex_binary_f32(RLibraryMathComplexBinaryOperation operation,
                                           RStdMathComplexF32 left,
                                           RStdMathComplexF32 right) {
    RLibraryMathEnvironment environment = {0};
    RLibraryMathIndicators indicators;
    volatile float complex canonical_left;
    volatile float complex canonical_right;
    float complex computed = CMPLXF(0.0F, 0.0F);
    RStdMathComplexF32 extracted;
    _Bool finite_inputs;
    _Bool domain;
    _Bool pole;
    _Bool zero_base_power;
    _Bool overflow;
    _Bool underflow;

    r_library_internal_math_environment_begin(&environment);
    canonical_left = r_library_internal_math_make_complex_f32(left);
    canonical_right = r_library_internal_math_make_complex_f32(right);
    switch (operation) {
    case R_LIBRARY_MATH_COMPLEX_BINARY_DIV:
        computed = canonical_left / canonical_right;
        break;
    case R_LIBRARY_MATH_COMPLEX_BINARY_POW:
        computed = cpowf(canonical_left, canonical_right);
        break;
    }
    extracted = r_library_internal_math_extract_complex_f32(computed);
    finite_inputs = r_library_internal_math_complex_f32_is_finite(left) &&
                    r_library_internal_math_complex_f32_is_finite(right);
    domain = (operation == R_LIBRARY_MATH_COMPLEX_BINARY_DIV) &&
             r_library_internal_math_complex_f32_is_zero(left) &&
             r_library_internal_math_complex_f32_is_zero(right);
    zero_base_power = (operation == R_LIBRARY_MATH_COMPLEX_BINARY_POW) &&
                      r_library_internal_math_complex_f32_is_zero(left);
    pole = ((operation == R_LIBRARY_MATH_COMPLEX_BINARY_DIV) &&
            r_library_internal_math_complex_f32_is_nonzero(left) &&
            r_library_internal_math_complex_f32_is_zero(right)) ||
           (zero_base_power &&
            r_library_internal_math_complex_f32_is_zero((RStdMathComplexF32){0.0F, right.imag}) &&
            r_library_internal_math_f32_is_negative_integer(right.real));
    overflow = finite_inputs && !zero_base_power &&
               r_library_internal_math_complex_f32_has_infinity(extracted);
    indicators = r_library_internal_math_environment_end(&environment);
    underflow = r_library_internal_math_complex_f32_underflows(
        extracted, finite_inputs && !zero_base_power, indicators.floating_exceptions);
    if (domain) {
        return r_library_internal_math_complex_result_f32(
            extracted, R_STD_MATH_ERROR_DOMAIN, 1, indicators);
    }
    if (pole) {
        return r_library_internal_math_complex_result_f32(
            extracted, R_STD_MATH_ERROR_POLE, 1, indicators);
    }
    if (overflow) {
        return r_library_internal_math_complex_result_f32(
            extracted, R_STD_MATH_ERROR_OVERFLOW, 1, indicators);
    }
    return r_library_internal_math_complex_result_f32(
        extracted, R_STD_MATH_ERROR_UNDERFLOW, underflow, indicators);
}

RStdMathComplexF64Result
r_library_internal_math_complex_binary_f64(RLibraryMathComplexBinaryOperation operation,
                                           RStdMathComplexF64 left,
                                           RStdMathComplexF64 right) {
    RLibraryMathEnvironment environment = {0};
    RLibraryMathIndicators indicators;
    volatile double complex canonical_left;
    volatile double complex canonical_right;
    double complex computed = CMPLX(0.0, 0.0);
    RStdMathComplexF64 extracted;
    _Bool finite_inputs;
    _Bool domain;
    _Bool pole;
    _Bool zero_base_power;
    _Bool overflow;
    _Bool underflow;

    r_library_internal_math_environment_begin(&environment);
    canonical_left = r_library_internal_math_make_complex_f64(left);
    canonical_right = r_library_internal_math_make_complex_f64(right);
    switch (operation) {
    case R_LIBRARY_MATH_COMPLEX_BINARY_DIV:
        computed = canonical_left / canonical_right;
        break;
    case R_LIBRARY_MATH_COMPLEX_BINARY_POW:
        computed = cpow(canonical_left, canonical_right);
        break;
    }
    extracted = r_library_internal_math_extract_complex_f64(computed);
    finite_inputs = r_library_internal_math_complex_f64_is_finite(left) &&
                    r_library_internal_math_complex_f64_is_finite(right);
    domain = (operation == R_LIBRARY_MATH_COMPLEX_BINARY_DIV) &&
             r_library_internal_math_complex_f64_is_zero(left) &&
             r_library_internal_math_complex_f64_is_zero(right);
    zero_base_power = (operation == R_LIBRARY_MATH_COMPLEX_BINARY_POW) &&
                      r_library_internal_math_complex_f64_is_zero(left);
    pole = ((operation == R_LIBRARY_MATH_COMPLEX_BINARY_DIV) &&
            r_library_internal_math_complex_f64_is_nonzero(left) &&
            r_library_internal_math_complex_f64_is_zero(right)) ||
           (zero_base_power &&
            r_library_internal_math_complex_f64_is_zero((RStdMathComplexF64){0.0, right.imag}) &&
            r_library_internal_math_f64_is_negative_integer(right.real));
    overflow = finite_inputs && !zero_base_power &&
               r_library_internal_math_complex_f64_has_infinity(extracted);
    indicators = r_library_internal_math_environment_end(&environment);
    underflow = r_library_internal_math_complex_f64_underflows(
        extracted, finite_inputs && !zero_base_power, indicators.floating_exceptions);
    if (domain) {
        return r_library_internal_math_complex_result_f64(
            extracted, R_STD_MATH_ERROR_DOMAIN, 1, indicators);
    }
    if (pole) {
        return r_library_internal_math_complex_result_f64(
            extracted, R_STD_MATH_ERROR_POLE, 1, indicators);
    }
    if (overflow) {
        return r_library_internal_math_complex_result_f64(
            extracted, R_STD_MATH_ERROR_OVERFLOW, 1, indicators);
    }
    return r_library_internal_math_complex_result_f64(
        extracted, R_STD_MATH_ERROR_UNDERFLOW, underflow, indicators);
}

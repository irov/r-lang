#include "r_std_math.h"

#include "r_runtime_allocator.h"

#include <errno.h>
#include <fenv.h>
#include <float.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#pragma STDC FENV_ACCESS ON

#define R_TEST_CHECK(expression)                                                                   \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            (void)fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, #expression); \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

typedef struct RTestMathFloatResult {
    RStdMathCallStatus status;
    float value;
    RStdMathError error;
} RTestMathFloatResult;

typedef struct RTestMathDoubleResult {
    RStdMathCallStatus status;
    double value;
    RStdMathError error;
} RTestMathDoubleResult;

typedef struct RTestMathLongDoubleResult {
    RStdMathCallStatus status;
    long double value;
    RStdMathError error;
} RTestMathLongDoubleResult;

typedef RTestMathFloatResult (*RTestMathFloatOperation)(float value);
typedef RTestMathDoubleResult (*RTestMathDoubleOperation)(double value);
typedef RTestMathLongDoubleResult (*RTestMathLongDoubleOperation)(long double value);

_Static_assert(LDBL_MANT_DIG == 53, "target long double uses a binary64 significand");
_Static_assert(LDBL_MAX_EXP == 1024, "target long double uses a binary64 exponent");
_Static_assert(sizeof(long double) == sizeof(uint64_t),
               "arm64-apple-darwin long double must use binary64 storage");

static float r_test_signaling_nan_f32_value(void) {
    uint32_t bits = UINT32_C(0x7f800001);
    float value;

    (void)memcpy(&value, &bits, sizeof(value));
    return value;
}

static double r_test_signaling_nan_f64_value(void) {
    uint64_t bits = UINT64_C(0x7ff0000000000001);
    double value;

    (void)memcpy(&value, &bits, sizeof(value));
    return value;
}

static long double r_test_signaling_nan_c_long_double_value(void) {
    uint64_t bits = UINT64_C(0x7ff0000000000001);
    long double value;

    (void)memcpy(&value, &bits, sizeof(value));
    return value;
}

static _Bool r_test_is_quiet_nan_f32_bits(float value) {
    uint32_t bits;

    (void)memcpy(&bits, &value, sizeof(bits));
    return (bits & UINT32_C(0x7f800000)) == UINT32_C(0x7f800000) &&
           (bits & UINT32_C(0x007fffff)) != UINT32_C(0) &&
           (bits & UINT32_C(0x00400000)) != UINT32_C(0);
}

static _Bool r_test_is_quiet_nan_f64_bits(double value) {
    uint64_t bits;

    (void)memcpy(&bits, &value, sizeof(bits));
    return (bits & UINT64_C(0x7ff0000000000000)) == UINT64_C(0x7ff0000000000000) &&
           (bits & UINT64_C(0x000fffffffffffff)) != UINT64_C(0) &&
           (bits & UINT64_C(0x0008000000000000)) != UINT64_C(0);
}

static _Bool r_test_is_quiet_nan_c_long_double_bits(long double value) {
    uint64_t bits;

    (void)memcpy(&bits, &value, sizeof(bits));
    return (bits & UINT64_C(0x7ff0000000000000)) == UINT64_C(0x7ff0000000000000) &&
           (bits & UINT64_C(0x000fffffffffffff)) != UINT64_C(0) &&
           (bits & UINT64_C(0x0008000000000000)) != UINT64_C(0);
}

static RTestMathFloatResult r_test_asinh_f32(float value) {
    RStdMathF32Result public_result = r_std_math_asinh_f32(value);
    RTestMathFloatResult result = {public_result.status, public_result.value, public_result.error};

    return result;
}

static RTestMathDoubleResult r_test_asinh_f64(double value) {
    RStdMathF64Result public_result = r_std_math_asinh_f64(value);
    RTestMathDoubleResult result = {public_result.status, public_result.value, public_result.error};

    return result;
}

static RTestMathFloatResult r_test_asinh_c_float(float value) {
    RStdMathCFloatResult public_result = r_std_math_asinh_c_float(value);
    RTestMathFloatResult result = {public_result.status, public_result.value, public_result.error};

    return result;
}

static RTestMathDoubleResult r_test_asinh_c_double(double value) {
    RStdMathCDoubleResult public_result = r_std_math_asinh_c_double(value);
    RTestMathDoubleResult result = {public_result.status, public_result.value, public_result.error};

    return result;
}

static RTestMathLongDoubleResult r_test_asinh_c_long_double(long double value) {
    RStdMathCLongDoubleResult public_result = r_std_math_asinh_c_long_double(value);
    RTestMathLongDoubleResult result = {
        public_result.status, public_result.value, public_result.error};

    return result;
}

static int r_test_seed_environment(int rounding, int exceptions, int native_errno) {
    R_TEST_CHECK(fesetround(rounding) == 0);
    R_TEST_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    R_TEST_CHECK(feraiseexcept(exceptions) == 0);
    errno = native_errno;
    return 0;
}

static _Bool r_test_environment_is(int rounding, int exceptions, int native_errno) {
    return fegetround() == rounding &&
           (fetestexcept(FE_ALL_EXCEPT) & FE_ALL_EXCEPT) == exceptions && errno == native_errno;
}

static int r_test_environment_matches(int rounding, int exceptions, int native_errno) {
    R_TEST_CHECK(r_test_environment_is(rounding, exceptions, native_errno));
    return 0;
}

static int r_test_native_asinh_f32(float value, float *result) {
    fenv_t caller_environment;
    fenv_t held_environment;
    int caller_errno = errno;
    volatile float canonical_operand;

    if (fegetenv(&caller_environment) != 0) {
        return 1;
    }
    if ((feholdexcept(&held_environment) != 0) || (fesetround(FE_TONEAREST) != 0)) {
        (void)fesetenv(&caller_environment);
        errno = caller_errno;
        return 1;
    }
    errno = 0;
    canonical_operand = value;
    *result = asinhf(canonical_operand);
    if (fesetenv(&caller_environment) != 0) {
        errno = caller_errno;
        return 1;
    }
    errno = caller_errno;
    return 0;
}

static int r_test_native_asinh_f64(double value, double *result) {
    fenv_t caller_environment;
    fenv_t held_environment;
    int caller_errno = errno;
    volatile double canonical_operand;

    if (fegetenv(&caller_environment) != 0) {
        return 1;
    }
    if ((feholdexcept(&held_environment) != 0) || (fesetround(FE_TONEAREST) != 0)) {
        (void)fesetenv(&caller_environment);
        errno = caller_errno;
        return 1;
    }
    errno = 0;
    canonical_operand = value;
    *result = asinh(canonical_operand);
    if (fesetenv(&caller_environment) != 0) {
        errno = caller_errno;
        return 1;
    }
    errno = caller_errno;
    return 0;
}

static int r_test_native_asinh_c_long_double(long double value, long double *result) {
    fenv_t caller_environment;
    fenv_t held_environment;
    int caller_errno = errno;
    volatile long double canonical_operand;

    if (fegetenv(&caller_environment) != 0) {
        return 1;
    }
    if ((feholdexcept(&held_environment) != 0) || (fesetround(FE_TONEAREST) != 0)) {
        (void)fesetenv(&caller_environment);
        errno = caller_errno;
        return 1;
    }
    errno = 0;
    canonical_operand = value;
    *result = asinhl(canonical_operand);
    if (fesetenv(&caller_environment) != 0) {
        errno = caller_errno;
        return 1;
    }
    errno = caller_errno;
    return 0;
}

static int r_test_float_underflow(RTestMathFloatOperation operation,
                                  float value,
                                  int rounding,
                                  int exceptions,
                                  int native_errno) {
    RTestMathFloatResult result = operation(value);

    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_UNDERFLOW);
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);
    return 0;
}

static int r_test_double_underflow(RTestMathDoubleOperation operation,
                                   double value,
                                   int rounding,
                                   int exceptions,
                                   int native_errno) {
    RTestMathDoubleResult result = operation(value);

    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_UNDERFLOW);
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);
    return 0;
}

static int r_test_long_double_underflow(RTestMathLongDoubleOperation operation,
                                        long double value,
                                        int rounding,
                                        int exceptions,
                                        int native_errno) {
    RTestMathLongDoubleResult result = operation(value);

    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_UNDERFLOW);
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);
    return 0;
}

static int r_test_float_family(RTestMathFloatOperation operation,
                               int rounding,
                               int exceptions,
                               int native_errno) {
    const float inputs[] = {-FLT_MAX, -1.0F, -0.5F, -FLT_MIN, FLT_MIN, 0.5F, 1.0F, FLT_MAX};
    const float maximum_subnormal = nextafterf(FLT_MIN, 0.0F);
    float references[8];
    float signaling_nan = r_test_signaling_nan_f32_value();
    size_t index;
    RTestMathFloatResult result;

    R_TEST_CHECK(r_test_seed_environment(rounding, exceptions, native_errno) == 0);
    for (index = 0U; index < 8U; index += 1U) {
        R_TEST_CHECK(r_test_native_asinh_f32(inputs[index], &references[index]) == 0);
        result = operation(inputs[index]);
        R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
        R_TEST_CHECK(result.value == references[index]);
        R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);
    }
    result = operation(0.0F);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS && result.value == 0.0F);
    R_TEST_CHECK(!signbit(result.value));
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);
    result = operation(-0.0F);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS && result.value == 0.0F);
    R_TEST_CHECK(signbit(result.value));
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(INFINITY);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS && isinf(result.value));
    R_TEST_CHECK(!signbit(result.value));
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);
    result = operation(-INFINITY);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS && isinf(result.value));
    R_TEST_CHECK(signbit(result.value));
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(NAN);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS && isnan(result.value));
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);
    R_TEST_CHECK(r_test_float_underflow(
                     operation, maximum_subnormal, rounding, exceptions, native_errno) == 0);
    R_TEST_CHECK(r_test_float_underflow(
                     operation, -maximum_subnormal, rounding, exceptions, native_errno) == 0);
    R_TEST_CHECK(
        r_test_float_underflow(operation, FLT_TRUE_MIN, rounding, exceptions, native_errno) == 0);
    R_TEST_CHECK(
        r_test_float_underflow(operation, -FLT_TRUE_MIN, rounding, exceptions, native_errno) == 0);

    R_TEST_CHECK(r_test_seed_environment(rounding, FE_DIVBYZERO, native_errno) == 0);
    result = operation(signaling_nan);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_is_quiet_nan_f32_bits(result.value));
    R_TEST_CHECK(r_test_environment_matches(rounding, FE_DIVBYZERO, native_errno) == 0);
    return 0;
}

static int r_test_double_family(RTestMathDoubleOperation operation,
                                int rounding,
                                int exceptions,
                                int native_errno) {
    const double inputs[] = {-DBL_MAX, -1.0, -0.5, -DBL_MIN, DBL_MIN, 0.5, 1.0, DBL_MAX};
    const double maximum_subnormal = nextafter(DBL_MIN, 0.0);
    double references[8];
    double signaling_nan = r_test_signaling_nan_f64_value();
    size_t index;
    RTestMathDoubleResult result;

    R_TEST_CHECK(r_test_seed_environment(rounding, exceptions, native_errno) == 0);
    for (index = 0U; index < 8U; index += 1U) {
        R_TEST_CHECK(r_test_native_asinh_f64(inputs[index], &references[index]) == 0);
        result = operation(inputs[index]);
        R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
        R_TEST_CHECK(result.value == references[index]);
        R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);
    }
    result = operation(0.0);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS && result.value == 0.0);
    R_TEST_CHECK(!signbit(result.value));
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);
    result = operation(-0.0);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS && result.value == 0.0);
    R_TEST_CHECK(signbit(result.value));
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(INFINITY);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS && isinf(result.value));
    R_TEST_CHECK(!signbit(result.value));
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);
    result = operation(-INFINITY);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS && isinf(result.value));
    R_TEST_CHECK(signbit(result.value));
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(NAN);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS && isnan(result.value));
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);
    R_TEST_CHECK(r_test_double_underflow(
                     operation, maximum_subnormal, rounding, exceptions, native_errno) == 0);
    R_TEST_CHECK(r_test_double_underflow(
                     operation, -maximum_subnormal, rounding, exceptions, native_errno) == 0);
    R_TEST_CHECK(
        r_test_double_underflow(operation, DBL_TRUE_MIN, rounding, exceptions, native_errno) == 0);
    R_TEST_CHECK(
        r_test_double_underflow(operation, -DBL_TRUE_MIN, rounding, exceptions, native_errno) == 0);

    R_TEST_CHECK(r_test_seed_environment(rounding, FE_DIVBYZERO, native_errno) == 0);
    result = operation(signaling_nan);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_is_quiet_nan_f64_bits(result.value));
    R_TEST_CHECK(r_test_environment_matches(rounding, FE_DIVBYZERO, native_errno) == 0);
    return 0;
}

static int r_test_long_double_family(RTestMathLongDoubleOperation operation,
                                     int rounding,
                                     int exceptions,
                                     int native_errno) {
    const long double inputs[] = {
        -LDBL_MAX, -1.0L, -0.5L, -LDBL_MIN, LDBL_MIN, 0.5L, 1.0L, LDBL_MAX};
    const long double maximum_subnormal = nextafterl(LDBL_MIN, 0.0L);
    long double references[8];
    long double signaling_nan = r_test_signaling_nan_c_long_double_value();
    size_t index;
    RTestMathLongDoubleResult result;

    R_TEST_CHECK(r_test_seed_environment(rounding, exceptions, native_errno) == 0);
    for (index = 0U; index < 8U; index += 1U) {
        R_TEST_CHECK(r_test_native_asinh_c_long_double(inputs[index], &references[index]) == 0);
        result = operation(inputs[index]);
        R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
        R_TEST_CHECK(result.value == references[index]);
        R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);
    }
    result = operation(0.0L);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS && result.value == 0.0L);
    R_TEST_CHECK(!signbit(result.value));
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);
    result = operation(-0.0L);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS && result.value == 0.0L);
    R_TEST_CHECK(signbit(result.value));
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(INFINITY);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS && isinf(result.value));
    R_TEST_CHECK(!signbit(result.value));
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);
    result = operation(-INFINITY);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS && isinf(result.value));
    R_TEST_CHECK(signbit(result.value));
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(NAN);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS && isnan(result.value));
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);
    R_TEST_CHECK(r_test_long_double_underflow(
                     operation, maximum_subnormal, rounding, exceptions, native_errno) == 0);
    R_TEST_CHECK(r_test_long_double_underflow(
                     operation, -maximum_subnormal, rounding, exceptions, native_errno) == 0);
    R_TEST_CHECK(r_test_long_double_underflow(
                     operation, LDBL_TRUE_MIN, rounding, exceptions, native_errno) == 0);
    R_TEST_CHECK(r_test_long_double_underflow(
                     operation, -LDBL_TRUE_MIN, rounding, exceptions, native_errno) == 0);

    R_TEST_CHECK(r_test_seed_environment(rounding, FE_DIVBYZERO, native_errno) == 0);
    result = operation(signaling_nan);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_is_quiet_nan_c_long_double_bits(result.value));
    R_TEST_CHECK(r_test_environment_matches(rounding, FE_DIVBYZERO, native_errno) == 0);
    return 0;
}

typedef struct RTestAsinhThreadContext {
    int rounding;
    int exceptions;
    int native_errno;
    float expected_float;
    double expected_double;
    long double expected_long_double;
    int failed;
} RTestAsinhThreadContext;

static void *r_test_asinh_thread_main(void *opaque_context) {
    RTestAsinhThreadContext *context = opaque_context;
    int iteration;

    if (r_test_seed_environment(context->rounding, context->exceptions, context->native_errno) !=
        0) {
        context->failed = 1;
        return NULL;
    }
    for (iteration = 0; iteration < 500; iteration += 1) {
        RStdMathF32Result f32 = r_std_math_asinh_f32(0.5F);
        RStdMathF64Result f64 = r_std_math_asinh_f64(0.5);
        RStdMathCFloatResult c_float = r_std_math_asinh_c_float(0.5F);
        RStdMathCDoubleResult c_double = r_std_math_asinh_c_double(0.5);
        RStdMathCLongDoubleResult c_long_double = r_std_math_asinh_c_long_double(0.5L);

        if (f32.status != R_STD_MATH_CALL_SUCCESS || f32.value != context->expected_float ||
            f64.status != R_STD_MATH_CALL_SUCCESS || f64.value != context->expected_double ||
            c_float.status != R_STD_MATH_CALL_SUCCESS || c_float.value != context->expected_float ||
            c_double.status != R_STD_MATH_CALL_SUCCESS ||
            c_double.value != context->expected_double ||
            c_long_double.status != R_STD_MATH_CALL_SUCCESS ||
            c_long_double.value != context->expected_long_double ||
            !r_test_environment_is(context->rounding, context->exceptions, context->native_errno)) {
            context->failed = 1;
            return NULL;
        }
    }
    return NULL;
}

static int r_test_concurrent_environments(void) {
    float expected_float;
    double expected_double;
    long double expected_long_double;
    pthread_t first_thread;
    pthread_t second_thread;
    RTestAsinhThreadContext first;
    RTestAsinhThreadContext second;

    R_TEST_CHECK(r_test_native_asinh_f32(0.5F, &expected_float) == 0);
    R_TEST_CHECK(r_test_native_asinh_f64(0.5, &expected_double) == 0);
    R_TEST_CHECK(r_test_native_asinh_c_long_double(0.5L, &expected_long_double) == 0);
    first = (RTestAsinhThreadContext){
        FE_DOWNWARD, FE_DIVBYZERO, EDOM, expected_float, expected_double, expected_long_double, 0};
    second = (RTestAsinhThreadContext){
        FE_UPWARD, FE_INVALID, ERANGE, expected_float, expected_double, expected_long_double, 0};

    R_TEST_CHECK(pthread_create(&first_thread, NULL, r_test_asinh_thread_main, &first) == 0);
    R_TEST_CHECK(pthread_create(&second_thread, NULL, r_test_asinh_thread_main, &second) == 0);
    R_TEST_CHECK(pthread_join(first_thread, NULL) == 0);
    R_TEST_CHECK(pthread_join(second_thread, NULL) == 0);
    R_TEST_CHECK(first.failed == 0);
    R_TEST_CHECK(second.failed == 0);
    return 0;
}

static int r_test_allocation_free(void) {
    RRuntimeAllocator allocator;
    RRuntimeAllocationStatus allocation_status;
    RStdMathF32Result f32;
    RStdMathF64Result f64;
    RStdMathCFloatResult c_float;
    RStdMathCDoubleResult c_double;
    RStdMathCLongDoubleResult c_long_double;
    void *allocation = NULL;

    r_runtime_allocator_initialize(&allocator);
    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    f32 = r_std_math_asinh_f32(0.5F);
    f64 = r_std_math_asinh_f64(0.5);
    c_float = r_std_math_asinh_c_float(0.5F);
    c_double = r_std_math_asinh_c_double(0.5);
    c_long_double = r_std_math_asinh_c_long_double(0.5L);
    R_TEST_CHECK(f32.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(f64.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(c_float.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(c_double.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(c_long_double.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(0));

    allocation_status =
        r_runtime_allocator_allocate(&allocator, 1U, _Alignof(unsigned char), &allocation);
    R_TEST_CHECK(allocation_status == R_RUNTIME_ALLOCATION_EXHAUSTED);
    R_TEST_CHECK(allocation == NULL);
    return 0;
}

int main(void) {
    int original_errno = errno;
    fenv_t original_environment;

    R_TEST_CHECK(fegetenv(&original_environment) == 0);
    R_TEST_CHECK(r_test_float_family(r_test_asinh_f32, FE_UPWARD, FE_DIVBYZERO, E2BIG) == 0);
    R_TEST_CHECK(r_test_double_family(r_test_asinh_f64, FE_DOWNWARD, FE_INVALID, EDOM) == 0);
    R_TEST_CHECK(r_test_float_family(r_test_asinh_c_float, FE_DOWNWARD, FE_DIVBYZERO, ERANGE) == 0);
    R_TEST_CHECK(r_test_double_family(r_test_asinh_c_double, FE_UPWARD, FE_INVALID, EILSEQ) == 0);
    R_TEST_CHECK(r_test_long_double_family(
                     r_test_asinh_c_long_double, FE_DOWNWARD, FE_DIVBYZERO, ENOMEM) == 0);
    R_TEST_CHECK(r_test_concurrent_environments() == 0);
    R_TEST_CHECK(r_test_allocation_free() == 0);
    R_TEST_CHECK(fesetenv(&original_environment) == 0);
    errno = original_errno;
    (void)fprintf(stdout, "library_math_asinh_tests: ok\n");
    return 0;
}

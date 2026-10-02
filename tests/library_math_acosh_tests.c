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

    _Static_assert(LDBL_MANT_DIG == 53, "target long double uses a binary64 significand");
    _Static_assert(LDBL_MAX_EXP == 1024, "target long double uses a binary64 exponent");
    _Static_assert(sizeof(value) == sizeof(bits), "target long double uses binary64 storage");
    (void)memcpy(&value, &bits, sizeof(value));
    return value;
}

static _Bool r_test_is_quiet_nan_f32_bits(float value) {
    uint32_t bits;

    (void)memcpy(&bits, &value, sizeof(bits));
    return ((bits & UINT32_C(0x7f800000)) == UINT32_C(0x7f800000)) &&
           ((bits & UINT32_C(0x007fffff)) != UINT32_C(0)) &&
           ((bits & UINT32_C(0x00400000)) != UINT32_C(0));
}

static _Bool r_test_is_quiet_nan_f64_bits(double value) {
    uint64_t bits;

    (void)memcpy(&bits, &value, sizeof(bits));
    return ((bits & UINT64_C(0x7ff0000000000000)) == UINT64_C(0x7ff0000000000000)) &&
           ((bits & UINT64_C(0x000fffffffffffff)) != UINT64_C(0)) &&
           ((bits & UINT64_C(0x0008000000000000)) != UINT64_C(0));
}

static _Bool r_test_is_quiet_nan_c_long_double_bits(long double value) {
    uint64_t bits;

    _Static_assert(LDBL_MANT_DIG == 53, "target long double uses a binary64 significand");
    _Static_assert(LDBL_MAX_EXP == 1024, "target long double uses a binary64 exponent");
    _Static_assert(sizeof(value) == sizeof(bits), "target long double uses binary64 storage");
    (void)memcpy(&bits, &value, sizeof(bits));
    return ((bits & UINT64_C(0x7ff0000000000000)) == UINT64_C(0x7ff0000000000000)) &&
           ((bits & UINT64_C(0x000fffffffffffff)) != UINT64_C(0)) &&
           ((bits & UINT64_C(0x0008000000000000)) != UINT64_C(0));
}

static RTestMathFloatResult r_test_acosh_f32(float value) {
    RStdMathF32Result public_result = r_std_math_acosh_f32(value);
    RTestMathFloatResult result = {public_result.status, public_result.value, public_result.error};

    return result;
}

static RTestMathDoubleResult r_test_acosh_f64(double value) {
    RStdMathF64Result public_result = r_std_math_acosh_f64(value);
    RTestMathDoubleResult result = {public_result.status, public_result.value, public_result.error};

    return result;
}

static RTestMathFloatResult r_test_acosh_c_float(float value) {
    RStdMathCFloatResult public_result = r_std_math_acosh_c_float(value);
    RTestMathFloatResult result = {public_result.status, public_result.value, public_result.error};

    return result;
}

static RTestMathDoubleResult r_test_acosh_c_double(double value) {
    RStdMathCDoubleResult public_result = r_std_math_acosh_c_double(value);
    RTestMathDoubleResult result = {public_result.status, public_result.value, public_result.error};

    return result;
}

static RTestMathLongDoubleResult r_test_acosh_c_long_double(long double value) {
    RStdMathCLongDoubleResult public_result = r_std_math_acosh_c_long_double(value);
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
    return (fegetround() == rounding) &&
           ((fetestexcept(FE_ALL_EXCEPT) & FE_ALL_EXCEPT) == exceptions) && (errno == native_errno);
}

static int r_test_environment_matches(int rounding, int exceptions, int native_errno) {
    R_TEST_CHECK(r_test_environment_is(rounding, exceptions, native_errno));
    return 0;
}

static int r_test_native_acosh_f32(float value, float *result) {
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
    *result = acoshf(canonical_operand);
    if (fesetenv(&caller_environment) != 0) {
        errno = caller_errno;
        return 1;
    }
    errno = caller_errno;
    return 0;
}

static int r_test_native_acosh_f64(double value, double *result) {
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
    *result = acosh(canonical_operand);
    if (fesetenv(&caller_environment) != 0) {
        errno = caller_errno;
        return 1;
    }
    errno = caller_errno;
    return 0;
}

static int r_test_native_acosh_c_long_double(long double value, long double *result) {
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
    *result = acoshl(canonical_operand);
    if (fesetenv(&caller_environment) != 0) {
        errno = caller_errno;
        return 1;
    }
    errno = caller_errno;
    return 0;
}

static int r_test_float_domain(RTestMathFloatOperation operation,
                               float value,
                               int rounding,
                               int exceptions,
                               int native_errno) {
    RTestMathFloatResult result = operation(value);

    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_DOMAIN);
    R_TEST_CHECK(result.error.native_code == INT64_C(0));
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);
    return 0;
}

static int r_test_double_domain(RTestMathDoubleOperation operation,
                                double value,
                                int rounding,
                                int exceptions,
                                int native_errno) {
    RTestMathDoubleResult result = operation(value);

    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_DOMAIN);
    R_TEST_CHECK(result.error.native_code == INT64_C(0));
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);
    return 0;
}

static int r_test_long_double_domain(RTestMathLongDoubleOperation operation,
                                     long double value,
                                     int rounding,
                                     int exceptions,
                                     int native_errno) {
    RTestMathLongDoubleResult result = operation(value);

    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_DOMAIN);
    R_TEST_CHECK(result.error.native_code == INT64_C(0));
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);
    return 0;
}

static int r_test_float_family(RTestMathFloatOperation operation,
                               int rounding,
                               int exceptions,
                               int native_errno) {
    float above_one = nextafterf(1.0F, INFINITY);
    float below_one = nextafterf(1.0F, -INFINITY);
    float signaling_nan = r_test_signaling_nan_f32_value();
    float above_reference;
    float normal_reference;
    float maximum_reference;
    RTestMathFloatResult result;

    R_TEST_CHECK(r_test_seed_environment(rounding, exceptions, native_errno) == 0);
    R_TEST_CHECK(r_test_native_acosh_f32(above_one, &above_reference) == 0);
    R_TEST_CHECK(r_test_native_acosh_f32(2.0F, &normal_reference) == 0);
    R_TEST_CHECK(r_test_native_acosh_f32(FLT_MAX, &maximum_reference) == 0);
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(1.0F);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == 0.0F);
    R_TEST_CHECK(!signbit(result.value));
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(above_one);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == above_reference);
    R_TEST_CHECK(isnormal(result.value));
    R_TEST_CHECK(result.value > 0.0F);
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(2.0F);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == normal_reference);
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(FLT_MAX);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == maximum_reference);
    R_TEST_CHECK(isfinite(result.value));
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(INFINITY);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isinf(result.value));
    R_TEST_CHECK(!signbit(result.value));
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(NAN);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isnan(result.value));
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    R_TEST_CHECK(r_test_float_domain(operation, below_one, rounding, exceptions, native_errno) ==
                 0);
    R_TEST_CHECK(r_test_float_domain(operation, 0.0F, rounding, exceptions, native_errno) == 0);
    R_TEST_CHECK(r_test_float_domain(operation, -0.0F, rounding, exceptions, native_errno) == 0);
    R_TEST_CHECK(r_test_float_domain(operation, -2.0F, rounding, exceptions, native_errno) == 0);
    R_TEST_CHECK(r_test_float_domain(operation, -INFINITY, rounding, exceptions, native_errno) ==
                 0);

    R_TEST_CHECK(r_test_seed_environment(rounding, FE_DIVBYZERO, native_errno) == 0);
    result = operation(signaling_nan);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_environment_matches(rounding, FE_DIVBYZERO, native_errno) == 0);
    R_TEST_CHECK(r_test_is_quiet_nan_f32_bits(result.value));
    R_TEST_CHECK(r_test_environment_matches(rounding, FE_DIVBYZERO, native_errno) == 0);
    return 0;
}

static int r_test_double_family(RTestMathDoubleOperation operation,
                                int rounding,
                                int exceptions,
                                int native_errno) {
    double above_one = nextafter(1.0, INFINITY);
    double below_one = nextafter(1.0, -INFINITY);
    double signaling_nan = r_test_signaling_nan_f64_value();
    double above_reference;
    double normal_reference;
    double maximum_reference;
    RTestMathDoubleResult result;

    R_TEST_CHECK(r_test_seed_environment(rounding, exceptions, native_errno) == 0);
    R_TEST_CHECK(r_test_native_acosh_f64(above_one, &above_reference) == 0);
    R_TEST_CHECK(r_test_native_acosh_f64(2.0, &normal_reference) == 0);
    R_TEST_CHECK(r_test_native_acosh_f64(DBL_MAX, &maximum_reference) == 0);
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(1.0);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == 0.0);
    R_TEST_CHECK(!signbit(result.value));
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(above_one);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == above_reference);
    R_TEST_CHECK(isnormal(result.value));
    R_TEST_CHECK(result.value > 0.0);
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(2.0);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == normal_reference);
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(DBL_MAX);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == maximum_reference);
    R_TEST_CHECK(isfinite(result.value));
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(INFINITY);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isinf(result.value));
    R_TEST_CHECK(!signbit(result.value));
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(NAN);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isnan(result.value));
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    R_TEST_CHECK(r_test_double_domain(operation, below_one, rounding, exceptions, native_errno) ==
                 0);
    R_TEST_CHECK(r_test_double_domain(operation, 0.0, rounding, exceptions, native_errno) == 0);
    R_TEST_CHECK(r_test_double_domain(operation, -0.0, rounding, exceptions, native_errno) == 0);
    R_TEST_CHECK(r_test_double_domain(operation, -2.0, rounding, exceptions, native_errno) == 0);
    R_TEST_CHECK(r_test_double_domain(operation, -INFINITY, rounding, exceptions, native_errno) ==
                 0);

    R_TEST_CHECK(r_test_seed_environment(rounding, FE_DIVBYZERO, native_errno) == 0);
    result = operation(signaling_nan);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_environment_matches(rounding, FE_DIVBYZERO, native_errno) == 0);
    R_TEST_CHECK(r_test_is_quiet_nan_f64_bits(result.value));
    R_TEST_CHECK(r_test_environment_matches(rounding, FE_DIVBYZERO, native_errno) == 0);
    return 0;
}

static int r_test_long_double_family(RTestMathLongDoubleOperation operation,
                                     int rounding,
                                     int exceptions,
                                     int native_errno) {
    long double above_one = nextafterl(1.0L, INFINITY);
    long double below_one = nextafterl(1.0L, -INFINITY);
    long double signaling_nan = r_test_signaling_nan_c_long_double_value();
    long double above_reference;
    long double normal_reference;
    long double maximum_reference;
    RTestMathLongDoubleResult result;

    R_TEST_CHECK(r_test_seed_environment(rounding, exceptions, native_errno) == 0);
    R_TEST_CHECK(r_test_native_acosh_c_long_double(above_one, &above_reference) == 0);
    R_TEST_CHECK(r_test_native_acosh_c_long_double(2.0L, &normal_reference) == 0);
    R_TEST_CHECK(r_test_native_acosh_c_long_double(LDBL_MAX, &maximum_reference) == 0);
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(1.0L);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == 0.0L);
    R_TEST_CHECK(!signbit(result.value));
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(above_one);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == above_reference);
    R_TEST_CHECK(isnormal(result.value));
    R_TEST_CHECK(result.value > 0.0L);
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(2.0L);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == normal_reference);
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(LDBL_MAX);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == maximum_reference);
    R_TEST_CHECK(isfinite(result.value));
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(INFINITY);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isinf(result.value));
    R_TEST_CHECK(!signbit(result.value));
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(NAN);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isnan(result.value));
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    R_TEST_CHECK(
        r_test_long_double_domain(operation, below_one, rounding, exceptions, native_errno) == 0);
    R_TEST_CHECK(r_test_long_double_domain(operation, 0.0L, rounding, exceptions, native_errno) ==
                 0);
    R_TEST_CHECK(r_test_long_double_domain(operation, -0.0L, rounding, exceptions, native_errno) ==
                 0);
    R_TEST_CHECK(r_test_long_double_domain(operation, -2.0L, rounding, exceptions, native_errno) ==
                 0);
    R_TEST_CHECK(
        r_test_long_double_domain(operation, -INFINITY, rounding, exceptions, native_errno) == 0);

    R_TEST_CHECK(r_test_seed_environment(rounding, FE_DIVBYZERO, native_errno) == 0);
    result = operation(signaling_nan);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(r_test_environment_matches(rounding, FE_DIVBYZERO, native_errno) == 0);
    R_TEST_CHECK(r_test_is_quiet_nan_c_long_double_bits(result.value));
    R_TEST_CHECK(r_test_environment_matches(rounding, FE_DIVBYZERO, native_errno) == 0);
    return 0;
}

typedef struct RTestAcoshThreadContext {
    int rounding;
    int exceptions;
    int native_errno;
    float expected_float;
    double expected_double;
    long double expected_long_double;
    int failed;
} RTestAcoshThreadContext;

static void *r_test_acosh_thread_main(void *opaque_context) {
    RTestAcoshThreadContext *context = opaque_context;
    int iteration;

    if (r_test_seed_environment(context->rounding, context->exceptions, context->native_errno) !=
        0) {
        context->failed = 1;
        return NULL;
    }
    for (iteration = 0; iteration < 500; ++iteration) {
        RStdMathF32Result f32 = r_std_math_acosh_f32(2.0F);
        RStdMathF64Result f64 = r_std_math_acosh_f64(2.0);
        RStdMathCFloatResult c_float = r_std_math_acosh_c_float(2.0F);
        RStdMathCDoubleResult c_double = r_std_math_acosh_c_double(2.0);
        RStdMathCLongDoubleResult c_long_double = r_std_math_acosh_c_long_double(2.0L);

        if ((f32.status != R_STD_MATH_CALL_SUCCESS) || (f32.value != context->expected_float) ||
            (f64.status != R_STD_MATH_CALL_SUCCESS) || (f64.value != context->expected_double) ||
            (c_float.status != R_STD_MATH_CALL_SUCCESS) ||
            (c_float.value != context->expected_float) ||
            (c_double.status != R_STD_MATH_CALL_SUCCESS) ||
            (c_double.value != context->expected_double) ||
            (c_long_double.status != R_STD_MATH_CALL_SUCCESS) ||
            (c_long_double.value != context->expected_long_double) ||
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
    RTestAcoshThreadContext first;
    RTestAcoshThreadContext second;

    R_TEST_CHECK(r_test_native_acosh_f32(2.0F, &expected_float) == 0);
    R_TEST_CHECK(r_test_native_acosh_f64(2.0, &expected_double) == 0);
    R_TEST_CHECK(r_test_native_acosh_c_long_double(2.0L, &expected_long_double) == 0);
    first = (RTestAcoshThreadContext){
        FE_DOWNWARD, FE_DIVBYZERO, EDOM, expected_float, expected_double, expected_long_double, 0};
    second = (RTestAcoshThreadContext){
        FE_UPWARD, FE_INVALID, ERANGE, expected_float, expected_double, expected_long_double, 0};

    R_TEST_CHECK(pthread_create(&first_thread, NULL, r_test_acosh_thread_main, &first) == 0);
    R_TEST_CHECK(pthread_create(&second_thread, NULL, r_test_acosh_thread_main, &second) == 0);
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
    f32 = r_std_math_acosh_f32(2.0F);
    f64 = r_std_math_acosh_f64(2.0);
    c_float = r_std_math_acosh_c_float(2.0F);
    c_double = r_std_math_acosh_c_double(2.0);
    c_long_double = r_std_math_acosh_c_long_double(2.0L);
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
    R_TEST_CHECK(r_test_float_family(r_test_acosh_f32, FE_UPWARD, FE_DIVBYZERO, E2BIG) == 0);
    R_TEST_CHECK(r_test_double_family(r_test_acosh_f64, FE_DOWNWARD, FE_INVALID, EDOM) == 0);
    R_TEST_CHECK(r_test_float_family(r_test_acosh_c_float, FE_DOWNWARD, FE_DIVBYZERO, ERANGE) == 0);
    R_TEST_CHECK(r_test_double_family(r_test_acosh_c_double, FE_UPWARD, FE_INVALID, EILSEQ) == 0);
    R_TEST_CHECK(r_test_long_double_family(
                     r_test_acosh_c_long_double, FE_DOWNWARD, FE_DIVBYZERO, ENOMEM) == 0);
    R_TEST_CHECK(r_test_concurrent_environments() == 0);
    R_TEST_CHECK(r_test_allocation_free() == 0);
    R_TEST_CHECK(fesetenv(&original_environment) == 0);
    errno = original_errno;
    (void)fprintf(stdout, "library_math_acosh_tests: ok\n");
    return 0;
}

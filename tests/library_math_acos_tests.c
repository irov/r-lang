#include "r_std_math.h"

#include "r_runtime_allocator.h"

#include <errno.h>
#include <fenv.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

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

static RTestMathFloatResult r_test_acos_f32(float value) {
    RStdMathF32Result public_result = r_std_math_acos_f32(value);
    RTestMathFloatResult result = {public_result.status, public_result.value, public_result.error};

    return result;
}

static RTestMathDoubleResult r_test_acos_f64(double value) {
    RStdMathF64Result public_result = r_std_math_acos_f64(value);
    RTestMathDoubleResult result = {public_result.status, public_result.value, public_result.error};

    return result;
}

static RTestMathFloatResult r_test_acos_c_float(float value) {
    RStdMathCFloatResult public_result = r_std_math_acos_c_float(value);
    RTestMathFloatResult result = {public_result.status, public_result.value, public_result.error};

    return result;
}

static RTestMathDoubleResult r_test_acos_c_double(double value) {
    RStdMathCDoubleResult public_result = r_std_math_acos_c_double(value);
    RTestMathDoubleResult result = {public_result.status, public_result.value, public_result.error};

    return result;
}

static RTestMathLongDoubleResult r_test_acos_c_long_double(long double value) {
    RStdMathCLongDoubleResult public_result = r_std_math_acos_c_long_double(value);
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

static int r_test_native_acos_f32(float value, float *result) {
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
    *result = acosf(canonical_operand);
    if (fesetenv(&caller_environment) != 0) {
        errno = caller_errno;
        return 1;
    }
    errno = caller_errno;
    return 0;
}

static int r_test_native_acos_f64(double value, double *result) {
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
    *result = acos(canonical_operand);
    if (fesetenv(&caller_environment) != 0) {
        errno = caller_errno;
        return 1;
    }
    errno = caller_errno;
    return 0;
}

static int r_test_native_acos_c_long_double(long double value, long double *result) {
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
    *result = acosl(canonical_operand);
    if (fesetenv(&caller_environment) != 0) {
        errno = caller_errno;
        return 1;
    }
    errno = caller_errno;
    return 0;
}

static int r_test_float_family(RTestMathFloatOperation operation,
                               int rounding,
                               int exceptions,
                               int native_errno) {
    float negative_boundary;
    float negative_inside;
    float positive_inside;
    float zero_reference;
    RTestMathFloatResult result;

    R_TEST_CHECK(r_test_seed_environment(rounding, exceptions, native_errno) == 0);
    R_TEST_CHECK(r_test_native_acos_f32(-1.0F, &negative_boundary) == 0);
    R_TEST_CHECK(r_test_native_acos_f32(-0.5F, &negative_inside) == 0);
    R_TEST_CHECK(r_test_native_acos_f32(0.5F, &positive_inside) == 0);
    R_TEST_CHECK(r_test_native_acos_f32(0.0F, &zero_reference) == 0);
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(1.0F);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == 0.0F);
    R_TEST_CHECK(!signbit(result.value));
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(-1.0F);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == negative_boundary);
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(0.5F);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == positive_inside);
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(-0.5F);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == negative_inside);
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(0.0F);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == zero_reference);
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(-0.0F);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == zero_reference);
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(2.0F);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_DOMAIN);
    R_TEST_CHECK(result.error.native_code == INT64_C(0));
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(-2.0F);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_DOMAIN);
    R_TEST_CHECK(result.error.native_code == INT64_C(0));
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(INFINITY);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_DOMAIN);
    R_TEST_CHECK(result.error.native_code == INT64_C(0));
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(-INFINITY);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_DOMAIN);
    R_TEST_CHECK(result.error.native_code == INT64_C(0));
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(NAN);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isnan(result.value));
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);
    return 0;
}

static int r_test_double_family(RTestMathDoubleOperation operation,
                                int rounding,
                                int exceptions,
                                int native_errno) {
    double negative_boundary;
    double negative_inside;
    double positive_inside;
    double zero_reference;
    RTestMathDoubleResult result;

    R_TEST_CHECK(r_test_seed_environment(rounding, exceptions, native_errno) == 0);
    R_TEST_CHECK(r_test_native_acos_f64(-1.0, &negative_boundary) == 0);
    R_TEST_CHECK(r_test_native_acos_f64(-0.5, &negative_inside) == 0);
    R_TEST_CHECK(r_test_native_acos_f64(0.5, &positive_inside) == 0);
    R_TEST_CHECK(r_test_native_acos_f64(0.0, &zero_reference) == 0);
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(1.0);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == 0.0);
    R_TEST_CHECK(!signbit(result.value));
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(-1.0);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == negative_boundary);
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(0.5);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == positive_inside);
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(-0.5);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == negative_inside);
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(0.0);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == zero_reference);
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(-0.0);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == zero_reference);
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(2.0);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_DOMAIN);
    R_TEST_CHECK(result.error.native_code == INT64_C(0));
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(-2.0);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_DOMAIN);
    R_TEST_CHECK(result.error.native_code == INT64_C(0));
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(INFINITY);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_DOMAIN);
    R_TEST_CHECK(result.error.native_code == INT64_C(0));
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(-INFINITY);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_DOMAIN);
    R_TEST_CHECK(result.error.native_code == INT64_C(0));
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(NAN);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isnan(result.value));
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);
    return 0;
}

static int r_test_long_double_family(RTestMathLongDoubleOperation operation,
                                     int rounding,
                                     int exceptions,
                                     int native_errno) {
    long double negative_boundary;
    long double negative_inside;
    long double positive_inside;
    long double zero_reference;
    RTestMathLongDoubleResult result;

    R_TEST_CHECK(r_test_seed_environment(rounding, exceptions, native_errno) == 0);
    R_TEST_CHECK(r_test_native_acos_c_long_double(-1.0L, &negative_boundary) == 0);
    R_TEST_CHECK(r_test_native_acos_c_long_double(-0.5L, &negative_inside) == 0);
    R_TEST_CHECK(r_test_native_acos_c_long_double(0.5L, &positive_inside) == 0);
    R_TEST_CHECK(r_test_native_acos_c_long_double(0.0L, &zero_reference) == 0);
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(1.0L);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == 0.0L);
    R_TEST_CHECK(!signbit(result.value));
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(-1.0L);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == negative_boundary);
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(0.5L);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == positive_inside);
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(-0.5L);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == negative_inside);
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(0.0L);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == zero_reference);
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(-0.0L);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(result.value == zero_reference);
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(2.0L);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_DOMAIN);
    R_TEST_CHECK(result.error.native_code == INT64_C(0));
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(-2.0L);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_DOMAIN);
    R_TEST_CHECK(result.error.native_code == INT64_C(0));
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(INFINITY);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_DOMAIN);
    R_TEST_CHECK(result.error.native_code == INT64_C(0));
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(-INFINITY);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_ERROR);
    R_TEST_CHECK(result.error.code == R_STD_MATH_ERROR_DOMAIN);
    R_TEST_CHECK(result.error.native_code == INT64_C(0));
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);

    result = operation(NAN);
    R_TEST_CHECK(result.status == R_STD_MATH_CALL_SUCCESS);
    R_TEST_CHECK(isnan(result.value));
    R_TEST_CHECK(r_test_environment_matches(rounding, exceptions, native_errno) == 0);
    return 0;
}

typedef struct RTestAcosThreadContext {
    int rounding;
    int exceptions;
    int native_errno;
    float expected_float;
    double expected_double;
    long double expected_long_double;
    int failed;
} RTestAcosThreadContext;

static void *r_test_acos_thread_main(void *opaque_context) {
    RTestAcosThreadContext *context = opaque_context;
    int iteration;

    if (r_test_seed_environment(context->rounding, context->exceptions, context->native_errno) !=
        0) {
        context->failed = 1;
        return NULL;
    }
    for (iteration = 0; iteration < 500; ++iteration) {
        RStdMathF32Result f32 = r_std_math_acos_f32(0.5F);
        RStdMathF64Result f64 = r_std_math_acos_f64(0.5);
        RStdMathCFloatResult c_float = r_std_math_acos_c_float(0.5F);
        RStdMathCDoubleResult c_double = r_std_math_acos_c_double(0.5);
        RStdMathCLongDoubleResult c_long_double = r_std_math_acos_c_long_double(0.5L);

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
    RTestAcosThreadContext first;
    RTestAcosThreadContext second;

    R_TEST_CHECK(r_test_native_acos_f32(0.5F, &expected_float) == 0);
    R_TEST_CHECK(r_test_native_acos_f64(0.5, &expected_double) == 0);
    R_TEST_CHECK(r_test_native_acos_c_long_double(0.5L, &expected_long_double) == 0);
    first = (RTestAcosThreadContext){
        FE_DOWNWARD, FE_DIVBYZERO, EDOM, expected_float, expected_double, expected_long_double, 0};
    second = (RTestAcosThreadContext){
        FE_UPWARD, FE_INVALID, ERANGE, expected_float, expected_double, expected_long_double, 0};

    R_TEST_CHECK(pthread_create(&first_thread, NULL, r_test_acos_thread_main, &first) == 0);
    R_TEST_CHECK(pthread_create(&second_thread, NULL, r_test_acos_thread_main, &second) == 0);
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
    f32 = r_std_math_acos_f32(0.5F);
    f64 = r_std_math_acos_f64(0.5);
    c_float = r_std_math_acos_c_float(0.5F);
    c_double = r_std_math_acos_c_double(0.5);
    c_long_double = r_std_math_acos_c_long_double(0.5L);
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
    R_TEST_CHECK(r_test_float_family(r_test_acos_f32, FE_UPWARD, FE_DIVBYZERO, E2BIG) == 0);
    R_TEST_CHECK(r_test_double_family(r_test_acos_f64, FE_DOWNWARD, FE_INVALID, EDOM) == 0);
    R_TEST_CHECK(r_test_float_family(r_test_acos_c_float, FE_DOWNWARD, FE_DIVBYZERO, ERANGE) == 0);
    R_TEST_CHECK(r_test_double_family(r_test_acos_c_double, FE_UPWARD, FE_INVALID, EILSEQ) == 0);
    R_TEST_CHECK(r_test_long_double_family(
                     r_test_acos_c_long_double, FE_DOWNWARD, FE_DIVBYZERO, ENOMEM) == 0);
    R_TEST_CHECK(r_test_concurrent_environments() == 0);
    R_TEST_CHECK(r_test_allocation_free() == 0);
    R_TEST_CHECK(fesetenv(&original_environment) == 0);
    errno = original_errno;
    (void)fprintf(stdout, "library_math_acos_tests: ok\n");
    return 0;
}

#include "r_std_format.h"

#include <errno.h>
#include <fenv.h>
#include <float.h>
#include <locale.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xlocale.h>

#pragma STDC FENV_ACCESS ON

#define R_TEST_CHECK(expression)                                                                   \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            (void)fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, #expression); \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static RStdStringView r_test_view(const char *text) {
    return (RStdStringView){(const uint8_t *)text, strlen(text)};
}

static _Bool r_test_equals(RStdStringView actual, const char *expected) {
    size_t length = strlen(expected);
    return (actual.length == length) && (memcmp(actual.data, expected, length) == 0);
}

static int r_test_builder_and_finish(void) {
    RRuntimeAllocator allocator;
    RStdFormatBuilderResult created;
    RStdString finished;

    r_runtime_allocator_initialize(&allocator);
    created = r_std_format_create(&allocator);
    R_TEST_CHECK(created.status == R_STD_FORMAT_CALL_SUCCESS);
    R_TEST_CHECK(r_test_equals(r_std_format_as_str(&created.value), ""));
    R_TEST_CHECK(r_std_format_append_str(&created.value, r_test_view("value=")).status ==
                 R_STD_FORMAT_CALL_SUCCESS);
    R_TEST_CHECK(r_std_format_append_suffix(
                     &created.value, (RStdConvertParsedInteger){1, UINT64_C(255)}, UINT32_C(16))
                     .status == R_STD_FORMAT_CALL_SUCCESS);
    R_TEST_CHECK(r_std_format_append_char(&created.value, UINT32_C(0x20ac)).status ==
                 R_STD_FORMAT_CALL_SUCCESS);
    R_TEST_CHECK(r_test_equals(r_std_format_as_str(&created.value), "value=-ff\xe2\x82\xac"));

    finished = r_std_format_finish(&created.value);
    R_TEST_CHECK(r_test_equals(r_std_string_as_str(&finished), "value=-ff\xe2\x82\xac"));
    R_TEST_CHECK(created.value.output.bytes.data == NULL);
    r_runtime_string_destroy(&finished);
    r_runtime_string_destroy(&created.value.output);
    return 0;
}

static int r_test_errors_are_transactional(void) {
    RRuntimeAllocator allocator;
    RStdFormatBuilderResult created;
    RStdFormatAppendResult appended;
    void *allocation;
    size_t capacity;

    r_runtime_allocator_initialize(&allocator);
    created = r_std_format_with_capacity(&allocator, 4U);
    R_TEST_CHECK(created.status == R_STD_FORMAT_CALL_SUCCESS);
    R_TEST_CHECK(r_std_format_append_str(&created.value, r_test_view("base")).status ==
                 R_STD_FORMAT_CALL_SUCCESS);
    allocation = created.value.output.bytes.data;
    capacity = created.value.output.bytes.capacity;

    appended = r_std_format_append_suffix(
        &created.value, (RStdConvertParsedInteger){0, UINT64_C(1)}, UINT32_C(1));
    R_TEST_CHECK(appended.status == R_STD_FORMAT_CALL_ERROR);
    R_TEST_CHECK(appended.error.kind == R_STD_FORMAT_ERROR_INVALID_RADIX);
    R_TEST_CHECK(created.value.output.bytes.data == allocation);
    R_TEST_CHECK(r_test_equals(r_std_format_as_str(&created.value), "base"));

    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    appended = r_std_format_append_suffix(
        &created.value, (RStdConvertParsedInteger){0, UINT64_MAX}, UINT32_C(2));
    R_TEST_CHECK(appended.status == R_STD_FORMAT_CALL_ERROR);
    R_TEST_CHECK(appended.error.kind == R_STD_FORMAT_ERROR_ALLOCATION_FAILED);
    R_TEST_CHECK(appended.error.allocation_error == R_STD_ALLOC_ERROR_OUT_OF_MEMORY);
    R_TEST_CHECK(created.value.output.bytes.data == allocation);
    R_TEST_CHECK(created.value.output.bytes.capacity == capacity);
    R_TEST_CHECK(r_test_equals(r_std_format_as_str(&created.value), "base"));

    r_std_format_clear(&created.value);
    R_TEST_CHECK(created.value.output.bytes.data == allocation);
    R_TEST_CHECK(r_test_equals(r_std_format_as_str(&created.value), ""));
    r_runtime_string_destroy(&created.value.output);
    return 0;
}

static int r_test_float_canonical_spellings(void) {
    RRuntimeAllocator allocator;
    RStdFormatBuilderResult created;

    r_runtime_allocator_initialize(&allocator);
    created = r_std_format_with_capacity(&allocator, 128U);
    R_TEST_CHECK(created.status == R_STD_FORMAT_CALL_SUCCESS);

    R_TEST_CHECK(r_std_format_append_f32(&created.value, 1.25F).status ==
                 R_STD_FORMAT_CALL_SUCCESS);
    R_TEST_CHECK(r_test_equals(r_std_format_as_str(&created.value), "1.25"));
    r_std_format_clear(&created.value);
    R_TEST_CHECK(r_std_format_append_f32(&created.value, 100.0F).status ==
                 R_STD_FORMAT_CALL_SUCCESS);
    /* "100" and "1e2" tie before the notation-independent parity and fixed-form rules. */
    R_TEST_CHECK(r_test_equals(r_std_format_as_str(&created.value), "100"));
    r_std_format_clear(&created.value);
    R_TEST_CHECK(r_std_format_append_f32(&created.value, 1000.0F).status ==
                 R_STD_FORMAT_CALL_SUCCESS);
    R_TEST_CHECK(r_test_equals(r_std_format_as_str(&created.value), "1e3"));
    r_std_format_clear(&created.value);
    R_TEST_CHECK(r_std_format_append_f32(&created.value, 0.01F).status ==
                 R_STD_FORMAT_CALL_SUCCESS);
    R_TEST_CHECK(r_test_equals(r_std_format_as_str(&created.value), "0.01"));
    r_std_format_clear(&created.value);
    R_TEST_CHECK(r_std_format_append_f32(&created.value, 0.001F).status ==
                 R_STD_FORMAT_CALL_SUCCESS);
    R_TEST_CHECK(r_test_equals(r_std_format_as_str(&created.value), "1e-3"));
    r_std_format_clear(&created.value);
    R_TEST_CHECK(r_std_format_append_f32(&created.value, FLT_TRUE_MIN).status ==
                 R_STD_FORMAT_CALL_SUCCESS);
    R_TEST_CHECK(r_test_equals(r_std_format_as_str(&created.value), "1e-45"));
    r_std_format_clear(&created.value);
    R_TEST_CHECK(r_std_format_append_f32(&created.value, FLT_MAX).status ==
                 R_STD_FORMAT_CALL_SUCCESS);
    R_TEST_CHECK(r_test_equals(r_std_format_as_str(&created.value), "3.4028235e38"));

    r_std_format_clear(&created.value);
    R_TEST_CHECK(r_std_format_append_f64(&created.value, -0.0).status == R_STD_FORMAT_CALL_SUCCESS);
    R_TEST_CHECK(r_test_equals(r_std_format_as_str(&created.value), "-0"));
    r_std_format_clear(&created.value);
    R_TEST_CHECK(r_std_format_append_f64(&created.value, DBL_TRUE_MIN).status ==
                 R_STD_FORMAT_CALL_SUCCESS);
    R_TEST_CHECK(r_test_equals(r_std_format_as_str(&created.value), "5e-324"));
    r_std_format_clear(&created.value);
    R_TEST_CHECK(r_std_format_append_f64(&created.value, DBL_MAX).status ==
                 R_STD_FORMAT_CALL_SUCCESS);
    R_TEST_CHECK(r_test_equals(r_std_format_as_str(&created.value), "1.7976931348623157e308"));
    r_std_format_clear(&created.value);
    R_TEST_CHECK(r_std_format_append_f64(&created.value, 12345678901234567890.0).status ==
                 R_STD_FORMAT_CALL_SUCCESS);
    R_TEST_CHECK(r_test_equals(r_std_format_as_str(&created.value), "12345678901234567168"));
    {
        uint64_t tie_bits = UINT64_C(0xc3146f827230d2ab);
        double tie_value;

        (void)memcpy(&tie_value, &tie_bits, sizeof(tie_value));
        r_std_format_clear(&created.value);
        R_TEST_CHECK(r_std_format_append_f64(&created.value, tie_value).status ==
                     R_STD_FORMAT_CALL_SUCCESS);
        /* Equidistant .7 and .8 both have minimum length; the even final digit selects .8. */
        R_TEST_CHECK(r_test_equals(r_std_format_as_str(&created.value), "-1438026396611754.8"));
    }
    {
        uint64_t tie_bits = UINT64_C(0xc3164148645e361d);
        double tie_value;

        (void)memcpy(&tie_value, &tie_bits, sizeof(tie_value));
        r_std_format_clear(&created.value);
        R_TEST_CHECK(r_std_format_append_f64(&created.value, tie_value).status ==
                     R_STD_FORMAT_CALL_SUCCESS);
        /* Equidistant .2 and .3 both have minimum length; the even final digit selects .2. */
        R_TEST_CHECK(r_test_equals(r_std_format_as_str(&created.value), "-1566057166245255.2"));
    }
    r_std_format_clear(&created.value);
    R_TEST_CHECK(r_std_format_append_f64(&created.value, INFINITY).status ==
                 R_STD_FORMAT_CALL_SUCCESS);
    R_TEST_CHECK(r_test_equals(r_std_format_as_str(&created.value), "inf"));
    r_std_format_clear(&created.value);
    R_TEST_CHECK(r_std_format_append_f64(&created.value, -INFINITY).status ==
                 R_STD_FORMAT_CALL_SUCCESS);
    R_TEST_CHECK(r_test_equals(r_std_format_as_str(&created.value), "-inf"));
    r_std_format_clear(&created.value);
    R_TEST_CHECK(r_std_format_append_f64(&created.value, -NAN).status == R_STD_FORMAT_CALL_SUCCESS);
    R_TEST_CHECK(r_test_equals(r_std_format_as_str(&created.value), "nan"));

    r_std_format_clear(&created.value);
    R_TEST_CHECK(r_std_format_append_c_float(&created.value, 1.5F).status ==
                 R_STD_FORMAT_CALL_SUCCESS);
    R_TEST_CHECK(r_test_equals(r_std_format_as_str(&created.value), "1.5"));
    r_std_format_clear(&created.value);
    R_TEST_CHECK(r_std_format_append_c_double(&created.value, 1.0e20).status ==
                 R_STD_FORMAT_CALL_SUCCESS);
    R_TEST_CHECK(r_test_equals(r_std_format_as_str(&created.value), "1e20"));
    r_std_format_clear(&created.value);
    R_TEST_CHECK(r_std_format_append_c_long_double(&created.value, 0.125L).status ==
                 R_STD_FORMAT_CALL_SUCCESS);
    R_TEST_CHECK(r_test_equals(r_std_format_as_str(&created.value), "0.125"));

    r_runtime_string_destroy(&created.value.output);
    return 0;
}

static uint64_t r_test_next_random(uint64_t *state) {
    *state = (*state * UINT64_C(6364136223846793005)) + UINT64_C(1442695040888963407);
    return *state;
}

static int r_test_float_round_trip_samples(void) {
    RRuntimeAllocator allocator;
    RStdFormatBuilderResult created;
    uint64_t random_state = UINT64_C(0x6a09e667f3bcc909);
    size_t index;

    r_runtime_allocator_initialize(&allocator);
    created = r_std_format_with_capacity(&allocator, 128U);
    R_TEST_CHECK(created.status == R_STD_FORMAT_CALL_SUCCESS);
    for (index = 0U; index < 64U; index += 1U) {
        uint32_t bits = (uint32_t)r_test_next_random(&random_state);
        float value;
        RStdConvertParseF32Result parsed;

        (void)memcpy(&value, &bits, sizeof(value));
        r_std_format_clear(&created.value);
        R_TEST_CHECK(r_std_format_append_f32(&created.value, value).status ==
                     R_STD_FORMAT_CALL_SUCCESS);
        parsed = r_std_convert_parse_f32(r_std_format_as_str(&created.value));
        R_TEST_CHECK(parsed.status == R_STD_CONVERT_CALL_SUCCESS);
        if (isnan(value)) {
            R_TEST_CHECK(isnan(parsed.value));
        } else if (value == 0.0F) {
            R_TEST_CHECK(signbit(parsed.value) == signbit(value));
        } else {
            R_TEST_CHECK(parsed.value == value);
        }
    }
    for (index = 0U; index < 64U; index += 1U) {
        uint64_t bits = r_test_next_random(&random_state);
        double value;
        RStdConvertParseF64Result parsed;

        (void)memcpy(&value, &bits, sizeof(value));
        r_std_format_clear(&created.value);
        R_TEST_CHECK(r_std_format_append_f64(&created.value, value).status ==
                     R_STD_FORMAT_CALL_SUCCESS);
        parsed = r_std_convert_parse_f64(r_std_format_as_str(&created.value));
        R_TEST_CHECK(parsed.status == R_STD_CONVERT_CALL_SUCCESS);
        if (isnan(value)) {
            R_TEST_CHECK(isnan(parsed.value));
        } else if (value == 0.0) {
            R_TEST_CHECK(signbit(parsed.value) == signbit(value));
        } else {
            R_TEST_CHECK(parsed.value == value);
        }
    }
    r_runtime_string_destroy(&created.value.output);
    return 0;
}

static int r_test_float_environment_is_restored(void) {
    RRuntimeAllocator allocator;
    RStdFormatBuilderResult created;
    uint64_t signaling_nan_bits = UINT64_C(0x7ff0000000000001);
    double signaling_nan;
    fenv_t original_environment;
    locale_t french_locale;
    locale_t original_locale;
    int original_errno = errno;

    R_TEST_CHECK(fegetenv(&original_environment) == 0);
    french_locale = newlocale(LC_NUMERIC_MASK, "fr_FR.UTF-8", NULL);
    R_TEST_CHECK(french_locale != (locale_t)0);
    original_locale = uselocale(french_locale);
    R_TEST_CHECK(original_locale != (locale_t)0);
    R_TEST_CHECK(fesetround(FE_UPWARD) == 0);
    R_TEST_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    R_TEST_CHECK(feraiseexcept(FE_DIVBYZERO) == 0);
    errno = EDOM;

    r_runtime_allocator_initialize(&allocator);
    created = r_std_format_with_capacity(&allocator, 128U);
    R_TEST_CHECK(created.status == R_STD_FORMAT_CALL_SUCCESS);
    R_TEST_CHECK(r_std_format_append_f64(&created.value, 1234.5).status ==
                 R_STD_FORMAT_CALL_SUCCESS);
    R_TEST_CHECK(r_test_equals(r_std_format_as_str(&created.value), "1234.5"));
    R_TEST_CHECK(uselocale(NULL) == french_locale);
    R_TEST_CHECK(fegetround() == FE_UPWARD);
    R_TEST_CHECK(fetestexcept(FE_ALL_EXCEPT) == FE_DIVBYZERO);
    R_TEST_CHECK(errno == EDOM);

    (void)memcpy(&signaling_nan, &signaling_nan_bits, sizeof(signaling_nan));
    r_std_format_clear(&created.value);
    R_TEST_CHECK(r_std_format_append_f64(&created.value, signaling_nan).status ==
                 R_STD_FORMAT_CALL_SUCCESS);
    R_TEST_CHECK(r_test_equals(r_std_format_as_str(&created.value), "nan"));
    R_TEST_CHECK(uselocale(NULL) == french_locale);
    R_TEST_CHECK(fegetround() == FE_UPWARD);
    R_TEST_CHECK(fetestexcept(FE_ALL_EXCEPT) == FE_DIVBYZERO);
    R_TEST_CHECK(errno == EDOM);

    r_runtime_string_destroy(&created.value.output);
    R_TEST_CHECK(uselocale(original_locale) == french_locale);
    freelocale(french_locale);
    R_TEST_CHECK(fesetenv(&original_environment) == 0);
    errno = original_errno;
    return 0;
}

static int r_test_float_failures_are_transactional(void) {
    RRuntimeAllocator allocator;
    RStdFormatBuilderResult created;
    RStdFormatAppendResult appended;
    void *allocation;
    size_t capacity;

    r_runtime_allocator_initialize(&allocator);
    created = r_std_format_with_capacity(&allocator, 4U);
    R_TEST_CHECK(created.status == R_STD_FORMAT_CALL_SUCCESS);
    R_TEST_CHECK(r_std_format_append_str(&created.value, r_test_view("base")).status ==
                 R_STD_FORMAT_CALL_SUCCESS);
    allocation = created.value.output.bytes.data;
    capacity = created.value.output.bytes.capacity;

#define R_TEST_FLOAT_ALLOCATION_FAILURE(expression)                                                \
    do {                                                                                           \
        r_runtime_allocator_set_failure(&allocator, UINT64_C(1));                                  \
        appended = (expression);                                                                   \
        R_TEST_CHECK(appended.status == R_STD_FORMAT_CALL_ERROR);                                  \
        R_TEST_CHECK(appended.error.kind == R_STD_FORMAT_ERROR_ALLOCATION_FAILED);                 \
        R_TEST_CHECK(appended.error.allocation_error == R_STD_ALLOC_ERROR_OUT_OF_MEMORY);          \
        R_TEST_CHECK(created.value.output.bytes.data == allocation);                               \
        R_TEST_CHECK(created.value.output.bytes.capacity == capacity);                             \
        R_TEST_CHECK(r_test_equals(r_std_format_as_str(&created.value), "base"));                  \
    } while (0)

    R_TEST_FLOAT_ALLOCATION_FAILURE(r_std_format_append_f32(&created.value, FLT_MAX));
    R_TEST_FLOAT_ALLOCATION_FAILURE(r_std_format_append_f64(&created.value, DBL_MAX));
    R_TEST_FLOAT_ALLOCATION_FAILURE(r_std_format_append_c_float(&created.value, FLT_MAX));
    R_TEST_FLOAT_ALLOCATION_FAILURE(r_std_format_append_c_double(&created.value, DBL_MAX));
    R_TEST_FLOAT_ALLOCATION_FAILURE(
        r_std_format_append_c_long_double(&created.value, (long double)DBL_MAX));
#undef R_TEST_FLOAT_ALLOCATION_FAILURE

    r_runtime_string_destroy(&created.value.output);
    return 0;
}

typedef struct RTestFloatThreadState {
    double value;
    const char *expected;
    int rounding;
    int exception;
    int failed;
} RTestFloatThreadState;

static void *r_test_float_thread_main(void *context) {
    RTestFloatThreadState *state = context;
    RRuntimeAllocator allocator;
    RStdFormatBuilderResult created;
    fenv_t original_environment;
    size_t iteration;

    if ((fegetenv(&original_environment) != 0) || (fesetround(state->rounding) != 0) ||
        (feclearexcept(FE_ALL_EXCEPT) != 0) || (feraiseexcept(state->exception) != 0)) {
        state->failed = 1;
        return NULL;
    }
    r_runtime_allocator_initialize(&allocator);
    created = r_std_format_with_capacity(&allocator, 128U);
    if (created.status != R_STD_FORMAT_CALL_SUCCESS) {
        state->failed = 1;
        (void)fesetenv(&original_environment);
        return NULL;
    }
    for (iteration = 0U; iteration < 64U; iteration += 1U) {
        r_std_format_clear(&created.value);
        if ((r_std_format_append_f64(&created.value, state->value).status !=
             R_STD_FORMAT_CALL_SUCCESS) ||
            !r_test_equals(r_std_format_as_str(&created.value), state->expected) ||
            (fegetround() != state->rounding) ||
            (fetestexcept(FE_ALL_EXCEPT) != state->exception)) {
            state->failed = 1;
            break;
        }
    }
    r_runtime_string_destroy(&created.value.output);
    if (fesetenv(&original_environment) != 0) {
        state->failed = 1;
    }
    return NULL;
}

static int r_test_float_concurrent_environments(void) {
    RTestFloatThreadState downward = {1234.5, "1234.5", FE_DOWNWARD, FE_DIVBYZERO, 0};
    RTestFloatThreadState upward = {DBL_TRUE_MIN, "5e-324", FE_UPWARD, FE_INVALID, 0};
    pthread_t downward_thread;
    pthread_t upward_thread;

    R_TEST_CHECK(pthread_create(&downward_thread, NULL, r_test_float_thread_main, &downward) == 0);
    R_TEST_CHECK(pthread_create(&upward_thread, NULL, r_test_float_thread_main, &upward) == 0);
    R_TEST_CHECK(pthread_join(downward_thread, NULL) == 0);
    R_TEST_CHECK(pthread_join(upward_thread, NULL) == 0);
    R_TEST_CHECK(downward.failed == 0);
    R_TEST_CHECK(upward.failed == 0);
    return 0;
}

#include "format_spec_tests.inc"

int main(void) {
    R_TEST_CHECK(r_test_literal_specs() == 0);
    R_TEST_CHECK(r_test_text_widths() == 0);
    R_TEST_CHECK(r_test_builder_and_finish() == 0);
    R_TEST_CHECK(r_test_errors_are_transactional() == 0);
    R_TEST_CHECK(r_test_float_canonical_spellings() == 0);
    R_TEST_CHECK(r_test_float_environment_is_restored() == 0);
    R_TEST_CHECK(r_test_float_round_trip_samples() == 0);
    R_TEST_CHECK(r_test_float_failures_are_transactional() == 0);
    R_TEST_CHECK(r_test_float_concurrent_environments() == 0);
    (void)fprintf(stdout, "library_format_tests: ok\n");
    return 0;
}

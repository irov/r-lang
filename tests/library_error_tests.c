#include "r_std_error.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define R_TEST_CHECK(expression)                                                                   \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            (void)fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, #expression); \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

typedef struct RTestErrorNameCase {
    RStdErrorDomain domain;
    uint32_t code;
    const char *expected;
} RTestErrorNameCase;

static _Bool r_test_view_equals(RStdStringView view, const char *expected) {
    size_t length = strlen(expected);
    return (view.length == length) && (memcmp(view.data, expected, length) == 0);
}

static int r_test_names(void) {
    static const RTestErrorNameCase cases[] = {
        {R_STD_ERROR_DOMAIN_ALLOCATION, UINT32_C(0), "out_of_memory"},
        {R_STD_ERROR_DOMAIN_ASYNC_RUNTIME, UINT32_C(1), "runtime_stopping"},
        {R_STD_ERROR_DOMAIN_BYTES, UINT32_C(1), "range_overflow"},
        {R_STD_ERROR_DOMAIN_BYTES, UINT32_C(0x0100), "unexpected_end"},
        {R_STD_ERROR_DOMAIN_BYTES, UINT32_C(0x0101), "invalid_width"},
        {R_STD_ERROR_DOMAIN_STRING, UINT32_C(0x0101), "not_scalar_boundary"},
        {R_STD_ERROR_DOMAIN_CONVERSION, UINT32_C(0x0104), "inexact"},
        {R_STD_ERROR_DOMAIN_FORMAT, UINT32_C(0), "invalid_radix"},
        {R_STD_ERROR_DOMAIN_MATH, UINT32_C(1), "pole"},
        {R_STD_ERROR_DOMAIN_TIME, UINT32_C(0x0100), "invalid_nanoseconds"},
        {R_STD_ERROR_DOMAIN_ENVIRONMENT, UINT32_C(4), "resource_exhausted"},
        {R_STD_ERROR_DOMAIN_IO, UINT32_C(1), "broken_pipe"},
        {R_STD_ERROR_DOMAIN_FILESYSTEM, UINT32_C(18), "other"},
        {R_STD_ERROR_DOMAIN_FILESYSTEM, UINT32_C(19), "closed"},
        {R_STD_ERROR_DOMAIN_FILESYSTEM, UINT32_C(0x0103), "absolute_component"},
        {R_STD_ERROR_DOMAIN_NETWORK, UINT32_C(19), "other"},
        {R_STD_ERROR_DOMAIN_NETWORK, UINT32_C(0x0102), "invalid_component"},
        {R_STD_ERROR_DOMAIN_PROCESS, UINT32_C(5), "spawn_failed"},
        {R_STD_ERROR_DOMAIN_THREADING, UINT32_C(0x0100), "zero_participants"},
        {R_STD_ERROR_DOMAIN_C_ABI, UINT32_C(0x0102), "floating_environment_unavailable"},
    };
    size_t index;

    for (index = 0U; index < (sizeof(cases) / sizeof(cases[0])); index += 1U) {
        RStdStringView actual =
            r_std_error_name((RStdError){cases[index].domain, cases[index].code, INT64_C(0)});
        R_TEST_CHECK(r_test_view_equals(actual, cases[index].expected));
    }
    R_TEST_CHECK(r_test_view_equals(r_std_error_name((RStdError){
                                        R_STD_ERROR_DOMAIN_ALLOCATION,
                                        UINT32_C(99),
                                        INT64_C(0),
                                    }),
                                    "unknown"));
    R_TEST_CHECK(r_test_view_equals(
        r_std_error_name((RStdError){(RStdErrorDomain)99, UINT32_C(0), INT64_C(0)}), "unknown"));
    return 0;
}

static int r_test_diagnostic(void) {
    static const char expected[] =
        "domain=network family=primary name=timed_out code=17 native_code=-9223372036854775808";
    RRuntimeAllocator allocator;
    RStdErrorDiagnosticResult result;

    r_runtime_allocator_initialize(&allocator);
    result = r_std_error_diagnostic(
        &allocator, (RStdError){R_STD_ERROR_DOMAIN_NETWORK, UINT32_C(17), INT64_MIN});
    R_TEST_CHECK(result.status == R_STD_STRING_CALL_SUCCESS);
    R_TEST_CHECK(r_test_view_equals(r_std_string_as_str(&result.value), expected));
    r_runtime_string_destroy(&result.value);

    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    result = r_std_error_diagnostic(
        &allocator, (RStdError){R_STD_ERROR_DOMAIN_ALLOCATION, UINT32_C(0), INT64_C(0)});
    R_TEST_CHECK(result.status == R_STD_STRING_CALL_ERROR);
    R_TEST_CHECK(result.error == R_STD_ALLOC_ERROR_OUT_OF_MEMORY);
    R_TEST_CHECK(result.value.bytes.data == NULL);
    r_runtime_string_destroy(&result.value);
    return 0;
}

static int r_test_typed_conversions(void) {
    RStdStringError string_error = {
        R_STD_STRING_ERROR_INVALID_UTF8,
        37U,
        R_STD_ALLOC_ERROR_UNSUPPORTED_ALIGNMENT,
    };
    RStdError converted = r_std_error_from_alloc(R_STD_ALLOC_ERROR_SIZE_OVERFLOW);

    R_TEST_CHECK(converted.domain == R_STD_ERROR_DOMAIN_ALLOCATION);
    R_TEST_CHECK(converted.code == UINT32_C(1));
    R_TEST_CHECK(converted.native_code == INT64_C(0));
    converted = r_std_error_from_bytes(R_STD_BYTES_ERROR_RANGE_OVERFLOW);
    R_TEST_CHECK(converted.domain == R_STD_ERROR_DOMAIN_BYTES);
    R_TEST_CHECK(converted.code == UINT32_C(1));
    converted = r_std_error_from_string(string_error);
    R_TEST_CHECK(converted.domain == R_STD_ERROR_DOMAIN_STRING);
    R_TEST_CHECK(converted.code == UINT32_C(0));
    R_TEST_CHECK(string_error.invalid_index == 37U);
    converted = r_std_error_from_boundary(R_STD_STRING_BOUNDARY_ERROR_NOT_SCALAR_BOUNDARY);
    R_TEST_CHECK(converted.domain == R_STD_ERROR_DOMAIN_STRING);
    R_TEST_CHECK(converted.code == UINT32_C(0x0101));
    converted = r_std_error_from_duration(R_STD_TIME_DURATION_ERROR_OVERFLOW);
    R_TEST_CHECK(converted.domain == R_STD_ERROR_DOMAIN_TIME);
    R_TEST_CHECK(converted.code == UINT32_C(0x0101));
    converted = r_std_error_from_format((RStdFormatError){
        R_STD_FORMAT_ERROR_ALLOCATION_FAILED,
        R_STD_ALLOC_ERROR_OUT_OF_MEMORY,
    });
    R_TEST_CHECK(converted.domain == R_STD_ERROR_DOMAIN_FORMAT);
    R_TEST_CHECK(converted.code == UINT32_C(1));
    converted = r_std_error_from_parse((RStdConvertParseError){
        R_STD_CONVERT_PARSE_ERROR_TRAILING_CHARACTER,
        91U,
    });
    R_TEST_CHECK(converted.domain == R_STD_ERROR_DOMAIN_CONVERSION);
    R_TEST_CHECK(converted.code == UINT32_C(3));
    converted = r_std_error_from_range(R_STD_CONVERT_RANGE_ERROR_INEXACT);
    R_TEST_CHECK(converted.domain == R_STD_ERROR_DOMAIN_CONVERSION);
    R_TEST_CHECK(converted.code == UINT32_C(0x0104));
    converted = r_std_error_from_async(R_STD_ASYNC_START_RUNTIME_STOPPING);
    R_TEST_CHECK(converted.domain == R_STD_ERROR_DOMAIN_ASYNC_RUNTIME);
    R_TEST_CHECK(converted.code == UINT32_C(1));
    R_TEST_CHECK(converted.native_code == INT64_C(0));
    converted = r_std_error_from_path((RStdFsPathError){
        R_STD_FS_PATH_ERROR_ABSOLUTE_COMPONENT,
        12U,
        R_STD_ALLOC_ERROR_SIZE_OVERFLOW,
    });
    R_TEST_CHECK(converted.domain == R_STD_ERROR_DOMAIN_FILESYSTEM);
    R_TEST_CHECK(converted.code == UINT32_C(0x0103));
    R_TEST_CHECK(converted.native_code == INT64_C(0));
    converted = r_std_error_from_address((RStdNetAddressError){
        R_STD_NET_ADDRESS_ERROR_OUT_OF_RANGE,
        27U,
    });
    R_TEST_CHECK(converted.domain == R_STD_ERROR_DOMAIN_NETWORK);
    R_TEST_CHECK(converted.code == UINT32_C(0x0103));
    R_TEST_CHECK(converted.native_code == INT64_C(0));
    converted = r_std_error_from_barrier(R_STD_SYNC_BARRIER_ERROR_ZERO_PARTICIPANTS);
    R_TEST_CHECK(converted.domain == R_STD_ERROR_DOMAIN_THREADING);
    R_TEST_CHECK(converted.code == UINT32_C(0x0100));
    R_TEST_CHECK(converted.native_code == INT64_C(0));
    converted = r_std_error_from_thread(R_STD_THREAD_ERROR_PERMISSION_DENIED);
    R_TEST_CHECK(converted.domain == R_STD_ERROR_DOMAIN_THREADING);
    R_TEST_CHECK(converted.code == UINT32_C(2));
    R_TEST_CHECK(converted.native_code == INT64_C(0));
    return 0;
}

int main(void) {
    R_TEST_CHECK(r_test_names() == 0);
    R_TEST_CHECK(r_test_diagnostic() == 0);
    R_TEST_CHECK(r_test_typed_conversions() == 0);
    (void)fprintf(stdout, "library_error_tests: ok\n");
    return 0;
}

#include "r_std_c.h"

#include "r_runtime_allocator.h"

#include <errno.h>
#include <fenv.h>
#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <xlocale.h>

#define R_TEST_CHECK(expression)                                                                   \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            (void)fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, #expression); \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

#define R_TEST_COUNT(values) (sizeof(values) / sizeof((values)[0]))

typedef struct RTestIntegerType {
    RStdConvertNumericType type;
    _Bool is_signed;
    uint64_t max_positive;
    uint64_t max_negative_magnitude;
} RTestIntegerType;

static const RStdConvertNumericType r_test_all_types[] = {
    R_STD_CONVERT_NUMERIC_TYPE_I8,
    R_STD_CONVERT_NUMERIC_TYPE_U8,
    R_STD_CONVERT_NUMERIC_TYPE_I16,
    R_STD_CONVERT_NUMERIC_TYPE_U16,
    R_STD_CONVERT_NUMERIC_TYPE_I32,
    R_STD_CONVERT_NUMERIC_TYPE_U32,
    R_STD_CONVERT_NUMERIC_TYPE_I64,
    R_STD_CONVERT_NUMERIC_TYPE_U64,
    R_STD_CONVERT_NUMERIC_TYPE_ISIZE,
    R_STD_CONVERT_NUMERIC_TYPE_USIZE,
    R_STD_CONVERT_NUMERIC_TYPE_F32,
    R_STD_CONVERT_NUMERIC_TYPE_F64,
    R_STD_CONVERT_NUMERIC_TYPE_C_CHAR,
    R_STD_CONVERT_NUMERIC_TYPE_C_SCHAR,
    R_STD_CONVERT_NUMERIC_TYPE_C_UCHAR,
    R_STD_CONVERT_NUMERIC_TYPE_C_SHORT,
    R_STD_CONVERT_NUMERIC_TYPE_C_USHORT,
    R_STD_CONVERT_NUMERIC_TYPE_C_INT,
    R_STD_CONVERT_NUMERIC_TYPE_C_UINT,
    R_STD_CONVERT_NUMERIC_TYPE_C_LONG,
    R_STD_CONVERT_NUMERIC_TYPE_C_ULONG,
    R_STD_CONVERT_NUMERIC_TYPE_C_LLONG,
    R_STD_CONVERT_NUMERIC_TYPE_C_ULLONG,
    R_STD_CONVERT_NUMERIC_TYPE_C_BOOL,
    R_STD_CONVERT_NUMERIC_TYPE_C_WCHAR,
    R_STD_CONVERT_NUMERIC_TYPE_C_WINT,
    R_STD_CONVERT_NUMERIC_TYPE_C_INT8,
    R_STD_CONVERT_NUMERIC_TYPE_C_UINT8,
    R_STD_CONVERT_NUMERIC_TYPE_C_INT16,
    R_STD_CONVERT_NUMERIC_TYPE_C_UINT16,
    R_STD_CONVERT_NUMERIC_TYPE_C_INT32,
    R_STD_CONVERT_NUMERIC_TYPE_C_UINT32,
    R_STD_CONVERT_NUMERIC_TYPE_C_INT64,
    R_STD_CONVERT_NUMERIC_TYPE_C_UINT64,
    R_STD_CONVERT_NUMERIC_TYPE_C_INTPTR,
    R_STD_CONVERT_NUMERIC_TYPE_C_UINTPTR,
    R_STD_CONVERT_NUMERIC_TYPE_C_INTMAX,
    R_STD_CONVERT_NUMERIC_TYPE_C_UINTMAX,
    R_STD_CONVERT_NUMERIC_TYPE_C_FLOAT,
    R_STD_CONVERT_NUMERIC_TYPE_C_DOUBLE,
    R_STD_CONVERT_NUMERIC_TYPE_C_LONG_DOUBLE,
    R_STD_CONVERT_NUMERIC_TYPE_C_SIZE,
    R_STD_CONVERT_NUMERIC_TYPE_C_PTRDIFF,
};

static const RStdConvertNumericType r_test_c_types[] = {
    R_STD_CONVERT_NUMERIC_TYPE_C_CHAR,        R_STD_CONVERT_NUMERIC_TYPE_C_SCHAR,
    R_STD_CONVERT_NUMERIC_TYPE_C_UCHAR,       R_STD_CONVERT_NUMERIC_TYPE_C_SHORT,
    R_STD_CONVERT_NUMERIC_TYPE_C_USHORT,      R_STD_CONVERT_NUMERIC_TYPE_C_INT,
    R_STD_CONVERT_NUMERIC_TYPE_C_UINT,        R_STD_CONVERT_NUMERIC_TYPE_C_LONG,
    R_STD_CONVERT_NUMERIC_TYPE_C_ULONG,       R_STD_CONVERT_NUMERIC_TYPE_C_LLONG,
    R_STD_CONVERT_NUMERIC_TYPE_C_ULLONG,      R_STD_CONVERT_NUMERIC_TYPE_C_BOOL,
    R_STD_CONVERT_NUMERIC_TYPE_C_WCHAR,       R_STD_CONVERT_NUMERIC_TYPE_C_WINT,
    R_STD_CONVERT_NUMERIC_TYPE_C_INT8,        R_STD_CONVERT_NUMERIC_TYPE_C_UINT8,
    R_STD_CONVERT_NUMERIC_TYPE_C_INT16,       R_STD_CONVERT_NUMERIC_TYPE_C_UINT16,
    R_STD_CONVERT_NUMERIC_TYPE_C_INT32,       R_STD_CONVERT_NUMERIC_TYPE_C_UINT32,
    R_STD_CONVERT_NUMERIC_TYPE_C_INT64,       R_STD_CONVERT_NUMERIC_TYPE_C_UINT64,
    R_STD_CONVERT_NUMERIC_TYPE_C_INTPTR,      R_STD_CONVERT_NUMERIC_TYPE_C_UINTPTR,
    R_STD_CONVERT_NUMERIC_TYPE_C_INTMAX,      R_STD_CONVERT_NUMERIC_TYPE_C_UINTMAX,
    R_STD_CONVERT_NUMERIC_TYPE_C_FLOAT,       R_STD_CONVERT_NUMERIC_TYPE_C_DOUBLE,
    R_STD_CONVERT_NUMERIC_TYPE_C_LONG_DOUBLE, R_STD_CONVERT_NUMERIC_TYPE_C_SIZE,
    R_STD_CONVERT_NUMERIC_TYPE_C_PTRDIFF,
};

static const RTestIntegerType r_test_integer_types[] = {
    {R_STD_CONVERT_NUMERIC_TYPE_I8, 1, UINT64_C(127), UINT64_C(128)},
    {R_STD_CONVERT_NUMERIC_TYPE_U8, 0, UINT64_C(255), UINT64_C(0)},
    {R_STD_CONVERT_NUMERIC_TYPE_I16, 1, UINT64_C(32767), UINT64_C(32768)},
    {R_STD_CONVERT_NUMERIC_TYPE_U16, 0, UINT64_C(65535), UINT64_C(0)},
    {R_STD_CONVERT_NUMERIC_TYPE_I32, 1, UINT64_C(2147483647), UINT64_C(2147483648)},
    {R_STD_CONVERT_NUMERIC_TYPE_U32, 0, UINT64_C(4294967295), UINT64_C(0)},
    {R_STD_CONVERT_NUMERIC_TYPE_I64,
     1,
     UINT64_C(9223372036854775807),
     UINT64_C(9223372036854775808)},
    {R_STD_CONVERT_NUMERIC_TYPE_U64, 0, UINT64_MAX, UINT64_C(0)},
    {R_STD_CONVERT_NUMERIC_TYPE_ISIZE,
     1,
     UINT64_C(9223372036854775807),
     UINT64_C(9223372036854775808)},
    {R_STD_CONVERT_NUMERIC_TYPE_USIZE, 0, UINT64_MAX, UINT64_C(0)},
    {R_STD_CONVERT_NUMERIC_TYPE_C_CHAR, 1, UINT64_C(127), UINT64_C(128)},
    {R_STD_CONVERT_NUMERIC_TYPE_C_SCHAR, 1, UINT64_C(127), UINT64_C(128)},
    {R_STD_CONVERT_NUMERIC_TYPE_C_UCHAR, 0, UINT64_C(255), UINT64_C(0)},
    {R_STD_CONVERT_NUMERIC_TYPE_C_SHORT, 1, UINT64_C(32767), UINT64_C(32768)},
    {R_STD_CONVERT_NUMERIC_TYPE_C_USHORT, 0, UINT64_C(65535), UINT64_C(0)},
    {R_STD_CONVERT_NUMERIC_TYPE_C_INT, 1, UINT64_C(2147483647), UINT64_C(2147483648)},
    {R_STD_CONVERT_NUMERIC_TYPE_C_UINT, 0, UINT64_C(4294967295), UINT64_C(0)},
    {R_STD_CONVERT_NUMERIC_TYPE_C_LONG,
     1,
     UINT64_C(9223372036854775807),
     UINT64_C(9223372036854775808)},
    {R_STD_CONVERT_NUMERIC_TYPE_C_ULONG, 0, UINT64_MAX, UINT64_C(0)},
    {R_STD_CONVERT_NUMERIC_TYPE_C_LLONG,
     1,
     UINT64_C(9223372036854775807),
     UINT64_C(9223372036854775808)},
    {R_STD_CONVERT_NUMERIC_TYPE_C_ULLONG, 0, UINT64_MAX, UINT64_C(0)},
    {R_STD_CONVERT_NUMERIC_TYPE_C_BOOL, 0, UINT64_C(1), UINT64_C(0)},
    {R_STD_CONVERT_NUMERIC_TYPE_C_WCHAR, 1, UINT64_C(2147483647), UINT64_C(2147483648)},
    {R_STD_CONVERT_NUMERIC_TYPE_C_WINT, 1, UINT64_C(2147483647), UINT64_C(2147483648)},
    {R_STD_CONVERT_NUMERIC_TYPE_C_INT8, 1, UINT64_C(127), UINT64_C(128)},
    {R_STD_CONVERT_NUMERIC_TYPE_C_UINT8, 0, UINT64_C(255), UINT64_C(0)},
    {R_STD_CONVERT_NUMERIC_TYPE_C_INT16, 1, UINT64_C(32767), UINT64_C(32768)},
    {R_STD_CONVERT_NUMERIC_TYPE_C_UINT16, 0, UINT64_C(65535), UINT64_C(0)},
    {R_STD_CONVERT_NUMERIC_TYPE_C_INT32, 1, UINT64_C(2147483647), UINT64_C(2147483648)},
    {R_STD_CONVERT_NUMERIC_TYPE_C_UINT32, 0, UINT64_C(4294967295), UINT64_C(0)},
    {R_STD_CONVERT_NUMERIC_TYPE_C_INT64,
     1,
     UINT64_C(9223372036854775807),
     UINT64_C(9223372036854775808)},
    {R_STD_CONVERT_NUMERIC_TYPE_C_UINT64, 0, UINT64_MAX, UINT64_C(0)},
    {R_STD_CONVERT_NUMERIC_TYPE_C_INTPTR,
     1,
     UINT64_C(9223372036854775807),
     UINT64_C(9223372036854775808)},
    {R_STD_CONVERT_NUMERIC_TYPE_C_UINTPTR, 0, UINT64_MAX, UINT64_C(0)},
    {R_STD_CONVERT_NUMERIC_TYPE_C_INTMAX,
     1,
     UINT64_C(9223372036854775807),
     UINT64_C(9223372036854775808)},
    {R_STD_CONVERT_NUMERIC_TYPE_C_UINTMAX, 0, UINT64_MAX, UINT64_C(0)},
    {R_STD_CONVERT_NUMERIC_TYPE_C_SIZE, 0, UINT64_MAX, UINT64_C(0)},
    {R_STD_CONVERT_NUMERIC_TYPE_C_PTRDIFF,
     1,
     UINT64_C(9223372036854775807),
     UINT64_C(9223372036854775808)},
};

_Static_assert(R_TEST_COUNT(r_test_all_types) == 43U, "closed source/destination count changed");
_Static_assert(R_TEST_COUNT(r_test_c_types) == 31U, "closed C destination count changed");
_Static_assert(R_TEST_COUNT(r_test_integer_types) == 38U, "closed integer type count changed");

static RStdConvertNumericValue r_test_signed(RStdConvertNumericType type, int64_t value) {
    RStdConvertNumericValue source = {0};

    source.type = type;
    source.payload.signed_integer = value;
    return source;
}

static RStdConvertNumericValue r_test_unsigned(RStdConvertNumericType type, uint64_t value) {
    RStdConvertNumericValue source = {0};

    source.type = type;
    source.payload.unsigned_integer = value;
    return source;
}

static RStdConvertNumericValue r_test_binary32(RStdConvertNumericType type, uint32_t bits) {
    RStdConvertNumericValue source = {0};

    source.type = type;
    source.payload.binary32_bits = bits;
    return source;
}

static RStdConvertNumericValue r_test_binary64(RStdConvertNumericType type, uint64_t bits) {
    RStdConvertNumericValue source = {0};

    source.type = type;
    source.payload.binary64_bits = bits;
    return source;
}

static RStdConvertDestination r_test_destination(RStdConvertNumericType type) {
    RStdConvertDestination destination = {type};

    return destination;
}

static _Bool r_test_is_binary32(RStdConvertNumericType type) {
    return type == R_STD_CONVERT_NUMERIC_TYPE_F32 || type == R_STD_CONVERT_NUMERIC_TYPE_C_FLOAT;
}

static _Bool r_test_is_binary64(RStdConvertNumericType type) {
    return type == R_STD_CONVERT_NUMERIC_TYPE_F64 || type == R_STD_CONVERT_NUMERIC_TYPE_C_DOUBLE ||
           type == R_STD_CONVERT_NUMERIC_TYPE_C_LONG_DOUBLE;
}

static const RTestIntegerType *r_test_integer_descriptor(RStdConvertNumericType type) {
    size_t index;

    for (index = 0U; index < R_TEST_COUNT(r_test_integer_types); index += 1U) {
        if (r_test_integer_types[index].type == type) {
            return &r_test_integer_types[index];
        }
    }
    return NULL;
}

static int r_test_expect_error(RStdConvertNumericValue source,
                               RStdConvertNumericType destination,
                               RStdConvertRangeError error) {
    RStdConvertCheckedResult result =
        r_std_convert_checked(source, r_test_destination(destination));

    R_TEST_CHECK(result.status == R_STD_CONVERT_CALL_ERROR);
    R_TEST_CHECK(result.error == error);
    return 0;
}

static int r_test_zero_matrices(void) {
    size_t source_index;
    size_t destination_index;
    size_t convert_edges = 0U;
    size_t c_edges = 0U;

    for (source_index = 0U; source_index < R_TEST_COUNT(r_test_all_types); source_index += 1U) {
        RStdConvertNumericValue source = {0};

        source.type = r_test_all_types[source_index];
        for (destination_index = 0U; destination_index < R_TEST_COUNT(r_test_all_types);
             destination_index += 1U) {
            RStdConvertNumericType destination_type = r_test_all_types[destination_index];
            RStdConvertCheckedResult result =
                r_std_convert_checked(source, r_test_destination(destination_type));
            const RTestIntegerType *integer = r_test_integer_descriptor(destination_type);

            R_TEST_CHECK(result.status == R_STD_CONVERT_CALL_SUCCESS);
            R_TEST_CHECK(result.value.type == destination_type);
            if (integer != NULL && integer->is_signed) {
                R_TEST_CHECK(result.value.payload.signed_integer == INT64_C(0));
            } else if (integer != NULL) {
                R_TEST_CHECK(result.value.payload.unsigned_integer == UINT64_C(0));
            } else if (r_test_is_binary32(destination_type)) {
                R_TEST_CHECK(result.value.payload.binary32_bits == UINT32_C(0));
            } else {
                R_TEST_CHECK(r_test_is_binary64(destination_type));
                R_TEST_CHECK(result.value.payload.binary64_bits == UINT64_C(0));
            }
            convert_edges += 1U;
        }
        for (destination_index = 0U; destination_index < R_TEST_COUNT(r_test_c_types);
             destination_index += 1U) {
            RStdCCheckedResult result =
                r_std_c_checked(source, r_test_destination(r_test_c_types[destination_index]));

            R_TEST_CHECK(result.status == R_STD_CONVERT_CALL_SUCCESS);
            R_TEST_CHECK(result.value.type == r_test_c_types[destination_index]);
            c_edges += 1U;
        }
    }
    R_TEST_CHECK(convert_edges == 1849U);
    R_TEST_CHECK(c_edges == 1333U);
    return 0;
}

static int r_test_integer_boundaries(void) {
    size_t index;

    for (index = 0U; index < R_TEST_COUNT(r_test_integer_types); index += 1U) {
        const RTestIntegerType *integer = &r_test_integer_types[index];
        RStdConvertCheckedResult result = r_std_convert_checked(
            r_test_unsigned(R_STD_CONVERT_NUMERIC_TYPE_U64, integer->max_positive),
            r_test_destination(integer->type));

        R_TEST_CHECK(result.status == R_STD_CONVERT_CALL_SUCCESS);
        if (integer->is_signed) {
            int64_t minimum = integer->max_negative_magnitude == UINT64_C(9223372036854775808)
                                  ? INT64_MIN
                                  : -(int64_t)integer->max_negative_magnitude;

            R_TEST_CHECK(result.value.payload.signed_integer == (int64_t)integer->max_positive);
            result = r_std_convert_checked(r_test_signed(R_STD_CONVERT_NUMERIC_TYPE_I64, minimum),
                                           r_test_destination(integer->type));
            R_TEST_CHECK(result.status == R_STD_CONVERT_CALL_SUCCESS);
            R_TEST_CHECK(result.value.payload.signed_integer == minimum);
            if (integer->max_positive != UINT64_C(9223372036854775807)) {
                R_TEST_CHECK(
                    r_test_expect_error(r_test_unsigned(R_STD_CONVERT_NUMERIC_TYPE_U64,
                                                        integer->max_positive + UINT64_C(1)),
                                        integer->type,
                                        R_STD_CONVERT_RANGE_ERROR_ABOVE_MAXIMUM) == 0);
                R_TEST_CHECK(r_test_expect_error(r_test_signed(R_STD_CONVERT_NUMERIC_TYPE_I64,
                                                               minimum - INT64_C(1)),
                                                 integer->type,
                                                 R_STD_CONVERT_RANGE_ERROR_BELOW_MINIMUM) == 0);
            } else {
                R_TEST_CHECK(r_test_expect_error(r_test_binary64(R_STD_CONVERT_NUMERIC_TYPE_F64,
                                                                 UINT64_C(0x43e0000000000000)),
                                                 integer->type,
                                                 R_STD_CONVERT_RANGE_ERROR_ABOVE_MAXIMUM) == 0);
                R_TEST_CHECK(r_test_expect_error(r_test_binary64(R_STD_CONVERT_NUMERIC_TYPE_F64,
                                                                 UINT64_C(0xc3f0000000000000)),
                                                 integer->type,
                                                 R_STD_CONVERT_RANGE_ERROR_BELOW_MINIMUM) == 0);
            }
        } else {
            R_TEST_CHECK(result.value.payload.unsigned_integer == integer->max_positive);
            R_TEST_CHECK(r_test_expect_error(r_test_signed(R_STD_CONVERT_NUMERIC_TYPE_I64, -1),
                                             integer->type,
                                             R_STD_CONVERT_RANGE_ERROR_BELOW_MINIMUM) == 0);
            if (integer->max_positive != UINT64_MAX) {
                R_TEST_CHECK(
                    r_test_expect_error(r_test_unsigned(R_STD_CONVERT_NUMERIC_TYPE_U64,
                                                        integer->max_positive + UINT64_C(1)),
                                        integer->type,
                                        R_STD_CONVERT_RANGE_ERROR_ABOVE_MAXIMUM) == 0);
            } else {
                R_TEST_CHECK(r_test_expect_error(r_test_binary64(R_STD_CONVERT_NUMERIC_TYPE_F64,
                                                                 UINT64_C(0x43f0000000000000)),
                                                 integer->type,
                                                 R_STD_CONVERT_RANGE_ERROR_ABOVE_MAXIMUM) == 0);
            }
        }
    }
    return 0;
}

static int r_test_integer_precedence(void) {
    RStdConvertCheckedResult result;

    R_TEST_CHECK(r_test_expect_error(
                     r_test_binary64(R_STD_CONVERT_NUMERIC_TYPE_F64, UINT64_C(0x7ff0000000000001)),
                     R_STD_CONVERT_NUMERIC_TYPE_I8,
                     R_STD_CONVERT_RANGE_ERROR_NOT_FINITE) == 0);
    R_TEST_CHECK(r_test_expect_error(
                     r_test_binary64(R_STD_CONVERT_NUMERIC_TYPE_F64, UINT64_C(0xfff0000000000000)),
                     R_STD_CONVERT_NUMERIC_TYPE_I8,
                     R_STD_CONVERT_RANGE_ERROR_NOT_FINITE) == 0);
    R_TEST_CHECK(r_test_expect_error(
                     r_test_binary64(R_STD_CONVERT_NUMERIC_TYPE_F64, UINT64_C(0xbfe0000000000000)),
                     R_STD_CONVERT_NUMERIC_TYPE_U8,
                     R_STD_CONVERT_RANGE_ERROR_BELOW_MINIMUM) == 0);
    R_TEST_CHECK(r_test_expect_error(
                     r_test_binary64(R_STD_CONVERT_NUMERIC_TYPE_F64, UINT64_C(0x406ff00000000000)),
                     R_STD_CONVERT_NUMERIC_TYPE_U8,
                     R_STD_CONVERT_RANGE_ERROR_ABOVE_MAXIMUM) == 0);
    R_TEST_CHECK(r_test_expect_error(
                     r_test_binary64(R_STD_CONVERT_NUMERIC_TYPE_F64, UINT64_C(0xc060100000000000)),
                     R_STD_CONVERT_NUMERIC_TYPE_I8,
                     R_STD_CONVERT_RANGE_ERROR_BELOW_MINIMUM) == 0);
    R_TEST_CHECK(r_test_expect_error(
                     r_test_binary64(R_STD_CONVERT_NUMERIC_TYPE_F64, UINT64_C(0x405fe00000000000)),
                     R_STD_CONVERT_NUMERIC_TYPE_I8,
                     R_STD_CONVERT_RANGE_ERROR_ABOVE_MAXIMUM) == 0);
    R_TEST_CHECK(r_test_expect_error(
                     r_test_binary64(R_STD_CONVERT_NUMERIC_TYPE_F64, UINT64_C(0x3ff8000000000000)),
                     R_STD_CONVERT_NUMERIC_TYPE_I8,
                     R_STD_CONVERT_RANGE_ERROR_FRACTIONAL) == 0);
    R_TEST_CHECK(r_test_expect_error(
                     r_test_binary64(R_STD_CONVERT_NUMERIC_TYPE_F64, UINT64_C(0xbff8000000000000)),
                     R_STD_CONVERT_NUMERIC_TYPE_I8,
                     R_STD_CONVERT_RANGE_ERROR_FRACTIONAL) == 0);
    R_TEST_CHECK(r_test_expect_error(
                     r_test_binary64(R_STD_CONVERT_NUMERIC_TYPE_F64, UINT64_C(0xc05fe00000000000)),
                     R_STD_CONVERT_NUMERIC_TYPE_I8,
                     R_STD_CONVERT_RANGE_ERROR_FRACTIONAL) == 0);
    result = r_std_convert_checked(
        r_test_binary64(R_STD_CONVERT_NUMERIC_TYPE_F64, UINT64_C(0x8000000000000000)),
        r_test_destination(R_STD_CONVERT_NUMERIC_TYPE_U8));
    R_TEST_CHECK(result.status == R_STD_CONVERT_CALL_SUCCESS);
    R_TEST_CHECK(result.value.payload.unsigned_integer == UINT64_C(0));
    result =
        r_std_convert_checked(r_test_binary32(R_STD_CONVERT_NUMERIC_TYPE_F32, UINT32_C(0x42280000)),
                              r_test_destination(R_STD_CONVERT_NUMERIC_TYPE_I8));
    R_TEST_CHECK(result.status == R_STD_CONVERT_CALL_SUCCESS);
    R_TEST_CHECK(result.value.payload.signed_integer == INT64_C(42));
    return 0;
}

static int r_test_integer_to_float(void) {
    RStdConvertCheckedResult result =
        r_std_convert_checked(r_test_unsigned(R_STD_CONVERT_NUMERIC_TYPE_U64, UINT64_C(16777216)),
                              r_test_destination(R_STD_CONVERT_NUMERIC_TYPE_F32));

    R_TEST_CHECK(result.status == R_STD_CONVERT_CALL_SUCCESS);
    R_TEST_CHECK(result.value.payload.binary32_bits == UINT32_C(0x4b800000));
    R_TEST_CHECK(
        r_test_expect_error(r_test_unsigned(R_STD_CONVERT_NUMERIC_TYPE_U64, UINT64_C(16777217)),
                            R_STD_CONVERT_NUMERIC_TYPE_F32,
                            R_STD_CONVERT_RANGE_ERROR_INEXACT) == 0);
    result =
        r_std_convert_checked(r_test_unsigned(R_STD_CONVERT_NUMERIC_TYPE_U64, UINT64_C(16777218)),
                              r_test_destination(R_STD_CONVERT_NUMERIC_TYPE_F32));
    R_TEST_CHECK(result.status == R_STD_CONVERT_CALL_SUCCESS);
    R_TEST_CHECK(result.value.payload.binary32_bits == UINT32_C(0x4b800001));
    result =
        r_std_convert_checked(r_test_unsigned(R_STD_CONVERT_NUMERIC_TYPE_U64, UINT64_C(16777215)),
                              r_test_destination(R_STD_CONVERT_NUMERIC_TYPE_F32));
    R_TEST_CHECK(result.status == R_STD_CONVERT_CALL_SUCCESS);
    R_TEST_CHECK(result.value.payload.binary32_bits == UINT32_C(0x4b7fffff));

    result = r_std_convert_checked(
        r_test_unsigned(R_STD_CONVERT_NUMERIC_TYPE_U64, UINT64_C(9007199254740992)),
        r_test_destination(R_STD_CONVERT_NUMERIC_TYPE_F64));
    R_TEST_CHECK(result.status == R_STD_CONVERT_CALL_SUCCESS);
    R_TEST_CHECK(result.value.payload.binary64_bits == UINT64_C(0x4340000000000000));
    R_TEST_CHECK(r_test_expect_error(
                     r_test_unsigned(R_STD_CONVERT_NUMERIC_TYPE_U64, UINT64_C(9007199254740993)),
                     R_STD_CONVERT_NUMERIC_TYPE_F64,
                     R_STD_CONVERT_RANGE_ERROR_INEXACT) == 0);
    result = r_std_convert_checked(
        r_test_unsigned(R_STD_CONVERT_NUMERIC_TYPE_U64, UINT64_C(9007199254740991)),
        r_test_destination(R_STD_CONVERT_NUMERIC_TYPE_F64));
    R_TEST_CHECK(result.status == R_STD_CONVERT_CALL_SUCCESS);
    R_TEST_CHECK(result.value.payload.binary64_bits == UINT64_C(0x433fffffffffffff));
    result = r_std_convert_checked(
        r_test_unsigned(R_STD_CONVERT_NUMERIC_TYPE_U64, UINT64_C(9007199254740994)),
        r_test_destination(R_STD_CONVERT_NUMERIC_TYPE_F64));
    R_TEST_CHECK(result.status == R_STD_CONVERT_CALL_SUCCESS);
    R_TEST_CHECK(result.value.payload.binary64_bits == UINT64_C(0x4340000000000001));
    R_TEST_CHECK(r_test_expect_error(r_test_unsigned(R_STD_CONVERT_NUMERIC_TYPE_U64, UINT64_MAX),
                                     R_STD_CONVERT_NUMERIC_TYPE_F64,
                                     R_STD_CONVERT_RANGE_ERROR_INEXACT) == 0);
    R_TEST_CHECK(r_test_expect_error(r_test_unsigned(R_STD_CONVERT_NUMERIC_TYPE_U64, UINT64_MAX),
                                     R_STD_CONVERT_NUMERIC_TYPE_F32,
                                     R_STD_CONVERT_RANGE_ERROR_INEXACT) == 0);
    R_TEST_CHECK(r_test_expect_error(r_test_signed(R_STD_CONVERT_NUMERIC_TYPE_I64, INT64_MAX),
                                     R_STD_CONVERT_NUMERIC_TYPE_F64,
                                     R_STD_CONVERT_RANGE_ERROR_INEXACT) == 0);
    R_TEST_CHECK(r_test_expect_error(r_test_signed(R_STD_CONVERT_NUMERIC_TYPE_I64, INT64_MAX),
                                     R_STD_CONVERT_NUMERIC_TYPE_F32,
                                     R_STD_CONVERT_RANGE_ERROR_INEXACT) == 0);
    result = r_std_convert_checked(r_test_signed(R_STD_CONVERT_NUMERIC_TYPE_I64, INT64_MIN),
                                   r_test_destination(R_STD_CONVERT_NUMERIC_TYPE_F32));
    R_TEST_CHECK(result.status == R_STD_CONVERT_CALL_SUCCESS);
    R_TEST_CHECK(result.value.payload.binary32_bits == UINT32_C(0xdf000000));
    return 0;
}

static int r_test_float_to_float(void) {
    RStdConvertCheckedResult result =
        r_std_convert_checked(r_test_binary32(R_STD_CONVERT_NUMERIC_TYPE_F32, UINT32_C(0x3fc00000)),
                              r_test_destination(R_STD_CONVERT_NUMERIC_TYPE_F64));

    R_TEST_CHECK(result.status == R_STD_CONVERT_CALL_SUCCESS);
    R_TEST_CHECK(result.value.payload.binary64_bits == UINT64_C(0x3ff8000000000000));
    result = r_std_convert_checked(
        r_test_binary64(R_STD_CONVERT_NUMERIC_TYPE_F64, UINT64_C(0x3ff8000000000000)),
        r_test_destination(R_STD_CONVERT_NUMERIC_TYPE_F32));
    R_TEST_CHECK(result.status == R_STD_CONVERT_CALL_SUCCESS);
    R_TEST_CHECK(result.value.payload.binary32_bits == UINT32_C(0x3fc00000));
    R_TEST_CHECK(r_test_expect_error(
                     r_test_binary64(R_STD_CONVERT_NUMERIC_TYPE_F64, UINT64_C(0x3ff0000000000001)),
                     R_STD_CONVERT_NUMERIC_TYPE_F32,
                     R_STD_CONVERT_RANGE_ERROR_INEXACT) == 0);

    result = r_std_convert_checked(
        r_test_binary64(R_STD_CONVERT_NUMERIC_TYPE_F64, UINT64_C(0x47efffffe0000000)),
        r_test_destination(R_STD_CONVERT_NUMERIC_TYPE_F32));
    R_TEST_CHECK(result.status == R_STD_CONVERT_CALL_SUCCESS);
    R_TEST_CHECK(result.value.payload.binary32_bits == UINT32_C(0x7f7fffff));
    R_TEST_CHECK(r_test_expect_error(
                     r_test_binary64(R_STD_CONVERT_NUMERIC_TYPE_F64, UINT64_C(0x47f0000000000000)),
                     R_STD_CONVERT_NUMERIC_TYPE_F32,
                     R_STD_CONVERT_RANGE_ERROR_ABOVE_MAXIMUM) == 0);
    R_TEST_CHECK(r_test_expect_error(
                     r_test_binary64(R_STD_CONVERT_NUMERIC_TYPE_F64, UINT64_C(0xc7f0000000000000)),
                     R_STD_CONVERT_NUMERIC_TYPE_F32,
                     R_STD_CONVERT_RANGE_ERROR_BELOW_MINIMUM) == 0);
    R_TEST_CHECK(r_test_expect_error(
                     r_test_binary64(R_STD_CONVERT_NUMERIC_TYPE_F64, UINT64_C(0x47efffffe0000001)),
                     R_STD_CONVERT_NUMERIC_TYPE_F32,
                     R_STD_CONVERT_RANGE_ERROR_ABOVE_MAXIMUM) == 0);
    R_TEST_CHECK(r_test_expect_error(
                     r_test_binary64(R_STD_CONVERT_NUMERIC_TYPE_F64, UINT64_C(0xc7efffffe0000001)),
                     R_STD_CONVERT_NUMERIC_TYPE_F32,
                     R_STD_CONVERT_RANGE_ERROR_BELOW_MINIMUM) == 0);
    R_TEST_CHECK(r_test_expect_error(
                     r_test_binary64(R_STD_CONVERT_NUMERIC_TYPE_F64, UINT64_C(0x3ff0000010000000)),
                     R_STD_CONVERT_NUMERIC_TYPE_F32,
                     R_STD_CONVERT_RANGE_ERROR_INEXACT) == 0);

    result = r_std_convert_checked(
        r_test_binary64(R_STD_CONVERT_NUMERIC_TYPE_F64, UINT64_C(0x36a0000000000000)),
        r_test_destination(R_STD_CONVERT_NUMERIC_TYPE_F32));
    R_TEST_CHECK(result.status == R_STD_CONVERT_CALL_SUCCESS);
    R_TEST_CHECK(result.value.payload.binary32_bits == UINT32_C(0x00000001));
    R_TEST_CHECK(r_test_expect_error(
                     r_test_binary64(R_STD_CONVERT_NUMERIC_TYPE_F64, UINT64_C(0x3690000000000000)),
                     R_STD_CONVERT_NUMERIC_TYPE_F32,
                     R_STD_CONVERT_RANGE_ERROR_INEXACT) == 0);
    R_TEST_CHECK(r_test_expect_error(
                     r_test_binary64(R_STD_CONVERT_NUMERIC_TYPE_F64, UINT64_C(0x0000000000000001)),
                     R_STD_CONVERT_NUMERIC_TYPE_F32,
                     R_STD_CONVERT_RANGE_ERROR_INEXACT) == 0);
    result = r_std_convert_checked(
        r_test_binary64(R_STD_CONVERT_NUMERIC_TYPE_F64, UINT64_C(0x0000000000000001)),
        r_test_destination(R_STD_CONVERT_NUMERIC_TYPE_F64));
    R_TEST_CHECK(result.status == R_STD_CONVERT_CALL_SUCCESS);
    R_TEST_CHECK(result.value.payload.binary64_bits == UINT64_C(0x0000000000000001));

    result = r_std_convert_checked(
        r_test_binary64(R_STD_CONVERT_NUMERIC_TYPE_F64, UINT64_C(0x8000000000000000)),
        r_test_destination(R_STD_CONVERT_NUMERIC_TYPE_F32));
    R_TEST_CHECK(result.status == R_STD_CONVERT_CALL_SUCCESS);
    R_TEST_CHECK(result.value.payload.binary32_bits == UINT32_C(0x80000000));
    result = r_std_convert_checked(
        r_test_binary64(R_STD_CONVERT_NUMERIC_TYPE_F64, UINT64_C(0x7ff0000000000001)),
        r_test_destination(R_STD_CONVERT_NUMERIC_TYPE_F32));
    R_TEST_CHECK(result.status == R_STD_CONVERT_CALL_SUCCESS);
    R_TEST_CHECK(result.value.payload.binary32_bits == UINT32_C(0x7fc00000));
    result = r_std_convert_checked(
        r_test_binary32(R_STD_CONVERT_NUMERIC_TYPE_C_FLOAT, UINT32_C(0x7f800001)),
        r_test_destination(R_STD_CONVERT_NUMERIC_TYPE_C_LONG_DOUBLE));
    R_TEST_CHECK(result.status == R_STD_CONVERT_CALL_SUCCESS);
    R_TEST_CHECK(result.value.payload.binary64_bits == UINT64_C(0x7ff8000000000000));
    result = r_std_convert_checked(
        r_test_binary64(R_STD_CONVERT_NUMERIC_TYPE_C_DOUBLE, UINT64_C(0xfff0000000000000)),
        r_test_destination(R_STD_CONVERT_NUMERIC_TYPE_C_FLOAT));
    R_TEST_CHECK(result.status == R_STD_CONVERT_CALL_SUCCESS);
    R_TEST_CHECK(result.value.payload.binary32_bits == UINT32_C(0xff800000));
    result =
        r_std_convert_checked(r_test_binary32(R_STD_CONVERT_NUMERIC_TYPE_F32, UINT32_C(0xc248f5c3)),
                              r_test_destination(R_STD_CONVERT_NUMERIC_TYPE_F32));
    R_TEST_CHECK(result.status == R_STD_CONVERT_CALL_SUCCESS);
    R_TEST_CHECK(result.value.payload.binary32_bits == UINT32_C(0xc248f5c3));
    result = r_std_convert_checked(
        r_test_binary64(R_STD_CONVERT_NUMERIC_TYPE_F64, UINT64_C(0x0123456789abcdef)),
        r_test_destination(R_STD_CONVERT_NUMERIC_TYPE_F64));
    R_TEST_CHECK(result.status == R_STD_CONVERT_CALL_SUCCESS);
    R_TEST_CHECK(result.value.payload.binary64_bits == UINT64_C(0x0123456789abcdef));
    result = r_std_convert_checked(
        r_test_binary64(R_STD_CONVERT_NUMERIC_TYPE_F64, UINT64_C(0xfff0000000000001)),
        r_test_destination(R_STD_CONVERT_NUMERIC_TYPE_F64));
    R_TEST_CHECK(result.status == R_STD_CONVERT_CALL_SUCCESS);
    R_TEST_CHECK(result.value.payload.binary64_bits == UINT64_C(0x7ff8000000000000));
    return 0;
}

static int r_test_float_to_wide_integer_boundaries(void) {
    RStdConvertCheckedResult result = r_std_convert_checked(
        r_test_binary64(R_STD_CONVERT_NUMERIC_TYPE_F64, UINT64_C(0xc3e0000000000000)),
        r_test_destination(R_STD_CONVERT_NUMERIC_TYPE_I64));

    R_TEST_CHECK(result.status == R_STD_CONVERT_CALL_SUCCESS);
    R_TEST_CHECK(result.value.payload.signed_integer == INT64_MIN);
    R_TEST_CHECK(r_test_expect_error(
                     r_test_binary64(R_STD_CONVERT_NUMERIC_TYPE_F64, UINT64_C(0xc3e0000000000001)),
                     R_STD_CONVERT_NUMERIC_TYPE_I64,
                     R_STD_CONVERT_RANGE_ERROR_BELOW_MINIMUM) == 0);
    result = r_std_convert_checked(
        r_test_binary64(R_STD_CONVERT_NUMERIC_TYPE_F64, UINT64_C(0x43dfffffffffffff)),
        r_test_destination(R_STD_CONVERT_NUMERIC_TYPE_I64));
    R_TEST_CHECK(result.status == R_STD_CONVERT_CALL_SUCCESS);
    R_TEST_CHECK(result.value.payload.signed_integer == INT64_C(9223372036854774784));
    R_TEST_CHECK(r_test_expect_error(
                     r_test_binary64(R_STD_CONVERT_NUMERIC_TYPE_F64, UINT64_C(0x43e0000000000000)),
                     R_STD_CONVERT_NUMERIC_TYPE_I64,
                     R_STD_CONVERT_RANGE_ERROR_ABOVE_MAXIMUM) == 0);
    result = r_std_convert_checked(
        r_test_binary64(R_STD_CONVERT_NUMERIC_TYPE_F64, UINT64_C(0x43efffffffffffff)),
        r_test_destination(R_STD_CONVERT_NUMERIC_TYPE_U64));
    R_TEST_CHECK(result.status == R_STD_CONVERT_CALL_SUCCESS);
    R_TEST_CHECK(result.value.payload.unsigned_integer == UINT64_C(18446744073709549568));
    return 0;
}

static int r_test_contract_and_allocator(void) {
    RRuntimeAllocator allocator;
    RStdConvertCheckedResult result;

    r_runtime_allocator_initialize(&allocator);
    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    result = r_std_convert_checked(r_test_unsigned(R_STD_CONVERT_NUMERIC_TYPE_U64, UINT64_C(42)),
                                   r_test_destination(R_STD_CONVERT_NUMERIC_TYPE_I8));
    R_TEST_CHECK(result.status == R_STD_CONVERT_CALL_SUCCESS);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(0));
    return 0;
}

static int r_test_locale_errno_and_fenv(void) {
    fenv_t original_environment;
    locale_t numeric_locale;
    locale_t original_locale;
    RStdConvertCheckedResult result;
    int expected_exceptions = FE_INVALID | FE_DIVBYZERO;

    R_TEST_CHECK(fegetenv(&original_environment) == 0);
    numeric_locale = newlocale(LC_NUMERIC_MASK, "fr_FR.UTF-8", NULL);
    R_TEST_CHECK(numeric_locale != NULL);
    original_locale = uselocale(numeric_locale);
    R_TEST_CHECK(original_locale != (locale_t)0);
    R_TEST_CHECK(fesetround(FE_UPWARD) == 0);
    R_TEST_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    R_TEST_CHECK(feraiseexcept(expected_exceptions) == 0);
    errno = EDOM;

    result = r_std_convert_checked(
        r_test_binary64(R_STD_CONVERT_NUMERIC_TYPE_F64, UINT64_C(0x7ff0000000000001)),
        r_test_destination(R_STD_CONVERT_NUMERIC_TYPE_F32));
    R_TEST_CHECK(result.status == R_STD_CONVERT_CALL_SUCCESS);
    R_TEST_CHECK(result.value.payload.binary32_bits == UINT32_C(0x7fc00000));
    R_TEST_CHECK(errno == EDOM);
    R_TEST_CHECK(fegetround() == FE_UPWARD);
    R_TEST_CHECK(fetestexcept(FE_ALL_EXCEPT) == expected_exceptions);
    R_TEST_CHECK(uselocale(NULL) == numeric_locale);

    R_TEST_CHECK(uselocale(original_locale) == numeric_locale);
    freelocale(numeric_locale);
    R_TEST_CHECK(fesetenv(&original_environment) == 0);
    return 0;
}

int main(void) {
    R_TEST_CHECK(r_test_zero_matrices() == 0);
    R_TEST_CHECK(r_test_integer_boundaries() == 0);
    R_TEST_CHECK(r_test_integer_precedence() == 0);
    R_TEST_CHECK(r_test_integer_to_float() == 0);
    R_TEST_CHECK(r_test_float_to_float() == 0);
    R_TEST_CHECK(r_test_float_to_wide_integer_boundaries() == 0);
    R_TEST_CHECK(r_test_contract_and_allocator() == 0);
    R_TEST_CHECK(r_test_locale_errno_and_fenv() == 0);
    (void)fprintf(stdout, "library_checked_convert_tests: ok\n");
    return 0;
}

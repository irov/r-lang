#ifndef R_STD_CONVERT_H
#define R_STD_CONVERT_H

#include "r_std_string.h"

#include <stdint.h>

typedef enum RStdConvertParseErrorCode {
    R_STD_CONVERT_PARSE_ERROR_EMPTY = 0,
    R_STD_CONVERT_PARSE_ERROR_INVALID_RADIX = 1,
    R_STD_CONVERT_PARSE_ERROR_INVALID_DIGIT = 2,
    R_STD_CONVERT_PARSE_ERROR_TRAILING_CHARACTER = 3,
    R_STD_CONVERT_PARSE_ERROR_BELOW_MINIMUM = 4,
    R_STD_CONVERT_PARSE_ERROR_ABOVE_MAXIMUM = 5
} RStdConvertParseErrorCode;

typedef struct RStdConvertParseError {
    RStdConvertParseErrorCode code;
    size_t index;
} RStdConvertParseError;

typedef enum RStdConvertRangeError {
    R_STD_CONVERT_RANGE_ERROR_BELOW_MINIMUM = 0,
    R_STD_CONVERT_RANGE_ERROR_ABOVE_MAXIMUM = 1,
    R_STD_CONVERT_RANGE_ERROR_NOT_FINITE = 2,
    R_STD_CONVERT_RANGE_ERROR_FRACTIONAL = 3,
    R_STD_CONVERT_RANGE_ERROR_INEXACT = 4
} RStdConvertRangeError;

/*
 * Compiler-generated monomorphic wrappers provide the exact destination range. A zero
 * max_negative_magnitude denotes an unsigned destination; otherwise its value is the magnitude
 * of the destination minimum. max_positive is the exact destination maximum.
 */
typedef struct RStdConvertIntegerBounds {
    uint64_t max_positive;
    uint64_t max_negative_magnitude;
} RStdConvertIntegerBounds;

typedef struct RStdConvertParsedInteger {
    _Bool negative;
    uint64_t magnitude;
} RStdConvertParsedInteger;

/*
 * Closed arm64-apple-darwin numeric type set. The first twelve entries are R numeric types;
 * the remaining entries are the exact available C ABI suffix set from the target manifest.
 */
typedef enum RStdConvertNumericType {
    R_STD_CONVERT_NUMERIC_TYPE_I8 = 0,
    R_STD_CONVERT_NUMERIC_TYPE_U8 = 1,
    R_STD_CONVERT_NUMERIC_TYPE_I16 = 2,
    R_STD_CONVERT_NUMERIC_TYPE_U16 = 3,
    R_STD_CONVERT_NUMERIC_TYPE_I32 = 4,
    R_STD_CONVERT_NUMERIC_TYPE_U32 = 5,
    R_STD_CONVERT_NUMERIC_TYPE_I64 = 6,
    R_STD_CONVERT_NUMERIC_TYPE_U64 = 7,
    R_STD_CONVERT_NUMERIC_TYPE_ISIZE = 8,
    R_STD_CONVERT_NUMERIC_TYPE_USIZE = 9,
    R_STD_CONVERT_NUMERIC_TYPE_F32 = 10,
    R_STD_CONVERT_NUMERIC_TYPE_F64 = 11,
    R_STD_CONVERT_NUMERIC_TYPE_C_CHAR = 12,
    R_STD_CONVERT_NUMERIC_TYPE_C_SCHAR = 13,
    R_STD_CONVERT_NUMERIC_TYPE_C_UCHAR = 14,
    R_STD_CONVERT_NUMERIC_TYPE_C_SHORT = 15,
    R_STD_CONVERT_NUMERIC_TYPE_C_USHORT = 16,
    R_STD_CONVERT_NUMERIC_TYPE_C_INT = 17,
    R_STD_CONVERT_NUMERIC_TYPE_C_UINT = 18,
    R_STD_CONVERT_NUMERIC_TYPE_C_LONG = 19,
    R_STD_CONVERT_NUMERIC_TYPE_C_ULONG = 20,
    R_STD_CONVERT_NUMERIC_TYPE_C_LLONG = 21,
    R_STD_CONVERT_NUMERIC_TYPE_C_ULLONG = 22,
    R_STD_CONVERT_NUMERIC_TYPE_C_BOOL = 23,
    R_STD_CONVERT_NUMERIC_TYPE_C_WCHAR = 24,
    R_STD_CONVERT_NUMERIC_TYPE_C_WINT = 25,
    R_STD_CONVERT_NUMERIC_TYPE_C_INT8 = 26,
    R_STD_CONVERT_NUMERIC_TYPE_C_UINT8 = 27,
    R_STD_CONVERT_NUMERIC_TYPE_C_INT16 = 28,
    R_STD_CONVERT_NUMERIC_TYPE_C_UINT16 = 29,
    R_STD_CONVERT_NUMERIC_TYPE_C_INT32 = 30,
    R_STD_CONVERT_NUMERIC_TYPE_C_UINT32 = 31,
    R_STD_CONVERT_NUMERIC_TYPE_C_INT64 = 32,
    R_STD_CONVERT_NUMERIC_TYPE_C_UINT64 = 33,
    R_STD_CONVERT_NUMERIC_TYPE_C_INTPTR = 34,
    R_STD_CONVERT_NUMERIC_TYPE_C_UINTPTR = 35,
    R_STD_CONVERT_NUMERIC_TYPE_C_INTMAX = 36,
    R_STD_CONVERT_NUMERIC_TYPE_C_UINTMAX = 37,
    R_STD_CONVERT_NUMERIC_TYPE_C_FLOAT = 38,
    R_STD_CONVERT_NUMERIC_TYPE_C_DOUBLE = 39,
    R_STD_CONVERT_NUMERIC_TYPE_C_LONG_DOUBLE = 40,
    R_STD_CONVERT_NUMERIC_TYPE_C_SIZE = 41,
    R_STD_CONVERT_NUMERIC_TYPE_C_PTRDIFF = 42
} RStdConvertNumericType;

typedef union RStdConvertNumericPayload {
    int64_t signed_integer;
    uint64_t unsigned_integer;
    uint32_t binary32_bits;
    uint64_t binary64_bits;
} RStdConvertNumericPayload;

/*
 * type discriminates payload. Every signed-integer tag uses signed_integer; every unsigned-integer
 * tag, including C_BOOL, uses unsigned_integer with its exact in-range value. BINARY32 tags use
 * binary32_bits and BINARY64 tags use binary64_bits. Floating values are raw target-format bits and
 * are never read as C floating objects by the type-erased operation. A signaling-NaN input is
 * classified without raising an exception or changing caller fenv; every successful NaN result is
 * canonical quiet NaN in the destination format. A result carrier uses its destination tag and the
 * same member mapping.
 */
typedef struct RStdConvertNumericValue {
    RStdConvertNumericType type;
    RStdConvertNumericPayload payload;
} RStdConvertNumericValue;

typedef struct RStdConvertDestination {
    RStdConvertNumericType type;
} RStdConvertDestination;

/* CONTRACT_VIOLATION is a compiler/runtime ABI state and is not an R parse error. */
typedef enum RStdConvertCallStatus {
    R_STD_CONVERT_CALL_SUCCESS = 0,
    R_STD_CONVERT_CALL_ERROR = 1,
    R_STD_CONVERT_CALL_CONTRACT_VIOLATION = 2
} RStdConvertCallStatus;

typedef struct RStdConvertParseIntegerResult {
    RStdConvertCallStatus status;
    RStdConvertParsedInteger value;
    RStdConvertParseError error;
} RStdConvertParseIntegerResult;

typedef struct RStdConvertParseF32Result {
    RStdConvertCallStatus status;
    float value;
    RStdConvertParseError error;
} RStdConvertParseF32Result;

typedef struct RStdConvertParseF64Result {
    RStdConvertCallStatus status;
    double value;
    RStdConvertParseError error;
} RStdConvertParseF64Result;

typedef struct RStdConvertParseCFloatResult {
    RStdConvertCallStatus status;
    float value;
    RStdConvertParseError error;
} RStdConvertParseCFloatResult;

typedef struct RStdConvertParseCDoubleResult {
    RStdConvertCallStatus status;
    double value;
    RStdConvertParseError error;
} RStdConvertParseCDoubleResult;

typedef struct RStdConvertParseCLongDoubleResult {
    RStdConvertCallStatus status;
    long double value;
    RStdConvertParseError error;
} RStdConvertParseCLongDoubleResult;

typedef struct RStdConvertCheckedResult {
    RStdConvertCallStatus status;
    RStdConvertNumericValue value;
    RStdConvertRangeError error;
} RStdConvertCheckedResult;

/*
 * Type-erased implementation of std.convert::checked_D. Compiler-generated monomorphic wrappers
 * bit-copy floating operands into and out of the carrier and select a closed destination tag.
 * The operation allocates nothing and does not inspect or modify locale, errno or fenv.
 */
RStdConvertCheckedResult r_std_convert_checked(RStdConvertNumericValue source,
                                               RStdConvertDestination destination);

/*
 * Type-erased implementation of the closed parse_SUFFIX family. source is a shared call-bounded
 * UTF-8 borrow and is not retained. The operation allocates nothing and never changes locale.
 */
RStdConvertParseIntegerResult
r_std_convert_parse_suffix(RStdStringView source, uint32_t radix, RStdConvertIntegerBounds bounds);

/*
 * Ownership: source is a shared call-bounded UTF-8 borrow and is not retained. These operations
 * allocate nothing, consume the entire source, use the C numeric locale independently of the
 * calling thread's locale, and restore the calling thread's floating environment and errno.
 */
RStdConvertParseF32Result r_std_convert_parse_f32(RStdStringView source);
RStdConvertParseF64Result r_std_convert_parse_f64(RStdStringView source);
RStdConvertParseCFloatResult r_std_convert_parse_c_float(RStdStringView source);
RStdConvertParseCDoubleResult r_std_convert_parse_c_double(RStdStringView source);
RStdConvertParseCLongDoubleResult r_std_convert_parse_c_long_double(RStdStringView source);

#endif

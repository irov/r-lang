#include "r_std_convert.h"

#include <errno.h>
#include <fenv.h>
#include <float.h>
#include <locale.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xlocale.h>

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

static uint64_t r_test_random_next(uint64_t *state) {
    uint64_t value = *state;

    value ^= value << 13U;
    value ^= value >> 7U;
    value ^= value << 17U;
    *state = value;
    return value;
}

static int
r_test_f64_error(const char *text, RStdConvertParseErrorCode expected_code, size_t expected_index) {
    RStdConvertParseF64Result result = r_std_convert_parse_f64(r_test_view(text));

    R_TEST_CHECK(result.status == R_STD_CONVERT_CALL_ERROR);
    R_TEST_CHECK(result.error.code == expected_code);
    R_TEST_CHECK(result.error.index == expected_index);
    return 0;
}

static int r_test_successes(void) {
    const RStdConvertIntegerBounds i8_bounds = {UINT64_C(127), UINT64_C(128)};
    const RStdConvertIntegerBounds u8_bounds = {UINT64_C(255), UINT64_C(0)};
    const RStdConvertIntegerBounds u64_bounds = {UINT64_MAX, UINT64_C(0)};
    RStdConvertParseIntegerResult result =
        r_std_convert_parse_suffix(r_test_view("-128"), UINT32_C(10), i8_bounds);

    R_TEST_CHECK(result.status == R_STD_CONVERT_CALL_SUCCESS);
    R_TEST_CHECK(result.value.negative && (result.value.magnitude == UINT64_C(128)));
    result = r_std_convert_parse_suffix(r_test_view("+127"), UINT32_C(10), i8_bounds);
    R_TEST_CHECK(result.status == R_STD_CONVERT_CALL_SUCCESS);
    R_TEST_CHECK(!result.value.negative && (result.value.magnitude == UINT64_C(127)));
    result = r_std_convert_parse_suffix(r_test_view("0xff"), UINT32_C(0), u8_bounds);
    R_TEST_CHECK(result.status == R_STD_CONVERT_CALL_SUCCESS);
    R_TEST_CHECK(result.value.magnitude == UINT64_C(255));
    result = r_std_convert_parse_suffix(r_test_view("0b101"), UINT32_C(0), u8_bounds);
    R_TEST_CHECK(result.status == R_STD_CONVERT_CALL_SUCCESS);
    R_TEST_CHECK(result.value.magnitude == UINT64_C(5));
    result = r_std_convert_parse_suffix(r_test_view("0o17"), UINT32_C(0), u8_bounds);
    R_TEST_CHECK(result.status == R_STD_CONVERT_CALL_SUCCESS);
    R_TEST_CHECK(result.value.magnitude == UINT64_C(15));
    result =
        r_std_convert_parse_suffix(r_test_view("18446744073709551615"), UINT32_C(10), u64_bounds);
    R_TEST_CHECK(result.status == R_STD_CONVERT_CALL_SUCCESS);
    R_TEST_CHECK(result.value.magnitude == UINT64_MAX);
    result = r_std_convert_parse_suffix(r_test_view("Ff"), UINT32_C(16), u8_bounds);
    R_TEST_CHECK(result.status == R_STD_CONVERT_CALL_SUCCESS);
    R_TEST_CHECK(result.value.magnitude == UINT64_C(255));
    return 0;
}

static int r_test_errors(void) {
    const RStdConvertIntegerBounds i8_bounds = {UINT64_C(127), UINT64_C(128)};
    const RStdConvertIntegerBounds u8_bounds = {UINT64_C(255), UINT64_C(0)};
    RStdConvertParseIntegerResult result =
        r_std_convert_parse_suffix(r_test_view("123"), UINT32_C(1), i8_bounds);

    R_TEST_CHECK(result.status == R_STD_CONVERT_CALL_ERROR);
    R_TEST_CHECK(result.error.code == R_STD_CONVERT_PARSE_ERROR_INVALID_RADIX);
    R_TEST_CHECK(result.error.index == 0U);
    result = r_std_convert_parse_suffix(r_test_view(""), UINT32_C(10), i8_bounds);
    R_TEST_CHECK(result.error.code == R_STD_CONVERT_PARSE_ERROR_EMPTY);
    R_TEST_CHECK(result.error.index == 0U);
    result = r_std_convert_parse_suffix(r_test_view("-1"), UINT32_C(10), u8_bounds);
    R_TEST_CHECK(result.error.code == R_STD_CONVERT_PARSE_ERROR_INVALID_DIGIT);
    R_TEST_CHECK(result.error.index == 0U);
    result = r_std_convert_parse_suffix(r_test_view("+"), UINT32_C(10), i8_bounds);
    R_TEST_CHECK(result.error.code == R_STD_CONVERT_PARSE_ERROR_INVALID_DIGIT);
    R_TEST_CHECK(result.error.index == 1U);
    result = r_std_convert_parse_suffix(r_test_view("0x"), UINT32_C(0), i8_bounds);
    R_TEST_CHECK(result.error.code == R_STD_CONVERT_PARSE_ERROR_INVALID_DIGIT);
    R_TEST_CHECK(result.error.index == 2U);
    result = r_std_convert_parse_suffix(r_test_view("8"), UINT32_C(8), i8_bounds);
    R_TEST_CHECK(result.error.code == R_STD_CONVERT_PARSE_ERROR_INVALID_DIGIT);
    R_TEST_CHECK(result.error.index == 0U);
    result = r_std_convert_parse_suffix(r_test_view("1_0"), UINT32_C(10), i8_bounds);
    R_TEST_CHECK(result.error.code == R_STD_CONVERT_PARSE_ERROR_TRAILING_CHARACTER);
    R_TEST_CHECK(result.error.index == 1U);
    result = r_std_convert_parse_suffix(r_test_view("_1"), UINT32_C(10), i8_bounds);
    R_TEST_CHECK(result.error.code == R_STD_CONVERT_PARSE_ERROR_INVALID_DIGIT);
    R_TEST_CHECK(result.error.index == 0U);
    result = r_std_convert_parse_suffix(r_test_view("128"), UINT32_C(10), i8_bounds);
    R_TEST_CHECK(result.error.code == R_STD_CONVERT_PARSE_ERROR_ABOVE_MAXIMUM);
    R_TEST_CHECK(result.error.index == 3U);
    result = r_std_convert_parse_suffix(r_test_view("-129"), UINT32_C(10), i8_bounds);
    R_TEST_CHECK(result.error.code == R_STD_CONVERT_PARSE_ERROR_BELOW_MINIMUM);
    R_TEST_CHECK(result.error.index == 4U);
    result = r_std_convert_parse_suffix(
        r_test_view("184467440737095516160000"), UINT32_C(10), u8_bounds);
    R_TEST_CHECK(result.error.code == R_STD_CONVERT_PARSE_ERROR_ABOVE_MAXIMUM);
    R_TEST_CHECK(result.error.index == 24U);
    return 0;
}

static int r_test_float_successes(void) {
    RStdConvertParseF32Result f32 = r_std_convert_parse_f32(r_test_view("1.5"));
    RStdConvertParseF64Result f64 = r_std_convert_parse_f64(r_test_view("-2.25"));
    RStdConvertParseCFloatResult c_float = r_std_convert_parse_c_float(r_test_view(".5"));
    RStdConvertParseCDoubleResult c_double = r_std_convert_parse_c_double(r_test_view("1."));
    RStdConvertParseCLongDoubleResult c_long_double =
        r_std_convert_parse_c_long_double(r_test_view("1E+2"));

    R_TEST_CHECK(f32.status == R_STD_CONVERT_CALL_SUCCESS);
    R_TEST_CHECK(f32.value == 1.5F);
    R_TEST_CHECK(f64.status == R_STD_CONVERT_CALL_SUCCESS);
    R_TEST_CHECK(f64.value == -2.25);
    R_TEST_CHECK(c_float.status == R_STD_CONVERT_CALL_SUCCESS);
    R_TEST_CHECK(c_float.value == 0.5F);
    R_TEST_CHECK(c_double.status == R_STD_CONVERT_CALL_SUCCESS);
    R_TEST_CHECK(c_double.value == 1.0);
    R_TEST_CHECK(c_long_double.status == R_STD_CONVERT_CALL_SUCCESS);
    R_TEST_CHECK(c_long_double.value == 100.0L);

    f64 = r_std_convert_parse_f64(r_test_view("-0"));
    R_TEST_CHECK(f64.status == R_STD_CONVERT_CALL_SUCCESS);
    R_TEST_CHECK((f64.value == 0.0) && signbit(f64.value));
    f64 = r_std_convert_parse_f64(r_test_view("inf"));
    R_TEST_CHECK(f64.status == R_STD_CONVERT_CALL_SUCCESS);
    R_TEST_CHECK(isinf(f64.value) && !signbit(f64.value));
    f64 = r_std_convert_parse_f64(r_test_view("-inf"));
    R_TEST_CHECK(f64.status == R_STD_CONVERT_CALL_SUCCESS);
    R_TEST_CHECK(isinf(f64.value) && signbit(f64.value));
    f64 = r_std_convert_parse_f64(r_test_view("nan"));
    R_TEST_CHECK(f64.status == R_STD_CONVERT_CALL_SUCCESS);
    R_TEST_CHECK(isnan(f64.value));
    return 0;
}

static int r_test_float_grammar_errors(void) {
    static const struct {
        const char *text;
        RStdConvertParseErrorCode code;
        size_t index;
    } cases[] = {
        {"", R_STD_CONVERT_PARSE_ERROR_EMPTY, 0U},
        {"+", R_STD_CONVERT_PARSE_ERROR_INVALID_DIGIT, 1U},
        {"-", R_STD_CONVERT_PARSE_ERROR_INVALID_DIGIT, 1U},
        {".", R_STD_CONVERT_PARSE_ERROR_INVALID_DIGIT, 1U},
        {".e1", R_STD_CONVERT_PARSE_ERROR_INVALID_DIGIT, 1U},
        {"e1", R_STD_CONVERT_PARSE_ERROR_INVALID_DIGIT, 0U},
        {"+-1", R_STD_CONVERT_PARSE_ERROR_INVALID_DIGIT, 1U},
        {"1e", R_STD_CONVERT_PARSE_ERROR_INVALID_DIGIT, 2U},
        {"1e+", R_STD_CONVERT_PARSE_ERROR_INVALID_DIGIT, 3U},
        {"1e+x", R_STD_CONVERT_PARSE_ERROR_INVALID_DIGIT, 3U},
        {"1e.2", R_STD_CONVERT_PARSE_ERROR_INVALID_DIGIT, 2U},
        {"i", R_STD_CONVERT_PARSE_ERROR_INVALID_DIGIT, 1U},
        {"in", R_STD_CONVERT_PARSE_ERROR_INVALID_DIGIT, 2U},
        {"ix", R_STD_CONVERT_PARSE_ERROR_INVALID_DIGIT, 1U},
        {"n", R_STD_CONVERT_PARSE_ERROR_INVALID_DIGIT, 1U},
        {"na", R_STD_CONVERT_PARSE_ERROR_INVALID_DIGIT, 2U},
        {"nax", R_STD_CONVERT_PARSE_ERROR_INVALID_DIGIT, 2U},
        {"-in", R_STD_CONVERT_PARSE_ERROR_INVALID_DIGIT, 3U},
        {"-ix", R_STD_CONVERT_PARSE_ERROR_INVALID_DIGIT, 2U},
        {"+inf", R_STD_CONVERT_PARSE_ERROR_INVALID_DIGIT, 1U},
        {"-nan", R_STD_CONVERT_PARSE_ERROR_INVALID_DIGIT, 1U},
        {"Inf", R_STD_CONVERT_PARSE_ERROR_INVALID_DIGIT, 0U},
        {"NAN", R_STD_CONVERT_PARSE_ERROR_INVALID_DIGIT, 0U},
        {"1_0", R_STD_CONVERT_PARSE_ERROR_TRAILING_CHARACTER, 1U},
        {"1,0", R_STD_CONVERT_PARSE_ERROR_TRAILING_CHARACTER, 1U},
        {"1..2", R_STD_CONVERT_PARSE_ERROR_TRAILING_CHARACTER, 2U},
        {"1e2x", R_STD_CONVERT_PARSE_ERROR_TRAILING_CHARACTER, 3U},
        {"infx", R_STD_CONVERT_PARSE_ERROR_TRAILING_CHARACTER, 3U},
        {"nanx", R_STD_CONVERT_PARSE_ERROR_TRAILING_CHARACTER, 3U},
        {"-infx", R_STD_CONVERT_PARSE_ERROR_TRAILING_CHARACTER, 4U},
        {" 1", R_STD_CONVERT_PARSE_ERROR_INVALID_DIGIT, 0U},
        {"1 ", R_STD_CONVERT_PARSE_ERROR_TRAILING_CHARACTER, 1U},
    };
    size_t index;
    const uint8_t embedded_nul[] = {UINT8_C('1'), UINT8_C(0), UINT8_C('2')};
    RStdConvertParseF64Result nul_result =
        r_std_convert_parse_f64((RStdStringView){embedded_nul, sizeof(embedded_nul)});

    for (index = 0U; index < (sizeof(cases) / sizeof(cases[0])); index += 1U) {
        R_TEST_CHECK(r_test_f64_error(cases[index].text, cases[index].code, cases[index].index) ==
                     0);
    }
    R_TEST_CHECK(nul_result.status == R_STD_CONVERT_CALL_ERROR);
    R_TEST_CHECK(nul_result.error.code == R_STD_CONVERT_PARSE_ERROR_TRAILING_CHARACTER);
    R_TEST_CHECK(nul_result.error.index == 1U);
    return 0;
}

static int r_test_float_ranges(void) {
    static const char f32_max[] = "340282346638528859811704183484516925440";
    static const char f32_above[] = "340282346638528859811704183484516925441";
    static const char f64_max[] =
        "179769313486231570814527423731704356798070567525844996598917476803157260780028538760"
        "589558632766878171540458953514382464234321326889464182768467546703537516986049910576"
        "551282076245490090389328944075868508455133942304583236903222948165808559332123348274"
        "797826204144723168738177180919299881250404026184124858368";
    static const char f64_above[] =
        "179769313486231570814527423731704356798070567525844996598917476803157260780028538760"
        "589558632766878171540458953514382464234321326889464182768467546703537516986049910576"
        "551282076245490090389328944075868508455133942304583236903222948165808559332123348274"
        "797826204144723168738177180919299881250404026184124858369";
    RStdConvertParseF32Result f32 = r_std_convert_parse_f32(r_test_view(f32_max));
    RStdConvertParseF64Result f64 = r_std_convert_parse_f64(r_test_view(f64_max));
    RStdConvertParseCLongDoubleResult c_long_double =
        r_std_convert_parse_c_long_double(r_test_view(f64_max));

    R_TEST_CHECK((f32.status == R_STD_CONVERT_CALL_SUCCESS) && (f32.value == FLT_MAX));
    R_TEST_CHECK((f64.status == R_STD_CONVERT_CALL_SUCCESS) && (f64.value == DBL_MAX));
    R_TEST_CHECK((c_long_double.status == R_STD_CONVERT_CALL_SUCCESS) &&
                 (c_long_double.value == LDBL_MAX));
    R_TEST_CHECK(r_test_f64_error(
                     f64_above, R_STD_CONVERT_PARSE_ERROR_ABOVE_MAXIMUM, strlen(f64_above)) == 0);
    R_TEST_CHECK(r_test_f64_error("-1.7976931348623158e308",
                                  R_STD_CONVERT_PARSE_ERROR_BELOW_MINIMUM,
                                  strlen("-1.7976931348623158e308")) == 0);
    R_TEST_CHECK(r_test_f64_error("1e1000000", R_STD_CONVERT_PARSE_ERROR_ABOVE_MAXIMUM, 9U) == 0);
    f32 = r_std_convert_parse_f32(r_test_view(f32_above));
    R_TEST_CHECK(f32.status == R_STD_CONVERT_CALL_ERROR);
    R_TEST_CHECK(f32.error.code == R_STD_CONVERT_PARSE_ERROR_ABOVE_MAXIMUM);
    R_TEST_CHECK(f32.error.index == strlen(f32_above));
    f32 = r_std_convert_parse_f32(r_test_view("-3.4028234663852886e38"));
    R_TEST_CHECK(f32.status == R_STD_CONVERT_CALL_ERROR);
    R_TEST_CHECK(f32.error.code == R_STD_CONVERT_PARSE_ERROR_BELOW_MINIMUM);
    R_TEST_CHECK(f32.error.index == strlen("-3.4028234663852886e38"));

    f64 = r_std_convert_parse_f64(r_test_view("1e-1000000"));
    R_TEST_CHECK((f64.status == R_STD_CONVERT_CALL_SUCCESS) && (f64.value == 0.0));
    R_TEST_CHECK(!signbit(f64.value));
    f64 = r_std_convert_parse_f64(r_test_view("-1e-1000000"));
    R_TEST_CHECK((f64.status == R_STD_CONVERT_CALL_SUCCESS) && (f64.value == 0.0));
    R_TEST_CHECK(signbit(f64.value));
    return 0;
}

static int r_test_float_rounding(void) {
    static const char f32_midpoint[] = "1.000000059604644775390625";
    static const char f64_midpoint[] = "1.00000000000000011102230246251565404236316680908203125";
    RStdConvertParseF32Result f32 = r_std_convert_parse_f32(r_test_view(f32_midpoint));
    RStdConvertParseF64Result f64 = r_std_convert_parse_f64(r_test_view(f64_midpoint));
    RStdConvertParseCFloatResult c_float = r_std_convert_parse_c_float(r_test_view(f32_midpoint));
    RStdConvertParseCDoubleResult c_double =
        r_std_convert_parse_c_double(r_test_view(f64_midpoint));
    RStdConvertParseCLongDoubleResult c_long_double =
        r_std_convert_parse_c_long_double(r_test_view(f64_midpoint));

    R_TEST_CHECK((f32.status == R_STD_CONVERT_CALL_SUCCESS) && (f32.value == 1.0F));
    R_TEST_CHECK((c_float.status == R_STD_CONVERT_CALL_SUCCESS) && (c_float.value == 1.0F));
    R_TEST_CHECK((c_double.status == R_STD_CONVERT_CALL_SUCCESS) && (c_double.value == 1.0));
    R_TEST_CHECK((c_long_double.status == R_STD_CONVERT_CALL_SUCCESS) &&
                 (c_long_double.value == 1.0L));
    f32 = r_std_convert_parse_f32(r_test_view("1.0000000596046447753906251"));
    R_TEST_CHECK((f32.status == R_STD_CONVERT_CALL_SUCCESS) &&
                 (f32.value == nextafterf(1.0F, 2.0F)));
    f32 = r_std_convert_parse_f32(r_test_view("16777217"));
    R_TEST_CHECK((f32.status == R_STD_CONVERT_CALL_SUCCESS) && (f32.value == 16777216.0F));
    f32 = r_std_convert_parse_f32(r_test_view("16777219"));
    R_TEST_CHECK((f32.status == R_STD_CONVERT_CALL_SUCCESS) && (f32.value == 16777220.0F));

    R_TEST_CHECK((f64.status == R_STD_CONVERT_CALL_SUCCESS) && (f64.value == 1.0));
    f64 = r_std_convert_parse_f64(
        r_test_view("1.000000000000000111022302462515654042363166809082031251"));
    R_TEST_CHECK((f64.status == R_STD_CONVERT_CALL_SUCCESS) && (f64.value == nextafter(1.0, 2.0)));
    f64 = r_std_convert_parse_f64(r_test_view("9007199254740993"));
    R_TEST_CHECK((f64.status == R_STD_CONVERT_CALL_SUCCESS) && (f64.value == 9007199254740992.0));
    f64 = r_std_convert_parse_f64(r_test_view("9007199254740995"));
    R_TEST_CHECK((f64.status == R_STD_CONVERT_CALL_SUCCESS) && (f64.value == 9007199254740996.0));
    return 0;
}

static int r_test_float_long_mantissa(void) {
    static const char midpoint[] = "1.00000000000000011102230246251565404236316680908203125";
    char text[4096];
    size_t length = strlen(midpoint);
    size_t index;
    RStdConvertParseF64Result result;

    (void)memcpy(text, midpoint, length);
    for (index = 0U; index < 3000U; index += 1U) {
        text[length] = '0';
        length += 1U;
    }
    result = r_std_convert_parse_f64((RStdStringView){(const uint8_t *)text, length});
    R_TEST_CHECK((result.status == R_STD_CONVERT_CALL_SUCCESS) && (result.value == 1.0));
    text[length] = '1';
    length += 1U;
    result = r_std_convert_parse_f64((RStdStringView){(const uint8_t *)text, length});
    R_TEST_CHECK((result.status == R_STD_CONVERT_CALL_SUCCESS) &&
                 (result.value == nextafter(1.0, 2.0)));
    return 0;
}

static int r_test_float_large_exponent_cancellation(void) {
    static const char negative_exponent[] = "e-1000000";
    static const char positive_exponent[] = "e+1000001";
    const size_t zero_count = 1000001U;
    size_t length = 1U + zero_count + (sizeof(negative_exponent) - 1U);
    char *text = (char *)malloc(length);
    RStdConvertParseF64Result result;

    R_TEST_CHECK(text != NULL);
    text[0] = '1';
    (void)memset(&text[1], '0', zero_count);
    (void)memcpy(&text[1U + zero_count], negative_exponent, sizeof(negative_exponent) - 1U);
    result = r_std_convert_parse_f64((RStdStringView){(const uint8_t *)text, length});
    free(text);
    R_TEST_CHECK((result.status == R_STD_CONVERT_CALL_SUCCESS) && (result.value == 10.0));

    length = 3U + zero_count + (sizeof(positive_exponent) - 1U);
    text = (char *)malloc(length);
    R_TEST_CHECK(text != NULL);
    text[0] = '0';
    text[1] = '.';
    (void)memset(&text[2], '0', zero_count);
    text[2U + zero_count] = '1';
    (void)memcpy(&text[3U + zero_count], positive_exponent, sizeof(positive_exponent) - 1U);
    result = r_std_convert_parse_f64((RStdStringView){(const uint8_t *)text, length});
    free(text);
    R_TEST_CHECK((result.status == R_STD_CONVERT_CALL_SUCCESS) && (result.value == 0.1));
    return 0;
}

static size_t r_test_append_exponent(char *text, size_t length, int32_t exponent) {
    uint32_t magnitude;

    text[length] = 'e';
    length += 1U;
    if (exponent < INT32_C(0)) {
        text[length] = '-';
        length += 1U;
        magnitude = (uint32_t)(-exponent);
    } else {
        text[length] = '+';
        length += 1U;
        magnitude = (uint32_t)exponent;
    }
    text[length] = (char)(UINT8_C('0') + (uint8_t)(magnitude / UINT32_C(100)));
    length += 1U;
    text[length] = (char)(UINT8_C('0') + (uint8_t)((magnitude / UINT32_C(10)) % UINT32_C(10)));
    length += 1U;
    text[length] = (char)(UINT8_C('0') + (uint8_t)(magnitude % UINT32_C(10)));
    return length + 1U;
}

static int r_test_float_random_long_rounding(void) {
    char text[4096];
    uint64_t random_state = UINT64_C(0x9384d2e17ac65b0f);
    fenv_t original_environment;
    int original_errno = errno;
    size_t sample;

    R_TEST_CHECK(fegetenv(&original_environment) == 0);
    R_TEST_CHECK(fesetround(FE_TONEAREST) == 0);
    for (sample = 0U; sample < 512U; sample += 1U) {
        size_t length = 0U;
        size_t digit_index;
        int32_t exponent;
        char *end;
        double expected;
        RStdConvertParseF64Result actual;

        if ((r_test_random_next(&random_state) & UINT64_C(1)) != UINT64_C(0)) {
            text[length] = '-';
            length += 1U;
        }
        text[length] =
            (char)(UINT8_C('1') + (uint8_t)(r_test_random_next(&random_state) % UINT64_C(9)));
        length += 1U;
        text[length] = '.';
        length += 1U;
        for (digit_index = 0U; digit_index < 3000U; digit_index += 1U) {
            text[length] =
                (char)(UINT8_C('0') + (uint8_t)(r_test_random_next(&random_state) % UINT64_C(10)));
            length += 1U;
        }
        exponent = (int32_t)(r_test_random_next(&random_state) % UINT64_C(638)) - INT32_C(330);
        length = r_test_append_exponent(text, length, exponent);
        text[length] = '\0';
        end = NULL;
        expected = strtod_l(text, &end, LC_C_LOCALE);
        R_TEST_CHECK(end == &text[length]);
        actual = r_std_convert_parse_f64((RStdStringView){(const uint8_t *)text, length});
        R_TEST_CHECK(actual.status == R_STD_CONVERT_CALL_SUCCESS);
        R_TEST_CHECK(actual.value == expected);
    }
    for (sample = 0U; sample < 512U; sample += 1U) {
        size_t length = 0U;
        size_t digit_index;
        int32_t exponent;
        char *end;
        float expected;
        RStdConvertParseF32Result actual;

        text[length] =
            (char)(UINT8_C('1') + (uint8_t)(r_test_random_next(&random_state) % UINT64_C(9)));
        length += 1U;
        text[length] = '.';
        length += 1U;
        for (digit_index = 0U; digit_index < 3000U; digit_index += 1U) {
            text[length] =
                (char)(UINT8_C('0') + (uint8_t)(r_test_random_next(&random_state) % UINT64_C(10)));
            length += 1U;
        }
        exponent = (int32_t)(r_test_random_next(&random_state) % UINT64_C(88)) - INT32_C(50);
        length = r_test_append_exponent(text, length, exponent);
        text[length] = '\0';
        end = NULL;
        expected = strtof_l(text, &end, LC_C_LOCALE);
        R_TEST_CHECK(end == &text[length]);
        actual = r_std_convert_parse_f32((RStdStringView){(const uint8_t *)text, length});
        R_TEST_CHECK(actual.status == R_STD_CONVERT_CALL_SUCCESS);
        R_TEST_CHECK(actual.value == expected);
    }
    R_TEST_CHECK(fesetenv(&original_environment) == 0);
    errno = original_errno;
    return 0;
}

static int r_test_float_locale_and_environment(void) {
    fenv_t original_environment;
    int original_errno = errno;
    locale_t numeric_locale = newlocale(LC_NUMERIC_MASK, "fr_FR.UTF-8", NULL);
    locale_t original_locale;
    int expected_exceptions = FE_INVALID | FE_DIVBYZERO;
    RStdConvertParseF64Result result;

    R_TEST_CHECK(numeric_locale != NULL);
    original_locale = uselocale(numeric_locale);
    R_TEST_CHECK(original_locale != (locale_t)0);
    result = r_std_convert_parse_f64(r_test_view("1.5"));
    R_TEST_CHECK((result.status == R_STD_CONVERT_CALL_SUCCESS) && (result.value == 1.5));
    R_TEST_CHECK(uselocale(NULL) == numeric_locale);
    R_TEST_CHECK(r_test_f64_error("1,5", R_STD_CONVERT_PARSE_ERROR_TRAILING_CHARACTER, 1U) == 0);
    R_TEST_CHECK(uselocale(original_locale) == numeric_locale);
    freelocale(numeric_locale);

    R_TEST_CHECK(fegetenv(&original_environment) == 0);
    R_TEST_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    R_TEST_CHECK(fesetround(FE_UPWARD) == 0);
    R_TEST_CHECK(feraiseexcept(expected_exceptions) == 0);
    errno = EDOM;
    result = r_std_convert_parse_f64(
        r_test_view("1.00000000000000011102230246251565404236316680908203125"));
    R_TEST_CHECK((result.status == R_STD_CONVERT_CALL_SUCCESS) && (result.value == 1.0));
    R_TEST_CHECK(fegetround() == FE_UPWARD);
    R_TEST_CHECK(fetestexcept(FE_ALL_EXCEPT) == expected_exceptions);
    R_TEST_CHECK(errno == EDOM);
    R_TEST_CHECK(fesetenv(&original_environment) == 0);
    errno = original_errno;
    return 0;
}

int main(void) {
    R_TEST_CHECK(r_test_successes() == 0);
    R_TEST_CHECK(r_test_errors() == 0);
    R_TEST_CHECK(r_test_float_successes() == 0);
    R_TEST_CHECK(r_test_float_grammar_errors() == 0);
    R_TEST_CHECK(r_test_float_ranges() == 0);
    R_TEST_CHECK(r_test_float_rounding() == 0);
    R_TEST_CHECK(r_test_float_long_mantissa() == 0);
    R_TEST_CHECK(r_test_float_large_exponent_cancellation() == 0);
    R_TEST_CHECK(r_test_float_random_long_rounding() == 0);
    R_TEST_CHECK(r_test_float_locale_and_environment() == 0);
    (void)fprintf(stdout, "library_convert_tests: ok\n");
    return 0;
}

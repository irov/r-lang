static int r_test_literal_specs(void) {
    static const struct {
        long double value;
        size_t precision;
        size_t width;
        _Bool zero_fill;
        const char *expected;
    } cases[] = {
        {1.125L, 2U, 0U, 0, "1.12"},
        {1.375L, 2U, 0U, 0, "1.38"},
        {-1.125L, 2U, 0U, 0, "-1.12"},
        {9.999L, 2U, 0U, 0, "10.00"},
        {2.5L, 0U, 0U, 0, "2"},
        {3.5L, 0U, 0U, 0, "4"},
        {0.0L, 3U, 0U, 0, "0.000"},
        {-0.0L, 3U, 0U, 0, "-0.000"},
        {-1.5L, 2U, 10U, 1, "-000001.50"},
        {1.5L, 2U, 8U, 0, "    1.50"},
        {DBL_TRUE_MIN, 2U, 0U, 0, "0.00"},
        {INFINITY, 2U, 8U, 1, "     inf"},
        {-INFINITY, 2U, 8U, 1, "    -inf"},
        {NAN, 2U, 8U, 1, "     nan"},
    };
    RRuntimeAllocator allocator;
    RStdFormatBuilderResult created;
    fenv_t environment;
    R_TEST_CHECK(fegetenv(&environment) == 0);
    R_TEST_CHECK(fesetround(FE_UPWARD) == 0);
    R_TEST_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    R_TEST_CHECK(feraiseexcept(FE_DIVBYZERO) == 0);
    r_runtime_allocator_initialize(&allocator);
    created = r_std_format_create(&allocator);
    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        RStdFormatSpec spec = {.width = cases[index].width,
                               .precision = cases[index].precision,
                               .radix = 10U,
                               .zero_fill = cases[index].zero_fill,
                               .fixed = 1};
        r_std_format_clear(&created.value);
        R_TEST_CHECK(
            r_library_internal_format_float(&created.value, cases[index].value, 4U, spec).status ==
            R_STD_FORMAT_CALL_SUCCESS);
        R_TEST_CHECK(r_test_equals(r_std_format_as_str(&created.value), cases[index].expected));
        R_TEST_CHECK(fegetround() == FE_UPWARD);
        R_TEST_CHECK(fetestexcept(FE_ALL_EXCEPT) == FE_DIVBYZERO);
    }
    uint32_t signaling_bits = UINT32_C(0x7f800001);
    float signaling;
    (void)memcpy(&signaling, &signaling_bits, sizeof(signaling));
    r_std_format_clear(&created.value);
    R_TEST_CHECK(r_library_internal_format_float32(
                     &created.value, signaling, 0U, (RStdFormatSpec){.radix = 10U})
                     .status == R_STD_FORMAT_CALL_SUCCESS);
    R_TEST_CHECK(r_test_equals(r_std_format_as_str(&created.value), "nan"));
    R_TEST_CHECK(fegetround() == FE_UPWARD);
    R_TEST_CHECK(fetestexcept(FE_ALL_EXCEPT) == FE_DIVBYZERO);
    R_TEST_CHECK(fesetenv(&environment) == 0);
    r_std_format_clear(&created.value);
    R_TEST_CHECK(r_library_internal_format_integer(
                     &created.value,
                     (RStdConvertParsedInteger){1, UINT64_C(0x8000000000000000)},
                     (RStdFormatSpec){.radix = 10U})
                     .status == R_STD_FORMAT_CALL_SUCCESS);
    R_TEST_CHECK(r_test_equals(r_std_format_as_str(&created.value), "-9223372036854775808"));
    r_std_format_clear(&created.value);
    R_TEST_CHECK(r_library_internal_format_integer(&created.value,
                                                   (RStdConvertParsedInteger){0, UINT64_MAX},
                                                   (RStdFormatSpec){.radix = 16U, .uppercase = 1})
                     .status == R_STD_FORMAT_CALL_SUCCESS);
    R_TEST_CHECK(r_test_equals(r_std_format_as_str(&created.value), "FFFFFFFFFFFFFFFF"));
    r_std_format_clear(&created.value);
    R_TEST_CHECK(r_library_internal_format_float(
                     &created.value,
                     1.0L,
                     4U,
                     (RStdFormatSpec){.precision = 1100U, .radix = 10U, .fixed = 1})
                     .status == R_STD_FORMAT_CALL_SUCCESS);
    RStdStringView long_precision = r_std_format_as_str(&created.value);
    R_TEST_CHECK(long_precision.length == 1102U && long_precision.data[0] == '1' &&
                 long_precision.data[1] == '.');
    for (size_t index = 2U; index < long_precision.length; ++index) {
        R_TEST_CHECK(long_precision.data[index] == '0');
    }
    RStdFormatAllocResult overflow = r_library_internal_format_float(
        &created.value,
        1.0L,
        4U,
        (RStdFormatSpec){.precision = SIZE_MAX, .radix = 10U, .fixed = 1});
    R_TEST_CHECK(overflow.status == R_STD_FORMAT_CALL_ERROR &&
                 overflow.error == R_STD_ALLOC_ERROR_SIZE_OVERFLOW);
    R_TEST_CHECK(r_std_format_as_str(&created.value).length == long_precision.length);
    r_std_string_destroy(&created.value.output);
    for (uint64_t failure = 1U; failure <= 2U; ++failure) {
        created = r_std_format_create(&allocator);
        r_runtime_allocator_set_failure(&allocator, failure);
        RStdFormatAllocResult failed = r_library_internal_format_float(
            &created.value, 1.25L, 1U, (RStdFormatSpec){.width = 100U, .radix = 10U});
        R_TEST_CHECK(failed.status == R_STD_FORMAT_CALL_ERROR &&
                     failed.error == R_STD_ALLOC_ERROR_OUT_OF_MEMORY);
        R_TEST_CHECK(r_std_format_as_str(&created.value).length == 0U);
        r_std_string_destroy(&created.value.output);
        r_runtime_allocator_set_failure(&allocator, 0U);
    }
    return 0;
}

/* R-EXPR-0028 (L32): a width pads text and characters with spaces to that many Unicode scalar
 * values, never truncates, and a failed reservation preserves the builder. */
static int r_test_text_widths(void) {
    static const struct {
        const char *text;
        size_t width;
        const char *expected;
    } cases[] = {
        {"ab", 4U, "  ab"},
        {"ab", 1U, "ab"},
        {"h\xc3\xa9llo", 7U, "  h\xc3\xa9llo"},
        {"\xf0\x9f\x98\x80", 2U, " \xf0\x9f\x98\x80"},
        {"", 3U, "   "},
        {"", 0U, ""},
    };
    RRuntimeAllocator allocator;
    RStdFormatBuilderResult created;
    r_runtime_allocator_initialize(&allocator);
    created = r_std_format_create(&allocator);
    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        RStdStringView text = {(const uint8_t *)cases[index].text, strlen(cases[index].text)};
        r_std_format_clear(&created.value);
        R_TEST_CHECK(r_library_internal_format_text(
                         &created.value,
                         text,
                         (RStdFormatSpec){.width = cases[index].width, .radix = 10U})
                         .status == R_STD_FORMAT_CALL_SUCCESS);
        R_TEST_CHECK(r_test_equals(r_std_format_as_str(&created.value), cases[index].expected));
    }
    static const struct {
        uint32_t scalar;
        size_t width;
        const char *expected;
    } characters[] = {
        {UINT32_C(0x41), 3U, "  A"},
        {UINT32_C(0xe9), 2U, " \xc3\xa9"},
        {UINT32_C(0x20ac), 1U, "\xe2\x82\xac"},
        {UINT32_C(0x1f600), 3U, "  \xf0\x9f\x98\x80"},
    };
    for (size_t index = 0U; index < sizeof(characters) / sizeof(characters[0]); ++index) {
        r_std_format_clear(&created.value);
        R_TEST_CHECK(r_library_internal_format_char(
                         &created.value,
                         characters[index].scalar,
                         (RStdFormatSpec){.width = characters[index].width, .radix = 10U})
                         .status == R_STD_FORMAT_CALL_SUCCESS);
        R_TEST_CHECK(
            r_test_equals(r_std_format_as_str(&created.value), characters[index].expected));
    }
    RStdFormatAllocResult overflow = r_library_internal_format_text(
        &created.value,
        (RStdStringView){(const uint8_t *)"ab", 2U},
        (RStdFormatSpec){.width = SIZE_MAX, .radix = 10U});
    R_TEST_CHECK(overflow.status == R_STD_FORMAT_CALL_ERROR &&
                 overflow.error == R_STD_ALLOC_ERROR_SIZE_OVERFLOW);
    r_std_string_destroy(&created.value.output);
    created = r_std_format_create(&allocator);
    r_runtime_allocator_set_failure(&allocator, 1U);
    RStdFormatAllocResult failed = r_library_internal_format_text(
        &created.value,
        (RStdStringView){(const uint8_t *)"ab", 2U},
        (RStdFormatSpec){.width = 8U, .radix = 10U});
    R_TEST_CHECK(failed.status == R_STD_FORMAT_CALL_ERROR &&
                 failed.error == R_STD_ALLOC_ERROR_OUT_OF_MEMORY);
    R_TEST_CHECK(r_std_format_as_str(&created.value).length == 0U);
    r_runtime_allocator_set_failure(&allocator, 0U);
    r_std_string_destroy(&created.value.output);
    return 0;
}

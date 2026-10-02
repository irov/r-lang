#include "r_std_json.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression)                                                                          \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            (void)fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression);                 \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static RStdJsonByteView view(const char *text) {
    return (RStdJsonByteView){(const uint8_t *)text, strlen(text)};
}
static bool equal(RStdJsonByteView a, RStdJsonByteView b) {
    return a.length == b.length && (a.length == 0U || memcmp(a.data, b.data, a.length) == 0);
}
static int test_tree(void) {
    RRuntimeAllocator allocator;
    const char *source = "{\"id\":18446744073709551615,\"name\":\"Madrid\\n\\uD83D\\uDE00\",\"a\":["
                         "null,true,false,-0,1.25e+3],\"\":{}}";
    RStdJsonValueResult parsed;
    RStdJsonStringResult output;
    RStdJsonValueResult taken;
    RStdJsonValueResult reparsed;
    const RStdJsonValue *field;
    r_runtime_allocator_initialize(&allocator);
    parsed = r_std_json_parse(&allocator, view(source));
    CHECK(parsed.outcome.status == R_STD_JSON_CALL_SUCCESS);
    CHECK(r_std_json_kind(&parsed.value) == R_STD_JSON_OBJECT);
    CHECK(r_std_json_len(&parsed.value) == 4U);
    field = r_std_json_find(&parsed.value, view("id"));
    CHECK(field != NULL && equal(r_std_json_text(field), view("18446744073709551615")));
    field = r_std_json_find(&parsed.value, view("name"));
    CHECK(field != NULL && equal(r_std_json_text(field), view("Madrid\n\xf0\x9f\x98\x80")));
    CHECK(equal(r_std_json_key_at(&parsed.value, 2U), view("a")));
    output = r_std_json_stringify(&allocator, &parsed.value);
    CHECK(output.outcome.status == R_STD_JSON_CALL_SUCCESS);
    reparsed = r_std_json_parse(&allocator,
                                (RStdJsonByteView){r_runtime_string_bytes(&output.value),
                                                   r_runtime_string_length(&output.value)});
    CHECK(reparsed.outcome.status == R_STD_JSON_CALL_SUCCESS);
    r_json_value_destroy(&reparsed.value);
    r_runtime_string_destroy(&output.value);
    taken = r_std_json_take_field(&parsed.value, view("a"));
    CHECK(taken.outcome.status == R_STD_JSON_CALL_SUCCESS);
    CHECK(r_std_json_len(&taken.value) == 5U && r_std_json_len(&parsed.value) == 3U);
    CHECK(r_std_json_kind(r_std_json_get(&taken.value, 0U)) == R_STD_JSON_NULL);
    CHECK(r_std_json_boolean(r_std_json_get(&taken.value, 1U)));
    CHECK(!r_std_json_boolean(r_std_json_get(&taken.value, 2U)));
    CHECK(r_std_json_insert(&parsed.value, view("a"), &taken.value).status ==
          R_STD_JSON_CALL_SUCCESS);
    CHECK(taken.value.node == NULL);
    r_json_value_destroy(&parsed.value);
    return 0;
}
static int test_invalid(void) {
    static const char *const invalid[] = {"",
                                          " ",
                                          "[",
                                          "{",
                                          "[1,]",
                                          "{\"a\":1,}",
                                          "{a:1}",
                                          "[1 2]",
                                          "01",
                                          "-01",
                                          "+1",
                                          ".1",
                                          "1.",
                                          "1e",
                                          "1e+",
                                          "--1",
                                          "NaN",
                                          "Infinity",
                                          "True",
                                          "nul",
                                          "true x",
                                          "[/*comment*/]",
                                          "\"\\uD800\"",
                                          "\"\\uDC00\"",
                                          "\"\\uD800\\u0041\"",
                                          "\"\\x20\"",
                                          "\"\n\"",
                                          "\"\xc0\x80\"",
                                          "\"\xed\xa0\x80\"",
                                          "\"\xf4\x90\x80\x80\"",
                                          "\"\xe2\x82\"",
                                          "{\"a\":1,\"\\u0061\":2}",
                                          "{\"a\":{\"b\":0,\"b\":1}}",
                                          "{\"\":0,\"\":1}"};
    RRuntimeAllocator allocator;
    r_runtime_allocator_initialize(&allocator);
    for (size_t i = 0U; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        RStdJsonValueResult result = r_std_json_parse(&allocator, view(invalid[i]));
        if (result.outcome.status != R_STD_JSON_CALL_JSON_ERROR)
            (void)fprintf(stderr, "unexpected acceptance: %s\n", invalid[i]);
        CHECK(result.outcome.status == R_STD_JSON_CALL_JSON_ERROR);
        CHECK(result.value.node == NULL);
        r_json_error_destroy(&result.outcome.error);
    }
    {
        RStdJsonValueResult result =
            r_std_json_parse(&allocator, view("{\"a/b~c\":[{\"d\":0,\"d\":1}]}"));
        CHECK(result.outcome.error.code == R_STD_JSON_ERROR_DUPLICATE_KEY);
        CHECK(equal((RStdJsonByteView){r_runtime_string_bytes(&result.outcome.error.pointer),
                                       r_runtime_string_length(&result.outcome.error.pointer)},
                    view("/a~1b~0c/0/d")));
        r_json_error_destroy(&result.outcome.error);
    }
    return 0;
}
typedef struct Trace {
    uint64_t hash;
    size_t tokens;
    size_t values;
} Trace;
static int
scan_chunks(const char *source, size_t chunk_size, RStdJsonOptions options, Trace *trace) {
    RRuntimeAllocator allocator;
    RStdJsonScanner scanner;
    RStdJsonResult initialized;
    size_t length = strlen(source), offset = 0U;
    bool end = false;
    r_runtime_allocator_initialize(&allocator);
    initialized = r_json_scanner_initialize(&scanner, &allocator, options);
    CHECK(initialized.status == R_STD_JSON_CALL_SUCCESS);
    *trace = (Trace){UINT64_C(14695981039346656037), 0U, 0U};
    while (!end) {
        size_t available = length - offset;
        size_t consumed = 0U;
        uint8_t scratch[512];
        if (available > chunk_size)
            available = chunk_size;
        CHECK(available <= sizeof(scratch));
        memcpy(scratch, source + offset, available);
        for (;;) {
            RStdJsonFeedResult fed =
                r_json_scanner_feed(&scanner,
                                    (RStdJsonByteView){scratch + consumed, available - consumed},
                                    offset + available == length);
            consumed += fed.consumed;
            CHECK(fed.outcome.status == R_STD_JSON_CALL_SUCCESS);
            if (fed.state == R_STD_JSON_FEED_TOKEN) {
                ++trace->tokens;
                trace->hash ^= (uint64_t)fed.token.kind + (uint64_t)fed.token.offset * 31U;
                trace->hash *= UINT64_C(1099511628211);
                for (size_t i = 0U; i < fed.token.text.length; ++i) {
                    trace->hash ^= fed.token.text.data[i];
                    trace->hash *= UINT64_C(1099511628211);
                }
            } else if (fed.state == R_STD_JSON_FEED_VALUE_READY)
                ++trace->values;
            else if (fed.state == R_STD_JSON_FEED_END) {
                end = true;
                break;
            } else {
                CHECK(consumed == available);
                CHECK(offset + available != length);
                break;
            }
        }
        memset(scratch, 0xff, sizeof(scratch));
        offset += consumed;
    }
    CHECK(offset == length);
    r_json_scanner_destroy(&scanner);
    return 0;
}
static int test_chunks(void) {
    const char *source = " {\"\\u0061\":[-12.34e-56,\"\xe2\x82\xac\\uD83D\\uDE00\\\"\\\\\",true,"
                         "false,null],\"b\":{}} ";
    Trace expected, actual;
    RStdJsonOptions options = R_STD_JSON_DEFAULT_OPTIONS;
    CHECK(scan_chunks(source, strlen(source), options, &expected) == 0);
    CHECK(expected.values == 1U);
    for (size_t size = 1U; size <= strlen(source); ++size) {
        CHECK(scan_chunks(source, size, options, &actual) == 0);
        CHECK(actual.hash == expected.hash && actual.tokens == expected.tokens &&
              actual.values == 1U);
    }
    options.mode = R_STD_JSON_MODE_SEQUENCE;
    CHECK(scan_chunks("{} [] 12 true \"x\" null", 1U, options, &actual) == 0);
    CHECK(actual.values == 6U);
    options.mode = R_STD_JSON_MODE_ARRAY_ELEMENTS;
    options.max_value_bytes = 4U;
    CHECK(scan_chunks(" [ 1234, {} ,true, [] ,null] ", 1U, options, &actual) == 0);
    CHECK(actual.values == 5U);
    CHECK(scan_chunks("[]", 1U, options, &actual) == 0 && actual.values == 0U);
    return 0;
}
static int test_numbers_and_folding(void) {
    RRuntimeAllocator allocator;
    const char *const zero[] = {"0", "-0", "0.0", "-0.000E-9999999999999", "0e9999999999999"};
    r_runtime_allocator_initialize(&allocator);
    for (size_t i = 0U; i < sizeof(zero) / sizeof(zero[0]); ++i) {
        RStdJsonNumberResult number = r_std_json_parse_number(&allocator, view(zero[i]));
        CHECK(number.outcome.status == R_STD_JSON_CALL_SUCCESS);
        CHECK(r_std_json_number_is_zero(&number.value));
        CHECK(equal(r_std_json_number_text(&number.value), view(zero[i])));
        r_json_number_destroy(&number.value);
    }
    {
        RStdJsonNumberResult number = r_std_json_parse_number(&allocator, view("0.00001e-99999"));
        CHECK(number.outcome.status == R_STD_JSON_CALL_SUCCESS);
        CHECK(!r_std_json_number_is_zero(&number.value));
        r_json_number_destroy(&number.value);
    }
    CHECK(r_std_json_name_equal(view("Display_Name"), view("display-name"), true));
    CHECK(!r_std_json_name_equal(view("Display_Name"), view("display-name"), false));
    CHECK(r_std_json_name_equal(view("K"), view("\xe2\x84\xaa"), true));
    CHECK(r_std_json_name_equal(view("S"), view("\xc5\xbf"), true));
    CHECK(r_std_json_name_equal(view("\xce\xa3"), view("\xcf\x82"), true));
    CHECK(!r_std_json_name_equal(view("ss"), view("\xc3\x9f"), true));
    CHECK(!r_std_json_name_equal(view("\xc3\xa9"), view("e\xcc\x81"), true));
    CHECK(!r_std_json_name_equal(view("I"), view("\xc4\xb1"), true));
    return 0;
}
static int test_limits(void) {
    RRuntimeAllocator allocator;
    RStdJsonOptions options = R_STD_JSON_DEFAULT_OPTIONS;
    RStdJsonValueResult result;
    r_runtime_allocator_initialize(&allocator);
    options.max_depth = 2U;
    result = r_std_json_parse_with_options(&allocator, view("[[[]]]"), options);
    CHECK(result.outcome.status == R_STD_JSON_CALL_JSON_ERROR &&
          result.outcome.error.code == R_STD_JSON_ERROR_DEPTH_LIMIT);
    CHECK(result.outcome.error.offset == 2U);
    r_json_error_destroy(&result.outcome.error);
    options.max_value_bytes = 4U;
    result = r_std_json_parse_with_options(&allocator, view("[123]"), options);
    CHECK(result.outcome.status == R_STD_JSON_CALL_JSON_ERROR &&
          result.outcome.error.code == R_STD_JSON_ERROR_SIZE_LIMIT);
    r_json_error_destroy(&result.outcome.error);
    result = r_std_json_parse_with_options(&allocator, view(" true "), options);
    CHECK(result.outcome.status == R_STD_JSON_CALL_SUCCESS);
    r_json_value_destroy(&result.value);
    return 0;
}
static int test_allocation_failures(void) {
    const char *source = "{\"long-key/with~escapes\":[\"long owned string "
                         "value\",{\"a\":12.34,\"b\":true}],\"c\":null}";
    RRuntimeAllocator allocator;
    RStdJsonValueResult result;
    uint64_t attempts;
    r_runtime_allocator_initialize(&allocator);
    result = r_std_json_parse(&allocator, view(source));
    CHECK(result.outcome.status == R_STD_JSON_CALL_SUCCESS);
    attempts = r_runtime_allocator_attempt_count(&allocator);
    r_json_value_destroy(&result.value);
    for (uint64_t i = 1U; i <= attempts; ++i) {
        r_runtime_allocator_set_failure(&allocator, i);
        result = r_std_json_parse(&allocator, view(source));
        CHECK(result.outcome.status == R_STD_JSON_CALL_ALLOCATION_ERROR);
        CHECK(result.value.node == NULL);
    }
    r_runtime_allocator_set_failure(&allocator, 0U);
    result = r_std_json_parse(&allocator, view(source));
    CHECK(result.outcome.status == R_STD_JSON_CALL_SUCCESS);
    {
        RStdJsonOptions options = R_STD_JSON_DEFAULT_OPTIONS;
        RStdJsonStringResult text;
        options.indent = 4U;
        r_runtime_allocator_set_failure(&allocator, 0U);
        text = r_std_json_stringify_with_options(&allocator, &result.value, options);
        CHECK(text.outcome.status == R_STD_JSON_CALL_SUCCESS);
        attempts = r_runtime_allocator_attempt_count(&allocator);
        r_runtime_string_destroy(&text.value);
        for (uint64_t i = 1U; i <= attempts; ++i) {
            r_runtime_allocator_set_failure(&allocator, i);
            text = r_std_json_stringify_with_options(&allocator, &result.value, options);
            CHECK(text.outcome.status == R_STD_JSON_CALL_ALLOCATION_ERROR);
            CHECK(text.value.bytes.data == NULL);
            CHECK(r_std_json_len(&result.value) == 2U);
        }
    }
    r_json_value_destroy(&result.value);
    r_runtime_allocator_set_failure(&allocator, 0U);
    result = r_std_json_parse(&allocator, view("{\"key\":[0,]}"));
    CHECK(result.outcome.status == R_STD_JSON_CALL_JSON_ERROR);
    attempts = r_runtime_allocator_attempt_count(&allocator);
    r_json_error_destroy(&result.outcome.error);
    for (uint64_t i = 1U; i <= attempts; ++i) {
        r_runtime_allocator_set_failure(&allocator, i);
        result = r_std_json_parse(&allocator, view("{\"key\":[0,]}"));
        CHECK(result.outcome.status == R_STD_JSON_CALL_ALLOCATION_ERROR);
        CHECK(result.value.node == NULL);
    }
    return 0;
}
static int test_transactional_mutation(void) {
    RRuntimeAllocator allocator;
    RStdJsonValueResult target, value;
    RStdJsonResult inserted;
    r_runtime_allocator_initialize(&allocator);
    target = r_std_json_object(&allocator);
    value = r_std_json_from_string(&allocator, view("owner"));
    CHECK(target.outcome.status == R_STD_JSON_CALL_SUCCESS &&
          value.outcome.status == R_STD_JSON_CALL_SUCCESS);
    for (uint64_t i = 1U; i <= 2U; ++i) {
        r_runtime_allocator_set_failure(&allocator, i);
        inserted = r_std_json_insert(&target.value, view("new_key"), &value.value);
        CHECK(inserted.status == R_STD_JSON_CALL_ALLOCATION_ERROR);
        CHECK(r_std_json_len(&target.value) == 0U && value.value.node != NULL);
    }
    r_runtime_allocator_set_failure(&allocator, 0U);
    inserted = r_std_json_insert(&target.value, view("new_key"), &value.value);
    CHECK(inserted.status == R_STD_JSON_CALL_SUCCESS && value.value.node == NULL);
    r_json_value_destroy(&target.value);
    return 0;
}
static int test_cursor(void) {
    RRuntimeAllocator allocator;
    RStdJsonCursor cursor;
    RStdJsonResult result;
    uint64_t magnitude;
    bool negative;
    const char *const invalid_integer[] = {"1.0", "1e0", "18446744073709551616", "-1", "\"1\""};
    r_runtime_allocator_initialize(&allocator);
    for (size_t i = 0U; i < sizeof(invalid_integer) / sizeof(invalid_integer[0]); ++i) {
        (void)r_json_cursor_initialize(
            &cursor, &allocator, view(invalid_integer[i]), R_STD_JSON_DEFAULT_OPTIONS);
        CHECK(!r_json_cursor_integer(&cursor, false, UINT64_MAX, 0U, &negative, &magnitude));
        result = r_json_cursor_finish(&cursor);
        CHECK(result.status == R_STD_JSON_CALL_JSON_ERROR);
        r_json_error_destroy(&result.error);
    }
    (void)r_json_cursor_initialize(
        &cursor,
        &allocator,
        view(
            "[18446744073709551615,-9223372036854775808,true,\"\\uD83D\\uDE00\",1.25e2,\"owned\"]"),
        R_STD_JSON_DEFAULT_OPTIONS);
    CHECK(r_json_cursor_expect(&cursor, R_STD_JSON_TOKEN_ARRAY_BEGIN));
    CHECK(r_json_cursor_next(&cursor));
    CHECK(r_json_cursor_integer(&cursor, false, UINT64_MAX, 0U, &negative, &magnitude));
    CHECK(!negative && magnitude == UINT64_MAX);
    CHECK(r_json_cursor_integer(
        &cursor, false, INT64_MAX, UINT64_C(9223372036854775808), &negative, &magnitude));
    CHECK(negative && magnitude == UINT64_C(9223372036854775808));
    {
        bool boolean;
        uint32_t scalar;
        double decimal;
        RStdString string;
        CHECK(r_json_cursor_boolean(&cursor, &boolean) && boolean);
        CHECK(r_json_cursor_char(&cursor, &scalar) && scalar == UINT32_C(0x1f600));
        CHECK(r_json_cursor_float(&cursor, false, false, &decimal) && decimal == 125.0);
        CHECK(r_json_cursor_string(&cursor, &string));
        CHECK(r_runtime_string_length(&string) == 5U);
        CHECK(r_json_cursor_expect(&cursor, R_STD_JSON_TOKEN_ARRAY_END));
        CHECK(r_json_cursor_next(&cursor));
        result = r_json_cursor_finish(&cursor);
        CHECK(result.status == R_STD_JSON_CALL_SUCCESS);
        CHECK(memcmp(r_runtime_string_bytes(&string), "owned", 5U) == 0);
        r_runtime_string_destroy(&string);
    }
    (void)r_json_cursor_initialize(
        &cursor, &allocator, view("{\"a/b~c\":[true]}"), R_STD_JSON_DEFAULT_OPTIONS);
    CHECK(r_json_cursor_next(&cursor));
    CHECK(r_json_cursor_next(&cursor));
    CHECK(r_json_cursor_next(&cursor));
    CHECK(!r_json_cursor_integer(&cursor, false, UINT64_MAX, 0U, &negative, &magnitude));
    result = r_json_cursor_finish(&cursor);
    CHECK(result.status == R_STD_JSON_CALL_JSON_ERROR);
    CHECK(equal((RStdJsonByteView){r_runtime_string_bytes(&result.error.pointer),
                                   r_runtime_string_length(&result.error.pointer)},
                view("/a~1b~0c/0")));
    r_json_error_destroy(&result.error);
    {
        uint64_t attempts;
        RStdString string;
        r_runtime_allocator_set_failure(&allocator, 0U);
        (void)r_json_cursor_initialize(
            &cursor, &allocator, view("\"owned UTF-8 text\""), R_STD_JSON_DEFAULT_OPTIONS);
        CHECK(r_json_cursor_string(&cursor, &string));
        result = r_json_cursor_finish(&cursor);
        CHECK(result.status == R_STD_JSON_CALL_SUCCESS);
        r_runtime_string_destroy(&string);
        attempts = r_runtime_allocator_attempt_count(&allocator);
        for (uint64_t i = 1U; i <= attempts; ++i) {
            r_runtime_allocator_set_failure(&allocator, i);
            (void)r_json_cursor_initialize(
                &cursor, &allocator, view("\"owned UTF-8 text\""), R_STD_JSON_DEFAULT_OPTIONS);
            CHECK(!r_json_cursor_string(&cursor, &string));
            result = r_json_cursor_finish(&cursor);
            CHECK(result.status == R_STD_JSON_CALL_ALLOCATION_ERROR);
        }
    }
    return 0;
}

static int test_scalar_encoders(void) {
    RRuntimeAllocator allocator;
    RStdJsonValueResult value;
    RStdJsonStringResult text;
    r_runtime_allocator_initialize(&allocator);
    value = r_json_encode_integer(&allocator, false, UINT64_MAX, true);
    CHECK(value.outcome.status == R_STD_JSON_CALL_SUCCESS);
    text = r_std_json_stringify(&allocator, &value.value);
    CHECK(text.outcome.status == R_STD_JSON_CALL_SUCCESS);
    CHECK(equal((RStdJsonByteView){r_runtime_string_bytes(&text.value),
                                   r_runtime_string_length(&text.value)},
                view("\"18446744073709551615\"")));
    r_runtime_string_destroy(&text.value);
    r_json_value_destroy(&value.value);
    value = r_json_encode_integer(&allocator, true, UINT64_C(9223372036854775808), false);
    CHECK(value.outcome.status == R_STD_JSON_CALL_SUCCESS);
    CHECK(equal(r_std_json_text(&value.value), view("-9223372036854775808")));
    r_json_value_destroy(&value.value);
    value = r_json_encode_char(&allocator, UINT32_C(0x1f600));
    CHECK(value.outcome.status == R_STD_JSON_CALL_SUCCESS);
    CHECK(equal(r_std_json_text(&value.value), view("\xf0\x9f\x98\x80")));
    r_json_value_destroy(&value.value);
    value = r_json_encode_float(&allocator, 0.125L, 1U, false);
    CHECK(value.outcome.status == R_STD_JSON_CALL_SUCCESS);
    CHECK(equal(r_std_json_text(&value.value), view("0.125")));
    CHECK(!r_json_value_empty(&value.value));
    r_json_value_destroy(&value.value);
    for (uint32_t operation = 0U; operation < 3U; ++operation) {
        uint64_t attempts;
        r_runtime_allocator_set_failure(&allocator, 0U);
        value = operation == 0U   ? r_json_encode_integer(&allocator, false, UINT64_MAX, false)
                : operation == 1U ? r_json_encode_float(&allocator, 0.125L, 1U, true)
                                  : r_json_encode_char(&allocator, UINT32_C(0x1f600));
        CHECK(value.outcome.status == R_STD_JSON_CALL_SUCCESS);
        attempts = r_runtime_allocator_attempt_count(&allocator);
        r_json_value_destroy(&value.value);
        for (uint64_t i = 1U; i <= attempts; ++i) {
            r_runtime_allocator_set_failure(&allocator, i);
            value = operation == 0U   ? r_json_encode_integer(&allocator, false, UINT64_MAX, false)
                    : operation == 1U ? r_json_encode_float(&allocator, 0.125L, 1U, true)
                                      : r_json_encode_char(&allocator, UINT32_C(0x1f600));
            CHECK(value.outcome.status == R_STD_JSON_CALL_ALLOCATION_ERROR);
            CHECK(value.value.node == NULL);
        }
    }
    return 0;
}
static int test_deep_writer(void) {
    RRuntimeAllocator allocator;
    RStdJsonValue value = {0};
    RStdJsonOptions options = R_STD_JSON_DEFAULT_OPTIONS;
    RStdJsonStringResult text;
    r_runtime_allocator_initialize(&allocator);
    for (size_t i = 0U; i < 5000U; ++i) {
        RStdJsonValueResult parent = r_std_json_array(&allocator);
        CHECK(parent.outcome.status == R_STD_JSON_CALL_SUCCESS);
        CHECK(r_std_json_append(&parent.value, &value).status == R_STD_JSON_CALL_SUCCESS);
        value = parent.value;
    }
    text = r_std_json_stringify(&allocator, &value);
    CHECK(text.outcome.status == R_STD_JSON_CALL_JSON_ERROR);
    CHECK(text.outcome.error.code == R_STD_JSON_ERROR_DEPTH_LIMIT);
    r_json_error_destroy(&text.outcome.error);
    RStdJsonValueResult cloned = r_json_clone_value(&allocator, &value);
    CHECK(cloned.outcome.status == R_STD_JSON_CALL_SUCCESS);
    r_json_value_destroy(&value);
    value = cloned.value;
    options.max_depth = 5000U;
    text = r_std_json_stringify_with_options(&allocator, &value, options);
    CHECK(text.outcome.status == R_STD_JSON_CALL_SUCCESS);
    CHECK(r_runtime_string_length(&text.value) == 10004U);
    r_runtime_string_destroy(&text.value);
    r_json_value_destroy(&value);
    return 0;
}

#include "library_json_stream_tests.inc"

int main(void) {
    CHECK(test_stream_boundaries() == 0);
    CHECK(test_stream_sequences() == 0);
    CHECK(test_stream_failures() == 0);
    CHECK(test_scalar_encoders() == 0);
    CHECK(test_deep_writer() == 0);
    CHECK(test_cursor() == 0);
    CHECK(test_tree() == 0);
    CHECK(test_invalid() == 0);
    CHECK(test_chunks() == 0);
    CHECK(test_numbers_and_folding() == 0);
    CHECK(test_limits() == 0);
    CHECK(test_allocation_failures() == 0);
    CHECK(test_transactional_mutation() == 0);
    return 0;
}

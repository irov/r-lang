#include "r_library_float_parse_internal.h"
#include "r_library_json_internal.h"

#include <math.h>

RStdJsonResult r_json_cursor_initialize(RStdJsonCursor *cursor,
                                        RRuntimeAllocator *allocator,
                                        RStdJsonByteView input,
                                        RStdJsonOptions options) {
    *cursor = (RStdJsonCursor){0};
    cursor->input = input;
    cursor->allocator = allocator;
    options.mode = R_STD_JSON_MODE_DOCUMENT;
    cursor->options = options;
    cursor->outcome = r_json_scanner_initialize(&cursor->scanner, allocator, options);
    if (cursor->outcome.status == R_STD_JSON_CALL_SUCCESS)
        (void)r_json_cursor_next(cursor);
    /* Outcome remains owned by the cursor until finish, even when initialization fails. */
    return (RStdJsonResult){.status = cursor->outcome.status};
}
bool r_json_cursor_next(RStdJsonCursor *cursor) {
    RStdJsonFeedResult result;
    if (cursor->outcome.status != R_STD_JSON_CALL_SUCCESS)
        return false;
    if (cursor->decoder != NULL) {
        cursor->has_token = false;
        return true;
    }
    if (cursor->complete) {
        cursor->has_token = false;
        return true;
    }
    result = r_json_scanner_feed(&cursor->scanner,
                                 (RStdJsonByteView){cursor->input.data == NULL
                                                        ? NULL
                                                        : cursor->input.data + cursor->consumed,
                                                    cursor->input.length - cursor->consumed},
                                 true);
    cursor->consumed += result.consumed;
    cursor->outcome = result.outcome;
    cursor->has_token = result.state == R_STD_JSON_FEED_TOKEN;
    cursor->token = result.token;
    cursor->complete =
        result.state == R_STD_JSON_FEED_VALUE_READY || result.state == R_STD_JSON_FEED_END;
    return cursor->outcome.status == R_STD_JSON_CALL_SUCCESS;
}
bool r_json_cursor_fail(RStdJsonCursor *cursor, RStdJsonErrorCode code) {
    if (cursor->outcome.status == R_STD_JSON_CALL_SUCCESS)
        cursor->outcome = r_json_scanner_failure(
            &cursor->scanner, code, cursor->has_token ? cursor->token.offset : cursor->consumed);
    return false;
}
bool r_json_cursor_expect(RStdJsonCursor *cursor, RStdJsonTokenKind kind) {
    if (cursor->outcome.status != R_STD_JSON_CALL_SUCCESS)
        return false;
    if (!cursor->has_token || cursor->token.kind != kind)
        return r_json_cursor_fail(cursor, R_STD_JSON_ERROR_TYPE);
    return true;
}
bool r_json_cursor_skip(RStdJsonCursor *cursor) {
    size_t depth = 0U;
    if (!cursor->has_token)
        return r_json_cursor_fail(cursor, R_STD_JSON_ERROR_UNEXPECTED_EOF);
    do {
        if (cursor->token.kind == R_STD_JSON_TOKEN_ARRAY_BEGIN ||
            cursor->token.kind == R_STD_JSON_TOKEN_OBJECT_BEGIN)
            ++depth;
        else if (cursor->token.kind == R_STD_JSON_TOKEN_ARRAY_END ||
                 cursor->token.kind == R_STD_JSON_TOKEN_OBJECT_END) {
            if (depth == 0U)
                return r_json_cursor_fail(cursor, R_STD_JSON_ERROR_TYPE);
            --depth;
        }
        if (!r_json_cursor_next(cursor))
            return false;
    } while (depth != 0U && cursor->has_token);
    return depth == 0U;
}
static bool r_json_cursor_number_text(RStdJsonCursor *cursor, bool quoted, RStdJsonByteView *text) {
    if (!r_json_cursor_expect(cursor, quoted ? R_STD_JSON_TOKEN_STRING : R_STD_JSON_TOKEN_NUMBER))
        return false;
    *text = cursor->token.text;
    return r_json_number_valid(*text) || r_json_cursor_fail(cursor, R_STD_JSON_ERROR_TYPE);
}
bool r_json_cursor_integer(RStdJsonCursor *cursor,
                           bool quoted,
                           uint64_t positive_limit,
                           uint64_t negative_limit,
                           bool *negative,
                           uint64_t *magnitude) {
    RStdJsonByteView text;
    size_t at = 0U;
    uint64_t value = 0U, limit;
    if (!r_json_cursor_number_text(cursor, quoted, &text))
        return false;
    *negative = text.data[0] == '-';
    if (*negative)
        ++at;
    limit = *negative ? negative_limit : positive_limit;
    for (; at < text.length; ++at) {
        uint8_t c = text.data[at];
        uint64_t digit;
        if (c < '0' || c > '9')
            return r_json_cursor_fail(cursor, R_STD_JSON_ERROR_TYPE);
        digit = (uint64_t)(c - '0');
        if (digit > limit || value > (limit - digit) / 10U)
            return r_json_cursor_fail(cursor, R_STD_JSON_ERROR_RANGE);
        value = value * 10U + digit;
    }
    if (*negative && negative_limit == 0U)
        return r_json_cursor_fail(cursor, R_STD_JSON_ERROR_RANGE);
    *magnitude = value;
    return r_json_cursor_next(cursor);
}
bool r_json_cursor_float(RStdJsonCursor *cursor, bool quoted, bool binary32, double *value) {
    RStdJsonByteView text;
    RLibraryFloatParseResult parsed;
    if (!r_json_cursor_number_text(cursor, quoted, &text))
        return false;
    parsed = r_library_internal_float_parse((RStdStringView){text.data, text.length},
                                            binary32 ? R_LIBRARY_FLOAT_DESTINATION_F32
                                                     : R_LIBRARY_FLOAT_DESTINATION_F64);
    if (parsed.status != R_STD_CONVERT_CALL_SUCCESS)
        return r_json_cursor_fail(cursor, R_STD_JSON_ERROR_RANGE);
    *value = binary32 ? (double)parsed.value.f32 : parsed.value.f64;
    if (!isfinite(*value))
        return r_json_cursor_fail(cursor, R_STD_JSON_ERROR_RANGE);
    return r_json_cursor_next(cursor);
}
bool r_json_cursor_long_double(RStdJsonCursor *cursor, bool quoted, long double *value) {
    RStdJsonByteView text;
    if (!r_json_cursor_number_text(cursor, quoted, &text))
        return false;
    RLibraryFloatParseResult parsed = r_library_internal_float_parse(
        (RStdStringView){text.data, text.length}, R_LIBRARY_FLOAT_DESTINATION_C_LONG_DOUBLE);
    if (parsed.status != R_STD_CONVERT_CALL_SUCCESS || !isfinite(parsed.value.c_long_double))
        return r_json_cursor_fail(cursor, R_STD_JSON_ERROR_RANGE);
    *value = parsed.value.c_long_double;
    return r_json_cursor_next(cursor);
}
bool r_json_cursor_string(RStdJsonCursor *cursor, RStdString *value) {
    if (!r_json_cursor_expect(cursor, R_STD_JSON_TOKEN_STRING))
        return false;
    *value = r_json_scanner_take_text(&cursor->scanner);
    if (!r_json_cursor_next(cursor)) {
        r_runtime_string_destroy(value);
        return false;
    }
    return true;
}
bool r_json_cursor_boolean(RStdJsonCursor *cursor, bool *value) {
    if (!cursor->has_token || (cursor->token.kind != R_STD_JSON_TOKEN_TRUE &&
                               cursor->token.kind != R_STD_JSON_TOKEN_FALSE))
        return r_json_cursor_fail(cursor, R_STD_JSON_ERROR_TYPE);
    *value = cursor->token.kind == R_STD_JSON_TOKEN_TRUE;
    return r_json_cursor_next(cursor);
}
bool r_json_cursor_char(RStdJsonCursor *cursor, uint32_t *value) {
    RStdJsonByteView text;
    uint32_t scalar;
    size_t width;
    if (!r_json_cursor_expect(cursor, R_STD_JSON_TOKEN_STRING))
        return false;
    text = cursor->token.text;
    if (text.length == 0U)
        return r_json_cursor_fail(cursor, R_STD_JSON_ERROR_TYPE);
    /* The scanner has already validated UTF-8; exactly one scalar is required. */
    width = text.data[0] < 0x80U ? 1U : text.data[0] < 0xe0U ? 2U : text.data[0] < 0xf0U ? 3U : 4U;
    if (text.length != width)
        return r_json_cursor_fail(cursor, R_STD_JSON_ERROR_TYPE);
    scalar = text.data[0] & (width == 1U   ? 0x7fU
                             : width == 2U ? 0x1fU
                             : width == 3U ? 0x0fU
                                           : 0x07U);
    for (size_t i = 1U; i < width; ++i)
        scalar = (scalar << 6U) | (uint32_t)(text.data[i] & 0x3fU);
    *value = scalar;
    return r_json_cursor_next(cursor);
}
bool r_json_cursor_default_string(RStdJsonCursor *cursor,
                                  RStdString *value,
                                  RStdJsonByteView source) {
    if (cursor->outcome.status != R_STD_JSON_CALL_SUCCESS)
        return false;
    cursor->outcome = r_json_copy_text(value, cursor->allocator, source);
    return cursor->outcome.status == R_STD_JSON_CALL_SUCCESS;
}
RStdJsonResult r_json_cursor_finish(RStdJsonCursor *cursor) {
    RStdJsonResult result;
    if (cursor->outcome.status == R_STD_JSON_CALL_SUCCESS && !cursor->complete)
        (void)r_json_cursor_fail(cursor, R_STD_JSON_ERROR_TYPE);
    result = cursor->outcome;
    cursor->outcome = (RStdJsonResult){0};
    r_json_scanner_destroy(&cursor->scanner);
    return result;
}
bool r_json_cursor_array_push(RStdJsonCursor *cursor, RRuntimeArray *array, void *value) {
    if (cursor->outcome.status != R_STD_JSON_CALL_SUCCESS)
        return false;
    cursor->outcome = r_json_array_result(r_runtime_array_push(array, value));
    return cursor->outcome.status == R_STD_JSON_CALL_SUCCESS;
}
void r_json_cursor_destroy(RStdJsonCursor *cursor) {
    r_json_scanner_destroy(&cursor->scanner);
    r_json_error_destroy(&cursor->outcome.error);
    *cursor = (RStdJsonCursor){0};
}

bool r_json_cursor_number(RStdJsonCursor *cursor, RStdJsonNumber *value, bool quoted) {
    RStdJsonByteView text;
    if (!r_json_cursor_number_text(cursor, quoted, &text))
        return false;
    value->text = r_json_scanner_take_text(&cursor->scanner);
    if (!r_json_cursor_next(cursor)) {
        r_runtime_string_destroy(&value->text);
        return false;
    }
    return true;
}

#include "frontend_internal.h"

typedef struct RKeyword {
    const char *text;
    RTokenKind kind;
} RKeyword;

typedef struct RByteVector {
    uint8_t *items;
    size_t count;
    size_t capacity;
} RByteVector;

static const RKeyword r_keywords[] = {
    {"alignof", R_TOKEN_KW_ALIGNOF},
    {"ai8", R_TOKEN_KW_AI8},
    {"ai16", R_TOKEN_KW_AI16},
    {"ai32", R_TOKEN_KW_AI32},
    {"ai64", R_TOKEN_KW_AI64},
    {"aisize", R_TOKEN_KW_AISIZE},
    {"arc", R_TOKEN_KW_ARC},
    {"array", R_TOKEN_KW_ARRAY},
    {"as", R_TOKEN_KW_AS},
    {"async", R_TOKEN_KW_ASYNC},
    {"atomic", R_TOKEN_KW_ATOMIC},
    {"au8", R_TOKEN_KW_AU8},
    {"au16", R_TOKEN_KW_AU16},
    {"au32", R_TOKEN_KW_AU32},
    {"au64", R_TOKEN_KW_AU64},
    {"ausize", R_TOKEN_KW_AUSIZE},
    {"await", R_TOKEN_KW_AWAIT},
    {"bool", R_TOKEN_KW_BOOL},
    {"break", R_TOKEN_KW_BREAK},
    {"case", R_TOKEN_KW_CASE},
    {"catch", R_TOKEN_KW_CATCH},
    {"char", R_TOKEN_KW_CHAR},
    {"const", R_TOKEN_KW_CONST},
    {"constexpr", R_TOKEN_KW_CONSTEXPR},
    {"continue", R_TOKEN_KW_CONTINUE},
    {"default", R_TOKEN_KW_DEFAULT},
    {"dict", R_TOKEN_KW_DICT},
    {"drop", R_TOKEN_KW_DROP},
    {"dyn", R_TOKEN_KW_DYN},
    {"else", R_TOKEN_KW_ELSE},
    {"enum", R_TOKEN_KW_ENUM},
    {"extern", R_TOKEN_KW_EXTERN},
    {"f32", R_TOKEN_KW_F32},
    {"f64", R_TOKEN_KW_F64},
    {"false", R_TOKEN_KW_FALSE},
    {"fallthrough", R_TOKEN_KW_FALLTHROUGH},
    {"finally", R_TOKEN_KW_FINALLY},
    {"fn", R_TOKEN_KW_FN},
    {"for", R_TOKEN_KW_FOR},
    {"i8", R_TOKEN_KW_I8},
    {"i16", R_TOKEN_KW_I16},
    {"i32", R_TOKEN_KW_I32},
    {"i64", R_TOKEN_KW_I64},
    {"if", R_TOKEN_KW_IF},
    {"import", R_TOKEN_KW_IMPORT},
    {"isize", R_TOKEN_KW_ISIZE},
    {"list", R_TOKEN_KW_LIST},
    {"module", R_TOKEN_KW_MODULE},
    {"move", R_TOKEN_KW_MOVE},
    {"never", R_TOKEN_KW_NEVER},
    {"new", R_TOKEN_KW_NEW},
    {"null", R_TOKEN_KW_NULL},
    {"null_t", R_TOKEN_KW_NULL_T},
    {"o", R_TOKEN_KW_O},
    {"opaque", R_TOKEN_KW_OPAQUE},
    {"own", R_TOKEN_KW_OWN},
    {"panic", R_TOKEN_KW_PANIC},
    {"protected", R_TOKEN_KW_PROTECTED},
    {"raw", R_TOKEN_KW_RAW},
    {"rc", R_TOKEN_KW_RC},
    {"return", R_TOKEN_KW_RETURN},
    {"sizeof", R_TOKEN_KW_SIZEOF},
    {"static", R_TOKEN_KW_STATIC},
    {"str", R_TOKEN_KW_STR},
    {"struct", R_TOKEN_KW_STRUCT},
    {"switch", R_TOKEN_KW_SWITCH},
    {"task", R_TOKEN_KW_TASK},
    {"thread_local", R_TOKEN_KW_THREAD_LOCAL},
    {"thread_scope", R_TOKEN_KW_THREAD_SCOPE},
    {"throw", R_TOKEN_KW_THROW},
    {"throws", R_TOKEN_KW_THROWS},
    {"true", R_TOKEN_KW_TRUE},
    {"try", R_TOKEN_KW_TRY},
    {"u8", R_TOKEN_KW_U8},
    {"u16", R_TOKEN_KW_U16},
    {"u32", R_TOKEN_KW_U32},
    {"u64", R_TOKEN_KW_U64},
    {"unsafe", R_TOKEN_KW_UNSAFE},
    {"usize", R_TOKEN_KW_USIZE},
    {"variant", R_TOKEN_KW_VARIANT},
    {"void", R_TOKEN_KW_VOID},
    {"weak", R_TOKEN_KW_WEAK},
    {"while", R_TOKEN_KW_WHILE},
    {"impl", R_TOKEN_KW_IMPL},
    {"Self", R_TOKEN_KW_SELF},
    {"this", R_TOKEN_KW_THIS},
    {"trait", R_TOKEN_KW_TRAIT},
    {"auto", R_TOKEN_KW_AUTO},
    {"in", R_TOKEN_KW_IN},
    {"c_char", R_TOKEN_KW_C_CHAR},
    {"c_schar", R_TOKEN_KW_C_SCHAR},
    {"c_uchar", R_TOKEN_KW_C_UCHAR},
    {"c_short", R_TOKEN_KW_C_SHORT},
    {"c_ushort", R_TOKEN_KW_C_USHORT},
    {"c_int", R_TOKEN_KW_C_INT},
    {"c_uint", R_TOKEN_KW_C_UINT},
    {"c_long", R_TOKEN_KW_C_LONG},
    {"c_ulong", R_TOKEN_KW_C_ULONG},
    {"c_llong", R_TOKEN_KW_C_LLONG},
    {"c_ullong", R_TOKEN_KW_C_ULLONG},
    {"c_bool", R_TOKEN_KW_C_BOOL},
    {"c_wchar", R_TOKEN_KW_C_WCHAR},
    {"c_wint", R_TOKEN_KW_C_WINT},
    {"c_int8", R_TOKEN_KW_C_INT8},
    {"c_uint8", R_TOKEN_KW_C_UINT8},
    {"c_int16", R_TOKEN_KW_C_INT16},
    {"c_uint16", R_TOKEN_KW_C_UINT16},
    {"c_int32", R_TOKEN_KW_C_INT32},
    {"c_uint32", R_TOKEN_KW_C_UINT32},
    {"c_int64", R_TOKEN_KW_C_INT64},
    {"c_uint64", R_TOKEN_KW_C_UINT64},
    {"c_intptr", R_TOKEN_KW_C_INTPTR},
    {"c_uintptr", R_TOKEN_KW_C_UINTPTR},
    {"c_intmax", R_TOKEN_KW_C_INTMAX},
    {"c_uintmax", R_TOKEN_KW_C_UINTMAX},
    {"c_float", R_TOKEN_KW_C_FLOAT},
    {"c_double", R_TOKEN_KW_C_DOUBLE},
    {"c_long_double", R_TOKEN_KW_C_LONG_DOUBLE},
    {"c_size", R_TOKEN_KW_C_SIZE},
    {"c_ptrdiff", R_TOKEN_KW_C_PTRDIFF},
};

static RSourceSpan r_lex_span(RSourceId source, size_t start, size_t end) {
    RSourceSpan span;
    span.source = source;
    span.start = (uint32_t)start;
    span.end = (uint32_t)end;
    return span;
}

static bool r_byte_vector_push(RFrontendContext *context, RByteVector *vector, uint8_t byte) {
    if (!r_grow_array(context,
                      (void **)&vector->items,
                      &vector->capacity,
                      sizeof(*vector->items),
                      vector->count + 1U)) {
        return false;
    }
    vector->items[vector->count] = byte;
    vector->count += 1U;
    return true;
}

static bool r_byte_vector_append(RFrontendContext *context,
                                 RByteVector *vector,
                                 const uint8_t *bytes,
                                 size_t length) {
    if (length == 0U) {
        return true;
    }
    if ((bytes == NULL) || (vector->count > (SIZE_MAX - length)) ||
        !r_grow_array(context,
                      (void **)&vector->items,
                      &vector->capacity,
                      sizeof(*vector->items),
                      vector->count + length)) {
        return false;
    }
    (void)memcpy(vector->items + vector->count, bytes, length);
    vector->count += length;
    return true;
}

static bool r_is_ascii_identifier_continue(uint8_t byte) {
    return ((byte >= (uint8_t)'a') && (byte <= (uint8_t)'z')) ||
           ((byte >= (uint8_t)'A') && (byte <= (uint8_t)'Z')) ||
           ((byte >= (uint8_t)'0') && (byte <= (uint8_t)'9')) || (byte == (uint8_t)'_');
}

static bool r_is_forbidden_unicode_whitespace(uint32_t code_point) {
    return (code_point == UINT32_C(0x0085)) || (code_point == UINT32_C(0x00A0)) ||
           (code_point == UINT32_C(0x1680)) ||
           ((code_point >= UINT32_C(0x2000)) && (code_point <= UINT32_C(0x200A))) ||
           (code_point == UINT32_C(0x2028)) || (code_point == UINT32_C(0x2029)) ||
           (code_point == UINT32_C(0x202F)) || (code_point == UINT32_C(0x205F)) ||
           (code_point == UINT32_C(0x3000));
}

static RTokenKind r_keyword_kind(const uint8_t *bytes, size_t length) {
    size_t index;

    for (index = 0U; index < (sizeof(r_keywords) / sizeof(r_keywords[0])); ++index) {
        size_t keyword_length = strlen(r_keywords[index].text);
        if ((keyword_length == length) && (memcmp(bytes, r_keywords[index].text, length) == 0)) {
            return r_keywords[index].kind;
        }
    }
    return R_TOKEN_IDENTIFIER;
}

static bool r_scan_identifier(RFrontendContext *context,
                              RSource *source,
                              RSourceId source_id,
                              size_t start,
                              size_t initial_length,
                              bool non_ascii_start,
                              size_t *end,
                              RTokenKind *kind) {
    size_t offset = start;
    bool valid_start = true;
    bool contains_non_ascii = non_ascii_start;

    if (non_ascii_start) {
        uint32_t code_point;
        size_t width;
        if (!r_utf8_decode(source->bytes, source->length, offset, &code_point, &width)) {
            *end = start + 1U;
            *kind = R_TOKEN_ERROR;
            (void)r_add_diagnostic(context,
                                   "R-DIAG-LEX-001",
                                   "R-LEX-0001",
                                   "malformed UTF-8 sequence",
                                   R_DIAGNOSTIC_ERROR,
                                   r_lex_span(source_id, start, *end));
            return true;
        }
        if (code_point == UINT32_C(0xFEFF)) {
            *end = start + width;
            *kind = R_TOKEN_ERROR;
            (void)r_add_diagnostic(context,
                                   "R-DIAG-LEX-001",
                                   "R-LEX-0002",
                                   "U+FEFF is permitted only as the first code point",
                                   R_DIAGNOSTIC_ERROR,
                                   r_lex_span(source_id, start, *end));
            return true;
        }
        if (r_is_forbidden_unicode_whitespace(code_point)) {
            *end = start + width;
            *kind = R_TOKEN_ERROR;
            (void)r_add_diagnostic(context,
                                   "R-DIAG-LEX-004",
                                   "R-LEX-0004",
                                   "Unicode whitespace is not an R whitespace token",
                                   R_DIAGNOSTIC_ERROR,
                                   r_lex_span(source_id, start, *end));
            return true;
        }
        valid_start = r_unicode_is_xid_start(code_point);
        offset += width;
    } else {
        offset += initial_length;
    }

    while (offset < source->length) {
        uint8_t byte = source->bytes[offset];
        if (byte < UINT8_C(0x80)) {
            if (!r_is_ascii_identifier_continue(byte)) {
                break;
            }
            offset += 1U;
        } else {
            uint32_t code_point;
            size_t width;
            if (!r_utf8_decode(source->bytes, source->length, offset, &code_point, &width) ||
                !r_unicode_is_xid_continue(code_point)) {
                break;
            }
            contains_non_ascii = true;
            offset += width;
        }
    }
    *end = offset;
    if (!valid_start) {
        *kind = R_TOKEN_ERROR;
        (void)r_add_diagnostic(context,
                               "R-DIAG-LEX-003",
                               "R-LEX-0006",
                               "identifier does not begin with XID_Start or underscore",
                               R_DIAGNOSTIC_ERROR,
                               r_lex_span(source_id, start, offset));
        return true;
    }
    if (contains_non_ascii) {
        bool is_nfc = false;
        if (!r_unicode_sequence_is_nfc(context, source->bytes + start, offset - start, &is_nfc)) {
            return false;
        }
        if (!is_nfc) {
            (void)r_add_diagnostic(context,
                                   "R-DIAG-LEX-003",
                                   "R-LEX-0006",
                                   "identifier is not in Unicode NFC",
                                   R_DIAGNOSTIC_ERROR,
                                   r_lex_span(source_id, start, offset));
        }
    }
    *kind = non_ascii_start ? R_TOKEN_IDENTIFIER
                            : r_keyword_kind(source->bytes + start, offset - start);
    return true;
}

static bool r_is_digit_for_base(uint8_t byte, uint32_t base) {
    if ((byte >= (uint8_t)'0') && (byte <= (uint8_t)'9')) {
        return (uint32_t)(byte - (uint8_t)'0') < base;
    }
    if ((base == UINT32_C(16)) && (((byte >= (uint8_t)'a') && (byte <= (uint8_t)'f')) ||
                                   ((byte >= (uint8_t)'A') && (byte <= (uint8_t)'F')))) {
        return true;
    }
    return false;
}

static size_t r_scan_digit_sequence(
    const RSource *source, size_t offset, uint32_t base, bool *has_digit, bool *valid_separators) {
    bool previous_was_digit = false;

    *has_digit = false;
    while (offset < source->length) {
        uint8_t byte = source->bytes[offset];
        if (r_is_digit_for_base(byte, base)) {
            *has_digit = true;
            previous_was_digit = true;
            offset += 1U;
        } else if (byte == (uint8_t)'_') {
            if (!previous_was_digit || ((offset + 1U) >= source->length) ||
                !r_is_digit_for_base(source->bytes[offset + 1U], base)) {
                *valid_separators = false;
            }
            previous_was_digit = false;
            offset += 1U;
        } else {
            break;
        }
    }
    return offset;
}

static bool r_text_at(const RSource *source, size_t offset, const char *text) {
    size_t length = strlen(text);
    return (offset <= source->length) && (length <= (source->length - offset)) &&
           (memcmp(source->bytes + offset, text, length) == 0);
}

static size_t r_consume_integer_suffix(const RSource *source, size_t offset) {
    static const char *const suffixes[] = {
        "isize",
        "usize",
        "i16",
        "u16",
        "i32",
        "u32",
        "i64",
        "u64",
        "i8",
        "u8",
    };
    size_t index;

    for (index = 0U; index < (sizeof(suffixes) / sizeof(suffixes[0])); ++index) {
        if (r_text_at(source, offset, suffixes[index])) {
            return offset + strlen(suffixes[index]);
        }
    }
    return offset;
}

static size_t r_consume_float_suffix(const RSource *source, size_t offset) {
    if (r_text_at(source, offset, "f32") || r_text_at(source, offset, "f64")) {
        return offset + 3U;
    }
    return offset;
}

static bool r_scan_exponent(const RSource *source,
                            size_t *offset,
                            uint8_t lower_marker,
                            uint8_t upper_marker,
                            bool *valid) {
    size_t current = *offset;
    bool has_digit;
    bool separators_valid = true;

    if ((current >= source->length) ||
        ((source->bytes[current] != lower_marker) && (source->bytes[current] != upper_marker))) {
        return false;
    }
    current += 1U;
    if ((current < source->length) &&
        ((source->bytes[current] == (uint8_t)'+') || (source->bytes[current] == (uint8_t)'-'))) {
        current += 1U;
    }
    current = r_scan_digit_sequence(source, current, UINT32_C(10), &has_digit, &separators_valid);
    if (!has_digit || !separators_valid) {
        *valid = false;
    }
    *offset = current;
    return true;
}

/* R-TYPE-0052 (L18.1): a number right after `.` or `->` selects a tuple element, so `t.0.1`
   is two selections rather than a floating literal. */
static bool r_scan_after_member_access(const RSource *source) {
    for (size_t index = source->token_count; index != 0U; --index) {
        const RTokenKind previous = source->tokens[index - 1U].kind;
        if ((previous == R_TOKEN_WHITESPACE) || (previous == R_TOKEN_LINE_COMMENT) ||
            (previous == R_TOKEN_BLOCK_COMMENT)) {
            continue;
        }
        return (previous == R_TOKEN_DOT) || (previous == R_TOKEN_ARROW);
    }
    return false;
}

static bool r_scan_number(RFrontendContext *context,
                          RSource *source,
                          RSourceId source_id,
                          size_t start,
                          size_t *end,
                          RTokenKind *kind) {
    size_t offset = start;
    uint32_t base = UINT32_C(10);
    bool has_digit = false;
    bool valid = true;
    bool is_float = false;
    bool has_hex_prefix = false;

    if (((start + 1U) < source->length) && (source->bytes[start] == (uint8_t)'0')) {
        uint8_t prefix = source->bytes[start + 1U];
        if ((prefix == (uint8_t)'b') || (prefix == (uint8_t)'B')) {
            base = UINT32_C(2);
            offset += 2U;
        } else if ((prefix == (uint8_t)'o') || (prefix == (uint8_t)'O')) {
            base = UINT32_C(8);
            offset += 2U;
        } else if ((prefix == (uint8_t)'x') || (prefix == (uint8_t)'X')) {
            base = UINT32_C(16);
            has_hex_prefix = true;
            offset += 2U;
        }
    }
    offset = r_scan_digit_sequence(source, offset, base, &has_digit, &valid);
    if (!has_digit) {
        valid = false;
    }

    if (has_hex_prefix) {
        if ((offset < source->length) && (source->bytes[offset] == (uint8_t)'.') &&
            ((offset + 1U) < source->length) && (source->bytes[offset + 1U] != (uint8_t)'.')) {
            bool fractional_digit;
            bool fractional_valid = true;
            is_float = true;
            offset += 1U;
            offset = r_scan_digit_sequence(
                source, offset, UINT32_C(16), &fractional_digit, &fractional_valid);
            valid = valid && fractional_valid;
        }
        if (r_scan_exponent(source, &offset, (uint8_t)'p', (uint8_t)'P', &valid)) {
            is_float = true;
        } else if (is_float) {
            valid = false;
        }
    } else if (base == UINT32_C(10)) {
        if (!r_scan_after_member_access(source) && (offset < source->length) &&
            (source->bytes[offset] == (uint8_t)'.') && ((offset + 1U) < source->length) &&
            (source->bytes[offset + 1U] >= (uint8_t)'0') &&
            (source->bytes[offset + 1U] <= (uint8_t)'9')) {
            bool fractional_digit;
            bool fractional_valid = true;
            is_float = true;
            offset += 1U;
            offset = r_scan_digit_sequence(
                source, offset, UINT32_C(10), &fractional_digit, &fractional_valid);
            valid = valid && fractional_digit && fractional_valid;
        }
        if (r_scan_exponent(source, &offset, (uint8_t)'e', (uint8_t)'E', &valid)) {
            is_float = true;
        }
    }

    offset = is_float ? r_consume_float_suffix(source, offset)
                      : r_consume_integer_suffix(source, offset);
    *end = offset;
    *kind = is_float ? R_TOKEN_FLOAT_LITERAL : R_TOKEN_INTEGER_LITERAL;
    if (!valid) {
        *kind = R_TOKEN_ERROR;
        (void)r_add_diagnostic(context,
                               "R-DIAG-LEX-002",
                               is_float ? "R-LEX-0011" : "R-LEX-0009",
                               "malformed numeric literal",
                               R_DIAGNOSTIC_ERROR,
                               r_lex_span(source_id, start, offset));
    }
    return true;
}

static int r_hex_value(uint8_t byte) {
    if ((byte >= (uint8_t)'0') && (byte <= (uint8_t)'9')) {
        return (int)(byte - (uint8_t)'0');
    }
    if ((byte >= (uint8_t)'a') && (byte <= (uint8_t)'f')) {
        return (int)(byte - (uint8_t)'a') + 10;
    }
    if ((byte >= (uint8_t)'A') && (byte <= (uint8_t)'F')) {
        return (int)(byte - (uint8_t)'A') + 10;
    }
    return -1;
}

static bool r_character_literal_append_byte(uint8_t *bytes, size_t *length, uint8_t byte) {
    if ((bytes == NULL) || (length == NULL) || (*length >= 4U)) {
        return false;
    }
    bytes[*length] = byte;
    *length += 1U;
    return true;
}

static bool r_character_literal_append_scalar(uint8_t *bytes, size_t *length, uint32_t code_point) {
    if (code_point <= UINT32_C(0x7F)) {
        return r_character_literal_append_byte(bytes, length, (uint8_t)code_point);
    }
    if (code_point <= UINT32_C(0x7FF)) {
        return r_character_literal_append_byte(
                   bytes, length, (uint8_t)(UINT32_C(0xC0) | (code_point >> 6U))) &&
               r_character_literal_append_byte(
                   bytes, length, (uint8_t)(UINT32_C(0x80) | (code_point & UINT32_C(0x3F))));
    }
    if ((code_point >= UINT32_C(0xD800)) && (code_point <= UINT32_C(0xDFFF))) {
        return false;
    }
    if (code_point <= UINT32_C(0xFFFF)) {
        return r_character_literal_append_byte(
                   bytes, length, (uint8_t)(UINT32_C(0xE0) | (code_point >> 12U))) &&
               r_character_literal_append_byte(
                   bytes,
                   length,
                   (uint8_t)(UINT32_C(0x80) | ((code_point >> 6U) & UINT32_C(0x3F)))) &&
               r_character_literal_append_byte(
                   bytes, length, (uint8_t)(UINT32_C(0x80) | (code_point & UINT32_C(0x3F))));
    }
    if (code_point <= UINT32_C(0x10FFFF)) {
        return r_character_literal_append_byte(
                   bytes, length, (uint8_t)(UINT32_C(0xF0) | (code_point >> 18U))) &&
               r_character_literal_append_byte(
                   bytes,
                   length,
                   (uint8_t)(UINT32_C(0x80) | ((code_point >> 12U) & UINT32_C(0x3F)))) &&
               r_character_literal_append_byte(
                   bytes,
                   length,
                   (uint8_t)(UINT32_C(0x80) | ((code_point >> 6U) & UINT32_C(0x3F)))) &&
               r_character_literal_append_byte(
                   bytes, length, (uint8_t)(UINT32_C(0x80) | (code_point & UINT32_C(0x3F))));
    }
    return false;
}

bool r_decode_character_literal(const RFrontendContext *context,
                                RSourceSpan span,
                                uint32_t *code_point) {
    const RSource *source = r_get_source_const(context, span.source);
    uint8_t decoded[4] = {0};
    size_t decoded_length = 0U;
    size_t offset;
    size_t end;

    if ((source == NULL) || (code_point == NULL) || (span.end < span.start)) {
        return false;
    }
    offset = (size_t)span.start;
    end = (size_t)span.end;
    if ((end > source->length) || ((end - offset) < 3U) ||
        (source->bytes[offset] != (uint8_t)'\'') || (source->bytes[end - 1U] != (uint8_t)'\'')) {
        return false;
    }
    offset += 1U;
    end -= 1U;
    while (offset < end) {
        uint8_t byte = source->bytes[offset];

        if ((byte == (uint8_t)'\n') || (byte == (uint8_t)'\r')) {
            return false;
        }
        if (byte != (uint8_t)'\\') {
            uint32_t decoded_code_point;
            size_t width;
            size_t index;

            if (!r_utf8_decode(source->bytes, end, offset, &decoded_code_point, &width)) {
                return false;
            }
            (void)decoded_code_point;
            for (index = 0U; index < width; ++index) {
                if (!r_character_literal_append_byte(
                        decoded, &decoded_length, source->bytes[offset + index])) {
                    return false;
                }
            }
            offset += width;
            continue;
        }

        offset += 1U;
        if (offset >= end) {
            return false;
        }
        byte = source->bytes[offset];
        if ((byte == (uint8_t)'\\') || (byte == (uint8_t)'"') || (byte == (uint8_t)'\'')) {
            if (!r_character_literal_append_byte(decoded, &decoded_length, byte)) {
                return false;
            }
            offset += 1U;
        } else if ((byte == (uint8_t)'n') || (byte == (uint8_t)'r') || (byte == (uint8_t)'t') ||
                   (byte == (uint8_t)'0')) {
            uint8_t escaped = UINT8_C(0);

            if (byte == (uint8_t)'n') {
                escaped = (uint8_t)'\n';
            } else if (byte == (uint8_t)'r') {
                escaped = (uint8_t)'\r';
            } else if (byte == (uint8_t)'t') {
                escaped = (uint8_t)'\t';
            }
            if (!r_character_literal_append_byte(decoded, &decoded_length, escaped)) {
                return false;
            }
            offset += 1U;
        } else if (byte == (uint8_t)'x') {
            int high;
            int low;
            uint8_t escaped;

            if ((end - offset) < 3U) {
                return false;
            }
            high = r_hex_value(source->bytes[offset + 1U]);
            low = r_hex_value(source->bytes[offset + 2U]);
            if ((high < 0) || (low < 0)) {
                return false;
            }
            escaped = (uint8_t)((high << 4) | low);
            if ((escaped > UINT8_C(0x7F)) ||
                !r_character_literal_append_byte(decoded, &decoded_length, escaped)) {
                return false;
            }
            offset += 3U;
        } else if ((byte == (uint8_t)'u') && ((end - offset) >= 4U) &&
                   (source->bytes[offset + 1U] == (uint8_t)'{')) {
            uint32_t escaped_code_point = UINT32_C(0);
            uint32_t digits = UINT32_C(0);

            offset += 2U;
            while ((offset < end) && (source->bytes[offset] != (uint8_t)'}')) {
                const int value = r_hex_value(source->bytes[offset]);

                if ((value < 0) || (digits >= UINT32_C(6))) {
                    return false;
                }
                escaped_code_point = (escaped_code_point << 4U) | (uint32_t)value;
                digits += 1U;
                offset += 1U;
            }
            if ((offset >= end) || (source->bytes[offset] != (uint8_t)'}') ||
                (digits == UINT32_C(0)) ||
                !r_character_literal_append_scalar(decoded, &decoded_length, escaped_code_point)) {
                return false;
            }
            offset += 1U;
        } else {
            return false;
        }
    }
    {
        size_t width;

        if (!r_utf8_decode(decoded, decoded_length, 0U, code_point, &width) ||
            (width != decoded_length)) {
            return false;
        }
    }
    return true;
}

static bool r_append_utf8(RFrontendContext *context, RByteVector *output, uint32_t code_point) {
    if (code_point <= UINT32_C(0x7F)) {
        return r_byte_vector_push(context, output, (uint8_t)code_point);
    }
    if (code_point <= UINT32_C(0x7FF)) {
        return r_byte_vector_push(
                   context, output, (uint8_t)(UINT32_C(0xC0) | (code_point >> 6U))) &&
               r_byte_vector_push(
                   context, output, (uint8_t)(UINT32_C(0x80) | (code_point & UINT32_C(0x3F))));
    }
    if ((code_point >= UINT32_C(0xD800)) && (code_point <= UINT32_C(0xDFFF))) {
        return false;
    }
    if (code_point <= UINT32_C(0xFFFF)) {
        return r_byte_vector_push(
                   context, output, (uint8_t)(UINT32_C(0xE0) | (code_point >> 12U))) &&
               r_byte_vector_push(
                   context,
                   output,
                   (uint8_t)(UINT32_C(0x80) | ((code_point >> 6U) & UINT32_C(0x3F)))) &&
               r_byte_vector_push(
                   context, output, (uint8_t)(UINT32_C(0x80) | (code_point & UINT32_C(0x3F))));
    }
    if (code_point <= UINT32_C(0x10FFFF)) {
        return r_byte_vector_push(
                   context, output, (uint8_t)(UINT32_C(0xF0) | (code_point >> 18U))) &&
               r_byte_vector_push(
                   context,
                   output,
                   (uint8_t)(UINT32_C(0x80) | ((code_point >> 12U) & UINT32_C(0x3F)))) &&
               r_byte_vector_push(
                   context,
                   output,
                   (uint8_t)(UINT32_C(0x80) | ((code_point >> 6U) & UINT32_C(0x3F)))) &&
               r_byte_vector_push(
                   context, output, (uint8_t)(UINT32_C(0x80) | (code_point & UINT32_C(0x3F))));
    }
    return false;
}

static bool r_literal_error(RFrontendContext *context,
                            RSourceId source_id,
                            size_t start,
                            size_t end,
                            const char *message,
                            bool report) {
    if (report) {
        (void)r_add_diagnostic(context,
                               "R-DIAG-LEX-002",
                               "R-LEX-0012",
                               message,
                               R_DIAGNOSTIC_ERROR,
                               r_lex_span(source_id, start, end));
    }
    return true;
}

static bool r_decode_literal(RFrontendContext *context,
                             const RSource *source,
                             RSourceId source_id,
                             size_t start,
                             uint8_t quote,
                             RByteVector *decoded,
                             size_t *end,
                             bool *closed,
                             bool report_errors) {
    size_t offset = start + 1U;
    bool valid = true;

    *closed = false;
    while (offset < source->length) {
        uint8_t byte = source->bytes[offset];
        if (byte == quote) {
            offset += 1U;
            *closed = true;
            break;
        }
        if ((byte == (uint8_t)'\n') || (byte == (uint8_t)'\r')) {
            valid = false;
            (void)r_literal_error(
                context, source_id, start, offset, "unterminated literal", report_errors);
            break;
        }
        if (byte != (uint8_t)'\\') {
            if (byte < UINT8_C(0x80)) {
                if (!r_byte_vector_push(context, decoded, byte)) {
                    return false;
                }
                offset += 1U;
            } else {
                uint32_t code_point;
                size_t width;
                if (!r_utf8_decode(source->bytes, source->length, offset, &code_point, &width)) {
                    valid = false;
                    (void)r_literal_error(context,
                                          source_id,
                                          offset,
                                          offset + 1U,
                                          "malformed UTF-8 in literal",
                                          report_errors);
                    offset += 1U;
                } else {
                    if (!r_byte_vector_append(context, decoded, source->bytes + offset, width)) {
                        return false;
                    }
                    offset += width;
                }
            }
            continue;
        }

        offset += 1U;
        if (offset >= source->length) {
            break;
        }
        byte = source->bytes[offset];
        if ((byte == (uint8_t)'\\') || (byte == (uint8_t)'\"') || (byte == (uint8_t)'\'')) {
            if (!r_byte_vector_push(context, decoded, byte)) {
                return false;
            }
            offset += 1U;
        } else if ((byte == (uint8_t)'n') || (byte == (uint8_t)'r') || (byte == (uint8_t)'t') ||
                   (byte == (uint8_t)'0')) {
            uint8_t escaped = UINT8_C(0);
            if (byte == (uint8_t)'n') {
                escaped = (uint8_t)'\n';
            } else if (byte == (uint8_t)'r') {
                escaped = (uint8_t)'\r';
            } else if (byte == (uint8_t)'t') {
                escaped = (uint8_t)'\t';
            }
            if (!r_byte_vector_push(context, decoded, escaped)) {
                return false;
            }
            offset += 1U;
        } else if (byte == (uint8_t)'x') {
            int high;
            int low;
            uint8_t escaped;
            if ((offset + 2U) >= source->length) {
                valid = false;
                offset += 1U;
                (void)r_literal_error(context,
                                      source_id,
                                      start,
                                      offset,
                                      "\\x escape requires exactly two hexadecimal digits",
                                      report_errors);
                continue;
            }
            high = r_hex_value(source->bytes[offset + 1U]);
            low = r_hex_value(source->bytes[offset + 2U]);
            if ((high < 0) || (low < 0)) {
                valid = false;
                (void)r_literal_error(context,
                                      source_id,
                                      offset - 1U,
                                      offset + 3U,
                                      "invalid hexadecimal escape",
                                      report_errors);
            } else {
                escaped = (uint8_t)((high << 4) | low);
                if ((quote == (uint8_t)'\'') && (escaped > UINT8_C(0x7F))) {
                    valid = false;
                    if (report_errors) {
                        (void)r_add_diagnostic(
                            context,
                            "R-DIAG-LEX-002",
                            "R-LEX-0013",
                            "character hexadecimal escape must encode a complete single-byte "
                            "Unicode scalar",
                            R_DIAGNOSTIC_ERROR,
                            r_lex_span(source_id, offset - 1U, offset + 3U));
                    }
                } else if (!r_byte_vector_push(context, decoded, escaped)) {
                    return false;
                }
            }
            offset += 3U;
        } else if ((byte == (uint8_t)'u') && ((offset + 1U) < source->length) &&
                   (source->bytes[offset + 1U] == (uint8_t)'{')) {
            uint32_t code_point = UINT32_C(0);
            uint32_t digits = UINT32_C(0);
            offset += 2U;
            while ((offset < source->length) && (source->bytes[offset] != (uint8_t)'}')) {
                int value = r_hex_value(source->bytes[offset]);
                if ((value < 0) || (digits >= UINT32_C(6))) {
                    valid = false;
                    break;
                }
                code_point = (code_point << 4U) | (uint32_t)value;
                digits += 1U;
                offset += 1U;
            }
            if ((offset >= source->length) || (source->bytes[offset] != (uint8_t)'}') ||
                (digits == UINT32_C(0)) || (code_point > UINT32_C(0x10FFFF)) ||
                ((code_point >= UINT32_C(0xD800)) && (code_point <= UINT32_C(0xDFFF)))) {
                valid = false;
                (void)r_literal_error(
                    context, source_id, start, offset, "invalid Unicode escape", report_errors);
                while ((offset < source->length) && (source->bytes[offset] != (uint8_t)'}') &&
                       (source->bytes[offset] != quote) &&
                       (source->bytes[offset] != (uint8_t)'\n')) {
                    offset += 1U;
                }
                if ((offset < source->length) && (source->bytes[offset] == (uint8_t)'}')) {
                    offset += 1U;
                }
            } else {
                offset += 1U;
                if (!r_append_utf8(context, decoded, code_point)) {
                    return false;
                }
            }
        } else {
            valid = false;
            (void)r_literal_error(context,
                                  source_id,
                                  offset - 1U,
                                  offset + 1U,
                                  "unknown escape sequence",
                                  report_errors);
            offset += 1U;
        }
    }
    if (!*closed && valid) {
        (void)r_literal_error(
            context, source_id, start, offset, "unterminated literal", report_errors);
    }
    *end = offset;
    return context->resource_status != R_FRONTEND_OUT_OF_MEMORY;
}

static bool r_decoded_utf8_is_valid(const uint8_t *bytes, size_t length, size_t *scalar_count) {
    size_t offset = 0U;
    size_t count = 0U;

    while (offset < length) {
        uint32_t code_point;
        size_t width;
        if (!r_utf8_decode(bytes, length, offset, &code_point, &width)) {
            return false;
        }
        (void)code_point;
        offset += width;
        count += 1U;
    }
    if (scalar_count != NULL) {
        *scalar_count = count;
    }
    return true;
}

static bool r_scan_literal(RFrontendContext *context,
                           RSource *source,
                           RSourceId source_id,
                           size_t start,
                           uint8_t quote,
                           size_t *end,
                           RTokenKind *kind) {
    RByteVector decoded = {0};
    bool closed;
    bool result;

    result =
        r_decode_literal(context, source, source_id, start, quote, &decoded, end, &closed, true);
    *kind = (quote == (uint8_t)'\"') ? R_TOKEN_STRING_LITERAL : R_TOKEN_CHARACTER_LITERAL;
    if (result && (quote == (uint8_t)'\'')) {
        size_t scalar_count = 0U;
        if (!closed || !r_decoded_utf8_is_valid(decoded.items, decoded.count, &scalar_count) ||
            (scalar_count != 1U)) {
            (void)r_add_diagnostic(context,
                                   "R-DIAG-LEX-002",
                                   "R-LEX-0012",
                                   "character literal must contain exactly one Unicode scalar",
                                   R_DIAGNOSTIC_ERROR,
                                   r_lex_span(source_id, start, *end));
        }
    }
    r_context_free(context, decoded.items);
    return result;
}

#include "format_literal.inc"

static bool
r_validate_string_chunk(RFrontendContext *context, RByteVector *decoded, RSourceSpan span) {
    if (!r_decoded_utf8_is_valid(decoded->items, decoded->count, NULL)) {
        return r_add_diagnostic(context,
                                "R-DIAG-LEX-002",
                                "R-LEX-0012",
                                "concatenated string text is not well-formed UTF-8",
                                R_DIAGNOSTIC_ERROR,
                                span);
    }
    return true;
}

static bool
r_validate_string_sequences(RFrontendContext *context, RSource *source, RSourceId source_id) {
    size_t index = 0U;
    while (index < source->token_count) {
        RByteVector decoded = {0};
        RSourceSpan span = source->tokens[index].span;
        bool inside_format = false;
        bool inside_slot = false;
        if (source->tokens[index].kind != R_TOKEN_STRING_LITERAL &&
            source->tokens[index].kind != R_TOKEN_FORMAT_START) {
            ++index;
            continue;
        }
        while (index < source->token_count) {
            const RToken *token = &source->tokens[index];
            span.end = token->span.end;
            if (inside_slot) {
                if (token->kind == R_TOKEN_RBRACE) {
                    inside_slot = false;
                    span.start = span.end;
                } else if (token->kind == R_TOKEN_FORMAT_END || token->kind == R_TOKEN_EOF) {
                    inside_slot = false;
                    continue;
                }
            } else if (token->kind == R_TOKEN_STRING_LITERAL) {
                RByteVector part = {0};
                size_t end;
                bool closed;
                bool valid = r_decode_literal(context,
                                              source,
                                              source_id,
                                              token->span.start,
                                              (uint8_t)'"',
                                              &part,
                                              &end,
                                              &closed,
                                              false) &&
                             r_byte_vector_append(context, &decoded, part.items, part.count);
                r_context_free(context, part.items);
                if (!valid) {
                    r_context_free(context, decoded.items);
                    return false;
                }
            } else if (token->kind == R_TOKEN_FORMAT_START) {
                inside_format = true;
            } else if (inside_format && token->kind == R_TOKEN_FORMAT_TEXT) {
                const RInternEntry *part = &context->intern_entries[token->intern_id - 1U];
                if (!r_byte_vector_append(
                        context, &decoded, (const uint8_t *)part->bytes, part->length)) {
                    r_context_free(context, decoded.items);
                    return false;
                }
            } else if (inside_format && token->kind == R_TOKEN_LBRACE) {
                if (!r_validate_string_chunk(context, &decoded, span)) {
                    r_context_free(context, decoded.items);
                    return false;
                }
                decoded.count = 0U;
                inside_slot = true;
            } else if (inside_format && token->kind == R_TOKEN_FORMAT_END) {
                inside_format = false;
            } else if (!r_token_is_trivia(token->kind)) {
                break;
            }
            ++index;
        }
        if (!r_validate_string_chunk(context, &decoded, span)) {
            r_context_free(context, decoded.items);
            return false;
        }
        r_context_free(context, decoded.items);
    }
    return true;
}

bool r_intern_string_sequence(RFrontendContext *context,
                              RSourceSpan span,
                              uint32_t *intern_id,
                              size_t *decoded_length) {
    const RSource *source;
    RByteVector decoded = {0};
    size_t token_index;
    bool found = false;
    bool success = false;

    if ((context == NULL) || (intern_id == NULL) || (decoded_length == NULL) ||
        (span.end < span.start)) {
        return false;
    }
    source = r_get_source_const(context, span.source);
    if ((source == NULL) || ((size_t)span.end > source->length)) {
        return false;
    }
    for (token_index = 0U; token_index < source->token_count; ++token_index) {
        const RToken *token = &source->tokens[token_index];
        RByteVector part = {0};
        size_t end;
        bool closed;

        if ((token->span.start < span.start) || (token->span.end > span.end)) {
            continue;
        }
        if (r_token_is_trivia(token->kind)) {
            continue;
        }
        if (token->kind != R_TOKEN_STRING_LITERAL) {
            goto cleanup;
        }
        if (!r_decode_literal(context,
                              source,
                              span.source,
                              token->span.start,
                              (uint8_t)'"',
                              &part,
                              &end,
                              &closed,
                              false) ||
            !closed || (end != token->span.end) ||
            !r_byte_vector_append(context, &decoded, part.items, part.count)) {
            r_context_free(context, part.items);
            goto cleanup;
        }
        r_context_free(context, part.items);
        found = true;
    }
    if (!found || !r_intern_bytes(context, decoded.items, decoded.count, intern_id)) {
        goto cleanup;
    }
    *decoded_length = decoded.count;
    success = true;

cleanup:
    r_context_free(context, decoded.items);
    return success;
}

static void r_warn_lone_carriage_returns(RFrontendContext *context,
                                         const RSource *source,
                                         RSourceId source_id,
                                         size_t start,
                                         size_t end) {
    size_t offset;
    for (offset = start; offset < end; ++offset) {
        if ((source->bytes[offset] == (uint8_t)'\r') &&
            (((offset + 1U) >= end) || (source->bytes[offset + 1U] != (uint8_t)'\n'))) {
            (void)r_add_diagnostic(context,
                                   "R-WARN-LEX-001",
                                   "R-LEX-0003",
                                   "lone carriage return is a non-portable newline",
                                   R_DIAGNOSTIC_WARNING,
                                   r_lex_span(source_id, offset, offset + 1U));
        }
    }
}

static bool
r_validate_source_encoding(RFrontendContext *context, RSource *source, RSourceId source_id) {
    size_t offset = 0U;
    while (offset < source->length) {
        uint32_t code_point;
        size_t width;
        if (!r_utf8_decode(source->bytes, source->length, offset, &code_point, &width)) {
            if (!r_add_diagnostic(context,
                                  "R-DIAG-LEX-001",
                                  "R-LEX-0001",
                                  "source is not well-formed UTF-8",
                                  R_DIAGNOSTIC_ERROR,
                                  r_lex_span(source_id, offset, offset + 1U))) {
                return false;
            }
            offset += 1U;
            continue;
        }
        if ((code_point == UINT32_C(0xFEFF)) && (offset != 0U) &&
            !r_add_diagnostic(context,
                              "R-DIAG-LEX-001",
                              "R-LEX-0002",
                              "U+FEFF is permitted only as the first code point",
                              R_DIAGNOSTIC_ERROR,
                              r_lex_span(source_id, offset, offset + width))) {
            return false;
        }
        offset += width;
    }
    return true;
}

/* Lexes the bytes of `source` from `offset` to its end, appending tokens; false only when the
   context reports a resource failure. */
static bool
r_lex_range(RFrontendContext *context, RSource *source, RSourceId source_id, size_t offset) {
    while (offset < source->length) {
        size_t start = offset;
        size_t end = offset + 1U;
        RTokenKind token_kind = R_TOKEN_ERROR;
        RAsciiLexeme lexeme;

        if (source->bytes[offset] == (uint8_t)'f' && offset + 1U < source->length &&
            source->bytes[offset + 1U] == (uint8_t)'"') {
            if (!r_scan_format_literal(context, source, source_id, offset, &end)) {
                return false;
            }
            offset = end;
            continue;
        }
        if (source->bytes[offset] == UINT8_C(0)) {
            (void)r_add_diagnostic(context,
                                   "R-DIAG-LEX-004",
                                   "R-LEX-0016",
                                   "NUL is not a source token",
                                   R_DIAGNOSTIC_ERROR,
                                   r_lex_span(source_id, offset, offset + 1U));
        } else {
            lexeme = r_lexer_scan_ascii(source->bytes + offset);
            switch (lexeme.kind) {
            case R_ASCII_LEXEME_WHITESPACE:
                end = offset + lexeme.length;
                token_kind = R_TOKEN_WHITESPACE;
                r_warn_lone_carriage_returns(context, source, source_id, offset, end);
                break;
            case R_ASCII_LEXEME_LINE_COMMENT:
                end = offset + lexeme.length;
                token_kind = R_TOKEN_LINE_COMMENT;
                break;
            case R_ASCII_LEXEME_BLOCK_COMMENT_START: {
                size_t current = offset + lexeme.length;
                bool closed = false;
                while ((current + 1U) < source->length) {
                    if ((source->bytes[current] == (uint8_t)'*') &&
                        (source->bytes[current + 1U] == (uint8_t)'/')) {
                        current += 2U;
                        closed = true;
                        break;
                    }
                    current += 1U;
                }
                end = closed ? current : source->length;
                token_kind = R_TOKEN_BLOCK_COMMENT;
                if (!closed) {
                    (void)r_add_diagnostic(context,
                                           "R-DIAG-LEX-002",
                                           "R-LEX-0004",
                                           "unterminated block comment",
                                           R_DIAGNOSTIC_ERROR,
                                           r_lex_span(source_id, offset, end));
                }
                break;
            }
            case R_ASCII_LEXEME_IDENTIFIER:
                if (!r_scan_identifier(context,
                                       source,
                                       source_id,
                                       offset,
                                       lexeme.length,
                                       false,
                                       &end,
                                       &token_kind)) {
                    return false;
                }
                break;
            case R_ASCII_LEXEME_NUMBER_START:
                if (!r_scan_number(context, source, source_id, offset, &end, &token_kind)) {
                    return false;
                }
                break;
            case R_ASCII_LEXEME_STRING_START:
                if (!r_scan_literal(
                        context, source, source_id, offset, (uint8_t)'\"', &end, &token_kind)) {
                    return false;
                }
                break;
            case R_ASCII_LEXEME_CHARACTER_START:
                if (!r_scan_literal(
                        context, source, source_id, offset, (uint8_t)'\'', &end, &token_kind)) {
                    return false;
                }
                break;
            case R_ASCII_LEXEME_PUNCTUATOR:
                end = offset + lexeme.length;
                token_kind = lexeme.token_kind;
                break;
            case R_ASCII_LEXEME_NON_ASCII:
                if (!r_scan_identifier(
                        context, source, source_id, offset, 0U, true, &end, &token_kind)) {
                    return false;
                }
                if ((token_kind == R_TOKEN_ERROR) && (end == offset + 1U)) {
                    uint32_t code_point;
                    size_t width;
                    if (r_utf8_decode(source->bytes, source->length, offset, &code_point, &width)) {
                        end = offset + width;
                    }
                }
                break;
            case R_ASCII_LEXEME_EOF:
            case R_ASCII_LEXEME_UNKNOWN:
            default:
                (void)r_add_diagnostic(context,
                                       "R-DIAG-LEX-004",
                                       "R-LEX-0016",
                                       "unknown or forbidden source character",
                                       R_DIAGNOSTIC_ERROR,
                                       r_lex_span(source_id, offset, offset + 1U));
                break;
            }
        }
        if (end <= start) {
            end = start + 1U;
        }
        if (!r_add_token(context, source, token_kind, (uint32_t)start, (uint32_t)end)) {
            return false;
        }
        offset = end;
    }
    return true;
}

/* M32-7: whether the starts of the tokens of a source never decrease. */
static bool r_tokens_ordered(const RSource *source) {
    for (size_t index = 1U; index < source->token_count; ++index) {
        if (source->tokens[index].span.start < source->tokens[index - 1U].span.start) {
            return false;
        }
    }
    return true;
}

RFrontendStatus r_frontend_lex(RFrontendContext *context, RSourceId source_id) {
    RSource *source;
    size_t offset = 0U;

    if (context == NULL) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    if (context->resource_status != R_FRONTEND_OK) {
        return context->resource_status;
    }
    source = r_get_source(context, source_id);
    if (source == NULL) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    if (source->lexed) {
        return source->has_lex_error ? R_FRONTEND_INVALID_SOURCE : R_FRONTEND_OK;
    }
    if (!r_validate_source_encoding(context, source, source_id)) {
        return context->resource_status;
    }

    if ((source->length >= 3U) && (source->bytes[0] == UINT8_C(0xEF)) &&
        (source->bytes[1] == UINT8_C(0xBB)) && (source->bytes[2] == UINT8_C(0xBF))) {
        if (!r_add_token(context, source, R_TOKEN_BOM, UINT32_C(0), UINT32_C(3))) {
            return context->resource_status;
        }
        offset = 3U;
    }

    if (!r_lex_range(context, source, source_id, offset)) {
        return context->resource_status;
    }
    if (!r_add_token(
            context, source, R_TOKEN_EOF, (uint32_t)source->length, (uint32_t)source->length) ||
        !r_validate_string_sequences(context, source, source_id)) {
        return context->resource_status;
    }
    source->lexed = true;
    source->tokens_ordered = r_tokens_ordered(source);
    if (context->resource_status != R_FRONTEND_OK) {
        return context->resource_status;
    }
    return source->has_lex_error ? R_FRONTEND_INVALID_SOURCE : R_FRONTEND_OK;
}

/* R-AGG-0012 (L27): appends generated declarations to the end of a lexed source and lexes them;
   the authored text keeps its tokens, and the generated text holds no string literal. */
bool r_lex_append(RFrontendContext *context, RSourceId source_id, const char *text, size_t length) {
    RSource *source = r_get_source(context, source_id);
    size_t old_length;

    if ((source == NULL) || !source->lexed || (text == NULL)) {
        if (context != NULL) {
            context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        }
        return false;
    }
    old_length = source->length;
    if (!r_source_append_bytes(context, source, (const uint8_t *)text, length)) {
        return false;
    }
    if ((source->token_count != 0U) &&
        (source->tokens[source->token_count - 1U].kind == R_TOKEN_EOF)) {
        source->token_count -= 1U;
    }
    if (!r_lex_range(context, source, source_id, old_length) ||
        !r_add_token(
            context, source, R_TOKEN_EOF, (uint32_t)source->length, (uint32_t)source->length)) {
        return false;
    }
    source->tokens_ordered = r_tokens_ordered(source);
    return true;
}

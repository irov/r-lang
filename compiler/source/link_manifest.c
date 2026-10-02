#include "link_manifest.h"

#include "frontend_internal.h"

#include <string.h>

#define R_LINK_MANIFEST_MAX_DEPTH 256U
#define R_LINK_MANIFEST_MAX_LOGICAL_NAME 255U

typedef struct RLinkJsonParser {
    const uint8_t *bytes;
    size_t length;
    size_t cursor;
    uint32_t depth;
    RLinkManifestVisitFn visitor;
    RLinkManifestSymbolVisitFn symbol_visitor;
    RLinkManifestDefinitionVisitFn definition_visitor;
    void *user_data;
    size_t entry_count;
    bool rejected;
    /* The bytes between the brackets of the links array, recorded while it is parsed. */
    size_t links_first;
    size_t links_last;
} RLinkJsonParser;

static void r_link_json_whitespace(RLinkJsonParser *parser) {
    while (parser->cursor < parser->length) {
        const uint8_t value = parser->bytes[parser->cursor];

        if ((value != UINT8_C(' ')) && (value != UINT8_C('\t')) && (value != UINT8_C('\r')) &&
            (value != UINT8_C('\n'))) {
            break;
        }
        parser->cursor += 1U;
    }
}

static bool r_link_json_byte(RLinkJsonParser *parser, uint8_t expected) {
    r_link_json_whitespace(parser);
    if ((parser->cursor >= parser->length) || (parser->bytes[parser->cursor] != expected)) {
        return false;
    }
    parser->cursor += 1U;
    return true;
}

static bool r_link_json_hex_digit(uint8_t value, uint32_t *digit) {
    if ((value >= UINT8_C('0')) && (value <= UINT8_C('9'))) {
        *digit = (uint32_t)(value - UINT8_C('0'));
        return true;
    }
    if ((value >= UINT8_C('a')) && (value <= UINT8_C('f'))) {
        *digit = (uint32_t)(value - UINT8_C('a')) + UINT32_C(10);
        return true;
    }
    if ((value >= UINT8_C('A')) && (value <= UINT8_C('F'))) {
        *digit = (uint32_t)(value - UINT8_C('A')) + UINT32_C(10);
        return true;
    }
    return false;
}

static bool r_link_json_hex_quad(RLinkJsonParser *parser, uint32_t *value) {
    uint32_t result = UINT32_C(0);
    uint32_t index;

    if ((parser->length - parser->cursor) < 4U) {
        return false;
    }
    for (index = UINT32_C(0); index < UINT32_C(4); ++index) {
        uint32_t digit;

        if (!r_link_json_hex_digit(parser->bytes[parser->cursor], &digit)) {
            return false;
        }
        result = (result << UINT32_C(4)) | digit;
        parser->cursor += 1U;
    }
    *value = result;
    return true;
}

static bool
r_link_json_append_utf8(uint8_t *output, size_t capacity, size_t *length, uint32_t value) {
    size_t required;

    if ((value >= UINT32_C(0xd800)) && (value <= UINT32_C(0xdfff))) {
        return false;
    }
    required = value <= UINT32_C(0x7f)       ? 1U
               : value <= UINT32_C(0x7ff)    ? 2U
               : value <= UINT32_C(0xffff)   ? 3U
               : value <= UINT32_C(0x10ffff) ? 4U
                                             : 0U;
    if ((required == 0U) || (*length > capacity) || (required > (capacity - *length))) {
        return false;
    }
    if (required == 1U) {
        output[*length] = (uint8_t)value;
    } else if (required == 2U) {
        output[*length] = (uint8_t)(UINT32_C(0xc0) | (value >> UINT32_C(6)));
        output[*length + 1U] = (uint8_t)(UINT32_C(0x80) | (value & UINT32_C(0x3f)));
    } else if (required == 3U) {
        output[*length] = (uint8_t)(UINT32_C(0xe0) | (value >> UINT32_C(12)));
        output[*length + 1U] =
            (uint8_t)(UINT32_C(0x80) | ((value >> UINT32_C(6)) & UINT32_C(0x3f)));
        output[*length + 2U] = (uint8_t)(UINT32_C(0x80) | (value & UINT32_C(0x3f)));
    } else {
        output[*length] = (uint8_t)(UINT32_C(0xf0) | (value >> UINT32_C(18)));
        output[*length + 1U] =
            (uint8_t)(UINT32_C(0x80) | ((value >> UINT32_C(12)) & UINT32_C(0x3f)));
        output[*length + 2U] =
            (uint8_t)(UINT32_C(0x80) | ((value >> UINT32_C(6)) & UINT32_C(0x3f)));
        output[*length + 3U] = (uint8_t)(UINT32_C(0x80) | (value & UINT32_C(0x3f)));
    }
    *length += required;
    return true;
}

static bool r_link_json_string(RLinkJsonParser *parser,
                               uint8_t *output,
                               size_t capacity,
                               size_t *output_length) {
    size_t length = 0U;

    if (!r_link_json_byte(parser, UINT8_C('"'))) {
        return false;
    }
    while (parser->cursor < parser->length) {
        uint8_t value = parser->bytes[parser->cursor];

        if (value >= UINT8_C(0x80)) {
            uint32_t code_point;
            size_t width;

            if (!r_utf8_decode(
                    parser->bytes, parser->length, parser->cursor, &code_point, &width) ||
                ((output != NULL) && ((length > capacity) || (width > (capacity - length))))) {
                return false;
            }
            (void)code_point;
            if (output != NULL) {
                (void)memcpy(output + length, parser->bytes + parser->cursor, width);
            }
            parser->cursor += width;
            length += width;
            continue;
        }
        parser->cursor += 1U;
        if (value == UINT8_C('"')) {
            if (output_length != NULL) {
                *output_length = length;
            }
            return true;
        }
        if (value < UINT8_C(0x20)) {
            return false;
        }
        if (value == UINT8_C('\\')) {
            uint32_t scalar;

            if (parser->cursor >= parser->length) {
                return false;
            }
            value = parser->bytes[parser->cursor];
            parser->cursor += 1U;
            if ((value == UINT8_C('"')) || (value == UINT8_C('\\')) || (value == UINT8_C('/'))) {
                scalar = value;
            } else if (value == UINT8_C('b')) {
                scalar = UINT32_C(0x08);
            } else if (value == UINT8_C('f')) {
                scalar = UINT32_C(0x0c);
            } else if (value == UINT8_C('n')) {
                scalar = UINT32_C(0x0a);
            } else if (value == UINT8_C('r')) {
                scalar = UINT32_C(0x0d);
            } else if (value == UINT8_C('t')) {
                scalar = UINT32_C(0x09);
            } else if (value == UINT8_C('u')) {
                if (!r_link_json_hex_quad(parser, &scalar)) {
                    return false;
                }
                if ((scalar >= UINT32_C(0xd800)) && (scalar <= UINT32_C(0xdbff))) {
                    uint32_t low;

                    if (((parser->length - parser->cursor) < 6U) ||
                        (parser->bytes[parser->cursor] != UINT8_C('\\')) ||
                        (parser->bytes[parser->cursor + 1U] != UINT8_C('u'))) {
                        return false;
                    }
                    parser->cursor += 2U;
                    if (!r_link_json_hex_quad(parser, &low) || (low < UINT32_C(0xdc00)) ||
                        (low > UINT32_C(0xdfff))) {
                        return false;
                    }
                    scalar = UINT32_C(0x10000) + ((scalar - UINT32_C(0xd800)) << UINT32_C(10)) +
                             (low - UINT32_C(0xdc00));
                }
            } else {
                return false;
            }
            if ((output != NULL) && !r_link_json_append_utf8(output, capacity, &length, scalar)) {
                return false;
            }
            if (output == NULL) {
                uint8_t ignored[4];
                size_t ignored_length = 0U;

                if (!r_link_json_append_utf8(ignored, sizeof(ignored), &ignored_length, scalar)) {
                    return false;
                }
                length += ignored_length;
            }
        } else {
            if ((output != NULL) && (length >= capacity)) {
                return false;
            }
            if (output != NULL) {
                output[length] = value;
            }
            length += 1U;
        }
    }
    return false;
}

static bool r_link_json_key(
    RLinkJsonParser *parser, uint8_t *output, size_t capacity, size_t *output_length, bool *fits) {
    const size_t start = parser->cursor;
    size_t end;
    size_t length;

    if (!r_link_json_string(parser, NULL, 0U, &length)) {
        return false;
    }
    end = parser->cursor;
    *output_length = length;
    *fits = length <= capacity;
    if (!*fits) {
        return true;
    }
    parser->cursor = start;
    if (!r_link_json_string(parser, output, capacity, &length) || (parser->cursor != end)) {
        return false;
    }
    *output_length = length;
    return true;
}

static bool r_link_json_skip_value(RLinkJsonParser *parser);

static bool r_link_json_skip_array(RLinkJsonParser *parser) {
    if ((parser->depth >= R_LINK_MANIFEST_MAX_DEPTH) || !r_link_json_byte(parser, UINT8_C('['))) {
        return false;
    }
    parser->depth += UINT32_C(1);
    r_link_json_whitespace(parser);
    if ((parser->cursor < parser->length) && (parser->bytes[parser->cursor] == UINT8_C(']'))) {
        parser->cursor += 1U;
        parser->depth -= UINT32_C(1);
        return true;
    }
    while (r_link_json_skip_value(parser)) {
        r_link_json_whitespace(parser);
        if ((parser->cursor < parser->length) && (parser->bytes[parser->cursor] == UINT8_C(']'))) {
            parser->cursor += 1U;
            parser->depth -= UINT32_C(1);
            return true;
        }
        if (!r_link_json_byte(parser, UINT8_C(','))) {
            return false;
        }
    }
    return false;
}

static bool r_link_json_skip_object(RLinkJsonParser *parser) {
    if ((parser->depth >= R_LINK_MANIFEST_MAX_DEPTH) || !r_link_json_byte(parser, UINT8_C('{'))) {
        return false;
    }
    parser->depth += UINT32_C(1);
    r_link_json_whitespace(parser);
    if ((parser->cursor < parser->length) && (parser->bytes[parser->cursor] == UINT8_C('}'))) {
        parser->cursor += 1U;
        parser->depth -= UINT32_C(1);
        return true;
    }
    while (r_link_json_string(parser, NULL, 0U, NULL)) {
        if (!r_link_json_byte(parser, UINT8_C(':')) || !r_link_json_skip_value(parser)) {
            return false;
        }
        r_link_json_whitespace(parser);
        if ((parser->cursor < parser->length) && (parser->bytes[parser->cursor] == UINT8_C('}'))) {
            parser->cursor += 1U;
            parser->depth -= UINT32_C(1);
            return true;
        }
        if (!r_link_json_byte(parser, UINT8_C(','))) {
            return false;
        }
    }
    return false;
}

static bool r_link_json_literal(RLinkJsonParser *parser, const char *literal) {
    const size_t length = strlen(literal);

    r_link_json_whitespace(parser);
    if ((length > (parser->length - parser->cursor)) ||
        (memcmp(parser->bytes + parser->cursor, literal, length) != 0)) {
        return false;
    }
    parser->cursor += length;
    return true;
}

static bool r_link_json_skip_number(RLinkJsonParser *parser) {
    const size_t start = parser->cursor;

    r_link_json_whitespace(parser);
    if ((parser->cursor < parser->length) && (parser->bytes[parser->cursor] == UINT8_C('-'))) {
        parser->cursor += 1U;
    }
    if ((parser->cursor < parser->length) && (parser->bytes[parser->cursor] == UINT8_C('0'))) {
        parser->cursor += 1U;
    } else {
        size_t digits = 0U;

        while ((parser->cursor < parser->length) &&
               (parser->bytes[parser->cursor] >= UINT8_C('0')) &&
               (parser->bytes[parser->cursor] <= UINT8_C('9'))) {
            parser->cursor += 1U;
            digits += 1U;
        }
        if (digits == 0U) {
            parser->cursor = start;
            return false;
        }
    }
    if ((parser->cursor < parser->length) && (parser->bytes[parser->cursor] == UINT8_C('.'))) {
        size_t digits = 0U;

        parser->cursor += 1U;
        while ((parser->cursor < parser->length) &&
               (parser->bytes[parser->cursor] >= UINT8_C('0')) &&
               (parser->bytes[parser->cursor] <= UINT8_C('9'))) {
            parser->cursor += 1U;
            digits += 1U;
        }
        if (digits == 0U) {
            return false;
        }
    }
    if ((parser->cursor < parser->length) && ((parser->bytes[parser->cursor] == UINT8_C('e')) ||
                                              (parser->bytes[parser->cursor] == UINT8_C('E')))) {
        size_t digits = 0U;

        parser->cursor += 1U;
        if ((parser->cursor < parser->length) &&
            ((parser->bytes[parser->cursor] == UINT8_C('+')) ||
             (parser->bytes[parser->cursor] == UINT8_C('-')))) {
            parser->cursor += 1U;
        }
        while ((parser->cursor < parser->length) &&
               (parser->bytes[parser->cursor] >= UINT8_C('0')) &&
               (parser->bytes[parser->cursor] <= UINT8_C('9'))) {
            parser->cursor += 1U;
            digits += 1U;
        }
        if (digits == 0U) {
            return false;
        }
    }
    return parser->cursor > start;
}

static bool r_link_json_skip_value(RLinkJsonParser *parser) {
    r_link_json_whitespace(parser);
    if (parser->cursor >= parser->length) {
        return false;
    }
    if (parser->bytes[parser->cursor] == UINT8_C('{')) {
        return r_link_json_skip_object(parser);
    }
    if (parser->bytes[parser->cursor] == UINT8_C('[')) {
        return r_link_json_skip_array(parser);
    }
    if (parser->bytes[parser->cursor] == UINT8_C('"')) {
        return r_link_json_string(parser, NULL, 0U, NULL);
    }
    if (r_link_json_literal(parser, "true") || r_link_json_literal(parser, "false") ||
        r_link_json_literal(parser, "null")) {
        return true;
    }
    return r_link_json_skip_number(parser);
}

static bool r_link_logical_name_is_valid(const uint8_t *bytes, size_t length) {
    size_t index;
    bool segment_start = true;

    if ((bytes == NULL) || (length == 0U) || (length > R_LINK_MANIFEST_MAX_LOGICAL_NAME)) {
        return false;
    }
    for (index = 0U; index < length; ++index) {
        const uint8_t value = bytes[index];

        if (segment_start) {
            if ((value < UINT8_C('a')) || (value > UINT8_C('z'))) {
                return false;
            }
            segment_start = false;
        } else if ((value == UINT8_C('.')) || (value == UINT8_C('_')) || (value == UINT8_C('-'))) {
            segment_start = true;
        } else if (!(((value >= UINT8_C('a')) && (value <= UINT8_C('z'))) ||
                     ((value >= UINT8_C('0')) && (value <= UINT8_C('9'))))) {
            return false;
        }
    }
    return !segment_start;
}

#define R_LINK_MANIFEST_MAX_C_IDENTIFIER 256U

static bool r_link_c_identifier_is_valid(const uint8_t *bytes, size_t length) {
    size_t index;

    if ((length == 0U) || (length > R_LINK_MANIFEST_MAX_C_IDENTIFIER)) {
        return false;
    }
    for (index = 0U; index < length; ++index) {
        const uint8_t value = bytes[index];
        const bool letter = ((value >= UINT8_C('a')) && (value <= UINT8_C('z'))) ||
                            ((value >= UINT8_C('A')) && (value <= UINT8_C('Z'))) ||
                            (value == UINT8_C('_'));
        const bool digit = (value >= UINT8_C('0')) && (value <= UINT8_C('9'));

        if (!letter && !((index != 0U) && digit)) {
            return false;
        }
    }
    return true;
}

static bool r_link_json_text_equal(const uint8_t *bytes, size_t length, const char *text) {
    return (length == strlen(text)) && (memcmp(bytes, text, length) == 0);
}

/* One object of an entry's symbols array (R-FFI-0031). */
static bool r_link_json_symbol_object(RLinkJsonParser *parser,
                                      const RLinkManifestEntryView *entry) {
    uint8_t identifier[R_LINK_MANIFEST_MAX_C_IDENTIFIER];
    size_t identifier_length = 0U;
    bool have_identifier = false;
    bool have_kind = false;
    bool have_binding = false;
    RLinkManifestSymbolView symbol;

    (void)memset(&symbol, 0, sizeof(symbol));
    if (!r_link_json_byte(parser, UINT8_C('{'))) {
        return false;
    }
    r_link_json_whitespace(parser);
    if ((parser->cursor < parser->length) && (parser->bytes[parser->cursor] == UINT8_C('}'))) {
        return false;
    }
    while (parser->cursor < parser->length) {
        uint8_t key[32];
        size_t key_length = 0U;
        bool key_fits;

        if (!r_link_json_key(parser, key, sizeof(key), &key_length, &key_fits) ||
            !r_link_json_byte(parser, UINT8_C(':'))) {
            return false;
        }
        if (key_fits && r_link_json_text_equal(key, key_length, "c_identifier")) {
            if (have_identifier ||
                !r_link_json_string(parser, identifier, sizeof(identifier), &identifier_length)) {
                return false;
            }
            have_identifier = true;
        } else if (key_fits && r_link_json_text_equal(key, key_length, "kind")) {
            uint8_t text[16];
            size_t text_length = 0U;

            if (have_kind || !r_link_json_string(parser, text, sizeof(text), &text_length)) {
                return false;
            }
            if (r_link_json_text_equal(text, text_length, "function")) {
                symbol.kind = R_LINK_SYMBOL_FUNCTION;
            } else if (r_link_json_text_equal(text, text_length, "data")) {
                symbol.kind = R_LINK_SYMBOL_DATA;
            } else if (r_link_json_text_equal(text, text_length, "tls")) {
                symbol.kind = R_LINK_SYMBOL_TLS;
            } else {
                return false;
            }
            have_kind = true;
        } else if (key_fits && r_link_json_text_equal(key, key_length, "binding")) {
            uint8_t text[16];
            size_t text_length = 0U;

            if (have_binding || !r_link_json_string(parser, text, sizeof(text), &text_length)) {
                return false;
            }
            if (r_link_json_text_equal(text, text_length, "strong")) {
                symbol.weak = false;
            } else if (r_link_json_text_equal(text, text_length, "weak")) {
                symbol.weak = true;
            } else {
                return false;
            }
            have_binding = true;
        } else if (!r_link_json_skip_value(parser)) {
            return false;
        }
        r_link_json_whitespace(parser);
        if ((parser->cursor < parser->length) && (parser->bytes[parser->cursor] == UINT8_C('}'))) {
            parser->cursor += 1U;
            if (!have_identifier || !r_link_c_identifier_is_valid(identifier, identifier_length)) {
                return false;
            }
            symbol.c_identifier = identifier;
            symbol.c_identifier_length = identifier_length;
            if ((parser->symbol_visitor != NULL) &&
                !parser->symbol_visitor(parser->user_data, entry, symbol)) {
                parser->rejected = true;
                return false;
            }
            return true;
        }
        if (!r_link_json_byte(parser, UINT8_C(','))) {
            return false;
        }
    }
    return false;
}

#define R_LINK_MANIFEST_MAX_DEFINITION 512U

/* NAME or NAME=VALUE: NAME is a C identifier, VALUE is non-empty printable ASCII. */
static bool r_link_definition_is_valid(const uint8_t *bytes, size_t length) {
    size_t index = 0U;

    if ((length == 0U) || (length > R_LINK_MANIFEST_MAX_DEFINITION)) {
        return false;
    }
    while ((index < length) && (bytes[index] != UINT8_C('='))) {
        index += 1U;
    }
    if (!r_link_c_identifier_is_valid(bytes, index)) {
        return false;
    }
    if (index == length) {
        return true;
    }
    if ((index + 1U) == length) {
        return false;
    }
    for (index += 1U; index < length; ++index) {
        if ((bytes[index] < UINT8_C(0x20)) || (bytes[index] > UINT8_C(0x7e))) {
            return false;
        }
    }
    return true;
}

static bool r_link_json_definition_array(RLinkJsonParser *parser,
                                         const RLinkManifestEntryView *entry) {
    bool more;

    if (!r_link_json_byte(parser, UINT8_C('['))) {
        return false;
    }
    r_link_json_whitespace(parser);
    more = !((parser->cursor < parser->length) && (parser->bytes[parser->cursor] == UINT8_C(']')));
    if (!more) {
        parser->cursor += 1U;
    }
    while (more) {
        uint8_t definition[R_LINK_MANIFEST_MAX_DEFINITION];
        size_t definition_length = 0U;

        if (!r_link_json_string(parser, definition, sizeof(definition), &definition_length) ||
            !r_link_definition_is_valid(definition, definition_length)) {
            return false;
        }
        if ((parser->definition_visitor != NULL) &&
            !parser->definition_visitor(parser->user_data, entry, definition, definition_length)) {
            parser->rejected = true;
            return false;
        }
        r_link_json_whitespace(parser);
        if ((parser->cursor < parser->length) && (parser->bytes[parser->cursor] == UINT8_C(']'))) {
            parser->cursor += 1U;
            more = false;
        } else if (!r_link_json_byte(parser, UINT8_C(','))) {
            return false;
        }
    }
    return true;
}

/* Second scan of one accepted entry object that delivers its symbols array. */
static bool r_link_json_entry_symbols(RLinkJsonParser *parser,
                                      const RLinkManifestEntryView *entry) {
    bool have_symbols = false;
    bool have_definitions = false;

    if (!r_link_json_byte(parser, UINT8_C('{'))) {
        return false;
    }
    while (parser->cursor < parser->length) {
        uint8_t key[32];
        size_t key_length = 0U;
        bool key_fits;

        if (!r_link_json_key(parser, key, sizeof(key), &key_length, &key_fits) ||
            !r_link_json_byte(parser, UINT8_C(':'))) {
            return false;
        }
        if (key_fits && r_link_json_text_equal(key, key_length, "symbols")) {
            bool more;

            if (have_symbols || !r_link_json_byte(parser, UINT8_C('['))) {
                return false;
            }
            have_symbols = true;
            r_link_json_whitespace(parser);
            more = !((parser->cursor < parser->length) &&
                     (parser->bytes[parser->cursor] == UINT8_C(']')));
            if (!more) {
                parser->cursor += 1U;
            }
            while (more) {
                if (!r_link_json_symbol_object(parser, entry)) {
                    return false;
                }
                r_link_json_whitespace(parser);
                if ((parser->cursor < parser->length) &&
                    (parser->bytes[parser->cursor] == UINT8_C(']'))) {
                    parser->cursor += 1U;
                    more = false;
                } else if (!r_link_json_byte(parser, UINT8_C(','))) {
                    return false;
                }
            }
        } else if (key_fits &&
                   r_link_json_text_equal(key, key_length, "feature_test_definitions")) {
            if (have_definitions || !r_link_json_definition_array(parser, entry)) {
                return false;
            }
            have_definitions = true;
        } else if (!r_link_json_skip_value(parser)) {
            return false;
        }
        r_link_json_whitespace(parser);
        if ((parser->cursor < parser->length) && (parser->bytes[parser->cursor] == UINT8_C('}'))) {
            parser->cursor += 1U;
            return true;
        }
        if (!r_link_json_byte(parser, UINT8_C(','))) {
            return false;
        }
    }
    return false;
}

static bool r_link_json_manifest_entry(RLinkJsonParser *parser) {
    uint8_t logical_name[R_LINK_MANIFEST_MAX_LOGICAL_NAME];
    size_t logical_name_length = 0U;
    size_t object_start;
    bool have_logical_name = false;
    bool have_available = false;
    bool have_kind = false;
    bool have_implicit = false;
    bool available = true;
    bool implicit_c_runtime = false;
    RLinkKind kind = R_LINK_KIND_UNSPECIFIED;
    RLinkManifestEntryView entry;

    r_link_json_whitespace(parser);
    object_start = parser->cursor;
    if (!r_link_json_byte(parser, UINT8_C('{'))) {
        return false;
    }
    r_link_json_whitespace(parser);
    if ((parser->cursor < parser->length) && (parser->bytes[parser->cursor] == UINT8_C('}'))) {
        return false;
    }
    while (parser->cursor < parser->length) {
        uint8_t key[32];
        size_t key_length = 0U;
        bool key_fits;

        if (!r_link_json_key(parser, key, sizeof(key), &key_length, &key_fits) ||
            !r_link_json_byte(parser, UINT8_C(':'))) {
            return false;
        }
        if (key_fits && r_link_json_text_equal(key, key_length, "logical_name")) {
            if (have_logical_name ||
                !r_link_json_string(
                    parser, logical_name, sizeof(logical_name), &logical_name_length)) {
                return false;
            }
            have_logical_name = true;
        } else if (key_fits && r_link_json_text_equal(key, key_length, "available")) {
            if (have_available) {
                return false;
            }
            if (r_link_json_literal(parser, "true")) {
                available = true;
            } else if (r_link_json_literal(parser, "false")) {
                available = false;
            } else {
                return false;
            }
            have_available = true;
        } else if (key_fits && r_link_json_text_equal(key, key_length, "kind")) {
            uint8_t text[16];
            size_t text_length = 0U;

            if (have_kind || !r_link_json_string(parser, text, sizeof(text), &text_length) ||
                !r_link_manifest_kind_from_text(text, text_length, &kind)) {
                return false;
            }
            have_kind = true;
        } else if (key_fits && r_link_json_text_equal(key, key_length, "implicit_c_runtime")) {
            if (have_implicit) {
                return false;
            }
            if (r_link_json_literal(parser, "true")) {
                implicit_c_runtime = true;
            } else if (r_link_json_literal(parser, "false")) {
                implicit_c_runtime = false;
            } else {
                return false;
            }
            have_implicit = true;
        } else if (!r_link_json_skip_value(parser)) {
            return false;
        }
        r_link_json_whitespace(parser);
        if ((parser->cursor < parser->length) && (parser->bytes[parser->cursor] == UINT8_C('}'))) {
            parser->cursor += 1U;
            if (!have_logical_name ||
                !r_link_logical_name_is_valid(logical_name, logical_name_length)) {
                return false;
            }
            entry.logical_name = logical_name;
            entry.logical_name_length = logical_name_length;
            entry.available = available;
            entry.kind = kind;
            entry.implicit_c_runtime = implicit_c_runtime;
            if ((parser->visitor != NULL) && !parser->visitor(parser->user_data, entry)) {
                parser->rejected = true;
                return false;
            }
            if ((parser->symbol_visitor != NULL) || (parser->definition_visitor != NULL)) {
                const size_t object_end = parser->cursor;
                const uint32_t depth = parser->depth;

                parser->cursor = object_start;
                if (!r_link_json_entry_symbols(parser, &entry)) {
                    return false;
                }
                parser->cursor = object_end;
                parser->depth = depth;
            }
            parser->entry_count += 1U;
            return true;
        }
        if (!r_link_json_byte(parser, UINT8_C(','))) {
            return false;
        }
    }
    return false;
}

static bool r_link_json_manifest_entries(RLinkJsonParser *parser) {
    if (!r_link_json_byte(parser, UINT8_C('['))) {
        return false;
    }
    r_link_json_whitespace(parser);
    if ((parser->cursor < parser->length) && (parser->bytes[parser->cursor] == UINT8_C(']'))) {
        parser->cursor += 1U;
        return true;
    }
    while (r_link_json_manifest_entry(parser)) {
        r_link_json_whitespace(parser);
        if ((parser->cursor < parser->length) && (parser->bytes[parser->cursor] == UINT8_C(']'))) {
            parser->cursor += 1U;
            return true;
        }
        if (!r_link_json_byte(parser, UINT8_C(','))) {
            return false;
        }
    }
    return false;
}

static bool r_link_json_manifest(RLinkJsonParser *parser) {
    bool have_links = false;

    if (!r_link_json_byte(parser, UINT8_C('{'))) {
        return false;
    }
    r_link_json_whitespace(parser);
    if ((parser->cursor < parser->length) && (parser->bytes[parser->cursor] == UINT8_C('}'))) {
        return false;
    }
    while (parser->cursor < parser->length) {
        uint8_t key[32];
        size_t key_length = 0U;
        bool key_fits;

        if (!r_link_json_key(parser, key, sizeof(key), &key_length, &key_fits) ||
            !r_link_json_byte(parser, UINT8_C(':'))) {
            return false;
        }
        if (key_fits && (key_length == strlen("links")) &&
            (memcmp(key, "links", key_length) == 0)) {
            r_link_json_whitespace(parser);
            parser->links_first = parser->cursor + 1U;
            if (have_links || !r_link_json_manifest_entries(parser)) {
                return false;
            }
            parser->links_last = parser->cursor - 1U;
            have_links = true;
        } else if (!r_link_json_skip_value(parser)) {
            return false;
        }
        r_link_json_whitespace(parser);
        if ((parser->cursor < parser->length) && (parser->bytes[parser->cursor] == UINT8_C('}'))) {
            parser->cursor += 1U;
            return have_links;
        }
        if (!r_link_json_byte(parser, UINT8_C(','))) {
            return false;
        }
    }
    return false;
}

RLinkManifestVisitStatus r_link_manifest_visit(const uint8_t *bytes,
                                               size_t length,
                                               RLinkManifestVisitFn visitor,
                                               void *user_data,
                                               size_t *entry_count) {
    return r_link_manifest_visit_with_symbols(bytes, length, visitor, NULL, user_data, entry_count);
}

RLinkManifestVisitStatus
r_link_manifest_visit_with_symbols(const uint8_t *bytes,
                                   size_t length,
                                   RLinkManifestVisitFn visitor,
                                   RLinkManifestSymbolVisitFn symbol_visitor,
                                   void *user_data,
                                   size_t *entry_count) {
    RLinkManifestVisitors visitors;

    (void)memset(&visitors, 0, sizeof(visitors));
    visitors.entry = visitor;
    visitors.symbol = symbol_visitor;
    return r_link_manifest_visit_full(bytes, length, &visitors, user_data, entry_count);
}

bool r_link_manifest_logical_name_is_valid(const uint8_t *bytes, size_t length) {
    return r_link_logical_name_is_valid(bytes, length);
}

bool r_link_manifest_kind_from_text(const uint8_t *bytes, size_t length, RLinkKind *kind) {
    static const struct {
        const char *name;
        RLinkKind value;
    } kinds[] = {
        {"static", R_LINK_KIND_STATIC},
        {"dynamic", R_LINK_KIND_DYNAMIC},
        {"framework", R_LINK_KIND_FRAMEWORK},
        {"system", R_LINK_KIND_SYSTEM},
    };
    size_t index;

    if ((bytes == NULL) || (kind == NULL)) {
        return false;
    }
    for (index = 0U; index < sizeof(kinds) / sizeof(kinds[0]); ++index) {
        if (r_link_json_text_equal(bytes, length, kinds[index].name)) {
            *kind = kinds[index].value;
            return true;
        }
    }
    return false;
}

const char *r_link_manifest_kind_name(RLinkKind kind) {
    switch (kind) {
    case R_LINK_KIND_STATIC:
        return "static";
    case R_LINK_KIND_DYNAMIC:
        return "dynamic";
    case R_LINK_KIND_FRAMEWORK:
        return "framework";
    case R_LINK_KIND_SYSTEM:
        return "system";
    case R_LINK_KIND_UNSPECIFIED:
    default:
        return "unspecified";
    }
}

RLinkManifestVisitStatus r_link_manifest_visit_full(const uint8_t *bytes,
                                                    size_t length,
                                                    const RLinkManifestVisitors *visitors,
                                                    void *user_data,
                                                    size_t *entry_count) {
    RLinkJsonParser parser;

    if (entry_count != NULL) {
        *entry_count = 0U;
    }
    if ((bytes == NULL) && (length == 0U)) {
        return R_LINK_MANIFEST_VISIT_OK;
    }
    if ((bytes == NULL) || (length == 0U)) {
        return R_LINK_MANIFEST_VISIT_INVALID;
    }
    (void)memset(&parser, 0, sizeof(parser));
    parser.bytes = bytes;
    parser.length = length;
    parser.visitor = visitors == NULL ? NULL : visitors->entry;
    parser.symbol_visitor = visitors == NULL ? NULL : visitors->symbol;
    parser.definition_visitor = visitors == NULL ? NULL : visitors->definition;
    parser.user_data = user_data;
    if (!r_link_json_manifest(&parser)) {
        return parser.rejected ? R_LINK_MANIFEST_VISIT_REJECTED : R_LINK_MANIFEST_VISIT_INVALID;
    }
    r_link_json_whitespace(&parser);
    if (parser.cursor != parser.length) {
        return R_LINK_MANIFEST_VISIT_INVALID;
    }
    if (entry_count != NULL) {
        *entry_count = parser.entry_count;
    }
    return R_LINK_MANIFEST_VISIT_OK;
}

bool r_link_manifest_links_span(const uint8_t *bytes, size_t length, size_t *first, size_t *last) {
    RLinkJsonParser parser;

    if ((bytes == NULL) || (length == 0U) || (first == NULL) || (last == NULL)) {
        return false;
    }
    (void)memset(&parser, 0, sizeof(parser));
    parser.bytes = bytes;
    parser.length = length;
    if (!r_link_json_manifest(&parser)) {
        return false;
    }
    r_link_json_whitespace(&parser);
    if ((parser.cursor != parser.length) || (parser.links_last < parser.links_first)) {
        return false;
    }
    parser.cursor = parser.links_first;
    r_link_json_whitespace(&parser);
    *first = parser.cursor;
    *last = parser.links_last;
    while ((*last > *first) && ((bytes[*last - 1U] == UINT8_C(' ')) ||
                                (bytes[*last - 1U] == UINT8_C('\n')) ||
                                (bytes[*last - 1U] == UINT8_C('\r')) ||
                                (bytes[*last - 1U] == UINT8_C('\t')))) {
        *last -= 1U;
    }
    return true;
}

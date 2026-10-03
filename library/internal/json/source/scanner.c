#include "r_library_json_internal.h"

#include <stdalign.h>
#include <string.h>

/* Grammar and lexical state are independent. Every consumed input byte is processed once;
 * only unfinished token bytes and the active objects' keys are retained.
 *
 * Token text borrows the fed input while a string, key or number lies inside one feed and has
 * no escape: the token then carries a view of those input bytes, valid until the next feed
 * (R-SLIB-JSON feed: a fragment is consumed only for the call). An escape, or a feed that ends
 * inside a token, copies the bytes seen so far into the scanner's own text buffer first, so an
 * unfinished token never refers to a previous fragment. */
typedef enum RJsonGrammar {
    R_JSON_OBJECT_FIRST,
    R_JSON_OBJECT_KEY,
    R_JSON_OBJECT_COLON,
    R_JSON_OBJECT_VALUE,
    R_JSON_OBJECT_AFTER,
    R_JSON_ARRAY_FIRST,
    R_JSON_ARRAY_VALUE,
    R_JSON_ARRAY_AFTER
} RJsonGrammar;
/* The keys of all open objects share one stack of entries and bytes; a frame owns the entries
 * from key_first on and truncates the stack when it closes. Duplicate detection scans an
 * object's entries linearly up to R_JSON_LINEAR_KEYS keys and uses the frame's open-addressing
 * buckets beyond. */
typedef struct RJsonFrame {
    RJsonGrammar grammar;
    size_t index;
    size_t key_first;
    size_t bytes_first;
    RRuntimeArray buckets;
} RJsonFrame;
typedef struct RJsonKeyEntry {
    size_t offset;
    size_t length;
    uint64_t hash;
} RJsonKeyEntry;
enum { R_JSON_LINEAR_KEYS = 16 };
typedef enum RJsonLex {
    R_JSON_LEX_IDLE,
    R_JSON_LEX_STRING,
    R_JSON_LEX_ESCAPE,
    R_JSON_LEX_UNICODE,
    R_JSON_LEX_LOW_SLASH,
    R_JSON_LEX_LOW_U,
    R_JSON_LEX_NUMBER,
    R_JSON_LEX_LITERAL
} RJsonLex;
struct RJsonScannerState {
    RRuntimeAllocator *allocator;
    RStdJsonOptions options;
    RRuntimeArray frames;
    RRuntimeArray text;
    RRuntimeArray key_bytes;
    RRuntimeArray key_entries;
    /* The byte being processed and, while view is set, the borrowed token bytes. */
    const uint8_t *input_at;
    const uint8_t *view_data;
    size_t view_length;
    /* An allocation failure of take_text, reported by the next feed. */
    RStdJsonResult take_failure;
    RJsonLex lex;
    RStdJsonTokenKind token_kind;
    size_t offset;
    size_t token_offset;
    size_t value_bytes;
    size_t stream_index;
    uint32_t scalar;
    uint32_t high_surrogate;
    uint32_t utf8_scalar;
    uint32_t utf8_min;
    unsigned utf8_remaining;
    unsigned digits;
    unsigned number_state;
    unsigned literal_index;
    const char *literal;
    bool view;
    bool active;
    bool ready;
    bool delivered;
    bool finished;
    bool poisoned;
    bool token_delivered;
    bool stream_open;
    bool stream_closed;
    bool stream_after;
    bool stream_comma;
};

static void r_json_frame_drop(void *value) {
    RJsonFrame *frame = value;
    r_runtime_array_destroy(&frame->buckets);
}
static RJsonFrame *r_json_top(struct RJsonScannerState *s) {
    if (s->frames.length == 0U)
        return NULL;
    return &((RJsonFrame *)s->frames.data)[s->frames.length - 1U];
}
static RJsonKeyEntry *r_json_entries(struct RJsonScannerState *s) {
    return s->key_entries.data;
}
static RStdJsonByteView r_json_entry_view(struct RJsonScannerState *s, size_t index) {
    const RJsonKeyEntry *entry = &r_json_entries(s)[index];
    return (RStdJsonByteView){(const uint8_t *)s->key_bytes.data + entry->offset, entry->length};
}
static bool r_json_space(uint8_t c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}
/* A string byte that stands for itself and needs no further state: printable ASCII other than
 * the quote and the backslash. */
static bool r_json_plain(uint8_t c) {
    return c >= 0x20U && c < 0x80U && c != '"' && c != '\\';
}
static RStdJsonByteView r_json_token_text(const struct RJsonScannerState *s) {
    if (s->view)
        return (RStdJsonByteView){s->view_data, s->view_length};
    return (RStdJsonByteView){s->text.data, s->text.length};
}
static RStdJsonResult r_json_append_text(struct RJsonScannerState *s,
                                         const uint8_t *bytes,
                                         size_t length) {
    RStdJsonResult result;
    if (length == 0U)
        return (RStdJsonResult){0};
    result = r_json_array_result(r_runtime_array_reserve(&s->text, length));
    if (result.status != R_STD_JSON_CALL_SUCCESS)
        return result;
    memcpy((uint8_t *)s->text.data + s->text.length, bytes, length);
    s->text.length += length;
    return result;
}
/* Ends the borrow of the current token: its bytes so far move into the owned text buffer. */
static RStdJsonResult r_json_own_text(struct RJsonScannerState *s) {
    if (!s->view)
        return (RStdJsonResult){0};
    s->view = false;
    return r_json_append_text(s, s->view_data, s->view_length);
}
static void r_json_begin_view(struct RJsonScannerState *s, const uint8_t *first) {
    s->view = true;
    s->view_data = first;
    s->view_length = 0U;
}
/* Adds one verbatim input byte (the one being processed) or, after an escape, a decoded byte. */
static RStdJsonResult r_json_byte(struct RJsonScannerState *s, uint8_t c) {
    if (s->view) {
        ++s->view_length;
        return (RStdJsonResult){0};
    }
    return r_json_array_result(r_runtime_array_push(&s->text, &c));
}
static uint64_t r_json_key_hash(RStdJsonByteView key) {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0U; i < key.length; ++i) {
        hash ^= key.data[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}
static bool r_json_entry_equal(struct RJsonScannerState *s,
                               size_t index,
                               uint64_t hash,
                               RStdJsonByteView key) {
    const RJsonKeyEntry *entry = &r_json_entries(s)[index];
    return entry->hash == hash && r_json_view_equal(r_json_entry_view(s, index), key);
}
/* Rebuilds the frame's buckets for count keys at a load of at most one half. */
static RStdJsonResult r_json_rebuild_buckets(struct RJsonScannerState *s, RJsonFrame *frame) {
    const size_t count = s->key_entries.length - frame->key_first;
    size_t capacity = frame->buckets.length == 0U ? 64U : frame->buckets.length;
    RRuntimeArray fresh;
    RStdJsonResult result;
    size_t *buckets;
    while (count + 1U > capacity / 2U) {
        if (capacity > SIZE_MAX / 2U)
            return r_json_allocation_result(R_RUNTIME_ALLOCATION_SIZE_OVERFLOW);
        capacity *= 2U;
    }
    if (capacity == frame->buckets.length)
        return (RStdJsonResult){0};
    result = r_json_array_result(r_runtime_array_with_capacity(
        &fresh, s->allocator, (RRuntimeTypeInfo){sizeof(size_t), alignof(size_t), NULL, NULL},
        capacity));
    if (result.status != R_STD_JSON_CALL_SUCCESS)
        return result;
    memset(fresh.data, 0, capacity * sizeof(size_t));
    fresh.length = capacity;
    buckets = fresh.data;
    for (size_t i = 0U; i < count; ++i) {
        size_t slot = (size_t)r_json_entries(s)[frame->key_first + i].hash & (capacity - 1U);
        while (buckets[slot] != 0U)
            slot = (slot + 1U) & (capacity - 1U);
        buckets[slot] = i + 1U;
    }
    r_runtime_array_destroy(&frame->buckets);
    frame->buckets = fresh;
    return result;
}
static RStdJsonResult r_json_key_add(struct RJsonScannerState *s) {
    RJsonFrame *frame = r_json_top(s);
    const RStdJsonByteView text = r_json_token_text(s);
    const uint64_t hash = r_json_key_hash(text);
    const size_t count = s->key_entries.length - frame->key_first;
    size_t slot = 0U;
    RJsonKeyEntry entry;
    RStdJsonResult result;
    if (count < R_JSON_LINEAR_KEYS) {
        for (size_t i = frame->key_first; i < s->key_entries.length; ++i)
            if (r_json_entry_equal(s, i, hash, text))
                return r_json_failure(R_STD_JSON_ERROR_DUPLICATE_KEY, s->token_offset);
    } else {
        size_t *buckets;
        result = r_json_rebuild_buckets(s, frame);
        if (result.status != R_STD_JSON_CALL_SUCCESS)
            return result;
        buckets = frame->buckets.data;
        slot = (size_t)hash & (frame->buckets.length - 1U);
        while (buckets[slot] != 0U) {
            if (r_json_entry_equal(s, frame->key_first + buckets[slot] - 1U, hash, text))
                return r_json_failure(R_STD_JSON_ERROR_DUPLICATE_KEY, s->token_offset);
            slot = (slot + 1U) & (frame->buckets.length - 1U);
        }
    }
    if (s->key_bytes.capacity - s->key_bytes.length < text.length) {
        result = r_json_array_result(
            r_runtime_array_reserve(&s->key_bytes, text.length < 256U ? 256U : text.length));
        if (result.status != R_STD_JSON_CALL_SUCCESS)
            return result;
    }
    if (s->key_entries.length == s->key_entries.capacity) {
        result = r_json_array_result(r_runtime_array_reserve(&s->key_entries, 32U));
        if (result.status != R_STD_JSON_CALL_SUCCESS)
            return result;
    }
    if (text.length != 0U)
        memcpy((uint8_t *)s->key_bytes.data + s->key_bytes.length, text.data, text.length);
    entry = (RJsonKeyEntry){s->key_bytes.length, text.length, hash};
    s->key_bytes.length += text.length;
    r_json_entries(s)[s->key_entries.length] = entry;
    ++s->key_entries.length;
    if (count >= R_JSON_LINEAR_KEYS)
        ((size_t *)frame->buckets.data)[slot] = count + 1U;
    return (RStdJsonResult){0};
}
static void r_json_pop_frame(struct RJsonScannerState *s) {
    RJsonFrame *frame = r_json_top(s);
    s->key_entries.length = frame->key_first;
    s->key_bytes.length = frame->bytes_first;
    r_json_frame_drop(frame);
    --s->frames.length;
}
static RRuntimeStringStatus r_json_pointer_key(RStdString *pointer, RStdJsonByteView key) {
    RRuntimeStringStatus status = r_runtime_string_append(pointer, (const uint8_t *)"/", 1U);
    size_t begin = 0U;
    for (size_t i = 0U; status == R_RUNTIME_STRING_OK && i < key.length; ++i) {
        if (key.data[i] != '~' && key.data[i] != '/')
            continue;
        status = r_runtime_string_append(pointer, key.data + begin, i - begin);
        if (status == R_RUNTIME_STRING_OK)
            status = r_runtime_string_append(
                pointer, (const uint8_t *)(key.data[i] == '~' ? "~0" : "~1"), 2U);
        begin = i + 1U;
    }
    if (status == R_RUNTIME_STRING_OK && begin < key.length)
        status = r_runtime_string_append(pointer, key.data + begin, key.length - begin);
    return status;
}
static RRuntimeStringStatus r_json_pointer_index(RStdString *pointer, size_t index) {
    uint8_t digits[3U * sizeof(size_t) + 1U];
    size_t at = sizeof(digits);
    do {
        digits[--at] = (uint8_t)('0' + index % 10U);
        index /= 10U;
    } while (index != 0U);
    return r_json_pointer_key(pointer, (RStdJsonByteView){digits + at, sizeof(digits) - at});
}
static RStdJsonResult r_json_scanner_error(struct RJsonScannerState *s, RStdJsonResult result) {
    RRuntimeStringStatus status;
    s->poisoned = true;
    if (result.status != R_STD_JSON_CALL_JSON_ERROR)
        return result;
    status = r_runtime_string_initialize(&result.error.pointer, s->allocator);
    if (s->options.mode == R_STD_JSON_MODE_ARRAY_ELEMENTS && s->stream_open && !s->stream_closed)
        status = r_json_pointer_index(&result.error.pointer, s->stream_index);
    for (size_t i = 0U; status == R_RUNTIME_STRING_OK && i < s->frames.length; ++i) {
        RJsonFrame *frame = &((RJsonFrame *)s->frames.data)[i];
        const size_t key_end = i + 1U < s->frames.length
                                   ? ((RJsonFrame *)s->frames.data)[i + 1U].key_first
                                   : s->key_entries.length;
        if (frame->grammar >= R_JSON_ARRAY_FIRST) {
            status = r_json_pointer_index(&result.error.pointer, frame->index);
        } else if (i + 1U == s->frames.length &&
                   result.error.code == R_STD_JSON_ERROR_DUPLICATE_KEY) {
            status = r_json_pointer_key(&result.error.pointer, r_json_token_text(s));
        } else if (key_end > frame->key_first && (frame->grammar == R_JSON_OBJECT_COLON ||
                                                  frame->grammar == R_JSON_OBJECT_VALUE)) {
            status = r_json_pointer_key(&result.error.pointer, r_json_entry_view(s, key_end - 1U));
        }
    }
    if (status != R_RUNTIME_STRING_OK) {
        r_json_error_destroy(&result.error);
        return r_json_string_result(status);
    }
    return result;
}
static void r_json_complete_value(struct RJsonScannerState *s) {
    RJsonFrame *frame = r_json_top(s);
    if (frame == NULL) {
        s->ready = true;
        s->active = false;
    } else if (frame->grammar == R_JSON_OBJECT_VALUE) {
        frame->grammar = R_JSON_OBJECT_AFTER;
    } else {
        frame->grammar = R_JSON_ARRAY_AFTER;
        ++frame->index;
    }
}
static RStdJsonResult r_json_finish_token(struct RJsonScannerState *s) {
    RStdJsonResult result = {0};
    if (s->token_kind == R_STD_JSON_TOKEN_KEY) {
        result = r_json_key_add(s);
        if (result.status != R_STD_JSON_CALL_SUCCESS)
            return result;
        r_json_top(s)->grammar = R_JSON_OBJECT_COLON;
    } else {
        r_json_complete_value(s);
    }
    s->lex = R_JSON_LEX_IDLE;
    s->token_delivered = true;
    return result;
}
static bool r_json_number_accept(unsigned state) {
    return state == 2U || state == 3U || state == 5U || state == 8U;
}
/* 0=start, 1=minus, 2=zero, 3=integer, 4=dot, 5=fraction,
 * 6=exponent, 7=exponent sign, 8=exponent digits. */
static unsigned r_json_number_step(unsigned state, uint8_t c) {
    if (state == 0U && c == '-')
        return 1U;
    if ((state == 0U || state == 1U) && c == '0')
        return 2U;
    if ((state == 0U || state == 1U) && c >= '1' && c <= '9')
        return 3U;
    if (state == 3U && c >= '0' && c <= '9')
        return 3U;
    if ((state == 2U || state == 3U) && c == '.')
        return 4U;
    if ((state == 4U || state == 5U) && c >= '0' && c <= '9')
        return 5U;
    if ((state == 2U || state == 3U || state == 5U) && (c == 'e' || c == 'E'))
        return 6U;
    if (state == 6U && (c == '+' || c == '-'))
        return 7U;
    if ((state == 6U || state == 7U || state == 8U) && c >= '0' && c <= '9')
        return 8U;
    return 9U;
}
bool r_json_number_valid(RStdJsonByteView source) {
    unsigned state = 0U;
    for (size_t i = 0U; i < source.length; ++i) {
        state = r_json_number_step(state, source.data[i]);
        if (state == 9U)
            return false;
    }
    return r_json_number_accept(state);
}
static int r_json_hex(uint8_t c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}
static RStdJsonResult r_json_emit_scalar(struct RJsonScannerState *s, uint32_t scalar) {
    uint8_t bytes[4];
    size_t length;
    if (scalar < 0x80U) {
        bytes[0] = (uint8_t)scalar;
        length = 1U;
    } else if (scalar < 0x800U) {
        bytes[0] = (uint8_t)(0xc0U | (scalar >> 6U));
        bytes[1] = (uint8_t)(0x80U | (scalar & 0x3fU));
        length = 2U;
    } else if (scalar < 0x10000U) {
        bytes[0] = (uint8_t)(0xe0U | (scalar >> 12U));
        bytes[1] = (uint8_t)(0x80U | ((scalar >> 6U) & 0x3fU));
        bytes[2] = (uint8_t)(0x80U | (scalar & 0x3fU));
        length = 3U;
    } else {
        bytes[0] = (uint8_t)(0xf0U | (scalar >> 18U));
        bytes[1] = (uint8_t)(0x80U | ((scalar >> 12U) & 0x3fU));
        bytes[2] = (uint8_t)(0x80U | ((scalar >> 6U) & 0x3fU));
        bytes[3] = (uint8_t)(0x80U | (scalar & 0x3fU));
        length = 4U;
    }
    return r_json_append_text(s, bytes, length);
}
static RStdJsonResult r_json_lex_byte(struct RJsonScannerState *s, uint8_t c) {
    if (s->lex == R_JSON_LEX_LITERAL) {
        if ((uint8_t)s->literal[s->literal_index] != c)
            return r_json_failure(R_STD_JSON_ERROR_SYNTAX, s->offset);
        ++s->literal_index;
        if (s->literal[s->literal_index] == '\0')
            return r_json_finish_token(s);
        return (RStdJsonResult){0};
    }
    if (s->lex == R_JSON_LEX_NUMBER) {
        s->number_state = r_json_number_step(s->number_state, c);
        return r_json_byte(s, c);
    }
    if (s->lex == R_JSON_LEX_LOW_SLASH) {
        if (c != '\\')
            return r_json_failure(R_STD_JSON_ERROR_SYNTAX, s->offset);
        s->lex = R_JSON_LEX_LOW_U;
    } else if (s->lex == R_JSON_LEX_LOW_U) {
        if (c != 'u')
            return r_json_failure(R_STD_JSON_ERROR_SYNTAX, s->offset);
        s->lex = R_JSON_LEX_UNICODE;
        s->scalar = 0U;
        s->digits = 0U;
    } else if (s->lex == R_JSON_LEX_UNICODE) {
        int digit = r_json_hex(c);
        if (digit < 0)
            return r_json_failure(R_STD_JSON_ERROR_SYNTAX, s->offset);
        s->scalar = (s->scalar << 4U) | (unsigned)digit;
        if (++s->digits == 4U) {
            uint32_t scalar = s->scalar;
            if (s->high_surrogate != 0U) {
                if (scalar < 0xdc00U || scalar > 0xdfffU)
                    return r_json_failure(R_STD_JSON_ERROR_SYNTAX, s->offset);
                scalar = 0x10000U + ((s->high_surrogate - 0xd800U) << 10U) + scalar - 0xdc00U;
                s->high_surrogate = 0U;
            } else if (scalar >= 0xd800U && scalar <= 0xdbffU) {
                s->high_surrogate = scalar;
                s->lex = R_JSON_LEX_LOW_SLASH;
                return (RStdJsonResult){0};
            } else if (scalar >= 0xdc00U && scalar <= 0xdfffU) {
                return r_json_failure(R_STD_JSON_ERROR_SYNTAX, s->offset);
            }
            s->lex = R_JSON_LEX_STRING;
            return r_json_emit_scalar(s, scalar);
        }
    } else if (s->lex == R_JSON_LEX_ESCAPE) {
        uint8_t decoded;
        s->lex = R_JSON_LEX_STRING;
        switch (c) {
        case '"':
        case '\\':
        case '/':
            decoded = c;
            break;
        case 'b':
            decoded = '\b';
            break;
        case 'f':
            decoded = '\f';
            break;
        case 'n':
            decoded = '\n';
            break;
        case 'r':
            decoded = '\r';
            break;
        case 't':
            decoded = '\t';
            break;
        case 'u':
            s->lex = R_JSON_LEX_UNICODE;
            s->scalar = 0U;
            s->digits = 0U;
            return (RStdJsonResult){0};
        default:
            return r_json_failure(R_STD_JSON_ERROR_SYNTAX, s->offset);
        }
        return r_json_append_text(s, &decoded, 1U);
    } else if (s->utf8_remaining != 0U) {
        if ((c & 0xc0U) != 0x80U)
            return r_json_failure(R_STD_JSON_ERROR_INVALID_UTF8, s->offset);
        s->utf8_scalar = (s->utf8_scalar << 6U) | (c & 0x3fU);
        if (--s->utf8_remaining == 0U &&
            (s->utf8_scalar < s->utf8_min || s->utf8_scalar > 0x10ffffU ||
             (s->utf8_scalar >= 0xd800U && s->utf8_scalar <= 0xdfffU)))
            return r_json_failure(R_STD_JSON_ERROR_INVALID_UTF8, s->offset);
        return r_json_byte(s, c);
    } else if (c == '"') {
        return r_json_finish_token(s);
    } else if (c == '\\') {
        /* Escapes decode into owned text; the borrowed prefix moves there first. */
        s->lex = R_JSON_LEX_ESCAPE;
        return r_json_own_text(s);
    } else if (c < 0x20U) {
        return r_json_failure(R_STD_JSON_ERROR_SYNTAX, s->offset);
    } else {
        if (c >= 0x80U) {
            if (c >= 0xc2U && c <= 0xdfU) {
                s->utf8_remaining = 1U;
                s->utf8_scalar = c & 0x1fU;
                s->utf8_min = 0x80U;
            } else if (c >= 0xe0U && c <= 0xefU) {
                s->utf8_remaining = 2U;
                s->utf8_scalar = c & 0x0fU;
                s->utf8_min = 0x800U;
            } else if (c >= 0xf0U && c <= 0xf4U) {
                s->utf8_remaining = 3U;
                s->utf8_scalar = c & 7U;
                s->utf8_min = 0x10000U;
            } else
                return r_json_failure(R_STD_JSON_ERROR_INVALID_UTF8, s->offset);
        }
        return r_json_byte(s, c);
    }
    return (RStdJsonResult){0};
}
static RStdJsonResult r_json_start_value(struct RJsonScannerState *s, uint8_t c) {
    s->token_offset = s->offset;
    if (!s->active) {
        s->active = true;
        s->value_bytes = 0U;
    }
    if (c == '{' || c == '[') {
        RJsonFrame frame = {0};
        RStdJsonResult result;
        if (s->frames.length >= s->options.max_depth)
            return r_json_failure(R_STD_JSON_ERROR_DEPTH_LIMIT, s->offset);
        frame.grammar = c == '{' ? R_JSON_OBJECT_FIRST : R_JSON_ARRAY_FIRST;
        frame.key_first = s->key_entries.length;
        frame.bytes_first = s->key_bytes.length;
        r_runtime_array_initialize(&frame.buckets,
                                   s->allocator,
                                   (RRuntimeTypeInfo){sizeof(size_t), alignof(size_t), NULL, NULL});
        if (s->frames.length == s->frames.capacity) {
            result = r_json_array_result(r_runtime_array_reserve(&s->frames, 8U));
            if (result.status != R_STD_JSON_CALL_SUCCESS)
                return result;
        }
        result = r_json_array_result(r_runtime_array_push(&s->frames, &frame));
        if (result.status != R_STD_JSON_CALL_SUCCESS)
            return result;
        s->token_kind = c == '{' ? R_STD_JSON_TOKEN_OBJECT_BEGIN : R_STD_JSON_TOKEN_ARRAY_BEGIN;
        s->token_delivered = true;
    } else if (c == '"') {
        s->token_kind = R_STD_JSON_TOKEN_STRING;
        s->lex = R_JSON_LEX_STRING;
        r_json_begin_view(s, s->input_at + 1U);
    } else if (c == '-' || (c >= '0' && c <= '9')) {
        s->token_kind = R_STD_JSON_TOKEN_NUMBER;
        s->lex = R_JSON_LEX_NUMBER;
        s->number_state = r_json_number_step(0U, c);
        r_json_begin_view(s, s->input_at);
        return r_json_byte(s, c);
    } else if (c == 't' || c == 'f' || c == 'n') {
        s->token_kind = c == 't' ? R_STD_JSON_TOKEN_TRUE
                                 : (c == 'f' ? R_STD_JSON_TOKEN_FALSE : R_STD_JSON_TOKEN_NULL);
        s->literal = c == 't' ? "true" : (c == 'f' ? "false" : "null");
        s->literal_index = 1U;
        s->lex = R_JSON_LEX_LITERAL;
    } else
        return r_json_failure(R_STD_JSON_ERROR_SYNTAX, s->offset);
    return (RStdJsonResult){0};
}
static RStdJsonResult r_json_grammar_byte(struct RJsonScannerState *s, uint8_t c) {
    RJsonFrame *frame = r_json_top(s);
    if (frame == NULL)
        return r_json_start_value(s, c);
    switch (frame->grammar) {
    case R_JSON_OBJECT_FIRST:
    case R_JSON_OBJECT_KEY:
        if (c == '"') {
            s->lex = R_JSON_LEX_STRING;
            s->token_kind = R_STD_JSON_TOKEN_KEY;
            s->token_offset = s->offset;
            r_json_begin_view(s, s->input_at + 1U);
            return (RStdJsonResult){0};
        }
        if (frame->grammar == R_JSON_OBJECT_FIRST && c == '}')
            break;
        return r_json_failure(R_STD_JSON_ERROR_SYNTAX, s->offset);
    case R_JSON_OBJECT_COLON:
        if (c != ':')
            return r_json_failure(R_STD_JSON_ERROR_SYNTAX, s->offset);
        frame->grammar = R_JSON_OBJECT_VALUE;
        return (RStdJsonResult){0};
    case R_JSON_OBJECT_VALUE:
    case R_JSON_ARRAY_VALUE:
        return r_json_start_value(s, c);
    case R_JSON_ARRAY_FIRST:
        if (c != ']')
            return r_json_start_value(s, c);
        break;
    case R_JSON_OBJECT_AFTER:
        if (c == ',') {
            frame->grammar = R_JSON_OBJECT_KEY;
            return (RStdJsonResult){0};
        }
        if (c != '}')
            return r_json_failure(R_STD_JSON_ERROR_SYNTAX, s->offset);
        break;
    case R_JSON_ARRAY_AFTER:
        if (c == ',') {
            frame->grammar = R_JSON_ARRAY_VALUE;
            return (RStdJsonResult){0};
        }
        if (c != ']')
            return r_json_failure(R_STD_JSON_ERROR_SYNTAX, s->offset);
        break;
    }
    r_json_pop_frame(s);
    s->token_kind = c == '}' ? R_STD_JSON_TOKEN_OBJECT_END : R_STD_JSON_TOKEN_ARRAY_END;
    s->token_offset = s->offset;
    s->token_delivered = true;
    r_json_complete_value(s);
    return (RStdJsonResult){0};
}
RStdJsonResult r_json_scanner_initialize(RStdJsonScanner *scanner,
                                         RRuntimeAllocator *allocator,
                                         RStdJsonOptions options) {
    void *storage = NULL;
    RStdJsonResult result;
    *scanner = (RStdJsonScanner){0};
    if (options.max_depth == 0U || options.max_value_bytes == 0U ||
        options.mode > R_STD_JSON_MODE_ARRAY_ELEMENTS)
        return r_json_failure(R_STD_JSON_ERROR_INVALID_STATE, 0U);
    result = r_json_allocation_result(r_runtime_allocator_allocate(
        allocator, sizeof(struct RJsonScannerState), alignof(struct RJsonScannerState), &storage));
    if (result.status != R_STD_JSON_CALL_SUCCESS)
        return result;
    scanner->state = storage;
    *scanner->state = (struct RJsonScannerState){0};
    scanner->state->allocator = allocator;
    scanner->state->options = options;
    r_runtime_array_initialize(
        &scanner->state->frames,
        allocator,
        (RRuntimeTypeInfo){sizeof(RJsonFrame), alignof(RJsonFrame), NULL, r_json_frame_drop});
    r_runtime_array_initialize(&scanner->state->text,
                               allocator,
                               (RRuntimeTypeInfo){sizeof(uint8_t), alignof(uint8_t), NULL, NULL});
    r_runtime_array_initialize(&scanner->state->key_bytes,
                               allocator,
                               (RRuntimeTypeInfo){sizeof(uint8_t), alignof(uint8_t), NULL, NULL});
    r_runtime_array_initialize(
        &scanner->state->key_entries,
        allocator,
        (RRuntimeTypeInfo){sizeof(RJsonKeyEntry), alignof(RJsonKeyEntry), NULL, NULL});
    return result;
}
RStdJsonFeedResult
r_json_scanner_feed(RStdJsonScanner *scanner, RStdJsonByteView input, bool final) {
    RStdJsonFeedResult result = {0};
    struct RJsonScannerState *s = scanner->state;
    result.state = R_STD_JSON_FEED_NEED_INPUT;
    if (s == NULL || s->poisoned || (input.length != 0U && input.data == NULL)) {
        result.outcome = r_json_failure(R_STD_JSON_ERROR_INVALID_STATE, s == NULL ? 0U : s->offset);
        return result;
    }
    if (s->take_failure.status != R_STD_JSON_CALL_SUCCESS) {
        result.outcome = s->take_failure;
        s->take_failure = (RStdJsonResult){0};
        s->poisoned = true;
        return result;
    }
    if (s->token_delivered) {
        s->text.length = 0U;
        s->view = false;
        s->token_delivered = false;
    }
    if (s->delivered && s->options.mode != R_STD_JSON_MODE_DOCUMENT) {
        s->ready = false;
        s->delivered = false;
        if (s->options.mode == R_STD_JSON_MODE_ARRAY_ELEMENTS) {
            s->stream_after = true;
            ++s->stream_index;
        }
    }
    for (;;) {
        uint8_t c;
        bool counted;
        if (s->ready && !s->delivered && s->options.mode != R_STD_JSON_MODE_DOCUMENT) {
            result.state = R_STD_JSON_FEED_VALUE_READY;
            s->delivered = true;
            return result;
        }
        if (result.consumed == input.length) {
            if (!final) {
                /* The fragment is consumed only for this call: an unfinished token owns its
                   bytes from here on. */
                result.outcome = r_json_own_text(s);
                if (result.outcome.status != R_STD_JSON_CALL_SUCCESS)
                    break;
                return result;
            }
            if (s->lex == R_JSON_LEX_NUMBER && r_json_number_accept(s->number_state)) {
                result.outcome = r_json_finish_token(s);
                break;
            }
            if (s->lex != R_JSON_LEX_IDLE || s->frames.length != 0U ||
                (s->options.mode == R_STD_JSON_MODE_DOCUMENT && !s->ready) ||
                (s->options.mode == R_STD_JSON_MODE_ARRAY_ELEMENTS && !s->stream_closed)) {
                result.outcome = r_json_failure(R_STD_JSON_ERROR_UNEXPECTED_EOF, s->offset);
                break;
            }
            if (s->ready && !s->delivered) {
                s->delivered = true;
                result.state = R_STD_JSON_FEED_VALUE_READY;
            } else {
                s->finished = true;
                result.state = R_STD_JSON_FEED_END;
            }
            return result;
        }
        c = input.data[result.consumed];
        if (s->lex == R_JSON_LEX_STRING && s->utf8_remaining == 0U && s->active &&
            r_json_plain(c)) {
            /* A run of plain string bytes in one pass. It stops before a byte that would exceed
               the value-size limit or the offset range, which the byte path then reports. */
            size_t limit = input.length - result.consumed;
            size_t run = 0U;
            if (limit > s->options.max_value_bytes - s->value_bytes)
                limit = s->options.max_value_bytes - s->value_bytes;
            if (limit > (s->offset < SIZE_MAX - 1U ? SIZE_MAX - 1U - s->offset : 0U))
                limit = s->offset < SIZE_MAX - 1U ? SIZE_MAX - 1U - s->offset : 0U;
            while (run < limit && r_json_plain(input.data[result.consumed + run]))
                ++run;
            if (run != 0U) {
                if (s->view)
                    s->view_length += run;
                else {
                    result.outcome = r_json_append_text(s, input.data + result.consumed, run);
                    if (result.outcome.status != R_STD_JSON_CALL_SUCCESS)
                        break;
                }
                s->value_bytes += run;
                s->offset += run;
                result.consumed += run;
                continue;
            }
        }
        s->input_at = input.data + result.consumed;
        if (s->lex == R_JSON_LEX_NUMBER && r_json_number_step(s->number_state, c) == 9U) {
            if (!r_json_number_accept(s->number_state) ||
                !(r_json_space(c) || c == ',' || c == ']' || c == '}')) {
                result.outcome = r_json_failure(R_STD_JSON_ERROR_SYNTAX, s->offset);
                break;
            }
            result.outcome = r_json_finish_token(s);
            break;
        }
        counted = s->active;
        if (s->lex == R_JSON_LEX_IDLE && r_json_space(c)) {
            /* Whitespace inside a value counts toward its size limit. */
        } else if (s->finished || (s->ready && s->options.mode == R_STD_JSON_MODE_DOCUMENT) ||
                   s->stream_closed) {
            result.outcome = r_json_failure(R_STD_JSON_ERROR_SYNTAX, s->offset);
            break;
        } else if (s->lex != R_JSON_LEX_IDLE) {
            result.outcome = r_json_lex_byte(s, c);
        } else if (s->options.mode == R_STD_JSON_MODE_ARRAY_ELEMENTS && s->frames.length == 0U &&
                   !s->active) {
            if (!s->stream_open) {
                if (c != '[') {
                    result.outcome = r_json_failure(R_STD_JSON_ERROR_TYPE, s->offset);
                    break;
                }
                s->stream_open = true;
            } else if (s->stream_after) {
                if (c == ',') {
                    s->stream_after = false;
                    s->stream_comma = true;
                } else if (c == ']')
                    s->stream_closed = true;
                else {
                    result.outcome = r_json_failure(R_STD_JSON_ERROR_SYNTAX, s->offset);
                    break;
                }
            } else if (c == ']' && !s->stream_comma) {
                s->stream_closed = true;
            } else {
                s->stream_comma = false;
                result.outcome = r_json_start_value(s, c);
                counted = true;
            }
        } else {
            result.outcome = r_json_grammar_byte(s, c);
            counted = counted || s->active || s->ready;
        }
        if (result.outcome.status != R_STD_JSON_CALL_SUCCESS)
            break;
        if (counted && ++s->value_bytes > s->options.max_value_bytes) {
            result.outcome = r_json_failure(R_STD_JSON_ERROR_SIZE_LIMIT, s->offset);
            break;
        }
        if (s->offset == SIZE_MAX) {
            result.outcome = r_json_failure(R_STD_JSON_ERROR_SIZE_LIMIT, s->offset);
            break;
        }
        ++result.consumed;
        ++s->offset;
        if (s->token_delivered)
            break;
    }
    if (result.outcome.status != R_STD_JSON_CALL_SUCCESS) {
        result.outcome = r_json_scanner_error(s, result.outcome);
        return result;
    }
    result.state = R_STD_JSON_FEED_TOKEN;
    result.token = (RStdJsonToken){s->token_kind, s->token_offset, r_json_token_text(s)};
    return result;
}
/* Owned text of the delivered key, string or number token. Accumulated text moves out; a
 * borrowed token is copied, and if that copy cannot be allocated the result is empty and the
 * next feed reports the allocation failure. */
RStdString r_json_scanner_take_text(RStdJsonScanner *scanner) {
    RStdString result = {0};
    struct RJsonScannerState *s = scanner->state;
    RRuntimeStringStatus status;
    if (s == NULL || !s->token_delivered ||
        (s->token_kind != R_STD_JSON_TOKEN_KEY && s->token_kind != R_STD_JSON_TOKEN_STRING &&
         s->token_kind != R_STD_JSON_TOKEN_NUMBER))
        return result;
    if (!s->view) {
        result.bytes = s->text;
        r_runtime_array_initialize(&s->text,
                                   s->allocator,
                                   (RRuntimeTypeInfo){sizeof(uint8_t), alignof(uint8_t), NULL,
                                                      NULL});
        return result;
    }
    status = r_runtime_string_from_valid_utf8(&result, s->allocator, s->view_data, s->view_length);
    if (status != R_RUNTIME_STRING_OK) {
        if (s->take_failure.status == R_STD_JSON_CALL_SUCCESS)
            s->take_failure = r_json_string_result(status);
        return (RStdString){0};
    }
    s->view_length = 0U;
    return result;
}
void r_json_scanner_destroy(RStdJsonScanner *scanner) {
    struct RJsonScannerState *s = scanner->state;
    if (s == NULL)
        return;
    r_runtime_array_destroy(&s->frames);
    r_runtime_array_destroy(&s->text);
    r_runtime_array_destroy(&s->key_bytes);
    r_runtime_array_destroy(&s->key_entries);
    r_runtime_allocator_deallocate(s, alignof(struct RJsonScannerState));
    scanner->state = NULL;
}

RStdJsonResult
r_json_scanner_failure(RStdJsonScanner *scanner, RStdJsonErrorCode code, size_t offset) {
    struct RJsonScannerState *s = scanner->state;
    RJsonFrame *frame;
    if (s == NULL)
        return r_json_failure(code, offset);
    frame = r_json_top(s);
    if (s->token_delivered && frame != NULL) {
        if (s->token_kind == R_STD_JSON_TOKEN_ARRAY_BEGIN ||
            s->token_kind == R_STD_JSON_TOKEN_OBJECT_BEGIN) {
            RStdJsonResult result;
            --s->frames.length;
            result = r_json_scanner_error(s, r_json_failure(code, offset));
            ++s->frames.length;
            return result;
        }
        if (frame->grammar == R_JSON_OBJECT_AFTER)
            frame->grammar = R_JSON_OBJECT_VALUE;
        else if (frame->grammar == R_JSON_ARRAY_AFTER && frame->index != 0U)
            --frame->index;
    }
    return r_json_scanner_error(s, r_json_failure(code, offset));
}

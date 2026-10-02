#include "frontend_internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/*
 * ABI records for complete @repr(C) aggregates declared inside extern "C" blocks (R-FFI-0017,
 * R-FFI-0040, R-FFI-0041, R-CMAP-0018). A record is an immutable JSON document produced by
 * tools/generate_c_abi_record.py from the block's verified headers with the selected C
 * compiler. The frontend loads it, proves every C-declared struct member inventory (name,
 * order, type spelling, offset, size, alignment) and every C-declared enumerator inventory
 * (name, value, compatible integer type) one-for-one, and emits the inventory request that
 * the generator consumes. Layout alone is never accepted: a member omitted by the R
 * declaration is a mismatch even when it hides in padding.
 */

#define R_ABI_JSON_MAX_DEPTH 64U
#define R_ABI_SPELL_CAPACITY 512U

typedef enum RAbiJsonKind {
    R_ABI_JSON_OBJECT = 0,
    R_ABI_JSON_ARRAY,
    R_ABI_JSON_STRING,
    R_ABI_JSON_INTEGER,
    R_ABI_JSON_TRUE,
    R_ABI_JSON_OTHER
} RAbiJsonKind;

/* Document tree: children hang off first_child/next_sibling; strings are decoded copies. */
typedef struct RAbiJsonNode {
    RAbiJsonKind kind;
    char *key;
    size_t key_length;
    char *text;
    size_t text_length;
    int64_t integer;
    size_t first_child;
    size_t next_sibling;
    size_t child_count;
} RAbiJsonNode;

typedef struct RAbiJsonDocument {
    RFrontendContext *context;
    const uint8_t *bytes;
    size_t length;
    size_t cursor;
    uint32_t depth;
    RAbiJsonNode *nodes;
    size_t node_count;
    size_t node_capacity;
} RAbiJsonDocument;

#define R_ABI_NODE_NONE SIZE_MAX

static void r_abi_json_whitespace(RAbiJsonDocument *document) {
    while (document->cursor < document->length) {
        const uint8_t value = document->bytes[document->cursor];

        if ((value != UINT8_C(' ')) && (value != UINT8_C('\t')) && (value != UINT8_C('\r')) &&
            (value != UINT8_C('\n'))) {
            break;
        }
        document->cursor += 1U;
    }
}

static bool r_abi_json_peek(RAbiJsonDocument *document, uint8_t expected) {
    r_abi_json_whitespace(document);
    return (document->cursor < document->length) && (document->bytes[document->cursor] == expected);
}

static bool r_abi_json_byte(RAbiJsonDocument *document, uint8_t expected) {
    if (!r_abi_json_peek(document, expected)) {
        return false;
    }
    document->cursor += 1U;
    return true;
}

static bool r_abi_json_hex(uint8_t value, uint32_t *digit) {
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

/* Decodes one JSON string into a fresh context allocation (UTF-8, BMP escapes only). */
static bool r_abi_json_string(RAbiJsonDocument *document, char **text, size_t *text_length) {
    const size_t start = document->cursor;
    size_t end;
    size_t length = 0U;
    size_t pass;
    char *output = NULL;

    if (!r_abi_json_byte(document, UINT8_C('"'))) {
        return false;
    }
    /* First pass measures, second pass writes; both walk the same escape grammar. */
    for (pass = 0U; pass < 2U; ++pass) {
        size_t written = 0U;

        document->cursor = start;
        (void)r_abi_json_byte(document, UINT8_C('"'));
        while (true) {
            uint8_t value;

            if (document->cursor >= document->length) {
                r_context_free(document->context, output);
                return false;
            }
            value = document->bytes[document->cursor];
            if (value == UINT8_C('"')) {
                document->cursor += 1U;
                break;
            }
            if (value < UINT8_C(0x20)) {
                r_context_free(document->context, output);
                return false;
            }
            if (value == UINT8_C('\\')) {
                uint8_t escape;
                char decoded[4];
                size_t decoded_length = 1U;

                if (document->cursor + 1U >= document->length) {
                    r_context_free(document->context, output);
                    return false;
                }
                escape = document->bytes[document->cursor + 1U];
                document->cursor += 2U;
                switch (escape) {
                case UINT8_C('"'):
                case UINT8_C('\\'):
                case UINT8_C('/'):
                    decoded[0] = (char)escape;
                    break;
                case UINT8_C('n'):
                    decoded[0] = '\n';
                    break;
                case UINT8_C('t'):
                    decoded[0] = '\t';
                    break;
                case UINT8_C('r'):
                    decoded[0] = '\r';
                    break;
                case UINT8_C('b'):
                    decoded[0] = '\b';
                    break;
                case UINT8_C('f'):
                    decoded[0] = '\f';
                    break;
                case UINT8_C('u'): {
                    uint32_t code = UINT32_C(0);
                    size_t digit_index;

                    if (document->cursor + 4U > document->length) {
                        r_context_free(document->context, output);
                        return false;
                    }
                    for (digit_index = 0U; digit_index < 4U; ++digit_index) {
                        uint32_t digit;

                        if (!r_abi_json_hex(document->bytes[document->cursor + digit_index],
                                            &digit)) {
                            r_context_free(document->context, output);
                            return false;
                        }
                        code = (code << UINT32_C(4)) | digit;
                    }
                    document->cursor += 4U;
                    if ((code >= UINT32_C(0xd800)) && (code <= UINT32_C(0xdfff))) {
                        r_context_free(document->context, output);
                        return false;
                    }
                    if (code <= UINT32_C(0x7f)) {
                        decoded[0] = (char)code;
                    } else if (code <= UINT32_C(0x7ff)) {
                        decoded[0] = (char)(UINT32_C(0xc0) | (code >> UINT32_C(6)));
                        decoded[1] = (char)(UINT32_C(0x80) | (code & UINT32_C(0x3f)));
                        decoded_length = 2U;
                    } else {
                        decoded[0] = (char)(UINT32_C(0xe0) | (code >> UINT32_C(12)));
                        decoded[1] =
                            (char)(UINT32_C(0x80) | ((code >> UINT32_C(6)) & UINT32_C(0x3f)));
                        decoded[2] = (char)(UINT32_C(0x80) | (code & UINT32_C(0x3f)));
                        decoded_length = 3U;
                    }
                    break;
                }
                default:
                    r_context_free(document->context, output);
                    return false;
                }
                if (output != NULL) {
                    (void)memcpy(output + written, decoded, decoded_length);
                }
                written += decoded_length;
                continue;
            }
            if (output != NULL) {
                output[written] = (char)value;
            }
            written += 1U;
            document->cursor += 1U;
        }
        if (pass == 0U) {
            length = written;
            output = r_context_allocate(document->context, length + 1U);
            if (output == NULL) {
                return false;
            }
        }
    }
    end = document->cursor;
    output[length] = '\0';
    *text = output;
    *text_length = length;
    document->cursor = end;
    return true;
}

static bool r_abi_json_append_node(RAbiJsonDocument *document, RAbiJsonNode node, size_t *index) {
    if (!r_grow_array(document->context,
                      (void **)&document->nodes,
                      &document->node_capacity,
                      sizeof(*document->nodes),
                      document->node_count + 1U)) {
        return false;
    }
    document->nodes[document->node_count] = node;
    *index = document->node_count;
    document->node_count += 1U;
    return true;
}

static bool r_abi_json_value(RAbiJsonDocument *document, size_t *index);

static bool
r_abi_json_link_child(RAbiJsonDocument *document, size_t parent, size_t *last_child, size_t child) {
    if (*last_child == R_ABI_NODE_NONE) {
        document->nodes[parent].first_child = child;
    } else {
        document->nodes[*last_child].next_sibling = child;
    }
    *last_child = child;
    document->nodes[parent].child_count += 1U;
    return true;
}

static bool r_abi_json_container(RAbiJsonDocument *document, bool object, size_t *index) {
    RAbiJsonNode node;
    size_t last_child = R_ABI_NODE_NONE;

    if (document->depth >= R_ABI_JSON_MAX_DEPTH) {
        return false;
    }
    (void)memset(&node, 0, sizeof(node));
    node.kind = object ? R_ABI_JSON_OBJECT : R_ABI_JSON_ARRAY;
    node.first_child = R_ABI_NODE_NONE;
    node.next_sibling = R_ABI_NODE_NONE;
    if (!r_abi_json_byte(document, object ? UINT8_C('{') : UINT8_C('[')) ||
        !r_abi_json_append_node(document, node, index)) {
        return false;
    }
    document->depth += UINT32_C(1);
    if (r_abi_json_byte(document, object ? UINT8_C('}') : UINT8_C(']'))) {
        document->depth -= UINT32_C(1);
        return true;
    }
    while (true) {
        size_t child;
        char *key = NULL;
        size_t key_length = 0U;

        if (object) {
            if (!r_abi_json_string(document, &key, &key_length) ||
                !r_abi_json_byte(document, UINT8_C(':'))) {
                r_context_free(document->context, key);
                return false;
            }
        }
        if (!r_abi_json_value(document, &child)) {
            r_context_free(document->context, key);
            return false;
        }
        document->nodes[child].key = key;
        document->nodes[child].key_length = key_length;
        (void)r_abi_json_link_child(document, *index, &last_child, child);
        if (r_abi_json_byte(document, UINT8_C(','))) {
            continue;
        }
        if (!r_abi_json_byte(document, object ? UINT8_C('}') : UINT8_C(']'))) {
            return false;
        }
        break;
    }
    document->depth -= UINT32_C(1);
    return true;
}

static bool r_abi_json_literal(RAbiJsonDocument *document, const char *literal) {
    const size_t length = strlen(literal);

    r_abi_json_whitespace(document);
    if ((length > (document->length - document->cursor)) ||
        (memcmp(document->bytes + document->cursor, literal, length) != 0)) {
        return false;
    }
    document->cursor += length;
    return true;
}

static bool r_abi_json_number(RAbiJsonDocument *document, RAbiJsonNode *node) {
    bool negative = false;
    uint64_t magnitude = UINT64_C(0);
    size_t digits = 0U;
    bool integral = true;

    r_abi_json_whitespace(document);
    if ((document->cursor < document->length) && (document->bytes[document->cursor] == '-')) {
        negative = true;
        document->cursor += 1U;
    }
    while ((document->cursor < document->length) &&
           (document->bytes[document->cursor] >= UINT8_C('0')) &&
           (document->bytes[document->cursor] <= UINT8_C('9'))) {
        const uint64_t digit = (uint64_t)(document->bytes[document->cursor] - UINT8_C('0'));

        if (magnitude > (UINT64_MAX - digit) / UINT64_C(10)) {
            integral = false;
        } else {
            magnitude = magnitude * UINT64_C(10) + digit;
        }
        digits += 1U;
        document->cursor += 1U;
    }
    if (digits == 0U) {
        return false;
    }
    /* Fractions and exponents are skipped: the record only carries integers we need. */
    while ((document->cursor < document->length) &&
           ((document->bytes[document->cursor] == UINT8_C('.')) ||
            (document->bytes[document->cursor] == UINT8_C('e')) ||
            (document->bytes[document->cursor] == UINT8_C('E')) ||
            (document->bytes[document->cursor] == UINT8_C('+')) ||
            (document->bytes[document->cursor] == UINT8_C('-')) ||
            ((document->bytes[document->cursor] >= UINT8_C('0')) &&
             (document->bytes[document->cursor] <= UINT8_C('9'))))) {
        integral = false;
        document->cursor += 1U;
    }
    if (integral && (magnitude <= (uint64_t)INT64_MAX)) {
        node->kind = R_ABI_JSON_INTEGER;
        node->integer = negative ? -(int64_t)magnitude : (int64_t)magnitude;
    } else if (integral && negative && (magnitude == (uint64_t)INT64_MAX + UINT64_C(1))) {
        node->kind = R_ABI_JSON_INTEGER;
        node->integer = INT64_MIN;
    } else {
        node->kind = R_ABI_JSON_OTHER;
    }
    return true;
}

static bool r_abi_json_value(RAbiJsonDocument *document, size_t *index) {
    RAbiJsonNode node;

    r_abi_json_whitespace(document);
    if (document->cursor >= document->length) {
        return false;
    }
    if (document->bytes[document->cursor] == UINT8_C('{')) {
        return r_abi_json_container(document, true, index);
    }
    if (document->bytes[document->cursor] == UINT8_C('[')) {
        return r_abi_json_container(document, false, index);
    }
    (void)memset(&node, 0, sizeof(node));
    node.first_child = R_ABI_NODE_NONE;
    node.next_sibling = R_ABI_NODE_NONE;
    if (document->bytes[document->cursor] == UINT8_C('"')) {
        node.kind = R_ABI_JSON_STRING;
        if (!r_abi_json_string(document, &node.text, &node.text_length)) {
            return false;
        }
        if (!r_abi_json_append_node(document, node, index)) {
            r_context_free(document->context, node.text);
            return false;
        }
        return true;
    }
    if (r_abi_json_literal(document, "true")) {
        node.kind = R_ABI_JSON_TRUE;
        return r_abi_json_append_node(document, node, index);
    }
    if (r_abi_json_literal(document, "false") || r_abi_json_literal(document, "null")) {
        node.kind = R_ABI_JSON_OTHER;
        return r_abi_json_append_node(document, node, index);
    }
    return r_abi_json_number(document, &node) && r_abi_json_append_node(document, node, index);
}

static void r_abi_json_dispose(RAbiJsonDocument *document) {
    size_t index;

    for (index = 0U; index < document->node_count; ++index) {
        r_context_free(document->context, document->nodes[index].key);
        r_context_free(document->context, document->nodes[index].text);
    }
    r_context_free(document->context, document->nodes);
    document->nodes = NULL;
    document->node_count = 0U;
    document->node_capacity = 0U;
}

static const RAbiJsonNode *
r_abi_json_member(const RAbiJsonDocument *document, size_t object, const char *key) {
    const size_t key_length = strlen(key);
    size_t child;

    if ((object == R_ABI_NODE_NONE) || (document->nodes[object].kind != R_ABI_JSON_OBJECT)) {
        return NULL;
    }
    for (child = document->nodes[object].first_child; child != R_ABI_NODE_NONE;
         child = document->nodes[child].next_sibling) {
        const RAbiJsonNode *node = &document->nodes[child];

        if ((node->key_length == key_length) && (memcmp(node->key, key, key_length) == 0)) {
            return node;
        }
    }
    return NULL;
}

/* Copies a decoded string node into a fresh allocation owned by the record tables. */
static bool
r_abi_copy_text(RFrontendContext *context, const RAbiJsonNode *node, char **text, size_t *length) {
    char *copy;

    *text = NULL;
    *length = 0U;
    if ((node == NULL) || (node->kind != R_ABI_JSON_STRING)) {
        return false;
    }
    copy = r_context_allocate(context, node->text_length + 1U);
    if (copy == NULL) {
        return false;
    }
    (void)memcpy(copy, node->text, node->text_length + 1U);
    *text = copy;
    *length = node->text_length;
    return true;
}

static bool r_abi_text_equal(const char *bytes, size_t length, const char *text) {
    const size_t text_length = strlen(text);

    return (length == text_length) && (memcmp(bytes, text, text_length) == 0);
}

static bool r_abi_kind_from_text(const RAbiJsonNode *node, RCTypeKind *kind) {
    if ((node == NULL) || (node->kind != R_ABI_JSON_STRING)) {
        return false;
    }
    if (r_abi_text_equal(node->text, node->text_length, "struct")) {
        *kind = R_C_TYPE_KIND_STRUCT;
    } else if (r_abi_text_equal(node->text, node->text_length, "union")) {
        *kind = R_C_TYPE_KIND_UNION;
    } else if (r_abi_text_equal(node->text, node->text_length, "enum")) {
        *kind = R_C_TYPE_KIND_ENUM;
    } else if (r_abi_text_equal(node->text, node->text_length, "typedef")) {
        *kind = R_C_TYPE_KIND_TYPEDEF;
    } else {
        return false;
    }
    return true;
}

static bool r_abi_integer_field(const RAbiJsonDocument *document,
                                size_t object,
                                const char *key,
                                int64_t *value,
                                bool *present) {
    const RAbiJsonNode *node = r_abi_json_member(document, object, key);

    *present = node != NULL;
    if (node == NULL) {
        return true;
    }
    if (node->kind != R_ABI_JSON_INTEGER) {
        return false;
    }
    *value = node->integer;
    return true;
}

static bool r_abi_bool_field(const RAbiJsonDocument *document, size_t object, const char *key) {
    const RAbiJsonNode *node = r_abi_json_member(document, object, key);

    return (node != NULL) && (node->kind == R_ABI_JSON_TRUE);
}

/* The document index of an object node, found by identity among the parsed nodes. */
static size_t r_abi_json_index(const RAbiJsonDocument *document, const RAbiJsonNode *node) {
    return (node == NULL) ? R_ABI_NODE_NONE : (size_t)(node - document->nodes);
}

static bool r_abi_optional_text(RFrontendContext *context,
                                const RAbiJsonDocument *document,
                                size_t object,
                                const char *key,
                                char **text,
                                size_t *length) {
    const RAbiJsonNode *node = r_abi_json_member(document, object, key);

    *text = NULL;
    *length = 0U;
    return (node == NULL) || r_abi_copy_text(context, node, text, length);
}

static bool r_abi_load_type_node(RFrontendContext *context,
                                 const RAbiJsonDocument *document,
                                 size_t object,
                                 size_t *result);

/* Loads the tree under key of object; children are loaded before their parent's entry. */
static bool r_abi_load_type_tree(RFrontendContext *context,
                                 const RAbiJsonDocument *document,
                                 size_t object,
                                 const char *key,
                                 size_t *result) {
    const RAbiJsonNode *node = r_abi_json_member(document, object, key);

    return (node != NULL) && (node->kind == R_ABI_JSON_OBJECT) &&
           r_abi_load_type_node(context, document, r_abi_json_index(document, node), result);
}

static bool r_abi_load_type_node(RFrontendContext *context,
                                 const RAbiJsonDocument *document,
                                 size_t object,
                                 size_t *result) {
    const RAbiJsonNode *kind = r_abi_json_member(document, object, "node");
    const RAbiJsonNode *parameters = r_abi_json_member(document, object, "parameters");
    RAbiTypeNode node;
    size_t *children = NULL;
    size_t child_count = 0U;
    size_t child_capacity = 0U;
    size_t child;
    int64_t length = 0;
    bool has_length;
    bool success = false;

    (void)memset(&node, 0, sizeof(node));
    if ((kind == NULL) || (kind->kind != R_ABI_JSON_STRING)) {
        return false;
    }
    if (r_abi_text_equal(kind->text, kind->text_length, "void")) {
        node.kind = R_ABI_TYPE_NODE_VOID;
    } else if (r_abi_text_equal(kind->text, kind->text_length, "scalar")) {
        node.kind = R_ABI_TYPE_NODE_SCALAR;
    } else if (r_abi_text_equal(kind->text, kind->text_length, "pointer")) {
        node.kind = R_ABI_TYPE_NODE_POINTER;
    } else if (r_abi_text_equal(kind->text, kind->text_length, "function")) {
        node.kind = R_ABI_TYPE_NODE_FUNCTION;
    } else if (r_abi_text_equal(kind->text, kind->text_length, "record")) {
        node.kind = R_ABI_TYPE_NODE_RECORD;
    } else if (r_abi_text_equal(kind->text, kind->text_length, "enum")) {
        node.kind = R_ABI_TYPE_NODE_ENUM;
    } else if (r_abi_text_equal(kind->text, kind->text_length, "array")) {
        node.kind = R_ABI_TYPE_NODE_ARRAY;
    } else {
        node.kind = R_ABI_TYPE_NODE_OTHER;
    }
    node.is_const = r_abi_bool_field(document, object, "const");
    node.is_variadic = r_abi_bool_field(document, object, "variadic");
    node.is_complete = r_abi_bool_field(document, object, "complete");
    {
        const RAbiJsonNode *tag = r_abi_json_member(document, object, "tag");

        node.is_union = (tag != NULL) && (tag->kind == R_ABI_JSON_STRING) &&
                        r_abi_text_equal(tag->text, tag->text_length, "union");
    }
    if (!r_abi_integer_field(document, object, "length", &length, &has_length) || (length < 0)) {
        return false;
    }
    node.length = (uint64_t)length;
    /* Children in tree order: pointee, element, or result followed by the parameters. */
    if (node.kind == R_ABI_TYPE_NODE_POINTER) {
        if (!r_grow_array(context, (void **)&children, &child_capacity, sizeof(*children), 1U) ||
            !r_abi_load_type_tree(context, document, object, "pointee", &children[0])) {
            goto cleanup;
        }
        child_count = 1U;
    } else if (node.kind == R_ABI_TYPE_NODE_ARRAY) {
        if (!has_length ||
            !r_grow_array(context, (void **)&children, &child_capacity, sizeof(*children), 1U) ||
            !r_abi_load_type_tree(context, document, object, "element", &children[0])) {
            goto cleanup;
        }
        child_count = 1U;
    } else if (node.kind == R_ABI_TYPE_NODE_FUNCTION) {
        if ((parameters == NULL) || (parameters->kind != R_ABI_JSON_ARRAY) ||
            !r_grow_array(context, (void **)&children, &child_capacity, sizeof(*children), 1U) ||
            !r_abi_load_type_tree(context, document, object, "result", &children[0])) {
            goto cleanup;
        }
        child_count = 1U;
        for (child = parameters->first_child; child != R_ABI_NODE_NONE;
             child = document->nodes[child].next_sibling) {
            size_t parameter;

            if ((document->nodes[child].kind != R_ABI_JSON_OBJECT) ||
                !r_abi_load_type_node(context, document, child, &parameter) ||
                !r_grow_array(context,
                              (void **)&children,
                              &child_capacity,
                              sizeof(*children),
                              child_count + 1U)) {
                goto cleanup;
            }
            children[child_count] = parameter;
            child_count += 1U;
        }
    }
    if (!r_abi_optional_text(
            context, document, object, "spelling", &node.spelling, &node.spelling_length) ||
        !r_abi_optional_text(
            context, document, object, "tag_name", &node.tag_name, &node.tag_name_length) ||
        !r_abi_optional_text(
            context, document, object, "typedef", &node.typedef_name, &node.typedef_name_length) ||
        !r_grow_array(context,
                      (void **)&context->abi_type_children,
                      &context->abi_type_child_capacity,
                      sizeof(*context->abi_type_children),
                      context->abi_type_child_count + child_count) ||
        !r_grow_array(context,
                      (void **)&context->abi_type_nodes,
                      &context->abi_type_node_capacity,
                      sizeof(*context->abi_type_nodes),
                      context->abi_type_node_count + 1U)) {
        r_context_free(context, node.spelling);
        r_context_free(context, node.tag_name);
        r_context_free(context, node.typedef_name);
        goto cleanup;
    }
    node.first_child = context->abi_type_child_count;
    node.child_count = (uint32_t)child_count;
    if (child_count != 0U) {
        (void)memcpy(context->abi_type_children + context->abi_type_child_count,
                     children,
                     child_count * sizeof(*children));
    }
    context->abi_type_child_count += child_count;
    context->abi_type_nodes[context->abi_type_node_count] = node;
    *result = context->abi_type_node_count;
    context->abi_type_node_count += 1U;
    success = true;

cleanup:
    r_context_free(context, children);
    return success;
}

static bool r_abi_load_members(RFrontendContext *context,
                               const RAbiJsonDocument *document,
                               const RAbiJsonNode *members,
                               RAbiRecordType *type) {
    size_t child;

    type->first_member = context->abi_member_count;
    if ((members == NULL) || (members->kind != R_ABI_JSON_ARRAY)) {
        return false;
    }
    for (child = members->first_child; child != R_ABI_NODE_NONE;
         child = document->nodes[child].next_sibling) {
        const RAbiJsonNode *desugared = r_abi_json_member(document, child, "desugared_type");
        RAbiRecordMember member;
        int64_t offset = 0;
        bool present;

        (void)memset(&member, 0, sizeof(member));
        member.type_node = SIZE_MAX;
        if (!r_abi_copy_text(context,
                             r_abi_json_member(document, child, "name"),
                             &member.name,
                             &member.name_length) ||
            !r_abi_copy_text(context,
                             r_abi_json_member(document, child, "type"),
                             &member.type,
                             &member.type_length) ||
            ((desugared != NULL) &&
             !r_abi_copy_text(
                 context, desugared, &member.desugared_type, &member.desugared_type_length)) ||
            !r_abi_integer_field(document, child, "offset", &offset, &present) || !present ||
            (offset < 0)) {
            r_context_free(context, member.name);
            r_context_free(context, member.type);
            r_context_free(context, member.desugared_type);
            return false;
        }
        member.offset = (uint64_t)offset;
        if ((r_abi_json_member(document, child, "type_tree") != NULL) &&
            !r_abi_load_type_tree(context, document, child, "type_tree", &member.type_node)) {
            r_context_free(context, member.name);
            r_context_free(context, member.type);
            r_context_free(context, member.desugared_type);
            return false;
        }
        if (!r_grow_array(context,
                          (void **)&context->abi_members,
                          &context->abi_member_capacity,
                          sizeof(*context->abi_members),
                          context->abi_member_count + 1U)) {
            r_context_free(context, member.name);
            r_context_free(context, member.type);
            r_context_free(context, member.desugared_type);
            return false;
        }
        context->abi_members[context->abi_member_count] = member;
        context->abi_member_count += 1U;
        type->member_count += 1U;
    }
    return true;
}

static bool r_abi_load_enumerators(RFrontendContext *context,
                                   const RAbiJsonDocument *document,
                                   const RAbiJsonNode *enumerators,
                                   RAbiRecordType *type) {
    size_t child;

    type->first_enumerator = context->abi_enumerator_count;
    if ((enumerators == NULL) || (enumerators->kind != R_ABI_JSON_ARRAY)) {
        return false;
    }
    for (child = enumerators->first_child; child != R_ABI_NODE_NONE;
         child = document->nodes[child].next_sibling) {
        RAbiRecordEnumerator enumerator;
        bool present;

        (void)memset(&enumerator, 0, sizeof(enumerator));
        if (!r_abi_copy_text(context,
                             r_abi_json_member(document, child, "name"),
                             &enumerator.name,
                             &enumerator.name_length) ||
            !r_abi_integer_field(document, child, "value", &enumerator.value, &present) ||
            !present) {
            r_context_free(context, enumerator.name);
            return false;
        }
        if (!r_grow_array(context,
                          (void **)&context->abi_enumerators,
                          &context->abi_enumerator_capacity,
                          sizeof(*context->abi_enumerators),
                          context->abi_enumerator_count + 1U)) {
            r_context_free(context, enumerator.name);
            return false;
        }
        context->abi_enumerators[context->abi_enumerator_count] = enumerator;
        context->abi_enumerator_count += 1U;
        type->enumerator_count += 1U;
    }
    return true;
}

static bool r_abi_load_type(RFrontendContext *context,
                            const RAbiJsonDocument *document,
                            size_t object,
                            RAbiRecordEntry *entry) {
    RAbiRecordType type;
    const RAbiJsonNode *tag = r_abi_json_member(document, object, "tag");
    const RAbiJsonNode *underlying = r_abi_json_member(document, object, "underlying");
    int64_t size = 0;
    int64_t alignment = 0;
    bool has_size;
    bool has_alignment;

    (void)memset(&type, 0, sizeof(type));
    if (!r_abi_copy_text(context,
                         r_abi_json_member(document, object, "c_name"),
                         &type.c_name,
                         &type.c_name_length) ||
        !r_abi_kind_from_text(r_abi_json_member(document, object, "kind"), &type.kind)) {
        r_context_free(context, type.c_name);
        return false;
    }
    type.tag_kind = type.kind;
    if ((type.kind == R_C_TYPE_KIND_TYPEDEF) && !r_abi_kind_from_text(tag, &type.tag_kind)) {
        r_context_free(context, type.c_name);
        return false;
    }
    if (!r_abi_integer_field(document, object, "size", &size, &has_size) ||
        !r_abi_integer_field(document, object, "alignment", &alignment, &has_alignment) ||
        (has_size != has_alignment) || (size < 0) || (alignment < 0)) {
        r_context_free(context, type.c_name);
        return false;
    }
    type.has_layout = has_size;
    type.size = (uint64_t)size;
    type.alignment = (uint64_t)alignment;
    if ((underlying != NULL) &&
        !r_abi_copy_text(context, underlying, &type.underlying, &type.underlying_length)) {
        r_context_free(context, type.c_name);
        return false;
    }
    type.inexpressible = r_abi_json_member(document, object, "inexpressible") != NULL;
    type.first_member = context->abi_member_count;
    type.first_enumerator = context->abi_enumerator_count;
    if (type.inexpressible) {
        /* Reached from a prototype only: no inventory to load. */
    } else if (type.tag_kind == R_C_TYPE_KIND_STRUCT) {
        if (!r_abi_load_members(
                context, document, r_abi_json_member(document, object, "members"), &type)) {
            r_context_free(context, type.c_name);
            r_context_free(context, type.underlying);
            return false;
        }
    } else if (type.tag_kind == R_C_TYPE_KIND_ENUM) {
        if (!r_abi_load_enumerators(
                context, document, r_abi_json_member(document, object, "enumerators"), &type)) {
            r_context_free(context, type.c_name);
            r_context_free(context, type.underlying);
            return false;
        }
    }
    if (!r_grow_array(context,
                      (void **)&context->abi_types,
                      &context->abi_type_capacity,
                      sizeof(*context->abi_types),
                      context->abi_type_count + 1U)) {
        r_context_free(context, type.c_name);
        r_context_free(context, type.underlying);
        return false;
    }
    context->abi_types[context->abi_type_count] = type;
    context->abi_type_count += 1U;
    entry->type_count += 1U;
    return true;
}

/* One {c_name, kind, type} symbol of a record: the C type of a requested import. */
static bool r_abi_load_symbol(RFrontendContext *context,
                              const RAbiJsonDocument *document,
                              size_t object,
                              RAbiRecordEntry *entry) {
    RAbiRecordSymbol symbol;
    const RAbiJsonNode *kind = r_abi_json_member(document, object, "kind");

    (void)memset(&symbol, 0, sizeof(symbol));
    if ((kind == NULL) || (kind->kind != R_ABI_JSON_STRING) ||
        !r_abi_copy_text(context,
                         r_abi_json_member(document, object, "c_name"),
                         &symbol.c_name,
                         &symbol.c_name_length)) {
        return false;
    }
    symbol.is_object = r_abi_text_equal(kind->text, kind->text_length, "object");
    if ((!symbol.is_object && !r_abi_text_equal(kind->text, kind->text_length, "function")) ||
        !r_abi_load_type_tree(context, document, object, "type", &symbol.type_node) ||
        !r_grow_array(context,
                      (void **)&context->abi_symbols,
                      &context->abi_symbol_capacity,
                      sizeof(*context->abi_symbols),
                      context->abi_symbol_count + 1U)) {
        r_context_free(context, symbol.c_name);
        return false;
    }
    context->abi_symbols[context->abi_symbol_count] = symbol;
    context->abi_symbol_count += 1U;
    entry->symbol_count += 1U;
    return true;
}

static void r_abi_dispose_entry(RFrontendContext *context, RAbiRecordEntry *entry) {
    r_context_free(context, entry->name);
    r_context_free(context, entry->provider);
    r_context_free(context, entry->target_triple);
    r_context_free(context, entry->compiler_identity);
}

/* Appends one {spelling, sha256} header object of a record (R-FFI-0044). */
static bool r_abi_load_header(RFrontendContext *context,
                              const RAbiJsonDocument *document,
                              size_t object,
                              RAbiRecordEntry *entry) {
    RAbiRecordHeader header;

    (void)memset(&header, 0, sizeof(header));
    if ((document->nodes[object].kind != R_ABI_JSON_OBJECT) ||
        !r_abi_copy_text(context,
                         r_abi_json_member(document, object, "spelling"),
                         &header.spelling,
                         &header.spelling_length) ||
        !r_abi_copy_text(context,
                         r_abi_json_member(document, object, "sha256"),
                         &header.sha256,
                         &header.sha256_length) ||
        !r_grow_array(context,
                      (void **)&context->abi_headers,
                      &context->abi_header_capacity,
                      sizeof(*context->abi_headers),
                      context->abi_header_count + 1U)) {
        r_context_free(context, header.spelling);
        r_context_free(context, header.sha256);
        return false;
    }
    context->abi_headers[context->abi_header_count] = header;
    context->abi_header_count += 1U;
    entry->header_count += 1U;
    return true;
}

static bool
r_abi_load_record(RFrontendContext *context, const RAbiJsonDocument *document, size_t object) {
    RAbiRecordEntry entry;
    const RAbiJsonNode *provider = r_abi_json_member(document, object, "provider");
    const RAbiJsonNode *target = r_abi_json_member(document, object, "target_triple");
    const RAbiJsonNode *identity = r_abi_json_member(document, object, "compiler_identity");
    const RAbiJsonNode *options = r_abi_json_member(document, object, "options");
    const RAbiJsonNode *headers = r_abi_json_member(document, object, "headers");
    const RAbiJsonNode *types = r_abi_json_member(document, object, "types");
    const RAbiJsonNode *symbols = r_abi_json_member(document, object, "symbols");
    size_t child;

    (void)memset(&entry, 0, sizeof(entry));
    entry.first_type = context->abi_type_count;
    entry.first_header = context->abi_header_count;
    entry.first_symbol = context->abi_symbol_count;
    if (!r_abi_copy_text(context,
                         r_abi_json_member(document, object, "name"),
                         &entry.name,
                         &entry.name_length) ||
        ((provider != NULL) &&
         !r_abi_copy_text(context, provider, &entry.provider, &entry.provider_length)) ||
        ((target != NULL) &&
         !r_abi_copy_text(context, target, &entry.target_triple, &entry.target_triple_length)) ||
        ((identity != NULL) &&
         !r_abi_copy_text(
             context, identity, &entry.compiler_identity, &entry.compiler_identity_length)) ||
        (types == NULL) || (types->kind != R_ABI_JSON_ARRAY) ||
        ((headers != NULL) && (headers->kind != R_ABI_JSON_ARRAY))) {
        r_abi_dispose_entry(context, &entry);
        return false;
    }
    if (headers != NULL) {
        for (child = headers->first_child; child != R_ABI_NODE_NONE;
             child = document->nodes[child].next_sibling) {
            if (!r_abi_load_header(context, document, child, &entry)) {
                r_abi_dispose_entry(context, &entry);
                return false;
            }
        }
    }
    if ((options != NULL) && (options->kind == R_ABI_JSON_ARRAY)) {
        for (child = options->first_child; child != R_ABI_NODE_NONE;
             child = document->nodes[child].next_sibling) {
            const RAbiJsonNode *option = &document->nodes[child];

            if ((option->kind == R_ABI_JSON_STRING) &&
                r_abi_text_equal(option->text, option->text_length, "-std=c17")) {
                entry.c17_options = true;
            }
        }
    }
    for (child = types->first_child; child != R_ABI_NODE_NONE;
         child = document->nodes[child].next_sibling) {
        if (!r_abi_load_type(context, document, child, &entry)) {
            r_abi_dispose_entry(context, &entry);
            return false;
        }
    }
    if ((symbols != NULL) && (symbols->kind == R_ABI_JSON_ARRAY)) {
        for (child = symbols->first_child; child != R_ABI_NODE_NONE;
             child = document->nodes[child].next_sibling) {
            if (!r_abi_load_symbol(context, document, child, &entry)) {
                r_abi_dispose_entry(context, &entry);
                return false;
            }
        }
    }
    if (!r_grow_array(context,
                      (void **)&context->abi_records,
                      &context->abi_record_capacity,
                      sizeof(*context->abi_records),
                      context->abi_record_count + 1U)) {
        r_abi_dispose_entry(context, &entry);
        return false;
    }
    context->abi_records[context->abi_record_count] = entry;
    context->abi_record_count += 1U;
    return true;
}

RFrontendStatus r_frontend_load_abi_records(RFrontendContext *context,
                                            const uint8_t *document_bytes,
                                            size_t document_length) {
    RAbiJsonDocument document;
    const RAbiJsonNode *schema;
    const RAbiJsonNode *records;
    size_t root = R_ABI_NODE_NONE;
    size_t child;
    RFrontendStatus status = R_FRONTEND_INVALID_ARGUMENT;

    if ((context == NULL) || (document_bytes == NULL)) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    (void)memset(&document, 0, sizeof(document));
    document.context = context;
    document.bytes = document_bytes;
    document.length = document_length;
    if (!r_abi_json_value(&document, &root)) {
        goto cleanup;
    }
    r_abi_json_whitespace(&document);
    if (document.cursor != document.length) {
        goto cleanup;
    }
    schema = r_abi_json_member(&document, root, "schema");
    records = r_abi_json_member(&document, root, "records");
    if ((schema == NULL) || (schema->kind != R_ABI_JSON_STRING) ||
        !r_abi_text_equal(schema->text, schema->text_length, "r-abi-record-0.1") ||
        (records == NULL) || (records->kind != R_ABI_JSON_ARRAY)) {
        goto cleanup;
    }
    for (child = records->first_child; child != R_ABI_NODE_NONE;
         child = document.nodes[child].next_sibling) {
        if (!r_abi_load_record(context, &document, child)) {
            goto cleanup;
        }
    }
    /* R-FFI-0044: the record document identity enters the interface fingerprint. */
    r_sha256_digest(document_bytes, document_length, context->abi_record_digest);
    context->abi_record_digest_present = true;
    context->abi_records_loaded = true;
    status = R_FRONTEND_OK;

cleanup:
    r_abi_json_dispose(&document);
    if ((status != R_FRONTEND_OK) && (context->resource_status != R_FRONTEND_OK)) {
        return context->resource_status;
    }
    return status;
}

/* ---- verification --------------------------------------------------------------------- */

static bool r_abi_diagnostic(RFrontendContext *context,
                             const char *rule_id,
                             const char *message,
                             RSourceSpan span) {
    return r_add_diagnostic_phase(context,
                                  R_DIAGNOSTIC_PHASE_SEMANTIC,
                                  "R-DIAG-FFI-004",
                                  rule_id,
                                  message,
                                  R_DIAGNOSTIC_ERROR,
                                  span);
}

static bool r_abi_intern_text(const RFrontendContext *context,
                              uint32_t intern_id,
                              const char **bytes,
                              size_t *length) {
    const RInternEntry *entry;

    if ((intern_id == UINT32_C(0)) || ((size_t)intern_id > context->intern_count)) {
        return false;
    }
    entry = &context->intern_entries[(size_t)intern_id - 1U];
    *bytes = entry->bytes;
    *length = entry->length;
    return true;
}

static const RAbiRecordEntry *
r_abi_find_record(const RFrontendContext *context, const char *name, size_t name_length) {
    size_t index;

    for (index = 0U; index < context->abi_record_count; ++index) {
        const RAbiRecordEntry *entry = &context->abi_records[index];

        if ((entry->name_length == name_length) && (memcmp(entry->name, name, name_length) == 0)) {
            return entry;
        }
    }
    return NULL;
}

static const RAbiRecordType *r_abi_find_type(const RFrontendContext *context,
                                             const RAbiRecordEntry *entry,
                                             const char *c_name,
                                             size_t c_name_length) {
    size_t index;

    for (index = 0U; index < entry->type_count; ++index) {
        const RAbiRecordType *type = &context->abi_types[entry->first_type + index];

        if ((type->c_name_length == c_name_length) &&
            (memcmp(type->c_name, c_name, c_name_length) == 0)) {
            return type;
        }
    }
    return NULL;
}

/* Collapses runs of whitespace so "const char*" and "const char *" compare equal. */
static bool
r_abi_spelling_equal(const char *left, size_t left_length, const char *right, size_t right_length) {
    size_t left_index = 0U;
    size_t right_index = 0U;

    while ((left_index < left_length) || (right_index < right_length)) {
        const bool left_space =
            (left_index < left_length) && ((left[left_index] == ' ') || (left[left_index] == '\t'));
        const bool right_space = (right_index < right_length) &&
                                 ((right[right_index] == ' ') || (right[right_index] == '\t'));

        if (left_space || right_space) {
            if (left_space != right_space) {
                return false;
            }
            while ((left_index < left_length) &&
                   ((left[left_index] == ' ') || (left[left_index] == '\t'))) {
                left_index += 1U;
            }
            while ((right_index < right_length) &&
                   ((right[right_index] == ' ') || (right[right_index] == '\t'))) {
                right_index += 1U;
            }
            continue;
        }
        if ((left_index >= left_length) || (right_index >= right_length) ||
            (left[left_index] != right[right_index])) {
            return false;
        }
        left_index += 1U;
        right_index += 1U;
    }
    return true;
}

static bool r_abi_verify_struct(RFrontendContext *context,
                                RSemanticAggregate *aggregate,
                                const RAbiRecordType *type,
                                bool *failed) {
    uint64_t offset = UINT64_C(0);
    uint64_t alignment = UINT64_C(1);
    uint32_t field_index;
    char spelling[R_ABI_SPELL_CAPACITY];

    if (type->tag_kind != R_C_TYPE_KIND_STRUCT) {
        *failed = true;
        aggregate->poisoned = true;
        return r_abi_diagnostic(context,
                                "R-FFI-0017",
                                "ABI record declares the C type with a different tag kind",
                                aggregate->name_span);
    }
    if (!type->has_layout || ((size_t)type->member_count != (size_t)aggregate->field_count)) {
        *failed = true;
        aggregate->poisoned = true;
        return r_abi_diagnostic(context,
                                "R-FFI-0017",
                                "ABI record member inventory count disagrees with the "
                                "@repr(C) struct",
                                aggregate->name_span);
    }
    for (field_index = UINT32_C(0); field_index < aggregate->field_count; ++field_index) {
        const size_t position = (size_t)aggregate->first_field + (size_t)field_index;
        const RSemanticField *field = &context->semantic_fields[position];
        const RAbiRecordMember *member = &context->abi_members[type->first_member + field_index];
        const char *name;
        size_t name_length;
        uint64_t field_size;
        uint64_t field_alignment;
        uint64_t aligned;

        if ((position >= context->semantic_field_count) ||
            !r_abi_intern_text(context, field->name_intern_id, &name, &name_length)) {
            context->resource_status = R_FRONTEND_INTERNAL_ERROR;
            return false;
        }
        if ((name_length != member->name_length) ||
            (memcmp(name, member->name, name_length) != 0)) {
            *failed = true;
            aggregate->poisoned = true;
            return r_abi_diagnostic(context,
                                    "R-FFI-0017",
                                    "ABI record member name or order disagrees with the "
                                    "@repr(C) struct field",
                                    field->name_span);
        }
        if (!r_c_abi_spell_header_type(context, field->type, spelling, sizeof(spelling))) {
            *failed = true;
            aggregate->poisoned = true;
            return r_abi_diagnostic(context,
                                    "R-FFI-0019",
                                    "@repr(C) struct field type has no C spelling in the "
                                    "extern block slice",
                                    field->name_span);
        }
        if (!r_abi_spelling_equal(spelling, strlen(spelling), member->type, member->type_length) &&
            ((member->desugared_type == NULL) ||
             !r_abi_spelling_equal(spelling,
                                   strlen(spelling),
                                   member->desugared_type,
                                   member->desugared_type_length))) {
            *failed = true;
            aggregate->poisoned = true;
            return r_abi_diagnostic(context,
                                    "R-FFI-0017",
                                    "ABI record member type disagrees with the @repr(C) "
                                    "struct field type",
                                    field->name_span);
        }
        if (!r_semantic_type_layout(context, field->type, &field_size, &field_alignment) ||
            (field_alignment == UINT64_C(0))) {
            context->resource_status = R_FRONTEND_INTERNAL_ERROR;
            return false;
        }
        aligned = offset + ((field_alignment - (offset % field_alignment)) % field_alignment);
        if (aligned != member->offset) {
            *failed = true;
            aggregate->poisoned = true;
            return r_abi_diagnostic(context,
                                    "R-FFI-0017",
                                    "ABI record member offset disagrees with the @repr(C) "
                                    "struct layout",
                                    field->name_span);
        }
        offset = aligned + field_size;
        if (field_alignment > alignment) {
            alignment = field_alignment;
        }
    }
    offset += (alignment - (offset % alignment)) % alignment;
    if (aggregate->field_count == UINT32_C(0)) {
        offset = UINT64_C(0);
    }
    if ((offset != type->size) || (alignment != type->alignment)) {
        *failed = true;
        aggregate->poisoned = true;
        return r_abi_diagnostic(context,
                                "R-FFI-0017",
                                "ABI record size or alignment disagrees with the @repr(C) "
                                "struct layout",
                                aggregate->name_span);
    }
    return true;
}

static bool r_abi_verify_enum(RFrontendContext *context,
                              RSemanticAggregate *aggregate,
                              const RAbiRecordType *type,
                              bool *failed) {
    uint32_t variant_index;
    size_t enumerator_index;
    bool underlying_signed;
    uint32_t underlying_width;
    uint64_t mask;
    char spelling[R_ABI_SPELL_CAPACITY];

    if (type->tag_kind != R_C_TYPE_KIND_ENUM) {
        *failed = true;
        aggregate->poisoned = true;
        return r_abi_diagnostic(context,
                                "R-FFI-0017",
                                "ABI record declares the C type with a different tag kind",
                                aggregate->name_span);
    }
    if (!r_c_abi_spell_header_type(
            context, aggregate->enum_underlying_type, spelling, sizeof(spelling)) ||
        (type->underlying == NULL) ||
        !r_abi_spelling_equal(
            spelling, strlen(spelling), type->underlying, type->underlying_length)) {
        *failed = true;
        aggregate->poisoned = true;
        return r_abi_diagnostic(context,
                                "R-FFI-0017",
                                "ABI record enumeration representation disagrees with the "
                                "@repr(C) enum underlying type",
                                aggregate->name_span);
    }
    if (!r_semantic_integer_properties_public(
            r_semantic_integer_representation(context, aggregate->enum_underlying_type),
            &underlying_signed,
            &underlying_width)) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    mask = underlying_width == UINT32_C(64) ? UINT64_MAX
                                            : (UINT64_C(1) << underlying_width) - UINT64_C(1);
    for (enumerator_index = 0U; enumerator_index < type->enumerator_count; ++enumerator_index) {
        const RAbiRecordEnumerator *enumerator =
            &context->abi_enumerators[type->first_enumerator + enumerator_index];
        size_t other;

        for (other = enumerator_index + 1U; other < type->enumerator_count; ++other) {
            if (context->abi_enumerators[type->first_enumerator + other].value ==
                enumerator->value) {
                *failed = true;
                aggregate->poisoned = true;
                return r_abi_diagnostic(context,
                                        "R-FFI-0017",
                                        "C enumeration has aliased enumerators; mediate it "
                                        "through its integer type and verified constants",
                                        aggregate->name_span);
            }
        }
    }
    if ((size_t)aggregate->variant_count != type->enumerator_count) {
        *failed = true;
        aggregate->poisoned = true;
        return r_abi_diagnostic(context,
                                "R-FFI-0017",
                                "ABI record enumerator inventory count disagrees with the "
                                "@repr(C) enum",
                                aggregate->name_span);
    }
    for (variant_index = UINT32_C(0); variant_index < aggregate->variant_count; ++variant_index) {
        const size_t position = (size_t)aggregate->first_variant + (size_t)variant_index;
        const RSemanticVariant *variant = &context->semantic_variants[position];
        const char *name;
        size_t name_length;
        const RAbiRecordEnumerator *match = NULL;
        int64_t declared;

        if ((position >= context->semantic_variant_count) ||
            !r_abi_intern_text(context, variant->name_intern_id, &name, &name_length)) {
            context->resource_status = R_FRONTEND_INTERNAL_ERROR;
            return false;
        }
        for (enumerator_index = 0U; enumerator_index < type->enumerator_count; ++enumerator_index) {
            const RAbiRecordEnumerator *enumerator =
                &context->abi_enumerators[type->first_enumerator + enumerator_index];

            if ((enumerator->name_length == name_length) &&
                (memcmp(enumerator->name, name, name_length) == 0)) {
                match = enumerator;
            }
        }
        if (underlying_signed &&
            ((variant->value & (UINT64_C(1) << (underlying_width - UINT32_C(1)))) != UINT64_C(0))) {
            declared = -(int64_t)((UINT64_C(0) - variant->value) & mask);
        } else {
            declared = (int64_t)(variant->value & mask);
        }
        if ((match == NULL) || (match->value != declared)) {
            *failed = true;
            aggregate->poisoned = true;
            return r_abi_diagnostic(context,
                                    "R-FFI-0017",
                                    "ABI record enumerator name or value disagrees with the "
                                    "@repr(C) enum variant",
                                    variant->name_span);
        }
    }
    return true;
}

/* The top-level "target_triple" string of a target manifest, found without a full parse. */
/* Finds the first string member named key in the manifest text without parsing it as a host path.
 */
static bool r_abi_manifest_string(const uint8_t *manifest,
                                  size_t length,
                                  const char *key,
                                  const uint8_t **triple,
                                  size_t *triple_length) {
    const size_t key_length = strlen(key);
    size_t index;

    *triple = NULL;
    *triple_length = 0U;
    if ((manifest == NULL) || (length < key_length)) {
        return false;
    }
    for (index = 0U; index + key_length <= length; ++index) {
        size_t cursor;
        size_t start;

        if (memcmp(manifest + index, key, key_length) != 0) {
            continue;
        }
        cursor = index + key_length;
        while ((cursor < length) &&
               ((manifest[cursor] == UINT8_C(' ')) || (manifest[cursor] == UINT8_C(':')) ||
                (manifest[cursor] == UINT8_C('\t')) || (manifest[cursor] == UINT8_C('\n')) ||
                (manifest[cursor] == UINT8_C('\r')))) {
            cursor += 1U;
        }
        if ((cursor >= length) || (manifest[cursor] != UINT8_C('"'))) {
            return false;
        }
        start = cursor + 1U;
        cursor = start;
        while ((cursor < length) && (manifest[cursor] != UINT8_C('"')) &&
               (manifest[cursor] != UINT8_C('\\'))) {
            cursor += 1U;
        }
        if ((cursor >= length) || (manifest[cursor] != UINT8_C('"'))) {
            return false;
        }
        *triple = manifest + start;
        *triple_length = cursor - start;
        return true;
    }
    return false;
}

/* R-FFI-0040: a used record shall belong to the block's provider, target and C17 options. */
static bool r_abi_manifest_target_triple(const uint8_t *manifest,
                                         size_t length,
                                         const uint8_t **triple,
                                         size_t *triple_length) {
    return r_abi_manifest_string(manifest, length, "\"target_triple\"", triple, triple_length);
}

/* The toolchain's C compiler build identity, which the record's compiler identity shall name. */
static bool r_abi_manifest_compiler_build(const uint8_t *manifest,
                                          size_t length,
                                          const uint8_t **build,
                                          size_t *build_length) {
    return r_abi_manifest_string(manifest, length, "\"c_compiler_build\"", build, build_length);
}

static bool r_abi_text_contains(const char *text,
                                size_t text_length,
                                const uint8_t *needle,
                                size_t needle_length) {
    size_t index;

    if (needle_length == 0U) {
        return true;
    }
    for (index = 0U; index + needle_length <= text_length; ++index) {
        if (memcmp(text + index, needle, needle_length) == 0) {
            return true;
        }
    }
    return false;
}

static bool r_abi_verify_record_identity(RFrontendContext *context,
                                         bool *poisoned,
                                         RSourceSpan span,
                                         const RExternBlockRecord *block,
                                         const RAbiRecordEntry *record,
                                         const uint8_t *target_triple,
                                         size_t target_triple_length,
                                         const uint8_t *compiler_build,
                                         size_t compiler_build_length,
                                         bool *failed) {
    const char *provider = "";
    size_t provider_length = 0U;

    if ((block->link_name_intern_id != UINT32_C(0)) &&
        !r_abi_intern_text(context, block->link_name_intern_id, &provider, &provider_length)) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    if ((record->provider_length != provider_length) ||
        ((provider_length != 0U) && (memcmp(record->provider, provider, provider_length) != 0))) {
        *failed = true;
        *poisoned = true;
        return r_abi_diagnostic(context,
                                "R-FFI-0040",
                                "ABI record provider disagrees with the block's link provider",
                                span);
    }
    if ((target_triple != NULL) && (record->target_triple != NULL) &&
        ((record->target_triple_length != target_triple_length) ||
         (memcmp(record->target_triple, target_triple, target_triple_length) != 0))) {
        *failed = true;
        *poisoned = true;
        return r_abi_diagnostic(
            context, "R-FFI-0040", "ABI record target disagrees with the target manifest", span);
    }
    if (!record->c17_options) {
        *failed = true;
        *poisoned = true;
        return r_abi_diagnostic(
            context, "R-FFI-0040", "ABI record was not produced with C17 compiler options", span);
    }
    /* R-FFI-0044: the record names the C implementation the target manifest pins. */
    if (record->compiler_identity == NULL) {
        *failed = true;
        *poisoned = true;
        return r_abi_diagnostic(context,
                                "R-FFI-0044",
                                "ABI record lacks the C compiler identity it was produced with",
                                span);
    }
    if ((compiler_build != NULL) && !r_abi_text_contains(record->compiler_identity,
                                                         record->compiler_identity_length,
                                                         compiler_build,
                                                         compiler_build_length)) {
        *failed = true;
        *poisoned = true;
        return r_abi_diagnostic(
            context,
            "R-FFI-0044",
            "ABI record compiler identity disagrees with the target manifest toolchain",
            span);
    }
    return true;
}

/* ---- R-declared structs in import prototypes (R-FFI-0021, R-FFI-0041) ----------------- */

/* One walk of an import's C type against its R signature. Slots are collected for the import
   in the order the C spellers visit its R-declared struct leaves: the parameters in order,
   then the result, depth first; a raw function type likewise. */
typedef struct RAbiMatch {
    RFrontendContext *context;
    const RAbiRecordEntry *record;
    RSemanticSymbol *symbol;
    uint32_t position;
    bool collect;
    RSourceSpan span;
    bool *failed;
} RAbiMatch;

static bool
r_abi_verify_r_struct(RAbiMatch *match, uint32_t aggregate_id, size_t abi_type, RSourceSpan span);

static const RSemanticAggregate *r_abi_aggregate(const RFrontendContext *context,
                                                 const RSemanticType *type,
                                                 uint32_t *aggregate_id) {
    const uint32_t id = (type == NULL) ? UINT32_C(0) : (uint32_t)type->base;

    if ((type == NULL) ||
        ((type->kind != R_SEMANTIC_TYPE_STRUCT) && (type->kind != R_SEMANTIC_TYPE_ENUM)) ||
        (id == UINT32_C(0)) || ((size_t)id > context->semantic_aggregate_count)) {
        return NULL;
    }
    if (aggregate_id != NULL) {
        *aggregate_id = id;
    }
    return &context->semantic_aggregates[(size_t)id - 1U];
}

/* An aggregate declared in R with @repr(C), outside every extern "C" block. */
static bool r_abi_is_r_declared(const RSemanticAggregate *aggregate) {
    return (aggregate != NULL) && aggregate->is_repr_c && !aggregate->is_opaque &&
           !aggregate->is_c_declared && !aggregate->is_tagged;
}

/* Whether a C ABI type mentions an R-declared struct or enum at any depth. */
static bool
r_abi_type_mentions_r_declared(const RFrontendContext *context, RTypeId type_id, uint32_t depth) {
    const RSemanticType *type = r_semantic_type(context, type_id);

    if ((type == NULL) || (depth > context->options.limits.max_nesting)) {
        return false;
    }
    switch (type->kind) {
    case R_SEMANTIC_TYPE_CONST:
    case R_SEMANTIC_TYPE_RAW:
    case R_SEMANTIC_TYPE_FIXED_ARRAY:
        return r_abi_type_mentions_r_declared(context, type->base, depth + UINT32_C(1));
    case R_SEMANTIC_TYPE_RAW_FUNCTION: {
        RTypeId parameter = type->second;
        uint64_t index;

        if (r_abi_type_mentions_r_declared(context, type->base, depth + UINT32_C(1))) {
            return true;
        }
        for (index = UINT64_C(0); index < type->length; ++index) {
            const RSemanticType *item = r_semantic_type(context, parameter);

            if ((item == NULL) || (item->kind != R_SEMANTIC_TYPE_FUNCTION_PARAMETER)) {
                return false;
            }
            if (r_abi_type_mentions_r_declared(context, item->base, depth + UINT32_C(1))) {
                return true;
            }
            parameter = item->second;
        }
        return false;
    }
    case R_SEMANTIC_TYPE_STRUCT:
    case R_SEMANTIC_TYPE_ENUM:
        return r_abi_is_r_declared(r_abi_aggregate(context, type, NULL));
    default:
        return false;
    }
}

static const RAbiTypeNode *r_abi_type_node(const RFrontendContext *context, size_t index) {
    return (index < context->abi_type_node_count) ? &context->abi_type_nodes[index] : NULL;
}

static size_t
r_abi_type_node_child(const RFrontendContext *context, const RAbiTypeNode *node, uint32_t index) {
    const size_t position = node->first_child + (size_t)index;

    return ((index < node->child_count) && (position < context->abi_type_child_count))
               ? context->abi_type_children[position]
               : SIZE_MAX;
}

/* The inventory of a record or enumeration leaf: its tag, else the typedef naming it. */
static size_t r_abi_leaf_type(const RFrontendContext *context,
                              const RAbiRecordEntry *record,
                              const RAbiTypeNode *node) {
    const bool by_tag = node->tag_name_length != 0U;
    const char *name = by_tag ? node->tag_name : node->typedef_name;
    const size_t name_length = by_tag ? node->tag_name_length : node->typedef_name_length;
    const RCTypeKind kind = !by_tag                                ? R_C_TYPE_KIND_TYPEDEF
                            : (node->kind == R_ABI_TYPE_NODE_ENUM) ? R_C_TYPE_KIND_ENUM
                            : node->is_union                       ? R_C_TYPE_KIND_UNION
                                                                   : R_C_TYPE_KIND_STRUCT;
    size_t index;

    if (name == NULL) {
        return SIZE_MAX;
    }
    for (index = 0U; index < record->type_count; ++index) {
        const RAbiRecordType *type = &context->abi_types[record->first_type + index];

        if ((type->kind == kind) && (type->c_name_length == name_length) &&
            (memcmp(type->c_name, name, name_length) == 0)) {
            return record->first_type + index;
        }
    }
    return SIZE_MAX;
}

static bool r_abi_mismatch(RAbiMatch *match, const char *rule_id, const char *message) {
    *match->failed = true;
    if (match->symbol != NULL) {
        match->symbol->poisoned = true;
    }
    return r_abi_diagnostic(match->context, rule_id, message, match->span);
}

static bool r_abi_match_type(RAbiMatch *match, RTypeId type_id, size_t node_index, uint32_t depth);

/* A C function type against an R prototype: parameters in order, then the result. */
static bool r_abi_match_function(RAbiMatch *match,
                                 RTypeId result,
                                 const RTypeId *parameters,
                                 RTypeId parameter_chain,
                                 uint64_t parameter_count,
                                 bool variadic,
                                 bool top_level,
                                 size_t node_index,
                                 uint32_t depth) {
    const RAbiTypeNode *node = r_abi_type_node(match->context, node_index);
    RTypeId parameter = parameter_chain;
    uint64_t index;

    if ((node == NULL) || (node->kind != R_ABI_TYPE_NODE_FUNCTION) ||
        (node->child_count == UINT32_C(0)) ||
        ((uint64_t)node->child_count - UINT64_C(1) != parameter_count) ||
        (node->is_variadic != variadic)) {
        return r_abi_mismatch(
            match, "R-FFI-0021", "ABI record prototype disagrees with the import signature");
    }
    for (index = UINT64_C(0); index < parameter_count; ++index) {
        RTypeId parameter_type;

        if (parameters != NULL) {
            parameter_type = parameters[index];
        } else {
            const RSemanticType *item = r_semantic_type(match->context, parameter);

            if ((item == NULL) || (item->kind != R_SEMANTIC_TYPE_FUNCTION_PARAMETER)) {
                match->context->resource_status = R_FRONTEND_INTERNAL_ERROR;
                return false;
            }
            parameter_type = item->base;
            parameter = item->second;
        }
        if (top_level) {
            match->position = (uint32_t)index + UINT32_C(1);
        }
        if (!r_abi_match_type(match,
                              parameter_type,
                              r_abi_type_node_child(match->context, node, (uint32_t)index + 1U),
                              depth + UINT32_C(1))) {
            return false;
        }
        if (*match->failed) {
            /* The first disagreement is reported; the walk stops there. */
            return true;
        }
    }
    if (top_level) {
        match->position = UINT32_C(0);
    }
    return r_abi_match_type(
        match, result, r_abi_type_node_child(match->context, node, UINT32_C(0)), depth + 1U);
}

/* The R underlying type of an R-declared enum against an enumeration or integer leaf. */
static bool r_abi_match_r_enum(RAbiMatch *match,
                               const RSemanticAggregate *aggregate,
                               const RAbiTypeNode *node) {
    char spelling[R_ABI_SPELL_CAPACITY];
    const char *c_spelling = NULL;
    size_t c_spelling_length = 0U;

    if (!r_c_abi_spell_header_type(
            match->context, aggregate->enum_underlying_type, spelling, sizeof(spelling))) {
        match->context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    if (node->kind == R_ABI_TYPE_NODE_SCALAR) {
        c_spelling = node->spelling;
        c_spelling_length = node->spelling_length;
    } else if (node->kind == R_ABI_TYPE_NODE_ENUM) {
        const size_t type_index = r_abi_leaf_type(match->context, match->record, node);

        if ((type_index != SIZE_MAX) && !match->context->abi_types[type_index].inexpressible) {
            c_spelling = match->context->abi_types[type_index].underlying;
            c_spelling_length = match->context->abi_types[type_index].underlying_length;
        }
    }
    if ((c_spelling == NULL) ||
        !r_abi_spelling_equal(spelling, strlen(spelling), c_spelling, c_spelling_length)) {
        return r_abi_mismatch(match,
                              "R-FFI-0021",
                              "C type at the position of an R-declared @repr(C) enum is not its "
                              "underlying integer type");
    }
    return true;
}

static bool r_abi_match_type(RAbiMatch *match, RTypeId type_id, size_t node_index, uint32_t depth) {
    RFrontendContext *context = match->context;
    const RSemanticType *type = r_semantic_type(context, type_id);
    const RAbiTypeNode *node = r_abi_type_node(context, node_index);
    const RSemanticAggregate *aggregate;
    uint32_t aggregate_id = UINT32_C(0);

    if ((type == NULL) || (depth > context->options.limits.max_nesting)) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    if (type->kind == R_SEMANTIC_TYPE_CONST) {
        return r_abi_match_type(match, type->base, node_index, depth + UINT32_C(1));
    }
    if (!r_abi_type_mentions_r_declared(context, type_id, UINT32_C(0))) {
        /* Everything else is proven by the verifier's prototype assignment (R-FFI-0042). */
        return true;
    }
    if (node == NULL) {
        return r_abi_mismatch(
            match, "R-FFI-0041", "ABI record carries no C type for an R-declared position");
    }
    switch (type->kind) {
    case R_SEMANTIC_TYPE_RAW:
        if (node->kind != R_ABI_TYPE_NODE_POINTER) {
            return r_abi_mismatch(
                match, "R-FFI-0021", "ABI record prototype disagrees with the import signature");
        }
        return r_abi_match_type(
            match, type->base, r_abi_type_node_child(context, node, 0U), depth + UINT32_C(1));
    case R_SEMANTIC_TYPE_RAW_FUNCTION: {
        const RAbiTypeNode *pointee =
            (node->kind == R_ABI_TYPE_NODE_POINTER)
                ? r_abi_type_node(context, r_abi_type_node_child(context, node, 0U))
                : NULL;

        if (pointee == NULL) {
            return r_abi_mismatch(
                match, "R-FFI-0021", "ABI record prototype disagrees with the import signature");
        }
        return r_abi_match_function(match,
                                    type->base,
                                    NULL,
                                    type->second,
                                    type->length,
                                    (type->flags & R_SEMANTIC_TYPE_FLAG_VARIADIC) != 0U,
                                    false,
                                    r_abi_type_node_child(context, node, 0U),
                                    depth + UINT32_C(1));
    }
    case R_SEMANTIC_TYPE_FIXED_ARRAY:
        if ((node->kind != R_ABI_TYPE_NODE_ARRAY) || (node->length != type->length)) {
            return r_abi_mismatch(match,
                                  "R-FFI-0041",
                                  "ABI record array member disagrees with the R-declared "
                                  "@repr(C) struct field");
        }
        return r_abi_match_type(
            match, type->base, r_abi_type_node_child(context, node, 0U), depth + UINT32_C(1));
    case R_SEMANTIC_TYPE_ENUM:
        aggregate = r_abi_aggregate(context, type, NULL);
        return r_abi_match_r_enum(match, aggregate, node);
    case R_SEMANTIC_TYPE_STRUCT: {
        size_t type_index;

        aggregate = r_abi_aggregate(context, type, &aggregate_id);
        type_index = (node->kind == R_ABI_TYPE_NODE_RECORD)
                         ? r_abi_leaf_type(context, match->record, node)
                         : SIZE_MAX;
        if ((node->kind != R_ABI_TYPE_NODE_RECORD) || node->is_union) {
            return r_abi_mismatch(match,
                                  "R-FFI-0021",
                                  "C type at the position of an R-declared @repr(C) struct is not "
                                  "a struct");
        }
        if ((type_index == SIZE_MAX) || context->abi_types[type_index].inexpressible) {
            return r_abi_mismatch(match,
                                  "R-FFI-0019",
                                  "C struct at the position of an R-declared @repr(C) struct has "
                                  "no complete inventory expressible in R");
        }
        if (match->collect) {
            if (!r_grow_array(context,
                              (void **)&context->c_import_slots,
                              &context->c_import_slot_capacity,
                              sizeof(*context->c_import_slots),
                              context->c_import_slot_count + 1U)) {
                return false;
            }
            context->c_import_slots[context->c_import_slot_count].type_node = node_index;
            context->c_import_slots[context->c_import_slot_count].position = match->position;
            context->c_import_slot_count += 1U;
            match->symbol->c_import_slot_count += 1U;
        }
        return r_abi_verify_r_struct(match, aggregate_id, type_index, match->span);
    }
    default:
        return true;
    }
}

/* R-FFI-0041: an R-declared struct against the complete member inventory of the C struct at
   its position: count, names, order, member types, offsets, size and alignment. */
static bool
r_abi_verify_r_struct(RAbiMatch *match, uint32_t aggregate_id, size_t abi_type, RSourceSpan span) {
    RFrontendContext *context = match->context;
    const RSemanticAggregate *aggregate = &context->semantic_aggregates[(size_t)aggregate_id - 1U];
    const RAbiRecordType *type = &context->abi_types[abi_type];
    RAbiMatch member_match = *match;
    uint64_t offset = UINT64_C(0);
    uint64_t alignment = UINT64_C(1);
    uint32_t field_index;
    size_t pair;
    char spelling[R_ABI_SPELL_CAPACITY];

    (void)span;
    for (pair = 0U; pair < context->c_repr_pair_count; ++pair) {
        if ((context->c_repr_pairs[pair].aggregate_id == aggregate_id) &&
            (context->c_repr_pairs[pair].abi_type == abi_type)) {
            /* Proven, being proven (a recursive struct), or already reported. */
            return true;
        }
    }
    if (!r_grow_array(context,
                      (void **)&context->c_repr_pairs,
                      &context->c_repr_pair_capacity,
                      sizeof(*context->c_repr_pairs),
                      context->c_repr_pair_count + 1U)) {
        return false;
    }
    pair = context->c_repr_pair_count;
    context->c_repr_pairs[pair].aggregate_id = aggregate_id;
    context->c_repr_pairs[pair].abi_type = abi_type;
    context->c_repr_pairs[pair].verified = false;
    context->c_repr_pair_count += 1U;
    member_match.collect = false;
    member_match.span = aggregate->name_span;
    if ((type->tag_kind != R_C_TYPE_KIND_STRUCT) || !type->has_layout ||
        ((size_t)type->member_count != (size_t)aggregate->field_count)) {
        return r_abi_mismatch(&member_match,
                              "R-FFI-0041",
                              "ABI record member inventory count disagrees with the R-declared "
                              "@repr(C) struct");
    }
    for (field_index = UINT32_C(0); field_index < aggregate->field_count; ++field_index) {
        const size_t position = (size_t)aggregate->first_field + (size_t)field_index;
        const RSemanticField *field = &context->semantic_fields[position];
        const RAbiRecordMember *member = &context->abi_members[type->first_member + field_index];
        const char *name;
        size_t name_length;
        uint64_t field_size;
        uint64_t field_alignment;
        uint64_t aligned;

        if ((position >= context->semantic_field_count) ||
            !r_abi_intern_text(context, field->name_intern_id, &name, &name_length)) {
            context->resource_status = R_FRONTEND_INTERNAL_ERROR;
            return false;
        }
        member_match.span = field->name_span;
        if ((name_length != member->name_length) ||
            (memcmp(name, member->name, name_length) != 0)) {
            return r_abi_mismatch(&member_match,
                                  "R-FFI-0041",
                                  "ABI record member name or order disagrees with the "
                                  "R-declared @repr(C) struct field");
        }
        if (r_abi_type_mentions_r_declared(context, field->type, UINT32_C(0))) {
            if (!r_abi_match_type(&member_match, field->type, member->type_node, UINT32_C(0))) {
                return false;
            }
            if (*match->failed) {
                return true;
            }
        } else if (!r_c_abi_spell_header_type(context, field->type, spelling, sizeof(spelling)) ||
                   (!r_abi_spelling_equal(
                        spelling, strlen(spelling), member->type, member->type_length) &&
                    ((member->desugared_type == NULL) ||
                     !r_abi_spelling_equal(spelling,
                                           strlen(spelling),
                                           member->desugared_type,
                                           member->desugared_type_length)))) {
            return r_abi_mismatch(&member_match,
                                  "R-FFI-0041",
                                  "ABI record member type disagrees with the R-declared "
                                  "@repr(C) struct field type");
        }
        if (!r_semantic_type_layout(context, field->type, &field_size, &field_alignment) ||
            (field_alignment == UINT64_C(0))) {
            context->resource_status = R_FRONTEND_INTERNAL_ERROR;
            return false;
        }
        aligned = offset + ((field_alignment - (offset % field_alignment)) % field_alignment);
        if (aligned != member->offset) {
            return r_abi_mismatch(&member_match,
                                  "R-FFI-0041",
                                  "ABI record member offset disagrees with the R-declared "
                                  "@repr(C) struct layout");
        }
        offset = aligned + field_size;
        if (field_alignment > alignment) {
            alignment = field_alignment;
        }
    }
    offset += (alignment - (offset % alignment)) % alignment;
    if (aggregate->field_count == UINT32_C(0)) {
        offset = UINT64_C(0);
    }
    member_match.span = aggregate->name_span;
    if ((offset != type->size) || (alignment != type->alignment)) {
        return r_abi_mismatch(&member_match,
                              "R-FFI-0041",
                              "ABI record size or alignment disagrees with the R-declared "
                              "@repr(C) struct layout");
    }
    context->c_repr_pairs[pair].verified = true;
    return true;
}

static const RAbiRecordSymbol *r_abi_find_symbol(const RFrontendContext *context,
                                                 const RAbiRecordEntry *record,
                                                 const RSemanticSymbol *symbol) {
    const char *name;
    size_t name_length;
    size_t index;

    if (!r_abi_intern_text(context, symbol->c_name_intern_id, &name, &name_length)) {
        return NULL;
    }
    for (index = 0U; index < record->symbol_count; ++index) {
        const RAbiRecordSymbol *candidate = &context->abi_symbols[record->first_symbol + index];

        if ((candidate->c_name_length == name_length) &&
            (memcmp(candidate->c_name, name, name_length) == 0) &&
            (candidate->is_object == (symbol->kind == R_SEMANTIC_SYMBOL_MODULE_OBJECT))) {
            return candidate;
        }
    }
    return NULL;
}

/* Every import that passes an R-declared struct is proven against its record prototype. */
static bool r_abi_verify_imports(RFrontendContext *context,
                                 const uint8_t *target_triple,
                                 size_t target_triple_length,
                                 const uint8_t *compiler_build,
                                 size_t compiler_build_length,
                                 bool *failed) {
    size_t index;

    for (index = 0U; index < context->semantic_symbol_count; ++index) {
        RSemanticSymbol *symbol = &context->semantic_symbols[index];
        const RExternBlockRecord *block;
        const RAbiRecordEntry *record;
        const RAbiRecordSymbol *prototype;
        const char *text;
        size_t text_length;
        RAbiMatch match;

        if (!symbol->is_import || !symbol->passes_r_struct || symbol->poisoned ||
            !symbol->signature_supported) {
            continue;
        }
        if ((symbol->extern_block == UINT32_C(0)) ||
            ((size_t)symbol->extern_block > context->extern_block_count)) {
            context->resource_status = R_FRONTEND_INTERNAL_ERROR;
            return false;
        }
        block = &context->extern_blocks[(size_t)symbol->extern_block - 1U];
        if (block->abi_name_intern_id == UINT32_C(0)) {
            /* Reported while collecting the import (R-FFI-0041). */
            continue;
        }
        if (!r_abi_intern_text(context, block->abi_name_intern_id, &text, &text_length)) {
            context->resource_status = R_FRONTEND_INTERNAL_ERROR;
            return false;
        }
        record = context->abi_records_loaded ? r_abi_find_record(context, text, text_length) : NULL;
        if (record == NULL) {
            *failed = true;
            symbol->poisoned = true;
            if (!r_abi_diagnostic(context,
                                  "R-FFI-0041",
                                  "an import that passes an R-declared @repr(C) struct requires "
                                  "the ABI record named by @abi (pass --abi-record)",
                                  symbol->name_span)) {
                return false;
            }
            continue;
        }
        if (!r_abi_verify_record_identity(context,
                                          &symbol->poisoned,
                                          symbol->name_span,
                                          block,
                                          record,
                                          target_triple,
                                          target_triple_length,
                                          compiler_build,
                                          compiler_build_length,
                                          failed)) {
            return false;
        }
        if (symbol->poisoned) {
            continue;
        }
        prototype = r_abi_find_symbol(context, record, symbol);
        if (prototype == NULL) {
            *failed = true;
            symbol->poisoned = true;
            if (!r_abi_diagnostic(context,
                                  "R-FFI-0041",
                                  "ABI record has no C type for the import (regenerate it from "
                                  "--emit=abi-inventory)",
                                  symbol->name_span)) {
                return false;
            }
            continue;
        }
        (void)memset(&match, 0, sizeof(match));
        match.context = context;
        match.record = record;
        match.symbol = symbol;
        match.collect = true;
        match.span = symbol->name_span;
        match.failed = failed;
        symbol->first_c_import_slot = (uint32_t)context->c_import_slot_count;
        symbol->c_import_slot_count = UINT32_C(0);
        if (symbol->kind == R_SEMANTIC_SYMBOL_MODULE_OBJECT) {
            if (!r_abi_match_type(&match, symbol->type, prototype->type_node, UINT32_C(0))) {
                return false;
            }
        } else if (!r_abi_match_function(
                       &match,
                       symbol->return_type,
                       &context->semantic_parameter_types[symbol->first_parameter_type],
                       R_TYPE_ID_INVALID,
                       symbol->parameter_count,
                       symbol->is_variadic,
                       true,
                       prototype->type_node,
                       UINT32_C(0))) {
            return false;
        }
    }
    return true;
}

RFrontendStatus r_frontend_verify_abi_records(RFrontendContext *context,
                                              const uint8_t *target_manifest,
                                              size_t target_manifest_length) {
    size_t index;
    bool failed = false;
    const uint8_t *target_triple = NULL;
    size_t target_triple_length = 0U;
    const uint8_t *compiler_build = NULL;
    size_t compiler_build_length = 0U;

    if ((context == NULL) || !context->semantic_analyzed) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    (void)r_abi_manifest_target_triple(
        target_manifest, target_manifest_length, &target_triple, &target_triple_length);
    (void)r_abi_manifest_compiler_build(
        target_manifest, target_manifest_length, &compiler_build, &compiler_build_length);
    for (index = 0U; index < context->semantic_aggregate_count; ++index) {
        RSemanticAggregate *aggregate = &context->semantic_aggregates[index];
        const RExternBlockRecord *block;
        const RAbiRecordEntry *record;
        const RAbiRecordType *type;
        const char *text;
        size_t text_length;
        bool success;

        if (!aggregate->is_c_declared || aggregate->poisoned) {
            continue;
        }
        if ((aggregate->extern_block == UINT32_C(0)) ||
            ((size_t)aggregate->extern_block > context->extern_block_count)) {
            return R_FRONTEND_INTERNAL_ERROR;
        }
        block = &context->extern_blocks[(size_t)aggregate->extern_block - 1U];
        if (block->abi_name_intern_id == UINT32_C(0)) {
            /* Reported while collecting the block (R-FFI-0041). */
            continue;
        }
        if (!r_abi_intern_text(context, block->abi_name_intern_id, &text, &text_length)) {
            return R_FRONTEND_INTERNAL_ERROR;
        }
        record = context->abi_records_loaded ? r_abi_find_record(context, text, text_length) : NULL;
        if (record == NULL) {
            failed = true;
            aggregate->poisoned = true;
            if (!r_abi_diagnostic(context,
                                  "R-FFI-0041",
                                  "complete C aggregate requires the ABI record named by @abi "
                                  "(pass --abi-record)",
                                  aggregate->name_span)) {
                return context->resource_status;
            }
            continue;
        }
        if (!r_abi_verify_record_identity(context,
                                          &aggregate->poisoned,
                                          aggregate->name_span,
                                          block,
                                          record,
                                          target_triple,
                                          target_triple_length,
                                          compiler_build,
                                          compiler_build_length,
                                          &failed)) {
            return context->resource_status != R_FRONTEND_OK ? context->resource_status
                                                             : R_FRONTEND_INTERNAL_ERROR;
        }
        if (aggregate->poisoned) {
            continue;
        }
        if (!r_abi_intern_text(context, aggregate->c_name_intern_id, &text, &text_length)) {
            return R_FRONTEND_INTERNAL_ERROR;
        }
        type = r_abi_find_type(context, record, text, text_length);
        if (type == NULL) {
            failed = true;
            aggregate->poisoned = true;
            if (!r_abi_diagnostic(context,
                                  "R-FFI-0041",
                                  "ABI record has no inventory for the C type named by @c_type",
                                  aggregate->name_span)) {
                return context->resource_status;
            }
            continue;
        }
        if (type->kind != aggregate->c_type_kind) {
            failed = true;
            aggregate->poisoned = true;
            if (!r_abi_diagnostic(context,
                                  "R-FFI-0016",
                                  "ABI record spelling kind disagrees with @c_type",
                                  aggregate->name_span)) {
                return context->resource_status;
            }
            continue;
        }
        success = aggregate->kind == R_SEMANTIC_AGGREGATE_STRUCT
                      ? r_abi_verify_struct(context, aggregate, type, &failed)
                      : r_abi_verify_enum(context, aggregate, type, &failed);
        if (!success) {
            return context->resource_status != R_FRONTEND_OK ? context->resource_status
                                                             : R_FRONTEND_INTERNAL_ERROR;
        }
    }
    if (!r_abi_verify_imports(context,
                              target_triple,
                              target_triple_length,
                              compiler_build,
                              compiler_build_length,
                              &failed)) {
        return context->resource_status != R_FRONTEND_OK ? context->resource_status
                                                         : R_FRONTEND_INTERNAL_ERROR;
    }
    return failed ? R_FRONTEND_INVALID_SOURCE : R_FRONTEND_OK;
}

/* ---- header digests (R-FFI-0044) ------------------------------------------------------- */

static bool r_abi_hex_digit_value(char digit, uint8_t *value) {
    if ((digit >= '0') && (digit <= '9')) {
        *value = (uint8_t)(digit - '0');
        return true;
    }
    if ((digit >= 'a') && (digit <= 'f')) {
        *value = (uint8_t)(10 + (digit - 'a'));
        return true;
    }
    if ((digit >= 'A') && (digit <= 'F')) {
        *value = (uint8_t)(10 + (digit - 'A'));
        return true;
    }
    return false;
}

/* The recorded digest is 64 hexadecimal digits; anything else never matches a header. */
static bool r_abi_digest_matches(const RAbiRecordHeader *header, const uint8_t digest[32]) {
    size_t index;

    if (header->sha256_length != 64U) {
        return false;
    }
    for (index = 0U; index < 32U; ++index) {
        uint8_t high;
        uint8_t low;

        if (!r_abi_hex_digit_value(header->sha256[index * 2U], &high) ||
            !r_abi_hex_digit_value(header->sha256[(index * 2U) + 1U], &low) ||
            (digest[index] != (uint8_t)((high << 4U) | low))) {
            return false;
        }
    }
    return true;
}

/*
 * Verifies the headers of one record once. The aggregate anchors the diagnostics: every
 * complete aggregate of a block naming a stale record is poisoned so that no C is emitted.
 */
static bool r_abi_verify_record_headers(RFrontendContext *context,
                                        RSemanticAggregate *aggregate,
                                        const RAbiRecordEntry *record,
                                        const RFrontendAbiHeaderSource *source,
                                        bool *failed) {
    size_t index;

    for (index = 0U; index < record->header_count; ++index) {
        const RAbiRecordHeader *header = &context->abi_headers[record->first_header + index];
        uint8_t *bytes = NULL;
        size_t length = 0U;
        uint8_t digest[32];
        bool matches;

        if (!source->read(
                source->user_data, header->spelling, header->spelling_length, &bytes, &length)) {
            *failed = true;
            aggregate->poisoned = true;
            return r_abi_diagnostic(context,
                                    "R-FFI-0044",
                                    "ABI record header is not available under the header roots "
                                    "of this build (pass --abi-header-dir)",
                                    aggregate->name_span);
        }
        r_sha256_digest(bytes, length, digest);
        source->release(source->user_data, bytes);
        matches = r_abi_digest_matches(header, digest);
        if (!matches) {
            *failed = true;
            aggregate->poisoned = true;
            return r_abi_diagnostic(context,
                                    "R-FFI-0044",
                                    "ABI record header digest disagrees with the header of this "
                                    "build; regenerate the record",
                                    aggregate->name_span);
        }
    }
    return true;
}

RFrontendStatus r_frontend_verify_abi_record_headers(RFrontendContext *context,
                                                     const RFrontendAbiHeaderSource *source) {
    size_t index;
    bool failed = false;

    if ((context == NULL) || !context->semantic_analyzed || (source == NULL) ||
        (source->read == NULL) || (source->release == NULL)) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    if (!context->abi_records_loaded) {
        return R_FRONTEND_OK;
    }
    for (index = 0U; index < context->semantic_aggregate_count; ++index) {
        RSemanticAggregate *aggregate = &context->semantic_aggregates[index];
        const RExternBlockRecord *block;
        const RAbiRecordEntry *record;
        const char *text;
        size_t text_length;

        if (!aggregate->is_c_declared || aggregate->poisoned ||
            (aggregate->extern_block == UINT32_C(0)) ||
            ((size_t)aggregate->extern_block > context->extern_block_count)) {
            continue;
        }
        block = &context->extern_blocks[(size_t)aggregate->extern_block - 1U];
        if ((block->abi_name_intern_id == UINT32_C(0)) ||
            !r_abi_intern_text(context, block->abi_name_intern_id, &text, &text_length)) {
            continue;
        }
        record = r_abi_find_record(context, text, text_length);
        if ((record == NULL) ||
            !r_abi_verify_record_headers(context, aggregate, record, source, &failed)) {
            if (record == NULL) {
                continue;
            }
            return context->resource_status != R_FRONTEND_OK ? context->resource_status
                                                             : R_FRONTEND_INTERNAL_ERROR;
        }
    }
    return failed ? R_FRONTEND_INVALID_SOURCE : R_FRONTEND_OK;
}

/* ---- inventory request ---------------------------------------------------------------- */

static bool r_abi_write_json_string(RFrontendWriteFn writer,
                                    void *user_data,
                                    const char *bytes,
                                    size_t length) {
    size_t index;

    if (!r_write_text(writer, user_data, "\"")) {
        return false;
    }
    for (index = 0U; index < length; ++index) {
        const unsigned char value = (unsigned char)bytes[index];
        char escaped[8];

        if ((value == '"') || (value == '\\')) {
            escaped[0] = '\\';
            escaped[1] = (char)value;
            if (!writer(user_data, escaped, 2U)) {
                return false;
            }
        } else if (value < 0x20U) {
            (void)snprintf(escaped, sizeof(escaped), "\\u%04x", (unsigned int)value);
            if (!r_write_text(writer, user_data, escaped)) {
                return false;
            }
        } else if (!writer(user_data, bytes + index, 1U)) {
            return false;
        }
    }
    return r_write_text(writer, user_data, "\"");
}

static const char *r_abi_kind_name(RCTypeKind kind) {
    switch (kind) {
    case R_C_TYPE_KIND_STRUCT:
        return "struct";
    case R_C_TYPE_KIND_UNION:
        return "union";
    case R_C_TYPE_KIND_ENUM:
        return "enum";
    case R_C_TYPE_KIND_TYPEDEF:
        return "typedef";
    case R_C_TYPE_KIND_NONE:
    default:
        return "";
    }
}

static int r_abi_compare_blocks(const RFrontendContext *context,
                                const RExternBlockRecord *left,
                                const RExternBlockRecord *right) {
    const RSource *left_source = r_get_source_const(context, left->source);
    const RSource *right_source = r_get_source_const(context, right->source);
    const char *left_name =
        (left_source == NULL) || (left_source->module_name == NULL) ? "" : left_source->module_name;
    const char *right_name = (right_source == NULL) || (right_source->module_name == NULL)
                                 ? ""
                                 : right_source->module_name;
    const int comparison = strcmp(left_name, right_name);

    if (comparison != 0) {
        return comparison;
    }
    if (left->span_start != right->span_start) {
        return left->span_start < right->span_start ? -1 : 1;
    }
    return 0;
}

/* Writes one request entry for a block naming an @abi record. */
static bool r_abi_write_block_request(const RFrontendContext *context,
                                      const RExternBlockRecord *block,
                                      uint32_t block_index,
                                      RFrontendWriteFn writer,
                                      void *user_data) {
    const char *text;
    size_t text_length;
    uint32_t header_index;
    size_t index;
    bool first;

    if (!r_abi_intern_text(context, block->abi_name_intern_id, &text, &text_length) ||
        !r_write_text(writer, user_data, "    {\n      \"name\": ") ||
        !r_abi_write_json_string(writer, user_data, text, text_length) ||
        !r_write_text(writer, user_data, ",\n      \"provider\": ")) {
        return false;
    }
    if (block->link_name_intern_id != UINT32_C(0)) {
        if (!r_abi_intern_text(context, block->link_name_intern_id, &text, &text_length) ||
            !r_abi_write_json_string(writer, user_data, text, text_length)) {
            return false;
        }
    } else if (!r_write_text(writer, user_data, "\"\"")) {
        return false;
    }
    if (!r_write_text(writer, user_data, ",\n      \"feature_test_definitions\": [")) {
        return false;
    }
    first = true;
    if ((block->link_provider != UINT32_C(0)) &&
        ((size_t)block->link_provider <= context->link_provider_count)) {
        const RLinkProviderRecord *provider =
            &context->link_providers[(size_t)block->link_provider - 1U];

        for (index = 0U; index < provider->definition_count; ++index) {
            const RLinkDefinitionRecord *definition =
                &context->link_definitions[provider->first_definition + index];

            if ((!first && !r_write_text(writer, user_data, ", ")) ||
                !r_write_text(writer, user_data, "\"") ||
                !writer(user_data, definition->name, definition->name_length) ||
                ((definition->value_length != 0U) &&
                 (!r_write_text(writer, user_data, "=") ||
                  !writer(user_data, definition->value, definition->value_length))) ||
                !r_write_text(writer, user_data, "\"")) {
                return false;
            }
            first = false;
        }
    }
    if (!r_write_text(writer, user_data, "],\n      \"headers\": [")) {
        return false;
    }
    for (header_index = UINT32_C(0); header_index < block->header_count; ++header_index) {
        const size_t position = (size_t)block->first_header + (size_t)header_index;

        if ((position >= context->extern_header_count) ||
            !r_abi_intern_text(context, context->extern_headers[position], &text, &text_length) ||
            ((header_index != UINT32_C(0)) && !r_write_text(writer, user_data, ", ")) ||
            !r_abi_write_json_string(writer, user_data, text, text_length)) {
            return false;
        }
    }
    if (!r_write_text(writer, user_data, "],\n      \"types\": [")) {
        return false;
    }
    first = true;
    for (index = 0U; index < context->semantic_aggregate_count; ++index) {
        const RSemanticAggregate *aggregate = &context->semantic_aggregates[index];

        if (!aggregate->is_c_declared || (aggregate->extern_block != block_index) ||
            !r_abi_intern_text(context, aggregate->c_name_intern_id, &text, &text_length)) {
            continue;
        }
        if ((!first && !r_write_text(writer, user_data, ", ")) ||
            !r_write_text(writer, user_data, "{\"c_name\": ") ||
            !r_abi_write_json_string(writer, user_data, text, text_length) ||
            !r_write_text(writer, user_data, ", \"kind\": \"") ||
            !r_write_text(writer, user_data, r_abi_kind_name(aggregate->c_type_kind)) ||
            !r_write_text(writer, user_data, "\"}")) {
            return false;
        }
        first = false;
    }
    /* R-FFI-0021: the C type of every import that passes an R-declared @repr(C) struct. */
    if (!r_write_text(writer, user_data, "],\n      \"symbols\": [")) {
        return false;
    }
    first = true;
    for (index = 0U; index < context->semantic_symbol_count; ++index) {
        const RSemanticSymbol *symbol = &context->semantic_symbols[index];

        if (!symbol->is_import || !symbol->passes_r_struct || symbol->poisoned ||
            (symbol->extern_block != block_index) ||
            !r_abi_intern_text(context, symbol->c_name_intern_id, &text, &text_length)) {
            continue;
        }
        if ((!first && !r_write_text(writer, user_data, ", ")) ||
            !r_write_text(writer, user_data, "{\"c_name\": ") ||
            !r_abi_write_json_string(writer, user_data, text, text_length) ||
            !r_write_text(writer,
                          user_data,
                          symbol->kind == R_SEMANTIC_SYMBOL_MODULE_OBJECT
                              ? ", \"kind\": \"object\"}"
                              : ", \"kind\": \"function\"}")) {
            return false;
        }
        first = false;
    }
    return r_write_text(writer, user_data, "]\n    }");
}

RFrontendStatus r_frontend_emit_abi_inventory(const RFrontendContext *context,
                                              const RFrontendArtifactOptions *options,
                                              RFrontendWriteFn writer,
                                              void *user_data) {
    const RExternBlockRecord *previous = NULL;
    const uint8_t *target_triple = NULL;
    size_t target_triple_length = 0U;
    bool first = true;
    bool more = true;

    if ((context == NULL) || (writer == NULL) || !context->semantic_analyzed) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    if (options != NULL) {
        (void)r_abi_manifest_target_triple(options->target_manifest,
                                           options->target_manifest_length,
                                           &target_triple,
                                           &target_triple_length);
    }
    if (!r_write_text(writer,
                      user_data,
                      "{\n  \"schema\": \"r-abi-inventory-request-0.1\",\n  \"target_triple\": ") ||
        !r_abi_write_json_string(
            writer, user_data, (const char *)target_triple, target_triple_length) ||
        !r_write_text(writer, user_data, ",\n  \"records\": [\n")) {
        return R_FRONTEND_IO_ERROR;
    }
    /* Blocks in ascending (module, position) order; the next smallest is found each round. */
    while (more) {
        const RExternBlockRecord *best = NULL;
        size_t index;

        for (index = 0U; index < context->extern_block_count; ++index) {
            const RExternBlockRecord *candidate = &context->extern_blocks[index];

            if ((candidate->abi_name_intern_id == UINT32_C(0)) ||
                ((previous != NULL) && (r_abi_compare_blocks(context, candidate, previous) <= 0))) {
                continue;
            }
            if ((best == NULL) || (r_abi_compare_blocks(context, candidate, best) < 0)) {
                best = candidate;
            }
        }
        if (best == NULL) {
            more = false;
        } else {
            const uint32_t block_index = (uint32_t)(best - context->extern_blocks) + UINT32_C(1);

            if ((!first && !r_write_text(writer, user_data, ",\n")) ||
                !r_abi_write_block_request(context, best, block_index, writer, user_data)) {
                return R_FRONTEND_IO_ERROR;
            }
            first = false;
            previous = best;
        }
    }
    if (!r_write_text(writer, user_data, first ? "  ]\n}\n" : "\n  ]\n}\n")) {
        return R_FRONTEND_IO_ERROR;
    }
    return R_FRONTEND_OK;
}

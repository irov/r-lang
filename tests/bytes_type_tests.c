#include "frontend_internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct RBytesTypeTestBuffer {
    char *bytes;
    size_t length;
    size_t capacity;
} RBytesTypeTestBuffer;

static int failures = 0;

#define R_BYTES_TYPE_CHECK(condition)                                                              \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            (void)fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);    \
            failures += 1;                                                                         \
        }                                                                                          \
    } while (false)

static bool r_bytes_type_test_write(void *user_data, const char *bytes, size_t length) {
    RBytesTypeTestBuffer *buffer = user_data;
    size_t required;
    size_t capacity;
    char *replacement;

    if (length > (SIZE_MAX - buffer->length - 1U)) {
        return false;
    }
    required = buffer->length + length + 1U;
    if (required > buffer->capacity) {
        capacity = buffer->capacity == 0U ? 256U : buffer->capacity;
        while (capacity < required) {
            if (capacity > (SIZE_MAX / 2U)) {
                return false;
            }
            capacity *= 2U;
        }
        replacement = realloc(buffer->bytes, capacity);
        if (replacement == NULL) {
            return false;
        }
        buffer->bytes = replacement;
        buffer->capacity = capacity;
    }
    if (length != 0U) {
        (void)memcpy(buffer->bytes + buffer->length, bytes, length);
    }
    buffer->length += length;
    buffer->bytes[buffer->length] = '\0';
    return true;
}

static void r_bytes_type_test_buffer_destroy(RBytesTypeTestBuffer *buffer) {
    free(buffer->bytes);
    (void)memset(buffer, 0, sizeof(*buffer));
}

static RSourceId
r_bytes_type_test_add_source(RFrontendContext *context, const char *name, const char *source) {
    RSourceId source_id = R_SOURCE_ID_INVALID;

    R_BYTES_TYPE_CHECK(
        r_frontend_add_source(context, name, (const uint8_t *)source, strlen(source), &source_id) ==
        R_FRONTEND_OK);
    return source_id;
}

static void r_bytes_type_test_print_diagnostics(const RFrontendContext *context,
                                                const char *case_name) {
    size_t index;

    (void)fprintf(stderr, "%s diagnostics:\n", case_name);
    for (index = 0U; index < r_frontend_diagnostic_count(context); ++index) {
        const RDiagnostic *diagnostic = r_frontend_diagnostic(context, index);

        if (diagnostic != NULL) {
            (void)fprintf(stderr,
                          "  %s %s: %s\n",
                          diagnostic->code,
                          diagnostic->rule_id,
                          diagnostic->message);
        }
    }
}

static bool r_bytes_type_test_has_diagnostic(const RFrontendContext *context,
                                             const char *code,
                                             const char *rule_id) {
    size_t index;

    for (index = 0U; index < r_frontend_diagnostic_count(context); ++index) {
        const RDiagnostic *diagnostic = r_frontend_diagnostic(context, index);

        if ((diagnostic != NULL) && (strcmp(diagnostic->code, code) == 0) &&
            (strcmp(diagnostic->rule_id, rule_id) == 0)) {
            return true;
        }
    }
    return false;
}

static bool
r_bytes_type_test_token_is_text(const RSource *source, const RToken *token, const char *text) {
    const size_t length = strlen(text);

    return (token->span.end >= token->span.start) &&
           ((size_t)(token->span.end - token->span.start) == length) &&
           ((size_t)token->span.end <= source->length) &&
           (memcmp(source->bytes + token->span.start, text, length) == 0);
}

static void r_bytes_type_test_parser(void) {
    static const char source[] = "module bytes.parser;\n"
                                 "enum marker { bytes, };\n"
                                 "struct digest {\n"
                                 "    u8[20] bytes;\n"
                                 "    bytes payload;\n"
                                 "    bytes[2] fixed;\n"
                                 "    bytes[] view;\n"
                                 "    bytes* borrowed;\n"
                                 "    const bytes* shared;\n"
                                 "    arc bytes shared_owner;\n"
                                 "    weak arc bytes weak_owner;\n"
                                 "    own bytes* unique;\n"
                                 "    raw const bytes*? raw_view;\n"
                                 "};\n"
                                 "i32 bytes() { return 7; }\n"
                                 "i32 parse_names() {\n"
                                 "    bytes();\n"
                                 "    i32 scalar = 0;\n"
                                 "    bytes = scalar;\n"
                                 "    bytes.member = scalar;\n"
                                 "    bytes[0] = scalar;\n"
                                 "    bytes owned = {};\n"
                                 "    bytes bytes = {};\n"
                                 "    bytes[2] fixed = {};\n"
                                 "    bytes[] view = fixed;\n"
                                 "    return scalar;\n"
                                 "}\n";
    RFrontendContext *context = r_frontend_create(NULL);
    RSourceId source_id;
    RSyntaxNodeId cst = R_SYNTAX_NODE_ID_INVALID;
    RAstNodeId ast = R_AST_NODE_ID_INVALID;
    RBytesTypeTestBuffer dump = {0};
    const RSource *parsed_source;
    size_t token_index;
    size_t bytes_token_count = 0U;

    R_BYTES_TYPE_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    source_id = r_bytes_type_test_add_source(context, "bytes-parser.r", source);
    R_BYTES_TYPE_CHECK(r_frontend_lex(context, source_id) == R_FRONTEND_OK);
    parsed_source = &context->sources[(size_t)source_id - 1U];
    for (token_index = 0U; token_index < parsed_source->token_count; ++token_index) {
        const RToken *token = &parsed_source->tokens[token_index];

        if (r_bytes_type_test_token_is_text(parsed_source, token, "bytes")) {
            bytes_token_count += 1U;
            R_BYTES_TYPE_CHECK(token->kind == R_TOKEN_IDENTIFIER);
        }
    }
    R_BYTES_TYPE_CHECK(bytes_token_count >= 16U);
    R_BYTES_TYPE_CHECK(r_frontend_parse_cst(context, source_id, &cst) == R_FRONTEND_OK);
    R_BYTES_TYPE_CHECK(cst != R_SYNTAX_NODE_ID_INVALID);
    R_BYTES_TYPE_CHECK(r_frontend_lower_ast(context, source_id, &ast) == R_FRONTEND_OK);
    R_BYTES_TYPE_CHECK(ast != R_AST_NODE_ID_INVALID);
    R_BYTES_TYPE_CHECK(r_frontend_diagnostic_count(context) == 0U);
    R_BYTES_TYPE_CHECK(r_frontend_dump_cst(context, source_id, r_bytes_type_test_write, &dump) ==
                       R_FRONTEND_OK);
    R_BYTES_TYPE_CHECK((dump.bytes != NULL) &&
                       (strstr(dump.bytes, "ambiguous_decl_or_expr") == NULL));
    R_BYTES_TYPE_CHECK((dump.bytes != NULL) &&
                       (strstr(dump.bytes, "expression_statement") != NULL));
    R_BYTES_TYPE_CHECK((dump.bytes != NULL) && (strstr(dump.bytes, "object_declaration") != NULL));
    r_bytes_type_test_buffer_destroy(&dump);
    r_frontend_destroy(context);
}

static bool r_bytes_type_test_analyze(const char *case_name, const char *source) {
    RFrontendContext *context = r_frontend_create(NULL);
    RFrontendStatus status;
    bool success;

    R_BYTES_TYPE_CHECK(context != NULL);
    if (context == NULL) {
        return false;
    }
    (void)r_bytes_type_test_add_source(context, case_name, source);
    status = r_frontend_analyze(context);
    success = (status == R_FRONTEND_OK) && (r_frontend_diagnostic_count(context) == 0U);
    if (!success) {
        r_bytes_type_test_print_diagnostics(context, case_name);
    }
    R_BYTES_TYPE_CHECK(success);
    r_frontend_destroy(context);
    return success;
}

static void r_bytes_type_test_namespaces(void) {
    static const struct RBytesTypePositiveCase {
        const char *name;
        const char *source;
    } positive_cases[] = {
        {"bytes-module.r", "module bytes; i32 value() { return 0; }\n"},
        {"bytes-function.r",
         "module names.bytes_function;\n"
         "i32 bytes() { return 1; }\n"
         "i32 use() { i32 value = bytes(); return value; }\n"},
        {"bytes-value.r", "module names.bytes_value; const i32 bytes = 1;\n"},
        {"bytes-parameter.r",
         "module names.bytes_parameter; i32 pass(i32 bytes) { return bytes; }\n"},
        {"bytes-local.r", "module names.bytes_local; i32 run() { i32 bytes = 1; return bytes; }\n"},
        {"bytes-variant.r", "module names.bytes_variant; enum marker { bytes, other, };\n"},
        {"bytes-member.r",
         "module names.bytes_member;\n"
         "struct digest { i32 bytes; };\n"
         "i32 read(digest value) { return value.bytes; }\n"},
        {"bytes-type-and-value.r",
         "module names.bytes_type_and_value;\n"
         "array<u8> take() { bytes bytes = {}; return move bytes; }\n"},
    };
    static const char *const negative_sources[] = {
        "module names.struct_bytes; struct bytes { i32 value; };\n",
        "module names.enum_bytes; enum bytes { value, };\n",
    };
    size_t index;

    for (index = 0U; index < (sizeof(positive_cases) / sizeof(positive_cases[0])); ++index) {
        (void)r_bytes_type_test_analyze(positive_cases[index].name, positive_cases[index].source);
    }
    for (index = 0U; index < (sizeof(negative_sources) / sizeof(negative_sources[0])); ++index) {
        RFrontendContext *context = r_frontend_create(NULL);

        R_BYTES_TYPE_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        (void)r_bytes_type_test_add_source(
            context, "bytes-aggregate-negative.r", negative_sources[index]);
        R_BYTES_TYPE_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
        R_BYTES_TYPE_CHECK(
            r_bytes_type_test_has_diagnostic(context, "R-DIAG-NAME-003", "R-NAME-0009"));
        r_frontend_destroy(context);
    }
}

static bool r_bytes_type_test_span_name_equal(const RFrontendContext *context,
                                              RSourceSpan span,
                                              const char *name) {
    const RSource *source;
    const size_t length = strlen(name);

    if ((span.source == R_SOURCE_ID_INVALID) || ((size_t)span.source > context->source_count)) {
        return false;
    }
    source = &context->sources[(size_t)span.source - 1U];
    return (span.end >= span.start) && ((size_t)(span.end - span.start) == length) &&
           ((size_t)span.end <= source->length) &&
           (memcmp(source->bytes + span.start, name, length) == 0);
}

static const RSemanticSymbol *r_bytes_type_test_find_function(const RFrontendContext *context,
                                                              const char *name) {
    size_t index;

    for (index = 0U; index < context->semantic_symbol_count; ++index) {
        const RSemanticSymbol *symbol = &context->semantic_symbols[index];

        if ((symbol->kind == R_SEMANTIC_SYMBOL_FUNCTION) &&
            r_bytes_type_test_span_name_equal(context, symbol->name_span, name)) {
            return symbol;
        }
    }
    return NULL;
}

static const RSemanticField *r_bytes_type_test_find_field(const RFrontendContext *context,
                                                          const char *name) {
    size_t index;

    for (index = 0U; index < context->semantic_field_count; ++index) {
        const RSemanticField *field = &context->semantic_fields[index];

        if (r_bytes_type_test_span_name_equal(context, field->name_span, name)) {
            return field;
        }
    }
    return NULL;
}

static RTypeId r_bytes_type_test_parameter_type(const RFrontendContext *context,
                                                const RSemanticSymbol *function,
                                                uint32_t index) {
    const size_t parameter_index = (size_t)function->first_parameter_type + (size_t)index;

    R_BYTES_TYPE_CHECK(index < function->parameter_count);
    R_BYTES_TYPE_CHECK(parameter_index < context->semantic_parameter_type_count);
    if ((index >= function->parameter_count) ||
        (parameter_index >= context->semantic_parameter_type_count)) {
        return R_TYPE_ID_INVALID;
    }
    return context->semantic_parameter_types[parameter_index];
}

static void r_bytes_type_test_canonical_types(void) {
    static const char source[] = "module semantic.bytes_types;\n"
                                 "bytes alias_return();\n"
                                 "array<u8> explicit_return();\n"
                                 "void alias_plain(bytes value);\n"
                                 "void explicit_plain(array<u8> value);\n"
                                 "void alias_const(const bytes value);\n"
                                 "void explicit_const(const array<u8> value);\n"
                                 "void alias_borrow(bytes* value);\n"
                                 "void explicit_borrow(array<u8>* value);\n"
                                 "void alias_shared_borrow(const bytes* value);\n"
                                 "void explicit_shared_borrow(const array<u8>* value);\n"
                                 "void alias_arc(arc bytes value);\n"
                                 "void explicit_arc(arc (array<u8>) value);\n"
                                 "void alias_rc(rc bytes value);\n"
                                 "void explicit_rc(rc (array<u8>) value);\n"
                                 "void alias_weak(weak arc bytes value);\n"
                                 "void explicit_weak(weak arc (array<u8>) value);\n"
                                 "void alias_own(own bytes* value);\n"
                                 "void explicit_own(own array<u8>* value);\n"
                                 "void alias_raw(raw const bytes*? value);\n"
                                 "void explicit_raw(raw const array<u8>*? value);\n"
                                 "void alias_slice(bytes[] value);\n"
                                 "void explicit_slice((array<u8>)[] value);\n"
                                 "void alias_shared_slice(const bytes[] value);\n"
                                 "void explicit_shared_slice(const (array<u8>)[] value);\n"
                                 "void alias_option(o<bytes> value);\n"
                                 "void explicit_option(o<array<u8>> value);\n"
                                 "void alias_task(task<bytes> value);\n"
                                 "void explicit_task(task<array<u8>> value);\n"
                                 "struct nested_types {\n"
                                 "    bytes[4] alias_fixed;\n"
                                 "    (array<u8>)[4] explicit_fixed;\n"
                                 "    array<bytes> alias_nested;\n"
                                 "    array<array<u8>> explicit_nested;\n"
                                 "};\n";
    static const struct RBytesTypePair {
        const char *alias;
        const char *explicit_type;
    } parameter_pairs[] = {
        {"alias_plain", "explicit_plain"},
        {"alias_const", "explicit_const"},
        {"alias_borrow", "explicit_borrow"},
        {"alias_shared_borrow", "explicit_shared_borrow"},
        {"alias_arc", "explicit_arc"},
        {"alias_rc", "explicit_rc"},
        {"alias_weak", "explicit_weak"},
        {"alias_own", "explicit_own"},
        {"alias_raw", "explicit_raw"},
        {"alias_slice", "explicit_slice"},
        {"alias_shared_slice", "explicit_shared_slice"},
        {"alias_option", "explicit_option"},
        {"alias_task", "explicit_task"},
    };
    static const struct RBytesTypePair field_pairs[] = {
        {"alias_fixed", "explicit_fixed"},
        {"alias_nested", "explicit_nested"},
    };
    RFrontendContext *context = r_frontend_create(NULL);
    const RSemanticSymbol *alias_return;
    const RSemanticSymbol *explicit_return;
    const RSemanticType *bytes_type;
    size_t pair_index;

    R_BYTES_TYPE_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    (void)r_bytes_type_test_add_source(context, "bytes-types.r", source);
    R_BYTES_TYPE_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    if (r_frontend_diagnostic_count(context) != 0U) {
        r_bytes_type_test_print_diagnostics(context, "bytes canonical types");
        for (pair_index = 0U; pair_index < context->semantic_symbol_count; ++pair_index) {
            const RSemanticSymbol *symbol = &context->semantic_symbols[pair_index];

            if ((symbol->kind == R_SEMANTIC_SYMBOL_FUNCTION) && !symbol->signature_supported) {
                const RSource *symbol_source =
                    &context->sources[(size_t)symbol->name_span.source - 1U];
                (void)fprintf(stderr,
                              "  unsupported function: %.*s\n",
                              (int)(symbol->name_span.end - symbol->name_span.start),
                              symbol_source->bytes + symbol->name_span.start);
            }
        }
    }
    R_BYTES_TYPE_CHECK(r_frontend_diagnostic_count(context) == 0U);
    alias_return = r_bytes_type_test_find_function(context, "alias_return");
    explicit_return = r_bytes_type_test_find_function(context, "explicit_return");
    R_BYTES_TYPE_CHECK(alias_return != NULL);
    R_BYTES_TYPE_CHECK(explicit_return != NULL);
    if ((alias_return != NULL) && (explicit_return != NULL)) {
        R_BYTES_TYPE_CHECK(alias_return->return_type == explicit_return->return_type);
        bytes_type = r_semantic_type(context, alias_return->return_type);
        R_BYTES_TYPE_CHECK((bytes_type != NULL) && (bytes_type->kind == R_SEMANTIC_TYPE_ARRAY));
        R_BYTES_TYPE_CHECK(
            (bytes_type != NULL) && (r_semantic_type(context, bytes_type->base) != NULL) &&
            (r_semantic_type(context, bytes_type->base)->kind == R_SEMANTIC_TYPE_U8));
        R_BYTES_TYPE_CHECK(r_semantic_type_is_send(context, alias_return->return_type) ==
                           r_semantic_type_is_send(context, explicit_return->return_type));
        R_BYTES_TYPE_CHECK(r_semantic_type_is_sync(context, alias_return->return_type) ==
                           r_semantic_type_is_sync(context, explicit_return->return_type));
    }
    for (pair_index = 0U; pair_index < (sizeof(parameter_pairs) / sizeof(parameter_pairs[0]));
         ++pair_index) {
        const RSemanticSymbol *alias =
            r_bytes_type_test_find_function(context, parameter_pairs[pair_index].alias);
        const RSemanticSymbol *explicit_type =
            r_bytes_type_test_find_function(context, parameter_pairs[pair_index].explicit_type);

        R_BYTES_TYPE_CHECK(alias != NULL);
        R_BYTES_TYPE_CHECK(explicit_type != NULL);
        if ((alias != NULL) && (explicit_type != NULL)) {
            const RTypeId alias_type =
                r_bytes_type_test_parameter_type(context, alias, UINT32_C(0));
            const RTypeId canonical_type =
                r_bytes_type_test_parameter_type(context, explicit_type, UINT32_C(0));

            if (alias_type != canonical_type) {
                (void)fprintf(stderr,
                              "  canonical mismatch: %s=%" PRIu32 " %s=%" PRIu32 "\n",
                              parameter_pairs[pair_index].alias,
                              alias_type,
                              parameter_pairs[pair_index].explicit_type,
                              canonical_type);
            }
            R_BYTES_TYPE_CHECK(alias_type == canonical_type);
            R_BYTES_TYPE_CHECK(r_semantic_type_is_send(context, alias_type) ==
                               r_semantic_type_is_send(context, canonical_type));
            R_BYTES_TYPE_CHECK(r_semantic_type_is_sync(context, alias_type) ==
                               r_semantic_type_is_sync(context, canonical_type));
        }
    }
    for (pair_index = 0U; pair_index < (sizeof(field_pairs) / sizeof(field_pairs[0]));
         ++pair_index) {
        const RSemanticField *alias =
            r_bytes_type_test_find_field(context, field_pairs[pair_index].alias);
        const RSemanticField *explicit_type =
            r_bytes_type_test_find_field(context, field_pairs[pair_index].explicit_type);

        R_BYTES_TYPE_CHECK(alias != NULL);
        R_BYTES_TYPE_CHECK(explicit_type != NULL);
        if ((alias != NULL) && (explicit_type != NULL)) {
            R_BYTES_TYPE_CHECK(alias->type == explicit_type->type);
            R_BYTES_TYPE_CHECK(r_semantic_type_is_send(context, alias->type) ==
                               r_semantic_type_is_send(context, explicit_type->type));
            R_BYTES_TYPE_CHECK(r_semantic_type_is_sync(context, alias->type) ==
                               r_semantic_type_is_sync(context, explicit_type->type));
        }
    }
    r_frontend_destroy(context);
}

static void r_bytes_type_test_empty_initializer(void) {
    static const char positive_source[] =
        "module semantic.bytes_empty;\n"
        "bytes alias_empty() { bytes value = {}; return move value; }\n"
        "array<u8> explicit_empty() { array<u8> value = {}; return move value; }\n"
        "array<u8> alias_to_array(bytes value) { return move value; }\n"
        "bytes array_to_alias(array<u8> value) { return move value; }\n"
        "array<u8> same_spelling() { bytes bytes = {}; return move bytes; }\n"
        "void alias_view() {\n"
        "    bytes value = {};\n"
        "    const u8[] view = std.array::as_slice(&value);\n"
        "    view as void;\n"
        "}\n"
        "void explicit_view() {\n"
        "    array<u8> value = {};\n"
        "    const u8[] view = std.array::as_slice(&value);\n"
        "    view as void;\n"
        "}\n";
    static const char *const negative_sources[] = {
        "module semantic.bytes_nonempty;\n"
        "bytes invalid() { bytes value = {1}; return move value; }\n",
        "module semantic.array_u8_nonempty;\n"
        "array<u8> invalid() { array<u8> value = {1}; return move value; }\n",
    };
    RFrontendContext *context = r_frontend_create(NULL);
    size_t hir_index;
    size_t empty_count = 0U;
    size_t index;

    R_BYTES_TYPE_CHECK(context != NULL);
    if (context != NULL) {
        (void)r_bytes_type_test_add_source(context, "bytes-empty.r", positive_source);
        R_BYTES_TYPE_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
        if (r_frontend_diagnostic_count(context) != 0U) {
            r_bytes_type_test_print_diagnostics(context, "bytes empty initializer");
        }
        R_BYTES_TYPE_CHECK(r_frontend_diagnostic_count(context) == 0U);
        for (hir_index = 0U; hir_index < context->hir_node_count; ++hir_index) {
            const RHirNode *node = &context->hir_nodes[hir_index];
            const RSemanticType *type = r_semantic_type(context, node->type);
            const RSemanticType *element =
                type != NULL ? r_semantic_type(context, type->base) : NULL;

            if ((node->kind == R_HIR_DEFAULT_VALUE) && (type != NULL) &&
                (type->kind == R_SEMANTIC_TYPE_ARRAY) && (element != NULL) &&
                (element->kind == R_SEMANTIC_TYPE_U8)) {
                empty_count += 1U;
            }
        }
        R_BYTES_TYPE_CHECK(empty_count == 5U);
        r_frontend_destroy(context);
    }
    for (index = 0U; index < (sizeof(negative_sources) / sizeof(negative_sources[0])); ++index) {
        context = r_frontend_create(NULL);
        R_BYTES_TYPE_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        (void)r_bytes_type_test_add_source(context, "bytes-nonempty.r", negative_sources[index]);
        R_BYTES_TYPE_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
        R_BYTES_TYPE_CHECK(
            r_bytes_type_test_has_diagnostic(context, "R-DIAG-TYPE-001", "R-TYPE-0030"));
        r_frontend_destroy(context);
    }
}

static void r_bytes_type_test_standard_call_diagnostics(void) {
    static const struct RBytesDiagnosticCase {
        const char *name;
        const char *source;
        const char *rule_id;
    } cases[] = {
        {
            "bytes-append-arity.r",
            "module semantic.bytes_append_arity;\n"
            "void test() throws std.alloc::alloc_error {\n"
            "    std.bytes::append_u8();\n"
            "}\n",
            "R-SLIB-BYTES-0003",
        },
        {
            "utf8-is-valid-arity.r",
            "module semantic.utf8_is_valid_arity;\n"
            "void test() {\n"
            "    bool result = std.utf8::is_valid();\n"
            "}\n",
            "R-SLIB-UTF8-0001",
        },
        {
            "bytes-md5-arity.r",
            "module semantic.bytes_md5_arity;\n"
            "void test() {\n"
            "    std.hash::md5_digest result = std.hash::md5();\n"
            "}\n",
            "R-SLIB-BYTES-0005",
        },
        {
            "bytes-hash-arity.r",
            "module semantic.bytes_hash_arity;\n"
            "void test() {\n"
            "    u32 result = std.hash::crc32();\n"
            "}\n",
            "R-SLIB-BYTES-0006",
        },
    };
    size_t index;

    for (index = 0U; index < (sizeof(cases) / sizeof(cases[0])); ++index) {
        RFrontendContext *context = r_frontend_create(NULL);

        R_BYTES_TYPE_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        (void)r_bytes_type_test_add_source(context, cases[index].name, cases[index].source);
        R_BYTES_TYPE_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
        R_BYTES_TYPE_CHECK(
            r_bytes_type_test_has_diagnostic(context, "R-DIAG-TYPE-001", cases[index].rule_id));
        r_frontend_destroy(context);
    }
}

static void r_bytes_type_test_contextual_byte_views(void) {
    static const char positive_source[] =
        "module semantic.contextual_byte_views;\n"
        "bool views(const u8[] shared, u8[] mutable, bytes owned, array<u8> array_value, "
        "u8[2] fixed, str text) throws core::utf8_error {\n"
        "    constexpr str literal = \"ok\";\n"
        "    bool a = std.utf8::is_valid(shared);\n"
        "    bool b = std.utf8::is_valid(mutable);\n"
        "    bool c = std.utf8::is_valid(owned);\n"
        "    bool d = std.utf8::is_valid(array_value);\n"
        "    bool e = std.utf8::is_valid(fixed);\n"
        "    bool f = std.utf8::is_valid(text);\n"
        "    bool g = std.utf8::is_valid(literal);\n"
        "    str checked_shared = std.utf8::validate(shared);\n"
        "    str checked_mutable = std.utf8::validate(mutable);\n"
        "    str checked_owned = std.utf8::validate(owned);\n"
        "    str checked_array = std.utf8::validate(array_value);\n"
        "    str checked_fixed = std.utf8::validate(fixed);\n"
        "    str checked_text = std.utf8::validate(text);\n"
        "    str checked_literal = std.utf8::validate(literal);\n"
        "    checked_shared as void;\n"
        "    checked_mutable as void;\n"
        "    checked_owned as void;\n"
        "    checked_array as void;\n"
        "    checked_fixed as void;\n"
        "    checked_text as void;\n"
        "    checked_literal as void;\n"
        "    b as void; c as void; d as void; e as void; f as void; g as void;\n"
        "    return a && b && c && d && e && f && g;\n"
        "}\n"
        "protected bytes shadow_after_borrow(bytes source) {\n"
        "    const u8[] borrowed = std.array::as_slice(&source);\n"
        "    borrowed as void;\n"
        "    {\n"
        "        i32 borrowed = 0;\n"
        "        borrowed as void;\n"
        "    }\n"
        "    return move source;\n"
        "}\n";
    static const struct RContextualByteViewNegativeCase {
        const char *name;
        const char *source;
        const char *diagnostic;
        const char *rule_id;
    } negative_cases[] = {
        {
            "byte-view-initializer.r",
            "module semantic.byte_view_initializer;\n"
            "const u8[] test(u8[2] source, const u8[] fallback) {\n"
            "    const u8[] stored = source;\n"
            "    return fallback;\n"
            "}\n",
            "R-DIAG-TYPE-001",
            "R-INIT-0002",
        },
        {
            "byte-view-assignment.r",
            "module semantic.byte_view_assignment;\n"
            "const u8[] test(u8[2] source, const u8[] fallback) {\n"
            "    const u8[] stored = fallback;\n"
            "    stored = source;\n"
            "    return stored;\n"
            "}\n",
            "R-DIAG-TYPE-001",
            "R-INIT-0007",
        },
        {
            "byte-view-return.r",
            "module semantic.byte_view_return;\n"
            "const u8[] test(u8[2] source) {\n"
            "    return source;\n"
            "}\n",
            "R-DIAG-TYPE-001",
            "R-STMT-0005",
        },
        {
            "byte-view-owner-initializer.r",
            "module semantic.byte_view_owner_initializer;\n"
            "const u8[] test(bytes source, const u8[] fallback) {\n"
            "    const u8[] stored = source;\n"
            "    return fallback;\n"
            "}\n",
            "R-DIAG-MOVE-001",
            "R-OWN-0003",
        },
        {
            "byte-view-constexpr-initializer.r",
            "module semantic.byte_view_constexpr_initializer;\n"
            "const u8[] test(const u8[] fallback) {\n"
            "    constexpr str source = \"ok\";\n"
            "    const u8[] stored = source;\n"
            "    return fallback;\n"
            "}\n",
            "R-DIAG-TYPE-001",
            "R-INIT-0002",
        },
        {
            "byte-view-wrong-element.r",
            "module semantic.byte_view_wrong_element;\n"
            "bool test(u16[2] source) {\n"
            "    bool valid = std.utf8::is_valid(source);\n"
            "    return valid;\n"
            "}\n",
            "R-DIAG-TYPE-001",
            "R-SLIB-UTF8-0001",
        },
        {
            "utf8-validate-unhandled.r",
            "module semantic.utf8_validate_unhandled;\n"
            "void test(const u8[] source) {\n"
            "    str text = std.utf8::validate(source);\n"
            "    text as void;\n"
            "}\n",
            "R-DIAG-EFFECT-001",
            "R-ERR-0001",
        },
        {
            "utf8-validate-owner-freeze.r",
            "module semantic.utf8_validate_owner_freeze;\n"
            "void test(bytes source) throws core::utf8_error, std.alloc::alloc_error {\n"
            "    str text = std.utf8::validate(source);\n"
            "    std.bytes::append_u8(&source, 0);\n"
            "    text as void;\n"
            "}\n",
            "R-DIAG-BORROW-001",
            "R-BORROW-0002",
        },
        {
            "utf8-validate-array-freeze.r",
            "module semantic.utf8_validate_array_freeze;\n"
            "void test(array<u8> source) throws core::utf8_error, std.alloc::alloc_error {\n"
            "    str text = std.utf8::validate(source);\n"
            "    std.bytes::append_u8(&source, 0);\n"
            "    text as void;\n"
            "}\n",
            "R-DIAG-BORROW-001",
            "R-BORROW-0002",
        },
        {
            "utf8-validate-mutable-slice-freeze.r",
            "module semantic.utf8_validate_mutable_slice_freeze;\n"
            "void test(u8[] source) throws core::utf8_error {\n"
            "    str text = std.utf8::validate(source);\n"
            "    source[0] = 0;\n"
            "    text as void;\n"
            "}\n",
            "R-DIAG-BORROW-001",
            "R-BORROW-0002",
        },
        {
            "utf8-validate-fixed-freeze.r",
            "module semantic.utf8_validate_fixed_freeze;\n"
            "void test(u8[2] source) throws core::utf8_error {\n"
            "    str text = std.utf8::validate(source);\n"
            "    source[0] = 0;\n"
            "    text as void;\n"
            "}\n",
            "R-DIAG-BORROW-001",
            "R-BORROW-0002",
        },
    };
    RFrontendContext *context = r_frontend_create(NULL);
    size_t utf8_call_count = 0U;
    size_t validate_call_count = 0U;
    size_t validate_origin_count = 0U;
    size_t index;

    R_BYTES_TYPE_CHECK(context != NULL);
    if (context != NULL) {
        (void)r_bytes_type_test_add_source(context, "contextual-byte-views.r", positive_source);
        R_BYTES_TYPE_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
        for (index = 0U; index < context->hir_node_count; ++index) {
            const RHirNode *node = &context->hir_nodes[index];

            if ((node->kind == R_HIR_STANDARD_CALL) &&
                (node->standard_operation == R_STANDARD_CALL_UTF8_IS_VALID)) {
                utf8_call_count += 1U;
            }
            if ((node->kind == R_HIR_STANDARD_CALL) &&
                (node->standard_operation == R_STANDARD_CALL_UTF8_VALIDATE)) {
                const RSemanticType *value_type = r_semantic_type(context, node->type);
                const RSemanticType *carrier_type = r_semantic_type(context, node->auxiliary_type);

                R_BYTES_TYPE_CHECK((value_type != NULL) &&
                                   (value_type->kind == R_SEMANTIC_TYPE_STR));
                R_BYTES_TYPE_CHECK(
                    (carrier_type != NULL) &&
                    (carrier_type->kind == R_SEMANTIC_TYPE_EFFECT_CARRIER) &&
                    (carrier_type->base == node->type) &&
                    (r_semantic_effect_count(context, carrier_type->second) == UINT32_C(1)));
                validate_call_count += 1U;
                if (node->borrow_origin != R_SYMBOL_ID_INVALID) {
                    validate_origin_count += 1U;
                }
            }
        }
        R_BYTES_TYPE_CHECK(utf8_call_count == 7U);
        R_BYTES_TYPE_CHECK(validate_call_count == 7U);
        R_BYTES_TYPE_CHECK(validate_origin_count == 6U);
        if (r_frontend_diagnostic_count(context) != 0U) {
            r_bytes_type_test_print_diagnostics(context, "contextual byte views");
        }
        r_frontend_destroy(context);
    }
    for (index = 0U; index < (sizeof(negative_cases) / sizeof(negative_cases[0])); ++index) {
        context = r_frontend_create(NULL);
        R_BYTES_TYPE_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        (void)r_bytes_type_test_add_source(
            context, negative_cases[index].name, negative_cases[index].source);
        R_BYTES_TYPE_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
        if (!r_bytes_type_test_has_diagnostic(
                context, negative_cases[index].diagnostic, negative_cases[index].rule_id)) {
            r_bytes_type_test_print_diagnostics(context, negative_cases[index].name);
            R_BYTES_TYPE_CHECK(false);
        }
        r_frontend_destroy(context);
    }
}

static RFrontendContext *r_bytes_type_test_make_interface(const char *source) {
    RFrontendContext *context = r_frontend_create(NULL);

    if (context == NULL) {
        return NULL;
    }
    (void)r_bytes_type_test_add_source(context, "bytes-interface.r", source);
    if ((r_frontend_analyze(context) != R_FRONTEND_OK) ||
        (r_frontend_lower_mir(context) != R_FRONTEND_OK)) {
        r_bytes_type_test_print_diagnostics(context, "bytes interface");
        r_frontend_destroy(context);
        return NULL;
    }
    return context;
}

static void r_bytes_type_test_interface_fingerprint(void) {
    static const char alias_source[] = "module interface.bytes_identity;\n"
                                       "struct digest { bytes bytes; };\n"
                                       "bytes transfer(bytes value);\n";
    static const char explicit_source[] = "module interface.bytes_identity;\n"
                                          "struct digest { array<u8> bytes; };\n"
                                          "array<u8> transfer(array<u8> value);\n";
    RFrontendContext *alias = r_bytes_type_test_make_interface(alias_source);
    RFrontendContext *explicit_type = r_bytes_type_test_make_interface(explicit_source);
    RFrontendArtifactOptions options;
    RBytesTypeTestBuffer alias_interface = {0};
    RBytesTypeTestBuffer explicit_interface = {0};
    uint8_t alias_fingerprint[32];
    uint8_t explicit_fingerprint[32];

    R_BYTES_TYPE_CHECK(alias != NULL);
    R_BYTES_TYPE_CHECK(explicit_type != NULL);
    if ((alias == NULL) || (explicit_type == NULL)) {
        r_frontend_destroy(alias);
        r_frontend_destroy(explicit_type);
        return;
    }
    (void)memset(&options, 0, sizeof(options));
    options.profile = "hosted";
    R_BYTES_TYPE_CHECK(
        r_frontend_dump_interface(alias, &options, r_bytes_type_test_write, &alias_interface) ==
        R_FRONTEND_OK);
    R_BYTES_TYPE_CHECK(r_frontend_dump_interface(
                           explicit_type, &options, r_bytes_type_test_write, &explicit_interface) ==
                       R_FRONTEND_OK);
    R_BYTES_TYPE_CHECK(alias_interface.length == explicit_interface.length);
    R_BYTES_TYPE_CHECK(
        (alias_interface.length == explicit_interface.length) &&
        (memcmp(alias_interface.bytes, explicit_interface.bytes, alias_interface.length) == 0));
    R_BYTES_TYPE_CHECK((alias_interface.bytes != NULL) &&
                       (strstr(alias_interface.bytes, "type=(array u8)") != NULL));
    r_sha256_digest(
        (const uint8_t *)alias_interface.bytes, alias_interface.length, alias_fingerprint);
    r_sha256_digest(
        (const uint8_t *)explicit_interface.bytes, explicit_interface.length, explicit_fingerprint);
    R_BYTES_TYPE_CHECK(memcmp(alias_fingerprint, explicit_fingerprint, sizeof(alias_fingerprint)) ==
                       0);
    r_bytes_type_test_buffer_destroy(&alias_interface);
    r_bytes_type_test_buffer_destroy(&explicit_interface);
    r_frontend_destroy(alias);
    r_frontend_destroy(explicit_type);
}

int main(void) {
    r_bytes_type_test_parser();
    r_bytes_type_test_namespaces();
    r_bytes_type_test_canonical_types();
    r_bytes_type_test_empty_initializer();
    r_bytes_type_test_standard_call_diagnostics();
    r_bytes_type_test_contextual_byte_views();
    r_bytes_type_test_interface_fingerprint();
    if (failures != 0) {
        (void)fprintf(stderr, "%d bytes type checks failed\n", failures);
        return 1;
    }
    (void)printf("r_frontend_bytes_type_tests: ok\n");
    return 0;
}

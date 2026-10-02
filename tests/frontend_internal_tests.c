#include "frontend_internal.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct RInternalTestAllocator {
    size_t allocation_count;
    size_t live_count;
} RInternalTestAllocator;

static int failures = 0;

#define R_INTERNAL_CHECK(condition)                                                                \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            (void)fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);    \
            failures += 1;                                                                         \
        }                                                                                          \
    } while (false)

static void *r_internal_test_allocate(void *user_data, size_t size) {
    RInternalTestAllocator *allocator = user_data;
    void *pointer = malloc(size);

    allocator->allocation_count += 1U;
    if (pointer != NULL) {
        allocator->live_count += 1U;
    }
    return pointer;
}

static void r_internal_test_free(void *user_data, void *pointer) {
    RInternalTestAllocator *allocator = user_data;

    if (pointer != NULL) {
        R_INTERNAL_CHECK(allocator->live_count != 0U);
        if (allocator->live_count != 0U) {
            allocator->live_count -= 1U;
        }
        free(pointer);
    }
}

static bool r_internal_find_token_text(const RFrontendContext *context,
                                       RAstRef ref,
                                       const char *expected,
                                       RAstRef *result) {
    RAstNodeView node;
    RAstTextView text;
    size_t expected_length = strlen(expected);
    uint32_t child_index;

    if (!r_ast_ref_view(context, ref, &node)) {
        return false;
    }
    if (r_ast_ref_text(context, ref, &text) && (text.length == expected_length) &&
        (memcmp(text.bytes, expected, expected_length) == 0)) {
        *result = ref;
        return true;
    }
    for (child_index = 0U; child_index < node.child_count; ++child_index) {
        RAstRef child;
        if (r_ast_ref_child(context, ref, child_index, &child) &&
            r_internal_find_token_text(context, child, expected, result)) {
            return true;
        }
    }
    return false;
}

static void r_internal_test_diagnostic_phases_and_ast_view(void) {
    static const char source_text[] = "module internal.view;\n"
                                      "protected i32 identity(i32 value) { return value; }\n";
    RInternalTestAllocator allocator = {0};
    RFrontendOptions options = r_frontend_default_options();
    RFrontendContext *context;
    RSourceId source_id = R_SOURCE_ID_INVALID;
    RSyntaxNodeId cst_root = R_SYNTAX_NODE_ID_INVALID;
    RAstNodeId ast_root = R_AST_NODE_ID_INVALID;
    RSourceSpan diagnostic_span;
    RSource *source;
    RAstRef root;
    RAstRef identifier;
    RAstRef invalid_ref = {R_SOURCE_ID_INVALID, R_AST_NODE_ID_INVALID};
    RAstNodeView root_view;
    RAstNodeView invalid_view;
    RAstTokenView identifier_token;
    RAstTextView identifier_text;
    size_t allocation_count;
    const RDiagnostic *diagnostic;

    options.allocate = r_internal_test_allocate;
    options.free = r_internal_test_free;
    options.allocator_user_data = &allocator;
    context = r_frontend_create(&options);
    R_INTERNAL_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_INTERNAL_CHECK(r_frontend_add_source(context,
                                           "internal-view.r",
                                           (const uint8_t *)source_text,
                                           strlen(source_text),
                                           &source_id) == R_FRONTEND_OK);
    R_INTERNAL_CHECK(r_frontend_parse_cst(context, source_id, &cst_root) == R_FRONTEND_OK);
    diagnostic_span.source = source_id;
    diagnostic_span.start = UINT32_C(0);
    diagnostic_span.end = UINT32_C(6);
    R_INTERNAL_CHECK(r_add_diagnostic_phase(context,
                                            R_DIAGNOSTIC_PHASE_SEMANTIC,
                                            "R-DIAG-TYPE-001",
                                            "R-TYPE-0015",
                                            "semantic test diagnostic",
                                            R_DIAGNOSTIC_ERROR,
                                            diagnostic_span));
    R_INTERNAL_CHECK(r_add_diagnostic_phase(context,
                                            R_DIAGNOSTIC_PHASE_OWNERSHIP,
                                            "R-DIAG-BORROW-001",
                                            "R-BORROW-0003",
                                            "ownership test diagnostic",
                                            R_DIAGNOSTIC_ERROR,
                                            diagnostic_span));
    R_INTERNAL_CHECK(r_add_diagnostic(context,
                                      "R-DIAG-NAME-001",
                                      "R-NAME-0003",
                                      "classified semantic diagnostic",
                                      R_DIAGNOSTIC_ERROR,
                                      diagnostic_span));
    R_INTERNAL_CHECK(r_add_diagnostic(context,
                                      "R-DIAG-MOVE-001",
                                      "R-OWN-0003",
                                      "classified ownership diagnostic",
                                      R_DIAGNOSTIC_ERROR,
                                      diagnostic_span));
    R_INTERNAL_CHECK(r_add_diagnostic_phase(context,
                                            R_DIAGNOSTIC_PHASE_SYNTAX,
                                            "R-DIAG-SYN-001",
                                            "R-GRAM-0001",
                                            "warning does not poison syntax",
                                            R_DIAGNOSTIC_WARNING,
                                            diagnostic_span));

    source = r_get_source(context, source_id);
    R_INTERNAL_CHECK(source != NULL);
    if (source != NULL) {
        R_INTERNAL_CHECK(!source->has_lex_error);
        R_INTERNAL_CHECK(!source->has_syntax_error);
        R_INTERNAL_CHECK(source->has_semantic_error);
        R_INTERNAL_CHECK(source->has_ownership_error);
    }
    R_INTERNAL_CHECK(r_frontend_diagnostic_count(context) == 5U);
    diagnostic = r_frontend_diagnostic(context, 0U);
    R_INTERNAL_CHECK((diagnostic != NULL) && (diagnostic->phase == R_DIAGNOSTIC_PHASE_SEMANTIC));
    diagnostic = r_frontend_diagnostic(context, 1U);
    R_INTERNAL_CHECK((diagnostic != NULL) && (diagnostic->phase == R_DIAGNOSTIC_PHASE_OWNERSHIP));
    diagnostic = r_frontend_diagnostic(context, 2U);
    R_INTERNAL_CHECK((diagnostic != NULL) && (diagnostic->phase == R_DIAGNOSTIC_PHASE_SEMANTIC));
    diagnostic = r_frontend_diagnostic(context, 3U);
    R_INTERNAL_CHECK((diagnostic != NULL) && (diagnostic->phase == R_DIAGNOSTIC_PHASE_OWNERSHIP));

    R_INTERNAL_CHECK(r_frontend_lower_ast(context, source_id, &ast_root) == R_FRONTEND_OK);
    allocation_count = allocator.allocation_count;
    R_INTERNAL_CHECK(r_ast_root_ref(context, source_id, &root));
    R_INTERNAL_CHECK((root.source == source_id) && (root.node == ast_root));
    R_INTERNAL_CHECK(r_ast_ref_view(context, root, &root_view));
    R_INTERNAL_CHECK(root_view.kind == R_SYNTAX_TRANSLATION_UNIT);
    R_INTERNAL_CHECK(root_view.child_count != 0U);
    R_INTERNAL_CHECK(!r_ast_ref_token(context, root, &identifier_token));
    R_INTERNAL_CHECK(!r_ast_ref_text(context, root, &identifier_text));
    R_INTERNAL_CHECK(!r_ast_ref_child(context, root, root_view.child_count, &identifier));
    R_INTERNAL_CHECK(r_internal_find_token_text(context, root, "value", &identifier));
    R_INTERNAL_CHECK(r_ast_ref_token(context, identifier, &identifier_token));
    R_INTERNAL_CHECK(identifier_token.kind == R_TOKEN_IDENTIFIER);
    R_INTERNAL_CHECK(identifier_token.intern_id != UINT32_C(0));
    R_INTERNAL_CHECK(r_ast_ref_text(context, identifier, &identifier_text));
    R_INTERNAL_CHECK((identifier_text.length == 5U) &&
                     (memcmp(identifier_text.bytes, "value", 5U) == 0));
    R_INTERNAL_CHECK(!r_ast_ref_view(context, invalid_ref, &invalid_view));
    R_INTERNAL_CHECK(invalid_view.kind == R_SYNTAX_INVALID);
    R_INTERNAL_CHECK(allocator.allocation_count == allocation_count);
    R_INTERNAL_CHECK(r_add_diagnostic(context,
                                      "R-DIAG-LEX-001",
                                      "R-LEX-0001",
                                      "classified lexical diagnostic",
                                      R_DIAGNOSTIC_ERROR,
                                      diagnostic_span));
    R_INTERNAL_CHECK(r_add_diagnostic(context,
                                      "R-DIAG-SYN-001",
                                      "R-GRAM-0001",
                                      "classified syntax diagnostic",
                                      R_DIAGNOSTIC_ERROR,
                                      diagnostic_span));
    diagnostic = r_frontend_diagnostic(context, 5U);
    R_INTERNAL_CHECK((diagnostic != NULL) && (diagnostic->phase == R_DIAGNOSTIC_PHASE_LEXICAL));
    diagnostic = r_frontend_diagnostic(context, 6U);
    R_INTERNAL_CHECK((diagnostic != NULL) && (diagnostic->phase == R_DIAGNOSTIC_PHASE_SYNTAX));
    if (source != NULL) {
        R_INTERNAL_CHECK(source->has_lex_error);
        R_INTERNAL_CHECK(source->has_syntax_error);
    }

    r_frontend_destroy(context);
    R_INTERNAL_CHECK(allocator.live_count == 0U);
}

static void r_internal_test_generic_header_span(void) {
    static const char text[] = "module test.header;\n"
                               "@ /* gap */ generic <T: copy> struct Box { T value; };";
    const char *header = strchr(text, '@');
    const char *end = strchr(header, '>') + 1;
    RFrontendContext *context = r_frontend_create(NULL);
    RSourceId source_id;
    RAstNodeId ast;
    size_t ast_headers = 0U, cst_headers = 0U;
    R_INTERNAL_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_INTERNAL_CHECK(r_frontend_add_source(
                         context, "header.r", (const uint8_t *)text, strlen(text), &source_id) ==
                     R_FRONTEND_OK);
    R_INTERNAL_CHECK(r_frontend_lower_ast(context, source_id, &ast) == R_FRONTEND_OK);
    const RSource *source = r_get_source(context, source_id);
    R_INTERNAL_CHECK(source != NULL);
    if (source != NULL) {
        for (size_t index = 0U; index < source->ast_node_count; ++index) {
            const RAstNode *node = &source->ast_nodes[index];
            if (node->kind == R_SYNTAX_GENERIC_HEADER) {
                ++ast_headers;
                R_INTERNAL_CHECK(node->span.start == (uint32_t)(header - text));
                R_INTERNAL_CHECK(node->span.end == (uint32_t)(end - text));
            }
        }
        for (size_t index = 0U; index < source->cst_event_count; ++index) {
            const RCstEvent *event = &source->cst_events[index];
            if (event->kind == R_CST_EVENT_OPEN && event->syntax_kind == R_SYNTAX_GENERIC_HEADER) {
                ++cst_headers;
                R_INTERNAL_CHECK(event->span.start == (uint32_t)(header - text));
                R_INTERNAL_CHECK(event->span.end == (uint32_t)(end - text));
            }
        }
    }
    R_INTERNAL_CHECK(ast_headers == 1U && cst_headers == 1U);
    r_frontend_destroy(context);
}

int main(void) {
    r_internal_test_diagnostic_phases_and_ast_view();
    r_internal_test_generic_header_span();
    if (failures != 0) {
        (void)fprintf(stderr, "%d internal frontend checks failed\n", failures);
        return 1;
    }
    (void)printf("r_frontend_internal_tests: ok\n");
    return 0;
}

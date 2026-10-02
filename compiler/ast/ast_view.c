#include "frontend_internal.h"

static const RAstNode *
r_ast_resolve_ref(const RFrontendContext *context, RAstRef ref, const RSource **source_result) {
    const RSource *source = r_get_source_const(context, ref.source);

    if (source_result != NULL) {
        *source_result = NULL;
    }
    if ((source == NULL) || !source->lowered || (ref.node == R_AST_NODE_ID_INVALID) ||
        ((size_t)ref.node > source->ast_node_count)) {
        return NULL;
    }
    if (source_result != NULL) {
        *source_result = source;
    }
    return &source->ast_nodes[(size_t)ref.node - 1U];
}

bool r_ast_root_ref(const RFrontendContext *context, RSourceId source_id, RAstRef *root) {
    const RSource *source;

    if (root == NULL) {
        return false;
    }
    root->source = R_SOURCE_ID_INVALID;
    root->node = R_AST_NODE_ID_INVALID;
    source = r_get_source_const(context, source_id);
    if ((source == NULL) || !source->lowered || (source->ast_root == R_AST_NODE_ID_INVALID) ||
        ((size_t)source->ast_root > source->ast_node_count)) {
        return false;
    }
    root->source = source_id;
    root->node = source->ast_root;
    return true;
}

bool r_ast_ref_view(const RFrontendContext *context, RAstRef ref, RAstNodeView *view) {
    const RAstNode *node;

    if (view == NULL) {
        return false;
    }
    (void)memset(view, 0, sizeof(*view));
    node = r_ast_resolve_ref(context, ref, NULL);
    if (node == NULL) {
        return false;
    }
    view->kind = node->kind;
    view->span = node->span;
    view->syntax = node->syntax;
    view->token = node->token;
    view->child_count = node->child_count;
    view->call_free = node->call_free;
    view->implicit_chain = node->implicit_chain;
    return true;
}

bool r_semantic_root_ref(const RFrontendContext *context, RSourceId source_id, RAstRef *root) {
    const RSource *source = r_get_source_const(context, source_id);
    if (!r_ast_root_ref(context, source_id, root)) {
        return false;
    }
    if (source->semantic_root != R_AST_NODE_ID_INVALID) {
        root->node = source->semantic_root;
    }
    return true;
}

bool r_ast_ref_child(const RFrontendContext *context,
                     RAstRef parent,
                     uint32_t child_index,
                     RAstRef *child) {
    const RSource *source;
    const RAstNode *node;
    size_t index;
    RAstNodeId child_id;

    if (child == NULL) {
        return false;
    }
    child->source = R_SOURCE_ID_INVALID;
    child->node = R_AST_NODE_ID_INVALID;
    node = r_ast_resolve_ref(context, parent, &source);
    if ((node == NULL) || (child_index >= node->child_count)) {
        return false;
    }
    index = (size_t)node->first_child + (size_t)child_index;
    if (index >= source->ast_child_count) {
        return false;
    }
    child_id = source->ast_children[index];
    if ((child_id == R_AST_NODE_ID_INVALID) || ((size_t)child_id > source->ast_node_count)) {
        return false;
    }
    child->source = parent.source;
    child->node = child_id;
    return true;
}

bool r_ast_ref_token(const RFrontendContext *context, RAstRef ref, RAstTokenView *token) {
    const RSource *source;
    const RAstNode *node;
    const RToken *source_token;
    size_t token_index;

    if (token == NULL) {
        return false;
    }
    (void)memset(token, 0, sizeof(*token));
    node = r_ast_resolve_ref(context, ref, &source);
    if (node != NULL && node->kind == R_SYNTAX_TOKEN && node->generated_name != 0U) {
        token->kind = node->generated_token_kind != R_TOKEN_INVALID ? node->generated_token_kind
                                                                    : R_TOKEN_IDENTIFIER;
        token->span = node->span;
        token->intern_id = node->generated_name;
        return true;
    }
    if ((node == NULL) || (node->kind != R_SYNTAX_TOKEN) || (node->token == R_TOKEN_ID_INVALID)) {
        return false;
    }
    token_index = (size_t)node->token - 1U;
    if (token_index >= source->token_count) {
        return false;
    }
    source_token = &source->tokens[token_index];
    token->token = node->token;
    token->kind = source_token->kind;
    token->span = source_token->span;
    token->intern_id = source_token->intern_id;
    return true;
}

bool r_ast_ref_text(const RFrontendContext *context, RAstRef ref, RAstTextView *text) {
    const RSource *source;
    RAstTokenView token;

    if (text == NULL) {
        return false;
    }
    text->bytes = NULL;
    text->length = 0U;
    if (!r_ast_ref_token(context, ref, &token)) {
        return false;
    }
    if (token.token == R_TOKEN_ID_INVALID && token.intern_id != 0U) {
        const RInternEntry *entry = &context->intern_entries[token.intern_id - 1U];
        text->bytes = (const uint8_t *)entry->bytes;
        text->length = entry->length;
        return true;
    }
    source = r_get_source_const(context, token.span.source);
    if ((source == NULL) || (token.span.end < token.span.start) ||
        ((size_t)token.span.end > source->length)) {
        return false;
    }
    text->bytes = source->bytes + token.span.start;
    text->length = (size_t)(token.span.end - token.span.start);
    return true;
}

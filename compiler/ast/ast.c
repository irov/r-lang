#include "frontend_internal.h"

typedef struct RAstChildVector {
    RAstNodeId *items;
    size_t count;
    size_t capacity;
} RAstChildVector;

/* Angle brackets delimit type and generic argument lists (R-TYPE-0031) like parentheses do
   elsewhere; inside those nodes they are punctuation, in expressions they are operators. */
static bool r_ast_parent_takes_angle_lists(RSyntaxKind parent) {
    return (parent == R_SYNTAX_TYPE) || (parent == R_SYNTAX_GENERIC_HEADER) ||
           (parent == R_SYNTAX_GENERIC_ARGUMENT_LIST) || (parent == R_SYNTAX_STANDARD_TYPE_CALL) ||
           (parent == R_SYNTAX_TRAIT_NAME);
}

static bool r_ast_token_is_retained(RTokenKind kind, RSyntaxKind parent) {
    if (r_token_is_trivia(kind) || (kind == R_TOKEN_EOF)) {
        return false;
    }
    if (((kind == R_TOKEN_LESS) || (kind == R_TOKEN_GREATER) ||
         (kind == R_TOKEN_GREATER_GREATER)) &&
        r_ast_parent_takes_angle_lists(parent)) {
        return false;
    }
    switch (kind) {
    case R_TOKEN_LBRACE:
    case R_TOKEN_RBRACE:
    case R_TOKEN_LBRACKET:
    case R_TOKEN_RBRACKET:
    case R_TOKEN_LPAREN:
    case R_TOKEN_RPAREN:
    case R_TOKEN_SEMICOLON:
    case R_TOKEN_COMMA:
    case R_TOKEN_DOT:
    case R_TOKEN_COLON:
    case R_TOKEN_COLON_COLON:
    case R_TOKEN_FORMAT_START:
    case R_TOKEN_FORMAT_END:
        return false;
    default:
        return true;
    }
}

static bool
r_ast_append_node(RFrontendContext *context, RSource *source, RAstNode node, RAstNodeId *node_id) {
    if (source->ast_node_count >= UINT32_MAX) {
        context->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
        return false;
    }
    if (!r_grow_array(context,
                      (void **)&source->ast_nodes,
                      &source->ast_node_capacity,
                      sizeof(*source->ast_nodes),
                      source->ast_node_count + 1U)) {
        return false;
    }
    source->ast_nodes[source->ast_node_count] = node;
    source->ast_node_count += 1U;
    *node_id = (RAstNodeId)source->ast_node_count;
    return true;
}

static bool
r_ast_child_vector_push(RFrontendContext *context, RAstChildVector *children, RAstNodeId child) {
    if (!r_grow_array(context,
                      (void **)&children->items,
                      &children->capacity,
                      sizeof(*children->items),
                      children->count + 1U)) {
        return false;
    }
    children->items[children->count] = child;
    children->count += 1U;
    return true;
}

static bool r_ast_commit_children(RFrontendContext *context,
                                  RSource *source,
                                  RAstNodeId parent,
                                  const RAstChildVector *children) {
    RAstNode *node;

    if ((parent == R_AST_NODE_ID_INVALID) || ((size_t)parent > source->ast_node_count) ||
        (source->ast_child_count > UINT32_MAX) || (children->count > UINT32_MAX) ||
        (children->count > ((size_t)UINT32_MAX - source->ast_child_count))) {
        context->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
        return false;
    }
    if (!r_grow_array(context,
                      (void **)&source->ast_children,
                      &source->ast_child_capacity,
                      sizeof(*source->ast_children),
                      source->ast_child_count + children->count)) {
        return false;
    }
    node = &source->ast_nodes[(size_t)parent - 1U];
    node->first_child = (uint32_t)source->ast_child_count;
    node->child_count = (uint32_t)children->count;
    if (children->count != 0U) {
        (void)memcpy(source->ast_children + source->ast_child_count,
                     children->items,
                     children->count * sizeof(*children->items));
        source->ast_child_count += children->count;
    }
    return true;
}

static bool r_lower_cst_node(RFrontendContext *context,
                             RSource *source,
                             size_t open_index,
                             RAstNodeId *result) {
    const RCstEvent *open_event;
    RAstNode node;
    RAstNodeId node_id;
    RAstChildVector children = {0};
    size_t event_index;
    bool success = false;

    if (open_index >= source->cst_event_count) {
        return false;
    }
    open_event = &source->cst_events[open_index];
    if ((open_event->kind != R_CST_EVENT_OPEN) ||
        ((size_t)open_event->matching_event >= source->cst_event_count)) {
        return false;
    }
    (void)memset(&node, 0, sizeof(node));
    node.kind = open_event->syntax_kind;
    node.span = open_event->span;
    node.syntax = (RSyntaxNodeId)(open_index + 1U);
    node.call_free = open_event->call_free;
    node.implicit_chain = open_event->implicit_chain;
    if (!r_ast_append_node(context, source, node, &node_id)) {
        return false;
    }

    event_index = open_index + 1U;
    while (event_index < (size_t)open_event->matching_event) {
        const RCstEvent *event = &source->cst_events[event_index];
        if (event->kind == R_CST_EVENT_OPEN) {
            RAstNodeId child;
            if (!r_lower_cst_node(context, source, event_index, &child) ||
                !r_ast_child_vector_push(context, &children, child)) {
                goto cleanup;
            }
            event_index = (size_t)event->matching_event + 1U;
        } else if (event->kind == R_CST_EVENT_TOKEN) {
            size_t token_index = (size_t)event->token - 1U;
            if ((event->token == R_TOKEN_ID_INVALID) || (token_index >= source->token_count)) {
                goto cleanup;
            }
            if (r_ast_token_is_retained(source->tokens[token_index].kind, node.kind)) {
                RAstNode token_node;
                RAstNodeId token_id;
                (void)memset(&token_node, 0, sizeof(token_node));
                token_node.kind = R_SYNTAX_TOKEN;
                token_node.span = source->tokens[token_index].span;
                token_node.token = event->token;
                if (!r_ast_append_node(context, source, token_node, &token_id) ||
                    !r_ast_child_vector_push(context, &children, token_id)) {
                    goto cleanup;
                }
            }
            event_index += 1U;
        } else if (event->kind == R_CST_EVENT_MISSING) {
            goto cleanup;
        } else {
            event_index += 1U;
        }
    }
    if (!r_ast_commit_children(context, source, node_id, &children)) {
        goto cleanup;
    }
    *result = node_id;
    success = true;

cleanup:
    r_context_free(context, children.items);
    return success;
}

RFrontendStatus r_lower_source_ast(RFrontendContext *context, RSourceId source_id) {
    RSource *source = r_get_source(context, source_id);
    size_t root_index;

    if (source == NULL) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    if (source->lowered) {
        return R_FRONTEND_OK;
    }
    if (!source->parsed || source->has_lex_error || source->has_syntax_error ||
        source->has_unresolved_ambiguity || (source->cst_root == R_SYNTAX_NODE_ID_INVALID)) {
        return R_FRONTEND_NOT_LOWERABLE;
    }
    root_index = (size_t)source->cst_root - 1U;
    source->ast_node_count = 0U;
    source->ast_child_count = 0U;
    if (!r_lower_cst_node(context, source, root_index, &source->ast_root)) {
        return (context->resource_status == R_FRONTEND_OK) ? R_FRONTEND_INTERNAL_ERROR
                                                           : context->resource_status;
    }
    source->lowered = true;
    return R_FRONTEND_OK;
}

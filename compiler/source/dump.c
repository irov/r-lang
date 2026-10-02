#include "frontend_internal.h"

#include <inttypes.h>
#include <stdio.h>

bool r_write_bytes(RFrontendWriteFn writer, void *user_data, const char *bytes, size_t length) {
    return (writer != NULL) && writer(user_data, bytes, length);
}

bool r_write_text(RFrontendWriteFn writer, void *user_data, const char *text) {
    return (text != NULL) && r_write_bytes(writer, user_data, text, strlen(text));
}

bool r_write_uint32(RFrontendWriteFn writer, void *user_data, uint32_t value) {
    char buffer[16];
    int length = snprintf(buffer, sizeof(buffer), "%" PRIu32, value);
    return (length > 0) && ((size_t)length < sizeof(buffer)) &&
           r_write_bytes(writer, user_data, buffer, (size_t)length);
}

bool r_write_escaped(RFrontendWriteFn writer,
                     void *user_data,
                     const uint8_t *bytes,
                     size_t length) {
    static const char hexadecimal[] = "0123456789ABCDEF";
    size_t index;
    if (!r_write_text(writer, user_data, "\"")) {
        return false;
    }
    for (index = 0U; index < length; ++index) {
        uint8_t byte = bytes[index];
        if ((byte == (uint8_t)'\\') || (byte == (uint8_t)'\"')) {
            char escaped[2] = {'\\', (char)byte};
            if (!r_write_bytes(writer, user_data, escaped, sizeof(escaped))) {
                return false;
            }
        } else if (byte == (uint8_t)'\n') {
            if (!r_write_text(writer, user_data, "\\n")) {
                return false;
            }
        } else if (byte == (uint8_t)'\r') {
            if (!r_write_text(writer, user_data, "\\r")) {
                return false;
            }
        } else if (byte == (uint8_t)'\t') {
            if (!r_write_text(writer, user_data, "\\t")) {
                return false;
            }
        } else if ((byte >= UINT8_C(0x20)) && (byte <= UINT8_C(0x7E))) {
            char character = (char)byte;
            if (!r_write_bytes(writer, user_data, &character, 1U)) {
                return false;
            }
        } else {
            char escaped[4] = {
                '\\', 'x', hexadecimal[byte >> 4U], hexadecimal[byte & UINT8_C(0x0F)]};
            if (!r_write_bytes(writer, user_data, escaped, sizeof(escaped))) {
                return false;
            }
        }
    }
    return r_write_text(writer, user_data, "\"");
}

static bool r_write_indent(RFrontendWriteFn writer, void *user_data, uint32_t depth) {
    uint32_t index;
    for (index = 0U; index < depth; ++index) {
        if (!r_write_text(writer, user_data, "  ")) {
            return false;
        }
    }
    return true;
}

RFrontendStatus r_frontend_dump_tokens(const RFrontendContext *context,
                                       RSourceId source_id,
                                       RFrontendWriteFn writer,
                                       void *user_data) {
    const RSource *source = r_get_source_const(context, source_id);
    size_t index;
    if ((source == NULL) || (writer == NULL) || !source->lexed) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    for (index = 0U; index < source->token_count; ++index) {
        const RToken *token = &source->tokens[index];
        if (!r_write_text(writer, user_data, "(token ") ||
            !r_write_text(writer, user_data, r_token_kind_name(token->kind)) ||
            !r_write_text(writer, user_data, " ") ||
            !r_write_uint32(writer, user_data, token->span.start) ||
            !r_write_text(writer, user_data, " ") ||
            !r_write_uint32(writer, user_data, token->span.end) ||
            !r_write_text(writer, user_data, " ") ||
            !r_write_escaped(writer,
                             user_data,
                             source->bytes + token->span.start,
                             (size_t)(token->span.end - token->span.start)) ||
            !r_write_text(writer, user_data, ")\n")) {
            return R_FRONTEND_IO_ERROR;
        }
    }
    return R_FRONTEND_OK;
}

/* R-AGG-0012 (L27): the declarations derived after the authored text are not dumped. */
static bool r_dump_generated(const RSource *source, RSourceSpan span) {
    return source->derive_expanded && (source->authored_length < source->length) &&
           (span.start >= source->authored_length);
}

static bool r_dump_cst_node(const RSource *source,
                            size_t open_index,
                            uint32_t depth,
                            RFrontendWriteFn writer,
                            void *user_data) {
    const RCstEvent *open_event = &source->cst_events[open_index];
    size_t index = open_index + 1U;
    if (!r_write_indent(writer, user_data, depth) || !r_write_text(writer, user_data, "(") ||
        !r_write_text(writer, user_data, r_syntax_kind_name(open_event->syntax_kind)) ||
        !r_write_text(writer, user_data, "\n")) {
        return false;
    }
    while (index < (size_t)open_event->matching_event) {
        const RCstEvent *event = &source->cst_events[index];
        if ((event->kind == R_CST_EVENT_OPEN) && (depth == 0U) &&
            r_dump_generated(source, event->span)) {
            index = (size_t)event->matching_event + 1U;
        } else if (event->kind == R_CST_EVENT_OPEN) {
            if (!r_dump_cst_node(source, index, depth + 1U, writer, user_data)) {
                return false;
            }
            index = (size_t)event->matching_event + 1U;
        } else if (event->kind == R_CST_EVENT_TOKEN) {
            size_t token_index = (size_t)event->token - 1U;
            const RToken *token;
            if ((event->token == R_TOKEN_ID_INVALID) || (token_index >= source->token_count)) {
                return false;
            }
            token = &source->tokens[token_index];
            if (!r_write_indent(writer, user_data, depth + 1U) ||
                !r_write_text(writer, user_data, "(token ") ||
                !r_write_text(writer, user_data, r_token_kind_name(token->kind)) ||
                !r_write_text(writer, user_data, " ") ||
                !r_write_escaped(writer,
                                 user_data,
                                 source->bytes + token->span.start,
                                 (size_t)(token->span.end - token->span.start)) ||
                !r_write_text(writer, user_data, ")\n")) {
                return false;
            }
            index += 1U;
        } else if (event->kind == R_CST_EVENT_MISSING) {
            if (!r_write_indent(writer, user_data, depth + 1U) ||
                !r_write_text(writer, user_data, "(missing ") ||
                !r_write_text(writer, user_data, r_token_kind_name(event->missing_kind)) ||
                !r_write_text(writer, user_data, ")\n")) {
                return false;
            }
            index += 1U;
        } else {
            index += 1U;
        }
    }
    return r_write_indent(writer, user_data, depth) && r_write_text(writer, user_data, ")\n");
}

RFrontendStatus r_frontend_dump_cst(const RFrontendContext *context,
                                    RSourceId source_id,
                                    RFrontendWriteFn writer,
                                    void *user_data) {
    const RSource *source = r_get_source_const(context, source_id);
    size_t root;
    if ((source == NULL) || (writer == NULL) || !source->parsed ||
        (source->cst_root == R_SYNTAX_NODE_ID_INVALID)) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    root = (size_t)source->cst_root - 1U;
    return r_dump_cst_node(source, root, UINT32_C(0), writer, user_data) ? R_FRONTEND_OK
                                                                         : R_FRONTEND_IO_ERROR;
}

static bool r_dump_ast_node(const RSource *source,
                            RAstNodeId node_id,
                            uint32_t depth,
                            RFrontendWriteFn writer,
                            void *user_data) {
    const RAstNode *node;
    uint32_t index;
    if ((node_id == R_AST_NODE_ID_INVALID) || ((size_t)node_id > source->ast_node_count)) {
        return false;
    }
    node = &source->ast_nodes[(size_t)node_id - 1U];
    if (!r_write_indent(writer, user_data, depth) || !r_write_text(writer, user_data, "(") ||
        !r_write_text(writer, user_data, r_syntax_kind_name(node->kind))) {
        return false;
    }
    if (node->kind == R_SYNTAX_TOKEN) {
        size_t token_index = (size_t)node->token - 1U;
        const RToken *token;
        if ((node->token == R_TOKEN_ID_INVALID) || (token_index >= source->token_count)) {
            return false;
        }
        token = &source->tokens[token_index];
        return r_write_text(writer, user_data, " ") &&
               r_write_text(writer, user_data, r_token_kind_name(token->kind)) &&
               r_write_text(writer, user_data, " ") &&
               r_write_escaped(writer,
                               user_data,
                               source->bytes + token->span.start,
                               (size_t)(token->span.end - token->span.start)) &&
               r_write_text(writer, user_data, ")\n");
    }
    if (node->child_count == UINT32_C(0)) {
        return r_write_text(writer, user_data, ")\n");
    }
    if (!r_write_text(writer, user_data, "\n")) {
        return false;
    }
    for (index = 0U; index < node->child_count; ++index) {
        size_t child_index = (size_t)node->first_child + (size_t)index;
        if ((child_index < source->ast_child_count) && (depth == 0U) &&
            (source->ast_children[child_index] != R_AST_NODE_ID_INVALID) &&
            ((size_t)source->ast_children[child_index] <= source->ast_node_count) &&
            r_dump_generated(
                source,
                source->ast_nodes[(size_t)source->ast_children[child_index] - 1U].span)) {
            continue;
        }
        if ((child_index >= source->ast_child_count) ||
            !r_dump_ast_node(
                source, source->ast_children[child_index], depth + 1U, writer, user_data)) {
            return false;
        }
    }
    return r_write_indent(writer, user_data, depth) && r_write_text(writer, user_data, ")\n");
}

RFrontendStatus r_frontend_dump_ast(const RFrontendContext *context,
                                    RSourceId source_id,
                                    RFrontendWriteFn writer,
                                    void *user_data) {
    const RSource *source = r_get_source_const(context, source_id);
    if ((source == NULL) || (writer == NULL) || !source->lowered ||
        (source->ast_root == R_AST_NODE_ID_INVALID)) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    return r_dump_ast_node(source, source->ast_root, UINT32_C(0), writer, user_data)
               ? R_FRONTEND_OK
               : R_FRONTEND_IO_ERROR;
}

RFrontendStatus r_frontend_reconstruct_source(const RFrontendContext *context,
                                              RSourceId source_id,
                                              RFrontendWriteFn writer,
                                              void *user_data) {
    const RSource *source = r_get_source_const(context, source_id);
    size_t event_index;
    uint32_t expected_start = UINT32_C(0);
    if ((source == NULL) || (writer == NULL) || !source->parsed) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    for (event_index = 0U; event_index < source->cst_event_count; ++event_index) {
        const RCstEvent *event = &source->cst_events[event_index];
        if (event->kind == R_CST_EVENT_TOKEN) {
            size_t token_index;
            const RToken *token;
            if (event->token == R_TOKEN_ID_INVALID) {
                return R_FRONTEND_INTERNAL_ERROR;
            }
            token_index = (size_t)event->token - 1U;
            if (token_index >= source->token_count) {
                return R_FRONTEND_INTERNAL_ERROR;
            }
            token = &source->tokens[token_index];
            if ((token->span.start != expected_start) || (token->span.end < token->span.start)) {
                return R_FRONTEND_INTERNAL_ERROR;
            }
            if ((token->span.end > token->span.start) &&
                !r_write_bytes(writer,
                               user_data,
                               (const char *)(source->bytes + token->span.start),
                               (size_t)(token->span.end - token->span.start))) {
                return R_FRONTEND_IO_ERROR;
            }
            expected_start = token->span.end;
        }
    }
    return ((size_t)expected_start == source->length) ? R_FRONTEND_OK : R_FRONTEND_INTERNAL_ERROR;
}

static bool r_write_json_string(RFrontendWriteFn writer, void *user_data, const char *text) {
    static const char hexadecimal[] = "0123456789ABCDEF";
    size_t index;
    size_t length;
    if ((text == NULL) || !r_write_text(writer, user_data, "\"")) {
        return false;
    }
    length = strlen(text);
    for (index = 0U; index < length; ++index) {
        uint8_t byte = (uint8_t)text[index];
        if ((byte == (uint8_t)'\\') || (byte == (uint8_t)'\"')) {
            char escaped[2] = {'\\', (char)byte};
            if (!r_write_bytes(writer, user_data, escaped, sizeof(escaped))) {
                return false;
            }
        } else if (byte == (uint8_t)'\n') {
            if (!r_write_text(writer, user_data, "\\n")) {
                return false;
            }
        } else if (byte == (uint8_t)'\r') {
            if (!r_write_text(writer, user_data, "\\r")) {
                return false;
            }
        } else if (byte == (uint8_t)'\t') {
            if (!r_write_text(writer, user_data, "\\t")) {
                return false;
            }
        } else if (byte == (uint8_t)'\b') {
            if (!r_write_text(writer, user_data, "\\b")) {
                return false;
            }
        } else if (byte == (uint8_t)'\f') {
            if (!r_write_text(writer, user_data, "\\f")) {
                return false;
            }
        } else if (byte < UINT8_C(0x20)) {
            char escaped[6] = {
                '\\', 'u', '0', '0', hexadecimal[byte >> 4U], hexadecimal[byte & UINT8_C(0x0F)]};
            if (!r_write_bytes(writer, user_data, escaped, sizeof(escaped))) {
                return false;
            }
        } else {
            char character = (char)byte;
            if (!r_write_bytes(writer, user_data, &character, 1U)) {
                return false;
            }
        }
    }
    return r_write_text(writer, user_data, "\"");
}

static const char *r_diagnostic_phase_name(RDiagnosticPhase phase) {
    switch (phase) {
    case R_DIAGNOSTIC_PHASE_LEXICAL:
        return "lexical";
    case R_DIAGNOSTIC_PHASE_SYNTAX:
        return "syntax";
    case R_DIAGNOSTIC_PHASE_SEMANTIC:
        return "semantic";
    case R_DIAGNOSTIC_PHASE_OWNERSHIP:
        return "ownership";
    }
    return "syntax";
}

RFrontendStatus r_frontend_dump_diagnostics_json(const RFrontendContext *context,
                                                 RFrontendWriteFn writer,
                                                 void *user_data) {
    size_t index;
    if ((context == NULL) || (writer == NULL)) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    if (!r_write_text(writer, user_data, "[")) {
        return R_FRONTEND_IO_ERROR;
    }
    for (index = 0U; index < context->diagnostic_count; ++index) {
        const RDiagnostic *diagnostic = &context->diagnostics[index];
        const char *source_name = r_frontend_source_name(context, diagnostic->primary_span.source);
        uint32_t line;
        uint32_t column;
        r_frontend_source_position(context, diagnostic->primary_span, &line, &column);
        if ((index != 0U) && !r_write_text(writer, user_data, ",")) {
            return R_FRONTEND_IO_ERROR;
        }
        if (!r_write_text(writer, user_data, "{\"source\":") ||
            !r_write_json_string(writer, user_data, (source_name == NULL) ? "" : source_name) ||
            !r_write_text(writer, user_data, ",\"start\":") ||
            !r_write_uint32(writer, user_data, diagnostic->primary_span.start) ||
            !r_write_text(writer, user_data, ",\"end\":") ||
            !r_write_uint32(writer, user_data, diagnostic->primary_span.end) ||
            !r_write_text(writer, user_data, ",\"line\":") ||
            !r_write_uint32(writer, user_data, line) ||
            !r_write_text(writer, user_data, ",\"column\":") ||
            !r_write_uint32(writer, user_data, column) ||
            !r_write_text(writer, user_data, ",\"severity\":") ||
            !r_write_json_string(writer,
                                 user_data,
                                 diagnostic->severity == R_DIAGNOSTIC_ERROR ? "error"
                                                                            : "warning") ||
            !r_write_text(writer, user_data, ",\"phase\":") ||
            !r_write_json_string(writer, user_data, r_diagnostic_phase_name(diagnostic->phase)) ||
            !r_write_text(writer, user_data, ",\"code\":") ||
            !r_write_json_string(writer, user_data, diagnostic->code) ||
            !r_write_text(writer, user_data, ",\"rule\":") ||
            !r_write_json_string(writer, user_data, diagnostic->rule_id) ||
            !r_write_text(writer, user_data, ",\"message\":") ||
            !r_write_json_string(writer, user_data, diagnostic->message) ||
            !r_write_text(writer, user_data, "}")) {
            return R_FRONTEND_IO_ERROR;
        }
    }
    return r_write_text(writer, user_data, "]\n") ? R_FRONTEND_OK : R_FRONTEND_IO_ERROR;
}

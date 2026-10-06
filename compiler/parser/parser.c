#include "frontend_internal.h"

#include <stdio.h>

typedef struct RTextBuilder {
    char *items;
    size_t count;
    size_t capacity;
} RTextBuilder;

typedef enum RExpressionMode {
    R_EXPRESSION_NORMAL = 0,
    R_EXPRESSION_CALL_FREE,
    R_EXPRESSION_CONSTANT
} RExpressionMode;

typedef struct RExpressionInfo {
    bool parsed;
    bool is_condition;
    /* R-EXPR-0029: the expression is one membership test, a condition leaf on its own. */
    bool is_membership;
} RExpressionInfo;

typedef struct RParser {
    RFrontendContext *context;
    RSource *source;
    RSourceId source_id;
    size_t cursor;
    uint32_t depth;
    uint32_t nesting_depth;
    uint32_t last_consumed_end;
    bool suppress_diagnostics;
    bool in_async_function;
    size_t generic_header_start;
    size_t generic_header_end;
    /* R-TYPE-0031: a `>>` token closes two nested type argument lists; the second close is
       pending until the enclosing list consumes it. */
    uint32_t pending_greater;
    /* A constant generic argument ends at `>`: relational `>`, `>>` and `>=` stop it unless
       they appear inside parentheses or brackets. */
    bool no_greater;
} RParser;

static size_t r_skip_trivia(const RSource *source, size_t index) {
    while ((index < source->token_count) && r_token_is_trivia(source->tokens[index].kind)) {
        index += 1U;
    }
    return index;
}

static const RToken *r_scan_token(const RSource *source, size_t index) {
    if (source->tokens == NULL) {
        return NULL;
    }
    index = r_skip_trivia(source, index);
    if (index >= source->token_count) {
        return NULL;
    }
    return &source->tokens[index];
}

static RTokenKind r_scan_kind(const RSource *source, size_t index) {
    const RToken *token = r_scan_token(source, index);
    return (token == NULL) ? R_TOKEN_EOF : token->kind;
}

/* Skips the leading attribute sequence of a module declaration (R-OBJ-0012). */
static size_t r_scan_skip_leading_attributes(const RSource *source, size_t index) {
    while (r_scan_kind(source, index) == R_TOKEN_AT) {
        index = r_skip_trivia(source, index) + 1U;
        if ((r_scan_kind(source, index) != R_TOKEN_IDENTIFIER) &&
            (r_scan_kind(source, index) != R_TOKEN_KW_DEFAULT)) {
            return index;
        }
        index = r_skip_trivia(source, index) + 1U;
        if (r_scan_kind(source, index) == R_TOKEN_LESS) {
            /* `@generic<...>`: parameters, constraints and nested type arguments. */
            size_t depth = 0U;

            do {
                const RTokenKind kind = r_scan_kind(source, index);

                if (kind == R_TOKEN_EOF) {
                    return index;
                }
                if (kind == R_TOKEN_LESS) {
                    depth += 1U;
                } else if (kind == R_TOKEN_GREATER) {
                    depth -= 1U;
                } else if (kind == R_TOKEN_GREATER_GREATER) {
                    depth = depth >= 2U ? depth - 2U : 0U;
                }
                index = r_skip_trivia(source, index) + 1U;
            } while (depth != 0U);
        } else if (r_scan_kind(source, index) == R_TOKEN_LPAREN) {
            size_t depth = 0U;

            do {
                const RTokenKind kind = r_scan_kind(source, index);

                if (kind == R_TOKEN_EOF) {
                    return index;
                }
                if (kind == R_TOKEN_LPAREN) {
                    depth += 1U;
                } else if (kind == R_TOKEN_RPAREN) {
                    depth -= 1U;
                }
                index = r_skip_trivia(source, index) + 1U;
            } while (depth != 0U);
        }
    }
    return index;
}

/* Contextual only: an ordinary type, variable or path named error stays an identifier. */
static bool r_scan_error_declaration(const RSource *source, size_t index) {
    const RToken *head = r_scan_token(source, index);
    size_t name;
    size_t tail;
    if ((head == NULL) || (head->kind != R_TOKEN_IDENTIFIER) ||
        !r_source_text_equal(source, head->span, "error")) {
        return false;
    }
    name = r_skip_trivia(source, index) + 1U;
    if (r_scan_kind(source, name) != R_TOKEN_IDENTIFIER) {
        return false;
    }
    tail = r_skip_trivia(source, name) + 1U;
    return (r_scan_kind(source, tail) == R_TOKEN_LBRACE) ||
           (r_scan_kind(source, tail) == R_TOKEN_COLON);
}

static bool r_text_builder_append(RFrontendContext *context,
                                  RTextBuilder *builder,
                                  const char *bytes,
                                  size_t length) {
    if (builder->count > (SIZE_MAX - length - 1U)) {
        context->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
        return false;
    }
    if (!r_grow_array(context,
                      (void **)&builder->items,
                      &builder->capacity,
                      sizeof(*builder->items),
                      builder->count + length + 1U)) {
        return false;
    }
    if (length != 0U) {
        (void)memcpy(builder->items + builder->count, bytes, length);
    }
    builder->count += length;
    builder->items[builder->count] = '\0';
    return true;
}

static bool r_text_builder_append_token(RFrontendContext *context,
                                        RTextBuilder *builder,
                                        const RSource *source,
                                        const RToken *token) {
    size_t length;
    if ((token == NULL) || (token->span.end < token->span.start)) {
        return false;
    }
    length = (size_t)(token->span.end - token->span.start);
    return r_text_builder_append(
        context, builder, (const char *)(source->bytes + token->span.start), length);
}

static bool r_scan_module_path(RFrontendContext *context,
                               const RSource *source,
                               size_t *index,
                               RTextBuilder *builder,
                               RSourceSpan *span) {
    const RToken *token;
    bool first = true;

    while (true) {
        *index = r_skip_trivia(source, *index);
        token = r_scan_token(source, *index);
        if ((token == NULL) || (token->kind != R_TOKEN_IDENTIFIER)) {
            return !first;
        }
        if (!first && !r_text_builder_append(context, builder, ".", 1U)) {
            return false;
        }
        if (!r_text_builder_append_token(context, builder, source, token)) {
            return false;
        }
        if (span != NULL) {
            if (first) {
                *span = token->span;
            }
            span->end = token->span.end;
        }
        first = false;
        *index = r_skip_trivia(source, *index) + 1U;
        *index = r_skip_trivia(source, *index);
        if (r_scan_kind(source, *index) != R_TOKEN_DOT) {
            break;
        }
        *index = r_skip_trivia(source, *index) + 1U;
    }
    return true;
}

static bool r_add_import_entry(RFrontendContext *context,
                               RSourceId source_id,
                               const char *module_name,
                               RSourceSpan module_span,
                               const RSource *source,
                               const RToken *name) {
    RImportEntry *entry;

    if (!r_grow_array(context,
                      (void **)&context->imports,
                      &context->import_capacity,
                      sizeof(*context->imports),
                      context->import_count + 1U)) {
        return false;
    }
    entry = &context->imports[context->import_count];
    (void)memset(entry, 0, sizeof(*entry));
    entry->source = source_id;
    entry->module_span = module_span;
    entry->module_name = r_copy_string(context, module_name, strlen(module_name));
    if (entry->module_name == NULL) {
        return false;
    }
    if (name != NULL) {
        size_t length = (size_t)(name->span.end - name->span.start);
        entry->name =
            r_copy_string(context, (const char *)(source->bytes + name->span.start), length);
        if (entry->name == NULL) {
            r_context_free(context, entry->module_name);
            entry->module_name = NULL;
            return false;
        }
        entry->name_span = name->span;
        entry->imports_name = true;
    }
    context->import_count += 1U;
    return true;
}

static bool r_scan_generic_prefix(const RSource *source, size_t index) {
    while (index != 0U) {
        const RToken *token = &source->tokens[--index];
        if (token->kind == R_TOKEN_SEMICOLON || token->kind == R_TOKEN_LBRACE ||
            token->kind == R_TOKEN_RBRACE) {
            return false;
        }
        if (token->kind == R_TOKEN_IDENTIFIER &&
            r_source_text_equal(source, token->span, "generic") &&
            (r_scan_kind(source, index + 1U) == R_TOKEN_LESS ||
             r_scan_kind(source, index + 1U) == R_TOKEN_LPAREN)) {
            size_t prefix = index;
            while (prefix != 0U && r_token_is_trivia(source->tokens[prefix - 1U].kind)) {
                --prefix;
            }
            if (prefix != 0U && source->tokens[prefix - 1U].kind == R_TOKEN_AT) {
                return true;
            }
        }
    }
    return false;
}

static bool r_add_aggregate_entry(RFrontendContext *context,
                                  RSourceId source_id,
                                  const RSource *source,
                                  const RToken *name,
                                  bool is_protected,
                                  bool is_error,
                                  bool is_generic) {
    RAggregateEntry *entry;
    const char *module_name = (source->module_name == NULL) ? "" : source->module_name;
    size_t name_length;

    if ((name == NULL) || (name->kind != R_TOKEN_IDENTIFIER)) {
        return true;
    }
    if (!r_grow_array(context,
                      (void **)&context->aggregates,
                      &context->aggregate_capacity,
                      sizeof(*context->aggregates),
                      context->aggregate_count + 1U)) {
        return false;
    }
    entry = &context->aggregates[context->aggregate_count];
    (void)memset(entry, 0, sizeof(*entry));
    entry->module_name = r_copy_string(context, module_name, strlen(module_name));
    name_length = (size_t)(name->span.end - name->span.start);
    entry->name =
        r_copy_string(context, (const char *)(source->bytes + name->span.start), name_length);
    if ((entry->module_name == NULL) || (entry->name == NULL)) {
        r_context_free(context, entry->module_name);
        r_context_free(context, entry->name);
        (void)memset(entry, 0, sizeof(*entry));
        return false;
    }
    entry->source = source_id;
    entry->declaration_offset = name->span.end;
    entry->is_generic = is_generic;
    entry->is_error = is_error;
    entry->is_protected = is_protected;
    context->aggregate_count += 1U;
    return true;
}

#include "derive.inc"
#include "test_entry.inc"

RFrontendStatus r_frontend_scan_interface(RFrontendContext *context, RSourceId source_id) {
    RSource *source;
    size_t index = 0U;
    RFrontendStatus lex_status;

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
    if (source->interface_scanned) {
        return source->has_lex_error ? R_FRONTEND_INVALID_SOURCE : R_FRONTEND_OK;
    }
    lex_status = r_frontend_lex(context, source_id);
    if ((lex_status != R_FRONTEND_OK) && (lex_status != R_FRONTEND_INVALID_SOURCE)) {
        return lex_status;
    }

    index = r_skip_trivia(source, index);
    if (r_scan_kind(source, index) == R_TOKEN_AT) {
        /* R-OBJ-0012: module attributes precede the module keyword. */
        const size_t after_attributes = r_scan_skip_leading_attributes(source, index);

        if (r_scan_kind(source, after_attributes) == R_TOKEN_KW_MODULE) {
            index = after_attributes;
        }
    }
    if (r_scan_kind(source, index) == R_TOKEN_KW_MODULE) {
        RTextBuilder module = {0};
        index = r_skip_trivia(source, index) + 1U;
        if (!r_scan_module_path(context, source, &index, &module, NULL)) {
            r_context_free(context, module.items);
            return context->resource_status;
        }
        source->module_name = module.items;
        while ((r_scan_kind(source, index) != R_TOKEN_SEMICOLON) &&
               (r_scan_kind(source, index) != R_TOKEN_EOF)) {
            index = r_skip_trivia(source, index) + 1U;
        }
        if (r_scan_kind(source, index) == R_TOKEN_SEMICOLON) {
            index = r_skip_trivia(source, index) + 1U;
        }
    }
    if (source->module_name == NULL) {
        source->module_name = r_copy_string(context, "", 0U);
        if (source->module_name == NULL) {
            return context->resource_status;
        }
    }

    while (r_scan_kind(source, index) == R_TOKEN_KW_IMPORT) {
        RTextBuilder module = {0};
        RSourceSpan module_span = {source_id, 0U, 0U};
        index = r_skip_trivia(source, index) + 1U;
        if (!r_scan_module_path(context, source, &index, &module, &module_span)) {
            r_context_free(context, module.items);
            return context->resource_status;
        }
        index = r_skip_trivia(source, index);
        if (r_scan_kind(source, index) == R_TOKEN_COLON_COLON) {
            index = r_skip_trivia(source, index) + 1U;
            if (r_scan_kind(source, index) == R_TOKEN_LBRACE) {
                index = r_skip_trivia(source, index) + 1U;
                while ((r_scan_kind(source, index) != R_TOKEN_RBRACE) &&
                       (r_scan_kind(source, index) != R_TOKEN_EOF)) {
                    const RToken *name = r_scan_token(source, index);
                    if ((name != NULL) && (name->kind == R_TOKEN_IDENTIFIER) &&
                        !r_add_import_entry(
                            context, source_id, module.items, module_span, source, name)) {
                        r_context_free(context, module.items);
                        return context->resource_status;
                    }
                    while ((r_scan_kind(source, index) != R_TOKEN_COMMA) &&
                           (r_scan_kind(source, index) != R_TOKEN_RBRACE) &&
                           (r_scan_kind(source, index) != R_TOKEN_EOF)) {
                        index = r_skip_trivia(source, index) + 1U;
                    }
                    if (r_scan_kind(source, index) == R_TOKEN_COMMA) {
                        index = r_skip_trivia(source, index) + 1U;
                    }
                }
            }
        } else if (!r_add_import_entry(
                       context, source_id, module.items, module_span, source, NULL)) {
            r_context_free(context, module.items);
            return context->resource_status;
        }
        r_context_free(context, module.items);
        while ((r_scan_kind(source, index) != R_TOKEN_SEMICOLON) &&
               (r_scan_kind(source, index) != R_TOKEN_EOF)) {
            index = r_skip_trivia(source, index) + 1U;
        }
        if (r_scan_kind(source, index) == R_TOKEN_SEMICOLON) {
            index = r_skip_trivia(source, index) + 1U;
        }
    }

    if (!r_derive_scan_imports(context, source_id, source) ||
        !r_test_scan_imports(context, source_id, source)) {
        return context->resource_status;
    }

    index = 0U;
    while (r_scan_kind(source, index) != R_TOKEN_EOF) {
        size_t significant = r_skip_trivia(source, index);
        RTokenKind kind = r_scan_kind(source, significant);
        if ((kind == R_TOKEN_KW_STRUCT) || (kind == R_TOKEN_KW_ENUM) ||
            r_scan_error_declaration(source, significant)) {
            size_t previous = significant;
            size_t next = significant + 1U;
            bool is_protected = false;
            while (previous > 0U) {
                previous -= 1U;
                if (!r_token_is_trivia(source->tokens[previous].kind)) {
                    is_protected = source->tokens[previous].kind == R_TOKEN_KW_PROTECTED;
                    break;
                }
            }
            next = r_skip_trivia(source, next);
            if (!r_add_aggregate_entry(context,
                                       source_id,
                                       source,
                                       (next < source->token_count) ? &source->tokens[next] : NULL,
                                       is_protected,
                                       r_scan_error_declaration(source, significant),
                                       r_scan_generic_prefix(source, significant))) {
                return context->resource_status;
            }
        }
        index = significant + 1U;
    }
    source->interface_scanned = true;
    return source->has_lex_error ? R_FRONTEND_INVALID_SOURCE : R_FRONTEND_OK;
}

static bool r_parser_add_event(RParser *parser, RCstEvent event, size_t *event_index) {
    RSource *source = parser->source;

    if (source->cst_event_count >= UINT32_MAX) {
        parser->context->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
        return false;
    }
    if (!r_grow_array(parser->context,
                      (void **)&source->cst_events,
                      &source->cst_event_capacity,
                      sizeof(*source->cst_events),
                      source->cst_event_count + 1U)) {
        return false;
    }
    if (event_index != NULL) {
        *event_index = source->cst_event_count;
    }
    source->cst_events[source->cst_event_count] = event;
    source->cst_event_count += 1U;
    return true;
}

static RTokenKind r_parser_peek_kind(const RParser *parser) {
    return r_scan_kind(parser->source, parser->cursor);
}

static const RToken *r_parser_peek_token(const RParser *parser) {
    return r_scan_token(parser->source, parser->cursor);
}

static RTokenKind r_parser_peek_n_kind(const RParser *parser, size_t distance) {
    size_t index = parser->cursor;
    size_t current;
    for (current = 0U; current < distance; ++current) {
        index = r_skip_trivia(parser->source, index);
        if (index < parser->source->token_count) {
            index += 1U;
        }
    }
    return r_scan_kind(parser->source, index);
}

static bool r_parser_emit_token(RParser *parser, size_t token_index) {
    RCstEvent event;
    const RToken *token = &parser->source->tokens[token_index];
    (void)memset(&event, 0, sizeof(event));
    event.kind = R_CST_EVENT_TOKEN;
    event.token = (RTokenId)(token_index + 1U);
    event.span = token->span;
    parser->last_consumed_end = token->span.end;
    return r_parser_add_event(parser, event, NULL);
}

static bool r_parser_flush_through(RParser *parser, size_t token_index) {
    while ((parser->cursor <= token_index) && (parser->cursor < parser->source->token_count)) {
        if (!r_parser_emit_token(parser, parser->cursor)) {
            return false;
        }
        parser->cursor += 1U;
    }
    return true;
}

static bool r_parser_flush_trivia(RParser *parser) {
    while ((parser->cursor < parser->source->token_count) &&
           r_token_is_trivia(parser->source->tokens[parser->cursor].kind)) {
        if (!r_parser_emit_token(parser, parser->cursor)) {
            return false;
        }
        parser->cursor += 1U;
    }
    return true;
}

static bool r_parser_bump(RParser *parser) {
    size_t significant = r_skip_trivia(parser->source, parser->cursor);
    if (significant >= parser->source->token_count) {
        return false;
    }
    return r_parser_flush_through(parser, significant);
}

static bool r_parser_enter_nesting(RParser *parser) {
    if (parser->nesting_depth >= parser->context->options.limits.max_nesting) {
        if (!parser->suppress_diagnostics) {
            const RToken *token = r_parser_peek_token(parser);
            RSourceSpan span =
                token != NULL ? token->span : (RSourceSpan){parser->source_id, 0U, 0U};
            (void)r_add_diagnostic(parser->context,
                                   "R-DIAG-LIMIT-001",
                                   "R-LIMIT-0003",
                                   "parser nesting limit exceeded",
                                   R_DIAGNOSTIC_ERROR,
                                   span);
        }
        parser->context->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
        return false;
    }
    parser->nesting_depth += 1U;
    return true;
}

static void r_parser_leave_nesting(RParser *parser) {
    if (parser->nesting_depth != 0U) {
        parser->nesting_depth -= 1U;
    }
}

static size_t r_parser_open_raw(RParser *parser, RSyntaxKind kind, bool flush_trivia) {
    RCstEvent event;
    const RToken *token;
    size_t event_index = SIZE_MAX;

    if (flush_trivia && !r_parser_flush_trivia(parser)) {
        return SIZE_MAX;
    }
    token = r_parser_peek_token(parser);
    (void)memset(&event, 0, sizeof(event));
    event.kind = R_CST_EVENT_OPEN;
    event.syntax_kind = kind;
    if (token != NULL) {
        event.span.source = parser->source_id;
        event.span.start = token->span.start;
        event.span.end = token->span.start;
    }
    if (!r_parser_add_event(parser, event, &event_index)) {
        return SIZE_MAX;
    }
    parser->depth += 1U;
    return event_index;
}

static size_t r_parser_open(RParser *parser, RSyntaxKind kind) {
    return r_parser_open_raw(parser, kind, true);
}

static bool r_parser_close(RParser *parser, size_t open_event) {
    RCstEvent close_event;
    size_t close_index;

    if ((open_event == SIZE_MAX) || (open_event >= parser->source->cst_event_count) ||
        (parser->depth == 0U)) {
        return false;
    }
    (void)memset(&close_event, 0, sizeof(close_event));
    close_event.kind = R_CST_EVENT_CLOSE;
    close_event.syntax_kind = parser->source->cst_events[open_event].syntax_kind;
    close_event.span = parser->source->cst_events[open_event].span;
    close_event.span.end = parser->last_consumed_end;
    close_event.matching_event = (uint32_t)open_event;
    if (!r_parser_add_event(parser, close_event, &close_index)) {
        return false;
    }
    parser->source->cst_events[open_event].matching_event = (uint32_t)close_index;
    parser->source->cst_events[open_event].span.end = parser->last_consumed_end;
    parser->depth -= 1U;
    return true;
}

static bool r_parser_at(const RParser *parser, RTokenKind kind) {
    return r_parser_peek_kind(parser) == kind;
}

/* Open a node of `kind` at event `mark`, before the events already parsed from there. */
static size_t r_parser_open_at(RParser *parser, size_t mark, RSyntaxKind kind) {
    RCstEvent opening;
    size_t index;

    if (mark > parser->source->cst_event_count) {
        return SIZE_MAX;
    }
    (void)memset(&opening, 0, sizeof(opening));
    opening.kind = R_CST_EVENT_OPEN;
    opening.syntax_kind = kind;
    opening.span.source = parser->source_id;
    if (mark < parser->source->cst_event_count) {
        opening.span.start = parser->source->cst_events[mark].span.start;
        opening.span.end = opening.span.start;
    }
    if (!r_parser_add_event(parser, opening, NULL)) {
        return SIZE_MAX;
    }
    (void)memmove(parser->source->cst_events + mark + 1U,
                  parser->source->cst_events + mark,
                  (parser->source->cst_event_count - mark - 1U) * sizeof(opening));
    parser->source->cst_events[mark] = opening;
    for (index = mark + 1U; index < parser->source->cst_event_count; ++index) {
        RCstEvent *event = &parser->source->cst_events[index];
        if ((event->kind == R_CST_EVENT_OPEN) || (event->kind == R_CST_EVENT_CLOSE)) {
            event->matching_event += 1U;
        }
    }
    parser->depth += 1U;
    return mark;
}

/* Wrap the completed left operand without changing trees for other expressions. */
static bool r_parser_wrap_expression(RParser *parser, size_t node, RSyntaxKind kind) {
    RCstEvent wrapper;
    size_t index;

    if (!r_parser_close(parser, node)) {
        return false;
    }
    wrapper = parser->source->cst_events[node];
    wrapper.syntax_kind = kind;
    wrapper.matching_event = 0U;
    if (!r_parser_add_event(parser, wrapper, NULL)) {
        return false;
    }
    (void)memmove(parser->source->cst_events + node + 1U,
                  parser->source->cst_events + node,
                  (parser->source->cst_event_count - node - 1U) * sizeof(wrapper));
    parser->source->cst_events[node] = wrapper;
    for (index = node + 1U; index < parser->source->cst_event_count; ++index) {
        RCstEvent *event = &parser->source->cst_events[index];
        if ((event->kind == R_CST_EVENT_OPEN) || (event->kind == R_CST_EVENT_CLOSE)) {
            event->matching_event += 1U;
        }
    }
    parser->depth += 1U;
    return true;
}

static bool r_parser_eat(RParser *parser, RTokenKind kind) {
    if (!r_parser_at(parser, kind)) {
        return false;
    }
    return r_parser_bump(parser);
}

static bool r_parser_syntax_error(RParser *parser, const char *message) {
    const RToken *token = r_parser_peek_token(parser);
    RSourceSpan span;

    if (token != NULL) {
        span = token->span;
    } else {
        span.source = parser->source_id;
        span.start = (uint32_t)parser->source->length;
        span.end = span.start;
    }
    parser->source->has_syntax_error = true;
    if (!parser->suppress_diagnostics) {
        return r_add_diagnostic(
            parser->context, "R-DIAG-SYN-001", "R-GRAM-0005", message, R_DIAGNOSTIC_ERROR, span);
    }
    return true;
}

static bool r_parser_missing(RParser *parser, RTokenKind expected, const char *message) {
    RCstEvent event;
    const RToken *token = r_parser_peek_token(parser);

    (void)r_parser_syntax_error(parser, message);
    (void)memset(&event, 0, sizeof(event));
    event.kind = R_CST_EVENT_MISSING;
    event.missing_kind = expected;
    event.span.source = parser->source_id;
    event.span.start = (token == NULL) ? (uint32_t)parser->source->length : token->span.start;
    event.span.end = event.span.start;
    return r_parser_add_event(parser, event, NULL);
}

static bool r_parser_expect(RParser *parser, RTokenKind kind, const char *message) {
    if (r_parser_eat(parser, kind)) {
        return true;
    }
    return r_parser_missing(parser, kind, message);
}

/* R-TYPE-0031: type and generic argument lists are written `<...>`. */
static bool r_parser_at_type_close(const RParser *parser) {
    return (parser->pending_greater != 0U) || r_parser_at(parser, R_TOKEN_GREATER) ||
           r_parser_at(parser, R_TOKEN_GREATER_GREATER);
}

static bool r_parser_expect_type_close(RParser *parser, const char *message) {
    if (parser->pending_greater != 0U) {
        parser->pending_greater -= 1U;
        return true;
    }
    if (r_parser_at(parser, R_TOKEN_GREATER_GREATER)) {
        parser->pending_greater = 1U;
        return r_parser_bump(parser);
    }
    return r_parser_expect(parser, R_TOKEN_GREATER, message);
}

/* The index after the `<...>` list starting at `index`, or zero when the tokens cannot form a
   type argument list. Parenthesized and bracketed text is skipped as a unit. */
static size_t r_skip_balanced_angles(const RParser *parser, size_t index) {
    uint32_t angles = 0U;
    uint32_t groups = 0U;
    RTokenKind previous = R_TOKEN_INVALID;
    index = r_skip_trivia(parser->source, index);
    if (r_scan_kind(parser->source, index) != R_TOKEN_LESS) {
        return 0U;
    }
    do {
        const RTokenKind kind = r_scan_kind(parser->source, index);
        /* `*?` and `fn?` are nullable pointer spellings; no conditional `?` follows them. */
        const bool nullable_marker = (kind == R_TOKEN_QUESTION) &&
                                     ((previous == R_TOKEN_STAR) || (previous == R_TOKEN_KW_FN));
        if (kind == R_TOKEN_EOF) {
            return 0U;
        }
        if ((kind == R_TOKEN_LPAREN) || (kind == R_TOKEN_LBRACKET)) {
            groups += 1U;
        } else if ((kind == R_TOKEN_RPAREN) || (kind == R_TOKEN_RBRACKET)) {
            if (groups == 0U) {
                return 0U;
            }
            groups -= 1U;
        } else if (groups == 0U) {
            if (kind == R_TOKEN_LESS) {
                angles += 1U;
            } else if (kind == R_TOKEN_GREATER) {
                angles -= 1U;
            } else if (kind == R_TOKEN_GREATER_GREATER) {
                if (angles < 2U) {
                    return 0U;
                }
                angles -= 2U;
            } else if ((kind == R_TOKEN_SEMICOLON) || (kind == R_TOKEN_LBRACE) ||
                       (kind == R_TOKEN_RBRACE) || (kind == R_TOKEN_EQUAL) ||
                       (kind == R_TOKEN_AMP_AMP) || (kind == R_TOKEN_PIPE_PIPE) ||
                       (kind == R_TOKEN_EQUAL_EQUAL) || (kind == R_TOKEN_BANG_EQUAL) ||
                       (kind == R_TOKEN_LESS_EQUAL) || (kind == R_TOKEN_GREATER_EQUAL) ||
                       ((kind == R_TOKEN_QUESTION) && !nullable_marker)) {
                return 0U;
            }
        }
        if (angles > parser->context->options.limits.max_nesting) {
            return 0U;
        }
        previous = kind;
        index = r_skip_trivia(parser->source, index + 1U);
    } while (angles != 0U);
    return index;
}

static bool r_token_in_set(RTokenKind kind, const RTokenKind *set, size_t count) {
    size_t index;
    for (index = 0U; index < count; ++index) {
        if (kind == set[index]) {
            return true;
        }
    }
    return false;
}

static bool r_parser_recover(RParser *parser, const RTokenKind *synchronizers, size_t count) {
    size_t node = r_parser_open(parser, R_SYNTAX_ERROR);
    bool consumed = false;

    while (!r_parser_at(parser, R_TOKEN_EOF) &&
           !r_token_in_set(r_parser_peek_kind(parser), synchronizers, count)) {
        if (!r_parser_bump(parser)) {
            break;
        }
        consumed = true;
    }
    if (!consumed && !r_parser_at(parser, R_TOKEN_EOF)) {
        consumed = r_parser_bump(parser);
    }
    return r_parser_close(parser, node) && consumed;
}

static RExpressionInfo r_parse_expression_mode(RParser *parser, RExpressionMode mode);
static bool r_parse_await_operation(RParser *parser);
static RExpressionInfo r_parse_condition_expression(RParser *parser);
static bool r_parse_aggregate_initializer(RParser *parser, RExpressionMode mode);
static bool r_parse_braced_initializer(RParser *parser, RExpressionMode mode, bool allow_dict);
static bool r_parse_comprehension_clauses(RParser *parser);
static bool r_parse_collection_expression(RParser *parser);
static bool r_visible_qualified_generic_aggregate(const RParser *parser);

static bool r_parser_token_text_is(const RParser *parser, const RToken *token, const char *text) {
    return (token != NULL) && r_source_text_equal(parser->source, token->span, text);
}

static bool r_parser_token_index_text_is(const RParser *parser, size_t index, const char *text) {
    return r_parser_token_text_is(parser, r_scan_token(parser->source, index), text);
}

static bool r_abi_literal_is_c(const RParser *parser, const RToken *token) {
    const uint8_t *bytes;
    size_t length;
    if ((token == NULL) || (token->kind != R_TOKEN_STRING_LITERAL) ||
        (token->span.end < token->span.start)) {
        return false;
    }
    bytes = parser->source->bytes + token->span.start;
    length = (size_t)(token->span.end - token->span.start);
    if ((length == 3U) && (bytes[0] == (uint8_t)'"') && (bytes[1] == (uint8_t)'C') &&
        (bytes[2] == (uint8_t)'"')) {
        return true;
    }
    if ((length == 6U) && (bytes[0] == (uint8_t)'"') && (bytes[1] == (uint8_t)'\\') &&
        (bytes[2] == (uint8_t)'x') && (bytes[3] == (uint8_t)'4') && (bytes[4] == (uint8_t)'3') &&
        (bytes[5] == (uint8_t)'"')) {
        return true;
    }
    if ((length >= 7U) && (bytes[0] == (uint8_t)'"') && (bytes[1] == (uint8_t)'\\') &&
        (bytes[2] == (uint8_t)'u') && (bytes[3] == (uint8_t)'{') &&
        (bytes[length - 2U] == (uint8_t)'}') && (bytes[length - 1U] == (uint8_t)'"')) {
        uint32_t value = UINT32_C(0);
        size_t index;
        if ((length - 6U) > 6U) {
            return false;
        }
        for (index = 4U; index < length - 2U; ++index) {
            uint8_t byte = bytes[index];
            uint32_t digit;
            if ((byte >= (uint8_t)'0') && (byte <= (uint8_t)'9')) {
                digit = (uint32_t)(byte - (uint8_t)'0');
            } else if ((byte >= (uint8_t)'a') && (byte <= (uint8_t)'f')) {
                digit = (uint32_t)(byte - (uint8_t)'a') + UINT32_C(10);
            } else if ((byte >= (uint8_t)'A') && (byte <= (uint8_t)'F')) {
                digit = (uint32_t)(byte - (uint8_t)'A') + UINT32_C(10);
            } else {
                return false;
            }
            value = (value << 4U) | digit;
        }
        return value == UINT32_C(0x43);
    }
    return false;
}

/* R-TYPE-0036: `name::<arguments>` closes a generic function; `::` then `<` is not a path. */
static bool r_generic_arguments_at(const RParser *parser, size_t index) {
    index = r_skip_trivia(parser->source, index);
    return (r_scan_kind(parser->source, index) == R_TOKEN_COLON_COLON) &&
           (r_scan_kind(parser->source, r_skip_trivia(parser->source, index + 1U)) == R_TOKEN_LESS);
}

static bool r_is_qualified_component(RTokenKind kind) {
    return (kind == R_TOKEN_IDENTIFIER) || (kind == R_TOKEN_KW_ASYNC) || (kind == R_TOKEN_KW_ARC) ||
           (kind == R_TOKEN_KW_ARRAY) || (kind == R_TOKEN_KW_DICT) || (kind == R_TOKEN_KW_LIST) ||
           (kind == R_TOKEN_KW_RC) || (kind == R_TOKEN_KW_O) || (kind == R_TOKEN_KW_NULL);
}

static bool r_parse_qualified_tail(RParser *parser, bool require_colon_colon) {
    bool saw_colon_colon = false;

    while (r_parser_at(parser, R_TOKEN_DOT)) {
        if (!r_parser_bump(parser)) {
            return false;
        }
        if (!r_is_qualified_component(r_parser_peek_kind(parser))) {
            return r_parser_missing(
                parser, R_TOKEN_IDENTIFIER, "expected qualified-name component after '.'");
        }
        if (!r_parser_bump(parser)) {
            return false;
        }
    }
    while (r_parser_at(parser, R_TOKEN_COLON_COLON) &&
           !r_generic_arguments_at(parser, parser->cursor)) {
        saw_colon_colon = true;
        if (!r_parser_bump(parser)) {
            return false;
        }
        if (!r_is_qualified_component(r_parser_peek_kind(parser))) {
            return r_parser_missing(
                parser, R_TOKEN_IDENTIFIER, "expected qualified-name component after '::'");
        }
        if (!r_parser_bump(parser)) {
            return false;
        }
    }
    if (require_colon_colon && !saw_colon_colon) {
        return false;
    }
    return true;
}

static bool r_parse_type(RParser *parser, bool allow_unresolved_name);
static bool r_parse_fn_resource_attributes(RParser *parser);

static bool r_parse_throws_clause(RParser *parser) {
    size_t node = r_parser_open(parser, R_SYNTAX_THROWS_CLAUSE);

    (void)r_parser_expect(parser, R_TOKEN_KW_THROWS, "expected throws");
    if (!r_parse_type(parser, true)) {
        (void)r_parser_syntax_error(parser, "throws requires at least one error type");
    }
    while (r_parser_eat(parser, R_TOKEN_COMMA)) {
        if (!r_parse_type(parser, true)) {
            (void)r_parser_syntax_error(parser, "expected error type after ',' in throws clause");
            break;
        }
    }
    return r_parser_close(parser, node);
}

static bool r_parse_task_type(RParser *parser) {
    bool parsed;

    (void)r_parser_expect(parser, R_TOKEN_KW_TASK, "expected task");
    (void)r_parser_expect(parser, R_TOKEN_LESS, "expected '<' after task");
    parsed = r_parse_type(parser, true);
    if (!parsed) {
        (void)r_parser_syntax_error(parser, "expected task value type");
    }
    if (r_parser_at(parser, R_TOKEN_KW_THROWS)) {
        (void)r_parse_throws_clause(parser);
    }
    (void)r_parser_expect_type_close(parser, "expected '>' after task type");
    return parsed;
}

static bool r_parse_effectful_type_argument(RParser *parser) {
    bool parsed;

    (void)r_parser_expect(parser, R_TOKEN_LESS, "expected '<' after effectful type");
    parsed = r_parse_type(parser, true);
    if (!parsed) {
        (void)r_parser_syntax_error(parser, "expected effectful value type");
    }
    if (r_parser_at(parser, R_TOKEN_KW_THROWS)) {
        (void)r_parse_throws_clause(parser);
    }
    (void)r_parser_expect_type_close(parser, "expected '>' after effectful type");
    return parsed;
}

static bool r_standard_thread_handle_type(const RParser *parser) {
    size_t index = r_skip_trivia(parser->source, parser->cursor);
    size_t module_index;
    size_t name_index;

    if (!r_parser_token_index_text_is(parser, index, "std")) {
        return false;
    }
    index = r_skip_trivia(parser->source, index + 1U);
    if (r_scan_kind(parser->source, index) != R_TOKEN_DOT) {
        return false;
    }
    module_index = r_skip_trivia(parser->source, index + 1U);
    if (!r_parser_token_index_text_is(parser, module_index, "thread")) {
        return false;
    }
    index = r_skip_trivia(parser->source, module_index + 1U);
    if (r_scan_kind(parser->source, index) != R_TOKEN_COLON_COLON) {
        return false;
    }
    name_index = r_skip_trivia(parser->source, index + 1U);
    return r_parser_token_index_text_is(parser, name_index, "join_handle") ||
           r_parser_token_index_text_is(parser, name_index, "scoped_join_handle");
}

static int r_standard_parametric_type_arity(const RParser *parser, size_t *tail_index) {
    size_t index = r_skip_trivia(parser->source, parser->cursor);
    size_t module_index;
    size_t name_index;

    if (r_parser_token_index_text_is(parser, index, "core")) {
        index = r_skip_trivia(parser->source, index + 1U);
        if (r_scan_kind(parser->source, index) != R_TOKEN_COLON_COLON) {
            return -1;
        }
        name_index = r_skip_trivia(parser->source, index + 1U);
        if (!r_is_qualified_component(r_scan_kind(parser->source, name_index))) {
            return -1;
        }
        if (tail_index != NULL) {
            *tail_index = r_skip_trivia(parser->source, name_index + 1U);
        }
        /* R-GRAM-0005: `core::memory_order` and `core::utf8_error` name closed types too. */
        return r_parser_token_index_text_is(parser, name_index, "atomic_compare_exchange_result")
                   ? 1
                   : 0;
    }
    if (!r_parser_token_index_text_is(parser, index, "std")) {
        return -1;
    }
    index = r_skip_trivia(parser->source, index + 1U);
    if (r_scan_kind(parser->source, index) != R_TOKEN_DOT) {
        return -1;
    }
    module_index = r_skip_trivia(parser->source, index + 1U);
    index = r_skip_trivia(parser->source, module_index + 1U);
    if (r_scan_kind(parser->source, index) != R_TOKEN_COLON_COLON) {
        return -1;
    }
    name_index = r_skip_trivia(parser->source, index + 1U);
    if (!r_is_qualified_component(r_scan_kind(parser->source, name_index))) {
        return -1;
    }
    if (tail_index != NULL) {
        *tail_index = r_skip_trivia(parser->source, name_index + 1U);
    }
    if (r_parser_token_index_text_is(parser, module_index, "json") &&
        (r_parser_token_index_text_is(parser, name_index, "decoder") ||
         r_parser_token_index_text_is(parser, name_index, "reader") ||
         r_parser_token_index_text_is(parser, name_index, "detached")))
        return 1;
    if ((r_parser_token_index_text_is(parser, module_index, "arc") ||
         r_parser_token_index_text_is(parser, module_index, "rc")) &&
        r_parser_token_index_text_is(parser, name_index, "try_unwrap_result")) {
        return 1;
    }
    if (r_parser_token_index_text_is(parser, module_index, "dict") &&
        (r_parser_token_index_text_is(parser, name_index, "insert_error") ||
         r_parser_token_index_text_is(parser, name_index, "iter") ||
         r_parser_token_index_text_is(parser, name_index, "entry_ref"))) {
        return 2;
    }
    if ((r_parser_token_index_text_is(parser, module_index, "alloc") &&
         (r_parser_token_index_text_is(parser, name_index, "new_error") ||
          r_parser_token_index_text_is(parser, name_index, "alloc_error"))) ||
        (r_parser_token_index_text_is(parser, module_index, "thread") &&
         (r_parser_token_index_text_is(parser, name_index, "join_handle") ||
          r_parser_token_index_text_is(parser, name_index, "scoped_join_handle") ||
          r_parser_token_index_text_is(parser, name_index, "join_result"))) ||
        (r_parser_token_index_text_is(parser, module_index, "sync") &&
         (r_parser_token_index_text_is(parser, name_index, "mutex") ||
          r_parser_token_index_text_is(parser, name_index, "rw_lock") ||
          r_parser_token_index_text_is(parser, name_index, "lock_result") ||
          r_parser_token_index_text_is(parser, name_index, "try_lock_result") ||
          r_parser_token_index_text_is(parser, name_index, "read_lock_result") ||
          r_parser_token_index_text_is(parser, name_index, "try_read_lock_result") ||
          r_parser_token_index_text_is(parser, name_index, "write_lock_result") ||
          r_parser_token_index_text_is(parser, name_index, "try_write_lock_result") ||
          r_parser_token_index_text_is(parser, name_index, "mutex_guard") ||
          r_parser_token_index_text_is(parser, name_index, "rw_read_guard") ||
          r_parser_token_index_text_is(parser, name_index, "rw_write_guard") ||
          r_parser_token_index_text_is(parser, name_index, "once_lock") ||
          r_parser_token_index_text_is(parser, name_index, "set_result") ||
          r_parser_token_index_text_is(parser, name_index, "channel") ||
          r_parser_token_index_text_is(parser, name_index, "sync_channel") ||
          r_parser_token_index_text_is(parser, name_index, "sender") ||
          r_parser_token_index_text_is(parser, name_index, "sync_sender") ||
          r_parser_token_index_text_is(parser, name_index, "receiver") ||
          r_parser_token_index_text_is(parser, name_index, "send_result") ||
          r_parser_token_index_text_is(parser, name_index, "try_send_result") ||
          r_parser_token_index_text_is(parser, name_index, "recv_result") ||
          r_parser_token_index_text_is(parser, name_index, "try_recv_result") ||
          r_parser_token_index_text_is(parser, name_index, "permit") ||
          r_parser_token_index_text_is(parser, name_index, "reserve_result") ||
          r_parser_token_index_text_is(parser, name_index, "try_reserve_result"))) ||
        /* R-SLIB-ASYNC-0013..0016 (L30): the generic asynchronous resources. */
        (r_parser_token_index_text_is(parser, module_index, "async") &&
         (r_parser_token_index_text_is(parser, name_index, "mutex") ||
          r_parser_token_index_text_is(parser, name_index, "mutex_guard") ||
          r_parser_token_index_text_is(parser, name_index, "rw_lock") ||
          r_parser_token_index_text_is(parser, name_index, "rw_read_guard") ||
          r_parser_token_index_text_is(parser, name_index, "rw_write_guard") ||
          r_parser_token_index_text_is(parser, name_index, "broadcast") ||
          r_parser_token_index_text_is(parser, name_index, "broadcast_receiver") ||
          r_parser_token_index_text_is(parser, name_index, "broadcast_result"))) ||
        (r_parser_token_index_text_is(parser, module_index, "array") &&
         r_parser_token_index_text_is(parser, name_index, "push_error")) ||
        (r_parser_token_index_text_is(parser, module_index, "list") &&
         (r_parser_token_index_text_is(parser, name_index, "push_error") ||
          r_parser_token_index_text_is(parser, name_index, "iter")))) {
        return 1;
    }
    return 0;
}

static bool r_token_text_is_one_of(const RParser *parser,
                                   const RToken *token,
                                   const char *const *names,
                                   size_t count) {
    size_t index;
    for (index = 0U; index < count; ++index) {
        if (r_parser_token_text_is(parser, token, names[index])) {
            return true;
        }
    }
    return false;
}

static bool r_standard_keyword_path_parts(const RParser *parser,
                                          const RToken **module,
                                          const RToken **operation,
                                          size_t *tail_index) {
    size_t index = r_skip_trivia(parser->source, parser->cursor);
    if (!r_parser_token_index_text_is(parser, index, "std")) {
        return false;
    }
    index = r_skip_trivia(parser->source, index + 1U);
    if (r_scan_kind(parser->source, index) != R_TOKEN_DOT) {
        return false;
    }
    index = r_skip_trivia(parser->source, index + 1U);
    *module = r_scan_token(parser->source, index);
    if ((*module == NULL) || ((*module)->kind == R_TOKEN_IDENTIFIER)) {
        return false;
    }
    index = r_skip_trivia(parser->source, index + 1U);
    if (r_scan_kind(parser->source, index) != R_TOKEN_COLON_COLON) {
        return true;
    }
    index = r_skip_trivia(parser->source, index + 1U);
    *operation = r_scan_token(parser->source, index);
    if (*operation == NULL) {
        return true;
    }
    *tail_index = r_skip_trivia(parser->source, index + 1U);
    return true;
}

static bool r_standard_keyword_type_is_allowed(const RParser *parser, int arity) {
    const RToken *module = NULL;
    const RToken *operation = NULL;
    size_t tail = 0U;
    if (!r_standard_keyword_path_parts(parser, &module, &operation, &tail)) {
        return true;
    }
    if ((module == NULL) || (operation == NULL)) {
        return false;
    }
    if (arity > 0) {
        return r_scan_kind(parser->source, tail) == R_TOKEN_LESS;
    }
    return (module->kind == R_TOKEN_KW_ASYNC) &&
           (r_parser_token_text_is(parser, operation, "start_error") ||
            r_parser_token_text_is(parser, operation, "semaphore") ||
            r_parser_token_text_is(parser, operation, "semaphore_permit") ||
            r_parser_token_text_is(parser, operation, "notify")) &&
           (r_scan_kind(parser->source, tail) != R_TOKEN_COLON_COLON);
}

static bool r_standard_keyword_expression_is_allowed(const RParser *parser) {
    static const char *const owner_operations[] = {
        "clone",
        "clone_weak",
        "downgrade",
        "upgrade",
        "get_mut",
        "try_unwrap",
        "strong_count",
        "weak_count",
        "ptr_eq",
        "into_raw",
        "from_raw",
    };
    static const char *const array_operations[] = {
        "create",
        "with_capacity",
        /* R-LIB-0019 (P4.2). */
        "filled",
        "capacity",
        "reserve",
        "push",
        "pop",
        "remove",
        "get",
        "get_mut",
        "as_slice",
        "as_slice_mut",
        "clear",
    };
    static const char *const list_operations[] = {
        "create",
        "push_front",
        "push_back",
        "insert_before",
        "insert_after",
        "front",
        "back",
        "front_mut",
        "back_mut",
        "get",
        "get_mut",
        "remove",
        "pop_front",
        "pop_back",
        "clear",
        "iter",
        "next",
    };
    /* R-SLIB-ASYNC-0013..0016 (L30). */
    static const char *const async_operations[] = {
        "cancel",
        "detach",
        "join",
        "mutex_new",
        "clone_mutex",
        "lock",
        "try_lock",
        "mutex_guard_ref",
        "mutex_guard_mut",
        "unlock",
        "rwlock_new",
        "clone_rw_lock",
        "read",
        "write",
        "try_read",
        "try_write",
        "rw_read_guard_ref",
        "rw_write_guard_ref",
        "rw_write_guard_mut",
        "semaphore_new",
        "clone_semaphore",
        "acquire",
        "try_acquire",
        "release",
        "add_permits",
        "available_permits",
        "notify_new",
        "clone_notify",
        "notify_one",
        "notify_all",
        "notified",
        "broadcast",
        "clone_broadcast",
        "subscribe",
        "publish",
        "broadcast_receive",
        /* R-SLIB-ASYNC-0017 (L31). */
        "blocking",
        /* R-SLIB-ASYNC-0018 (M23). */
        "task_id",
    };
    static const char *const dict_operations[] = {
        "create",
        "with_capacity",
        "reserve",
        "insert",
        "contains",
        "get",
        "get_mut",
        "remove",
        "clear",
        "iter",
        "next",
    };
    const RToken *module = NULL;
    const RToken *operation = NULL;
    size_t tail = 0U;
    const char *const *operations = NULL;
    size_t operation_count = 0U;

    if (!r_standard_keyword_path_parts(parser, &module, &operation, &tail)) {
        return true;
    }
    if ((module == NULL) || (operation == NULL)) {
        return false;
    }
    if ((module->kind == R_TOKEN_KW_ARC) || (module->kind == R_TOKEN_KW_RC)) {
        operations = owner_operations;
        operation_count = sizeof(owner_operations) / sizeof(owner_operations[0]);
    } else if (module->kind == R_TOKEN_KW_ARRAY) {
        operations = array_operations;
        operation_count = sizeof(array_operations) / sizeof(array_operations[0]);
    } else if (module->kind == R_TOKEN_KW_LIST) {
        operations = list_operations;
        operation_count = sizeof(list_operations) / sizeof(list_operations[0]);
    } else if (module->kind == R_TOKEN_KW_DICT) {
        operations = dict_operations;
        operation_count = sizeof(dict_operations) / sizeof(dict_operations[0]);
    } else if (module->kind == R_TOKEN_KW_ASYNC) {
        operations = async_operations;
        operation_count = sizeof(async_operations) / sizeof(async_operations[0]);
        if (r_parser_token_text_is(parser, operation, "broadcast_result") &&
            (r_scan_kind(parser->source, tail) == R_TOKEN_COLON_COLON)) {
            const size_t variant_index = r_skip_trivia(parser->source, tail + 1U);
            const RToken *variant = r_scan_token(parser->source, variant_index);
            const size_t after_variant = r_skip_trivia(parser->source, variant_index + 1U);

            return (r_parser_token_text_is(parser, variant, "received") ||
                    r_parser_token_text_is(parser, variant, "lagged") ||
                    r_parser_token_text_is(parser, variant, "closed")) &&
                   (r_scan_kind(parser->source, after_variant) != R_TOKEN_COLON_COLON);
        }
    }
    if ((operations != NULL) &&
        r_token_text_is_one_of(parser, operation, operations, operation_count) &&
        ((r_scan_kind(parser->source, tail) != R_TOKEN_COLON_COLON) ||
         r_generic_arguments_at(parser, tail))) {
        return true;
    }
    if (((module->kind == R_TOKEN_KW_ARC) || (module->kind == R_TOKEN_KW_RC)) &&
        r_parser_token_text_is(parser, operation, "try_unwrap_result") &&
        (r_scan_kind(parser->source, tail) == R_TOKEN_COLON_COLON)) {
        const size_t variant_index = r_skip_trivia(parser->source, tail + 1U);
        const RToken *variant = r_scan_token(parser->source, variant_index);
        const size_t after_variant = r_skip_trivia(parser->source, variant_index + 1U);

        return (r_parser_token_text_is(parser, variant, "unwrapped") ||
                r_parser_token_text_is(parser, variant, "shared")) &&
               (r_scan_kind(parser->source, after_variant) != R_TOKEN_COLON_COLON);
    }
    if ((module->kind == R_TOKEN_KW_ARRAY) || (module->kind == R_TOKEN_KW_LIST) ||
        (module->kind == R_TOKEN_KW_DICT) || (module->kind == R_TOKEN_KW_ASYNC)) {
        const char *error_type = module->kind == R_TOKEN_KW_DICT    ? "insert_error"
                                 : module->kind == R_TOKEN_KW_ASYNC ? "start_error"
                                                                    : "push_error";
        if (r_parser_token_text_is(parser, operation, error_type) &&
            (r_scan_kind(parser->source, tail) == R_TOKEN_COLON_COLON)) {
            size_t variant_index = r_skip_trivia(parser->source, tail + 1U);
            const RToken *variant = r_scan_token(parser->source, variant_index);
            size_t after_variant = r_skip_trivia(parser->source, variant_index + 1U);
            bool valid_variant =
                module->kind == R_TOKEN_KW_ASYNC
                    ? (r_parser_token_text_is(parser, variant, "allocation_failed") ||
                       r_parser_token_text_is(parser, variant, "runtime_stopping") ||
                       r_parser_token_text_is(parser, variant, "scope_full") ||
                       r_parser_token_text_is(parser, variant, "budget_exhausted"))
                    : r_parser_token_text_is(parser, variant, "allocation_failed");
            return valid_variant &&
                   (r_scan_kind(parser->source, after_variant) != R_TOKEN_COLON_COLON);
        }
    }
    return false;
}

static bool r_parse_parenthesized_type(RParser *parser, bool allow_unresolved_name) {
    if (!r_parser_eat(parser, R_TOKEN_LPAREN)) {
        return false;
    }
    if (!r_parse_type(parser, allow_unresolved_name)) {
        (void)r_parser_syntax_error(parser, "expected type inside parentheses");
    }
    /* R-TYPE-0053 (L18.3): `(T...)` is the tuple of a pack. */
    (void)r_parser_eat(parser, R_TOKEN_ELLIPSIS);
    /* R-TYPE-0052 (L18.1): `(T1, T2, ...)` is a tuple type. */
    while (r_parser_eat(parser, R_TOKEN_COMMA)) {
        if (!r_parse_type(parser, allow_unresolved_name)) {
            (void)r_parser_syntax_error(parser, "expected tuple element type");
            break;
        }
        (void)r_parser_eat(parser, R_TOKEN_ELLIPSIS);
    }
    return r_parser_expect(parser, R_TOKEN_RPAREN, "expected ')' after type");
}

/* generic-argument = type | constant-expression; a constant is wrapped in a type node. */
static bool r_parse_generic_argument(RParser *parser, bool allow_unresolved_name) {
    RParser trial = *parser;
    const size_t events = parser->source->cst_event_count;
    const bool had_error = parser->source->has_syntax_error;
    trial.suppress_diagnostics = true;
    parser->source->has_syntax_error = false;
    const bool is_type = r_parse_type(&trial, allow_unresolved_name) &&
                         !parser->source->has_syntax_error &&
                         (r_parser_at(&trial, R_TOKEN_COMMA) || r_parser_at_type_close(&trial));
    parser->source->has_syntax_error = had_error;
    if (parser->context->resource_status != R_FRONTEND_OK) {
        return false;
    }
    if (is_type) {
        trial.suppress_diagnostics = parser->suppress_diagnostics;
        *parser = trial;
        return true;
    }
    parser->source->cst_event_count = events;
    const size_t argument = r_parser_open(parser, R_SYNTAX_TYPE);
    const bool no_greater = parser->no_greater;
    parser->no_greater = true;
    const bool parsed = r_parse_expression_mode(parser, R_EXPRESSION_CONSTANT).parsed;
    parser->no_greater = no_greater;
    (void)r_parser_close(parser, argument);
    if (!parsed) {
        (void)r_parser_syntax_error(parser, "expected type or constant argument");
    }
    return parsed;
}

static bool r_parse_type_argument_list(RParser *parser,
                                       bool allow_unresolved_name,
                                       uint32_t minimum,
                                       uint32_t maximum) {
    uint32_t count = UINT32_C(0);

    if (!r_parser_expect(parser, R_TOKEN_LESS, "expected '<' after type constructor")) {
        return false;
    }
    if (!r_parser_at_type_close(parser)) {
        while (true) {
            if (!r_parse_generic_argument(parser, allow_unresolved_name)) {
                if (parser->context->resource_status != R_FRONTEND_OK) {
                    return false;
                }
                break;
            }
            count += 1U;
            /* A pending `>` of a split `>>` closes this list; a following ',' is outside it. */
            if ((parser->pending_greater != 0U) || !r_parser_eat(parser, R_TOKEN_COMMA)) {
                break;
            }
        }
    }
    if ((count < minimum) || (count > maximum)) {
        (void)r_parser_syntax_error(parser, "wrong number of type-constructor arguments");
    }
    return r_parser_expect_type_close(parser, "expected '>' after type arguments");
}

static bool r_parse_raw_function_type(RParser *parser) {
    (void)r_parser_expect(parser, R_TOKEN_KW_FN, "expected 'fn' after 'raw'");
    (void)r_parse_fn_resource_attributes(parser);
    (void)r_parser_eat(parser, R_TOKEN_QUESTION);
    (void)r_parser_expect(parser, R_TOKEN_LPAREN, "expected '(' in raw function type");
    if (!r_parser_at(parser, R_TOKEN_RPAREN)) {
        while (true) {
            if (r_parser_at(parser, R_TOKEN_KW_VOID)) {
                (void)r_parser_syntax_error(parser, "void is not a raw function parameter type");
            }
            if (!r_parse_type(parser, true)) {
                (void)r_parser_syntax_error(parser, "expected raw function parameter type");
                break;
            }
            if (!r_parser_eat(parser, R_TOKEN_COMMA)) {
                break;
            }
        }
    }
    (void)r_parser_expect(parser, R_TOKEN_RPAREN, "expected ')' in raw function type");
    (void)r_parser_expect(parser, R_TOKEN_ARROW, "expected '->' in raw function type");
    return r_parse_type(parser, true);
}

static bool r_parser_generic_parameter(const RParser *parser, const RToken *name) {
    size_t index = parser->generic_header_start;
    bool parameter = true;
    if (name == NULL) {
        return false;
    }
    while (index < parser->generic_header_end) {
        const RToken *token = &parser->source->tokens[index++];
        if (r_token_is_trivia(token->kind)) {
            continue;
        }
        if (parameter && token->kind == R_TOKEN_IDENTIFIER && token->intern_id == name->intern_id) {
            return true;
        }
        parameter = token->kind == R_TOKEN_COMMA;
    }
    return false;
}

static bool r_parser_applied_type_arguments(const RParser *parser, const RToken *head) {
    size_t index;
    size_t previous = r_skip_trivia(parser->source, parser->cursor);
    const RToken *name = head;
    bool known = false;
    if (r_parser_generic_parameter(parser, head) || r_parser_token_text_is(parser, head, "bytes")) {
        return false;
    }
    while (previous != 0U) {
        const RToken *token = &parser->source->tokens[--previous];
        if (!r_token_is_trivia(token->kind)) {
            name = token;
            break;
        }
    }
    for (index = 0U; index < parser->context->aggregate_count; ++index) {
        const RAggregateEntry *aggregate = &parser->context->aggregates[index];
        if (r_source_text_equal(parser->source, name->span, aggregate->name)) {
            known = true;
            if (aggregate->is_generic) {
                return true;
            }
        }
    }
    return !known;
}

static bool r_parse_type_atom(RParser *parser, bool allow_unresolved_name) {
    RTokenKind kind = r_parser_peek_kind(parser);

    if (kind == R_TOKEN_KW_SELF) {
        /* Self is validated against its trait or impl scope by the semantic pass (R-TYPE-0041);
           Self::Name selects an associated type (R-TYPE-0045). */
        if (!r_parser_bump(parser)) {
            return false;
        }
        if (r_parser_at(parser, R_TOKEN_COLON_COLON)) {
            /* R-TYPE-0045 (L16.3): Self::Name<T> applies an associated type with parameters. */
            return r_parse_qualified_tail(parser, true) &&
                   (!r_parser_at(parser, R_TOKEN_LESS) ||
                    r_parse_type_argument_list(parser, true, UINT32_C(1), UINT32_MAX));
        }
        return true;
    }
    if (r_token_is_primitive_type(kind) || (kind == R_TOKEN_KW_STR) || (kind == R_TOKEN_KW_VOID) ||
        (kind == R_TOKEN_KW_NEVER)) {
        return r_parser_bump(parser);
    }
    if (kind == R_TOKEN_IDENTIFIER) {
        const RToken *token = r_parser_peek_token(parser);
        bool standard_name = r_parser_token_text_is(parser, token, "std") ||
                             r_parser_token_text_is(parser, token, "core");
        bool std_name = r_parser_token_text_is(parser, token, "std");
        int standard_arity = standard_name ? r_standard_parametric_type_arity(parser, NULL) : -1;
        bool effectful_thread_handle = std_name && r_standard_thread_handle_type(parser);
        const bool library_generic =
            std_name && (standard_arity == 0) && r_visible_qualified_generic_aggregate(parser);
        if (std_name && !r_standard_keyword_type_is_allowed(parser, standard_arity)) {
            (void)r_parser_syntax_error(parser, "unknown closed standard-library type form");
        }
        if (!allow_unresolved_name && !r_parser_generic_parameter(parser, token) &&
            !r_is_visible_aggregate(
                parser->context, parser->source_id, token->span, token->span.start) &&
            !standard_name) {
            return false;
        }
        const bool parameter_name = r_parser_generic_parameter(parser, token);
        if (!r_parser_bump(parser)) {
            return false;
        }
        const size_t tail_start = parser->cursor;
        if (!r_parse_qualified_tail(parser, false)) {
            return false;
        }
        if (parameter_name && (parser->cursor != tail_start) && r_parser_at(parser, R_TOKEN_LESS)) {
            /* R-TYPE-0045 (L16.3): P::Name<T> applies an associated type with parameters. */
            return r_parse_type_argument_list(parser, true, UINT32_C(1), UINT32_MAX);
        }
        if ((standard_arity > 0) && r_parser_at(parser, R_TOKEN_LESS)) {
            uint32_t arity = (uint32_t)standard_arity;
            if (effectful_thread_handle) {
                return r_parse_effectful_type_argument(parser);
            }
            return r_parse_type_argument_list(parser, true, arity, arity);
        }
        if (!standard_name && r_parser_at(parser, R_TOKEN_LESS) &&
            r_parser_applied_type_arguments(parser, token)) {
            return r_parse_type_argument_list(parser, true, UINT32_C(1), UINT32_MAX);
        }
        if (library_generic && r_parser_at(parser, R_TOKEN_LESS)) {
            /* R-MOD-0002: a generic aggregate of a standard module written in R is applied
               like any other generic type. */
            return r_parse_type_argument_list(parser, true, UINT32_C(1), UINT32_MAX);
        }
        return true;
    }
    if (kind == R_TOKEN_LPAREN) {
        return r_parse_parenthesized_type(parser, allow_unresolved_name);
    }
    return false;
}

static bool r_token_is_r_integer_type(RTokenKind kind) {
    return (kind == R_TOKEN_KW_I8) || (kind == R_TOKEN_KW_U8) || (kind == R_TOKEN_KW_I16) ||
           (kind == R_TOKEN_KW_U16) || (kind == R_TOKEN_KW_I32) || (kind == R_TOKEN_KW_U32) ||
           (kind == R_TOKEN_KW_I64) || (kind == R_TOKEN_KW_U64) || (kind == R_TOKEN_KW_ISIZE) ||
           (kind == R_TOKEN_KW_USIZE);
}

static bool r_token_is_atomic_shorthand(RTokenKind kind) {
    return (kind == R_TOKEN_KW_AI8) || (kind == R_TOKEN_KW_AI16) || (kind == R_TOKEN_KW_AI32) ||
           (kind == R_TOKEN_KW_AI64) || (kind == R_TOKEN_KW_AISIZE) || (kind == R_TOKEN_KW_AU8) ||
           (kind == R_TOKEN_KW_AU16) || (kind == R_TOKEN_KW_AU32) || (kind == R_TOKEN_KW_AU64) ||
           (kind == R_TOKEN_KW_AUSIZE);
}

static bool r_token_is_c_integer_type(RTokenKind kind) {
    return (kind >= R_TOKEN_KW_C_CHAR) && (kind <= R_TOKEN_KW_C_UINTMAX) &&
           (kind != R_TOKEN_KW_C_BOOL);
}

static bool r_parse_pointer_pointee(RParser *parser) {
    RTokenKind kind = r_parser_peek_kind(parser);
    if ((kind == R_TOKEN_KW_ARRAY) || (kind == R_TOKEN_KW_LIST)) {
        (void)r_parser_bump(parser);
        return r_parse_type_argument_list(parser, true, UINT32_C(1), UINT32_C(1));
    }
    if (kind == R_TOKEN_KW_DICT) {
        (void)r_parser_bump(parser);
        return r_parse_type_argument_list(parser, true, UINT32_C(2), UINT32_C(2));
    }
    return r_parse_type_atom(parser, true);
}

static bool r_parse_trait_name(RParser *parser);
static bool r_parse_callable_mode(RParser *parser, bool lambda);
static bool r_parse_callable_throws(RParser *parser);

static bool r_parse_parameter_type(RParser *parser) {
    if (!r_parser_token_text_is(parser, r_parser_peek_token(parser), "out"))
        return r_parse_type(parser, true);
    const size_t node = r_parser_open(parser, R_SYNTAX_TYPE);
    (void)r_parser_bump(parser);
    const bool parsed = r_parse_type(parser, true);
    (void)r_parser_close(parser, node);
    return parsed;
}

/* R-TYPE-0054 (L28): `[async] fn [@attribute...] [mode] (P...) -> R` at the cursor names a
   function type. A lambda declaration names its result type and then its own name, so a
   parenthesized group after `fn` begins a function type only when `->` follows it. */
static bool r_function_type_ahead(const RParser *parser) {
    const RSource *source = parser->source;
    size_t index = r_skip_trivia(source, parser->cursor);
    size_t depth = 0U;
    if (r_scan_kind(source, index) == R_TOKEN_KW_ASYNC) {
        index = r_skip_trivia(source, index + 1U);
    }
    if (r_scan_kind(source, index) != R_TOKEN_KW_FN) {
        return false;
    }
    index = r_skip_trivia(source, index + 1U);
    while (r_scan_kind(source, index) == R_TOKEN_AT) {
        index = r_skip_trivia(source, r_skip_trivia(source, index + 1U) + 1U);
    }
    if (r_scan_kind(source, index) == R_TOKEN_IDENTIFIER) {
        const RToken *mode = r_scan_token(source, index);
        if ((mode != NULL) && (r_parser_token_text_is(parser, mode, "shared") ||
                               r_parser_token_text_is(parser, mode, "mut") ||
                               r_parser_token_text_is(parser, mode, "once"))) {
            index = r_skip_trivia(source, index + 1U);
        }
    }
    if (r_scan_kind(source, index) != R_TOKEN_LPAREN) {
        return false;
    }
    do {
        const RTokenKind kind = r_scan_kind(source, index);
        if ((kind == R_TOKEN_EOF) || (kind == R_TOKEN_LBRACE) || (kind == R_TOKEN_SEMICOLON)) {
            return false;
        }
        if (kind == R_TOKEN_LPAREN) {
            depth += 1U;
        } else if (kind == R_TOKEN_RPAREN) {
            depth -= 1U;
        }
        index = r_skip_trivia(source, index + 1U);
    } while (depth != 0U);
    return r_scan_kind(source, index) == R_TOKEN_ARROW;
}

static bool r_parse_callable_constraint(RParser *parser) {
    /* R-TYPE-0044: callable-constraint = "fn", "(", [types], ")", "->", type */
    size_t callable = r_parser_open(parser, R_SYNTAX_CALLABLE_CONSTRAINT);
    (void)r_parser_eat(parser, R_TOKEN_KW_ASYNC);
    (void)r_parser_expect(parser, R_TOKEN_KW_FN, "expected fn in callable constraint");
    (void)r_parse_fn_resource_attributes(parser);
    (void)r_parse_callable_mode(parser, false);
    (void)r_parser_expect(parser, R_TOKEN_LPAREN, "expected '(' after fn");
    if (!r_parser_at(parser, R_TOKEN_RPAREN)) {
        do {
            if (!r_parse_parameter_type(parser)) {
                (void)r_parser_syntax_error(parser, "expected callable parameter type");
                break;
            }
            /* R-TYPE-0053 (L18.3): `fn(P...)` takes the elements of a pack. */
            if (r_parser_eat(parser, R_TOKEN_ELLIPSIS)) {
                break;
            }
        } while (r_parser_eat(parser, R_TOKEN_COMMA));
    }
    (void)r_parser_expect(parser, R_TOKEN_RPAREN, "expected ')' after callable parameters");
    (void)r_parser_expect(parser, R_TOKEN_ARROW, "expected '->' before the callable result type");
    if (!r_parse_type(parser, true)) {
        (void)r_parser_syntax_error(parser, "expected callable result type");
    }
    (void)r_parse_callable_throws(parser);
    (void)r_parser_close(parser, callable);
    return !parser->source->has_syntax_error;
}

/* opaque-result = "opaque", "(", contract, { "&", contract }, ")" (R-TYPE-0048);
   dyn-type = "dyn", "(", contract, { "&", contract }, ")" (R-TYPE-0051). */
static bool r_parse_opaque_result(RParser *parser) {
    const bool dyn = r_parser_at(parser, R_TOKEN_KW_DYN);
    size_t node = r_parser_open(parser, dyn ? R_SYNTAX_DYN_TYPE : R_SYNTAX_OPAQUE_RESULT);
    (void)r_parser_bump(parser);
    (void)r_parser_expect(
        parser, R_TOKEN_LPAREN, dyn ? "expected ( after dyn" : "expected ( after opaque");
    do {
        if (r_parser_at(parser, R_TOKEN_IDENTIFIER) &&
            r_parser_peek_n_kind(parser, 1U) == R_TOKEN_EQUAL) {
            size_t binding = r_parser_open(parser, R_SYNTAX_ASSOCIATED_TYPE);
            (void)r_parser_bump(parser);
            (void)r_parser_bump(parser);
            if (!r_parse_type(parser, true))
                (void)r_parser_syntax_error(parser, "expected associated result type");
            (void)r_parser_close(parser, binding);
        } else if (r_parser_at(parser, R_TOKEN_KW_FN) || r_parser_at(parser, R_TOKEN_KW_ASYNC))
            (void)r_parse_callable_constraint(parser);
        else
            (void)r_parse_trait_name(parser);
    } while (r_parser_eat(parser, R_TOKEN_AMP));
    (void)r_parser_expect(parser,
                          R_TOKEN_RPAREN,
                          dyn ? "expected ) after dyn contracts"
                              : "expected ) after opaque result contracts");
    return r_parser_close(parser, node);
}

static bool r_parse_type(RParser *parser, bool allow_unresolved_name) {
    size_t node;
    RTokenKind kind;
    bool parsed = true;
    bool pointer_pointee = false;
    bool sequence_element = false;
    bool saw_slice_suffix = false;
    bool saw_array_suffix = false;
    bool has_const;

    if (!r_parser_enter_nesting(parser)) {
        return false;
    }
    node = r_parser_open(parser, R_SYNTAX_TYPE);

    has_const = r_parser_eat(parser, R_TOKEN_KW_CONST);
    kind = r_parser_peek_kind(parser);

    if (has_const &&
        ((kind == R_TOKEN_KW_ARC) || (kind == R_TOKEN_KW_RC) || (kind == R_TOKEN_KW_WEAK) ||
         (kind == R_TOKEN_KW_OWN) || (kind == R_TOKEN_KW_RAW) || (kind == R_TOKEN_KW_ATOMIC) ||
         r_token_is_atomic_shorthand(kind))) {
        (void)r_parser_syntax_error(parser, "const cannot qualify this type constructor");
    }

    if (kind == R_TOKEN_KW_OPAQUE) {
        parsed = r_parse_opaque_result(parser);
    } else if (((kind == R_TOKEN_KW_FN) || (kind == R_TOKEN_KW_ASYNC)) &&
               r_function_type_ahead(parser)) {
        /* R-TYPE-0054 (L28): a function type takes no suffix; `(fn(i32) -> i32)[]` groups. */
        parsed = r_parse_callable_constraint(parser);
        parsed = r_parser_close(parser, node) && parsed;
        r_parser_leave_nesting(parser);
        return parsed;
    } else if (kind == R_TOKEN_KW_DYN) {
        /* R-TYPE-0051: an interface is named only behind a borrow. */
        parsed = r_parse_opaque_result(parser);
        pointer_pointee = parsed;
    } else if ((kind == R_TOKEN_KW_O) || (kind == R_TOKEN_KW_ARRAY) || (kind == R_TOKEN_KW_LIST)) {
        (void)r_parser_bump(parser);
        parsed = r_parse_type_argument_list(parser, true, UINT32_C(1), UINT32_C(1));
        pointer_pointee = (kind == R_TOKEN_KW_ARRAY) || (kind == R_TOKEN_KW_LIST);
    } else if (kind == R_TOKEN_KW_TASK) {
        parsed = r_parse_task_type(parser);
    } else if (kind == R_TOKEN_KW_DICT) {
        (void)r_parser_bump(parser);
        parsed = r_parse_type_argument_list(parser, true, UINT32_C(2), UINT32_C(2));
        pointer_pointee = true;
    } else if ((kind == R_TOKEN_KW_ARC) || (kind == R_TOKEN_KW_RC)) {
        (void)r_parser_bump(parser);
        /* R-TYPE-0055 (L29): `arc dyn(C)` and `rc dyn(C)` own a member of an interface. */
        parsed = r_parser_at(parser, R_TOKEN_KW_DYN) ? r_parse_opaque_result(parser)
                                                     : r_parse_type_atom(parser, true);
    } else if (kind == R_TOKEN_KW_WEAK) {
        (void)r_parser_bump(parser);
        if (!r_parser_at(parser, R_TOKEN_KW_ARC) && !r_parser_at(parser, R_TOKEN_KW_RC)) {
            (void)r_parser_syntax_error(parser, "expected arc or rc after weak");
            parsed = false;
        } else {
            (void)r_parser_bump(parser);
            parsed = r_parser_at(parser, R_TOKEN_KW_DYN) ? r_parse_opaque_result(parser)
                                                         : r_parse_type_atom(parser, true);
        }
    } else if (kind == R_TOKEN_KW_OWN) {
        (void)r_parser_bump(parser);
        /* R-TYPE-0055 (L29): `own dyn(C)*` owns a member of an interface. */
        parsed = r_parser_at(parser, R_TOKEN_KW_DYN) ? r_parse_opaque_result(parser)
                                                     : r_parse_pointer_pointee(parser);
        (void)r_parser_expect(parser, R_TOKEN_STAR, "expected '*' in owning pointer type");
        (void)r_parser_eat(parser, R_TOKEN_QUESTION);
    } else if (kind == R_TOKEN_KW_RAW) {
        (void)r_parser_bump(parser);
        if (r_parser_at(parser, R_TOKEN_KW_FN)) {
            parsed = r_parse_raw_function_type(parser);
        } else {
            (void)r_parser_eat(parser, R_TOKEN_KW_CONST);
            parsed = r_parse_pointer_pointee(parser);
            (void)r_parser_expect(parser, R_TOKEN_STAR, "expected '*' in raw pointer type");
            (void)r_parser_eat(parser, R_TOKEN_QUESTION);
        }
    } else if (kind == R_TOKEN_KW_ATOMIC) {
        (void)r_parser_bump(parser);
        if (r_parser_at(parser, R_TOKEN_KW_RAW)) {
            (void)r_parser_bump(parser);
            (void)r_parser_eat(parser, R_TOKEN_KW_CONST);
            parsed = r_parse_pointer_pointee(parser);
            (void)r_parser_expect(parser, R_TOKEN_STAR, "expected '*' in atomic raw pointer type");
            (void)r_parser_expect(
                parser, R_TOKEN_QUESTION, "atomic raw pointer type must be nullable");
        } else if (r_token_is_r_integer_type(r_parser_peek_kind(parser)) ||
                   r_parser_at(parser, R_TOKEN_KW_BOOL)) {
            parsed = r_parser_bump(parser);
        } else {
            parsed = false;
        }
    } else if (r_token_is_atomic_shorthand(kind)) {
        parsed = r_parser_bump(parser);
    } else if (kind == R_TOKEN_KW_CONSTEXPR) {
        (void)r_parser_bump(parser);
        parsed = r_parser_expect(parser, R_TOKEN_KW_STR, "expected str after constexpr");
    } else if ((kind == R_TOKEN_IDENTIFIER) &&
               r_parser_token_text_is(parser, r_parser_peek_token(parser), "bytes")) {
        parsed = r_parser_bump(parser);
        pointer_pointee = parsed;
        sequence_element = parsed;
    } else {
        /* sequence-element = type-atom: a standard-library type name, like any aggregate type
           name, takes array and slice suffixes (R-GRAM-0005). */
        parsed = r_parse_type_atom(parser, allow_unresolved_name);
        pointer_pointee = parsed;
        sequence_element = parsed;
    }

    if (parser->pending_greater != 0U) {
        pointer_pointee = false;
        sequence_element = false;
    }
    if (parsed && pointer_pointee && r_parser_eat(parser, R_TOKEN_STAR)) {
        (void)r_parser_eat(parser, R_TOKEN_QUESTION);
        sequence_element = false;
    }

    while (parsed && sequence_element && r_parser_eat(parser, R_TOKEN_LBRACKET)) {
        if (saw_slice_suffix) {
            (void)r_parser_syntax_error(parser, "slice type cannot have another array suffix");
        }
        if (r_parser_at(parser, R_TOKEN_RBRACKET)) {
            /* array-type and slice-type share one sequence element: a slice of arrays spells its
               element in parentheses, (T[N])[]. */
            if (saw_array_suffix) {
                (void)r_parser_syntax_error(
                    parser, "a slice suffix cannot follow an array suffix; write (T[N])[]");
            }
            saw_slice_suffix = true;
        } else {
            saw_array_suffix = true;
            RExpressionInfo bound = r_parse_expression_mode(parser, R_EXPRESSION_CONSTANT);
            if (!bound.parsed) {
                (void)r_parser_syntax_error(parser, "expected constant array bound");
            }
        }
        (void)r_parser_expect(parser, R_TOKEN_RBRACKET, "expected ']' in array or slice type");
    }
    parsed = r_parser_close(parser, node) && parsed;
    r_parser_leave_nesting(parser);
    return parsed;
}

static bool r_scan_qualified_aggregate_name(const RParser *parser,
                                            size_t *module_start,
                                            size_t *colon_index,
                                            size_t *name_index,
                                            size_t *tail_index) {
    size_t index = r_skip_trivia(parser->source, parser->cursor);
    size_t start = index;
    if (r_scan_kind(parser->source, index) != R_TOKEN_IDENTIFIER) {
        return false;
    }
    index = r_skip_trivia(parser->source, index + 1U);
    while (r_scan_kind(parser->source, index) == R_TOKEN_DOT) {
        index = r_skip_trivia(parser->source, index + 1U);
        if (r_scan_kind(parser->source, index) != R_TOKEN_IDENTIFIER) {
            return false;
        }
        index = r_skip_trivia(parser->source, index + 1U);
    }
    if (r_scan_kind(parser->source, index) != R_TOKEN_COLON_COLON) {
        return false;
    }
    if (module_start != NULL) {
        *module_start = start;
    }
    if (colon_index != NULL) {
        *colon_index = r_skip_trivia(parser->source, index);
    }
    index = r_skip_trivia(parser->source, index + 1U);
    if (r_scan_kind(parser->source, index) != R_TOKEN_IDENTIFIER) {
        return false;
    }
    if (name_index != NULL) {
        *name_index = r_skip_trivia(parser->source, index);
    }
    if (tail_index != NULL) {
        *tail_index = r_skip_trivia(parser->source, index + 1U);
    }
    return true;
}

static bool r_module_tokens_equal(const RParser *parser,
                                  size_t module_start,
                                  size_t colon_index,
                                  const char *module_name) {
    size_t source_index = r_skip_trivia(parser->source, module_start);
    size_t name_offset = 0U;
    size_t module_length = strlen(module_name);
    while (source_index < colon_index) {
        const RToken *component = r_scan_token(parser->source, source_index);
        size_t component_length;
        if ((component == NULL) || (component->kind != R_TOKEN_IDENTIFIER)) {
            return false;
        }
        component_length = (size_t)(component->span.end - component->span.start);
        if ((name_offset > module_length) || (component_length > (module_length - name_offset)) ||
            (strncmp(module_name + name_offset,
                     (const char *)(parser->source->bytes + component->span.start),
                     component_length) != 0) ||
            (module_name[name_offset + component_length] != '\0' &&
             module_name[name_offset + component_length] != '.')) {
            return false;
        }
        name_offset += component_length;
        source_index = r_skip_trivia(parser->source, source_index + 1U);
        if (source_index >= colon_index) {
            break;
        }
        if ((r_scan_kind(parser->source, source_index) != R_TOKEN_DOT) ||
            (module_name[name_offset] != '.')) {
            return false;
        }
        name_offset += 1U;
        source_index = r_skip_trivia(parser->source, source_index + 1U);
    }
    return module_name[name_offset] == '\0';
}

static bool r_visible_qualified_aggregate(const RParser *parser) {
    size_t module_start;
    size_t colon_index;
    size_t name_index;
    const RToken *name;
    size_t aggregate_index;
    if (!r_scan_qualified_aggregate_name(parser, &module_start, &colon_index, &name_index, NULL)) {
        return false;
    }
    name = r_scan_token(parser->source, name_index);
    if (name == NULL) {
        return false;
    }
    for (aggregate_index = 0U; aggregate_index < parser->context->aggregate_count;
         ++aggregate_index) {
        const RAggregateEntry *aggregate = &parser->context->aggregates[aggregate_index];
        if (!r_source_text_equal(parser->source, name->span, aggregate->name) ||
            !r_module_tokens_equal(parser, module_start, colon_index, aggregate->module_name)) {
            continue;
        }
        if ((aggregate->source == parser->source_id) &&
            (aggregate->is_error || (aggregate->declaration_offset <= name->span.start))) {
            return true;
        }
        if (!aggregate->is_protected) {
            size_t import_index;
            for (import_index = 0U; import_index < parser->context->import_count; ++import_index) {
                const RImportEntry *import = &parser->context->imports[import_index];
                if ((import->source == parser->source_id) && !import->imports_name &&
                    (strcmp(import->module_name, aggregate->module_name) == 0)) {
                    return true;
                }
            }
        }
    }
    return false;
}

/* A visible qualified aggregate that was declared with a generic header (R-MOD-0002: the
   generic aggregates of standard modules written in R take type arguments like any other). */
static bool r_visible_qualified_generic_aggregate(const RParser *parser) {
    size_t module_start;
    size_t colon_index;
    size_t name_index;
    const RToken *name;
    size_t aggregate_index;
    if (!r_scan_qualified_aggregate_name(parser, &module_start, &colon_index, &name_index, NULL)) {
        return false;
    }
    name = r_scan_token(parser->source, name_index);
    if (name == NULL) {
        return false;
    }
    for (aggregate_index = 0U; aggregate_index < parser->context->aggregate_count;
         ++aggregate_index) {
        const RAggregateEntry *aggregate = &parser->context->aggregates[aggregate_index];
        if (aggregate->is_generic &&
            r_source_text_equal(parser->source, name->span, aggregate->name) &&
            r_module_tokens_equal(parser, module_start, colon_index, aggregate->module_name)) {
            return true;
        }
    }
    return false;
}

static bool r_named_statement_starts_declaration(const RParser *parser, const char *name) {
    size_t index = r_skip_trivia(parser->source, parser->cursor);
    const RToken *token;

    if ((r_scan_kind(parser->source, index) == R_TOKEN_KW_STATIC) ||
        (r_scan_kind(parser->source, index) == R_TOKEN_KW_THREAD_LOCAL)) {
        index = r_skip_trivia(parser->source, index + 1U);
    }
    token = r_scan_token(parser->source, index);
    if ((token == NULL) || (token->kind != R_TOKEN_IDENTIFIER) ||
        !r_parser_token_text_is(parser, token, name)) {
        return false;
    }
    index = r_skip_trivia(parser->source, index + 1U);
    if (r_scan_kind(parser->source, index) == R_TOKEN_STAR) {
        index = r_skip_trivia(parser->source, index + 1U);
        if (r_scan_kind(parser->source, index) == R_TOKEN_QUESTION) {
            index = r_skip_trivia(parser->source, index + 1U);
        }
    } else {
        while (r_scan_kind(parser->source, index) == R_TOKEN_LBRACKET) {
            uint32_t depth = UINT32_C(1);

            index = r_skip_trivia(parser->source, index + 1U);
            while ((depth != UINT32_C(0)) && (r_scan_kind(parser->source, index) != R_TOKEN_EOF)) {
                const RTokenKind kind = r_scan_kind(parser->source, index);

                if (kind == R_TOKEN_LBRACKET) {
                    depth += UINT32_C(1);
                } else if (kind == R_TOKEN_RBRACKET) {
                    depth -= UINT32_C(1);
                }
                index = r_skip_trivia(parser->source, index + 1U);
            }
            if (depth != UINT32_C(0)) {
                return false;
            }
        }
    }
    if (r_scan_kind(parser->source, index) != R_TOKEN_IDENTIFIER) {
        return false;
    }
    index = r_skip_trivia(parser->source, index + 1U);
    return r_scan_kind(parser->source, index) == R_TOKEN_EQUAL;
}

static bool r_parenthesized_statement_starts_declaration(const RParser *parser) {
    size_t index = r_skip_trivia(parser->source, parser->cursor);
    uint32_t depth = UINT32_C(0);

    if ((r_scan_kind(parser->source, index) == R_TOKEN_KW_STATIC) ||
        (r_scan_kind(parser->source, index) == R_TOKEN_KW_THREAD_LOCAL)) {
        index = r_skip_trivia(parser->source, index + 1U);
    }
    if (r_scan_kind(parser->source, index) != R_TOKEN_LPAREN) {
        return false;
    }
    do {
        const RTokenKind kind = r_scan_kind(parser->source, index);

        if (kind == R_TOKEN_LPAREN) {
            if (depth == UINT32_MAX) {
                return false;
            }
            depth += UINT32_C(1);
        } else if (kind == R_TOKEN_RPAREN) {
            depth -= UINT32_C(1);
        } else if (kind == R_TOKEN_EOF) {
            return false;
        }
        index = r_skip_trivia(parser->source, index + 1U);
    } while (depth != UINT32_C(0));

    if (r_scan_kind(parser->source, index) == R_TOKEN_STAR) {
        index = r_skip_trivia(parser->source, index + 1U);
        if (r_scan_kind(parser->source, index) == R_TOKEN_QUESTION) {
            index = r_skip_trivia(parser->source, index + 1U);
        }
    } else {
        while (r_scan_kind(parser->source, index) == R_TOKEN_LBRACKET) {
            uint32_t bracket_depth = UINT32_C(1);

            index = r_skip_trivia(parser->source, index + 1U);
            while ((bracket_depth != UINT32_C(0)) &&
                   (r_scan_kind(parser->source, index) != R_TOKEN_EOF)) {
                const RTokenKind kind = r_scan_kind(parser->source, index);

                if (kind == R_TOKEN_LBRACKET) {
                    if (bracket_depth == UINT32_MAX) {
                        return false;
                    }
                    bracket_depth += UINT32_C(1);
                } else if (kind == R_TOKEN_RBRACKET) {
                    bracket_depth -= UINT32_C(1);
                }
                index = r_skip_trivia(parser->source, index + 1U);
            }
            if (bracket_depth != UINT32_C(0)) {
                return false;
            }
        }
    }
    if (r_scan_kind(parser->source, index) != R_TOKEN_IDENTIFIER) {
        return false;
    }
    index = r_skip_trivia(parser->source, index + 1U);
    return r_scan_kind(parser->source, index) == R_TOKEN_EQUAL;
}

/* The token index after the parenthesized group starting at `index`, or zero when the group
   is unbalanced or nested beyond the limit. */
static size_t r_skip_balanced_parentheses(const RParser *parser, size_t index) {
    uint32_t depth = 0U;

    if (r_scan_kind(parser->source, index) != R_TOKEN_LPAREN) {
        return 0U;
    }
    do {
        const RTokenKind kind = r_scan_kind(parser->source, index);
        if (kind == R_TOKEN_EOF) {
            return 0U;
        }
        if (kind == R_TOKEN_LPAREN) {
            depth += 1U;
            if (depth > parser->context->options.limits.max_nesting) {
                return 0U;
            }
        }
        if (kind == R_TOKEN_RPAREN) {
            depth -= 1U;
        }
        index = r_skip_trivia(parser->source, index + 1U);
    } while (depth != 0U);
    return index;
}

static bool r_known_statement_type_start(const RParser *parser) {
    RParser type_parser = *parser;
    size_t type_index = r_skip_trivia(parser->source, parser->cursor);
    RTokenKind kind;
    const RToken *token;

    kind = r_scan_kind(parser->source, type_index);
    if ((kind == R_TOKEN_KW_STATIC) || (kind == R_TOKEN_KW_THREAD_LOCAL)) {
        type_index = r_skip_trivia(parser->source, type_index + 1U);
        kind = r_scan_kind(parser->source, type_index);
    }
    type_parser.cursor = type_index;
    if (kind == R_TOKEN_LPAREN) {
        return r_parenthesized_statement_starts_declaration(parser);
    }
    if (r_token_is_primitive_type(kind) || (kind == R_TOKEN_KW_CONST) ||
        (kind == R_TOKEN_KW_CONSTEXPR) || (kind == R_TOKEN_KW_STR) || (kind == R_TOKEN_KW_NEVER) ||
        (kind == R_TOKEN_KW_O) || (kind == R_TOKEN_KW_ARRAY) || (kind == R_TOKEN_KW_LIST) ||
        (kind == R_TOKEN_KW_DICT) || (kind == R_TOKEN_KW_TASK) || (kind == R_TOKEN_KW_ARC) ||
        (kind == R_TOKEN_KW_RC) || (kind == R_TOKEN_KW_WEAK) || (kind == R_TOKEN_KW_OWN) ||
        (kind == R_TOKEN_KW_RAW) || (kind == R_TOKEN_KW_ATOMIC) || (kind == R_TOKEN_KW_SELF) ||
        (kind == R_TOKEN_KW_AUTO) || (kind == R_TOKEN_KW_OPAQUE) || (kind == R_TOKEN_KW_DYN) ||
        r_token_is_atomic_shorthand(kind)) {
        return true;
    }
    if (kind != R_TOKEN_IDENTIFIER) {
        return false;
    }
    token = r_scan_token(parser->source, type_index);
    if (r_parser_token_text_is(parser, token, "bytes")) {
        return r_named_statement_starts_declaration(parser, "bytes");
    }
    if (r_parser_token_text_is(parser, token, "error") &&
        r_is_visible_aggregate(
            parser->context, parser->source_id, token->span, token->span.start)) {
        return r_named_statement_starts_declaration(parser, "error");
    }
    if (r_parser_token_text_is(parser, token, "std") ||
        r_parser_token_text_is(parser, token, "core")) {
        size_t tail_index = 0U;
        int standard_arity = r_standard_parametric_type_arity(&type_parser, &tail_index);
        RTokenKind tail_kind;
        if (standard_arity < 0) {
            return false;
        }
        if ((standard_arity == 0) && (tail_index != 0U) &&
            (r_scan_kind(parser->source, tail_index) == R_TOKEN_LESS) &&
            r_visible_qualified_generic_aggregate(&type_parser)) {
            /* R-MOD-0002: `std.m::Name<args> name` applies a generic aggregate of a standard
               module written in R. */
            tail_index = r_skip_balanced_angles(parser, tail_index);
            tail_kind = r_scan_kind(parser->source, tail_index);
            return (tail_index != 0U) &&
                   ((tail_kind == R_TOKEN_IDENTIFIER) || (tail_kind == R_TOKEN_STAR) ||
                    (tail_kind == R_TOKEN_LBRACKET));
        }
        tail_kind = r_scan_kind(parser->source, tail_index);
        return (standard_arity > 0) || (tail_kind == R_TOKEN_IDENTIFIER) ||
               (tail_kind == R_TOKEN_STAR) || (tail_kind == R_TOKEN_LBRACKET);
    }
    if (r_parser_generic_parameter(&type_parser, token)) {
        return true;
    }
    {
        size_t index = r_skip_trivia(parser->source, type_index + 1U);
        if (r_scan_kind(parser->source, index) == R_TOKEN_COLON_COLON) {
            index = r_skip_trivia(parser->source, index + 1U);
            if (r_scan_kind(parser->source, index) == R_TOKEN_IDENTIFIER) {
                index = r_skip_trivia(parser->source, index + 1U);
                if (r_scan_kind(parser->source, index) == R_TOKEN_LPAREN) {
                    const size_t tail = r_skip_balanced_parentheses(parser, index);
                    if (tail != 0U && r_scan_kind(parser->source, tail) == R_TOKEN_SEMICOLON) {
                        return false;
                    }
                }
            }
        }
    }
    if (r_visible_qualified_aggregate(&type_parser)) {
        return true;
    }
    return r_is_visible_aggregate(
        parser->context, parser->source_id, token->span, token->span.start);
}

static bool r_looks_like_unresolved_declaration(const RParser *parser) {
    size_t index = r_skip_trivia(parser->source, parser->cursor);
    const RToken *first;
    size_t qualified_tail;

    if (index >= parser->source->token_count) {
        return false;
    }
    first = &parser->source->tokens[index];
    if (first->kind != R_TOKEN_IDENTIFIER) {
        return false;
    }
    if (r_scan_qualified_aggregate_name(parser, NULL, NULL, NULL, &qualified_tail)) {
        index = qualified_tail;
    } else {
        if (r_is_visible_aggregate(
                parser->context, parser->source_id, first->span, first->span.start)) {
            return false;
        }
        index = r_skip_trivia(parser->source, index + 1U);
    }
    if ((index < parser->source->token_count) &&
        (parser->source->tokens[index].kind == R_TOKEN_STAR)) {
        index = r_skip_trivia(parser->source, index + 1U);
        if ((index < parser->source->token_count) &&
            (parser->source->tokens[index].kind == R_TOKEN_QUESTION)) {
            index = r_skip_trivia(parser->source, index + 1U);
        }
    }
    if ((index >= parser->source->token_count) ||
        (parser->source->tokens[index].kind != R_TOKEN_IDENTIFIER)) {
        return false;
    }
    index = r_skip_trivia(parser->source, index + 1U);
    return (index < parser->source->token_count) &&
           (parser->source->tokens[index].kind == R_TOKEN_EQUAL);
}

static bool r_is_literal_token(RTokenKind kind) {
    return (kind == R_TOKEN_INTEGER_LITERAL) || (kind == R_TOKEN_FLOAT_LITERAL) ||
           (kind == R_TOKEN_CHARACTER_LITERAL) || (kind == R_TOKEN_STRING_LITERAL);
}

static bool r_is_assignment_operator(RTokenKind kind) {
    return (kind == R_TOKEN_EQUAL) || (kind == R_TOKEN_PLUS_EQUAL) ||
           (kind == R_TOKEN_MINUS_EQUAL) || (kind == R_TOKEN_STAR_EQUAL) ||
           (kind == R_TOKEN_SLASH_EQUAL) || (kind == R_TOKEN_PERCENT_EQUAL) ||
           (kind == R_TOKEN_AMP_EQUAL) || (kind == R_TOKEN_PIPE_EQUAL) ||
           (kind == R_TOKEN_CARET_EQUAL) || (kind == R_TOKEN_LESS_LESS_EQUAL) ||
           (kind == R_TOKEN_GREATER_GREATER_EQUAL);
}

static bool r_is_comparison_operator(RTokenKind kind) {
    return (kind == R_TOKEN_EQUAL_EQUAL) || (kind == R_TOKEN_BANG_EQUAL) ||
           (kind == R_TOKEN_LESS) || (kind == R_TOKEN_LESS_EQUAL) || (kind == R_TOKEN_GREATER) ||
           (kind == R_TOKEN_GREATER_EQUAL);
}

static bool r_binary_binding_power(RTokenKind kind,
                                   uint32_t *left_power,
                                   uint32_t *right_power,
                                   bool *condition_result) {
    uint32_t power;
    bool result = false;

    switch (kind) {
    case R_TOKEN_PIPE_PIPE:
        power = UINT32_C(3);
        result = true;
        break;
    case R_TOKEN_AMP_AMP:
        power = UINT32_C(4);
        result = true;
        break;
    case R_TOKEN_PIPE:
        power = UINT32_C(5);
        break;
    case R_TOKEN_CARET:
        power = UINT32_C(6);
        break;
    case R_TOKEN_AMP:
        power = UINT32_C(7);
        break;
    case R_TOKEN_EQUAL_EQUAL:
    case R_TOKEN_BANG_EQUAL:
        power = UINT32_C(8);
        result = true;
        break;
    case R_TOKEN_LESS:
    case R_TOKEN_LESS_EQUAL:
    case R_TOKEN_GREATER:
    case R_TOKEN_GREATER_EQUAL:
        power = UINT32_C(9);
        result = true;
        break;
    case R_TOKEN_LESS_LESS:
    case R_TOKEN_GREATER_GREATER:
        power = UINT32_C(10);
        break;
    case R_TOKEN_PLUS:
    case R_TOKEN_MINUS:
        power = UINT32_C(11);
        break;
    case R_TOKEN_STAR:
    case R_TOKEN_SLASH:
    case R_TOKEN_PERCENT:
        power = UINT32_C(12);
        break;
    default:
        return false;
    }
    *left_power = power;
    *right_power = power + 1U;
    *condition_result = result;
    return true;
}

static bool r_expression_operator_allowed(RTokenKind kind, bool condition_value) {
    if (!condition_value) {
        return true;
    }
    return (kind == R_TOKEN_PIPE) || (kind == R_TOKEN_CARET) || (kind == R_TOKEN_AMP) ||
           (kind == R_TOKEN_LESS_LESS) || (kind == R_TOKEN_GREATER_GREATER) ||
           (kind == R_TOKEN_PLUS) || (kind == R_TOKEN_MINUS) || (kind == R_TOKEN_STAR) ||
           (kind == R_TOKEN_SLASH) || (kind == R_TOKEN_PERCENT);
}

static bool r_qualified_name_ahead(const RParser *parser) {
    size_t index = r_skip_trivia(parser->source, parser->cursor);
    RTokenKind kind;
    bool saw_colon_colon = false;

    kind = r_scan_kind(parser->source, index);
    if (!r_is_qualified_component(kind)) {
        return false;
    }
    index = r_skip_trivia(parser->source, index) + 1U;
    while (true) {
        kind = r_scan_kind(parser->source, index);
        if (((kind != R_TOKEN_DOT) && (kind != R_TOKEN_COLON_COLON)) ||
            r_generic_arguments_at(parser, index)) {
            break;
        }
        if (kind == R_TOKEN_COLON_COLON) {
            saw_colon_colon = true;
        }
        index = r_skip_trivia(parser->source, index) + 1U;
        if (!r_is_qualified_component(r_scan_kind(parser->source, index))) {
            return false;
        }
        index = r_skip_trivia(parser->source, index) + 1U;
    }
    return saw_colon_colon;
}

static bool r_scan_identifier_text(const RParser *parser, size_t *index, const char *text) {
    const RToken *token;
    *index = r_skip_trivia(parser->source, *index);
    if (*index >= parser->source->token_count) {
        return false;
    }
    token = &parser->source->tokens[*index];
    if ((token->kind != R_TOKEN_IDENTIFIER) || !r_parser_token_text_is(parser, token, text)) {
        return false;
    }
    *index += 1U;
    return true;
}

typedef enum RStandardTypeCallKind {
    R_STANDARD_TYPE_CALL_NONE = 0,
    R_STANDARD_TYPE_CALL_ONE_TYPE,
    R_STANDARD_TYPE_CALL_ONE_TYPE_ONE_VALUE,
    R_STANDARD_TYPE_CALL_TWO_TYPES,
    R_STANDARD_TYPE_CALL_TWO_TYPES_ONE_VALUE,
    /* R-REFL-0001..0003: reflection-constant-expression forms, valid where call-free
       expressions are; the type-and-value forms of enum_at and enum_from_name are calls. */
    R_STANDARD_TYPE_CALL_REFLECTION_TYPE,
    R_STANDARD_TYPE_CALL_REFLECTION_TYPE_CONSTANT,
    R_STANDARD_TYPE_CALL_REFLECTION_TYPE_VALUE,
    /* R-REFL-0005 (L42): an attribute type and the type it reads, without or with a value. */
    R_STANDARD_TYPE_CALL_REFLECTION_TWO_TYPES,
    R_STANDARD_TYPE_CALL_REFLECTION_TWO_TYPES_VALUE
} RStandardTypeCallKind;

static bool r_reflection_type_call_kind(const RParser *parser,
                                        const RToken *operation,
                                        RStandardTypeCallKind *kind) {
    static const char *const constant_forms[] = {"enum_count",
                                                 "enum_min",
                                                 "enum_max",
                                                 "enum_variants",
                                                 "variant_count",
                                                 "field_count",
                                                 "type_name"};
    size_t index;

    for (index = 0U; index < sizeof(constant_forms) / sizeof(constant_forms[0]); ++index) {
        if (r_parser_token_text_is(parser, operation, constant_forms[index])) {
            *kind = R_STANDARD_TYPE_CALL_REFLECTION_TYPE;
            return true;
        }
    }
    if (r_parser_token_text_is(parser, operation, "field_name")) {
        *kind = R_STANDARD_TYPE_CALL_REFLECTION_TYPE_CONSTANT;
        return true;
    }
    if (r_parser_token_text_is(parser, operation, "enum_at") ||
        r_parser_token_text_is(parser, operation, "enum_from_name")) {
        *kind = R_STANDARD_TYPE_CALL_REFLECTION_TYPE_VALUE;
        return true;
    }
    if (r_parser_token_text_is(parser, operation, "type_attribute")) {
        *kind = R_STANDARD_TYPE_CALL_REFLECTION_TWO_TYPES;
        return true;
    }
    if (r_parser_token_text_is(parser, operation, "field_attribute") ||
        r_parser_token_text_is(parser, operation, "variant_attribute")) {
        *kind = R_STANDARD_TYPE_CALL_REFLECTION_TWO_TYPES_VALUE;
        return true;
    }
    return false;
}

/* `core`, "::", reflection-operation, "(" ahead: the closed type-operand reflection forms. */
static RStandardTypeCallKind r_reflection_type_call_ahead(const RParser *parser) {
    size_t index = parser->cursor;
    const RToken *operation;
    RStandardTypeCallKind kind = R_STANDARD_TYPE_CALL_NONE;

    if (!r_scan_identifier_text(parser, &index, "core") ||
        (r_scan_kind(parser->source, index) != R_TOKEN_COLON_COLON)) {
        return R_STANDARD_TYPE_CALL_NONE;
    }
    index = r_skip_trivia(parser->source, index) + 1U;
    operation = r_scan_token(parser->source, index);
    if ((operation == NULL) || (operation->kind != R_TOKEN_IDENTIFIER)) {
        return R_STANDARD_TYPE_CALL_NONE;
    }
    index = r_skip_trivia(parser->source, index) + 1U;
    if (((r_scan_kind(parser->source, index) != R_TOKEN_LPAREN) &&
         !r_generic_arguments_at(parser, index)) ||
        !r_reflection_type_call_kind(parser, operation, &kind)) {
        return R_STANDARD_TYPE_CALL_NONE;
    }
    return kind;
}

static RStandardTypeCallKind r_standard_type_call_ahead(const RParser *parser) {
    size_t index = parser->cursor;
    const RToken *module;
    const RToken *operation;

    if (!r_scan_identifier_text(parser, &index, "std") ||
        (r_scan_kind(parser->source, index) != R_TOKEN_DOT)) {
        return r_reflection_type_call_ahead(parser);
    }
    index = r_skip_trivia(parser->source, index) + 1U;
    module = r_scan_token(parser->source, index);
    if (module == NULL) {
        return R_STANDARD_TYPE_CALL_NONE;
    }
    index = r_skip_trivia(parser->source, index) + 1U;
    if (r_scan_kind(parser->source, index) != R_TOKEN_COLON_COLON) {
        return R_STANDARD_TYPE_CALL_NONE;
    }
    index = r_skip_trivia(parser->source, index) + 1U;
    operation = r_scan_token(parser->source, index);
    if (operation == NULL) {
        return R_STANDARD_TYPE_CALL_NONE;
    }
    index = r_skip_trivia(parser->source, index) + 1U;
    if ((r_scan_kind(parser->source, index) != R_TOKEN_LPAREN) &&
        !r_generic_arguments_at(parser, index)) {
        return R_STANDARD_TYPE_CALL_NONE;
    }

    if ((module->kind == R_TOKEN_KW_ASYNC) &&
        r_parser_token_text_is(parser, operation, "broadcast")) {
        /* R-SLIB-ASYNC-0016 (L30): std.async::broadcast::<T>(capacity). */
        return R_STANDARD_TYPE_CALL_ONE_TYPE_ONE_VALUE;
    }
    if (r_parser_token_text_is(parser, module, "json") &&
        r_parser_token_text_is(parser, operation, "schema")) {
        /* M32.6 (R-SLIB-JSON-0002): std.json::schema::<T>(). */
        return R_STANDARD_TYPE_CALL_ONE_TYPE;
    }
    if (r_parser_token_text_is(parser, module, "sync")) {
        if (r_parser_token_text_is(parser, operation, "sync_channel")) {
            return R_STANDARD_TYPE_CALL_ONE_TYPE_ONE_VALUE;
        }
        if (r_parser_token_text_is(parser, operation, "channel") ||
            r_parser_token_text_is(parser, operation, "once_lock")) {
            return R_STANDARD_TYPE_CALL_ONE_TYPE;
        }
    } else if ((module->kind == R_TOKEN_KW_ARRAY) ||
               r_parser_token_text_is(parser, module, "array")) {
        if (r_parser_token_text_is(parser, operation, "with_capacity")) {
            return R_STANDARD_TYPE_CALL_ONE_TYPE_ONE_VALUE;
        }
        if (r_parser_token_text_is(parser, operation, "create")) {
            return R_STANDARD_TYPE_CALL_ONE_TYPE;
        }
    } else if ((module->kind == R_TOKEN_KW_LIST) ||
               r_parser_token_text_is(parser, module, "list")) {
        if (r_parser_token_text_is(parser, operation, "create")) {
            return R_STANDARD_TYPE_CALL_ONE_TYPE;
        }
    } else if ((module->kind == R_TOKEN_KW_DICT) ||
               r_parser_token_text_is(parser, module, "dict")) {
        if (r_parser_token_text_is(parser, operation, "with_capacity")) {
            return R_STANDARD_TYPE_CALL_TWO_TYPES_ONE_VALUE;
        }
        if (r_parser_token_text_is(parser, operation, "create")) {
            return R_STANDARD_TYPE_CALL_TWO_TYPES;
        }
    }
    return R_STANDARD_TYPE_CALL_NONE;
}

/* Only an applied type followed by a constructor/hook separator is an expression head.
 * Ordinary function parentheses remain value arguments. */
static RTokenKind r_applied_type_tail(const RParser *parser) {
    size_t index = r_skip_trivia(parser->source, parser->cursor);
    if (r_scan_kind(parser->source, index) != R_TOKEN_IDENTIFIER) {
        return R_TOKEN_INVALID;
    }
    while (r_scan_kind(parser->source, index) == R_TOKEN_IDENTIFIER) {
        index = r_skip_trivia(parser->source, index + 1U);
        if (r_scan_kind(parser->source, index) != R_TOKEN_DOT &&
            r_scan_kind(parser->source, index) != R_TOKEN_COLON_COLON) {
            break;
        }
        index = r_skip_trivia(parser->source, index + 1U);
    }
    /* R-TYPE-0031: `Name<arguments>` followed by `::` or `{` is a type head; any other
       continuation leaves `<` as a comparison. */
    index = r_skip_balanced_angles(parser, index);
    if (index == 0U) {
        return R_TOKEN_INVALID;
    }
    return r_scan_kind(parser->source, index);
}

static bool r_parse_qualified_name(RParser *parser) {
    size_t node = r_parser_open(parser, R_SYNTAX_QUALIFIED_NAME);
    if (r_applied_type_tail(parser) == R_TOKEN_COLON_COLON) {
        (void)r_parse_type(parser, true);
        (void)r_parser_expect(parser, R_TOKEN_COLON_COLON, "expected '::' after applied type");
        (void)r_parser_expect(parser, R_TOKEN_IDENTIFIER, "expected variant or hook name");
        return r_parser_close(parser, node);
    }
    if (!r_standard_keyword_expression_is_allowed(parser)) {
        (void)r_parser_syntax_error(parser, "unknown closed standard-library qualified name");
    }
    if (!r_is_qualified_component(r_parser_peek_kind(parser)) || !r_parser_bump(parser)) {
        (void)r_parser_syntax_error(parser, "expected qualified name");
        return r_parser_close(parser, node);
    }
    (void)r_parse_qualified_tail(parser, true);
    return r_parser_close(parser, node);
}

/* explicit-generic-arguments = "::", "<", explicit-generic-argument,
                                { ",", explicit-generic-argument }, ">" ;
   explicit-generic-argument = generic-argument
                             | "throws", "(", [ type, { ",", type } ], ")" ; (R-TYPE-0036) */
static bool r_parse_generic_argument_list(RParser *parser) {
    const size_t node = r_parser_open(parser, R_SYNTAX_GENERIC_ARGUMENT_LIST);
    (void)r_parser_expect(parser, R_TOKEN_COLON_COLON, "expected '::' before generic arguments");
    (void)r_parser_expect(parser, R_TOKEN_LESS, "expected '<' after '::'");
    if (r_parser_at_type_close(parser)) {
        (void)r_parser_syntax_error(parser, "an explicit generic argument list shall not be empty");
    }
    while (!r_parser_at_type_close(parser)) {
        if (r_parser_at(parser, R_TOKEN_KW_THROWS)) {
            const size_t set = r_parser_open(parser, R_SYNTAX_THROWS_CLAUSE);
            (void)r_parser_bump(parser);
            (void)r_parser_expect(parser, R_TOKEN_LPAREN, "expected '(' after throws");
            while (!r_parser_at(parser, R_TOKEN_RPAREN)) {
                if (!r_parse_type(parser, true)) {
                    (void)r_parser_syntax_error(parser, "expected checked error type");
                    break;
                }
                if (!r_parser_eat(parser, R_TOKEN_COMMA)) {
                    break;
                }
            }
            (void)r_parser_expect(parser, R_TOKEN_RPAREN, "expected ')' after checked errors");
            (void)r_parser_close(parser, set);
        } else if (!r_parse_generic_argument(parser, true)) {
            if (parser->context->resource_status != R_FRONTEND_OK) {
                return false;
            }
            break;
        }
        if ((parser->pending_greater != 0U) || !r_parser_eat(parser, R_TOKEN_COMMA)) {
            break;
        }
    }
    (void)r_parser_expect_type_close(parser, "expected '>' after generic arguments");
    return r_parser_close(parser, node);
}

/* A standard operation names its type operands like any generic function (R-TYPE-0036):
   `std.array::with_capacity::<T>(capacity)`, `core::enum_count::<T>()`. The retired spelling
   with types among the value arguments is diagnosed with the replacement. */
static bool r_parse_standard_type_call(RParser *parser, RStandardTypeCallKind call_kind) {
    size_t node = r_parser_open(parser, R_SYNTAX_STANDARD_TYPE_CALL);
    uint32_t type_count = UINT32_C(1);
    uint32_t index;
    const bool has_value = (call_kind == R_STANDARD_TYPE_CALL_ONE_TYPE_ONE_VALUE) ||
                           (call_kind == R_STANDARD_TYPE_CALL_TWO_TYPES_ONE_VALUE) ||
                           (call_kind == R_STANDARD_TYPE_CALL_REFLECTION_TYPE_CONSTANT) ||
                           (call_kind == R_STANDARD_TYPE_CALL_REFLECTION_TYPE_VALUE) ||
                           (call_kind == R_STANDARD_TYPE_CALL_REFLECTION_TWO_TYPES_VALUE);

    (void)r_parse_qualified_name(parser);
    if ((call_kind == R_STANDARD_TYPE_CALL_TWO_TYPES) ||
        (call_kind == R_STANDARD_TYPE_CALL_TWO_TYPES_ONE_VALUE) ||
        (call_kind == R_STANDARD_TYPE_CALL_REFLECTION_TWO_TYPES) ||
        (call_kind == R_STANDARD_TYPE_CALL_REFLECTION_TWO_TYPES_VALUE)) {
        type_count = UINT32_C(2);
    }
    if (!r_parser_at(parser, R_TOKEN_COLON_COLON) &&
        (call_kind != R_STANDARD_TYPE_CALL_REFLECTION_TYPE) &&
        (call_kind != R_STANDARD_TYPE_CALL_REFLECTION_TYPE_CONSTANT) &&
        (call_kind != R_STANDARD_TYPE_CALL_REFLECTION_TYPE_VALUE) &&
        (call_kind != R_STANDARD_TYPE_CALL_REFLECTION_TWO_TYPES) &&
        (call_kind != R_STANDARD_TYPE_CALL_REFLECTION_TWO_TYPES_VALUE)) {
        /* R-TYPE-0036 (L17.2): a constructor written with only its value operands takes its
           type operands from the expected type of its result. */
        RParser trial = *parser;
        const size_t events = parser->source->cst_event_count;
        const bool had_error = parser->source->has_syntax_error;
        bool inferred;

        trial.suppress_diagnostics = true;
        parser->source->has_syntax_error = false;
        inferred = r_parser_eat(&trial, R_TOKEN_LPAREN) &&
                   (!has_value || r_parse_expression_mode(&trial, R_EXPRESSION_NORMAL).parsed) &&
                   r_parser_eat(&trial, R_TOKEN_RPAREN) && !parser->source->has_syntax_error;
        parser->source->has_syntax_error = had_error;
        if (parser->context->resource_status != R_FRONTEND_OK) {
            return false;
        }
        if (inferred) {
            trial.suppress_diagnostics = parser->suppress_diagnostics;
            *parser = trial;
            return r_parser_close(parser, node);
        }
        parser->source->cst_event_count = events;
    }
    if (!r_parser_eat(parser, R_TOKEN_COLON_COLON)) {
        (void)r_parser_syntax_error(
            parser,
            has_value ? "standard type operands are named first: write op::<types>(value)"
                      : "standard type operands are named first: write op::<types>()");
        (void)r_parser_expect(parser, R_TOKEN_LPAREN, "expected '(' in standard type call");
        for (index = 0U; index < type_count; ++index) {
            (void)r_parse_type(parser, true);
            if (!r_parser_eat(parser, R_TOKEN_COMMA)) {
                break;
            }
        }
        if (has_value) {
            (void)r_parse_expression_mode(parser, R_EXPRESSION_NORMAL);
        }
        (void)r_parser_expect(parser, R_TOKEN_RPAREN, "expected ')' after standard type call");
        return r_parser_close(parser, node);
    }
    (void)r_parser_expect(parser, R_TOKEN_LESS, "expected '<' before type operands");
    for (index = 0U; index < type_count; ++index) {
        if (!r_parse_type(parser, true)) {
            (void)r_parser_syntax_error(parser, "expected type operand in standard call");
        }
        if ((index + 1U) < type_count) {
            (void)r_parser_expect(parser, R_TOKEN_COMMA, "expected ',' between type operands");
        }
    }
    (void)r_parser_expect_type_close(parser, "expected '>' after type operands");
    (void)r_parser_expect(parser, R_TOKEN_LPAREN, "expected '(' before value operands");
    if (has_value) {
        const RExpressionInfo value = r_parse_expression_mode(parser, R_EXPRESSION_NORMAL);
        if (!value.parsed) {
            (void)r_parser_syntax_error(parser, "expected value operand");
        }
    }
    (void)r_parser_expect(parser, R_TOKEN_RPAREN, "expected ')' after standard type call");
    return r_parser_close(parser, node);
}

static bool r_parse_argument_list(RParser *parser) {
    size_t node = r_parser_open(parser, R_SYNTAX_ARGUMENT_LIST);
    (void)r_parser_expect(parser, R_TOKEN_LPAREN, "expected '('");
    if (!r_parser_at(parser, R_TOKEN_RPAREN)) {
        while (true) {
            RExpressionInfo argument;
            const bool output =
                r_parser_token_text_is(parser, r_parser_peek_token(parser), "out") &&
                (r_parser_peek_n_kind(parser, 1U) == R_TOKEN_IDENTIFIER ||
                 r_parser_peek_n_kind(parser, 1U) == R_TOKEN_STAR);
            const size_t output_node =
                output ? r_parser_open(parser, R_SYNTAX_OUT_ARGUMENT) : SIZE_MAX;
            if (output)
                (void)r_parser_bump(parser);
            if (r_parser_at(parser, R_TOKEN_ELLIPSIS)) {
                /* A spread argument forwards a slice as the
                   whole variadic pack and shall be the last argument (R-FUNC-0018). */
                size_t spread = r_parser_open(parser, R_SYNTAX_SPREAD_ARGUMENT);
                (void)r_parser_bump(parser);
                argument = r_parse_expression_mode(parser, R_EXPRESSION_NORMAL);
                (void)r_parser_close(parser, spread);
                if (!argument.parsed) {
                    (void)r_parser_syntax_error(parser, "expected a spread slice argument");
                    break;
                }
                if (r_parser_at(parser, R_TOKEN_COMMA)) {
                    (void)r_parser_syntax_error(parser, "a spread argument shall be the last");
                }
                break;
            }
            if (!output && r_parser_at(parser, R_TOKEN_IDENTIFIER) &&
                (r_parser_peek_n_kind(parser, 1U) == R_TOKEN_ELLIPSIS)) {
                /* R-TYPE-0053 (L18.4): `T...` names a pack, as in `len(T...)`. */
                size_t expansion = r_parser_open(parser, R_SYNTAX_PACK_EXPANSION);
                (void)r_parser_bump(parser);
                (void)r_parser_bump(parser);
                (void)r_parser_close(parser, expansion);
                if (!r_parser_eat(parser, R_TOKEN_COMMA)) {
                    break;
                }
                continue;
            }
            argument = r_parse_expression_mode(parser, R_EXPRESSION_NORMAL);
            if (output)
                (void)r_parser_close(parser, output_node);
            if (!argument.parsed) {
                (void)r_parser_syntax_error(parser, "expected function argument");
                break;
            }
            if (!r_parser_eat(parser, R_TOKEN_COMMA)) {
                break;
            }
        }
    }
    (void)r_parser_expect(parser, R_TOKEN_RPAREN, "expected ')' after arguments");
    return r_parser_close(parser, node);
}

static RExpressionInfo r_parse_expression_bp(RParser *parser,
                                             RExpressionMode mode,
                                             uint32_t minimum_power,
                                             bool condition_value);

#include "format_literal.inc"
static bool r_conditional_throw_ahead(const RParser *parser);
static bool r_parse_throw_statement(RParser *parser);
#include "match.inc"

static RExpressionInfo r_parse_primary_expression(RParser *parser, RExpressionMode mode) {
    RExpressionInfo result = {false, false, false};
    size_t node;
    RTokenKind kind = r_parser_peek_kind(parser);
    RStandardTypeCallKind standard_call = r_standard_type_call_ahead(parser);

    /* A reflection constant is call-free (R-REFL-0001..0003); every other type call is a call. */
    if ((standard_call != R_STANDARD_TYPE_CALL_NONE) && (mode == R_EXPRESSION_CALL_FREE) &&
        (standard_call != R_STANDARD_TYPE_CALL_REFLECTION_TYPE) &&
        (standard_call != R_STANDARD_TYPE_CALL_REFLECTION_TYPE_CONSTANT)) {
        return result;
    }
    if (mode == R_EXPRESSION_NORMAL && r_match_expression_ahead(parser)) {
        result.parsed = r_parse_match_expression(parser);
        return result;
    }
    if (standard_call != R_STANDARD_TYPE_CALL_NONE) {
        result.parsed = r_parse_standard_type_call(parser, standard_call);
        return result;
    }
    node = r_parser_open(parser, R_SYNTAX_PRIMARY_EXPRESSION);
    if (r_format_sequence_ahead(parser)) {
        result.parsed = r_parse_format_sequence(parser);
    } else if (kind == R_TOKEN_STRING_LITERAL) {
        size_t sequence = r_parser_open(parser, R_SYNTAX_STRING_SEQUENCE);
        do {
            (void)r_parser_bump(parser);
        } while (r_parser_at(parser, R_TOKEN_STRING_LITERAL));
        result.parsed = r_parser_close(parser, sequence);
    } else if (r_is_literal_token(kind)) {
        result.parsed = r_parser_bump(parser);
    } else if ((kind == R_TOKEN_KW_TRUE) || (kind == R_TOKEN_KW_FALSE)) {
        result.parsed = r_parser_bump(parser);
        result.is_condition = true;
    } else if (kind == R_TOKEN_KW_NULL) {
        result.parsed = r_parser_bump(parser);
    } else if (kind == R_TOKEN_KW_THIS) {
        /* The receiver parameter is an ordinary name in expressions (R-FUNC-0013). */
        result.parsed = r_parser_bump(parser);
    } else if ((kind == R_TOKEN_KW_SELF) &&
               (r_parser_peek_n_kind(parser, 1U) == R_TOKEN_COLON_COLON)) {
        /* R-TYPE-0050: `Self::NAME` names an associated constant of the enclosing scope. */
        const size_t qualified = r_parser_open(parser, R_SYNTAX_QUALIFIED_NAME);
        (void)r_parser_bump(parser);
        (void)r_parse_qualified_tail(parser, true);
        result.parsed = r_parser_close(parser, qualified);
    } else if ((kind == R_TOKEN_IDENTIFIER) || (kind == R_TOKEN_KW_O)) {
        if (r_applied_type_tail(parser) == R_TOKEN_COLON_COLON) {
            result.parsed = r_parse_qualified_name(parser);
        } else if (r_applied_type_tail(parser) == R_TOKEN_LBRACE &&
                   (r_is_visible_aggregate(parser->context,
                                           parser->source_id,
                                           r_parser_peek_token(parser)->span,
                                           r_parser_peek_token(parser)->span.start) ||
                    r_visible_qualified_aggregate(parser))) {
            result.parsed = r_parse_type(parser, true);
        } else if (r_qualified_name_ahead(parser)) {
            result.parsed = r_parse_qualified_name(parser);
        } else {
            result.parsed = r_parser_bump(parser);
        }
        const bool explicit_arguments =
            result.parsed && r_generic_arguments_at(parser, parser->cursor);
        if (explicit_arguments) {
            result.parsed = r_parse_generic_argument_list(parser);
        }
        if (result.parsed && r_parser_at(parser, R_TOKEN_LBRACE)) {
            /* An aggregate constructor names a generic type as `Box<i32> { ... }` (Annex A). */
            if (explicit_arguments)
                (void)r_parser_syntax_error(parser,
                                            "an aggregate literal writes its generic type "
                                            "without '::', as in Box<i32> { ... }");
            result.parsed = r_parse_aggregate_initializer(parser, mode);
        }
    } else if (kind == R_TOKEN_LPAREN) {
        const bool no_greater = parser->no_greater;
        size_t mark;
        parser->no_greater = false;
        if (!r_parser_flush_trivia(parser)) {
            result.parsed = false;
            return result;
        }
        mark = parser->source->cst_event_count;
        (void)r_parser_bump(parser);
        result = r_parse_expression_bp(parser, mode, UINT32_C(1), false);
        if (r_parser_at(parser, R_TOKEN_COMMA)) {
            /* R-TYPE-0052 (L18.1): `(e1, e2, ...)` builds a tuple. */
            const size_t tuple = r_parser_open_at(parser, mark, R_SYNTAX_TUPLE_EXPRESSION);
            while (r_parser_eat(parser, R_TOKEN_COMMA)) {
                if (r_parser_at(parser, R_TOKEN_ELLIPSIS)) {
                    /* R-TYPE-0053 (L18.4): `(e1, ...rest)` appends the elements of a tuple
                       or a pack, as the last element. */
                    const size_t spread = r_parser_open(parser, R_SYNTAX_SPREAD_ARGUMENT);
                    (void)r_parser_bump(parser);
                    const RExpressionInfo operand =
                        r_parse_expression_bp(parser, mode, UINT32_C(1), false);
                    (void)r_parser_close(parser, spread);
                    result.parsed = result.parsed && operand.parsed;
                    if (r_parser_at(parser, R_TOKEN_COMMA)) {
                        (void)r_parser_syntax_error(parser,
                                                    "a spread shall be the last tuple element");
                    }
                    break;
                }
                const RExpressionInfo element =
                    r_parse_expression_bp(parser, mode, UINT32_C(1), false);
                result.parsed = result.parsed && element.parsed;
            }
            parser->no_greater = no_greater;
            (void)r_parser_expect(parser, R_TOKEN_RPAREN, "expected ')' after tuple elements");
            (void)r_parser_close(parser, tuple);
            result.is_condition = false;
        } else {
            parser->no_greater = no_greater;
            (void)r_parser_expect(parser, R_TOKEN_RPAREN, "expected ')' after expression");
        }
    } else if ((kind == R_TOKEN_LBRACKET) && (mode == R_EXPRESSION_NORMAL)) {
        /* R-EXPR-0030: an array expression or comprehension in an initializer position. */
        result.parsed = r_parse_collection_expression(parser);
    } else if (kind == R_TOKEN_KW_PANIC) {
        if (mode == R_EXPRESSION_CALL_FREE) {
            result.parsed = false;
        } else {
            (void)r_parser_bump(parser);
            (void)r_parser_expect(parser, R_TOKEN_LPAREN, "expected '(' after panic");
            result = r_parse_expression_mode(parser, R_EXPRESSION_CALL_FREE);
            (void)r_parser_expect(parser, R_TOKEN_RPAREN, "expected ')' after panic operand");
            result.is_condition = false;
        }
    }
    (void)r_parser_close(parser, node);
    return result;
}

static bool r_is_prefix_operator(RTokenKind kind) {
    return (kind == R_TOKEN_PLUS) || (kind == R_TOKEN_MINUS) || (kind == R_TOKEN_BANG) ||
           (kind == R_TOKEN_TILDE) || (kind == R_TOKEN_STAR) || (kind == R_TOKEN_AMP) ||
           (kind == R_TOKEN_PLUS_PLUS) || (kind == R_TOKEN_MINUS_MINUS) ||
           (kind == R_TOKEN_KW_MOVE);
}

static RExpressionInfo r_parse_postfix_expression(RParser *parser, RExpressionMode mode);

static RExpressionInfo r_parse_prefix_expression(RParser *parser, RExpressionMode mode) {
    RExpressionInfo result = {false, false, false};
    RTokenKind kind = r_parser_peek_kind(parser);

    if (!r_parser_enter_nesting(parser)) {
        return result;
    }

    if (kind == R_TOKEN_KW_AWAIT && mode == R_EXPRESSION_NORMAL) {
        result.parsed = r_parse_await_operation(parser);
        r_parser_leave_nesting(parser);
        return result;
    }
    if (r_is_prefix_operator(kind)) {
        size_t node = r_parser_open(parser, R_SYNTAX_UNARY_EXPRESSION);
        (void)r_parser_bump(parser);
        result = r_parse_prefix_expression(parser, mode);
        if (kind == R_TOKEN_BANG) {
            result.is_condition = true;
        }
        (void)r_parser_close(parser, node);
        r_parser_leave_nesting(parser);
        return result;
    }
    if ((kind == R_TOKEN_KW_SIZEOF) || (kind == R_TOKEN_KW_ALIGNOF)) {
        size_t node = r_parser_open(parser, R_SYNTAX_UNARY_EXPRESSION);
        (void)r_parser_bump(parser);
        (void)r_parser_expect(parser, R_TOKEN_LPAREN, "expected '(' after sizeof or alignof");
        result.parsed = r_parse_type(parser, true);
        (void)r_parser_expect(parser, R_TOKEN_RPAREN, "expected ')' after type operand");
        (void)r_parser_close(parser, node);
        r_parser_leave_nesting(parser);
        return result;
    }
    if (kind == R_TOKEN_KW_NEW) {
        size_t node = r_parser_open(parser, R_SYNTAX_NEW_EXPRESSION);
        (void)r_parser_bump(parser);
        result.parsed = r_parse_type(parser, true);
        if (r_parser_eat(parser, R_TOKEN_LPAREN)) {
            RExpressionInfo initializer = r_parse_expression_mode(parser, mode);
            result.parsed = result.parsed && initializer.parsed;
            (void)r_parser_expect(parser, R_TOKEN_RPAREN, "expected ')' after new initializer");
        } else if (r_parser_at(parser, R_TOKEN_LBRACE)) {
            result.parsed = result.parsed && r_parse_aggregate_initializer(parser, mode);
        } else {
            result.parsed = false;
            (void)r_parser_syntax_error(parser, "expected initializer after new type");
        }
        (void)r_parser_close(parser, node);
        r_parser_leave_nesting(parser);
        return result;
    }
    result = r_parse_postfix_expression(parser, mode);
    r_parser_leave_nesting(parser);
    return result;
}

/* R-FUNC-0024 (L19): a suffix after a method call continues a chain. The completed postfix
   expression becomes the parenthesized receiver of the rest, as if written `(a.f()).g()`, and is
   marked so that the semantic check requires the called method to be declared @chain. */
static bool r_parser_continue_chain(RParser *parser, RExpressionMode mode, size_t node) {
    const RTokenKind kind = r_parser_peek_kind(parser);

    if ((mode == R_EXPRESSION_CALL_FREE) || (node == SIZE_MAX) ||
        ((kind != R_TOKEN_DOT) && (kind != R_TOKEN_ARROW) && (kind != R_TOKEN_LBRACKET) &&
         (kind != R_TOKEN_LPAREN))) {
        return false;
    }
    if (!r_parser_wrap_expression(parser, node, R_SYNTAX_BINARY_EXPRESSION) ||
        !r_parser_wrap_expression(parser, node, R_SYNTAX_PRIMARY_EXPRESSION) ||
        !r_parser_wrap_expression(parser, node, R_SYNTAX_POSTFIX_EXPRESSION)) {
        return false;
    }
    parser->source->cst_events[node + 3U].implicit_chain = true;
    return true;
}

static RExpressionInfo r_parse_postfix_expression(RParser *parser, RExpressionMode mode) {
    size_t node = r_parser_open(parser, R_SYNTAX_POSTFIX_EXPRESSION);
    bool constructor_candidate =
        r_qualified_name_ahead(parser) || r_applied_type_tail(parser) == R_TOKEN_COLON_COLON;
    RExpressionInfo result = r_parse_primary_expression(parser, mode);

    while (result.parsed) {
        RTokenKind kind = r_parser_peek_kind(parser);
        if (kind == R_TOKEN_LPAREN) {
            size_t suffix;
            if ((mode == R_EXPRESSION_CALL_FREE) && !constructor_candidate) {
                break;
            }
            suffix = r_parser_open(parser,
                                   mode == R_EXPRESSION_CALL_FREE ? R_SYNTAX_CONSTRUCTOR_SUFFIX
                                                                  : R_SYNTAX_CALL_SUFFIX);
            constructor_candidate = false;
            (void)r_parse_argument_list(parser);
            (void)r_parser_close(parser, suffix);
        } else if (kind == R_TOKEN_LBRACKET) {
            constructor_candidate = false;
            size_t suffix;
            RExpressionMode inner_mode =
                (mode == R_EXPRESSION_CALL_FREE) ? R_EXPRESSION_CALL_FREE : R_EXPRESSION_NORMAL;
            (void)r_parser_bump(parser);
            if (r_parser_at(parser, R_TOKEN_DOT_DOT)) {
                suffix = r_parser_open(parser, R_SYNTAX_SLICE_SUFFIX);
                (void)r_parser_bump(parser);
                if (!r_parser_at(parser, R_TOKEN_RBRACKET)) {
                    (void)r_parse_expression_mode(parser, inner_mode);
                }
            } else {
                RExpressionInfo first = r_parse_expression_mode(parser, inner_mode);
                if (!first.parsed) {
                    (void)r_parser_syntax_error(parser, "expected index or slice bound");
                }
                if (r_parser_eat(parser, R_TOKEN_DOT_DOT)) {
                    suffix = r_parser_open(parser, R_SYNTAX_SLICE_SUFFIX);
                    if (!r_parser_at(parser, R_TOKEN_RBRACKET)) {
                        (void)r_parse_expression_mode(parser, inner_mode);
                    }
                } else {
                    suffix = r_parser_open(parser, R_SYNTAX_INDEX_SUFFIX);
                }
            }
            (void)r_parser_expect(parser, R_TOKEN_RBRACKET, "expected ']' after postfix subscript");
            (void)r_parser_close(parser, suffix);
        } else if (kind == R_TOKEN_DOT && r_parser_peek_n_kind(parser, 1U) == R_TOKEN_IDENTIFIER &&
                   r_parser_peek_n_kind(parser, 2U) == R_TOKEN_LPAREN &&
                   r_parser_token_index_text_is(
                       parser,
                       r_skip_trivia(parser->source,
                                     r_skip_trivia(parser->source, parser->cursor) + 1U),
                       "format")) {
            size_t suffix = r_parser_open(parser, R_SYNTAX_FORMAT_SUFFIX);
            (void)r_parser_bump(parser);
            (void)r_parser_bump(parser);
            (void)r_parse_argument_list(parser);
            (void)r_parser_close(parser, suffix);
            constructor_candidate = false;
        } else if (((kind == R_TOKEN_DOT) || (kind == R_TOKEN_ARROW)) &&
                   (r_parser_peek_n_kind(parser, 1U) == R_TOKEN_IDENTIFIER) &&
                   (r_parser_peek_n_kind(parser, 2U) == R_TOKEN_COLON_COLON) &&
                   (r_parser_peek_n_kind(parser, 3U) == R_TOKEN_LESS)) {
            /* R-TYPE-0036: `receiver.method::<arguments>(values)` closes a generic method. */
            size_t suffix = r_parser_open(parser, R_SYNTAX_METHOD_CALL_SUFFIX);
            if (suffix != SIZE_MAX) {
                parser->source->cst_events[suffix].call_free = mode == R_EXPRESSION_CALL_FREE;
            }
            (void)r_parser_bump(parser);
            (void)r_parser_bump(parser);
            (void)r_parse_generic_argument_list(parser);
            (void)r_parse_argument_list(parser);
            (void)r_parser_close(parser, suffix);
            if (!r_parser_continue_chain(parser, mode, node)) {
                break;
            }
        } else if (((kind == R_TOKEN_DOT) || (kind == R_TOKEN_ARROW)) &&
                   (r_parser_peek_n_kind(parser, 1U) == R_TOKEN_IDENTIFIER) &&
                   (r_parser_peek_n_kind(parser, 2U) == R_TOKEN_LPAREN)) {
            /* R-FUNC-0014: the method-call suffix is the last suffix unless the method is
               @chain (R-FUNC-0024); its arguments are call-free. In call-free mode the member
               suffix is taken and the following '(' stops the expression, as it does for any
               nested call. */
            size_t suffix = r_parser_open(parser, R_SYNTAX_METHOD_CALL_SUFFIX);
            if (suffix != SIZE_MAX) {
                parser->source->cst_events[suffix].call_free = mode == R_EXPRESSION_CALL_FREE;
            }
            (void)r_parser_bump(parser);
            (void)r_parser_bump(parser);
            (void)r_parse_argument_list(parser);
            (void)r_parser_close(parser, suffix);
            if (!r_parser_continue_chain(parser, mode, node)) {
                break;
            }
        } else if ((kind == R_TOKEN_DOT) || (kind == R_TOKEN_ARROW)) {
            size_t suffix = r_parser_open(parser, R_SYNTAX_MEMBER_SUFFIX);
            (void)r_parser_bump(parser);
            if (r_parser_at(parser, R_TOKEN_INTEGER_LITERAL)) {
                /* R-TYPE-0052 (L18.1): `.0` selects a tuple element. */
                (void)r_parser_bump(parser);
            } else {
                (void)r_parser_expect(parser, R_TOKEN_IDENTIFIER, "expected member name");
            }
            (void)r_parser_close(parser, suffix);
        } else if ((kind == R_TOKEN_PLUS_PLUS) || (kind == R_TOKEN_MINUS_MINUS)) {
            (void)r_parser_bump(parser);
        } else {
            break;
        }
    }
    (void)r_parser_close(parser, node);
    return result;
}

/* membership-operator = [ "not" ], "in" ; `not` is contextual: an identifier that precedes
   the keyword (R-EXPR-0029). */
static bool r_membership_operator_ahead(const RParser *parser, bool *negated) {
    size_t index = r_skip_trivia(parser->source, parser->cursor);

    *negated = false;
    if (r_scan_kind(parser->source, index) == R_TOKEN_KW_IN) {
        return true;
    }
    if ((r_scan_kind(parser->source, index) == R_TOKEN_IDENTIFIER) &&
        r_parser_token_index_text_is(parser, index, "not")) {
        index = r_skip_trivia(parser->source, index + 1U);
        if (r_scan_kind(parser->source, index) == R_TOKEN_KW_IN) {
            *negated = true;
            return true;
        }
    }
    return false;
}

/* range-expression = expression, "..", expression ; the operand of `in` and the iterable of a
   range-for are one expression or one range (R-STMT-0014, R-EXPR-0029). */
static RExpressionInfo r_parse_range_or_expression(RParser *parser,
                                                   RExpressionMode mode,
                                                   uint32_t minimum_power,
                                                   bool condition_value) {
    size_t node = r_parser_open(parser, R_SYNTAX_EXPRESSION);
    RExpressionInfo result = r_parse_expression_bp(parser, mode, minimum_power, condition_value);

    if (r_parser_at(parser, R_TOKEN_DOT_DOT)) {
        RExpressionInfo high;
        parser->source->cst_events[node].syntax_kind = R_SYNTAX_RANGE_EXPRESSION;
        (void)r_parser_bump(parser);
        high = r_parse_expression_bp(parser, mode, minimum_power, condition_value);
        result.parsed = result.parsed && high.parsed;
    }
    result.is_condition = false;
    result.is_membership = false;
    (void)r_parser_close(parser, node);
    return result;
}

static RExpressionInfo r_parse_expression_bp(RParser *parser,
                                             RExpressionMode mode,
                                             uint32_t minimum_power,
                                             bool condition_value) {
    size_t node;
    RExpressionInfo left = {false, false, false};
    bool saw_assignment = false;
    bool saw_membership = false;
    bool negated_membership = false;

    if (!r_parser_enter_nesting(parser)) {
        return left;
    }
    node = r_parser_open(parser, R_SYNTAX_BINARY_EXPRESSION);
    left = r_parse_prefix_expression(parser, mode);

    while (left.parsed) {
        RTokenKind operation = r_parser_peek_kind(parser);
        uint32_t left_power;
        uint32_t right_power;
        bool condition_result;

        if ((parser->pending_greater != 0U) ||
            (parser->no_greater &&
             ((operation == R_TOKEN_GREATER) || (operation == R_TOKEN_GREATER_GREATER) ||
              (operation == R_TOKEN_GREATER_EQUAL) ||
              (operation == R_TOKEN_GREATER_GREATER_EQUAL)))) {
            /* The `>` closes the enclosing generic argument list (R-TYPE-0031). */
            break;
        }
        if (operation == R_TOKEN_KW_AS) {
            if (UINT32_C(13) < minimum_power) {
                break;
            }
            (void)r_parser_bump(parser);
            if (!r_parse_type(parser, true)) {
                (void)r_parser_syntax_error(parser, "expected type after as");
            }
            left.is_condition = false;
            continue;
        }
        if (r_is_assignment_operator(operation) && !condition_value) {
            if (UINT32_C(1) < minimum_power) {
                break;
            }
            if (saw_assignment) {
                (void)r_parser_syntax_error(parser, "assignment operators are non-associative");
            }
            saw_assignment = true;
            (void)r_parser_bump(parser);
            {
                RExpressionInfo right = r_parse_expression_bp(parser, mode, UINT32_C(2), false);
                left.parsed = left.parsed && right.parsed;
                left.is_condition = false;
            }
            continue;
        }
        if ((operation == R_TOKEN_QUESTION) && !condition_value) {
            RExpressionInfo true_arm;
            RExpressionInfo false_arm;
            if (UINT32_C(2) < minimum_power) {
                break;
            }
            if (!left.is_condition) {
                (void)r_parser_syntax_error(parser,
                                            "conditional operator requires an explicit condition");
            }
            if (!r_parser_wrap_expression(parser, node, R_SYNTAX_CONDITIONAL_EXPRESSION)) {
                left.parsed = false;
                break;
            }
            (void)r_parser_bump(parser);
            true_arm = r_parse_expression_bp(parser, mode, UINT32_C(1), false);
            (void)r_parser_expect(parser, R_TOKEN_COLON, "expected ':' in conditional expression");
            false_arm = r_parse_expression_bp(parser, mode, UINT32_C(2), false);
            left.parsed = left.parsed && true_arm.parsed && false_arm.parsed;
            left.is_condition = false;
            if (r_is_assignment_operator(r_parser_peek_kind(parser)) &&
                (minimum_power <= UINT32_C(1)) &&
                !r_parser_wrap_expression(parser, node, R_SYNTAX_BINARY_EXPRESSION)) {
                left.parsed = false;
            }
            continue;
        }
        if (r_membership_operator_ahead(parser, &negated_membership)) {
            /* membership-expression = value-expression, [ "not" ], "in",
                                       ( expression | range-expression ) ; binds like a
               relational operator and is non-associative (R-EXPR-0029). */
            if (UINT32_C(9) < minimum_power) {
                break;
            }
            if (saw_membership) {
                (void)r_parser_syntax_error(parser, "membership tests are non-associative");
            }
            saw_membership = true;
            if (!r_parser_wrap_expression(parser, node, R_SYNTAX_MEMBERSHIP_EXPRESSION)) {
                left.parsed = false;
                break;
            }
            if (negated_membership) {
                (void)r_parser_bump(parser);
            }
            (void)r_parser_bump(parser);
            {
                RExpressionInfo right =
                    r_parse_range_or_expression(parser, mode, UINT32_C(10), condition_value);
                left.parsed = left.parsed && right.parsed;
            }
            left.is_condition = true;
            left.is_membership = true;
            /* The completed test becomes one operand of the surrounding chain. */
            if (!r_parser_wrap_expression(parser, node, R_SYNTAX_BINARY_EXPRESSION)) {
                left.parsed = false;
                break;
            }
            continue;
        }
        if (!r_binary_binding_power(operation, &left_power, &right_power, &condition_result) ||
            (left_power < minimum_power) ||
            !r_expression_operator_allowed(operation, condition_value)) {
            break;
        }
        (void)r_parser_bump(parser);
        {
            RExpressionInfo right =
                r_parse_expression_bp(parser, mode, right_power, condition_value);
            left.parsed = left.parsed && right.parsed;
            if (condition_result) {
                if (((operation == R_TOKEN_PIPE_PIPE) || (operation == R_TOKEN_AMP_AMP)) &&
                    (!left.is_condition || !right.is_condition)) {
                    left.is_condition = false;
                } else {
                    left.is_condition = true;
                }
            } else {
                left.is_condition = false;
            }
            left.is_membership = false;
        }
    }
    (void)r_parser_close(parser, node);
    r_parser_leave_nesting(parser);
    return left;
}

static RExpressionInfo r_parse_expression_mode(RParser *parser, RExpressionMode mode) {
    size_t node = r_parser_open(parser, R_SYNTAX_EXPRESSION);
    RExpressionInfo result = r_parse_expression_bp(parser, mode, UINT32_C(1), false);
    (void)r_parser_close(parser, node);
    return result;
}

/* Annex A: a parenthesized group at the start of a condition leaf is a nested condition only
   when a nested condition may end there, before `&&`, `||`, `)`, `;` or `?`. Any other token
   continues a value operand, as in `(*p).x != 1` or `(a + b) * 2 > c`. */
static bool r_parenthesized_value_operand(const RParser *parser) {
    size_t index = r_skip_trivia(parser->source, parser->cursor);
    uint32_t depth = UINT32_C(0);

    if (r_scan_kind(parser->source, index) != R_TOKEN_LPAREN) {
        return false;
    }
    while (index < parser->source->token_count) {
        RTokenKind kind = r_scan_kind(parser->source, index);
        if (kind == R_TOKEN_LPAREN) {
            depth += 1U;
        } else if (kind == R_TOKEN_RPAREN) {
            if (depth == UINT32_C(0)) {
                return false;
            }
            depth -= 1U;
            if (depth == UINT32_C(0)) {
                index = r_skip_trivia(parser->source, index + 1U);
                kind = r_scan_kind(parser->source, index);
                return (kind != R_TOKEN_AMP_AMP) && (kind != R_TOKEN_PIPE_PIPE) &&
                       (kind != R_TOKEN_RPAREN) && (kind != R_TOKEN_SEMICOLON) &&
                       (kind != R_TOKEN_QUESTION) && (kind != R_TOKEN_EOF);
            }
        } else if (kind == R_TOKEN_EOF) {
            return false;
        }
        index = r_skip_trivia(parser->source, index + 1U);
    }
    return false;
}

static RExpressionInfo r_parse_condition_primary(RParser *parser) {
    RExpressionInfo result = {false, false, false};

    if (r_parser_at(parser, R_TOKEN_KW_TRUE) || r_parser_at(parser, R_TOKEN_KW_FALSE)) {
        result.parsed = r_parser_bump(parser);
        result.is_condition = true;
        return result;
    }
    if (r_parser_at(parser, R_TOKEN_LPAREN) && !r_parenthesized_value_operand(parser) &&
        r_parser_eat(parser, R_TOKEN_LPAREN)) {
        result = r_parse_condition_expression(parser);
        (void)r_parser_expect(parser, R_TOKEN_RPAREN, "expected ')' after condition");
        return result;
    }
    const size_t mark = parser->source->cst_event_count;
    result = r_parse_expression_bp(parser, R_EXPRESSION_NORMAL, UINT32_C(1), true);
    if (result.parsed && r_parser_at(parser, R_TOKEN_IDENTIFIER) &&
        r_parser_token_text_is(parser, r_parser_peek_token(parser), "is")) {
        /* R-STMT-0002 (L37.1): `value is pattern` tests a match pattern (R-EXPR-0031). */
        const size_t test = r_parser_open_at(parser, mark, R_SYNTAX_PATTERN_TEST);
        (void)r_parser_bump(parser);
        result.parsed = r_parse_match_pattern(parser) && result.parsed;
        (void)r_parser_close(parser, test);
        result.is_condition = result.parsed;
        return result;
    }
    if (result.parsed && result.is_membership &&
        !r_is_comparison_operator(r_parser_peek_kind(parser))) {
        /* R-STMT-0002: a membership test is a condition leaf of its own. */
        result.is_condition = true;
        return result;
    }
    if (!result.parsed || !r_is_comparison_operator(r_parser_peek_kind(parser))) {
        result.is_condition = false;
        (void)r_parser_syntax_error(parser,
                                    "condition requires true, false, or an explicit comparison");
        return result;
    }
    (void)r_parser_bump(parser);
    {
        RExpressionInfo right =
            r_parse_expression_bp(parser, R_EXPRESSION_NORMAL, UINT32_C(1), true);
        result.parsed = result.parsed && right.parsed;
        result.is_condition = result.parsed;
    }
    return result;
}

static RExpressionInfo r_parse_condition_and_expression(RParser *parser) {
    size_t node = r_parser_open(parser, R_SYNTAX_BINARY_EXPRESSION);
    RExpressionInfo result = r_parse_condition_primary(parser);

    while (r_parser_at(parser, R_TOKEN_AMP_AMP)) {
        RExpressionInfo right;
        (void)r_parser_bump(parser);
        right = r_parse_condition_primary(parser);
        result.parsed = result.parsed && right.parsed;
        result.is_condition = result.is_condition && right.is_condition;
    }
    (void)r_parser_close(parser, node);
    return result;
}

static RExpressionInfo r_parse_condition_expression(RParser *parser) {
    size_t node;
    RExpressionInfo result = {false, false, false};

    if (!r_parser_enter_nesting(parser)) {
        return result;
    }
    node = r_parser_open(parser, R_SYNTAX_EXPRESSION);
    result = r_parse_condition_and_expression(parser);

    while (r_parser_at(parser, R_TOKEN_PIPE_PIPE)) {
        RExpressionInfo right;
        (void)r_parser_bump(parser);
        right = r_parse_condition_and_expression(parser);
        result.parsed = result.parsed && right.parsed;
        result.is_condition = result.is_condition && right.is_condition;
    }
    (void)r_parser_close(parser, node);
    r_parser_leave_nesting(parser);
    return result;
}

static bool r_parse_initializer(RParser *parser, RExpressionMode mode) {
    size_t node = r_parser_open(parser, R_SYNTAX_INITIALIZER);
    bool parsed;
    if (r_parser_at(parser, R_TOKEN_LBRACE)) {
        /* R-EXPR-0030: a braced initializer whose first item is followed by ':' is a dict
           expression; ordinary expressions may not host one. */
        parsed = r_parse_braced_initializer(parser, mode, mode == R_EXPRESSION_NORMAL);
    } else {
        parsed = r_parse_expression_mode(parser, mode).parsed;
    }
    (void)r_parser_close(parser, node);
    return parsed;
}

static bool r_parse_aggregate_initializer(RParser *parser, RExpressionMode mode) {
    return r_parse_braced_initializer(parser, mode, false);
}

/* dict-entry = expression, ":", expression ; the first entry is reached from the aggregate
   initializer item that turned out to be a key (R-EXPR-0030). */
static bool r_parse_dict_entry_value(RParser *parser) {
    (void)r_parser_expect(parser, R_TOKEN_COLON, "expected ':' in dict entry");
    return r_parse_expression_mode(parser, R_EXPRESSION_NORMAL).parsed;
}

static bool r_parse_braced_initializer(RParser *parser, RExpressionMode mode, bool allow_dict) {
    size_t node;
    size_t item_count = 0U;
    bool parsed = true;
    bool is_dict = false;

    if (!r_parser_enter_nesting(parser)) {
        return false;
    }
    node = r_parser_open(parser, R_SYNTAX_AGGREGATE_INITIALIZER);

    (void)r_parser_expect(parser, R_TOKEN_LBRACE, "expected '{' in aggregate initializer");
    if (!r_parser_at(parser, R_TOKEN_RBRACE)) {
        while (true) {
            if (!is_dict && r_parser_at(parser, R_TOKEN_ELLIPSIS)) {
                /* R-INIT-0004 (L37.4): `...base` takes the omitted fields from another value of
                   the struct type, as the last item. */
                const size_t spread = r_parser_open(parser, R_SYNTAX_SPREAD_ARGUMENT);
                (void)r_parser_bump(parser);
                parsed = r_parse_expression_mode(parser, R_EXPRESSION_NORMAL).parsed && parsed;
                (void)r_parser_close(parser, spread);
                (void)r_parser_eat(parser, R_TOKEN_COMMA);
                if (!r_parser_at(parser, R_TOKEN_RBRACE)) {
                    parsed = false;
                    (void)r_parser_syntax_error(
                        parser, "a struct update `...base` shall be the last initializer item");
                }
                break;
            }
            size_t item = r_parser_open(parser, R_SYNTAX_INITIALIZER_ITEM);
            bool designated = false;
            if (r_parser_eat(parser, R_TOKEN_DOT)) {
                designated = true;
                (void)r_parser_expect(parser, R_TOKEN_IDENTIFIER, "expected designated field name");
                (void)r_parser_expect(
                    parser, R_TOKEN_EQUAL, "expected '=' in designated initializer");
            }
            if (is_dict) {
                if (designated || !r_parse_expression_mode(parser, R_EXPRESSION_NORMAL).parsed) {
                    parsed = false;
                    (void)r_parser_syntax_error(parser, "expected dict entry key");
                }
                parser->source->cst_events[item].syntax_kind = R_SYNTAX_DICT_ENTRY;
                parsed = r_parse_dict_entry_value(parser) && parsed;
            } else {
                if (!r_parse_initializer(parser, mode)) {
                    parsed = false;
                    (void)r_parser_syntax_error(parser, "expected initializer item");
                }
                if (!designated && allow_dict && r_parser_at(parser, R_TOKEN_COLON)) {
                    is_dict = true;
                    parser->source->cst_events[node].syntax_kind = R_SYNTAX_DICT_EXPRESSION;
                    parser->source->cst_events[item].syntax_kind = R_SYNTAX_DICT_ENTRY;
                    parsed = r_parse_dict_entry_value(parser) && parsed;
                }
            }
            (void)r_parser_close(parser, item);
            item_count += 1U;
            if (is_dict &&
                (r_parser_at(parser, R_TOKEN_KW_FOR) || r_parser_at(parser, R_TOKEN_KW_IF))) {
                if (item_count != 1U) {
                    parsed = false;
                    (void)r_parser_syntax_error(parser,
                                                "a dict comprehension has exactly one entry");
                }
                parsed = r_parse_comprehension_clauses(parser) && parsed;
                break;
            }
            if (!r_parser_eat(parser, R_TOKEN_COMMA)) {
                break;
            }
            if (r_parser_at(parser, R_TOKEN_RBRACE)) {
                break;
            }
        }
    }
    (void)r_parser_expect(parser,
                          R_TOKEN_RBRACE,
                          is_dict ? "expected '}' after dict expression"
                                  : "expected '}' after aggregate initializer");
    parsed = r_parser_close(parser, node) && parsed;
    r_parser_leave_nesting(parser);
    return parsed;
}

/* comprehension-clause = "for", "(", ( type | "auto" ), identifier, "in", iterable, ")"
                        | "if", "(", condition-expression, ")" ; at least one for clause
   leads (R-EXPR-0030). */
static bool r_parse_comprehension_clauses(RParser *parser) {
    bool parsed = true;
    bool first = true;

    while (r_parser_at(parser, R_TOKEN_KW_FOR) || r_parser_at(parser, R_TOKEN_KW_IF)) {
        if (r_parser_at(parser, R_TOKEN_KW_FOR)) {
            size_t clause = r_parser_open(parser, R_SYNTAX_COMPREHENSION_FOR);
            RExpressionInfo iterable;
            (void)r_parser_bump(parser);
            (void)r_parser_expect(parser, R_TOKEN_LPAREN, "expected '(' after for");
            if (r_parser_at(parser, R_TOKEN_KW_AUTO)) {
                size_t inferred = r_parser_open(parser, R_SYNTAX_TYPE);
                (void)r_parser_bump(parser);
                (void)r_parser_close(parser, inferred);
            } else if (!r_parse_type(parser, true)) {
                parsed = false;
                (void)r_parser_syntax_error(parser, "expected loop variable type");
            }
            (void)r_parser_expect(parser, R_TOKEN_IDENTIFIER, "expected loop variable name");
            (void)r_parser_expect(parser, R_TOKEN_KW_IN, "expected 'in' after the loop variable");
            iterable = r_parse_range_or_expression(parser, R_EXPRESSION_NORMAL, UINT32_C(1), false);
            if (!iterable.parsed) {
                parsed = false;
                (void)r_parser_syntax_error(parser, "expected an iterable expression or range");
            }
            (void)r_parser_expect(parser, R_TOKEN_RPAREN, "expected ')' after the iterable");
            (void)r_parser_close(parser, clause);
        } else {
            size_t clause = r_parser_open(parser, R_SYNTAX_COMPREHENSION_IF);
            RExpressionInfo condition;
            if (first) {
                parsed = false;
                (void)r_parser_syntax_error(parser, "a comprehension starts with a for clause");
            }
            (void)r_parser_bump(parser);
            (void)r_parser_expect(parser, R_TOKEN_LPAREN, "expected '(' after if");
            condition = r_parse_condition_expression(parser);
            if (!condition.parsed || !condition.is_condition) {
                parsed = false;
                (void)r_parser_syntax_error(parser, "invalid explicit comprehension condition");
            }
            (void)r_parser_expect(parser, R_TOKEN_RPAREN, "expected ')' after the condition");
            (void)r_parser_close(parser, clause);
        }
        first = false;
    }
    return parsed;
}

/* collection-expression = "[", [ expression, ( { ",", expression }, [ "," ]
                           | comprehension-clause, { comprehension-clause } ) ], "]" */
static bool r_parse_collection_expression(RParser *parser) {
    size_t node;
    size_t element_count = 0U;
    bool parsed = true;

    if (!r_parser_enter_nesting(parser)) {
        return false;
    }
    node = r_parser_open(parser, R_SYNTAX_COLLECTION_EXPRESSION);
    (void)r_parser_expect(parser, R_TOKEN_LBRACKET, "expected '['");
    if (!r_parser_at(parser, R_TOKEN_RBRACKET)) {
        while (true) {
            if (!r_parse_expression_mode(parser, R_EXPRESSION_NORMAL).parsed) {
                parsed = false;
                (void)r_parser_syntax_error(parser, "expected collection element");
                break;
            }
            element_count += 1U;
            if (r_parser_at(parser, R_TOKEN_KW_FOR) || r_parser_at(parser, R_TOKEN_KW_IF)) {
                if (element_count != 1U) {
                    parsed = false;
                    (void)r_parser_syntax_error(parser, "a comprehension has exactly one element");
                }
                parsed = r_parse_comprehension_clauses(parser) && parsed;
                break;
            }
            if (!r_parser_eat(parser, R_TOKEN_COMMA)) {
                break;
            }
            if (r_parser_at(parser, R_TOKEN_RBRACKET)) {
                break;
            }
        }
    }
    (void)r_parser_expect(parser, R_TOKEN_RBRACKET, "expected ']' after collection expression");
    parsed = r_parser_close(parser, node) && parsed;
    r_parser_leave_nesting(parser);
    return parsed;
}

/* A call mode is contextual after fn; a result type named mut/once/shared remains legal. */
static bool r_parse_callable_mode(RParser *parser, bool lambda) {
    const RToken *token = r_parser_peek_token(parser);
    if (token == NULL || token->kind != R_TOKEN_IDENTIFIER ||
        (!r_parser_token_text_is(parser, token, "shared") &&
         !r_parser_token_text_is(parser, token, "mut") &&
         !r_parser_token_text_is(parser, token, "once")))
        return true;
    if (lambda) {
        if (r_parser_peek_n_kind(parser, 1U) == R_TOKEN_STAR ||
            r_parser_peek_n_kind(parser, 1U) == R_TOKEN_LBRACKET ||
            r_parser_peek_n_kind(parser, 1U) == R_TOKEN_LPAREN)
            return true;
        RParser trial = *parser;
        const size_t events = parser->source->cst_event_count;
        const bool had_error = parser->source->has_syntax_error;
        trial.suppress_diagnostics = true;
        parser->source->has_syntax_error = false;
        (void)r_parser_bump(&trial);
        const bool has_mode =
            r_parse_type(&trial, true) && r_parser_eat(&trial, R_TOKEN_IDENTIFIER) &&
            r_parser_at(&trial, R_TOKEN_LPAREN) && !parser->source->has_syntax_error;
        parser->source->cst_event_count = events;
        parser->source->has_syntax_error = had_error;
        if (!has_mode)
            return true;
    }
    const size_t mode = r_parser_open(parser, R_SYNTAX_CALLABLE_MODE);
    return r_parser_bump(parser) && r_parser_close(parser, mode);
}

static bool r_parse_generic_header(RParser *parser);

static bool r_parser_generic_attribute_ahead(const RParser *parser) {
    size_t index = r_skip_trivia(parser->source, parser->cursor);
    return r_scan_kind(parser->source, index) == R_TOKEN_AT &&
           r_parser_token_text_is(parser, r_scan_token(parser->source, index + 1U), "generic");
}

static void r_parse_misplaced_generic_header(RParser *parser) {
    size_t start = parser->generic_header_start;
    size_t end = parser->generic_header_end;
    (void)r_parser_syntax_error(
        parser, "@generic is only allowed on aggregate, function or hook declarations");
    (void)r_parse_generic_header(parser);
    parser->generic_header_start = start;
    parser->generic_header_end = end;
}

static bool r_attribute_name_is_known(const RParser *parser, const RToken *name) {
    static const char *const known_names[] = {
        "repr",
        "safety",
        "link",
        "header",
        "abi",
        "link_name",
        "fenv",
        "c_type",
        "c_constant",
        "export_name",
        "callback",
        "json",
        "deny_panic_alloc",
        "noalloc",
        "nonblocking",
        "must_use",
        "discardable",
        "lending",
        "chain",
        "scoped",
        "derive",
        "test",
        "recursion",
        "attribute",
    };
    size_t index;
    for (index = 0U; index < (sizeof(known_names) / sizeof(known_names[0])); ++index) {
        if (r_parser_token_text_is(parser, name, known_names[index])) {
            return true;
        }
    }
    return false;
}

/* R-GRAM-0007: these attributes take no arguments, so a parenthesized group after one that has
   a comma or `...` at its top level is the tuple type beginning the declaration, as in
   `@discardable (i32, bool) pair();` (R-TYPE-0052, R-TYPE-0053); any other group is an
   argument list, which the attribute rejects. */
static bool r_attribute_is_argument_free(const RParser *parser, const RToken *name) {
    static const char *const names[] = {
        "deny_panic_alloc",
        "noalloc",
        "nonblocking",
        "must_use",
        "discardable",
        "lending",
        "chain",
        "scoped",
        "callback",
    };
    for (size_t index = 0U; index < (sizeof(names) / sizeof(names[0])); ++index) {
        if (r_parser_token_text_is(parser, name, names[index])) {
            return true;
        }
    }
    return false;
}

static bool r_parenthesized_tuple_ahead(const RParser *parser) {
    size_t index = r_skip_trivia(parser->source, parser->cursor);
    uint32_t depth = UINT32_C(0);
    uint32_t angles = UINT32_C(0);

    if (r_scan_kind(parser->source, index) != R_TOKEN_LPAREN) {
        return false;
    }
    do {
        const RTokenKind kind = r_scan_kind(parser->source, index);
        if ((kind == R_TOKEN_EOF) || (kind == R_TOKEN_LBRACE) || (kind == R_TOKEN_SEMICOLON)) {
            return false;
        }
        if ((kind == R_TOKEN_LPAREN) || (kind == R_TOKEN_LBRACKET)) {
            depth += UINT32_C(1);
        } else if (((kind == R_TOKEN_RPAREN) || (kind == R_TOKEN_RBRACKET)) &&
                   (depth != UINT32_C(0))) {
            depth -= UINT32_C(1);
        } else if (kind == R_TOKEN_LESS) {
            angles += UINT32_C(1);
        } else if (kind == R_TOKEN_GREATER) {
            angles = angles != UINT32_C(0) ? angles - UINT32_C(1) : UINT32_C(0);
        } else if (kind == R_TOKEN_GREATER_GREATER) {
            angles = angles > UINT32_C(1) ? angles - UINT32_C(2) : UINT32_C(0);
        } else if ((depth == UINT32_C(1)) && (angles == UINT32_C(0)) &&
                   ((kind == R_TOKEN_COMMA) || (kind == R_TOKEN_ELLIPSIS))) {
            return true;
        }
        index = r_skip_trivia(parser->source, index + 1U);
    } while (depth != UINT32_C(0));
    return false;
}

static bool r_parse_attribute_value(RParser *parser) {
    RTokenKind kind = r_parser_peek_kind(parser);
    if ((kind == R_TOKEN_INTEGER_LITERAL) || (kind == R_TOKEN_STRING_LITERAL) ||
        (kind == R_TOKEN_FLOAT_LITERAL) || (kind == R_TOKEN_CHARACTER_LITERAL) ||
        (kind == R_TOKEN_KW_TRUE) || (kind == R_TOKEN_KW_FALSE) || (kind == R_TOKEN_KW_VARIANT)) {
        /* `variant` is a keyword and a target of `@attribute` (R-AGG-0013). */
        return r_parser_bump(parser);
    }
    if (kind == R_TOKEN_MINUS) {
        /* R-AGG-0013 (L42): a negative number argument of a user attribute. */
        const RTokenKind next = r_parser_peek_n_kind(parser, 1U);
        return ((next == R_TOKEN_INTEGER_LITERAL) || (next == R_TOKEN_FLOAT_LITERAL)) &&
               r_parser_bump(parser) && r_parser_bump(parser);
    }
    return r_parse_type(parser, true);
}

static bool r_parse_attribute(RParser *parser) {
    size_t node = r_parser_open(parser, R_SYNTAX_ATTRIBUTE);
    const RToken *name;
    bool json;
    bool argument_free;
    bool user;

    (void)r_parser_expect(parser, R_TOKEN_AT, "expected '@' before attribute");
    name = r_parser_peek_token(parser);
    /* R-AGG-0013 (L42): any other name, or a qualified one, names an attribute type as a type is
       named; the semantic pass resolves it and rejects a name that is no attribute type. */
    user = (name != NULL) && (name->kind == R_TOKEN_IDENTIFIER) &&
           (!r_attribute_name_is_known(parser, name) ||
            (r_parser_peek_n_kind(parser, 1U) == R_TOKEN_DOT) ||
            (r_parser_peek_n_kind(parser, 1U) == R_TOKEN_COLON_COLON));
    json = !user && (name != NULL) && r_parser_token_text_is(parser, name, "json");
    argument_free = !user && (name != NULL) && (name->kind == R_TOKEN_IDENTIFIER) &&
                    r_attribute_is_argument_free(parser, name);
    if ((name != NULL) && (name->kind == R_TOKEN_KW_DEFAULT)) {
        /* R-INIT-0005 (L33): the keyword names the attribute `@default` of an enum variant. */
        (void)r_parser_bump(parser);
    } else if (user) {
        (void)r_parse_type(parser, true);
    } else {
        (void)r_parser_expect(parser, R_TOKEN_IDENTIFIER, "expected attribute name");
    }
    if (!(argument_free && r_parenthesized_tuple_ahead(parser)) &&
        r_parser_eat(parser, R_TOKEN_LPAREN)) {
        if (!r_parser_at(parser, R_TOKEN_RPAREN)) {
            while (true) {
                if (json) {
                    RTokenKind key = r_parser_peek_kind(parser);
                    if (key != R_TOKEN_IDENTIFIER && key != R_TOKEN_KW_CASE &&
                        key != R_TOKEN_KW_DEFAULT) {
                        (void)r_parser_syntax_error(parser, "expected JSON parameter name");
                        break;
                    }
                    (void)r_parser_bump(parser);
                    if (r_parser_eat(parser, R_TOKEN_EQUAL) &&
                        !r_parse_expression_mode(parser, R_EXPRESSION_CALL_FREE).parsed) {
                        (void)r_parser_syntax_error(parser, "expected JSON parameter value");
                        break;
                    }
                    if (!r_parser_eat(parser, R_TOKEN_COMMA))
                        break;
                    if (r_parser_at(parser, R_TOKEN_RPAREN)) {
                        (void)r_parser_syntax_error(
                            parser, "trailing comma is not allowed in an attribute");
                        break;
                    }
                    continue;
                }
                if ((r_parser_peek_kind(parser) == R_TOKEN_IDENTIFIER) &&
                    (r_parser_peek_n_kind(parser, 1U) == R_TOKEN_EQUAL)) {
                    (void)r_parser_bump(parser);
                    (void)r_parser_bump(parser);
                }
                if (!r_parse_attribute_value(parser)) {
                    const RToken *token = r_parser_peek_token(parser);
                    RSourceSpan span =
                        token != NULL ? token->span : (RSourceSpan){parser->source_id, 0U, 0U};
                    (void)r_add_diagnostic(parser->context,
                                           "R-DIAG-SYN-002",
                                           "R-GRAM-0007",
                                           "malformed attribute value",
                                           R_DIAGNOSTIC_ERROR,
                                           span);
                    break;
                }
                if (!r_parser_eat(parser, R_TOKEN_COMMA)) {
                    break;
                }
                if (r_parser_at(parser, R_TOKEN_RPAREN)) {
                    (void)r_parser_syntax_error(parser,
                                                "trailing comma is not allowed in an attribute");
                    break;
                }
            }
        }
        (void)r_parser_expect(parser, R_TOKEN_RPAREN, "expected ')' after attribute arguments");
    }
    return r_parser_close(parser, node);
}

static void r_parse_attributes(RParser *parser) {
    while (r_parser_at(parser, R_TOKEN_AT)) {
        if (r_parser_generic_attribute_ahead(parser)) {
            r_parse_misplaced_generic_header(parser);
        } else {
            (void)r_parse_attribute(parser);
        }
    }
}

static bool r_parse_module_path_tokens(RParser *parser) {
    if (!r_parser_expect(parser, R_TOKEN_IDENTIFIER, "expected module path")) {
        return false;
    }
    while (r_parser_eat(parser, R_TOKEN_DOT)) {
        (void)r_parser_expect(parser, R_TOKEN_IDENTIFIER, "expected module path component");
    }
    return true;
}

static bool r_parser_module_declaration_ahead(const RParser *parser) {
    const size_t index = r_scan_skip_leading_attributes(parser->source, parser->cursor);

    return r_scan_kind(parser->source, index) == R_TOKEN_KW_MODULE;
}

static bool r_parse_module_declaration(RParser *parser) {
    size_t node = r_parser_open(parser, R_SYNTAX_MODULE_DECLARATION);
    r_parse_attributes(parser);
    (void)r_parser_expect(parser, R_TOKEN_KW_MODULE, "expected 'module'");
    (void)r_parse_module_path_tokens(parser);
    (void)r_parser_expect(parser, R_TOKEN_SEMICOLON, "expected ';' after module declaration");
    return r_parser_close(parser, node);
}

static bool r_parse_import_declaration(RParser *parser) {
    size_t node = r_parser_open(parser, R_SYNTAX_IMPORT_DECLARATION);
    (void)r_parser_bump(parser);
    (void)r_parse_module_path_tokens(parser);
    if (r_parser_eat(parser, R_TOKEN_COLON_COLON)) {
        (void)r_parser_expect(parser, R_TOKEN_LBRACE, "expected '{' after import '::'");
        if (r_parser_at(parser, R_TOKEN_RBRACE)) {
            (void)r_parser_missing(
                parser, R_TOKEN_IDENTIFIER, "selected import requires at least one identifier");
        } else {
            while (true) {
                (void)r_parser_expect(parser, R_TOKEN_IDENTIFIER, "expected imported identifier");
                if (!r_parser_eat(parser, R_TOKEN_COMMA)) {
                    break;
                }
                if (r_parser_at(parser, R_TOKEN_RBRACE)) {
                    break;
                }
            }
        }
        (void)r_parser_expect(parser, R_TOKEN_RBRACE, "expected '}' after selected imports");
    }
    (void)r_parser_expect(parser, R_TOKEN_SEMICOLON, "expected ';' after import declaration");
    return r_parser_close(parser, node);
}

static bool r_parse_field_declaration(RParser *parser) {
    size_t node = r_parser_open(parser, R_SYNTAX_FIELD_DECLARATION);
    r_parse_attributes(parser);
    (void)r_parser_eat(parser, R_TOKEN_KW_PROTECTED);
    if (!r_parse_type(parser, true)) {
        (void)r_parser_syntax_error(parser, "expected field type");
    }
    (void)r_parser_expect(parser, R_TOKEN_IDENTIFIER, "expected field name");
    /* R-INIT-0004 (L33): `= initializer` gives the field the value of an initialization that
       omits it. */
    if (r_parser_eat(parser, R_TOKEN_EQUAL) && !r_parse_initializer(parser, R_EXPRESSION_NORMAL)) {
        (void)r_parser_syntax_error(parser, "expected field initializer");
    }
    (void)r_parser_expect(parser, R_TOKEN_SEMICOLON, "expected ';' after field declaration");
    return r_parser_close(parser, node);
}

static bool r_parse_struct_declaration(RParser *parser, bool is_error) {
    size_t node = r_parser_open(
        parser, is_error ? R_SYNTAX_ERROR_STRUCT_DECLARATION : R_SYNTAX_STRUCT_DECLARATION);
    (void)r_parser_expect(parser,
                          is_error ? R_TOKEN_IDENTIFIER : R_TOKEN_KW_STRUCT,
                          is_error ? "expected error" : "expected struct");
    (void)r_parser_expect(parser, R_TOKEN_IDENTIFIER, "expected struct name");
    if (is_error && r_parser_eat(parser, R_TOKEN_COLON)) {
        /* R-AGG-0011 (L21.1): the parent error whose fields this error extends. */
        if (!r_parse_type(parser, true)) {
            (void)r_parser_syntax_error(parser, "expected parent error type");
        }
    }
    (void)r_parser_expect(parser, R_TOKEN_LBRACE, "expected '{' after struct name");
    while (!r_parser_at(parser, R_TOKEN_RBRACE) && !r_parser_at(parser, R_TOKEN_EOF)) {
        size_t before = parser->cursor;
        (void)r_parse_field_declaration(parser);
        if (parser->cursor == before) {
            static const RTokenKind synchronizers[] = {R_TOKEN_SEMICOLON, R_TOKEN_RBRACE};
            (void)r_parser_recover(
                parser, synchronizers, sizeof(synchronizers) / sizeof(synchronizers[0]));
            (void)r_parser_eat(parser, R_TOKEN_SEMICOLON);
        }
    }
    (void)r_parser_expect(parser, R_TOKEN_RBRACE, "expected '}' after struct fields");
    (void)r_parser_expect(parser, R_TOKEN_SEMICOLON, "expected ';' after struct declaration");
    return r_parser_close(parser, node);
}

static bool r_parse_enum_variant(RParser *parser) {
    size_t node = r_parser_open(parser, R_SYNTAX_ENUM_VARIANT);
    RExpressionInfo value;
    /* R-INIT-0005 (L33): `@default` selects the default variant. */
    r_parse_attributes(parser);

    (void)r_parser_expect(parser, R_TOKEN_IDENTIFIER, "expected enum variant name");
    if (r_parser_eat(parser, R_TOKEN_EQUAL)) {
        value = r_parse_expression_mode(parser, R_EXPRESSION_CONSTANT);
        if (!value.parsed) {
            (void)r_parser_syntax_error(parser, "expected enum constant expression");
        }
    } else if (r_parser_eat(parser, R_TOKEN_LPAREN)) {
        if (!r_parse_type(parser, true)) {
            (void)r_parser_syntax_error(parser, "expected enum payload type");
        }
        (void)r_parser_expect(parser, R_TOKEN_RPAREN, "expected ')' after enum payload type");
    } else if (r_parser_eat(parser, R_TOKEN_LBRACE)) {
        if (r_parser_at(parser, R_TOKEN_RBRACE)) {
            (void)r_parser_missing(
                parser, R_TOKEN_IDENTIFIER, "structured enum variant requires at least one field");
        }
        while (!r_parser_at(parser, R_TOKEN_RBRACE) && !r_parser_at(parser, R_TOKEN_EOF)) {
            const size_t before = parser->cursor;
            (void)r_parse_field_declaration(parser);
            if (parser->cursor == before) {
                static const RTokenKind synchronizers[] = {R_TOKEN_SEMICOLON, R_TOKEN_RBRACE};
                (void)r_parser_recover(
                    parser, synchronizers, sizeof(synchronizers) / sizeof(synchronizers[0]));
                (void)r_parser_eat(parser, R_TOKEN_SEMICOLON);
            }
        }
        (void)r_parser_expect(parser, R_TOKEN_RBRACE, "expected '}' after enum fields");
    }
    return r_parser_close(parser, node);
}

static bool r_parse_enum_declaration(RParser *parser, bool is_error) {
    size_t node = r_parser_open(
        parser, is_error ? R_SYNTAX_ERROR_ENUM_DECLARATION : R_SYNTAX_ENUM_DECLARATION);
    (void)r_parser_expect(parser,
                          is_error ? R_TOKEN_IDENTIFIER : R_TOKEN_KW_ENUM,
                          is_error ? "expected error" : "expected enum");
    (void)r_parser_expect(parser, R_TOKEN_IDENTIFIER, "expected enum name");
    if (r_parser_eat(parser, R_TOKEN_COLON)) {
        if (!r_token_is_r_integer_type(r_parser_peek_kind(parser)) &&
            !r_token_is_c_integer_type(r_parser_peek_kind(parser))) {
            (void)r_parser_syntax_error(parser, "expected integer enum underlying type");
        } else {
            (void)r_parser_bump(parser);
        }
    }
    (void)r_parser_expect(parser, R_TOKEN_LBRACE, "expected '{' after enum name");
    if (r_parser_at(parser, R_TOKEN_RBRACE)) {
        (void)r_parser_missing(
            parser, R_TOKEN_IDENTIFIER, "enum declaration requires at least one variant");
    } else {
        while (true) {
            (void)r_parse_enum_variant(parser);
            if (!r_parser_eat(parser, R_TOKEN_COMMA)) {
                break;
            }
            if (r_parser_at(parser, R_TOKEN_RBRACE)) {
                break;
            }
        }
    }
    (void)r_parser_expect(parser, R_TOKEN_RBRACE, "expected '}' after enum variants");
    (void)r_parser_expect(parser, R_TOKEN_SEMICOLON, "expected ';' after enum declaration");
    return r_parser_close(parser, node);
}

static bool r_parse_error_declaration(RParser *parser) {
    size_t index = r_skip_trivia(parser->source, parser->cursor);
    RTokenKind kind;
    uint32_t parentheses = 0U;

    index = r_skip_trivia(parser->source, index + 1U); /* name */
    index = r_skip_trivia(parser->source, index + 1U); /* colon or body */
    if (r_scan_kind(parser->source, index) == R_TOKEN_COLON) {
        /* R-AGG-0004: an integer type after ':' represents a fieldless error; any other name is
           the parent of an error with fields (R-AGG-0011, L21.1). */
        if (r_token_is_primitive_type(
                r_scan_kind(parser->source, r_skip_trivia(parser->source, index + 1U)))) {
            return r_parse_enum_declaration(parser, true);
        }
        return r_parse_struct_declaration(parser, true);
    }
    /* The first member, after its attributes (`@json` of a field, `@default` of a variant). */
    index = r_skip_trivia(
        parser->source,
        r_scan_skip_leading_attributes(parser->source, r_skip_trivia(parser->source, index + 1U)));
    if (r_scan_kind(parser->source, index) != R_TOKEN_IDENTIFIER) {
        return r_parse_struct_declaration(parser, true);
    }
    index = r_skip_trivia(parser->source, index + 1U);
    kind = r_scan_kind(parser->source, index);
    if (kind == R_TOKEN_LPAREN) {
        /* A payload ends at ',' or '}', whereas a parametric field type has a name. */
        do {
            kind = r_scan_kind(parser->source, index);
            if (kind == R_TOKEN_LPAREN) {
                parentheses += 1U;
            } else if (kind == R_TOKEN_RPAREN) {
                parentheses -= 1U;
            } else if (kind == R_TOKEN_EOF) {
                break;
            }
            index = r_skip_trivia(parser->source, index + 1U);
        } while (parentheses != 0U);
        kind = r_scan_kind(parser->source, index);
    }
    if ((kind == R_TOKEN_COMMA) || (kind == R_TOKEN_RBRACE) || (kind == R_TOKEN_EQUAL) ||
        (kind == R_TOKEN_LBRACE)) {
        return r_parse_enum_declaration(parser, true);
    }
    return r_parse_struct_declaration(parser, true);
}

static bool r_parse_parameter_list(RParser *parser, bool c_variadic) {
    size_t node = r_parser_open(parser, R_SYNTAX_PARAMETER_LIST);
    bool first_parameter = true;
    bool variadic_slice = false;
    (void)r_parser_expect(parser, R_TOKEN_LPAREN, "expected '('");
    if (!r_parser_at(parser, R_TOKEN_RPAREN)) {
        while (true) {
            size_t parameter;
            parameter = r_parser_open(parser, R_SYNTAX_PARAMETER);
            while (r_parser_generic_attribute_ahead(parser)) {
                r_parse_misplaced_generic_header(parser);
            }
            if (r_parser_at(parser, R_TOKEN_KW_VOID)) {
                (void)r_parser_syntax_error(parser, "void is not a function parameter type");
            }
            if (!r_parse_parameter_type(parser)) {
                (void)r_parser_syntax_error(parser, "expected parameter type");
            }
            if (!c_variadic && r_parser_at(parser, R_TOKEN_ELLIPSIS)) {
                /* variadic-parameter = type, "...", identifier ; only the last parameter
                   (R-FUNC-0018). */
                (void)r_parser_bump(parser);
                variadic_slice = true;
            }
            if (r_parser_at(parser, R_TOKEN_KW_THIS)) {
                /* R-FUNC-0013: the receiver is the first parameter of a method. */
                if (!first_parameter) {
                    (void)r_parser_syntax_error(parser, "this shall be the first parameter");
                }
                (void)r_parser_bump(parser);
            } else {
                (void)r_parser_expect(parser, R_TOKEN_IDENTIFIER, "expected parameter name");
            }
            first_parameter = false;
            (void)r_parser_close(parser, parameter);
            if (!r_parser_eat(parser, R_TOKEN_COMMA)) {
                break;
            }
            if (variadic_slice) {
                (void)r_parser_syntax_error(parser, "a variadic parameter shall be the last");
                variadic_slice = false;
            }
            if (c_variadic && r_parser_at(parser, R_TOKEN_ELLIPSIS)) {
                (void)r_parser_bump(parser);
                break;
            }
        }
    }
    (void)r_parser_expect(parser, R_TOKEN_RPAREN, "expected ')' after parameters");
    return r_parser_close(parser, node);
}

static bool r_parse_function_name(RParser *parser) {
    size_t associated = SIZE_MAX;
    const bool applied = r_applied_type_tail(parser) == R_TOKEN_COLON_COLON;
    if (applied || r_parser_peek_n_kind(parser, 1U) == R_TOKEN_COLON_COLON) {
        associated = r_parser_open(parser, R_SYNTAX_ASSOCIATED_NAME);
    }
    if (applied) {
        (void)r_parse_type(parser, true);
    } else {
        (void)r_parser_expect(parser, R_TOKEN_IDENTIFIER, "expected function name");
    }
    if (r_parser_eat(parser, R_TOKEN_COLON_COLON)) {
        /* R-FUNC-0013: any identifier names a method or associated function; the hook
           names keep their R-FUNC-0009 contract in the semantic pass. */
        (void)r_parser_expect(parser, R_TOKEN_IDENTIFIER, "expected associated function name");
    }
    return associated == SIZE_MAX || r_parser_close(parser, associated);
}

/* trait-name = identifier | module-path, "::", identifier (R-TYPE-0041). */
static bool r_parse_trait_name(RParser *parser) {
    size_t node = r_parser_open(parser, R_SYNTAX_TRAIT_NAME);
    (void)r_parser_expect(parser, R_TOKEN_IDENTIFIER, "expected trait name");
    while (r_parser_eat(parser, R_TOKEN_DOT)) {
        (void)r_parser_expect(parser, R_TOKEN_IDENTIFIER, "expected module path component");
    }
    if (r_parser_eat(parser, R_TOKEN_COLON_COLON)) {
        (void)r_parser_expect(parser, R_TOKEN_IDENTIFIER, "expected trait name after '::'");
    }
    if (r_parser_at(parser, R_TOKEN_LESS))
        (void)r_parse_type_argument_list(parser, true, 1U, UINT32_MAX);
    return r_parser_close(parser, node);
}

static bool r_trait_application_ahead(const RParser *parser) {
    size_t index = r_skip_trivia(parser->source, parser->cursor);
    if (r_scan_kind(parser->source, index) != R_TOKEN_IDENTIFIER)
        return false;
    index = r_skip_trivia(parser->source, index + 1U);
    while (r_scan_kind(parser->source, index) == R_TOKEN_DOT ||
           r_scan_kind(parser->source, index) == R_TOKEN_COLON_COLON) {
        index = r_skip_trivia(parser->source, index + 1U);
        if (r_scan_kind(parser->source, index) != R_TOKEN_IDENTIFIER)
            return false;
        index = r_skip_trivia(parser->source, index + 1U);
    }
    return r_scan_kind(parser->source, index) == R_TOKEN_LESS;
}

static bool r_parse_block(RParser *parser);

/* A trait prototype or an impl definition, wrapped like a module-level function so that
   the semantic collector reads its modifiers and signature unchanged (R-TYPE-0041/0042). */
static bool r_parse_member_function(RParser *parser, bool require_body) {
    size_t node = r_parser_open(parser, R_SYNTAX_EXTERNAL_DECLARATION);
    size_t declaration;
    bool async_function;

    r_parse_attributes(parser);
    if (r_parser_at(parser, R_TOKEN_KW_PROTECTED)) {
        (void)r_parser_syntax_error(parser, "trait and impl members do not accept protected");
        (void)r_parser_bump(parser);
    }
    (void)r_parser_eat(parser, R_TOKEN_KW_UNSAFE);
    async_function = r_parser_eat(parser, R_TOKEN_KW_ASYNC);
    declaration = r_parser_open(parser, R_SYNTAX_FUNCTION_DECLARATION);
    if (!r_parse_type(parser, true)) {
        (void)r_parser_syntax_error(parser, "expected member function type");
    }
    (void)r_parser_expect(parser, R_TOKEN_IDENTIFIER, "expected member function name");
    (void)r_parse_parameter_list(parser, false);
    if (r_parser_at(parser, R_TOKEN_KW_THROWS)) {
        (void)r_parse_throws_clause(parser);
    }
    if (r_parser_at(parser, R_TOKEN_LBRACE)) {
        bool previous_async_context = parser->in_async_function;
        parser->in_async_function = async_function;
        (void)r_parse_block(parser);
        parser->in_async_function = previous_async_context;
    } else {
        if (require_body) {
            (void)r_parser_syntax_error(parser, "impl member requires a body");
        }
        (void)r_parser_expect(parser, R_TOKEN_SEMICOLON, "expected ';' after trait member");
    }
    (void)r_parser_close(parser, declaration);
    return r_parser_close(parser, node);
}

/* associated-type-declaration = "type", identifier, ";" (trait) and
   associated-type-binding = "type", identifier, "=", type, ";" (impl); `type` is contextual
   (R-TYPE-0045). */
static bool r_associated_type_ahead(const RParser *parser) {
    size_t index = r_skip_trivia(parser->source, parser->cursor);
    RTokenKind tail;

    if ((r_scan_kind(parser->source, index) != R_TOKEN_IDENTIFIER) ||
        !r_parser_token_index_text_is(parser, index, "type")) {
        return false;
    }
    index = r_skip_trivia(parser->source, index + 1U);
    if (r_scan_kind(parser->source, index) != R_TOKEN_IDENTIFIER) {
        return false;
    }
    tail = r_scan_kind(parser->source, r_skip_trivia(parser->source, index + 1U));
    return (tail == R_TOKEN_SEMICOLON) || (tail == R_TOKEN_EQUAL) || (tail == R_TOKEN_COLON) ||
           (tail == R_TOKEN_LESS);
}

static bool r_parse_generic_constraint_list(RParser *parser);

static bool r_parse_associated_type(RParser *parser, bool require_body) {
    size_t node = r_parser_open(parser, R_SYNTAX_ASSOCIATED_TYPE);
    (void)r_parser_bump(parser);
    (void)r_parser_expect(parser, R_TOKEN_IDENTIFIER, "expected associated type name");
    if (r_parser_eat(parser, R_TOKEN_LESS)) {
        /* R-TYPE-0045 (L16.3): the type parameters of an associated type; an implementation
           repeats their names and the trait states their constraints. */
        do {
            size_t parameter = r_parser_open(parser, R_SYNTAX_GENERIC_PARAMETER);
            (void)r_parser_expect(
                parser, R_TOKEN_IDENTIFIER, "expected associated type parameter name");
            if (r_parser_eat(parser, R_TOKEN_COLON)) {
                if (require_body) {
                    (void)r_parser_syntax_error(
                        parser, "associated type parameter constraints belong to the trait");
                }
                (void)r_parse_generic_constraint_list(parser);
            }
            (void)r_parser_close(parser, parameter);
        } while (r_parser_eat(parser, R_TOKEN_COMMA));
        (void)r_parser_expect_type_close(parser, "expected '>' after associated type parameters");
    }
    if (r_parser_eat(parser, R_TOKEN_COLON)) {
        if (require_body)
            (void)r_parser_syntax_error(parser, "associated type constraints belong to the trait");
        do {
            /* Annex A admits any generic-constraint; R-TYPE-0045 then limits the kinds. */
            if (r_parser_at(parser, R_TOKEN_KW_FN) || r_parser_at(parser, R_TOKEN_KW_ASYNC))
                (void)r_parse_callable_constraint(parser);
            else
                (void)r_parse_trait_name(parser);
        } while (r_parser_eat(parser, R_TOKEN_AMP));
    }
    if (r_parser_at(parser, R_TOKEN_EQUAL)) {
        if (!require_body) {
            (void)r_parser_syntax_error(parser,
                                        "a trait declares an associated type without a binding");
        }
        (void)r_parser_bump(parser);
        if (!r_parse_type(parser, true)) {
            (void)r_parser_syntax_error(parser, "expected the bound associated type");
        }
    } else if (require_body) {
        (void)r_parser_syntax_error(parser,
                                    "an implementation binds every associated type with '='");
    }
    (void)r_parser_expect(parser, R_TOKEN_SEMICOLON, "expected ';' after associated type");
    return r_parser_close(parser, node);
}

/* R-TYPE-0050: `const T NAME;` or `const T NAME = value;` where T is an integer type or bool. */
static bool r_associated_constant_ahead(const RParser *parser) {
    size_t index = r_skip_trivia(parser->source, parser->cursor);
    RTokenKind kind;
    if (r_scan_kind(parser->source, index) != R_TOKEN_KW_CONST) {
        return false;
    }
    index = r_skip_trivia(parser->source, index + 1U);
    kind = r_scan_kind(parser->source, index);
    if (!r_token_is_primitive_type(kind) && kind != R_TOKEN_KW_STR && kind != R_TOKEN_IDENTIFIER) {
        return false;
    }
    index = r_skip_trivia(parser->source, index + 1U);
    if (r_scan_kind(parser->source, index) != R_TOKEN_IDENTIFIER) {
        return false;
    }
    kind = r_scan_kind(parser->source, r_skip_trivia(parser->source, index + 1U));
    return (kind == R_TOKEN_SEMICOLON) || (kind == R_TOKEN_EQUAL);
}

static bool r_parse_associated_constant(RParser *parser, bool require_body) {
    size_t node = r_parser_open(parser, R_SYNTAX_ASSOCIATED_CONSTANT);
    RTokenKind kind;
    (void)r_parser_bump(parser);
    kind = r_parser_peek_kind(parser);
    if (kind != R_TOKEN_KW_I8 && kind != R_TOKEN_KW_I16 && kind != R_TOKEN_KW_I32 &&
        kind != R_TOKEN_KW_I64 && kind != R_TOKEN_KW_ISIZE && kind != R_TOKEN_KW_U8 &&
        kind != R_TOKEN_KW_U16 && kind != R_TOKEN_KW_U32 && kind != R_TOKEN_KW_U64 &&
        kind != R_TOKEN_KW_USIZE && kind != R_TOKEN_KW_BOOL) {
        (void)r_parser_syntax_error(parser, "associated constants require an integer type or bool");
    }
    (void)r_parser_bump(parser);
    (void)r_parser_expect(parser, R_TOKEN_IDENTIFIER, "expected associated constant name");
    if (r_parser_eat(parser, R_TOKEN_EQUAL)) {
        if (!r_parse_expression_mode(parser, R_EXPRESSION_NORMAL).parsed) {
            (void)r_parser_syntax_error(parser, "expected the associated constant value");
        }
    } else if (require_body) {
        (void)r_parser_syntax_error(parser,
                                    "an implementation binds every associated constant with '='");
    }
    (void)r_parser_expect(parser, R_TOKEN_SEMICOLON, "expected ';' after associated constant");
    return r_parser_close(parser, node);
}

static void r_parse_member_functions(RParser *parser, bool require_body) {
    while (!r_parser_at(parser, R_TOKEN_RBRACE) && !r_parser_at(parser, R_TOKEN_EOF)) {
        size_t before = parser->cursor;
        if (r_associated_type_ahead(parser)) {
            (void)r_parse_associated_type(parser, require_body);
        } else if (r_associated_constant_ahead(parser)) {
            (void)r_parse_associated_constant(parser, require_body);
        } else {
            (void)r_parse_member_function(parser, require_body);
        }
        if (parser->cursor == before) {
            static const RTokenKind synchronizers[] = {R_TOKEN_SEMICOLON, R_TOKEN_RBRACE};
            (void)r_parser_recover(
                parser, synchronizers, sizeof(synchronizers) / sizeof(synchronizers[0]));
            (void)r_parser_eat(parser, R_TOKEN_SEMICOLON);
        }
    }
}

/* trait-declaration = "trait", identifier, "{", { trait-method-declaration }, "}", ";" */
static bool r_parse_trait_declaration(RParser *parser) {
    size_t node = r_parser_open(parser, R_SYNTAX_TRAIT_DECLARATION);
    (void)r_parser_expect(parser, R_TOKEN_KW_TRAIT, "expected trait");
    (void)r_parser_expect(parser, R_TOKEN_IDENTIFIER, "expected trait name");
    if (r_parser_eat(parser, R_TOKEN_COLON)) {
        do {
            (void)r_parse_trait_name(parser);
        } while (r_parser_eat(parser, R_TOKEN_AMP));
    }
    (void)r_parser_expect(parser, R_TOKEN_LBRACE, "expected '{' after trait name");
    r_parse_member_functions(parser, false);
    (void)r_parser_expect(parser, R_TOKEN_RBRACE, "expected '}' after trait members");
    (void)r_parser_expect(parser, R_TOKEN_SEMICOLON, "expected ';' after trait declaration");
    return r_parser_close(parser, node);
}

/* impl-declaration = "impl", trait-name, "for", type, "{", { impl-method-definition }, "}", ";" */
static bool r_parse_impl_declaration(RParser *parser) {
    size_t node = r_parser_open(parser, R_SYNTAX_IMPL_DECLARATION);
    (void)r_parser_expect(parser, R_TOKEN_KW_IMPL, "expected impl");
    (void)r_parse_trait_name(parser);
    (void)r_parser_expect(parser, R_TOKEN_KW_FOR, "expected 'for' after the implemented trait");
    if (!r_parse_type(parser, true)) {
        (void)r_parser_syntax_error(parser, "expected impl target type");
    }
    (void)r_parser_expect(parser, R_TOKEN_LBRACE, "expected '{' after impl target");
    r_parse_member_functions(parser, true);
    (void)r_parser_expect(parser, R_TOKEN_RBRACE, "expected '}' after impl members");
    (void)r_parser_expect(parser, R_TOKEN_SEMICOLON, "expected ';' after impl declaration");
    return r_parser_close(parser, node);
}

static bool r_parse_await_operation(RParser *parser) {
    size_t node = r_parser_open(parser, R_SYNTAX_AWAIT_OPERATION);
    if (!parser->in_async_function) {
        const RToken *token = r_parser_peek_token(parser);
        RSourceSpan span = token != NULL ? token->span : (RSourceSpan){parser->source_id, 0U, 0U};
        (void)r_add_diagnostic(parser->context,
                               "R-DIAG-ASYNC-001",
                               "R-STMT-0012",
                               "await is permitted only inside an async function",
                               R_DIAGNOSTIC_ERROR,
                               span);
    }
    (void)r_parser_expect(parser, R_TOKEN_KW_AWAIT, "expected await");
    if (r_parser_eat(parser, R_TOKEN_KW_MOVE)) {
        (void)r_parser_expect(parser, R_TOKEN_IDENTIFIER, "await move requires a named task");
    } else {
        const size_t expression_start = parser->source->cst_event_count;
        const RExpressionInfo expression = r_parse_postfix_expression(parser, R_EXPRESSION_NORMAL);
        RSyntaxKind last = R_SYNTAX_ERROR;
        size_t event_index = expression_start;
        const size_t expression_end = parser->source->cst_event_count;
        while (event_index < expression_end &&
               parser->source->cst_events[event_index].kind != R_CST_EVENT_OPEN) {
            ++event_index;
        }
        ++event_index;
        while (event_index < expression_end) {
            const RCstEvent event = parser->source->cst_events[event_index];
            if (event.kind == R_CST_EVENT_OPEN) {
                last = event.syntax_kind;
                event_index = (size_t)event.matching_event + 1U;
            } else {
                ++event_index;
            }
        }
        if (!expression.parsed ||
            (last != R_SYNTAX_CALL_SUFFIX && last != R_SYNTAX_METHOD_CALL_SUFFIX)) {
            (void)r_parser_syntax_error(
                parser, "await requires a direct function or method call, or move name");
        }
    }
    return r_parser_close(parser, node);
}

/* R-STMT-0022 (L37.3): `auto (a, b, ...) = initializer;` binds the elements of a tuple. */
static bool r_parse_destructuring_declaration(RParser *parser) {
    size_t node = r_parser_open(parser, R_SYNTAX_DESTRUCTURING_DECLARATION);
    (void)r_parser_expect(parser, R_TOKEN_KW_AUTO, "expected 'auto'");
    (void)r_parser_expect(parser, R_TOKEN_LPAREN, "expected '(' before the bound names");
    do {
        (void)r_parser_expect(parser, R_TOKEN_IDENTIFIER, "expected a name to bind");
    } while (r_parser_eat(parser, R_TOKEN_COMMA));
    (void)r_parser_expect(parser, R_TOKEN_RPAREN, "expected ')' after the bound names");
    (void)r_parser_expect(parser, R_TOKEN_EQUAL, "destructuring declaration requires initializer");
    if (!r_parse_initializer(parser, R_EXPRESSION_NORMAL)) {
        (void)r_parser_syntax_error(parser, "expected object initializer");
    }
    (void)r_parser_expect(parser, R_TOKEN_SEMICOLON, "expected ';' after object declaration");
    return r_parser_close(parser, node);
}

static bool r_parse_object_declaration(RParser *parser, bool with_semicolon) {
    if (with_semicolon && r_parser_at(parser, R_TOKEN_KW_AUTO) &&
        (r_parser_peek_n_kind(parser, 1U) == R_TOKEN_LPAREN)) {
        return r_parse_destructuring_declaration(parser);
    }
    size_t node = r_parser_open(parser, R_SYNTAX_OBJECT_DECLARATION);
    if (r_parser_eat(parser, R_TOKEN_KW_STATIC)) {
        if (r_parser_eat(parser, R_TOKEN_KW_THREAD_LOCAL)) {
            (void)r_parser_syntax_error(parser, "object declaration accepts one storage specifier");
        }
    } else {
        (void)r_parser_eat(parser, R_TOKEN_KW_THREAD_LOCAL);
    }
    if (r_parser_at(parser, R_TOKEN_KW_AUTO)) {
        /* R-NAME-0011: the declared type is inferred from the initializer. */
        size_t inferred = r_parser_open(parser, R_SYNTAX_TYPE);
        (void)r_parser_bump(parser);
        (void)r_parser_close(parser, inferred);
    } else if (!r_parse_type(parser, true)) {
        (void)r_parser_syntax_error(parser, "expected object type");
    }
    (void)r_parser_expect(parser, R_TOKEN_IDENTIFIER, "expected object name");
    (void)r_parser_expect(parser, R_TOKEN_EQUAL, "object declaration requires initializer");
    if (!r_parse_initializer(parser, R_EXPRESSION_NORMAL)) {
        (void)r_parser_syntax_error(parser, "expected object initializer");
    }
    if (with_semicolon) {
        (void)r_parser_expect(parser, R_TOKEN_SEMICOLON, "expected ';' after object declaration");
    }
    return r_parser_close(parser, node);
}

static bool r_parse_drop_definition(RParser *parser) {
    size_t node = r_parser_open(parser, R_SYNTAX_DROP_DEFINITION);
    (void)r_parser_expect(parser, R_TOKEN_KW_DROP, "expected drop");
    (void)r_parse_parameter_list(parser, false);
    (void)r_parse_block(parser);
    return r_parser_close(parser, node);
}

static bool r_parse_c_declaration(RParser *parser) {
    size_t node = r_parser_open(parser, R_SYNTAX_C_DECLARATION);

    r_parse_attributes(parser);
    if (r_parser_eat(parser, R_TOKEN_KW_OPAQUE)) {
        (void)r_parser_expect(parser, R_TOKEN_KW_STRUCT, "expected struct after opaque");
        (void)r_parser_expect(parser, R_TOKEN_IDENTIFIER, "expected opaque struct name");
        (void)r_parser_expect(parser, R_TOKEN_SEMICOLON, "expected ';' after opaque struct");
    } else if (r_parser_at(parser, R_TOKEN_KW_STRUCT)) {
        (void)r_parse_struct_declaration(parser, false);
    } else if (r_parser_at(parser, R_TOKEN_KW_ENUM)) {
        (void)r_parse_enum_declaration(parser, false);
    } else {
        bool thread_local_declaration = r_parser_eat(parser, R_TOKEN_KW_THREAD_LOCAL);
        bool unsafe_declaration = r_parser_eat(parser, R_TOKEN_KW_UNSAFE);
        if (!r_parse_type(parser, true)) {
            (void)r_parser_syntax_error(parser, "expected C declaration type");
        }
        (void)r_parser_expect(parser, R_TOKEN_IDENTIFIER, "expected C declaration name");
        if (r_parser_at(parser, R_TOKEN_LPAREN)) {
            if (thread_local_declaration) {
                (void)r_parser_syntax_error(parser,
                                            "C function declaration cannot be thread_local");
            }
            (void)r_parse_parameter_list(parser, true);
            (void)r_parser_expect(parser, R_TOKEN_SEMICOLON, "expected ';' after C function");
        } else if (r_parser_eat(parser, R_TOKEN_EQUAL)) {
            if (thread_local_declaration || unsafe_declaration) {
                (void)r_parser_syntax_error(
                    parser, "C constant declaration has no storage or unsafe specifier");
            }
            RExpressionInfo constant = r_parse_expression_mode(parser, R_EXPRESSION_CONSTANT);
            if (!constant.parsed) {
                (void)r_parser_syntax_error(parser, "expected C constant expression");
            }
            (void)r_parser_expect(parser, R_TOKEN_SEMICOLON, "expected ';' after C constant");
        } else {
            if (unsafe_declaration) {
                (void)r_parser_syntax_error(parser, "C object declaration cannot be unsafe");
            }
            (void)r_parser_expect(parser, R_TOKEN_SEMICOLON, "expected ';' after C object");
        }
    }
    return r_parser_close(parser, node);
}

static bool r_parse_extern_block(RParser *parser) {
    size_t node = r_parser_open(parser, R_SYNTAX_EXTERN_BLOCK);
    const RToken *abi_literal;
    (void)r_parser_expect(parser, R_TOKEN_KW_EXTERN, "expected extern");
    abi_literal = r_parser_peek_token(parser);
    if ((abi_literal != NULL) && (abi_literal->kind == R_TOKEN_STRING_LITERAL) &&
        !r_abi_literal_is_c(parser, abi_literal)) {
        (void)r_add_diagnostic(parser->context,
                               "R-DIAG-SYN-001",
                               "R-FFI-0013",
                               "extern ABI string shall contain exactly C",
                               R_DIAGNOSTIC_ERROR,
                               abi_literal->span);
    }
    (void)r_parser_expect(parser, R_TOKEN_STRING_LITERAL, "expected C ABI string");
    (void)r_parser_expect(parser, R_TOKEN_LBRACE, "expected '{' after extern ABI");
    while (!r_parser_at(parser, R_TOKEN_RBRACE) && !r_parser_at(parser, R_TOKEN_EOF)) {
        size_t before = parser->cursor;
        (void)r_parse_c_declaration(parser);
        if (parser->cursor == before) {
            static const RTokenKind synchronizers[] = {R_TOKEN_SEMICOLON, R_TOKEN_RBRACE};
            (void)r_parser_recover(
                parser, synchronizers, sizeof(synchronizers) / sizeof(synchronizers[0]));
            (void)r_parser_eat(parser, R_TOKEN_SEMICOLON);
        }
    }
    (void)r_parser_expect(parser, R_TOKEN_RBRACE, "expected '}' after extern block");
    return r_parser_close(parser, node);
}

static bool r_extern_block_ahead(const RParser *parser) {
    return r_parser_peek_kind(parser) == R_TOKEN_KW_EXTERN &&
           r_parser_peek_n_kind(parser, 1U) == R_TOKEN_STRING_LITERAL &&
           r_parser_peek_n_kind(parser, 2U) == R_TOKEN_LBRACE;
}

static bool r_parse_fn_resource_attributes(RParser *parser) {
    while (r_parser_at(parser, R_TOKEN_AT)) {
        const RToken *name =
            r_scan_token(parser->source, r_skip_trivia(parser->source, parser->cursor) + 1U);
        if (name == NULL || (!r_parser_token_text_is(parser, name, "noalloc") &&
                             !r_parser_token_text_is(parser, name, "nonblocking"))) {
            (void)r_parser_syntax_error(parser,
                                        "a callable signature permits only resource attributes");
        }
        /* The following '(' belongs to the callable parameters, not this attribute. */
        const size_t attribute = r_parser_open(parser, R_SYNTAX_ATTRIBUTE);
        if (!r_parser_bump(parser) ||
            !r_parser_expect(parser, R_TOKEN_IDENTIFIER, "expected resource attribute name") ||
            !r_parser_close(parser, attribute))
            return false;
    }
    return true;
}

/* Parentheses keep a callable's checked errors separate from generic parameters. */
static bool r_parse_callable_throws(RParser *parser) {
    if (!r_parser_at(parser, R_TOKEN_KW_THROWS))
        return true;
    const size_t node = r_parser_open(parser, R_SYNTAX_THROWS_CLAUSE);
    (void)r_parser_bump(parser);
    (void)r_parser_expect(parser, R_TOKEN_LPAREN, "expected '(' before callable errors");
    do {
        if (!r_parse_type(parser, true)) {
            (void)r_parser_syntax_error(parser, "expected checked callable error type");
            break;
        }
    } while (r_parser_eat(parser, R_TOKEN_COMMA));
    (void)r_parser_expect(parser, R_TOKEN_RPAREN, "expected ')' after callable errors");
    return r_parser_close(parser, node);
}

/* `c & Trait<T> & fn(P) -> R`, the constraints after the `:` of a generic parameter. */
static bool r_parse_generic_constraint_list(RParser *parser) {
    do {
        if (r_trait_application_ahead(parser)) {
            (void)r_parse_trait_name(parser);
            continue;
        }
        if (r_parser_at(parser, R_TOKEN_KW_FN) || r_parser_at(parser, R_TOKEN_KW_ASYNC)) {
            (void)r_parse_callable_constraint(parser);
            continue;
        }
        (void)r_parser_expect(parser, R_TOKEN_IDENTIFIER, "expected generic constraint");
        /* R-TYPE-0043: a trait constraint may be module-qualified. */
        while (r_parser_eat(parser, R_TOKEN_DOT)) {
            (void)r_parser_expect(parser, R_TOKEN_IDENTIFIER, "expected module path component");
        }
        if (r_parser_eat(parser, R_TOKEN_COLON_COLON)) {
            (void)r_parser_expect(parser, R_TOKEN_IDENTIFIER, "expected trait name after '::'");
        }
    } while (r_parser_eat(parser, R_TOKEN_AMP));
    return true;
}

static bool r_parse_generic_header(RParser *parser) {
    size_t node = r_parser_open(parser, R_SYNTAX_GENERIC_HEADER);
    (void)r_parser_eat(parser, R_TOKEN_AT);
    (void)r_parser_bump(parser);
    /* R-TYPE-0031: `@generic<...>`; the retired parenthesized header is diagnosed and still
       parsed so that the declaration after it recovers. */
    const bool parenthesized = r_parser_at(parser, R_TOKEN_LPAREN);
    if (parenthesized) {
        (void)r_parser_syntax_error(parser, "generic parameters are written @generic<...>");
        (void)r_parser_bump(parser);
    } else if (!r_parser_expect(parser, R_TOKEN_LESS, "expected '<' after @generic")) {
        return r_parser_close(parser, node);
    }
    parser->generic_header_start = r_skip_trivia(parser->source, parser->cursor);
    do {
        size_t parameter = r_parser_open(parser, R_SYNTAX_GENERIC_PARAMETER);
        if (r_parser_at(parser, R_TOKEN_IDENTIFIER) &&
            (r_parser_peek_n_kind(parser, 1U) == R_TOKEN_COLON_COLON)) {
            /* R-TYPE-0043 (M19): `P::Name: constraints` constrains an associated type of a
               parameter of the same header. */
            const size_t associated = r_parser_open(parser, R_SYNTAX_ASSOCIATED_NAME);
            (void)r_parser_bump(parser);
            (void)r_parser_bump(parser);
            (void)r_parser_expect(
                parser, R_TOKEN_IDENTIFIER, "expected associated type name after '::'");
            (void)r_parser_close(parser, associated);
            (void)r_parser_expect(
                parser, R_TOKEN_COLON, "expected ':' before the constraints of an associated type");
            (void)r_parse_generic_constraint_list(parser);
            (void)r_parser_close(parser, parameter);
            continue;
        }
        const bool constant = r_parser_eat(parser, R_TOKEN_KW_CONST);
        if (constant) {
            /* R-TYPE-0047: an R integer type or bool. */
            const RTokenKind value_type = r_parser_peek_kind(parser);
            if (value_type == R_TOKEN_KW_I8 || value_type == R_TOKEN_KW_I16 ||
                value_type == R_TOKEN_KW_I32 || value_type == R_TOKEN_KW_I64 ||
                value_type == R_TOKEN_KW_ISIZE || value_type == R_TOKEN_KW_U8 ||
                value_type == R_TOKEN_KW_U16 || value_type == R_TOKEN_KW_U32 ||
                value_type == R_TOKEN_KW_U64 || value_type == R_TOKEN_KW_USIZE ||
                value_type == R_TOKEN_KW_BOOL) {
                (void)r_parser_bump(parser);
            } else {
                (void)r_parser_syntax_error(
                    parser, "constant generic parameters require an integer type or bool");
            }
        }
        (void)r_parser_expect(parser, R_TOKEN_IDENTIFIER, "expected generic parameter name");
        /* R-TYPE-0053 (L18.2): `T...` declares a pack. */
        if (!constant) {
            (void)r_parser_eat(parser, R_TOKEN_ELLIPSIS);
        }
        if (!constant && r_parser_eat(parser, R_TOKEN_COLON)) {
            (void)r_parse_generic_constraint_list(parser);
        }
        (void)r_parser_close(parser, parameter);
        if (!r_parser_at(parser, R_TOKEN_COMMA) &&
            !(parenthesized ? r_parser_at(parser, R_TOKEN_RPAREN)
                            : r_parser_at_type_close(parser))) {
            static const RTokenKind synchronizers[] = {R_TOKEN_COMMA,
                                                       R_TOKEN_RPAREN,
                                                       R_TOKEN_GREATER,
                                                       R_TOKEN_AT,
                                                       R_TOKEN_KW_STRUCT,
                                                       R_TOKEN_KW_ENUM,
                                                       R_TOKEN_KW_ASYNC,
                                                       R_TOKEN_KW_DROP,
                                                       R_TOKEN_KW_PROTECTED,
                                                       R_TOKEN_SEMICOLON,
                                                       R_TOKEN_LBRACE,
                                                       R_TOKEN_RBRACE};
            (void)r_parser_syntax_error(parser, "expected ',' or '>' after generic parameter");
            (void)r_parser_recover(
                parser, synchronizers, sizeof(synchronizers) / sizeof(synchronizers[0]));
        }
    } while (r_parser_eat(parser, R_TOKEN_COMMA));
    parser->generic_header_end = r_skip_trivia(parser->source, parser->cursor);
    if (parenthesized) {
        (void)r_parser_expect(parser, R_TOKEN_RPAREN, "expected ')' after generic parameters");
    } else {
        (void)r_parser_expect_type_close(parser, "expected '>' after generic parameters");
    }
    return r_parser_close(parser, node);
}

/* Recognize legacy headers only for diagnostics and recovery.
 * After generic(...), a function name followed by its parameter list belongs
 * to that applied return type; a header still needs a return type or decl kind. */
static bool r_parser_legacy_generic_header_ahead(const RParser *parser) {
    size_t index = r_skip_trivia(parser->source, parser->cursor);
    uint32_t depth = 0U;
    if (!r_parser_token_text_is(parser, r_scan_token(parser->source, index), "generic")) {
        return false;
    }
    index = r_skip_trivia(parser->source, index + 1U);
    if (r_scan_kind(parser->source, index) == R_TOKEN_LESS) {
        /* `generic<...>` without `@`, unless it applies a type named `generic`. */
        index = r_skip_balanced_angles(parser, index);
        if (index == 0U) {
            return true;
        }
    } else if (r_scan_kind(parser->source, index) != R_TOKEN_LPAREN) {
        return false;
    } else {
        do {
            RTokenKind kind = r_scan_kind(parser->source, index);
            if (kind == R_TOKEN_EOF) {
                return true;
            }
            if (kind == R_TOKEN_LPAREN) {
                ++depth;
            }
            if (kind == R_TOKEN_RPAREN) {
                --depth;
            }
            index = r_skip_trivia(parser->source, index + 1U);
        } while (depth != 0U);
    }
    if (r_scan_kind(parser->source, index) == R_TOKEN_STAR ||
        r_scan_kind(parser->source, index) == R_TOKEN_LBRACKET ||
        r_scan_kind(parser->source, index) == R_TOKEN_COLON_COLON ||
        (r_scan_kind(parser->source, index) == R_TOKEN_IDENTIFIER &&
         r_scan_kind(parser->source, index + 1U) == R_TOKEN_EQUAL)) {
        return false;
    }
    if (r_scan_kind(parser->source, index) != R_TOKEN_IDENTIFIER ||
        r_scan_kind(parser->source, index + 1U) != R_TOKEN_LPAREN) {
        return true;
    }
    index = r_skip_trivia(parser->source, index + 1U);
    do {
        RTokenKind kind = r_scan_kind(parser->source, index);
        if (kind == R_TOKEN_EOF) {
            return false;
        }
        if (kind == R_TOKEN_LPAREN) {
            ++depth;
        }
        if (kind == R_TOKEN_RPAREN) {
            --depth;
        }
        index = r_skip_trivia(parser->source, index + 1U);
    } while (depth != 0U);
    return r_scan_kind(parser->source, index) != R_TOKEN_LBRACE &&
           r_scan_kind(parser->source, index) != R_TOKEN_SEMICOLON &&
           r_scan_kind(parser->source, index) != R_TOKEN_KW_THROWS;
}

static bool r_parse_external_declaration(RParser *parser);

#include "static_conditions.inc"

static bool r_parse_external_declaration(RParser *parser) {
    if (r_parser_at(parser, R_TOKEN_AT) && r_parser_peek_n_kind(parser, 1U) == R_TOKEN_KW_IF) {
        return r_parse_static_if(parser, true);
    }
    size_t node = r_parser_open(parser, R_SYNTAX_EXTERNAL_DECLARATION);
    bool is_protected;
    bool has_generic_header = false;

    parser->generic_header_start = 0U;
    parser->generic_header_end = 0U;
    while (r_parser_at(parser, R_TOKEN_AT) || r_parser_legacy_generic_header_ahead(parser)) {
        if (r_parser_generic_attribute_ahead(parser) || !r_parser_at(parser, R_TOKEN_AT)) {
            size_t start = parser->generic_header_start;
            size_t end = parser->generic_header_end;
            if (!r_parser_at(parser, R_TOKEN_AT)) {
                (void)r_parser_syntax_error(parser,
                                            "use @generic<...> to declare generic parameters");
            }
            if (has_generic_header) {
                (void)r_parser_syntax_error(parser, "duplicate @generic header");
            }
            (void)r_parse_generic_header(parser);
            if (has_generic_header) {
                parser->generic_header_start = start;
                parser->generic_header_end = end;
            }
            has_generic_header = true;
        } else {
            (void)r_parse_attribute(parser);
        }
    }
    is_protected = r_parser_eat(parser, R_TOKEN_KW_PROTECTED);
    if (r_parser_at(parser, R_TOKEN_KW_DROP)) {
        if (is_protected) {
            (void)r_parser_syntax_error(parser, "drop definition does not accept protected");
        }
        (void)r_parse_drop_definition(parser);
    } else if (r_parser_at(parser, R_TOKEN_KW_TRAIT)) {
        (void)r_parse_trait_declaration(parser);
    } else if (r_parser_at(parser, R_TOKEN_KW_IMPL)) {
        if (is_protected) {
            (void)r_parser_syntax_error(parser, "impl declaration does not accept protected");
        }
        (void)r_parse_impl_declaration(parser);
    } else if (r_scan_error_declaration(parser->source, parser->cursor)) {
        (void)r_parse_error_declaration(parser);
    } else if (r_parser_at(parser, R_TOKEN_KW_STRUCT)) {
        (void)r_parse_struct_declaration(parser, false);
    } else if (r_parser_at(parser, R_TOKEN_KW_ENUM)) {
        (void)r_parse_enum_declaration(parser, false);
    } else if (r_extern_block_ahead(parser)) {
        if (has_generic_header) {
            (void)r_parser_syntax_error(parser, "@generic is not allowed on extern blocks");
        }
        (void)r_parse_extern_block(parser);
    } else {
        size_t declaration;
        bool async_function;
        (void)r_parser_eat(parser, R_TOKEN_KW_UNSAFE);
        async_function = r_parser_eat(parser, R_TOKEN_KW_ASYNC);
        if (r_parser_at(parser, R_TOKEN_KW_EXTERN) &&
            (r_parser_peek_n_kind(parser, 1U) == R_TOKEN_STRING_LITERAL)) {
            const RToken *abi_literal;
            (void)r_parser_bump(parser);
            abi_literal = r_parser_peek_token(parser);
            if ((abi_literal != NULL) && !r_abi_literal_is_c(parser, abi_literal)) {
                (void)r_add_diagnostic(parser->context,
                                       "R-DIAG-SYN-001",
                                       "R-FFI-0013",
                                       "extern ABI string shall contain exactly C",
                                       R_DIAGNOSTIC_ERROR,
                                       abi_literal->span);
            }
            (void)r_parser_bump(parser);
        }
        const RToken *storage = r_parser_peek_token(parser);
        const bool thread_local_storage = r_parser_eat(parser, R_TOKEN_KW_THREAD_LOCAL);
        declaration = r_parser_open(parser, R_SYNTAX_FUNCTION_DECLARATION);
        if (!r_parse_type(parser, true)) {
            (void)r_parser_syntax_error(parser, "expected external declaration type");
        }
        (void)r_parse_function_name(parser);
        if (r_parser_at(parser, R_TOKEN_LPAREN)) {
            /* Annex A: a storage specifier belongs to module objects, never to functions. */
            if (thread_local_storage && storage != NULL)
                (void)r_add_diagnostic(parser->context,
                                       "R-DIAG-SYN-001",
                                       "R-GRAM-0005",
                                       "thread_local applies only to module objects",
                                       R_DIAGNOSTIC_ERROR,
                                       storage->span);
            (void)r_parse_parameter_list(parser, false);
            if (r_parser_at(parser, R_TOKEN_KW_THROWS)) {
                (void)r_parse_throws_clause(parser);
            }
            if (!r_parser_eat(parser, R_TOKEN_SEMICOLON)) {
                bool previous_async_context = parser->in_async_function;
                parser->in_async_function = async_function;
                (void)r_parse_block(parser);
                parser->in_async_function = previous_async_context;
            }
        } else {
            if (has_generic_header) {
                (void)r_parser_syntax_error(parser, "@generic is not allowed on module objects");
            }
            parser->source->cst_events[declaration].syntax_kind = R_SYNTAX_OBJECT_DECLARATION;
            (void)r_parser_expect(
                parser, R_TOKEN_EQUAL, "module object declaration requires initializer");
            if (!r_parse_initializer(parser, R_EXPRESSION_CONSTANT)) {
                (void)r_parser_syntax_error(parser, "expected module object initializer");
            }
            (void)r_parser_expect(
                parser, R_TOKEN_SEMICOLON, "expected ';' after module object declaration");
        }
        (void)r_parser_close(parser, declaration);
    }
    return r_parser_close(parser, node);
}

static bool r_parse_return_operand(RParser *parser) {
    if (r_parser_at(parser, R_TOKEN_LBRACE)) {
        return r_parse_aggregate_initializer(parser, R_EXPRESSION_NORMAL);
    }
    return r_parse_expression_mode(parser, R_EXPRESSION_NORMAL).parsed;
}

static bool r_conditional_throw_ahead(const RParser *parser) {
    size_t index = r_skip_trivia(parser->source, parser->cursor);
    uint32_t depth = UINT32_C(0);
    bool has_condition_leaf = false;

    if (r_scan_kind(parser->source, index) != R_TOKEN_KW_THROW) {
        return false;
    }
    index = r_skip_trivia(parser->source, index + 1U);
    if (r_scan_kind(parser->source, index) != R_TOKEN_LPAREN) {
        return false;
    }
    while (index < parser->source->token_count) {
        const RTokenKind kind = r_scan_kind(parser->source, index);

        if (kind == R_TOKEN_LPAREN) {
            depth += UINT32_C(1);
        } else if (kind == R_TOKEN_RPAREN) {
            depth -= UINT32_C(1);
            if (depth == UINT32_C(0)) {
                const RTokenKind next = r_scan_kind(parser->source, index + 1U);

                /* Operators shared by prefix and infix/postfix syntax continue an ordinary
                 * operand unless the first group contains an explicit condition leaf. */
                if (!has_condition_leaf &&
                    ((next == R_TOKEN_PLUS) || (next == R_TOKEN_MINUS) || (next == R_TOKEN_STAR) ||
                     (next == R_TOKEN_AMP) || (next == R_TOKEN_PLUS_PLUS) ||
                     (next == R_TOKEN_MINUS_MINUS))) {
                    return false;
                }
                return (next == R_TOKEN_LBRACE) || (next == R_TOKEN_LPAREN) ||
                       (next == R_TOKEN_IDENTIFIER) || (next == R_TOKEN_KW_O) ||
                       r_is_prefix_operator(next) || r_is_literal_token(next) ||
                       (next == R_TOKEN_KW_TRUE) || (next == R_TOKEN_KW_FALSE) ||
                       (next == R_TOKEN_KW_NULL) || (next == R_TOKEN_KW_NEW) ||
                       (next == R_TOKEN_KW_SIZEOF) || (next == R_TOKEN_KW_ALIGNOF) ||
                       (next == R_TOKEN_KW_PANIC);
            }
        } else if (kind == R_TOKEN_EOF) {
            return false;
        } else if ((kind == R_TOKEN_KW_TRUE) || (kind == R_TOKEN_KW_FALSE) ||
                   r_is_comparison_operator(kind)) {
            has_condition_leaf = true;
        }
        index = r_skip_trivia(parser->source, index + 1U);
    }
    return false;
}

static bool r_conditional_throw_else_ahead(const RParser *parser) {
    size_t index = r_skip_trivia(parser->source, parser->cursor);
    uint32_t depth = UINT32_C(0);

    if (!r_conditional_throw_ahead(parser)) {
        return false;
    }
    while (index < parser->source->token_count) {
        const RTokenKind kind = r_scan_kind(parser->source, index);

        if ((kind == R_TOKEN_EOF) || ((depth == UINT32_C(0)) && (kind == R_TOKEN_SEMICOLON))) {
            return false;
        }
        if ((depth == UINT32_C(0)) && (kind == R_TOKEN_KW_ELSE)) {
            return true;
        }
        if ((kind == R_TOKEN_LPAREN) || (kind == R_TOKEN_LBRACKET) || (kind == R_TOKEN_LBRACE)) {
            depth += UINT32_C(1);
        } else if ((kind == R_TOKEN_RPAREN) || (kind == R_TOKEN_RBRACKET) ||
                   (kind == R_TOKEN_RBRACE)) {
            if (depth == UINT32_C(0)) {
                return false;
            }
            depth -= UINT32_C(1);
        }
        index = r_skip_trivia(parser->source, index + 1U);
    }
    return false;
}

static bool r_parse_throw_operand(RParser *parser) {
    const bool parsed = r_parser_at(parser, R_TOKEN_LBRACE)
                            ? r_parse_aggregate_initializer(parser, R_EXPRESSION_NORMAL)
                            : r_parse_expression_mode(parser, R_EXPRESSION_NORMAL).parsed;

    if (!parsed) {
        (void)r_parser_syntax_error(parser, "expected throw operand");
    }
    return parsed;
}

static bool r_parse_throw_statement(RParser *parser) {
    const bool conditional = r_conditional_throw_ahead(parser);
    size_t node = r_parser_open(parser, R_SYNTAX_THROW_STATEMENT);

    (void)r_parser_expect(parser, R_TOKEN_KW_THROW, "expected throw");
    if (conditional) {
        size_t condition_node = r_parser_open(parser, R_SYNTAX_THROW_CONDITION);
        RExpressionInfo condition;

        (void)r_parser_expect(parser, R_TOKEN_LPAREN, "expected '(' after conditional throw");
        condition = r_parse_condition_expression(parser);
        if (!condition.parsed || !condition.is_condition) {
            (void)r_parser_syntax_error(parser, "invalid explicit throw condition");
        }
        (void)r_parser_expect(parser, R_TOKEN_RPAREN, "expected ')' after throw condition");
        (void)r_parser_close(parser, condition_node);
    }
    if (conditional || !r_parser_at(parser, R_TOKEN_SEMICOLON)) {
        (void)r_parse_throw_operand(parser);
    }
    if (conditional && r_parser_at(parser, R_TOKEN_KW_ELSE)) {
        size_t else_node = r_parser_open(parser, R_SYNTAX_THROW_ELSE);

        (void)r_parser_bump(parser);
        (void)r_parse_throw_operand(parser);
        (void)r_parser_close(parser, else_node);
    }
    (void)r_parser_expect(parser, R_TOKEN_SEMICOLON, "expected ';' after throw");
    return r_parser_close(parser, node);
}

static bool r_parse_catch_clause(RParser *parser) {
    size_t node = r_parser_open(parser, R_SYNTAX_CATCH_CLAUSE);

    (void)r_parser_expect(parser, R_TOKEN_KW_CATCH, "expected catch");
    (void)r_parser_expect(parser, R_TOKEN_LPAREN, "expected '(' after catch");
    if (!r_parse_type(parser, true)) {
        (void)r_parser_syntax_error(parser, "expected caught error type");
    }
    (void)r_parser_expect(parser, R_TOKEN_IDENTIFIER, "expected catch binding name");
    (void)r_parser_expect(parser, R_TOKEN_RPAREN, "expected ')' after catch binding");
    (void)r_parse_block(parser);
    return r_parser_close(parser, node);
}

static bool r_parse_finally_clause(RParser *parser) {
    size_t node = r_parser_open(parser, R_SYNTAX_FINALLY_CLAUSE);

    (void)r_parser_expect(parser, R_TOKEN_KW_FINALLY, "expected finally");
    (void)r_parse_block(parser);
    return r_parser_close(parser, node);
}

static bool r_parse_try_statement(RParser *parser) {
    size_t node = r_parser_open(parser, R_SYNTAX_TRY_STATEMENT);
    bool has_handler = false;
    bool saw_finally = false;

    (void)r_parser_expect(parser, R_TOKEN_KW_TRY, "expected try");
    (void)r_parse_block(parser);
    while (r_parser_at(parser, R_TOKEN_KW_CATCH) || r_parser_at(parser, R_TOKEN_KW_FINALLY)) {
        has_handler = true;
        if (r_parser_at(parser, R_TOKEN_KW_CATCH)) {
            if (saw_finally) {
                (void)r_parser_syntax_error(parser, "catch clause cannot follow finally clause");
            }
            (void)r_parse_catch_clause(parser);
        } else {
            if (saw_finally) {
                (void)r_parser_syntax_error(parser,
                                            "try statement accepts only one finally clause");
            }
            saw_finally = true;
            (void)r_parse_finally_clause(parser);
        }
    }
    if (!has_handler) {
        (void)r_parser_missing(
            parser, R_TOKEN_KW_CATCH, "try requires at least one catch or finally clause");
    }
    return r_parser_close(parser, node);
}

static bool r_parse_jump_statement(RParser *parser) {
    size_t node = r_parser_open(parser, R_SYNTAX_JUMP_STATEMENT);
    RTokenKind kind = r_parser_peek_kind(parser);

    if ((kind == R_TOKEN_KW_BREAK) || (kind == R_TOKEN_KW_CONTINUE)) {
        (void)r_parser_bump(parser);
        /* R-STMT-0004 (L37.2): `break name;` and `continue name;` name an enclosing loop. */
        (void)r_parser_eat(parser, R_TOKEN_IDENTIFIER);
    } else if (kind == R_TOKEN_KW_RETURN) {
        (void)r_parser_bump(parser);
        if (!r_parser_at(parser, R_TOKEN_SEMICOLON) && !r_parse_return_operand(parser)) {
            (void)r_parser_syntax_error(parser, "expected return operand");
        }
    } else {
        (void)r_parser_syntax_error(parser, "expected jump statement");
    }
    (void)r_parser_expect(parser, R_TOKEN_SEMICOLON, "expected ';' after jump statement");
    return r_parser_close(parser, node);
}

static bool r_parse_clause_terminator(RParser *parser) {
    if (r_parser_at(parser, R_TOKEN_KW_THROW)) {
        return r_parse_throw_statement(parser);
    }

    size_t node = r_parser_open(parser, R_SYNTAX_JUMP_STATEMENT);
    RTokenKind kind = r_parser_peek_kind(parser);

    if ((kind == R_TOKEN_KW_BREAK) || (kind == R_TOKEN_KW_CONTINUE)) {
        (void)r_parser_bump(parser);
        /* R-STMT-0004 (L37.2): a clause may end by leaving a named enclosing loop. */
        (void)r_parser_eat(parser, R_TOKEN_IDENTIFIER);
    } else if (kind == R_TOKEN_KW_FALLTHROUGH) {
        (void)r_parser_bump(parser);
    } else if (kind == R_TOKEN_KW_RETURN) {
        (void)r_parser_bump(parser);
        if (!r_parser_at(parser, R_TOKEN_SEMICOLON) && !r_parse_return_operand(parser)) {
            (void)r_parser_syntax_error(parser, "expected return operand");
        }
    } else {
        (void)r_parser_missing(parser,
                               R_TOKEN_KW_BREAK,
                               "switch clause requires break, continue, return, throw, or "
                               "fallthrough");
    }
    (void)r_parser_expect(parser, R_TOKEN_SEMICOLON, "expected ';' after clause terminator");
    return r_parser_close(parser, node);
}

static bool r_parse_case_pattern(RParser *parser) {
    size_t node = r_parser_open(parser, R_SYNTAX_CASE_PATTERN);
    bool parsed = true;

    if (r_parser_eat(parser, R_TOKEN_KW_VARIANT)) {
        if (r_qualified_name_ahead(parser) || r_applied_type_tail(parser) == R_TOKEN_COLON_COLON) {
            parsed = r_parse_qualified_name(parser);
        } else {
            parsed = r_parser_bump(parser);
        }
        if (r_parser_eat(parser, R_TOKEN_LPAREN)) {
            if (!r_parser_at(parser, R_TOKEN_RPAREN)) {
                (void)r_parser_eat(parser, R_TOKEN_KW_MOVE);
                (void)r_parser_expect(parser, R_TOKEN_IDENTIFIER, "expected variant binding name");
            }
            (void)r_parser_expect(parser, R_TOKEN_RPAREN, "expected ')' after variant binding");
        }
    } else {
        parsed = r_parse_expression_mode(parser, R_EXPRESSION_CONSTANT).parsed;
        if (!parsed) {
            (void)r_parser_syntax_error(parser, "expected constant or variant case pattern");
        }
    }
    (void)r_parser_close(parser, node);
    return parsed;
}

static bool r_is_clause_terminator(const RParser *parser) {
    const RTokenKind kind = r_parser_peek_kind(parser);

    return (kind == R_TOKEN_KW_BREAK) || (kind == R_TOKEN_KW_CONTINUE) ||
           (kind == R_TOKEN_KW_RETURN) ||
           ((kind == R_TOKEN_KW_THROW) &&
            (!r_conditional_throw_ahead(parser) || r_conditional_throw_else_ahead(parser))) ||
           (kind == R_TOKEN_KW_FALLTHROUGH);
}

static bool r_parse_statement(RParser *parser);
static bool r_parse_unresolved_declaration(RParser *parser, bool consume_semicolon);

static bool r_parse_switch_clause(RParser *parser, bool is_default) {
    size_t node =
        r_parser_open(parser, is_default ? R_SYNTAX_DEFAULT_CLAUSE : R_SYNTAX_CASE_CLAUSE);

    if (is_default) {
        (void)r_parser_expect(parser, R_TOKEN_KW_DEFAULT, "expected default");
    } else {
        (void)r_parser_expect(parser, R_TOKEN_KW_CASE, "expected case");
        (void)r_parse_case_pattern(parser);
    }
    (void)r_parser_expect(parser, R_TOKEN_COLON, "expected ':' after switch label");
    while (!r_is_clause_terminator(parser) && !r_parser_at(parser, R_TOKEN_KW_CASE) &&
           !r_parser_at(parser, R_TOKEN_KW_DEFAULT) && !r_parser_at(parser, R_TOKEN_RBRACE) &&
           !r_parser_at(parser, R_TOKEN_EOF)) {
        size_t before = parser->cursor;
        (void)r_parse_statement(parser);
        if (parser->cursor == before) {
            static const RTokenKind synchronizers[] = {
                R_TOKEN_SEMICOLON,
                R_TOKEN_KW_CASE,
                R_TOKEN_KW_DEFAULT,
                R_TOKEN_RBRACE,
            };
            (void)r_parser_recover(
                parser, synchronizers, sizeof(synchronizers) / sizeof(synchronizers[0]));
            (void)r_parser_eat(parser, R_TOKEN_SEMICOLON);
        }
    }
    /* R-STMT-0007 (L20.1): a clause without a terminator ends at the next label or the closing
       brace and leaves the switch like break. */
    if (r_is_clause_terminator(parser)) {
        (void)r_parse_clause_terminator(parser);
    }
    return r_parser_close(parser, node);
}

static bool r_parse_switch_statement(RParser *parser) {
    size_t node;
    bool closed;
    if (!r_parser_enter_nesting(parser)) {
        return false;
    }
    node = r_parser_open(parser, R_SYNTAX_SWITCH_STATEMENT);
    (void)r_parser_bump(parser);
    (void)r_parser_expect(parser, R_TOKEN_LPAREN, "expected '(' after switch");
    if (!r_parse_expression_mode(parser, R_EXPRESSION_NORMAL).parsed) {
        (void)r_parser_syntax_error(parser, "expected switch expression");
    }
    (void)r_parser_expect(parser, R_TOKEN_RPAREN, "expected ')' after switch expression");
    (void)r_parser_expect(parser, R_TOKEN_LBRACE, "expected '{' after switch");
    while (!r_parser_at(parser, R_TOKEN_RBRACE) && !r_parser_at(parser, R_TOKEN_EOF)) {
        if (r_parser_at(parser, R_TOKEN_KW_CASE)) {
            (void)r_parse_switch_clause(parser, false);
        } else if (r_parser_at(parser, R_TOKEN_KW_DEFAULT)) {
            (void)r_parse_switch_clause(parser, true);
        } else {
            static const RTokenKind synchronizers[] = {
                R_TOKEN_KW_CASE,
                R_TOKEN_KW_DEFAULT,
                R_TOKEN_RBRACE,
            };
            (void)r_parser_syntax_error(parser, "expected case or default in switch");
            (void)r_parser_recover(
                parser, synchronizers, sizeof(synchronizers) / sizeof(synchronizers[0]));
        }
    }
    (void)r_parser_expect(parser, R_TOKEN_RBRACE, "expected '}' after switch");
    closed = r_parser_close(parser, node);
    r_parser_leave_nesting(parser);
    return closed;
}

static bool r_parse_if_statement(RParser *parser) {
    size_t node = r_parser_open(parser, R_SYNTAX_IF_STATEMENT);
    RExpressionInfo condition;
    (void)r_parser_bump(parser);
    (void)r_parser_expect(parser, R_TOKEN_LPAREN, "expected '(' after if");
    condition = r_parse_condition_expression(parser);
    if (!condition.parsed || !condition.is_condition) {
        (void)r_parser_syntax_error(parser, "invalid explicit if condition");
    }
    (void)r_parser_expect(parser, R_TOKEN_RPAREN, "expected ')' after if condition");
    (void)r_parse_block(parser);
    if (r_parser_eat(parser, R_TOKEN_KW_ELSE)) {
        (void)r_parse_block(parser);
    }
    return r_parser_close(parser, node);
}

static bool r_parse_while_statement(RParser *parser) {
    size_t node = r_parser_open(parser, R_SYNTAX_WHILE_STATEMENT);
    RExpressionInfo condition;
    (void)r_parser_bump(parser);
    (void)r_parser_expect(parser, R_TOKEN_LPAREN, "expected '(' after while");
    condition = r_parse_condition_expression(parser);
    if (!condition.parsed || !condition.is_condition) {
        (void)r_parser_syntax_error(parser, "invalid explicit while condition");
    }
    (void)r_parser_expect(parser, R_TOKEN_RPAREN, "expected ')' after while condition");
    (void)r_parse_block(parser);
    return r_parser_close(parser, node);
}

/* for-in-statement = "for", "(", ( type | "auto" ), identifier, "in", iterable, ")", block ;
   The header is recognized by a trial parse of its declaration prefix (R-STMT-0014). */
static bool r_for_in_header_ahead(RParser *parser) {
    RParser trial = *parser;
    const size_t event_count = parser->source->cst_event_count;
    const bool had_syntax_error = parser->source->has_syntax_error;
    bool result;

    trial.suppress_diagnostics = true;
    if (r_parser_at(&trial, R_TOKEN_KW_AUTO)) {
        result = r_parser_bump(&trial);
    } else {
        result = r_parse_type(&trial, true);
    }
    result =
        result && r_parser_eat(&trial, R_TOKEN_IDENTIFIER) && r_parser_at(&trial, R_TOKEN_KW_IN);
    parser->source->cst_event_count = event_count;
    parser->source->has_syntax_error = had_syntax_error;
    return result;
}

static bool r_parse_for_in_statement(RParser *parser, size_t node) {
    RExpressionInfo iterable;
    parser->source->cst_events[node].syntax_kind = R_SYNTAX_FOR_IN_STATEMENT;
    if (r_parser_at(parser, R_TOKEN_KW_AUTO)) {
        /* R-NAME-0011: the loop variable takes the item type. */
        size_t inferred = r_parser_open(parser, R_SYNTAX_TYPE);
        (void)r_parser_bump(parser);
        (void)r_parser_close(parser, inferred);
    } else if (!r_parse_type(parser, true)) {
        (void)r_parser_syntax_error(parser, "expected loop variable type");
    }
    (void)r_parser_expect(parser, R_TOKEN_IDENTIFIER, "expected loop variable name");
    (void)r_parser_expect(parser, R_TOKEN_KW_IN, "expected 'in' after the loop variable");
    iterable = r_parse_range_or_expression(parser, R_EXPRESSION_NORMAL, UINT32_C(1), false);
    if (!iterable.parsed) {
        (void)r_parser_syntax_error(parser, "expected an iterable expression or range");
    }
    (void)r_parser_expect(parser, R_TOKEN_RPAREN, "expected ')' after the iterable");
    (void)r_parse_block(parser);
    return r_parser_close(parser, node);
}

static bool r_parse_for_statement(RParser *parser) {
    size_t node = r_parser_open(parser, R_SYNTAX_FOR_STATEMENT);
    RExpressionInfo condition;
    (void)r_parser_bump(parser);
    (void)r_parser_expect(parser, R_TOKEN_LPAREN, "expected '(' after for");
    if (r_for_in_header_ahead(parser)) {
        return r_parse_for_in_statement(parser, node);
    }
    if (!r_parser_at(parser, R_TOKEN_SEMICOLON)) {
        if (r_known_statement_type_start(parser)) {
            (void)r_parse_object_declaration(parser, false);
        } else if (r_looks_like_unresolved_declaration(parser)) {
            (void)r_parse_unresolved_declaration(parser, false);
        } else {
            (void)r_parse_expression_mode(parser, R_EXPRESSION_NORMAL);
        }
    }
    (void)r_parser_expect(parser, R_TOKEN_SEMICOLON, "expected ';' after for initializer");
    condition = r_parse_condition_expression(parser);
    if (!condition.parsed || !condition.is_condition) {
        (void)r_parser_syntax_error(parser, "for loop requires an explicit condition");
    }
    (void)r_parser_expect(parser, R_TOKEN_SEMICOLON, "expected ';' after for condition");
    if (!r_parser_at(parser, R_TOKEN_RPAREN)) {
        (void)r_parse_expression_mode(parser, R_EXPRESSION_NORMAL);
    }
    (void)r_parser_expect(parser, R_TOKEN_RPAREN, "expected ')' after for clauses");
    (void)r_parse_block(parser);
    return r_parser_close(parser, node);
}

/* lambda-declaration = "fn", type, identifier, "(", parameter-list, ")",
                        [ "move", "(", identifier, { ",", identifier }, ")" ], block (R-FUNC-0015)
 */
static bool r_parse_lambda_declaration(RParser *parser) {
    size_t node = r_parser_open(parser, R_SYNTAX_LAMBDA_DECLARATION);
    bool previous_async_context = parser->in_async_function;
    const bool asynchronous = r_parser_eat(parser, R_TOKEN_KW_ASYNC);
    (void)r_parser_expect(parser, R_TOKEN_KW_FN, "expected fn");
    (void)r_parse_fn_resource_attributes(parser);
    (void)r_parse_callable_mode(parser, true);
    if (!r_parse_type(parser, true)) {
        (void)r_parser_syntax_error(parser, "expected lambda result type");
    }
    (void)r_parser_expect(parser, R_TOKEN_IDENTIFIER, "expected lambda name");
    (void)r_parse_parameter_list(parser, false);
    if (r_parser_at(parser, R_TOKEN_KW_MOVE)) {
        size_t captures = r_parser_open(parser, R_SYNTAX_MOVE_CAPTURE_LIST);
        (void)r_parser_bump(parser);
        (void)r_parser_expect(parser, R_TOKEN_LPAREN, "expected '(' after move");
        do {
            (void)r_parser_expect(parser, R_TOKEN_IDENTIFIER, "expected captured name");
        } while (r_parser_eat(parser, R_TOKEN_COMMA));
        (void)r_parser_expect(parser, R_TOKEN_RPAREN, "expected ')' after captured names");
        (void)r_parser_close(parser, captures);
    }
    if (r_parser_at(parser, R_TOKEN_KW_THROWS)) {
        (void)r_parse_throws_clause(parser);
    }
    parser->in_async_function = asynchronous;
    (void)r_parse_block(parser);
    parser->in_async_function = previous_async_context;
    return r_parser_close(parser, node);
}

static bool r_parse_unresolved_declaration(RParser *parser, bool consume_semicolon) {
    size_t node = r_parser_open(parser, R_SYNTAX_AMBIGUOUS_DECL_OR_EXPR);
    uint32_t brace_depth = UINT32_C(0);
    uint32_t paren_depth = UINT32_C(0);
    uint32_t bracket_depth = UINT32_C(0);

    parser->source->has_unresolved_ambiguity = true;
    while (!r_parser_at(parser, R_TOKEN_EOF)) {
        RTokenKind kind = r_parser_peek_kind(parser);
        if ((kind == R_TOKEN_SEMICOLON) && (brace_depth == UINT32_C(0)) &&
            (paren_depth == UINT32_C(0)) && (bracket_depth == UINT32_C(0))) {
            if (consume_semicolon) {
                (void)r_parser_bump(parser);
            }
            break;
        }
        if (kind == R_TOKEN_LBRACE) {
            brace_depth += 1U;
        } else if ((kind == R_TOKEN_RBRACE) && (brace_depth != UINT32_C(0))) {
            brace_depth -= 1U;
        } else if (kind == R_TOKEN_LPAREN) {
            paren_depth += 1U;
        } else if ((kind == R_TOKEN_RPAREN) && (paren_depth != UINT32_C(0))) {
            paren_depth -= 1U;
        } else if (kind == R_TOKEN_LBRACKET) {
            bracket_depth += 1U;
        } else if ((kind == R_TOKEN_RBRACKET) && (bracket_depth != UINT32_C(0))) {
            bracket_depth -= 1U;
        }
        (void)r_parser_bump(parser);
    }
    return r_parser_close(parser, node);
}

static bool r_parse_expression_statement(RParser *parser) {
    size_t node = r_parser_open(parser, R_SYNTAX_EXPRESSION_STATEMENT);
    if (!r_parser_at(parser, R_TOKEN_SEMICOLON) &&
        !r_parse_expression_mode(parser, R_EXPRESSION_NORMAL).parsed) {
        (void)r_parser_syntax_error(parser, "expected expression statement");
    }
    (void)r_parser_expect(parser, R_TOKEN_SEMICOLON, "expected ';' after expression");
    return r_parser_close(parser, node);
}

static bool r_task_scope_ahead(const RParser *parser) {
    size_t index = r_skip_trivia(parser->source, parser->cursor);
    if (!r_parser_token_index_text_is(parser, index, "task_scope"))
        return false;
    index = r_skip_balanced_parentheses(parser, r_skip_trivia(parser->source, index + 1U));
    if (index == 0U || r_scan_kind(parser->source, index) != R_TOKEN_IDENTIFIER)
        return false;
    index = r_skip_trivia(parser->source, index + 1U);
    return r_scan_kind(parser->source, index) == R_TOKEN_LBRACE;
}

/* R-STMT-0018: `select (group) {` is a statement only when the parenthesized group name is
 * followed by a clause block; otherwise `select` stays an ordinary identifier. */
static bool r_select_ahead(const RParser *parser) {
    size_t index = r_skip_trivia(parser->source, parser->cursor);
    if (!r_parser_token_index_text_is(parser, index, "select"))
        return false;
    index = r_skip_trivia(parser->source, index + 1U);
    if (r_scan_kind(parser->source, index) != R_TOKEN_LPAREN)
        return false;
    index = r_skip_balanced_parentheses(parser, index);
    return index != 0U && r_scan_kind(parser->source, index) == R_TOKEN_LBRACE;
}

/* R-STMT-0019: `deadline (instant) {` is a statement only when the parenthesized expression is
 * followed by a block; otherwise `deadline` stays an ordinary identifier. */
static bool r_deadline_ahead(const RParser *parser) {
    size_t index = r_skip_trivia(parser->source, parser->cursor);
    if (!r_parser_token_index_text_is(parser, index, "deadline"))
        return false;
    index = r_skip_trivia(parser->source, index + 1U);
    if (r_scan_kind(parser->source, index) != R_TOKEN_LPAREN)
        return false;
    index = r_skip_balanced_parentheses(parser, index);
    return index != 0U && r_scan_kind(parser->source, index) == R_TOKEN_LBRACE;
}

/* R-STMT-0020: `budget` begins a budget statement only before a parenthesized expression and a
   block, as `deadline` does. */
static bool r_budget_ahead(const RParser *parser) {
    size_t index = r_skip_trivia(parser->source, parser->cursor);
    if (!r_parser_token_index_text_is(parser, index, "budget"))
        return false;
    index = r_skip_trivia(parser->source, index + 1U);
    if (r_scan_kind(parser->source, index) != R_TOKEN_LPAREN)
        return false;
    index = r_skip_balanced_parentheses(parser, index);
    return index != 0U && r_scan_kind(parser->source, index) == R_TOKEN_LBRACE;
}

static bool r_parse_select_clause(RParser *parser) {
    const size_t node = r_parser_open(parser, R_SYNTAX_SELECT_CLAUSE);
    (void)r_parser_expect(parser, R_TOKEN_KW_CASE, "expected case");
    if (r_parser_at(parser, R_TOKEN_IDENTIFIER) &&
        r_parser_token_index_text_is(
            parser, r_skip_trivia(parser->source, parser->cursor), "until") &&
        r_parser_peek_n_kind(parser, 1U) == R_TOKEN_LPAREN) {
        const size_t deadline = r_parser_open(parser, R_SYNTAX_SELECT_DEADLINE);
        (void)r_parser_bump(parser);
        (void)r_parser_expect(parser, R_TOKEN_LPAREN, "expected '(' after until");
        if (!r_parse_expression_mode(parser, R_EXPRESSION_NORMAL).parsed)
            (void)r_parser_syntax_error(parser, "expected select deadline");
        (void)r_parser_expect(parser, R_TOKEN_RPAREN, "expected ')' after select deadline");
        (void)r_parser_close(parser, deadline);
    } else if (r_parser_at(parser, R_TOKEN_KW_AWAIT)) {
        const size_t arm = r_parser_open(parser, R_SYNTAX_AWAIT_STATEMENT);
        (void)r_parse_await_operation(parser);
        (void)r_parser_close(parser, arm);
    } else {
        (void)r_parse_object_declaration(parser, false);
    }
    (void)r_parser_expect(parser, R_TOKEN_COLON, "expected ':' after select branch");
    while (!r_is_clause_terminator(parser) && !r_parser_at(parser, R_TOKEN_KW_CASE) &&
           !r_parser_at(parser, R_TOKEN_KW_DEFAULT) && !r_parser_at(parser, R_TOKEN_RBRACE) &&
           !r_parser_at(parser, R_TOKEN_EOF)) {
        const size_t before = parser->cursor;
        (void)r_parse_statement(parser);
        if (parser->cursor == before) {
            static const RTokenKind synchronizers[] = {
                R_TOKEN_SEMICOLON,
                R_TOKEN_KW_CASE,
                R_TOKEN_RBRACE,
            };
            (void)r_parser_recover(
                parser, synchronizers, sizeof(synchronizers) / sizeof(synchronizers[0]));
            (void)r_parser_eat(parser, R_TOKEN_SEMICOLON);
        }
    }
    /* R-STMT-0018: a select clause ends like a switch clause (R-STMT-0007). */
    if (r_is_clause_terminator(parser)) {
        (void)r_parse_clause_terminator(parser);
    }
    return r_parser_close(parser, node);
}

static bool r_parse_select_statement(RParser *parser) {
    size_t node;
    bool closed;
    if (!r_parser_enter_nesting(parser))
        return false;
    node = r_parser_open(parser, R_SYNTAX_SELECT_STATEMENT);
    (void)r_parser_bump(parser);
    (void)r_parser_expect(parser, R_TOKEN_LPAREN, "expected '(' after select");
    (void)r_parser_expect(parser, R_TOKEN_IDENTIFIER, "expected task group name");
    (void)r_parser_expect(parser, R_TOKEN_RPAREN, "expected ')' after task group name");
    (void)r_parser_expect(parser, R_TOKEN_LBRACE, "expected '{' after select");
    while (!r_parser_at(parser, R_TOKEN_RBRACE) && !r_parser_at(parser, R_TOKEN_EOF)) {
        if (r_parser_at(parser, R_TOKEN_KW_CASE)) {
            (void)r_parse_select_clause(parser);
        } else {
            static const RTokenKind synchronizers[] = {
                R_TOKEN_KW_CASE,
                R_TOKEN_RBRACE,
            };
            (void)r_parser_syntax_error(parser, "expected case in select");
            (void)r_parser_recover(
                parser, synchronizers, sizeof(synchronizers) / sizeof(synchronizers[0]));
        }
    }
    (void)r_parser_expect(parser, R_TOKEN_RBRACE, "expected '}' after select");
    closed = r_parser_close(parser, node);
    r_parser_leave_nesting(parser);
    return closed;
}

static bool r_parse_statement(RParser *parser) {
    if (r_parser_at(parser, R_TOKEN_AT) && r_parser_peek_n_kind(parser, 1U) == R_TOKEN_KW_IF) {
        return r_parse_static_if(parser, false);
    }
    while (r_parser_generic_attribute_ahead(parser)) {
        r_parse_misplaced_generic_header(parser);
    }
    RTokenKind kind = r_parser_peek_kind(parser);

    if ((kind == R_TOKEN_IDENTIFIER) && (r_parser_peek_n_kind(parser, 1U) == R_TOKEN_COLON) &&
        ((r_parser_peek_n_kind(parser, 2U) == R_TOKEN_KW_WHILE) ||
         (r_parser_peek_n_kind(parser, 2U) == R_TOKEN_KW_FOR))) {
        /* R-STMT-0004 (L37.2): `name: while (...)` and `name: for (...)` name a loop. */
        const size_t labeled = r_parser_open(parser, R_SYNTAX_LABELED_STATEMENT);
        (void)r_parser_bump(parser);
        (void)r_parser_bump(parser);
        (void)r_parse_statement(parser);
        return r_parser_close(parser, labeled);
    }
    if (kind == R_TOKEN_LBRACE) {
        return r_parse_block(parser);
    }
    if (kind == R_TOKEN_KW_IF) {
        return r_parse_if_statement(parser);
    }
    if (kind == R_TOKEN_KW_WHILE) {
        return r_parse_while_statement(parser);
    }
    if (kind == R_TOKEN_KW_FOR) {
        return r_parse_for_statement(parser);
    }
    if (kind == R_TOKEN_KW_SWITCH) {
        return r_parse_switch_statement(parser);
    }
    if (kind == R_TOKEN_KW_TRY) {
        return r_parse_try_statement(parser);
    }
    if (kind == R_TOKEN_KW_THROW) {
        return r_parse_throw_statement(parser);
    }
    if (kind == R_TOKEN_KW_THREAD_SCOPE) {
        size_t node = r_parser_open(parser, R_SYNTAX_THREAD_SCOPE_STATEMENT);
        (void)r_parser_bump(parser);
        (void)r_parse_block(parser);
        return r_parser_close(parser, node);
    }
    if (kind == R_TOKEN_IDENTIFIER && r_select_ahead(parser))
        return r_parse_select_statement(parser);
    if (kind == R_TOKEN_IDENTIFIER && r_deadline_ahead(parser)) {
        size_t node = r_parser_open(parser, R_SYNTAX_DEADLINE_STATEMENT);
        (void)r_parser_bump(parser);
        (void)r_parser_expect(parser, R_TOKEN_LPAREN, "expected '(' after deadline");
        if (!r_parse_expression_mode(parser, R_EXPRESSION_NORMAL).parsed)
            (void)r_parser_syntax_error(parser, "expected deadline instant");
        (void)r_parser_expect(parser, R_TOKEN_RPAREN, "expected ')' after deadline instant");
        (void)r_parse_block(parser);
        return r_parser_close(parser, node);
    }
    if (kind == R_TOKEN_IDENTIFIER && r_budget_ahead(parser)) {
        size_t node = r_parser_open(parser, R_SYNTAX_BUDGET_STATEMENT);
        (void)r_parser_bump(parser);
        (void)r_parser_expect(parser, R_TOKEN_LPAREN, "expected '(' after budget");
        if (!r_parse_expression_mode(parser, R_EXPRESSION_NORMAL).parsed)
            (void)r_parser_syntax_error(parser, "expected budget limits");
        (void)r_parser_expect(parser, R_TOKEN_RPAREN, "expected ')' after budget limits");
        (void)r_parse_block(parser);
        return r_parser_close(parser, node);
    }
    if (kind == R_TOKEN_IDENTIFIER && r_task_scope_ahead(parser)) {
        size_t node = r_parser_open(parser, R_SYNTAX_TASK_SCOPE_STATEMENT);
        (void)r_parser_bump(parser);
        (void)r_parser_expect(parser, R_TOKEN_LPAREN, "expected '(' after task_scope");
        if (!r_parse_expression_mode(parser, R_EXPRESSION_CONSTANT).parsed)
            (void)r_parser_syntax_error(parser, "expected task group capacity");
        (void)r_parser_expect(parser, R_TOKEN_RPAREN, "expected ')' after task group capacity");
        (void)r_parser_expect(parser, R_TOKEN_IDENTIFIER, "expected task group name");
        (void)r_parse_block(parser);
        return r_parser_close(parser, node);
    }
    if (kind == R_TOKEN_KW_UNSAFE) {
        size_t node = r_parser_open(parser, R_SYNTAX_UNSAFE_BLOCK);
        (void)r_parser_bump(parser);
        (void)r_parse_block(parser);
        return r_parser_close(parser, node);
    }
    if ((kind == R_TOKEN_KW_BREAK) || (kind == R_TOKEN_KW_CONTINUE) ||
        (kind == R_TOKEN_KW_RETURN)) {
        return r_parse_jump_statement(parser);
    }
    if (kind == R_TOKEN_KW_DROP) {
        size_t node = r_parser_open(parser, R_SYNTAX_DROP_STATEMENT);
        (void)r_parser_bump(parser);
        if (!r_parse_expression_mode(parser, R_EXPRESSION_NORMAL).parsed) {
            (void)r_parser_syntax_error(parser, "expected drop operand");
        }
        (void)r_parser_expect(parser, R_TOKEN_SEMICOLON, "expected ';' after drop");
        return r_parser_close(parser, node);
    }
    if (kind == R_TOKEN_KW_AWAIT) {
        size_t node = r_parser_open(parser, R_SYNTAX_AWAIT_STATEMENT);
        (void)r_parse_await_operation(parser);
        (void)r_parser_expect(parser, R_TOKEN_SEMICOLON, "expected ';' after await");
        return r_parser_close(parser, node);
    }
    if ((kind == R_TOKEN_KW_FN || kind == R_TOKEN_KW_ASYNC) && r_function_type_ahead(parser)) {
        /* R-TYPE-0054 (L28): a local of a function type. */
        return r_parse_object_declaration(parser, true);
    }
    if (kind == R_TOKEN_KW_FN ||
        (kind == R_TOKEN_KW_ASYNC && r_parser_peek_n_kind(parser, 1U) == R_TOKEN_KW_FN)) {
        return r_parse_lambda_declaration(parser);
    }
    if (r_known_statement_type_start(parser)) {
        return r_parse_object_declaration(parser, true);
    }
    if (r_looks_like_unresolved_declaration(parser)) {
        return r_parse_unresolved_declaration(parser, true);
    }
    return r_parse_expression_statement(parser);
}

static bool r_parse_block(RParser *parser) {
    size_t node;
    bool closed;
    if (!r_parser_enter_nesting(parser)) {
        return false;
    }
    node = r_parser_open(parser, R_SYNTAX_BLOCK);
    (void)r_parser_expect(parser, R_TOKEN_LBRACE, "expected block beginning with '{'");
    while (!r_parser_at(parser, R_TOKEN_RBRACE) && !r_parser_at(parser, R_TOKEN_EOF)) {
        size_t before = parser->cursor;
        (void)r_parse_statement(parser);
        if (parser->cursor == before) {
            static const RTokenKind synchronizers[] = {R_TOKEN_SEMICOLON, R_TOKEN_RBRACE};
            (void)r_parser_recover(
                parser, synchronizers, sizeof(synchronizers) / sizeof(synchronizers[0]));
            (void)r_parser_eat(parser, R_TOKEN_SEMICOLON);
        }
    }
    (void)r_parser_expect(parser, R_TOKEN_RBRACE, "expected '}' after block");
    closed = r_parser_close(parser, node);
    r_parser_leave_nesting(parser);
    return closed;
}

RFrontendStatus r_parse_source(RFrontendContext *context, RSourceId source_id) {
    RSource *source = r_get_source(context, source_id);
    RParser parser;
    size_t root;

    if (source == NULL) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    if (source->parsed) {
        return (source->has_lex_error || source->has_syntax_error) ? R_FRONTEND_INVALID_SOURCE
                                                                   : R_FRONTEND_OK;
    }
    {
        /* R-AGG-0012 (L27): derived implementations are parsed with the declarations. */
        const RFrontendStatus derived = r_derive_expand(context, source_id);
        if (derived != R_FRONTEND_OK) {
            return derived;
        }
        /* R-FUNC-0025 (M24): the entry of test mode follows the derived declarations. */
        {
            const RFrontendStatus tested = r_test_expand(context, source_id);
            if (tested != R_FRONTEND_OK) {
                return tested;
            }
        }
        source = r_get_source(context, source_id);
    }
    (void)memset(&parser, 0, sizeof(parser));
    parser.context = context;
    parser.source = source;
    parser.source_id = source_id;
    parser.last_consumed_end = UINT32_C(0);

    source->cst_event_count = 0U;
    source->has_syntax_error = false;
    source->has_unresolved_ambiguity = false;
    root = r_parser_open_raw(&parser, R_SYNTAX_TRANSLATION_UNIT, false);
    if (root == SIZE_MAX) {
        return context->resource_status;
    }
    if (r_parser_module_declaration_ahead(&parser)) {
        (void)r_parse_module_declaration(&parser);
    }
    while (r_parser_at(&parser, R_TOKEN_KW_IMPORT)) {
        (void)r_parse_import_declaration(&parser);
    }
    while (!r_parser_at(&parser, R_TOKEN_EOF)) {
        size_t before = parser.cursor;
        (void)r_parse_external_declaration(&parser);
        if (parser.cursor == before) {
            static const RTokenKind synchronizers[] = {
                R_TOKEN_SEMICOLON,
                R_TOKEN_KW_STRUCT,
                R_TOKEN_KW_ENUM,
                R_TOKEN_KW_EXTERN,
                R_TOKEN_RBRACE,
            };
            (void)r_parser_recover(
                &parser, synchronizers, sizeof(synchronizers) / sizeof(synchronizers[0]));
            (void)r_parser_eat(&parser, R_TOKEN_SEMICOLON);
        }
        if (context->resource_status == R_FRONTEND_OUT_OF_MEMORY) {
            return context->resource_status;
        }
    }
    if (!r_parser_bump(&parser) || !r_parser_close(&parser, root)) {
        return context->resource_status;
    }
    source->cst_root = (RSyntaxNodeId)(root + 1U);
    source->parsed = true;
    return (source->has_lex_error || source->has_syntax_error) ? R_FRONTEND_INVALID_SOURCE
                                                               : R_FRONTEND_OK;
}

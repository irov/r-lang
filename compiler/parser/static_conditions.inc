/* Static predicates are a separate grammar; they are never runtime expressions. A constant
   condition leaf reuses the condition grammar of R-STMT-0002 and is evaluated at translation
   time (R-META-0002). */
static bool r_parse_static_predicate(RParser *parser);
static bool r_parse_static_if(RParser *parser, bool module);

static bool r_static_configuration_ahead(const RParser *parser) {
    size_t index = r_skip_trivia(parser->source, parser->cursor);
    if (!r_parser_token_index_text_is(parser, index, "core")) {
        return false;
    }
    index = r_skip_trivia(parser->source, index + 1U);
    if (r_scan_kind(parser->source, index) != R_TOKEN_COLON_COLON) {
        return false;
    }
    index = r_skip_trivia(parser->source, index + 1U);
    return r_parser_token_index_text_is(parser, index, "profile") ||
           r_parser_token_index_text_is(parser, index, "target");
}

static bool r_static_type_followed_by_is(RParser *parser) {
    RParser trial = *parser;
    const size_t events = parser->source->cst_event_count;
    const bool had_error = parser->source->has_syntax_error;
    bool result;
    trial.suppress_diagnostics = true;
    parser->source->has_syntax_error = false;
    result = r_parse_type(&trial, true) && !parser->source->has_syntax_error &&
             r_parser_token_text_is(&trial, r_parser_peek_token(&trial), "is");
    parser->source->cst_event_count = events;
    parser->source->has_syntax_error = had_error;
    return result;
}

/* static-value-condition = "true" | "false" | condition-value-expression, comparison-operator,
   condition-value-expression | membership-expression ; one leaf of a condition-expression. */
static bool r_parse_static_value_condition(RParser *parser) {
    const size_t expression = r_parser_open(parser, R_SYNTAX_EXPRESSION);
    const size_t leaf = r_parser_open(parser, R_SYNTAX_BINARY_EXPRESSION);
    const RExpressionInfo result = r_parse_condition_primary(parser);
    if (!result.parsed || !result.is_condition) {
        (void)r_parser_syntax_error(parser, "invalid constant condition in static predicate");
    }
    (void)r_parser_close(parser, leaf);
    return r_parser_close(parser, expression) && result.parsed;
}

static bool r_parse_static_atom(RParser *parser) {
    size_t node;
    bool closed;
    if (!r_parser_enter_nesting(parser)) {
        return false;
    }
    node = r_parser_open(parser, R_SYNTAX_STATIC_ATOM);
    if (r_parser_eat(parser, R_TOKEN_BANG)) {
        (void)r_parse_static_atom(parser);
    } else if (r_parser_at(parser, R_TOKEN_LPAREN) && !r_static_type_followed_by_is(parser) &&
               !r_parenthesized_value_operand(parser)) {
        (void)r_parser_bump(parser);
        (void)r_parse_static_predicate(parser);
        (void)r_parser_expect(parser, R_TOKEN_RPAREN, "expected ')' after static predicate");
    } else if (!r_static_configuration_ahead(parser) && !r_static_type_followed_by_is(parser)) {
        /* Only `is` separates a type predicate from a condition over values. */
        (void)r_parse_static_value_condition(parser);
    } else {
        const bool configuration = r_static_configuration_ahead(parser);
        bool profile = false;
        if (configuration) {
            (void)r_parser_bump(parser);
            (void)r_parser_bump(parser);
            profile = r_parser_token_text_is(parser, r_parser_peek_token(parser), "profile");
            (void)r_parser_expect(parser, R_TOKEN_IDENTIFIER, "expected static configuration name");
        } else if (!r_parse_type(parser, true)) {
            (void)r_parser_syntax_error(parser, "expected type in static predicate");
        }
        if (!r_parser_token_text_is(parser, r_parser_peek_token(parser), "is")) {
            (void)r_parser_syntax_error(parser, "expected 'is' in static predicate");
        } else {
            (void)r_parser_bump(parser);
        }
        if (configuration) {
            if (profile) {
                (void)r_parser_expect(parser, R_TOKEN_IDENTIFIER, "expected profile name");
                while (r_parser_eat(parser, R_TOKEN_MINUS)) {
                    if (r_parser_at(parser, R_TOKEN_KW_ASYNC)) {
                        (void)r_parser_bump(parser);
                    } else {
                        (void)r_parser_expect(
                            parser, R_TOKEN_IDENTIFIER, "expected profile name component");
                    }
                }
            } else {
                (void)r_parser_expect(parser, R_TOKEN_STRING_LITERAL, "expected target string");
            }
        } else if (r_parser_at(parser, R_TOKEN_KW_FN) || r_parser_at(parser, R_TOKEN_KW_ASYNC)) {
            const size_t callable = r_parser_open(parser, R_SYNTAX_CALLABLE_CONSTRAINT);
            (void)r_parser_eat(parser, R_TOKEN_KW_ASYNC);
            (void)r_parser_expect(parser, R_TOKEN_KW_FN, "expected fn in callable predicate");
            (void)r_parse_fn_resource_attributes(parser);
            (void)r_parse_callable_mode(parser, false);
            (void)r_parser_expect(parser, R_TOKEN_LPAREN, "expected '(' after fn");
            if (!r_parser_at(parser, R_TOKEN_RPAREN)) {
                do {
                    if (!r_parse_type(parser, true)) {
                        (void)r_parser_syntax_error(parser, "expected callable parameter type");
                        break;
                    }
                } while (r_parser_eat(parser, R_TOKEN_COMMA));
            }
            (void)r_parser_expect(parser, R_TOKEN_RPAREN, "expected ')' after callable parameters");
            (void)r_parser_expect(parser, R_TOKEN_ARROW, "expected '->' before callable result");
            if (!r_parse_type(parser, true)) {
                (void)r_parser_syntax_error(parser, "expected callable result type");
            }
            (void)r_parse_callable_throws(parser);
            (void)r_parser_close(parser, callable);
        } else if (!r_parse_type(parser, true)) {
            (void)r_parser_syntax_error(parser, "expected type, capability or trait after 'is'");
        }
    }
    closed = r_parser_close(parser, node);
    r_parser_leave_nesting(parser);
    return closed;
}

static bool r_parse_static_conjunction(RParser *parser) {
    const size_t node = r_parser_open(parser, R_SYNTAX_STATIC_CONJUNCTION);
    (void)r_parse_static_atom(parser);
    while (r_parser_eat(parser, R_TOKEN_AMP_AMP)) {
        (void)r_parse_static_atom(parser);
    }
    return r_parser_close(parser, node);
}

static bool r_parse_static_predicate(RParser *parser) {
    const size_t node = r_parser_open(parser, R_SYNTAX_STATIC_PREDICATE);
    (void)r_parse_static_conjunction(parser);
    while (r_parser_eat(parser, R_TOKEN_PIPE_PIPE)) {
        (void)r_parse_static_conjunction(parser);
    }
    return r_parser_close(parser, node);
}

static bool r_parse_static_declarations(RParser *parser) {
    const size_t node = r_parser_open(parser, R_SYNTAX_STATIC_DECLARATIONS);
    (void)r_parser_expect(parser, R_TOKEN_LBRACE, "expected '{' before static declarations");
    while (!r_parser_at(parser, R_TOKEN_RBRACE) && !r_parser_at(parser, R_TOKEN_EOF)) {
        const size_t before = parser->cursor;
        (void)r_parse_external_declaration(parser);
        if (before == parser->cursor && !r_parser_bump(parser)) {
            break;
        }
    }
    (void)r_parser_expect(parser, R_TOKEN_RBRACE, "expected '}' after static declarations");
    return r_parser_close(parser, node);
}

static bool r_parse_static_if(RParser *parser, bool module) {
    size_t node;
    bool closed;
    if (!r_parser_enter_nesting(parser)) {
        return false;
    }
    node = r_parser_open(parser,
                         module ? R_SYNTAX_STATIC_IF_DECLARATION : R_SYNTAX_STATIC_IF_STATEMENT);
    (void)r_parser_expect(parser, R_TOKEN_AT, "expected '@' before static if");
    (void)r_parser_expect(parser, R_TOKEN_KW_IF, "expected static if");
    (void)r_parser_expect(parser, R_TOKEN_LPAREN, "expected '(' before static predicate");
    (void)r_parse_static_predicate(parser);
    (void)r_parser_expect(parser, R_TOKEN_RPAREN, "expected ')' after static predicate");
    (void)(module ? r_parse_static_declarations(parser) : r_parse_block(parser));
    if (r_parser_at(parser, R_TOKEN_AT) && r_parser_peek_n_kind(parser, 1U) == R_TOKEN_KW_ELSE) {
        (void)r_parser_bump(parser);
        (void)r_parser_bump(parser);
        if (r_parser_at(parser, R_TOKEN_AT) && r_parser_peek_n_kind(parser, 1U) == R_TOKEN_KW_IF) {
            const size_t branch =
                r_parser_open(parser, module ? R_SYNTAX_STATIC_DECLARATIONS : R_SYNTAX_BLOCK);
            (void)r_parse_static_if(parser, module);
            (void)r_parser_close(parser, branch);
        } else {
            (void)(module ? r_parse_static_declarations(parser) : r_parse_block(parser));
        }
    }
    closed = r_parser_close(parser, node);
    r_parser_leave_nesting(parser);
    return closed;
}

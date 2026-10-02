/* Match is contextual: an ordinary call named match keeps its existing grammar. */
static bool r_match_expression_ahead(const RParser *parser) {
    size_t index = r_skip_trivia(parser->source, parser->cursor);
    if (!r_parser_token_index_text_is(parser, index, "match"))
        return false;
    index = r_skip_trivia(parser->source, index + 1U);
    index = r_skip_balanced_parentheses(parser, index);
    return index != 0U && r_scan_kind(parser->source, index) == R_TOKEN_LBRACE;
}

static bool r_parse_match_pattern(RParser *parser) {
    if (!r_parser_enter_nesting(parser))
        return false;
    const size_t node = r_parser_open(parser, R_SYNTAX_MATCH_PATTERN);
    if (r_parser_eat(parser, R_TOKEN_KW_VARIANT)) {
        (void)r_parse_qualified_name(parser);
        if (r_parser_eat(parser, R_TOKEN_LPAREN)) {
            (void)r_parse_match_pattern(parser);
            (void)r_parser_expect(parser, R_TOKEN_RPAREN, "expected ')' after payload pattern");
        } else if (r_parser_at(parser, R_TOKEN_LBRACE)) {
            (void)r_parse_match_pattern(parser);
        }
    } else if (r_parser_eat(parser, R_TOKEN_LBRACE)) {
        while (!r_parser_at(parser, R_TOKEN_RBRACE) && !r_parser_at(parser, R_TOKEN_EOF)) {
            const size_t field = r_parser_open(parser, R_SYNTAX_MATCH_FIELD);
            (void)r_parser_expect(parser, R_TOKEN_DOT, "expected '.' before pattern field");
            /* R-TYPE-0052 (L18.1): `.0 = pattern` matches an element of a tuple. */
            if (!r_parser_eat(parser, R_TOKEN_INTEGER_LITERAL)) {
                (void)r_parser_expect(parser, R_TOKEN_IDENTIFIER, "expected pattern field name");
            }
            (void)r_parser_expect(parser, R_TOKEN_EQUAL, "expected '=' after pattern field");
            const bool parsed = r_parse_match_pattern(parser);
            (void)r_parser_close(parser, field);
            if (!parsed || !r_parser_eat(parser, R_TOKEN_COMMA))
                break;
        }
        (void)r_parser_expect(parser, R_TOKEN_RBRACE, "expected '}' after field patterns");
    } else if (r_parser_eat(parser, R_TOKEN_KW_MOVE)) {
        (void)r_parser_expect(parser, R_TOKEN_IDENTIFIER, "expected move binding name");
    } else if (r_parser_at(parser, R_TOKEN_IDENTIFIER) && !r_qualified_name_ahead(parser)) {
        (void)r_parser_bump(parser);
    } else if (!r_parse_expression_mode(parser, R_EXPRESSION_CONSTANT).parsed) {
        (void)r_parser_syntax_error(parser,
                                    "expected a binding, wildcard, constant or payload pattern");
        (void)r_parser_close(parser, node);
        r_parser_leave_nesting(parser);
        return false;
    }
    const bool closed = r_parser_close(parser, node);
    r_parser_leave_nesting(parser);
    return closed;
}

static bool r_parse_match_expression(RParser *parser) {
    const size_t node = r_parser_open(parser, R_SYNTAX_MATCH_EXPRESSION);
    (void)r_parser_bump(parser);
    (void)r_parser_expect(parser, R_TOKEN_LPAREN, "expected '(' after match");
    if (!r_parse_expression_mode(parser, R_EXPRESSION_NORMAL).parsed)
        (void)r_parser_syntax_error(parser, "expected match value");
    (void)r_parser_expect(parser, R_TOKEN_RPAREN, "expected ')' after match value");
    (void)r_parser_expect(parser, R_TOKEN_LBRACE, "expected '{' after match value");
    bool saw_arm = false;
    while (!r_parser_at(parser, R_TOKEN_RBRACE) && !r_parser_at(parser, R_TOKEN_EOF)) {
        const size_t before = parser->cursor;
        const size_t arm = r_parser_open(parser, R_SYNTAX_MATCH_ARM);
        saw_arm = true;
        if (r_parser_eat(parser, R_TOKEN_KW_CASE)) {
            (void)r_parse_match_pattern(parser);
            if (r_parser_at(parser, R_TOKEN_KW_IF)) {
                const size_t guard = r_parser_open(parser, R_SYNTAX_MATCH_GUARD);
                (void)r_parser_bump(parser);
                (void)r_parser_expect(parser, R_TOKEN_LPAREN, "expected '(' before match guard");
                const RExpressionInfo condition = r_parse_condition_expression(parser);
                if (!condition.parsed || !condition.is_condition)
                    (void)r_parser_syntax_error(parser, "invalid explicit match guard");
                (void)r_parser_expect(parser, R_TOKEN_RPAREN, "expected ')' after match guard");
                (void)r_parser_close(parser, guard);
            }
        } else {
            (void)r_parser_expect(parser, R_TOKEN_KW_DEFAULT, "expected case or default in match");
        }
        (void)r_parser_expect(parser, R_TOKEN_COLON, "expected ':' before match result");
        if (r_parser_at(parser, R_TOKEN_KW_THROW) && !r_conditional_throw_ahead(parser)) {
            /* R-EXPR-0031 (L20.2): an arm may throw; the throw statement ends the arm. */
            (void)r_parse_throw_statement(parser);
        } else {
            if (!r_parse_expression_mode(parser, R_EXPRESSION_NORMAL).parsed)
                (void)r_parser_syntax_error(parser, "expected match arm result");
            (void)r_parser_expect(parser, R_TOKEN_SEMICOLON, "expected ';' after match arm");
        }
        (void)r_parser_close(parser, arm);
        if (before == parser->cursor) {
            static const RTokenKind recovery[] = {
                R_TOKEN_SEMICOLON, R_TOKEN_KW_CASE, R_TOKEN_KW_DEFAULT, R_TOKEN_RBRACE};
            (void)r_parser_recover(parser, recovery, sizeof(recovery) / sizeof(recovery[0]));
            (void)r_parser_eat(parser, R_TOKEN_SEMICOLON);
            if (before == parser->cursor && !r_parser_at(parser, R_TOKEN_RBRACE))
                (void)r_parser_bump(parser);
        }
    }
    if (!saw_arm)
        (void)r_parser_syntax_error(parser, "match requires at least one arm");
    (void)r_parser_expect(parser, R_TOKEN_RBRACE, "expected '}' after match arms");
    return r_parser_close(parser, node);
}

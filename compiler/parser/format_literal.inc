static bool r_format_sequence_ahead(const RParser *parser) {
    size_t lookahead = 0U;
    while (r_parser_peek_n_kind(parser, lookahead) == R_TOKEN_STRING_LITERAL) {
        ++lookahead;
    }
    return r_parser_peek_n_kind(parser, lookahead) == R_TOKEN_FORMAT_START;
}

static bool r_parse_format_sequence(RParser *parser) {
    size_t sequence = r_parser_open(parser, R_SYNTAX_FORMAT_SEQUENCE);
    do {
        if (r_parser_at(parser, R_TOKEN_STRING_LITERAL)) {
            (void)r_parser_bump(parser);
        } else {
            size_t literal = r_parser_open(parser, R_SYNTAX_FORMAT_LITERAL);
            (void)r_parser_bump(parser);
            while (!r_parser_at(parser, R_TOKEN_FORMAT_END) && !r_parser_at(parser, R_TOKEN_EOF)) {
                if (r_parser_at(parser, R_TOKEN_FORMAT_TEXT)) {
                    (void)r_parser_bump(parser);
                } else if (r_parser_at(parser, R_TOKEN_LBRACE)) {
                    size_t slot = r_parser_open(parser, R_SYNTAX_FORMAT_SLOT);
                    (void)r_parser_bump(parser);
                    if (r_parser_at(parser, R_TOKEN_INTEGER_LITERAL)) {
                        (void)r_parser_bump(parser);
                    } else {
                        size_t path = r_parser_open(parser, R_SYNTAX_POSTFIX_EXPRESSION);
                        size_t primary = r_parser_open(parser, R_SYNTAX_PRIMARY_EXPRESSION);
                        /* The receiver parameter `this` roots a path like any parameter. */
                        if (!r_parser_eat(parser, R_TOKEN_KW_THIS)) {
                            (void)r_parser_expect(
                                parser,
                                R_TOKEN_IDENTIFIER,
                                "expected capture path or positive argument index");
                        }
                        (void)r_parser_close(parser, primary);
                        while (r_parser_at(parser, R_TOKEN_DOT) ||
                               r_parser_at(parser, R_TOKEN_ARROW)) {
                            size_t member = r_parser_open(parser, R_SYNTAX_MEMBER_SUFFIX);
                            (void)r_parser_bump(parser);
                            (void)r_parser_expect(
                                parser, R_TOKEN_IDENTIFIER, "expected capture field");
                            (void)r_parser_close(parser, member);
                        }
                        (void)r_parser_close(parser, path);
                    }
                    if (r_parser_eat(parser, R_TOKEN_COLON)) {
                        (void)r_parser_expect(
                            parser, R_TOKEN_FORMAT_SPEC, "expected format specification");
                    }
                    if (!r_parser_at(parser, R_TOKEN_RBRACE)) {
                        (void)r_parser_syntax_error(parser, "expected '}' after format slot");
                        while (!r_parser_at(parser, R_TOKEN_RBRACE) &&
                               !r_parser_at(parser, R_TOKEN_FORMAT_END) &&
                               !r_parser_at(parser, R_TOKEN_EOF)) {
                            (void)r_parser_bump(parser);
                        }
                    }
                    (void)r_parser_expect(parser, R_TOKEN_RBRACE, "expected '}' after format slot");
                    (void)r_parser_close(parser, slot);
                } else {
                    break;
                }
            }
            (void)r_parser_expect(
                parser, R_TOKEN_FORMAT_END, "expected closing quote in formatted literal");
            (void)r_parser_close(parser, literal);
        }
    } while (r_parser_at(parser, R_TOKEN_STRING_LITERAL) ||
             r_parser_at(parser, R_TOKEN_FORMAT_START));
    return r_parser_close(parser, sequence);
}

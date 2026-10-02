/* Generated output is committed in lexer_generated.c. */
#include "frontend_internal.h"

static RAsciiLexeme r_ascii_lexeme(RAsciiLexemeKind kind,
                                   RTokenKind token_kind,
                                   const uint8_t *start,
                                   const uint8_t *end) {
    RAsciiLexeme result;
    result.kind = kind;
    result.token_kind = token_kind;
    result.length = (size_t)(end - start);
    return result;
}

RAsciiLexeme r_lexer_scan_ascii(const uint8_t *cursor) {
    const uint8_t *token_start = cursor;
    const uint8_t *YYCURSOR = cursor;

/*!re2c
        re2c:define:YYCTYPE = uint8_t;
        re2c:yyfill:enable = 0;

        [ \t\v\f\r\n]+ {
            return r_ascii_lexeme(R_ASCII_LEXEME_WHITESPACE, R_TOKEN_WHITESPACE,
                                  token_start, YYCURSOR);
        }
        "//" [^\x00\r\n]* {
            return r_ascii_lexeme(R_ASCII_LEXEME_LINE_COMMENT, R_TOKEN_LINE_COMMENT,
                                  token_start, YYCURSOR);
        }
        "/*" {
            return r_ascii_lexeme(R_ASCII_LEXEME_BLOCK_COMMENT_START, R_TOKEN_BLOCK_COMMENT,
                                  token_start, YYCURSOR);
        }
        [A-Za-z_] [A-Za-z0-9_]* {
            return r_ascii_lexeme(R_ASCII_LEXEME_IDENTIFIER, R_TOKEN_IDENTIFIER,
                                  token_start, YYCURSOR);
        }
        [0-9] {
            return r_ascii_lexeme(R_ASCII_LEXEME_NUMBER_START, R_TOKEN_INVALID,
                                  token_start, YYCURSOR);
        }
        "\"" {
            return r_ascii_lexeme(R_ASCII_LEXEME_STRING_START, R_TOKEN_STRING_LITERAL,
                                  token_start, YYCURSOR);
        }
        "'" {
            return r_ascii_lexeme(R_ASCII_LEXEME_CHARACTER_START, R_TOKEN_CHARACTER_LITERAL,
                                  token_start, YYCURSOR);
        }

        "<<=" { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_LESS_LESS_EQUAL, token_start, YYCURSOR); }
        ">>=" { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_GREATER_GREATER_EQUAL, token_start, YYCURSOR); }
        "..." { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_ELLIPSIS, token_start, YYCURSOR); }
        ".."  { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_DOT_DOT, token_start, YYCURSOR); }
        "::"  { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_COLON_COLON, token_start, YYCURSOR); }
        "++"  { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_PLUS_PLUS, token_start, YYCURSOR); }
        "--"  { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_MINUS_MINUS, token_start, YYCURSOR); }
        "->"  { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_ARROW, token_start, YYCURSOR); }
        "=="  { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_EQUAL_EQUAL, token_start, YYCURSOR); }
        "!="  { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_BANG_EQUAL, token_start, YYCURSOR); }
        "<="  { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_LESS_EQUAL, token_start, YYCURSOR); }
        ">="  { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_GREATER_EQUAL, token_start, YYCURSOR); }
        "&&"  { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_AMP_AMP, token_start, YYCURSOR); }
        "||"  { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_PIPE_PIPE, token_start, YYCURSOR); }
        "<<"  { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_LESS_LESS, token_start, YYCURSOR); }
        ">>"  { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_GREATER_GREATER, token_start, YYCURSOR); }
        "+="  { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_PLUS_EQUAL, token_start, YYCURSOR); }
        "-="  { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_MINUS_EQUAL, token_start, YYCURSOR); }
        "*="  { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_STAR_EQUAL, token_start, YYCURSOR); }
        "/="  { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_SLASH_EQUAL, token_start, YYCURSOR); }
        "%="  { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_PERCENT_EQUAL, token_start, YYCURSOR); }
        "&="  { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_AMP_EQUAL, token_start, YYCURSOR); }
        "|="  { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_PIPE_EQUAL, token_start, YYCURSOR); }
        "^="  { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_CARET_EQUAL, token_start, YYCURSOR); }

        "{" { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_LBRACE, token_start, YYCURSOR); }
        "}" { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_RBRACE, token_start, YYCURSOR); }
        "[" { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_LBRACKET, token_start, YYCURSOR); }
        "]" { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_RBRACKET, token_start, YYCURSOR); }
        "(" { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_LPAREN, token_start, YYCURSOR); }
        ")" { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_RPAREN, token_start, YYCURSOR); }
        ";" { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_SEMICOLON, token_start, YYCURSOR); }
        "," { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_COMMA, token_start, YYCURSOR); }
        "." { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_DOT, token_start, YYCURSOR); }
        ":" { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_COLON, token_start, YYCURSOR); }
        "?" { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_QUESTION, token_start, YYCURSOR); }
        "@" { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_AT, token_start, YYCURSOR); }
        "+" { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_PLUS, token_start, YYCURSOR); }
        "-" { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_MINUS, token_start, YYCURSOR); }
        "*" { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_STAR, token_start, YYCURSOR); }
        "/" { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_SLASH, token_start, YYCURSOR); }
        "%" { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_PERCENT, token_start, YYCURSOR); }
        "&" { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_AMP, token_start, YYCURSOR); }
        "|" { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_PIPE, token_start, YYCURSOR); }
        "^" { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_CARET, token_start, YYCURSOR); }
        "~" { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_TILDE, token_start, YYCURSOR); }
        "!" { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_BANG, token_start, YYCURSOR); }
        "=" { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_EQUAL, token_start, YYCURSOR); }
        "<" { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_LESS, token_start, YYCURSOR); }
        ">" { return r_ascii_lexeme(R_ASCII_LEXEME_PUNCTUATOR, R_TOKEN_GREATER, token_start, YYCURSOR); }

        [\x80-\xFF] {
            return r_ascii_lexeme(R_ASCII_LEXEME_NON_ASCII, R_TOKEN_INVALID,
                                  token_start, YYCURSOR);
        }
        * {
            return r_ascii_lexeme(R_ASCII_LEXEME_UNKNOWN, R_TOKEN_INVALID,
                                  token_start, YYCURSOR);
        }
*/
}

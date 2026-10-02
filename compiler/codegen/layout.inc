/* Line breaking for one generated C17 statement.

   SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
   This file is derived from clang-format of the LLVM Project, release 22.1.8
   (clang/lib/Format/TokenAnnotator.cpp, ContinuationIndenter.cpp and UnwrappedLineFormatter.cpp),
   Copyright the LLVM Project contributors, licensed under the Apache License v2.0 with LLVM
   Exceptions; the license text is in LICENSE-LLVM.txt of this directory. Changes: translated
   from C++ to C17 and restricted to the statement forms that the R emitter produces.

   The emitter is required to produce C17 that clang-format 22.1.8 leaves unchanged under the
   repository style (LLVM base, four-space indentation, 100 columns, arguments never bin-packed).
   This file reproduces the decision procedure of that formatter for the statement forms the
   emitter produces: declarations with initializers, assignments, calls, casts, compound
   literals, member chains, subscripts, unary and binary operators and conditionals. It is a
   port of the relevant parts of TokenAnnotator, ContinuationIndenter and the optimizing line
   formatter: tokens are annotated with nesting, binding strength and split penalties, then
   the cheapest sequence of line-break decisions is searched with the same state model and
   the same penalties (15 for the first break inside a bracket level, 19 after an opening
   bracket, 2 after an assignment, 1 after a comma, 20 per bracket level, a prohibitive
   amount per character beyond the limit).

   The engine is self-contained: it reads a statement without its indentation, receives the
   indentation of the first line and writes the laid-out statement with LF-separated lines.
   A construct outside the modeled forms yields R_LAYOUT_UNSUPPORTED and the caller keeps the
   statement as written. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define R_LAYOUT_COLUMN_LIMIT 100U
#define R_LAYOUT_CONTINUATION 4U
#define R_LAYOUT_MAX_TOKENS 512U
#define R_LAYOUT_MAX_STACK 40U
#define R_LAYOUT_MAX_FAKE 8U
#define R_LAYOUT_EXCESS_PENALTY 1000000U
#define R_LAYOUT_FIRST_BREAK_PENALTY 15U
#define R_LAYOUT_OPEN_BRACKET_PENALTY 19U
#define R_LAYOUT_MEMBER_ACCESS_PENALTY 150U
#define R_LAYOUT_STRENGTH_PENALTY 20U
#define R_LAYOUT_FIRST_SHIFT_PENALTY 120U
#define R_LAYOUT_TYPE_KEYWORD_PENALTY 5000U
#define R_LAYOUT_CONDITION_PAREN_PENALTY 1000U
#define R_LAYOUT_MAX_STATES 60000U
#define R_LAYOUT_INITIAL_STATES 512U

/* Operator precedence levels of clang, with the two synthetic levels of its expression
   parser above them. */
#define R_PREC_UNKNOWN 0
#define R_PREC_COMMA 1
#define R_PREC_ASSIGNMENT 2
#define R_PREC_CONDITIONAL 3
#define R_PREC_LOGICAL_OR 4
#define R_PREC_LOGICAL_AND 5
#define R_PREC_INCLUSIVE_OR 6
#define R_PREC_EXCLUSIVE_OR 7
#define R_PREC_AND 8
#define R_PREC_EQUALITY 9
#define R_PREC_RELATIONAL 10
#define R_PREC_SHIFT 12
#define R_PREC_ADDITIVE 13
#define R_PREC_MULTIPLICATIVE 14
#define R_PREC_POINTER_TO_MEMBER 15
#define R_PREC_UNARY 16
#define R_PREC_ARROW_AND_PERIOD 17

typedef enum RLayoutStatus {
    R_LAYOUT_DONE = 0,
    R_LAYOUT_UNSUPPORTED,
    R_LAYOUT_NO_MEMORY,
    R_LAYOUT_LIMIT
} RLayoutStatus;

typedef struct RLayoutRequest {
    const char *text;
    size_t length;
    unsigned indent;
    void *(*allocate)(void *context, size_t size);
    void (*release)(void *context, void *block);
    void *context;
} RLayoutRequest;

typedef enum RLayoutKind {
    R_LAYOUT_IDENT = 0,
    R_LAYOUT_LPAREN,
    R_LAYOUT_RPAREN,
    R_LAYOUT_LBRACE,
    R_LAYOUT_RBRACE,
    R_LAYOUT_LSQUARE,
    R_LAYOUT_RSQUARE,
    R_LAYOUT_COMMA,
    R_LAYOUT_SEMI,
    R_LAYOUT_PERIOD,
    R_LAYOUT_ARROW,
    R_LAYOUT_OPERATOR,
    R_LAYOUT_QUESTION,
    R_LAYOUT_COLON,
    R_LAYOUT_STRING,
    R_LAYOUT_NUMBER
} RLayoutKind;

/* The token types of clang-format that influence the decisions modeled here. */
typedef enum RLayoutType {
    R_LAYOUT_TT_UNKNOWN = 0,
    R_LAYOUT_TT_CAST_RPAREN,
    R_LAYOUT_TT_UNARY,
    R_LAYOUT_TT_TRAILING_UNARY,
    R_LAYOUT_TT_BINARY,
    R_LAYOUT_TT_CONDITIONAL,
    R_LAYOUT_TT_START_OF_NAME,
    R_LAYOUT_TT_POINTER,
    R_LAYOUT_TT_SUBSCRIPT,
    R_LAYOUT_TT_DESIGNATED
} RLayoutType;

typedef struct RLayoutToken {
    RLayoutKind kind;
    RLayoutType type;
    const char *text;
    unsigned length;
    int matching;
    unsigned nesting;
    unsigned parameter_count;
    unsigned binding_strength;
    unsigned spaces_before;
    unsigned total_length;
    unsigned split_penalty;
    unsigned precedence;
    bool can_break_before;
    bool must_break_before;
    bool is_return;
    bool is_control;
    bool is_type_word;
    unsigned char fake_lparens[R_LAYOUT_MAX_FAKE];
    unsigned fake_lparen_count;
    unsigned fake_rparens;
    bool starts_binary_expression;
    bool ends_binary_expression;
    int next_operator;
    unsigned operator_index;
} RLayoutToken;

typedef struct RLayoutParen {
    int16_t tok;
    uint16_t indent;
    uint16_t last_space;
    uint16_t nested_block_indent;
    uint16_t question_column;
    uint16_t start_of_function_call;
    uint16_t start_of_array_subscripts;
    uint16_t call_continuation;
    uint16_t variable_pos;
    uint16_t first_less_less;
    bool is_aligned;
    bool break_before_closing_brace;
    bool avoid_bin_packing;
    bool break_before_parameter;
    bool no_line_break;
    bool no_line_break_in_operand;
    bool last_operator_wrapped;
    bool contains_line_break;
    bool contains_unwrapped_builder;
    bool nested_block_inlined;
    bool is_chained_conditional;
    bool is_wrapped_conditional;
    bool unindent_operator;
} RLayoutParen;

/* The search hashes and compares paren states bytewise, so they must carry no padding. */
_Static_assert(sizeof(RLayoutParen) == 34U, "RLayoutParen must be packed");

typedef struct RLayoutState {
    uint16_t column;
    int16_t next;
    uint8_t start_of_line_level;
    uint8_t lowest_level_on_line;
    uint8_t stack_count;
    bool no_line_break;
    uint16_t first_indent;
    RLayoutParen stack[R_LAYOUT_MAX_STACK];
} RLayoutState;

typedef struct RLayoutLine {
    RLayoutToken *tokens;
    unsigned count;
} RLayoutLine;

typedef struct RLayoutNode {
    RLayoutState state;
    unsigned penalty;
    unsigned order;
    int previous;
    bool newline;
} RLayoutNode;

typedef struct RLayoutHash {
    int *slots;
    unsigned capacity;
    unsigned count;
} RLayoutHash;

typedef struct RLayoutHeapItem {
    unsigned penalty;
    unsigned order;
    int node;
} RLayoutHeapItem;

/* --- tokens ---------------------------------------------------------------------------- */

static bool r_layout_is_ident_byte(char value) {
    return ((value >= 'a') && (value <= 'z')) || ((value >= 'A') && (value <= 'Z')) ||
           ((value >= '0') && (value <= '9')) || (value == '_');
}

static bool r_layout_text_is(const RLayoutToken *token, const char *text) {
    size_t length = strlen(text);
    return (token->length == length) && (memcmp(token->text, text, length) == 0);
}

static bool r_layout_tokenize(const char *text, size_t length, RLayoutLine *line) {
    static const char *const punctuators[] = {
        "<<=", ">>=", "->", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||", "++", "--", "+=", "-=",
        "*=",  "/=",  "%=", "&=", "|=", "^=", "(",  ")",  "{",  "}",  "[",  "]",  ",",  ";",  ".",
        "?",   ":",   "=",  "+",  "-",  "*",  "/",  "%",  "&",  "|",  "^",  "!",  "~",  "<",  ">",
    };
    size_t offset = 0;

    line->count = 0;
    while (offset < length) {
        RLayoutToken *token;
        char first = text[offset];
        size_t index;

        if ((first == ' ') || (first == '\t')) {
            offset += 1;
            continue;
        }
        if (line->count >= R_LAYOUT_MAX_TOKENS) {
            return false;
        }
        token = &line->tokens[line->count];
        (void)memset(token, 0, sizeof(*token));
        token->text = text + offset;
        token->matching = -1;
        token->next_operator = -1;
        if (r_layout_is_ident_byte(first)) {
            size_t end = offset;
            bool number = (first >= '0') && (first <= '9');
            while ((end < length) &&
                   (r_layout_is_ident_byte(text[end]) || (number && (text[end] == '.')) ||
                    (number && ((text[end] == '+') || (text[end] == '-')) &&
                     ((text[end - 1] == 'e') || (text[end - 1] == 'E') || (text[end - 1] == 'p') ||
                      (text[end - 1] == 'P'))))) {
                end += 1;
            }
            token->kind = number ? R_LAYOUT_NUMBER : R_LAYOUT_IDENT;
            token->length = (unsigned)(end - offset);
            offset = end;
            line->count += 1;
            continue;
        }
        if ((first == '"') || (first == '\'')) {
            size_t end = offset + 1;
            while ((end < length) && (text[end] != first)) {
                end += (text[end] == '\\') ? 2 : 1;
            }
            if (end >= length) {
                return false;
            }
            token->kind = R_LAYOUT_STRING;
            token->length = (unsigned)(end + 1 - offset);
            offset = end + 1;
            line->count += 1;
            continue;
        }
        for (index = 0; index < sizeof(punctuators) / sizeof(punctuators[0]); ++index) {
            size_t width = strlen(punctuators[index]);
            if ((offset + width <= length) &&
                (memcmp(text + offset, punctuators[index], width) == 0)) {
                break;
            }
        }
        if (index == sizeof(punctuators) / sizeof(punctuators[0])) {
            return false;
        }
        token->length = (unsigned)strlen(punctuators[index]);
        switch (first) {
        case '(':
            token->kind = R_LAYOUT_LPAREN;
            break;
        case ')':
            token->kind = R_LAYOUT_RPAREN;
            break;
        case '{':
            token->kind = R_LAYOUT_LBRACE;
            break;
        case '}':
            token->kind = R_LAYOUT_RBRACE;
            break;
        case '[':
            token->kind = R_LAYOUT_LSQUARE;
            break;
        case ']':
            token->kind = R_LAYOUT_RSQUARE;
            break;
        case ',':
            token->kind = R_LAYOUT_COMMA;
            break;
        case ';':
            token->kind = R_LAYOUT_SEMI;
            break;
        case '.':
            token->kind = R_LAYOUT_PERIOD;
            break;
        case '?':
            token->kind = R_LAYOUT_QUESTION;
            break;
        case ':':
            token->kind = R_LAYOUT_COLON;
            break;
        default:
            token->kind = (token->length == 2U && first == '-' && text[offset + 1] == '>')
                              ? R_LAYOUT_ARROW
                              : R_LAYOUT_OPERATOR;
            break;
        }
        offset += token->length;
        line->count += 1;
    }
    return line->count != 0;
}

/* --- annotation ------------------------------------------------------------------------ */

static bool r_layout_opens_scope(const RLayoutToken *token) {
    return (token->kind == R_LAYOUT_LPAREN) || (token->kind == R_LAYOUT_LBRACE) ||
           (token->kind == R_LAYOUT_LSQUARE);
}

static bool r_layout_closes_scope(const RLayoutToken *token) {
    return (token->kind == R_LAYOUT_RPAREN) || (token->kind == R_LAYOUT_RBRACE) ||
           (token->kind == R_LAYOUT_RSQUARE);
}

static bool r_layout_is_member_access(const RLayoutToken *token) {
    return ((token->kind == R_LAYOUT_PERIOD) || (token->kind == R_LAYOUT_ARROW)) &&
           (token->type != R_LAYOUT_TT_DESIGNATED);
}

static bool r_layout_is_conditional(const RLayoutToken *token) {
    return token->type == R_LAYOUT_TT_CONDITIONAL;
}

static bool r_layout_is_shift_left(const RLayoutToken *token) {
    return (token->kind == R_LAYOUT_OPERATOR) && (token->type == R_LAYOUT_TT_BINARY) &&
           r_layout_text_is(token, "<<");
}

/* A break is allowed after 'struct' at a high penalty; 'union' never breaks. */
static bool r_layout_is_aggregate_keyword(const RLayoutToken *token) {
    return (token->kind == R_LAYOUT_IDENT) && r_layout_text_is(token, "struct");
}

/* clang's FormatToken::isBinaryOperator: any precedence above the comma. */
static bool r_layout_is_binary_operator(const RLayoutToken *token) {
    return token->precedence > R_PREC_COMMA;
}

static unsigned r_layout_operator_precedence(const RLayoutToken *token) {
    static const struct {
        const char *text;
        unsigned precedence;
    } table[] = {
        {"=", R_PREC_ASSIGNMENT},     {"+=", R_PREC_ASSIGNMENT},
        {"-=", R_PREC_ASSIGNMENT},    {"*=", R_PREC_ASSIGNMENT},
        {"/=", R_PREC_ASSIGNMENT},    {"%=", R_PREC_ASSIGNMENT},
        {"&=", R_PREC_ASSIGNMENT},    {"|=", R_PREC_ASSIGNMENT},
        {"^=", R_PREC_ASSIGNMENT},    {"<<=", R_PREC_ASSIGNMENT},
        {">>=", R_PREC_ASSIGNMENT},   {"||", R_PREC_LOGICAL_OR},
        {"&&", R_PREC_LOGICAL_AND},   {"|", R_PREC_INCLUSIVE_OR},
        {"^", R_PREC_EXCLUSIVE_OR},   {"&", R_PREC_AND},
        {"==", R_PREC_EQUALITY},      {"!=", R_PREC_EQUALITY},
        {"<", R_PREC_RELATIONAL},     {">", R_PREC_RELATIONAL},
        {"<=", R_PREC_RELATIONAL},    {">=", R_PREC_RELATIONAL},
        {"<<", R_PREC_SHIFT},         {">>", R_PREC_SHIFT},
        {"+", R_PREC_ADDITIVE},       {"-", R_PREC_ADDITIVE},
        {"*", R_PREC_MULTIPLICATIVE}, {"/", R_PREC_MULTIPLICATIVE},
        {"%", R_PREC_MULTIPLICATIVE},
    };
    size_t index;

    for (index = 0; index < sizeof(table) / sizeof(table[0]); ++index) {
        if (r_layout_text_is(token, table[index].text)) {
            return table[index].precedence;
        }
    }
    return R_PREC_UNKNOWN;
}

static bool r_layout_is_type_word(const RLayoutToken *token) {
    static const char *const words[] = {
        "unsigned",
        "signed",
        "const",
        "volatile",
        "static",
        "struct",
        "enum",
        "union",
        "long",
        "short",
        "int",
        "char",
        "void",
        "double",
        "float",
        "_Bool",
    };
    size_t index;

    for (index = 0; index < sizeof(words) / sizeof(words[0]); ++index) {
        if (r_layout_text_is(token, words[index])) {
            return true;
        }
    }
    return false;
}

/* Matching brackets, nesting levels, parameter counts and binding strengths. */
static bool r_layout_annotate_scopes(RLayoutLine *line) {
    int stack[R_LAYOUT_MAX_STACK];
    unsigned strength[R_LAYOUT_MAX_STACK];
    unsigned depth = 0;
    unsigned index;

    strength[0] = 1U;
    for (index = 0; index < line->count; ++index) {
        RLayoutToken *token = &line->tokens[index];
        token->nesting = depth;
        token->binding_strength = strength[depth];
        if (r_layout_opens_scope(token)) {
            if (depth + 1U >= R_LAYOUT_MAX_STACK) {
                return false;
            }
            stack[depth] = (int)index;
            strength[depth + 1U] = strength[depth] + 1U;
            depth += 1U;
            token->parameter_count = 0U;
        } else if (r_layout_closes_scope(token)) {
            RLayoutToken *opener;
            if (depth == 0U) {
                /* The brace closing the previous block before 'else' or 'while'. */
                if ((index == 0U) && (token->kind == R_LAYOUT_RBRACE)) {
                    continue;
                }
                return false;
            }
            depth -= 1U;
            opener = &line->tokens[stack[depth]];
            if (((opener->kind == R_LAYOUT_LPAREN) && (token->kind != R_LAYOUT_RPAREN)) ||
                ((opener->kind == R_LAYOUT_LBRACE) && (token->kind != R_LAYOUT_RBRACE)) ||
                ((opener->kind == R_LAYOUT_LSQUARE) && (token->kind != R_LAYOUT_RSQUARE))) {
                return false;
            }
            opener->matching = (int)index;
            token->matching = stack[depth];
            /* The closing bracket keeps the level and strength of the bracket's content. */
            token->nesting = depth + 1U;
            token->binding_strength = strength[depth + 1U];
        } else if (token->kind == R_LAYOUT_COMMA) {
            if (depth != 0U) {
                line->tokens[stack[depth - 1U]].parameter_count += 1U;
            }
        } else if (depth != 0U) {
            RLayoutToken *opener = &line->tokens[stack[depth - 1U]];
            if (opener->parameter_count == 0U) {
                opener->parameter_count = 1U;
            }
        }
    }
    /* A control header ends with the brace that opens its block. */
    if ((depth == 1U) && (line->tokens[line->count - 1U].kind == R_LAYOUT_LBRACE) &&
        (stack[0] == (int)line->count - 1)) {
        depth = 0U;
    }
    if (depth != 0U) {
        return false;
    }
    /* A comma counts the parameter it ends; the last parameter follows the last comma. */
    for (index = 0; index < line->count; ++index) {
        RLayoutToken *token = &line->tokens[index];
        if (r_layout_opens_scope(token) && (token->matching > (int)index + 1) &&
            (token->parameter_count != 0U)) {
            unsigned commas = 0U;
            int inner;
            unsigned level = 0U;
            for (inner = (int)index + 1; inner < token->matching; ++inner) {
                const RLayoutToken *candidate = &line->tokens[inner];
                if (r_layout_opens_scope(candidate)) {
                    level += 1U;
                } else if (r_layout_closes_scope(candidate)) {
                    level -= 1U;
                } else if ((candidate->kind == R_LAYOUT_COMMA) && (level == 0U)) {
                    commas += 1U;
                }
            }
            token->parameter_count = commas + 1U;
        }
    }
    return true;
}

static bool r_layout_previous_starts_operand(const RLayoutLine *line, unsigned index) {
    const RLayoutToken *previous;
    if (index == 0U) {
        return true;
    }
    previous = &line->tokens[index - 1U];
    return (previous->kind == R_LAYOUT_LPAREN) || (previous->kind == R_LAYOUT_LSQUARE) ||
           (previous->kind == R_LAYOUT_LBRACE) || (previous->kind == R_LAYOUT_COMMA) ||
           (previous->kind == R_LAYOUT_SEMI) || (previous->kind == R_LAYOUT_OPERATOR) ||
           (previous->kind == R_LAYOUT_QUESTION) || (previous->kind == R_LAYOUT_COLON) ||
           previous->is_return;
}

static bool r_layout_is_keyword(const RLayoutToken *token) {
    return (token->kind == R_LAYOUT_IDENT) &&
           (token->is_return || token->is_type_word || token->is_control ||
            r_layout_text_is(token, "sizeof") || r_layout_text_is(token, "_Alignof") ||
            r_layout_text_is(token, "_Atomic") || r_layout_text_is(token, "_Generic"));
}

static bool r_layout_is_identifier(const RLayoutToken *token) {
    return (token->kind == R_LAYOUT_IDENT) && !r_layout_is_keyword(token);
}

/* A parenthesis opened directly after '_Atomic' declares a type. */
static bool r_layout_is_type_declaration_paren(const RLayoutLine *line, unsigned index) {
    const RLayoutToken *token = &line->tokens[index];
    int open = (token->kind == R_LAYOUT_LPAREN) ? (int)index : token->matching;
    if ((open <= 0) || ((token->kind != R_LAYOUT_LPAREN) && (token->kind != R_LAYOUT_RPAREN))) {
        return false;
    }
    return r_layout_text_is(&line->tokens[open - 1], "_Atomic");
}

/* clang's rParenEndsCast, restricted to the token forms of generated code. */
static bool r_layout_paren_ends_cast(const RLayoutLine *line, unsigned close) {
    const RLayoutToken *rparen = &line->tokens[close];
    int open = rparen->matching;
    const RLayoutToken *before = &line->tokens[close - 1U];
    const RLayoutToken *after;
    const RLayoutToken *left = NULL;
    bool parens_are_type;
    bool parens_could_end_decl;
    int inner;

    if ((open < 0) || (open + 1 == (int)close) || (close + 1U >= line->count)) {
        return false;
    }
    after = &line->tokens[close + 1U];
    if (open > 0) {
        left = &line->tokens[open - 1];
        if ((left->kind == R_LAYOUT_RPAREN) && (left->type != R_LAYOUT_TT_CAST_RPAREN)) {
            if (left->matching <= 0) {
                return false;
            }
            left = &line->tokens[left->matching - 1];
        }
        if ((left->kind == R_LAYOUT_IDENT) && !left->is_return) {
            return false;
        }
        if (left->kind == R_LAYOUT_RSQUARE) {
            return false;
        }
    }
    if ((after->kind == R_LAYOUT_QUESTION) || (after->kind == R_LAYOUT_ARROW) ||
        ((after->kind == R_LAYOUT_OPERATOR) && r_layout_text_is(after, "&&") &&
         !before->is_type_word)) {
        return false;
    }
    if ((after->kind == R_LAYOUT_NUMBER) || r_layout_text_is(after, "sizeof")) {
        return true;
    }
    parens_are_type =
        before->is_type_word || (before->type == R_LAYOUT_TT_POINTER) ||
        ((before->kind == R_LAYOUT_RPAREN) && r_layout_is_type_declaration_paren(line, close - 1U));
    parens_could_end_decl = (after->kind == R_LAYOUT_SEMI) || (after->kind == R_LAYOUT_LBRACE) ||
                            ((after->kind == R_LAYOUT_OPERATOR) && r_layout_text_is(after, "="));
    if (parens_are_type && !parens_could_end_decl) {
        return true;
    }
    if (left == NULL) {
        return false;
    }
    for (inner = open + 1; inner < (int)close; ++inner) {
        if (line->tokens[inner].type == R_LAYOUT_TT_BINARY) {
            return false;
        }
    }
    if (r_layout_is_identifier(after)) {
        return true;
    }
    if (after->kind == R_LAYOUT_LPAREN) {
        inner = (int)close - 1;
        while ((inner > open) && r_layout_is_identifier(&line->tokens[inner])) {
            inner -= 1;
        }
        return inner == open;
    }
    if (close + 2U >= line->count) {
        return false;
    }
    if (after->kind == R_LAYOUT_LBRACE) {
        return true;
    }
    if ((after->kind == R_LAYOUT_OPERATOR) &&
        (r_layout_text_is(after, "-") || r_layout_text_is(after, "!") ||
         r_layout_text_is(after, "~") || r_layout_text_is(after, "++") ||
         r_layout_text_is(after, "--") || r_layout_text_is(after, "&") ||
         r_layout_text_is(after, "*"))) {
        const RLayoutToken *operand = &line->tokens[close + 2U];
        bool amp_or_star = r_layout_text_is(after, "&") || r_layout_text_is(after, "*");
        if (!r_layout_is_identifier(operand) && (operand->kind != R_LAYOUT_NUMBER)) {
            return false;
        }
        if (amp_or_star && (operand->kind == R_LAYOUT_NUMBER)) {
            return false;
        }
        if (before->kind == R_LAYOUT_RPAREN) {
            return false;
        }
        for (inner = open + 1; inner < (int)close; ++inner) {
            if (!r_layout_is_identifier(&line->tokens[inner]) &&
                !r_layout_text_is(&line->tokens[inner], "const")) {
                return false;
            }
        }
        return true;
    }
    return false;
}

/* Token types: casts, unary operators, declarations, subscripts, conditionals. */
static bool r_layout_annotate_types(RLayoutLine *line) {
    unsigned index;
    unsigned decl_end = 0U;
    unsigned decl_start;

    for (index = 0; index < line->count; ++index) {
        RLayoutToken *token = &line->tokens[index];
        if (token->kind == R_LAYOUT_IDENT) {
            token->is_return = r_layout_text_is(token, "return");
            token->is_type_word = r_layout_is_type_word(token);
            token->is_control = r_layout_text_is(token, "if") || r_layout_text_is(token, "else") ||
                                r_layout_text_is(token, "while") ||
                                r_layout_text_is(token, "for") ||
                                r_layout_text_is(token, "switch") || r_layout_text_is(token, "do");
        }
    }
    /* A declaration: qualifiers, the type, pointers, then the name, then '=' or ';'; it
       starts the statement or the header of a for loop. */
    decl_start = 0U;
    if ((line->count >= 3U) && r_layout_text_is(&line->tokens[0], "for") &&
        (line->tokens[1].kind == R_LAYOUT_LPAREN)) {
        decl_start = 2U;
    }
    if ((line->count >= decl_start + 3U) && (line->tokens[decl_start].kind == R_LAYOUT_IDENT) &&
        !line->tokens[decl_start].is_return && !line->tokens[decl_start].is_control) {
        unsigned at = decl_start;
        bool has_type = false;
        while ((at < line->count) && (line->tokens[at].kind == R_LAYOUT_IDENT) &&
               line->tokens[at].is_type_word) {
            at += 1U;
            has_type = true;
        }
        if ((at + 4U < line->count) && (line->tokens[at].kind == R_LAYOUT_IDENT) &&
            r_layout_text_is(&line->tokens[at], "_Atomic") &&
            (line->tokens[at + 1U].kind == R_LAYOUT_LPAREN) &&
            (line->tokens[at + 1U].matching == (int)at + 3)) {
            at += 4U;
            has_type = true;
        } else if ((at + 1U < line->count) && (line->tokens[at].kind == R_LAYOUT_IDENT) &&
                   ((line->tokens[at + 1U].kind == R_LAYOUT_IDENT) ||
                    ((line->tokens[at + 1U].kind == R_LAYOUT_OPERATOR) &&
                     r_layout_text_is(&line->tokens[at + 1U], "*")))) {
            at += 1U;
            has_type = true;
        }
        while (has_type && (at < line->count) && (line->tokens[at].kind == R_LAYOUT_OPERATOR) &&
               r_layout_text_is(&line->tokens[at], "*")) {
            at += 1U;
        }
        if (has_type && (at + 1U < line->count) && (line->tokens[at].kind == R_LAYOUT_IDENT) &&
            ((line->tokens[at + 1U].kind == R_LAYOUT_SEMI) ||
             ((line->tokens[at + 1U].kind == R_LAYOUT_OPERATOR) &&
              r_layout_text_is(&line->tokens[at + 1U], "=")) ||
             (line->tokens[at + 1U].kind == R_LAYOUT_LSQUARE))) {
            unsigned pointer;
            line->tokens[at].type = R_LAYOUT_TT_START_OF_NAME;
            for (pointer = decl_start + 1U; pointer < at; ++pointer) {
                if (line->tokens[pointer].kind == R_LAYOUT_OPERATOR) {
                    line->tokens[pointer].type = R_LAYOUT_TT_POINTER;
                }
            }
            decl_end = at;
        }
    }
    for (index = 0; index < line->count; ++index) {
        RLayoutToken *token = &line->tokens[index];
        if ((token->kind == R_LAYOUT_QUESTION) || (token->kind == R_LAYOUT_COLON)) {
            token->type = R_LAYOUT_TT_CONDITIONAL;
            token->precedence =
                (token->kind == R_LAYOUT_QUESTION) ? R_PREC_CONDITIONAL : R_PREC_UNKNOWN;
        } else if (token->kind == R_LAYOUT_COMMA) {
            token->precedence = R_PREC_COMMA;
        } else if (token->kind == R_LAYOUT_LSQUARE) {
            token->type = R_LAYOUT_TT_SUBSCRIPT;
        } else if ((token->kind == R_LAYOUT_PERIOD) && (index > 0U) &&
                   ((line->tokens[index - 1U].kind == R_LAYOUT_LBRACE) ||
                    (line->tokens[index - 1U].kind == R_LAYOUT_COMMA))) {
            token->type = R_LAYOUT_TT_DESIGNATED;
        } else if ((token->kind == R_LAYOUT_OPERATOR) && (token->type == R_LAYOUT_TT_UNKNOWN)) {
            bool unary_form = r_layout_text_is(token, "&") || r_layout_text_is(token, "*") ||
                              r_layout_text_is(token, "-") || r_layout_text_is(token, "+") ||
                              r_layout_text_is(token, "!") || r_layout_text_is(token, "~") ||
                              r_layout_text_is(token, "++") || r_layout_text_is(token, "--");
            bool starts_operand = r_layout_previous_starts_operand(line, index) &&
                                  ((index == 0U) || (index > decl_end) || (decl_end == 0U));
            if (r_layout_text_is(token, "!") || r_layout_text_is(token, "~")) {
                starts_operand = true;
            }
            if (unary_form && starts_operand) {
                token->type = R_LAYOUT_TT_UNARY;
            } else if (r_layout_text_is(token, "++") || r_layout_text_is(token, "--")) {
                token->type = R_LAYOUT_TT_TRAILING_UNARY;
            } else {
                token->type = R_LAYOUT_TT_BINARY;
                token->precedence = r_layout_operator_precedence(token);
                if (token->precedence == R_PREC_UNKNOWN) {
                    return false;
                }
            }
        }
    }
    /* Pointers: a '*' or '&' directly before ')' or ',' declares a pointer type. */
    for (index = 1; index + 1U < line->count; ++index) {
        RLayoutToken *token = &line->tokens[index];
        if ((token->kind == R_LAYOUT_OPERATOR) &&
            (r_layout_text_is(token, "*") || r_layout_text_is(token, "&")) &&
            ((line->tokens[index + 1U].kind == R_LAYOUT_RPAREN) ||
             (line->tokens[index + 1U].kind == R_LAYOUT_COMMA))) {
            token->type = R_LAYOUT_TT_POINTER;
            token->precedence = R_PREC_UNKNOWN;
        }
    }
    /* Casts, decided left to right as clang does; the operand after a cast is unary. */
    for (index = 1; index < line->count; ++index) {
        RLayoutToken *token = &line->tokens[index];
        if ((token->kind == R_LAYOUT_RPAREN) && r_layout_paren_ends_cast(line, index)) {
            RLayoutToken *after = &line->tokens[index + 1U];
            token->type = R_LAYOUT_TT_CAST_RPAREN;
            if ((after->kind == R_LAYOUT_OPERATOR) && (after->type == R_LAYOUT_TT_BINARY)) {
                after->type = R_LAYOUT_TT_UNARY;
                after->precedence = R_PREC_UNKNOWN;
            }
        }
    }
    /* A braced list ending in a comma breaks after its opening and before its closing brace. */
    for (index = 0; index < line->count; ++index) {
        RLayoutToken *token = &line->tokens[index];
        if ((token->kind == R_LAYOUT_LBRACE) && (token->matching > (int)index + 1) &&
            (line->tokens[token->matching - 1].kind == R_LAYOUT_COMMA)) {
            line->tokens[index + 1U].must_break_before = true;
            line->tokens[token->matching].must_break_before = true;
        }
    }
    /* Subscripts bind ten levels tighter than parentheses. */
    for (index = 0; index < line->count; ++index) {
        RLayoutToken *token = &line->tokens[index];
        if ((token->kind == R_LAYOUT_LSQUARE) && (token->matching > 0)) {
            int inner;
            for (inner = (int)index + 1; inner <= token->matching; ++inner) {
                line->tokens[inner].binding_strength += 9U;
            }
        }
    }
    return true;
}

static unsigned r_layout_spaces_before(const RLayoutLine *line, unsigned index) {
    const RLayoutToken *token = &line->tokens[index];
    const RLayoutToken *previous;
    if (index == 0U) {
        return 0U;
    }
    previous = &line->tokens[index - 1U];
    switch (token->kind) {
    case R_LAYOUT_RPAREN:
    case R_LAYOUT_RSQUARE:
    case R_LAYOUT_RBRACE:
    case R_LAYOUT_COMMA:
    case R_LAYOUT_SEMI:
    case R_LAYOUT_ARROW:
    case R_LAYOUT_LSQUARE:
        return 0U;
    case R_LAYOUT_PERIOD:
        if (token->type != R_LAYOUT_TT_DESIGNATED) {
            return 0U;
        }
        break;
    case R_LAYOUT_LPAREN:
        if ((previous->kind == R_LAYOUT_IDENT) && !previous->is_return && !previous->is_type_word &&
            !previous->is_control) {
            return 0U;
        }
        break;
    case R_LAYOUT_LBRACE:
        if (previous->type == R_LAYOUT_TT_CAST_RPAREN) {
            return 0U;
        }
        return 1U;
    case R_LAYOUT_QUESTION:
    case R_LAYOUT_COLON:
        return 1U;
    case R_LAYOUT_OPERATOR:
        if (token->type == R_LAYOUT_TT_TRAILING_UNARY) {
            return 0U;
        }
        if ((token->type == R_LAYOUT_TT_BINARY) || (token->type == R_LAYOUT_TT_POINTER)) {
            return 1U;
        }
        break;
    default:
        break;
    }
    if ((previous->kind == R_LAYOUT_LPAREN) || (previous->kind == R_LAYOUT_LSQUARE) ||
        (previous->kind == R_LAYOUT_LBRACE) || (previous->kind == R_LAYOUT_PERIOD) ||
        (previous->kind == R_LAYOUT_ARROW) || (previous->type == R_LAYOUT_TT_UNARY) ||
        (previous->type == R_LAYOUT_TT_POINTER) || (previous->type == R_LAYOUT_TT_CAST_RPAREN)) {
        return 0U;
    }
    return 1U;
}

/* --- expression parser (fake parentheses by precedence) -------------------------------- */

typedef struct RLayoutParser {
    RLayoutLine *line;
    int current;
} RLayoutParser;

static void r_layout_parser_next(RLayoutParser *parser) {
    if (parser->current >= 0) {
        parser->current += 1;
        if (parser->current >= (int)parser->line->count) {
            parser->current = -1;
        }
    }
}

static int r_layout_current_precedence(const RLayoutParser *parser) {
    const RLayoutToken *token;
    if (parser->current < 0) {
        return -1;
    }
    token = &parser->line->tokens[parser->current];
    if (r_layout_is_conditional(token)) {
        return R_PREC_CONDITIONAL;
    }
    if (token->kind == R_LAYOUT_SEMI) {
        return 0;
    }
    if ((token->type == R_LAYOUT_TT_BINARY) || (token->kind == R_LAYOUT_COMMA)) {
        return (int)token->precedence;
    }
    if (r_layout_is_member_access(token)) {
        return R_PREC_ARROW_AND_PERIOD;
    }
    return -1;
}

static void r_layout_add_fake_parenthesis(RLayoutParser *parser, int start, unsigned precedence) {
    RLayoutToken *first = &parser->line->tokens[start];
    int end = (parser->current >= 0) ? parser->current - 1 : (int)parser->line->count - 1;
    if (first->fake_lparen_count < R_LAYOUT_MAX_FAKE) {
        first->fake_lparens[first->fake_lparen_count] = (unsigned char)precedence;
        first->fake_lparen_count += 1U;
    }
    if (precedence > R_PREC_UNKNOWN) {
        first->starts_binary_expression = true;
    }
    if (end >= 0) {
        parser->line->tokens[end].fake_rparens += 1U;
        if (precedence > R_PREC_UNKNOWN) {
            parser->line->tokens[end].ends_binary_expression = true;
        }
    }
}

static void r_layout_parse(RLayoutParser *parser, int precedence);

static void r_layout_parse_unary(RLayoutParser *parser) {
    int operators[R_LAYOUT_MAX_FAKE];
    unsigned count = 0U;
    while ((parser->current >= 0) &&
           (parser->line->tokens[parser->current].type == R_LAYOUT_TT_UNARY) &&
           (count < R_LAYOUT_MAX_FAKE)) {
        operators[count] = parser->current;
        count += 1U;
        r_layout_parser_next(parser);
    }
    r_layout_parse(parser, R_PREC_ARROW_AND_PERIOD);
    while (count > 0U) {
        count -= 1U;
        r_layout_add_fake_parenthesis(parser, operators[count], R_PREC_UNKNOWN);
    }
}

static void r_layout_parse_conditional(RLayoutParser *parser) {
    int start = parser->current;
    r_layout_parse(parser, R_PREC_LOGICAL_OR);
    if ((parser->current < 0) ||
        (parser->line->tokens[parser->current].kind != R_LAYOUT_QUESTION)) {
        return;
    }
    r_layout_parser_next(parser);
    r_layout_parse(parser, R_PREC_ASSIGNMENT);
    if ((parser->current < 0) || !r_layout_is_conditional(&parser->line->tokens[parser->current])) {
        return;
    }
    r_layout_parser_next(parser);
    r_layout_parse(parser, R_PREC_ASSIGNMENT);
    r_layout_add_fake_parenthesis(parser, start, R_PREC_CONDITIONAL);
}

static void r_layout_parse(RLayoutParser *parser, int precedence) {
    int start;
    int latest = -1;
    unsigned operator_index = 0U;

    while ((parser->current >= 0) && parser->line->tokens[parser->current].is_return) {
        r_layout_parser_next(parser);
    }
    if ((parser->current < 0) || (precedence > R_PREC_ARROW_AND_PERIOD)) {
        return;
    }
    if (precedence == R_PREC_CONDITIONAL) {
        r_layout_parse_conditional(parser);
        return;
    }
    if (precedence == R_PREC_UNARY) {
        r_layout_parse_unary(parser);
        return;
    }
    start = parser->current;
    while (parser->current >= 0) {
        const RLayoutToken *current;
        int current_precedence;
        r_layout_parse(parser, precedence + 1);
        current_precedence = r_layout_current_precedence(parser);
        if (parser->current < 0) {
            break;
        }
        current = &parser->line->tokens[parser->current];
        if ((r_layout_closes_scope(current) && (current->matching >= 0)) ||
            ((current_precedence != -1) && (current_precedence < precedence)) ||
            ((current_precedence == R_PREC_CONDITIONAL) && (precedence == R_PREC_ASSIGNMENT) &&
             (current->kind == R_LAYOUT_COLON))) {
            break;
        }
        if (r_layout_opens_scope(current)) {
            while ((parser->current >= 0) &&
                   (!r_layout_closes_scope(&parser->line->tokens[parser->current]) ||
                    r_layout_opens_scope(&parser->line->tokens[parser->current]))) {
                r_layout_parser_next(parser);
                r_layout_parse(parser, 0);
            }
            r_layout_parser_next(parser);
        } else {
            if (current_precedence == precedence) {
                if (latest >= 0) {
                    parser->line->tokens[latest].next_operator = parser->current;
                }
                latest = parser->current;
                parser->line->tokens[latest].operator_index = operator_index;
                operator_index += 1U;
            }
            r_layout_parser_next(parser);
        }
    }
    if ((latest >= 0) && ((parser->current >= 0) || (precedence > 0))) {
        r_layout_add_fake_parenthesis(
            parser,
            start,
            (precedence == R_PREC_ARROW_AND_PERIOD) ? R_PREC_UNKNOWN : (unsigned)precedence);
    }
}

/* --- formatting information ------------------------------------------------------------ */

static unsigned r_layout_token_precedence(const RLayoutToken *token) {
    if (token->kind == R_LAYOUT_QUESTION) {
        return R_PREC_CONDITIONAL;
    }
    return token->precedence;
}

static bool r_layout_can_break_before(const RLayoutLine *line, unsigned index) {
    const RLayoutToken *right = &line->tokens[index];
    const RLayoutToken *left = &line->tokens[index - 1U];
    if (right->kind == R_LAYOUT_RBRACE) {
        return false;
    }
    if (((right->kind == R_LAYOUT_LBRACE) && (right->matching < 0)) || right->is_control) {
        return false;
    }
    if (right->kind == R_LAYOUT_RPAREN) {
        return false;
    }
    if (right->type == R_LAYOUT_TT_POINTER) {
        return true;
    }
    if (right->type == R_LAYOUT_TT_START_OF_NAME) {
        return true;
    }
    if (left->type == R_LAYOUT_TT_POINTER) {
        return false;
    }
    if ((left->kind == R_LAYOUT_QUESTION) && (right->kind == R_LAYOUT_COLON)) {
        return false;
    }
    if (r_layout_is_conditional(right)) {
        return true;
    }
    if (r_layout_is_conditional(left)) {
        return false;
    }
    if (left->type == R_LAYOUT_TT_UNARY) {
        return false;
    }
    if ((left->kind == R_LAYOUT_LPAREN) && (index >= 2U) &&
        ((line->tokens[index - 2U].type == R_LAYOUT_TT_BINARY) ||
         (line->tokens[index - 2U].type == R_LAYOUT_TT_CAST_RPAREN) ||
         r_layout_is_type_declaration_paren(line, index - 1U))) {
        return false;
    }
    if (right->type == R_LAYOUT_TT_TRAILING_UNARY) {
        return false;
    }
    if ((left->type == R_LAYOUT_TT_BINARY) && !r_layout_is_shift_left(left)) {
        return true;
    }
    return (left->kind == R_LAYOUT_COMMA) || (left->kind == R_LAYOUT_SEMI) ||
           (left->kind == R_LAYOUT_LBRACE) || r_layout_is_aggregate_keyword(left) ||
           r_layout_is_member_access(right) || r_layout_is_shift_left(right) ||
           (right->kind == R_LAYOUT_LSQUARE) || (right->kind == R_LAYOUT_COLON) ||
           ((left->kind == R_LAYOUT_RPAREN) && (right->kind == R_LAYOUT_IDENT)) ||
           ((left->kind == R_LAYOUT_LPAREN) && (right->kind != R_LAYOUT_RPAREN));
}

static unsigned r_layout_split_penalty(const RLayoutLine *line, unsigned index) {
    const RLayoutToken *right = &line->tokens[index];
    const RLayoutToken *left = &line->tokens[index - 1U];
    unsigned level;
    if (left->kind == R_LAYOUT_SEMI) {
        return 0U;
    }
    if (right->kind == R_LAYOUT_LSQUARE) {
        return (left->kind == R_LAYOUT_RSQUARE) ? 200U : 500U;
    }
    if (right->type == R_LAYOUT_TT_START_OF_NAME) {
        return (left->type == R_LAYOUT_TT_START_OF_NAME) ? 110U : 200U;
    }
    if (right->type == R_LAYOUT_TT_POINTER) {
        return 190U;
    }
    if ((left->type == R_LAYOUT_TT_BINARY) && r_layout_text_is(left, "=") &&
        (right->kind == R_LAYOUT_LBRACE)) {
        return 160U;
    }
    if (left->type == R_LAYOUT_TT_CAST_RPAREN) {
        return 100U;
    }
    if (r_layout_is_aggregate_keyword(left)) {
        return R_LAYOUT_TYPE_KEYWORD_PENALTY;
    }
    if (r_layout_is_member_access(right)) {
        if ((right->next_operator > 0) &&
            r_layout_closes_scope(&line->tokens[right->next_operator - 1])) {
            return 35U;
        }
        return R_LAYOUT_MEMBER_ACCESS_PENALTY;
    }
    if (right->kind == R_LAYOUT_RBRACE) {
        return 1U;
    }
    if ((left->kind == R_LAYOUT_LPAREN) && (index >= 2U) &&
        (r_layout_text_is(&line->tokens[index - 2U], "if") ||
         r_layout_text_is(&line->tokens[index - 2U], "for"))) {
        return R_LAYOUT_CONDITION_PAREN_PENALTY;
    }
    if (r_layout_opens_scope(left)) {
        return R_LAYOUT_OPEN_BRACKET_PENALTY;
    }
    if (left->type == R_LAYOUT_TT_UNARY) {
        return 60U;
    }
    if (left->kind == R_LAYOUT_COMMA) {
        return 1U;
    }
    if (r_layout_is_shift_left(right)) {
        return ((left->kind != R_LAYOUT_RPAREN) || (right->operator_index > 0U)) ? 2U : 1U;
    }
    if (r_layout_is_conditional(left)) {
        return R_PREC_CONDITIONAL;
    }
    level = r_layout_token_precedence(left);
    if (level == R_PREC_UNKNOWN) {
        level = r_layout_token_precedence(right);
    }
    if (level == R_PREC_ASSIGNMENT) {
        return R_PREC_ASSIGNMENT;
    }
    if (level != R_PREC_UNKNOWN) {
        return level;
    }
    return 3U;
}

static bool r_layout_annotate(RLayoutLine *line) {
    RLayoutParser parser;
    unsigned index;

    if (!r_layout_annotate_scopes(line) || !r_layout_annotate_types(line)) {
        return false;
    }
    for (index = 0; index < line->count; ++index) {
        line->tokens[index].spaces_before = r_layout_spaces_before(line, index);
    }
    parser.line = line;
    parser.current = 0;
    r_layout_parse(&parser, 0);
    line->tokens[0].total_length = line->tokens[0].length;
    for (index = 1; index < line->count; ++index) {
        RLayoutToken *token = &line->tokens[index];
        token->can_break_before =
            token->must_break_before || r_layout_can_break_before(line, index);
        token->total_length = line->tokens[index - 1U].total_length +
                              (token->must_break_before ? R_LAYOUT_COLUMN_LIMIT
                                                        : token->length + token->spaces_before);
        token->split_penalty = r_layout_split_penalty(line, index) +
                               R_LAYOUT_STRENGTH_PENALTY * token->binding_strength;
    }
    return true;
}

/* --- the continuation indenter --------------------------------------------------------- */

static uint16_t r_layout_column_value(unsigned value) {
    return (uint16_t)(value > UINT16_MAX ? UINT16_MAX : value);
}

static uint8_t r_layout_level_value(unsigned value) {
    return (uint8_t)(value > UINT8_MAX ? UINT8_MAX : value);
}

static unsigned r_layout_max(unsigned left, unsigned right) {
    return left > right ? left : right;
}

static unsigned r_layout_min(unsigned left, unsigned right) {
    return left < right ? left : right;
}

static RLayoutParen *r_layout_top(RLayoutState *state) {
    return &state->stack[state->stack_count - 1U];
}

static const RLayoutParen *r_layout_top_const(const RLayoutState *state) {
    return &state->stack[state->stack_count - 1U];
}

static bool r_layout_starts_next_parameter(const RLayoutLine *line, unsigned index) {
    return (index > 0U) && (line->tokens[index - 1U].kind == R_LAYOUT_COMMA);
}

static unsigned r_layout_length_to_next_operator(const RLayoutLine *line, unsigned index) {
    const RLayoutToken *token = &line->tokens[index];
    if (token->next_operator < 0) {
        return 0U;
    }
    return line->tokens[token->next_operator].total_length - token->total_length;
}

static unsigned r_layout_new_line_column(const RLayoutLine *line, const RLayoutState *state) {
    const RLayoutToken *current = &line->tokens[state->next];
    const RLayoutToken *previous = &line->tokens[state->next - 1];
    const RLayoutParen *top = r_layout_top_const(state);
    unsigned continuation = r_layout_max(top->last_space, top->indent) + R_LAYOUT_CONTINUATION;

    if (((current->kind == R_LAYOUT_RBRACE) || (current->kind == R_LAYOUT_RSQUARE)) &&
        (state->stack_count > 1U)) {
        if ((current->kind == R_LAYOUT_RBRACE) && (current->matching >= 0)) {
            return state->stack[state->stack_count - 2U].last_space;
        }
        return state->first_indent;
    }
    if ((current->kind == R_LAYOUT_RPAREN) && (state->stack_count > 1U) &&
        ((state->next + 1 >= (int)line->count) ||
         (line->tokens[state->next + 1].kind == R_LAYOUT_SEMI) ||
         (line->tokens[state->next + 1].kind == R_LAYOUT_LBRACE))) {
        return state->stack[state->stack_count - 2U].last_space;
    }
    if (r_layout_is_shift_left(current) && (top->first_less_less != 0U)) {
        return top->first_less_less;
    }
    if (r_layout_is_member_access(current)) {
        return (top->call_continuation == 0U) ? continuation : top->call_continuation;
    }
    if ((top->question_column != 0U) &&
        (((current->kind == R_LAYOUT_COLON) && r_layout_is_conditional(current)) ||
         r_layout_is_conditional(previous))) {
        bool chained = false;
        if ((current->kind == R_LAYOUT_COLON) && (state->next + 1 < (int)line->count)) {
            const RLayoutToken *after = &line->tokens[state->next + 1];
            chained = (after->fake_lparen_count != 0U) &&
                      (after->fake_lparens[after->fake_lparen_count - 1U] == R_PREC_CONDITIONAL);
        }
        if ((previous->kind == R_LAYOUT_COLON) && (current->fake_lparen_count != 0U) &&
            (current->fake_lparens[current->fake_lparen_count - 1U] == R_PREC_CONDITIONAL)) {
            chained = true;
        }
        if (chained && !top->is_wrapped_conditional) {
            unsigned indent = top->indent;
            indent -= R_LAYOUT_CONTINUATION;
            return indent;
        }
        return top->question_column;
    }
    if ((previous->kind == R_LAYOUT_COMMA) && (top->variable_pos != 0U)) {
        return top->variable_pos;
    }
    if ((current->type == R_LAYOUT_TT_START_OF_NAME) || (current->type == R_LAYOUT_TT_POINTER) ||
        ((previous->type == R_LAYOUT_TT_BINARY) && r_layout_text_is(previous, "="))) {
        return continuation;
    }
    if (current->kind == R_LAYOUT_LSQUARE) {
        return (top->start_of_array_subscripts != 0U) ? top->start_of_array_subscripts
                                                      : continuation;
    }
    if ((previous->kind == R_LAYOUT_RPAREN) && !r_layout_is_binary_operator(current) &&
        (current->kind != R_LAYOUT_COLON)) {
        return continuation;
    }
    if (r_layout_is_binary_operator(current) && top->unindent_operator) {
        return top->indent - current->length - current->spaces_before;
    }
    if ((top->indent == state->first_indent) && (previous->kind != R_LAYOUT_RBRACE)) {
        return top->indent + R_LAYOUT_CONTINUATION;
    }
    return top->indent;
}

static bool r_layout_can_break(const RLayoutLine *line, const RLayoutState *state) {
    const RLayoutToken *current = &line->tokens[state->next];
    const RLayoutToken *previous = &line->tokens[state->next - 1];
    const RLayoutParen *top = r_layout_top_const(state);
    if (!current->can_break_before) {
        return false;
    }
    if (!current->must_break_before && (previous->kind == R_LAYOUT_LBRACE) && (state->next >= 2) &&
        ((line->tokens[state->next - 2].kind == R_LAYOUT_LBRACE) ||
         (line->tokens[state->next - 2].kind == R_LAYOUT_LPAREN) ||
         (line->tokens[state->next - 2].kind == R_LAYOUT_COMMA))) {
        return false;
    }
    if (r_layout_opens_scope(previous) && (previous->kind != R_LAYOUT_LBRACE) &&
        (state->lowest_level_on_line < state->start_of_line_level) &&
        (state->lowest_level_on_line < current->nesting)) {
        return false;
    }
    if (r_layout_is_member_access(current) && top->contains_unwrapped_builder) {
        return false;
    }
    if ((current->type != R_LAYOUT_TT_BINARY) && (current->kind != R_LAYOUT_COMMA) &&
        top->no_line_break_in_operand) {
        return false;
    }
    return !state->no_line_break && !top->no_line_break;
}

static bool r_layout_must_break(const RLayoutLine *line, const RLayoutState *state) {
    const RLayoutToken *current = &line->tokens[state->next];
    const RLayoutToken *previous = &line->tokens[state->next - 1];
    const RLayoutParen *top = r_layout_top_const(state);
    unsigned new_line_column;

    if (current->must_break_before) {
        return true;
    }
    if ((r_layout_starts_next_parameter(line, (unsigned)state->next) ||
         (previous->kind == R_LAYOUT_SEMI) ||
         (r_layout_is_conditional(current) && (previous->kind != R_LAYOUT_QUESTION))) &&
        top->break_before_parameter && (current->kind != R_LAYOUT_RPAREN) &&
        (current->kind != R_LAYOUT_RBRACE)) {
        return true;
    }
    if (top->is_chained_conditional && r_layout_is_conditional(current) &&
        (current->kind == R_LAYOUT_COLON)) {
        return true;
    }
    if (r_layout_is_shift_left(current) && top->break_before_parameter &&
        (top->first_less_less == 0U)) {
        return true;
    }
    new_line_column = r_layout_new_line_column(line, state);
    if (r_layout_is_member_access(current) &&
        (state->column + r_layout_length_to_next_operator(line, (unsigned)state->next) >
         R_LAYOUT_COLUMN_LIMIT) &&
        ((state->column > new_line_column) || (current->nesting < state->start_of_line_level))) {
        return true;
    }
    if (r_layout_is_member_access(current) && r_layout_closes_scope(previous) &&
        ((top->call_continuation != 0U) || top->break_before_parameter)) {
        return true;
    }
    if (state->column <= new_line_column) {
        return false;
    }
    if ((previous->type == R_LAYOUT_TT_BINARY) && current->can_break_before) {
        unsigned previous_precedence = r_layout_token_precedence(previous);
        if ((previous_precedence != R_PREC_ASSIGNMENT) && top->break_before_parameter) {
            bool lhs_is_binary =
                (state->next >= 2) && line->tokens[state->next - 2].ends_binary_expression;
            bool is_comparison = ((previous_precedence == R_PREC_RELATIONAL) ||
                                  (previous_precedence == R_PREC_EQUALITY)) &&
                                 (state->next >= 2) &&
                                 (line->tokens[state->next - 2].type != R_LAYOUT_TT_BINARY);
            if (lhs_is_binary || !is_comparison) {
                return true;
            }
        }
    }
    return false;
}

static void r_layout_add_token_on_current_line(const RLayoutLine *line, RLayoutState *state) {
    RLayoutToken *current = &line->tokens[state->next];
    const RLayoutToken *previous = &line->tokens[state->next - 1];
    RLayoutParen *top = r_layout_top(state);
    unsigned spaces = current->spaces_before;

    if ((current->type == R_LAYOUT_TT_BINARY) && r_layout_text_is(current, "=") &&
        (current->nesting == 0U) && (top->variable_pos == 0U)) {
        int back = state->next - 1;
        top->variable_pos = state->column;
        while ((back >= 0) && (top->variable_pos >= line->tokens[back].length)) {
            top->variable_pos -= line->tokens[back].length;
            if (line->tokens[back].spaces_before != 0U) {
                break;
            }
            back -= 1;
        }
    }
    if (r_layout_opens_scope(previous)) {
        top->indent = r_layout_column_value(state->column + spaces);
        top->is_aligned = true;
    }
    if (top->avoid_bin_packing && r_layout_starts_next_parameter(line, (unsigned)state->next)) {
        top->no_line_break = true;
    }
    if (r_layout_is_member_access(current) && r_layout_closes_scope(previous) &&
        (state->column > r_layout_new_line_column(line, state))) {
        top->contains_unwrapped_builder = true;
    }
    if (r_layout_is_member_access(current) && (previous->kind == R_LAYOUT_RPAREN) &&
        (previous->matching >= 0) &&
        (previous->total_length - line->tokens[previous->matching].total_length > 10U)) {
        top->no_line_break = true;
    }
    if ((previous->type == R_LAYOUT_TT_BINARY) || (previous->kind == R_LAYOUT_COMMA) ||
        (r_layout_is_conditional(previous) && (previous->kind == R_LAYOUT_COLON))) {
        unsigned precedence = r_layout_token_precedence(previous);
        if ((precedence != R_PREC_ASSIGNMENT) && (precedence != R_PREC_RELATIONAL)) {
            bool break_before_operator =
                r_layout_is_conditional(previous) || r_layout_is_shift_left(previous);
            bool two_operands = (previous->operator_index == 0U) && (previous->next_operator < 0) &&
                                !r_layout_is_conditional(previous);
            if ((!break_before_operator && !two_operands) ||
                (!top->last_operator_wrapped && break_before_operator)) {
                top->no_line_break_in_operand = true;
            }
        }
    }
    state->column += spaces;
    if ((previous->kind == R_LAYOUT_LPAREN) && (state->next >= 2) &&
        (r_layout_text_is(&line->tokens[state->next - 2], "if") ||
         r_layout_text_is(&line->tokens[state->next - 2], "for"))) {
        top->last_space = state->column;
        top->nested_block_indent = state->column;
    } else if (previous->kind == R_LAYOUT_COMMA) {
        top->last_space = state->column;
    } else if (r_layout_is_conditional(previous)) {
        top->last_space = state->column;
    } else if ((previous->type == R_LAYOUT_TT_BINARY) &&
               (((r_layout_token_precedence(previous) != R_PREC_ASSIGNMENT) &&
                 (!r_layout_is_shift_left(previous) || (previous->operator_index != 0U) ||
                  (previous->next_operator >= 0))) ||
                current->starts_binary_expression)) {
        top->last_space = state->column;
    } else if (r_layout_opens_scope(previous) && (previous->matching >= 0)) {
        int after = previous->matching + 1;
        if ((after < (int)line->count) && r_layout_is_member_access(&line->tokens[after]) &&
            (state->stack_count > 1U) &&
            (state->stack[state->stack_count - 2U].call_continuation == 0U)) {
            top->last_space = state->column;
        }
    }
}

static unsigned r_layout_add_token_on_new_line(const RLayoutLine *line, RLayoutState *state) {
    const RLayoutToken *current = &line->tokens[state->next];
    const RLayoutToken *previous = &line->tokens[state->next - 1];
    RLayoutParen *top = r_layout_top(state);
    unsigned penalty = 0U;
    unsigned index;

    if (!top->contains_line_break) {
        penalty += R_LAYOUT_FIRST_BREAK_PENALTY;
    }
    top->contains_line_break = true;
    penalty += current->split_penalty;
    if (r_layout_is_shift_left(current) && (top->first_less_less == 0U) &&
        ((state->column <= R_LAYOUT_COLUMN_LIMIT / 3U) || top->break_before_parameter)) {
        penalty += R_LAYOUT_FIRST_SHIFT_PENALTY;
    }
    state->column = r_layout_column_value(r_layout_new_line_column(line, state));
    top->nested_block_indent = state->column;
    if (r_layout_is_member_access(current)) {
        if (top->call_continuation == 0U) {
            top->call_continuation = state->column;
        }
    }
    if (((previous->kind == R_LAYOUT_COMMA) || (previous->kind == R_LAYOUT_SEMI)) &&
        !top->avoid_bin_packing) {
        top->break_before_parameter = false;
    }
    if (previous->type == R_LAYOUT_TT_BINARY) {
        top->break_before_parameter = false;
    }
    if ((current->kind == R_LAYOUT_QUESTION) || (previous->kind == R_LAYOUT_QUESTION)) {
        top->break_before_parameter = true;
    }
    top->last_space = state->column;
    if (r_layout_is_shift_left(current)) {
        top->last_space = r_layout_column_value(top->last_space + 3U);
    }
    state->start_of_line_level = r_layout_level_value(current->nesting);
    state->lowest_level_on_line = r_layout_level_value(current->nesting);
    for (index = 0; index + 1U < state->stack_count; ++index) {
        state->stack[index].break_before_parameter = true;
    }
    if ((previous->kind != R_LAYOUT_COMMA) && (previous->kind != R_LAYOUT_COLON) &&
        (previous->kind != R_LAYOUT_SEMI) && (previous->type != R_LAYOUT_TT_BINARY) &&
        (current->type != R_LAYOUT_TT_BINARY) && !r_layout_opens_scope(previous)) {
        top->break_before_parameter = true;
    }
    if (previous->kind == R_LAYOUT_LBRACE) {
        top->break_before_closing_brace = true;
    }
    if (top->avoid_bin_packing) {
        if ((previous->kind != R_LAYOUT_LPAREN) && (previous->kind != R_LAYOUT_LBRACE) &&
            (previous->type != R_LAYOUT_TT_BINARY)) {
            top->break_before_parameter = true;
        }
    }
    return penalty;
}

static void r_layout_push(RLayoutState *state, const RLayoutParen *paren) {
    state->stack[state->stack_count] = *paren;
    state->stack_count += 1U;
}

static void
r_layout_move_past_fake_lparens(const RLayoutLine *line, RLayoutState *state, bool newline) {
    const RLayoutToken *current = &line->tokens[state->next];
    const RLayoutToken *previous = (state->next > 0) ? &line->tokens[state->next - 1] : NULL;
    bool skip_first_extra_indent;
    unsigned index;

    if (current->fake_lparen_count == 0U) {
        return;
    }
    skip_first_extra_indent =
        (previous != NULL) &&
        (r_layout_opens_scope(previous) || (previous->kind == R_LAYOUT_SEMI) ||
         previous->is_return || (r_layout_token_precedence(previous) == R_PREC_ASSIGNMENT));
    for (index = current->fake_lparen_count; index > 0U; --index) {
        unsigned precedence = current->fake_lparens[index - 1U];
        const RLayoutParen *top = r_layout_top_const(state);
        RLayoutParen paren = *top;
        if (state->stack_count >= R_LAYOUT_MAX_STACK) {
            return;
        }
        paren.tok = -1;
        paren.contains_line_break = false;
        paren.last_operator_wrapped = true;
        paren.is_chained_conditional = false;
        paren.is_wrapped_conditional = false;
        paren.unindent_operator = false;
        paren.no_line_break = paren.no_line_break || top->no_line_break_in_operand;
        if (precedence > R_PREC_COMMA) {
            paren.avoid_bin_packing = false;
        }
        if ((previous == NULL) || !previous->is_return || (precedence > 0U)) {
            paren.indent = r_layout_column_value(
                r_layout_max(r_layout_max(state->column, paren.indent), top->last_space));
        }
        if (precedence > R_PREC_UNKNOWN) {
            paren.last_space = r_layout_column_value(r_layout_max(paren.last_space, state->column));
        }
        if ((precedence != R_PREC_CONDITIONAL) && (current->type != R_LAYOUT_TT_UNARY)) {
            paren.start_of_function_call = state->column;
        }
        if ((precedence == R_PREC_CONDITIONAL) && (previous != NULL) &&
            (previous->kind == R_LAYOUT_COLON) && r_layout_is_conditional(previous) &&
            (index == 1U) && !top->is_wrapped_conditional) {
            paren.is_chained_conditional = true;
            paren.unindent_operator = top->unindent_operator;
        } else if ((precedence == R_PREC_CONDITIONAL) ||
                   (!skip_first_extra_indent && (precedence > R_PREC_ASSIGNMENT))) {
            paren.indent += R_LAYOUT_CONTINUATION;
        }
        if (((previous != NULL) && !r_layout_opens_scope(previous)) ||
            (precedence != R_PREC_COMMA)) {
            paren.break_before_parameter = false;
        }
        r_layout_push(state, &paren);
        skip_first_extra_indent = false;
    }
    (void)newline;
}

static void r_layout_move_past_fake_rparens(const RLayoutLine *line, RLayoutState *state) {
    const RLayoutToken *current = &line->tokens[state->next];
    unsigned index;
    for (index = 0; index < current->fake_rparens; ++index) {
        unsigned variable_pos = r_layout_top_const(state)->variable_pos;
        if (state->stack_count == 1U) {
            break;
        }
        state->stack_count -= 1U;
        r_layout_top(state)->variable_pos = r_layout_column_value(variable_pos);
    }
}

static void r_layout_move_past_scope_opener(const RLayoutLine *line, RLayoutState *state) {
    const RLayoutToken *current = &line->tokens[state->next];
    const RLayoutParen *top = r_layout_top_const(state);
    RLayoutParen paren;
    unsigned nested_block_indent;
    bool ends_in_comma = false;

    if (!r_layout_opens_scope(current) || (state->stack_count >= R_LAYOUT_MAX_STACK)) {
        return;
    }
    if ((current->matching > 0) && (line->tokens[current->matching - 1].kind == R_LAYOUT_COMMA)) {
        ends_in_comma = true;
    }
    (void)memset(&paren, 0, sizeof(paren));
    paren.tok = state->next;
    paren.last_space = top->last_space;
    paren.last_operator_wrapped = true;
    nested_block_indent = r_layout_max(top->start_of_function_call, top->nested_block_indent);
    if (current->kind == R_LAYOUT_LBRACE) {
        paren.indent = top->last_space + R_LAYOUT_CONTINUATION;
        paren.avoid_bin_packing = true;
        paren.break_before_parameter = ends_in_comma;
        if (current->parameter_count > 1U) {
            nested_block_indent = r_layout_max(nested_block_indent, state->column + 1U);
        }
    } else {
        paren.indent = r_layout_column_value(
            r_layout_max(top->last_space, top->start_of_function_call) + R_LAYOUT_CONTINUATION);
        paren.avoid_bin_packing = true;
    }
    paren.no_line_break = top->no_line_break || top->no_line_break_in_operand;
    paren.nested_block_indent = r_layout_column_value(nested_block_indent);
    r_layout_push(state, &paren);
}

static void r_layout_move_past_scope_closer(const RLayoutLine *line, RLayoutState *state) {
    const RLayoutToken *current = &line->tokens[state->next];
    if (!r_layout_closes_scope(current)) {
        return;
    }
    if ((state->stack_count > 1U) &&
        ((current->kind == R_LAYOUT_RPAREN) || (current->kind == R_LAYOUT_RSQUARE) ||
         ((current->kind == R_LAYOUT_RBRACE) && (state->next != 0)))) {
        state->stack_count -= 1U;
    }
    if (current->kind == R_LAYOUT_RSQUARE) {
        if ((state->next + 1 >= (int)line->count) ||
            (line->tokens[state->next + 1].kind != R_LAYOUT_LSQUARE)) {
            r_layout_top(state)->start_of_array_subscripts = 0U;
        }
    }
}

static unsigned
r_layout_move_to_next_token(const RLayoutLine *line, RLayoutState *state, bool newline) {
    const RLayoutToken *current = &line->tokens[state->next];
    const RLayoutToken *previous = (state->next > 0) ? &line->tokens[state->next - 1] : NULL;
    RLayoutParen *top = r_layout_top(state);
    unsigned penalty = 0U;

    if ((current->kind == R_LAYOUT_COMMA) || (current->type == R_LAYOUT_TT_BINARY)) {
        top->no_line_break_in_operand = false;
    }
    if (r_layout_is_shift_left(current)) {
        if (top->first_less_less == 0U) {
            top->first_less_less = state->column;
        } else {
            top->last_operator_wrapped = newline;
        }
    } else if (current->type == R_LAYOUT_TT_BINARY) {
        top->last_operator_wrapped = newline;
    }
    if (r_layout_is_conditional(current) && (previous != NULL) &&
        !r_layout_is_conditional(previous)) {
        top->last_operator_wrapped = newline;
    }
    if ((current->kind == R_LAYOUT_LSQUARE) && (top->start_of_array_subscripts == 0U)) {
        top->start_of_array_subscripts = state->column;
    }
    if (current->kind == R_LAYOUT_QUESTION) {
        top->question_column = state->column;
    }
    if (!r_layout_opens_scope(current) && !r_layout_closes_scope(current) &&
        (current->type != R_LAYOUT_TT_POINTER)) {
        state->lowest_level_on_line =
            r_layout_level_value(r_layout_min(state->lowest_level_on_line, current->nesting));
    }
    if (r_layout_is_member_access(current)) {
        top->start_of_function_call = (current->next_operator < 0) ? 0U : state->column;
    }
    if ((current->type == R_LAYOUT_TT_BINARY || r_layout_is_conditional(current)) && newline) {
        top->nested_block_indent = r_layout_column_value(state->column + current->length + 1U);
    }
    if ((previous != NULL) && (previous->kind == R_LAYOUT_LBRACE) && (state->stack_count > 1U)) {
        if (state->stack[state->stack_count - 2U].nested_block_inlined && newline) {
            unsigned index;
            for (index = 0; index + 1U < state->stack_count; ++index) {
                state->stack[index].no_line_break = true;
            }
        }
        state->stack[state->stack_count - 2U].nested_block_inlined = false;
    }
    if ((previous != NULL) &&
        ((previous->type == R_LAYOUT_TT_BINARY) || r_layout_is_conditional(previous) ||
         (previous->kind == R_LAYOUT_LPAREN) || (previous->kind == R_LAYOUT_COMMA) ||
         (previous->kind == R_LAYOUT_COLON))) {
        bool inlined = (previous->kind != R_LAYOUT_LPAREN) || (previous->parameter_count > 1U);
        top->nested_block_inlined = !newline && inlined;
    }
    r_layout_move_past_fake_lparens(line, state, newline);
    r_layout_move_past_scope_closer(line, state);
    r_layout_move_past_scope_opener(line, state);
    r_layout_move_past_fake_rparens(line, state);
    state->column += current->length;
    state->next += 1;
    if (state->next >= (int)line->count) {
        state->next = -1;
    }
    if (state->column > R_LAYOUT_COLUMN_LIMIT) {
        penalty += R_LAYOUT_EXCESS_PENALTY * (state->column - R_LAYOUT_COLUMN_LIMIT);
    }
    return penalty;
}

static unsigned r_layout_add_token(const RLayoutLine *line, RLayoutState *state, bool newline) {
    unsigned penalty = 0U;
    if (newline) {
        penalty = r_layout_add_token_on_new_line(line, state);
    } else {
        r_layout_add_token_on_current_line(line, state);
    }
    return r_layout_move_to_next_token(line, state, newline) + penalty;
}

static void r_layout_initial_state(const RLayoutLine *line, unsigned indent, RLayoutState *state) {
    RLayoutParen paren;
    (void)memset(state, 0, sizeof(*state));
    (void)memset(&paren, 0, sizeof(paren));
    paren.tok = -1;
    paren.indent = r_layout_column_value(indent);
    paren.last_space = r_layout_column_value(indent);
    paren.nested_block_indent = r_layout_column_value(indent);
    paren.last_operator_wrapped = true;
    state->column = r_layout_column_value(indent);
    state->first_indent = r_layout_column_value(indent);
    state->next = 0;
    r_layout_push(state, &paren);
    (void)r_layout_move_to_next_token(line, state, false);
}

/* --- the optimizing search ------------------------------------------------------------- */

typedef struct RLayoutSearch {
    const RLayoutRequest *request;
    const RLayoutLine *line;
    RLayoutNode *nodes;
    unsigned node_count;
    unsigned node_capacity;
    RLayoutHeapItem *heap;
    unsigned heap_count;
    unsigned heap_capacity;
    RLayoutHash seen;
    unsigned order;
} RLayoutSearch;

static size_t r_layout_state_size(const RLayoutState *state) {
    return offsetof(RLayoutState, stack) + sizeof(RLayoutParen) * (size_t)state->stack_count;
}

static bool r_layout_states_equal(const RLayoutState *left, const RLayoutState *right) {
    return (left->stack_count == right->stack_count) &&
           (memcmp(left, right, r_layout_state_size(left)) == 0);
}

static uint64_t r_layout_hash_state(const RLayoutState *state) {
    const unsigned char *bytes = (const unsigned char *)state;
    size_t size = r_layout_state_size(state);
    uint64_t hash = UINT64_C(1469598103934665603);
    size_t index;
    for (index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static void *
r_layout_grow(const RLayoutRequest *request, void *block, size_t old_size, size_t new_size) {
    void *replacement = request->allocate(request->context, new_size);
    if (replacement == NULL) {
        return NULL;
    }
    if (block != NULL) {
        (void)memcpy(replacement, block, old_size);
        request->release(request->context, block);
    }
    return replacement;
}

static bool r_layout_reserve_node(RLayoutSearch *search) {
    unsigned capacity;
    RLayoutNode *replacement;
    if (search->node_count < search->node_capacity) {
        return true;
    }
    if (search->node_capacity >= R_LAYOUT_MAX_STATES) {
        return false;
    }
    capacity = (search->node_capacity == 0U) ? R_LAYOUT_INITIAL_STATES : search->node_capacity * 2U;
    replacement = r_layout_grow(search->request,
                                search->nodes,
                                sizeof(RLayoutNode) * search->node_count,
                                sizeof(RLayoutNode) * capacity);
    if (replacement == NULL) {
        return false;
    }
    search->nodes = replacement;
    search->node_capacity = capacity;
    return true;
}

static bool r_layout_heap_push(RLayoutSearch *search, unsigned penalty, unsigned order, int node) {
    unsigned index;
    if (search->heap_count >= search->heap_capacity) {
        unsigned capacity =
            (search->heap_capacity == 0U) ? R_LAYOUT_INITIAL_STATES : search->heap_capacity * 2U;
        RLayoutHeapItem *replacement = r_layout_grow(search->request,
                                                     search->heap,
                                                     sizeof(RLayoutHeapItem) * search->heap_count,
                                                     sizeof(RLayoutHeapItem) * capacity);
        if (replacement == NULL) {
            return false;
        }
        search->heap = replacement;
        search->heap_capacity = capacity;
    }
    index = search->heap_count;
    search->heap_count += 1U;
    while (index > 0U) {
        unsigned parent = (index - 1U) / 2U;
        const RLayoutHeapItem *above = &search->heap[parent];
        if ((above->penalty < penalty) || ((above->penalty == penalty) && (above->order < order))) {
            break;
        }
        search->heap[index] = *above;
        index = parent;
    }
    search->heap[index].penalty = penalty;
    search->heap[index].order = order;
    search->heap[index].node = node;
    return true;
}

static RLayoutHeapItem r_layout_heap_pop(RLayoutSearch *search) {
    RLayoutHeapItem top = search->heap[0];
    RLayoutHeapItem last;
    unsigned index = 0U;
    search->heap_count -= 1U;
    last = search->heap[search->heap_count];
    while (true) {
        unsigned left = index * 2U + 1U;
        unsigned right = left + 1U;
        unsigned smallest = left;
        if (left >= search->heap_count) {
            break;
        }
        if ((right < search->heap_count) &&
            ((search->heap[right].penalty < search->heap[left].penalty) ||
             ((search->heap[right].penalty == search->heap[left].penalty) &&
              (search->heap[right].order < search->heap[left].order)))) {
            smallest = right;
        }
        if ((search->heap[smallest].penalty > last.penalty) ||
            ((search->heap[smallest].penalty == last.penalty) &&
             (search->heap[smallest].order > last.order))) {
            break;
        }
        search->heap[index] = search->heap[smallest];
        index = smallest;
    }
    search->heap[index] = last;
    return top;
}

/* The seen set: open addressing over node indices, keyed by the node's state. Every state
   is expanded once, in order of penalty. Returns false when the state was seen already or
   the table could not grow. */
static bool r_layout_seen_insert(RLayoutSearch *search, int node, bool *inserted) {
    RLayoutHash *table = &search->seen;
    const RLayoutState *state = &search->nodes[node].state;
    uint64_t hash;
    unsigned mask;
    unsigned index;

    *inserted = false;
    if ((table->count + 1U) * 2U > table->capacity) {
        unsigned capacity =
            (table->capacity == 0U) ? R_LAYOUT_INITIAL_STATES * 2U : table->capacity * 2U;
        int *slots = search->request->allocate(search->request->context, sizeof(int) * capacity);
        unsigned old;
        if (slots == NULL) {
            return false;
        }
        for (old = 0; old < capacity; ++old) {
            slots[old] = -1;
        }
        for (old = 0; old < table->capacity; ++old) {
            int entry = table->slots[old];
            if (entry >= 0) {
                unsigned probe = (unsigned)(r_layout_hash_state(&search->nodes[entry].state) &
                                            (uint64_t)(capacity - 1U));
                while (slots[probe] >= 0) {
                    probe = (probe + 1U) & (capacity - 1U);
                }
                slots[probe] = entry;
            }
        }
        search->request->release(search->request->context, table->slots);
        table->slots = slots;
        table->capacity = capacity;
    }
    mask = table->capacity - 1U;
    hash = r_layout_hash_state(state);
    index = (unsigned)(hash & (uint64_t)mask);
    while (table->slots[index] >= 0) {
        if (r_layout_states_equal(&search->nodes[table->slots[index]].state, state)) {
            return true;
        }
        index = (index + 1U) & mask;
    }
    table->slots[index] = node;
    table->count += 1U;
    *inserted = true;
    return true;
}

static bool
r_layout_add_next_state(RLayoutSearch *search, unsigned penalty, int previous, bool newline) {
    const RLayoutNode *from = &search->nodes[previous];
    RLayoutNode *node;
    if (newline && !r_layout_can_break(search->line, &from->state)) {
        return true;
    }
    if (!newline && r_layout_must_break(search->line, &from->state)) {
        return true;
    }
    if (!r_layout_reserve_node(search)) {
        return false;
    }
    from = &search->nodes[previous];
    node = &search->nodes[search->node_count];
    node->state = from->state;
    node->previous = previous;
    node->newline = newline;
    node->order = search->order;
    penalty += r_layout_add_token(search->line, &node->state, newline);
    node->penalty = penalty;
    if (!r_layout_heap_push(search, penalty, search->order, (int)search->node_count)) {
        return false;
    }
    search->order += 1U;
    search->node_count += 1U;
    return true;
}

static RLayoutStatus r_layout_solve(RLayoutSearch *search, unsigned indent, int *best) {
    RLayoutNode *root;
    *best = -1;
    if (!r_layout_reserve_node(search)) {
        return R_LAYOUT_NO_MEMORY;
    }
    root = &search->nodes[0];
    (void)memset(root, 0, sizeof(*root));
    root->previous = -1;
    r_layout_initial_state(search->line, indent, &root->state);
    search->node_count = 1U;
    search->order = 1U;
    if (!r_layout_heap_push(search, 0U, 0U, 0)) {
        return R_LAYOUT_NO_MEMORY;
    }
    while (search->heap_count != 0U) {
        RLayoutHeapItem item = r_layout_heap_pop(search);
        bool inserted;
        if (search->nodes[item.node].state.next < 0) {
            *best = item.node;
            return R_LAYOUT_DONE;
        }
        if (!r_layout_seen_insert(search, item.node, &inserted)) {
            return R_LAYOUT_NO_MEMORY;
        }
        if (!inserted) {
            continue;
        }
        if (!r_layout_add_next_state(search, item.penalty, item.node, false) ||
            !r_layout_add_next_state(search, item.penalty, item.node, true)) {
            return R_LAYOUT_LIMIT;
        }
    }
    return R_LAYOUT_UNSUPPORTED;
}

/* --- rendering ------------------------------------------------------------------------- */

static bool
r_layout_emit(char *out, size_t capacity, size_t *length, const char *bytes, size_t count) {
    if (*length + count > capacity) {
        return false;
    }
    (void)memcpy(out + *length, bytes, count);
    *length += count;
    return true;
}

static bool r_layout_emit_spaces(char *out, size_t capacity, size_t *length, unsigned count) {
    unsigned index;
    for (index = 0; index < count; ++index) {
        if (!r_layout_emit(out, capacity, length, " ", 1U)) {
            return false;
        }
    }
    return true;
}

static RLayoutStatus r_layout_render(const RLayoutSearch *search,
                                     int best,
                                     unsigned indent,
                                     char *out,
                                     size_t capacity,
                                     size_t *length) {
    const RLayoutLine *line = search->line;
    bool newline_before[R_LAYOUT_MAX_TOKENS];
    unsigned column_before[R_LAYOUT_MAX_TOKENS];
    int node = best;
    unsigned index;

    (void)memset(newline_before, 0, sizeof(newline_before));
    (void)memset(column_before, 0, sizeof(column_before));
    while ((node > 0) && (search->nodes[node].previous >= 0)) {
        const RLayoutNode *entry = &search->nodes[node];
        const RLayoutNode *from = &search->nodes[entry->previous];
        unsigned token = (unsigned)from->state.next;
        newline_before[token] = entry->newline;
        column_before[token] = entry->state.column - line->tokens[token].length;
        node = entry->previous;
    }
    *length = 0U;
    if (!r_layout_emit_spaces(out, capacity, length, indent) ||
        !r_layout_emit(out, capacity, length, line->tokens[0].text, line->tokens[0].length)) {
        return R_LAYOUT_LIMIT;
    }
    for (index = 1; index < line->count; ++index) {
        const RLayoutToken *token = &line->tokens[index];
        if (newline_before[index]) {
            if (!r_layout_emit(out, capacity, length, "\n", 1U) ||
                !r_layout_emit_spaces(out, capacity, length, column_before[index])) {
                return R_LAYOUT_LIMIT;
            }
        } else if (!r_layout_emit_spaces(out, capacity, length, token->spaces_before)) {
            return R_LAYOUT_LIMIT;
        }
        if (!r_layout_emit(out, capacity, length, token->text, token->length)) {
            return R_LAYOUT_LIMIT;
        }
    }
    return R_LAYOUT_DONE;
}

/* --- entry point ----------------------------------------------------------------------- */

static RLayoutStatus
r_layout_statement(const RLayoutRequest *request, char *out, size_t capacity, size_t *length) {
    RLayoutToken *tokens;
    RLayoutLine line;
    RLayoutSearch search;
    RLayoutStatus status;
    int best = -1;
    unsigned index;

    *length = 0U;
    if (request->indent >= UINT16_MAX / 2U) {
        return R_LAYOUT_UNSUPPORTED;
    }
    tokens = request->allocate(request->context, sizeof(RLayoutToken) * R_LAYOUT_MAX_TOKENS);
    if (tokens == NULL) {
        return R_LAYOUT_NO_MEMORY;
    }
    line.tokens = tokens;
    line.count = 0U;
    if (!r_layout_tokenize(request->text, request->length, &line) || !r_layout_annotate(&line)) {
        request->release(request->context, tokens);
        return R_LAYOUT_UNSUPPORTED;
    }
    {
        unsigned questions = 0U;
        for (index = 0; index < line.count; ++index) {
            if (line.tokens[index].kind == R_LAYOUT_QUESTION) {
                questions += 1U;
            }
            /* Strings may be split by the formatter; chained conditionals are aligned by a
               separate whitespace pass. Neither is modeled. */
            if ((line.tokens[index].kind == R_LAYOUT_STRING) || (questions > 1U)) {
                request->release(request->context, tokens);
                return R_LAYOUT_UNSUPPORTED;
            }
        }
    }
    (void)memset(&search, 0, sizeof(search));
    search.request = request;
    search.line = &line;
    status = r_layout_solve(&search, request->indent, &best);
    if (status == R_LAYOUT_DONE) {
        status = r_layout_render(&search, best, request->indent, out, capacity, length);
    }
    request->release(request->context, search.seen.slots);
    request->release(request->context, search.heap);
    request->release(request->context, search.nodes);
    request->release(request->context, tokens);
    return status;
}

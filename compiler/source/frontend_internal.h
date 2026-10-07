#ifndef R_FRONTEND_INTERNAL_H
#define R_FRONTEND_INTERNAL_H

#include "link_manifest.h"
#include "r_frontend.h"

#include <limits.h>
#include <string.h>

#define R_SOURCE_PADDING 16U
#define R_INITIAL_ARRAY_CAPACITY 16U

typedef enum RTokenKind {
    R_TOKEN_INVALID = 0,
    R_TOKEN_EOF,
    R_TOKEN_WHITESPACE,
    R_TOKEN_LINE_COMMENT,
    R_TOKEN_BLOCK_COMMENT,
    R_TOKEN_BOM,
    R_TOKEN_ERROR,
    R_TOKEN_IDENTIFIER,
    R_TOKEN_INTEGER_LITERAL,
    R_TOKEN_FLOAT_LITERAL,
    R_TOKEN_CHARACTER_LITERAL,
    R_TOKEN_STRING_LITERAL,
    R_TOKEN_FORMAT_START,
    R_TOKEN_FORMAT_END,
    R_TOKEN_FORMAT_TEXT,
    R_TOKEN_FORMAT_SPEC,

    R_TOKEN_LBRACE,
    R_TOKEN_RBRACE,
    R_TOKEN_LBRACKET,
    R_TOKEN_RBRACKET,
    R_TOKEN_LPAREN,
    R_TOKEN_RPAREN,
    R_TOKEN_SEMICOLON,
    R_TOKEN_COMMA,
    R_TOKEN_DOT,
    R_TOKEN_DOT_DOT,
    R_TOKEN_ELLIPSIS,
    R_TOKEN_COLON,
    R_TOKEN_COLON_COLON,
    R_TOKEN_QUESTION,
    R_TOKEN_AT,
    R_TOKEN_PLUS,
    R_TOKEN_MINUS,
    R_TOKEN_STAR,
    R_TOKEN_SLASH,
    R_TOKEN_PERCENT,
    R_TOKEN_AMP,
    R_TOKEN_PIPE,
    R_TOKEN_CARET,
    R_TOKEN_TILDE,
    R_TOKEN_BANG,
    R_TOKEN_EQUAL,
    R_TOKEN_LESS,
    R_TOKEN_GREATER,
    R_TOKEN_PLUS_PLUS,
    R_TOKEN_MINUS_MINUS,
    R_TOKEN_ARROW,
    R_TOKEN_EQUAL_EQUAL,
    R_TOKEN_BANG_EQUAL,
    R_TOKEN_LESS_EQUAL,
    R_TOKEN_GREATER_EQUAL,
    R_TOKEN_AMP_AMP,
    R_TOKEN_PIPE_PIPE,
    R_TOKEN_LESS_LESS,
    R_TOKEN_GREATER_GREATER,
    R_TOKEN_PLUS_EQUAL,
    R_TOKEN_MINUS_EQUAL,
    R_TOKEN_STAR_EQUAL,
    R_TOKEN_SLASH_EQUAL,
    R_TOKEN_PERCENT_EQUAL,
    R_TOKEN_AMP_EQUAL,
    R_TOKEN_PIPE_EQUAL,
    R_TOKEN_CARET_EQUAL,
    R_TOKEN_LESS_LESS_EQUAL,
    R_TOKEN_GREATER_GREATER_EQUAL,

    R_TOKEN_KW_ALIGNOF,
    R_TOKEN_KW_AI8,
    R_TOKEN_KW_AI16,
    R_TOKEN_KW_AI32,
    R_TOKEN_KW_AI64,
    R_TOKEN_KW_AISIZE,
    R_TOKEN_KW_ARC,
    R_TOKEN_KW_ARRAY,
    R_TOKEN_KW_AS,
    R_TOKEN_KW_ASYNC,
    R_TOKEN_KW_ATOMIC,
    R_TOKEN_KW_AU8,
    R_TOKEN_KW_AU16,
    R_TOKEN_KW_AU32,
    R_TOKEN_KW_AU64,
    R_TOKEN_KW_AUSIZE,
    R_TOKEN_KW_AWAIT,
    R_TOKEN_KW_BOOL,
    R_TOKEN_KW_BREAK,
    R_TOKEN_KW_CASE,
    R_TOKEN_KW_CATCH,
    R_TOKEN_KW_CHAR,
    R_TOKEN_KW_CONST,
    R_TOKEN_KW_CONSTEXPR,
    R_TOKEN_KW_CONTINUE,
    R_TOKEN_KW_DEFAULT,
    R_TOKEN_KW_DICT,
    R_TOKEN_KW_DROP,
    R_TOKEN_KW_DYN,
    R_TOKEN_KW_ELSE,
    R_TOKEN_KW_ENUM,
    R_TOKEN_KW_EXTERN,
    R_TOKEN_KW_F32,
    R_TOKEN_KW_F64,
    R_TOKEN_KW_FALSE,
    R_TOKEN_KW_FALLTHROUGH,
    R_TOKEN_KW_FINALLY,
    R_TOKEN_KW_FN,
    R_TOKEN_KW_FOR,
    R_TOKEN_KW_I8,
    R_TOKEN_KW_I16,
    R_TOKEN_KW_I32,
    R_TOKEN_KW_I64,
    R_TOKEN_KW_IF,
    R_TOKEN_KW_IMPORT,
    R_TOKEN_KW_ISIZE,
    R_TOKEN_KW_LIST,
    R_TOKEN_KW_MODULE,
    R_TOKEN_KW_MOVE,
    R_TOKEN_KW_NEVER,
    R_TOKEN_KW_NEW,
    R_TOKEN_KW_NULL,
    R_TOKEN_KW_NULL_T,
    R_TOKEN_KW_O,
    R_TOKEN_KW_OPAQUE,
    R_TOKEN_KW_OWN,
    R_TOKEN_KW_PANIC,
    R_TOKEN_KW_PROTECTED,
    R_TOKEN_KW_RAW,
    R_TOKEN_KW_RC,
    R_TOKEN_KW_RETURN,
    R_TOKEN_KW_SIZEOF,
    R_TOKEN_KW_STATIC,
    R_TOKEN_KW_STR,
    R_TOKEN_KW_STRUCT,
    R_TOKEN_KW_SWITCH,
    R_TOKEN_KW_TASK,
    R_TOKEN_KW_THREAD_LOCAL,
    R_TOKEN_KW_THREAD_SCOPE,
    R_TOKEN_KW_THROW,
    R_TOKEN_KW_THROWS,
    R_TOKEN_KW_TRUE,
    R_TOKEN_KW_TRY,
    R_TOKEN_KW_U8,
    R_TOKEN_KW_U16,
    R_TOKEN_KW_U32,
    R_TOKEN_KW_U64,
    R_TOKEN_KW_UNSAFE,
    R_TOKEN_KW_USIZE,
    R_TOKEN_KW_VARIANT,
    R_TOKEN_KW_VOID,
    R_TOKEN_KW_WEAK,
    R_TOKEN_KW_WHILE,
    /* Method and trait keywords (R-LEX-0008, R-FUNC-0013, R-TYPE-0041). */
    R_TOKEN_KW_IMPL,
    R_TOKEN_KW_SELF,
    R_TOKEN_KW_THIS,
    R_TOKEN_KW_TRAIT,
    /* Inferred local type (R-NAME-0011). */
    R_TOKEN_KW_AUTO,
    /* Iteration and membership (R-STMT-0014, R-EXPR-0029). */
    R_TOKEN_KW_IN,

    R_TOKEN_KW_C_CHAR,
    R_TOKEN_KW_C_SCHAR,
    R_TOKEN_KW_C_UCHAR,
    R_TOKEN_KW_C_SHORT,
    R_TOKEN_KW_C_USHORT,
    R_TOKEN_KW_C_INT,
    R_TOKEN_KW_C_UINT,
    R_TOKEN_KW_C_LONG,
    R_TOKEN_KW_C_ULONG,
    R_TOKEN_KW_C_LLONG,
    R_TOKEN_KW_C_ULLONG,
    R_TOKEN_KW_C_BOOL,
    R_TOKEN_KW_C_WCHAR,
    R_TOKEN_KW_C_WINT,
    R_TOKEN_KW_C_INT8,
    R_TOKEN_KW_C_UINT8,
    R_TOKEN_KW_C_INT16,
    R_TOKEN_KW_C_UINT16,
    R_TOKEN_KW_C_INT32,
    R_TOKEN_KW_C_UINT32,
    R_TOKEN_KW_C_INT64,
    R_TOKEN_KW_C_UINT64,
    R_TOKEN_KW_C_INTPTR,
    R_TOKEN_KW_C_UINTPTR,
    R_TOKEN_KW_C_INTMAX,
    R_TOKEN_KW_C_UINTMAX,
    R_TOKEN_KW_C_FLOAT,
    R_TOKEN_KW_C_DOUBLE,
    R_TOKEN_KW_C_LONG_DOUBLE,
    R_TOKEN_KW_C_SIZE,
    R_TOKEN_KW_C_PTRDIFF,

    R_TOKEN_KIND_COUNT
} RTokenKind;

typedef struct RToken {
    RTokenKind kind;
    RSourceSpan span;
    uint32_t intern_id;
} RToken;

typedef struct RInternEntry {
    char *bytes;
    size_t length;
    uint64_t hash;
} RInternEntry;

typedef enum RSyntaxKind {
    R_SYNTAX_INVALID = 0,
    R_SYNTAX_TRANSLATION_UNIT,
    R_SYNTAX_MODULE_DECLARATION,
    R_SYNTAX_IMPORT_DECLARATION,
    R_SYNTAX_EXTERNAL_DECLARATION,
    R_SYNTAX_GENERIC_HEADER,
    R_SYNTAX_GENERIC_PARAMETER,
    R_SYNTAX_ASSOCIATED_NAME,
    R_SYNTAX_ATTRIBUTE,
    R_SYNTAX_STRUCT_DECLARATION,
    R_SYNTAX_ERROR_STRUCT_DECLARATION,
    R_SYNTAX_FIELD_DECLARATION,
    R_SYNTAX_ENUM_DECLARATION,
    R_SYNTAX_ERROR_ENUM_DECLARATION,
    R_SYNTAX_ENUM_VARIANT,
    R_SYNTAX_OBJECT_DECLARATION,
    /* R-STMT-0022 (L37.3): `auto (a, b) = tuple;`. */
    R_SYNTAX_DESTRUCTURING_DECLARATION,
    /* R-STMT-0022 (L40): one `.field = name` or `.field` of `auto {...} = value;`. */
    R_SYNTAX_DESTRUCTURING_FIELD,
    R_SYNTAX_FUNCTION_DECLARATION,
    R_SYNTAX_PARAMETER_LIST,
    R_SYNTAX_PARAMETER,
    R_SYNTAX_THROWS_CLAUSE,
    R_SYNTAX_DROP_DEFINITION,
    R_SYNTAX_EXTERN_BLOCK,
    R_SYNTAX_C_DECLARATION,
    R_SYNTAX_TRAIT_DECLARATION,
    R_SYNTAX_TRAIT_NAME,
    R_SYNTAX_IMPL_DECLARATION,
    R_SYNTAX_LAMBDA_DECLARATION,
    R_SYNTAX_MOVE_CAPTURE_LIST,
    R_SYNTAX_CALLABLE_CONSTRAINT,
    R_SYNTAX_OPAQUE_RESULT,
    R_SYNTAX_DYN_TYPE,
    R_SYNTAX_ASSOCIATED_TYPE,
    R_SYNTAX_ASSOCIATED_CONSTANT,
    R_SYNTAX_TYPE,
    R_SYNTAX_INITIALIZER,
    R_SYNTAX_AGGREGATE_INITIALIZER,
    R_SYNTAX_INITIALIZER_ITEM,
    R_SYNTAX_BLOCK,
    R_SYNTAX_EXPRESSION_STATEMENT,
    R_SYNTAX_IF_STATEMENT,
    R_SYNTAX_WHILE_STATEMENT,
    R_SYNTAX_FOR_STATEMENT,
    R_SYNTAX_FOR_IN_STATEMENT,
    R_SYNTAX_SWITCH_STATEMENT,
    R_SYNTAX_CASE_CLAUSE,
    R_SYNTAX_DEFAULT_CLAUSE,
    R_SYNTAX_CASE_PATTERN,
    R_SYNTAX_MATCH_EXPRESSION,
    R_SYNTAX_MATCH_ARM,
    R_SYNTAX_MATCH_PATTERN,
    R_SYNTAX_MATCH_FIELD,
    R_SYNTAX_MATCH_GUARD,
    /* R-STMT-0002 (L37.1): `value is pattern` as the condition of if or while. */
    R_SYNTAX_PATTERN_TEST,
    R_SYNTAX_THREAD_SCOPE_STATEMENT,
    R_SYNTAX_TASK_SCOPE_STATEMENT,
    R_SYNTAX_SELECT_STATEMENT,
    R_SYNTAX_SELECT_CLAUSE,
    R_SYNTAX_SELECT_DEADLINE,
    R_SYNTAX_DEADLINE_STATEMENT,
    R_SYNTAX_BUDGET_STATEMENT,
    R_SYNTAX_TRY_STATEMENT,
    R_SYNTAX_CATCH_CLAUSE,
    R_SYNTAX_FINALLY_CLAUSE,
    R_SYNTAX_JUMP_STATEMENT,
    /* R-STMT-0004 (L37.2): a loop with a label. */
    R_SYNTAX_LABELED_STATEMENT,
    R_SYNTAX_THROW_STATEMENT,
    R_SYNTAX_THROW_CONDITION,
    R_SYNTAX_THROW_ELSE,
    R_SYNTAX_DROP_STATEMENT,
    R_SYNTAX_AWAIT_STATEMENT,
    R_SYNTAX_AWAIT_OPERATION,
    R_SYNTAX_UNSAFE_BLOCK,
    R_SYNTAX_EXPRESSION,
    R_SYNTAX_ASSIGNMENT_EXPRESSION,
    R_SYNTAX_CONDITIONAL_EXPRESSION,
    R_SYNTAX_MEMBERSHIP_EXPRESSION,
    R_SYNTAX_RANGE_EXPRESSION,
    R_SYNTAX_COLLECTION_EXPRESSION,
    R_SYNTAX_DICT_EXPRESSION,
    R_SYNTAX_DICT_ENTRY,
    R_SYNTAX_COMPREHENSION_FOR,
    R_SYNTAX_COMPREHENSION_IF,
    R_SYNTAX_SPREAD_ARGUMENT,
    R_SYNTAX_OUT_ARGUMENT,
    R_SYNTAX_BINARY_EXPRESSION,
    R_SYNTAX_UNARY_EXPRESSION,
    R_SYNTAX_CAST_EXPRESSION,
    R_SYNTAX_POSTFIX_EXPRESSION,
    R_SYNTAX_CALL_SUFFIX,
    R_SYNTAX_CONSTRUCTOR_SUFFIX,
    R_SYNTAX_INDEX_SUFFIX,
    R_SYNTAX_SLICE_SUFFIX,
    R_SYNTAX_MEMBER_SUFFIX,
    R_SYNTAX_METHOD_CALL_SUFFIX,
    R_SYNTAX_PRIMARY_EXPRESSION,
    R_SYNTAX_QUALIFIED_NAME,
    R_SYNTAX_ARGUMENT_LIST,
    R_SYNTAX_AGGREGATE_CONSTRUCTOR,
    R_SYNTAX_STANDARD_TYPE_CALL,
    R_SYNTAX_NEW_EXPRESSION,
    R_SYNTAX_STRING_SEQUENCE,
    R_SYNTAX_FORMAT_SEQUENCE,
    R_SYNTAX_FORMAT_LITERAL,
    R_SYNTAX_FORMAT_SLOT,
    R_SYNTAX_FORMAT_SUFFIX,
    R_SYNTAX_AMBIGUOUS_DECL_OR_EXPR,
    R_SYNTAX_ERROR,
    R_SYNTAX_TOKEN,
    R_SYNTAX_STATIC_IF_STATEMENT,
    R_SYNTAX_STATIC_IF_DECLARATION,
    R_SYNTAX_STATIC_DECLARATIONS,
    R_SYNTAX_STATIC_PREDICATE,
    R_SYNTAX_STATIC_CONJUNCTION,
    R_SYNTAX_STATIC_ATOM,
    R_SYNTAX_CALLABLE_MODE,
    R_SYNTAX_GENERIC_ARGUMENT_LIST,
    /* R-TYPE-0052 (L18.1): `(e1, e2, ...)`. */
    R_SYNTAX_TUPLE_EXPRESSION,
    /* R-TYPE-0053 (L18.2): `T...` naming a pack in an argument list, as in `len(T...)`. */
    R_SYNTAX_PACK_EXPANSION,
    R_SYNTAX_KIND_COUNT
} RSyntaxKind;

typedef enum RCstEventKind {
    R_CST_EVENT_OPEN = 0,
    R_CST_EVENT_CLOSE,
    R_CST_EVENT_TOKEN,
    R_CST_EVENT_MISSING
} RCstEventKind;

typedef struct RCstEvent {
    RCstEventKind kind;
    RSyntaxKind syntax_kind;
    RTokenId token;
    RTokenKind missing_kind;
    RSourceSpan span;
    uint32_t matching_event;
    bool call_free;
    /* R-FUNC-0024 (L19): a postfix expression whose method call is continued by further
       suffixes without parentheses; the called method shall be declared @chain. */
    bool implicit_chain;
} RCstEvent;

typedef struct RAstNode {
    RSyntaxKind kind;
    RSourceSpan span;
    RSyntaxNodeId syntax;
    RTokenId token;
    uint32_t first_child;
    uint32_t child_count;
    /* Semantic-only identifier; generated nodes are not attached to the source AST root. */
    uint32_t generated_name;
    RTokenKind generated_token_kind;
    bool call_free;
    bool implicit_chain;
} RAstNode;

typedef struct RAstRef {
    RSourceId source;
    RAstNodeId node;
} RAstRef;

typedef struct RAstNodeView {
    RSyntaxKind kind;
    RSourceSpan span;
    RSyntaxNodeId syntax;
    RTokenId token;
    uint32_t child_count;
    bool call_free;
    bool implicit_chain;
} RAstNodeView;

typedef struct RAstTokenView {
    RTokenId token;
    RTokenKind kind;
    RSourceSpan span;
    uint32_t intern_id;
} RAstTokenView;

typedef struct RAstTextView {
    /* Borrowed immutable source bytes; valid until r_frontend_destroy. */
    const uint8_t *bytes;
    size_t length;
} RAstTextView;

typedef enum RSemanticTypeKind {
    R_SEMANTIC_TYPE_INVALID = 0,
    R_SEMANTIC_TYPE_BOOL,
    R_SEMANTIC_TYPE_I8,
    R_SEMANTIC_TYPE_I16,
    R_SEMANTIC_TYPE_I32,
    R_SEMANTIC_TYPE_I64,
    R_SEMANTIC_TYPE_ISIZE,
    R_SEMANTIC_TYPE_U8,
    R_SEMANTIC_TYPE_U16,
    R_SEMANTIC_TYPE_U32,
    R_SEMANTIC_TYPE_U64,
    R_SEMANTIC_TYPE_USIZE,
    R_SEMANTIC_TYPE_F32,
    R_SEMANTIC_TYPE_F64,
    R_SEMANTIC_TYPE_C_ABI_NUMERIC,
    R_SEMANTIC_TYPE_CHAR,
    R_SEMANTIC_TYPE_STR,
    R_SEMANTIC_TYPE_CONSTEXPR_STR,
    R_SEMANTIC_TYPE_VOID,
    R_SEMANTIC_TYPE_NEVER,
    R_SEMANTIC_TYPE_NULL_T,
    R_SEMANTIC_TYPE_PARAMETER,
    R_SEMANTIC_TYPE_STRUCT,
    R_SEMANTIC_TYPE_ENUM,
    /* R-TYPE-0051: a dyn interface; `base` is the parameter that carries its contracts. It is
       a closed type that exists only as the referent of a borrow. */
    R_SEMANTIC_TYPE_DYN,
    R_SEMANTIC_TYPE_FIXED_ARRAY,
    R_SEMANTIC_TYPE_SLICE,
    R_SEMANTIC_TYPE_BORROW,
    R_SEMANTIC_TYPE_ARRAY,
    R_SEMANTIC_TYPE_LIST,
    R_SEMANTIC_TYPE_DICT,
    R_SEMANTIC_TYPE_TASK,
    R_SEMANTIC_TYPE_ARC,
    R_SEMANTIC_TYPE_RC,
    R_SEMANTIC_TYPE_WEAK,
    R_SEMANTIC_TYPE_OWN,
    R_SEMANTIC_TYPE_RAW,
    R_SEMANTIC_TYPE_RAW_FUNCTION,
    /* Ordered, interned signature parameter list; never a source value type. */
    R_SEMANTIC_TYPE_FUNCTION_PARAMETER,
    R_SEMANTIC_TYPE_STANDARD,
    R_SEMANTIC_TYPE_OPTION,
    R_SEMANTIC_TYPE_RESULT,
    R_SEMANTIC_TYPE_CONST,
    R_SEMANTIC_TYPE_ATOMIC,
    /* Internal, non-source types used by checked-effect lowering. */
    R_SEMANTIC_TYPE_EFFECT_SET,
    R_SEMANTIC_TYPE_EFFECT_CARRIER,
    /* Checked constant-expression DAG used by dependent array bounds. */
    R_SEMANTIC_TYPE_CONSTANT_EXPR,
    /* R-TYPE-0045 (L16.3): the binding of an associated type with parameters in an
       implementation: `base` is the bound type and `length` the schema of its own parameters.
       It is applied at once and never is the type of a value. */
    R_SEMANTIC_TYPE_TYPE_FUNCTION,
    /* R-TYPE-0054 (L28): a safe function value `fn(P...) -> R` or `async fn(P...) -> R`: `base`
       is the result or its effect carrier, `second` the FUNCTION_PARAMETER chain, `length` the
       parameter count and `flags` the resource guarantees and CALL_ASYNC. */
    R_SEMANTIC_TYPE_FUNCTION
} RSemanticTypeKind;

enum {
    R_SEMANTIC_TYPE_FLAG_NONE = 0,
    R_SEMANTIC_TYPE_FLAG_SHARED = UINT32_C(1) << 0U,
    R_SEMANTIC_TYPE_FLAG_NULLABLE = UINT32_C(1) << 1U,
    R_SEMANTIC_TYPE_FLAG_RC_OWNER = UINT32_C(1) << 2U,
    /* Raw function type of a variadic C import (R-FFI-0005); never spelled by R source. */
    R_SEMANTIC_TYPE_FLAG_VARIADIC = UINT32_C(1) << 3U,
    /* Callable signature key of a closure constraint (R-TYPE-0044); never a value type. */
    R_SEMANTIC_TYPE_FLAG_CALLABLE = UINT32_C(1) << 4U,
    R_SEMANTIC_TYPE_FLAG_CALL_MUT = UINT32_C(1) << 5U,
    R_SEMANTIC_TYPE_FLAG_CALL_ONCE = UINT32_C(1) << 6U,
    R_SEMANTIC_TYPE_FLAG_NOALLOC = UINT32_C(1) << 7U,
    R_SEMANTIC_TYPE_FLAG_NONBLOCKING = UINT32_C(1) << 8U,
    R_SEMANTIC_TYPE_FLAG_CALL_ASYNC = UINT32_C(1) << 9U,
    R_SEMANTIC_TYPE_FLAG_OUT = UINT32_C(1) << 10U,
    R_SEMANTIC_TYPE_FLAG_RESOURCES = R_SEMANTIC_TYPE_FLAG_NOALLOC | R_SEMANTIC_TYPE_FLAG_NONBLOCKING
};

typedef struct RSemanticType {
    RSemanticTypeKind kind;
    RTypeId base;
    RTypeId second;
    uint64_t length;
    uint32_t flags;
    /* Concrete executable type after nominal opaque contracts have been checked. */
    RTypeId representation;
} RSemanticType;

/* Integer representation from the pinned target, without changing nominal identity. */
RSemanticTypeKind r_semantic_integer_representation(const RFrontendContext *context,
                                                    RTypeId type_id);

typedef enum RSemanticAggregateKind {
    R_SEMANTIC_AGGREGATE_INVALID = 0,
    R_SEMANTIC_AGGREGATE_STRUCT,
    R_SEMANTIC_AGGREGATE_ENUM
} RSemanticAggregateKind;

enum {
    R_GENERIC_COPY = UINT32_C(1) << 0U,
    R_GENERIC_POD = UINT32_C(1) << 1U,
    R_GENERIC_SEND = UINT32_C(1) << 2U,
    R_GENERIC_SYNC = UINT32_C(1) << 3U,
    R_GENERIC_KEY = UINT32_C(1) << 4U,
    R_GENERIC_ERROR = UINT32_C(1) << 5U,
    R_GENERIC_UNBORROWED = UINT32_C(1) << 6U,
    R_GENERIC_JSON_ENCODE = UINT32_C(1) << 7U,
    R_GENERIC_JSON_DECODE = UINT32_C(1) << 8U,
    R_GENERIC_ERRORS = UINT32_C(1) << 9U,
    /* R-TYPE-0033 (L26): core::clone produces an independent value (R-OWN-0020). */
    R_GENERIC_CLONE = UINT32_C(1) << 10U
};

/* The capability after `property` in R_GENERIC_COPY..R_GENERIC_CLONE; the error-set marker
   R_GENERIC_ERRORS is not a capability of a type and is skipped. */
static inline uint32_t r_generic_next_capability(uint32_t property) {
    property <<= 1U;
    return property == R_GENERIC_ERRORS ? property << 1U : property;
}

typedef struct RGenericParameter {
    /* Zero for type parameters; usize for a compile-time value parameter. */
    RTypeId constant_type;
    RSourceSpan name_span;
    uint32_t name_intern_id;
    uint32_t constraints;
    RTypeId type;
    /* Branch-local evidence has its own type identity; closing follows the original parameter. */
    RTypeId refinement_origin;
    /* Opaque return facade: nominal identity and its checked concrete representation. */
    RAstRef opaque_declaration;
    RTypeId opaque_concrete;
    uint32_t opaque_schema;
    uint32_t opaque_origin;
    uint32_t opaque_first_argument;
    uint32_t excluded_constraints;
    uint32_t first_static_fact;
    uint32_t static_fact_count;
    /* R-TYPE-0043: trait constraints, as one-based trait indices. */
    uint32_t first_trait_constraint;
    uint32_t trait_constraint_count;
    /* R-TYPE-0045: a projection `P::Name` is a synthetic parameter over the parameter type
       `projection_base`; it names associated type `projection_index` of `projection_trait`.
       Ordinary parameters have a zero base. */
    RTypeId projection_base;
    /* Exact associated-type equality supplied by an opaque return contract. */
    RTypeId projection_bound;
    uint32_t projection_trait;
    uint32_t projection_index;
    /* R-TYPE-0045 (L16.3): an application `A<T...>` of an associated type with parameters is a
       synthetic parameter over `application_base`, the associated type of a trait or a
       projection, and `application_arguments`, the FUNCTION_PARAMETER chain of its arguments.
       It carries the constraints of its base and follows it through substitution. */
    RTypeId application_base;
    RTypeId application_arguments;
    /* R-TYPE-0053 (L18.2): a pack `T...`; its argument is the tuple of the types it stands for
       and its constraints apply to each element. */
    bool is_pack;
    /* R-TYPE-0053 (L18.4): the constraints of each element of a pack are those of the synthetic
       parameter `pack_element`; the pack itself proves only what a tuple of such elements
       proves. A synthetic parameter over the pack `pack_base` is its element `pack_index`
       (`pack_derived` false), or the pack of the `pack_prefix_count` types of the
       FUNCTION_PARAMETER chain `pack_prefix` followed by the elements of `pack_base` from
       `pack_index` on (`pack_derived` true). */
    RTypeId pack_element;
    RTypeId pack_base;
    RTypeId pack_prefix;
    uint32_t pack_prefix_count;
    uint32_t pack_index;
    bool pack_derived;
    /* The lengths a branch of `@if (len(T...) ...)` proves for the pack (R-META-0003). */
    uint32_t pack_min_length;
    uint32_t pack_max_length;
    bool pack_max_known;
    /* The first use of the application, where its arguments prove the parameter constraints. */
    RSourceSpan application_span;
    /* R-TYPE-0051: the canonical contract spelling of a dyn interface; zero otherwise. */
    uint32_t dyn_key;
} RGenericParameter;

/* A constraint name that is not in the closed vocabulary; resolved once traits are known. */
typedef struct RGenericPendingConstraint {
    uint32_t parameter;
    uint32_t name_intern_id;
    /* R-TYPE-0043: the interned module path of `m.n::Name`; zero for an unqualified name. */
    uint32_t module_intern_id;
    RSourceSpan span;
    /* The constraint is spelled `core::Name` and names a core trait (R-TYPE-0046). */
    bool is_core;
    RAstRef application;
} RGenericPendingConstraint;

/* R-TYPE-0043 (M19): `P::Name: constraints` in the header of a generic function. The holder is a
   generic parameter outside every schema that collects the constraints; the projection is the
   constrained associated type of P once the constraints of P are known. */
typedef struct RGenericProjectionConstraint {
    uint32_t schema;
    uint32_t holder;
    RAstRef entry;
    RSourceSpan span;
    RTypeId projection;
} RGenericProjectionConstraint;

/* A callable constraint `fn(P...) -> R` awaiting type resolution (R-TYPE-0044). */
typedef struct RGenericPendingCallable {
    uint32_t parameter;
    RAstRef node;
} RGenericPendingCallable;

typedef struct RGenericSchema {
    RSourceSpan span;
    RAstNodeId external_ast;
    RAstNodeId declaration_ast;
    uint32_t first_parameter;
    uint32_t parameter_count;
    uint32_t aggregate;
    RSymbolId function;
    /* The parameters of an associated type or of its binding (L16.3); names inside `span`
       resolve among them before the enclosing schema. */
    bool associated_parameters;
    /* The schema of a synthesized tuple struct (R-TYPE-0052); it has no source span. */
    bool tuple;
} RGenericSchema;

/* @c_type kind of an extern "C" aggregate (R-FFI-0016). */
typedef enum RCTypeKind {
    R_C_TYPE_KIND_NONE = 0,
    R_C_TYPE_KIND_STRUCT,
    R_C_TYPE_KIND_UNION,
    R_C_TYPE_KIND_ENUM,
    R_C_TYPE_KIND_TYPEDEF
} RCTypeKind;

/* L21.5: the root of the standard errors has no aggregate; its family marks it this way. */
#define R_SEMANTIC_STANDARD_ERROR_ROOT UINT32_MAX

typedef struct RSemanticAggregate {
    RSemanticAggregateKind kind;
    /* R-AGG-0011 (L21.1): the one-based parent of an error with fields, or zero. */
    uint32_t error_parent;
    /* L21.3: a program error derives from this one, so a value of its type holds any member of
       its subtree. */
    bool error_has_children;
    /* L21.3: the synthesized family of an error with descendants, a tagged enum whose variants
       carry the members of the subtree, the error itself first; zero for other aggregates. */
    RTypeId error_family_type;
    /* The one-based error whose family this synthesized enum is; zero for other aggregates, and
       R_SEMANTIC_STANDARD_ERROR_ROOT for the family of std.error::fault (R-SLIB-ERR-0004). */
    uint32_t error_family_of;
    RSourceSpan name_span;
    uint32_t name_intern_id;
    RSourceId module_source;
    RAstNodeId declaration_ast;
    RTypeId type;
    RTypeId enum_underlying_type;
    uint32_t first_field;
    uint32_t field_count;
    uint32_t first_variant;
    uint32_t variant_count;
    bool is_protected;
    bool is_error;
    bool is_repr_c;
    bool is_tagged;
    uint32_t payload_parent;
    RTypeId standard_payload_element;
    uint32_t generic_schema;
    uint32_t generic_origin;
    uint32_t first_generic_argument;
    uint32_t format_schema;
    RSymbolId drop_function;
    RSymbolId hash_function;
    RSymbolId equal_function;
    RSymbolId json_marshal_function;
    RSymbolId json_unmarshal_function;
    RSymbolId json_is_zero_function;
    /* M32.6 (R-SLIB-JSON-0002): `std.json::value T::json_schema()` of a type with converters. */
    RSymbolId json_schema_function;
    /* R-OWN-0020 (L26): the associated `T::clone` hook, or zero. */
    RSymbolId clone_function;
    /* R-INIT-0005 (L33): one plus the index of the `@default` variant of an enum, or zero. */
    uint32_t default_variant;
    /* R-TYPE-0046 (L32): the core::Format method of this closed type once a formatted value
       reaches it, or zero. */
    RSymbolId format_function;
    RTypeId json_schema_type;
    uint32_t first_json_field;
    uint32_t json_field_count;
    bool json_schema_invalid;
    bool is_copy;
    bool is_default_initializable;
    bool complete;
    bool poisoned;
    /* An instance whose fields are being substituted: its identity is published for recursive
       owners, but it has no layout yet (a computed bound may not measure it, R-TYPE-0047). */
    bool completing;
    /* One-based ownership component of a self-nesting aggregate (R-FUNC-0004); zero otherwise. */
    uint32_t recursive_group;
    /* A JSON encoder or decoder was requested for this aggregate (directly or as a member). */
    bool json_derived;
    /* R-OWN-0020: a structural clone was declared for this aggregate (`@derive(clone)`). */
    bool clone_derived;
    /* R-AGG-0012 (L27): the capabilities `@derive` lists (bit 0 clone, 1 equal, 2 ordered,
       3 key, 4 format) and the capability names that list them. */
    uint32_t derived;
    RSourceSpan derive_origins[5];
    /* Environment of one lambda declaration (R-FUNC-0015): its fields are the captures. */
    bool is_closure;
    /* R-TYPE-0052 (L18.1): the synthesized generic struct of one tuple arity, or an instance of
       it; its fields are the elements, named by their index. */
    bool is_tuple;
    RTypeId closure_signature;
    /* A capture-free callable whose identity is an ordinary, statically known function. */
    RSymbolId function_item_target;
    bool is_must_use;
    /* opaque struct of an extern "C" block (R-FFI-0015): never complete, raw-pointer-only. */
    bool is_opaque;
    /* Complete @repr(C) aggregate declared inside an extern "C" block (R-FFI-0017): its member
       or enumerator inventory is proven one-for-one against the block's ABI record. */
    bool is_c_declared;
    /* Declaring extern block: AST node while collecting, then the one-based block index. */
    RAstNodeId extern_block_ast;
    uint32_t extern_block;
    /* The main translation unit may not spell the C name (typedef or reserved spelling);
       the header-compatible bridge of R-CMAP-0026 substitutes it. */
    bool c_name_private;
    RCTypeKind c_type_kind;
    /* Interned C tag or typedef spelling selected by @c_type; zero when the R name is used. */
    uint32_t c_name_intern_id;
    /* R-AGG-0013 (L42): the R_ATTRIBUTE_TARGET_* bits of an attribute type, zero otherwise. */
    uint32_t attribute_targets;
    /* The external declaration node, whose attributes mark the type. */
    RAstNodeId external_ast;
} RSemanticAggregate;

/* R-AGG-0013 (L42): what an attribute type may mark. */
enum {
    R_ATTRIBUTE_TARGET_TYPE = UINT32_C(1),
    R_ATTRIBUTE_TARGET_FIELD = UINT32_C(2),
    R_ATTRIBUTE_TARGET_VARIANT = UINT32_C(4)
};

/* One argument of a user attribute: a translation-time constant of the type of its field. */
typedef struct RSemanticAttributeValue {
    RTypeId type;
    /* An integer, a char, a bool, the bits of an f32 or f64, or the value of an enumerator. */
    uint64_t bits;
    /* A str argument: its interned bytes and length. */
    uint32_t text_intern_id;
    uint32_t text_length;
} RSemanticAttributeValue;

/* One use of a user attribute on a type, a struct field or an enumerator; its values follow the
   fields of the attribute type, starting at first_value. */
typedef struct RSemanticAttributeUse {
    RAstRef attribute;
    uint32_t target;
    /* The type of the aggregate that declares the marked type, field or enumerator; aggregates are
       renumbered after collection, types are not. A type use is recorded before its aggregate is
       complete, with its declaration node, and takes the type when resolved. */
    RTypeId owner;
    RAstNodeId declaration;
    /* The declaration index of the field or enumerator; zero for a type. */
    uint32_t member;
    RTypeId attribute_type;
    uint32_t first_value;
    bool valid;
} RSemanticAttributeUse;

/* R-REFL-0005: the valid use of an attribute type on a type (member 0), a struct field or an
   enumerator of the aggregate type `owner`; a generic instance answers for its origin. */
const RSemanticAttributeUse *r_semantic_attribute_use(const RFrontendContext *context,
                                                      RTypeId attribute_type,
                                                      uint32_t target,
                                                      RTypeId owner,
                                                      uint32_t member);

enum {
    R_JSON_FIELD_NAME = UINT32_C(1) << 0U,
    R_JSON_FIELD_SKIP = UINT32_C(1) << 1U,
    R_JSON_FIELD_OMITEMPTY = UINT32_C(1) << 2U,
    R_JSON_FIELD_OMITZERO = UINT32_C(1) << 3U,
    R_JSON_FIELD_STRING = UINT32_C(1) << 4U,
    R_JSON_FIELD_CASE = UINT32_C(1) << 5U,
    R_JSON_FIELD_EMBED = UINT32_C(1) << 6U,
    R_JSON_FIELD_OPTIONAL = UINT32_C(1) << 7U,
    R_JSON_FIELD_DEFAULT = UINT32_C(1) << 8U,
    R_JSON_FIELD_OMITNONE = UINT32_C(1) << 9U,
    /* M32.6: the description of the field in its JSON Schema (R-JSON-0001). */
    R_JSON_FIELD_DESCRIPTION = UINT32_C(1) << 10U
};

typedef enum RJsonOperation {
    R_JSON_OPERATION_INVALID = 0,
    R_JSON_OPERATION_PARSE,
    R_JSON_OPERATION_PARSE_WITH_OPTIONS,
    R_JSON_OPERATION_STRINGIFY,
    R_JSON_OPERATION_STRINGIFY_WITH_OPTIONS,
    R_JSON_OPERATION_KIND,
    R_JSON_OPERATION_LEN,
    R_JSON_OPERATION_TEXT,
    R_JSON_OPERATION_BOOLEAN,
    R_JSON_OPERATION_NULL,
    R_JSON_OPERATION_FROM_BOOL,
    R_JSON_OPERATION_FROM_STRING,
    R_JSON_OPERATION_FROM_NUMBER,
    R_JSON_OPERATION_ARRAY,
    R_JSON_OPERATION_OBJECT,
    R_JSON_OPERATION_PARSE_NUMBER,
    R_JSON_OPERATION_NUMBER_TEXT,
    R_JSON_OPERATION_NUMBER_IS_ZERO,
    R_JSON_OPERATION_NAME_EQUAL,
    R_JSON_OPERATION_APPEND,
    R_JSON_OPERATION_INSERT,
    R_JSON_OPERATION_GET,
    R_JSON_OPERATION_FIND,
    R_JSON_OPERATION_KEY_AT,
    R_JSON_OPERATION_TAKE_INDEX,
    R_JSON_OPERATION_TAKE_FIELD,
    R_JSON_OPERATION_MARSHAL,
    R_JSON_OPERATION_MARSHAL_WITH_OPTIONS,
    R_JSON_OPERATION_UNMARSHAL,
    R_JSON_OPERATION_UNMARSHAL_WITH_OPTIONS,
    R_JSON_OPERATION_NEW_DECODER,
    R_JSON_OPERATION_FEED,
    R_JSON_OPERATION_TAKE,
    R_JSON_OPERATION_NEW_READER,
    R_JSON_OPERATION_READ_NEXT,
    R_JSON_OPERATION_DETACH,
    R_JSON_OPERATION_TAKE_HANDLE,
    R_JSON_OPERATION_TAKE_BYTES,
    /* M32.6: std.json::schema::<T>() (R-SLIB-JSON-0002). */
    R_JSON_OPERATION_SCHEMA,
} RJsonOperation;

enum {
    R_JSON_DEFAULT_NONE = 0,
    R_JSON_DEFAULT_SCALAR,
    R_JSON_DEFAULT_STRING,
    R_JSON_DEFAULT_FACTORY
};

typedef struct RJsonFieldContract {
    uint32_t flags;
    uint32_t name_intern_id;
    uint32_t description_intern_id;
    RAstNodeId default_ast;
    uint32_t default_kind;
    uint32_t default_string;
    uint64_t default_bits;
    RSemanticTypeKind default_scalar_kind;
    RHirNodeId default_node;
    RSymbolId default_factory;
    RSourceSpan span;
    bool ignore_case;
    bool present;
} RJsonFieldContract;

typedef struct RSemanticField {
    RSourceSpan name_span;
    uint32_t name_intern_id;
    RTypeId type;
    uint32_t aggregate;
    uint32_t layout_index;
    RJsonFieldContract json;
    bool is_protected;
    bool is_closure_value_capture;
    /* R-INIT-0004 (L33): the declared `= initializer` of the field (node zero when absent),
       checked once in the module of its aggregate; an invalid one is not lowered again. */
    RAstRef initializer;
    bool initializer_checked;
    bool initializer_invalid;
} RSemanticField;

/* A checked object schema. Parent/end are indices relative to its aggregate's first entry. */
typedef struct RJsonSchemaField {
    uint32_t field;
    uint32_t parent;
    uint32_t end;
    bool embedded;
    bool collector;
    bool excluded;
} RJsonSchemaField;

typedef struct RSemanticVariant {
    RSourceSpan name_span;
    uint32_t name_intern_id;
    uint32_t aggregate;
    uint32_t declaration_index;
    uint64_t value;
    RTypeId payload_type;
    bool structured_payload;
} RSemanticVariant;

const RSemanticAggregate *r_semantic_tagged_enum(const RFrontendContext *context, RTypeId type);
/* R-STMT-0020: the fields bytes and tasks of std.alloc::limits; false for another type. */
bool r_semantic_budget_limits_fields(const RFrontendContext *context,
                                     RTypeId type,
                                     const RSemanticField **bytes,
                                     const RSemanticField **tasks);
const RSemanticVariant *
r_semantic_tagged_variant(const RFrontendContext *context, RTypeId type, uint64_t tag);

/* R-FUNC-0013: the declared form of the `this` parameter of a method. */
typedef enum RSemanticReceiverKind {
    R_SEMANTIC_RECEIVER_NONE = 0,
    R_SEMANTIC_RECEIVER_SHARED,
    R_SEMANTIC_RECEIVER_EXCLUSIVE,
    R_SEMANTIC_RECEIVER_VALUE
} RSemanticReceiverKind;

/* R-TYPE-0041: a trait declaration and its synthetic one-parameter `Self` schema. */
/* R-TYPE-0045: one associated type of a trait; its parameter follows Self in the schema. */
typedef struct RSemanticAssociatedType {
    RSourceSpan name_span;
    uint32_t name_intern_id;
    RTypeId parameter_type;
    /* R-TYPE-0045 (L16.3): the schema of the associated type's own parameters, or zero. */
    uint32_t parameter_schema;
} RSemanticAssociatedType;

typedef struct RSemanticTrait {
    RSourceSpan span;
    RSourceSpan name_span;
    uint32_t name_intern_id;
    RSourceId module_source;
    RAstNodeId declaration_ast;
    uint32_t generic_schema;
    uint32_t type_parameter_schema;
    uint32_t generic_origin;
    uint32_t first_generic_argument;
    uint32_t first_supertrait;
    uint32_t supertrait_count;
    RTypeId self_type;
    /* Nonzero for the synthetic trait of one callable signature (R-TYPE-0044). */
    RTypeId callable_signature;
    /* R-TYPE-0045: associated types, as a range of the context associated-type table. */
    uint32_t first_associated;
    uint32_t associated_count;
    /* R-TYPE-0050: associated constants, as a range of the context constant table. */
    uint32_t first_constant;
    uint32_t constant_count;
    /* R-TYPE-0046: a core trait the compiler declares; spelled `core::Name`. */
    bool is_core;
    bool is_protected;
    bool poisoned;
} RSemanticTrait;

/* R-TYPE-0042: one implementation of a trait for one target type. */
typedef struct RSemanticImpl {
    uint32_t trait;
    RTypeId target_type;
    uint32_t target_aggregate;
    RSourceSpan span;
    RSourceId module_source;
    RAstNodeId declaration_ast;
    uint32_t generic_schema;
    /* R-TYPE-0045: bound associated types, one per associated type of the trait, in the
       context binding table. */
    uint32_t first_binding;
    /* R-TYPE-0050: one initializer per associated constant of the trait; a zero node takes
       the trait's default. */
    uint32_t first_constant_binding;
    bool poisoned;
} RSemanticImpl;

/* R-TYPE-0050: one associated constant of a trait; `initializer` is its default. */
typedef struct RSemanticAssociatedConstant {
    RSourceSpan name_span;
    uint32_t name_intern_id;
    RTypeId type;
    RAstRef initializer;
} RSemanticAssociatedConstant;

/* R-NAME-0010: one entry of the method namespace of a nominal or scalar type. */
typedef struct RSemanticMethod {
    RTypeId owner_type;
    uint32_t owner_aggregate;
    uint32_t name_intern_id;
    RSymbolId function;
    uint32_t trait;
    uint32_t impl;
    bool is_default;
} RSemanticMethod;

typedef enum RSemanticSymbolKind {
    R_SEMANTIC_SYMBOL_INVALID = 0,
    R_SEMANTIC_SYMBOL_FUNCTION,
    R_SEMANTIC_SYMBOL_MODULE_CONSTANT,
    R_SEMANTIC_SYMBOL_MODULE_OBJECT,
    R_SEMANTIC_SYMBOL_PARAMETER,
    R_SEMANTIC_SYMBOL_LOCAL
} RSemanticSymbolKind;

typedef enum RSemanticObjectState {
    R_SEMANTIC_OBJECT_STATE_NOT_APPLICABLE = 0,
    R_SEMANTIC_OBJECT_STATE_INITIALIZED,
    R_SEMANTIC_OBJECT_STATE_STAGED_ASYNC_MOVE,
    R_SEMANTIC_OBJECT_STATE_MOVED,
    /* R-STMT-0018: a task a select statement may have left unconsumed; only group.cancel_all()
       or the group exit may resolve it. */
    R_SEMANTIC_OBJECT_STATE_SELECT_PENDING
} RSemanticObjectState;

typedef struct RSemanticSymbol {
    RSemanticSymbolKind kind;
    RSourceSpan name_span;
    uint32_t scope_start;
    uint32_t name_intern_id;
    RSourceId module_source;
    RTypeId type;
    RTypeId return_type;
    RTypeId opaque_result_contract;
    RTypeId async_task_type;
    RTypeId async_start_type;
    RTypeId throws_type;
    RTypeId effect_carrier_type;
    uint32_t first_parameter_type;
    uint32_t parameter_count;
    uint32_t return_borrow_parameter;
    uint64_t return_borrow_parameters_low;
    uint64_t return_borrow_parameters_high;
    /* Parameters of the mask above whose every contribution is what the storage they designate
       holds, never that storage (R-BORROW-0009); a call maps them to what the argument holds. */
    uint64_t return_borrow_held_low;
    uint64_t return_borrow_held_high;
    uint32_t return_projected_borrow_state;
    bool return_projections_seen;
    bool return_projections_complete;
    RHirNodeId hir_node;
    uint64_t integer_value;
    RSymbolId parent;
    /* Nonzero on the private, initially uninitialized local of an out parameter. */
    RSymbolId out_parameter;
    bool is_out_storage;
    RSymbolId borrow_origin;
    uint64_t borrow_referent;
    /* R-BORROW-0011: the field path below a single borrow origin that this borrow is restricted
       to, one byte per level (layout index plus one, outermost lowest); zero is the whole. */
    uint64_t borrow_path;
    uint64_t payload_borrow_referent;
    uint32_t borrow_origin_set;
    uint32_t projected_borrow_state;
    RAstNodeId declaration_ast;
    RHirNodeId pending_async_start;
    RSemanticObjectState object_state;
    bool is_protected;
    bool is_unsafe;
    bool is_async;
    bool is_scoped;
    bool is_extern_c;
    bool is_callback;
    /* R-FUNC-0019: independent, transitive resource contracts. */
    bool is_noalloc;
    bool is_nonblocking;
    bool is_must_use;
    /* R-FUNC-0021: the result may be ignored at a direct call in statement position. */
    bool is_discardable;
    /* R-TYPE-0045 (L16.1): a trait method whose associated result may view the storage its
       receiver designates; its implementations inherit the mark. */
    bool is_lending;
    /* R-FUNC-0024 (L19): a synchronous method whose call may be continued by further postfix
       suffixes without parentheses; implementations inherit the mark of their prototype. */
    bool is_chain;
    /* R-FUNC-0026 (L35): `@recursion(depth = N)`, the most activations at once on a thread;
       zero for a function without the attribute. */
    uint32_t recursion_depth;
    /* Imported C function of an extern "C" block (R-FFI-0013); never has an R body. */
    bool is_import;
    bool fenv_preserve;
    /* Reserved-class C identifier reached through the private bridge of R-CMAP-0026. */
    bool uses_bridge;
    /* The signature passes an R-declared @repr(C) struct: the C type at each such position
       comes from the block's ABI record (R-FFI-0021, R-FFI-0041); the slots list them in the
       order the C spellers visit them. */
    bool passes_r_struct;
    uint32_t first_c_import_slot;
    uint32_t c_import_slot_count;
    /* Imported C function declared with a trailing ... (R-FFI-0005). */
    bool is_variadic;
    /* R-FUNC-0018: the last parameter is `T... name`, a shared slice packed at the call. */
    bool is_variadic_slice;
    /* R-TYPE-0053 (L18.2): the last parameter is `T... name` of the pack T, a tuple of the
       trailing arguments of a call. */
    bool is_pack_variadic;
    RLinkKind link_kind;
    /* Logical @link name of the declaring block; zero selects the implicit C runtime entry. */
    uint32_t link_name_intern_id;
    /* One-based index into the context extern-block records; zero for non-imports. */
    uint32_t extern_block;
    /* One-based index into the link providers recorded by r_frontend_resolve_links. */
    uint32_t link_provider;
    RSourceSpan c_name_span;
    uint32_t c_name_intern_id;
    bool has_prototype;
    bool has_definition;
    bool hir_building;
    uint32_t generic_schema;
    RTypeId associated_type;
    uint32_t associated_operation;
    /* R-FUNC-0013: method state; the owner type is `associated_type`. */
    uint32_t receiver_kind;
    uint32_t trait;
    uint32_t impl;
    uint32_t method_name_intern_id;
    bool is_method;
    bool is_trait_prototype;
    /* Compiler-made prototype of a callable constraint: no AST, no HIR, no code. */
    bool is_synthetic;
    /* Body of a lambda: the one-based environment aggregate whose borrow is parameter 0. */
    uint32_t closure_aggregate;
    uint32_t generic_depth;
    RSymbolId generic_parent;
    RSymbolId generic_origin;
    uint32_t first_generic_argument;
    uint32_t format_recipe;
    uint32_t json_operation;
    RTypeId json_type;
    /* M32.6: the intern id of the schema text of a std.json::schema function. */
    uint32_t json_schema_text;
    bool is_static;
    bool is_thread_local;
    /* A local the compiler declares for a range-for (R-STMT-0014); named by symbol. */
    bool is_hidden_local;
    bool is_task_scope;
    /* The hidden group of a thread_scope: its scope_loans are the borrows its children hold
       until region completion (R-MEM-0015). */
    bool is_thread_scope;
    uint32_t scope_loans;
    RSymbolId task_scope;
    /* The scoped start whose loans a consuming await of this member releases (R-STMT-0017). */
    RHirNodeId scope_start_call;
    bool is_switch_borrow;
    bool is_switch_payload_binding;
    uint32_t pattern_place;
    bool borrow_origin_multiple;
    /* R-BORROW-0018, R-BORROW-0019: the storage a borrow or slice local may designate, as an
       origin set that only grows. Without `view_target_known` it may designate any origin. */
    RSymbolId view_target;
    uint32_t view_target_set;
    bool view_target_multiple;
    bool view_target_known;
    /* The storage designated by the borrow or slice payload that a switch payload binding
       borrows, such as the container of `std.list::get_mut`. */
    RSymbolId payload_view_target;
    uint32_t payload_view_target_set;
    bool payload_view_target_multiple;
    bool payload_view_target_known;
    /* R-BORROW-0018: the hidden region of what a parameter holds or designates, apart from the
       parameter's own storage. `parameter_content` on a parameter names the hidden symbol that
       stands for that region; `content_parameter` on that symbol names its parameter. */
    RSymbolId parameter_content;
    RSymbolId content_parameter;
    bool proven_non_null;
    bool is_format_temporary;
    bool signature_supported;
    bool poisoned;
    /* R-FUNC-0004: stable identity of a member of a function overload set. */
    uint32_t overload_identity;
    bool is_overloaded;
    /* R-FUNC-0023: translation-time evaluability derived from the body (RConstevalState), the
       first reason against it and the node or callee that carries that reason. */
    uint8_t consteval_state;
    uint8_t consteval_reason;
    RHirNodeId consteval_blocker;
    RSymbolId consteval_callee;
    /* R-EXPR-0032: a module constant whose initializer awaits translation-time evaluation, and
       one whose value is computed that way at all (a discovery pass reports it). */
    bool consteval_pending;
    bool consteval_resolving;
    bool consteval_computed;
    /* R-FUNC-0020: a constant expression read this local during translation (an array bound,
       a static condition), so its value is used even without an executable read. */
    bool translation_time_read;
} RSemanticSymbol;

typedef struct RErrorBorrowMapping {
    RSymbolId function;
    RTypeId error_type;
    uint32_t parameter;
    uint64_t parameters_low;
    uint64_t parameters_high;
    uint64_t held_low;
    uint64_t held_high;
    uint32_t projected_borrow_state;
    bool projections_seen;
    bool projections_complete;
} RErrorBorrowMapping;

/* R-BORROW-0019: the storage parameter `target` (1-based) designates receives the views the
   parameters of `sources` contribute, of `held` only what their designated storage holds. */
typedef struct RStoreBorrowMapping {
    RSymbolId function;
    uint32_t target;
    uint64_t sources_low;
    uint64_t sources_high;
    uint64_t held_low;
    uint64_t held_high;
} RStoreBorrowMapping;

typedef struct RFormatSpec {
    uint64_t width;
    uint64_t precision;
    uint32_t radix;
    bool uppercase;
    bool zero_fill;
    bool fixed;
    bool specified;
    bool integer_presentation;
} RFormatSpec;

typedef enum RFormatPartKind {
    R_FORMAT_TEXT,
    R_FORMAT_CAPTURE,
    R_FORMAT_POSITIONAL
} RFormatPartKind;

typedef struct RFormatPart {
    RSourceSpan span;
    RFormatPartKind kind;
    uint32_t index;
    RFormatSpec spec;
} RFormatPart;

typedef struct RFormatSchema {
    RSourceSpan span;
    RTypeId type;
    uint32_t first_part;
    uint32_t part_count;
    uint32_t capture_count;
} RFormatSchema;

typedef enum RFormatRecipeKind {
    R_FORMAT_CREATE,
    R_FORMAT_RENDER,
    R_FORMAT_IMMEDIATE,
    R_FORMAT_STRING_FROM_STR,
    R_FORMAT_STRING_AS_STR,
    R_FORMAT_STRING_AS_BYTES,
    R_FORMAT_STRING_INTO_BYTES
} RFormatRecipeKind;

typedef enum RStaticPredicateKind {
    R_STATIC_CONSTANT,
    R_STATIC_PROPERTY,
    R_STATIC_IDENTITY,
    R_STATIC_TRAIT,
    R_STATIC_AND,
    R_STATIC_OR,
    R_STATIC_NOT,
    /* R-META-0002: a condition leaf over constant operands, evaluated at translation time. */
    R_STATIC_VALUE
} RStaticPredicateKind;

typedef struct RStaticPredicate {
    RStaticPredicateKind kind;
    RSourceSpan span;
    RTypeId type;
    RTypeId other_type;
    uint32_t left;
    uint32_t right;
    uint32_t property;
    uint32_t trait;
    bool value;
    /* R_STATIC_VALUE: the condition leaf and, while it depends on generic parameters, its
       checked HIR in the generic definition (R-META-0003). */
    RAstRef expression;
    RHirNodeId condition;
} RStaticPredicate;

/* R-META-0003: a dependent constant condition decided for one closed instantiation. */
typedef struct RStaticChoice {
    uint32_t predicate;
    uint32_t schema;
    uint32_t arguments;
    int truth;
} RStaticChoice;

typedef struct RStaticFact {
    uint32_t predicate;
    bool value;
} RStaticFact;

typedef struct RFormatRecipe {
    RFormatRecipeKind kind;
    uint32_t schema;
} RFormatRecipe;

bool r_generic_type_is_dependent(const RFrontendContext *, RTypeId, uint32_t);
bool r_generic_function_is_open(const RFrontendContext *, const RSemanticSymbol *);
bool r_semantic_type_has_default(const RFrontendContext *context, RTypeId type_id);
bool r_semantic_type_is_direct_shared_borrow_output(const RFrontendContext *, RTypeId);

/* R-FUNC-0023: the translation-time evaluability of a function, derived from its body. */
typedef enum RConstevalState {
    R_CONSTEVAL_UNKNOWN = 0,
    R_CONSTEVAL_CHECKING,
    R_CONSTEVAL_EVALUABLE,
    R_CONSTEVAL_RUNTIME
} RConstevalState;

/*
 * A module-scope translation-time value (R-EXPR-0032) computed by a discovery pass before the
 * declarations that need it are checked. The key is the AST node of the constant declaration or
 * of the bound expression; an unresolved entry keeps the reason for its diagnostic.
 */
/* M32.6 (R-SLIB-JSON-0002): a schema hook that the schema operation `function` calls and
   inserts under `$defs` by the interned name. */
typedef struct RJsonSchemaHook {
    RSymbolId function;
    RSymbolId hook;
    uint32_t name_intern_id;
} RJsonSchemaHook;

typedef struct RConstevalValue {
    RSourceId source;
    RAstNodeId node;
    uint64_t bits;
    bool resolved;
    /* Interned diagnostic code, rule and message of an unresolved value; zero when resolved. */
    uint32_t code;
    uint32_t rule;
    uint32_t message;
    RSourceSpan span;
    /* Discovery only: an inline module-scope expression evaluated once bodies exist, and the
       type it is lowered to; both are meaningful only in the context that recorded them. */
    bool request;
    RTypeId expected;
    /* The expression is the condition leaf of a module `@if` (R-META-0002). */
    bool condition;
    /* R-TYPE-0047: the closed arguments of the generic instance a computed bound belongs to,
       spelled canonically; zero for a value without generic arguments. */
    uint32_t instance;
    /* Discovery only: the closed computed bound to evaluate once bodies exist. */
    RTypeId deferred;
} RConstevalValue;

/* R-TYPE-0047: a bound or constant argument in a generic declaration that calls a function over
   the declaration's parameters. Its checked HIR is evaluated for each closed instance; at module
   scope it is lowered once function signatures exist. */
typedef struct RComputedBound {
    /* Interned identity: the module and the expression's tokens with the declaration's
       parameters spelled by position, so equal formulas in one module are one type. */
    uint32_t key;
    RSourceId source;
    RAstNodeId node;
    uint32_t schema;
    /* The value type: usize for a bound, the parameter's type for a constant argument. */
    RTypeId type;
    RHirNodeId hir;
    bool lowering;
    bool failed;
} RComputedBound;

/* The value of one closed computed bound; `pending` while it is being evaluated. */
/* R-TYPE-0051: a type the program converts to an interface, with the tag that selects it. */
typedef struct RDynMember {
    RTypeId interface;
    RTypeId member;
    uint32_t tag;
} RDynMember;

/* R-TYPE-0054 (L28): one conversion of the safe function `function` to the function type
   `type`; the conversions to a type are the targets of its calls. */
typedef struct RFunctionValueTarget {
    RTypeId type;
    RSymbolId function;
    RSourceSpan span;
} RFunctionValueTarget;

/* R-TYPE-0051: the implementation a dispatching method application reaches for one member. */
typedef struct RDynTarget {
    RSymbolId dispatcher;
    RTypeId member;
    RSymbolId target;
    /* R-TYPE-0054 (L28): where the target of a function type was converted, or no source. */
    RSourceSpan span;
} RDynTarget;

typedef struct RComputedValue {
    RTypeId expression;
    uint64_t value;
    bool valid;
    bool pending;
} RComputedValue;

typedef enum RHirKind {
    R_HIR_INVALID = 0,
    R_HIR_PROGRAM,
    R_HIR_MODULE,
    R_HIR_FUNCTION,
    R_HIR_PARAMETER,
    R_HIR_BLOCK,
    R_HIR_LOCAL,
    R_HIR_RETURN,
    R_HIR_LITERAL,
    R_HIR_TYPE_QUERY,
    R_HIR_STRING_LITERAL,
    R_HIR_VALUE_SCOPE,
    R_HIR_ENUM_CONSTANT,
    R_HIR_DEFAULT_VALUE,
    R_HIR_NEW,
    R_HIR_FIELD_INIT,
    R_HIR_AGGREGATE_INIT,
    R_HIR_ARRAY_INIT,
    R_HIR_VARIANT,
    R_HIR_VARIANT_TAG,
    R_HIR_VARIANT_PAYLOAD,
    R_HIR_PLACE,
    R_HIR_FIELD_PLACE,
    R_HIR_INDEX_PLACE,
    R_HIR_DEREF_PLACE,
    R_HIR_BORROW,
    R_HIR_SLICE,
    R_HIR_LENGTH,
    R_HIR_LOAD,
    R_HIR_MOVE,
    R_HIR_DROP,
    R_HIR_UNARY,
    R_HIR_CAST,
    R_HIR_DISCARD,
    R_HIR_BINARY,
    R_HIR_CONDITIONAL,
    R_HIR_CALL,
    R_HIR_FUNCTION_ADDRESS,
    R_HIR_INDIRECT_CALL,
    R_HIR_ASYNC_START,
    R_HIR_AWAIT,
    R_HIR_TASK_SCOPE_ENTER,
    R_HIR_TASK_SCOPE_WAIT,
    R_HIR_TASK_SCOPE_CLOSE,
    R_HIR_STANDARD_CALL,
    R_HIR_ASSIGN,
    R_HIR_COMPOUND_ASSIGN,
    R_HIR_EXPRESSION_STATEMENT,
    R_HIR_IF,
    R_HIR_WHILE,
    R_HIR_FOR,
    R_HIR_BREAK,
    R_HIR_CONTINUE,
    R_HIR_TRY,
    R_HIR_CATCH,
    R_HIR_FINALLY,
    R_HIR_THROW,
    R_HIR_SWITCH,
    R_HIR_CASE,
    R_HIR_STATIC_IF,
    R_HIR_GENERIC_CONSTANT,
    /* R-TYPE-0053 (L18.4): a spread of a pack whose length an instantiation decides. The
       statement partitions the hidden object `symbol` into one piece per element; an element
       node stands for piece `integer_value`, and an argument node for the pieces from
       `integer_value` on. Only generic definitions contain them; a clone expands them. */
    R_HIR_PACK_PARTITION,
    R_HIR_PACK_ELEMENT,
    R_HIR_PACK_ARGS

} RHirKind;

typedef enum RStandardCallOperation {
    R_STANDARD_CALL_INVALID = 0,
    R_STANDARD_CALL_CORE_ASSUME,
    R_STANDARD_CALL_CORE_PANIC,
    R_STANDARD_CALL_CORE_HASH,
    R_STANDARD_CALL_CORE_KEY_EQUAL,
    R_STANDARD_CALL_CORE_SLICE_FROM_RAW_PARTS,
    R_STANDARD_CALL_CORE_SLICE_FROM_RAW_PARTS_MUT,
    R_STANDARD_CALL_CORE_VOLATILE_LOAD,
    R_STANDARD_CALL_CORE_VOLATILE_STORE,
    R_STANDARD_CALL_CORE_ADOPT,
    R_STANDARD_CALL_CORE_RELEASE,
    R_STANDARD_CALL_CORE_CHECKED_ADD,
    R_STANDARD_CALL_CORE_CHECKED_SUB,
    R_STANDARD_CALL_CORE_CHECKED_MUL,
    R_STANDARD_CALL_CORE_WRAPPING_ADD,
    R_STANDARD_CALL_CORE_WRAPPING_SUB,
    R_STANDARD_CALL_CORE_WRAPPING_MUL,
    R_STANDARD_CALL_CORE_SATURATING_ADD,
    R_STANDARD_CALL_CORE_SATURATING_SUB,
    R_STANDARD_CALL_CORE_SATURATING_MUL,
    R_STANDARD_CALL_CORE_ATOMIC_LOAD,
    R_STANDARD_CALL_CORE_ATOMIC_STORE,
    R_STANDARD_CALL_CORE_ATOMIC_EXCHANGE,
    R_STANDARD_CALL_CORE_ATOMIC_COMPARE_EXCHANGE,
    R_STANDARD_CALL_CORE_ATOMIC_FETCH_ADD,
    R_STANDARD_CALL_CORE_ATOMIC_FETCH_SUB,
    R_STANDARD_CALL_CORE_ATOMIC_FETCH_AND,
    R_STANDARD_CALL_CORE_ATOMIC_FETCH_OR,
    R_STANDARD_CALL_CORE_ATOMIC_FETCH_XOR,
    R_STANDARD_CALL_CORE_ATOMIC_IS_LOCK_FREE,
    /* R-REFL-0001..0002: runtime selections of static reflection (semantic/reflection.inc). */
    R_STANDARD_CALL_CORE_ENUM_NAME,
    R_STANDARD_CALL_CORE_ENUM_ORDINAL,
    R_STANDARD_CALL_CORE_ENUM_AT,
    R_STANDARD_CALL_CORE_ENUM_FROM_NAME,
    R_STANDARD_CALL_CORE_VARIANT_NAME,
    /* R-REFL-0004: folded to program strings while lowering; never reach HIR. */
    R_STANDARD_CALL_CORE_TARGET_NAME,
    R_STANDARD_CALL_CORE_PROFILE_NAME,
    /* R-REFL-0001..0003 over a generic parameter: folded when the instantiation clones the
       body (r_reflection_fold_clone); an open definition is never emitted. */
    R_STANDARD_CALL_CORE_REFLECT_ENUM_COUNT,
    R_STANDARD_CALL_CORE_REFLECT_ENUM_MIN,
    R_STANDARD_CALL_CORE_REFLECT_ENUM_MAX,
    R_STANDARD_CALL_CORE_REFLECT_VARIANT_COUNT,
    R_STANDARD_CALL_CORE_REFLECT_TYPE_NAME,
    R_STANDARD_CALL_CORE_REFLECT_FIELD_COUNT,
    R_STANDARD_CALL_CORE_REFLECT_FIELD_NAME,
    R_STANDARD_CALL_ARC_CLONE,
    R_STANDARD_CALL_RC_CLONE,
    R_STANDARD_CALL_ARC_CLONE_WEAK,
    R_STANDARD_CALL_ARC_DOWNGRADE,
    R_STANDARD_CALL_ARC_UPGRADE,
    R_STANDARD_CALL_ARC_GET_MUT,
    R_STANDARD_CALL_ARC_STRONG_COUNT,
    R_STANDARD_CALL_ARC_WEAK_COUNT,
    R_STANDARD_CALL_ARC_PTR_EQ,
    R_STANDARD_CALL_ARC_INTO_RAW,
    R_STANDARD_CALL_ARC_FROM_RAW,
    R_STANDARD_CALL_ARC_TRY_UNWRAP,
    R_STANDARD_CALL_RC_CLONE_WEAK,
    R_STANDARD_CALL_RC_DOWNGRADE,
    R_STANDARD_CALL_RC_UPGRADE,
    R_STANDARD_CALL_RC_GET_MUT,
    R_STANDARD_CALL_RC_STRONG_COUNT,
    R_STANDARD_CALL_RC_WEAK_COUNT,
    R_STANDARD_CALL_RC_PTR_EQ,
    R_STANDARD_CALL_RC_INTO_RAW,
    R_STANDARD_CALL_RC_FROM_RAW,
    R_STANDARD_CALL_RC_TRY_UNWRAP,
    R_STANDARD_CALL_ASYNC_CANCEL,
    R_STANDARD_CALL_ASYNC_DETACH,
    R_STANDARD_CALL_ASYNC_BLOCKING,
    R_STANDARD_CALL_THREAD_SPAWN,
    R_STANDARD_CALL_THREAD_SPAWN_SCOPED,
    R_STANDARD_CALL_THREAD_JOIN,
    R_STANDARD_CALL_THREAD_DETACH,
    R_STANDARD_CALL_SYNC_ONCE_NEW,
    R_STANDARD_CALL_SYNC_ONCE_LOCK,
    R_STANDARD_CALL_SYNC_CALL_ONCE,
    R_STANDARD_CALL_SYNC_CALL_ONCE_FORCE,
    R_STANDARD_CALL_SYNC_GET_OR_INIT,
    R_STANDARD_CALL_SYNC_CHANNEL,
    R_STANDARD_CALL_SYNC_SYNC_CHANNEL,
    R_STANDARD_CALL_SYNC_SENDER,
    R_STANDARD_CALL_SYNC_SYNC_SENDER,
    R_STANDARD_CALL_SYNC_CLONE_SENDER,
    R_STANDARD_CALL_SYNC_CLONE_SYNC_SENDER,
    R_STANDARD_CALL_SYNC_SYNC_RECEIVER,
    R_STANDARD_CALL_SYNC_SEND,
    R_STANDARD_CALL_SYNC_SYNC_SEND,
    R_STANDARD_CALL_SYNC_TRY_SEND,
    R_STANDARD_CALL_SYNC_RECV,
    R_STANDARD_CALL_SYNC_TRY_RECV,
    R_STANDARD_CALL_SYNC_GET,
    R_STANDARD_CALL_SYNC_SET,
    R_STANDARD_CALL_SYNC_RECEIVER,
    R_STANDARD_CALL_SYNC_BARRIER_NEW,
    R_STANDARD_CALL_SYNC_BARRIER_WAIT,
    R_STANDARD_CALL_SYNC_CONDVAR_NEW,
    R_STANDARD_CALL_SYNC_NOTIFY_ONE,
    R_STANDARD_CALL_SYNC_NOTIFY_ALL,
    R_STANDARD_CALL_SYNC_MUTEX_NEW,
    R_STANDARD_CALL_SYNC_RWLOCK_NEW,
    R_STANDARD_CALL_SYNC_LOCK,
    R_STANDARD_CALL_SYNC_TRY_LOCK,
    R_STANDARD_CALL_SYNC_READ,
    R_STANDARD_CALL_SYNC_TRY_READ,
    R_STANDARD_CALL_SYNC_WRITE,
    R_STANDARD_CALL_SYNC_TRY_WRITE,
    R_STANDARD_CALL_SYNC_MUTEX_GUARD_REF,
    R_STANDARD_CALL_SYNC_MUTEX_GUARD_MUT,
    R_STANDARD_CALL_SYNC_RW_READ_GUARD_REF,
    R_STANDARD_CALL_SYNC_RW_WRITE_GUARD_REF,
    R_STANDARD_CALL_SYNC_RW_WRITE_GUARD_MUT,
    R_STANDARD_CALL_SYNC_UNLOCK,
    R_STANDARD_CALL_SYNC_WAIT,

    R_STANDARD_CALL_FORMAT_CREATE,
    R_STANDARD_CALL_FORMAT_WITH_CAPACITY,
    R_STANDARD_CALL_FORMAT_AS_STR,
    R_STANDARD_CALL_FORMAT_CLEAR,
    R_STANDARD_CALL_FORMAT_FINISH,
    R_STANDARD_CALL_FORMAT_APPEND_STR,
    R_STANDARD_CALL_FORMAT_APPEND_CHAR,
    R_STANDARD_CALL_FORMAT_APPEND_F32,
    R_STANDARD_CALL_FORMAT_APPEND_F64,
    R_STANDARD_CALL_FORMAT_APPEND_C_FLOAT,
    R_STANDARD_CALL_FORMAT_APPEND_C_DOUBLE,
    R_STANDARD_CALL_FORMAT_APPEND_C_LONG_DOUBLE,
    R_STANDARD_CALL_FORMAT_APPEND_INTEGER,
    R_STANDARD_CALL_ALLOC_TRY_NEW,
    R_STANDARD_CALL_ALLOC_INTO_VALUE,
    R_STANDARD_CALL_ARRAY_CAPACITY,
    R_STANDARD_CALL_ARRAY_RESERVE,
    R_STANDARD_CALL_ARRAY_PUSH,
    R_STANDARD_CALL_ARRAY_POP,
    R_STANDARD_CALL_ARRAY_REMOVE,
    R_STANDARD_CALL_ARRAY_GET,
    R_STANDARD_CALL_ARRAY_GET_MUT,
    R_STANDARD_CALL_ARRAY_CLEAR,
    R_STANDARD_CALL_ARRAY_WITH_CAPACITY,
    R_STANDARD_CALL_LIST_CREATE,
    R_STANDARD_CALL_LIST_PUSH_FRONT,
    R_STANDARD_CALL_LIST_PUSH_BACK,
    R_STANDARD_CALL_LIST_INSERT_BEFORE,
    R_STANDARD_CALL_LIST_INSERT_AFTER,
    R_STANDARD_CALL_LIST_FRONT,
    R_STANDARD_CALL_LIST_BACK,
    R_STANDARD_CALL_LIST_FRONT_MUT,
    R_STANDARD_CALL_LIST_BACK_MUT,
    R_STANDARD_CALL_LIST_GET,
    R_STANDARD_CALL_LIST_GET_MUT,
    R_STANDARD_CALL_LIST_REMOVE,
    R_STANDARD_CALL_LIST_POP_FRONT,
    R_STANDARD_CALL_LIST_POP_BACK,
    R_STANDARD_CALL_LIST_CLEAR,
    R_STANDARD_CALL_LIST_ITER,
    R_STANDARD_CALL_LIST_NEXT,
    R_STANDARD_CALL_DICT_CREATE,
    R_STANDARD_CALL_DICT_WITH_CAPACITY,
    R_STANDARD_CALL_DICT_RESERVE,
    R_STANDARD_CALL_DICT_INSERT,
    R_STANDARD_CALL_DICT_CONTAINS,
    R_STANDARD_CALL_DICT_GET,
    R_STANDARD_CALL_DICT_GET_MUT,
    R_STANDARD_CALL_DICT_REMOVE,
    R_STANDARD_CALL_DICT_CLEAR,
    R_STANDARD_CALL_DICT_ITER,
    R_STANDARD_CALL_DICT_NEXT,
    R_STANDARD_CALL_BYTES_WITH_CAPACITY,
    R_STANDARD_CALL_BYTES_APPEND,
    R_STANDARD_CALL_BYTES_APPEND_U8,
    R_STANDARD_CALL_BYTES_APPEND_U16_LE,
    R_STANDARD_CALL_BYTES_APPEND_U32_LE,
    R_STANDARD_CALL_BYTES_APPEND_U64_LE,
    R_STANDARD_CALL_BYTES_COPY,
    R_STANDARD_CALL_BYTES_COPY_WITHIN,
    R_STANDARD_CALL_BYTES_FILL,
    R_STANDARD_CALL_BYTES_FIND,
    R_STANDARD_CALL_BYTES_FIND_SLICE,
    R_STANDARD_CALL_BYTES_STARTS_WITH,
    R_STANDARD_CALL_BYTES_ENDS_WITH,
    R_STANDARD_CALL_HASH_CRC32,
    R_STANDARD_CALL_HASH_MD5,
    R_STANDARD_CALL_HASH_SHA1,
    R_STANDARD_CALL_HASH_SHA256,
    R_STANDARD_CALL_HASH_SHA512,
    R_STANDARD_CALL_UTF8_IS_VALID,
    R_STANDARD_CALL_UTF8_VALIDATE,
    R_STANDARD_CALL_BITS_READ,
    R_STANDARD_CALL_BITS_ALIGN_BYTE,
    R_STANDARD_CALL_FS_PATH_FROM_UTF8,
    R_STANDARD_CALL_FS_PATH_FROM_UTF8_BYTES,
    R_STANDARD_CALL_FS_PATH_CLONE,
    R_STANDARD_CALL_FS_PATH_TO_UTF8,
    R_STANDARD_CALL_FS_PATH_JOIN,
    R_STANDARD_CALL_FS_PATH_IS_ABSOLUTE,
    R_STANDARD_CALL_FS_READ_FILE,
    R_STANDARD_CALL_FS_OPEN_DIRECTORY,
    R_STANDARD_CALL_FS_OPEN_FILE,
    R_STANDARD_CALL_FS_CREATE_DIRECTORY_BENEATH,
    R_STANDARD_CALL_FS_WRITE_FILE_ATOMIC_NO_REPLACE_BENEATH,
    R_STANDARD_CALL_FS_AS_ERROR,
    R_STANDARD_CALL_FS_FILE_METADATA,
    R_STANDARD_CALL_FS_CLOSE_DIRECTORY,
    R_STANDARD_CALL_FS_CLOSE_FILE,
    R_STANDARD_CALL_FS_CREATE_DIRECTORY,
    R_STANDARD_CALL_FS_FLUSH,
    R_STANDARD_CALL_FS_ITERATE,
    R_STANDARD_CALL_FS_METADATA,
    R_STANDARD_CALL_FS_METADATA_BENEATH,
    R_STANDARD_CALL_FS_NEXT,
    R_STANDARD_CALL_FS_OPEN_DIRECTORY_BENEATH,
    R_STANDARD_CALL_FS_OPEN_FILE_BENEATH,
    R_STANDARD_CALL_FS_READ,
    R_STANDARD_CALL_FS_READ_FILE_BENEATH,
    R_STANDARD_CALL_FS_REMOVE_DIRECTORY_BENEATH,
    R_STANDARD_CALL_FS_REMOVE_FILE_BENEATH,
    R_STANDARD_CALL_FS_RENAME_BENEATH,
    R_STANDARD_CALL_FS_SEEK,
    R_STANDARD_CALL_FS_WRITE,
    R_STANDARD_CALL_FS_WRITE_ALL,
    R_STANDARD_CALL_FS_WRITE_FILE_ATOMIC_NO_REPLACE,
    R_STANDARD_CALL_FS_SYNC,
    R_STANDARD_CALL_FS_READ_AT,
    R_STANDARD_CALL_FS_WRITE_ALL_AT,
    R_STANDARD_CALL_FS_TRY_LOCK,
    R_STANDARD_CALL_FS_LOCK,
    R_STANDARD_CALL_FS_UNLOCK,
    R_STANDARD_CALL_IO_STDIN,
    R_STANDARD_CALL_IO_STDOUT,
    R_STANDARD_CALL_IO_STDERR,
    R_STANDARD_CALL_IO_READ,
    R_STANDARD_CALL_IO_FLUSH,
    R_STANDARD_CALL_IO_CLOSE_INPUT,
    R_STANDARD_CALL_IO_CLOSE_OUTPUT,
    R_STANDARD_CALL_IO_WRITE,
    R_STANDARD_CALL_IO_WRITE_ALL,
    R_STANDARD_CALL_IO_WRITE_SHARED,
    R_STANDARD_CALL_IO_AS_ERROR,
    R_STANDARD_CALL_CONVERT_PARSE,
    R_STANDARD_CALL_CONVERT_CHECKED,
    R_STANDARD_CALL_C_CHECKED,
    R_STANDARD_CALL_C_LINK_AVAILABLE,
    R_STANDARD_CALL_ARRAY_CREATE,
    R_STANDARD_CALL_ALLOC_BYTES,
    R_STANDARD_CALL_BYTES_EQUAL,
    R_STANDARD_CALL_BYTES_COMPARE,
    R_STANDARD_CALL_C_TARGET,
    R_STANDARD_CALL_C_STRING_FROM_STR,
    R_STANDARD_CALL_C_STRING_AS_SLICE,
    R_STANDARD_CALL_C_STRING_AS_PTR,
    R_STANDARD_CALL_C_VALIDATE_UTF8,
    R_STANDARD_CALL_C_COPY_UTF8,
    R_STANDARD_CALL_C_ATTACH_THREAD,
    R_STANDARD_CALL_C_DETACH_THREAD,
    R_STANDARD_CALL_C_HANDLE_POINTER,
    R_STANDARD_CALL_C_RELEASE_HANDLE,
    R_STANDARD_CALL_C_ADOPT_HANDLE,
    R_STANDARD_CALL_ENV_ARGUMENTS,
    R_STANDARD_CALL_ENV_VARIABLES,
    R_STANDARD_CALL_ENV_GET,
    R_STANDARD_CALL_ENV_SET,
    R_STANDARD_CALL_ENV_REMOVE,
    R_STANDARD_CALL_NET_PARSE_IP,
    R_STANDARD_CALL_NET_FORMAT_IP,
    R_STANDARD_CALL_NET_TCP_LOCAL_ADDRESS,
    R_STANDARD_CALL_NET_TCP_LISTENER_LOCAL_ADDRESS,
    R_STANDARD_CALL_NET_TCP_PEER_ADDRESS,
    R_STANDARD_CALL_NET_UDP_LOCAL_ADDRESS,
    R_STANDARD_CALL_NET_TCP_GET_OPTIONS,
    R_STANDARD_CALL_NET_TCP_SET_OPTIONS,
    R_STANDARD_CALL_NET_UDP_GET_OPTIONS,
    R_STANDARD_CALL_NET_UDP_SET_OPTIONS,
    R_STANDARD_CALL_NET_UDP_JOIN_MULTICAST,
    R_STANDARD_CALL_NET_UDP_LEAVE_MULTICAST,
    /* R-SLIB-NET-0016 (M22): the credentials of a Unix-domain peer. */
    R_STANDARD_CALL_NET_UNIX_PEER_CREDENTIALS,
    R_STANDARD_CALL_NET_RESOLVE,
    R_STANDARD_CALL_NET_TCP_LISTEN,
    R_STANDARD_CALL_NET_TCP_ACCEPT,
    R_STANDARD_CALL_NET_TCP_CONNECT,
    R_STANDARD_CALL_NET_TCP_READ,
    R_STANDARD_CALL_NET_TCP_WRITE,
    R_STANDARD_CALL_NET_TCP_WRITE_ALL,
    R_STANDARD_CALL_NET_TCP_SHUTDOWN,
    R_STANDARD_CALL_NET_TCP_CLOSE,
    R_STANDARD_CALL_NET_TCP_LISTENER_CLOSE,
    R_STANDARD_CALL_NET_UDP_BIND,
    R_STANDARD_CALL_NET_UDP_SEND_TO,
    R_STANDARD_CALL_NET_UDP_RECEIVE_FROM,
    R_STANDARD_CALL_NET_UDP_CLOSE,
    /* R-SLIB-NET-0014, R-SLIB-NET-0015, R-SLIB-NET-0017 (M22): Unix-domain sockets. */
    R_STANDARD_CALL_NET_UNIX_LISTEN,
    R_STANDARD_CALL_NET_UNIX_ACCEPT,
    R_STANDARD_CALL_NET_UNIX_CONNECT,
    R_STANDARD_CALL_NET_UNIX_SHUTDOWN,
    R_STANDARD_CALL_NET_UNIX_CLOSE,
    R_STANDARD_CALL_NET_UNIX_LISTENER_CLOSE,
    R_STANDARD_CALL_NET_UNIX_DATAGRAM_BIND,
    R_STANDARD_CALL_NET_UNIX_DATAGRAM_CONNECT,
    R_STANDARD_CALL_NET_UNIX_DATAGRAM_CLOSE,
    R_STANDARD_CALL_PROCESS_COMMAND_CREATE,
    R_STANDARD_CALL_PROCESS_ARG,
    R_STANDARD_CALL_PROCESS_ENVIRONMENT,
    R_STANDARD_CALL_PROCESS_REMOVE_ENVIRONMENT,
    R_STANDARD_CALL_PROCESS_CLEAR_ENVIRONMENT,
    R_STANDARD_CALL_PROCESS_WORKING_DIRECTORY,
    R_STANDARD_CALL_PROCESS_SET_STDIO,
    R_STANDARD_CALL_PROCESS_TAKE_STDIN,
    R_STANDARD_CALL_PROCESS_TAKE_STDOUT,
    R_STANDARD_CALL_PROCESS_TAKE_STDERR,
    R_STANDARD_CALL_PROCESS_ID,
    R_STANDARD_CALL_PROCESS_EXIT,
    R_STANDARD_CALL_PROCESS_ABORT,
    R_STANDARD_CALL_PROCESS_SPAWN,
    R_STANDARD_CALL_PROCESS_WAIT,
    R_STANDARD_CALL_PROCESS_TERMINATE,
    R_STANDARD_CALL_SIGNAL_LISTEN,
    R_STANDARD_CALL_SIGNAL_RAISE,
    R_STANDARD_CALL_SIGNAL_NEXT,
    R_STANDARD_CALL_ERROR_NAME,
    R_STANDARD_CALL_ERROR_DIAGNOSTIC,
    R_STANDARD_CALL_THREAD_CURRENT,
    R_STANDARD_CALL_THREAD_CLONE,
    R_STANDARD_CALL_THREAD_UNPARK,
    R_STANDARD_CALL_THREAD_PARK,
    R_STANDARD_CALL_THREAD_YIELD_NOW,
    R_STANDARD_CALL_THREAD_SLEEP_NANOSECONDS,
    R_STANDARD_CALL_THREAD_PANIC_CATEGORY,
    R_STANDARD_CALL_THREAD_PANIC_TEXT,
    R_STANDARD_CALL_ERROR_ERASURE,
    R_STANDARD_CALL_MATH_OPERATION,
    R_STANDARD_CALL_MATH_ABS_F64,
    R_STANDARD_CALL_MATH_SIN_F64,
    R_STANDARD_CALL_STRING_CREATE,
    R_STANDARD_CALL_STRING_FROM_STR,
    R_STANDARD_CALL_STRING_FROM_BYTES,
    R_STANDARD_CALL_STRING_AS_STR,
    R_STANDARD_CALL_STRING_AS_BYTES,
    R_STANDARD_CALL_STRING_INTO_BYTES,
    R_STANDARD_CALL_STRING_LEN,
    R_STANDARD_CALL_STRING_CAPACITY,
    R_STANDARD_CALL_STRING_CLEAR,
    R_STANDARD_CALL_STRING_WITH_CAPACITY,
    R_STANDARD_CALL_STRING_FROM_UTF8,
    R_STANDARD_CALL_STRING_RESERVE,
    R_STANDARD_CALL_STRING_APPEND_STR,
    R_STANDARD_CALL_STRING_APPEND_UTF8,
    R_STANDARD_CALL_STRING_PUSH_SCALAR,
    R_STANDARD_CALL_STRING_TRUNCATE,
    R_STANDARD_CALL_TIME_DURATION_FROM_SECONDS,
    R_STANDARD_CALL_TIME_DURATION_SECONDS,
    R_STANDARD_CALL_TIME_DURATION_NANOSECONDS,
    R_STANDARD_CALL_TIME_DURATION_COMPARE,
    R_STANDARD_CALL_TIME_DURATION_FROM_PARTS,
    R_STANDARD_CALL_TIME_DURATION_ADD,
    R_STANDARD_CALL_TIME_DURATION_SUB,
    R_STANDARD_CALL_TIME_DURATION_MULTIPLY,
    R_STANDARD_CALL_TIME_MONOTONIC_NOW,
    R_STANDARD_CALL_TIME_SYSTEM_NOW,
    R_STANDARD_CALL_TIME_INSTANT_ADD,
    R_STANDARD_CALL_TIME_INSTANT_DURATION,
    R_STANDARD_CALL_TIME_SYSTEM_ADD,
    R_STANDARD_CALL_TIME_TO_UTC,
    R_STANDARD_CALL_TIME_FROM_UTC,
    R_STANDARD_CALL_TIME_AS_ERROR,
    R_STANDARD_CALL_TIME_SLEEP_FOR,
    R_STANDARD_CALL_TIME_SLEEP_UNTIL,
    R_STANDARD_CALL_SECRET_WITH_LENGTH,
    R_STANDARD_CALL_SECRET_FROM_BYTES,
    R_STANDARD_CALL_SECRET_LEN,
    R_STANDARD_CALL_SECRET_AS_SLICE,
    R_STANDARD_CALL_SECRET_AS_SLICE_MUT,
    R_STANDARD_CALL_SECRET_ZEROIZE,
    R_STANDARD_CALL_SECRET_CONSTANT_TIME_EQUAL,
    /* R-SLIB-RANDOM-0001 (M20): bytes of the operating-system generator into a mutable byte
       view; lowered like std.secret::zeroize, whose byte-view calls it follows. */
    R_STANDARD_CALL_RANDOM_FILL,
    R_STANDARD_CALL_CORE_REPLACE,
    R_STANDARD_CALL_CORE_TAKE,
    /* R-OWN-0019, R-OWN-0020 (L26): exchange of two places and a checked copy of a value. */
    R_STANDARD_CALL_CORE_SWAP,
    R_STANDARD_CALL_CORE_CLONE,
    /* R-TYPE-0046 (L32): the text of a core::Format value as a new string, or appended to a
       builder, by its implementation or its standard formatting. */
    R_STANDARD_CALL_CORE_FORMAT_RENDER,
    R_STANDARD_CALL_CORE_FORMAT_APPEND,
    /* Scoped operations (Library R-SLIB-ASYNC-0012): a caller view loaned by the task group. */
    R_STANDARD_CALL_IO_READ_INTO,
    R_STANDARD_CALL_IO_WRITE_FROM,
    R_STANDARD_CALL_IO_WRITE_ALL_FROM,
    R_STANDARD_CALL_FS_READ_INTO,
    R_STANDARD_CALL_FS_WRITE_FROM,
    R_STANDARD_CALL_FS_WRITE_ALL_FROM,
    R_STANDARD_CALL_FS_READ_AT_INTO,
    R_STANDARD_CALL_FS_WRITE_ALL_AT_FROM,
    R_STANDARD_CALL_NET_TCP_READ_INTO,
    R_STANDARD_CALL_NET_TCP_WRITE_FROM,
    R_STANDARD_CALL_NET_TCP_WRITE_ALL_FROM,
    R_STANDARD_CALL_NET_UDP_SEND_FROM,
    R_STANDARD_CALL_NET_UDP_RECEIVE_INTO,
    R_STANDARD_CALL_NET_UNIX_READ_INTO,
    R_STANDARD_CALL_NET_UNIX_WRITE_FROM,
    R_STANDARD_CALL_NET_UNIX_WRITE_ALL_FROM,
    R_STANDARD_CALL_NET_UNIX_SEND_FROM,
    R_STANDARD_CALL_NET_UNIX_RECEIVE_INTO,
    /* R-STMT-0019: compiler-internal entry and exit of a deadline block; no source spelling. */
    R_STANDARD_CALL_ASYNC_DEADLINE_ENTER,
    R_STANDARD_CALL_ASYNC_DEADLINE_LEAVE,
    /* R-STMT-0020: compiler-internal entry and exit of a budget block; no source spelling. */
    R_STANDARD_CALL_ASYNC_BUDGET_ENTER,
    R_STANDARD_CALL_ASYNC_BUDGET_LEAVE,
    /* R-LIB-0016 (L24): an asynchronous receive from a channel. */
    R_STANDARD_CALL_SYNC_RECEIVE,
    /* R-SLIB-ASYNC-0013..0016, R-LIB-0016 (L30): asynchronous locks, semaphore, notify,
       broadcast and channel reservations; compiler/source/standard_async_sync.h describes each. */
    R_STANDARD_CALL_ASYNC_MUTEX_NEW,
    R_STANDARD_CALL_ASYNC_CLONE_MUTEX,
    R_STANDARD_CALL_ASYNC_LOCK,
    R_STANDARD_CALL_ASYNC_TRY_LOCK,
    R_STANDARD_CALL_ASYNC_MUTEX_GUARD_REF,
    R_STANDARD_CALL_ASYNC_MUTEX_GUARD_MUT,
    R_STANDARD_CALL_ASYNC_UNLOCK,
    R_STANDARD_CALL_ASYNC_RWLOCK_NEW,
    R_STANDARD_CALL_ASYNC_CLONE_RW_LOCK,
    R_STANDARD_CALL_ASYNC_READ,
    R_STANDARD_CALL_ASYNC_WRITE,
    R_STANDARD_CALL_ASYNC_TRY_READ,
    R_STANDARD_CALL_ASYNC_TRY_WRITE,
    R_STANDARD_CALL_ASYNC_RW_READ_GUARD_REF,
    R_STANDARD_CALL_ASYNC_RW_WRITE_GUARD_REF,
    R_STANDARD_CALL_ASYNC_RW_WRITE_GUARD_MUT,
    R_STANDARD_CALL_ASYNC_SEMAPHORE_NEW,
    R_STANDARD_CALL_ASYNC_CLONE_SEMAPHORE,
    R_STANDARD_CALL_ASYNC_ACQUIRE,
    R_STANDARD_CALL_ASYNC_TRY_ACQUIRE,
    R_STANDARD_CALL_ASYNC_RELEASE,
    R_STANDARD_CALL_ASYNC_ADD_PERMITS,
    R_STANDARD_CALL_ASYNC_AVAILABLE_PERMITS,
    R_STANDARD_CALL_ASYNC_NOTIFY_NEW,
    R_STANDARD_CALL_ASYNC_CLONE_NOTIFY,
    R_STANDARD_CALL_ASYNC_NOTIFY_ONE,
    R_STANDARD_CALL_ASYNC_NOTIFY_ALL,
    R_STANDARD_CALL_ASYNC_NOTIFIED,
    R_STANDARD_CALL_ASYNC_BROADCAST,
    R_STANDARD_CALL_ASYNC_CLONE_BROADCAST,
    R_STANDARD_CALL_ASYNC_SUBSCRIBE,
    R_STANDARD_CALL_ASYNC_PUBLISH,
    R_STANDARD_CALL_ASYNC_BROADCAST_RECEIVE,
    /* R-SLIB-ASYNC-0018 (M23): the identifier of the running task. */
    R_STANDARD_CALL_ASYNC_TASK_ID,
    /* R-SLIB-TEST-0003 (M24): the allocation failures of tests. */
    R_STANDARD_CALL_TEST_ALLOCATION_ATTEMPTS,
    R_STANDARD_CALL_TEST_FAIL_ALLOCATION_AT,
    R_STANDARD_CALL_SYNC_RESERVE,
    R_STANDARD_CALL_SYNC_TRY_RESERVE,
    R_STANDARD_CALL_SYNC_SEND_PERMIT,
    /* R-FUNC-0026 (L35): compiler-internal entry and exit of an activation of a function with
       @recursion; the node names the function and the entry carries the depth. */
    R_STANDARD_CALL_CORE_RECURSION_ENTER,
    R_STANDARD_CALL_CORE_RECURSION_LEAVE,
    /* R-UNSAFE-0008 (L38): the anchored raw-parts slices name source calls only; they lower to
       the raw-parts operations above, and the node marks its anchor (integer_value 1). */
    R_STANDARD_CALL_CORE_SLICE_FROM_RAW_PARTS_IN,
    R_STANDARD_CALL_CORE_SLICE_FROM_RAW_PARTS_IN_MUT,
    /* R-LIB-0019 (P4.2): std.array::filled(length, value), length copies of one Copy value in
       one allocation; the children are the length and the value, auxiliary_type the effect
       carrier of the array and std.alloc::alloc_error. */
    R_STANDARD_CALL_ARRAY_FILLED,
    /* R-SLIB-ASYNC-0020 (L39): std.async::join names the operand of an await only; that await
       (HIR integer_value 1, MIR integer_value 1) observes a panic of the task into a
       std.thread::join_result instead of continuing it. */
    R_STANDARD_CALL_ASYNC_JOIN,
    /* R-REFL-0004 (M44.3): core::location(), folded to the program string `module.path:line` of
       the call while lowering, like target_name and profile_name; never reaches HIR. */
    R_STANDARD_CALL_CORE_LOCATION,
    /* R-REFL-0005 (L42): the arguments of a user attribute on a type, a field or an enumerator as
       o<A>, and R-REFL-0003 core::field_name with an index known only at run time. They stay
       standard calls to the C17 emitter, which selects from translation-time tables. */
    R_STANDARD_CALL_CORE_TYPE_ATTRIBUTE,
    R_STANDARD_CALL_CORE_FIELD_ATTRIBUTE,
    R_STANDARD_CALL_CORE_VARIANT_ATTRIBUTE,
    R_STANDARD_CALL_CORE_FIELD_NAME_AT
} RStandardCallOperation;

typedef struct RStandardMathOperationDescriptor {
    const char *name;
    const char *c_symbol;
    const char *result_type;
    const char *parameter0_type;
    const char *parameter1_type;
    const char *rule_id;
    uint32_t parameter_count;
    bool checked;
} RStandardMathOperationDescriptor;

size_t r_standard_math_operation_count(void);
const RStandardMathOperationDescriptor *r_standard_math_operation(uint64_t operation_id);

typedef struct RStandardErrorErasureDescriptor {
    const char *module_name;
    const char *operation_name;
    const char *qualified_name;
    const char *source_type;
    const char *c_symbol;
    const char *library_target;
} RStandardErrorErasureDescriptor;

size_t r_standard_error_erasure_count(void);
const RStandardErrorErasureDescriptor *r_standard_error_erasure(uint64_t operation_id);

typedef enum RHirCasePatternKind {
    R_HIR_CASE_PATTERN_INVALID = 0,
    R_HIR_CASE_PATTERN_CONSTANT,
    R_HIR_CASE_PATTERN_VARIANT,
    R_HIR_CASE_PATTERN_DEFAULT
} RHirCasePatternKind;

typedef struct RHirNode {
    RHirKind kind;
    RSourceSpan span;
    RTypeId type;
    RTypeId auxiliary_type;
    RTypeId runtime_type;
    RSymbolId symbol;
    RSymbolId task_scope;
    RSymbolId borrow_origin;
    uint64_t borrow_referent;
    uint32_t borrow_origin_set;
    uint32_t projected_borrow_state;
    RTokenKind operation;
    RStandardCallOperation standard_operation;
    uint32_t source_operation;
    uint32_t first_child;
    uint32_t child_count;
    uint32_t first_effect_exit;
    uint32_t effect_exit_count;
    /* L39 (R-ERR-0005): the block of drops that leaves the function when this node panics under
       the unwind strategy; consecutive nodes with the same live objects share one block. */
    RHirNodeId panic_cleanup;
    uint64_t integer_value;
    uint32_t aggregate_member;
    /* BREAK/CONTINUE of a labeled jump (R-STMT-0004): n targets the n-th innermost enclosing
       loop, whatever loops and switches lie between; 0 is the ordinary innermost target. */
    uint32_t loop_target;
    RHirCasePatternKind case_pattern;
    bool is_place;
    bool is_move;
    bool replaces_initialized;
    bool borrow_origin_multiple;
    bool immediate_await;        /* ASYNC_START consumed by the await that directly follows it. */
    bool function_item_receiver; /* Erase the callable receiver after static trait binding. */
    bool out_commit;             /* Success-only publication, after all return finalizers. */
    bool out_argument;           /* A reserved destination; never an ordinary input borrow. */
    RSymbolId out_destination;   /* Caller binding updated by a successful output assignment. */
    RHirNodeId out_place;        /* Original destination, before staging its address. */
    RSymbolId deref_local;       /* Async DEREF_PLACE of a borrow value: its hidden frame local. */
} RHirNode;

/*
 * One record describes the ownership cleanup required by one checked-error edge of an
 * effectful HIR expression. cleanup_block is a hidden R_HIR_BLOCK containing the implicit drops
 * for that exact error type in reverse declaration order. The error payload is initialized before
 * this block is entered; MIR routes through the applicable active finally blocks afterwards.
 */
typedef struct RHirEffectExit {
    RTypeId error_type;
    RHirNodeId cleanup_block;
    /* A named catch may match an abstract error set only after substitution. */
    RTypeId condition_set;
    /* For such a conditional exit, one plus the index of the first clause of its try statement:
       a larger value is an inner try (R-ERR-0003, L21.2). */
    uint32_t catch_group;
} RHirEffectExit;

typedef enum RMirInstructionKind {
    R_MIR_INSTRUCTION_INVALID = 0,
    R_MIR_INSTRUCTION_PARAMETER,
    R_MIR_INSTRUCTION_LOCAL,
    R_MIR_INSTRUCTION_CONSTANT,
    R_MIR_INSTRUCTION_TYPE_QUERY,
    R_MIR_INSTRUCTION_STRING,
    R_MIR_INSTRUCTION_AGGREGATE,
    R_MIR_INSTRUCTION_NEW,
    R_MIR_INSTRUCTION_ARRAY,
    R_MIR_INSTRUCTION_VARIANT,
    R_MIR_INSTRUCTION_VARIANT_TAG,
    R_MIR_INSTRUCTION_VARIANT_PAYLOAD,
    R_MIR_INSTRUCTION_FIELD,
    R_MIR_INSTRUCTION_INDEX,
    R_MIR_INSTRUCTION_DEREF,
    R_MIR_INSTRUCTION_BORROW,
    R_MIR_INSTRUCTION_RAW_ADDRESS,
    R_MIR_INSTRUCTION_SLICE,
    R_MIR_INSTRUCTION_LENGTH,
    R_MIR_INSTRUCTION_LOAD,
    R_MIR_INSTRUCTION_MOVE,
    R_MIR_INSTRUCTION_DROP,
    R_MIR_INSTRUCTION_UNARY,
    R_MIR_INSTRUCTION_CAST,
    R_MIR_INSTRUCTION_BINARY,
    R_MIR_INSTRUCTION_CALL,
    R_MIR_INSTRUCTION_FUNCTION_ADDRESS,
    R_MIR_INSTRUCTION_INDIRECT_CALL,
    R_MIR_INSTRUCTION_ASYNC_START,
    R_MIR_INSTRUCTION_AWAIT,
    R_MIR_INSTRUCTION_TASK_SCOPE_ENTER,
    R_MIR_INSTRUCTION_TASK_SCOPE_WAIT,
    R_MIR_INSTRUCTION_TASK_SCOPE_CLOSE,
    R_MIR_INSTRUCTION_STANDARD_CALL,
    R_MIR_INSTRUCTION_STORE,
    R_MIR_INSTRUCTION_PHI,
    R_MIR_INSTRUCTION_DISCARD,
    R_MIR_INSTRUCTION_BRANCH,
    R_MIR_INSTRUCTION_JUMP,
    R_MIR_INSTRUCTION_RETURN,
    R_MIR_INSTRUCTION_THROW,
    R_MIR_INSTRUCTION_EFFECT_TAG,
    R_MIR_INSTRUCTION_EFFECT_PAYLOAD,
    R_MIR_INSTRUCTION_PENDING_SET,
    R_MIR_INSTRUCTION_PENDING_RESUME,
    R_MIR_INSTRUCTION_FINALLY_PUSH,
    R_MIR_INSTRUCTION_FINALLY_ENTER,
    R_MIR_INSTRUCTION_FINALLY_EXIT,
    R_MIR_INSTRUCTION_CANCEL,
    R_MIR_INSTRUCTION_UNREACHABLE,
    /* L39 (R-ERR-0005): ends a step whose finalies ran for a pending panic. */
    R_MIR_INSTRUCTION_PANIC
} RMirInstructionKind;

typedef enum RMirPendingCompletionReason {
    R_MIR_PENDING_COMPLETION_INVALID = 0,
    R_MIR_PENDING_COMPLETION_NORMAL,
    R_MIR_PENDING_COMPLETION_RETURN,
    R_MIR_PENDING_COMPLETION_CHECKED_ERROR,
    R_MIR_PENDING_COMPLETION_BREAK,
    R_MIR_PENDING_COMPLETION_CONTINUE,
    R_MIR_PENDING_COMPLETION_FALLTHROUGH,
    R_MIR_PENDING_COMPLETION_CANCEL,
    R_MIR_PENDING_COMPLETION_PANIC_UNWIND
} RMirPendingCompletionReason;

/*
 * pending_set stages the hidden completion record before scope drops and transfers control to the
 * innermost shared finally block. finally_push/finally_enter/finally_exit maintain the lexical
 * active-finally stack. pending_resume restores the staged payload before the explicit terminal
 * return/throw/jump/cancel. A lexical finally body occurs once in the CFG and finally_exit selects
 * either the next active finally or the recorded resume block.
 */

typedef struct RMirCallBorrowMask {
    uint64_t low;
    uint64_t high;
} RMirCallBorrowMask;

typedef struct RMirInstruction {
    RMirInstructionKind kind;
    RSourceSpan span;
    RTypeId type;
    RTypeId auxiliary_type;
    RTypeId runtime_type;
    RSymbolId symbol;
    RSymbolId task_scope;
    RSymbolId borrow_origin;
    uint32_t borrow_origin_set;
    bool borrow_origin_multiple;
    RTokenKind operation;
    RStandardCallOperation standard_operation;
    RMirValueId result;
    RMirValueId operand0;
    RMirValueId operand1;
    RMirValueId operand2;
    uint32_t first_operand;
    uint32_t operand_count;
    RMirCallBorrowMask call_borrow_mask; /* Bit i marks operand i as a call-bounded borrow. */
    RMirBlockId target0;
    RMirBlockId target1;
    /* L39 (R-ERR-0005): in an async body that unwinds, the block a panic of this instruction
       continues in: the active finalies, then PANIC. */
    RMirBlockId panic_target;
    /* L39: a standard call that may run R code (r_semantic_standard_call_runs_code), after which
       an async step tests for a panic of that code. */
    bool runs_code;
    uint64_t integer_value;
    uint32_t place_ordinal;
    uint32_t aggregate_member;
    bool place_is_parameter;
    bool is_async_staged_move;
    bool replaces_initialized;
    bool immediate_await;
    bool deferred_start; /* Task-group start: commit without dispatch, run after bind. */
} RMirInstruction;

typedef struct RMirBlock {
    uint32_t first_instruction;
    uint32_t instruction_count;
} RMirBlock;

typedef struct RMirFunction {
    RSymbolId symbol;
    RTypeId return_type;
    uint32_t first_block;
    uint32_t block_count;
} RMirFunction;

/* R-AGG-0012 (L27): the declarations generated for one capability of one `@derive`
   attribute, appended after the authored text of their source. A diagnostic inside them is
   reported at `origin`, the capability name in the attribute; once a derivation has reported
   its own diagnostic, the region is `suppressed` and its further diagnostics are dropped. */
typedef struct RDeriveRegion {
    uint32_t start;
    uint32_t end;
    RSourceSpan origin;
    bool suppressed;
    /* The fields of a parent error could not be read and nothing was generated. */
    bool incomplete;
} RDeriveRegion;

typedef struct RSource {
    char *display_name;
    uint8_t *bytes;
    size_t length;
    uint32_t *line_starts;
    size_t line_count;
    size_t line_capacity;
    RToken *tokens;
    size_t token_count;
    size_t token_capacity;
    RCstEvent *cst_events;
    size_t cst_event_count;
    size_t cst_event_capacity;
    RAstNode *ast_nodes;
    size_t ast_node_count;
    size_t ast_node_capacity;
    RAstNodeId *ast_children;
    size_t ast_child_count;
    size_t ast_child_capacity;
    char *module_name;
    RSyntaxNodeId cst_root;
    RAstNodeId ast_root;
    RAstNodeId semantic_root;
    bool lexed;
    /* M32-7: the tokens are in the order of their starts, which the lexer checks once, so a
       binary search may find the first token of a span. */
    bool tokens_ordered;
    bool interface_scanned;
    bool parsed;
    bool lowered;
    bool has_lex_error;
    bool has_syntax_error;
    bool has_semantic_error;
    bool has_ownership_error;
    bool has_unresolved_ambiguity;
    bool semantically_analyzed;
    /* R-OBJ-0012: the module declaration carries @deny_panic_alloc. */
    bool deny_panic_alloc;
    /* R-MOD-0002: a standard module supplied as R source by the library map; it may declare a
       reserved module path and is available from `minimum_profile` upwards. */
    bool is_library;
    uint32_t minimum_profile;
    /* R-AGG-0012 (L27): generated declarations follow the first `authored_length` bytes. */
    bool derive_expanded;
    /* R-FUNC-0025 (M24): the entry of test mode was appended. */
    bool test_expanded;
    uint32_t authored_length;
    RDeriveRegion *derive_regions;
    size_t derive_region_count;
    size_t derive_region_capacity;
} RSource;

typedef struct RAggregateEntry {
    char *module_name;
    char *name;
    RSourceId source;
    uint32_t declaration_offset;
    bool is_generic;
    bool is_protected;
    bool is_error;
} RAggregateEntry;

typedef struct RImportEntry {
    char *module_name;
    char *name;
    RSourceSpan name_span;
    /* The module path as written in the import declaration. */
    RSourceSpan module_span;
    RSourceId source;
    bool imports_name;
} RImportEntry;

/* One extern "C" block: its @header spellings live in the context header table. */
typedef struct RExternBlockRecord {
    RSourceId source;
    uint32_t span_start;
    uint32_t first_header;
    uint32_t header_count;
    /* Interned @link name and @abi record name of the block; zero when absent. */
    uint32_t link_name_intern_id;
    uint32_t abi_name_intern_id;
    /* One-based provider index recorded by r_frontend_resolve_links; zero when unresolved. */
    uint32_t link_provider;
} RExternBlockRecord;

/* One header a record was derived from and the digest recorded for it (R-FFI-0044). */
typedef struct RAbiRecordHeader {
    char *spelling;
    size_t spelling_length;
    char *sha256;
    size_t sha256_length;
} RAbiRecordHeader;

/* One loaded ABI record (schema r-abi-record-0.1) and its normalized inventories (R-FFI-0040). */
typedef struct RAbiRecordEntry {
    char *name;
    size_t name_length;
    char *provider;
    size_t provider_length;
    char *target_triple;
    size_t target_triple_length;
    /* First line of the generating C compiler's version output; NULL when absent. */
    char *compiler_identity;
    size_t compiler_identity_length;
    /* The record's compiler options name C17 mode (R-FFI-0040). */
    bool c17_options;
    size_t first_type;
    size_t type_count;
    size_t first_header;
    size_t header_count;
    size_t first_symbol;
    size_t symbol_count;
} RAbiRecordEntry;

typedef struct RAbiRecordType {
    char *c_name;
    size_t c_name_length;
    /* Reached from an import prototype but not expressible in R (bit-field, anonymous member,
       union, incomplete type): an R-declared struct cannot match it. */
    bool inexpressible;
    RCTypeKind kind;
    /* Tag kind behind a typedef spelling; equals kind for a tag spelling. */
    RCTypeKind tag_kind;
    char *underlying;
    size_t underlying_length;
    uint64_t size;
    uint64_t alignment;
    bool has_layout;
    size_t first_member;
    size_t member_count;
    size_t first_enumerator;
    size_t enumerator_count;
} RAbiRecordType;

typedef struct RAbiRecordMember {
    char *name;
    size_t name_length;
    char *type;
    size_t type_length;
    char *desugared_type;
    size_t desugared_type_length;
    uint64_t offset;
    /* Type tree of the member; SIZE_MAX when the record carries none. */
    size_t type_node;
} RAbiRecordMember;

/* One node of a C type tree of an ABI record: an import's C type or a member's type as the
   selected compiler resolved it (R-FFI-0041). Children follow in abi_type_children: the
   pointee of a pointer, the element of an array, the result and then the parameters of a
   function. */
typedef enum RAbiTypeNodeKind {
    R_ABI_TYPE_NODE_OTHER = 0,
    R_ABI_TYPE_NODE_VOID,
    R_ABI_TYPE_NODE_SCALAR,
    R_ABI_TYPE_NODE_POINTER,
    R_ABI_TYPE_NODE_FUNCTION,
    R_ABI_TYPE_NODE_RECORD,
    R_ABI_TYPE_NODE_ENUM,
    R_ABI_TYPE_NODE_ARRAY
} RAbiTypeNodeKind;

typedef struct RAbiTypeNode {
    RAbiTypeNodeKind kind;
    bool is_const;
    bool is_variadic;
    bool is_union;
    bool is_complete;
    /* Scalar spelling, record or enumeration tag and the typedef naming this position. */
    char *spelling;
    size_t spelling_length;
    char *tag_name;
    size_t tag_name_length;
    char *typedef_name;
    size_t typedef_name_length;
    uint64_t length;
    size_t first_child;
    uint32_t child_count;
} RAbiTypeNode;

/* The C type of one requested import symbol. */
typedef struct RAbiRecordSymbol {
    char *c_name;
    size_t c_name_length;
    bool is_object;
    size_t type_node;
} RAbiRecordSymbol;

/* One R-declared struct position of an import: the record type node at that position and the
   one-based parameter it belongs to (zero for the result or an object's type). */
typedef struct RCImportSlot {
    size_t type_node;
    uint32_t position;
} RCImportSlot;

/* An R-declared struct proven against one C struct of a record (R-FFI-0041). */
typedef struct RCReprPair {
    uint32_t aggregate_id;
    size_t abi_type;
    bool verified;
} RCReprPair;

typedef struct RAbiRecordEnumerator {
    char *name;
    size_t name_length;
    int64_t value;
} RAbiRecordEnumerator;

/* Link provider selected for at least one import; definitions are its feature-test macros. */
typedef struct RLinkProviderRecord {
    char *name;
    size_t name_length;
    RLinkKind kind;
    size_t first_definition;
    size_t definition_count;
} RLinkProviderRecord;

typedef struct RLinkDefinitionRecord {
    char *name;
    size_t name_length;
    char *value;
    size_t value_length;
} RLinkDefinitionRecord;

typedef struct RBorrowOriginSet {
    size_t first;
    uint32_t count;
} RBorrowOriginSet;

typedef struct RProjectedBorrowStateEntry {
    RHirNodeId destination;
    RHirNodeId source;
    RSymbolId rebase_origin;
    uint32_t component_count;
} RProjectedBorrowStateEntry;

typedef struct RProjectedBorrowState {
    size_t first;
    uint32_t count;
} RProjectedBorrowState;

/*
 * Library profiles are cumulative (Core R-CONF-G005, Library R-SLIB-PROFILE-0001): each value
 * makes every facility of the smaller values available as well.
 */
typedef enum RFrontendProfile {
    R_FRONTEND_PROFILE_FREESTANDING = 0,
    R_FRONTEND_PROFILE_ALLOCATION,
    R_FRONTEND_PROFILE_HOSTED,
    R_FRONTEND_PROFILE_HOSTED_THREAD,
    R_FRONTEND_PROFILE_HOSTED_NATIVE_ASYNC
} RFrontendProfile;

struct RFrontendContext {
    RFrontendOptions options;
    /* R-SLIB-ERR-0004 (L21.5): std.error::fault and the family of the standard errors it
       names, in hosted profiles; zero otherwise. */
    RTypeId standard_fault_type;
    RTypeId standard_fault_family;
    RSource *sources;
    size_t source_count;
    size_t source_capacity;
    RDiagnostic *diagnostics;
    size_t diagnostic_count;
    size_t diagnostic_capacity;
    RAggregateEntry *aggregates;
    size_t aggregate_count;
    size_t aggregate_capacity;
    RImportEntry *imports;
    size_t import_count;
    size_t import_capacity;
    RInternEntry *intern_entries;
    size_t intern_count;
    size_t intern_capacity;
    uint32_t generic_instantiation_depth;
    RSymbolId generic_current_function;
    bool generic_members_ready;
    /* While aggregate members are collected: which aggregates are collected or in progress, so
       that a field type naming checked errors collects those errors first (R-TYPE-0054). */
    unsigned char *member_collection;
    size_t member_collection_count;
    bool generic_constraints_ready;
    RGenericSchema *generic_schemas;
    RSourceSpan format_local_type_span;
    RFormatSchema *format_schemas;
    size_t format_schema_count;
    size_t format_schema_capacity;
    RFormatPart *format_parts;
    size_t format_part_count;
    size_t format_part_capacity;
    RFormatRecipe *format_recipes;
    size_t format_recipe_count;
    size_t format_recipe_capacity;
    size_t generic_schema_count;
    size_t generic_schema_capacity;
    RGenericParameter *generic_parameters;
    RStaticPredicate *static_predicates;
    size_t static_predicate_count;
    size_t static_predicate_capacity;
    RStaticFact *static_facts;
    size_t static_fact_count;
    size_t static_fact_capacity;
    RStaticChoice *static_choices;
    size_t static_choice_count;
    size_t static_choice_capacity;
    RComputedBound *computed_bounds;
    size_t computed_bound_count;
    size_t computed_bound_capacity;
    RComputedValue *computed_values;
    size_t computed_value_count;
    size_t computed_value_capacity;
    RDynMember *dyn_members;
    size_t dyn_member_count;
    size_t dyn_member_capacity;
    RDynTarget *dyn_targets;
    size_t dyn_target_count;
    size_t dyn_target_capacity;
    RFunctionValueTarget *function_value_targets;
    size_t function_value_target_count;
    size_t function_value_target_capacity;
    /* Discovery only: closures of computed bounds that wait for this pass's evaluation. */
    size_t computed_pending;
    size_t generic_parameter_count;
    size_t generic_parameter_capacity;
    RGenericPendingConstraint *generic_pending_constraints;
    size_t generic_pending_constraint_count;
    size_t generic_pending_constraint_capacity;
    RGenericProjectionConstraint *generic_projection_constraints;
    size_t generic_projection_constraint_count;
    size_t generic_projection_constraint_capacity;
    RGenericPendingCallable *generic_pending_callables;
    size_t generic_pending_callable_count;
    size_t generic_pending_callable_capacity;
    uint32_t *generic_trait_constraints;
    size_t generic_trait_constraint_count;
    size_t generic_trait_constraint_capacity;
    RSemanticTrait *semantic_traits;
    size_t semantic_trait_count;
    size_t semantic_trait_capacity;
    RSemanticImpl *semantic_impls;
    size_t semantic_impl_count;
    size_t semantic_impl_capacity;
    RSemanticAssociatedType *semantic_associated_types;
    size_t semantic_associated_type_count;
    size_t semantic_associated_type_capacity;
    RTypeId *semantic_associated_bindings;
    size_t semantic_associated_binding_count;
    size_t semantic_associated_binding_capacity;
    RSemanticAssociatedConstant *semantic_associated_constants;
    size_t semantic_associated_constant_count;
    size_t semantic_associated_constant_capacity;
    RAstRef *semantic_constant_bindings;
    size_t semantic_constant_binding_count;
    size_t semantic_constant_binding_capacity;
    /* R-TYPE-0050: implementations and their constant bindings are collected. */
    bool impls_ready;
    /* R-TYPE-0046: one-based indices of the core traits, zero until declared. */
    uint32_t core_iterator_trait;
    uint32_t core_contains_trait;
    /* R-TYPE-0046 (L20.4): core::CaseMatcher, the matcher of switch and match labels. */
    uint32_t core_case_matcher_trait;
    uint32_t core_format_trait;
    uint32_t core_lending_iterator_trait;
    /* R-TYPE-0046 (L36.1): core::AsyncIterator, declared in the hosted-native-async profile. */
    uint32_t core_async_iterator_trait;
    RSemanticMethod *semantic_methods;
    size_t semantic_method_count;
    size_t semantic_method_capacity;
    RTypeId *generic_arguments;
    size_t generic_argument_count;
    size_t generic_argument_capacity;
    RSemanticType *semantic_types;
    size_t semantic_type_count;
    size_t semantic_type_capacity;
    /* M32-7: an open-addressing index of semantic_types by kind, base, second, length and flags
       (type ids, zero for an empty slot); the first semantic_type_indexed types are in it. */
    uint32_t *semantic_type_index;
    size_t semantic_type_index_capacity;
    size_t semantic_type_indexed;
    RSemanticAggregate *semantic_aggregates;
    size_t semantic_aggregate_count;
    size_t semantic_aggregate_capacity;
    RSemanticField *semantic_fields;
    size_t semantic_field_count;
    size_t semantic_field_capacity;
    RJsonSchemaField *json_fields;
    size_t json_field_count;
    size_t json_field_capacity;
    RSemanticVariant *semantic_variants;
    size_t semantic_variant_count;
    size_t semantic_variant_capacity;
    /* R-AGG-0013 (L42): the uses of user attributes and their arguments. */
    RSemanticAttributeUse *attribute_uses;
    size_t attribute_use_count;
    size_t attribute_use_capacity;
    RSemanticAttributeValue *attribute_values;
    size_t attribute_value_count;
    size_t attribute_value_capacity;
    RSemanticSymbol *semantic_symbols;
    size_t semantic_symbol_count;
    size_t semantic_symbol_capacity;
    RErrorBorrowMapping *error_borrow_mappings;
    size_t error_borrow_mapping_count;
    size_t error_borrow_mapping_capacity;
    /* R-TYPE-0052 (L18.1): the synthesized generic struct of each tuple arity, indexed by arity;
       zero where none exists yet. */
    uint32_t *tuple_origins;
    size_t tuple_origin_capacity;
    /* R-TYPE-0053: a pack may be named here, in a position that expands it. */
    bool pack_expansion_permitted;
    RStoreBorrowMapping *store_borrow_mappings;
    size_t store_borrow_mapping_count;
    size_t store_borrow_mapping_capacity;
    RBorrowOriginSet *borrow_origin_sets;
    size_t borrow_origin_set_count;
    size_t borrow_origin_set_capacity;
    RSymbolId *borrow_origin_set_items;
    size_t borrow_origin_set_item_count;
    size_t borrow_origin_set_item_capacity;
    RProjectedBorrowState *projected_borrow_states;
    size_t projected_borrow_state_count;
    size_t projected_borrow_state_capacity;
    RProjectedBorrowStateEntry *projected_borrow_state_entries;
    size_t projected_borrow_state_entry_count;
    size_t projected_borrow_state_entry_capacity;
    RTypeId *semantic_parameter_types;
    size_t semantic_parameter_type_count;
    size_t semantic_parameter_type_capacity;
    RHirNode *hir_nodes;
    size_t hir_node_count;
    size_t hir_node_capacity;
    RHirNodeId *hir_children;
    size_t hir_child_count;
    size_t hir_child_capacity;
    RHirEffectExit *hir_effect_exits;
    size_t hir_effect_exit_count;
    size_t hir_effect_exit_capacity;
    RHirNodeId hir_root;
    RMirFunction *mir_functions;
    size_t mir_function_count;
    size_t mir_function_capacity;
    RMirBlock *mir_blocks;
    size_t mir_block_count;
    size_t mir_block_capacity;
    RMirInstruction *mir_instructions;
    size_t mir_instruction_count;
    size_t mir_instruction_capacity;
    RMirValueId *mir_operands;
    size_t mir_operand_count;
    size_t mir_operand_capacity;
    uint32_t *mir_operand_members;
    size_t mir_operand_member_capacity;
    RFrontendStatus resource_status;
    RFrontendStatus semantic_status;
    bool interfaces_linked;
    bool semantic_analyzing;
    bool semantic_analyzed;
    bool mir_lowered;
    bool sources_sealed;
    RFrontendProfile profile;
    /* R-OBJ-0012: the translation-wide deny-panic-allocation option. */
    bool deny_panic_alloc;
    /* R-FUNC-0025 (M24): test mode and the module whose tests run, NULL for the first source. */
    bool test_mode;
    char *test_module;
    /* R-FUNC-0023: function bodies may be lowered on demand for translation-time evaluation. */
    bool consteval_ready;
    /* The last type name the type resolver could not find (R-NAME-0003); end == 0 when none.
       A caller whose type reference failed without a diagnostic reports it. */
    RSourceSpan unresolved_type_name_span;
    /* R-EXPR-0032: this context is a discovery pass for module-scope translation-time values. */
    bool consteval_discovery;
    /* L22.4: a const module or static object is initialized, so a translation-time owner value
       may be frozen into the program image. */
    bool consteval_freeze;
    /* Counts HIR lowered on demand for evaluation, so scratch lowering keeps it. */
    size_t consteval_builds;
    /* Values computed at translation time; the interface then records source dependencies. */
    size_t consteval_evaluations;
    /* Storage slots of evaluated locals by symbol, reused by successive evaluations. */
    void **consteval_slots;
    size_t consteval_slot_count;
    bool consteval_slots_in_use;
    RConstevalValue *consteval_values;
    size_t consteval_value_count;
    size_t consteval_value_capacity;
    RJsonSchemaHook *json_schema_hooks;
    size_t json_schema_hook_count;
    size_t json_schema_hook_capacity;
    RExternBlockRecord *extern_blocks;
    size_t extern_block_count;
    size_t extern_block_capacity;
    uint32_t *extern_headers;
    size_t extern_header_count;
    size_t extern_header_capacity;
    RLinkProviderRecord *link_providers;
    size_t link_provider_count;
    size_t link_provider_capacity;
    RLinkDefinitionRecord *link_definitions;
    size_t link_definition_count;
    size_t link_definition_capacity;
    RAbiRecordEntry *abi_records;
    size_t abi_record_count;
    size_t abi_record_capacity;
    RAbiRecordType *abi_types;
    size_t abi_type_count;
    size_t abi_type_capacity;
    RAbiRecordMember *abi_members;
    size_t abi_member_count;
    size_t abi_member_capacity;
    RAbiRecordEnumerator *abi_enumerators;
    size_t abi_enumerator_count;
    size_t abi_enumerator_capacity;
    RAbiRecordHeader *abi_headers;
    size_t abi_header_count;
    size_t abi_header_capacity;
    RAbiTypeNode *abi_type_nodes;
    size_t abi_type_node_count;
    size_t abi_type_node_capacity;
    size_t *abi_type_children;
    size_t abi_type_child_count;
    size_t abi_type_child_capacity;
    RAbiRecordSymbol *abi_symbols;
    size_t abi_symbol_count;
    size_t abi_symbol_capacity;
    RCImportSlot *c_import_slots;
    size_t c_import_slot_count;
    size_t c_import_slot_capacity;
    RCReprPair *c_repr_pairs;
    size_t c_repr_pair_count;
    size_t c_repr_pair_capacity;
    bool abi_records_loaded;
    /* SHA-256 of the loaded ABI record document; part of the interface fingerprint. */
    uint8_t abi_record_digest[32];
    bool abi_record_digest_present;
};

/* Spells a C ABI type as the header declares it (struct/enum tags, typedef names, pointers,
   function pointers); false when the type has no C spelling or the buffer is too small. */
bool r_c_abi_spell_header_type(const RFrontendContext *context,
                               RTypeId type_id,
                               char *buffer,
                               size_t capacity);

/* Target size and alignment of a complete value type; false for dependent or incomplete types. */
bool r_semantic_type_layout(const RFrontendContext *context,
                            RTypeId type_id,
                            uint64_t *size,
                            uint64_t *alignment);

/* Signedness and width of a fixed R integer kind (the representation of a C ABI integer). */
bool r_semantic_integer_properties_public(RSemanticTypeKind kind, bool *is_signed, uint32_t *width);

typedef enum RAsciiLexemeKind {
    R_ASCII_LEXEME_EOF = 0,
    R_ASCII_LEXEME_WHITESPACE,
    R_ASCII_LEXEME_LINE_COMMENT,
    R_ASCII_LEXEME_BLOCK_COMMENT_START,
    R_ASCII_LEXEME_IDENTIFIER,
    R_ASCII_LEXEME_NUMBER_START,
    R_ASCII_LEXEME_STRING_START,
    R_ASCII_LEXEME_CHARACTER_START,
    R_ASCII_LEXEME_PUNCTUATOR,
    R_ASCII_LEXEME_NON_ASCII,
    R_ASCII_LEXEME_UNKNOWN
} RAsciiLexemeKind;

typedef struct RAsciiLexeme {
    RAsciiLexemeKind kind;
    RTokenKind token_kind;
    size_t length;
} RAsciiLexeme;

RAsciiLexeme r_lexer_scan_ascii(const uint8_t *cursor);

void *r_context_allocate(RFrontendContext *context, size_t size);
void r_context_free(RFrontendContext *context, void *pointer);
bool r_grow_array(
    RFrontendContext *context, void **items, size_t *capacity, size_t item_size, size_t required);
char *r_copy_string(RFrontendContext *context, const char *bytes, size_t length);
bool r_intern_bytes(RFrontendContext *context,
                    const uint8_t *bytes,
                    size_t length,
                    uint32_t *intern_id);
RSource *r_get_source(RFrontendContext *context, RSourceId source_id);
const RSource *r_get_source_const(const RFrontendContext *context, RSourceId source_id);
bool r_add_diagnostic(RFrontendContext *context,
                      const char *code,
                      const char *rule_id,
                      const char *message,
                      RDiagnosticSeverity severity,
                      RSourceSpan span);
bool r_add_diagnostic_phase(RFrontendContext *context,
                            RDiagnosticPhase phase,
                            const char *code,
                            const char *rule_id,
                            const char *message,
                            RDiagnosticSeverity severity,
                            RSourceSpan span);
/* R-AGG-0012 (L27): generated text appended to a source and the region lookup. */
bool r_source_append_bytes(RFrontendContext *context,
                           RSource *source,
                           const uint8_t *bytes,
                           size_t length);
bool r_lex_append(RFrontendContext *context, RSourceId source_id, const char *text, size_t length);
RDeriveRegion *r_source_derive_region(const RFrontendContext *context, RSourceSpan span);
RFrontendStatus r_derive_expand(RFrontendContext *context, RSourceId source_id);
RFrontendStatus r_test_expand(RFrontendContext *context, RSourceId source_id);
bool r_add_token(
    RFrontendContext *context, RSource *source, RTokenKind kind, uint32_t start, uint32_t end);

const char *r_token_kind_name(RTokenKind kind);
const char *r_syntax_kind_name(RSyntaxKind kind);
bool r_token_is_trivia(RTokenKind kind);
bool r_token_is_keyword(RTokenKind kind);
bool r_token_is_c_abi_type(RTokenKind kind);
bool r_token_is_primitive_type(RTokenKind kind);

bool r_utf8_decode(
    const uint8_t *bytes, size_t length, size_t offset, uint32_t *code_point, size_t *width);
bool r_decode_character_literal(const RFrontendContext *context,
                                RSourceSpan span,
                                uint32_t *code_point);
bool r_intern_string_sequence(RFrontendContext *context,
                              RSourceSpan span,
                              uint32_t *intern_id,
                              size_t *decoded_length);
bool r_unicode_is_xid_start(uint32_t code_point);
bool r_unicode_is_xid_continue(uint32_t code_point);
bool r_unicode_sequence_is_nfc(RFrontendContext *context,
                               const uint8_t *bytes,
                               size_t length,
                               bool *is_nfc);

bool r_source_text_equal(const RSource *source, RSourceSpan span, const char *text);
bool r_source_span_equal(const RSource *left_source,
                         RSourceSpan left,
                         const RSource *right_source,
                         RSourceSpan right);
bool r_is_visible_aggregate(const RFrontendContext *context,
                            RSourceId source_id,
                            RSourceSpan name,
                            uint32_t use_offset);

RFrontendStatus r_parse_source(RFrontendContext *context, RSourceId source_id);
RFrontendStatus r_lower_source_ast(RFrontendContext *context, RSourceId source_id);

bool r_ast_root_ref(const RFrontendContext *context, RSourceId source_id, RAstRef *root);
bool r_semantic_root_ref(const RFrontendContext *context, RSourceId source_id, RAstRef *root);
bool r_ast_ref_view(const RFrontendContext *context, RAstRef ref, RAstNodeView *view);
bool r_ast_ref_child(const RFrontendContext *context,
                     RAstRef parent,
                     uint32_t child_index,
                     RAstRef *child);
bool r_ast_ref_token(const RFrontendContext *context, RAstRef ref, RAstTokenView *token);
bool r_ast_ref_text(const RFrontendContext *context, RAstRef ref, RAstTextView *text);

RFrontendStatus r_analyze_program(RFrontendContext *context);
bool r_semantic_type_from_token(RFrontendContext *context, RTokenKind token_kind, RTypeId *type_id);
const RSemanticType *r_semantic_type(const RFrontendContext *context, RTypeId type_id);
RTypeId r_semantic_representation_type(const RFrontendContext *context, RTypeId type_id);
/*
 * R-FUNC-0007: whether the callback function may be the target of a call through the raw
 * function type (exact exported signature). An unresolved pointer type admits every callback.
 */
/* R-TYPE-0051: the interface a borrow type refers to, whether a function symbol dispatches on an
   interface, and the tag of one member of an interface. */
RTypeId r_semantic_dyn_referent(const RFrontendContext *context, RTypeId type_id);
RTypeId r_semantic_dyn_owned(const RFrontendContext *context, RTypeId type_id);
bool r_semantic_dyn_dispatcher(const RFrontendContext *context, RSymbolId symbol);
bool r_semantic_dyn_format_target(const RFrontendContext *context, RSymbolId target);
bool r_semantic_function_value_dispatcher(const RFrontendContext *context, RSymbolId symbol);
bool r_semantic_dyn_tag(const RFrontendContext *context,
                        RTypeId interface,
                        RTypeId member,
                        uint32_t *tag);
bool r_semantic_callback_may_be_target(const RFrontendContext *context,
                                       RSymbolId function_id,
                                       RTypeId raw_function_type);
bool r_semantic_type_is_send(const RFrontendContext *context, RTypeId type_id);
bool r_semantic_type_is_replaceable(const RFrontendContext *, RTypeId);
bool r_semantic_type_is_async_staged_move(const RFrontendContext *context, RTypeId type_id);
bool r_semantic_type_is_scoped_staged_move(const RFrontendContext *context, RTypeId type_id);
bool r_semantic_type_is_sync(const RFrontendContext *context, RTypeId type_id);
bool r_semantic_type_crosses_await(const RFrontendContext *context, RTypeId type_id);
uint32_t r_semantic_effect_count(const RFrontendContext *context, RTypeId effect_set);
/* L39: whether a standard call may run R code on the calling thread, through the glue of a user
   type it handles or an initializer it calls, and so return with a panic of that code pending. */
bool r_semantic_standard_call_runs_code(const RFrontendContext *context, const RHirNode *node);
RTypeId r_semantic_effect_at(const RFrontendContext *context, RTypeId effect_set, uint32_t index);
bool r_semantic_effect_contains(const RFrontendContext *context,
                                RTypeId effect_set,
                                RTypeId error_type);
/* R-ERR-0003 (L21.2): the number of parent steps from the exact error `error_type` up to the error
   whose values a catch clause of `catch_type` receives, or UINT32_MAX when the clause does not
   receive it; zero for the same type. */
uint32_t r_semantic_error_catch_distance(const RFrontendContext *context,
                                         RTypeId catch_type,
                                         RTypeId error_type);
/* R-AGG-0011 (L21.3): the variant of a family holding the exact error, or UINT32_MAX. */
uint32_t r_semantic_error_family_tag(const RFrontendContext *context,
                                     RTypeId family_type,
                                     RTypeId error_type);
/* R-SLIB-ERR-0004 (L21.5): whether `type` is an argument of the erasure `descriptor`. */
bool r_semantic_error_erasure_accepts(const RFrontendContext *context,
                                      const RStandardErrorErasureDescriptor *descriptor,
                                      RTypeId type);
bool r_semantic_effect_carrier_type(RFrontendContext *context,
                                    RTypeId success_type,
                                    RTypeId effect_set,
                                    RTypeId *carrier_type);
const char *r_semantic_type_kind_name(RSemanticTypeKind kind);
const char *r_hir_kind_name(RHirKind kind);
/* L25.4: whether two HIR trees perform the same operations, apart from their source spans; the
   error exits of one call whose cleanups agree leave through one shared path. */
bool r_hir_same_tree(const RFrontendContext *context, RHirNodeId left, RHirNodeId right);
const char *r_mir_instruction_kind_name(RMirInstructionKind kind);

/* R-LIMIT-0001, R-LIMIT-0003: the depth that a walk of an AST or HIR tree admits. The parser admits
   max_nesting levels of source nesting, and one level becomes at most three levels of either
   tree (the body of a for loop, an argument of a call), so a program the parser accepts stays
   within four times that depth. */
uint32_t r_frontend_tree_depth_limit(const RFrontendContext *context);
void r_frontend_diagnose_limit(RFrontendContext *context, const char *message);

bool r_write_text(RFrontendWriteFn writer, void *user_data, const char *text);
bool r_write_bytes(RFrontendWriteFn writer, void *user_data, const char *bytes, size_t length);
bool r_write_uint32(RFrontendWriteFn writer, void *user_data, uint32_t value);
bool r_write_escaped(RFrontendWriteFn writer, void *user_data, const uint8_t *bytes, size_t length);
void r_sha256_digest(const uint8_t *bytes, size_t length, uint8_t digest[32]);

#endif

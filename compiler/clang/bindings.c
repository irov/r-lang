#include "clang_internal.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Bindings from C headers (B9.2, R-FFI-0009). The input is an R module with one empty
 * extern "C" block that names its link provider and its headers; the output is that module with
 * the block filled with declarations of what the headers declare themselves (not what they
 * include): structs and unions, enumerations, integer constants of object-like macros and
 * enumerators, objects and functions, in the forms of R 0.1. The text is source to review and
 * keep under version control, not something a build regenerates: every function carries an
 * @safety whose text says that its contract was not reviewed. A complete struct or enumeration is
 * declared complete only when the block names an @abi record, which proves it (R-FFI-0041);
 * otherwise a struct is opaque and an enumeration is its integer type with its enumerators as
 * verified constants. What R cannot express is left out with a comment that says why: a
 * function with a struct by value R declares opaque, an inline or static function (R-FFI-0020),
 * a symbol the header decorates (R-CMAP-0026), a variadic function pointer, a qualifier R has
 * no form for.
 */

typedef struct RBindText {
    RFrontendContext *context;
    char *bytes;
    size_t length;
    size_t capacity;
    bool failed;
} RBindText;

typedef enum RBindForm {
    R_BIND_FORM_OPAQUE = 0,
    R_BIND_FORM_STRUCT,
    R_BIND_FORM_ENUM,
    R_BIND_FORM_INTEGER
} RBindForm;

/* One C struct, union or enumeration the bindings name. */
typedef struct RBindType {
    CXCursor canonical;
    CXCursor definition;
    bool has_definition;
    bool is_union;
    bool is_enum;
    bool in_headers;
    RBindForm form;
    char *r_name;
    char *c_name;
    /* The tag of the type; "" when only a typedef names it. */
    char *tag;
    const char *c_kind;
    char *reason;
    const char *integer;
    /* R-FFI-0061: `packed` and the alignment the aggregate raises itself to, or zero. */
    bool packed;
    long long align;
} RBindType;

typedef enum RBindPosition {
    R_BIND_PARAMETER = 0,
    R_BIND_RESULT,
    R_BIND_MEMBER,
    R_BIND_OBJECT,
    R_BIND_POINTEE
} RBindPosition;

typedef struct RBindGenerator {
    RFrontendContext *context;
    CXTranslationUnit unit;
    CXFile files[64];
    size_t file_count;
    bool complete_allowed;
    RBindType *types;
    size_t type_count;
    size_t type_capacity;
    char **names;
    size_t name_count;
    size_t name_capacity;
    CXCursor *items;
    size_t item_count;
    size_t item_capacity;
    /* The first typedef of the headers that names each tag: how the headers spell the type. */
    CXCursor *aliased;
    char **aliases;
    size_t alias_count;
    size_t alias_capacity;
    size_t aliased_capacity;
    RBindText types_text;
    RBindText constants_text;
    RBindText objects_text;
    RBindText functions_text;
    RBindText skipped_text;
    /* A type of the declaration being spelled has a typedef or reserved C spelling, which a
       variadic import cannot carry (R-FFI-0057, R-CMAP-0018). */
    bool mentions_private;
    bool exhausted;
} RBindGenerator;

/* ---- text ---------------------------------------------------------------------------- */

static bool r_bind_append_bytes(RBindText *text, const char *bytes, size_t length) {
    if (text->failed) {
        return false;
    }
    if ((length >= SIZE_MAX - text->length) || !r_grow_array(text->context,
                                                             (void **)&text->bytes,
                                                             &text->capacity,
                                                             1U,
                                                             text->length + length + 1U)) {
        text->failed = true;
        return false;
    }
    if (length != 0U) {
        (void)memcpy(text->bytes + text->length, bytes, length);
    }
    text->length += length;
    text->bytes[text->length] = '\0';
    return true;
}

static bool r_bind_append(RBindText *text, const char *bytes) {
    return r_bind_append_bytes(text, bytes, strlen(bytes));
}

static bool r_bind_appendf(RBindText *text, const char *format, ...) {
    char buffer[1024];
    va_list arguments;
    int written;

    va_start(arguments, format);
    written = vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    if ((written < 0) || ((size_t)written >= sizeof(buffer))) {
        text->failed = true;
        return false;
    }
    return r_bind_append_bytes(text, buffer, (size_t)written);
}

static void r_bind_text_init(RBindText *text, RFrontendContext *context) {
    (void)memset(text, 0, sizeof(*text));
    text->context = context;
}

static char *r_bind_take(RBindGenerator *generator, CXString text) {
    const char *bytes = clang_getCString(text);
    char *copy = r_copy_string(
        generator->context, bytes != NULL ? bytes : "", bytes != NULL ? strlen(bytes) : 0U);

    clang_disposeString(text);
    if (copy == NULL) {
        generator->exhausted = true;
    }
    return copy;
}

/* An ASCII identifier that is no keyword of R. */
static bool r_bind_valid_name(const char *name) {
    size_t index;

    if ((name == NULL) || (name[0] == '\0') || ((name[0] >= '0') && (name[0] <= '9'))) {
        return false;
    }
    for (index = 0U; name[index] != '\0'; ++index) {
        const char c = name[index];

        if (!(((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) ||
              ((c >= '0') && (c <= '9')) || (c == '_'))) {
            return false;
        }
    }
    return !r_lexer_is_keyword((const uint8_t *)name, index);
}

static bool r_bind_name_taken(const RBindGenerator *generator, const char *name) {
    size_t index;

    for (index = 0U; index < generator->name_count; ++index) {
        if (strcmp(generator->names[index], name) == 0) {
            return true;
        }
    }
    return false;
}

/* Claims an R name of the module: the C name when it is free and valid, else the C name with a
   suffix. NULL when the allocator is exhausted. */
static char *r_bind_claim(RBindGenerator *generator, const char *c_name, const char *suffix) {
    char candidate[256];
    unsigned attempt;
    char *claimed;

    (void)snprintf(candidate, sizeof(candidate), "%s", c_name);
    for (attempt = 0U; !r_bind_valid_name(candidate) || r_bind_name_taken(generator, candidate);
         ++attempt) {
        if (attempt == 0U) {
            (void)snprintf(candidate, sizeof(candidate), "%s%s", c_name, suffix);
        } else {
            (void)snprintf(candidate, sizeof(candidate), "%s%s%u", c_name, suffix, attempt);
        }
        if (attempt > 64U) {
            return NULL;
        }
    }
    claimed = r_copy_string(generator->context, candidate, strlen(candidate));
    if ((claimed == NULL) || !r_grow_array(generator->context,
                                           (void **)&generator->names,
                                           &generator->name_capacity,
                                           sizeof(char *),
                                           generator->name_count + 1U)) {
        generator->exhausted = true;
        r_context_free(generator->context, claimed);
        return NULL;
    }
    generator->names[generator->name_count++] = claimed;
    return claimed;
}

/* ---- the headers' declarations -------------------------------------------------------- */

static bool r_bind_in_headers(const RBindGenerator *generator, CXCursor cursor) {
    CXFile file = NULL;
    size_t index;

    clang_getSpellingLocation(clang_getCursorLocation(cursor), &file, NULL, NULL, NULL);
    if (file == NULL) {
        return false;
    }
    for (index = 0U; index < generator->file_count; ++index) {
        if (clang_File_isEqual(file, generator->files[index]) != 0) {
            return true;
        }
    }
    return false;
}

static enum CXChildVisitResult
r_bind_include_visit(CXCursor cursor, CXCursor parent, CXClientData data) {
    RBindGenerator *generator = data;
    CXFile included;
    CXFile file = NULL;

    (void)parent;
    if (clang_getCursorKind(cursor) != CXCursor_InclusionDirective) {
        return CXChildVisit_Continue;
    }
    clang_getSpellingLocation(clang_getCursorLocation(cursor), &file, NULL, NULL, NULL);
    included = clang_getIncludedFile(cursor);
    if ((file != NULL) && (included != NULL) &&
        (clang_File_isEqual(file, clang_getFile(generator->unit, R_CLANG_UNIT_NAME)) != 0) &&
        (generator->file_count < sizeof(generator->files) / sizeof(generator->files[0]))) {
        generator->files[generator->file_count++] = included;
    }
    return CXChildVisit_Continue;
}

static enum CXChildVisitResult
r_bind_item_visit(CXCursor cursor, CXCursor parent, CXClientData data) {
    RBindGenerator *generator = data;

    (void)parent;
    switch (clang_getCursorKind(cursor)) {
    case CXCursor_StructDecl:
    case CXCursor_UnionDecl:
    case CXCursor_EnumDecl:
    case CXCursor_TypedefDecl:
    case CXCursor_FunctionDecl:
    case CXCursor_VarDecl:
    case CXCursor_MacroDefinition:
        break;
    default:
        return CXChildVisit_Continue;
    }
    if (!r_bind_in_headers(generator, cursor)) {
        return CXChildVisit_Continue;
    }
    if (!r_grow_array(generator->context,
                      (void **)&generator->items,
                      &generator->item_capacity,
                      sizeof(CXCursor),
                      generator->item_count + 1U)) {
        generator->exhausted = true;
        return CXChildVisit_Break;
    }
    generator->items[generator->item_count++] = cursor;
    return CXChildVisit_Continue;
}

/* ---- types --------------------------------------------------------------------------- */

static RBindType *r_bind_find_type(RBindGenerator *generator, CXCursor canonical) {
    size_t index;

    for (index = 0U; index < generator->type_count; ++index) {
        if (clang_equalCursors(generator->types[index].canonical, canonical) != 0) {
            return &generator->types[index];
        }
    }
    return NULL;
}

/* The tag of a declaration, or "" when only a typedef names it. */
static char *r_bind_tag(RBindGenerator *generator, CXCursor declaration) {
    static const char *const keywords[] = {"struct ", "union ", "enum "};
    CXString spelling;
    const char *text;
    char *tag = NULL;
    size_t index;

    if (clang_Cursor_isAnonymous(declaration) != 0U) {
        return r_copy_string(generator->context, "", 0U);
    }
    spelling = clang_getTypeSpelling(clang_getCursorType(declaration));
    text = clang_getCString(spelling);
    for (index = 0U; (text != NULL) && (index < sizeof(keywords) / sizeof(keywords[0])); ++index) {
        const size_t length = strlen(keywords[index]);

        if (strncmp(text, keywords[index], length) == 0) {
            tag = r_copy_string(generator->context, text + length, strlen(text + length));
            break;
        }
    }
    clang_disposeString(spelling);
    if (tag == NULL) {
        tag = r_copy_string(generator->context, "", 0U);
    }
    if (tag == NULL) {
        generator->exhausted = true;
    }
    return tag;
}

/* The type of a tag declaration, created on first use: the C spelling is the tag, or the
   typedef that names an anonymous tag. */
static RBindType *
r_bind_type_of(RBindGenerator *generator, CXCursor declaration, const char *typedef_name) {
    const CXCursor canonical = clang_getCanonicalCursor(declaration);
    const CXCursor definition = clang_getCursorDefinition(declaration);
    RBindType *type = r_bind_find_type(generator, canonical);
    char *tag;

    if (type != NULL) {
        return type;
    }
    tag = r_bind_tag(generator, declaration);
    if (tag == NULL) {
        return NULL;
    }
    if ((tag[0] == '\0') && (typedef_name == NULL)) {
        r_context_free(generator->context, tag);
        return NULL;
    }
    if (!r_grow_array(generator->context,
                      (void **)&generator->types,
                      &generator->type_capacity,
                      sizeof(RBindType),
                      generator->type_count + 1U)) {
        generator->exhausted = true;
        r_context_free(generator->context, tag);
        return NULL;
    }
    type = &generator->types[generator->type_count++];
    (void)memset(type, 0, sizeof(*type));
    type->canonical = canonical;
    type->has_definition = clang_Cursor_isNull(definition) == 0;
    type->definition = definition;
    type->is_enum = clang_getCursorKind(declaration) == CXCursor_EnumDecl;
    type->is_union = clang_getCursorKind(declaration) == CXCursor_UnionDecl;
    type->in_headers = r_bind_in_headers(generator, declaration);
    {
        size_t alias;

        for (alias = 0U; alias < generator->alias_count; ++alias) {
            if (clang_equalCursors(generator->aliased[alias], canonical) != 0) {
                typedef_name = generator->aliases[alias];
                break;
            }
        }
    }
    type->tag = r_copy_string(generator->context, tag, strlen(tag));
    if (type->tag == NULL) {
        generator->exhausted = true;
    }
    if ((tag[0] != '\0') && (typedef_name == NULL || !type->in_headers)) {
        type->c_name = tag;
        type->c_kind = type->is_enum ? "enum" : type->is_union ? "union" : "struct";
    } else if (typedef_name == NULL) {
        r_context_free(generator->context, tag);
        r_context_free(generator->context, type->c_name);
        generator->type_count -= 1U;
        return NULL;
    } else {
        r_context_free(generator->context, tag);
        type->c_name = r_copy_string(generator->context, typedef_name, strlen(typedef_name));
        type->c_kind = "typedef";
    }
    type->form = type->is_enum ? R_BIND_FORM_INTEGER : R_BIND_FORM_OPAQUE;
    if (type->c_name == NULL) {
        generator->exhausted = true;
        return NULL;
    }
    type->r_name = r_bind_claim(generator, type->c_name, "_type");
    return type->r_name != NULL ? type : NULL;
}

/* The spelling of a C integer of a canonical builtin type, or NULL. */
static const char *r_bind_builtin(CXType type) {
    switch (clang_getCanonicalType(type).kind) {
    case CXType_Bool:
        return "c_bool";
    case CXType_Char_S:
    case CXType_Char_U:
        return "c_char";
    case CXType_SChar:
        return "c_schar";
    case CXType_UChar:
        return "c_uchar";
    case CXType_Short:
        return "c_short";
    case CXType_UShort:
        return "c_ushort";
    case CXType_Int:
        return "c_int";
    case CXType_UInt:
        return "c_uint";
    case CXType_Long:
        return "c_long";
    case CXType_ULong:
        return "c_ulong";
    case CXType_LongLong:
        return "c_llong";
    case CXType_ULongLong:
        return "c_ullong";
    case CXType_Float:
        return "c_float";
    case CXType_Double:
        return "c_double";
    case CXType_LongDouble:
        return "c_long_double";
    case CXType_WChar:
        return "c_wchar";
    default:
        return NULL;
    }
}

/* The ABI types R predeclares for the C library's typedefs (R-FFI-0002). */
static const char *r_bind_known_typedef(const char *name) {
    static const char *const pairs[][2] = {
        {"size_t", "c_size"},
        {"ptrdiff_t", "c_ptrdiff"},
        {"intptr_t", "c_intptr"},
        {"uintptr_t", "c_uintptr"},
        {"intmax_t", "c_intmax"},
        {"uintmax_t", "c_uintmax"},
        {"wchar_t", "c_wchar"},
        {"wint_t", "c_wint"},
        {"int8_t", "c_int8"},
        {"uint8_t", "c_uint8"},
        {"int16_t", "c_int16"},
        {"uint16_t", "c_uint16"},
        {"int32_t", "c_int32"},
        {"uint32_t", "c_uint32"},
        {"int64_t", "c_int64"},
        {"uint64_t", "c_uint64"},
    };
    size_t index;

    for (index = 0U; index < sizeof(pairs) / sizeof(pairs[0]); ++index) {
        if (strcmp(pairs[index][0], name) == 0) {
            return pairs[index][1];
        }
    }
    return NULL;
}

static bool r_bind_spell(RBindGenerator *generator,
                         CXType type,
                         RBindPosition position,
                         RBindText *out,
                         const char **reason);

/* A C function pointer as `raw fn(P...) -> R`. */
static bool r_bind_spell_function(RBindGenerator *generator,
                                  CXType function,
                                  RBindText *out,
                                  const char **reason) {
    const int count = clang_getNumArgTypes(function);
    int index;

    if (function.kind != CXType_FunctionProto) {
        *reason = "a function pointer without a prototype";
        return false;
    }
    if (clang_isFunctionTypeVariadic(function) != 0U) {
        *reason = "a variadic function pointer (R-FFI-0019)";
        return false;
    }
    if (!r_bind_append(out, "raw fn(")) {
        return false;
    }
    for (index = 0; index < count; ++index) {
        if (((index != 0) && !r_bind_append(out, ", ")) ||
            !r_bind_spell(generator,
                          clang_getArgType(function, (unsigned)index),
                          R_BIND_PARAMETER,
                          out,
                          reason)) {
            return false;
        }
    }
    return r_bind_append(out, ") -> ") &&
           r_bind_spell(generator, clang_getResultType(function), R_BIND_RESULT, out, reason);
}

/* One C type as R spells it at a position; false with the reason when R cannot. */
static bool r_bind_spell(RBindGenerator *generator,
                         CXType type,
                         RBindPosition position,
                         RBindText *out,
                         const char **reason) {
    const char *builtin;
    RBindType *bound;

    /* A qualifier of a parameter itself is not part of the function type (C17 6.7.6.3). */
    if ((position != R_BIND_PARAMETER) && ((clang_isVolatileQualifiedType(type) != 0U) ||
                                           (clang_isRestrictQualifiedType(type) != 0U))) {
        *reason = "a volatile or restrict type (R-FFI-0019)";
        return false;
    }
    if ((position == R_BIND_MEMBER) && (clang_isConstQualifiedType(type) != 0U)) {
        *reason = "a const member";
        return false;
    }
    switch (type.kind) {
    case CXType_Elaborated:
        return r_bind_spell(generator, clang_Type_getNamedType(type), position, out, reason);
    case CXType_Attributed:
        return r_bind_spell(generator, clang_Type_getModifiedType(type), position, out, reason);
    case CXType_Typedef: {
        CXString name = clang_getTypedefName(type);
        const char *known = r_bind_known_typedef(clang_getCString(name));
        const CXType canonical = clang_getCanonicalType(type);

        if (known != NULL) {
            clang_disposeString(name);
            return r_bind_append(out, known);
        }
        if ((canonical.kind == CXType_Record) || (canonical.kind == CXType_Enum)) {
            bound = r_bind_type_of(
                generator, clang_getTypeDeclaration(canonical), clang_getCString(name));
            clang_disposeString(name);
            if (bound == NULL) {
                *reason = "a type the generator cannot name";
                return false;
            }
            break;
        }
        clang_disposeString(name);
        return r_bind_spell(generator,
                            clang_getTypedefDeclUnderlyingType(clang_getTypeDeclaration(type)),
                            position,
                            out,
                            reason);
    }
    case CXType_Void:
        if ((position != R_BIND_RESULT) && (position != R_BIND_POINTEE)) {
            *reason = "a void value";
            return false;
        }
        return r_bind_append(out, "void");
    case CXType_Pointer: {
        const CXType pointee = clang_getPointeeType(type);
        const CXType canonical = clang_getCanonicalType(pointee);
        RBindText inner;
        bool spelled;

        if ((canonical.kind == CXType_FunctionProto) ||
            (canonical.kind == CXType_FunctionNoProto)) {
            return r_bind_spell_function(generator, canonical, out, reason);
        }
        if ((clang_isVolatileQualifiedType(pointee) != 0U) ||
            (clang_isRestrictQualifiedType(pointee) != 0U)) {
            *reason = "a pointer to a volatile or restrict type (R-FFI-0019)";
            return false;
        }
        r_bind_text_init(&inner, generator->context);
        spelled = r_bind_spell(generator, pointee, R_BIND_POINTEE, &inner, reason);
        if (spelled) {
            const bool nested = (strncmp(inner.bytes, "raw ", 4U) == 0);

            spelled =
                r_bind_append(out, "raw ") &&
                ((clang_isConstQualifiedType(pointee) == 0U) || r_bind_append(out, "const ")) &&
                (!nested || r_bind_append(out, "(")) && r_bind_append(out, inner.bytes) &&
                (!nested || r_bind_append(out, ")")) && r_bind_append(out, "*");
        }
        r_context_free(generator->context, inner.bytes);
        return spelled;
    }
    case CXType_Record:
    case CXType_Enum:
        bound = r_bind_type_of(generator, clang_getTypeDeclaration(type), NULL);
        if (bound == NULL) {
            *reason = "an anonymous struct, union or enumeration";
            return false;
        }
        break;
    case CXType_ConstantArray: {
        char length[32];

        if (position != R_BIND_MEMBER) {
            *reason = "an array outside a struct";
            return false;
        }
        (void)snprintf(length, sizeof(length), "[%lld]", clang_getArraySize(type));
        return r_bind_spell(
                   generator, clang_getArrayElementType(type), R_BIND_OBJECT, out, reason) &&
               r_bind_append(out, length);
    }
    default:
        builtin = r_bind_builtin(type);
        if ((builtin == NULL) || (type.kind == CXType_Unexposed)) {
            *reason = "a C type R has no form for";
            return false;
        }
        return r_bind_append(out, builtin);
    }
    /* A struct, union or enumeration the bindings declare. */
    if ((strcmp(bound->c_kind, "typedef") == 0) || r_semantic_c_name_reserved(bound->c_name)) {
        generator->mentions_private = true;
    }
    if (bound->is_enum) {
        if (bound->form == R_BIND_FORM_ENUM) {
            return r_bind_append(out, bound->r_name);
        }
        return r_bind_append(out, bound->integer != NULL ? bound->integer : "c_int");
    }
    if ((position != R_BIND_POINTEE) && (bound->form != R_BIND_FORM_STRUCT)) {
        *reason = bound->is_union ? "a union by value" : "an opaque struct by value";
        return false;
    }
    return r_bind_append(out, bound->r_name);
}

/* ---- struct and enumeration forms ---------------------------------------------------- */

typedef struct RBindAttributeWalk {
    RBindGenerator *generator;
    bool packed;
    /* The argument of an `aligned` or `_Alignas` attribute; -1 when it is no integer literal. */
    long long align;
} RBindAttributeWalk;

static enum CXChildVisitResult
r_bind_attribute_visit(CXCursor cursor, CXCursor parent, CXClientData data) {
    RBindAttributeWalk *walk = data;
    CXToken *tokens = NULL;
    unsigned count = 0U;
    unsigned used;
    unsigned index;
    long long value = -1;

    if (clang_getCursorKind(cursor) == CXCursor_PackedAttr) {
        walk->packed = true;
        return CXChildVisit_Continue;
    }
    if (clang_getCursorKind(cursor) != CXCursor_AlignedAttr) {
        return CXChildVisit_Continue;
    }
    clang_tokenize(walk->generator->unit, clang_getCursorExtent(cursor), &tokens, &count);
    used = count;
    if ((count == 1U) && (clang_getTokenKind(tokens[0]) == CXToken_Keyword)) {
        /* libclang's extent of `_Alignas(N)` is the keyword alone: read on to the end of the
           declaration it belongs to, and take the parenthesized argument. */
        const CXSourceRange extent =
            clang_getRange(clang_getRangeStart(clang_getCursorExtent(cursor)),
                           clang_getRangeEnd(clang_getCursorExtent(parent)));
        clang_disposeTokens(walk->generator->unit, tokens, count);
        clang_tokenize(walk->generator->unit, extent, &tokens, &count);
        used = 0U;
        if (count >= 4U) {
            CXString close = clang_getTokenSpelling(walk->generator->unit, tokens[3]);
            used = (strcmp(clang_getCString(close), ")") == 0) ? 4U : 0U;
            clang_disposeString(close);
        }
    }
    for (index = 0U; index < used; ++index) {
        if (clang_getTokenKind(tokens[index]) == CXToken_Literal) {
            CXString spelling = clang_getTokenSpelling(walk->generator->unit, tokens[index]);
            const char *text = clang_getCString(spelling);
            char *end = NULL;
            const long long parsed = (text == NULL) ? -1 : strtoll(text, &end, 0);
            value =
                ((value == -1) && (end != NULL) && (*end == '\0') && (parsed > 0)) ? parsed : -2;
            clang_disposeString(spelling);
        }
    }
    clang_disposeTokens(walk->generator->unit, tokens, count);
    /* Two alignment attributes, or one without a literal, are not read. */
    walk->align = ((walk->align == 0) && (value > 0)) ? value : -1;
    return CXChildVisit_Continue;
}

static RBindAttributeWalk r_bind_attributes(RBindGenerator *generator, CXCursor cursor) {
    RBindAttributeWalk walk;

    walk.generator = generator;
    walk.packed = false;
    walk.align = 0;
    (void)clang_visitChildren(cursor, r_bind_attribute_visit, &walk);
    return walk;
}

typedef struct RBindFieldWalk {
    RBindGenerator *generator;
    RBindText *out;
    const char *reason;
    bool expressible;
    /* The largest alignment of a member as it lies in the aggregate (R-FFI-0061). */
    long long natural;
    bool packed;
} RBindFieldWalk;

static enum CXChildVisitResult
r_bind_field_visit(CXCursor cursor, CXCursor parent, CXClientData data) {
    RBindFieldWalk *walk = data;
    const enum CXCursorKind kind = clang_getCursorKind(cursor);
    CXString name;
    const char *spelled;
    bool written;

    (void)parent;
    /* A tag a member names first is declared there without a definition; only a definition
       inside the struct is a nested aggregate. */
    if (((kind == CXCursor_StructDecl) || (kind == CXCursor_UnionDecl) ||
         (kind == CXCursor_EnumDecl)) &&
        (clang_isCursorDefinition(cursor) != 0U)) {
        walk->reason = "a nested declaration";
        walk->expressible = false;
        return CXChildVisit_Break;
    }
    if (kind != CXCursor_FieldDecl) {
        return CXChildVisit_Continue;
    }
    {
        /* R-FFI-0061: a member alignment written as an integer literal becomes @align(N). */
        const RBindAttributeWalk attributes = r_bind_attributes(walk->generator, cursor);
        const long long type_align = clang_Type_getAlignOf(clang_getCursorType(cursor));
        long long member_align = walk->packed ? 1 : type_align;
        if ((attributes.align < 0) || (type_align <= 0)) {
            walk->reason = "a member alignment R cannot read";
            walk->expressible = false;
            return CXChildVisit_Break;
        }
        if (attributes.align > member_align) {
            member_align = attributes.align;
        }
        if (member_align > walk->natural) {
            walk->natural = member_align;
        }
        if ((attributes.align > 0) && (walk->out != NULL) &&
            !r_bind_appendf(walk->out, "        @align(%lld)\n", attributes.align)) {
            walk->expressible = false;
            return CXChildVisit_Break;
        }
    }
    name = clang_getCursorSpelling(cursor);
    spelled = clang_getCString(name);
    if (!r_bind_valid_name(spelled)) {
        clang_disposeString(name);
        walk->reason = "a member without a name R can spell";
        walk->expressible = false;
        return CXChildVisit_Break;
    }
    written = (walk->out == NULL) || r_bind_append(walk->out, "        ");
    if (walk->out == NULL) {
        RBindText probe;

        r_bind_text_init(&probe, walk->generator->context);
        written = r_bind_spell(
            walk->generator, clang_getCursorType(cursor), R_BIND_MEMBER, &probe, &walk->reason);
        r_context_free(walk->generator->context, probe.bytes);
    } else {
        written = written &&
                  r_bind_spell(walk->generator,
                               clang_getCursorType(cursor),
                               R_BIND_MEMBER,
                               walk->out,
                               &walk->reason) &&
                  r_bind_append(walk->out, " ") && r_bind_append(walk->out, spelled) &&
                  ((clang_Cursor_isBitField(cursor) == 0U) ||
                   r_bind_appendf(walk->out, " : %d", clang_getFieldDeclBitWidth(cursor))) &&
                  r_bind_append(walk->out, ";\n");
    }
    if (written && (clang_Cursor_isBitField(cursor) != 0U)) {
        /* R-FFI-0062: a bit-field of an integer type or _Bool; an enumeration is not one. */
        const enum CXTypeKind field_kind = clang_getCanonicalType(clang_getCursorType(cursor)).kind;
        if ((field_kind == CXType_Enum) || (field_kind == CXType_Float) ||
            (field_kind == CXType_Double) || (field_kind == CXType_LongDouble)) {
            walk->reason = "a bit-field of a type that is not an integer";
            written = false;
        }
    }
    clang_disposeString(name);
    if (!written) {
        walk->expressible = false;
        return CXChildVisit_Break;
    }
    return CXChildVisit_Continue;
}

/* Whether the members of a struct have R forms; the reason when one has not. */
static bool r_bind_struct_expressible(RBindGenerator *generator, RBindType *type) {
    RBindFieldWalk walk;
    const long long size = clang_Type_getSizeOf(clang_getCursorType(type->definition));
    const long long align = clang_Type_getAlignOf(clang_getCursorType(type->definition));
    const RBindAttributeWalk attributes = r_bind_attributes(generator, type->definition);

    walk.generator = generator;
    walk.out = NULL;
    walk.reason = NULL;
    walk.expressible = attributes.align >= 0;
    walk.natural = 1;
    walk.packed = attributes.packed;
    if (!walk.expressible) {
        walk.reason = "an alignment R cannot read";
    } else {
        (void)clang_visitChildren(type->definition, r_bind_field_visit, &walk);
    }
    if (walk.expressible && (size <= 0)) {
        walk.reason = "a struct without members";
        walk.expressible = false;
    }
    /* R-FFI-0061: an alignment above the members' is the aggregate's own. */
    type->packed = attributes.packed;
    type->align = (walk.expressible && (align > walk.natural)) ? align : 0;
    if (!walk.expressible) {
        r_context_free(generator->context, type->reason);
        type->reason = r_copy_string(generator->context, walk.reason, strlen(walk.reason));
    }
    return walk.expressible;
}

typedef struct RBindEnumWalk {
    long long values[512];
    size_t count;
    bool unique;
    bool named;
} RBindEnumWalk;

static enum CXChildVisitResult
r_bind_enum_visit(CXCursor cursor, CXCursor parent, CXClientData data) {
    RBindEnumWalk *walk = data;
    CXString name;
    size_t index;
    long long value;

    (void)parent;
    if (clang_getCursorKind(cursor) != CXCursor_EnumConstantDecl) {
        return CXChildVisit_Continue;
    }
    name = clang_getCursorSpelling(cursor);
    walk->named = walk->named && r_bind_valid_name(clang_getCString(name));
    clang_disposeString(name);
    value = clang_getEnumConstantDeclValue(cursor);
    for (index = 0U; index < walk->count; ++index) {
        walk->unique = walk->unique && (walk->values[index] != value);
    }
    if (walk->count < sizeof(walk->values) / sizeof(walk->values[0])) {
        walk->values[walk->count++] = value;
    } else {
        walk->unique = false;
    }
    return CXChildVisit_Continue;
}

/* Decides the R form of every type the headers declare. A struct is complete when the block names
   an @abi record and every member has a form, which may depend on other structs: the forms are
   decided until none changes. */
static void r_bind_decide_forms(RBindGenerator *generator) {
    size_t index;
    bool changed = true;

    for (index = 0U; index < generator->type_count; ++index) {
        RBindType *type = &generator->types[index];

        if (type->is_enum && type->has_definition) {
            RBindEnumWalk walk;

            walk.count = 0U;
            walk.unique = true;
            walk.named = true;
            type->integer = r_bind_builtin(clang_getEnumDeclIntegerType(type->definition));
            (void)clang_visitChildren(type->definition, r_bind_enum_visit, &walk);
            if (generator->complete_allowed && walk.unique && walk.named && (walk.count != 0U) &&
                (type->integer != NULL)) {
                type->form = R_BIND_FORM_ENUM;
            } else {
                type->form = R_BIND_FORM_INTEGER;
            }
        } else if (!type->is_enum && type->has_definition && generator->complete_allowed) {
            type->form = R_BIND_FORM_STRUCT;
        }
    }
    while (changed) {
        changed = false;
        for (index = 0U; index < generator->type_count; ++index) {
            RBindType *type = &generator->types[index];

            if ((type->form == R_BIND_FORM_STRUCT) && !r_bind_struct_expressible(generator, type)) {
                type->form = R_BIND_FORM_OPAQUE;
                changed = true;
            }
        }
    }
}

/* ---- declarations -------------------------------------------------------------------- */

static bool r_bind_skip(RBindGenerator *generator, const char *name, const char *reason) {
    return r_bind_appendf(&generator->skipped_text, "    // %s: %s\n", name, reason);
}

static bool r_bind_emit_type(RBindGenerator *generator, RBindType *type) {
    RBindText *out = &generator->types_text;

    if (type->form == R_BIND_FORM_INTEGER) {
        return true;
    }
    if (type->form == R_BIND_FORM_OPAQUE) {
        if ((type->reason != NULL) && !r_bind_appendf(out, "    // Opaque: %s.\n", type->reason)) {
            return false;
        }
        return r_bind_appendf(
            out,
            "    @c_type(name = \"%s\", kind = \"%s\")\n    opaque struct %s;\n\n",
            type->c_name,
            type->c_kind,
            type->r_name);
    }
    if (type->form == R_BIND_FORM_STRUCT) {
        RBindFieldWalk walk;

        char representation[64];

        /* R-AGG-0014, R-FFI-0061: a union is declared with its keyword; packing and an
           alignment of the aggregate are representation arguments. */
        if (type->align > 0) {
            (void)snprintf(representation,
                           sizeof(representation),
                           "C%s, align = %lld",
                           type->packed ? ", packed" : "",
                           type->align);
        } else {
            (void)snprintf(
                representation, sizeof(representation), "C%s", type->packed ? ", packed" : "");
        }
        if (!r_bind_appendf(
                out,
                "    @repr(%s)\n    @c_type(name = \"%s\", kind = \"%s\")\n    %s %s {\n",
                representation,
                type->c_name,
                type->c_kind,
                type->is_union ? "union" : "struct",
                type->r_name)) {
            return false;
        }
        walk.generator = generator;
        walk.out = out;
        walk.reason = NULL;
        walk.expressible = true;
        walk.natural = 1;
        walk.packed = type->packed;
        (void)clang_visitChildren(type->definition, r_bind_field_visit, &walk);
        return walk.expressible && r_bind_append(out, "    };\n\n");
    }
    return true;
}

typedef struct RBindEnumeratorWalk {
    RBindGenerator *generator;
    RBindText *out;
    bool as_constants;
} RBindEnumeratorWalk;

/* The literal of an integer value of an R ABI integer type: `-7i32 as c_int`. */
static const char *r_bind_suffix(const char *integer) {
    static const char *const pairs[][2] = {
        {"c_char", "i8"},
        {"c_schar", "i8"},
        {"c_uchar", "u8"},
        {"c_short", "i16"},
        {"c_ushort", "u16"},
        {"c_int", "i32"},
        {"c_uint", "u32"},
        {"c_long", "i64"},
        {"c_ulong", "u64"},
        {"c_llong", "i64"},
        {"c_ullong", "u64"},
        {"c_wchar", "i32"},
    };
    size_t index;

    for (index = 0U; index < sizeof(pairs) / sizeof(pairs[0]); ++index) {
        if (strcmp(pairs[index][0], integer) == 0) {
            return pairs[index][1];
        }
    }
    return NULL;
}

static bool r_bind_constant(RBindGenerator *generator,
                            const char *c_name,
                            const char *integer,
                            bool is_unsigned,
                            long long value,
                            unsigned long long unsigned_value) {
    const char *suffix = r_bind_suffix(integer);
    char *r_name;

    if (suffix == NULL) {
        return r_bind_skip(generator, c_name, "an integer type without an R literal");
    }
    r_name = r_bind_claim(generator, c_name, "_value");
    if (r_name == NULL) {
        return generator->exhausted ? false : r_bind_skip(generator, c_name, "no R name");
    }
    if (is_unsigned) {
        return r_bind_appendf(&generator->constants_text,
                              "    @c_constant(name = \"%s\")\n    const %s %s = %llu%s as %s;\n\n",
                              c_name,
                              integer,
                              r_name,
                              unsigned_value,
                              suffix,
                              integer);
    }
    return r_bind_appendf(&generator->constants_text,
                          "    @c_constant(name = \"%s\")\n    const %s %s = %lld%s as %s;\n\n",
                          c_name,
                          integer,
                          r_name,
                          value,
                          suffix,
                          integer);
}

static enum CXChildVisitResult
r_bind_enumerator_visit(CXCursor cursor, CXCursor parent, CXClientData data) {
    RBindEnumeratorWalk *walk = data;
    CXString name;
    const char *spelled;
    bool written;

    (void)parent;
    if (clang_getCursorKind(cursor) != CXCursor_EnumConstantDecl) {
        return CXChildVisit_Continue;
    }
    name = clang_getCursorSpelling(cursor);
    spelled = clang_getCString(name);
    if (walk->as_constants) {
        /* In C17 an enumeration constant has the type int, not its enumeration's type. */
        CXType constant = clang_getCursorType(cursor);
        const char *integer;
        bool is_unsigned;

        if (clang_getCanonicalType(constant).kind == CXType_Enum) {
            constant = clang_getEnumDeclIntegerType(
                clang_getTypeDeclaration(clang_getCanonicalType(constant)));
        }
        integer = r_bind_builtin(constant);
        is_unsigned = (integer != NULL) && (integer[0] == 'c') && (integer[2] == 'u');
        written = r_bind_constant(walk->generator,
                                  spelled,
                                  integer != NULL ? integer : "c_int",
                                  is_unsigned,
                                  clang_getEnumConstantDeclValue(cursor),
                                  clang_getEnumConstantDeclUnsignedValue(cursor));
    } else {
        written = r_bind_appendf(
            walk->out, "        %s = %lld,\n", spelled, clang_getEnumConstantDeclValue(cursor));
    }
    clang_disposeString(name);
    return written ? CXChildVisit_Continue : CXChildVisit_Break;
}

static bool r_bind_emit_enum(RBindGenerator *generator, RBindType *type) {
    RBindEnumeratorWalk walk;

    walk.generator = generator;
    walk.out = &generator->types_text;
    walk.as_constants = type->form != R_BIND_FORM_ENUM;
    if (!walk.as_constants &&
        !r_bind_appendf(
            &generator->types_text,
            "    @repr(C)\n    @c_type(name = \"%s\", kind = \"%s\")\n    enum %s : %s {\n",
            type->c_name,
            type->c_kind,
            type->r_name,
            type->integer)) {
        return false;
    }
    (void)clang_visitChildren(type->definition, r_bind_enumerator_visit, &walk);
    return walk.as_constants || r_bind_append(&generator->types_text, "    };\n\n");
}

/* An object-like macro whose expansion is an integer constant becomes a verified constant
   (R-FFI-0018); the value and type come from a probe the generator parses. */
static bool r_bind_macro_tokens_simple(CXTranslationUnit unit, CXCursor cursor) {
    CXToken *tokens = NULL;
    unsigned count = 0U;
    unsigned index;
    bool simple = true;

    clang_tokenize(unit, clang_getCursorExtent(cursor), &tokens, &count);
    if (count < 2U) {
        simple = false;
    }
    for (index = 1U; simple && (index < count); ++index) {
        const CXTokenKind kind = clang_getTokenKind(tokens[index]);

        if (kind == CXToken_Punctuation) {
            CXString spelling = clang_getTokenSpelling(unit, tokens[index]);
            const char *text = clang_getCString(spelling);

            simple = (text != NULL) && (strchr("{};[]#\"'", text[0]) == NULL);
            clang_disposeString(spelling);
        } else {
            simple = (kind == CXToken_Literal) || (kind == CXToken_Identifier);
        }
        if (simple && (kind == CXToken_Literal)) {
            CXString spelling = clang_getTokenSpelling(unit, tokens[index]);
            const char *text = clang_getCString(spelling);

            simple = (text != NULL) && (text[0] != '"') && (text[0] != '\'');
            clang_disposeString(spelling);
        }
    }
    clang_disposeTokens(unit, tokens, count);
    return simple;
}

typedef struct RBindProbeWalk {
    RBindGenerator *generator;
    char **names;
    size_t count;
} RBindProbeWalk;

static enum CXChildVisitResult
r_bind_probe_visit(CXCursor cursor, CXCursor parent, CXClientData data) {
    RBindProbeWalk *walk = data;
    CXString spelling;
    const char *name;
    unsigned position = 0U;

    (void)parent;
    if (clang_getCursorKind(cursor) != CXCursor_VarDecl) {
        return CXChildVisit_Continue;
    }
    spelling = clang_getCursorSpelling(cursor);
    name = clang_getCString(spelling);
    if ((name != NULL) && (sscanf(name, "r_binding_probe_%u", &position) == 1) &&
        (position < walk->count)) {
        CXEvalResult result = clang_Cursor_Evaluate(cursor);
        const char *integer = r_bind_builtin(clang_getCursorType(cursor));

        if ((result != NULL) && (clang_EvalResult_getKind(result) == CXEval_Int) &&
            (integer != NULL) && (strcmp(integer, "c_bool") != 0) &&
            (strcmp(integer, "c_float") != 0) && (strcmp(integer, "c_double") != 0) &&
            (strcmp(integer, "c_long_double") != 0)) {
            const bool is_unsigned = clang_EvalResult_isUnsignedInt(result) != 0U;

            if (!r_bind_constant(walk->generator,
                                 walk->names[position],
                                 integer,
                                 is_unsigned,
                                 clang_EvalResult_getAsLongLong(result),
                                 clang_EvalResult_getAsUnsigned(result))) {
                walk->generator->exhausted =
                    walk->generator->exhausted || walk->generator->constants_text.failed;
            }
        }
        if (result != NULL) {
            clang_EvalResult_dispose(result);
        }
    }
    clang_disposeString(spelling);
    return CXChildVisit_Continue;
}

static bool r_bind_macros(RBindGenerator *generator,
                          const RFrontendHeaderOptions *options,
                          const char *prelude,
                          size_t prelude_length) {
    RBindText probe;
    RBindProbeWalk walk;
    size_t name_capacity = 0U;
    size_t index;
    CXIndex clang_index = NULL;
    CXTranslationUnit unit = NULL;
    RFrontendStatus status;

    r_bind_text_init(&probe, generator->context);
    walk.generator = generator;
    walk.names = NULL;
    walk.count = 0U;
    (void)r_bind_append_bytes(&probe, prelude, prelude_length);
    for (index = 0U; index < generator->item_count; ++index) {
        const CXCursor cursor = generator->items[index];
        char *name;

        if ((clang_getCursorKind(cursor) != CXCursor_MacroDefinition) ||
            (clang_Cursor_isMacroFunctionLike(cursor) != 0U) ||
            (clang_Cursor_isMacroBuiltin(cursor) != 0U) ||
            !r_bind_macro_tokens_simple(generator->unit, cursor)) {
            continue;
        }
        name = r_bind_take(generator, clang_getCursorSpelling(cursor));
        if ((name == NULL) || !r_grow_array(generator->context,
                                            (void **)&walk.names,
                                            &name_capacity,
                                            sizeof(char *),
                                            walk.count + 1U)) {
            r_context_free(generator->context, name);
            generator->exhausted = true;
            break;
        }
        walk.names[walk.count] = name;
        (void)r_bind_appendf(&probe,
                             "static const __typeof__((%s)) r_binding_probe_%zu = (%s);\n",
                             name,
                             walk.count,
                             name);
        walk.count += 1U;
    }
    if (!generator->exhausted && !probe.failed && (walk.count != 0U)) {
        status = r_clang_parse(generator->context,
                               options,
                               probe.bytes,
                               probe.length,
                               CXTranslationUnit_None,
                               &clang_index,
                               &unit);
        if (status == R_FRONTEND_OK) {
            (void)clang_visitChildren(
                clang_getTranslationUnitCursor(unit), r_bind_probe_visit, &walk);
        } else {
            generator->exhausted = status == R_FRONTEND_OUT_OF_MEMORY;
        }
    }
    if (unit != NULL) {
        clang_disposeTranslationUnit(unit);
    }
    if (clang_index != NULL) {
        clang_disposeIndex(clang_index);
    }
    for (index = 0U; index < walk.count; ++index) {
        r_context_free(generator->context, walk.names[index]);
    }
    r_context_free(generator->context, walk.names);
    r_context_free(generator->context, probe.bytes);
    return !generator->exhausted && !probe.failed;
}

/* An object the headers declare extern (R-FFI-0022). */
static bool r_bind_object(RBindGenerator *generator, CXCursor cursor) {
    RBindText type;
    const char *reason = NULL;
    const CXType object_type = clang_getCursorType(cursor);
    char *c_name;
    char *r_name;
    bool written;

    if (clang_Cursor_getStorageClass(cursor) != CX_SC_Extern) {
        return true;
    }
    c_name = r_bind_take(generator, clang_getCursorSpelling(cursor));
    if (c_name == NULL) {
        return false;
    }
    r_bind_text_init(&type, generator->context);
    if (!r_bind_spell(generator, object_type, R_BIND_OBJECT, &type, &reason)) {
        written = (reason == NULL) ? false : r_bind_skip(generator, c_name, reason);
        r_context_free(generator->context, type.bytes);
        r_context_free(generator->context, c_name);
        return written;
    }
    r_name = r_bind_claim(generator, c_name, "_object");
    written = (r_name != NULL) &&
              ((strcmp(r_name, c_name) == 0) ||
               r_bind_appendf(&generator->objects_text, "    @link_name(\"%s\")\n", c_name)) &&
              r_bind_appendf(&generator->objects_text,
                             "    %s%s%s %s;\n\n",
                             clang_getCursorTLSKind(cursor) != CXTLS_None ? "thread_local " : "",
                             clang_isConstQualifiedType(object_type) != 0U ? "const " : "",
                             type.bytes,
                             r_name);
    r_context_free(generator->context, type.bytes);
    r_context_free(generator->context, c_name);
    return written;
}

/* A function the headers declare (R-FFI-0021); its contract is the reviewer's to state. */
static bool r_bind_function(RBindGenerator *generator, CXCursor cursor, const char *prefix) {
    const CXType function = clang_getCursorType(cursor);
    RBindText result;
    RBindText parameters;
    const char *reason = NULL;
    const char *skip = NULL;
    char *c_name;
    char *r_name = NULL;
    char identifier[256];
    CXString mangling;
    const char *linked;
    int count = 0;
    int index;
    size_t position;
    bool written = true;

    c_name = r_bind_take(generator, clang_getCursorSpelling(cursor));
    if (c_name == NULL) {
        return false;
    }
    mangling = clang_Cursor_getMangling(cursor);
    linked = clang_getCString(mangling);
    if ((clang_Cursor_getStorageClass(cursor) == CX_SC_Static) ||
        (clang_Cursor_isFunctionInlined(cursor) != 0U)) {
        skip = "an inline or static function (R-FFI-0020)";
    } else if ((linked == NULL) || (strncmp(linked, prefix, strlen(prefix)) != 0) ||
               (strcmp(linked + strlen(prefix), c_name) != 0)) {
        skip = "the header binds it to a decorated symbol (R-CMAP-0026)";
    } else if (function.kind != CXType_FunctionProto) {
        skip = "a function without a prototype";
    } else {
        count = clang_getNumArgTypes(function);
        if ((clang_isFunctionTypeVariadic(function) != 0U) && (count == 0)) {
            skip = "a variadic function without a fixed parameter";
        }
    }
    clang_disposeString(mangling);
    if (skip != NULL) {
        written = r_bind_skip(generator, c_name, skip);
        r_context_free(generator->context, c_name);
        return written;
    }
    r_bind_text_init(&result, generator->context);
    r_bind_text_init(&parameters, generator->context);
    generator->mentions_private = false;
    written =
        r_bind_spell(generator, clang_getResultType(function), R_BIND_RESULT, &result, &reason);
    for (index = 0; written && (index < count); ++index) {
        CXCursor argument = clang_Cursor_getArgument(cursor, (unsigned)index);
        CXString parameter = clang_getCursorSpelling(argument);
        const char *parameter_name = clang_getCString(parameter);
        char fallback[32];

        (void)snprintf(fallback, sizeof(fallback), "a%d", index);
        if ((parameter_name == NULL) || !r_bind_valid_name(parameter_name) ||
            r_bind_name_taken(generator, parameter_name) || (strcmp(parameter_name, c_name) == 0)) {
            parameter_name = fallback;
        }
        written = ((index == 0) || r_bind_append(&parameters, ", ")) &&
                  r_bind_spell(generator,
                               clang_getArgType(function, (unsigned)index),
                               R_BIND_PARAMETER,
                               &parameters,
                               &reason) &&
                  r_bind_append(&parameters, " ") && r_bind_append(&parameters, parameter_name);
        clang_disposeString(parameter);
    }
    if (written && (clang_isFunctionTypeVariadic(function) != 0U) &&
        (generator->mentions_private || r_semantic_c_name_reserved(c_name))) {
        /* A variadic call cannot be forwarded by the header-compatible path (R-FFI-0057). */
        written = false;
        reason = "a variadic function with a typedef or reserved C spelling (R-FFI-0057)";
    }
    if (!written) {
        bool skipped = !result.failed && !parameters.failed && (reason != NULL) &&
                       r_bind_skip(generator, c_name, reason);

        r_context_free(generator->context, result.bytes);
        r_context_free(generator->context, parameters.bytes);
        r_context_free(generator->context, c_name);
        return skipped;
    }
    r_name = r_bind_claim(generator, c_name, "_function");
    for (position = 0U; (c_name[position] != '\0') && (position + 1U < sizeof(identifier));
         ++position) {
        const char c = c_name[position];

        identifier[position] = (c == '_')                   ? '-'
                               : ((c >= 'a') && (c <= 'z')) ? (char)(c - 'a' + 'A')
                                                            : c;
    }
    identifier[position] = '\0';
    written =
        (r_name != NULL) &&
        ((strcmp(r_name, c_name) == 0) ||
         r_bind_appendf(&generator->functions_text, "    @link_name(\"%s\")\n", c_name)) &&
        r_bind_appendf(&generator->functions_text,
                       "    @safety(\"%s\", \"Not reviewed: the header states no contract\")\n",
                       identifier) &&
        r_bind_appendf(&generator->functions_text,
                       "    %s %s(%s%s);\n\n",
                       result.bytes != NULL ? result.bytes : "",
                       r_name,
                       parameters.bytes != NULL ? parameters.bytes : "",
                       (clang_isFunctionTypeVariadic(function) != 0U) ? ", ..." : "");
    r_context_free(generator->context, result.bytes);
    r_context_free(generator->context, parameters.bytes);
    r_context_free(generator->context, c_name);
    return written;
}

/* ---- the stub module ----------------------------------------------------------------- */

/* The empty extern "C" block the bindings fill: its index and the offsets of its braces. */
static bool
r_bind_stub(RFrontendContext *context, uint32_t *block_index, size_t *open, size_t *close) {
    size_t index;
    bool found = false;

    for (index = 0U; index < context->extern_block_count; ++index) {
        const RExternBlockRecord *block = &context->extern_blocks[index];
        const uint32_t candidate = (uint32_t)index + UINT32_C(1);
        const RSource *source = r_get_source_const(context, block->source);
        size_t cursor;
        size_t member;
        bool empty = block->header_count != UINT32_C(0);

        for (member = 0U; empty && (member < context->semantic_symbol_count); ++member) {
            empty = context->semantic_symbols[member].extern_block != candidate;
        }
        for (member = 0U; empty && (member < context->semantic_aggregate_count); ++member) {
            empty = context->semantic_aggregates[member].extern_block != candidate;
        }
        if (!empty || (source == NULL) || found) {
            if (empty && found) {
                return false;
            }
            continue;
        }
        cursor = block->span_start;
        while ((cursor < source->length) && (source->bytes[cursor] != '{')) {
            cursor += 1U;
        }
        *open = cursor;
        cursor += 1U;
        while ((cursor < source->length) &&
               ((source->bytes[cursor] == ' ') || (source->bytes[cursor] == '\n') ||
                (source->bytes[cursor] == '\t') || (source->bytes[cursor] == '\r'))) {
            cursor += 1U;
        }
        if ((cursor >= source->length) || (source->bytes[cursor] != '}')) {
            continue;
        }
        *close = cursor;
        *block_index = candidate;
        found = true;
    }
    return found;
}

static bool
r_bind_prelude(RFrontendContext *context, const RExternBlockRecord *block, RBindText *out) {
    uint32_t header_index;
    size_t index;

    if ((block->link_provider != UINT32_C(0)) &&
        ((size_t)block->link_provider <= context->link_provider_count)) {
        const RLinkProviderRecord *provider =
            &context->link_providers[(size_t)block->link_provider - 1U];

        for (index = 0U; index < provider->definition_count; ++index) {
            const RLinkDefinitionRecord *definition =
                &context->link_definitions[provider->first_definition + index];

            if (!r_bind_append(out, "#define ") ||
                !r_bind_append_bytes(out, definition->name, definition->name_length) ||
                !r_bind_append(out, " ") ||
                !(definition->value_length != 0U
                      ? r_bind_append_bytes(out, definition->value, definition->value_length)
                      : r_bind_append(out, "1")) ||
                !r_bind_append(out, "\n")) {
                return false;
            }
        }
    }
    for (header_index = UINT32_C(0); header_index < block->header_count; ++header_index) {
        const size_t position = (size_t)block->first_header + (size_t)header_index;
        const RInternEntry *entry;

        if ((position >= context->extern_header_count) ||
            (context->extern_headers[position] == UINT32_C(0)) ||
            ((size_t)context->extern_headers[position] > context->intern_count)) {
            return false;
        }
        entry = &context->intern_entries[(size_t)context->extern_headers[position] - 1U];
        if (!r_bind_append(out, "#include <") ||
            !r_bind_append_bytes(out, entry->bytes, entry->length) || !r_bind_append(out, ">\n")) {
            return false;
        }
    }
    return true;
}

static void r_bind_dispose(RBindGenerator *generator) {
    RFrontendContext *context = generator->context;
    size_t index;

    for (index = 0U; index < generator->type_count; ++index) {
        r_context_free(context, generator->types[index].c_name);
        r_context_free(context, generator->types[index].tag);
        r_context_free(context, generator->types[index].reason);
    }
    for (index = 0U; index < generator->name_count; ++index) {
        r_context_free(context, generator->names[index]);
    }
    for (index = 0U; index < generator->alias_count; ++index) {
        r_context_free(context, generator->aliases[index]);
    }
    r_context_free(context, generator->aliased);
    r_context_free(context, generator->aliases);
    r_context_free(context, generator->types);
    r_context_free(context, generator->names);
    r_context_free(context, generator->items);
    r_context_free(context, generator->types_text.bytes);
    r_context_free(context, generator->constants_text.bytes);
    r_context_free(context, generator->objects_text.bytes);
    r_context_free(context, generator->functions_text.bytes);
    r_context_free(context, generator->skipped_text.bytes);
}

/* Appends a titled section; sections are separated by one blank line. */
static bool r_bind_section(RBindText *body, const char *title, const RBindText *text) {
    size_t length = text->length;

    if (length == 0U) {
        return true;
    }
    /* A declaration ends with a blank line, which the next section's title supplies. */
    if ((length > 1U) && (text->bytes[length - 1U] == '\n') && (text->bytes[length - 2U] == '\n')) {
        length -= 1U;
    }
    return r_bind_append(body, "\n    // ") && r_bind_append(body, title) &&
           r_bind_append(body, "\n\n") && r_bind_append_bytes(body, text->bytes, length);
}

RFrontendStatus r_frontend_emit_bindings(RFrontendContext *context,
                                         const RFrontendHeaderOptions *options,
                                         RFrontendWriteFn writer,
                                         void *user_data) {
    RBindGenerator generator;
    RBindText prelude;
    RBindText body;
    uint32_t block_index = UINT32_C(0);
    size_t open = 0U;
    size_t close = 0U;
    const RExternBlockRecord *block;
    const RSource *source;
    CXIndex clang_index = NULL;
    RFrontendStatus status;
    bool failed = false;
    bool written = true;
    char version[32];
    size_t index;

    if ((context == NULL) || (options == NULL) || (options->sysroot == NULL) || (writer == NULL) ||
        ((options->root_count != 0U) && (options->roots == NULL)) || !context->semantic_analyzed) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    if (!r_clang_check_toolchain(context, options)) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    context->header_report_length = 0U;
    if (!r_bind_stub(context, &block_index, &open, &close)) {
        (void)r_clang_report_append(
            context,
            "bindings: the module shall have exactly one empty extern \"C\" block with @header\n",
            sizeof("bindings: the module shall have exactly one empty extern \"C\" block with "
                   "@header\n") -
                1U);
        return R_FRONTEND_INVALID_SOURCE;
    }
    block = &context->extern_blocks[(size_t)block_index - 1U];
    source = r_get_source_const(context, block->source);
    (void)memset(&generator, 0, sizeof(generator));
    generator.context = context;
    generator.complete_allowed = block->abi_name_intern_id != UINT32_C(0);
    r_bind_text_init(&generator.types_text, context);
    r_bind_text_init(&generator.constants_text, context);
    r_bind_text_init(&generator.objects_text, context);
    r_bind_text_init(&generator.functions_text, context);
    r_bind_text_init(&generator.skipped_text, context);
    r_bind_text_init(&prelude, context);
    r_bind_text_init(&body, context);
    if (!r_bind_prelude(context, block, &prelude)) {
        r_context_free(context, prelude.bytes);
        return R_FRONTEND_OUT_OF_MEMORY;
    }
    status = r_clang_parse(context,
                           options,
                           prelude.bytes,
                           prelude.length,
                           CXTranslationUnit_DetailedPreprocessingRecord,
                           &clang_index,
                           &generator.unit);
    if ((status == R_FRONTEND_OK) && !r_clang_report_errors(context, generator.unit, &failed)) {
        status = R_FRONTEND_OUT_OF_MEMORY;
    }
    if ((status == R_FRONTEND_OK) && failed) {
        status = R_FRONTEND_INVALID_SOURCE;
    }
    if (status == R_FRONTEND_OK) {
        const CXCursor root = clang_getTranslationUnitCursor(generator.unit);

        (void)clang_visitChildren(root, r_bind_include_visit, &generator);
        (void)clang_visitChildren(root, r_bind_item_visit, &generator);
        /* A tag a typedef of the headers names is spelled by that typedef, as the headers
           spell it in members and prototypes (R-FFI-0017 compares spellings). */
        for (index = 0U; !generator.exhausted && (index < generator.item_count); ++index) {
            const CXCursor cursor = generator.items[index];
            const CXType underlying =
                clang_getCanonicalType(clang_getTypedefDeclUnderlyingType(cursor));
            CXCursor tag;
            size_t alias;
            bool known = false;

            if ((clang_getCursorKind(cursor) != CXCursor_TypedefDecl) ||
                ((underlying.kind != CXType_Record) && (underlying.kind != CXType_Enum))) {
                continue;
            }
            tag = clang_getCanonicalCursor(clang_getTypeDeclaration(underlying));
            for (alias = 0U; alias < generator.alias_count; ++alias) {
                known = known || (clang_equalCursors(generator.aliased[alias], tag) != 0);
            }
            if (known) {
                continue;
            }
            if (!r_grow_array(context,
                              (void **)&generator.aliased,
                              &generator.aliased_capacity,
                              sizeof(CXCursor),
                              generator.alias_count + 1U) ||
                !r_grow_array(context,
                              (void **)&generator.aliases,
                              &generator.alias_capacity,
                              sizeof(char *),
                              generator.alias_count + 1U)) {
                generator.exhausted = true;
                break;
            }
            generator.aliases[generator.alias_count] =
                r_bind_take(&generator, clang_getCursorSpelling(cursor));
            if (generator.aliases[generator.alias_count] == NULL) {
                break;
            }
            generator.aliased[generator.alias_count] = tag;
            generator.alias_count += 1U;
        }
        /* Every struct, union and enumeration of the headers, in their order. */
        for (index = 0U; !generator.exhausted && (index < generator.item_count); ++index) {
            const CXCursor cursor = generator.items[index];
            const enum CXCursorKind kind = clang_getCursorKind(cursor);

            if ((kind == CXCursor_StructDecl) || (kind == CXCursor_UnionDecl) ||
                (kind == CXCursor_EnumDecl)) {
                (void)r_bind_type_of(&generator, cursor, NULL);
            } else if (kind == CXCursor_TypedefDecl) {
                const CXType underlying =
                    clang_getCanonicalType(clang_getTypedefDeclUnderlyingType(cursor));

                if ((underlying.kind == CXType_Record) || (underlying.kind == CXType_Enum)) {
                    CXString name = clang_getCursorSpelling(cursor);

                    (void)r_bind_type_of(
                        &generator, clang_getTypeDeclaration(underlying), clang_getCString(name));
                    clang_disposeString(name);
                }
            }
        }
        r_bind_decide_forms(&generator);
        /* An opaque type names a C type for pointers only: its tag spells it everywhere, which
           keeps it out of the typedef spellings a variadic import cannot carry (R-FFI-0057). */
        for (index = 0U; !generator.exhausted && (index < generator.type_count); ++index) {
            RBindType *type = &generator.types[index];

            if ((type->form == R_BIND_FORM_OPAQUE) && (type->tag != NULL) &&
                (type->tag[0] != '\0') && (strcmp(type->c_kind, "typedef") == 0)) {
                char *tag = r_copy_string(context, type->tag, strlen(type->tag));

                if (tag == NULL) {
                    generator.exhausted = true;
                    break;
                }
                r_context_free(context, type->c_name);
                type->c_name = tag;
                type->c_kind = type->is_union ? "union" : "struct";
            }
        }
        for (index = 0U; !generator.exhausted && (index < generator.type_count); ++index) {
            RBindType *type = &generator.types[index];

            if (!type->in_headers) {
                continue;
            }
            written = written &&
                      (type->is_enum && type->has_definition ? r_bind_emit_enum(&generator, type)
                                                             : r_bind_emit_type(&generator, type));
        }
        /* The enumerators of an enumeration that neither a tag nor a typedef names. */
        for (index = 0U; written && !generator.exhausted && (index < generator.item_count);
             ++index) {
            const CXCursor cursor = generator.items[index];

            if ((clang_getCursorKind(cursor) == CXCursor_EnumDecl) &&
                (clang_isCursorDefinition(cursor) != 0U) &&
                (r_bind_find_type(&generator, clang_getCanonicalCursor(cursor)) == NULL)) {
                RBindEnumeratorWalk walk;

                walk.generator = &generator;
                walk.out = &generator.constants_text;
                walk.as_constants = true;
                (void)clang_visitChildren(cursor, r_bind_enumerator_visit, &walk);
            }
        }
        written = written && r_bind_macros(&generator, options, prelude.bytes, prelude.length);
        for (index = 0U; written && !generator.exhausted && (index < generator.item_count);
             ++index) {
            const CXCursor cursor = generator.items[index];

            if (clang_getCursorKind(cursor) == CXCursor_VarDecl) {
                written = r_bind_object(&generator, cursor);
            } else if ((clang_getCursorKind(cursor) == CXCursor_FunctionDecl) &&
                       (clang_equalCursors(clang_getCanonicalCursor(cursor), cursor) != 0)) {
                written = r_bind_function(&generator, cursor, r_frontend_target_global_prefix());
            }
        }
        /* Types reached from other headers, behind pointers only, become opaque. */
        for (index = 0U; written && !generator.exhausted && (index < generator.type_count);
             ++index) {
            RBindType *type = &generator.types[index];

            if (!type->in_headers && !type->is_enum) {
                written = r_bind_appendf(&generator.types_text,
                                         "    @c_type(name = \"%s\", kind = \"%s\")\n    opaque "
                                         "struct %s;\n\n",
                                         type->c_name,
                                         type->c_kind,
                                         type->r_name);
            }
        }
        if (generator.exhausted || !written || generator.types_text.failed ||
            generator.constants_text.failed || generator.objects_text.failed ||
            generator.functions_text.failed || generator.skipped_text.failed) {
            status = R_FRONTEND_OUT_OF_MEMORY;
        }
    }
    if ((status == R_FRONTEND_OK) && !r_frontend_header_version(version, sizeof(version))) {
        status = R_FRONTEND_INTERNAL_ERROR;
    }
    if ((status == R_FRONTEND_OK) &&
        (!r_bind_section(&body, "Types", &generator.types_text) ||
         !r_bind_section(&body, "Constants", &generator.constants_text) ||
         !r_bind_section(&body, "Objects", &generator.objects_text) ||
         !r_bind_section(&body, "Functions", &generator.functions_text) ||
         !r_bind_section(&body, "Not declared", &generator.skipped_text))) {
        status = R_FRONTEND_OUT_OF_MEMORY;
    }
    /* Every declaration ends with a blank line; the last one ends the block without it. */
    while ((body.length > 1U) && (body.bytes[body.length - 1U] == '\n') &&
           (body.bytes[body.length - 2U] == '\n')) {
        body.length -= 1U;
    }
    if (status == R_FRONTEND_OK) {
        const char *bytes = (const char *)source->bytes;

        written = writer(user_data, bytes, open + 1U) &&
                  r_write_text(writer,
                               user_data,
                               "\n    // Generated by r-front --emit=bindings with libclang ") &&
                  r_write_text(writer, user_data, version) &&
                  r_write_text(writer,
                               user_data,
                               " from the headers of this block (R-FFI-0009).\n    // Review "
                               "every declaration and state the contract of every @safety before "
                               "use.\n") &&
                  writer(user_data, body.bytes, body.length) &&
                  writer(user_data, bytes + close, source->length - close);
        if (!written) {
            status = R_FRONTEND_IO_ERROR;
        }
    }
    if (generator.unit != NULL) {
        clang_disposeTranslationUnit(generator.unit);
    }
    if (clang_index != NULL) {
        clang_disposeIndex(clang_index);
    }
    r_context_free(context, prelude.bytes);
    r_context_free(context, body.bytes);
    r_bind_dispose(&generator);
    return status;
}

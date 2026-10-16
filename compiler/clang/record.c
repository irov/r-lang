#include "clang_internal.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * ABI records through libclang (B9.1). For every record an extern "C" block names with @abi,
 * the generator parses the block's verified headers, with the provider's feature-test
 * definitions, for the target and the SDK, and writes the record of schema r-abi-record-0.1
 * (R-FFI-0040): the complete member inventory and layout of every requested struct and the
 * enumerator inventory and compatible integer type of every requested enumeration
 * (R-FFI-0017, R-FFI-0041); for every import that passes an R-declared @repr(C) struct, the C
 * type of the symbol as a type tree, and the inventories of every struct and enumeration reached
 * from it, transitively through members (R-FFI-0021). Nothing is executed (R-CMAP-0024): sizes,
 * alignments and offsets are the compiler's record layouts. Blocks that name one record share
 * it, with the union of their headers, definitions, types and symbols.
 */

typedef struct RClangRequestedType {
    uint32_t c_name;
    RCTypeKind kind;
} RClangRequestedType;

typedef struct RClangRequestedSymbol {
    uint32_t c_name;
    bool object;
} RClangRequestedSymbol;

/* One record to generate: the merged request of the blocks that name it. */
typedef struct RClangRequest {
    const char *name;
    size_t name_length;
    const char *provider;
    size_t provider_length;
    size_t *definitions;
    size_t definition_count;
    size_t definition_capacity;
    uint32_t *headers;
    size_t header_count;
    size_t header_capacity;
    RClangRequestedType *types;
    size_t type_count;
    size_t type_capacity;
    RClangRequestedSymbol *symbols;
    size_t symbol_count;
    size_t symbol_capacity;
} RClangRequest;

typedef enum RClangNodeKind {
    R_CLANG_NODE_VOID = 0,
    R_CLANG_NODE_SCALAR,
    R_CLANG_NODE_POINTER,
    R_CLANG_NODE_FUNCTION,
    R_CLANG_NODE_RECORD,
    R_CLANG_NODE_ENUM,
    R_CLANG_NODE_ARRAY,
    R_CLANG_NODE_OTHER,
    R_CLANG_NODE_ATOMIC
} RClangNodeKind;

/* One node of a type tree. Children are contiguous in RClangGenerator.children: the pointee,
   the element, the value type of an atomic, or the result followed by the parameters. */
typedef struct RClangNode {
    RClangNodeKind kind;
    bool is_const;
    bool is_union;
    bool complete;
    bool variadic;
    char *spelling;
    char *tag_name;
    char *typedef_name;
    CXCursor declaration;
    bool has_declaration;
    unsigned long long length;
    size_t first_child;
    size_t child_count;
} RClangNode;

typedef struct RClangMember {
    char *name;
    char *type;
    char *desugared;
    unsigned long long offset;
    /* R-FFI-0062: a bit-field lies at bit `bit_offset` (0..7) of the byte at `offset`. */
    unsigned bit_offset;
    unsigned bit_width;
    size_t tree;
} RClangMember;

typedef struct RClangEnumerator {
    char *name;
    bool is_unsigned;
    long long value;
    unsigned long long unsigned_value;
} RClangEnumerator;

/* One type inventory of the record: requested by a block or reached from a symbol. */
typedef struct RClangInventory {
    char *c_name;
    const char *kind;
    const char *tag;
    char *tag_name;
    char *inexpressible;
    bool is_struct;
    bool is_enum;
    bool emit_trees;
    /* Walked as reached from a symbol: its member leaves are pending or done. */
    bool reached;
    unsigned long long size;
    unsigned long long alignment;
    size_t first_member;
    size_t member_count;
    size_t first_enumerator;
    size_t enumerator_count;
    char *underlying;
} RClangInventory;

typedef struct RClangSymbol {
    const char *c_name;
    size_t c_name_length;
    bool object;
    size_t tree;
} RClangSymbol;

/* The declarations of the headers by name, as a block names them. */
typedef struct RClangDeclaration {
    CXCursor cursor;
    char *name;
    int kind;
} RClangDeclaration;

enum {
    R_CLANG_DECLARATION_STRUCT = 0,
    R_CLANG_DECLARATION_UNION,
    R_CLANG_DECLARATION_ENUM,
    R_CLANG_DECLARATION_TYPEDEF,
    R_CLANG_DECLARATION_FUNCTION,
    R_CLANG_DECLARATION_VARIABLE
};

typedef struct RClangGenerator {
    RFrontendContext *context;
    RClangNode *nodes;
    size_t node_count;
    size_t node_capacity;
    size_t *children;
    size_t child_count;
    size_t child_capacity;
    RClangMember *members;
    size_t member_count;
    size_t member_capacity;
    RClangEnumerator *enumerators;
    size_t enumerator_count;
    size_t enumerator_capacity;
    RClangInventory *inventories;
    size_t inventory_count;
    size_t inventory_capacity;
    RClangSymbol *symbols;
    size_t symbol_count;
    size_t symbol_capacity;
    RClangDeclaration *declarations;
    size_t declaration_count;
    size_t declaration_capacity;
    /* The first failure: why the record cannot be generated, and whether it is a resource one. */
    bool failed;
    bool exhausted;
} RClangGenerator;

/* ---- text ---------------------------------------------------------------------------- */

static bool r_clang_intern(const RFrontendContext *context,
                           uint32_t intern_id,
                           const char **bytes,
                           size_t *length) {
    const RInternEntry *entry;

    if ((intern_id == UINT32_C(0)) || ((size_t)intern_id > context->intern_count)) {
        return false;
    }
    entry = &context->intern_entries[(size_t)intern_id - 1U];
    *bytes = entry->bytes;
    *length = entry->length;
    return true;
}

static char *r_clang_copy(RClangGenerator *generator, const char *text) {
    char *copy = r_copy_string(generator->context, text, strlen(text));

    if (copy == NULL) {
        generator->exhausted = true;
    }
    return copy;
}

static char *r_clang_take(RClangGenerator *generator, CXString text) {
    const char *bytes = clang_getCString(text);
    char *copy = r_clang_copy(generator, bytes != NULL ? bytes : "");

    clang_disposeString(text);
    return copy;
}

static bool r_clang_fail(RClangGenerator *generator, const char *reason, const char *subject) {
    RFrontendContext *context = generator->context;

    if (!generator->failed) {
        generator->failed = true;
        if (!r_clang_report_append(context, reason, strlen(reason)) ||
            ((subject != NULL) && (!r_clang_report_append(context, ": ", 2U) ||
                                   !r_clang_report_append(context, subject, strlen(subject)))) ||
            !r_clang_report_append(context, "\n", 1U)) {
            generator->exhausted = true;
        }
    }
    return false;
}

/* The tag of a struct, union or enumeration declaration, or "" for one without a tag: libclang
   names a tag that only a typedef names by the typedef, but its type is spelled without the
   tag keyword then. */
static char *r_clang_tag_name(RClangGenerator *generator, CXCursor declaration) {
    static const char *const keywords[] = {"struct ", "union ", "enum "};
    CXString spelling;
    const char *text;
    char *name = NULL;
    size_t index;

    if (clang_Cursor_isAnonymous(declaration)) {
        return r_clang_copy(generator, "");
    }
    spelling = clang_getTypeSpelling(clang_getCursorType(declaration));
    text = clang_getCString(spelling);
    for (index = 0U; (text != NULL) && (index < sizeof(keywords) / sizeof(keywords[0])); ++index) {
        const size_t length = strlen(keywords[index]);

        if (strncmp(text, keywords[index], length) == 0) {
            name = r_clang_copy(generator, text + length);
            break;
        }
    }
    clang_disposeString(spelling);
    return name != NULL ? name : (generator->exhausted ? NULL : r_clang_copy(generator, ""));
}

/* ---- declarations of the headers ------------------------------------------------------- */

static enum CXChildVisitResult
r_clang_index_visit(CXCursor cursor, CXCursor parent, CXClientData data) {
    RClangGenerator *generator = data;
    const enum CXCursorKind kind = clang_getCursorKind(cursor);
    int declaration_kind = -1;

    (void)parent;
    switch (kind) {
    case CXCursor_StructDecl:
        declaration_kind = R_CLANG_DECLARATION_STRUCT;
        break;
    case CXCursor_UnionDecl:
        declaration_kind = R_CLANG_DECLARATION_UNION;
        break;
    case CXCursor_EnumDecl:
        declaration_kind = R_CLANG_DECLARATION_ENUM;
        break;
    case CXCursor_TypedefDecl:
        declaration_kind = R_CLANG_DECLARATION_TYPEDEF;
        break;
    case CXCursor_FunctionDecl:
        declaration_kind = R_CLANG_DECLARATION_FUNCTION;
        break;
    case CXCursor_VarDecl:
        declaration_kind = R_CLANG_DECLARATION_VARIABLE;
        break;
    default:
        return CXChildVisit_Continue;
    }
    /* A tag is indexed by its complete definition, an enumeration only with enumerators; the
       first declaration of a function or object names it. Bodies are not entered. */
    if (((declaration_kind == R_CLANG_DECLARATION_STRUCT) ||
         (declaration_kind == R_CLANG_DECLARATION_UNION) ||
         (declaration_kind == R_CLANG_DECLARATION_ENUM)) &&
        !clang_isCursorDefinition(cursor)) {
        return CXChildVisit_Continue;
    }
    {
        const bool tagged = (declaration_kind == R_CLANG_DECLARATION_STRUCT) ||
                            (declaration_kind == R_CLANG_DECLARATION_UNION) ||
                            (declaration_kind == R_CLANG_DECLARATION_ENUM);
        char *name = tagged ? r_clang_tag_name(generator, cursor)
                            : r_clang_take(generator, clang_getCursorSpelling(cursor));

        if ((name != NULL) && (name[0] == '\0')) {
            r_context_free(generator->context, name);
            return tagged && (kind != CXCursor_EnumDecl) ? CXChildVisit_Recurse
                                                         : CXChildVisit_Continue;
        }
        if ((name == NULL) || !r_grow_array(generator->context,
                                            (void **)&generator->declarations,
                                            &generator->declaration_capacity,
                                            sizeof(RClangDeclaration),
                                            generator->declaration_count + 1U)) {
            r_context_free(generator->context, name);
            generator->exhausted = true;
            return CXChildVisit_Break;
        }
        generator->declarations[generator->declaration_count].cursor = cursor;
        generator->declarations[generator->declaration_count].name = name;
        generator->declarations[generator->declaration_count].kind = declaration_kind;
        generator->declaration_count += 1U;
    }
    return (kind == CXCursor_StructDecl) || (kind == CXCursor_UnionDecl) ? CXChildVisit_Recurse
                                                                         : CXChildVisit_Continue;
}

static const RClangDeclaration *
r_clang_find(const RClangGenerator *generator, int kind, const char *name, size_t length) {
    size_t index;

    for (index = 0U; index < generator->declaration_count; ++index) {
        const RClangDeclaration *declaration = &generator->declarations[index];

        if ((declaration->kind == kind) && (strlen(declaration->name) == length) &&
            (memcmp(declaration->name, name, length) == 0)) {
            return declaration;
        }
    }
    return NULL;
}

/* ---- type trees ---------------------------------------------------------------------- */

static bool r_clang_new_node(RClangGenerator *generator, RClangNodeKind kind, size_t *index) {
    RClangNode *node;

    if (!r_grow_array(generator->context,
                      (void **)&generator->nodes,
                      &generator->node_capacity,
                      sizeof(RClangNode),
                      generator->node_count + 1U)) {
        generator->exhausted = true;
        return false;
    }
    node = &generator->nodes[generator->node_count];
    (void)memset(node, 0, sizeof(*node));
    node->kind = kind;
    node->first_child = generator->child_count;
    *index = generator->node_count;
    generator->node_count += 1U;
    return true;
}

static bool r_clang_add_children(RClangGenerator *generator,
                                 size_t node,
                                 const size_t *children,
                                 size_t count) {
    size_t index;

    if (!r_grow_array(generator->context,
                      (void **)&generator->children,
                      &generator->child_capacity,
                      sizeof(size_t),
                      generator->child_count + count)) {
        generator->exhausted = true;
        return false;
    }
    generator->nodes[node].first_child = generator->child_count;
    generator->nodes[node].child_count = count;
    for (index = 0U; index < count; ++index) {
        generator->children[generator->child_count++] = children[index];
    }
    return true;
}

static bool r_clang_other(RClangGenerator *generator, CXType type, size_t *index) {
    if (!r_clang_new_node(generator, R_CLANG_NODE_OTHER, index)) {
        return false;
    }
    generator->nodes[*index].spelling = r_clang_take(generator, clang_getTypeSpelling(type));
    return generator->nodes[*index].spelling != NULL;
}

static bool r_clang_tree(RClangGenerator *generator, CXType type, size_t *index);

static bool r_clang_tree_with_children(RClangGenerator *generator,
                                       RClangNodeKind kind,
                                       const CXType *types,
                                       size_t count,
                                       size_t *index) {
    size_t *children = NULL;
    size_t position;
    bool built = true;

    if (count != 0U) {
        children = r_context_allocate(generator->context, count * sizeof(size_t));
        if (children == NULL) {
            generator->exhausted = true;
            return false;
        }
    }
    for (position = 0U; built && (position < count); ++position) {
        built = r_clang_tree(generator, types[position], &children[position]);
    }
    built = built && r_clang_new_node(generator, kind, index) &&
            r_clang_add_children(generator, *index, children, count);
    r_context_free(generator->context, children);
    return built;
}

/* One C type as a record type tree (schema r-abi-record-0.1). Sugar that does not change the
   type is looked through; a typedef naming a struct, union or enumeration is kept as the
   spelling of the position; volatile and restrict are not expressible (R-FFI-0019), and an
   _Atomic type is a node over its value type, which only a struct member may have (R-FFI-0017). */
static bool r_clang_tree(RClangGenerator *generator, CXType type, size_t *index) {
    const bool is_const = clang_isConstQualifiedType(type) != 0U;

    if ((clang_isVolatileQualifiedType(type) != 0U) ||
        (clang_isRestrictQualifiedType(type) != 0U)) {
        return r_clang_other(generator, type, index);
    }
    switch (type.kind) {
    case CXType_Elaborated:
        if (!r_clang_tree(generator, clang_Type_getNamedType(type), index)) {
            return false;
        }
        break;
    case CXType_Attributed:
        if (!r_clang_tree(generator, clang_Type_getModifiedType(type), index)) {
            return false;
        }
        break;
    case CXType_Typedef: {
        CXCursor declaration = clang_getTypeDeclaration(type);

        if (!r_clang_tree(generator, clang_getTypedefDeclUnderlyingType(declaration), index)) {
            return false;
        }
        if ((generator->nodes[*index].kind == R_CLANG_NODE_RECORD) ||
            (generator->nodes[*index].kind == R_CLANG_NODE_ENUM)) {
            /* The outermost typedef is how the header spelled this position. */
            r_context_free(generator->context, generator->nodes[*index].typedef_name);
            generator->nodes[*index].typedef_name =
                r_clang_take(generator, clang_getTypedefName(type));
            if (generator->nodes[*index].typedef_name == NULL) {
                return false;
            }
        }
        break;
    }
    case CXType_Void:
        if (!r_clang_new_node(generator, R_CLANG_NODE_VOID, index)) {
            return false;
        }
        break;
    case CXType_Pointer: {
        const CXType pointee = clang_getPointeeType(type);

        if (!r_clang_tree_with_children(generator, R_CLANG_NODE_POINTER, &pointee, 1U, index)) {
            return false;
        }
        break;
    }
    case CXType_FunctionProto: {
        const int arguments = clang_getNumArgTypes(type);
        CXType *types;
        int position;
        bool built;

        if ((arguments < 0) || ((size_t)arguments > SIZE_MAX / sizeof(CXType) - 1U)) {
            return r_clang_other(generator, type, index);
        }
        types = r_context_allocate(generator->context, ((size_t)arguments + 1U) * sizeof(CXType));
        if (types == NULL) {
            generator->exhausted = true;
            return false;
        }
        types[0] = clang_getResultType(type);
        for (position = 0; position < arguments; ++position) {
            types[(size_t)position + 1U] = clang_getArgType(type, (unsigned)position);
        }
        built = r_clang_tree_with_children(
            generator, R_CLANG_NODE_FUNCTION, types, (size_t)arguments + 1U, index);
        r_context_free(generator->context, types);
        if (!built) {
            return false;
        }
        generator->nodes[*index].variadic = clang_isFunctionTypeVariadic(type) != 0U;
        break;
    }
    case CXType_Record:
    case CXType_Enum: {
        CXCursor declaration = clang_getTypeDeclaration(type);
        CXCursor definition = clang_getCursorDefinition(declaration);
        const bool is_enum = type.kind == CXType_Enum;
        RClangNode *node;

        if (!r_clang_new_node(
                generator, is_enum ? R_CLANG_NODE_ENUM : R_CLANG_NODE_RECORD, index)) {
            return false;
        }
        node = &generator->nodes[*index];
        node->is_union = clang_getCursorKind(declaration) == CXCursor_UnionDecl;
        node->complete = clang_Cursor_isNull(definition) == 0;
        node->has_declaration = node->complete;
        node->declaration = definition;
        node->tag_name = r_clang_tag_name(generator, declaration);
        if (generator->nodes[*index].tag_name == NULL) {
            return false;
        }
        break;
    }
    case CXType_Atomic: {
        const CXType value = clang_Type_getValueType(type);

        if (!r_clang_tree_with_children(generator, R_CLANG_NODE_ATOMIC, &value, 1U, index)) {
            return false;
        }
        break;
    }
    case CXType_ConstantArray: {
        const CXType element = clang_getArrayElementType(type);

        if (!r_clang_tree_with_children(generator, R_CLANG_NODE_ARRAY, &element, 1U, index)) {
            return false;
        }
        generator->nodes[*index].length = (unsigned long long)clang_getArraySize(type);
        break;
    }
    default:
        if ((type.kind >= CXType_FirstBuiltin) && (type.kind <= CXType_LastBuiltin)) {
            if (!r_clang_new_node(generator, R_CLANG_NODE_SCALAR, index)) {
                return false;
            }
            generator->nodes[*index].spelling =
                r_clang_take(generator, clang_getTypeSpelling(clang_getUnqualifiedType(type)));
            if (generator->nodes[*index].spelling == NULL) {
                return false;
            }
            break;
        }
        return r_clang_other(generator, type, index);
    }
    if (is_const) {
        generator->nodes[*index].is_const = true;
    }
    return true;
}

/* The inventory key of a record or enumeration leaf: its tag, else the typedef naming an
   anonymous tag; NULL for neither. */
static const char *r_clang_leaf_key(const RClangNode *node, const char **kind) {
    if ((node->tag_name != NULL) && (node->tag_name[0] != '\0')) {
        *kind = node->kind == R_CLANG_NODE_ENUM ? "enum" : node->is_union ? "union" : "struct";
        return node->tag_name;
    }
    if (node->typedef_name != NULL) {
        *kind = "typedef";
        return node->typedef_name;
    }
    return NULL;
}

/* Appends the record and enumeration leaves of a tree to the pending list. */
static bool r_clang_collect_leaves(
    RClangGenerator *generator, size_t tree, size_t **pending, size_t *count, size_t *capacity) {
    const RClangNode *node = &generator->nodes[tree];
    size_t child;

    if ((node->kind == R_CLANG_NODE_RECORD) || (node->kind == R_CLANG_NODE_ENUM)) {
        if (!r_grow_array(
                generator->context, (void **)pending, capacity, sizeof(size_t), *count + 1U)) {
            generator->exhausted = true;
            return false;
        }
        (*pending)[(*count)++] = tree;
        return true;
    }
    for (child = 0U; child < node->child_count; ++child) {
        if (!r_clang_collect_leaves(generator,
                                    generator->children[node->first_child + child],
                                    pending,
                                    count,
                                    capacity)) {
            return false;
        }
    }
    return true;
}

/* ---- inventories --------------------------------------------------------------------- */

static RClangInventory *r_clang_new_inventory(RClangGenerator *generator) {
    RClangInventory *inventory;

    if (!r_grow_array(generator->context,
                      (void **)&generator->inventories,
                      &generator->inventory_capacity,
                      sizeof(RClangInventory),
                      generator->inventory_count + 1U)) {
        generator->exhausted = true;
        return NULL;
    }
    inventory = &generator->inventories[generator->inventory_count++];
    (void)memset(inventory, 0, sizeof(*inventory));
    return inventory;
}

/* The JSON qualType clang writes: the type as declared. Its desugared form strips the sugar at
   the top level only and keeps the qualifiers collected on the way. */
static char *r_clang_desugared(RClangGenerator *generator, CXType type) {
    bool is_const = false;
    bool sugared = false;
    char *spelling;

    for (;;) {
        is_const = is_const || (clang_isConstQualifiedType(type) != 0U);
        if (type.kind == CXType_Typedef) {
            type = clang_getTypedefDeclUnderlyingType(clang_getTypeDeclaration(type));
        } else if (type.kind == CXType_Elaborated) {
            type = clang_Type_getNamedType(type);
        } else if (type.kind == CXType_Attributed) {
            type = clang_Type_getModifiedType(type);
        } else {
            break;
        }
        sugared = true;
    }
    if (!sugared) {
        return NULL;
    }
    spelling = r_clang_take(generator, clang_getTypeSpelling(clang_getUnqualifiedType(type)));
    if ((spelling != NULL) && is_const) {
        const size_t length = strlen(spelling);
        const bool pointer = (length != 0U) && (spelling[length - 1U] == '*');
        char *qualified = r_context_allocate(generator->context, length + sizeof(" const"));

        if (qualified == NULL) {
            generator->exhausted = true;
            r_context_free(generator->context, spelling);
            return NULL;
        }
        (void)snprintf(
            qualified, length + sizeof(" const"), pointer ? "%s const" : "const %s", spelling);
        r_context_free(generator->context, spelling);
        spelling = qualified;
    }
    return spelling;
}

typedef struct RClangFieldWalk {
    RClangGenerator *generator;
    const char *subject;
    size_t first;
    bool complete;
} RClangFieldWalk;

static enum CXChildVisitResult
r_clang_field_visit(CXCursor cursor, CXCursor parent, CXClientData data) {
    RClangFieldWalk *walk = data;
    RClangGenerator *generator = walk->generator;
    const enum CXCursorKind kind = clang_getCursorKind(cursor);
    RClangMember *member;
    CXType type;
    long long offset;

    (void)parent;
    /* A tag a member names first is declared there without a definition; only a definition
       inside the struct is a nested aggregate. */
    if (((kind == CXCursor_StructDecl) || (kind == CXCursor_UnionDecl)) &&
        (clang_isCursorDefinition(cursor) != 0U)) {
        walk->complete = false;
        (void)r_clang_fail(generator, "has a nested aggregate; not expressible", walk->subject);
        return CXChildVisit_Break;
    }
    if (kind != CXCursor_FieldDecl) {
        return CXChildVisit_Continue;
    }
    if (!r_grow_array(generator->context,
                      (void **)&generator->members,
                      &generator->member_capacity,
                      sizeof(RClangMember),
                      generator->member_count + 1U)) {
        generator->exhausted = true;
        walk->complete = false;
        return CXChildVisit_Break;
    }
    member = &generator->members[generator->member_count++];
    (void)memset(member, 0, sizeof(*member));
    member->tree = SIZE_MAX;
    member->name = r_clang_take(generator, clang_getCursorSpelling(cursor));
    if ((member->name != NULL) && (member->name[0] == '\0')) {
        walk->complete = false;
        (void)r_clang_fail(generator, "has an anonymous member; not expressible", walk->subject);
        return CXChildVisit_Break;
    }
    type = clang_getCursorType(cursor);
    member->type = r_clang_take(generator, clang_getTypeSpelling(type));
    member->desugared = r_clang_desugared(generator, type);
    if ((member->desugared != NULL) && (member->type != NULL) &&
        (strcmp(member->desugared, member->type) == 0)) {
        r_context_free(generator->context, member->desugared);
        member->desugared = NULL;
    }
    offset = clang_Cursor_getOffsetOfField(cursor);
    if (clang_Cursor_isBitField(cursor) != 0U) {
        const int width = clang_getFieldDeclBitWidth(cursor);
        if (width <= 0) {
            walk->complete = false;
            (void)r_clang_fail(generator,
                               "has an unnamed or zero-width bit-field; not expressible",
                               walk->subject);
            return CXChildVisit_Break;
        }
        member->bit_width = (unsigned)width;
        member->bit_offset = offset < 0 ? 0U : (unsigned)(offset % 8);
    }
    if ((member->name == NULL) || (member->type == NULL) || (offset < 0) ||
        ((member->bit_width == 0U) && ((offset % 8) != 0))) {
        walk->complete = false;
        if (!generator->exhausted) {
            (void)r_clang_fail(generator, "has a member without a byte offset", walk->subject);
        }
        return CXChildVisit_Break;
    }
    member->offset = (unsigned long long)offset / 8U;
    if (!r_clang_tree(generator, type, &member->tree)) {
        walk->complete = false;
        return CXChildVisit_Break;
    }
    return CXChildVisit_Continue;
}

/* The members and layout of a complete struct or union; false (with the reason reported) when it
   has a member R cannot express. */
static bool r_clang_struct_inventory(RClangGenerator *generator,
                                     RClangInventory *inventory,
                                     CXCursor definition,
                                     const char *subject) {
    RClangFieldWalk walk;
    const CXType type = clang_getCursorType(definition);
    const long long size = clang_Type_getSizeOf(type);
    const long long alignment = clang_Type_getAlignOf(type);

    walk.generator = generator;
    walk.subject = subject;
    walk.first = generator->member_count;
    walk.complete = true;
    (void)clang_visitChildren(definition, r_clang_field_visit, &walk);
    if (!walk.complete || generator->exhausted) {
        return false;
    }
    if ((size < 0) || (alignment <= 0)) {
        return r_clang_fail(generator, "has no record layout", subject);
    }
    inventory->is_struct = true;
    inventory->first_member = walk.first;
    inventory->member_count = generator->member_count - walk.first;
    inventory->size = (unsigned long long)size;
    inventory->alignment = (unsigned long long)alignment;
    return true;
}

typedef struct RClangEnumeratorWalk {
    RClangGenerator *generator;
    bool is_unsigned;
    bool complete;
} RClangEnumeratorWalk;

static enum CXChildVisitResult
r_clang_enumerator_visit(CXCursor cursor, CXCursor parent, CXClientData data) {
    RClangEnumeratorWalk *walk = data;
    RClangGenerator *generator = walk->generator;
    RClangEnumerator *enumerator;

    (void)parent;
    if (clang_getCursorKind(cursor) != CXCursor_EnumConstantDecl) {
        return CXChildVisit_Continue;
    }
    if (!r_grow_array(generator->context,
                      (void **)&generator->enumerators,
                      &generator->enumerator_capacity,
                      sizeof(RClangEnumerator),
                      generator->enumerator_count + 1U)) {
        generator->exhausted = true;
        walk->complete = false;
        return CXChildVisit_Break;
    }
    enumerator = &generator->enumerators[generator->enumerator_count++];
    enumerator->name = r_clang_take(generator, clang_getCursorSpelling(cursor));
    enumerator->is_unsigned = walk->is_unsigned;
    enumerator->value = clang_getEnumConstantDeclValue(cursor);
    enumerator->unsigned_value = clang_getEnumConstantDeclUnsignedValue(cursor);
    if (enumerator->name == NULL) {
        walk->complete = false;
        return CXChildVisit_Break;
    }
    return CXChildVisit_Continue;
}

static bool r_clang_is_unsigned(CXType type) {
    switch (clang_getCanonicalType(type).kind) {
    case CXType_Bool:
    case CXType_Char_U:
    case CXType_UChar:
    case CXType_UShort:
    case CXType_UInt:
    case CXType_ULong:
    case CXType_ULongLong:
        return true;
    default:
        return false;
    }
}

/* The enumerators and the compatible integer type of an enumeration. */
static bool r_clang_enum_inventory(RClangGenerator *generator,
                                   RClangInventory *inventory,
                                   CXCursor definition,
                                   const char *subject) {
    RClangEnumeratorWalk walk;
    const CXType integer = clang_getEnumDeclIntegerType(definition);
    const size_t first = generator->enumerator_count;

    walk.generator = generator;
    walk.is_unsigned = r_clang_is_unsigned(integer);
    walk.complete = true;
    (void)clang_visitChildren(definition, r_clang_enumerator_visit, &walk);
    if (!walk.complete || generator->exhausted) {
        return false;
    }
    if (generator->enumerator_count == first) {
        return r_clang_fail(generator, "has no enumerators", subject);
    }
    inventory->is_enum = true;
    inventory->first_enumerator = first;
    inventory->enumerator_count = generator->enumerator_count - first;
    inventory->underlying =
        r_clang_take(generator, clang_getTypeSpelling(clang_getCanonicalType(integer)));
    return inventory->underlying != NULL;
}

/* Follows a typedef chain to the struct, union or enumeration it names. */
static bool r_clang_resolve_typedef(RClangGenerator *generator,
                                    const RClangDeclaration *declaration,
                                    CXCursor *definition,
                                    bool *is_enum,
                                    bool *is_union,
                                    char **tag_name) {
    CXType type = clang_getCanonicalType(clang_getTypedefDeclUnderlyingType(declaration->cursor));
    CXCursor tag;

    if ((type.kind != CXType_Record) && (type.kind != CXType_Enum)) {
        return r_clang_fail(
            generator, "typedef does not name a struct, union or enumeration", declaration->name);
    }
    tag = clang_getTypeDeclaration(type);
    *definition = clang_getCursorDefinition(tag);
    *is_enum = type.kind == CXType_Enum;
    *is_union = clang_getCursorKind(tag) == CXCursor_UnionDecl;
    *tag_name = r_clang_tag_name(generator, tag);
    return *tag_name != NULL;
}

/* The inventory of one type a block requests (R-FFI-0017). */
static bool r_clang_requested_inventory(RClangGenerator *generator,
                                        const RClangRequestedType *requested) {
    const char *name;
    size_t length;
    RClangInventory *inventory;
    const RClangDeclaration *declaration;
    CXCursor definition;
    bool is_enum = requested->kind == R_C_TYPE_KIND_ENUM;
    bool is_union = requested->kind == R_C_TYPE_KIND_UNION;
    char *subject;

    if (!r_clang_intern(generator->context, requested->c_name, &name, &length)) {
        generator->exhausted = true;
        return false;
    }
    inventory = r_clang_new_inventory(generator);
    if (inventory == NULL) {
        return false;
    }
    inventory->c_name = r_copy_string(generator->context, name, length);
    inventory->kind = r_abi_kind_name(requested->kind);
    /* B9.2: the member type trees let the frontend compare member types as C types, whatever
       the header spells them by (R-FFI-0017). */
    inventory->emit_trees = true;
    if (inventory->c_name == NULL) {
        generator->exhausted = true;
        return false;
    }
    subject = inventory->c_name;
    if (requested->kind == R_C_TYPE_KIND_TYPEDEF) {
        declaration = r_clang_find(generator, R_CLANG_DECLARATION_TYPEDEF, name, length);
        if (declaration == NULL) {
            return r_clang_fail(generator, "typedef is not declared by the headers", subject);
        }
        if (!r_clang_resolve_typedef(
                generator, declaration, &definition, &is_enum, &is_union, &inventory->tag_name)) {
            return false;
        }
        inventory->tag = is_enum ? "enum" : is_union ? "union" : "struct";
    } else {
        declaration = r_clang_find(generator,
                                   is_enum    ? R_CLANG_DECLARATION_ENUM
                                   : is_union ? R_CLANG_DECLARATION_UNION
                                              : R_CLANG_DECLARATION_STRUCT,
                                   name,
                                   length);
        definition = declaration != NULL ? declaration->cursor : clang_getNullCursor();
    }
    if (clang_Cursor_isNull(definition) != 0) {
        return r_clang_fail(generator,
                            is_enum    ? "enumeration is not declared by the headers"
                            : is_union ? "union is not completely declared by the headers"
                                       : "struct is not completely declared by the headers",
                            subject);
    }
    return is_enum ? r_clang_enum_inventory(generator, inventory, definition, subject)
                   : r_clang_struct_inventory(generator, inventory, definition, subject);
}

static RClangInventory *
r_clang_find_inventory(RClangGenerator *generator, const char *c_name, const char *kind) {
    size_t index;

    for (index = 0U; index < generator->inventory_count; ++index) {
        RClangInventory *inventory = &generator->inventories[index];

        if ((strcmp(inventory->c_name, c_name) == 0) && (strcmp(inventory->kind, kind) == 0)) {
            return inventory;
        }
    }
    return NULL;
}

/* The inventory of a struct, union or enumeration reached from a symbol tree; a tag R cannot
   express is recorded as inexpressible instead of failing the record. */
static bool r_clang_reached_inventory(RClangGenerator *generator,
                                      size_t leaf,
                                      const char *key,
                                      const char *kind,
                                      size_t **pending,
                                      size_t *pending_count,
                                      size_t *pending_capacity) {
    const RClangNode node = generator->nodes[leaf];
    RClangInventory *existing = r_clang_find_inventory(generator, key, kind);
    RClangInventory *inventory;
    size_t index;
    const bool failed_before = generator->failed;
    const size_t report_length = generator->context->header_report_length;
    bool expressed;
    const char *reason = NULL;

    if ((existing != NULL) && existing->reached) {
        return true;
    }
    if (existing != NULL) {
        /* A requested inventory keeps its layout; its members lead further. */
        existing->reached = true;
        existing->emit_trees = true;
        inventory = existing;
    } else {
        inventory = r_clang_new_inventory(generator);
        if (inventory == NULL) {
            return false;
        }
        inventory->emit_trees = true;
        inventory->reached = true;
        inventory->c_name = r_clang_copy(generator, key);
        inventory->kind = kind;
        if (inventory->c_name == NULL) {
            return false;
        }
        if (strcmp(kind, "typedef") == 0) {
            inventory->tag = node.kind == R_CLANG_NODE_ENUM ? "enum"
                             : node.is_union                ? "union"
                                                            : "struct";
            inventory->tag_name = r_clang_copy(generator, node.tag_name);
            if (inventory->tag_name == NULL) {
                return false;
            }
        }
        if (!node.has_declaration) {
            reason = node.kind == R_CLANG_NODE_ENUM ? "has no enumerators" : "is incomplete";
        }
        if (reason != NULL) {
            inventory->inexpressible = r_clang_copy(generator, reason);
            return inventory->inexpressible != NULL;
        }
        expressed = node.kind == R_CLANG_NODE_ENUM
                        ? r_clang_enum_inventory(generator, inventory, node.declaration, key)
                        : r_clang_struct_inventory(generator, inventory, node.declaration, key);
        if (!expressed) {
            if (generator->exhausted) {
                return false;
            }
            /* Not a failure of the record: the reason becomes the inventory's. */
            const char *reason_text = generator->context->header_report + report_length;
            size_t reason_length = generator->context->header_report_length - report_length;

            if ((reason_length != 0U) && (reason_text[reason_length - 1U] == '\n')) {
                reason_length -= 1U;
            }
            inventory->inexpressible =
                r_copy_string(generator->context, reason_text, reason_length);
            generator->failed = failed_before;
            generator->context->header_report_length = report_length;
            if (inventory->inexpressible == NULL) {
                generator->exhausted = true;
            }
            return inventory->inexpressible != NULL;
        }
    }
    for (index = 0U; index < inventory->member_count; ++index) {
        const size_t tree = generator->members[inventory->first_member + index].tree;

        if ((tree != SIZE_MAX) &&
            !r_clang_collect_leaves(generator, tree, pending, pending_count, pending_capacity)) {
            return false;
        }
    }
    return true;
}

/* The C type of every requested symbol and the inventories reached from them (R-FFI-0021). */
static bool r_clang_symbols(RClangGenerator *generator, const RClangRequest *request) {
    size_t *pending = NULL;
    size_t pending_count = 0U;
    size_t pending_capacity = 0U;
    size_t index;
    bool success = true;

    for (index = 0U; success && (index < request->symbol_count); ++index) {
        const RClangRequestedSymbol *requested = &request->symbols[index];
        const RClangDeclaration *declaration;
        RClangSymbol *symbol;
        const char *name;
        size_t length;

        if (!r_clang_intern(generator->context, requested->c_name, &name, &length) ||
            !r_grow_array(generator->context,
                          (void **)&generator->symbols,
                          &generator->symbol_capacity,
                          sizeof(RClangSymbol),
                          generator->symbol_count + 1U)) {
            generator->exhausted = true;
            success = false;
            break;
        }
        declaration = r_clang_find(generator,
                                   requested->object ? R_CLANG_DECLARATION_VARIABLE
                                                     : R_CLANG_DECLARATION_FUNCTION,
                                   name,
                                   length);
        if (declaration == NULL) {
            char subject[256];

            (void)snprintf(subject, sizeof(subject), "%.*s", (int)length, name);
            success = r_clang_fail(generator,
                                   requested->object ? "object is not declared by the headers"
                                                     : "function is not declared by the headers",
                                   subject);
            break;
        }
        symbol = &generator->symbols[generator->symbol_count++];
        symbol->c_name = name;
        symbol->c_name_length = length;
        symbol->object = requested->object;
        success = r_clang_tree(generator, clang_getCursorType(declaration->cursor), &symbol->tree);
        if (success &&
            ((generator->nodes[symbol->tree].kind == R_CLANG_NODE_FUNCTION) == requested->object)) {
            success = r_clang_fail(generator,
                                   requested->object ? "symbol is not an object"
                                                     : "symbol is not a function",
                                   declaration->name);
        }
        success =
            success && r_clang_collect_leaves(
                           generator, symbol->tree, &pending, &pending_count, &pending_capacity);
    }
    /* Breadth first, as the members of each round reveal the next. */
    for (index = 0U; success && (index < pending_count); ++index) {
        const char *kind = NULL;
        const char *key = r_clang_leaf_key(&generator->nodes[pending[index]], &kind);

        if (key == NULL) {
            continue;
        }
        success = r_clang_reached_inventory(
            generator, pending[index], key, kind, &pending, &pending_count, &pending_capacity);
    }
    r_context_free(generator->context, pending);
    return success;
}

/* ---- request ------------------------------------------------------------------------- */

static bool r_clang_add_unique_intern(
    RFrontendContext *context, uint32_t **items, size_t *count, size_t *capacity, uint32_t value) {
    size_t index;

    for (index = 0U; index < *count; ++index) {
        if ((*items)[index] == value) {
            return true;
        }
    }
    if (!r_grow_array(context, (void **)items, capacity, sizeof(uint32_t), *count + 1U)) {
        return false;
    }
    (*items)[(*count)++] = value;
    return true;
}

/* Merges one block into the request of its record name. */
static bool r_clang_merge_block(RFrontendContext *context,
                                RClangRequest *request,
                                const RExternBlockRecord *block,
                                uint32_t block_index) {
    size_t index;
    uint32_t header_index;

    if (request->provider == NULL) {
        request->provider = "";
        if ((block->link_name_intern_id != UINT32_C(0)) &&
            !r_clang_intern(context,
                            block->link_name_intern_id,
                            &request->provider,
                            &request->provider_length)) {
            return false;
        }
    }
    if ((block->link_provider != UINT32_C(0)) &&
        ((size_t)block->link_provider <= context->link_provider_count)) {
        const RLinkProviderRecord *provider =
            &context->link_providers[(size_t)block->link_provider - 1U];

        for (index = 0U; index < provider->definition_count; ++index) {
            const RLinkDefinitionRecord *definition =
                &context->link_definitions[provider->first_definition + index];
            size_t seen;
            bool present = false;

            for (seen = 0U; seen < request->definition_count; ++seen) {
                const RLinkDefinitionRecord *other =
                    &context->link_definitions[request->definitions[seen]];

                present = present ||
                          ((other->name_length == definition->name_length) &&
                           (memcmp(other->name, definition->name, other->name_length) == 0) &&
                           (other->value_length == definition->value_length) &&
                           ((other->value_length == 0U) ||
                            (memcmp(other->value, definition->value, other->value_length) == 0)));
            }
            if (!present && (!r_grow_array(context,
                                           (void **)&request->definitions,
                                           &request->definition_capacity,
                                           sizeof(size_t),
                                           request->definition_count + 1U))) {
                return false;
            }
            if (!present) {
                request->definitions[request->definition_count++] =
                    provider->first_definition + index;
            }
        }
    }
    for (header_index = UINT32_C(0); header_index < block->header_count; ++header_index) {
        const size_t position = (size_t)block->first_header + (size_t)header_index;

        if ((position >= context->extern_header_count) ||
            !r_clang_add_unique_intern(context,
                                       &request->headers,
                                       &request->header_count,
                                       &request->header_capacity,
                                       context->extern_headers[position])) {
            return false;
        }
    }
    for (index = 0U; index < context->semantic_aggregate_count; ++index) {
        const RSemanticAggregate *aggregate = &context->semantic_aggregates[index];
        size_t seen;
        bool present = false;

        if (!aggregate->is_c_declared || (aggregate->extern_block != block_index)) {
            continue;
        }
        for (seen = 0U; seen < request->type_count; ++seen) {
            present = present || ((request->types[seen].c_name == aggregate->c_name_intern_id) &&
                                  (request->types[seen].kind == aggregate->c_type_kind));
        }
        if (present) {
            continue;
        }
        if (!r_grow_array(context,
                          (void **)&request->types,
                          &request->type_capacity,
                          sizeof(RClangRequestedType),
                          request->type_count + 1U)) {
            return false;
        }
        request->types[request->type_count].c_name = aggregate->c_name_intern_id;
        request->types[request->type_count].kind = aggregate->c_type_kind;
        request->type_count += 1U;
    }
    for (index = 0U; index < context->semantic_symbol_count; ++index) {
        const RSemanticSymbol *symbol = &context->semantic_symbols[index];
        const bool object = symbol->kind == R_SEMANTIC_SYMBOL_MODULE_OBJECT;
        size_t seen;
        bool present = false;

        if (!symbol->is_import || !symbol->passes_r_struct || symbol->poisoned ||
            (symbol->extern_block != block_index)) {
            continue;
        }
        for (seen = 0U; seen < request->symbol_count; ++seen) {
            present = present || ((request->symbols[seen].c_name == symbol->c_name_intern_id) &&
                                  (request->symbols[seen].object == object));
        }
        if (present) {
            continue;
        }
        if (!r_grow_array(context,
                          (void **)&request->symbols,
                          &request->symbol_capacity,
                          sizeof(RClangRequestedSymbol),
                          request->symbol_count + 1U)) {
            return false;
        }
        request->symbols[request->symbol_count].c_name = symbol->c_name_intern_id;
        request->symbols[request->symbol_count].object = object;
        request->symbol_count += 1U;
    }
    return true;
}

static int r_clang_compare_requests(const RClangRequest *left, const RClangRequest *right) {
    const size_t length =
        left->name_length < right->name_length ? left->name_length : right->name_length;
    const int comparison = memcmp(left->name, right->name, length);

    if (comparison != 0) {
        return comparison;
    }
    return left->name_length < right->name_length   ? -1
           : left->name_length > right->name_length ? 1
                                                    : 0;
}

/* The records to generate, by record name, each merged from its blocks in (module, position)
   order. */
static bool r_clang_requests(RFrontendContext *context,
                             RClangRequest **requests,
                             size_t *count,
                             size_t *capacity) {
    const RExternBlockRecord *previous = NULL;

    for (;;) {
        const RExternBlockRecord *best = NULL;
        const char *name;
        size_t name_length;
        size_t index;
        RClangRequest *request = NULL;

        for (index = 0U; index < context->extern_block_count; ++index) {
            const RExternBlockRecord *candidate = &context->extern_blocks[index];

            if ((candidate->abi_name_intern_id == UINT32_C(0)) ||
                ((previous != NULL) && (r_abi_compare_blocks(context, candidate, previous) <= 0))) {
                continue;
            }
            if ((best == NULL) || (r_abi_compare_blocks(context, candidate, best) < 0)) {
                best = candidate;
            }
        }
        if (best == NULL) {
            break;
        }
        previous = best;
        if (!r_clang_intern(context, best->abi_name_intern_id, &name, &name_length)) {
            return false;
        }
        for (index = 0U; index < *count; ++index) {
            if (((*requests)[index].name_length == name_length) &&
                (memcmp((*requests)[index].name, name, name_length) == 0)) {
                request = &(*requests)[index];
            }
        }
        if (request == NULL) {
            /* Sorted insertion keeps the records in name order. */
            RClangRequest probe;

            if (!r_grow_array(
                    context, (void **)requests, capacity, sizeof(RClangRequest), *count + 1U)) {
                return false;
            }
            (void)memset(&probe, 0, sizeof(probe));
            probe.name = name;
            probe.name_length = name_length;
            index = *count;
            while ((index > 0U) &&
                   (r_clang_compare_requests(&(*requests)[index - 1U], &probe) > 0)) {
                (*requests)[index] = (*requests)[index - 1U];
                index -= 1U;
            }
            (*requests)[index] = probe;
            *count += 1U;
            request = &(*requests)[index];
        }
        if (!r_clang_merge_block(
                context, request, best, (uint32_t)(best - context->extern_blocks) + UINT32_C(1))) {
            return false;
        }
    }
    return true;
}

static void
r_clang_dispose_requests(RFrontendContext *context, RClangRequest *requests, size_t count) {
    size_t index;

    for (index = 0U; index < count; ++index) {
        r_context_free(context, requests[index].definitions);
        r_context_free(context, requests[index].headers);
        r_context_free(context, requests[index].types);
        r_context_free(context, requests[index].symbols);
    }
    r_context_free(context, requests);
}

/* ---- output -------------------------------------------------------------------------- */

static bool r_clang_write_string(RFrontendWriteFn writer, void *user_data, const char *text) {
    return r_abi_write_json_string(writer, user_data, text, strlen(text));
}

static bool r_clang_write_tree(const RClangGenerator *generator,
                               size_t tree,
                               RFrontendWriteFn writer,
                               void *user_data) {
    static const char *const names[] = {
        "void", "scalar", "pointer", "function", "record", "enum", "array", "other", "atomic"};
    const RClangNode *node = &generator->nodes[tree];
    char number[32];
    size_t child;

    if (!r_write_text(writer, user_data, "{\"node\": \"") ||
        !r_write_text(writer, user_data, names[node->kind]) ||
        !r_write_text(writer, user_data, "\"")) {
        return false;
    }
    if (node->is_const && !r_write_text(writer, user_data, ", \"const\": true")) {
        return false;
    }
    switch (node->kind) {
    case R_CLANG_NODE_SCALAR:
    case R_CLANG_NODE_OTHER:
        return r_write_text(writer, user_data, ", \"spelling\": ") &&
               r_clang_write_string(writer, user_data, node->spelling) &&
               r_write_text(writer, user_data, "}");
    case R_CLANG_NODE_POINTER:
        return r_write_text(writer, user_data, ", \"pointee\": ") &&
               r_clang_write_tree(
                   generator, generator->children[node->first_child], writer, user_data) &&
               r_write_text(writer, user_data, "}");
    case R_CLANG_NODE_ATOMIC:
        return r_write_text(writer, user_data, ", \"value\": ") &&
               r_clang_write_tree(
                   generator, generator->children[node->first_child], writer, user_data) &&
               r_write_text(writer, user_data, "}");
    case R_CLANG_NODE_ARRAY:
        (void)snprintf(number, sizeof(number), "%llu", node->length);
        return r_write_text(writer, user_data, ", \"length\": ") &&
               r_write_text(writer, user_data, number) &&
               r_write_text(writer, user_data, ", \"element\": ") &&
               r_clang_write_tree(
                   generator, generator->children[node->first_child], writer, user_data) &&
               r_write_text(writer, user_data, "}");
    case R_CLANG_NODE_FUNCTION:
        if (!r_write_text(writer, user_data, ", \"result\": ") ||
            !r_clang_write_tree(
                generator, generator->children[node->first_child], writer, user_data) ||
            !r_write_text(writer, user_data, ", \"parameters\": [")) {
            return false;
        }
        for (child = 1U; child < node->child_count; ++child) {
            if (((child > 1U) && !r_write_text(writer, user_data, ", ")) ||
                !r_clang_write_tree(
                    generator, generator->children[node->first_child + child], writer, user_data)) {
                return false;
            }
        }
        return r_write_text(writer, user_data, "], \"variadic\": ") &&
               r_write_text(writer, user_data, node->variadic ? "true}" : "false}");
    case R_CLANG_NODE_RECORD:
        if (!r_write_text(writer,
                          user_data,
                          node->is_union ? ", \"tag\": \"union\"" : ", \"tag\": \"struct\"") ||
            !r_write_text(writer, user_data, ", \"complete\": ") ||
            !r_write_text(writer, user_data, node->complete ? "true" : "false")) {
            return false;
        }
        break;
    case R_CLANG_NODE_ENUM:
    case R_CLANG_NODE_VOID:
    default:
        break;
    }
    if ((node->kind == R_CLANG_NODE_RECORD) || (node->kind == R_CLANG_NODE_ENUM)) {
        if (!r_write_text(writer, user_data, ", \"tag_name\": ") ||
            !r_clang_write_string(writer, user_data, node->tag_name) ||
            ((node->typedef_name != NULL) &&
             (!r_write_text(writer, user_data, ", \"typedef\": ") ||
              !r_clang_write_string(writer, user_data, node->typedef_name)))) {
            return false;
        }
    }
    return r_write_text(writer, user_data, "}");
}

static bool r_clang_write_inventory(const RClangGenerator *generator,
                                    const RClangInventory *inventory,
                                    RFrontendWriteFn writer,
                                    void *user_data) {
    char number[96];
    size_t index;

    if (!r_write_text(writer, user_data, "        {\"c_name\": ") ||
        !r_clang_write_string(writer, user_data, inventory->c_name) ||
        !r_write_text(writer, user_data, ", \"kind\": ") ||
        !r_clang_write_string(writer, user_data, inventory->kind)) {
        return false;
    }
    if ((inventory->tag != NULL) &&
        (!r_write_text(writer, user_data, ", \"tag\": ") ||
         !r_clang_write_string(writer, user_data, inventory->tag) ||
         !r_write_text(writer, user_data, ", \"tag_name\": ") ||
         !r_clang_write_string(
             writer, user_data, inventory->tag_name != NULL ? inventory->tag_name : ""))) {
        return false;
    }
    if (inventory->inexpressible != NULL) {
        return r_write_text(writer, user_data, ", \"inexpressible\": ") &&
               r_clang_write_string(writer, user_data, inventory->inexpressible) &&
               r_write_text(writer, user_data, "}");
    }
    if (inventory->is_struct) {
        (void)snprintf(number,
                       sizeof(number),
                       ", \"size\": %llu, \"alignment\": %llu",
                       inventory->size,
                       inventory->alignment);
        if (!r_write_text(writer, user_data, number) ||
            !r_write_text(writer, user_data, ", \"members\": [")) {
            return false;
        }
        for (index = 0U; index < inventory->member_count; ++index) {
            const RClangMember *member = &generator->members[inventory->first_member + index];

            if (member->bit_width == 0U) {
                (void)snprintf(number, sizeof(number), ", \"offset\": %llu", member->offset);
            } else {
                (void)snprintf(number,
                               sizeof(number),
                               ", \"offset\": %llu, \"bit_offset\": %u, \"bit_width\": %u",
                               member->offset,
                               member->bit_offset,
                               member->bit_width);
            }
            if (((index != 0U) && !r_write_text(writer, user_data, ", ")) ||
                !r_write_text(writer, user_data, "\n          {\"name\": ") ||
                !r_clang_write_string(writer, user_data, member->name) ||
                !r_write_text(writer, user_data, ", \"type\": ") ||
                !r_clang_write_string(writer, user_data, member->type) ||
                ((member->desugared != NULL) &&
                 (!r_write_text(writer, user_data, ", \"desugared_type\": ") ||
                  !r_clang_write_string(writer, user_data, member->desugared))) ||
                !r_write_text(writer, user_data, number) ||
                (inventory->emit_trees && (member->tree != SIZE_MAX) &&
                 (!r_write_text(writer, user_data, ", \"type_tree\": ") ||
                  !r_clang_write_tree(generator, member->tree, writer, user_data))) ||
                !r_write_text(writer, user_data, "}")) {
                return false;
            }
        }
        if (!r_write_text(writer, user_data, "]")) {
            return false;
        }
    }
    if (inventory->is_enum) {
        if (!r_write_text(writer, user_data, ", \"underlying\": ") ||
            !r_clang_write_string(writer, user_data, inventory->underlying) ||
            !r_write_text(writer, user_data, ", \"enumerators\": [")) {
            return false;
        }
        for (index = 0U; index < inventory->enumerator_count; ++index) {
            const RClangEnumerator *enumerator =
                &generator->enumerators[inventory->first_enumerator + index];

            if (enumerator->is_unsigned) {
                (void)snprintf(
                    number, sizeof(number), ", \"value\": %llu}", enumerator->unsigned_value);
            } else {
                (void)snprintf(number, sizeof(number), ", \"value\": %lld}", enumerator->value);
            }
            if (((index != 0U) && !r_write_text(writer, user_data, ", ")) ||
                !r_write_text(writer, user_data, "{\"name\": ") ||
                !r_clang_write_string(writer, user_data, enumerator->name) ||
                !r_write_text(writer, user_data, number)) {
                return false;
            }
        }
        if (!r_write_text(writer, user_data, "]")) {
            return false;
        }
    }
    return r_write_text(writer, user_data, "}");
}

/* The digest of a header as the first root that provides it holds it (R-FFI-0044). */
static bool r_clang_header_digest(RClangGenerator *generator,
                                  const RFrontendHeaderOptions *options,
                                  const char *spelling,
                                  size_t spelling_length,
                                  char hexadecimal[65]) {
    static const char digits[] = "0123456789abcdef";
    size_t index;

    for (index = 0U; index < options->root_count; ++index) {
        char path[4096];
        FILE *stream;
        uint8_t *bytes = NULL;
        size_t length = 0U;
        size_t capacity = 0U;
        uint8_t digest[32];
        size_t position;
        int written = snprintf(
            path, sizeof(path), "%s/%.*s", options->roots[index], (int)spelling_length, spelling);

        if ((written < 0) || ((size_t)written >= sizeof(path))) {
            continue;
        }
        stream = fopen(path, "rb");
        if (stream == NULL) {
            continue;
        }
        for (;;) {
            size_t read;

            if (!r_grow_array(generator->context, (void **)&bytes, &capacity, 1U, length + 4096U)) {
                generator->exhausted = true;
                (void)fclose(stream);
                r_context_free(generator->context, bytes);
                return false;
            }
            read = fread(bytes + length, 1U, 4096U, stream);
            length += read;
            if (read < 4096U) {
                break;
            }
        }
        (void)fclose(stream);
        r_sha256_digest(bytes, length, digest);
        r_context_free(generator->context, bytes);
        for (position = 0U; position < 32U; ++position) {
            hexadecimal[2U * position] = digits[digest[position] >> 4U];
            hexadecimal[2U * position + 1U] = digits[digest[position] & 15U];
        }
        hexadecimal[64] = '\0';
        return true;
    }
    {
        char subject[256];

        (void)snprintf(subject, sizeof(subject), "%.*s", (int)spelling_length, spelling);
        return r_clang_fail(generator, "header is not under any header root", subject);
    }
}

/* ---- generation ---------------------------------------------------------------------- */

static void r_clang_generator_reset(RClangGenerator *generator) {
    RFrontendContext *context = generator->context;
    size_t index;

    for (index = 0U; index < generator->node_count; ++index) {
        r_context_free(context, generator->nodes[index].spelling);
        r_context_free(context, generator->nodes[index].tag_name);
        r_context_free(context, generator->nodes[index].typedef_name);
    }
    for (index = 0U; index < generator->member_count; ++index) {
        r_context_free(context, generator->members[index].name);
        r_context_free(context, generator->members[index].type);
        r_context_free(context, generator->members[index].desugared);
    }
    for (index = 0U; index < generator->enumerator_count; ++index) {
        r_context_free(context, generator->enumerators[index].name);
    }
    for (index = 0U; index < generator->inventory_count; ++index) {
        r_context_free(context, generator->inventories[index].c_name);
        r_context_free(context, generator->inventories[index].tag_name);
        r_context_free(context, generator->inventories[index].inexpressible);
        r_context_free(context, generator->inventories[index].underlying);
    }
    for (index = 0U; index < generator->declaration_count; ++index) {
        r_context_free(context, generator->declarations[index].name);
    }
    generator->node_count = 0U;
    generator->child_count = 0U;
    generator->member_count = 0U;
    generator->enumerator_count = 0U;
    generator->inventory_count = 0U;
    generator->symbol_count = 0U;
    generator->declaration_count = 0U;
}

static void r_clang_generator_dispose(RClangGenerator *generator) {
    RFrontendContext *context = generator->context;

    r_clang_generator_reset(generator);
    r_context_free(context, generator->nodes);
    r_context_free(context, generator->children);
    r_context_free(context, generator->members);
    r_context_free(context, generator->enumerators);
    r_context_free(context, generator->inventories);
    r_context_free(context, generator->symbols);
    r_context_free(context, generator->declarations);
}

/* The prelude of a record: the feature-test definitions and the headers. */
static bool r_clang_prelude(RFrontendContext *context,
                            const RClangRequest *request,
                            char **text,
                            size_t *length) {
    size_t capacity = 0U;
    size_t index;

    *text = NULL;
    *length = 0U;
    for (index = 0U; index < request->definition_count + request->header_count; ++index) {
        const char *first;
        size_t first_length;
        const char *second = "";
        size_t second_length = 0U;
        const char *opening;
        const char *closing;
        size_t needed;

        if (index < request->definition_count) {
            const RLinkDefinitionRecord *definition =
                &context->link_definitions[request->definitions[index]];

            first = definition->name;
            first_length = definition->name_length;
            second = definition->value_length != 0U ? definition->value : "1";
            second_length = definition->value_length != 0U ? definition->value_length : 1U;
            opening = "#define ";
            closing = "\n";
        } else if (!r_clang_intern(context,
                                   request->headers[index - request->definition_count],
                                   &first,
                                   &first_length)) {
            return false;
        } else {
            opening = "#include <";
            closing = ">\n";
        }
        needed = strlen(opening) + first_length + 1U + second_length + strlen(closing) + 1U;
        if (!r_grow_array(context, (void **)text, &capacity, 1U, *length + needed)) {
            return false;
        }
        *length += (size_t)snprintf(*text + *length,
                                    capacity - *length,
                                    "%s%.*s%s%.*s%s",
                                    opening,
                                    (int)first_length,
                                    first,
                                    second_length != 0U ? " " : "",
                                    (int)second_length,
                                    second,
                                    closing);
    }
    return true;
}

static bool r_clang_write_record(RClangGenerator *generator,
                                 const RClangRequest *request,
                                 const RFrontendHeaderOptions *options,
                                 const char *target_triple,
                                 size_t target_triple_length,
                                 RFrontendWriteFn writer,
                                 void *user_data,
                                 bool *io_failed) {
    RFrontendContext *context = generator->context;
    char identity[256];
    char version[32];
    const char *build = r_frontend_target_c_compiler_build();
    const char **arguments;
    size_t argument_count = 0U;
    size_t index;
    bool written;

    /* R-FFI-0044: the identity names libclang and the toolchain build the manifest pins, whose
       release r_clang_check_toolchain has verified. */
    if (!r_frontend_header_version(version, sizeof(version))) {
        return false;
    }
    (void)snprintf(identity, sizeof(identity), "libclang %s (toolchain %s)", version, build);
    arguments = r_clang_arguments(context, options, &argument_count);
    if (arguments == NULL) {
        generator->exhausted = true;
        return false;
    }
    written =
        r_write_text(writer, user_data, "    {\n      \"name\": ") &&
        r_abi_write_json_string(writer, user_data, request->name, request->name_length) &&
        r_write_text(writer, user_data, ",\n      \"provider\": ") &&
        r_abi_write_json_string(writer, user_data, request->provider, request->provider_length) &&
        r_write_text(writer, user_data, ",\n      \"compiler_identity\": ") &&
        r_clang_write_string(writer, user_data, identity) &&
        r_write_text(writer, user_data, ",\n      \"options\": [");
    for (index = 0U; written && (index < argument_count); ++index) {
        written = ((index == 0U) || r_write_text(writer, user_data, ", ")) &&
                  r_clang_write_string(writer, user_data, arguments[index]);
    }
    r_context_free(context, arguments);
    written =
        written && r_write_text(writer, user_data, "],\n      \"feature_test_definitions\": [");
    for (index = 0U; written && (index < request->definition_count); ++index) {
        const RLinkDefinitionRecord *definition =
            &context->link_definitions[request->definitions[index]];

        written = ((index == 0U) || r_write_text(writer, user_data, ", ")) &&
                  r_write_text(writer, user_data, "\"") &&
                  writer(user_data, definition->name, definition->name_length) &&
                  ((definition->value_length == 0U) ||
                   (r_write_text(writer, user_data, "=") &&
                    writer(user_data, definition->value, definition->value_length))) &&
                  r_write_text(writer, user_data, "\"");
    }
    written = written && r_write_text(writer, user_data, "],\n      \"headers\": [");
    for (index = 0U; written && (index < request->header_count); ++index) {
        const char *spelling;
        size_t spelling_length;
        char digest[65];

        if (!r_clang_intern(context, request->headers[index], &spelling, &spelling_length)) {
            generator->exhausted = true;
            return false;
        }
        if (!r_clang_header_digest(generator, options, spelling, spelling_length, digest)) {
            return false;
        }
        written = ((index == 0U) || r_write_text(writer, user_data, ", ")) &&
                  r_write_text(writer, user_data, "{\"spelling\": ") &&
                  r_abi_write_json_string(writer, user_data, spelling, spelling_length) &&
                  r_write_text(writer, user_data, ", \"sha256\": \"") &&
                  r_write_text(writer, user_data, digest) && r_write_text(writer, user_data, "\"}");
    }
    written = written && r_write_text(writer, user_data, "],\n      \"types\": [\n");
    for (index = 0U; written && (index < generator->inventory_count); ++index) {
        written =
            ((index == 0U) || r_write_text(writer, user_data, ",\n")) &&
            r_clang_write_inventory(generator, &generator->inventories[index], writer, user_data);
    }
    written = written && r_write_text(writer, user_data, "\n      ]");
    if (written && (generator->symbol_count != 0U)) {
        written = r_write_text(writer, user_data, ",\n      \"symbols\": [");
        for (index = 0U; written && (index < generator->symbol_count); ++index) {
            const RClangSymbol *symbol = &generator->symbols[index];

            written =
                ((index == 0U) || r_write_text(writer, user_data, ",")) &&
                r_write_text(writer, user_data, "\n        {\"c_name\": ") &&
                r_abi_write_json_string(writer, user_data, symbol->c_name, symbol->c_name_length) &&
                r_write_text(writer,
                             user_data,
                             symbol->object ? ", \"kind\": \"object\", \"type\": "
                                            : ", \"kind\": \"function\", \"type\": ") &&
                r_clang_write_tree(generator, symbol->tree, writer, user_data) &&
                r_write_text(writer, user_data, "}");
        }
        written = written && r_write_text(writer, user_data, "]");
    }
    if (written && (target_triple != NULL)) {
        /* Only a request that names its target pins the record to it (R-FFI-0040). */
        written = r_write_text(writer, user_data, ",\n      \"target_triple\": ") &&
                  r_abi_write_json_string(writer, user_data, target_triple, target_triple_length);
    }
    written = written && r_write_text(writer, user_data, "\n    }");
    if (!written) {
        *io_failed = true;
    }
    return written;
}

static bool r_clang_generate(RClangGenerator *generator,
                             const RClangRequest *request,
                             const RFrontendHeaderOptions *options,
                             RFrontendStatus *status) {
    RFrontendContext *context = generator->context;
    char *prelude = NULL;
    size_t prelude_length = 0U;
    CXIndex clang_index = NULL;
    CXTranslationUnit unit = NULL;
    bool failed = false;
    size_t index;
    bool success;

    if (!r_clang_prelude(context, request, &prelude, &prelude_length)) {
        r_context_free(context, prelude);
        *status = R_FRONTEND_OUT_OF_MEMORY;
        return false;
    }
    *status = r_clang_parse(
        context, options, prelude, prelude_length, CXTranslationUnit_None, &clang_index, &unit);
    r_context_free(context, prelude);
    success = *status == R_FRONTEND_OK;
    if (success && !r_clang_report_errors(context, unit, &failed)) {
        *status = R_FRONTEND_OUT_OF_MEMORY;
        success = false;
    }
    if (success && failed) {
        (void)r_clang_fail(generator, "the headers do not compile", NULL);
        success = false;
    }
    if (success) {
        (void)clang_visitChildren(
            clang_getTranslationUnitCursor(unit), r_clang_index_visit, generator);
        success = !generator->exhausted;
    }
    for (index = 0U; success && (index < request->type_count); ++index) {
        success = r_clang_requested_inventory(generator, &request->types[index]);
    }
    success = success && r_clang_symbols(generator, request);
    if (!success && (*status == R_FRONTEND_OK)) {
        *status = generator->exhausted ? R_FRONTEND_OUT_OF_MEMORY : R_FRONTEND_INVALID_SOURCE;
    }
    if (unit != NULL) {
        clang_disposeTranslationUnit(unit);
    }
    if (clang_index != NULL) {
        clang_disposeIndex(clang_index);
    }
    return success;
}

RFrontendStatus r_frontend_emit_abi_record(RFrontendContext *context,
                                           const RFrontendArtifactOptions *artifact_options,
                                           const RFrontendHeaderOptions *options,
                                           RFrontendWriteFn writer,
                                           void *user_data) {
    RClangRequest *requests = NULL;
    size_t request_count = 0U;
    size_t request_capacity = 0U;
    RClangGenerator generator;
    RFrontendStatus status = R_FRONTEND_OK;
    const char *target_triple = NULL;
    size_t target_triple_length = 0U;
    bool io_failed = false;
    size_t index;

    if ((context == NULL) || (options == NULL) || (options->sysroot == NULL) || (writer == NULL) ||
        ((options->root_count != 0U) && (options->roots == NULL)) || !context->semantic_analyzed) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    if (!r_clang_check_toolchain(context, options)) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    if ((artifact_options != NULL) && (artifact_options->target_manifest != NULL)) {
        const uint8_t *triple = NULL;

        if (r_abi_manifest_target_triple(artifact_options->target_manifest,
                                         artifact_options->target_manifest_length,
                                         &triple,
                                         &target_triple_length)) {
            target_triple = (const char *)triple;
        }
    }
    context->header_report_length = 0U;
    (void)memset(&generator, 0, sizeof(generator));
    generator.context = context;
    if (!r_clang_requests(context, &requests, &request_count, &request_capacity)) {
        r_clang_dispose_requests(context, requests, request_count);
        return R_FRONTEND_OUT_OF_MEMORY;
    }
    if (!r_write_text(
            writer, user_data, "{\n  \"schema\": \"r-abi-record-0.1\",\n  \"records\": [\n")) {
        status = R_FRONTEND_IO_ERROR;
    }
    for (index = 0U; (status == R_FRONTEND_OK) && (index < request_count); ++index) {
        r_clang_generator_reset(&generator);
        generator.failed = false;
        if (r_clang_generate(&generator, &requests[index], options, &status) &&
            (((index != 0U) && !r_write_text(writer, user_data, ",\n")) ||
             !r_clang_write_record(&generator,
                                   &requests[index],
                                   options,
                                   target_triple,
                                   target_triple_length,
                                   writer,
                                   user_data,
                                   &io_failed))) {
            status = io_failed             ? R_FRONTEND_IO_ERROR
                     : generator.exhausted ? R_FRONTEND_OUT_OF_MEMORY
                                           : R_FRONTEND_INVALID_SOURCE;
        }
        if ((status != R_FRONTEND_OK) && (status != R_FRONTEND_IO_ERROR) &&
            (status != R_FRONTEND_OUT_OF_MEMORY)) {
            /* Name the record whose generation failed. */
            (void)r_clang_report_append(context, "abi record ", sizeof("abi record ") - 1U);
            (void)r_clang_report_append(context, requests[index].name, requests[index].name_length);
            (void)r_clang_report_append(context, "\n", 1U);
        }
    }
    if ((status == R_FRONTEND_OK) &&
        !r_write_text(writer, user_data, request_count == 0U ? "  ]\n}\n" : "\n  ]\n}\n")) {
        status = R_FRONTEND_IO_ERROR;
    }
    r_clang_generator_dispose(&generator);
    r_clang_dispose_requests(context, requests, request_count);
    return status;
}

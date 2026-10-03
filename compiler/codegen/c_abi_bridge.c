#include "frontend_internal.h"
#include "link_manifest.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/*
 * ABI verifier and bridge translation units for extern "C" imports (R-FFI-0042, R-FFI-0057,
 * R-CMAP-0017..0019, R-CMAP-0024..0026). Both units contain only the providers' feature-test
 * macros, the blocks' verified headers and declarations spelled from the import signatures;
 * neither includes an R runtime header. The verifier is compiled, never executed: a
 * function-pointer assignment with warnings as errors proves each prototype against its header,
 * a _Generic probe proves each imported object type and _Static_assert proves each verified
 * constant's value and type. The bridge defines one private forwarding function per import
 * whose spelling is header-owned: a reserved-class identifier or a typedef/reserved opaque type.
 */

/* One spelling: an inline buffer that grows on the context allocator. A declaration within the
   minimum translation limits (R-LIMIT-0001: 127 parameters, 256-scalar identifiers) exceeds any
   small fixed capacity, so none bounds a spelling; an exhausted allocator is reported as such.
   A spelling is initialized before use and disposed after it, and never copied. */
#define R_C_ABI_SPELL_INLINE 160U

typedef struct RCAbiSpell {
    RFrontendContext *context;
    char *bytes;
    size_t length;
    size_t capacity;
    char inline_bytes[R_C_ABI_SPELL_INLINE];
} RCAbiSpell;

/* Header spelling names header-owned types; private spelling matches the main unit's tags. */
typedef enum RCAbiSpellMode {
    R_C_ABI_SPELL_HEADER = 0,
    R_C_ABI_SPELL_PRIVATE
} RCAbiSpellMode;

/* One spelling pass. An import that passes R-declared @repr(C) structs names, per such leaf in
   visiting order (parameters, then the result, depth first), the record type node of the C
   type at that position (R-FFI-0021); the header spelling takes them one by one. */
typedef struct RCAbiSpeller {
    RCAbiSpellMode mode;
    const RCImportSlot *slots;
    uint32_t slot_count;
    uint32_t next_slot;
} RCAbiSpeller;

static RCAbiSpeller r_c_abi_plain_speller(RCAbiSpellMode mode) {
    RCAbiSpeller speller;

    speller.mode = mode;
    speller.slots = NULL;
    speller.slot_count = UINT32_C(0);
    speller.next_slot = UINT32_C(0);
    return speller;
}

/* The speller of an import from the first slot of position on (one-based parameter, zero for
   the result or an object's type), or of all its slots in order when whole is set. */
static RCAbiSpeller r_c_abi_import_speller(const RFrontendContext *context,
                                           const RSemanticSymbol *symbol,
                                           RCAbiSpellMode mode,
                                           bool whole,
                                           uint32_t position) {
    RCAbiSpeller speller = r_c_abi_plain_speller(mode);

    if (symbol->passes_r_struct &&
        ((size_t)symbol->first_c_import_slot + (size_t)symbol->c_import_slot_count <=
         context->c_import_slot_count)) {
        speller.slots = context->c_import_slots + symbol->first_c_import_slot;
        speller.slot_count = symbol->c_import_slot_count;
        if (!whole) {
            while ((speller.next_slot < speller.slot_count) &&
                   (speller.slots[speller.next_slot].position != position)) {
                speller.next_slot += 1U;
            }
        }
    }
    return speller;
}

static bool r_c_abi_intern_text(const RFrontendContext *context,
                                uint32_t intern_id,
                                const char **bytes,
                                size_t *length);

static const char *r_c_abi_spelling_for_token(RTokenKind token) {
    switch (token) {
#include "target_c_abi_spelling.generated.inc"
    default:
        return NULL;
    }
}

static const RSemanticType *r_c_abi_type(const RFrontendContext *context, RTypeId id) {
    if ((id == R_TYPE_ID_INVALID) || ((size_t)id > context->semantic_type_count)) {
        return NULL;
    }
    return &context->semantic_types[(size_t)id - 1U];
}

/* The unqualified value type behind an outermost const. */
static RTypeId r_c_abi_value_type(const RFrontendContext *context, RTypeId id) {
    const RSemanticType *type = r_c_abi_type(context, id);

    return ((type != NULL) && (type->kind == R_SEMANTIC_TYPE_CONST)) ? type->base : id;
}

static void r_c_abi_spell_init(RCAbiSpell *spell, const RFrontendContext *context) {
    spell->context = (RFrontendContext *)context;
    spell->bytes = spell->inline_bytes;
    spell->length = 0U;
    spell->capacity = sizeof(spell->inline_bytes);
    spell->inline_bytes[0] = '\0';
}

static void r_c_abi_spell_clear(RCAbiSpell *spell) {
    spell->length = 0U;
    spell->bytes[0] = '\0';
}

static void r_c_abi_spell_dispose(RCAbiSpell *spell) {
    if (spell->bytes != spell->inline_bytes) {
        r_context_free(spell->context, spell->bytes);
    }
    r_c_abi_spell_init(spell, spell->context);
}

static bool r_c_abi_spell_append_bytes(RCAbiSpell *spell, const char *text, size_t length) {
    if (length >= (spell->capacity - spell->length)) {
        size_t capacity = spell->capacity;
        char *grown;

        while (length >= (capacity - spell->length)) {
            if (capacity > (SIZE_MAX / 2U)) {
                spell->context->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
                return false;
            }
            capacity *= 2U;
        }
        grown = r_context_allocate(spell->context, capacity);
        if (grown == NULL) {
            return false;
        }
        (void)memcpy(grown, spell->bytes, spell->length + 1U);
        if (spell->bytes != spell->inline_bytes) {
            r_context_free(spell->context, spell->bytes);
        }
        spell->bytes = grown;
        spell->capacity = capacity;
    }
    (void)memcpy(spell->bytes + spell->length, text, length);
    spell->length += length;
    spell->bytes[spell->length] = '\0';
    return true;
}

static bool r_c_abi_spell_append(RCAbiSpell *spell, const char *text) {
    return r_c_abi_spell_append_bytes(spell, text, strlen(text));
}

/* A spelling that exhausted the allocator or a size limit fails for that reason, not as a
   construct outside the implemented bridge. */
static RFrontendStatus r_c_abi_resource_status(const RFrontendContext *context,
                                               RFrontendStatus status) {
    return ((status == R_FRONTEND_NOT_LOWERABLE) && (context->resource_status != R_FRONTEND_OK))
               ? context->resource_status
               : status;
}

static bool r_c_abi_spell_type(const RFrontendContext *context,
                               RTypeId type_id,
                               const char *inner,
                               RCAbiSpell *spell,
                               uint32_t depth,
                               RCAbiSpeller *speller);

/* The declarator after a type specifier; an array declarator follows it without a space, as
   the compiler's own spelling does ("unsigned char[4]"). */
static bool r_c_abi_spell_declarator(RCAbiSpell *spell, const char *inner) {
    return (inner[0] == '\0') ||
           ((inner[0] != '[') && r_c_abi_spell_append(spell, " ") &&
            r_c_abi_spell_append(spell, inner)) ||
           ((inner[0] == '[') && r_c_abi_spell_append(spell, inner));
}

/* Parameter list without names; "void" for an empty prototype; ", ..." for a variadic one. */
static bool r_c_abi_spell_parameters(const RFrontendContext *context,
                                     RTypeId first_parameter,
                                     uint64_t count,
                                     bool variadic,
                                     RCAbiSpell *spell,
                                     uint32_t depth,
                                     RCAbiSpeller *speller) {
    RTypeId parameter = first_parameter;
    uint64_t index;

    if (count == UINT64_C(0)) {
        return !variadic && r_c_abi_spell_append(spell, "void");
    }
    for (index = UINT64_C(0); index < count; ++index) {
        const RSemanticType *item = r_c_abi_type(context, parameter);

        if ((item == NULL) || (item->kind != R_SEMANTIC_TYPE_FUNCTION_PARAMETER)) {
            return false;
        }
        if (((index != UINT64_C(0)) && !r_c_abi_spell_append(spell, ", ")) ||
            !r_c_abi_spell_type(context, item->base, "", spell, depth + UINT32_C(1), speller)) {
            return false;
        }
        parameter = item->second;
    }
    return (parameter == R_TYPE_ID_INVALID) && (!variadic || r_c_abi_spell_append(spell, ", ..."));
}

/* Pointer to function: R (*inner)(P...). */
static bool r_c_abi_spell_function_pointer(const RFrontendContext *context,
                                           const RSemanticType *type,
                                           const char *inner,
                                           RCAbiSpell *spell,
                                           uint32_t depth,
                                           RCAbiSpeller *speller) {
    RCAbiSpell declarator;
    bool spelled;

    r_c_abi_spell_init(&declarator, context);
    spelled = r_c_abi_spell_append(&declarator, "(*") && r_c_abi_spell_append(&declarator, inner) &&
              r_c_abi_spell_append(&declarator, ")(") &&
              r_c_abi_spell_parameters(context,
                                       type->second,
                                       type->length,
                                       (type->flags & R_SEMANTIC_TYPE_FLAG_VARIADIC) != 0U,
                                       &declarator,
                                       depth,
                                       speller) &&
              r_c_abi_spell_append(&declarator, ")") &&
              r_c_abi_spell_type(
                  context, type->base, declarator.bytes, spell, depth + UINT32_C(1), speller);
    r_c_abi_spell_dispose(&declarator);
    return spelled;
}

/* Opaque or C-declared aggregate (R-FFI-0015..0017): tag or typedef spelling, or the main
   unit's private tag; a C-declared enumeration is its compatible integer in private mode. */
static bool r_c_abi_spell_opaque(const RFrontendContext *context,
                                 const RSemanticType *type,
                                 const char *inner,
                                 RCAbiSpell *spell,
                                 RCAbiSpeller *speller,
                                 uint32_t depth) {
    const uint32_t aggregate_id = (uint32_t)type->base;
    const RSemanticAggregate *aggregate;
    const char *bytes;
    size_t length;
    char private_tag[32];

    if ((aggregate_id == UINT32_C(0)) ||
        ((size_t)aggregate_id > context->semantic_aggregate_count)) {
        return false;
    }
    aggregate = &context->semantic_aggregates[(size_t)aggregate_id - 1U];
    if (aggregate->poisoned) {
        return false;
    }
    /* An R-declared fieldless @repr(C) enum is its compatible integer type (R-AGG-0005); the
       verifier proves the header's enum compatible with it through the prototype. */
    if (!aggregate->is_opaque && !aggregate->is_c_declared) {
        if (!aggregate->is_repr_c || aggregate->is_tagged) {
            return false;
        }
        if (aggregate->kind == R_SEMANTIC_AGGREGATE_ENUM) {
            return r_c_abi_spell_type(context,
                                      aggregate->enum_underlying_type,
                                      inner,
                                      spell,
                                      depth + UINT32_C(1),
                                      speller);
        }
        if (speller->mode == R_C_ABI_SPELL_PRIVATE) {
            /* The bridge never defines an R-declared struct: its tag stays incomplete there. */
            (void)snprintf(private_tag, sizeof(private_tag), "struct r_a%08" PRIu32, aggregate_id);
            if (!r_c_abi_spell_append(spell, private_tag)) {
                return false;
            }
        } else {
            /* R-FFI-0021: the C type the record found at this position. */
            const RAbiTypeNode *node;

            if ((speller->slots == NULL) || (speller->next_slot >= speller->slot_count) ||
                (speller->slots[speller->next_slot].type_node >= context->abi_type_node_count)) {
                return false;
            }
            node = &context->abi_type_nodes[speller->slots[speller->next_slot].type_node];
            speller->next_slot += 1U;
            if (node->typedef_name != NULL) {
                if (!r_c_abi_spell_append_bytes(
                        spell, node->typedef_name, node->typedef_name_length)) {
                    return false;
                }
            } else if ((node->tag_name == NULL) || !r_c_abi_spell_append(spell, "struct ") ||
                       !r_c_abi_spell_append_bytes(spell, node->tag_name, node->tag_name_length)) {
                return false;
            }
        }
        return r_c_abi_spell_declarator(spell, inner);
    }
    if ((aggregate->kind == R_SEMANTIC_AGGREGATE_ENUM) &&
        (speller->mode == R_C_ABI_SPELL_PRIVATE)) {
        return r_c_abi_spell_type(
            context, aggregate->enum_underlying_type, inner, spell, depth + UINT32_C(1), speller);
    }
    if ((speller->mode == R_C_ABI_SPELL_PRIVATE) && aggregate->c_name_private) {
        (void)snprintf(private_tag, sizeof(private_tag), "struct r_a%08" PRIu32, aggregate_id);
        if (!r_c_abi_spell_append(spell, private_tag)) {
            return false;
        }
    } else {
        if (aggregate->c_type_kind == R_C_TYPE_KIND_STRUCT) {
            if (!r_c_abi_spell_append(spell, "struct ")) {
                return false;
            }
        } else if (aggregate->c_type_kind == R_C_TYPE_KIND_UNION) {
            if (!r_c_abi_spell_append(spell, "union ")) {
                return false;
            }
        } else if (aggregate->c_type_kind == R_C_TYPE_KIND_ENUM) {
            if (!r_c_abi_spell_append(spell, "enum ")) {
                return false;
            }
        } else if (aggregate->c_type_kind != R_C_TYPE_KIND_TYPEDEF) {
            return false;
        }
        if (!r_c_abi_intern_text(context, aggregate->c_name_intern_id, &bytes, &length) ||
            !r_c_abi_spell_append_bytes(spell, bytes, length)) {
            return false;
        }
    }
    return r_c_abi_spell_declarator(spell, inner);
}

static bool r_c_abi_spell_type_with(const RFrontendContext *context,
                                    RTypeId type_id,
                                    const char *inner,
                                    RCAbiSpell *spell,
                                    uint32_t depth,
                                    RCAbiSpeller *speller,
                                    RCAbiSpell *declarator) {
    const RSemanticType *type = r_c_abi_type(context, type_id);

    if ((type == NULL) || (depth > context->options.limits.max_nesting)) {
        return false;
    }
    switch (type->kind) {
    case R_SEMANTIC_TYPE_CONST: {
        const RSemanticType *base = r_c_abi_type(context, type->base);

        if (base == NULL) {
            return false;
        }
        if ((base->kind == R_SEMANTIC_TYPE_C_ABI_NUMERIC) || (base->kind == R_SEMANTIC_TYPE_VOID) ||
            (base->kind == R_SEMANTIC_TYPE_STRUCT) || (base->kind == R_SEMANTIC_TYPE_ENUM)) {
            return r_c_abi_spell_append(spell, "const ") &&
                   r_c_abi_spell_type(
                       context, type->base, inner, spell, depth + UINT32_C(1), speller);
        }
        r_c_abi_spell_clear(declarator);
        return r_c_abi_spell_append(declarator, "const ") &&
               r_c_abi_spell_append(declarator, inner) &&
               r_c_abi_spell_type(
                   context, type->base, declarator->bytes, spell, depth + UINT32_C(1), speller);
    }
    case R_SEMANTIC_TYPE_RAW: {
        /* A shared raw pointer addresses a const-qualified pointee (raw const T*). */
        const RSemanticType *base = r_c_abi_type(context, type->base);
        const bool shared = (type->flags & R_SEMANTIC_TYPE_FLAG_SHARED) != 0U;
        const bool leading_const =
            (base != NULL) &&
            ((base->kind == R_SEMANTIC_TYPE_C_ABI_NUMERIC) ||
             (base->kind == R_SEMANTIC_TYPE_VOID) || (base->kind == R_SEMANTIC_TYPE_STRUCT) ||
             (base->kind == R_SEMANTIC_TYPE_ENUM));

        if (base == NULL) {
            return false;
        }
        r_c_abi_spell_clear(declarator);
        if (shared && !leading_const && !r_c_abi_spell_append(declarator, "const ")) {
            return false;
        }
        if (!r_c_abi_spell_append(declarator, "*") || !r_c_abi_spell_append(declarator, inner)) {
            return false;
        }
        if (shared && leading_const && !r_c_abi_spell_append(spell, "const ")) {
            return false;
        }
        return r_c_abi_spell_type(
            context, type->base, declarator->bytes, spell, depth + UINT32_C(1), speller);
    }
    case R_SEMANTIC_TYPE_RAW_FUNCTION:
        return r_c_abi_spell_function_pointer(context, type, inner, spell, depth, speller);
    case R_SEMANTIC_TYPE_FIXED_ARRAY: {
        /* A member array: T inner[N], parenthesized behind a pointer declarator. */
        char length[32];

        r_c_abi_spell_clear(declarator);
        (void)snprintf(length, sizeof(length), "[%" PRIu64 "]", type->length);
        if (((inner[0] == '*') &&
             (!r_c_abi_spell_append(declarator, "(") || !r_c_abi_spell_append(declarator, inner) ||
              !r_c_abi_spell_append(declarator, ")"))) ||
            ((inner[0] != '*') && !r_c_abi_spell_append(declarator, inner)) ||
            !r_c_abi_spell_append(declarator, length)) {
            return false;
        }
        return r_c_abi_spell_type(
            context, type->base, declarator->bytes, spell, depth + UINT32_C(1), speller);
    }
    case R_SEMANTIC_TYPE_STRUCT:
    case R_SEMANTIC_TYPE_ENUM:
        return r_c_abi_spell_opaque(context, type, inner, spell, speller, depth);
    case R_SEMANTIC_TYPE_C_ABI_NUMERIC: {
        const char *spelling = NULL;

        if (type->length <= (uint64_t)R_TOKEN_KIND_COUNT) {
            spelling = r_c_abi_spelling_for_token((RTokenKind)type->length);
        }
        if (spelling == NULL) {
            return false;
        }
        return r_c_abi_spell_append(spell, spelling) && r_c_abi_spell_declarator(spell, inner);
    }
    case R_SEMANTIC_TYPE_VOID:
    case R_SEMANTIC_TYPE_NEVER:
        /* A never result is a C void result that does not return. */
        return r_c_abi_spell_append(spell, "void") && r_c_abi_spell_declarator(spell, inner);
    default:
        return false;
    }
}

static bool r_c_abi_spell_type(const RFrontendContext *context,
                               RTypeId type_id,
                               const char *inner,
                               RCAbiSpell *spell,
                               uint32_t depth,
                               RCAbiSpeller *speller) {
    RCAbiSpell declarator;
    bool spelled;

    r_c_abi_spell_init(&declarator, context);
    spelled = r_c_abi_spell_type_with(context, type_id, inner, spell, depth, speller, &declarator);
    r_c_abi_spell_dispose(&declarator);
    return spelled;
}

static bool r_c_abi_intern_text(const RFrontendContext *context,
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

static const char *r_c_abi_module_name(const RFrontendContext *context, RSourceId source_id) {
    const RSource *source = r_get_source_const(context, source_id);

    return (source == NULL) || (source->module_name == NULL) ? "" : source->module_name;
}

/* Deterministic import order: module name, then C identifier, then declaration position. */
static int r_c_abi_compare_imports(const RFrontendContext *context,
                                   const RSemanticSymbol *left,
                                   const RSemanticSymbol *right) {
    const char *left_bytes;
    size_t left_length;
    const char *right_bytes;
    size_t right_length;
    int comparison = strcmp(r_c_abi_module_name(context, left->module_source),
                            r_c_abi_module_name(context, right->module_source));
    size_t common;

    if (comparison != 0) {
        return comparison;
    }
    if (!r_c_abi_intern_text(context, left->c_name_intern_id, &left_bytes, &left_length) ||
        !r_c_abi_intern_text(context, right->c_name_intern_id, &right_bytes, &right_length)) {
        return 0;
    }
    common = left_length < right_length ? left_length : right_length;
    comparison = memcmp(left_bytes, right_bytes, common);
    if (comparison != 0) {
        return comparison;
    }
    if (left_length != right_length) {
        return left_length < right_length ? -1 : 1;
    }
    if (left->name_span.start != right->name_span.start) {
        return left->name_span.start < right->name_span.start ? -1 : 1;
    }
    return 0;
}

static bool r_c_abi_symbol_is_import(const RSemanticSymbol *symbol) {
    return symbol->is_import && !symbol->poisoned && symbol->signature_supported &&
           ((symbol->kind == R_SEMANTIC_SYMBOL_FUNCTION) ||
            (symbol->kind == R_SEMANTIC_SYMBOL_MODULE_OBJECT) ||
            (symbol->kind == R_SEMANTIC_SYMBOL_MODULE_CONSTANT));
}

/* Sorted list of import symbol ids; allocated with the context allocator. */
static bool r_c_abi_collect_imports(const RFrontendContext *context,
                                    RFrontendContext *allocator,
                                    RSymbolId **imports,
                                    size_t *count,
                                    bool *unresolved) {
    size_t index;
    size_t capacity = 0U;

    *imports = NULL;
    *count = 0U;
    *unresolved = false;
    for (index = 0U; index < context->semantic_symbol_count; ++index) {
        const RSemanticSymbol *symbol = &context->semantic_symbols[index];
        size_t position;

        if (!r_c_abi_symbol_is_import(symbol)) {
            continue;
        }
        if (symbol->link_provider == UINT32_C(0)) {
            *unresolved = true;
        }
        if (!r_grow_array(allocator, (void **)imports, &capacity, sizeof(**imports), *count + 1U)) {
            return false;
        }
        position = *count;
        while ((position != 0U) &&
               (r_c_abi_compare_imports(context,
                                        &context->semantic_symbols[(*imports)[position - 1U] - 1U],
                                        symbol) > 0)) {
            (*imports)[position] = (*imports)[position - 1U];
            position -= 1U;
        }
        (*imports)[position] = (RSymbolId)(index + 1U);
        *count += 1U;
    }
    return true;
}

static int r_c_abi_compare_blocks(const RFrontendContext *context,
                                  const RExternBlockRecord *left,
                                  const RExternBlockRecord *right) {
    const int comparison = strcmp(r_c_abi_module_name(context, left->source),
                                  r_c_abi_module_name(context, right->source));

    if (comparison != 0) {
        return comparison;
    }
    if (left->span_start != right->span_start) {
        return left->span_start < right->span_start ? -1 : 1;
    }
    return 0;
}

/* Blocks in ascending (module, position) order; the k-th smallest is found without allocation. */
static const RExternBlockRecord *r_c_abi_block_at_rank(const RFrontendContext *context,
                                                       size_t rank) {
    const RExternBlockRecord *previous = NULL;
    size_t step;

    for (step = 0U; step <= rank; ++step) {
        const RExternBlockRecord *best = NULL;
        size_t index;

        for (index = 0U; index < context->extern_block_count; ++index) {
            const RExternBlockRecord *candidate = &context->extern_blocks[index];

            if ((previous != NULL) && (r_c_abi_compare_blocks(context, candidate, previous) <= 0)) {
                continue;
            }
            if ((best == NULL) || (r_c_abi_compare_blocks(context, candidate, best) < 0)) {
                best = candidate;
            }
        }
        if (best == NULL) {
            return NULL;
        }
        previous = best;
    }
    return previous;
}

static bool r_c_abi_write_definitions(const RFrontendContext *context,
                                      const RSymbolId *imports,
                                      size_t import_count,
                                      RFrontendWriteFn writer,
                                      void *user_data) {
    const RLinkDefinitionRecord *previous = NULL;
    bool more = true;

    /* Ascending macro-name order, one line per distinct name, providers of used imports only. */
    while (more) {
        const RLinkDefinitionRecord *best = NULL;
        size_t import_index;

        for (import_index = 0U; import_index < import_count; ++import_index) {
            const RSemanticSymbol *symbol = &context->semantic_symbols[imports[import_index] - 1U];
            const RLinkProviderRecord *provider;
            size_t index;

            if ((symbol->link_provider == UINT32_C(0)) ||
                ((size_t)symbol->link_provider > context->link_provider_count)) {
                return false;
            }
            provider = &context->link_providers[(size_t)symbol->link_provider - 1U];
            for (index = 0U; index < provider->definition_count; ++index) {
                const RLinkDefinitionRecord *definition =
                    &context->link_definitions[provider->first_definition + index];
                int comparison;

                if (previous != NULL) {
                    const size_t common = definition->name_length < previous->name_length
                                              ? definition->name_length
                                              : previous->name_length;

                    comparison = memcmp(definition->name, previous->name, common);
                    if ((comparison < 0) ||
                        ((comparison == 0) && (definition->name_length <= previous->name_length))) {
                        continue;
                    }
                }
                if (best != NULL) {
                    const size_t common = definition->name_length < best->name_length
                                              ? definition->name_length
                                              : best->name_length;

                    comparison = memcmp(definition->name, best->name, common);
                    if ((comparison > 0) ||
                        ((comparison == 0) && (definition->name_length >= best->name_length))) {
                        continue;
                    }
                }
                best = definition;
            }
        }
        if (best == NULL) {
            more = false;
        } else {
            if (!r_write_text(writer, user_data, "#define ") ||
                !writer(user_data, best->name, best->name_length) ||
                !r_write_text(writer, user_data, " ") ||
                (best->value_length == 0U ? !r_write_text(writer, user_data, "1")
                                          : !writer(user_data, best->value, best->value_length)) ||
                !r_write_text(writer, user_data, "\n")) {
                return false;
            }
            previous = best;
        }
    }
    return true;
}

/* Headers in block order (R-FFI-0043); a repeated identical spelling is included once. */
static bool r_c_abi_write_headers(const RFrontendContext *context,
                                  RFrontendContext *allocator,
                                  RFrontendWriteFn writer,
                                  void *user_data,
                                  bool *any) {
    uint32_t *seen = NULL;
    size_t seen_count = 0U;
    size_t seen_capacity = 0U;
    size_t rank;
    bool success = false;

    *any = false;
    for (rank = 0U; rank < context->extern_block_count; ++rank) {
        const RExternBlockRecord *block = r_c_abi_block_at_rank(context, rank);
        uint32_t header_index;

        if (block == NULL) {
            goto cleanup;
        }
        for (header_index = UINT32_C(0); header_index < block->header_count; ++header_index) {
            const size_t position = (size_t)block->first_header + (size_t)header_index;
            uint32_t intern_id;
            const char *bytes;
            size_t length;
            size_t seen_index;
            bool duplicate = false;

            if (position >= context->extern_header_count) {
                goto cleanup;
            }
            intern_id = context->extern_headers[position];
            for (seen_index = 0U; seen_index < seen_count; ++seen_index) {
                if (seen[seen_index] == intern_id) {
                    duplicate = true;
                }
            }
            if (duplicate) {
                continue;
            }
            if (!r_grow_array(
                    allocator, (void **)&seen, &seen_capacity, sizeof(*seen), seen_count + 1U)) {
                goto cleanup;
            }
            seen[seen_count] = intern_id;
            seen_count += 1U;
            if (!r_c_abi_intern_text(context, intern_id, &bytes, &length) ||
                !r_write_text(writer, user_data, "#include <") ||
                !writer(user_data, bytes, length) || !r_write_text(writer, user_data, ">\n")) {
                goto cleanup;
            }
            *any = true;
        }
    }
    success = true;

cleanup:
    r_context_free(allocator, seen);
    return success;
}

static bool r_c_abi_write_c_name(const RFrontendContext *context,
                                 const RSemanticSymbol *symbol,
                                 RFrontendWriteFn writer,
                                 void *user_data) {
    const char *bytes;
    size_t length;

    return r_c_abi_intern_text(context, symbol->c_name_intern_id, &bytes, &length) &&
           writer(user_data, bytes, length);
}

/* Whether a signature type names a private (typedef or reserved) opaque spelling. */
static bool r_c_abi_type_mentions_private_opaque(const RFrontendContext *context,
                                                 RTypeId type_id,
                                                 uint32_t depth) {
    const RSemanticType *type = r_c_abi_type(context, type_id);

    if ((type == NULL) || (depth > context->options.limits.max_nesting)) {
        return false;
    }
    if ((type->kind == R_SEMANTIC_TYPE_CONST) || (type->kind == R_SEMANTIC_TYPE_RAW)) {
        return r_c_abi_type_mentions_private_opaque(context, type->base, depth + UINT32_C(1));
    }
    if (type->kind == R_SEMANTIC_TYPE_RAW_FUNCTION) {
        RTypeId parameter = type->second;
        uint64_t index;

        if (r_c_abi_type_mentions_private_opaque(context, type->base, depth + UINT32_C(1))) {
            return true;
        }
        for (index = UINT64_C(0); index < type->length; ++index) {
            const RSemanticType *item = r_c_abi_type(context, parameter);

            if ((item == NULL) || (item->kind != R_SEMANTIC_TYPE_FUNCTION_PARAMETER)) {
                return false;
            }
            if (r_c_abi_type_mentions_private_opaque(context, item->base, depth + UINT32_C(1))) {
                return true;
            }
            parameter = item->second;
        }
        return false;
    }
    if (type->kind == R_SEMANTIC_TYPE_STRUCT) {
        const uint32_t aggregate_id = (uint32_t)type->base;

        return (aggregate_id != UINT32_C(0)) &&
               ((size_t)aggregate_id <= context->semantic_aggregate_count) &&
               context->semantic_aggregates[(size_t)aggregate_id - 1U].c_name_private;
    }
    return false;
}

/* A by-value struct whose header spelling is private: the bridge converts it with memcpy. */
static bool r_c_abi_type_is_private_value(const RFrontendContext *context, RTypeId type_id) {
    const RSemanticType *type = r_c_abi_type(context, r_c_abi_value_type(context, type_id));

    return (type != NULL) && (type->kind == R_SEMANTIC_TYPE_STRUCT) &&
           r_c_abi_type_mentions_private_opaque(context, type_id, UINT32_C(0));
}

/* Whether a type names an R-declared @repr(C) struct at any depth (R-FFI-0021). */
static bool
r_c_abi_type_mentions_r_struct(const RFrontendContext *context, RTypeId type_id, uint32_t depth) {
    const RSemanticType *type = r_c_abi_type(context, type_id);

    if ((type == NULL) || (depth > context->options.limits.max_nesting)) {
        return false;
    }
    if ((type->kind == R_SEMANTIC_TYPE_CONST) || (type->kind == R_SEMANTIC_TYPE_RAW) ||
        (type->kind == R_SEMANTIC_TYPE_FIXED_ARRAY)) {
        return r_c_abi_type_mentions_r_struct(context, type->base, depth + UINT32_C(1));
    }
    if (type->kind == R_SEMANTIC_TYPE_RAW_FUNCTION) {
        RTypeId parameter = type->second;
        uint64_t index;

        if (r_c_abi_type_mentions_r_struct(context, type->base, depth + UINT32_C(1))) {
            return true;
        }
        for (index = UINT64_C(0); index < type->length; ++index) {
            const RSemanticType *item = r_c_abi_type(context, parameter);

            if ((item == NULL) || (item->kind != R_SEMANTIC_TYPE_FUNCTION_PARAMETER)) {
                return false;
            }
            if (r_c_abi_type_mentions_r_struct(context, item->base, depth + UINT32_C(1))) {
                return true;
            }
            parameter = item->second;
        }
        return false;
    }
    if (type->kind == R_SEMANTIC_TYPE_STRUCT) {
        const uint32_t aggregate_id = (uint32_t)type->base;
        const RSemanticAggregate *aggregate =
            ((aggregate_id == UINT32_C(0)) ||
             ((size_t)aggregate_id > context->semantic_aggregate_count))
                ? NULL
                : &context->semantic_aggregates[(size_t)aggregate_id - 1U];

        return (aggregate != NULL) && !aggregate->is_opaque && !aggregate->is_c_declared;
    }
    return false;
}

/* An R-declared struct passed by value: the main unit and the bridge exchange its address. */
static bool r_c_abi_type_is_r_struct_value(const RFrontendContext *context, RTypeId type_id) {
    const RSemanticType *type = r_c_abi_type(context, r_c_abi_value_type(context, type_id));

    return (type != NULL) && (type->kind == R_SEMANTIC_TYPE_STRUCT) &&
           r_c_abi_type_mentions_r_struct(context, type_id, UINT32_C(0));
}

/* Exported header spelling for record verification (R-FFI-0017). */
bool r_c_abi_spell_header_type(const RFrontendContext *context,
                               RTypeId type_id,
                               char *buffer,
                               size_t capacity) {
    RCAbiSpell spell;
    bool spelled;

    RCAbiSpeller speller = r_c_abi_plain_speller(R_C_ABI_SPELL_HEADER);

    r_c_abi_spell_init(&spell, context);
    spelled = (buffer != NULL) && (capacity != 0U) &&
              r_c_abi_spell_type(context, type_id, "", &spell, UINT32_C(0), &speller) &&
              (spell.length < capacity);
    if (spelled) {
        (void)memcpy(buffer, spell.bytes, spell.length + 1U);
    }
    r_c_abi_spell_dispose(&spell);
    return spelled;
}

/* Number of C-declared aggregates the verifier shall probe (R-FFI-0041). */
static size_t r_c_abi_probed_aggregate_count(const RFrontendContext *context) {
    size_t index;
    size_t count = 0U;

    for (index = 0U; index < context->semantic_aggregate_count; ++index) {
        const RSemanticAggregate *aggregate = &context->semantic_aggregates[index];

        if (aggregate->is_c_declared && !aggregate->poisoned) {
            count += 1U;
        }
    }
    return count + context->c_repr_pair_count;
}

/* Two's-complement constant bits as a C integer constant expression cast to CTYPE. */
static bool r_c_abi_spell_constant_value(const RFrontendContext *context,
                                         const RSemanticSymbol *symbol,
                                         RCAbiSpell *spell) {
    const RSemanticTypeKind representation =
        r_semantic_integer_representation(context, symbol->type);
    uint64_t width;
    bool is_signed;
    uint64_t mask;
    uint64_t sign_bit;
    uint64_t bits = symbol->integer_value;
    char text[48];
    RCAbiSpeller header_speller = r_c_abi_plain_speller(R_C_ABI_SPELL_HEADER);

    switch (representation) {
    case R_SEMANTIC_TYPE_I8:
    case R_SEMANTIC_TYPE_U8:
        width = UINT64_C(8);
        break;
    case R_SEMANTIC_TYPE_I16:
    case R_SEMANTIC_TYPE_U16:
        width = UINT64_C(16);
        break;
    case R_SEMANTIC_TYPE_I32:
    case R_SEMANTIC_TYPE_U32:
        width = UINT64_C(32);
        break;
    case R_SEMANTIC_TYPE_I64:
    case R_SEMANTIC_TYPE_U64:
    case R_SEMANTIC_TYPE_ISIZE:
    case R_SEMANTIC_TYPE_USIZE:
        width = UINT64_C(64);
        break;
    default:
        return false;
    }
    is_signed = (representation == R_SEMANTIC_TYPE_I8) || (representation == R_SEMANTIC_TYPE_I16) ||
                (representation == R_SEMANTIC_TYPE_I32) ||
                (representation == R_SEMANTIC_TYPE_I64) ||
                (representation == R_SEMANTIC_TYPE_ISIZE);
    mask = width == UINT64_C(64) ? UINT64_MAX : (UINT64_C(1) << width) - UINT64_C(1);
    sign_bit = UINT64_C(1) << (width - UINT64_C(1));
    if ((bits & ~mask) != UINT64_C(0)) {
        return false;
    }
    /* The probe compares against the unqualified value type: lvalue conversion drops const. */
    if (!r_c_abi_spell_append(spell, "((") ||
        !r_c_abi_spell_type(context,
                            r_c_abi_value_type(context, symbol->type),
                            "",
                            spell,
                            UINT32_C(0),
                            &header_speller) ||
        !r_c_abi_spell_append(spell, ")")) {
        return false;
    }
    if (!is_signed) {
        (void)snprintf(text, sizeof(text), "%" PRIu64 "U", bits);
    } else if ((bits & sign_bit) == UINT64_C(0)) {
        (void)snprintf(text, sizeof(text), "%" PRIu64, bits);
    } else {
        const uint64_t magnitude = (UINT64_C(0) - bits) & mask;

        /* The most negative value has no positive literal: spell it as -(limit) - 1. */
        if (magnitude == sign_bit) {
            (void)snprintf(text, sizeof(text), "(-%" PRIu64 " - 1)", magnitude - UINT64_C(1));
        } else {
            (void)snprintf(text, sizeof(text), "(-%" PRIu64 ")", magnitude);
        }
    }
    return r_c_abi_spell_append(spell, text) && r_c_abi_spell_append(spell, ")");
}

static RFrontendStatus r_c_abi_prepare(const RFrontendContext *context,
                                       RFrontendWriteFn writer,
                                       RSymbolId **imports,
                                       size_t *import_count) {
    bool unresolved;

    if ((context == NULL) || (writer == NULL)) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    if (!context->semantic_analyzed) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    /* The context allocator is used for scratch only; the context contents stay unchanged. */
    if (!r_c_abi_collect_imports(
            context, (RFrontendContext *)context, imports, import_count, &unresolved)) {
        return context->resource_status != R_FRONTEND_OK ? context->resource_status
                                                         : R_FRONTEND_OUT_OF_MEMORY;
    }
    if (unresolved) {
        r_context_free((RFrontendContext *)context, *imports);
        *imports = NULL;
        return R_FRONTEND_NOT_LOWERABLE;
    }
    return R_FRONTEND_OK;
}

/* Writes a variant or enumerator value in the underlying type as a C constant expression. */
static bool r_c_abi_write_variant_value(const RFrontendContext *context,
                                        const RSemanticAggregate *aggregate,
                                        uint64_t bits,
                                        RFrontendWriteFn writer,
                                        void *user_data) {
    const RSemanticTypeKind representation =
        r_semantic_integer_representation(context, aggregate->enum_underlying_type);
    bool is_signed;
    uint32_t width;
    uint64_t mask;
    char text[48];

    if (!r_semantic_integer_properties_public(representation, &is_signed, &width)) {
        return false;
    }
    mask = width == UINT32_C(64) ? UINT64_MAX : (UINT64_C(1) << width) - UINT64_C(1);
    if (is_signed && ((bits & (UINT64_C(1) << (width - UINT32_C(1)))) != UINT64_C(0))) {
        const uint64_t magnitude = (UINT64_C(0) - bits) & mask;

        if (magnitude == (UINT64_C(1) << (width - UINT32_C(1)))) {
            (void)snprintf(text, sizeof(text), "(-%" PRIu64 " - 1)", magnitude - UINT64_C(1));
        } else {
            (void)snprintf(text, sizeof(text), "(-%" PRIu64 ")", magnitude);
        }
    } else {
        (void)snprintf(text, sizeof(text), "%" PRIu64, bits & mask);
    }
    return r_write_text(writer, user_data, text);
}

/* Probes of one C-declared enumeration: compatible integer type and every enumerator. */
static bool r_c_abi_write_enum_probes_with(const RFrontendContext *context,
                                           const RSemanticAggregate *aggregate,
                                           const RCAbiSpell *spelling,
                                           const char *name,
                                           size_t name_length,
                                           RFrontendWriteFn writer,
                                           void *user_data,
                                           RCAbiSpell *underlying) {
    uint32_t variant_index;
    RCAbiSpeller header_speller = r_c_abi_plain_speller(R_C_ABI_SPELL_HEADER);

    if (!r_c_abi_spell_type(context,
                            aggregate->enum_underlying_type,
                            "",
                            underlying,
                            UINT32_C(0),
                            &header_speller) ||
        !r_write_text(writer, user_data, "_Static_assert(_Generic((") ||
        !writer(user_data, spelling->bytes, spelling->length) ||
        !r_write_text(writer, user_data, ")0, ") ||
        !writer(user_data, underlying->bytes, underlying->length) ||
        !r_write_text(writer, user_data, ": 1, default: 0), \"R-FFI-0017 representation: ") ||
        !writer(user_data, name, name_length) || !r_write_text(writer, user_data, "\");\n")) {
        return false;
    }
    for (variant_index = UINT32_C(0); variant_index < aggregate->variant_count; ++variant_index) {
        const size_t position = (size_t)aggregate->first_variant + (size_t)variant_index;
        const RSemanticVariant *variant;
        const char *variant_name;
        size_t variant_name_length;

        if (position >= context->semantic_variant_count) {
            return false;
        }
        variant = &context->semantic_variants[position];
        if (!r_c_abi_intern_text(
                context, variant->name_intern_id, &variant_name, &variant_name_length) ||
            !r_write_text(writer, user_data, "_Static_assert((") ||
            !writer(user_data, variant_name, variant_name_length) ||
            !r_write_text(writer, user_data, ") == ") ||
            !r_c_abi_write_variant_value(context, aggregate, variant->value, writer, user_data) ||
            !r_write_text(writer, user_data, ", \"R-FFI-0017 enumerator: ") ||
            !writer(user_data, variant_name, variant_name_length) ||
            !r_write_text(writer, user_data, "\");\n")) {
            return false;
        }
    }
    return true;
}

static bool r_c_abi_write_enum_probes(const RFrontendContext *context,
                                      const RSemanticAggregate *aggregate,
                                      const RCAbiSpell *spelling,
                                      const char *name,
                                      size_t name_length,
                                      RFrontendWriteFn writer,
                                      void *user_data) {
    RCAbiSpell underlying;
    bool written;

    r_c_abi_spell_init(&underlying, context);
    written = r_c_abi_write_enum_probes_with(
        context, aggregate, spelling, name, name_length, writer, user_data, &underlying);
    r_c_abi_spell_dispose(&underlying);
    return written;
}

/* Probes of one struct: size, alignment, every member offset and type (R-FFI-0042). */
static bool r_c_abi_write_struct_probes_with(const RFrontendContext *context,
                                             const char *rule,
                                             const RSemanticAggregate *aggregate,
                                             const RCAbiSpell *spelling,
                                             const char *name,
                                             size_t name_length,
                                             RFrontendWriteFn writer,
                                             void *user_data,
                                             RCAbiSpell *field_spelling) {
    uint64_t size;
    uint64_t alignment;
    uint64_t offset = UINT64_C(0);
    uint32_t field_index;
    char number[32];
    RCAbiSpeller header_speller = r_c_abi_plain_speller(R_C_ABI_SPELL_HEADER);

    if (!r_semantic_type_layout(context, aggregate->type, &size, &alignment)) {
        return false;
    }
    (void)snprintf(number, sizeof(number), "%" PRIu64 "U", size);
    if (!r_write_text(writer, user_data, "_Static_assert(sizeof(") ||
        !writer(user_data, spelling->bytes, spelling->length) ||
        !r_write_text(writer, user_data, ") == ") || !r_write_text(writer, user_data, number) ||
        !r_write_text(writer, user_data, ", \"") || !r_write_text(writer, user_data, rule) ||
        !r_write_text(writer, user_data, " size: ") || !writer(user_data, name, name_length) ||
        !r_write_text(writer, user_data, "\");\n")) {
        return false;
    }
    (void)snprintf(number, sizeof(number), "%" PRIu64 "U", alignment);
    if (!r_write_text(writer, user_data, "_Static_assert(_Alignof(") ||
        !writer(user_data, spelling->bytes, spelling->length) ||
        !r_write_text(writer, user_data, ") == ") || !r_write_text(writer, user_data, number) ||
        !r_write_text(writer, user_data, ", \"") || !r_write_text(writer, user_data, rule) ||
        !r_write_text(writer, user_data, " alignment: ") || !writer(user_data, name, name_length) ||
        !r_write_text(writer, user_data, "\");\n")) {
        return false;
    }
    for (field_index = UINT32_C(0); field_index < aggregate->field_count; ++field_index) {
        const size_t position = (size_t)aggregate->first_field + (size_t)field_index;
        const RSemanticField *field;
        const char *field_name;
        size_t field_name_length;
        uint64_t field_size;
        uint64_t field_alignment;
        bool typed;
        bool is_array;

        if (position >= context->semantic_field_count) {
            return false;
        }
        field = &context->semantic_fields[position];
        r_c_abi_spell_clear(field_spelling);
        /* A member whose R type names an R-declared struct has no header spelling of its own;
           the record proved its type tree (R-FFI-0041). */
        typed = !r_c_abi_type_mentions_r_struct(context, field->type, UINT32_C(0));
        /* An array member is probed through its address: lvalue conversion would decay it. */
        {
            const RSemanticType *field_value =
                r_c_abi_type(context, r_c_abi_value_type(context, field->type));

            is_array = (field_value != NULL) && (field_value->kind == R_SEMANTIC_TYPE_FIXED_ARRAY);
        }
        if (!r_c_abi_intern_text(context, field->name_intern_id, &field_name, &field_name_length) ||
            !r_semantic_type_layout(context, field->type, &field_size, &field_alignment) ||
            (field_alignment == UINT64_C(0)) ||
            (typed && !r_c_abi_spell_type(context,
                                          field->type,
                                          is_array ? "*" : "",
                                          field_spelling,
                                          UINT32_C(0),
                                          &header_speller))) {
            return false;
        }
        offset += (field_alignment - (offset % field_alignment)) % field_alignment;
        (void)snprintf(number, sizeof(number), "%" PRIu64 "U", offset);
        if (!r_write_text(writer, user_data, "_Static_assert(offsetof(") ||
            !writer(user_data, spelling->bytes, spelling->length) ||
            !r_write_text(writer, user_data, ", ") ||
            !writer(user_data, field_name, field_name_length) ||
            !r_write_text(writer, user_data, ") == ") || !r_write_text(writer, user_data, number) ||
            !r_write_text(writer, user_data, ", \"") || !r_write_text(writer, user_data, rule) ||
            !r_write_text(writer, user_data, " offset: ") ||
            !writer(user_data, name, name_length) || !r_write_text(writer, user_data, ".") ||
            !writer(user_data, field_name, field_name_length) ||
            !r_write_text(writer, user_data, "\");\n")) {
            return false;
        }
        if (typed &&
            (!r_write_text(writer,
                           user_data,
                           is_array ? "_Static_assert(_Generic(&(("
                                    : "_Static_assert(_Generic(((") ||
             !writer(user_data, spelling->bytes, spelling->length) ||
             !r_write_text(writer, user_data, " *)0)->") ||
             !writer(user_data, field_name, field_name_length) ||
             !r_write_text(writer, user_data, ", ") ||
             !writer(user_data, field_spelling->bytes, field_spelling->length) ||
             !r_write_text(writer, user_data, ": 1, default: 0), \"") ||
             !r_write_text(writer, user_data, rule) ||
             !r_write_text(writer, user_data, " type: ") || !writer(user_data, name, name_length) ||
             !r_write_text(writer, user_data, ".") ||
             !writer(user_data, field_name, field_name_length) ||
             !r_write_text(writer, user_data, "\");\n"))) {
            return false;
        }
        offset += field_size;
    }
    return true;
}

static bool r_c_abi_write_struct_probes(const RFrontendContext *context,
                                        const char *rule,
                                        const RSemanticAggregate *aggregate,
                                        const RCAbiSpell *spelling,
                                        const char *name,
                                        size_t name_length,
                                        RFrontendWriteFn writer,
                                        void *user_data) {
    RCAbiSpell field_spelling;
    bool written;

    r_c_abi_spell_init(&field_spelling, context);
    written = r_c_abi_write_struct_probes_with(
        context, rule, aggregate, spelling, name, name_length, writer, user_data, &field_spelling);
    r_c_abi_spell_dispose(&field_spelling);
    return written;
}

/* R-FFI-0041/R-FFI-0042: probes of every C-declared aggregate against the headers. */
static bool r_c_abi_write_aggregate_probes_with(const RFrontendContext *context,
                                                RFrontendWriteFn writer,
                                                void *user_data,
                                                RCAbiSpell *spelling) {
    size_t index;

    for (index = 0U; index < context->semantic_aggregate_count; ++index) {
        const RSemanticAggregate *aggregate = &context->semantic_aggregates[index];
        const char *name;
        size_t name_length;
        RCAbiSpeller header_speller = r_c_abi_plain_speller(R_C_ABI_SPELL_HEADER);

        if (!aggregate->is_c_declared || aggregate->poisoned) {
            continue;
        }
        r_c_abi_spell_clear(spelling);
        if (!r_c_abi_spell_type(
                context, aggregate->type, "", spelling, UINT32_C(0), &header_speller) ||
            !r_c_abi_intern_text(context, aggregate->c_name_intern_id, &name, &name_length) ||
            !r_write_text(writer, user_data, "/* ") ||
            !r_write_text(
                writer, user_data, r_c_abi_module_name(context, aggregate->module_source)) ||
            !r_write_text(writer, user_data, " */\n")) {
            return false;
        }
        if (aggregate->kind == R_SEMANTIC_AGGREGATE_ENUM
                ? !r_c_abi_write_enum_probes(
                      context, aggregate, spelling, name, name_length, writer, user_data)
                : !r_c_abi_write_struct_probes(context,
                                               "R-FFI-0017",
                                               aggregate,
                                               spelling,
                                               name,
                                               name_length,
                                               writer,
                                               user_data)) {
            return false;
        }
    }
    /* R-FFI-0041: every R-declared struct against the C struct its record position names. */
    for (index = 0U; index < context->c_repr_pair_count; ++index) {
        const RCReprPair *pair = &context->c_repr_pairs[index];
        const RSemanticAggregate *aggregate;
        const RAbiRecordType *type;

        if ((pair->aggregate_id == UINT32_C(0)) ||
            ((size_t)pair->aggregate_id > context->semantic_aggregate_count) ||
            (pair->abi_type >= context->abi_type_count) || !pair->verified) {
            return false;
        }
        aggregate = &context->semantic_aggregates[(size_t)pair->aggregate_id - 1U];
        type = &context->abi_types[pair->abi_type];
        r_c_abi_spell_clear(spelling);
        if (((type->kind != R_C_TYPE_KIND_TYPEDEF) && !r_c_abi_spell_append(spelling, "struct ")) ||
            !r_c_abi_spell_append_bytes(spelling, type->c_name, type->c_name_length)) {
            return false;
        }
        if (!r_write_text(writer, user_data, "/* ") ||
            !r_write_text(
                writer, user_data, r_c_abi_module_name(context, aggregate->module_source)) ||
            !r_write_text(writer, user_data, " */\n") ||
            !r_c_abi_write_struct_probes(context,
                                         "R-FFI-0041",
                                         aggregate,
                                         spelling,
                                         type->c_name,
                                         type->c_name_length,
                                         writer,
                                         user_data)) {
            return false;
        }
    }
    return true;
}

static bool r_c_abi_write_aggregate_probes(const RFrontendContext *context,
                                           RFrontendWriteFn writer,
                                           void *user_data) {
    RCAbiSpell spelling;
    bool written;

    r_c_abi_spell_init(&spelling, context);
    written = r_c_abi_write_aggregate_probes_with(context, writer, user_data, &spelling);
    r_c_abi_spell_dispose(&spelling);
    return written;
}

RFrontendStatus r_frontend_emit_abi_verifier(const RFrontendContext *context,
                                             const RFrontendArtifactOptions *options,
                                             RFrontendWriteFn writer,
                                             void *user_data) {
    RSymbolId *imports = NULL;
    size_t import_count = 0U;
    size_t index;
    bool any_header;
    RCAbiSpell spell;
    RCAbiSpell declarator;
    RFrontendStatus status = r_c_abi_prepare(context, writer, &imports, &import_count);

    (void)options;
    if (status != R_FRONTEND_OK) {
        return status;
    }
    r_c_abi_spell_init(&spell, context);
    r_c_abi_spell_init(&declarator, context);
    status = R_FRONTEND_IO_ERROR;
    if (!r_write_text(
            writer,
            user_data,
            "/* Generated by the R 0.1 reference frontend: extern \"C\" ABI verifier. */\n")) {
        goto cleanup;
    }
    if ((import_count == 0U) && (r_c_abi_probed_aggregate_count(context) == 0U)) {
        status = r_write_text(writer, user_data, "typedef int r_verify_empty_translation_unit;\n")
                     ? R_FRONTEND_OK
                     : R_FRONTEND_IO_ERROR;
        goto cleanup;
    }
    if (!r_c_abi_write_definitions(context, imports, import_count, writer, user_data) ||
        !r_c_abi_write_headers(
            context, (RFrontendContext *)context, writer, user_data, &any_header) ||
        !r_write_text(writer, user_data, "#include <stddef.h>\n\n")) {
        goto cleanup;
    }
    for (index = 0U; index < import_count; ++index) {
        const RSemanticSymbol *symbol = &context->semantic_symbols[imports[index] - 1U];
        char object_name[32];
        /* The whole prototype visits the R-declared struct positions in slot order. */
        RCAbiSpeller header_speller =
            r_c_abi_import_speller(context, symbol, R_C_ABI_SPELL_HEADER, true, UINT32_C(0));

        r_c_abi_spell_clear(&spell);
        r_c_abi_spell_clear(&declarator);
        if (!r_write_text(writer, user_data, "/* ") ||
            !r_write_text(writer, user_data, r_c_abi_module_name(context, symbol->module_source)) ||
            !r_write_text(writer, user_data, " */\n")) {
            goto cleanup;
        }
        if (symbol->kind == R_SEMANTIC_SYMBOL_MODULE_CONSTANT) {
            /* R-CMAP-0019: value and type probes of a verified constant. */
            if (!r_c_abi_spell_constant_value(context, symbol, &spell) ||
                !r_c_abi_spell_type(context,
                                    r_c_abi_value_type(context, symbol->type),
                                    "",
                                    &declarator,
                                    UINT32_C(0),
                                    &header_speller)) {
                status = R_FRONTEND_NOT_LOWERABLE;
                goto cleanup;
            }
            if (!r_write_text(writer, user_data, "_Static_assert((") ||
                !r_c_abi_write_c_name(context, symbol, writer, user_data) ||
                !r_write_text(writer, user_data, ") == ") ||
                !writer(user_data, spell.bytes, spell.length) ||
                !r_write_text(writer, user_data, ", \"R-FFI-0018 value: ") ||
                !r_c_abi_write_c_name(context, symbol, writer, user_data) ||
                !r_write_text(writer, user_data, "\");\n_Static_assert(_Generic((") ||
                !r_c_abi_write_c_name(context, symbol, writer, user_data) ||
                !r_write_text(writer, user_data, "), ") ||
                !writer(user_data, declarator.bytes, declarator.length) ||
                !r_write_text(writer, user_data, ": 1, default: 0), \"R-FFI-0018 type: ") ||
                !r_c_abi_write_c_name(context, symbol, writer, user_data) ||
                !r_write_text(writer, user_data, "\");\n")) {
                goto cleanup;
            }
            continue;
        }
        if (symbol->kind == R_SEMANTIC_SYMBOL_MODULE_OBJECT) {
            /* R-FFI-0022: the object's address shall have exactly the declared pointer type. */
            if (!r_c_abi_spell_type(
                    context, symbol->type, "*", &spell, UINT32_C(0), &header_speller)) {
                status = R_FRONTEND_NOT_LOWERABLE;
                goto cleanup;
            }
            if (!r_write_text(writer, user_data, "_Static_assert(_Generic(&(") ||
                !r_c_abi_write_c_name(context, symbol, writer, user_data) ||
                !r_write_text(writer, user_data, "), ") ||
                !writer(user_data, spell.bytes, spell.length) ||
                !r_write_text(writer, user_data, ": 1, default: 0), \"R-FFI-0022 object: ") ||
                !r_c_abi_write_c_name(context, symbol, writer, user_data) ||
                !r_write_text(writer, user_data, "\");\n")) {
                goto cleanup;
            }
            continue;
        }
        (void)snprintf(object_name, sizeof(object_name), "const r_verify_%08zu", index + 1U);
        if (!r_c_abi_spell_append(&declarator, "(*") ||
            !r_c_abi_spell_append(&declarator, object_name) ||
            !r_c_abi_spell_append(&declarator, ")(")) {
            status = R_FRONTEND_NOT_LOWERABLE;
            goto cleanup;
        }
        {
            /* Reuse the parameter speller over the symbol's committed parameter list. */
            uint32_t parameter_index;

            if (symbol->parameter_count == UINT32_C(0) &&
                !r_c_abi_spell_append(&declarator, "void")) {
                status = R_FRONTEND_NOT_LOWERABLE;
                goto cleanup;
            }
            for (parameter_index = UINT32_C(0); parameter_index < symbol->parameter_count;
                 ++parameter_index) {
                const size_t position =
                    (size_t)symbol->first_parameter_type + (size_t)parameter_index;

                if ((position >= context->semantic_parameter_type_count) ||
                    ((parameter_index != UINT32_C(0)) &&
                     !r_c_abi_spell_append(&declarator, ", ")) ||
                    !r_c_abi_spell_type(context,
                                        context->semantic_parameter_types[position],
                                        "",
                                        &declarator,
                                        UINT32_C(0),
                                        &header_speller)) {
                    status = R_FRONTEND_NOT_LOWERABLE;
                    goto cleanup;
                }
            }
            if (symbol->is_variadic && !r_c_abi_spell_append(&declarator, ", ...")) {
                status = R_FRONTEND_NOT_LOWERABLE;
                goto cleanup;
            }
        }
        if (!r_c_abi_spell_append(&declarator, ")") || !r_c_abi_spell_type(context,
                                                                           symbol->return_type,
                                                                           declarator.bytes,
                                                                           &spell,
                                                                           UINT32_C(0),
                                                                           &header_speller)) {
            status = R_FRONTEND_NOT_LOWERABLE;
            goto cleanup;
        }
        if (!writer(user_data, spell.bytes, spell.length) ||
            !r_write_text(writer, user_data, " = ") ||
            !r_c_abi_write_c_name(context, symbol, writer, user_data) ||
            !r_write_text(writer, user_data, ";\n")) {
            goto cleanup;
        }
    }
    if (!r_c_abi_write_aggregate_probes(context, writer, user_data)) {
        status = R_FRONTEND_NOT_LOWERABLE;
        goto cleanup;
    }
    status = R_FRONTEND_OK;

cleanup:
    r_c_abi_spell_dispose(&declarator);
    r_c_abi_spell_dispose(&spell);
    r_context_free((RFrontendContext *)context, imports);
    return r_c_abi_resource_status(context, status);
}

static bool r_c_abi_type_is_function_pointer(const RFrontendContext *context, RTypeId type_id) {
    const RSemanticType *type = r_c_abi_type(context, r_c_abi_value_type(context, type_id));

    return (type != NULL) && (type->kind == R_SEMANTIC_TYPE_RAW_FUNCTION);
}

/* A by-value struct the bridge copies into or out of its header type: a private complete
   struct (R-CMAP-0018) or an R-declared struct (R-FFI-0021). */
static bool r_c_abi_type_is_copied_value(const RFrontendContext *context, RTypeId type_id) {
    return r_c_abi_type_is_private_value(context, type_id) ||
           r_c_abi_type_is_r_struct_value(context, type_id);
}

/* Whether any bridged import passes a private complete struct or an R-declared struct by
   value. */
static bool r_c_abi_bridge_needs_memcpy(const RFrontendContext *context,
                                        const RSymbolId *imports,
                                        size_t import_count) {
    size_t index;

    for (index = 0U; index < import_count; ++index) {
        const RSemanticSymbol *symbol = &context->semantic_symbols[imports[index] - 1U];
        uint32_t parameter_index;

        if ((symbol->kind != R_SEMANTIC_SYMBOL_FUNCTION) || !symbol->uses_bridge) {
            continue;
        }
        if (r_c_abi_type_is_copied_value(context, symbol->return_type)) {
            return true;
        }
        for (parameter_index = UINT32_C(0); parameter_index < symbol->parameter_count;
             ++parameter_index) {
            const size_t position = (size_t)symbol->first_parameter_type + (size_t)parameter_index;

            if ((position < context->semantic_parameter_type_count) &&
                r_c_abi_type_is_copied_value(context,
                                             context->semantic_parameter_types[position])) {
                return true;
            }
        }
    }
    return false;
}

/* Whether a bridged signature type names the given R-declared struct outside a struct. */
static bool r_c_abi_type_names_aggregate(const RFrontendContext *context,
                                         RTypeId type_id,
                                         uint32_t aggregate_id,
                                         uint32_t depth) {
    const RSemanticType *type = r_c_abi_type(context, type_id);

    if ((type == NULL) || (depth > context->options.limits.max_nesting)) {
        return false;
    }
    if ((type->kind == R_SEMANTIC_TYPE_CONST) || (type->kind == R_SEMANTIC_TYPE_RAW)) {
        return r_c_abi_type_names_aggregate(context, type->base, aggregate_id, depth + 1U);
    }
    if (type->kind == R_SEMANTIC_TYPE_RAW_FUNCTION) {
        RTypeId parameter = type->second;
        uint64_t index;

        if (r_c_abi_type_names_aggregate(context, type->base, aggregate_id, depth + 1U)) {
            return true;
        }
        for (index = UINT64_C(0); index < type->length; ++index) {
            const RSemanticType *item = r_c_abi_type(context, parameter);

            if ((item == NULL) || (item->kind != R_SEMANTIC_TYPE_FUNCTION_PARAMETER)) {
                return false;
            }
            if (r_c_abi_type_names_aggregate(context, item->base, aggregate_id, depth + 1U)) {
                return true;
            }
            parameter = item->second;
        }
        return false;
    }
    return (type->kind == R_SEMANTIC_TYPE_STRUCT) && ((uint32_t)type->base == aggregate_id);
}

/* The file-scope incomplete tag of every R-declared struct a bridged signature names; the
   bridge never defines one, it only copies or forwards its storage (R-FFI-0021). */
static bool r_c_abi_write_r_struct_tags(const RFrontendContext *context,
                                        const RSymbolId *imports,
                                        size_t import_count,
                                        RFrontendWriteFn writer,
                                        void *user_data) {
    size_t aggregate_index;

    for (aggregate_index = 0U; aggregate_index < context->semantic_aggregate_count;
         ++aggregate_index) {
        const RSemanticAggregate *aggregate = &context->semantic_aggregates[aggregate_index];
        const uint32_t aggregate_id = (uint32_t)(aggregate_index + 1U);
        bool named = false;
        size_t index;
        char tag[48];

        if (aggregate->is_opaque || aggregate->is_c_declared || !aggregate->is_repr_c ||
            (aggregate->kind != R_SEMANTIC_AGGREGATE_STRUCT)) {
            continue;
        }
        for (index = 0U; !named && (index < import_count); ++index) {
            const RSemanticSymbol *symbol = &context->semantic_symbols[imports[index] - 1U];
            uint32_t parameter_index;

            if (!symbol->passes_r_struct) {
                continue;
            }
            if (symbol->kind == R_SEMANTIC_SYMBOL_MODULE_OBJECT) {
                named =
                    r_c_abi_type_names_aggregate(context, symbol->type, aggregate_id, UINT32_C(0));
                continue;
            }
            named = r_c_abi_type_names_aggregate(
                context, symbol->return_type, aggregate_id, UINT32_C(0));
            for (parameter_index = UINT32_C(0);
                 !named && (parameter_index < symbol->parameter_count);
                 ++parameter_index) {
                named = r_c_abi_type_names_aggregate(
                    context,
                    context->semantic_parameter_types[(size_t)symbol->first_parameter_type +
                                                      (size_t)parameter_index],
                    aggregate_id,
                    UINT32_C(0));
            }
        }
        if (!named) {
            continue;
        }
        (void)snprintf(tag, sizeof(tag), "struct r_a%08" PRIu32 ";\n", aggregate_id);
        if (!r_write_text(writer, user_data, tag)) {
            return false;
        }
    }
    return true;
}

/* The bridge declarator in private spelling. An R-declared struct passed by value travels by
   address: a parameter as a const pointer, the result through a leading out-pointer. */
static bool r_c_abi_spell_bridge_signature_with(const RFrontendContext *context,
                                                const RSemanticSymbol *symbol,
                                                const char *name,
                                                bool with_names,
                                                RCAbiSpell *spell,
                                                RCAbiSpell *declarator) {
    RCAbiSpeller private_speller = r_c_abi_plain_speller(R_C_ABI_SPELL_PRIVATE);
    const bool out_result = r_c_abi_type_is_r_struct_value(context, symbol->return_type);
    uint32_t index;

    if (!r_c_abi_spell_append(declarator, name) || !r_c_abi_spell_append(declarator, "(")) {
        return false;
    }
    if (out_result && !r_c_abi_spell_type(context,
                                          r_c_abi_value_type(context, symbol->return_type),
                                          with_names ? "*r_result" : "*",
                                          declarator,
                                          UINT32_C(0),
                                          &private_speller)) {
        return false;
    }
    if ((symbol->parameter_count == UINT32_C(0)) && !out_result &&
        !r_c_abi_spell_append(declarator, "void")) {
        return false;
    }
    for (index = UINT32_C(0); index < symbol->parameter_count; ++index) {
        const size_t position = (size_t)symbol->first_parameter_type + (size_t)index;
        char parameter_name[24];
        RTypeId parameter_type;
        bool by_address;

        if (position >= context->semantic_parameter_type_count) {
            return false;
        }
        parameter_type = context->semantic_parameter_types[position];
        by_address = r_c_abi_type_is_r_struct_value(context, parameter_type);
        (void)snprintf(
            parameter_name, sizeof(parameter_name), by_address ? "*a%" PRIu32 : "a%" PRIu32, index);
        if (!with_names) {
            parameter_name[by_address ? 1U : 0U] = '\0';
        }
        if ((((index != UINT32_C(0)) || out_result) && !r_c_abi_spell_append(declarator, ", ")) ||
            (by_address && !r_c_abi_spell_append(declarator, "const ")) ||
            !r_c_abi_spell_type(context,
                                by_address ? r_c_abi_value_type(context, parameter_type)
                                           : parameter_type,
                                parameter_name,
                                declarator,
                                UINT32_C(0),
                                &private_speller)) {
            return false;
        }
    }
    if (!r_c_abi_spell_append(declarator, ")")) {
        return false;
    }
    r_c_abi_spell_clear(spell);
    if (out_result) {
        return r_c_abi_spell_append(spell, "void ") &&
               r_c_abi_spell_append(spell, declarator->bytes);
    }
    return r_c_abi_spell_type(
        context, symbol->return_type, declarator->bytes, spell, UINT32_C(0), &private_speller);
}

static bool r_c_abi_spell_bridge_signature(const RFrontendContext *context,
                                           const RSemanticSymbol *symbol,
                                           const char *name,
                                           bool with_names,
                                           RCAbiSpell *spell) {
    RCAbiSpell declarator;
    bool spelled;

    r_c_abi_spell_init(&declarator, context);
    spelled =
        r_c_abi_spell_bridge_signature_with(context, symbol, name, with_names, spell, &declarator);
    r_c_abi_spell_dispose(&declarator);
    return spelled;
}

/* One forwarding function of the bridge (R-CMAP-0026): copies of by-value structs into their
   header types, casts of header-owned pointers, the call and the converted result. */
/* r_bridge_<identifier>, suffixed with the one-based symbol for an import that passes an
   R-declared struct: two imports of one C symbol may name different R structs. */
static bool
r_c_abi_spell_bridge_name(const RFrontendContext *context, RSymbolId symbol_id, RCAbiSpell *name) {
    const RSemanticSymbol *symbol = &context->semantic_symbols[symbol_id - 1U];
    const char *bytes;
    size_t length;
    char suffix[16];

    r_c_abi_spell_clear(name);
    if (!r_c_abi_intern_text(context, symbol->c_name_intern_id, &bytes, &length) ||
        !r_c_abi_spell_append(name, "r_bridge_") ||
        !r_c_abi_spell_append_bytes(name, bytes, length)) {
        return false;
    }
    if (symbol->passes_r_struct) {
        (void)snprintf(suffix, sizeof(suffix), "_%08" PRIu32, symbol_id);
        return r_c_abi_spell_append(name, suffix);
    }
    return true;
}

static RFrontendStatus r_c_abi_write_bridge_function_with(const RFrontendContext *context,
                                                          RSymbolId symbol_id,
                                                          RFrontendWriteFn writer,
                                                          void *user_data,
                                                          RCAbiSpell *prototype,
                                                          RCAbiSpell *definition,
                                                          RCAbiSpell *name,
                                                          RCAbiSpell *cast) {
    const RSemanticSymbol *symbol = &context->semantic_symbols[symbol_id - 1U];
    const RSemanticType *return_type = r_c_abi_type(context, symbol->return_type);
    const bool returns_private_value = r_c_abi_type_is_private_value(context, symbol->return_type);
    const bool returns_r_struct = r_c_abi_type_is_r_struct_value(context, symbol->return_type);
    uint32_t parameter_index;
    const char *bytes;
    size_t length;

    if ((return_type == NULL) ||
        !r_c_abi_intern_text(context, symbol->c_name_intern_id, &bytes, &length) ||
        !r_c_abi_spell_bridge_name(context, symbol_id, name)) {
        return R_FRONTEND_NOT_LOWERABLE;
    }
    if (!r_c_abi_spell_bridge_signature(context, symbol, name->bytes, false, prototype) ||
        !r_c_abi_spell_bridge_signature(context, symbol, name->bytes, true, definition)) {
        return R_FRONTEND_NOT_LOWERABLE;
    }
    if (!r_write_text(writer, user_data, "\n") ||
        !writer(user_data, prototype->bytes, prototype->length) ||
        !r_write_text(writer, user_data, ";\n") ||
        !writer(user_data, definition->bytes, definition->length) ||
        !r_write_text(writer, user_data, " {\n")) {
        return R_FRONTEND_IO_ERROR;
    }
    /* By-value structs travel through header-typed copies (R-CMAP-0026, R-FFI-0021). */
    for (parameter_index = UINT32_C(0); parameter_index < symbol->parameter_count;
         ++parameter_index) {
        const size_t position = (size_t)symbol->first_parameter_type + (size_t)parameter_index;
        RCAbiSpeller header_speller = r_c_abi_import_speller(
            context, symbol, R_C_ABI_SPELL_HEADER, false, parameter_index + UINT32_C(1));
        char copy_name[24];

        if ((position >= context->semantic_parameter_type_count) ||
            !r_c_abi_type_is_copied_value(context, context->semantic_parameter_types[position])) {
            continue;
        }
        (void)snprintf(copy_name, sizeof(copy_name), "c%" PRIu32, parameter_index);
        r_c_abi_spell_clear(cast);
        if (!r_c_abi_spell_type(
                context,
                r_c_abi_value_type(context, context->semantic_parameter_types[position]),
                copy_name,
                cast,
                UINT32_C(0),
                &header_speller)) {
            return R_FRONTEND_NOT_LOWERABLE;
        }
        if (!r_write_text(writer, user_data, "    ") ||
            !writer(user_data, cast->bytes, cast->length) ||
            !r_write_text(writer, user_data, ";\n")) {
            return R_FRONTEND_IO_ERROR;
        }
    }
    if (returns_private_value || returns_r_struct) {
        RCAbiSpeller header_speller =
            r_c_abi_import_speller(context, symbol, R_C_ABI_SPELL_HEADER, false, UINT32_C(0));

        r_c_abi_spell_clear(cast);
        if (!r_c_abi_spell_type(
                context, symbol->return_type, "result", cast, UINT32_C(0), &header_speller)) {
            return R_FRONTEND_NOT_LOWERABLE;
        }
        if (!r_write_text(writer, user_data, "    ") ||
            !writer(user_data, cast->bytes, cast->length) ||
            !r_write_text(writer, user_data, ";\n")) {
            return R_FRONTEND_IO_ERROR;
        }
    }
    if (returns_private_value) {
        RCAbiSpeller private_speller = r_c_abi_plain_speller(R_C_ABI_SPELL_PRIVATE);

        r_c_abi_spell_clear(cast);
        if (!r_c_abi_spell_type(
                context, symbol->return_type, "converted", cast, UINT32_C(0), &private_speller)) {
            return R_FRONTEND_NOT_LOWERABLE;
        }
        if (!r_write_text(writer, user_data, "    ") ||
            !writer(user_data, cast->bytes, cast->length) ||
            !r_write_text(writer, user_data, ";\n")) {
            return R_FRONTEND_IO_ERROR;
        }
    }
    for (parameter_index = UINT32_C(0); parameter_index < symbol->parameter_count;
         ++parameter_index) {
        const size_t position = (size_t)symbol->first_parameter_type + (size_t)parameter_index;
        const RTypeId parameter_type = context->semantic_parameter_types[position];
        char line[96];

        if (!r_c_abi_type_is_copied_value(context, parameter_type)) {
            continue;
        }
        (void)snprintf(line,
                       sizeof(line),
                       r_c_abi_type_is_r_struct_value(context, parameter_type)
                           ? "    memcpy(&c%" PRIu32 ", a%" PRIu32 ", sizeof(c%" PRIu32 "));\n"
                           : "    memcpy(&c%" PRIu32 ", &a%" PRIu32 ", sizeof(c%" PRIu32 "));\n",
                       parameter_index,
                       parameter_index,
                       parameter_index);
        if (!r_write_text(writer, user_data, line)) {
            return R_FRONTEND_IO_ERROR;
        }
    }
    if (!r_write_text(writer, user_data, "    ") ||
        ((returns_private_value || returns_r_struct) &&
         !r_write_text(writer, user_data, "result = ")) ||
        (!returns_private_value && !returns_r_struct &&
         (return_type->kind != R_SEMANTIC_TYPE_VOID) &&
         !r_write_text(writer, user_data, "return "))) {
        return R_FRONTEND_IO_ERROR;
    }
    /* A header-owned pointer result converts back to the main unit's private spelling. */
    if (!returns_private_value && !returns_r_struct &&
        (r_c_abi_type_mentions_private_opaque(context, symbol->return_type, UINT32_C(0)) ||
         r_c_abi_type_mentions_r_struct(context, symbol->return_type, UINT32_C(0)))) {
        RCAbiSpeller private_speller = r_c_abi_plain_speller(R_C_ABI_SPELL_PRIVATE);

        r_c_abi_spell_clear(cast);
        if (!r_c_abi_spell_type(
                context, symbol->return_type, "", cast, UINT32_C(0), &private_speller)) {
            return R_FRONTEND_NOT_LOWERABLE;
        }
        if (!r_write_text(writer, user_data, "(") ||
            !writer(user_data, cast->bytes, cast->length) ||
            !r_write_text(writer, user_data, ")") ||
            (r_c_abi_type_is_function_pointer(context, symbol->return_type) &&
             !r_write_text(writer, user_data, "(void (*)(void))"))) {
            return R_FRONTEND_IO_ERROR;
        }
    }
    if (!writer(user_data, bytes, length) || !r_write_text(writer, user_data, "(")) {
        return R_FRONTEND_IO_ERROR;
    }
    for (parameter_index = UINT32_C(0); parameter_index < symbol->parameter_count;
         ++parameter_index) {
        const size_t position = (size_t)symbol->first_parameter_type + (size_t)parameter_index;
        const RTypeId parameter_type = context->semantic_parameter_types[position];
        char argument[24];

        (void)snprintf(argument,
                       sizeof(argument),
                       r_c_abi_type_is_copied_value(context, parameter_type) ? "c%" PRIu32
                                                                             : "a%" PRIu32,
                       parameter_index);
        if ((parameter_index != UINT32_C(0)) && !r_write_text(writer, user_data, ", ")) {
            return R_FRONTEND_IO_ERROR;
        }
        /* A header-owned pointer or function pointer converts to the header's own spelling. */
        if (!r_c_abi_type_is_copied_value(context, parameter_type) &&
            (r_c_abi_type_mentions_private_opaque(context, parameter_type, UINT32_C(0)) ||
             r_c_abi_type_mentions_r_struct(context, parameter_type, UINT32_C(0)))) {
            RCAbiSpeller header_speller = r_c_abi_import_speller(
                context, symbol, R_C_ABI_SPELL_HEADER, false, parameter_index + UINT32_C(1));

            r_c_abi_spell_clear(cast);
            if (!r_c_abi_spell_type(
                    context, parameter_type, "", cast, UINT32_C(0), &header_speller)) {
                return R_FRONTEND_NOT_LOWERABLE;
            }
            /* A function pointer converts through the generic function pointer type: the
               header's prototype names the C struct where R names its own (R-FFI-0012). */
            if (!r_write_text(writer, user_data, "(") ||
                !writer(user_data, cast->bytes, cast->length) ||
                !r_write_text(writer, user_data, ")") ||
                (r_c_abi_type_is_function_pointer(context, parameter_type) &&
                 !r_write_text(writer, user_data, "(void (*)(void))"))) {
                return R_FRONTEND_IO_ERROR;
            }
        }
        if (!r_write_text(writer, user_data, argument)) {
            return R_FRONTEND_IO_ERROR;
        }
    }
    if (!r_write_text(writer, user_data, ");\n") ||
        (returns_private_value &&
         !r_write_text(writer,
                       user_data,
                       "    memcpy(&converted, &result, sizeof(converted));\n"
                       "    return converted;\n")) ||
        (returns_r_struct &&
         !r_write_text(writer, user_data, "    memcpy(r_result, &result, sizeof(result));\n")) ||
        !r_write_text(writer, user_data, "}\n")) {
        return R_FRONTEND_IO_ERROR;
    }
    return R_FRONTEND_OK;
}

static RFrontendStatus r_c_abi_write_bridge_function(const RFrontendContext *context,
                                                     RSymbolId symbol_id,
                                                     RFrontendWriteFn writer,
                                                     void *user_data) {
    RCAbiSpell prototype;
    RCAbiSpell definition;
    RCAbiSpell name;
    RCAbiSpell cast;
    RFrontendStatus status;

    r_c_abi_spell_init(&prototype, context);
    r_c_abi_spell_init(&definition, context);
    r_c_abi_spell_init(&name, context);
    r_c_abi_spell_init(&cast, context);
    status = r_c_abi_write_bridge_function_with(
        context, symbol_id, writer, user_data, &prototype, &definition, &name, &cast);
    r_c_abi_spell_dispose(&cast);
    r_c_abi_spell_dispose(&name);
    r_c_abi_spell_dispose(&definition);
    r_c_abi_spell_dispose(&prototype);
    return status;
}

/* The private definition of a typedef/reserved complete struct, dependencies first; it lists
   the verified members in the main unit's private spelling (R-CMAP-0018). */
static bool r_c_abi_write_private_definition(const RFrontendContext *context,
                                             uint32_t aggregate_id,
                                             unsigned char *defined,
                                             RFrontendWriteFn writer,
                                             void *user_data);

static bool r_c_abi_write_private_members(const RFrontendContext *context,
                                          const RSemanticAggregate *aggregate,
                                          RFrontendWriteFn writer,
                                          void *user_data,
                                          RCAbiSpell *member,
                                          RCAbiSpell *spelling) {
    RCAbiSpeller private_speller = r_c_abi_plain_speller(R_C_ABI_SPELL_PRIVATE);

    for (uint32_t field_index = UINT32_C(0); field_index < aggregate->field_count; ++field_index) {
        const RSemanticField *field =
            &context->semantic_fields[(size_t)aggregate->first_field + (size_t)field_index];
        const char *name;
        size_t name_length;

        r_c_abi_spell_clear(member);
        r_c_abi_spell_clear(spelling);
        if (!r_c_abi_intern_text(context, field->name_intern_id, &name, &name_length) ||
            !r_c_abi_spell_append_bytes(member, name, name_length) ||
            !r_c_abi_spell_type(
                context, field->type, member->bytes, spelling, UINT32_C(0), &private_speller) ||
            !r_write_text(writer, user_data, "    ") ||
            !writer(user_data, spelling->bytes, spelling->length) ||
            !r_write_text(writer, user_data, ";\n")) {
            return false;
        }
    }
    return true;
}

static bool r_c_abi_write_private_definition(const RFrontendContext *context,
                                             uint32_t aggregate_id,
                                             unsigned char *defined,
                                             RFrontendWriteFn writer,
                                             void *user_data) {
    const RSemanticAggregate *aggregate;
    uint32_t field_index;
    char tag[48];
    RCAbiSpell member;
    RCAbiSpell spelling;
    bool written;

    if ((aggregate_id == UINT32_C(0)) ||
        ((size_t)aggregate_id > context->semantic_aggregate_count)) {
        return false;
    }
    aggregate = &context->semantic_aggregates[(size_t)aggregate_id - 1U];
    if (!aggregate->is_c_declared || !aggregate->c_name_private || aggregate->poisoned ||
        (aggregate->kind != R_SEMANTIC_AGGREGATE_STRUCT) || (defined[aggregate_id] != 0U)) {
        return true;
    }
    defined[aggregate_id] = 1U;
    for (field_index = UINT32_C(0); field_index < aggregate->field_count; ++field_index) {
        const size_t position = (size_t)aggregate->first_field + (size_t)field_index;
        const RSemanticType *field_type;

        if (position >= context->semantic_field_count) {
            return false;
        }
        field_type = r_c_abi_type(
            context, r_c_abi_value_type(context, context->semantic_fields[position].type));
        if ((field_type != NULL) && (field_type->kind == R_SEMANTIC_TYPE_STRUCT) &&
            !r_c_abi_write_private_definition(
                context, (uint32_t)field_type->base, defined, writer, user_data)) {
            return false;
        }
    }
    (void)snprintf(tag, sizeof(tag), "struct r_a%08" PRIu32 " {\n", aggregate_id);
    if (!r_write_text(writer, user_data, tag)) {
        return false;
    }
    r_c_abi_spell_init(&member, context);
    r_c_abi_spell_init(&spelling, context);
    written =
        r_c_abi_write_private_members(context, aggregate, writer, user_data, &member, &spelling);
    r_c_abi_spell_dispose(&spelling);
    r_c_abi_spell_dispose(&member);
    return written && r_write_text(writer, user_data, "};\n");
}

/* The accessor of an imported object whose spelling is header-owned (R-FFI-0057) or whose
   type names an R-declared struct (R-FFI-0021): it returns the object's own address, so the
   main unit reaches the actual object and no storage is mirrored (R-CMAP-0021, R-CMAP-0026). */
static RFrontendStatus r_c_abi_write_object_accessor_with(const RFrontendContext *context,
                                                          RSymbolId symbol_id,
                                                          RFrontendWriteFn writer,
                                                          void *user_data,
                                                          RCAbiSpell *name,
                                                          RCAbiSpell *declarator,
                                                          RCAbiSpell *prototype,
                                                          RCAbiSpell *cast) {
    const RSemanticSymbol *symbol = &context->semantic_symbols[symbol_id - 1U];
    RCAbiSpeller private_speller = r_c_abi_plain_speller(R_C_ABI_SPELL_PRIVATE);
    const char *bytes;
    size_t length;

    if (!r_c_abi_intern_text(context, symbol->c_name_intern_id, &bytes, &length) ||
        !r_c_abi_spell_bridge_name(context, symbol_id, name) ||
        !r_c_abi_spell_append(declarator, "*") || !r_c_abi_spell_append(declarator, name->bytes) ||
        !r_c_abi_spell_append(declarator, "(void)") ||
        !r_c_abi_spell_type(
            context, symbol->type, declarator->bytes, prototype, UINT32_C(0), &private_speller) ||
        !r_c_abi_spell_type(context, symbol->type, "*", cast, UINT32_C(0), &private_speller)) {
        return R_FRONTEND_NOT_LOWERABLE;
    }
    if (!r_write_text(writer, user_data, "\n") ||
        !writer(user_data, prototype->bytes, prototype->length) ||
        !r_write_text(writer, user_data, ";\n") ||
        !writer(user_data, prototype->bytes, prototype->length) ||
        !r_write_text(writer, user_data, " {\n    return ")) {
        return R_FRONTEND_IO_ERROR;
    }
    if ((r_c_abi_type_mentions_private_opaque(context, symbol->type, UINT32_C(0)) ||
         r_c_abi_type_mentions_r_struct(context, symbol->type, UINT32_C(0))) &&
        (!r_write_text(writer, user_data, "(") || !writer(user_data, cast->bytes, cast->length) ||
         !r_write_text(writer, user_data, ")"))) {
        return R_FRONTEND_IO_ERROR;
    }
    if (!r_write_text(writer, user_data, "&") || !writer(user_data, bytes, length) ||
        !r_write_text(writer, user_data, ";\n}\n")) {
        return R_FRONTEND_IO_ERROR;
    }
    return R_FRONTEND_OK;
}

static RFrontendStatus r_c_abi_write_object_accessor(const RFrontendContext *context,
                                                     RSymbolId symbol_id,
                                                     RFrontendWriteFn writer,
                                                     void *user_data) {
    RCAbiSpell name;
    RCAbiSpell declarator;
    RCAbiSpell prototype;
    RCAbiSpell cast;
    RFrontendStatus status;

    r_c_abi_spell_init(&name, context);
    r_c_abi_spell_init(&declarator, context);
    r_c_abi_spell_init(&prototype, context);
    r_c_abi_spell_init(&cast, context);
    status = r_c_abi_write_object_accessor_with(
        context, symbol_id, writer, user_data, &name, &declarator, &prototype, &cast);
    r_c_abi_spell_dispose(&cast);
    r_c_abi_spell_dispose(&prototype);
    r_c_abi_spell_dispose(&declarator);
    r_c_abi_spell_dispose(&name);
    return status;
}

RFrontendStatus r_frontend_emit_c17_bridge(const RFrontendContext *context,
                                           const RFrontendArtifactOptions *options,
                                           RFrontendWriteFn writer,
                                           void *user_data) {
    RSymbolId *imports = NULL;
    size_t import_count = 0U;
    size_t index;
    size_t bridged = 0U;
    bool any_header;
    RFrontendStatus status = r_c_abi_prepare(context, writer, &imports, &import_count);

    (void)options;
    if (status != R_FRONTEND_OK) {
        return status;
    }
    status = R_FRONTEND_IO_ERROR;
    for (index = 0U; index < import_count; ++index) {
        const RSemanticSymbol *symbol = &context->semantic_symbols[imports[index] - 1U];

        if (((symbol->kind == R_SEMANTIC_SYMBOL_FUNCTION) ||
             (symbol->kind == R_SEMANTIC_SYMBOL_MODULE_OBJECT)) &&
            symbol->uses_bridge) {
            bridged += 1U;
        }
    }
    if (!r_write_text(writer,
                      user_data,
                      "/* Generated by the R 0.1 reference frontend: extern \"C\" bridge. */\n")) {
        goto cleanup;
    }
    if (bridged == 0U) {
        status = r_write_text(writer, user_data, "typedef int r_bridge_empty_translation_unit;\n")
                     ? R_FRONTEND_OK
                     : R_FRONTEND_IO_ERROR;
        goto cleanup;
    }
    if (!r_c_abi_write_definitions(context, imports, import_count, writer, user_data) ||
        !r_c_abi_write_headers(
            context, (RFrontendContext *)context, writer, user_data, &any_header)) {
        goto cleanup;
    }
    /* Private tags of typedef/reserved opaque types and private definitions of typedef/reserved
       complete structs: the bridge signature matches the main unit one-for-one. */
    if (r_c_abi_bridge_needs_memcpy(context, imports, import_count) &&
        !r_write_text(writer, user_data, "#include <string.h>\n")) {
        goto cleanup;
    }
    for (index = 0U; index < context->semantic_aggregate_count; ++index) {
        const RSemanticAggregate *aggregate = &context->semantic_aggregates[index];
        char private_tag[48];

        if (!aggregate->is_opaque || !aggregate->c_name_private || aggregate->poisoned) {
            continue;
        }
        (void)snprintf(
            private_tag, sizeof(private_tag), "struct r_a%08" PRIu32 ";\n", (uint32_t)(index + 1U));
        if (!r_write_text(writer, user_data, private_tag)) {
            goto cleanup;
        }
    }
    if (!r_c_abi_write_r_struct_tags(context, imports, import_count, writer, user_data)) {
        goto cleanup;
    }
    {
        unsigned char *defined =
            r_context_allocate((RFrontendContext *)context, context->semantic_aggregate_count + 1U);

        if (defined == NULL) {
            status = R_FRONTEND_OUT_OF_MEMORY;
            goto cleanup;
        }
        (void)memset(defined, 0, context->semantic_aggregate_count + 1U);
        for (index = 0U; index < context->semantic_aggregate_count; ++index) {
            if (!r_c_abi_write_private_definition(
                    context, (uint32_t)(index + 1U), defined, writer, user_data)) {
                r_context_free((RFrontendContext *)context, defined);
                status = R_FRONTEND_NOT_LOWERABLE;
                goto cleanup;
            }
        }
        r_context_free((RFrontendContext *)context, defined);
    }
    for (index = 0U; index < import_count; ++index) {
        const RSemanticSymbol *symbol = &context->semantic_symbols[imports[index] - 1U];
        bool duplicate = false;
        size_t earlier;

        if (((symbol->kind != R_SEMANTIC_SYMBOL_FUNCTION) &&
             (symbol->kind != R_SEMANTIC_SYMBOL_MODULE_OBJECT)) ||
            !symbol->uses_bridge) {
            continue;
        }
        /* One bridge per C identifier; a repeated import of the same symbol shares it, unless
           it passes R-declared structs and has a bridge of its own. */
        for (earlier = 0U; !symbol->passes_r_struct && (earlier < index); ++earlier) {
            const RSemanticSymbol *other = &context->semantic_symbols[imports[earlier] - 1U];

            if (other->uses_bridge && !other->passes_r_struct &&
                (other->c_name_intern_id == symbol->c_name_intern_id)) {
                duplicate = true;
            }
        }
        if (duplicate) {
            continue;
        }
        status = (symbol->kind == R_SEMANTIC_SYMBOL_MODULE_OBJECT)
                     ? r_c_abi_write_object_accessor(context, imports[index], writer, user_data)
                     : r_c_abi_write_bridge_function(context, imports[index], writer, user_data);
        if (status != R_FRONTEND_OK) {
            goto cleanup;
        }
        status = R_FRONTEND_IO_ERROR;
    }
    status = R_FRONTEND_OK;

cleanup:
    r_context_free((RFrontendContext *)context, imports);
    return r_c_abi_resource_status(context, status);
}

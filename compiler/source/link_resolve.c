#include "frontend_internal.h"
#include "link_manifest.h"

#include <string.h>

/*
 * Link resolution for extern "C" imports (R-FFI-0030..0036). Every import selects one manifest
 * provider by its logical @link name or, without @link, the entry flagged implicit_c_runtime.
 * The provider shall be available for the selected target, agree with the source link kind and
 * list an imported function or object in its inventory with the matching symbol kind. A verified
 * constant (R-FFI-0018) selects its provider for header evidence only and links nothing.
 * Nothing here searches the host.
 */

#define R_LINK_RESOLVE_MAX_NAME 255U
#define R_LINK_RESOLVE_MAX_IDENTIFIER 256U

typedef struct RLinkResolveEntry {
    uint8_t name[R_LINK_RESOLVE_MAX_NAME];
    size_t name_length;
    RLinkKind kind;
    bool available;
    bool implicit;
} RLinkResolveEntry;

typedef struct RLinkResolveSymbol {
    size_t entry;
    uint8_t identifier[R_LINK_RESOLVE_MAX_IDENTIFIER];
    size_t identifier_length;
    RLinkSymbolKind kind;
} RLinkResolveSymbol;

typedef struct RLinkResolveDefinition {
    size_t entry;
    char *text;
    size_t text_length;
} RLinkResolveDefinition;

typedef struct RLinkResolveTable {
    RFrontendContext *context;
    RLinkResolveEntry *entries;
    size_t entry_count;
    size_t entry_capacity;
    RLinkResolveSymbol *symbols;
    size_t symbol_count;
    size_t symbol_capacity;
    RLinkResolveDefinition *definitions;
    size_t definition_count;
    size_t definition_capacity;
    bool duplicate_entry;
} RLinkResolveTable;

static bool r_link_resolve_visit_entry(void *user_data, RLinkManifestEntryView view) {
    RLinkResolveTable *table = user_data;
    RLinkResolveEntry *entry;
    size_t index;

    if (view.logical_name_length > R_LINK_RESOLVE_MAX_NAME) {
        return false;
    }
    for (index = 0U; index < table->entry_count; ++index) {
        if ((table->entries[index].name_length == view.logical_name_length) &&
            (memcmp(table->entries[index].name, view.logical_name, view.logical_name_length) ==
             0)) {
            table->duplicate_entry = true;
        }
    }
    if (!r_grow_array(table->context,
                      (void **)&table->entries,
                      &table->entry_capacity,
                      sizeof(*table->entries),
                      table->entry_count + 1U)) {
        return false;
    }
    entry = &table->entries[table->entry_count];
    (void)memset(entry, 0, sizeof(*entry));
    (void)memcpy(entry->name, view.logical_name, view.logical_name_length);
    entry->name_length = view.logical_name_length;
    entry->kind = view.kind;
    entry->available = view.available;
    entry->implicit = view.implicit_c_runtime;
    table->entry_count += 1U;
    return true;
}

static bool r_link_resolve_visit_symbol(void *user_data,
                                        const RLinkManifestEntryView *entry,
                                        RLinkManifestSymbolView view) {
    RLinkResolveTable *table = user_data;
    RLinkResolveSymbol *symbol;

    (void)entry;
    if ((table->entry_count == 0U) || (view.c_identifier_length > R_LINK_RESOLVE_MAX_IDENTIFIER)) {
        return false;
    }
    if (!r_grow_array(table->context,
                      (void **)&table->symbols,
                      &table->symbol_capacity,
                      sizeof(*table->symbols),
                      table->symbol_count + 1U)) {
        return false;
    }
    symbol = &table->symbols[table->symbol_count];
    (void)memset(symbol, 0, sizeof(*symbol));
    symbol->entry = table->entry_count - 1U;
    (void)memcpy(symbol->identifier, view.c_identifier, view.c_identifier_length);
    symbol->identifier_length = view.c_identifier_length;
    symbol->kind = view.kind;
    table->symbol_count += 1U;
    return true;
}

static bool r_link_resolve_visit_definition(void *user_data,
                                            const RLinkManifestEntryView *entry,
                                            const uint8_t *definition,
                                            size_t definition_length) {
    RLinkResolveTable *table = user_data;
    RLinkResolveDefinition *record;
    char *text;

    (void)entry;
    if (table->entry_count == 0U) {
        return false;
    }
    if (!r_grow_array(table->context,
                      (void **)&table->definitions,
                      &table->definition_capacity,
                      sizeof(*table->definitions),
                      table->definition_count + 1U)) {
        return false;
    }
    text = r_context_allocate(table->context, definition_length + 1U);
    if (text == NULL) {
        return false;
    }
    (void)memcpy(text, definition, definition_length);
    text[definition_length] = '\0';
    record = &table->definitions[table->definition_count];
    record->entry = table->entry_count - 1U;
    record->text = text;
    record->text_length = definition_length;
    table->definition_count += 1U;
    return true;
}

static void r_link_resolve_table_dispose(RLinkResolveTable *table) {
    size_t index;

    for (index = 0U; index < table->definition_count; ++index) {
        r_context_free(table->context, table->definitions[index].text);
    }
    r_context_free(table->context, table->definitions);
    r_context_free(table->context, table->entries);
    r_context_free(table->context, table->symbols);
}

/* Records one manifest provider (once) for the emitters and returns its one-based index. */
static bool r_link_resolve_record_provider(RFrontendContext *context,
                                           const RLinkResolveTable *table,
                                           size_t position,
                                           uint32_t *provider_index) {
    const RLinkResolveEntry *entry = &table->entries[position];
    RLinkProviderRecord *record;
    size_t index;

    for (index = 0U; index < context->link_provider_count; ++index) {
        const RLinkProviderRecord *existing = &context->link_providers[index];

        if ((existing->name_length == entry->name_length) &&
            (memcmp(existing->name, entry->name, entry->name_length) == 0)) {
            *provider_index = (uint32_t)(index + 1U);
            return true;
        }
    }
    if ((context->link_provider_count >= (size_t)UINT32_MAX) ||
        !r_grow_array(context,
                      (void **)&context->link_providers,
                      &context->link_provider_capacity,
                      sizeof(*context->link_providers),
                      context->link_provider_count + 1U)) {
        return false;
    }
    record = &context->link_providers[context->link_provider_count];
    (void)memset(record, 0, sizeof(*record));
    record->name = r_context_allocate(context, entry->name_length + 1U);
    if (record->name == NULL) {
        return false;
    }
    (void)memcpy(record->name, entry->name, entry->name_length);
    record->name[entry->name_length] = '\0';
    record->name_length = entry->name_length;
    record->kind = entry->kind;
    record->first_definition = context->link_definition_count;
    for (index = 0U; index < table->definition_count; ++index) {
        const RLinkResolveDefinition *definition = &table->definitions[index];
        RLinkDefinitionRecord *stored;
        size_t name_length = 0U;

        if (definition->entry != position) {
            continue;
        }
        while ((name_length < definition->text_length) && (definition->text[name_length] != '=')) {
            name_length += 1U;
        }
        if (!r_grow_array(context,
                          (void **)&context->link_definitions,
                          &context->link_definition_capacity,
                          sizeof(*context->link_definitions),
                          context->link_definition_count + 1U)) {
            return false;
        }
        stored = &context->link_definitions[context->link_definition_count];
        (void)memset(stored, 0, sizeof(*stored));
        stored->name = r_context_allocate(context, name_length + 1U);
        if (stored->name == NULL) {
            return false;
        }
        (void)memcpy(stored->name, definition->text, name_length);
        stored->name[name_length] = '\0';
        stored->name_length = name_length;
        if (name_length < definition->text_length) {
            const size_t value_length = definition->text_length - name_length - 1U;

            stored->value = r_context_allocate(context, value_length + 1U);
            if (stored->value == NULL) {
                return false;
            }
            (void)memcpy(stored->value, definition->text + name_length + 1U, value_length);
            stored->value[value_length] = '\0';
            stored->value_length = value_length;
        }
        context->link_definition_count += 1U;
        record->definition_count += 1U;
    }
    context->link_provider_count += 1U;
    *provider_index = (uint32_t)context->link_provider_count;
    return true;
}

static bool r_link_resolve_diagnostic(RFrontendContext *context,
                                      const char *code,
                                      const char *rule_id,
                                      const char *message,
                                      RSourceSpan span) {
    return r_add_diagnostic_phase(
        context, R_DIAGNOSTIC_PHASE_SEMANTIC, code, rule_id, message, R_DIAGNOSTIC_ERROR, span);
}

static bool r_link_resolve_intern_text(const RFrontendContext *context,
                                       uint32_t intern_id,
                                       const uint8_t **bytes,
                                       size_t *length) {
    const RInternEntry *entry;

    if ((intern_id == UINT32_C(0)) || ((size_t)intern_id > context->intern_count)) {
        return false;
    }
    entry = &context->intern_entries[(size_t)intern_id - 1U];
    *bytes = (const uint8_t *)entry->bytes;
    *length = entry->length;
    return true;
}

static const RLinkResolveEntry *r_link_resolve_find_entry(const RLinkResolveTable *table,
                                                          const uint8_t *name,
                                                          size_t length,
                                                          size_t *position) {
    size_t index;

    for (index = 0U; index < table->entry_count; ++index) {
        const RLinkResolveEntry *entry = &table->entries[index];

        if ((entry->name_length == length) && (memcmp(entry->name, name, length) == 0)) {
            *position = index;
            return entry;
        }
    }
    return NULL;
}

static bool r_link_resolve_symbol_present(const RLinkResolveTable *table,
                                          size_t entry,
                                          RLinkSymbolKind kind,
                                          const uint8_t *identifier,
                                          size_t length) {
    size_t index;

    for (index = 0U; index < table->symbol_count; ++index) {
        const RLinkResolveSymbol *symbol = &table->symbols[index];

        if ((symbol->entry == entry) && (symbol->kind == kind) &&
            (symbol->identifier_length == length) &&
            (memcmp(symbol->identifier, identifier, length) == 0)) {
            return true;
        }
    }
    return false;
}

/* Resolves one import; returns false only on a resource failure. */
static bool r_link_resolve_import(RFrontendContext *context,
                                  const RLinkResolveTable *table,
                                  RSemanticSymbol *symbol,
                                  size_t implicit_entry,
                                  bool *failed) {
    const RLinkResolveEntry *entry = NULL;
    size_t position = 0U;
    const uint8_t *identifier;
    size_t identifier_length;

    if (symbol->link_name_intern_id != UINT32_C(0)) {
        const uint8_t *name;
        size_t name_length;

        if (!r_link_resolve_intern_text(
                context, symbol->link_name_intern_id, &name, &name_length)) {
            context->resource_status = R_FRONTEND_INTERNAL_ERROR;
            return false;
        }
        entry = r_link_resolve_find_entry(table, name, name_length, &position);
        if (entry == NULL) {
            *failed = true;
            return r_link_resolve_diagnostic(
                context,
                "R-DIAG-LINK-001",
                "R-FFI-0031",
                "logical link name is not declared by the link manifest",
                symbol->name_span);
        }
    } else if (implicit_entry == SIZE_MAX) {
        *failed = true;
        return r_link_resolve_diagnostic(
            context,
            "R-DIAG-LINK-001",
            "R-FFI-0030",
            "extern block without @link requires an implicit C runtime provider in the manifest",
            symbol->name_span);
    } else {
        position = implicit_entry;
        entry = &table->entries[position];
    }
    if (!entry->available) {
        *failed = true;
        return r_link_resolve_diagnostic(context,
                                         "R-DIAG-LINK-001",
                                         "R-FFI-0033",
                                         "link provider artifact is unresolved for the target",
                                         symbol->name_span);
    }
    if ((symbol->link_kind != R_LINK_KIND_UNSPECIFIED) &&
        (entry->kind != R_LINK_KIND_UNSPECIFIED) && (symbol->link_kind != entry->kind)) {
        *failed = true;
        return r_link_resolve_diagnostic(context,
                                         "R-DIAG-LINK-002",
                                         "R-FFI-0034",
                                         "link kind disagrees with the manifest provider",
                                         symbol->name_span);
    }
    if (!r_link_resolve_intern_text(
            context, symbol->c_name_intern_id, &identifier, &identifier_length)) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    if (symbol->kind == R_SEMANTIC_SYMBOL_FUNCTION) {
        if (!r_link_resolve_symbol_present(
                table, position, R_LINK_SYMBOL_FUNCTION, identifier, identifier_length)) {
            *failed = true;
            return r_link_resolve_diagnostic(
                context,
                "R-DIAG-LINK-003",
                "R-FFI-0036",
                "imported C function is not in the provider symbol inventory",
                symbol->name_span);
        }
    } else if (symbol->kind == R_SEMANTIC_SYMBOL_MODULE_OBJECT) {
        const RLinkSymbolKind expected =
            symbol->is_thread_local ? R_LINK_SYMBOL_TLS : R_LINK_SYMBOL_DATA;

        if (!r_link_resolve_symbol_present(
                table, position, expected, identifier, identifier_length)) {
            *failed = true;
            return r_link_resolve_diagnostic(
                context,
                "R-DIAG-LINK-003",
                "R-FFI-0036",
                "imported C object is not in the provider symbol inventory with its storage kind",
                symbol->name_span);
        }
    }
    return r_link_resolve_record_provider(context, table, position, &symbol->link_provider);
}

/* R-FFI-0043: providers used together shall not disagree on one feature-test macro. */
static bool
r_link_resolve_check_definitions(RFrontendContext *context, RSourceSpan span, bool *failed) {
    size_t left;
    size_t right;

    for (left = 0U; left < context->link_definition_count; ++left) {
        const RLinkDefinitionRecord *first = &context->link_definitions[left];

        for (right = left + 1U; right < context->link_definition_count; ++right) {
            const RLinkDefinitionRecord *second = &context->link_definitions[right];

            if ((first->name_length == second->name_length) &&
                (memcmp(first->name, second->name, first->name_length) == 0) &&
                ((first->value_length != second->value_length) ||
                 ((first->value_length != 0U) &&
                  (memcmp(first->value, second->value, first->value_length) != 0)))) {
                *failed = true;
                return r_link_resolve_diagnostic(
                    context,
                    "R-DIAG-FFI-004",
                    "R-FFI-0043",
                    "link providers disagree on a feature-test definition",
                    span);
            }
        }
    }
    return true;
}

bool r_frontend_uses_library_natives(const RFrontendContext *context) {
    if (context == NULL) {
        return false;
    }
    for (size_t index = 0U; index < context->extern_block_count; ++index) {
        const RSource *source = r_get_source_const(context, context->extern_blocks[index].source);
        if ((source != NULL) && source->is_library) {
            return true;
        }
    }
    return false;
}

typedef struct RLinkNameProbe {
    const uint8_t *other;
    size_t other_length;
    const uint8_t *name;
    size_t name_length;
    bool shared;
} RLinkNameProbe;

static bool r_link_name_probe_inner(void *user_data, RLinkManifestEntryView entry) {
    RLinkNameProbe *probe = user_data;
    if ((entry.logical_name_length == probe->name_length) &&
        (memcmp(entry.logical_name, probe->name, probe->name_length) == 0)) {
        probe->shared = true;
    }
    return true;
}

static bool r_link_name_probe_outer(void *user_data, RLinkManifestEntryView entry) {
    RLinkNameProbe *probe = user_data;
    RLinkNameProbe inner = *probe;
    inner.name = entry.logical_name;
    inner.name_length = entry.logical_name_length;
    inner.shared = false;
    if (r_link_manifest_visit(
            probe->other, probe->other_length, r_link_name_probe_inner, &inner, NULL) !=
        R_LINK_MANIFEST_VISIT_OK) {
        return false;
    }
    probe->shared = probe->shared || inner.shared;
    return true;
}

bool r_frontend_link_manifests_share_name(const uint8_t *first,
                                          size_t first_length,
                                          const uint8_t *second,
                                          size_t second_length) {
    RLinkNameProbe probe;
    (void)memset(&probe, 0, sizeof(probe));
    probe.other = second;
    probe.other_length = second_length;
    return (r_link_manifest_visit(first, first_length, r_link_name_probe_outer, &probe, NULL) ==
            R_LINK_MANIFEST_VISIT_OK) &&
           probe.shared;
}

bool r_frontend_link_manifest_links(const uint8_t *manifest,
                                    size_t manifest_length,
                                    size_t *first,
                                    size_t *last) {
    return r_link_manifest_links_span(manifest, manifest_length, first, last);
}

RFrontendStatus r_frontend_resolve_links(RFrontendContext *context,
                                         const uint8_t *link_manifest,
                                         size_t link_manifest_length) {
    RLinkResolveTable table;
    RLinkManifestVisitors visitors;
    RLinkManifestVisitStatus status;
    RFrontendStatus result = R_FRONTEND_OK;
    size_t index;
    size_t implicit_entry = SIZE_MAX;
    size_t implicit_count = 0U;
    bool have_imports = false;
    bool failed = false;
    RSourceSpan first_span;

    if (context == NULL) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    if (!context->semantic_analyzed) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    (void)memset(&first_span, 0, sizeof(first_span));
    for (index = 0U; index < context->semantic_symbol_count; ++index) {
        const RSemanticSymbol *symbol = &context->semantic_symbols[index];

        if (symbol->is_import && ((symbol->kind == R_SEMANTIC_SYMBOL_FUNCTION) ||
                                  (symbol->kind == R_SEMANTIC_SYMBOL_MODULE_OBJECT) ||
                                  (symbol->kind == R_SEMANTIC_SYMBOL_MODULE_CONSTANT))) {
            if (!have_imports) {
                first_span = symbol->name_span;
            }
            have_imports = true;
        }
    }
    for (index = 0U; index < context->extern_block_count; ++index) {
        const RExternBlockRecord *block = &context->extern_blocks[index];

        /* A block naming an ABI record needs its provider's feature-test definitions. */
        if (block->abi_name_intern_id != UINT32_C(0)) {
            if (!have_imports) {
                first_span.source = block->source;
                first_span.start = block->span_start;
                first_span.end = block->span_start;
            }
            have_imports = true;
        }
    }
    if (!have_imports) {
        return R_FRONTEND_OK;
    }
    if ((link_manifest == NULL) || (link_manifest_length == 0U)) {
        if (!r_link_resolve_diagnostic(context,
                                       "R-DIAG-LINK-001",
                                       "R-FFI-0030",
                                       "extern C imports require a link manifest",
                                       first_span)) {
            return context->resource_status != R_FRONTEND_OK ? context->resource_status
                                                             : R_FRONTEND_INTERNAL_ERROR;
        }
        return R_FRONTEND_INVALID_SOURCE;
    }
    (void)memset(&table, 0, sizeof(table));
    table.context = context;
    (void)memset(&visitors, 0, sizeof(visitors));
    visitors.entry = r_link_resolve_visit_entry;
    visitors.symbol = r_link_resolve_visit_symbol;
    visitors.definition = r_link_resolve_visit_definition;
    status =
        r_link_manifest_visit_full(link_manifest, link_manifest_length, &visitors, &table, NULL);
    if ((status != R_LINK_MANIFEST_VISIT_OK) || table.duplicate_entry) {
        result = context->resource_status != R_FRONTEND_OK ? context->resource_status
                                                           : R_FRONTEND_INVALID_ARGUMENT;
        goto cleanup;
    }
    for (index = 0U; index < table.entry_count; ++index) {
        if (table.entries[index].implicit) {
            implicit_entry = index;
            implicit_count += 1U;
        }
    }
    if (implicit_count > 1U) {
        result = R_FRONTEND_INVALID_ARGUMENT;
        goto cleanup;
    }
    for (index = 0U; index < context->semantic_symbol_count; ++index) {
        RSemanticSymbol *symbol = &context->semantic_symbols[index];

        if (!symbol->is_import || symbol->poisoned ||
            ((symbol->kind != R_SEMANTIC_SYMBOL_FUNCTION) &&
             (symbol->kind != R_SEMANTIC_SYMBOL_MODULE_OBJECT) &&
             (symbol->kind != R_SEMANTIC_SYMBOL_MODULE_CONSTANT))) {
            continue;
        }
        if (!r_link_resolve_import(context, &table, symbol, implicit_entry, &failed)) {
            result = context->resource_status != R_FRONTEND_OK ? context->resource_status
                                                               : R_FRONTEND_INTERNAL_ERROR;
            goto cleanup;
        }
    }
    for (index = 0U; index < context->extern_block_count; ++index) {
        RExternBlockRecord *block = &context->extern_blocks[index];
        const RLinkResolveEntry *entry = NULL;
        size_t position = 0U;
        RSourceSpan span;

        if (block->abi_name_intern_id == UINT32_C(0)) {
            continue;
        }
        span.source = block->source;
        span.start = block->span_start;
        span.end = block->span_start;
        if (block->link_name_intern_id != UINT32_C(0)) {
            const uint8_t *name;
            size_t name_length;

            if (!r_link_resolve_intern_text(
                    context, block->link_name_intern_id, &name, &name_length)) {
                result = R_FRONTEND_INTERNAL_ERROR;
                goto cleanup;
            }
            entry = r_link_resolve_find_entry(&table, name, name_length, &position);
        } else if (implicit_entry != SIZE_MAX) {
            position = implicit_entry;
            entry = &table.entries[position];
        }
        if (entry == NULL) {
            failed = true;
            if (!r_link_resolve_diagnostic(context,
                                           "R-DIAG-LINK-001",
                                           "R-FFI-0031",
                                           "logical link name is not declared by the link manifest",
                                           span)) {
                result = context->resource_status != R_FRONTEND_OK ? context->resource_status
                                                                   : R_FRONTEND_INTERNAL_ERROR;
                goto cleanup;
            }
            continue;
        }
        if (!r_link_resolve_record_provider(context, &table, position, &block->link_provider)) {
            result = context->resource_status != R_FRONTEND_OK ? context->resource_status
                                                               : R_FRONTEND_INTERNAL_ERROR;
            goto cleanup;
        }
    }
    if (!failed && !r_link_resolve_check_definitions(context, first_span, &failed)) {
        result = context->resource_status != R_FRONTEND_OK ? context->resource_status
                                                           : R_FRONTEND_INTERNAL_ERROR;
        goto cleanup;
    }
    result = failed ? R_FRONTEND_INVALID_SOURCE : R_FRONTEND_OK;

cleanup:
    r_link_resolve_table_dispose(&table);
    return result;
}

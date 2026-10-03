#include "frontend_internal.h"

#include <stdlib.h>

#define R_DEFAULT_MAX_SOURCE_BYTES (64U * 1024U * 1024U)
#define R_DEFAULT_MAX_NESTING UINT32_C(256)
#define R_DEFAULT_MAX_DIAGNOSTICS UINT32_C(100)

static void *r_default_allocate(void *user_data, size_t size) {
    (void)user_data;
    return malloc(size);
}

static void r_default_free(void *user_data, void *pointer) {
    (void)user_data;
    free(pointer);
}

RFrontendOptions r_frontend_default_options(void) {
    RFrontendOptions options;

    options.limits.max_source_bytes = R_DEFAULT_MAX_SOURCE_BYTES;
    options.limits.max_nesting = R_DEFAULT_MAX_NESTING;
    options.limits.max_diagnostics = R_DEFAULT_MAX_DIAGNOSTICS;
    options.allocate = r_default_allocate;
    options.free = r_default_free;
    options.allocator_user_data = NULL;
    return options;
}

static RFrontendOptions r_normalize_options(const RFrontendOptions *requested) {
    RFrontendOptions options = r_frontend_default_options();

    if (requested == NULL) {
        return options;
    }

    options = *requested;
    if ((options.allocate == NULL) || (options.free == NULL)) {
        options.allocate = r_default_allocate;
        options.free = r_default_free;
        options.allocator_user_data = NULL;
    }
    if (options.limits.max_source_bytes < R_DEFAULT_MAX_SOURCE_BYTES) {
        options.limits.max_source_bytes = R_DEFAULT_MAX_SOURCE_BYTES;
    }
    if (options.limits.max_nesting < R_DEFAULT_MAX_NESTING) {
        options.limits.max_nesting = R_DEFAULT_MAX_NESTING;
    }
    if (options.limits.max_diagnostics < R_DEFAULT_MAX_DIAGNOSTICS) {
        options.limits.max_diagnostics = R_DEFAULT_MAX_DIAGNOSTICS;
    }
    return options;
}

RFrontendContext *r_frontend_create(const RFrontendOptions *requested) {
    RFrontendOptions options = r_normalize_options(requested);
    RFrontendContext *context = options.allocate(options.allocator_user_data, sizeof(*context));

    if (context == NULL) {
        return NULL;
    }
    (void)memset(context, 0, sizeof(*context));
    context->options = options;
    context->resource_status = R_FRONTEND_OK;
    context->profile = R_FRONTEND_PROFILE_HOSTED_NATIVE_ASYNC;
    return context;
}

static bool r_frontend_profile_from_name(const char *profile, RFrontendProfile *value) {
    static const struct {
        const char *name;
        RFrontendProfile value;
    } profiles[] = {
        {"freestanding", R_FRONTEND_PROFILE_FREESTANDING},
        {"allocation", R_FRONTEND_PROFILE_ALLOCATION},
        {"hosted", R_FRONTEND_PROFILE_HOSTED},
        {"hosted-thread", R_FRONTEND_PROFILE_HOSTED_THREAD},
        {"hosted-native-async", R_FRONTEND_PROFILE_HOSTED_NATIVE_ASYNC},
    };
    size_t index;

    for (index = 0U; index < sizeof(profiles) / sizeof(profiles[0]); ++index) {
        if (strcmp(profile, profiles[index].name) == 0) {
            *value = profiles[index].value;
            return true;
        }
    }
    return false;
}

RFrontendStatus r_frontend_set_profile(RFrontendContext *context, const char *profile) {
    RFrontendProfile value;

    if ((context == NULL) || (profile == NULL)) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    if (context->semantic_analyzing || context->semantic_analyzed) {
        return R_FRONTEND_CONTEXT_SEALED;
    }
    if (!r_frontend_profile_from_name(profile, &value)) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    context->profile = value;
    return R_FRONTEND_OK;
}

RFrontendStatus r_frontend_add_library_source(RFrontendContext *context,
                                              const char *display_name,
                                              const uint8_t *bytes,
                                              size_t length,
                                              const char *minimum_profile,
                                              RSourceId *source_id) {
    RFrontendProfile profile;
    RFrontendStatus status;
    RSource *source;

    if ((minimum_profile == NULL) || !r_frontend_profile_from_name(minimum_profile, &profile)) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    status = r_frontend_add_source(context, display_name, bytes, length, source_id);
    if (status != R_FRONTEND_OK) {
        return status;
    }
    source = r_get_source(context, *source_id);
    if (source == NULL) {
        return R_FRONTEND_INTERNAL_ERROR;
    }
    source->is_library = true;
    source->minimum_profile = (uint32_t)profile;
    return R_FRONTEND_OK;
}

RFrontendStatus r_frontend_set_test_mode(RFrontendContext *context, const char *entry_module) {
    if (context == NULL) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    if ((context->source_count != 0U) || context->semantic_analyzing ||
        context->semantic_analyzed) {
        return R_FRONTEND_CONTEXT_SEALED;
    }
    if (entry_module != NULL) {
        char *copy = r_copy_string(context, entry_module, strlen(entry_module));
        if (copy == NULL) {
            return context->resource_status;
        }
        r_context_free(context, context->test_module);
        context->test_module = copy;
    }
    context->test_mode = true;
    return R_FRONTEND_OK;
}

RFrontendStatus r_frontend_set_deny_panic_alloc(RFrontendContext *context, bool deny) {
    if (context == NULL) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    if (context->semantic_analyzing || context->semantic_analyzed) {
        return R_FRONTEND_CONTEXT_SEALED;
    }
    context->deny_panic_alloc = deny;
    return R_FRONTEND_OK;
}

void *r_context_allocate(RFrontendContext *context, size_t size) {
    void *result;

    if ((context == NULL) || (size == 0U)) {
        return NULL;
    }
    result = context->options.allocate(context->options.allocator_user_data, size);
    if (result == NULL) {
        context->resource_status = R_FRONTEND_OUT_OF_MEMORY;
    }
    return result;
}

uint32_t r_frontend_tree_depth_limit(const RFrontendContext *context) {
    const uint32_t nesting = context->options.limits.max_nesting;

    return nesting > (UINT32_MAX / UINT32_C(4)) ? UINT32_MAX : nesting * UINT32_C(4);
}

void r_context_free(RFrontendContext *context, void *pointer) {
    if ((context != NULL) && (pointer != NULL)) {
        context->options.free(context->options.allocator_user_data, pointer);
    }
}

bool r_grow_array(
    RFrontendContext *context, void **items, size_t *capacity, size_t item_size, size_t required) {
    size_t next_capacity;
    size_t byte_count;
    void *replacement;

    if ((context == NULL) || (items == NULL) || (capacity == NULL) || (item_size == 0U)) {
        return false;
    }
    if (required <= *capacity) {
        return true;
    }
    if (required > (SIZE_MAX / item_size)) {
        context->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
        return false;
    }

    next_capacity = (*capacity == 0U) ? R_INITIAL_ARRAY_CAPACITY : *capacity;
    while (next_capacity < required) {
        if (next_capacity > (SIZE_MAX / 2U)) {
            next_capacity = required;
            break;
        }
        next_capacity *= 2U;
    }
    if (next_capacity > (SIZE_MAX / item_size)) {
        context->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
        return false;
    }
    byte_count = next_capacity * item_size;
    replacement = r_context_allocate(context, byte_count);
    if (replacement == NULL) {
        return false;
    }
    if (*items != NULL) {
        (void)memcpy(replacement, *items, (*capacity) * item_size);
        r_context_free(context, *items);
    }
    *items = replacement;
    *capacity = next_capacity;
    return true;
}

char *r_copy_string(RFrontendContext *context, const char *bytes, size_t length) {
    char *result;

    if ((bytes == NULL) && (length != 0U)) {
        return NULL;
    }
    if (length == SIZE_MAX) {
        context->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
        return NULL;
    }
    result = r_context_allocate(context, length + 1U);
    if (result == NULL) {
        return NULL;
    }
    if (length != 0U) {
        (void)memcpy(result, bytes, length);
    }
    result[length] = '\0';
    return result;
}

static void r_destroy_source(RFrontendContext *context, RSource *source) {
    r_context_free(context, source->display_name);
    r_context_free(context, source->bytes);
    r_context_free(context, source->line_starts);
    r_context_free(context, source->tokens);
    r_context_free(context, source->cst_events);
    r_context_free(context, source->ast_nodes);
    r_context_free(context, source->ast_children);
    r_context_free(context, source->module_name);
    r_context_free(context, source->derive_regions);
    (void)memset(source, 0, sizeof(*source));
}

void r_frontend_destroy(RFrontendContext *context) {
    size_t index;

    if (context == NULL) {
        return;
    }
    for (index = 0U; index < context->source_count; ++index) {
        r_destroy_source(context, &context->sources[index]);
    }
    r_context_free(context, context->test_module);
    for (index = 0U; index < context->aggregate_count; ++index) {
        r_context_free(context, context->aggregates[index].module_name);
        r_context_free(context, context->aggregates[index].name);
    }
    for (index = 0U; index < context->import_count; ++index) {
        r_context_free(context, context->imports[index].module_name);
        r_context_free(context, context->imports[index].name);
    }
    for (index = 0U; index < context->intern_count; ++index) {
        r_context_free(context, context->intern_entries[index].bytes);
    }
    for (index = 0U; index < context->link_provider_count; ++index) {
        r_context_free(context, context->link_providers[index].name);
    }
    for (index = 0U; index < context->link_definition_count; ++index) {
        r_context_free(context, context->link_definitions[index].name);
        r_context_free(context, context->link_definitions[index].value);
    }
    for (index = 0U; index < context->abi_record_count; ++index) {
        r_context_free(context, context->abi_records[index].name);
        r_context_free(context, context->abi_records[index].provider);
        r_context_free(context, context->abi_records[index].target_triple);
        r_context_free(context, context->abi_records[index].compiler_identity);
    }
    for (index = 0U; index < context->abi_type_count; ++index) {
        r_context_free(context, context->abi_types[index].c_name);
        r_context_free(context, context->abi_types[index].underlying);
    }
    for (index = 0U; index < context->abi_member_count; ++index) {
        r_context_free(context, context->abi_members[index].name);
        r_context_free(context, context->abi_members[index].type);
        r_context_free(context, context->abi_members[index].desugared_type);
    }
    for (index = 0U; index < context->abi_enumerator_count; ++index) {
        r_context_free(context, context->abi_enumerators[index].name);
    }
    r_context_free(context, context->abi_records);
    r_context_free(context, context->abi_types);
    r_context_free(context, context->abi_members);
    r_context_free(context, context->abi_enumerators);
    for (index = 0U; index < context->abi_header_count; ++index) {
        r_context_free(context, context->abi_headers[index].spelling);
        r_context_free(context, context->abi_headers[index].sha256);
    }
    r_context_free(context, context->abi_headers);
    for (index = 0U; index < context->abi_type_node_count; ++index) {
        r_context_free(context, context->abi_type_nodes[index].spelling);
        r_context_free(context, context->abi_type_nodes[index].tag_name);
        r_context_free(context, context->abi_type_nodes[index].typedef_name);
    }
    r_context_free(context, context->abi_type_nodes);
    r_context_free(context, context->abi_type_children);
    for (index = 0U; index < context->abi_symbol_count; ++index) {
        r_context_free(context, context->abi_symbols[index].c_name);
    }
    r_context_free(context, context->abi_symbols);
    r_context_free(context, context->c_import_slots);
    r_context_free(context, context->c_repr_pairs);
    r_context_free(context, context->extern_blocks);
    r_context_free(context, context->extern_headers);
    r_context_free(context, context->link_providers);
    r_context_free(context, context->link_definitions);
    r_context_free(context, context->consteval_values);
    r_context_free(context, context->json_schema_hooks);
    r_context_free(context, context->consteval_slots);
    r_context_free(context, context->sources);
    r_context_free(context, context->diagnostics);
    r_context_free(context, context->aggregates);
    r_context_free(context, context->imports);
    r_context_free(context, context->intern_entries);
    r_context_free(context, context->semantic_types);
    r_context_free(context, context->semantic_type_index);
    r_context_free(context, context->format_schemas);
    r_context_free(context, context->format_parts);
    r_context_free(context, context->format_recipes);
    r_context_free(context, context->semantic_aggregates);
    r_context_free(context, context->generic_schemas);
    r_context_free(context, context->generic_parameters);
    r_context_free(context, context->static_predicates);
    r_context_free(context, context->static_facts);
    r_context_free(context, context->static_choices);
    r_context_free(context, context->computed_bounds);
    r_context_free(context, context->computed_values);
    r_context_free(context, context->dyn_members);
    r_context_free(context, context->function_value_targets);
    r_context_free(context, context->dyn_targets);
    r_context_free(context, context->semantic_associated_constants);
    r_context_free(context, context->semantic_constant_bindings);
    r_context_free(context, context->generic_pending_constraints);
    r_context_free(context, context->generic_projection_constraints);
    r_context_free(context, context->generic_pending_callables);
    r_context_free(context, context->generic_trait_constraints);
    r_context_free(context, context->semantic_traits);
    r_context_free(context, context->semantic_impls);
    r_context_free(context, context->semantic_associated_types);
    r_context_free(context, context->semantic_associated_bindings);
    r_context_free(context, context->semantic_methods);
    r_context_free(context, context->generic_arguments);
    r_context_free(context, context->semantic_fields);
    r_context_free(context, context->json_fields);
    r_context_free(context, context->semantic_variants);
    r_context_free(context, context->semantic_symbols);
    r_context_free(context, context->error_borrow_mappings);
    r_context_free(context, context->store_borrow_mappings);
    r_context_free(context, context->tuple_origins);
    r_context_free(context, context->borrow_origin_sets);
    r_context_free(context, context->borrow_origin_set_items);
    r_context_free(context, context->projected_borrow_states);
    r_context_free(context, context->projected_borrow_state_entries);
    r_context_free(context, context->semantic_parameter_types);
    r_context_free(context, context->hir_nodes);
    r_context_free(context, context->hir_children);
    r_context_free(context, context->hir_effect_exits);
    r_context_free(context, context->mir_functions);
    r_context_free(context, context->mir_blocks);
    r_context_free(context, context->mir_instructions);
    r_context_free(context, context->mir_operands);
    r_context_free(context, context->mir_operand_members);
    context->options.free(context->options.allocator_user_data, context);
}

static uint64_t r_hash_bytes(const uint8_t *bytes, size_t length) {
    uint64_t hash = UINT64_C(14695981039346656037);
    size_t index;
    for (index = 0U; index < length; ++index) {
        hash ^= (uint64_t)bytes[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

bool r_intern_bytes(RFrontendContext *context,
                    const uint8_t *bytes,
                    size_t length,
                    uint32_t *intern_id) {
    uint64_t hash;
    size_t index;
    RInternEntry *entry;
    if ((context == NULL) || (intern_id == NULL) || ((bytes == NULL) && (length != 0U))) {
        return false;
    }
    hash = r_hash_bytes(bytes, length);
    for (index = 0U; index < context->intern_count; ++index) {
        entry = &context->intern_entries[index];
        if ((entry->hash == hash) && (entry->length == length) &&
            (memcmp(entry->bytes, bytes, length) == 0)) {
            *intern_id = (uint32_t)(index + 1U);
            return true;
        }
    }
    if (context->intern_count >= UINT32_MAX) {
        context->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
        return false;
    }
    if (!r_grow_array(context,
                      (void **)&context->intern_entries,
                      &context->intern_capacity,
                      sizeof(*context->intern_entries),
                      context->intern_count + 1U)) {
        return false;
    }
    entry = &context->intern_entries[context->intern_count];
    entry->bytes = r_copy_string(context, (const char *)bytes, length);
    if (entry->bytes == NULL) {
        return false;
    }
    entry->length = length;
    entry->hash = hash;
    context->intern_count += 1U;
    *intern_id = (uint32_t)context->intern_count;
    return true;
}

static bool r_add_line_start(RFrontendContext *context, RSource *source, uint32_t offset) {
    if (!r_grow_array(context,
                      (void **)&source->line_starts,
                      &source->line_capacity,
                      sizeof(*source->line_starts),
                      source->line_count + 1U)) {
        return false;
    }
    source->line_starts[source->line_count] = offset;
    source->line_count += 1U;
    return true;
}

static bool r_build_line_index(RFrontendContext *context, RSource *source) {
    size_t offset = 0U;

    if (!r_add_line_start(context, source, 0U)) {
        return false;
    }
    while (offset < source->length) {
        if (source->bytes[offset] == (uint8_t)'\r') {
            offset += 1U;
            if ((offset < source->length) && (source->bytes[offset] == (uint8_t)'\n')) {
                offset += 1U;
            }
            if (!r_add_line_start(context, source, (uint32_t)offset)) {
                return false;
            }
        } else if (source->bytes[offset] == (uint8_t)'\n') {
            offset += 1U;
            if (!r_add_line_start(context, source, (uint32_t)offset)) {
                return false;
            }
        } else {
            offset += 1U;
        }
    }
    return true;
}

/* R-AGG-0012 (L27): appends generated text after the bytes of `source` and extends its line
   index. */
bool r_source_append_bytes(RFrontendContext *context,
                           RSource *source,
                           const uint8_t *bytes,
                           size_t length) {
    const size_t old_length = source->length;
    uint8_t *grown;
    size_t offset;

    if ((length > context->options.limits.max_source_bytes) ||
        (old_length > context->options.limits.max_source_bytes - length) ||
        (old_length + length > UINT32_MAX) || (old_length + length > SIZE_MAX - R_SOURCE_PADDING)) {
        context->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
        return false;
    }
    grown = r_context_allocate(context, old_length + length + R_SOURCE_PADDING);
    if (grown == NULL) {
        return false;
    }
    if (old_length != 0U) {
        (void)memcpy(grown, source->bytes, old_length);
    }
    if (length != 0U) {
        (void)memcpy(grown + old_length, bytes, length);
    }
    (void)memset(grown + old_length + length, 0, R_SOURCE_PADDING);
    r_context_free(context, source->bytes);
    source->bytes = grown;
    source->length = old_length + length;
    for (offset = old_length; offset < source->length; ++offset) {
        const uint8_t byte = source->bytes[offset];
        if ((byte == (uint8_t)'\n') ||
            ((byte == (uint8_t)'\r') &&
             ((offset + 1U >= source->length) || (source->bytes[offset + 1U] != (uint8_t)'\n')))) {
            if (!r_add_line_start(context, source, (uint32_t)(offset + 1U))) {
                return false;
            }
        }
    }
    return true;
}

/* R-AGG-0012 (L27): the derivation region that holds `span`, or NULL for authored text. */
RDeriveRegion *r_source_derive_region(const RFrontendContext *context, RSourceSpan span) {
    RSource *source;

    if ((context == NULL) || (span.source == R_SOURCE_ID_INVALID) ||
        ((size_t)span.source > context->source_count)) {
        return NULL;
    }
    source = &context->sources[(size_t)span.source - 1U];
    if (!source->derive_expanded || (span.start < source->authored_length)) {
        return NULL;
    }
    for (size_t index = 0U; index < source->derive_region_count; ++index) {
        RDeriveRegion *region = &source->derive_regions[index];
        if ((span.start >= region->start) && (span.start < region->end)) {
            return region;
        }
    }
    return NULL;
}

RFrontendStatus r_frontend_add_source(RFrontendContext *context,
                                      const char *display_name,
                                      const uint8_t *bytes,
                                      size_t length,
                                      RSourceId *source_id) {
    RSource *source;
    size_t padded_length;

    if ((context == NULL) || (display_name == NULL) || (source_id == NULL) ||
        ((bytes == NULL) && (length != 0U))) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    if (context->sources_sealed) {
        *source_id = R_SOURCE_ID_INVALID;
        return R_FRONTEND_CONTEXT_SEALED;
    }
    if ((length > context->options.limits.max_source_bytes) || (length > UINT32_MAX) ||
        (context->source_count >= UINT32_MAX)) {
        RSourceSpan span = {R_SOURCE_ID_INVALID, UINT32_C(0), UINT32_C(0)};
        if ((length <= UINT32_MAX) && (context->source_count < UINT32_MAX)) {
            span.start = (uint32_t)length;
            span.end = span.start;
        }
        if (!r_add_diagnostic(context,
                              "R-DIAG-LIMIT-001",
                              "R-LIMIT-0002",
                              "source or source-count limit exceeded",
                              R_DIAGNOSTIC_ERROR,
                              span) &&
            (context->resource_status == R_FRONTEND_OUT_OF_MEMORY)) {
            return context->resource_status;
        }
        context->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
        return R_FRONTEND_LIMIT_EXCEEDED;
    }
    if (length > (SIZE_MAX - R_SOURCE_PADDING)) {
        context->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
        return R_FRONTEND_LIMIT_EXCEEDED;
    }
    if (!r_grow_array(context,
                      (void **)&context->sources,
                      &context->source_capacity,
                      sizeof(*context->sources),
                      context->source_count + 1U)) {
        return context->resource_status;
    }

    source = &context->sources[context->source_count];
    (void)memset(source, 0, sizeof(*source));
    source->display_name = r_copy_string(context, display_name, strlen(display_name));
    if (source->display_name == NULL) {
        return context->resource_status;
    }
    padded_length = length + R_SOURCE_PADDING;
    source->bytes = r_context_allocate(context, padded_length);
    if (source->bytes == NULL) {
        r_destroy_source(context, source);
        return context->resource_status;
    }
    if (length != 0U) {
        (void)memcpy(source->bytes, bytes, length);
    }
    (void)memset(source->bytes + length, 0, R_SOURCE_PADDING);
    source->length = length;
    if (!r_build_line_index(context, source)) {
        r_destroy_source(context, source);
        return context->resource_status;
    }

    context->source_count += 1U;
    *source_id = (RSourceId)context->source_count;
    context->interfaces_linked = false;
    return R_FRONTEND_OK;
}

RSource *r_get_source(RFrontendContext *context, RSourceId source_id) {
    if ((context == NULL) || (source_id == R_SOURCE_ID_INVALID) ||
        ((size_t)source_id > context->source_count)) {
        return NULL;
    }
    return &context->sources[(size_t)source_id - 1U];
}

const RSource *r_get_source_const(const RFrontendContext *context, RSourceId source_id) {
    if ((context == NULL) || (source_id == R_SOURCE_ID_INVALID) ||
        ((size_t)source_id > context->source_count)) {
        return NULL;
    }
    return &context->sources[(size_t)source_id - 1U];
}

static bool r_diagnostic_code_has_prefix(const char *code, const char *prefix) {
    return strncmp(code, prefix, strlen(prefix)) == 0;
}

static RDiagnosticPhase r_diagnostic_phase_from_code(const char *code) {
    if (r_diagnostic_code_has_prefix(code, "R-DIAG-LEX-")) {
        return R_DIAGNOSTIC_PHASE_LEXICAL;
    }
    if (r_diagnostic_code_has_prefix(code, "R-DIAG-MOVE-") ||
        r_diagnostic_code_has_prefix(code, "R-DIAG-BORROW-") ||
        r_diagnostic_code_has_prefix(code, "R-DIAG-DROP-")) {
        return R_DIAGNOSTIC_PHASE_OWNERSHIP;
    }
    if (r_diagnostic_code_has_prefix(code, "R-DIAG-NAME-") ||
        r_diagnostic_code_has_prefix(code, "R-DIAG-INIT-") ||
        r_diagnostic_code_has_prefix(code, "R-DIAG-TYPE-") ||
        r_diagnostic_code_has_prefix(code, "R-DIAG-CONST-") ||
        r_diagnostic_code_has_prefix(code, "R-DIAG-BOUNDS-") ||
        r_diagnostic_code_has_prefix(code, "R-DIAG-FLOW-") ||
        r_diagnostic_code_has_prefix(code, "R-DIAG-SWITCH-") ||
        r_diagnostic_code_has_prefix(code, "R-DIAG-UNSAFE-") ||
        r_diagnostic_code_has_prefix(code, "R-DIAG-MEM-") ||
        r_diagnostic_code_has_prefix(code, "R-DIAG-ATOMIC-") ||
        r_diagnostic_code_has_prefix(code, "R-DIAG-MOD-") ||
        r_diagnostic_code_has_prefix(code, "R-DIAG-FFI-") ||
        r_diagnostic_code_has_prefix(code, "R-DIAG-LINK-") ||
        r_diagnostic_code_has_prefix(code, "R-DIAG-PROFILE-")) {
        return R_DIAGNOSTIC_PHASE_SEMANTIC;
    }
    return R_DIAGNOSTIC_PHASE_SYNTAX;
}

bool r_add_diagnostic_phase(RFrontendContext *context,
                            RDiagnosticPhase phase,
                            const char *code,
                            const char *rule_id,
                            const char *message,
                            RDiagnosticSeverity severity,
                            RSourceSpan span) {
    RDiagnostic *diagnostic;
    RSource *source;

    if ((context == NULL) || (code == NULL) || (rule_id == NULL) || (message == NULL)) {
        return false;
    }
    if ((phase != R_DIAGNOSTIC_PHASE_LEXICAL) && (phase != R_DIAGNOSTIC_PHASE_SYNTAX) &&
        (phase != R_DIAGNOSTIC_PHASE_SEMANTIC) && (phase != R_DIAGNOSTIC_PHASE_OWNERSHIP)) {
        return false;
    }
    if (context->diagnostic_count >= (size_t)context->options.limits.max_diagnostics) {
        context->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
        return false;
    }
    if ((context->diagnostic_count + 1U == (size_t)context->options.limits.max_diagnostics) &&
        (strcmp(code, "R-DIAG-LIMIT-001") != 0)) {
        code = "R-DIAG-LIMIT-001";
        rule_id = "R-LIMIT-0003";
        message = "diagnostic limit exceeded";
        severity = R_DIAGNOSTIC_ERROR;
        context->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
    }
    if (!r_grow_array(context,
                      (void **)&context->diagnostics,
                      &context->diagnostic_capacity,
                      sizeof(*context->diagnostics),
                      context->diagnostic_count + 1U)) {
        return false;
    }
    {
        /* R-AGG-0012 (L27): a diagnostic inside generated declarations names the derived
           capability; each derivation reports one diagnostic, its own check or the first
           one inside its declarations. */
        RDeriveRegion *region = r_source_derive_region(context, span);
        if (region != NULL) {
            source = r_get_source(context, span.source);
            if (region->suppressed) {
                if ((source != NULL) && (severity == R_DIAGNOSTIC_ERROR)) {
                    source->has_semantic_error = true;
                }
                return true;
            }
            if (severity == R_DIAGNOSTIC_ERROR) {
                region->suppressed = true;
            }
            span = region->origin;
        }
    }
    diagnostic = &context->diagnostics[context->diagnostic_count];
    diagnostic->code = code;
    diagnostic->rule_id = rule_id;
    diagnostic->message = message;
    diagnostic->severity = severity;
    diagnostic->phase = phase;
    diagnostic->primary_span = span;
    context->diagnostic_count += 1U;

    source = r_get_source(context, span.source);
    if ((source != NULL) && (severity == R_DIAGNOSTIC_ERROR)) {
        if (phase == R_DIAGNOSTIC_PHASE_LEXICAL) {
            source->has_lex_error = true;
        } else if (phase == R_DIAGNOSTIC_PHASE_SYNTAX) {
            source->has_syntax_error = true;
        } else if (phase == R_DIAGNOSTIC_PHASE_SEMANTIC) {
            source->has_semantic_error = true;
        } else if (phase == R_DIAGNOSTIC_PHASE_OWNERSHIP) {
            source->has_ownership_error = true;
        }
    }
    return true;
}

bool r_add_diagnostic(RFrontendContext *context,
                      const char *code,
                      const char *rule_id,
                      const char *message,
                      RDiagnosticSeverity severity,
                      RSourceSpan span) {
    if (code == NULL) {
        return false;
    }
    return r_add_diagnostic_phase(
        context, r_diagnostic_phase_from_code(code), code, rule_id, message, severity, span);
}

bool r_add_token(
    RFrontendContext *context, RSource *source, RTokenKind kind, uint32_t start, uint32_t end) {
    RToken *token;
    uint32_t intern_id = UINT32_C(0);

    if ((context == NULL) || (source == NULL) || (start > end) || ((size_t)end > source->length) ||
        (source->token_count >= UINT32_MAX)) {
        if (context != NULL) {
            context->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
        }
        return false;
    }
    /* The receiver keyword names a parameter (R-FUNC-0013), so it is interned like a name. */
    if (((kind == R_TOKEN_IDENTIFIER) || (kind == R_TOKEN_KW_THIS)) &&
        !r_intern_bytes(context, source->bytes + start, (size_t)(end - start), &intern_id)) {
        return false;
    }
    if (!r_grow_array(context,
                      (void **)&source->tokens,
                      &source->token_capacity,
                      sizeof(*source->tokens),
                      source->token_count + 1U)) {
        return false;
    }
    token = &source->tokens[source->token_count];
    token->kind = kind;
    token->span.source = (RSourceId)((source - context->sources) + 1);
    token->span.start = start;
    token->span.end = end;
    token->intern_id = intern_id;
    source->token_count += 1U;
    return true;
}

size_t r_frontend_source_count(const RFrontendContext *context) {
    return (context == NULL) ? 0U : context->source_count;
}

const char *r_frontend_source_name(const RFrontendContext *context, RSourceId source_id) {
    const RSource *source = r_get_source_const(context, source_id);
    return (source == NULL) ? NULL : source->display_name;
}

size_t r_frontend_diagnostic_count(const RFrontendContext *context) {
    return (context == NULL) ? 0U : context->diagnostic_count;
}

const RDiagnostic *r_frontend_diagnostic(const RFrontendContext *context, size_t index) {
    if ((context == NULL) || (index >= context->diagnostic_count)) {
        return NULL;
    }
    return &context->diagnostics[index];
}

void r_frontend_source_position(const RFrontendContext *context,
                                RSourceSpan span,
                                uint32_t *line,
                                uint32_t *column) {
    const RSource *source = r_get_source_const(context, span.source);
    size_t lower = 0U;
    size_t upper;
    size_t line_index;
    size_t offset;
    uint32_t result_column = 1U;

    {
        /* R-AGG-0012 (L27): generated declarations take the position of their capability. */
        const RDeriveRegion *region = r_source_derive_region(context, span);
        if (region != NULL) {
            span = region->origin;
        }
    }
    if (line != NULL) {
        *line = 0U;
    }
    if (column != NULL) {
        *column = 0U;
    }
    if ((source == NULL) || ((size_t)span.start > source->length) || (source->line_count == 0U)) {
        return;
    }
    upper = source->line_count;
    while (lower + 1U < upper) {
        size_t middle = lower + ((upper - lower) / 2U);
        if (source->line_starts[middle] <= span.start) {
            lower = middle;
        } else {
            upper = middle;
        }
    }
    line_index = lower;
    offset = source->line_starts[line_index];
    while (offset < (size_t)span.start) {
        uint32_t code_point;
        size_t width;
        if (!r_utf8_decode(source->bytes, source->length, offset, &code_point, &width)) {
            width = 1U;
        }
        (void)code_point;
        offset += width;
        result_column += 1U;
    }
    if (line != NULL) {
        *line = (uint32_t)line_index + 1U;
    }
    if (column != NULL) {
        *column = result_column;
    }
}

bool r_source_text_equal(const RSource *source, RSourceSpan span, const char *text) {
    size_t text_length;
    size_t span_length;

    if ((source == NULL) || (text == NULL) || (span.end < span.start) ||
        ((size_t)span.end > source->length)) {
        return false;
    }
    text_length = strlen(text);
    span_length = (size_t)(span.end - span.start);
    return (text_length == span_length) &&
           (memcmp(source->bytes + span.start, text, span_length) == 0);
}

bool r_source_span_equal(const RSource *left_source,
                         RSourceSpan left,
                         const RSource *right_source,
                         RSourceSpan right) {
    size_t left_length;
    size_t right_length;

    if ((left_source == NULL) || (right_source == NULL) || (left.end < left.start) ||
        (right.end < right.start) || ((size_t)left.end > left_source->length) ||
        ((size_t)right.end > right_source->length)) {
        return false;
    }
    left_length = (size_t)(left.end - left.start);
    right_length = (size_t)(right.end - right.start);
    return (left_length == right_length) && (memcmp(left_source->bytes + left.start,
                                                    right_source->bytes + right.start,
                                                    left_length) == 0);
}

static bool r_span_equals_owned(const RSource *source, RSourceSpan span, const char *owned) {
    return r_source_text_equal(source, span, owned);
}

bool r_is_visible_aggregate(const RFrontendContext *context,
                            RSourceId source_id,
                            RSourceSpan name,
                            uint32_t use_offset) {
    const RSource *source = r_get_source_const(context, source_id);
    size_t aggregate_index;
    size_t import_index;

    if (source == NULL) {
        return false;
    }
    for (aggregate_index = 0U; aggregate_index < context->aggregate_count; ++aggregate_index) {
        const RAggregateEntry *aggregate = &context->aggregates[aggregate_index];
        if ((aggregate->source == source_id) &&
            (aggregate->is_error || aggregate->is_generic ||
             (aggregate->declaration_offset <= use_offset)) &&
            r_span_equals_owned(source, name, aggregate->name)) {
            return true;
        }
    }
    for (import_index = 0U; import_index < context->import_count; ++import_index) {
        const RImportEntry *import = &context->imports[import_index];
        if ((import->source != source_id) || !import->imports_name ||
            !r_span_equals_owned(source, name, import->name)) {
            continue;
        }
        for (aggregate_index = 0U; aggregate_index < context->aggregate_count; ++aggregate_index) {
            const RAggregateEntry *aggregate = &context->aggregates[aggregate_index];
            if (!aggregate->is_protected &&
                (strcmp(import->module_name, aggregate->module_name) == 0) &&
                (strcmp(import->name, aggregate->name) == 0)) {
                return true;
            }
        }
    }
    return false;
}

RFrontendStatus r_frontend_link_interfaces(RFrontendContext *context) {
    size_t source_index;

    if (context == NULL) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    if (context->resource_status != R_FRONTEND_OK) {
        return context->resource_status;
    }
    for (source_index = 0U; source_index < context->source_count; ++source_index) {
        RSourceId source_id = (RSourceId)(source_index + 1U);
        RFrontendStatus status;
        if (!context->sources[source_index].lexed) {
            status = r_frontend_lex(context, source_id);
            if ((status != R_FRONTEND_OK) && (status != R_FRONTEND_INVALID_SOURCE)) {
                return status;
            }
        }
        if (!context->sources[source_index].interface_scanned) {
            status = r_frontend_scan_interface(context, source_id);
            if ((status != R_FRONTEND_OK) && (status != R_FRONTEND_INVALID_SOURCE)) {
                return status;
            }
        }
    }
    context->interfaces_linked = true;
    return R_FRONTEND_OK;
}

RFrontendStatus
r_frontend_parse_cst(RFrontendContext *context, RSourceId source_id, RSyntaxNodeId *root_node) {
    RFrontendStatus status;
    RSource *source;

    if ((context == NULL) || (root_node == NULL)) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    if (context->resource_status != R_FRONTEND_OK) {
        return context->resource_status;
    }
    source = r_get_source(context, source_id);
    if (source == NULL) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    status = r_frontend_link_interfaces(context);
    if ((status != R_FRONTEND_OK) && (status != R_FRONTEND_INVALID_SOURCE)) {
        return status;
    }
    status = r_parse_source(context, source_id);
    *root_node = source->cst_root;
    if (context->resource_status != R_FRONTEND_OK) {
        return context->resource_status;
    }
    return status;
}

RFrontendStatus
r_frontend_lower_ast(RFrontendContext *context, RSourceId source_id, RAstNodeId *root_node) {
    RSource *source;
    RFrontendStatus status;
    RSyntaxNodeId cst_root;

    if ((context == NULL) || (root_node == NULL)) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    if (context->resource_status != R_FRONTEND_OK) {
        return context->resource_status;
    }
    source = r_get_source(context, source_id);
    if (source == NULL) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    if (!source->parsed) {
        status = r_frontend_parse_cst(context, source_id, &cst_root);
        if ((status != R_FRONTEND_OK) && (status != R_FRONTEND_INVALID_SOURCE)) {
            return status;
        }
        if (context->resource_status != R_FRONTEND_OK) {
            return context->resource_status;
        }
    }
    if (source->has_lex_error || source->has_syntax_error || source->has_unresolved_ambiguity) {
        *root_node = R_AST_NODE_ID_INVALID;
        return R_FRONTEND_NOT_LOWERABLE;
    }
    status = r_lower_source_ast(context, source_id);
    *root_node = source->ast_root;
    return status;
}

/* R-LIMIT-0003: a translation limit that a phase after the parser exhausts is diagnosed as the
   parser's own limits are, also where that phase names no source position. */
void r_frontend_diagnose_limit(RFrontendContext *context, const char *message) {
    RSourceSpan span = {R_SOURCE_ID_INVALID, UINT32_C(0), UINT32_C(0)};

    for (size_t index = 0U; index < context->diagnostic_count; ++index) {
        if (strcmp(context->diagnostics[index].code, "R-DIAG-LIMIT-001") == 0) {
            return;
        }
    }
    if (context->source_count != 0U) {
        span.source = (RSourceId)1U;
    }
    (void)r_add_diagnostic(
        context, "R-DIAG-LIMIT-001", "R-LIMIT-0003", message, R_DIAGNOSTIC_ERROR, span);
}

RFrontendStatus r_frontend_analyze(RFrontendContext *context) {
    RFrontendStatus status;

    if (context == NULL) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    status = r_analyze_program(context);
    if (status == R_FRONTEND_LIMIT_EXCEEDED) {
        r_frontend_diagnose_limit(context, "semantic analysis exceeded a translation limit");
    }
    return status;
}

RHirNodeId r_frontend_hir_root(const RFrontendContext *context) {
    if ((context == NULL) || !context->semantic_analyzed) {
        return R_HIR_NODE_ID_INVALID;
    }
    return context->hir_root;
}

size_t r_frontend_source_import_count(const RFrontendContext *context, RSourceId source_id) {
    size_t count = 0U;
    size_t index;

    if ((context == NULL) || (r_get_source_const(context, source_id) == NULL)) {
        return 0U;
    }
    for (index = 0U; index < context->import_count; ++index) {
        if (context->imports[index].source == source_id) {
            count += 1U;
        }
    }
    return count;
}

const char *r_frontend_source_module_name(const RFrontendContext *context, RSourceId source_id) {
    const RSource *source = r_get_source_const(context, source_id);
    if ((source == NULL) || !source->interface_scanned) {
        return NULL;
    }
    return source->module_name;
}

const char *r_frontend_source_import_module(const RFrontendContext *context,
                                            RSourceId source_id,
                                            size_t import_index) {
    size_t seen = 0U;
    size_t index;

    if ((context == NULL) || (r_get_source_const(context, source_id) == NULL)) {
        return NULL;
    }
    for (index = 0U; index < context->import_count; ++index) {
        if (context->imports[index].source != source_id) {
            continue;
        }
        if (seen == import_index) {
            return context->imports[index].module_name;
        }
        seen += 1U;
    }
    return NULL;
}

bool r_token_is_trivia(RTokenKind kind) {
    return (kind == R_TOKEN_WHITESPACE) || (kind == R_TOKEN_LINE_COMMENT) ||
           (kind == R_TOKEN_BLOCK_COMMENT) || (kind == R_TOKEN_BOM);
}

bool r_token_is_keyword(RTokenKind kind) {
    return (kind >= R_TOKEN_KW_ALIGNOF) && (kind <= R_TOKEN_KW_C_PTRDIFF);
}

bool r_token_is_c_abi_type(RTokenKind kind) {
    return (kind >= R_TOKEN_KW_C_CHAR) && (kind <= R_TOKEN_KW_C_PTRDIFF);
}

bool r_token_is_primitive_type(RTokenKind kind) {
    return (kind == R_TOKEN_KW_NULL_T) || (kind == R_TOKEN_KW_BOOL) || (kind == R_TOKEN_KW_CHAR) ||
           (kind == R_TOKEN_KW_F32) || (kind == R_TOKEN_KW_F64) || (kind == R_TOKEN_KW_I8) ||
           (kind == R_TOKEN_KW_I16) || (kind == R_TOKEN_KW_I32) || (kind == R_TOKEN_KW_I64) ||
           (kind == R_TOKEN_KW_ISIZE) || (kind == R_TOKEN_KW_U8) || (kind == R_TOKEN_KW_U16) ||
           (kind == R_TOKEN_KW_U32) || (kind == R_TOKEN_KW_U64) || (kind == R_TOKEN_KW_USIZE) ||
           r_token_is_c_abi_type(kind);
}

const char *r_token_kind_name(RTokenKind kind) {
    static const char *const names[R_TOKEN_KIND_COUNT] = {"invalid",
                                                          "eof",
                                                          "whitespace",
                                                          "line_comment",
                                                          "block_comment",
                                                          "bom",
                                                          "error",
                                                          "identifier",
                                                          "integer_literal",
                                                          "float_literal",
                                                          "character_literal",
                                                          "string_literal",
                                                          "format_start",
                                                          "format_end",
                                                          "format_text",
                                                          "format_spec",
                                                          "{",
                                                          "}",
                                                          "[",
                                                          "]",
                                                          "(",
                                                          ")",
                                                          ";",
                                                          ",",
                                                          ".",
                                                          "..",
                                                          "...",
                                                          ":",
                                                          "::",
                                                          "?",
                                                          "@",
                                                          "+",
                                                          "-",
                                                          "*",
                                                          "/",
                                                          "%",
                                                          "&",
                                                          "|",
                                                          "^",
                                                          "~",
                                                          "!",
                                                          "=",
                                                          "<",
                                                          ">",
                                                          "++",
                                                          "--",
                                                          "->",
                                                          "==",
                                                          "!=",
                                                          "<=",
                                                          ">=",
                                                          "&&",
                                                          "||",
                                                          "<<",
                                                          ">>",
                                                          "+=",
                                                          "-=",
                                                          "*=",
                                                          "/=",
                                                          "%=",
                                                          "&=",
                                                          "|=",
                                                          "^=",
                                                          "<<=",
                                                          ">>=",
                                                          "alignof",
                                                          "ai8",
                                                          "ai16",
                                                          "ai32",
                                                          "ai64",
                                                          "aisize",
                                                          "arc",
                                                          "array",
                                                          "as",
                                                          "async",
                                                          "atomic",
                                                          "au8",
                                                          "au16",
                                                          "au32",
                                                          "au64",
                                                          "ausize",
                                                          "await",
                                                          "bool",
                                                          "break",
                                                          "case",
                                                          "catch",
                                                          "char",
                                                          "const",
                                                          "constexpr",
                                                          "continue",
                                                          "default",
                                                          "dict",
                                                          "drop",
                                                          "dyn",
                                                          "else",
                                                          "enum",
                                                          "extern",
                                                          "f32",
                                                          "f64",
                                                          "false",
                                                          "fallthrough",
                                                          "finally",
                                                          "fn",
                                                          "for",
                                                          "i8",
                                                          "i16",
                                                          "i32",
                                                          "i64",
                                                          "if",
                                                          "import",
                                                          "isize",
                                                          "list",
                                                          "module",
                                                          "move",
                                                          "never",
                                                          "new",
                                                          "null",
                                                          "null_t",
                                                          "o",
                                                          "opaque",
                                                          "own",
                                                          "panic",
                                                          "protected",
                                                          "raw",
                                                          "rc",
                                                          "return",
                                                          "sizeof",
                                                          "static",
                                                          "str",
                                                          "struct",
                                                          "switch",
                                                          "task",
                                                          "thread_local",
                                                          "thread_scope",
                                                          "throw",
                                                          "throws",
                                                          "true",
                                                          "try",
                                                          "u8",
                                                          "u16",
                                                          "u32",
                                                          "u64",
                                                          "unsafe",
                                                          "usize",
                                                          "variant",
                                                          "void",
                                                          "weak",
                                                          "while",
                                                          "impl",
                                                          "Self",
                                                          "this",
                                                          "trait",
                                                          "auto",
                                                          "in",
                                                          "c_char",
                                                          "c_schar",
                                                          "c_uchar",
                                                          "c_short",
                                                          "c_ushort",
                                                          "c_int",
                                                          "c_uint",
                                                          "c_long",
                                                          "c_ulong",
                                                          "c_llong",
                                                          "c_ullong",
                                                          "c_bool",
                                                          "c_wchar",
                                                          "c_wint",
                                                          "c_int8",
                                                          "c_uint8",
                                                          "c_int16",
                                                          "c_uint16",
                                                          "c_int32",
                                                          "c_uint32",
                                                          "c_int64",
                                                          "c_uint64",
                                                          "c_intptr",
                                                          "c_uintptr",
                                                          "c_intmax",
                                                          "c_uintmax",
                                                          "c_float",
                                                          "c_double",
                                                          "c_long_double",
                                                          "c_size",
                                                          "c_ptrdiff"};

    if ((kind < R_TOKEN_INVALID) || (kind >= R_TOKEN_KIND_COUNT)) {
        return "invalid";
    }
    return names[(size_t)kind];
}

const char *r_syntax_kind_name(RSyntaxKind kind) {
    static const char *const names[R_SYNTAX_KIND_COUNT] = {"invalid",
                                                           "translation_unit",
                                                           "module_declaration",
                                                           "import_declaration",
                                                           "external_declaration",
                                                           "generic_header",
                                                           "generic_parameter",
                                                           "associated_name",
                                                           "attribute",
                                                           "struct_declaration",
                                                           "error_struct_declaration",
                                                           "field_declaration",
                                                           "enum_declaration",
                                                           "error_enum_declaration",
                                                           "enum_variant",
                                                           "object_declaration",
                                                           "destructuring_declaration",
                                                           "function_declaration",
                                                           "parameter_list",
                                                           "parameter",
                                                           "throws_clause",
                                                           "drop_definition",
                                                           "extern_block",
                                                           "c_declaration",
                                                           "trait_declaration",
                                                           "trait_name",
                                                           "impl_declaration",
                                                           "lambda_declaration",
                                                           "move_capture_list",
                                                           "callable_constraint",
                                                           "opaque_result",
                                                           "dyn_type",
                                                           "associated_type",
                                                           "associated_constant",
                                                           "type",
                                                           "initializer",
                                                           "aggregate_initializer",
                                                           "initializer_item",
                                                           "block",
                                                           "expression_statement",
                                                           "if_statement",
                                                           "while_statement",
                                                           "for_statement",
                                                           "for_in_statement",
                                                           "switch_statement",
                                                           "case_clause",
                                                           "default_clause",
                                                           "case_pattern",
                                                           "match_expression",
                                                           "match_arm",
                                                           "match_pattern",
                                                           "match_field",
                                                           "match_guard",
                                                           "pattern_test",
                                                           "thread_scope_statement",
                                                           "task_scope_statement",
                                                           "select_statement",
                                                           "select_clause",
                                                           "select_deadline",
                                                           "deadline_statement",
                                                           "budget_statement",
                                                           "try_statement",
                                                           "catch_clause",
                                                           "finally_clause",
                                                           "jump_statement",
                                                           "labeled_statement",
                                                           "throw_statement",
                                                           "throw_condition",
                                                           "throw_else",
                                                           "drop_statement",
                                                           "await_statement",
                                                           "await_operation",
                                                           "unsafe_block",
                                                           "expression",
                                                           "assignment_expression",
                                                           "conditional_expression",
                                                           "membership_expression",
                                                           "range_expression",
                                                           "collection_expression",
                                                           "dict_expression",
                                                           "dict_entry",
                                                           "comprehension_for",
                                                           "comprehension_if",
                                                           "spread_argument",
                                                           "out_argument",
                                                           "binary_expression",
                                                           "unary_expression",
                                                           "cast_expression",
                                                           "postfix_expression",
                                                           "call_suffix",
                                                           "constructor_suffix",
                                                           "index_suffix",
                                                           "slice_suffix",
                                                           "member_suffix",
                                                           "method_call_suffix",
                                                           "primary_expression",
                                                           "qualified_name",
                                                           "argument_list",
                                                           "aggregate_constructor",
                                                           "standard_type_call",
                                                           "new_expression",
                                                           "string_sequence",
                                                           "format_sequence",
                                                           "format_literal",
                                                           "format_slot",
                                                           "format_suffix",
                                                           "ambiguous_decl_or_expr",
                                                           "error",
                                                           "token",
                                                           "static_if_statement",
                                                           "static_if_declaration",
                                                           "static_declarations",
                                                           "static_predicate",
                                                           "static_conjunction",
                                                           "static_atom",
                                                           "callable_mode",
                                                           "generic_argument_list",
                                                           "tuple_expression",
                                                           "pack_expansion"};

    if ((kind < R_SYNTAX_INVALID) || (kind >= R_SYNTAX_KIND_COUNT)) {
        return "invalid";
    }
    return names[(size_t)kind];
}

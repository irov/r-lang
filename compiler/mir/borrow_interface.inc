/* R-BORROW-0007/R-BORROW-0009: export region bounds, never compiler-local identities.
 * The bound applies to every nested field, active payload and indexed projection. It does
 * not assert that two projections are disjoint or that a copied descriptor owns its referent.
 * `held` lists the inputs the bound takes only what their designated storage holds from.
 * Source modules are still required; importing this record never manufactures a lifetime. */
static bool r_mir_write_borrow_bound(const RFrontendContext *context,
                                     RTypeId type,
                                     uint32_t parameter,
                                     uint64_t low,
                                     uint64_t high,
                                     uint64_t held_low,
                                     uint64_t held_high,
                                     RFrontendWriteFn writer,
                                     void *user_data) {
    const bool may_borrow = r_semantic_type_crosses_await(context, type) ||
                            r_generic_type_is_dependent(context, type, 0U);
    const bool exact = low != 0U || high != 0U || (parameter != 0U && parameter != UINT32_MAX);
    if (!r_write_text(writer, user_data, "(origins="))
        return false;
    if (!may_borrow)
        return r_write_text(writer, user_data, "none)");
    if (!exact)
        return r_write_text(writer,
                            user_data,
                            parameter == UINT32_MAX ? "conservative_inputs projections=union)"
                                                    : "static projections=union)");
    if (!r_write_text(writer, user_data, "inputs parameters=("))
        return false;
    bool first = true;
    for (uint32_t index = 0U; index < 128U; ++index) {
        const bool present = index < 64U ? (low & (UINT64_C(1) << index)) != 0U
                                         : (high & (UINT64_C(1) << (index - 64U))) != 0U;
        if (!present && parameter != index + 1U)
            continue;
        if ((!first && !r_write_text(writer, user_data, " ")) ||
            !r_write_uint32(writer, user_data, index))
            return false;
        first = false;
    }
    if (!r_write_text(writer, user_data, ")"))
        return false;
    if ((held_low & low) != 0U || (held_high & high) != 0U) {
        if (!r_write_text(writer, user_data, " held=("))
            return false;
        first = true;
        for (uint32_t index = 0U; index < 128U; ++index) {
            const bool held = index < 64U
                                  ? (held_low & low & (UINT64_C(1) << index)) != 0U
                                  : (held_high & high & (UINT64_C(1) << (index - 64U))) != 0U;
            if (!held)
                continue;
            if ((!first && !r_write_text(writer, user_data, " ")) ||
                !r_write_uint32(writer, user_data, index))
                return false;
            first = false;
        }
        if (!r_write_text(writer, user_data, ")"))
            return false;
    }
    return r_write_text(writer, user_data, " projections=union)");
}

/* Paths contain field names, constant indices (or a wildcard), and dereferences. They
 * never contain symbol, type-table or HIR node identifiers from this compilation. */
static RHirNodeId r_mir_borrow_path_root(const RFrontendContext *context, RHirNodeId path) {
    for (uint32_t depth = 0U; depth <= context->options.limits.max_nesting; ++depth) {
        if (path == R_HIR_NODE_ID_INVALID || path > context->hir_node_count)
            return R_HIR_NODE_ID_INVALID;
        const RHirNode *node = &context->hir_nodes[path - 1U];
        if (node->kind == R_HIR_PLACE)
            return path;
        if (node->kind != R_HIR_FIELD_PLACE && node->kind != R_HIR_INDEX_PLACE &&
            node->kind != R_HIR_DEREF_PLACE)
            return R_HIR_NODE_ID_INVALID;
        path = r_mir_hir_child(context, node, 0U);
    }
    return R_HIR_NODE_ID_INVALID;
}

static bool r_mir_write_borrow_path(const RFrontendContext *context,
                                    RHirNodeId path,
                                    uint32_t depth,
                                    RFrontendWriteFn writer,
                                    void *user_data) {
    if (path == R_HIR_NODE_ID_INVALID || path > context->hir_node_count ||
        depth > context->options.limits.max_nesting)
        return r_write_text(writer, user_data, "unknown");
    const RHirNode *node = &context->hir_nodes[path - 1U];
    if (node->kind == R_HIR_PLACE)
        return true;
    if (node->kind != R_HIR_FIELD_PLACE && node->kind != R_HIR_INDEX_PLACE &&
        node->kind != R_HIR_DEREF_PLACE)
        return r_write_text(writer, user_data, "unknown");
    const RHirNodeId parent = r_mir_hir_child(context, node, 0U);
    if (!r_mir_write_borrow_path(context, parent, depth + 1U, writer, user_data) ||
        (parent != R_HIR_NODE_ID_INVALID && parent <= context->hir_node_count &&
         context->hir_nodes[parent - 1U].kind != R_HIR_PLACE &&
         !r_write_text(writer, user_data, " ")))
        return false;
    if (node->kind == R_HIR_DEREF_PLACE)
        return r_write_text(writer, user_data, "deref");
    if (node->kind == R_HIR_FIELD_PLACE) {
        if (node->aggregate_member == 0U || node->aggregate_member > context->semantic_field_count)
            return r_write_text(writer, user_data, "unknown");
        const RSemanticField *field = &context->semantic_fields[node->aggregate_member - 1U];
        const RInternEntry *name = &context->intern_entries[field->name_intern_id - 1U];
        return r_write_text(writer, user_data, "(field ") &&
               r_write_escaped(writer, user_data, (const uint8_t *)name->bytes, name->length) &&
               r_write_text(writer, user_data, ")");
    }
    const RHirNodeId index_id = r_mir_hir_child(context, node, 1U);
    const RHirNode *index = index_id == R_HIR_NODE_ID_INVALID || index_id > context->hir_node_count
                                ? NULL
                                : &context->hir_nodes[index_id - 1U];
    return r_write_text(writer, user_data, "(index ") &&
           (index != NULL && index->kind == R_HIR_LITERAL
                ? r_mir_write_uint64(writer, user_data, index->integer_value)
                : r_write_text(writer, user_data, "*")) &&
           r_write_text(writer, user_data, ")");
}

static uint32_t
r_mir_borrow_parameter(const RFrontendContext *context, RSymbolId function, RSymbolId symbol) {
    uint32_t ordinal = 0U;
    for (size_t index = 0U; index < context->semantic_symbol_count; ++index) {
        const RSemanticSymbol *candidate = &context->semantic_symbols[index];
        if (candidate->kind != R_SEMANTIC_SYMBOL_PARAMETER || candidate->parent != function)
            continue;
        if (index + 1U == symbol)
            return ordinal;
        ++ordinal;
    }
    return UINT32_MAX;
}

static bool r_mir_write_borrow_components(const RFrontendContext *context,
                                          RSymbolId function,
                                          uint32_t state_id,
                                          bool complete,
                                          RFrontendWriteFn writer,
                                          void *user_data) {
    if (!r_write_text(
            writer, user_data, complete ? "(complete=true entries=(" : "(complete=false entries=("))
        return false;
    if (state_id != 0U) {
        if (state_id > context->projected_borrow_state_count)
            return false;
        const RProjectedBorrowState state = context->projected_borrow_states[state_id - 1U];
        if (state.first > context->projected_borrow_state_entry_count ||
            state.count > context->projected_borrow_state_entry_count - state.first)
            return false;
        for (uint32_t index = 0U; index < state.count; ++index) {
            const RProjectedBorrowStateEntry entry =
                context->projected_borrow_state_entries[state.first + index];
            const RHirNodeId root_id = r_mir_borrow_path_root(context, entry.source);
            const RHirNode *root =
                root_id == R_HIR_NODE_ID_INVALID ? NULL : &context->hir_nodes[root_id - 1U];
            uint32_t parameter =
                root == NULL ? UINT32_MAX : r_mir_borrow_parameter(context, function, root->symbol);
            if (parameter == UINT32_MAX && root != NULL && !root->borrow_origin_multiple)
                parameter = r_mir_borrow_parameter(context, function, root->borrow_origin);
            if ((index != 0U && !r_write_text(writer, user_data, " ")) ||
                !r_write_text(writer, user_data, "(output=(") ||
                !r_mir_write_borrow_path(context, entry.destination, 0U, writer, user_data) ||
                !r_write_text(writer, user_data, ") input=") ||
                !(parameter == UINT32_MAX ? r_write_text(writer, user_data, "conservative")
                                          : r_write_uint32(writer, user_data, parameter)) ||
                !r_write_text(writer, user_data, " path=(") ||
                !r_mir_write_borrow_path(context, entry.source, 0U, writer, user_data) ||
                !r_write_text(writer, user_data, "))"))
                return false;
        }
    }
    return r_write_text(writer, user_data, "))");
}

static bool r_mir_write_borrow_contract(const RFrontendContext *context,
                                        RSymbolId function,
                                        RFrontendWriteFn writer,
                                        void *user_data) {
    const RSemanticSymbol *symbol = &context->semantic_symbols[function - 1U];
    const uint32_t count = r_semantic_effect_count(context, symbol->throws_type);
    if (!r_write_text(writer, user_data, " borrow_contract=(return=") ||
        !r_mir_write_borrow_bound(context,
                                  symbol->return_type,
                                  symbol->return_borrow_parameter,
                                  symbol->return_borrow_parameters_low,
                                  symbol->return_borrow_parameters_high,
                                  symbol->return_borrow_held_low,
                                  symbol->return_borrow_held_high,
                                  writer,
                                  user_data) ||
        !r_write_text(writer, user_data, " return_components=") ||
        !r_mir_write_borrow_components(context,
                                       function,
                                       symbol->return_projected_borrow_state,
                                       symbol->return_projections_complete,
                                       writer,
                                       user_data) ||
        !r_write_text(writer, user_data, " errors=("))
        return false;
    for (uint32_t index = 0U; index < count; ++index) {
        const RTypeId error = r_semantic_effect_at(context, symbol->throws_type, index);
        const RErrorBorrowMapping *mapping = NULL;
        for (size_t candidate = 0U; candidate < context->error_borrow_mapping_count; ++candidate) {
            const RErrorBorrowMapping *entry = &context->error_borrow_mappings[candidate];
            if (entry->function == function && entry->error_type == error) {
                mapping = entry;
                break;
            }
        }
        if ((index != 0U && !r_write_text(writer, user_data, " ")) ||
            !r_write_text(writer, user_data, "(type=") ||
            !r_mir_write_type(context, error, writer, user_data) ||
            !r_write_text(writer, user_data, " bound=") ||
            !r_mir_write_borrow_bound(context,
                                      error,
                                      mapping != NULL ? mapping->parameter
                                                      : (symbol->has_definition ? 0U : UINT32_MAX),
                                      mapping != NULL ? mapping->parameters_low : 0U,
                                      mapping != NULL ? mapping->parameters_high : 0U,
                                      mapping != NULL ? mapping->held_low : 0U,
                                      mapping != NULL ? mapping->held_high : 0U,
                                      writer,
                                      user_data) ||
            !r_write_text(writer, user_data, " components=") ||
            !r_mir_write_borrow_components(context,
                                           function,
                                           mapping != NULL ? mapping->projected_borrow_state : 0U,
                                           mapping != NULL && mapping->projections_complete,
                                           writer,
                                           user_data) ||
            !r_write_text(writer, user_data, ")"))
            return false;
    }
    /* R-BORROW-0019 (L15.2): the parameters whose views reach the storage another parameter
       designates, by zero-based position. */
    if (!r_write_text(writer, user_data, ") stores=("))
        return false;
    bool first_store = true;
    for (size_t index = 0U; index < context->store_borrow_mapping_count; ++index) {
        const RStoreBorrowMapping *store = &context->store_borrow_mappings[index];
        if (store->function != function)
            continue;
        if ((!first_store && !r_write_text(writer, user_data, " ")) ||
            !r_write_text(writer, user_data, "(into=") ||
            !r_write_uint32(writer, user_data, store->target - 1U) ||
            !r_write_text(writer, user_data, " from=("))
            return false;
        first_store = false;
        for (uint32_t pass = 0U; pass < 2U; ++pass) {
            bool first = true;
            for (uint32_t source = 0U; source < 128U; ++source) {
                const uint64_t bit = UINT64_C(1) << (source % 64U);
                const uint64_t sources = source < 64U ? store->sources_low : store->sources_high;
                const uint64_t held = source < 64U ? store->held_low : store->held_high;
                if ((sources & bit) == 0U || (pass == 1U && (held & bit) == 0U))
                    continue;
                if ((!first && !r_write_text(writer, user_data, " ")) ||
                    !r_write_uint32(writer, user_data, source))
                    return false;
                first = false;
            }
            if (!r_write_text(writer, user_data, pass == 0U ? ") held=(" : "))"))
                return false;
        }
    }
    return r_write_text(writer, user_data, "))");
}

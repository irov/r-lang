/* Input origins are substituted independently for each returned projection. HIR places are
 * compiler-owned paths; their roots never escape as identities in an interface record. */
static bool r_body_projection_path_equal(const RBodyContext *body,
                                         RHirNodeId left_id,
                                         RHirNodeId right_id,
                                         uint32_t depth) {
    const RHirNode *left = r_body_hir_node(body, left_id);
    const RHirNode *right = r_body_hir_node(body, right_id);
    RHirNodeId left_base, right_base;
    if (left == NULL || right == NULL || depth > r_frontend_tree_depth_limit(body->frontend) ||
        left->kind != right->kind)
        return false;
    if (left->kind == R_HIR_PLACE)
        return r_semantic_value_type(body->frontend, left->type) ==
               r_semantic_value_type(body->frontend, right->type);
    if (left->kind == R_HIR_FIELD_PLACE) {
        if (left->aggregate_member != right->aggregate_member)
            return false;
    } else if (left->kind == R_HIR_INDEX_PLACE) {
        RHirNodeId li, ri;
        uint64_t lv, rv;
        if (!r_body_hir_child(body, left, 1U, &li) || !r_body_hir_child(body, right, 1U, &ri) ||
            !r_body_constant_index(body, li, &lv) || !r_body_constant_index(body, ri, &rv) ||
            lv != rv)
            return false;
    } else if (left->kind != R_HIR_DEREF_PLACE) {
        return false;
    }
    return r_body_hir_child(body, left, 0U, &left_base) &&
           r_body_hir_child(body, right, 0U, &right_base) &&
           r_body_projection_path_equal(body, left_base, right_base, depth + 1U);
}

static bool r_body_projection_state_complete(RBodyContext *body, uint32_t state, RTypeId type) {
    const RProjectedBorrowStateEntry *entries;
    size_t count;
    const uint32_t required = r_semantic_type_view_component_count(body->frontend, type, NULL);
    uint64_t covered = 0U;
    if (required == 0U)
        return true;
    if (required == UINT32_MAX ||
        !r_body_projected_borrow_state_entries(body, state, &entries, &count))
        return false;
    for (size_t index = 0U; index < count; ++index) {
        RHirNodeId root;
        if (!r_body_projected_borrow_place_root(body, entries[index].destination, &root) ||
            r_semantic_value_type(body->frontend, r_body_hir_node(body, root)->type) !=
                r_semantic_value_type(body->frontend, type))
            return false;
        bool duplicate = false;
        for (size_t previous = 0U; previous < index; ++previous)
            duplicate |= r_body_projection_path_equal(
                body, entries[previous].destination, entries[index].destination, 0U);
        if (!duplicate) {
            if (entries[index].component_count == UINT32_MAX)
                return false;
            covered += entries[index].component_count;
        }
    }
    return covered == required;
}

/* Copy only the suffix below a selected aggregate. Origins still refer to the original
 * input paths; only output paths become relative to the selected value's new root. */
static bool r_body_projection_suffix(RBodyContext *body,
                                     RHirNodeId path,
                                     RHirNodeId prefix,
                                     RHirNodeId root,
                                     uint32_t depth,
                                     RHirNodeId *output) {
    *output = R_HIR_NODE_ID_INVALID;
    if (depth > r_frontend_tree_depth_limit(body->frontend))
        return true;
    if (r_body_projection_path_equal(body, path, prefix, 0U)) {
        *output = root;
        return true;
    }
    const RHirNode *node = r_body_hir_node(body, path);
    if (node == NULL || (node->kind != R_HIR_FIELD_PLACE && node->kind != R_HIR_INDEX_PLACE &&
                         node->kind != R_HIR_DEREF_PLACE))
        return true;
    RHirNode copy = *node;
    RHirNodeId storage[2], base;
    if (!r_body_hir_child(body, &copy, 0U, &base) ||
        !r_body_projection_suffix(body, base, prefix, root, depth + 1U, &storage[0]))
        return false;
    if (storage[0] == R_HIR_NODE_ID_INVALID)
        return true;
    RHirVector children = {storage, copy.child_count, 2U};
    if (copy.child_count > 2U ||
        (copy.child_count == 2U && !r_body_hir_child(body, &copy, 1U, &storage[1])) ||
        !r_hir_append_node(body->frontend, copy, output) ||
        !r_hir_commit_children(body->frontend, *output, &children))
        return false;
    return true;
}

static bool r_body_select_value_projections(RBodyContext *body, RExpressionResult *value) {
    const RSemanticTypeKind kind = r_semantic_value_kind(body->frontend, value->type);
    if (r_semantic_type_view_component_count(body->frontend, value->type, NULL) == 0U) {
        value->projected_borrow_state = 0U;
        return true;
    }
    if (value->projected_borrow_state == 0U ||
        (kind != R_SEMANTIC_TYPE_STRUCT && kind != R_SEMANTIC_TYPE_ENUM &&
         kind != R_SEMANTIC_TYPE_FIXED_ARRAY && kind != R_SEMANTIC_TYPE_BORROW &&
         kind != R_SEMANTIC_TYPE_SLICE && kind != R_SEMANTIC_TYPE_STR &&
         kind != R_SEMANTIC_TYPE_PARAMETER) ||
        r_body_hir_node(body, value->node)->kind == R_HIR_PLACE)
        return true;
    const RProjectedBorrowStateEntry *entries;
    size_t count;
    RHirNodeId root;
    RProjectedBorrowStateEntryVector selected = {0};
    bool success = false;
    if (!r_body_projected_borrow_state_entries(
            body, value->projected_borrow_state, &entries, &count) ||
        !r_body_append_node(body,
                            R_HIR_PLACE,
                            r_body_hir_node(body, value->node)->span,
                            value->type,
                            value->symbol,
                            R_TOKEN_INVALID,
                            0U,
                            true,
                            NULL,
                            &root))
        return false;
    for (size_t index = 0U; index < count; ++index) {
        RProjectedBorrowStateEntry entry = entries[index];
        if (!r_body_projection_suffix(
                body, entry.destination, value->node, root, 0U, &entry.destination))
            goto cleanup;
        if (entry.destination != R_HIR_NODE_ID_INVALID &&
            !r_body_projected_borrow_entry_push(body, &selected, entry))
            goto cleanup;
    }
    success = r_body_append_projected_borrow_state(body, &selected, &value->projected_borrow_state);
cleanup:
    r_context_free(body->frontend, selected.items);
    return success;
}

/* Access weakening and view conversions preserve the referent, but their descriptor
 * type changes. Its output path must describe that descriptor rather than the input type. */
static bool r_body_normalize_value_projections(RBodyContext *body, RExpressionResult *value) {
    if (!value->valid || value->projected_borrow_state == 0U)
        return true;
    const uint32_t components =
        r_semantic_type_view_component_count(body->frontend, value->type, NULL);
    if (components == 0U) {
        value->projected_borrow_state = 0U;
        r_body_hir_node(body, value->node)->projected_borrow_state = 0U;
        return true;
    }
    const RSemanticTypeKind kind = r_semantic_value_kind(body->frontend, value->type);
    if ((kind != R_SEMANTIC_TYPE_BORROW && kind != R_SEMANTIC_TYPE_SLICE &&
         kind != R_SEMANTIC_TYPE_STR) ||
        r_body_projection_state_complete(body, value->projected_borrow_state, value->type))
        return true;
    const RHirNodeId source = r_body_direct_borrow_source_depth(body, value->node, 0U);
    RHirNodeId root;
    value->projected_borrow_state = 0U;
    if (source != R_HIR_NODE_ID_INVALID &&
        r_body_projected_borrow_place_root(body, source, &root)) {
        const RSymbolId symbol = r_body_hir_node(body, root)->symbol;
        RHirNodeId destination;
        if (!r_body_append_node(body,
                                R_HIR_PLACE,
                                r_body_hir_node(body, value->node)->span,
                                value->type,
                                symbol,
                                R_TOKEN_INVALID,
                                0U,
                                true,
                                NULL,
                                &destination))
            return false;
        RProjectedBorrowStateEntry entry = {destination, source, R_SYMBOL_ID_INVALID, 1U};
        RProjectedBorrowStateEntryVector entries = {&entry, 1U, 1U};
        if (!r_body_append_projected_borrow_state(body, &entries, &value->projected_borrow_state))
            return false;
    }
    r_body_hir_node(body, value->node)->projected_borrow_state = value->projected_borrow_state;
    return true;
}

/* A parameter's components initially refer to themselves. This allows a returned nested
 * aggregate to retain its input field paths before a caller substitutes concrete owners. */
static bool r_body_identity_borrow_components(RBodyContext *body,
                                              RHirNodeId place,
                                              uint32_t depth,
                                              RProjectedBorrowStateEntryVector *entries) {
    const RHirNode base = *r_body_hir_node(body, place);
    const RSemanticType *type =
        r_semantic_type(body->frontend, r_semantic_value_type(body->frontend, base.type));
    const uint32_t components =
        r_semantic_type_view_component_count(body->frontend, base.type, NULL);
    if (components == 0U || depth > r_frontend_tree_depth_limit(body->frontend))
        return true;
    if (type->kind == R_SEMANTIC_TYPE_STRUCT) {
        const RSemanticAggregate aggregate =
            *r_semantic_aggregate_for_type(body->frontend, base.type);
        for (uint32_t index = 0U; index < aggregate.field_count; ++index) {
            const uint32_t member = aggregate.first_field + index;
            const RTypeId field_type = body->frontend->semantic_fields[member].type;
            if (r_semantic_type_view_component_count(body->frontend, field_type, NULL) == 0U)
                continue;
            RHirNode field = base;
            RHirNodeId child;
            field.kind = R_HIR_FIELD_PLACE;
            field.type = field_type;
            field.aggregate_member = member + 1U;
            RHirVector children = {&place, 1U, 1U};
            if (!r_hir_append_node(body->frontend, field, &child) ||
                !r_hir_commit_children(body->frontend, child, &children) ||
                !r_body_identity_borrow_components(body, child, depth + 1U, entries))
                return false;
        }
        return true;
    }
    if (type->kind == R_SEMANTIC_TYPE_FIXED_ARRAY && type->second == R_TYPE_ID_INVALID) {
        const uint64_t length = type->length;
        const RTypeId element_type = type->base;
        RTypeId usize_type;
        if (!r_semantic_type_from_token(body->frontend, R_TOKEN_KW_USIZE, &usize_type))
            return false;
        for (uint64_t index = 0U; index < length; ++index) {
            RHirNodeId literal, child;
            if (!r_body_append_node(body,
                                    R_HIR_LITERAL,
                                    base.span,
                                    usize_type,
                                    R_SYMBOL_ID_INVALID,
                                    R_TOKEN_INTEGER_LITERAL,
                                    index,
                                    false,
                                    NULL,
                                    &literal))
                return false;
            RHirNode element = base;
            element.kind = R_HIR_INDEX_PLACE;
            element.type = element_type;
            element.integer_value = length;
            RHirNodeId storage[] = {place, literal};
            RHirVector children = {storage, 2U, 2U};
            if (!r_hir_append_node(body->frontend, element, &child) ||
                !r_hir_commit_children(body->frontend, child, &children) ||
                !r_body_identity_borrow_components(body, child, depth + 1U, entries))
                return false;
        }
        return true;
    }
    RProjectedBorrowStateEntry entry = {place, place, R_SYMBOL_ID_INVALID, components};
    return r_body_projected_borrow_entry_push(body, entries, entry);
}

static bool r_body_seed_parameter_projections(RBodyContext *body, RSymbolId symbol) {
    const RSemanticSymbol snapshot = *r_body_symbol(body, symbol);
    if (r_semantic_type_view_component_count(body->frontend, snapshot.type, NULL) == 0U ||
        r_semantic_value_kind(body->frontend, snapshot.type) == R_SEMANTIC_TYPE_PARAMETER)
        return true;
    RHirNodeId root;
    RProjectedBorrowStateEntryVector entries = {0};
    uint32_t state;
    bool success = false;
    if (!r_body_append_node(body,
                            R_HIR_PLACE,
                            snapshot.name_span,
                            snapshot.type,
                            symbol,
                            R_TOKEN_INVALID,
                            0U,
                            true,
                            NULL,
                            &root))
        return false;
    /* A holder passed by value carries its content region (R-BORROW-0018). */
    r_body_hir_node(body, root)->borrow_origin =
        snapshot.borrow_origin != R_SYMBOL_ID_INVALID ? snapshot.borrow_origin : symbol;
    if (!r_body_identity_borrow_components(body, root, 0U, &entries) ||
        !r_body_append_projected_borrow_state(body, &entries, &state))
        goto cleanup;
    r_body_symbol_mut(body, symbol)->projected_borrow_state = state;
    success = true;
cleanup:
    r_context_free(body->frontend, entries.items);
    return success;
}

static bool r_body_record_return_projections(RBodyContext *body, const RExpressionResult *value) {
    if (!value->valid || body->function_symbol == R_SYMBOL_ID_INVALID)
        return true;
    RExpressionResult output = *value;
    const RSemanticTypeKind kind = r_semantic_value_kind(body->frontend, value->type);
    if ((kind == R_SEMANTIC_TYPE_BORROW || kind == R_SEMANTIC_TYPE_SLICE ||
         kind == R_SEMANTIC_TYPE_STR) &&
        !r_body_projection_state_complete(body, output.projected_borrow_state, output.type)) {
        const RHirNodeId source = r_body_direct_borrow_source_depth(body, value->node, 0U);
        RHirNodeId root;
        if (source != R_HIR_NODE_ID_INVALID &&
            r_body_projected_borrow_place_root(body, source, &root)) {
            const RSymbolId symbol = r_body_hir_node(body, root)->symbol;
            RHirNodeId destination;
            if (!r_body_append_node(body,
                                    R_HIR_PLACE,
                                    r_body_hir_node(body, value->node)->span,
                                    value->type,
                                    symbol,
                                    R_TOKEN_INVALID,
                                    0U,
                                    true,
                                    NULL,
                                    &destination))
                return false;
            RProjectedBorrowStateEntry entry = {destination, source, R_SYMBOL_ID_INVALID, 1U};
            RProjectedBorrowStateEntryVector entries = {&entry, 1U, 1U};
            if (!r_body_append_projected_borrow_state(
                    body, &entries, &output.projected_borrow_state))
                return false;
        }
    }
    value = &output;
    RSemanticSymbol *function = r_body_symbol_mut(body, body->function_symbol);
    const bool complete =
        r_body_projection_state_complete(body, value->projected_borrow_state, value->type);
    if (!function->return_projections_seen) {
        function->return_projections_complete = complete;
        function->return_projected_borrow_state = value->projected_borrow_state;
        function->return_projections_seen = true;
        return true;
    }
    function->return_projections_complete &= complete;
    uint32_t merged;
    if (!r_body_merge_projected_borrow_states(
            body, function->return_projected_borrow_state, value->projected_borrow_state, &merged))
        return false;
    r_body_symbol_mut(body, body->function_symbol)->return_projected_borrow_state = merged;
    return true;
}

/* Parameter positions are declaration order, independent of local or HIR numbering. */
static uint32_t r_body_projection_parameter(const RBodyContext *body,
                                            const RSemanticSymbol *function,
                                            RSymbolId symbol) {
    uint32_t parameter = 0U;
    const RSymbolId function_id = (RSymbolId)(function - body->frontend->semantic_symbols) + 1U;
    for (size_t index = 0U; index < body->frontend->semantic_symbol_count; ++index) {
        const RSemanticSymbol *candidate = &body->frontend->semantic_symbols[index];
        if (candidate->kind == R_SEMANTIC_SYMBOL_PARAMETER && candidate->parent == function_id) {
            if (index + 1U == symbol)
                return parameter;
            ++parameter;
        }
    }
    return UINT32_MAX;
}

static bool r_body_refine_projected_value(RBodyContext *, RExpressionResult *);
static bool r_body_result_held_content(RBodyContext *body, RExpressionResult *result);
static bool r_body_call_input_is_held(const RBodyContext *body,
                                      const RSemanticSymbol *function,
                                      uint32_t index,
                                      uint64_t held_low,
                                      uint64_t held_high);
static uint64_t r_body_call_result_held_low(const RBodyContext *body,
                                            const RSemanticSymbol *function);

static bool r_body_apply_return_projections(RBodyContext *body,
                                            RSymbolId function_id,
                                            const RSemanticSymbol *snapshot,
                                            const RHirVector *arguments,
                                            uint64_t held_low,
                                            uint64_t held_high,
                                            RExpressionResult *result) {
    if (!snapshot->return_projections_complete || snapshot->return_projected_borrow_state == 0U)
        return true;
    const RProjectedBorrowStateEntry *entries;
    size_t count;
    RProjectedBorrowStateEntryVector mapped = {0};
    bool success = false;
    if (!r_body_projected_borrow_state_entries(
            body, snapshot->return_projected_borrow_state, &entries, &count))
        return false;
    for (size_t index = 0U; index < count; ++index) {
        RProjectedBorrowStateEntry entry = entries[index];
        RHirNodeId root;
        if (entry.source == R_HIR_NODE_ID_INVALID ||
            !r_body_projected_borrow_place_root(body, entry.source, &root))
            goto conservative;
        const RHirNode root_node = *r_body_hir_node(body, root);
        uint32_t parameter =
            r_body_projection_parameter(body, r_body_symbol(body, function_id), root_node.symbol);
        if (parameter == UINT32_MAX && !root_node.borrow_origin_multiple)
            parameter = r_body_projection_parameter(
                body, r_body_symbol(body, function_id), root_node.borrow_origin);
        if (parameter >= arguments->count)
            goto conservative;
        RHirNodeId source =
            r_body_direct_borrow_source_depth(body, arguments->items[parameter], 0U);
        if (source == R_HIR_NODE_ID_INVALID)
            source = arguments->items[parameter];
        const RHirNode argument = *r_body_hir_node(body, source);
        if (argument.symbol == R_SYMBOL_ID_INVALID)
            goto conservative;
        if (!r_body_rebase_projected_borrow_place(
                body, entry.source, root_node.symbol, source, argument.symbol, 0U, &entry.source))
            goto cleanup;
        const RHirNode projected_node = *r_body_hir_node(body, entry.source);
        const RHirNode actual_value = *r_body_hir_node(body, arguments->items[parameter]);
        RExpressionResult projected = {0};
        projected.node = entry.source;
        projected.type = projected_node.type;
        projected.symbol = projected_node.symbol;
        projected.borrow_origin = actual_value.borrow_origin;
        projected.borrow_origin_set = actual_value.borrow_origin_set;
        projected.borrow_origin_multiple = actual_value.borrow_origin_multiple;
        projected.borrow_referent = actual_value.borrow_referent;
        projected.projected_borrow_state = actual_value.projected_borrow_state;
        projected.valid = true;
        /* R-BORROW-0009: a component taken from what a held input holds lies where that input's
           storage points; a component the argument's own projections name refines it below. */
        if (r_body_call_input_is_held(body, snapshot, parameter, held_low, held_high) &&
            !r_body_result_held_content(body, &projected))
            goto cleanup;
        if (!r_body_refine_projected_value(body, &projected))
            goto cleanup;
        /* This is a provenance path, not another evaluation of the argument. Keep its
           annotation separate from the already emitted argument node. */
        RHirNode annotated = *r_body_hir_node(body, entry.source);
        annotated.borrow_origin = projected.borrow_origin;
        annotated.borrow_origin_set = projected.borrow_origin_set;
        annotated.borrow_origin_multiple = projected.borrow_origin_multiple;
        annotated.borrow_referent = projected.borrow_referent;
        annotated.projected_borrow_state = 0U;
        if (!r_hir_append_node(body->frontend, annotated, &entry.source))
            goto cleanup;
        entry.rebase_origin = R_SYMBOL_ID_INVALID;
        if (!r_body_projected_borrow_entry_push(body, &mapped, entry))
            goto cleanup;
    }
    RSymbolId origin = R_SYMBOL_ID_INVALID;
    uint32_t origin_set = 0U;
    bool multiple = false;
    uint64_t referent = 0U;
    for (size_t index = 0U; index < mapped.count; ++index) {
        const RHirNode *source = r_body_hir_node(body, mapped.items[index].source);
        if (!r_semantic_merge_borrow_origins(body->frontend,
                                             origin,
                                             origin_set,
                                             multiple,
                                             source->borrow_origin,
                                             source->borrow_origin_set,
                                             source->borrow_origin_multiple,
                                             &origin,
                                             &origin_set,
                                             &multiple))
            goto cleanup;
        referent = index == 0U
                       ? source->borrow_referent
                       : r_semantic_merge_borrow_referents(referent, source->borrow_referent);
    }
    result->borrow_origin = origin;
    result->borrow_origin_set = origin_set;
    result->borrow_origin_multiple = multiple;
    result->borrow_referent = referent;
    success = r_body_append_projected_borrow_state(body, &mapped, &result->projected_borrow_state);
    goto cleanup;
conservative:
    /* A missing path retains the ordinary input union. It never removes an origin. */
    success = body->frontend->resource_status == R_FRONTEND_OK;
cleanup:
    r_context_free(body->frontend, mapped.items);
    return success;
}

static bool r_body_refine_projected_value(RBodyContext *body, RExpressionResult *value) {
    const RSemanticTypeKind kind = r_semantic_value_kind(body->frontend, value->type);
    if (value->projected_borrow_state == 0U ||
        (kind != R_SEMANTIC_TYPE_BORROW && kind != R_SEMANTIC_TYPE_SLICE &&
         kind != R_SEMANTIC_TYPE_STR))
        return true;
    const RProjectedBorrowStateEntry *entries;
    size_t count;
    RExpressionResult refined = *value;
    bool found = false;
    refined.borrow_origin = R_SYMBOL_ID_INVALID;
    refined.borrow_origin_set = 0U;
    refined.borrow_origin_multiple = false;
    refined.borrow_referent = 0U;
    if (!r_body_projected_borrow_state_entries(
            body, value->projected_borrow_state, &entries, &count))
        return false;
    for (size_t index = 0U; index < count; ++index) {
        if (!r_body_projection_path_equal(body, entries[index].destination, value->node, 0U))
            continue;
        const RHirNode *source = r_body_hir_node(body, entries[index].source);
        if (source == NULL)
            return true;
        if (!r_semantic_merge_borrow_origins(body->frontend,
                                             refined.borrow_origin,
                                             refined.borrow_origin_set,
                                             refined.borrow_origin_multiple,
                                             source->borrow_origin,
                                             source->borrow_origin_set,
                                             source->borrow_origin_multiple,
                                             &refined.borrow_origin,
                                             &refined.borrow_origin_set,
                                             &refined.borrow_origin_multiple))
            return false;
        refined.borrow_referent = found ? r_semantic_merge_borrow_referents(refined.borrow_referent,
                                                                            source->borrow_referent)
                                        : source->borrow_referent;
        found = true;
    }
    if (found) {
        *value = refined;
        RHirNode *node = r_body_hir_node(body, value->node);
        node->borrow_origin = value->borrow_origin;
        node->borrow_origin_set = value->borrow_origin_set;
        node->borrow_origin_multiple = value->borrow_origin_multiple;
        node->borrow_referent = value->borrow_referent;
    }
    return true;
}

static bool r_body_append_array_borrow_component(RBodyContext *body,
                                                 RTypeId array_type,
                                                 uint64_t index,
                                                 const RExpressionResult *value,
                                                 RProjectedBorrowStateEntryVector *output) {
    const uint32_t components =
        r_semantic_type_view_component_count(body->frontend, value->type, NULL);
    if (components == 0U)
        return true;
    const RProjectedBorrowStateEntry *entries;
    size_t count;
    RProjectedBorrowStateEntry direct = {0};
    if (!r_body_projected_borrow_state_entries(
            body, value->projected_borrow_state, &entries, &count))
        return false;
    const RSourceSpan span = r_body_hir_node(body, value->node)->span;
    if (count == 0U) {
        direct.source = r_body_direct_borrow_source_depth(body, value->node, 0U);
        if (direct.source == R_HIR_NODE_ID_INVALID)
            return true;
        const RHirNode *source = r_body_hir_node(body, direct.source);
        if ((source->symbol == R_SYMBOL_ID_INVALID) ||
            r_body_symbol(body, source->symbol)->is_hidden_local)
            return true;
        if (!r_body_append_node(body,
                                R_HIR_PLACE,
                                span,
                                value->type,
                                source->symbol,
                                R_TOKEN_INVALID,
                                0U,
                                true,
                                NULL,
                                &direct.destination))
            return false;
        direct.component_count = components;
        entries = &direct;
        count = 1U;
    }
    for (size_t entry_index = 0U; entry_index < count; ++entry_index) {
        RProjectedBorrowStateEntry entry = entries[entry_index];
        RHirNodeId root;
        if (!r_body_projected_borrow_place_root(body, entry.destination, &root))
            return false;
        const RSymbolId symbol = r_body_hir_node(body, root)->symbol;
        RHirNodeId array, element, literal;
        RTypeId usize_type;
        if (!r_semantic_type_from_token(body->frontend, R_TOKEN_KW_USIZE, &usize_type) ||
            !r_body_append_node(body,
                                R_HIR_PLACE,
                                span,
                                array_type,
                                symbol,
                                R_TOKEN_INVALID,
                                0U,
                                true,
                                NULL,
                                &array) ||
            !r_body_append_node(body,
                                R_HIR_LITERAL,
                                span,
                                usize_type,
                                R_SYMBOL_ID_INVALID,
                                R_TOKEN_INTEGER_LITERAL,
                                index,
                                false,
                                NULL,
                                &literal))
            return false;
        RHirNodeId storage[2] = {array, literal};
        RHirVector children = {storage, 2U, 2U};
        if (!r_body_append_node(body,
                                R_HIR_INDEX_PLACE,
                                span,
                                value->type,
                                symbol,
                                R_TOKEN_INVALID,
                                r_semantic_type(body->frontend, array_type)->length,
                                true,
                                &children,
                                &element) ||
            !r_body_rebase_projected_borrow_place(
                body, entry.destination, symbol, element, symbol, 0U, &entry.destination) ||
            (entry.rebase_origin != R_SYMBOL_ID_INVALID && entry.source != R_HIR_NODE_ID_INVALID &&
             !r_body_rebase_projected_borrow_place(
                 body, entry.source, symbol, element, symbol, 0U, &entry.source)))
            return false;
        if (!r_body_projected_borrow_entry_push(body, output, entry))
            return false;
    }
    return true;
}

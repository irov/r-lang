/* R-FUNC-0022: out has a private callee value and a caller staging slot. Neither
   the caller destination nor its old owner is touched by an exceptional return. */
static bool r_out_type(const RFrontendContext *context, RTypeId id) {
    const RSemanticType *type = r_semantic_type(context, id);
    return type != NULL && type->kind == R_SEMANTIC_TYPE_BORROW &&
           (type->flags & R_SEMANTIC_TYPE_FLAG_OUT) != 0U;
}

static bool r_out_function(const RFrontendContext *context, const RSemanticSymbol *function) {
    for (uint32_t i = 0U; i < function->parameter_count; ++i)
        if (r_out_type(context,
                       context->semantic_parameter_types[function->first_parameter_type + i]))
            return true;
    return false;
}

static bool r_out_local(RBodyContext *body,
                        RSemanticSymbol symbol,
                        RHirVector *prefix,
                        RSymbolId *id,
                        RHirNodeId *place) {
    symbol.kind = R_SEMANTIC_SYMBOL_LOCAL;
    symbol.parent = body->function_symbol;
    symbol.module_source = body->source;
    symbol.object_state = R_SEMANTIC_OBJECT_STATE_MOVED;
    symbol.is_out_storage = true;
    if (body->format_temporary_count == UINT32_MAX) {
        body->frontend->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
        return false;
    }
    symbol.integer_value = body->format_temporary_count++;
    symbol.signature_supported = true;
    if (symbol.name_intern_id == 0U &&
        !r_intern_bytes(body->frontend, (const uint8_t *)"$out_local", 10U, &symbol.name_intern_id))
        return false;
    RHirNodeId local;
    if (!r_semantic_append_symbol(body->frontend, symbol, id) ||
        !r_body_append_node(body,
                            R_HIR_LOCAL,
                            symbol.name_span,
                            symbol.type,
                            *id,
                            R_TOKEN_INVALID,
                            0U,
                            false,
                            NULL,
                            &local) ||
        !r_hir_vector_push(body->frontend, prefix, local) ||
        !r_symbol_vector_push(body->frontend, &body->active_symbols, *id) ||
        !r_body_append_node(body,
                            R_HIR_PLACE,
                            symbol.name_span,
                            symbol.type,
                            *id,
                            R_TOKEN_INVALID,
                            0U,
                            true,
                            NULL,
                            place))
        return false;
    body->frontend->semantic_symbols[*id - 1U].hir_node = local;
    return true;
}

static bool r_out_parameter_local(RBodyContext *body, RSymbolId parameter, RHirVector *prefix) {
    RSemanticSymbol local = *r_body_symbol(body, parameter);
    const RSemanticType *output = r_semantic_type(body->frontend, local.type);
    local.type = output->base;
    local.out_parameter = parameter;
    local.borrow_origin = 0U;
    local.borrow_origin_set = 0U;
    local.projected_borrow_state = 0U;
    local.borrow_origin_multiple = false;
    RSemanticSymbol *slot = r_body_symbol_mut(body, parameter);
    slot->is_hidden_local = true;
    slot->is_out_storage = true;
    if (body->format_temporary_count == UINT32_MAX) {
        body->frontend->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
        return false;
    }
    slot->integer_value = body->format_temporary_count++;
    if (!r_intern_bytes(body->frontend, (const uint8_t *)"$out_slot", 9U, &slot->name_intern_id))
        return false;
    RSymbolId id;
    RHirNodeId place;
    return r_out_local(body, local, prefix, &id, &place);
}

static bool r_out_load(RBodyContext *body, RHirNodeId place, bool consume, RHirNodeId *value) {
    const RHirNode source = *r_body_hir_node(body, place);
    const bool copy = r_semantic_type_is_copy(body->frontend, source.type);
    RHirVector children = {&place, 1U, 1U};
    return r_body_append_node(body,
                              consume && !copy ? R_HIR_MOVE : R_HIR_LOAD,
                              source.span,
                              source.type,
                              source.symbol,
                              consume && !copy ? R_TOKEN_KW_MOVE : R_TOKEN_INVALID,
                              0U,
                              false,
                              &children,
                              value);
}

static bool r_out_assignment(RBodyContext *body,
                             RSourceSpan span,
                             RHirNodeId place,
                             RHirNodeId value,
                             bool replaces,
                             bool publication,
                             RHirVector *statements) {
    RTypeId void_type;
    RHirNodeId assignment, statement;
    RHirNodeId storage[] = {place, value};
    RHirVector children = {storage, 2U, 2U};
    const RSymbolId destination = r_body_hir_node(body, place)->symbol;
    if (!r_semantic_type_from_token(body->frontend, R_TOKEN_KW_VOID, &void_type) ||
        !r_body_append_node(body,
                            R_HIR_ASSIGN,
                            span,
                            void_type,
                            destination,
                            R_TOKEN_EQUAL,
                            0U,
                            false,
                            &children,
                            &assignment))
        return false;
    r_body_hir_node(body, assignment)->replaces_initialized = replaces;
    r_body_hir_node(body, assignment)->out_argument = !publication;
    r_body_hir_node(body, assignment)->out_destination =
        r_body_hir_node(body, place)->out_destination;
    r_body_hir_node(body, assignment)->out_place = r_body_hir_node(body, place)->out_place;
    children.items = &assignment;
    children.count = children.capacity = 1U;
    if (!r_body_append_node(body,
                            R_HIR_EXPRESSION_STATEMENT,
                            span,
                            void_type,
                            0U,
                            R_TOKEN_INVALID,
                            0U,
                            false,
                            &children,
                            &statement))
        return false;
    r_body_hir_node(body, statement)->out_commit = publication;
    if (publication)
        r_body_hir_node(body, statement)->integer_value = body->active_finally_scope_bases.count;
    return r_hir_vector_push(body->frontend, statements, statement);
}

static bool r_out_return(RBodyContext *body, RSourceSpan span, RHirVector *statements) {
    for (size_t i = 0U; i < body->active_symbols.count; ++i) {
        const RSymbolId id = body->active_symbols.items[i];
        const RSemanticSymbol local = *r_body_symbol(body, id);
        if (local.out_parameter == 0U)
            continue;
        if (local.object_state != R_SEMANTIC_OBJECT_STATE_INITIALIZED &&
            body->active_finally_scope_bases.count == 0U) {
            /* After an error the initialization state of this path is not reliable. */
            if (!body->poisoned &&
                !r_body_diagnostic(body,
                                   "R-DIAG-FLOW-001",
                                   "R-FUNC-0022",
                                   "every out parameter shall be initialized on successful return",
                                   span))
                return false;
            continue;
        }
        RHirNodeId source, value, slot, address, destination;
        const RTypeId slot_type = r_body_symbol(body, local.out_parameter)->type;
        if (!r_body_append_node(body,
                                R_HIR_PLACE,
                                span,
                                local.type,
                                id,
                                R_TOKEN_INVALID,
                                0U,
                                true,
                                NULL,
                                &source) ||
            !r_out_load(body, source, true, &value) ||
            !r_body_append_node(body,
                                R_HIR_PLACE,
                                span,
                                slot_type,
                                local.out_parameter,
                                R_TOKEN_INVALID,
                                0U,
                                true,
                                NULL,
                                &slot) ||
            !r_out_load(body, slot, false, &address))
            return false;
        RHirVector children = {&address, 1U, 1U};
        if (!r_body_append_node(body,
                                R_HIR_DEREF_PLACE,
                                span,
                                local.type,
                                local.out_parameter,
                                R_TOKEN_STAR,
                                0U,
                                true,
                                &children,
                                &destination))
            return false;
        r_body_hir_node(body, destination)->auxiliary_type = slot_type;
        if (!r_out_assignment(body, span, destination, value, false, true, statements))
            return false;
    }
    return true;
}

static bool r_out_lower_argument(RBodyContext *body, RAstRef argument, RExpressionResult *result) {
    RAstRef expression;
    RAstNodeView view;
    RExpressionResult place;
    RTypeId type;
    if (!r_ast_ref_view(body->frontend, argument, &view) ||
        !r_body_ast_single_node_child(body->frontend, argument, &expression) ||
        !r_body_lower_expression(body, expression, 0U, &place))
        return false;
    if (!place.valid)
        return true;
    if (!place.is_place)
        return r_body_diagnostic(body,
                                 "R-DIAG-TYPE-001",
                                 "R-FUNC-0022",
                                 "out argument requires a mutable destination place",
                                 view.span);
    /* Reserve the destination's storage, not the provenance of its previous contents. */
    if (r_body_hir_node(body, place.node)->kind == R_HIR_PLACE) {
        place.borrow_origin = place.symbol;
        place.borrow_origin_set = 0U;
        place.borrow_origin_multiple = false;
    }
    if (!r_semantic_intern_derived_type(body->frontend,
                                        R_SEMANTIC_TYPE_BORROW,
                                        r_semantic_value_type(body->frontend, place.type),
                                        0U,
                                        0U,
                                        R_SEMANTIC_TYPE_FLAG_OUT,
                                        &type) ||
        !r_body_form_safe_borrow(body, view.span, type, &place, result))
        return false;
    if (result->valid)
        r_body_hir_node(body, result->node)->out_argument = true;
    return true;
}

static RHirNodeId
r_out_unstage_argument(const RFrontendContext *context, RHirNodeId id, uint32_t depth) {
    if (id == 0U || depth > r_frontend_tree_depth_limit(context))
        return 0U;
    const RHirNode *node = &context->hir_nodes[id - 1U];
    if ((node->kind == R_HIR_LOAD || node->kind == R_HIR_MOVE) && node->symbol != 0U) {
        const RSemanticSymbol *local = &context->semantic_symbols[node->symbol - 1U];
        if (local->is_hidden_local && local->hir_node != 0U)
            return r_out_unstage_argument(context, local->hir_node, depth + 1U);
    }
    if (node->kind == R_HIR_LOCAL && node->child_count == 1U)
        return r_out_unstage_argument(
            context, context->hir_children[node->first_child], depth + 1U);
    return id;
}

static RHirNodeId r_out_source(const RFrontendContext *context, RHirNodeId id, uint32_t depth) {
    id = r_out_unstage_argument(context, id, depth);
    if (id == 0U)
        return 0U;
    const RHirNode *node = &context->hir_nodes[id - 1U];
    return node->out_argument && node->kind == R_HIR_BORROW
               ? context->hir_children[node->first_child]
               : 0U;
}

static bool r_out_prepare_call(RBodyContext *body,
                               const RSemanticSymbol *function,
                               bool named_callee,
                               RHirVector *arguments,
                               RHirVector *prefix,
                               RHirVector *suffix) {
    for (uint32_t i = 0U; i < arguments->count && i < function->parameter_count; ++i) {
        const RTypeId parameter =
            body->frontend->semantic_parameter_types[function->first_parameter_type + i];
        if (!r_out_type(body->frontend, parameter))
            continue;
        RHirNodeId argument = arguments->items[i];
        const RHirNodeId original = r_out_source(body->frontend, argument, 0U);
        if (original == 0U)
            return r_body_diagnostic(body,
                                     "R-DIAG-TYPE-001",
                                     "R-FUNC-0022",
                                     "an out parameter requires an explicit out argument",
                                     r_body_hir_node(body, argument)->span);
        const RHirNode source = *r_body_hir_node(body, original);
        const RSemanticSymbol destination_symbol = *r_body_symbol(body, source.symbol);
        const RTypeId value_type = r_semantic_type(body->frontend, parameter)->base;
        RSemanticSymbol temporary = {0};
        temporary.type = value_type;
        temporary.name_span = source.span;
        temporary.scope_start = source.span.end;
        temporary.is_hidden_local = true;
        RSymbolId temporary_id;
        RHirNodeId place, value, destination;
        if (!r_out_local(body, temporary, prefix, &temporary_id, &place) ||
            !r_out_load(body, place, true, &value))
            return false;
        RHirVector address = {&argument, 1U, 1U};
        if (!r_body_append_node(body,
                                R_HIR_DEREF_PLACE,
                                source.span,
                                value_type,
                                r_body_hir_node(body, argument)->symbol,
                                R_TOKEN_STAR,
                                0U,
                                true,
                                &address,
                                &destination))
            return false;
        r_body_hir_node(body, destination)->auxiliary_type = parameter;
        r_body_hir_node(body, destination)->out_destination = source.symbol;
        r_body_hir_node(body, destination)->out_place = original;
        if (!r_out_assignment(body,
                              source.span,
                              destination,
                              value,
                              source.kind != R_HIR_PLACE || destination_symbol.object_state ==
                                                                R_SEMANTIC_OBJECT_STATE_INITIALIZED,
                              false,
                              suffix))
            return false;
        if (named_callee && function->is_discardable) {
            const RHirNode *statement = r_body_hir_node(body, suffix->items[suffix->count - 1U]);
            r_body_hir_node(body, body->frontend->hir_children[statement->first_child])
                ->out_argument = false;
        }
        /* Retain the original place for state/use tracking; its address has already been
           evaluated once in the staged argument. */
        r_body_hir_node(body, destination)->out_argument = source.kind == R_HIR_PLACE;
        RHirVector slot = {&place, 1U, 1U};
        if (!r_body_append_node(body,
                                R_HIR_BORROW,
                                source.span,
                                parameter,
                                temporary_id,
                                R_TOKEN_AMP,
                                0U,
                                false,
                                &slot,
                                &arguments->items[i]))
            return false;
    }
    return true;
}

static void r_out_commit_call_state(RBodyContext *body, const RHirVector *suffix) {
    for (size_t i = 0U; i < suffix->count; ++i) {
        const RHirNode *statement = r_body_hir_node(body, suffix->items[i]);
        if (statement->kind != R_HIR_EXPRESSION_STATEMENT || statement->child_count != 1U)
            continue;
        const RHirNode *assignment =
            r_body_hir_node(body, body->frontend->hir_children[statement->first_child]);
        if (assignment->kind != R_HIR_ASSIGN || assignment->out_place == 0U)
            continue;
        const RHirNode *destination =
            r_body_hir_node(body, body->frontend->hir_children[assignment->first_child]);
        if (destination->out_argument) {
            RSemanticSymbol *symbol = r_body_symbol_mut(body, destination->out_destination);
            symbol->object_state = R_SEMANTIC_OBJECT_STATE_INITIALIZED;
            symbol->proven_non_null = false;
        }
    }
}

static bool r_out_validate_signatures(RFrontendContext *context) {
    for (size_t i = 0U; i < context->semantic_symbol_count; ++i) {
        RSemanticSymbol *function = &context->semantic_symbols[i];
        if (function->kind != R_SEMANTIC_SYMBOL_FUNCTION || !r_out_function(context, function))
            continue;
        if (function->is_async || function->is_extern_c || function->is_import ||
            function->is_callback) {
            if (!r_semantic_add_diagnostic(context,
                                           "R-DIAG-TYPE-001",
                                           "R-FUNC-0022",
                                           "out requires a synchronous R function; async and C "
                                           "boundaries do not carry out slots",
                                           function->name_span))
                return false;
            function->poisoned = true;
            function->signature_supported = false;
        }
    }
    return true;
}

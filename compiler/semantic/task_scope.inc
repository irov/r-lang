/* Compiler-owned scopes share the existing checked-transfer/finally machinery. */
static bool r_body_lower_try_statement(RBodyContext *, RAstRef, RHirNodeId *, bool *);

static bool r_scope_finally(RBodyContext *body, RSourceSpan span, RHirNodeId *result) {
    RTypeId void_type;
    RHirNodeId wait, close, block;
    if (!r_semantic_type_from_token(body->frontend, R_TOKEN_KW_VOID, &void_type) ||
        !r_body_append_node(body,
                            R_HIR_TASK_SCOPE_WAIT,
                            span,
                            void_type,
                            body->current_task_scope,
                            R_TOKEN_KW_FINALLY,
                            0U,
                            false,
                            NULL,
                            &wait) ||
        !r_body_append_node(body,
                            R_HIR_TASK_SCOPE_CLOSE,
                            span,
                            void_type,
                            body->current_task_scope,
                            R_TOKEN_INVALID,
                            0U,
                            false,
                            NULL,
                            &close))
        return false;
    RHirNodeId statements[] = {wait, close};
    RHirVector children = {statements, 2U, 2U};
    if (!r_body_append_node(
            body, R_HIR_BLOCK, span, void_type, 0U, R_TOKEN_INVALID, 0U, false, &children, &block))
        return false;
    children = (RHirVector){&block, 1U, 1U};
    return r_body_append_node(
        body, R_HIR_FINALLY, span, void_type, 0U, R_TOKEN_KW_FINALLY, 0U, false, &children, result);
}

static bool
r_scope_lower(RBodyContext *body, RAstRef statement, RHirNodeId *result, bool *always_returns) {
    RAstNodeView view;
    RAstRef capacity_ref;
    RAstTokenView name = {0};
    RExpressionResult capacity;
    RBodyConstantValue constant;
    RSemanticSymbol group = {0};
    RTypeId usize_type, void_type;
    uint64_t bound;
    if (!r_ast_ref_view(body->frontend, statement, &view) ||
        !r_ast_find_direct_child(body->frontend, statement, R_SYNTAX_EXPRESSION, &capacity_ref))
        return r_body_invariant(body);
    *result = R_HIR_NODE_ID_INVALID;
    *always_returns = false;
    if (!body->is_async || body->finally_depth != 0U)
        return r_body_diagnostic(body,
                                 "R-DIAG-ASYNC-001",
                                 "R-STMT-0017",
                                 "task_scope requires an async body outside finally",
                                 view.span);
    for (uint32_t i = 0U; i < view.child_count; ++i) {
        RAstRef child;
        RAstTokenView token;
        if (r_ast_ref_child(body->frontend, statement, i, &child) &&
            r_ast_ref_token(body->frontend, child, &token) && token.kind == R_TOKEN_IDENTIFIER)
            name = token;
    }
    if (!r_semantic_type_from_token(body->frontend, R_TOKEN_KW_USIZE, &usize_type) ||
        !r_semantic_type_from_token(body->frontend, R_TOKEN_KW_VOID, &void_type) ||
        !r_body_lower_value(body, capacity_ref, usize_type, &capacity))
        return false;
    if (!capacity.valid)
        return true;
    /* L25.2: a capacity over the constant generic parameters of the function is a constant of
       each instance, checked when the instance is closed (r_consteval_group_capacity). */
    RHirNodeId dependent_capacity = R_HIR_NODE_ID_INVALID;
    if (!r_body_constant_scalar(body, capacity.node, &constant) ||
        !r_body_constant_nonnegative_integer(&constant, &bound) || bound == 0U || bound > 65536U) {
        bool dependent = false;
        if (!r_consteval_dependent_initializer(body->frontend, capacity.node, &dependent))
            return false;
        if (!dependent)
            return r_body_diagnostic(body,
                                     "R-DIAG-ASYNC-001",
                                     "R-STMT-0017",
                                     "task group capacity shall be a constant in 1..65536",
                                     view.span);
        r_consteval_mark_reads(body->frontend, capacity.node, 0U);
        dependent_capacity = capacity.node;
        bound = 0U;
    }
    group.kind = R_SEMANTIC_SYMBOL_LOCAL;
    group.name_span = name.span;
    group.name_intern_id = name.intern_id;
    group.module_source = body->source;
    group.scope_start = name.span.start;
    group.type = void_type;
    group.parent = body->function_symbol;
    group.object_state = R_SEMANTIC_OBJECT_STATE_INITIALIZED;
    group.signature_supported = true;
    group.is_task_scope = true;
    const size_t scope_base = body->active_symbols.count;
    const RSymbolId previous = body->current_task_scope;
    RSymbolId symbol;
    RHirNodeId enter, body_node;
    bool success = false;
    if (!r_semantic_append_symbol(body->frontend, group, &symbol) ||
        !r_symbol_vector_push(body->frontend, &body->active_symbols, symbol))
        goto cleanup;
    body->current_task_scope = symbol;
    if (!r_body_append_node(body,
                            R_HIR_TASK_SCOPE_ENTER,
                            view.span,
                            void_type,
                            symbol,
                            R_TOKEN_INVALID,
                            bound,
                            false,
                            NULL,
                            &enter))
        goto cleanup;
    /* The dependent capacity is a reference, not a child: only the clone of an instance reads
       it. */
    r_body_hir_node(body, enter)->aggregate_member = (uint32_t)dependent_capacity;
    if (!r_body_lower_try_statement(body, statement, &body_node, always_returns))
        goto cleanup;
    RHirNodeId statements[] = {enter, body_node};
    RHirVector children = {statements, 2U, 2U};
    success = r_body_append_node(body,
                                 R_HIR_BLOCK,
                                 view.span,
                                 void_type,
                                 symbol,
                                 R_TOKEN_INVALID,
                                 0U,
                                 false,
                                 &children,
                                 result);
cleanup:
    body->current_task_scope = previous;
    body->active_symbols.count = scope_base;
    return success;
}

static RSymbolId r_scope_value_region(RBodyContext *body, RHirNodeId id, uint32_t depth) {
    const RHirNode *node = r_body_hir_node(body, id);
    if (node == NULL || depth > r_frontend_tree_depth_limit(body->frontend))
        return 0U;
    if (node->task_scope != 0U)
        return node->task_scope;
    if (node->kind == R_HIR_PLACE || node->kind == R_HIR_MOVE || node->kind == R_HIR_LOAD) {
        const RSemanticSymbol *symbol = r_body_symbol(body, node->symbol);
        return symbol == NULL ? 0U : symbol->task_scope;
    }
    if (node->kind == R_HIR_VALUE_SCOPE && node->integer_value < node->child_count)
        return r_scope_value_region(
            body,
            body->frontend->hir_children[node->first_child + (uint32_t)node->integer_value],
            depth + 1U);
    if ((node->kind == R_HIR_BORROW || node->kind == R_HIR_CAST ||
         node->kind == R_HIR_FIELD_INIT) &&
        node->child_count == 1U)
        return r_scope_value_region(
            body, body->frontend->hir_children[node->first_child], depth + 1U);
    if (node->kind == R_HIR_CONDITIONAL) {
        for (uint32_t i = 1U; i < node->child_count; ++i) {
            const RSymbolId region = r_scope_value_region(
                body, body->frontend->hir_children[node->first_child + i], depth + 1U);
            if (region != 0U)
                return region;
        }
    }
    return 0U;
}

static bool r_scope_validate_tree(RBodyContext *body, RHirNodeId id, uint32_t depth) {
    const RHirNode *node = r_body_hir_node(body, id);
    if (node == NULL)
        return r_body_invariant(body);
    if (depth > r_frontend_tree_depth_limit(body->frontend)) {
        body->frontend->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
        return false;
    }
    for (uint32_t i = 0U; i < node->child_count; ++i) {
        const RHirNodeId child = body->frontend->hir_children[node->first_child + i];
        if (!r_scope_validate_tree(body, child, depth + 1U))
            return false;
        const RSymbolId region = r_scope_value_region(body, child, 0U);
        if (region == 0U)
            continue;
        const bool call = node->kind == R_HIR_CALL || node->kind == R_HIR_ASYNC_START ||
                          node->kind == R_HIR_INDIRECT_CALL || node->kind == R_HIR_STANDARD_CALL;
        /* R-STMT-0017 (L24): consuming cancel and detach keep the member supervised. */
        const bool cancel = node->kind == R_HIR_STANDARD_CALL &&
                            (node->standard_operation == R_STANDARD_CALL_ASYNC_CANCEL ||
                             node->standard_operation == R_STANDARD_CALL_ASYNC_DETACH);
        bool escapes = node->kind == R_HIR_RETURN || node->kind == R_HIR_FIELD_INIT ||
                       node->kind == R_HIR_AGGREGATE_INIT || node->kind == R_HIR_ARRAY_INIT ||
                       node->kind == R_HIR_VARIANT || node->kind == R_HIR_NEW || (call && !cancel);
        if ((node->kind == R_HIR_ASSIGN && i == 1U) || node->kind == R_HIR_LOCAL) {
            const RSemanticSymbol *destination = r_body_symbol(body, node->symbol);
            const RSemanticSymbol *group = r_body_symbol(body, region);
            escapes = destination == NULL || group == NULL || destination->is_static ||
                      destination->name_span.source != group->name_span.source ||
                      destination->name_span.start < group->scope_start;
            if (!escapes) {
                RSemanticSymbol *local = r_body_symbol_mut(body, node->symbol);
                if (local->task_scope != 0U && local->task_scope != region)
                    escapes = true;
                else
                    local->task_scope = region;
            }
        }
        if (escapes)
            return r_body_diagnostic(
                body,
                "R-DIAG-ASYNC-001",
                "R-STMT-0017",
                "a scoped task observation cannot escape its group; await, cancel or detach it "
                "there",
                node->span);
    }
    return true;
}

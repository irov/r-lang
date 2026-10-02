/* Significant results remain ordinary values. Their individual uses are checked
   by the structured-flow analysis shared with output parameters. */
static bool r_semantic_must_use_type(const RFrontendContext *context, RTypeId type) {
    const RSemanticAggregate *aggregate =
        r_semantic_aggregate_for_type(context, r_semantic_value_type(context, type));
    return aggregate != NULL && aggregate->is_must_use;
}

static bool
r_semantic_must_use_expression(const RFrontendContext *context, RHirNodeId id, uint32_t depth) {
    if (id == 0U || id > context->hir_node_count || depth > r_frontend_tree_depth_limit(context))
        return false;
    const RHirNode *node = &context->hir_nodes[id - 1U];
    if (r_semantic_must_use_type(context, node->type))
        return true;
    if ((node->kind == R_HIR_CALL || node->kind == R_HIR_ASYNC_START) && node->symbol != 0U)
        return context->semantic_symbols[node->symbol - 1U].is_must_use;
    if (node->kind == R_HIR_INDIRECT_CALL && node->child_count != 0U) {
        const RHirNode *callee = &context->hir_nodes[context->hir_children[node->first_child] - 1U];
        return callee->kind == R_HIR_FUNCTION_ADDRESS && callee->symbol != 0U &&
               context->semantic_symbols[callee->symbol - 1U].is_must_use;
    }
    if (node->kind == R_HIR_VALUE_SCOPE && node->integer_value < node->child_count)
        return r_semantic_must_use_expression(
            context,
            context->hir_children[node->first_child + (size_t)node->integer_value],
            depth + 1U);
    if (node->kind == R_HIR_AWAIT && node->symbol != 0U) {
        const RSemanticSymbol *task = &context->semantic_symbols[node->symbol - 1U];
        return r_semantic_must_use_expression(context, task->hir_node, depth + 1U);
    }
    if (node->kind == R_HIR_LOCAL || node->kind == R_HIR_CONDITIONAL) {
        const uint32_t first = node->kind == R_HIR_CONDITIONAL ? 1U : 0U;
        for (uint32_t child = first; child < node->child_count; ++child)
            if (r_semantic_must_use_expression(
                    context, context->hir_children[node->first_child + child], depth + 1U))
                return true;
    }
    return false;
}

/* R-FUNC-0021: a direct call whose declared callee lets the caller ignore the result. The
   attribute names the logical result: the task of an async callee is not discardable, the
   awaited value is. Locals, callable constraints and raw signatures do not carry it. */
static bool r_semantic_standard_call_is_discardable(RStandardCallOperation operation) {
    for (size_t index = 0U; index < sizeof(r_standard_discardable_operations) /
                                        sizeof(r_standard_discardable_operations[0]);
         ++index)
        if (r_standard_discardable_operations[index] == operation)
            return true;
    return false;
}

/* A call through a function item value first evaluates the item in a statement of its value
   scope; it is not a direct call of the named function (R-FUNC-0021). */
static bool r_semantic_function_item_call_scope(const RFrontendContext *context,
                                                const RHirNode *scope) {
    for (uint32_t index = 0U; index < scope->child_count; ++index) {
        const RHirNode *statement =
            &context->hir_nodes[context->hir_children[scope->first_child + index] - 1U];
        if ((uint64_t)index == scope->integer_value ||
            statement->kind != R_HIR_EXPRESSION_STATEMENT || statement->child_count != 1U)
            continue;
        const RHirNode *discard =
            &context->hir_nodes[context->hir_children[statement->first_child] - 1U];
        if (discard->kind != R_HIR_DISCARD || discard->child_count != 1U)
            continue;
        const RHirNode *item =
            &context->hir_nodes[context->hir_children[discard->first_child] - 1U];
        const RSemanticAggregate *aggregate =
            r_semantic_aggregate_for_type(context, r_semantic_value_type(context, item->type));
        if (aggregate != NULL && aggregate->function_item_target != R_SYMBOL_ID_INVALID)
            return true;
    }
    return false;
}

static bool r_semantic_discardable_expression(const RFrontendContext *context,
                                              RHirNodeId id,
                                              uint32_t depth,
                                              bool awaited) {
    if (id == 0U || id > context->hir_node_count || depth > r_frontend_tree_depth_limit(context))
        return false;
    const RHirNode *node = &context->hir_nodes[id - 1U];
    if (node->kind == (awaited ? R_HIR_ASYNC_START : R_HIR_CALL) && node->symbol != 0U)
        return context->semantic_symbols[node->symbol - 1U].is_discardable;
    if (!awaited && node->kind == R_HIR_STANDARD_CALL)
        return r_semantic_standard_call_is_discardable(node->standard_operation);
    if (awaited && node->kind == R_HIR_LOCAL && node->child_count != 0U)
        return r_semantic_discardable_expression(
            context, context->hir_children[node->first_child], depth + 1U, true);
    if (node->kind == R_HIR_VALUE_SCOPE && node->integer_value < node->child_count)
        return !r_semantic_function_item_call_scope(context, node) &&
               r_semantic_discardable_expression(
                   context,
                   context->hir_children[node->first_child + (size_t)node->integer_value],
                   depth + 1U,
                   awaited);
    if (!awaited && node->kind == R_HIR_AWAIT && node->symbol != 0U) {
        const RSemanticSymbol *task = &context->semantic_symbols[node->symbol - 1U];
        return task->is_hidden_local &&
               r_semantic_discardable_expression(context, task->hir_node, depth + 1U, true);
    }
    return false;
}

static const char *r_semantic_discardable_problem(const RFrontendContext *context,
                                                  const RSemanticSymbol *symbol) {
    const RSemanticTypeKind result = r_semantic_value_kind(context, symbol->return_type);
    if (!symbol->is_discardable)
        return NULL;
    if (symbol->is_must_use)
        return "@discardable and @must_use cannot mark the same function";
    if (result == R_SEMANTIC_TYPE_NEVER ||
        (result == R_SEMANTIC_TYPE_VOID && !r_out_function(context, symbol)))
        return "@discardable requires a function that produces a value";
    if (r_semantic_must_use_type(context, symbol->return_type))
        return "@discardable cannot ignore a @must_use result type";
    for (uint32_t i = 0U; i < symbol->parameter_count; ++i) {
        const RTypeId parameter =
            context->semantic_parameter_types[symbol->first_parameter_type + i];
        if (r_out_type(context, parameter) &&
            r_semantic_must_use_type(context, r_semantic_type(context, parameter)->base))
            return "@discardable cannot ignore a @must_use out value type";
    }
    return NULL;
}

static bool
r_semantic_significant_result(const RFrontendContext *context, RHirNodeId id, uint32_t depth) {
    if (id == 0U || id > context->hir_node_count || depth > r_frontend_tree_depth_limit(context))
        return false;
    const RHirNode *node = &context->hir_nodes[id - 1U];
    const RSemanticTypeKind kind = r_semantic_value_kind(context, node->type);
    if (kind == R_SEMANTIC_TYPE_VOID || kind == R_SEMANTIC_TYPE_NEVER)
        return false;
    if (r_semantic_must_use_expression(context, id, depth))
        return true;
    if (node->kind == R_HIR_CALL || node->kind == R_HIR_INDIRECT_CALL ||
        node->kind == R_HIR_STANDARD_CALL || node->kind == R_HIR_ASYNC_START ||
        node->kind == R_HIR_AWAIT)
        return true;
    if (node->kind == R_HIR_VALUE_SCOPE && node->integer_value < node->child_count)
        return r_semantic_significant_result(
            context,
            context->hir_children[node->first_child + (size_t)node->integer_value],
            depth + 1U);
    if (node->kind == R_HIR_LOCAL || node->kind == R_HIR_CAST || node->kind == R_HIR_CONDITIONAL) {
        const uint32_t first = node->kind == R_HIR_CONDITIONAL ? 1U : 0U;
        for (uint32_t child = first; child < node->child_count; ++child)
            if (r_semantic_significant_result(
                    context, context->hir_children[node->first_child + child], depth + 1U))
                return true;
    }
    if ((node->kind == R_HIR_LOAD || node->kind == R_HIR_MOVE) && node->symbol != 0U) {
        const RSemanticSymbol *binding = &context->semantic_symbols[node->symbol - 1U];
        if (binding->is_hidden_local && binding->hir_node != id)
            return r_semantic_significant_result(context, binding->hir_node, depth + 1U);
    }
    return false;
}

static bool r_semantic_validate_must_use(RFrontendContext *context) {
    for (size_t index = 0U; index < context->semantic_symbol_count; ++index) {
        const RSemanticSymbol *symbol = &context->semantic_symbols[index];
        if (symbol->kind != R_SEMANTIC_SYMBOL_FUNCTION)
            continue;
        if (symbol->is_must_use && !symbol->is_async &&
            ((r_semantic_value_kind(context, symbol->return_type) == R_SEMANTIC_TYPE_VOID &&
              !r_out_function(context, symbol)) ||
             r_semantic_value_kind(context, symbol->return_type) == R_SEMANTIC_TYPE_NEVER) &&
            !r_semantic_add_diagnostic(context,
                                       "R-DIAG-USE-001",
                                       "R-FUNC-0020",
                                       "@must_use requires a function that produces a value",
                                       symbol->name_span))
            return false;
        {
            const char *problem = r_semantic_discardable_problem(context, symbol);
            if (problem != NULL &&
                !r_semantic_add_diagnostic(
                    context, "R-DIAG-USE-001", "R-FUNC-0021", problem, symbol->name_span))
                return false;
        }
    }
    return true;
}

static bool r_must_use_validate_attribute_positions(RFrontendContext *context) {
    for (size_t source = 0U; source < context->source_count; ++source) {
        RAstRef root;
        RTypeVector pending = {0};
        bool success = false;
        if (!r_semantic_root_ref(context, (RSourceId)source + 1U, &root) ||
            !r_type_vector_push(context, &pending, root.node)) {
            r_context_free(context, pending.items);
            return false;
        }
        while (pending.count != 0U) {
            const RAstRef parent = {root.source, pending.items[--pending.count]};
            RAstNodeView view;
            RAstRef declaration;
            if (!r_ast_ref_view(context, parent, &view))
                goto cleanup;
            const bool allowed =
                (view.kind == R_SYNTAX_EXTERNAL_DECLARATION &&
                 (r_ast_find_direct_child(
                      context, parent, R_SYNTAX_FUNCTION_DECLARATION, &declaration) ||
                  r_semantic_find_struct_declaration(context, parent, &declaration) ||
                  r_semantic_find_enum_declaration(context, parent, &declaration))) ||
                (view.kind == R_SYNTAX_C_DECLARATION &&
                 (r_ast_find_direct_child(context, parent, R_SYNTAX_PARAMETER_LIST, &declaration) ||
                  r_semantic_find_struct_declaration(context, parent, &declaration) ||
                  r_semantic_find_enum_declaration(context, parent, &declaration)));
            for (uint32_t index = 0U; index < view.child_count; ++index) {
                RAstRef child;
                RAstNodeView attribute;
                RAstTokenView name;
                if (!r_ast_ref_child(context, parent, index, &child) ||
                    !r_type_vector_push(context, &pending, child.node))
                    goto cleanup;
                if (allowed || !r_ast_ref_view(context, child, &attribute) ||
                    attribute.kind != R_SYNTAX_ATTRIBUTE ||
                    !r_ast_direct_token(context, child, 1U, NULL, &name))
                    continue;
                /* An aggregate declaration rejects @discardable itself (R-GRAM-0007). */
                if (r_semantic_token_text_equal_owned(context, &name, "must_use") &&
                    !r_semantic_add_diagnostic(
                        context,
                        "R-DIAG-SYN-002",
                        "R-GRAM-0007",
                        "@must_use requires a named function or nominal type declaration",
                        attribute.span))
                    goto cleanup;
                if (r_semantic_token_text_equal_owned(context, &name, "discardable") &&
                    !r_semantic_add_diagnostic(context,
                                               "R-DIAG-SYN-002",
                                               "R-GRAM-0007",
                                               "@discardable requires a named function declaration",
                                               attribute.span))
                    goto cleanup;
            }
        }
        success = true;
    cleanup:
        r_context_free(context, pending.items);
        if (!success)
            return false;
    }
    return true;
}

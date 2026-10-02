/* R-STMT-0020: `budget (limits) block` gives the executing task a new budget for the block. The
 * budget it replaces lives in a hidden local, and the block is lowered as a try statement whose
 * compiler-owned finally restores that budget on every exit, like a deadline block (R-STMT-0019).
 * The limits value of type std.alloc::limits is the one operand of the entry; the C17 emitter
 * reads its fields bytes and tasks. */

static bool
r_budget_lower(RBodyContext *body, RAstRef statement, RHirNodeId *result, bool *always_returns) {
    RAstNodeView view;
    RAstRef value_ref;
    RAstTokenView keyword;
    RTypeId usize_type, void_type;
    RExpressionResult value, entered;
    RHirVector statements = {0};
    RSymbolId saved = R_SYMBOL_ID_INVALID;
    RHirNodeId try_node;
    const RSemanticField *bytes_field;
    const RSemanticField *tasks_field;
    const size_t scope_base = body->active_symbols.count;
    const RSymbolId previous = body->current_budget_saved;
    bool success = false;

    *result = R_HIR_NODE_ID_INVALID;
    *always_returns = false;
    if (!r_ast_ref_view(body->frontend, statement, &view) ||
        !r_ast_find_direct_child(body->frontend, statement, R_SYNTAX_EXPRESSION, &value_ref) ||
        !r_body_ast_first_token(body->frontend, statement, &keyword))
        return r_body_invariant(body);
    if (!body->is_async || body->finally_depth != 0U)
        return r_body_diagnostic(body,
                                 "R-DIAG-ASYNC-001",
                                 "R-STMT-0020",
                                 "a budget block requires an async body outside finally",
                                 view.span);
    if (!r_semantic_type_from_token(body->frontend, R_TOKEN_KW_USIZE, &usize_type) ||
        !r_semantic_type_from_token(body->frontend, R_TOKEN_KW_VOID, &void_type) ||
        !r_body_lower_value(body, value_ref, R_TYPE_ID_INVALID, &value))
        return false;
    if (!value.valid)
        return true;
    if (!r_semantic_budget_limits_fields(body->frontend, value.type, &bytes_field, &tasks_field))
        return r_body_diagnostic(body,
                                 "R-DIAG-TYPE-001",
                                 "R-STMT-0020",
                                 "a budget block requires a std.alloc::limits",
                                 view.span);
    {
        RHirNodeId child_storage[1] = {value.node};
        RHirVector children = {child_storage, UINT32_C(1), UINT32_C(1)};

        r_body_expression_invalid(&entered);
        if (!r_body_append_standard_call(body,
                                         view.span,
                                         usize_type,
                                         usize_type,
                                         R_STANDARD_CALL_ASYNC_BUDGET_ENTER,
                                         &children,
                                         NULL,
                                         &entered))
            goto cleanup;
    }
    if (!r_iteration_declare_hidden(body,
                                    scope_base,
                                    "$budget",
                                    usize_type,
                                    statement.node,
                                    keyword.span,
                                    &entered,
                                    &statements,
                                    &saved))
        goto cleanup;
    body->current_budget_saved = saved;
    if (!r_body_lower_try_statement(body, statement, &try_node, always_returns) ||
        !r_hir_vector_push(body->frontend, &statements, try_node))
        goto cleanup;
    success = r_body_append_node(body,
                                 R_HIR_BLOCK,
                                 view.span,
                                 void_type,
                                 R_SYMBOL_ID_INVALID,
                                 R_TOKEN_INVALID,
                                 0U,
                                 false,
                                 &statements,
                                 result);
cleanup:
    body->current_budget_saved = previous;
    body->active_symbols.count = scope_base;
    r_context_free(body->frontend, statements.items);
    return success;
}

static bool r_budget_finally(RBodyContext *body, RSourceSpan span, RHirNodeId *result) {
    RTypeId void_type;
    RExpressionResult saved, left;
    RHirVector statements = {0};
    RHirNodeId block;
    bool success = false;

    if (body->current_budget_saved == R_SYMBOL_ID_INVALID)
        return r_body_invariant(body);
    if (!r_semantic_type_from_token(body->frontend, R_TOKEN_KW_VOID, &void_type) ||
        !r_iteration_load(body, body->current_budget_saved, span, &saved))
        goto cleanup;
    {
        RHirNodeId child_storage[1] = {saved.node};
        RHirVector children = {child_storage, UINT32_C(1), UINT32_C(1)};

        r_body_expression_invalid(&left);
        if (!r_body_append_standard_call(body,
                                         span,
                                         void_type,
                                         void_type,
                                         R_STANDARD_CALL_ASYNC_BUDGET_LEAVE,
                                         &children,
                                         NULL,
                                         &left))
            goto cleanup;
    }
    if (!r_iteration_expression_statement(body, span, &left, &statements) ||
        !r_body_append_node(body,
                            R_HIR_BLOCK,
                            span,
                            void_type,
                            R_SYMBOL_ID_INVALID,
                            R_TOKEN_INVALID,
                            0U,
                            false,
                            &statements,
                            &block))
        goto cleanup;
    {
        RHirVector children = {&block, 1U, 1U};
        success = r_body_append_node(body,
                                     R_HIR_FINALLY,
                                     span,
                                     void_type,
                                     R_SYMBOL_ID_INVALID,
                                     R_TOKEN_KW_FINALLY,
                                     0U,
                                     false,
                                     &children,
                                     result);
    }
cleanup:
    r_context_free(body->frontend, statements.items);
    return success;
}

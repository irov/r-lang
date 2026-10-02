/* R-STMT-0019: `deadline (instant) block` narrows the deadline of the executing task for the
 * block. The deadline it replaces lives in a hidden local, and the block is lowered as a try
 * statement whose compiler-owned finally restores that deadline on every exit, like the drain
 * of a task group (R-STMT-0017). */

static bool r_deadline_types(RBodyContext *body, RTypeId *instant, RTypeId *option) {
    return r_semantic_intern_named_standard_type(body->frontend, "std.time::instant", instant) &&
           r_semantic_intern_type(body->frontend, R_SEMANTIC_TYPE_OPTION, *instant, option);
}

static bool r_deadline_call(RBodyContext *body,
                            RSourceSpan span,
                            RStandardCallOperation operation,
                            RTypeId result_type,
                            const RExpressionResult *operand,
                            RExpressionResult *result) {
    RHirNodeId child_storage[1] = {operand->node};
    RHirVector children = {child_storage, UINT32_C(1), UINT32_C(1)};

    r_body_expression_invalid(result);
    return r_body_append_standard_call(
        body, span, result_type, result_type, operation, &children, NULL, result);
}

static bool
r_deadline_lower(RBodyContext *body, RAstRef statement, RHirNodeId *result, bool *always_returns) {
    RAstNodeView view;
    RAstRef value_ref;
    RAstTokenView keyword;
    RTypeId instant_type, option_type, void_type, value_type = R_TYPE_ID_INVALID;
    RExpressionResult value, entered;
    RHirVector statements = {0};
    RSymbolId saved = R_SYMBOL_ID_INVALID;
    RHirNodeId try_node;
    const size_t scope_base = body->active_symbols.count;
    const RSymbolId previous = body->current_deadline_saved;
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
                                 "R-STMT-0019",
                                 "a deadline block requires an async body outside finally",
                                 view.span);
    if (!r_deadline_types(body, &instant_type, &option_type) ||
        !r_semantic_type_from_token(body->frontend, R_TOKEN_KW_VOID, &void_type) ||
        !r_overload_argument_type(body, value_ref, 0U, false, &value_type))
        return false;
    /* A value whose type is not known before lowering, such as o::none, is checked against
     * o<instant>; an instant becomes o::some(instant). */
    if (value_type != R_TYPE_ID_INVALID) {
        value_type = r_semantic_value_type(body->frontend, value_type);
    }
    if (!r_body_lower_value(body,
                            value_ref,
                            value_type == R_TYPE_ID_INVALID || value_type == option_type
                                ? option_type
                                : R_TYPE_ID_INVALID,
                            &value))
        return false;
    if (!value.valid)
        return true;
    value_type = r_semantic_value_type(body->frontend, value.type);
    if (value_type != instant_type && value_type != option_type)
        return r_body_diagnostic(body,
                                 "R-DIAG-TYPE-001",
                                 "R-STMT-0019",
                                 "a deadline block requires a std.time::instant or "
                                 "o<std.time::instant>",
                                 view.span);
    if (value_type == instant_type &&
        !r_body_append_builtin_variant(
            body, view.span, option_type, R_BUILTIN_VARIANT_OPTION_SOME, &value, &value))
        return false;
    if (!r_deadline_call(
            body, view.span, R_STANDARD_CALL_ASYNC_DEADLINE_ENTER, option_type, &value, &entered) ||
        !r_iteration_declare_hidden(body,
                                    scope_base,
                                    "$deadline",
                                    option_type,
                                    statement.node,
                                    keyword.span,
                                    &entered,
                                    &statements,
                                    &saved))
        goto cleanup;
    body->current_deadline_saved = saved;
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
    body->current_deadline_saved = previous;
    body->active_symbols.count = scope_base;
    r_context_free(body->frontend, statements.items);
    return success;
}

static bool r_deadline_finally(RBodyContext *body, RSourceSpan span, RHirNodeId *result) {
    RTypeId void_type;
    RExpressionResult saved, left;
    RHirVector statements = {0};
    RHirNodeId block;
    bool success = false;

    if (body->current_deadline_saved == R_SYMBOL_ID_INVALID)
        return r_body_invariant(body);
    if (!r_semantic_type_from_token(body->frontend, R_TOKEN_KW_VOID, &void_type) ||
        !r_iteration_load(body, body->current_deadline_saved, span, &saved) ||
        !r_deadline_call(
            body, span, R_STANDARD_CALL_ASYNC_DEADLINE_LEAVE, void_type, &saved, &left) ||
        !r_iteration_expression_statement(body, span, &left, &statements) ||
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

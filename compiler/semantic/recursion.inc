/* R-FUNC-0026 (L35): a function with `@recursion(depth = N)` begins with a standard entry that
 * throws core::recursion_error while N activations of it exist on the thread, before any
 * statement of its body. The body is then lowered as a try statement whose compiler-owned
 * finally ends the activation on every exit, like a budget block (R-STMT-0020). The entry and the
 * exit name the function, whose instance the C17 emitter counts, and the entry carries N. */

static bool r_recursion_lower_body(RBodyContext *body,
                                   RAstRef function,
                                   RSourceSpan span,
                                   RHirNodeId *result,
                                   bool *always_returns) {
    RFrontendContext *context = body->frontend;
    const RSemanticSymbol *symbol = r_body_symbol(body, body->function_symbol);
    RTypeId void_type, error_type, effect_set = R_TYPE_ID_INVALID, carrier;
    RHirVector statements = {0};
    RHirNodeId entry, try_node;
    RAstRef block;
    RExpressionResult entered;
    bool declared = false, success = false;

    *result = R_HIR_NODE_ID_INVALID;
    *always_returns = false;
    if ((symbol == NULL) || !r_ast_find_direct_child(context, function, R_SYNTAX_BLOCK, &block))
        return r_body_invariant(body);
    if (!r_semantic_type_from_token(context, R_TOKEN_KW_VOID, &void_type) ||
        !r_semantic_intern_named_standard_type(context, "core::recursion_error", &error_type))
        return false;
    for (uint32_t index = 0U; index < r_semantic_effect_count(context, body->throws_type); ++index)
        declared = declared || r_semantic_effect_at(context, body->throws_type, index) == error_type;
    /* A rejected attribute leaves an ordinary body, so its own diagnostics still appear. */
    if (symbol->is_async)
        return r_body_diagnostic(body,
                                 "R-DIAG-ASYNC-001",
                                 "R-FUNC-0026",
                                 "@recursion requires a synchronous function; asynchronous "
                                 "recursion has no static stack bound",
                                 symbol->name_span) &&
               r_body_lower_block(body, block, true, result, always_returns);
    if (!declared)
        return r_body_diagnostic(body,
                                 "R-DIAG-EFFECT-001",
                                 "R-FUNC-0026",
                                 "a function with @recursion lists core::recursion_error in its "
                                 "throws list",
                                 symbol->name_span) &&
               r_body_lower_block(body, block, true, result, always_returns);
    if (!r_semantic_single_effect_set(context, error_type, &effect_set) ||
        !r_semantic_intern_effect_carrier(context, void_type, effect_set, &carrier) ||
        !r_body_append_node(body,
                            R_HIR_STANDARD_CALL,
                            span,
                            void_type,
                            body->function_symbol,
                            R_TOKEN_INVALID,
                            (uint64_t)symbol->recursion_depth,
                            false,
                            NULL,
                            &entry))
        goto cleanup;
    r_body_hir_node(body, entry)->standard_operation = R_STANDARD_CALL_CORE_RECURSION_ENTER;
    r_body_hir_node(body, entry)->auxiliary_type = carrier;
    if (!r_body_attach_effect_exits(body, entry, effect_set, span))
        goto cleanup;
    r_body_expression_invalid(&entered);
    entered.valid = true;
    entered.node = entry;
    entered.type = void_type;
    if (!r_iteration_expression_statement(body, span, &entered, &statements) ||
        !r_body_lower_try_statement(body, function, &try_node, always_returns) ||
        !r_hir_vector_push(context, &statements, try_node))
        goto cleanup;
    success = r_body_append_node(body,
                                 R_HIR_BLOCK,
                                 span,
                                 void_type,
                                 R_SYMBOL_ID_INVALID,
                                 R_TOKEN_INVALID,
                                 0U,
                                 false,
                                 &statements,
                                 result);
cleanup:
    r_context_free(context, statements.items);
    return success;
}

static bool r_recursion_finally(RBodyContext *body, RSourceSpan span, RHirNodeId *result) {
    RTypeId void_type;
    RExpressionResult left;
    RHirVector statements = {0};
    RHirNodeId leave, block;
    bool success = false;

    if (!r_semantic_type_from_token(body->frontend, R_TOKEN_KW_VOID, &void_type) ||
        !r_body_append_node(body,
                            R_HIR_STANDARD_CALL,
                            span,
                            void_type,
                            body->function_symbol,
                            R_TOKEN_INVALID,
                            0U,
                            false,
                            NULL,
                            &leave))
        return false;
    r_body_hir_node(body, leave)->standard_operation = R_STANDARD_CALL_CORE_RECURSION_LEAVE;
    r_body_hir_node(body, leave)->auxiliary_type = void_type;
    r_body_expression_invalid(&left);
    left.valid = true;
    left.node = leave;
    left.type = void_type;
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

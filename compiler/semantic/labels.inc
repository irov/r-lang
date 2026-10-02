/* R-STMT-0006 (L20.3, L20.4): a switch over `str` compares its string labels byte for byte, and a
   switch over a core::CaseMatcher asks its `matches` for each label. The value is evaluated once
   into a hidden object, the number of the first clause whose label matches is computed in clause
   order, and the clauses are then lowered as an integer switch over that number, as select does
   (R-STMT-0018); no match selects the default clause. */

static bool r_label_switch_type(RBodyContext *body, RTypeId type) {
    RSymbolId matches;
    RTypeId label_type;
    const RSourceSpan span = {0};
    if (r_match_string_type(body, type)) {
        return true;
    }
    return r_label_matcher(body, type, span, &matches, &label_type) &&
           (matches != R_SYMBOL_ID_INVALID);
}

static bool r_label_error(RBodyContext *body, RSourceSpan span, const char *message) {
    return r_body_diagnostic(body, "R-DIAG-SWITCH-001", "R-STMT-0006", message, span);
}

static bool r_body_lower_label_switch(RBodyContext *body,
                                      RAstRef statement,
                                      const RExpressionResult *value,
                                      RHirNodeId *node_id,
                                      bool *always_returns) {
    RAstNodeView view;
    const size_t scope_base = body->active_symbols.count;
    RHirVector prefix = {0};
    RSymbolId hidden = R_SYMBOL_ID_INVALID;
    RExpressionResult place = {0};
    RExpressionResult *tests = NULL;
    uint64_t *tags = NULL;
    uint64_t *labels = NULL;
    bool *string_labels = NULL;
    uint32_t count = 0U;
    uint32_t default_child = UINT32_MAX;
    RTypeId usize_type, void_type;
    RSymbolId matches = R_SYMBOL_ID_INVALID;
    RTypeId label_type = value->type;
    bool success = false;

    *node_id = R_HIR_NODE_ID_INVALID;
    *always_returns = false;
    if (!r_ast_ref_view(body->frontend, statement, &view) ||
        !r_semantic_type_from_token(body->frontend, R_TOKEN_KW_USIZE, &usize_type) ||
        !r_semantic_type_from_token(body->frontend, R_TOKEN_KW_VOID, &void_type)) {
        body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    place = *value;
    if (!value->is_place) {
        /* A computed value is evaluated once into a hidden object the labels then read. */
        RAstRef expression = {0};
        (void)r_ast_find_direct_child(body->frontend, statement, R_SYNTAX_EXPRESSION, &expression);
        if (!r_iteration_declare_hidden(body,
                                        scope_base,
                                        "$switch",
                                        value->type,
                                        expression.node != 0U ? expression.node : statement.node,
                                        view.span,
                                        value,
                                        &prefix,
                                        &hidden) ||
            !r_body_symbol_place(body, hidden, view.span, &place)) {
            goto cleanup;
        }
    }
    if (!r_match_string_type(body, value->type) &&
        !r_label_matcher(body, value->type, view.span, &matches, &label_type)) {
        goto cleanup;
    }
    tests = r_context_allocate(body->frontend, ((size_t)view.child_count + 1U) * sizeof(*tests));
    string_labels = r_context_allocate(body->frontend,
                                       ((size_t)view.child_count + 1U) * sizeof(*string_labels));
    tags = r_context_allocate(body->frontend, ((size_t)view.child_count + 1U) * sizeof(*tags));
    labels = r_context_allocate(body->frontend, ((size_t)view.child_count + 1U) * sizeof(*labels));
    if ((tests == NULL) || (tags == NULL) || (labels == NULL) || (string_labels == NULL)) {
        goto cleanup;
    }
    (void)memset(tags, 0, ((size_t)view.child_count + 1U) * sizeof(*tags));
    for (uint32_t child_index = 0U; child_index < view.child_count; ++child_index) {
        RAstRef clause, pattern, label;
        RAstNodeView clause_view;
        if (!r_ast_ref_child(body->frontend, statement, child_index, &clause) ||
            !r_ast_ref_view(body->frontend, clause, &clause_view)) {
            continue;
        }
        if (clause_view.kind == R_SYNTAX_DEFAULT_CLAUSE) {
            default_child = child_index;
            continue;
        }
        if (clause_view.kind != R_SYNTAX_CASE_CLAUSE) {
            continue;
        }
        if (!r_ast_find_direct_child(body->frontend, clause, R_SYNTAX_CASE_PATTERN, &pattern) ||
            !r_ast_find_direct_child(body->frontend, pattern, R_SYNTAX_EXPRESSION, &label)) {
            success = r_label_error(
                body, clause_view.span, "a switch label is a string literal or a constant");
            goto cleanup;
        }
        {
            RExpressionResult constant = {0};
            RBodyConstantValue bits = {0};
            const RHirNode *literal;
            if (!r_body_lower_value(body, label, label_type, &constant) ||
                (constant.valid && !r_body_finish_value(body, label_type, &constant))) {
                goto cleanup;
            }
            if (!constant.valid) {
                success = true;
                goto cleanup;
            }
            if (r_semantic_value_type(body->frontend, constant.type) !=
                r_semantic_value_type(body->frontend, label_type)) {
                success = r_body_diagnostic(body,
                                            "R-DIAG-TYPE-001",
                                            "R-STMT-0006",
                                            "a label has the label type of its switch",
                                            clause_view.span);
                goto cleanup;
            }
            literal = r_match_string_literal(body, constant.node);
            string_labels[count] = literal != NULL;
            if (literal != NULL) {
                labels[count] = literal->aggregate_member;
            } else if ((matches != R_SYMBOL_ID_INVALID) &&
                       r_body_constant_scalar(body, constant.node, &bits) && bits.valid) {
                labels[count] = bits.bits;
            } else {
                success = r_label_error(
                    body, clause_view.span, "a switch label is a string literal or a constant");
                goto cleanup;
            }
            for (uint32_t prior = 0U; prior < count; ++prior) {
                if ((string_labels[prior] == string_labels[count]) &&
                    (labels[prior] == labels[count])) {
                    success = r_label_error(body, clause_view.span, "case labels shall be unique");
                    goto cleanup;
                }
            }
        }
        if ((matches != R_SYMBOL_ID_INVALID)
                ? !r_label_matcher_test(
                      body, place, matches, label, label_type, clause_view.span, &tests[count])
                : !r_label_string_test(
                      body, place, label, value->type, clause_view.span, &tests[count])) {
            goto cleanup;
        }
        if (!tests[count].valid) {
            success = true;
            goto cleanup;
        }
        count += 1U;
        tags[child_index] = (uint64_t)count;
    }
    if (default_child == UINT32_MAX) {
        success = r_label_error(
            body, view.span, "a switch over string or matcher labels requires a default clause");
        goto cleanup;
    }
    {
        /* The first matching label in clause order selects its clause number; none selects 0. */
        RExpressionResult selected = {0};
        RBodySelect select = {0};
        RHirNodeId switch_node = R_HIR_NODE_ID_INVALID;
        if (!r_iteration_literal(body, view.span, usize_type, UINT64_C(0), &selected)) {
            goto cleanup;
        }
        for (uint32_t index = count; index > 0U; --index) {
            RExpressionResult number = {0};
            RHirNodeId nodes[3];
            RHirVector children = {nodes, 3U, 3U};
            if (!r_iteration_literal(body, view.span, usize_type, (uint64_t)index, &number)) {
                goto cleanup;
            }
            nodes[0] = tests[index - 1U].node;
            nodes[1] = number.node;
            nodes[2] = selected.node;
            if (!r_body_append_node(body,
                                    R_HIR_CONDITIONAL,
                                    view.span,
                                    usize_type,
                                    R_SYMBOL_ID_INVALID,
                                    R_TOKEN_QUESTION,
                                    UINT64_C(0),
                                    false,
                                    &children,
                                    &selected.node)) {
                goto cleanup;
            }
        }
        selected.type = usize_type;
        selected.symbol = R_SYMBOL_ID_INVALID;
        selected.borrow_origin = R_SYMBOL_ID_INVALID;
        selected.valid = true;
        select.value = selected;
        select.tags = tags;
        select.default_child = default_child;
        if (!r_body_lower_switch_statement(
                body, statement, &select, &switch_node, always_returns)) {
            goto cleanup;
        }
        if (switch_node == R_HIR_NODE_ID_INVALID) {
            success = true;
            goto cleanup;
        }
        if (!r_hir_vector_push(body->frontend, &prefix, switch_node) ||
            (!*always_returns && (hidden != R_SYMBOL_ID_INVALID) &&
             !r_body_append_implicit_drop(body, hidden, view.span, &prefix))) {
            goto cleanup;
        }
    }
    body->active_symbols.count = scope_base;
    success = r_body_append_node(body,
                                 R_HIR_BLOCK,
                                 view.span,
                                 void_type,
                                 R_SYMBOL_ID_INVALID,
                                 R_TOKEN_INVALID,
                                 UINT64_C(0),
                                 false,
                                 &prefix,
                                 node_id);
cleanup:
    body->active_symbols.count = scope_base;
    r_context_free(body->frontend, prefix.items);
    r_context_free(body->frontend, tests);
    r_context_free(body->frontend, tags);
    r_context_free(body->frontend, labels);
    r_context_free(body->frontend, string_labels);
    return success;
}

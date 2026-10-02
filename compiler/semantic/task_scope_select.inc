static bool r_scope_select_find_await(const RFrontendContext *context,
                                      RAstRef ref,
                                      uint32_t depth,
                                      RAstRef *operation) {
    RAstNodeView view;
    if (depth > r_frontend_tree_depth_limit(context) || !r_ast_ref_view(context, ref, &view))
        return false;
    if (view.kind == R_SYNTAX_AWAIT_OPERATION) {
        *operation = ref;
        return true;
    }
    for (uint32_t index = 0U; index < view.child_count; ++index) {
        RAstRef child;
        if (r_ast_ref_child(context, ref, index, &child) &&
            r_scope_select_find_await(context, child, depth + 1U, operation))
            return true;
    }
    return false;
}

/* R-STMT-0018: the member a select branch consumes, or false when the branch is not
 * `T name = await move member` or `await move member`. The head of the branch is its first
 * node after `case`, so a declaration in the body of an `await move member` branch is not taken
 * for it (M32T-2). */
static bool r_scope_select_arm_member(RBodyContext *body, RAstRef clause, RAstTokenView *member) {
    RAstRef arm = {0}, operation;
    RAstNodeView clause_view, arm_view = {0}, operation_view;
    bool moved = false, named = false, headed = false;
    if (!r_ast_ref_view(body->frontend, clause, &clause_view))
        return false;
    for (uint32_t index = 0U; index < clause_view.child_count && !headed; ++index) {
        if (r_ast_ref_child(body->frontend, clause, index, &arm) &&
            r_ast_ref_view(body->frontend, arm, &arm_view) && arm_view.kind != R_SYNTAX_TOKEN)
            headed = true;
    }
    if (!headed)
        return false;
    if (arm_view.kind == R_SYNTAX_OBJECT_DECLARATION) {
        RAstRef initializer, expression;
        RAstNodeView expression_view;
        if (!r_ast_find_direct_child(body->frontend, arm, R_SYNTAX_INITIALIZER, &initializer) ||
            !r_ast_find_direct_child(
                body->frontend, initializer, R_SYNTAX_EXPRESSION, &expression) ||
            !r_ast_ref_view(body->frontend, expression, &expression_view) ||
            !r_scope_select_find_await(body->frontend, expression, 0U, &operation) ||
            !r_ast_ref_view(body->frontend, operation, &operation_view) ||
            operation_view.span.start != expression_view.span.start ||
            operation_view.span.end != expression_view.span.end)
            return false;
    } else if (arm_view.kind != R_SYNTAX_AWAIT_STATEMENT ||
               !r_ast_find_direct_child(
                   body->frontend, arm, R_SYNTAX_AWAIT_OPERATION, &operation) ||
               !r_ast_ref_view(body->frontend, operation, &operation_view)) {
        return false;
    }
    for (uint32_t index = 0U; index < operation_view.child_count; ++index) {
        RAstRef child;
        RAstTokenView token;
        if (!r_ast_ref_child(body->frontend, operation, index, &child) ||
            !r_ast_ref_token(body->frontend, child, &token))
            return false;
        if (token.kind == R_TOKEN_KW_MOVE) {
            moved = true;
        } else if (token.kind == R_TOKEN_IDENTIFIER && moved) {
            *member = token;
            named = true;
        }
    }
    return named;
}

/* R-STMT-0018: `select (group) { case T name = await move member: ... case until (deadline):
 * ... }` waits until a listed member is terminal or the deadline passes, then consumes that
 * member in its branch; a tie selects the earliest branch. Unselected members keep running and
 * stay unconsumed: they become select-pending until group.cancel_all() or the group exit. */
static bool r_scope_lower_select(RBodyContext *body,
                                 RAstRef statement,
                                 RHirNodeId *node_id,
                                 bool *always_returns) {
    RAstNodeView view;
    RAstTokenView group_name = {0};
    RBodyLookupIssue issue;
    RAstRef deadline = {0};
    RHirVector children = {0};
    RSymbolId *members = NULL;
    uint64_t *tags = NULL;
    size_t member_count = 0U;
    uint32_t deadline_child = UINT32_MAX;
    uint32_t last_clause = UINT32_MAX;
    bool named = false, success = false;
    *node_id = R_HIR_NODE_ID_INVALID;
    *always_returns = false;
    if (!r_ast_ref_view(body->frontend, statement, &view))
        return r_body_invariant(body);
    for (uint32_t index = 0U; index < view.child_count && !named; ++index) {
        RAstRef child;
        if (r_ast_ref_child(body->frontend, statement, index, &child) &&
            r_ast_ref_token(body->frontend, child, &group_name) &&
            group_name.kind == R_TOKEN_IDENTIFIER && index != 0U)
            named = true;
    }
    const RSymbolId group =
        named ? r_body_resolve_name(body, &group_name, &issue) : R_SYMBOL_ID_INVALID;
    const RSemanticSymbol *group_symbol = r_body_symbol(body, group);
    if (group_symbol == NULL || !group_symbol->is_task_scope)
        return r_body_diagnostic(body,
                                 "R-DIAG-ASYNC-001",
                                 "R-STMT-0018",
                                 "select names an enclosing task group",
                                 view.span);
    members = r_context_allocate(body->frontend, (size_t)view.child_count * sizeof(*members));
    tags = r_context_allocate(body->frontend, (size_t)view.child_count * sizeof(*tags));
    if (members == NULL || tags == NULL)
        goto cleanup;
    (void)memset(tags, 0, (size_t)view.child_count * sizeof(*tags));
    for (uint32_t index = 0U; index < view.child_count; ++index) {
        RAstRef clause, deadline_arm;
        RAstNodeView clause_view;
        RAstTokenView member_name;
        if (!r_ast_ref_child(body->frontend, statement, index, &clause) ||
            !r_ast_ref_view(body->frontend, clause, &clause_view) ||
            clause_view.kind != R_SYNTAX_SELECT_CLAUSE)
            continue;
        last_clause = index;
        for (uint32_t child_index = 0U; child_index < clause_view.child_count; ++child_index) {
            RAstRef child;
            RAstNodeView child_view;
            RAstTokenView jump;
            if (r_ast_ref_child(body->frontend, clause, child_index, &child) &&
                r_ast_view_kind(body->frontend, child, R_SYNTAX_JUMP_STATEMENT, &child_view) &&
                r_body_ast_first_token(body->frontend, child, &jump) &&
                jump.kind == R_TOKEN_KW_FALLTHROUGH) {
                success = r_body_diagnostic(body,
                                            "R-DIAG-FLOW-001",
                                            "R-STMT-0018",
                                            "a select branch cannot fall through",
                                            clause_view.span);
                goto cleanup;
            }
        }
        if (r_ast_find_direct_child(
                body->frontend, clause, R_SYNTAX_SELECT_DEADLINE, &deadline_arm)) {
            if (deadline_child != UINT32_MAX) {
                success = r_body_diagnostic(body,
                                            "R-DIAG-ASYNC-001",
                                            "R-STMT-0018",
                                            "select takes at most one until branch",
                                            clause_view.span);
                goto cleanup;
            }
            if (!r_ast_find_direct_child(
                    body->frontend, deadline_arm, R_SYNTAX_EXPRESSION, &deadline)) {
                success = r_body_cascade(body);
                goto cleanup;
            }
            deadline_child = index;
            continue;
        }
        if (!r_scope_select_arm_member(body, clause, &member_name)) {
            success = r_body_diagnostic(body,
                                        "R-DIAG-ASYNC-001",
                                        "R-STMT-0018",
                                        "a select branch is `T name = await move member`, "
                                        "`await move member` or `until (deadline)`",
                                        clause_view.span);
            goto cleanup;
        }
        RBodyLookupIssue member_issue;
        const RSymbolId member = r_body_resolve_name(body, &member_name, &member_issue);
        const RSemanticSymbol *member_symbol = r_body_symbol(body, member);
        if (member_symbol == NULL || member_symbol->task_scope != group) {
            success = r_body_diagnostic(body,
                                        "R-DIAG-ASYNC-001",
                                        "R-STMT-0018",
                                        "a select branch consumes a member task of its group",
                                        member_name.span);
            goto cleanup;
        }
        for (size_t previous = 0U; previous < member_count; ++previous) {
            if (members[previous] == member) {
                success = r_body_diagnostic(body,
                                            "R-DIAG-ASYNC-001",
                                            "R-STMT-0018",
                                            "a select lists each member task once",
                                            member_name.span);
                goto cleanup;
            }
        }
        if (member_symbol->object_state != R_SEMANTIC_OBJECT_STATE_INITIALIZED) {
            success = r_body_diagnostic(body,
                                        "R-DIAG-MOVE-002",
                                        "R-STMT-0018",
                                        "a select branch names a task that may already be consumed",
                                        member_name.span);
            goto cleanup;
        }
        tags[index] = (uint64_t)member_count;
        members[member_count++] = member;
    }
    if (member_count == 0U) {
        success = r_body_diagnostic(body,
                                    "R-DIAG-ASYNC-001",
                                    "R-STMT-0018",
                                    "select requires at least one member task branch",
                                    view.span);
        goto cleanup;
    }
    if (deadline_child != UINT32_MAX) {
        /* The deadline branch takes the index after the last member. */
        RTypeId instant;
        RExpressionResult value;
        tags[deadline_child] = (uint64_t)member_count;
        if (!r_semantic_intern_named_standard_type(body->frontend, "std.time::instant", &instant) ||
            !r_body_lower_value(body, deadline, instant, &value))
            goto cleanup;
        if (!value.valid) {
            success = true;
            goto cleanup;
        }
        if (r_semantic_value_type(body->frontend, value.type) != instant) {
            success = r_body_diagnostic(body,
                                        "R-DIAG-TYPE-001",
                                        "R-STMT-0018",
                                        "the deadline of a select is a std.time::instant",
                                        view.span);
            goto cleanup;
        }
        if (!r_hir_vector_push(body->frontend, &children, value.node))
            goto cleanup;
    }
    for (size_t index = 0U; index < member_count; ++index) {
        const RSemanticSymbol *member = r_body_symbol(body, members[index]);
        RExpressionResult place, borrow;
        RTypeId borrowed_task;
        if (!r_semantic_intern_derived_type(body->frontend,
                                            R_SEMANTIC_TYPE_BORROW,
                                            member->type,
                                            0U,
                                            0U,
                                            R_SEMANTIC_TYPE_FLAG_SHARED,
                                            &borrowed_task) ||
            !r_body_symbol_place(body, members[index], view.span, &place) ||
            !r_body_form_safe_borrow(body, view.span, borrowed_task, &place, &borrow))
            goto cleanup;
        if (!borrow.valid) {
            success = true;
            goto cleanup;
        }
        if (!r_hir_vector_push(body->frontend, &children, borrow.node))
            goto cleanup;
    }
    RBodySelect select = {0};
    if (!r_semantic_type_from_token(body->frontend, R_TOKEN_KW_USIZE, &select.value.type) ||
        !r_body_append_node(body,
                            R_HIR_TASK_SCOPE_WAIT,
                            view.span,
                            select.value.type,
                            group,
                            R_TOKEN_KW_AWAIT,
                            1U | (deadline_child != UINT32_MAX ? 2U : 0U) | 4U,
                            false,
                            &children,
                            &select.value.node))
        goto cleanup;
    select.value.symbol = R_SYMBOL_ID_INVALID;
    select.value.borrow_origin = R_SYMBOL_ID_INVALID;
    select.value.valid = true;
    select.tags = tags;
    select.default_child = last_clause;
    select.members = members;
    select.member_count = member_count;
    success = r_body_lower_switch_statement(body, statement, &select, node_id, always_returns);
cleanup:
    r_context_free(body->frontend, children.items);
    r_context_free(body->frontend, members);
    r_context_free(body->frontend, tags);
    return success;
}

static bool r_scope_first_name(const RFrontendContext *context,
                               RAstRef ref,
                               RAstTokenView *name,
                               uint32_t depth) {
    RAstNodeView view;
    if (depth > r_frontend_tree_depth_limit(context) || !r_ast_ref_view(context, ref, &view))
        return false;
    for (uint32_t i = 0U; i < view.child_count; ++i) {
        RAstRef child;
        RAstTokenView token;
        if (!r_ast_ref_child(context, ref, i, &child))
            return false;
        if (r_ast_ref_token(context, child, &token)) {
            if (token.kind == R_TOKEN_IDENTIFIER) {
                *name = token;
                return true;
            }
        } else if (r_scope_first_name(context, child, name, depth + 1U))
            return true;
    }
    return false;
}

static bool r_scope_lower_wait(
    RBodyContext *body, RAstRef call, bool is_statement, RExpressionResult *result, bool *handled) {
    RAstNodeView view, member_view;
    RAstRef root, member, suffix, args;
    RAstTokenView name, operation;
    RBodyLookupIssue issue;
    *handled = false;
    if (!r_ast_ref_view(body->frontend, call, &view) || view.child_count != 2U ||
        !r_ast_ref_child(body->frontend, call, 0U, &root) ||
        !r_ast_ref_child(body->frontend, call, 1U, &suffix) ||
        !r_ast_view_kind(body->frontend, suffix, R_SYNTAX_METHOD_CALL_SUFFIX, &member_view) ||
        !r_scope_first_name(body->frontend, root, &name, 0U))
        return true;
    member = suffix;
    const RSymbolId symbol = r_body_resolve_name(body, &name, &issue);
    const RSemanticSymbol *group = r_body_symbol(body, symbol);
    if (group == NULL || !group->is_task_scope)
        return true;
    *handled = true;
    if (!r_scope_first_name(body->frontend, member, &operation, 0U))
        return r_body_invariant(body);
    /* R-STMT-0018: first_until and all_until bound the readiness wait by a monotonic instant;
       vacancy and vacancy_until wait until the group has a free slot (L25.3). */
    const bool first_until =
        r_semantic_token_text_equal_owned(body->frontend, &operation, "first_until");
    const bool all_until =
        r_semantic_token_text_equal_owned(body->frontend, &operation, "all_until");
    const bool vacancy_until =
        r_semantic_token_text_equal_owned(body->frontend, &operation, "vacancy_until");
    const bool vacancy =
        vacancy_until || r_semantic_token_text_equal_owned(body->frontend, &operation, "vacancy");
    const bool until = first_until || all_until || vacancy_until;
    const bool first =
        first_until || r_semantic_token_text_equal_owned(body->frontend, &operation, "first");
    if (!first && !all_until && !vacancy &&
        !r_semantic_token_text_equal_owned(body->frontend, &operation, "all"))
        return r_body_diagnostic(body,
                                 "R-DIAG-ASYNC-001",
                                 "R-STMT-0017",
                                 "task group supports await group.all(), group.first(&tasks...), "
                                 "group.vacancy(), group.all_until(deadline), "
                                 "group.first_until(deadline, &tasks...) and "
                                 "group.vacancy_until(deadline)",
                                 member_view.span);
    if ((first || until) && is_statement)
        return r_body_diagnostic(body,
                                 until ? "R-DIAG-USE-001" : "R-DIAG-ASYNC-001",
                                 until ? "R-STMT-0018" : "R-STMT-0017",
                                 until
                                     ? "a deadline-bounded group wait reports whether the "
                                       "deadline passed and requires a value context"
                                     : "group.first returns an index and requires a value context",
                                 view.span);
    RHirVector children = {0};
    bool success = false;
    if (r_ast_find_direct_child(body->frontend, suffix, R_SYNTAX_ARGUMENT_LIST, &args)) {
        RAstNodeView args_view;
        if (!r_ast_ref_view(body->frontend, args, &args_view))
            goto cleanup;
        bool deadline_seen = false;
        for (uint32_t i = 0U; i < args_view.child_count; ++i) {
            RAstRef arg;
            RAstTokenView token;
            RExpressionResult value;
            if (!r_ast_ref_child(body->frontend, args, i, &arg))
                goto cleanup;
            if (r_ast_ref_token(body->frontend, arg, &token))
                continue;
            if (until && !deadline_seen) {
                /* The deadline is an instant of the monotonic clock. */
                RTypeId instant;
                deadline_seen = true;
                if (!r_semantic_intern_named_standard_type(
                        body->frontend, "std.time::instant", &instant) ||
                    !r_body_lower_value(body, arg, instant, &value))
                    goto cleanup;
                if (!value.valid) {
                    success = true;
                    goto cleanup;
                }
                if (r_semantic_value_type(body->frontend, value.type) != instant) {
                    success = r_body_diagnostic(body,
                                                "R-DIAG-TYPE-001",
                                                "R-STMT-0018",
                                                "the deadline of a group wait is a "
                                                "std.time::instant",
                                                view.span);
                    goto cleanup;
                }
                if (!r_hir_vector_push(body->frontend, &children, value.node))
                    goto cleanup;
                continue;
            }
            RAstTokenView task_name;
            if (vacancy) {
                success = r_body_diagnostic(body,
                                            "R-DIAG-ASYNC-001",
                                            "R-STMT-0018",
                                            "group.vacancy takes no arguments; "
                                            "group.vacancy_until takes a deadline",
                                            view.span);
                goto cleanup;
            }
            if (!r_scope_first_name(body->frontend, arg, &task_name, 0U)) {
                success = r_body_diagnostic(body,
                                            "R-DIAG-ASYNC-001",
                                            "R-STMT-0017",
                                            "group.first requires named member tasks",
                                            view.span);
                goto cleanup;
            }
            RBodyLookupIssue task_issue;
            const RSymbolId task_id = r_body_resolve_name(body, &task_name, &task_issue);
            const RSemanticSymbol *task_symbol = r_body_symbol(body, task_id);
            if (task_symbol == NULL || task_symbol->task_scope != symbol) {
                success = r_body_diagnostic(body,
                                            "R-DIAG-ASYNC-001",
                                            "R-STMT-0017",
                                            "selected task does not belong to this group",
                                            view.span);
                goto cleanup;
            }
            RTypeId borrowed_task;
            if (!r_semantic_intern_derived_type(body->frontend,
                                                R_SEMANTIC_TYPE_BORROW,
                                                task_symbol->type,
                                                0U,
                                                0U,
                                                R_SEMANTIC_TYPE_FLAG_SHARED,
                                                &borrowed_task) ||
                !r_body_lower_value(body, arg, borrowed_task, &value))
                goto cleanup;
            if (!value.valid) {
                success = true;
                goto cleanup;
            }
            const RHirNode *borrow = r_body_hir_node(body, value.node);
            const RHirNode *place =
                borrow != NULL && borrow->kind == R_HIR_BORROW && borrow->child_count == 1U
                    ? r_body_hir_node(body, body->frontend->hir_children[borrow->first_child])
                    : NULL;
            const RSemanticSymbol *task = place == NULL ? NULL : r_body_symbol(body, place->symbol);
            if (place == NULL || place->kind != R_HIR_PLACE || task == NULL ||
                r_semantic_value_kind(body->frontend, task->type) != R_SEMANTIC_TYPE_TASK) {
                success = r_body_diagnostic(body,
                                            "R-DIAG-ASYNC-001",
                                            "R-STMT-0017",
                                            "group.first arguments shall borrow named member tasks",
                                            view.span);
                goto cleanup;
            }
            if (!r_hir_vector_push(body->frontend, &children, value.node))
                goto cleanup;
        }
    }
    const size_t members = children.count - (until && children.count != 0U ? 1U : 0U);
    if ((until && children.count == 0U) || (first && members == 0U) || (!first && members != 0U)) {
        success = r_body_diagnostic(body,
                                    "R-DIAG-ASYNC-001",
                                    until || vacancy ? "R-STMT-0018" : "R-STMT-0017",
                                    vacancy ? "group.vacancy takes no arguments; "
                                              "group.vacancy_until takes a deadline"
                                    : until ? "group.all_until takes a deadline; "
                                              "group.first_until takes a deadline and members"
                                            : "group.all takes no arguments; group.first requires "
                                              "members",
                                    view.span);
        goto cleanup;
    }
    RTypeId type;
    if (!r_semantic_type_from_token(body->frontend,
                                    first_until ? R_TOKEN_KW_USIZE
                                    : first     ? R_TOKEN_KW_USIZE
                                    : until     ? R_TOKEN_KW_BOOL
                                                : R_TOKEN_KW_VOID,
                                    &type) ||
        (first_until &&
         !r_semantic_intern_type(body->frontend, R_SEMANTIC_TYPE_OPTION, type, &type)) ||
        !r_body_append_node(body,
                            R_HIR_TASK_SCOPE_WAIT,
                            view.span,
                            type,
                            symbol,
                            R_TOKEN_KW_AWAIT,
                            (first ? 1U : 0U) | (until ? 2U : 0U) | (vacancy ? 8U : 0U),
                            false,
                            &children,
                            &result->node))
        goto cleanup;
    result->type = type;
    result->valid = true;
    /* An all-members barrier observes terminal publication, after child payload cleanup; a
       bounded one or a vacancy may return first, so the loans remain until the group is known
       drained. */
    if (!first && !until && !vacancy)
        r_body_symbol_mut(body, symbol)->scope_loans = 0U;
    success = true;
cleanup:
    r_context_free(body->frontend, children.items);
    return success;
}

/* R-STMT-0018: `group.cancel_all()` requests cancellation of every member whose observation is
 * still unconsumed and consumes it; supervision, loans and slots remain until terminal
 * acknowledgement, which `await group.all()` or the group exit observes. */
static bool r_scope_lower_cancel_all(RBodyContext *body,
                                     RSymbolId group,
                                     const RAstTokenView *name,
                                     RAstRef suffix,
                                     RSourceSpan span,
                                     RExpressionResult *result) {
    RAstRef arguments;
    RAstNodeView arguments_view;
    RTypeId void_type;
    if (!r_semantic_token_text_equal_owned(body->frontend, name, "cancel_all"))
        return r_body_diagnostic(
            body,
            "R-DIAG-ASYNC-001",
            "R-STMT-0018",
            "a task group name takes group.cancel_all(), or all, first, vacancy, all_until, "
            "first_until and vacancy_until under await",
            span);
    if (r_ast_find_direct_child(body->frontend, suffix, R_SYNTAX_ARGUMENT_LIST, &arguments) &&
        r_ast_ref_view(body->frontend, arguments, &arguments_view)) {
        for (uint32_t index = 0U; index < arguments_view.child_count; ++index) {
            RAstRef argument;
            RAstTokenView token;
            if (r_ast_ref_child(body->frontend, arguments, index, &argument) &&
                !r_ast_ref_token(body->frontend, argument, &token))
                return r_body_diagnostic(body,
                                         "R-DIAG-ASYNC-001",
                                         "R-STMT-0018",
                                         "group.cancel_all takes no arguments",
                                         span);
        }
    }
    if (!r_semantic_type_from_token(body->frontend, R_TOKEN_KW_VOID, &void_type) ||
        !r_body_append_node(body,
                            R_HIR_TASK_SCOPE_CLOSE,
                            span,
                            void_type,
                            group,
                            R_TOKEN_KW_DROP,
                            0U,
                            false,
                            NULL,
                            &result->node))
        return false;
    for (size_t index = 0U; index < body->active_symbols.count; ++index) {
        RSemanticSymbol *member = r_body_symbol_mut(body, body->active_symbols.items[index]);
        if (member != NULL && member->task_scope == group &&
            (member->object_state == R_SEMANTIC_OBJECT_STATE_INITIALIZED ||
             member->object_state == R_SEMANTIC_OBJECT_STATE_SELECT_PENDING))
            member->object_state = R_SEMANTIC_OBJECT_STATE_MOVED;
    }
    result->type = void_type;
    result->valid = true;
    return true;
}

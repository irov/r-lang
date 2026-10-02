/* R-EXPR-0014: a conditional expression is a fork. It shall not occur at any depth within a
   call argument or a return operand; the selected value is named first or the fork becomes an
   `if`. A block nested in such an operand is a lambda body and starts a new statement context. */
static bool r_conditional_restricts_children(const RFrontendContext *context,
                                             RAstRef node,
                                             const RAstNodeView *view,
                                             bool restricted) {
    switch (view->kind) {
    case R_SYNTAX_BLOCK:
        return false;
    case R_SYNTAX_ARGUMENT_LIST:
    case R_SYNTAX_STANDARD_TYPE_CALL:
        return true;
    case R_SYNTAX_JUMP_STATEMENT:
        return restricted || r_ast_has_direct_token(context, node, R_TOKEN_KW_RETURN);
    case R_SYNTAX_PRIMARY_EXPRESSION:
        return restricted || r_ast_has_direct_token(context, node, R_TOKEN_KW_PANIC);
    default:
        return restricted;
    }
}

static bool r_conditional_validate_positions(RFrontendContext *context) {
    for (size_t source = 0U; source < context->source_count; ++source) {
        RAstRef root;
        RTypeVector pending = {0};
        RTypeVector restricted = {0};
        bool success = false;
        if (!r_semantic_root_ref(context, (RSourceId)source + 1U, &root) ||
            !r_type_vector_push(context, &pending, root.node) ||
            !r_type_vector_push(context, &restricted, 0U)) {
            goto cleanup;
        }
        while (pending.count != 0U) {
            const RAstRef parent = {root.source, pending.items[--pending.count]};
            bool inside = restricted.items[--restricted.count] != 0U;
            RAstNodeView view;
            if (!r_ast_ref_view(context, parent, &view)) {
                goto cleanup;
            }
            if (view.kind == R_SYNTAX_CONDITIONAL_EXPRESSION && inside) {
                if (!r_semantic_add_diagnostic(
                        context,
                        "R-DIAG-FLOW-001",
                        "R-EXPR-0014",
                        "a conditional expression shall not occur in a call argument or return "
                        "operand; name the selected value first or use if",
                        view.span)) {
                    goto cleanup;
                }
                /* One diagnostic per fork: its arms are checked as ordinary expressions. */
                inside = false;
            }
            inside = r_conditional_restricts_children(context, parent, &view, inside);
            /* Children are pushed in reverse so that diagnostics follow source order. */
            for (uint32_t index = view.child_count; index != 0U; --index) {
                RAstRef child;
                if (!r_ast_ref_child(context, parent, index - 1U, &child) ||
                    !r_type_vector_push(context, &pending, child.node) ||
                    !r_type_vector_push(context, &restricted, inside ? 1U : 0U)) {
                    goto cleanup;
                }
            }
        }
        success = true;
    cleanup:
        r_context_free(context, pending.items);
        r_context_free(context, restricted.items);
        if (!success) {
            return false;
        }
    }
    return true;
}

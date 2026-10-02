/* R-STMT-0004 (L37.2): loop labels. `name: while (...)` and `name: for (...)` give a loop a
 * label that `break name;` and `continue name;` inside it use. A jump whose labeled loop is the
 * target an ordinary jump would take is that jump. Any other leaves every scope, loop and switch
 * up to the labeled loop at once: the HIR jump names its target loop (loop_target), and its
 * drops, finally routes and recorded object states are those of a jump to that loop. */

static bool r_label_mark_broken(RBodyContext *body, size_t loop_index) {
    for (size_t index = body->loop_label_count; index != 0U; --index) {
        if (body->loop_labels[index - 1U].loop_index == loop_index) {
            body->loop_labels[index - 1U].broken = true;
            return true;
        }
    }
    return false;
}

static bool r_label_loop_broken(const RBodyContext *body, size_t loop_index) {
    for (size_t index = body->loop_label_count; index != 0U; --index) {
        if (body->loop_labels[index - 1U].loop_index == loop_index) {
            return body->loop_labels[index - 1U].broken;
        }
    }
    return false;
}

/* `name: while (...)` or `name: for (...)`. */
static bool r_label_lower_statement(RBodyContext *body,
                                    RAstRef statement,
                                    size_t scope_base,
                                    RHirNodeId *node_id,
                                    bool *always_returns) {
    RFrontendContext *context = body->frontend;
    RAstTokenView name;
    RAstRef loop;
    RLoopLabel label = {0};

    *node_id = R_HIR_NODE_ID_INVALID;
    *always_returns = false;
    if (!r_ast_direct_token(context, statement, 0U, NULL, &name) ||
        !r_body_ast_single_node_child(context, statement, &loop)) {
        return r_body_invariant(body);
    }
    for (size_t index = 0U; index < body->loop_label_count; ++index) {
        if (body->loop_labels[index].name == name.intern_id) {
            return r_body_diagnostic(body,
                                     "R-DIAG-NAME-002",
                                     "R-STMT-0004",
                                     "a loop label repeats the label of an enclosing loop",
                                     name.span);
        }
    }
    if (!r_grow_array(context,
                      (void **)&body->loop_labels,
                      &body->loop_label_capacity,
                      sizeof(*body->loop_labels),
                      body->loop_label_count + 1U)) {
        return false;
    }
    label.name = name.intern_id;
    label.break_depth = body->active_break_target_kinds.count;
    label.loop_index = body->active_loop_scope_bases.count;
    body->loop_labels[body->loop_label_count++] = label;
    const bool lowered = r_body_lower_statement(body, loop, scope_base, node_id, always_returns);
    body->loop_label_count -= 1U;
    return lowered;
}

/* `break name;` or `continue name;`. */
static bool r_label_lower_jump(RBodyContext *body,
                               RTokenKind kind,
                               const RAstTokenView *name,
                               RSourceSpan span,
                               RHirNodeId *node_id,
                               bool *always_returns) {
    size_t index = body->loop_label_count;

    *node_id = R_HIR_NODE_ID_INVALID;
    *always_returns = false;
    while ((index != 0U) && (body->loop_labels[index - 1U].name != name->intern_id)) {
        index -= 1U;
    }
    if (index == 0U) {
        return r_body_diagnostic(body,
                                 "R-DIAG-FLOW-001",
                                 "R-STMT-0004",
                                 "no enclosing loop has this label",
                                 name->span);
    }
    const RLoopLabel label = body->loop_labels[index - 1U];
    if (label.loop_index >= body->active_loop_scope_bases.count) {
        return r_body_invariant(body);
    }
    /* The labeled loop is the target an ordinary jump would take. */
    if (((kind == R_TOKEN_KW_BREAK) &&
         (label.break_depth + 1U == body->active_break_target_kinds.count)) ||
        ((kind == R_TOKEN_KW_CONTINUE) &&
         (label.loop_index + 1U == body->active_loop_scope_bases.count))) {
        return r_body_lower_structured_jump(body, kind, span, node_id, always_returns);
    }
    return r_body_lower_targeted_jump(
        body, kind, span, label.loop_index, node_id, always_returns);
}

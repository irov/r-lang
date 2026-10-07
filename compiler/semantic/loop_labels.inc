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

/* R-STMT-0024 (L46): `name: switch (value) { ... }` with `continue name (next);` is lowered as
 * `{ T selector = value; while (true) { switch (selector) { ... } break; } }`. The hidden loop
 * carries the label, so `break name;` leaves it, `continue name (next);` assigns the selector and
 * continues it, and its object states reach the fixed point of an ordinary loop; an unlabeled
 * `continue;` passes over it to the loop around the switch. The C17 emitter prints the loop and
 * switch as written: a dispatch replicated after every clause measured slower (matrix, L46). */

typedef struct RDispatchLoop {
    RAstRef switch_ref;
    RSymbolId selector;
    RTypeId type;
    RSourceSpan span;
} RDispatchLoop;

/* Whether the loop at `loop_index` is the hidden loop of a labeled switch. */
static bool r_dispatch_loop(const RBodyContext *body, size_t loop_index) {
    for (size_t index = body->loop_label_count; index != 0U; --index) {
        if ((body->loop_labels[index - 1U].loop_index == loop_index) &&
            body->loop_labels[index - 1U].dispatch) {
            return true;
        }
    }
    return false;
}

/* Whether `ref` holds `continue name (value);` for the label `name`. An unreadable node counts,
   so that the answer errs toward a selection anew. */
static bool r_dispatch_ast_selects(RBodyContext *body, RAstRef ref, uint32_t name, uint32_t depth) {
    RFrontendContext *context = body->frontend;
    RAstNodeView view;

    if ((depth > r_frontend_tree_depth_limit(context)) || !r_ast_ref_view(context, ref, &view)) {
        return true;
    }
    if (view.kind == R_SYNTAX_JUMP_STATEMENT) {
        RAstTokenView first;
        RAstTokenView label;
        RAstRef value;
        return r_body_ast_first_token(context, ref, &first) &&
               (first.kind == R_TOKEN_KW_CONTINUE) &&
               r_ast_direct_token(context, ref, 1U, NULL, &label) &&
               (label.kind == R_TOKEN_IDENTIFIER) && (label.intern_id == name) &&
               r_ast_find_direct_child(context, ref, R_SYNTAX_EXPRESSION, &value);
    }
    for (uint32_t index = 0U; index < view.child_count; ++index) {
        RAstRef child;
        RAstTokenView token;
        if (!r_ast_ref_child(context, ref, index, &child)) {
            return true;
        }
        if (!r_ast_ref_token(context, child, &token) &&
            r_dispatch_ast_selects(body, child, name, depth + 1U)) {
            return true;
        }
    }
    return false;
}

/* R-STMT-0024 (L46): the back edge of the hidden loop of a labeled switch is a selection anew, not
   the end of the switch, which leaves the loop. A change inside the switch reaches the next
   selection only through `continue name (value);` in its clause or in a later one, which a
   fallthrough or a nested loop may reach; the operand, evaluated once before the loop, is no
   use of a later pass, so the uses that matter begin after it (`clauses_start`). */
static bool r_dispatch_selects_after(RBodyContext *body,
                                     size_t loop_index,
                                     RAstRef switch_ref,
                                     RSourceSpan mutation_span,
                                     RSourceSpan *clauses_start) {
    RFrontendContext *context = body->frontend;
    RAstNodeView view;
    RAstNodeView expression_view;
    RAstRef expression;
    uint32_t name = 0U;

    for (size_t index = body->loop_label_count; index != 0U; --index) {
        if ((body->loop_labels[index - 1U].loop_index == loop_index) &&
            body->loop_labels[index - 1U].dispatch) {
            name = body->loop_labels[index - 1U].name;
            break;
        }
    }
    if ((name == 0U) || !r_ast_ref_view(context, switch_ref, &view) ||
        !r_ast_find_direct_child(context, switch_ref, R_SYNTAX_EXPRESSION, &expression) ||
        !r_ast_ref_view(context, expression, &expression_view)) {
        *clauses_start = view.span;
        clauses_start->end = clauses_start->start;
        return true;
    }
    *clauses_start = expression_view.span;
    clauses_start->start = clauses_start->end;
    for (uint32_t index = 0U; index < view.child_count; ++index) {
        RAstRef child;
        RAstTokenView token;
        RAstNodeView child_view;
        if (!r_ast_ref_child(context, switch_ref, index, &child)) {
            return true;
        }
        if (r_ast_ref_token(context, child, &token) ||
            !r_ast_ref_view(context, child, &child_view) || (child.node == expression.node) ||
            (child_view.span.end <= mutation_span.start)) {
            continue;
        }
        if (r_dispatch_ast_selects(body, child, name, 0U)) {
            return true;
        }
    }
    return false;
}

static bool r_dispatch_condition(RBodyContext *body, void *user, RExpressionResult *result) {
    const RDispatchLoop *loop = (const RDispatchLoop *)user;
    RTypeId bool_type;
    return r_semantic_type_from_token(body->frontend, R_TOKEN_KW_BOOL, &bool_type) &&
           r_iteration_literal(body, loop->span, bool_type, UINT64_C(1), result);
}

/* One pass: the switch over the selector, then the break that ends the loop after a clause
   completes; so the end of the pass is never reached. */
static bool
r_dispatch_body(RBodyContext *body, void *user, RHirNodeId *node_id, bool *always_returns) {
    const RDispatchLoop *loop = (const RDispatchLoop *)user;
    RExpressionResult operand = {0};
    RHirNodeId storage[2];
    RHirVector statements = {storage, 0U, 2U};
    RHirNodeId switch_node = R_HIR_NODE_ID_INVALID;
    RHirNodeId break_node = R_HIR_NODE_ID_INVALID;
    RTypeId void_type;
    bool switch_returns = false;

    *node_id = R_HIR_NODE_ID_INVALID;
    *always_returns = true;
    if (!r_semantic_type_from_token(body->frontend, R_TOKEN_KW_VOID, &void_type) ||
        !r_body_append_node(body,
                            R_HIR_PLACE,
                            loop->span,
                            loop->type,
                            loop->selector,
                            R_TOKEN_INVALID,
                            UINT64_C(0),
                            true,
                            NULL,
                            &operand.node)) {
        return false;
    }
    operand.type = loop->type;
    operand.symbol = loop->selector;
    operand.borrow_origin = loop->selector;
    operand.is_place = true;
    operand.valid = true;
    body->dispatch_operand = &operand;
    body->switch_reaches_end = true;
    const bool lowered = r_body_lower_switch(body, loop->switch_ref, &switch_node, &switch_returns);
    body->dispatch_operand = NULL;
    if (!lowered) {
        return false;
    }
    if (switch_node != R_HIR_NODE_ID_INVALID) {
        storage[statements.count++] = switch_node;
    }
    /* The clauses that select anew end in continue, which a switch does not count as returning;
       the loop ends only where control may leave the switch at its end. */
    if (!switch_returns && body->switch_reaches_end) {
        if (!r_body_lower_loop_break(body, loop->span, &break_node)) {
            return false;
        }
        if (break_node != R_HIR_NODE_ID_INVALID) {
            storage[statements.count++] = break_node;
        }
    }
    return r_body_append_node(body,
                              R_HIR_BLOCK,
                              loop->span,
                              void_type,
                              R_SYMBOL_ID_INVALID,
                              R_TOKEN_INVALID,
                              UINT64_C(0),
                              false,
                              &statements,
                              node_id);
}

/* `name: switch (value) { ... }`: the selector, then the hidden loop under the label. */
static bool r_dispatch_lower(RBodyContext *body,
                             const RAstTokenView *name,
                             RAstRef switch_ref,
                             RHirNodeId *node_id,
                             bool *always_returns) {
    RFrontendContext *context = body->frontend;
    RAstNodeView view;
    RAstRef expression;
    RExpressionResult value = {0};
    RHirVector wrapper = {0};
    RSymbolId selector = R_SYMBOL_ID_INVALID;
    RHirNodeId loop_node = R_HIR_NODE_ID_INVALID;
    RTypeId void_type;
    RLoopLabel label = {0};
    bool success = false;

    if (!r_ast_ref_view(context, switch_ref, &view) ||
        !r_ast_find_direct_child(context, switch_ref, R_SYNTAX_EXPRESSION, &expression) ||
        !r_semantic_type_from_token(context, R_TOKEN_KW_VOID, &void_type)) {
        return r_body_invariant(body);
    }
    /* After an error the statement counts as not completing, so that the end of the function
       is not reported again (defect L46-7). */
    *always_returns = true;
    if (!r_body_lower_value(body, expression, R_TYPE_ID_INVALID, &value)) {
        return false;
    }
    if (!value.valid) {
        return r_body_cascade(body);
    }
    const RTypeId type = r_semantic_value_type(context, value.type);
    const RSemanticTypeKind kind = r_semantic_value_kind(context, type);
    const RSemanticAggregate *standard_enum = NULL;
    bool is_signed;
    uint32_t width;
    /* A fieldless enum of the library selects as a source enum does (defect L46-6). */
    if ((kind == R_SEMANTIC_TYPE_STANDARD) &&
        !r_semantic_prepare_standard_enum(context, type, &standard_enum)) {
        return false;
    }
    if (!r_semantic_integer_properties(kind, &is_signed, &width) &&
        (kind != R_SEMANTIC_TYPE_CHAR) && (standard_enum == NULL) &&
        ((kind != R_SEMANTIC_TYPE_ENUM) || (r_semantic_tagged_enum(context, type) != NULL))) {
        return r_body_diagnostic(body,
                                 "R-DIAG-TYPE-001",
                                 "R-STMT-0024",
                                 "a labeled switch selects by an integer, char or fieldless enum "
                                 "value",
                                 view.span);
    }
    if (!r_body_materialize_switch_value(body, name->span, &value, &wrapper, &selector) ||
        !r_grow_array(context,
                      (void **)&body->loop_labels,
                      &body->loop_label_capacity,
                      sizeof(*body->loop_labels),
                      body->loop_label_count + 1U)) {
        goto cleanup;
    }
    label.name = name->intern_id;
    label.break_depth = body->active_break_target_kinds.count;
    label.loop_index = body->active_loop_scope_bases.count;
    label.dispatch = true;
    label.selector = selector;
    body->loop_labels[body->loop_label_count++] = label;
    RDispatchLoop loop = {switch_ref, selector, type, view.span};
    const bool was_poisoned = body->poisoned;
    const bool lowered = r_body_lower_loop(body,
                                           view.span,
                                           switch_ref,
                                           r_dispatch_condition,
                                           r_dispatch_body,
                                           &loop,
                                           &loop_node,
                                           always_returns);
    body->loop_label_count -= 1U;
    if (body->poisoned && !was_poisoned) {
        /* An error inside the switch already explains it; the loop of an erroneous body leaves
           its end open, which would report the end of the function again. */
        *always_returns = true;
    }
    if (!lowered || ((loop_node != R_HIR_NODE_ID_INVALID) &&
                     !r_hir_vector_push(context, &wrapper, loop_node))) {
        goto cleanup;
    }
    success = r_body_append_node(body,
                                 R_HIR_BLOCK,
                                 view.span,
                                 void_type,
                                 R_SYMBOL_ID_INVALID,
                                 R_TOKEN_INVALID,
                                 UINT64_C(0),
                                 false,
                                 &wrapper,
                                 node_id);

cleanup:
    r_context_free(context, wrapper.items);
    return success;
}

/* `continue name (value);`: the selector of the labeled switch takes the value, then the hidden
   loop continues, leaving the scopes between as a labeled continue does (R-STMT-0004). */
static bool r_dispatch_lower_continue(RBodyContext *body,
                                      const RAstTokenView *name,
                                      RAstRef value_ref,
                                      RSourceSpan span,
                                      RHirNodeId *node_id,
                                      bool *always_returns) {
    RFrontendContext *context = body->frontend;
    size_t index = body->loop_label_count;
    RExpressionResult place = {0};
    RExpressionResult assigned = {0};
    RHirNodeId storage[2];
    RHirVector statements = {storage, 0U, 2U};
    RHirNodeId jump_node = R_HIR_NODE_ID_INVALID;
    RTypeId void_type;

    *node_id = R_HIR_NODE_ID_INVALID;
    *always_returns = false;
    while ((index != 0U) && (body->loop_labels[index - 1U].name != name->intern_id)) {
        index -= 1U;
    }
    if (index == 0U) {
        return r_body_diagnostic(
            body, "R-DIAG-FLOW-001", "R-STMT-0004", "no enclosing loop has this label", name->span);
    }
    const RLoopLabel label = body->loop_labels[index - 1U];
    if (!label.dispatch) {
        return r_body_diagnostic(body,
                                 "R-DIAG-FLOW-001",
                                 "R-STMT-0024",
                                 "continue with a value names a labeled switch",
                                 span);
    }
    const RSemanticSymbol *selector = r_body_symbol(body, label.selector);
    if ((selector == NULL) || (label.loop_index >= body->active_loop_scope_bases.count) ||
        !r_semantic_type_from_token(context, R_TOKEN_KW_VOID, &void_type) ||
        !r_body_append_node(body,
                            R_HIR_PLACE,
                            span,
                            selector->type,
                            label.selector,
                            R_TOKEN_INVALID,
                            UINT64_C(0),
                            true,
                            NULL,
                            &place.node)) {
        return selector == NULL ? r_body_invariant(body) : false;
    }
    place.type = selector->type;
    place.symbol = label.selector;
    place.borrow_origin = label.selector;
    place.is_place = true;
    place.valid = true;
    if (!r_body_lower_assignment(body, span, R_TOKEN_EQUAL, &place, value_ref, &assigned)) {
        return false;
    }
    if (!assigned.valid) {
        *always_returns = true;
        return true;
    }
    RHirVector assignment = {&assigned.node, 1U, 1U};
    if (!r_body_append_node(body,
                            R_HIR_EXPRESSION_STATEMENT,
                            span,
                            void_type,
                            R_SYMBOL_ID_INVALID,
                            R_TOKEN_INVALID,
                            UINT64_C(0),
                            false,
                            &assignment,
                            &storage[statements.count])) {
        return false;
    }
    statements.count += 1U;
    if (!((label.loop_index + 1U == body->active_loop_scope_bases.count)
              ? r_body_lower_structured_jump(
                    body, R_TOKEN_KW_CONTINUE, span, &jump_node, always_returns)
              : r_body_lower_targeted_jump(body,
                                           R_TOKEN_KW_CONTINUE,
                                           span,
                                           label.loop_index,
                                           &jump_node,
                                           always_returns))) {
        return false;
    }
    if (jump_node != R_HIR_NODE_ID_INVALID) {
        storage[statements.count++] = jump_node;
    }
    return r_body_append_node(body,
                              R_HIR_BLOCK,
                              span,
                              void_type,
                              R_SYMBOL_ID_INVALID,
                              R_TOKEN_INVALID,
                              UINT64_C(0),
                              false,
                              &statements,
                              node_id);
}

/* An unlabeled `continue;` takes the innermost loop that is not the hidden loop of a labeled
   switch. */
static bool r_dispatch_lower_plain_continue(RBodyContext *body,
                                            RSourceSpan span,
                                            RHirNodeId *node_id,
                                            bool *always_returns) {
    size_t index = body->active_loop_scope_bases.count;

    *node_id = R_HIR_NODE_ID_INVALID;
    *always_returns = false;
    while ((index != 0U) && r_dispatch_loop(body, index - 1U)) {
        index -= 1U;
    }
    if (index == 0U) {
        return r_body_diagnostic(
            body, "R-DIAG-FLOW-001", "R-STMT-0004", "structured continue is outside a loop", span);
    }
    return r_body_lower_targeted_jump(
        body, R_TOKEN_KW_CONTINUE, span, index - 1U, node_id, always_returns);
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
    RAstNodeView loop_view;
    if (r_ast_ref_view(context, loop, &loop_view) &&
        (loop_view.kind == R_SYNTAX_SWITCH_STATEMENT)) {
        (void)scope_base;
        return r_dispatch_lower(body, &name, loop, node_id, always_returns);
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
    if (label.dispatch && (kind == R_TOKEN_KW_CONTINUE)) {
        *always_returns = true;
        return r_body_diagnostic(body,
                                 "R-DIAG-FLOW-001",
                                 "R-STMT-0024",
                                 "continue of a labeled switch takes the value to select by",
                                 span);
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

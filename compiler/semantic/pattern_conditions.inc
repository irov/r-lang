/* R-STMT-0002 (L37.1): `if (value is pattern)` and `while (value is pattern)` test one match
 * pattern (R-EXPR-0031) as the whole condition. The value is evaluated once (per iteration); a
 * value that is not a place lives in a hidden object. Bindings without `move` are read-only
 * aliases of the tested value; `move` bindings need an owned value, a temporary or `move place`,
 * and take their parts when the pattern matches. The bindings exist only in the block that the
 * match selects; the hidden object is destroyed at the end of each branch. */

/* The pattern test that is the whole condition, if it is one. */
static bool r_pattern_condition(RBodyContext *body, RAstRef condition, RAstRef *test) {
    RAstRef group = {0};
    RAstRef leaf = {0};
    RAstNodeView view;

    *test = (RAstRef){0};
    if (!r_body_ast_single_node_child(body->frontend, condition, &group) ||
        !r_ast_ref_view(body->frontend, group, &view)) {
        return false;
    }
    if (view.kind == R_SYNTAX_PATTERN_TEST) {
        *test = group;
        return true;
    }
    /* A binary expression of one operand and no operator; `x is p && y` is not a pattern
       condition, and its pattern test is rejected where it is lowered. */
    if ((view.kind != R_SYNTAX_BINARY_EXPRESSION) || (view.child_count != 1U) ||
        !r_body_ast_single_node_child(body->frontend, group, &leaf) ||
        !r_ast_view_kind(body->frontend, leaf, R_SYNTAX_PATTERN_TEST, &view)) {
        return false;
    }
    *test = leaf;
    return true;
}

typedef struct RPatternCondition {
    RExpressionResult place;
    RExpressionResult provenance;
    RMatchPattern *pattern;
    RSymbolId temporary;
    size_t arm_base;
    RSourceSpan span;
} RPatternCondition;

/* The tested value as a stable place, and the checked pattern. `prefix` receives the hidden
   object of a value that is not a place. `pattern` stays NULL after a diagnostic. */
static bool r_pattern_prepare(RBodyContext *body,
                              RAstRef test,
                              size_t outer_base,
                              RHirVector *prefix,
                              RPatternCondition *condition) {
    RAstNodeView view;
    RAstRef operand = {0};
    RAstRef pattern = {0};

    (void)memset(condition, 0, sizeof(*condition));
    if (!r_ast_ref_view(body->frontend, test, &view)) {
        return r_body_invariant(body);
    }
    condition->span = view.span;
    for (uint32_t index = 0U; index < view.child_count; ++index) {
        RAstRef child;
        RAstNodeView child_view;
        if (!r_ast_ref_child(body->frontend, test, index, &child) ||
            !r_ast_ref_view(body->frontend, child, &child_view)) {
            continue;
        }
        if (child_view.kind == R_SYNTAX_MATCH_PATTERN) {
            pattern = child;
        } else if ((child_view.kind != R_SYNTAX_TOKEN) && (operand.node == 0U)) {
            operand = child;
        }
    }
    if ((operand.node == 0U) || (pattern.node == 0U)) {
        return r_body_invariant(body);
    }
    if (!r_body_lower_expression(body, operand, R_TYPE_ID_INVALID, &condition->place)) {
        return false;
    }
    if (!condition->place.valid) {
        return true;
    }
    condition->provenance = condition->place;
    if (!condition->place.is_place &&
        (!r_iteration_declare_hidden(body,
                                     outer_base,
                                     "$match",
                                     condition->place.type,
                                     test.node,
                                     view.span,
                                     &condition->place,
                                     prefix,
                                     &condition->temporary) ||
         !r_body_symbol_place(body, condition->temporary, view.span, &condition->place))) {
        return false;
    }
    RMatchPattern *checked = NULL;
    if (!r_match_check(body, pattern, condition->place.type, 0U, &checked)) {
        return false;
    }
    if (body->poisoned) {
        r_match_destroy(body, checked);
        return true;
    }
    if (checked->contains_move && (condition->temporary == 0U)) {
        if (!r_semantic_type_is_copy(body->frontend, condition->place.type)) {
            const RSourceSpan span = checked->span;
            r_match_destroy(body, checked);
            return r_match_error(body,
                                 span,
                                 "move payload patterns require (move value) or an owned "
                                 "temporary");
        }
        RExpressionResult copy = condition->place;
        if (!r_body_place_to_value(body, &copy) ||
            !r_iteration_declare_hidden(body,
                                        outer_base,
                                        "$match",
                                        copy.type,
                                        test.node,
                                        view.span,
                                        &copy,
                                        prefix,
                                        &condition->temporary) ||
            !r_body_symbol_place(body, condition->temporary, view.span, &condition->place)) {
            r_match_destroy(body, checked);
            return false;
        }
    }
    condition->pattern = checked;
    condition->arm_base = body->active_symbols.count;
    return true;
}

/* The statements of the selected branch before its block: the parts the `move` bindings take
   and the forgotten partitioned value. */
static bool
r_pattern_commit(RBodyContext *body, const RPatternCondition *condition, RHirVector *statements) {
    if (!condition->pattern->contains_move) {
        return true;
    }
    body->active_symbols.count = condition->arm_base;
    return r_match_commit(body,
                          condition->pattern,
                          condition->place,
                          condition->span,
                          condition->arm_base,
                          statements,
                          &condition->provenance) &&
           r_match_forget_partition(body, condition->temporary, condition->span, statements);
}

/* Drops at the end of a branch that completes: everything from `base` down. */
static bool r_pattern_branch_drops(RBodyContext *body,
                                   size_t base,
                                   RSourceSpan span,
                                   RHirVector *statements) {
    for (size_t index = body->active_symbols.count; index > base; --index) {
        if (!r_body_append_implicit_drop(body, body->active_symbols.items[index - 1U], span, statements)) {
            return false;
        }
    }
    return true;
}

static bool r_pattern_lower_if(RBodyContext *body,
                               RAstRef statement,
                               RAstRef test,
                               RAstRef then_ref,
                               RAstRef else_ref,
                               RHirNodeId *node_id,
                               bool *always_returns) {
    RFrontendContext *context = body->frontend;
    RAstNodeView view;
    RHirVector prefix = {0};
    RHirVector then_statements = {0};
    RHirVector else_statements = {0};
    RPatternCondition condition = {0};
    RExpressionResult tested = {0};
    RSemanticObjectSnapshot *entry_states = NULL;
    RSemanticObjectSnapshot *then_states = NULL;
    RSemanticObjectSnapshot *else_states = NULL;
    size_t entry_count = 0U, then_count = 0U, else_count = 0U;
    RHirNodeId then_block = R_HIR_NODE_ID_INVALID;
    RHirNodeId else_block = R_HIR_NODE_ID_INVALID;
    RHirNodeId then_node, else_node, if_node;
    RHirNodeId children_storage[3];
    RHirVector children = {children_storage, UINT32_C(0), UINT32_C(3)};
    bool then_returns = false;
    bool else_returns = false;
    RTypeId void_type;
    const size_t outer_base = body->active_symbols.count;
    bool success = false;

    *node_id = R_HIR_NODE_ID_INVALID;
    *always_returns = false;
    if (!r_ast_ref_view(context, statement, &view) ||
        !r_semantic_type_from_token(context, R_TOKEN_KW_VOID, &void_type) ||
        !r_pattern_prepare(body, test, outer_base, &prefix, &condition)) {
        goto cleanup;
    }
    if (condition.pattern == NULL) {
        success = true;
        goto cleanup;
    }
    if (!r_body_capture_active_object_states(body, &entry_states, &entry_count) ||
        !r_match_test(body, condition.pattern, condition.place, condition.arm_base, &tested) ||
        !tested.valid) {
        goto cleanup;
    }
    /* The selected branch: the parts the pattern takes, the bindings, the block. */
    if (!r_pattern_commit(body, &condition, &then_statements) ||
        !r_body_lower_block(body, then_ref, false, &then_block, &then_returns) ||
        ((then_block != R_HIR_NODE_ID_INVALID) &&
         !r_hir_vector_push(context, &then_statements, then_block)) ||
        (!then_returns && !r_pattern_branch_drops(body,
                                                  condition.temporary != 0U ? outer_base
                                                                            : condition.arm_base,
                                                  view.span,
                                                  &then_statements))) {
        goto cleanup;
    }
    /* The states before the branch, the hidden tested value included, return for the other
       branch; only the states of the enclosing objects merge after them. */
    body->active_symbols.count = condition.arm_base;
    if (!r_body_capture_active_object_states(body, &then_states, &then_count) ||
        (then_count != entry_count) ||
        !r_body_restore_active_object_states(body, entry_states, entry_count)) {
        goto cleanup;
    }
    /* The other branch: no bindings; an owned tested value is destroyed. */
    if ((else_ref.node != 0U) &&
        (!r_body_lower_block(body, else_ref, false, &else_block, &else_returns) ||
         ((else_block != R_HIR_NODE_ID_INVALID) &&
          !r_hir_vector_push(context, &else_statements, else_block)))) {
        goto cleanup;
    }
    body->active_symbols.count = condition.arm_base;
    if ((!else_returns && (condition.temporary != 0U) &&
         !r_pattern_branch_drops(body, outer_base, view.span, &else_statements)) ||
        !r_body_capture_active_object_states(body, &else_states, &else_count) ||
        (else_count != entry_count)) {
        goto cleanup;
    }
    body->active_symbols.count = outer_base;
    if (!then_returns && !else_returns) {
        if (!r_body_merge_branch_object_states(body, then_states, else_states, outer_base, view.span)) {
            goto cleanup;
        }
    } else if (!then_returns) {
        if (!r_body_restore_active_object_states(body, then_states, outer_base)) {
            goto cleanup;
        }
    } else if (!r_body_restore_active_object_states(body, else_states, outer_base)) {
        goto cleanup;
    }
    if (!r_body_append_node(body,
                            R_HIR_BLOCK,
                            view.span,
                            void_type,
                            R_SYMBOL_ID_INVALID,
                            R_TOKEN_INVALID,
                            UINT64_C(0),
                            false,
                            &then_statements,
                            &then_node) ||
        !r_body_append_node(body,
                            R_HIR_BLOCK,
                            view.span,
                            void_type,
                            R_SYMBOL_ID_INVALID,
                            R_TOKEN_INVALID,
                            UINT64_C(0),
                            false,
                            &else_statements,
                            &else_node)) {
        goto cleanup;
    }
    children_storage[children.count++] = tested.node;
    children_storage[children.count++] = then_node;
    children_storage[children.count++] = else_node;
    if (!r_body_append_node(body,
                            R_HIR_IF,
                            view.span,
                            void_type,
                            R_SYMBOL_ID_INVALID,
                            R_TOKEN_KW_IF,
                            UINT64_C(0),
                            false,
                            &children,
                            &if_node) ||
        !r_hir_vector_push(context, &prefix, if_node) ||
        !r_body_append_node(body,
                            R_HIR_BLOCK,
                            view.span,
                            void_type,
                            R_SYMBOL_ID_INVALID,
                            R_TOKEN_INVALID,
                            UINT64_C(0),
                            false,
                            &prefix,
                            node_id)) {
        goto cleanup;
    }
    *always_returns = (else_ref.node != 0U) && then_returns && else_returns;
    success = true;

cleanup:
    if (condition.pattern != NULL) {
        r_match_destroy(body, condition.pattern);
    }
    body->active_symbols.count = outer_base;
    r_context_free(context, prefix.items);
    r_context_free(context, then_statements.items);
    r_context_free(context, else_statements.items);
    r_context_free(context, entry_states);
    r_context_free(context, then_states);
    r_context_free(context, else_states);
    return success;
}

typedef struct RPatternWhile {
    RAstRef test;
    RAstRef block;
    RSourceSpan span;
} RPatternWhile;

static bool r_pattern_while_condition(RBodyContext *body, void *user, RExpressionResult *result) {
    const RPatternWhile *loop = (const RPatternWhile *)user;
    RTypeId bool_type;
    return r_semantic_type_from_token(body->frontend, R_TOKEN_KW_BOOL, &bool_type) &&
           r_iteration_literal(body, loop->span, bool_type, UINT64_C(1), result);
}

/* One iteration: the tested value, `if (!(test)) break;`, the parts the pattern takes, the
   block and the drops of what the iteration owns. */
static bool
r_pattern_while_body(RBodyContext *body, void *user, RHirNodeId *node_id, bool *always_returns) {
    RFrontendContext *context = body->frontend;
    const RPatternWhile *loop = (const RPatternWhile *)user;
    RHirVector statements = {0};
    RHirVector stop = {0};
    RPatternCondition condition = {0};
    RExpressionResult tested = {0};
    RExpressionResult no = {0};
    RExpressionResult failed = {0};
    RSemanticObjectSnapshot *entry_states = NULL;
    size_t entry_count = 0U;
    RHirNodeId break_node = R_HIR_NODE_ID_INVALID;
    RHirNodeId block_node = R_HIR_NODE_ID_INVALID;
    RHirNodeId stop_node, if_node;
    RHirNodeId children_storage[2];
    RHirVector children = {children_storage, UINT32_C(2), UINT32_C(2)};
    RTypeId bool_type, void_type;
    bool block_returns = false;
    const size_t outer_base = body->active_symbols.count;
    bool success = false;

    *node_id = R_HIR_NODE_ID_INVALID;
    *always_returns = false;
    if (!r_semantic_type_from_token(context, R_TOKEN_KW_BOOL, &bool_type) ||
        !r_semantic_type_from_token(context, R_TOKEN_KW_VOID, &void_type) ||
        !r_pattern_prepare(body, loop->test, outer_base, &statements, &condition)) {
        goto cleanup;
    }
    if (condition.pattern == NULL) {
        success = true;
        goto cleanup;
    }
    if (!r_match_test(body, condition.pattern, condition.place, condition.arm_base, &tested) ||
        !tested.valid || !r_iteration_literal(body, loop->span, bool_type, UINT64_C(0), &no) ||
        !r_iteration_compare(body, loop->span, R_TOKEN_EQUAL_EQUAL, &tested, &no, &failed) ||
        !failed.valid) {
        goto cleanup;
    }
    /* No match: leave the loop; the break destroys the tested value with the other locals. */
    const size_t aliases = body->active_symbols.count;
    body->active_symbols.count = condition.arm_base;
    if (!r_body_capture_active_object_states(body, &entry_states, &entry_count) ||
        !r_body_lower_loop_break(body, loop->span, &break_node) ||
        ((break_node != R_HIR_NODE_ID_INVALID) &&
         !r_hir_vector_push(context, &stop, break_node)) ||
        !r_body_append_node(body,
                            R_HIR_BLOCK,
                            loop->span,
                            void_type,
                            R_SYMBOL_ID_INVALID,
                            R_TOKEN_INVALID,
                            UINT64_C(0),
                            false,
                            &stop,
                            &stop_node) ||
        !r_body_restore_active_object_states(body, entry_states, entry_count)) {
        goto cleanup;
    }
    body->active_symbols.count = aliases;
    children_storage[0] = failed.node;
    children_storage[1] = stop_node;
    if (!r_body_append_node(body,
                            R_HIR_IF,
                            loop->span,
                            void_type,
                            R_SYMBOL_ID_INVALID,
                            R_TOKEN_KW_IF,
                            UINT64_C(0),
                            false,
                            &children,
                            &if_node) ||
        !r_hir_vector_push(context, &statements, if_node)) {
        goto cleanup;
    }
    /* A match: the bindings, then the block. */
    if (!r_pattern_commit(body, &condition, &statements) ||
        !r_body_lower_block(body, loop->block, false, &block_node, &block_returns) ||
        ((block_node != R_HIR_NODE_ID_INVALID) &&
         !r_hir_vector_push(context, &statements, block_node)) ||
        (!block_returns && !r_pattern_branch_drops(body, outer_base, loop->span, &statements)) ||
        !r_body_append_node(body,
                            R_HIR_BLOCK,
                            loop->span,
                            void_type,
                            R_SYMBOL_ID_INVALID,
                            R_TOKEN_INVALID,
                            UINT64_C(0),
                            false,
                            &statements,
                            node_id)) {
        goto cleanup;
    }
    *always_returns = block_returns;
    success = true;

cleanup:
    if (condition.pattern != NULL) {
        r_match_destroy(body, condition.pattern);
    }
    body->active_symbols.count = outer_base;
    r_context_free(context, statements.items);
    r_context_free(context, stop.items);
    r_context_free(context, entry_states);
    return success;
}

static bool r_pattern_lower_while(RBodyContext *body,
                                  RAstRef statement,
                                  RAstRef test,
                                  RAstRef block,
                                  RHirNodeId *node_id,
                                  bool *always_returns) {
    RAstNodeView view;
    RPatternWhile loop;
    if (!r_ast_ref_view(body->frontend, statement, &view)) {
        return r_body_invariant(body);
    }
    loop.test = test;
    loop.block = block;
    loop.span = view.span;
    return r_body_lower_loop(body,
                             view.span,
                             block,
                             r_pattern_while_condition,
                             r_pattern_while_body,
                             &loop,
                             node_id,
                             always_returns);
}

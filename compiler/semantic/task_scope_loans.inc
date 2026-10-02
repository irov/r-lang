/* A group's loans are persistent flow state, independent of observation handles and lexical
 * uses of local view names. Failed starts snapshot this state before any new loan is committed. */
static bool
r_scope_merge_loans(RBodyContext *body, uint32_t left, uint32_t right, uint32_t *result) {
    if (left == right || left == 0U || right == 0U) {
        *result = left == 0U ? right : left;
        return true;
    }
    RProjectedBorrowStateEntryVector entries = {0};
    bool success = false;
    const uint32_t states[] = {left, right};
    for (size_t pass = 0U; pass < 2U; ++pass) {
        const RProjectedBorrowStateEntry *source;
        size_t count;
        if (!r_body_projected_borrow_state_entries(body, states[pass], &source, &count))
            goto cleanup;
        for (size_t i = 0U; i < count; ++i) {
            bool present = false;
            for (size_t j = 0U; j < entries.count; ++j)
                if (entries.items[j].source == source[i].source)
                    present = true;
            if (!present && !r_body_projected_borrow_entry_push(body, &entries, source[i]))
                goto cleanup;
        }
    }
    success = r_body_append_projected_borrow_state(body, &entries, result);
cleanup:
    r_context_free(body->frontend, entries.items);
    return success;
}

static bool
r_scope_record_call(RBodyContext *body, RHirNodeId call_id, const RHirVector *arguments) {
    const RSymbolId group_id = body->current_task_scope;
    const RSemanticSymbol *group = r_body_symbol(body, group_id);
    RProjectedBorrowStateEntryVector entries = {0};
    bool success = false;
    if (group == NULL || !group->is_task_scope)
        return r_body_invariant(body);
    const uint32_t old_state = group->scope_loans;
    const uint32_t start = group->scope_start;
    const RSourceId source = group->module_source;
    for (size_t i = 0U; i < arguments->count; ++i) {
        const RHirNode *argument = r_body_hir_node(body, arguments->items[i]);
        if (argument == NULL)
            goto cleanup;
        if (!r_semantic_type_crosses_await(body->frontend, argument->type))
            continue;
        RSymbolVector origins = {0};
        bool unknown = false;
        if (!r_semantic_append_borrow_origins(body->frontend,
                                              &origins,
                                              argument->borrow_origin,
                                              argument->borrow_origin_set,
                                              argument->borrow_origin_multiple,
                                              &unknown)) {
            r_context_free(body->frontend, origins.items);
            goto cleanup;
        }
        bool invalid = unknown;
        for (size_t j = 0U; j < origins.count; ++j) {
            const RSemanticSymbol *origin = r_body_symbol(body, origins.items[j]);
            if (origin == NULL ||
                (origin->kind == R_SEMANTIC_SYMBOL_LOCAL &&
                 (origin->module_source != source || origin->name_span.start >= start)))
                invalid = true;
        }
        r_context_free(body->frontend, origins.items);
        if (invalid) {
            success = r_body_diagnostic(
                body,
                "R-DIAG-BORROW-002",
                "R-STMT-0017",
                "scoped child borrows shall originate in storage enclosing the task_scope",
                argument->span);
            goto cleanup;
        }
        RProjectedBorrowStateEntry entry = {0};
        entry.source = arguments->items[i];
        entry.destination = call_id;
        if (!r_body_projected_borrow_entry_push(body, &entries, entry))
            goto cleanup;
    }
    uint32_t added, merged;
    if (!r_body_append_projected_borrow_state(body, &entries, &added) ||
        !r_scope_merge_loans(body, old_state, added, &merged))
        goto cleanup;
    r_body_symbol_mut(body, group_id)->scope_loans = merged;
    success = true;
cleanup:
    r_context_free(body->frontend, entries.items);
    return success;
}

static bool r_scope_loans_conflict(RBodyContext *body,
                                   RSymbolId origin,
                                   uint32_t origin_set,
                                   bool multiple,
                                   uint64_t referent,
                                   bool exclusive_only,
                                   bool match_referent,
                                   RSourceSpan span,
                                   const char *rule,
                                   const char *message,
                                   bool *conflict) {
    for (size_t i = 0U; i < body->active_symbols.count; ++i) {
        const RSemanticSymbol *group = r_body_symbol(body, body->active_symbols.items[i]);
        if (group == NULL || !(group->is_task_scope || group->is_thread_scope) ||
            group->scope_loans == 0U)
            continue;
        const RProjectedBorrowStateEntry *loans;
        size_t count;
        if (!r_body_projected_borrow_state_entries(body, group->scope_loans, &loans, &count))
            return false;
        for (size_t j = 0U; j < count; ++j) {
            const RHirNode *loan = r_body_hir_node(body, loans[j].source);
            if (loan == NULL)
                return r_body_invariant(body);
            if (exclusive_only &&
                !r_semantic_type_contains_exclusive_view(body->frontend, loan->type, NULL))
                continue;
            if (match_referent && referent != 0U && loan->borrow_referent != 0U &&
                referent != R_BORROW_REFERENT_UNKNOWN_LIST_NODE &&
                loan->borrow_referent != R_BORROW_REFERENT_UNKNOWN_LIST_NODE &&
                referent != loan->borrow_referent)
                continue;
            if (r_semantic_borrow_origins_intersect(body->frontend,
                                                    origin,
                                                    origin_set,
                                                    multiple,
                                                    loan->borrow_origin,
                                                    loan->borrow_origin_set,
                                                    loan->borrow_origin_multiple)) {
                *conflict = true;
                return r_body_diagnostic(body, "R-DIAG-BORROW-001", rule, message, span);
            }
        }
    }
    return true;
}

/* The scoped start that produced a member's task value: the async start or scoped standard call
   behind a local initializer, possibly wrapped by a value scope or a cast. */
static RHirNodeId r_scope_find_start(RBodyContext *body, RHirNodeId node_id, uint32_t depth) {
    const RHirNode *node = r_body_hir_node(body, node_id);
    RHirNodeId child;
    if (node == NULL || depth > 8U)
        return R_HIR_NODE_ID_INVALID;
    if ((node->kind == R_HIR_ASYNC_START || node->kind == R_HIR_STANDARD_CALL) &&
        node->task_scope != R_SYMBOL_ID_INVALID &&
        r_semantic_value_kind(body->frontend, node->type) == R_SEMANTIC_TYPE_TASK)
        return node_id;
    /* R-MEM-0015: a scoped spawn, and its exact handle through local whole-handle moves. */
    if (node->kind == R_HIR_STANDARD_CALL &&
        node->standard_operation == R_STANDARD_CALL_THREAD_SPAWN_SCOPED)
        return node_id;
    if (node->kind == R_HIR_MOVE && node->child_count == 1U &&
        r_body_hir_child(body, node, 0U, &child)) {
        const RHirNode *place = r_body_hir_node(body, child);
        const RSemanticSymbol *source = (place == NULL || place->kind != R_HIR_PLACE)
                                            ? NULL
                                            : r_body_symbol(body, place->symbol);
        const RHirNode *start =
            source == NULL ? NULL : r_body_hir_node(body, source->scope_start_call);
        if (start != NULL && start->kind == R_HIR_STANDARD_CALL &&
            start->standard_operation == R_STANDARD_CALL_THREAD_SPAWN_SCOPED)
            return source->scope_start_call;
        return R_HIR_NODE_ID_INVALID;
    }
    if (node->kind == R_HIR_VALUE_SCOPE) {
        if (node->integer_value >= node->child_count ||
            !r_body_hir_child(body, node, (uint32_t)node->integer_value, &child))
            return R_HIR_NODE_ID_INVALID;
        return r_scope_find_start(body, child, depth + 1U);
    }
    if ((node->kind == R_HIR_CAST || node->kind == R_HIR_LOCAL) && node->child_count != 0U &&
        r_body_hir_child(body, node, 0U, &child))
        return r_scope_find_start(body, child, depth + 1U);
    return R_HIR_NODE_ID_INVALID;
}

/* The async start behind an `await f(...)` operand, through value scopes, casts and hidden
 * locals; INVALID when the operand is not a direct start. */
static RHirNodeId
r_scope_find_immediate_start(RBodyContext *body, RHirNodeId node_id, uint32_t depth) {
    const RHirNode *node = r_body_hir_node(body, node_id);
    RHirNodeId child;
    if (node == NULL || depth > 8U)
        return R_HIR_NODE_ID_INVALID;
    if (node->kind == R_HIR_ASYNC_START)
        return node_id;
    if (node->kind == R_HIR_VALUE_SCOPE) {
        if (node->integer_value >= node->child_count ||
            !r_body_hir_child(body, node, (uint32_t)node->integer_value, &child))
            return R_HIR_NODE_ID_INVALID;
        return r_scope_find_immediate_start(body, child, depth + 1U);
    }
    if ((node->kind == R_HIR_CAST || node->kind == R_HIR_LOCAL) && node->child_count != 0U &&
        r_body_hir_child(body, node, 0U, &child))
        return r_scope_find_immediate_start(body, child, depth + 1U);
    return R_HIR_NODE_ID_INVALID;
}

/* A consuming await observed the member's terminal outcome: the child frame and any native
 * backend behind it can no longer touch the loaned storage, so its loans end here. */
static bool r_scope_release_call(RBodyContext *body, RHirNodeId call_id) {
    const RHirNode *call = r_body_hir_node(body, call_id);
    const RSemanticSymbol *group;
    const RProjectedBorrowStateEntry *loans;
    RProjectedBorrowStateEntryVector kept = {0};
    size_t count;
    uint32_t state = 0U;
    bool success = false;
    if (call == NULL || call->task_scope == R_SYMBOL_ID_INVALID)
        return true;
    group = r_body_symbol(body, call->task_scope);
    if (group == NULL || !group->is_task_scope || group->scope_loans == 0U)
        return true;
    if (!r_body_projected_borrow_state_entries(body, group->scope_loans, &loans, &count))
        return false;
    for (size_t i = 0U; i < count; ++i) {
        if (loans[i].destination == call_id)
            continue;
        if (!r_body_projected_borrow_entry_push(body, &kept, loans[i]))
            goto cleanup;
    }
    if (kept.count == count) {
        success = true;
        goto cleanup;
    }
    if (kept.count != 0U && !r_body_append_projected_borrow_state(body, &kept, &state))
        goto cleanup;
    r_body_symbol_mut(body, call->task_scope)->scope_loans = state;
    success = true;
cleanup:
    r_context_free(body->frontend, kept.items);
    return success;
}

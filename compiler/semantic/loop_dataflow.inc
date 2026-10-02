/* R-BORROW-0007, R-BORROW-0018, R-BORROW-0019: borrow origins across loop iterations. A loop
 * body is lowered once. Uses and stores of borrow-bearing symbols inside it are recorded; after
 * the body the origins a back edge carries into the next iteration are propagated through the
 * recorded stores until nothing changes, and the uses are checked again against them. A local
 * of the loop body ends with each iteration, so a carried origin naming it is replaced by a
 * marker local that no scope ever holds. */

#define R_LOOP_WIDENING_ROUNDS UINT32_C(64)

static bool r_body_check_store_through_parameter(RBodyContext *body,
                                                 RSymbolId parameter,
                                                 const RExpressionResult *value,
                                                 RSourceSpan span,
                                                 bool *rejected);
static bool r_body_view_value_targets(RBodyContext *body,
                                      RHirNodeId node_id,
                                      RSymbolVector *targets,
                                      bool *known);
static bool r_body_read_view_value_origins(RBodyContext *body, RExpressionResult *value);

static bool r_loop_symbol_carries_views(const RBodyContext *body, const RSemanticSymbol *symbol) {
    const RSemanticTypeKind kind = r_semantic_value_kind(body->frontend, symbol->type);

    return (kind == R_SEMANTIC_TYPE_BORROW) || (kind == R_SEMANTIC_TYPE_SLICE) ||
           (kind == R_SEMANTIC_TYPE_STR) ||
           r_semantic_type_contains_borrow(body->frontend, symbol->type) ||
           r_semantic_type_contains_slice(body->frontend, symbol->type) ||
           r_generic_type_is_dependent(body->frontend, symbol->type, 0U);
}

static bool
r_loop_symbol_is_body_local(const RBodyContext *body, RSymbolId symbol_id, size_t symbol_base) {
    const RSemanticSymbol *symbol = r_body_symbol(body, symbol_id);

    return ((size_t)symbol_id > symbol_base) && (symbol != NULL) &&
           (symbol->kind == R_SEMANTIC_SYMBOL_LOCAL) && (symbol->parent == body->function_symbol) &&
           (symbol_id != body->previous_iteration_origin);
}

/* A use of a borrow-bearing local or parameter inside a loop, with the locals of the enclosing
   loop bodies that are alive at the use. */
static bool r_body_record_loop_read(RBodyContext *body,
                                    RSymbolId symbol_id,
                                    RLoopReadKind kind,
                                    RSourceSpan span) {
    const RSemanticSymbol *symbol = r_body_symbol(body, symbol_id);
    size_t outermost_base;
    RLoopRead read;

    if (body->replaying_loop_borrow_checks || (body->active_loop_symbol_bases.count == 0U) ||
        (symbol == NULL) || (symbol->parent != body->function_symbol) ||
        ((symbol->kind != R_SEMANTIC_SYMBOL_LOCAL) &&
         (symbol->kind != R_SEMANTIC_SYMBOL_PARAMETER)) ||
        !r_loop_symbol_carries_views(body, symbol)) {
        return true;
    }
    outermost_base = body->active_loop_symbol_bases.items[0];
    (void)memset(&read, 0, sizeof(read));
    read.symbol = symbol_id;
    read.kind = kind;
    read.span = span;
    read.live_first = body->loop_live_locals.count;
    for (size_t index = 0U; index < body->active_symbols.count; ++index) {
        if (((size_t)body->active_symbols.items[index] > outermost_base) &&
            !r_symbol_vector_push(
                body->frontend, &body->loop_live_locals, body->active_symbols.items[index])) {
            return false;
        }
    }
    read.live_count = body->loop_live_locals.count - read.live_first;
    if (!r_grow_array(body->frontend,
                      (void **)&body->loop_reads.items,
                      &body->loop_reads.capacity,
                      sizeof(*body->loop_reads.items),
                      body->loop_reads.count + 1U)) {
        return false;
    }
    body->loop_reads.items[body->loop_reads.count++] = read;
    return true;
}

/* R-BORROW-0009: the reads that form an argument a call holds apart from the storage it
   designates pass on what that storage holds, as the result does, rather than the storage. */
static void r_body_loop_reads_through(RBodyContext *body, size_t first, size_t end) {
    for (size_t index = first; (index < end) && (index < body->loop_reads.count); ++index) {
        RLoopRead *read = &body->loop_reads.items[index];
        const RSemanticSymbol *symbol = r_body_symbol(body, read->symbol);
        const RSemanticTypeKind kind = symbol == NULL
                                           ? R_SEMANTIC_TYPE_INVALID
                                           : r_semantic_value_kind(body->frontend, symbol->type);

        if ((read->kind == R_LOOP_READ_VALUE) &&
            ((kind == R_SEMANTIC_TYPE_BORROW) || (kind == R_SEMANTIC_TYPE_SLICE))) {
            read->kind = R_LOOP_READ_THROUGH;
        }
    }
}

/* A store of `value` into `target` inside a loop; through a view it reaches the storage the
   view designates. */
static bool r_body_record_loop_flow(RBodyContext *body,
                                    RSymbolId target,
                                    bool through_view,
                                    const RExpressionResult *value,
                                    RSourceSpan span) {
    const RSemanticSymbol *symbol = r_body_symbol(body, target);
    RLoopFlow flow;

    if (body->replaying_loop_borrow_checks || (body->active_loop_symbol_bases.count == 0U) ||
        (symbol == NULL) || !value->valid ||
        ((symbol->kind != R_SEMANTIC_SYMBOL_MODULE_OBJECT) &&
         (symbol->parent != body->function_symbol)) ||
        !r_loop_symbol_carries_views(body, symbol)) {
        return true;
    }
    (void)memset(&flow, 0, sizeof(flow));
    flow.target = target;
    flow.through_view = through_view;
    flow.origin = value->borrow_origin;
    flow.origin_set = value->borrow_origin_set;
    flow.origin_multiple = value->borrow_origin_multiple;
    flow.source_first = body->loop_statement_read_base <= body->loop_reads.count
                            ? body->loop_statement_read_base
                            : body->loop_reads.count;
    flow.source_count = body->loop_reads.count - flow.source_first;
    flow.span = span;
    if (!r_grow_array(body->frontend,
                      (void **)&body->loop_flows.items,
                      &body->loop_flows.capacity,
                      sizeof(*body->loop_flows.items),
                      body->loop_flows.count + 1U)) {
        return false;
    }
    body->loop_flows.items[body->loop_flows.count++] = flow;
    return true;
}

/* A store through a borrow value, such as a call result, whose designated storage is computed
   again from `pointer` after the loop body. */
static bool r_body_record_loop_node_flow(RBodyContext *body,
                                         RHirNodeId pointer,
                                         const RExpressionResult *value,
                                         RSourceSpan span) {
    RLoopFlow flow;

    if (body->replaying_loop_borrow_checks || (body->active_loop_symbol_bases.count == 0U) ||
        !value->valid) {
        return true;
    }
    (void)memset(&flow, 0, sizeof(flow));
    flow.target_node = pointer;
    flow.origin = value->borrow_origin;
    flow.origin_set = value->borrow_origin_set;
    flow.origin_multiple = value->borrow_origin_multiple;
    flow.source_first = body->loop_statement_read_base <= body->loop_reads.count
                            ? body->loop_statement_read_base
                            : body->loop_reads.count;
    flow.source_count = body->loop_reads.count - flow.source_first;
    flow.span = span;
    if (!r_grow_array(body->frontend,
                      (void **)&body->loop_flows.items,
                      &body->loop_flows.capacity,
                      sizeof(*body->loop_flows.items),
                      body->loop_flows.count + 1U)) {
        return false;
    }
    body->loop_flows.items[body->loop_flows.count++] = flow;
    return true;
}

static bool r_body_previous_iteration_origin(RBodyContext *body, RSymbolId *marker) {
    if (body->previous_iteration_origin == R_SYMBOL_ID_INVALID) {
        RSemanticSymbol symbol;

        (void)memset(&symbol, 0, sizeof(symbol));
        if (!r_semantic_type_from_token(body->frontend, R_TOKEN_KW_BOOL, &symbol.type)) {
            return false;
        }
        symbol.kind = R_SEMANTIC_SYMBOL_LOCAL;
        symbol.module_source = body->source;
        symbol.parent = body->function_symbol;
        symbol.object_state = R_SEMANTIC_OBJECT_STATE_MOVED;
        symbol.is_hidden_local = true;
        symbol.signature_supported = true;
        if (!r_semantic_append_symbol(body->frontend, symbol, &body->previous_iteration_origin)) {
            return false;
        }
    }
    *marker = body->previous_iteration_origin;
    return true;
}

/* The origins a value keeps into the next iteration: each local of the loop body becomes the
   previous-iteration marker. */
static bool r_body_loop_carried_origins(RBodyContext *body,
                                        size_t symbol_base,
                                        RSymbolId *origin,
                                        uint32_t *origin_set,
                                        bool *origin_multiple) {
    RSymbolVector origins = {0};
    RSymbolVector carried = {0};
    bool unknown = false;
    bool changed = false;
    bool success = false;
    uint32_t set_id = UINT32_C(0);

    if ((*origin == R_SYMBOL_ID_INVALID) && (*origin_set == UINT32_C(0)) && !*origin_multiple) {
        return true;
    }
    if (!r_semantic_append_borrow_origins(
            body->frontend, &origins, *origin, *origin_set, *origin_multiple, &unknown)) {
        goto cleanup;
    }
    if (unknown) {
        success = true;
        goto cleanup;
    }
    for (size_t index = 0U; index < origins.count; ++index) {
        RSymbolId next = origins.items[index];
        bool present = false;

        if (r_loop_symbol_is_body_local(body, next, symbol_base)) {
            if (!r_body_previous_iteration_origin(body, &next)) {
                goto cleanup;
            }
            changed = true;
        }
        for (size_t kept = 0U; kept < carried.count; ++kept) {
            present = present || (carried.items[kept] == next);
        }
        if (!present && !r_symbol_vector_push(body->frontend, &carried, next)) {
            goto cleanup;
        }
    }
    if (changed) {
        if (!r_semantic_intern_borrow_origin_set(body->frontend, &carried, &set_id)) {
            goto cleanup;
        }
        *origin = carried.count == 1U ? carried.items[0] : R_SYMBOL_ID_INVALID;
        *origin_set = set_id;
        *origin_multiple = carried.count > 1U;
    }
    success = true;

cleanup:
    r_context_free(body->frontend, origins.items);
    r_context_free(body->frontend, carried.items);
    return success;
}

/* The back-edge states with the locals of the loop body replaced by the marker. */
static bool r_body_loop_carried_states(RBodyContext *body,
                                       const RSemanticObjectSnapshot *states,
                                       size_t state_count,
                                       size_t symbol_base,
                                       RSemanticObjectSnapshot **carried) {
    *carried = NULL;
    if (state_count == 0U) {
        return true;
    }
    *carried = r_context_allocate(body->frontend, state_count * sizeof(**carried));
    if (*carried == NULL) {
        return false;
    }
    for (size_t index = 0U; index < state_count; ++index) {
        RSemanticObjectSnapshot state = states[index];
        const RSymbolId origin = state.borrow_origin;
        const uint32_t origin_set = state.borrow_origin_set;

        if (!r_body_loop_carried_origins(body,
                                         symbol_base,
                                         &state.borrow_origin,
                                         &state.borrow_origin_set,
                                         &state.borrow_origin_multiple)) {
            return false;
        }
        if ((state.borrow_origin != origin) || (state.borrow_origin_set != origin_set)) {
            state.projected_borrow_state = UINT32_C(0);
            state.borrow_path = UINT64_C(0);
        }
        (*carried)[index] = state;
    }
    return true;
}

/* Propagates the recorded stores of a loop until the states stop growing. With `carry`, the
   stores reach the next iteration, so the locals of the loop body become the marker in values
   stored into symbols declared outside the loop. */
static bool r_body_widen_loop_flows(
    RBodyContext *body, size_t symbol_base, size_t flow_first, bool carry, bool check_parameters) {
    RSymbolVector targets = {0};
    bool success = false;

    for (uint32_t round = UINT32_C(0); round < R_LOOP_WIDENING_ROUNDS; ++round) {
        bool changed = false;

        for (size_t index = flow_first; index < body->loop_flows.count; ++index) {
            const RLoopFlow flow = body->loop_flows.items[index];
            const RSemanticSymbol *direct_target =
                (flow.through_view || (flow.target_node != R_HIR_NODE_ID_INVALID))
                    ? NULL
                    : r_body_symbol(body, flow.target);
            /* The state a back edge carries already holds what a direct store wrote, and a later
               assignment of the whole symbol replaces it; only the sources can bring more. */
            const bool sources_only =
                (direct_target != NULL) && (direct_target->kind != R_SEMANTIC_SYMBOL_PARAMETER);
            RSymbolId origin = sources_only ? R_SYMBOL_ID_INVALID : flow.origin;
            uint32_t origin_set = sources_only ? UINT32_C(0) : flow.origin_set;
            bool origin_multiple = sources_only ? false : flow.origin_multiple;
            bool unknown = false;

            for (size_t source = 0U; source < flow.source_count; ++source) {
                const RLoopRead *read = &body->loop_reads.items[flow.source_first + source];
                const RSemanticSymbol *symbol = r_body_symbol(body, read->symbol);
                RExpressionResult read_value = {0};

                if ((symbol == NULL) || (read->kind == R_LOOP_READ_USE)) {
                    continue;
                }
                read_value.symbol = read->symbol;
                read_value.borrow_origin = symbol->borrow_origin;
                read_value.borrow_origin_set = symbol->borrow_origin_set;
                read_value.borrow_origin_multiple = symbol->borrow_origin_multiple;
                read_value.valid = true;
                if (((read->kind == R_LOOP_READ_THROUGH) &&
                     !r_body_read_view_value_origins(body, &read_value)) ||
                    !r_semantic_merge_borrow_origins(body->frontend,
                                                     origin,
                                                     origin_set,
                                                     origin_multiple,
                                                     read_value.borrow_origin,
                                                     read_value.borrow_origin_set,
                                                     read_value.borrow_origin_multiple,
                                                     &origin,
                                                     &origin_set,
                                                     &origin_multiple)) {
                    goto cleanup;
                }
            }
            if ((origin == R_SYMBOL_ID_INVALID) && (origin_set == UINT32_C(0)) &&
                !origin_multiple) {
                continue;
            }
            targets.count = 0U;
            if (flow.target_node != R_HIR_NODE_ID_INVALID) {
                bool known = true;

                if (!r_body_view_value_targets(body, flow.target_node, &targets, &known)) {
                    goto cleanup;
                }
                if (!known) {
                    continue;
                }
            } else if (flow.through_view) {
                const RSemanticSymbol *view = r_body_symbol(body, flow.target);

                if ((view == NULL) ||
                    !r_semantic_append_borrow_origins(
                        body->frontend,
                        &targets,
                        view->view_target_known ? view->view_target : view->borrow_origin,
                        view->view_target_known ? view->view_target_set : view->borrow_origin_set,
                        view->view_target_known ? view->view_target_multiple
                                                : view->borrow_origin_multiple,
                        &unknown)) {
                    goto cleanup;
                }
            } else if (!r_symbol_vector_push(body->frontend, &targets, flow.target)) {
                goto cleanup;
            }
            for (size_t target_index = 0U; target_index < targets.count; ++target_index) {
                const RSymbolId target_id = targets.items[target_index];
                const RSemanticSymbol *target = r_body_symbol(body, target_id);
                RSymbolId stored = origin;
                uint32_t stored_set = origin_set;
                bool stored_multiple = origin_multiple;
                RSymbolId merged;
                uint32_t merged_set;
                bool merged_multiple;
                RSemanticSymbol *writable;

                if ((target == NULL) || !r_loop_symbol_carries_views(body, target) ||
                    ((target->kind != R_SEMANTIC_SYMBOL_MODULE_OBJECT) &&
                     (target->parent != body->function_symbol))) {
                    continue;
                }
                if (target->kind == R_SEMANTIC_SYMBOL_PARAMETER) {
                    if (check_parameters && !body->loop_flows.items[index].reported) {
                        RExpressionResult value = {0};
                        bool rejected = false;

                        /* A read of the parameter itself, such as the target of a push through
                           it, brings nothing another iteration changes; the store in the body
                           was checked with what it stored (R-BORROW-0019). */
                        value.borrow_origin = sources_only ? R_SYMBOL_ID_INVALID : flow.origin;
                        value.borrow_origin_set = sources_only ? UINT32_C(0) : flow.origin_set;
                        value.borrow_origin_multiple = !sources_only && flow.origin_multiple;
                        for (size_t source = 0U; source < flow.source_count; ++source) {
                            const RLoopRead *read =
                                &body->loop_reads.items[flow.source_first + source];
                            const RSemanticSymbol *symbol = r_body_symbol(body, read->symbol);
                            RExpressionResult read_value = {0};

                            if ((symbol == NULL) || (read->kind == R_LOOP_READ_USE) ||
                                (read->symbol == target_id)) {
                                continue;
                            }
                            read_value.symbol = read->symbol;
                            read_value.borrow_origin = symbol->borrow_origin;
                            read_value.borrow_origin_set = symbol->borrow_origin_set;
                            read_value.borrow_origin_multiple = symbol->borrow_origin_multiple;
                            read_value.valid = true;
                            if (((read->kind == R_LOOP_READ_THROUGH) &&
                                 !r_body_read_view_value_origins(body, &read_value)) ||
                                !r_semantic_merge_borrow_origins(body->frontend,
                                                                 value.borrow_origin,
                                                                 value.borrow_origin_set,
                                                                 value.borrow_origin_multiple,
                                                                 read_value.borrow_origin,
                                                                 read_value.borrow_origin_set,
                                                                 read_value.borrow_origin_multiple,
                                                                 &value.borrow_origin,
                                                                 &value.borrow_origin_set,
                                                                 &value.borrow_origin_multiple)) {
                                goto cleanup;
                            }
                        }
                        value.valid = true;
                        if (!r_body_check_store_through_parameter(
                                body, target_id, &value, flow.span, &rejected)) {
                            goto cleanup;
                        }
                        body->loop_flows.items[index].reported = rejected;
                    }
                    continue;
                }
                if ((target->kind != R_SEMANTIC_SYMBOL_LOCAL) &&
                    (target->kind != R_SEMANTIC_SYMBOL_MODULE_OBJECT)) {
                    continue;
                }
                if (carry && ((size_t)target_id <= symbol_base) &&
                    !r_body_loop_carried_origins(
                        body, symbol_base, &stored, &stored_set, &stored_multiple)) {
                    goto cleanup;
                }
                writable = r_body_symbol_mut(body, target_id);
                if ((writable == NULL) ||
                    !r_semantic_merge_borrow_origins(body->frontend,
                                                     writable->borrow_origin,
                                                     writable->borrow_origin_set,
                                                     writable->borrow_origin_multiple,
                                                     stored,
                                                     stored_set,
                                                     stored_multiple,
                                                     &merged,
                                                     &merged_set,
                                                     &merged_multiple)) {
                    goto cleanup;
                }
                if ((merged != writable->borrow_origin) ||
                    (merged_set != writable->borrow_origin_set) ||
                    (merged_multiple != writable->borrow_origin_multiple)) {
                    writable->borrow_origin = merged;
                    writable->borrow_origin_set = merged_set;
                    writable->borrow_origin_multiple = merged_multiple;
                    writable->projected_borrow_state = UINT32_C(0);
                    writable->borrow_path = UINT64_C(0);
                    changed = true;
                }
            }
        }
        if (!changed) {
            break;
        }
    }
    success = true;

cleanup:
    r_context_free(body->frontend, targets.items);
    return success;
}

/* The effect on `symbol_id` of the assignments in one statement that precedes a use in the same
   iteration: an assignment of the whole symbol replaces its origins, a store into a component
   adds to them. */
static bool r_loop_assignment_in_statement(RBodyContext *body,
                                           RSourceSpan statement_span,
                                           RSymbolId symbol_id,
                                           size_t hir_first,
                                           bool definite) {
    for (size_t hir_index = hir_first; hir_index < body->frontend->hir_node_count; ++hir_index) {
        const RHirNode *node = &body->frontend->hir_nodes[hir_index];
        const RHirNode *left;
        const RHirNode *right;
        RSemanticSymbol *symbol;

        if ((node->kind != R_HIR_ASSIGN) || (node->symbol != symbol_id) ||
            !r_body_span_contains(statement_span, node->span) ||
            (node->child_count != UINT32_C(2)) ||
            ((size_t)node->first_child + UINT32_C(1) >= body->frontend->hir_child_count)) {
            continue;
        }
        left = r_body_hir_node(body, body->frontend->hir_children[(size_t)node->first_child]);
        right = r_body_hir_node(body, body->frontend->hir_children[(size_t)node->first_child + 1U]);
        symbol = r_body_symbol_mut(body, symbol_id);
        if ((left == NULL) || (right == NULL) || (symbol == NULL)) {
            body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
            return false;
        }
        const RSemanticTypeKind stored_kind = r_semantic_value_kind(body->frontend, left->type);
        if ((stored_kind != R_SEMANTIC_TYPE_BORROW) && (stored_kind != R_SEMANTIC_TYPE_SLICE) &&
            (stored_kind != R_SEMANTIC_TYPE_STR) &&
            !r_semantic_type_may_hold_views(body->frontend, left->type)) {
            /* M18-7: a stored value that holds no view, such as an integer read from a local of
               the iteration, brings no origin. */
            continue;
        }
        if (definite && (left->kind == R_HIR_PLACE)) {
            symbol->borrow_origin = right->borrow_origin;
            symbol->borrow_origin_set = right->borrow_origin_set;
            symbol->borrow_origin_multiple = right->borrow_origin_multiple;
        } else if (!r_semantic_merge_borrow_origins(body->frontend,
                                                    symbol->borrow_origin,
                                                    symbol->borrow_origin_set,
                                                    symbol->borrow_origin_multiple,
                                                    right->borrow_origin,
                                                    right->borrow_origin_set,
                                                    right->borrow_origin_multiple,
                                                    &symbol->borrow_origin,
                                                    &symbol->borrow_origin_set,
                                                    &symbol->borrow_origin_multiple)) {
            return false;
        }
    }
    return true;
}

static bool r_loop_prior_statement(
    RBodyContext *body, RAstRef ref, RSymbolId symbol_id, size_t hir_first, uint32_t depth) {
    RAstNodeView view;

    if ((depth > r_frontend_tree_depth_limit(body->frontend)) ||
        !r_ast_ref_view(body->frontend, ref, &view)) {
        return depth <= r_frontend_tree_depth_limit(body->frontend);
    }
    if (view.kind == R_SYNTAX_BLOCK) {
        for (uint32_t child_index = UINT32_C(0); child_index < view.child_count; ++child_index) {
            RAstRef child;

            if (!r_ast_ref_child(body->frontend, ref, child_index, &child) ||
                !r_loop_prior_statement(body, child, symbol_id, hir_first, depth + UINT32_C(1))) {
                return false;
            }
        }
        return true;
    }
    /* An expression statement or declaration runs completely; any other statement may run its
       assignments or not. */
    return r_loop_assignment_in_statement(body,
                                          view.span,
                                          symbol_id,
                                          hir_first,
                                          (view.kind == R_SYNTAX_EXPRESSION_STATEMENT) ||
                                              (view.kind == R_SYNTAX_OBJECT_DECLARATION));
}

/* Applies the assignments of the loop body that precede `use_span` in the same iteration. */
static bool r_loop_state_before_use(RBodyContext *body,
                                    RAstRef ref,
                                    RSymbolId symbol_id,
                                    RSourceSpan use_span,
                                    size_t hir_first,
                                    uint32_t depth) {
    RAstNodeView view;

    if ((depth > r_frontend_tree_depth_limit(body->frontend)) ||
        !r_ast_ref_view(body->frontend, ref, &view) || !r_body_span_contains(view.span, use_span)) {
        return depth <= r_frontend_tree_depth_limit(body->frontend);
    }
    for (uint32_t child_index = UINT32_C(0); child_index < view.child_count; ++child_index) {
        RAstRef child;
        RAstNodeView child_view;

        if (!r_ast_ref_child(body->frontend, ref, child_index, &child) ||
            !r_ast_ref_view(body->frontend, child, &child_view)) {
            body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
            return false;
        }
        if (r_body_span_contains(child_view.span, use_span)) {
            return r_loop_state_before_use(
                body, child, symbol_id, use_span, hir_first, depth + UINT32_C(1));
        }
        if ((view.kind == R_SYNTAX_BLOCK) && (child_view.span.source == use_span.source) &&
            (child_view.span.end <= use_span.start) &&
            !r_loop_prior_statement(body, child, symbol_id, hir_first, depth + UINT32_C(1))) {
            return false;
        }
    }
    return true;
}

static bool r_body_loop_read_expired(RBodyContext *body,
                                     const RLoopRead *read,
                                     size_t symbol_base,
                                     bool *expired) {
    const RSemanticSymbol *symbol = r_body_symbol(body, read->symbol);
    RSymbolVector origins = {0};
    bool unknown = false;
    bool success = false;

    *expired = false;
    if ((symbol == NULL) || !r_semantic_append_borrow_origins(body->frontend,
                                                              &origins,
                                                              symbol->borrow_origin,
                                                              symbol->borrow_origin_set,
                                                              symbol->borrow_origin_multiple,
                                                              &unknown)) {
        goto cleanup;
    }
    for (size_t index = 0U; !*expired && (index < origins.count); ++index) {
        const RSymbolId origin_id = origins.items[index];
        const RSemanticSymbol *origin = r_body_symbol(body, origin_id);
        bool live = false;

        if (origin_id == body->previous_iteration_origin) {
            *expired = true;
            break;
        }
        if ((origin == NULL) ||
            ((origin->kind != R_SEMANTIC_SYMBOL_LOCAL) &&
             (origin->kind != R_SEMANTIC_SYMBOL_PARAMETER)) ||
            (origin->parent != body->function_symbol) ||
            (origin->kind == R_SEMANTIC_SYMBOL_PARAMETER) ||
            (origin->content_parameter != R_SYMBOL_ID_INVALID)) {
            continue;
        }
        if ((size_t)origin_id > symbol_base) {
            for (size_t live_index = 0U; live_index < read->live_count; ++live_index) {
                live = live ||
                       (body->loop_live_locals.items[read->live_first + live_index] == origin_id);
            }
        } else {
            for (size_t active_index = 0U; active_index < body->active_symbols.count;
                 ++active_index) {
                live = live || (body->active_symbols.items[active_index] == origin_id);
            }
            live = live && (origin->object_state == R_SEMANTIC_OBJECT_STATE_INITIALIZED);
        }
        *expired = !live;
    }
    success = true;

cleanup:
    r_context_free(body->frontend, origins.items);
    return success;
}

/* Checks the recorded uses of symbols declared outside the loop again, with the carried state
   and the assignments that precede each use in its iteration. */
static bool r_body_replay_loop_reads(RBodyContext *body,
                                     size_t symbol_base,
                                     size_t read_first,
                                     size_t hir_first) {
    const bool was_replaying = body->replaying_loop_borrow_checks;
    bool success = true;

    body->replaying_loop_borrow_checks = true;
    for (size_t index = read_first; success && (index < body->loop_reads.count); ++index) {
        const RLoopRead read = body->loop_reads.items[index];
        RSemanticObjectSnapshot *states = NULL;
        size_t state_count = 0U;
        RAstRef loop_block = {0};
        bool expired = false;

        if (read.reported || ((size_t)read.symbol > symbol_base)) {
            continue;
        }
        for (size_t loop_index = body->active_loop_blocks.count; loop_index != 0U; --loop_index) {
            RAstNodeView block_view;
            const RAstRef candidate = body->active_loop_blocks.items[loop_index - 1U];

            if (r_ast_ref_view(body->frontend, candidate, &block_view) &&
                r_body_span_contains(block_view.span, read.span)) {
                loop_block = candidate;
                break;
            }
        }
        if (!r_body_capture_active_object_states(body, &states, &state_count)) {
            r_context_free(body->frontend, states);
            success = false;
            break;
        }
        success =
            ((loop_block.node == R_AST_NODE_ID_INVALID) ||
             r_loop_state_before_use(body, loop_block, read.symbol, read.span, hir_first, 0U)) &&
            r_body_loop_read_expired(body, &read, symbol_base, &expired);
        success = r_body_restore_active_object_states(body, states, state_count) && success;
        r_context_free(body->frontend, states);
        if (success && expired) {
            body->loop_reads.items[index].reported = true;
            success = r_body_diagnostic(body,
                                        "R-DIAG-BORROW-002",
                                        "R-BORROW-0007",
                                        "borrowed value outlives its source object",
                                        read.span);
        }
    }
    body->replaying_loop_borrow_checks = was_replaying;
    return success;
}

/* At a back edge: the carried states join the loop header for the checks of the next
   iteration, the recorded stores widen them, and the recorded uses are checked again. */
static bool r_body_finish_loop_iteration(RBodyContext *body,
                                         const RSemanticObjectSnapshot *header_states,
                                         const RSemanticObjectSnapshot *backedge_states,
                                         size_t state_count,
                                         size_t symbol_base,
                                         size_t read_first,
                                         size_t flow_first,
                                         size_t hir_first,
                                         RSourceSpan span) {
    RSemanticObjectSnapshot *carried = NULL;
    bool success =
        r_body_loop_carried_states(body, backedge_states, state_count, symbol_base, &carried) &&
        r_body_merge_branch_object_states(body, header_states, carried, state_count, span) &&
        r_body_widen_loop_flows(body, symbol_base, flow_first, true, true) &&
        r_body_replay_loop_reads(body, symbol_base, read_first, hir_first);

    r_context_free(body->frontend, carried);
    return success;
}

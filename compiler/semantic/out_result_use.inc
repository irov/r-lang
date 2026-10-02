/* Track each significant result separately. CLEAR and PENDING are sets of
   possible states, so a read on one branch cannot excuse loss on another. */
enum {
    R_OUT_USE_CLEAR = 1U,
    R_OUT_USE_PENDING = 2U
};
typedef struct ROutUseExit {
    RHirKind kind;
    RTypeId error;
    uint32_t states;
} ROutUseExit;
typedef struct ROutUseFlow {
    uint32_t normal;
    ROutUseExit *exits;
    size_t count, capacity;
} ROutUseFlow;
typedef struct ROutUseCheck {
    RBodyContext body;
    RHirNodeId producer, place;
    RSymbolId binding;
    RSourceSpan span;
    bool reported;
    bool initialization;
    bool ordinary_result;
} ROutUseCheck;

static void r_out_use_free(ROutUseCheck *check, ROutUseFlow *flow) {
    r_context_free(check->body.frontend, flow->exits);
    memset(flow, 0, sizeof(*flow));
}

static bool r_out_use_exit(
    ROutUseCheck *check, ROutUseFlow *flow, RHirKind kind, RTypeId error, uint32_t states) {
    if (states == 0U)
        return true;
    for (size_t i = 0U; i < flow->count; ++i)
        if (flow->exits[i].kind == kind && flow->exits[i].error == error) {
            flow->exits[i].states |= states;
            return true;
        }
    if (!r_grow_array(check->body.frontend,
                      (void **)&flow->exits,
                      &flow->capacity,
                      sizeof(*flow->exits),
                      flow->count + 1U))
        return false;
    flow->exits[flow->count++] = (ROutUseExit){kind, error, states};
    return true;
}

static bool r_out_use_merge(ROutUseCheck *check, ROutUseFlow *target, const ROutUseFlow *source) {
    for (size_t i = 0U; i < source->count; ++i)
        if (!r_out_use_exit(check,
                            target,
                            source->exits[i].kind,
                            source->exits[i].error,
                            source->exits[i].states))
            return false;
    return true;
}

static bool r_out_use_lost(ROutUseCheck *check, uint32_t states, RSourceSpan span) {
    if ((states & R_OUT_USE_PENDING) == 0U || check->reported)
        return true;
    check->reported = true;
    return r_semantic_add_diagnostic(
        check->body.frontend,
        check->initialization ? "R-DIAG-FLOW-001" : "R-DIAG-USE-001",
        check->ordinary_result ? "R-FUNC-0020" : "R-FUNC-0022",
        check->initialization
            ? "out value may be uninitialized or consumed before use or successful publication"
        : check->ordinary_result
            ? "result is overwritten or leaves scope without use; use it, forward it, or "
              "explicitly discard it with as void"
            : "out result is overwritten or leaves scope without use; use it, forward it, "
              "or explicitly discard it with as void",
        span);
}

static bool r_out_use_walk(ROutUseCheck *, RHirNodeId, uint32_t, uint32_t, ROutUseFlow *);

/* The object a place designates storage of: projections of any depth share their root. */
static RSymbolId r_out_use_root_symbol(ROutUseCheck *check, RHirNodeId place) {
    for (uint32_t depth = 0U; depth <= check->body.frontend->options.limits.max_nesting; ++depth) {
        const RHirNode *node = r_body_hir_node(&check->body, place);
        if (node == NULL)
            return R_SYMBOL_ID_INVALID;
        if (node->symbol != R_SYMBOL_ID_INVALID || node->child_count == 0U ||
            (node->kind != R_HIR_FIELD_PLACE && node->kind != R_HIR_INDEX_PLACE))
            return node->symbol;
        place = check->body.frontend->hir_children[node->first_child];
    }
    return R_SYMBOL_ID_INVALID;
}

/* R-BORROW-0011: the object and field path a place designates, after lowering. A place through a
 * borrow local counts only when the local is bound once, by its declaration, to a borrow of a
 * field of one object; its place then lies within that field. Otherwise `root` is invalid. */
static uint64_t r_out_use_place_path(ROutUseCheck *check, RHirNodeId place, RSymbolId *root) {
    RFrontendContext *context = check->body.frontend;
    RHirNodeId current = place;

    *root = R_SYMBOL_ID_INVALID;
    for (uint32_t depth = 0U; depth <= context->options.limits.max_nesting; ++depth) {
        const RHirNode *node = r_body_hir_node(&check->body, current);
        if (node == NULL)
            return UINT64_C(0);
        if (node->kind == R_HIR_PLACE)
            return r_body_place_field_path(&check->body, place, root);
        if (node->kind == R_HIR_DEREF_PLACE) {
            const RHirNode *pointer =
                node->child_count == 1U
                    ? r_body_hir_node(&check->body, context->hir_children[node->first_child])
                    : NULL;
            const RSemanticSymbol *borrow =
                pointer != NULL && pointer->kind == R_HIR_PLACE && pointer->symbol != 0U
                    ? &context->semantic_symbols[pointer->symbol - 1U]
                    : NULL;
            const RHirNode *local =
                borrow != NULL ? r_body_hir_node(&check->body, borrow->hir_node) : NULL;
            if (borrow == NULL || borrow->kind != R_SEMANTIC_SYMBOL_LOCAL || local == NULL ||
                local->kind != R_HIR_LOCAL || local->child_count != 1U ||
                borrow->borrow_origin == R_SYMBOL_ID_INVALID || borrow->borrow_origin_set != 0U ||
                borrow->borrow_origin_multiple)
                return UINT64_C(0);
            /* A rebinding of the local itself makes its referent flow-dependent. */
            for (size_t index = 0U; index < context->hir_node_count; ++index) {
                const RHirNode *write = &context->hir_nodes[index];
                const RHirNode *target =
                    (write->kind == R_HIR_ASSIGN || write->kind == R_HIR_COMPOUND_ASSIGN) &&
                            write->child_count != 0U
                        ? r_body_hir_node(&check->body, context->hir_children[write->first_child])
                        : NULL;
                if (target != NULL && target->kind == R_HIR_PLACE &&
                    target->symbol == pointer->symbol)
                    return UINT64_C(0);
            }
            *root = borrow->borrow_origin;
            return r_body_value_borrow_path(&check->body,
                                            context->hir_children[local->first_child],
                                            borrow->borrow_origin,
                                            false);
        }
        if ((node->kind != R_HIR_FIELD_PLACE && node->kind != R_HIR_INDEX_PLACE) ||
            node->child_count == 0U)
            return UINT64_C(0);
        current = context->hir_children[node->first_child];
    }
    return UINT64_C(0);
}

static RBodyPlaceRelation r_out_use_relation(ROutUseCheck *check, RHirNodeId place) {
    const RHirNode *left = r_body_hir_node(&check->body, check->place);
    const RHirNode *right = r_body_hir_node(&check->body, place);
    if (check->ordinary_result) {
        /* The obligation belongs to the stored result, not the pointees named
           by its borrow origins. Taking its address already acknowledges it. */
        if (right == NULL || right->symbol != check->binding)
            return R_BODY_PLACE_RELATION_DISJOINT;
        return right->kind == R_HIR_PLACE ? R_BODY_PLACE_RELATION_SAME
                                          : R_BODY_PLACE_RELATION_OVERLAPPING;
    }
    if (left == NULL)
        return right != NULL && right->symbol == check->binding ? R_BODY_PLACE_RELATION_OVERLAPPING
                                                                : R_BODY_PLACE_RELATION_DISJOINT;
    if (left != NULL && right != NULL &&
        r_out_use_root_symbol(check, check->place) != r_out_use_root_symbol(check, place) &&
        r_body_place_is_direct_storage_projection(&check->body, check->place) &&
        r_body_place_is_direct_storage_projection(&check->body, place))
        return R_BODY_PLACE_RELATION_DISJOINT;
    if (left != NULL && right != NULL) {
        RSymbolId left_root, right_root;
        const uint64_t left_path = r_out_use_place_path(check, check->place, &left_root);
        const uint64_t right_path = r_out_use_place_path(check, place, &right_root);
        if (left_root != R_SYMBOL_ID_INVALID && left_root == right_root &&
            !r_semantic_borrow_paths_overlap(left_path, right_path))
            return R_BODY_PLACE_RELATION_DISJOINT;
    }
    return r_body_place_relation_depth(&check->body, check->place, place, 0U);
}

static bool r_out_use_step(ROutUseCheck *check, RHirNodeId id, uint32_t depth, ROutUseFlow *flow) {
    ROutUseFlow child = {0};
    const bool ok = r_out_use_walk(check, id, flow->normal, depth + 1U, &child) &&
                    r_out_use_merge(check, flow, &child);
    flow->normal = child.normal;
    r_out_use_free(check, &child);
    return ok;
}

/* Keep short-circuit truth paths separate until their enclosing branch chooses
   an arm. An error on the true path must not invent an unused normal false path. */
static bool r_out_use_condition(ROutUseCheck *check,
                                RHirNodeId id,
                                uint32_t input,
                                uint32_t depth,
                                ROutUseFlow *flow,
                                uint32_t *when_true,
                                uint32_t *when_false) {
    RFrontendContext *context = check->body.frontend;
    if (id == 0U || depth > r_frontend_tree_depth_limit(context))
        return false;
    const RHirNode node = context->hir_nodes[id - 1U];
    if (node.kind == R_HIR_BINARY && node.child_count == 2U &&
        (node.operation == R_TOKEN_EQUAL_EQUAL || node.operation == R_TOKEN_BANG_EQUAL)) {
        for (uint32_t side = 0U; side < 2U; ++side) {
            const RHirNode *literal =
                &context->hir_nodes[context->hir_children[node.first_child + side] - 1U];
            if (literal->kind != R_HIR_LITERAL ||
                r_semantic_value_kind(context, literal->type) != R_SEMANTIC_TYPE_BOOL)
                continue;
            const bool preserve =
                (node.operation == R_TOKEN_EQUAL_EQUAL) == (literal->integer_value != 0U);
            return r_out_use_condition(check,
                                       context->hir_children[node.first_child + 1U - side],
                                       input,
                                       depth + 1U,
                                       flow,
                                       preserve ? when_true : when_false,
                                       preserve ? when_false : when_true);
        }
    }
    if (node.kind == R_HIR_UNARY && node.operation == R_TOKEN_BANG && node.child_count == 1U)
        return r_out_use_condition(check,
                                   context->hir_children[node.first_child],
                                   input,
                                   depth + 1U,
                                   flow,
                                   when_false,
                                   when_true);
    if (node.kind == R_HIR_BINARY &&
        (node.operation == R_TOKEN_AMP_AMP || node.operation == R_TOKEN_PIPE_PIPE)) {
        uint32_t left_true = 0U, left_false = 0U, right_true = 0U, right_false = 0U;
        ROutUseFlow right = {0};
        bool ok = r_out_use_condition(check,
                                      context->hir_children[node.first_child],
                                      input,
                                      depth + 1U,
                                      flow,
                                      &left_true,
                                      &left_false);
        const bool conjunction = node.operation == R_TOKEN_AMP_AMP;
        if (ok)
            ok = r_out_use_condition(check,
                                     context->hir_children[node.first_child + 1U],
                                     conjunction ? left_true : left_false,
                                     depth + 1U,
                                     &right,
                                     &right_true,
                                     &right_false) &&
                 r_out_use_merge(check, flow, &right);
        *when_true = conjunction ? right_true : left_true | right_true;
        *when_false = conjunction ? left_false | right_false : right_false;
        flow->normal = *when_true | *when_false;
        r_out_use_free(check, &right);
        return ok;
    }
    if (!r_out_use_walk(check, id, input, depth + 1U, flow))
        return false;
    bool truth = false;
    const bool known = r_body_constant_bool(&check->body, id, &truth);
    *when_true = !known || truth ? flow->normal : 0U;
    *when_false = !known || !truth ? flow->normal : 0U;
    return true;
}

static bool r_out_use_walk(
    ROutUseCheck *check, RHirNodeId id, uint32_t input, uint32_t depth, ROutUseFlow *flow) {
    RFrontendContext *context = check->body.frontend;
    flow->normal = input;
    if (input == 0U || check->reported)
        return true;
    if (id == 0U || depth > r_frontend_tree_depth_limit(context))
        return false;
    const RHirNode node = context->hir_nodes[id - 1U];
    if (node.kind == R_HIR_BINARY &&
        (node.operation == R_TOKEN_AMP_AMP || node.operation == R_TOKEN_PIPE_PIPE)) {
        uint32_t when_true = 0U, when_false = 0U;
        return r_out_use_condition(check, id, input, depth + 1U, flow, &when_true, &when_false);
    }
    if (node.kind == R_HIR_IF || node.kind == R_HIR_STATIC_IF || node.kind == R_HIR_CONDITIONAL) {
        uint32_t when_true = 0U, when_false = 0U;
        if (!r_out_use_condition(check,
                                 context->hir_children[node.first_child],
                                 input,
                                 depth + 1U,
                                 flow,
                                 &when_true,
                                 &when_false))
            return false;
        flow->normal = 0U;
        for (uint32_t branch = 1U; branch <= 2U; ++branch) {
            const uint32_t branch_input = branch == 1U ? when_true : when_false;
            if (branch >= node.child_count) {
                flow->normal |= branch_input;
                continue;
            }
            ROutUseFlow arm = {0};
            const bool ok = r_out_use_walk(check,
                                           context->hir_children[node.first_child + branch],
                                           branch_input,
                                           depth + 1U,
                                           &arm) &&
                            r_out_use_merge(check, flow, &arm);
            flow->normal |= arm.normal;
            r_out_use_free(check, &arm);
            if (!ok)
                return false;
        }
        return true;
    }
    if (node.kind == R_HIR_WHILE || node.kind == R_HIR_FOR) {
        uint32_t header = input;
        flow->normal = 0U;
        for (uint32_t iteration = 0U; iteration < 3U && !check->reported; ++iteration) {
            ROutUseFlow condition = {0}, body = {0};
            uint32_t when_true = 0U, when_false = 0U;
            bool ok = r_out_use_condition(check,
                                          context->hir_children[node.first_child],
                                          header,
                                          depth + 1U,
                                          &condition,
                                          &when_true,
                                          &when_false) &&
                      r_out_use_merge(check, flow, &condition);
            flow->normal |= when_false;
            if (ok)
                ok = r_out_use_walk(check,
                                    context->hir_children[node.first_child + 1U],
                                    when_true,
                                    depth + 1U,
                                    &body);
            uint32_t repeat = body.normal;
            for (size_t i = 0U; ok && i < body.count; ++i) {
                const ROutUseExit exit = body.exits[i];
                /* A jump's `error` is its loop_target: above 1 it leaves this loop too. */
                if ((exit.kind == R_HIR_BREAK || exit.kind == R_HIR_CONTINUE) && exit.error > 1U)
                    ok = r_out_use_exit(check, flow, exit.kind, exit.error - 1U, exit.states);
                else if (exit.kind == R_HIR_BREAK)
                    flow->normal |= exit.states;
                else if (exit.kind == R_HIR_CONTINUE)
                    repeat |= exit.states;
                else
                    ok = r_out_use_exit(check, flow, exit.kind, exit.error, exit.states);
            }
            if (ok && node.kind == R_HIR_FOR) {
                ROutUseFlow increment = {0};
                ok = r_out_use_walk(check,
                                    context->hir_children[node.first_child + 2U],
                                    repeat,
                                    depth + 1U,
                                    &increment) &&
                     r_out_use_merge(check, flow, &increment);
                repeat = increment.normal;
                r_out_use_free(check, &increment);
            }
            r_out_use_free(check, &condition);
            r_out_use_free(check, &body);
            if (!ok)
                return false;
            const uint32_t next = header | repeat;
            if (next == header)
                break;
            header = next;
        }
        return true;
    }
    if (node.kind == R_HIR_SWITCH) {
        if (!r_out_use_step(check, context->hir_children[node.first_child], depth, flow))
            return false;
        const uint32_t branch_input = flow->normal;
        flow->normal = 0U;
        uint32_t fallthrough = 0U;
        for (uint32_t i = 1U; i < node.child_count; ++i) {
            const RHirNodeId case_id = context->hir_children[node.first_child + i];
            const RHirNode *arm_node = &context->hir_nodes[case_id - 1U];
            ROutUseFlow arm = {0};
            bool ok = r_out_use_walk(check, case_id, branch_input | fallthrough, depth + 1U, &arm);
            fallthrough = arm_node->operation == R_TOKEN_KW_FALLTHROUGH ? arm.normal : 0U;
            if (fallthrough == 0U)
                flow->normal |= arm.normal;
            for (size_t j = 0U; ok && j < arm.count; ++j) {
                const ROutUseExit exit = arm.exits[j];
                if (exit.kind == R_HIR_BREAK && exit.error == 0U)
                    flow->normal |= exit.states;
                else
                    ok = r_out_use_exit(check, flow, exit.kind, exit.error, exit.states);
            }
            r_out_use_free(check, &arm);
            if (!ok)
                return false;
        }
        /* Semantic lowering requires exhaustive coverage, including a default for
           integer/character switches. There is no unmatched normal path. */
        return true;
    }
    if (node.kind == R_HIR_TRY && node.operation == R_TOKEN_KW_TRY) {
        ROutUseFlow tried = {0};
        bool ok = r_out_use_walk(
            check, context->hir_children[node.first_child], input, depth + 1U, &tried);
        flow->normal = tried.normal;
        const uint32_t catches = (uint32_t)node.integer_value;
        for (size_t i = 0U; ok && i < tried.count; ++i) {
            const ROutUseExit exit = tried.exits[i];
            bool handled = false;
            for (uint32_t j = 0U; ok && exit.kind == R_HIR_THROW && j < catches; ++j) {
                const RHirNodeId catch_id = context->hir_children[node.first_child + j + 1U];
                const RTypeId caught = context->hir_nodes[catch_id - 1U].type;
                const bool abstract =
                    (r_generic_parameter_constraints(context, exit.error) & R_GENERIC_ERRORS) != 0U;
                if (caught != exit.error && !abstract)
                    continue;
                ROutUseFlow caught_flow = {0};
                ok = r_out_use_walk(check, catch_id, exit.states, depth + 1U, &caught_flow) &&
                     r_out_use_merge(check, flow, &caught_flow);
                flow->normal |= caught_flow.normal;
                r_out_use_free(check, &caught_flow);
                handled = !abstract;
                if (handled)
                    break;
            }
            if (ok && !handled)
                ok = r_out_use_exit(check, flow, exit.kind, exit.error, exit.states);
        }
        r_out_use_free(check, &tried);
        if (!ok)
            return false;
        if (node.child_count == catches + 2U) {
            ROutUseFlow pending = *flow;
            memset(flow, 0, sizeof(*flow));
            const RHirNodeId finalizer = context->hir_children[node.first_child + catches + 1U];
            for (size_t i = 0U; ok && i <= pending.count; ++i) {
                const uint32_t states = i == 0U ? pending.normal : pending.exits[i - 1U].states;
                ROutUseFlow final = {0};
                ok = r_out_use_walk(check, finalizer, states, depth + 1U, &final) &&
                     r_out_use_merge(check, flow, &final);
                if (i == 0U)
                    flow->normal |= final.normal;
                else if (ok)
                    ok = r_out_use_exit(check,
                                        flow,
                                        pending.exits[i - 1U].kind,
                                        pending.exits[i - 1U].error,
                                        final.normal);
                r_out_use_free(check, &final);
            }
            r_out_use_free(check, &pending);
        }
        return ok;
    }

    for (uint32_t i = 0U; i < node.child_count; ++i) {
        const RHirNodeId child = context->hir_children[node.first_child + i];
        if (node.kind == R_HIR_RETURN && context->hir_nodes[child - 1U].out_commit)
            continue;
        if (!r_out_use_step(check, child, depth, flow))
            return false;
    }
    if (check->initialization &&
        (node.kind == R_HIR_FIELD_PLACE || node.kind == R_HIR_INDEX_PLACE ||
         node.kind == R_HIR_DEREF_PLACE) &&
        node.symbol == check->binding && !r_out_use_lost(check, flow->normal, node.span))
        return false;
    if (check->ordinary_result && node.kind == R_HIR_DEREF_PLACE && node.symbol == check->binding &&
        flow->normal != 0U)
        flow->normal = R_OUT_USE_CLEAR;
    /* R-STMT-0018: group.cancel_all() consumes every unconsumed member, like a consuming cancel. */
    if (check->ordinary_result && node.kind == R_HIR_TASK_SCOPE_CLOSE &&
        node.operation == R_TOKEN_KW_DROP && check->binding != 0U &&
        context->semantic_symbols[check->binding - 1U].task_scope == node.symbol &&
        flow->normal != 0U)
        flow->normal = R_OUT_USE_CLEAR;
    const bool read = node.kind == R_HIR_LOAD || node.kind == R_HIR_MOVE ||
                      (node.kind == R_HIR_BORROW && !node.out_argument) ||
                      node.kind == R_HIR_LENGTH || node.kind == R_HIR_SLICE ||
                      (node.kind == R_HIR_DROP && node.operation == R_TOKEN_KW_DROP) ||
                      node.kind == R_HIR_AWAIT || node.kind == R_HIR_COMPOUND_ASSIGN;
    if (read && node.child_count != 0U && flow->normal != 0U) {
        const RHirNodeId place = node.kind == R_HIR_AWAIT
                                     ? context->hir_children[node.first_child]
                                     : r_body_call_argument_access_place(&check->body, id);
        const RBodyPlaceRelation relation = r_out_use_relation(check, place);
        if (relation == R_BODY_PLACE_RELATION_SAME ||
            relation == R_BODY_PLACE_RELATION_OVERLAPPING) {
            if (check->initialization && !r_out_use_lost(check, flow->normal, node.span))
                return false;
            const bool consumes =
                node.kind == R_HIR_DROP ||
                (node.kind == R_HIR_MOVE && !r_semantic_type_is_copy(context, node.type));
            flow->normal = check->initialization && consumes ? R_OUT_USE_PENDING : R_OUT_USE_CLEAR;
        }
    }
    if (node.kind == R_HIR_ASSIGN || node.kind == R_HIR_COMPOUND_ASSIGN) {
        const RHirNodeId place =
            node.out_place != 0U ? node.out_place : context->hir_children[node.first_child];
        const RBodyPlaceRelation relation = r_out_use_relation(check, place);
        /* Updating a component uses an ordinary aggregate result as storage;
           replacing its whole local is the operation that loses that result.
           An out obligation may name the component itself and stays distinct. */
        if (check->ordinary_result && relation == R_BODY_PLACE_RELATION_OVERLAPPING &&
            flow->normal != 0U)
            flow->normal = R_OUT_USE_CLEAR;
        if (check->initialization) {
            if (relation == R_BODY_PLACE_RELATION_SAME)
                flow->normal = flow->normal != 0U ? R_OUT_USE_CLEAR : 0U;
        } else if (relation != R_BODY_PLACE_RELATION_DISJOINT &&
                   !r_out_use_lost(check, flow->normal, node.span))
            return false;
    }
    for (uint32_t i = 0U; i < node.effect_exit_count; ++i)
        if (!r_out_use_exit(check,
                            flow,
                            R_HIR_THROW,
                            context->hir_effect_exits[node.first_effect_exit + i].error_type,
                            flow->normal))
            return false;
    if (id == check->producer && flow->normal != 0U)
        flow->normal = R_OUT_USE_PENDING;
    if (node.kind == R_HIR_RETURN || node.kind == R_HIR_THROW || node.kind == R_HIR_BREAK ||
        node.kind == R_HIR_CONTINUE) {
        if (!r_out_use_exit(check,
                            flow,
                            node.kind,
                            node.kind == R_HIR_THROW    ? node.auxiliary_type
                            : node.kind == R_HIR_RETURN ? 0U
                                                        : node.loop_target,
                            flow->normal))
            return false;
        flow->normal = 0U;
    }
    if (r_semantic_value_kind(context, node.type) == R_SEMANTIC_TYPE_NEVER)
        flow->normal = 0U;
    if (node.kind == R_HIR_BLOCK) {
        bool closes_binding = false;
        for (uint32_t i = 0U; i < node.child_count; ++i) {
            const RHirNode *child =
                &context->hir_nodes[context->hir_children[node.first_child + i] - 1U];
            closes_binding =
                closes_binding || (child->kind == R_HIR_LOCAL && child->symbol == check->binding);
        }
        if (closes_binding) {
            uint32_t states = flow->normal;
            for (size_t i = 0U; i < flow->count; ++i)
                if (flow->exits[i].kind != R_HIR_THROW &&
                    (!check->initialization || flow->exits[i].kind == R_HIR_RETURN) &&
                    (check->initialization || flow->exits[i].kind != R_HIR_RETURN ||
                     context->semantic_symbols[check->binding - 1U].out_parameter == 0U))
                    states |= flow->exits[i].states;
            if (!r_out_use_lost(check, states, check->span))
                return false;
            if (!check->initialization)
                for (size_t i = 0U; i < flow->count; ++i)
                    if (flow->exits[i].kind == R_HIR_THROW)
                        flow->exits[i].states = R_OUT_USE_CLEAR;
        }
    }
    return true;
}

static bool r_out_validate_uses_with(RFrontendContext *context, const RHirNodeId *first_place);

/* M32-7: the first PLACE node of each symbol, found once, replaces a scan of every HIR node per
   checked local, which made the check quadratic in the size of the program. */
static bool r_out_validate_uses(RFrontendContext *context) {
    const size_t count = context->semantic_symbol_count + 1U;
    RHirNodeId *first_place;
    bool ok;

    if (count > SIZE_MAX / sizeof(*first_place)) {
        context->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
        return false;
    }
    first_place = r_context_allocate(context, count * sizeof(*first_place));
    if (first_place == NULL)
        return false;
    (void)memset(first_place, 0, count * sizeof(*first_place));
    for (size_t j = 0U; j < context->hir_node_count; ++j) {
        const RHirNode *node = &context->hir_nodes[j];
        if (node->kind == R_HIR_PLACE && node->symbol != 0U && node->symbol < count &&
            first_place[node->symbol] == 0U)
            first_place[node->symbol] = (RHirNodeId)j + 1U;
    }
    ok = r_out_validate_uses_with(context, first_place);
    r_context_free(context, first_place);
    return ok;
}

static bool r_out_validate_uses_with(RFrontendContext *context, const RHirNodeId *first_place) {
    for (size_t i = 0U; i < context->semantic_symbol_count; ++i) {
        const RSemanticSymbol *binding = &context->semantic_symbols[i];
        if (binding->out_parameter == 0U)
            continue;
        const RSemanticSymbol *function = &context->semantic_symbols[binding->parent - 1U];
        if (function->hir_node == 0U)
            continue;
        ROutUseCheck check = {0};
        check.body.frontend = context;
        check.body.function_symbol = binding->parent;
        check.binding = (RSymbolId)i + 1U;
        check.producer = binding->hir_node;
        check.span = binding->name_span;
        check.initialization = true;
        check.place = first_place[check.binding];
        if (check.place == 0U)
            continue;
        ROutUseFlow flow = {0};
        bool ok = r_out_use_walk(&check, function->hir_node, R_OUT_USE_PENDING, 0U, &flow);
        uint32_t states = flow.normal;
        for (size_t j = 0U; j < flow.count; ++j)
            if (flow.exits[j].kind == R_HIR_RETURN)
                states |= flow.exits[j].states;
        ok = ok && r_out_use_lost(&check, states, binding->name_span);
        r_out_use_free(&check, &flow);
        if (!ok)
            return false;
    }
    for (size_t i = 0U; i < context->hir_node_count; ++i) {
        const RHirNode *producer = &context->hir_nodes[i];
        RSymbolId destination = 0U;
        RHirNodeId place = 0U;
        bool ordinary_result = false;
        if (producer->kind == R_HIR_ASSIGN && producer->out_argument && producer->out_place != 0U) {
            destination = producer->out_destination;
            place = producer->out_place;
        } else if (producer->kind == R_HIR_LOCAL && producer->child_count != 0U &&
                   producer->symbol != 0U &&
                   !context->semantic_symbols[producer->symbol - 1U].is_hidden_local &&
                   r_semantic_significant_result(context, (RHirNodeId)i + 1U, 0U)) {
            destination = producer->symbol;
            ordinary_result = true;
            place = first_place[destination];
        } else if (producer->kind == R_HIR_ASSIGN && producer->child_count == 2U &&
                   producer->out_place == 0U && !producer->out_commit &&
                   r_semantic_significant_result(
                       context, context->hir_children[producer->first_child + 1U], 0U)) {
            place = context->hir_children[producer->first_child];
            if (context->hir_nodes[place - 1U].kind != R_HIR_PLACE)
                continue;
            destination = context->hir_nodes[place - 1U].symbol;
            ordinary_result = true;
        }
        if (destination == 0U)
            continue;
        const RSemanticSymbol *binding = &context->semantic_symbols[destination - 1U];
        if ((binding->kind != R_SEMANTIC_SYMBOL_LOCAL &&
             binding->kind != R_SEMANTIC_SYMBOL_PARAMETER) ||
            binding->is_hidden_local || (ordinary_result && binding->translation_time_read))
            continue;
        const RSemanticTypeKind destination_kind = r_semantic_value_kind(context, binding->type);
        if (binding->kind == R_SEMANTIC_SYMBOL_PARAMETER &&
            (destination_kind == R_SEMANTIC_TYPE_BORROW ||
             destination_kind == R_SEMANTIC_TYPE_SLICE))
            continue; /* The output is forwarded into the caller's storage. */
        const RSemanticSymbol *function = &context->semantic_symbols[binding->parent - 1U];
        if (function->hir_node == 0U)
            continue;
        ROutUseCheck check = {0};
        check.body.frontend = context;
        check.body.function_symbol = binding->parent;
        check.producer = (RHirNodeId)i + 1U;
        check.span = producer->span;
        check.place = place;
        check.binding = destination;
        check.ordinary_result = ordinary_result;
        ROutUseFlow flow = {0};
        bool ok = r_out_use_walk(&check, function->hir_node, R_OUT_USE_CLEAR, 0U, &flow);
        uint32_t states = flow.normal;
        for (size_t j = 0U; j < flow.count; ++j)
            if (flow.exits[j].kind != R_HIR_THROW &&
                (flow.exits[j].kind != R_HIR_RETURN || binding->out_parameter == 0U))
                states |= flow.exits[j].states;
        ok = ok && r_out_use_lost(&check, states, producer->span);
        r_out_use_free(&check, &flow);
        if (!ok)
            return false;
    }
    return true;
}

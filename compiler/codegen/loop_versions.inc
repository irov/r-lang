/* Loop versioning of affine indices (P4.4), an optimization under Core R-AM-0003.
 *
 * An innermost loop `for (...; k < B; k += 1) { ... base[A + k * S] ... }` checks its index
 * against len(base) in every iteration (R-EXPR-0021). When k is an unsigned local that only the
 * step changes, and B, A, S and the length of base have the same value in every iteration, the
 * index grows with k and its largest value is A + (B - 1) * S. If that value is computed without
 * wrapping and lies below len(base), no iteration can fail the check; in R unsigned arithmetic
 * wraps, so the absence of wrapping at the largest value also shows that every iteration computes
 * the exact affine value. The emitter tests this once before the loop and runs a copy of the loop
 * without those checks when it holds, the loop as written otherwise, so both copies behave alike
 * wherever the checks cannot fail and the loop as written runs wherever one might.
 *
 * The walk of index_proofs.inc calls r_c17_version_loop when it enters a for loop, right after it
 * stamped every object the loop changes: an object whose stamp precedes that serial keeps its
 * value through the loop. The invariant parts are literals, loads of such unsigned locals and
 * parameters, lengths of such slices and arrays, and their wrapping sums, differences and
 * products, which evaluate without a check or a side effect and may be evaluated again before the
 * loop. A loop body is copied only when it consists of statements and expressions that the
 * emitter writes the same way twice: no nested loop, return, throw, try, switch or task
 * operation, no local with a drop obligation, and calls only of synchronous functions without
 * checked errors. */

static bool r_c17_version_is_size(const RC17Emitter *emitter, RTypeId type) {
    return r_c17_value_kind(emitter, type) == R_SEMANTIC_TYPE_USIZE;
}

static bool r_c17_version_invariant(const RC17Emitter *emitter,
                                    RHirNodeId node_id,
                                    uint32_t guard,
                                    uint32_t depth) {
    const RHirNode *node = r_c17_node(emitter, node_id);

    if ((node == NULL) || (depth >= R_C17_INDEX_RANGE_DEPTH_LIMIT) ||
        !r_c17_version_is_size(emitter, node->type)) {
        return false;
    }
    switch (node->kind) {
    case R_HIR_LITERAL:
        return true;
    case R_HIR_LOAD: {
        const RHirNode *place = r_c17_node(emitter, r_c17_child(emitter, node, 0U));

        return (place != NULL) && (place->kind == R_HIR_PLACE) &&
               r_c17_index_subject(emitter, place->symbol) &&
               r_c17_index_trusted(emitter, place->symbol, guard);
    }
    case R_HIR_LENGTH: {
        const RSymbolId base = r_c17_index_length_base(emitter, node_id);

        return (base != R_SYMBOL_ID_INVALID) && r_c17_index_trusted(emitter, base, guard);
    }
    case R_HIR_BINARY:
        return ((node->operation == R_TOKEN_PLUS) || (node->operation == R_TOKEN_MINUS) ||
                (node->operation == R_TOKEN_STAR)) &&
               r_c17_version_invariant(
                   emitter, r_c17_child(emitter, node, 0U), guard, depth + 1U) &&
               r_c17_version_invariant(emitter, r_c17_child(emitter, node, 1U), guard, depth + 1U);
    default:
        return false;
    }
}

static bool
r_c17_version_is_induction(const RC17Emitter *emitter, RHirNodeId node_id, RSymbolId induction) {
    const RHirNode *node = r_c17_node(emitter, node_id);
    const RHirNode *place = (node == NULL) || (node->kind != R_HIR_LOAD)
                                ? NULL
                                : r_c17_node(emitter, r_c17_child(emitter, node, 0U));

    return (place != NULL) && (place->kind == R_HIR_PLACE) && (place->symbol == induction);
}

/* `k`, `k * S` or `S * k`: true with the stride node, R_HIR_NODE_ID_INVALID for one. */
static bool r_c17_version_scaled(const RC17Emitter *emitter,
                                 RHirNodeId node_id,
                                 RSymbolId induction,
                                 uint32_t guard,
                                 RHirNodeId *stride) {
    const RHirNode *node = r_c17_node(emitter, node_id);

    if (r_c17_version_is_induction(emitter, node_id, induction)) {
        *stride = R_HIR_NODE_ID_INVALID;
        return true;
    }
    if ((node == NULL) || (node->kind != R_HIR_BINARY) || (node->operation != R_TOKEN_STAR) ||
        !r_c17_version_is_size(emitter, node->type)) {
        return false;
    }
    if (r_c17_version_is_induction(emitter, r_c17_child(emitter, node, 0U), induction) &&
        r_c17_version_invariant(emitter, r_c17_child(emitter, node, 1U), guard, 0U)) {
        *stride = r_c17_child(emitter, node, 1U);
        return true;
    }
    if (r_c17_version_is_induction(emitter, r_c17_child(emitter, node, 1U), induction) &&
        r_c17_version_invariant(emitter, r_c17_child(emitter, node, 0U), guard, 0U)) {
        *stride = r_c17_child(emitter, node, 0U);
        return true;
    }
    return false;
}

/* An index `k * S`, `A + k * S` or `k * S + A` (k * S also as k or S * k). */
static bool r_c17_version_affine(const RC17Emitter *emitter,
                                 RHirNodeId node_id,
                                 RSymbolId induction,
                                 uint32_t guard,
                                 RHirNodeId *offset,
                                 RHirNodeId *stride) {
    const RHirNode *node = r_c17_node(emitter, node_id);

    if ((node == NULL) || !r_c17_version_is_size(emitter, node->type)) {
        return false;
    }
    *offset = R_HIR_NODE_ID_INVALID;
    if (r_c17_version_scaled(emitter, node_id, induction, guard, stride)) {
        return true;
    }
    if ((node->kind != R_HIR_BINARY) || (node->operation != R_TOKEN_PLUS)) {
        return false;
    }
    if (r_c17_version_scaled(emitter, r_c17_child(emitter, node, 0U), induction, guard, stride) &&
        r_c17_version_invariant(emitter, r_c17_child(emitter, node, 1U), guard, 0U)) {
        *offset = r_c17_child(emitter, node, 1U);
        return true;
    }
    if (r_c17_version_scaled(emitter, r_c17_child(emitter, node, 1U), induction, guard, stride) &&
        r_c17_version_invariant(emitter, r_c17_child(emitter, node, 0U), guard, 0U)) {
        *offset = r_c17_child(emitter, node, 0U);
        return true;
    }
    return false;
}

/* Whether the emitter writes a part of a loop body the same way when it writes it twice. */
static bool r_c17_version_copyable(const RC17Emitter *emitter, RHirNodeId node_id, uint32_t depth) {
    const RHirNode *node = r_c17_node(emitter, node_id);
    uint32_t child_index;

    if ((node == NULL) || (depth >= R_C17_INDEX_WALK_DEPTH_LIMIT)) {
        return false;
    }
    switch (node->kind) {
    case R_HIR_BLOCK:
    case R_HIR_EXPRESSION_STATEMENT:
    case R_HIR_IF:
    case R_HIR_BREAK:
    case R_HIR_CONTINUE:
    case R_HIR_LITERAL:
    case R_HIR_LOAD:
    case R_HIR_PLACE:
    case R_HIR_FIELD_PLACE:
    case R_HIR_INDEX_PLACE:
    case R_HIR_DEREF_PLACE:
    case R_HIR_LENGTH:
    case R_HIR_BINARY:
    case R_HIR_UNARY:
    case R_HIR_CAST:
    case R_HIR_ASSIGN:
    case R_HIR_COMPOUND_ASSIGN:
        break;
    case R_HIR_LOCAL:
        if (r_c17_type_requires_drop(emitter, node->type) ||
            r_c17_type_requires_init_flag(emitter, node->type)) {
            return false;
        }
        break;
    case R_HIR_CALL: {
        const RSemanticSymbol *callee = r_c17_symbol(emitter, node->symbol);

        if ((callee == NULL) || (callee->kind != R_SEMANTIC_SYMBOL_FUNCTION) || callee->is_async ||
            (callee->effect_carrier_type != R_TYPE_ID_INVALID) ||
            r_semantic_dyn_dispatcher(emitter->frontend, node->symbol) ||
            r_semantic_function_value_dispatcher(emitter->frontend, node->symbol) ||
            r_c17_type_requires_drop(emitter, node->type)) {
            return false;
        }
        break;
    }
    default:
        return false;
    }
    for (child_index = 0U; child_index < node->child_count; ++child_index) {
        if (!r_c17_version_copyable(emitter, r_c17_child(emitter, node, child_index), depth + 1U)) {
            return false;
        }
    }
    return true;
}

/* Whether a part of the body names the induction variable as the target of a store. */
static bool r_c17_version_stores(const RC17Emitter *emitter,
                                 RHirNodeId node_id,
                                 RSymbolId induction,
                                 uint32_t depth) {
    const RHirNode *node = r_c17_node(emitter, node_id);
    uint32_t child_index;

    if ((node == NULL) || (depth >= R_C17_INDEX_WALK_DEPTH_LIMIT)) {
        return true;
    }
    if ((node->kind == R_HIR_ASSIGN) || (node->kind == R_HIR_COMPOUND_ASSIGN)) {
        const RHirNode *target = r_c17_node(emitter, r_c17_child(emitter, node, 0U));

        if ((target == NULL) || ((target->kind == R_HIR_PLACE) && (target->symbol == induction))) {
            return true;
        }
    }
    for (child_index = 0U; child_index < node->child_count; ++child_index) {
        if (r_c17_version_stores(
                emitter, r_c17_child(emitter, node, child_index), induction, depth + 1U)) {
            return true;
        }
    }
    return false;
}

static bool r_c17_version_collect(
    RC17Emitter *emitter, RHirNodeId node_id, RSymbolId induction, uint32_t guard, uint32_t depth) {
    const RHirNode *node = r_c17_node(emitter, node_id);
    uint32_t child_index;

    if ((node == NULL) || (depth >= R_C17_INDEX_WALK_DEPTH_LIMIT)) {
        return true;
    }
    if (node->kind == R_HIR_INDEX_PLACE) {
        const RHirNode *base = r_c17_node(emitter, r_c17_child(emitter, node, 0U));
        const RSemanticType *base_type =
            base == NULL ? NULL : r_c17_type(emitter, r_c17_value_type(emitter, base->type));
        RHirNodeId offset = R_HIR_NODE_ID_INVALID;
        RHirNodeId stride = R_HIR_NODE_ID_INVALID;

        if ((base_type != NULL) &&
            ((base_type->kind == R_SEMANTIC_TYPE_SLICE) ||
             (base_type->kind == R_SEMANTIC_TYPE_ARRAY)) &&
            (base->kind == R_HIR_PLACE) && r_c17_index_base(emitter, base->symbol) &&
            r_c17_index_trusted(emitter, base->symbol, guard) &&
            r_c17_version_affine(
                emitter, r_c17_child(emitter, node, 1U), induction, guard, &offset, &stride)) {
            RC17VersionedIndex *entry;

            if (emitter->versioned_index_count == emitter->versioned_index_capacity) {
                const size_t capacity = emitter->versioned_index_capacity == 0U
                                            ? 16U
                                            : emitter->versioned_index_capacity * 2U;
                RC17VersionedIndex *grown;

                if (capacity > SIZE_MAX / sizeof(*grown)) {
                    return r_c17_fail(emitter, R_FRONTEND_LIMIT_EXCEEDED);
                }
                grown = r_c17_allocate(emitter, capacity * sizeof(*grown));
                if (grown == NULL) {
                    return false;
                }
                if (emitter->versioned_index_count != 0U) {
                    (void)memcpy(grown,
                                 emitter->versioned_indices,
                                 emitter->versioned_index_count * sizeof(*grown));
                }
                r_c17_free(emitter, emitter->versioned_indices);
                emitter->versioned_indices = grown;
                emitter->versioned_index_capacity = capacity;
            }
            entry = &emitter->versioned_indices[emitter->versioned_index_count++];
            entry->place = node_id;
            entry->offset = offset;
            entry->stride = stride;
            entry->base = base->symbol;
        }
    }
    for (child_index = 0U; child_index < node->child_count; ++child_index) {
        if (!r_c17_version_collect(
                emitter, r_c17_child(emitter, node, child_index), induction, guard, depth + 1U)) {
            return false;
        }
    }
    return true;
}

/* Records the loop when it qualifies; false only when emission must fail. */
static bool r_c17_version_loop(RC17Emitter *emitter,
                               RHirNodeId node_id,
                               const RHirNode *node,
                               uint32_t loop_guard) {
    const RHirNode *condition =
        node->child_count == 3U ? r_c17_node(emitter, r_c17_child(emitter, node, 0U)) : NULL;
    const RHirNode *step =
        node->child_count == 3U ? r_c17_node(emitter, r_c17_child(emitter, node, 2U)) : NULL;
    const RHirNode *increment =
        (step == NULL) || (step->kind != R_HIR_EXPRESSION_STATEMENT) || (step->child_count != 1U)
            ? NULL
            : r_c17_node(emitter, r_c17_child(emitter, step, 0U));
    const RHirNode *target = (increment == NULL) || (increment->kind != R_HIR_COMPOUND_ASSIGN) ||
                                     (increment->operation != R_TOKEN_PLUS_EQUAL) ||
                                     (increment->child_count != 2U)
                                 ? NULL
                                 : r_c17_node(emitter, r_c17_child(emitter, increment, 0U));
    const RHirNode *amount =
        target == NULL ? NULL : r_c17_node(emitter, r_c17_child(emitter, increment, 1U));
    const RHirNode *counter = (condition == NULL) || (condition->kind != R_HIR_BINARY) ||
                                      (condition->operation != R_TOKEN_LESS) ||
                                      (condition->child_count != 2U)
                                  ? NULL
                                  : r_c17_node(emitter, r_c17_child(emitter, condition, 0U));
    const RHirNode *counter_place = (counter == NULL) || (counter->kind != R_HIR_LOAD)
                                        ? NULL
                                        : r_c17_node(emitter, r_c17_child(emitter, counter, 0U));
    const size_t first_index = emitter->versioned_index_count;
    RC17LoopVersion *entry;

    if ((emitter->index_proof_generation == UINT32_C(0)) || (target == NULL) ||
        (target->kind != R_HIR_PLACE) || (amount == NULL) || (amount->kind != R_HIR_LITERAL) ||
        (amount->integer_value != UINT64_C(1)) || (counter_place == NULL) ||
        (counter_place->kind != R_HIR_PLACE) || (counter_place->symbol != target->symbol) ||
        !r_c17_version_is_size(emitter, counter_place->type) ||
        !r_c17_index_subject(emitter, target->symbol) ||
        !r_c17_version_invariant(emitter, r_c17_child(emitter, condition, 1U), loop_guard, 0U) ||
        !r_c17_version_copyable(emitter, r_c17_child(emitter, node, 1U), 0U) ||
        r_c17_version_stores(emitter, r_c17_child(emitter, node, 1U), target->symbol, 0U)) {
        return true;
    }
    if (!r_c17_version_collect(
            emitter, r_c17_child(emitter, node, 1U), target->symbol, loop_guard, 0U)) {
        return false;
    }
    if (emitter->versioned_index_count == first_index) {
        return true;
    }
    if (emitter->loop_version_count == emitter->loop_version_capacity) {
        const size_t capacity =
            emitter->loop_version_capacity == 0U ? 8U : emitter->loop_version_capacity * 2U;
        RC17LoopVersion *grown;

        if (capacity > SIZE_MAX / sizeof(*grown)) {
            return r_c17_fail(emitter, R_FRONTEND_LIMIT_EXCEEDED);
        }
        grown = r_c17_allocate(emitter, capacity * sizeof(*grown));
        if (grown == NULL) {
            return false;
        }
        if (emitter->loop_version_count != 0U) {
            (void)memcpy(
                grown, emitter->loop_versions, emitter->loop_version_count * sizeof(*grown));
        }
        r_c17_free(emitter, emitter->loop_versions);
        emitter->loop_versions = grown;
        emitter->loop_version_capacity = capacity;
    }
    entry = &emitter->loop_versions[emitter->loop_version_count++];
    entry->loop = node_id;
    entry->bound = r_c17_child(emitter, condition, 1U);
    entry->induction = target->symbol;
    entry->first_index = first_index;
    entry->index_count = emitter->versioned_index_count - first_index;
    return true;
}

/* The loop of the body being emitted that the node versions, or NULL. */
static const RC17LoopVersion *r_c17_version_for_loop(const RC17Emitter *emitter,
                                                     const RHirNode *node) {
    const uintptr_t first = (uintptr_t)emitter->frontend->hir_nodes;
    const uintptr_t address = (uintptr_t)node;
    RHirNodeId node_id;
    size_t index;

    if ((emitter->index_proof_active == UINT32_C(0)) || (address < first) ||
        ((address - first) % sizeof(*node) != 0U)) {
        return NULL;
    }
    node_id = (RHirNodeId)((address - first) / sizeof(*node) + 1U);
    for (index = 0U; index < emitter->loop_version_count; ++index) {
        if (emitter->loop_versions[index].loop == node_id) {
            return &emitter->loop_versions[index];
        }
    }
    return NULL;
}

/* Whether the index place lies in the copy of a loop whose indices were checked before it. */
static bool r_c17_index_versioned(const RC17Emitter *emitter, const RHirNode *place) {
    const uintptr_t first = (uintptr_t)emitter->frontend->hir_nodes;
    const uintptr_t address = (uintptr_t)place;
    const RC17LoopVersion *loop;
    RHirNodeId node_id;
    size_t index;

    if ((emitter->loop_version_active == 0U) || (emitter->index_proof_active == UINT32_C(0)) ||
        (emitter->loop_version_active > emitter->loop_version_count) || (address < first) ||
        ((address - first) % sizeof(*place) != 0U)) {
        return false;
    }
    node_id = (RHirNodeId)((address - first) / sizeof(*place) + 1U);
    loop = &emitter->loop_versions[emitter->loop_version_active - 1U];
    for (index = 0U; index < loop->index_count; ++index) {
        if (emitter->versioned_indices[loop->first_index + index].place == node_id) {
            return true;
        }
    }
    return false;
}

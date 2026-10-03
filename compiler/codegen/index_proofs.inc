/* Index proofs (P4.3). R-EXPR-0021 requires a bounds panic for an index at or past the bound of
 * its base. Where the index is proven below that bound at the point of the index, the panic is
 * unreachable and the check is not emitted; every other index keeps it.
 *
 * Before the body of a synchronous function is emitted, one forward walk over its HIR keeps
 * facts about the local and parameter objects of the body:
 *   - RANGE:   low <= subject <= high, for an unsigned integer subject;
 *   - BELOW:   subject < len(base), for a slice or array<T> base;
 *   - AT_MOST: subject <= len(base);
 *   - LENGTH:  subject == len(base);
 *   - MINIMUM: len(base) >= low.
 * Facts come from initializers and plain assignments (a slice `b[a..a + K]` holds K elements),
 * the conditions of if, while and for statements, the operands of &&, || and ?:, and an if
 * statement whose only branch always leaves the block. Before a statement is walked, every fact
 * about an object the statement may change dies; a loop, switch or try forgets the facts about
 * every object it changes anywhere before its first part is walked, so a fact never survives a path
 * that changes its objects. An object whose storage is borrowed exclusively anywhere in the body,
 * that receives an output argument, or that appears as an operand the walk does not understand
 * carries no facts in that body: a change through a borrow is not visible at a statement.
 *
 * An index is proven when its value range lies below a fixed-array bound, when it is no larger
 * than a subject with a live BELOW fact for its slice or array<T> base (the subject itself, an
 * AND with it, a right shift, quotient or unsigned cast of it), when it is a remainder by that
 * length or by a subject that does not exceed it, or when its value range lies below a live
 * MINIMUM of that base. Value ranges come from literals, unsigned types, casts, masks,
 * shifts, divisions and remainders, and from arithmetic that cannot wrap. A range never depends
 * on a fact outside the walk, so the same evaluation without facts proves constant and masked
 * indices anywhere, asynchronous bodies included. An explicit integer conversion whose operand
 * range fits its target loses its conversion check (R-EXPR-0015) by the same proofs. */

enum {
    /* Deeper statement nesting abandons the proofs of the body; deeper expressions only lose
       their range, which the unsigned type still bounds. */
    R_C17_INDEX_WALK_DEPTH_LIMIT = 1024,
    R_C17_INDEX_RANGE_DEPTH_LIMIT = 64
};

static bool r_c17_index_walk_statement(RC17Emitter *emitter, RHirNodeId node_id, uint32_t depth);
static bool r_c17_index_walk_expression(RC17Emitter *emitter,
                                        RHirNodeId node_id,
                                        uint32_t guard,
                                        uint32_t depth);

/* The non-negative values of an integer or bool type. A signed type yields its non-negative
   half, which describes a value only once that value is known not to be negative. */
static bool r_c17_index_type_range(const RC17Emitter *emitter,
                                   RTypeId type,
                                   bool *is_signed,
                                   uint32_t *width,
                                   RC17IndexRange *range) {
    const RSemanticTypeKind kind = r_c17_value_kind(emitter, type);

    if (kind == R_SEMANTIC_TYPE_BOOL) {
        *is_signed = false;
        *width = UINT32_C(1);
        range->low = UINT64_C(0);
        range->high = UINT64_C(1);
        return true;
    }
    if (!r_c17_integer_properties(kind, is_signed, width)) {
        return false;
    }
    range->low = UINT64_C(0);
    if (*is_signed) {
        range->high = (UINT64_C(1) << (*width - UINT32_C(1))) - UINT64_C(1);
    } else {
        range->high = *width == UINT32_C(64) ? UINT64_MAX : (UINT64_C(1) << *width) - UINT64_C(1);
    }
    return true;
}

static bool r_c17_index_type_is_unsigned(const RC17Emitter *emitter, RTypeId type) {
    bool is_signed = true;
    uint32_t width = UINT32_C(0);
    RC17IndexRange range;

    return r_c17_index_type_range(emitter, type, &is_signed, &width, &range) && !is_signed &&
           (r_c17_value_kind(emitter, type) != R_SEMANTIC_TYPE_BOOL);
}

/* All ones up to the highest set bit: the bound of an OR or XOR of values up to `value`. */
static uint64_t r_c17_index_bit_mask(uint64_t value) {
    value |= value >> 1U;
    value |= value >> 2U;
    value |= value >> 4U;
    value |= value >> 8U;
    value |= value >> 16U;
    value |= value >> 32U;
    return value;
}

/* The range of `left operation right` in a type whose non-negative values are `type_range`.
   Unsigned arithmetic wraps, so a result that may wrap is only known to lie in its type;
   signed arithmetic panics on overflow and a signed result is known only without one. */
static bool r_c17_index_combine(RTokenKind operation,
                                bool left_known,
                                RC17IndexRange left,
                                bool right_known,
                                RC17IndexRange right,
                                bool is_signed,
                                uint32_t width,
                                RC17IndexRange type_range,
                                RC17IndexRange *range) {
    *range = type_range;
    if (operation == R_TOKEN_AMP) {
        /* A non-negative operand bounds an AND, whatever the other operand is. */
        if (!left_known && !right_known) {
            return !is_signed;
        }
        range->low = UINT64_C(0);
        range->high = !left_known    ? right.high
                      : !right_known ? left.high
                                     : (left.high < right.high ? left.high : right.high);
        return true;
    }
    if (!left_known || !right_known) {
        return !is_signed;
    }
    switch (operation) {
    case R_TOKEN_PLUS:
        if ((left.high <= type_range.high) && (right.high <= type_range.high - left.high)) {
            range->low = left.low + right.low;
            range->high = left.high + right.high;
            return true;
        }
        break;
    case R_TOKEN_MINUS:
        if (left.low >= right.high) {
            range->low = left.low - right.high;
            range->high = left.high - right.low;
            return true;
        }
        break;
    case R_TOKEN_STAR:
        if ((left.high == UINT64_C(0)) || (right.high <= type_range.high / left.high)) {
            range->low = left.low * right.low;
            range->high = left.high * right.high;
            return true;
        }
        break;
    case R_TOKEN_SLASH:
        /* A zero divisor panics; every result comes from a divisor of at least one. */
        if (right.high != UINT64_C(0)) {
            range->low = left.low / right.high;
            range->high = left.high / (right.low == UINT64_C(0) ? UINT64_C(1) : right.low);
            return true;
        }
        break;
    case R_TOKEN_PERCENT:
        if (right.high != UINT64_C(0)) {
            if (left.high < right.low) {
                *range = left;
            } else {
                range->low = UINT64_C(0);
                range->high =
                    left.high < right.high - UINT64_C(1) ? left.high : right.high - UINT64_C(1);
            }
            return true;
        }
        break;
    case R_TOKEN_PIPE:
    case R_TOKEN_CARET:
        range->low = UINT64_C(0);
        range->high = r_c17_index_bit_mask(left.high > right.high ? left.high : right.high);
        return true;
    case R_TOKEN_GREATER_GREATER:
        /* A count at or past the width panics, so the counts that produce a value are below it. */
        if (right.low < (uint64_t)width) {
            const uint64_t largest =
                right.high < (uint64_t)width ? right.high : (uint64_t)width - UINT64_C(1);
            range->low = left.low >> largest;
            range->high = left.high >> right.low;
            return true;
        }
        break;
    case R_TOKEN_LESS_LESS:
        if ((right.high < (uint64_t)width) && (left.high <= (type_range.high >> right.high))) {
            range->low = left.low << right.low;
            range->high = left.high << right.high;
            return true;
        }
        break;
    default:
        break;
    }
    *range = type_range;
    return !is_signed;
}

static bool r_c17_index_symbol_is(const RC17Emitter *emitter, RSymbolId symbol) {
    const RSemanticSymbol *entry = r_c17_symbol(emitter, symbol);

    return (entry != NULL) &&
           ((entry->kind == R_SEMANTIC_SYMBOL_LOCAL) ||
            (entry->kind == R_SEMANTIC_SYMBOL_PARAMETER)) &&
           (emitter->index_proof_generation != UINT32_C(0)) && (emitter->index_unstable != NULL) &&
           (emitter->index_unstable[symbol] != emitter->index_proof_generation);
}

static uint64_t r_c17_index_symbol_maximum(const RC17Emitter *emitter, RSymbolId symbol) {
    const RSemanticSymbol *entry = r_c17_symbol(emitter, symbol);
    bool is_signed = true;
    uint32_t width = UINT32_C(0);
    RC17IndexRange range;

    return (entry != NULL) &&
                   r_c17_index_type_range(emitter, entry->type, &is_signed, &width, &range)
               ? range.high
               : UINT64_C(0);
}

/* A local or parameter unsigned integer whose changes the walk sees. */
static bool r_c17_index_subject(const RC17Emitter *emitter, RSymbolId symbol) {
    const RSemanticSymbol *entry = r_c17_symbol(emitter, symbol);

    return r_c17_index_symbol_is(emitter, symbol) &&
           r_c17_index_type_is_unsigned(emitter, entry->type);
}

/* A local or parameter slice or array<T> whose length the walk sees change. */
static bool r_c17_index_base(const RC17Emitter *emitter, RSymbolId symbol) {
    const RSemanticSymbol *entry = r_c17_symbol(emitter, symbol);
    RSemanticTypeKind kind;

    if (!r_c17_index_symbol_is(emitter, symbol)) {
        return false;
    }
    kind = r_c17_value_kind(emitter, entry->type);
    return (kind == R_SEMANTIC_TYPE_SLICE) || (kind == R_SEMANTIC_TYPE_ARRAY);
}

/* The subject whose value an expression has, through casts that keep every value. */
static RSymbolId r_c17_index_peel(const RC17Emitter *emitter, RHirNodeId node_id) {
    const RHirNode *node = r_c17_node(emitter, node_id);
    uint32_t steps;

    for (steps = UINT32_C(0); (node != NULL) && (steps < R_C17_INDEX_RANGE_DEPTH_LIMIT); ++steps) {
        if (node->kind == R_HIR_CAST) {
            const RHirNode *child = r_c17_node(emitter, r_c17_child(emitter, node, 0U));
            bool source_signed = true;
            bool target_signed = true;
            uint32_t source_width = UINT32_C(0);
            uint32_t target_width = UINT32_C(0);
            RC17IndexRange source_range;
            RC17IndexRange target_range;

            if ((child == NULL) ||
                !r_c17_index_type_range(
                    emitter, child->type, &source_signed, &source_width, &source_range) ||
                !r_c17_index_type_range(
                    emitter, node->type, &target_signed, &target_width, &target_range) ||
                source_signed || (source_range.high > target_range.high)) {
                return R_SYMBOL_ID_INVALID;
            }
            node = child;
            continue;
        }
        if (node->kind == R_HIR_LOAD) {
            const RHirNode *place = r_c17_node(emitter, r_c17_child(emitter, node, 0U));
            return (place != NULL) && (place->kind == R_HIR_PLACE) &&
                           r_c17_index_subject(emitter, place->symbol)
                       ? place->symbol
                       : R_SYMBOL_ID_INVALID;
        }
        return R_SYMBOL_ID_INVALID;
    }
    return R_SYMBOL_ID_INVALID;
}

/* The base of `len(base)`, when the length is that of a local slice or array<T>. */
static RSymbolId r_c17_index_length_base(const RC17Emitter *emitter, RHirNodeId node_id) {
    const RHirNode *node = r_c17_node(emitter, node_id);
    const RHirNode *place;

    if ((node == NULL) || (node->kind != R_HIR_LENGTH)) {
        return R_SYMBOL_ID_INVALID;
    }
    place = r_c17_node(emitter, r_c17_child(emitter, node, 0U));
    return (place != NULL) && (place->kind == R_HIR_PLACE) &&
                   r_c17_index_base(emitter, place->symbol)
               ? place->symbol
               : R_SYMBOL_ID_INVALID;
}

static bool r_c17_index_fact_find(const RC17Emitter *emitter,
                                  RC17IndexFactKind kind,
                                  RSymbolId subject,
                                  RSymbolId base,
                                  RSymbolId *found_base) {
    size_t index;

    for (index = emitter->index_fact_count; index != 0U; --index) {
        const RC17IndexFact *fact = &emitter->index_facts[index - 1U];
        if (fact->live && (fact->kind == kind) && (fact->subject == subject) &&
            ((base == R_SYMBOL_ID_INVALID) || (fact->base == base))) {
            if (found_base != NULL) {
                *found_base = fact->base;
            }
            return true;
        }
    }
    return false;
}

static bool r_c17_index_fact_push(RC17Emitter *emitter,
                                  RC17IndexFactKind kind,
                                  RSymbolId subject,
                                  RSymbolId base,
                                  uint64_t low,
                                  uint64_t high) {
    RC17IndexFact *fact;

    if (emitter->index_fact_count == emitter->index_fact_capacity) {
        const size_t capacity =
            emitter->index_fact_capacity == 0U ? 32U : emitter->index_fact_capacity * 2U;
        RC17IndexFact *replacement;

        if (capacity > SIZE_MAX / sizeof(*replacement)) {
            return r_c17_fail(emitter, R_FRONTEND_LIMIT_EXCEEDED);
        }
        replacement = r_c17_allocate(emitter, capacity * sizeof(*replacement));
        if (replacement == NULL) {
            return false;
        }
        if (emitter->index_fact_count != 0U) {
            (void)memcpy(replacement,
                         emitter->index_facts,
                         emitter->index_fact_count * sizeof(*replacement));
        }
        r_c17_free(emitter, emitter->index_facts);
        emitter->index_facts = replacement;
        emitter->index_fact_capacity = capacity;
    }
    fact = &emitter->index_facts[emitter->index_fact_count];
    fact->kind = kind;
    fact->subject = subject;
    fact->base = base;
    fact->low = low;
    fact->high = high;
    fact->live = true;
    ++emitter->index_fact_count;
    return true;
}

/* Every fact about an object the walk is about to see change dies. The log of deaths lets an
   if statement keep the deaths of one branch out of the other, and out of the rest of the
   block when that branch always leaves it. */
static void r_c17_index_kill(RC17Emitter *emitter, RSymbolId symbol) {
    size_t index;

    for (index = 0U; index < emitter->index_fact_count; ++index) {
        RC17IndexFact *fact = &emitter->index_facts[index];
        if (fact->live && ((fact->subject == symbol) || (fact->base == symbol))) {
            fact->live = false;
            if (emitter->index_kill_count == emitter->index_kill_capacity) {
                const size_t capacity =
                    emitter->index_kill_capacity == 0U ? 64U : emitter->index_kill_capacity * 2U;
                size_t *replacement;

                if (capacity > SIZE_MAX / sizeof(*replacement)) {
                    emitter->index_proof_abandoned = true;
                    return;
                }
                replacement = r_c17_allocate(emitter, capacity * sizeof(*replacement));
                if (replacement == NULL) {
                    emitter->index_proof_abandoned = true;
                    return;
                }
                if (emitter->index_kill_count != 0U) {
                    (void)memcpy(replacement,
                                 emitter->index_kill_log,
                                 emitter->index_kill_count * sizeof(*replacement));
                }
                r_c17_free(emitter, emitter->index_kill_log);
                emitter->index_kill_log = replacement;
                emitter->index_kill_capacity = capacity;
            }
            emitter->index_kill_log[emitter->index_kill_count] = index;
            ++emitter->index_kill_count;
        }
    }
}

/* Sets the facts below `mark` that died between two points of the log back to `live`. When
   `forget` is set, the entries leave the log: the path that killed them never reaches the
   rest of the block, so no enclosing if statement may kill them again. */
static void r_c17_index_set_deaths(
    RC17Emitter *emitter, size_t first, size_t end, size_t mark, bool live, bool forget) {
    size_t entry;

    for (entry = first; entry < end; ++entry) {
        const size_t index = emitter->index_kill_log[entry];
        if (index < mark) {
            emitter->index_facts[index].live = live;
            if (forget) {
                emitter->index_kill_log[entry] = SIZE_MAX;
            }
        }
    }
}

/* The range of an expression at the current point of the walk; false when the value may be
   negative or is not an integer. */
static bool r_c17_index_range(const RC17Emitter *emitter,
                              RHirNodeId node_id,
                              uint32_t depth,
                              RC17IndexRange *range) {
    const RHirNode *node = r_c17_node(emitter, node_id);
    RC17IndexRange type_range;
    bool is_signed = true;
    uint32_t width = UINT32_C(0);

    if ((node == NULL) ||
        !r_c17_index_type_range(emitter, node->type, &is_signed, &width, &type_range)) {
        return false;
    }
    *range = type_range;
    if (depth >= R_C17_INDEX_RANGE_DEPTH_LIMIT) {
        return !is_signed;
    }
    switch (node->kind) {
    case R_HIR_LITERAL:
        /* A negative signed literal is stored in two's complement, past the non-negative half. */
        if ((node->operation != R_TOKEN_KW_NULL) && (node->integer_value <= type_range.high)) {
            range->low = node->integer_value;
            range->high = node->integer_value;
            return true;
        }
        break;
    case R_HIR_LOAD: {
        const RHirNode *place = r_c17_node(emitter, r_c17_child(emitter, node, 0U));
        size_t index;

        if ((place == NULL) || (place->kind != R_HIR_PLACE) ||
            !r_c17_index_subject(emitter, place->symbol)) {
            break;
        }
        for (index = 0U; index < emitter->index_fact_count; ++index) {
            const RC17IndexFact *fact = &emitter->index_facts[index];
            if (fact->live && (fact->kind == R_C17_INDEX_FACT_RANGE) &&
                (fact->subject == place->symbol)) {
                range->low = fact->low > range->low ? fact->low : range->low;
                range->high = fact->high < range->high ? fact->high : range->high;
            }
        }
        return true;
    }
    case R_HIR_CAST: {
        RC17IndexRange child;
        if (r_c17_index_range(emitter, r_c17_child(emitter, node, 0U), depth + 1U, &child) &&
            (child.high <= type_range.high)) {
            *range = child;
            return true;
        }
        break;
    }
    case R_HIR_LENGTH: {
        const RHirNode *place = r_c17_node(emitter, r_c17_child(emitter, node, 0U));
        const RSemanticType *base_type =
            place == NULL ? NULL : r_c17_type(emitter, r_c17_value_type(emitter, place->type));
        if ((base_type != NULL) && (base_type->kind == R_SEMANTIC_TYPE_FIXED_ARRAY) &&
            (base_type->length <= type_range.high)) {
            range->low = base_type->length;
            range->high = base_type->length;
            return true;
        }
        break;
    }
    case R_HIR_BINARY: {
        RC17IndexRange left;
        RC17IndexRange right;
        const bool left_known =
            r_c17_index_range(emitter, r_c17_child(emitter, node, 0U), depth + 1U, &left);
        const bool right_known =
            r_c17_index_range(emitter, r_c17_child(emitter, node, 1U), depth + 1U, &right);
        return r_c17_index_combine(node->operation,
                                   left_known,
                                   left,
                                   right_known,
                                   right,
                                   is_signed,
                                   width,
                                   type_range,
                                   range);
    }
    case R_HIR_CONDITIONAL: {
        RC17IndexRange first;
        RC17IndexRange second;
        if (r_c17_index_range(emitter, r_c17_child(emitter, node, 1U), depth + 1U, &first) &&
            r_c17_index_range(emitter, r_c17_child(emitter, node, 2U), depth + 1U, &second)) {
            range->low = first.low < second.low ? first.low : second.low;
            range->high = first.high > second.high ? first.high : second.high;
            return true;
        }
        break;
    }
    default:
        break;
    }
    *range = type_range;
    return !is_signed;
}

/* Marks the change of an object by the part of the body being walked: its facts die, and the
   serial tells a condition walked later inside that part which objects it may not trust. */
static void r_c17_index_stamp(RC17Emitter *emitter, RSymbolId symbol) {
    if ((symbol == R_SYMBOL_ID_INVALID) ||
        ((size_t)symbol > emitter->frontend->semantic_symbol_count)) {
        return;
    }
    emitter->index_stamp[symbol] = emitter->index_proof_serial;
    r_c17_index_kill(emitter, symbol);
}

static void r_c17_index_forget_tree(RC17Emitter *emitter, RHirNodeId node_id, uint32_t depth) {
    const RHirNode *node = r_c17_node(emitter, node_id);
    uint32_t child_index;

    if ((node == NULL) || (node->kind == R_HIR_FUNCTION)) {
        return;
    }
    if (depth >= R_C17_INDEX_WALK_DEPTH_LIMIT) {
        emitter->index_proof_abandoned = true;
        return;
    }
    if ((node->kind == R_HIR_ASSIGN) || (node->kind == R_HIR_COMPOUND_ASSIGN)) {
        const RHirNode *target = r_c17_node(emitter, r_c17_child(emitter, node, 0U));
        if ((target != NULL) && (target->kind == R_HIR_PLACE)) {
            r_c17_index_stamp(emitter, target->symbol);
        }
    }
    if (node->out_destination != R_SYMBOL_ID_INVALID) {
        r_c17_index_stamp(emitter, node->out_destination);
    }
    for (child_index = 0U; child_index < node->child_count; ++child_index) {
        r_c17_index_forget_tree(emitter, r_c17_child(emitter, node, child_index), depth + 1U);
    }
}

/* Forgets the facts about every object that a part of the body may change and returns the
   serial of that part. */
static uint32_t r_c17_index_forget(RC17Emitter *emitter, RHirNodeId node_id) {
    if (emitter->index_proof_serial == UINT32_MAX) {
        emitter->index_proof_abandoned = true;
        return emitter->index_proof_serial;
    }
    ++emitter->index_proof_serial;
    r_c17_index_forget_tree(emitter, node_id, UINT32_C(0));
    return emitter->index_proof_serial;
}

/* Whether a fact may name an object: not one that the part of the body with serial `guard`
   changes, because the fact would describe its value before that change. */
static bool r_c17_index_trusted(const RC17Emitter *emitter, RSymbolId symbol, uint32_t guard) {
    return emitter->index_stamp[symbol] < guard;
}

/* Records what `small < large` (strict) or `small <= large` says about local objects. */
static bool r_c17_index_relate(
    RC17Emitter *emitter, RHirNodeId small, RHirNodeId large, bool strict, uint32_t guard) {
    const RSymbolId subject = r_c17_index_peel(emitter, small);
    const RSymbolId large_subject = r_c17_index_peel(emitter, large);
    const RSymbolId large_base = r_c17_index_length_base(emitter, large);
    const size_t count = emitter->index_fact_count;
    RC17IndexRange range;
    size_t index;

    if ((subject != R_SYMBOL_ID_INVALID) && r_c17_index_trusted(emitter, subject, guard)) {
        if ((large_base != R_SYMBOL_ID_INVALID) &&
            r_c17_index_trusted(emitter, large_base, guard) &&
            !r_c17_index_fact_push(emitter,
                                   strict ? R_C17_INDEX_FACT_BELOW : R_C17_INDEX_FACT_AT_MOST,
                                   subject,
                                   large_base,
                                   UINT64_C(0),
                                   UINT64_C(0))) {
            return false;
        }
        /* The facts of `large` carry over: a value below or at most it is below or at most the
           lengths that bound it. Facts pushed here are past `count` and are not revisited. */
        for (index = 0U; (large_subject != R_SYMBOL_ID_INVALID) && (index < count); ++index) {
            const RC17IndexFact fact = emitter->index_facts[index];
            RC17IndexFactKind kind;

            if (!fact.live || (fact.subject != large_subject)) {
                continue;
            }
            if (fact.kind == R_C17_INDEX_FACT_BELOW) {
                kind = R_C17_INDEX_FACT_BELOW;
            } else if ((fact.kind == R_C17_INDEX_FACT_LENGTH) ||
                       (fact.kind == R_C17_INDEX_FACT_AT_MOST)) {
                kind = strict ? R_C17_INDEX_FACT_BELOW : R_C17_INDEX_FACT_AT_MOST;
            } else {
                continue;
            }
            if (!r_c17_index_fact_push(
                    emitter, kind, subject, fact.base, UINT64_C(0), UINT64_C(0))) {
                return false;
            }
        }
        if (r_c17_index_range(emitter, large, UINT32_C(0), &range) &&
            (!strict || (range.high != UINT64_C(0))) &&
            ((strict ? range.high - UINT64_C(1) : range.high) <
             r_c17_index_symbol_maximum(emitter, subject)) &&
            !r_c17_index_fact_push(emitter,
                                   R_C17_INDEX_FACT_RANGE,
                                   subject,
                                   R_SYMBOL_ID_INVALID,
                                   UINT64_C(0),
                                   strict ? range.high - UINT64_C(1) : range.high)) {
            return false;
        }
    }
    if (r_c17_index_range(emitter, small, UINT32_C(0), &range) &&
        (!strict || (range.low != UINT64_MAX)) &&
        ((strict ? range.low + UINT64_C(1) : range.low) != UINT64_C(0))) {
        const uint64_t minimum = strict ? range.low + UINT64_C(1) : range.low;

        if ((large_base != R_SYMBOL_ID_INVALID) &&
            r_c17_index_trusted(emitter, large_base, guard) &&
            !r_c17_index_fact_push(
                emitter, R_C17_INDEX_FACT_MINIMUM, R_SYMBOL_ID_INVALID, large_base, minimum, 0U)) {
            return false;
        }
        for (index = 0U; (large_subject != R_SYMBOL_ID_INVALID) && (index < count); ++index) {
            const RC17IndexFact fact = emitter->index_facts[index];

            if (fact.live && (fact.kind == R_C17_INDEX_FACT_LENGTH) &&
                (fact.subject == large_subject) &&
                !r_c17_index_fact_push(emitter,
                                       R_C17_INDEX_FACT_MINIMUM,
                                       R_SYMBOL_ID_INVALID,
                                       fact.base,
                                       minimum,
                                       0U)) {
                return false;
            }
        }
        if ((large_subject != R_SYMBOL_ID_INVALID) &&
            r_c17_index_trusted(emitter, large_subject, guard) &&
            !r_c17_index_fact_push(emitter,
                                   R_C17_INDEX_FACT_RANGE,
                                   large_subject,
                                   R_SYMBOL_ID_INVALID,
                                   minimum,
                                   UINT64_MAX)) {
            return false;
        }
    }
    return true;
}

static bool r_c17_index_operands_are_integers(const RC17Emitter *emitter, const RHirNode *node) {
    const RHirNode *left = r_c17_node(emitter, r_c17_child(emitter, node, 0U));
    const RHirNode *right = r_c17_node(emitter, r_c17_child(emitter, node, 1U));

    /* Only integer comparisons negate exactly: a float comparison with NaN is false both ways. */
    return (left != NULL) && (right != NULL) &&
           r_c17_kind_is_integer(r_c17_value_kind(emitter, left->type)) &&
           r_c17_kind_is_integer(r_c17_value_kind(emitter, right->type));
}

/* Records what a condition with the value `truth` says, for the path that it selects. */
static bool r_c17_index_assume(
    RC17Emitter *emitter, RHirNodeId condition, bool truth, uint32_t guard, uint32_t depth) {
    const RHirNode *node = r_c17_node(emitter, condition);
    RHirNodeId left;
    RHirNodeId right;

    if ((node == NULL) || (depth >= R_C17_INDEX_RANGE_DEPTH_LIMIT)) {
        return true;
    }
    if ((node->kind == R_HIR_UNARY) && (node->operation == R_TOKEN_BANG)) {
        return r_c17_index_assume(
            emitter, r_c17_child(emitter, node, 0U), !truth, guard, depth + 1U);
    }
    if ((node->kind != R_HIR_BINARY) || (node->child_count != 2U)) {
        return true;
    }
    left = r_c17_child(emitter, node, 0U);
    right = r_c17_child(emitter, node, 1U);
    if (((node->operation == R_TOKEN_AMP_AMP) && truth) ||
        ((node->operation == R_TOKEN_PIPE_PIPE) && !truth)) {
        return r_c17_index_assume(emitter, left, truth, guard, depth + 1U) &&
               r_c17_index_assume(emitter, right, truth, guard, depth + 1U);
    }
    if (!r_c17_index_operands_are_integers(emitter, node)) {
        return true;
    }
    switch (node->operation) {
    case R_TOKEN_LESS:
        return truth ? r_c17_index_relate(emitter, left, right, true, guard)
                     : r_c17_index_relate(emitter, right, left, false, guard);
    case R_TOKEN_LESS_EQUAL:
        return truth ? r_c17_index_relate(emitter, left, right, false, guard)
                     : r_c17_index_relate(emitter, right, left, true, guard);
    case R_TOKEN_GREATER:
        return truth ? r_c17_index_relate(emitter, right, left, true, guard)
                     : r_c17_index_relate(emitter, left, right, false, guard);
    case R_TOKEN_GREATER_EQUAL:
        return truth ? r_c17_index_relate(emitter, right, left, false, guard)
                     : r_c17_index_relate(emitter, left, right, true, guard);
    case R_TOKEN_EQUAL_EQUAL:
    case R_TOKEN_BANG_EQUAL:
        if (truth == (node->operation == R_TOKEN_EQUAL_EQUAL)) {
            return r_c17_index_relate(emitter, left, right, false, guard) &&
                   r_c17_index_relate(emitter, right, left, false, guard);
        }
        return true;
    default:
        return true;
    }
}

/* Two operands that read the same value: the same literal, or the same local object, which
   nothing between them can change. */
static bool
r_c17_index_same_value(const RC17Emitter *emitter, RHirNodeId first, RHirNodeId second) {
    const RHirNode *left = r_c17_node(emitter, first);
    const RHirNode *right = r_c17_node(emitter, second);
    const RHirNode *left_place;
    const RHirNode *right_place;

    if ((left == NULL) || (right == NULL) || (left->kind != right->kind)) {
        return false;
    }
    if (left->kind == R_HIR_LITERAL) {
        return (left->operation != R_TOKEN_KW_NULL) && (right->operation != R_TOKEN_KW_NULL) &&
               r_c17_kind_is_integer(r_c17_value_kind(emitter, left->type)) &&
               (left->integer_value == right->integer_value);
    }
    if (left->kind != R_HIR_LOAD) {
        return false;
    }
    left_place = r_c17_node(emitter, r_c17_child(emitter, left, 0U));
    right_place = r_c17_node(emitter, r_c17_child(emitter, right, 0U));
    return (left_place != NULL) && (right_place != NULL) && (left_place->kind == R_HIR_PLACE) &&
           (right_place->kind == R_HIR_PLACE) && (left_place->symbol == right_place->symbol) &&
           r_c17_index_symbol_is(emitter, left_place->symbol);
}

/* A slice `b[a..a + K]` or `b[J..J + K]` exists only after its check passed, and then holds
   exactly K elements. */
static bool r_c17_index_derive_slice(RC17Emitter *emitter, RSymbolId symbol, RHirNodeId value) {
    const RHirNode *node = r_c17_node(emitter, value);
    const RHirNode *lower;
    const RHirNode *upper;
    uint64_t length;

    while ((node != NULL) && (node->kind == R_HIR_CAST)) {
        node = r_c17_node(emitter, r_c17_child(emitter, node, 0U));
    }
    if ((node == NULL) || (node->kind != R_HIR_SLICE) || (node->child_count != 3U) ||
        !r_c17_index_base(emitter, symbol)) {
        return true;
    }
    lower = r_c17_node(emitter, r_c17_child(emitter, node, 1U));
    upper = r_c17_node(emitter, r_c17_child(emitter, node, 2U));
    if ((lower == NULL) || (upper == NULL)) {
        return true;
    }
    if ((lower->kind == R_HIR_LITERAL) && (upper->kind == R_HIR_LITERAL) &&
        r_c17_index_type_is_unsigned(emitter, lower->type) &&
        r_c17_index_type_is_unsigned(emitter, upper->type) &&
        (upper->integer_value >= lower->integer_value)) {
        length = upper->integer_value - lower->integer_value;
    } else {
        const RHirNode *step = r_c17_node(emitter, r_c17_child(emitter, upper, 1U));
        if ((upper->kind != R_HIR_BINARY) || (upper->operation != R_TOKEN_PLUS) || (step == NULL) ||
            (step->kind != R_HIR_LITERAL) || !r_c17_index_type_is_unsigned(emitter, step->type) ||
            !r_c17_index_same_value(
                emitter, r_c17_child(emitter, node, 1U), r_c17_child(emitter, upper, 0U))) {
            return true;
        }
        length = step->integer_value;
    }
    return (length == UINT64_C(0)) ||
           r_c17_index_fact_push(
               emitter, R_C17_INDEX_FACT_MINIMUM, R_SYMBOL_ID_INVALID, symbol, length, 0U);
}

/* Records the facts about `subject` that holding the value of `value` gives. A plain
   assignment passes the serial of its own statement, which changes the subject itself. */
static bool
r_c17_index_derive(RC17Emitter *emitter, RSymbolId subject, RHirNodeId value, uint32_t guard) {
    const RSemanticSymbol *entry = r_c17_symbol(emitter, subject);
    const RSymbolId length_base = r_c17_index_length_base(emitter, value);
    const RSymbolId source = r_c17_index_peel(emitter, value);
    const size_t count = emitter->index_fact_count;
    RC17IndexRange range;
    RC17IndexRange type_range;
    bool is_signed = true;
    uint32_t width = UINT32_C(0);
    size_t index;

    if (!r_c17_index_subject(emitter, subject) ||
        !r_c17_index_type_range(emitter, entry->type, &is_signed, &width, &type_range)) {
        return r_c17_index_derive_slice(emitter, subject, value);
    }
    if (r_c17_index_range(emitter, value, UINT32_C(0), &range) &&
        ((range.low != type_range.low) || (range.high != type_range.high)) &&
        !r_c17_index_fact_push(
            emitter, R_C17_INDEX_FACT_RANGE, subject, R_SYMBOL_ID_INVALID, range.low, range.high)) {
        return false;
    }
    if ((length_base != R_SYMBOL_ID_INVALID) && r_c17_index_trusted(emitter, length_base, guard)) {
        return r_c17_index_fact_push(
            emitter, R_C17_INDEX_FACT_LENGTH, subject, length_base, UINT64_C(0), UINT64_C(0));
    }
    if ((source == R_SYMBOL_ID_INVALID) || (source == subject)) {
        return true;
    }
    for (index = 0U; index < count; ++index) {
        const RC17IndexFact fact = emitter->index_facts[index];

        if (fact.live && (fact.subject == source) &&
            ((fact.kind == R_C17_INDEX_FACT_LENGTH) || (fact.kind == R_C17_INDEX_FACT_BELOW) ||
             (fact.kind == R_C17_INDEX_FACT_AT_MOST)) &&
            !r_c17_index_fact_push(
                emitter, fact.kind, subject, fact.base, UINT64_C(0), UINT64_C(0))) {
            return false;
        }
    }
    return true;
}

/* Whether control never continues after a statement: it returns, throws, jumps or panics. */
static bool r_c17_index_exits(const RC17Emitter *emitter, RHirNodeId node_id, uint32_t depth) {
    const RHirNode *node = r_c17_node(emitter, node_id);

    if ((node == NULL) || (depth >= R_C17_INDEX_RANGE_DEPTH_LIMIT)) {
        return false;
    }
    switch (node->kind) {
    case R_HIR_RETURN:
    case R_HIR_THROW:
    case R_HIR_BREAK:
    case R_HIR_CONTINUE:
        return true;
    case R_HIR_EXPRESSION_STATEMENT: {
        const RHirNode *expression = r_c17_node(emitter, r_c17_child(emitter, node, 0U));
        return (expression != NULL) &&
               (r_c17_value_kind(emitter, expression->type) == R_SEMANTIC_TYPE_NEVER);
    }
    case R_HIR_BLOCK:
        return (node->child_count != 0U) &&
               r_c17_index_exits(
                   emitter, r_c17_child(emitter, node, node->child_count - 1U), depth + 1U);
    case R_HIR_IF:
        return (node->child_count == 3U) &&
               r_c17_index_exits(emitter, r_c17_child(emitter, node, 1U), depth + 1U) &&
               r_c17_index_exits(emitter, r_c17_child(emitter, node, 2U), depth + 1U);
    default:
        return false;
    }
}

static void r_c17_index_restore(RC17Emitter *emitter, size_t mark) {
    emitter->index_fact_count = mark;
}

/* Whether a length bounds a remainder by `divisor`: the divisor is that length, or an object
   below, at most or equal to it. */
static bool
r_c17_index_divisor_bounded(const RC17Emitter *emitter, RHirNodeId divisor, RSymbolId base) {
    const RSymbolId subject = r_c17_index_peel(emitter, divisor);

    return (r_c17_index_length_base(emitter, divisor) == base) ||
           ((subject != R_SYMBOL_ID_INVALID) &&
            (r_c17_index_fact_find(emitter, R_C17_INDEX_FACT_BELOW, subject, base, NULL) ||
             r_c17_index_fact_find(emitter, R_C17_INDEX_FACT_AT_MOST, subject, base, NULL) ||
             r_c17_index_fact_find(emitter, R_C17_INDEX_FACT_LENGTH, subject, base, NULL)));
}

/* Whether an index value lies below len(base): it is a subject below that length, or a value
   no larger than one (an AND with it, a right shift or quotient of it, an unsigned cast of it),
   or a remainder by that length or by an object that does not exceed it. */
static bool r_c17_index_below_length(const RC17Emitter *emitter,
                                     RHirNodeId node_id,
                                     RSymbolId base,
                                     uint32_t depth) {
    const RHirNode *node = r_c17_node(emitter, node_id);
    const RSymbolId subject = r_c17_index_peel(emitter, node_id);
    const RHirNode *child;

    if ((node == NULL) || (depth >= R_C17_INDEX_RANGE_DEPTH_LIMIT)) {
        return false;
    }
    if (subject != R_SYMBOL_ID_INVALID) {
        return r_c17_index_fact_find(emitter, R_C17_INDEX_FACT_BELOW, subject, base, NULL);
    }
    if (!r_c17_index_type_is_unsigned(emitter, node->type)) {
        return false;
    }
    if (node->kind == R_HIR_CAST) {
        /* Widening keeps an unsigned value and truncation only lowers it. */
        child = r_c17_node(emitter, r_c17_child(emitter, node, 0U));
        return (child != NULL) && r_c17_index_type_is_unsigned(emitter, child->type) &&
               r_c17_index_below_length(emitter, r_c17_child(emitter, node, 0U), base, depth + 1U);
    }
    if ((node->kind != R_HIR_BINARY) || (node->child_count != 2U)) {
        return false;
    }
    switch (node->operation) {
    case R_TOKEN_AMP:
        return r_c17_index_below_length(
                   emitter, r_c17_child(emitter, node, 0U), base, depth + 1U) ||
               r_c17_index_below_length(emitter, r_c17_child(emitter, node, 1U), base, depth + 1U);
    case R_TOKEN_GREATER_GREATER:
    case R_TOKEN_SLASH:
        return r_c17_index_below_length(emitter, r_c17_child(emitter, node, 0U), base, depth + 1U);
    case R_TOKEN_PERCENT:
        /* A zero divisor panics, so a remainder is below its divisor. */
        return r_c17_index_divisor_bounded(emitter, r_c17_child(emitter, node, 1U), base) ||
               r_c17_index_below_length(emitter, r_c17_child(emitter, node, 0U), base, depth + 1U);
    default:
        return false;
    }
}

/* Proves an index place against the facts of the current point of the walk. */
static void r_c17_index_prove(RC17Emitter *emitter, RHirNodeId node_id, const RHirNode *node) {
    const RHirNode *base = r_c17_node(emitter, r_c17_child(emitter, node, 0U));
    const RHirNodeId index = r_c17_child(emitter, node, 1U);
    const RSemanticType *base_type =
        base == NULL ? NULL : r_c17_type(emitter, r_c17_value_type(emitter, base->type));
    RC17IndexRange range;
    bool proven = false;

    if (base_type == NULL) {
        return;
    }
    if (base_type->kind == R_SEMANTIC_TYPE_FIXED_ARRAY) {
        proven = r_c17_index_range(emitter, index, UINT32_C(0), &range) &&
                 (range.high < base_type->length);
    } else if (((base_type->kind == R_SEMANTIC_TYPE_SLICE) ||
                (base_type->kind == R_SEMANTIC_TYPE_ARRAY)) &&
               (base->kind == R_HIR_PLACE) && r_c17_index_base(emitter, base->symbol)) {
        size_t fact_index;

        proven = r_c17_index_below_length(emitter, index, base->symbol, UINT32_C(0));
        if (!proven && r_c17_index_range(emitter, index, UINT32_C(0), &range)) {
            for (fact_index = 0U; fact_index < emitter->index_fact_count; ++fact_index) {
                const RC17IndexFact *fact = &emitter->index_facts[fact_index];
                if (fact->live && (fact->kind == R_C17_INDEX_FACT_MINIMUM) &&
                    (fact->base == base->symbol) && (range.high < fact->low)) {
                    proven = true;
                    break;
                }
            }
        }
    }
    if (proven) {
        emitter->index_proven[node_id] = emitter->index_proof_generation;
    }
}

/* Whether an explicit integer conversion keeps its operand's value: the operand's range lies
   in the non-negative values of the target, so the conversion check cannot fail. */
static bool r_c17_index_cast_fits(const RC17Emitter *emitter, const RHirNode *node) {
    RC17IndexRange operand;
    RC17IndexRange target;
    bool is_signed = true;
    uint32_t width = UINT32_C(0);

    return (node->kind == R_HIR_CAST) && (node->operation == R_TOKEN_KW_AS) &&
           (r_c17_value_kind(emitter, node->type) != R_SEMANTIC_TYPE_BOOL) &&
           r_c17_index_type_range(emitter, node->type, &is_signed, &width, &target) &&
           r_c17_index_range(emitter, r_c17_child(emitter, node, 0U), UINT32_C(0), &operand) &&
           (operand.high <= target.high);
}

static bool r_c17_index_is_statement(RHirKind kind) {
    switch (kind) {
    case R_HIR_BLOCK:
    case R_HIR_LOCAL:
    case R_HIR_RETURN:
    case R_HIR_EXPRESSION_STATEMENT:
    case R_HIR_IF:
    case R_HIR_WHILE:
    case R_HIR_FOR:
    case R_HIR_BREAK:
    case R_HIR_CONTINUE:
    case R_HIR_TRY:
    case R_HIR_CATCH:
    case R_HIR_FINALLY:
    case R_HIR_THROW:
    case R_HIR_SWITCH:
    case R_HIR_CASE:
    case R_HIR_STATIC_IF:
    case R_HIR_PACK_PARTITION:
        return true;
    default:
        return false;
    }
}

static bool r_c17_index_walk_expression(RC17Emitter *emitter,
                                        RHirNodeId node_id,
                                        uint32_t guard,
                                        uint32_t depth) {
    const RHirNode *node = r_c17_node(emitter, node_id);
    const size_t mark = emitter->index_fact_count;
    uint32_t child_index;

    if ((node == NULL) || (node->kind == R_HIR_FUNCTION)) {
        return true;
    }
    if (depth >= R_C17_INDEX_WALK_DEPTH_LIMIT) {
        emitter->index_proof_abandoned = true;
        return false;
    }
    if (r_c17_index_is_statement(node->kind)) {
        const bool walked = r_c17_index_walk_statement(emitter, node_id, depth + 1U);
        r_c17_index_restore(emitter, mark);
        return walked;
    }
    if (node->kind == R_HIR_INDEX_PLACE) {
        r_c17_index_prove(emitter, node_id, node);
    }
    if (r_c17_index_cast_fits(emitter, node)) {
        emitter->index_proven[node_id] = emitter->index_proof_generation;
    }
    if ((node->kind == R_HIR_BINARY) && (node->child_count == 2U) &&
        ((node->operation == R_TOKEN_AMP_AMP) || (node->operation == R_TOKEN_PIPE_PIPE))) {
        const RHirNodeId left = r_c17_child(emitter, node, 0U);
        bool walked =
            r_c17_index_walk_expression(emitter, left, guard, depth + 1U) &&
            r_c17_index_assume(
                emitter, left, node->operation == R_TOKEN_AMP_AMP, guard, UINT32_C(0)) &&
            r_c17_index_walk_expression(emitter, r_c17_child(emitter, node, 1U), guard, depth + 1U);
        r_c17_index_restore(emitter, mark);
        return walked;
    }
    if ((node->kind == R_HIR_CONDITIONAL) && (node->child_count == 3U)) {
        const RHirNodeId condition = r_c17_child(emitter, node, 0U);
        bool walked =
            r_c17_index_walk_expression(emitter, condition, guard, depth + 1U) &&
            r_c17_index_assume(emitter, condition, true, guard, UINT32_C(0)) &&
            r_c17_index_walk_expression(emitter, r_c17_child(emitter, node, 1U), guard, depth + 1U);
        r_c17_index_restore(emitter, mark);
        walked =
            walked && r_c17_index_assume(emitter, condition, false, guard, UINT32_C(0)) &&
            r_c17_index_walk_expression(emitter, r_c17_child(emitter, node, 2U), guard, depth + 1U);
        r_c17_index_restore(emitter, mark);
        return walked;
    }
    for (child_index = 0U; child_index < node->child_count; ++child_index) {
        const RHirNodeId child = r_c17_child(emitter, node, child_index);
        const bool walked =
            (node->kind == R_HIR_VALUE_SCOPE) && ((uint64_t)child_index != node->integer_value)
                ? r_c17_index_walk_statement(emitter, child, depth + 1U)
                : r_c17_index_walk_expression(emitter, child, guard, depth + 1U);
        if (!walked) {
            r_c17_index_restore(emitter, mark);
            return false;
        }
    }
    r_c17_index_restore(emitter, mark);
    return true;
}

/* Walks the parts of a statement that forgot everything it changes, each part on its own. */
static bool
r_c17_index_walk_parts(RC17Emitter *emitter, const RHirNode *node, uint32_t guard, uint32_t depth) {
    const size_t mark = emitter->index_fact_count;
    uint32_t child_index;

    for (child_index = 0U; child_index < node->child_count; ++child_index) {
        const bool walked = r_c17_index_walk_expression(
            emitter, r_c17_child(emitter, node, child_index), guard, depth + 1U);
        r_c17_index_restore(emitter, mark);
        if (!walked) {
            return false;
        }
    }
    return true;
}

static bool r_c17_index_walk_if(RC17Emitter *emitter, const RHirNode *node, uint32_t depth) {
    const RHirNodeId condition = r_c17_child(emitter, node, 0U);
    const RHirNodeId then_branch = r_c17_child(emitter, node, 1U);
    const RHirNodeId else_branch = r_c17_child(emitter, node, 2U);
    const bool then_exits = r_c17_index_exits(emitter, then_branch, UINT32_C(0));
    const bool else_exits = (else_branch != R_HIR_NODE_ID_INVALID) &&
                            r_c17_index_exits(emitter, else_branch, UINT32_C(0));
    const uint32_t guard = r_c17_index_forget(emitter, condition);
    const size_t mark = emitter->index_fact_count;
    size_t then_start;
    size_t then_end;
    size_t else_end;
    bool walked = r_c17_index_walk_expression(emitter, condition, guard, depth + 1U);

    then_start = emitter->index_kill_count;
    walked = walked && r_c17_index_assume(emitter, condition, true, guard, UINT32_C(0)) &&
             r_c17_index_walk_statement(emitter, then_branch, depth + 1U);
    r_c17_index_restore(emitter, mark);
    then_end = emitter->index_kill_count;
    /* The else branch starts from the facts before the then branch changed anything. */
    r_c17_index_set_deaths(emitter, then_start, then_end, mark, true, false);
    if (walked && (else_branch != R_HIR_NODE_ID_INVALID)) {
        walked = r_c17_index_assume(emitter, condition, false, guard, UINT32_C(0)) &&
                 r_c17_index_walk_statement(emitter, else_branch, depth + 1U);
        r_c17_index_restore(emitter, mark);
    }
    else_end = emitter->index_kill_count;
    if (!walked || emitter->index_proof_abandoned) {
        return false;
    }
    /* The rest of the block follows the branches that do not always leave it: their deaths
       hold there, the deaths of a leaving branch do not. */
    if (else_exits && !then_exits) {
        r_c17_index_set_deaths(emitter, then_end, else_end, mark, true, true);
    }
    if (then_exits) {
        r_c17_index_set_deaths(emitter, then_start, then_end, mark, true, true);
    } else {
        r_c17_index_set_deaths(emitter, then_start, then_end, mark, false, false);
    }
    /* Only the path that skips a leaving branch reaches the rest of the block. The guard also
       rejects every object that branch changes, although that path never runs it. */
    if (then_exits) {
        return (else_branch != R_HIR_NODE_ID_INVALID) ||
               r_c17_index_assume(emitter, condition, false, guard, UINT32_C(0));
    }
    if (else_exits) {
        return r_c17_index_assume(emitter, condition, true, guard, UINT32_C(0));
    }
    return true;
}

static bool r_c17_index_walk_loop(RC17Emitter *emitter,
                                  RHirNodeId node_id,
                                  const RHirNode *node,
                                  uint32_t depth) {
    const RHirNodeId condition = r_c17_child(emitter, node, 0U);
    const size_t mark = emitter->index_fact_count;
    uint32_t guard;
    bool walked;

    /* Every iteration after the first starts after the changes of the whole loop. */
    (void)r_c17_index_forget(emitter, node_id);
    guard = r_c17_index_forget(emitter, condition);
    walked = r_c17_index_walk_expression(emitter, condition, guard, depth + 1U) &&
             r_c17_index_assume(emitter, condition, true, guard, UINT32_C(0)) &&
             r_c17_index_walk_statement(emitter, r_c17_child(emitter, node, 1U), depth + 1U);
    r_c17_index_restore(emitter, mark);
    if (walked && (node->kind == R_HIR_FOR) && (node->child_count > 2U)) {
        walked = r_c17_index_walk_statement(emitter, r_c17_child(emitter, node, 2U), depth + 1U);
        r_c17_index_restore(emitter, mark);
    }
    return walked;
}

static bool r_c17_index_walk_statement(RC17Emitter *emitter, RHirNodeId node_id, uint32_t depth) {
    const RHirNode *node = r_c17_node(emitter, node_id);
    uint32_t guard;

    if ((node == NULL) || (node->kind == R_HIR_FUNCTION)) {
        return true;
    }
    if ((depth >= R_C17_INDEX_WALK_DEPTH_LIMIT) || emitter->index_proof_abandoned) {
        emitter->index_proof_abandoned = true;
        return false;
    }
    switch (node->kind) {
    case R_HIR_BLOCK: {
        const size_t mark = emitter->index_fact_count;
        uint32_t child_index;

        for (child_index = 0U; child_index < node->child_count; ++child_index) {
            if (!r_c17_index_walk_statement(
                    emitter, r_c17_child(emitter, node, child_index), depth + 1U)) {
                r_c17_index_restore(emitter, mark);
                return false;
            }
        }
        r_c17_index_restore(emitter, mark);
        return true;
    }
    case R_HIR_LOCAL: {
        const RHirNodeId initializer = r_c17_child(emitter, node, 0U);

        if (initializer == R_HIR_NODE_ID_INVALID) {
            return true;
        }
        guard = r_c17_index_forget(emitter, initializer);
        return r_c17_index_walk_expression(emitter, initializer, guard, depth + 1U) &&
               r_c17_index_derive(emitter, node->symbol, initializer, guard);
    }
    case R_HIR_EXPRESSION_STATEMENT: {
        const RHirNode *expression = r_c17_node(emitter, r_c17_child(emitter, node, 0U));
        const RHirNode *target = ((expression != NULL) && (expression->kind == R_HIR_ASSIGN))
                                     ? r_c17_node(emitter, r_c17_child(emitter, expression, 0U))
                                     : NULL;

        guard = r_c17_index_forget(emitter, node_id);
        if (!r_c17_index_walk_parts(emitter, node, guard, depth)) {
            return false;
        }
        /* The value is computed before the store, from facts that do not name the objects
           this statement changes; the subject itself lost its old facts above. */
        if ((target != NULL) && (target->kind == R_HIR_PLACE)) {
            return r_c17_index_derive(
                emitter, target->symbol, r_c17_child(emitter, expression, 1U), guard);
        }
        return true;
    }
    case R_HIR_IF:
        return r_c17_index_walk_if(emitter, node, depth);
    case R_HIR_WHILE:
    case R_HIR_FOR:
        return r_c17_index_walk_loop(emitter, node_id, node, depth);
    default:
        guard = r_c17_index_forget(emitter, node_id);
        return r_c17_index_walk_parts(emitter, node, guard, depth);
    }
}

static void r_c17_index_unstable(RC17Emitter *emitter, RSymbolId symbol) {
    if ((symbol != R_SYMBOL_ID_INVALID) &&
        ((size_t)symbol <= emitter->frontend->semantic_symbol_count)) {
        emitter->index_unstable[symbol] = emitter->index_proof_generation;
    }
}

/* Whether the walk sees every change that an operand naming a whole object can make: a read,
   a length, a projection, a borrow (judged by its type), a move or drop, or the target of an
   assignment. Any other operand position may change the object unseen. */
static bool r_c17_index_place_use_is_seen(RHirKind parent, uint32_t child_index) {
    switch (parent) {
    case R_HIR_LOAD:
    case R_HIR_LENGTH:
    case R_HIR_FIELD_PLACE:
    case R_HIR_DEREF_PLACE:
    case R_HIR_BORROW:
    case R_HIR_MOVE:
    case R_HIR_DROP:
        return true;
    case R_HIR_INDEX_PLACE:
    case R_HIR_SLICE:
    case R_HIR_ASSIGN:
    case R_HIR_COMPOUND_ASSIGN:
        return child_index == 0U;
    default:
        return false;
    }
}

/* Finds the objects that carry no facts in this body: those borrowed exclusively or by raw
   address, those that receive an output argument, and those named where a change is unseen. */
static void r_c17_index_prescan(RC17Emitter *emitter,
                                RHirNodeId node_id,
                                RHirKind parent,
                                uint32_t child_index,
                                uint32_t depth) {
    const RHirNode *node = r_c17_node(emitter, node_id);
    uint32_t index;

    if ((node == NULL) || (node->kind == R_HIR_FUNCTION)) {
        return;
    }
    if (depth >= R_C17_INDEX_WALK_DEPTH_LIMIT) {
        emitter->index_proof_abandoned = true;
        return;
    }
    if (node->out_destination != R_SYMBOL_ID_INVALID) {
        r_c17_index_unstable(emitter, node->out_destination);
    }
    if (node->kind == R_HIR_BORROW) {
        const RHirNode *place = r_c17_node(emitter, r_c17_child(emitter, node, 0U));
        const RSemanticType *type = r_c17_type(emitter, node->type);
        if ((place != NULL) && (place->kind == R_HIR_PLACE) &&
            ((type == NULL) || (type->kind != R_SEMANTIC_TYPE_BORROW) ||
             ((type->flags & R_SEMANTIC_TYPE_FLAG_SHARED) == 0U))) {
            r_c17_index_unstable(emitter, place->symbol);
        }
    }
    if ((node->kind == R_HIR_PLACE) && !r_c17_index_place_use_is_seen(parent, child_index)) {
        r_c17_index_unstable(emitter, node->symbol);
    }
    for (index = 0U; index < node->child_count; ++index) {
        r_c17_index_prescan(
            emitter, r_c17_child(emitter, node, index), node->kind, index, depth + 1U);
    }
}

/* Proves the index places of the synchronous body `body` before it is emitted. False only
   when emission must fail; a body too deep to walk keeps all of its checks. */
static bool r_c17_index_proofs_analyze(RC17Emitter *emitter, RHirNodeId body) {
    const size_t node_count = emitter->frontend->hir_node_count;
    const size_t symbol_count = emitter->frontend->semantic_symbol_count;

    emitter->index_proof_active = UINT32_C(0);
    if (emitter->index_proven == NULL) {
        if ((node_count >= SIZE_MAX / sizeof(*emitter->index_proven)) ||
            (symbol_count >= SIZE_MAX / sizeof(*emitter->index_unstable))) {
            return r_c17_fail(emitter, R_FRONTEND_LIMIT_EXCEEDED);
        }
        emitter->index_proven =
            r_c17_allocate(emitter, (node_count + 1U) * sizeof(*emitter->index_proven));
        emitter->index_unstable =
            r_c17_allocate(emitter, (symbol_count + 1U) * sizeof(*emitter->index_unstable));
        emitter->index_stamp =
            r_c17_allocate(emitter, (symbol_count + 1U) * sizeof(*emitter->index_stamp));
        if ((emitter->index_proven == NULL) || (emitter->index_unstable == NULL) ||
            (emitter->index_stamp == NULL)) {
            return false;
        }
        (void)memset(emitter->index_proven, 0, (node_count + 1U) * sizeof(*emitter->index_proven));
        (void)memset(
            emitter->index_unstable, 0, (symbol_count + 1U) * sizeof(*emitter->index_unstable));
        (void)memset(emitter->index_stamp, 0, (symbol_count + 1U) * sizeof(*emitter->index_stamp));
    }
    /* Generations and serials only grow; when one would wrap, the remaining bodies keep all
       of their checks. */
    if ((emitter->index_proof_generation >= UINT32_MAX - UINT32_C(1)) ||
        (emitter->index_proof_serial == UINT32_MAX)) {
        return true;
    }
    ++emitter->index_proof_generation;
    emitter->index_proof_abandoned = false;
    emitter->index_fact_count = 0U;
    emitter->index_kill_count = 0U;
    r_c17_index_prescan(emitter, body, R_HIR_INVALID, 0U, UINT32_C(0));
    if (!emitter->index_proof_abandoned) {
        (void)r_c17_index_walk_statement(emitter, body, UINT32_C(0));
    }
    emitter->index_fact_count = 0U;
    if (emitter->status != R_FRONTEND_OK) {
        return false;
    }
    if (emitter->index_proof_abandoned) {
        /* A fresh generation disowns the marks this walk made. */
        ++emitter->index_proof_generation;
        return true;
    }
    emitter->index_proof_active = emitter->index_proof_generation;
    return true;
}

/* Whether the walk of the body being emitted proved this node. */
static bool r_c17_index_node_proven(const RC17Emitter *emitter, const RHirNode *node) {
    const uintptr_t first = (uintptr_t)emitter->frontend->hir_nodes;
    const uintptr_t address = (uintptr_t)node;

    return (emitter->index_proof_active != UINT32_C(0)) && (emitter->index_proven != NULL) &&
           (address >= first) && ((address - first) % sizeof(*node) == 0U) &&
           ((address - first) / sizeof(*node) < emitter->frontend->hir_node_count) &&
           (emitter->index_proven[(address - first) / sizeof(*node) + 1U] ==
            emitter->index_proof_active);
}

/* Whether the bounds check of an index place may be left out: the walk of the body being
   emitted proved it, or its value range alone lies below a fixed-array bound. Facts exist only
   during a walk, so outside one a range uses literals and types alone, which hold anywhere. */
static bool r_c17_index_check_is_redundant(const RC17Emitter *emitter,
                                           const RHirNode *place,
                                           const RSemanticType *base_type) {
    RC17IndexRange range;

    return r_c17_index_node_proven(emitter, place) ||
           ((emitter->index_fact_count == 0U) && (base_type->kind == R_SEMANTIC_TYPE_FIXED_ARRAY) &&
            r_c17_index_range(emitter, r_c17_child(emitter, place, 1U), UINT32_C(0), &range) &&
            (range.high < base_type->length));
}

/* Whether the check of an explicit integer conversion may be left out, by the same rules. */
static bool r_c17_index_cast_is_redundant(const RC17Emitter *emitter, const RHirNode *node) {
    return r_c17_index_node_proven(emitter, node) ||
           ((emitter->index_fact_count == 0U) && r_c17_index_cast_fits(emitter, node));
}

/* The range of a value of an asynchronous body. Values there are defined once, so their
   range needs no walk; loads keep only the range of their type. */
static bool r_c17_index_mir_range(const RC17Emitter *emitter,
                                  const RMirFunction *mir,
                                  RMirValueId value,
                                  uint32_t depth,
                                  RC17IndexRange *range) {
    const RMirInstruction *definition = r_c17_mir_value_definition(emitter, mir, value);
    RC17IndexRange type_range;
    bool is_signed = true;
    uint32_t width = UINT32_C(0);

    if ((definition == NULL) ||
        !r_c17_index_type_range(emitter, definition->type, &is_signed, &width, &type_range)) {
        return false;
    }
    *range = type_range;
    if (depth >= R_C17_INDEX_RANGE_DEPTH_LIMIT) {
        return !is_signed;
    }
    switch (definition->kind) {
    case R_MIR_INSTRUCTION_CONSTANT:
        if ((definition->operation != R_TOKEN_KW_NULL) &&
            (definition->integer_value <= type_range.high)) {
            range->low = definition->integer_value;
            range->high = definition->integer_value;
            return true;
        }
        break;
    case R_MIR_INSTRUCTION_CAST: {
        RC17IndexRange child;
        if ((definition->operand0 != R_MIR_VALUE_ID_INVALID) &&
            r_c17_index_mir_range(emitter, mir, definition->operand0, depth + 1U, &child) &&
            (child.high <= type_range.high)) {
            *range = child;
            return true;
        }
        break;
    }
    case R_MIR_INSTRUCTION_BINARY: {
        RC17IndexRange left;
        RC17IndexRange right;
        const bool left_known =
            r_c17_index_mir_range(emitter, mir, definition->operand0, depth + 1U, &left);
        const bool right_known =
            r_c17_index_mir_range(emitter, mir, definition->operand1, depth + 1U, &right);
        return r_c17_index_combine(definition->operation,
                                   left_known,
                                   left,
                                   right_known,
                                   right,
                                   is_signed,
                                   width,
                                   type_range,
                                   range);
    }
    default:
        break;
    }
    *range = type_range;
    return !is_signed;
}

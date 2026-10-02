/* R-AGG-0011 (L21): families of errors with descendants. A value of such an error's type is a
   value of its family, a synthesized tagged enum whose variants carry the exact members of the
   subtree, the error itself first (r_semantic_declare_error_families). */

/* The family a type designates, or NULL. */
static const RSemanticAggregate *r_error_family(const RFrontendContext *context, RTypeId type) {
    const RSemanticAggregate *aggregate =
        r_semantic_aggregate_for_type(context, r_semantic_value_type(context, type));
    return (aggregate != NULL) && (aggregate->error_family_of != 0U) ? aggregate : NULL;
}

/* The fields of a family are those of its error; they are read through the payload of the first
   variant, the error itself, since the fields of every member start with them. */
static bool r_error_family_base(RBodyContext *body,
                                RExpressionResult *current,
                                RSourceSpan span,
                                const RSemanticAggregate **aggregate) {
    RFrontendContext *context = body->frontend;
    const RSemanticAggregate *family = *aggregate;
    const RSemanticAggregate *root;
    RTypeId root_type;
    RExpressionResult borrow = {0}, payload = {0};
    RTypeId family_borrow, payload_borrow;
    RHirNodeId node_id;
    RHirNode *node;

    if (family->error_family_of == R_SEMANTIC_STANDARD_ERROR_ROOT) {
        /* R-SLIB-ERR-0004: the root of the standard errors has no fields. */
        current->valid = false;
        return r_body_diagnostic(body,
                                 "R-DIAG-TYPE-001",
                                 "R-AGG-0011",
                                 "std.error::fault has no fields; std.error::from_fault gives the "
                                 "portable error it holds",
                                 span);
    }
    root = &context->semantic_aggregates[family->error_family_of - 1U];
    root_type = root->type;
    if (!r_body_standard_borrow_type(body, family->type, true, &family_borrow) ||
        !r_body_form_safe_borrow(body, span, family_borrow, current, &borrow)) {
        return false;
    }
    if (!borrow.valid) {
        *current = borrow;
        return true;
    }
    if (!r_body_standard_borrow_type(body, root_type, true, &payload_borrow)) {
        return false;
    }
    RHirVector child = {&borrow.node, 1U, 1U};
    if (!r_body_append_node(body,
                            R_HIR_VARIANT_PAYLOAD,
                            span,
                            payload_borrow,
                            current->symbol,
                            R_TOKEN_AMP,
                            UINT64_C(0),
                            false,
                            &child,
                            &node_id)) {
        return false;
    }
    node = r_body_hir_node(body, node_id);
    node->borrow_origin = borrow.borrow_origin;
    node->borrow_origin_set = borrow.borrow_origin_set;
    node->borrow_origin_multiple = borrow.borrow_origin_multiple;
    node->borrow_referent = borrow.borrow_referent;
    payload = borrow;
    payload.node = node_id;
    payload.type = payload_borrow;
    if (!r_body_form_deref_place(body, payload, span, current)) {
        return false;
    }
    *aggregate = root;
    return true;
}

/* The variant `tag` of a family built from `value`, an exact member. */
static bool r_error_family_variant(RBodyContext *body,
                                   const RSemanticAggregate *family,
                                   uint32_t tag,
                                   RExpressionResult *value) {
    RHirNodeId node_id;
    RHirNode *node;
    RHirVector children = {&value->node, 1U, 1U};

    if (!r_body_append_node(body,
                            R_HIR_VARIANT,
                            r_body_hir_node(body, value->node)->span,
                            family->type,
                            R_SYMBOL_ID_INVALID,
                            R_TOKEN_INVALID,
                            (uint64_t)tag,
                            false,
                            &children,
                            &node_id)) {
        return false;
    }
    node = r_body_hir_node(body, node_id);
    node->borrow_origin = value->borrow_origin;
    node->borrow_referent = value->borrow_referent;
    node->borrow_origin_set = value->borrow_origin_set;
    node->borrow_origin_multiple = value->borrow_origin_multiple;
    value->node = node_id;
    value->type = family->type;
    value->is_place = false;
    value->is_integer_literal_form = false;
    value->is_unsuffixed_integer_literal = false;
    return true;
}

/* The pattern of one variant of a family, for the helpers of match (R-EXPR-0031). */
static RMatchPattern r_error_family_pattern(const RFrontendContext *context,
                                            const RSemanticAggregate *family,
                                            uint32_t tag,
                                            RSourceSpan span) {
    RMatchPattern pattern;

    (void)memset(&pattern, 0, sizeof(pattern));
    pattern.kind = R_MATCH_VARIANT;
    pattern.span = span;
    pattern.type = family->type;
    pattern.payload_type = context->semantic_variants[family->first_variant + tag].payload_type;
    pattern.tag = tag;
    return pattern;
}

/* Moves the exact error of variant `tag` out of the family `hidden` holds into a new hidden local
   and forgets the partitioned family, as a match arm with a move pattern does; `value` then moves
   the error out of that local. */
static bool r_error_family_take(RBodyContext *body,
                                const RSemanticAggregate *family,
                                RSymbolId hidden,
                                uint32_t tag,
                                size_t scope_base,
                                RSourceSpan span,
                                RHirVector *statements,
                                RExpressionResult *value) {
    RFrontendContext *context = body->frontend;
    const RMatchPattern pattern = r_error_family_pattern(context, family, tag, span);
    RExpressionResult place = {0}, payload = {0}, piece = {0}, local = {0};
    RSymbolId symbol = R_SYMBOL_ID_INVALID;

    r_body_expression_invalid(value);
    if (!r_body_symbol_place(body, hidden, span, &place) ||
        !r_match_payload_place(body, &pattern, place, false, &payload) ||
        !r_match_transfer(body, payload, span, &piece) ||
        !r_iteration_declare_hidden(body,
                                    scope_base,
                                    "$error",
                                    pattern.payload_type,
                                    0U,
                                    span,
                                    &piece,
                                    statements,
                                    &symbol) ||
        !r_match_forget_partition(body, hidden, span, statements) ||
        (symbol == R_SYMBOL_ID_INVALID) || !r_body_symbol_place(body, symbol, span, &local)) {
        return false;
    }
    if (r_semantic_type_is_copy(context, local.type)) {
        *value = local;
        return r_body_place_to_value(body, value);
    }
    return r_body_move_from_place(body, local, span, value);
}

/* The test that the family `hidden` holds variant `tag`. */
static bool r_error_family_test(RBodyContext *body,
                                const RSemanticAggregate *family,
                                RSymbolId hidden,
                                uint32_t tag,
                                size_t scope_base,
                                RSourceSpan span,
                                RExpressionResult *test) {
    const RMatchPattern pattern = r_error_family_pattern(body->frontend, family, tag, span);
    RExpressionResult place = {0};

    return r_body_symbol_place(body, hidden, span, &place) &&
           r_match_test(body, &pattern, place, scope_base, test);
}

/* R-ERR-0002 (L21.2): `throw` of a family value throws the exact error it holds, so a rethrow
   keeps the concrete type: a chain over the variants moves the error out and throws it, and each
   exact error reaches the clause that handles it or leaves the function. */
static bool r_error_family_throw(RBodyContext *body,
                                 RSourceSpan span,
                                 RExpressionResult *value,
                                 RHirNodeId *node_id) {
    RFrontendContext *context = body->frontend;
    const RSemanticAggregate *family = r_error_family(context, value->type);
    const size_t base = body->active_symbols.count;
    RHirVector outer = {0};
    RSemanticObjectSnapshot *entry = NULL;
    size_t entry_count = 0U;
    RSymbolId hidden = R_SYMBOL_ID_INVALID;
    RHirNodeId chain = R_HIR_NODE_ID_INVALID;
    RTypeId void_type;
    bool success = false;

    *node_id = R_HIR_NODE_ID_INVALID;
    if ((family == NULL) || (family->variant_count == 0U) ||
        !r_semantic_type_from_token(context, R_TOKEN_KW_VOID, &void_type)) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    /* One diagnostic for the thrown value rather than one per member (R-ERR-0001, R-ERR-0003). */
    for (uint32_t tag = 0U; tag < family->variant_count; ++tag) {
        const RTypeId member = context->semantic_variants[family->first_variant + tag].payload_type;
        size_t scope = 0U, index = SIZE_MAX;
        if (r_body_effect_catch_scope(body, member, &scope, &index)) {
            continue;
        }
        if (body->finally_depth != UINT32_C(0)) {
            return r_body_diagnostic(body,
                                     "R-DIAG-EFFECT-001",
                                     "R-ERR-0003",
                                     "throw cannot escape a finally clause",
                                     span);
        }
        if (!r_semantic_effect_contains(context, body->throws_type, member)) {
            return r_body_diagnostic(body,
                                     "R-DIAG-EFFECT-001",
                                     "R-ERR-0001",
                                     "thrown error is not declared by the function",
                                     span);
        }
    }
    if (!r_iteration_declare_hidden(
            body, base, "$throw", family->type, 0U, span, value, &outer, &hidden)) {
        goto cleanup;
    }
    if ((hidden == R_SYMBOL_ID_INVALID) || body->poisoned) {
        success = true;
        goto cleanup;
    }
    if (!r_body_capture_active_object_states(body, &entry, &entry_count)) {
        goto cleanup;
    }
    for (uint32_t tag = family->variant_count; tag-- != 0U;) {
        const RTypeId member = context->semantic_variants[family->first_variant + tag].payload_type;
        RHirVector branch = {0};
        RExpressionResult thrown = {0}, test = {0};
        RHirNodeId thrown_node = R_HIR_NODE_ID_INVALID;
        RHirNodeId block = R_HIR_NODE_ID_INVALID;
        const bool last = chain == R_HIR_NODE_ID_INVALID;
        bool built;

        body->active_symbols.count = entry_count;
        built =
            r_body_restore_active_object_states(body, entry, entry_count) &&
            (last || r_error_family_test(body, family, hidden, tag, entry_count, span, &test)) &&
            r_error_family_take(body, family, hidden, tag, entry_count, span, &branch, &thrown) &&
            (!thrown.valid || r_body_throw_value(body, span, &thrown, member, &thrown_node)) &&
            ((thrown_node == R_HIR_NODE_ID_INVALID) ||
             r_hir_vector_push(context, &branch, thrown_node)) &&
            r_body_append_node(body,
                               R_HIR_BLOCK,
                               span,
                               void_type,
                               R_SYMBOL_ID_INVALID,
                               R_TOKEN_INVALID,
                               UINT64_C(0),
                               false,
                               &branch,
                               &block);
        r_context_free(context, branch.items);
        if (!built) {
            goto cleanup;
        }
        if (last) {
            chain = block;
            continue;
        }
        if (!test.valid) {
            success = true;
            goto cleanup;
        }
        {
            RHirNodeId otherwise = chain;
            const RHirNode *previous = r_body_hir_node(body, chain);
            if ((previous != NULL) && (previous->kind != R_HIR_BLOCK)) {
                RHirVector wrapped = {&chain, 1U, 1U};
                if (!r_body_append_node(body,
                                        R_HIR_BLOCK,
                                        span,
                                        void_type,
                                        R_SYMBOL_ID_INVALID,
                                        R_TOKEN_INVALID,
                                        UINT64_C(0),
                                        false,
                                        &wrapped,
                                        &otherwise)) {
                    goto cleanup;
                }
            }
            RHirNodeId children_storage[3] = {test.node, block, otherwise};
            RHirVector children = {children_storage, 3U, 3U};
            if (!r_body_append_node(body,
                                    R_HIR_IF,
                                    span,
                                    void_type,
                                    R_SYMBOL_ID_INVALID,
                                    R_TOKEN_KW_IF,
                                    UINT64_C(0),
                                    false,
                                    &children,
                                    &chain)) {
                goto cleanup;
            }
        }
    }
    body->active_symbols.count = entry_count;
    if (!r_body_restore_active_object_states(body, entry, entry_count) ||
        !r_hir_vector_push(context, &outer, chain) ||
        !r_body_append_node(body,
                            R_HIR_BLOCK,
                            span,
                            void_type,
                            R_SYMBOL_ID_INVALID,
                            R_TOKEN_INVALID,
                            UINT64_C(0),
                            false,
                            &outer,
                            node_id)) {
        goto cleanup;
    }
    success = true;

cleanup:
    body->active_symbols.count = base;
    r_context_free(context, outer.items);
    r_context_free(context, entry);
    return success;
}

/* R-EXPR-0015 (L21.3): a family value of a descendant widens to the family of an ancestor; each
   variant moves its exact error into the variant of the wider family, like a match. */
static bool r_error_family_widen(RBodyContext *body,
                                 const RSemanticAggregate *target,
                                 RExpressionResult *result) {
    RFrontendContext *context = body->frontend;
    const RSemanticAggregate *source = r_error_family(context, result->type);
    const RSourceSpan span = r_body_hir_node(body, result->node)->span;
    const size_t base = body->active_symbols.count;
    RHirVector prefix = {0};
    RSemanticObjectSnapshot *entry = NULL, *merged = NULL, *exit_states = NULL;
    size_t entry_count = 0U, merged_count = 0U, exit_count = 0U;
    RSymbolId hidden = R_SYMBOL_ID_INVALID;
    RExpressionResult chain = {0};
    bool success = false;

    if ((source == NULL) || (source->variant_count == 0U)) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    if (!r_iteration_declare_hidden(
            body, base, "$widen", source->type, 0U, span, result, &prefix, &hidden)) {
        goto cleanup;
    }
    r_body_expression_invalid(result);
    if ((hidden == R_SYMBOL_ID_INVALID) || body->poisoned) {
        success = true;
        goto cleanup;
    }
    if (!r_body_capture_active_object_states(body, &entry, &entry_count)) {
        goto cleanup;
    }
    for (uint32_t tag = source->variant_count; tag-- != 0U;) {
        const RTypeId member = context->semantic_variants[source->first_variant + tag].payload_type;
        const uint32_t target_tag = r_semantic_error_family_tag(context, target->type, member);
        const bool last = !chain.valid;
        RHirVector values = {0};
        RExpressionResult arm = {0}, test = {0};
        bool built;

        body->active_symbols.count = entry_count;
        built =
            (target_tag != UINT32_MAX) &&
            r_body_restore_active_object_states(body, entry, entry_count) &&
            (last || r_error_family_test(body, source, hidden, tag, entry_count, span, &test)) &&
            r_error_family_take(body, source, hidden, tag, entry_count, span, &values, &arm) &&
            (!arm.valid || (r_error_family_variant(body, target, target_tag, &arm) &&
                            r_match_scope_value(body, span, &values, &arm, base)));
        r_context_free(context, values.items);
        if (!built) {
            if (target_tag == UINT32_MAX) {
                context->resource_status = R_FRONTEND_INTERNAL_ERROR;
            }
            goto cleanup;
        }
        if (!arm.valid || (!last && !test.valid)) {
            success = true;
            goto cleanup;
        }
        body->active_symbols.count = base;
        r_context_free(context, exit_states);
        exit_states = NULL;
        if (!r_body_capture_active_object_states(body, &exit_states, &exit_count)) {
            goto cleanup;
        }
        if (merged == NULL) {
            merged = exit_states;
            merged_count = exit_count;
            exit_states = NULL;
        } else {
            if (!r_body_merge_branch_object_states(body, merged, exit_states, merged_count, span)) {
                goto cleanup;
            }
            r_context_free(context, merged);
            merged = NULL;
            if (!r_body_capture_active_object_states(body, &merged, &merged_count)) {
                goto cleanup;
            }
        }
        if (last) {
            chain = arm;
            continue;
        }
        {
            RHirNodeId nodes[3] = {test.node, arm.node, chain.node};
            RHirVector children = {nodes, 3U, 3U};
            if (!r_body_append_node(body,
                                    R_HIR_CONDITIONAL,
                                    span,
                                    target->type,
                                    R_SYMBOL_ID_INVALID,
                                    R_TOKEN_QUESTION,
                                    UINT64_C(0),
                                    false,
                                    &children,
                                    &chain.node)) {
                goto cleanup;
            }
        }
    }
    body->active_symbols.count = base;
    chain.type = target->type;
    chain.symbol = R_SYMBOL_ID_INVALID;
    chain.is_place = false;
    /* The widened value is held by a hidden object too, so an asynchronous frame owns it until
       a call takes it, as it owns any other argument (R-FUNC-0014). */
    if ((merged != NULL) && !r_body_restore_active_object_states(body, merged, merged_count)) {
        goto cleanup;
    }
    {
        RSymbolId wide = R_SYMBOL_ID_INVALID;
        RExpressionResult place = {0};
        if (!r_iteration_declare_hidden(
                body, base, "$wide", target->type, 0U, span, &chain, &prefix, &wide) ||
            (wide == R_SYMBOL_ID_INVALID) || !r_body_symbol_place(body, wide, span, &place)) {
            goto cleanup;
        }
        if (r_semantic_type_is_copy(context, target->type)) {
            chain = place;
            if (!r_body_place_to_value(body, &chain)) {
                goto cleanup;
            }
        } else if (!r_body_move_from_place(body, place, span, &chain)) {
            goto cleanup;
        }
    }
    if (!r_hir_vector_push(context, &prefix, chain.node) ||
        !r_body_append_node(body,
                            R_HIR_VALUE_SCOPE,
                            span,
                            target->type,
                            R_SYMBOL_ID_INVALID,
                            R_TOKEN_INVALID,
                            (uint64_t)(prefix.count - 1U),
                            false,
                            &prefix,
                            &chain.node)) {
        goto cleanup;
    }
    chain.type = target->type;
    chain.symbol = R_SYMBOL_ID_INVALID;
    chain.is_place = false;
    *result = chain;
    success = true;

cleanup:
    body->active_symbols.count = base;
    r_context_free(context, prefix.items);
    r_context_free(context, entry);
    r_context_free(context, merged);
    r_context_free(context, exit_states);
    return success;
}

/* A value of an exact member widens implicitly to the family of an ancestor, and the family of a
   descendant to the family of its ancestor (R-EXPR-0015). */
static bool
r_error_family_upcast(RBodyContext *body, RTypeId contextual_type, RExpressionResult *result) {
    RFrontendContext *context = body->frontend;
    const RSemanticAggregate *family;
    const RSemanticAggregate *source;
    RTypeId value_type;
    uint32_t tag;

    if ((contextual_type == R_TYPE_ID_INVALID) || !result->valid) {
        return true;
    }
    family = r_error_family(context, contextual_type);
    value_type = r_semantic_value_type(context, result->type);
    if ((family == NULL) || (value_type == family->type)) {
        return true;
    }
    source = r_error_family(context, value_type);
    if (source != NULL) {
        if (source->error_family_of == R_SEMANTIC_STANDARD_ERROR_ROOT) {
            return true;
        }
        const RTypeId root = context->semantic_aggregates[source->error_family_of - 1U].type;
        return (r_semantic_error_catch_distance(context, family->type, root) == UINT32_MAX) ||
               r_error_family_widen(body, family, result);
    }
    tag = r_semantic_error_family_tag(context, family->type, value_type);
    if (tag == UINT32_MAX) {
        return true;
    }
    if (!r_body_place_to_value(body, result)) {
        return false;
    }
    return !result->valid || r_error_family_variant(body, family, tag, result);
}

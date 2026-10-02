/* An exchange has no observable uninitialized intermediate state. Its destination loan
 * remains active while the replacement is prepared, including checked-error paths. */
bool r_semantic_type_is_replaceable(const RFrontendContext *context, RTypeId type) {
    const RSemanticTypeKind kind = r_semantic_value_kind(context, type);
    return kind != R_SEMANTIC_TYPE_VOID && kind != R_SEMANTIC_TYPE_NEVER &&
           kind != R_SEMANTIC_TYPE_ATOMIC && !r_semantic_type_is_const(context, type) &&
           (r_semantic_type_is_copy(context, type) ||
            r_semantic_type_is_supported_move(context, type)) &&
           r_generic_has_property(context, type, R_GENERIC_UNBORROWED, 0U);
}

static bool r_body_lower_core_replace(RBodyContext *body,
                                      RAstRef argument_list,
                                      const RAstNodeView *arguments_view,
                                      RSourceSpan span,
                                      RStandardCallOperation operation,
                                      RExpressionResult *result) {
    RFrontendContext *context = body->frontend;
    const bool take = operation == R_STANDARD_CALL_CORE_TAKE;
    const size_t scope_base = body->active_symbols.count;
    RAstRef argument;
    RExpressionResult destination, replacement;
    const RSemanticType *pointer;
    RTypeId value_type;
    RHirVector prefix = {0}, children;
    RHirNodeId operands[2];
    RHirNode *node;
    bool success = false;
    r_body_expression_invalid(result);
    r_body_expression_invalid(&replacement);
    if (arguments_view->child_count != (take ? 1U : 2U)) {
        return r_body_diagnostic(
            body,
            "R-DIAG-TYPE-001",
            "R-OWN-0019",
            take ? "core::take requires one exclusive destination borrow"
                 : "core::replace requires a destination borrow and replacement value",
            span);
    }
    if (!r_ast_ref_child(context, argument_list, 0U, &argument) ||
        !r_body_lower_resource_argument(body,
                                        argument,
                                        false,
                                        "R-OWN-0019",
                                        "replacement requires an initialized mutable place",
                                        &destination))
        goto cleanup;
    if (!destination.valid) {
        success = true;
        goto cleanup;
    }
    pointer = r_semantic_type(context, r_semantic_value_type(context, destination.type));
    if (pointer == NULL || pointer->kind != R_SEMANTIC_TYPE_BORROW ||
        pointer->flags != R_SEMANTIC_TYPE_FLAG_NONE ||
        r_semantic_type_is_const(context, pointer->base)) {
        success = r_body_diagnostic(body,
                                    "R-DIAG-BORROW-001",
                                    "R-OWN-0019",
                                    "replacement requires a non-null exclusive T* borrow",
                                    span);
        goto cleanup;
    }
    value_type = pointer->base;
    if (!r_semantic_type_is_replaceable(context, value_type)) {
        success = r_body_diagnostic(body,
                                    "R-DIAG-TYPE-001",
                                    "R-OWN-0019",
                                    "replacement requires a supported unborrowed value type",
                                    span);
        goto cleanup;
    }
    if (r_semantic_value_kind(context, value_type) == R_SEMANTIC_TYPE_ATOMIC) {
        success = r_body_diagnostic(body,
                                    "R-DIAG-TYPE-001",
                                    "R-OWN-0019",
                                    "atomic storage requires the core atomic operations",
                                    span);
        goto cleanup;
    }
    if (!r_format_stage_operand(body, &destination, &prefix))
        goto cleanup;
    if (take && !r_semantic_type_has_default(context, value_type) &&
        r_semantic_type_has_default_value(context, value_type)) {
        /* R-OWN-0019, R-INIT-0005 (L33): an evaluated default is prepared before the exchange;
           its checked errors leave the destination unchanged. */
        if (!r_body_lower_default_value(body, value_type, span, &replacement))
            goto cleanup;
        if (!replacement.valid) {
            success = true;
            goto cleanup;
        }
    } else if (take) {
        if (!r_semantic_type_has_default(context, value_type)) {
            success = r_body_diagnostic(
                body,
                "R-DIAG-INIT-002",
                "R-OWN-0019",
                "core::take requires a proven R default; use core::replace with an explicit value",
                span);
            goto cleanup;
        }
        if (!r_body_append_node(body,
                                R_HIR_DEFAULT_VALUE,
                                span,
                                value_type,
                                0U,
                                R_TOKEN_INVALID,
                                0U,
                                false,
                                NULL,
                                &replacement.node))
            goto cleanup;
        replacement.type = value_type;
        replacement.valid = true;
    } else {
        if (!r_ast_ref_child(context, argument_list, 1U, &argument) ||
            !r_body_lower_value(body, argument, value_type, &replacement))
            goto cleanup;
        if (!replacement.valid) {
            success = true;
            goto cleanup;
        }
        if (r_semantic_value_type(context, replacement.type) != value_type) {
            success = r_body_diagnostic(body,
                                        "R-DIAG-TYPE-001",
                                        "R-OWN-0019",
                                        "replacement value must match the destination type",
                                        span);
            goto cleanup;
        }
    }
    operands[0] = destination.node;
    operands[1] = replacement.node;
    children = (RHirVector){operands, 2U, 2U};
    if (!r_body_append_node(body,
                            R_HIR_STANDARD_CALL,
                            span,
                            value_type,
                            0U,
                            R_TOKEN_INVALID,
                            0U,
                            false,
                            &children,
                            &result->node))
        goto cleanup;
    node = r_body_hir_node(body, result->node);
    node->standard_operation = operation;
    node->auxiliary_type = value_type;
    result->type = value_type;
    result->valid = true;
    r_format_commit_operands(body, scope_base);
    success = r_format_wrap_operands(body, &prefix, result);
cleanup:
    body->active_symbols.count = scope_base;
    r_context_free(context, prefix.items);
    return success;
}

/* R-OWN-0019 (L26): core::swap(T* first, T* second) exchanges two initialized places of one
 * replaceable type. Both destination loans stay active until the exchange, so an overlap is the
 * ordinary aliasing diagnostic of a call; nothing is dropped, allocated or run in user code. */
static bool r_body_lower_core_swap(RBodyContext *body,
                                   RAstRef argument_list,
                                   const RAstNodeView *arguments_view,
                                   RSourceSpan span,
                                   RExpressionResult *result) {
    RFrontendContext *context = body->frontend;
    const size_t scope_base = body->active_symbols.count;
    RAstRef argument;
    RExpressionResult first, second;
    RTypeId value_type = R_TYPE_ID_INVALID, void_type;
    RHirVector prefix = {0}, children;
    RHirNodeId operands[2];
    bool success = false;
    r_body_expression_invalid(result);
    r_body_expression_invalid(&second);
    if (arguments_view->child_count != 2U)
        return r_body_diagnostic(body,
                                 "R-DIAG-TYPE-001",
                                 "R-OWN-0019",
                                 "core::swap requires two exclusive borrows of one type",
                                 span);
    for (uint32_t index = 0U; index < 2U; ++index) {
        RExpressionResult *operand = index == 0U ? &first : &second;
        const RSemanticType *pointer;
        if (!r_ast_ref_child(context, argument_list, index, &argument) ||
            !r_body_lower_resource_argument(body,
                                            argument,
                                            false,
                                            "R-OWN-0019",
                                            "an exchange requires an initialized mutable place",
                                            operand))
            goto cleanup;
        if (!operand->valid) {
            success = true;
            goto cleanup;
        }
        pointer = r_semantic_type(context, r_semantic_value_type(context, operand->type));
        if (pointer == NULL || pointer->kind != R_SEMANTIC_TYPE_BORROW ||
            pointer->flags != R_SEMANTIC_TYPE_FLAG_NONE ||
            r_semantic_type_is_const(context, pointer->base)) {
            success = r_body_diagnostic(body,
                                        "R-DIAG-BORROW-001",
                                        "R-OWN-0019",
                                        "an exchange requires non-null exclusive T* borrows",
                                        span);
            goto cleanup;
        }
        if (index == 0U) {
            value_type = pointer->base;
            if (!r_semantic_type_is_replaceable(context, value_type)) {
                success = r_body_diagnostic(
                    body,
                    "R-DIAG-TYPE-001",
                    "R-OWN-0019",
                    "an exchange requires a supported unborrowed value type",
                    span);
                goto cleanup;
            }
            if (!r_format_stage_operand(body, &first, &prefix))
                goto cleanup;
        } else if (pointer->base != value_type) {
            success = r_body_diagnostic(body,
                                        "R-DIAG-TYPE-001",
                                        "R-OWN-0019",
                                        "both places of an exchange shall have the same type",
                                        span);
            goto cleanup;
        }
    }
    if (!r_semantic_type_from_token(context, R_TOKEN_KW_VOID, &void_type))
        goto cleanup;
    operands[0] = first.node;
    operands[1] = second.node;
    children = (RHirVector){operands, 2U, 2U};
    if (!r_body_append_standard_call(
            body, span, void_type, value_type, R_STANDARD_CALL_CORE_SWAP, &children, NULL, result))
        goto cleanup;
    r_format_commit_operands(body, scope_base);
    success = r_format_wrap_operands(body, &prefix, result);
cleanup:
    body->active_symbols.count = scope_base;
    r_context_free(context, prefix.items);
    return success;
}

/* The operand of `&operand` when the argument is an address-of expression. */
static bool r_body_clone_address_operand(RBodyContext *body, RAstRef argument, RAstRef *operand) {
    RAstRef direct = argument;
    RAstNodeView view;
    uint32_t depth = 0U;
    for (;;) {
        RAstRef child;
        if (!r_ast_ref_view(body->frontend, direct, &view))
            return false;
        if ((view.kind != R_SYNTAX_EXPRESSION) && (view.kind != R_SYNTAX_INITIALIZER) &&
            (view.kind != R_SYNTAX_BINARY_EXPRESSION) && (view.kind != R_SYNTAX_POSTFIX_EXPRESSION))
            break;
        if (!r_body_ast_single_node_child(body->frontend, direct, &child) ||
            ++depth > r_frontend_tree_depth_limit(body->frontend))
            return false;
        direct = child;
    }
    RAstTokenView operation;
    return view.kind == R_SYNTAX_UNARY_EXPRESSION &&
           r_body_ast_first_token(body->frontend, direct, &operation) &&
           operation.kind == R_TOKEN_AMP &&
           r_body_ast_single_node_child(body->frontend, direct, operand);
}

/* R-OWN-0020 (L26): core::clone(const T* value) produces an independent T. A Copy value is read
 * through the borrow, so its views keep their provenance. Any other cloneable T is unborrowed and
 * owns what it copies; its copy can fail only with std.alloc::alloc_error. */
static bool r_body_lower_core_clone(RBodyContext *body,
                                    RAstRef argument_list,
                                    const RAstNodeView *arguments_view,
                                    RSourceSpan span,
                                    RTypeId contextual_type,
                                    RExpressionResult *result) {
    RFrontendContext *context = body->frontend;
    RAstRef argument;
    RExpressionResult source;
    const RSemanticType *pointer;
    RTypeId value_type, alloc_error_type, effect_set, carrier;
    RHirNodeId operand;
    RHirVector children;
    r_body_expression_invalid(result);
    if (arguments_view->child_count != 1U)
        return r_body_diagnostic(
            body, "R-DIAG-TYPE-001", "R-OWN-0020", "core::clone requires one borrow", span);
    if (!r_ast_ref_child(context, argument_list, 0U, &argument))
        return false;
    r_body_expression_invalid(&source);
    {
        /* `core::clone(&place)` of a Copy place is a read of the place itself, so the copy's
           views keep the provenance of the place's content rather than of the borrow; any other
           place is borrowed once for the clone. */
        RAstRef owner_ref;
        if (r_body_clone_address_operand(body, argument, &owner_ref)) {
            RExpressionResult owner;
            RTypeId borrow_type;
            r_body_expression_invalid(&owner);
            if (!r_body_lower_expression(body, owner_ref, R_TYPE_ID_INVALID, &owner))
                return false;
            if (!owner.valid)
                return true;
            if (!owner.is_place)
                return r_body_diagnostic(body,
                                         "R-DIAG-TYPE-001",
                                         "R-OWN-0020",
                                         "core::clone requires a borrow of an initialized value",
                                         span);
            if (r_semantic_type_is_copy(context, owner.type) &&
                r_generic_has_property(
                    context, r_semantic_value_type(context, owner.type), R_GENERIC_CLONE, 0U)) {
                *result = owner;
                return r_body_place_to_value(body, result);
            }
            if (!r_semantic_intern_derived_type(context,
                                                R_SEMANTIC_TYPE_BORROW,
                                                r_semantic_value_type(context, owner.type),
                                                R_TYPE_ID_INVALID,
                                                UINT64_C(0),
                                                R_SEMANTIC_TYPE_FLAG_SHARED,
                                                &borrow_type) ||
                !r_body_form_safe_borrow(body, span, borrow_type, &owner, &source)) {
                if (context->resource_status != R_FRONTEND_OK)
                    return false;
                return r_body_diagnostic(body,
                                         "R-DIAG-TYPE-001",
                                         "R-OWN-0020",
                                         "core::clone requires a borrow of an initialized value",
                                         span);
            }
        } else if (!r_body_lower_resource_argument(
                       body,
                       argument,
                       true,
                       "R-OWN-0020",
                       "core::clone requires a borrow of an initialized value",
                       &source)) {
            return false;
        }
    }
    if (!source.valid)
        return true;
    pointer = r_semantic_type(context, r_semantic_value_type(context, source.type));
    if (pointer == NULL || pointer->kind != R_SEMANTIC_TYPE_BORROW ||
        (pointer->flags & R_SEMANTIC_TYPE_FLAG_NULLABLE) != 0U)
        return r_body_diagnostic(body,
                                 "R-DIAG-TYPE-001",
                                 "R-OWN-0020",
                                 "core::clone requires a non-null borrow `const T*` or `T*`",
                                 span);
    value_type = r_semantic_value_type(context, pointer->base);
    {
        const RSemanticType *stripped = r_semantic_type(context, value_type);
        if (stripped != NULL && stripped->kind == R_SEMANTIC_TYPE_CONST)
            value_type = r_semantic_value_type(context, stripped->base);
    }
    if (!r_generic_has_property(context, value_type, R_GENERIC_CLONE, 0U)) {
        const RSemanticAggregate *aggregate = r_semantic_aggregate_for_type(context, value_type);
        return r_body_diagnostic(
            body,
            aggregate != NULL && aggregate->recursive_group != 0U ? "R-DIAG-STACK-001"
                                                                  : "R-DIAG-TYPE-001",
            "R-OWN-0020",
            aggregate != NULL && aggregate->recursive_group != 0U
                ? "a structural clone of a self-nesting type recurses with the data and has no "
                  "static stack bound; declare a clone hook"
                : "the type has no clone contract: a Copy type, a standard owner of cloneable "
                  "values, a shared owner or a type with a `T::clone` hook",
            span);
    }
    if (r_semantic_type_is_copy(context, value_type)) {
        RExpressionResult place;
        r_body_expression_invalid(&place);
        if (!r_body_form_deref_place(body, source, span, &place))
            return false;
        if (!place.valid)
            return true;
        *result = place;
        return r_body_place_to_value(body, result);
    }
    if (!r_generic_has_property(context, value_type, R_GENERIC_UNBORROWED, 0U))
        return r_body_diagnostic(body,
                                 "R-DIAG-TYPE-001",
                                 "R-OWN-0020",
                                 "a clone of a value that is not Copy requires an unborrowed "
                                 "type; a generic parameter declares `clone & unborrowed`",
                                 span);
    const bool checked = r_clone_reports_failure(context, value_type, 0U);
    carrier = value_type;
    effect_set = R_TYPE_ID_INVALID;
    if (checked &&
        (!r_semantic_intern_named_standard_type(
             context, "std.alloc::alloc_error", &alloc_error_type) ||
         !r_semantic_single_effect_set(context, alloc_error_type, &effect_set) ||
         !r_semantic_intern_effect_carrier(context, value_type, effect_set, &carrier) ||
         !r_body_validate_effect_set(body, effect_set, span)))
        return false;
    if (!r_body_standard_context_matches(
            body, contextual_type, value_type, span, "the clone does not match its context"))
        return false;
    operand = source.node;
    children = (RHirVector){&operand, 1U, 1U};
    if (!r_body_append_standard_call(
            body, span, value_type, carrier, R_STANDARD_CALL_CORE_CLONE, &children, NULL, result))
        return false;
    return !checked || r_body_attach_effect_exits(body, result->node, effect_set, span);
}

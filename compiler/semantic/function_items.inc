/* Function items share the empty-closure representation, but every invocation binds to
   the original function. No wrapper body is cloned: static storage, effects and identity
   belong to that original declaration. */
/* The closed item type of the safe R function `target`, created on first use. */
static bool r_function_item_type(RFrontendContext *context, RSymbolId target, RTypeId *result) {
    const RSemanticSymbol function = context->semantic_symbols[target - 1U];
    RTypeId type = R_TYPE_ID_INVALID;
    for (size_t index = 0U; index < context->semantic_aggregate_count; ++index) {
        if (context->semantic_aggregates[index].function_item_target == target) {
            type = context->semantic_aggregates[index].type;
            break;
        }
    }
    if (type == R_TYPE_ID_INVALID) {
        RSemanticAggregate aggregate = {0};
        RTypeVector parameters = {0};
        const RInternEntry function_name = context->intern_entries[function.name_intern_id - 1U];
        const size_t length = sizeof("$function()") - 1U + function_name.length;
        char *label = r_context_allocate(context, length + 1U);
        const uint32_t id = (uint32_t)context->semantic_aggregate_count + 1U;
        if (label == NULL)
            return false;
        (void)snprintf(label, length + 1U, "$function(%s)", function_name.bytes);
        const bool named = r_closure_intern(context, label, &aggregate.name_intern_id);
        r_context_free(context, label);
        if (!named)
            return false;
        for (uint32_t index = 0U; index < function.parameter_count; ++index) {
            if (!r_type_vector_push(
                    context,
                    &parameters,
                    context->semantic_parameter_types[function.first_parameter_type + index])) {
                r_context_free(context, parameters.items);
                return false;
            }
        }
        const bool signature_ok = r_semantic_callable_signature(
            context,
            &parameters,
            function.return_type,
            function.is_async ? R_SEMANTIC_RECEIVER_VALUE : R_SEMANTIC_RECEIVER_SHARED,
            (function.is_noalloc ? R_SEMANTIC_TYPE_FLAG_NOALLOC : 0U) |
                (function.is_nonblocking ? R_SEMANTIC_TYPE_FLAG_NONBLOCKING : 0U),
            function.throws_type,
            function.is_async,
            &aggregate.closure_signature);
        r_context_free(context, parameters.items);
        if (!signature_ok || !r_semantic_intern_type(context, R_SEMANTIC_TYPE_STRUCT, id, &type) ||
            !r_grow_array(context,
                          (void **)&context->semantic_aggregates,
                          &context->semantic_aggregate_capacity,
                          sizeof(aggregate),
                          context->semantic_aggregate_count + 1U))
            return false;
        aggregate.kind = R_SEMANTIC_AGGREGATE_STRUCT;
        aggregate.name_span = function.name_span;
        aggregate.module_source = function.module_source;
        aggregate.declaration_ast = function.declaration_ast;
        aggregate.type = type;
        aggregate.first_field = (uint32_t)context->semantic_field_count;
        aggregate.complete = true;
        aggregate.is_copy = true;
        aggregate.is_default_initializable = true;
        aggregate.is_protected = true;
        aggregate.is_closure = true;
        aggregate.function_item_target = target;
        context->semantic_aggregates[context->semantic_aggregate_count++] = aggregate;
    }
    *result = type;
    return true;
}

/* Whether `target` may be named as a function item (R-FUNC-0007). */
/* Why `target` may not be named as a function item (R-FUNC-0007), or NULL. */
static const char *r_function_item_problem_in(const RFrontendContext *context,
                                             RSymbolId target,
                                             bool template_body) {
    const RSemanticSymbol *function = &context->semantic_symbols[target - 1U];
    const RSemanticSymbol *origin = function->generic_origin == 0U
                                        ? NULL
                                        : &context->semantic_symbols[function->generic_origin - 1U];
    if (function->kind != R_SEMANTIC_SYMBOL_FUNCTION || function->is_unsafe ||
        function->is_extern_c || function->is_variadic_slice)
        return "a function item requires a safe R function without variadic parameters";
    /* A trait method applied to its owner keeps the overload family of its prototype. */
    if ((function->is_overloaded && function->generic_origin == 0U) ||
        (origin != NULL && origin->trait != 0U && origin->is_overloaded))
        return "an overloaded name does not form a function item; name a function that is not "
               "overloaded or call the overload from a lambda";
    /* M32-4: in the body of a generic function, name::<...> closed over its own parameters is
       open only until the body is cloned for an instance, which closes the target. */
    if (r_generic_function_is_open(context, function) &&
        !(template_body && function->generic_origin != 0U))
        return "a generic function forms a function item once closed with name::<...>";
    return NULL;
}

static const char *r_function_item_problem(const RFrontendContext *context, RSymbolId target) {
    return r_function_item_problem_in(context, target, false);
}

static bool r_function_item_allowed(const RFrontendContext *context, RSymbolId target) {
    return r_function_item_problem(context, target) == NULL;
}

static bool r_body_function_item(RBodyContext *body,
                                 RSymbolId target,
                                 RSourceSpan span,
                                 RExpressionResult *result) {
    RFrontendContext *context = body->frontend;
    RTypeId type = R_TYPE_ID_INVALID;
    const RSemanticSymbol *owner = body->function_symbol == R_SYMBOL_ID_INVALID
                                       ? NULL
                                       : &context->semantic_symbols[body->function_symbol - 1U];
    const bool template_body =
        owner != NULL && owner->generic_schema != 0U && owner->generic_origin == 0U;
    const char *problem = r_function_item_problem_in(context, target, template_body);
    if (problem != NULL) {
        return r_body_diagnostic(body, "R-DIAG-TYPE-001", "R-FUNC-0007", problem, span);
    }
    if (!r_function_item_type(context, target, &type))
        return false;
    if (!r_body_append_node(body,
                            R_HIR_AGGREGATE_INIT,
                            span,
                            type,
                            R_SYMBOL_ID_INVALID,
                            R_TOKEN_INVALID,
                            0U,
                            false,
                            NULL,
                            &result->node))
        return false;
    result->type = type;
    result->valid = true;
    return true;
}

/* R-TYPE-0036, R-FUNC-0007: `name::<...>`, `Owner::method` and `Owner<args>::method` in a
   value position. A method item takes its receiver as the first parameter. Without a list,
   `handled` stays false when the qualified name does not denote an associated function. */
static bool r_body_named_function_item(RBodyContext *body,
                                       RAstRef name_ref,
                                       RAstRef list,
                                       bool has_list,
                                       RSourceSpan span,
                                       RExpressionResult *result,
                                       bool *handled) {
    RFrontendContext *context = body->frontend;
    RAstTokenView name = {0};
    RBodyLookupIssue issue = R_BODY_LOOKUP_UNRESOLVED;
    RSymbolId target = R_SYMBOL_ID_INVALID;
    *handled = false;
    if (r_ast_ref_token(context, name_ref, &name) && name.kind == R_TOKEN_IDENTIFIER) {
        target = r_body_resolve_name(body, &name, &issue);
    } else {
        /* M24-6: only a diagnostic of this resolution ends it; an earlier one in the body does
           not hide an enum constant or a method named through its owner. */
        const bool was_poisoned = body->poisoned;
        body->poisoned = false;
        const bool resolved = r_generic_resolve_associated(body, name_ref, &name, &target);
        const bool rejected = body->poisoned;
        body->poisoned = rejected || was_poisoned;
        if (!resolved) {
            return false;
        }
        if (rejected) {
            *handled = true;
            return true;
        }
        if (target == R_SYMBOL_ID_INVALID && has_list) {
            target = r_body_resolve_qualified_name(body, name_ref, &name, &issue);
        }
    }
    if (target == R_SYMBOL_ID_INVALID) {
        body->associated_owner_type = R_TYPE_ID_INVALID;
        if (!has_list) {
            return true;
        }
        *handled = true;
        return r_body_diagnostic(
            body, "R-DIAG-NAME-001", "R-NAME-0003", r_body_lookup_message(issue, false), span);
    }
    *handled = true;
    const bool was_poisoned = body->poisoned;
    body->poisoned = false;
    const bool closed = r_generic_close_explicit(body, list, has_list, &target, false, 0U, span);
    const bool rejected = body->poisoned;
    body->poisoned = rejected || was_poisoned;
    if (!closed) {
        return false;
    }
    if (rejected) {
        return true;
    }
    return r_body_function_item(body, target, span, result);
}

static bool r_body_call_function_item(RBodyContext *body,
                                      RAstRef postfix,
                                      RAstRef primary,
                                      RAstRef suffix,
                                      RAstTokenView name,
                                      RSymbolId target,
                                      RExpressionResult *callee,
                                      RExpressionResult *result) {
    RAstRef arguments;
    RAstNodeView argument_view, primary_view, suffix_view;
    RHirVector prefix = {0};
    RHirNodeId statement;
    const size_t scope_base = body->active_symbols.count;
    bool success = false;
    if (!r_body_place_to_value(body, callee))
        return false;
    if (!callee->valid)
        return true;
    if (!r_ast_find_direct_child(body->frontend, suffix, R_SYNTAX_ARGUMENT_LIST, &arguments) ||
        !r_ast_ref_view(body->frontend, arguments, &argument_view) ||
        !r_ast_ref_view(body->frontend, primary, &primary_view) ||
        !r_ast_ref_view(body->frontend, suffix, &suffix_view) ||
        !r_body_append_discard(body, callee, primary_view.span, primary_view.span, "R-FUNC-0007"))
        goto cleanup;
    RHirNodeId statement_storage[] = {callee->node};
    RHirVector statement_children = {statement_storage, 1U, 1U};
    if (!r_body_append_node(body,
                            R_HIR_EXPRESSION_STATEMENT,
                            primary_view.span,
                            callee->type,
                            R_SYMBOL_ID_INVALID,
                            R_TOKEN_INVALID,
                            0U,
                            false,
                            &statement_children,
                            &statement) ||
        !r_hir_vector_push(body->frontend, &prefix, statement))
        goto cleanup;
    success = r_body_lower_resolved_call(body,
                                         postfix,
                                         primary,
                                         suffix,
                                         arguments,
                                         argument_view,
                                         primary_view,
                                         suffix_view,
                                         name,
                                         target,
                                         scope_base,
                                         R_BODY_LOOKUP_UNRESOLVED,
                                         NULL,
                                         false,
                                         R_TYPE_ID_INVALID,
                                         result) &&
              r_body_wrap_value_scope(body, &prefix, result, NULL);
cleanup:
    r_context_free(body->frontend, prefix.items);
    return success;
}

/* Callable constraints have an environment argument. Once their concrete implementation
   is a function item, retain evaluation of that argument but call the original signature. */
static bool r_function_items_lower_calls(RFrontendContext *context) {
    const size_t count = context->hir_node_count;
    for (size_t index = 0U; index < count; ++index) {
        RHirNode call = context->hir_nodes[index];
        if (!call.function_item_receiver)
            continue;
        if ((call.kind != R_HIR_CALL && call.kind != R_HIR_ASYNC_START) || call.child_count == 0U ||
            call.symbol == R_SYMBOL_ID_INVALID)
            return false;
        const RHirNodeId receiver = context->hir_children[call.first_child];
        call.function_item_receiver = false;
        call.first_child += 1U;
        call.child_count -= 1U;
        RHirNodeId direct;
        if (!r_hir_append_node(context, call, &direct))
            return false;
        RHirNode discarded = {0};
        RHirNodeId discarded_id;
        discarded.kind = R_HIR_DISCARD;
        discarded.span = call.span;
        discarded.operation = R_TOKEN_KW_AS;
        if (!r_semantic_type_from_token(context, R_TOKEN_KW_VOID, &discarded.type) ||
            !r_hir_append_node(context, discarded, &discarded_id))
            return false;
        RHirNodeId receiver_storage[] = {receiver};
        RHirVector receiver_children = {receiver_storage, 1U, 1U};
        if (!r_hir_commit_children(context, discarded_id, &receiver_children))
            return false;
        RHirNode statement = discarded;
        RHirNodeId statement_id;
        statement.kind = R_HIR_EXPRESSION_STATEMENT;
        statement.operation = R_TOKEN_INVALID;
        RHirNodeId statement_storage[] = {discarded_id};
        RHirVector statement_children = {statement_storage, 1U, 1U};
        if (!r_hir_append_node(context, statement, &statement_id) ||
            !r_hir_commit_children(context, statement_id, &statement_children))
            return false;
        RHirNode scope = call;
        scope.kind = R_HIR_VALUE_SCOPE;
        scope.symbol = R_SYMBOL_ID_INVALID;
        scope.task_scope = R_SYMBOL_ID_INVALID;
        scope.auxiliary_type = R_TYPE_ID_INVALID;
        scope.first_effect_exit = 0U;
        scope.effect_exit_count = 0U;
        scope.integer_value = 1U;
        scope.is_place = false;
        scope.is_move = false;
        scope.immediate_await = false;
        context->hir_nodes[index] = scope;
        RHirNodeId storage[] = {statement_id, direct};
        RHirVector children = {storage, 2U, 2U};
        if (!r_hir_commit_children(context, (RHirNodeId)index + 1U, &children))
            return false;
    }
    return true;
}

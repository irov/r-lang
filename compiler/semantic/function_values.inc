/* R-TYPE-0054 (L28): function values. `fn [@noalloc] [@nonblocking] (P...) -> R [throws(E...)]`
 * and `async fn (P...) -> R [throws(E...)]` are Copy, Send and Sync values that designate one
 * safe R function. A function item converts to such a type when its signature matches; the
 * program's conversions to a type are the targets of its calls, and a call binds to the
 * prototype of the signature's callable trait applied to the function type (a dispatcher). */

/* The function type spelled by the callable signature `node` in a type position. */
/* M42-1: the Send and unborrowed requirements of an async function type (R-TYPE-0054); a type
   that depends on a generic parameter is checked in each instance, since an instance of a generic
   struct named in a field is not yet complete when the field is read. */
static bool r_function_value_async_signature(const RFrontendContext *context,
                                             const RTypeVector *parameters,
                                             RTypeId result,
                                             RTypeId effects) {
    if (!r_generic_type_is_dependent(context, result, 0U) &&
        !r_closure_async_value(context, result, true)) {
        return false;
    }
    for (size_t index = 0U; index < parameters->count; ++index) {
        if (!r_generic_type_is_dependent(context, parameters->items[index], 0U) &&
            !r_closure_async_value(context, parameters->items[index], false)) {
            return false;
        }
    }
    for (uint32_t index = 0U; index < r_semantic_effect_count(context, effects); ++index) {
        if (!r_closure_async_value(context, r_semantic_effect_at(context, effects, index), false)) {
            return false;
        }
    }
    return true;
}

static bool r_function_value_type(RFrontendContext *context, RAstRef node, RTypeId *result) {
    RAstNodeView view;
    RTypeVector types = {0};
    RTypeId chain = R_TYPE_ID_INVALID;
    RTypeId effects = R_TYPE_ID_INVALID;
    RTypeId returned;
    uint32_t resource_flags = 0U;
    bool effects_supported = false;
    bool valid = true;
    bool success = false;
    *result = R_TYPE_ID_INVALID;
    if (!r_ast_ref_view(context, node, &view)) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    {
        RAstRef mode;
        if (r_ast_find_direct_child(context, node, R_SYNTAX_CALLABLE_MODE, &mode)) {
            return r_semantic_add_diagnostic(context,
                                             "R-DIAG-TYPE-001",
                                             "R-TYPE-0054",
                                             "a function type has no callable mode",
                                             view.span);
        }
    }
    for (uint32_t child = 0U; child < view.child_count; ++child) {
        RAstRef part;
        RAstNodeView part_view;
        RAstTokenView token;
        RTypeId type;
        if (!r_ast_ref_child(context, node, child, &part)) {
            context->resource_status = R_FRONTEND_INTERNAL_ERROR;
            goto cleanup;
        }
        if (r_ast_ref_token(context, part, &token)) {
            if (token.kind == R_TOKEN_ELLIPSIS) {
                success = r_semantic_add_diagnostic(context,
                                                    "R-DIAG-TYPE-001",
                                                    "R-TYPE-0054",
                                                    "a function type does not expand a pack",
                                                    view.span);
                goto cleanup;
            }
            continue;
        }
        if (!r_ast_ref_view(context, part, &part_view)) {
            context->resource_status = R_FRONTEND_INTERNAL_ERROR;
            goto cleanup;
        }
        if (part_view.kind != R_SYNTAX_TYPE) {
            continue;
        }
        if (!r_semantic_type_from_ast(context, part, &type)) {
            success = context->resource_status == R_FRONTEND_OK;
            goto cleanup;
        }
        if (!r_type_vector_push(context, &types, type)) {
            goto cleanup;
        }
    }
    if (types.count == 0U) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        goto cleanup;
    }
    types.count -= 1U;
    returned = types.items[types.count];
    const bool asynchronous = r_semantic_type_has_direct_token(context, node, R_TOKEN_KW_ASYNC);
    for (size_t index = 0U; index <= types.count; ++index)
        if (!r_semantic_collect_type_on_demand(context, types.items[index]))
            goto cleanup;
    /* M42-1: a type that depends on a generic parameter, such as an instance of a generic
       struct, is checked in each instance; the declaration only fixes its shape. */
    valid = r_generic_type_is_dependent(context, returned, 0U) ||
            r_closure_sync_result(context, returned);
    for (size_t parameter = 0U; valid && parameter < types.count; ++parameter)
        valid = r_generic_type_is_dependent(context, types.items[parameter], 0U) ||
                r_closure_sync_input(context, types.items[parameter]);
    if (!valid) {
        success = r_semantic_add_diagnostic(
            context,
            "R-DIAG-TYPE-001",
            "R-TYPE-0054",
            r_semantic_value_kind(context, returned) == R_SEMANTIC_TYPE_NEVER
                ? "a function type result cannot be never"
                : "a function type requires supported synchronous parameter and result types",
            view.span);
        goto cleanup;
    }
    if (!r_semantic_collect_throws(context, node, asynchronous, &effects, &effects_supported)) {
        goto cleanup;
    }
    if (!effects_supported) {
        success = true;
        goto cleanup;
    }
    for (uint32_t effect = 0U; effect < r_semantic_effect_count(context, effects); ++effect)
        if ((r_generic_parameter_constraints(context, r_semantic_effect_at(context, effects, effect)) &
             R_GENERIC_ERRORS) != 0U) {
            success = r_semantic_add_diagnostic(
                context,
                "R-DIAG-TYPE-001",
                "R-TYPE-0054",
                "a function type names its exact checked errors, not an error set parameter",
                view.span);
            goto cleanup;
        }
    if (asynchronous && !r_function_value_async_signature(context, &types, returned, effects)) {
        success = r_semantic_add_diagnostic(context,
                                            "R-DIAG-ASYNC-001",
                                            "R-TYPE-0054",
                                            "an async function type has Send, unborrowed "
                                            "parameters, result and errors",
                                            view.span);
        goto cleanup;
    }
    if (!r_semantic_callable_resources(context, node, &resource_flags)) {
        success = context->resource_status == R_FRONTEND_OK;
        goto cleanup;
    }
    if (effects != R_TYPE_ID_INVALID &&
        !r_semantic_intern_effect_carrier(context, returned, effects, &returned)) {
        goto cleanup;
    }
    for (size_t index = types.count; index != 0U; --index) {
        if (!r_semantic_intern_derived_type(context,
                                            R_SEMANTIC_TYPE_FUNCTION_PARAMETER,
                                            types.items[index - 1U],
                                            chain,
                                            UINT64_C(0),
                                            R_SEMANTIC_TYPE_FLAG_NONE,
                                            &chain)) {
            goto cleanup;
        }
    }
    success = r_semantic_intern_derived_type(
        context,
        R_SEMANTIC_TYPE_FUNCTION,
        returned,
        chain,
        (uint64_t)types.count,
        resource_flags | (asynchronous ? R_SEMANTIC_TYPE_FLAG_CALL_ASYNC : 0U),
        result);
cleanup:
    r_context_free(context, types.items);
    return success;
}

/* R-TYPE-0054 (L28): whether the function `target`, with the callable signature `signature` of
   its item, converts to the function type `destination`: parameters, result, checked errors and
   the synchronous or async form agree exactly, and the function proves every resource guarantee
   the type promises. */
static bool r_function_value_matches(const RFrontendContext *context,
                                     RTypeId signature,
                                     RTypeId destination) {
    const RSemanticType *provided = r_semantic_type(context, signature);
    const RSemanticType *required = r_semantic_type(context, destination);
    if (provided == NULL || required == NULL || required->kind != R_SEMANTIC_TYPE_FUNCTION)
        return false;
    const uint32_t required_resources = required->flags & R_SEMANTIC_TYPE_FLAG_RESOURCES;
    const bool provided_async = (provided->flags & R_SEMANTIC_TYPE_FLAG_CALL_ASYNC) != 0U;
    const bool required_async = (required->flags & R_SEMANTIC_TYPE_FLAG_CALL_ASYNC) != 0U;
    return provided->base == required->base && provided->second == required->second &&
           provided->length == required->length && provided_async == required_async &&
           (provided->flags & required_resources) == required_resources;
}

/* The callable signature a function item or a function value provides, or 0. */
static RTypeId r_function_value_source(const RFrontendContext *context,
                                       RTypeId type,
                                       RSymbolId *target) {
    const RSemanticType *value = r_semantic_type(context, r_semantic_value_type(context, type));
    const RSemanticAggregate *item =
        r_semantic_aggregate_for_type(context, r_semantic_value_type(context, type));
    *target = R_SYMBOL_ID_INVALID;
    if (item != NULL && item->function_item_target != R_SYMBOL_ID_INVALID) {
        *target = item->function_item_target;
        return item->closure_signature;
    }
    return value != NULL && value->kind == R_SEMANTIC_TYPE_FUNCTION
               ? r_semantic_value_type(context, type)
               : R_TYPE_ID_INVALID;
}

static bool r_function_value_record(RFrontendContext *context,
                                    RTypeId type,
                                    RSymbolId function,
                                    RSourceSpan span) {
    for (size_t index = 0U; index < context->function_value_target_count; ++index)
        if (context->function_value_targets[index].type == type &&
            context->function_value_targets[index].function == function)
            return true;
    if (!r_grow_array(context,
                      (void **)&context->function_value_targets,
                      &context->function_value_target_capacity,
                      sizeof(*context->function_value_targets),
                      context->function_value_target_count + 1U))
        return false;
    context->function_value_targets[context->function_value_target_count++] =
        (RFunctionValueTarget){type, function, span};
    return true;
}

/* M42-2: whether the concrete function type `destination` meets the requirements of R-TYPE-0054.
   A type that depends on a generic parameter passes its declaration (M42-1), so the types of an
   instance, a field of `holder<const i32*>` for one, are checked where a function converts to
   them: every function value comes from such a conversion (r_function_value_resolve_targets). */
static bool r_function_value_concrete(RFrontendContext *context, RTypeId destination) {
    const RSemanticType *function = r_semantic_type(context, destination);
    RTypeVector parameters = {0};
    RTypeId returned = function->base;
    RTypeId effects = R_TYPE_ID_INVALID;
    const RSemanticType *carrier = r_semantic_type(context, returned);
    bool valid = true;
    if (carrier != NULL && carrier->kind == R_SEMANTIC_TYPE_EFFECT_CARRIER) {
        returned = carrier->base;
        effects = carrier->second;
    }
    for (RTypeId chain = function->second; chain != R_TYPE_ID_INVALID;) {
        const RSemanticType *item = r_semantic_type(context, chain);
        if (item == NULL || item->kind != R_SEMANTIC_TYPE_FUNCTION_PARAMETER) {
            context->resource_status = R_FRONTEND_INTERNAL_ERROR;
            r_context_free(context, parameters.items);
            return false;
        }
        if (!r_type_vector_push(context, &parameters, item->base)) {
            r_context_free(context, parameters.items);
            return false;
        }
        chain = item->second;
    }
    valid = r_generic_type_is_dependent(context, returned, 0U) || r_closure_sync_result(context, returned);
    for (size_t index = 0U; valid && index < parameters.count; ++index)
        valid = r_generic_type_is_dependent(context, parameters.items[index], 0U) ||
                r_closure_sync_input(context, parameters.items[index]);
    if (valid && (function->flags & R_SEMANTIC_TYPE_FLAG_CALL_ASYNC) != 0U)
        valid = r_function_value_async_signature(context, &parameters, returned, effects);
    r_context_free(context, parameters.items);
    return valid;
}

/* R-TYPE-0054 (L28): a function item, or a function value with more guarantees, where a function
   type is expected converts to it. */
static bool r_function_value_convert(RBodyContext *body,
                                     RTypeId contextual_type,
                                     RExpressionResult *result) {
    RFrontendContext *context = body->frontend;
    const RTypeId destination = r_semantic_value_type(context, contextual_type);
    const RSemanticType *expected = r_semantic_type(context, destination);
    RSymbolId target = R_SYMBOL_ID_INVALID;
    if (!result->valid || expected == NULL || expected->kind != R_SEMANTIC_TYPE_FUNCTION ||
        r_semantic_value_type(context, result->type) == destination)
        return true;
    const RTypeId signature = r_function_value_source(context, result->type, &target);
    const RSemanticAggregate *closure =
        r_semantic_aggregate_for_type(context, r_semantic_value_type(context, result->type));
    if ((closure != NULL) && closure->is_closure &&
        (closure->function_item_target == R_SYMBOL_ID_INVALID)) {
        result->valid = false;
        return r_body_diagnostic(body,
                                 "R-DIAG-TYPE-001",
                                 "R-TYPE-0054",
                                 "a lambda does not convert to a function type; a function value "
                                 "designates a declared function, a closed generic function or a "
                                 "method",
                                 r_body_hir_node(body, result->node)->span);
    }
    if (signature == R_TYPE_ID_INVALID)
        return true;
    const RSourceSpan span = r_body_hir_node(body, result->node)->span;
    if (target == R_SYMBOL_ID_INVALID) {
        /* A value keeps its function; only guarantees the destination does not promise are
           forgotten. */
        const RSemanticType *source = r_semantic_type(context, signature);
        const uint32_t required = expected->flags & R_SEMANTIC_TYPE_FLAG_RESOURCES;
        if (source->base != expected->base || source->second != expected->second ||
            ((source->flags ^ expected->flags) & R_SEMANTIC_TYPE_FLAG_CALL_ASYNC) != 0U ||
            (source->flags & required) != required)
            return true;
        return r_body_place_to_value(body, result) &&
               (!result->valid || r_body_append_cast(body, result, destination, span, false));
    }
    if (!r_function_value_matches(context, signature, destination)) {
        result->valid = false;
        return r_body_diagnostic(body,
                                 "R-DIAG-TYPE-001",
                                 "R-TYPE-0054",
                                 "the function does not match the function type: parameters, "
                                 "result, checked errors and async form agree exactly, and the "
                                 "function proves every resource guarantee of the type",
                                 span);
    }
    RHirNodeId node;
    if (!r_body_append_node(body,
                            R_HIR_FUNCTION_ADDRESS,
                            span,
                            destination,
                            target,
                            R_TOKEN_INVALID,
                            0U,
                            false,
                            NULL,
                            &node))
        return false;
    r_body_expression_invalid(result);
    result->node = node;
    result->type = destination;
    result->valid = true;
    return true;
}

/* R-TYPE-0054 (L28): `==` and `!=` between a function value and a function item compare the
   value with the item converted to its type. */
static bool r_function_value_compare_operands(RBodyContext *body,
                                              RExpressionResult *left,
                                              RExpressionResult *right) {
    RFrontendContext *context = body->frontend;
    if (r_semantic_value_kind(context, left->type) == R_SEMANTIC_TYPE_FUNCTION) {
        return r_function_value_convert(body, left->type, right);
    }
    if (r_semantic_value_kind(context, right->type) == R_SEMANTIC_TYPE_FUNCTION) {
        return r_function_value_convert(body, right->type, left);
    }
    return true;
}

/* R-TYPE-0054 (L28): the callable signature a value of the function type `type_id` provides:
   the same parameters, result, checked errors and guarantees, in the mode `mode` (none for
   shared, CALL_MUT or CALL_ONCE), and always in once mode for an async type. */
static bool r_function_value_signature(RFrontendContext *context,
                                       RTypeId type_id,
                                       uint32_t mode,
                                       RTypeId *signature) {
    const RSemanticType *found = r_semantic_type(context, r_semantic_value_type(context, type_id));
    if (found == NULL || found->kind != R_SEMANTIC_TYPE_FUNCTION) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    const RSemanticType type = *found;
    const bool asynchronous = (type.flags & R_SEMANTIC_TYPE_FLAG_CALL_ASYNC) != 0U;
    return r_semantic_intern_derived_type(
        context,
        R_SEMANTIC_TYPE_RAW_FUNCTION,
        type.base,
        type.second,
        type.length,
        R_SEMANTIC_TYPE_FLAG_CALLABLE | (type.flags & R_SEMANTIC_TYPE_FLAG_RESOURCES) |
            (asynchronous ? (R_SEMANTIC_TYPE_FLAG_CALL_ASYNC | R_SEMANTIC_TYPE_FLAG_CALL_ONCE)
                          : (mode & (R_SEMANTIC_TYPE_FLAG_CALL_MUT |
                                     R_SEMANTIC_TYPE_FLAG_CALL_ONCE))),
        signature);
}

/* Whether `symbol` applies the `call` prototype of a callable trait to a function type, so that a
   call of it calls the function the receiver designates. */
static bool r_function_value_dispatcher(const RFrontendContext *context, RSymbolId symbol) {
    if (symbol == R_SYMBOL_ID_INVALID || (size_t)symbol > context->semantic_symbol_count)
        return false;
    const RSemanticSymbol *function = &context->semantic_symbols[symbol - 1U];
    if (function->kind != R_SEMANTIC_SYMBOL_FUNCTION || function->generic_origin == 0U)
        return false;
    const RSemanticSymbol *origin = &context->semantic_symbols[function->generic_origin - 1U];
    if (!origin->is_trait_prototype || origin->trait == 0U ||
        context->semantic_traits[origin->trait - 1U].callable_signature == R_TYPE_ID_INVALID)
        return false;
    const RSemanticType *self =
        r_semantic_type(context, context->generic_arguments[function->first_generic_argument]);
    return self != NULL && self->kind == R_SEMANTIC_TYPE_FUNCTION;
}

bool r_semantic_function_value_dispatcher(const RFrontendContext *context, RSymbolId symbol) {
    return r_function_value_dispatcher(context, symbol);
}

/* R-TYPE-0054 (L28): a function value holds no data, so the result of a call through it
   borrows only from the arguments its function type allows, never from the value. */
static void r_function_value_receiver_unborrowed(RFrontendContext *context, RSymbolId dispatcher) {
    RSemanticSymbol *symbol = &context->semantic_symbols[dispatcher - 1U];
    uint32_t count = 0U;
    uint32_t only = 0U;
    symbol->return_borrow_parameters_low &= ~UINT64_C(1);
    symbol->return_borrow_held_low &= ~UINT64_C(1);
    for (uint32_t index = 0U; index < 128U; ++index) {
        const uint64_t mask = index < 64U ? symbol->return_borrow_parameters_low
                                          : symbol->return_borrow_parameters_high;
        if ((mask & (UINT64_C(1) << (index % 64U))) != 0U) {
            count += 1U;
            only = index + 1U;
        }
    }
    if (symbol->return_borrow_parameter != 0U) {
        symbol->return_borrow_parameter = count == 1U ? only : UINT32_MAX;
    }
}

/* The dispatcher of calls through values of the function type `type_id` in the callable mode
   `mode` (R-TYPE-0054). */
static bool r_function_value_call(RFrontendContext *context,
                                  RSourceId source,
                                  RTypeId type_id,
                                  uint32_t mode,
                                  RSymbolId *dispatcher) {
    RTypeId signature;
    uint32_t trait;
    uint32_t call_name;
    RTypeVector arguments = {0};
    bool success;
    *dispatcher = R_SYMBOL_ID_INVALID;
    if (!r_function_value_signature(context, type_id, mode, &signature) ||
        !r_semantic_callable_trait(context, source, signature, &trait) ||
        !r_closure_intern(context, "call", &call_name))
        return false;
    const RSemanticMethod *prototype = r_semantic_find_trait_method(context, trait, call_name);
    if (prototype == NULL) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    success = r_type_vector_push(context, &arguments, r_semantic_value_type(context, type_id)) &&
              r_generic_apply_function(context, prototype->function, &arguments, dispatcher);
    r_context_free(context, arguments.items);
    if (success && r_function_value_dispatcher(context, *dispatcher)) {
        r_function_value_receiver_unborrowed(context, *dispatcher);
    }
    return success;
}

/* The value types that reach `type` through conversions of values (R-TYPE-0054): `type` itself
   and every function type with more guarantees whose values are converted to it. */
static bool r_function_value_reach(RFrontendContext *context,
                                   RTypeId type,
                                   const RTypeVector *sources,
                                   const RTypeVector *destinations,
                                   RTypeVector *reach) {
    reach->count = 0U;
    if (!r_type_vector_push(context, reach, type))
        return false;
    for (bool changed = true; changed;) {
        changed = false;
        for (size_t edge = 0U; edge < sources->count; ++edge) {
            bool to = false, from = false;
            for (size_t index = 0U; index < reach->count; ++index) {
                to = to || reach->items[index] == destinations->items[edge];
                from = from || reach->items[index] == sources->items[edge];
            }
            if (to && !from) {
                if (!r_type_vector_push(context, reach, sources->items[edge]))
                    return false;
                changed = true;
            }
        }
    }
    return true;
}

/* R-TYPE-0054 (L28): the values of a function type designate the functions converted to it and
   to every function type whose values convert to it. Each dispatcher of the type reaches all of
   them, as a dispatching method reaches the implementation of each member of a dyn interface
   (R-TYPE-0051), so the call graph, stack and resource proofs pass through every target
   (R-FUNC-0004). The conversions are read from the program after every generic instance is
   closed, so conversions in instances count with their own types. */
static bool r_function_value_resolve_targets(RFrontendContext *context) {
    RTypeVector sources = {0}, destinations = {0}, reach = {0};
    size_t *targets = NULL;
    size_t target_count = 0U;
    bool success = false;
    context->function_value_target_count = 0U;
    for (size_t index = 0U; index < context->hir_node_count; ++index) {
        const RHirNode node = context->hir_nodes[index];
        const RTypeId type = r_semantic_value_type(context, node.type);
        if (r_semantic_value_kind(context, type) != R_SEMANTIC_TYPE_FUNCTION ||
            r_generic_type_is_dependent(context, type, 0U))
            continue;
        if (node.kind == R_HIR_FUNCTION_ADDRESS && node.symbol != R_SYMBOL_ID_INVALID &&
            (size_t)node.symbol <= context->semantic_symbol_count &&
            !r_generic_function_is_open(context, &context->semantic_symbols[node.symbol - 1U])) {
            if (!r_function_value_concrete(context, type)) {
                const bool asynchronous =
                    (r_semantic_type(context, type)->flags & R_SEMANTIC_TYPE_FLAG_CALL_ASYNC) != 0U;
                if (context->resource_status != R_FRONTEND_OK ||
                    !r_semantic_add_diagnostic(context,
                                               asynchronous ? "R-DIAG-ASYNC-001" : "R-DIAG-TYPE-001",
                                               "R-TYPE-0054",
                                               asynchronous ? "an async function type has Send, unborrowed "
                                                              "parameters, result and errors"
                                                            : "a function type requires supported "
                                                              "synchronous parameter and result types",
                                               node.span))
                    goto cleanup;
                continue;
            }
            if (!r_function_value_record(context, type, node.symbol, node.span))
                goto cleanup;
        } else if (node.kind == R_HIR_CAST && node.child_count == 1U) {
            const RHirNodeId operand = context->hir_children[node.first_child];
            const RTypeId from = r_semantic_value_type(context, context->hir_nodes[operand - 1U].type);
            if (from != type && r_semantic_value_kind(context, from) == R_SEMANTIC_TYPE_FUNCTION &&
                !r_generic_type_is_dependent(context, from, 0U) &&
                (!r_type_vector_push(context, &sources, from) ||
                 !r_type_vector_push(context, &destinations, type)))
                goto cleanup;
        }
    }
    if (context->function_value_target_count != 0U) {
        targets = r_context_allocate(context,
                                     context->function_value_target_count * sizeof(*targets));
        if (targets == NULL)
            goto cleanup;
    }
    for (size_t symbol = 1U; symbol <= context->semantic_symbol_count; ++symbol) {
        if (!r_function_value_dispatcher(context, (RSymbolId)symbol))
            continue;
        const RSemanticSymbol dispatcher = context->semantic_symbols[symbol - 1U];
        const RTypeId self = r_semantic_value_type(
            context, context->generic_arguments[dispatcher.first_generic_argument]);
        if (r_generic_type_is_dependent(context, self, 0U))
            continue;
        if (!r_function_value_reach(context, self, &sources, &destinations, &reach))
            goto cleanup;
        target_count = 0U;
        for (size_t entry = 0U; entry < context->function_value_target_count; ++entry) {
            const RFunctionValueTarget pair = context->function_value_targets[entry];
            bool reached = false, known = false;
            for (size_t index = 0U; index < reach.count; ++index)
                reached = reached || reach.items[index] == pair.type;
            for (size_t index = 0U; index < target_count; ++index)
                known = known ||
                        context->function_value_targets[targets[index]].function == pair.function;
            if (reached && !known)
                targets[target_count++] = entry;
        }
        /* The targets are dispatched in the order of their symbols. */
        for (size_t index = 1U; index < target_count; ++index)
            for (size_t other = index; other != 0U &&
                                       context->function_value_targets[targets[other - 1U]].function >
                                           context->function_value_targets[targets[other]].function;
                 --other) {
                const size_t swapped = targets[other];
                targets[other] = targets[other - 1U];
                targets[other - 1U] = swapped;
            }
        for (size_t index = 0U; index < target_count; ++index) {
            const RFunctionValueTarget pair = context->function_value_targets[targets[index]];
            if (!r_grow_array(context,
                              (void **)&context->dyn_targets,
                              &context->dyn_target_capacity,
                              sizeof(*context->dyn_targets),
                              context->dyn_target_count + 1U))
                goto cleanup;
            context->dyn_targets[context->dyn_target_count++] =
                (RDynTarget){(RSymbolId)symbol, self, pair.function, pair.span};
        }
    }
    success = true;
cleanup:
    r_context_free(context, targets);
    r_context_free(context, sources.items);
    r_context_free(context, destinations.items);
    r_context_free(context, reach.items);
    return success;
}

/* R-TYPE-0054 (L28): the declaration of a dispatcher of a function type: its receiver and the
   parameters its translation forwards to the target the receiver designates. */
static bool r_function_value_build_dispatcher(RFrontendContext *context,
                                              RSymbolId id,
                                              RHirNodeId *result) {
    const RSemanticSymbol function = context->semantic_symbols[id - 1U];
    RHirVector children = {0};
    RHirNode node;
    bool success = false;
    if (function.hir_node != R_HIR_NODE_ID_INVALID) {
        *result = function.hir_node;
        return true;
    }
    (void)memset(&node, 0, sizeof(node));
    node.kind = R_HIR_FUNCTION;
    node.span.source = function.module_source;
    node.type = function.return_type;
    node.auxiliary_type = function.async_start_type;
    node.symbol = id;
    if (!r_hir_append_node(context, node, result))
        return false;
    for (uint32_t index = 0U; index < function.parameter_count; ++index) {
        RSemanticSymbol parameter;
        RSymbolId parameter_id;
        RHirNodeId parameter_node;
        char name[32];
        const int written = index == 0U ? snprintf(name, sizeof(name), "$this")
                                        : snprintf(name, sizeof(name), "$argument%u", index);
        (void)memset(&parameter, 0, sizeof(parameter));
        parameter.kind = R_SEMANTIC_SYMBOL_PARAMETER;
        /* The storage of a parameter is named after its position in the declaration. */
        parameter.name_span.start = index;
        parameter.name_span.end = index;
        parameter.module_source = function.module_source;
        parameter.type = context->semantic_parameter_types[function.first_parameter_type + index];
        parameter.parent = id;
        parameter.object_state = R_SEMANTIC_OBJECT_STATE_INITIALIZED;
        parameter.signature_supported = true;
        if (written <= 0 || !r_closure_intern(context, name, &parameter.name_intern_id) ||
            !r_semantic_append_symbol(context, parameter, &parameter_id))
            goto cleanup;
        (void)memset(&node, 0, sizeof(node));
        node.kind = R_HIR_PARAMETER;
        node.span.source = function.module_source;
        node.type = parameter.type;
        node.symbol = parameter_id;
        if (!r_hir_append_node(context, node, &parameter_node) ||
            !r_hir_vector_push(context, &children, parameter_node))
            goto cleanup;
    }
    success = r_hir_commit_children(context, *result, &children);
    if (success)
        context->semantic_symbols[id - 1U].hir_node = *result;
cleanup:
    r_context_free(context, children.items);
    return success;
}

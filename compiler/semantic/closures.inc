/* R-FUNC-0015 through R-FUNC-0017 and R-TYPE-0044: local lambda declarations, captured
 * environments, closure calls and callable constraints. A lambda is lowered to an environment
 * aggregate whose fields are its captures and to a method `call` of that aggregate whose
 * receiver is the environment; a callable constraint `fn(P...) -> R` is a synthetic trait with
 * the single prototype `call`, so every closure call is a statically bound method call. */

typedef struct RClosureUse {
    uint32_t name_intern_id;
    RSourceSpan span;
    bool written;
    /* Written only as the receiver of method calls, never assigned or borrowed. */
    bool receiver_only;
    bool moved;
} RClosureUse;

typedef struct RClosureUseVector {
    RClosureUse *items;
    size_t count;
    size_t capacity;
} RClosureUseVector;

typedef struct RClosureNameVector {
    uint32_t *items;
    size_t count;
    size_t capacity;
} RClosureNameVector;

typedef struct RClosureScan {
    RClosureUseVector uses;
    RClosureNameVector declared;
} RClosureScan;

static bool r_closure_intern(RFrontendContext *context, const char *text, uint32_t *id) {
    return r_intern_bytes(context, (const uint8_t *)text, strlen(text), id);
}

/* --- callable signatures and their synthetic traits ------------------------------------- */

static RSemanticReceiverKind r_closure_call_mode(const RFrontendContext *context, RAstRef node) {
    RAstRef mode;
    RAstTokenView name;
    if (!r_ast_find_direct_child(context, node, R_SYNTAX_CALLABLE_MODE, &mode) ||
        !r_ast_direct_token(context, mode, 0U, NULL, &name))
        return r_semantic_type_has_direct_token(context, node, R_TOKEN_KW_ASYNC)
                   ? R_SEMANTIC_RECEIVER_VALUE
                   : R_SEMANTIC_RECEIVER_NONE;
    if (r_semantic_token_text_equal_owned(context, &name, "once"))
        return R_SEMANTIC_RECEIVER_VALUE;
    if (r_semantic_token_text_equal_owned(context, &name, "mut"))
        return R_SEMANTIC_RECEIVER_EXCLUSIVE;
    return R_SEMANTIC_RECEIVER_SHARED;
}

static RSemanticReceiverKind r_closure_signature_mode(const RSemanticType *type) {
    if ((type->flags & R_SEMANTIC_TYPE_FLAG_CALL_ONCE) != 0U)
        return R_SEMANTIC_RECEIVER_VALUE;
    if ((type->flags & R_SEMANTIC_TYPE_FLAG_CALL_MUT) != 0U)
        return R_SEMANTIC_RECEIVER_EXCLUSIVE;
    return R_SEMANTIC_RECEIVER_SHARED;
}

/* R-TYPE-0044, R-TYPE-0053 (L18.4): a signature whose last parameter `P...` expands a pack; its
   link has length one, and substitution splices the elements of the tuple it becomes. */
static bool r_semantic_callable_signature_expanding(RFrontendContext *context,
                                                    const RTypeVector *parameters,
                                                    bool expands_last,
                                                    RTypeId return_type,
                                                    RSemanticReceiverKind mode,
                                                    uint32_t resource_flags,
                                                    RTypeId effects,
                                                    bool asynchronous,
                                                    RTypeId *signature) {
    RTypeId chain = R_TYPE_ID_INVALID;
    size_t index;
    if (effects != R_TYPE_ID_INVALID &&
        !r_semantic_intern_effect_carrier(context, return_type, effects, &return_type))
        return false;
    for (index = parameters->count; index != 0U; --index) {
        if (!r_semantic_intern_derived_type(
                context,
                R_SEMANTIC_TYPE_FUNCTION_PARAMETER,
                parameters->items[index - 1U],
                chain,
                (expands_last && (index == parameters->count)) ? UINT64_C(1) : UINT64_C(0),
                R_SEMANTIC_TYPE_FLAG_NONE,
                &chain)) {
            return false;
        }
    }
    return r_semantic_intern_derived_type(
        context,
        R_SEMANTIC_TYPE_RAW_FUNCTION,
        return_type,
        chain,
        (uint64_t)parameters->count,
        R_SEMANTIC_TYPE_FLAG_CALLABLE | resource_flags |
            (asynchronous ? R_SEMANTIC_TYPE_FLAG_CALL_ASYNC : 0U) |
            (mode == R_SEMANTIC_RECEIVER_EXCLUSIVE ? R_SEMANTIC_TYPE_FLAG_CALL_MUT
             : mode == R_SEMANTIC_RECEIVER_VALUE   ? R_SEMANTIC_TYPE_FLAG_CALL_ONCE
                                                   : 0U),
        signature);
}

static bool r_semantic_callable_signature(RFrontendContext *context,
                                          const RTypeVector *parameters,
                                          RTypeId return_type,
                                          RSemanticReceiverKind mode,
                                          uint32_t resource_flags,
                                          RTypeId effects,
                                          bool asynchronous,
                                          RTypeId *signature) {
    return r_semantic_callable_signature_expanding(context,
                                                   parameters,
                                                   false,
                                                   return_type,
                                                   mode,
                                                   resource_flags,
                                                   effects,
                                                   asynchronous,
                                                   signature);
}

/* The synthetic trait of one signature: created once, shared by every constraint and lambda
 * that spells the same parameter and result types. */
static bool r_semantic_callable_trait(RFrontendContext *context,
                                      RSourceId module_source,
                                      RTypeId signature,
                                      uint32_t *trait_id) {
    const RSemanticType *signature_type = r_semantic_type(context, signature);
    RSemanticType type;
    RSemanticTrait trait;
    RGenericSchema schema;
    RGenericParameter parameter;
    RSemanticSymbol prototype;
    RTypeVector parameters = {0};
    RTypeId receiver_type;
    RTypeId chain;
    RSymbolId prototype_id;
    char label[48];
    int written;
    size_t index;
    bool success = false;

    for (index = 0U; index < context->semantic_trait_count; ++index) {
        if (context->semantic_traits[index].callable_signature == signature) {
            *trait_id = (uint32_t)index + UINT32_C(1);
            return true;
        }
    }
    if ((signature_type == NULL) || (signature_type->kind != R_SEMANTIC_TYPE_RAW_FUNCTION)) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    /* Interning below reallocates the type table, so the signature is copied first. */
    type = *signature_type;
    (void)memset(&trait, 0, sizeof(trait));
    (void)memset(&schema, 0, sizeof(schema));
    (void)memset(&parameter, 0, sizeof(parameter));
    (void)memset(&prototype, 0, sizeof(prototype));
    if ((type.flags & R_SEMANTIC_TYPE_FLAG_CALL_ASYNC) != 0U)
        parameter.constraints = R_GENERIC_SEND | R_GENERIC_UNBORROWED;
    written = snprintf(label, sizeof(label), "$callable%u", (unsigned)signature);
    if ((written <= 0) || ((size_t)written >= sizeof(label)) ||
        !r_closure_intern(context, label, &trait.name_intern_id) ||
        !r_closure_intern(context, "Self", &parameter.name_intern_id) ||
        !r_grow_array(context,
                      (void **)&context->generic_parameters,
                      &context->generic_parameter_capacity,
                      sizeof(parameter),
                      context->generic_parameter_count + 1U) ||
        !r_semantic_intern_type(context,
                                R_SEMANTIC_TYPE_PARAMETER,
                                (RTypeId)context->generic_parameter_count + 1U,
                                &parameter.type)) {
        return false;
    }
    schema.first_parameter = (uint32_t)context->generic_parameter_count;
    schema.parameter_count = UINT32_C(1);
    context->generic_parameters[context->generic_parameter_count++] = parameter;
    if (!r_grow_array(context,
                      (void **)&context->generic_schemas,
                      &context->generic_schema_capacity,
                      sizeof(schema),
                      context->generic_schema_count + 1U)) {
        return false;
    }
    context->generic_schemas[context->generic_schema_count++] = schema;
    trait.generic_schema = (uint32_t)context->generic_schema_count;
    trait.self_type = parameter.type;
    trait.callable_signature = signature;
    trait.module_source = module_source;
    if (!r_grow_array(context,
                      (void **)&context->semantic_traits,
                      &context->semantic_trait_capacity,
                      sizeof(trait),
                      context->semantic_trait_count + 1U)) {
        return false;
    }
    context->semantic_traits[context->semantic_trait_count++] = trait;
    *trait_id = (uint32_t)context->semantic_trait_count;

    /* The prototype `R call(const Self* this, P...)`. */
    const RSemanticReceiverKind mode = r_closure_signature_mode(&type);
    receiver_type = parameter.type;
    if ((mode != R_SEMANTIC_RECEIVER_VALUE &&
         !r_semantic_intern_derived_type(context,
                                         R_SEMANTIC_TYPE_BORROW,
                                         parameter.type,
                                         R_TYPE_ID_INVALID,
                                         UINT64_C(0),
                                         mode == R_SEMANTIC_RECEIVER_SHARED
                                             ? R_SEMANTIC_TYPE_FLAG_SHARED
                                             : R_SEMANTIC_TYPE_FLAG_NONE,
                                         &receiver_type)) ||
        !r_type_vector_push(context, &parameters, receiver_type)) {
        goto cleanup;
    }
    for (chain = type.second; chain != R_TYPE_ID_INVALID;) {
        const RSemanticType *item = r_semantic_type(context, chain);
        if ((item == NULL) || (item->kind != R_SEMANTIC_TYPE_FUNCTION_PARAMETER) ||
            !r_type_vector_push(context, &parameters, item->base)) {
            goto cleanup;
        }
        /* R-TYPE-0053 (L18.4): `P...` stands for the trailing arguments. */
        prototype.is_pack_variadic = item->length == UINT64_C(1);
        chain = item->second;
    }
    prototype.kind = R_SEMANTIC_SYMBOL_FUNCTION;
    prototype.module_source = module_source;
    prototype.is_protected = true;
    const RSemanticType *result_type = r_semantic_type(context, type.base);
    prototype.return_type = type.base;
    if (result_type != NULL && result_type->kind == R_SEMANTIC_TYPE_EFFECT_CARRIER) {
        prototype.return_type = result_type->base;
        prototype.throws_type = result_type->second;
        prototype.effect_carrier_type = type.base;
    }
    prototype.parameter_count = (uint32_t)parameters.count;
    prototype.generic_schema = trait.generic_schema;
    prototype.associated_type = parameter.type;
    prototype.receiver_kind = mode;
    prototype.trait = *trait_id;
    prototype.is_method = true;
    prototype.is_trait_prototype = true;
    prototype.is_synthetic = true;
    prototype.has_prototype = true;
    prototype.signature_supported = true;
    prototype.is_async = (type.flags & R_SEMANTIC_TYPE_FLAG_CALL_ASYNC) != 0U;
    if (prototype.is_async && !r_semantic_checked_async_types(context,
                                                              prototype.return_type,
                                                              prototype.throws_type,
                                                              &prototype.async_task_type,
                                                              &prototype.async_start_type))
        goto cleanup;
    prototype.is_noalloc = (type.flags & R_SEMANTIC_TYPE_FLAG_NOALLOC) != 0U;
    prototype.is_nonblocking = (type.flags & R_SEMANTIC_TYPE_FLAG_NONBLOCKING) != 0U;
    /* R-BORROW-0009: a dependent result borrows conservatively from every borrowing parameter. */
    r_semantic_prototype_borrow_mapping(
        context, &prototype, parameters.items, (uint32_t)parameters.count);
    if (!r_semantic_commit_parameter_types(context, &parameters, &prototype.first_parameter_type) ||
        !r_closure_intern(context, "call", &prototype.method_name_intern_id) ||
        !r_generic_method_symbol_name(
            context, parameter.type, prototype.method_name_intern_id, &prototype.name_intern_id) ||
        !r_semantic_append_symbol(context, prototype, &prototype_id) ||
        !r_semantic_register_method(context, prototype_id)) {
        goto cleanup;
    }
    success = true;
cleanup:
    r_context_free(context, parameters.items);
    return success;
}

/* A direct shared or exclusive borrow, or a slice, of a value type that carries no borrows or
   slices itself: it can neither escape nor alias beyond the call (R-BORROW-0019). */
static bool r_semantic_type_is_nonescaping_input(const RFrontendContext *context, RTypeId type_id) {
    const RSemanticType *type = r_semantic_type(context, r_semantic_value_type(context, type_id));

    return (type != NULL) &&
           ((type->kind == R_SEMANTIC_TYPE_BORROW) || (type->kind == R_SEMANTIC_TYPE_SLICE)) &&
           ((type->flags & R_SEMANTIC_TYPE_FLAG_NULLABLE) == 0U) &&
           !r_semantic_type_contains_borrow(context, type->base) &&
           !r_semantic_type_contains_slice(context, type->base) &&
           (r_semantic_value_kind(context, type->base) != R_SEMANTIC_TYPE_STR);
}

/* R-TYPE-0044: a synchronous result follows ordinary borrowing rules, like a function result,
   including an exclusive borrow bounded by the input origins (R-BORROW-0009). */
static bool r_closure_sync_result(const RFrontendContext *context, RTypeId type) {
    return r_semantic_value_kind(context, type) != R_SEMANTIC_TYPE_CONST &&
           r_semantic_value_kind(context, type) != R_SEMANTIC_TYPE_NEVER &&
           r_semantic_type_is_slice_scalar(context, type, true) &&
           (r_semantic_value_kind(context, type) == R_SEMANTIC_TYPE_VOID ||
            r_semantic_type_is_copy(context, type) ||
            r_semantic_type_is_supported_move(context, type) ||
            r_semantic_type_is_direct_exclusive_borrow_output(context, type) ||
            r_semantic_type_is_view_of_view_holder(context, type));
}

static bool r_closure_sync_input(const RFrontendContext *context, RTypeId type) {
    if (r_semantic_type_is_borrow_bearing_aggregate_input(context, type) ||
        r_semantic_type_is_view_of_view_holder(context, type) ||
        r_semantic_type_is_nonescaping_input_borrow(context, type) ||
        r_semantic_type_is_nonescaping_string_input(context, type) ||
        r_semantic_type_is_string_slice_input(context, type) ||
        r_semantic_type_is_optional_input_borrow(context, type) ||
        r_semantic_type_is_nonescaping_input(context, type))
        return true;
    return r_semantic_type_is_slice_scalar(context, type, false) &&
           !r_semantic_type_contains_borrow(context, type) &&
           !r_semantic_type_contains_slice(context, type) &&
           (r_semantic_type_is_copy(context, type) ||
            r_semantic_type_is_supported_move(context, type));
}

static bool r_closure_async_value(const RFrontendContext *context, RTypeId type, bool allow_void) {
    if (r_semantic_value_kind(context, type) == R_SEMANTIC_TYPE_VOID)
        return allow_void;
    return r_generic_has_property(context, type, R_GENERIC_SEND, 0U) &&
           r_generic_has_property(context, type, R_GENERIC_UNBORROWED, 0U);
}

static bool r_closure_async_signature(const RFrontendContext *context,
                                      const RTypeVector *parameters,
                                      RTypeId result,
                                      RTypeId effects) {
    if (!r_closure_async_value(context, result, true))
        return false;
    for (size_t index = 0U; index < parameters->count; ++index)
        if (!r_closure_async_value(context, parameters->items[index], false))
            return false;
    for (uint32_t index = 0U; index < r_semantic_effect_count(context, effects); ++index)
        if (!r_closure_async_value(context, r_semantic_effect_at(context, effects, index), false))
            return false;
    return true;
}

/* R-TYPE-0044: every pending `fn(P...) -> R` constraint becomes a trait constraint. */
static bool r_generic_resolve_callable_constraint(RFrontendContext *context,
                                                  RGenericPendingCallable pending) {
    RAstNodeView view;
    RTypeVector types = {0};
    RTypeId signature;
    uint32_t trait;
    uint32_t child;
    bool valid = true;
    bool never_result = false;
    bool success;
    if (!r_ast_ref_view(context, pending.node, &view)) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    size_t expansion_position = SIZE_MAX;
    for (child = 0U; child < view.child_count; ++child) {
        RAstRef part;
        RAstRef next;
        RAstNodeView part_view;
        RAstTokenView next_token;
        RTypeId type;
        if (!r_ast_ref_child(context, pending.node, child, &part) ||
            !r_ast_ref_view(context, part, &part_view)) {
            context->resource_status = R_FRONTEND_INTERNAL_ERROR;
            r_context_free(context, types.items);
            return false;
        }
        if (part_view.kind != R_SYNTAX_TYPE) {
            continue;
        }
        /* R-TYPE-0053 (L18.4): `P...` expands a pack as the last parameter. */
        const bool expansion = (child + 1U < view.child_count) &&
                               r_ast_ref_child(context, pending.node, child + 1U, &next) &&
                               r_ast_ref_token(context, next, &next_token) &&
                               (next_token.kind == R_TOKEN_ELLIPSIS);
        if (expansion && (expansion_position != SIZE_MAX)) {
            valid = false;
            break;
        }
        if (expansion) {
            expansion_position = types.count;
        }
        context->pack_expansion_permitted =
            expansion && r_semantic_type_ast_is_plain_name(context, part);
        const bool formed = r_semantic_type_from_ast(context, part, &type);
        context->pack_expansion_permitted = false;
        if (formed && expansion && (r_generic_pack(context, type) == NULL)) {
            r_context_free(context, types.items);
            return r_semantic_add_diagnostic(context,
                                             "R-DIAG-TYPE-001",
                                             "R-TYPE-0053",
                                             "`...` expands a pack of the generic header",
                                             part_view.span);
        }
        if (!formed) {
            if (context->resource_status != R_FRONTEND_OK) {
                r_context_free(context, types.items);
                return false;
            }
            valid = false;
            break;
        }
        if (!r_type_vector_push(context, &types, type)) {
            r_context_free(context, types.items);
            return false;
        }
    }
    if ((expansion_position != SIZE_MAX) && (expansion_position + 2U != types.count)) {
        /* The expansion is the last parameter, before the result. */
        valid = false;
    }
    const bool expands_last = expansion_position != SIZE_MAX;
    if (valid && types.count != 0U) {
        never_result =
            r_semantic_value_kind(context, types.items[types.count - 1U]) == R_SEMANTIC_TYPE_NEVER;
        valid = r_closure_sync_result(context, types.items[types.count - 1U]);
        for (size_t parameter = 0U; valid && parameter + 1U < types.count; ++parameter)
            valid = r_closure_sync_input(context, types.items[parameter]);
    }
    if (!valid || (types.count == 0U)) {
        r_context_free(context, types.items);
        if (!r_semantic_add_diagnostic(context,
                                       "R-DIAG-TYPE-001",
                                       "R-TYPE-0044",
                                       never_result ? "a callable constraint result cannot be never"
                                                    : "a callable constraint requires supported "
                                                      "synchronous parameter and result types",
                                       view.span)) {
            return false;
        }
        return true;
    }
    types.count -= 1U;
    uint32_t resource_flags;
    RTypeId effects;
    bool effects_supported;
    const bool asynchronous =
        r_semantic_type_has_direct_token(context, pending.node, R_TOKEN_KW_ASYNC);
    if (!r_semantic_collect_throws(
            context, pending.node, asynchronous, &effects, &effects_supported)) {
        r_context_free(context, types.items);
        return false;
    }
    if (!effects_supported) {
        r_context_free(context, types.items);
        return true;
    }
    uint32_t set_parameters = 0U;
    for (uint32_t effect = 0U; effect < r_semantic_effect_count(context, effects); ++effect)
        if ((r_generic_parameter_constraints(context,
                                             r_semantic_effect_at(context, effects, effect)) &
             R_GENERIC_ERRORS) != 0U)
            ++set_parameters;
    if (set_parameters > 1U) {
        r_context_free(context, types.items);
        return r_semantic_add_diagnostic(
            context,
            "R-DIAG-TYPE-001",
            "R-TYPE-0049",
            "a callable constraint can infer at most one error set parameter",
            view.span);
    }
    if (asynchronous &&
        (r_closure_call_mode(context, pending.node) != R_SEMANTIC_RECEIVER_VALUE ||
         !r_closure_async_signature(context, &types, types.items[types.count], effects))) {
        r_context_free(context, types.items);
        return r_semantic_add_diagnostic(context,
                                         "R-DIAG-ASYNC-001",
                                         "R-FUNC-0015",
                                         "an async callable uses once mode with Send, "
                                         "unborrowed parameters, result and errors",
                                         view.span);
    }
    success = effects_supported &&
              r_semantic_callable_resources(context, pending.node, &resource_flags) &&
              r_semantic_callable_signature_expanding(context,
                                                      &types,
                                                      expands_last,
                                                      types.items[types.count],
                                                      r_closure_call_mode(context, pending.node),
                                                      resource_flags,
                                                      effects,
                                                      asynchronous,
                                                      &signature) &&
              r_semantic_callable_trait(context, pending.node.source, signature, &trait) &&
              r_semantic_append_trait_constraint(context, pending.parameter, trait);
    r_context_free(context, types.items);
    if (!success) {
        return false;
    }
    return true;
}

static bool r_generic_resolve_callable_constraints(RFrontendContext *context) {
    for (size_t index = 0U; index < context->generic_pending_callable_count; ++index) {
        if (!r_generic_resolve_callable_constraint(context,
                                                   context->generic_pending_callables[index]))
            return false;
    }
    return true;
}

/* --- capture analysis --------------------------------------------------------------------- */

static bool r_closure_push_receiver_use(RFrontendContext *context,
                                        RClosureScan *scan,
                                        const RAstTokenView *token,
                                        bool written,
                                        bool receiver);

static bool r_closure_push_use(RFrontendContext *context,
                               RClosureScan *scan,
                               const RAstTokenView *token,
                               bool written) {
    return r_closure_push_receiver_use(context, scan, token, written, false);
}

static bool r_closure_push_receiver_use(RFrontendContext *context,
                                        RClosureScan *scan,
                                        const RAstTokenView *token,
                                        bool written,
                                        bool receiver) {
    size_t index;
    for (index = 0U; index < scan->uses.count; ++index) {
        if (scan->uses.items[index].name_intern_id == token->intern_id) {
            RClosureUse *use = &scan->uses.items[index];
            if (written)
                use->receiver_only = (use->written ? use->receiver_only : true) && receiver;
            use->written = use->written || written;
            return true;
        }
    }
    if (!r_grow_array(context,
                      (void **)&scan->uses.items,
                      &scan->uses.capacity,
                      sizeof(*scan->uses.items),
                      scan->uses.count + 1U)) {
        return false;
    }
    scan->uses.items[scan->uses.count].name_intern_id = token->intern_id;
    scan->uses.items[scan->uses.count].span = token->span;
    scan->uses.items[scan->uses.count].written = written;
    scan->uses.items[scan->uses.count].receiver_only = written && receiver;
    scan->uses.items[scan->uses.count].moved = false;
    scan->uses.count += 1U;
    return true;
}

static bool r_closure_name_declared(const RClosureScan *scan, uint32_t name_intern_id) {
    size_t index;
    for (index = 0U; index < scan->declared.count; ++index) {
        if (scan->declared.items[index] == name_intern_id) {
            return true;
        }
    }
    return false;
}

static bool r_closure_declare(RFrontendContext *context, RClosureScan *scan, uint32_t name) {
    if (!r_grow_array(context,
                      (void **)&scan->declared.items,
                      &scan->declared.capacity,
                      sizeof(*scan->declared.items),
                      scan->declared.count + 1U)) {
        return false;
    }
    scan->declared.items[scan->declared.count++] = name;
    return true;
}

/* The identifier a primary expression names, when it is a plain name. */
static bool
r_closure_primary_name(const RFrontendContext *context, RAstRef node, RAstTokenView *token) {
    RAstNodeView view;
    RAstRef child;
    if (!r_ast_ref_view(context, node, &view) || (view.kind != R_SYNTAX_PRIMARY_EXPRESSION) ||
        (view.child_count != UINT32_C(1)) || !r_ast_ref_child(context, node, 0U, &child) ||
        !r_ast_ref_token(context, child, token)) {
        return false;
    }
    return (token->kind == R_TOKEN_IDENTIFIER) || (token->kind == R_TOKEN_KW_THIS);
}

/* The leftmost plain name of an expression: the root of an assignment or borrow target. */
static bool r_closure_leftmost_name(const RFrontendContext *context,
                                    RAstRef node,
                                    uint32_t depth,
                                    RAstTokenView *token) {
    RAstNodeView view;
    RAstRef child;
    if ((depth > r_frontend_tree_depth_limit(context)) || !r_ast_ref_view(context, node, &view)) {
        return false;
    }
    if (r_closure_primary_name(context, node, token)) {
        return true;
    }
    if ((view.child_count == 0U) || !r_ast_ref_child(context, node, 0U, &child)) {
        return false;
    }
    if (r_ast_ref_token(context, child, token)) {
        /* A unary operator precedes its operand. */
        return (view.child_count > 1U) && r_ast_ref_child(context, node, 1U, &child) &&
               r_closure_leftmost_name(context, child, depth + 1U, token);
    }
    return r_closure_leftmost_name(context, child, depth + 1U, token);
}

static bool r_closure_mark_written_as(RFrontendContext *context,
                                      RClosureScan *scan,
                                      RAstRef target,
                                      bool receiver) {
    RAstTokenView token;
    if (r_closure_leftmost_name(context, target, 0U, &token) &&
        !r_closure_name_declared(scan, token.intern_id)) {
        return r_closure_push_receiver_use(context, scan, &token, true, receiver);
    }
    return true;
}

static bool r_closure_mark_written(RFrontendContext *context, RClosureScan *scan, RAstRef target) {
    return r_closure_mark_written_as(context, scan, target, false);
}

static bool
r_closure_scan(RFrontendContext *context, RAstRef node, uint32_t depth, RClosureScan *scan) {
    RAstNodeView view;
    RAstTokenView token;
    uint32_t child_index;
    size_t declared_mark = scan->declared.count;
    bool scoped = false;

    if (depth > r_frontend_tree_depth_limit(context)) {
        context->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
        return false;
    }
    if (r_ast_ref_token(context, node, &token)) {
        return true;
    }
    if (!r_ast_ref_view(context, node, &view)) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    if (r_closure_primary_name(context, node, &token)) {
        if (!r_closure_name_declared(scan, token.intern_id) &&
            !r_closure_push_use(context, scan, &token, false)) {
            return false;
        }
        return true;
    }
    switch (view.kind) {
    case R_SYNTAX_BLOCK:
    case R_SYNTAX_LAMBDA_DECLARATION:
    case R_SYNTAX_CATCH_CLAUSE:
    case R_SYNTAX_CASE_CLAUSE:
    case R_SYNTAX_SELECT_CLAUSE:
    case R_SYNTAX_FOR_STATEMENT:
    case R_SYNTAX_FOR_IN_STATEMENT:
    case R_SYNTAX_COLLECTION_EXPRESSION:
    case R_SYNTAX_DICT_EXPRESSION:
        scoped = true;
        break;
    default:
        break;
    }
    /* Names a declaration introduces hide the enclosing names in its scope (R-FUNC-0016). */
    if ((view.kind == R_SYNTAX_OBJECT_DECLARATION) || (view.kind == R_SYNTAX_PARAMETER) ||
        (view.kind == R_SYNTAX_LAMBDA_DECLARATION) || (view.kind == R_SYNTAX_CATCH_CLAUSE) ||
        (view.kind == R_SYNTAX_CASE_PATTERN) || (view.kind == R_SYNTAX_MOVE_CAPTURE_LIST) ||
        (view.kind == R_SYNTAX_FOR_IN_STATEMENT) || (view.kind == R_SYNTAX_COMPREHENSION_FOR)) {
        for (child_index = 0U; child_index < view.child_count; ++child_index) {
            RAstRef child;
            if (r_ast_ref_child(context, node, child_index, &child) &&
                r_ast_ref_token(context, child, &token) &&
                ((token.kind == R_TOKEN_IDENTIFIER) || (token.kind == R_TOKEN_KW_THIS)) &&
                (view.kind != R_SYNTAX_MOVE_CAPTURE_LIST) &&
                !r_closure_declare(context, scan, token.intern_id)) {
                return false;
            }
        }
    }
    if ((view.kind == R_SYNTAX_ASSIGNMENT_EXPRESSION) ||
        ((view.kind == R_SYNTAX_BINARY_EXPRESSION) && (view.child_count >= 2U))) {
        /* An assignment is a binary node whose operator token assigns (A.6). */
        RAstRef target;
        RAstRef operator_ref;
        bool assigns = view.kind == R_SYNTAX_ASSIGNMENT_EXPRESSION;
        if (!assigns && r_ast_ref_child(context, node, 1U, &operator_ref) &&
            r_ast_ref_token(context, operator_ref, &token)) {
            assigns = (token.kind == R_TOKEN_EQUAL) || (token.kind == R_TOKEN_PLUS_EQUAL) ||
                      (token.kind == R_TOKEN_MINUS_EQUAL) || (token.kind == R_TOKEN_STAR_EQUAL) ||
                      (token.kind == R_TOKEN_SLASH_EQUAL) ||
                      (token.kind == R_TOKEN_PERCENT_EQUAL) || (token.kind == R_TOKEN_AMP_EQUAL) ||
                      (token.kind == R_TOKEN_PIPE_EQUAL) || (token.kind == R_TOKEN_CARET_EQUAL) ||
                      (token.kind == R_TOKEN_LESS_LESS_EQUAL) ||
                      (token.kind == R_TOKEN_GREATER_GREATER_EQUAL);
        }
        if (assigns && r_ast_ref_child(context, node, 0U, &target) &&
            !r_closure_mark_written(context, scan, target)) {
            return false;
        }
    } else if (view.kind == R_SYNTAX_UNARY_EXPRESSION) {
        RAstRef first;
        if (r_ast_ref_child(context, node, 0U, &first) && r_ast_ref_token(context, first, &token) &&
            ((token.kind == R_TOKEN_PLUS_PLUS) || (token.kind == R_TOKEN_MINUS_MINUS) ||
             (token.kind == R_TOKEN_AMP)) &&
            !r_closure_mark_written(context, scan, node)) {
            return false;
        }
    } else if (view.kind == R_SYNTAX_POSTFIX_EXPRESSION) {
        for (child_index = 0U; child_index < view.child_count; ++child_index) {
            RAstRef child;
            RAstNodeView child_view;
            if (!r_ast_ref_child(context, node, child_index, &child)) {
                continue;
            }
            const bool update =
                r_ast_ref_token(context, child, &token) &&
                ((token.kind == R_TOKEN_PLUS_PLUS) || (token.kind == R_TOKEN_MINUS_MINUS));
            if (update || (r_ast_ref_view(context, child, &child_view) &&
                           (child_view.kind == R_SYNTAX_METHOD_CALL_SUFFIX))) {
                if (!r_closure_mark_written_as(context, scan, node, !update)) {
                    return false;
                }
                break;
            }
        }
    }
    for (child_index = 0U; child_index < view.child_count; ++child_index) {
        RAstRef child;
        if (!r_ast_ref_child(context, node, child_index, &child)) {
            context->resource_status = R_FRONTEND_INTERNAL_ERROR;
            return false;
        }
        if (!r_closure_scan(context, child, depth + 1U, scan)) {
            return false;
        }
    }
    if (scoped) {
        scan->declared.count = declared_mark;
    }
    return true;
}

/* --- environment access inside a lambda body ---------------------------------------------- */

static bool
r_closure_field_index(const RBodyContext *body, uint32_t name_intern_id, uint32_t *global_index) {
    const RSemanticAggregate *aggregate;
    uint32_t index;
    if (body->closure_aggregate == UINT32_C(0)) {
        return false;
    }
    aggregate = &body->frontend->semantic_aggregates[body->closure_aggregate - 1U];
    for (index = 0U; index < aggregate->field_count; ++index) {
        const uint32_t global = aggregate->first_field + index;
        if (body->frontend->semantic_fields[global].name_intern_id == name_intern_id) {
            *global_index = global;
            return true;
        }
    }
    return false;
}

static bool r_body_form_field_place(RBodyContext *body,
                                    const RExpressionResult *base,
                                    uint32_t field_global_index,
                                    RSourceSpan span,
                                    RExpressionResult *result) {
    const RSemanticField *field = &body->frontend->semantic_fields[field_global_index];
    RExpressionResult place = *base;
    RHirNodeId children_storage[1];
    RHirVector children;
    RHirNodeId node_id;
    RHirNode *node;

    children_storage[0] = place.node;
    children.items = children_storage;
    children.count = 1U;
    children.capacity = 1U;
    if (!r_body_append_node(body,
                            R_HIR_FIELD_PLACE,
                            span,
                            field->type,
                            place.symbol,
                            R_TOKEN_INVALID,
                            UINT64_C(0),
                            true,
                            &children,
                            &node_id)) {
        return false;
    }
    node = r_body_hir_node(body, node_id);
    if (node == NULL) {
        body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    node->aggregate_member = field_global_index + UINT32_C(1);
    if (!r_semantic_type_contains_borrow(body->frontend, field->type) &&
        !r_semantic_type_contains_slice(body->frontend, field->type) &&
        !r_generic_type_is_dependent(body->frontend, field->type, 0U) &&
        r_body_place_is_direct_storage_projection(body, place.node)) {
        place.borrow_origin = place.symbol;
        place.borrow_referent = UINT64_C(0);
        place.borrow_origin_set = UINT32_C(0);
        place.borrow_origin_multiple = false;
    }
    node->borrow_origin = place.borrow_origin;
    node->borrow_referent = place.borrow_referent;
    node->borrow_origin_set = place.borrow_origin_set;
    node->borrow_origin_multiple = place.borrow_origin_multiple;
    *result = place;
    result->node = node_id;
    result->type = field->type;
    result->is_place = true;
    result->is_integer_literal_form = false;
    result->is_unsuffixed_integer_literal = false;
    result->valid = true;
    return true;
}

/* R-FUNC-0016: a captured name designates the place it was captured from, through the
 * environment field: the field itself for a value capture, its referent for a borrow. */
static bool r_body_capture_place(RBodyContext *body,
                                 uint32_t field_global_index,
                                 RSourceSpan span,
                                 RExpressionResult *result) {
    const RSemanticType *field_type =
        r_semantic_type(body->frontend, body->frontend->semantic_fields[field_global_index].type);
    const bool field_is_borrow =
        !body->frontend->semantic_fields[field_global_index].is_closure_value_capture &&
        (field_type != NULL) && (field_type->kind == R_SEMANTIC_TYPE_BORROW);
    RExpressionResult env;
    RExpressionResult environment_place;
    RExpressionResult field_place;

    if (!r_body_symbol_place(body, body->closure_env_symbol, span, &env) || !env.valid) {
        r_body_expression_invalid(result);
        return body->frontend->resource_status == R_FRONTEND_OK;
    }
    environment_place = env;
    if ((r_semantic_value_kind(body->frontend, env.type) == R_SEMANTIC_TYPE_BORROW &&
         !r_body_form_deref_place(body, env, span, &environment_place)) ||
        !environment_place.valid ||
        !r_body_form_field_place(
            body, &environment_place, field_global_index, span, &field_place)) {
        r_body_expression_invalid(result);
        return body->frontend->resource_status == R_FRONTEND_OK;
    }
    if (field_is_borrow) {
        return r_body_form_deref_place(body, field_place, span, result);
    }
    *result = field_place;
    return true;
}

static bool r_body_capture_place_by_name(RBodyContext *body,
                                         const RAstTokenView *name,
                                         RExpressionResult *result) {
    uint32_t field_global_index;
    if (!r_closure_field_index(body, name->intern_id, &field_global_index)) {
        body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    return r_body_capture_place(body, field_global_index, name->span, result);
}

/* --- lambda declaration ------------------------------------------------------------------- */

/* The place a name designates in the enclosing body: an active local or parameter, or a
 * capture of the enclosing lambda. */
static bool r_closure_source_place(RBodyContext *outer,
                                   uint32_t name_intern_id,
                                   RSourceSpan span,
                                   bool *found,
                                   RExpressionResult *result) {
    size_t index;
    uint32_t field_global_index;
    *found = false;
    for (index = outer->active_symbols.count; index > outer->symbol_floor; --index) {
        const RSymbolId symbol_id = outer->active_symbols.items[index - 1U];
        const RSemanticSymbol *symbol = r_body_symbol(outer, symbol_id);
        if ((symbol != NULL) && (symbol->name_intern_id == name_intern_id) &&
            ((symbol->kind == R_SEMANTIC_SYMBOL_LOCAL) ||
             (symbol->kind == R_SEMANTIC_SYMBOL_PARAMETER))) {
            *found = true;
            return r_body_symbol_place(outer, symbol_id, span, result);
        }
    }
    if (!outer->module_context &&
        r_closure_field_index(outer, name_intern_id, &field_global_index)) {
        *found = true;
        return r_body_capture_place(outer, field_global_index, span, result);
    }
    return true;
}

static bool r_closure_lambda_parts(const RFrontendContext *context,
                                   RAstRef statement,
                                   RAstRef *return_type,
                                   RAstTokenView *name,
                                   RAstRef *parameter_list,
                                   RAstRef *move_list,
                                   bool *has_move_list) {
    RAstNodeView view;
    uint32_t index;
    bool has_type = false;
    bool has_name = false;
    bool has_parameters = false;
    *has_move_list = false;
    if (!r_ast_ref_view(context, statement, &view)) {
        return false;
    }
    for (index = 0U; index < view.child_count; ++index) {
        RAstRef child;
        RAstNodeView child_view;
        RAstTokenView token;
        if (!r_ast_ref_child(context, statement, index, &child)) {
            return false;
        }
        if (r_ast_ref_token(context, child, &token)) {
            if ((token.kind == R_TOKEN_IDENTIFIER) && !has_name) {
                *name = token;
                has_name = true;
            }
            continue;
        }
        if (!r_ast_ref_view(context, child, &child_view)) {
            return false;
        }
        if ((child_view.kind == R_SYNTAX_TYPE) && !has_type) {
            *return_type = child;
            has_type = true;
        } else if (child_view.kind == R_SYNTAX_PARAMETER_LIST) {
            *parameter_list = child;
            has_parameters = true;
        } else if (child_view.kind == R_SYNTAX_MOVE_CAPTURE_LIST) {
            *move_list = child;
            *has_move_list = true;
        }
    }
    return has_type && has_name && has_parameters;
}

static bool
r_body_lower_lambda(RBodyContext *body, RAstRef statement, size_t scope_base, RHirNodeId *node_id) {
    RFrontendContext *context = body->frontend;
    RAstNodeView view;
    RAstRef return_type_ref;
    RAstRef parameter_list;
    RAstRef move_list = {0};
    RAstTokenView name;
    RClosureScan scan = {{0}, {0}};
    RTypeVector parameters = {0};
    RTypeVector signature_parameters = {0};
    RHirVector field_initializers = {0};
    RProjectedBorrowStateEntryVector projected = {0};
    RSemanticAggregate aggregate;
    RSemanticSymbol function;
    RSemanticImpl impl;
    RExpressionResult initial_value;
    RSymbolId function_id;
    RHirNodeId function_node;
    RTypeId return_type;
    RTypeId effects = R_TYPE_ID_INVALID;
    RTypeId carrier = R_TYPE_ID_INVALID;
    bool effects_supported = true;
    bool asynchronous = false;
    RTypeId environment_type;
    RTypeId receiver_type;
    RTypeId signature;
    RTypeId first_parameter;
    uint32_t aggregate_id;
    uint32_t origin_aggregate_id;
    uint32_t closure_schema = 0U;
    uint32_t trait_id;
    uint32_t capture_count = UINT32_C(0);
    size_t index;
    size_t local_diagnostics;
    bool has_move_list;
    bool exclusive_environment = false;
    RSemanticReceiverKind mode;
    bool parameters_supported = true;
    bool names_poisoned = false;
    bool valid = true;
    bool success = false;
    RSymbolId aggregate_origin = R_SYMBOL_ID_INVALID;
    uint64_t aggregate_referent = UINT64_C(0);
    uint32_t aggregate_origin_set = UINT32_C(0);
    bool aggregate_origin_multiple = false;
    bool aggregate_referent_recorded = false;
    char label[32];

    *node_id = R_HIR_NODE_ID_INVALID;
    r_body_expression_invalid(&initial_value);
    if (!r_ast_ref_view(context, statement, &view) || !r_closure_lambda_parts(context,
                                                                              statement,
                                                                              &return_type_ref,
                                                                              &name,
                                                                              &parameter_list,
                                                                              &move_list,
                                                                              &has_move_list)) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    const uint32_t enclosing_schema = r_generic_schema_at(context, view.span);
    if (enclosing_schema != 0U) {
        /* Share lexical parameter identities, but never overwrite the enclosing definition's
           schema record. Captures and the checked body close through ordinary substitution. */
        RGenericSchema schema = context->generic_schemas[enclosing_schema - 1U];
        schema.aggregate = 0U;
        schema.function = 0U;
        schema.declaration_ast = statement.node;
        schema.span.source = R_SOURCE_ID_INVALID;
        if (!r_grow_array(context,
                          (void **)&context->generic_schemas,
                          &context->generic_schema_capacity,
                          sizeof(schema),
                          context->generic_schema_count + 1U))
            return false;
        context->generic_schemas[context->generic_schema_count++] = schema;
        closure_schema = (uint32_t)context->generic_schema_count;
    }
    mode = r_closure_call_mode(context, statement);
    asynchronous = r_semantic_type_has_direct_token(context, statement, R_TOKEN_KW_ASYNC);
    if (asynchronous && mode != R_SEMANTIC_RECEIVER_VALUE)
        return r_body_diagnostic(body,
                                 "R-DIAG-ASYNC-001",
                                 "R-FUNC-0015",
                                 "an async closure uses once mode and owns its environment",
                                 view.span);
    if (asynchronous &&
        !r_semantic_profile_at_least(context, R_FRONTEND_PROFILE_HOSTED_NATIVE_ASYNC))
        return r_body_diagnostic(body,
                                 R_SEMANTIC_PROFILE_CODE,
                                 R_SEMANTIC_PROFILE_RULE,
                                 "async closures require the hosted-native-async profile",
                                 view.span);
    if (!r_semantic_collect_throws(context, statement, asynchronous, &effects, &effects_supported))
        goto cleanup;
    if (!effects_supported) {
        success = true;
        goto cleanup;
    }
    local_diagnostics = context->diagnostic_count;
    if (!r_semantic_type_from_ast(context, return_type_ref, &return_type)) {
        if (context->resource_status != R_FRONTEND_OK) {
            return false;
        }
        return r_body_report_type_failure(
            body, return_type_ref, view.span, local_diagnostics, "lambda result type");
    }
    if (!r_closure_sync_result(context, return_type))
        return r_body_diagnostic(body,
                                 "R-DIAG-TYPE-001",
                                 "R-FUNC-0015",
                                 r_semantic_value_kind(context, return_type) ==
                                         R_SEMANTIC_TYPE_NEVER
                                     ? "a lambda result cannot be never"
                                     : "unsupported lambda result type",
                                 view.span);
    if (!r_semantic_collect_parameter_types(context,
                                            parameter_list,
                                            &parameters,
                                            !asynchronous,
                                            &parameters_supported,
                                            &names_poisoned))
        goto cleanup;
    if (!parameters_supported || names_poisoned) {
        success = r_body_diagnostic(
            body, "R-DIAG-TYPE-001", "R-FUNC-0015", "unsupported lambda parameter type", view.span);
        goto cleanup;
    }

    /* Captures: every free name that the enclosing body resolves (R-FUNC-0016). */
    if (!r_closure_scan(context, statement, 0U, &scan)) {
        goto cleanup;
    }
    if (has_move_list) {
        RAstNodeView move_view;
        uint32_t child;
        if (!r_ast_ref_view(context, move_list, &move_view)) {
            context->resource_status = R_FRONTEND_INTERNAL_ERROR;
            goto cleanup;
        }
        for (child = 0U; child < move_view.child_count; ++child) {
            RAstRef part;
            RAstTokenView token;
            size_t use;
            bool listed = false;
            if (!r_ast_ref_child(context, move_list, child, &part) ||
                !r_ast_ref_token(context, part, &token) || (token.kind != R_TOKEN_IDENTIFIER)) {
                continue;
            }
            for (use = 0U; use < scan.uses.count; ++use) {
                if (scan.uses.items[use].name_intern_id == token.intern_id) {
                    scan.uses.items[use].moved = true;
                    listed = true;
                }
            }
            if (!listed) {
                if (!r_closure_push_use(context, &scan, &token, false)) {
                    goto cleanup;
                }
                scan.uses.items[scan.uses.count - 1U].moved = true;
            }
        }
    }

    /* The environment aggregate: one field per capture. */
    (void)memset(&aggregate, 0, sizeof(aggregate));
    aggregate.kind = R_SEMANTIC_AGGREGATE_STRUCT;
    aggregate.name_span = name.span;
    aggregate.module_source = body->source;
    aggregate.declaration_ast = statement.node;
    aggregate.first_field = (uint32_t)context->semantic_field_count;
    aggregate.is_protected = true;
    aggregate.is_closure = true;
    aggregate.generic_schema = closure_schema;
    aggregate.complete = true;
    aggregate.is_copy = true;
    aggregate_id = (uint32_t)context->semantic_aggregate_count + UINT32_C(1);
    {
        const int written = snprintf(label, sizeof(label), "$closure%u", (unsigned)name.span.start);
        if ((written <= 0) || ((size_t)written >= sizeof(label)) ||
            !r_closure_intern(context, label, &aggregate.name_intern_id)) {
            goto cleanup;
        }
    }
    if (!r_grow_array(context,
                      (void **)&context->semantic_aggregates,
                      &context->semantic_aggregate_capacity,
                      sizeof(aggregate),
                      context->semantic_aggregate_count + 1U) ||
        !r_semantic_intern_type(context, R_SEMANTIC_TYPE_STRUCT, aggregate_id, &aggregate.type)) {
        goto cleanup;
    }
    environment_type = aggregate.type;
    /* The record exists before its fields, so that every lookup of the environment type
       resolves while the captures are lowered; the field count is written back below. */
    context->semantic_aggregates[context->semantic_aggregate_count++] = aggregate;
    for (index = 0U; index < scan.uses.count; ++index) {
        RClosureUse *use = &scan.uses.items[index];
        RExpressionResult source;
        RSemanticField field;
        RTypeId source_type;
        bool found;
        if (!r_closure_source_place(body, use->name_intern_id, use->span, &found, &source)) {
            goto cleanup;
        }
        if (!found) {
            if (use->moved) {
                valid = false;
                if (!r_body_diagnostic(body,
                                       "R-DIAG-NAME-001",
                                       "R-FUNC-0016",
                                       "a moved capture names a local or parameter of the "
                                       "enclosing body",
                                       use->span)) {
                    goto cleanup;
                }
            }
            continue;
        }
        if (!source.valid) {
            valid = false;
            continue;
        }
        source_type = r_semantic_value_type(context, source.type);
        (void)memset(&field, 0, sizeof(field));
        field.name_span = use->span;
        field.name_intern_id = use->name_intern_id;
        field.aggregate = aggregate_id;
        field.layout_index = capture_count;
        field.is_closure_value_capture = use->moved;
        if (use->moved) {
            field.type = source_type;
        } else if (!r_semantic_intern_derived_type(
                       context,
                       R_SEMANTIC_TYPE_BORROW,
                       source_type,
                       R_TYPE_ID_INVALID,
                       UINT64_C(0),
                       (use->written && mode != R_SEMANTIC_RECEIVER_SHARED)
                           ? R_SEMANTIC_TYPE_FLAG_NONE
                           : R_SEMANTIC_TYPE_FLAG_SHARED,
                       &field.type)) {
            goto cleanup;
        }
        /* R-FUNC-0017: a method call through a moved shared borrow reaches its pointee only
           through a shared view, so it neither writes the capture nor needs exclusivity. */
        if (use->written &&
            !(use->moved && use->receiver_only &&
              r_semantic_value_kind(context, source_type) == R_SEMANTIC_TYPE_BORROW &&
              (r_semantic_type(context, source_type)->flags & R_SEMANTIC_TYPE_FLAG_SHARED) != 0U)) {
            exclusive_environment = true;
        }
        if (!r_semantic_type_is_copy(context, field.type)) {
            aggregate.is_copy = false;
        }
        if (!r_grow_array(context,
                          (void **)&context->semantic_fields,
                          &context->semantic_field_capacity,
                          sizeof(field),
                          context->semantic_field_count + 1U)) {
            goto cleanup;
        }
        context->semantic_fields[context->semantic_field_count++] = field;
        capture_count += 1U;
        /* The field value: a borrow of the source place or the moved value. */
        {
            RExpressionResult value;
            RHirNodeId child_storage[1];
            RHirVector children;
            RHirNodeId field_init_id;
            RHirNode *field_init;
            const uint32_t field_global_index = aggregate.first_field + capture_count - 1U;
            const bool field_carries_borrow =
                !use->moved || r_semantic_type_contains_borrow(context, field.type) ||
                r_semantic_type_contains_slice(context, field.type) ||
                r_semantic_value_kind(context, field.type) == R_SEMANTIC_TYPE_STR;
            if (use->moved) {
                if (!r_body_move_from_place(body, source, use->span, &value)) {
                    goto cleanup;
                }
            } else if (!r_body_form_safe_borrow(body, use->span, field.type, &source, &value)) {
                goto cleanup;
            }
            if (!value.valid) {
                valid = false;
                continue;
            }
            child_storage[0] = value.node;
            children.items = child_storage;
            children.count = 1U;
            children.capacity = 1U;
            if (!r_body_append_node(body,
                                    R_HIR_FIELD_INIT,
                                    use->span,
                                    field.type,
                                    R_SYMBOL_ID_INVALID,
                                    R_TOKEN_EQUAL,
                                    UINT64_C(0),
                                    false,
                                    &children,
                                    &field_init_id)) {
                goto cleanup;
            }
            field_init = r_body_hir_node(body, field_init_id);
            if (field_init == NULL) {
                context->resource_status = R_FRONTEND_INTERNAL_ERROR;
                goto cleanup;
            }
            field_init->aggregate_member = field_global_index + UINT32_C(1);
            field_init->borrow_origin = value.borrow_origin;
            field_init->borrow_referent = value.borrow_referent;
            field_init->borrow_origin_set = value.borrow_origin_set;
            field_init->borrow_origin_multiple = value.borrow_origin_multiple;
            field_init->projected_borrow_state = value.projected_borrow_state;
            if (field_carries_borrow) {
                if (!r_body_append_projected_borrow_field_entry(body,
                                                                environment_type,
                                                                field_global_index + UINT32_C(1),
                                                                field.type,
                                                                &value,
                                                                use->span,
                                                                &projected) ||
                    !r_semantic_merge_borrow_origins(context,
                                                     aggregate_origin,
                                                     aggregate_origin_set,
                                                     aggregate_origin_multiple,
                                                     value.borrow_origin,
                                                     value.borrow_origin_set,
                                                     value.borrow_origin_multiple,
                                                     &aggregate_origin,
                                                     &aggregate_origin_set,
                                                     &aggregate_origin_multiple)) {
                    goto cleanup;
                }
                if (!aggregate_referent_recorded) {
                    aggregate_referent = value.borrow_referent;
                    aggregate_referent_recorded = true;
                } else {
                    aggregate_referent = r_semantic_merge_borrow_referents(aggregate_referent,
                                                                           value.borrow_referent);
                }
            }
            if (!r_hir_vector_push(context, &field_initializers, field_init_id)) {
                goto cleanup;
            }
        }
    }
    aggregate.field_count = capture_count;
    context->semantic_aggregates[aggregate_id - 1U] = aggregate;
    origin_aggregate_id = aggregate_id;
    if (closure_schema != 0U) {
        RTypeVector arguments = {0};
        const RGenericSchema schema = context->generic_schemas[closure_schema - 1U];
        bool applied = true;
        for (uint32_t parameter = 0U; applied && parameter < schema.parameter_count; ++parameter)
            applied = r_type_vector_push(
                context,
                &arguments,
                context->generic_parameters[schema.first_parameter + parameter].type);
        applied = applied &&
                  r_generic_apply_aggregate(context, aggregate_id, &arguments, &environment_type);
        r_context_free(context, arguments.items);
        if (!applied)
            goto cleanup;
        aggregate_id = r_semantic_type(context, environment_type)->base;
        const RSemanticAggregate instance = context->semantic_aggregates[aggregate_id - 1U];
        for (size_t field_index = 0U; field_index < field_initializers.count; ++field_index) {
            RHirNode *initializer = r_body_hir_node(body, field_initializers.items[field_index]);
            initializer->aggregate_member =
                instance.first_field + initializer->aggregate_member - aggregate.first_field;
        }
    }
    if (asynchronous && (!r_closure_async_value(context, environment_type, false) ||
                         !r_closure_async_signature(context, &parameters, return_type, effects))) {
        success = r_body_diagnostic(
            body,
            "R-DIAG-ASYNC-001",
            "R-FUNC-0015",
            "async closure captures, parameters, result and errors shall be Send and unborrowed",
            view.span);
        goto cleanup;
    }

    /* The body: `R call(Env* this | const Env* this, P...)` as a method of the environment. */
    if (mode == R_SEMANTIC_RECEIVER_NONE) {
        mode = exclusive_environment ? R_SEMANTIC_RECEIVER_EXCLUSIVE : R_SEMANTIC_RECEIVER_SHARED;
    }
    receiver_type = environment_type;
    uint32_t resource_flags;
    if (!r_semantic_callable_resources(context, statement, &resource_flags))
        goto cleanup;
    if ((mode != R_SEMANTIC_RECEIVER_VALUE &&
         !r_semantic_intern_derived_type(context,
                                         R_SEMANTIC_TYPE_BORROW,
                                         environment_type,
                                         R_TYPE_ID_INVALID,
                                         UINT64_C(0),
                                         mode == R_SEMANTIC_RECEIVER_SHARED
                                             ? R_SEMANTIC_TYPE_FLAG_SHARED
                                             : R_SEMANTIC_TYPE_FLAG_NONE,
                                         &receiver_type)) ||
        !r_semantic_callable_signature(context,
                                       &parameters,
                                       return_type,
                                       mode,
                                       resource_flags,
                                       effects,
                                       asynchronous,
                                       &signature) ||
        !r_semantic_callable_trait(context, body->source, signature, &trait_id) ||
        !r_type_vector_push(context, &signature_parameters, receiver_type)) {
        goto cleanup;
    }
    for (index = 0U; index < parameters.count; ++index) {
        if (!r_type_vector_push(context, &signature_parameters, parameters.items[index])) {
            goto cleanup;
        }
    }
    (void)memset(&impl, 0, sizeof(impl));
    context->semantic_aggregates[origin_aggregate_id - 1U].closure_signature = signature;
    context->semantic_aggregates[aggregate_id - 1U].closure_signature = signature;
    impl.trait = trait_id;
    impl.target_type = environment_type;
    impl.target_aggregate = aggregate_id;
    impl.module_source = body->source;
    impl.declaration_ast = statement.node;
    impl.span.source = R_SOURCE_ID_INVALID;
    if (!r_grow_array(context,
                      (void **)&context->semantic_impls,
                      &context->semantic_impl_capacity,
                      sizeof(impl),
                      context->semantic_impl_count + 1U)) {
        goto cleanup;
    }
    context->semantic_impls[context->semantic_impl_count++] = impl;
    (void)memset(&function, 0, sizeof(function));
    function.kind = R_SEMANTIC_SYMBOL_FUNCTION;
    function.name_span = name.span;
    function.scope_start = view.span.end;
    function.module_source = body->source;
    function.return_type = return_type;
    function.throws_type = effects;
    if (effects != R_TYPE_ID_INVALID &&
        !r_semantic_intern_effect_carrier(context, return_type, effects, &carrier))
        goto cleanup;
    function.effect_carrier_type = carrier;
    function.is_async = asynchronous;
    if (asynchronous &&
        !r_semantic_checked_async_types(
            context, return_type, effects, &function.async_task_type, &function.async_start_type))
        goto cleanup;
    function.parameter_count = (uint32_t)signature_parameters.count;
    function.declaration_ast = statement.node;
    function.associated_type = environment_type;
    function.receiver_kind = mode;
    function.trait = trait_id;
    function.impl = (uint32_t)context->semantic_impl_count;
    function.closure_aggregate = aggregate_id;
    function.generic_schema = closure_schema;
    function.is_method = true;
    function.is_protected = true;
    function.has_definition = true;
    function.signature_supported = true;
    function.is_noalloc = (resource_flags & R_SEMANTIC_TYPE_FLAG_NOALLOC) != 0U;
    function.is_nonblocking = (resource_flags & R_SEMANTIC_TYPE_FLAG_NONBLOCKING) != 0U;
    function.object_state = R_SEMANTIC_OBJECT_STATE_NOT_APPLICABLE;
    if (!r_semantic_commit_parameter_types(context, &signature_parameters, &first_parameter) ||
        !r_closure_intern(context, "call", &function.method_name_intern_id) ||
        !r_generic_method_symbol_name(
            context, environment_type, function.method_name_intern_id, &function.name_intern_id)) {
        goto cleanup;
    }
    function.first_parameter_type = first_parameter;
    if (!r_semantic_append_symbol(context, function, &function_id) ||
        !r_semantic_register_method(context, function_id) ||
        !r_hir_build_function(
            context, function_id, body->module_bindings_incomplete, &function_node)) {
        goto cleanup;
    }
    if (context->semantic_symbols[function_id - 1U].poisoned) {
        valid = false;
    }

    /* The declaration: `Env name = Env { captures };` with the provenance of its borrows. */
    if (valid) {
        RHirNodeId init_id;
        uint32_t projected_state;
        if (!r_body_append_projected_borrow_state(body, &projected, &projected_state) ||
            !r_body_append_node(body,
                                R_HIR_AGGREGATE_INIT,
                                view.span,
                                environment_type,
                                R_SYMBOL_ID_INVALID,
                                R_TOKEN_INVALID,
                                UINT64_C(0),
                                false,
                                &field_initializers,
                                &init_id)) {
            goto cleanup;
        }
        initial_value.node = init_id;
        initial_value.type = environment_type;
        initial_value.borrow_origin = aggregate_origin;
        initial_value.borrow_referent = aggregate_referent;
        initial_value.borrow_origin_set = aggregate_origin_set;
        initial_value.borrow_origin_multiple = aggregate_origin_multiple;
        initial_value.projected_borrow_state = projected_state;
        initial_value.valid = true;
        r_body_hir_node(body, init_id)->borrow_origin = aggregate_origin;
        r_body_hir_node(body, init_id)->borrow_referent = aggregate_referent;
        r_body_hir_node(body, init_id)->borrow_origin_set = aggregate_origin_set;
        r_body_hir_node(body, init_id)->borrow_origin_multiple = aggregate_origin_multiple;
        r_body_hir_node(body, init_id)->projected_borrow_state = projected_state;
    }
    success = r_body_declare_local(body,
                                   scope_base,
                                   name,
                                   environment_type,
                                   statement.node,
                                   false,
                                   false,
                                   view.span,
                                   &initial_value,
                                   false,
                                   false,
                                   node_id);
cleanup:
    r_context_free(context, scan.uses.items);
    r_context_free(context, scan.declared.items);
    r_context_free(context, parameters.items);
    r_context_free(context, signature_parameters.items);
    r_context_free(context, field_initializers.items);
    r_context_free(context, projected.items);
    return success;
}

/* --- closure calls ------------------------------------------------------------------------ */

#include "function_items.inc"

static bool r_semantic_type_is_closure_environment(const RFrontendContext *context,
                                                   RTypeId type_id) {
    const RSemanticAggregate *aggregate =
        r_semantic_aggregate_for_type(context, r_semantic_value_type(context, type_id));
    return (aggregate != NULL) && aggregate->is_closure;
}

/* R-FUNC-0017: whether a callee of this type is a closure or a constrained parameter. */
static bool r_semantic_type_is_callable_owner(const RFrontendContext *context, RTypeId type_id) {
    const RSemanticType *type = r_semantic_type(context, r_semantic_value_type(context, type_id));
    const RSemanticAggregate *aggregate;
    if (type == NULL) {
        return false;
    }
    if (type->kind == R_SEMANTIC_TYPE_PARAMETER) {
        return true;
    }
    /* R-TYPE-0054 (L28): a function value is called through `call` like a closure. */
    if (type->kind == R_SEMANTIC_TYPE_FUNCTION) {
        return true;
    }
    aggregate = r_semantic_aggregate_for_type(context, r_semantic_value_type(context, type_id));
    return (aggregate != NULL) && aggregate->is_closure;
}

static bool r_semantic_type_is_callable_callee(const RFrontendContext *context, RTypeId type_id) {
    const RSemanticType *type = r_semantic_type(context, r_semantic_value_type(context, type_id));
    if (type == NULL) {
        return false;
    }
    /* A borrow capture of a borrowed callable is a borrow of a borrow (R-FUNC-0017). */
    if (type->kind == R_SEMANTIC_TYPE_BORROW) {
        const RSemanticType *inner =
            r_semantic_type(context, r_semantic_value_type(context, type->base));
        if (inner != NULL && inner->kind == R_SEMANTIC_TYPE_BORROW)
            return r_semantic_type_is_callable_owner(context, inner->base);
        return r_semantic_type_is_callable_owner(context, type->base);
    }
    return r_semantic_type_is_callable_owner(context, type_id);
}

/* R-FUNC-0017, R-TYPE-0054: the call of a lowered callee that is a closure, a constrained
   parameter or a function value, through its `call` method. */
static bool r_body_call_callable(RBodyContext *body,
                                 RAstRef postfix,
                                 RAstRef call_suffix,
                                 const RExpressionResult *callee_value,
                                 const RAstTokenView *callee_name,
                                 RExpressionResult *result) {
    RFrontendContext *context = body->frontend;
    RExpressionResult callee = *callee_value;
    RAstNodeView postfix_view;
    RAstTokenView call_name;
    const RSemanticType *type;
    bool arrow;

    r_body_expression_invalid(result);
    if (!r_ast_ref_view(context, postfix, &postfix_view)) {
        return false;
    }
    type = r_semantic_type(context, r_semantic_value_type(context, callee.type));
    arrow = (type != NULL) && (type->kind == R_SEMANTIC_TYPE_BORROW);
    call_name = *callee_name;
    call_name.kind = R_TOKEN_IDENTIFIER;
    call_name.token = R_TOKEN_ID_INVALID;
    if (!r_closure_intern(context, "call", &call_name.intern_id)) {
        return false;
    }
    return r_body_lower_method_call_named(body,
                                          postfix,
                                          call_suffix,
                                          &callee,
                                          arrow,
                                          &call_name,
                                          false,
                                          postfix_view.span,
                                          R_TYPE_ID_INVALID,
                                          result);
}

static bool r_body_lower_closure_call(RBodyContext *body,
                                      RAstRef postfix,
                                      RAstRef primary,
                                      RAstRef call_suffix,
                                      const RAstTokenView *callee_name,
                                      RExpressionResult *result) {
    RExpressionResult callee;
    r_body_expression_invalid(result);
    if (!r_body_lower_primary(body, primary, R_TYPE_ID_INVALID, &callee)) {
        return false;
    }
    if (!callee.valid) {
        return true;
    }
    return r_body_call_callable(body, postfix, call_suffix, &callee, callee_name, result);
}

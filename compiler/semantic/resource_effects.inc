/* R-FUNC-0019: prove resource contracts over calls and all possible cleanup paths. */
static bool r_resource_validate_attribute_positions(RFrontendContext *context) {
    size_t source_index;
    for (source_index = 0U; source_index < context->source_count; ++source_index) {
        RAstRef root;
        RTypeVector pending = {0};
        bool success = false;
        if (!r_semantic_root_ref(context, (RSourceId)source_index + 1U, &root) ||
            !r_type_vector_push(context, &pending, root.node)) {
            r_context_free(context, pending.items);
            return false;
        }
        while (pending.count != 0U) {
            RAstRef parent = {root.source, pending.items[--pending.count]};
            RAstNodeView view;
            RAstRef declaration;
            uint32_t child_index;
            bool allowed;
            if (!r_ast_ref_view(context, parent, &view)) {
                goto cleanup;
            }
            for (child_index = 0U; child_index < view.child_count; ++child_index) {
                RAstRef child;
                if (!r_ast_ref_child(context, parent, child_index, &child) ||
                    !r_type_vector_push(context, &pending, child.node))
                    goto cleanup;
            }
            allowed =
                view.kind == R_SYNTAX_CALLABLE_CONSTRAINT ||
                view.kind == R_SYNTAX_LAMBDA_DECLARATION ||
                (view.kind == R_SYNTAX_TYPE &&
                 r_semantic_type_has_direct_token(context, parent, R_TOKEN_KW_FN)) ||
                (view.kind == R_SYNTAX_EXTERNAL_DECLARATION &&
                 (r_ast_find_direct_child(
                      context, parent, R_SYNTAX_FUNCTION_DECLARATION, &declaration) ||
                  r_ast_find_direct_child(
                      context, parent, R_SYNTAX_DROP_DEFINITION, &declaration))) ||
                (view.kind == R_SYNTAX_C_DECLARATION &&
                 r_ast_find_direct_child(context, parent, R_SYNTAX_PARAMETER_LIST, &declaration));
            if (allowed) {
                continue;
            }
            for (child_index = 0U; child_index < view.child_count; ++child_index) {
                RAstRef child;
                RAstNodeView attribute;
                RAstTokenView name;
                if (r_ast_ref_child(context, parent, child_index, &child) &&
                    r_ast_ref_view(context, child, &attribute) &&
                    attribute.kind == R_SYNTAX_ATTRIBUTE &&
                    r_ast_direct_token(context, child, 1U, NULL, &name) &&
                    (r_semantic_token_text_equal_owned(context, &name, "noalloc") ||
                     r_semantic_token_text_equal_owned(context, &name, "nonblocking")) &&
                    !r_semantic_add_diagnostic(
                        context,
                        "R-DIAG-SYN-002",
                        "R-GRAM-0007",
                        "resource attributes require a function or drop declaration",
                        attribute.span)) {
                    goto cleanup;
                }
            }
        }
        success = true;
    cleanup:
        r_context_free(context, pending.items);
        if (!success)
            return false;
    }
    return true;
}

typedef struct RResourceCheck {
    RFrontendContext *context;
    uint8_t *functions;
    uint8_t *types;
    RSymbolId *chain;
    size_t chain_count;
    const char *reason;
    RSourceSpan span;
    bool nonblocking;
} RResourceCheck;

static bool r_resource_function(RResourceCheck *, RSymbolId, RSourceSpan);
static bool r_resource_node(RResourceCheck *, RHirNodeId, uint32_t);

static bool r_resource_reject(RResourceCheck *check, RSourceSpan span, const char *reason) {
    check->span = span;
    check->reason = reason;
    return false;
}

static bool
r_resource_drop(RResourceCheck *check, RTypeId type_id, RSourceSpan span, uint32_t depth) {
    RFrontendContext *context = check->context;
    const RSemanticType *type;
    uint32_t index;
    type_id = r_semantic_value_type(context, type_id);
    type = r_semantic_type(context, type_id);
    if (type == NULL || r_semantic_type_is_copy(context, type_id)) {
        return true;
    }
    if (depth > context->options.limits.max_nesting) {
        return r_resource_reject(check, span, "cleanup exceeds the resource proof depth limit");
    }
    if (check->types[type_id] != 0U) {
        return true;
    }
    check->types[type_id] = 1U;
    switch (type->kind) {
    case R_SEMANTIC_TYPE_PARAMETER: {
        const RGenericParameter *parameter = &context->generic_parameters[type->base - 1U];
        const uint32_t flag =
            check->nonblocking ? R_SEMANTIC_TYPE_FLAG_NONBLOCKING : R_SEMANTIC_TYPE_FLAG_NOALLOC;
        for (index = 0U; index < parameter->trait_constraint_count; ++index) {
            const uint32_t trait =
                context->generic_trait_constraints[parameter->first_trait_constraint + index];
            const RSemanticType *signature =
                r_semantic_type(context, context->semantic_traits[trait - 1U].callable_signature);
            if (signature != NULL && (signature->flags & flag) != 0U)
                return true;
        }
        return r_resource_reject(check, span, "generic cleanup has no resource contract");
    }
    case R_SEMANTIC_TYPE_VOID:
    case R_SEMANTIC_TYPE_NEVER:
    case R_SEMANTIC_TYPE_BORROW:
    case R_SEMANTIC_TYPE_SLICE:
    case R_SEMANTIC_TYPE_STR:
    case R_SEMANTIC_TYPE_ATOMIC:
        return true;
    case R_SEMANTIC_TYPE_OWN:
    case R_SEMANTIC_TYPE_ARC:
    case R_SEMANTIC_TYPE_RC:
    case R_SEMANTIC_TYPE_WEAK:
    case R_SEMANTIC_TYPE_ARRAY:
    case R_SEMANTIC_TYPE_LIST:
    case R_SEMANTIC_TYPE_DICT:
        if (check->nonblocking) {
            return r_resource_reject(
                check, span, "owner cleanup may enter the allocator or synchronization runtime");
        }
        return r_resource_drop(check, type->base, span, depth + 1U) &&
               (type->kind != R_SEMANTIC_TYPE_DICT ||
                r_resource_drop(check, type->second, span, depth + 1U));
    case R_SEMANTIC_TYPE_OPTION:
    case R_SEMANTIC_TYPE_FIXED_ARRAY:
        return r_resource_drop(check, type->base, span, depth + 1U);
    case R_SEMANTIC_TYPE_RESULT:
    case R_SEMANTIC_TYPE_EFFECT_SET:
    case R_SEMANTIC_TYPE_EFFECT_CARRIER:
        return r_resource_drop(check, type->base, span, depth + 1U) &&
               r_resource_drop(check, type->second, span, depth + 1U);
    case R_SEMANTIC_TYPE_STRUCT:
    case R_SEMANTIC_TYPE_ENUM: {
        const RSemanticAggregate *aggregate = r_stack_aggregate(context, (uint32_t)type->base);
        if (aggregate == NULL) {
            return r_resource_reject(check, span, "aggregate cleanup has no definition");
        }
        if (aggregate->drop_function != 0U &&
            !r_resource_function(check, aggregate->drop_function, span)) {
            return false;
        }
        for (index = 0U; index < aggregate->field_count; ++index) {
            if (!r_resource_drop(check,
                                 context->semantic_fields[aggregate->first_field + index].type,
                                 span,
                                 depth + 1U)) {
                return false;
            }
        }
        for (index = 0U; index < aggregate->variant_count; ++index) {
            if (!r_resource_drop(
                    check,
                    context->semantic_variants[aggregate->first_variant + index].payload_type,
                    span,
                    depth + 1U)) {
                return false;
            }
        }
        return true;
    }
    case R_SEMANTIC_TYPE_STANDARD:
        if (r_semantic_standard_type_name_equal(context, type, "std.list::iter") ||
            r_semantic_standard_type_name_equal(context, type, "std.dict::iter")) {
            /* Iterators carry source borrows; they do not own their element types. */
            return true;
        }
        /* These destructors release storage and never allocate. Type arguments are
           still visited: returning an unaccepted owner does not erase its drop hook. */
        if (!check->nonblocking &&
            (r_semantic_standard_type_name_equal(context, type, "std.string::string") ||
             r_semantic_standard_type_name_equal(context, type, "std.format::builder") ||
             r_semantic_standard_type_name_equal(context, type, "std.alloc::new_error") ||
             r_semantic_standard_type_name_equal(context, type, "std.array::push_error") ||
             r_semantic_standard_type_name_equal(context, type, "std.list::push_error") ||
             r_semantic_standard_type_name_equal(context, type, "std.dict::insert_error"))) {
            return r_resource_drop(check, type->base, span, depth + 1U) &&
                   r_resource_drop(check, type->second, span, depth + 1U);
        }
        break;
    default:
        break;
    }
    return r_resource_reject(check, span, "cleanup has no proven resource contract");
}

static bool
r_resource_default(RResourceCheck *check, RTypeId type_id, RSourceSpan span, uint32_t depth) {
    const RSemanticType *type;
    uint32_t index;
    type_id = r_semantic_value_type(check->context, type_id);
    type = r_semantic_type(check->context, type_id);
    if (type == NULL || depth > check->context->options.limits.max_nesting) {
        return r_resource_reject(check, span, "default initialization has no resource proof");
    }
    switch (type->kind) {
    case R_SEMANTIC_TYPE_ARRAY:
        if (r_semantic_value_kind(check->context, type->base) == R_SEMANTIC_TYPE_U8) {
            return true;
        }
        return r_resource_reject(check, span, "container default has no resource proof");
    case R_SEMANTIC_TYPE_OPTION:
        return true;
    case R_SEMANTIC_TYPE_OWN:
    case R_SEMANTIC_TYPE_RC:
    case R_SEMANTIC_TYPE_ARC:
        if ((type->flags & R_SEMANTIC_TYPE_FLAG_NULLABLE) != 0U) {
            return true;
        }
        return r_resource_reject(check, span, "default initialization allocates owner storage");
    case R_SEMANTIC_TYPE_FIXED_ARRAY:
        return r_resource_default(check, type->base, span, depth + 1U);
    case R_SEMANTIC_TYPE_STRUCT: {
        const RSemanticAggregate *aggregate =
            r_stack_aggregate(check->context, (uint32_t)type->base);
        if (aggregate == NULL) {
            return r_resource_reject(check, span, "default aggregate has no definition");
        }
        for (index = 0U; index < aggregate->field_count; ++index) {
            if (!r_resource_default(
                    check,
                    check->context->semantic_fields[aggregate->first_field + index].type,
                    span,
                    depth + 1U)) {
                return false;
            }
        }
        return true;
    }
    case R_SEMANTIC_TYPE_ENUM:
        if (!r_semantic_type_is_copy(check->context, type_id)) {
            return r_resource_reject(check, span, "default variant has no resource proof");
        }
        return true;
    case R_SEMANTIC_TYPE_STANDARD:
        if (!r_semantic_type_is_copy(check->context, type_id)) {
            return r_resource_reject(
                check, span, "standard default initialization has no resource proof");
        }
        return true;
    default:
        return (r_semantic_type_is_copy(check->context, type_id) &&
                !r_generic_type_is_dependent(check->context, type_id, 0U)) ||
               r_resource_reject(
                   check, span, "dependent default initialization has no resource proof");
    }
}

/* Positive registry only. A newly added operation is unknown until its runtime and
   generated glue have been audited. Operations with callbacks need a separate proof. */
static bool r_resource_standard_proven(RStandardCallOperation operation, bool nonblocking) {
    switch (operation) {
    case R_STANDARD_CALL_CORE_PANIC:
    case R_STANDARD_CALL_CORE_ASSUME:
    case R_STANDARD_CALL_CORE_SLICE_FROM_RAW_PARTS:
    case R_STANDARD_CALL_CORE_SLICE_FROM_RAW_PARTS_MUT:
    case R_STANDARD_CALL_CORE_CHECKED_ADD:
    case R_STANDARD_CALL_CORE_CHECKED_SUB:
    case R_STANDARD_CALL_CORE_CHECKED_MUL:
    case R_STANDARD_CALL_CORE_WRAPPING_ADD:
    case R_STANDARD_CALL_CORE_WRAPPING_SUB:
    case R_STANDARD_CALL_CORE_WRAPPING_MUL:
    case R_STANDARD_CALL_CORE_SATURATING_ADD:
    case R_STANDARD_CALL_CORE_SATURATING_SUB:
    case R_STANDARD_CALL_CORE_SATURATING_MUL:
    case R_STANDARD_CALL_CORE_ENUM_NAME:
    case R_STANDARD_CALL_CORE_ENUM_ORDINAL:
    case R_STANDARD_CALL_CORE_ENUM_AT:
    case R_STANDARD_CALL_CORE_ENUM_FROM_NAME:
    case R_STANDARD_CALL_CORE_VARIANT_NAME:
    case R_STANDARD_CALL_CORE_TARGET_NAME:
    case R_STANDARD_CALL_CORE_PROFILE_NAME:
    case R_STANDARD_CALL_CORE_LOCATION:
    case R_STANDARD_CALL_CORE_TYPE_ATTRIBUTE:
    case R_STANDARD_CALL_CORE_FIELD_ATTRIBUTE:
    case R_STANDARD_CALL_CORE_VARIANT_ATTRIBUTE:
    case R_STANDARD_CALL_CORE_FIELD_NAME_AT:
    case R_STANDARD_CALL_CORE_REFLECT_ENUM_COUNT:
    case R_STANDARD_CALL_CORE_REFLECT_ENUM_MIN:
    case R_STANDARD_CALL_CORE_REFLECT_ENUM_MAX:
    case R_STANDARD_CALL_CORE_REFLECT_VARIANT_COUNT:
    case R_STANDARD_CALL_CORE_REFLECT_TYPE_NAME:
    case R_STANDARD_CALL_CORE_REFLECT_FIELD_COUNT:
    case R_STANDARD_CALL_CORE_REFLECT_FIELD_NAME:
    case R_STANDARD_CALL_ARRAY_CAPACITY:
    case R_STANDARD_CALL_ARRAY_GET:
    case R_STANDARD_CALL_ARRAY_GET_MUT:
    case R_STANDARD_CALL_LIST_FRONT:
    case R_STANDARD_CALL_LIST_BACK:
    case R_STANDARD_CALL_LIST_FRONT_MUT:
    case R_STANDARD_CALL_LIST_BACK_MUT:
    case R_STANDARD_CALL_LIST_GET:
    case R_STANDARD_CALL_LIST_GET_MUT:
    case R_STANDARD_CALL_LIST_ITER:
    case R_STANDARD_CALL_LIST_NEXT:
    case R_STANDARD_CALL_DICT_ITER:
    case R_STANDARD_CALL_DICT_NEXT:
    case R_STANDARD_CALL_BYTES_COPY_WITHIN:
    case R_STANDARD_CALL_BYTES_FILL:
    case R_STANDARD_CALL_BYTES_FIND:
    case R_STANDARD_CALL_BYTES_FIND_SLICE:
    case R_STANDARD_CALL_BYTES_STARTS_WITH:
    case R_STANDARD_CALL_BYTES_ENDS_WITH:
    case R_STANDARD_CALL_BYTES_EQUAL:
    case R_STANDARD_CALL_BYTES_COMPARE:
    case R_STANDARD_CALL_HASH_CRC32:
    case R_STANDARD_CALL_HASH_MD5:
    case R_STANDARD_CALL_HASH_SHA1:
    case R_STANDARD_CALL_HASH_SHA256:
    case R_STANDARD_CALL_HASH_SHA512:
    case R_STANDARD_CALL_UTF8_IS_VALID:
    case R_STANDARD_CALL_UTF8_VALIDATE:
    case R_STANDARD_CALL_CORE_RECURSION_ENTER:
    case R_STANDARD_CALL_CORE_RECURSION_LEAVE:
    case R_STANDARD_CALL_BITS_READ:
    case R_STANDARD_CALL_BITS_ALIGN_BYTE:
    case R_STANDARD_CALL_FORMAT_AS_STR:
    case R_STANDARD_CALL_FORMAT_CLEAR:
    case R_STANDARD_CALL_STRING_AS_STR:
    case R_STANDARD_CALL_STRING_AS_BYTES:
    case R_STANDARD_CALL_STRING_LEN:
    case R_STANDARD_CALL_STRING_CAPACITY:
    case R_STANDARD_CALL_STRING_CLEAR:
    case R_STANDARD_CALL_STRING_TRUNCATE:
    case R_STANDARD_CALL_CONVERT_CHECKED:
    case R_STANDARD_CALL_C_CHECKED:
    case R_STANDARD_CALL_C_TARGET:
    case R_STANDARD_CALL_C_LINK_AVAILABLE:
    /* R-SLIB-ASYNC-0018: a read of the running task, without allocation or waiting. */
    case R_STANDARD_CALL_ASYNC_TASK_ID:
    /* R-SLIB-TEST-0003: a read or a setting of the allocator's attempt counter. */
    case R_STANDARD_CALL_TEST_ALLOCATION_ATTEMPTS:
    case R_STANDARD_CALL_TEST_FAIL_ALLOCATION_AT:
        return true;
    case R_STANDARD_CALL_CORE_ATOMIC_LOAD:
    case R_STANDARD_CALL_CORE_ATOMIC_STORE:
    case R_STANDARD_CALL_CORE_ATOMIC_EXCHANGE:
    case R_STANDARD_CALL_CORE_ATOMIC_COMPARE_EXCHANGE:
    case R_STANDARD_CALL_CORE_ATOMIC_FETCH_ADD:
    case R_STANDARD_CALL_CORE_ATOMIC_FETCH_SUB:
    case R_STANDARD_CALL_CORE_ATOMIC_FETCH_AND:
    case R_STANDARD_CALL_CORE_ATOMIC_FETCH_OR:
    case R_STANDARD_CALL_CORE_ATOMIC_FETCH_XOR:
    case R_STANDARD_CALL_CORE_ATOMIC_IS_LOCK_FREE:
        /* Atomic does not imply a lock-free implementation on every R target. */
        return !nonblocking;
    default:
        return false;
    }
}

static bool r_resource_node(RResourceCheck *check, RHirNodeId node_id, uint32_t depth) {
    RFrontendContext *context = check->context;
    const RHirNode *node;
    uint32_t index;
    if (node_id == 0U || node_id > context->hir_node_count ||
        depth > r_frontend_tree_depth_limit(context)) {
        RSourceSpan span = {0};
        return r_resource_reject(check, span, "body exceeds the resource proof limits");
    }
    node = &context->hir_nodes[node_id - 1U];
    switch (node->kind) {
    case R_HIR_NEW:
        return r_resource_reject(check, node->span, "new allocates heap storage");
    case R_HIR_ASYNC_START:
        return r_resource_reject(
            check, node->span, "async launch allocates a frame and enters the executor");
    case R_HIR_TASK_SCOPE_ENTER:
    case R_HIR_TASK_SCOPE_WAIT:
    case R_HIR_TASK_SCOPE_CLOSE:
    case R_HIR_AWAIT:
        return r_resource_reject(
            check, node->span, "await registration and cancellation have no resource proof");
    case R_HIR_DISCARD:
        if (node->child_count != 1U ||
            !r_resource_drop(check,
                             context->hir_nodes[context->hir_children[node->first_child] - 1U].type,
                             node->span,
                             0U))
            return false;
        break;
    case R_HIR_INDIRECT_CALL: {
        const RHirNode *callee =
            node->child_count == 0U
                ? NULL
                : &context->hir_nodes[context->hir_children[node->first_child] - 1U];
        /* Named C calls share the raw-call HIR, but retain an exact function address. */
        if (callee != NULL && callee->kind == R_HIR_FUNCTION_ADDRESS) {
            if (!r_resource_function(check, callee->symbol, node->span)) {
                return false;
            }
            break;
        }
        const RSemanticType *signature =
            callee == NULL ? NULL
                           : r_semantic_type(context, r_semantic_value_type(context, callee->type));
        const uint32_t flag =
            check->nonblocking ? R_SEMANTIC_TYPE_FLAG_NONBLOCKING : R_SEMANTIC_TYPE_FLAG_NOALLOC;
        if (signature != NULL && signature->kind == R_SEMANTIC_TYPE_RAW_FUNCTION &&
            (signature->flags & flag) != 0U)
            break;
        return r_resource_reject(
            check, node->span, "raw fn signature lacks the required resource contract");
    }
    case R_HIR_CALL:
        if (!r_resource_function(check, node->symbol, node->span)) {
            return false;
        }
        break;
    case R_HIR_STANDARD_CALL:
        if (node->standard_operation == R_STANDARD_CALL_CORE_CLONE) {
            /* R-OWN-0020: a Copy clone is a read; any other clone allocates its copy and
               may call clone hooks, and the allocator has no nonblocking proof. */
            return r_resource_reject(check,
                                     node->span,
                                     check->nonblocking
                                         ? "clone allocation has no nonblocking proof"
                                         : "a clone of a Move value allocates");
        }
        if ((node->standard_operation == R_STANDARD_CALL_CORE_FORMAT_RENDER) ||
            (node->standard_operation == R_STANDARD_CALL_CORE_FORMAT_APPEND)) {
            /* R-TYPE-0046 (L32): formatting grows a builder and may call implementations. */
            return r_resource_reject(check,
                                     node->span,
                                     check->nonblocking
                                         ? "formatting allocation has no nonblocking proof"
                                         : "formatting allocates its text");
        }
        if (node->standard_operation == R_STANDARD_CALL_CORE_REPLACE ||
            node->standard_operation == R_STANDARD_CALL_CORE_TAKE ||
            node->standard_operation == R_STANDARD_CALL_CORE_SWAP) {
            /* The exchange itself neither allocates nor invokes drop. Copy glue
               has no atomic components; other move glue needs a separate proof. */
            if (check->nonblocking && !r_semantic_type_is_copy(context, node->type)) {
                return r_resource_reject(
                    check, node->span, "replacement move glue has no nonblocking proof");
            }
            break;
        }
        if (!r_resource_standard_proven(node->standard_operation, check->nonblocking)) {
            return r_resource_reject(
                check, node->span, "standard operation has no proven resource contract");
        }
        break;
    case R_HIR_DEFAULT_VALUE:
        if (!r_resource_default(check, node->type, node->span, 0U)) {
            return false;
        }
        break;
    case R_HIR_ARRAY_INIT: {
        const RSemanticType *type = r_semantic_type(context, node->type);
        if (type != NULL && type->kind == R_SEMANTIC_TYPE_FIXED_ARRAY &&
            node->child_count < type->length &&
            !r_resource_default(check, type->base, node->span, 0U)) {
            return false;
        }
        break;
    }
    default:
        break;
    }
    /* Include both normal and exceptional cleanup, even for values moved onward.
       This is deliberately conservative and does not infer path-sensitive promises. */
    if (node->kind != R_HIR_FUNCTION && r_stack_node_owns_value(node->kind) &&
        (!r_resource_drop(check, node->type, node->span, 0U) ||
         !r_resource_drop(check, node->auxiliary_type, node->span, 0U))) {
        return false;
    }
    if (node->kind == R_HIR_ASSIGN && node->replaces_initialized && node->child_count != 0U) {
        const RHirNode *place = &context->hir_nodes[context->hir_children[node->first_child] - 1U];
        if (!r_resource_drop(check, place->type, node->span, 0U)) {
            return false;
        }
    }
    for (index = 0U; index < node->child_count; ++index) {
        if (!r_resource_node(check, context->hir_children[node->first_child + index], depth + 1U)) {
            return false;
        }
    }
    for (index = 0U; index < node->effect_exit_count; ++index) {
        if (!r_resource_node(
                check,
                context->hir_effect_exits[node->first_effect_exit + index].cleanup_block,
                depth + 1U)) {
            return false;
        }
    }
    return true;
}

static bool r_resource_function(RResourceCheck *check, RSymbolId function_id, RSourceSpan span) {
    RFrontendContext *context = check->context;
    const RSemanticSymbol *function;
    if (function_id == 0U || function_id > context->semantic_symbol_count) {
        return r_resource_reject(check, span, "callee has no resource contract");
    }
    function = &context->semantic_symbols[function_id - 1U];
    if (function->hir_node == 0U && function->generic_origin != 0U &&
        function->generic_schema != 0U) {
        const RGenericSchema *schema = &context->generic_schemas[function->generic_schema - 1U];
        for (uint32_t argument = 0U; argument < schema->parameter_count; ++argument) {
            if (r_generic_type_is_dependent(
                    context,
                    context->generic_arguments[function->first_generic_argument + argument],
                    0U)) {
                /* An open application has no executable clone yet. Prove its definition
                   with the schema's constraints; never trust an unchecked annotation. */
                return r_resource_function(check, function->generic_origin, span);
            }
        }
    }
    if (check->functions[function_id] == 2U) {
        return true;
    }
    if ((check->functions[function_id] == 1U) && (function->recursion_depth != 0U)) {
        /* L35-1 (R-FUNC-0026): a call back into a function with @recursion whose body is being
           proved adds nothing that body does not already show; the outer proof decides. */
        return true;
    }
    if (check->functions[function_id] == 1U ||
        check->chain_count >= context->options.limits.max_nesting) {
        return r_resource_reject(check, span, "call chain exceeds the resource proof limits");
    }
    check->functions[function_id] = 1U;
    check->chain[check->chain_count++] = function_id;
    if (r_dyn_dispatcher(context, function_id)) {
        /* R-TYPE-0051: a dispatching call may reach the implementation of every member. */
        for (size_t entry = 0U; entry < context->dyn_target_count; ++entry) {
            if (context->dyn_targets[entry].dispatcher == function_id &&
                !r_resource_function(check, context->dyn_targets[entry].target, span)) {
                return false;
            }
        }
    } else if (r_function_value_dispatcher(context, function_id)) {
        /* R-TYPE-0054 (L28): a call through a function value is proved by the guarantee its
           type promises; every conversion to the type proved it for the function. */
        if (!(check->nonblocking ? function->is_nonblocking : function->is_noalloc)) {
            return r_resource_reject(check,
                                     span,
                                     check->nonblocking
                                         ? "the function type does not promise @nonblocking"
                                         : "the function type does not promise @noalloc");
        }
    } else if (function->is_import ||
        (function->is_synthetic && function->is_trait_prototype && function->trait != 0U &&
         context->semantic_traits[function->trait - 1U].callable_signature != 0U)) {
        if (!(check->nonblocking ? function->is_nonblocking : function->is_noalloc)) {
            return r_resource_reject(check, span, "callee has no corresponding resource contract");
        }
    } else {
        if (function->is_extern_c && context->profile != R_FRONTEND_PROFILE_FREESTANDING) {
            size_t index;
            if (check->nonblocking) {
                return r_resource_reject(
                    check, span, "C entry attachment and detachment may acquire runtime locks");
            }
            /* A foreign caller may own a temporary attachment. Its exit runs all
               initialized R thread-local destructors before restoring the C environment. */
            for (index = 0U; index < context->semantic_symbol_count; ++index) {
                const RSemanticSymbol *local = &context->semantic_symbols[index];
                if (local->is_thread_local && !local->is_import &&
                    !r_resource_drop(check, local->type, span, 0U)) {
                    return false;
                }
            }
        }
        if (check->nonblocking && function->is_async) {
            return r_resource_reject(
                check, span, "async frame cleanup enters the allocator and executor");
        }
        if (function->hir_node == 0U) {
            return r_resource_reject(check, span, "callee has no checked body");
        }
        if (function->closure_aggregate != 0U &&
            (check->nonblocking ? function->is_nonblocking : function->is_noalloc) &&
            !r_resource_drop(check,
                             context->semantic_aggregates[function->closure_aggregate - 1U].type,
                             span,
                             0U))
            return false;
        if (!r_resource_node(check, function->hir_node, 0U)) {
            return false;
        }
    }
    check->chain_count--;
    check->functions[function_id] = 2U;
    return true;
}

static bool r_resource_report(RResourceCheck *check) {
    char message[512];
    size_t length;
    size_t index;
    uint32_t intern_id;
    const char *prefix =
        check->nonblocking ? "@nonblocking cannot be proved: " : "@noalloc cannot be proved: ";
    length = strlen(prefix);
    (void)memcpy(message, prefix, length);
    for (index = 0U; index < check->chain_count; ++index) {
        r_stack_append_name(
            check->context, message, sizeof(message) - 192U, &length, check->chain[index]);
        if (length + 4U < sizeof(message) - 192U) {
            (void)memcpy(message + length, " -> ", 4U);
            length += 4U;
        }
    }
    (void)snprintf(message + length, sizeof(message) - length, "%s", check->reason);
    if (!r_intern_bytes(check->context, (const uint8_t *)message, strlen(message), &intern_id)) {
        return false;
    }
    return r_semantic_add_diagnostic(check->context,
                                     check->nonblocking ? "R-DIAG-RESOURCE-002"
                                                        : "R-DIAG-RESOURCE-001",
                                     "R-FUNC-0019",
                                     check->context->intern_entries[intern_id - 1U].bytes,
                                     check->span);
}

static bool r_semantic_validate_resource_effects(RFrontendContext *context) {
    RResourceCheck check = {0};
    size_t index;
    bool success = false;
    if (r_semantic_has_errors(context)) {
        return true;
    }
    for (index = 0U; index < context->semantic_symbol_count; ++index) {
        const RSemanticSymbol *function = &context->semantic_symbols[index];
        if (function->kind == R_SEMANTIC_SYMBOL_FUNCTION && !function->is_import &&
            (function->is_noalloc || function->is_nonblocking)) {
            break;
        }
    }
    if (index == context->semantic_symbol_count) {
        return true;
    }
    check.context = context;
    check.functions = r_context_allocate(context, context->semantic_symbol_count + 1U);
    check.types = r_context_allocate(context, context->semantic_type_count + 1U);
    check.chain =
        r_context_allocate(context, (context->semantic_symbol_count + 1U) * sizeof(RSymbolId));
    if (check.functions == NULL || check.types == NULL || check.chain == NULL) {
        goto cleanup;
    }
    for (index = 0U; index < context->semantic_symbol_count; ++index) {
        const RSemanticSymbol *function = &context->semantic_symbols[index];
        uint32_t effect;
        if (function->kind != R_SEMANTIC_SYMBOL_FUNCTION || function->is_import) {
            continue;
        }
        for (effect = 0U; effect < 2U; ++effect) {
            if (!(effect == 0U ? function->is_noalloc : function->is_nonblocking)) {
                continue;
            }
            check.nonblocking = effect != 0U;
            check.chain_count = 0U;
            check.reason = NULL;
            (void)memset(check.functions, 0, context->semantic_symbol_count + 1U);
            (void)memset(check.types, 0, context->semantic_type_count + 1U);
            if (!r_resource_function(&check, (RSymbolId)index + 1U, function->name_span) &&
                !r_resource_report(&check)) {
                goto cleanup;
            }
        }
    }
    success = true;
cleanup:
    r_context_free(context, check.functions);
    r_context_free(context, check.types);
    r_context_free(context, check.chain);
    return success;
}

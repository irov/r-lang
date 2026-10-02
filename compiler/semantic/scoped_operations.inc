/* Scoped standard operations (Library R-SLIB-ASYNC-0012): the byte view stays a task-group loan
   of the caller (Core R-STMT-0017) until the native backend acknowledges the outcome. */

static bool r_body_standard_operation_is_scoped(RStandardCallOperation operation) {
    size_t index;

    for (index = 0U;
         index < sizeof(r_standard_scoped_operations) / sizeof(r_standard_scoped_operations[0]);
         ++index) {
        if (r_standard_scoped_operations[index] == operation) {
            return r_standard_scoped_operation(operation) != NULL;
        }
    }
    return false;
}

/* Resolves one argument or result spelling of a scoped descriptor. */
static bool r_body_scoped_type(RBodyContext *body,
                               const RStandardScopedOperationDescriptor *descriptor,
                               uint32_t argument_index,
                               RTypeId *type) {
    RTypeId base;
    const uint32_t view_index = r_standard_scoped_view_index(descriptor);

    if (argument_index == UINT32_C(0)) {
        return r_semantic_intern_named_standard_type(
                   body->frontend, descriptor->handle_type, &base) &&
               r_body_standard_borrow_type(body, base, true, type);
    }
    if (argument_index == view_index) {
        return r_semantic_type_from_token(body->frontend, R_TOKEN_KW_U8, &base) &&
               r_semantic_intern_derived_type(body->frontend,
                                              R_SEMANTIC_TYPE_SLICE,
                                              base,
                                              R_TYPE_ID_INVALID,
                                              UINT64_C(0),
                                              descriptor->exclusive_view
                                                  ? R_SEMANTIC_TYPE_FLAG_NONE
                                                  : R_SEMANTIC_TYPE_FLAG_SHARED,
                                              type);
    }
    if (argument_index == view_index + UINT32_C(1)) {
        return r_semantic_intern_named_standard_type(body->frontend, "std.time::instant", &base) &&
               r_semantic_intern_type(body->frontend, R_SEMANTIC_TYPE_OPTION, base, type);
    }
    if ((descriptor->extra_type != NULL) && (strcmp(descriptor->extra_type, "u64") == 0)) {
        return r_semantic_type_from_token(body->frontend, R_TOKEN_KW_U64, type);
    }
    return (descriptor->extra_type != NULL) &&
           r_semantic_intern_named_standard_type(body->frontend, descriptor->extra_type, type);
}

static bool r_body_scoped_result_type(RBodyContext *body,
                                      const RStandardScopedOperationDescriptor *descriptor,
                                      RTypeId *type) {
    if (strcmp(descriptor->result_type, "usize") == 0) {
        return r_semantic_type_from_token(body->frontend, R_TOKEN_KW_USIZE, type);
    }
    if (strcmp(descriptor->result_type, "void") == 0) {
        return r_semantic_type_from_token(body->frontend, R_TOKEN_KW_VOID, type);
    }
    return r_semantic_intern_named_standard_type(body->frontend, descriptor->result_type, type) &&
           r_semantic_prepare_net_schema(body->frontend, *type);
}

static bool r_body_lower_standard_scoped_call(RBodyContext *body,
                                              RAstRef argument_list,
                                              const RAstNodeView *arguments_view,
                                              RSourceSpan call_span,
                                              RSourceSpan operation_span,
                                              RTypeId contextual_type,
                                              RStandardCallOperation operation,
                                              RExpressionResult *result) {
    const RStandardScopedOperationDescriptor *descriptor = r_standard_scoped_operation(operation);
    RTypeId logical_type, error_type, completion_effects, task_type, start_type, start_effects;
    RHirNodeId child_storage[4];
    RHirVector children = {child_storage, 0U, 4U};
    RHirNodeId node_id;
    RHirNode *node;
    uint32_t argument_count;
    uint32_t index;

    r_body_expression_invalid(result);
    if (descriptor == NULL) {
        body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    argument_count = r_standard_scoped_argument_count(descriptor);
    if (body->current_task_scope == R_SYMBOL_ID_INVALID) {
        return r_body_diagnostic(body,
                                 "R-DIAG-ASYNC-001",
                                 "R-SLIB-ASYNC-0012",
                                 "a scoped standard operation requires an enclosing task_scope",
                                 operation_span);
    }
    if (arguments_view->child_count != argument_count) {
        return r_body_diagnostic(body,
                                 "R-DIAG-TYPE-001",
                                 descriptor->rule,
                                 "scoped operation arguments do not match its signature",
                                 call_span);
    }
    if (!r_body_scoped_result_type(body, descriptor, &logical_type) ||
        !r_semantic_intern_named_standard_type(
            body->frontend, descriptor->error_type, &error_type) ||
        !r_semantic_single_effect_set(body->frontend, error_type, &completion_effects) ||
        !r_semantic_checked_async_types(
            body->frontend, logical_type, completion_effects, &task_type, &start_type)) {
        return false;
    }
    if ((contextual_type != R_TYPE_ID_INVALID) &&
        (r_semantic_value_type(body->frontend, contextual_type) != task_type)) {
        return r_body_diagnostic(body,
                                 "R-DIAG-TYPE-001",
                                 descriptor->rule,
                                 "scoped operation requires its exact task result type",
                                 operation_span);
    }
    if (!r_body_standard_async_start_effects(body, start_type, operation_span, &start_effects)) {
        return false;
    }
    if (body->staging_async_move_argument) {
        return r_body_diagnostic(body,
                                 "R-DIAG-MOVE-003",
                                 descriptor->rule,
                                 "nested staged async arguments are not permitted",
                                 call_span);
    }
    for (index = 0U; index < argument_count; ++index) {
        RAstRef argument_ref;
        RTypeId expected;
        RExpressionResult argument;

        r_body_expression_invalid(&argument);
        if (!r_body_scoped_type(body, descriptor, index, &expected) ||
            !r_ast_ref_child(body->frontend, argument_list, index, &argument_ref) ||
            !r_body_lower_value(body, argument_ref, expected, &argument)) {
            return false;
        }
        if (!argument.valid) {
            return true;
        }
        if (r_semantic_value_type(body->frontend, argument.type) != expected) {
            return r_body_diagnostic(body,
                                     "R-DIAG-TYPE-001",
                                     descriptor->rule,
                                     "scoped operation argument type does not match its contract",
                                     operation_span);
        }
        child_storage[children.count++] = argument.node;
    }
    if (!r_body_append_node(body,
                            R_HIR_STANDARD_CALL,
                            call_span,
                            task_type,
                            R_SYMBOL_ID_INVALID,
                            R_TOKEN_INVALID,
                            UINT64_C(0),
                            false,
                            &children,
                            &node_id)) {
        return false;
    }
    node = r_body_hir_node(body, node_id);
    if (node == NULL) {
        body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    node->standard_operation = operation;
    node->auxiliary_type = start_type;
    /* The handle and the view are loans of the group until the outcome is published. */
    if (!r_body_attach_effect_exits(body, node_id, start_effects, operation_span) ||
        !r_scope_record_call(body, node_id, &children)) {
        return false;
    }
    result->node = node_id;
    result->type = task_type;
    result->valid = true;
    return true;
}

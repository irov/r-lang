/* Native tasks copy shared arguments at publication and commit owners only on successful start. */
static bool r_body_fs_async_type(RBodyContext *body, const char *name, RTypeId *type) {
    RFrontendContext *context = body->frontend;
    RTypeId base;
    if (name[0] == '&') {
        return r_body_fs_async_type(body, name + 1, &base) &&
               r_body_standard_borrow_type(body, base, true, type);
    }
    if (strcmp(name, "deadline") == 0) {
        return r_semantic_intern_named_standard_type(context, "std.time::instant", &base) &&
               r_semantic_intern_derived_type(context, R_SEMANTIC_TYPE_OPTION, base,
                                              R_TYPE_ID_INVALID, 0U, 0U, type);
    }
    if (strcmp(name, "bytes") == 0) {
        return r_semantic_type_from_token(context, R_TOKEN_KW_U8, &base) &&
               r_semantic_intern_type(context, R_SEMANTIC_TYPE_ARRAY, base, type);
    }
    if (strcmp(name, "void") == 0)
        return r_semantic_type_from_token(context, R_TOKEN_KW_VOID, type);
    if (strcmp(name, "bool") == 0)
        return r_semantic_type_from_token(context, R_TOKEN_KW_BOOL, type);
    if (strcmp(name, "i64") == 0)
        return r_semantic_type_from_token(context, R_TOKEN_KW_I64, type);
    if (strcmp(name, "u64") == 0)
        return r_semantic_type_from_token(context, R_TOKEN_KW_U64, type);
    if (strcmp(name, "usize") == 0)
        return r_semantic_type_from_token(context, R_TOKEN_KW_USIZE, type);
    if (strcmp(name, "std.fs::open_file_options") == 0)
        return r_semantic_prepare_fs_open_file_options_schema(context, type);
    if (r_semantic_fs_enum_variants(name, NULL, NULL)) {
        const RSemanticAggregate *aggregate;
        return r_semantic_intern_named_standard_type(context, name, type) &&
               r_semantic_prepare_fs_enum_schema(context, name, &aggregate);
    }
    return r_semantic_intern_named_standard_type(context, name, type);
}

static bool r_body_lower_standard_fs_extended_call(RBodyContext *body,
                                                    RAstRef argument_list,
                                                    const RAstNodeView *arguments_view,
                                                    RSourceSpan call_span,
                                                    RSourceSpan operation_span,
                                                    RTypeId contextual_type,
                                                    const RStandardFsAsyncDescriptor *descriptor,
                                                    RExpressionResult *result) {
    RTypeId expected[5], logical_type, task_type, start_type, start_effects;
    RTypeId completion_effects = R_TYPE_ID_INVALID;
    RHirNodeId child_storage[5], node_id;
    RHirVector children;
    size_t first_move = 0U;
    bool staged = false, committed = false, success = false;
    uint32_t index;
    r_body_expression_invalid(result);
    if (arguments_view->child_count != descriptor->argument_count)
        return r_body_diagnostic(body, "R-DIAG-TYPE-001", "R-SLIB-FS-0004",
                                 "std.fs async operation argument count does not match its contract",
                                 call_span);
    for (index = 0U; index < descriptor->argument_count; ++index)
        if (!r_body_fs_async_type(body, descriptor->arguments[index], &expected[index]))
            return false;
    if (!r_body_fs_async_type(body, descriptor->result, &logical_type))
        return false;
    if (descriptor->throws_fs) {
        RTypeId error;
        if (!r_semantic_intern_named_standard_type(body->frontend, "std.fs::fs_error", &error) ||
            !r_semantic_single_effect_set(body->frontend, error, &completion_effects))
            return false;
    }
    if (!r_semantic_checked_async_types(body->frontend, logical_type, completion_effects,
                                        &task_type, &start_type) ||
        !r_body_standard_async_start_effects(body, start_type, operation_span, &start_effects))
        return false;
    if (contextual_type != R_TYPE_ID_INVALID &&
        r_semantic_value_type(body->frontend, contextual_type) != task_type)
        return r_body_diagnostic(body, "R-DIAG-TYPE-001", "R-SLIB-FS-0004",
                                 "std.fs async operation requires its exact task result type",
                                 operation_span);
    for (index = 0U; index < descriptor->argument_count; ++index) {
        RAstRef argument;
        RExpressionResult value;
        if (!r_ast_ref_child(body->frontend, argument_list, index, &argument))
            goto cleanup;
        if (index == descriptor->move_index) {
            if (body->staging_async_move_argument) {
                success = r_body_diagnostic(body, "R-DIAG-MOVE-003", "R-SLIB-FS-0004",
                                            "nested staged filesystem owner is not permitted",
                                            operation_span);
                goto cleanup;
            }
            first_move = body->frontend->hir_node_count;
            staged = true;
            body->staging_async_move_argument = true;
        }
        if (!r_body_lower_value(body, argument, expected[index], &value))
            goto cleanup;
        body->staging_async_move_argument = false;
        if (!value.valid) {
            success = true;
            goto cleanup;
        }
        if (value.type != expected[index]) {
            success = r_body_diagnostic(body, "R-DIAG-TYPE-001", "R-SLIB-FS-0004",
                                        "std.fs async argument type does not match its contract",
                                        operation_span);
            goto cleanup;
        }
        if (index == descriptor->move_index &&
            r_body_hir_node(body, value.node)->kind != R_HIR_MOVE) {
            success = r_body_diagnostic(body, "R-DIAG-MOVE-003", "R-SLIB-FS-0004",
                                        "filesystem owner argument requires the direct form move name",
                                        operation_span);
            goto cleanup;
        }
        child_storage[index] = value.node;
    }
    children.items = child_storage;
    children.count = descriptor->argument_count;
    children.capacity = 5U;
    if (!r_body_append_node(body, R_HIR_STANDARD_CALL, call_span, task_type, R_SYMBOL_ID_INVALID,
                            R_TOKEN_INVALID, 0U, false, &children, &node_id))
        goto cleanup;
    r_body_hir_node(body, node_id)->auxiliary_type = start_type;
    r_body_hir_node(body, node_id)->standard_operation = descriptor->operation;
    if (!r_body_attach_effect_exits(body, node_id, start_effects, operation_span) ||
        (staged && !r_body_set_async_start_source_state(body, node_id, R_SEMANTIC_OBJECT_STATE_MOVED)))
        goto cleanup;
    result->node = node_id;
    result->type = task_type;
    result->valid = true;
    committed = true;
    success = true;
cleanup:
    body->staging_async_move_argument = false;
    if (staged && !committed && !r_body_rollback_async_move_arguments(body, first_move))
        success = false;
    return success;
}

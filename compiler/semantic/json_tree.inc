/* Tree borrows preserve the origin; mutations consume only the explicit value argument. */
static bool r_json_tree_call(RBodyContext *body,
                             RAstRef arguments_ref,
                             RSourceSpan span,
                             RJsonOperation operation,
                             RExpressionResult *result) {
    RFrontendContext *context = body->frontend;
    RTypeId value_type, byte, bytes, shared, unique, size, boolean, string, result_type;
    RTypeId parameters[3];
    RExpressionResult values[3] = {{0}};
    RHirNodeId nodes[3];
    RHirVector arguments = {nodes, 0U, 3U};
    RAstNodeView view;
    bool consumes = operation == R_JSON_OPERATION_APPEND || operation == R_JSON_OPERATION_INSERT;
    bool mutation = consumes || operation == R_JSON_OPERATION_TAKE_INDEX ||
                    operation == R_JSON_OPERATION_TAKE_FIELD;
    bool borrowed = operation == R_JSON_OPERATION_GET || operation == R_JSON_OPERATION_FIND ||
                    operation == R_JSON_OPERATION_KEY_AT;
    uint32_t count =
        operation == R_JSON_OPERATION_NAME_EQUAL || operation == R_JSON_OPERATION_INSERT ? 3U : 2U;
    if (!r_semantic_intern_named_standard_type(context, "std.json::value", &value_type) ||
        !r_semantic_type_from_token(context, R_TOKEN_KW_U8, &byte) ||
        !r_semantic_type_from_token(context, R_TOKEN_KW_USIZE, &size) ||
        !r_semantic_type_from_token(context, R_TOKEN_KW_BOOL, &boolean) ||
        !r_semantic_type_from_token(context, R_TOKEN_KW_STR, &string) ||
        !r_semantic_intern_derived_type(
            context, R_SEMANTIC_TYPE_SLICE, byte, 0U, 0U, R_SEMANTIC_TYPE_FLAG_SHARED, &bytes) ||
        !r_semantic_intern_derived_type(context,
                                        R_SEMANTIC_TYPE_BORROW,
                                        value_type,
                                        0U,
                                        0U,
                                        R_SEMANTIC_TYPE_FLAG_SHARED,
                                        &shared) ||
        !r_semantic_intern_derived_type(
            context, R_SEMANTIC_TYPE_BORROW, value_type, 0U, 0U, 0U, &unique) ||
        !r_ast_ref_view(context, arguments_ref, &view))
        return false;
    parameters[0] = mutation ? unique : shared;
    parameters[1] = operation == R_JSON_OPERATION_FIND || operation == R_JSON_OPERATION_INSERT ||
                            operation == R_JSON_OPERATION_TAKE_FIELD
                        ? bytes
                        : size;
    parameters[2] = value_type;
    result_type = value_type;
    if (operation == R_JSON_OPERATION_NAME_EQUAL) {
        parameters[0] = parameters[1] = bytes;
        parameters[2] = boolean;
        result_type = boolean;
    } else if (operation == R_JSON_OPERATION_GET || operation == R_JSON_OPERATION_FIND) {
        if (!r_semantic_intern_derived_type(
                context, R_SEMANTIC_TYPE_OPTION, shared, 0U, 0U, 0U, &result_type))
            return false;
    } else if (operation == R_JSON_OPERATION_KEY_AT)
        result_type = string;
    else if (consumes) {
        if (!r_semantic_type_from_token(context, R_TOKEN_KW_VOID, &result_type))
            return false;
        if (operation == R_JSON_OPERATION_APPEND)
            parameters[1] = value_type;
    }
    if (view.child_count != count)
        return r_body_diagnostic(
            body, "R-DIAG-JSON-001", "R-JSON-0001", "wrong number of JSON arguments", span);
    for (uint32_t i = 0U; i < count; ++i) {
        RAstRef argument;
        if (!r_ast_ref_child(context, arguments_ref, i, &argument) ||
            !r_body_lower_call_bounded_value(body, argument, parameters[i], &values[i]))
            return false;
        if (!values[i].valid)
            return true;
        if (values[i].type != parameters[i])
            return r_body_diagnostic(
                body, "R-DIAG-JSON-001", "R-JSON-0001", "JSON argument type does not match", span);
        if (mutation && i == 0U &&
            ((values[0].borrow_origin != R_SYMBOL_ID_INVALID) ||
             values[0].borrow_origin_multiple)) {
            bool has_active_borrow = false;

            if (!r_body_diagnose_active_derived_borrow(
                    body,
                    values[0].borrow_origin,
                    values[0].symbol,
                    span,
                    "R-BORROW-0002",
                    "JSON mutation target has an active derived borrow",
                    &has_active_borrow)) {
                return false;
            }
            if (has_active_borrow) {
                r_body_expression_invalid(result);
                return true;
            }
        }
        if (mutation && i != 0U &&
            (r_semantic_borrow_origins_intersect(body->frontend,
                                                 values[0].borrow_origin,
                                                 values[0].borrow_origin_set,
                                                 values[0].borrow_origin_multiple,
                                                 values[i].borrow_origin,
                                                 values[i].borrow_origin_set,
                                                 values[i].borrow_origin_multiple) ||
             r_semantic_borrow_origin_contains(body->frontend,
                                               values[0].borrow_origin,
                                               values[0].borrow_origin_set,
                                               values[0].borrow_origin_multiple,
                                               values[i].symbol)))
            return r_body_diagnostic(
                body,
                "R-DIAG-BORROW-001",
                "R-BORROW-0002",
                "JSON mutation arguments require conflicting access to one origin",
                span);
        nodes[arguments.count++] = values[i].node;
    }
    if (!r_json_call(
            body, span, operation, result_type, result_type, &arguments, mutation, false, result))
        return false;
    if (borrowed) {
        result->borrow_origin = values[0].borrow_origin;
        result->borrow_origin_set = values[0].borrow_origin_set;
        result->borrow_origin_multiple = values[0].borrow_origin_multiple;
        r_body_hir_node(body, result->node)->borrow_origin = result->borrow_origin;
        r_body_hir_node(body, result->node)->borrow_origin_set = result->borrow_origin_set;
        r_body_hir_node(body, result->node)->borrow_origin_multiple =
            result->borrow_origin_multiple;
        context->semantic_symbols[result->symbol - 1U].return_borrow_parameter = 1U;
        context->semantic_symbols[result->symbol - 1U].return_borrow_parameters_low = UINT64_C(1);
    }
    return true;
}

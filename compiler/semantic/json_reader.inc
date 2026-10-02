static bool r_json_reader_transport(const RFrontendContext *context, RTypeId id) {
    const RSemanticType *type = r_semantic_type(context, r_semantic_value_type(context, id));
    return r_semantic_standard_type_name_equal(context, type, "std.fs::file") ||
           r_semantic_standard_type_name_equal(context, type, "std.io::input") ||
           r_semantic_standard_type_name_equal(context, type, "std.net::tcp_stream");
}
static bool
r_json_reader_named_type(RFrontendContext *context, const char *name, RTypeId base, RTypeId *out) {
    uint32_t intern;
    return r_intern_bytes(context, (const uint8_t *)name, strlen(name), &intern) &&
           r_semantic_intern_derived_type(
               context, R_SEMANTIC_TYPE_STANDARD, base, 0U, intern, 0U, out);
}
/* Native initialization moves the named owner only after every reservation succeeds. The HIR
 * argument is its call-bounded exclusive borrow; checked exits retain the initialized source. */
static bool r_json_reader_move_borrow(RBodyContext *body,
                                      RAstRef argument,
                                      RSourceSpan span,
                                      RExpressionResult *borrowed,
                                      RTypeId *value_type,
                                      RSymbolId *consumed) {
    RExpressionResult value = {0};
    if (body->staging_async_move_argument)
        return r_body_diagnostic(body,
                                 "R-DIAG-JSON-001",
                                 "R-JSON-0001",
                                 "nested JSON owner transfer is not permitted",
                                 span);
    body->staging_async_move_argument = true;
    bool lowered = r_generic_lower_argument(body, argument, R_TYPE_ID_INVALID, 0U, &value);
    body->staging_async_move_argument = false;
    if (!lowered || !value.valid)
        return lowered;
    const RHirNode *move = r_body_hir_node(body, value.node);
    if (move->kind != R_HIR_MOVE || value.symbol == 0U)
        return r_body_diagnostic(body,
                                 "R-DIAG-JSON-001",
                                 "R-JSON-0001",
                                 "JSON owner transfer requires move of a named owner",
                                 span);
    RHirNodeId place;
    if (!r_body_hir_child(body, move, 0U, &place))
        return false;
    r_body_symbol_mut(body, value.symbol)->object_state = R_SEMANTIC_OBJECT_STATE_INITIALIZED;
    *value_type = value.type;
    *consumed = value.symbol;
    value.node = place;
    value.is_place = true;
    RTypeId borrow_type;
    return r_semantic_intern_derived_type(
               body->frontend, R_SEMANTIC_TYPE_BORROW, value.type, 0U, 0U, 0U, &borrow_type) &&
           r_body_form_safe_borrow(body, span, borrow_type, &value, borrowed);
}
static bool r_json_reader_call(RBodyContext *body,
                               RAstRef arguments_ref,
                               RSourceSpan span,
                               RJsonOperation operation,
                               RTypeId contextual_type,
                               RExpressionResult *result) {
    RFrontendContext *context = body->frontend;
    RAstNodeView arguments;
    RAstRef argument;
    RExpressionResult first = {0};
    RHirNodeId nodes[2];
    RHirVector children = {nodes, 0U, 2U};
    RTypeId owner_type = 0U, transport_type = 0U, result_type = 0U, value_type = 0U;
    RSymbolId consumed = 0U;
    bool transfer =
        operation == R_JSON_OPERATION_NEW_READER || operation == R_JSON_OPERATION_DETACH;
    if (!r_ast_ref_view(context, arguments_ref, &arguments))
        return false;
    if (arguments.child_count < 1U || arguments.child_count > 2U ||
        (operation == R_JSON_OPERATION_READ_NEXT && arguments.child_count != 2U) ||
        (operation >= R_JSON_OPERATION_DETACH && arguments.child_count != 1U))
        return r_body_diagnostic(
            body, "R-DIAG-JSON-001", "R-JSON-0001", "wrong number of JSON reader arguments", span);
    if (!r_ast_ref_child(context, arguments_ref, 0U, &argument))
        return false;
    if (transfer) {
        if (!r_json_reader_move_borrow(body, argument, span, &first, &owner_type, &consumed))
            return false;
    } else {
        if (!r_generic_lower_argument(body, argument, R_TYPE_ID_INVALID, 0U, &first) ||
            !r_body_place_to_value(body, &first))
            return false;
    }
    if (!first.valid)
        return true;
    const RSemanticType *pointer = r_semantic_type(context, first.type);
    if (pointer->kind != R_SEMANTIC_TYPE_BORROW || pointer->flags != 0U)
        return r_body_diagnostic(body,
                                 "R-DIAG-JSON-001",
                                 "R-JSON-0001",
                                 "JSON reader operation requires an exclusive owner borrow",
                                 span);
    owner_type = pointer->base;
    const RSemanticType *owner = r_semantic_type(context, owner_type);
    if (operation == R_JSON_OPERATION_NEW_READER) {
        transport_type = owner_type;
        if (!r_json_reader_transport(context, transport_type))
            return r_body_diagnostic(body,
                                     "R-DIAG-JSON-001",
                                     "R-JSON-0001",
                                     "JSON reader accepts file, input, or tcp_stream",
                                     span);
        if (!r_json_reader_named_type(context, "std.json::reader", transport_type, &result_type))
            return false;
        value_type = transport_type;
    } else {
        const char *name =
            operation >= R_JSON_OPERATION_TAKE_HANDLE ? "std.json::detached" : "std.json::reader";
        if (!r_semantic_standard_type_name_equal(context, owner, name) || owner->second != 0U ||
            !r_json_reader_transport(context, owner->base))
            return r_body_diagnostic(
                body,
                "R-DIAG-JSON-001",
                "R-JSON-0001",
                "JSON operation requires the corresponding reader or detached owner",
                span);
        transport_type = owner->base;
        value_type = transport_type;
        if (operation == R_JSON_OPERATION_DETACH) {
            if (!r_json_reader_named_type(
                    context, "std.json::detached", transport_type, &result_type))
                return false;
        } else if (operation == R_JSON_OPERATION_TAKE_HANDLE)
            result_type = transport_type;
        else if (operation == R_JSON_OPERATION_TAKE_BYTES) {
            if (!r_semantic_bytes_type(context, &result_type))
                return false;
        } else {
            const RSemanticType *expected =
                r_semantic_type(context, r_semantic_value_type(context, contextual_type));
            RTypeId logical = contextual_type;
            if (expected != NULL && expected->kind == R_SEMANTIC_TYPE_TASK) {
                logical = expected->base;
                expected = r_semantic_type(context, logical);
            }
            if (expected == NULL || expected->kind != R_SEMANTIC_TYPE_OPTION)
                return r_body_diagnostic(
                    body,
                    "R-DIAG-JSON-001",
                    "R-JSON-0001",
                    "JSON read_next requires a task<o<T>> or direct await o<T> destination",
                    span);
            value_type = expected->base;
            size_t diagnostics = context->diagnostic_count;
            if (!r_json_decode_type(body, value_type, span, 0U))
                return false;
            if (context->diagnostic_count != diagnostics)
                return true;
            if (!r_semantic_type_is_send(context, value_type) ||
                r_semantic_type_crosses_await(context, value_type))
                return r_body_diagnostic(
                    body,
                    "R-DIAG-JSON-001",
                    "R-JSON-0001",
                    "JSON read_next result requires proven send and unborrowed",
                    span);
            RTypeId types[3], effects, completion;
            RTypeVector errors = {types, 3U, 3U};
            bool duplicate;
            const char *transport_error =
                r_semantic_standard_type_name_equal(
                    context, r_semantic_type(context, transport_type), "std.net::tcp_stream")
                    ? "std.net::net_error"
                    : "std.io::io_error";
            if (!r_semantic_intern_named_standard_type(
                    context, "std.alloc::alloc_error", &types[0]) ||
                !r_semantic_intern_named_standard_type(context, "std.json::error", &types[1]) ||
                !r_semantic_intern_named_standard_type(context, transport_error, &types[2]) ||
                !r_semantic_intern_effect_set(context, &errors, &effects, &duplicate) ||
                !r_semantic_intern_effect_carrier(context, logical, effects, &completion) ||
                !r_semantic_intern_derived_type(
                    context, R_SEMANTIC_TYPE_TASK, logical, effects, 0U, 0U, &result_type))
                return false;
        }
    }
    nodes[children.count++] = first.node;
    if (arguments.child_count == 2U) {
        RTypeId parameter;
        RExpressionResult second = {0};
        if (operation == R_JSON_OPERATION_NEW_READER) {
            if (!r_semantic_intern_named_standard_type(context, "std.json::options", &parameter) ||
                !r_semantic_prepare_json_schema(context, parameter))
                return false;
        } else {
            RTypeId instant;
            if (!r_semantic_intern_named_standard_type(context, "std.time::instant", &instant) ||
                !r_semantic_intern_derived_type(
                    context, R_SEMANTIC_TYPE_OPTION, instant, 0U, 0U, 0U, &parameter))
                return false;
        }
        if (!r_ast_ref_child(context, arguments_ref, 1U, &argument) ||
            !r_body_lower_call_bounded_value(body, argument, parameter, &second))
            return false;
        if (!second.valid)
            return true;
        if (second.type != parameter)
            return r_body_diagnostic(body,
                                     "R-DIAG-JSON-001",
                                     "R-JSON-0001",
                                     "JSON reader argument type does not match",
                                     span);
        nodes[children.count++] = second.node;
    }
    if (!r_json_call(
            body, span, operation, value_type, result_type, &children, true, false, result))
        return false;
    if (result->valid && consumed != 0U)
        r_body_symbol_mut(body, consumed)->object_state = R_SEMANTIC_OBJECT_STATE_MOVED;
    return true;
}

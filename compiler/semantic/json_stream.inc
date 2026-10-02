static bool r_json_stream_call(RBodyContext *body,
                               RAstRef arguments_ref,
                               RSourceSpan span,
                               RJsonOperation operation,
                               RTypeId contextual_type,
                               RExpressionResult *result) {
    RFrontendContext *context = body->frontend;
    RAstNodeView arguments;
    RHirNodeId nodes[3];
    RHirVector children = {nodes, 0U, 3U};
    RTypeId parameters[3] = {0};
    RTypeId result_type = contextual_type;
    RTypeId value_type;
    if (!r_ast_ref_view(context, arguments_ref, &arguments))
        return false;
    if (operation == R_JSON_OPERATION_NEW_DECODER) {
        const RSemanticType *type =
            r_semantic_type(context, r_semantic_value_type(context, contextual_type));
        if (!r_semantic_standard_type_name_equal(context, type, "std.json::decoder") ||
            type->base == 0U || type->second != 0U)
            return r_body_diagnostic(
                body,
                "R-DIAG-JSON-001",
                "R-JSON-0001",
                "JSON new_decoder requires a decoder(T) initializer or assignment",
                span);
        value_type = type->base;
        size_t diagnostics = context->diagnostic_count;
        if (!r_json_decode_type(body, value_type, span, 0U))
            return false;
        if (context->diagnostic_count != diagnostics)
            return true;
        if (arguments.child_count > 1U)
            return r_body_diagnostic(body,
                                     "R-DIAG-JSON-001",
                                     "R-JSON-0001",
                                     "JSON new_decoder accepts zero arguments or options",
                                     span);
        if (!r_semantic_intern_named_standard_type(context, "std.json::options", &parameters[0]) ||
            !r_semantic_prepare_json_schema(context, parameters[0]))
            return false;
    } else {
        uint32_t arity = operation == R_JSON_OPERATION_FEED ? 3U : 1U;
        if (arguments.child_count != arity)
            return r_body_diagnostic(body,
                                     "R-DIAG-JSON-001",
                                     "R-JSON-0001",
                                     "wrong number of JSON decoder arguments",
                                     span);
        RAstRef argument;
        RExpressionResult borrowed = {0};
        if (!r_ast_ref_child(context, arguments_ref, 0U, &argument) ||
            !r_generic_lower_argument(body, argument, R_TYPE_ID_INVALID, R_SEMANTIC_TYPE_FLAG_NONE, &borrowed) ||
            !r_body_place_to_value(body, &borrowed))
            return false;
        if (!borrowed.valid)
            return true;
        const RSemanticType *pointer = r_semantic_type(context, borrowed.type);
        const RSemanticType *decoder = pointer->kind == R_SEMANTIC_TYPE_BORROW
                                           ? r_semantic_type(context, pointer->base)
                                           : NULL;
        if (pointer->kind != R_SEMANTIC_TYPE_BORROW || pointer->flags != 0U ||
            !r_semantic_standard_type_name_equal(context, decoder, "std.json::decoder") ||
            decoder->base == 0U || decoder->second != 0U)
            return r_body_diagnostic(
                body,
                "R-DIAG-JSON-001",
                "R-JSON-0001",
                "JSON decoder operation requires an exclusive decoder(T) borrow",
                span);
        value_type = decoder->base;
        result_type = value_type;
        nodes[children.count++] = borrowed.node;
        if (operation == R_JSON_OPERATION_FEED) {
            RTypeId byte;
            if (!r_semantic_type_from_token(context, R_TOKEN_KW_U8, &byte) ||
                !r_semantic_type_from_token(context, R_TOKEN_KW_BOOL, &parameters[2]) ||
                !r_semantic_intern_derived_type(context,
                                                R_SEMANTIC_TYPE_SLICE,
                                                byte,
                                                0U,
                                                0U,
                                                R_SEMANTIC_TYPE_FLAG_SHARED,
                                                &parameters[1]) ||
                !r_semantic_intern_named_standard_type(
                    context, "std.json::feed_result", &result_type) ||
                !r_semantic_prepare_json_schema(context, result_type))
                return false;
        }
    }
    for (uint32_t i = (uint32_t)children.count; i < arguments.child_count; ++i) {
        RAstRef argument;
        RExpressionResult value = {0};
        if (!r_ast_ref_child(context, arguments_ref, i, &argument) ||
            !r_body_lower_call_bounded_value(body, argument, parameters[i], &value))
            return false;
        if (!value.valid)
            return true;
        if (value.type != parameters[i])
            return r_body_diagnostic(body,
                                     "R-DIAG-JSON-001",
                                     "R-JSON-0001",
                                     "JSON decoder argument type does not match",
                                     span);
        nodes[children.count++] = value.node;
    }
    return r_json_call(
        body, span, operation, value_type, result_type, &children, true, false, result);
}

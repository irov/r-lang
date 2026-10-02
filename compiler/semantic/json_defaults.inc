#include "json_fields.inc"

static bool r_json_has_implicit_default(const RFrontendContext *context, RTypeId id) {
    const RSemanticType *type = r_semantic_type(context, r_semantic_value_type(context, id));
    if (type == NULL)
        return false;
    if ((type->kind >= R_SEMANTIC_TYPE_BOOL && type->kind <= R_SEMANTIC_TYPE_CHAR) ||
        type->kind == R_SEMANTIC_TYPE_ARRAY || type->kind == R_SEMANTIC_TYPE_LIST ||
        type->kind == R_SEMANTIC_TYPE_DICT || type->kind == R_SEMANTIC_TYPE_OPTION ||
        r_semantic_standard_type_name_equal(context, type, "std.string::string"))
        return true;
    return (type->kind == R_SEMANTIC_TYPE_STRUCT || type->kind == R_SEMANTIC_TYPE_FIXED_ARRAY) &&
           r_body_type_is_default_initializable(context, id);
}
static bool r_json_validate_contracts(RFrontendContext *context) {
    if (!r_json_validate_field_types(context))
        return false;
    for (size_t a = 0U; a < context->semantic_aggregate_count; ++a) {
        const RSemanticAggregate *aggregate = &context->semantic_aggregates[a];
        if (!aggregate->complete || aggregate->poisoned)
            continue;
        if (!r_json_build_field_schema(context, aggregate->type))
            return false;
        for (uint32_t i = 0U; i < aggregate->field_count; ++i) {
            const RSemanticField *field = &context->semantic_fields[aggregate->first_field + i];
            /* A declared initializer becomes the default (L33, r_json_validate_defaults). */
            if ((field->json.flags & (R_JSON_FIELD_OPTIONAL | R_JSON_FIELD_SKIP)) != 0U &&
                (field->json.flags & R_JSON_FIELD_DEFAULT) == 0U &&
                (field->initializer.node == 0U) &&
                !r_json_has_implicit_default(context, field->type))
                if (!r_json_attribute_diagnostic(
                        context,
                        field->name_span,
                        "this optional or excluded field requires an explicit JSON default"))
                    return false;
            if (field->is_protected || (field->json.flags & R_JSON_FIELD_SKIP) != 0U)
                continue;
            const RSemanticType *type =
                r_semantic_type(context, r_semantic_value_type(context, field->type));
            if (type == NULL)
                continue; /* The field type was diagnosed where it was declared. */
            if (type->kind == R_SEMANTIC_TYPE_OPTION)
                type = r_semantic_type(context, type->base);
            if (type == NULL)
                continue;
            if ((field->json.flags & R_JSON_FIELD_STRING) != 0U &&
                !r_json_field_is_numeric(context, field->type) &&
                type->kind != R_SEMANTIC_TYPE_PARAMETER) {
                const RSemanticAggregate *custom = r_semantic_aggregate_for_type(
                    context, (RTypeId)(type - context->semantic_types) + 1U);
                if (custom == NULL ||
                    (custom->json_marshal_function == 0U && custom->json_unmarshal_function == 0U))
                    if (!r_json_attribute_diagnostic(
                            context,
                            field->name_span,
                            "JSON string requires a numeric field or a numeric custom converter"))
                        return false;
            }
        }
    }
    return true;
}

/* Defaults resolve in the declaring module and are checked even without a decode call. */
static RAstRef r_json_default_leaf(const RFrontendContext *context, RAstRef ref) {
    RAstNodeView view;
    while (r_ast_ref_view(context, ref, &view) && view.child_count == 1U &&
           view.kind != R_SYNTAX_STRING_SEQUENCE) {
        RAstRef child;
        if (!r_ast_ref_child(context, ref, 0U, &child))
            break;
        ref = child;
    }
    return ref;
}
static bool r_json_check_default(RFrontendContext *context, RSemanticField *field) {
    RBodyContext body = {0};
    RAstRef expression = {field->name_span.source, field->json.default_ast};
    RAstRef leaf = r_json_default_leaf(context, expression);
    RAstNodeView view;
    RAstTokenView token = {0};
    RBodyLookupIssue issue;
    RExpressionResult value = {0};
    RSymbolId factory = 0U;
    body.frontend = context;
    body.source = field->name_span.source;
    if (r_ast_ref_view(context, leaf, &view) && view.kind == R_SYNTAX_QUALIFIED_NAME)
        factory = r_body_resolve_qualified_name(&body, leaf, &token, &issue);
    else if (r_ast_ref_token(context, leaf, &token) && token.kind == R_TOKEN_IDENTIFIER) {
        token.span.start = UINT32_MAX;
        factory = r_body_resolve_name(&body, &token, &issue);
    } else {
        const RSemanticType *type =
            r_semantic_type(context, r_semantic_value_type(context, field->type));
        if (r_semantic_standard_type_name_equal(context, type, "std.string::string")) {
            size_t length;
            if (r_ast_ref_view(context, leaf, &view) && view.kind == R_SYNTAX_STRING_SEQUENCE &&
                r_intern_string_sequence(
                    context, view.span, &field->json.default_string, &length)) {
                field->json.default_kind = R_JSON_DEFAULT_STRING;
                return true;
            }
            return r_json_attribute_diagnostic(context,
                                               field->name_span,
                                               "JSON string default requires a string literal or a "
                                               "factory returning the exact field type");
        }
        if (r_generic_type_is_dependent(context, field->type, 0U))
            return r_json_attribute_diagnostic(
                context,
                field->name_span,
                "a literal JSON default requires a proven concrete scalar field type");
        if (!r_body_lower_value(&body, expression, field->type, &value))
            return false;
        if (!value.valid || value.type != field->type)
            return r_json_attribute_diagnostic(
                context, field->name_span, "JSON default type does not match the field");
        RBodyConstantValue constant = {0};
        const RHirNode *literal = r_body_literal_node(&body, value.node);
        if (type->kind == R_SEMANTIC_TYPE_F32 || type->kind == R_SEMANTIC_TYPE_F64 ||
            type->kind == R_SEMANTIC_TYPE_CHAR) {
            if (literal == NULL)
                return r_json_attribute_diagnostic(
                    context,
                    field->name_span,
                    "JSON default requires a scalar literal or a factory");
            constant.kind = type->kind;
            constant.bits = literal->integer_value;
            constant.valid = true;
            const RHirNode *root = r_body_hir_node(&body, value.node);
            if (root->kind == R_HIR_UNARY && root->operation == R_TOKEN_MINUS)
                constant.bits ^= type->kind == R_SEMANTIC_TYPE_F32 ? UINT64_C(0x80000000)
                                                                   : UINT64_C(0x8000000000000000);
        } else if (!r_body_constant_scalar(&body, value.node, &constant) || !constant.valid)
            return r_json_attribute_diagnostic(
                context, field->name_span, "JSON default requires a scalar literal or a factory");
        field->json.default_bits = constant.bits;
        field->json.default_scalar_kind = constant.kind;
        field->json.default_kind = R_JSON_DEFAULT_SCALAR;
        field->json.default_node = value.node;
        return true;
    }
    if (factory == 0U)
        return r_json_attribute_diagnostic(
            context, field->name_span, "JSON default factory is unresolved or inaccessible");
    const RSemanticSymbol *function = &context->semantic_symbols[factory - 1U];
    if (function->kind != R_SEMANTIC_SYMBOL_FUNCTION || function->is_async || function->is_unsafe ||
        function->is_extern_c || function->parameter_count != 0U ||
        function->return_type != field->type || function->generic_schema != 0U)
        return r_json_attribute_diagnostic(
            context,
            field->name_span,
            "JSON default factory must be safe, synchronous, non-generic, take no arguments and "
            "return the exact field type");
    for (uint32_t i = 0U; i < r_semantic_effect_count(context, function->throws_type); ++i) {
        const RSemanticType *error =
            r_semantic_type(context, r_semantic_effect_at(context, function->throws_type, i));
        if (!r_semantic_standard_type_name_equal(context, error, "std.json::error") &&
            !r_semantic_standard_type_name_equal(context, error, "std.alloc::alloc_error"))
            return r_json_attribute_diagnostic(
                context,
                field->name_span,
                "JSON default factory may throw only std.json::error and std.alloc::alloc_error");
    }
    field->json.default_kind = R_JSON_DEFAULT_FACTORY;
    field->json.default_factory = factory;
    return true;
}
/* R-JSON-0001, R-INIT-0004 (L33): an optional or skipped field without a JSON default takes
   its declared initializer as the default when the initializer is a JSON default form, a literal
   or a call `factory()` of a zero-argument factory. */
static bool r_json_default_from_initializer(RFrontendContext *context, RSemanticField *field) {
    RAstRef leaf = r_json_default_leaf(context, field->initializer);
    RAstRef target = field->initializer;
    RAstNodeView view;
    RAstNodeView suffix_view;
    RAstRef callee;
    RAstRef suffix;
    RAstRef arguments;
    RAstNodeView arguments_view;

    if (r_ast_ref_view(context, leaf, &view) && (view.kind == R_SYNTAX_POSTFIX_EXPRESSION)) {
        if ((view.child_count != 2U) || !r_ast_ref_child(context, leaf, 0U, &callee) ||
            !r_ast_ref_child(context, leaf, 1U, &suffix) ||
            !r_ast_ref_view(context, suffix, &suffix_view) ||
            (suffix_view.kind != R_SYNTAX_CALL_SUFFIX) ||
            !r_ast_find_direct_child(context, suffix, R_SYNTAX_ARGUMENT_LIST, &arguments) ||
            !r_ast_ref_view(context, arguments, &arguments_view) ||
            (arguments_view.child_count != 0U)) {
            return r_json_attribute_diagnostic(
                context,
                field->name_span,
                "a field initializer is the JSON default of an optional field only as a literal or "
                "a call of a zero-argument factory");
        }
        target = callee;
    }
    if (target.source != field->name_span.source) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    field->json.flags |= R_JSON_FIELD_DEFAULT;
    field->json.default_ast = target.node;
    return r_json_check_default(context, field);
}

static bool r_json_validate_defaults(RFrontendContext *context) {
    for (size_t i = 0U; i < context->semantic_field_count; ++i) {
        RSemanticField *field = &context->semantic_fields[i];
        if (((field->json.flags & (R_JSON_FIELD_OPTIONAL | R_JSON_FIELD_SKIP)) != 0U) &&
            ((field->json.flags & R_JSON_FIELD_DEFAULT) == 0U) &&
            (field->initializer.node != 0U) && (field->json.default_kind == 0U)) {
            if (!r_json_default_from_initializer(context, field))
                return false;
            continue;
        }
        if ((field->json.flags & R_JSON_FIELD_DEFAULT) != 0U && field->json.default_kind == 0U &&
            !r_json_check_default(context, field))
            return false;
    }
    return true;
}

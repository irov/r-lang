#include "json_defaults.inc"

/* This capability describes the same concrete codec schema used by the generators. */
static bool r_json_capability_visit(const RFrontendContext *context,
                                    RTypeId id,
                                    bool encode,
                                    uint32_t depth,
                                    const RSemanticTypeVisit *parent) {
    id = r_semantic_value_type(context, id);
    for (const RSemanticTypeVisit *previous = parent; previous != NULL; previous = previous->parent)
        if (previous->type == id)
            return true;
    RSemanticTypeVisit visit = {id, parent, (uint32_t)depth};
    const RSemanticType *type = r_semantic_type(context, r_semantic_value_type(context, id));
    if (type == NULL || depth > context->options.limits.max_nesting)
        return false;
    if (type->kind == R_SEMANTIC_TYPE_PARAMETER)
        return (r_generic_parameter_constraints(context, id) &
                (encode ? R_GENERIC_JSON_ENCODE : R_GENERIC_JSON_DECODE)) != 0U;
    if ((type->kind >= R_SEMANTIC_TYPE_BOOL && type->kind <= R_SEMANTIC_TYPE_CHAR) ||
        r_semantic_standard_type_name_equal(context, type, "std.string::string") ||
        r_semantic_standard_type_name_equal(context, type, "std.json::value") ||
        r_semantic_standard_type_name_equal(context, type, "std.json::number"))
        return true;
    if (encode &&
        (type->kind == R_SEMANTIC_TYPE_STR || type->kind == R_SEMANTIC_TYPE_CONSTEXPR_STR))
        return true;
    if (type->kind == R_SEMANTIC_TYPE_OPTION || type->kind == R_SEMANTIC_TYPE_ARRAY ||
        type->kind == R_SEMANTIC_TYPE_LIST || type->kind == R_SEMANTIC_TYPE_FIXED_ARRAY ||
        (encode && type->kind == R_SEMANTIC_TYPE_SLICE)) {
        const RSemanticType *child = r_semantic_type(context, type->base);
        return !(type->kind == R_SEMANTIC_TYPE_OPTION && child->kind == R_SEMANTIC_TYPE_OPTION) &&
               r_json_capability_visit(context, type->base, encode, depth + 1U, &visit);
    }
    if (type->kind == R_SEMANTIC_TYPE_DICT)
        return r_semantic_standard_type_name_equal(
                   context, r_semantic_type(context, type->base), "std.string::string") &&
               r_json_capability_visit(context, type->second, encode, depth + 1U, &visit);
    const RSemanticAggregate *aggregate = r_semantic_aggregate_for_type(context, id);
    /* R-TYPE-0052 (L18.1): a tuple has no JSON form. */
    if (aggregate == NULL || !aggregate->complete || aggregate->poisoned || aggregate->is_tuple)
        return false;
    if ((encode ? aggregate->json_marshal_function : aggregate->json_unmarshal_function) != 0U)
        return true;
    for (uint32_t i = 0U; i < aggregate->field_count; ++i) {
        const RSemanticField *field = &context->semantic_fields[aggregate->first_field + i];
        if (field->is_protected || (field->json.flags & R_JSON_FIELD_SKIP) != 0U) {
            if (!encode && (field->json.flags & R_JSON_FIELD_DEFAULT) == 0U &&
                !r_json_has_implicit_default(context, field->type))
                return false;
            continue;
        }
        if (!r_json_capability_visit(context, field->type, encode, depth + 1U, &visit))
            return false;
    }
    for (uint32_t i = 0U; i < aggregate->variant_count; ++i) {
        RTypeId payload = context->semantic_variants[aggregate->first_variant + i].payload_type;
        if (payload != 0U && !r_json_capability_visit(context, payload, encode, depth + 1U, &visit))
            return false;
    }
    return true;
}

static bool
r_json_capability(const RFrontendContext *context, RTypeId id, bool encode, uint32_t depth) {
    return r_json_capability_visit(context, id, encode, depth, NULL);
}

/* R-FUNC-0004: derived encoders and decoders recurse; the stack pass rejects self-nesting ones. */
static void r_json_mark_derived(RFrontendContext *context, const RSemanticAggregate *aggregate) {
    if (aggregate != NULL) {
        context->semantic_aggregates[(size_t)(aggregate - context->semantic_aggregates)]
            .json_derived = true;
    }
}

static bool r_json_encode_type_visit(RBodyContext *body,
                                     RTypeId id,
                                     RSourceSpan span,
                                     size_t depth,
                                     const RSemanticTypeVisit *parent) {
    RFrontendContext *context = body->frontend;
    id = r_semantic_value_type(context, id);
    for (const RSemanticTypeVisit *previous = parent; previous != NULL; previous = previous->parent)
        if (previous->type == id)
            return true;
    RSemanticTypeVisit visit = {id, parent, (uint32_t)depth};
    const RSemanticType *type = r_semantic_type(context, r_semantic_value_type(context, id));
    if (type == NULL || depth > context->options.limits.max_nesting)
        return r_body_diagnostic(body,
                                 "R-DIAG-JSON-001",
                                 "R-JSON-0001",
                                 "JSON type exceeds the compiler nesting limit",
                                 span);
    if (type->kind == R_SEMANTIC_TYPE_PARAMETER)
        return r_json_capability(context, id, true, 0U) ||
               r_body_diagnostic(body,
                                 "R-DIAG-JSON-001",
                                 "R-JSON-0001",
                                 "generic JSON operation requires its directional JSON constraint",
                                 span);
    const RSemanticAggregate *custom = r_semantic_aggregate_for_type(context, id);
    if (custom != NULL && custom->json_marshal_function != 0U)
        return true;
    if ((type->kind >= R_SEMANTIC_TYPE_BOOL && type->kind <= R_SEMANTIC_TYPE_CHAR) ||
        type->kind == R_SEMANTIC_TYPE_STR || type->kind == R_SEMANTIC_TYPE_CONSTEXPR_STR ||
        r_semantic_standard_type_name_equal(context, type, "std.string::string") ||
        r_semantic_standard_type_name_equal(context, type, "std.json::value") ||
        r_semantic_standard_type_name_equal(context, type, "std.json::number"))
        return true;
    if (type->kind == R_SEMANTIC_TYPE_FIXED_ARRAY || type->kind == R_SEMANTIC_TYPE_OPTION ||
        type->kind == R_SEMANTIC_TYPE_ARRAY || type->kind == R_SEMANTIC_TYPE_LIST ||
        type->kind == R_SEMANTIC_TYPE_SLICE) {
        const RSemanticType *base = r_semantic_type(context, type->base);
        if (type->kind == R_SEMANTIC_TYPE_OPTION && base->kind == R_SEMANTIC_TYPE_OPTION)
            return r_body_diagnostic(body,
                                     "R-DIAG-JSON-001",
                                     "R-JSON-0001",
                                     "nested optional JSON representations require a converter",
                                     span);
        return r_json_encode_type_visit(body, type->base, span, depth + 1U, &visit);
    }
    if (type->kind == R_SEMANTIC_TYPE_DICT) {
        if (!r_semantic_standard_type_name_equal(
                context, r_semantic_type(context, type->base), "std.string::string"))
            return r_body_diagnostic(body,
                                     "R-DIAG-JSON-001",
                                     "R-JSON-0001",
                                     "JSON dictionaries require string keys",
                                     span);
        return r_json_encode_type_visit(body, type->second, span, depth + 1U, &visit);
    }
    if (type->kind == R_SEMANTIC_TYPE_ENUM) {
        const RSemanticAggregate *aggregate = r_semantic_aggregate_for_type(context, id);
        r_json_mark_derived(context, aggregate);
        for (uint32_t i = 0U; i < aggregate->variant_count; ++i) {
            const RSemanticVariant *variant =
                &context->semantic_variants[aggregate->first_variant + i];
            if (variant->payload_type != 0U &&
                !r_json_encode_type_visit(
                    body, variant->payload_type, variant->name_span, depth + 1U, &visit))
                return false;
        }
        return true;
    }
    if (type->kind == R_SEMANTIC_TYPE_STRUCT) {
        if (!r_json_build_field_schema(context, id))
            return false;
        const RSemanticAggregate *aggregate = r_semantic_aggregate_for_type(context, id);
        r_json_mark_derived(context, aggregate);
        for (uint32_t i = 0U; i < aggregate->field_count; ++i) {
            const RSemanticField *field = &context->semantic_fields[aggregate->first_field + i];
            if (field->is_protected || (field->json.flags & R_JSON_FIELD_SKIP) != 0U)
                continue;
            if ((field->json.flags & R_JSON_FIELD_STRING) != 0U &&
                !r_json_field_is_numeric(context, field->type)) {
                const RSemanticType *numeric_type =
                    r_semantic_type(context, r_semantic_value_type(context, field->type));
                RTypeId numeric_id =
                    numeric_type->kind == R_SEMANTIC_TYPE_OPTION ? numeric_type->base : field->type;
                const RSemanticAggregate *converter =
                    r_semantic_aggregate_for_type(context, numeric_id);
                if (converter == NULL || converter->json_marshal_function == 0U)
                    return r_json_attribute_diagnostic(context,
                                                       field->name_span,
                                                       "JSON string requires a numeric field or a "
                                                       "custom converter for this direction");
            }
            if (!r_json_encode_type_visit(body, field->type, field->name_span, depth + 1U, &visit))
                return false;
        }
        return true;
    }
    return r_body_diagnostic(
        body, "R-DIAG-JSON-001", "R-JSON-0001", "this type has no automatic JSON encoder", span);
}

static bool r_json_encode_type(RBodyContext *body, RTypeId id, RSourceSpan span, size_t depth) {
    return r_json_encode_type_visit(body, id, span, depth, NULL);
}

/* Concrete decode eligibility is checked before any generated helper is registered. */
static bool r_json_decode_type_visit(RBodyContext *body,
                                     RTypeId id,
                                     RSourceSpan span,
                                     size_t depth,
                                     const RSemanticTypeVisit *parent) {
    RFrontendContext *context = body->frontend;
    id = r_semantic_value_type(context, id);
    for (const RSemanticTypeVisit *previous = parent; previous != NULL; previous = previous->parent)
        if (previous->type == id)
            return true;
    RSemanticTypeVisit visit = {id, parent, (uint32_t)depth};
    const RSemanticType *type = r_semantic_type(context, r_semantic_value_type(context, id));
    if (type == NULL || depth > context->options.limits.max_nesting)
        return r_body_diagnostic(body,
                                 "R-DIAG-JSON-001",
                                 "R-JSON-0001",
                                 "JSON type exceeds the compiler nesting limit",
                                 span);
    if (type->kind == R_SEMANTIC_TYPE_PARAMETER)
        return r_json_capability(context, id, false, 0U) ||
               r_body_diagnostic(body,
                                 "R-DIAG-JSON-001",
                                 "R-JSON-0001",
                                 "generic JSON operation requires its directional JSON constraint",
                                 span);
    const RSemanticAggregate *custom = r_semantic_aggregate_for_type(context, id);
    if (custom != NULL && custom->json_unmarshal_function != 0U)
        return true;
    if ((type->kind >= R_SEMANTIC_TYPE_BOOL && type->kind <= R_SEMANTIC_TYPE_F64) ||
        type->kind == R_SEMANTIC_TYPE_CHAR ||
        r_semantic_standard_type_name_equal(context, type, "std.string::string") ||
        r_semantic_standard_type_name_equal(context, type, "std.json::value") ||
        r_semantic_standard_type_name_equal(context, type, "std.json::number"))
        return true;
    if (type->kind == R_SEMANTIC_TYPE_C_ABI_NUMERIC)
        return true;
    if (type->kind == R_SEMANTIC_TYPE_FIXED_ARRAY || type->kind == R_SEMANTIC_TYPE_OPTION ||
        type->kind == R_SEMANTIC_TYPE_ARRAY || type->kind == R_SEMANTIC_TYPE_LIST) {
        const RSemanticType *base = r_semantic_type(context, type->base);
        if (type->kind == R_SEMANTIC_TYPE_OPTION && base->kind == R_SEMANTIC_TYPE_OPTION)
            return r_body_diagnostic(body,
                                     "R-DIAG-JSON-001",
                                     "R-JSON-0001",
                                     "nested optional JSON representations require a converter",
                                     span);
        return r_json_decode_type_visit(body, type->base, span, depth + 1U, &visit);
    }
    if (type->kind == R_SEMANTIC_TYPE_DICT) {
        if (!r_semantic_standard_type_name_equal(
                context, r_semantic_type(context, type->base), "std.string::string"))
            return r_body_diagnostic(body,
                                     "R-DIAG-JSON-001",
                                     "R-JSON-0001",
                                     "JSON dictionaries require string keys",
                                     span);
        return r_json_decode_type_visit(body, type->second, span, depth + 1U, &visit);
    }
    if (type->kind == R_SEMANTIC_TYPE_ENUM) {
        const RSemanticAggregate *aggregate = r_semantic_aggregate_for_type(context, id);
        r_json_mark_derived(context, aggregate);
        for (uint32_t i = 0U; i < aggregate->variant_count; ++i) {
            const RSemanticVariant *variant =
                &context->semantic_variants[aggregate->first_variant + i];
            if (variant->payload_type != 0U &&
                !r_json_decode_type_visit(
                    body, variant->payload_type, variant->name_span, depth + 1U, &visit))
                return false;
        }
        return true;
    }
    if (type->kind == R_SEMANTIC_TYPE_STRUCT) {
        if (!r_json_build_field_schema(context, id))
            return false;
        const RSemanticAggregate *aggregate = r_semantic_aggregate_for_type(context, id);
        r_json_mark_derived(context, aggregate);
        for (uint32_t i = 0U; i < aggregate->field_count; ++i) {
            const RSemanticField *field = &context->semantic_fields[aggregate->first_field + i];
            if (field->is_protected || (field->json.flags & R_JSON_FIELD_SKIP) != 0U) {
                if ((field->json.flags & R_JSON_FIELD_DEFAULT) == 0U &&
                    !r_json_has_implicit_default(context, field->type))
                    return r_body_diagnostic(
                        body,
                        "R-DIAG-JSON-001",
                        "R-JSON-0001",
                        "this optional or excluded field requires an explicit JSON default",
                        field->name_span);
                continue;
            }
            if ((field->json.flags & R_JSON_FIELD_STRING) != 0U &&
                !r_json_field_is_numeric(context, field->type)) {
                const RSemanticType *numeric_type =
                    r_semantic_type(context, r_semantic_value_type(context, field->type));
                RTypeId numeric_id =
                    numeric_type->kind == R_SEMANTIC_TYPE_OPTION ? numeric_type->base : field->type;
                const RSemanticAggregate *converter =
                    r_semantic_aggregate_for_type(context, numeric_id);
                if (converter == NULL || converter->json_unmarshal_function == 0U)
                    return r_json_attribute_diagnostic(context,
                                                       field->name_span,
                                                       "JSON string requires a numeric field or a "
                                                       "custom converter for this direction");
            }
            if (!r_json_decode_type_visit(body, field->type, field->name_span, depth + 1U, &visit))
                return false;
            if ((field->json.flags & (R_JSON_FIELD_OPTIONAL | R_JSON_FIELD_SKIP)) != 0U ||
                field->is_protected) {
                if (!r_json_has_implicit_default(context, field->type) &&
                    (field->json.flags & R_JSON_FIELD_DEFAULT) == 0U)
                    return r_body_diagnostic(
                        body,
                        "R-DIAG-JSON-001",
                        "R-JSON-0001",
                        "this optional or excluded field requires an explicit JSON default",
                        field->name_span);
            }
        }
        return true;
    }
    return r_body_diagnostic(
        body, "R-DIAG-JSON-001", "R-JSON-0001", "this type has no automatic JSON decoder", span);
}

static bool r_json_decode_type(RBodyContext *body, RTypeId id, RSourceSpan span, size_t depth) {
    return r_json_decode_type_visit(body, id, span, depth, NULL);
}

/* Concrete compiler-known JSON call adapters. */
static bool r_json_call(RBodyContext *body,
                        RSourceSpan span,
                        uint32_t operation,
                        RTypeId value_type,
                        RTypeId result_type,
                        RHirVector *arguments,
                        bool checked,
                        bool already_checked,
                        RExpressionResult *result) {
    RFrontendContext *context = body->frontend;
    RSemanticSymbol function = {0};
    RSymbolId function_id;
    RHirNode function_node = {0};
    RHirNodeId function_node_id;
    RHirVector children = {0};
    RTypeId effects = R_TYPE_ID_INVALID;
    char name[96];
    size_t index;
    bool success = false;
    if (already_checked) {
        if ((operation == R_JSON_OPERATION_MARSHAL ||
             operation == R_JSON_OPERATION_MARSHAL_WITH_OPTIONS) &&
            !r_json_encode_type(body, value_type, span, 0U))
            return false;
        if ((operation == R_JSON_OPERATION_UNMARSHAL ||
             operation == R_JSON_OPERATION_UNMARSHAL_WITH_OPTIONS ||
             operation == R_JSON_OPERATION_NEW_DECODER ||
             operation == R_JSON_OPERATION_READ_NEXT) &&
            !r_json_decode_type(body, value_type, span, 0U))
            return false;
    }
    if (checked) {
        RTypeId error_types[2];
        RTypeVector errors = {error_types, 2U, 2U};
        bool duplicate;
        if (operation == R_JSON_OPERATION_READ_NEXT) {
            errors.count = 1U;
            if (!r_semantic_intern_named_standard_type(
                    context, "std.async::start_error", &error_types[0]))
                return false;
        } else if (!r_semantic_intern_named_standard_type(
                       context, "std.alloc::alloc_error", &error_types[0]) ||
                   !r_semantic_intern_named_standard_type(
                       context, "std.json::error", &error_types[1]))
            return false;
        if (!r_semantic_intern_effect_set(context, &errors, &effects, &duplicate) ||
            !r_semantic_intern_effect_carrier(
                context, result_type, effects, &function.effect_carrier_type) ||
            (!already_checked && !r_body_validate_effect_set(body, effects, span))) {
            return false;
        }
    }
    if (context->semantic_parameter_type_count > UINT32_MAX - arguments->count ||
        !r_grow_array(context,
                      (void **)&context->semantic_parameter_types,
                      &context->semantic_parameter_type_capacity,
                      sizeof(*context->semantic_parameter_types),
                      context->semantic_parameter_type_count + arguments->count)) {
        return false;
    }
    function.json_operation = operation;
    function.json_type = value_type;
    (void)snprintf(name, sizeof(name), "$json_call_%u_%u", span.start, operation);
    if (!r_intern_bytes(context, (const uint8_t *)name, strlen(name), &function.name_intern_id)) {
        return false;
    }
    RTypeId key_items[5] = {value_type, result_type};
    RTypeVector key = {key_items, 2U, 5U};
    for (index = 0U; index < arguments->count; ++index)
        key.items[key.count++] = r_body_hir_node(body, arguments->items[index])->type;
    if (!r_generic_instance_name(context, function.name_intern_id, &key, &function.name_intern_id))
        return false;
    for (index = 0U; index < context->semantic_symbol_count; ++index) {
        const RSemanticSymbol *existing = &context->semantic_symbols[index];
        if (existing->json_operation == operation && existing->module_source == body->source &&
            existing->name_intern_id == function.name_intern_id) {
            function = *existing;
            function_id = (RSymbolId)index + 1U;
            goto emit_call;
        }
    }
    function.kind = R_SEMANTIC_SYMBOL_FUNCTION;
    function.name_span = span;
    function.module_source = body->source;
    function.return_type = result_type;
    function.throws_type = effects;
    function.is_protected = true;
    function.has_definition = true;
    function.signature_supported = true;
    function.first_parameter_type = (uint32_t)context->semantic_parameter_type_count;
    function.parameter_count = (uint32_t)arguments->count;
    for (index = 0U; index < arguments->count; ++index) {
        context->semantic_parameter_types[context->semantic_parameter_type_count++] =
            r_body_hir_node(body, arguments->items[index])->type;
    }
    if (!r_semantic_append_symbol(context, function, &function_id)) {
        return false;
    }
    for (index = 0U; index < arguments->count; ++index) {
        RSemanticSymbol parameter = {0};
        RSymbolId parameter_id;
        RHirNode parameter_node = {0};
        RHirNodeId node;
        parameter.kind = R_SEMANTIC_SYMBOL_PARAMETER;
        parameter.integer_value = (uint64_t)index;
        parameter.name_span = span;
        parameter.module_source = body->source;
        parameter.parent = function_id;
        parameter.type = context->semantic_parameter_types[function.first_parameter_type + index];
        parameter.signature_supported = true;
        parameter.object_state = R_SEMANTIC_OBJECT_STATE_INITIALIZED;
        (void)snprintf(name, sizeof(name), "$argument_%zu", index);
        if (!r_intern_bytes(
                context, (const uint8_t *)name, strlen(name), &parameter.name_intern_id) ||
            !r_semantic_append_symbol(context, parameter, &parameter_id)) {
            goto cleanup;
        }
        parameter_node.kind = R_HIR_PARAMETER;
        parameter_node.span = span;
        parameter_node.type = parameter.type;
        parameter_node.symbol = parameter_id;
        if (!r_hir_append_node(context, parameter_node, &node) ||
            !r_hir_vector_push(context, &children, node)) {
            goto cleanup;
        }
    }
    {
        RHirNode block = {0};
        RHirNodeId node;
        block.kind = R_HIR_BLOCK;
        block.span = span;
        if (!r_semantic_type_from_token(context, R_TOKEN_KW_VOID, &block.type)) {
            goto cleanup;
        }
        if (!r_hir_append_node(context, block, &node) ||
            !r_hir_vector_push(context, &children, node)) {
            goto cleanup;
        }
    }
    function_node.kind = R_HIR_FUNCTION;
    function_node.span = span;
    function_node.symbol = function_id;
    function_node.type = result_type;
    if (!r_hir_append_node(context, function_node, &function_node_id) ||
        !r_hir_commit_children(context, function_node_id, &children)) {
        goto cleanup;
    }
    context->semantic_symbols[function_id - 1U].hir_node = function_node_id;
emit_call:
    if (!r_body_append_node(body,
                            R_HIR_CALL,
                            span,
                            result_type,
                            function_id,
                            R_TOKEN_INVALID,
                            0U,
                            false,
                            arguments,
                            &result->node)) {
        goto cleanup;
    }
    r_body_hir_node(body, result->node)->auxiliary_type = function.effect_carrier_type;
    r_body_hir_node(body, result->node)->borrow_origin = R_SYMBOL_ID_INVALID;
    if (checked && !already_checked &&
        !r_body_attach_effect_exits(body, result->node, effects, span)) {
        goto cleanup;
    }
    result->type = result_type;
    result->symbol = function_id;
    result->borrow_origin = R_SYMBOL_ID_INVALID;
    result->is_place = false;
    result->valid = true;
    success = true;
cleanup:
    r_context_free(context, children.items);
    return success;
}

#include "json_reader.inc"
#include "json_stream.inc"
#include "json_tree.inc"

static bool r_json_operation(RBodyContext *body,
                             RAstRef name_ref,
                             RAstRef arguments_ref,
                             RSourceSpan span,
                             RTypeId contextual_type,
                             bool *handled,
                             RExpressionResult *result) {
    static const struct {
        const char *name;
        RJsonOperation operation;
    } operations[] = {{"marshal", R_JSON_OPERATION_MARSHAL},
                      {"marshal_with_options", R_JSON_OPERATION_MARSHAL_WITH_OPTIONS},
                      {"unmarshal", R_JSON_OPERATION_UNMARSHAL},
                      {"unmarshal_with_options", R_JSON_OPERATION_UNMARSHAL_WITH_OPTIONS},
                      {"parse_with_options", R_JSON_OPERATION_PARSE_WITH_OPTIONS},
                      {"stringify_with_options", R_JSON_OPERATION_STRINGIFY_WITH_OPTIONS},
                      {"parse", R_JSON_OPERATION_PARSE},
                      {"stringify", R_JSON_OPERATION_STRINGIFY},
                      {"kind", R_JSON_OPERATION_KIND},
                      {"len", R_JSON_OPERATION_LEN},
                      {"text", R_JSON_OPERATION_TEXT},
                      {"boolean", R_JSON_OPERATION_BOOLEAN},
                      {"null", R_JSON_OPERATION_NULL},
                      {"from_bool", R_JSON_OPERATION_FROM_BOOL},
                      {"from_string", R_JSON_OPERATION_FROM_STRING},
                      {"from_number", R_JSON_OPERATION_FROM_NUMBER},
                      {"array", R_JSON_OPERATION_ARRAY},
                      {"object", R_JSON_OPERATION_OBJECT},
                      {"parse_number", R_JSON_OPERATION_PARSE_NUMBER},
                      {"number_text", R_JSON_OPERATION_NUMBER_TEXT},
                      {"number_is_zero", R_JSON_OPERATION_NUMBER_IS_ZERO},
                      {"name_equal", R_JSON_OPERATION_NAME_EQUAL},
                      {"get", R_JSON_OPERATION_GET},
                      {"find", R_JSON_OPERATION_FIND},
                      {"key_at", R_JSON_OPERATION_KEY_AT},
                      {"append", R_JSON_OPERATION_APPEND},
                      {"insert", R_JSON_OPERATION_INSERT},
                      {"take_index", R_JSON_OPERATION_TAKE_INDEX},
                      {"take_field", R_JSON_OPERATION_TAKE_FIELD},
                      {"new_decoder", R_JSON_OPERATION_NEW_DECODER},
                      {"feed", R_JSON_OPERATION_FEED},
                      {"take", R_JSON_OPERATION_TAKE},
                      {"new_reader", R_JSON_OPERATION_NEW_READER},
                      {"read_next", R_JSON_OPERATION_READ_NEXT},
                      {"detach", R_JSON_OPERATION_DETACH},
                      {"take_handle", R_JSON_OPERATION_TAKE_HANDLE},
                      {"take_bytes", R_JSON_OPERATION_TAKE_BYTES}};
    RFrontendContext *context = body->frontend;
    uint32_t name;
    RJsonOperation operation = R_JSON_OPERATION_INVALID;
    RTypeId argument_type = 0U, result_type = 0U, json_value, json_number, string, byte;
    RAstNodeView arguments;
    RHirNodeId storage[2];
    RHirVector children = {storage, 0U, 2U};
    RExpressionResult value = {0};
    bool checked = true;
    size_t arity = 1U;
    bool with_options;
    *handled = false;
    if (!r_semantic_standard_name(context, name_ref, &name))
        return context->resource_status == R_FRONTEND_OK;
    for (size_t i = 0U; i < sizeof(operations) / sizeof(operations[0]); ++i) {
        char qualified[80];
        (void)snprintf(qualified, sizeof(qualified), "std.json::%s", operations[i].name);
        if (r_semantic_intern_name_equal(context, name, qualified)) {
            operation = operations[i].operation;
            break;
        }
    }
    if (operation == R_JSON_OPERATION_INVALID)
        return true;
    *handled = true;
    if (operation >= R_JSON_OPERATION_NEW_READER && operation <= R_JSON_OPERATION_TAKE_BYTES)
        return r_json_reader_call(body, arguments_ref, span, operation, contextual_type, result);
    if (operation >= R_JSON_OPERATION_NEW_DECODER && operation <= R_JSON_OPERATION_TAKE)
        return r_json_stream_call(body, arguments_ref, span, operation, contextual_type, result);
    if (operation >= R_JSON_OPERATION_NAME_EQUAL && operation <= R_JSON_OPERATION_TAKE_FIELD)
        return r_json_tree_call(body, arguments_ref, span, operation, result);
    with_options = operation == R_JSON_OPERATION_MARSHAL_WITH_OPTIONS ||
                   operation == R_JSON_OPERATION_UNMARSHAL_WITH_OPTIONS ||
                   operation == R_JSON_OPERATION_PARSE_WITH_OPTIONS ||
                   operation == R_JSON_OPERATION_STRINGIFY_WITH_OPTIONS;
    if (with_options)
        arity = 2U;
    if (!r_semantic_intern_named_standard_type(context, "std.json::value", &json_value) ||
        !r_semantic_intern_named_standard_type(context, "std.json::number", &json_number) ||
        !r_semantic_intern_named_standard_type(context, "std.string::string", &string) ||
        !r_semantic_type_from_token(context, R_TOKEN_KW_U8, &byte) ||
        !r_ast_ref_view(context, arguments_ref, &arguments))
        return false;
    if (operation == R_JSON_OPERATION_MARSHAL ||
        operation == R_JSON_OPERATION_MARSHAL_WITH_OPTIONS) {
        RAstRef argument;
        RExpressionResult borrowed = {0};
        const RSemanticType *pointer;
        RTypeId value_type;
        size_t diagnostics = context->diagnostic_count;
        if (arguments.child_count != arity)
            return r_body_diagnostic(
                body, "R-DIAG-JSON-001", "R-JSON-0001", "wrong number of JSON arguments", span);
        if (!r_ast_ref_child(context, arguments_ref, 0U, &argument) ||
            !r_generic_lower_argument(body, argument, R_TYPE_ID_INVALID, R_SEMANTIC_TYPE_FLAG_SHARED, &borrowed) ||
            !r_body_place_to_value(body, &borrowed))
            return false;
        if (!borrowed.valid)
            return true;
        pointer = r_semantic_type(context, borrowed.type);
        if (pointer->kind != R_SEMANTIC_TYPE_BORROW ||
            (pointer->flags & R_SEMANTIC_TYPE_FLAG_NULLABLE) != 0U)
            return r_body_diagnostic(body,
                                     "R-DIAG-JSON-001",
                                     "R-JSON-0001",
                                     "JSON marshal requires a non-null borrow",
                                     span);
        value_type = pointer->base;
        RTypeId shared_type;
        if (!r_semantic_intern_derived_type(context,
                                            R_SEMANTIC_TYPE_BORROW,
                                            value_type,
                                            0U,
                                            0U,
                                            R_SEMANTIC_TYPE_FLAG_SHARED,
                                            &shared_type) ||
            !r_body_finish_value(body, shared_type, &borrowed))
            return false;
        if (!borrowed.valid)
            return true;

        if (!r_json_encode_type(body, value_type, span, 0U))
            return false;
        if (context->diagnostic_count != diagnostics)
            return true;
        children.items[children.count++] = borrowed.node;
        if (with_options) {
            RTypeId options_type;
            RExpressionResult options = {0};
            if (!r_semantic_intern_named_standard_type(
                    context, "std.json::options", &options_type) ||
                !r_semantic_prepare_json_schema(context, options_type) ||
                !r_ast_ref_child(context, arguments_ref, 1U, &argument) ||
                !r_body_lower_call_bounded_value(body, argument, options_type, &options))
                return false;
            if (!options.valid)
                return true;
            if (options.type != options_type)
                return r_body_diagnostic(body,
                                         "R-DIAG-JSON-001",
                                         "R-JSON-0001",
                                         "JSON options type does not match",
                                         span);
            children.items[children.count++] = options.node;
        }
        return r_json_call(
            body, span, operation, value_type, string, &children, true, false, result);
    }
    result_type = json_value;
    switch (operation) {
    case R_JSON_OPERATION_NULL:
        checked = false;
        arity = 0U;
        break;
    case R_JSON_OPERATION_ARRAY:
    case R_JSON_OPERATION_OBJECT:
        arity = 0U;
        break;
    case R_JSON_OPERATION_FROM_BOOL:
        if (!r_semantic_type_from_token(context, R_TOKEN_KW_BOOL, &argument_type))
            return false;
        break;
    case R_JSON_OPERATION_UNMARSHAL_WITH_OPTIONS:
    case R_JSON_OPERATION_PARSE_WITH_OPTIONS:
    case R_JSON_OPERATION_UNMARSHAL:
    case R_JSON_OPERATION_PARSE:
    case R_JSON_OPERATION_PARSE_NUMBER:
    case R_JSON_OPERATION_FROM_STRING:
        if (!r_semantic_intern_derived_type(context,
                                            R_SEMANTIC_TYPE_SLICE,
                                            byte,
                                            0U,
                                            0U,
                                            R_SEMANTIC_TYPE_FLAG_SHARED,
                                            &argument_type))
            return false;
        if (operation == R_JSON_OPERATION_UNMARSHAL ||
            operation == R_JSON_OPERATION_UNMARSHAL_WITH_OPTIONS) {
            if (contextual_type == R_TYPE_ID_INVALID)
                return r_body_diagnostic(
                    body,
                    "R-DIAG-JSON-001",
                    "R-JSON-0001",
                    "JSON unmarshal requires a complete initializer or assignment result type",
                    span);
            size_t diagnostic_count = context->diagnostic_count;
            if (!r_json_decode_type(body, contextual_type, span, 0U))
                return false;
            if (context->diagnostic_count != diagnostic_count)
                return true;
            result_type = contextual_type;
        }
        if (operation == R_JSON_OPERATION_PARSE_NUMBER)
            result_type = json_number;
        break;
    default:
        if (!r_semantic_intern_derived_type(context,
                                            R_SEMANTIC_TYPE_BORROW,
                                            operation == R_JSON_OPERATION_FROM_NUMBER ||
                                                    operation == R_JSON_OPERATION_NUMBER_TEXT ||
                                                    operation == R_JSON_OPERATION_NUMBER_IS_ZERO
                                                ? json_number
                                                : json_value,
                                            0U,
                                            0U,
                                            R_SEMANTIC_TYPE_FLAG_SHARED,
                                            &argument_type))
            return false;
        if (operation == R_JSON_OPERATION_STRINGIFY ||
            operation == R_JSON_OPERATION_STRINGIFY_WITH_OPTIONS)
            result_type = string;
        else if (operation == R_JSON_OPERATION_FROM_NUMBER)
            result_type = json_value;
        else {
            checked = false;
            if (operation == R_JSON_OPERATION_KIND) {
                if (!r_semantic_intern_named_standard_type(
                        context, "std.json::value_kind", &result_type))
                    return false;
            } else if (!r_semantic_type_from_token(
                           context,
                           operation == R_JSON_OPERATION_LEN
                               ? R_TOKEN_KW_USIZE
                               : (operation == R_JSON_OPERATION_TEXT ||
                                          operation == R_JSON_OPERATION_NUMBER_TEXT
                                      ? R_TOKEN_KW_STR
                                      : R_TOKEN_KW_BOOL),
                           &result_type))
                return false;
        }
        break;
    }
    if (arguments.child_count != arity)
        return r_body_diagnostic(
            body, "R-DIAG-JSON-001", "R-JSON-0001", "wrong number of JSON arguments", span);
    if (arity != 0U) {
        RAstRef argument;
        if (!r_ast_ref_child(context, arguments_ref, 0U, &argument) ||
            !r_body_lower_call_bounded_value(body, argument, argument_type, &value))
            return false;
        if (!value.valid)
            return true;
        if (value.type != argument_type)
            return r_body_diagnostic(
                body, "R-DIAG-JSON-001", "R-JSON-0001", "JSON argument type does not match", span);
        children.items[children.count++] = value.node;
    }
    if (with_options) {
        RAstRef argument;
        RTypeId options_type;
        RExpressionResult options = {0};
        if (!r_semantic_intern_named_standard_type(context, "std.json::options", &options_type) ||
            !r_semantic_prepare_json_schema(context, options_type) ||
            !r_ast_ref_child(context, arguments_ref, 1U, &argument) ||
            !r_body_lower_call_bounded_value(body, argument, options_type, &options))
            return false;
        if (!options.valid)
            return true;
        if (options.type != options_type)
            return r_body_diagnostic(
                body, "R-DIAG-JSON-001", "R-JSON-0001", "JSON options type does not match", span);
        children.items[children.count++] = options.node;
    }
    if (!r_json_call(
            body, span, operation, result_type, result_type, &children, checked, false, result))
        return false;
    if (operation == R_JSON_OPERATION_TEXT || operation == R_JSON_OPERATION_NUMBER_TEXT) {
        result->borrow_origin = value.borrow_origin;
        result->borrow_origin_set = value.borrow_origin_set;
        result->borrow_origin_multiple = value.borrow_origin_multiple;
        r_body_hir_node(body, result->node)->borrow_origin = value.borrow_origin;
        r_body_hir_node(body, result->node)->borrow_origin_set = value.borrow_origin_set;
        r_body_hir_node(body, result->node)->borrow_origin_multiple = value.borrow_origin_multiple;
        context->semantic_symbols[result->symbol - 1U].return_borrow_parameter = 1U;
        context->semantic_symbols[result->symbol - 1U].return_borrow_parameters_low = UINT64_C(1);
    }
    return true;
}

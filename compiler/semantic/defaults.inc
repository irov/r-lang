/* R-INIT-0004, R-INIT-0005 (L33): default values. A zero default (numeric zero, false, U+0000,
 * null, o::none, and arrays and structs of them whose fields declare no initializer) is one
 * DEFAULT_VALUE node. Every other default is evaluated where it is needed: a struct whose fields
 * each declare an initializer or have a default takes the value of `Type {}`, an enum with a
 * `@default` variant takes that variant with the default of its payload, and a fixed array the
 * default of each element. A field initializer is lowered where it is used, anew at every use,
 * with the names of the module of its aggregate and none of the enclosing locals; its checked
 * errors are effects of the initialization that uses it. */

static bool r_semantic_type_has_default_value_at(const RFrontendContext *context,
                                                 RTypeId type_id,
                                                 uint32_t depth);

static bool r_semantic_aggregate_has_default_value(const RFrontendContext *context,
                                                   const RSemanticAggregate *aggregate,
                                                   uint32_t depth) {
    if ((aggregate == NULL) || aggregate->poisoned || !aggregate->complete ||
        aggregate->is_opaque) {
        return false;
    }
    if (aggregate->kind == R_SEMANTIC_AGGREGATE_STRUCT) {
        for (uint32_t index = 0U; index < aggregate->field_count; ++index) {
            const RSemanticField *field =
                &context->semantic_fields[aggregate->first_field + index];
            if ((field->initializer.node == 0U) &&
                !r_semantic_type_has_default_value_at(context, field->type, depth + 1U)) {
                return false;
            }
        }
        return true;
    }
    if (aggregate->default_variant == 0U) {
        return false;
    }
    const RSemanticVariant *variant =
        &context->semantic_variants[aggregate->first_variant + aggregate->default_variant - 1U];
    return (variant->payload_type == R_TYPE_ID_INVALID) ||
           r_semantic_type_has_default_value_at(context, variant->payload_type, depth + 1U);
}

static bool r_semantic_type_has_default_value_at(const RFrontendContext *context,
                                                 RTypeId type_id,
                                                 uint32_t depth) {
    const RTypeId value = r_semantic_value_type(context, type_id);
    const RSemanticType *type = r_semantic_type(context, value);

    if ((type == NULL) || (depth > context->options.limits.max_nesting)) {
        return false;
    }
    if (r_semantic_type_has_default(context, value)) {
        return true;
    }
    switch (type->kind) {
    case R_SEMANTIC_TYPE_STRUCT:
    case R_SEMANTIC_TYPE_ENUM:
        return r_semantic_aggregate_has_default_value(
            context, r_semantic_aggregate_for_type(context, value), depth);
    case R_SEMANTIC_TYPE_FIXED_ARRAY:
        return r_semantic_type_has_default_value_at(context, type->base, depth + 1U);
    default:
        return false;
    }
}

/* Whether `type_id` has a default, zero or evaluated (R-INIT-0005). */
static bool r_semantic_type_has_default_value(const RFrontendContext *context, RTypeId type_id) {
    return r_semantic_type_has_default_value_at(context, type_id, 0U);
}

/* The value of the declared initializer of the field `field_index` (global), typed as the
 * field's value type in the aggregate being initialized. The initializer was checked at its
 * declaration; a diagnostic of this use, such as an undeclared checked error, is reported at the
 * initialization `span`. */
static bool r_body_lower_field_initializer(RBodyContext *body,
                                           uint32_t field_index,
                                           RTypeId field_type,
                                           RSourceSpan span,
                                           RExpressionResult *result) {
    RFrontendContext *context = body->frontend;
    const RSemanticField field = context->semantic_fields[field_index];
    const RSourceId source = body->source;
    const size_t floor = body->symbol_floor;
    const bool module_context = body->module_context;
    const RSourceSpan use_site = body->use_site;
    const bool use_site_set = body->use_site_set;
    bool success;

    r_body_expression_invalid(result);
    if (field.initializer_invalid) {
        r_body_poison(body);
        return true;
    }
    body->source = field.initializer.source;
    body->symbol_floor = body->active_symbols.count;
    body->module_context = true;
    if (!use_site_set) {
        body->use_site = span;
        body->use_site_set = field.initializer_checked;
    }
    success = r_body_lower_value(body, field.initializer, field_type, result);
    body->source = source;
    body->symbol_floor = floor;
    body->module_context = module_context;
    body->use_site = use_site;
    body->use_site_set = use_site_set;
    if (!success || !result->valid) {
        return success;
    }
    if (r_semantic_value_type(context, result->type) != r_semantic_value_type(context, field_type)) {
        r_body_expression_invalid(result);
        return r_body_diagnostic(body,
                                 "R-DIAG-TYPE-001",
                                 "R-INIT-0004",
                                 "field initializer type does not match the field type",
                                 field.name_span);
    }
    return true;
}

static bool r_body_lower_default_value_at(RBodyContext *body,
                                          RTypeId type_id,
                                          RSourceSpan span,
                                          uint32_t depth,
                                          RExpressionResult *result);

/* The value of an omitted field `field_index` (global): its initializer, else its default. */
static bool r_body_lower_member_default(RBodyContext *body,
                                        uint32_t field_index,
                                        RTypeId field_type,
                                        RSourceSpan span,
                                        uint32_t depth,
                                        RExpressionResult *result) {
    if (body->frontend->semantic_fields[field_index].initializer.node != 0U) {
        return r_body_lower_field_initializer(body, field_index, field_type, span, result);
    }
    return r_body_lower_default_value_at(body, field_type, span, depth + 1U, result);
}

/* `Type {}` of a struct with an evaluated default: each field in declaration order. */
static bool r_body_lower_struct_default(RBodyContext *body,
                                        const RSemanticAggregate *aggregate,
                                        RTypeId value_type,
                                        RSourceSpan span,
                                        uint32_t depth,
                                        RExpressionResult *result) {
    RHirVector fields = {0};
    const uint32_t first_field = aggregate->first_field;
    const uint32_t field_count = aggregate->field_count;
    RSymbolId origin = R_SYMBOL_ID_INVALID;
    bool success = false;
    bool valid = true;

    for (uint32_t index = 0U; index < field_count; ++index) {
        const RTypeId field_type = r_semantic_value_type(
            body->frontend, body->frontend->semantic_fields[first_field + index].type);
        RExpressionResult value;
        RHirNodeId field_init_id;
        RHirNodeId storage[1];
        RHirVector children = {storage, 1U, 1U};

        if (!r_body_lower_member_default(
                body, first_field + index, field_type, span, depth, &value)) {
            goto cleanup;
        }
        if (!value.valid) {
            valid = false;
            continue;
        }
        if (!r_body_place_to_value(body, &value)) {
            goto cleanup;
        }
        storage[0] = value.node;
        if (!r_body_append_node(body,
                                R_HIR_FIELD_INIT,
                                span,
                                field_type,
                                R_SYMBOL_ID_INVALID,
                                R_TOKEN_EQUAL,
                                UINT64_C(0),
                                false,
                                &children,
                                &field_init_id)) {
            goto cleanup;
        }
        r_body_hir_node(body, field_init_id)->aggregate_member = first_field + index + 1U;
        if (value.borrow_origin != R_SYMBOL_ID_INVALID) {
            origin = value.borrow_origin;
        }
        if (!r_hir_vector_push(body->frontend, &fields, field_init_id)) {
            goto cleanup;
        }
    }
    r_body_expression_invalid(result);
    if (valid) {
        RHirNodeId node_id;
        if (!r_body_append_node(body,
                                R_HIR_AGGREGATE_INIT,
                                span,
                                value_type,
                                R_SYMBOL_ID_INVALID,
                                R_TOKEN_INVALID,
                                UINT64_C(0),
                                false,
                                &fields,
                                &node_id)) {
            goto cleanup;
        }
        r_body_hir_node(body, node_id)->borrow_origin = origin;
        result->node = node_id;
        result->type = value_type;
        result->borrow_origin = origin;
        result->valid = true;
    }
    success = true;
cleanup:
    r_context_free(body->frontend, fields.items);
    return success;
}

/* The `@default` variant of an enum, with the default of its payload. */
static bool r_body_lower_enum_default(RBodyContext *body,
                                      const RSemanticAggregate *aggregate,
                                      RTypeId value_type,
                                      RSourceSpan span,
                                      uint32_t depth,
                                      RExpressionResult *result) {
    const uint32_t global_index = aggregate->first_variant + aggregate->default_variant - 1U;
    const RSemanticVariant variant = body->frontend->semantic_variants[global_index];
    const bool tagged = aggregate->is_tagged;
    RExpressionResult payload;
    RHirNodeId storage[1];
    RHirVector children = {storage, 1U, 1U};
    RHirNodeId node_id;

    r_body_expression_invalid(result);
    if (variant.payload_type == R_TYPE_ID_INVALID) {
        if (!r_body_append_node(body,
                                tagged ? R_HIR_VARIANT : R_HIR_ENUM_CONSTANT,
                                span,
                                value_type,
                                R_SYMBOL_ID_INVALID,
                                R_TOKEN_INVALID,
                                variant.value,
                                false,
                                NULL,
                                &node_id)) {
            return false;
        }
    } else {
        if (!r_body_lower_default_value_at(body, variant.payload_type, span, depth + 1U, &payload)) {
            return false;
        }
        if (!payload.valid) {
            return true;
        }
        if (!r_body_place_to_value(body, &payload)) {
            return false;
        }
        storage[0] = payload.node;
        if (!r_body_append_node(body,
                                R_HIR_VARIANT,
                                span,
                                value_type,
                                R_SYMBOL_ID_INVALID,
                                R_TOKEN_INVALID,
                                (uint64_t)variant.declaration_index,
                                false,
                                &children,
                                &node_id)) {
            return false;
        }
        r_body_hir_node(body, node_id)->borrow_origin = payload.borrow_origin;
        result->borrow_origin = payload.borrow_origin;
    }
    r_body_hir_node(body, node_id)->aggregate_member = global_index + 1U;
    result->node = node_id;
    result->type = value_type;
    result->valid = true;
    return true;
}

/* A fixed array of evaluated defaults: the default of every element in order. */
static bool r_body_lower_array_default(RBodyContext *body,
                                       const RSemanticType *array_type,
                                       RTypeId value_type,
                                       RSourceSpan span,
                                       uint32_t depth,
                                       RExpressionResult *result) {
    RHirVector elements = {0};
    const RTypeId element_type = array_type->base;
    const uint64_t length = array_type->length;
    bool success = false;
    bool valid = true;

    r_body_expression_invalid(result);
    if (length > (uint64_t)body->frontend->options.limits.max_nesting * UINT64_C(1024)) {
        return r_body_diagnostic(body,
                                 "R-DIAG-LIMIT-001",
                                 "R-INIT-0005",
                                 "a fixed array of evaluated defaults exceeds the element limit",
                                 span);
    }
    for (uint64_t index = 0U; index < length; ++index) {
        RExpressionResult element;
        if (!r_body_lower_default_value_at(body, element_type, span, depth + 1U, &element)) {
            goto cleanup;
        }
        if (!element.valid) {
            valid = false;
            break;
        }
        if (!r_body_place_to_value(body, &element) ||
            !r_hir_vector_push(body->frontend, &elements, element.node)) {
            goto cleanup;
        }
    }
    if (valid) {
        RHirNodeId node_id;
        if (!r_body_append_node(body,
                                R_HIR_ARRAY_INIT,
                                span,
                                value_type,
                                R_SYMBOL_ID_INVALID,
                                R_TOKEN_INVALID,
                                length,
                                false,
                                &elements,
                                &node_id)) {
            goto cleanup;
        }
        result->node = node_id;
        result->type = value_type;
        result->valid = true;
    }
    success = true;
cleanup:
    r_context_free(body->frontend, elements.items);
    return success;
}

static bool r_body_lower_default_value_at(RBodyContext *body,
                                          RTypeId type_id,
                                          RSourceSpan span,
                                          uint32_t depth,
                                          RExpressionResult *result) {
    RFrontendContext *context = body->frontend;
    const RTypeId value_type = r_semantic_value_type(context, type_id);
    const RSemanticType *type = r_semantic_type(context, value_type);
    RHirNodeId node_id;

    r_body_expression_invalid(result);
    if ((type == NULL) || (depth > context->options.limits.max_nesting)) {
        return r_body_diagnostic(
            body, "R-DIAG-INIT-002", "R-INIT-0005", "the type has no default value", span);
    }
    if (r_semantic_type_has_default(context, value_type)) {
        if (!r_body_append_node(body,
                                R_HIR_DEFAULT_VALUE,
                                span,
                                value_type,
                                R_SYMBOL_ID_INVALID,
                                R_TOKEN_INVALID,
                                UINT64_C(0),
                                false,
                                NULL,
                                &node_id)) {
            return false;
        }
        result->node = node_id;
        result->type = value_type;
        result->valid = true;
        return true;
    }
    if (!r_semantic_type_has_default_value_at(context, value_type, depth)) {
        return r_body_diagnostic(
            body, "R-DIAG-INIT-002", "R-INIT-0005", "the type has no default value", span);
    }
    if (type->kind == R_SEMANTIC_TYPE_FIXED_ARRAY) {
        const RSemanticType array = *type;
        return r_body_lower_array_default(body, &array, value_type, span, depth, result);
    }
    const RSemanticAggregate aggregate = *r_semantic_aggregate_for_type(context, value_type);
    return aggregate.kind == R_SEMANTIC_AGGREGATE_STRUCT
               ? r_body_lower_struct_default(body, &aggregate, value_type, span, depth, result)
               : r_body_lower_enum_default(body, &aggregate, value_type, span, depth, result);
}

/* The default value of `type_id` where an initialization needs one (R-INIT-0005). */
static bool r_body_lower_default_value(RBodyContext *body,
                                       RTypeId type_id,
                                       RSourceSpan span,
                                       RExpressionResult *result) {
    return r_body_lower_default_value_at(body, type_id, span, 0U, result);
}

/* R-INIT-0004 (L33): each declared field initializer is checked once, in the module of its
 * aggregate, as a value of the field type that names no local; its checked errors belong to
 * each initialization that uses it. A field whose aggregate is generic is checked in each
 * instance where an initialization uses it. */
static bool r_semantic_check_field_initializers(RFrontendContext *context) {
    for (size_t index = 0U; index < context->semantic_aggregate_count; ++index) {
        const RSemanticAggregate aggregate = context->semantic_aggregates[index];
        if ((aggregate.generic_origin != 0U) || (aggregate.generic_schema != 0U) ||
            aggregate.poisoned || !aggregate.complete) {
            continue;
        }
        for (uint32_t field_index = 0U; field_index < aggregate.field_count; ++field_index) {
            const size_t global = (size_t)aggregate.first_field + field_index;
            const RSemanticField field = context->semantic_fields[global];
            const size_t diagnostics = context->diagnostic_count;
            RBodyContext body = {0};
            RExpressionResult value;
            bool lowered;

            if ((field.initializer.node == 0U) || field.initializer_checked) {
                continue;
            }
            body.frontend = context;
            body.source = field.initializer.source;
            body.translation_effects = true;
            body.module_context = true;
            lowered = r_body_lower_value(
                &body, field.initializer, r_semantic_value_type(context, field.type), &value);
            r_context_free(context, body.active_symbols.items);
            if (!lowered) {
                return false;
            }
            context->semantic_fields[global].initializer_checked = true;
            if (!value.valid || body.poisoned || (context->diagnostic_count != diagnostics)) {
                context->semantic_fields[global].initializer_invalid = true;
                continue;
            }
            if (r_semantic_value_type(context, value.type) !=
                r_semantic_value_type(context, field.type)) {
                context->semantic_fields[global].initializer_invalid = true;
                if (!r_semantic_add_diagnostic(context,
                                               "R-DIAG-TYPE-001",
                                               "R-INIT-0004",
                                               "field initializer type does not match the field "
                                               "type",
                                               field.name_span)) {
                    return false;
                }
            }
        }
    }
    return true;
}

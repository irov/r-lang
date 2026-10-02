/* R-TYPE-0052 (L18.1): tuples. The tuple type `(T1, ..., Tn)` is the instance of a synthesized
 * generic struct of arity n whose fields, named `0` to `n - 1`, are the elements, so its layout,
 * copies, moves, cleanup and element borrows are those of a generic struct instance, and
 * substitution and inference see through it. */

/* The synthesized generic struct of the tuples of `arity` elements. */
static bool r_tuple_origin(RFrontendContext *context, uint32_t arity, uint32_t *origin_id) {
    RGenericSchema schema = {0};
    RSemanticAggregate aggregate = {0};
    const uint32_t aggregate_id = (uint32_t)context->semantic_aggregate_count + UINT32_C(1);
    char name[24];

    if ((arity < context->tuple_origin_capacity) && (context->tuple_origins[arity] != 0U)) {
        *origin_id = context->tuple_origins[arity];
        return true;
    }
    if (arity >= context->tuple_origin_capacity) {
        const size_t previous = context->tuple_origin_capacity;
        if (!r_grow_array(context,
                          (void **)&context->tuple_origins,
                          &context->tuple_origin_capacity,
                          sizeof(*context->tuple_origins),
                          (size_t)arity + 1U)) {
            return false;
        }
        (void)memset(context->tuple_origins + previous,
                     0,
                     (context->tuple_origin_capacity - previous) * sizeof(*context->tuple_origins));
    }
    schema.first_parameter = (uint32_t)context->generic_parameter_count;
    schema.aggregate = aggregate_id;
    schema.tuple = true;
    for (uint32_t index = 0U; index < arity; ++index) {
        RGenericParameter parameter = {0};
        const int length = snprintf(name, sizeof(name), "T%" PRIu32, index);
        if ((length <= 0) || ((size_t)length >= sizeof(name)) ||
            !r_intern_bytes(
                context, (const uint8_t *)name, (size_t)length, &parameter.name_intern_id) ||
            (context->generic_parameter_count >= UINT32_MAX) ||
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
        context->generic_parameters[context->generic_parameter_count++] = parameter;
        schema.parameter_count += 1U;
    }
    if (!r_grow_array(context,
                      (void **)&context->generic_schemas,
                      &context->generic_schema_capacity,
                      sizeof(schema),
                      context->generic_schema_count + 1U)) {
        return false;
    }
    context->generic_schemas[context->generic_schema_count++] = schema;
    aggregate.kind = R_SEMANTIC_AGGREGATE_STRUCT;
    aggregate.module_source = R_SOURCE_ID_INVALID;
    aggregate.declaration_ast = R_AST_NODE_ID_INVALID;
    aggregate.generic_schema = (uint32_t)context->generic_schema_count;
    aggregate.first_field = (uint32_t)context->semantic_field_count;
    aggregate.field_count = arity;
    aggregate.is_tuple = true;
    aggregate.complete = true;
    aggregate.is_copy = true;
    if (!r_intern_bytes(context, (const uint8_t *)"tuple", 5U, &aggregate.name_intern_id) ||
        !r_grow_array(context,
                      (void **)&context->semantic_fields,
                      &context->semantic_field_capacity,
                      sizeof(*context->semantic_fields),
                      context->semantic_field_count + arity)) {
        return false;
    }
    for (uint32_t index = 0U; index < arity; ++index) {
        RSemanticField field = {0};
        const int length = snprintf(name, sizeof(name), "%" PRIu32, index);
        if ((length <= 0) || ((size_t)length >= sizeof(name)) ||
            !r_intern_bytes(
                context, (const uint8_t *)name, (size_t)length, &field.name_intern_id)) {
            return false;
        }
        field.type = context->generic_parameters[schema.first_parameter + index].type;
        field.aggregate = aggregate_id;
        field.layout_index = index;
        context->semantic_fields[context->semantic_field_count++] = field;
    }
    if (!r_grow_array(context,
                      (void **)&context->semantic_aggregates,
                      &context->semantic_aggregate_capacity,
                      sizeof(aggregate),
                      context->semantic_aggregate_count + 1U) ||
        !r_semantic_intern_type(context, R_SEMANTIC_TYPE_STRUCT, aggregate_id, &aggregate.type)) {
        return false;
    }
    context->semantic_aggregates[context->semantic_aggregate_count++] = aggregate;
    context->tuple_origins[arity] = aggregate_id;
    *origin_id = aggregate_id;
    return true;
}

/* The tuple instance aggregate of `type`, or NULL. */
static const RSemanticAggregate *r_tuple_aggregate(const RFrontendContext *context, RTypeId type) {
    const RSemanticAggregate *aggregate =
        r_semantic_aggregate_for_type(context, r_semantic_value_type(context, type));
    return ((aggregate != NULL) && aggregate->is_tuple && (aggregate->generic_origin != 0U))
               ? aggregate
               : NULL;
}

/* R-TYPE-0052: an element is a complete, sized, inhabited value type without an outer const;
   false after a diagnostic. */
static bool r_tuple_element_valid(RFrontendContext *context, RTypeId element, RSourceSpan span) {
    const RSemanticType *type = r_semantic_type(context, element);
    if ((type == NULL) || (type->kind == R_SEMANTIC_TYPE_CONST) ||
        (type->kind == R_SEMANTIC_TYPE_VOID) || (type->kind == R_SEMANTIC_TYPE_NULL_T) ||
        (type->kind == R_SEMANTIC_TYPE_CONSTANT_EXPR) ||
        (type->kind == R_SEMANTIC_TYPE_EFFECT_SET) ||
        !r_semantic_type_is_inhabited(context, element, 0U) ||
        !r_semantic_type_is_slice_scalar(context, element, false)) {
        (void)r_semantic_add_diagnostic(context,
                                        "R-DIAG-TYPE-001",
                                        "R-TYPE-0052",
                                        "a tuple element shall be a complete sized inhabited "
                                        "value type without outer const",
                                        span);
        return false;
    }
    return true;
}

/* R-TYPE-0052: `(elements)`. */
static bool r_tuple_type(RFrontendContext *context,
                         const RTypeVector *elements,
                         RSourceSpan span,
                         RTypeId *type_id) {
    uint32_t origin;

    *type_id = R_TYPE_ID_INVALID;
    for (size_t index = 0U; index < elements->count; ++index) {
        if (!r_tuple_element_valid(context, elements->items[index], span)) {
            return false;
        }
    }
    return r_tuple_origin(context, (uint32_t)elements->count, &origin) &&
           r_generic_apply_aggregate(context, origin, elements, type_id);
}

/* R-TYPE-0052: the element index a `.N` member suffix selects, from its decimal literal. */
static bool
r_tuple_index(const RFrontendContext *context, const RAstTokenView *token, uint32_t *index) {
    const RSource *source = r_get_source_const(context, token->span.source);
    uint64_t value = 0U;

    if ((source == NULL) || (token->span.end <= token->span.start) ||
        (token->span.end > source->length) ||
        ((token->span.end - token->span.start > 1U) &&
         (source->bytes[token->span.start] == (uint8_t)'0'))) {
        return false;
    }
    for (uint32_t offset = token->span.start; offset < token->span.end; ++offset) {
        const uint8_t digit = source->bytes[offset];
        if ((digit < (uint8_t)'0') || (digit > (uint8_t)'9') || (value > UINT32_MAX / 10U)) {
            return false;
        }
        value = value * 10U + (uint64_t)(digit - (uint8_t)'0');
    }
    if (value > UINT32_MAX) {
        return false;
    }
    *index = (uint32_t)value;
    return true;
}

/* R-TYPE-0052: the tuple of `count` element values already lowered in order; each is moved or
   copied into its element. `valid` turns false after a diagnosed conflict between elements. */
static bool r_tuple_build(RBodyContext *body,
                          RExpressionResult *values,
                          uint32_t count,
                          RSourceSpan span,
                          RExpressionResult *result) {
    RFrontendContext *context = body->frontend;
    RTypeVector elements = {0};
    RAggregateInitState state = {0};
    RTypeId tuple_type;
    const RSemanticAggregate *aggregate;
    RHirNodeId node_id;
    uint32_t projected_borrow_state;
    bool valid = true;
    bool success = false;

    r_body_expression_invalid(result);
    for (uint32_t index = 0U; index < count; ++index) {
        if (!r_type_vector_push(
                context, &elements, r_semantic_value_type(context, values[index].type))) {
            goto cleanup;
        }
    }
    if (!r_tuple_type(context, &elements, span, &tuple_type)) {
        success = context->resource_status == R_FRONTEND_OK;
        goto cleanup;
    }
    aggregate = r_tuple_aggregate(context, tuple_type);
    if ((aggregate == NULL) || !aggregate->complete) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        goto cleanup;
    }
    const RSemanticAggregate snapshot = *aggregate;
    for (uint32_t index = 0U; index < count; ++index) {
        bool conflict = false;
        const RHirNode *value_node = r_body_hir_node(body, values[index].node);
        if ((value_node == NULL) ||
            !r_body_check_field_initializer_conflicts(body, &values[index], &state, &conflict)) {
            goto cleanup;
        }
        valid = valid && !conflict;
        if (!r_body_append_field_initializer(body,
                                             tuple_type,
                                             snapshot.first_field + index,
                                             elements.items[index],
                                             &values[index],
                                             value_node->span,
                                             R_TOKEN_INVALID,
                                             &state)) {
            goto cleanup;
        }
    }
    if (!valid) {
        success = true;
        goto cleanup;
    }
    if (!r_body_append_projected_borrow_state(
            body, &state.projected_borrows, &projected_borrow_state) ||
        !r_body_append_node(body,
                            R_HIR_AGGREGATE_INIT,
                            span,
                            tuple_type,
                            R_SYMBOL_ID_INVALID,
                            R_TOKEN_INVALID,
                            UINT64_C(0),
                            false,
                            &state.field_initializers,
                            &node_id)) {
        goto cleanup;
    }
    result->node = node_id;
    result->type = tuple_type;
    result->borrow_origin = state.origin;
    result->borrow_referent = state.referent;
    result->borrow_origin_set = state.origin_set;
    result->borrow_origin_multiple = state.origin_multiple;
    result->projected_borrow_state = projected_borrow_state;
    result->valid = true;
    r_body_hir_node(body, node_id)->borrow_origin = state.origin;
    r_body_hir_node(body, node_id)->borrow_referent = state.referent;
    r_body_hir_node(body, node_id)->borrow_origin_set = state.origin_set;
    r_body_hir_node(body, node_id)->borrow_origin_multiple = state.origin_multiple;
    r_body_hir_node(body, node_id)->projected_borrow_state = projected_borrow_state;
    success = true;

cleanup:
    r_context_free(context, elements.items);
    r_context_free(context, state.field_initializers.items);
    r_context_free(context, state.projected_borrows.items);
    return success;
}

static bool r_tuple_spread_lowered(RBodyContext *,
                                   const RExpressionResult *,
                                   RSourceSpan,
                                   RHirVector *,
                                   RExpressionResult **,
                                   uint32_t *);
static bool r_pack_partition(
    RBodyContext *, const RExpressionResult *, RSourceSpan, RHirVector *, bool, RSymbolId *);
static bool r_pack_build(RBodyContext *,
                         const RExpressionResult *,
                         uint32_t,
                         RSymbolId,
                         uint32_t,
                         RTypeId,
                         const RExpressionResult *,
                         RSourceSpan,
                         RExpressionResult *);
static bool r_format_stage_operand(RBodyContext *, RExpressionResult *, RHirVector *);
static void r_format_commit_operands(RBodyContext *, size_t);
static bool r_format_wrap_operands(RBodyContext *, RHirVector *, RExpressionResult *);

/* R-TYPE-0053 (L18.4): `(e1, ..., ek, ...rest)`: the elements before the spread are evaluated
   in order into hidden objects, then the operand; a tuple operand is partitioned now and a pack
   when the definition is instantiated. */
static bool r_tuple_lower_spread_expression(RBodyContext *body,
                                            RAstRef spread,
                                            RExpressionResult *values,
                                            uint32_t count,
                                            RSourceSpan span,
                                            RExpressionResult *result) {
    RFrontendContext *context = body->frontend;
    const size_t scope_base = body->active_symbols.count;
    RHirVector prefix = {0};
    RAstRef operand_ref;
    RExpressionResult operand = {0};
    RExpressionResult *pieces = NULL;
    RExpressionResult *elements = NULL;
    RTypeVector prefix_types = {0};
    uint32_t piece_count = 0U;
    bool success = false;

    r_body_expression_invalid(result);
    for (uint32_t index = 0U; index < count; ++index) {
        if (!r_format_stage_operand(body, &values[index], &prefix)) {
            goto cleanup;
        }
    }
    if (!r_body_ast_single_node_child(context, spread, &operand_ref)) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        goto cleanup;
    }
    if (!r_body_lower_value(body, operand_ref, R_TYPE_ID_INVALID, &operand)) {
        goto cleanup;
    }
    if (!operand.valid) {
        success = true;
        goto cleanup;
    }
    if (r_tuple_aggregate(context, operand.type) != NULL) {
        if (!r_tuple_spread_lowered(body, &operand, span, &prefix, &pieces, &piece_count)) {
            goto cleanup;
        }
        elements = r_context_allocate(
            context, ((size_t)count + (size_t)piece_count + 1U) * sizeof(*elements));
        if (elements == NULL) {
            goto cleanup;
        }
        for (uint32_t index = 0U; index < count; ++index) {
            elements[index] = values[index];
        }
        for (uint32_t index = 0U; index < piece_count; ++index) {
            elements[count + index] = pieces[index];
        }
        if (!r_tuple_build(body, elements, count + piece_count, span, result)) {
            goto cleanup;
        }
    } else if (r_generic_pack(context, r_semantic_value_type(context, operand.type)) != NULL) {
        RSymbolId root;
        RTypeId derived;
        for (uint32_t index = 0U; index < count; ++index) {
            if (!r_type_vector_push(
                    context, &prefix_types, r_semantic_value_type(context, values[index].type))) {
                goto cleanup;
            }
        }
        if (!r_pack_partition(body, &operand, span, &prefix, false, &root) ||
            !r_pack_derived_type(context,
                                 &prefix_types,
                                 r_semantic_value_type(context, operand.type),
                                 0U,
                                 &derived) ||
            !r_pack_build(body, values, count, root, 0U, derived, &operand, span, result)) {
            goto cleanup;
        }
    } else {
        success = r_body_diagnostic(
            body, "R-DIAG-TYPE-001", "R-TYPE-0053", "a spread forwards a tuple or a pack", span);
        goto cleanup;
    }
    if (result->valid) {
        r_format_commit_operands(body, scope_base);
        if (!r_format_wrap_operands(body, &prefix, result)) {
            goto cleanup;
        }
    }
    success = true;

cleanup:
    body->active_symbols.count = scope_base;
    r_context_free(context, prefix.items);
    r_context_free(context, pieces);
    r_context_free(context, elements);
    r_context_free(context, prefix_types.items);
    return success;
}

/* R-TYPE-0052: `(e1, ..., en)` evaluates its elements in order and moves or copies each into
   the element of a new tuple. An expected tuple type of the same arity gives each element its
   expected type; the tuple type is the one of its elements. */
static bool r_tuple_lower_expression(RBodyContext *body,
                                     RAstRef tuple,
                                     RTypeId contextual_type,
                                     RExpressionResult *result) {
    RFrontendContext *context = body->frontend;
    const RSemanticAggregate *expected = r_tuple_aggregate(context, contextual_type);
    RAstNodeView view;
    RExpressionResult *values = NULL;
    RAstRef spread = {0};
    uint32_t count = 0U;
    bool valid = true;
    bool success = false;

    r_body_expression_invalid(result);
    if (!r_ast_view_kind(context, tuple, R_SYNTAX_TUPLE_EXPRESSION, &view)) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    values = r_context_allocate(context, ((size_t)view.child_count + 1U) * sizeof(*values));
    if (values == NULL) {
        return false;
    }
    for (uint32_t child = 0U; child < view.child_count; ++child) {
        RAstRef element;
        RAstNodeView element_view;
        RExpressionResult *value = &values[count];
        const RTypeId element_context =
            ((expected != NULL) && (expected->field_count == view.child_count))
                ? r_semantic_value_type(
                      context, context->semantic_fields[expected->first_field + child].type)
                : R_TYPE_ID_INVALID;

        if (!r_ast_ref_child(context, tuple, child, &element) ||
            !r_ast_ref_view(context, element, &element_view)) {
            context->resource_status = R_FRONTEND_INTERNAL_ERROR;
            goto cleanup;
        }
        if (element_view.kind == R_SYNTAX_SPREAD_ARGUMENT) {
            spread = element;
            break;
        }
        if (!r_body_lower_value(body, element, element_context, value)) {
            goto cleanup;
        }
        count += 1U;
        if (!value->valid) {
            valid = false;
            continue;
        }
        if (value->is_integer_literal_form &&
            !r_body_finalize_integer_literal(
                body, value, element_context, r_body_literal_is_negative(body, value))) {
            goto cleanup;
        }
    }
    if (!valid) {
        success = true;
        goto cleanup;
    }
    success = (spread.node != R_AST_NODE_ID_INVALID)
                  ? r_tuple_lower_spread_expression(body, spread, values, count, view.span, result)
                  : r_tuple_build(body, values, count, view.span, result);

cleanup:
    r_context_free(context, values);
    return success;
}

static bool r_reflection_append_call(RBodyContext *,
                                     RSourceSpan,
                                     RStandardCallOperation,
                                     RTypeId,
                                     RTypeId,
                                     uint64_t,
                                     const RHirVector *,
                                     RExpressionResult *);

/* R-TYPE-0053 (L18.4): `len(T...)`, the number of types the pack T stands for: a constant of
   each instantiation, usable in `@if`. */
static bool r_tuple_lower_pack_length(RBodyContext *body,
                                      RAstRef expansion,
                                      RSourceSpan span,
                                      RExpressionResult *result) {
    RFrontendContext *context = body->frontend;
    RAstTokenView name;
    RTypeId pack;
    RTypeId usize_type;

    r_body_expression_invalid(result);
    if (!r_ast_direct_token(context, expansion, 0U, NULL, &name) ||
        (name.kind != R_TOKEN_IDENTIFIER)) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    if (!r_generic_resolve_parameter(context, &name, &pack) ||
        (r_generic_pack(context, pack) == NULL)) {
        return r_body_diagnostic(
            body, "R-DIAG-TYPE-001", "R-TYPE-0053", "`len(T...)` names a pack", name.span);
    }
    if (!r_semantic_type_from_token(context, R_TOKEN_KW_USIZE, &usize_type)) {
        return false;
    }
    return r_reflection_append_call(body,
                                    span,
                                    R_STANDARD_CALL_CORE_REFLECT_FIELD_COUNT,
                                    usize_type,
                                    pack,
                                    UINT64_C(0),
                                    NULL,
                                    result);
}

static bool
r_match_field_place(RBodyContext *, RExpressionResult, uint32_t, RSourceSpan, RExpressionResult *);
static bool r_match_transfer(RBodyContext *, RExpressionResult, RSourceSpan, RExpressionResult *);
static bool r_match_forget_partition(RBodyContext *, RSymbolId, RSourceSpan, RHirVector *);

/* R-TYPE-0053 (L18.3): the values a spread of the lowered tuple `operand` stands for, one per
   element in order. The tuple is evaluated once into a hidden object whose elements move or copy
   into hidden pieces before the call, the same partition a pattern performs (R-EXPR-0031), and
   each piece is then passed. `values` receives `count` results the caller frees. */
static bool r_tuple_spread_lowered(RBodyContext *body,
                                   const RExpressionResult *operand,
                                   RSourceSpan span,
                                   RHirVector *statements,
                                   RExpressionResult **values,
                                   uint32_t *count) {
    RFrontendContext *context = body->frontend;
    RExpressionResult root_place = {0};
    RSymbolId root;
    const RSemanticAggregate *tuple = r_tuple_aggregate(context, operand->type);
    RSymbolId *pieces = NULL;
    bool success = false;

    *values = NULL;
    *count = 0U;
    if (tuple == NULL) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    const uint32_t arity = tuple->field_count;
    pieces = r_context_allocate(context, ((size_t)arity + 1U) * sizeof(*pieces));
    *values = r_context_allocate(context, ((size_t)arity + 1U) * sizeof(**values));
    if ((pieces == NULL) || (*values == NULL)) {
        goto cleanup;
    }
    (void)memset(*values, 0, ((size_t)arity + 1U) * sizeof(**values));
    if (!r_iteration_declare_hidden(body,
                                    body->active_symbols.count,
                                    "$spread",
                                    r_semantic_value_type(context, operand->type),
                                    R_AST_NODE_ID_INVALID,
                                    span,
                                    operand,
                                    statements,
                                    &root) ||
        !r_body_symbol_place(body, root, span, &root_place)) {
        goto cleanup;
    }
    for (uint32_t index = 0U; index < arity; ++index) {
        RExpressionResult field = {0};
        RExpressionResult piece = {0};
        if (!r_match_field_place(body, root_place, index, span, &field) ||
            !r_match_transfer(body, field, span, &piece) ||
            !r_iteration_declare_hidden(body,
                                        body->active_symbols.count,
                                        "$spread_piece",
                                        piece.type,
                                        R_AST_NODE_ID_INVALID,
                                        span,
                                        &piece,
                                        statements,
                                        &pieces[index])) {
            goto cleanup;
        }
    }
    if (!r_match_forget_partition(body, root, span, statements)) {
        goto cleanup;
    }
    for (uint32_t index = 0U; index < arity; ++index) {
        RExpressionResult place = {0};
        if (!r_body_symbol_place(body, pieces[index], span, &place) ||
            !r_match_transfer(body, place, span, &(*values)[index])) {
            goto cleanup;
        }
        /* A piece is a named object, which an async start stages as a direct Move operand
           (R-FUNC-0010). */
        RHirNode *transfer = r_body_hir_node(body, (*values)[index].node);
        if (transfer->kind == R_HIR_MOVE) {
            transfer->operation = R_TOKEN_KW_MOVE;
        }
    }
    *count = arity;
    success = true;

cleanup:
    r_context_free(context, pieces);
    if (!success) {
        r_context_free(context, *values);
        *values = NULL;
    }
    return success;
}

/* R-STMT-0022 (L37.3): `auto (a, b, ...) = value;` evaluates the tuple once into a hidden object
   and moves or copies each element, in order, into a new local of the enclosing block, the
   partition a pattern performs (R-EXPR-0031); the emptied tuple is then forgotten. The locals
   are appended to the statements of the block, so they live until its end. */
static bool r_tuple_lower_destructuring(RBodyContext *body,
                                        RAstRef statement,
                                        size_t scope_base,
                                        RHirNodeId *node_id) {
    RFrontendContext *context = body->frontend;
    RAstNodeView view;
    RAstTokenView names[64];
    uint32_t name_count = 0U;
    RAstRef initializer = {0};
    RExpressionResult value = {0};
    RExpressionResult root_place = {0};
    RSymbolId root;

    *node_id = R_HIR_NODE_ID_INVALID;
    if (!r_ast_ref_view(context, statement, &view)) {
        return r_body_invariant(body);
    }
    for (uint32_t index = 0U; index < view.child_count; ++index) {
        RAstRef child;
        RAstTokenView token;
        if (!r_ast_ref_child(context, statement, index, &child)) {
            return r_body_invariant(body);
        }
        if (r_ast_ref_token(context, child, &token)) {
            if (token.kind == R_TOKEN_IDENTIFIER) {
                if (name_count == 64U) {
                    return r_body_diagnostic(body,
                                             "R-DIAG-LIMIT-001",
                                             "R-STMT-0022",
                                             "a destructuring declaration binds at most 64 names",
                                             token.span);
                }
                names[name_count++] = token;
            }
        } else if (initializer.node == R_AST_NODE_ID_INVALID) {
            initializer = child;
        }
    }
    if ((initializer.node == R_AST_NODE_ID_INVALID) || (name_count == 0U)) {
        return r_body_invariant(body);
    }
    if ((body->statement_splice == NULL) || (body->splice_statement != statement.node)) {
        return r_body_diagnostic(body,
                                 "R-DIAG-TYPE-001",
                                 "R-STMT-0022",
                                 "a destructuring declaration is a statement of a block",
                                 view.span);
    }
    r_body_expression_invalid(&value);
    if (!r_body_lower_value(body, initializer, R_TYPE_ID_INVALID, &value)) {
        return false;
    }
    if (!value.valid) {
        return true;
    }
    const RSemanticAggregate *tuple = r_tuple_aggregate(context, value.type);
    if (tuple == NULL) {
        return r_body_diagnostic(body,
                                 "R-DIAG-TYPE-001",
                                 "R-STMT-0022",
                                 "a destructuring declaration binds the elements of a tuple",
                                 view.span);
    }
    if (tuple->field_count != name_count) {
        return r_body_diagnostic(body,
                                 "R-DIAG-TYPE-001",
                                 "R-STMT-0022",
                                 "a destructuring declaration names every element of the tuple",
                                 view.span);
    }
    RHirVector *statements = body->statement_splice;
    if (!r_iteration_declare_hidden(body,
                                    body->active_symbols.count,
                                    "$tuple",
                                    r_semantic_value_type(context, value.type),
                                    statement.node,
                                    view.span,
                                    &value,
                                    statements,
                                    &root) ||
        !r_body_symbol_place(body, root, view.span, &root_place)) {
        return false;
    }
    for (uint32_t index = 0U; index < name_count; ++index) {
        RExpressionResult field = {0};
        RExpressionResult piece = {0};
        RHirNodeId local = R_HIR_NODE_ID_INVALID;
        if (!r_match_field_place(body, root_place, index, names[index].span, &field) ||
            !r_match_transfer(body, field, names[index].span, &piece) ||
            !r_body_declare_local(body,
                                  scope_base,
                                  names[index],
                                  piece.type,
                                  statement.node,
                                  false,
                                  false,
                                  names[index].span,
                                  &piece,
                                  false,
                                  false,
                                  &local) ||
            ((local != R_HIR_NODE_ID_INVALID) &&
             !r_hir_vector_push(context, statements, local))) {
            return false;
        }
    }
    return r_match_forget_partition(body, root, view.span, statements);
}

/* R-TYPE-0053 (L18.3): the argument values a spread `...operand` of a tuple stands for, lowered
   from the spread; NULL `values` after a diagnostic. */
static bool r_tuple_spread_values(RBodyContext *body,
                                  RAstRef spread,
                                  RHirVector *statements,
                                  RExpressionResult **values,
                                  uint32_t *count) {
    RFrontendContext *context = body->frontend;
    RAstNodeView view;
    RAstRef operand_ref;
    RExpressionResult operand = {0};

    *values = NULL;
    *count = 0U;
    if (!r_ast_ref_view(context, spread, &view) ||
        !r_body_ast_single_node_child(context, spread, &operand_ref)) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    if (!r_body_lower_value(body, operand_ref, R_TYPE_ID_INVALID, &operand)) {
        return false;
    }
    if (!operand.valid) {
        return true;
    }
    if (r_tuple_aggregate(context, operand.type) == NULL) {
        return r_body_diagnostic(
            body,
            "R-DIAG-TYPE-001",
            "R-TYPE-0053",
            r_generic_pack(context, r_semantic_value_type(context, operand.type)) != NULL
                ? "a spread of a pack forwards it to a generic function"
                : "a spread into ordinary parameters forwards a tuple",
            view.span);
    }
    return r_tuple_spread_lowered(body, &operand, view.span, statements, values, count);
}

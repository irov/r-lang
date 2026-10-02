/* R-TYPE-0053 (L18.4): packs whose length an instantiation decides. A pack parameter keeps the
 * constraints of its elements on a synthetic carrier parameter; its elements and the packs formed
 * from its elements are synthetic parameters over it, which substitution reduces once the pack
 * becomes a tuple. A spread of such a pack partitions a hidden object into pieces that only an
 * instantiation creates. */

/* What a tuple of elements with some constraints proves as a whole. */
#define R_PACK_WHOLE_CONSTRAINTS                                                                   \
    (R_GENERIC_COPY | R_GENERIC_POD | R_GENERIC_SEND | R_GENERIC_SYNC | R_GENERIC_UNBORROWED)

/* The parameter record of the parameter type `type`, or NULL. */
static const RGenericParameter *r_pack_record(const RFrontendContext *context, RTypeId type_id) {
    const RSemanticType *type = r_semantic_type(context, type_id);
    if ((type == NULL) || (type->kind != R_SEMANTIC_TYPE_PARAMETER) || (type->base == 0U) ||
        ((size_t)type->base > context->generic_parameter_count)) {
        return NULL;
    }
    return &context->generic_parameters[type->base - 1U];
}

/* The elements of the tuple `type` stands for, or false when it is no tuple. */
static bool r_pack_tuple_elements(RFrontendContext *context, RTypeId type, RTypeVector *elements) {
    const RSemanticAggregate *tuple = r_tuple_aggregate(context, type);
    if (tuple == NULL) {
        return false;
    }
    const uint32_t count = context->generic_schemas[tuple->generic_schema - 1U].parameter_count;
    for (uint32_t index = 0U; index < count; ++index) {
        if (!r_type_vector_push(
                context,
                elements,
                context->generic_arguments[tuple->first_generic_argument + index])) {
            return false;
        }
    }
    return true;
}

/* The synthetic parameters of `record` share its constraints; the lengths are those given. */
static bool
r_pack_append_parameter(RFrontendContext *context, RGenericParameter parameter, RTypeId *type_id) {
    if ((context->generic_parameter_count >= UINT32_MAX) ||
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
    *type_id = parameter.type;
    return true;
}

/* R-TYPE-0053 (L18.4): once every constraint is known, the constraints of a pack move to the
   carrier of its elements; the pack keeps what a tuple of those elements proves. */
static bool r_pack_finalize_parameters(RFrontendContext *context) {
    const size_t count = context->generic_parameter_count;
    for (size_t index = 0U; index < count; ++index) {
        RGenericParameter carrier = context->generic_parameters[index];
        if (!carrier.is_pack || (carrier.pack_element != R_TYPE_ID_INVALID) ||
            (carrier.pack_base != R_TYPE_ID_INVALID)) {
            continue;
        }
        const uint32_t whole =
            r_generic_parameter_constraints(context, carrier.type) & R_PACK_WHOLE_CONSTRAINTS;
        RTypeId element;
        carrier.is_pack = false;
        if (!r_pack_append_parameter(context, carrier, &element)) {
            return false;
        }
        RGenericParameter *pack = &context->generic_parameters[index];
        pack->pack_element = element;
        pack->constraints = whole;
        pack->first_trait_constraint = 0U;
        pack->trait_constraint_count = 0U;
    }
    return true;
}

/* The length bounds a pack type proves: a pack parameter from its branch facts, a derived pack
   from its prefix and base, a tuple exactly. */
static void r_pack_length_bounds(const RFrontendContext *context,
                                 RTypeId type,
                                 uint32_t *minimum,
                                 uint32_t *maximum,
                                 bool *maximum_known) {
    const RSemanticAggregate *tuple = r_tuple_aggregate(context, type);
    const RGenericParameter *record = r_pack_record(context, type);
    *minimum = 0U;
    *maximum = 0U;
    *maximum_known = false;
    if (tuple != NULL) {
        *minimum = tuple->field_count;
        *maximum = tuple->field_count;
        *maximum_known = true;
    } else if (record != NULL) {
        *minimum = record->pack_min_length;
        *maximum = record->pack_max_length;
        *maximum_known = record->pack_max_known;
    }
}

static bool
r_pack_derived_type(RFrontendContext *, const RTypeVector *, RTypeId, uint32_t, RTypeId *);

/* R-TYPE-0053 (L18.4): element `index` of the pack or tuple `base`. */
static bool
r_pack_element_type(RFrontendContext *context, RTypeId base, uint32_t index, RTypeId *result) {
    RTypeVector elements = {0};
    const RGenericParameter *record;
    bool success = false;

    *result = R_TYPE_ID_INVALID;
    if (r_tuple_aggregate(context, base) != NULL) {
        if (!r_pack_tuple_elements(context, base, &elements)) {
            goto cleanup;
        }
        if (index >= elements.count) {
            context->resource_status = R_FRONTEND_INTERNAL_ERROR;
            goto cleanup;
        }
        *result = elements.items[index];
        success = true;
        goto cleanup;
    }
    record = r_pack_record(context, base);
    if ((record == NULL) || !record->is_pack) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    if (record->pack_derived) {
        /* An element of a derived pack is an element of its prefix or of its base. */
        const RGenericParameter derived = *record;
        if (index < derived.pack_prefix_count) {
            if (!r_semantic_chain_arguments(context, derived.pack_prefix, &elements)) {
                goto cleanup;
            }
            *result = elements.items[index];
            success = true;
            goto cleanup;
        }
        return r_pack_element_type(context,
                                   derived.pack_base,
                                   index - derived.pack_prefix_count + derived.pack_index,
                                   result);
    }
    for (size_t existing = 0U; existing < context->generic_parameter_count; ++existing) {
        const RGenericParameter *candidate = &context->generic_parameters[existing];
        if ((candidate->pack_base == base) && !candidate->pack_derived && !candidate->is_pack &&
            (candidate->pack_index == index)) {
            *result = candidate->type;
            return true;
        }
    }
    {
        const RGenericParameter *carrier = r_pack_record(context, record->pack_element);
        RGenericParameter parameter;
        if (carrier == NULL) {
            context->resource_status = R_FRONTEND_INTERNAL_ERROR;
            return false;
        }
        parameter = *carrier;
        parameter.refinement_origin = R_TYPE_ID_INVALID;
        parameter.first_static_fact = 0U;
        parameter.static_fact_count = 0U;
        parameter.excluded_constraints = 0U;
        parameter.pack_element = R_TYPE_ID_INVALID;
        parameter.pack_base = base;
        parameter.pack_prefix = R_TYPE_ID_INVALID;
        parameter.pack_prefix_count = 0U;
        parameter.pack_index = index;
        parameter.pack_derived = false;
        success = r_pack_append_parameter(context, parameter, result);
    }

cleanup:
    r_context_free(context, elements.items);
    return success;
}

/* R-TYPE-0053 (L18.4): the pack of `prefix` followed by the elements of the pack or tuple `base`
   from `offset` on. A tuple base gives a tuple; a derived base folds into one derived pack over
   a pack parameter. */
static bool r_pack_derived_type(RFrontendContext *context,
                                const RTypeVector *prefix,
                                RTypeId base,
                                uint32_t offset,
                                RTypeId *result) {
    RTypeVector elements = {0};
    RTypeVector inner = {0};
    RTypeId chain = R_TYPE_ID_INVALID;
    const RGenericParameter *record;
    bool success = false;

    *result = R_TYPE_ID_INVALID;
    if (r_tuple_aggregate(context, base) != NULL) {
        RTypeVector tail = {0};
        if (!r_pack_tuple_elements(context, base, &tail)) {
            r_context_free(context, tail.items);
            return false;
        }
        if (offset > tail.count) {
            r_context_free(context, tail.items);
            context->resource_status = R_FRONTEND_INTERNAL_ERROR;
            return false;
        }
        for (size_t index = 0U; index < prefix->count; ++index) {
            if (!r_type_vector_push(context, &elements, prefix->items[index])) {
                r_context_free(context, tail.items);
                goto cleanup;
            }
        }
        for (size_t index = offset; index < tail.count; ++index) {
            if (!r_type_vector_push(context, &elements, tail.items[index])) {
                r_context_free(context, tail.items);
                goto cleanup;
            }
        }
        r_context_free(context, tail.items);
        success = r_tuple_type(context, &elements, (RSourceSpan){0}, result);
        goto cleanup;
    }
    record = r_pack_record(context, base);
    if ((record == NULL) || !record->is_pack) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    if (record->pack_derived) {
        const RGenericParameter derived = *record;
        if (!r_semantic_chain_arguments(context, derived.pack_prefix, &inner)) {
            goto cleanup;
        }
        for (size_t index = 0U; index < prefix->count; ++index) {
            if (!r_type_vector_push(context, &elements, prefix->items[index])) {
                goto cleanup;
            }
        }
        if (offset <= derived.pack_prefix_count) {
            for (size_t index = offset; index < inner.count; ++index) {
                if (!r_type_vector_push(context, &elements, inner.items[index])) {
                    goto cleanup;
                }
            }
            success = r_pack_derived_type(
                context, &elements, derived.pack_base, derived.pack_index, result);
        } else {
            success = r_pack_derived_type(context,
                                          &elements,
                                          derived.pack_base,
                                          derived.pack_index + offset - derived.pack_prefix_count,
                                          result);
        }
        goto cleanup;
    }
    if ((prefix->count == 0U) && (offset == 0U)) {
        *result = base;
        return true;
    }
    if (!r_semantic_associated_chain(context, prefix, &chain)) {
        return false;
    }
    for (size_t existing = 0U; existing < context->generic_parameter_count; ++existing) {
        const RGenericParameter *candidate = &context->generic_parameters[existing];
        if ((candidate->pack_base == base) && candidate->pack_derived &&
            (candidate->pack_index == offset) && (candidate->pack_prefix == chain)) {
            *result = candidate->type;
            return true;
        }
    }
    {
        RGenericParameter parameter;
        uint32_t whole = record->constraints & R_PACK_WHOLE_CONSTRAINTS;
        for (size_t index = 0U; index < prefix->count; ++index) {
            for (uint32_t property = R_GENERIC_COPY; property <= R_GENERIC_UNBORROWED;
                 property <<= 1U) {
                if (((whole & property) != 0U) &&
                    !r_generic_has_property(context, prefix->items[index], property, 0U)) {
                    whole &= ~property;
                }
            }
        }
        (void)memset(&parameter, 0, sizeof(parameter));
        parameter.name_span = record->name_span;
        parameter.name_intern_id = record->name_intern_id;
        parameter.constraints = whole;
        parameter.is_pack = true;
        parameter.pack_element = record->pack_element;
        parameter.pack_base = base;
        parameter.pack_prefix = chain;
        parameter.pack_prefix_count = (uint32_t)prefix->count;
        parameter.pack_index = offset;
        parameter.pack_derived = true;
        parameter.pack_min_length =
            (uint32_t)prefix->count +
            (record->pack_min_length > offset ? record->pack_min_length - offset : 0U);
        parameter.pack_max_known = record->pack_max_known;
        parameter.pack_max_length =
            record->pack_max_known
                ? (uint32_t)prefix->count +
                      (record->pack_max_length > offset ? record->pack_max_length - offset : 0U)
                : 0U;
        success = r_pack_append_parameter(context, parameter, result);
    }

cleanup:
    r_context_free(context, elements.items);
    r_context_free(context, inner.items);
    return success;
}

/* R-TYPE-0053 (L18.4): an element or a derived pack follows its base and its prefix through
   substitution. */
static bool r_pack_substitute(RFrontendContext *context,
                              const RGenericParameter *parameter,
                              uint32_t schema_id,
                              uint32_t first_argument,
                              RTypeId *result) {
    const RGenericParameter record = *parameter;
    RTypeVector prefix = {0};
    RTypeId base;
    RTypeId chain = R_TYPE_ID_INVALID;
    bool success;

    *result = record.type;
    if (!r_generic_substitute_type(context, record.pack_base, schema_id, first_argument, &base) ||
        (record.pack_derived &&
         !r_generic_substitute_type(
             context, record.pack_prefix, schema_id, first_argument, &chain))) {
        return false;
    }
    if ((base == record.pack_base) && (!record.pack_derived || (chain == record.pack_prefix))) {
        return true;
    }
    if (!record.pack_derived) {
        return r_pack_element_type(context, base, record.pack_index, result);
    }
    success = r_semantic_chain_arguments(context, chain, &prefix) &&
              r_pack_derived_type(context, &prefix, base, record.pack_index, result);
    r_context_free(context, prefix.items);
    return success;
}

/* R-META-0003 (L18.4): the lengths a branch proves for one pack. */
typedef struct RPackBounds {
    uint32_t minimum;
    uint32_t maximum;
    bool maximum_known;
    bool contradiction;
} RPackBounds;

static void r_pack_bounds_at_least(RPackBounds *bounds, uint64_t minimum) {
    if (minimum > UINT32_MAX) {
        bounds->contradiction = true;
    } else if (minimum > bounds->minimum) {
        bounds->minimum = (uint32_t)minimum;
    }
}

static void r_pack_bounds_at_most(RPackBounds *bounds, uint64_t maximum) {
    if (maximum > UINT32_MAX) {
        return;
    }
    if (!bounds->maximum_known || (maximum < bounds->maximum)) {
        bounds->maximum = (uint32_t)maximum;
        bounds->maximum_known = true;
    }
}

/* `len OP value` holds (or fails, when `holds` is false). */
static void
r_pack_bounds_compare(RPackBounds *bounds, RTokenKind operation, uint64_t value, bool holds) {
    if (!holds) {
        switch (operation) {
        case R_TOKEN_EQUAL_EQUAL:
            operation = R_TOKEN_BANG_EQUAL;
            break;
        case R_TOKEN_BANG_EQUAL:
            operation = R_TOKEN_EQUAL_EQUAL;
            break;
        case R_TOKEN_LESS:
            operation = R_TOKEN_GREATER_EQUAL;
            break;
        case R_TOKEN_LESS_EQUAL:
            operation = R_TOKEN_GREATER;
            break;
        case R_TOKEN_GREATER:
            operation = R_TOKEN_LESS_EQUAL;
            break;
        case R_TOKEN_GREATER_EQUAL:
            operation = R_TOKEN_LESS;
            break;
        default:
            return;
        }
    }
    switch (operation) {
    case R_TOKEN_EQUAL_EQUAL:
        r_pack_bounds_at_least(bounds, value);
        r_pack_bounds_at_most(bounds, value);
        break;
    case R_TOKEN_BANG_EQUAL:
        /* Only an excluded end of the known range narrows it. */
        if (value == bounds->minimum) {
            r_pack_bounds_at_least(bounds, value + 1U);
        }
        if (bounds->maximum_known && (value == bounds->maximum)) {
            if (value == 0U) {
                bounds->contradiction = true;
            } else {
                r_pack_bounds_at_most(bounds, value - 1U);
            }
        }
        break;
    case R_TOKEN_LESS:
        if (value == 0U) {
            bounds->contradiction = true;
        } else {
            r_pack_bounds_at_most(bounds, value - 1U);
        }
        break;
    case R_TOKEN_LESS_EQUAL:
        r_pack_bounds_at_most(bounds, value);
        break;
    case R_TOKEN_GREATER:
        r_pack_bounds_at_least(bounds, value + 1U);
        break;
    case R_TOKEN_GREATER_EQUAL:
        r_pack_bounds_at_least(bounds, value);
        break;
    default:
        break;
    }
    if (bounds->maximum_known && (bounds->maximum < bounds->minimum)) {
        bounds->contradiction = true;
    }
}

/* Whether the HIR node is `len(P...)` of a pack whose origin is `origin`. */
static bool
r_pack_is_length_of(const RFrontendContext *context, const RHirNode *node, RTypeId origin) {
    return (node != NULL) && (node->kind == R_HIR_STANDARD_CALL) &&
           (node->standard_operation == R_STANDARD_CALL_CORE_REFLECT_FIELD_COUNT) &&
           (r_generic_pack(context, node->auxiliary_type) != NULL) &&
           (r_static_origin(context, node->auxiliary_type) == origin);
}

/* The facts that the lowered condition `id` having the truth `holds` proves about `origin`. */
static void r_pack_condition_bounds(const RFrontendContext *context,
                                    RHirNodeId id,
                                    bool holds,
                                    RTypeId origin,
                                    RPackBounds *bounds,
                                    uint32_t depth) {
    const RHirNode *node;
    if ((id == R_HIR_NODE_ID_INVALID) || ((size_t)id > context->hir_node_count) ||
        (depth > context->options.limits.max_nesting)) {
        return;
    }
    node = &context->hir_nodes[id - 1U];
    if ((node->kind == R_HIR_UNARY) && (node->operation == R_TOKEN_BANG) &&
        (node->child_count == 1U)) {
        r_pack_condition_bounds(
            context, context->hir_children[node->first_child], !holds, origin, bounds, depth + 1U);
        return;
    }
    if ((node->kind != R_HIR_BINARY) || (node->child_count != 2U)) {
        return;
    }
    const RHirNode *left = &context->hir_nodes[context->hir_children[node->first_child] - 1U];
    const RHirNode *right = &context->hir_nodes[context->hir_children[node->first_child + 1U] - 1U];
    if ((node->operation == R_TOKEN_AMP_AMP) || (node->operation == R_TOKEN_PIPE_PIPE)) {
        /* A conjunction that holds, or a disjunction that fails, proves both sides. */
        if ((node->operation == R_TOKEN_AMP_AMP) == holds) {
            r_pack_condition_bounds(context,
                                    context->hir_children[node->first_child],
                                    holds,
                                    origin,
                                    bounds,
                                    depth + 1U);
            r_pack_condition_bounds(context,
                                    context->hir_children[node->first_child + 1U],
                                    holds,
                                    origin,
                                    bounds,
                                    depth + 1U);
        }
        return;
    }
    if (r_pack_is_length_of(context, left, origin) && (right->kind == R_HIR_LITERAL)) {
        r_pack_bounds_compare(bounds, node->operation, right->integer_value, holds);
    } else if (r_pack_is_length_of(context, right, origin) && (left->kind == R_HIR_LITERAL)) {
        /* `value OP len` is `len OP' value` with the mirrored operator. */
        RTokenKind mirrored = node->operation;
        if (mirrored == R_TOKEN_LESS) {
            mirrored = R_TOKEN_GREATER;
        } else if (mirrored == R_TOKEN_GREATER) {
            mirrored = R_TOKEN_LESS;
        } else if (mirrored == R_TOKEN_LESS_EQUAL) {
            mirrored = R_TOKEN_GREATER_EQUAL;
        } else if (mirrored == R_TOKEN_GREATER_EQUAL) {
            mirrored = R_TOKEN_LESS_EQUAL;
        }
        r_pack_bounds_compare(bounds, mirrored, left->integer_value, holds);
    }
}

/* The facts the static predicate `id` having the truth `holds` proves about `origin`. */
static void r_pack_predicate_bounds(const RFrontendContext *context,
                                    uint32_t id,
                                    bool holds,
                                    RTypeId origin,
                                    RPackBounds *bounds,
                                    uint32_t depth) {
    RStaticPredicate predicate;
    if ((id == 0U) || (id > context->static_predicate_count) ||
        (depth > context->options.limits.max_nesting)) {
        return;
    }
    predicate = context->static_predicates[id - 1U];
    if (predicate.kind == R_STATIC_NOT) {
        r_pack_predicate_bounds(context, predicate.left, !holds, origin, bounds, depth + 1U);
    } else if ((predicate.kind == R_STATIC_AND) || (predicate.kind == R_STATIC_OR)) {
        if ((predicate.kind == R_STATIC_AND) == holds) {
            r_pack_predicate_bounds(context, predicate.left, holds, origin, bounds, depth + 1U);
            r_pack_predicate_bounds(context, predicate.right, holds, origin, bounds, depth + 1U);
        }
    } else if (predicate.kind == R_STATIC_VALUE) {
        r_pack_condition_bounds(context, predicate.condition, holds, origin, bounds, 0U);
    }
}

/* R-META-0003 (L18.4): the pack `current` refined in a branch of the static predicate
   `predicate` taken when it has the truth `holds`; `current` itself when the branch proves no
   narrower lengths. */
static bool r_pack_refine_branch(
    RFrontendContext *context, uint32_t predicate, bool holds, RTypeId current, RTypeId *result) {
    const RGenericParameter *record = r_pack_record(context, current);
    RPackBounds bounds = {0};
    RGenericParameter parameter;
    *result = current;
    if ((record == NULL) || !record->is_pack || record->pack_derived) {
        return true;
    }
    bounds.minimum = record->pack_min_length;
    bounds.maximum = record->pack_max_length;
    bounds.maximum_known = record->pack_max_known;
    r_pack_predicate_bounds(
        context, predicate, holds, r_static_origin(context, current), &bounds, 0U);
    if (bounds.contradiction ||
        ((bounds.minimum == record->pack_min_length) &&
         (bounds.maximum_known == record->pack_max_known) &&
         (!bounds.maximum_known || (bounds.maximum == record->pack_max_length)))) {
        /* A contradictory branch is never instantiated; it keeps the enclosing facts. */
        return true;
    }
    parameter = *record;
    parameter.refinement_origin = r_static_origin(context, current);
    parameter.pack_min_length = bounds.minimum;
    parameter.pack_max_length = bounds.maximum;
    parameter.pack_max_known = bounds.maximum_known;
    return r_pack_append_parameter(context, parameter, result);
}

/* R-TYPE-0053 (L18.4): a spread of a pack whose length depends on an instantiation evaluates its
   operand once into the hidden object `root`, which a partition statement divides into pieces
   when the definition is instantiated; the object itself is consumed here. */
static bool r_pack_partition(RBodyContext *body,
                             const RExpressionResult *operand,
                             RSourceSpan span,
                             RHirVector *statements,
                             bool staged,
                             RSymbolId *root) {
    RFrontendContext *context = body->frontend;
    RHirNodeId partition;
    RTypeId void_type;
    if (!r_iteration_declare_hidden(body,
                                    body->active_symbols.count,
                                    "$spread",
                                    r_semantic_value_type(context, operand->type),
                                    R_AST_NODE_ID_INVALID,
                                    span,
                                    operand,
                                    statements,
                                    root) ||
        !r_semantic_type_from_token(context, R_TOKEN_KW_VOID, &void_type) ||
        !r_body_append_node(body,
                            R_HIR_PACK_PARTITION,
                            span,
                            void_type,
                            *root,
                            R_TOKEN_INVALID,
                            UINT64_C(0),
                            false,
                            NULL,
                            &partition) ||
        !r_hir_vector_push(context, statements, partition)) {
        return false;
    }
    if (!r_semantic_type_is_copy(context, operand->type)) {
        /* An async start stages the object its pieces stand for (R-FUNC-0010). */
        r_body_symbol_mut(body, *root)->object_state =
            staged ? R_SEMANTIC_OBJECT_STATE_STAGED_ASYNC_MOVE : R_SEMANTIC_OBJECT_STATE_MOVED;
    }
    return true;
}

/* A value that stands for piece `index` (an element node) or for the pieces from `index` on (an
   arguments node) of the partitioned object `root`; it carries the views of the operand. */
static bool r_pack_piece_value(RBodyContext *body,
                               RHirKind kind,
                               RSymbolId root,
                               uint32_t index,
                               RTypeId type,
                               const RExpressionResult *operand,
                               RSourceSpan span,
                               RExpressionResult *result) {
    RHirNode *node;
    *result = *operand;
    result->is_place = false;
    result->is_integer_literal_form = false;
    result->is_unsuffixed_integer_literal = false;
    result->projected_borrow_state = 0U;
    result->type = type;
    result->symbol = root;
    if (!r_body_append_node(body,
                            kind,
                            span,
                            type,
                            root,
                            R_TOKEN_INVALID,
                            (uint64_t)index,
                            false,
                            NULL,
                            &result->node)) {
        return false;
    }
    node = r_body_hir_node(body, result->node);
    node->borrow_origin = result->borrow_origin;
    node->borrow_referent = result->borrow_referent;
    node->borrow_origin_set = result->borrow_origin_set;
    node->borrow_origin_multiple = result->borrow_origin_multiple;
    return true;
}

/* A field initializer whose member an instantiation assigns by position (L18.4). */
static bool r_pack_field_init(RBodyContext *body,
                              const RExpressionResult *value,
                              RSourceSpan span,
                              RHirNodeId *result) {
    RHirNodeId child = value->node;
    RHirVector children = {&child, 1U, 1U};
    RHirNode *node;
    if (!r_body_append_node(body,
                            R_HIR_FIELD_INIT,
                            span,
                            r_semantic_value_type(body->frontend, value->type),
                            R_SYMBOL_ID_INVALID,
                            R_TOKEN_EQUAL,
                            UINT64_C(0),
                            false,
                            &children,
                            result)) {
        return false;
    }
    node = r_body_hir_node(body, *result);
    node->borrow_origin = value->borrow_origin;
    node->borrow_referent = value->borrow_referent;
    node->borrow_origin_set = value->borrow_origin_set;
    node->borrow_origin_multiple = value->borrow_origin_multiple;
    return true;
}

/* R-TYPE-0053 (L18.4): the pack `type` built from the `count` lowered values followed by the
   pieces from `offset` on of the partitioned object `root`. */
static bool r_pack_build(RBodyContext *body,
                         const RExpressionResult *values,
                         uint32_t count,
                         RSymbolId root,
                         uint32_t offset,
                         RTypeId type,
                         const RExpressionResult *operand,
                         RSourceSpan span,
                         RExpressionResult *result) {
    RFrontendContext *context = body->frontend;
    RHirVector children = {0};
    RExpressionResult rest = {0};
    RHirNodeId field;
    RSymbolId origin = operand->borrow_origin;
    uint32_t origin_set = operand->borrow_origin_set;
    bool origin_multiple = operand->borrow_origin_multiple;
    uint64_t referent = operand->borrow_referent;
    bool success = false;

    for (uint32_t index = 0U; index < count; ++index) {
        if (!r_pack_field_init(body, &values[index], span, &field) ||
            !r_hir_vector_push(context, &children, field) ||
            !r_semantic_merge_borrow_origins(context,
                                             origin,
                                             origin_set,
                                             origin_multiple,
                                             values[index].borrow_origin,
                                             values[index].borrow_origin_set,
                                             values[index].borrow_origin_multiple,
                                             &origin,
                                             &origin_set,
                                             &origin_multiple)) {
            goto cleanup;
        }
        referent = r_semantic_merge_borrow_referents(referent, values[index].borrow_referent);
    }
    if (!r_pack_piece_value(body, R_HIR_PACK_ARGS, root, offset, type, operand, span, &rest) ||
        !r_pack_field_init(body, &rest, span, &field) ||
        !r_hir_vector_push(context, &children, field)) {
        goto cleanup;
    }
    *result = *operand;
    result->is_place = false;
    result->projected_borrow_state = 0U;
    result->type = type;
    result->symbol = R_SYMBOL_ID_INVALID;
    result->borrow_origin = origin;
    result->borrow_origin_set = origin_set;
    result->borrow_origin_multiple = origin_multiple;
    result->borrow_referent = referent;
    if (!r_body_append_node(body,
                            R_HIR_AGGREGATE_INIT,
                            span,
                            type,
                            R_SYMBOL_ID_INVALID,
                            R_TOKEN_INVALID,
                            UINT64_C(0),
                            false,
                            &children,
                            &result->node)) {
        goto cleanup;
    }
    r_body_hir_node(body, result->node)->borrow_origin = origin;
    r_body_hir_node(body, result->node)->borrow_origin_set = origin_set;
    r_body_hir_node(body, result->node)->borrow_origin_multiple = origin_multiple;
    r_body_hir_node(body, result->node)->borrow_referent = referent;
    success = true;

cleanup:
    r_context_free(context, children.items);
    return success;
}

/* R-TYPE-0053 (L18.4): the arguments a trailing spread of the lowered `operand` stands for in a
   call of a generic function with `fixed_count` fixed parameters, of which the arguments before
   the spread supply `supplied`. With a pack parameter, the arguments after the fixed ones and
   what remains of the spread form the pack at `values[fixed_count]`. A tuple is partitioned now;
   a pack whose length an instantiation decides is partitioned then, after its known length
   proves the fixed parameters it supplies. `valid` turns false after a diagnostic. */
static bool r_pack_spread_call_values(RBodyContext *body,
                                      const RExpressionResult *operand,
                                      RSourceSpan span,
                                      RHirVector *statements,
                                      uint32_t fixed_count,
                                      uint32_t supplied,
                                      bool has_pack,
                                      bool splice,
                                      bool staged,
                                      RExpressionResult *values,
                                      bool *valid) {
    RFrontendContext *context = body->frontend;
    const uint32_t need = supplied < fixed_count ? fixed_count - supplied : 0U;
    const uint32_t extras = supplied > fixed_count ? supplied - fixed_count : 0U;
    const RTypeId spread_type = r_semantic_value_type(context, operand->type);
    const RSemanticAggregate *tuple = r_tuple_aggregate(context, spread_type);
    RExpressionResult *pieces = NULL;
    RExpressionResult *elements = NULL;
    RTypeVector prefix = {0};
    uint32_t piece_count = 0U;
    bool success = false;

    *valid = false;
    if (!has_pack && (extras != 0U)) {
        return r_body_diagnostic(body,
                                 "R-DIAG-TYPE-001",
                                 "R-EXPR-0020",
                                 "generic function call has the wrong argument count",
                                 span);
    }
    if ((tuple == NULL) && (r_generic_pack(context, spread_type) == NULL)) {
        return r_body_diagnostic(
            body, "R-DIAG-TYPE-001", "R-TYPE-0053", "a spread forwards a tuple or a pack", span);
    }
    if (splice) {
        /* R-TYPE-0044 (L18.4): the pack of a callable signature `fn(..., P...)` is matched by a
           spread of a P value alone; an instantiation passes its elements as the arguments. */
        RSymbolId root;
        if ((need != 0U) || (extras != 0U) || (tuple != NULL)) {
            return r_body_diagnostic(body,
                                     "R-DIAG-TYPE-001",
                                     "R-TYPE-0053",
                                     "the pack of a callable signature is supplied by a spread of "
                                     "a value of that pack",
                                     span);
        }
        if (!r_pack_partition(body, operand, span, statements, staged, &root) ||
            !r_pack_piece_value(body,
                                R_HIR_PACK_ARGS,
                                root,
                                0U,
                                spread_type,
                                operand,
                                span,
                                &values[fixed_count])) {
            return false;
        }
        *valid = true;
        return true;
    }
    if (has_pack && (need == 0U) && (extras == 0U)) {
        /* The spread forwards the whole pack. */
        values[fixed_count] = *operand;
        *valid = true;
        return true;
    }
    if (tuple != NULL) {
        const uint32_t arity = tuple->field_count;
        if (has_pack ? (arity < need) : (arity != need)) {
            return r_body_diagnostic(body,
                                     "R-DIAG-TYPE-001",
                                     "R-EXPR-0020",
                                     "generic function call has the wrong argument count",
                                     span);
        }
        if (!r_tuple_spread_lowered(body, operand, span, statements, &pieces, &piece_count)) {
            goto cleanup;
        }
        for (uint32_t index = 0U; index < need; ++index) {
            values[supplied + index] = pieces[index];
        }
        if (has_pack) {
            RExpressionResult pack = {0};
            elements = r_context_allocate(
                context, ((size_t)extras + (size_t)arity - (size_t)need + 1U) * sizeof(*elements));
            if (elements == NULL) {
                goto cleanup;
            }
            for (uint32_t index = 0U; index < extras; ++index) {
                elements[index] = values[fixed_count + index];
            }
            for (uint32_t index = need; index < arity; ++index) {
                elements[extras + index - need] = pieces[index];
            }
            if (!r_tuple_build(body, elements, extras + arity - need, span, &pack)) {
                goto cleanup;
            }
            if (!pack.valid) {
                success = true;
                goto cleanup;
            }
            values[fixed_count] = pack;
        }
        *valid = true;
        success = true;
        goto cleanup;
    }
    {
        uint32_t minimum;
        uint32_t maximum;
        bool maximum_known;
        RSymbolId root;
        r_pack_length_bounds(context, spread_type, &minimum, &maximum, &maximum_known);
        if (has_pack ? (minimum < need)
                     : (!maximum_known || (minimum != need) || (maximum != need))) {
            success = r_body_diagnostic(
                body,
                "R-DIAG-TYPE-001",
                "R-TYPE-0053",
                has_pack ? "a spread of a pack into fixed parameters needs a length the enclosing "
                           "`@if (len(P...) ...)` proves"
                         : "a spread of a pack into fixed parameters needs the exact length an "
                           "enclosing `@if (len(P...) == N)` proves",
                span);
            goto cleanup;
        }
        if (!r_pack_partition(body, operand, span, statements, false, &root)) {
            goto cleanup;
        }
        for (uint32_t index = 0U; index < need; ++index) {
            RTypeId element;
            if (!r_pack_element_type(context, spread_type, index, &element) ||
                !r_pack_piece_value(body,
                                    R_HIR_PACK_ELEMENT,
                                    root,
                                    index,
                                    element,
                                    operand,
                                    span,
                                    &values[supplied + index])) {
                goto cleanup;
            }
        }
        if (has_pack) {
            RTypeId derived;
            RExpressionResult pack = {0};
            for (uint32_t index = 0U; index < extras; ++index) {
                if (!r_type_vector_push(
                        context,
                        &prefix,
                        r_semantic_value_type(context, values[fixed_count + index].type))) {
                    goto cleanup;
                }
            }
            if (!r_pack_derived_type(context, &prefix, spread_type, need, &derived) ||
                !r_pack_build(body,
                              &values[fixed_count],
                              extras,
                              root,
                              need,
                              derived,
                              operand,
                              span,
                              &pack)) {
                goto cleanup;
            }
            values[fixed_count] = pack;
        }
        *valid = true;
        success = true;
    }

cleanup:
    r_context_free(context, pieces);
    r_context_free(context, elements);
    r_context_free(context, prefix.items);
    return success;
}

/* Whether the parameter chain of a signature ends in a pack expansion `P...` (L18.4). */
static bool r_pack_chain_expands(const RFrontendContext *context, RTypeId chain) {
    for (uint32_t depth = 0U;
         (chain != R_TYPE_ID_INVALID) && (depth <= context->options.limits.max_nesting);
         ++depth) {
        const RSemanticType *link = r_semantic_type(context, chain);
        if ((link == NULL) || (link->kind != R_SEMANTIC_TYPE_FUNCTION_PARAMETER)) {
            return false;
        }
        if (link->length == UINT64_C(1)) {
            return true;
        }
        chain = link->second;
    }
    return false;
}

/* The number of links of a parameter chain. */
static uint64_t r_pack_chain_count(const RFrontendContext *context, RTypeId chain) {
    uint64_t count = 0U;
    while (chain != R_TYPE_ID_INVALID) {
        const RSemanticType *link = r_semantic_type(context, chain);
        if ((link == NULL) || (link->kind != R_SEMANTIC_TYPE_FUNCTION_PARAMETER)) {
            break;
        }
        count += 1U;
        chain = link->second;
    }
    return count;
}

/* R-TYPE-0053 (L18.4): the link `P...` of a signature after substitution: the elements of the
   tuple the pack became, one link each, or a link that still expands a pack. */
static bool r_pack_substitute_expansion(RFrontendContext *context,
                                        const RSemanticType *link,
                                        uint32_t schema_id,
                                        uint32_t first_argument,
                                        RTypeId *result) {
    const RSemanticType expansion = *link;
    RTypeVector elements = {0};
    RTypeId base;
    RTypeId rest;
    bool success = false;
    if (!r_generic_substitute_type(context, expansion.base, schema_id, first_argument, &base) ||
        !r_generic_substitute_type(context, expansion.second, schema_id, first_argument, &rest)) {
        return false;
    }
    if (r_tuple_aggregate(context, base) == NULL) {
        return r_semantic_intern_derived_type(context,
                                              R_SEMANTIC_TYPE_FUNCTION_PARAMETER,
                                              base,
                                              rest,
                                              UINT64_C(1),
                                              expansion.flags,
                                              result);
    }
    if (!r_pack_tuple_elements(context, base, &elements)) {
        goto cleanup;
    }
    *result = rest;
    for (size_t index = elements.count; index != 0U; --index) {
        if (!r_semantic_intern_derived_type(context,
                                            R_SEMANTIC_TYPE_FUNCTION_PARAMETER,
                                            elements.items[index - 1U],
                                            *result,
                                            UINT64_C(0),
                                            R_SEMANTIC_TYPE_FLAG_NONE,
                                            result)) {
            goto cleanup;
        }
    }
    success = true;

cleanup:
    r_context_free(context, elements.items);
    return success;
}

/* R-TYPE-0053 (L18.4): the parameter chain `pattern` ending in `P...` against the chain of a
   signature: fixed links match in order, and the pack takes the tuple of the remaining types,
   or the pack an expanded remainder of the actual chain forms. */
static bool r_pack_infer_chain(RFrontendContext *context,
                               RTypeId pattern,
                               RTypeId actual,
                               uint32_t schema_id,
                               RTypeVector *arguments,
                               uint32_t depth) {
    RTypeVector rest = {0};
    bool success = false;
    while (pattern != R_TYPE_ID_INVALID) {
        const RSemanticType *pattern_link = r_semantic_type(context, pattern);
        if ((pattern_link == NULL) || (depth > context->options.limits.max_nesting)) {
            return false;
        }
        const RSemanticType expected = *pattern_link;
        if (expected.length == UINT64_C(1)) {
            break;
        }
        const RSemanticType *actual_link = r_semantic_type(context, actual);
        if ((actual_link == NULL) || (actual_link->length != UINT64_C(0))) {
            return false;
        }
        const RSemanticType found = *actual_link;
        if (!r_generic_infer_type(
                context, expected.base, found.base, schema_id, arguments, depth + 1U)) {
            return false;
        }
        pattern = expected.second;
        actual = found.second;
        depth += 1U;
    }
    if (pattern == R_TYPE_ID_INVALID) {
        return actual == R_TYPE_ID_INVALID;
    }
    const RTypeId pack = r_semantic_type(context, pattern)->base;
    RTypeId expanded = R_TYPE_ID_INVALID;
    RTypeId tail = R_TYPE_ID_INVALID;
    while (actual != R_TYPE_ID_INVALID) {
        const RSemanticType *actual_link = r_semantic_type(context, actual);
        if (actual_link == NULL) {
            goto cleanup;
        }
        if (actual_link->length == UINT64_C(1)) {
            expanded = actual_link->base;
            break;
        }
        if (!r_type_vector_push(context, &rest, actual_link->base)) {
            goto cleanup;
        }
        actual = actual_link->second;
    }
    if (expanded != R_TYPE_ID_INVALID) {
        if (!r_pack_derived_type(context, &rest, expanded, 0U, &tail)) {
            goto cleanup;
        }
    } else if (!r_tuple_type(context, &rest, (RSourceSpan){0}, &tail)) {
        goto cleanup;
    }
    success = r_generic_infer_type(context, pack, tail, schema_id, arguments, depth + 1U);

cleanup:
    r_context_free(context, rest.items);
    return success;
}

/* R-TYPE-0044 (L18.4): the arguments a spread stands for in a call through a callable
   signature `fn(..., P...)` whose `fixed_count` fixed parameters the `supplied` earlier arguments
   supply: one value that an instantiation replaces by the elements. NULL `values` after a
   diagnostic. */
static bool r_pack_spread_callable_values(RBodyContext *body,
                                          RAstRef spread,
                                          RHirVector *statements,
                                          uint32_t fixed_count,
                                          uint32_t supplied,
                                          bool staged,
                                          RExpressionResult **values,
                                          uint32_t *count) {
    RFrontendContext *context = body->frontend;
    RAstNodeView view;
    RAstRef operand_ref;
    RExpressionResult operand = {0};
    RExpressionResult *slots = NULL;
    bool valid = false;

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
    slots = r_context_allocate(context, ((size_t)fixed_count + 2U) * sizeof(*slots));
    if (slots == NULL) {
        return false;
    }
    (void)memset(slots, 0, ((size_t)fixed_count + 2U) * sizeof(*slots));
    if (!r_pack_spread_call_values(body,
                                   &operand,
                                   view.span,
                                   statements,
                                   fixed_count,
                                   supplied,
                                   true,
                                   true,
                                   staged,
                                   slots,
                                   &valid)) {
        r_context_free(context, slots);
        return false;
    }
    if (!valid) {
        r_context_free(context, slots);
        return true;
    }
    *values = r_context_allocate(context, 2U * sizeof(**values));
    if (*values == NULL) {
        r_context_free(context, slots);
        return false;
    }
    (void)memset(*values, 0, 2U * sizeof(**values));
    (*values)[0] = slots[fixed_count];
    *count = 1U;
    r_context_free(context, slots);
    return true;
}

/* R-TYPE-0053 (L18.4): the derived pack `pattern` of a prefix and all elements of a pack
   against a tuple, or against a pack of the same shape: the prefix matches the leading
   elements, and the pack takes the rest. */
static bool r_pack_infer_derived(RFrontendContext *context,
                                 RTypeId pattern,
                                 RTypeId actual,
                                 uint32_t schema_id,
                                 RTypeVector *arguments,
                                 uint32_t depth) {
    const RGenericParameter derived = *r_pack_record(context, pattern);
    RTypeVector prefix = {0};
    RTypeVector elements = {0};
    RTypeVector rest = {0};
    RTypeId tail = R_TYPE_ID_INVALID;
    bool success = false;
    if ((derived.pack_index != 0U) || (depth > context->options.limits.max_nesting) ||
        !r_semantic_chain_arguments(context, derived.pack_prefix, &prefix)) {
        goto cleanup;
    }
    if (r_tuple_aggregate(context, actual) != NULL) {
        if (!r_pack_tuple_elements(context, actual, &elements) || (elements.count < prefix.count)) {
            goto cleanup;
        }
        for (size_t index = prefix.count; index < elements.count; ++index) {
            if (!r_type_vector_push(context, &rest, elements.items[index])) {
                goto cleanup;
            }
        }
        if (!r_tuple_type(context, &rest, (RSourceSpan){0}, &tail)) {
            goto cleanup;
        }
    } else {
        const RGenericParameter *other = r_pack_record(context, actual);
        if ((other == NULL) || !other->is_pack || !other->pack_derived ||
            (other->pack_prefix_count < prefix.count)) {
            goto cleanup;
        }
        const RGenericParameter source = *other;
        if (!r_semantic_chain_arguments(context, source.pack_prefix, &elements)) {
            goto cleanup;
        }
        for (size_t index = prefix.count; index < elements.count; ++index) {
            if (!r_type_vector_push(context, &rest, elements.items[index])) {
                goto cleanup;
            }
        }
        if (!r_pack_derived_type(context, &rest, source.pack_base, source.pack_index, &tail)) {
            goto cleanup;
        }
    }
    success = true;
    for (size_t index = 0U; success && (index < prefix.count); ++index) {
        success = r_generic_infer_type(
            context, prefix.items[index], elements.items[index], schema_id, arguments, depth + 1U);
    }
    success = success && r_generic_infer_type(
                             context, derived.pack_base, tail, schema_id, arguments, depth + 1U);

cleanup:
    r_context_free(context, prefix.items);
    r_context_free(context, elements.items);
    r_context_free(context, rest.items);
    return success;
}

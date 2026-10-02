/* Opaque results are nominal facades checked as constrained parameters. The executable
 * representation is the single concrete return type; no runtime witness or box exists. */
static const RGenericParameter *r_opaque_parameter(const RFrontendContext *context, RTypeId id) {
    const RSemanticType *type = r_semantic_type(context, id);
    if (type == NULL || type->kind != R_SEMANTIC_TYPE_PARAMETER || type->base == 0U ||
        type->base > context->generic_parameter_count)
        return NULL;
    const RGenericParameter *parameter = &context->generic_parameters[type->base - 1U];
    return parameter->opaque_declaration.source != 0U ? parameter : NULL;
}

RTypeId r_semantic_representation_type(const RFrontendContext *context, RTypeId id) {
    for (uint32_t depth = 0U; depth <= context->options.limits.max_nesting; ++depth) {
        const RSemanticType *type = r_semantic_type(context, id);
        if (type != NULL && type->representation != 0U)
            return type->representation;
        const RGenericParameter *parameter = r_opaque_parameter(context, id);
        if (parameter == NULL || parameter->opaque_concrete == 0U)
            return id;
        id = parameter->opaque_concrete;
    }
    return R_TYPE_ID_INVALID;
}

static bool
r_opaque_prepare_type(RFrontendContext *context, RTypeId id, uint32_t depth, RTypeId *result) {
    const RSemanticType *description = r_semantic_type(context, id);
    *result = id;
    if (description == NULL)
        return true;
    if (depth > context->options.limits.max_nesting)
        return false;
    const RSemanticType type = *description;
    if (type.representation != 0U) {
        *result = type.representation;
        return true;
    }
    const RGenericParameter *opaque = r_opaque_parameter(context, id);
    if (opaque != NULL && opaque->opaque_concrete != 0U &&
        !r_generic_type_is_dependent(context, opaque->opaque_concrete, 0U)) {
        if (!r_opaque_prepare_type(context, opaque->opaque_concrete, depth + 1U, result))
            return false;
    } else if (type.kind >= R_SEMANTIC_TYPE_FIXED_ARRAY) {
        RTypeId base, second;
        if (!r_opaque_prepare_type(context, type.base, depth + 1U, &base) ||
            !r_opaque_prepare_type(context, type.second, depth + 1U, &second) ||
            !r_semantic_intern_derived_type(
                context, type.kind, base, second, type.length, type.flags, result))
            return false;
    }
    context->semantic_types[id - 1U].representation = *result;
    return true;
}

static bool r_opaque_prepare_representations(RFrontendContext *context) {
    bool present = false;
    for (size_t index = 0U; index < context->generic_parameter_count; ++index)
        present = present || context->generic_parameters[index].opaque_declaration.source != 0U;
    if (!present || r_semantic_has_errors(context))
        return true;
    /* Interning can grow the table; each new entry is itself canonicalized. */
    for (size_t index = 0U; index < context->semantic_type_count; ++index) {
        RTypeId representation;
        if (!r_opaque_prepare_type(context, (RTypeId)index + 1U, 0U, &representation))
            return false;
    }
    /* This is the final semantic-to-executable boundary. Generic instance keys keep
     * facade identities, so two opaque declarations never merge their static state. */
    for (size_t index = 0U; index < context->semantic_symbol_count; ++index) {
        RSemanticSymbol *symbol = &context->semantic_symbols[index];
        if (r_opaque_parameter(context, symbol->return_type) != NULL)
            symbol->opaque_result_contract = symbol->return_type;
        symbol->type = r_semantic_representation_type(context, symbol->type);
        symbol->return_type = r_semantic_representation_type(context, symbol->return_type);
        symbol->throws_type = r_semantic_representation_type(context, symbol->throws_type);
        symbol->effect_carrier_type =
            r_semantic_representation_type(context, symbol->effect_carrier_type);
        symbol->async_task_type = r_semantic_representation_type(context, symbol->async_task_type);
        symbol->async_start_type =
            r_semantic_representation_type(context, symbol->async_start_type);
        symbol->json_type = r_semantic_representation_type(context, symbol->json_type);
    }
    for (size_t index = 0U; index < context->semantic_parameter_type_count; ++index)
        context->semantic_parameter_types[index] =
            r_semantic_representation_type(context, context->semantic_parameter_types[index]);
    for (size_t index = 0U; index < context->semantic_field_count; ++index)
        context->semantic_fields[index].type =
            r_semantic_representation_type(context, context->semantic_fields[index].type);
    for (size_t index = 0U; index < context->hir_node_count; ++index) {
        RHirNode *node = &context->hir_nodes[index];
        node->type = r_semantic_representation_type(context, node->type);
        node->auxiliary_type = r_semantic_representation_type(context, node->auxiliary_type);
        node->runtime_type = r_semantic_representation_type(context, node->runtime_type);
        if (node->kind == R_HIR_MOVE && r_semantic_type_is_copy(context, node->type)) {
            node->kind = R_HIR_LOAD;
            node->operation = R_TOKEN_INVALID;
            node->is_move = false;
        }
    }
    for (size_t index = 0U; index < context->hir_effect_exit_count; ++index)
        context->hir_effect_exits[index].error_type =
            r_semantic_representation_type(context, context->hir_effect_exits[index].error_type);
    for (size_t index = 0U; index < context->error_borrow_mapping_count; ++index)
        context->error_borrow_mappings[index].error_type = r_semantic_representation_type(
            context, context->error_borrow_mappings[index].error_type);
    for (size_t index = 0U; index < context->semantic_variant_count; ++index)
        context->semantic_variants[index].payload_type =
            r_semantic_representation_type(context, context->semantic_variants[index].payload_type);
    /* An opaque Move value may have a trivially Copy representation. Its source is
     * still consumed by the checked body, but executable cleanup has nothing to do. */
    for (size_t index = 0U; index < context->hir_node_count; ++index) {
        const RHirNode node = context->hir_nodes[index];
        RHirVector children = {0};
        bool changed = false;
        for (uint32_t child = 0U; child < node.child_count; ++child) {
            const RHirNode *part =
                &context->hir_nodes[context->hir_children[node.first_child + child] - 1U];
            if (part->kind == R_HIR_DROP && part->symbol != 0U &&
                r_semantic_type_is_copy(context, context->semantic_symbols[part->symbol - 1U].type))
                changed = true;
        }
        if (!changed)
            continue;
        for (uint32_t child = 0U; child < node.child_count; ++child) {
            const RHirNodeId child_id = context->hir_children[node.first_child + child];
            const RHirNode *part = &context->hir_nodes[child_id - 1U];
            if (node.kind == R_HIR_VALUE_SCOPE && child == node.integer_value) {
                context->hir_nodes[index].integer_value = children.count;
            }
            if (part->kind == R_HIR_DROP && part->symbol != 0U &&
                r_semantic_type_is_copy(context,
                                        context->semantic_symbols[part->symbol - 1U].type)) {
                changed = true;
                continue;
            }
            if (!r_hir_vector_push(context, &children, child_id)) {
                r_context_free(context, children.items);
                return false;
            }
        }
        const bool committed =
            !changed || r_hir_commit_children(context, (RHirNodeId)index + 1U, &children);
        r_context_free(context, children.items);
        if (!committed)
            return false;
    }
    return true;
}

/* R-TYPE-0045 (L16.3): associated type `index` of `trait` takes type parameters; no
 * `Name = Type` equality fixes it. */
static bool r_opaque_associated_has_parameters(const RFrontendContext *context,
                                               uint32_t trait,
                                               uint32_t index) {
    const RSemanticTrait origin =
        context->semantic_traits[r_semantic_trait_origin(context, trait) - 1U];
    return context->semantic_associated_types[origin.first_associated + index].parameter_schema !=
           0U;
}

/* The `Name = Type` equalities of an opaque result (R-TYPE-0048) or of a dyn interface
 * (R-TYPE-0051); every advertised associated type must be fixed. */
static bool r_opaque_associated_contract(RFrontendContext *context,
                                         RTypeId facade,
                                         RAstRef contract,
                                         bool dyn) {
    const char *rule = dyn ? "R-TYPE-0051" : "R-TYPE-0048";
    RAstNodeView view;
    if (!r_ast_ref_view(context, contract, &view))
        return false;
    for (uint32_t index = 0U; index < view.child_count; ++index) {
        RAstRef binding, type_ref;
        RAstNodeView binding_view;
        RAstTokenView name;
        if (!r_ast_ref_child(context, contract, index, &binding) ||
            !r_ast_ref_view(context, binding, &binding_view))
            return false;
        if (binding_view.kind != R_SYNTAX_ASSOCIATED_TYPE)
            continue;
        RTypeId projection, bound;
        bool ambiguous = false;
        if (!r_semantic_first_identifier(context, binding, &name) ||
            !r_ast_find_direct_child(context, binding, R_SYNTAX_TYPE, &type_ref))
            return false;
        if (!r_semantic_parameter_projection(
                context, facade, name.intern_id, &projection, &ambiguous))
            return r_semantic_add_diagnostic(context,
                                             "R-DIAG-TRAIT-001",
                                             rule,
                                             dyn ? "a dyn associated binding must name one "
                                                   "unambiguous advertised associated type"
                                                 : "an opaque associated binding must name one "
                                                   "unambiguous advertised associated type",
                                             binding_view.span);
        const RSemanticType *type = r_semantic_type(context, projection);
        if (type == NULL || type->kind != R_SEMANTIC_TYPE_PARAMETER ||
            context->generic_parameters[type->base - 1U].projection_base != facade ||
            context->generic_parameters[type->base - 1U].projection_bound != 0U)
            return r_semantic_add_diagnostic(
                context,
                "R-DIAG-TRAIT-001",
                rule,
                dyn ? "a dyn associated type may be bound only once"
                    : "an opaque associated type may be bound only once",
                binding_view.span);
        const uint32_t slot = type->base - 1U;
        if (r_opaque_associated_has_parameters(context,
                                               context->generic_parameters[slot].projection_trait,
                                               context->generic_parameters[slot].projection_index))
            return r_semantic_add_diagnostic(
                context,
                "R-DIAG-TRAIT-001",
                rule,
                dyn ? "a dyn interface contract cannot fix an associated type with parameters"
                    : "an opaque result contract cannot fix an associated type with parameters",
                binding_view.span);
        if (!r_semantic_type_from_ast(context, type_ref, &bound))
            return false;
        if (!r_semantic_type_is_slice_scalar(context, bound, false) ||
            r_semantic_type_is_const(context, bound))
            return r_semantic_add_diagnostic(
                context,
                "R-DIAG-TYPE-001",
                rule,
                dyn ? "a dyn associated binding requires a complete value type"
                    : "an opaque associated binding requires a complete value type",
                binding_view.span);
        context->generic_parameters[slot].projection_bound = bound;
    }
    for (uint32_t trait = 1U; trait <= context->semantic_trait_count; ++trait) {
        const RSemanticTrait record = context->semantic_traits[trait - 1U];
        if (record.associated_count == 0U ||
            (record.type_parameter_schema != 0U && record.generic_origin == 0U) ||
            !r_semantic_type_implements_trait(context, facade, trait))
            continue;
        for (uint32_t index = 0U; index < record.associated_count; ++index) {
            bool bound = false;
            if (r_opaque_associated_has_parameters(context, trait, index))
                return r_semantic_add_diagnostic(
                    context,
                    "R-DIAG-TRAIT-001",
                    rule,
                    dyn ? "a dyn interface contract cannot fix an associated type with "
                          "parameters"
                        : "an opaque result contract cannot fix an associated type with "
                          "parameters",
                    view.span);
            for (size_t slot = 0U; slot < context->generic_parameter_count; ++slot) {
                const RGenericParameter projection = context->generic_parameters[slot];
                if (projection.projection_base == facade && projection.projection_trait == trait &&
                    projection.projection_index == index && projection.projection_bound != 0U)
                    bound = true;
            }
            if (!bound)
                return r_semantic_add_diagnostic(
                    context,
                    "R-DIAG-TRAIT-001",
                    rule,
                    dyn ? "dyn interface contracts must fix their "
                          "advertised associated types with Name = Type"
                        : "opaque result contracts must fix their "
                          "advertised associated types with Name = Type",
                    view.span);
        }
    }
    return true;
}

static bool r_opaque_result_type(RFrontendContext *context, RAstRef type_ref, RTypeId *result) {
    RAstRef contract;
    RAstNodeView view;
    if (!r_ast_find_direct_child(context, type_ref, R_SYNTAX_OPAQUE_RESULT, &contract))
        return r_semantic_type_from_ast(context, type_ref, result);
    if (!r_ast_ref_view(context, contract, &view))
        return false;
    for (size_t index = 0U; index < context->generic_parameter_count; ++index) {
        const RGenericParameter parameter = context->generic_parameters[index];
        if (parameter.opaque_declaration.source == contract.source &&
            parameter.opaque_declaration.node == contract.node && parameter.opaque_origin == 0U) {
            *result = parameter.type;
            return true;
        }
    }
    RGenericParameter parameter = {0};
    parameter.opaque_declaration = contract;
    parameter.opaque_schema = r_generic_schema_at(context, view.span);
    parameter.name_span = view.span;
    char name[80];
    const int length = snprintf(
        name, sizeof(name), "$opaque%u_%u", (unsigned)contract.source, (unsigned)view.span.start);
    if (length <= 0 || (size_t)length >= sizeof(name) ||
        !r_closure_intern(context, name, &parameter.name_intern_id) ||
        !r_grow_array(context,
                      (void **)&context->generic_parameters,
                      &context->generic_parameter_capacity,
                      sizeof(parameter),
                      context->generic_parameter_count + 1U) ||
        !r_semantic_intern_type(context,
                                R_SEMANTIC_TYPE_PARAMETER,
                                (RTypeId)context->generic_parameter_count + 1U,
                                &parameter.type))
        return false;
    const uint32_t index = (uint32_t)context->generic_parameter_count;
    context->generic_parameters[context->generic_parameter_count++] = parameter;
    *result = parameter.type;
    for (uint32_t child = 0U; child < view.child_count; ++child) {
        RAstRef bound;
        RAstNodeView bound_view;
        if (!r_ast_ref_child(context, contract, child, &bound) ||
            !r_ast_ref_view(context, bound, &bound_view))
            return false;
        if (bound_view.kind == R_SYNTAX_TRAIT_NAME) {
            if (!r_semantic_trait_bound(context, bound, index, false))
                return false;
        } else if (bound_view.kind == R_SYNTAX_CALLABLE_CONSTRAINT) {
            RGenericPendingCallable pending = {0};
            pending.parameter = index;
            pending.node = bound;
            if (!r_generic_resolve_callable_constraint(context, pending))
                return false;
        }
    }
    return r_opaque_associated_contract(context, parameter.type, contract, false);
}

static bool r_opaque_refresh(RFrontendContext *context, uint32_t index) {
    RGenericParameter parameter = context->generic_parameters[index];
    if (parameter.opaque_origin == 0U || parameter.opaque_concrete != 0U)
        return true;
    const RGenericParameter origin = context->generic_parameters[parameter.opaque_origin - 1U];
    if (origin.opaque_concrete == 0U)
        return true;
    if (!r_generic_substitute_type(context,
                                   origin.opaque_concrete,
                                   parameter.opaque_schema,
                                   parameter.opaque_first_argument,
                                   &parameter.opaque_concrete))
        return false;
    context->generic_parameters[index].opaque_concrete = parameter.opaque_concrete;
    return true;
}

static bool r_opaque_substitute(RFrontendContext *context,
                                RTypeId id,
                                uint32_t schema,
                                uint32_t first_argument,
                                RTypeId *result) {
    const RGenericParameter parameter = *r_opaque_parameter(context, id);
    *result = id;
    if (parameter.opaque_schema == 0U)
        return true;
    RTypeVector arguments = {0};
    const RGenericSchema owner = context->generic_schemas[parameter.opaque_schema - 1U];
    const RGenericSchema substitution = context->generic_schemas[schema - 1U];
    if (parameter.opaque_origin == 0U && (owner.first_parameter != substitution.first_parameter ||
                                          owner.parameter_count != substitution.parameter_count))
        return true;
    bool success = false;
    for (uint32_t index = 0U; index < owner.parameter_count; ++index) {
        RTypeId argument = parameter.opaque_origin == 0U
                               ? context->generic_arguments[first_argument + index]
                               : R_TYPE_ID_INVALID;
        if (parameter.opaque_origin != 0U &&
            !r_generic_substitute_type(
                context,
                context->generic_arguments[parameter.opaque_first_argument + index],
                schema,
                first_argument,
                &argument))
            goto cleanup;
        if (!r_type_vector_push(context, &arguments, argument))
            goto cleanup;
    }
    const RSemanticType *type = r_semantic_type(context, id);
    const uint32_t origin_index =
        parameter.opaque_origin != 0U ? parameter.opaque_origin : type->base;
    for (size_t index = 0U; index < context->generic_parameter_count; ++index) {
        const RGenericParameter candidate = context->generic_parameters[index];
        if (candidate.opaque_origin == origin_index &&
            memcmp(context->generic_arguments + candidate.opaque_first_argument,
                   arguments.items,
                   arguments.count * sizeof(RTypeId)) == 0) {
            *result = candidate.type;
            success = r_opaque_refresh(context, (uint32_t)index);
            goto cleanup;
        }
    }
    RGenericParameter instance = context->generic_parameters[origin_index - 1U];
    instance.opaque_origin = origin_index;
    instance.opaque_concrete = 0U;
    instance.first_trait_constraint = 0U;
    instance.trait_constraint_count = 0U;
    if (!r_generic_commit_arguments(context, &arguments, &instance.opaque_first_argument) ||
        !r_grow_array(context,
                      (void **)&context->generic_parameters,
                      &context->generic_parameter_capacity,
                      sizeof(instance),
                      context->generic_parameter_count + 1U) ||
        !r_semantic_intern_type(context,
                                R_SEMANTIC_TYPE_PARAMETER,
                                (RTypeId)context->generic_parameter_count + 1U,
                                &instance.type))
        goto cleanup;
    const uint32_t instance_index = (uint32_t)context->generic_parameter_count;
    context->generic_parameters[context->generic_parameter_count++] = instance;
    *result = instance.type;
    const RGenericParameter origin = context->generic_parameters[origin_index - 1U];
    for (uint32_t index = 0U; index < origin.trait_constraint_count; ++index) {
        uint32_t bound = context->generic_trait_constraints[origin.first_trait_constraint + index];
        if (!r_generic_effective_callable_trait(context,
                                                bound,
                                                instance.opaque_schema,
                                                &arguments,
                                                instance.opaque_declaration.source,
                                                &bound) ||
            !r_semantic_append_trait_constraint(context, instance_index, bound))
            goto cleanup;
    }
    const size_t parameter_count = context->generic_parameter_count;
    for (size_t slot = 0U; slot < parameter_count; ++slot) {
        const RGenericParameter binding = context->generic_parameters[slot];
        if (binding.projection_base != origin.type || binding.projection_bound == 0U)
            continue;
        uint32_t trait = binding.projection_trait;
        RTypeId projection, bound;
        if (!r_generic_effective_callable_trait(context,
                                                trait,
                                                instance.opaque_schema,
                                                &arguments,
                                                instance.opaque_declaration.source,
                                                &trait) ||
            !r_semantic_trait_projection(
                context, instance.type, trait, binding.projection_index, &projection) ||
            !r_generic_substitute_type(context,
                                       binding.projection_bound,
                                       instance.opaque_schema,
                                       instance.opaque_first_argument,
                                       &bound))
            goto cleanup;
        const RSemanticType *projection_type = r_semantic_type(context, projection);
        context->generic_parameters[projection_type->base - 1U].projection_bound = bound;
    }
    success = r_opaque_refresh(context, instance_index);
cleanup:
    r_context_free(context, arguments.items);
    return success;
}

static bool r_opaque_record_return(RBodyContext *body, RTypeId concrete, RSourceSpan span) {
    RFrontendContext *context = body->frontend;
    const RGenericParameter *found = r_opaque_parameter(context, body->return_type);
    if (found == NULL)
        return true;
    const RGenericParameter parameter = *found;
    const RSemanticType *facade_type = r_semantic_type(context, body->return_type);
    const uint32_t origin_index =
        parameter.opaque_origin != 0U ? parameter.opaque_origin : facade_type->base;
    const RTypeId previous_concrete =
        context->generic_parameters[origin_index - 1U].opaque_concrete;
    if (concrete == body->return_type)
        return r_body_diagnostic(body,
                                 "R-DIAG-TYPE-001",
                                 "R-TYPE-0048",
                                 "all returns of an opaque result must have one concrete type",
                                 span);
    if (!r_semantic_type_is_slice_scalar(context, concrete, false))
        return r_body_diagnostic(body,
                                 "R-DIAG-TYPE-001",
                                 "R-TYPE-0048",
                                 "an opaque result requires a complete sized inhabited value",
                                 span);
    for (uint32_t capability = R_GENERIC_COPY; capability <= R_GENERIC_CLONE;
         capability = r_generic_next_capability(capability))
        if ((parameter.constraints & capability) != 0U &&
            !r_generic_has_property(context, concrete, capability, 0U))
            return r_body_diagnostic(
                body,
                "R-DIAG-TYPE-001",
                "R-TYPE-0048",
                "the concrete result does not prove an advertised opaque capability",
                span);
    for (uint32_t index = 0U; index < parameter.trait_constraint_count; ++index)
        if (!r_semantic_type_implements_trait(
                context,
                concrete,
                context->generic_trait_constraints[parameter.first_trait_constraint + index]))
            return r_body_diagnostic(
                body,
                "R-DIAG-TRAIT-001",
                "R-TYPE-0048",
                "the concrete result does not implement an advertised opaque trait",
                span);
    if (parameter.opaque_schema != 0U) {
        const RGenericSchema schema = context->generic_schemas[parameter.opaque_schema - 1U];
        RTypeVector arguments = {0};
        bool canonicalized = true;
        uint32_t first_argument;
        for (uint32_t index = 0U; canonicalized && index < schema.parameter_count; ++index)
            canonicalized = r_type_vector_push(
                context,
                &arguments,
                r_static_origin(context,
                                context->generic_parameters[schema.first_parameter + index].type));
        canonicalized = canonicalized &&
                        r_generic_commit_arguments(context, &arguments, &first_argument) &&
                        r_generic_substitute_type(
                            context, concrete, parameter.opaque_schema, first_argument, &concrete);
        r_context_free(context, arguments.items);
        if (!canonicalized)
            return false;
    }
    if (previous_concrete != 0U && previous_concrete != concrete)
        return r_body_diagnostic(body,
                                 "R-DIAG-TYPE-001",
                                 "R-TYPE-0048",
                                 "all returns of an opaque result must have one concrete type",
                                 span);
    const size_t parameter_count = context->generic_parameter_count;
    for (size_t index = 0U; index < parameter_count; ++index) {
        const RGenericParameter binding = context->generic_parameters[index];
        if (binding.projection_base != body->return_type || binding.projection_bound == 0U)
            continue;
        RTypeId actual;
        if (!r_semantic_resolve_projection(
                context, concrete, binding.projection_trait, binding.projection_index, &actual) ||
            actual != binding.projection_bound)
            return r_body_diagnostic(
                body,
                "R-DIAG-TRAIT-001",
                "R-TYPE-0048",
                "the concrete result does not match an opaque associated-type equality",
                span);
    }
    context->generic_parameters[origin_index - 1U].opaque_concrete = concrete;
    for (size_t index = 0U; index < context->generic_parameter_count; ++index)
        if (context->generic_parameters[index].opaque_origin == origin_index &&
            !r_opaque_refresh(context, (uint32_t)index))
            return false;
    return true;
}

static bool r_opaque_bind_calls(RFrontendContext *context) {
    for (size_t index = 0U; index < context->hir_node_count; ++index) {
        const RHirNode node = context->hir_nodes[index];
        /* R-TYPE-0048: a direct start of an opaque async callable binds like a call. */
        if ((node.kind != R_HIR_CALL && node.kind != R_HIR_ASYNC_START) || node.symbol == 0U)
            continue;
        const RSemanticSymbol function = context->semantic_symbols[node.symbol - 1U];
        if (!function.is_trait_prototype || function.generic_origin == 0U)
            continue;
        const RTypeId self = context->generic_arguments[function.first_generic_argument];
        const RGenericParameter *parameter = r_opaque_parameter(context, self);
        if (parameter == NULL || parameter->opaque_concrete == 0U ||
            r_generic_type_is_dependent(context, parameter->opaque_concrete, 0U))
            continue;
        RSymbolId target;
        if (!r_generic_bind_trait_call(context, node.symbol, self, &target, NULL))
            return r_semantic_add_diagnostic(
                context,
                "R-DIAG-TRAIT-001",
                "R-TYPE-0048",
                "the opaque result method cannot be bound to its concrete implementation",
                node.span);
        context->hir_nodes[index].symbol = target;
        const RSemanticAggregate *item =
            r_semantic_aggregate_for_type(context, r_semantic_representation_type(context, self));
        context->hir_nodes[index].function_item_receiver =
            item != NULL && item->function_item_target != R_SYMBOL_ID_INVALID;
        if (node.kind == R_HIR_CALL)
            context->hir_nodes[index].auxiliary_type =
                context->semantic_symbols[target - 1U].is_async
                    ? context->semantic_symbols[target - 1U].async_start_type
                    : context->semantic_symbols[target - 1U].effect_carrier_type;
    }
    return true;
}

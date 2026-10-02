/* Trait declarations share the generic capability vocabulary and nominal bounds. */
static bool r_semantic_trait_bound(RFrontendContext *context,
                                   RAstRef ref,
                                   uint32_t parameter_index,
                                   bool supertrait) {
    static const char *const names[] = {
        "copy", "pod", "send", "sync", "key", "error", "unborrowed", "json_encode", "json_decode",
        "", "clone"};
    RAstTokenView name;
    RAstNodeView view;
    if (!r_ast_ref_view(context, ref, &view) || !r_semantic_last_identifier(context, ref, &name))
        return false;
    if (view.child_count == 1U) {
        for (uint32_t index = 0U; index < sizeof(names) / sizeof(names[0]); ++index) {
            if (!r_semantic_token_text_equal_owned(context, &name, names[index]))
                continue;
            context->generic_parameters[parameter_index].constraints |= 1U << index;
            if (index == 1U)
                context->generic_parameters[parameter_index].constraints |=
                    R_GENERIC_COPY | R_GENERIC_UNBORROWED;
            return true;
        }
    }
    uint32_t trait;
    if (!r_semantic_trait_from_ast(context, ref, &trait))
        return false;
    if (trait == 0U)
        return true;
    const RSemanticType *self =
        r_semantic_type(context, context->semantic_traits[trait - 1U].self_type);
    if (supertrait && self != NULL && self->base == parameter_index + 1U)
        return r_semantic_add_diagnostic(context,
                                         "R-DIAG-TRAIT-001",
                                         "R-TYPE-0041",
                                         "a trait cannot be its own supertrait",
                                         view.span);
    return r_semantic_append_trait_constraint(context, parameter_index, trait);
}

static bool r_semantic_collect_trait_bounds(RFrontendContext *context) {
    for (uint32_t id = 1U; id <= context->semantic_trait_count; ++id) {
        const RSemanticTrait trait = context->semantic_traits[id - 1U];
        if (trait.generic_origin != 0U || trait.is_core ||
            trait.callable_signature != R_TYPE_ID_INVALID)
            continue;
        RAstRef declaration = {trait.module_source, trait.declaration_ast};
        RAstNodeView view;
        if (!r_ast_ref_view(context, declaration, &view))
            return false;
        for (uint32_t index = 0U; index < view.child_count; ++index) {
            RAstRef child;
            RAstNodeView member;
            if (!r_ast_ref_child(context, declaration, index, &child) ||
                !r_ast_ref_view(context, child, &member))
                return false;
            if (member.kind == R_SYNTAX_TRAIT_NAME) {
                if (!r_semantic_trait_bound(
                        context,
                        child,
                        context->generic_schemas[trait.generic_schema - 1U].first_parameter,
                        true))
                    return false;
            } else if (member.kind == R_SYNTAX_ASSOCIATED_TYPE) {
                RAstTokenView name;
                if (!r_semantic_associated_name(context, child, &name))
                    return false;
                const uint32_t slot = r_semantic_associated_index(context, id, name.intern_id);
                const RSemanticType *type = r_semantic_type(
                    context,
                    context->semantic_associated_types[trait.first_associated + slot]
                        .parameter_type);
                const uint32_t parameter_index = type->base - 1U;
                for (uint32_t bound = 0U; bound < member.child_count; ++bound) {
                    RAstRef constraint;
                    RAstNodeView constraint_view;
                    if (!r_ast_ref_child(context, child, bound, &constraint) ||
                        !r_ast_ref_view(context, constraint, &constraint_view))
                        continue;
                    if (constraint_view.kind == R_SYNTAX_CALLABLE_CONSTRAINT &&
                        !r_semantic_add_diagnostic(context,
                                                   "R-DIAG-TRAIT-001",
                                                   "R-TYPE-0045",
                                                   "an associated type takes capability and "
                                                   "nominal trait constraints; a callable "
                                                   "constraint belongs to a generic parameter",
                                                   constraint_view.span))
                        return false;
                    if (constraint_view.kind == R_SYNTAX_TRAIT_NAME &&
                        !r_semantic_trait_bound(context, constraint, parameter_index, false))
                        return false;
                }
            }
        }
    }
    for (uint32_t id = 1U; id <= context->semantic_trait_count; ++id) {
        RSemanticTrait *trait = &context->semantic_traits[id - 1U];
        if (trait->generic_origin != 0U)
            continue;
        const RSemanticType *self = r_semantic_type(context, trait->self_type);
        if (self == NULL || self->kind != R_SEMANTIC_TYPE_PARAMETER)
            continue;
        const RGenericParameter parameter = context->generic_parameters[self->base - 1U];
        trait->first_supertrait = parameter.first_trait_constraint;
        trait->supertrait_count = parameter.trait_constraint_count;
    }
    /* Expand each finite bound set once, retaining a cycle diagnostic. This avoids
       recursive graph searches during every method lookup and constraint proof. */
    for (uint32_t id = 1U; id <= context->semantic_trait_count; ++id) {
        const RSemanticTrait trait = context->semantic_traits[id - 1U];
        if (trait.generic_origin != 0U)
            continue;
        const RSemanticType *self = r_semantic_type(context, trait.self_type);
        if (self == NULL || self->kind != R_SEMANTIC_TYPE_PARAMETER)
            continue;
        const uint32_t parameter_index = self->base - 1U;
        for (uint32_t index = 0U;
             index < context->generic_parameters[parameter_index].trait_constraint_count;
             ++index) {
            const RGenericParameter parameter = context->generic_parameters[parameter_index];
            const uint32_t parent =
                context->generic_trait_constraints[parameter.first_trait_constraint + index];
            if (r_semantic_trait_origin(context, parent) == id)
                continue;
            const RSemanticType *parent_self =
                r_semantic_type(context, context->semantic_traits[parent - 1U].self_type);
            if (parent_self == NULL || parent_self->kind != R_SEMANTIC_TYPE_PARAMETER)
                continue;
            const RGenericParameter bounds = context->generic_parameters[parent_self->base - 1U];
            RTypeVector arguments = {0};
            uint32_t first_argument;
            const uint32_t parent_schema = context->semantic_traits[parent - 1U].generic_schema;
            const bool prepared =
                r_semantic_trait_arguments(context, parent, trait.self_type, 0U, &arguments) &&
                r_generic_commit_arguments(context, &arguments, &first_argument);
            r_context_free(context, arguments.items);
            if (!prepared)
                return false;
            for (uint32_t inherited = 0U; inherited < bounds.trait_constraint_count; ++inherited) {
                uint32_t ancestor =
                    context->generic_trait_constraints[bounds.first_trait_constraint + inherited];
                if (r_semantic_trait_origin(context, ancestor) ==
                    r_semantic_trait_origin(context, parent))
                    continue;
                if (!r_semantic_effective_trait(
                        context, ancestor, parent_schema, first_argument, &ancestor))
                    return false;
                if (r_semantic_trait_origin(context, ancestor) == id) {
                    context->semantic_traits[id - 1U].poisoned = true;
                    if (!r_semantic_add_diagnostic(context,
                                                   "R-DIAG-TRAIT-001",
                                                   "R-TYPE-0041",
                                                   "supertrait dependencies must be acyclic",
                                                   trait.span))
                        return false;
                } else if (!r_semantic_append_trait_constraint(
                               context, parameter_index, ancestor)) {
                    return false;
                }
            }
        }
    }
    return true;
}

static bool r_semantic_validate_trait_bounds(RFrontendContext *context) {
    for (uint32_t id = 1U; id <= context->semantic_impl_count; ++id) {
        const RSemanticImpl impl = context->semantic_impls[id - 1U];
        if (impl.poisoned || impl.trait == 0U)
            continue;
        const RSemanticTrait trait = context->semantic_traits[impl.trait - 1U];
        const RSemanticTrait declaration =
            context->semantic_traits[r_semantic_trait_origin(context, impl.trait) - 1U];
        RTypeVector arguments = {0};
        uint32_t first_argument;
        const bool prepared =
            r_semantic_trait_arguments(context, impl.trait, impl.target_type, id, &arguments) &&
            r_generic_commit_arguments(context, &arguments, &first_argument);
        r_context_free(context, arguments.items);
        if (!prepared)
            return false;
        for (uint32_t property = R_GENERIC_COPY; property <= R_GENERIC_CLONE;
             property = r_generic_next_capability(property)) {
            if ((r_generic_parameter_constraints(context, trait.self_type) & property) != 0U &&
                !r_generic_has_property(context, impl.target_type, property, 0U) &&
                !r_semantic_add_diagnostic(
                    context,
                    "R-DIAG-TRAIT-001",
                    "R-TYPE-0042",
                    "the target does not prove a capability required by its trait",
                    impl.span))
                return false;
        }
        RTypeId owner;
        uint32_t aggregate;
        r_semantic_canonical_owner(context, impl.target_type, &owner, &aggregate);
        for (uint32_t index = 0U; index < declaration.supertrait_count; ++index) {
            uint32_t parent =
                context->generic_trait_constraints[declaration.first_supertrait + index];
            if (r_semantic_trait_origin(context, parent) ==
                r_semantic_trait_origin(context, impl.trait))
                continue;
            if (!r_semantic_effective_trait(
                    context, parent, trait.generic_schema, first_argument, &parent))
                return false;
            bool implemented = false;
            for (uint32_t candidate = 0U; candidate < context->semantic_impl_count; ++candidate) {
                const RSemanticImpl other = context->semantic_impls[candidate];
                RTypeId other_owner;
                uint32_t other_aggregate;
                r_semantic_canonical_owner(
                    context, other.target_type, &other_owner, &other_aggregate);
                if (!other.poisoned && other_owner == owner && other_aggregate == aggregate) {
                    uint32_t effective;
                    if (!r_semantic_impl_trait(
                            context, candidate + 1U, impl.target_type, &effective))
                        return false;
                    implemented = implemented || effective == parent;
                }
            }
            if (!implemented &&
                !r_semantic_add_diagnostic(context,
                                           "R-DIAG-TRAIT-001",
                                           "R-TYPE-0042",
                                           "the target must separately implement each supertrait",
                                           impl.span))
                return false;
        }
        for (uint32_t slot = 0U; slot < trait.associated_count; ++slot) {
            RTypeId bound = context->semantic_associated_bindings[impl.first_binding + slot];
            /* R-TYPE-0045 (L16.3): a binding with parameters proves the constraints for every
               argument, so its body is checked with its parameters abstract. */
            if (r_semantic_value_kind(context, bound) == R_SEMANTIC_TYPE_TYPE_FUNCTION) {
                bound = r_semantic_type(context, bound)->base;
            }
            const RSemanticType *type = r_semantic_type(
                context,
                context->semantic_associated_types[trait.first_associated + slot].parameter_type);
            const RGenericParameter parameter = context->generic_parameters[type->base - 1U];
            for (uint32_t property = R_GENERIC_COPY; property <= R_GENERIC_CLONE;
                 property = r_generic_next_capability(property)) {
                if ((parameter.constraints & property) != 0U &&
                    !r_generic_has_property(context, bound, property, 0U) &&
                    !r_semantic_add_diagnostic(
                        context,
                        "R-DIAG-TRAIT-001",
                        "R-TYPE-0045",
                        "the associated type does not prove its required capability",
                        impl.span))
                    return false;
            }
            for (uint32_t index = 0U; index < parameter.trait_constraint_count; ++index) {
                uint32_t required =
                    context->generic_trait_constraints[parameter.first_trait_constraint + index];
                if (!r_semantic_effective_trait(
                        context, required, trait.generic_schema, first_argument, &required))
                    return false;
                if (!r_semantic_type_implements_trait(context, bound, required) &&
                    !r_semantic_add_diagnostic(
                        context,
                        "R-DIAG-TRAIT-001",
                        "R-TYPE-0045",
                        "the associated type does not implement its required trait",
                        impl.span))
                    return false;
            }
        }
    }
    return true;
}

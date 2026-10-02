/* Applied traits are compile-time identities, keyed by their declaration and type arguments. */
static uint32_t r_semantic_trait_origin(const RFrontendContext *context, uint32_t trait) {
    if (trait == 0U || trait > context->semantic_trait_count)
        return 0U;
    const uint32_t origin = context->semantic_traits[trait - 1U].generic_origin;
    return origin == 0U ? trait : origin;
}

static bool
r_semantic_probe_trait_designator(RFrontendContext *context, RAstRef ref, uint32_t *result) {
    RAstNodeView view;
    RAstTokenView name = {0};
    char module[1024];
    size_t used = 0U;
    uint32_t count = 0U;
    *result = 0U;
    if (!r_ast_ref_view(context, ref, &view))
        return false;
    for (uint32_t index = 0U; index < view.child_count; ++index) {
        RAstRef child;
        RAstTokenView token;
        if (!r_ast_ref_child(context, ref, index, &child))
            return false;
        if (!r_ast_ref_token(context, child, &token))
            continue;
        if (token.kind != R_TOKEN_IDENTIFIER)
            return true;
        if (count != 0U) {
            const char *bytes;
            size_t length;
            if (!r_semantic_intern_text(context, name.intern_id, &bytes, &length) ||
                used + length + 1U >= sizeof(module))
                return true;
            if (used != 0U)
                module[used++] = '.';
            (void)memcpy(module + used, bytes, length);
            used += length;
        }
        name = token;
        ++count;
    }
    if (count == 0U)
        return true;
    if (used == 4U && memcmp(module, "core", 4U) == 0) {
        *result = r_semantic_resolve_core_trait_name(context, name.intern_id);
    } else if (used != 0U) {
        uint32_t module_id;
        if (!r_intern_bytes(context, (const uint8_t *)module, used, &module_id))
            return false;
        *result =
            r_semantic_resolve_trait_name_in_module(context, ref.source, module_id, name.intern_id);
    } else {
        bool ambiguous;
        *result = r_semantic_resolve_trait_name(context, ref.source, name.intern_id, &ambiguous);
        if (ambiguous)
            *result = 0U;
    }
    return true;
}

static bool r_semantic_apply_trait(RFrontendContext *context,
                                   uint32_t trait,
                                   const RTypeVector *arguments,
                                   RSourceSpan span,
                                   uint32_t *result) {
    const uint32_t origin = r_semantic_trait_origin(context, trait);
    *result = 0U;
    if (origin == 0U)
        return false;
    RSemanticTrait application = context->semantic_traits[origin - 1U];
    const uint32_t count =
        application.type_parameter_schema == 0U
            ? 0U
            : context->generic_schemas[application.type_parameter_schema - 1U].parameter_count;
    if (count != arguments->count)
        return r_semantic_add_diagnostic(context,
                                         "R-DIAG-TRAIT-001",
                                         "R-TYPE-0043",
                                         "trait application has the wrong number of type arguments",
                                         span);
    if (count == 0U) {
        *result = origin;
        return true;
    }
    if (context->generic_instantiation_depth >= context->options.limits.max_nesting) {
        r_generic_limit_diagnostic(context, application.name_intern_id, span);
        context->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
        return false;
    }
    ++context->generic_instantiation_depth;
    const bool valid =
        r_generic_validate_arguments(context, application.type_parameter_schema, arguments, span);
    --context->generic_instantiation_depth;
    if (!valid)
        return false;
    for (uint32_t id = 1U; id <= context->semantic_trait_count; ++id) {
        const RSemanticTrait *candidate = &context->semantic_traits[id - 1U];
        if (candidate->generic_origin != origin)
            continue;
        bool equal = true;
        for (uint32_t index = 0U; index < count; ++index)
            equal =
                equal && context->generic_arguments[candidate->first_generic_argument + index] ==
                             arguments->items[index];
        if (equal) {
            *result = id;
            return true;
        }
    }
    if (context->semantic_trait_count >= UINT32_MAX ||
        !r_generic_commit_arguments(context, arguments, &application.first_generic_argument) ||
        !r_grow_array(context,
                      (void **)&context->semantic_traits,
                      &context->semantic_trait_capacity,
                      sizeof(application),
                      context->semantic_trait_count + 1U))
        return false;
    application.generic_origin = origin;
    application.span = span;
    context->semantic_traits[context->semantic_trait_count++] = application;
    *result = (uint32_t)context->semantic_trait_count;
    return true;
}

static bool r_semantic_effective_trait(RFrontendContext *context,
                                       uint32_t trait,
                                       uint32_t schema,
                                       uint32_t first_argument,
                                       uint32_t *result) {
    const RSemanticTrait record = context->semantic_traits[trait - 1U];
    *result = trait;
    if (record.generic_origin == 0U)
        return true;
    const uint32_t count =
        context->generic_schemas[record.type_parameter_schema - 1U].parameter_count;
    RTypeVector arguments = {0};
    bool success = true;
    for (uint32_t index = 0U; success && index < count; ++index) {
        RTypeId argument;
        success = r_generic_substitute_type(
                      context,
                      context->generic_arguments[record.first_generic_argument + index],
                      schema,
                      first_argument,
                      &argument) &&
                  r_type_vector_push(context, &arguments, argument);
    }
    success = success && r_semantic_apply_trait(
                             context, record.generic_origin, &arguments, record.span, result);
    r_context_free(context, arguments.items);
    return success;
}

static bool r_semantic_trait_from_ast(RFrontendContext *context, RAstRef ref, uint32_t *result) {
    RAstNodeView view;
    RAstTokenView name = {0};
    RTypeVector arguments = {0};
    char module_path[1024];
    size_t module_length = 0U;
    uint32_t identifier_count = 0U;
    bool success = false;
    *result = 0U;
    if (!r_ast_ref_view(context, ref, &view))
        return false;
    for (uint32_t index = 0U; index < view.child_count; ++index) {
        RAstRef child;
        RAstTokenView token;
        RAstNodeView child_view;
        if (!r_ast_ref_child(context, ref, index, &child) ||
            !r_ast_ref_view(context, child, &child_view))
            goto cleanup;
        if (r_ast_ref_token(context, child, &token) && token.kind == R_TOKEN_IDENTIFIER) {
            if (identifier_count != 0U) {
                const char *bytes;
                size_t length;
                if (!r_semantic_intern_text(context, name.intern_id, &bytes, &length) ||
                    module_length + length + 1U >= sizeof(module_path)) {
                    success =
                        r_semantic_add_diagnostic(context,
                                                  "R-DIAG-TRAIT-001",
                                                  "R-TYPE-0043",
                                                  "trait module path exceeds the compiler limit",
                                                  view.span);
                    goto cleanup;
                }
                if (module_length != 0U)
                    module_path[module_length++] = '.';
                (void)memcpy(module_path + module_length, bytes, length);
                module_length += length;
            }
            name = token;
            ++identifier_count;
        } else if (child_view.kind == R_SYNTAX_TYPE) {
            RTypeId argument;
            if (!r_semantic_type_from_ast(context, child, &argument)) {
                success = context->resource_status == R_FRONTEND_OK &&
                          r_semantic_add_diagnostic(context,
                                                    "R-DIAG-TRAIT-001",
                                                    "R-TYPE-0043",
                                                    "unresolved trait type argument",
                                                    child_view.span);
                goto cleanup;
            }
            if (!r_type_vector_push(context, &arguments, argument))
                goto cleanup;
        }
    }
    bool ambiguous = false;
    uint32_t trait = 0U;
    if (module_length == 4U && memcmp(module_path, "core", 4U) == 0) {
        trait = r_semantic_resolve_core_trait_name(context, name.intern_id);
    } else if (module_length != 0U) {
        uint32_t module;
        if (!r_intern_bytes(context, (const uint8_t *)module_path, module_length, &module))
            goto cleanup;
        trait =
            r_semantic_resolve_trait_name_in_module(context, ref.source, module, name.intern_id);
    } else {
        trait = r_semantic_resolve_trait_name(context, ref.source, name.intern_id, &ambiguous);
    }
    if (trait == 0U || ambiguous) {
        success = r_semantic_add_diagnostic(
            context,
            "R-DIAG-TRAIT-001",
            "R-TYPE-0043",
            ambiguous ? "ambiguous trait name"
            : module_length != 0U
                ? "unresolved trait name"
                : "unresolved trait name; an imported module's trait is written module::Trait or "
                  "imported by name",
            view.span);
    } else {
        success = r_semantic_apply_trait(context, trait, &arguments, view.span, result);
    }
cleanup:
    r_context_free(context, arguments.items);
    return success;
}

static bool r_semantic_impl_trait(RFrontendContext *context,
                                  uint32_t impl_id,
                                  RTypeId target,
                                  uint32_t *result) {
    const RSemanticImpl impl = context->semantic_impls[impl_id - 1U];
    *result = impl.trait;
    if (impl.generic_schema == 0U)
        return true;
    const RSemanticAggregate *owner = r_semantic_aggregate_for_type(context, target);
    if (owner == NULL || owner->generic_origin == 0U)
        return true;
    return r_semantic_effective_trait(
        context, impl.trait, impl.generic_schema, owner->first_generic_argument, result);
}

static bool r_semantic_validate_trait_applications(RFrontendContext *context) {
    for (uint32_t id = 1U; id <= context->semantic_trait_count; ++id) {
        const RSemanticTrait application = context->semantic_traits[id - 1U];
        if (application.generic_origin == 0U)
            continue;
        const uint32_t count =
            context->generic_schemas[application.type_parameter_schema - 1U].parameter_count;
        RTypeVector arguments = {0};
        bool valid = true;
        for (uint32_t index = 0U; valid && index < count; ++index)
            valid = r_type_vector_push(
                context,
                &arguments,
                context->generic_arguments[application.first_generic_argument + index]);
        valid =
            valid && r_generic_validate_arguments(
                         context, application.type_parameter_schema, &arguments, application.span);
        r_context_free(context, arguments.items);
        if (!valid)
            return false;
    }
    return true;
}

/* Follow declared supertrait edges with this application's arguments at each step. */
static bool r_semantic_find_provided_trait(RFrontendContext *context,
                                           uint32_t provided,
                                           uint32_t required,
                                           RTypeId self_type,
                                           bool match_declaration,
                                           uint32_t depth,
                                           uint32_t *result) {
    *result = 0U;
    const uint32_t origin = r_semantic_trait_origin(context, provided);
    if (origin == 0U || required == 0U || required > context->semantic_trait_count)
        return true;
    if (provided == required ||
        (match_declaration && origin == r_semantic_trait_origin(context, required)) ||
        r_semantic_callable_satisfies(
            r_semantic_type(context, context->semantic_traits[provided - 1U].callable_signature),
            r_semantic_type(context, context->semantic_traits[required - 1U].callable_signature))) {
        *result = provided;
        return true;
    }
    const RSemanticTrait declaration = context->semantic_traits[origin - 1U];
    if (declaration.poisoned)
        return true;
    if (depth >= context->options.limits.max_nesting) {
        r_generic_limit_diagnostic(context, declaration.name_intern_id, declaration.span);
        context->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
        return false;
    }
    if (declaration.supertrait_count == 0U)
        return true;
    RTypeVector arguments = {0};
    uint32_t first_argument;
    const bool prepared =
        r_semantic_trait_arguments(context, provided, self_type, 0U, &arguments) &&
        r_generic_commit_arguments(context, &arguments, &first_argument);
    r_context_free(context, arguments.items);
    if (!prepared)
        return false;
    for (uint32_t index = 0U; index < declaration.supertrait_count; ++index) {
        const uint32_t bound =
            context->generic_trait_constraints[declaration.first_supertrait + index];
        if (r_semantic_trait_origin(context, bound) == origin)
            continue;
        uint32_t parent, found;
        if (!r_semantic_effective_trait(
                context, bound, declaration.generic_schema, first_argument, &parent) ||
            !r_semantic_find_provided_trait(
                context, parent, required, self_type, match_declaration, depth + 1U, &found))
            return false;
        if (found == 0U)
            continue;
        if (!match_declaration) {
            *result = found;
            return true;
        }
        if (*result != 0U && *result != found) {
            *result = 0U;
            return r_semantic_add_diagnostic(
                context,
                "R-DIAG-TRAIT-001",
                "R-TYPE-0043",
                "different applications of a supertrait provide this member",
                declaration.span);
        }
        *result = found;
    }
    return true;
}

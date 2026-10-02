/* R-OWN-0020 (L26): the clone contract. core::clone(const T*) produces an independent value of
 * T: a Copy value is copied; standard owners copy their contents element by element; shared
 * owners and senders return another handle; a nominal type proves the contract through its
 * associated `T::clone` hook. A type that reaches itself through a structural clone would
 * recurse with the data and has no static stack bound (R-FUNC-0004), so it is not cloneable
 * without a hook. */

static bool r_clone_standard_handle(const RFrontendContext *context, const RSemanticType *type) {
    return r_semantic_standard_type_name_equal(context, type, "std.string::string") ||
           r_semantic_standard_type_name_equal(context, type, "std.fs::path");
}

static bool r_clone_capability_visit(const RFrontendContext *context,
                                     RTypeId id,
                                     uint32_t depth,
                                     const RSemanticTypeVisit *parent) {
    id = r_semantic_value_type(context, id);
    for (const RSemanticTypeVisit *previous = parent; previous != NULL; previous = previous->parent)
        if (previous->type == id)
            return false;
    RSemanticTypeVisit visit = {id, parent, depth};
    const RSemanticType *type = r_semantic_type(context, id);
    if (type == NULL || depth > context->options.limits.max_nesting)
        return false;
    if (type->kind == R_SEMANTIC_TYPE_PARAMETER)
        return (r_generic_parameter_constraints(context, id) &
                (R_GENERIC_CLONE | R_GENERIC_COPY)) != 0U;
    if (type->kind == R_SEMANTIC_TYPE_CONST)
        return r_clone_capability_visit(context, type->base, depth + 1U, &visit);
    if (type->kind == R_SEMANTIC_TYPE_DYN || type->kind == R_SEMANTIC_TYPE_ATOMIC ||
        type->kind == R_SEMANTIC_TYPE_VOID || type->kind == R_SEMANTIC_TYPE_NEVER ||
        type->kind == R_SEMANTIC_TYPE_TASK)
        return false;
    if (r_semantic_type_is_copy(context, id))
        return true;
    switch (type->kind) {
    case R_SEMANTIC_TYPE_ARC:
    case R_SEMANTIC_TYPE_RC:
    case R_SEMANTIC_TYPE_WEAK:
        return true;
    case R_SEMANTIC_TYPE_ARRAY:
    case R_SEMANTIC_TYPE_LIST:
    case R_SEMANTIC_TYPE_OPTION:
    case R_SEMANTIC_TYPE_FIXED_ARRAY:
    case R_SEMANTIC_TYPE_OWN:
        return r_clone_capability_visit(context, type->base, depth + 1U, &visit);
    case R_SEMANTIC_TYPE_DICT:
        return r_clone_capability_visit(context, type->base, depth + 1U, &visit) &&
               r_clone_capability_visit(context, type->second, depth + 1U, &visit);
    case R_SEMANTIC_TYPE_STANDARD:
        return r_clone_standard_handle(context, type);
    case R_SEMANTIC_TYPE_STRUCT:
    case R_SEMANTIC_TYPE_ENUM: {
        const RSemanticAggregate *aggregate = r_semantic_aggregate_for_type(context, id);
        if (aggregate == NULL || !aggregate->complete || aggregate->poisoned)
            return false;
        if (aggregate->clone_function != 0U)
            return true;
        if (!aggregate->is_tuple && !aggregate->clone_derived)
            return false;
        for (uint32_t i = 0U; i < aggregate->field_count; ++i)
            if (!r_clone_capability_visit(context,
                                          context->semantic_fields[aggregate->first_field + i].type,
                                          depth + 1U,
                                          &visit))
                return false;
        for (uint32_t i = 0U; i < aggregate->variant_count; ++i) {
            const RTypeId payload =
                context->semantic_variants[aggregate->first_variant + i].payload_type;
            if (payload != 0U && !r_clone_capability_visit(context, payload, depth + 1U, &visit))
                return false;
        }
        return true;
    }
    default:
        return false;
    }
}

/* A cloneable type is Copy or unborrowed: a Move copy owns what it copies and carries no view
 * whose provenance the copy would have to share. A generic parameter proves `clone` by its
 * constraint; its clone additionally needs `unborrowed` unless it is Copy. */
static bool r_clone_capability(const RFrontendContext *context, RTypeId id, uint32_t depth) {
    const RSemanticType *type = r_semantic_type(context, r_semantic_value_type(context, id));
    if (!r_clone_capability_visit(context, id, depth, NULL))
        return false;
    return (type != NULL && type->kind == R_SEMANTIC_TYPE_PARAMETER) ||
           r_semantic_type_is_copy(context, id) ||
           r_generic_has_property(context, id, R_GENERIC_UNBORROWED, depth);
}

/* Every user clone hook a structural clone of `id` calls, so the static call graph and the
 * resource contracts see the functions that core::clone enters (R-FUNC-0004, R-FUNC-0019). */
static bool r_clone_visit_hooks(const RFrontendContext *context,
                                RTypeId id,
                                uint32_t depth,
                                bool (*visit_hook)(void *user, RSymbolId hook),
                                void *user) {
    id = r_semantic_value_type(context, id);
    const RSemanticType *type = r_semantic_type(context, id);
    if (type == NULL || depth > context->options.limits.max_nesting)
        return true;
    switch (type->kind) {
    case R_SEMANTIC_TYPE_CONST:
    case R_SEMANTIC_TYPE_ARRAY:
    case R_SEMANTIC_TYPE_LIST:
    case R_SEMANTIC_TYPE_OPTION:
    case R_SEMANTIC_TYPE_FIXED_ARRAY:
    case R_SEMANTIC_TYPE_OWN:
        return r_clone_visit_hooks(context, type->base, depth + 1U, visit_hook, user);
    case R_SEMANTIC_TYPE_DICT:
        return r_clone_visit_hooks(context, type->base, depth + 1U, visit_hook, user) &&
               r_clone_visit_hooks(context, type->second, depth + 1U, visit_hook, user);
    case R_SEMANTIC_TYPE_STRUCT:
    case R_SEMANTIC_TYPE_ENUM: {
        const RSemanticAggregate *aggregate = r_semantic_aggregate_for_type(context, id);
        if (aggregate == NULL)
            return true;
        if (aggregate->clone_function != 0U)
            return visit_hook(user, aggregate->clone_function);
        if (r_semantic_type_is_copy(context, id) ||
            (!aggregate->is_tuple && !aggregate->clone_derived))
            return true;
        for (uint32_t i = 0U; i < aggregate->field_count; ++i)
            if (!r_clone_visit_hooks(context,
                                     context->semantic_fields[aggregate->first_field + i].type,
                                     depth + 1U,
                                     visit_hook,
                                     user))
                return false;
        for (uint32_t i = 0U; i < aggregate->variant_count; ++i) {
            const RTypeId payload =
                context->semantic_variants[aggregate->first_variant + i].payload_type;
            if (payload != 0U &&
                !r_clone_visit_hooks(context, payload, depth + 1U, visit_hook, user))
                return false;
        }
        return true;
    }
    default:
        return true;
    }
}

/* Whether a clone of `id` can report std.alloc::alloc_error (R-OWN-0020): a Copy value, a shared
 * owner handle and a hook without checked errors cannot; a generic parameter that is not Copy
 * might. */
static bool r_clone_reports_failure(const RFrontendContext *context, RTypeId id, uint32_t depth) {
    id = r_semantic_value_type(context, id);
    const RSemanticType *type = r_semantic_type(context, id);
    if (type == NULL || depth > context->options.limits.max_nesting)
        return true;
    if (type->kind == R_SEMANTIC_TYPE_CONST)
        return r_clone_reports_failure(context, type->base, depth + 1U);
    if (r_semantic_type_is_copy(context, id))
        return false;
    switch (type->kind) {
    case R_SEMANTIC_TYPE_ARC:
    case R_SEMANTIC_TYPE_RC:
    case R_SEMANTIC_TYPE_WEAK:
        return false;
    case R_SEMANTIC_TYPE_OPTION:
    case R_SEMANTIC_TYPE_FIXED_ARRAY:
        return r_clone_reports_failure(context, type->base, depth + 1U);
    case R_SEMANTIC_TYPE_STRUCT:
    case R_SEMANTIC_TYPE_ENUM: {
        const RSemanticAggregate *aggregate = r_semantic_aggregate_for_type(context, id);
        if (aggregate == NULL)
            return true;
        if (aggregate->clone_function != 0U)
            return context->semantic_symbols[aggregate->clone_function - 1U].throws_type != 0U;
        for (uint32_t i = 0U; i < aggregate->field_count; ++i)
            if (r_clone_reports_failure(
                    context, context->semantic_fields[aggregate->first_field + i].type, depth + 1U))
                return true;
        for (uint32_t i = 0U; i < aggregate->variant_count; ++i) {
            const RTypeId payload =
                context->semantic_variants[aggregate->first_variant + i].payload_type;
            if (payload != 0U && r_clone_reports_failure(context, payload, depth + 1U))
                return true;
        }
        return false;
    }
    default:
        return true;
    }
}

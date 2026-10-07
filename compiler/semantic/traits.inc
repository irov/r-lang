/* R-FUNC-0013, R-FUNC-0014, R-TYPE-0041 through R-TYPE-0043 and R-NAME-0010: methods with an
 * explicit receiver, trait declarations, implementations and statically dispatched trait calls.
 * A trait is a generic schema with the single parameter Self; its members are open generic
 * prototypes. An implementation binds those prototypes to definitions of one target type. */

static bool r_semantic_span_contains(RSourceSpan outer, RSourceSpan inner) {
    return (outer.source == inner.source) && (outer.source != R_SOURCE_ID_INVALID) &&
           (outer.start <= inner.start) && (inner.end <= outer.end);
}

static uint32_t r_semantic_trait_at(const RFrontendContext *context, RSourceSpan span) {
    size_t index;
    for (index = 0U; index < context->semantic_trait_count; ++index) {
        if (context->semantic_traits[index].generic_origin == 0U &&
            r_semantic_span_contains(context->semantic_traits[index].span, span)) {
            return (uint32_t)index + UINT32_C(1);
        }
    }
    return UINT32_C(0);
}

static uint32_t r_semantic_impl_at(const RFrontendContext *context, RSourceSpan span) {
    size_t index;
    for (index = 0U; index < context->semantic_impl_count; ++index) {
        if (r_semantic_span_contains(context->semantic_impls[index].span, span)) {
            return (uint32_t)index + UINT32_C(1);
        }
    }
    return UINT32_C(0);
}

static bool
r_semantic_self_type_at(const RFrontendContext *context, RSourceSpan span, RTypeId *type_id) {
    const uint32_t trait = r_semantic_trait_at(context, span);
    const uint32_t impl = r_semantic_impl_at(context, span);
    if (trait != UINT32_C(0)) {
        *type_id = context->semantic_traits[trait - 1U].self_type;
        return *type_id != R_TYPE_ID_INVALID;
    }
    if (impl != UINT32_C(0)) {
        *type_id = context->semantic_impls[impl - 1U].target_type;
        return *type_id != R_TYPE_ID_INVALID;
    }
    return false;
}

/* The method namespace keys generic owners by the schema origin, so that a method declared on
 * Vector(T) is found through any closed application of Vector. */
static void r_semantic_canonical_owner(const RFrontendContext *context,
                                       RTypeId type_id,
                                       RTypeId *owner_type,
                                       uint32_t *owner_aggregate) {
    const RSemanticAggregate *aggregate = r_semantic_aggregate_for_type(context, type_id);
    *owner_aggregate = UINT32_C(0);
    *owner_type = r_semantic_value_type(context, type_id);
    if (aggregate == NULL) {
        return;
    }
    {
        /* M18-9: the field schema of a standard struct, such as std.net::tcp_connection, is
           formed on first use; the owner stays the standard type before and after. */
        const RSemanticType *owner = r_semantic_type(context, aggregate->type);
        if ((owner != NULL) && (owner->kind == R_SEMANTIC_TYPE_STANDARD)) {
            return;
        }
    }
    *owner_aggregate = aggregate->generic_origin != UINT32_C(0)
                           ? aggregate->generic_origin
                           : (uint32_t)(aggregate - context->semantic_aggregates) + UINT32_C(1);
    *owner_type = context->semantic_aggregates[*owner_aggregate - 1U].type;
}

/* L27 (R-TYPE-0042): whether `pattern_id`, the target of a generic implementation over a
 * standard type constructor, has the shape of `actual_id` wherever it does not name a parameter
 * of `schema_id`; r_generic_infer_type binds the parameters themselves. */
static bool r_semantic_structural_shape(const RFrontendContext *context,
                                        RTypeId pattern_id,
                                        RTypeId actual_id,
                                        uint32_t schema_id,
                                        uint32_t depth) {
    const RSemanticType *pattern = r_semantic_type(context, pattern_id);
    const RSemanticType *actual = r_semantic_type(context, actual_id);
    const RGenericSchema *schema = &context->generic_schemas[schema_id - 1U];
    if (pattern_id == actual_id)
        return true;
    if (pattern == NULL || actual == NULL || depth > context->options.limits.max_nesting)
        return false;
    if (pattern->kind == R_SEMANTIC_TYPE_PARAMETER)
        return pattern->base > schema->first_parameter &&
               pattern->base <= schema->first_parameter + schema->parameter_count;
    if (pattern->kind != actual->kind || pattern->flags != actual->flags)
        return false;
    if (pattern->kind == R_SEMANTIC_TYPE_STRUCT || pattern->kind == R_SEMANTIC_TYPE_ENUM) {
        const RSemanticAggregate *left = &context->semantic_aggregates[pattern->base - 1U];
        const RSemanticAggregate *right = &context->semantic_aggregates[actual->base - 1U];
        if (left->generic_origin == 0U || left->generic_origin != right->generic_origin)
            return false;
        const uint32_t count = context->generic_schemas[left->generic_schema - 1U].parameter_count;
        for (uint32_t index = 0U; index < count; ++index)
            if (!r_semantic_structural_shape(
                    context,
                    context->generic_arguments[left->first_generic_argument + index],
                    context->generic_arguments[right->first_generic_argument + index],
                    schema_id,
                    depth + 1U))
                return false;
        return true;
    }
    if (pattern->kind < R_SEMANTIC_TYPE_FIXED_ARRAY)
        return false;
    if (pattern->kind == R_SEMANTIC_TYPE_FIXED_ARRAY && pattern->second != 0U) {
        /* `T[N]`: the constant parameter takes the length of the instance. */
        const RSemanticType *bound = r_semantic_type(context, pattern->second);
        if (bound == NULL || bound->kind != R_SEMANTIC_TYPE_PARAMETER)
            return false;
        return r_semantic_structural_shape(
            context, pattern->base, actual->base, schema_id, depth + 1U);
    }
    if (pattern->length != actual->length || (pattern->base == 0U) != (actual->base == 0U) ||
        (pattern->second == 0U) != (actual->second == 0U))
        return false;
    return (pattern->base == 0U ||
            r_semantic_structural_shape(
                context, pattern->base, actual->base, schema_id, depth + 1U)) &&
           (pattern->second == 0U ||
            r_semantic_structural_shape(
                context, pattern->second, actual->second, schema_id, depth + 1U));
}

/* L27 (R-TYPE-0042): an implementation whose target is a standard type constructor over the
 * implementation's parameters, such as `o<T>`, `array<T>`, `T[]` or `T[N]`, applies to every
 * instance of that shape. `arguments` receives the implementation's arguments for `type_id`;
 * with `check_constraints`, arguments that do not prove the implementation's constraints do not
 * apply. */
/* R-TYPE-0042 (L27): whether the implementation arguments `arguments` of the generic
   implementation `impl_id` prove its constraints. An argument left unbound is not checked. */
static bool r_semantic_impl_arguments_prove(RFrontendContext *context,
                                            uint32_t impl_id,
                                            const RTypeVector *arguments,
                                            bool *proven) {
    const RSemanticImpl impl = context->semantic_impls[impl_id - 1U];
    const RGenericSchema schema = context->generic_schemas[impl.generic_schema - 1U];
    *proven = false;
    for (uint32_t index = 0U; index < schema.parameter_count && index < arguments->count;
         ++index) {
        const RGenericParameter parameter =
            context->generic_parameters[schema.first_parameter + index];
        const RTypeId argument = arguments->items[index];
        if ((argument == R_TYPE_ID_INVALID) || (parameter.constant_type != 0U) ||
            parameter.is_pack) {
            continue;
        }
        for (uint32_t property = R_GENERIC_COPY; property <= R_GENERIC_CLONE;
             property = r_generic_next_capability(property))
            if ((parameter.constraints & property) != 0U &&
                !r_generic_has_property(context, argument, property, 0U))
                return true;
        for (uint32_t constraint = 0U; constraint < parameter.trait_constraint_count;
             ++constraint) {
            const uint32_t trait =
                context->generic_trait_constraints[parameter.first_trait_constraint + constraint];
            const RSemanticTrait *record = &context->semantic_traits[trait - 1U];
            /* A callable or applied trait depends on the other arguments; instantiation checks
               it (R-TYPE-0044). */
            if ((record->callable_signature != R_TYPE_ID_INVALID) ||
                (record->type_parameter_schema != 0U) || (record->generic_origin != 0U)) {
                continue;
            }
            if (!r_semantic_type_implements_trait(context, argument, trait))
                return context->resource_status == R_FRONTEND_OK;
        }
    }
    *proven = true;
    return true;
}

/* R-TYPE-0042 (L27): whether the generic implementation `impl_id` of a nominal target applies
   to the instance `type_id`: its arguments, inferred from the instance, prove the constraints
   the implementation adds. */
static bool r_semantic_nominal_impl_applies(RFrontendContext *context,
                                            uint32_t impl_id,
                                            RTypeId type_id,
                                            bool *applies) {
    const RSemanticImpl impl = context->semantic_impls[impl_id - 1U];
    RTypeVector arguments = {0};
    bool success = true;
    *applies = true;
    if ((impl.generic_schema == 0U) || impl.poisoned) {
        return true;
    }
    for (uint32_t index = 0U;
         success && index < context->generic_schemas[impl.generic_schema - 1U].parameter_count;
         ++index)
        success = r_type_vector_push(context, &arguments, R_TYPE_ID_INVALID);
    if (success && r_generic_infer_type(context,
                                        impl.target_type,
                                        r_semantic_value_type(context, type_id),
                                        impl.generic_schema,
                                        &arguments,
                                        0U)) {
        success = r_semantic_impl_arguments_prove(context, impl_id, &arguments, applies);
    }
    r_context_free(context, arguments.items);
    return success && (context->resource_status == R_FRONTEND_OK);
}

static bool r_semantic_structural_impl(RFrontendContext *context,
                                       uint32_t impl_id,
                                       RTypeId type_id,
                                       bool check_constraints,
                                       RTypeVector *arguments,
                                       bool *applies) {
    const RSemanticImpl impl = context->semantic_impls[impl_id - 1U];
    const RTypeId actual = r_semantic_value_type(context, type_id);
    const RSemanticType *pattern = r_semantic_type(context, impl.target_type);
    const RSemanticType *value = r_semantic_type(context, actual);
    *applies = false;
    arguments->count = 0U;
    if (impl.poisoned || impl.generic_schema == 0U || impl.target_aggregate != 0U ||
        pattern == NULL || value == NULL || pattern->kind != value->kind ||
        pattern->kind < R_SEMANTIC_TYPE_FIXED_ARRAY ||
        !r_generic_type_is_dependent(context, impl.target_type, 0U))
        return true;
    const RGenericSchema schema = context->generic_schemas[impl.generic_schema - 1U];
    for (uint32_t index = 0U; index < schema.parameter_count; ++index)
        if (!r_type_vector_push(context, arguments, R_TYPE_ID_INVALID))
            return false;
    if (!r_semantic_structural_shape(context, impl.target_type, actual, impl.generic_schema, 0U) ||
        !r_generic_infer_type(
            context, impl.target_type, actual, impl.generic_schema, arguments, 0U))
        return context->resource_status == R_FRONTEND_OK;
    for (uint32_t index = 0U; index < schema.parameter_count; ++index)
        if (arguments->items[index] == R_TYPE_ID_INVALID)
            return true;
    if (!check_constraints) {
        *applies = true;
        return true;
    }
    return r_semantic_impl_arguments_prove(context, impl_id, arguments, applies);
}

/* L27 (R-TYPE-0042): the method `name_intern_id` of an implementation over a standard type
 * constructor whose shape `owner_type` has; NULL when none or more than one applies. */
static bool r_semantic_find_structural_method(RFrontendContext *context,
                                              RTypeId owner_type,
                                              uint32_t name_intern_id,
                                              const RSemanticMethod **match,
                                              bool *ambiguous) {
    RTypeVector arguments = {0};
    bool success = true;
    *match = NULL;
    *ambiguous = false;
    for (size_t index = 0U; success && index < context->semantic_method_count; ++index) {
        const RSemanticMethod method = context->semantic_methods[index];
        bool applies = false;
        if (method.impl == 0U || method.owner_aggregate != 0U ||
            method.name_intern_id != name_intern_id)
            continue;
        success = r_semantic_structural_impl(
            context, method.impl, owner_type, false, &arguments, &applies);
        if (!success || !applies)
            continue;
        if (*match != NULL && (*match)->function != method.function) {
            *ambiguous = true;
            break;
        }
        *match = &context->semantic_methods[index];
    }
    r_context_free(context, arguments.items);
    return success;
}

static const RSemanticMethod *r_semantic_find_method(const RFrontendContext *context,
                                                     RTypeId owner_type,
                                                     uint32_t name_intern_id,
                                                     bool *ambiguous) {
    RTypeId canonical;
    uint32_t aggregate;
    const RSemanticMethod *match = NULL;
    size_t index;

    *ambiguous = false;
    r_semantic_canonical_owner(context, owner_type, &canonical, &aggregate);
    for (index = 0U; index < context->semantic_method_count; ++index) {
        const RSemanticMethod *method = &context->semantic_methods[index];
        if ((method->name_intern_id != name_intern_id) || (method->owner_type != canonical) ||
            (method->owner_aggregate != aggregate)) {
            continue;
        }
        if ((match != NULL) &&
            (match->function != method->function || match->trait != method->trait)) {
            const RSemanticSymbol *a = &context->semantic_symbols[match->function - 1U];
            const RSemanticSymbol *b = &context->semantic_symbols[method->function - 1U];
            if ((match->is_default || method->is_default) && match->trait == method->trait &&
                (a->is_overloaded || b->is_overloaded))
                continue;
            if (a->is_overloaded && b->is_overloaded && a->name_intern_id == b->name_intern_id &&
                a->module_source == b->module_source && match->trait == method->trait) {
                continue;
            }
            *ambiguous = true;
            return match;
        }
        match = method;
    }
    return match;
}

static bool r_semantic_trait_satisfies(const RFrontendContext *, uint32_t, uint32_t);

static const RSemanticMethod *r_semantic_find_trait_method(const RFrontendContext *context,
                                                           uint32_t trait,
                                                           uint32_t name_intern_id) {
    size_t index;
    for (index = 0U; index < context->semantic_method_count; ++index) {
        const RSemanticMethod *method = &context->semantic_methods[index];
        if ((method->trait == r_semantic_trait_origin(context, trait)) &&
            (method->impl == UINT32_C(0)) && (method->name_intern_id == name_intern_id)) {
            return method;
        }
    }
    const RSemanticMethod *found = NULL;
    for (index = 0U; index < context->semantic_method_count; ++index) {
        const RSemanticMethod *method = &context->semantic_methods[index];
        if (method->trait != trait && method->impl == 0U &&
            method->name_intern_id == name_intern_id &&
            r_semantic_trait_satisfies(context, trait, method->trait)) {
            if (found != NULL && found->function != method->function)
                return NULL;
            found = method;
        }
    }
    return found;
}

static const RSemanticMethod *r_semantic_find_impl_method(RFrontendContext *context,
                                                          uint32_t trait,
                                                          RTypeId target_type,
                                                          uint32_t name_intern_id) {
    RTypeId canonical;
    uint32_t aggregate;
    size_t index;

    r_semantic_canonical_owner(context, target_type, &canonical, &aggregate);
    for (index = 0U; index < context->semantic_method_count; ++index) {
        const RSemanticMethod *method = &context->semantic_methods[index];
        if ((method->impl != UINT32_C(0)) && (method->name_intern_id == name_intern_id) &&
            (method->owner_type == canonical) && (method->owner_aggregate == aggregate)) {
            uint32_t effective;
            if (!r_semantic_impl_trait(context, method->impl, target_type, &effective))
                return NULL;
            if (effective == trait)
                return &context->semantic_methods[index];
        }
    }
    if (aggregate == UINT32_C(0)) {
        /* L27 (R-TYPE-0042): an implementation over a standard type constructor. */
        RTypeVector arguments = {0};
        const RSemanticMethod *found = NULL;
        for (index = 0U; found == NULL && index < context->semantic_method_count; ++index) {
            const RSemanticMethod method = context->semantic_methods[index];
            bool applies = false;
            if (method.impl == UINT32_C(0) || method.owner_aggregate != UINT32_C(0) ||
                method.name_intern_id != name_intern_id ||
                context->semantic_impls[method.impl - 1U].trait != trait)
                continue;
            if (!r_semantic_structural_impl(
                    context, method.impl, target_type, false, &arguments, &applies))
                break;
            if (applies)
                found = &context->semantic_methods[index];
        }
        r_context_free(context, arguments.items);
        return found;
    }
    return NULL;
}

static bool
r_semantic_trait_satisfies(const RFrontendContext *context, uint32_t provided, uint32_t required) {
    if (provided == required || (r_semantic_trait_origin(context, provided) == required &&
                                 context->semantic_traits[required - 1U].generic_origin == 0U))
        return true;
    if (provided == 0U || required == 0U || provided > context->semantic_trait_count ||
        required > context->semantic_trait_count)
        return false;
    const RSemanticTrait *trait = &context->semantic_traits[provided - 1U];
    if (r_semantic_callable_satisfies(
            r_semantic_type(context, trait->callable_signature),
            r_semantic_type(context, context->semantic_traits[required - 1U].callable_signature)))
        return true;
    const RSemanticType *self = r_semantic_type(context, trait->self_type);
    if (self == NULL || self->kind != R_SEMANTIC_TYPE_PARAMETER || self->base == 0U)
        return false;
    const RGenericParameter *parameter = &context->generic_parameters[self->base - 1U];
    for (uint32_t index = 0U; index < parameter->trait_constraint_count; ++index) {
        const uint32_t bound =
            context->generic_trait_constraints[parameter->first_trait_constraint + index];
        if (bound == required || (r_semantic_trait_origin(context, bound) == required &&
                                  context->semantic_traits[required - 1U].generic_origin == 0U))
            return true;
    }
    return false;
}

/* R-TYPE-0046 (L32): the standard formatting of R-EXPR-0028 makes the built-in scalars and text,
 * std.string::string, the std.net addresses, and o, fixed arrays, slices, array and tuples of
 * core::Format types satisfy core::Format without an implementation. */
static bool r_semantic_type_has_standard_format(RFrontendContext *context, RTypeId type_id) {
    const RTypeId value = r_semantic_value_type(context, type_id);
    const RSemanticType *type = r_semantic_type(context, value);
    const RSemanticAggregate *aggregate;

    if (type == NULL) {
        return false;
    }
    if ((type->kind >= R_SEMANTIC_TYPE_I8) && (type->kind <= R_SEMANTIC_TYPE_USIZE)) {
        return true;
    }
    switch (type->kind) {
    case R_SEMANTIC_TYPE_BOOL:
    case R_SEMANTIC_TYPE_CHAR:
    case R_SEMANTIC_TYPE_STR:
    case R_SEMANTIC_TYPE_CONSTEXPR_STR:
    case R_SEMANTIC_TYPE_F32:
    case R_SEMANTIC_TYPE_F64:
    case R_SEMANTIC_TYPE_C_ABI_NUMERIC:
        return true;
    case R_SEMANTIC_TYPE_OPTION:
    case R_SEMANTIC_TYPE_FIXED_ARRAY:
    case R_SEMANTIC_TYPE_SLICE:
    case R_SEMANTIC_TYPE_ARRAY:
        return r_semantic_type_implements_trait(context, type->base, context->core_format_trait);
    case R_SEMANTIC_TYPE_STANDARD:
        return r_semantic_standard_type_name_equal(context, type, "std.string::string") ||
               r_semantic_standard_type_name_equal(context, type, "std.net::ip_address") ||
               r_semantic_standard_type_name_equal(context, type, "std.net::socket_address");
    default:
        break;
    }
    aggregate = r_semantic_aggregate_for_type(context, value);
    if ((aggregate == NULL) || !aggregate->is_tuple || (aggregate->generic_origin == 0U)) {
        return false;
    }
    for (uint32_t index = 0U; index < aggregate->field_count; ++index) {
        if (!r_semantic_type_implements_trait(context,
                                              context->semantic_fields[aggregate->first_field + index].type,
                                              context->core_format_trait)) {
            return false;
        }
    }
    return true;
}

/* R-TYPE-0043: a type proves a trait constraint through its own constraints when it is a
 * parameter, and otherwise through exactly one implementation for its canonical owner. */
static bool
r_semantic_type_implements_trait(RFrontendContext *context, RTypeId type_id, uint32_t trait) {
    RTypeId canonical;
    uint32_t aggregate;
    size_t index;
    const RSemanticType *type = r_semantic_type(context, type_id);
    const RSemanticAggregate *closure = r_semantic_aggregate_for_type(context, type_id);
    if ((trait != 0U) && (trait == context->core_format_trait) &&
        r_semantic_type_has_standard_format(context, type_id)) {
        return true;
    }
    /* R-TYPE-0051: an interface implements what its contract proves. */
    if (r_dyn_contract(context, type_id) != NULL)
        return r_semantic_type_implements_trait(
            context, r_dyn_contract(context, type_id)->type, trait);
    if ((type != NULL) && (type->kind == R_SEMANTIC_TYPE_FUNCTION) && (trait != 0U) &&
        (context->semantic_traits[trait - 1U].callable_signature != R_TYPE_ID_INVALID)) {
        /* R-TYPE-0054 (L28): a function value implements a callable constraint of its own
           signature in every mode, and one that promises no more resource guarantees. */
        const RSemanticType *required = r_semantic_type(
            context, context->semantic_traits[trait - 1U].callable_signature);
        return (required != NULL) && (required->base == type->base) &&
               (required->second == type->second) && (required->length == type->length) &&
               (((required->flags ^ type->flags) & R_SEMANTIC_TYPE_FLAG_CALL_ASYNC) == 0U) &&
               ((required->flags & R_SEMANTIC_TYPE_FLAG_RESOURCES & ~type->flags) == 0U);
    }
    if (closure != NULL && closure->is_closure && trait != 0U &&
        context->semantic_traits[trait - 1U].callable_signature != R_TYPE_ID_INVALID) {
        RTypeId signature = closure->closure_signature;
        const RTypeId required = context->semantic_traits[trait - 1U].callable_signature;
        if (!r_function_item_signature(context, type_id, required, &signature))
            return false;
        return r_semantic_callable_satisfies(r_semantic_type(context, signature),
                                             r_semantic_type(context, required));
    }

    if ((type != NULL) && (type->kind == R_SEMANTIC_TYPE_PARAMETER) && (type->base != 0U) &&
        ((size_t)type->base <= context->generic_parameter_count)) {
        const RGenericParameter parameter = context->generic_parameters[type->base - 1U];
        uint32_t constraint;
        for (constraint = 0U; constraint < parameter.trait_constraint_count; ++constraint) {
            uint32_t found;
            if (!r_semantic_find_provided_trait(
                    context,
                    context
                        ->generic_trait_constraints[parameter.first_trait_constraint + constraint],
                    trait,
                    type_id,
                    false,
                    0U,
                    &found))
                return false;
            if (found != 0U)
                return true;
        }
        return false;
    }
    r_semantic_canonical_owner(context, type_id, &canonical, &aggregate);
    for (index = 0U; index < context->semantic_impl_count; ++index) {
        const RSemanticImpl *impl = &context->semantic_impls[index];
        RTypeId impl_owner;
        uint32_t impl_aggregate;
        r_semantic_canonical_owner(context, impl->target_type, &impl_owner, &impl_aggregate);
        if ((aggregate == UINT32_C(0)) && (impl_aggregate == UINT32_C(0)) &&
            (impl_owner != canonical) && (impl->generic_schema != UINT32_C(0))) {
            /* L27 (R-TYPE-0042): an implementation over a standard type constructor applies
               when the instance has its shape and its arguments prove its constraints. */
            RTypeVector arguments = {0};
            bool applies = false;
            const bool matched = r_semantic_structural_impl(
                context, (uint32_t)index + 1U, type_id, true, &arguments, &applies);
            r_context_free(context, arguments.items);
            if (!matched)
                return false;
            impl = &context->semantic_impls[index];
            if (!applies)
                continue;
            impl_owner = canonical;
        }
        if ((impl_owner == canonical) && (impl_aggregate == aggregate)) {
            uint32_t effective;
            bool applies = true;
            if ((aggregate != UINT32_C(0)) &&
                !r_semantic_nominal_impl_applies(context, (uint32_t)index + 1U, type_id, &applies))
                return false;
            if (!applies)
                continue;
            if (!r_semantic_impl_trait(context, (uint32_t)index + 1U, type_id, &effective))
                return false;
            uint32_t found;
            if (!r_semantic_find_provided_trait(
                    context, effective, trait, type_id, false, 0U, &found))
                return false;
            if (found != 0U)
                return true;
        }
    }
    return false;
}

/* R-TYPE-0045: the position of one associated type within its trait, or UINT32_MAX. */
static uint32_t r_semantic_associated_index(const RFrontendContext *context,
                                            uint32_t trait,
                                            uint32_t name_intern_id) {
    const RSemanticTrait *record = &context->semantic_traits[trait - 1U];
    uint32_t index;
    for (index = 0U; index < record->associated_count; ++index) {
        if (context->semantic_associated_types[record->first_associated + index].name_intern_id ==
            name_intern_id) {
            return index;
        }
    }
    return UINT32_MAX;
}

/* R-TYPE-0045: `P::Name` on a type parameter is a synthetic parameter of its own, interned once
 * per (parameter, trait, associated type) so that equal projections have one type. */
static bool r_semantic_append_trait_constraint(RFrontendContext *, uint32_t, uint32_t);

/* R-REFL-0006 (L44): the synthetic parameter for the field that `core::field` borrows from the
   dependent struct `base_type`: at the constant `index_value`, or at the constant of the
   translation-time loop whose parameter is `index_parameter`. It carries the field traits of a
   parameter base, so that their methods apply to the field in the generic definition. */
static bool r_semantic_field_parameter(RFrontendContext *context,
                                       RTypeId base_type,
                                       RTypeId index_parameter,
                                       uint64_t index_value,
                                       RTypeId *type_id) {
    RGenericParameter parameter;
    for (size_t existing = 0U; existing < context->generic_parameter_count; ++existing) {
        const RGenericParameter *candidate = &context->generic_parameters[existing];
        if ((candidate->field_base == base_type) && (candidate->field_index == index_parameter) &&
            ((index_parameter != R_TYPE_ID_INVALID) ||
             (candidate->field_index_value == index_value))) {
            *type_id = candidate->type;
            return true;
        }
    }
    (void)memset(&parameter, 0, sizeof(parameter));
    const RSemanticType *base = r_semantic_type(context, base_type);
    if ((base != NULL) && (base->kind == R_SEMANTIC_TYPE_PARAMETER) && (base->base != 0U) &&
        ((size_t)base->base <= context->generic_parameter_count)) {
        const RGenericParameter source = context->generic_parameters[base->base - 1U];
        parameter.name_span = source.name_span;
        parameter.name_intern_id = source.name_intern_id;
        parameter.first_trait_constraint = source.first_field_trait;
        parameter.trait_constraint_count = source.field_trait_count;
    }
    parameter.field_base = base_type;
    parameter.field_index = index_parameter;
    parameter.field_index_value = index_value;
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

static bool r_semantic_trait_projection(RFrontendContext *context,
                                        RTypeId base_type,
                                        uint32_t trait,
                                        uint32_t index,
                                        RTypeId *type_id) {
    const RSemanticTrait record = context->semantic_traits[trait - 1U];
    RGenericParameter parameter;
    size_t existing;

    for (existing = 0U; existing < context->generic_parameter_count; ++existing) {
        const RGenericParameter *candidate = &context->generic_parameters[existing];
        if ((candidate->projection_base == base_type) && (candidate->projection_trait == trait) &&
            (candidate->projection_index == index)) {
            *type_id =
                candidate->projection_bound != 0U ? candidate->projection_bound : candidate->type;
            return true;
        }
    }
    (void)memset(&parameter, 0, sizeof(parameter));
    parameter.name_span =
        context->semantic_associated_types[record.first_associated + index].name_span;
    parameter.name_intern_id =
        context->semantic_associated_types[record.first_associated + index].name_intern_id;
    const RSemanticType *associated = r_semantic_type(
        context,
        context->semantic_associated_types[record.first_associated + index].parameter_type);
    if (associated != NULL && associated->kind == R_SEMANTIC_TYPE_PARAMETER &&
        associated->base != 0U) {
        const RGenericParameter source = context->generic_parameters[associated->base - 1U];
        parameter.constraints = source.constraints;
        parameter.first_trait_constraint = source.first_trait_constraint;
        parameter.trait_constraint_count = source.trait_constraint_count;
    }
    parameter.projection_base = base_type;
    parameter.projection_trait = trait;
    parameter.projection_index = index;
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
    const uint32_t parameter_index = (uint32_t)context->generic_parameter_count;
    context->generic_parameters[context->generic_parameter_count++] = parameter;
    *type_id = parameter.type;
    if (record.generic_origin != 0U) {
        context->generic_parameters[parameter_index].trait_constraint_count = 0U;
        for (uint32_t bound = 0U; bound < parameter.trait_constraint_count; ++bound) {
            uint32_t effective;
            if (!r_semantic_effective_trait(
                    context,
                    context->generic_trait_constraints[parameter.first_trait_constraint + bound],
                    record.type_parameter_schema,
                    record.first_generic_argument,
                    &effective) ||
                (effective != 0U &&
                 !r_semantic_append_trait_constraint(context, parameter_index, effective)))
                return false;
        }
    }
    return true;
}

/* R-TYPE-0045: the associated type a closed type binds through its implementation, or the
 * projection when the base is still a parameter. False when no implementation binds it. */
static bool r_semantic_resolve_projection(RFrontendContext *context,
                                          RTypeId base_type,
                                          uint32_t trait,
                                          uint32_t index,
                                          RTypeId *type_id) {
    const RSemanticType *base = r_semantic_type(context, base_type);
    RTypeId canonical;
    uint32_t aggregate;
    size_t impl_index;

    if (base == NULL) {
        return false;
    }
    if (base->kind == R_SEMANTIC_TYPE_PARAMETER) {
        return r_semantic_trait_projection(context, base_type, trait, index, type_id);
    }
    r_semantic_canonical_owner(context, base_type, &canonical, &aggregate);
    for (impl_index = 0U; impl_index < context->semantic_impl_count; ++impl_index) {
        const RSemanticImpl impl = context->semantic_impls[impl_index];
        RTypeId impl_owner;
        uint32_t impl_aggregate;
        RTypeId bound;
        if (impl.poisoned) {
            continue;
        }
        r_semantic_canonical_owner(context, impl.target_type, &impl_owner, &impl_aggregate);
        if ((impl_owner != canonical) || (impl_aggregate != aggregate)) {
            continue;
        }
        uint32_t effective;
        if (!r_semantic_impl_trait(context, (uint32_t)impl_index + 1U, base_type, &effective))
            return false;
        if (effective != trait)
            continue;
        bound = context->semantic_associated_bindings[impl.first_binding + index];
        if (bound == R_TYPE_ID_INVALID) {
            return false;
        }
        if ((impl.generic_schema != UINT32_C(0)) && (aggregate != UINT32_C(0))) {
            /* A generic implementation binds in terms of its own parameters; the closed
               application of the target supplies their arguments. */
            const RSemanticAggregate *applied = r_semantic_aggregate_for_type(context, base_type);
            if ((applied == NULL) || (applied->generic_origin == UINT32_C(0))) {
                return false;
            }
            return r_generic_substitute_type(
                context, bound, impl.generic_schema, applied->first_generic_argument, type_id);
        }
        *type_id = bound;
        return true;
    }
    return false;
}

/* R-TYPE-0045 (L16.3): the schema of the parameters that the associated type `type` takes: that
 * of a binding of an implementation, of a projection or of an associated type of a trait; zero
 * for an associated type without parameters or any other type. */
static uint32_t r_semantic_associated_parameter_schema(const RFrontendContext *context,
                                                       RTypeId type_id) {
    const RSemanticType *type = r_semantic_type(context, type_id);

    if (type == NULL) {
        return 0U;
    }
    if (type->kind == R_SEMANTIC_TYPE_TYPE_FUNCTION) {
        return (uint32_t)type->length;
    }
    if ((type->kind != R_SEMANTIC_TYPE_PARAMETER) || (type->base == 0U) ||
        ((size_t)type->base > context->generic_parameter_count)) {
        return 0U;
    }
    const RGenericParameter parameter = context->generic_parameters[type->base - 1U];
    if (parameter.projection_trait != 0U) {
        const RSemanticTrait trait = context->semantic_traits[parameter.projection_trait - 1U];
        return context->semantic_associated_types[trait.first_associated +
                                                  parameter.projection_index]
            .parameter_schema;
    }
    for (size_t index = 0U; index < context->semantic_associated_type_count; ++index) {
        if (context->semantic_associated_types[index].parameter_type == type_id) {
            return context->semantic_associated_types[index].parameter_schema;
        }
    }
    return 0U;
}

static bool r_generic_substitute_type(RFrontendContext *, RTypeId, uint32_t, uint32_t, RTypeId *);
static bool r_generic_commit_arguments(RFrontendContext *, const RTypeVector *, uint32_t *);
static bool r_generic_validate_arguments(RFrontendContext *,
                                         uint32_t,
                                         const RTypeVector *,
                                         RSourceSpan);

/* R-TYPE-0045 (L16.3): the binding `function` of an associated type applied to `arguments`. */
static bool r_semantic_reduce_associated(RFrontendContext *context,
                                         RTypeId function,
                                         const RTypeVector *arguments,
                                         RTypeId *type_id) {
    const RSemanticType *type = r_semantic_type(context, function);
    uint32_t first = 0U;

    if ((type == NULL) || (type->kind != R_SEMANTIC_TYPE_TYPE_FUNCTION)) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    const RTypeId body = type->base;
    const uint32_t schema = (uint32_t)type->length;
    return r_generic_commit_arguments(context, arguments, &first) &&
           r_generic_substitute_type(context, body, schema, first, type_id);
}

/* R-TYPE-0045 (L16.3): the synthetic parameter that applies the associated type of a trait or
 * the projection `base` to the argument chain `arguments`; one per application. */
static bool r_semantic_associated_application(RFrontendContext *context,
                                              RTypeId base,
                                              RTypeId arguments,
                                              RSourceSpan span,
                                              RTypeId *type_id) {
    const RSemanticType *base_type = r_semantic_type(context, base);
    RGenericParameter parameter;

    for (size_t existing = 0U; existing < context->generic_parameter_count; ++existing) {
        const RGenericParameter *candidate = &context->generic_parameters[existing];
        if ((candidate->application_base == base) &&
            (candidate->application_arguments == arguments)) {
            *type_id = candidate->type;
            return true;
        }
    }
    if ((base_type == NULL) || (base_type->kind != R_SEMANTIC_TYPE_PARAMETER) ||
        (base_type->base == 0U) || ((size_t)base_type->base > context->generic_parameter_count)) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    const RGenericParameter source = context->generic_parameters[base_type->base - 1U];
    (void)memset(&parameter, 0, sizeof(parameter));
    parameter.name_span = source.name_span;
    parameter.name_intern_id = source.name_intern_id;
    parameter.constraints = source.constraints;
    parameter.first_trait_constraint = source.first_trait_constraint;
    parameter.trait_constraint_count = source.trait_constraint_count;
    parameter.application_base = base;
    parameter.application_arguments = arguments;
    parameter.application_span = span;
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

/* R-TYPE-0045 (L16.3): the arguments of the chain of an application. */
static bool r_semantic_chain_arguments(RFrontendContext *context,
                                       RTypeId chain,
                                       RTypeVector *arguments) {
    while (chain != R_TYPE_ID_INVALID) {
        const RSemanticType *link = r_semantic_type(context, chain);
        if ((link == NULL) || (link->kind != R_SEMANTIC_TYPE_FUNCTION_PARAMETER)) {
            context->resource_status = R_FRONTEND_INTERNAL_ERROR;
            return false;
        }
        const RTypeId argument = link->base;
        chain = link->second;
        if (!r_type_vector_push(context, arguments, argument)) {
            return false;
        }
    }
    return true;
}

/* R-TYPE-0045 (L16.3): the argument chain of an application, one FUNCTION_PARAMETER per
 * argument. */
static bool r_semantic_associated_chain(RFrontendContext *context,
                                        const RTypeVector *arguments,
                                        RTypeId *chain) {
    *chain = R_TYPE_ID_INVALID;
    for (size_t index = arguments->count; index != 0U; --index) {
        if (!r_semantic_intern_derived_type(context,
                                            R_SEMANTIC_TYPE_FUNCTION_PARAMETER,
                                            arguments->items[index - 1U],
                                            *chain,
                                            UINT64_C(0),
                                            R_SEMANTIC_TYPE_FLAG_NONE,
                                            chain)) {
            return false;
        }
    }
    return true;
}

/* R-TYPE-0045 (L16.3): `associated<arguments>`. A binding is applied at once; an associated type
 * of a trait or a projection stays an application until instantiation. The arguments match the
 * parameters in number and prove their constraints. */
static bool r_semantic_apply_associated(RFrontendContext *context,
                                        RTypeId associated,
                                        const RTypeVector *arguments,
                                        bool written,
                                        RSourceSpan span,
                                        RTypeId *type_id) {
    const uint32_t schema = r_semantic_associated_parameter_schema(context, associated);
    const uint32_t count =
        schema == 0U ? 0U : context->generic_schemas[schema - 1U].parameter_count;
    RTypeId chain = R_TYPE_ID_INVALID;

    *type_id = associated;
    if (!written && (count == 0U)) {
        return true;
    }
    if (arguments->count != count) {
        (void)r_semantic_add_diagnostic(context,
                                        "R-DIAG-TYPE-001",
                                        "R-TYPE-0045",
                                        count == 0U ? "this associated type takes no type arguments"
                                                    : "an associated type with parameters is "
                                                      "applied to one type argument per parameter",
                                        span);
        return false;
    }
    const size_t diagnostics = context->diagnostic_count;
    if (!r_generic_validate_arguments(context, schema, arguments, span) ||
        (context->diagnostic_count != diagnostics)) {
        return false;
    }
    if (r_semantic_value_kind(context, associated) == R_SEMANTIC_TYPE_TYPE_FUNCTION) {
        return r_semantic_reduce_associated(context, associated, arguments, type_id);
    }
    return r_semantic_associated_chain(context, arguments, &chain) &&
           r_semantic_associated_application(context, associated, chain, span, type_id);
}

static uint32_t r_semantic_constant_index(const RFrontendContext *, uint32_t, uint32_t);

/* R-TYPE-0050: the trait and position of the associated constant `name_intern_id` that `type`
 * provides through exactly one trait: a parameter through its constraints, a closed type through
 * its implementations. `trait` stays zero when none provides it. */
static bool r_semantic_find_constant(RFrontendContext *context,
                                     RTypeId type,
                                     uint32_t name_intern_id,
                                     uint32_t *trait,
                                     uint32_t *index,
                                     bool *ambiguous) {
    uint32_t candidate;
    *trait = UINT32_C(0);
    *index = UINT32_MAX;
    *ambiguous = false;
    for (candidate = 1U; candidate <= context->semantic_trait_count; ++candidate) {
        const RSemanticTrait *record = &context->semantic_traits[candidate - 1U];
        uint32_t position;
        if ((record->constant_count == 0U) ||
            (record->generic_origin == 0U && record->type_parameter_schema != 0U)) {
            continue;
        }
        position = r_semantic_constant_index(context, candidate, name_intern_id);
        if (position == UINT32_MAX) {
            continue;
        }
        if (!r_semantic_type_implements_trait(context, type, candidate)) {
            if (context->resource_status != R_FRONTEND_OK) {
                return false;
            }
            continue;
        }
        if (*trait != UINT32_C(0)) {
            *ambiguous = true;
            return true;
        }
        *trait = candidate;
        *index = position;
    }
    return true;
}

/* R-TYPE-0050: `P::NAME` over a type that is not closed: a formula over P and the arguments of
 * the trait application, closed by substitution. */
static bool r_semantic_constant_projection(
    RFrontendContext *context, RTypeId type, uint32_t trait, uint32_t index, RTypeId *result) {
    const RSemanticTrait record = context->semantic_traits[trait - 1U];
    const uint32_t origin = record.generic_origin != 0U ? record.generic_origin : trait;
    RTypeId chain = R_TYPE_ID_INVALID;
    if (record.generic_origin != 0U && record.type_parameter_schema != 0U) {
        uint32_t argument =
            context->generic_schemas[record.type_parameter_schema - 1U].parameter_count;
        for (; argument != 0U; --argument) {
            if (!r_semantic_intern_derived_type(
                    context,
                    R_SEMANTIC_TYPE_CONSTANT_EXPR,
                    context->generic_arguments[record.first_generic_argument + argument - 1U],
                    chain,
                    0U,
                    (uint32_t)R_TOKEN_COMMA,
                    &chain))
                return false;
        }
    }
    return r_semantic_intern_derived_type(context,
                                          R_SEMANTIC_TYPE_CONSTANT_EXPR,
                                          type,
                                          chain,
                                          0U,
                                          (uint32_t)R_TOKEN_COMMA,
                                          &chain) &&
           r_semantic_intern_derived_type(context,
                                          R_SEMANTIC_TYPE_CONSTANT_EXPR,
                                          chain,
                                          R_TYPE_ID_INVALID,
                                          ((uint64_t)origin << 32U) | index,
                                          (uint32_t)R_TOKEN_DOT,
                                          result);
}

/* The implementation of applied trait `trait` for closed type `type`, or zero. */
static bool r_semantic_constant_impl(RFrontendContext *context,
                                     RTypeId type,
                                     uint32_t trait,
                                     uint32_t *impl_id) {
    RTypeId canonical;
    uint32_t aggregate;
    size_t index;
    *impl_id = UINT32_C(0);
    r_semantic_canonical_owner(context, type, &canonical, &aggregate);
    for (index = 0U; index < context->semantic_impl_count; ++index) {
        const RSemanticImpl impl = context->semantic_impls[index];
        RTypeId impl_owner;
        uint32_t impl_aggregate;
        uint32_t effective;
        if (impl.poisoned) {
            continue;
        }
        r_semantic_canonical_owner(context, impl.target_type, &impl_owner, &impl_aggregate);
        if ((impl_owner != canonical) || (impl_aggregate != aggregate)) {
            continue;
        }
        if (!r_semantic_impl_trait(context, (uint32_t)index + 1U, type, &effective)) {
            return false;
        }
        if (effective == trait) {
            *impl_id = (uint32_t)index + 1U;
            return true;
        }
    }
    return true;
}

/* R-TYPE-0045: `Self::Name` inside a trait is the schema parameter of the associated type;
 * inside an implementation it is the bound type. */
static bool r_semantic_self_projection_at(RFrontendContext *context,
                                          RSourceSpan span,
                                          uint32_t name_intern_id,
                                          RTypeId *type_id) {
    const uint32_t trait = r_semantic_trait_at(context, span);
    const uint32_t impl = r_semantic_impl_at(context, span);
    uint32_t index;
    if (trait != UINT32_C(0)) {
        index = r_semantic_associated_index(context, trait, name_intern_id);
        if (index == UINT32_MAX) {
            bool ambiguous;
            return r_semantic_parameter_projection(context,
                                                   context->semantic_traits[trait - 1U].self_type,
                                                   name_intern_id,
                                                   type_id,
                                                   &ambiguous);
        }
        *type_id =
            context
                ->semantic_associated_types[context->semantic_traits[trait - 1U].first_associated +
                                            index]
                .parameter_type;
        return true;
    }
    if ((impl != UINT32_C(0)) && (context->semantic_impls[impl - 1U].trait != UINT32_C(0))) {
        const RSemanticImpl *record = &context->semantic_impls[impl - 1U];
        index = r_semantic_associated_index(context, record->trait, name_intern_id);
        if (index == UINT32_MAX) {
            return false;
        }
        *type_id = context->semantic_associated_bindings[record->first_binding + index];
        return *type_id != R_TYPE_ID_INVALID;
    }
    return false;
}

/* R-TYPE-0045: the associated types a parameter's trait constraints declare under one name;
 * exactly one constraint shall provide it. */
static bool r_semantic_parameter_projection(RFrontendContext *context,
                                            RTypeId parameter_type,
                                            uint32_t name_intern_id,
                                            RTypeId *type_id,
                                            bool *ambiguous) {
    const RSemanticType *type = r_semantic_type(context, parameter_type);
    uint32_t found_trait = UINT32_C(0);
    uint32_t found_index = UINT32_MAX;

    *ambiguous = false;
    if ((type == NULL) || (type->kind != R_SEMANTIC_TYPE_PARAMETER) || (type->base == 0U) ||
        ((size_t)type->base > context->generic_parameter_count)) {
        return false;
    }
    for (uint32_t trait = 1U; trait <= context->semantic_trait_count; ++trait) {
        if (context->semantic_traits[trait - 1U].generic_origin == 0U &&
            context->semantic_traits[trait - 1U].type_parameter_schema != 0U)
            continue;
        if (!r_semantic_type_implements_trait(context, parameter_type, trait))
            continue;
        const uint32_t index = r_semantic_associated_index(context, trait, name_intern_id);
        if (index == UINT32_MAX) {
            continue;
        }
        if (found_trait != UINT32_C(0)) {
            *ambiguous = true;
            return false;
        }
        found_trait = trait;
        found_index = index;
    }
    if (found_trait == UINT32_C(0)) {
        return false;
    }
    return r_semantic_trait_projection(context, parameter_type, found_trait, found_index, type_id);
}

/* The name of an associated-type member: `type Name` has the contextual `type` first. */
static bool
r_semantic_associated_name(const RFrontendContext *context, RAstRef member, RAstTokenView *name) {
    RAstNodeView view;
    uint32_t index;
    uint32_t seen = 0U;
    if (!r_ast_ref_view(context, member, &view)) {
        return false;
    }
    for (index = 0U; index < view.child_count; ++index) {
        RAstRef child;
        if (r_ast_ref_child(context, member, index, &child) &&
            r_ast_ref_token(context, child, name) && (name->kind == R_TOKEN_IDENTIFIER)) {
            if (seen == 1U) {
                return true;
            }
            seen += 1U;
        }
    }
    return false;
}

/* The arguments [Self, associated...] that apply one trait prototype to a closed target or a
 * parameter (R-TYPE-0045). */
static bool r_semantic_trait_arguments(RFrontendContext *context,
                                       uint32_t trait,
                                       RTypeId self_type,
                                       uint32_t impl,
                                       RTypeVector *arguments) {
    const RSemanticTrait record = context->semantic_traits[trait - 1U];
    uint32_t index;
    if (!r_type_vector_push(context, arguments, self_type)) {
        return false;
    }
    if (record.type_parameter_schema != 0U) {
        const RGenericSchema parameters =
            context->generic_schemas[record.type_parameter_schema - 1U];
        for (index = 0U; index < parameters.parameter_count; ++index) {
            const RTypeId argument =
                record.generic_origin == 0U
                    ? context->generic_parameters[parameters.first_parameter + index].type
                    : context->generic_arguments[record.first_generic_argument + index];
            if (!r_type_vector_push(context, arguments, argument))
                return false;
        }
    }
    for (index = 0U; index < record.associated_count; ++index) {
        RTypeId associated;
        if (impl != UINT32_C(0)) {
            associated = context->semantic_associated_bindings
                             [context->semantic_impls[impl - 1U].first_binding + index];
            if (context->semantic_impls[impl - 1U].generic_schema != 0U &&
                !r_semantic_resolve_projection(context, self_type, trait, index, &associated))
                return false;
        } else if (self_type == record.self_type) {
            associated =
                context->semantic_associated_types[record.first_associated + index].parameter_type;
        } else if (r_dyn_contract(context, self_type) != NULL) {
            /* R-TYPE-0051: an interface fixes every associated type of its contract. */
            if (!r_dyn_projection(context, self_type, trait, index, &associated))
                return false;
        } else if (!r_semantic_trait_projection(context, self_type, trait, index, &associated)) {
            return false;
        }
        if (!r_type_vector_push(context, arguments, associated)) {
            return false;
        }
    }
    return true;
}

/* R-TYPE-0046: the compiler declares core::Iterator, core::LendingIterator, core::Contains and
 * core::CaseMatcher (and core::Format, below) before user traits. Each has one associated type (Item, or Label for
 * core::CaseMatcher) and one prototype over Self and it; the `next` of core::LendingIterator
 * lends (L16.2), and `matches` of core::CaseMatcher takes its label by value (L20.4). */
static bool r_semantic_declare_core_trait(RFrontendContext *context,
                                          const char *name,
                                          const char *method,
                                          const char *associated_name,
                                          bool shared_receiver,
                                          bool item_argument,
                                          bool item_by_value,
                                          bool option_result,
                                          bool lending,
                                          uint32_t *trait_id) {
    RSemanticTrait trait;
    RGenericSchema schema;
    RGenericParameter self_parameter;
    RGenericParameter item_parameter;
    RSemanticAssociatedType associated;
    RSemanticSymbol prototype;
    RTypeVector parameters = {0};
    RTypeId receiver_type;
    RTypeId argument_type;
    RSymbolId prototype_id;
    bool success = false;

    (void)memset(&trait, 0, sizeof(trait));
    (void)memset(&schema, 0, sizeof(schema));
    (void)memset(&self_parameter, 0, sizeof(self_parameter));
    (void)memset(&item_parameter, 0, sizeof(item_parameter));
    (void)memset(&associated, 0, sizeof(associated));
    (void)memset(&prototype, 0, sizeof(prototype));
    if (!r_intern_bytes(context, (const uint8_t *)name, strlen(name), &trait.name_intern_id) ||
        !r_intern_bytes(context, (const uint8_t *)"Self", 4U, &self_parameter.name_intern_id) ||
        !r_intern_bytes(context,
                        (const uint8_t *)associated_name,
                        strlen(associated_name),
                        &item_parameter.name_intern_id) ||
        !r_grow_array(context,
                      (void **)&context->generic_parameters,
                      &context->generic_parameter_capacity,
                      sizeof(self_parameter),
                      context->generic_parameter_count + 2U) ||
        !r_semantic_intern_type(context,
                                R_SEMANTIC_TYPE_PARAMETER,
                                (RTypeId)context->generic_parameter_count + 1U,
                                &self_parameter.type) ||
        !r_semantic_intern_type(context,
                                R_SEMANTIC_TYPE_PARAMETER,
                                (RTypeId)context->generic_parameter_count + 2U,
                                &item_parameter.type)) {
        return false;
    }
    schema.first_parameter = (uint32_t)context->generic_parameter_count;
    schema.parameter_count = UINT32_C(2);
    context->generic_parameters[context->generic_parameter_count++] = self_parameter;
    context->generic_parameters[context->generic_parameter_count++] = item_parameter;
    if (!r_grow_array(context,
                      (void **)&context->generic_schemas,
                      &context->generic_schema_capacity,
                      sizeof(schema),
                      context->generic_schema_count + 1U)) {
        return false;
    }
    context->generic_schemas[context->generic_schema_count++] = schema;
    associated.name_intern_id = item_parameter.name_intern_id;
    associated.parameter_type = item_parameter.type;
    if (!r_grow_array(context,
                      (void **)&context->semantic_associated_types,
                      &context->semantic_associated_type_capacity,
                      sizeof(associated),
                      context->semantic_associated_type_count + 1U)) {
        return false;
    }
    trait.first_associated = (uint32_t)context->semantic_associated_type_count;
    trait.associated_count = UINT32_C(1);
    context->semantic_associated_types[context->semantic_associated_type_count++] = associated;
    trait.generic_schema = (uint32_t)context->generic_schema_count;
    trait.self_type = self_parameter.type;
    trait.is_core = true;
    if (!r_grow_array(context,
                      (void **)&context->semantic_traits,
                      &context->semantic_trait_capacity,
                      sizeof(trait),
                      context->semantic_trait_count + 1U)) {
        return false;
    }
    context->semantic_traits[context->semantic_trait_count++] = trait;
    *trait_id = (uint32_t)context->semantic_trait_count;

    if (!r_semantic_intern_derived_type(context,
                                        R_SEMANTIC_TYPE_BORROW,
                                        self_parameter.type,
                                        R_TYPE_ID_INVALID,
                                        UINT64_C(0),
                                        shared_receiver ? R_SEMANTIC_TYPE_FLAG_SHARED
                                                        : R_SEMANTIC_TYPE_FLAG_NONE,
                                        &receiver_type) ||
        !r_type_vector_push(context, &parameters, receiver_type)) {
        goto cleanup;
    }
    if (item_argument && item_by_value) {
        argument_type = item_parameter.type;
        if (!r_type_vector_push(context, &parameters, argument_type)) {
            goto cleanup;
        }
    } else if (item_argument && (!r_semantic_intern_derived_type(context,
                                                                 R_SEMANTIC_TYPE_BORROW,
                                                                 item_parameter.type,
                                                                 R_TYPE_ID_INVALID,
                                                                 UINT64_C(0),
                                                                 R_SEMANTIC_TYPE_FLAG_SHARED,
                                                                 &argument_type) ||
                                 !r_type_vector_push(context, &parameters, argument_type))) {
        goto cleanup;
    }
    if (option_result
            ? !r_semantic_intern_type(
                  context, R_SEMANTIC_TYPE_OPTION, item_parameter.type, &prototype.return_type)
            : !r_semantic_type_from_token(context, R_TOKEN_KW_BOOL, &prototype.return_type)) {
        goto cleanup;
    }
    prototype.kind = R_SEMANTIC_SYMBOL_FUNCTION;
    prototype.parameter_count = (uint32_t)parameters.count;
    prototype.generic_schema = trait.generic_schema;
    prototype.associated_type = self_parameter.type;
    prototype.receiver_kind =
        shared_receiver ? R_SEMANTIC_RECEIVER_SHARED : R_SEMANTIC_RECEIVER_EXCLUSIVE;
    prototype.trait = *trait_id;
    prototype.is_method = true;
    prototype.is_trait_prototype = true;
    prototype.is_synthetic = true;
    prototype.is_lending = lending;
    prototype.has_prototype = true;
    prototype.signature_supported = true;
    if (!r_semantic_commit_parameter_types(context, &parameters, &prototype.first_parameter_type)) {
        goto cleanup;
    }
    r_semantic_prototype_borrow_mapping(
        context, &prototype, parameters.items, (uint32_t)parameters.count);
    if (!r_intern_bytes(
            context, (const uint8_t *)method, strlen(method), &prototype.method_name_intern_id) ||
        !r_generic_method_symbol_name(context,
                                      self_parameter.type,
                                      prototype.method_name_intern_id,
                                      &prototype.name_intern_id) ||
        !r_semantic_append_symbol(context, prototype, &prototype_id) ||
        !r_semantic_register_method(context, prototype_id)) {
        goto cleanup;
    }
    success = true;
cleanup:
    r_context_free(context, parameters.items);
    return success;
}

/* R-TYPE-0046 (L32): core::Format has no associated type; its one prototype
 * `void format(const Self* this, std.format::builder* out) throws std.alloc::alloc_error`
 * appends the text of the value to a builder. */
static bool r_semantic_declare_core_format_trait(RFrontendContext *context, uint32_t *trait_id) {
    RSemanticTrait trait;
    RGenericSchema schema;
    RGenericParameter self_parameter;
    RSemanticSymbol prototype;
    RTypeVector parameters = {0};
    RTypeId receiver_type;
    RTypeId builder_type;
    RTypeId output_type;
    RTypeId alloc_error;
    RSymbolId prototype_id;
    bool success = false;

    (void)memset(&trait, 0, sizeof(trait));
    (void)memset(&schema, 0, sizeof(schema));
    (void)memset(&self_parameter, 0, sizeof(self_parameter));
    (void)memset(&prototype, 0, sizeof(prototype));
    if (!r_intern_bytes(context, (const uint8_t *)"Format", 6U, &trait.name_intern_id) ||
        !r_intern_bytes(context, (const uint8_t *)"Self", 4U, &self_parameter.name_intern_id) ||
        !r_grow_array(context,
                      (void **)&context->generic_parameters,
                      &context->generic_parameter_capacity,
                      sizeof(self_parameter),
                      context->generic_parameter_count + 1U) ||
        !r_semantic_intern_type(context,
                                R_SEMANTIC_TYPE_PARAMETER,
                                (RTypeId)context->generic_parameter_count + 1U,
                                &self_parameter.type)) {
        return false;
    }
    schema.first_parameter = (uint32_t)context->generic_parameter_count;
    schema.parameter_count = UINT32_C(1);
    context->generic_parameters[context->generic_parameter_count++] = self_parameter;
    if (!r_grow_array(context,
                      (void **)&context->generic_schemas,
                      &context->generic_schema_capacity,
                      sizeof(schema),
                      context->generic_schema_count + 1U)) {
        return false;
    }
    context->generic_schemas[context->generic_schema_count++] = schema;
    trait.first_associated = (uint32_t)context->semantic_associated_type_count;
    trait.associated_count = UINT32_C(0);
    trait.generic_schema = (uint32_t)context->generic_schema_count;
    trait.self_type = self_parameter.type;
    trait.is_core = true;
    if (!r_grow_array(context,
                      (void **)&context->semantic_traits,
                      &context->semantic_trait_capacity,
                      sizeof(trait),
                      context->semantic_trait_count + 1U)) {
        return false;
    }
    context->semantic_traits[context->semantic_trait_count++] = trait;
    *trait_id = (uint32_t)context->semantic_trait_count;

    if (!r_semantic_intern_derived_type(context,
                                        R_SEMANTIC_TYPE_BORROW,
                                        self_parameter.type,
                                        R_TYPE_ID_INVALID,
                                        UINT64_C(0),
                                        R_SEMANTIC_TYPE_FLAG_SHARED,
                                        &receiver_type) ||
        !r_semantic_intern_named_standard_type(context, "std.format::builder", &builder_type) ||
        !r_semantic_intern_derived_type(context,
                                        R_SEMANTIC_TYPE_BORROW,
                                        builder_type,
                                        R_TYPE_ID_INVALID,
                                        UINT64_C(0),
                                        R_SEMANTIC_TYPE_FLAG_NONE,
                                        &output_type) ||
        !r_type_vector_push(context, &parameters, receiver_type) ||
        !r_type_vector_push(context, &parameters, output_type) ||
        !r_semantic_type_from_token(context, R_TOKEN_KW_VOID, &prototype.return_type) ||
        !r_semantic_intern_named_standard_type(context, "std.alloc::alloc_error", &alloc_error) ||
        !r_semantic_single_effect_set(context, alloc_error, &prototype.throws_type) ||
        !r_semantic_intern_effect_carrier(
            context, prototype.return_type, prototype.throws_type, &prototype.effect_carrier_type)) {
        goto cleanup;
    }
    prototype.kind = R_SEMANTIC_SYMBOL_FUNCTION;
    prototype.parameter_count = (uint32_t)parameters.count;
    prototype.generic_schema = trait.generic_schema;
    prototype.associated_type = self_parameter.type;
    prototype.receiver_kind = R_SEMANTIC_RECEIVER_SHARED;
    prototype.trait = *trait_id;
    prototype.is_method = true;
    prototype.is_trait_prototype = true;
    prototype.is_synthetic = true;
    prototype.has_prototype = true;
    prototype.signature_supported = true;
    if (!r_semantic_commit_parameter_types(context, &parameters, &prototype.first_parameter_type)) {
        goto cleanup;
    }
    r_semantic_prototype_borrow_mapping(
        context, &prototype, parameters.items, (uint32_t)parameters.count);
    if (!r_intern_bytes(
            context, (const uint8_t *)"format", 6U, &prototype.method_name_intern_id) ||
        !r_generic_method_symbol_name(context,
                                      self_parameter.type,
                                      prototype.method_name_intern_id,
                                      &prototype.name_intern_id) ||
        !r_semantic_append_symbol(context, prototype, &prototype_id) ||
        !r_semantic_register_method(context, prototype_id)) {
        goto cleanup;
    }
    success = true;
cleanup:
    r_context_free(context, parameters.items);
    return success;
}

/* R-TYPE-0046 (L36.1): core::AsyncIterator is core::Iterator whose `next` is
 * `@scoped async o<Self::Item> next(Self* this) throws std.error::fault`; Self is Send, so that
 * the receiver can be a loan of a task group, and Item is Send and holds no borrow, so that it
 * can be a task result. Only a hosted-native-async program has asynchronous functions. */
static bool r_semantic_declare_core_async_iterator_trait(RFrontendContext *context,
                                                         uint32_t *trait_id) {
    RSemanticSymbol *prototype = NULL;
    RTypeId fault;

    if (context->profile < R_FRONTEND_PROFILE_HOSTED_NATIVE_ASYNC) {
        return true;
    }
    if (!r_semantic_declare_core_trait(
            context, "AsyncIterator", "next", "Item", false, false, false, true, false, trait_id)) {
        return false;
    }
    for (size_t index = context->semantic_symbol_count; index != 0U; --index) {
        RSemanticSymbol *symbol = &context->semantic_symbols[index - 1U];
        if (symbol->is_trait_prototype && (symbol->trait == *trait_id)) {
            prototype = symbol;
            break;
        }
    }
    if ((prototype == NULL) || (prototype->generic_schema == 0U)) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    {
        const RGenericSchema *schema = &context->generic_schemas[prototype->generic_schema - 1U];
        context->generic_parameters[schema->first_parameter].constraints |= R_GENERIC_SEND;
        context->generic_parameters[schema->first_parameter + 1U].constraints |=
            R_GENERIC_SEND | R_GENERIC_UNBORROWED;
    }
    if (!r_semantic_intern_named_standard_type(context, "std.error::fault", &fault) ||
        !r_semantic_resolve_standard_fault(context, &fault)) {
        return false;
    }
    /* The fault family may have grown the symbol table; find the prototype again. */
    for (size_t index = context->semantic_symbol_count; index != 0U; --index) {
        RSemanticSymbol *symbol = &context->semantic_symbols[index - 1U];
        if (symbol->is_trait_prototype && (symbol->trait == *trait_id)) {
            prototype = symbol;
            break;
        }
    }
    /* `throws std.error::fault` names the exact errors of the family (R-TYPE-0012, L21.2). */
    RTypeId throws_type = R_TYPE_ID_INVALID, carrier, task_type, start_type;
    const RTypeId return_type = prototype->return_type;
    const RSemanticAggregate *family = r_error_family(context, fault);
    RTypeVector errors = {0};
    bool duplicate = false, interned = family != NULL;
    for (uint32_t tag = 0U; interned && (tag < family->variant_count); ++tag) {
        const RTypeId member = context->semantic_variants[family->first_variant + tag].payload_type;
        interned = !r_semantic_type_is_inhabited(context, member, 0U) ||
                   r_type_vector_push(context, &errors, member);
    }
    interned = interned && r_semantic_intern_effect_set(context, &errors, &throws_type, &duplicate) &&
               !duplicate;
    r_context_free(context, errors.items);
    if (!interned ||
        !r_semantic_intern_effect_carrier(context, return_type, throws_type, &carrier) ||
        !r_semantic_checked_async_types(context, return_type, throws_type, &task_type, &start_type)) {
        if (context->resource_status == R_FRONTEND_OK)
            context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    for (size_t index = context->semantic_symbol_count; index != 0U; --index) {
        RSemanticSymbol *symbol = &context->semantic_symbols[index - 1U];
        if (symbol->is_trait_prototype && (symbol->trait == *trait_id)) {
            symbol->is_async = true;
            symbol->is_scoped = true;
            symbol->throws_type = throws_type;
            symbol->effect_carrier_type = carrier;
            symbol->async_task_type = task_type;
            symbol->async_start_type = start_type;
            return true;
        }
    }
    context->resource_status = R_FRONTEND_INTERNAL_ERROR;
    return false;
}

static bool r_semantic_declare_core_traits(RFrontendContext *context) {
    if (context->core_iterator_trait != UINT32_C(0)) {
        return true;
    }
    return r_semantic_declare_core_trait(context,
                                         "Iterator",
                                         "next",
                                         "Item",
                                         false,
                                         false,
                                         false,
                                         true,
                                         false,
                                         &context->core_iterator_trait) &&
           r_semantic_declare_core_trait(context,
                                         "Contains",
                                         "contains",
                                         "Item",
                                         true,
                                         true,
                                         false,
                                         false,
                                         false,
                                         &context->core_contains_trait) &&
           r_semantic_declare_core_trait(context,
                                         "LendingIterator",
                                         "next",
                                         "Item",
                                         false,
                                         false,
                                         false,
                                         true,
                                         true,
                                         &context->core_lending_iterator_trait) &&
           r_semantic_declare_core_trait(context,
                                         "CaseMatcher",
                                         "matches",
                                         "Label",
                                         true,
                                         true,
                                         true,
                                         false,
                                         false,
                                         &context->core_case_matcher_trait) &&
           r_semantic_declare_core_format_trait(context, &context->core_format_trait) &&
           r_semantic_declare_core_async_iterator_trait(context,
                                                        &context->core_async_iterator_trait);
}

/* R-TYPE-0046: `core::Name` resolves among the core traits only; the core traits are
 * declared on their first mention, so a program that never names them is unchanged. */
static uint32_t r_semantic_resolve_core_trait_name(RFrontendContext *context,
                                                   uint32_t name_intern_id) {
    size_t index;
    if ((context->core_iterator_trait == UINT32_C(0)) && !r_semantic_declare_core_traits(context)) {
        return UINT32_C(0);
    }
    for (index = 0U; index < context->semantic_trait_count; ++index) {
        const RSemanticTrait *trait = &context->semantic_traits[index];
        if (trait->is_core && (trait->name_intern_id == name_intern_id)) {
            return (uint32_t)index + UINT32_C(1);
        }
    }
    return UINT32_C(0);
}

static bool r_semantic_append_trait_constraint(RFrontendContext *context,
                                               uint32_t parameter_index,
                                               uint32_t trait) {
    RGenericParameter *parameter = &context->generic_parameters[parameter_index];
    const uint32_t count = parameter->trait_constraint_count;
    for (uint32_t index = 0U; index < count; ++index) {
        if (context->generic_trait_constraints[parameter->first_trait_constraint + index] == trait)
            return true;
    }
    const bool relocate = count != 0U && parameter->first_trait_constraint + count !=
                                             context->generic_trait_constraint_count;
    if (!r_grow_array(context,
                      (void **)&context->generic_trait_constraints,
                      &context->generic_trait_constraint_capacity,
                      sizeof(*context->generic_trait_constraints),
                      context->generic_trait_constraint_count + 1U + (relocate ? count : 0U))) {
        return false;
    }
    if (relocate) {
        (void)memmove(context->generic_trait_constraints + context->generic_trait_constraint_count,
                      context->generic_trait_constraints + parameter->first_trait_constraint,
                      count * sizeof(*context->generic_trait_constraints));
        parameter->first_trait_constraint = (uint32_t)context->generic_trait_constraint_count;
        context->generic_trait_constraint_count += count;
    }
    if (parameter->trait_constraint_count == UINT32_C(0)) {
        parameter->first_trait_constraint = (uint32_t)context->generic_trait_constraint_count;
    }
    context->generic_trait_constraints[context->generic_trait_constraint_count] = trait;
    context->generic_trait_constraint_count += 1U;
    parameter->trait_constraint_count += 1U;
    return true;
}

/* R-TYPE-0043 (L44): adds a trait of a `fields(Trait)` constraint to a parameter; the list shares
   the storage of trait constraints and moves to its end when it grows. */
static bool
r_semantic_append_field_trait(RFrontendContext *context, uint32_t parameter_index, uint32_t trait) {
    RGenericParameter *parameter = &context->generic_parameters[parameter_index];
    const uint32_t count = parameter->field_trait_count;
    for (uint32_t index = 0U; index < count; ++index) {
        if (context->generic_trait_constraints[parameter->first_field_trait + index] == trait)
            return true;
    }
    if (!r_grow_array(context,
                      (void **)&context->generic_trait_constraints,
                      &context->generic_trait_constraint_capacity,
                      sizeof(*context->generic_trait_constraints),
                      context->generic_trait_constraint_count + 1U + count)) {
        return false;
    }
    parameter = &context->generic_parameters[parameter_index];
    if ((count != 0U) &&
        (parameter->first_field_trait + count != context->generic_trait_constraint_count)) {
        (void)memmove(context->generic_trait_constraints + context->generic_trait_constraint_count,
                      context->generic_trait_constraints + parameter->first_field_trait,
                      count * sizeof(*context->generic_trait_constraints));
        parameter->first_field_trait = (uint32_t)context->generic_trait_constraint_count;
        context->generic_trait_constraint_count += count;
    }
    if (count == 0U) {
        parameter->first_field_trait = (uint32_t)context->generic_trait_constraint_count;
    }
    context->generic_trait_constraints[context->generic_trait_constraint_count] = trait;
    context->generic_trait_constraint_count += 1U;
    parameter->field_trait_count += 1U;
    return true;
}

/* A trait is visible in a source when it is declared there or its module is imported. */
static uint32_t r_semantic_resolve_trait_name(const RFrontendContext *context,
                                              RSourceId source,
                                              uint32_t name_intern_id,
                                              bool *ambiguous) {
    uint32_t match = UINT32_C(0);
    size_t index;

    *ambiguous = false;
    for (index = 0U; index < context->semantic_trait_count; ++index) {
        const RSemanticTrait *trait = &context->semantic_traits[index];
        const RSource *declaration = r_get_source_const(context, trait->module_source);
        bool visible = trait->module_source == source;
        size_t import_index;
        if ((trait->name_intern_id != name_intern_id) || trait->is_core ||
            trait->generic_origin != 0U) {
            continue;
        }
        /* R-MOD-0002: an imported trait is named without its module only when the import
           selects it by name; `import m;` makes it available as m::Trait. */
        for (import_index = 0U; !visible && (import_index < context->import_count);
             ++import_index) {
            const RImportEntry *import = &context->imports[import_index];
            visible = (import->source == source) && import->imports_name && (declaration != NULL) &&
                      (declaration->module_name != NULL) &&
                      (strcmp(import->module_name, declaration->module_name) == 0) &&
                      r_source_text_equal(r_get_source_const(context, trait->name_span.source),
                                          trait->name_span,
                                          import->name);
        }
        if (!visible || (trait->is_protected && (trait->module_source != source))) {
            continue;
        }
        if (match != UINT32_C(0)) {
            *ambiguous = true;
            return match;
        }
        match = (uint32_t)index + UINT32_C(1);
    }
    return match;
}

static bool
r_semantic_first_identifier(const RFrontendContext *context, RAstRef node, RAstTokenView *name) {
    RAstNodeView view;
    uint32_t index;
    if (!r_ast_ref_view(context, node, &view)) {
        return false;
    }
    for (index = 0U; index < view.child_count; ++index) {
        RAstRef child;
        if (r_ast_ref_child(context, node, index, &child) &&
            r_ast_ref_token(context, child, name) && (name->kind == R_TOKEN_IDENTIFIER)) {
            return true;
        }
    }
    return false;
}

static bool
r_semantic_last_identifier(const RFrontendContext *context, RAstRef node, RAstTokenView *name) {
    RAstNodeView view;
    uint32_t index;
    bool found = false;
    if (!r_ast_ref_view(context, node, &view)) {
        return false;
    }
    for (index = 0U; index < view.child_count; ++index) {
        RAstRef child;
        RAstTokenView token;
        if (r_ast_ref_child(context, node, index, &child) &&
            r_ast_ref_token(context, child, &token) && (token.kind == R_TOKEN_IDENTIFIER)) {
            *name = token;
            found = true;
        }
    }
    return found;
}

/* R-TYPE-0050: the parts of `const T NAME [= value];`. */
static bool r_semantic_associated_constant_parts(RFrontendContext *context,
                                                 RAstRef member,
                                                 RAstTokenView *name,
                                                 RTypeId *type,
                                                 RAstRef *initializer) {
    RAstTokenView keyword;
    initializer->source = member.source;
    initializer->node = R_AST_NODE_ID_INVALID;
    if (!r_ast_direct_token(context, member, 1U, NULL, &keyword) ||
        !r_ast_direct_token(context, member, 2U, NULL, name) ||
        !r_semantic_type_from_token(context, keyword.kind, type)) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    if (!r_ast_find_direct_child(context, member, R_SYNTAX_EXPRESSION, initializer)) {
        initializer->source = member.source;
        initializer->node = R_AST_NODE_ID_INVALID;
    }
    return true;
}

static bool r_semantic_collect_trait_constants(RFrontendContext *context,
                                               RAstRef declaration,
                                               RSemanticTrait *trait) {
    RAstNodeView view;
    uint32_t member_index;
    if (!r_ast_ref_view(context, declaration, &view)) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    for (member_index = 0U; member_index < view.child_count; ++member_index) {
        RAstRef member;
        RAstNodeView member_view;
        RSemanticAssociatedConstant constant;
        uint32_t previous;
        if (!r_ast_ref_child(context, declaration, member_index, &member) ||
            !r_ast_ref_view(context, member, &member_view) ||
            (member_view.kind != R_SYNTAX_ASSOCIATED_CONSTANT)) {
            continue;
        }
        RAstTokenView name;
        (void)memset(&constant, 0, sizeof(constant));
        if (!r_semantic_associated_constant_parts(
                context, member, &name, &constant.type, &constant.initializer)) {
            return false;
        }
        for (previous = 0U; previous < trait->constant_count; ++previous) {
            if (context->semantic_associated_constants[trait->first_constant + previous]
                        .name_intern_id == name.intern_id &&
                !r_semantic_add_diagnostic(context,
                                           "R-DIAG-NAME-002",
                                           "R-TYPE-0050",
                                           "duplicate associated constant in one trait",
                                           name.span)) {
                return false;
            }
        }
        constant.name_span = name.span;
        constant.name_intern_id = name.intern_id;
        if (!r_grow_array(context,
                          (void **)&context->semantic_associated_constants,
                          &context->semantic_associated_constant_capacity,
                          sizeof(constant),
                          context->semantic_associated_constant_count + 1U)) {
            return false;
        }
        context->semantic_associated_constants[context->semantic_associated_constant_count++] =
            constant;
        trait->constant_count += UINT32_C(1);
    }
    return true;
}

/* R-TYPE-0050: the position of one associated constant within its trait, or UINT32_MAX. */
static uint32_t r_semantic_constant_index(const RFrontendContext *context,
                                          uint32_t trait,
                                          uint32_t name_intern_id) {
    const RSemanticTrait *record = &context->semantic_traits[trait - 1U];
    uint32_t index;
    for (index = 0U; index < record->constant_count; ++index) {
        if (context->semantic_associated_constants[record->first_constant + index].name_intern_id ==
            name_intern_id) {
            return index;
        }
    }
    return UINT32_MAX;
}

static bool r_semantic_collect_traits(RFrontendContext *context) {
    size_t source_index;
    uint32_t self_intern_id = UINT32_C(0);

    if (!r_intern_bytes(context, (const uint8_t *)"Self", 4U, &self_intern_id)) {
        return false;
    }
    for (source_index = 0U; source_index < context->source_count; ++source_index) {
        const RSourceId source_id = (RSourceId)(source_index + 1U);
        RAstRef root;
        RAstNodeView root_view;
        uint32_t child_index;
        if (!r_semantic_root_ref(context, source_id, &root) ||
            !r_ast_view_kind(context, root, R_SYNTAX_TRANSLATION_UNIT, &root_view)) {
            context->resource_status = R_FRONTEND_INTERNAL_ERROR;
            return false;
        }
        for (child_index = 0U; child_index < root_view.child_count; ++child_index) {
            RAstRef external, declaration;
            RAstNodeView declaration_view;
            RAstTokenView name;
            RSemanticTrait trait;
            RGenericSchema schema;
            RGenericParameter parameter;
            size_t existing;
            if (!r_ast_ref_child(context, root, child_index, &external)) {
                context->resource_status = R_FRONTEND_INTERNAL_ERROR;
                return false;
            }
            if (!r_ast_find_direct_child(
                    context, external, R_SYNTAX_TRAIT_DECLARATION, &declaration) ||
                !r_ast_ref_view(context, declaration, &declaration_view) ||
                !r_semantic_first_identifier(context, declaration, &name)) {
                continue;
            }
            (void)memset(&trait, 0, sizeof(trait));
            (void)memset(&schema, 0, sizeof(schema));
            (void)memset(&parameter, 0, sizeof(parameter));
            trait.type_parameter_schema = r_generic_schema_at(context, name.span);
            for (existing = 0U; existing < context->semantic_trait_count; ++existing) {
                if ((context->semantic_traits[existing].name_intern_id == name.intern_id) &&
                    (context->semantic_traits[existing].module_source == source_id) &&
                    !r_semantic_add_diagnostic(context,
                                               "R-DIAG-NAME-002",
                                               "R-NAME-0004",
                                               "duplicate trait declaration",
                                               name.span)) {
                    return false;
                }
            }
            parameter.name_span = name.span;
            parameter.name_intern_id = self_intern_id;
            if (!r_grow_array(context,
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
            schema.span = declaration_view.span;
            schema.external_ast = external.node;
            schema.declaration_ast = declaration.node;
            schema.first_parameter = (uint32_t)context->generic_parameter_count;
            schema.parameter_count = UINT32_C(1);
            context->generic_parameters[context->generic_parameter_count++] = parameter;
            if (trait.type_parameter_schema != 0U) {
                const RGenericSchema parameters =
                    context->generic_schemas[trait.type_parameter_schema - 1U];
                if (!r_grow_array(context,
                                  (void **)&context->generic_parameters,
                                  &context->generic_parameter_capacity,
                                  sizeof(parameter),
                                  context->generic_parameter_count + parameters.parameter_count))
                    return false;
                for (uint32_t index = 0U; index < parameters.parameter_count; ++index) {
                    const RGenericParameter argument =
                        context->generic_parameters[parameters.first_parameter + index];
                    if (argument.constant_type != 0U &&
                        !r_semantic_add_diagnostic(context,
                                                   "R-DIAG-TRAIT-001",
                                                   "R-TYPE-0041",
                                                   "traits accept type parameters only",
                                                   argument.name_span))
                        return false;
                    context->generic_parameters[context->generic_parameter_count++] = argument;
                    ++schema.parameter_count;
                }
            }
            /* R-TYPE-0045: every associated type is a schema parameter after Self. */
            trait.first_associated = (uint32_t)context->semantic_associated_type_count;
            {
                uint32_t member_index;
                for (member_index = 0U; member_index < declaration_view.child_count;
                     ++member_index) {
                    RAstRef member;
                    RAstNodeView member_view;
                    RAstTokenView associated_name;
                    RGenericParameter associated_parameter;
                    RSemanticAssociatedType associated;
                    uint32_t previous;
                    if (!r_ast_ref_child(context, declaration, member_index, &member) ||
                        !r_ast_ref_view(context, member, &member_view) ||
                        (member_view.kind != R_SYNTAX_ASSOCIATED_TYPE)) {
                        continue;
                    }
                    if (!r_semantic_associated_name(context, member, &associated_name)) {
                        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
                        return false;
                    }
                    for (previous = 0U; previous < trait.associated_count; ++previous) {
                        if ((context->semantic_associated_types[trait.first_associated + previous]
                                 .name_intern_id == associated_name.intern_id) &&
                            !r_semantic_add_diagnostic(context,
                                                       "R-DIAG-NAME-002",
                                                       "R-TYPE-0045",
                                                       "duplicate associated type in one trait",
                                                       associated_name.span)) {
                            return false;
                        }
                    }
                    (void)memset(&associated_parameter, 0, sizeof(associated_parameter));
                    (void)memset(&associated, 0, sizeof(associated));
                    associated_parameter.name_span = associated_name.span;
                    associated_parameter.name_intern_id = associated_name.intern_id;
                    if ((context->generic_parameter_count >= UINT32_MAX) ||
                        !r_grow_array(context,
                                      (void **)&context->generic_parameters,
                                      &context->generic_parameter_capacity,
                                      sizeof(associated_parameter),
                                      context->generic_parameter_count + 1U) ||
                        !r_semantic_intern_type(context,
                                                R_SEMANTIC_TYPE_PARAMETER,
                                                (RTypeId)context->generic_parameter_count + 1U,
                                                &associated_parameter.type) ||
                        !r_grow_array(context,
                                      (void **)&context->semantic_associated_types,
                                      &context->semantic_associated_type_capacity,
                                      sizeof(associated),
                                      context->semantic_associated_type_count + 1U)) {
                        return false;
                    }
                    context->generic_parameters[context->generic_parameter_count++] =
                        associated_parameter;
                    schema.parameter_count += UINT32_C(1);
                    associated.name_span = associated_name.span;
                    associated.name_intern_id = associated_name.intern_id;
                    associated.parameter_type = associated_parameter.type;
                    associated.parameter_schema = r_generic_associated_schema(context, member);
                    context->semantic_associated_types[context->semantic_associated_type_count++] =
                        associated;
                    trait.associated_count += UINT32_C(1);
                }
            }
            /* R-TYPE-0050: associated constants with an optional default. */
            trait.first_constant = (uint32_t)context->semantic_associated_constant_count;
            if (!r_semantic_collect_trait_constants(context, declaration, &trait)) {
                return false;
            }
            if (!r_grow_array(context,
                              (void **)&context->generic_schemas,
                              &context->generic_schema_capacity,
                              sizeof(schema),
                              context->generic_schema_count + 1U)) {
                return false;
            }
            context->generic_schemas[context->generic_schema_count++] = schema;
            trait.span = declaration_view.span;
            trait.name_span = name.span;
            trait.name_intern_id = name.intern_id;
            trait.module_source = source_id;
            trait.declaration_ast = declaration.node;
            trait.generic_schema = (uint32_t)context->generic_schema_count;
            trait.self_type = parameter.type;
            trait.is_protected = r_ast_has_direct_token(context, external, R_TOKEN_KW_PROTECTED);
            if (!r_grow_array(context,
                              (void **)&context->semantic_traits,
                              &context->semantic_trait_capacity,
                              sizeof(trait),
                              context->semantic_trait_count + 1U)) {
                return false;
            }
            context->semantic_traits[context->semantic_trait_count++] = trait;
            uint32_t self_constraint = (uint32_t)context->semantic_trait_count;
            if (trait.type_parameter_schema != 0U) {
                RTypeVector arguments = {0};
                const RGenericSchema parameters =
                    context->generic_schemas[trait.type_parameter_schema - 1U];
                bool applied = true;
                for (uint32_t index = 0U; applied && index < parameters.parameter_count; ++index)
                    applied = r_type_vector_push(
                        context,
                        &arguments,
                        context->generic_parameters[parameters.first_parameter + index].type);
                applied = applied &&
                          r_semantic_apply_trait(
                              context, self_constraint, &arguments, trait.span, &self_constraint);
                r_context_free(context, arguments.items);
                if (!applied)
                    return false;
            }
            if (!r_semantic_append_trait_constraint(
                    context, schema.first_parameter, self_constraint))
                return false;
        }
    }
    return true;
}

/* R-TYPE-0043: `m.n::Name` names the trait `Name` declared by module `m.n`, which is the
 * current module or an imported one. */
static uint32_t r_semantic_resolve_trait_name_in_module(const RFrontendContext *context,
                                                        RSourceId source,
                                                        uint32_t module_intern_id,
                                                        uint32_t name_intern_id) {
    const char *module_bytes;
    size_t module_length;
    size_t index;

    if (!r_semantic_intern_text(context, module_intern_id, &module_bytes, &module_length)) {
        return UINT32_C(0);
    }
    for (index = 0U; index < context->semantic_trait_count; ++index) {
        const RSemanticTrait *trait = &context->semantic_traits[index];
        const RSource *declaration = r_get_source_const(context, trait->module_source);
        bool visible = trait->module_source == source;
        size_t import_index;
        if ((trait->name_intern_id != name_intern_id) || trait->is_core ||
            trait->generic_origin != 0U || (declaration == NULL) ||
            (declaration->module_name == NULL) ||
            !r_semantic_bytes_equal_text(module_bytes, module_length, declaration->module_name)) {
            continue;
        }
        for (import_index = 0U; !visible && (import_index < context->import_count);
             ++import_index) {
            const RImportEntry *import = &context->imports[import_index];
            visible = (import->source == source) && !import->imports_name &&
                      (strcmp(import->module_name, declaration->module_name) == 0);
        }
        if (!visible || (trait->is_protected && (trait->module_source != source))) {
            continue;
        }
        return (uint32_t)index + UINT32_C(1);
    }
    return UINT32_C(0);
}

#include "trait_applications.inc"
#include "trait_bounds.inc"

static bool r_generic_resolve_trait_constraints(RFrontendContext *context) {
    if (!r_semantic_collect_trait_bounds(context))
        return false;
    size_t index;
    for (index = 0U; index < context->generic_pending_constraint_count; ++index) {
        const RGenericPendingConstraint pending = context->generic_pending_constraints[index];
        if (pending.application.node != 0U) {
            uint32_t applied;
            if (!r_semantic_trait_from_ast(context, pending.application, &applied) ||
                (applied != 0U &&
                 !(pending.fields
                       ? r_semantic_append_field_trait(context, pending.parameter, applied)
                       : r_semantic_append_trait_constraint(context, pending.parameter, applied))))
                return false;
            continue;
        }
        bool ambiguous = false;
        const uint32_t trait =
            pending.is_core ? r_semantic_resolve_core_trait_name(context, pending.name_intern_id)
            : (pending.module_intern_id != UINT32_C(0))
                ? r_semantic_resolve_trait_name_in_module(context,
                                                          pending.span.source,
                                                          pending.module_intern_id,
                                                          pending.name_intern_id)
                : r_semantic_resolve_trait_name(
                      context, pending.span.source, pending.name_intern_id, &ambiguous);
        if ((trait == UINT32_C(0)) || ambiguous) {
            if (!r_semantic_add_diagnostic(
                    context,
                    "R-DIAG-TYPE-001",
                    "R-TYPE-0043",
                    ambiguous ? "trait constraint name is ambiguous"
                    : (pending.is_core || pending.module_intern_id != UINT32_C(0))
                        ? "unknown generic constraint"
                        : "unknown generic constraint; an imported "
                          "module's trait is written module::Trait or "
                          "imported by name",
                    pending.span)) {
                return false;
            }
            continue;
        }
        if (context->semantic_traits[trait - 1U].type_parameter_schema != 0U) {
            if (!r_semantic_add_diagnostic(context,
                                           "R-DIAG-TRAIT-001",
                                           "R-TYPE-0043",
                                           "a generic trait requires explicit type arguments",
                                           pending.span))
                return false;
            continue;
        }
        if (!r_semantic_append_trait_constraint(context, pending.parameter, trait)) {
            return false;
        }
    }
    for (uint32_t id = 1U; id <= context->semantic_trait_count; ++id) {
        const RSemanticTrait trait = context->semantic_traits[id - 1U];
        if (trait.generic_origin != 0U || trait.type_parameter_schema == 0U)
            continue;
        const RGenericSchema parameters =
            context->generic_schemas[trait.type_parameter_schema - 1U];
        const RGenericSchema schema = context->generic_schemas[trait.generic_schema - 1U];
        for (uint32_t parameter = 0U; parameter < parameters.parameter_count; ++parameter)
            context->generic_parameters[schema.first_parameter + 1U + parameter] =
                context->generic_parameters[parameters.first_parameter + parameter];
    }
    return true;
}

/* R-TYPE-0043 (M19): `P::Name: constraints` adds the constraints of its holder to the projection
   `P::Name` of the one trait that P satisfies, directly or through a supertrait, and that
   declares Name. */
static bool r_generic_resolve_projection_constraints(RFrontendContext *context) {
    for (size_t index = 0U; index < context->generic_projection_constraint_count; ++index) {
        RGenericProjectionConstraint *constraint = &context->generic_projection_constraints[index];
        const RGenericSchema schema = context->generic_schemas[constraint->schema - 1U];
        RAstRef associated;
        RAstNodeView associated_view;
        RAstTokenView base_name;
        RAstTokenView member_name;
        if (!r_ast_find_direct_child(
                context, constraint->entry, R_SYNTAX_ASSOCIATED_NAME, &associated) ||
            !r_ast_ref_view(context, associated, &associated_view) ||
            !r_ast_direct_token(context, associated, 0U, NULL, &base_name) ||
            !r_semantic_last_identifier(context, associated, &member_name)) {
            context->resource_status = R_FRONTEND_INTERNAL_ERROR;
            return false;
        }
        uint32_t parameter_index = UINT32_MAX;
        for (uint32_t slot = 0U; slot < schema.parameter_count; ++slot) {
            if (context->generic_parameters[schema.first_parameter + slot].name_intern_id ==
                base_name.intern_id) {
                parameter_index = schema.first_parameter + slot;
            }
        }
        if (parameter_index == UINT32_MAX) {
            if (!r_semantic_add_diagnostic(context,
                                           "R-DIAG-NAME-001",
                                           "R-TYPE-0043",
                                           "an associated type constraint shall name a "
                                           "parameter of its header",
                                           base_name.span)) {
                return false;
            }
            continue;
        }
        RTypeId projection = R_TYPE_ID_INVALID;
        bool ambiguous = false;
        if (!r_semantic_parameter_projection(context,
                                             context->generic_parameters[parameter_index].type,
                                             member_name.intern_id,
                                             &projection,
                                             &ambiguous)) {
            if (context->resource_status != R_FRONTEND_OK) {
                return false;
            }
            if (!r_semantic_add_diagnostic(context,
                                           "R-DIAG-TRAIT-001",
                                           "R-TYPE-0043",
                                           ambiguous ? "more than one trait constraint of the "
                                                       "parameter declares this associated type"
                                                     : "no trait constraint of the parameter "
                                                       "declares this associated type",
                                           member_name.span)) {
                return false;
            }
            continue;
        }
        const RSemanticType *type = r_semantic_type(context, projection);
        if ((type == NULL) || (type->kind != R_SEMANTIC_TYPE_PARAMETER) || (type->base == 0U)) {
            context->resource_status = R_FRONTEND_INTERNAL_ERROR;
            return false;
        }
        const uint32_t target = type->base - 1U;
        const RGenericParameter holder = context->generic_parameters[constraint->holder];
        context->generic_parameters[target].constraints |= holder.constraints;
        for (uint32_t bound = 0U; bound < holder.trait_constraint_count; ++bound) {
            if (!r_semantic_append_trait_constraint(
                    context,
                    target,
                    context->generic_trait_constraints[holder.first_trait_constraint + bound])) {
                return false;
            }
        }
        constraint->projection = projection;
    }
    return true;
}

/* R-TYPE-0045: `type Name = T;` binds one associated type of the implemented trait; every
 * associated type is bound exactly once. Self within a binding is the target type. */
/* R-TYPE-0050: an implementation's value for one associated constant of its trait. */
static bool
r_semantic_bind_impl_constant(RFrontendContext *context, uint32_t impl_id, RAstRef member) {
    const uint32_t trait = context->semantic_impls[impl_id - 1U].trait;
    RAstTokenView name;
    RTypeId type;
    RAstRef initializer;
    RAstRef *slot;
    uint32_t index;
    if (!r_semantic_associated_constant_parts(context, member, &name, &type, &initializer)) {
        return false;
    }
    index = r_semantic_constant_index(context, trait, name.intern_id);
    if (index == UINT32_MAX) {
        return r_semantic_add_diagnostic(context,
                                         "R-DIAG-TRAIT-001",
                                         "R-TYPE-0050",
                                         "the trait declares no associated constant of this name",
                                         name.span);
    }
    if (context
            ->semantic_associated_constants[context->semantic_traits[trait - 1U].first_constant +
                                            index]
            .type != type) {
        return r_semantic_add_diagnostic(context,
                                         "R-DIAG-TRAIT-001",
                                         "R-TYPE-0050",
                                         "an associated constant keeps the type its trait declares",
                                         name.span);
    }
    slot = &context->semantic_constant_bindings
                [context->semantic_impls[impl_id - 1U].first_constant_binding + index];
    if (slot->node != R_AST_NODE_ID_INVALID) {
        return r_semantic_add_diagnostic(context,
                                         "R-DIAG-NAME-002",
                                         "R-TYPE-0050",
                                         "an associated constant is bound once per implementation",
                                         name.span);
    }
    *slot = initializer;
    return true;
}

static bool
r_semantic_collect_impl_bindings(RFrontendContext *context, uint32_t impl_id, RAstRef declaration) {
    RAstNodeView declaration_view;
    uint32_t member_index;
    uint32_t slot;
    const uint32_t trait = context->semantic_impls[impl_id - 1U].trait;
    const uint32_t first_binding = context->semantic_impls[impl_id - 1U].first_binding;
    const RSemanticTrait record = context->semantic_traits[trait - 1U];

    if (!r_ast_ref_view(context, declaration, &declaration_view)) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    for (member_index = 0U; member_index < declaration_view.child_count; ++member_index) {
        RAstRef member;
        RAstNodeView member_view;
        RAstTokenView name;
        RAstRef type_ref;
        RTypeId bound;
        uint32_t index;
        if (!r_ast_ref_child(context, declaration, member_index, &member) ||
            !r_ast_ref_view(context, member, &member_view)) {
            continue;
        }
        if (member_view.kind == R_SYNTAX_ASSOCIATED_CONSTANT) {
            if (!r_semantic_bind_impl_constant(context, impl_id, member)) {
                return false;
            }
            continue;
        }
        if (member_view.kind != R_SYNTAX_ASSOCIATED_TYPE) {
            continue;
        }
        if (!r_semantic_associated_name(context, member, &name) ||
            !r_ast_find_direct_child(context, member, R_SYNTAX_TYPE, &type_ref)) {
            context->resource_status = R_FRONTEND_INTERNAL_ERROR;
            return false;
        }
        index = r_semantic_associated_index(context, trait, name.intern_id);
        if (index == UINT32_MAX) {
            if (!r_semantic_add_diagnostic(context,
                                           "R-DIAG-TRAIT-001",
                                           "R-TYPE-0045",
                                           "the trait declares no associated type of this name",
                                           name.span)) {
                return false;
            }
            continue;
        }
        if (context->semantic_associated_bindings[first_binding + index] != R_TYPE_ID_INVALID) {
            if (!r_semantic_add_diagnostic(context,
                                           "R-DIAG-NAME-002",
                                           "R-TYPE-0045",
                                           "an associated type is bound once per implementation",
                                           name.span)) {
                return false;
            }
            continue;
        }
        /* R-TYPE-0045 (L16.3): a binding names the parameters its associated type declares;
           they carry the declared constraints. */
        const uint32_t declared_schema =
            context->semantic_associated_types[record.first_associated + index].parameter_schema;
        const uint32_t binding_schema = r_generic_associated_schema(context, member);
        const uint32_t declared_count =
            declared_schema == 0U ? 0U
                                  : context->generic_schemas[declared_schema - 1U].parameter_count;
        const uint32_t binding_count =
            binding_schema == 0U ? 0U
                                 : context->generic_schemas[binding_schema - 1U].parameter_count;
        if (declared_count != binding_count) {
            if (!r_semantic_add_diagnostic(context,
                                           "R-DIAG-TRAIT-001",
                                           "R-TYPE-0045",
                                           "an implementation binds an associated type with the "
                                           "parameters its trait declares",
                                           name.span)) {
                return false;
            }
            context->semantic_impls[impl_id - 1U].poisoned = true;
            continue;
        }
        for (uint32_t parameter = 0U; parameter < binding_count; ++parameter) {
            const RGenericParameter declared =
                context->generic_parameters[context->generic_schemas[declared_schema - 1U]
                                                .first_parameter +
                                            parameter];
            RGenericParameter *binding =
                &context->generic_parameters[context->generic_schemas[binding_schema - 1U]
                                                 .first_parameter +
                                             parameter];
            binding->constraints = declared.constraints;
            binding->first_trait_constraint = declared.first_trait_constraint;
            binding->trait_constraint_count = declared.trait_constraint_count;
        }
        if (!r_semantic_type_from_ast(context, type_ref, &bound)) {
            if (context->resource_status != R_FRONTEND_OK) {
                return false;
            }
            if (!r_semantic_add_diagnostic(context,
                                           "R-DIAG-NAME-001",
                                           "R-TYPE-0045",
                                           "unresolved associated type binding",
                                           member_view.span)) {
                return false;
            }
            context->semantic_impls[impl_id - 1U].poisoned = true;
            continue;
        }
        if ((binding_count != 0U) &&
            !r_semantic_intern_derived_type(context,
                                            R_SEMANTIC_TYPE_TYPE_FUNCTION,
                                            bound,
                                            R_TYPE_ID_INVALID,
                                            (uint64_t)binding_schema,
                                            R_SEMANTIC_TYPE_FLAG_NONE,
                                            &bound)) {
            return false;
        }
        context->semantic_associated_bindings[first_binding + index] = bound;
    }
    for (slot = 0U; slot < record.constant_count; ++slot) {
        const RSemanticAssociatedConstant constant =
            context->semantic_associated_constants[record.first_constant + slot];
        if (context->semantic_constant_bindings
                    [context->semantic_impls[impl_id - 1U].first_constant_binding + slot]
                        .node == R_AST_NODE_ID_INVALID &&
            constant.initializer.node == R_AST_NODE_ID_INVALID) {
            if (!r_semantic_add_diagnostic(
                    context,
                    "R-DIAG-TRAIT-001",
                    "R-TYPE-0050",
                    "the implementation binds every associated constant without a default",
                    context->semantic_impls[impl_id - 1U].span)) {
                return false;
            }
            context->semantic_impls[impl_id - 1U].poisoned = true;
            return true;
        }
    }
    for (slot = 0U; slot < record.associated_count; ++slot) {
        if (context->semantic_associated_bindings[first_binding + slot] == R_TYPE_ID_INVALID) {
            if (!r_semantic_add_diagnostic(
                    context,
                    "R-DIAG-TRAIT-001",
                    "R-TYPE-0045",
                    "the implementation binds every associated type the trait declares",
                    context->semantic_impls[impl_id - 1U].span)) {
                return false;
            }
            context->semantic_impls[impl_id - 1U].poisoned = true;
            return true;
        }
    }
    return true;
}

static bool r_semantic_collect_impls(RFrontendContext *context) {
    size_t source_index;

    for (source_index = 0U; source_index < context->source_count; ++source_index) {
        const RSourceId source_id = (RSourceId)(source_index + 1U);
        RAstRef root;
        RAstNodeView root_view;
        uint32_t child_index;
        if (!r_semantic_root_ref(context, source_id, &root) ||
            !r_ast_view_kind(context, root, R_SYNTAX_TRANSLATION_UNIT, &root_view)) {
            context->resource_status = R_FRONTEND_INTERNAL_ERROR;
            return false;
        }
        for (child_index = 0U; child_index < root_view.child_count; ++child_index) {
            RAstRef external, declaration, trait_name_ref, target_ref;
            RAstNodeView declaration_view;
            RAstTokenView trait_name;
            RSemanticImpl impl;
            const RSemanticAggregate *aggregate;
            const RSemanticTrait *trait_record;
            bool ambiguous = false;
            size_t existing;
            bool orphan;
            if (!r_ast_ref_child(context, root, child_index, &external)) {
                context->resource_status = R_FRONTEND_INTERNAL_ERROR;
                return false;
            }
            if (!r_ast_find_direct_child(
                    context, external, R_SYNTAX_IMPL_DECLARATION, &declaration) ||
                !r_ast_ref_view(context, declaration, &declaration_view)) {
                continue;
            }
            (void)memset(&impl, 0, sizeof(impl));
            impl.span = declaration_view.span;
            impl.module_source = source_id;
            impl.declaration_ast = declaration.node;
            impl.generic_schema = r_generic_schema_at(context, declaration_view.span);
            if (!r_ast_find_direct_child(
                    context, declaration, R_SYNTAX_TRAIT_NAME, &trait_name_ref) ||
                !r_semantic_last_identifier(context, trait_name_ref, &trait_name) ||
                !r_ast_find_direct_child(context, declaration, R_SYNTAX_TYPE, &target_ref)) {
                context->resource_status = R_FRONTEND_INTERNAL_ERROR;
                return false;
            }
            const size_t trait_diagnostics = context->diagnostic_count;
            if (!r_semantic_trait_from_ast(context, trait_name_ref, &impl.trait))
                return false;
            if ((impl.trait == UINT32_C(0)) && (context->diagnostic_count != trait_diagnostics)) {
                impl.poisoned = true;
            } else if ((impl.trait == UINT32_C(0)) || ambiguous) {
                if (!r_semantic_add_diagnostic(context,
                                               "R-DIAG-NAME-001",
                                               "R-TYPE-0042",
                                               ambiguous ? "implemented trait name is ambiguous"
                                                         : "unresolved trait name",
                                               trait_name.span)) {
                    return false;
                }
                impl.poisoned = true;
            }
            if (!r_semantic_type_from_ast(context, target_ref, &impl.target_type)) {
                if (context->resource_status != R_FRONTEND_OK) {
                    return false;
                }
                if (!r_semantic_add_diagnostic(context,
                                               "R-DIAG-NAME-001",
                                               "R-TYPE-0042",
                                               "unresolved impl target type",
                                               declaration_view.span)) {
                    return false;
                }
                impl.poisoned = true;
                impl.target_type = R_TYPE_ID_INVALID;
            }
            aggregate =
                impl.poisoned ? NULL : r_semantic_aggregate_for_type(context, impl.target_type);
            if (aggregate != NULL) {
                impl.target_aggregate =
                    aggregate->generic_origin != UINT32_C(0)
                        ? aggregate->generic_origin
                        : (uint32_t)(aggregate - context->semantic_aggregates) + UINT32_C(1);
            }
            trait_record =
                impl.trait != UINT32_C(0) ? &context->semantic_traits[impl.trait - 1U] : NULL;
            /* R-TYPE-0046: a core trait belongs to no module; its implementations live with
               their target type. */
            orphan = (trait_record != NULL) && !trait_record->is_core &&
                     (trait_record->module_source != source_id) &&
                     ((aggregate == NULL) ||
                      (context->semantic_aggregates[impl.target_aggregate - 1U].module_source !=
                       source_id));
            /* R-TYPE-0046 (L32): a tuple is formatted by standard formatting, so core::Format
               has no implementation for it. */
            if ((trait_record != NULL) && trait_record->is_core &&
                ((aggregate == NULL) ||
                 ((impl.trait == context->core_format_trait) && aggregate->is_tuple))) {
                if (!r_semantic_add_diagnostic(
                        context,
                        "R-DIAG-TRAIT-001",
                        "R-TYPE-0046",
                        "a core trait is implemented for a nominal type of the implementing module",
                        declaration_view.span)) {
                    return false;
                }
                impl.poisoned = true;
            }
            if (orphan) {
                if (!r_semantic_add_diagnostic(context,
                                               "R-DIAG-TRAIT-001",
                                               "R-TYPE-0042",
                                               "an implementation belongs to the module of its "
                                               "trait or of its target type",
                                               declaration_view.span)) {
                    return false;
                }
                impl.poisoned = true;
            }
            for (existing = 0U; !impl.poisoned && (existing < context->semantic_impl_count);
                 ++existing) {
                const RSemanticImpl *other = &context->semantic_impls[existing];
                if ((other->trait == impl.trait) && (other->target_type == impl.target_type) &&
                    !r_semantic_add_diagnostic(context,
                                               "R-DIAG-TRAIT-001",
                                               "R-TYPE-0042",
                                               "duplicate implementation of one trait for one type",
                                               declaration_view.span)) {
                    return false;
                }
            }
            /* R-TYPE-0050: one binding slot per associated constant of the trait. */
            impl.first_constant_binding = (uint32_t)context->semantic_constant_binding_count;
            if (trait_record != NULL) {
                uint32_t slot;
                const uint32_t constants = trait_record->constant_count;
                if (!r_grow_array(context,
                                  (void **)&context->semantic_constant_bindings,
                                  &context->semantic_constant_binding_capacity,
                                  sizeof(*context->semantic_constant_bindings),
                                  context->semantic_constant_binding_count + constants)) {
                    return false;
                }
                trait_record = &context->semantic_traits[impl.trait - 1U];
                for (slot = 0U; slot < constants; ++slot) {
                    context->semantic_constant_bindings[context->semantic_constant_binding_count]
                        .source = source_id;
                    context->semantic_constant_bindings[context->semantic_constant_binding_count++]
                        .node = R_AST_NODE_ID_INVALID;
                }
            }
            /* R-TYPE-0045: one binding slot per associated type of the trait. */
            impl.first_binding = (uint32_t)context->semantic_associated_binding_count;
            if (trait_record != NULL) {
                uint32_t slot;
                if (!r_grow_array(context,
                                  (void **)&context->semantic_associated_bindings,
                                  &context->semantic_associated_binding_capacity,
                                  sizeof(*context->semantic_associated_bindings),
                                  context->semantic_associated_binding_count +
                                      trait_record->associated_count)) {
                    return false;
                }
                for (slot = 0U; slot < trait_record->associated_count; ++slot) {
                    context->semantic_associated_bindings
                        [context->semantic_associated_binding_count++] = R_TYPE_ID_INVALID;
                }
            }
            if (!r_grow_array(context,
                              (void **)&context->semantic_impls,
                              &context->semantic_impl_capacity,
                              sizeof(impl),
                              context->semantic_impl_count + 1U)) {
                return false;
            }
            context->semantic_impls[context->semantic_impl_count++] = impl;
            if ((trait_record != NULL) && !impl.poisoned &&
                !r_semantic_collect_impl_bindings(
                    context, (uint32_t)context->semantic_impl_count, declaration)) {
                return false;
            }
        }
    }
    context->impls_ready = true;
    return true;
}

/* The method namespace of a nominal owner is spelled through the schema origin name, so that
 * inherent and implementation methods of one type share one flat namespace (R-NAME-0010). */
static bool r_generic_method_symbol_name(RFrontendContext *context,
                                         RTypeId owner_type,
                                         uint32_t method_name_intern_id,
                                         uint32_t *name_intern_id) {
    RGenericText text = {context, NULL, 0U, 0U, false};
    const RSemanticAggregate *aggregate = r_semantic_aggregate_for_type(context, owner_type);
    const RInternEntry *method_name = &context->intern_entries[method_name_intern_id - 1U];
    char owner_label[32];
    bool success;

    if (aggregate != NULL) {
        const RSemanticAggregate *origin =
            aggregate->generic_origin != UINT32_C(0)
                ? &context->semantic_aggregates[aggregate->generic_origin - 1U]
                : aggregate;
        const RInternEntry *base = &context->intern_entries[origin->name_intern_id - 1U];
        success = r_generic_text(&text, base->bytes, base->length);
    } else {
        /* A trait prototype is named after its trait, whose Self parameter owns it. */
        const RSemanticTrait *owner = NULL;
        for (size_t trait = 0U; owner == NULL && trait < context->semantic_trait_count; ++trait)
            if (context->semantic_traits[trait].self_type == owner_type &&
                context->semantic_traits[trait].generic_origin == 0U)
                owner = &context->semantic_traits[trait];
        if (owner != NULL) {
            const RInternEntry *base = &context->intern_entries[owner->name_intern_id - 1U];
            success = r_generic_text(&text, base->bytes, base->length);
        } else {
            const int written =
                snprintf(owner_label, sizeof(owner_label), "#%u", (unsigned)owner_type);
            success = (written > 0) && ((size_t)written < sizeof(owner_label)) &&
                      r_generic_text(&text, owner_label, (size_t)written);
        }
    }
    success = success && r_generic_text(&text, "::", 2U) &&
              r_generic_text(&text, method_name->bytes, method_name->length) &&
              r_intern_bytes(context, text.bytes, text.length, name_intern_id);
    r_context_free(context, text.bytes);
    return success;
}

static bool r_generic_applied_method_name(RFrontendContext *context,
                                          uint32_t trait_id,
                                          uint32_t *name_intern_id) {
    const RSemanticTrait trait = context->semantic_traits[trait_id - 1U];
    if (trait.generic_origin == 0U)
        return true;
    const RSource *source = r_get_source_const(context, trait.module_source);
    const RInternEntry method = context->intern_entries[*name_intern_id - 1U];
    const RInternEntry name = context->intern_entries[trait.name_intern_id - 1U];
    RGenericText text = {context, NULL, 0U, 0U, false};
    uint32_t base;
    bool valid =
        source != NULL && source->module_name != NULL &&
        r_generic_text(&text, method.bytes, method.length) && r_generic_text(&text, "@", 1U) &&
        r_generic_text(&text, source->module_name, strlen(source->module_name)) &&
        r_generic_text(&text, "::", 2U) && r_generic_text(&text, name.bytes, name.length) &&
        r_intern_bytes(context, text.bytes, text.length, &base);
    r_context_free(context, text.bytes);
    RTypeVector arguments = {0};
    const uint32_t count =
        context->generic_schemas[trait.type_parameter_schema - 1U].parameter_count;
    for (uint32_t index = 0U; valid && index < count; ++index)
        valid = r_type_vector_push(
            context, &arguments, context->generic_arguments[trait.first_generic_argument + index]);
    valid = valid && r_generic_instance_name(context, base, &arguments, name_intern_id);
    r_context_free(context, arguments.items);
    return valid;
}

static bool r_semantic_owner_member_name_taken(const RFrontendContext *context,
                                               RTypeId owner_type,
                                               uint32_t name_intern_id) {
    const RSemanticAggregate *aggregate = r_semantic_aggregate_for_type(context, owner_type);
    uint32_t index;
    if (aggregate == NULL) {
        return false;
    }
    for (index = 0U; index < aggregate->field_count; ++index) {
        if (context->semantic_fields[aggregate->first_field + index].name_intern_id ==
            name_intern_id) {
            return true;
        }
    }
    for (index = 0U; index < aggregate->variant_count; ++index) {
        if (context->semantic_variants[aggregate->first_variant + index].name_intern_id ==
            name_intern_id) {
            return true;
        }
    }
    return false;
}

/* R-FUNC-0013: a receiver is the first parameter, named `this`, with one of exactly three
 * forms of the owner type. */
static bool r_generic_receiver_kind(const RFrontendContext *context,
                                    RAstRef parameter_list,
                                    const RTypeVector *parameters,
                                    RTypeId owner_type,
                                    uint32_t *receiver_kind,
                                    bool *has_receiver) {
    RAstNodeView list_view;
    RAstRef parameter;
    RAstNodeView parameter_view;
    RAstTokenView name;
    const RSemanticType *type;
    uint32_t index;

    *receiver_kind = R_SEMANTIC_RECEIVER_NONE;
    *has_receiver = false;
    if (!r_ast_view_kind(context, parameter_list, R_SYNTAX_PARAMETER_LIST, &list_view)) {
        return false;
    }
    for (index = 0U; index < list_view.child_count; ++index) {
        if (r_ast_ref_child(context, parameter_list, index, &parameter) &&
            r_ast_ref_view(context, parameter, &parameter_view) &&
            (parameter_view.kind == R_SYNTAX_PARAMETER)) {
            break;
        }
    }
    if ((index >= list_view.child_count) || (parameters->count == 0U) ||
        !r_semantic_parameter_name(context, parameter, &name) || (name.kind != R_TOKEN_KW_THIS)) {
        return true;
    }
    *has_receiver = true;
    if (parameters->items[0] == owner_type) {
        *receiver_kind = R_SEMANTIC_RECEIVER_VALUE;
        return true;
    }
    type = r_semantic_type(context, parameters->items[0]);
    if ((type != NULL) && (type->kind == R_SEMANTIC_TYPE_BORROW) && (type->base == owner_type)) {
        if (type->flags == R_SEMANTIC_TYPE_FLAG_SHARED) {
            *receiver_kind = R_SEMANTIC_RECEIVER_SHARED;
            return true;
        }
        if (type->flags == R_SEMANTIC_TYPE_FLAG_NONE) {
            *receiver_kind = R_SEMANTIC_RECEIVER_EXCLUSIVE;
            return true;
        }
    }
    return true;
}

/* R-TYPE-0041: a prototype has no body to analyze, so a result that may borrow is mapped to
 * every borrowing parameter; exactly one such parameter gives the exact mapping. */
static void r_semantic_prototype_borrow_mapping(const RFrontendContext *context,
                                                RSemanticSymbol *prototype,
                                                const RTypeId *parameter_types,
                                                uint32_t parameter_count) {
    uint32_t index;
    uint32_t count = UINT32_C(0);
    uint32_t only = UINT32_C(0);
    uint64_t low = UINT64_C(0);
    uint64_t high = UINT64_C(0);

    if (!r_semantic_type_is_direct_shared_borrow_output(context, prototype->return_type) &&
        !r_semantic_type_contains_borrow(context, prototype->return_type) &&
        !r_semantic_type_contains_slice(context, prototype->return_type) &&
        !r_generic_type_is_dependent(context, prototype->return_type, 0U)) {
        return;
    }
    for (index = 0U; (index < parameter_count) && (index < UINT32_C(128)); ++index) {
        const RTypeId parameter = parameter_types[index];
        const RSemanticTypeKind kind = r_semantic_value_kind(context, parameter);
        if ((kind == R_SEMANTIC_TYPE_BORROW) || (kind == R_SEMANTIC_TYPE_SLICE) ||
            (kind == R_SEMANTIC_TYPE_STR) || r_semantic_type_contains_borrow(context, parameter) ||
            r_semantic_type_contains_slice(context, parameter) ||
            r_generic_type_is_dependent(context, parameter, 0U)) {
            count += UINT32_C(1);
            only = index + UINT32_C(1);
            if (index < 64U)
                low |= UINT64_C(1) << index;
            else
                high |= UINT64_C(1) << (index - 64U);
        }
    }
    prototype->return_borrow_parameter = count == UINT32_C(1) ? only : UINT32_MAX;
    prototype->return_borrow_parameters_low = low;
    prototype->return_borrow_parameters_high = high;
}

static bool r_generic_prepare_method(RFrontendContext *context,
                                     RAstRef parameter_list,
                                     const RTypeVector *parameters,
                                     RSemanticSymbol *symbol,
                                     RTypeId owner_type,
                                     uint32_t trait,
                                     uint32_t impl,
                                     RSourceSpan span) {
    const RSemanticAggregate *aggregate = r_semantic_aggregate_for_type(context, owner_type);
    uint32_t receiver_kind = R_SEMANTIC_RECEIVER_NONE;
    bool has_receiver = false;

    if (owner_type == R_TYPE_ID_INVALID) {
        return r_semantic_add_diagnostic(context,
                                         "R-DIAG-TYPE-001",
                                         "R-FUNC-0013",
                                         "a method requires a resolved owner type",
                                         span);
    }
    if ((aggregate != NULL) && !aggregate->complete) {
        return r_semantic_add_diagnostic(context,
                                         "R-DIAG-TYPE-001",
                                         "R-FUNC-0013",
                                         "a method requires a complete nominal owner type",
                                         span);
    }
    if ((trait == UINT32_C(0)) && (impl == UINT32_C(0)) && (aggregate != NULL) &&
        (aggregate->module_source != symbol->module_source)) {
        return r_semantic_add_diagnostic(context,
                                         "R-DIAG-TYPE-001",
                                         "R-FUNC-0013",
                                         "an inherent method belongs to the module of its type",
                                         span);
    }
    if (symbol->is_extern_c) {
        return r_semantic_add_diagnostic(
            context, "R-DIAG-FFI-003", "R-FUNC-0013", "a method shall not have C linkage", span);
    }
    if (!r_generic_receiver_kind(
            context, parameter_list, parameters, owner_type, &receiver_kind, &has_receiver)) {
        return false;
    }
    if (has_receiver && (receiver_kind == R_SEMANTIC_RECEIVER_NONE)) {
        return r_semantic_add_diagnostic(
            context,
            "R-DIAG-TYPE-001",
            "R-FUNC-0013",
            "the receiver shall have type const Owner*, Owner* or Owner",
            span);
    }
    if (!has_receiver && (trait != UINT32_C(0) || impl != UINT32_C(0))) {
        return r_semantic_add_diagnostic(context,
                                         "R-DIAG-TYPE-001",
                                         "R-TYPE-0041",
                                         "a trait member requires the receiver parameter this",
                                         span);
    }
    if (r_semantic_owner_member_name_taken(context, owner_type, symbol->name_intern_id)) {
        return r_semantic_add_diagnostic(context,
                                         "R-DIAG-NAME-002",
                                         "R-NAME-0010",
                                         "a method name shall differ from every field and variant "
                                         "name of its owner",
                                         span);
    }
    /* R-NAME-0010: a method of an implementation may repeat a registered standard method of its
       target; the receiver form keeps naming the standard method, and the implementation's method
       is reached through a constraint or an interface. */
    for (size_t index = 0U;
         impl == UINT32_C(0) && index < sizeof(r_standard_methods) / sizeof(r_standard_methods[0]);
         ++index) {
        const RStandardMethodDescriptor *method = &r_standard_methods[index];
        const RInternEntry *name = &context->intern_entries[symbol->name_intern_id - 1U];
        if (r_standard_method_owner(context, r_semantic_type(context, owner_type), method->owner) &&
            name->length == strlen(method->name) &&
            memcmp(name->bytes, method->name, name->length) == 0) {
            return r_semantic_add_diagnostic(
                context,
                "R-DIAG-NAME-002",
                "R-NAME-0010",
                "a method name conflicts with a registered standard method",
                span);
        }
    }
    symbol->is_method = true;
    symbol->receiver_kind = receiver_kind;
    symbol->trait = trait;
    symbol->impl = impl;
    symbol->is_trait_prototype = (trait != UINT32_C(0)) && (impl == UINT32_C(0));
    symbol->associated_type = owner_type;
    symbol->method_name_intern_id = symbol->name_intern_id;
    if (symbol->is_trait_prototype) {
        r_semantic_prototype_borrow_mapping(
            context, symbol, parameters->items, (uint32_t)parameters->count);
    }
    if (aggregate != NULL) {
        symbol->is_protected = symbol->is_protected || aggregate->is_protected;
    }
    return r_generic_method_symbol_name(
               context, owner_type, symbol->method_name_intern_id, &symbol->name_intern_id) &&
           (impl == 0U || r_generic_applied_method_name(context, trait, &symbol->name_intern_id));
}

/* A trait or implementation member is a method of the enclosing declaration. */
static bool r_generic_prepare_member_method(RFrontendContext *context,
                                            RAstRef parameter_list,
                                            const RTypeVector *parameters,
                                            RSemanticSymbol *symbol) {
    const uint32_t trait = r_semantic_trait_at(context, symbol->name_span);
    const uint32_t impl = r_semantic_impl_at(context, symbol->name_span);

    if (trait != UINT32_C(0)) {
        const RSemanticTrait *record = &context->semantic_traits[trait - 1U];
        symbol->is_protected = record->is_protected;
        return r_generic_prepare_method(context,
                                        parameter_list,
                                        parameters,
                                        symbol,
                                        record->self_type,
                                        trait,
                                        UINT32_C(0),
                                        symbol->name_span);
    }
    if (impl != UINT32_C(0)) {
        const RSemanticImpl *record = &context->semantic_impls[impl - 1U];
        if (record->poisoned) {
            symbol->poisoned = true;
            return true;
        }
        return r_generic_prepare_method(context,
                                        parameter_list,
                                        parameters,
                                        symbol,
                                        record->target_type,
                                        record->trait,
                                        impl,
                                        symbol->name_span);
    }
    return true;
}

static bool r_semantic_register_method(RFrontendContext *context, RSymbolId id) {
    const RSemanticSymbol *symbol = &context->semantic_symbols[id - 1U];
    RSemanticMethod method;
    size_t index;

    (void)memset(&method, 0, sizeof(method));
    r_semantic_canonical_owner(
        context, symbol->associated_type, &method.owner_type, &method.owner_aggregate);
    method.name_intern_id = symbol->method_name_intern_id;
    method.function = id;
    method.trait = symbol->trait;
    method.impl = symbol->impl;
    for (index = 0U; index < context->semantic_method_count; ++index) {
        const RSemanticMethod *other = &context->semantic_methods[index];
        if ((other->owner_type == method.owner_type) &&
            (other->owner_aggregate == method.owner_aggregate) &&
            (other->name_intern_id == method.name_intern_id) && (other->trait == method.trait) &&
            !(symbol->is_overloaded &&
              context->semantic_symbols[other->function - 1U].is_overloaded) &&
            !r_semantic_add_diagnostic(context,
                                       "R-DIAG-NAME-002",
                                       "R-NAME-0010",
                                       "duplicate method in the namespace of its owner type",
                                       symbol->name_span)) {
            return false;
        }
    }
    if (!r_grow_array(context,
                      (void **)&context->semantic_methods,
                      &context->semantic_method_capacity,
                      sizeof(method),
                      context->semantic_method_count + 1U)) {
        return false;
    }
    context->semantic_methods[context->semantic_method_count++] = method;
    return true;
}

/* Select an implementation by the prototype's substituted parameter signature.
 * Return type and effects are validated afterwards, never used as overload keys. */
static bool r_semantic_impl_overload(RFrontendContext *context,
                                     const RSemanticSymbol *prototype,
                                     RTypeId target,
                                     uint32_t first_argument,
                                     const RSemanticMethod **result) {
    const RSemanticSymbol prototype_snapshot = *prototype;
    prototype = &prototype_snapshot;
    RTypeId canonical;
    uint32_t aggregate;
    r_semantic_canonical_owner(context, target, &canonical, &aggregate);
    *result = NULL;
    for (size_t index = 0U; index < context->semantic_method_count; ++index) {
        const RSemanticMethod *method = &context->semantic_methods[index];
        if (method->impl == 0U || method->name_intern_id != prototype->method_name_intern_id ||
            method->owner_type != canonical || method->owner_aggregate != aggregate) {
            continue;
        }
        uint32_t effective;
        if (!r_semantic_impl_trait(context, method->impl, target, &effective))
            return false;
        if (effective != prototype->trait)
            continue;
        const RSemanticSymbol candidate = context->semantic_symbols[method->function - 1U];
        uint32_t owner_arguments = 0U;
        bool substitute_owner = false;
        if (method->is_default) {
            owner_arguments = first_argument;
            substitute_owner = true;
        } else if (candidate.generic_schema != 0U && candidate.generic_origin == 0U) {
            const RSemanticAggregate *owner = r_semantic_aggregate_for_type(context, target);
            if (owner != NULL && owner->generic_origin != 0U &&
                !r_generic_type_is_dependent(context, target, 0U)) {
                owner_arguments = owner->first_generic_argument;
                substitute_owner = true;
            }
        }
        bool match = candidate.parameter_count == prototype->parameter_count;
        for (uint32_t parameter = 0U; match && parameter < prototype->parameter_count;
             ++parameter) {
            RTypeId expected;
            RTypeId actual =
                context->semantic_parameter_types[candidate.first_parameter_type + parameter];
            if (!r_generic_substitute_type(
                    context,
                    context->semantic_parameter_types[prototype->first_parameter_type + parameter],
                    context->semantic_traits[prototype->trait - 1U].generic_schema,
                    first_argument,
                    &expected)) {
                return false;
            }
            if (substitute_owner &&
                !r_generic_substitute_type(
                    context, actual, candidate.generic_schema, owner_arguments, &actual)) {
                return false;
            }
            match = expected == actual;
        }
        if (match) {
            *result = &context->semantic_methods[index];
            return true;
        }
    }
    return true;
}

/* R-TYPE-0042: every prototype of the trait is defined with the signature the trait fixes
 * after substituting Self, and the implementation adds nothing else. */
/* R-TYPE-0045 (L16.1): `@lending` marks a trait method with a borrowed receiver whose result
   mentions an associated type of its trait, or an implementation of one (checked with the
   implementation); no other function lends. */
static bool r_semantic_validate_lending(RFrontendContext *context) {
    for (size_t index = 0U; index < context->semantic_symbol_count; ++index) {
        const RSemanticSymbol *symbol = &context->semantic_symbols[index];

        if ((symbol->kind != R_SEMANTIC_SYMBOL_FUNCTION) || !symbol->is_lending ||
            symbol->is_synthetic || (symbol->generic_origin != R_SYMBOL_ID_INVALID) ||
            ((symbol->impl != 0U) && (symbol->trait != 0U))) {
            continue;
        }
        if (!r_semantic_prototype_may_lend(context, symbol) &&
            !r_semantic_add_diagnostic(context,
                                       "R-DIAG-TRAIT-001",
                                       "R-TYPE-0045",
                                       "@lending marks a trait method with a borrowed receiver "
                                       "whose result mentions an associated type of its trait",
                                       symbol->name_span)) {
            return false;
        }
    }
    return true;
}

/* R-FUNC-0024 (L19): `@chain` marks a synchronous method with a receiver and a result that is
   neither void nor never, including a trait prototype; an implementation inherits it from its
   prototype (checked with the implementation). */
static bool r_semantic_validate_chain(RFrontendContext *context) {
    for (size_t index = 0U; index < context->semantic_symbol_count; ++index) {
        const RSemanticSymbol *symbol = &context->semantic_symbols[index];
        RSemanticTypeKind result;

        if ((symbol->kind != R_SEMANTIC_SYMBOL_FUNCTION) || !symbol->is_chain ||
            symbol->is_synthetic || symbol->poisoned ||
            (symbol->generic_origin != R_SYMBOL_ID_INVALID)) {
            continue;
        }
        result = r_semantic_value_kind(context, symbol->return_type);
        if ((!symbol->is_method || (symbol->receiver_kind == R_SEMANTIC_RECEIVER_NONE)) &&
            !r_semantic_add_diagnostic(context,
                                       "R-DIAG-SYN-002",
                                       "R-FUNC-0024",
                                       "@chain marks a method with a receiver",
                                       symbol->name_span)) {
            return false;
        }
        if (symbol->is_method && (symbol->receiver_kind != R_SEMANTIC_RECEIVER_NONE) &&
            (symbol->is_async || (result == R_SEMANTIC_TYPE_VOID) ||
             (result == R_SEMANTIC_TYPE_NEVER)) &&
            !r_semantic_add_diagnostic(context,
                                       "R-DIAG-SYN-002",
                                       "R-FUNC-0024",
                                       "@chain marks a synchronous method whose result is neither "
                                       "void nor never",
                                       symbol->name_span)) {
            return false;
        }
    }
    return true;
}

static bool r_semantic_validate_impls(RFrontendContext *context) {
    if (!r_semantic_validate_trait_bounds(context))
        return false;
    size_t impl_index;

    for (impl_index = 0U; impl_index < context->semantic_impl_count; ++impl_index) {
        const RSemanticImpl impl = context->semantic_impls[impl_index];
        const RSemanticTrait *trait;
        RTypeVector arguments = {0};
        uint32_t first_argument = UINT32_C(0);
        size_t method_index;
        bool committed;

        if (impl.poisoned || (impl.trait == UINT32_C(0))) {
            continue;
        }
        trait = &context->semantic_traits[impl.trait - 1U];
        if (!r_semantic_trait_arguments(
                context, impl.trait, impl.target_type, (uint32_t)impl_index + 1U, &arguments)) {
            r_context_free(context, arguments.items);
            return false;
        }
        committed = r_generic_commit_arguments(context, &arguments, &first_argument);
        r_context_free(context, arguments.items);
        if (!committed) {
            return false;
        }
        for (method_index = 0U; method_index < context->semantic_method_count; ++method_index) {
            const RSemanticMethod prototype = context->semantic_methods[method_index];
            const RSemanticSymbol *declaration;
            const RSemanticMethod *definition;
            const RSemanticSymbol *body;
            uint32_t parameter;
            bool compatible;

            if ((prototype.trait != r_semantic_trait_origin(context, impl.trait)) ||
                (prototype.impl != UINT32_C(0))) {
                continue;
            }
            declaration = &context->semantic_symbols[prototype.function - 1U];
            definition = r_semantic_find_impl_method(
                context, impl.trait, impl.target_type, prototype.name_intern_id);
            RSemanticSymbol specialized = *declaration;
            specialized.trait = impl.trait;
            if (declaration->is_overloaded &&
                !r_semantic_impl_overload(
                    context, &specialized, impl.target_type, first_argument, &definition)) {
                return false;
            }
            if (definition == NULL) {
                if (declaration->has_definition) {
                    RSemanticMethod inherited = prototype;
                    r_semantic_canonical_owner(context,
                                               impl.target_type,
                                               &inherited.owner_type,
                                               &inherited.owner_aggregate);
                    inherited.impl = (uint32_t)impl_index + 1U;
                    inherited.trait = impl.trait;
                    inherited.is_default = true;
                    if (!r_grow_array(context,
                                      (void **)&context->semantic_methods,
                                      &context->semantic_method_capacity,
                                      sizeof(inherited),
                                      context->semantic_method_count + 1U))
                        return false;
                    context->semantic_methods[context->semantic_method_count++] = inherited;
                    continue;
                }
                if (!r_semantic_add_diagnostic(context,
                                               "R-DIAG-TRAIT-001",
                                               "R-TYPE-0042",
                                               "the implementation does not define every method "
                                               "the trait declares",
                                               impl.span)) {
                    return false;
                }
                continue;
            }
            body = &context->semantic_symbols[definition->function - 1U];
            /* An implementation inherits the promises its trait exposes, even when
               the definition does not repeat their attributes. Its body is checked. */
            context->semantic_symbols[definition->function - 1U].is_noalloc |=
                declaration->is_noalloc;
            context->semantic_symbols[definition->function - 1U].is_nonblocking |=
                declaration->is_nonblocking;
            context->semantic_symbols[definition->function - 1U].is_must_use |=
                declaration->is_must_use;
            context->semantic_symbols[definition->function - 1U].is_discardable |=
                declaration->is_discardable;
            if (context->semantic_symbols[definition->function - 1U].is_lending &&
                !declaration->is_lending &&
                !r_semantic_add_diagnostic(context,
                                           "R-DIAG-TRAIT-001",
                                           "R-TYPE-0045",
                                           "@lending marks an implementation only of a lending "
                                           "trait method",
                                           impl.span)) {
                return false;
            }
            context->semantic_symbols[definition->function - 1U].is_lending |=
                declaration->is_lending;
            if (context->semantic_symbols[definition->function - 1U].is_chain &&
                !declaration->is_chain &&
                !r_semantic_add_diagnostic(context,
                                           "R-DIAG-TRAIT-001",
                                           "R-FUNC-0024",
                                           "@chain marks an implementation only of a @chain "
                                           "trait method",
                                           impl.span)) {
                return false;
            }
            context->semantic_symbols[definition->function - 1U].is_chain |= declaration->is_chain;
            compatible = (body->parameter_count == declaration->parameter_count) &&
                         (body->is_unsafe == declaration->is_unsafe) &&
                         (body->is_async == declaration->is_async) &&
                         (body->is_scoped == declaration->is_scoped);
            for (parameter = 0U; compatible && (parameter < declaration->parameter_count);
                 ++parameter) {
                RTypeId expected;
                if (!r_generic_substitute_type(
                        context,
                        context->semantic_parameter_types[declaration->first_parameter_type +
                                                          parameter],
                        trait->generic_schema,
                        first_argument,
                        &expected)) {
                    return false;
                }
                compatible =
                    expected ==
                    context->semantic_parameter_types[body->first_parameter_type + parameter];
            }
            if (compatible) {
                RTypeId expected_return;
                RTypeId expected_throws;
                if (!r_generic_substitute_type(context,
                                               declaration->return_type,
                                               trait->generic_schema,
                                               first_argument,
                                               &expected_return) ||
                    !r_generic_substitute_type(context,
                                               declaration->throws_type,
                                               trait->generic_schema,
                                               first_argument,
                                               &expected_throws)) {
                    return false;
                }
                compatible = (expected_return == body->return_type) &&
                             (expected_throws == body->throws_type);
            }
            if (!compatible &&
                !r_semantic_add_diagnostic(context,
                                           "R-DIAG-TRAIT-001",
                                           "R-TYPE-0042",
                                           "the implemented method shall have the exact signature "
                                           "the trait declares for the target type",
                                           body->name_span)) {
                return false;
            }
        }
        for (method_index = 0U; method_index < context->semantic_method_count; ++method_index) {
            const RSemanticMethod definition = context->semantic_methods[method_index];
            if ((definition.impl == impl_index + 1U) &&
                (r_semantic_find_trait_method(context, impl.trait, definition.name_intern_id) ==
                 NULL) &&
                !r_semantic_add_diagnostic(
                    context,
                    "R-DIAG-TRAIT-001",
                    "R-TYPE-0042",
                    "the implementation defines a method the trait does not declare",
                    context->semantic_symbols[definition.function - 1U].name_span)) {
                return false;
            }
        }
    }
    return true;
}

/* Monomorphization replaces a call of a trait prototype by the definition of the
 * implementation selected by the closed Self type (R-TYPE-0042). */
static bool r_generic_bind_trait_call(RFrontendContext *context,
                                      RSymbolId prototype_id,
                                      RTypeId self_type,
                                      RSymbolId *result,
                                      const RTypeVector *closed_arguments) {
    self_type = r_semantic_representation_type(context, self_type);
    RSemanticSymbol prototype = context->semantic_symbols[prototype_id - 1U];
    const RSemanticTrait trait = context->semantic_traits[prototype.trait - 1U];
    const RSemanticAggregate *item = r_semantic_aggregate_for_type(context, self_type);
    if ((trait.callable_signature != R_TYPE_ID_INVALID) &&
        (r_semantic_value_kind(context, self_type) == R_SEMANTIC_TYPE_FUNCTION) &&
        !r_generic_type_is_dependent(context, self_type, 0U)) {
        /* R-TYPE-0054 (L28): a function value is called through the dispatcher of its own
           signature, in the mode of the constraint. */
        const RSemanticType *signature = r_semantic_type(context, trait.callable_signature);
        return (signature != NULL) &&
               r_function_value_call(context,
                                     prototype.module_source,
                                     self_type,
                                     signature->flags & (R_SEMANTIC_TYPE_FLAG_CALL_MUT |
                                                         R_SEMANTIC_TYPE_FLAG_CALL_ONCE),
                                     result);
    }
    if (trait.callable_signature != R_TYPE_ID_INVALID && item != NULL &&
        item->function_item_target != R_SYMBOL_ID_INVALID) {
        *result = item->function_item_target;
        return true;
    }
    if (trait.type_parameter_schema != 0U &&
        (prototype.generic_origin != 0U || closed_arguments != NULL)) {
        const uint32_t count =
            context->generic_schemas[trait.type_parameter_schema - 1U].parameter_count;
        RTypeVector arguments = {0};
        bool applied = true;
        for (uint32_t index = 0U; applied && index < count; ++index)
            applied = r_type_vector_push(
                context,
                &arguments,
                closed_arguments != NULL
                    ? closed_arguments->items[1U + index]
                    : context->generic_arguments[prototype.first_generic_argument + 1U + index]);
        applied = applied &&
                  r_semantic_apply_trait(
                      context, prototype.trait, &arguments, prototype.name_span, &prototype.trait);
        r_context_free(context, arguments.items);
        if (!applied || prototype.trait == 0U)
            return false;
    }
    const RSemanticMethod *definition = r_semantic_find_impl_method(
        context, prototype.trait, self_type, prototype.method_name_intern_id);
    const RSemanticSymbol *body;
    RSymbolId function;

    if ((definition == NULL) &&
        (context->semantic_traits[prototype.trait - 1U].callable_signature != R_TYPE_ID_INVALID)) {
        /* R-TYPE-0044: a dependent callable constraint binds to the one callable implementation
           of the closure argument; instantiation already proved the substituted signature. */
        uint32_t closure_trait = UINT32_C(0);
        (void)r_generic_closure_signature(context, self_type, &closure_trait);
        if (closure_trait != UINT32_C(0)) {
            definition = r_semantic_find_impl_method(
                context, closure_trait, self_type, prototype.method_name_intern_id);
        }
    }
    if (prototype.is_overloaded) {
        RTypeVector arguments = {0};
        uint32_t first_argument;
        const uint32_t implementation = definition == NULL ? 0U : definition->impl;
        if (implementation != 0U) {
            const bool applied =
                r_semantic_trait_arguments(
                    context, prototype.trait, self_type, implementation, &arguments) &&
                r_generic_commit_arguments(context, &arguments, &first_argument);
            r_context_free(context, arguments.items);
            if (!applied || !r_semantic_impl_overload(
                                context, &prototype, self_type, first_argument, &definition)) {
                return false;
            }
        }
    }
    if (definition == NULL) {
        return false;
    }
    function = definition->function;
    body = &context->semantic_symbols[function - 1U];
    if (definition->is_default) {
        RTypeVector arguments = {0};
        bool applied = r_semantic_trait_arguments(
                           context, prototype.trait, self_type, definition->impl, &arguments) &&
                       r_generic_apply_function(context, function, &arguments, &function);
        r_context_free(context, arguments.items);
        if (!applied)
            return false;
        *result = function;
        return true;
    }
    if ((body->generic_schema != UINT32_C(0)) && (body->generic_origin == UINT32_C(0))) {
        const RSemanticAggregate *aggregate = r_semantic_aggregate_for_type(context, self_type);
        const uint32_t count = context->generic_schemas[body->generic_schema - 1U].parameter_count;
        RTypeVector arguments = {0};
        uint32_t index;
        bool applied = (aggregate != NULL) && (aggregate->generic_origin != UINT32_C(0));
        if (aggregate == NULL && definition->impl != UINT32_C(0)) {
            /* L27 (R-TYPE-0042): the arguments of an implementation over a standard type
               constructor follow from the shape of the instance. */
            bool shaped = false;
            applied = r_semantic_structural_impl(
                          context, definition->impl, self_type, false, &arguments, &shaped) &&
                      shaped && arguments.count == count;
            if (!applied) {
                r_context_free(context, arguments.items);
                return false;
            }
        }
        for (index = 0U; applied && aggregate != NULL && (index < count); ++index) {
            applied = r_type_vector_push(
                context,
                &arguments,
                context->generic_arguments[aggregate->first_generic_argument + index]);
        }
        applied = applied && r_generic_apply_function(context, function, &arguments, &function);
        r_context_free(context, arguments.items);
        if (!applied) {
            return false;
        }
    }
    *result = function;
    return true;
}

/* R-TYPE-0046 (L32): the one prototype of core::Format. */
static RSymbolId r_format_prototype(const RFrontendContext *context) {
    for (size_t index = 0U; index < context->semantic_method_count; ++index) {
        const RSemanticMethod *method = &context->semantic_methods[index];
        if ((method->trait == context->core_format_trait) && (method->impl == UINT32_C(0)))
            return method->function;
    }
    return R_SYMBOL_ID_INVALID;
}

/* R-TYPE-0046 (L32): a formatted value of a closed type reaches the implementations of the
 * nominal types its standard formatting meets through o, arrays, slices and tuples. Each such
 * aggregate records the core::Format method of its own closed type, which the format glue calls;
 * a dependent type is bound by each instantiation. */
static bool r_format_bind_type(RFrontendContext *context, RTypeId type_id, uint32_t depth) {
    type_id = r_semantic_value_type(context, type_id);
    const RSemanticType *type = r_semantic_type(context, type_id);
    const RSemanticAggregate *aggregate;
    uint32_t aggregate_id;
    RSymbolId function = R_SYMBOL_ID_INVALID;

    if ((type == NULL) || (depth > context->options.limits.max_nesting) ||
        r_generic_type_is_dependent(context, type_id, 0U)) {
        return true;
    }
    switch (type->kind) {
    case R_SEMANTIC_TYPE_BORROW:
    case R_SEMANTIC_TYPE_OPTION:
    case R_SEMANTIC_TYPE_FIXED_ARRAY:
    case R_SEMANTIC_TYPE_SLICE:
    case R_SEMANTIC_TYPE_ARRAY:
        return r_format_bind_type(context, type->base, depth + 1U);
    case R_SEMANTIC_TYPE_STRUCT:
    case R_SEMANTIC_TYPE_ENUM:
        break;
    default:
        return true;
    }
    aggregate = r_semantic_aggregate_for_type(context, type_id);
    if (aggregate == NULL) {
        return true;
    }
    aggregate_id = (uint32_t)(aggregate - context->semantic_aggregates) + 1U;
    if (aggregate->is_tuple) {
        const uint32_t first = aggregate->first_field;
        const uint32_t count = aggregate->field_count;
        for (uint32_t index = 0U; index < count; ++index) {
            if (!r_format_bind_type(
                    context, context->semantic_fields[first + index].type, depth + 1U)) {
                return false;
            }
        }
        return true;
    }
    if ((aggregate->format_function != R_SYMBOL_ID_INVALID) ||
        (r_format_prototype(context) == R_SYMBOL_ID_INVALID) ||
        !r_semantic_type_implements_trait(context, type_id, context->core_format_trait)) {
        return true;
    }
    if (!r_generic_bind_trait_call(context, r_format_prototype(context), type_id, &function, NULL)) {
        return context->resource_status == R_FRONTEND_OK;
    }
    context->semantic_aggregates[aggregate_id - 1U].format_function = function;
    return true;
}

/* R-TYPE-0046 (L32): every core::Format method the format glue of `type_id` calls, so the static
 * call graph and the resource contracts see the functions that formatting enters (R-FUNC-0004,
 * R-FUNC-0019). */
static bool r_format_visit_hooks(const RFrontendContext *context,
                                 RTypeId type_id,
                                 uint32_t depth,
                                 bool (*visit_hook)(void *user, RSymbolId hook),
                                 void *user) {
    type_id = r_semantic_value_type(context, type_id);
    const RSemanticType *type = r_semantic_type(context, type_id);
    const RSemanticAggregate *aggregate;

    if ((type == NULL) || (depth > context->options.limits.max_nesting)) {
        return true;
    }
    switch (type->kind) {
    case R_SEMANTIC_TYPE_BORROW:
    case R_SEMANTIC_TYPE_OPTION:
    case R_SEMANTIC_TYPE_FIXED_ARRAY:
    case R_SEMANTIC_TYPE_SLICE:
    case R_SEMANTIC_TYPE_ARRAY:
        return r_format_visit_hooks(context, type->base, depth + 1U, visit_hook, user);
    case R_SEMANTIC_TYPE_STRUCT:
    case R_SEMANTIC_TYPE_ENUM:
        break;
    default:
        return true;
    }
    aggregate = r_semantic_aggregate_for_type(context, type_id);
    if (aggregate == NULL) {
        return true;
    }
    if (aggregate->is_tuple) {
        for (uint32_t index = 0U; index < aggregate->field_count; ++index) {
            if (!r_format_visit_hooks(context,
                                      context->semantic_fields[aggregate->first_field + index].type,
                                      depth + 1U,
                                      visit_hook,
                                      user)) {
                return false;
            }
        }
        return true;
    }
    return (aggregate->format_function == R_SYMBOL_ID_INVALID) ||
           visit_hook(user, aggregate->format_function);
}

/* R-FUNC-0014: a method name resolves in the namespace of its owner type, and on a type
 * parameter through the traits its constraints prove. */
static bool r_body_resolve_method(RBodyContext *body,
                                  RTypeId owner_type,
                                  const RAstTokenView *name,
                                  RSourceSpan span,
                                  RSymbolId *function_id) {
    RFrontendContext *context = body->frontend;
    const RSemanticType *type = r_semantic_type(context, owner_type);
    const RSemanticSymbol *symbol;
    const RSemanticMethod *match = NULL;
    bool ambiguous = false;
    uint32_t matched_trait = 0U;

    *function_id = R_SYMBOL_ID_INVALID;
    /* R-TYPE-0051: a method of an interface is the prototype its contract provides, applied to
       the interface itself; the call dispatches to the implementation of the borrowed type. */
    const RGenericParameter *dyn = r_dyn_contract(context, owner_type);
    if (dyn != NULL ||
        ((type != NULL) && (type->kind == R_SEMANTIC_TYPE_PARAMETER) && (type->base != 0U) &&
         ((size_t)type->base <= context->generic_parameter_count))) {
        const RGenericParameter parameter =
            dyn != NULL ? *dyn : context->generic_parameters[type->base - 1U];
        RTypeVector arguments = {0};
        uint32_t index;
        bool applied;
        for (index = 0U; index < parameter.trait_constraint_count; ++index) {
            const uint32_t trait =
                context->generic_trait_constraints[parameter.first_trait_constraint + index];
            const RSemanticMethod *candidate =
                r_semantic_find_trait_method(context, trait, name->intern_id);
            if (candidate == NULL) {
                continue;
            }
            uint32_t provided;
            if (!r_semantic_find_provided_trait(context,
                                                trait,
                                                candidate->trait,
                                                dyn != NULL ? dyn->type : owner_type,
                                                true,
                                                0U,
                                                &provided))
                return false;
            if (provided == 0U)
                return r_body_diagnostic(
                    body,
                    "R-DIAG-TRAIT-001",
                    "R-TYPE-0043",
                    "the trait application does not provide an unambiguous method",
                    span);
            if (match != NULL &&
                (match->function != candidate->function || matched_trait != provided)) {
                return r_body_diagnostic(
                    body,
                    "R-DIAG-NAME-001",
                    "R-TYPE-0043",
                    "more than one trait constraint of this parameter provides the method",
                    span);
            }
            match = candidate;
            matched_trait = provided;
        }
        if (match == NULL) {
            return r_body_diagnostic(
                body,
                "R-DIAG-NAME-001",
                "R-TYPE-0035",
                "no trait constraint of this type parameter provides this method",
                span);
        }
        if (!r_semantic_trait_arguments(
                context, matched_trait, owner_type, UINT32_C(0), &arguments)) {
            r_context_free(context, arguments.items);
            return false;
        }
        applied = r_generic_apply_function(context, match->function, &arguments, function_id);
        r_context_free(context, arguments.items);
        return applied;
    }
    if ((type != NULL) && (type->kind == R_SEMANTIC_TYPE_FUNCTION)) {
        /* R-TYPE-0054 (L28): a function value provides exactly `call`. */
        uint32_t call_name;
        if (!r_closure_intern(context, "call", &call_name))
            return false;
        if (name->intern_id != call_name)
            return r_body_diagnostic(body,
                                     "R-DIAG-NAME-001",
                                     "R-TYPE-0054",
                                     "a function value provides only the method call",
                                     span);
        /* A function value is Copy: a call takes it by value, so a temporary may be called. */
        return r_function_value_call(
            context, body->source, owner_type, R_SEMANTIC_TYPE_FLAG_CALL_ONCE, function_id);
    }
    match = r_semantic_find_method(context, owner_type, name->intern_id, &ambiguous);
    if ((match == NULL) && !ambiguous &&
        !r_semantic_find_structural_method(
            context, owner_type, name->intern_id, &match, &ambiguous)) {
        return false;
    }
    if (ambiguous) {
        return r_body_diagnostic(body,
                                 "R-DIAG-NAME-001",
                                 "R-NAME-0010",
                                 "method name is ambiguous in the namespace of its owner type",
                                 span);
    }
    if (match == NULL) {
        return r_body_diagnostic(body,
                                 "R-DIAG-NAME-001",
                                 "R-NAME-0010",
                                 "the receiver type has no method with this name",
                                 span);
    }
    symbol = &context->semantic_symbols[match->function - 1U];
    if (symbol->is_protected && (symbol->module_source != body->source)) {
        return r_body_diagnostic(
            body, "R-DIAG-NAME-001", "R-NAME-0010", "method is protected", span);
    }
    *function_id = match->function;
    uint32_t effective_trait = match->trait;
    if (match->impl != 0U &&
        !r_semantic_impl_trait(context, match->impl, owner_type, &effective_trait))
        return false;
    if (match->trait != 0U) {
        const RSemanticMethod *prototype =
            r_semantic_find_trait_method(context, match->trait, name->intern_id);
        if (prototype != NULL &&
            context->semantic_symbols[prototype->function - 1U].is_overloaded) {
            const RSymbolId declaration = prototype->function;
            RTypeVector arguments = {0};
            bool applied = r_semantic_trait_arguments(
                               context, effective_trait, owner_type, match->impl, &arguments) &&
                           r_generic_apply_function(context, declaration, &arguments, function_id);
            r_context_free(context, arguments.items);
            return applied;
        }
    }
    if (match->is_default) {
        const RSymbolId declaration = match->function;
        RTypeVector arguments = {0};
        bool applied = r_semantic_trait_arguments(
                           context, effective_trait, owner_type, match->impl, &arguments) &&
                       r_generic_apply_function(context, declaration, &arguments, function_id);
        r_context_free(context, arguments.items);
        return applied;
    }
    return true;
}

/* R-FUNC-0014: when the name selects a field of the owner, the expression is member access
 * followed by the raw-function call of R-FUNC-0007. The field place is formed exactly as the
 * member suffix forms it, through the dereference of `->` when present. */
static bool r_body_method_field_place(RBodyContext *body,
                                      const RExpressionResult *current,
                                      bool arrow,
                                      const RAstTokenView *name,
                                      RSourceSpan span,
                                      bool *is_field,
                                      RExpressionResult *result) {
    RFrontendContext *context = body->frontend;
    RExpressionResult place = *current;
    const RSemanticAggregate *aggregate;
    const RSemanticField *field = NULL;
    uint32_t field_global_index = UINT32_C(0);
    uint32_t index;
    RHirNodeId children_storage[1];
    RHirVector children;
    RHirNodeId node_id;
    RHirNode *node;

    *is_field = false;
    r_body_expression_invalid(result);
    if (arrow) {
        const RSemanticType *pointer_type =
            r_semantic_type(context, r_semantic_value_type(context, place.type));
        if ((pointer_type == NULL) || (pointer_type->kind != R_SEMANTIC_TYPE_BORROW)) {
            return true;
        }
        aggregate = r_semantic_aggregate_for_type(context, pointer_type->base);
    } else {
        aggregate = r_semantic_aggregate_for_type(context, place.type);
    }
    if ((aggregate == NULL) || (aggregate->kind != R_SEMANTIC_AGGREGATE_STRUCT)) {
        return true;
    }
    for (index = 0U; index < aggregate->field_count; ++index) {
        const uint32_t global = aggregate->first_field + index;
        const RSemanticField *candidate = &context->semantic_fields[global];
        if (candidate->name_intern_id == name->intern_id) {
            field = candidate;
            field_global_index = global;
            break;
        }
    }
    if (field == NULL) {
        return true;
    }
    *is_field = true;
    if (arrow) {
        const RSemanticType *pointer_type =
            r_semantic_type(context, r_semantic_value_type(context, place.type));
        RHirNodeId deref_node_id;
        uint64_t dereferenced_borrow_referent;
        bool shared;
        bool supported;
        if (!r_body_indirection_access(body, &place, pointer_type, &supported, &shared)) {
            return false;
        }
        if (!supported) {
            return r_body_indirection_diagnostic(
                body,
                pointer_type,
                "pointer member access requires a usable non-null borrow or strong owner",
                span);
        }
        dereferenced_borrow_referent = r_body_dereferenced_borrow_referent(body, &place);
        children_storage[0] = place.node;
        children.items = children_storage;
        children.count = 1U;
        children.capacity = 1U;
        if (!r_body_append_node(body,
                                R_HIR_DEREF_PLACE,
                                span,
                                pointer_type->base,
                                place.symbol,
                                R_TOKEN_STAR,
                                UINT64_C(0),
                                true,
                                &children,
                                &deref_node_id)) {
            return false;
        }
        node = r_body_hir_node(body, deref_node_id);
        if (node == NULL) {
            context->resource_status = R_FRONTEND_INTERNAL_ERROR;
            return false;
        }
        node->aggregate_member = shared ? UINT32_C(1) : UINT32_C(0);
        node->auxiliary_type = r_semantic_value_type(context, place.type);
        node->borrow_origin = place.borrow_origin;
        node->borrow_referent = dereferenced_borrow_referent;
        node->borrow_origin_set = place.borrow_origin_set;
        node->borrow_origin_multiple = place.borrow_origin_multiple;
        place.node = deref_node_id;
        place.type = pointer_type->base;
        place.borrow_referent = dereferenced_borrow_referent;
        place.is_place = true;
    }
    if (!place.is_place) {
        return r_body_diagnostic(
            body, "R-DIAG-TYPE-001", "R-AGG-0002", "field selection requires a struct place", span);
    }
    if (field->is_protected && (aggregate->module_source != body->source)) {
        return r_body_diagnostic(
            body, "R-DIAG-NAME-001", "R-NAME-0003", "protected struct field is not visible", span);
    }
    children_storage[0] = place.node;
    children.items = children_storage;
    children.count = 1U;
    children.capacity = 1U;
    if (!r_body_append_node(body,
                            R_HIR_FIELD_PLACE,
                            span,
                            field->type,
                            place.symbol,
                            R_TOKEN_INVALID,
                            UINT64_C(0),
                            true,
                            &children,
                            &node_id)) {
        return false;
    }
    node = r_body_hir_node(body, node_id);
    if (node == NULL) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    node->aggregate_member = field_global_index + UINT32_C(1);
    if (!r_semantic_type_contains_borrow(context, field->type) &&
        !r_semantic_type_contains_slice(context, field->type) &&
        r_body_place_is_direct_storage_projection(body, place.node)) {
        place.borrow_origin = place.symbol;
        place.borrow_referent = UINT64_C(0);
        place.borrow_origin_set = UINT32_C(0);
        place.borrow_origin_multiple = false;
    }
    node->borrow_origin = place.borrow_origin;
    node->borrow_referent = place.borrow_referent;
    node->borrow_origin_set = place.borrow_origin_set;
    node->borrow_origin_multiple = place.borrow_origin_multiple;
    *result = place;
    result->node = node_id;
    result->type = field->type;
    result->is_place = true;
    result->is_integer_literal_form = false;
    result->is_unsuffixed_integer_literal = false;
    result->valid = true;
    return true;
}

static bool r_body_lower_method_call(RBodyContext *body,
                                     RAstRef postfix,
                                     RAstRef suffix,
                                     const RExpressionResult *current,
                                     RSourceSpan span,
                                     RTypeId contextual_type,
                                     RExpressionResult *result) {
    RAstTokenView name = {0};
    if (!r_semantic_first_identifier(body->frontend, suffix, &name)) {
        body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    return r_body_lower_method_call_named(
        body,
        postfix,
        suffix,
        current,
        r_ast_has_direct_token(body->frontend, suffix, R_TOKEN_ARROW),
        &name,
        true,
        span,
        contextual_type,
        result);
}

/* The call of method `name` on `current`, with the receiver formed for the declared receiver
 * kind; `allow_field` admits the field-of-function-pointer reading of R-FUNC-0014. */
static bool r_body_lower_method_call_named(RBodyContext *body,
                                           RAstRef postfix,
                                           RAstRef suffix,
                                           const RExpressionResult *current,
                                           bool arrow,
                                           const RAstTokenView *name_in,
                                           bool allow_field,
                                           RSourceSpan span,
                                           RTypeId contextual_type,
                                           RExpressionResult *result) {
    RFrontendContext *context = body->frontend;
    RAstNodeView suffix_view;
    RAstNodeView arguments_view;
    RAstNodeView primary_view;
    RAstRef argument_list;
    RAstTokenView name = *name_in;
    RExpressionResult receiver = *current;
    RSymbolId function_symbol_id = R_SYMBOL_ID_INVALID;
    const RSemanticSymbol *symbol;
    const RSemanticType *pointer;
    RTypeId owner_type;
    RTypeId borrow_type;
    uint32_t receiver_kind;
    RAstNodeView postfix_view;
    /* R-FUNC-0024 (L19): the call is continued by further suffixes without parentheses. */
    const bool chain_link =
        r_ast_ref_view(context, postfix, &postfix_view) && postfix_view.implicit_chain;

    r_body_expression_invalid(result);
    {
        /* R-STMT-0018: a task group name only receives group.cancel_all(). */
        const RSemanticSymbol *group = r_body_symbol(body, current->symbol);
        if (current->valid && group != NULL && group->is_task_scope) {
            if (chain_link) {
                return r_body_chain_rejected(body, span);
            }
            return r_scope_lower_cancel_all(body, current->symbol, name_in, suffix, span, result);
        }
    }
    /* The reads that form the receiver belong to it (R-BORROW-0009). */
    body->pending_receiver_read_first = body->loop_reads.count;
    body->pending_receiver_reads = true;
    if (!r_ast_ref_view(context, suffix, &suffix_view) ||
        !r_ast_find_direct_child(context, suffix, R_SYNTAX_ARGUMENT_LIST, &argument_list) ||
        !r_ast_ref_view(context, argument_list, &arguments_view)) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    if (!current->valid) {
        *result = *current;
        return true;
    }
    if (allow_field) {
        RExpressionResult field_place;
        bool is_field = false;
        if (!r_body_method_field_place(
                body, current, arrow, &name, suffix_view.span, &is_field, &field_place)) {
            return false;
        }
        if (is_field) {
            if (chain_link) {
                return r_body_chain_rejected(body, span);
            }
            if (suffix_view.call_free) {
                return r_body_diagnostic(body,
                                         "R-DIAG-TYPE-001",
                                         "R-EXPR-0020",
                                         "a call-free operand cannot call a function field",
                                         span);
            }
            if (!field_place.valid) {
                return true;
            }
            if (r_semantic_type_is_callable_callee(context, field_place.type)) {
                /* R-FUNC-0017: a field holding a closure, or a borrow of one, is called
                   through `call` like a closure local. */
                const RSemanticType *field_type =
                    r_semantic_type(context, r_semantic_value_type(context, field_place.type));
                RAstTokenView call_name = name;
                call_name.kind = R_TOKEN_IDENTIFIER;
                call_name.token = R_TOKEN_ID_INVALID;
                if (!r_intern_bytes(context, (const uint8_t *)"call", 4U, &call_name.intern_id)) {
                    return false;
                }
                return r_body_lower_method_call_named(
                    body,
                    postfix,
                    suffix,
                    &field_place,
                    (field_type != NULL) && (field_type->kind == R_SEMANTIC_TYPE_BORROW),
                    &call_name,
                    false,
                    span,
                    contextual_type,
                    result);
            }
            return r_body_indirect_call(body, &field_place, suffix, span, result);
        }
    }
    /* R-TYPE-0055 (L29): `owner->method()` reaches the member an owner of an interface holds. */
    const bool dyn_owner = arrow && (r_dyn_owned(context, receiver.type) != R_TYPE_ID_INVALID);
    if (dyn_owner) {
        owner_type = r_dyn_owned(context, receiver.type);
    } else if (arrow) {
        pointer = r_semantic_type(context, r_semantic_value_type(context, receiver.type));
        if ((pointer == NULL) || (pointer->kind != R_SEMANTIC_TYPE_BORROW) ||
            ((pointer->flags & R_SEMANTIC_TYPE_FLAG_NULLABLE) != 0U)) {
            return r_body_diagnostic(
                body,
                "R-DIAG-TYPE-001",
                "R-FUNC-0014",
                "a method receiver through -> requires a non-null borrow of its owner",
                span);
        }
        owner_type = pointer->base;
    } else {
        owner_type = r_semantic_value_type(context, receiver.type);
    }
    if (suffix_view.call_free) {
        return r_body_diagnostic(body,
                                 "R-DIAG-TYPE-001",
                                 "R-EXPR-0020",
                                 "a call-free operand permits only registered standard methods",
                                 span);
    }
    const RSemanticAggregate *item = r_semantic_aggregate_for_type(context, owner_type);
    if (item != NULL && item->function_item_target != R_SYMBOL_ID_INVALID &&
        r_semantic_token_text_equal_owned(context, &name, "call")) {
        if (chain_link) {
            return r_body_chain_rejected(body, span);
        }
        return r_body_call_function_item(
            body, postfix, postfix, suffix, name, item->function_item_target, &receiver, result);
    }
    /* R-TYPE-0046 (L32): the method of core::Format on a type with standard formatting. */
    const bool format_call = arrow && !dyn_owner &&
                             r_semantic_token_text_equal_owned(context, &name, "format");
    if (format_call) {
        if (!r_semantic_declare_core_traits(context)) {
            return false;
        }
        if (r_semantic_type_has_standard_format(context, owner_type)) {
            if (chain_link) {
                return r_body_chain_rejected(body, span);
            }
            return r_format_lower_append(body, argument_list, &receiver, owner_type, span, result);
        }
    }
    if (!r_body_resolve_method(body, owner_type, &name, span, &function_symbol_id)) {
        return false;
    }
    if (function_symbol_id == R_SYMBOL_ID_INVALID) {
        return true;
    }
    if (format_call && (r_dyn_contract(context, owner_type) == NULL)) {
        /* A parameter bound by core::Format: each instantiation formats by its own type. An
           interface dispatches to the member it was converted from (R-TYPE-0051). */
        const RSemanticSymbol *resolved = r_body_symbol(body, function_symbol_id);
        if ((resolved != NULL) && resolved->is_trait_prototype &&
            (resolved->trait == context->core_format_trait)) {
            if (chain_link) {
                return r_body_chain_rejected(body, span);
            }
            return r_format_lower_append(body, argument_list, &receiver, owner_type, span, result);
        }
    }
    RAstRef explicit_list;
    if (r_ast_find_direct_child(context, suffix, R_SYNTAX_GENERIC_ARGUMENT_LIST, &explicit_list)) {
        /* R-TYPE-0036: the receiver's owner supplies the schema prefix; the list the rest. */
        const RSemanticAggregate *owner = r_semantic_aggregate_for_type(context, owner_type);
        body->associated_owner_type =
            owner != NULL && owner->generic_origin != 0U ? owner_type : R_TYPE_ID_INVALID;
        if (!r_generic_close_explicit(body,
                                      explicit_list,
                                      true,
                                      &function_symbol_id,
                                      true,
                                      arguments_view.child_count + 1U,
                                      span)) {
            return false;
        }
        if (body->poisoned) {
            return true;
        }
    } else if (!r_overload_select(body,
                                  &function_symbol_id,
                                  argument_list,
                                  &arguments_view,
                                  &receiver,
                                  arrow,
                                  span)) {
        return false;
    }
    if (function_symbol_id == R_SYMBOL_ID_INVALID) {
        return true;
    }
    symbol = r_body_symbol(body, function_symbol_id);
    if (symbol == NULL) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    if (chain_link && !symbol->is_chain) {
        return r_body_chain_rejected(body, span);
    }
    receiver_kind = symbol->receiver_kind;
    if (receiver_kind == R_SEMANTIC_RECEIVER_NONE) {
        return r_body_diagnostic(
            body,
            "R-DIAG-TYPE-001",
            "R-FUNC-0014",
            "an associated function without a receiver is called through its qualified name",
            span);
    }
    if (dyn_owner) {
        const RSemanticType *owner =
            r_semantic_type(context, r_semantic_value_type(context, receiver.type));
        const bool shared_owner = (owner == NULL) || (owner->kind != R_SEMANTIC_TYPE_OWN);
        const RExpressionResult place = receiver;
        RExpressionResult borrowed;
        RTypeId interface_borrow;
        r_body_expression_invalid(&borrowed);
        if (receiver_kind == R_SEMANTIC_RECEIVER_VALUE) {
            return r_body_diagnostic(body,
                                     "R-DIAG-TYPE-001",
                                     "R-FUNC-0014",
                                     "a consuming receiver requires the value form of the call",
                                     span);
        }
        if ((owner != NULL) && (owner->kind == R_SEMANTIC_TYPE_WEAK)) {
            return r_body_diagnostic(body,
                                     "R-DIAG-TYPE-001",
                                     "R-TYPE-0055",
                                     "a weak owner of a dyn interface is upgraded before its "
                                     "methods are called",
                                     span);
        }
        if (shared_owner && (receiver_kind == R_SEMANTIC_RECEIVER_EXCLUSIVE)) {
            return r_body_diagnostic(body,
                                     "R-DIAG-BORROW-001",
                                     "R-TYPE-0055",
                                     "a shared owner of a dyn interface reaches only methods "
                                     "whose receiver is const Self*",
                                     span);
        }
        const uint32_t access = receiver_kind == R_SEMANTIC_RECEIVER_SHARED
                                    ? R_SEMANTIC_TYPE_FLAG_SHARED
                                    : R_SEMANTIC_TYPE_FLAG_NONE;
        if (!r_semantic_intern_derived_type(context,
                                            R_SEMANTIC_TYPE_BORROW,
                                            r_semantic_value_type(context, receiver.type),
                                            R_TYPE_ID_INVALID,
                                            UINT64_C(0),
                                            access,
                                            &borrow_type) ||
            !r_semantic_intern_derived_type(context,
                                            R_SEMANTIC_TYPE_BORROW,
                                            owner_type,
                                            R_TYPE_ID_INVALID,
                                            UINT64_C(0),
                                            access,
                                            &interface_borrow) ||
            !r_body_form_safe_borrow(body, span, borrow_type, &place, &borrowed)) {
            return false;
        }
        if (!borrowed.valid) {
            return true;
        }
        if (!r_body_append_cast(body, &borrowed, interface_borrow, span, false)) {
            return false;
        }
        receiver = borrowed;
        arrow = false;
    } else if (arrow && (receiver_kind == R_SEMANTIC_RECEIVER_VALUE) &&
               r_function_value_dispatcher(context, function_symbol_id)) {
        /* R-TYPE-0054 (L28): a call through a borrowed function value copies the value. */
        RExpressionResult referent;
        if (!r_body_place_to_value(body, &receiver) ||
            !r_body_form_deref_place(body, receiver, span, &referent)) {
            return false;
        }
        if (!referent.valid) {
            return true;
        }
        receiver = referent;
        arrow = false;
    }
    if (dyn_owner) {
        /* The receiver is the interface borrow formed above. */
    } else if (arrow) {
        if (receiver_kind == R_SEMANTIC_RECEIVER_VALUE) {
            return r_body_diagnostic(body,
                                     "R-DIAG-TYPE-001",
                                     "R-FUNC-0014",
                                     "a consuming receiver requires the value form of the call",
                                     span);
        }
        if (!r_body_place_to_value(body, &receiver)) {
            return false;
        }
        pointer = r_semantic_type(context, receiver.type);
        if ((pointer != NULL) && (receiver_kind == R_SEMANTIC_RECEIVER_SHARED) &&
            (pointer->flags == R_SEMANTIC_TYPE_FLAG_NONE)) {
            if (!r_semantic_intern_derived_type(context,
                                                R_SEMANTIC_TYPE_BORROW,
                                                pointer->base,
                                                R_TYPE_ID_INVALID,
                                                UINT64_C(0),
                                                R_SEMANTIC_TYPE_FLAG_SHARED,
                                                &borrow_type) ||
                !r_body_finish_value(body, borrow_type, &receiver)) {
                return false;
            }
        } else if ((pointer != NULL) && (receiver_kind == R_SEMANTIC_RECEIVER_EXCLUSIVE) &&
                   (pointer->flags != R_SEMANTIC_TYPE_FLAG_NONE)) {
            return r_body_diagnostic(body,
                                     "R-DIAG-BORROW-001",
                                     "R-FUNC-0014",
                                     "an exclusive receiver requires an exclusive borrow",
                                     span);
        }
    } else if (receiver_kind == R_SEMANTIC_RECEIVER_VALUE) {
        if (!r_body_place_to_value(body, &receiver)) {
            return false;
        }
    } else {
        const RExpressionResult place = receiver;
        if (!r_semantic_intern_derived_type(context,
                                            R_SEMANTIC_TYPE_BORROW,
                                            owner_type,
                                            R_TYPE_ID_INVALID,
                                            UINT64_C(0),
                                            receiver_kind == R_SEMANTIC_RECEIVER_SHARED
                                                ? R_SEMANTIC_TYPE_FLAG_SHARED
                                                : R_SEMANTIC_TYPE_FLAG_NONE,
                                            &borrow_type) ||
            !r_body_form_safe_borrow(body, span, borrow_type, &place, &receiver)) {
            return false;
        }
    }
    if (!receiver.valid) {
        return true;
    }
    (void)memset(&primary_view, 0, sizeof(primary_view));
    primary_view.span = span;
    return r_body_lower_resolved_call(body,
                                      postfix,
                                      postfix,
                                      suffix,
                                      argument_list,
                                      arguments_view,
                                      primary_view,
                                      suffix_view,
                                      name,
                                      function_symbol_id,
                                      body->active_symbols.count,
                                      R_BODY_LOOKUP_NONE,
                                      &receiver,
                                      true,
                                      contextual_type,
                                      result);
}

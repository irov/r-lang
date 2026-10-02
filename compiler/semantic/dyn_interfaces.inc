/* R-TYPE-0051: dyn interfaces. `dyn(Contracts)` is a closed type identified by its resolved
 * contracts. A hidden parameter carries them, so method lookup, associated projections and
 * capability proofs follow the rules of a constrained parameter. The interface exists only as
 * the referent of a borrow, whose value is a tag that selects one of the finitely many types the
 * program converts to the interface and the address of the borrowed object. */

static const RGenericParameter *r_dyn_contract(const RFrontendContext *context, RTypeId id) {
    const RSemanticType *type = r_semantic_type(context, id);
    if (type == NULL || type->kind != R_SEMANTIC_TYPE_DYN)
        return NULL;
    const RSemanticType *carrier = r_semantic_type(context, type->base);
    if (carrier == NULL || carrier->kind != R_SEMANTIC_TYPE_PARAMETER || carrier->base == 0U ||
        carrier->base > context->generic_parameter_count)
        return NULL;
    return &context->generic_parameters[carrier->base - 1U];
}

/* The interface a borrow type refers to, or R_TYPE_ID_INVALID. */
static RTypeId r_dyn_referent(const RFrontendContext *context, RTypeId id) {
    const RSemanticType *type = r_semantic_type(context, r_semantic_value_type(context, id));
    return type != NULL && type->kind == R_SEMANTIC_TYPE_BORROW &&
                   r_dyn_contract(context, type->base) != NULL
               ? type->base
               : R_TYPE_ID_INVALID;
}

static int r_dyn_compare_text(const void *left, const void *right) {
    const RGenericText *a = left;
    const RGenericText *b = right;
    const size_t length = a->length < b->length ? a->length : b->length;
    const int order = memcmp(a->bytes, b->bytes, length);
    return order != 0 ? order : (a->length < b->length ? -1 : (a->length > b->length ? 1 : 0));
}

/* One trait application spelled by module, name and arguments. */
static bool r_dyn_trait_text(RGenericText *text, uint32_t trait) {
    const RFrontendContext *context = text->context;
    const RSemanticTrait record = context->semantic_traits[trait - 1U];
    const uint32_t origin = record.generic_origin != 0U ? record.generic_origin : trait;
    const RSemanticTrait declaration = context->semantic_traits[origin - 1U];
    const RSource *source = r_get_source_const(context, declaration.module_source);
    const RInternEntry *name = &context->intern_entries[declaration.name_intern_id - 1U];
    const char *module = declaration.is_core                               ? "core"
                         : (source == NULL || source->module_name == NULL) ? ""
                                                                           : source->module_name;
    if (!r_generic_text(text, module, strlen(module)) || !r_generic_text(text, "::", 2U) ||
        !r_generic_text(text, name->bytes, name->length))
        return false;
    if (record.generic_origin == 0U || record.type_parameter_schema == 0U)
        return true;
    const uint32_t count =
        context->generic_schemas[record.type_parameter_schema - 1U].parameter_count;
    for (uint32_t index = 0U; index < count; ++index)
        if (!r_generic_text(text, index == 0U ? "<" : ", ", index == 0U ? 1U : 2U) ||
            !r_generic_type_name(
                text, context->generic_arguments[record.first_generic_argument + index], 1U))
            return false;
    return r_generic_text(text, ">", 1U);
}

/* The canonical spelling `dyn(A & B & send & A.Item = i32)`: traits, capabilities and associated
 * equalities, each group sorted, so equal contracts written in any order are one type. */
static bool r_dyn_contract_text(RFrontendContext *context, uint32_t index, RGenericText *text) {
    static const char *const capabilities[] = {
        "copy", "pod", "send", "sync", "key", "error", "unborrowed", "json_encode", "json_decode",
        "", "clone"};
    const RGenericParameter parameter = context->generic_parameters[index];
    RGenericText *parts = NULL;
    size_t count = 0U, capacity = 0U;
    bool success = false;
    for (uint32_t trait = 0U; trait < parameter.trait_constraint_count; ++trait) {
        if (!r_grow_array(context, (void **)&parts, &capacity, sizeof(*parts), count + 1U))
            goto cleanup;
        parts[count] = (RGenericText){context, NULL, 0U, 0U, false};
        if (!r_dyn_trait_text(
                &parts[count++],
                context->generic_trait_constraints[parameter.first_trait_constraint + trait]))
            goto cleanup;
    }
    for (uint32_t bit = 0U; bit < sizeof(capabilities) / sizeof(capabilities[0]); ++bit) {
        if ((parameter.constraints & (1U << bit)) == 0U ||
            (bit != 1U && (parameter.constraints & R_GENERIC_POD) != 0U &&
             ((1U << bit) & (R_GENERIC_COPY | R_GENERIC_UNBORROWED)) != 0U))
            continue;
        if (!r_grow_array(context, (void **)&parts, &capacity, sizeof(*parts), count + 1U))
            goto cleanup;
        parts[count] = (RGenericText){context, NULL, 0U, 0U, false};
        if (!r_generic_text(&parts[count++], capabilities[bit], strlen(capabilities[bit])))
            goto cleanup;
    }
    for (size_t slot = 0U; slot < context->generic_parameter_count; ++slot) {
        const RGenericParameter binding = context->generic_parameters[slot];
        if (binding.projection_base != parameter.type || binding.projection_bound == 0U)
            continue;
        const RSemanticTrait trait = context->semantic_traits[binding.projection_trait - 1U];
        const uint32_t origin =
            trait.generic_origin != 0U ? trait.generic_origin : binding.projection_trait;
        const RSemanticTrait declaration = context->semantic_traits[origin - 1U];
        const RInternEntry *name =
            &context->intern_entries[context
                                         ->semantic_associated_types[declaration.first_associated +
                                                                     binding.projection_index]
                                         .name_intern_id -
                                     1U];
        if (!r_grow_array(context, (void **)&parts, &capacity, sizeof(*parts), count + 1U))
            goto cleanup;
        parts[count] = (RGenericText){context, NULL, 0U, 0U, false};
        if (!r_dyn_trait_text(&parts[count++], binding.projection_trait) ||
            !r_generic_text(&parts[count - 1U], ".", 1U) ||
            !r_generic_text(&parts[count - 1U], name->bytes, name->length) ||
            !r_generic_text(&parts[count - 1U], " = ", 3U) ||
            !r_generic_type_name(&parts[count - 1U], binding.projection_bound, 1U))
            goto cleanup;
    }
    if (count > 1U)
        qsort(parts, count, sizeof(*parts), r_dyn_compare_text);
    if (!r_generic_text(text, "dyn(", 4U))
        goto cleanup;
    for (size_t part = 0U; part < count; ++part) {
        /* A repeated trait adds nothing to the contract. */
        if (part != 0U && r_dyn_compare_text(&parts[part], &parts[part - 1U]) == 0)
            continue;
        if ((part != 0U && !r_generic_text(text, " & ", 3U)) ||
            !r_generic_text(text, (const char *)parts[part].bytes, parts[part].length))
            goto cleanup;
    }
    success = r_generic_text(text, ")", 1U);
cleanup:
    for (size_t part = 0U; part < count; ++part)
        r_context_free(context, parts[part].bytes);
    r_context_free(context, parts);
    return success;
}

/* R-TYPE-0051: the interface named by `dyn(contracts)`. */
static bool r_dyn_type(RFrontendContext *context, RAstRef contract, RTypeId *result) {
    RAstNodeView view;
    RGenericParameter parameter = {0};
    RGenericText text = {context, NULL, 0U, 0U, false};
    uint32_t key = 0U;
    char name[80];
    *result = R_TYPE_ID_INVALID;
    if (!r_ast_ref_view(context, contract, &view))
        return false;
    parameter.name_span = view.span;
    const int length = snprintf(
        name, sizeof(name), "$dyn%u_%u", (unsigned)contract.source, (unsigned)view.span.start);
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
    const size_t diagnostics = context->diagnostic_count;
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
            return r_semantic_add_diagnostic(context,
                                             "R-DIAG-TYPE-001",
                                             "R-TYPE-0051",
                                             "a dyn interface names traits, capabilities and "
                                             "associated equalities, not callable signatures",
                                             bound_view.span);
        }
    }
    if (!r_opaque_associated_contract(context, parameter.type, contract, true))
        return false;
    if (context->diagnostic_count != diagnostics)
        return true;
    if (context->generic_parameters[index].trait_constraint_count == 0U)
        return r_semantic_add_diagnostic(context,
                                         "R-DIAG-TYPE-001",
                                         "R-TYPE-0051",
                                         "a dyn interface names at least one trait",
                                         view.span);
    /* The members of an interface are closed types, so its contract is closed as well. */
    bool dependent = false;
    for (uint32_t bound = 0U;
         !dependent && bound < context->generic_parameters[index].trait_constraint_count;
         ++bound) {
        const RSemanticTrait trait =
            context->semantic_traits
                [context->generic_trait_constraints
                     [context->generic_parameters[index].first_trait_constraint + bound] -
                 1U];
        const uint32_t count =
            trait.generic_origin == 0U || trait.type_parameter_schema == 0U
                ? 0U
                : context->generic_schemas[trait.type_parameter_schema - 1U].parameter_count;
        for (uint32_t argument = 0U; !dependent && argument < count; ++argument)
            dependent = r_generic_type_is_dependent(
                context, context->generic_arguments[trait.first_generic_argument + argument], 0U);
    }
    for (size_t slot = 0U; !dependent && slot < context->generic_parameter_count; ++slot)
        dependent = context->generic_parameters[slot].projection_base == parameter.type &&
                    context->generic_parameters[slot].projection_bound != 0U &&
                    r_generic_type_is_dependent(
                        context, context->generic_parameters[slot].projection_bound, 0U);
    if (dependent)
        return r_semantic_add_diagnostic(context,
                                         "R-DIAG-TYPE-001",
                                         "R-TYPE-0051",
                                         "the contract of a dyn interface does not depend on "
                                         "generic parameters",
                                         view.span);
    /* The capabilities that the traits require of Self belong to the contract, so an interface
       written with or without them is one interface. */
    context->generic_parameters[index].constraints |=
        r_generic_parameter_constraints(context, parameter.type);
    const bool keyed = r_dyn_contract_text(context, index, &text) &&
                       r_intern_bytes(context, (const uint8_t *)text.bytes, text.length, &key);
    r_context_free(context, text.bytes);
    if (!keyed)
        return false;
    /* An equal contract written elsewhere is the same interface; this carrier stays unused. */
    for (uint32_t other = 0U; other < index; ++other)
        if (context->generic_parameters[other].dyn_key == key)
            return r_semantic_intern_type(
                context, R_SEMANTIC_TYPE_DYN, context->generic_parameters[other].type, result);
    context->generic_parameters[index].dyn_key = key;
    return r_semantic_intern_type(context, R_SEMANTIC_TYPE_DYN, parameter.type, result);
}

/* R-TYPE-0055 (L29): `own dyn(C)*`, `arc dyn(C)`, `rc dyn(C)`, `weak arc dyn(C)` and
   `weak rc dyn(C)` own the member of an interface they were converted from. */
static bool r_dyn_owner_type(RFrontendContext *context,
                             RAstRef type_ref,
                             RAstRef contract,
                             RTokenKind lead,
                             RTypeId *result) {
    RAstNodeView view;
    bool has_pointer = false;
    bool nullable = false;
    RTypeId dyn = R_TYPE_ID_INVALID;
    uint32_t flags = R_SEMANTIC_TYPE_FLAG_NONE;
    RSemanticTypeKind kind = R_SEMANTIC_TYPE_OWN;
    *result = R_TYPE_ID_INVALID;
    if (!r_ast_ref_view(context, type_ref, &view))
        return false;
    if (!r_semantic_check_profile(context,
                                  R_FRONTEND_PROFILE_ALLOCATION,
                                  "owners of dyn interfaces require at least the allocation "
                                  "profile",
                                  view.span))
        return false;
    if (!r_semantic_pointer_suffix(context, type_ref, &has_pointer, &nullable) ||
        !r_dyn_type(context, contract, &dyn) || dyn == R_TYPE_ID_INVALID)
        return false;
    if (nullable) {
        (void)r_semantic_add_diagnostic(context,
                                        "R-DIAG-TYPE-001",
                                        "R-TYPE-0055",
                                        "an owner of a dyn interface is not nullable; o<...> "
                                        "expresses its absence",
                                        view.span);
        return false;
    }
    if (lead == R_TOKEN_KW_ARC) {
        kind = R_SEMANTIC_TYPE_ARC;
    } else if (lead == R_TOKEN_KW_RC) {
        kind = R_SEMANTIC_TYPE_RC;
    } else if (lead == R_TOKEN_KW_WEAK) {
        kind = R_SEMANTIC_TYPE_WEAK;
        if (r_semantic_type_has_direct_token(context, type_ref, R_TOKEN_KW_RC))
            flags |= R_SEMANTIC_TYPE_FLAG_RC_OWNER;
    }
    return r_semantic_intern_derived_type(
        context, kind, dyn, R_TYPE_ID_INVALID, UINT64_C(0), flags, result);
}

/* The interface an owner type owns a member of (R-TYPE-0055), or R_TYPE_ID_INVALID. */
static RTypeId r_dyn_owned(const RFrontendContext *context, RTypeId id) {
    const RSemanticType *type = r_semantic_type(context, r_semantic_value_type(context, id));
    return type != NULL &&
                   (type->kind == R_SEMANTIC_TYPE_OWN || type->kind == R_SEMANTIC_TYPE_ARC ||
                    type->kind == R_SEMANTIC_TYPE_RC || type->kind == R_SEMANTIC_TYPE_WEAK) &&
                   r_dyn_contract(context, type->base) != NULL
               ? type->base
               : R_TYPE_ID_INVALID;
}

/* The associated type `index` of `trait` fixed by the contract of `dyn_type`. */
static bool r_dyn_projection(const RFrontendContext *context,
                             RTypeId dyn_type,
                             uint32_t trait,
                             uint32_t index,
                             RTypeId *result) {
    const RGenericParameter *contract = r_dyn_contract(context, dyn_type);
    *result = R_TYPE_ID_INVALID;
    if (contract == NULL)
        return false;
    for (int pass = 0; pass < 2 && *result == R_TYPE_ID_INVALID; ++pass)
        for (size_t slot = 0U; slot < context->generic_parameter_count; ++slot) {
            const RGenericParameter binding = context->generic_parameters[slot];
            if (binding.projection_base == contract->type && binding.projection_index == index &&
                binding.projection_bound != 0U &&
                (pass == 0 ? binding.projection_trait == trait
                           : r_semantic_trait_origin(context, binding.projection_trait) ==
                                 r_semantic_trait_origin(context, trait))) {
                *result = binding.projection_bound;
                break;
            }
        }
    return *result != R_TYPE_ID_INVALID;
}

/* R-TYPE-0051: whether a borrow of `source` may become a borrow of the interface `dyn_type`:
 * the referent proves every trait, capability and associated equality of the contract. */
static bool r_dyn_accepts(RFrontendContext *context, RTypeId source, RTypeId dyn_type) {
    const RGenericParameter *found = r_dyn_contract(context, dyn_type);
    if (found == NULL || source == dyn_type)
        return false;
    const RGenericParameter contract = *found;
    for (uint32_t capability = R_GENERIC_COPY; capability <= R_GENERIC_CLONE;
         capability = r_generic_next_capability(capability))
        if ((contract.constraints & capability) != 0U &&
            !r_generic_has_property(context, source, capability, 0U))
            return false;
    for (uint32_t index = 0U; index < contract.trait_constraint_count; ++index)
        if (!r_semantic_type_implements_trait(
                context,
                source,
                context->generic_trait_constraints[contract.first_trait_constraint + index]))
            return false;
    for (size_t slot = 0U; slot < context->generic_parameter_count; ++slot) {
        const RGenericParameter binding = context->generic_parameters[slot];
        RTypeId actual;
        if (binding.projection_base != contract.type || binding.projection_bound == 0U)
            continue;
        if (!r_semantic_resolve_projection(
                context, source, binding.projection_trait, binding.projection_index, &actual) ||
            actual != binding.projection_bound)
            return false;
    }
    return true;
}

/* R-TYPE-0051: `&value` of a type that proves the contract becomes `dyn(...)*` or
 * `const dyn(...)*` where that type is expected. The conversion keeps the borrow's origin.
 * R-TYPE-0055 (L29): an owner of a member converts to the same owner of the interface, and an
 * interface converts to one whose contract its own contract proves. */
static bool r_dyn_convert(RBodyContext *body, RExpressionResult *result, RTypeId destination_type) {
    RFrontendContext *context = body->frontend;
    const RSemanticType *source =
        r_semantic_type(context, r_semantic_value_type(context, result->type));
    const RSemanticType *destination = r_semantic_type(context, destination_type);
    if (source == NULL || destination == NULL || source->kind != destination->kind ||
        source->base == destination->base || r_dyn_contract(context, destination->base) == NULL)
        return true;
    const bool borrow = source->kind == R_SEMANTIC_TYPE_BORROW;
    if (!borrow && source->kind != R_SEMANTIC_TYPE_OWN && source->kind != R_SEMANTIC_TYPE_ARC &&
        source->kind != R_SEMANTIC_TYPE_RC && source->kind != R_SEMANTIC_TYPE_WEAK)
        return true;
    if (borrow && (((source->flags & R_SEMANTIC_TYPE_FLAG_SHARED) != 0U &&
                    (destination->flags & R_SEMANTIC_TYPE_FLAG_SHARED) == 0U) ||
                   ((source->flags & R_SEMANTIC_TYPE_FLAG_NULLABLE) != 0U &&
                    (destination->flags & R_SEMANTIC_TYPE_FLAG_NULLABLE) == 0U)))
        return true;
    if (!borrow && source->flags != destination->flags)
        return true;
    const RSourceSpan span = r_body_hir_node(body, result->node)->span;
    const RTypeId source_base = r_semantic_value_type(context, source->base);
    const RTypeId destination_base = destination->base;
    if (borrow && (r_dyn_owned(context, source_base) != R_TYPE_ID_INVALID)) {
        /* R-TYPE-0055 (L29): a borrow of an owner of an interface is a borrow of its member; a
           shared owner lends only shared access. */
        const RSemanticType *owner = r_semantic_type(context, source_base);
        const RTypeId owned = r_dyn_owned(context, source_base);
        if ((owner != NULL) && (owner->kind == R_SEMANTIC_TYPE_WEAK)) {
            result->valid = false;
            return r_body_diagnostic(body,
                                     "R-DIAG-TYPE-001",
                                     "R-TYPE-0055",
                                     "a weak owner of a dyn interface is upgraded before its "
                                     "member is borrowed",
                                     span);
        }
        if ((owner != NULL) && (owner->kind != R_SEMANTIC_TYPE_OWN) &&
            ((destination->flags & R_SEMANTIC_TYPE_FLAG_SHARED) == 0U)) {
            result->valid = false;
            return r_body_diagnostic(body,
                                     "R-DIAG-BORROW-001",
                                     "R-TYPE-0055",
                                     "a shared owner of a dyn interface lends only a const "
                                     "interface borrow",
                                     span);
        }
        if ((owned != destination_base) && !r_dyn_accepts(context, owned, destination_base)) {
            if (context->resource_status != R_FRONTEND_OK)
                return false;
            result->valid = false;
            return r_body_diagnostic(body,
                                     "R-DIAG-TYPE-001",
                                     "R-TYPE-0055",
                                     "a dyn interface converts only to an interface whose "
                                     "contract its own contract proves",
                                     span);
        }
        return r_body_append_cast(body, result, destination_type, span, false);
    }
    if (r_dyn_contract(context, source_base) != NULL) {
        /* R-TYPE-0055 (L29): narrowing renumbers the member's tag and allocates nothing. */
        if (!r_dyn_accepts(context, source_base, destination_base)) {
            if (context->resource_status != R_FRONTEND_OK)
                return false;
            result->valid = false;
            return r_body_diagnostic(body,
                                     "R-DIAG-TYPE-001",
                                     "R-TYPE-0055",
                                     "a dyn interface converts only to an interface whose "
                                     "contract its own contract proves",
                                     span);
        }
        return r_body_append_cast(body, result, destination_type, span, false);
    }
    if (!r_dyn_accepts(context, source_base, destination_base)) {
        if (context->resource_status != R_FRONTEND_OK)
            return false;
        result->valid = false;
        return r_body_diagnostic(body,
                                 "R-DIAG-TRAIT-001",
                                 borrow ? "R-TYPE-0051" : "R-TYPE-0055",
                                 borrow ? "the borrowed type does not satisfy the contract of the "
                                          "dyn interface"
                                        : "the owned type does not satisfy the contract of the "
                                          "dyn interface",
                                 span);
    }
    if (!borrow && !r_generic_has_property(context, source_base, R_GENERIC_UNBORROWED, 0U)) {
        result->valid = false;
        return r_body_diagnostic(body,
                                 "R-DIAG-TYPE-001",
                                 "R-TYPE-0055",
                                 "an owner of a dyn interface owns a member that holds no views",
                                 span);
    }
    return r_body_append_cast(body, result, destination_type, span, false);
}

/* The borrow that `&place` forms where `contextual_type` is expected: a borrow of the place with
 * the expected qualifiers when an interface is expected, so that the conversion follows. */
static bool r_dyn_borrow_context(RFrontendContext *context,
                                 RTypeId contextual_type,
                                 RTypeId place_type,
                                 RTypeId *borrow_type) {
    const RSemanticType *destination =
        r_semantic_type(context, r_semantic_value_type(context, contextual_type));
    *borrow_type = contextual_type;
    place_type = r_semantic_value_type(context, place_type);
    if (destination == NULL || destination->kind != R_SEMANTIC_TYPE_BORROW ||
        r_dyn_contract(context, destination->base) == NULL || place_type == destination->base ||
        r_dyn_contract(context, place_type) != NULL)
        return true;
    /* R-TYPE-0055 (L29): `&owner` of an interface borrows the owner with the expected access. */
    return r_semantic_intern_derived_type(context,
                                          R_SEMANTIC_TYPE_BORROW,
                                          place_type,
                                          R_TYPE_ID_INVALID,
                                          UINT64_C(0),
                                          destination->flags & R_SEMANTIC_TYPE_FLAG_SHARED,
                                          borrow_type);
}

/* Whether `symbol` applies a trait prototype to an interface, so that each call of it dispatches
 * on the tag of its receiver. */
static bool r_dyn_dispatcher(const RFrontendContext *context, RSymbolId symbol) {
    if (symbol == R_SYMBOL_ID_INVALID || (size_t)symbol > context->semantic_symbol_count)
        return false;
    const RSemanticSymbol *function = &context->semantic_symbols[symbol - 1U];
    return function->kind == R_SEMANTIC_SYMBOL_FUNCTION && function->generic_origin != 0U &&
           context->semantic_symbols[function->generic_origin - 1U].is_trait_prototype &&
           r_dyn_contract(context, context->generic_arguments[function->first_generic_argument]) !=
               NULL;
}

/* R-TYPE-0046 (L32): a dispatcher of a prototype the compiler declares (core::Format) has no
 * declaration to instantiate; its function node and parameters are built from its signature at
 * the first call, whose span it takes. */
static bool r_dyn_synthetic_dispatcher(RFrontendContext *context, RSymbolId id, RSourceSpan span) {
    const RSemanticSymbol dispatcher = context->semantic_symbols[id - 1U];
    RHirVector children = {0};
    RHirNode function_node;
    RHirNodeId function_id;
    bool success = false;
    static const char *const names[2] = {"this", "out"};

    if ((dispatcher.hir_node != R_HIR_NODE_ID_INVALID) || (dispatcher.parameter_count > 2U)) {
        return true;
    }
    (void)memset(&function_node, 0, sizeof(function_node));
    function_node.kind = R_HIR_FUNCTION;
    function_node.span = span;
    function_node.type = dispatcher.return_type;
    function_node.auxiliary_type = dispatcher.async_start_type;
    function_node.symbol = id;
    if (!r_hir_append_node(context, function_node, &function_id)) {
        return false;
    }
    for (uint32_t index = 0U; index < dispatcher.parameter_count; ++index) {
        RSemanticSymbol parameter;
        RSymbolId parameter_id;
        RHirNode parameter_node;
        RHirNodeId parameter_node_id;
        (void)memset(&parameter, 0, sizeof(parameter));
        parameter.kind = R_SEMANTIC_SYMBOL_PARAMETER;
        /* Distinct positions give the parameters distinct C names. */
        parameter.name_span = span;
        parameter.name_span.start = span.start + index <= span.end ? span.start + index : span.end;
        parameter.scope_start = span.start;
        parameter.module_source = span.source;
        parameter.type =
            context->semantic_parameter_types[dispatcher.first_parameter_type + index];
        parameter.parent = id;
        parameter.object_state = R_SEMANTIC_OBJECT_STATE_INITIALIZED;
        parameter.signature_supported = true;
        if (!r_intern_bytes(context,
                            (const uint8_t *)names[index],
                            strlen(names[index]),
                            &parameter.name_intern_id) ||
            !r_semantic_append_symbol(context, parameter, &parameter_id)) {
            goto cleanup;
        }
        (void)memset(&parameter_node, 0, sizeof(parameter_node));
        parameter_node.kind = R_HIR_PARAMETER;
        parameter_node.span = parameter.name_span;
        parameter_node.type = parameter.type;
        parameter_node.symbol = parameter_id;
        if (!r_hir_append_node(context, parameter_node, &parameter_node_id) ||
            !r_hir_vector_push(context, &children, parameter_node_id)) {
            goto cleanup;
        }
    }
    if (!r_hir_commit_children(context, function_id, &children)) {
        goto cleanup;
    }
    context->semantic_symbols[id - 1U].hir_node = function_id;
    context->semantic_symbols[id - 1U].module_source = span.source;
    success = true;
cleanup:
    r_context_free(context, children.items);
    return success;
}

/* R-TYPE-0051: the members of every interface are the closed types the program converts to it,
 * and every dispatching application is bound to the implementation of each member. Binding may
 * instantiate default or generic methods whose bodies convert further types, so the caller
 * repeats until nothing changes. */
static bool r_dyn_resolve_targets(RFrontendContext *context, bool *changed) {
    *changed = false;
    for (size_t index = 0U; index < context->hir_node_count; ++index) {
        const RHirNode node = context->hir_nodes[index];
        const RTypeId borrowed = r_dyn_referent(context, node.type);
        const RTypeId interface =
            borrowed != R_TYPE_ID_INVALID ? borrowed : r_dyn_owned(context, node.type);
        if (node.kind != R_HIR_CAST || node.child_count != 1U || interface == R_TYPE_ID_INVALID)
            continue;
        const RHirNode *child = &context->hir_nodes[context->hir_children[node.first_child] - 1U];
        const RSemanticType *holder =
            r_semantic_type(context, r_semantic_value_type(context, child->type));
        if (holder == NULL ||
            holder->kind != r_semantic_value_kind(context, node.type) ||
            r_generic_type_is_dependent(context, holder->base, 0U))
            continue;
        /* R-TYPE-0055 (L29): a narrowed interface, directly or through a borrow of an owner,
           has every member of the wider one; a borrow of an owner of the same interface adds
           none. */
        const RTypeId viewed = r_dyn_owned(context, holder->base);
        const RTypeId wider = viewed != R_TYPE_ID_INVALID ? viewed
                              : r_dyn_contract(context, holder->base) != NULL
                                  ? r_semantic_value_type(context, holder->base)
                                  : R_TYPE_ID_INVALID;
        if (wider == interface)
            continue;
        const size_t candidates = wider == R_TYPE_ID_INVALID ? 1U : context->dyn_member_count;
        for (size_t candidate = 0U; candidate < candidates; ++candidate) {
            if (wider != R_TYPE_ID_INVALID && context->dyn_members[candidate].interface != wider)
                continue;
            const RTypeId member =
                wider != R_TYPE_ID_INVALID
                    ? context->dyn_members[candidate].member
                    : r_semantic_representation_type(
                          context, r_semantic_value_type(context, holder->base));
            bool known = false;
            for (size_t entry = 0U; !known && entry < context->dyn_member_count; ++entry)
                known = context->dyn_members[entry].interface == interface &&
                        context->dyn_members[entry].member == member;
            if (known)
                continue;
            if (!r_grow_array(context,
                              (void **)&context->dyn_members,
                              &context->dyn_member_capacity,
                              sizeof(*context->dyn_members),
                              context->dyn_member_count + 1U))
                return false;
            context->dyn_members[context->dyn_member_count++] =
                (RDynMember){interface, member, 0U};
            *changed = true;
        }
    }
    for (size_t index = 0U; index < context->hir_node_count; ++index) {
        const RHirNode node = context->hir_nodes[index];
        if ((node.kind != R_HIR_CALL && node.kind != R_HIR_ASYNC_START) ||
            !r_dyn_dispatcher(context, node.symbol))
            continue;
        if (context->semantic_symbols[context->semantic_symbols[node.symbol - 1U].generic_origin -
                                      1U]
                .is_synthetic &&
            !r_dyn_synthetic_dispatcher(context, node.symbol, node.span))
            return false;
        const RSemanticSymbol dispatcher = context->semantic_symbols[node.symbol - 1U];
        const RTypeId interface = context->generic_arguments[dispatcher.first_generic_argument];
        const uint32_t count =
            context->generic_schemas[dispatcher.generic_schema - 1U].parameter_count;
        for (size_t entry = 0U; entry < context->dyn_member_count; ++entry) {
            const RDynMember member = context->dyn_members[entry];
            bool bound = false;
            if (member.interface != interface)
                continue;
            for (size_t target = 0U; !bound && target < context->dyn_target_count; ++target)
                bound = context->dyn_targets[target].dispatcher == node.symbol &&
                        context->dyn_targets[target].member == member.member;
            if (bound)
                continue;
            RTypeVector arguments = {0};
            RSymbolId target = R_SYMBOL_ID_INVALID;
            bool applied = r_type_vector_push(context, &arguments, member.member);
            for (uint32_t argument = 1U; applied && argument < count; ++argument)
                applied = r_type_vector_push(
                    context,
                    &arguments,
                    context->generic_arguments[dispatcher.first_generic_argument + argument]);
            applied = applied &&
                      r_generic_bind_trait_call(
                          context, dispatcher.generic_origin, member.member, &target, &arguments);
            r_context_free(context, arguments.items);
            if ((!applied || (target == R_SYMBOL_ID_INVALID)) &&
                (context->resource_status == R_FRONTEND_OK) &&
                (context->semantic_symbols[dispatcher.generic_origin - 1U].trait ==
                 context->core_format_trait) &&
                (context->core_format_trait != 0U) &&
                r_semantic_type_has_standard_format(context, member.member)) {
                /* R-TYPE-0046 (L32): a member with standard formatting is formatted by the
                   format glue of its type; the prototype stands for it as the target. */
                if (!r_format_bind_type(context, member.member, 0U))
                    return false;
                applied = true;
                target = dispatcher.generic_origin;
            }
            if (!applied || target == R_SYMBOL_ID_INVALID) {
                if (context->resource_status != R_FRONTEND_OK)
                    return false;
                if (!r_semantic_add_diagnostic(context,
                                               "R-DIAG-TRAIT-001",
                                               "R-TYPE-0051",
                                               "a dyn interface method cannot be bound to the "
                                               "implementation of one of its members",
                                               node.span))
                    return false;
                continue;
            }
            if (!r_grow_array(context,
                              (void **)&context->dyn_targets,
                              &context->dyn_target_capacity,
                              sizeof(*context->dyn_targets),
                              context->dyn_target_count + 1U))
                return false;
            context->dyn_targets[context->dyn_target_count++] =
                (RDynTarget){node.symbol, member.member, target, {0U, 0U, 0U}};
            *changed = true;
        }
    }
    return true;
}

typedef struct RDynOrder {
    RGenericText name;
    size_t entry;
} RDynOrder;

static int r_dyn_compare_order(const void *left, const void *right) {
    return r_dyn_compare_text(&((const RDynOrder *)left)->name, &((const RDynOrder *)right)->name);
}

/* R-TYPE-0051: the members of an interface are tagged in the order of their canonical names, so
 * the tags do not depend on the order of sources or declarations. */
static bool r_dyn_assign_tags(RFrontendContext *context) {
    RDynOrder *order = NULL;
    size_t count = 0U, capacity = 0U;
    bool success = false;
    for (size_t first = 0U; first < context->dyn_member_count; ++first) {
        const RTypeId interface = context->dyn_members[first].interface;
        bool seen = false;
        for (size_t previous = 0U; !seen && previous < first; ++previous)
            seen = context->dyn_members[previous].interface == interface;
        if (seen)
            continue;
        count = 0U;
        for (size_t entry = first; entry < context->dyn_member_count; ++entry) {
            if (context->dyn_members[entry].interface != interface)
                continue;
            if (!r_grow_array(context, (void **)&order, &capacity, sizeof(*order), count + 1U))
                goto cleanup;
            order[count] = (RDynOrder){{context, NULL, 0U, 0U, false}, entry};
            if (!r_generic_type_name(&order[count++].name, context->dyn_members[entry].member, 0U))
                goto cleanup;
        }
        qsort(order, count, sizeof(*order), r_dyn_compare_order);
        for (size_t tag = 0U; tag < count; ++tag) {
            context->dyn_members[order[tag].entry].tag = (uint32_t)tag;
            r_context_free(context, order[tag].name.bytes);
            order[tag].name.bytes = NULL;
        }
    }
    success = true;
cleanup:
    for (size_t entry = 0U; order != NULL && entry < count; ++entry)
        r_context_free(context, order[entry].name.bytes);
    r_context_free(context, order);
    return success;
}

RTypeId r_semantic_dyn_referent(const RFrontendContext *context, RTypeId type_id) {
    return r_dyn_referent(context, type_id);
}

RTypeId r_semantic_dyn_owned(const RFrontendContext *context, RTypeId type_id) {
    return r_dyn_owned(context, type_id);
}

bool r_semantic_dyn_dispatcher(const RFrontendContext *context, RSymbolId symbol) {
    return r_dyn_dispatcher(context, symbol);
}

/* R-TYPE-0046 (L32): whether a dispatch target stands for the format glue of a member with
   standard formatting: the core::Format prototype itself. */
bool r_semantic_dyn_format_target(const RFrontendContext *context, RSymbolId target) {
    if ((target == R_SYMBOL_ID_INVALID) || ((size_t)target > context->semantic_symbol_count) ||
        (context->core_format_trait == 0U)) {
        return false;
    }
    const RSemanticSymbol *symbol = &context->semantic_symbols[target - 1U];
    return symbol->is_trait_prototype && !symbol->has_definition &&
           (symbol->generic_origin == R_SYMBOL_ID_INVALID) &&
           (symbol->trait == context->core_format_trait);
}

bool r_semantic_dyn_tag(const RFrontendContext *context,
                        RTypeId interface,
                        RTypeId member,
                        uint32_t *tag) {
    member = r_semantic_representation_type(context, r_semantic_value_type(context, member));
    for (size_t entry = 0U; entry < context->dyn_member_count; ++entry)
        if (context->dyn_members[entry].interface == interface &&
            context->dyn_members[entry].member == member) {
            *tag = context->dyn_members[entry].tag;
            return true;
        }
    return false;
}

/* Whether `type` names the implementing type `self` other than through a projection. */
static bool
r_dyn_type_mentions(const RFrontendContext *context, RTypeId type, RTypeId self, uint32_t depth) {
    const RSemanticType *description = r_semantic_type(context, type);
    if (description == NULL || depth > context->options.limits.max_nesting)
        return false;
    if (type == self)
        return true;
    if (description->kind == R_SEMANTIC_TYPE_STRUCT || description->kind == R_SEMANTIC_TYPE_ENUM) {
        const RSemanticAggregate *aggregate = r_semantic_aggregate_for_type(context, type);
        if (aggregate == NULL || aggregate->generic_origin == 0U)
            return false;
        const uint32_t count =
            context->generic_schemas[aggregate->generic_schema - 1U].parameter_count;
        for (uint32_t index = 0U; index < count; ++index)
            if (r_dyn_type_mentions(
                    context,
                    context->generic_arguments[aggregate->first_generic_argument + index],
                    self,
                    depth + 1U))
                return true;
        return false;
    }
    return description->kind >= R_SEMANTIC_TYPE_FIXED_ARRAY &&
           description->kind != R_SEMANTIC_TYPE_CONSTANT_EXPR &&
           (r_dyn_type_mentions(context, description->base, self, depth + 1U) ||
            r_dyn_type_mentions(context, description->second, self, depth + 1U));
}

/* R-TYPE-0051: every method a dyn interface provides takes its receiver by borrow, has no
 * generic parameters of its own and mentions Self nowhere else, so one call has one signature for
 * every member. */
static bool r_dyn_validate_contracts(RFrontendContext *context) {
    for (size_t index = 0U; index < context->generic_parameter_count; ++index) {
        const RGenericParameter contract = context->generic_parameters[index];
        uint32_t *traits = NULL;
        size_t count = 0U, capacity = 0U;
        bool success = true;
        if (contract.dyn_key == 0U)
            continue;
        for (uint32_t bound = 0U; bound < contract.trait_constraint_count; ++bound) {
            const uint32_t origin = r_semantic_trait_origin(
                context,
                context->generic_trait_constraints[contract.first_trait_constraint + bound]);
            const RSemanticTrait declaration = context->semantic_traits[origin - 1U];
            for (uint32_t parent = 0U; success && parent <= declaration.supertrait_count;
                 ++parent) {
                const uint32_t trait =
                    parent == 0U
                        ? origin
                        : r_semantic_trait_origin(
                              context,
                              context->generic_trait_constraints[declaration.first_supertrait +
                                                                 parent - 1U]);
                bool seen = false;
                for (size_t previous = 0U; !seen && previous < count; ++previous)
                    seen = traits[previous] == trait;
                if (seen || trait == 0U)
                    continue;
                success =
                    r_grow_array(context, (void **)&traits, &capacity, sizeof(*traits), count + 1U);
                if (success)
                    traits[count++] = trait;
            }
        }
        for (size_t entry = 0U; success && entry < count; ++entry) {
            const RSemanticTrait trait = context->semantic_traits[traits[entry] - 1U];
            for (size_t method = 0U; success && method < context->semantic_method_count; ++method) {
                const RSemanticMethod record = context->semantic_methods[method];
                if (record.trait != traits[entry] || record.impl != 0U)
                    continue;
                const RSemanticSymbol function = context->semantic_symbols[record.function - 1U];
                const char *reason = NULL;
                if (function.receiver_kind != R_SEMANTIC_RECEIVER_SHARED &&
                    function.receiver_kind != R_SEMANTIC_RECEIVER_EXCLUSIVE)
                    reason = "a method of a dyn interface takes its receiver as const Self* or "
                             "Self*";
                else if (function.generic_schema != trait.generic_schema)
                    reason = "a method of a dyn interface has no generic parameters of its own";
                else {
                    bool mentions =
                        r_dyn_type_mentions(context, function.return_type, trait.self_type, 0U) ||
                        r_dyn_type_mentions(context, function.throws_type, trait.self_type, 0U);
                    for (uint32_t parameter = 1U; !mentions && parameter < function.parameter_count;
                         ++parameter)
                        mentions = r_dyn_type_mentions(
                            context,
                            context->semantic_parameter_types[function.first_parameter_type +
                                                              parameter],
                            trait.self_type,
                            0U);
                    if (mentions)
                        reason = "a method of a dyn interface mentions Self only in its receiver";
                }
                if (reason != NULL)
                    success = r_semantic_add_diagnostic(
                        context, "R-DIAG-TRAIT-001", "R-TYPE-0051", reason, contract.name_span);
            }
        }
        r_context_free(context, traits);
        if (!success)
            return false;
    }
    return true;
}

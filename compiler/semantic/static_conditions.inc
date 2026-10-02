/* R-META-0001..0003: predicates carry compile-time evidence, never runtime values. */
static bool r_static_text(RFrontendContext *context,
                          RSemanticByteVector *buffer,
                          const char *text,
                          size_t length) {
    if (!r_semantic_byte_vector_append(context, buffer, (const uint8_t *)text, length) ||
        !r_grow_array(context,
                      (void **)&buffer->items,
                      &buffer->capacity,
                      sizeof(*buffer->items),
                      buffer->count + 1U))
        return false;
    buffer->items[buffer->count] = 0U;
    return true;
}

static bool r_static_error(RFrontendContext *context, RSourceSpan span, const char *message) {
    return r_semantic_add_diagnostic(context, "R-DIAG-META-001", "R-META-0001", message, span);
}

static RTypeId r_static_origin(const RFrontendContext *context, RTypeId id) {
    uint32_t depth;
    for (depth = 0U; depth <= context->options.limits.max_nesting; ++depth) {
        const RSemanticType *type = r_semantic_type(context, id);
        if (type == NULL || type->kind != R_SEMANTIC_TYPE_PARAMETER || type->base == 0U ||
            type->base > context->generic_parameter_count ||
            context->generic_parameters[type->base - 1U].refinement_origin == 0U) {
            break;
        }
        id = context->generic_parameters[type->base - 1U].refinement_origin;
    }
    return id;
}

static bool r_static_append(RFrontendContext *context, RStaticPredicate predicate, uint32_t *id) {
    if (context->static_predicate_count >= UINT32_MAX ||
        !r_grow_array(context,
                      (void **)&context->static_predicates,
                      &context->static_predicate_capacity,
                      sizeof(*context->static_predicates),
                      context->static_predicate_count + 1U)) {
        return false;
    }
    context->static_predicates[context->static_predicate_count++] = predicate;
    *id = (uint32_t)context->static_predicate_count;
    return true;
}

static bool r_static_same(const RFrontendContext *context, uint32_t a, uint32_t b) {
    const RStaticPredicate *left = &context->static_predicates[a - 1U];
    const RStaticPredicate *right = &context->static_predicates[b - 1U];
    return left->kind == right->kind && left->property == right->property &&
           left->trait == right->trait && left->expression.source == right->expression.source &&
           left->expression.node == right->expression.node &&
           r_static_origin(context, left->type) == r_static_origin(context, right->type) &&
           r_static_origin(context, left->other_type) ==
               r_static_origin(context, right->other_type);
}

/* -1 is dependent, 0 and 1 are proven. A clone supplies closed schema arguments. */
static bool r_static_evaluate(RFrontendContext *context,
                              uint32_t id,
                              uint32_t schema,
                              uint32_t arguments,
                              uint32_t depth,
                              int *result) {
    RStaticPredicate predicate;
    int left, right;
    const RSemanticType *type;
    *result = -1;
    if (depth > context->options.limits.max_nesting) {
        (void)r_add_diagnostic(context,
                               "R-DIAG-LIMIT-001",
                               "R-LIMIT-0003",
                               "static predicate nesting limit exceeded",
                               R_DIAGNOSTIC_ERROR,
                               id != 0U && id <= context->static_predicate_count
                                   ? context->static_predicates[id - 1U].span
                                   : (RSourceSpan){0});
        context->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
        return false;
    }
    if (id == 0U || id > context->static_predicate_count)
        return false;
    predicate = context->static_predicates[id - 1U];
    if (predicate.kind == R_STATIC_CONSTANT) {
        *result = predicate.value ? 1 : 0;
        return true;
    }
    if (predicate.kind == R_STATIC_VALUE) {
        /* A constant condition still open here depends on generic parameters or waits for a
           discovery pass; an instantiation decides it with its closed arguments. */
        return schema == 0U || predicate.condition == R_HIR_NODE_ID_INVALID ||
               r_consteval_static_choice(context, id, schema, arguments, result);
    }
    if (predicate.kind == R_STATIC_AND || predicate.kind == R_STATIC_OR ||
        predicate.kind == R_STATIC_NOT) {
        if (!r_static_evaluate(context, predicate.left, schema, arguments, depth + 1U, &left)) {
            return false;
        }
        if (predicate.kind == R_STATIC_NOT) {
            *result = left < 0 ? -1 : 1 - left;
            return true;
        }
        if (!r_static_evaluate(context, predicate.right, schema, arguments, depth + 1U, &right)) {
            return false;
        }
        if (predicate.kind == R_STATIC_AND) {
            *result = left == 0 || right == 0 ? 0 : (left == 1 && right == 1 ? 1 : -1);
        } else {
            *result = left == 1 || right == 1 ? 1 : (left == 0 && right == 0 ? 0 : -1);
        }
        return true;
    }
    if (schema != 0U) {
        if (!r_generic_substitute_type(
                context, predicate.type, schema, arguments, &predicate.type) ||
            !r_generic_substitute_type(
                context, predicate.other_type, schema, arguments, &predicate.other_type)) {
            return false;
        }
        if (predicate.kind == R_STATIC_TRAIT &&
            !r_semantic_effective_trait(
                context, predicate.trait, schema, arguments, &predicate.trait))
            return false;
        if (predicate.kind == R_STATIC_TRAIT &&
            context->semantic_traits[predicate.trait - 1U].callable_signature != 0U) {
            const RSemanticTrait trait = context->semantic_traits[predicate.trait - 1U];
            RTypeId signature;
            if (!r_generic_substitute_type(
                    context, trait.callable_signature, schema, arguments, &signature) ||
                !r_semantic_callable_trait(
                    context, trait.module_source, signature, &predicate.trait)) {
                return false;
            }
        }
    }
    type = r_semantic_type(context, predicate.type);
    if (schema == 0U && type != NULL && type->kind == R_SEMANTIC_TYPE_PARAMETER &&
        type->base != 0U && type->base <= context->generic_parameter_count) {
        const RGenericParameter parameter = context->generic_parameters[type->base - 1U];
        uint32_t index;
        for (index = 0U; index < parameter.static_fact_count; ++index) {
            const RStaticFact fact = context->static_facts[parameter.first_static_fact + index];
            if (r_static_same(context, id, fact.predicate)) {
                *result = fact.value ? 1 : 0;
                return true;
            }
        }
        if (predicate.kind == R_STATIC_PROPERTY &&
            (parameter.excluded_constraints & predicate.property) != 0U) {
            *result = 0;
            return true;
        }
    }
    if (predicate.kind == R_STATIC_PROPERTY) {
        if (r_generic_has_property(context, predicate.type, predicate.property, 0U)) {
            *result = 1;
        } else if (!r_generic_type_is_dependent(context, predicate.type, 0U)) {
            *result = 0;
        }
    } else if (predicate.kind == R_STATIC_TRAIT) {
        if (r_semantic_type_implements_trait(context, predicate.type, predicate.trait)) {
            *result = 1;
        } else if (!r_generic_type_is_dependent(context, predicate.type, 0U)) {
            *result = 0;
        }
    } else if (predicate.kind == R_STATIC_IDENTITY) {
        if (predicate.type == predicate.other_type) {
            *result = 1;
        } else if (!r_generic_type_is_dependent(context, predicate.type, 0U) &&
                   !r_generic_type_is_dependent(context, predicate.other_type, 0U)) {
            *result = 0;
        }
    }
    return true;
}

/* A bare qualified name may designate a capability or trait before it designates a type. */
static bool
r_static_named_rhs(RFrontendContext *context, RAstRef ref, uint32_t *property, uint32_t *trait) {
    static const char *const properties[] = {
        "copy", "pod", "send", "sync", "key", "error", "unborrowed", "json_encode", "json_decode",
        "", "clone"};
    RAstNodeView view;
    const RSource *source = r_get_source_const(context, ref.source);
    uint32_t names[64];
    size_t count = 0U, index;
    RSemanticByteVector path = {0};
    bool qualified = false, valid = true, ambiguous = false, success = false;
    *property = 0U;
    *trait = 0U;
    if (!r_ast_ref_view(context, ref, &view)) {
        return false;
    }
    for (index = 0U; index < source->token_count; ++index) {
        const RToken *token = &source->tokens[index];
        if (token->span.start < view.span.start)
            continue;
        if (token->span.start >= view.span.end)
            break;
        if (r_token_is_trivia(token->kind))
            continue;
        if (token->kind == R_TOKEN_IDENTIFIER && count < sizeof(names) / sizeof(names[0])) {
            names[count++] = token->intern_id;
        } else if (token->kind == R_TOKEN_COLON_COLON) {
            qualified = true;
        } else if (token->kind != R_TOKEN_DOT) {
            valid = false;
        }
    }
    if (!valid) {
        uint32_t declaration;
        if (!r_semantic_probe_trait_designator(context, ref, &declaration))
            return false;
        if (declaration != 0U)
            return r_semantic_trait_from_ast(context, ref, trait);
        return true;
    }
    if (count == 0U)
        return true;
    if (count == 1U) {
        const char *name;
        size_t length;
        if (!r_semantic_intern_text(context, names[0], &name, &length))
            return false;
        for (index = 0U; index < sizeof(properties) / sizeof(properties[0]); ++index) {
            if (strlen(properties[index]) == length &&
                memcmp(name, properties[index], length) == 0) {
                *property = UINT32_C(1) << index;
                return true;
            }
        }
        *trait = r_semantic_resolve_trait_name(context, ref.source, names[0], &ambiguous);
        if (*trait != 0U && context->semantic_traits[*trait - 1U].type_parameter_schema != 0U)
            return r_static_error(context, view.span, "a generic trait requires type arguments");
        return !ambiguous ||
               r_static_error(context, view.span, "ambiguous trait in static predicate");
    }
    if (!qualified)
        return true;
    for (index = 0U; index + 1U < count; ++index) {
        const char *name;
        size_t length;
        if (!r_semantic_intern_text(context, names[index], &name, &length) ||
            (index != 0U && !r_static_text(context, &path, ".", 1U)) ||
            !r_static_text(context, &path, name, length))
            goto cleanup;
    }
    if (path.count == 4U && memcmp(path.items, "core", 4U) == 0) {
        *trait = r_semantic_resolve_core_trait_name(context, names[count - 1U]);
    } else {
        uint32_t module;
        if (!r_intern_bytes(context, (const uint8_t *)path.items, path.count, &module))
            goto cleanup;
        *trait =
            r_semantic_resolve_trait_name_in_module(context, ref.source, module, names[count - 1U]);
    }
    if (*trait != 0U && context->semantic_traits[*trait - 1U].type_parameter_schema != 0U) {
        success = r_static_error(context, view.span, "a generic trait requires type arguments");
        goto cleanup;
    }
    success = true;
cleanup:
    r_context_free(context, path.items);
    return success;
}

static bool r_static_type(RFrontendContext *context, RAstRef ref, RTypeId *type) {
    RAstNodeView view;
    *type = R_TYPE_ID_INVALID;
    if (r_semantic_type_from_ast(context, ref, type))
        return true;
    if (context->resource_status != R_FRONTEND_OK || !r_ast_ref_view(context, ref, &view))
        return false;
    *type = R_TYPE_ID_INVALID;
    return r_static_error(context, view.span, "unresolved or invalid type in static predicate");
}

static bool r_static_parse(
    RFrontendContext *context, RAstRef ref, bool module, uint32_t depth, uint32_t *result) {
    RAstNodeView view;
    RStaticPredicate predicate = {0};
    RAstRef types[2] = {{0}}, callable = {0}, condition = {0};
    uint32_t index, type_count = 0U, first = 0U;
    RAstTokenView tokens[32];
    size_t token_count = 0U;
    bool negate = false;
    *result = 0U;
    if (depth > r_frontend_tree_depth_limit(context) || !r_ast_ref_view(context, ref, &view))
        return false;
    predicate.span = view.span;
    for (index = 0U; index < view.child_count; ++index) {
        RAstRef child;
        RAstNodeView child_view;
        RAstTokenView token;
        if (!r_ast_ref_child(context, ref, index, &child) ||
            !r_ast_ref_view(context, child, &child_view))
            return false;
        if (child_view.kind == R_SYNTAX_STATIC_PREDICATE ||
            child_view.kind == R_SYNTAX_STATIC_CONJUNCTION ||
            child_view.kind == R_SYNTAX_STATIC_ATOM) {
            uint32_t next;
            if (!r_static_parse(context, child, module, depth + 1U, &next))
                return false;
            if (next == 0U)
                return true;
            if (first == 0U)
                first = next;
            else {
                predicate.kind =
                    view.kind == R_SYNTAX_STATIC_PREDICATE ? R_STATIC_OR : R_STATIC_AND;
                predicate.left = first;
                predicate.right = next;
                if (!r_static_append(context, predicate, &first))
                    return false;
            }
        } else if (child_view.kind == R_SYNTAX_TYPE && type_count < 2U) {
            types[type_count++] = child;
        } else if (child_view.kind == R_SYNTAX_CALLABLE_CONSTRAINT) {
            callable = child;
        } else if (child_view.kind == R_SYNTAX_EXPRESSION) {
            condition = child;
        } else if (r_ast_ref_token(context, child, &token)) {
            negate = negate || token.kind == R_TOKEN_BANG;
            if (token_count < sizeof(tokens) / sizeof(tokens[0]))
                tokens[token_count++] = token;
        }
    }
    if (first != 0U) {
        if (!negate) {
            *result = first;
            return true;
        }
        predicate.kind = R_STATIC_NOT;
        predicate.left = first;
        return r_static_append(context, predicate, result);
    }
    if (condition.node != 0U) {
        /* A condition in a body is lowered in its scope (r_static_resolve_values); a module
           condition was computed by the discovery pass. */
        predicate.kind = R_STATIC_VALUE;
        predicate.expression = condition;
        if (module) {
            int truth;
            bool reported;
            if (!r_consteval_module_condition(context, condition, view.span, &truth, &reported))
                return false;
            if (reported)
                return true;
            if (truth >= 0) {
                predicate.kind = R_STATIC_CONSTANT;
                predicate.value = truth == 1;
            }
        }
        return r_static_append(context, predicate, result);
    }
    if (type_count == 0U) {
        const char *expected;
        RSemanticByteVector spelling = {0};
        bool success = false;
        if (token_count < 4U)
            return r_static_error(context, view.span, "invalid static configuration predicate");
        predicate.kind = R_STATIC_CONSTANT;
        if (r_semantic_token_text_equal_owned(context, &tokens[1], "target")) {
            uint32_t intern;
            size_t length;
            const char *decoded;
            size_t decoded_length;
            expected = r_target_identity_triple;
            if (!r_intern_string_sequence(context, tokens[3].span, &intern, &length) ||
                !r_semantic_intern_text(context, intern, &decoded, &decoded_length))
                return false;
            predicate.value = expected != NULL && strlen(expected) == decoded_length &&
                              memcmp(expected, decoded, decoded_length) == 0;
        } else {
            expected = r_reflection_profile_name(context->profile);
            for (index = 3U; index < token_count; ++index) {
                const RSource *source = r_get_source_const(context, tokens[index].span.source);
                if (!r_static_text(context,
                                   &spelling,
                                   (const char *)source->bytes + tokens[index].span.start,
                                   tokens[index].span.end - tokens[index].span.start))
                    goto config_cleanup;
            }
            if (strcmp((const char *)spelling.items, "freestanding") != 0 &&
                strcmp((const char *)spelling.items, "allocation") != 0 &&
                strcmp((const char *)spelling.items, "hosted") != 0 &&
                strcmp((const char *)spelling.items, "hosted-thread") != 0 &&
                strcmp((const char *)spelling.items, "hosted-native-async") != 0) {
                success = r_static_error(context, view.span, "unknown static profile name");
                goto config_cleanup;
            }
            predicate.value = strcmp(expected, (const char *)spelling.items) == 0;
        }
        success = r_static_append(context, predicate, result);
    config_cleanup:
        r_context_free(context, spelling.items);
        return success;
    }
    if (module)
        return r_static_error(context,
                              view.span,
                              "module static predicates require core::profile, core::target or "
                              "a constant condition");
    if (!r_static_type(context, types[0], &predicate.type))
        return false;
    if (predicate.type == 0U)
        return true;
    if (callable.node != 0U) {
        RTypeVector parameters = {0};
        RTypeId signature;
        bool success = false;
        if (!r_ast_ref_view(context, callable, &view))
            return false;
        for (index = 0U; index < view.child_count; ++index) {
            RAstRef child;
            RAstNodeView child_view;
            RTypeId type;
            if (!r_ast_ref_child(context, callable, index, &child) ||
                !r_ast_ref_view(context, child, &child_view))
                goto callable_cleanup;
            if (child_view.kind != R_SYNTAX_TYPE)
                continue;
            if (!r_static_type(context, child, &type))
                goto callable_cleanup;
            if (type == R_TYPE_ID_INVALID) {
                success = true;
                goto callable_cleanup;
            }
            if (!r_type_vector_push(context, &parameters, type))
                goto callable_cleanup;
        }
        if (parameters.count == 0U)
            goto callable_cleanup;
        predicate.other_type = parameters.items[--parameters.count];
        uint32_t resource_flags;
        RTypeId effects;
        bool effects_supported;
        const bool asynchronous =
            r_semantic_type_has_direct_token(context, callable, R_TOKEN_KW_ASYNC);
        if (!r_semantic_collect_throws(
                context, callable, asynchronous, &effects, &effects_supported))
            goto callable_cleanup;
        if (!effects_supported) {
            success = true;
            goto callable_cleanup;
        }
        bool valid_signature = r_closure_sync_result(context, predicate.other_type);
        for (size_t parameter = 0U; parameter < parameters.count; ++parameter)
            valid_signature =
                valid_signature && r_closure_sync_input(context, parameters.items[parameter]);
        if (!valid_signature ||
            (asynchronous &&
             (r_closure_call_mode(context, callable) != R_SEMANTIC_RECEIVER_VALUE ||
              !r_closure_async_signature(context, &parameters, predicate.other_type, effects)))) {
            success = r_static_error(context, view.span, "invalid callable predicate signature");
            goto callable_cleanup;
        }
        if (!r_semantic_callable_resources(context, callable, &resource_flags) ||
            !r_semantic_callable_signature(context,
                                           &parameters,
                                           predicate.other_type,
                                           r_closure_call_mode(context, callable),
                                           resource_flags,
                                           effects,
                                           asynchronous,
                                           &signature) ||
            !r_semantic_callable_trait(context, ref.source, signature, &predicate.trait))
            goto callable_cleanup;
        predicate.other_type = 0U;
        predicate.kind = R_STATIC_TRAIT;
        success = r_static_append(context, predicate, result);
    callable_cleanup:
        r_context_free(context, parameters.items);
        return success;
    }
    if (type_count != 2U)
        return r_static_error(context, predicate.span, "missing static predicate operand");
    if (!r_static_named_rhs(context, types[1], &predicate.property, &predicate.trait))
        return false;
    if (predicate.property != 0U)
        predicate.kind = R_STATIC_PROPERTY;
    else if (predicate.trait != 0U)
        predicate.kind = R_STATIC_TRAIT;
    else {
        predicate.kind = R_STATIC_IDENTITY;
        if (!r_static_type(context, types[1], &predicate.other_type))
            return false;
        if (predicate.other_type == 0U)
            return r_static_error(
                context, predicate.span, "unresolved type or trait in static predicate");
    }
    return r_static_append(context, predicate, result);
}

/*
 * R-META-0002: lowers every constant condition of a body predicate in the scope of the `@if`.
 * A closed condition becomes a constant now; a condition over generic parameters keeps its
 * checked HIR for instantiation (R-META-0003). `*id` becomes zero after a diagnostic.
 */
static bool
r_static_resolve_values(RBodyContext *body, uint32_t *id, uint32_t root, uint32_t depth) {
    RFrontendContext *context = body->frontend;
    RStaticPredicate predicate;
    RExpressionResult condition;
    bool dependent, valid, value;
    if (depth > context->options.limits.max_nesting || root == 0U ||
        root > context->static_predicate_count)
        return false;
    predicate = context->static_predicates[root - 1U];
    if (predicate.kind == R_STATIC_AND || predicate.kind == R_STATIC_OR ||
        predicate.kind == R_STATIC_NOT) {
        return r_static_resolve_values(body, id, predicate.left, depth + 1U) &&
               (predicate.kind == R_STATIC_NOT ||
                r_static_resolve_values(body, id, predicate.right, depth + 1U));
    }
    if (predicate.kind != R_STATIC_VALUE || *id == 0U)
        return true;
    {
        /* A required constant: its calls throw only during translation (L22.1). */
        const bool translation_effects = body->translation_effects;
        bool lowered;
        body->translation_effects = true;
        lowered = r_body_lower_condition(body, predicate.expression, &condition);
        body->translation_effects = translation_effects;
        if (!lowered)
            return false;
    }
    if (!condition.valid) {
        *id = 0U;
        return true;
    }
    r_consteval_mark_reads(context, condition.node, 0U);
    if (!r_consteval_condition(context, condition.node, &dependent, &valid, &value))
        return false;
    if (!valid) {
        *id = 0U;
        return true;
    }
    predicate = context->static_predicates[root - 1U];
    if (dependent) {
        predicate.condition = condition.node;
    } else {
        predicate.kind = R_STATIC_CONSTANT;
        predicate.value = value;
    }
    context->static_predicates[root - 1U] = predicate;
    return true;
}

/* A module `@if` whose predicate has a constant condition, in any branch at module scope. */
static bool
r_static_module_condition(const RFrontendContext *context, RAstRef ref, uint32_t depth) {
    RAstNodeView view;
    uint32_t index;
    if (depth > r_frontend_tree_depth_limit(context) || !r_ast_ref_view(context, ref, &view))
        return false;
    for (index = 0U; index < view.child_count; ++index) {
        RAstRef child;
        RAstNodeView child_view;
        if (!r_ast_ref_child(context, ref, index, &child) ||
            !r_ast_ref_view(context, child, &child_view))
            return false;
        if (child_view.kind == R_SYNTAX_EXPRESSION && view.kind == R_SYNTAX_STATIC_ATOM)
            return true;
        if ((child_view.kind == R_SYNTAX_STATIC_IF_DECLARATION ||
             child_view.kind == R_SYNTAX_STATIC_DECLARATIONS ||
             child_view.kind == R_SYNTAX_STATIC_PREDICATE ||
             child_view.kind == R_SYNTAX_STATIC_CONJUNCTION ||
             child_view.kind == R_SYNTAX_STATIC_ATOM) &&
            r_static_module_condition(context, child, depth + 1U))
            return true;
    }
    return false;
}

static bool r_static_module_conditions(RFrontendContext *context, bool *present) {
    size_t index;
    *present = false;
    for (index = 0U; index < context->source_count && !*present; ++index) {
        RAstRef root;
        if (!r_ast_root_ref(context, (RSourceId)index + 1U, &root))
            return false;
        *present = r_static_module_condition(context, root, 0U);
    }
    return true;
}

typedef struct RStaticFacts {
    RStaticFact *items;
    size_t count;
    size_t capacity;
} RStaticFacts;

static bool r_static_fact_push(RFrontendContext *context, RStaticFacts *facts, RStaticFact fact) {
    size_t index;
    for (index = 0U; index < facts->count; ++index) {
        if (facts->items[index].value == fact.value &&
            r_static_same(context, facts->items[index].predicate, fact.predicate))
            return true;
    }
    if (!r_grow_array(context,
                      (void **)&facts->items,
                      &facts->capacity,
                      sizeof(*facts->items),
                      facts->count + 1U))
        return false;
    facts->items[facts->count++] = fact;
    return true;
}

static bool r_static_collect_facts(
    RFrontendContext *context, uint32_t id, bool value, RStaticFacts *facts, uint32_t depth) {
    const RStaticPredicate predicate = context->static_predicates[id - 1U];
    RStaticFacts left = {0}, right = {0};
    size_t i, j;
    bool success = false;
    int a, b;
    if (depth > context->options.limits.max_nesting)
        return false;
    /* A constant condition proves nothing about a type parameter. */
    if (predicate.kind == R_STATIC_CONSTANT || predicate.kind == R_STATIC_VALUE)
        return true;
    if (predicate.kind == R_STATIC_NOT) {
        return r_static_collect_facts(context, predicate.left, !value, facts, depth + 1U);
    }
    if (predicate.kind != R_STATIC_AND && predicate.kind != R_STATIC_OR) {
        const RStaticFact fact = {id, value};
        uint32_t implied = 0U;
        if (!r_static_fact_push(context, facts, fact))
            return false;
        if (predicate.kind == R_STATIC_PROPERTY) {
            if (value && predicate.property == R_GENERIC_POD)
                implied = R_GENERIC_COPY | R_GENERIC_UNBORROWED;
            if (!value && (predicate.property == R_GENERIC_COPY ||
                           predicate.property == R_GENERIC_UNBORROWED))
                implied = R_GENERIC_POD;
        }
        for (uint32_t property = R_GENERIC_COPY; property <= R_GENERIC_CLONE;
             property = r_generic_next_capability(property)) {
            if ((implied & property) != 0U) {
                RStaticPredicate consequence = predicate;
                RStaticFact inferred;
                consequence.property = property;
                inferred.value = value;
                if (!r_static_append(context, consequence, &inferred.predicate) ||
                    !r_static_fact_push(context, facts, inferred))
                    return false;
            }
        }
        return true;
    }
    if ((predicate.kind == R_STATIC_AND) == value) {
        return r_static_collect_facts(context, predicate.left, value, facts, depth + 1U) &&
               r_static_collect_facts(context, predicate.right, value, facts, depth + 1U);
    }
    if (!r_static_evaluate(context, predicate.left, 0U, 0U, 0U, &a) ||
        !r_static_evaluate(context, predicate.right, 0U, 0U, 0U, &b))
        return false;
    if (a >= 0 && (a == 1) != value) {
        return r_static_collect_facts(context, predicate.right, value, facts, depth + 1U);
    }
    if (b >= 0 && (b == 1) != value) {
        return r_static_collect_facts(context, predicate.left, value, facts, depth + 1U);
    }
    if (!r_static_collect_facts(context, predicate.left, value, &left, depth + 1U) ||
        !r_static_collect_facts(context, predicate.right, value, &right, depth + 1U))
        goto cleanup;
    /* A disjunction proves only the facts shared by all alternatives. */
    for (i = 0U; i < left.count; ++i) {
        for (j = 0U; j < right.count; ++j) {
            if (left.items[i].value == right.items[j].value &&
                r_static_same(context, left.items[i].predicate, right.items[j].predicate) &&
                !r_static_fact_push(context, facts, left.items[i]))
                goto cleanup;
        }
    }
    success = true;
cleanup:
    r_context_free(context, left.items);
    r_context_free(context, right.items);
    return success;
}

static bool r_static_refined_parameter(RFrontendContext *context,
                                       RTypeId original,
                                       const RStaticFacts *facts,
                                       RTypeId *result) {
    const RSemanticType *description = r_semantic_type(context, original);
    RGenericParameter parameter;
    RTypeVector traits = {0};
    RStaticFacts evidence = {0};
    RTypeId identity = 0U;
    size_t index;
    bool success = false;
    *result = original;
    if (description == NULL || description->kind != R_SEMANTIC_TYPE_PARAMETER ||
        description->base == 0U || description->base > context->generic_parameter_count)
        return true;
    parameter = context->generic_parameters[description->base - 1U];
    for (index = 0U; index < parameter.trait_constraint_count; ++index) {
        if (!r_type_vector_push(
                context,
                &traits,
                context->generic_trait_constraints[parameter.first_trait_constraint + index]))
            goto cleanup;
    }
    for (index = 0U; index < parameter.static_fact_count; ++index) {
        if (!r_static_fact_push(
                context, &evidence, context->static_facts[parameter.first_static_fact + index]))
            goto cleanup;
    }
    for (index = 0U; index < facts->count; ++index) {
        const RStaticFact fact = facts->items[index];
        const RStaticPredicate predicate = context->static_predicates[fact.predicate - 1U];
        if (r_static_origin(context, predicate.type) != r_static_origin(context, original))
            continue;
        for (size_t prior = 0U; prior < evidence.count; ++prior) {
            if (evidence.items[prior].value != fact.value &&
                r_static_same(context, evidence.items[prior].predicate, fact.predicate)) {
                success = r_static_error(
                    context, predicate.span, "incompatible static branch assumptions");
                goto cleanup;
            }
        }
        if (!r_static_fact_push(context, &evidence, fact))
            goto cleanup;
        if (predicate.kind == R_STATIC_PROPERTY) {
            if (fact.value)
                parameter.constraints |= predicate.property;
            else
                parameter.excluded_constraints |= predicate.property;
        } else if (predicate.kind == R_STATIC_TRAIT && fact.value) {
            if (!r_type_vector_push(context, &traits, predicate.trait))
                goto cleanup;
        } else if (predicate.kind == R_STATIC_IDENTITY && fact.value &&
                   r_static_origin(context, predicate.other_type) !=
                       r_static_origin(context, original)) {
            if (identity != 0U && identity != predicate.other_type &&
                !r_generic_type_is_dependent(context, identity, 0U) &&
                !r_generic_type_is_dependent(context, predicate.other_type, 0U)) {
                success = r_static_error(
                    context, predicate.span, "incompatible type identities in static branch");
                goto cleanup;
            }
            identity = predicate.other_type;
        }
    }
    if ((parameter.constraints & R_GENERIC_POD) != 0U)
        parameter.constraints |= R_GENERIC_COPY | R_GENERIC_UNBORROWED;
    if ((parameter.excluded_constraints & (R_GENERIC_COPY | R_GENERIC_UNBORROWED)) != 0U)
        parameter.excluded_constraints |= R_GENERIC_POD;
    if ((parameter.constraints & parameter.excluded_constraints) != 0U) {
        success =
            r_static_error(context, parameter.name_span, "incompatible static branch assumptions");
        goto cleanup;
    }
    if (identity != 0U) {
        /* Identity supplies the exact type, but may not contradict any surrounding contract. */
        uint32_t property;
        for (property = R_GENERIC_COPY; property <= R_GENERIC_CLONE;
             property = r_generic_next_capability(property)) {
            if (((parameter.constraints & property) != 0U &&
                 !r_generic_has_property(context, identity, property, 0U)) ||
                ((parameter.excluded_constraints & property) != 0U &&
                 r_generic_has_property(context, identity, property, 0U))) {
                success = r_static_error(context,
                                         parameter.name_span,
                                         "type identity contradicts static branch constraints");
                goto cleanup;
            }
        }
        for (index = 0U; index < traits.count; ++index) {
            if (!r_semantic_type_implements_trait(context, identity, traits.items[index])) {
                success = r_static_error(
                    context, parameter.name_span, "type identity contradicts static branch traits");
                goto cleanup;
            }
        }
        *result = identity;
        success = true;
        goto cleanup;
    }
    if (evidence.count == parameter.static_fact_count) {
        success = true;
        goto cleanup;
    }
    parameter.refinement_origin = r_static_origin(context, original);
    parameter.first_trait_constraint = (uint32_t)context->generic_trait_constraint_count;
    parameter.trait_constraint_count = (uint32_t)traits.count;
    parameter.first_static_fact = (uint32_t)context->static_fact_count;
    parameter.static_fact_count = (uint32_t)evidence.count;
    if (!r_grow_array(context,
                      (void **)&context->generic_trait_constraints,
                      &context->generic_trait_constraint_capacity,
                      sizeof(*context->generic_trait_constraints),
                      context->generic_trait_constraint_count + traits.count) ||
        !r_grow_array(context,
                      (void **)&context->static_facts,
                      &context->static_fact_capacity,
                      sizeof(*context->static_facts),
                      context->static_fact_count + evidence.count) ||
        !r_grow_array(context,
                      (void **)&context->generic_parameters,
                      &context->generic_parameter_capacity,
                      sizeof(*context->generic_parameters),
                      context->generic_parameter_count + 1U))
        goto cleanup;
    for (index = 0U; index < traits.count; ++index)
        context->generic_trait_constraints[context->generic_trait_constraint_count++] =
            traits.items[index];
    for (index = 0U; index < evidence.count; ++index)
        context->static_facts[context->static_fact_count++] = evidence.items[index];
    if (!r_semantic_intern_derived_type(context,
                                        R_SEMANTIC_TYPE_PARAMETER,
                                        (RTypeId)context->generic_parameter_count + 1U,
                                        0U,
                                        0U,
                                        R_SEMANTIC_TYPE_FLAG_NONE,
                                        &parameter.type))
        goto cleanup;
    context->generic_parameters[context->generic_parameter_count++] = parameter;
    *result = parameter.type;
    success = true;
cleanup:
    r_context_free(context, traits.items);
    r_context_free(context, evidence.items);
    return success;
}

static bool r_static_lower_branch(RBodyContext *body,
                                  RAstRef block,
                                  uint32_t predicate,
                                  bool value,
                                  RHirNodeId *node,
                                  bool *returns) {
    RFrontendContext *context = body->frontend;
    const RStaticPredicate condition = context->static_predicates[predicate - 1U];
    const uint32_t schema_id = r_generic_schema_at(context, condition.span);
    RGenericSchema schema;
    RStaticFacts facts = {0};
    RTypeVector originals = {0}, replacements = {0}, symbols = {0}, catches = {0};
    const RTypeId return_type = body->return_type, throws_type = body->throws_type,
                  catch_type = body->current_catch_type, associated = body->associated_owner_type;
    uint32_t arguments = 0U;
    size_t index, changed_symbols = 0U, changed_catches = 0U;
    bool parameters_changed = false, success = false;
    if (schema_id == 0U)
        return false;
    schema = context->generic_schemas[schema_id - 1U];
    if (!r_static_collect_facts(context, predicate, value, &facts, 0U))
        goto cleanup;
    for (index = 0U; index < schema.parameter_count; ++index) {
        const RTypeId original = context->generic_parameters[schema.first_parameter + index].type;
        RTypeId refined;
        if (!r_type_vector_push(context, &originals, original) ||
            !r_static_refined_parameter(context, original, &facts, &refined) ||
            !r_pack_refine_branch(context, predicate, value, refined, &refined) ||
            !r_type_vector_push(context, &replacements, refined))
            goto cleanup;
    }
    if (!r_grow_array(context,
                      (void **)&context->generic_arguments,
                      &context->generic_argument_capacity,
                      sizeof(*context->generic_arguments),
                      context->generic_argument_count + replacements.count))
        goto cleanup;
    arguments = (uint32_t)context->generic_argument_count;
    for (index = 0U; index < replacements.count; ++index) {
        context->generic_arguments[context->generic_argument_count++] = replacements.items[index];
        context->generic_parameters[schema.first_parameter + index].type =
            replacements.items[index];
    }
    parameters_changed = true;
    for (index = 0U; index < body->active_symbols.count; ++index) {
        const RSymbolId symbol = body->active_symbols.items[index];
        RTypeId refined;
        if (!r_type_vector_push(context, &symbols, context->semantic_symbols[symbol - 1U].type) ||
            !r_generic_substitute_type(
                context, symbols.items[index], schema_id, arguments, &refined))
            goto cleanup;
        context->semantic_symbols[symbol - 1U].type = refined;
        ++changed_symbols;
    }
    for (index = 0U; index < body->active_catch_types.count; ++index) {
        if (!r_type_vector_push(context, &catches, body->active_catch_types.items[index]) ||
            !r_generic_substitute_type(context,
                                       catches.items[index],
                                       schema_id,
                                       arguments,
                                       &body->active_catch_types.items[index]))
            goto cleanup;
        ++changed_catches;
    }
    if (!r_generic_substitute_type(
            context, return_type, schema_id, arguments, &body->return_type) ||
        !r_generic_substitute_type(
            context, throws_type, schema_id, arguments, &body->throws_type) ||
        !r_generic_substitute_type(
            context, catch_type, schema_id, arguments, &body->current_catch_type) ||
        !r_generic_substitute_type(
            context, associated, schema_id, arguments, &body->associated_owner_type))
        goto cleanup;
    success = r_body_lower_block(body, block, false, node, returns);
cleanup:
    body->return_type = return_type;
    body->throws_type = throws_type;
    body->current_catch_type = catch_type;
    body->associated_owner_type = associated;
    for (index = 0U; index < changed_symbols; ++index)
        context->semantic_symbols[body->active_symbols.items[index] - 1U].type =
            symbols.items[index];
    for (index = 0U; index < changed_catches; ++index)
        body->active_catch_types.items[index] = catches.items[index];
    if (parameters_changed) {
        for (index = 0U; index < originals.count; ++index)
            context->generic_parameters[schema.first_parameter + index].type =
                originals.items[index];
    }
    r_context_free(context, facts.items);
    r_context_free(context, originals.items);
    r_context_free(context, replacements.items);
    r_context_free(context, symbols.items);
    r_context_free(context, catches.items);
    return success;
}

static bool
r_static_flatten(RFrontendContext *context, RAstRef root, RTypeVector *nodes, uint32_t depth) {
    RAstNodeView view;
    uint32_t index;
    if (depth > r_frontend_tree_depth_limit(context) || !r_ast_ref_view(context, root, &view))
        return false;
    for (index = 0U; index < view.child_count; ++index) {
        RAstRef child;
        RAstNodeView child_view;
        if (!r_ast_ref_child(context, root, index, &child) ||
            !r_ast_ref_view(context, child, &child_view))
            return false;
        if (child_view.kind == R_SYNTAX_STATIC_IF_DECLARATION) {
            RAstRef predicate, branches[2] = {{0}};
            uint32_t id, branch, count = 0U;
            int truth;
            if (!r_ast_find_direct_child(context, child, R_SYNTAX_STATIC_PREDICATE, &predicate) ||
                !r_static_parse(context, predicate, true, 0U, &id))
                return false;
            if (id == 0U)
                continue;
            if (!r_static_evaluate(context, id, 0U, 0U, 0U, &truth))
                return false;
            for (branch = 0U; branch < child_view.child_count; ++branch) {
                RAstRef block;
                RAstNodeView block_view;
                if (!r_ast_ref_child(context, child, branch, &block) ||
                    !r_ast_ref_view(context, block, &block_view))
                    return false;
                if (block_view.kind == R_SYNTAX_STATIC_DECLARATIONS && count < 2U)
                    branches[count++] = block;
            }
            if (truth < 0 && context->consteval_discovery)
                continue; /* a later discovery pass selects a branch once the value is known */
            if (truth < 0)
                return r_static_error(
                    context, child_view.span, "module predicate must be statically resolved");
            if (branches[truth == 1 ? 0 : 1].node != 0U &&
                !r_static_flatten(context, branches[truth == 1 ? 0 : 1], nodes, depth + 1U))
                return false;
        } else if (!r_type_vector_push(context, nodes, child.node))
            return false;
    }
    return true;
}

static bool r_static_select_modules(RFrontendContext *context) {
    size_t index;
    for (index = 0U; index < context->source_count; ++index) {
        RAstRef root;
        RTypeVector nodes = {0};
        RSource *source;
        RAstNode selected;
        bool success;
        if (!r_ast_root_ref(context, (RSourceId)index + 1U, &root))
            return false;
        success = r_static_flatten(context, root, &nodes, 0U);
        source = &context->sources[index];
        selected = source->ast_nodes[root.node - 1U];
        if (success) {
            success = r_grow_array(context,
                                   (void **)&source->ast_nodes,
                                   &source->ast_node_capacity,
                                   sizeof(*source->ast_nodes),
                                   source->ast_node_count + 1U) &&
                      r_grow_array(context,
                                   (void **)&source->ast_children,
                                   &source->ast_child_capacity,
                                   sizeof(*source->ast_children),
                                   source->ast_child_count + nodes.count);
        }
        if (success) {
            size_t child;
            selected.first_child = (uint32_t)source->ast_child_count;
            selected.child_count = (uint32_t)nodes.count;
            for (child = 0U; child < nodes.count; ++child)
                source->ast_children[source->ast_child_count++] = nodes.items[child];
            source->ast_nodes[source->ast_node_count++] = selected;
            source->semantic_root = (RAstNodeId)source->ast_node_count;
        }
        r_context_free(context, nodes.items);
        if (!success)
            return false;
    }
    return true;
}

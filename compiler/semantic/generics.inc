/* Generic definitions retain their original AST and lexical module. Type arguments
 * substitute semantic types, never source text or tokens. */
static void r_generic_limit_diagnostic(RFrontendContext *, uint32_t, RSourceSpan);

static uint32_t r_generic_schema_at(const RFrontendContext *context, RSourceSpan span) {
    const uint32_t trait = r_semantic_trait_at(context, span);
    if (trait != 0U && context->semantic_traits[trait - 1U].type_parameter_schema != 0U)
        return context->semantic_traits[trait - 1U].generic_schema;
    size_t index;
    for (index = 0U; index < context->generic_schema_count; ++index) {
        const RGenericSchema *schema = &context->generic_schemas[index];
        if (!schema->associated_parameters && !schema->tuple &&
            schema->span.source == span.source && schema->span.start <= span.start &&
            span.end <= schema->span.end) {
            return (uint32_t)index + UINT32_C(1);
        }
    }
    return UINT32_C(0);
}

static uint32_t r_generic_parameter_constraints(const RFrontendContext *context, RTypeId id) {
    const RSemanticType *type = r_semantic_type(context, id);
    if (type == NULL || type->kind != R_SEMANTIC_TYPE_PARAMETER || type->base == 0U ||
        (size_t)type->base > context->generic_parameter_count) {
        return UINT32_C(0);
    }
    const RGenericParameter parameter = context->generic_parameters[type->base - 1U];
    uint32_t constraints = parameter.constraints;
    for (uint32_t index = 0U; index < parameter.trait_constraint_count; ++index) {
        const uint32_t trait_id =
            context->generic_trait_constraints[parameter.first_trait_constraint + index];
        const RSemanticType *self =
            r_semantic_type(context, context->semantic_traits[trait_id - 1U].self_type);
        if (self == NULL || self->kind != R_SEMANTIC_TYPE_PARAMETER)
            continue;
        const RGenericParameter bound = context->generic_parameters[self->base - 1U];
        constraints |= bound.constraints;
        for (uint32_t parent = 0U; parent < bound.trait_constraint_count; ++parent) {
            const uint32_t inherited =
                context->generic_trait_constraints[bound.first_trait_constraint + parent];
            const RSemanticType *ancestor =
                r_semantic_type(context, context->semantic_traits[inherited - 1U].self_type);
            if (ancestor != NULL && ancestor->kind == R_SEMANTIC_TYPE_PARAMETER)
                constraints |= context->generic_parameters[ancestor->base - 1U].constraints;
        }
    }
    return constraints;
}

static bool r_generic_resolve_parameter(const RFrontendContext *context,
                                        const RAstTokenView *name,
                                        RTypeId *type) {
    uint32_t schema_id;
    uint32_t index;
    /* R-TYPE-0045 (L16.3): the parameters of an associated type or of its binding are in scope
       in its declaration, before those of the enclosing trait or implementation. */
    for (size_t inner = 0U; inner < context->generic_schema_count; ++inner) {
        const RGenericSchema *schema = &context->generic_schemas[inner];
        if (!schema->associated_parameters || schema->span.source != name->span.source ||
            schema->span.start > name->span.start || name->span.end > schema->span.end) {
            continue;
        }
        for (index = 0U; index < schema->parameter_count; ++index) {
            const RGenericParameter *parameter =
                &context->generic_parameters[schema->first_parameter + index];
            if (parameter->name_intern_id == name->intern_id) {
                *type = parameter->type;
                return true;
            }
        }
    }
    schema_id = r_generic_schema_at(context, name->span);
    if (schema_id == 0U) {
        return false;
    }
    for (index = 0U; index < context->generic_schemas[schema_id - 1U].parameter_count; ++index) {
        const RGenericParameter *parameter =
            &context->generic_parameters[context->generic_schemas[schema_id - 1U].first_parameter +
                                         index];
        if (parameter->name_intern_id == name->intern_id) {
            *type = parameter->type;
            return true;
        }
    }
    return false;
}

static const char *const r_generic_constraint_names[] = {"copy",
                                                         "pod",
                                                         "send",
                                                         "sync",
                                                         "key",
                                                         "error",
                                                         "unborrowed",
                                                         "json_encode",
                                                         "json_decode",
                                                         "errors",
                                                         "clone"};

static bool
r_semantic_last_identifier(const RFrontendContext *context, RAstRef node, RAstTokenView *name);

/* A generic parameter of `@generic<...>` or of an associated type (L16.3): its name, a constant
   type, and its constraints, whose traits and callable signatures are resolved later. */
static bool r_generic_register_parameter(RFrontendContext *context,
                                         RAstRef parameter_ref,
                                         RAstNodeView parameter_view,
                                         uint32_t schema_first,
                                         uint32_t *parameter_count) {
    RAstTokenView name;
    RGenericParameter parameter = {0};
    uint32_t token_index, previous;
    /* R-TYPE-0043 (M19): an associated type constraint `P::Name: ...` registers a holder of its
       constraints outside the schema, after its parameters. */
    const bool projection = parameter_count == NULL;
    if (projection) {
        /* The holder is named after the associated type it constrains. */
        RAstRef associated;
        if (!r_ast_find_direct_child(
                context, parameter_ref, R_SYNTAX_ASSOCIATED_NAME, &associated) ||
            !r_semantic_last_identifier(context, associated, &name)) {
            context->resource_status = R_FRONTEND_INTERNAL_ERROR;
            return false;
        }
    } else if (!r_ast_direct_token(context, parameter_ref, 0U, NULL, &name)) {
        return false;
    }
    uint32_t constraint_start = 1U;
    if (!projection && (name.kind == R_TOKEN_KW_CONST)) {
        RAstTokenView value_type;
        if (!r_ast_direct_token(context, parameter_ref, 1U, NULL, &value_type) ||
            !r_semantic_type_from_token(
                context, value_type.kind, &parameter.constant_type) ||
            !r_ast_direct_token(context, parameter_ref, 2U, NULL, &name))
            return false;
        constraint_start = parameter_view.child_count;
    }
    parameter.name_span = name.span;
    parameter.name_intern_id = name.intern_id;
    for (previous = schema_first; !projection && (previous < context->generic_parameter_count);
         ++previous) {
        if (context->generic_parameters[previous].name_intern_id == name.intern_id &&
            !r_semantic_add_diagnostic(context,
                                       "R-DIAG-NAME-002",
                                       "R-NAME-0004",
                                       "duplicate generic type parameter",
                                       name.span)) {
            return false;
        }
    }
    bool core_prefix = false;
    for (token_index = constraint_start; token_index < parameter_view.child_count;
         ++token_index) {
        RAstRef token_ref;
        RAstTokenView token;
        RAstNodeView constraint_view;
        size_t constraint;
        if (!r_ast_ref_child(context, parameter_ref, token_index, &token_ref)) {
            continue;
        }
        if (!r_ast_ref_token(context, token_ref, &token)) {
            if (r_ast_ref_view(context, token_ref, &constraint_view) &&
                constraint_view.kind == R_SYNTAX_FIELDS_CONSTRAINT) {
                /* R-TYPE-0043 (L44): `fields(Trait)` is resolved with the other traits. */
                RAstRef trait_ref;
                RGenericPendingConstraint pending = {0};
                if (projection) {
                    if (!r_semantic_add_diagnostic(context,
                                                   "R-DIAG-TRAIT-001",
                                                   "R-TYPE-0043",
                                                   "an associated type constraint takes capability "
                                                   "and nominal trait constraints",
                                                   constraint_view.span)) {
                        return false;
                    }
                    continue;
                }
                if (!r_ast_find_direct_child(context, token_ref, R_SYNTAX_TRAIT_NAME, &trait_ref)) {
                    continue;
                }
                pending.parameter = (uint32_t)context->generic_parameter_count;
                pending.application = trait_ref;
                pending.span = constraint_view.span;
                pending.fields = true;
                if (!r_grow_array(context,
                                  (void **)&context->generic_pending_constraints,
                                  &context->generic_pending_constraint_capacity,
                                  sizeof(pending),
                                  context->generic_pending_constraint_count + 1U))
                    return false;
                context->generic_pending_constraints[context->generic_pending_constraint_count++] =
                    pending;
                continue;
            }
            if (r_ast_ref_view(context, token_ref, &constraint_view) &&
                constraint_view.kind == R_SYNTAX_TRAIT_NAME) {
                RGenericPendingConstraint pending = {0};
                pending.parameter = (uint32_t)context->generic_parameter_count;
                pending.application = token_ref;
                pending.span = constraint_view.span;
                if (!r_grow_array(context,
                                  (void **)&context->generic_pending_constraints,
                                  &context->generic_pending_constraint_capacity,
                                  sizeof(pending),
                                  context->generic_pending_constraint_count + 1U))
                    return false;
                context->generic_pending_constraints
                    [context->generic_pending_constraint_count++] = pending;
                continue;
            }
            /* R-TYPE-0044: `fn(P...) -> R` is resolved once every type is known. */
            RGenericPendingCallable pending;
            if (!r_ast_ref_view(context, token_ref, &constraint_view) ||
                (constraint_view.kind != R_SYNTAX_CALLABLE_CONSTRAINT)) {
                continue;
            }
            if (projection) {
                if (!r_semantic_add_diagnostic(context,
                                               "R-DIAG-TRAIT-001",
                                               "R-TYPE-0043",
                                               "an associated type constraint takes capability "
                                               "and nominal trait constraints",
                                               constraint_view.span)) {
                    return false;
                }
                continue;
            }
            pending.parameter = (uint32_t)context->generic_parameter_count;
            pending.node = token_ref;
            if (!r_grow_array(context,
                              (void **)&context->generic_pending_callables,
                              &context->generic_pending_callable_capacity,
                              sizeof(pending),
                              context->generic_pending_callable_count + 1U)) {
                return false;
            }
            context
                ->generic_pending_callables[context->generic_pending_callable_count++] =
                pending;
            continue;
        }
        if (token.kind == R_TOKEN_ELLIPSIS) {
            /* R-TYPE-0053 (L18.2): `T...` declares a pack. */
            parameter.is_pack = true;
            continue;
        }
        if (token.kind != R_TOKEN_IDENTIFIER) {
            continue;
        }
        {
            /* R-TYPE-0043: consecutive identifiers spell one module-qualified trait
               name `m.n::Name`; the AST keeps neither `.` nor `::`. */
            uint32_t run_end = token_index;
            RAstTokenView last = token;
            char path[512];
            size_t path_length = 0U;
            bool path_fits = true;
            while (run_end + 1U < parameter_view.child_count) {
                RAstRef next_ref;
                RAstTokenView next;
                const char *bytes;
                size_t length;
                if (!r_ast_ref_child(context, parameter_ref, run_end + 1U, &next_ref) ||
                    !r_ast_ref_token(context, next_ref, &next) ||
                    (next.kind != R_TOKEN_IDENTIFIER)) {
                    break;
                }
                if (!r_semantic_intern_text(context, last.intern_id, &bytes, &length)) {
                    return false;
                }
                if (path_length + length + 1U >= sizeof(path)) {
                    path_fits = false;
                } else {
                    if (path_length != 0U) {
                        path[path_length++] = '.';
                    }
                    (void)memcpy(path + path_length, bytes, length);
                    path_length += length;
                }
                last = next;
                run_end += 1U;
            }
            if (run_end != token_index) {
                RGenericPendingConstraint pending;
                (void)memset(&pending, 0, sizeof(pending));
                pending.parameter = (uint32_t)context->generic_parameter_count;
                pending.name_intern_id = last.intern_id;
                pending.span = token.span;
                pending.span.end = last.span.end;
                if ((run_end == token_index + 1U) &&
                    r_semantic_token_text_equal_owned(context, &token, "core")) {
                    /* R-TYPE-0046: `core::Name` names a core trait. */
                    pending.is_core = true;
                } else if (!path_fits || !r_intern_bytes(context,
                                                         (const uint8_t *)path,
                                                         path_length,
                                                         &pending.module_intern_id)) {
                    if (!path_fits &&
                        !r_semantic_add_diagnostic(context,
                                                   "R-DIAG-TYPE-001",
                                                   "R-TYPE-0043",
                                                   "unknown generic constraint",
                                                   pending.span)) {
                        return false;
                    }
                    if (!path_fits) {
                        token_index = run_end;
                        continue;
                    }
                    return false;
                }
                if (!r_grow_array(context,
                                  (void **)&context->generic_pending_constraints,
                                  &context->generic_pending_constraint_capacity,
                                  sizeof(pending),
                                  context->generic_pending_constraint_count + 1U)) {
                    return false;
                }
                context->generic_pending_constraints
                    [context->generic_pending_constraint_count++] = pending;
                token_index = run_end;
                continue;
            }
        }
        if (!core_prefix && r_semantic_token_text_equal_owned(context, &token, "core")) {
            /* R-TYPE-0046: `core::Name` names a core trait. */
            core_prefix = true;
            continue;
        }
        const size_t vocabulary =
            sizeof(r_generic_constraint_names) / sizeof(r_generic_constraint_names[0]);
        for (constraint = 0U; constraint < vocabulary; ++constraint) {
            if (r_semantic_token_text_equal_owned(
                    context, &token, r_generic_constraint_names[constraint])) {
                parameter.constraints |= UINT32_C(1) << constraint;
                break;
            }
        }
        if (constraint == vocabulary) {
            /* R-TYPE-0043: a name outside the closed vocabulary is a trait
               constraint; traits are known only after their declarations. */
            RGenericPendingConstraint pending;
            (void)memset(&pending, 0, sizeof(pending));
            pending.parameter = (uint32_t)context->generic_parameter_count;
            pending.name_intern_id = token.intern_id;
            pending.span = token.span;
            pending.is_core = core_prefix;
            core_prefix = false;
            if (!r_grow_array(context,
                              (void **)&context->generic_pending_constraints,
                              &context->generic_pending_constraint_capacity,
                              sizeof(pending),
                              context->generic_pending_constraint_count + 1U)) {
                return false;
            }
            context->generic_pending_constraints
                [context->generic_pending_constraint_count++] = pending;
        }
    }
    if ((parameter.constraints & R_GENERIC_POD) != 0U) {
        parameter.constraints |= R_GENERIC_COPY | R_GENERIC_UNBORROWED;
    }
    if (context->generic_parameter_count >= UINT32_MAX ||
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
    if (!projection) {
        *parameter_count += 1U;
    }
    return true;
}

/* R-TYPE-0045 (L16.3): the parameters of an associated type that a trait declares or an
   implementation binds form a schema of their own, in scope in that member only. */
static bool r_generic_register_associated_parameters(RFrontendContext *context, RAstRef external) {
    RAstNodeView external_view;

    if (!r_ast_ref_view(context, external, &external_view)) {
        return false;
    }
    for (uint32_t child = 0U; child < external_view.child_count; ++child) {
        RAstRef declaration;
        RAstNodeView declaration_view;

        if (!r_ast_ref_child(context, external, child, &declaration) ||
            !r_ast_ref_view(context, declaration, &declaration_view) ||
            ((declaration_view.kind != R_SYNTAX_TRAIT_DECLARATION) &&
             (declaration_view.kind != R_SYNTAX_IMPL_DECLARATION))) {
            continue;
        }
        for (uint32_t member = 0U; member < declaration_view.child_count; ++member) {
            RAstRef associated;
            RAstNodeView associated_view;
            RGenericSchema schema = {0};

            if (!r_ast_ref_child(context, declaration, member, &associated) ||
                !r_ast_view_kind(
                    context, associated, R_SYNTAX_ASSOCIATED_TYPE, &associated_view)) {
                continue;
            }
            schema.span = associated_view.span;
            schema.external_ast = external.node;
            schema.declaration_ast = associated.node;
            schema.first_parameter = (uint32_t)context->generic_parameter_count;
            schema.associated_parameters = true;
            for (uint32_t index = 0U; index < associated_view.child_count; ++index) {
                RAstRef parameter_ref;
                RAstNodeView parameter_view;

                if (r_ast_ref_child(context, associated, index, &parameter_ref) &&
                    r_ast_view_kind(
                        context, parameter_ref, R_SYNTAX_GENERIC_PARAMETER, &parameter_view) &&
                    !r_generic_register_parameter(context,
                                                  parameter_ref,
                                                  parameter_view,
                                                  schema.first_parameter,
                                                  &schema.parameter_count)) {
                    return false;
                }
            }
            if (schema.parameter_count == 0U) {
                continue;
            }
            if (!r_grow_array(context,
                              (void **)&context->generic_schemas,
                              &context->generic_schema_capacity,
                              sizeof(schema),
                              context->generic_schema_count + 1U)) {
                return false;
            }
            context->generic_schemas[context->generic_schema_count++] = schema;
        }
    }
    return true;
}

/* R-NAME-0004 (L16.3): a parameter of an associated type or of its binding does not repeat a
   parameter of the enclosing generic trait or implementation `header`. */
static bool r_generic_check_associated_shadowing(RFrontendContext *context,
                                                 const RGenericSchema *header) {
    for (size_t index = 0U; index < context->generic_schema_count; ++index) {
        const RGenericSchema associated = context->generic_schemas[index];
        if (!associated.associated_parameters ||
            (associated.external_ast != header->external_ast) ||
            (associated.span.source != header->span.source)) {
            continue;
        }
        for (uint32_t inner = 0U; inner < associated.parameter_count; ++inner) {
            const RGenericParameter parameter =
                context->generic_parameters[associated.first_parameter + inner];
            for (uint32_t outer = 0U; outer < header->parameter_count; ++outer) {
                if ((context->generic_parameters[header->first_parameter + outer].name_intern_id ==
                     parameter.name_intern_id) &&
                    !r_semantic_add_diagnostic(context,
                                               "R-DIAG-NAME-002",
                                               "R-NAME-0004",
                                               "an associated type parameter repeats a generic "
                                               "parameter of its trait or implementation",
                                               parameter.name_span)) {
                    return false;
                }
            }
        }
    }
    return true;
}

/* R-TYPE-0053 (L18.2): a generic function declares at most one pack, as its last parameter. */
static bool r_generic_check_pack_placement(RFrontendContext *context,
                                           const RGenericSchema *header,
                                           RAstRef external) {
    RAstRef function;
    const bool is_function =
        r_ast_find_direct_child(context, external, R_SYNTAX_FUNCTION_DECLARATION, &function);
    for (uint32_t index = 0U; index < header->parameter_count; ++index) {
        const RGenericParameter parameter =
            context->generic_parameters[header->first_parameter + index];
        const char *message = NULL;
        if (!parameter.is_pack) {
            continue;
        }
        if (!is_function) {
            message = "a pack belongs to the header of a generic function";
        } else if (index + 1U != header->parameter_count) {
            message = "a pack is the last parameter of its generic header";
        }
        if ((message != NULL) &&
            !r_semantic_add_diagnostic(
                context, "R-DIAG-TYPE-001", "R-TYPE-0053", message, parameter.name_span)) {
            return false;
        }
    }
    return true;
}

/* R-TYPE-0053: the pack `type` names, or NULL. */
static const RGenericParameter *r_generic_pack(const RFrontendContext *context, RTypeId type_id) {
    const RSemanticType *type = r_semantic_type(context, type_id);
    if ((type == NULL) || (type->kind != R_SEMANTIC_TYPE_PARAMETER) || (type->base == 0U) ||
        ((size_t)type->base > context->generic_parameter_count) ||
        !context->generic_parameters[type->base - 1U].is_pack) {
        return NULL;
    }
    return &context->generic_parameters[type->base - 1U];
}

/* R-TYPE-0053 (L18.4): the parameter record of the element carrier `type`. */
static const RGenericParameter *r_generic_pack_carrier(const RFrontendContext *context,
                                                       RTypeId type_id) {
    const RSemanticType *type = r_semantic_type(context, type_id);
    return &context->generic_parameters[type->base - 1U];
}

/* The schema of the parameters of the associated type or binding `member`, or zero. */
static uint32_t r_generic_associated_schema(const RFrontendContext *context, RAstRef member) {
    for (size_t index = 0U; index < context->generic_schema_count; ++index) {
        if (context->generic_schemas[index].associated_parameters &&
            (context->generic_schemas[index].declaration_ast == member.node) &&
            (context->generic_schemas[index].span.source == member.source)) {
            return (uint32_t)index + 1U;
        }
    }
    return 0U;
}

static bool r_generic_register_headers(RFrontendContext *context) {
    size_t source;
    for (source = 0U; source < context->source_count; ++source) {
        RAstRef root;
        RAstNodeView root_view;
        uint32_t child;
        if (!r_semantic_root_ref(context, (RSourceId)source + 1U, &root) ||
            !r_ast_ref_view(context, root, &root_view)) {
            return false;
        }
        for (child = 0U; child < root_view.child_count; ++child) {
            RAstRef external, header;
            RAstNodeView header_view;
            RGenericSchema schema = {0};
            uint32_t member;
            if (!r_ast_ref_child(context, root, child, &external)) {
                continue;
            }
            if (!r_generic_register_associated_parameters(context, external)) {
                return false;
            }
            if (!r_ast_find_direct_child(context, external, R_SYNTAX_GENERIC_HEADER, &header)) {
                continue;
            }
            if (!r_ast_ref_view(context, external, &header_view)) {
                return false;
            }
            schema.span = header_view.span;
            schema.external_ast = external.node;
            schema.first_parameter = (uint32_t)context->generic_parameter_count;
            if (!r_ast_ref_view(context, header, &header_view)) {
                return false;
            }
            for (member = 0U; member < header_view.child_count; ++member) {
                RAstRef parameter_ref;
                RAstNodeView parameter_view;
                RAstRef associated;
                if (!r_ast_ref_child(context, header, member, &parameter_ref) ||
                    !r_ast_view_kind(
                        context, parameter_ref, R_SYNTAX_GENERIC_PARAMETER, &parameter_view) ||
                    r_ast_find_direct_child(
                        context, parameter_ref, R_SYNTAX_ASSOCIATED_NAME, &associated)) {
                    continue;
                }
                if (!r_generic_register_parameter(context,
                                                  parameter_ref,
                                                  parameter_view,
                                                  schema.first_parameter,
                                                  &schema.parameter_count)) {
                    return false;
                }
            }
            /* R-TYPE-0043 (M19): the associated type constraints of a generic function follow
               its parameters; their holders stay outside the schema. */
            for (member = 0U; member < header_view.child_count; ++member) {
                RAstRef parameter_ref;
                RAstNodeView parameter_view;
                RAstRef associated;
                RAstRef function;
                if (!r_ast_ref_child(context, header, member, &parameter_ref) ||
                    !r_ast_view_kind(
                        context, parameter_ref, R_SYNTAX_GENERIC_PARAMETER, &parameter_view) ||
                    !r_ast_find_direct_child(
                        context, parameter_ref, R_SYNTAX_ASSOCIATED_NAME, &associated)) {
                    continue;
                }
                if (!r_ast_find_direct_child(
                        context, external, R_SYNTAX_FUNCTION_DECLARATION, &function)) {
                    if (!r_semantic_add_diagnostic(context,
                                                   "R-DIAG-TYPE-001",
                                                   "R-TYPE-0043",
                                                   "an associated type constraint belongs to the "
                                                   "header of a generic function",
                                                   parameter_view.span)) {
                        return false;
                    }
                    continue;
                }
                RGenericProjectionConstraint constraint;
                (void)memset(&constraint, 0, sizeof(constraint));
                constraint.schema = (uint32_t)context->generic_schema_count + 1U;
                constraint.holder = (uint32_t)context->generic_parameter_count;
                constraint.entry = parameter_ref;
                constraint.span = parameter_view.span;
                if (!r_generic_register_parameter(
                        context, parameter_ref, parameter_view, schema.first_parameter, NULL) ||
                    !r_grow_array(context,
                                  (void **)&context->generic_projection_constraints,
                                  &context->generic_projection_constraint_capacity,
                                  sizeof(constraint),
                                  context->generic_projection_constraint_count + 1U)) {
                    return false;
                }
                context->generic_projection_constraints
                    [context->generic_projection_constraint_count++] = constraint;
            }
            if (context->generic_schema_count >= UINT32_MAX ||
                !r_grow_array(context,
                              (void **)&context->generic_schemas,
                              &context->generic_schema_capacity,
                              sizeof(schema),
                              context->generic_schema_count + 1U)) {
                return false;
            }
            context->generic_schemas[context->generic_schema_count++] = schema;
            if (!r_generic_check_associated_shadowing(context, &schema) ||
                !r_generic_check_pack_placement(context, &schema, external)) {
                return false;
            }
        }
    }
    return true;
}

static bool r_generic_commit_arguments(RFrontendContext *context,
                                       const RTypeVector *arguments,
                                       uint32_t *first) {
    if (arguments->count > UINT32_MAX - context->generic_argument_count) {
        context->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
        return false;
    }
    if (!r_grow_array(context,
                      (void **)&context->generic_arguments,
                      &context->generic_argument_capacity,
                      sizeof(*context->generic_arguments),
                      context->generic_argument_count + arguments->count)) {
        return false;
    }
    *first = (uint32_t)context->generic_argument_count;
    if (arguments->count != 0U) {
        (void)memcpy(context->generic_arguments + context->generic_argument_count,
                     arguments->items,
                     arguments->count * sizeof(*arguments->items));
    }
    context->generic_argument_count += arguments->count;
    return true;
}

static bool r_generic_apply_aggregate(RFrontendContext *context,
                                      uint32_t origin,
                                      const RTypeVector *arguments,
                                      RTypeId *result) {
    RSemanticAggregate aggregate = context->semantic_aggregates[origin - 1U];
    const uint32_t count = context->generic_schemas[aggregate.generic_schema - 1U].parameter_count;
    size_t index;
    if (arguments->count != count) {
        return false;
    }
    for (index = 0U; index < context->semantic_aggregate_count; ++index) {
        const RSemanticAggregate *candidate = &context->semantic_aggregates[index];
        if (candidate->generic_origin == origin &&
            memcmp(context->generic_arguments + candidate->first_generic_argument,
                   arguments->items,
                   count * sizeof(RTypeId)) == 0) {
            *result = candidate->type;
            return true;
        }
    }
    if (context->semantic_aggregate_count >= UINT32_MAX ||
        !r_generic_instance_name(
            context, aggregate.name_intern_id, arguments, &aggregate.name_intern_id) ||
        !r_generic_commit_arguments(context, arguments, &aggregate.first_generic_argument) ||
        !r_grow_array(context,
                      (void **)&context->semantic_aggregates,
                      &context->semantic_aggregate_capacity,
                      sizeof(aggregate),
                      context->semantic_aggregate_count + 1U)) {
        return false;
    }
    aggregate.generic_origin = origin;
    aggregate.complete = false;
    aggregate.first_field = 0U;
    aggregate.field_count = 0U;
    aggregate.first_variant = 0U;
    aggregate.variant_count = 0U;
    if (!r_semantic_intern_type(context,
                                aggregate.kind == R_SEMANTIC_AGGREGATE_STRUCT
                                    ? R_SEMANTIC_TYPE_STRUCT
                                    : R_SEMANTIC_TYPE_ENUM,
                                (RTypeId)context->semantic_aggregate_count + 1U,
                                &aggregate.type)) {
        return false;
    }
    *result = aggregate.type;
    context->semantic_aggregates[context->semantic_aggregate_count++] = aggregate;
    if (context->generic_members_ready) {
        bool completed;
        if (context->generic_instantiation_depth >= context->options.limits.max_nesting) {
            r_generic_limit_diagnostic(context, aggregate.name_intern_id, aggregate.name_span);
            context->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
            return false;
        }
        ++context->generic_instantiation_depth;
        completed =
            r_generic_complete_aggregate(context, (uint32_t)context->semantic_aggregate_count);
        --context->generic_instantiation_depth;
        return completed;
    }
    return true;
}

/* R-STMT-0023 (L44): the value that the innermost repetition being unrolled binds to the constant
   of a translation-time loop. */
static bool
r_constexpr_binding(const RFrontendContext *context, RTypeId parameter, uint64_t *value) {
    for (size_t index = context->constexpr_binding_count; index != 0U; --index) {
        if (context->constexpr_binding_parameters[index - 1U] == parameter) {
            *value = context->constexpr_binding_values[index - 1U];
            return true;
        }
    }
    return false;
}

static bool r_generic_substitute_type(RFrontendContext *context,
                                      RTypeId id,
                                      uint32_t schema_id,
                                      uint32_t first_argument,
                                      RTypeId *result) {
    const RSemanticType *description = r_semantic_type(context, id);
    RSemanticType type;
    RGenericSchema schema = context->generic_schemas[schema_id - 1U];
    uint32_t index;
    *result = id;
    if (description == NULL) {
        return id == R_TYPE_ID_INVALID;
    }
    type = *description;
    if (r_opaque_parameter(context, id) != NULL)
        return r_opaque_substitute(context, id, schema_id, first_argument, result);
    if (type.kind == R_SEMANTIC_TYPE_PARAMETER && type.base != 0U &&
        type.base <= context->generic_parameter_count &&
        context->generic_parameters[type.base - 1U].loop_constant) {
        /* R-STMT-0023 (L44): the constant of the repetition being unrolled; outside one it stays.
         */
        uint64_t value;
        if (!r_constexpr_binding(context, id, &value)) {
            return true;
        }
        return r_semantic_intern_derived_type(
            context, R_SEMANTIC_TYPE_CONSTANT_EXPR, 0U, 0U, value, R_TOKEN_INVALID, result);
    }
    if (type.kind == R_SEMANTIC_TYPE_PARAMETER && type.base != 0U &&
        type.base <= context->generic_parameter_count &&
        context->generic_parameters[type.base - 1U].field_base != R_TYPE_ID_INVALID) {
        /* R-REFL-0006 (L44): the field of the substituted struct at the bound index. */
        const RGenericParameter field = context->generic_parameters[type.base - 1U];
        uint64_t field_number = field.field_index_value;
        RTypeId base;
        if ((field.field_index != R_TYPE_ID_INVALID) &&
            !r_constexpr_binding(context, field.field_index, &field_number)) {
            return true;
        }
        if (!r_generic_substitute_type(
                context, field.field_base, schema_id, first_argument, &base)) {
            return false;
        }
        const RSemanticAggregate *aggregate = r_semantic_aggregate_for_type(context, base);
        if ((aggregate == NULL) || (aggregate->kind != R_SEMANTIC_AGGREGATE_STRUCT) ||
            (field_number >= (uint64_t)aggregate->field_count)) {
            return true;
        }
        *result = context->semantic_fields[aggregate->first_field + (uint32_t)field_number].type;
        return true;
    }
    if (type.kind == R_SEMANTIC_TYPE_PARAMETER) {
        if (type.base != 0U && type.base <= context->generic_parameter_count &&
            context->generic_parameters[type.base - 1U].refinement_origin != 0U) {
            return r_generic_substitute_type(
                context,
                context->generic_parameters[type.base - 1U].refinement_origin,
                schema_id,
                first_argument,
                result);
        }
        for (index = 0U; index < schema.parameter_count; ++index) {
            if (type.base == schema.first_parameter + index + 1U ||
                context->generic_parameters[schema.first_parameter + index].type == id) {
                *result = context->generic_arguments[first_argument + index];
                return true;
            }
        }
        if ((type.base != 0U) && ((size_t)type.base <= context->generic_parameter_count) &&
            (context->generic_parameters[type.base - 1U].pack_base != R_TYPE_ID_INVALID)) {
            return r_pack_substitute(context,
                                     &context->generic_parameters[type.base - 1U],
                                     schema_id,
                                     first_argument,
                                     result);
        }
        if ((type.base != 0U) && ((size_t)type.base <= context->generic_parameter_count) &&
            (context->generic_parameters[type.base - 1U].application_base != R_TYPE_ID_INVALID)) {
            /* R-TYPE-0045 (L16.3): an application follows its base and its arguments; a base
               that became a binding applies at once. */
            const RGenericParameter application = context->generic_parameters[type.base - 1U];
            RTypeVector arguments = {0};
            RTypeId base;
            RTypeId chain;
            bool success;
            if (!r_generic_substitute_type(
                    context, application.application_base, schema_id, first_argument, &base) ||
                !r_generic_substitute_type(context,
                                           application.application_arguments,
                                           schema_id,
                                           first_argument,
                                           &chain)) {
                return false;
            }
            if ((base == application.application_base) &&
                (chain == application.application_arguments)) {
                return true;
            }
            if (r_semantic_value_kind(context, base) != R_SEMANTIC_TYPE_TYPE_FUNCTION) {
                return r_semantic_associated_application(
                    context, base, chain, application.application_span, result);
            }
            success = r_semantic_chain_arguments(context, chain, &arguments) &&
                      r_semantic_reduce_associated(context, base, &arguments, result);
            r_context_free(context, arguments.items);
            return success;
        }
        if ((type.base != 0U) && ((size_t)type.base <= context->generic_parameter_count) &&
            (context->generic_parameters[type.base - 1U].projection_base != R_TYPE_ID_INVALID)) {
            /* R-TYPE-0045: a projection follows its base; a closed base resolves through the
               implementation that binds the associated type. */
            const RGenericParameter projection = context->generic_parameters[type.base - 1U];
            if (projection.projection_bound != 0U)
                return r_generic_substitute_type(
                    context, projection.projection_bound, schema_id, first_argument, result);
            RTypeId base;
            RTypeId resolved;
            uint32_t projected_trait;
            if (!r_generic_substitute_type(
                    context, projection.projection_base, schema_id, first_argument, &base) ||
                !r_semantic_effective_trait(context,
                                            projection.projection_trait,
                                            schema_id,
                                            first_argument,
                                            &projected_trait)) {
                return false;
            }
            if (((base != projection.projection_base) ||
                 projected_trait != projection.projection_trait) &&
                r_semantic_resolve_projection(
                    context, base, projected_trait, projection.projection_index, &resolved)) {
                *result = resolved;
            }
        }
        return true;
    }
    if (type.kind == R_SEMANTIC_TYPE_STRUCT || type.kind == R_SEMANTIC_TYPE_ENUM) {
        RSemanticAggregate aggregate = context->semantic_aggregates[type.base - 1U];
        if (aggregate.standard_payload_element != 0U) {
            RTypeId element;
            return r_generic_substitute_type(context,
                                             aggregate.standard_payload_element,
                                             schema_id,
                                             first_argument,
                                             &element) &&
                   r_semantic_prepare_array_push_payload(context, element, result);
        }
        RTypeVector arguments = {0};
        bool success = true;
        if (aggregate.generic_origin == 0U) {
            if ((aggregate.payload_parent != 0U && aggregate.generic_schema == schema_id) ||
                (aggregate.is_closure && aggregate.generic_schema != 0U &&
                 context->generic_schemas[aggregate.generic_schema - 1U].first_parameter ==
                     schema.first_parameter &&
                 context->generic_schemas[aggregate.generic_schema - 1U].parameter_count ==
                     schema.parameter_count)) {
                for (index = 0U; index < schema.parameter_count; ++index) {
                    if (!r_type_vector_push(context,
                                            &arguments,
                                            context->generic_arguments[first_argument + index])) {
                        r_context_free(context, arguments.items);
                        return false;
                    }
                }
                success = r_generic_apply_aggregate(context, type.base, &arguments, result);
                r_context_free(context, arguments.items);
                return success;
            }
            return true;
        }
        for (index = 0U;
             index < context->generic_schemas[aggregate.generic_schema - 1U].parameter_count;
             ++index) {
            RTypeId argument;
            if (!r_generic_substitute_type(
                    context,
                    context->generic_arguments[aggregate.first_generic_argument + index],
                    schema_id,
                    first_argument,
                    &argument) ||
                !r_type_vector_push(context, &arguments, argument)) {
                success = false;
                break;
            }
        }
        if (success) {
            success =
                r_generic_apply_aggregate(context, aggregate.generic_origin, &arguments, result);
        }
        r_context_free(context, arguments.items);
        return success;
    }
    if (type.kind < R_SEMANTIC_TYPE_FIXED_ARRAY) {
        return true;
    }
    if ((type.kind == R_SEMANTIC_TYPE_FUNCTION_PARAMETER) && (type.length == UINT64_C(1))) {
        return r_pack_substitute_expansion(context, &type, schema_id, first_argument, result);
    }
    const bool expands =
        (type.kind == R_SEMANTIC_TYPE_RAW_FUNCTION) && r_pack_chain_expands(context, type.second);
    if (!r_generic_substitute_type(context, type.base, schema_id, first_argument, &type.base) ||
        !r_generic_substitute_type(context, type.second, schema_id, first_argument, &type.second)) {
        return false;
    }
    if (expands) {
        /* R-TYPE-0053 (L18.4): the parameters of a signature whose pack became a tuple. */
        type.length = r_pack_chain_count(context, type.second);
    }
    if (type.kind == R_SEMANTIC_TYPE_EFFECT_SET) {
        RTypeVector errors = {0};
        const RTypeId parts[] = {type.base, type.second};
        bool success = true, duplicate = false;
        for (size_t part = 0U; success && part < 2U; ++part) {
            const RSemanticType *part_type = r_semantic_type(context, parts[part]);
            if (part_type == NULL)
                continue;
            if (part_type->kind == R_SEMANTIC_TYPE_EFFECT_SET) {
                for (uint32_t entry = 0U;
                     success && entry < r_semantic_effect_count(context, parts[part]);
                     ++entry)
                    success = r_type_vector_push(
                        context, &errors, r_semantic_effect_at(context, parts[part], entry));
            } else {
                success = r_type_vector_push(context, &errors, parts[part]);
            }
        }
        /* Substituted sets are unions, so overlapping members occur only once. */
        for (size_t i = 0U; i < errors.count; ++i)
            for (size_t j = i + 1U; j < errors.count;)
                if (errors.items[i] == errors.items[j])
                    errors.items[j] = errors.items[--errors.count];
                else
                    ++j;
        success = success && r_semantic_intern_effect_set(context, &errors, result, &duplicate);
        r_context_free(context, errors.items);
        return success;
    }
    if (type.kind == R_SEMANTIC_TYPE_EFFECT_CARRIER && type.second == 0U) {
        *result = type.base;
        return true;
    }
    if (type.kind == R_SEMANTIC_TYPE_FIXED_ARRAY && type.second != 0U &&
        !r_generic_constant_fold(context, &type.second, &type.length, schema.span)) {
        return false;
    }
    if (!r_semantic_intern_derived_type(
            context, type.kind, type.base, type.second, type.length, type.flags, result))
        return false;
    return type.kind != R_SEMANTIC_TYPE_CONSTANT_EXPR ||
           r_generic_canonical_constant(context, result, schema.span);
}

static bool r_generic_complete_aggregate(RFrontendContext *context, uint32_t id) {
    RSemanticAggregate instance = context->semantic_aggregates[id - 1U];
    RSemanticAggregate origin;
    RTypeVector fields = {0};
    uint32_t index;
    bool success = false;
    if (instance.complete || instance.generic_origin == 0U) {
        return true;
    }
    origin = context->semantic_aggregates[instance.generic_origin - 1U];
    if (!origin.complete) {
        return false;
    }
    /* Publish the identity before substituting recursive owner fields. */
    context->semantic_aggregates[id - 1U].complete = true;
    context->semantic_aggregates[id - 1U].completing = true;
    instance.is_copy = origin.drop_function == R_SYMBOL_ID_INVALID;
    instance.is_default_initializable = origin.kind == R_SEMANTIC_AGGREGATE_STRUCT;
    for (index = 0U; index < origin.field_count; ++index) {
        const size_t diagnostics = context->diagnostic_count;
        const size_t pending = context->computed_pending;
        RTypeId field;
        if (!r_generic_substitute_type(context,
                                       context->semantic_fields[origin.first_field + index].type,
                                       instance.generic_schema,
                                       instance.first_generic_argument,
                                       &field)) {
            if (context->resource_status != R_FRONTEND_OK ||
                (context->diagnostic_count == diagnostics && context->computed_pending == pending))
                goto cleanup;
            /* A computed bound without a value (R-TYPE-0047), already diagnosed or awaiting a
               discovery pass: the instance is poisoned and keeps the dependent field type. */
            field = context->semantic_fields[origin.first_field + index].type;
            instance.poisoned = true;
        }
        if (!r_type_vector_push(context, &fields, field)) {
            goto cleanup;
        }
        instance.is_copy = instance.is_copy && r_semantic_type_is_copy(context, field);
        instance.is_default_initializable =
            instance.is_default_initializable && r_semantic_type_has_default(context, field) &&
            (context->semantic_fields[origin.first_field + index].initializer.node == 0U);
    }
    instance.first_field = (uint32_t)context->semantic_field_count;
    instance.field_count = origin.field_count;
    if (!r_grow_array(context,
                      (void **)&context->semantic_fields,
                      &context->semantic_field_capacity,
                      sizeof(*context->semantic_fields),
                      context->semantic_field_count + fields.count)) {
        goto cleanup;
    }
    for (index = 0U; index < origin.field_count; ++index) {
        RSemanticField field = context->semantic_fields[origin.first_field + index];
        field.type = fields.items[index];
        field.aggregate = id;
        context->semantic_fields[context->semantic_field_count++] = field;
    }
    if (origin.variant_count != 0U) {
        RTypeVector payloads = {0};
        for (index = 0U; index < origin.variant_count; ++index) {
            RTypeId payload;
            if (!r_generic_substitute_type(
                    context,
                    context->semantic_variants[origin.first_variant + index].payload_type,
                    instance.generic_schema,
                    instance.first_generic_argument,
                    &payload) ||
                !r_type_vector_push(context, &payloads, payload)) {
                r_context_free(context, payloads.items);
                goto cleanup;
            }
        }
        if (!r_grow_array(context,
                          (void **)&context->semantic_variants,
                          &context->semantic_variant_capacity,
                          sizeof(*context->semantic_variants),
                          context->semantic_variant_count + origin.variant_count)) {
            r_context_free(context, payloads.items);
            goto cleanup;
        }
        instance.first_variant = (uint32_t)context->semantic_variant_count;
        instance.variant_count = origin.variant_count;
        for (index = 0U; index < origin.variant_count; ++index) {
            RSemanticVariant variant = context->semantic_variants[origin.first_variant + index];
            variant.aggregate = id;
            variant.payload_type = payloads.items[index];
            context->semantic_variants[context->semantic_variant_count++] = variant;
        }
        r_context_free(context, payloads.items);
    }
    instance.complete = true;
    if (origin.is_closure && !r_generic_substitute_type(context,
                                                        origin.closure_signature,
                                                        instance.generic_schema,
                                                        instance.first_generic_argument,
                                                        &instance.closure_signature))
        goto cleanup;
    context->semantic_aggregates[id - 1U] = instance;
    success = r_generic_bind_drop(context, id);
cleanup:
    context->semantic_aggregates[id - 1U].completing = false;
    r_context_free(context, fields.items);
    return success;
}

static bool r_generic_apply_type_ast(RFrontendContext *context,
                                     RAstRef ref,
                                     uint32_t aggregate_id,
                                     RTypeId *result) {
    RSemanticAggregate aggregate = context->semantic_aggregates[aggregate_id - 1U];
    RTypeVector arguments = {0};
    RAstRef argument;
    RAstNodeView view;
    uint32_t index = 0U;
    bool success = false;
    if (!r_ast_ref_view(context, ref, &view)) {
        return false;
    }
    while (r_semantic_type_child(context, ref, index++, &argument)) {
        RTypeId type;
        const RGenericSchema schema = context->generic_schemas[aggregate.generic_schema - 1U];
        const bool constant =
            index <= schema.parameter_count &&
            context->generic_parameters[schema.first_parameter + index - 1U].constant_type != 0U;
        uint64_t value = 0U;
        RAstRef expression;
        if (!constant &&
            r_ast_find_direct_child(context, argument, R_SYNTAX_EXPRESSION, &expression)) {
            (void)r_semantic_add_diagnostic(context,
                                            "R-DIAG-TYPE-001",
                                            "R-TYPE-0012",
                                            "a generic type parameter requires a type argument",
                                            view.span);
            goto cleanup;
        }
        if (!(constant
                  ? r_generic_bound(context,
                                    argument,
                                    0U,
                                    context->generic_parameters[schema.first_parameter + index - 1U]
                                        .constant_type,
                                    &type,
                                    &value,
                                    false)
                  : r_semantic_type_from_ast(context, argument, &type)) ||
            !r_type_vector_push(context, &arguments, type)) {
            goto cleanup;
        }
    }
    if (arguments.count !=
        context->generic_schemas[aggregate.generic_schema - 1U].parameter_count) {
        (void)r_semantic_add_diagnostic(context,
                                        "R-DIAG-TYPE-001",
                                        "R-TYPE-0001",
                                        "generic type requires its complete type argument list",
                                        view.span);
        goto cleanup;
    }
    success =
        r_generic_validate_arguments(context, aggregate.generic_schema, &arguments, view.span) &&
        r_generic_apply_aggregate(context, aggregate_id, &arguments, result);
cleanup:
    r_context_free(context, arguments.items);
    return success;
}

/* The type depends on a generic parameter outside the `bound_count` parameters from
 * `bound_first`, which a binding of an associated type binds itself (R-TYPE-0045, L16.3). */
static bool r_generic_type_depends_outside(const RFrontendContext *context,
                                           RTypeId id,
                                           uint32_t bound_first,
                                           uint32_t bound_count,
                                           uint32_t depth) {
    const RSemanticType *type = r_semantic_type(context, id);
    uint32_t index;
    if (type == NULL) {
        return false;
    }
    const RGenericParameter *opaque = r_opaque_parameter(context, id);
    if (opaque != NULL && opaque->opaque_concrete != 0U &&
        depth <= context->options.limits.max_nesting)
        return r_generic_type_depends_outside(
            context, opaque->opaque_concrete, bound_first, bound_count, depth + 1U);
    if (depth > context->options.limits.max_nesting) {
        return true;
    }
    if (type->kind == R_SEMANTIC_TYPE_PARAMETER) {
        return (type->base <= bound_first) || (type->base > bound_first + bound_count);
    }
    if (type->kind == R_SEMANTIC_TYPE_TYPE_FUNCTION) {
        const RGenericSchema *schema = &context->generic_schemas[type->length - 1U];
        if (bound_count == 0U) {
            return r_generic_type_depends_outside(context,
                                                  type->base,
                                                  schema->first_parameter,
                                                  schema->parameter_count,
                                                  depth + 1U);
        }
    }
    if (type->kind == R_SEMANTIC_TYPE_STRUCT || type->kind == R_SEMANTIC_TYPE_ENUM) {
        const RSemanticAggregate *aggregate = &context->semantic_aggregates[type->base - 1U];
        if (aggregate->standard_payload_element != 0U) {
            return r_generic_type_depends_outside(context,
                                                  aggregate->standard_payload_element,
                                                  bound_first,
                                                  bound_count,
                                                  depth + 1U);
        }
        if (aggregate->generic_schema == 0U) {
            return false;
        }
        if (aggregate->generic_origin == 0U) {
            return true;
        }
        for (index = 0U;
             index < context->generic_schemas[aggregate->generic_schema - 1U].parameter_count;
             ++index) {
            if (r_generic_type_depends_outside(
                    context,
                    context->generic_arguments[aggregate->first_generic_argument + index],
                    bound_first,
                    bound_count,
                    depth + 1U)) {
                return true;
            }
        }
        return false;
    }
    return type->kind >= R_SEMANTIC_TYPE_FIXED_ARRAY &&
           (r_generic_type_depends_outside(
                context, type->base, bound_first, bound_count, depth + 1U) ||
            r_generic_type_depends_outside(
                context, type->second, bound_first, bound_count, depth + 1U));
}

/* A binding of an associated type with parameters depends only on the parameters it does not
 * bind itself. */
bool r_generic_type_is_dependent(const RFrontendContext *context, RTypeId id, uint32_t depth) {
    return r_generic_type_depends_outside(context, id, 0U, 0U, depth);
}

bool r_generic_function_is_open(const RFrontendContext *context, const RSemanticSymbol *symbol) {
    uint32_t index;
    if (symbol->is_trait_prototype && symbol->generic_origin != 0U &&
        r_opaque_parameter(context, context->generic_arguments[symbol->first_generic_argument]) !=
            NULL)
        return true;
    if (symbol->json_operation != 0U)
        return r_generic_type_is_dependent(context, symbol->json_type, 0U);
    if (symbol->generic_schema == 0U) {
        return false;
    }
    if (symbol->generic_origin == 0U) {
        return true;
    }
    for (index = 0U; index < context->generic_schemas[symbol->generic_schema - 1U].parameter_count;
         ++index) {
        if (r_generic_type_is_dependent(
                context, context->generic_arguments[symbol->first_generic_argument + index], 0U)) {
            return true;
        }
    }
    return false;
}

static bool
r_generic_infer_type(RFrontendContext *, RTypeId, RTypeId, uint32_t, RTypeVector *, uint32_t);

/* Infer one set variable from the exact remainder after explicitly named effects.
   Empty sets have a nonzero argument identity, distinct from an uninferred argument. */
static bool r_generic_infer_effects(RFrontendContext *context,
                                    RTypeId pattern,
                                    RTypeId actual,
                                    uint32_t schema,
                                    RTypeVector *arguments,
                                    uint32_t depth) {
    RTypeId variable = 0U;
    RTypeVector remainder = {0};
    bool success = false, duplicate = false;
    RTypeVector known = {0};
    const RGenericSchema declaration = context->generic_schemas[schema - 1U];
    for (uint32_t i = 0U; i < declaration.parameter_count; ++i)
        if (!r_type_vector_push(
                context,
                &known,
                arguments->items[i] != 0U
                    ? arguments->items[i]
                    : context->generic_parameters[declaration.first_parameter + i].type)) {
            r_context_free(context, known.items);
            return false;
        }
    uint32_t first_known;
    const bool substituted =
        r_generic_commit_arguments(context, &known, &first_known) &&
        r_generic_substitute_type(context, pattern, schema, first_known, &pattern);
    r_context_free(context, known.items);
    if (!substituted)
        return false;
    bool has_set_parameter = false;
    for (uint32_t i = 0U; i < r_semantic_effect_count(context, pattern); ++i)
        has_set_parameter =
            has_set_parameter ||
            (r_generic_parameter_constraints(context, r_semantic_effect_at(context, pattern, i)) &
             R_GENERIC_ERRORS) != 0U;
    if (!has_set_parameter) {
        if (r_semantic_effect_count(context, pattern) != r_semantic_effect_count(context, actual))
            return false;
        for (uint32_t i = 0U; i < r_semantic_effect_count(context, pattern); ++i)
            if (!r_generic_infer_type(context,
                                      r_semantic_effect_at(context, pattern, i),
                                      r_semantic_effect_at(context, actual, i),
                                      schema,
                                      arguments,
                                      depth + 1U))
                return false;
        return true;
    }
    for (uint32_t i = 0U; i < r_semantic_effect_count(context, pattern); ++i) {
        const RTypeId entry = r_semantic_effect_at(context, pattern, i);
        if ((r_generic_parameter_constraints(context, entry) & R_GENERIC_ERRORS) != 0U) {
            if (variable != 0U && variable != entry)
                goto cleanup;
            variable = entry;
        } else if (!r_semantic_effect_contains(context, actual, entry)) {
            /* A single error-type parameter still uses ordinary type inference. */
            if (r_semantic_effect_count(context, pattern) == 1U &&
                r_semantic_effect_count(context, actual) == 1U) {
                success = r_generic_infer_type(context,
                                               entry,
                                               r_semantic_effect_at(context, actual, 0U),
                                               schema,
                                               arguments,
                                               depth + 1U);
            }
            goto cleanup;
        }
    }
    for (uint32_t i = 0U; i < r_semantic_effect_count(context, actual); ++i) {
        const RTypeId entry = r_semantic_effect_at(context, actual, i);
        if (entry != variable && r_semantic_effect_contains(context, pattern, entry))
            continue;
        if (!r_type_vector_push(context, &remainder, entry))
            goto cleanup;
    }
    if (variable == 0U) {
        success = remainder.count == 0U;
        goto cleanup;
    }
    RTypeId value;
    if (remainder.count == 1U &&
        (r_generic_parameter_constraints(context, remainder.items[0]) & R_GENERIC_ERRORS) != 0U) {
        value = remainder.items[0];
    } else if (!r_semantic_intern_effect_set(context, &remainder, &value, &duplicate) ||
               (value == 0U && !r_semantic_intern_derived_type(
                                   context, R_SEMANTIC_TYPE_EFFECT_SET, 0U, 0U, 0U, 0U, &value))) {
        goto cleanup;
    }
    success = r_generic_infer_type(context, variable, value, schema, arguments, depth + 1U);
cleanup:
    r_context_free(context, remainder.items);
    return success;
}

static bool r_generic_infer_type(RFrontendContext *context,
                                 RTypeId pattern_id,
                                 RTypeId actual_id,
                                 uint32_t schema_id,
                                 RTypeVector *arguments,
                                 uint32_t depth) {
    const RSemanticType *pattern = r_semantic_type(context, pattern_id);
    const RSemanticType *actual = r_semantic_type(context, actual_id);
    RSemanticType pattern_snapshot;
    RSemanticType actual_snapshot;
    const RGenericSchema *schema = &context->generic_schemas[schema_id - 1U];
    uint32_t index;
    if (depth > context->options.limits.max_nesting || pattern == NULL || actual == NULL) {
        return pattern_id == actual_id;
    }
    /* Recursive dimension inference can grow the interned type table. */
    pattern_snapshot = *pattern;
    actual_snapshot = *actual;
    pattern = &pattern_snapshot;
    actual = &actual_snapshot;
    if (pattern->kind == R_SEMANTIC_TYPE_EFFECT_CARRIER) {
        const bool checked = actual->kind == R_SEMANTIC_TYPE_EFFECT_CARRIER;
        return r_generic_infer_type(context,
                                    pattern->base,
                                    checked ? actual->base : actual_id,
                                    schema_id,
                                    arguments,
                                    depth + 1U) &&
               r_generic_infer_effects(context,
                                       pattern->second,
                                       checked ? actual->second : 0U,
                                       schema_id,
                                       arguments,
                                       depth + 1U);
    }
    if (pattern->kind == R_SEMANTIC_TYPE_EFFECT_SET && actual->kind == R_SEMANTIC_TYPE_EFFECT_SET)
        return r_generic_infer_effects(
            context, pattern_id, actual_id, schema_id, arguments, depth + 1U);
    if (pattern->kind == R_SEMANTIC_TYPE_PARAMETER) {
        for (index = 0U; index < schema->parameter_count; ++index) {
            /* Inside a branch of `@if` the parameter record names its refinement; a signature
               still names the declared parameter. */
            if ((context->generic_parameters[schema->first_parameter + index].type == pattern_id) ||
                (pattern->base == schema->first_parameter + index + 1U)) {
                if (arguments->items[index] != R_TYPE_ID_INVALID &&
                    arguments->items[index] != actual_id) {
                    return false;
                }
                arguments->items[index] = actual_id;
                return true;
            }
        }
        if ((pattern->base != 0U) && ((size_t)pattern->base <= context->generic_parameter_count) &&
            ((context->generic_parameters[pattern->base - 1U].projection_base !=
              R_TYPE_ID_INVALID) ||
             (context->generic_parameters[pattern->base - 1U].application_base !=
              R_TYPE_ID_INVALID))) {
            /* R-TYPE-0045: a projection or an application follows its base; it is checked after
               substitution. */
            return true;
        }
        if ((pattern->base != 0U) && ((size_t)pattern->base <= context->generic_parameter_count) &&
            context->generic_parameters[pattern->base - 1U].pack_derived &&
            (pattern_id != actual_id)) {
            /* R-TYPE-0053 (L18.4): `(A, T...)` takes a tuple element by element. */
            return r_pack_infer_derived(
                context, pattern_id, actual_id, schema_id, arguments, depth + 1U);
        }
        return pattern_id == actual_id;
    }
    if (pattern->kind != actual->kind) {
        if (pattern->kind == R_SEMANTIC_TYPE_FUNCTION) {
            /* R-TYPE-0054 (L28): a function item argument supplies the parameters and result of
               its signature; the conversion is checked after inference. */
            const RSemanticAggregate *item = r_semantic_aggregate_for_type(context, actual_id);
            const RSemanticType *signature =
                (item == NULL || item->function_item_target == R_SYMBOL_ID_INVALID)
                    ? NULL
                    : r_semantic_type(context, item->closure_signature);
            if ((signature != NULL) && (signature->length == pattern->length)) {
                const RTypeId result = signature->base;
                const RTypeId parameters = signature->second;
                return r_generic_infer_type(
                           context, pattern->base, result, schema_id, arguments, depth + 1U) &&
                       r_generic_infer_type(context,
                                            pattern->second,
                                            parameters,
                                            schema_id,
                                            arguments,
                                            depth + 1U);
            }
        }
        if (pattern->kind == R_SEMANTIC_TYPE_SLICE &&
            (pattern->flags & R_SEMANTIC_TYPE_FLAG_SHARED) != 0U &&
            (actual->kind == R_SEMANTIC_TYPE_FIXED_ARRAY ||
             actual->kind == R_SEMANTIC_TYPE_ARRAY)) {
            return r_generic_infer_type(
                context, pattern->base, actual->base, schema_id, arguments, depth + 1U);
        }
        return !r_generic_type_is_dependent(context, pattern_id, 0U);
    }
    if (pattern->kind == R_SEMANTIC_TYPE_STRUCT || pattern->kind == R_SEMANTIC_TYPE_ENUM) {
        const RSemanticAggregate *left = &context->semantic_aggregates[pattern->base - 1U];
        const RSemanticAggregate *right = &context->semantic_aggregates[actual->base - 1U];
        if (left->generic_origin == 0U || right->generic_origin != left->generic_origin) {
            return pattern_id == actual_id;
        }
        for (index = 0U;
             index < context->generic_schemas[left->generic_schema - 1U].parameter_count;
             ++index) {
            if (!r_generic_infer_type(
                    context,
                    context->generic_arguments[left->first_generic_argument + index],
                    context->generic_arguments[right->first_generic_argument + index],
                    schema_id,
                    arguments,
                    depth + 1U)) {
                return false;
            }
        }
        return true;
    }
    if (pattern->kind == R_SEMANTIC_TYPE_FIXED_ARRAY && pattern->second != 0U) {
        const RTypeId element = pattern->base;
        const RTypeId bound = pattern->second;
        const RTypeId actual_element = actual->base;
        RTypeId actual_bound = actual->second;
        const RSemanticType *parameter = r_semantic_type(context, bound);
        if (parameter != NULL && parameter->kind == R_SEMANTIC_TYPE_PARAMETER &&
            context->generic_parameters[parameter->base - 1U].constant_type != 0U) {
            if (actual_bound == 0U && !r_semantic_intern_derived_type(context,
                                                                      R_SEMANTIC_TYPE_CONSTANT_EXPR,
                                                                      0U,
                                                                      0U,
                                                                      actual->length,
                                                                      R_TOKEN_INVALID,
                                                                      &actual_bound))
                return false;
            if (!r_generic_infer_type(
                    context, bound, actual_bound, schema_id, arguments, depth + 1U))
                return false;
        }
        return r_generic_infer_type(
            context, element, actual_element, schema_id, arguments, depth + 1U);
    }
    if ((pattern->kind == R_SEMANTIC_TYPE_RAW_FUNCTION) &&
        r_pack_chain_expands(context, pattern->second)) {
        /* R-TYPE-0053 (L18.4): `P...` takes the remaining parameters of the signature. */
        return r_generic_infer_type(
                   context, pattern->base, actual->base, schema_id, arguments, depth + 1U) &&
               r_pack_infer_chain(
                   context, pattern->second, actual->second, schema_id, arguments, depth + 1U);
    }
    if (pattern->kind >= R_SEMANTIC_TYPE_FIXED_ARRAY) {
        return pattern->length == actual->length &&
               r_generic_infer_type(
                   context, pattern->base, actual->base, schema_id, arguments, depth + 1U) &&
               r_generic_infer_type(
                   context, pattern->second, actual->second, schema_id, arguments, depth + 1U);
    }
    return true; /* Ordinary conversions are checked after inference. */
}

static bool r_generic_apply_function(RFrontendContext *context,
                                     RSymbolId origin_id,
                                     const RTypeVector *arguments,
                                     RSymbolId *result) {
    RSemanticSymbol symbol = context->semantic_symbols[origin_id - 1U];
    RTypeVector parameters = {0};
    size_t index;
    bool success = false;
    for (index = 0U; index < context->semantic_symbol_count; ++index) {
        const RSemanticSymbol *candidate = &context->semantic_symbols[index];
        if (candidate->kind == R_SEMANTIC_SYMBOL_FUNCTION &&
            candidate->generic_origin == origin_id &&
            memcmp(context->generic_arguments + candidate->first_generic_argument,
                   arguments->items,
                   arguments->count * sizeof(RTypeId)) == 0) {
            *result = (RSymbolId)index + 1U;
            return true;
        }
    }
    if (!r_generic_instance_name(context,
                                 symbol.is_overloaded ? symbol.overload_identity
                                                      : symbol.name_intern_id,
                                 arguments,
                                 &symbol.name_intern_id) ||
        !r_generic_commit_arguments(context, arguments, &symbol.first_generic_argument)) {
        return false;
    }
    for (index = 0U; index < symbol.parameter_count; ++index) {
        RTypeId parameter;
        if (!r_generic_substitute_type(
                context,
                context->semantic_parameter_types[symbol.first_parameter_type + index],
                symbol.generic_schema,
                symbol.first_generic_argument,
                &parameter) ||
            !r_type_vector_push(context, &parameters, parameter)) {
            goto cleanup;
        }
    }
    if (!r_semantic_commit_parameter_types(context, &parameters, &symbol.first_parameter_type) ||
        !r_generic_substitute_type(context,
                                   symbol.return_type,
                                   symbol.generic_schema,
                                   symbol.first_generic_argument,
                                   &symbol.return_type) ||
        !r_generic_substitute_type(context,
                                   symbol.throws_type,
                                   symbol.generic_schema,
                                   symbol.first_generic_argument,
                                   &symbol.throws_type) ||
        !r_generic_substitute_type(context,
                                   symbol.effect_carrier_type,
                                   symbol.generic_schema,
                                   symbol.first_generic_argument,
                                   &symbol.effect_carrier_type) ||
        !r_generic_substitute_type(context,
                                   symbol.async_task_type,
                                   symbol.generic_schema,
                                   symbol.first_generic_argument,
                                   &symbol.async_task_type) ||
        !r_generic_substitute_type(context,
                                   symbol.async_start_type,
                                   symbol.generic_schema,
                                   symbol.first_generic_argument,
                                   &symbol.async_start_type)) {
        goto cleanup;
    }
    if (!r_generic_substitute_type(context,
                                   symbol.associated_type,
                                   symbol.generic_schema,
                                   symbol.first_generic_argument,
                                   &symbol.associated_type)) {
        goto cleanup;
    }
    if (symbol.throws_type == R_TYPE_ID_INVALID)
        symbol.effect_carrier_type = R_TYPE_ID_INVALID;
    if (symbol.closure_aggregate != 0U) {
        const RSemanticAggregate *environment =
            r_semantic_aggregate_for_type(context, symbol.associated_type);
        if (environment == NULL)
            goto cleanup;
        symbol.closure_aggregate = (uint32_t)(environment - context->semantic_aggregates) + 1U;
    }
    if (!r_semantic_type_is_direct_shared_borrow_output(context, symbol.return_type) &&
        !r_semantic_type_contains_borrow(context, symbol.return_type) &&
        !r_semantic_type_contains_slice(context, symbol.return_type) &&
        !r_generic_type_is_dependent(context, symbol.return_type, 0U)) {
        symbol.return_borrow_parameter = UINT32_C(0);
        symbol.return_borrow_parameters_low = UINT64_C(0);
        symbol.return_borrow_parameters_high = UINT64_C(0);
        symbol.return_borrow_held_low = UINT64_C(0);
        symbol.return_borrow_held_high = UINT64_C(0);
    }
    symbol.generic_origin = origin_id;
    symbol.generic_depth = context->generic_instantiation_depth + 1U;
    symbol.generic_parent = context->generic_current_function;
    if (symbol.generic_depth > context->options.limits.max_nesting) {
        r_generic_limit_diagnostic(context, symbol.name_intern_id, symbol.name_span);
        context->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
        goto cleanup;
    }
    symbol.hir_node = R_HIR_NODE_ID_INVALID;
    symbol.hir_building = false;
    success = r_semantic_append_symbol(context, symbol, result);
cleanup:
    r_context_free(context, parameters.items);
    return success;
}

/* R-TYPE-0044: the callable signature a closure type implements, or invalid. */
static RTypeId r_generic_closure_signature(const RFrontendContext *context,
                                           RTypeId type_id,
                                           uint32_t *impl_trait) {
    const RSemanticAggregate *aggregate =
        r_semantic_aggregate_for_type(context, r_semantic_value_type(context, type_id));
    size_t index;

    if (impl_trait != NULL) {
        *impl_trait = UINT32_C(0);
    }
    const RSemanticType *parameter_type =
        r_semantic_type(context, r_semantic_value_type(context, type_id));
    if (parameter_type != NULL && parameter_type->kind == R_SEMANTIC_TYPE_PARAMETER &&
        parameter_type->base != 0U) {
        const RGenericParameter *parameter =
            &context->generic_parameters[parameter_type->base - 1U];
        RTypeId signature = 0U;
        for (uint32_t bound = 0U; bound < parameter->trait_constraint_count; ++bound) {
            const uint32_t trait =
                context->generic_trait_constraints[parameter->first_trait_constraint + bound];
            const RTypeId candidate = context->semantic_traits[trait - 1U].callable_signature;
            if (candidate == 0U)
                continue;
            if (signature != 0U && signature != candidate)
                return R_TYPE_ID_INVALID;
            signature = candidate;
            if (impl_trait != NULL)
                *impl_trait = trait;
        }
        return signature;
    }
    if ((aggregate == NULL) || !aggregate->is_closure) {
        return R_TYPE_ID_INVALID;
    }
    const RTypeId signature = aggregate->closure_signature;
    const uint32_t origin = aggregate->generic_origin;
    for (index = 0U; index < context->semantic_impl_count; ++index) {
        const RSemanticImpl *impl = &context->semantic_impls[index];
        const RSemanticTrait *trait;
        const RSemanticAggregate *target =
            r_semantic_aggregate_for_type(context, impl->target_type);
        if ((impl->trait == UINT32_C(0)) || target == NULL ||
            (target != aggregate && (origin == 0U || target->generic_origin != origin))) {
            continue;
        }
        trait = &context->semantic_traits[impl->trait - 1U];
        if (trait->callable_signature != R_TYPE_ID_INVALID) {
            if (impl_trait != NULL) {
                *impl_trait = impl->trait;
            }
            return signature;
        }
    }
    return signature;
}

/* Function items have no captured state and support every synchronous receiver mode. */
static bool r_function_item_signature(RFrontendContext *context,
                                      RTypeId item_type,
                                      RTypeId required,
                                      RTypeId *signature) {
    const RSemanticAggregate *item = r_semantic_aggregate_for_type(context, item_type);
    const RSemanticType *pattern = r_semantic_type(context, required);
    const RSemanticType *provided = r_semantic_type(context, *signature);
    if (item == NULL || item->function_item_target == 0U || pattern == NULL || provided == NULL ||
        (provided->flags & R_SEMANTIC_TYPE_FLAG_CALL_ASYNC) != 0U)
        return true;
    const uint32_t modes = R_SEMANTIC_TYPE_FLAG_CALL_MUT | R_SEMANTIC_TYPE_FLAG_CALL_ONCE;
    const RSemanticType type = *provided;
    return r_semantic_intern_derived_type(context,
                                          type.kind,
                                          type.base,
                                          type.second,
                                          type.length,
                                          (type.flags & ~modes) | (pattern->flags & modes),
                                          signature);
}

/* R-TYPE-0044: unifies every callable constraint of an inferred closure argument with the
   signature of that closure, so that the parameters named only by the constraint follow. */
static bool r_generic_infer_from_callables(RFrontendContext *context,
                                           uint32_t schema_id,
                                           RTypeVector *arguments,
                                           bool *consistent) {
    const RGenericSchema schema = context->generic_schemas[schema_id - 1U];
    uint32_t index;

    for (index = 0U; index < schema.parameter_count; ++index) {
        const RGenericParameter *parameter =
            &context->generic_parameters[schema.first_parameter + index];
        RTypeId closure_signature;
        uint32_t constraint;
        if (arguments->items[index] == R_TYPE_ID_INVALID) {
            continue;
        }
        closure_signature = r_generic_closure_signature(context, arguments->items[index], NULL);
        if (closure_signature == R_TYPE_ID_INVALID) {
            continue;
        }
        for (constraint = 0U; constraint < parameter->trait_constraint_count; ++constraint) {
            const uint32_t trait =
                context->generic_trait_constraints[parameter->first_trait_constraint + constraint];
            const RTypeId pattern = context->semantic_traits[trait - 1U].callable_signature;
            if (pattern == R_TYPE_ID_INVALID) {
                continue;
            }
            if (!r_function_item_signature(
                    context, arguments->items[index], pattern, &closure_signature))
                return false;
            if (!r_generic_infer_type(
                    context, pattern, closure_signature, schema_id, arguments, 0U)) {
                *consistent = false;
                return true;
            }
        }
    }
    return true;
}

/* R-TYPE-0044: the trait of a callable constraint after the schema arguments are substituted
   into its signature; the constraint's own trait when nothing depends on the schema. */
static bool r_generic_effective_callable_trait(RFrontendContext *context,
                                               uint32_t trait,
                                               uint32_t schema_id,
                                               const RTypeVector *arguments,
                                               RSourceId module_source,
                                               uint32_t *effective) {
    const RTypeId signature = context->semantic_traits[trait - 1U].callable_signature;
    uint32_t first_argument;
    RTypeId substituted;

    *effective = trait;
    if (context->semantic_traits[trait - 1U].generic_origin != 0U) {
        return r_generic_commit_arguments(context, arguments, &first_argument) &&
               r_semantic_effective_trait(context, trait, schema_id, first_argument, effective);
    }
    if ((signature == R_TYPE_ID_INVALID) || !r_generic_type_is_dependent(context, signature, 0U)) {
        return true;
    }
    /* Substitution may intern persistent aggregate instances. Their argument ranges must
       outlive this operation, including ranges allocated recursively by substitution. */
    if (!r_generic_commit_arguments(context, arguments, &first_argument) ||
        !r_generic_substitute_type(context, signature, schema_id, first_argument, &substituted)) {
        return false;
    }
    if (substituted == signature) {
        return true;
    }
    return r_semantic_callable_trait(context, module_source, substituted, effective);
}

/* Reserve only a direct named Move. Nested calls consume their own arguments normally. */
static bool r_generic_stage_argument(RBodyContext *body, RAstRef argument, bool staged) {
    bool direct_move, contains_move;
    if (!r_body_ast_direct_move_name(body->frontend, argument, &direct_move, &contains_move))
        return false;
    body->staging_async_move_argument = staged && direct_move;
    return true;
}

/* R-ARRAY-0002, R-EXPR-0020: a range argument takes a slice parameter as its context; under a
   dependent element type the context keeps the parameter's access and takes the element of
   the sliced base. Sets *context_type to R_TYPE_ID_INVALID for any other argument. */
static bool r_generic_range_argument_context(RBodyContext *body,
                                             RAstRef argument,
                                             RTypeId pattern_type,
                                             RTypeId *context_type) {
    const RSemanticType *pattern =
        r_semantic_type(body->frontend, r_semantic_value_type(body->frontend, pattern_type));
    RAstRef direct = argument;
    RAstRef last_suffix;
    RAstNodeView view;
    RAstNodeView last_view;
    uint32_t depth = 0U;

    *context_type = R_TYPE_ID_INVALID;
    if ((pattern == NULL) || (pattern->kind != R_SEMANTIC_TYPE_SLICE)) {
        return true;
    }
    while (r_ast_ref_view(body->frontend, direct, &view) &&
           (view.kind == R_SYNTAX_EXPRESSION || view.kind == R_SYNTAX_INITIALIZER ||
            view.kind == R_SYNTAX_BINARY_EXPRESSION)) {
        RAstRef child;
        if (!r_body_ast_single_node_child(body->frontend, direct, &child) ||
            (++depth > r_frontend_tree_depth_limit(body->frontend))) {
            return true;
        }
        direct = child;
    }
    if (!r_ast_ref_view(body->frontend, direct, &view) ||
        (view.kind != R_SYNTAX_POSTFIX_EXPRESSION) || (view.child_count < 2U) ||
        !r_ast_ref_child(body->frontend, direct, view.child_count - 1U, &last_suffix) ||
        !r_ast_ref_view(body->frontend, last_suffix, &last_view)) {
        return true;
    }
    if (last_view.kind != R_SYNTAX_SLICE_SUFFIX) {
        /* M18-8: a view formed by a call, such as `owner.as_slice_mut()`, takes a slice
           parameter that no type argument determines as its destination. */
        if (!r_generic_type_is_dependent(body->frontend, pattern_type, 0U)) {
            *context_type = r_semantic_value_type(body->frontend, pattern_type);
        }
        return true;
    }
    if (!r_generic_type_is_dependent(body->frontend, pattern_type, 0U)) {
        *context_type = r_semantic_value_type(body->frontend, pattern_type);
        return true;
    }
    {
        RTypeId probed = R_TYPE_ID_INVALID;
        const RSemanticType *probed_type;

        if (!r_overload_argument_type(body, direct, 0U, false, &probed)) {
            return false;
        }
        probed_type =
            r_semantic_type(body->frontend, r_semantic_value_type(body->frontend, probed));
        if ((probed_type != NULL) && (probed_type->kind == R_SEMANTIC_TYPE_SLICE)) {
            return r_semantic_intern_derived_type(body->frontend,
                                                  R_SEMANTIC_TYPE_SLICE,
                                                  probed_type->base,
                                                  R_TYPE_ID_INVALID,
                                                  UINT64_C(0),
                                                  pattern->flags,
                                                  context_type);
        }
    }
    return true;
}

/* R-EXPR-0020 (L17.1): the parameters that the arguments of a call leave open take the expected
   type of its result. The expected type fills only those parameters: it never overrides an
   inferred argument, and an expected type the result does not fit infers nothing, so the
   ordinary conversion or mismatch diagnostic applies afterwards. An async call is matched as
   its task, or as its logical result when it is awaited. */
static bool r_generic_infer_from_expected(RFrontendContext *context,
                                          const RSemanticSymbol *function,
                                          RTypeId contextual_type,
                                          RTypeVector *arguments) {
    RTypeVector trial = {0};
    RTypeId pattern = function->return_type;
    bool open = false;
    bool success = true;

    for (size_t index = 0U; index < arguments->count; ++index) {
        open = open || (arguments->items[index] == R_TYPE_ID_INVALID);
    }
    if (!open || (contextual_type == R_TYPE_ID_INVALID)) {
        return true;
    }
    if (function->is_async &&
        (r_semantic_value_kind(context, contextual_type) == R_SEMANTIC_TYPE_TASK)) {
        pattern = function->async_task_type;
    }
    for (size_t index = 0U; success && (index < arguments->count); ++index) {
        success = r_type_vector_push(context, &trial, arguments->items[index]);
    }
    if (success && (pattern != R_TYPE_ID_INVALID) &&
        r_generic_infer_type(context,
                             r_semantic_value_type(context, pattern),
                             r_semantic_value_type(context, contextual_type),
                             function->generic_schema,
                             &trial,
                             0U)) {
        for (size_t index = 0U; index < arguments->count; ++index) {
            if (arguments->items[index] == R_TYPE_ID_INVALID) {
                arguments->items[index] = trial.items[index];
            }
        }
    }
    r_context_free(context, trial.items);
    return success && (context->resource_status == R_FRONTEND_OK);
}

/* R-AM-0009, R-TYPE-0053: an argument before a spread is evaluated into a hidden object before
   the spread's operand. The direct `move place` operand of an async start stays a staged move,
   whose evaluation has no effect. */
static bool r_generic_stage_before_spread(RBodyContext *body,
                                          const RSemanticSymbol *function,
                                          RAstRef argument,
                                          RExpressionResult *value,
                                          RHirVector *staging) {
    bool direct_move = false;
    bool contains_move = false;
    if (function->is_async &&
        !r_body_ast_direct_move_name(body->frontend, argument, &direct_move, &contains_move)) {
        return false;
    }
    return (function->is_async && direct_move) || r_format_stage_operand(body, value, staging);
}

static bool r_generic_infer_call(RBodyContext *body,
                                 RSymbolId *function_id,
                                 RAstRef argument_list,
                                 const RAstNodeView *view,
                                 uint32_t first_argument,
                                 bool staged,
                                 const RExpressionResult *receiver,
                                 RExpressionResult **values,
                                 RHirVector *prefix,
                                 RHirVector *statements,
                                 RTypeId contextual_type) {
    RFrontendContext *context = body->frontend;
    RSemanticSymbol function = context->semantic_symbols[*function_id - 1U];
    RGenericSchema schema = context->generic_schemas[function.generic_schema - 1U];
    const uint32_t receiver_count = (receiver != NULL) ? UINT32_C(1) : UINT32_C(0);
    const uint32_t value_count = view->child_count - first_argument + receiver_count;
    /* R-TYPE-0053 (L18.2): the arguments after the fixed parameters form the pack. */
    const bool pack_call = function.is_pack_variadic && (function.parameter_count != 0U);
    const uint32_t fixed_count =
        pack_call ? function.parameter_count - 1U : function.parameter_count;
    RAstRef spread_argument = {0};
    RAstNodeView spread_view = {0};
    /* R-TYPE-0053 (L18.3): a trailing spread of a tuple or a pack stands for its elements. */
    const bool has_spread = (value_count > receiver_count) &&
                            r_ast_ref_child(context,
                                            argument_list,
                                            first_argument + value_count - 1U - receiver_count,
                                            &spread_argument) &&
                            r_ast_ref_view(context, spread_argument, &spread_view) &&
                            (spread_view.kind == R_SYNTAX_SPREAD_ARGUMENT);
    const uint32_t source_count = has_spread ? value_count - 1U : value_count;
    /* The arguments before a spread are evaluated, in order, before its operand. */
    RHirVector *const staging = has_spread ? statements : prefix;
    const uint32_t value_capacity =
        (value_count > function.parameter_count ? value_count : function.parameter_count) + 1U;
    RTypeVector arguments = {0};
    uint32_t index;
    bool success = false;
    if (!has_spread &&
        (pack_call ? (value_count < fixed_count) : (value_count != function.parameter_count))) {
        return r_body_diagnostic(body,
                                 "R-DIAG-TYPE-001",
                                 "R-EXPR-0020",
                                 "generic function call has the wrong argument count",
                                 view->span);
    }
    if (has_spread && (statements == NULL)) {
        return r_body_diagnostic(body,
                                 "R-DIAG-TYPE-001",
                                 "R-TYPE-0053",
                                 "a spread is not available in this call",
                                 spread_view.span);
    }
    /* R-FUNC-0013: a call without value arguments still infers from the owner spelling. */
    *values = r_context_allocate(context, (size_t)value_capacity * sizeof(**values));
    if (*values == NULL) {
        return false;
    }
    (void)memset(*values, 0, (size_t)value_capacity * sizeof(**values));
    /* R-TYPE-0053 (L18.4): a pack function closed by explicit arguments keeps them. */
    const bool closed = function.generic_origin != 0U;
    const RSemanticAggregate *closed_pack =
        closed && pack_call
            ? r_tuple_aggregate(
                  context,
                  context->semantic_parameter_types[function.first_parameter_type + fixed_count])
            : NULL;
    for (index = 0U; index < schema.parameter_count; ++index) {
        if (!r_type_vector_push(
                context,
                &arguments,
                closed ? context->generic_arguments[function.first_generic_argument + index]
                       : R_TYPE_ID_INVALID)) {
            goto cleanup;
        }
    }
    if (function.is_method && (body->associated_owner_type != R_TYPE_ID_INVALID)) {
        /* R-FUNC-0013: `Owner(args)::name(...)` spells the schema prefix of the method. */
        const RSemanticAggregate *owner =
            r_semantic_aggregate_for_type(context, body->associated_owner_type);
        body->associated_owner_type = R_TYPE_ID_INVALID;
        if ((owner != NULL) && (owner->generic_origin != 0U)) {
            const uint32_t owner_prefix =
                context->generic_schemas[owner->generic_schema - 1U].parameter_count;
            for (index = 0U; (index < owner_prefix) && (index < schema.parameter_count); ++index) {
                arguments.items[index] =
                    context->generic_arguments[owner->first_generic_argument + index];
            }
        }
    }
    if (!pack_call && (source_count > fixed_count)) {
        success = r_body_diagnostic(body,
                                    "R-DIAG-TYPE-001",
                                    "R-EXPR-0020",
                                    "generic function call has the wrong argument count",
                                    view->span);
        goto cleanup;
    }
    for (index = 0U; index < source_count; ++index) {
        RAstRef argument = {0};
        RExpressionResult *value = &(*values)[index];
        const bool previous_staging = body->staging_async_move_argument;
        if (index >= fixed_count) {
            /* R-TYPE-0053 (L18.2): an element of the pack. */
            RAstNodeView argument_view;
            if (!r_ast_ref_child(
                    context, argument_list, first_argument + index - receiver_count, &argument) ||
                !r_ast_ref_view(context, argument, &argument_view)) {
                context->resource_status = R_FRONTEND_INTERNAL_ERROR;
                goto cleanup;
            }
            body->staging_async_move_argument = staged;
            const RTypeId element_context =
                (closed_pack != NULL) && (index - fixed_count < closed_pack->field_count)
                    ? r_semantic_value_type(
                          context,
                          context->semantic_fields[closed_pack->first_field + index - fixed_count]
                              .type)
                    : R_TYPE_ID_INVALID;
            const bool lowered = r_generic_stage_argument(body, argument, staged) &&
                                 r_body_lower_value(body, argument, element_context, value);
            body->staging_async_move_argument = previous_staging;
            if (!lowered) {
                goto cleanup;
            }
            if (!value->valid) {
                success = true;
                goto cleanup;
            }
            if (value->is_integer_literal_form &&
                !r_body_finalize_integer_literal(
                    body, value, element_context, r_body_literal_is_negative(body, value))) {
                goto cleanup;
            }
            if (has_spread &&
                !r_generic_stage_before_spread(body, &function, argument, value, staging)) {
                goto cleanup;
            }
            continue;
        }
        body->staging_async_move_argument = staged;
        const RSemanticType *pattern = r_semantic_type(
            context, context->semantic_parameter_types[function.first_parameter_type + index]);
        const uint32_t access = pattern != NULL && pattern->kind == R_SEMANTIC_TYPE_BORROW
                                    ? pattern->flags
                                    : R_SEMANTIC_TYPE_FLAG_NONE;
        RTypeId range_context = R_TYPE_ID_INVALID;
        if (index < receiver_count) {
            *value = *receiver;
        } else if (!r_ast_ref_child(context,
                                    argument_list,
                                    first_argument + index - receiver_count,
                                    &argument) ||
                   !r_generic_stage_argument(body, argument, staged) ||
                   !r_generic_range_argument_context(
                       body,
                       argument,
                       context->semantic_parameter_types[function.first_parameter_type + index],
                       &range_context) ||
                   ((range_context != R_TYPE_ID_INVALID)
                        ? !(function.is_async
                                ? r_body_lower_value(body, argument, range_context, value)
                                : r_body_lower_call_bounded_value(
                                      body, argument, range_context, value))
                        : !r_generic_lower_argument(
                              body,
                              argument,
                              context->semantic_parameter_types[function.first_parameter_type +
                                                                index],
                              access,
                              value))) {
            body->staging_async_move_argument = previous_staging;
            goto cleanup;
        }
        body->staging_async_move_argument = previous_staging;
        if (value->valid && value->is_integer_literal_form &&
            !r_body_finalize_integer_literal(
                body, value, R_TYPE_ID_INVALID, r_body_literal_is_negative(body, value))) {
            goto cleanup;
        }
        if (value->valid && r_semantic_value_kind(context, value->type) == R_SEMANTIC_TYPE_NULL_T &&
            r_body_hir_node(body, value->node)->kind == R_HIR_LITERAL &&
            r_body_hir_node(body, value->node)->operation == R_TOKEN_KW_NULL) {
            /* A literal has no evaluation to stage and no pointee type to infer.
             * Finish it under the closed parameter type after the other arguments. */
            continue;
        }
        if (!value->valid && (context->diagnostic_count != 0U)) {
            /* M24-6: an argument that an earlier diagnostic left without a value, such as a
               borrow for a callee whose own declaration was rejected, implies no type
               argument; the call is not checked further. */
            r_body_poison(body);
            success = true;
            goto cleanup;
        }
        if (!value->valid ||
            !r_generic_infer_type(
                context,
                r_semantic_value_type(
                    context,
                    context->semantic_parameter_types[function.first_parameter_type + index]),
                r_semantic_value_type(context, value->type),
                function.generic_schema,
                &arguments,
                0U)) {
            success =
                r_body_diagnostic(body,
                                  "R-DIAG-TYPE-001",
                                  "R-EXPR-0020",
                                  "generic function arguments imply inconsistent type arguments",
                                  view->span);
            goto cleanup;
        }
        if (!has_spread ? ((staging != NULL) && !r_format_stage_operand(body, value, staging))
            : (index >= receiver_count)
                ? !r_generic_stage_before_spread(body, &function, argument, value, staging)
                : (!function.is_async && !r_format_stage_operand(body, value, staging))) {
            goto cleanup;
        }
    }
    if (has_spread) {
        RAstRef operand_ref;
        RExpressionResult operand = {0};
        bool spread_valid = false;
        const bool previous_staging = body->staging_async_move_argument;
        if (!r_body_ast_single_node_child(context, spread_argument, &operand_ref)) {
            context->resource_status = R_FRONTEND_INTERNAL_ERROR;
            goto cleanup;
        }
        body->staging_async_move_argument = false;
        const bool lowered = r_body_lower_value(body, operand_ref, R_TYPE_ID_INVALID, &operand);
        body->staging_async_move_argument = previous_staging;
        if (!lowered) {
            goto cleanup;
        }
        if (!operand.valid) {
            success = true;
            goto cleanup;
        }
        /* R-TYPE-0044 (L18.4): a callable signature `fn(..., P...)` takes the elements. */
        const bool splice =
            pack_call && (function.trait != 0U) &&
            (context->semantic_traits[function.trait - 1U].callable_signature != R_TYPE_ID_INVALID);
        if (!r_pack_spread_call_values(body,
                                       &operand,
                                       spread_view.span,
                                       statements,
                                       fixed_count,
                                       source_count,
                                       pack_call,
                                       splice,
                                       function.is_async,
                                       *values,
                                       &spread_valid)) {
            goto cleanup;
        }
        if (!spread_valid) {
            success = true;
            goto cleanup;
        }
        for (index = source_count; index < fixed_count; ++index) {
            if (!r_generic_infer_type(
                    context,
                    r_semantic_value_type(
                        context,
                        context->semantic_parameter_types[function.first_parameter_type + index]),
                    r_semantic_value_type(context, (*values)[index].type),
                    function.generic_schema,
                    &arguments,
                    0U)) {
                success = r_body_diagnostic(body,
                                            "R-DIAG-TYPE-001",
                                            "R-EXPR-0020",
                                            "generic function arguments imply inconsistent type "
                                            "arguments",
                                            view->span);
                goto cleanup;
            }
        }
    } else if (pack_call) {
        /* R-TYPE-0053 (L18.2): the pack is the tuple of the trailing arguments. */
        RExpressionResult pack;
        if (!r_tuple_build(
                body, &(*values)[fixed_count], value_count - fixed_count, view->span, &pack)) {
            goto cleanup;
        }
        if (!pack.valid) {
            success = true;
            goto cleanup;
        }
        (*values)[fixed_count] = pack;
    }
    if (pack_call &&
        !r_generic_infer_type(
            context,
            r_semantic_value_type(
                context,
                context->semantic_parameter_types[function.first_parameter_type + fixed_count]),
            r_semantic_value_type(context, (*values)[fixed_count].type),
            function.generic_schema,
            &arguments,
            0U)) {
        success = r_body_diagnostic(body,
                                    "R-DIAG-TYPE-001",
                                    "R-EXPR-0020",
                                    "generic function arguments imply inconsistent type arguments",
                                    view->span);
        goto cleanup;
    }
    {
        /* R-TYPE-0044: the signature of a closure argument determines the parameters that its
           callable constraint names. */
        bool consistent = true;
        if (!r_generic_infer_from_callables(
                context, function.generic_schema, &arguments, &consistent)) {
            goto cleanup;
        }
        if (!consistent) {
            success = r_body_diagnostic(body,
                                        "R-DIAG-TYPE-001",
                                        "R-TYPE-0044",
                                        "closure argument does not fit the callable constraint of "
                                        "its parameter",
                                        view->span);
            goto cleanup;
        }
    }
    if (!r_generic_infer_from_expected(context, &function, contextual_type, &arguments)) {
        goto cleanup;
    }
    for (index = 0U; index < schema.parameter_count; ++index) {
        if (arguments.items[index] == R_TYPE_ID_INVALID) {
            success = r_body_diagnostic(body,
                                        "R-DIAG-TYPE-001",
                                        "R-EXPR-0020",
                                        "generic type parameter cannot be inferred from value "
                                        "arguments or the expected result type; name it "
                                        "explicitly with name::<...>",
                                        view->span);
            goto cleanup;
        }
    }
    success =
        closed ||
        (r_generic_validate_arguments(context, function.generic_schema, &arguments, view->span) &&
         r_generic_apply_function(context, *function_id, &arguments, function_id));
cleanup:
    r_context_free(context, arguments.items);
    return success;
}

/* R-TYPE-0036: one explicit argument for one generic parameter. A set parameter takes
   `throws(...)`, a constant parameter a checked constant, a type parameter a type. */
static bool r_generic_explicit_argument(RBodyContext *body,
                                        const RGenericParameter *parameter,
                                        RAstRef argument,
                                        RTypeId *result) {
    RFrontendContext *context = body->frontend;
    RAstNodeView view;
    RAstRef expression;
    uint64_t value = 0U;
    const size_t diagnostics = context->diagnostic_count;
    const bool set = (parameter->constraints & R_GENERIC_ERRORS) != 0U;
    *result = R_TYPE_ID_INVALID;
    if (!r_ast_ref_view(context, argument, &view)) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    if (set != (view.kind == R_SYNTAX_THROWS_CLAUSE)) {
        return r_body_diagnostic(body,
                                 "R-DIAG-TYPE-001",
                                 "R-TYPE-0036",
                                 set ? "an errors parameter takes a throws(...) argument"
                                     : "throws(...) is an argument only for an errors parameter",
                                 view.span);
    }
    if (set) {
        RAstRef first;
        bool supported = true;
        if (!r_ast_find_direct_child(context, argument, R_SYNTAX_TYPE, &first)) {
            return r_semantic_intern_derived_type(
                context, R_SEMANTIC_TYPE_EFFECT_SET, 0U, 0U, 0U, 0U, result);
        }
        if (view.child_count == 2U) {
            if (r_semantic_type_from_ast(context, first, result)) {
                /* A set parameter in scope is forwarded as itself, as inference does. */
                if ((r_generic_parameter_constraints(context, *result) & R_GENERIC_ERRORS) != 0U) {
                    return true;
                }
            } else if (context->resource_status != R_FRONTEND_OK) {
                return false;
            } else if (context->diagnostic_count != diagnostics) {
                *result = R_TYPE_ID_INVALID;
                r_body_poison(body);
                return true;
            }
        }
        if (!r_semantic_collect_throws_clause(context, argument, false, result, &supported)) {
            return false;
        }
        if (!supported) {
            *result = R_TYPE_ID_INVALID;
            r_body_poison(body);
        }
        return true;
    }
    if (parameter->constant_type == 0U &&
        r_ast_find_direct_child(context, argument, R_SYNTAX_EXPRESSION, &expression)) {
        return r_body_diagnostic(body,
                                 "R-DIAG-TYPE-001",
                                 "R-TYPE-0036",
                                 "a generic type parameter requires a type argument",
                                 view.span);
    }
    if (parameter->constant_type != 0U
            ? r_generic_bound(
                  context, argument, 0U, parameter->constant_type, result, &value, false)
            : r_semantic_type_from_ast(context, argument, result)) {
        return true;
    }
    if (context->resource_status != R_FRONTEND_OK) {
        return false;
    }
    *result = R_TYPE_ID_INVALID;
    if (context->diagnostic_count != diagnostics) {
        r_body_poison(body);
        return true;
    }
    return r_body_diagnostic(body,
                             "R-DIAG-TYPE-001",
                             "R-TYPE-0036",
                             parameter->constant_type != 0U
                                 ? "constant generic argument requires a checked constant"
                                 : "generic argument does not name a type",
                             view.span);
}

/* The explicit list of `name::<...>` fits a candidate when it names every parameter that the
   owner spelling does not supply and each argument has the form its parameter needs. */
static bool r_generic_explicit_fits(const RFrontendContext *context,
                                    const RSemanticSymbol *candidate,
                                    RAstRef list,
                                    uint32_t explicit_count,
                                    uint32_t prefix) {
    const RGenericSchema schema = context->generic_schemas[candidate->generic_schema - 1U];
    if (schema.parameter_count != prefix + explicit_count) {
        return false;
    }
    for (uint32_t index = 0U; index < explicit_count; ++index) {
        RAstRef argument;
        RAstNodeView view;
        RAstRef expression;
        const RGenericParameter *parameter =
            &context->generic_parameters[schema.first_parameter + prefix + index];
        if (!r_ast_ref_child(context, list, index, &argument) ||
            !r_ast_ref_view(context, argument, &view)) {
            return false;
        }
        if (((parameter->constraints & R_GENERIC_ERRORS) != 0U) !=
            (view.kind == R_SYNTAX_THROWS_CLAUSE)) {
            return false;
        }
        if (parameter->constant_type == 0U && view.kind == R_SYNTAX_TYPE &&
            r_ast_find_direct_child(context, argument, R_SYNTAX_EXPRESSION, &expression)) {
            return false;
        }
    }
    return true;
}

/* R-TYPE-0036: `name::<arguments>` closes an open generic function without inference. A list
   may be absent only for a method whose generic owner spelling supplies every parameter. For
   an overload family the list, and for a call the value-argument count, select one member. */
static bool r_generic_close_explicit(RBodyContext *body,
                                     RAstRef list,
                                     bool has_list,
                                     RSymbolId *function_id,
                                     bool calling,
                                     uint32_t value_count,
                                     RSourceSpan span) {
    RFrontendContext *context = body->frontend;
    const RSemanticSymbol *symbol = r_body_symbol(body, *function_id);
    RAstNodeView list_view = {0};
    RTypeVector arguments = {0};
    uint32_t prefix = 0U;
    bool success = false;
    const RTypeId owner_type = body->associated_owner_type;
    body->associated_owner_type = R_TYPE_ID_INVALID;
    if (symbol == NULL) {
        return true;
    }
    if (has_list && !r_ast_ref_view(context, list, &list_view)) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    if (owner_type != R_TYPE_ID_INVALID && symbol->is_method) {
        const RSemanticAggregate *owner = r_semantic_aggregate_for_type(context, owner_type);
        if (owner != NULL && owner->generic_origin != 0U) {
            prefix = context->generic_schemas[owner->generic_schema - 1U].parameter_count;
        }
    }
    if (has_list && symbol->kind == R_SEMANTIC_SYMBOL_FUNCTION && symbol->is_overloaded &&
        symbol->generic_origin == 0U) {
        const RSemanticSymbol family = *symbol;
        RSymbolId selected = R_SYMBOL_ID_INVALID;
        uint32_t matches = 0U;
        for (size_t index = 0U; index < context->semantic_symbol_count; ++index) {
            const RSemanticSymbol *candidate = &context->semantic_symbols[index];
            if (candidate->kind != R_SEMANTIC_SYMBOL_FUNCTION || candidate->generic_origin != 0U ||
                candidate->generic_schema == 0U || candidate->poisoned ||
                !candidate->signature_supported ||
                candidate->module_source != family.module_source ||
                candidate->name_intern_id != family.name_intern_id ||
                candidate->is_method != family.is_method ||
                candidate->associated_type != family.associated_type ||
                (candidate->is_protected && candidate->module_source != body->source) ||
                (calling && candidate->parameter_count != value_count) ||
                !r_generic_explicit_fits(context, candidate, list, list_view.child_count, prefix)) {
                continue;
            }
            selected = (RSymbolId)index + 1U;
            ++matches;
        }
        if (matches != 1U) {
            return r_body_diagnostic(body,
                                     "R-DIAG-TYPE-001",
                                     "R-TYPE-0036",
                                     matches == 0U ? "no generic overload accepts these explicit "
                                                     "generic arguments"
                                                   : "explicit generic arguments are ambiguous "
                                                     "between overloads",
                                     span);
        }
        *function_id = selected;
        symbol = r_body_symbol(body, selected);
    }
    if (symbol->kind != R_SEMANTIC_SYMBOL_FUNCTION || symbol->generic_schema == 0U ||
        symbol->generic_origin != 0U) {
        if (!has_list) {
            return true;
        }
        return r_body_diagnostic(body,
                                 "R-DIAG-TYPE-001",
                                 "R-TYPE-0036",
                                 "explicit generic arguments require an open generic function",
                                 list_view.span);
    }
    const RGenericSchema schema = context->generic_schemas[symbol->generic_schema - 1U];
    const uint32_t explicit_count = has_list ? list_view.child_count : 0U;
    /* R-TYPE-0053 (L18.4): the arguments after the fixed parameters form the pack. */
    const bool has_pack =
        (schema.parameter_count != 0U) &&
        context->generic_parameters[schema.first_parameter + schema.parameter_count - 1U].is_pack;
    const bool packed =
        has_pack && has_list && (prefix + explicit_count + 1U >= schema.parameter_count);
    if (packed) {
        const uint32_t fixed = schema.parameter_count - 1U;
        RTypeVector elements = {0};
        RTypeId pack;
        const RGenericParameter pack_parameter =
            context->generic_parameters[schema.first_parameter + fixed];
        const RGenericParameter carrier =
            pack_parameter.pack_element != R_TYPE_ID_INVALID
                ? *r_generic_pack_carrier(context, pack_parameter.pack_element)
                : pack_parameter;
        bool formed = true;
        for (uint32_t index = 0U; formed && (index < fixed); ++index) {
            RTypeId argument = R_TYPE_ID_INVALID;
            if (index < prefix) {
                const RSemanticAggregate *owner =
                    r_semantic_aggregate_for_type(context, owner_type);
                argument = context->generic_arguments[owner->first_generic_argument + index];
            } else {
                RAstRef source;
                formed = r_ast_ref_child(context, list, index - prefix, &source) &&
                         r_generic_explicit_argument(
                             body,
                             &context->generic_parameters[schema.first_parameter + index],
                             source,
                             &argument);
            }
            formed = formed && (argument != R_TYPE_ID_INVALID) &&
                     r_type_vector_push(context, &arguments, argument);
        }
        for (uint32_t index = fixed - prefix; formed && (index < explicit_count); ++index) {
            RAstRef source;
            RTypeId element = R_TYPE_ID_INVALID;
            formed = r_ast_ref_child(context, list, index, &source) &&
                     r_generic_explicit_argument(body, &carrier, source, &element) &&
                     (element != R_TYPE_ID_INVALID) &&
                     r_tuple_element_valid(context, element, list_view.span) &&
                     r_type_vector_push(context, &elements, element);
        }
        formed = formed && r_tuple_type(context, &elements, list_view.span, &pack) &&
                 r_type_vector_push(context, &arguments, pack);
        r_context_free(context, elements.items);
        if (!formed) {
            if (context->resource_status != R_FRONTEND_OK) {
                goto cleanup;
            }
            success = true;
            r_body_poison(body);
            goto cleanup;
        }
    } else if (schema.parameter_count != prefix + explicit_count) {
        return r_body_diagnostic(
            body,
            "R-DIAG-TYPE-001",
            "R-TYPE-0036",
            has_list ? "an explicit generic argument list shall name every generic parameter "
                       "not supplied by the owner, in header order"
                     : "this generic function requires explicit generic arguments name::<...>",
            has_list ? list_view.span : span);
    }
    for (uint32_t index = 0U; !packed && (index < schema.parameter_count); ++index) {
        RTypeId argument = R_TYPE_ID_INVALID;
        if (index < prefix) {
            const RSemanticAggregate *owner = r_semantic_aggregate_for_type(context, owner_type);
            argument = context->generic_arguments[owner->first_generic_argument + index];
        } else {
            RAstRef source;
            if (!r_ast_ref_child(context, list, index - prefix, &source) ||
                !r_generic_explicit_argument(
                    body,
                    &context->generic_parameters[schema.first_parameter + index],
                    source,
                    &argument)) {
                goto cleanup;
            }
            if (argument == R_TYPE_ID_INVALID) {
                success = true;
                r_body_poison(body);
                goto cleanup;
            }
        }
        if (!r_type_vector_push(context, &arguments, argument)) {
            goto cleanup;
        }
    }
    {
        const size_t diagnostics = context->diagnostic_count;
        if (!r_generic_validate_arguments(
                context, symbol->generic_schema, &arguments, has_list ? list_view.span : span)) {
            goto cleanup;
        }
        if (context->diagnostic_count != diagnostics) {
            r_body_poison(body);
            success = true;
            goto cleanup;
        }
    }
    success = r_generic_apply_function(context, *function_id, &arguments, function_id);
cleanup:
    r_context_free(context, arguments.items);
    return success;
}

/* R-TYPE-0053 (L18.4): the pieces an instance created for one partitioned spread object. */
typedef struct RGenericPackPieces {
    RSymbolId root;
    RSymbolId first;
    uint32_t count;
} RGenericPackPieces;

typedef struct RGenericClone {
    RFrontendContext *context;
    /* The generic definition being instantiated. */
    RSymbolId origin;
    uint32_t schema;
    uint32_t arguments;
    RSymbolId *symbols;
    size_t symbol_count;
    RHirNodeId *nodes;
    size_t node_count;
    /* The definition has reported errors; its instances add no diagnostics of their own. */
    bool origin_poisoned;
    RGenericPackPieces *pack_pieces;
    size_t pack_piece_count;
    size_t pack_piece_capacity;
} RGenericClone;

static bool r_generic_clone_node(RGenericClone *, RHirNodeId, uint32_t, RHirNodeId *);

/* A node an instance adds that has no source node (R-TYPE-0053, L18.4). */
static bool r_generic_clone_append(RFrontendContext *context,
                                   RHirKind kind,
                                   RSourceSpan span,
                                   RTypeId type,
                                   RSymbolId symbol,
                                   RTokenKind operation,
                                   bool is_place,
                                   RHirNodeId child,
                                   RHirNodeId *result) {
    RHirNode node;
    RHirVector children = {&child, 1U, 1U};
    (void)memset(&node, 0, sizeof(node));
    node.kind = kind;
    node.span = span;
    node.type = type;
    node.symbol = symbol;
    node.operation = operation;
    node.is_place = is_place;
    if ((symbol != R_SYMBOL_ID_INVALID) && (kind != R_HIR_FIELD_INIT)) {
        const RSemanticSymbol *named = &context->semantic_symbols[symbol - 1U];
        node.task_scope = named->task_scope;
        node.borrow_origin = named->borrow_origin;
        node.borrow_origin_set = named->borrow_origin_set;
        node.borrow_origin_multiple = named->borrow_origin_multiple;
        node.borrow_referent = named->borrow_referent;
    }
    return r_hir_append_node(context, node, result) &&
           ((child == R_HIR_NODE_ID_INVALID) || r_hir_commit_children(context, *result, &children));
}

/* The transfer of the whole piece `piece` into an argument or an element. */
static bool r_generic_clone_piece_transfer(RFrontendContext *context,
                                           RSymbolId piece,
                                           RSourceSpan span,
                                           RHirNodeId *result) {
    const RTypeId type = context->semantic_symbols[piece - 1U].type;
    const bool copy = r_semantic_type_is_copy(context, type);
    RHirNodeId place;
    return r_generic_clone_append(
               context, R_HIR_PLACE, span, type, piece, R_TOKEN_INVALID, true, 0U, &place) &&
           r_generic_clone_append(context,
                                  copy ? R_HIR_LOAD : R_HIR_MOVE,
                                  span,
                                  type,
                                  piece,
                                  copy ? R_TOKEN_INVALID : R_TOKEN_KW_MOVE,
                                  false,
                                  place,
                                  result);
}

/* The pieces of the partitioned object `root` of the definition in this instance, or NULL. */
static const RGenericPackPieces *r_generic_clone_find_pieces(const RGenericClone *clone,
                                                             RSymbolId root) {
    const RSymbolId closed =
        (root != 0U) && (root <= clone->symbol_count) ? clone->symbols[root - 1U] : 0U;
    for (size_t index = 0U; (closed != 0U) && (index < clone->pack_piece_count); ++index) {
        if (clone->pack_pieces[index].root == closed) {
            return &clone->pack_pieces[index];
        }
    }
    return NULL;
}

static const RGenericPackPieces *r_generic_clone_pieces(const RGenericClone *clone,
                                                        RSymbolId root) {
    const RGenericPackPieces *pieces = r_generic_clone_find_pieces(clone, root);
    if (pieces == NULL) {
        clone->context->resource_status = R_FRONTEND_INTERNAL_ERROR;
    }
    return pieces;
}

/* R-TYPE-0053 (L18.4): the partition of a spread object into one hidden piece per element of the
   tuple its pack became, then the obsolete object is forgotten, as a pattern partitions it
   (R-EXPR-0031). */
static bool
r_generic_clone_partition(RGenericClone *clone, const RHirNode *source, RHirVector *children) {
    RFrontendContext *context = clone->context;
    const RSymbolId root = (source->symbol != 0U) && (source->symbol <= clone->symbol_count)
                               ? clone->symbols[source->symbol - 1U]
                               : 0U;
    const RTypeId root_type = root == 0U ? 0U : context->semantic_symbols[root - 1U].type;
    const RSemanticAggregate *tuple = r_tuple_aggregate(context, root_type);
    RGenericPackPieces pieces = {root, 0U, 0U};
    RTypeId void_type;
    if (tuple == NULL) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    const RSemanticAggregate snapshot = *tuple;
    for (uint32_t index = 0U; index < snapshot.field_count; ++index) {
        const RTypeId element = context->semantic_fields[snapshot.first_field + index].type;
        const bool copy = r_semantic_type_is_copy(context, element);
        RSemanticSymbol piece = context->semantic_symbols[root - 1U];
        RSymbolId piece_id;
        RHirNodeId place;
        RHirNodeId field;
        RHirNodeId transfer;
        RHirNodeId local;
        if (!r_generic_clone_append(context,
                                    R_HIR_PLACE,
                                    source->span,
                                    root_type,
                                    root,
                                    R_TOKEN_INVALID,
                                    true,
                                    0U,
                                    &place) ||
            !r_generic_clone_append(context,
                                    R_HIR_FIELD_PLACE,
                                    source->span,
                                    element,
                                    root,
                                    R_TOKEN_INVALID,
                                    true,
                                    place,
                                    &field)) {
            return false;
        }
        context->hir_nodes[field - 1U].aggregate_member = snapshot.first_field + index + 1U;
        if (!r_generic_clone_append(context,
                                    copy ? R_HIR_LOAD : R_HIR_MOVE,
                                    source->span,
                                    element,
                                    root,
                                    copy ? R_TOKEN_INVALID : R_TOKEN_KW_VARIANT,
                                    false,
                                    field,
                                    &transfer)) {
            return false;
        }
        piece.type = element;
        piece.hir_node = R_HIR_NODE_ID_INVALID;
        piece.object_state = R_SEMANTIC_OBJECT_STATE_INITIALIZED;
        piece.is_hidden_local = true;
        piece.projected_borrow_state = 0U;
        if (!r_semantic_append_symbol(context, piece, &piece_id) ||
            !r_generic_clone_append(context,
                                    R_HIR_LOCAL,
                                    source->span,
                                    element,
                                    piece_id,
                                    R_TOKEN_INVALID,
                                    false,
                                    transfer,
                                    &local) ||
            !r_hir_vector_push(context, children, local)) {
            return false;
        }
        context->semantic_symbols[piece_id - 1U].hir_node = local;
        if (index == 0U) {
            pieces.first = piece_id;
        }
        pieces.count += 1U;
    }
    if (!r_semantic_type_is_copy(context, root_type)) {
        RHirNodeId place;
        RHirNodeId forget;
        if (!r_semantic_type_from_token(context, R_TOKEN_KW_VOID, &void_type) ||
            !r_generic_clone_append(context,
                                    R_HIR_PLACE,
                                    source->span,
                                    root_type,
                                    root,
                                    R_TOKEN_INVALID,
                                    true,
                                    0U,
                                    &place) ||
            !r_generic_clone_append(context,
                                    R_HIR_DROP,
                                    source->span,
                                    void_type,
                                    root,
                                    R_TOKEN_KW_VARIANT,
                                    false,
                                    place,
                                    &forget) ||
            !r_hir_vector_push(context, children, forget)) {
            return false;
        }
    }
    if (!r_grow_array(context,
                      (void **)&clone->pack_pieces,
                      &clone->pack_piece_capacity,
                      sizeof(*clone->pack_pieces),
                      clone->pack_piece_count + 1U)) {
        return false;
    }
    clone->pack_pieces[clone->pack_piece_count++] = pieces;
    return true;
}

/* R-TYPE-0053 (L18.4): a partition, the pieces an arguments node stands for, or the element
   initializers such a node stands for inside a pack built by a spread; `expanded` is false for
   any other child. */
static bool r_generic_clone_pack_child(RGenericClone *clone,
                                       RHirNodeId source_id,
                                       RHirVector *children,
                                       bool *expanded) {
    RFrontendContext *context = clone->context;
    const RHirNode source = context->hir_nodes[source_id - 1U];
    RHirNode arguments = source;
    bool field_initializers = false;
    *expanded = false;
    if (source.kind == R_HIR_PACK_PARTITION) {
        *expanded = true;
        return r_generic_clone_partition(clone, &source, children);
    }
    if ((source.kind == R_HIR_DROP) && (source.operation != R_TOKEN_KW_VARIANT)) {
        /* The cleanup of a staged spread object after a failed start drops its pieces, which
           hold its whole representation, in reverse order. */
        const RGenericPackPieces *found = r_generic_clone_find_pieces(clone, source.symbol);
        if (found == NULL) {
            return true;
        }
        const RGenericPackPieces pieces = *found;
        RTypeId void_type;
        *expanded = true;
        if (!r_semantic_type_from_token(context, R_TOKEN_KW_VOID, &void_type)) {
            return false;
        }
        for (uint32_t index = pieces.count; index != 0U; --index) {
            const RSymbolId piece = pieces.first + index - 1U;
            const RTypeId type = context->semantic_symbols[piece - 1U].type;
            RHirNodeId place;
            RHirNodeId drop;
            if (r_semantic_type_is_copy(context, type)) {
                continue;
            }
            if (!r_generic_clone_append(context,
                                        R_HIR_PLACE,
                                        source.span,
                                        type,
                                        piece,
                                        R_TOKEN_INVALID,
                                        true,
                                        0U,
                                        &place) ||
                !r_generic_clone_append(context,
                                        R_HIR_DROP,
                                        source.span,
                                        void_type,
                                        piece,
                                        source.operation,
                                        false,
                                        place,
                                        &drop) ||
                !r_hir_vector_push(context, children, drop)) {
                return false;
            }
            context->hir_nodes[drop - 1U].integer_value = source.integer_value;
        }
        return true;
    }
    if ((source.kind == R_HIR_FIELD_INIT) && (source.child_count == 1U) &&
        (context->hir_nodes[context->hir_children[source.first_child] - 1U].kind ==
         R_HIR_PACK_ARGS)) {
        arguments = context->hir_nodes[context->hir_children[source.first_child] - 1U];
        field_initializers = true;
    } else if (source.kind != R_HIR_PACK_ARGS) {
        return true;
    }
    *expanded = true;
    const RGenericPackPieces *found = r_generic_clone_pieces(clone, arguments.symbol);
    if (found == NULL) {
        return false;
    }
    const RGenericPackPieces pieces = *found;
    for (uint32_t index = (uint32_t)arguments.integer_value; index < pieces.count; ++index) {
        const RSymbolId piece = pieces.first + index;
        RHirNodeId transfer;
        if (!r_generic_clone_piece_transfer(context, piece, arguments.span, &transfer)) {
            return false;
        }
        if (field_initializers) {
            RHirNodeId field;
            if (!r_generic_clone_append(context,
                                        R_HIR_FIELD_INIT,
                                        source.span,
                                        context->semantic_symbols[piece - 1U].type,
                                        R_SYMBOL_ID_INVALID,
                                        R_TOKEN_EQUAL,
                                        false,
                                        transfer,
                                        &field)) {
                return false;
            }
            const RHirNode moved = context->hir_nodes[transfer - 1U];
            context->hir_nodes[field - 1U].borrow_origin = moved.borrow_origin;
            context->hir_nodes[field - 1U].borrow_origin_set = moved.borrow_origin_set;
            context->hir_nodes[field - 1U].borrow_origin_multiple = moved.borrow_origin_multiple;
            context->hir_nodes[field - 1U].borrow_referent = moved.borrow_referent;
            transfer = field;
        }
        if (!r_hir_vector_push(context, children, transfer)) {
            return false;
        }
    }
    return true;
}

/* The callee that a call in a generic definition reaches in one instantiation: the instance of a
 * generic function for the substituted arguments, or the implementation that a trait prototype
 * binds to for the closed Self type (R-TYPE-0042), which `self_type` then names. A call of a
 * non-generic function keeps its callee. */
static bool r_generic_close_callee(RFrontendContext *context,
                                   RSymbolId callee,
                                   uint32_t schema,
                                   uint32_t arguments,
                                   RSymbolId *closed,
                                   RTypeId *self_type) {
    const RSemanticSymbol target = context->semantic_symbols[callee - 1U];
    RTypeVector substituted = {0};
    uint32_t index;
    bool applied = true;
    *closed = callee;
    *self_type = R_TYPE_ID_INVALID;
    if (target.generic_origin == 0U)
        return true;
    for (index = 0U; index < context->generic_schemas[target.generic_schema - 1U].parameter_count;
         ++index) {
        RTypeId argument;
        if (!r_generic_substitute_type(
                context,
                context->generic_arguments[target.first_generic_argument + index],
                schema,
                arguments,
                &argument) ||
            !r_type_vector_push(context, &substituted, argument)) {
            applied = false;
            break;
        }
    }
    if (applied && context->semantic_symbols[target.generic_origin - 1U].is_trait_prototype &&
        (substituted.count != 0U) &&
        r_generic_bind_trait_call(
            context, target.generic_origin, substituted.items[0], closed, &substituted)) {
        *self_type = substituted.items[0];
    } else if (applied) {
        applied = r_generic_apply_function(context, target.generic_origin, &substituted, closed);
    }
    r_context_free(context, substituted.items);
    return applied;
}

static bool r_generic_clone_borrow_state(RGenericClone *clone,
                                         uint32_t source_id,
                                         uint32_t depth,
                                         uint32_t *result) {
    RFrontendContext *context = clone->context;
    RProjectedBorrowStateEntryVector entries = {0};
    RBodyContext body = {0};
    body.frontend = context;
    *result = 0U;
    if (source_id == 0U)
        return true;
    const RProjectedBorrowState state = context->projected_borrow_states[source_id - 1U];
    bool success = false;
    for (uint32_t index = 0U; index < state.count; ++index) {
        RProjectedBorrowStateEntry entry =
            context->projected_borrow_state_entries[state.first + index];
        if (!r_generic_clone_node(clone, entry.destination, depth + 1U, &entry.destination) ||
            (entry.source != 0U &&
             !r_generic_clone_node(clone, entry.source, depth + 1U, &entry.source)))
            goto cleanup;
        if (entry.destination == 0U)
            continue;
        entry.component_count = r_semantic_type_view_component_count(
            context, context->hir_nodes[entry.destination - 1U].type, NULL);
        if (entry.component_count == 0U)
            continue;
        if (entry.rebase_origin != 0U && entry.rebase_origin <= clone->symbol_count &&
            clone->symbols[entry.rebase_origin - 1U] != 0U)
            entry.rebase_origin = clone->symbols[entry.rebase_origin - 1U];
        if (!r_body_projected_borrow_entry_push(&body, &entries, entry))
            goto cleanup;
    }
    success = r_body_append_projected_borrow_state(&body, &entries, result);
cleanup:
    r_context_free(context, entries.items);
    return success;
}

static bool
r_generic_loop_declared(const RFrontendContext *, RHirNodeId, RSymbolId, size_t, size_t, uint8_t *);

/* R-STMT-0023 (L44): marks every symbol a subtree names, whichever branch of a constant condition
   names it. */
static bool r_generic_mark_all_symbols(
    RGenericClone *clone, RHirNodeId id, uint8_t *symbols, uint8_t *seen, uint32_t depth) {
    RFrontendContext *context = clone->context;
    if ((id == 0U) || (id > clone->node_count) || (depth > r_frontend_tree_depth_limit(context))) {
        return false;
    }
    if (seen[id - 1U] != 0U) {
        return true;
    }
    seen[id - 1U] = 1U;
    const RHirNode node = context->hir_nodes[id - 1U];
    if ((node.symbol != 0U) && (node.symbol <= clone->symbol_count)) {
        symbols[node.symbol - 1U] = 1U;
    }
    if ((node.deref_local != 0U) && (node.deref_local <= clone->symbol_count)) {
        symbols[node.deref_local - 1U] = 1U;
    }
    for (uint32_t index = 0U; index < node.child_count; ++index) {
        if (!r_generic_mark_all_symbols(clone,
                                        context->hir_children[node.first_child + index],
                                        symbols,
                                        seen,
                                        depth + 1U)) {
            return false;
        }
    }
    return true;
}

/* Select branch-local storage along with its HIR. Inactive locals must not instantiate
 * layouts, destructors or runtime dependencies. Parameters retain the declared signature. */
static bool r_generic_mark_selected_symbols(
    RGenericClone *clone, RHirNodeId id, uint8_t *symbols, uint8_t *seen, uint32_t depth) {
    RFrontendContext *context = clone->context;
    RHirNode node;
    uint32_t index;
    if (id == 0U || id > clone->node_count || depth > r_frontend_tree_depth_limit(context))
        return false;
    if (seen[id - 1U] != 0U)
        return true;
    seen[id - 1U] = 1U;
    node = context->hir_nodes[id - 1U];
    if (node.symbol != 0U && node.symbol <= clone->symbol_count)
        symbols[node.symbol - 1U] = 1U;
    if (node.deref_local != 0U && node.deref_local <= clone->symbol_count)
        symbols[node.deref_local - 1U] = 1U;
    if (node.kind == R_HIR_CONSTEXPR_LOOP) {
        /* R-STMT-0023 (L44): every repetition copies the symbols its block declares itself. */
        uint8_t *declared = r_context_allocate(context, clone->symbol_count + 1U);
        const RSymbolId origin = clone->origin;
        bool marked = declared != NULL;
        /* The bounds select as usual; the block, whose constant conditions only the
           repetitions decide, keeps every symbol it names. */
        for (index = 0U; marked && (index < node.child_count); ++index) {
            const RHirNodeId child = context->hir_children[node.first_child + index];
            if (index + 1U < node.child_count) {
                marked = r_generic_mark_selected_symbols(clone, child, symbols, seen, depth + 1U);
            } else {
                marked = r_generic_mark_all_symbols(clone, child, symbols, seen, depth + 1U);
            }
        }
        if (marked && (node.child_count == 3U)) {
            marked = r_generic_loop_declared(context,
                                             context->hir_children[node.first_child + 2U],
                                             origin,
                                             clone->node_count,
                                             clone->symbol_count,
                                             declared);
            for (size_t slot = 0U; marked && (slot < clone->symbol_count); ++slot) {
                if (declared[slot] != 0U) {
                    symbols[slot] = 0U;
                }
            }
        }
        r_context_free(context, declared);
        return marked;
    }
    if (node.kind == R_HIR_STATIC_IF) {
        int truth;
        uint32_t branch;
        if (!r_static_evaluate(context,
                               (uint32_t)node.integer_value,
                               clone->schema,
                               clone->arguments,
                               0U,
                               &truth) ||
            truth < 0)
            return false;
        branch = truth == 1 ? 1U : 2U;
        return branch >= node.child_count ||
               r_generic_mark_selected_symbols(clone,
                                               context->hir_children[node.first_child + branch],
                                               symbols,
                                               seen,
                                               depth + 1U);
    }
    for (index = 0U; index < node.child_count; ++index) {
        if (!r_generic_mark_selected_symbols(
                clone, context->hir_children[node.first_child + index], symbols, seen, depth + 1U))
            return false;
    }
    return true;
}

/* R-STMT-0023 (L44): marks the nodes of a subtree. */
static bool r_generic_mark_subtree(const RFrontendContext *context,
                                   RHirNodeId id,
                                   uint8_t *nodes,
                                   size_t node_count,
                                   uint32_t depth) {
    if ((id == 0U) || ((size_t)id > node_count) || (depth > r_frontend_tree_depth_limit(context))) {
        return true;
    }
    if (nodes[id - 1U] != 0U) {
        return true;
    }
    nodes[id - 1U] = 1U;
    const RHirNode node = context->hir_nodes[id - 1U];
    for (uint32_t index = 0U; index < node.child_count; ++index) {
        if (!r_generic_mark_subtree(context,
                                    context->hir_children[node.first_child + index],
                                    nodes,
                                    node_count,
                                    depth + 1U)) {
            return false;
        }
    }
    return true;
}

/* R-STMT-0023 (L44): the symbols of `origin` that the block of a translation-time loop declares,
   those whose declaration node lies in it; each repetition gets copies of its own. */
static bool r_generic_loop_declared(const RFrontendContext *context,
                                    RHirNodeId block,
                                    RSymbolId origin,
                                    size_t node_count,
                                    size_t symbol_count,
                                    uint8_t *declared) {
    uint8_t *nodes = r_context_allocate((RFrontendContext *)context, node_count + 1U);
    if (nodes == NULL) {
        return false;
    }
    (void)memset(nodes, 0, node_count + 1U);
    const bool marked = r_generic_mark_subtree(context, block, nodes, node_count, 0U);
    /* A symbol without a node of its own, such as the group of a task_scope, is declared in the
       block when its scope begins there (defect L44-12). */
    const RSourceSpan span = context->hir_nodes[block - 1U].span;
    if (marked) {
        for (size_t index = 0U; index < symbol_count; ++index) {
            const RSemanticSymbol *symbol = &context->semantic_symbols[index];
            const bool by_node = (symbol->hir_node != 0U) &&
                                 ((size_t)symbol->hir_node <= node_count) &&
                                 (nodes[symbol->hir_node - 1U] != 0U);
            const bool by_scope = (symbol->name_span.source == span.source) &&
                                  (symbol->scope_start > span.start) &&
                                  (symbol->scope_start < span.end);
            declared[index] = (symbol->parent == origin) && (by_node || by_scope) ? 1U : 0U;
        }
    }
    r_context_free((RFrontendContext *)context, nodes);
    return marked;
}

/* R-STMT-0023 (L44): binds the constant of a translation-time loop for one repetition. */
static bool r_constexpr_bind(RFrontendContext *context, RTypeId parameter, uint64_t value) {
    if (!r_grow_array(context,
                      (void **)&context->constexpr_binding_parameters,
                      &context->constexpr_binding_parameter_capacity,
                      sizeof(*context->constexpr_binding_parameters),
                      context->constexpr_binding_count + 1U) ||
        !r_grow_array(context,
                      (void **)&context->constexpr_binding_values,
                      &context->constexpr_binding_value_capacity,
                      sizeof(*context->constexpr_binding_values),
                      context->constexpr_binding_count + 1U)) {
        return false;
    }
    context->constexpr_binding_parameters[context->constexpr_binding_count] = parameter;
    context->constexpr_binding_values[context->constexpr_binding_count] = value;
    context->constexpr_binding_count += 1U;
    return true;
}

/* R-STMT-0023 (L44): an instance unrolls a translation-time loop of its definition. The bounds are
   constants once cloned; each repetition binds the loop parameter to its value, gives the symbols
   the block declares copies of their own and clones the block anew. */
static bool r_generic_clone_constexpr_loop(RGenericClone *clone,
                                           const RHirNode *source,
                                           uint32_t depth,
                                           RHirNodeId *result) {
    RFrontendContext *context = clone->context;
    RHirNodeId low_id;
    RHirNodeId high_id;
    RHirVector copies = {0};
    uint8_t *declared = NULL;
    uint8_t *selected = NULL;
    uint8_t *seen = NULL;
    bool success = false;
    const RHirNodeId block = context->hir_children[source->first_child + 2U];
    /* Cloning the bounds may intern types and move semantic_types, so the parameter is read
       first (defect L44-9). */
    const RSemanticType *parameter_type = r_semantic_type(context, source->auxiliary_type);
    const uint32_t parameter_base = parameter_type == NULL ? 0U : parameter_type->base;

    if ((source->child_count != 3U) || (parameter_base == 0U) ||
        ((size_t)parameter_base > context->generic_parameter_count) ||
        !r_generic_clone_node(
            clone, context->hir_children[source->first_child], depth + 1U, &low_id) ||
        !r_generic_clone_node(
            clone, context->hir_children[source->first_child + 1U], depth + 1U, &high_id)) {
        return false;
    }
    /* The bounds of this instance: literals, or expressions over the substituted parameters
       that translation-time evaluation folds, such as `core::field_count::<T>() - 1usize`. */
    uint64_t bounds[2] = {0U, 0U};
    const RHirNodeId bound_ids[2] = {low_id, high_id};
    for (uint32_t which = 0U; which < 2U; ++which) {
        const RHirNode bound = context->hir_nodes[bound_ids[which] - 1U];
        bool folded = bound.kind == R_HIR_LITERAL;
        bounds[which] = bound.integer_value;
        if (!folded &&
            !r_consteval_required_scalar(context, bound_ids[which], &bounds[which], &folded)) {
            return false;
        }
        if (!folded) {
            if (clone->origin_poisoned) {
                return true;
            }
            return r_semantic_add_diagnostic(context,
                                             "R-DIAG-CONST-002",
                                             "R-STMT-0023",
                                             "a bound of a translation-time loop is a constant in "
                                             "every instantiation",
                                             source->span);
        }
    }
    const RTypeId loop_type = context->generic_parameters[parameter_base - 1U].constant_type;
    bool is_signed = false;
    uint32_t width = 64U;
    (void)r_semantic_integer_properties(
        r_semantic_value_kind(context, loop_type), &is_signed, &width);
    const uint64_t mask = width >= 64U ? UINT64_MAX : ((UINT64_C(1) << width) - 1U);
    uint64_t low = bounds[0];
    uint64_t high = bounds[1];
    if (is_signed && (width < 64U)) {
        const uint64_t sign = UINT64_C(1) << (width - 1U);
        low = ((low & mask) ^ sign) - sign;
        high = ((high & mask) ^ sign) - sign;
    }
    uint64_t count = 0U;
    if (is_signed ? ((int64_t)low < (int64_t)high) : (low < high)) {
        count = high - low;
    }
    if (count > (uint64_t)R_CONSTEXPR_LOOP_LIMIT) {
        return clone->origin_poisoned ||
               r_semantic_add_diagnostic(context,
                                         "R-DIAG-LIMIT-001",
                                         "R-STMT-0023",
                                         "a translation-time loop repeats its block at most 1024 "
                                         "times",
                                         source->span);
    }
    const RSymbolId origin = clone->origin;
    declared = r_context_allocate(context, clone->symbol_count + 1U);
    if ((declared == NULL) ||
        !r_generic_loop_declared(
            context, block, origin, clone->node_count, clone->symbol_count, declared)) {
        goto cleanup;
    }
    selected = r_context_allocate(context, clone->symbol_count + 1U);
    seen = r_context_allocate(context, clone->node_count + 1U);
    if ((selected == NULL) || (seen == NULL)) {
        goto cleanup;
    }
    for (uint64_t step = 0U; step < count; ++step) {
        RHirNodeId copy;
        uint8_t *nodes = NULL;
        if (!r_constexpr_bind(context, source->auxiliary_type, (low + step) & mask)) {
            goto cleanup;
        }
        /* The symbols this repetition uses: with its constant bound, the constant conditions of
           the block decide, so a branch it does not take copies nothing (defect L44-13). */
        (void)memset(selected, 0, clone->symbol_count + 1U);
        (void)memset(seen, 0, clone->node_count + 1U);
        if (!r_generic_mark_selected_symbols(clone, block, selected, seen, depth + 1U)) {
            context->constexpr_binding_count -= 1U;
            goto cleanup;
        }
        for (size_t index = 0U; index < clone->symbol_count; ++index) {
            if ((declared[index] == 0U) || (selected[index] == 0U)) {
                continue;
            }
            RSemanticSymbol symbol = context->semantic_symbols[index];
            symbol.parent = context->generic_current_function;
            symbol.hir_node = 0U;
            if (!r_generic_substitute_type(
                    context, symbol.type, clone->schema, clone->arguments, &symbol.type) ||
                !r_semantic_append_symbol(context, symbol, &clone->symbols[index])) {
                context->constexpr_binding_count -= 1U;
                goto cleanup;
            }
        }
        /* The links between symbols follow the copies, as for a whole instance. */
        for (size_t index = 0U; index < clone->symbol_count; ++index) {
            if ((declared[index] == 0U) || (selected[index] == 0U)) {
                continue;
            }
            RSemanticSymbol *copied = &context->semantic_symbols[clone->symbols[index] - 1U];
            if ((copied->task_scope != 0U) && (copied->task_scope <= clone->symbol_count)) {
                copied->task_scope = clone->symbols[copied->task_scope - 1U];
            }
            if ((copied->out_parameter != 0U) && (copied->out_parameter <= clone->symbol_count)) {
                copied->out_parameter = clone->symbols[copied->out_parameter - 1U];
            }
            if ((copied->parameter_content != 0U) &&
                (copied->parameter_content <= clone->symbol_count)) {
                copied->parameter_content = clone->symbols[copied->parameter_content - 1U];
            }
            if ((copied->content_parameter != 0U) &&
                (copied->content_parameter <= clone->symbol_count)) {
                copied->content_parameter = clone->symbols[copied->content_parameter - 1U];
            }
            if (!r_semantic_remap_borrow_origins(context,
                                                 clone->symbols,
                                                 clone->symbol_count,
                                                 &copied->borrow_origin,
                                                 &copied->borrow_origin_set,
                                                 &copied->borrow_origin_multiple)) {
                context->constexpr_binding_count -= 1U;
                goto cleanup;
            }
        }
        nodes = r_context_allocate(context, clone->node_count + 1U);
        if (nodes == NULL) {
            context->constexpr_binding_count -= 1U;
            goto cleanup;
        }
        (void)memset(nodes, 0, clone->node_count + 1U);
        const bool marked = r_generic_mark_subtree(context, block, nodes, clone->node_count, 0U);
        for (size_t index = 0U; marked && (index < clone->node_count); ++index) {
            if (nodes[index] != 0U) {
                clone->nodes[index] = R_HIR_NODE_ID_INVALID;
            }
        }
        r_context_free(context, nodes);
        const bool cloned = marked && r_generic_clone_node(clone, block, depth + 1U, &copy);
        context->constexpr_binding_count -= 1U;
        if (!cloned ||
            ((copy != R_HIR_NODE_ID_INVALID) && !r_hir_vector_push(context, &copies, copy))) {
            goto cleanup;
        }
        /* The projected borrow states of this repetition's copies, while the symbols still map
           to them. */
        for (size_t index = 0U; index < clone->symbol_count; ++index) {
            uint32_t state;
            if ((declared[index] == 0U) || (selected[index] == 0U)) {
                continue;
            }
            if (!r_generic_clone_borrow_state(
                    clone, context->semantic_symbols[index].projected_borrow_state, 0U, &state)) {
                goto cleanup;
            }
            context->semantic_symbols[clone->symbols[index] - 1U].projected_borrow_state = state;
        }
    }
    {
        RHirNode node;
        RTypeId void_type;
        (void)memset(&node, 0, sizeof(node));
        if (!r_semantic_type_from_token(context, R_TOKEN_KW_VOID, &void_type)) {
            goto cleanup;
        }
        node.kind = R_HIR_BLOCK;
        node.span = source->span;
        node.type = void_type;
        if (!r_hir_append_node(context, node, result) ||
            !r_hir_commit_children(context, *result, &copies)) {
            goto cleanup;
        }
    }
    success = true;
cleanup:
    r_context_free(context, declared);
    r_context_free(context, selected);
    r_context_free(context, seen);
    r_context_free(context, copies.items);
    return success;
}

/* R-REFL-0006 (L44): an instance sets the member of a field place that `core::field` formed in a
   dependent struct, from the synthetic field parameter of the definition. */
static bool r_generic_close_field_place(RGenericClone *clone, RTypeId parameter, RHirNode *node) {
    RFrontendContext *context = clone->context;
    const RSemanticType *type = r_semantic_type(context, parameter);
    if ((type == NULL) || (type->kind != R_SEMANTIC_TYPE_PARAMETER) || (type->base == 0U) ||
        ((size_t)type->base > context->generic_parameter_count)) {
        return true;
    }
    const RGenericParameter field = context->generic_parameters[type->base - 1U];
    if (field.field_base == R_TYPE_ID_INVALID) {
        return true;
    }
    uint64_t number = field.field_index_value;
    RTypeId base;
    if (((field.field_index != R_TYPE_ID_INVALID) &&
         !r_constexpr_binding(context, field.field_index, &number)) ||
        !r_generic_substitute_type(
            context, field.field_base, clone->schema, clone->arguments, &base)) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    const RSemanticAggregate *aggregate = r_semantic_aggregate_for_type(context, base);
    if ((aggregate == NULL) || (aggregate->kind != R_SEMANTIC_AGGREGATE_STRUCT) ||
        (number >= (uint64_t)aggregate->field_count)) {
        return clone->origin_poisoned ||
               r_semantic_add_diagnostic(context,
                                         "R-DIAG-TYPE-001",
                                         "R-REFL-0006",
                                         "core::field takes a struct or tuple with this field in "
                                         "every instantiation",
                                         node->span);
    }
    const uint32_t member = aggregate->first_field + (uint32_t)number + 1U;
    const RSemanticField *selected = &context->semantic_fields[member - 1U];
    const RSourceId module = context->semantic_symbols[clone->origin - 1U].module_source;
    if (selected->is_protected && (aggregate->module_source != module) && !clone->origin_poisoned &&
        !r_semantic_add_diagnostic(context,
                                   "R-DIAG-NAME-001",
                                   "R-NAME-0003",
                                   "protected struct field is not visible",
                                   node->span)) {
        return false;
    }
    node->aggregate_member = member;
    node->type = selected->type;
    node->auxiliary_type = R_TYPE_ID_INVALID;
    return true;
}

/* One concrete error a clause reached by an abstract error set catches after substitution: the
   conditional exit of that clause, its try statement and the distance to the clause's error. */
typedef struct RGenericCaughtExit {
    RTypeId error;
    uint32_t exit;
    uint32_t group;
    uint32_t distance;
} RGenericCaughtExit;

static bool r_generic_clone_node(RGenericClone *clone,
                                 RHirNodeId source_id,
                                 uint32_t depth,
                                 RHirNodeId *result) {
    RFrontendContext *context = clone->context;
    RHirNode source, node;
    RHirVector children = {0};
    uint32_t index;
    bool success = false;
    *result = R_HIR_NODE_ID_INVALID;
    if (source_id == 0U || source_id > clone->node_count ||
        depth > r_frontend_tree_depth_limit(context)) {
        return false;
    }
    if (clone->nodes[source_id - 1U] != 0U) {
        *result = clone->nodes[source_id - 1U];
        return true;
    }
    source = context->hir_nodes[source_id - 1U];
    if (source.kind == R_HIR_STATIC_IF) {
        int truth;
        uint32_t branch;
        if (!r_static_evaluate(context,
                               (uint32_t)source.integer_value,
                               clone->schema,
                               clone->arguments,
                               0U,
                               &truth) ||
            truth < 0)
            return false;
        branch = truth == 1 ? 1U : 2U;
        if (branch >= source.child_count)
            return true;
        if (!r_generic_clone_node(
                clone, context->hir_children[source.first_child + branch], depth + 1U, result))
            return false;
        clone->nodes[source_id - 1U] = *result;
        return true;
    }
    if (source.kind == R_HIR_CONSTEXPR_LOOP) {
        if (!r_generic_clone_constexpr_loop(clone, &source, depth, result)) {
            return false;
        }
        clone->nodes[source_id - 1U] = *result;
        return true;
    }
    if (source.kind == R_HIR_PACK_ELEMENT) {
        /* R-TYPE-0053 (L18.4): one piece of a partitioned spread object. */
        const RGenericPackPieces *pieces = r_generic_clone_pieces(clone, source.symbol);
        if ((pieces == NULL) || (source.integer_value >= pieces->count)) {
            context->resource_status = R_FRONTEND_INTERNAL_ERROR;
            return false;
        }
        if (!r_generic_clone_piece_transfer(
                context, pieces->first + (uint32_t)source.integer_value, source.span, result)) {
            return false;
        }
        clone->nodes[source_id - 1U] = *result;
        return true;
    }
    node = source;
    if (!r_generic_substitute_type(
            context, source.type, clone->schema, clone->arguments, &node.type) ||
        !r_generic_substitute_type(context,
                                   source.auxiliary_type,
                                   clone->schema,
                                   clone->arguments,
                                   &node.auxiliary_type) ||
        !r_generic_substitute_type(
            context, source.runtime_type, clone->schema, clone->arguments, &node.runtime_type)) {
        return false;
    }
    if (node.kind == R_HIR_GENERIC_CONSTANT &&
        !r_generic_type_is_dependent(context, node.auxiliary_type, 0U)) {
        if (!r_generic_constant_evaluate(context, node.auxiliary_type, &node.integer_value, 0U))
            return false;
        node.kind = R_HIR_LITERAL;
        node.operation = R_TOKEN_INVALID;
        node.auxiliary_type = 0U;
    }
    if ((node.kind == R_HIR_TASK_SCOPE_ENTER) && (node.aggregate_member != 0U)) {
        /* L25.2 (R-STMT-0017): the capacity of this instance's task group. */
        if (!r_consteval_group_capacity(context,
                                        (RHirNodeId)node.aggregate_member,
                                        clone->schema,
                                        clone->arguments,
                                        source.span,
                                        !clone->origin_poisoned,
                                        &node.integer_value))
            return false;
        node.aggregate_member = 0U;
    }
    if (node.kind == R_HIR_TYPE_QUERY) {
        uint64_t size, alignment;
        if (r_generic_layout(context, node.auxiliary_type, &size, &alignment, 0U)) {
            node.kind = R_HIR_LITERAL;
            node.integer_value = node.operation == R_TOKEN_KW_SIZEOF ? size : alignment;
            node.operation = R_TOKEN_INVALID;
            node.auxiliary_type = 0U;
        }
    }
    if (node.kind == R_HIR_STANDARD_CALL &&
        (node.standard_operation == R_STANDARD_CALL_CORE_HASH ||
         node.standard_operation == R_STANDARD_CALL_CORE_KEY_EQUAL)) {
        const RSemanticAggregate *aggregate =
            r_semantic_aggregate_for_type(context, node.runtime_type);
        RSymbolId hook = aggregate == NULL ? 0U
                                           : (node.standard_operation == R_STANDARD_CALL_CORE_HASH
                                                  ? aggregate->hash_function
                                                  : aggregate->equal_function);
        if (hook != 0U) {
            node.kind = R_HIR_CALL;
            node.symbol = hook;
            node.runtime_type = 0U;
            node.auxiliary_type = 0U;
            node.standard_operation = R_STANDARD_CALL_INVALID;
        }
    }
    if ((node.kind == R_HIR_STANDARD_CALL) &&
        ((node.standard_operation == R_STANDARD_CALL_CORE_FORMAT_RENDER) ||
         (node.standard_operation == R_STANDARD_CALL_CORE_FORMAT_APPEND)) &&
        !r_format_bind_type(context, node.runtime_type, 0U)) {
        /* R-TYPE-0046 (L32): the instance formats its own closed type. */
        return false;
    }
    if (!r_reflection_fold_clone(context, &node)) {
        return false;
    }
    if (node.kind == R_HIR_DROP && node.symbol != 0U && node.symbol <= clone->symbol_count) {
        RTypeId dropped;
        if (!r_generic_substitute_type(context,
                                       context->semantic_symbols[node.symbol - 1U].type,
                                       clone->schema,
                                       clone->arguments,
                                       &dropped)) {
            return false;
        }
        if (r_semantic_type_is_copy(context, dropped)) {
            return true;
        }
    }
    if (node.kind == R_HIR_MOVE && r_semantic_type_is_copy(context, node.type)) {
        node.kind = R_HIR_LOAD;
        node.operation = R_TOKEN_INVALID;
        node.is_move = false;
    }
    if (node.symbol != 0U && node.symbol <= clone->symbol_count) {
        if (clone->symbols[node.symbol - 1U] != 0U) {
            node.symbol = clone->symbols[node.symbol - 1U];
        } else {
            RTypeId self_type = R_TYPE_ID_INVALID;
            if (!r_generic_close_callee(context,
                                        node.symbol,
                                        clone->schema,
                                        clone->arguments,
                                        &node.symbol,
                                        &self_type))
                return false;
            /* Only a call carries the callee's effect carrier and receiver; a node that
               merely names the call's result, such as a dereference, keeps its own types. */
            if ((self_type != R_TYPE_ID_INVALID) &&
                ((node.kind == R_HIR_CALL) || (node.kind == R_HIR_ASYNC_START))) {
                node.auxiliary_type =
                    context->semantic_symbols[node.symbol - 1U].is_async
                        ? context->semantic_symbols[node.symbol - 1U].async_start_type
                        : context->semantic_symbols[node.symbol - 1U].effect_carrier_type;
                const RSemanticAggregate *item = r_semantic_aggregate_for_type(
                    context, r_semantic_representation_type(context, self_type));
                node.function_item_receiver =
                    item != NULL && item->function_item_target != R_SYMBOL_ID_INVALID;
            }
        }
    }
    if (node.task_scope != 0U && node.task_scope <= clone->symbol_count)
        node.task_scope = clone->symbols[node.task_scope - 1U];
    if ((node.kind == R_HIR_CALL || node.kind == R_HIR_ASYNC_START) && node.symbol != 0U) {
        const RSemanticSymbol *callee = &context->semantic_symbols[node.symbol - 1U];
        node.auxiliary_type =
            callee->is_async ? callee->async_start_type : callee->effect_carrier_type;
    }
    if (!r_semantic_remap_borrow_origins(context,
                                         clone->symbols,
                                         clone->symbol_count,
                                         &node.borrow_origin,
                                         &node.borrow_origin_set,
                                         &node.borrow_origin_multiple)) {
        return false;
    }
    if ((node.kind == R_HIR_CASE && node.is_move &&
         !r_semantic_type_contains_borrow(context, node.type)) ||
        ((node.kind == R_HIR_CALL || node.kind == R_HIR_RETURN) &&
         !r_semantic_type_is_direct_shared_borrow_output(context, node.type) &&
         !r_semantic_type_contains_borrow(context, node.type) &&
         !r_semantic_type_contains_slice(context, node.type) &&
         !r_generic_type_is_dependent(context, node.type, 0U))) {
        node.borrow_origin = R_SYMBOL_ID_INVALID;
        node.borrow_origin_set = UINT32_C(0);
        node.borrow_origin_multiple = false;
    }
    if ((node.kind == R_HIR_FIELD_PLACE) && (node.aggregate_member == 0U) &&
        (source.auxiliary_type != R_TYPE_ID_INVALID) &&
        !r_generic_close_field_place(clone, source.auxiliary_type, &node)) {
        return false;
    }
    if ((node.kind == R_HIR_FIELD_INIT || node.kind == R_HIR_FIELD_PLACE) &&
        node.aggregate_member != 0U) {
        RSemanticField field = context->semantic_fields[node.aggregate_member - 1U];
        RTypeId aggregate_type;
        const RSemanticAggregate *aggregate;
        if (!r_generic_substitute_type(context,
                                       context->semantic_aggregates[field.aggregate - 1U].type,
                                       clone->schema,
                                       clone->arguments,
                                       &aggregate_type)) {
            return false;
        }
        aggregate = r_semantic_aggregate_for_type(context, aggregate_type);
        if (aggregate == NULL) {
            /* A hidden standard schema struct exists only once a member of that instance is
               named; the substituted instance is prepared here on first use. */
            const RSemanticType *substituted = r_semantic_type(context, aggregate_type);
            RTypeId prepared = R_TYPE_ID_INVALID;
            if ((substituted != NULL) && (substituted->kind == R_SEMANTIC_TYPE_STANDARD)) {
                bool prepared_ok = true;
                if (r_semantic_standard_type_name_equal(
                        context, substituted, "std.dict::entry_ref")) {
                    prepared_ok = r_semantic_prepare_dict_entry_ref_schema(
                        context, aggregate_type, &prepared);
                } else if (r_semantic_standard_type_name_equal(
                               context, substituted, "std.alloc::new_error")) {
                    prepared_ok = r_semantic_prepare_alloc_new_error_schema(
                        context, aggregate_type, &prepared);
                } else if (r_semantic_standard_type_name_equal(
                               context, substituted, "std.c::target_info")) {
                    prepared_ok = r_semantic_prepare_c_target_info_schema(context, &prepared);
                } else {
                    prepared_ok = r_semantic_prepare_net_schema(context, aggregate_type) &&
                                  r_semantic_prepare_json_schema(context, aggregate_type);
                }
                if (!prepared_ok) {
                    return false;
                }
            }
            aggregate = r_semantic_aggregate_for_type(context, aggregate_type);
        }
        if (aggregate == NULL || field.layout_index >= aggregate->field_count) {
            return false;
        }
        node.aggregate_member = aggregate->first_field + field.layout_index + 1U;
    }
    if (node.kind == R_HIR_VARIANT && node.aggregate_member != 0U) {
        const RSemanticAggregate *aggregate = r_semantic_tagged_enum(context, node.type);
        if (aggregate != NULL) {
            node.aggregate_member = aggregate->first_variant + (uint32_t)node.integer_value + 1U;
        }
    }
    node.first_child = 0U;
    if (node.out_destination != 0U && node.out_destination <= clone->symbol_count)
        node.out_destination = clone->symbols[node.out_destination - 1U];
    if (node.deref_local != 0U && node.deref_local <= clone->symbol_count)
        node.deref_local = clone->symbols[node.deref_local - 1U];
    if (node.out_place != 0U &&
        !r_generic_clone_node(clone, node.out_place, depth + 1U, &node.out_place))
        goto cleanup;
    node.child_count = 0U;
    node.first_effect_exit = 0U;
    node.effect_exit_count = 0U;
    /* L39: the panic drops of an instance name its own symbols; nodes that shared a block in
       the definition share its clone. */
    if (node.panic_cleanup != 0U &&
        !r_generic_clone_node(clone, node.panic_cleanup, depth + 1U, &node.panic_cleanup))
        goto cleanup;
    if (!r_hir_append_node(context, node, result)) {
        return false;
    }
    clone->nodes[source_id - 1U] = *result;
    if (source.projected_borrow_state != 0U) {
        uint32_t state;
        if (!r_generic_clone_borrow_state(clone, source.projected_borrow_state, depth, &state))
            goto cleanup;
        context->hir_nodes[*result - 1U].projected_borrow_state = state;
    }
    for (index = 0U; index < source.child_count; ++index) {
        RHirNodeId child;
        bool expanded = false;
        if (source.kind == R_HIR_VALUE_SCOPE && index == source.integer_value) {
            context->hir_nodes[*result - 1U].integer_value = children.count;
        }
        if (!r_generic_clone_pack_child(
                clone, context->hir_children[source.first_child + index], &children, &expanded)) {
            goto cleanup;
        }
        if (expanded) {
            continue;
        }
        if (!r_generic_clone_node(
                clone, context->hir_children[source.first_child + index], depth + 1U, &child) ||
            (child != 0U && !r_hir_vector_push(context, &children, child))) {
            goto cleanup;
        }
    }
    if (!r_hir_commit_children(context, *result, &children)) {
        goto cleanup;
    }
    if ((node.kind == R_HIR_AGGREGATE_INIT) && (r_generic_pack(context, source.type) != NULL)) {
        /* R-TYPE-0053 (L18.4): a pack built by a spread became a tuple; its initializers name
           the elements in order. */
        const RSemanticAggregate *tuple = r_tuple_aggregate(context, node.type);
        if ((tuple == NULL) || (tuple->field_count != children.count)) {
            context->resource_status = R_FRONTEND_INTERNAL_ERROR;
            goto cleanup;
        }
        for (index = 0U; index < children.count; ++index) {
            context->hir_nodes[children.items[index] - 1U].aggregate_member =
                tuple->first_field + index + 1U;
        }
    }
    /* R-NAME-0008, R-META-0003: a static initializer over the definition's parameters is a
       constant of this instance; its closed translation-time calls are replaced by values. */
    if (node.kind == R_HIR_LOCAL && children.count == 1U && node.symbol != 0U &&
        (context->semantic_symbols[node.symbol - 1U].is_static ||
         context->semantic_symbols[node.symbol - 1U].is_thread_local) &&
        !r_generic_constant_initializer(context, children.items[0], 0U) &&
        !clone->origin_poisoned) {
        const size_t diagnostics = context->diagnostic_count;
        if (!r_consteval_fold_initializer(context, children.items[0]) ||
            (!r_generic_constant_initializer(context, children.items[0], 0U) &&
             context->diagnostic_count == diagnostics &&
             !r_semantic_add_diagnostic(context,
                                        "R-DIAG-INIT-001",
                                        "R-NAME-0008",
                                        "static object requires a constant initializer",
                                        node.span)))
            goto cleanup;
    }
    if (node.kind == R_HIR_CALL && node.symbol != 0U &&
        context->semantic_symbols[node.symbol - 1U].json_operation != 0U) {
        const RSemanticSymbol helper = context->semantic_symbols[node.symbol - 1U];
        RTypeId closed_type;
        RExpressionResult call = {0};
        RBodyContext body = {0};
        body.frontend = context;
        body.source = helper.module_source;
        if (!r_generic_substitute_type(
                context, helper.json_type, clone->schema, clone->arguments, &closed_type) ||
            !r_json_call(&body,
                         node.span,
                         helper.json_operation,
                         closed_type,
                         node.type,
                         &children,
                         helper.throws_type != 0U,
                         true,
                         &call))
            goto cleanup;
        context->hir_nodes[*result - 1U].symbol = call.symbol;
        context->hir_nodes[*result - 1U].auxiliary_type =
            context->semantic_symbols[call.symbol - 1U].effect_carrier_type;
        /* R-SLIB-JSON-0002: the schema of a generic body is built for the closed type. */
        if (helper.json_operation == R_JSON_OPERATION_SCHEMA &&
            !r_generic_type_is_dependent(context, closed_type, 0U) &&
            !r_json_schema_attach(&body, node.span, closed_type, call.symbol))
            goto cleanup;
    }
    if (children.count != 0U &&
        (node.kind == R_HIR_LENGTH || node.kind == R_HIR_INDEX_PLACE || node.kind == R_HIR_SLICE)) {
        const RSemanticType *base = r_semantic_type(
            context,
            r_semantic_value_type(context, context->hir_nodes[children.items[0] - 1U].type));
        if (base != NULL && base->kind == R_SEMANTIC_TYPE_FIXED_ARRAY) {
            context->hir_nodes[*result - 1U].integer_value = base->length;
            if (children.count >= 2U && (node.kind == R_HIR_INDEX_PLACE ||
                                         (node.kind == R_HIR_SLICE && children.count == 3U))) {
                RBodyContext body = {0};
                RBodyConstantValue bound;
                body.frontend = context;
                if (r_body_constant_scalar(&body, children.items[children.count - 1U], &bound) &&
                    bound.valid &&
                    (node.kind == R_HIR_INDEX_PLACE ? bound.bits >= base->length
                                                    : bound.bits > base->length) &&
                    !r_semantic_add_diagnostic(
                        context,
                        "R-DIAG-BOUNDS-001",
                        "R-EXPR-0021",
                        "index or range is outside the closed generic fixed-array bounds",
                        node.span)) {
                    goto cleanup;
                }
            }
        }
    }
    if (node.kind == R_HIR_ARRAY_INIT) {
        context->hir_nodes[*result - 1U].integer_value =
            r_semantic_type(context, node.type)->length;
    }
    /* The exits of a closed node follow its carrier, which lists the substituted errors in
       canonical order (R-TYPE-0049): they are collected, ordered and appended as one range. */
    {
        RHirEffectExit *exits = NULL;
        RGenericCaughtExit *caught = NULL;
        size_t exit_count = 0U, exit_capacity = 0U;
        size_t caught_count = 0U, caught_capacity = 0U;
        bool exits_ok = true;
        /* R-ERR-0003 (L21.2): a clause that an abstract set reaches receives each concrete error
           it catches, chosen as a catch selects: the innermost try, then the nearest ancestor. */
        for (index = 0U; exits_ok && index < source.effect_exit_count; ++index) {
            const RHirEffectExit effect =
                context->hir_effect_exits[source.first_effect_exit + index];
            RTypeId set, clause;
            if (effect.condition_set == 0U)
                continue;
            if (!r_generic_substitute_type(
                    context, effect.condition_set, clone->schema, clone->arguments, &set) ||
                !r_generic_substitute_type(
                    context, effect.error_type, clone->schema, clone->arguments, &clause)) {
                exits_ok = false;
                break;
            }
            for (uint32_t member = 0U; exits_ok && member < r_semantic_effect_count(context, set);
                 ++member) {
                const RTypeId error = r_semantic_effect_at(context, set, member);
                const uint32_t distance = r_semantic_error_catch_distance(context, clause, error);
                size_t entry = 0U;
                if (distance == UINT32_MAX)
                    continue;
                while (entry < caught_count && caught[entry].error != error)
                    ++entry;
                if (entry == caught_count) {
                    if (!r_grow_array(context,
                                      (void **)&caught,
                                      &caught_capacity,
                                      sizeof(*caught),
                                      caught_count + 1U)) {
                        exits_ok = false;
                        break;
                    }
                    caught[caught_count++] =
                        (RGenericCaughtExit){error, index, effect.catch_group, distance};
                } else if (effect.catch_group > caught[entry].group ||
                           (effect.catch_group == caught[entry].group &&
                            distance < caught[entry].distance)) {
                    caught[entry] =
                        (RGenericCaughtExit){error, index, effect.catch_group, distance};
                }
            }
        }
        for (index = 0U; exits_ok && index < source.effect_exit_count; ++index) {
            RHirEffectExit effect = context->hir_effect_exits[source.first_effect_exit + index];
            RHirNodeId block;
            if (effect.condition_set != 0U) {
                for (size_t entry = 0U; exits_ok && entry < caught_count; ++entry) {
                    bool duplicate = false;
                    if (caught[entry].exit != index)
                        continue;
                    for (size_t previous = 0U; previous < exit_count; ++previous)
                        duplicate = duplicate || exits[previous].error_type == caught[entry].error;
                    if (duplicate)
                        continue;
                    if (!r_generic_clone_node(clone, effect.cleanup_block, depth + 1U, &block) ||
                        !r_grow_array(context,
                                      (void **)&exits,
                                      &exit_capacity,
                                      sizeof(*exits),
                                      exit_count + 1U)) {
                        exits_ok = false;
                        break;
                    }
                    exits[exit_count++] = (RHirEffectExit){caught[entry].error, block, 0U, 0U};
                }
                continue;
            }
            if (!r_generic_substitute_type(context,
                                           effect.error_type,
                                           clone->schema,
                                           clone->arguments,
                                           &effect.error_type) ||
                !r_generic_clone_node(clone, effect.cleanup_block, depth + 1U, &block)) {
                exits_ok = false;
                break;
            }
            effect.cleanup_block = block;
            const RSemanticType *error = r_semantic_type(context, effect.error_type);
            const bool set = error != NULL && error->kind == R_SEMANTIC_TYPE_EFFECT_SET;
            const RTypeId effects = effect.error_type;
            const uint32_t count = set ? r_semantic_effect_count(context, effects) : 1U;
            for (uint32_t member = 0U; member < count; ++member) {
                effect.error_type = set ? r_semantic_effect_at(context, effects, member) : effects;
                bool duplicate = false;
                for (size_t previous = 0U; previous < exit_count; ++previous)
                    duplicate = duplicate || exits[previous].error_type == effect.error_type;
                if (duplicate)
                    continue;
                /* An empty inferred set contributes no exceptional completion path. */
                if (!r_grow_array(context,
                                  (void **)&exits,
                                  &exit_capacity,
                                  sizeof(effect),
                                  exit_count + 1U)) {
                    exits_ok = false;
                    break;
                }
                exits[exit_count++] = effect;
            }
        }
        for (size_t sorted = 1U; exits_ok && sorted < exit_count; ++sorted) {
            const RHirEffectExit key = exits[sorted];
            size_t position = sorted;
            while (position != 0U &&
                   r_semantic_compare_nominal_types(
                       context, exits[position - 1U].error_type, key.error_type) > 0) {
                exits[position] = exits[position - 1U];
                --position;
            }
            exits[position] = key;
        }
        if (exits_ok && exit_count != 0U &&
            !r_grow_array(context,
                          (void **)&context->hir_effect_exits,
                          &context->hir_effect_exit_capacity,
                          sizeof(*exits),
                          context->hir_effect_exit_count + exit_count))
            exits_ok = false;
        if (exits_ok && exit_count != 0U) {
            RHirNode *closed = &context->hir_nodes[*result - 1U];
            closed->first_effect_exit = (uint32_t)context->hir_effect_exit_count;
            closed->effect_exit_count = (uint32_t)exit_count;
            (void)memcpy(&context->hir_effect_exits[context->hir_effect_exit_count],
                         exits,
                         exit_count * sizeof(*exits));
            context->hir_effect_exit_count += exit_count;
        }
        r_context_free(context, exits);
        r_context_free(context, caught);
        if (!exits_ok)
            goto cleanup;
    }
    success = true;
cleanup:
    r_context_free(context, children.items);
    return success;
}

static bool r_generic_inherit_function_borrow_mappings(RFrontendContext *context, RSymbolId id) {
    const RSemanticSymbol instance = context->semantic_symbols[id - 1U];
    const RSemanticSymbol origin = context->semantic_symbols[instance.generic_origin - 1U];
    const size_t mapping_count = context->error_borrow_mapping_count;
    const size_t store_count = context->store_borrow_mapping_count;
    size_t index;

    /* R-BORROW-0019 (L15.2): a closed instance stores what its definition stores, except the
       views of parameters its arguments prove unborrowed. */
    for (index = 0U; index < store_count; ++index) {
        const RStoreBorrowMapping store = context->store_borrow_mappings[index];

        if (store.function != instance.generic_origin) {
            continue;
        }
        for (uint32_t source = 0U; (source < 128U) && (source < instance.parameter_count);
             ++source) {
            const uint64_t bit = UINT64_C(1) << (source % 64U);
            const uint64_t sources = source < 64U ? store.sources_low : store.sources_high;
            const uint64_t held = source < 64U ? store.held_low : store.held_high;
            const RTypeId type =
                context->semantic_parameter_types[(size_t)instance.first_parameter_type + source];

            if (((sources & bit) == 0U) ||
                r_generic_has_property(context, type, R_GENERIC_UNBORROWED, 0U)) {
                continue;
            }
            if (!r_semantic_record_store_borrow_parameter(
                    context, id, store.target, source + 1U, (held & bit) != 0U)) {
                return false;
            }
        }
    }

    context->semantic_symbols[id - 1U].return_borrow_parameter = origin.return_borrow_parameter;
    context->semantic_symbols[id - 1U].return_borrow_parameters_low =
        origin.return_borrow_parameters_low;
    context->semantic_symbols[id - 1U].return_borrow_parameters_high =
        origin.return_borrow_parameters_high;
    context->semantic_symbols[id - 1U].return_borrow_held_low = origin.return_borrow_held_low;
    context->semantic_symbols[id - 1U].return_borrow_held_high = origin.return_borrow_held_high;
    if (r_function_value_dispatcher(context, id)) {
        r_function_value_receiver_unborrowed(context, id);
    }
    if (r_generic_has_property(context, instance.return_type, R_GENERIC_UNBORROWED, 0U)) {
        context->semantic_symbols[id - 1U].return_borrow_parameter = 0U;
        context->semantic_symbols[id - 1U].return_borrow_parameters_low = 0U;
        context->semantic_symbols[id - 1U].return_borrow_parameters_high = 0U;
        context->semantic_symbols[id - 1U].return_borrow_held_low = 0U;
        context->semantic_symbols[id - 1U].return_borrow_held_high = 0U;
    }
    for (index = 0U; index < mapping_count; ++index) {
        const RErrorBorrowMapping mapping = context->error_borrow_mappings[index];
        RTypeId error_type;

        if (mapping.function != instance.generic_origin) {
            continue;
        }
        if (!r_generic_substitute_type(context,
                                       mapping.error_type,
                                       instance.generic_schema,
                                       instance.first_generic_argument,
                                       &error_type)) {
            return false;
        }
        const RSemanticType *error = r_semantic_type(context, error_type);
        const bool set = error != NULL && error->kind == R_SEMANTIC_TYPE_EFFECT_SET;
        const RTypeId effects = error_type;
        const uint32_t count = set ? r_semantic_effect_count(context, effects) : 1U;
        for (uint32_t member = 0U; member < count; ++member) {
            error_type = set ? r_semantic_effect_at(context, effects, member) : effects;
            if (r_generic_has_property(context, error_type, R_GENERIC_UNBORROWED, 0U))
                continue;
            if (mapping.parameters_low != 0U || mapping.parameters_high != 0U) {
                for (uint32_t parameter = 0U; parameter < 128U; ++parameter) {
                    const uint64_t mask =
                        parameter < 64U ? mapping.parameters_low : mapping.parameters_high;
                    const uint64_t held = parameter < 64U ? mapping.held_low : mapping.held_high;
                    const uint64_t bit = UINT64_C(1) << (parameter % 64U);
                    if ((mask & bit) != 0U &&
                        !r_semantic_record_error_borrow_parameter(
                            context, id, error_type, parameter + 1U, (held & bit) != 0U))
                        return false;
                }
            } else if (!r_semantic_record_error_borrow_parameter(
                           context, id, error_type, mapping.parameter, false))
                return false;
        }
    }
    return true;
}

static bool r_generic_close_function(RFrontendContext *context, RSymbolId id) {
    RSemanticSymbol instance = context->semantic_symbols[id - 1U];
    RGenericClone clone = {0};
    RHirNodeId function;
    uint8_t *selected_symbols = NULL, *selected_nodes = NULL;
    size_t index;
    bool success = false;
    if (instance.generic_origin == 0U) {
        return true;
    }
    if (!r_generic_inherit_function_borrow_mappings(context, id)) {
        return false;
    }
    instance = context->semantic_symbols[id - 1U];
    if (instance.hir_node != 0U || r_generic_function_is_open(context, &instance)) {
        return true;
    }
    /* R-TYPE-0051: a dyn dispatcher forwards to the implementation of each member, so a default
       method applied to the interface itself has no body of its own. */
    if (r_semantic_dyn_dispatcher(context, id) || r_function_value_dispatcher(context, id)) {
        context->semantic_symbols[id - 1U].has_definition = false;
        return true;
    }
    clone.context = context;
    clone.origin = instance.generic_origin;
    clone.schema = instance.generic_schema;
    clone.arguments = instance.first_generic_argument;
    clone.symbol_count = context->semantic_symbol_count;
    clone.node_count = context->hir_node_count;
    clone.origin_poisoned = context->semantic_symbols[instance.generic_origin - 1U].poisoned;
    clone.symbols = r_context_allocate(context, clone.symbol_count * sizeof(*clone.symbols));
    clone.nodes = r_context_allocate(context, clone.node_count * sizeof(*clone.nodes));
    if (clone.symbols == NULL || clone.nodes == NULL) {
        goto cleanup;
    }
    (void)memset(clone.symbols, 0, clone.symbol_count * sizeof(*clone.symbols));
    (void)memset(clone.nodes, 0, clone.node_count * sizeof(*clone.nodes));
    clone.symbols[instance.generic_origin - 1U] = id;
    selected_symbols = r_context_allocate(context, clone.symbol_count);
    selected_nodes = r_context_allocate(context, clone.node_count);
    if (selected_symbols == NULL || selected_nodes == NULL)
        goto cleanup;
    (void)memset(selected_symbols, 0, clone.symbol_count);
    (void)memset(selected_nodes, 0, clone.node_count);
    if (!r_generic_mark_selected_symbols(
            &clone,
            context->semantic_symbols[instance.generic_origin - 1U].hir_node,
            selected_symbols,
            selected_nodes,
            0U))
        goto cleanup;
    for (index = 0U; index < clone.symbol_count; ++index) {
        RSemanticSymbol symbol = context->semantic_symbols[index];
        if (symbol.parent != instance.generic_origin ||
            (symbol.kind != R_SEMANTIC_SYMBOL_PARAMETER &&
             symbol.content_parameter == R_SYMBOL_ID_INVALID && selected_symbols[index] == 0U)) {
            continue;
        }
        symbol.parent = id;
        symbol.hir_node = 0U;
        if (!r_generic_substitute_type(
                context, symbol.type, clone.schema, clone.arguments, &symbol.type) ||
            !r_semantic_append_symbol(context, symbol, &clone.symbols[index])) {
            goto cleanup;
        }
    }
    for (index = 0U; index < clone.symbol_count; ++index) {
        if (clone.symbols[index] != 0U) {
            RSemanticSymbol *symbol = &context->semantic_symbols[clone.symbols[index] - 1U];
            if (symbol->task_scope != 0U && symbol->task_scope <= clone.symbol_count)
                symbol->task_scope = clone.symbols[symbol->task_scope - 1U];
            if (symbol->out_parameter != 0U && symbol->out_parameter <= clone.symbol_count)
                symbol->out_parameter = clone.symbols[symbol->out_parameter - 1U];
            if (symbol->parameter_content != 0U && symbol->parameter_content <= clone.symbol_count)
                symbol->parameter_content = clone.symbols[symbol->parameter_content - 1U];
            if (symbol->content_parameter != 0U && symbol->content_parameter <= clone.symbol_count)
                symbol->content_parameter = clone.symbols[symbol->content_parameter - 1U];
            if (!r_semantic_remap_borrow_origins(context,
                                                 clone.symbols,
                                                 clone.symbol_count,
                                                 &symbol->borrow_origin,
                                                 &symbol->borrow_origin_set,
                                                 &symbol->borrow_origin_multiple)) {
                goto cleanup;
            }
        }
    }
    {
        const uint32_t previous_depth = context->generic_instantiation_depth;
        const RSymbolId previous_function = context->generic_current_function;
        context->generic_current_function = id;
        context->generic_instantiation_depth = instance.generic_depth;
        success =
            r_generic_clone_node(&clone,
                                 context->semantic_symbols[instance.generic_origin - 1U].hir_node,
                                 0U,
                                 &function);
        context->generic_instantiation_depth = previous_depth;
        context->generic_current_function = previous_function;
        if (!success) {
            goto cleanup;
        }
    }
    success = false;
    context->semantic_symbols[id - 1U].hir_node = function;
    for (index = 0U; index < clone.symbol_count; ++index) {
        if (clone.symbols[index] == 0U)
            continue;
        const uint32_t source_state = context->semantic_symbols[index].projected_borrow_state;
        uint32_t state;
        if (!r_generic_clone_borrow_state(&clone, source_state, 0U, &state))
            goto cleanup;
        context->semantic_symbols[clone.symbols[index] - 1U].projected_borrow_state = state;
    }
    {
        const size_t mapping_count = context->error_borrow_mapping_count;
        for (size_t mapping_index = 0U; mapping_index < mapping_count; ++mapping_index) {
            const RErrorBorrowMapping source = context->error_borrow_mappings[mapping_index];
            if (source.function != instance.generic_origin || !source.projections_seen)
                continue;
            RTypeId error_type;
            uint32_t projected;
            if (!r_generic_substitute_type(
                    context, source.error_type, clone.schema, clone.arguments, &error_type) ||
                !r_generic_clone_borrow_state(
                    &clone, source.projected_borrow_state, 0U, &projected))
                goto cleanup;
            const RErrorBorrowMapping *record =
                r_semantic_error_borrow_mapping(context, id, error_type);
            if (record == NULL)
                continue;
            const size_t record_index = (size_t)(record - context->error_borrow_mappings);
            RBodyContext body = {0};
            body.frontend = context;
            RErrorBorrowMapping *closed = &context->error_borrow_mappings[record_index];
            closed->projected_borrow_state = projected;
            closed->projections_seen = true;
            closed->projections_complete =
                r_body_projection_state_complete(&body, projected, error_type);
        }
    }
    /* Recompute projection coverage after substitution: a dependent component may become
       unborrowed, or expand into several independently borrowed fields. */
    {
        RBodyContext body = {0};
        body.frontend = context;
        body.function_symbol = id;
        body.return_type = context->semantic_symbols[id - 1U].return_type;
        context->semantic_symbols[id - 1U].return_projections_seen = false;
        context->semantic_symbols[id - 1U].return_projections_complete = false;
        context->semantic_symbols[id - 1U].return_projected_borrow_state = 0U;
        const size_t end = context->hir_node_count;
        for (size_t node_index = 0U; node_index < end; ++node_index) {
            const RHirNode returned = context->hir_nodes[node_index];
            RHirNodeId value_id;
            if (returned.kind != R_HIR_RETURN || returned.symbol != id ||
                returned.child_count == 0U ||
                r_semantic_value_kind(context, returned.type) == R_SEMANTIC_TYPE_VOID)
                continue;
            if (!r_body_hir_child(&body, &returned, 0U, &value_id))
                goto cleanup;
            const RHirNode value_node = context->hir_nodes[value_id - 1U];
            RExpressionResult value = {0};
            value.valid = true;
            value.node = value_id;
            value.type = value_node.type;
            value.projected_borrow_state = value_node.projected_borrow_state;
            if (!r_body_record_return_projections(&body, &value))
                goto cleanup;
        }
    }
    success = true;
cleanup:
    r_context_free(context, selected_symbols);
    r_context_free(context, selected_nodes);
    r_context_free(context, clone.symbols);
    r_context_free(context, clone.nodes);
    r_context_free(context, clone.pack_pieces);
    return success;
}

static bool r_generic_unborrowed(const RFrontendContext *context,
                                 RTypeId id,
                                 const RSemanticTypeVisit *parent) {
    const RSemanticType *type = r_semantic_type(context, id);
    const RSemanticTypeVisit *previous;
    RSemanticTypeVisit visit = {id, parent, parent == NULL ? 0U : parent->depth + 1U};
    const RSemanticAggregate *aggregate;
    uint32_t index;
    if (id == 0U) {
        return true;
    }
    if (type == NULL || visit.depth > context->options.limits.max_nesting) {
        return false;
    }
    for (previous = parent; previous != NULL; previous = previous->parent) {
        if (previous->type == id) {
            return true;
        }
    }
    if (type->kind == R_SEMANTIC_TYPE_PARAMETER) {
        return (r_generic_parameter_constraints(context, id) & R_GENERIC_UNBORROWED) != 0U;
    }
    if (r_dyn_owned(context, id) != R_TYPE_ID_INVALID) {
        /* R-TYPE-0055 (L29): an owner of an interface owns a member that holds no views. */
        return true;
    }
    if (type->kind == R_SEMANTIC_TYPE_BORROW || type->kind == R_SEMANTIC_TYPE_SLICE ||
        type->kind == R_SEMANTIC_TYPE_STR) {
        return false;
    }
    /* M26-2: a function value designates a function and holds no view (R-TYPE-0054), whatever
       its parameters borrow when it is called. */
    if (type->kind == R_SEMANTIC_TYPE_RAW || type->kind == R_SEMANTIC_TYPE_RAW_FUNCTION ||
        type->kind == R_SEMANTIC_TYPE_FUNCTION || type->kind == R_SEMANTIC_TYPE_VOID ||
        (type->kind >= R_SEMANTIC_TYPE_BOOL && type->kind <= R_SEMANTIC_TYPE_CONSTEXPR_STR)) {
        return true;
    }
    if (type->kind == R_SEMANTIC_TYPE_STRUCT || type->kind == R_SEMANTIC_TYPE_ENUM) {
        aggregate = r_semantic_aggregate_for_type(context, id);
        if (aggregate == NULL || !aggregate->complete || aggregate->poisoned) {
            return false;
        }
        for (index = 0U; index < aggregate->field_count; ++index) {
            if (!r_generic_unborrowed(context,
                                      context->semantic_fields[aggregate->first_field + index].type,
                                      &visit)) {
                return false;
            }
        }
        return true;
    }
    /* R-TYPE-0033: a guard, iterator or entry reference holds its creator's region. */
    if (r_semantic_standard_type_holds_region(context, type)) {
        return false;
    }
    return r_generic_unborrowed(context, type->base, &visit) &&
           (type->kind == R_SEMANTIC_TYPE_FIXED_ARRAY ||
            r_generic_unborrowed(context, type->second, &visit));
}

static bool r_generic_has_property(const RFrontendContext *context,
                                   RTypeId id,
                                   uint32_t property,
                                   uint32_t depth) {
    const RSemanticType *type = r_semantic_type(context, id);
    const RSemanticAggregate *aggregate;
    uint32_t index;
    if (type == NULL || depth > context->options.limits.max_nesting) {
        return false;
    }
    if (property == R_GENERIC_CLONE) {
        return r_clone_capability(context, id, depth);
    }
    if (type->kind == R_SEMANTIC_TYPE_PARAMETER) {
        return (r_generic_parameter_constraints(context, id) & property) == property;
    }
    if (type->kind == R_SEMANTIC_TYPE_DYN) {
        /* R-TYPE-0051: an interface proves exactly the capabilities its contract lists. */
        return (r_dyn_contract(context, id)->constraints & property) == property;
    }
    if (property == R_GENERIC_KEY && r_dyn_referent(context, id) != R_TYPE_ID_INVALID) {
        return false;
    }
    if (type->kind == R_SEMANTIC_TYPE_EFFECT_SET) {
        for (uint32_t entry = 0U; entry < r_semantic_effect_count(context, id); ++entry)
            if (!r_generic_has_property(
                    context, r_semantic_effect_at(context, id, entry), property, depth + 1U))
                return false;
        return true;
    }
    if (property == R_GENERIC_JSON_ENCODE || property == R_GENERIC_JSON_DECODE)
        return r_json_capability(context, id, property == R_GENERIC_JSON_ENCODE, depth);
    if (property == R_GENERIC_UNBORROWED) {
        return r_generic_unborrowed(context, id, NULL);
    }
    if (property == R_GENERIC_COPY) {
        return r_semantic_type_is_copy(context, id);
    }
    if (property == R_GENERIC_SEND || property == R_GENERIC_SYNC) {
        return property == R_GENERIC_SEND ? r_semantic_type_is_send(context, id)
                                          : r_semantic_type_is_sync(context, id);
    }
    if (property == R_GENERIC_ERROR) {
        return r_semantic_error_type_is_complete_nominal(context, id);
    }
    if (type->kind == R_SEMANTIC_TYPE_CONST) {
        return r_generic_has_property(context, type->base, property, depth + 1U);
    }
    if (property == R_GENERIC_KEY) {
        aggregate = r_semantic_aggregate_for_type(context, id);
        if (aggregate != NULL && aggregate->hash_function != 0U &&
            aggregate->equal_function != 0U) {
            return true;
        }
        if (type->kind == R_SEMANTIC_TYPE_ENUM) {
            aggregate = r_semantic_aggregate_for_type(context, id);
            return aggregate != NULL && aggregate->complete && !aggregate->is_tagged;
        }
        /* R-LIB-0020 (M24-2): an owned string keys by its UTF-8 bytes, like `str`. */
        if (r_semantic_standard_type_name_equal(context, type, "std.string::string")) {
            return true;
        }
        /* R-TYPE-0055 (L29): an owner of an interface is not a key. */
        return ((type->kind >= R_SEMANTIC_TYPE_BOOL &&
                 type->kind <= R_SEMANTIC_TYPE_CONSTEXPR_STR) ||
                type->kind == R_SEMANTIC_TYPE_RAW || type->kind == R_SEMANTIC_TYPE_BORROW ||
                type->kind == R_SEMANTIC_TYPE_OWN || type->kind == R_SEMANTIC_TYPE_ARC ||
                type->kind == R_SEMANTIC_TYPE_RC || type->kind == R_SEMANTIC_TYPE_WEAK) &&
               (r_dyn_owned(context, id) == R_TYPE_ID_INVALID);
    }
    if (type->kind >= R_SEMANTIC_TYPE_BOOL && type->kind <= R_SEMANTIC_TYPE_CHAR) {
        return true;
    }
    if (type->kind == R_SEMANTIC_TYPE_STRUCT || type->kind == R_SEMANTIC_TYPE_ENUM) {
        aggregate = r_semantic_aggregate_for_type(context, id);
        if (aggregate == NULL || !aggregate->complete || aggregate->poisoned ||
            (property == R_GENERIC_POD && aggregate->drop_function != 0U)) {
            return false;
        }
        for (index = 0U; index < aggregate->field_count; ++index) {
            if (!r_generic_has_property(
                    context,
                    context->semantic_fields[aggregate->first_field + index].type,
                    property,
                    depth + 1U)) {
                return false;
            }
        }
        return true;
    }
    if (type->kind == R_SEMANTIC_TYPE_FIXED_ARRAY || type->kind == R_SEMANTIC_TYPE_OPTION ||
        type->kind == R_SEMANTIC_TYPE_RESULT) {
        return r_generic_has_property(context, type->base, property, depth + 1U) &&
               (type->kind == R_SEMANTIC_TYPE_FIXED_ARRAY || type->second == 0U ||
                r_generic_has_property(context, type->second, property, depth + 1U));
    }
    if (property == R_GENERIC_POD) {
        const RNamedStandardCopyAbi *abi = r_semantic_standard_copy_abi(context, type);
        return abi != NULL && abi->is_pod;
    }
    if (property == R_GENERIC_UNBORROWED) {
        if (type->kind == R_SEMANTIC_TYPE_BORROW || type->kind == R_SEMANTIC_TYPE_SLICE ||
            type->kind == R_SEMANTIC_TYPE_STR) {
            return false;
        }
        if (type->kind == R_SEMANTIC_TYPE_RAW || type->kind == R_SEMANTIC_TYPE_CONSTEXPR_STR ||
            type->kind == R_SEMANTIC_TYPE_VOID) {
            return true;
        }
        if (type->kind >= R_SEMANTIC_TYPE_ARRAY) {
            return (type->base == 0U ||
                    r_generic_has_property(context, type->base, property, depth + 1U)) &&
                   (type->second == 0U ||
                    r_generic_has_property(context, type->second, property, depth + 1U));
        }
    }
    return false;
}

/* R-TYPE-0031, R-TYPE-0043: `argument` for parameter `index` of `schema_id` proves its
   constraints, reported by a diagnostic otherwise; false only for a resource failure. */
static bool r_generic_validate_against(RFrontendContext *context,
                                       uint32_t schema_id,
                                       const RGenericParameter parameter,
                                       RTypeId argument,
                                       const RTypeVector *arguments,
                                       RSourceSpan span) {
    const RSemanticType *type = r_semantic_type(context, argument);
    uint32_t property;
    if (parameter.constant_type != 0U) {
        /* R-TYPE-0047: the argument is a constant of the parameter's type. */
        if (r_generic_constant_type(context, argument) != parameter.constant_type)
            return r_semantic_add_diagnostic(
                context,
                "R-DIAG-CONST-001",
                "R-TYPE-0012",
                "constant generic argument requires a checked constant of the parameter type",
                span);
        return true;
    }
    if ((parameter.constraints & R_GENERIC_ERRORS) != 0U) {
        if (type == NULL ||
            (type->kind != R_SEMANTIC_TYPE_EFFECT_SET &&
             (r_generic_parameter_constraints(context, argument) & R_GENERIC_ERRORS) == 0U))
            return r_semantic_add_diagnostic(context,
                                             "R-DIAG-EFFECT-002",
                                             "R-TYPE-0012",
                                             "errors parameter requires an error set",
                                             span);
        for (property = R_GENERIC_COPY; property <= R_GENERIC_CLONE;
             property = r_generic_next_capability(property))
            if ((parameter.constraints & property) != 0U &&
                !r_generic_has_property(context, argument, property, 0U))
                return r_semantic_add_diagnostic(
                    context,
                    "R-DIAG-EFFECT-002",
                    "R-TYPE-0012",
                    "every error set member shall satisfy the parameter constraints",
                    span);
        return true;
    }
    if (type == NULL || type->kind == R_SEMANTIC_TYPE_CONSTANT_EXPR ||
        type->kind == R_SEMANTIC_TYPE_EFFECT_SET || type->kind == R_SEMANTIC_TYPE_CONST ||
        type->kind == R_SEMANTIC_TYPE_VOID || type->kind == R_SEMANTIC_TYPE_NULL_T ||
        !r_semantic_type_is_inhabited(context, argument, 0U) ||
        (context->generic_constraints_ready &&
         !r_semantic_type_is_slice_scalar(context, argument, false))) {
        return r_semantic_add_diagnostic(context,
                                         "R-DIAG-TYPE-001",
                                         "R-TYPE-0031",
                                         "generic argument shall be a complete sized inhabited "
                                         "value type without outer const",
                                         span);
    }
    if (!context->generic_constraints_ready) {
        return true;
    }
    for (property = R_GENERIC_COPY; property <= R_GENERIC_CLONE;
         property = r_generic_next_capability(property)) {
        if ((parameter.constraints & property) != 0U &&
            !r_generic_has_property(context, argument, property, 0U)) {
            return r_semantic_add_diagnostic(
                context,
                "R-DIAG-TYPE-001",
                "R-TYPE-0001",
                "generic type argument does not prove the required constraints",
                span);
        }
    }
    {
        uint32_t constraint;
        for (constraint = 0U; constraint < parameter.trait_constraint_count; ++constraint) {
            const uint32_t trait =
                context->generic_trait_constraints[parameter.first_trait_constraint + constraint];
            uint32_t effective = trait;
            if (!r_generic_effective_callable_trait(
                    context, trait, schema_id, arguments, span.source, &effective)) {
                return false;
            }
            if (!r_semantic_type_implements_trait(context, argument, effective)) {
                return r_semantic_add_diagnostic(
                    context,
                    "R-DIAG-TYPE-001",
                    "R-TYPE-0043",
                    "generic type argument does not implement the required trait",
                    span);
            }
        }
    }
    if (parameter.field_trait_count != 0U) {
        /* R-TYPE-0043 (L44): `fields(Trait)`; a parameter argument proves it with its own field
           traits, a struct or tuple with the traits of every field. */
        /* Proving a trait may instantiate implementations and move the aggregate and type
           arrays, so what the loop needs is copied first (defect L44-10). */
        const RSemanticAggregate *aggregate = r_semantic_aggregate_for_type(context, argument);
        const bool parameter_argument = (type->kind == R_SEMANTIC_TYPE_PARAMETER) &&
                                        (type->base != 0U) &&
                                        ((size_t)type->base <= context->generic_parameter_count);
        const uint32_t argument_base = parameter_argument ? type->base : 0U;
        if (!parameter_argument &&
            ((aggregate == NULL) || (aggregate->kind != R_SEMANTIC_AGGREGATE_STRUCT))) {
            return r_semantic_add_diagnostic(context,
                                             "R-DIAG-TYPE-001",
                                             "R-TYPE-0043",
                                             "a fields constraint requires a struct or tuple",
                                             span);
        }
        const uint32_t first_field = parameter_argument ? 0U : aggregate->first_field;
        const uint32_t field_count = parameter_argument ? 0U : aggregate->field_count;
        for (uint32_t constraint = 0U; constraint < parameter.field_trait_count; ++constraint) {
            const uint32_t trait =
                context->generic_trait_constraints[parameter.first_field_trait + constraint];
            if (parameter_argument) {
                const RGenericParameter proved = context->generic_parameters[argument_base - 1U];
                bool found = false;
                for (uint32_t own = 0U; !found && (own < proved.field_trait_count); ++own) {
                    found =
                        context->generic_trait_constraints[proved.first_field_trait + own] == trait;
                }
                if (!found) {
                    return r_semantic_add_diagnostic(
                        context,
                        "R-DIAG-TYPE-001",
                        "R-TYPE-0043",
                        "generic type argument does not prove the fields constraint",
                        span);
                }
                continue;
            }
            for (uint32_t member = 0U; member < field_count; ++member) {
                const RTypeId field_type = context->semantic_fields[first_field + member].type;
                if (!r_semantic_type_implements_trait(context, field_type, trait)) {
                    return r_semantic_add_diagnostic(
                        context,
                        "R-DIAG-TYPE-001",
                        "R-TYPE-0043",
                        "a field of the generic type argument does not implement the trait of "
                        "its fields constraint",
                        span);
                }
            }
        }
    }
    return true;
}

static bool r_generic_validate_argument(RFrontendContext *context,
                                        uint32_t schema_id,
                                        uint32_t index,
                                        RTypeId argument,
                                        const RTypeVector *arguments,
                                        RSourceSpan span) {
    const RGenericSchema schema = context->generic_schemas[schema_id - 1U];
    return r_generic_validate_against(context,
                                      schema_id,
                                      context->generic_parameters[schema.first_parameter + index],
                                      argument,
                                      arguments,
                                      span);
}

/* R-TYPE-0053 (L18.4): the types whose constraints stand for those of the elements of the pack
   argument `argument`: the elements of a tuple, the element carrier of a pack, and the prefix of
   a derived pack with the components of its base. False when the argument is no pack. */
static bool r_generic_pack_components(RFrontendContext *context,
                                      RTypeId argument,
                                      RTypeVector *components,
                                      uint32_t depth) {
    const RGenericParameter *record = r_generic_pack(context, argument);
    if (depth > context->options.limits.max_nesting) {
        return false;
    }
    if (r_tuple_aggregate(context, argument) != NULL) {
        const RSemanticAggregate *tuple = r_tuple_aggregate(context, argument);
        const uint32_t count = context->generic_schemas[tuple->generic_schema - 1U].parameter_count;
        const uint32_t first = tuple->first_generic_argument;
        for (uint32_t element = 0U; element < count; ++element) {
            if (!r_type_vector_push(
                    context, components, context->generic_arguments[first + element])) {
                return false;
            }
        }
        return true;
    }
    if (record == NULL) {
        return false;
    }
    if (!record->pack_derived) {
        return r_type_vector_push(context,
                                  components,
                                  record->pack_element != R_TYPE_ID_INVALID ? record->pack_element
                                                                            : argument);
    }
    const RGenericParameter derived = *record;
    return r_semantic_chain_arguments(context, derived.pack_prefix, components) &&
           r_generic_pack_components(context, derived.pack_base, components, depth + 1U);
}

/* R-TYPE-0043 (M19): each constrained associated type of the arguments proves the constraints
   that the header states for it. */
static bool r_generic_validate_projections(RFrontendContext *context,
                                           uint32_t schema_id,
                                           const RTypeVector *arguments,
                                           RSourceSpan span) {
    for (size_t index = 0U; index < context->generic_projection_constraint_count; ++index) {
        const RGenericProjectionConstraint constraint =
            context->generic_projection_constraints[index];
        uint32_t first;
        RTypeId concrete;
        if ((constraint.schema != schema_id) || (constraint.projection == R_TYPE_ID_INVALID)) {
            continue;
        }
        if (!r_generic_commit_arguments(context, arguments, &first) ||
            !r_generic_substitute_type(
                context, constraint.projection, schema_id, first, &concrete)) {
            return false;
        }
        if (!r_generic_validate_against(context,
                                        schema_id,
                                        context->generic_parameters[constraint.holder],
                                        concrete,
                                        arguments,
                                        span)) {
            return false;
        }
    }
    return true;
}

static bool r_generic_validate_arguments(RFrontendContext *context,
                                         uint32_t schema_id,
                                         const RTypeVector *arguments,
                                         RSourceSpan span) {
    RGenericSchema schema = context->generic_schemas[schema_id - 1U];
    for (uint32_t index = 0U; index < schema.parameter_count; ++index) {
        const RGenericParameter parameter =
            context->generic_parameters[schema.first_parameter + index];
        RTypeVector components = {0};
        if (!parameter.is_pack) {
            if (!r_generic_validate_argument(
                    context, schema_id, index, arguments->items[index], arguments, span)) {
                return false;
            }
            continue;
        }
        /* R-TYPE-0053 (L18.2): each element of the tuple a pack stands for proves the
           constraints of the elements of the pack. */
        if (!r_generic_pack_components(context, arguments->items[index], &components, 0U)) {
            r_context_free(context, components.items);
            if (context->resource_status != R_FRONTEND_OK) {
                return false;
            }
            return r_semantic_add_diagnostic(context,
                                             "R-DIAG-TYPE-001",
                                             "R-TYPE-0053",
                                             "a pack argument is a tuple of types",
                                             span);
        }
        const RGenericParameter carrier =
            parameter.pack_element != R_TYPE_ID_INVALID
                ? *r_generic_pack_carrier(context, parameter.pack_element)
                : parameter;
        for (size_t element = 0U; element < components.count; ++element) {
            if (!r_generic_validate_against(
                    context, schema_id, carrier, components.items[element], arguments, span)) {
                r_context_free(context, components.items);
                return false;
            }
        }
        r_context_free(context, components.items);
    }
    return !context->generic_constraints_ready ||
           r_generic_validate_projections(context, schema_id, arguments, span);
}

static bool r_generic_validate_nested_depth(RFrontendContext *context,
                                            RTypeId id,
                                            RSourceSpan span,
                                            const RSemanticTypeVisit *parent) {
    const RSemanticType *type = r_semantic_type(context, id);
    const RSemanticTypeVisit *previous;
    RSemanticTypeVisit visit = {id, parent, parent == NULL ? 0U : parent->depth + 1U};
    if (type == NULL) {
        return true;
    }
    if (type->kind == R_SEMANTIC_TYPE_PARAMETER &&
        (r_generic_parameter_constraints(context, id) & R_GENERIC_ERRORS) != 0U) {
        const RSemanticType *container =
            parent == NULL ? NULL : r_semantic_type(context, parent->type);
        if (container == NULL || container->kind != R_SEMANTIC_TYPE_EFFECT_SET)
            return r_semantic_add_diagnostic(
                context,
                "R-DIAG-TYPE-001",
                "R-TYPE-0049",
                "an error set parameter can occur only as a throws entry or set argument",
                span);
    }
    for (previous = parent; previous != NULL; previous = previous->parent) {
        if (previous->type == id) {
            return true;
        }
    }
    if (visit.depth > context->options.limits.max_nesting) {
        (void)r_semantic_add_diagnostic(
            context,
            "R-DIAG-LIMIT-001",
            "R-LIMIT-0003",
            "generic type dependency validation exceeds the nesting limit",
            span);
        if (context->resource_status == R_FRONTEND_OK) {
            context->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
        }
        return false;
    }
    if (type->kind == R_SEMANTIC_TYPE_DICT &&
        !r_generic_has_property(context, type->base, R_GENERIC_KEY, 0U)) {
        return r_semantic_add_diagnostic(
            context,
            "R-DIAG-TYPE-001",
            "R-TYPE-0034",
            "a dependent dictionary key requires a proven key constraint",
            span);
    }
    if (type->kind == R_SEMANTIC_TYPE_TASK &&
        (!r_generic_unborrowed(context, type->base, NULL) ||
         !r_generic_unborrowed(context, type->second, NULL))) {
        return r_semantic_add_diagnostic(
            context,
            "R-DIAG-ASYNC-001",
            "R-TYPE-0029",
            "dependent task results and completion errors require proven unborrowed constraints",
            span);
    }
    if (type->kind == R_SEMANTIC_TYPE_STRUCT || type->kind == R_SEMANTIC_TYPE_ENUM) {
        const RSemanticAggregate aggregate = context->semantic_aggregates[type->base - 1U];
        for (uint32_t index = 0U; index < aggregate.field_count; ++index) {
            const RSemanticField field = context->semantic_fields[aggregate.first_field + index];
            if (!r_generic_validate_nested_depth(context, field.type, field.name_span, &visit)) {
                return false;
            }
        }
        return true;
    }
    if (type->kind < R_SEMANTIC_TYPE_FIXED_ARRAY || type->kind == R_SEMANTIC_TYPE_CONSTANT_EXPR) {
        return true;
    }
    return r_generic_validate_nested_depth(context, type->base, span, &visit) &&
           (type->kind == R_SEMANTIC_TYPE_FIXED_ARRAY ||
            r_generic_validate_nested_depth(context, type->second, span, &visit));
}

static bool r_generic_validate_nested(RFrontendContext *context, RTypeId id, RSourceSpan span) {
    return r_generic_validate_nested_depth(context, id, span, NULL);
}

static bool r_generic_validate_definitions(RFrontendContext *context) {
    size_t index;
    for (index = 0U; index < context->generic_parameter_count; ++index) {
        const RGenericParameter parameter = context->generic_parameters[index];
        if ((parameter.constraints & R_GENERIC_ERRORS) != 0U &&
            parameter.trait_constraint_count != 0U &&
            !r_semantic_add_diagnostic(
                context,
                "R-DIAG-TYPE-001",
                "R-TYPE-0049",
                "error set parameters accept member capabilities, not trait or callable bounds",
                parameter.name_span))
            return false;
    }
    for (index = 0U; index < context->semantic_aggregate_count; ++index) {
        const RSemanticAggregate *aggregate = &context->semantic_aggregates[index];
        if (aggregate->generic_schema != 0U && aggregate->generic_origin == 0U &&
            aggregate->payload_parent == 0U) {
            RGenericSchema *schema = &context->generic_schemas[aggregate->generic_schema - 1U];
            schema->aggregate = (uint32_t)index + 1U;
            schema->declaration_ast = aggregate->declaration_ast;
        }
    }
    for (index = 0U; index < context->semantic_symbol_count; ++index) {
        const RSemanticSymbol *symbol = &context->semantic_symbols[index];
        if (symbol->kind == R_SEMANTIC_SYMBOL_FUNCTION && symbol->generic_schema != 0U &&
            symbol->generic_origin == 0U) {
            RGenericSchema *schema = &context->generic_schemas[symbol->generic_schema - 1U];
            schema->function = (RSymbolId)index + 1U;
            schema->declaration_ast = symbol->declaration_ast;
        }
    }
    context->generic_constraints_ready = true;
    /* R-TYPE-0045 (L16.3): the arguments of an application formed before the constraints were
       known prove the constraints of the parameters of its associated type. */
    for (index = 0U; index < context->generic_parameter_count; ++index) {
        const RGenericParameter application = context->generic_parameters[index];
        const uint32_t schema =
            application.application_base == R_TYPE_ID_INVALID
                ? 0U
                : r_semantic_associated_parameter_schema(context, application.application_base);
        RTypeVector arguments = {0};
        bool valid;
        if (schema == 0U) {
            continue;
        }
        valid =
            r_semantic_chain_arguments(context, application.application_arguments, &arguments) &&
            r_generic_validate_arguments(
                context, schema, &arguments, application.application_span);
        r_context_free(context, arguments.items);
        if (!valid) {
            return false;
        }
    }
    for (index = 0U; index < context->semantic_aggregate_count; ++index) {
        RSemanticAggregate aggregate = context->semantic_aggregates[index];
        if (aggregate.generic_schema != 0U &&
            !r_generic_validate_nested(context, aggregate.type, aggregate.name_span)) {
            return false;
        }
        if (aggregate.generic_origin != 0U) {
            RTypeVector arguments = {0};
            const uint32_t count =
                context->generic_schemas[aggregate.generic_schema - 1U].parameter_count;
            bool valid = true;
            /* Resolving dependent callable bounds interns more arguments. Keep this
             * instance's arguments independent of that growable context table. */
            for (uint32_t parameter = 0U; valid && parameter < count; ++parameter)
                valid = r_type_vector_push(
                    context,
                    &arguments,
                    context->generic_arguments[aggregate.first_generic_argument + parameter]);
            valid =
                valid && r_generic_validate_arguments(
                             context, aggregate.generic_schema, &arguments, aggregate.name_span);
            r_context_free(context, arguments.items);
            if (!valid) {
                return false;
            }
        }
    }
    for (index = 0U; index < context->semantic_symbol_count; ++index) {
        const RSemanticSymbol symbol = context->semantic_symbols[index];
        RTypeVector parameters = {0};
        uint32_t parameter;
        bool success = true;
        if (symbol.kind != R_SEMANTIC_SYMBOL_FUNCTION || symbol.generic_schema == 0U ||
            symbol.generic_origin != 0U) {
            continue;
        }
        for (parameter = 0U;
             parameter < context->generic_schemas[symbol.generic_schema - 1U].parameter_count;
             ++parameter) {
            if (!r_type_vector_push(context, &parameters, R_TYPE_ID_INVALID)) {
                success = false;
                break;
            }
        }
        if (success && symbol.is_method && (symbol.associated_type != R_TYPE_ID_INVALID)) {
            /* R-FUNC-0013: the schema prefix of a method follows from the owner spelling. */
            const RSemanticAggregate *owner =
                r_semantic_aggregate_for_type(context, symbol.associated_type);
            if ((owner != NULL) && (owner->generic_origin != 0U)) {
                owner = &context->semantic_aggregates[owner->generic_origin - 1U];
            }
            if ((owner != NULL) && (owner->generic_schema != 0U)) {
                const uint32_t owner_prefix =
                    context->generic_schemas[owner->generic_schema - 1U].parameter_count;
                const RGenericSchema *method_schema =
                    &context->generic_schemas[symbol.generic_schema - 1U];
                for (parameter = 0U; (parameter < owner_prefix) && (parameter < parameters.count);
                     ++parameter) {
                    parameters.items[parameter] =
                        context->generic_parameters[method_schema->first_parameter + parameter]
                            .type;
                }
            }
        }
        for (parameter = 0U; success && parameter < symbol.parameter_count; ++parameter) {
            const RTypeId type =
                context->semantic_parameter_types[symbol.first_parameter_type + parameter];
            success =
                r_generic_validate_nested(context, type, symbol.name_span) &&
                r_generic_infer_type(context, type, type, symbol.generic_schema, &parameters, 0U);
        }
        if (success) {
            /* R-TYPE-0044: a callable constraint of an inferable parameter makes the parameters
               its signature names inferable as well. */
            const RGenericSchema *definition_schema =
                &context->generic_schemas[symbol.generic_schema - 1U];
            bool changed = true;
            while (success && changed) {
                changed = false;
                for (parameter = 0U; success && (parameter < parameters.count); ++parameter) {
                    const RGenericParameter *record =
                        &context
                             ->generic_parameters[definition_schema->first_parameter + parameter];
                    uint32_t constraint;
                    if (parameters.items[parameter] == R_TYPE_ID_INVALID) {
                        continue;
                    }
                    for (constraint = 0U; success && (constraint < record->trait_constraint_count);
                         ++constraint) {
                        const uint32_t trait =
                            context->generic_trait_constraints[record->first_trait_constraint +
                                                               constraint];
                        const RTypeId signature =
                            context->semantic_traits[trait - 1U].callable_signature;
                        uint32_t known_before = 0U;
                        uint32_t known_after = 0U;
                        uint32_t probe;
                        if (signature == R_TYPE_ID_INVALID) {
                            continue;
                        }
                        for (probe = 0U; probe < parameters.count; ++probe) {
                            known_before += parameters.items[probe] != R_TYPE_ID_INVALID;
                        }
                        success = r_generic_infer_type(
                            context, signature, signature, symbol.generic_schema, &parameters, 0U);
                        for (probe = 0U; probe < parameters.count; ++probe) {
                            known_after += parameters.items[probe] != R_TYPE_ID_INVALID;
                        }
                        changed = changed || (known_after != known_before);
                    }
                }
            }
        }
        if (success) {
            success = r_generic_validate_nested(context, symbol.return_type, symbol.name_span) &&
                      r_generic_validate_nested(context, symbol.throws_type, symbol.name_span);
        }
        if (success && symbol.is_extern_c) {
            success = r_semantic_add_diagnostic(context,
                                                "R-DIAG-TYPE-001",
                                                "R-FUNC-0001",
                                                "generic functions cannot use C linkage",
                                                symbol.name_span);
        }
        if (success && symbol.is_async) {
            bool unborrowed = r_generic_unborrowed(context, symbol.return_type, NULL) &&
                              r_generic_unborrowed(context, symbol.throws_type, NULL);
            for (parameter = 0U; !symbol.is_scoped && parameter < symbol.parameter_count;
                 ++parameter) {
                unborrowed =
                    unborrowed &&
                    r_generic_unborrowed(
                        context,
                        context->semantic_parameter_types[symbol.first_parameter_type + parameter],
                        NULL);
            }
            if (!unborrowed) {
                success = r_semantic_add_diagnostic(
                    context,
                    "R-DIAG-ASYNC-001",
                    "R-TYPE-0029",
                    "generic async results, captures and completion errors require proven send and "
                    "unborrowed constraints",
                    symbol.name_span);
            }
        }
        r_context_free(context, parameters.items);
        if (!success) {
            return false;
        }
    }
    return true;
}

/* R-FUNC-0009: a generic hook repeats the parameters of its type; a hash, equal or clone hook
 * (`conditional`) may add capability constraints, which then decide the instances that have
 * the hook (L27). */
static bool r_generic_hook_matches(RFrontendContext *context,
                                   uint32_t type_id,
                                   uint32_t hook_id,
                                   bool conditional) {
    const RGenericSchema *type = &context->generic_schemas[type_id - 1U];
    const RGenericSchema *hook = &context->generic_schemas[hook_id - 1U];
    uint32_t index;
    if (type->parameter_count != hook->parameter_count) {
        return false;
    }
    for (index = 0U; index < type->parameter_count; ++index) {
        const RGenericParameter *left = &context->generic_parameters[type->first_parameter + index];
        const RGenericParameter *right =
            &context->generic_parameters[hook->first_parameter + index];
        if (left->name_intern_id != right->name_intern_id ||
            left->constant_type != right->constant_type ||
            (conditional ? (left->constraints & ~right->constraints) != 0U
                         : left->constraints != right->constraints)) {
            return false;
        }
    }
    return true;
}

/* R-FUNC-0009 (L27): whether the arguments of the instance `instance` prove the capability
 * constraints that the hook `hook` adds to the parameters of its type. */
static bool r_generic_hook_applies(const RFrontendContext *context,
                                   const RSemanticAggregate *instance,
                                   const RSemanticAggregate *origin,
                                   RSymbolId hook) {
    const RGenericSchema *type = &context->generic_schemas[origin->generic_schema - 1U];
    const uint32_t hook_schema = context->semantic_symbols[hook - 1U].generic_schema;
    if (hook_schema == 0U) {
        return true;
    }
    const RGenericSchema *added = &context->generic_schemas[hook_schema - 1U];
    for (uint32_t index = 0U; index < type->parameter_count && index < added->parameter_count;
         ++index) {
        const uint32_t extra =
            context->generic_parameters[added->first_parameter + index].constraints &
            ~context->generic_parameters[type->first_parameter + index].constraints;
        const RTypeId argument =
            context->generic_arguments[instance->first_generic_argument + index];
        for (uint32_t property = R_GENERIC_COPY; property <= R_GENERIC_CLONE;
             property = r_generic_next_capability(property))
            if ((extra & property) != 0U &&
                !r_generic_has_property(context, argument, property, 0U))
                return false;
    }
    return true;
}

static bool r_generic_bind_drop(RFrontendContext *context, uint32_t id) {
    const RSemanticAggregate instance = context->semantic_aggregates[id - 1U];
    RSemanticAggregate origin;
    RTypeVector arguments = {0};
    RSymbolId hooks[8];
    uint32_t index;
    bool success = true;
    if (instance.generic_origin == 0U) {
        return true;
    }
    origin = context->semantic_aggregates[instance.generic_origin - 1U];
    hooks[0] = origin.drop_function;
    hooks[1] = origin.hash_function;
    hooks[2] = origin.equal_function;
    hooks[3] = origin.json_marshal_function;
    hooks[4] = origin.json_unmarshal_function;
    hooks[5] = origin.json_is_zero_function;
    hooks[6] = origin.clone_function;
    hooks[7] = origin.json_schema_function;
    for (index = 0U; index < context->generic_schemas[instance.generic_schema - 1U].parameter_count;
         ++index) {
        if (!r_type_vector_push(
                context,
                &arguments,
                context->generic_arguments[instance.first_generic_argument + index])) {
            success = false;
            break;
        }
    }
    /* R-FUNC-0009 (L27): an instance whose arguments do not prove the constraints a key or
       clone hook adds has neither key hook, or no clone hook. */
    if ((hooks[1] != 0U && !r_generic_hook_applies(context, &instance, &origin, hooks[1])) ||
        (hooks[2] != 0U && !r_generic_hook_applies(context, &instance, &origin, hooks[2]))) {
        hooks[1] = 0U;
        hooks[2] = 0U;
        context->semantic_aggregates[id - 1U].hash_function = 0U;
        context->semantic_aggregates[id - 1U].equal_function = 0U;
    }
    if (hooks[6] != 0U && !r_generic_hook_applies(context, &instance, &origin, hooks[6])) {
        hooks[6] = 0U;
        context->semantic_aggregates[id - 1U].clone_function = 0U;
    }
    for (index = 0U; success && index < 8U; ++index) {
        RSymbolId closed;
        if (hooks[index] == 0U) {
            continue;
        }
        success = r_generic_apply_function(context, hooks[index], &arguments, &closed);
        if (success) {
            if (index == 0U) {
                context->semantic_aggregates[id - 1U].drop_function = closed;
                context->semantic_aggregates[id - 1U].is_copy = false;
            } else if (index == 1U) {
                context->semantic_aggregates[id - 1U].hash_function = closed;
            } else if (index == 2U) {
                context->semantic_aggregates[id - 1U].equal_function = closed;
            } else if (index == 3U) {
                context->semantic_aggregates[id - 1U].json_marshal_function = closed;
            } else if (index == 4U) {
                context->semantic_aggregates[id - 1U].json_unmarshal_function = closed;
            } else if (index == 5U) {
                context->semantic_aggregates[id - 1U].json_is_zero_function = closed;
            } else if (index == 6U) {
                context->semantic_aggregates[id - 1U].clone_function = closed;
            } else {
                context->semantic_aggregates[id - 1U].json_schema_function = closed;
            }
        }
    }
    r_context_free(context, arguments.items);
    return success;
}

typedef struct RGenericText {
    RFrontendContext *context;
    uint8_t *bytes;
    size_t length;
    size_t capacity;
    /* Text for a diagnostic: a type parameter is spelled by its name alone, not by the
       definition-qualified key that makes instance identities unique. */
    bool display;
} RGenericText;

static bool r_generic_text(RGenericText *text, const void *bytes, size_t length) {
    if (length > SIZE_MAX - text->length ||
        !r_grow_array(
            text->context, (void **)&text->bytes, &text->capacity, 1U, text->length + length)) {
        return false;
    }
    if (length != 0U) {
        (void)memcpy(text->bytes + text->length, bytes, length);
        text->length += length;
    }
    return true;
}

/* R-TYPE-0047: the type keyword and the value of a canonical typed constant; a signed value is
 * sign-extended from its width and a bool is spelled `true` or `false`. */
static bool r_generic_constant_spelling(const RSemanticType *type,
                                        const char **keyword,
                                        char *value,
                                        size_t capacity) {
    const RTokenKind token =
        type->flags == (uint32_t)R_TOKEN_INVALID ? R_TOKEN_KW_USIZE : (RTokenKind)type->flags;
    uint64_t bits = type->length;
    int count;
    *keyword = r_token_kind_name(token);
    if (token == R_TOKEN_KW_BOOL) {
        count = snprintf(value, capacity, "%s", bits != 0U ? "true" : "false");
    } else if (token == R_TOKEN_KW_I8 || token == R_TOKEN_KW_I16 || token == R_TOKEN_KW_I32 ||
               token == R_TOKEN_KW_I64 || token == R_TOKEN_KW_ISIZE) {
        const uint32_t width = token == R_TOKEN_KW_I8    ? 8U
                               : token == R_TOKEN_KW_I16 ? 16U
                               : token == R_TOKEN_KW_I32 ? 32U
                                                         : 64U;
        if (width < 64U && (bits & (UINT64_C(1) << (width - 1U))) != 0U)
            bits |= ~((UINT64_C(1) << width) - 1U);
        count = snprintf(value, capacity, "%" PRId64, (int64_t)bits);
    } else {
        count = snprintf(value, capacity, "%" PRIu64, bits);
    }
    return *keyword != NULL && count > 0 && (size_t)count < capacity;
}

static bool r_generic_type_name(RGenericText *text, RTypeId id, uint32_t depth) {
    const RFrontendContext *context = text->context;
    const RSemanticType *type = r_semantic_type(context, id);
    const RInternEntry *name;
    const char *kind;
    char suffix[64];
    int count;
    if (type == NULL) {
        return r_generic_text(text, "_", 1U);
    }
    if (depth > context->options.limits.max_nesting) {
        return false;
    }
    if (type->kind == R_SEMANTIC_TYPE_DYN) {
        /* R-TYPE-0051: an interface is named by its canonical contract. */
        name = &context->intern_entries[r_dyn_contract(context, id)->dyn_key - 1U];
        return r_generic_text(text, name->bytes, name->length);
    }
    if ((type->kind == R_SEMANTIC_TYPE_STRUCT) &&
        context->semantic_aggregates[type->base - 1U].is_tuple &&
        (context->semantic_aggregates[type->base - 1U].generic_origin != 0U)) {
        /* R-TYPE-0052 (L18.1): a tuple is named by its elements, `(A,B)`. */
        const RSemanticAggregate *tuple = &context->semantic_aggregates[type->base - 1U];
        const uint32_t arity = context->generic_schemas[tuple->generic_schema - 1U].parameter_count;
        if (!r_generic_text(text, "(", 1U)) {
            return false;
        }
        for (uint32_t element = 0U; element < arity; ++element) {
            if (((element != 0U) &&
                 !r_generic_text(text, text->display ? ", " : ",", text->display ? 2U : 1U)) ||
                !r_generic_type_name(
                    text,
                    context->generic_arguments[tuple->first_generic_argument + element],
                    depth + 1U)) {
                return false;
            }
        }
        return r_generic_text(text, ")", 1U);
    }
    if (type->kind == R_SEMANTIC_TYPE_STRUCT || type->kind == R_SEMANTIC_TYPE_ENUM) {
        const RSemanticAggregate *aggregate = &context->semantic_aggregates[type->base - 1U];
        const RSource *source = r_get_source_const(context, aggregate->module_source);
        name = &context->intern_entries[aggregate->name_intern_id - 1U];
        return (source == NULL || source->module_name == NULL ||
                (r_generic_text(text, source->module_name, strlen(source->module_name)) &&
                 r_generic_text(text, "::", 2U))) &&
               r_generic_text(text, name->bytes, name->length);
    }
    if (type->kind == R_SEMANTIC_TYPE_CONSTANT_EXPR && type->flags == (uint32_t)R_TOKEN_DOT) {
        /* R-TYPE-0050: an associated constant, named by its trait and its owner. */
        const RSemanticTrait *trait = &context->semantic_traits[(type->length >> 32U) - 1U];
        const RSource *source = r_get_source_const(context, trait->module_source);
        const RInternEntry *trait_name = &context->intern_entries[trait->name_intern_id - 1U];
        const RInternEntry *constant_name =
            &context
                 ->intern_entries[context
                                      ->semantic_associated_constants[trait->first_constant +
                                                                      (type->length & UINT32_MAX)]
                                      .name_intern_id -
                                  1U];
        RTypeId link = type->base;
        uint32_t index = 0U;
        if (!r_generic_text(text, "constant[associated,", 20U) ||
            (source != NULL && source->module_name != NULL &&
             (!r_generic_text(text, source->module_name, strlen(source->module_name)) ||
              !r_generic_text(text, "::", 2U))) ||
            !r_generic_text(text, trait_name->bytes, trait_name->length) ||
            !r_generic_text(text, "::", 2U) ||
            !r_generic_text(text, constant_name->bytes, constant_name->length) ||
            !r_generic_text(text, "]<", 2U))
            return false;
        for (; link != R_TYPE_ID_INVALID; ++index) {
            const RSemanticType *part = r_semantic_type(context, link);
            if (part == NULL || (index != 0U && !r_generic_text(text, ",", 1U)) ||
                !r_generic_type_name(text, part->base, depth + 1U))
                return false;
            link = part->second;
        }
        return r_generic_text(text, ">", 1U);
    }
    if (type->kind == R_SEMANTIC_TYPE_CONSTANT_EXPR && type->flags == (uint32_t)R_TOKEN_LPAREN) {
        /* R-TYPE-0047: a computed bound, named by its formula and its declaration's arguments. */
        const RInternEntry *formula = &context->intern_entries[type->length - 1U];
        RTypeId link = type->base;
        uint32_t index = 0U;
        if (!r_generic_text(text, "constant[call,", 14U) ||
            !r_generic_text(text, formula->bytes, formula->length) ||
            !r_generic_text(text, "]<", 2U))
            return false;
        for (; link != R_TYPE_ID_INVALID; ++index) {
            const RSemanticType *part = r_semantic_type(context, link);
            if (part == NULL || part->kind != R_SEMANTIC_TYPE_CONSTANT_EXPR ||
                (index != 0U && !r_generic_text(text, ",", 1U)) ||
                !r_generic_type_name(text, part->base, depth + 1U))
                return false;
            link = part->second;
        }
        return r_generic_text(text, ">", 1U);
    }
    if (type->kind == R_SEMANTIC_TYPE_CONSTANT_EXPR && type->base == 0U && type->second == 0U) {
        const char *keyword;
        char value[32];
        if (!r_generic_constant_spelling(type, &keyword, value, sizeof(value)))
            return false;
        count = snprintf(suffix, sizeof(suffix), "constant[%s,%s]", keyword, value);
        return count > 0 && (size_t)count < sizeof(suffix) &&
               r_generic_text(text, suffix, (size_t)count);
    }
    if (type->kind == R_SEMANTIC_TYPE_PARAMETER) {
        const RGenericParameter *parameter = &context->generic_parameters[type->base - 1U];
        const uint32_t schema_id = r_generic_schema_at(context, parameter->name_span);
        const RGenericSchema *schema;
        const RSource *source;
        name = &context->intern_entries[parameter->name_intern_id - 1U];
        if (parameter->opaque_declaration.source != 0U) {
            const RSource *definition =
                r_get_source_const(context, parameter->opaque_declaration.source);
            count =
                snprintf(suffix, sizeof(suffix), "::$opaque@%" PRIu32, parameter->name_span.start);
            if (count <= 0 || (size_t)count >= sizeof(suffix) || definition == NULL ||
                definition->module_name == NULL ||
                !r_generic_text(text, definition->module_name, strlen(definition->module_name)) ||
                !r_generic_text(text, suffix, (size_t)count))
                return false;
            if (parameter->opaque_origin != 0U) {
                if (!r_generic_text(text, "<", 1U))
                    return false;
                const uint32_t argument_count =
                    context->generic_schemas[parameter->opaque_schema - 1U].parameter_count;
                for (uint32_t index = 0U; index < argument_count; ++index) {
                    if ((index != 0U && !r_generic_text(text, ",", 1U)) ||
                        !r_generic_type_name(
                            text,
                            context->generic_arguments[parameter->opaque_first_argument + index],
                            depth + 1U))
                        return false;
                }
                return r_generic_text(text, ">", 1U);
            }
            return true;
        }
        if (parameter->pack_base != R_TYPE_ID_INVALID) {
            /* R-TYPE-0053 (L18.4): `T.i` for an element, `(A, T...)` for a derived pack. */
            RTypeId link = parameter->pack_prefix;
            if (!parameter->pack_derived) {
                count = snprintf(suffix, sizeof(suffix), ".%" PRIu32, parameter->pack_index);
                return r_generic_type_name(text, parameter->pack_base, depth + 1U) && count > 0 &&
                       (size_t)count < sizeof(suffix) &&
                       r_generic_text(text, suffix, (size_t)count);
            }
            if (!r_generic_text(text, "(", 1U)) {
                return false;
            }
            while (link != R_TYPE_ID_INVALID) {
                const RSemanticType *part = r_semantic_type(context, link);
                if ((part == NULL) || !r_generic_type_name(text, part->base, depth + 1U) ||
                    !r_generic_text(text, text->display ? ", " : ",", text->display ? 2U : 1U)) {
                    return false;
                }
                link = part->second;
            }
            if (!r_generic_type_name(text, parameter->pack_base, depth + 1U) ||
                !r_generic_text(text, "...", 3U)) {
                return false;
            }
            if (parameter->pack_index != 0U) {
                count = snprintf(suffix, sizeof(suffix), "[%" PRIu32 "..]", parameter->pack_index);
                if ((count <= 0) || ((size_t)count >= sizeof(suffix)) ||
                    !r_generic_text(text, suffix, (size_t)count)) {
                    return false;
                }
            }
            return r_generic_text(text, ")", 1U);
        }
        if (parameter->projection_base != R_TYPE_ID_INVALID) {
            /* R-TYPE-0045: a projection is named after its base, `P::Name`. */
            return r_generic_type_name(text, parameter->projection_base, depth + 1U) &&
                   r_generic_text(text, "::", 2U) &&
                   r_generic_text(text, name->bytes, name->length);
        }
        if (parameter->application_base != R_TYPE_ID_INVALID) {
            /* R-TYPE-0045 (L16.3): an application is named after its base, `P::Name<T>`. */
            RTypeId link = parameter->application_arguments;
            if (!r_generic_type_name(text, parameter->application_base, depth + 1U) ||
                !r_generic_text(text, "<", 1U))
                return false;
            while (link != R_TYPE_ID_INVALID) {
                const RSemanticType *part = r_semantic_type(context, link);
                if (part == NULL ||
                    (link != parameter->application_arguments && !r_generic_text(text, ",", 1U)) ||
                    !r_generic_type_name(text, part->base, depth + 1U))
                    return false;
                link = part->second;
            }
            return r_generic_text(text, ">", 1U);
        }
        schema = schema_id == 0U ? NULL : &context->generic_schemas[schema_id - 1U];
        source = schema == NULL ? NULL : r_get_source_const(context, schema->span.source);
        if (text->display) {
            return r_generic_text(text, name->bytes, name->length);
        }
        if ((schema == NULL) || (source == NULL) || (source->module_name == NULL)) {
            /* A synthetic parameter of a compiler-declared schema (R-TYPE-0044, R-TYPE-0046). */
            count = snprintf(suffix, sizeof(suffix), "$%" PRIu32 ":", type->base);
            return count > 0 && (size_t)count < sizeof(suffix) &&
                   r_generic_text(text, suffix, (size_t)count) &&
                   r_generic_text(text, name->bytes, name->length);
        }
        count = snprintf(suffix, sizeof(suffix), "@%" PRIu32 ":", schema->span.start);
        return count > 0 && (size_t)count < sizeof(suffix) &&
               r_generic_text(text, source->module_name, strlen(source->module_name)) &&
               r_generic_text(text, suffix, (size_t)count) &&
               r_generic_text(text, name->bytes, name->length);
    }
    if (type->kind == R_SEMANTIC_TYPE_STANDARD) {
        name = &context->intern_entries[(size_t)type->length - 1U];
        if (!r_generic_text(text, name->bytes, name->length)) {
            return false;
        }
    } else if (type->kind == R_SEMANTIC_TYPE_C_ABI_NUMERIC) {
        const char *spelling = r_token_kind_name((RTokenKind)type->length);
        return spelling != NULL && r_generic_text(text, spelling, strlen(spelling));
    } else {
        kind = r_semantic_type_kind_name(type->kind);
        if (!r_generic_text(text, kind, strlen(kind))) {
            return false;
        }
    }
    if (type->kind >= R_SEMANTIC_TYPE_FIXED_ARRAY && (type->base != 0U || type->second != 0U)) {
        if (!r_generic_text(text, "<", 1U) || !r_generic_type_name(text, type->base, depth + 1U) ||
            (type->second != 0U && (!r_generic_text(text, ",", 1U) ||
                                    !r_generic_type_name(text, type->second, depth + 1U))) ||
            !r_generic_text(text, ">", 1U)) {
            return false;
        }
        if (type->flags != 0U || (type->kind != R_SEMANTIC_TYPE_STANDARD && type->length != 0U)) {
            count = snprintf(suffix,
                             sizeof(suffix),
                             "[%" PRIu32 ",%" PRIu64 "]",
                             type->flags,
                             type->kind == R_SEMANTIC_TYPE_STANDARD ? UINT64_C(0) : type->length);
            return count > 0 && (size_t)count < sizeof(suffix) &&
                   r_generic_text(text, suffix, (size_t)count);
        }
    }
    return true;
}

static bool r_generic_instance_name(RFrontendContext *context,
                                    uint32_t base_name,
                                    const RTypeVector *arguments,
                                    uint32_t *result) {
    const RInternEntry *base = &context->intern_entries[base_name - 1U];
    RGenericText text = {context, NULL, 0U, 0U, false};
    size_t index;
    bool success = false;
    if (!r_generic_text(&text, base->bytes, base->length) || !r_generic_text(&text, "<", 1U)) {
        goto cleanup;
    }
    for (index = 0U; index < arguments->count; ++index) {
        if ((index != 0U && !r_generic_text(&text, ",", 1U)) ||
            !r_generic_type_name(&text, arguments->items[index], 0U)) {
            goto cleanup;
        }
    }
    success =
        r_generic_text(&text, ">", 1U) && r_intern_bytes(context, text.bytes, text.length, result);
cleanup:
    r_context_free(context, text.bytes);
    return success;
}

static void r_generic_limit_diagnostic(RFrontendContext *context, uint32_t name, RSourceSpan span) {
    RGenericText text = {context, NULL, 0U, 0U, true};
    RSymbolId parent = context->generic_current_function;
    const char prefix[] = "generic instantiation exceeds the nesting limit: ";
    uint32_t count = 0U;
    if (!r_generic_text(&text, prefix, sizeof(prefix) - 1U)) {
        goto cleanup;
    }
    for (;;) {
        const RInternEntry *entry = &context->intern_entries[name - 1U];
        if (entry->length <= 192U) {
            if (!r_generic_text(&text, entry->bytes, entry->length)) {
                goto cleanup;
            }
        } else if (!r_generic_text(&text, entry->bytes, 128U) ||
                   !r_generic_text(&text, "...", 3U) ||
                   !r_generic_text(&text, entry->bytes + entry->length - 48U, 48U)) {
            goto cleanup;
        }
        if (parent == 0U || ++count > context->options.limits.max_nesting) {
            break;
        }
        if (count == 8U) {
            while (context->semantic_symbols[parent - 1U].generic_parent != 0U) {
                parent = context->semantic_symbols[parent - 1U].generic_parent;
            }
            if (!r_generic_text(&text, " <- ...", 7U)) {
                goto cleanup;
            }
        }
        const RSemanticSymbol *symbol = &context->semantic_symbols[parent - 1U];
        name = symbol->name_intern_id;
        parent = symbol->generic_parent;
        if (!r_generic_text(&text, " <- ", 4U)) {
            goto cleanup;
        }
    }
    uint32_t message;
    if (r_intern_bytes(context, text.bytes, text.length, &message)) {
        (void)r_semantic_add_diagnostic(context,
                                        "R-DIAG-LIMIT-001",
                                        "R-LIMIT-0003",
                                        context->intern_entries[message - 1U].bytes,
                                        span);
    }
cleanup:
    r_context_free(context, text.bytes);
}

static bool r_generic_order_aggregates(RFrontendContext *context) {
    RSemanticAggregate *ordered;
    uint32_t *indices, *mapping;
    size_t index;
    const size_t count = context->semantic_aggregate_count;
    if ((context->generic_schema_count == 0U && context->format_schema_count == 0U) ||
        count == 0U) {
        return true;
    }
    ordered = r_context_allocate(context, count * sizeof(*ordered));
    indices = r_context_allocate(context, count * sizeof(*indices));
    mapping = r_context_allocate(context, count * sizeof(*mapping));
    if (ordered == NULL || indices == NULL || mapping == NULL) {
        r_context_free(context, ordered);
        r_context_free(context, indices);
        r_context_free(context, mapping);
        return false;
    }
    for (index = 0U; index < count; ++index) {
        size_t position = index;
        while (position > 0U && r_semantic_compare_type_keys(
                                    context,
                                    context->semantic_aggregates[index].type,
                                    context->semantic_aggregates[indices[position - 1U]].type,
                                    0U) < 0) {
            indices[position] = indices[position - 1U];
            --position;
        }
        indices[position] = (uint32_t)index;
    }
    for (index = 0U; index < count; ++index) {
        ordered[index] = context->semantic_aggregates[indices[index]];
        mapping[indices[index]] = (uint32_t)index + 1U;
    }
    for (index = 0U; index < count; ++index) {
        if (ordered[index].generic_origin != 0U) {
            ordered[index].generic_origin = mapping[ordered[index].generic_origin - 1U];
        }
        if (ordered[index].payload_parent != 0U) {
            ordered[index].payload_parent = mapping[ordered[index].payload_parent - 1U];
        }
        /* R-AGG-0011 (L21.1): a parent and the error of a family are aggregate indices too. */
        if (ordered[index].error_parent != 0U) {
            ordered[index].error_parent = mapping[ordered[index].error_parent - 1U];
        }
        if ((ordered[index].error_family_of != 0U) &&
            (ordered[index].error_family_of != R_SEMANTIC_STANDARD_ERROR_ROOT)) {
            ordered[index].error_family_of = mapping[ordered[index].error_family_of - 1U];
        }
    }
    (void)memcpy(context->semantic_aggregates, ordered, count * sizeof(*ordered));
    for (index = 0U; index < context->semantic_type_count; ++index) {
        RSemanticType *type = &context->semantic_types[index];
        if (type->kind == R_SEMANTIC_TYPE_STRUCT || type->kind == R_SEMANTIC_TYPE_ENUM) {
            type->base = mapping[type->base - 1U];
        }
    }
    /* M32-7: the bases of nominal types changed, so their places in the index did too. */
    r_semantic_type_index_reset(context);
    for (index = 0U; index < context->semantic_field_count; ++index) {
        context->semantic_fields[index].aggregate =
            mapping[context->semantic_fields[index].aggregate - 1U];
    }
    for (index = 0U; index < context->semantic_variant_count; ++index) {
        context->semantic_variants[index].aggregate =
            mapping[context->semantic_variants[index].aggregate - 1U];
    }
    for (index = 0U; index < context->generic_schema_count; ++index) {
        if (context->generic_schemas[index].aggregate != 0U) {
            context->generic_schemas[index].aggregate =
                mapping[context->generic_schemas[index].aggregate - 1U];
        }
    }
    r_context_free(context, ordered);
    r_context_free(context, indices);
    r_context_free(context, mapping);
    return true;
}

static bool r_generic_prepare_associated(RFrontendContext *context,
                                         RAstRef function,
                                         RAstRef parameter_list,
                                         const RTypeVector *parameters,
                                         RSemanticSymbol *symbol) {
    RAstRef name_ref, owner_ref;
    RAstNodeView view;
    RAstTokenView owner_token;
    const RSemanticAggregate *aggregate;
    RTypeId owner_type;
    uint32_t operation;
    RGenericText name = {context, NULL, 0U, 0U, false};
    const RInternEntry *base;
    bool success;
    if (!r_ast_find_direct_child(context, function, R_SYNTAX_ASSOCIATED_NAME, &name_ref)) {
        return r_generic_prepare_member_method(context, parameter_list, parameters, symbol);
    }
    if (!r_ast_ref_view(context, name_ref, &view)) {
        return false;
    }
    if (r_ast_find_direct_child(context, name_ref, R_SYNTAX_TYPE, &owner_ref)) {
        if (!r_semantic_type_from_ast(context, owner_ref, &owner_type)) {
            return false;
        }
    } else if (!r_ast_ref_child(context, name_ref, 0U, &owner_ref) ||
               !r_ast_ref_token(context, owner_ref, &owner_token) ||
               !r_semantic_resolve_aggregate_type(context, owner_ref, &owner_type)) {
        return r_semantic_add_diagnostic(context,
                                         "R-DIAG-TYPE-001",
                                         "R-FUNC-0009",
                                         "associated key operation requires a local nominal type",
                                         view.span);
    }
    aggregate = r_semantic_aggregate_for_type(context, owner_type);
    static const char *const names[] = {
        "hash", "equal", "json_marshal", "json_unmarshal", "json_is_zero", "clone", "json_schema"};
    for (operation = 1U; operation <= 7U; ++operation)
        if (r_source_text_equal(r_get_source_const(context, symbol->name_span.source),
                                symbol->name_span,
                                names[operation - 1U]))
            break;
    if (operation > 7U) {
        /* R-FUNC-0013: any other associated name declares an inherent method. */
        return r_generic_prepare_method(context,
                                        parameter_list,
                                        parameters,
                                        symbol,
                                        owner_type,
                                        UINT32_C(0),
                                        UINT32_C(0),
                                        view.span);
    }
    bool json = (operation >= 3U && operation <= 5U) || operation == 7U;
    const char *diagnostic = json ? "R-DIAG-JSON-001" : "R-DIAG-TYPE-001";
    const char *rule = json ? "R-JSON-0001" : operation == 6U ? "R-OWN-0020" : "R-FUNC-0009";
    bool result_ok =
        (operation == 4U || operation == 6U) ? symbol->return_type == owner_type
        : (operation == 3U || operation == 7U)
            ? r_semantic_standard_type_name_equal(
                  context, r_semantic_type(context, symbol->return_type), "std.json::value")
            : r_semantic_value_kind(context, symbol->return_type) ==
                  (operation == 1U ? R_SEMANTIC_TYPE_U64 : R_SEMANTIC_TYPE_BOOL);
    bool effects_ok = symbol->throws_type == 0U;
    if (operation == 7U) {
        /* M32.6: the schema hook throws exactly the errors of std.json::schema. */
        bool json_error = false;
        bool alloc_error = false;
        effects_ok = r_semantic_effect_count(context, symbol->throws_type) == 2U;
        for (uint32_t i = 0U; i < r_semantic_effect_count(context, symbol->throws_type); ++i) {
            const RSemanticType *error =
                r_semantic_type(context, r_semantic_effect_at(context, symbol->throws_type, i));
            json_error = json_error || r_semantic_standard_type_name_equal(context, error, "std.json::error");
            alloc_error =
                alloc_error || r_semantic_standard_type_name_equal(context, error, "std.alloc::alloc_error");
        }
        effects_ok = effects_ok && json_error && alloc_error;
    }
    if (operation == 3U || operation == 4U || operation == 6U) {
        /* R-OWN-0020: a clone hook may report only allocation failure. */
        effects_ok = true;
        for (uint32_t i = 0U; i < r_semantic_effect_count(context, symbol->throws_type); ++i) {
            const RSemanticType *error =
                r_semantic_type(context, r_semantic_effect_at(context, symbol->throws_type, i));
            if ((operation == 6U ||
                 !r_semantic_standard_type_name_equal(context, error, "std.json::error")) &&
                !r_semantic_standard_type_name_equal(context, error, "std.alloc::alloc_error"))
                effects_ok = false;
        }
    }
    if (aggregate == NULL || aggregate->module_source != symbol->module_source ||
        !aggregate->complete || symbol->is_protected || symbol->is_unsafe || symbol->is_async ||
        symbol->is_extern_c || !symbol->has_definition || !effects_ok || !result_ok) {
        return r_semantic_add_diagnostic(context,
                                         diagnostic,
                                         rule,
                                         "associated operation requires its exact safe synchronous "
                                         "signature and defining module",
                                         view.span);
    }
    if ((aggregate->generic_schema == 0U) != (symbol->generic_schema == 0U) ||
        (aggregate->generic_schema != 0U &&
         (aggregate->generic_origin == 0U ||
          (!r_generic_hook_matches(context,
                                   aggregate->generic_schema,
                                   symbol->generic_schema,
                                   operation == 1U || operation == 2U || operation == 6U) ||
           !r_generic_hook_arguments_match(context, aggregate, symbol->generic_schema))))) {
        return r_semantic_add_diagnostic(
            context,
            "R-DIAG-TYPE-001",
            "R-FUNC-0009",
            "generic hooks shall match the type schema parameters and constraints",
            view.span);
    }
    symbol->associated_type = owner_type;
    symbol->associated_operation = operation;
    symbol->is_protected = aggregate->is_protected;
    if (aggregate->generic_origin != 0U) {
        aggregate = &context->semantic_aggregates[aggregate->generic_origin - 1U];
    }
    base = &context->intern_entries[aggregate->name_intern_id - 1U];
    success = r_generic_text(&name, base->bytes, base->length) && r_generic_text(&name, "::", 2U) &&
              r_generic_text(&name, names[operation - 1U], strlen(names[operation - 1U])) &&
              r_intern_bytes(context, name.bytes, name.length, &symbol->name_intern_id);
    r_context_free(context, name.bytes);
    return success;
}

static bool r_generic_attach_associated(RFrontendContext *context, RSymbolId id) {
    const RSemanticSymbol *symbol = &context->semantic_symbols[id - 1U];
    const RSemanticAggregate *target;
    uint32_t aggregate_id, index;
    RSymbolId *slot;
    if (symbol->is_method) {
        return r_semantic_register_method(context, id);
    }
    if (symbol->associated_operation == 0U) {
        return true;
    }
    target = r_semantic_aggregate_for_type(context, symbol->associated_type);
    if (target == NULL) {
        return false;
    }
    const bool json_hook = (symbol->associated_operation >= 3U && symbol->associated_operation <= 5U) ||
                           symbol->associated_operation == 7U;
    const char *hook_rule = json_hook                             ? "R-JSON-0001"
                            : symbol->associated_operation == 6U ? "R-OWN-0020"
                                                                  : "R-FUNC-0009";
    if (symbol->parameter_count != (symbol->associated_operation == 2U   ? 2U
                                    : symbol->associated_operation == 7U ? 0U
                                                                         : 1U)) {
        return r_semantic_add_diagnostic(context,
                                         json_hook ? "R-DIAG-JSON-001" : "R-DIAG-TYPE-001",
                                         hook_rule,
                                         "associated operation has an incorrect parameter count",
                                         symbol->name_span);
    }
    for (index = 0U; index < symbol->parameter_count; ++index) {
        const RSemanticType *parameter = r_semantic_type(
            context, context->semantic_parameter_types[symbol->first_parameter_type + index]);
        if (parameter == NULL || parameter->kind != R_SEMANTIC_TYPE_BORROW ||
            parameter->flags != R_SEMANTIC_TYPE_FLAG_SHARED ||
            (symbol->associated_operation == 4U
                 ? !r_semantic_standard_type_name_equal(
                       context, r_semantic_type(context, parameter->base), "std.json::value")
                 : parameter->base != symbol->associated_type)) {
            return r_semantic_add_diagnostic(
                context,
                json_hook ? "R-DIAG-JSON-001" : "R-DIAG-TYPE-001",
                hook_rule,
                "associated hook parameter must be a non-null shared borrow of the required type",
                symbol->name_span);
        }
    }
    aggregate_id = target->generic_origin != 0U
                       ? target->generic_origin
                       : (uint32_t)(target - context->semantic_aggregates) + 1U;
    RSemanticAggregate *owner = &context->semantic_aggregates[aggregate_id - 1U];
    slot = symbol->associated_operation == 1U   ? &owner->hash_function
           : symbol->associated_operation == 2U ? &owner->equal_function
           : symbol->associated_operation == 3U ? &owner->json_marshal_function
           : symbol->associated_operation == 4U ? &owner->json_unmarshal_function
           : symbol->associated_operation == 5U ? &owner->json_is_zero_function
           : symbol->associated_operation == 7U ? &owner->json_schema_function
                                                : &owner->clone_function;
    if (*slot != 0U) {
        return r_semantic_add_diagnostic(context,
                                         "R-DIAG-NAME-002",
                                         hook_rule,
                                         "duplicate associated operation",
                                         symbol->name_span);
    }
    *slot = id;
    return true;
}

static bool r_generic_finish_associated(RFrontendContext *context) {
    size_t index;
    for (index = 0U; index < context->semantic_aggregate_count; ++index) {
        const RSemanticAggregate *aggregate = &context->semantic_aggregates[index];
        if (aggregate->generic_origin != 0U) {
            if (!r_generic_bind_drop(context, (uint32_t)index + 1U)) {
                return false;
            }
        } else if ((aggregate->hash_function == 0U) != (aggregate->equal_function == 0U) &&
                   !r_semantic_add_diagnostic(
                       context,
                       "R-DIAG-TYPE-001",
                       "R-FUNC-0009",
                       "the associated hash/equal pair shall be defined together",
                       aggregate->name_span)) {
            return false;
        }
    }
    return true;
}

static bool r_generic_lower_argument(RBodyContext *body,
                                     RAstRef argument,
                                     RTypeId pattern_type,
                                     uint32_t access_flags,
                                     RExpressionResult *result) {
    RAstRef direct = argument;
    RAstNodeView view;
    uint32_t depth = 0U;
    bool address = false;
    const RSemanticType *pattern =
        r_semantic_type(body->frontend, r_semantic_value_type(body->frontend, pattern_type));
    if (pattern != NULL && pattern->kind == R_SEMANTIC_TYPE_NULL_T) {
        return r_body_lower_value(body, argument, pattern_type, result);
    }
    while (r_ast_ref_view(body->frontend, direct, &view) &&
           (view.kind == R_SYNTAX_EXPRESSION || view.kind == R_SYNTAX_INITIALIZER ||
            view.kind == R_SYNTAX_BINARY_EXPRESSION || view.kind == R_SYNTAX_POSTFIX_EXPRESSION)) {
        RAstRef child;
        if (!r_body_ast_single_node_child(body->frontend, direct, &child)) {
            break;
        }
        if (++depth > r_frontend_tree_depth_limit(body->frontend)) {
            return false;
        }
        direct = child;
    }
    if (!r_ast_ref_view(body->frontend, direct, &view)) {
        return false;
    }
    if (view.kind == R_SYNTAX_POSTFIX_EXPRESSION) {
        if (!r_body_lower_postfix_impl(body, direct, R_TYPE_ID_INVALID, true, &address, result)) {
            return false;
        }
    } else if (view.kind == R_SYNTAX_UNARY_EXPRESSION) {
        RAstTokenView operation;
        RAstRef operand;
        if (r_body_ast_first_token(body->frontend, direct, &operation) &&
            operation.kind == R_TOKEN_AMP &&
            r_body_ast_single_node_child(body->frontend, direct, &operand)) {
            address = true;
            if (!r_body_lower_expression(body, operand, R_TYPE_ID_INVALID, result)) {
                return false;
            }
        } else if (!r_body_lower_expression(body, direct, R_TYPE_ID_INVALID, result)) {
            return false;
        }
    } else if (!r_body_lower_expression(body, direct, R_TYPE_ID_INVALID, result)) {
        return false;
    }
    if (address && result->valid) {
        RExpressionResult place = *result;
        const RSemanticType *place_type =
            r_semantic_type(body->frontend, r_semantic_value_type(body->frontend, place.type));
        RTypeId borrow;
        if ((pattern != NULL) && (pattern->kind == R_SEMANTIC_TYPE_SLICE) && (place_type != NULL) &&
            (place_type->kind == R_SEMANTIC_TYPE_FIXED_ARRAY)) {
            /* R-TYPE-0036: `&fixed` for a slice parameter is the whole-array slice, formed
               under the slice type of the actual element (R-EXPR-0015). */
            RTypeId slice_type;
            return r_semantic_intern_derived_type(body->frontend,
                                                  R_SEMANTIC_TYPE_SLICE,
                                                  place_type->base,
                                                  R_TYPE_ID_INVALID,
                                                  UINT64_C(0),
                                                  pattern->flags,
                                                  &slice_type) &&
                   r_body_lower_expression(body, direct, slice_type, result);
        }
        if (!r_semantic_intern_derived_type(body->frontend,
                                            R_SEMANTIC_TYPE_BORROW,
                                            r_semantic_value_type(body->frontend, place.type),
                                            0U,
                                            0U,
                                            access_flags,
                                            &borrow) ||
            !r_body_form_safe_borrow(body, view.span, borrow, &place, result)) {
            return false;
        }
    }
    return true;
}

static bool r_generic_lower_key_call(RBodyContext *body,
                                     RAstRef arguments,
                                     const RAstNodeView *view,
                                     RStandardCallOperation operation,
                                     RSourceSpan span,
                                     RExpressionResult *result) {
    RHirVector children = {0};
    RTypeId key_type = 0U, result_type;
    RSymbolId function = 0U;
    uint32_t index;
    bool success = false;
    const uint32_t count = operation == R_STANDARD_CALL_CORE_HASH ? 1U : 2U;
    if (view->child_count != count) {
        return r_body_diagnostic(body,
                                 "R-DIAG-TYPE-001",
                                 "R-FUNC-0009",
                                 "core key operation has the wrong argument count",
                                 span);
    }
    for (index = 0U; index < count; ++index) {
        RAstRef argument;
        RExpressionResult value;
        const RSemanticType *type;
        if (!r_ast_ref_child(body->frontend, arguments, index, &argument) ||
            !r_generic_lower_argument(
                body, argument, R_TYPE_ID_INVALID, R_SEMANTIC_TYPE_FLAG_SHARED, &value) ||
            !r_body_place_to_value(body, &value)) {
            goto cleanup;
        }
        if (!value.valid) {
            success = true;
            goto cleanup;
        }
        type = r_semantic_type(body->frontend, value.type);
        if (type != NULL && type->kind == R_SEMANTIC_TYPE_BORROW &&
            type->flags == R_SEMANTIC_TYPE_FLAG_NONE) {
            RTypeId shared_type;
            if (!r_semantic_intern_derived_type(body->frontend,
                                                R_SEMANTIC_TYPE_BORROW,
                                                type->base,
                                                0U,
                                                0U,
                                                R_SEMANTIC_TYPE_FLAG_SHARED,
                                                &shared_type) ||
                !r_body_finish_value(body, shared_type, &value)) {
                goto cleanup;
            }
            type = r_semantic_type(body->frontend, value.type);
        }
        if (type == NULL || type->kind != R_SEMANTIC_TYPE_BORROW ||
            type->flags != R_SEMANTIC_TYPE_FLAG_SHARED || (index != 0U && type->base != key_type)) {
            success = r_body_diagnostic(
                body,
                "R-DIAG-TYPE-001",
                "R-FUNC-0009",
                "core key operations require shared borrows of one exact key type",
                span);
            goto cleanup;
        }
        key_type = type->base;
        if (!r_hir_vector_push(body->frontend, &children, value.node)) {
            goto cleanup;
        }
    }
    if (!r_generic_has_property(body->frontend, key_type, R_GENERIC_KEY, 0U)) {
        success = r_body_diagnostic(body,
                                    "R-DIAG-TYPE-001",
                                    "R-FUNC-0009",
                                    "key contract is not available or proven for this type",
                                    span);
        goto cleanup;
    }
    {
        const RSemanticAggregate *aggregate =
            r_semantic_aggregate_for_type(body->frontend, key_type);
        if (aggregate != NULL) {
            function = operation == R_STANDARD_CALL_CORE_HASH ? aggregate->hash_function
                                                              : aggregate->equal_function;
        }
    }
    if (!r_semantic_type_from_token(
            body->frontend, count == 1U ? R_TOKEN_KW_U64 : R_TOKEN_KW_BOOL, &result_type) ||
        !r_body_append_node(body,
                            function != 0U ? R_HIR_CALL : R_HIR_STANDARD_CALL,
                            span,
                            result_type,
                            function,
                            R_TOKEN_INVALID,
                            0U,
                            false,
                            &children,
                            &result->node)) {
        goto cleanup;
    }
    r_body_hir_node(body, result->node)->borrow_origin = 0U;
    if (function == 0U) {
        r_body_hir_node(body, result->node)->standard_operation = operation;
        r_body_hir_node(body, result->node)->runtime_type = key_type;
        r_body_hir_node(body, result->node)->auxiliary_type = key_type;
    }
    result->type = result_type;
    result->valid = true;
    success = true;
cleanup:
    r_context_free(body->frontend, children.items);
    return success;
}

static bool r_generic_hook_arguments_match(const RFrontendContext *context,
                                           const RSemanticAggregate *target,
                                           uint32_t hook_schema) {
    const RGenericSchema *hook;
    uint32_t index;
    if (target->generic_schema == 0U) {
        return hook_schema == 0U;
    }
    if (hook_schema == 0U || target->generic_origin == 0U) {
        return false;
    }
    hook = &context->generic_schemas[hook_schema - 1U];
    for (index = 0U; index < hook->parameter_count; ++index) {
        if (context->generic_arguments[target->first_generic_argument + index] !=
            context->generic_parameters[hook->first_parameter + index].type) {
            return false;
        }
    }
    return true;
}

/* The visible non-generic aggregate that `[module.path::]Owner::name` names as its owner, found
   without diagnostics; *ambiguous reports a second visible candidate. */
static const RSemanticAggregate *r_generic_visible_owner(RBodyContext *body,
                                                         RAstRef qualified,
                                                         uint32_t count,
                                                         const RAstTokenView *owner,
                                                         bool *ambiguous) {
    RFrontendContext *context = body->frontend;
    const RSemanticAggregate *match = NULL;
    size_t candidate_index;
    *ambiguous = false;
    for (candidate_index = 0U; candidate_index < context->semantic_aggregate_count;
         ++candidate_index) {
        const RSemanticAggregate *candidate = &context->semantic_aggregates[candidate_index];
        const RSource *source = r_get_source_const(context, candidate->module_source);
        size_t import_index;
        bool visible = false;
        if (candidate->generic_schema != 0U || candidate->name_intern_id != owner->intern_id ||
            source == NULL || source->module_name == NULL ||
            (count > 2U && !r_body_qualified_module_matches(
                               body, qualified, count - 2U, source->module_name))) {
            continue;
        }
        if (candidate->module_source == body->source) {
            visible = true;
        }
        for (import_index = 0U; !visible && import_index < context->import_count;
             ++import_index) {
            const RImportEntry *import = &context->imports[import_index];
            visible = import->source == body->source &&
                      strcmp(import->module_name, source->module_name) == 0 &&
                      (count == 2U
                           ? (import->imports_name &&
                              r_semantic_token_text_equal_owned(context, owner, import->name))
                           : !import->imports_name);
        }
        if (!visible) {
            continue;
        }
        if (match != NULL) {
            *ambiguous = true;
            return match;
        }
        match = candidate;
    }
    return match;
}

/* M24-5: the method that a qualified `[module.path::]Owner::method` names, found without
   diagnostics, or invalid when the name is not such a method. */
static RSymbolId r_generic_associated_method_quiet(RBodyContext *body, RAstRef qualified) {
    RFrontendContext *context = body->frontend;
    RAstNodeView view;
    RAstRef applied;
    RAstTokenView owner = {0}, name = {0};
    uint32_t index, count = 0U;
    bool ambiguous = false;
    if (!r_ast_view_kind(context, qualified, R_SYNTAX_QUALIFIED_NAME, &view) ||
        r_ast_find_direct_child(context, qualified, R_SYNTAX_TYPE, &applied)) {
        return R_SYMBOL_ID_INVALID;
    }
    for (index = 0U; index < view.child_count; ++index) {
        RAstRef child;
        RAstTokenView token;
        if (r_ast_ref_child(context, qualified, index, &child) &&
            r_ast_ref_token(context, child, &token) && token.kind == R_TOKEN_IDENTIFIER) {
            owner = name;
            name = token;
            ++count;
        }
    }
    if (count < 2U) {
        return R_SYMBOL_ID_INVALID;
    }
    const RSemanticAggregate *match =
        r_generic_visible_owner(body, qualified, count, &owner, &ambiguous);
    if (match == NULL || ambiguous ||
        (match->is_protected && match->module_source != body->source)) {
        return R_SYMBOL_ID_INVALID;
    }
    const RSemanticMethod *method =
        r_semantic_find_method(context, match->type, name.intern_id, &ambiguous);
    return (method == NULL || ambiguous) ? R_SYMBOL_ID_INVALID : method->function;
}

static bool r_generic_resolve_associated(RBodyContext *body,
                                         RAstRef qualified,
                                         RAstTokenView *name,
                                         RSymbolId *result) {
    RFrontendContext *context = body->frontend;
    RAstNodeView view;
    RAstRef applied;
    RAstTokenView owner = {0};
    uint32_t index, count = 0U;
    const RSemanticAggregate *match = NULL;
    *result = 0U;
    if (!r_ast_view_kind(context, qualified, R_SYNTAX_QUALIFIED_NAME, &view)) {
        return true;
    }
    for (index = 0U; index < view.child_count; ++index) {
        RAstRef child;
        RAstTokenView token;
        if (r_ast_ref_child(context, qualified, index, &child) &&
            r_ast_ref_token(context, child, &token) && token.kind == R_TOKEN_IDENTIFIER) {
            owner = *name;
            *name = token;
            ++count;
        }
    }
    const bool is_hook = r_semantic_token_text_equal_owned(context, name, "hash") ||
                         r_semantic_token_text_equal_owned(context, name, "equal") ||
                         r_semantic_token_text_equal_owned(context, name, "json_marshal") ||
                         r_semantic_token_text_equal_owned(context, name, "json_unmarshal") ||
                         r_semantic_token_text_equal_owned(context, name, "json_is_zero") ||
                         r_semantic_token_text_equal_owned(context, name, "json_schema") ||
                         r_semantic_token_text_equal_owned(context, name, "clone");
    body->associated_owner_type = R_TYPE_ID_INVALID;
    if (r_ast_find_direct_child(context, qualified, R_SYNTAX_TYPE, &applied)) {
        RTypeId type;
        if (!r_semantic_type_from_ast(context, applied, &type)) {
            return context->resource_status == R_FRONTEND_OK;
        }
        match = r_semantic_aggregate_for_type(context, type);
        if ((match != NULL) && (match->generic_origin != 0U)) {
            /* R-FUNC-0013: the applied owner supplies the schema prefix of its methods. */
            body->associated_owner_type = type;
        }
    } else if (count >= 2U) {
        bool ambiguous_owner = false;
        match = r_generic_visible_owner(body, qualified, count, &owner, &ambiguous_owner);
        if (ambiguous_owner) {
            return r_body_diagnostic(body,
                                     "R-DIAG-NAME-001",
                                     "R-FUNC-0009",
                                     "associated type name is ambiguous",
                                     view.span);
        }
    }
    if (match != NULL) {
        if (match->is_protected && match->module_source != body->source) {
            return r_body_diagnostic(
                body, "R-DIAG-NAME-001", "R-FUNC-0009", "associated type is protected", view.span);
        }
        if (!is_hook) {
            /* R-FUNC-0013: Type::name also spells a method or an associated function. */
            bool ambiguous = false;
            const RSemanticMethod *method =
                r_semantic_find_method(context, match->type, name->intern_id, &ambiguous);
            if (ambiguous) {
                return r_body_diagnostic(body,
                                         "R-DIAG-NAME-001",
                                         "R-NAME-0010",
                                         "method name is ambiguous in the namespace of its owner "
                                         "type",
                                         view.span);
            }
            if (method != NULL) {
                return r_body_resolve_method(body, match->type, name, view.span, result);
            }
            return true;
        }
        *result = r_semantic_token_text_equal_owned(context, name, "hash") ? match->hash_function
                  : r_semantic_token_text_equal_owned(context, name, "equal")
                      ? match->equal_function
                  : r_semantic_token_text_equal_owned(context, name, "json_marshal")
                      ? match->json_marshal_function
                  : r_semantic_token_text_equal_owned(context, name, "json_unmarshal")
                      ? match->json_unmarshal_function
                  : r_semantic_token_text_equal_owned(context, name, "json_is_zero")
                      ? match->json_is_zero_function
                  : r_semantic_token_text_equal_owned(context, name, "json_schema")
                      ? match->json_schema_function
                      : match->clone_function;
    }
    return true;
}

static bool r_generic_key_local_destination(const RFrontendContext *context,
                                            RHirNodeId id,
                                            RSymbolId function) {
    const RHirNode *node = &context->hir_nodes[id - 1U];
    if (node->kind == R_HIR_PLACE && node->symbol != 0U) {
        const RSemanticSymbol *symbol = &context->semantic_symbols[node->symbol - 1U];
        return symbol->kind == R_SEMANTIC_SYMBOL_LOCAL && !symbol->is_static &&
               symbol->parent == function;
    }
    return (node->kind == R_HIR_FIELD_PLACE || node->kind == R_HIR_INDEX_PLACE) &&
           node->child_count != 0U &&
           r_generic_key_local_destination(
               context, context->hir_children[node->first_child], function);
}

static bool r_generic_check_key_body(RFrontendContext *context,
                                     RHirNodeId id,
                                     RSymbolId function,
                                     uint32_t depth) {
    const RHirNode node = context->hir_nodes[id - 1U];
    uint32_t index;
    bool allowed = depth <= context->options.limits.max_nesting && node.effect_exit_count == 0U;
    switch (node.kind) {
    case R_HIR_CALL:
        allowed = allowed && node.symbol != 0U &&
                  context->semantic_symbols[node.symbol - 1U].associated_operation != 0U &&
                  context->semantic_symbols[node.symbol - 1U].associated_operation <= 2U;
        break;
    case R_HIR_STANDARD_CALL:
        allowed = allowed && (node.standard_operation == R_STANDARD_CALL_CORE_HASH ||
                              node.standard_operation == R_STANDARD_CALL_CORE_KEY_EQUAL);
        break;
    case R_HIR_NEW:
    case R_HIR_ASYNC_START:
    case R_HIR_AWAIT:
    case R_HIR_DROP:
    case R_HIR_THROW:
    case R_HIR_TRY:
    case R_HIR_CATCH:
    case R_HIR_FINALLY:
        allowed = false;
        break;
    case R_HIR_LOCAL:
        allowed = allowed &&
                  (node.symbol == 0U || !context->semantic_symbols[node.symbol - 1U].is_static);
        break;
    case R_HIR_PLACE:
        allowed =
            allowed &&
            (node.symbol == 0U || !context->semantic_symbols[node.symbol - 1U].is_static ||
             r_semantic_type_is_const(context, context->semantic_symbols[node.symbol - 1U].type));
        break;
    case R_HIR_ASSIGN:
    case R_HIR_COMPOUND_ASSIGN:
        allowed = allowed && node.child_count != 0U &&
                  r_generic_key_local_destination(
                      context, context->hir_children[node.first_child], function);
        break;
    case R_HIR_DEREF_PLACE: {
        const RSemanticType *pointer =
            node.symbol != 0U
                ? r_semantic_type(context, context->semantic_symbols[node.symbol - 1U].type)
                : NULL;
        allowed = allowed && pointer != NULL && pointer->kind == R_SEMANTIC_TYPE_BORROW &&
                  pointer->flags == R_SEMANTIC_TYPE_FLAG_SHARED;
        break;
    }
    default:
        break;
    }
    if (!allowed) {
        return r_semantic_add_diagnostic(
            context,
            "R-DIAG-TYPE-001",
            "R-FUNC-0009",
            "associated key body contains an effect outside its closed contract",
            node.span);
    }
    for (index = 0U; index < node.child_count; ++index) {
        if (!r_generic_check_key_body(
                context, context->hir_children[node.first_child + index], function, depth + 1U)) {
            return false;
        }
    }
    return true;
}

static bool r_generic_check_key_bodies(RFrontendContext *context) {
    size_t index;
    for (index = 0U; index < context->semantic_symbol_count; ++index) {
        const RSemanticSymbol *symbol = &context->semantic_symbols[index];
        if (symbol->kind == R_SEMANTIC_SYMBOL_FUNCTION && symbol->generic_origin == 0U &&
            symbol->associated_operation != 0U && symbol->associated_operation <= 2U &&
            symbol->hir_node != 0U &&
            !r_generic_check_key_body(context, symbol->hir_node, (RSymbolId)index + 1U, 0U)) {
            return false;
        }
    }
    return true;
}

static bool
r_generic_constant_initializer(const RFrontendContext *context, RHirNodeId id, uint32_t depth) {
    const RHirNode *node;
    uint32_t index;
    if (id == 0U || depth > r_frontend_tree_depth_limit(context)) {
        return false;
    }
    node = &context->hir_nodes[id - 1U];
    switch (node->kind) {
    case R_HIR_LITERAL:
    case R_HIR_ENUM_CONSTANT:
    case R_HIR_GENERIC_CONSTANT:
    case R_HIR_TYPE_QUERY:
    case R_HIR_STRING_LITERAL:
    case R_HIR_DEFAULT_VALUE:
    case R_HIR_AGGREGATE_INIT:
    case R_HIR_ARRAY_INIT:
    case R_HIR_FIELD_INIT:
    case R_HIR_UNARY:
    case R_HIR_BINARY:
    case R_HIR_CAST:
    case R_HIR_CONDITIONAL:
        break;
    case R_HIR_FUNCTION_ADDRESS:
        /* R-TYPE-0054 (L28): a function value is the constant number of its target. */
        if (r_semantic_value_kind(context, node->type) != R_SEMANTIC_TYPE_FUNCTION) {
            return false;
        }
        break;
    case R_HIR_VARIANT:
        /* L22.1, L22.3: a variant of a user enumeration, an error family or an option with a
           constant payload. */
        if ((r_semantic_tagged_enum(context, node->type) == NULL) &&
            (r_semantic_value_kind(context, node->type) != R_SEMANTIC_TYPE_OPTION)) {
            return false;
        }
        break;
    default:
        return false;
    }
    for (index = 0U; index < node->child_count; ++index) {
        if (!r_generic_constant_initializer(
                context, context->hir_children[node->first_child + index], depth + 1U)) {
            return false;
        }
    }
    return true;
}

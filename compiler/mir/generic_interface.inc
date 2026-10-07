/* Interface v6 carries checked schemas, traits, implementations, method inventories and
 * source dependencies, never serialized bodies. */
static bool r_mir_write_definition_digest(const RFrontendContext *context,
                                          RSourceId source_id,
                                          RAstNodeId declaration,
                                          RFrontendWriteFn writer,
                                          void *user_data) {
    const RAstNode *node;
    const RSource *source;
    uint8_t digest[32];
    char encoded[64];
    static const char hex[] = "0123456789abcdef";
    size_t index;
    source = r_get_source_const(context, source_id);
    if (source == NULL || declaration == 0U || declaration > source->ast_node_count) {
        return false;
    }
    node = &source->ast_nodes[declaration - 1U];
    if (source == NULL || node->span.end < node->span.start || node->span.end > source->length) {
        return false;
    }
    r_sha256_digest(source->bytes + node->span.start, node->span.end - node->span.start, digest);
    for (index = 0U; index < sizeof(digest); ++index) {
        encoded[index * 2U] = hex[digest[index] >> 4U];
        encoded[index * 2U + 1U] = hex[digest[index] & 15U];
    }
    return writer(user_data, encoded, sizeof(encoded));
}

static bool r_mir_write_trait_reference(const RFrontendContext *context,
                                        uint32_t trait_id,
                                        RFrontendWriteFn writer,
                                        void *user_data) {
    const RSemanticTrait *trait = &context->semantic_traits[trait_id - 1U];
    const RInternEntry *name = &context->intern_entries[trait->name_intern_id - 1U];
    const RSource *source = r_get_source_const(context, trait->module_source);
    const char *module = trait->is_core ? "core" : source == NULL ? NULL : source->module_name;
    if (module == NULL || !r_write_text(writer, user_data, "(name=") ||
        !r_write_escaped(writer, user_data, (const uint8_t *)name->bytes, name->length) ||
        !r_write_text(writer, user_data, " module=") ||
        !r_write_escaped(writer, user_data, (const uint8_t *)module, strlen(module)) ||
        !r_write_text(writer, user_data, " arguments=("))
        return false;
    if (trait->generic_origin != 0U) {
        const uint32_t count =
            context->generic_schemas[trait->type_parameter_schema - 1U].parameter_count;
        for (uint32_t index = 0U; index < count; ++index) {
            if ((index != 0U && !r_write_text(writer, user_data, " ")) ||
                !r_mir_write_type(context,
                                  context->generic_arguments[trait->first_generic_argument + index],
                                  writer,
                                  user_data))
                return false;
        }
    }
    return r_write_text(writer, user_data, "))");
}

static bool r_mir_write_opaque_type(const RFrontendContext *context,
                                    const RGenericParameter *parameter,
                                    RFrontendWriteFn writer,
                                    void *user_data) {
    static const char *const names[] = {"copy",
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
    const RSource *source = r_get_source_const(context, parameter->opaque_declaration.source);
    if (source == NULL || source->module_name == NULL ||
        !r_write_text(writer, user_data, "(opaque module=") ||
        !r_write_escaped(
            writer, user_data, (const uint8_t *)source->module_name, strlen(source->module_name)) ||
        !r_write_text(writer, user_data, " declaration_offset=") ||
        !r_write_uint32(writer, user_data, parameter->name_span.start) ||
        !r_write_text(writer, user_data, " constraints=("))
        return false;
    bool first = true;
    for (uint32_t index = 0U; index < sizeof(names) / sizeof(names[0]); ++index) {
        if ((parameter->constraints & (1U << index)) == 0U)
            continue;
        if ((!first && !r_write_text(writer, user_data, " ")) ||
            !r_write_text(writer, user_data, names[index]))
            return false;
        first = false;
    }
    for (uint32_t index = 0U; index < parameter->trait_constraint_count; ++index) {
        const uint32_t trait =
            context->generic_trait_constraints[parameter->first_trait_constraint + index];
        const RTypeId callable = context->semantic_traits[trait - 1U].callable_signature;
        if ((!first && !r_write_text(writer, user_data, " ")) ||
            !(callable != 0U ? r_mir_write_type(context, callable, writer, user_data)
                             : r_mir_write_trait_reference(context, trait, writer, user_data)))
            return false;
        first = false;
    }
    if (!r_write_text(writer, user_data, ") bindings=("))
        return false;
    first = true;
    for (size_t index = 0U; index < context->generic_parameter_count; ++index) {
        const RGenericParameter *binding = &context->generic_parameters[index];
        if (binding->projection_base != parameter->type || binding->projection_bound == 0U)
            continue;
        const RInternEntry *name = &context->intern_entries[binding->name_intern_id - 1U];
        if ((!first && !r_write_text(writer, user_data, " ")) ||
            !r_write_text(writer, user_data, "(trait=") ||
            !r_mir_write_trait_reference(context, binding->projection_trait, writer, user_data) ||
            !r_write_text(writer, user_data, " name=") ||
            !r_write_escaped(writer, user_data, (const uint8_t *)name->bytes, name->length) ||
            !r_write_text(writer, user_data, " type=") ||
            !r_mir_write_type(context, binding->projection_bound, writer, user_data) ||
            !r_write_text(writer, user_data, ")"))
            return false;
        first = false;
    }
    if (!r_write_text(writer, user_data, ") arguments=("))
        return false;
    if (parameter->opaque_origin != 0U) {
        const uint32_t count =
            context->generic_schemas[parameter->opaque_schema - 1U].parameter_count;
        for (uint32_t index = 0U; index < count; ++index)
            if ((index != 0U && !r_write_text(writer, user_data, " ")) ||
                !r_mir_write_type(
                    context,
                    context->generic_arguments[parameter->opaque_first_argument + index],
                    writer,
                    user_data))
                return false;
    }
    return r_write_text(writer, user_data, "))");
}

/* The parameters of a schema, each with its constraints, after `prefix` and up to `)`. */
static bool r_mir_write_generic_schema_entries(const RFrontendContext *context,
                                               uint32_t schema_id,
                                               const char *prefix,
                                               RFrontendWriteFn writer,
                                               void *user_data) {
    const RGenericSchema *schema = &context->generic_schemas[schema_id - 1U];
    static const char *const names[] = {"copy",
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
    uint32_t index, property;
    if (!r_write_text(writer, user_data, prefix)) {
        return false;
    }
    for (index = 0U; index < schema->parameter_count; ++index) {
        const RGenericParameter *declared =
            &context->generic_parameters[schema->first_parameter + index];
        /* R-TYPE-0053 (L18.4): interface 23 marks a pack and lists the constraints of each of
           its elements. */
        const bool pack = declared->is_pack;
        const RGenericParameter *parameter =
            (pack && (declared->pack_element != R_TYPE_ID_INVALID))
                ? &context
                       ->generic_parameters[r_semantic_type(context, declared->pack_element)->base -
                                            1U]
                : declared;
        const RInternEntry *name = &context->intern_entries[parameter->name_intern_id - 1U];
        bool first = true;
        if (parameter->constant_type != 0U) {
            if ((index != 0U && !r_write_text(writer, user_data, " ")) ||
                !r_write_text(writer, user_data, "(name=") ||
                !r_write_escaped(writer, user_data, (const uint8_t *)name->bytes, name->length) ||
                !r_write_text(writer, user_data, " constant_type=") ||
                !r_mir_write_type(context, parameter->constant_type, writer, user_data) ||
                !r_write_text(writer, user_data, ")"))
                return false;
            continue;
        }
        if ((index != 0U && !r_write_text(writer, user_data, " ")) ||
            !r_write_text(writer, user_data, "(name=") ||
            !r_write_escaped(writer, user_data, (const uint8_t *)name->bytes, name->length) ||
            (pack && !r_write_text(writer, user_data, " pack=true"))) {
            return false;
        }
        /* R-TYPE-0045 (L16.3): interface 22 lists the parameters of an associated type. */
        for (uint32_t associated = 0U; associated < context->semantic_associated_type_count;
             ++associated) {
            const RSemanticAssociatedType *record =
                &context->semantic_associated_types[associated];
            if ((record->parameter_type == parameter->type) && (record->parameter_schema != 0U) &&
                !r_mir_write_generic_schema_entries(
                    context, record->parameter_schema, " parameters=(", writer, user_data)) {
                return false;
            }
        }
        if (!r_write_text(writer, user_data, " constraints=(")) {
            return false;
        }
        for (property = 0U; property < sizeof(names) / sizeof(names[0]); ++property) {
            if ((parameter->constraints & (1U << property)) != 0U) {
                if ((!first && !r_write_text(writer, user_data, " ")) ||
                    !r_write_text(writer, user_data, names[property])) {
                    return false;
                }
                first = false;
            }
        }
        /* R-TYPE-0043: normalized trait constraints follow the closed vocabulary. */
        for (property = 0U; property < parameter->trait_constraint_count; ++property) {
            const RSemanticTrait *trait =
                &context->semantic_traits[context->generic_trait_constraints
                                              [parameter->first_trait_constraint + property] -
                                          1U];
            if ((!first && !r_write_text(writer, user_data, " ")) ||
                !(trait->callable_signature != R_TYPE_ID_INVALID
                      ? r_mir_write_type(context, trait->callable_signature, writer, user_data)
                      : r_mir_write_trait_reference(
                            context,
                            context->generic_trait_constraints[parameter->first_trait_constraint +
                                                               property],
                            writer,
                            user_data))) {
                return false;
            }
            first = false;
        }
        /* R-TYPE-0043 (L44): interface 34 writes `fields(Trait)` as `(fields Trait)`. */
        for (property = 0U; property < parameter->field_trait_count; ++property) {
            if ((!first && !r_write_text(writer, user_data, " ")) ||
                !r_write_text(writer, user_data, "(fields ") ||
                !r_mir_write_trait_reference(
                    context,
                    context->generic_trait_constraints[parameter->first_field_trait + property],
                    writer,
                    user_data) ||
                !r_write_text(writer, user_data, ")")) {
                return false;
            }
            first = false;
        }
        if (!r_write_text(writer, user_data, "))")) {
            return false;
        }
    }
    return r_write_text(writer, user_data, ")");
}

/* R-TYPE-0043 (M19): interface 30 writes each associated type constraint of a generic function
   as `(associated=P::Name constraints=(...))`. */
static bool r_mir_write_projection_constraints(const RFrontendContext *context,
                                               uint32_t schema_id,
                                               RFrontendWriteFn writer,
                                               void *user_data) {
    static const char *const names[] = {"copy",
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
    bool opened = false;
    for (size_t index = 0U; index < context->generic_projection_constraint_count; ++index) {
        const RGenericProjectionConstraint *constraint =
            &context->generic_projection_constraints[index];
        const RSemanticType *type = r_semantic_type(context, constraint->projection);
        if ((constraint->schema != schema_id) || (type == NULL) ||
            (type->kind != R_SEMANTIC_TYPE_PARAMETER) || (type->base == 0U)) {
            continue;
        }
        const RGenericParameter *projection = &context->generic_parameters[type->base - 1U];
        const RSemanticType *base = r_semantic_type(context, projection->projection_base);
        const RGenericParameter *holder = &context->generic_parameters[constraint->holder];
        if ((base == NULL) || (base->base == 0U)) {
            continue;
        }
        const RInternEntry *base_name =
            &context->intern_entries[context->generic_parameters[base->base - 1U].name_intern_id -
                                     1U];
        const RInternEntry *member = &context->intern_entries[projection->name_intern_id - 1U];
        bool first = true;
        if ((!opened && !r_write_text(writer, user_data, " associated_constraints=(")) ||
            (opened && !r_write_text(writer, user_data, " ")) ||
            !r_write_text(writer, user_data, "(associated=") ||
            !r_write_escaped(
                writer, user_data, (const uint8_t *)base_name->bytes, base_name->length) ||
            !r_write_text(writer, user_data, "::") ||
            !r_write_escaped(writer, user_data, (const uint8_t *)member->bytes, member->length) ||
            !r_write_text(writer, user_data, " constraints=(")) {
            return false;
        }
        opened = true;
        for (uint32_t property = 0U; property < sizeof(names) / sizeof(names[0]); ++property) {
            if ((holder->constraints & (1U << property)) != 0U) {
                if ((!first && !r_write_text(writer, user_data, " ")) ||
                    !r_write_text(writer, user_data, names[property])) {
                    return false;
                }
                first = false;
            }
        }
        for (uint32_t bound = 0U; bound < holder->trait_constraint_count; ++bound) {
            if ((!first && !r_write_text(writer, user_data, " ")) ||
                !r_mir_write_trait_reference(
                    context,
                    context->generic_trait_constraints[holder->first_trait_constraint + bound],
                    writer,
                    user_data)) {
                return false;
            }
            first = false;
        }
        if (!r_write_text(writer, user_data, "))")) {
            return false;
        }
    }
    return !opened || r_write_text(writer, user_data, ")");
}

static bool r_mir_write_generic_parameters(const RFrontendContext *context,
                                           uint32_t schema_id,
                                           RFrontendWriteFn writer,
                                           void *user_data) {
    return r_mir_write_generic_schema_entries(
               context, schema_id, " generic=(", writer, user_data) &&
           r_mir_write_projection_constraints(context, schema_id, writer, user_data) &&
           r_write_text(writer, user_data, " source_required=true");
}

static bool r_mir_write_hook(const RFrontendContext *context,
                             const char *label,
                             RSymbolId symbol_id,
                             RFrontendWriteFn writer,
                             void *user_data) {
    const RSemanticSymbol *symbol;
    if (symbol_id == 0U) {
        return true;
    }
    symbol = &context->semantic_symbols[symbol_id - 1U];
    return r_write_text(writer, user_data, " ") && r_write_text(writer, user_data, label) &&
           r_write_text(writer, user_data, "=(name=") &&
           r_mir_write_qualified_symbol(context, symbol, writer, user_data) &&
           r_write_text(writer, user_data, " definition_sha256=\"") &&
           r_mir_write_definition_digest(
               context, symbol->module_source, symbol->declaration_ast, writer, user_data) &&
           r_write_text(writer,
                        user_data,
                        symbol->associated_operation >= 3U   ? "\" effects=json-contract)"
                        : symbol->associated_operation != 0U ? "\" effects=key-contract)"
                                                             : "\")");
}

static bool r_mir_write_aggregate_schema(const RFrontendContext *context,
                                         const RSemanticAggregate *aggregate,
                                         RFrontendWriteFn writer,
                                         void *user_data) {
    if (!r_write_text(writer, user_data, " json_definition_sha256=\"") ||
        !r_mir_write_json_schema_sha256(context, aggregate, writer, user_data) ||
        !r_write_text(writer, user_data, "\""))
        return false;
    if (aggregate->generic_schema != 0U && aggregate->generic_origin == 0U &&
        (!r_mir_write_generic_parameters(context, aggregate->generic_schema, writer, user_data) ||
         !r_write_text(writer, user_data, " definition_sha256=\"") ||
         !r_mir_write_definition_digest(
             context, aggregate->module_source, aggregate->declaration_ast, writer, user_data) ||
         !r_write_text(writer, user_data, "\""))) {
        return false;
    }
    if (aggregate->generic_origin != 0U) {
        const RSemanticAggregate *origin =
            &context->semantic_aggregates[aggregate->generic_origin - 1U];
        uint32_t index,
            count = context->generic_schemas[aggregate->generic_schema - 1U].parameter_count;
        if (!r_write_text(writer, user_data, " origin=") ||
            !r_mir_write_qualified_aggregate(context, origin, writer, user_data) ||
            !r_write_text(writer, user_data, " arguments=(")) {
            return false;
        }
        for (index = 0U; index < count; ++index) {
            if ((index != 0U && !r_write_text(writer, user_data, " ")) ||
                !r_mir_write_type(
                    context,
                    context->generic_arguments[aggregate->first_generic_argument + index],
                    writer,
                    user_data)) {
                return false;
            }
        }
        if (!r_write_text(writer, user_data, ")")) {
            return false;
        }
    }
    return r_mir_write_hook(context, "drop", aggregate->drop_function, writer, user_data) &&
           r_mir_write_hook(context, "hash", aggregate->hash_function, writer, user_data) &&
           r_mir_write_hook(context, "equal", aggregate->equal_function, writer, user_data) &&
           r_mir_write_hook(
               context, "json_marshal", aggregate->json_marshal_function, writer, user_data) &&
           r_mir_write_hook(
               context, "json_unmarshal", aggregate->json_unmarshal_function, writer, user_data) &&
           r_mir_write_hook(
               context, "json_is_zero", aggregate->json_is_zero_function, writer, user_data) &&
           r_mir_write_hook(
               context, "json_schema", aggregate->json_schema_function, writer, user_data) &&
           r_mir_write_hook(context, "clone", aggregate->clone_function, writer, user_data);
}

/* R-AGG-0012 (L27): ` derived=(clone equal ordered key format)` for the capabilities `@derive`
   lists. */
static bool r_mir_write_derived(uint32_t derived, RFrontendWriteFn writer, void *user_data) {
    static const char *const names[5] = {"clone", "equal", "ordered", "key", "format"};
    bool first = true;
    if (derived == 0U) {
        return true;
    }
    if (!r_write_text(writer, user_data, " derived=(")) {
        return false;
    }
    for (uint32_t bit = 0U; bit < 5U; ++bit) {
        if ((derived & (1U << bit)) == 0U) {
            continue;
        }
        if ((!first && !r_write_text(writer, user_data, " ")) ||
            !r_write_text(writer, user_data, names[bit])) {
            return false;
        }
        first = false;
    }
    return r_write_text(writer, user_data, ")");
}

/* R-AGG-0013 (L42): ` attribute_targets=(type field variant)` of an attribute type. */
static bool r_mir_write_attribute_targets(uint32_t targets, RFrontendWriteFn writer, void *user_data) {
    static const char *const names[3] = {"type", "field", "variant"};
    bool first = true;
    if (targets == 0U) {
        return true;
    }
    if (!r_write_text(writer, user_data, " attribute_targets=(")) {
        return false;
    }
    for (uint32_t bit = 0U; bit < 3U; ++bit) {
        if ((targets & (1U << bit)) == 0U) {
            continue;
        }
        if ((!first && !r_write_text(writer, user_data, " ")) ||
            !r_write_text(writer, user_data, names[bit])) {
            return false;
        }
        first = false;
    }
    return r_write_text(writer, user_data, ")");
}

/* ` attributes=((type=T values=(v ...)) ...)`: the attributes of the program on one type (member
   0), field or enumerator; a str value is quoted, every other value is its bits. */
static bool r_mir_write_attributes(const RFrontendContext *context,
                                   uint32_t target,
                                   RTypeId owner,
                                   uint32_t member,
                                   RFrontendWriteFn writer,
                                   void *user_data) {
    bool first = true;
    for (size_t index = 0U; index < context->attribute_use_count; ++index) {
        const RSemanticAttributeUse *use = &context->attribute_uses[index];
        const RSemanticType *type = r_semantic_type(context, use->attribute_type);
        const RSemanticAggregate *attribute;
        if (!use->valid || (use->target != target) || (use->owner != owner) ||
            (use->member != member)) {
            continue;
        }
        attribute = ((type != NULL) && (type->kind == R_SEMANTIC_TYPE_STRUCT) && (type->base != 0U) &&
                     ((size_t)type->base <= context->semantic_aggregate_count))
                        ? &context->semantic_aggregates[(size_t)type->base - 1U]
                        : NULL;
        if ((attribute == NULL) ||
            !r_write_text(writer, user_data, first ? " attributes=((type=" : " (type=") ||
            !r_mir_write_type(context, use->attribute_type, writer, user_data) ||
            !r_write_text(writer, user_data, " values=(")) {
            return false;
        }
        for (uint32_t field = 0U; field < attribute->field_count; ++field) {
            const RSemanticAttributeValue *value =
                &context->attribute_values[(size_t)use->first_value + field];
            const RSemanticType *value_type = r_semantic_type(context, value->type);
            if ((field != 0U) && !r_write_text(writer, user_data, " ")) {
                return false;
            }
            if (value->text_intern_id != 0U) {
                const RInternEntry *text = &context->intern_entries[value->text_intern_id - 1U];
                if (!r_write_escaped(
                        writer, user_data, (const uint8_t *)text->bytes, text->length)) {
                    return false;
                }
            } else if (((value_type != NULL) && (value_type->kind == R_SEMANTIC_TYPE_STR))
                           ? !r_write_text(writer, user_data, "\"\"")
                           : !r_mir_write_uint64(writer, user_data, value->bits)) {
                return false;
            }
        }
        if (!r_write_text(writer, user_data, "))")) {
            return false;
        }
        first = false;
    }
    return first || r_write_text(writer, user_data, ")");
}

static bool r_mir_write_payload_schema(const RFrontendContext *context,
                                       const RSemanticVariant *variant,
                                       RFrontendWriteFn writer,
                                       void *user_data) {
    uint32_t index;
    const RSemanticAggregate *payload;
    const RSemanticType *type;
    if (variant->payload_type == 0U) {
        return true;
    }
    if (!variant->structured_payload) {
        return r_write_text(writer, user_data, " payload=") &&
               r_mir_write_type(context, variant->payload_type, writer, user_data);
    }
    type = r_semantic_type(context, variant->payload_type);
    if (type == NULL || type->kind != R_SEMANTIC_TYPE_STRUCT) {
        return false;
    }
    payload = &context->semantic_aggregates[type->base - 1U];
    if (!r_write_text(writer, user_data, " fields=(")) {
        return false;
    }
    for (index = 0U; index < payload->field_count; ++index) {
        const RSemanticField *field = &context->semantic_fields[payload->first_field + index];
        const RInternEntry *name = &context->intern_entries[field->name_intern_id - 1U];
        if ((index != 0U && !r_write_text(writer, user_data, " ")) ||
            !r_write_text(writer, user_data, "(name=") ||
            !r_write_escaped(writer, user_data, (const uint8_t *)name->bytes, name->length) ||
            !r_write_text(writer, user_data, " type=") ||
            !r_mir_write_type(context, field->type, writer, user_data) ||
            !r_mir_write_json_field(context, field, writer, user_data) ||
            /* R-INIT-0004 (L33): a construction of the variant may omit the field. */
            ((field->initializer.node != 0U) &&
             !r_write_text(writer, user_data, " initializer=true")) ||
            !r_write_text(writer, user_data, ")")) {
            return false;
        }
    }
    return r_write_text(writer, user_data, ")");
}

static bool r_mir_write_generic_functions(const RFrontendContext *context,
                                          RFrontendWriteFn writer,
                                          void *user_data) {
    RSymbolId previous = 0U;
    size_t iteration;
    for (iteration = 0U; iteration < context->semantic_symbol_count; ++iteration) {
        RSymbolId next = 0U;
        size_t index;
        const RSemanticSymbol *symbol;
        uint32_t parameter;
        for (index = 0U; index < context->semantic_symbol_count; ++index) {
            const RSemanticSymbol *candidate = &context->semantic_symbols[index];
            RSymbolId id = (RSymbolId)index + 1U;
            if (candidate->kind != R_SEMANTIC_SYMBOL_FUNCTION || candidate->generic_origin != 0U ||
                candidate->generic_schema == 0U || candidate->is_protected ||
                candidate->is_trait_prototype ||
                (previous != 0U && r_mir_compare_symbols(context, id, previous) <= 0) ||
                (next != 0U && r_mir_compare_symbols(context, id, next) >= 0)) {
                continue;
            }
            next = id;
        }
        if (next == 0U) {
            break;
        }
        symbol = &context->semantic_symbols[next - 1U];
        if (!r_write_text(writer, user_data, "  (function-schema name=") ||
            !r_mir_write_qualified_symbol(context, symbol, writer, user_data) ||
            !r_mir_write_overload(context, symbol, writer, user_data) ||
            !r_mir_write_generic_parameters(context, symbol->generic_schema, writer, user_data) ||
            !r_write_text(writer, user_data, " return=") ||
            !r_mir_write_type(context,
                              symbol->opaque_result_contract != 0U ? symbol->opaque_result_contract
                                                                   : symbol->return_type,
                              writer,
                              user_data) ||
            !r_write_text(writer, user_data, " parameters=(")) {
            return false;
        }
        for (parameter = 0U; parameter < symbol->parameter_count; ++parameter) {
            if ((parameter != 0U && !r_write_text(writer, user_data, " ")) ||
                !r_mir_write_type(
                    context,
                    context->semantic_parameter_types[symbol->first_parameter_type + parameter],
                    writer,
                    user_data)) {
                return false;
            }
        }
        if (!r_write_text(writer, user_data, ") throws=") ||
            !(symbol->throws_type == 0U
                  ? r_write_text(writer, user_data, "(effects)")
                  : r_mir_write_type(context, symbol->throws_type, writer, user_data)) ||
            !r_write_text(writer, user_data, " async=") ||
            !r_mir_write_bool(writer, user_data, symbol->is_async) ||
            (symbol->is_scoped && !r_write_text(writer, user_data, " scoped=true")) ||
            !r_write_text(writer, user_data, " unsafe=") ||
            !r_mir_write_bool(writer, user_data, symbol->is_unsafe) ||
            !r_write_text(writer, user_data, " noalloc=") ||
            !r_mir_write_bool(writer, user_data, symbol->is_noalloc) ||
            (symbol->is_must_use && !r_write_text(writer, user_data, " must_use=true")) ||
            (symbol->is_discardable && !r_write_text(writer, user_data, " discardable=true")) ||
            (symbol->is_lending && !r_write_text(writer, user_data, " lending=true")) ||
            (symbol->is_chain && !r_write_text(writer, user_data, " chain=true")) ||
            !r_write_text(writer, user_data, " nonblocking=") ||
            !r_mir_write_bool(writer, user_data, symbol->is_nonblocking) ||
            !r_mir_write_borrow_contract(context, next, writer, user_data) ||
            !r_write_text(writer, user_data, " definition_sha256=\"") ||
            !r_mir_write_definition_digest(
                context, symbol->module_source, symbol->declaration_ast, writer, user_data) ||
            !r_write_text(writer, user_data, "\")\n")) {
            return false;
        }
        previous = next;
    }
    return true;
}

static bool r_mir_write_source_dependencies(const RFrontendContext *context,
                                            RFrontendWriteFn writer,
                                            void *user_data) {
    const char *previous = NULL;
    size_t iteration;
    /* Generic instances and translation-time values (R-EXPR-0032) depend on source bodies. */
    if ((context->generic_schema_count == 0U) && (context->consteval_evaluations == 0U)) {
        return true;
    }
    for (iteration = 0U; iteration < context->source_count; ++iteration) {
        const RSource *next = NULL;
        size_t index;
        for (index = 0U; index < context->source_count; ++index) {
            const RSource *source = &context->sources[index];
            if (source->module_name != NULL &&
                (previous == NULL || strcmp(source->module_name, previous) > 0) &&
                (next == NULL || strcmp(source->module_name, next->module_name) < 0)) {
                next = source;
            }
        }
        if (next == NULL) {
            break;
        }
        if (!r_write_text(writer, user_data, "  (source-dependency module=") ||
            !r_write_escaped(
                writer, user_data, (const uint8_t *)next->module_name, strlen(next->module_name)) ||
            !r_write_text(writer, user_data, " definition_sha256=\"") ||
            !r_mir_write_definition_digest(context,
                                           (RSourceId)(next - context->sources) + 1U,
                                           next->ast_root,
                                           writer,
                                           user_data) ||
            !r_write_text(writer, user_data, "\")\n")) {
            return false;
        }
        previous = next->module_name;
    }
    return true;
}

static bool r_mir_write_trait_contracts(const RFrontendContext *context,
                                        uint32_t trait_id,
                                        RFrontendWriteFn writer,
                                        void *user_data) {
    const RSemanticTrait *trait = &context->semantic_traits[trait_id - 1U];
    const RSource *source = r_get_source_const(context, trait->module_source);
    if (source == NULL || source->module_name == NULL ||
        !r_write_text(writer, user_data, " module=") ||
        !r_write_escaped(
            writer, user_data, (const uint8_t *)source->module_name, strlen(source->module_name)) ||
        !r_mir_write_generic_parameters(context, trait->generic_schema, writer, user_data) ||
        !r_write_text(writer, user_data, " method_contracts=("))
        return false;
    bool first = true;
    for (size_t index = 0U; index < context->semantic_method_count; ++index) {
        const RSemanticMethod *method = &context->semantic_methods[index];
        if (method->trait != trait_id || method->impl != 0U)
            continue;
        const RSemanticSymbol *function = &context->semantic_symbols[method->function - 1U];
        const RInternEntry *name = &context->intern_entries[method->name_intern_id - 1U];
        if ((!first && !r_write_text(writer, user_data, " ")) ||
            !r_write_text(writer, user_data, "(name=") ||
            !r_write_escaped(writer, user_data, (const uint8_t *)name->bytes, name->length) ||
            !r_write_text(writer, user_data, " parameters=("))
            return false;
        for (uint32_t parameter = 0U; parameter < function->parameter_count; ++parameter) {
            if ((parameter != 0U && !r_write_text(writer, user_data, " ")) ||
                !r_mir_write_type(
                    context,
                    context->semantic_parameter_types[function->first_parameter_type + parameter],
                    writer,
                    user_data))
                return false;
        }
        if (!r_write_text(writer, user_data, ") return=") ||
            !r_mir_write_type(context, function->return_type, writer, user_data) ||
            !r_write_text(writer, user_data, " throws=") ||
            !r_mir_write_type(context, function->throws_type, writer, user_data) ||
            !r_write_text(
                writer, user_data, function->has_definition ? " default=true" : " default=false") ||
            !r_write_text(writer, user_data, function->is_async ? " async=true" : " async=false") ||
            (function->is_scoped && !r_write_text(writer, user_data, " scoped=true")) ||
            !r_write_text(
                writer, user_data, function->is_unsafe ? " unsafe=true" : " unsafe=false") ||
            !r_write_text(
                writer, user_data, function->is_noalloc ? " noalloc=true" : " noalloc=false") ||
            !r_write_text(writer,
                          user_data,
                          function->is_nonblocking ? " nonblocking=true" : " nonblocking=false") ||
            (function->is_discardable && !r_write_text(writer, user_data, " discardable=true")) ||
            (function->is_lending && !r_write_text(writer, user_data, " lending=true")) ||
            (function->is_chain && !r_write_text(writer, user_data, " chain=true")) ||
            !r_write_text(
                writer, user_data, function->is_must_use ? " must_use=true)" : " must_use=false)"))
            return false;
        first = false;
    }
    return r_write_text(writer, user_data, ")");
}

/* Interface 16 retains complete static method contracts and their source fingerprints. */
static bool r_mir_write_trait_records(const RFrontendContext *context,
                                      RFrontendWriteFn writer,
                                      void *user_data) {
    size_t index;
    for (index = 0U; index < context->semantic_trait_count; ++index) {
        const RSemanticTrait *trait = &context->semantic_traits[index];
        const RInternEntry *name = &context->intern_entries[trait->name_intern_id - 1U];
        size_t method_index;
        uint32_t associated_index;
        bool first = true;
        /* R-TYPE-0046: core traits belong to the language, not to a module interface. */
        if (trait->generic_origin != 0U || trait->is_protected || trait->is_core ||
            trait->callable_signature != R_TYPE_ID_INVALID) {
            continue;
        }
        if (!r_mir_write_indent(writer, user_data, UINT32_C(1)) ||
            !r_write_text(writer, user_data, "(trait name=") ||
            !r_write_escaped(writer, user_data, (const uint8_t *)name->bytes, name->length) ||
            !r_write_text(writer, user_data, " associated=(")) {
            return false;
        }
        for (associated_index = 0U; associated_index < trait->associated_count;
             ++associated_index) {
            const RInternEntry *associated_name =
                &context->intern_entries
                     [context->semantic_associated_types[trait->first_associated + associated_index]
                          .name_intern_id -
                      1U];
            if (((associated_index != 0U) && !r_write_text(writer, user_data, " ")) ||
                !r_write_escaped(writer,
                                 user_data,
                                 (const uint8_t *)associated_name->bytes,
                                 associated_name->length)) {
                return false;
            }
        }
        /* R-TYPE-0050: interface 18 lists the associated constants with their types. */
        if (!r_write_text(writer, user_data, ") constants=(")) {
            return false;
        }
        for (associated_index = 0U; associated_index < trait->constant_count; ++associated_index) {
            const RSemanticAssociatedConstant *constant =
                &context->semantic_associated_constants[trait->first_constant + associated_index];
            const RInternEntry *constant_name =
                &context->intern_entries[constant->name_intern_id - 1U];
            if (((associated_index != 0U) && !r_write_text(writer, user_data, " ")) ||
                !r_write_text(writer, user_data, "(name=") ||
                !r_write_escaped(writer,
                                 user_data,
                                 (const uint8_t *)constant_name->bytes,
                                 constant_name->length) ||
                !r_write_text(writer, user_data, " type=") ||
                !r_mir_write_type(context, constant->type, writer, user_data) ||
                !r_write_text(writer,
                              user_data,
                              constant->initializer.node != R_AST_NODE_ID_INVALID
                                  ? " default=true)"
                                  : " default=false)")) {
                return false;
            }
        }
        if (!r_write_text(writer, user_data, ") methods=(")) {
            return false;
        }
        for (method_index = 0U; method_index < context->semantic_method_count; ++method_index) {
            const RSemanticMethod *method = &context->semantic_methods[method_index];
            const RInternEntry *method_name;
            if ((method->trait != (uint32_t)index + 1U) || (method->impl != UINT32_C(0))) {
                continue;
            }
            method_name = &context->intern_entries[method->name_intern_id - 1U];
            if ((!first && !r_write_text(writer, user_data, " ")) ||
                !r_write_escaped(
                    writer, user_data, (const uint8_t *)method_name->bytes, method_name->length)) {
                return false;
            }
            first = false;
        }
        if (!r_write_text(writer, user_data, ")") ||
            !r_mir_write_trait_contracts(context, (uint32_t)index + 1U, writer, user_data) ||
            !r_write_text(writer, user_data, " definition_sha256=\"") ||
            !r_mir_write_definition_digest(
                context,
                trait->module_source,
                trait->type_parameter_schema == 0U
                    ? trait->declaration_ast
                    : context->generic_schemas[trait->type_parameter_schema - 1U].external_ast,
                writer,
                user_data) ||
            !r_write_text(writer, user_data, "\")\n")) {
            return false;
        }
    }
    return true;
}

static bool r_mir_write_impl_records(const RFrontendContext *context,
                                     RFrontendWriteFn writer,
                                     void *user_data) {
    size_t index;
    for (index = 0U; index < context->semantic_impl_count; ++index) {
        const RSemanticImpl *impl = &context->semantic_impls[index];
        const RSemanticTrait *trait;
        if (impl->poisoned || (impl->trait == UINT32_C(0))) {
            continue;
        }
        trait = &context->semantic_traits[impl->trait - 1U];
        if (trait->callable_signature != R_TYPE_ID_INVALID)
            continue;
        if (!r_mir_write_indent(writer, user_data, UINT32_C(1)) ||
            !r_write_text(writer, user_data, "(impl trait=") ||
            !r_mir_write_trait_reference(context, impl->trait, writer, user_data) ||
            !r_write_text(writer, user_data, " target=") ||
            !r_mir_write_type(context, impl->target_type, writer, user_data) ||
            !r_write_text(writer, user_data, " bindings=(")) {
            return false;
        }
        {
            uint32_t binding_index;
            for (binding_index = 0U; binding_index < trait->associated_count; ++binding_index) {
                if (((binding_index != 0U) && !r_write_text(writer, user_data, " ")) ||
                    !r_mir_write_type(
                        context,
                        context->semantic_associated_bindings[impl->first_binding + binding_index],
                        writer,
                        user_data)) {
                    return false;
                }
            }
        }
        if (!r_write_text(writer, user_data, ") definition_sha256=\"") ||
            !r_mir_write_definition_digest(
                context, impl->module_source, impl->declaration_ast, writer, user_data) ||
            !r_write_text(writer, user_data, "\")\n")) {
            return false;
        }
    }
    return true;
}

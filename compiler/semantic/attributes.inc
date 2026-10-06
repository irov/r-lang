/* R-AGG-0013 (L42): attributes declared by the program.
 *
 * A struct marked `@attribute(targets)` is an attribute type. The parser writes the name of any
 * attribute that is not built in as a type; the collectors of aggregate headers, struct fields
 * and enumerators record each such use with the item it marks, and after the members of every
 * type are known r_attributes_resolve resolves the name, checks the arguments against the fields
 * of the attribute type and keeps them as translation-time constants for R-REFL-0005. A use that
 * no collector recorded stands where no attribute of the program is admitted and is reported
 * then. Nothing of an attribute reaches the run time but the values a reflection form selects. */

static const char *const r_attribute_builtin_names[] = {
    "repr",        "safety",     "link",        "header",   "abi",   "link_name",        "fenv",
    "c_type",      "c_constant", "export_name", "callback", "json",  "deny_panic_alloc", "noalloc",
    "nonblocking", "must_use",   "discardable", "lending",  "chain", "scoped",           "derive",
    "test",        "recursion",  "attribute",   "default",
};

static bool r_semantic_attribute_is_user(const RFrontendContext *context, RAstRef attribute) {
    RAstRef name;
    RAstNodeView view;

    return r_ast_ref_child(context, attribute, 1U, &name) && r_ast_ref_view(context, name, &view) &&
           (view.kind == R_SYNTAX_TYPE);
}

static bool r_attribute_record_use(
    RFrontendContext *context, RAstRef attribute, uint32_t target, RTypeId owner, uint32_t member) {
    RSemanticAttributeUse use;

    if (!r_grow_array(context,
                      (void **)&context->attribute_uses,
                      &context->attribute_use_capacity,
                      sizeof(*context->attribute_uses),
                      context->attribute_use_count + 1U)) {
        return false;
    }
    (void)memset(&use, 0, sizeof(use));
    use.attribute = attribute;
    use.target = target;
    use.owner = owner;
    use.member = member;
    use.attribute_type = R_TYPE_ID_INVALID;
    context->attribute_uses[context->attribute_use_count] = use;
    context->attribute_use_count += 1U;
    return true;
}

static bool r_attribute_diagnostic(RFrontendContext *context,
                                   const char *code,
                                   const char *message,
                                   RSourceSpan span) {
    return r_semantic_add_diagnostic(context, code, "R-AGG-0013", message, span);
}

/* `@attribute(type, field, variant)` before a struct declaration. */
static bool r_attribute_declared_targets(RFrontendContext *context,
                                         RAstRef attribute,
                                         const RAstNodeView *view,
                                         RSemanticAggregateKind kind,
                                         bool in_extern_block,
                                         RSemanticAggregate *aggregate) {
    uint32_t targets = UINT32_C(0);
    bool valid = (aggregate->attribute_targets == 0U) && (kind == R_SEMANTIC_AGGREGATE_STRUCT) &&
                 !aggregate->is_error && !in_extern_block && (aggregate->generic_schema == 0U) &&
                 (view->child_count >= 3U);

    for (uint32_t index = 2U; valid && (index < view->child_count); ++index) {
        RAstRef part;
        RAstTokenView token;
        RAstNodeView part_view;
        uint32_t target = UINT32_C(0);

        if (!r_ast_ref_child(context, attribute, index, &part)) {
            return false;
        }
        if (r_ast_ref_token(context, part, &token)) {
            if (token.kind == R_TOKEN_KW_VARIANT) {
                valid = (targets & R_ATTRIBUTE_TARGET_VARIANT) == 0U;
                targets |= R_ATTRIBUTE_TARGET_VARIANT;
                continue;
            }
            valid = (token.kind == R_TOKEN_LPAREN) || (token.kind == R_TOKEN_RPAREN) ||
                    (token.kind == R_TOKEN_COMMA);
            continue;
        }
        if (!r_ast_ref_view(context, part, &part_view)) {
            return false;
        }
        valid = (part_view.kind == R_SYNTAX_TYPE) && (part_view.child_count == 1U) &&
                r_ast_direct_token(context, part, 0U, NULL, &token) &&
                (token.kind == R_TOKEN_IDENTIFIER);
        if (valid) {
            target = r_semantic_token_text_equal_owned(context, &token, "type")
                         ? R_ATTRIBUTE_TARGET_TYPE
                     : r_semantic_token_text_equal_owned(context, &token, "field")
                         ? R_ATTRIBUTE_TARGET_FIELD
                     : r_semantic_token_text_equal_owned(context, &token, "variant")
                         ? R_ATTRIBUTE_TARGET_VARIANT
                         : UINT32_C(0);
            valid = (target != 0U) && ((targets & target) == 0U);
            targets |= target;
        }
    }
    valid = valid && (targets != 0U);
    if (valid) {
        const char *bytes;
        size_t length;

        if (!r_semantic_intern_text(context, aggregate->name_intern_id, &bytes, &length)) {
            context->resource_status = R_FRONTEND_INTERNAL_ERROR;
            return false;
        }
        for (size_t index = 0U; valid && (index < sizeof(r_attribute_builtin_names) /
                                                      sizeof(r_attribute_builtin_names[0]));
             ++index) {
            valid = (strlen(r_attribute_builtin_names[index]) != length) ||
                    (memcmp(r_attribute_builtin_names[index], bytes, length) != 0);
        }
    }
    if (!valid) {
        aggregate->poisoned = true;
        return r_attribute_diagnostic(
            context,
            "R-DIAG-SYN-002",
            "@attribute marks a non-generic struct, not named like a "
            "built-in attribute, with the targets type, field or variant, "
            "each once",
            view->span);
    }
    aggregate->attribute_targets = targets;
    return true;
}

/* An attribute type may hold only constants of these types (R-AGG-0013). */
static bool r_attribute_field_type_admitted(RFrontendContext *context, RTypeId type) {
    const RSemanticTypeKind kind = r_semantic_value_kind(context, type);
    const RSemanticAggregate *aggregate;

    if (r_semantic_type_is_integer(context, type) || (kind == R_SEMANTIC_TYPE_BOOL) ||
        (kind == R_SEMANTIC_TYPE_CHAR) || (kind == R_SEMANTIC_TYPE_F32) ||
        (kind == R_SEMANTIC_TYPE_F64) || (kind == R_SEMANTIC_TYPE_STR)) {
        return true;
    }
    aggregate = r_semantic_aggregate_for_type(context, r_semantic_value_type(context, type));
    return (aggregate != NULL) && (aggregate->kind == R_SEMANTIC_AGGREGATE_ENUM) &&
           !aggregate->is_tagged && !aggregate->is_error;
}

/* The constant of one argument or field initializer: a literal token, possibly after a `-`
   token, or an enumerator written as a type or a qualified name. */
typedef struct RAttributeConstant {
    RAstRef node;
    RAstTokenView token;
    bool has_token;
    bool negative;
    RSourceSpan span;
} RAttributeConstant;

static bool r_attribute_last_identifiers(const RFrontendContext *context,
                                         RAstRef node,
                                         RAstTokenView *qualifier,
                                         RAstTokenView *name,
                                         uint32_t *count) {
    RAstNodeView view;

    if (!r_ast_ref_view(context, node, &view)) {
        return false;
    }
    for (uint32_t index = 0U; index < view.child_count; ++index) {
        RAstRef child;
        RAstTokenView token;

        if (!r_ast_ref_child(context, node, index, &child)) {
            return false;
        }
        if (r_ast_ref_token(context, child, &token)) {
            if (token.kind == R_TOKEN_IDENTIFIER) {
                *qualifier = *name;
                *name = token;
                *count += 1U;
            }
        } else if (!r_attribute_last_identifiers(context, child, qualifier, name, count)) {
            return false;
        }
    }
    return true;
}

static bool r_attribute_constant_value(RFrontendContext *context,
                                       const RAttributeConstant *constant,
                                       RTypeId field_type,
                                       RSemanticAttributeValue *value,
                                       bool *valid) {
    const RSemanticTypeKind kind = r_semantic_value_kind(context, field_type);
    const RAstTokenView *token = &constant->token;

    *valid = false;
    (void)memset(value, 0, sizeof(*value));
    value->type = r_semantic_value_type(context, field_type);
    if (!constant->has_token) {
        /* An enumerator `Enum::name` of the fieldless enum of the field. */
        const RSemanticAggregate *aggregate = r_semantic_aggregate_for_type(context, value->type);
        RAstTokenView qualifier = {0};
        RAstTokenView name = {0};
        uint32_t count = UINT32_C(0);

        if ((aggregate == NULL) || (aggregate->kind != R_SEMANTIC_AGGREGATE_ENUM) ||
            aggregate->is_tagged || constant->negative) {
            return true;
        }
        if (!r_attribute_last_identifiers(context, constant->node, &qualifier, &name, &count)) {
            return false;
        }
        if ((count < 2U) || (qualifier.intern_id != aggregate->name_intern_id)) {
            return true;
        }
        for (uint32_t index = 0U; index < aggregate->variant_count; ++index) {
            const RSemanticVariant *variant =
                &context->semantic_variants[aggregate->first_variant + index];

            if (variant->name_intern_id == name.intern_id) {
                value->bits = variant->value;
                *valid = true;
                return true;
            }
        }
        return true;
    }
    if (r_semantic_type_is_integer(context, field_type) &&
        (token->kind == R_TOKEN_INTEGER_LITERAL)) {
        RToken literal = {token->kind, token->span, token->intern_id};
        RTokenKind suffix = R_TOKEN_INVALID;
        RTypeId suffix_type = R_TYPE_ID_INVALID;
        uint64_t magnitude = UINT64_C(0);
        bool is_signed = false;
        uint32_t width = UINT32_C(0);
        uint64_t maximum;

        if ((r_semantic_parse_integer_token(context, &literal, &magnitude, &suffix) !=
             R_INTEGER_TOKEN_VALID) ||
            !r_semantic_integer_properties(kind, &is_signed, &width)) {
            return true;
        }
        if ((suffix != R_TOKEN_INVALID) &&
            (!r_semantic_type_from_token(context, suffix, &suffix_type) ||
             (suffix_type != value->type))) {
            return context->resource_status == R_FRONTEND_OK;
        }
        maximum = (width == UINT32_C(64)) ? UINT64_MAX : ((UINT64_C(1) << width) - UINT64_C(1));
        if (is_signed) {
            maximum >>= 1U;
            if (constant->negative ? (magnitude > maximum + UINT64_C(1)) : (magnitude > maximum)) {
                return true;
            }
        } else if (constant->negative ? (magnitude != 0U) : (magnitude > maximum)) {
            return true;
        }
        value->bits = constant->negative ? (UINT64_C(0) - magnitude) : magnitude;
        *valid = true;
        return true;
    }
    if (((kind == R_SEMANTIC_TYPE_F32) || (kind == R_SEMANTIC_TYPE_F64)) &&
        (token->kind == R_TOKEN_FLOAT_LITERAL)) {
        RFloatLiteral literal = {0};

        if ((r_semantic_parse_float_token(context, token->span, &literal) !=
             R_FLOAT_LITERAL_VALID) ||
            (literal.type_token !=
             ((kind == R_SEMANTIC_TYPE_F32) ? R_TOKEN_KW_F32 : R_TOKEN_KW_F64))) {
            return context->resource_status == R_FRONTEND_OK;
        }
        value->bits = literal.bits;
        if (constant->negative) {
            value->bits ^=
                (kind == R_SEMANTIC_TYPE_F32) ? (UINT64_C(1) << 31U) : (UINT64_C(1) << 63U);
        }
        *valid = true;
        return true;
    }
    if (constant->negative) {
        return true;
    }
    if ((kind == R_SEMANTIC_TYPE_BOOL) &&
        ((token->kind == R_TOKEN_KW_TRUE) || (token->kind == R_TOKEN_KW_FALSE))) {
        value->bits = (token->kind == R_TOKEN_KW_TRUE) ? UINT64_C(1) : UINT64_C(0);
        *valid = true;
        return true;
    }
    if ((kind == R_SEMANTIC_TYPE_CHAR) && (token->kind == R_TOKEN_CHARACTER_LITERAL)) {
        uint32_t code_point = UINT32_C(0);

        if (!r_decode_character_literal(context, token->span, &code_point)) {
            return context->resource_status == R_FRONTEND_OK;
        }
        value->bits = code_point;
        *valid = true;
        return true;
    }
    if ((kind == R_SEMANTIC_TYPE_STR) && (token->kind == R_TOKEN_STRING_LITERAL)) {
        size_t length = 0U;

        if (!r_intern_string_sequence(context, token->span, &value->text_intern_id, &length)) {
            return false;
        }
        if (length > (size_t)UINT32_MAX) {
            return true;
        }
        value->text_length = (uint32_t)length;
        *valid = true;
        return true;
    }
    return true;
}

/* A field initializer as an attribute constant: the expression reduces to one literal, a
   negated literal or an enumerator. */
static bool r_attribute_initializer_constant(const RFrontendContext *context,
                                             RAstRef node,
                                             uint32_t depth,
                                             RAttributeConstant *constant,
                                             bool *found) {
    RAstNodeView view;
    RAstRef only = {0};
    uint32_t nodes = UINT32_C(0);

    *found = false;
    if ((depth > 32U) || !r_ast_ref_view(context, node, &view)) {
        return depth <= 32U;
    }
    if ((view.kind == R_SYNTAX_TYPE) || (view.kind == R_SYNTAX_QUALIFIED_NAME)) {
        constant->node = node;
        constant->span = view.span;
        *found = true;
        return true;
    }
    for (uint32_t index = 0U; index < view.child_count; ++index) {
        RAstRef child;
        RAstTokenView token;

        if (!r_ast_ref_child(context, node, index, &child)) {
            return false;
        }
        if (r_ast_ref_token(context, child, &token)) {
            if ((token.kind == R_TOKEN_EQUAL) || (token.kind == R_TOKEN_LPAREN) ||
                (token.kind == R_TOKEN_RPAREN)) {
                continue;
            }
            if ((token.kind == R_TOKEN_MINUS) && !constant->negative && !constant->has_token) {
                constant->negative = true;
                continue;
            }
            if (constant->has_token || (nodes != 0U)) {
                return true;
            }
            constant->token = token;
            constant->has_token = true;
            constant->span = token.span;
            continue;
        }
        only = child;
        nodes += 1U;
    }
    if (constant->has_token) {
        *found = nodes == 0U;
        return true;
    }
    if (nodes != 1U) {
        return true;
    }
    return r_attribute_initializer_constant(context, only, depth + 1U, constant, found);
}

/* The default of an omitted argument: the initializer of the field. */
static bool r_attribute_field_default(RFrontendContext *context,
                                      const RSemanticField *field,
                                      RSemanticAttributeValue *value,
                                      bool *present,
                                      bool *valid) {
    RAttributeConstant constant;
    bool found = false;

    *present = field->initializer.node != 0U;
    *valid = false;
    if (!*present) {
        return true;
    }
    (void)memset(&constant, 0, sizeof(constant));
    if (!r_attribute_initializer_constant(context, field->initializer, 0U, &constant, &found)) {
        return false;
    }
    return !found || r_attribute_constant_value(context, &constant, field->type, value, valid);
}

/* The fields of every attribute type hold admitted constant types, with constant defaults. */
static bool r_attributes_check_types(RFrontendContext *context) {
    for (size_t index = 0U; index < context->semantic_aggregate_count; ++index) {
        const RSemanticAggregate *aggregate = &context->semantic_aggregates[index];

        if (aggregate->attribute_targets == 0U) {
            continue;
        }
        for (uint32_t member = 0U; member < aggregate->field_count; ++member) {
            const RSemanticField *field =
                &context->semantic_fields[aggregate->first_field + member];
            RSemanticAttributeValue value;
            bool present = false;
            bool valid = false;

            if (!r_attribute_field_type_admitted(context, field->type)) {
                if (!r_attribute_diagnostic(
                        context,
                        "R-DIAG-TYPE-001",
                        "a field of an attribute type has an integer, bool, char, "
                        "f32, f64, str or fieldless enum type",
                        field->name_span)) {
                    return false;
                }
                continue;
            }
            if (!r_attribute_field_default(context, field, &value, &present, &valid)) {
                return false;
            }
            if (present && !valid &&
                !r_attribute_diagnostic(
                    context,
                    "R-DIAG-TYPE-001",
                    "the initializer of an attribute field is a constant of its "
                    "type",
                    field->name_span)) {
                return false;
            }
        }
    }
    return true;
}

/* The type that the uses of a type name: an instance of a generic answers for its origin. */
static RTypeId r_attribute_owner(const RFrontendContext *context, RTypeId type) {
    const RSemanticAggregate *owner =
        r_semantic_aggregate_for_type(context, r_semantic_value_type(context, type));

    if (owner == NULL) {
        return R_TYPE_ID_INVALID;
    }
    if ((owner->generic_origin != 0U) &&
        ((size_t)owner->generic_origin <= context->semantic_aggregate_count)) {
        owner = &context->semantic_aggregates[owner->generic_origin - 1U];
    }
    return owner->type;
}

/* The valid use of attribute type `attribute_type` on a type, a field or an enumerator. */
const RSemanticAttributeUse *r_semantic_attribute_use(const RFrontendContext *context,
                                                      RTypeId attribute_type,
                                                      uint32_t target,
                                                      RTypeId owner,
                                                      uint32_t member) {
    const RTypeId marked = r_attribute_owner(context, owner);

    if (marked == R_TYPE_ID_INVALID) {
        return NULL;
    }
    for (size_t index = 0U; index < context->attribute_use_count; ++index) {
        const RSemanticAttributeUse *use = &context->attribute_uses[index];

        if (use->valid && (use->attribute_type == attribute_type) && (use->target == target) &&
            (use->owner == marked) && (use->member == member)) {
            return use;
        }
    }
    return NULL;
}

/* The arguments of one use, in the order of the fields of its attribute type. */
static bool r_attributes_bind_arguments(RFrontendContext *context,
                                        RSemanticAttributeUse *use,
                                        const RSemanticAggregate *attribute,
                                        const RAstNodeView *view) {
    RSemanticAttributeValue *values;
    unsigned char *given;
    uint32_t position = UINT32_C(0);
    int form = 0; /* 1 positional, 2 named */
    bool valid = true;
    bool success = false;
    uint32_t index = 2U;

    if (attribute->field_count > 64U) {
        return r_attribute_diagnostic(
            context, "R-DIAG-SYN-002", "an attribute type has at most 64 fields", view->span);
    }
    values = r_context_allocate(context, ((size_t)attribute->field_count + 1U) * sizeof(*values));
    given = r_context_allocate(context, (size_t)attribute->field_count + 1U);
    if ((values == NULL) || (given == NULL)) {
        r_context_free(context, values);
        r_context_free(context, given);
        return false;
    }
    (void)memset(values, 0, ((size_t)attribute->field_count + 1U) * sizeof(*values));
    (void)memset(given, 0, (size_t)attribute->field_count + 1U);
    while (valid && (index < view->child_count)) {
        RAstRef part;
        RAstTokenView token;
        RAttributeConstant constant;
        uint32_t field_index = UINT32_MAX;
        bool constant_valid = false;

        if (!r_ast_ref_child(context, use->attribute, index, &part)) {
            goto cleanup;
        }
        if (r_ast_ref_token(context, part, &token) &&
            ((token.kind == R_TOKEN_LPAREN) || (token.kind == R_TOKEN_RPAREN) ||
             (token.kind == R_TOKEN_COMMA))) {
            index += 1U;
            continue;
        }
        (void)memset(&constant, 0, sizeof(constant));
        if (r_ast_ref_token(context, part, &token) && (token.kind == R_TOKEN_IDENTIFIER)) {
            /* `field = value` */
            RAstRef equal;
            RAstTokenView equal_token;

            if (!r_ast_ref_child(context, use->attribute, index + 1U, &equal) ||
                !r_ast_ref_token(context, equal, &equal_token) ||
                (equal_token.kind != R_TOKEN_EQUAL) || (form == 1)) {
                valid = false;
                break;
            }
            form = 2;
            for (uint32_t member = 0U; member < attribute->field_count; ++member) {
                if (context->semantic_fields[attribute->first_field + member].name_intern_id ==
                    token.intern_id) {
                    field_index = member;
                }
            }
            index += 2U;
            if (!r_ast_ref_child(context, use->attribute, index, &part)) {
                valid = false;
                break;
            }
        } else {
            if (form == 2) {
                valid = false;
                break;
            }
            form = 1;
            field_index = position;
            position += 1U;
        }
        if ((field_index >= attribute->field_count) || (given[field_index] != 0U)) {
            valid = false;
            break;
        }
        if (r_ast_ref_token(context, part, &token) && (token.kind == R_TOKEN_MINUS)) {
            constant.negative = true;
            index += 1U;
            if (!r_ast_ref_child(context, use->attribute, index, &part)) {
                valid = false;
                break;
            }
        }
        if (r_ast_ref_token(context, part, &token)) {
            constant.token = token;
            constant.has_token = true;
            constant.span = token.span;
        } else {
            RAstNodeView part_view;

            if (!r_ast_ref_view(context, part, &part_view)) {
                goto cleanup;
            }
            constant.node = part;
            constant.span = part_view.span;
        }
        index += 1U;
        if (!r_attribute_constant_value(
                context,
                &constant,
                context->semantic_fields[attribute->first_field + field_index].type,
                &values[field_index],
                &constant_valid)) {
            goto cleanup;
        }
        if (!constant_valid) {
            success =
                r_attribute_diagnostic(context,
                                       "R-DIAG-TYPE-001",
                                       "an attribute argument is a literal or an enumerator of "
                                       "the type of its field, an integer within its range",
                                       constant.span);
            goto cleanup;
        }
        given[field_index] = 1U;
    }
    for (uint32_t member = 0U; valid && (member < attribute->field_count); ++member) {
        bool present = false;
        bool default_valid = false;

        if (given[member] != 0U) {
            continue;
        }
        if (!r_attribute_field_default(context,
                                       &context->semantic_fields[attribute->first_field + member],
                                       &values[member],
                                       &present,
                                       &default_valid)) {
            goto cleanup;
        }
        valid = present && default_valid;
    }
    if (!valid) {
        success =
            r_attribute_diagnostic(context,
                                   "R-DIAG-SYN-002",
                                   "attribute arguments are all positional or all named, each "
                                   "field once, and every field without initializer has one",
                                   view->span);
        goto cleanup;
    }
    if (!r_grow_array(context,
                      (void **)&context->attribute_values,
                      &context->attribute_value_capacity,
                      sizeof(*context->attribute_values),
                      context->attribute_value_count + attribute->field_count + 1U)) {
        goto cleanup;
    }
    use->first_value = (uint32_t)context->attribute_value_count;
    for (uint32_t member = 0U; member < attribute->field_count; ++member) {
        context->attribute_values[context->attribute_value_count++] = values[member];
    }
    use->valid = true;
    success = true;
cleanup:
    r_context_free(context, values);
    r_context_free(context, given);
    return success;
}

static bool r_attributes_resolve_use(RFrontendContext *context, size_t use_index) {
    RSemanticAttributeUse *use = &context->attribute_uses[use_index];
    RAstNodeView view;
    RAstRef name;
    RTypeId type = R_TYPE_ID_INVALID;
    const RSemanticAggregate *attribute;
    const size_t diagnostics = context->diagnostic_count;

    if (!r_ast_ref_view(context, use->attribute, &view) ||
        !r_ast_ref_child(context, use->attribute, 1U, &name)) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    if (use->target == R_ATTRIBUTE_TARGET_TYPE) {
        for (size_t index = 0U; index < context->semantic_aggregate_count; ++index) {
            if ((context->semantic_aggregates[index].module_source == use->attribute.source) &&
                (context->semantic_aggregates[index].declaration_ast == use->declaration)) {
                use->owner = context->semantic_aggregates[index].type;
            }
        }
        if (use->owner == R_TYPE_ID_INVALID) {
            context->resource_status = R_FRONTEND_INTERNAL_ERROR;
            return false;
        }
    }
    context->unresolved_type_name_span = (RSourceSpan){0U, 0U, 0U};
    if (!r_semantic_type_from_ast(context, name, &type)) {
        context->unresolved_type_name_span = (RSourceSpan){0U, 0U, 0U};
        if (context->resource_status != R_FRONTEND_OK) {
            return false;
        }
        type = R_TYPE_ID_INVALID;
    }
    attribute = (type == R_TYPE_ID_INVALID)
                    ? NULL
                    : r_semantic_aggregate_for_type(context, r_semantic_value_type(context, type));
    if ((attribute == NULL) || (attribute->attribute_targets == 0U)) {
        return (context->diagnostic_count != diagnostics) ||
               r_attribute_diagnostic(context,
                                      "R-DIAG-SYN-002",
                                      "unknown attribute: the name is no built-in attribute and no "
                                      "attribute type",
                                      view.span);
    }
    if ((attribute->attribute_targets & use->target) == 0U) {
        return r_attribute_diagnostic(context,
                                      "R-DIAG-SYN-002",
                                      use->target == R_ATTRIBUTE_TARGET_TYPE
                                          ? "this attribute type does not mark a type"
                                      : use->target == R_ATTRIBUTE_TARGET_FIELD
                                          ? "this attribute type does not mark a struct field"
                                          : "this attribute type does not mark an enumerator",
                                      view.span);
    }
    use->attribute_type = attribute->type;
    if (r_semantic_attribute_use(context, attribute->type, use->target, use->owner, use->member) !=
        NULL) {
        return r_attribute_diagnostic(context,
                                      "R-DIAG-SYN-002",
                                      "a declaration carries each attribute type at most once",
                                      view.span);
    }
    return r_attributes_bind_arguments(context, use, attribute, &view);
}

/* Every attribute written as a type that no collector recorded stands where no attribute of the
   program is admitted. */
static bool r_attributes_report_unplaced(RFrontendContext *context) {
    for (size_t source_index = 0U; source_index < context->source_count; ++source_index) {
        const RSourceId source_id = (RSourceId)(source_index + 1U);
        const RSource *source = r_get_source_const(context, source_id);

        if (source == NULL) {
            continue;
        }
        for (size_t node = 0U; node < source->ast_node_count; ++node) {
            const RAstRef ref = {source_id, (RAstNodeId)(node + 1U)};
            bool recorded = false;

            if ((source->ast_nodes[node].kind != R_SYNTAX_ATTRIBUTE) ||
                !r_semantic_attribute_is_user(context, ref)) {
                continue;
            }
            for (size_t use = 0U; !recorded && (use < context->attribute_use_count); ++use) {
                recorded = (context->attribute_uses[use].attribute.source == ref.source) &&
                           (context->attribute_uses[use].attribute.node == ref.node);
            }
            if (!recorded &&
                !r_attribute_diagnostic(context,
                                        "R-DIAG-SYN-002",
                                        "an attribute of the program marks only a struct, enum or "
                                        "error declaration, a struct field or an enumerator",
                                        source->ast_nodes[node].span)) {
                return false;
            }
        }
    }
    return true;
}

static bool r_attributes_resolve(RFrontendContext *context) {
    if (!r_attributes_check_types(context)) {
        return false;
    }
    for (size_t index = 0U; index < context->attribute_use_count; ++index) {
        if (!r_attributes_resolve_use(context, index)) {
            return false;
        }
    }
    return r_attributes_report_unplaced(context);
}

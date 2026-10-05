/* R-REFL-0001 through R-REFL-0004 (Library R-LIB-0024): static reflection intrinsics.
 *
 * Every form is a compiler-recognized `core::` item. A form whose operands are concrete folds
 * here into ordinary literal nodes (integer, enum constant, fixed array or program string); a
 * form over a generic parameter becomes a dependent R_HIR_STANDARD_CALL that the generic clone
 * folds once the parameter is substituted (r_reflection_fold_clone). The five selections that
 * depend on a runtime value (enum_name, enum_ordinal, enum_at, enum_from_name, variant_name)
 * stay R_HIR_STANDARD_CALL nodes that the C17 emitter lowers to inline `switch` statements and
 * comparison chains over the program-string pool, so no runtime symbol exists and the
 * freestanding profile is served by the same lowering. */

#include "target_identity.generated.inc"

typedef enum RReflectionTypeOperation {
    R_REFLECTION_TYPE_INVALID = 0,
    R_REFLECTION_TYPE_ENUM_COUNT,
    R_REFLECTION_TYPE_ENUM_MIN,
    R_REFLECTION_TYPE_ENUM_MAX,
    R_REFLECTION_TYPE_ENUM_VARIANTS,
    R_REFLECTION_TYPE_ENUM_AT,
    R_REFLECTION_TYPE_ENUM_FROM_NAME,
    R_REFLECTION_TYPE_VARIANT_COUNT,
    R_REFLECTION_TYPE_TYPE_NAME,
    R_REFLECTION_TYPE_FIELD_COUNT,
    R_REFLECTION_TYPE_FIELD_NAME
} RReflectionTypeOperation;

typedef struct RReflectionTypeForm {
    const char *name;
    RReflectionTypeOperation operation;
    const char *rule_id;
} RReflectionTypeForm;

static const RReflectionTypeForm r_reflection_type_forms[] = {
    {"enum_count", R_REFLECTION_TYPE_ENUM_COUNT, "R-REFL-0001"},
    {"enum_min", R_REFLECTION_TYPE_ENUM_MIN, "R-REFL-0001"},
    {"enum_max", R_REFLECTION_TYPE_ENUM_MAX, "R-REFL-0001"},
    {"enum_variants", R_REFLECTION_TYPE_ENUM_VARIANTS, "R-REFL-0001"},
    {"enum_at", R_REFLECTION_TYPE_ENUM_AT, "R-REFL-0001"},
    {"enum_from_name", R_REFLECTION_TYPE_ENUM_FROM_NAME, "R-REFL-0001"},
    {"variant_count", R_REFLECTION_TYPE_VARIANT_COUNT, "R-REFL-0002"},
    {"type_name", R_REFLECTION_TYPE_TYPE_NAME, "R-REFL-0003"},
    {"field_count", R_REFLECTION_TYPE_FIELD_COUNT, "R-REFL-0003"},
    {"field_name", R_REFLECTION_TYPE_FIELD_NAME, "R-REFL-0003"},
};

/* RReflectionText (semantic.c) is growable NUL-terminated text owned by the frontend context. */
static bool r_reflection_text_append(RFrontendContext *context,
                                     RReflectionText *text,
                                     const char *bytes,
                                     size_t length) {
    if (!r_grow_array(context,
                      (void **)&text->bytes,
                      &text->capacity,
                      sizeof(char),
                      text->length + length + 1U)) {
        return false;
    }
    if (length != 0U) {
        (void)memcpy(text->bytes + text->length, bytes, length);
    }
    text->length += length;
    text->bytes[text->length] = '\0';
    return true;
}

static bool r_reflection_text_append_string(RFrontendContext *context,
                                            RReflectionText *text,
                                            const char *string) {
    return r_reflection_text_append(context, text, string, strlen(string));
}

static bool
r_reflection_text_append_uint(RFrontendContext *context, RReflectionText *text, uint64_t value) {
    char buffer[32];
    const int written = snprintf(buffer, sizeof(buffer), "%" PRIu64, value);

    return (written > 0) && ((size_t)written < sizeof(buffer)) &&
           r_reflection_text_append(context, text, buffer, (size_t)written);
}

static bool r_reflection_text_append_intern(RFrontendContext *context,
                                            RReflectionText *text,
                                            uint32_t intern_id) {
    const char *bytes;
    size_t length;

    return r_semantic_intern_text(context, intern_id, &bytes, &length) &&
           r_reflection_text_append(context, text, bytes, length);
}

/* The aggregate that a struct or enum type designates, or NULL. A fieldless enum of the library
   designates its hidden schema once r_semantic_prepare_standard_enum has prepared it. */
static const RSemanticAggregate *
r_reflection_aggregate(const RFrontendContext *context, RTypeId type_id, uint32_t *aggregate_id) {
    const RTypeId value_type = r_semantic_value_type(context, type_id);
    const RSemanticType *type = r_semantic_type(context, value_type);

    if ((type != NULL) && (type->kind == R_SEMANTIC_TYPE_STANDARD)) {
        for (size_t index = 0U; index < context->semantic_aggregate_count; ++index) {
            const RSemanticAggregate *schema = &context->semantic_aggregates[index];

            if ((schema->type == value_type) && (schema->kind == R_SEMANTIC_AGGREGATE_ENUM) &&
                !schema->is_tagged) {
                if (aggregate_id != NULL) {
                    *aggregate_id = (uint32_t)index + 1U;
                }
                return schema;
            }
        }
        return NULL;
    }
    if ((type == NULL) ||
        ((type->kind != R_SEMANTIC_TYPE_STRUCT) && (type->kind != R_SEMANTIC_TYPE_ENUM)) ||
        (type->base == 0U) || ((size_t)type->base > context->semantic_aggregate_count)) {
        return NULL;
    }
    if (aggregate_id != NULL) {
        *aggregate_id = (uint32_t)type->base;
    }
    return &context->semantic_aggregates[(size_t)type->base - 1U];
}

static const RSemanticAggregate *r_reflection_fieldless_enum(const RFrontendContext *context,
                                                             RTypeId type_id) {
    const RSemanticAggregate *aggregate = r_reflection_aggregate(context, type_id, NULL);

    return ((aggregate != NULL) && (aggregate->kind == R_SEMANTIC_AGGREGATE_ENUM) &&
            !aggregate->is_tagged && (aggregate->variant_count != 0U) && !aggregate->poisoned)
               ? aggregate
               : NULL;
}

static const RSemanticAggregate *r_reflection_tagged_enum(const RFrontendContext *context,
                                                          RTypeId type_id) {
    const RSemanticAggregate *aggregate = r_reflection_aggregate(context, type_id, NULL);

    return ((aggregate != NULL) && (aggregate->kind == R_SEMANTIC_AGGREGATE_ENUM) &&
            aggregate->is_tagged && (aggregate->variant_count != 0U) && !aggregate->poisoned)
               ? aggregate
               : NULL;
}

static const RSemanticAggregate *r_reflection_struct(const RFrontendContext *context,
                                                     RTypeId type_id) {
    const RSemanticAggregate *aggregate = r_reflection_aggregate(context, type_id, NULL);

    return ((aggregate != NULL) && (aggregate->kind == R_SEMANTIC_AGGREGATE_STRUCT) &&
            !aggregate->is_opaque && !aggregate->poisoned)
               ? aggregate
               : NULL;
}

static const RSemanticVariant *r_reflection_variant(const RFrontendContext *context,
                                                    const RSemanticAggregate *aggregate,
                                                    uint32_t index) {
    const size_t global = (size_t)aggregate->first_variant + (size_t)index;

    return (global < context->semantic_variant_count) ? &context->semantic_variants[global] : NULL;
}

static const RSemanticField *r_reflection_field(const RFrontendContext *context,
                                                const RSemanticAggregate *aggregate,
                                                uint32_t index) {
    const size_t global = (size_t)aggregate->first_field + (size_t)index;

    return (global < context->semantic_field_count) ? &context->semantic_fields[global] : NULL;
}

/* A type that still mentions a generic parameter of an open definition. */
static bool
r_reflection_type_is_dependent(const RFrontendContext *context, RTypeId type_id, uint32_t depth) {
    const RSemanticType *type = r_semantic_type(context, type_id);

    if ((type == NULL) || (depth > context->options.limits.max_nesting)) {
        return false;
    }
    switch (type->kind) {
    case R_SEMANTIC_TYPE_PARAMETER:
        return true;
    case R_SEMANTIC_TYPE_STRUCT:
    case R_SEMANTIC_TYPE_ENUM: {
        const RSemanticAggregate *aggregate = r_reflection_aggregate(context, type_id, NULL);
        const RGenericSchema *schema;
        uint32_t index;

        if ((aggregate == NULL) || (aggregate->generic_origin == 0U) ||
            (aggregate->generic_origin > context->semantic_aggregate_count)) {
            return false;
        }
        schema =
            &context->generic_schemas
                 [context->semantic_aggregates[aggregate->generic_origin - 1U].generic_schema - 1U];
        for (index = 0U; index < schema->parameter_count; ++index) {
            if (r_reflection_type_is_dependent(
                    context,
                    context->generic_arguments[aggregate->first_generic_argument + index],
                    depth + 1U)) {
                return true;
            }
        }
        return false;
    }
    case R_SEMANTIC_TYPE_RAW_FUNCTION: {
        RTypeId parameter_id = type->second;
        uint64_t index;

        if (r_reflection_type_is_dependent(context, type->base, depth + 1U)) {
            return true;
        }
        for (index = 0U; index < type->length; ++index) {
            const RSemanticType *parameter = r_semantic_type(context, parameter_id);

            if ((parameter == NULL) || (parameter->kind != R_SEMANTIC_TYPE_FUNCTION_PARAMETER)) {
                return false;
            }
            if (r_reflection_type_is_dependent(context, parameter->base, depth + 1U)) {
                return true;
            }
            parameter_id = parameter->second;
        }
        return false;
    }
    case R_SEMANTIC_TYPE_FIXED_ARRAY:
    case R_SEMANTIC_TYPE_SLICE:
    case R_SEMANTIC_TYPE_BORROW:
    case R_SEMANTIC_TYPE_ARRAY:
    case R_SEMANTIC_TYPE_LIST:
    case R_SEMANTIC_TYPE_DICT:
    case R_SEMANTIC_TYPE_TASK:
    case R_SEMANTIC_TYPE_ARC:
    case R_SEMANTIC_TYPE_RC:
    case R_SEMANTIC_TYPE_WEAK:
    case R_SEMANTIC_TYPE_OWN:
    case R_SEMANTIC_TYPE_RAW:
    case R_SEMANTIC_TYPE_STANDARD:
    case R_SEMANTIC_TYPE_OPTION:
    case R_SEMANTIC_TYPE_RESULT:
    case R_SEMANTIC_TYPE_CONST:
    case R_SEMANTIC_TYPE_ATOMIC:
        return ((type->base != R_TYPE_ID_INVALID) &&
                r_reflection_type_is_dependent(context, type->base, depth + 1U)) ||
               ((type->second != R_TYPE_ID_INVALID) &&
                (type->kind != R_SEMANTIC_TYPE_FIXED_ARRAY) &&
                r_reflection_type_is_dependent(context, type->second, depth + 1U));
    default:
        return false;
    }
}

static bool r_reflection_spell_type(RFrontendContext *context,
                                    RTypeId type_id,
                                    RReflectionText *text,
                                    uint32_t depth);

/* `module.path::Name` of an aggregate, followed by the generic arguments of an instance. */
static bool r_reflection_spell_aggregate(RFrontendContext *context,
                                         const RSemanticAggregate *aggregate,
                                         RReflectionText *text,
                                         uint32_t depth) {
    const RSemanticAggregate *origin = aggregate;
    const RSource *source;
    const RGenericSchema *schema;
    uint32_t index;

    if ((aggregate->generic_origin != 0U) &&
        (aggregate->generic_origin <= context->semantic_aggregate_count)) {
        origin = &context->semantic_aggregates[aggregate->generic_origin - 1U];
    }
    if (origin->is_tuple && (origin != aggregate)) {
        /* R-TYPE-0052 (L18.1): a tuple is spelled by its elements, `(i32, str)`. */
        schema = &context->generic_schemas[origin->generic_schema - 1U];
        if (!r_reflection_text_append_string(context, text, "(")) {
            return false;
        }
        for (index = 0U; index < schema->parameter_count; ++index) {
            if (((index != 0U) && !r_reflection_text_append_string(context, text, ", ")) ||
                !r_reflection_spell_type(
                    context,
                    context->generic_arguments[aggregate->first_generic_argument + index],
                    text,
                    depth + 1U)) {
                return false;
            }
        }
        return r_reflection_text_append_string(context, text, ")");
    }
    source = r_get_source_const(context, origin->module_source);
    if ((source != NULL) && (source->module_name != NULL) && !origin->is_closure &&
        (!r_reflection_text_append_string(context, text, source->module_name) ||
         !r_reflection_text_append_string(context, text, "::"))) {
        return false;
    }
    if (!r_reflection_text_append_intern(context, text, origin->name_intern_id)) {
        return false;
    }
    if ((origin == aggregate) || (origin->generic_schema == 0U) ||
        (origin->generic_schema > context->generic_schema_count)) {
        return true;
    }
    schema = &context->generic_schemas[origin->generic_schema - 1U];
    if (!r_reflection_text_append_string(context, text, "<")) {
        return false;
    }
    for (index = 0U; index < schema->parameter_count; ++index) {
        if (((index != 0U) && !r_reflection_text_append_string(context, text, ", ")) ||
            !r_reflection_spell_type(
                context,
                context->generic_arguments[aggregate->first_generic_argument + index],
                text,
                depth + 1U)) {
            return false;
        }
    }
    return r_reflection_text_append_string(context, text, ">");
}

/* R-REFL-0003: the canonical source spelling of a complete type. */
static bool r_reflection_spell_type(RFrontendContext *context,
                                    RTypeId type_id,
                                    RReflectionText *text,
                                    uint32_t depth) {
    const RSemanticType *type = r_semantic_type(context, type_id);
    const char *keyword = NULL;
    const bool shared = (type != NULL) && ((type->flags & R_SEMANTIC_TYPE_FLAG_SHARED) != 0U);
    const bool nullable = (type != NULL) && ((type->flags & R_SEMANTIC_TYPE_FLAG_NULLABLE) != 0U);

    if ((type == NULL) || (depth > context->options.limits.max_nesting)) {
        return r_reflection_text_append_string(context, text, "?");
    }
    switch (type->kind) {
    case R_SEMANTIC_TYPE_BOOL:
        keyword = "bool";
        break;
    case R_SEMANTIC_TYPE_I8:
        keyword = "i8";
        break;
    case R_SEMANTIC_TYPE_I16:
        keyword = "i16";
        break;
    case R_SEMANTIC_TYPE_I32:
        keyword = "i32";
        break;
    case R_SEMANTIC_TYPE_I64:
        keyword = "i64";
        break;
    case R_SEMANTIC_TYPE_ISIZE:
        keyword = "isize";
        break;
    case R_SEMANTIC_TYPE_U8:
        keyword = "u8";
        break;
    case R_SEMANTIC_TYPE_U16:
        keyword = "u16";
        break;
    case R_SEMANTIC_TYPE_U32:
        keyword = "u32";
        break;
    case R_SEMANTIC_TYPE_U64:
        keyword = "u64";
        break;
    case R_SEMANTIC_TYPE_USIZE:
        keyword = "usize";
        break;
    case R_SEMANTIC_TYPE_F32:
        keyword = "f32";
        break;
    case R_SEMANTIC_TYPE_F64:
        keyword = "f64";
        break;
    case R_SEMANTIC_TYPE_CHAR:
        keyword = "char";
        break;
    case R_SEMANTIC_TYPE_STR:
        keyword = "str";
        break;
    case R_SEMANTIC_TYPE_CONSTEXPR_STR:
        keyword = "constexpr str";
        break;
    case R_SEMANTIC_TYPE_VOID:
        keyword = "void";
        break;
    case R_SEMANTIC_TYPE_NEVER:
        keyword = "never";
        break;
    case R_SEMANTIC_TYPE_C_ABI_NUMERIC:
        keyword = (type->length < (uint64_t)R_TOKEN_KIND_COUNT)
                      ? r_token_kind_name((RTokenKind)type->length)
                      : "?";
        break;
    case R_SEMANTIC_TYPE_STRUCT:
    case R_SEMANTIC_TYPE_ENUM: {
        const RSemanticAggregate *aggregate = r_reflection_aggregate(context, type_id, NULL);

        return (aggregate == NULL) ? r_reflection_text_append_string(context, text, "?")
                                   : r_reflection_spell_aggregate(context, aggregate, text, depth);
    }
    case R_SEMANTIC_TYPE_CONST:
        return r_reflection_text_append_string(context, text, "const ") &&
               r_reflection_spell_type(context, type->base, text, depth + 1U);
    case R_SEMANTIC_TYPE_ATOMIC:
        return r_reflection_text_append_string(context, text, "atomic ") &&
               r_reflection_spell_type(context, type->base, text, depth + 1U);
    case R_SEMANTIC_TYPE_BORROW:
        return (!shared || r_reflection_text_append_string(context, text, "const ")) &&
               r_reflection_spell_type(context, type->base, text, depth + 1U) &&
               r_reflection_text_append_string(context, text, nullable ? "*?" : "*");
    case R_SEMANTIC_TYPE_SLICE:
        return (!shared || r_reflection_text_append_string(context, text, "const ")) &&
               r_reflection_spell_type(context, type->base, text, depth + 1U) &&
               r_reflection_text_append_string(context, text, "[]");
    case R_SEMANTIC_TYPE_FIXED_ARRAY:
        return r_reflection_spell_type(context, type->base, text, depth + 1U) &&
               r_reflection_text_append_string(context, text, "[") &&
               ((type->second != R_TYPE_ID_INVALID)
                    ? r_reflection_text_append_string(context, text, "?")
                    : r_reflection_text_append_uint(context, text, type->length)) &&
               r_reflection_text_append_string(context, text, "]");
    case R_SEMANTIC_TYPE_OWN:
        return r_reflection_text_append_string(context, text, "own ") &&
               r_reflection_spell_type(context, type->base, text, depth + 1U) &&
               r_reflection_text_append_string(context, text, nullable ? "*?" : "*");
    case R_SEMANTIC_TYPE_RAW:
        return r_reflection_text_append_string(context, text, shared ? "raw const " : "raw ") &&
               r_reflection_spell_type(context, type->base, text, depth + 1U) &&
               r_reflection_text_append_string(context, text, nullable ? "*?" : "*");
    case R_SEMANTIC_TYPE_ARC:
    case R_SEMANTIC_TYPE_RC:
        return r_reflection_text_append_string(
                   context, text, type->kind == R_SEMANTIC_TYPE_ARC ? "arc " : "rc ") &&
               r_reflection_spell_type(context, type->base, text, depth + 1U);
    case R_SEMANTIC_TYPE_WEAK:
        return r_reflection_text_append_string(context,
                                               text,
                                               (type->flags & R_SEMANTIC_TYPE_FLAG_RC_OWNER) != 0U
                                                   ? "weak rc "
                                                   : "weak arc ") &&
               r_reflection_spell_type(context, type->base, text, depth + 1U);
    case R_SEMANTIC_TYPE_ARRAY:
    case R_SEMANTIC_TYPE_LIST:
    case R_SEMANTIC_TYPE_OPTION:
    case R_SEMANTIC_TYPE_TASK:
        return r_reflection_text_append_string(context,
                                               text,
                                               type->kind == R_SEMANTIC_TYPE_ARRAY    ? "array<"
                                               : type->kind == R_SEMANTIC_TYPE_LIST   ? "list<"
                                               : type->kind == R_SEMANTIC_TYPE_OPTION ? "o<"
                                                                                      : "task<") &&
               r_reflection_spell_type(context, type->base, text, depth + 1U) &&
               r_reflection_text_append_string(context, text, ">");
    case R_SEMANTIC_TYPE_DICT:
    case R_SEMANTIC_TYPE_RESULT:
        return r_reflection_text_append_string(
                   context, text, type->kind == R_SEMANTIC_TYPE_DICT ? "dict<" : "r<") &&
               r_reflection_spell_type(context, type->base, text, depth + 1U) &&
               r_reflection_text_append_string(context, text, ", ") &&
               r_reflection_spell_type(context, type->second, text, depth + 1U) &&
               r_reflection_text_append_string(context, text, ">");
    case R_SEMANTIC_TYPE_STANDARD:
        if (!r_reflection_text_append_intern(context, text, (uint32_t)type->length)) {
            return false;
        }
        if (type->base == R_TYPE_ID_INVALID) {
            return true;
        }
        return r_reflection_text_append_string(context, text, "<") &&
               r_reflection_spell_type(context, type->base, text, depth + 1U) &&
               ((type->second == R_TYPE_ID_INVALID) ||
                (r_reflection_text_append_string(context, text, ", ") &&
                 r_reflection_spell_type(context, type->second, text, depth + 1U))) &&
               r_reflection_text_append_string(context, text, ">");
    case R_SEMANTIC_TYPE_RAW_FUNCTION: {
        RTypeId parameter_id = type->second;
        uint64_t index;

        if (!r_reflection_text_append_string(context, text, nullable ? "raw fn?(" : "raw fn(")) {
            return false;
        }
        for (index = 0U; index < type->length; ++index) {
            const RSemanticType *parameter = r_semantic_type(context, parameter_id);

            if ((parameter == NULL) || (parameter->kind != R_SEMANTIC_TYPE_FUNCTION_PARAMETER) ||
                ((index != 0U) && !r_reflection_text_append_string(context, text, ", ")) ||
                !r_reflection_spell_type(context, parameter->base, text, depth + 1U)) {
                return false;
            }
            parameter_id = parameter->second;
        }
        return r_reflection_text_append_string(context, text, ") -> ") &&
               r_reflection_spell_type(context, type->base, text, depth + 1U);
    }
    case R_SEMANTIC_TYPE_DYN: {
        /* R-TYPE-0051: an interface is spelled by its canonical contract. */
        const RGenericParameter *contract = r_dyn_contract(context, type_id);
        if (contract != NULL && contract->dyn_key != 0U)
            return r_reflection_text_append_intern(context, text, contract->dyn_key);
        keyword = "?";
        break;
    }
    case R_SEMANTIC_TYPE_CONSTANT_EXPR:
        /* A closed constant argument is spelled as its source literal. */
        if (type->base == R_TYPE_ID_INVALID && type->second == R_TYPE_ID_INVALID) {
            const char *suffix;
            char value[32];
            if (!r_generic_constant_spelling(type, &suffix, value, sizeof(value)))
                return false;
            return r_reflection_text_append_string(context, text, value) &&
                   (type->flags == (uint32_t)R_TOKEN_KW_BOOL ||
                    r_reflection_text_append_string(context, text, suffix));
        }
        keyword = "?";
        break;
    case R_SEMANTIC_TYPE_PARAMETER:
        /* Only a diagnostic spells an open signature; a parameter is named as declared. */
        if ((type->base != 0U) && ((size_t)type->base <= context->generic_parameter_count)) {
            return r_reflection_text_append_intern(
                context, text, context->generic_parameters[type->base - 1U].name_intern_id);
        }
        keyword = "?";
        break;
    case R_SEMANTIC_TYPE_FUNCTION_PARAMETER:
    case R_SEMANTIC_TYPE_EFFECT_SET:
    case R_SEMANTIC_TYPE_EFFECT_CARRIER:
    case R_SEMANTIC_TYPE_INVALID:
    default:
        keyword = "?";
        break;
    }
    return r_reflection_text_append_string(context, text, keyword);
}

/* ---- HIR node builders ---- */

static bool r_reflection_append_usize(RBodyContext *body,
                                      RSourceSpan span,
                                      uint64_t value,
                                      RExpressionResult *result) {
    RTypeId usize_type;

    r_body_expression_invalid(result);
    if (!r_semantic_type_from_token(body->frontend, R_TOKEN_KW_USIZE, &usize_type) ||
        !r_body_append_node(body,
                            R_HIR_LITERAL,
                            span,
                            usize_type,
                            R_SYMBOL_ID_INVALID,
                            R_TOKEN_INVALID,
                            value,
                            false,
                            NULL,
                            &result->node)) {
        return false;
    }
    result->type = usize_type;
    result->valid = true;
    return true;
}

/* A program string (R-TYPE-0028) holding `length` bytes of `bytes`. */
static bool r_reflection_append_text_node(RBodyContext *body,
                                          RSourceSpan span,
                                          const char *bytes,
                                          size_t length,
                                          RExpressionResult *result) {
    uint32_t intern_id = UINT32_C(0);
    RHirNode *node;

    r_body_expression_invalid(result);
    if (!r_intern_bytes(body->frontend, (const uint8_t *)bytes, length, &intern_id) ||
        !r_semantic_intern_type(
            body->frontend, R_SEMANTIC_TYPE_CONSTEXPR_STR, R_TYPE_ID_INVALID, &result->type) ||
        !r_body_append_node(body,
                            R_HIR_STRING_LITERAL,
                            span,
                            result->type,
                            R_SYMBOL_ID_INVALID,
                            R_TOKEN_INVALID,
                            (uint64_t)length,
                            false,
                            NULL,
                            &result->node)) {
        return false;
    }
    node = r_body_hir_node(body, result->node);
    if (node == NULL) {
        body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    node->aggregate_member = intern_id;
    result->valid = true;
    return true;
}

static bool r_reflection_append_enum_constant(RBodyContext *body,
                                              RSourceSpan span,
                                              const RSemanticAggregate *aggregate,
                                              uint32_t variant_index,
                                              RHirNodeId *node_id) {
    const RSemanticVariant *variant =
        r_reflection_variant(body->frontend, aggregate, variant_index);
    RHirNode *node;

    if ((variant == NULL) || !r_body_append_node(body,
                                                 R_HIR_ENUM_CONSTANT,
                                                 span,
                                                 aggregate->type,
                                                 R_SYMBOL_ID_INVALID,
                                                 R_TOKEN_INVALID,
                                                 variant->value,
                                                 false,
                                                 NULL,
                                                 node_id)) {
        return false;
    }
    node = r_body_hir_node(body, *node_id);
    if (node == NULL) {
        body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    node->aggregate_member = aggregate->first_variant + variant_index + UINT32_C(1);
    return true;
}

/* A reflection call node: dependent constants and the runtime selections share this shape. */
static bool r_reflection_append_call(RBodyContext *body,
                                     RSourceSpan span,
                                     RStandardCallOperation operation,
                                     RTypeId result_type,
                                     RTypeId operand_type,
                                     uint64_t integer_value,
                                     const RHirVector *children,
                                     RExpressionResult *result) {
    RHirNode *node;

    r_body_expression_invalid(result);
    if (!r_body_append_node(body,
                            R_HIR_STANDARD_CALL,
                            span,
                            result_type,
                            R_SYMBOL_ID_INVALID,
                            R_TOKEN_INVALID,
                            integer_value,
                            false,
                            children,
                            &result->node)) {
        return false;
    }
    node = r_body_hir_node(body, result->node);
    if (node == NULL) {
        body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    node->standard_operation = operation;
    node->auxiliary_type = operand_type;
    node->runtime_type = operand_type;
    node->borrow_origin = R_SYMBOL_ID_INVALID;
    result->type = result_type;
    result->valid = true;
    return true;
}

/* The variant of a fieldless enum with the least or greatest discriminant. */
static uint32_t r_reflection_enum_extreme(const RFrontendContext *context,
                                          const RSemanticAggregate *aggregate,
                                          bool greatest) {
    uint32_t selected = 0U;
    uint32_t index;

    for (index = 1U; index < aggregate->variant_count; ++index) {
        const RSemanticVariant *candidate = r_reflection_variant(context, aggregate, index);
        const RSemanticVariant *current = r_reflection_variant(context, aggregate, selected);
        bool is_signed = false;
        uint32_t width = 0U;
        bool better;

        if ((candidate == NULL) || (current == NULL)) {
            break;
        }
        (void)r_semantic_integer_properties(
            r_semantic_integer_representation(context, aggregate->enum_underlying_type),
            &is_signed,
            &width);
        better = is_signed ? (greatest ? ((int64_t)candidate->value > (int64_t)current->value)
                                       : ((int64_t)candidate->value < (int64_t)current->value))
                           : (greatest ? (candidate->value > current->value)
                                       : (candidate->value < current->value));
        if (better) {
            selected = index;
        }
    }
    return selected;
}

/* ---- type-operand forms ---- */

static bool r_reflection_type_operation(const RFrontendContext *context,
                                        RAstRef qualified,
                                        RReflectionTypeOperation *operation,
                                        const char **rule_id) {
    RAstNodeView view;
    RAstTokenView components[2];
    uint32_t child_index;
    uint32_t component_count = UINT32_C(0);
    size_t form_index;

    *operation = R_REFLECTION_TYPE_INVALID;
    if (!r_ast_view_kind(context, qualified, R_SYNTAX_QUALIFIED_NAME, &view)) {
        return false;
    }
    (void)memset(components, 0, sizeof(components));
    for (child_index = UINT32_C(0); child_index < view.child_count; ++child_index) {
        RAstRef child;
        RAstTokenView token;

        if (!r_ast_ref_child(context, qualified, child_index, &child) ||
            !r_ast_ref_token(context, child, &token)) {
            continue;
        }
        if (component_count >= UINT32_C(2)) {
            return false;
        }
        components[component_count] = token;
        component_count += UINT32_C(1);
    }
    if ((component_count != UINT32_C(2)) ||
        !r_semantic_token_text_equal_owned(context, &components[0], "core")) {
        return false;
    }
    for (form_index = 0U;
         form_index < sizeof(r_reflection_type_forms) / sizeof(r_reflection_type_forms[0]);
         ++form_index) {
        const RReflectionTypeForm *form = &r_reflection_type_forms[form_index];

        if (r_semantic_token_text_equal_owned(context, &components[1], form->name)) {
            *operation = form->operation;
            *rule_id = form->rule_id;
            return true;
        }
    }
    return false;
}

static bool r_reflection_lower_enum_variants(RBodyContext *body,
                                             RSourceSpan span,
                                             const RSemanticAggregate *aggregate,
                                             RExpressionResult *result) {
    RHirVector elements = {0};
    RTypeId array_type;
    uint32_t index;
    bool success = false;

    r_body_expression_invalid(result);
    for (index = 0U; index < aggregate->variant_count; ++index) {
        RHirNodeId element;

        if (!r_reflection_append_enum_constant(body, span, aggregate, index, &element) ||
            !r_hir_vector_push(body->frontend, &elements, element)) {
            goto cleanup;
        }
    }
    if (!r_semantic_intern_derived_type(body->frontend,
                                        R_SEMANTIC_TYPE_FIXED_ARRAY,
                                        aggregate->type,
                                        R_TYPE_ID_INVALID,
                                        (uint64_t)aggregate->variant_count,
                                        R_SEMANTIC_TYPE_FLAG_NONE,
                                        &array_type) ||
        !r_body_append_node(body,
                            R_HIR_ARRAY_INIT,
                            span,
                            array_type,
                            R_SYMBOL_ID_INVALID,
                            R_TOKEN_INVALID,
                            (uint64_t)aggregate->variant_count,
                            false,
                            &elements,
                            &result->node)) {
        goto cleanup;
    }
    result->type = array_type;
    result->valid = true;
    success = true;
cleanup:
    r_context_free(body->frontend, elements.items);
    return success;
}

/* `core::field_name::<T>(index)`: the index is an integer literal or a module constant. */
static bool r_reflection_constant_index(RBodyContext *body,
                                        RAstRef value_ref,
                                        RSourceSpan span,
                                        const char *rule_id,
                                        uint64_t *index,
                                        bool *valid) {
    RExpressionResult operand;
    RBodyConstantValue constant;
    RTypeId usize_type;

    *valid = false;
    if (!r_semantic_type_from_token(body->frontend, R_TOKEN_KW_USIZE, &usize_type) ||
        !r_body_lower_value(body, value_ref, usize_type, &operand)) {
        return false;
    }
    if (!operand.valid) {
        return true;
    }
    if (!r_body_constant_scalar(body, operand.node, &constant) || !constant.valid ||
        !r_semantic_type_is_integer(body->frontend, operand.type)) {
        return r_body_diagnostic(body,
                                 "R-DIAG-TYPE-001",
                                 rule_id,
                                 "reflection field index shall be an integer constant expression",
                                 span);
    }
    *index = constant.bits;
    *valid = true;
    return true;
}

static bool r_reflection_lower_type_call(RBodyContext *body,
                                         RAstRef call,
                                         RTypeId contextual_type,
                                         RExpressionResult *result) {
    RAstNodeView view;
    RAstRef qualified = {0};
    RAstRef type_ref = {0};
    RAstRef value_ref = {0};
    RReflectionTypeOperation operation = R_REFLECTION_TYPE_INVALID;
    const char *rule_id = "R-REFL-0001";
    RTypeId queried;
    RTypeId usize_type;
    uint32_t child_index;
    size_t diagnostics_before;
    bool dependent;
    const RSemanticAggregate *aggregate;

    (void)contextual_type;
    r_body_expression_invalid(result);
    if (!r_ast_view_kind(body->frontend, call, R_SYNTAX_STANDARD_TYPE_CALL, &view)) {
        body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    for (child_index = 0U; child_index < view.child_count; ++child_index) {
        RAstRef child;
        RAstNodeView child_view;

        if (!r_ast_ref_child(body->frontend, call, child_index, &child) ||
            !r_ast_ref_view(body->frontend, child, &child_view)) {
            continue;
        }
        if (child_view.kind == R_SYNTAX_QUALIFIED_NAME) {
            qualified = child;
        } else if ((child_view.kind == R_SYNTAX_TYPE) && (type_ref.node == R_AST_NODE_ID_INVALID)) {
            type_ref = child;
        } else {
            value_ref = child;
        }
    }
    if ((qualified.node == R_AST_NODE_ID_INVALID) ||
        !r_reflection_type_operation(body->frontend, qualified, &operation, &rule_id) ||
        (type_ref.node == R_AST_NODE_ID_INVALID) ||
        !r_semantic_type_from_token(body->frontend, R_TOKEN_KW_USIZE, &usize_type)) {
        body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    diagnostics_before = body->frontend->diagnostic_count;
    if (!r_semantic_type_from_ast(body->frontend, type_ref, &queried)) {
        if (body->frontend->resource_status != R_FRONTEND_OK) {
            return false;
        }
        return r_body_report_type_failure(
            body, type_ref, view.span, diagnostics_before, "reflection type operand");
    }
    dependent = r_reflection_type_is_dependent(body->frontend, queried, 0U);
    {
        const RSemanticAggregate *standard_enum;

        if (!r_semantic_prepare_standard_enum(body->frontend, queried, &standard_enum)) {
            return false;
        }
    }
    switch (operation) {
    case R_REFLECTION_TYPE_ENUM_COUNT:
    case R_REFLECTION_TYPE_ENUM_MIN:
    case R_REFLECTION_TYPE_ENUM_MAX:
    case R_REFLECTION_TYPE_ENUM_VARIANTS:
        aggregate = r_reflection_fieldless_enum(body->frontend, queried);
        if ((aggregate == NULL) && dependent && (operation != R_REFLECTION_TYPE_ENUM_VARIANTS)) {
            const RStandardCallOperation dependent_operation =
                operation == R_REFLECTION_TYPE_ENUM_COUNT ? R_STANDARD_CALL_CORE_REFLECT_ENUM_COUNT
                : operation == R_REFLECTION_TYPE_ENUM_MIN ? R_STANDARD_CALL_CORE_REFLECT_ENUM_MIN
                                                          : R_STANDARD_CALL_CORE_REFLECT_ENUM_MAX;

            return r_reflection_append_call(body,
                                            view.span,
                                            dependent_operation,
                                            operation == R_REFLECTION_TYPE_ENUM_COUNT ? usize_type
                                                                                      : queried,
                                            queried,
                                            UINT64_C(0),
                                            NULL,
                                            result);
        }
        if (aggregate == NULL) {
            return r_body_diagnostic(body,
                                     "R-DIAG-TYPE-001",
                                     rule_id,
                                     dependent ? "enum_variants requires a concrete fieldless enum"
                                               : "enum reflection requires a fieldless enum type",
                                     view.span);
        }
        if (operation == R_REFLECTION_TYPE_ENUM_COUNT) {
            return r_reflection_append_usize(
                body, view.span, (uint64_t)aggregate->variant_count, result);
        }
        if (operation == R_REFLECTION_TYPE_ENUM_VARIANTS) {
            return r_reflection_lower_enum_variants(body, view.span, aggregate, result);
        }
        if (!r_reflection_append_enum_constant(
                body,
                view.span,
                aggregate,
                r_reflection_enum_extreme(
                    body->frontend, aggregate, operation == R_REFLECTION_TYPE_ENUM_MAX),
                &result->node)) {
            return false;
        }
        result->type = aggregate->type;
        result->valid = true;
        return true;
    case R_REFLECTION_TYPE_ENUM_AT:
    case R_REFLECTION_TYPE_ENUM_FROM_NAME: {
        const bool at = operation == R_REFLECTION_TYPE_ENUM_AT;
        RExpressionResult operand;
        RTypeId operand_type;
        RTypeId option_type;
        RHirNodeId child_storage[1];
        RHirVector children;

        aggregate = r_reflection_fieldless_enum(body->frontend, queried);
        if ((aggregate == NULL) && !dependent) {
            return r_body_diagnostic(body,
                                     "R-DIAG-TYPE-001",
                                     rule_id,
                                     "enum reflection requires a fieldless enum type",
                                     view.span);
        }
        if (value_ref.node == R_AST_NODE_ID_INVALID) {
            body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
            return false;
        }
        if (at ? !r_semantic_type_from_token(body->frontend, R_TOKEN_KW_USIZE, &operand_type)
               : !r_semantic_type_from_token(body->frontend, R_TOKEN_KW_STR, &operand_type)) {
            return false;
        }
        if (at ? !r_body_lower_value(body, value_ref, operand_type, &operand)
               : !r_body_lower_call_bounded_value(body, value_ref, operand_type, &operand)) {
            return false;
        }
        if (!operand.valid) {
            return true;
        }
        if (r_semantic_value_type(body->frontend, operand.type) != operand_type) {
            return r_body_diagnostic(body,
                                     "R-DIAG-TYPE-001",
                                     rule_id,
                                     at ? "enum_at takes a usize index"
                                        : "enum_from_name takes a str name",
                                     view.span);
        }
        if (!r_semantic_intern_type(
                body->frontend, R_SEMANTIC_TYPE_OPTION, queried, &option_type)) {
            return false;
        }
        child_storage[0] = operand.node;
        children.items = child_storage;
        children.count = UINT32_C(1);
        children.capacity = UINT32_C(1);
        return r_reflection_append_call(body,
                                        view.span,
                                        at ? R_STANDARD_CALL_CORE_ENUM_AT
                                           : R_STANDARD_CALL_CORE_ENUM_FROM_NAME,
                                        option_type,
                                        queried,
                                        UINT64_C(0),
                                        &children,
                                        result);
    }
    case R_REFLECTION_TYPE_VARIANT_COUNT:
        aggregate = r_reflection_tagged_enum(body->frontend, queried);
        if ((aggregate == NULL) && dependent) {
            return r_reflection_append_call(body,
                                            view.span,
                                            R_STANDARD_CALL_CORE_REFLECT_VARIANT_COUNT,
                                            usize_type,
                                            queried,
                                            UINT64_C(0),
                                            NULL,
                                            result);
        }
        if (aggregate == NULL) {
            return r_body_diagnostic(body,
                                     "R-DIAG-TYPE-001",
                                     rule_id,
                                     "variant reflection requires a tagged union type",
                                     view.span);
        }
        return r_reflection_append_usize(
            body, view.span, (uint64_t)aggregate->variant_count, result);
    case R_REFLECTION_TYPE_TYPE_NAME: {
        RReflectionText text = {0};
        RTypeId text_type;
        bool success;

        if (dependent) {
            return r_semantic_intern_type(body->frontend,
                                          R_SEMANTIC_TYPE_CONSTEXPR_STR,
                                          R_TYPE_ID_INVALID,
                                          &text_type) &&
                   r_reflection_append_call(body,
                                            view.span,
                                            R_STANDARD_CALL_CORE_REFLECT_TYPE_NAME,
                                            text_type,
                                            queried,
                                            UINT64_C(0),
                                            NULL,
                                            result);
        }
        if (!r_reflection_spell_type(body->frontend, queried, &text, 0U)) {
            r_context_free(body->frontend, text.bytes);
            return false;
        }
        success = r_reflection_append_text_node(body, view.span, text.bytes, text.length, result);
        r_context_free(body->frontend, text.bytes);
        return success;
    }
    case R_REFLECTION_TYPE_FIELD_COUNT:
    case R_REFLECTION_TYPE_FIELD_NAME: {
        const bool count = operation == R_REFLECTION_TYPE_FIELD_COUNT;
        uint64_t index = UINT64_C(0);
        bool index_valid = true;
        const RSemanticField *field;

        aggregate = r_reflection_struct(body->frontend, queried);
        if (!count) {
            if (value_ref.node == R_AST_NODE_ID_INVALID) {
                body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
                return false;
            }
            if (!r_reflection_constant_index(
                    body, value_ref, view.span, rule_id, &index, &index_valid)) {
                return false;
            }
            if (!index_valid) {
                return true;
            }
        }
        if ((aggregate == NULL) && dependent) {
            RTypeId text_type;

            if (count) {
                return r_reflection_append_call(body,
                                                view.span,
                                                R_STANDARD_CALL_CORE_REFLECT_FIELD_COUNT,
                                                usize_type,
                                                queried,
                                                UINT64_C(0),
                                                NULL,
                                                result);
            }
            return r_semantic_intern_type(body->frontend,
                                          R_SEMANTIC_TYPE_CONSTEXPR_STR,
                                          R_TYPE_ID_INVALID,
                                          &text_type) &&
                   r_reflection_append_call(body,
                                            view.span,
                                            R_STANDARD_CALL_CORE_REFLECT_FIELD_NAME,
                                            text_type,
                                            queried,
                                            index,
                                            NULL,
                                            result);
        }
        if (aggregate == NULL) {
            return r_body_diagnostic(body,
                                     "R-DIAG-TYPE-001",
                                     rule_id,
                                     "field reflection requires a complete struct type",
                                     view.span);
        }
        if (count) {
            return r_reflection_append_usize(
                body, view.span, (uint64_t)aggregate->field_count, result);
        }
        field = (index < (uint64_t)aggregate->field_count)
                    ? r_reflection_field(body->frontend, aggregate, (uint32_t)index)
                    : NULL;
        if (field == NULL) {
            return r_body_diagnostic(body,
                                     "R-DIAG-CONST-001",
                                     rule_id,
                                     "reflection field index is outside the field count",
                                     view.span);
        }
        {
            const char *bytes;
            size_t length;

            if (!r_semantic_intern_text(body->frontend, field->name_intern_id, &bytes, &length)) {
                body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
                return false;
            }
            return r_reflection_append_text_node(body, view.span, bytes, length, result);
        }
    }
    case R_REFLECTION_TYPE_INVALID:
    default:
        body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
}

/* ---- value-operand forms ---- */

static const char *r_reflection_profile_name(RFrontendProfile profile) {
    switch (profile) {
    case R_FRONTEND_PROFILE_FREESTANDING:
        return "freestanding";
    case R_FRONTEND_PROFILE_ALLOCATION:
        return "allocation";
    case R_FRONTEND_PROFILE_HOSTED:
        return "hosted";
    case R_FRONTEND_PROFILE_HOSTED_THREAD:
        return "hosted-thread";
    case R_FRONTEND_PROFILE_HOSTED_NATIVE_ASYNC:
    default:
        return "hosted-native-async";
    }
}

static bool r_reflection_lower_value_call(RBodyContext *body,
                                          RAstRef argument_list,
                                          const RAstNodeView *arguments_view,
                                          RSourceSpan call_span,
                                          RSourceSpan operation_span,
                                          RStandardCallOperation operation,
                                          RExpressionResult *result) {
    const char *rule_id = operation == R_STANDARD_CALL_CORE_VARIANT_NAME ? "R-REFL-0002"
                          : ((operation == R_STANDARD_CALL_CORE_TARGET_NAME) ||
                             (operation == R_STANDARD_CALL_CORE_PROFILE_NAME) ||
                             (operation == R_STANDARD_CALL_CORE_LOCATION))
                              ? "R-REFL-0004"
                              : "R-REFL-0001";
    RAstRef argument;
    RExpressionResult operand;
    RTypeId operand_value_type;
    RTypeId result_type;
    RHirNodeId child_storage[1];
    RHirVector children;
    const RSemanticType *type;
    bool dependent;

    (void)operation_span;
    r_body_expression_invalid(result);
    if (operation == R_STANDARD_CALL_CORE_LOCATION) {
        /* M44.3: the module of the source that holds the call and the line of the call. */
        const char *module_name = r_frontend_source_module_name(body->frontend, call_span.source);
        uint32_t line = UINT32_C(0);
        uint32_t column = UINT32_C(0);
        char text[512];
        int length;

        if (arguments_view->child_count != UINT32_C(0)) {
            return r_body_diagnostic(body,
                                     "R-DIAG-TYPE-001",
                                     rule_id,
                                     "core::location takes no arguments",
                                     call_span);
        }
        r_frontend_source_position(body->frontend, call_span, &line, &column);
        length = snprintf(text,
                          sizeof(text),
                          "%s:%u",
                          module_name == NULL ? "" : module_name,
                          (unsigned int)line);
        if ((length < 0) || ((size_t)length >= sizeof(text))) {
            body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
            return false;
        }
        return r_reflection_append_text_node(body, call_span, text, (size_t)length, result);
    }
    if ((operation == R_STANDARD_CALL_CORE_TARGET_NAME) ||
        (operation == R_STANDARD_CALL_CORE_PROFILE_NAME)) {
        const char *text = operation == R_STANDARD_CALL_CORE_TARGET_NAME
                               ? r_target_identity_triple
                               : r_reflection_profile_name(body->frontend->profile);

        if (arguments_view->child_count != UINT32_C(0)) {
            return r_body_diagnostic(body,
                                     "R-DIAG-TYPE-001",
                                     rule_id,
                                     "target and profile reflection take no arguments",
                                     call_span);
        }
        return r_reflection_append_text_node(body, call_span, text, strlen(text), result);
    }
    if (arguments_view->child_count != UINT32_C(1)) {
        return r_body_diagnostic(body,
                                 "R-DIAG-TYPE-001",
                                 rule_id,
                                 "value reflection takes exactly one argument",
                                 call_span);
    }
    if (!r_ast_ref_child(body->frontend, argument_list, UINT32_C(0), &argument)) {
        body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    if (operation == R_STANDARD_CALL_CORE_VARIANT_NAME) {
        /* `const T* value`: a shared borrow of a tagged union, as core::hash takes its key. */
        if (!r_generic_lower_argument(
                body, argument, R_TYPE_ID_INVALID, R_SEMANTIC_TYPE_FLAG_SHARED, &operand) ||
            !r_body_place_to_value(body, &operand)) {
            return false;
        }
        if (!operand.valid) {
            return true;
        }
        type = r_semantic_type(body->frontend, operand.type);
        if ((type != NULL) && (type->kind == R_SEMANTIC_TYPE_BORROW) &&
            (type->flags == R_SEMANTIC_TYPE_FLAG_NONE)) {
            RTypeId shared_type;

            if (!r_semantic_intern_derived_type(body->frontend,
                                                R_SEMANTIC_TYPE_BORROW,
                                                type->base,
                                                R_TYPE_ID_INVALID,
                                                UINT64_C(0),
                                                R_SEMANTIC_TYPE_FLAG_SHARED,
                                                &shared_type) ||
                !r_body_finish_value(body, shared_type, &operand)) {
                return false;
            }
            type = r_semantic_type(body->frontend, operand.type);
        }
        if ((type == NULL) || (type->kind != R_SEMANTIC_TYPE_BORROW) ||
            (type->flags != R_SEMANTIC_TYPE_FLAG_SHARED)) {
            return r_body_diagnostic(body,
                                     "R-DIAG-TYPE-001",
                                     rule_id,
                                     "variant_name takes a shared borrow of a tagged union",
                                     call_span);
        }
        operand_value_type = r_semantic_value_type(body->frontend, type->base);
        dependent = r_reflection_type_is_dependent(body->frontend, operand_value_type, 0U);
        if ((r_reflection_tagged_enum(body->frontend, operand_value_type) == NULL) && !dependent) {
            return r_body_diagnostic(body,
                                     "R-DIAG-TYPE-001",
                                     rule_id,
                                     "variant_name takes a shared borrow of a tagged union",
                                     call_span);
        }
        if (!r_semantic_intern_type(
                body->frontend, R_SEMANTIC_TYPE_CONSTEXPR_STR, R_TYPE_ID_INVALID, &result_type)) {
            return false;
        }
    } else {
        if (!r_body_lower_value(body, argument, R_TYPE_ID_INVALID, &operand)) {
            return false;
        }
        if (!operand.valid) {
            return true;
        }
        operand_value_type = r_semantic_value_type(body->frontend, operand.type);
        dependent = r_reflection_type_is_dependent(body->frontend, operand_value_type, 0U);
        {
            const RSemanticAggregate *standard_enum;

            if (!r_semantic_prepare_standard_enum(
                    body->frontend, operand_value_type, &standard_enum)) {
                return false;
            }
        }
        if ((r_reflection_fieldless_enum(body->frontend, operand_value_type) == NULL) &&
            !dependent) {
            return r_body_diagnostic(body,
                                     "R-DIAG-TYPE-001",
                                     rule_id,
                                     "enum reflection takes a fieldless enum value",
                                     call_span);
        }
        if (operation == R_STANDARD_CALL_CORE_ENUM_ORDINAL
                ? !r_semantic_type_from_token(body->frontend, R_TOKEN_KW_USIZE, &result_type)
                : !r_semantic_intern_type(body->frontend,
                                          R_SEMANTIC_TYPE_CONSTEXPR_STR,
                                          R_TYPE_ID_INVALID,
                                          &result_type)) {
            return false;
        }
    }
    child_storage[0] = operand.node;
    children.items = child_storage;
    children.count = UINT32_C(1);
    children.capacity = UINT32_C(1);
    return r_reflection_append_call(body,
                                    call_span,
                                    operation,
                                    result_type,
                                    operand_value_type,
                                    UINT64_C(0),
                                    &children,
                                    result);
}

/* ---- instantiation ---- */

static bool r_reflection_is_operation(RStandardCallOperation operation) {
    return ((operation >= R_STANDARD_CALL_CORE_ENUM_NAME) &&
            (operation <= R_STANDARD_CALL_CORE_REFLECT_FIELD_NAME)) ||
           (operation == R_STANDARD_CALL_CORE_LOCATION);
}

/* R-REFL-0001..0003 at instantiation: a dependent form whose operand type became concrete
 * folds to its literal; a runtime selection keeps its node but proves its operand type. */
static bool r_reflection_fold_clone(RFrontendContext *context, RHirNode *node) {
    const RTypeId operand_type = r_semantic_value_type(context, node->auxiliary_type);
    const RSemanticAggregate *aggregate;
    const char *rule_id = "R-REFL-0001";
    const char *message = NULL;

    if ((node->kind != R_HIR_STANDARD_CALL) ||
        !r_reflection_is_operation(node->standard_operation) ||
        r_reflection_type_is_dependent(context, operand_type, 0U)) {
        return true;
    }
    aggregate = NULL;
    if (!r_semantic_prepare_standard_enum(context, operand_type, &aggregate)) {
        return false;
    }
    switch (node->standard_operation) {
    case R_STANDARD_CALL_CORE_ENUM_NAME:
    case R_STANDARD_CALL_CORE_ENUM_ORDINAL:
    case R_STANDARD_CALL_CORE_ENUM_AT:
    case R_STANDARD_CALL_CORE_ENUM_FROM_NAME:
        if (r_reflection_fieldless_enum(context, operand_type) == NULL) {
            message = "enum reflection requires a fieldless enum type";
        }
        break;
    case R_STANDARD_CALL_CORE_VARIANT_NAME:
        rule_id = "R-REFL-0002";
        if (r_reflection_tagged_enum(context, operand_type) == NULL) {
            message = "variant_name takes a shared borrow of a tagged union";
        }
        break;
    case R_STANDARD_CALL_CORE_REFLECT_ENUM_COUNT:
    case R_STANDARD_CALL_CORE_REFLECT_ENUM_MIN:
    case R_STANDARD_CALL_CORE_REFLECT_ENUM_MAX:
        aggregate = r_reflection_fieldless_enum(context, operand_type);
        if (aggregate == NULL) {
            message = "enum reflection requires a fieldless enum type";
            break;
        }
        if (node->standard_operation == R_STANDARD_CALL_CORE_REFLECT_ENUM_COUNT) {
            node->kind = R_HIR_LITERAL;
            node->integer_value = (uint64_t)aggregate->variant_count;
        } else {
            const uint32_t index = r_reflection_enum_extreme(
                context,
                aggregate,
                node->standard_operation == R_STANDARD_CALL_CORE_REFLECT_ENUM_MAX);
            const RSemanticVariant *variant = r_reflection_variant(context, aggregate, index);

            if (variant == NULL) {
                return false;
            }
            node->kind = R_HIR_ENUM_CONSTANT;
            node->type = aggregate->type;
            node->integer_value = variant->value;
            node->aggregate_member = aggregate->first_variant + index + UINT32_C(1);
        }
        break;
    case R_STANDARD_CALL_CORE_REFLECT_VARIANT_COUNT:
        rule_id = "R-REFL-0002";
        aggregate = r_reflection_tagged_enum(context, operand_type);
        if (aggregate == NULL) {
            message = "variant reflection requires a tagged union type";
            break;
        }
        node->kind = R_HIR_LITERAL;
        node->integer_value = (uint64_t)aggregate->variant_count;
        break;
    case R_STANDARD_CALL_CORE_REFLECT_TYPE_NAME: {
        RReflectionText text = {0};
        uint32_t intern_id = UINT32_C(0);
        bool interned;

        rule_id = "R-REFL-0003";
        if (!r_reflection_spell_type(context, operand_type, &text, 0U)) {
            r_context_free(context, text.bytes);
            return false;
        }
        interned = r_intern_bytes(context, (const uint8_t *)text.bytes, text.length, &intern_id);
        r_context_free(context, text.bytes);
        if (!interned) {
            return false;
        }
        node->kind = R_HIR_STRING_LITERAL;
        node->integer_value = (uint64_t)text.length;
        node->aggregate_member = intern_id;
        break;
    }
    case R_STANDARD_CALL_CORE_REFLECT_FIELD_COUNT:
    case R_STANDARD_CALL_CORE_REFLECT_FIELD_NAME: {
        const RSemanticField *field;
        const char *bytes;
        size_t length;
        uint32_t intern_id = UINT32_C(0);

        rule_id = "R-REFL-0003";
        aggregate = r_reflection_struct(context, operand_type);
        if (aggregate == NULL) {
            message = "field reflection requires a complete struct type";
            break;
        }
        if (node->standard_operation == R_STANDARD_CALL_CORE_REFLECT_FIELD_COUNT) {
            node->kind = R_HIR_LITERAL;
            node->integer_value = (uint64_t)aggregate->field_count;
            break;
        }
        field = (node->integer_value < (uint64_t)aggregate->field_count)
                    ? r_reflection_field(context, aggregate, (uint32_t)node->integer_value)
                    : NULL;
        if (field == NULL) {
            return r_semantic_add_diagnostic(context,
                                             "R-DIAG-CONST-001",
                                             rule_id,
                                             "reflection field index is outside the field count",
                                             node->span);
        }
        if (!r_semantic_intern_text(context, field->name_intern_id, &bytes, &length) ||
            !r_intern_bytes(context, (const uint8_t *)bytes, length, &intern_id)) {
            return false;
        }
        node->kind = R_HIR_STRING_LITERAL;
        node->integer_value = (uint64_t)length;
        node->aggregate_member = intern_id;
        break;
    }
    default:
        return true;
    }
    if (message != NULL) {
        return r_semantic_add_diagnostic(context, "R-DIAG-TYPE-001", rule_id, message, node->span);
    }
    if (node->kind != R_HIR_STANDARD_CALL) {
        node->standard_operation = R_STANDARD_CALL_INVALID;
        node->auxiliary_type = R_TYPE_ID_INVALID;
        node->runtime_type = R_TYPE_ID_INVALID;
    }
    return true;
}

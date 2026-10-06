/* Field contracts are part of the semantic schema, independent of R object layout. */
static bool
r_json_attribute_diagnostic(RFrontendContext *context, RSourceSpan span, const char *message) {
    return r_semantic_add_diagnostic(context, "R-DIAG-JSON-001", "R-JSON-0001", message, span);
}
static uint32_t r_json_attribute_flag(const RFrontendContext *context, const RAstTokenView *key) {
    static const struct {
        const char *name;
        uint32_t flag;
    } parameters[] = {{"name", R_JSON_FIELD_NAME},
                      {"skip", R_JSON_FIELD_SKIP},
                      {"omitempty", R_JSON_FIELD_OMITEMPTY},
                      {"omitzero", R_JSON_FIELD_OMITZERO},
                      {"string", R_JSON_FIELD_STRING},
                      {"case", R_JSON_FIELD_CASE},
                      {"embed", R_JSON_FIELD_EMBED},
                      {"optional", R_JSON_FIELD_OPTIONAL},
                      {"default", R_JSON_FIELD_DEFAULT},
                      {"omitnone", R_JSON_FIELD_OMITNONE},
                      {"description", R_JSON_FIELD_DESCRIPTION}};
    for (size_t i = 0U; i < sizeof(parameters) / sizeof(parameters[0]); ++i)
        if (r_semantic_token_text_equal_owned(context, key, parameters[i].name))
            return parameters[i].flag;
    return 0U;
}
static bool
r_json_attribute_literal(const RFrontendContext *context, RAstRef ref, RAstTokenView *token) {
    RAstNodeView view;
    if (r_ast_ref_token(context, ref, token))
        return true;
    if (!r_ast_ref_view(context, ref, &view) || view.child_count != 1U ||
        !r_ast_ref_child(context, ref, 0U, &ref))
        return false;
    return r_json_attribute_literal(context, ref, token);
}
static bool r_json_attribute_default_form(const RFrontendContext *context, RAstRef ref) {
    RAstNodeView view;
    RAstTokenView token;
    if (r_ast_ref_token(context, ref, &token)) {
        return token.kind == R_TOKEN_INTEGER_LITERAL || token.kind == R_TOKEN_FLOAT_LITERAL ||
               token.kind == R_TOKEN_CHARACTER_LITERAL || token.kind == R_TOKEN_STRING_LITERAL ||
               token.kind == R_TOKEN_KW_TRUE || token.kind == R_TOKEN_KW_FALSE ||
               token.kind == R_TOKEN_IDENTIFIER || token.kind == R_TOKEN_DOT ||
               token.kind == R_TOKEN_COLON_COLON || token.kind == R_TOKEN_MINUS;
    }
    if (!r_ast_ref_view(context, ref, &view))
        return false;
    if (view.kind == R_SYNTAX_BINARY_EXPRESSION && view.child_count == 1U) {
        RAstRef only;
        return r_ast_ref_child(context, ref, 0U, &only) &&
               r_json_attribute_default_form(context, only);
    }
    if (view.kind != R_SYNTAX_EXPRESSION && view.kind != R_SYNTAX_STRING_SEQUENCE &&
        view.kind != R_SYNTAX_POSTFIX_EXPRESSION && view.kind != R_SYNTAX_PRIMARY_EXPRESSION &&
        view.kind != R_SYNTAX_UNARY_EXPRESSION && view.kind != R_SYNTAX_QUALIFIED_NAME)
        return false;
    for (uint32_t i = 0U; i < view.child_count; ++i) {
        RAstRef child;
        if (!r_ast_ref_child(context, ref, i, &child) ||
            !r_json_attribute_default_form(context, child))
            return false;
    }
    return true;
}
static bool r_json_collect_field_attributes(RFrontendContext *context,
                                            RAstRef field_ref,
                                            RSemanticField *field,
                                            RSemanticAggregate *aggregate) {
    RAstNodeView field_view;
    if (!r_ast_ref_view(context, field_ref, &field_view))
        return false;
    for (uint32_t i = 0U; i < field_view.child_count; ++i) {
        RAstRef attribute;
        RAstNodeView view;
        RAstTokenView name;
        if (!r_ast_ref_child(context, field_ref, i, &attribute) ||
            !r_ast_ref_view(context, attribute, &view))
            return false;
        if (view.kind != R_SYNTAX_ATTRIBUTE)
            continue;
        if (r_semantic_attribute_is_user(context, attribute)) {
            /* R-AGG-0013 (L42): an attribute of a struct field, resolved with every type. */
            if (!aggregate->is_error && (aggregate->generic_origin == 0U) &&
                !r_attribute_record_use(context,
                                        attribute,
                                        R_ATTRIBUTE_TARGET_FIELD,
                                        aggregate->type,
                                        field->layout_index))
                return false;
            continue;
        }
        if (!r_ast_direct_token(context, attribute, 1U, NULL, &name) ||
            !r_semantic_token_text_equal_owned(context, &name, "json")) {
            /* A field takes only @json; an FFI attribute has its closed scopes (R-FFI-0050). */
            static const char *const ffi_attributes[] = {"link",
                                                         "header",
                                                         "abi",
                                                         "link_name",
                                                         "safety",
                                                         "fenv",
                                                         "repr",
                                                         "c_type",
                                                         "c_constant",
                                                         "export_name",
                                                         "callback"};
            bool ffi = false;
            for (size_t index = 0U; index < sizeof(ffi_attributes) / sizeof(ffi_attributes[0]);
                 ++index)
                ffi =
                    ffi || r_semantic_token_text_equal_owned(context, &name, ffi_attributes[index]);
            aggregate->poisoned = true;
            if (!r_semantic_add_diagnostic(context,
                                           ffi ? "R-DIAG-FFI-003" : "R-DIAG-SYN-002",
                                           ffi ? "R-FFI-0050" : "R-GRAM-0007",
                                           "a struct field accepts only the @json attribute",
                                           view.span))
                return false;
            continue;
        }
        if (field->json.present) {
            aggregate->poisoned = true;
            if (!r_json_attribute_diagnostic(
                    context, view.span, "a field permits only one @json attribute"))
                return false;
            continue;
        }
        field->json.present = true;
        field->json.span = view.span;
        for (uint32_t member = 2U; member < view.child_count; ++member) {
            RAstRef part, value = {0};
            RAstTokenView key, next;
            uint32_t flag;
            bool has_value = false;
            bool needs_value;
            if (!r_ast_ref_child(context, attribute, member, &part) ||
                !r_ast_ref_token(context, part, &key))
                return false;
            if (key.kind == R_TOKEN_COMMA || key.kind == R_TOKEN_RPAREN)
                continue;
            if (member + 1U < view.child_count &&
                r_ast_ref_child(context, attribute, member + 1U, &part) &&
                r_ast_ref_token(context, part, &next) && next.kind == R_TOKEN_EQUAL) {
                has_value = true;
                if (!r_ast_ref_child(context, attribute, member + 2U, &value))
                    return false;
                member += 2U;
            }
            flag = r_json_attribute_flag(context, &key);
            if (flag == 0U) {
                const char *message = "unknown @json parameter";
                if (r_semantic_token_text_equal_owned(context, &key, "inline"))
                    message = "JSON inline was replaced by embed";
                else if (r_semantic_token_text_equal_owned(context, &key, "format") ||
                         r_semantic_token_text_equal_owned(context, &key, "unknown"))
                    message = "the format and unknown @json parameters were removed";
                else if (r_semantic_token_text_equal_owned(context, &key, "omit_empty"))
                    message = "the canonical JSON parameter is omitempty";
                else if (r_semantic_token_text_equal_owned(context, &key, "omit_none"))
                    message = "the canonical JSON parameter is omitnone";
                aggregate->poisoned = true;
                if (!r_json_attribute_diagnostic(context, key.span, message))
                    return false;
                continue;
            }
            if ((field->json.flags & flag) != 0U) {
                aggregate->poisoned = true;
                if (!r_json_attribute_diagnostic(context, key.span, "duplicate @json parameter"))
                    return false;
            }
            field->json.flags |= flag;
            needs_value = flag == R_JSON_FIELD_NAME || flag == R_JSON_FIELD_CASE ||
                          flag == R_JSON_FIELD_DEFAULT || flag == R_JSON_FIELD_DESCRIPTION;
            if (has_value != needs_value) {
                aggregate->poisoned = true;
                if (!r_json_attribute_diagnostic(context,
                                                 key.span,
                                                 needs_value ? "JSON parameter requires a value"
                                                             : "JSON flag does not accept a value"))
                    return false;
                continue;
            }
            if (flag == R_JSON_FIELD_NAME || flag == R_JSON_FIELD_CASE ||
                flag == R_JSON_FIELD_DESCRIPTION) {
                RAstTokenView literal;
                uint32_t intern;
                size_t length;
                if (!r_json_attribute_literal(context, value, &literal) ||
                    literal.kind != R_TOKEN_STRING_LITERAL) {
                    aggregate->poisoned = true;
                    if (!r_json_attribute_diagnostic(
                            context,
                            key.span,
                            "JSON name, case and description require a string literal"))
                        return false;
                    continue;
                }
                if (!r_intern_string_sequence(context, literal.span, &intern, &length))
                    return false;
                if (flag == R_JSON_FIELD_NAME)
                    field->json.name_intern_id = intern;
                else if (flag == R_JSON_FIELD_DESCRIPTION)
                    field->json.description_intern_id = intern;
                else {
                    const RInternEntry *entry = &context->intern_entries[intern - 1U];
                    if (length == 6U && memcmp(entry->bytes, "ignore", 6U) == 0)
                        field->json.ignore_case = true;
                    else if (!(length == 6U && memcmp(entry->bytes, "strict", 6U) == 0)) {
                        aggregate->poisoned = true;
                        if (!r_json_attribute_diagnostic(
                                context, literal.span, "JSON case must be strict or ignore"))
                            return false;
                    }
                }
            } else if (flag == R_JSON_FIELD_DEFAULT) {
                if (!r_json_attribute_default_form(context, value)) {
                    aggregate->poisoned = true;
                    if (!r_json_attribute_diagnostic(
                            context,
                            key.span,
                            "JSON default requires a scalar or string literal, or a factory name "
                            "without arguments"))
                        return false;
                }
                field->json.default_ast = value.node;
            }
        }
        {
            uint32_t flags = field->json.flags;
            const char *message = NULL;
            if ((flags & R_JSON_FIELD_SKIP) != 0U &&
                (flags & ~(uint32_t)(R_JSON_FIELD_SKIP | R_JSON_FIELD_DEFAULT)) != 0U)
                message = "JSON skip permits only an additional default";
            else if ((flags & R_JSON_FIELD_EMBED) != 0U && flags != R_JSON_FIELD_EMBED)
                message = "JSON embed must be the only parameter on its field";
            else if ((flags & R_JSON_FIELD_DEFAULT) != 0U &&
                     (flags & (R_JSON_FIELD_OPTIONAL | R_JSON_FIELD_SKIP)) == 0U)
                message = "JSON default requires optional or skip";
            if (message != NULL) {
                aggregate->poisoned = true;
                if (!r_json_attribute_diagnostic(context, view.span, message))
                    return false;
            }
        }
    }
    return true;
}

static bool r_json_field_is_numeric(const RFrontendContext *context, RTypeId id) {
    const RSemanticType *type = r_semantic_type(context, r_semantic_value_type(context, id));
    if (type == NULL)
        return false;
    if (type->kind == R_SEMANTIC_TYPE_OPTION)
        return r_json_field_is_numeric(context, type->base);
    if (type->kind >= R_SEMANTIC_TYPE_I8 && type->kind <= R_SEMANTIC_TYPE_F64)
        return true;
    if (type->kind == R_SEMANTIC_TYPE_C_ABI_NUMERIC)
        return type->length != R_TOKEN_KW_C_BOOL;
    return r_semantic_standard_type_name_equal(context, type, "std.json::number");
}
static bool r_json_validate_field_types(RFrontendContext *context) {
    for (size_t i = 0U; i < context->semantic_field_count; ++i) {
        const RSemanticField *field = &context->semantic_fields[i];
        const RSemanticType *type =
            r_semantic_type(context, r_semantic_value_type(context, field->type));
        const RSemanticType *numeric = type != NULL && type->kind == R_SEMANTIC_TYPE_OPTION
                                           ? r_semantic_type(context, type->base)
                                           : type;
        uint32_t flags = field->json.flags;
        const char *message = NULL;
        if (!field->json.present || type == NULL)
            continue;
        if ((flags & R_JSON_FIELD_OMITNONE) != 0U && type->kind != R_SEMANTIC_TYPE_OPTION)
            message = "JSON omitnone requires an o<T> field";
        else if ((flags & R_JSON_FIELD_STRING) != 0U &&
                 !r_json_field_is_numeric(context, field->type) &&
                 numeric->kind != R_SEMANTIC_TYPE_STRUCT && numeric->kind != R_SEMANTIC_TYPE_ENUM &&
                 numeric->kind != R_SEMANTIC_TYPE_PARAMETER)
            message = "JSON string requires a numeric field or a numeric custom converter";
        else if ((flags & R_JSON_FIELD_EMBED) != 0U) {
            if (type->kind == R_SEMANTIC_TYPE_DICT) {
                const RSemanticType *key = r_semantic_type(context, type->base);
                if (!r_semantic_standard_type_name_equal(context, key, "std.string::string"))
                    message = "JSON embed requires a dictionary with owned string keys";
            } else if (type->kind != R_SEMANTIC_TYPE_STRUCT &&
                       !r_semantic_standard_type_name_equal(context, type, "std.json::value"))
                message = "JSON embed requires a structure, string-key dictionary, or JSON object";
        }
        if (message != NULL) {
            context->semantic_aggregates[field->aggregate - 1U].poisoned = true;
            if (!r_json_attribute_diagnostic(context, field->json.span, message))
                return false;
        }
    }
    return true;
}

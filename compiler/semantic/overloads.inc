/* R-FUNC-0004: overload selection is a type query. It never lowers an operand or
 * changes the ownership, borrowing, checked-effect, or async startup state. */

static uint32_t
r_overload_parameter_index(const RFrontendContext *context, RTypeId type, uint32_t schema) {
    uint32_t index;
    if (schema == 0U) {
        return UINT32_MAX;
    }
    for (index = 0U; index < context->generic_schemas[schema - 1U].parameter_count; ++index) {
        if (context
                ->generic_parameters[context->generic_schemas[schema - 1U].first_parameter + index]
                .type == type) {
            return index;
        }
    }
    return UINT32_MAX;
}

static bool r_overload_same_type(const RFrontendContext *context,
                                 RTypeId left_id,
                                 RTypeId right_id,
                                 uint32_t left_schema,
                                 uint32_t right_schema,
                                 uint32_t depth) {
    const RSemanticType *left = r_semantic_type(context, left_id);
    const RSemanticType *right = r_semantic_type(context, right_id);
    uint32_t index;
    if (left_id == right_id) {
        return true;
    }
    if (left == NULL || right == NULL || left->kind != right->kind ||
        depth > context->options.limits.max_nesting) {
        return false;
    }
    if (left->kind == R_SEMANTIC_TYPE_PARAMETER) {
        const uint32_t position = r_overload_parameter_index(context, left_id, left_schema);
        return position != UINT32_MAX &&
               position == r_overload_parameter_index(context, right_id, right_schema);
    }
    if (left->kind == R_SEMANTIC_TYPE_STRUCT || left->kind == R_SEMANTIC_TYPE_ENUM) {
        const RSemanticAggregate *a = &context->semantic_aggregates[left->base - 1U];
        const RSemanticAggregate *b = &context->semantic_aggregates[right->base - 1U];
        if (a->generic_origin == 0U || a->generic_origin != b->generic_origin) {
            return false;
        }
        for (index = 0U; index < context->generic_schemas[a->generic_schema - 1U].parameter_count;
             ++index) {
            if (!r_overload_same_type(context,
                                      context->generic_arguments[a->first_generic_argument + index],
                                      context->generic_arguments[b->first_generic_argument + index],
                                      left_schema,
                                      right_schema,
                                      depth + 1U)) {
                return false;
            }
        }
        return true;
    }
    return left->flags == right->flags && left->length == right->length &&
           r_overload_same_type(
               context, left->base, right->base, left_schema, right_schema, depth + 1U) &&
           r_overload_same_type(
               context, left->second, right->second, left_schema, right_schema, depth + 1U);
}

static bool r_overload_parameter_types_equal(const RFrontendContext *context,
                                             const RSemanticSymbol *symbol,
                                             const RTypeVector *parameters,
                                             uint32_t schema) {
    size_t index;
    if (symbol->parameter_count != parameters->count) {
        return false;
    }
    for (index = 0U; index < parameters->count; ++index) {
        if (!r_overload_same_type(
                context,
                r_semantic_value_type(
                    context,
                    context->semantic_parameter_types[symbol->first_parameter_type + index]),
                r_semantic_value_type(context, parameters->items[index]),
                symbol->generic_schema,
                schema,
                0U)) {
            return false;
        }
    }
    return true;
}

static bool r_overload_type_name(RGenericText *text, RTypeId id, uint32_t schema, uint32_t depth) {
    RFrontendContext *context = text->context;
    const RSemanticType *type = r_semantic_type(context, id);
    char suffix[80];
    int count;
    if (!r_generic_type_is_dependent(context, id, 0U) || type == NULL) {
        return r_generic_type_name(text, id, depth);
    }
    if (depth > context->options.limits.max_nesting) {
        return false;
    }
    if (type->kind == R_SEMANTIC_TYPE_PARAMETER) {
        const uint32_t position = r_overload_parameter_index(context, id, schema);
        if (position != UINT32_MAX) {
            count = snprintf(suffix, sizeof(suffix), "$%" PRIu32, position);
            return count > 0 && (size_t)count < sizeof(suffix) &&
                   r_generic_text(text, suffix, (size_t)count);
        }
        const RGenericParameter *parameter = &context->generic_parameters[type->base - 1U];
        if (parameter->pack_derived) {
            /* R-TYPE-0053 (L18.4): `(A, $0...)` keys by its prefix and its positional base. */
            RTypeId link = parameter->pack_prefix;
            if (!r_generic_text(text, "(", 1U)) {
                return false;
            }
            while (link != 0U) {
                const RSemanticType *part = r_semantic_type(context, link);
                if (part == NULL || !r_overload_type_name(text, part->base, schema, depth + 1U) ||
                    !r_generic_text(text, ",", 1U))
                    return false;
                link = part->second;
            }
            count = snprintf(suffix, sizeof(suffix), "...%" PRIu32 ")", parameter->pack_index);
            return r_overload_type_name(text, parameter->pack_base, schema, depth + 1U) &&
                   count > 0 && (size_t)count < sizeof(suffix) &&
                   r_generic_text(text, suffix, (size_t)count);
        }
        if (parameter->projection_base != 0U) {
            const RInternEntry *name = &context->intern_entries[parameter->name_intern_id - 1U];
            return r_overload_type_name(text, parameter->projection_base, schema, depth + 1U) &&
                   r_generic_text(text, "::", 2U) &&
                   r_generic_text(text, name->bytes, name->length);
        }
        if (parameter->application_base != 0U) {
            /* R-TYPE-0045 (L16.3): `P::Name<T>` keys by its base and its arguments. */
            RTypeId link = parameter->application_arguments;
            if (!r_overload_type_name(text, parameter->application_base, schema, depth + 1U) ||
                !r_generic_text(text, "<", 1U))
                return false;
            while (link != 0U) {
                const RSemanticType *part = r_semantic_type(context, link);
                if (part == NULL ||
                    (link != parameter->application_arguments && !r_generic_text(text, ",", 1U)) ||
                    !r_overload_type_name(text, part->base, schema, depth + 1U))
                    return false;
                link = part->second;
            }
            return r_generic_text(text, ">", 1U);
        }
        return r_generic_type_name(text, id, depth);
    }
    if (type->kind == R_SEMANTIC_TYPE_STRUCT || type->kind == R_SEMANTIC_TYPE_ENUM) {
        const RSemanticAggregate *aggregate = &context->semantic_aggregates[type->base - 1U];
        if (aggregate->generic_origin == 0U) {
            return r_generic_type_name(text, id, depth);
        }
        const RSemanticAggregate *origin =
            &context->semantic_aggregates[aggregate->generic_origin - 1U];
        if (!r_generic_type_name(text, origin->type, depth + 1U) ||
            !r_generic_text(text, "<", 1U)) {
            return false;
        }
        for (uint32_t index = 0U;
             index < context->generic_schemas[aggregate->generic_schema - 1U].parameter_count;
             ++index) {
            if ((index != 0U && !r_generic_text(text, ",", 1U)) ||
                !r_overload_type_name(
                    text,
                    context->generic_arguments[aggregate->first_generic_argument + index],
                    schema,
                    depth + 1U)) {
                return false;
            }
        }
        return r_generic_text(text, ">", 1U);
    }
    if (type->kind == R_SEMANTIC_TYPE_STANDARD) {
        const RInternEntry *name = &context->intern_entries[type->length - 1U];
        if (!r_generic_text(text, name->bytes, name->length)) {
            return false;
        }
    } else {
        const char *name = r_semantic_type_kind_name(type->kind);
        if (!r_generic_text(text, name, strlen(name))) {
            return false;
        }
    }
    count = snprintf(suffix,
                     sizeof(suffix),
                     ">[%" PRIu32 ",%" PRIu64 "]",
                     type->flags,
                     type->kind == R_SEMANTIC_TYPE_STANDARD ? UINT64_C(0) : type->length);
    return r_generic_text(text, "<", 1U) &&
           r_overload_type_name(text, type->base, schema, depth + 1U) &&
           r_generic_text(text, ",", 1U) &&
           r_overload_type_name(text, type->second, schema, depth + 1U) && count > 0 &&
           (size_t)count < sizeof(suffix) && r_generic_text(text, suffix, (size_t)count);
}

static bool r_overload_set_identity(RFrontendContext *context, RSymbolId id) {
    const RSemanticSymbol symbol = context->semantic_symbols[id - 1U];
    RGenericText text = {0};
    const RInternEntry *name = &context->intern_entries[symbol.name_intern_id - 1U];
    uint32_t index;
    uint32_t identity;
    bool success;
    text.context = context;
    success = r_generic_text(&text, name->bytes, name->length) && r_generic_text(&text, "(", 1U);
    for (index = 0U; success && index < symbol.parameter_count; ++index) {
        success = (index == 0U || r_generic_text(&text, ",", 1U)) &&
                  r_overload_type_name(
                      &text,
                      r_semantic_value_type(
                          context,
                          context->semantic_parameter_types[symbol.first_parameter_type + index]),
                      symbol.generic_schema,
                      0U);
    }
    success = success && r_generic_text(&text, ")", 1U) &&
              r_intern_bytes(context, text.bytes, text.length, &identity);
    if (success) {
        context->semantic_symbols[id - 1U].overload_identity = identity;
    }
    r_context_free(context, text.bytes);
    return success;
}

static bool r_overload_promoted_type(RBodyContext *body, RTypeId type, RTypeId *result) {
    const RSemanticTypeKind kind = r_semantic_value_kind(body->frontend, type);
    if (kind == R_SEMANTIC_TYPE_I8 || kind == R_SEMANTIC_TYPE_I16 || kind == R_SEMANTIC_TYPE_U8 ||
        kind == R_SEMANTIC_TYPE_U16) {
        return r_semantic_type_from_token(body->frontend, R_TOKEN_KW_I32, result);
    }
    if (r_semantic_type_is_c_abi_integer(body->frontend, type)) {
        return r_body_promoted_c_abi_integer_type(body, type, result);
    }
    *result = type;
    return true;
}

static bool r_overload_argument_type(
    RBodyContext *body, RAstRef ref, uint32_t depth, bool negative, RTypeId *result) {
    RFrontendContext *context = body->frontend;
    RAstNodeView view;
    RAstTokenView token;
    RAstRef first;
    RTypeId current = R_TYPE_ID_INVALID;
    uint32_t index;
    *result = R_TYPE_ID_INVALID;

    if (depth > r_frontend_tree_depth_limit(context) || !r_ast_ref_view(context, ref, &view)) {
        return false;
    }
    if (view.kind == R_SYNTAX_OUT_ARGUMENT) {
        RAstRef place;
        if (!r_body_ast_single_node_child(context, ref, &place) ||
            !r_overload_argument_type(body, place, depth + 1U, false, &current))
            return false;
        if (current == R_TYPE_ID_INVALID)
            return true;
        return r_semantic_intern_derived_type(context,
                                              R_SEMANTIC_TYPE_BORROW,
                                              r_semantic_value_type(context, current),
                                              0U,
                                              0U,
                                              R_SEMANTIC_TYPE_FLAG_OUT,
                                              result);
    }
    if (r_ast_ref_token(context, ref, &token)) {
        switch (token.kind) {
        case R_TOKEN_KW_THIS:
        case R_TOKEN_IDENTIFIER: {
            RBodyLookupIssue issue;
            uint32_t capture;
            if (r_closure_field_index(body, token.intern_id, &capture)) {
                *result = context->semantic_fields[capture].type;
                return true;
            }
            const RSymbolId symbol_id = r_body_resolve_name(body, &token, &issue);
            const RSemanticSymbol *symbol = r_body_symbol(body, symbol_id);
            if (symbol != NULL && symbol->kind != R_SEMANTIC_SYMBOL_FUNCTION) {
                *result = symbol->type;
            } else if (symbol != NULL && r_function_item_allowed(context, symbol_id)) {
                /* R-FUNC-0007: a function name in value position is its closed item. */
                return r_function_item_type(context, symbol_id, result);
            }
            return true;
        }
        case R_TOKEN_INTEGER_LITERAL: {
            RIntegerLiteral literal;
            if (!r_body_parse_integer_literal(body, ref, &literal)) {
                return false;
            }
            if (!literal.valid) {
                return true;
            }
            if (literal.has_explicit_type) {
                *result = literal.explicit_type;
                return true;
            }
            return r_semantic_type_from_token(
                context,
                literal.magnitude <= (uint64_t)INT32_MAX + (negative ? 1U : 0U)   ? R_TOKEN_KW_I32
                : literal.magnitude <= (uint64_t)INT64_MAX + (negative ? 1U : 0U) ? R_TOKEN_KW_I64
                                                                                  : R_TOKEN_KW_U64,
                result);
        }
        case R_TOKEN_FLOAT_LITERAL: {
            RFloatLiteral literal = {0};
            return r_semantic_parse_float_token(context, token.span, &literal) ==
                       R_FLOAT_LITERAL_VALID &&
                   r_semantic_type_from_token(context, literal.type_token, result);
        }
        case R_TOKEN_KW_NULL:
            return r_semantic_type_from_token(context, R_TOKEN_KW_NULL_T, result);
        case R_TOKEN_KW_TRUE:
        case R_TOKEN_KW_FALSE:
            return r_semantic_type_from_token(context, R_TOKEN_KW_BOOL, result);
        case R_TOKEN_CHARACTER_LITERAL:
            return r_semantic_type_from_token(context, R_TOKEN_KW_CHAR, result);
        case R_TOKEN_STRING_LITERAL:
            return r_semantic_intern_type(context, R_SEMANTIC_TYPE_CONSTEXPR_STR, 0U, result);
        default:
            return true;
        }
    }
    if (view.kind == R_SYNTAX_CONDITIONAL_EXPRESSION && view.child_count == 4U) {
        RAstRef arm;
        RTypeId other;
        if (!r_ast_ref_child(context, ref, 2U, &arm) ||
            !r_overload_argument_type(body, arm, depth + 1U, false, &current) ||
            !r_ast_ref_child(context, ref, 3U, &arm) ||
            !r_overload_argument_type(body, arm, depth + 1U, false, &other)) {
            return false;
        }
        if (r_body_conditional_converts(body, current, other)) {
            current = other;
        } else if (!r_body_conditional_converts(body, other, current)) {
            if (!r_semantic_type_from_token(context, R_TOKEN_KW_I32, &current)) {
                return false;
            }
            RTypeId left;
            if (!r_ast_ref_child(context, ref, 2U, &arm) ||
                !r_overload_argument_type(body, arm, depth + 1U, false, &left)) {
                return false;
            }
            if (!r_body_conditional_converts(body, left, current) ||
                !r_body_conditional_converts(body, other, current)) {
                current = R_TYPE_ID_INVALID;
            }
        }
        *result = current;
        return true;
    }
    if (view.kind == R_SYNTAX_STRING_SEQUENCE) {
        return r_semantic_intern_type(context, R_SEMANTIC_TYPE_CONSTEXPR_STR, 0U, result);
    }
    if (view.kind == R_SYNTAX_FORMAT_SEQUENCE) {
        return r_semantic_intern_named_standard_type(context, "std.string::string", result);
    }
    if (view.kind == R_SYNTAX_MEMBERSHIP_EXPRESSION) {
        return r_semantic_type_from_token(context, R_TOKEN_KW_BOOL, result);
    }
    if (view.kind == R_SYNTAX_NEW_EXPRESSION) {
        RAstRef allocation_type;
        if (!r_ast_find_direct_child(context, ref, R_SYNTAX_TYPE, &allocation_type) ||
            !r_semantic_type_from_ast(context, allocation_type, &current)) {
            return false;
        }
        current = r_semantic_value_type(context, current);
        const RSemanticTypeKind kind = r_semantic_value_kind(context, current);
        if (kind == R_SEMANTIC_TYPE_ARC || kind == R_SEMANTIC_TYPE_RC) {
            *result = current;
            return true;
        }
        return r_semantic_intern_derived_type(
            context, R_SEMANTIC_TYPE_OWN, current, 0U, 0U, 0U, result);
    }
    if (view.kind == R_SYNTAX_QUALIFIED_NAME) {
        RBodyLookupIssue issue;
        RAstTokenView name;
        const RSemanticSymbol *symbol =
            r_body_symbol(body, r_body_resolve_qualified_name(body, ref, &name, &issue));
        if (symbol != NULL && symbol->kind != R_SEMANTIC_SYMBOL_FUNCTION) {
            *result = symbol->type;
            return true;
        }
        if (symbol == NULL) {
            RExpressionResult selected = {0};
            uint32_t variant = 0U;
            if (!r_body_lower_enum_constant(body, ref, &selected, &variant)) {
                return false;
            }
            *result = selected.type;
        }
        return true;
    }
    if (view.kind == R_SYNTAX_PRIMARY_EXPRESSION && view.child_count > 1U) {
        RAstRef initializer;
        if (r_ast_find_direct_child(context, ref, R_SYNTAX_AGGREGATE_INITIALIZER, &initializer) &&
            r_ast_ref_child(context, ref, 0U, &first)) {
            return r_semantic_resolve_aggregate_type(context, first, result);
        }
    }
    if (view.kind == R_SYNTAX_UNARY_EXPRESSION) {
        RAstRef operand;
        const RSemanticType *type;
        if (!r_body_ast_first_token(context, ref, &token)) {
            return false;
        }
        if (token.kind == R_TOKEN_KW_SIZEOF || token.kind == R_TOKEN_KW_ALIGNOF) {
            return r_semantic_type_from_token(context, R_TOKEN_KW_USIZE, result);
        }
        if (!r_body_ast_single_node_child(context, ref, &operand) ||
            !r_overload_argument_type(
                body, operand, depth + 1U, token.kind == R_TOKEN_MINUS, &current)) {
            return false;
        }
        type = r_semantic_type(context, r_semantic_value_type(context, current));
        if (token.kind == R_TOKEN_AMP && type != NULL) {
            return r_semantic_intern_derived_type(
                context,
                R_SEMANTIC_TYPE_BORROW,
                r_semantic_value_type(context, current),
                0U,
                0U,
                r_semantic_type_is_const(context, current) ? R_SEMANTIC_TYPE_FLAG_SHARED : 0U,
                result);
        }
        if (token.kind == R_TOKEN_STAR && type != NULL) {
            *result = type->base;
            if ((type->flags & R_SEMANTIC_TYPE_FLAG_SHARED) != 0U) {
                return r_semantic_intern_type(context, R_SEMANTIC_TYPE_CONST, *result, result);
            }
        } else if (token.kind == R_TOKEN_BANG) {
            return r_semantic_type_from_token(context, R_TOKEN_KW_BOOL, result);
        } else if (token.kind == R_TOKEN_PLUS || token.kind == R_TOKEN_MINUS ||
                   token.kind == R_TOKEN_TILDE) {
            RAstTokenView literal;
            /* Signed literal syntax preserves an explicit small integer type. */
            if (token.kind != R_TOKEN_TILDE && r_body_ast_first_token(context, operand, &literal) &&
                literal.kind == R_TOKEN_INTEGER_LITERAL) {
                *result = current;
                return true;
            }
            return r_overload_promoted_type(body, current, result);
        } else {
            *result = current;
        }
        return true;
    }
    if (view.child_count == 0U || !r_ast_ref_child(context, ref, 0U, &first)) {
        return true;
    }
    if (!r_overload_argument_type(body, first, depth + 1U, negative, &current)) {
        return false;
    }
    if (view.kind == R_SYNTAX_BINARY_EXPRESSION) {
        for (index = 1U; index + 1U < view.child_count; index += 2U) {
            RAstRef operation;
            RAstRef right;
            RTypeId right_type;
            if (!r_ast_ref_child(context, ref, index, &operation) ||
                !r_ast_ref_token(context, operation, &token) ||
                !r_ast_ref_child(context, ref, index + 1U, &right)) {
                return false;
            }
            if (token.kind == R_TOKEN_KW_AS) {
                if (!r_semantic_type_from_ast(context, right, &current)) {
                    return false;
                }
            } else if (token.kind == R_TOKEN_EQUAL_EQUAL || token.kind == R_TOKEN_BANG_EQUAL ||
                       token.kind == R_TOKEN_LESS || token.kind == R_TOKEN_LESS_EQUAL ||
                       token.kind == R_TOKEN_GREATER || token.kind == R_TOKEN_GREATER_EQUAL ||
                       token.kind == R_TOKEN_AMP_AMP || token.kind == R_TOKEN_PIPE_PIPE) {
                if (!r_semantic_type_from_token(context, R_TOKEN_KW_BOOL, &current)) {
                    return false;
                }
            } else {
                if (!r_overload_argument_type(body, right, depth + 1U, false, &right_type)) {
                    return false;
                }
                if (!r_overload_promoted_type(body, current, &current) ||
                    !r_overload_promoted_type(body, right_type, &right_type)) {
                    return false;
                }
                const RSemanticTypeKind left_kind = r_semantic_value_kind(context, current);
                const RSemanticTypeKind right_kind = r_semantic_value_kind(context, right_type);
                if (token.kind == R_TOKEN_LESS_LESS || token.kind == R_TOKEN_GREATER_GREATER) {
                    continue;
                }
                if (r_semantic_kind_is_integer(left_kind) &&
                    r_semantic_kind_is_integer(right_kind)) {
                    if (!r_body_usual_integer_type(body, current, right_type, &current)) {
                        return false;
                    }
                } else if (left_kind == R_SEMANTIC_TYPE_C_ABI_NUMERIC &&
                           right_kind == R_SEMANTIC_TYPE_C_ABI_NUMERIC) {
                    bool is_float;
                    if (!r_body_common_c_abi_numeric_type(
                            body, current, right_type, &current, &is_float)) {
                        current = R_TYPE_ID_INVALID;
                    }
                } else if ((r_body_is_float_kind(left_kind) ||
                            r_semantic_kind_is_integer(left_kind)) &&
                           (r_body_is_float_kind(right_kind) ||
                            r_semantic_kind_is_integer(right_kind))) {
                    if (!r_semantic_type_from_token(context,
                                                    left_kind == R_SEMANTIC_TYPE_F64 ||
                                                            right_kind == R_SEMANTIC_TYPE_F64
                                                        ? R_TOKEN_KW_F64
                                                        : R_TOKEN_KW_F32,
                                                    &current)) {
                        return false;
                    }
                } else {
                    current = R_TYPE_ID_INVALID;
                }
            }
        }
    } else if (view.kind == R_SYNTAX_POSTFIX_EXPRESSION) {
        for (index = 1U; index < view.child_count; ++index) {
            RAstRef suffix;
            RAstNodeView suffix_view;
            const RSemanticType *type =
                r_semantic_type(context, r_semantic_value_type(context, current));
            if (!r_ast_ref_child(context, ref, index, &suffix) ||
                !r_ast_ref_view(context, suffix, &suffix_view)) {
                return false;
            }
            if (suffix_view.kind == R_SYNTAX_MEMBER_SUFFIX) {
                const RSemanticAggregate *aggregate;
                uint32_t field;
                bool readonly = r_semantic_type_is_const(context, current);
                if (r_ast_has_direct_token(context, suffix, R_TOKEN_ARROW) && type != NULL) {
                    current = type->base;
                    readonly = (type->flags & R_SEMANTIC_TYPE_FLAG_SHARED) != 0U;
                }
                aggregate = r_semantic_aggregate_for_type(context, current);
                if (!r_semantic_first_identifier(context, suffix, &token) || aggregate == NULL) {
                    current = R_TYPE_ID_INVALID;
                    continue;
                }
                current = R_TYPE_ID_INVALID;
                for (field = 0U; field < aggregate->field_count; ++field) {
                    const RSemanticField *entry =
                        &context->semantic_fields[aggregate->first_field + field];
                    if (entry->name_intern_id == token.intern_id) {
                        current = entry->type;
                        if (readonly && !r_semantic_type_is_const(context, current) &&
                            !r_semantic_intern_type(
                                context, R_SEMANTIC_TYPE_CONST, current, &current)) {
                            return false;
                        }
                        break;
                    }
                }
            } else if (suffix_view.kind == R_SYNTAX_INDEX_SUFFIX && type != NULL) {
                const bool readonly = r_semantic_type_is_const(context, current) ||
                                      (type->flags & R_SEMANTIC_TYPE_FLAG_SHARED) != 0U;
                current = type->base;
                if (readonly && !r_semantic_type_is_const(context, current) &&
                    !r_semantic_intern_type(context, R_SEMANTIC_TYPE_CONST, current, &current)) {
                    return false;
                }
            } else if (suffix_view.kind == R_SYNTAX_SLICE_SUFFIX && type != NULL) {
                if (!r_semantic_intern_derived_type(context,
                                                    R_SEMANTIC_TYPE_SLICE,
                                                    type->base,
                                                    0U,
                                                    0U,
                                                    R_SEMANTIC_TYPE_FLAG_SHARED,
                                                    &current)) {
                    return false;
                }
            } else if (suffix_view.kind == R_SYNTAX_FORMAT_SUFFIX) {
                if (!r_semantic_intern_named_standard_type(
                        context, "std.string::string", &current)) {
                    return false;
                }
            } else if (suffix_view.kind == R_SYNTAX_CALL_SUFFIX ||
                       suffix_view.kind == R_SYNTAX_CONSTRUCTOR_SUFFIX ||
                       suffix_view.kind == R_SYNTAX_METHOD_CALL_SUFFIX) {
                if (type != NULL && type->kind == R_SEMANTIC_TYPE_ENUM) {
                    continue;
                }
                if (!r_standard_query_call(body,
                                           first,
                                           suffix,
                                           current,
                                           suffix_view.kind == R_SYNTAX_METHOD_CALL_SUFFIX,
                                           &current)) {
                    return false;
                }
            }
        }
    }
    *result = current;
    return true;
}

static bool r_overload_compatible(RBodyContext *body, RTypeId actual, RTypeId parameter) {
    const RSemanticType *from = r_semantic_type(body->frontend, actual);
    const RSemanticType *to = r_semantic_type(body->frontend, parameter);
    if (r_body_conditional_converts(body, actual, parameter)) {
        return true;
    }
    if (from == NULL || to == NULL) {
        return false;
    }
    if (to->kind == R_SEMANTIC_TYPE_STR) {
        /* R-EXPR-0015 (L41): a string place views as str; lowering the selected call rejects a
           temporary string. */
        RTypeId string_type;
        return r_semantic_intern_named_standard_type(
                   body->frontend, "std.string::string", &string_type) &&
               (r_semantic_value_type(body->frontend, actual) == string_type);
    }
    if (to->kind == R_SEMANTIC_TYPE_SLICE && to->flags == R_SEMANTIC_TYPE_FLAG_SHARED) {
        if (from->kind == R_SEMANTIC_TYPE_BORROW) {
            const RSemanticType *array = r_semantic_type(body->frontend, from->base);
            if (array != NULL && array->kind == R_SEMANTIC_TYPE_FIXED_ARRAY &&
                array->base == to->base) {
                return true;
            }
        }
        if ((from->kind == R_SEMANTIC_TYPE_FIXED_ARRAY || from->kind == R_SEMANTIC_TYPE_ARRAY) &&
            from->base == to->base) {
            return true;
        }
        if (r_semantic_value_kind(body->frontend, to->base) == R_SEMANTIC_TYPE_U8 &&
            from->kind == R_SEMANTIC_TYPE_CONSTEXPR_STR) {
            return true;
        }
    }
    return false;
}

static bool r_overload_candidate(RBodyContext *body,
                                 const RSemanticSymbol *function,
                                 const RTypeVector *actuals,
                                 bool spread,
                                 bool *viable,
                                 bool *exact) {
    RFrontendContext *context = body->frontend;
    RTypeVector inferred = {0};
    uint32_t first_argument = 0U;
    uint32_t index;
    bool success = false;
    *viable = false;
    *exact = true;
    const bool packed = function->is_variadic_slice && !spread && function->parameter_count != 0U;
    /* R-TYPE-0053 (L18.4): the arguments after the fixed ones form the pack. */
    const bool pack = function->is_pack_variadic && !spread && (function->parameter_count != 0U);
    const uint32_t fixed =
        (packed || pack) ? function->parameter_count - 1U : function->parameter_count;
    RTypeId pack_tuple = R_TYPE_ID_INVALID;
    if ((!packed && !pack && function->parameter_count != actuals->count) ||
        ((packed || pack) && actuals->count < fixed) || (spread && !function->is_variadic_slice)) {
        return true;
    }
    if (pack) {
        RTypeVector elements = {0};
        bool formed = true;
        for (index = fixed; formed && (index < actuals->count); ++index) {
            const RSemanticType *element = r_semantic_type(context, actuals->items[index]);
            formed = (element != NULL) && (element->kind != R_SEMANTIC_TYPE_NULL_T) &&
                     (element->kind != R_SEMANTIC_TYPE_VOID) &&
                     r_semantic_type_is_inhabited(context, actuals->items[index], 0U) &&
                     r_type_vector_push(context, &elements, actuals->items[index]);
        }
        formed = formed && r_tuple_type(context, &elements, (RSourceSpan){0}, &pack_tuple);
        r_context_free(context, elements.items);
        if (!formed) {
            return context->resource_status == R_FRONTEND_OK;
        }
    }
    if (function->generic_schema != 0U && function->generic_origin == 0U) {
        const RGenericSchema schema = context->generic_schemas[function->generic_schema - 1U];
        for (index = 0U; index < schema.parameter_count; ++index) {
            if (!r_type_vector_push(context, &inferred, R_TYPE_ID_INVALID)) {
                goto cleanup;
            }
        }
        if (function->is_method && body->associated_owner_type != R_TYPE_ID_INVALID) {
            const RSemanticAggregate *owner =
                r_semantic_aggregate_for_type(context, body->associated_owner_type);
            if (owner != NULL && owner->generic_origin != 0U) {
                const uint32_t count =
                    context->generic_schemas[owner->generic_schema - 1U].parameter_count;
                for (index = 0U; index < count && index < schema.parameter_count; ++index) {
                    inferred.items[index] =
                        context->generic_arguments[owner->first_generic_argument + index];
                }
            }
        }
        for (index = 0U; index < (pack ? fixed + 1U : actuals->count); ++index) {
            if (pack && (index == fixed)) {
                if (!r_generic_infer_type(
                        context,
                        r_semantic_value_type(
                            context,
                            context
                                ->semantic_parameter_types[function->first_parameter_type + fixed]),
                        pack_tuple,
                        function->generic_schema,
                        &inferred,
                        0U)) {
                    success = true;
                    goto cleanup;
                }
                continue;
            }
            if (r_semantic_value_kind(context, actuals->items[index]) == R_SEMANTIC_TYPE_NULL_T) {
                /* Null supplies no value-type inference candidate. Other arguments
                 * may close a nullable pointer parameter before compatibility is checked. */
                continue;
            }
            RTypeId pattern =
                context->semantic_parameter_types[function->first_parameter_type +
                                                  (packed && index >= fixed ? fixed : index)];
            if (packed && index >= fixed) {
                pattern = r_semantic_type(context, pattern)->base;
            }
            if (!r_generic_infer_type(context,
                                      r_semantic_value_type(context, pattern),
                                      actuals->items[index],
                                      function->generic_schema,
                                      &inferred,
                                      0U)) {
                success = true;
                goto cleanup;
            }
        }
        bool consistent = true;
        if (!r_generic_infer_from_callables(
                context, function->generic_schema, &inferred, &consistent)) {
            goto cleanup;
        }
        if (!consistent) {
            success = true;
            goto cleanup;
        }
        for (index = 0U; index < schema.parameter_count; ++index) {
            const RGenericParameter declared =
                context->generic_parameters[schema.first_parameter + index];
            /* A pack proves the constraints of its elements on each of them. */
            const RGenericParameter parameter =
                (declared.is_pack && (declared.pack_element != R_TYPE_ID_INVALID))
                    ? *r_generic_pack_carrier(context, declared.pack_element)
                    : declared;
            uint32_t property;
            if (inferred.items[index] == R_TYPE_ID_INVALID ||
                r_semantic_value_kind(context, inferred.items[index]) == R_SEMANTIC_TYPE_NULL_T) {
                success = true;
                goto cleanup;
            }
            if (declared.is_pack) {
                RTypeVector components = {0};
                bool proven =
                    r_generic_pack_components(context, inferred.items[index], &components, 0U);
                for (size_t element = 0U; proven && (element < components.count); ++element) {
                    for (property = R_GENERIC_COPY; proven && (property <= R_GENERIC_CLONE);
                         property = r_generic_next_capability(property)) {
                        proven = ((parameter.constraints & property) == 0U) ||
                                 r_generic_has_property(
                                     context, components.items[element], property, 0U);
                    }
                    for (property = 0U; proven && (property < parameter.trait_constraint_count);
                         ++property) {
                        proven = r_semantic_type_implements_trait(
                            context,
                            components.items[element],
                            context->generic_trait_constraints[parameter.first_trait_constraint +
                                                               property]);
                    }
                }
                r_context_free(context, components.items);
                if (!proven) {
                    success = context->resource_status == R_FRONTEND_OK;
                    goto cleanup;
                }
                continue;
            }
            for (property = R_GENERIC_COPY; property <= R_GENERIC_CLONE;
                 property = r_generic_next_capability(property)) {
                if ((parameter.constraints & property) != 0U &&
                    !r_generic_has_property(context, inferred.items[index], property, 0U)) {
                    success = true;
                    goto cleanup;
                }
            }
            for (property = 0U; property < parameter.trait_constraint_count; ++property) {
                uint32_t effective =
                    context->generic_trait_constraints[parameter.first_trait_constraint + property];
                if (!r_generic_effective_callable_trait(context,
                                                        effective,
                                                        function->generic_schema,
                                                        &inferred,
                                                        function->module_source,
                                                        &effective)) {
                    goto cleanup;
                }
                if (!r_semantic_type_implements_trait(context, inferred.items[index], effective)) {
                    success = true;
                    goto cleanup;
                }
            }
        }
        if (!r_generic_commit_arguments(context, &inferred, &first_argument)) {
            goto cleanup;
        }
    }
    for (index = 0U; index < (pack ? fixed : actuals->count); ++index) {
        RTypeId parameter =
            context->semantic_parameter_types[function->first_parameter_type +
                                              (packed && index >= fixed ? fixed : index)];
        if (packed && index >= fixed) {
            parameter = r_semantic_type(context, parameter)->base;
        }
        if (inferred.count != 0U &&
            !r_generic_substitute_type(
                context, parameter, function->generic_schema, first_argument, &parameter)) {
            goto cleanup;
        }
        parameter = r_semantic_value_type(context, parameter);
        if (!r_overload_compatible(body, actuals->items[index], parameter)) {
            success = true;
            goto cleanup;
        }
        *exact = *exact && actuals->items[index] == parameter;
    }
    *viable = true;
    success = true;
cleanup:
    r_context_free(context, inferred.items);
    return success;
}

static bool r_overload_select_range(RBodyContext *body,
                                    RSymbolId *function_id,
                                    RAstRef arguments,
                                    const RAstNodeView *view,
                                    const RExpressionResult *receiver,
                                    bool arrow,
                                    RSourceSpan span,
                                    uint32_t first_argument) {
    RFrontendContext *context = body->frontend;
    const RSemanticSymbol *symbol = r_body_symbol(body, *function_id);
    RSemanticSymbol family;
    RTypeVector types = {0};
    RSymbolId exact_match = 0U;
    RSymbolId compatible_match = 0U;
    size_t exact_count = 0U;
    size_t compatible_count = 0U;
    /* Exact matches whose receiver keeps the place's own mutability win over those that
       borrow a mutable place shared (R-FUNC-0014). */
    RSymbolId strong_match = 0U;
    size_t strong_count = 0U;
    size_t index;
    bool success = false;
    if (symbol == NULL || !symbol->is_overloaded) {
        return true;
    }
    const RSemanticSymbol selected_family = *symbol;
    family = symbol->generic_origin == 0U ? *symbol
                                          : context->semantic_symbols[symbol->generic_origin - 1U];
    const size_t candidate_limit = context->semantic_symbol_count;
    if (receiver != NULL && !r_type_vector_push(context, &types, receiver->type)) {
        goto cleanup;
    }
    for (index = first_argument; index < view->child_count; ++index) {
        RAstRef argument;
        RTypeId type;
        if (!r_ast_ref_child(context, arguments, (uint32_t)index, &argument) ||
            !r_overload_argument_type(body, argument, 0U, false, &type) ||
            !r_type_vector_push(context, &types, r_semantic_value_type(context, type))) {
            goto cleanup;
        }
    }
    for (index = 0U; index < candidate_limit; ++index) {
        RSemanticSymbol candidate = context->semantic_symbols[index];
        RSymbolId candidate_id = (RSymbolId)index + 1U;
        bool viable;
        bool exact;
        bool weakened = false;
        if (candidate.kind != R_SEMANTIC_SYMBOL_FUNCTION || candidate.generic_origin != 0U ||
            candidate.poisoned || !candidate.signature_supported ||
            candidate.module_source != family.module_source ||
            candidate.name_intern_id != family.name_intern_id ||
            (candidate.is_protected && candidate.module_source != body->source) ||
            (candidate.module_source == body->source && candidate.scope_start > span.start &&
             !candidate.is_method)) {
            continue;
        }
        if (selected_family.generic_origin != 0U) {
            RTypeVector specialization = {0};
            const uint32_t count =
                context->generic_schemas[selected_family.generic_schema - 1U].parameter_count;
            for (uint32_t argument = 0U; argument < count; ++argument) {
                if (!r_type_vector_push(
                        context,
                        &specialization,
                        context->generic_arguments[selected_family.first_generic_argument +
                                                   argument])) {
                    r_context_free(context, specialization.items);
                    goto cleanup;
                }
            }
            const bool applied =
                r_generic_apply_function(context, candidate_id, &specialization, &candidate_id);
            r_context_free(context, specialization.items);
            if (!applied) {
                goto cleanup;
            }
            candidate = context->semantic_symbols[candidate_id - 1U];
        }
        if (receiver != NULL) {
            RTypeId owner = r_semantic_value_type(context, receiver->type);
            if (arrow) {
                const RSemanticType *pointer = r_semantic_type(context, owner);
                owner = pointer == NULL ? 0U : pointer->base;
                if (candidate.receiver_kind == R_SEMANTIC_RECEIVER_VALUE) {
                    continue;
                }
            }
            if (candidate.receiver_kind == R_SEMANTIC_RECEIVER_NONE) {
                continue;
            }
            types.items[0] = owner;
            if (candidate.receiver_kind != R_SEMANTIC_RECEIVER_VALUE) {
                /* R-FUNC-0014: a shared receiver borrows any place shared. */
                bool readonly = r_semantic_type_is_const(context, receiver->type);
                if (arrow) {
                    const RSemanticType *pointer =
                        r_semantic_type(context, r_semantic_value_type(context, receiver->type));
                    readonly =
                        pointer != NULL && (pointer->flags & R_SEMANTIC_TYPE_FLAG_SHARED) != 0U;
                } else if (receiver->is_place &&
                           !r_body_place_is_readonly(body, receiver->node, &readonly)) {
                    goto cleanup;
                }
                if (!readonly && candidate.receiver_kind == R_SEMANTIC_RECEIVER_SHARED) {
                    readonly = true;
                    weakened = true;
                }
                if (!r_semantic_intern_derived_type(context,
                                                    R_SEMANTIC_TYPE_BORROW,
                                                    owner,
                                                    0U,
                                                    0U,
                                                    readonly ? R_SEMANTIC_TYPE_FLAG_SHARED : 0U,
                                                    &types.items[0])) {
                    goto cleanup;
                }
            }
        }
        if (!r_overload_candidate(body,
                                  &candidate,
                                  &types,
                                  r_body_call_has_spread(body, arguments, view),
                                  &viable,
                                  &exact)) {
            goto cleanup;
        }
        if (viable) {
            compatible_match = candidate_id;
            ++compatible_count;
            if (exact) {
                exact_match = compatible_match;
                ++exact_count;
                if (!weakened) {
                    strong_match = candidate_id;
                    ++strong_count;
                }
            }
        }
    }
    if (exact_count > 1U && strong_count == 1U) {
        exact_match = strong_match;
        exact_count = 1U;
    }
    if (exact_count == 1U || (exact_count == 0U && compatible_count == 1U)) {
        *function_id = exact_count == 1U ? exact_match : compatible_match;
        const RSemanticSymbol selected = context->semantic_symbols[*function_id - 1U];
        if (selected.generic_origin != 0U && selected.is_trait_prototype &&
            !r_generic_function_is_open(context, &selected)) {
            const RTypeId self = context->generic_arguments[selected.first_generic_argument];
            if (!r_generic_bind_trait_call(context, *function_id, self, function_id, NULL))
                goto cleanup;
        }
        success = true;
    } else {
        *function_id = R_SYMBOL_ID_INVALID;
        RGenericText message = {0};
        message.display = true;
        uint32_t intern_id;
        const char *reason = compatible_count == 0U
                                 ? "no overload accepts these argument types; candidates: "
                                 : "function call is ambiguous between overloads; candidates: ";
        message.context = context;
        bool written = r_generic_text(&message, reason, strlen(reason));
        size_t count = 0U;
        for (index = 0U; written && index < candidate_limit; ++index) {
            const RSemanticSymbol *candidate = &context->semantic_symbols[index];
            if (candidate->kind != R_SEMANTIC_SYMBOL_FUNCTION || candidate->generic_origin != 0U ||
                candidate->module_source != family.module_source ||
                candidate->name_intern_id != family.name_intern_id ||
                candidate->overload_identity == 0U) {
                continue;
            }
            /* Each candidate is spelled by its declared parameter types. */
            const RInternEntry *name = &context->intern_entries[candidate->name_intern_id - 1U];
            written = (count++ == 0U || r_generic_text(&message, "; ", 2U)) &&
                      r_generic_text(&message, name->bytes, name->length) &&
                      r_generic_text(&message, "(", 1U);
            for (uint32_t parameter = 0U; written && parameter < candidate->parameter_count;
                 ++parameter) {
                RReflectionText spelled = {NULL, 0U, 0U};
                written =
                    (parameter == 0U || r_generic_text(&message, ", ", 2U)) &&
                    r_reflection_spell_type(
                        context,
                        context
                            ->semantic_parameter_types[candidate->first_parameter_type + parameter],
                        &spelled,
                        0U) &&
                    r_generic_text(&message, spelled.bytes, spelled.length) &&
                    (!candidate->is_pack_variadic ||
                     (parameter + 1U != candidate->parameter_count) ||
                     r_generic_text(&message, "...", 3U));
                r_context_free(context, spelled.bytes);
            }
            written = written && r_generic_text(&message, ")", 1U);
        }
        success = written && r_intern_bytes(context, message.bytes, message.length, &intern_id) &&
                  r_body_diagnostic(body,
                                    "R-DIAG-TYPE-001",
                                    "R-FUNC-0004",
                                    (const char *)context->intern_entries[intern_id - 1U].bytes,
                                    span);
        r_context_free(context, message.bytes);
    }
cleanup:
    r_context_free(context, types.items);
    return success;
}

static bool r_overload_select(RBodyContext *body,
                              RSymbolId *function_id,
                              RAstRef arguments,
                              const RAstNodeView *view,
                              const RExpressionResult *receiver,
                              bool arrow,
                              RSourceSpan span) {
    return r_overload_select_range(body, function_id, arguments, view, receiver, arrow, span, 0U);
}

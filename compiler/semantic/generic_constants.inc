/* Dependent constants are interned, checked expression DAGs. Their operands are
 * types or other constants, never source fragments or executable callbacks. */
static bool r_generic_align(uint64_t size, uint64_t alignment, uint64_t *result) {
    const uint64_t remainder = size % alignment;
    const uint64_t padding = remainder == 0U ? 0U : alignment - remainder;
    if (size > UINT64_MAX - padding) {
        return false;
    }
    *result = size + padding;
    return true;
}

static bool r_generic_layout(const RFrontendContext *context,
                             RTypeId id,
                             uint64_t *size,
                             uint64_t *alignment,
                             uint32_t depth) {
    id = r_semantic_representation_type(context, id);
    const RSemanticType *type = r_semantic_type(context, id);
    const RSemanticAggregate *aggregate;
    uint64_t member_size, member_alignment;
    uint32_t index, width;
    bool signed_integer;
    if (type == NULL || depth > context->options.limits.max_nesting ||
        r_generic_type_is_dependent(context, id, 0U)) {
        return false;
    }
    if (type->kind == R_SEMANTIC_TYPE_STANDARD && type->base == 0U && type->second == 0U) {
        const RInternEntry *name = &context->intern_entries[(size_t)type->length - 1U];
        const RNamedStandardCopyAbi *abi =
            r_named_standard_copy_abi_find(name->bytes, name->length);
        if (abi != NULL && abi->generic_arity == 0U) {
            *size = abi->object_size;
            *alignment = abi->alignment;
            return true;
        }
    }
    if (type->kind == R_SEMANTIC_TYPE_C_ABI_NUMERIC) {
        switch ((RTokenKind)type->length) {
#include "target_scalar_layout.generated.inc"
        default:
            return false;
        }
    }
    if (r_semantic_integer_properties(type->kind, &signed_integer, &width)) {
        *size = *alignment = width / 8U;
        return true;
    }
    switch (type->kind) {
    case R_SEMANTIC_TYPE_BOOL:
        *size = *alignment = 1U;
        return true;
    case R_SEMANTIC_TYPE_F32:
    case R_SEMANTIC_TYPE_CHAR:
        *size = *alignment = 4U;
        return true;
    case R_SEMANTIC_TYPE_F64:
        *size = *alignment = 8U;
        return true;
    case R_SEMANTIC_TYPE_CONST:
    case R_SEMANTIC_TYPE_ATOMIC:
        return r_generic_layout(context, type->base, size, alignment, depth + 1U);
    case R_SEMANTIC_TYPE_BORROW:
        /* R-TYPE-0051: an interface borrow is an address and a 32-bit tag. */
        if (r_dyn_contract(context, type->base) != NULL) {
            *size = 16U;
            *alignment = 8U;
            return true;
        }
        *size = *alignment = 8U;
        return true;
    case R_SEMANTIC_TYPE_RAW:
    case R_SEMANTIC_TYPE_RAW_FUNCTION:
        *size = *alignment = 8U;
        return true;
    case R_SEMANTIC_TYPE_SLICE:
    case R_SEMANTIC_TYPE_STR:
    case R_SEMANTIC_TYPE_CONSTEXPR_STR:
        *size = 16U;
        *alignment = 8U;
        return true;
    case R_SEMANTIC_TYPE_OWN:
        *size = sizeof(RRuntimeOwn);
        *alignment = _Alignof(RRuntimeOwn);
        return true;
    case R_SEMANTIC_TYPE_ARRAY:
        *size = sizeof(RRuntimeArray);
        *alignment = _Alignof(RRuntimeArray);
        return true;
    case R_SEMANTIC_TYPE_LIST:
        *size = sizeof(RRuntimeList);
        *alignment = _Alignof(RRuntimeList);
        return true;
    case R_SEMANTIC_TYPE_DICT:
        *size = sizeof(RRuntimeDict);
        *alignment = _Alignof(RRuntimeDict);
        return true;
    case R_SEMANTIC_TYPE_ARC:
        *size = sizeof(RRuntimeArc);
        *alignment = _Alignof(RRuntimeArc);
        return true;
    case R_SEMANTIC_TYPE_RC:
        *size = sizeof(RRuntimeRc);
        *alignment = _Alignof(RRuntimeRc);
        return true;
    case R_SEMANTIC_TYPE_WEAK:
        *size = sizeof(RRuntimeWeakArc);
        *alignment = _Alignof(RRuntimeWeakArc);
        return true;
    case R_SEMANTIC_TYPE_FIXED_ARRAY:
        if (type->length == 0U || type->second != 0U ||
            !r_generic_layout(context, type->base, &member_size, alignment, depth + 1U) ||
            member_size > UINT64_MAX / type->length) {
            return false;
        }
        *size = member_size * type->length;
        return true;
    case R_SEMANTIC_TYPE_OPTION:
    case R_SEMANTIC_TYPE_RESULT:
        *size = 1U;
        *alignment = 1U;
        if (r_semantic_value_kind(context, type->base) != R_SEMANTIC_TYPE_VOID &&
            !r_generic_layout(context, type->base, size, alignment, depth + 1U)) {
            return false;
        }
        if (type->second != 0U) {
            if (!r_generic_layout(
                    context, type->second, &member_size, &member_alignment, depth + 1U)) {
                return false;
            }
            if (member_size > *size) {
                *size = member_size;
            }
            if (member_alignment > *alignment) {
                *alignment = member_alignment;
            }
        }
        break;
    case R_SEMANTIC_TYPE_STRUCT:
    case R_SEMANTIC_TYPE_ENUM:
    case R_SEMANTIC_TYPE_STANDARD:
        aggregate = r_semantic_aggregate_for_type(context, id);
        if (aggregate == NULL || !aggregate->complete || aggregate->completing) {
            return false;
        }
        if (aggregate->kind == R_SEMANTIC_AGGREGATE_ENUM && !aggregate->is_tagged) {
            return r_generic_layout(
                context, aggregate->enum_underlying_type, size, alignment, depth + 1U);
        }
        *size = 0U;
        *alignment = 1U;
        for (index = 0U;
             index < (aggregate->is_tagged ? aggregate->variant_count : aggregate->field_count);
             ++index) {
            const RTypeId member =
                aggregate->is_tagged
                    ? context->semantic_variants[aggregate->first_variant + index].payload_type
                    : context->semantic_fields[aggregate->first_field + index].type;
            if (member == 0U) {
                continue;
            }
            if (!r_generic_layout(context, member, &member_size, &member_alignment, depth + 1U)) {
                return false;
            }
            if (member_alignment > *alignment) {
                *alignment = member_alignment;
            }
            if (aggregate->is_tagged) {
                if (member_size > *size) {
                    *size = member_size;
                }
            } else {
                if (!r_generic_align(*size, member_alignment, size) ||
                    *size > UINT64_MAX - member_size) {
                    return false;
                }
                *size += member_size;
            }
        }
        if (*size == 0U) {
            *size = 1U;
        }
        if (!aggregate->is_tagged) {
            return r_generic_align(*size, *alignment, size);
        }
        break;
    default:
        return false;
    }
    /* Tagged values use an ordinal u32 and an aligned union of active payloads. */
    if (!r_generic_align(*size, *alignment, size) ||
        !r_generic_align(4U, *alignment, &member_size) || *size > UINT64_MAX - member_size) {
        return false;
    }
    *size += member_size;
    if (*alignment < 4U) {
        *alignment = 4U;
    }
    return r_generic_align(*size, *alignment, size);
}

static bool r_generic_constant_evaluate(const RFrontendContext *context,
                                        RTypeId id,
                                        uint64_t *value,
                                        uint32_t depth) {
    const RSemanticType *type = r_semantic_type(context, id);
    uint64_t left = 0U, right = 0U, size, alignment;
    if (type == NULL || type->kind != R_SEMANTIC_TYPE_CONSTANT_EXPR ||
        depth > context->options.limits.max_nesting) {
        return false;
    }
    if (type->flags == (uint32_t)R_TOKEN_KW_SIZEOF || type->flags == (uint32_t)R_TOKEN_KW_ALIGNOF) {
        if (!r_generic_layout(context, type->base, &size, &alignment, depth + 1U)) {
            return false;
        }
        *value = type->flags == (uint32_t)R_TOKEN_KW_SIZEOF ? size : alignment;
        return true;
    }
    if (type->base == 0U && type->second == 0U) {
        *value = type->length;
        return true;
    }
    if (!r_generic_constant_evaluate(context, type->base, &left, depth + 1U) ||
        (type->second != 0U &&
         !r_generic_constant_evaluate(context, type->second, &right, depth + 1U))) {
        return false;
    }
    if (type->second == 0U) {
        switch ((RTokenKind)type->flags) {
        case R_TOKEN_PLUS:
            *value = left;
            return true;
        case R_TOKEN_MINUS:
            *value = 0U;
            return left == 0U;
        case R_TOKEN_TILDE:
            *value = ~left;
            return true;
        default:
            return false;
        }
    }
    switch ((RTokenKind)type->flags) {
    case R_TOKEN_PLUS:
        if (left > UINT64_MAX - right)
            return false;
        break;
    case R_TOKEN_MINUS:
        if (left < right)
            return false;
        break;
    case R_TOKEN_STAR:
        if (right != 0U && left > UINT64_MAX / right)
            return false;
        break;
    case R_TOKEN_LESS_LESS:
        if (right >= 64U || left > (UINT64_MAX >> right))
            return false;
        break;
    default:
        break;
    }
    return r_constant_integer_binary(
        (RTokenKind)type->flags, R_SEMANTIC_TYPE_USIZE, left, right, value);
}

static bool
r_generic_canonical_constant(RFrontendContext *context, RTypeId *expression, RSourceSpan span) {
    const RSemanticType *type = r_semantic_type(context, *expression);
    uint64_t value;
    bool valid = false;
    if (r_generic_type_is_dependent(context, *expression, 0U))
        return true;
    if (type == NULL || type->kind != R_SEMANTIC_TYPE_CONSTANT_EXPR)
        return false;
    if (type->flags == (uint32_t)R_TOKEN_COMMA)
        return true; /* an argument of a computed bound */
    if (type->flags == (uint32_t)R_TOKEN_DOT) {
        /* R-TYPE-0050: an associated constant whose owner is now closed. */
        const RTypeId value_type = r_generic_constant_type(context, *expression);
        if (!r_consteval_projection_value(context, *expression, &value, &valid) || !valid)
            return false;
        return r_generic_constant_literal(context, value_type, value, expression);
    }
    if (type->flags == (uint32_t)R_TOKEN_LPAREN) {
        /* R-TYPE-0047: a computed bound whose declaration's arguments are now closed. */
        const RTypeId value_type = r_consteval_computed_type(context, *expression);
        if (!r_consteval_computed_value(context, *expression, &value, &valid) || !valid)
            return false;
        return r_generic_constant_literal(context, value_type, value, expression);
    }
    if (type->base == 0U && type->second == 0U)
        return true; /* already a canonical literal of its type */
    if (!r_generic_constant_evaluate(context, *expression, &value, 0U)) {
        (void)r_semantic_add_diagnostic(
            context,
            "R-DIAG-CONST-001",
            "R-TYPE-0012",
            "constant generic argument requires a representable usize constant",
            span);
        return false;
    }
    return r_semantic_intern_derived_type(
        context, R_SEMANTIC_TYPE_CONSTANT_EXPR, 0U, 0U, value, R_TOKEN_INVALID, expression);
}

static bool r_generic_constant_fold(RFrontendContext *context,
                                    RTypeId *expression,
                                    uint64_t *value,
                                    RSourceSpan span) {
    if (r_generic_type_is_dependent(context, *expression, 0U)) {
        return true;
    }
    if (!r_generic_constant_evaluate(context, *expression, value, 0U) || *value == 0U) {
        r_semantic_add_diagnostic(
            context,
            "R-DIAG-CONST-001",
            "R-TYPE-0011",
            "fixed-array bound shall be a positive usize constant with a complete finite layout",
            span);
        return false;
    }
    *expression = 0U;
    return true;
}

/* R-TYPE-0047: a constant generic argument is typed; usize keeps the untagged form. */
static RTokenKind r_generic_constant_token(const RFrontendContext *context, RTypeId type) {
    switch (r_semantic_value_kind(context, type)) {
    case R_SEMANTIC_TYPE_I8:
        return R_TOKEN_KW_I8;
    case R_SEMANTIC_TYPE_I16:
        return R_TOKEN_KW_I16;
    case R_SEMANTIC_TYPE_I32:
        return R_TOKEN_KW_I32;
    case R_SEMANTIC_TYPE_I64:
        return R_TOKEN_KW_I64;
    case R_SEMANTIC_TYPE_ISIZE:
        return R_TOKEN_KW_ISIZE;
    case R_SEMANTIC_TYPE_U8:
        return R_TOKEN_KW_U8;
    case R_SEMANTIC_TYPE_U16:
        return R_TOKEN_KW_U16;
    case R_SEMANTIC_TYPE_U32:
        return R_TOKEN_KW_U32;
    case R_SEMANTIC_TYPE_U64:
        return R_TOKEN_KW_U64;
    case R_SEMANTIC_TYPE_BOOL:
        return R_TOKEN_KW_BOOL;
    default:
        return R_TOKEN_INVALID;
    }
}

/* The canonical constant of `type` whose value has the representation bits `bits`. */
static bool r_generic_constant_literal(RFrontendContext *context,
                                       RTypeId type,
                                       uint64_t bits,
                                       RTypeId *result) {
    const RSemanticTypeKind kind = r_semantic_value_kind(context, type);
    bool is_signed;
    uint32_t width;
    if (kind == R_SEMANTIC_TYPE_BOOL)
        bits = bits != 0U ? 1U : 0U;
    else if (r_semantic_integer_properties(kind, &is_signed, &width))
        bits &= r_constant_integer_mask(width);
    return r_semantic_intern_derived_type(context,
                                          R_SEMANTIC_TYPE_CONSTANT_EXPR,
                                          0U,
                                          0U,
                                          bits,
                                          (uint32_t)r_generic_constant_token(context, type),
                                          result);
}

/* A value type a constant generic parameter may have (R-TYPE-0047). */
static bool r_generic_constant_value_kind(const RFrontendContext *context, RTypeId type) {
    const RSemanticTypeKind kind = r_semantic_value_kind(context, type);
    return kind == R_SEMANTIC_TYPE_USIZE || kind == R_SEMANTIC_TYPE_BOOL ||
           r_generic_constant_token(context, type) != R_TOKEN_INVALID;
}

/* The value type of constant generic argument `constant`. */
static RTypeId r_generic_constant_type(RFrontendContext *context, RTypeId constant) {
    const RSemanticType *type = r_semantic_type(context, constant);
    RTypeId result = R_TYPE_ID_INVALID;
    if (type == NULL)
        return R_TYPE_ID_INVALID;
    if (type->kind == R_SEMANTIC_TYPE_PARAMETER)
        return context->generic_parameters[type->base - 1U].constant_type;
    if (type->kind != R_SEMANTIC_TYPE_CONSTANT_EXPR)
        return R_TYPE_ID_INVALID;
    if (type->flags == (uint32_t)R_TOKEN_LPAREN)
        return r_consteval_computed_type(context, constant);
    if (type->flags == (uint32_t)R_TOKEN_DOT) {
        const uint32_t trait = (uint32_t)(type->length >> 32U);
        return context
            ->semantic_associated_constants[context->semantic_traits[trait - 1U].first_constant +
                                            (uint32_t)(type->length & UINT32_MAX)]
            .type;
    }
    if (type->base == 0U && type->second == 0U && type->flags != (uint32_t)R_TOKEN_INVALID)
        return r_semantic_type_from_token(context, (RTokenKind)type->flags, &result) ? result : 0U;
    return r_semantic_type_from_token(context, R_TOKEN_KW_USIZE, &result) ? result : 0U;
}

static bool r_generic_constant_from_hir(RFrontendContext *context,
                                        RHirNodeId id,
                                        RTypeId *expression,
                                        uint32_t depth) {
    const RHirNode node = context->hir_nodes[id - 1U];
    RTypeId left = 0U, right = 0U;
    if (depth > r_frontend_tree_depth_limit(context)) {
        return false;
    }
    if (node.kind == R_HIR_GENERIC_CONSTANT) {
        *expression = node.auxiliary_type;
        return true;
    }
    if (node.kind == R_HIR_LOAD && node.child_count == 1U) {
        return r_generic_constant_from_hir(
            context, context->hir_children[node.first_child], expression, depth + 1U);
    }
    if (node.kind == R_HIR_PLACE && node.symbol != 0U) {
        const RSemanticSymbol *symbol = &context->semantic_symbols[node.symbol - 1U];
        if (symbol->kind == R_SEMANTIC_SYMBOL_LOCAL &&
            r_semantic_type_is_const(context, symbol->type) && symbol->hir_node != 0U) {
            const RHirNode *local = &context->hir_nodes[symbol->hir_node - 1U];
            return local->child_count == 1U &&
                   r_generic_constant_from_hir(
                       context, context->hir_children[local->first_child], expression, depth + 1U);
        }
        return false;
    }
    if (node.kind == R_HIR_CALL) {
        /* R-EXPR-0032: a translation-time call contributes its value. */
        uint64_t value = 0U;
        bool valid = false;
        if (!r_generic_constant_value_kind(context, node.type) ||
            !r_consteval_required_scalar(context, id, &value, &valid) || !valid) {
            return false;
        }
        return r_generic_constant_literal(context, node.type, value, expression);
    }
    if (node.kind == R_HIR_LITERAL && r_generic_constant_value_kind(context, node.type) &&
        r_semantic_value_kind(context, node.type) != R_SEMANTIC_TYPE_USIZE) {
        return r_generic_constant_literal(context, node.type, node.integer_value, expression);
    }
    /* Formulas below are usize arithmetic; other typed formulas are computed (R-TYPE-0047). */
    if (r_semantic_value_kind(context, node.type) != R_SEMANTIC_TYPE_USIZE) {
        return false;
    }
    if (node.kind == R_HIR_TYPE_QUERY) {
        left = node.auxiliary_type;
    } else if (node.kind == R_HIR_LITERAL) {
        /* Fully typed literal. */
    } else if (node.kind == R_HIR_BINARY || node.kind == R_HIR_UNARY) {
        if (node.child_count == 0U || node.child_count > 2U ||
            !r_generic_constant_from_hir(
                context, context->hir_children[node.first_child], &left, depth + 1U) ||
            (node.child_count == 2U &&
             !r_generic_constant_from_hir(
                 context, context->hir_children[node.first_child + 1U], &right, depth + 1U))) {
            return false;
        }
        if ((node.operation == R_TOKEN_SLASH || node.operation == R_TOKEN_PERCENT) &&
            !r_generic_type_is_dependent(context, right, 0U)) {
            uint64_t divisor;
            if (!r_generic_constant_evaluate(context, right, &divisor, 0U) || divisor == 0U) {
                return false;
            }
        }
    } else {
        return false;
    }
    return r_semantic_intern_derived_type(context,
                                          R_SEMANTIC_TYPE_CONSTANT_EXPR,
                                          left,
                                          right,
                                          node.kind == R_HIR_LITERAL ? node.integer_value : 0U,
                                          node.kind == R_HIR_LITERAL ? (uint32_t)R_TOKEN_INVALID
                                                                     : (uint32_t)node.operation,
                                          expression);
}

/* The formula is usize arithmetic over literals, parameters and layouts: a dependent formula of
 * this shape that does not convert is ill-formed rather than computed (R-TYPE-0047). */
static bool
r_generic_formula_arithmetic(const RFrontendContext *context, RHirNodeId id, uint32_t depth) {
    const RHirNode *node = &context->hir_nodes[id - 1U];
    uint32_t index;
    if (depth > r_frontend_tree_depth_limit(context))
        return false;
    if (node->kind == R_HIR_LOAD && node->child_count == 1U)
        return r_generic_formula_arithmetic(
            context, context->hir_children[node->first_child], depth + 1U);
    if (node->kind == R_HIR_PLACE && node->symbol != 0U) {
        const RSemanticSymbol *symbol = &context->semantic_symbols[node->symbol - 1U];
        const RHirNode *local =
            symbol->hir_node == 0U ? NULL : &context->hir_nodes[symbol->hir_node - 1U];
        return symbol->kind == R_SEMANTIC_SYMBOL_LOCAL && local != NULL &&
               local->child_count == 1U &&
               r_generic_formula_arithmetic(
                   context, context->hir_children[local->first_child], depth + 1U);
    }
    if (r_semantic_value_kind(context, node->type) != R_SEMANTIC_TYPE_USIZE)
        return false;
    if (node->kind == R_HIR_LITERAL || node->kind == R_HIR_GENERIC_CONSTANT ||
        node->kind == R_HIR_TYPE_QUERY)
        return true;
    if (node->kind != R_HIR_BINARY && node->kind != R_HIR_UNARY)
        return false;
    for (index = 0U; index < node->child_count; ++index) {
        if (!r_generic_formula_arithmetic(
                context, context->hir_children[node->first_child + index], depth + 1U))
            return false;
    }
    return true;
}

/* The expression names a parameter of generic declaration `schema` (R-TYPE-0047). */
static bool r_generic_ast_names_parameter(const RFrontendContext *context,
                                          RAstRef ref,
                                          uint32_t schema,
                                          uint32_t depth) {
    const RGenericSchema *generic = &context->generic_schemas[schema - 1U];
    RAstNodeView view;
    RAstTokenView token;
    uint32_t index;
    if (depth > r_frontend_tree_depth_limit(context))
        return false;
    if (r_ast_ref_token(context, ref, &token)) {
        for (index = 0U; token.kind == R_TOKEN_IDENTIFIER && index < generic->parameter_count;
             ++index) {
            if (context->generic_parameters[generic->first_parameter + index].name_intern_id ==
                token.intern_id)
                return true;
        }
        return false;
    }
    if (!r_ast_ref_view(context, ref, &view))
        return false;
    for (index = 0U; index < view.child_count; ++index) {
        RAstRef child;
        if (r_ast_ref_child(context, ref, index, &child) &&
            r_generic_ast_names_parameter(context, child, schema, depth + 1U))
            return true;
    }
    return false;
}

static bool r_generic_bound(RFrontendContext *context,
                            RAstRef type,
                            uint32_t requested_expression,
                            RTypeId constant_type,
                            RTypeId *expression,
                            uint64_t *value,
                            bool positive_bound) {
    RAstNodeView view;
    uint32_t index;
    RBodyContext body = {0};
    RExpressionResult result;
    RTypeId value_type = constant_type;
    RAstRef expression_ref = type;
    const size_t first_node = context->hir_node_count, first_child = context->hir_child_count;
    /* Bodies lowered on demand by translation-time evaluation outlive this scratch lowering. */
    const size_t builds = context->consteval_builds;
    size_t conversion_diagnostics;
    bool success = false;
    /* A computed bound keeps its checked HIR for the instances that close it. */
    bool keep_hir = false;
    bool dependent_call = false;
    uint32_t expression_index = UINT32_C(0);
    body.frontend = context;
    body.source = type.source;
    body.checking_usize_constant = true;
    body.translation_effects = true;
    if (!r_ast_ref_view(context, type, &view) ||
        (value_type == R_TYPE_ID_INVALID &&
         !r_semantic_type_from_token(context, R_TOKEN_KW_USIZE, &value_type))) {
        return false;
    }
    /* Reuse already checked local constants only inside their lexical block. */
    for (index = 0U; index < context->semantic_symbol_count; ++index) {
        const RSemanticSymbol *symbol = &context->semantic_symbols[index];
        const RSource *source = r_get_source_const(context, type.source);
        if (symbol->kind == R_SEMANTIC_SYMBOL_FUNCTION && symbol->hir_building &&
            symbol->module_source == type.source && symbol->declaration_ast != 0U) {
            const RSourceSpan span = source->ast_nodes[symbol->declaration_ast - 1U].span;
            if (span.start <= view.span.start && span.end >= view.span.end) {
                body.function_symbol = (RSymbolId)index + 1U;
            }
        }
    }
    if (body.function_symbol != 0U) {
        const RSource *source = r_get_source_const(context, type.source);
        for (index = 0U; index < context->semantic_symbol_count; ++index) {
            const RSemanticSymbol *symbol = &context->semantic_symbols[index];
            RSourceSpan scope = {0};
            size_t node_index;
            if (symbol->parent != body.function_symbol || symbol->scope_start > view.span.start) {
                continue;
            }
            for (node_index = 0U; node_index < source->ast_node_count; ++node_index) {
                const RAstNode *node = &source->ast_nodes[node_index];
                if (node->kind == R_SYNTAX_BLOCK && node->span.start <= symbol->name_span.start &&
                    node->span.end >= symbol->name_span.end &&
                    (scope.end == 0U ||
                     node->span.end - node->span.start < scope.end - scope.start)) {
                    scope = node->span;
                }
            }
            if ((scope.end == 0U ||
                 (scope.start <= view.span.start && scope.end >= view.span.end)) &&
                !r_symbol_vector_push(context, &body.active_symbols, (RSymbolId)index + 1U)) {
                r_context_free(context, body.active_symbols.items);
                return false;
            }
        }
    }
    r_body_expression_invalid(&result);
    RSourceSpan expression_span = view.span;
    bool found_expression = false;
    for (index = 0U; index < view.child_count; ++index) {
        RAstRef child;
        RAstNodeView child_view;
        if (!r_ast_ref_child(context, type, index, &child) ||
            !r_ast_ref_view(context, child, &child_view))
            break;
        if (child_view.kind != R_SYNTAX_EXPRESSION || expression_index++ != requested_expression)
            continue;
        found_expression = true;
        expression_span = child_view.span;
        expression_ref = child;
        if (body.function_symbol == 0U) {
            const uint32_t schema_id = r_generic_schema_at(context, expression_span);
            if (schema_id != 0U && r_consteval_ast_has_call(context, child, 0U) &&
                r_generic_ast_names_parameter(context, child, schema_id, 0U)) {
                /* R-TYPE-0047: a call over the declaration's parameters is computed for each
                   closed instance; it is checked once function signatures exist. */
                success =
                    r_consteval_computed_bound(
                        context, child, schema_id, 0U, false, value_type, expression) &&
                    (positive_bound
                         ? r_generic_constant_fold(context, expression, value, expression_span)
                         : r_generic_canonical_constant(context, expression, expression_span));
                goto cleanup;
            }
        }
        if (body.function_symbol == 0U) {
            /* R-EXPR-0032: a module-scope bound computed by a translation-time call. */
            uint64_t computed = 0U;
            bool computed_valid = false;
            bool handled = false;
            if (!r_consteval_module_scope_value(context,
                                                child,
                                                value_type,
                                                expression_span,
                                                &computed,
                                                &computed_valid,
                                                &handled)) {
                goto cleanup;
            }
            if (handled) {
                if (computed_valid &&
                    r_generic_constant_literal(context, value_type, computed, expression)) {
                    success =
                        positive_bound
                            ? r_generic_constant_fold(context, expression, value, expression_span)
                            : r_generic_canonical_constant(context, expression, expression_span);
                }
                goto cleanup;
            }
        }
        if (!r_body_lower_value(&body, child, value_type, &result))
            goto cleanup;
        if (result.valid)
            r_consteval_mark_reads(context, result.node, 0U);
        /* A call over the function's parameters is computed per instance (R-TYPE-0047). */
        dependent_call = result.valid && result.type == value_type && body.function_symbol != 0U &&
                         r_consteval_contains_call(context, result.node, 0U) &&
                         r_consteval_dependent(context, result.node, 0U);
        break;
    }
    if (!found_expression && !positive_bound) {
        RAstTokenView name = {0};
        uint32_t names = 0U;
        bool only_name = true;
        bool associated = false;
        for (index = 0U; index < view.child_count; ++index) {
            RAstRef child;
            RAstTokenView token;
            if (!r_ast_ref_child(context, type, index, &child) ||
                !r_ast_ref_token(context, child, &token)) {
                only_name = false;
                break;
            }
            if (token.kind == R_TOKEN_IDENTIFIER ||
                (token.kind == R_TOKEN_KW_SELF && index == 0U)) {
                name = token;
                ++names;
            } else if (token.kind != R_TOKEN_DOT && token.kind != R_TOKEN_COLON_COLON) {
                only_name = false;
                break;
            }
        }
        if (only_name && names != 0U) {
            bool parameter = false;
            if (names == 1U && !r_body_constant_parameter(&body, &name, &result, &parameter))
                goto cleanup;
            /* R-TYPE-0050: `P::NAME` or `Self::NAME` as a constant argument. */
            if (!parameter && names >= 2U &&
                !r_body_associated_constant(&body, type, true, &result, &associated))
                goto cleanup;
            if (!parameter && !associated) {
                RBodyLookupIssue issue;
                const RSymbolId id =
                    names == 1U ? r_body_resolve_name(&body, &name, &issue)
                                : r_body_resolve_qualified_name(&body, type, &name, &issue);
                const RSemanticSymbol *symbol = r_body_symbol(&body, id);
                if (symbol != NULL && symbol->kind == R_SEMANTIC_SYMBOL_MODULE_CONSTANT) {
                    if (!r_body_append_module_constant(&body, symbol, name.span, &result))
                        goto cleanup;
                } else if (symbol != NULL && symbol->kind == R_SEMANTIC_SYMBOL_LOCAL &&
                           r_semantic_type_is_const(context, symbol->type) &&
                           symbol->hir_node != 0U) {
                    const RHirNode *local = &context->hir_nodes[symbol->hir_node - 1U];
                    if (local->child_count == 1U) {
                        result.node = context->hir_children[local->first_child];
                        result.type = r_semantic_value_type(context, symbol->type);
                        result.valid = true;
                        context->semantic_symbols[id - 1U].translation_time_read = true;
                    }
                }
            }
        }
    }
    conversion_diagnostics = context->diagnostic_count;
    if (result.valid && context->hir_nodes[result.node - 1U].kind == R_HIR_CAST &&
        context->hir_nodes[result.node - 1U].operation != R_TOKEN_KW_AS) {
        /* A constant argument has exactly its parameter's type; no implicit conversion. */
        result.valid = false;
    }
    if (result.valid && result.type == value_type && !dependent_call &&
        r_generic_constant_from_hir(context, result.node, expression, 0U)) {
        success = positive_bound
                      ? r_generic_constant_fold(context, expression, value, expression_span)
                      : r_generic_canonical_constant(context, expression, expression_span);
    } else if (result.valid && result.type == value_type && !body.poisoned &&
               context->diagnostic_count == conversion_diagnostics &&
               context->resource_status == R_FRONTEND_OK &&
               (dependent_call || !r_generic_formula_arithmetic(context, result.node, 0U))) {
        /* R-TYPE-0047: a formula outside usize arithmetic, or with a call over the parameters,
           is computed for each instance; a closed one is evaluated now. */
        const uint32_t schema_id = r_generic_schema_at(context, expression_span);
        uint64_t bits = 0U;
        bool valid = false;
        if (schema_id != 0U && r_consteval_dependent(context, result.node, 0U)) {
            keep_hir = true;
            if (!r_consteval_static_check(context, result.node, true, R_HIR_NODE_ID_INVALID, 0U)) {
                if (context->resource_status == R_FRONTEND_OK)
                    (void)r_consteval_report_not_constant(context, result.node, expression_span);
                goto cleanup;
            }
            valid = r_consteval_computed_bound(context,
                                               expression_ref,
                                               schema_id,
                                               result.node,
                                               r_consteval_reads_locals(context, result.node, 0U),
                                               value_type,
                                               expression);
        } else if (!r_consteval_required_scalar(context, result.node, &bits, &valid)) {
            goto cleanup;
        } else {
            valid = valid && r_generic_constant_literal(context, value_type, bits, expression);
        }
        success =
            valid &&
            (positive_bound ? r_generic_constant_fold(context, expression, value, expression_span)
                            : r_generic_canonical_constant(context, expression, expression_span));
    } else if (!body.poisoned && (context->diagnostic_count == conversion_diagnostics)) {
        (void)r_semantic_add_diagnostic(context,
                                        "R-DIAG-CONST-001",
                                        positive_bound ? "R-TYPE-0011" : "R-TYPE-0012",
                                        positive_bound
                                            ? "expected a checked usize constant expression"
                                            : "expected a checked constant of the parameter type",
                                        expression_span);
    }
cleanup:
    if (!keep_hir && context->consteval_builds == builds) {
        context->hir_node_count = first_node;
        context->hir_child_count = first_child;
    }
    r_context_free(context, body.active_symbols.items);
    return success;
}

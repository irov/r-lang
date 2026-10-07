/* R-STMT-0023 (L44): `for (constexpr T name in low..high) block` repeats its block at translation,
   once for each value of name from low up to but excluding high. In each repetition name is a
   constant of T, so that it may index core::field (R-REFL-0006), decide a constant condition or
   bound an array. Each repetition is a block of its own, and no loop remains in the HIR of a
   function an instantiation does not decide. In a generic definition whose bounds depend on its
   parameters, such as `core::field_count::<T>()`, the block is checked once with name as the
   constant of a synthetic parameter, and every instantiation unrolls it (generics.inc). */

/* Whether a break or continue would leave the translation-time loop that is being repeated: the
   innermost break target or loop it takes began before the loop. A labeled jump names a loop or
   labeled switch, never the translation-time loop, so it may leave the repetitions for a loop
   around them (defect L44-17); an unlabeled continue passes over the hidden loop of a labeled
   switch to the loop it continues (defect L46-5). */
static bool
r_constexpr_loop_jump_escapes(RBodyContext *body, RTokenKind kind, const RAstTokenView *label) {
    if (!body->in_constexpr_loop || (label != NULL)) {
        return false;
    }
    if (kind == R_TOKEN_KW_BREAK) {
        return body->active_break_target_kinds.count <= body->constexpr_loop_break_floor;
    }
    size_t loops = body->active_loop_scope_bases.count;
    while ((loops != 0U) && r_dispatch_loop(body, loops - 1U)) {
        loops -= 1U;
    }
    return loops <= body->constexpr_loop_loop_floor;
}

/* Whether a bound that is not yet a constant is one an instantiation decides: it names a constant
   parameter, a reflection count or a size of a type parameter, or a call over them. */
static bool r_constexpr_loop_dependent_bound(RBodyContext *body, RHirNodeId node_id) {
    return r_consteval_dependent(body->frontend, node_id, 0U);
}

/* One bound of the range: a constant of the loop type, or a node an instantiation decides. */
static bool r_constexpr_loop_bound(RBodyContext *body,
                                   RAstRef bound,
                                   RTypeId type,
                                   RSourceSpan span,
                                   uint64_t *bits,
                                   RHirNodeId *dependent,
                                   bool *known) {
    RExpressionResult lowered = {0};
    RBodyConstantValue constant;

    *known = false;
    *bits = UINT64_C(0);
    *dependent = R_HIR_NODE_ID_INVALID;
    if (!r_body_lower_value(body, bound, type, &lowered)) {
        return false;
    }
    if (!lowered.valid) {
        return r_body_cascade(body);
    }
    if (r_body_constant_scalar(body, lowered.node, &constant) && constant.valid) {
        *bits = constant.bits;
        *known = true;
        return true;
    }
    if (r_constexpr_loop_dependent_bound(body, lowered.node)) {
        *dependent = lowered.node;
        *known = true;
        return true;
    }
    /* R-EXPR-0022, R-EXPR-0032: any other constant expression, such as a local constant, sizeof
       or a call of a translation-time evaluable function (defect L44-14). */
    {
        const size_t diagnostics = body->frontend->diagnostic_count;
        uint64_t value = 0U;
        bool valid = false;
        if (!r_consteval_required_scalar(body->frontend, lowered.node, &value, &valid)) {
            return false;
        }
        if (valid) {
            r_consteval_mark_reads(body->frontend, lowered.node, 0U);
            *bits = value;
            *known = true;
            return true;
        }
        if (body->frontend->diagnostic_count != diagnostics) {
            return r_body_cascade(body);
        }
    }
    return r_body_diagnostic(body,
                             "R-DIAG-CONST-002",
                             "R-STMT-0023",
                             "a bound of a translation-time loop is a constant expression",
                             span);
}

/* Declares the loop constant of one repetition, or of the generic definition when `parameter`
   names the synthetic parameter that an instantiation binds. */
static bool r_constexpr_loop_declare(RBodyContext *body,
                                     const RAstTokenView *name,
                                     RTypeId loop_type,
                                     uint64_t value,
                                     RTypeId parameter) {
    RSemanticSymbol symbol;
    RSymbolId symbol_id;

    (void)memset(&symbol, 0, sizeof(symbol));
    symbol.kind = R_SEMANTIC_SYMBOL_MODULE_CONSTANT;
    symbol.name_span = name->span;
    symbol.scope_start = name->span.end;
    symbol.name_intern_id = name->intern_id;
    symbol.module_source = body->source;
    symbol.type = loop_type;
    symbol.integer_value = value;
    symbol.constant_parameter = parameter;
    symbol.block_constant = true;
    symbol.parent = body->function_symbol;
    symbol.signature_supported = true;
    return r_semantic_append_symbol(body->frontend, symbol, &symbol_id) &&
           r_symbol_vector_push(body->frontend, &body->active_symbols, symbol_id);
}

/* The synthetic constant parameter of a translation-time loop of a generic definition. */
static bool r_constexpr_loop_parameter(RFrontendContext *context,
                                       const RAstTokenView *name,
                                       RTypeId loop_type,
                                       RTypeId *parameter_type) {
    RGenericParameter parameter;

    (void)memset(&parameter, 0, sizeof(parameter));
    parameter.constant_type = loop_type;
    parameter.name_span = name->span;
    parameter.name_intern_id = name->intern_id;
    parameter.loop_constant = true;
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
    *parameter_type = parameter.type;
    return true;
}

/* A bound as the literal node an instantiation unrolls with. */
static bool r_constexpr_loop_bound_node(RBodyContext *body,
                                        RHirNodeId dependent,
                                        RTypeId loop_type,
                                        uint64_t bits,
                                        RSourceSpan span,
                                        RHirNodeId *node_id) {
    if (dependent != R_HIR_NODE_ID_INVALID) {
        *node_id = dependent;
        return true;
    }
    return r_body_append_node(body,
                              R_HIR_LITERAL,
                              span,
                              loop_type,
                              R_SYMBOL_ID_INVALID,
                              R_TOKEN_INVALID,
                              bits,
                              false,
                              NULL,
                              node_id);
}

static bool r_body_lower_constexpr_for(RBodyContext *body,
                                       RAstRef statement,
                                       RHirNodeId *node_id,
                                       bool *always_returns) {
    RFrontendContext *context = body->frontend;
    RAstNodeView view;
    RAstRef type_ref = {0};
    RAstRef range_ref = {0};
    RAstRef block_ref = {0};
    RAstRef bounds[2] = {{0}, {0}};
    uint32_t bound_count = 0U;
    RAstTokenView name = {0};
    bool has_name = false;
    RTypeId loop_type = R_TYPE_ID_INVALID;
    RTypeId void_type;
    RHirVector copies = {0};
    bool is_signed = false;
    uint32_t width = 0U;
    bool success = false;

    *node_id = R_HIR_NODE_ID_INVALID;
    *always_returns = false;
    if (!r_ast_ref_view(context, statement, &view) ||
        !r_semantic_type_from_token(context, R_TOKEN_KW_VOID, &void_type)) {
        return r_body_invariant(body);
    }
    for (uint32_t index = 0U; index < view.child_count; ++index) {
        RAstRef child;
        RAstNodeView child_view;
        RAstTokenView token;
        if (!r_ast_ref_child(context, statement, index, &child)) {
            return r_body_invariant(body);
        }
        if (r_ast_ref_token(context, child, &token)) {
            if ((token.kind == R_TOKEN_IDENTIFIER) && !has_name) {
                name = token;
                has_name = true;
            }
            continue;
        }
        if (!r_ast_ref_view(context, child, &child_view)) {
            return r_body_invariant(body);
        }
        if (child_view.kind == R_SYNTAX_TYPE) {
            type_ref = child;
        } else if (child_view.kind == R_SYNTAX_BLOCK) {
            block_ref = child;
        } else if (range_ref.node == R_AST_NODE_ID_INVALID) {
            range_ref = child;
        }
    }
    if (!has_name || (type_ref.node == R_AST_NODE_ID_INVALID) ||
        (range_ref.node == R_AST_NODE_ID_INVALID) || (block_ref.node == R_AST_NODE_ID_INVALID)) {
        return r_body_invariant(body);
    }
    {
        RAstNodeView range_view;
        if (!r_ast_ref_view(context, range_ref, &range_view)) {
            return r_body_invariant(body);
        }
        if (range_view.kind == R_SYNTAX_RANGE_EXPRESSION) {
            for (uint32_t index = 0U; (index < range_view.child_count) && (bound_count < 2U);
                 ++index) {
                RAstRef child;
                RAstTokenView token;
                if (!r_ast_ref_child(context, range_ref, index, &child)) {
                    return r_body_invariant(body);
                }
                if (!r_ast_ref_token(context, child, &token)) {
                    bounds[bound_count++] = child;
                }
            }
        }
        if (bound_count != 2U) {
            return r_body_diagnostic(body,
                                     "R-DIAG-TYPE-001",
                                     "R-STMT-0023",
                                     "a translation-time loop takes a range low..high of constants",
                                     range_view.span);
        }
    }
    {
        /* A type name that resolves to nothing is reported here, since the type is no object
           declaration that would report it (defect L44-15). */
        const size_t type_diagnostics = context->diagnostic_count;
        context->unresolved_type_name_span = (RSourceSpan){0U, 0U, 0U};
        if (!r_semantic_type_from_ast(context, type_ref, &loop_type)) {
            bool reported = false;
            if (context->resource_status != R_FRONTEND_OK) {
                return false;
            }
            r_body_poison(body);
            if ((context->diagnostic_count == type_diagnostics) &&
                !r_semantic_report_unresolved_type_name(context, &reported)) {
                return false;
            }
            return reported || (context->diagnostic_count != type_diagnostics)
                       ? true
                       : r_body_diagnostic(body,
                                           "R-DIAG-TYPE-001",
                                           "R-STMT-0023",
                                           "the constant of a translation-time loop has an "
                                           "integer type",
                                           view.span);
        }
    }
    if (!r_semantic_integer_properties(
            r_semantic_value_kind(context, loop_type), &is_signed, &width)) {
        return r_body_diagnostic(body,
                                 "R-DIAG-TYPE-001",
                                 "R-STMT-0023",
                                 "the constant of a translation-time loop has an integer type",
                                 view.span);
    }
    uint64_t low = 0U;
    uint64_t high = 0U;
    bool low_known = false;
    bool high_known = false;
    RHirNodeId low_dependent = R_HIR_NODE_ID_INVALID;
    RHirNodeId high_dependent = R_HIR_NODE_ID_INVALID;
    if (!r_constexpr_loop_bound(
            body, bounds[0], loop_type, view.span, &low, &low_dependent, &low_known) ||
        !r_constexpr_loop_bound(
            body, bounds[1], loop_type, view.span, &high, &high_dependent, &high_known)) {
        return false;
    }
    if (!low_known || !high_known) {
        return true;
    }
    const bool previous_active = body->in_constexpr_loop;
    const size_t previous_break_floor = body->constexpr_loop_break_floor;
    const size_t previous_loop_floor = body->constexpr_loop_loop_floor;
    body->in_constexpr_loop = true;
    body->constexpr_loop_break_floor = body->active_break_target_kinds.count;
    body->constexpr_loop_loop_floor = body->active_loop_scope_bases.count;
    if ((low_dependent != R_HIR_NODE_ID_INVALID) || (high_dependent != R_HIR_NODE_ID_INVALID)) {
        /* A generic definition: the block is checked once over the loop parameter. */
        const size_t scope_base = body->active_symbols.count;
        RTypeId parameter = R_TYPE_ID_INVALID;
        RHirNodeId children_storage[3];
        RHirVector children = {children_storage, 3U, 3U};
        bool block_returns = false;
        const bool lowered =
            r_constexpr_loop_parameter(context, &name, loop_type, &parameter) &&
            r_constexpr_loop_declare(body, &name, loop_type, UINT64_C(0), parameter) &&
            r_body_lower_block(body, block_ref, false, &children_storage[2], &block_returns);
        body->active_symbols.count = scope_base;
        if (!lowered ||
            !r_constexpr_loop_bound_node(
                body, low_dependent, loop_type, low, view.span, &children_storage[0]) ||
            !r_constexpr_loop_bound_node(
                body, high_dependent, loop_type, high, view.span, &children_storage[1]) ||
            !r_body_append_node(body,
                                R_HIR_CONSTEXPR_LOOP,
                                view.span,
                                void_type,
                                R_SYMBOL_ID_INVALID,
                                R_TOKEN_INVALID,
                                UINT64_C(0),
                                false,
                                &children,
                                node_id)) {
            goto cleanup;
        }
        r_body_hir_node(body, *node_id)->auxiliary_type = parameter;
        success = true;
        goto cleanup;
    }
    /* Constants arrive as bits of the loop type's width; compare them in its signedness. */
    const uint64_t mask = width >= 64U ? UINT64_MAX : ((UINT64_C(1) << width) - 1U);
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
        success = r_body_diagnostic(body,
                                    "R-DIAG-LIMIT-001",
                                    "R-STMT-0023",
                                    "a translation-time loop repeats its block at most 1024 times",
                                    view.span);
        goto cleanup;
    }
    for (uint64_t step = 0U; step < count; ++step) {
        const size_t scope_base = body->active_symbols.count;
        RHirNodeId copy = R_HIR_NODE_ID_INVALID;
        bool copy_returns = false;
        const bool lowered = r_constexpr_loop_declare(
                                 body, &name, loop_type, (low + step) & mask, R_TYPE_ID_INVALID) &&
                             r_body_lower_block(body, block_ref, false, &copy, &copy_returns);
        body->active_symbols.count = scope_base;
        if (!lowered) {
            goto cleanup;
        }
        if ((copy != R_HIR_NODE_ID_INVALID) && !r_hir_vector_push(context, &copies, copy)) {
            goto cleanup;
        }
        if (copy_returns) {
            /* The later repetitions are not reached. */
            *always_returns = true;
            break;
        }
    }
    success = r_body_append_node(body,
                                 R_HIR_BLOCK,
                                 view.span,
                                 void_type,
                                 R_SYMBOL_ID_INVALID,
                                 R_TOKEN_INVALID,
                                 UINT64_C(0),
                                 false,
                                 &copies,
                                 node_id);

cleanup:
    body->in_constexpr_loop = previous_active;
    body->constexpr_loop_break_floor = previous_break_floor;
    body->constexpr_loop_loop_floor = previous_loop_floor;
    r_context_free(context, copies.items);
    return success;
}

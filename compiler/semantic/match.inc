/* Patterns are checked once, then lowered to lazy branches over one stable scrutinee. */
typedef enum RMatchPatternKind {
    R_MATCH_ANY,
    R_MATCH_BIND,
    R_MATCH_CONSTANT,
    /* R-EXPR-0031 (L20.3): a string literal compared with a `str` scrutinee byte for byte. */
    R_MATCH_STRING,
    /* R-EXPR-0031 (L20.4): a label that the `matches` of a core::CaseMatcher scrutinee tests. */
    R_MATCH_MATCHER,
    R_MATCH_VARIANT,
    R_MATCH_FIELDS
} RMatchPatternKind;

typedef struct RMatchPattern {
    RMatchPatternKind kind;
    RAstRef ast;
    RSourceSpan span;
    RTypeId type;
    RTypeId payload_type;
    uint64_t tag;
    RAstTokenView name;
    struct RMatchPattern **children;
    uint32_t child_count;
    bool moves;
    bool contains_move;
    /* A string or matcher label: `tag` is the interned literal rather than scalar bits. */
    bool string_label;
} RMatchPattern;

typedef struct RMatchArm {
    RMatchPattern *pattern;
    RAstRef value;
    /* R-EXPR-0031 (L20.2): an arm whose result is a throw statement of type never. */
    RAstRef thrown;
    RAstRef guard;
    RExpressionResult test;
    RExpressionResult result;
} RMatchArm;

static bool r_match_error(RBodyContext *body, RSourceSpan span, const char *message) {
    return r_body_diagnostic(body, "R-DIAG-SWITCH-001", "R-EXPR-0031", message, span);
}

static void r_match_destroy(RBodyContext *body, RMatchPattern *pattern) {
    if (pattern == NULL)
        return;
    for (uint32_t i = 0U; i < pattern->child_count; ++i)
        r_match_destroy(body, pattern->children[i]);
    r_context_free(body->frontend, pattern->children);
    r_context_free(body->frontend, pattern);
}

static bool r_match_children(RBodyContext *body, RMatchPattern *p, uint32_t count) {
    if (count == 0U)
        return true;
    p->children = r_context_allocate(body->frontend, (size_t)count * sizeof(*p->children));
    if (p->children == NULL)
        return false;
    (void)memset(p->children, 0, (size_t)count * sizeof(*p->children));
    p->child_count = count;
    return true;
}

/* L20.4: the `matches` a core::CaseMatcher implementation gives a scrutinee type, and the type
   of its labels; none for a type without one. */
static bool r_label_matcher(
    RBodyContext *body, RTypeId type, RSourceSpan span, RSymbolId *matches, RTypeId *label_type) {
    RFrontendContext *context = body->frontend;
    const RTypeId owner = r_semantic_value_type(context, type);
    const RSemanticTypeKind kind = r_semantic_value_kind(context, owner);
    const RSemanticSymbol *function;

    *matches = R_SYMBOL_ID_INVALID;
    *label_type = R_TYPE_ID_INVALID;
    if ((context->core_case_matcher_trait == UINT32_C(0)) ||
        ((kind != R_SEMANTIC_TYPE_STRUCT) && (kind != R_SEMANTIC_TYPE_PARAMETER))) {
        return true;
    }
    if (!r_iteration_resolve_trait_method(
            body, owner, context->core_case_matcher_trait, "matches", span, matches)) {
        return false;
    }
    if (*matches == R_SYMBOL_ID_INVALID) {
        return true;
    }
    function = r_body_symbol(body, *matches);
    if ((function == NULL) || (function->parameter_count != 2U)) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    *label_type = r_semantic_value_type(
        context, context->semantic_parameter_types[function->first_parameter_type + 1U]);
    return true;
}

/* L20.4: `matches(&scrutinee, label)` as the test of one label. */
static bool r_label_matcher_test(RBodyContext *body,
                                 RExpressionResult place,
                                 RSymbolId matches,
                                 RAstRef label,
                                 RTypeId label_type,
                                 RSourceSpan span,
                                 RExpressionResult *test) {
    RFrontendContext *context = body->frontend;
    const RSemanticSymbol *function = r_body_symbol(body, matches);
    RExpressionResult target = {0}, value = {0};
    RHirNodeId operands[2];
    RHirNodeId call;

    r_body_expression_invalid(test);
    if (function == NULL) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    if (!r_body_form_safe_borrow(body,
                                 span,
                                 context->semantic_parameter_types[function->first_parameter_type],
                                 &place,
                                 &target) ||
        !r_body_lower_value(body, label, label_type, &value) ||
        (value.valid && !r_body_finish_value(body, label_type, &value))) {
        return false;
    }
    if (!target.valid || !value.valid) {
        return true;
    }
    operands[0] = target.node;
    operands[1] = value.node;
    RHirVector children = {operands, 2U, 2U};
    if (!r_body_append_node(body,
                            R_HIR_CALL,
                            span,
                            function->return_type,
                            matches,
                            R_TOKEN_INVALID,
                            UINT64_C(0),
                            false,
                            &children,
                            &call)) {
        return false;
    }
    r_body_hir_node(body, call)->auxiliary_type = function->effect_carrier_type;
    r_body_hir_node(body, call)->borrow_origin = R_SYMBOL_ID_INVALID;
    test->node = call;
    test->type = function->return_type;
    test->symbol = R_SYMBOL_ID_INVALID;
    test->borrow_origin = R_SYMBOL_ID_INVALID;
    test->valid = true;
    return true;
}

/* A `str` or `constexpr str` scrutinee takes string literal patterns (L20.3). */
static bool r_match_string_type(RBodyContext *body, RTypeId type) {
    const RSemanticTypeKind kind = r_semantic_value_kind(body->frontend, type);
    return (kind == R_SEMANTIC_TYPE_STR) || (kind == R_SEMANTIC_TYPE_CONSTEXPR_STR);
}

/* The string literal a lowered pattern constant designates, through its conversion to `str`. */
static const RHirNode *r_match_string_literal(RBodyContext *body, RHirNodeId node_id) {
    for (uint32_t depth = 0U; depth < 4U; ++depth) {
        const RHirNode *node = r_body_hir_node(body, node_id);
        if (node == NULL)
            return NULL;
        if (node->kind == R_HIR_STRING_LITERAL)
            return node;
        if ((node->kind != R_HIR_CAST) || (node->child_count != 1U))
            return NULL;
        node_id = body->frontend->hir_children[node->first_child];
    }
    return NULL;
}

static bool
r_match_check(RBodyContext *body, RAstRef ast, RTypeId type, uint32_t depth, RMatchPattern **out) {
    RAstNodeView view = {0};
    RAstTokenView first = {0};
    if (depth > r_frontend_tree_depth_limit(body->frontend)) {
        body->frontend->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
        return false;
    }
    RMatchPattern *p = r_context_allocate(body->frontend, sizeof(*p));
    if (p == NULL)
        return false;
    (void)memset(p, 0, sizeof(*p));
    *out = p;
    p->ast = ast;
    p->type = r_semantic_value_type(body->frontend, type);
    if (ast.node == 0U)
        return true;
    if (!r_ast_ref_view(body->frontend, ast, &view))
        return false;
    (void)r_body_ast_first_token(body->frontend, ast, &first);
    RAstRef field_probe;
    if (view.child_count == 0U ||
        r_ast_find_direct_child(body->frontend, ast, R_SYNTAX_MATCH_FIELD, &field_probe))
        first.kind = R_TOKEN_LBRACE;
    p->span = view.span;
    if (first.kind == R_TOKEN_KW_MOVE || first.kind == R_TOKEN_IDENTIFIER) {
        RAstRef qualified;
        if (!r_ast_find_direct_child(body->frontend, ast, R_SYNTAX_EXPRESSION, &qualified)) {
            p->kind = R_MATCH_BIND;
            p->moves = first.kind == R_TOKEN_KW_MOVE;
            p->contains_move = p->moves;
            for (uint32_t i = 0U; i < view.child_count; ++i) {
                RAstRef child;
                RAstTokenView token;
                if (r_ast_ref_child(body->frontend, ast, i, &child) &&
                    r_ast_ref_token(body->frontend, child, &token) &&
                    token.kind == R_TOKEN_IDENTIFIER)
                    p->name = token;
            }
            if (r_semantic_intern_name_equal(body->frontend, p->name.intern_id, "_")) {
                p->kind = R_MATCH_ANY;
                if (p->moves)
                    return r_match_error(body, p->span, "move requires a named pattern binding");
            }
            return true;
        }
    }
    if (first.kind == R_TOKEN_KW_VARIANT) {
        if (r_semantic_value_kind(body->frontend, p->type) == R_SEMANTIC_TYPE_STANDARD)
            return r_match_error(
                body,
                p->span,
                "opaque native outcome payloads require their typed switch projection");
        bool has_binding = false, binding_moves = false;
        RAstTokenView binding = {0};
        p->kind = R_MATCH_VARIANT;
        if (r_semantic_value_kind(body->frontend, p->type) == R_SEMANTIC_TYPE_ENUM &&
            r_semantic_tagged_enum(body->frontend, p->type) == NULL) {
            RAstRef name, nested;
            RExpressionResult constant = {0};
            uint32_t selected = 0U;
            if (!r_ast_find_direct_child(body->frontend, ast, R_SYNTAX_QUALIFIED_NAME, &name) ||
                !r_body_lower_enum_constant(body, name, &constant, &selected))
                return false;
            if (!constant.valid)
                return true;
            if (constant.type != p->type || selected == 0U ||
                r_ast_find_direct_child(body->frontend, ast, R_SYNTAX_MATCH_PATTERN, &nested))
                return r_match_error(
                    body, p->span, "pattern requires a fieldless variant of the exact enum type");
            p->kind = R_MATCH_CONSTANT;
            p->tag = body->frontend->semantic_variants[selected - 1U].value;
            return true;
        }
        if (!r_body_switch_pattern(body,
                                   ast,
                                   p->type,
                                   &p->tag,
                                   &p->payload_type,
                                   &binding,
                                   &has_binding,
                                   &binding_moves))
            return false;
        if (body->poisoned)
            return true;
        const RSemanticType *matched_type = r_semantic_type(body->frontend, p->type);
        if (matched_type != NULL && matched_type->kind == R_SEMANTIC_TYPE_OPTION && p->tag == 0U)
            p->payload_type = 0U;
        RAstRef child;
        if (r_ast_find_direct_child(body->frontend, ast, R_SYNTAX_MATCH_PATTERN, &child)) {
            if (p->payload_type == 0U ||
                r_semantic_value_kind(body->frontend, p->payload_type) == R_SEMANTIC_TYPE_VOID)
                return r_match_error(
                    body, p->span, "fieldless variant cannot have a payload pattern");
            if (!r_match_children(body, p, 1U) ||
                !r_match_check(body, child, p->payload_type, depth + 1U, &p->children[0]))
                return false;
            p->contains_move = p->children[0]->contains_move;
        } else {
            const RSemanticType *t = r_semantic_type(body->frontend, p->type);
            if (t != NULL && t->kind == R_SEMANTIC_TYPE_OPTION && p->tag == 0U)
                p->payload_type = 0U;
        }
        return true;
    }
    if (first.kind == R_TOKEN_LBRACE) {
        const RSemanticAggregate *a = r_semantic_aggregate_for_type(body->frontend, p->type);
        if (a == NULL || a->kind == R_SEMANTIC_AGGREGATE_ENUM)
            return r_match_error(body, p->span, "named field pattern requires a structure payload");
        const uint32_t first_field = a->first_field, count = a->field_count;
        const RSourceId module = a->module_source;
        p->kind = R_MATCH_FIELDS;
        if (!r_match_children(body, p, count))
            return false;
        for (uint32_t i = 0U; i < view.child_count; ++i) {
            RAstRef child, nested;
            RAstNodeView field_view;
            RAstTokenView name = {0};
            if (!r_ast_ref_child(body->frontend, ast, i, &child) ||
                !r_ast_view_kind(body->frontend, child, R_SYNTAX_MATCH_FIELD, &field_view))
                continue;
            for (uint32_t j = 0U; j < field_view.child_count; ++j) {
                RAstRef part;
                RAstTokenView token;
                if (r_ast_ref_child(body->frontend, child, j, &part) &&
                    r_ast_ref_token(body->frontend, part, &token) &&
                    ((token.kind == R_TOKEN_IDENTIFIER) || (token.kind == R_TOKEN_INTEGER_LITERAL)))
                    name = token;
            }
            uint32_t index = 0U;
            if (name.kind == R_TOKEN_INTEGER_LITERAL) {
                /* R-TYPE-0052 (L18.1): a numbered field pattern matches a tuple element. */
                if (!a->is_tuple || !r_tuple_index(body->frontend, &name, &index))
                    return r_match_error(
                        body, name.span, "a numbered field pattern matches an element of a tuple");
                if (index > count)
                    index = count;
            } else {
                while (index < count &&
                       body->frontend->semantic_fields[first_field + index].name_intern_id !=
                           name.intern_id)
                    ++index;
            }
            if (index == count)
                return r_match_error(body, name.span, "unknown field in pattern");
            if (p->children[index] != NULL)
                return r_match_error(body, name.span, "duplicate field in pattern");
            const RSemanticField field = body->frontend->semantic_fields[first_field + index];
            if (field.is_protected && module != body->source)
                return r_match_error(body, name.span, "pattern cannot expose a protected field");
            if (!r_ast_find_direct_child(body->frontend, child, R_SYNTAX_MATCH_PATTERN, &nested) ||
                !r_match_check(body, nested, field.type, depth + 1U, &p->children[index]))
                return false;
            p->contains_move |= p->children[index]->contains_move;
        }
        return true;
    }
    RAstRef expr;
    RExpressionResult constant = {0};
    RBodyConstantValue bits = {0};
    if (!r_ast_find_direct_child(body->frontend, ast, R_SYNTAX_EXPRESSION, &expr))
        return false;
    {
        RSymbolId matches;
        RTypeId label_type;
        if (!r_label_matcher(body, p->type, p->span, &matches, &label_type))
            return false;
        if (matches != R_SYMBOL_ID_INVALID) {
            /* L20.4: a label of a matcher is a constant of its label type. */
            if (!r_body_lower_value(body, expr, label_type, &constant) ||
                (constant.valid && !r_body_finish_value(body, label_type, &constant)))
                return false;
            if (!constant.valid)
                return true;
            if (r_semantic_value_type(body->frontend, constant.type) != label_type)
                return r_body_diagnostic(body,
                                         "R-DIAG-TYPE-001",
                                         "R-EXPR-0031",
                                         "a label has the label type of its case matcher",
                                         p->span);
            const RHirNode *literal = r_match_string_literal(body, constant.node);
            p->kind = R_MATCH_MATCHER;
            p->ast = expr;
            p->payload_type = label_type;
            if (literal != NULL) {
                p->string_label = true;
                p->tag = literal->aggregate_member;
            } else if (r_body_constant_scalar(body, constant.node, &bits) && bits.valid) {
                p->tag = bits.bits;
            } else {
                return r_match_error(
                    body, p->span, "a label of a case matcher is a string literal or a constant");
            }
            return true;
        }
    }
    if (!r_body_lower_value(body, expr, p->type, &constant))
        return false;
    if (!constant.valid)
        return true;
    if (r_match_string_type(body, p->type)) {
        const RHirNode *literal = r_match_string_literal(body, constant.node);
        if (literal == NULL)
            return r_match_error(body, p->span, "a str pattern is a string literal");
        p->kind = R_MATCH_STRING;
        p->ast = expr;
        p->tag = literal->aggregate_member;
        p->string_label = true;
        return true;
    }
    if (!r_body_constant_scalar(body, constant.node, &bits) || !bits.valid ||
        r_semantic_value_type(body->frontend, constant.type) != p->type)
        return r_match_error(
            body, p->span, "pattern requires a constant of the exact scrutinee type");
    p->kind = R_MATCH_CONSTANT;
    p->tag = bits.bits;
    return true;
}

static bool r_match_wild(const RMatchPattern *p) {
    return p == NULL || p->kind == R_MATCH_ANY || p->kind == R_MATCH_BIND;
}

/* Matrix specialization proves coverage of combinations, including nested payloads. */
static bool r_match_exhaustive(RBodyContext *body,
                               const RMatchPattern *const *matrix,
                               size_t rows,
                               const RTypeId *types,
                               size_t columns,
                               uint32_t depth,
                               uint32_t *steps,
                               bool *covered) {
    *covered = false;
    if (rows == 0U)
        return true;
    if (columns == 0U) {
        *covered = true;
        return true;
    }
    if (depth > r_frontend_tree_depth_limit(body->frontend) || ++*steps > 65536U) {
        body->frontend->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
        return false;
    }
    const RSemanticType *type = r_semantic_type(body->frontend, types[0]);
    const RSemanticAggregate *aggregate = r_semantic_aggregate_for_type(body->frontend, types[0]);
    uint32_t count = 0U;
    bool fields = aggregate != NULL && aggregate->kind != R_SEMANTIC_AGGREGATE_ENUM;
    if (fields)
        count = 1U;
    else if (aggregate != NULL && aggregate->kind == R_SEMANTIC_AGGREGATE_ENUM)
        count = aggregate->variant_count;
    else if (type != NULL &&
             (type->kind == R_SEMANTIC_TYPE_BOOL || type->kind == R_SEMANTIC_TYPE_OPTION ||
              type->kind == R_SEMANTIC_TYPE_RESULT))
        count = 2U;
    /* An open scalar domain is exhausted only by a wildcard row. */
    const uint32_t alternatives = count == 0U ? 1U : count;
    for (uint32_t tag = 0U; tag < alternatives; ++tag) {
        size_t extra = 0U;
        RTypeId payload = 0U;
        uint64_t key = tag;
        if (fields)
            extra = aggregate->field_count;
        else if (aggregate != NULL && aggregate->kind == R_SEMANTIC_AGGREGATE_ENUM) {
            const RSemanticVariant *v =
                &body->frontend->semantic_variants[aggregate->first_variant + tag];
            payload = v->payload_type;
            if (!aggregate->is_tagged)
                key = v->value;
        } else if (type != NULL && type->kind == R_SEMANTIC_TYPE_OPTION && tag == 1U)
            payload = type->base;
        else if (type != NULL && type->kind == R_SEMANTIC_TYPE_RESULT)
            payload = tag == 0U ? type->base : type->second;
        if (payload != 0U && r_semantic_value_kind(body->frontend, payload) != R_SEMANTIC_TYPE_VOID)
            extra = 1U;
        const size_t width = columns - 1U + extra;
        if (width > SIZE_MAX / sizeof(RTypeId) ||
            (width != 0U && rows > SIZE_MAX / width / sizeof(*matrix))) {
            body->frontend->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
            return false;
        }
        const RMatchPattern **next =
            r_context_allocate(body->frontend, (rows * (width == 0U ? 1U : width)) * sizeof(*next));
        RTypeId *next_types =
            r_context_allocate(body->frontend, (width == 0U ? 1U : width) * sizeof(*next_types));
        if (next == NULL || next_types == NULL) {
            r_context_free(body->frontend, next);
            r_context_free(body->frontend, next_types);
            return false;
        }
        for (size_t j = 0U; j < extra; ++j)
            next_types[j] =
                fields ? body->frontend->semantic_fields[aggregate->first_field + j].type : payload;
        for (size_t j = 1U; j < columns; ++j)
            next_types[extra + j - 1U] = types[j];
        size_t next_rows = 0U;
        for (size_t row = 0U; row < rows; ++row) {
            const RMatchPattern *p = matrix[row * columns];
            /* A matcher label never covers its open domain (L20.4). */
            if (!r_match_wild(p) &&
                (count == 0U || p->kind == R_MATCH_MATCHER || (!fields && p->tag != key)))
                continue;
            for (size_t j = 0U; j < extra; ++j)
                next[next_rows * width + j] =
                    r_match_wild(p) || j >= p->child_count ? NULL : p->children[j];
            for (size_t j = 1U; j < columns; ++j)
                next[next_rows * width + extra + j - 1U] = matrix[row * columns + j];
            ++next_rows;
        }
        bool branch = false;
        const bool ok = r_match_exhaustive(
            body, next, next_rows, next_types, width, depth + 1U, steps, &branch);
        r_context_free(body->frontend, next);
        r_context_free(body->frontend, next_types);
        if (!ok || !branch)
            return ok;
    }
    *covered = true;
    return true;
}

static bool r_match_field_place(RBodyContext *body,
                                RExpressionResult base,
                                uint32_t index,
                                RSourceSpan span,
                                RExpressionResult *out) {
    const RSemanticAggregate *a = r_semantic_aggregate_for_type(body->frontend, base.type);
    if (a == NULL || index >= a->field_count)
        return false;
    const uint32_t member = a->first_field + index + 1U;
    const RTypeId type = body->frontend->semantic_fields[member - 1U].type;
    RHirVector child = {&base.node, 1U, 1U};
    *out = base;
    if (!r_body_append_node(body,
                            R_HIR_FIELD_PLACE,
                            span,
                            type,
                            base.symbol,
                            R_TOKEN_INVALID,
                            0U,
                            true,
                            &child,
                            &out->node))
        return false;
    out->type = type;
    out->is_place = true;
    bool automatic_storage = false;
    if (r_semantic_type_view_component_count(body->frontend, type, NULL) == 0U) {
        if (!r_body_place_is_automatic_storage(body, out->node, &automatic_storage))
            return false;
        if (automatic_storage) {
            out->borrow_origin = base.symbol;
            out->borrow_origin_set = 0U;
            out->borrow_origin_multiple = false;
            out->borrow_referent = 0U;
        }
    }
    RHirNode *node = r_body_hir_node(body, out->node);
    node->aggregate_member = member;
    node->borrow_origin = out->borrow_origin;
    node->borrow_origin_set = out->borrow_origin_set;
    node->borrow_origin_multiple = out->borrow_origin_multiple;
    node->borrow_referent = out->borrow_referent;
    return r_body_refine_projected_value(body, out);
}

static bool r_match_borrow(RBodyContext *body,
                           RExpressionResult place,
                           RSourceSpan span,
                           bool shared,
                           RExpressionResult *out) {
    bool automatic_storage = false;
    if (!r_body_place_is_automatic_storage(body, place.node, &automatic_storage))
        return false;
    if (automatic_storage) {
        place.borrow_origin = place.symbol;
        place.borrow_origin_set = 0U;
        place.borrow_origin_multiple = false;
        place.borrow_referent = R_BORROW_REFERENT_AUTOMATIC_STORAGE;
        place.projected_borrow_state = 0U;
    }
    RTypeId borrow;
    if (!r_body_standard_borrow_type(
            body, r_semantic_value_type(body->frontend, place.type), shared, &borrow))
        return false;
    return r_body_form_safe_borrow(body, span, borrow, &place, out);
}

static bool r_match_payload_place(RBodyContext *body,
                                  const RMatchPattern *p,
                                  RExpressionResult base,
                                  bool shared,
                                  RExpressionResult *out) {
    RExpressionResult borrow = {0}, payload = {0};
    RTypeId borrow_type;
    if (!r_match_borrow(body, base, p->span, shared, &borrow) || !borrow.valid ||
        !r_body_standard_borrow_type(body, p->payload_type, shared, &borrow_type))
        return false;
    RHirVector child = {&borrow.node, 1U, 1U};
    payload = borrow;
    if (!r_body_append_node(body,
                            R_HIR_VARIANT_PAYLOAD,
                            p->span,
                            borrow_type,
                            base.symbol,
                            R_TOKEN_AMP,
                            p->tag,
                            false,
                            &child,
                            &payload.node))
        return false;
    payload.type = borrow_type;
    RHirNode *node = r_body_hir_node(body, payload.node);
    node->borrow_origin = borrow.borrow_origin;
    node->borrow_origin_set = borrow.borrow_origin_set;
    node->borrow_origin_multiple = borrow.borrow_origin_multiple;
    node->borrow_referent = borrow.borrow_referent;
    if (!r_body_form_deref_place(body, payload, p->span, out))
        return false;
    out->borrow_origin = base.borrow_origin;
    out->borrow_origin_set = base.borrow_origin_set;
    out->borrow_origin_multiple = base.borrow_origin_multiple;
    out->borrow_referent = base.borrow_referent;
    out->projected_borrow_state = base.projected_borrow_state;
    node = r_body_hir_node(body, out->node);
    node->borrow_origin = out->borrow_origin;
    node->borrow_origin_set = out->borrow_origin_set;
    node->borrow_origin_multiple = out->borrow_origin_multiple;
    node->borrow_referent = out->borrow_referent;
    node->projected_borrow_state = out->projected_borrow_state;
    return true;
}

static bool
r_match_alias(RBodyContext *body, const RMatchPattern *p, RExpressionResult place, size_t base) {
    if (p == NULL || p->kind != R_MATCH_BIND)
        return true;
    for (size_t i = base; i < body->active_symbols.count; ++i)
        if (r_body_symbol(body, body->active_symbols.items[i])->name_intern_id == p->name.intern_id)
            return r_match_error(body, p->span, "duplicate binding in pattern");
    if (r_body_shadows_forbidden_name(body, &p->name))
        return r_match_error(
            body, p->span, "pattern binding shadows a parameter or module function");
    const RHirNode *source = r_body_hir_node(body, place.node);
    if (source == NULL)
        return false;
    const RHirNode original = *source;
    RExpressionResult readonly = place;
    if (!r_body_append_node(body,
                            original.kind,
                            original.span,
                            original.type,
                            original.symbol,
                            R_TOKEN_KW_CASE,
                            original.integer_value,
                            original.is_move,
                            NULL,
                            &readonly.node))
        return false;
    RHirNode *alias = r_body_hir_node(body, readonly.node);
    *alias = original;
    alias->operation = R_TOKEN_KW_CASE;
    RSemanticSymbol symbol = {0};
    RSymbolId id;
    symbol.kind = R_SEMANTIC_SYMBOL_LOCAL;
    symbol.name_span = p->name.span;
    symbol.name_intern_id = p->name.intern_id;
    symbol.scope_start = p->span.start;
    symbol.module_source = body->source;
    symbol.parent = body->function_symbol;
    symbol.type = readonly.type;
    symbol.object_state = R_SEMANTIC_OBJECT_STATE_INITIALIZED;
    symbol.signature_supported = true;
    symbol.pattern_place = readonly.node;
    symbol.borrow_origin = readonly.borrow_origin;
    symbol.borrow_origin_set = readonly.borrow_origin_set;
    symbol.borrow_origin_multiple = readonly.borrow_origin_multiple;
    symbol.borrow_referent = readonly.borrow_referent;
    return r_semantic_append_symbol(body->frontend, symbol, &id) &&
           r_symbol_vector_push(body->frontend, &body->active_symbols, id);
}

static bool r_match_compare(RBodyContext *body,
                            RSourceSpan span,
                            RTokenKind op,
                            const RExpressionResult *left,
                            const RExpressionResult *right,
                            RExpressionResult *out) {
    const RExpressionResult a = *left, b = *right;
    return r_iteration_compare(body, span, op, &a, &b, out);
}

/* L20.3: `std.bytes::equal` over the bytes of a `str` place and of a string literal label. */
static bool r_label_string_test(RBodyContext *body,
                                RExpressionResult place,
                                RAstRef label,
                                RTypeId label_type,
                                RSourceSpan span,
                                RExpressionResult *test) {
    RExpressionResult actual = place, expected = {0};
    RTypeId byte_type, bytes_type, bool_type;
    RHirNodeId operands[2];
    if (!r_body_place_to_value(body, &actual) ||
        !r_body_lower_value(body, label, label_type, &expected) ||
        !r_semantic_type_from_token(body->frontend, R_TOKEN_KW_U8, &byte_type) ||
        !r_semantic_type_from_token(body->frontend, R_TOKEN_KW_BOOL, &bool_type) ||
        !r_semantic_intern_derived_type(body->frontend,
                                        R_SEMANTIC_TYPE_SLICE,
                                        byte_type,
                                        R_TYPE_ID_INVALID,
                                        UINT64_C(0),
                                        R_SEMANTIC_TYPE_FLAG_SHARED,
                                        &bytes_type))
        return false;
    /* L24-1: the call borrows both operands only while it runs, so a `constexpr str` subject or
       label becomes a byte view through `str` like a runtime one. */
    const bool previous_call_view = body->allow_call_bounded_byte_view;
    body->allow_call_bounded_byte_view = true;
    const bool converted = r_body_finish_value(body, bytes_type, &actual) &&
                           r_body_finish_value(body, bytes_type, &expected);
    body->allow_call_bounded_byte_view = previous_call_view;
    if (!converted)
        return false;
    if (!actual.valid || !expected.valid) {
        r_body_expression_invalid(test);
        return true;
    }
    operands[0] = actual.node;
    operands[1] = expected.node;
    RHirVector children = {operands, 2U, 2U};
    return r_body_append_standard_call(
        body, span, bool_type, bool_type, R_STANDARD_CALL_BYTES_EQUAL, &children, NULL, test);
}

/* The constant a pattern compares with: an enumerator of an enum without payloads is an enum
   constant naming its variant (L27-3), any other scalar a literal. */
static bool r_match_constant(RBodyContext *body,
                             RSourceSpan span,
                             RTypeId type,
                             uint64_t bits,
                             RExpressionResult *result) {
    const RSemanticAggregate *aggregate = r_semantic_aggregate_for_type(body->frontend, type);
    if ((aggregate != NULL) && (aggregate->kind == R_SEMANTIC_AGGREGATE_ENUM) &&
        !aggregate->is_tagged) {
        for (uint32_t index = 0U; index < aggregate->variant_count; ++index) {
            const uint32_t global = aggregate->first_variant + index;
            RHirNode *node;
            if (body->frontend->semantic_variants[global].value != bits) {
                continue;
            }
            r_body_expression_invalid(result);
            if (!r_body_append_node(body,
                                    R_HIR_ENUM_CONSTANT,
                                    span,
                                    type,
                                    R_SYMBOL_ID_INVALID,
                                    R_TOKEN_INVALID,
                                    bits,
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
            node->aggregate_member = global + UINT32_C(1);
            result->type = type;
            result->valid = true;
            return true;
        }
    }
    return r_iteration_literal(body, span, type, bits, result);
}

static bool r_match_test(RBodyContext *body,
                         const RMatchPattern *p,
                         RExpressionResult place,
                         size_t scope_base,
                         RExpressionResult *test) {
    RTypeId bool_type;
    if (!r_semantic_type_from_token(body->frontend, R_TOKEN_KW_BOOL, &bool_type) ||
        !r_iteration_literal(body, p->span, bool_type, 1U, test))
        return false;
    if (r_match_wild(p))
        return r_match_alias(body, p, place, scope_base);
    if (p->kind == R_MATCH_CONSTANT) {
        RExpressionResult actual = place, expected = {0};
        return r_body_place_to_value(body, &actual) &&
               r_match_constant(body, p->span, p->type, p->tag, &expected) &&
               r_match_compare(body, p->span, R_TOKEN_EQUAL_EQUAL, &actual, &expected, test);
    }
    if (p->kind == R_MATCH_STRING)
        return r_label_string_test(body, place, p->ast, p->type, p->span, test);
    if (p->kind == R_MATCH_MATCHER) {
        RSymbolId matches;
        RTypeId label_type;
        if (!r_label_matcher(body, p->type, p->span, &matches, &label_type))
            return false;
        return r_label_matcher_test(body, place, matches, p->ast, label_type, p->span, test);
    }
    if (p->kind == R_MATCH_VARIANT) {
        RExpressionResult borrow = {0}, tag = {0}, expected = {0};
        RTypeId u32;
        if (!r_match_borrow(body, place, p->span, true, &borrow) || !borrow.valid ||
            !r_semantic_type_from_token(body->frontend, R_TOKEN_KW_U32, &u32))
            return false;
        RHirVector child = {&borrow.node, 1U, 1U};
        if (!r_body_append_node(body,
                                R_HIR_VARIANT_TAG,
                                p->span,
                                u32,
                                0U,
                                R_TOKEN_INVALID,
                                0U,
                                false,
                                &child,
                                &tag.node))
            return false;
        tag.type = u32;
        tag.valid = true;
        if (!r_iteration_literal(body, p->span, u32, p->tag, &expected) ||
            !r_match_compare(body, p->span, R_TOKEN_EQUAL_EQUAL, &tag, &expected, test))
            return false;
        if (p->child_count != 0U) {
            RExpressionResult payload = {0}, nested = {0};
            if (!r_match_payload_place(body, p, place, true, &payload) ||
                !r_match_test(body, p->children[0], payload, scope_base, &nested) ||
                !r_match_compare(body, p->span, R_TOKEN_AMP_AMP, test, &nested, test))
                return false;
        }
    } else if (p->kind == R_MATCH_FIELDS) {
        for (uint32_t i = 0U; i < p->child_count; ++i) {
            if (p->children[i] == NULL)
                continue;
            RExpressionResult field = {0}, nested = {0};
            if (!r_match_field_place(body, place, i, p->span, &field) ||
                !r_match_test(body, p->children[i], field, scope_base, &nested) ||
                !r_match_compare(body, p->span, R_TOKEN_AMP_AMP, test, &nested, test))
                return false;
        }
    }
    return true;
}

/* The selected arm partitions owned storage before any fallible arm expression. Each
 * component is transferred once, including unnamed components that need cleanup. */
static bool r_match_transfer(RBodyContext *body,
                             RExpressionResult place,
                             RSourceSpan span,
                             RExpressionResult *value) {
    RHirVector child = {&place.node, 1U, 1U};
    const bool copy = r_semantic_type_is_copy(body->frontend, place.type);
    *value = place;
    value->is_place = false;
    value->type = r_semantic_value_type(body->frontend, place.type);
    if (!r_body_append_node(body,
                            copy ? R_HIR_LOAD : R_HIR_MOVE,
                            span,
                            value->type,
                            place.symbol,
                            copy ? R_TOKEN_INVALID : R_TOKEN_KW_VARIANT,
                            0U,
                            false,
                            &child,
                            &value->node))
        return false;
    RHirNode *node = r_body_hir_node(body, value->node);
    node->borrow_origin = value->borrow_origin;
    node->borrow_referent = value->borrow_referent;
    node->borrow_origin_set = value->borrow_origin_set;
    node->borrow_origin_multiple = value->borrow_origin_multiple;
    node->projected_borrow_state = value->projected_borrow_state;
    return true;
}

static bool r_match_commit(RBodyContext *body,
                           const RMatchPattern *p,
                           RExpressionResult place,
                           RSourceSpan span,
                           size_t base,
                           RHirVector *nodes,
                           const RExpressionResult *provenance) {
    if (p == NULL || r_match_wild(p) || p->kind == R_MATCH_CONSTANT || p->kind == R_MATCH_STRING ||
        p->kind == R_MATCH_MATCHER) {
        RExpressionResult value = {0};
        RSymbolId local = 0U;
        RHirNodeId declaration = 0U;
        if (!r_match_transfer(body, place, span, &value))
            return false;
        if (r_semantic_type_contains_borrow(body->frontend, value.type) ||
            r_generic_type_is_dependent(body->frontend, value.type, 0U)) {
            value.borrow_origin = provenance->borrow_origin;
            value.borrow_origin_set = provenance->borrow_origin_set;
            value.borrow_origin_multiple = provenance->borrow_origin_multiple;
            value.borrow_referent = provenance->borrow_referent;
            value.projected_borrow_state = 0U;
            RHirNode *transferred = r_body_hir_node(body, value.node);
            transferred->borrow_origin = value.borrow_origin;
            transferred->borrow_origin_set = value.borrow_origin_set;
            transferred->borrow_origin_multiple = value.borrow_origin_multiple;
            transferred->borrow_referent = value.borrow_referent;
            transferred->projected_borrow_state = 0U;
        }
        if (p != NULL && p->kind == R_MATCH_BIND && p->moves) {
            if (!r_body_declare_local(body,
                                      base,
                                      p->name,
                                      value.type,
                                      p->ast.node,
                                      false,
                                      false,
                                      span,
                                      &value,
                                      false,
                                      false,
                                      &declaration))
                return false;
            return r_hir_vector_push(body->frontend, nodes, declaration);
        }
        /* M27-2: every piece is its own hidden local, so two wildcard fields of one pattern
           do not declare one name twice. */
        char label[48];
        (void)snprintf(label, sizeof(label), "$match_piece_%zu", body->active_symbols.count);
        if (!r_iteration_declare_hidden(body,
                                        base,
                                        label,
                                        value.type,
                                        p == NULL ? 0U : p->ast.node,
                                        span,
                                        &value,
                                        nodes,
                                        &local))
            return false;
        if (p != NULL && p->kind == R_MATCH_BIND) {
            RExpressionResult bound = {0};
            if (!r_body_symbol_place(body, local, span, &bound) ||
                !r_match_alias(body, p, bound, base))
                return false;
        }
        return true;
    }
    const RSemanticAggregate *aggregate = r_semantic_aggregate_for_type(body->frontend, place.type);
    if (aggregate != NULL && aggregate->drop_function != 0U)
        return r_match_error(
            body, span, "a pattern cannot partition a value with a user drop hook");
    if (p->kind == R_MATCH_FIELDS) {
        for (uint32_t i = 0U; i < p->child_count; ++i) {
            RExpressionResult field = {0};
            if (!r_match_field_place(body, place, i, span, &field) ||
                !r_match_commit(body, p->children[i], field, span, base, nodes, provenance))
                return false;
        }
        return true;
    }
    if (p->kind == R_MATCH_VARIANT) {
        if (p->payload_type == 0U ||
            r_semantic_value_kind(body->frontend, p->payload_type) == R_SEMANTIC_TYPE_VOID)
            return true;
        RExpressionResult payload = {0};
        if (!r_match_payload_place(body, p, place, false, &payload))
            return false;
        return r_match_commit(body,
                              p->child_count == 0U ? NULL : p->children[0],
                              payload,
                              span,
                              base,
                              nodes,
                              provenance);
    }
    return false;
}

/* The components now own the complete live representation. Async cancellation must not
 * visit the obsolete enclosing object's initialization flag. Sync cleanup is explicit. */
static bool
r_match_forget_partition(RBodyContext *body, RSymbolId root, RSourceSpan span, RHirVector *nodes) {
    RExpressionResult place = {0};
    RHirNodeId forget;
    RTypeId void_type;
    if (!r_body_symbol_place(body, root, span, &place))
        return false;
    if (r_semantic_type_is_copy(body->frontend, place.type))
        return true;
    RHirVector child = {&place.node, 1U, 1U};
    if (!r_semantic_type_from_token(body->frontend, R_TOKEN_KW_VOID, &void_type) ||
        !r_body_append_node(body,
                            R_HIR_DROP,
                            span,
                            void_type,
                            root,
                            R_TOKEN_KW_VARIANT,
                            0U,
                            false,
                            &child,
                            &forget) ||
        !r_hir_vector_push(body->frontend, nodes, forget))
        return false;
    r_body_symbol_mut(body, root)->object_state = R_SEMANTIC_OBJECT_STATE_MOVED;
    return true;
}

static bool r_match_scope_value(RBodyContext *body,
                                RSourceSpan span,
                                RHirVector *nodes,
                                RExpressionResult *value,
                                size_t cleanup_base) {
    value->is_integer_literal_form = false;
    value->is_unsuffixed_integer_literal = false;
    const uint32_t value_index = (uint32_t)nodes->count;
    if (!r_hir_vector_push(body->frontend, nodes, value->node))
        return false;
    if (r_semantic_type_contains_borrow(body->frontend, value->type) ||
        r_semantic_type_contains_slice(body->frontend, value->type)) {
        for (size_t i = cleanup_base; i < body->active_symbols.count; ++i) {
            const RSymbolId id = body->active_symbols.items[i];
            if (r_semantic_borrow_origin_contains(body->frontend,
                                                  value->borrow_origin,
                                                  value->borrow_origin_set,
                                                  value->borrow_origin_multiple,
                                                  id))
                return r_body_diagnostic(
                    body,
                    "R-DIAG-BORROW-001",
                    "R-EXPR-0031",
                    "match result cannot borrow storage owned by the selected arm",
                    span);
        }
    }
    for (size_t i = body->active_symbols.count; i > cleanup_base; --i)
        if (!r_body_append_implicit_drop(body, body->active_symbols.items[i - 1U], span, nodes))
            return false;
    if (!r_body_append_node(body,
                            R_HIR_VALUE_SCOPE,
                            span,
                            value->type,
                            0U,
                            R_TOKEN_INVALID,
                            value_index,
                            false,
                            nodes,
                            &value->node))
        return false;
    RHirNode *node = r_body_hir_node(body, value->node);
    node->borrow_origin = value->borrow_origin;
    node->borrow_origin_set = value->borrow_origin_set;
    node->borrow_origin_multiple = value->borrow_origin_multiple;
    node->borrow_referent = value->borrow_referent;
    node->projected_borrow_state = value->projected_borrow_state;
    return true;
}

static bool r_body_lower_match(RBodyContext *body,
                               RAstRef expression,
                               RTypeId expected,
                               RExpressionResult *result) {
    RAstNodeView view;
    RAstRef input;
    RExpressionResult place = {0}, provenance = {0};
    RHirVector prefix = {0}, values = {0};
    RMatchArm *arms = NULL;
    const RMatchPattern **coverage = NULL;
    RSemanticObjectSnapshot *entry = NULL, *merged = NULL, *exit_states = NULL;
    size_t entry_count = 0U, merged_count = 0U, exit_count = 0U;
    const size_t outer_base = body->active_symbols.count;
    size_t arm_base = outer_base, count = 0U, checked = 0U;
    RSymbolId temporary = 0U;
    RTypeId common = expected;
    bool success = false;
    r_body_expression_invalid(result);
    if (!r_ast_ref_view(body->frontend, expression, &view) ||
        !r_ast_find_direct_child(body->frontend, expression, R_SYNTAX_EXPRESSION, &input) ||
        !r_body_lower_expression(body, input, 0U, &place))
        goto cleanup;
    /* R-EXPR-0031 (L43): a string or builder place is matched as its str view; a temporary or
       moved input keeps its type, so a move binding still takes the owner. */
    if (place.valid && place.is_place && !r_body_view_string_owner(body, false, &place))
        goto cleanup;
    if (!place.valid) {
        success = true;
        goto cleanup;
    }
    provenance = place;
    if (!place.is_place && !r_iteration_declare_hidden(body,
                                                       outer_base,
                                                       "$match",
                                                       place.type,
                                                       expression.node,
                                                       view.span,
                                                       &place,
                                                       &prefix,
                                                       &temporary))
        goto cleanup;
    if (temporary != 0U && !r_body_symbol_place(body, temporary, view.span, &place))
        goto cleanup;
    arm_base = body->active_symbols.count;
    arms = r_context_allocate(body->frontend, (size_t)view.child_count * sizeof(*arms));
    coverage = r_context_allocate(body->frontend, (size_t)view.child_count * sizeof(*coverage));
    if (arms == NULL || coverage == NULL)
        goto cleanup;
    (void)memset(arms, 0, (size_t)view.child_count * sizeof(*arms));
    for (uint32_t i = 0U; i < view.child_count; ++i) {
        RAstRef child, pattern = {0};
        RAstNodeView arm;
        if (!r_ast_ref_child(body->frontend, expression, i, &child) ||
            !r_ast_view_kind(body->frontend, child, R_SYNTAX_MATCH_ARM, &arm))
            continue;
        RMatchArm *a = &arms[count++];
        (void)r_ast_find_direct_child(body->frontend, child, R_SYNTAX_MATCH_PATTERN, &pattern);
        (void)r_ast_find_direct_child(body->frontend, child, R_SYNTAX_MATCH_GUARD, &a->guard);
        if ((!r_ast_find_direct_child(body->frontend, child, R_SYNTAX_EXPRESSION, &a->value) &&
             !r_ast_find_direct_child(
                 body->frontend, child, R_SYNTAX_THROW_STATEMENT, &a->thrown)) ||
            !r_match_check(body, pattern, place.type, 0U, &a->pattern))
            goto cleanup;
        if (body->poisoned) {
            success = true;
            goto cleanup;
        }
        if (pattern.node == 0U)
            a->pattern->span = arm.span;
        if (a->pattern->contains_move && temporary == 0U &&
            !r_semantic_type_is_copy(body->frontend, place.type)) {
            success = r_match_error(
                body,
                a->pattern->span,
                "move payload patterns require match (move value) or an owned temporary");
            goto cleanup;
        }
        if (a->guard.node == 0U) {
            /* L20.3, L20.4: a string or matcher label occurs once; a repeated one could never be
               selected. */
            const bool label =
                (a->pattern->kind == R_MATCH_STRING) || (a->pattern->kind == R_MATCH_MATCHER);
            for (size_t prior = 0U; label && (prior < checked); ++prior) {
                if ((coverage[prior]->kind == a->pattern->kind) &&
                    (coverage[prior]->string_label == a->pattern->string_label) &&
                    (coverage[prior]->tag == a->pattern->tag)) {
                    success = r_match_error(body, a->pattern->span, "a label is repeated");
                    goto cleanup;
                }
            }
            coverage[checked++] = a->pattern;
        }
    }
    bool needs_partition = false;
    for (size_t i = 0U; i < count; ++i)
        needs_partition |= arms[i].pattern->contains_move;
    if (needs_partition && temporary == 0U) {
        RExpressionResult copy = place;
        if (!r_body_place_to_value(body, &copy) ||
            !r_iteration_declare_hidden(body,
                                        outer_base,
                                        "$match",
                                        copy.type,
                                        expression.node,
                                        view.span,
                                        &copy,
                                        &prefix,
                                        &temporary) ||
            !r_body_symbol_place(body, temporary, view.span, &place))
            goto cleanup;
        arm_base = body->active_symbols.count;
    }
    bool exhaustive = false;
    uint32_t steps = 0U;
    const RTypeId type = r_semantic_value_type(body->frontend, place.type);
    if (!r_match_exhaustive(body, coverage, checked, &type, 1U, 0U, &steps, &exhaustive))
        goto cleanup;
    if (!exhaustive) {
        success = r_match_error(
            body, view.span, "match patterns are not exhaustive; guards do not establish coverage");
        goto cleanup;
    }
    if (!r_body_capture_active_object_states(body, &entry, &entry_count))
        goto cleanup;
    for (size_t i = 0U; i < count; ++i) {
        RMatchArm *a = &arms[i];
        body->active_symbols.count = arm_base;
        if (!r_body_restore_active_object_states(body, entry, entry_count) ||
            !r_match_test(body, a->pattern, place, arm_base, &a->test))
            goto cleanup;
        if (a->guard.node != 0U) {
            RAstRef guard;
            RExpressionResult test = {0};
            RTypeId bool_type;
            if (!r_ast_find_direct_child(body->frontend, a->guard, R_SYNTAX_EXPRESSION, &guard) ||
                !r_semantic_type_from_token(body->frontend, R_TOKEN_KW_BOOL, &bool_type) ||
                !r_body_lower_value(body, guard, bool_type, &test) || !test.valid ||
                !r_match_compare(body, view.span, R_TOKEN_AMP_AMP, &a->test, &test, &a->test))
                goto cleanup;
            RSemanticObjectSnapshot *after_guard = NULL, *failed = NULL;
            size_t guard_count = 0U, failed_count = 0U;
            const size_t aliases = body->active_symbols.count;
            body->active_symbols.count = arm_base;
            bool guard_ok = r_body_capture_active_object_states(body, &after_guard, &guard_count);
            if (guard_ok && !r_match_wild(a->pattern))
                guard_ok = r_body_merge_branch_object_states(
                    body, entry, after_guard, entry_count, view.span);
            if (guard_ok)
                guard_ok = r_body_capture_active_object_states(body, &failed, &failed_count) &&
                           r_body_restore_active_object_states(body, after_guard, guard_count);
            body->active_symbols.count = aliases;
            r_context_free(body->frontend, after_guard);
            if (!guard_ok) {
                r_context_free(body->frontend, failed);
                goto cleanup;
            }
            r_context_free(body->frontend, entry);
            entry = failed;
            entry_count = failed_count;
        }
        if (a->pattern->contains_move) {
            body->active_symbols.count = arm_base;
            if (!r_match_commit(
                    body, a->pattern, place, view.span, arm_base, &values, &provenance) ||
                !r_match_forget_partition(body, temporary, view.span, &values)) {
                goto cleanup;
            }
        }
        if (a->thrown.node != 0U) {
            /* The throw exits the arm; its value is the unreachable completion of never. */
            RHirNodeId thrown = R_HIR_NODE_ID_INVALID;
            RAstNodeView thrown_view;
            RTypeId never_type;
            bool thrown_returns = false;
            if (!r_body_lower_statement(body, a->thrown, arm_base, &thrown, &thrown_returns))
                goto cleanup;
            if (body->poisoned) {
                success = true;
                goto cleanup;
            }
            r_body_expression_invalid(&a->result);
            if ((thrown != R_HIR_NODE_ID_INVALID) &&
                !r_hir_vector_push(body->frontend, &values, thrown))
                goto cleanup;
            if (!r_semantic_type_from_token(body->frontend, R_TOKEN_KW_NEVER, &never_type) ||
                !r_body_append_node(body,
                                    R_HIR_DEFAULT_VALUE,
                                    r_ast_ref_view(body->frontend, a->thrown, &thrown_view)
                                        ? thrown_view.span
                                        : view.span,
                                    never_type,
                                    R_SYMBOL_ID_INVALID,
                                    R_TOKEN_INVALID,
                                    UINT64_C(0),
                                    false,
                                    NULL,
                                    &a->result.node))
                goto cleanup;
            a->result.type = never_type;
            a->result.valid = true;
        } else if (!r_body_lower_value(body, a->value, common, &a->result))
            goto cleanup;
        if (!a->result.valid) {
            success = true;
            goto cleanup;
        }
        const bool arm_continues =
            r_semantic_value_kind(body->frontend, a->result.type) != R_SEMANTIC_TYPE_NEVER;
        if (common == 0U && arm_continues)
            common = a->result.type;
        if (a->result.type != common &&
            r_semantic_value_kind(body->frontend, a->result.type) != R_SEMANTIC_TYPE_NEVER) {
            success = r_body_diagnostic(body,
                                        "R-DIAG-TYPE-001",
                                        "R-EXPR-0031",
                                        "match arms require one result type",
                                        view.span);
            goto cleanup;
        }
        if (r_semantic_value_kind(body->frontend, common) == R_SEMANTIC_TYPE_VOID) {
            success = r_match_error(body, view.span, "match arms must produce a value");
            goto cleanup;
        }
        if (!r_match_scope_value(
                body, view.span, &values, &a->result, temporary != 0U ? outer_base : arm_base)) {
            goto cleanup;
        }
        r_context_free(body->frontend, values.items);
        values = (RHirVector){0};
        body->active_symbols.count = outer_base;
        r_context_free(body->frontend, exit_states);
        exit_states = NULL;
        if (!arm_continues) {
            body->active_symbols.count = arm_base;
            continue;
        }
        if (!r_body_capture_active_object_states(body, &exit_states, &exit_count))
            goto cleanup;
        if (merged == NULL) {
            merged = exit_states;
            merged_count = exit_count;
            exit_states = NULL;
        } else {
            if (!r_body_merge_branch_object_states(
                    body, merged, exit_states, merged_count, view.span))
                goto cleanup;
            r_context_free(body->frontend, merged);
            merged = NULL;
            if (!r_body_capture_active_object_states(body, &merged, &merged_count))
                goto cleanup;
        }
        body->active_symbols.count = arm_base;
    }
    body->active_symbols.count = outer_base;
    if (merged != NULL && !r_body_restore_active_object_states(body, merged, merged_count))
        goto cleanup;
    if (common == 0U && !r_semantic_type_from_token(body->frontend, R_TOKEN_KW_NEVER, &common))
        goto cleanup;
    *result = arms[count - 1U].result;
    bool tail_continues =
        r_semantic_value_kind(body->frontend, result->type) != R_SEMANTIC_TYPE_NEVER;
    result->is_integer_literal_form = false;
    result->is_unsuffixed_integer_literal = false;
    for (size_t i = count - 1U; i > 0U; --i) {
        RMatchArm *a = &arms[i - 1U];
        RHirNodeId nodes[3] = {a->test.node, a->result.node, result->node};
        RHirVector children = {nodes, 3U, 3U};
        const bool left_continues =
            r_semantic_value_kind(body->frontend, a->result.type) != R_SEMANTIC_TYPE_NEVER;
        if (left_continues && !tail_continues) {
            result->borrow_origin = a->result.borrow_origin;
            result->borrow_origin_set = a->result.borrow_origin_set;
            result->borrow_origin_multiple = a->result.borrow_origin_multiple;
            result->borrow_referent = a->result.borrow_referent;
            result->projected_borrow_state = a->result.projected_borrow_state;
        }
        if (left_continues && tail_continues &&
            (!r_semantic_merge_borrow_origins(body->frontend,
                                              a->result.borrow_origin,
                                              a->result.borrow_origin_set,
                                              a->result.borrow_origin_multiple,
                                              result->borrow_origin,
                                              result->borrow_origin_set,
                                              result->borrow_origin_multiple,
                                              &result->borrow_origin,
                                              &result->borrow_origin_set,
                                              &result->borrow_origin_multiple) ||
             !r_body_merge_projected_borrow_states(body,
                                                   a->result.projected_borrow_state,
                                                   result->projected_borrow_state,
                                                   &result->projected_borrow_state)))
            goto cleanup;
        tail_continues |= left_continues;
        if (!r_body_append_node(body,
                                R_HIR_CONDITIONAL,
                                view.span,
                                common,
                                0U,
                                R_TOKEN_QUESTION,
                                0U,
                                false,
                                &children,
                                &result->node))
            goto cleanup;
        RHirNode *node = r_body_hir_node(body, result->node);
        node->borrow_origin = result->borrow_origin;
        node->borrow_origin_set = result->borrow_origin_set;
        node->borrow_origin_multiple = result->borrow_origin_multiple;
        node->projected_borrow_state = result->projected_borrow_state;
    }
    result->type = common;
    if (prefix.count != 0U) {
        if (!r_hir_vector_push(body->frontend, &prefix, result->node) ||
            !r_body_append_node(body,
                                R_HIR_VALUE_SCOPE,
                                view.span,
                                common,
                                0U,
                                R_TOKEN_INVALID,
                                prefix.count - 1U,
                                false,
                                &prefix,
                                &result->node))
            goto cleanup;
    }
    RHirNode *final_node = r_body_hir_node(body, result->node);
    final_node->borrow_origin = result->borrow_origin;
    final_node->borrow_origin_set = result->borrow_origin_set;
    final_node->borrow_origin_multiple = result->borrow_origin_multiple;
    final_node->borrow_referent = result->borrow_referent;
    final_node->projected_borrow_state = result->projected_borrow_state;
    success = true;
cleanup:
    if (body->poisoned && body->frontend->resource_status == R_FRONTEND_OK)
        success = true;
    body->active_symbols.count = outer_base;
    for (size_t i = 0U; i < count; ++i)
        r_match_destroy(body, arms[i].pattern);
    r_context_free(body->frontend, arms);
    r_context_free(body->frontend, coverage);
    r_context_free(body->frontend, prefix.items);
    r_context_free(body->frontend, values.items);
    r_context_free(body->frontend, entry);
    r_context_free(body->frontend, merged);
    r_context_free(body->frontend, exit_states);
    return success;
}

/* Compiler-known formatting schemas and typed helper functions. */
typedef struct RFormatBuild {
    RFormatPart *parts;
    size_t part_count;
    size_t part_capacity;
    RAstRef *paths;
    size_t path_count;
    size_t path_capacity;
    RHirVector captures;
    /* The hidden locals of rendered values (L32). */
    RHirVector *prefix;
} RFormatBuild;

static bool r_format_named_type(const RFrontendContext *context, RTypeId type, const char *name) {
    return r_semantic_standard_type_name_equal(
        context, r_semantic_type(context, r_semantic_value_type(context, type)), name);
}

static bool r_format_diagnostic(RBodyContext *body, RSourceSpan span, const char *message) {
    return r_body_diagnostic(body, "R-DIAG-FORMAT-001", "R-EXPR-0028", message, span);
}

static bool r_format_decimal(const uint8_t *bytes, size_t length, size_t *offset, uint64_t *value) {
    size_t start = *offset;
    *value = 0U;
    while (*offset < length && bytes[*offset] >= (uint8_t)'0' && bytes[*offset] <= (uint8_t)'9') {
        uint64_t digit = (uint64_t)(bytes[*offset] - (uint8_t)'0');
        if (*value > (UINT64_MAX - digit) / UINT64_C(10)) {
            return false;
        }
        *value = *value * UINT64_C(10) + digit;
        ++*offset;
    }
    return *offset != start;
}

static bool r_format_parse_spec(RBodyContext *body, RAstRef ref, RFormatSpec *spec) {
    RAstTextView text;
    RAstNodeView view;
    size_t offset = 0U;
    spec->specified = true;
    if (!r_ast_ref_text(body->frontend, ref, &text) ||
        !r_ast_ref_view(body->frontend, ref, &view)) {
        return false;
    }
    spec->radix = 10U;
    if (text.length != 0U && text.bytes[0] >= (uint8_t)'0' && text.bytes[0] <= (uint8_t)'9') {
        spec->zero_fill = text.bytes[0] == (uint8_t)'0';
        if (!r_format_decimal(text.bytes, text.length, &offset, &spec->width) ||
            spec->width == 0U) {
            return r_format_diagnostic(
                body, view.span, "format width must be a positive usize constant");
        }
    }
    if (offset < text.length && text.bytes[offset] == (uint8_t)'.') {
        ++offset;
        spec->fixed = true;
        if (!r_format_decimal(text.bytes, text.length, &offset, &spec->precision)) {
            return r_format_diagnostic(
                body, view.span, "format precision must be a nonnegative usize constant");
        }
    } else if (offset < text.length) {
        spec->integer_presentation = true;
        switch (text.bytes[offset++]) {
        case 'd':
            spec->radix = 10U;
            break;
        case 'x':
            spec->radix = 16U;
            break;
        case 'X':
            spec->radix = 16U;
            spec->uppercase = true;
            break;
        case 'b':
            spec->radix = 2U;
            break;
        case 'o':
            spec->radix = 8U;
            break;
        default:
            return r_format_diagnostic(body, view.span, "unknown format specification");
        }
    }
    if (offset != text.length || text.length == 0U) {
        return r_format_diagnostic(body, view.span, "invalid format specification");
    }
    return true;
}

/* R-EXPR-0028: the values the helpers format by themselves. */
static bool r_format_builtin_type(const RFrontendContext *context, RTypeId type) {
    const RSemanticType *semantic = r_semantic_type(context, r_semantic_value_type(context, type));
    const RSemanticTypeKind kind = semantic == NULL ? R_SEMANTIC_TYPE_INVALID : semantic->kind;

    return ((kind >= R_SEMANTIC_TYPE_I8) && (kind <= R_SEMANTIC_TYPE_USIZE)) ||
           (kind == R_SEMANTIC_TYPE_F32) || (kind == R_SEMANTIC_TYPE_F64) ||
           (kind == R_SEMANTIC_TYPE_C_ABI_NUMERIC) || (kind == R_SEMANTIC_TYPE_BOOL) ||
           (kind == R_SEMANTIC_TYPE_CHAR) || (kind == R_SEMANTIC_TYPE_STR) ||
           (kind == R_SEMANTIC_TYPE_CONSTEXPR_STR) ||
           r_format_named_type(context, type, "std.string::string");
}

/* R-TYPE-0046 (L32): any other value whose type satisfies core::Format is rendered to text by
   its implementation or its standard formatting before the helper sees it. */
static bool r_format_trait_type(RBodyContext *body, RTypeId type) {
    if (r_format_builtin_type(body->frontend, type) ||
        !r_semantic_declare_core_traits(body->frontend)) {
        return false;
    }
    return r_semantic_type_implements_trait(
        body->frontend, r_semantic_value_type(body->frontend, type), body->frontend->core_format_trait);
}

static bool
r_format_check_type(RBodyContext *body, RTypeId type, RSourceSpan span, const RFormatSpec *spec) {
    const RSemanticType *semantic =
        r_semantic_type(body->frontend, r_semantic_value_type(body->frontend, type));
    RSemanticTypeKind kind = semantic == NULL ? R_SEMANTIC_TYPE_INVALID : semantic->kind;
    bool integer;
    bool floating;
    /* A width alone pads any value; the rest of a specification is numeric (R-EXPR-0028). */
    const bool numeric_spec = spec->fixed || spec->integer_presentation || spec->zero_fill;
    if (kind == R_SEMANTIC_TYPE_BORROW) {
        return r_format_check_type(body, semantic->base, span, spec);
    }
    if (r_format_trait_type(body, type)) {
        return !numeric_spec ||
               r_format_diagnostic(
                   body, span, "format specification is incompatible with the value type");
    }
    integer = kind >= R_SEMANTIC_TYPE_I8 && kind <= R_SEMANTIC_TYPE_USIZE;
    floating = kind == R_SEMANTIC_TYPE_F32 || kind == R_SEMANTIC_TYPE_F64;
    if (kind == R_SEMANTIC_TYPE_C_ABI_NUMERIC) {
        RTokenKind token = (RTokenKind)semantic->length;
        floating = token == R_TOKEN_KW_C_FLOAT || token == R_TOKEN_KW_C_DOUBLE ||
                   token == R_TOKEN_KW_C_LONG_DOUBLE;
        integer = !floating && token != R_TOKEN_KW_C_BOOL;
        if (token == R_TOKEN_KW_C_BOOL) {
            kind = R_SEMANTIC_TYPE_BOOL;
        }
    }
    if (!integer && !floating && kind != R_SEMANTIC_TYPE_BOOL && kind != R_SEMANTIC_TYPE_CHAR &&
        kind != R_SEMANTIC_TYPE_STR && kind != R_SEMANTIC_TYPE_CONSTEXPR_STR &&
        !r_format_named_type(body->frontend, type, "std.string::string")) {
        return r_format_diagnostic(body, span, "type has no formatting contract");
    }
    if (numeric_spec && ((!integer && !floating) || (integer && spec->fixed) ||
                         (floating && spec->integer_presentation))) {
        return r_format_diagnostic(
            body, span, "format specification is incompatible with the value type");
    }
    return true;
}

static bool r_format_path_equal(const RFrontendContext *context, RAstRef left, RAstRef right) {
    RAstNodeView a;
    RAstNodeView b;
    RAstTokenView at;
    RAstTokenView bt;
    uint32_t index;
    if (!r_ast_ref_view(context, left, &a) || !r_ast_ref_view(context, right, &b) ||
        a.kind != b.kind || a.child_count != b.child_count) {
        return false;
    }
    if (r_ast_ref_token(context, left, &at)) {
        const RSource *as = r_get_source_const(context, a.span.source);
        const RSource *bs = r_get_source_const(context, b.span.source);
        return r_ast_ref_token(context, right, &bt) && at.kind == bt.kind &&
               r_source_span_equal(as, at.span, bs, bt.span);
    }
    for (index = 0U; index < a.child_count; ++index) {
        RAstRef ac;
        RAstRef bc;
        if (!r_ast_ref_child(context, left, index, &ac) ||
            !r_ast_ref_child(context, right, index, &bc) || !r_format_path_equal(context, ac, bc)) {
            return false;
        }
    }
    return true;
}

static bool r_format_borrow(RBodyContext *body, RExpressionResult *value) {
    RTypeId borrowed;
    RExpressionResult result = {0};
    if (!r_semantic_intern_derived_type(body->frontend,
                                        R_SEMANTIC_TYPE_BORROW,
                                        r_semantic_value_type(body->frontend, value->type),
                                        R_TYPE_ID_INVALID,
                                        0U,
                                        R_SEMANTIC_TYPE_FLAG_SHARED,
                                        &borrowed) ||
        !r_body_form_safe_borrow(
            body, r_body_hir_node(body, value->node)->span, borrowed, value, &result)) {
        return false;
    }
    *value = result;
    return true;
}

static bool r_format_has_move(const RFrontendContext *context, RAstRef ref) {
    RAstNodeView view;
    RAstTokenView token;
    uint32_t index;
    if (!r_ast_ref_view(context, ref, &view)) {
        return false;
    }
    if (r_ast_ref_token(context, ref, &token)) {
        return token.kind == R_TOKEN_KW_MOVE;
    }
    for (index = 0U; index < view.child_count; ++index) {
        RAstRef child;
        if (r_ast_ref_child(context, ref, index, &child) && r_format_has_move(context, child)) {
            return true;
        }
    }
    return false;
}

static bool r_format_materialize(RBodyContext *body, RExpressionResult *value, RHirVector *prefix) {
    RSemanticSymbol symbol = {0};
    RSymbolId symbol_id;
    RHirNodeId local;
    RHirNodeId child = value->node;
    RHirVector children = {&child, 1U, 1U};
    RSourceSpan span = r_body_hir_node(body, value->node)->span;
    if (prefix == NULL) {
        return r_format_diagnostic(body, span, "capture must designate a local or parameter place");
    }
    symbol.kind = R_SEMANTIC_SYMBOL_LOCAL;
    symbol.name_span = span;
    symbol.scope_start = span.end;
    symbol.module_source = body->source;
    symbol.type = value->type;
    symbol.parent = body->function_symbol;
    symbol.borrow_origin = value->borrow_origin;
    symbol.borrow_origin_set = value->borrow_origin_set;
    symbol.borrow_origin_multiple = value->borrow_origin_multiple;
    symbol.borrow_referent = value->borrow_referent;
    symbol.projected_borrow_state = value->projected_borrow_state;
    symbol.object_state = R_SEMANTIC_OBJECT_STATE_INITIALIZED;
    symbol.signature_supported = true;
    symbol.is_format_temporary = true;
    symbol.is_hidden_local = true;
    if (body->format_temporary_count == UINT32_MAX) {
        body->frontend->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
        return false;
    }
    symbol.integer_value = body->format_temporary_count++;
    if (!r_intern_bytes(
            body->frontend, (const uint8_t *)"$format_local", 13U, &symbol.name_intern_id) ||
        !r_semantic_append_symbol(body->frontend, symbol, &symbol_id) ||
        !r_body_append_node(body,
                            R_HIR_LOCAL,
                            span,
                            value->type,
                            symbol_id,
                            R_TOKEN_INVALID,
                            0U,
                            false,
                            &children,
                            &local) ||
        !r_hir_vector_push(body->frontend, prefix, local) ||
        !r_symbol_vector_push(body->frontend, &body->active_symbols, symbol_id)) {
        return false;
    }
    body->frontend->semantic_symbols[symbol_id - 1U].hir_node = local;
    if (!r_body_append_node(body,
                            R_HIR_PLACE,
                            span,
                            value->type,
                            symbol_id,
                            R_TOKEN_INVALID,
                            0U,
                            true,
                            NULL,
                            &value->node)) {
        return false;
    }
    value->symbol = symbol_id;
    value->borrow_origin = R_SYMBOL_ID_INVALID;
    value->borrow_origin_set = UINT32_C(0);
    value->borrow_origin_multiple = false;
    value->is_place = true;
    return true;
}

/* Keep earlier operands owned until all later, potentially throwing operands succeed. */
static bool r_format_syntax(const RFrontendContext *context, RAstRef ref) {
    RAstNodeView view;
    if (!r_ast_ref_view(context, ref, &view)) {
        return false;
    }
    if (view.kind == R_SYNTAX_FORMAT_SEQUENCE || view.kind == R_SYNTAX_FORMAT_SUFFIX ||
        view.kind == R_SYNTAX_CALL_SUFFIX || view.kind == R_SYNTAX_METHOD_CALL_SUFFIX ||
        view.kind == R_SYNTAX_STANDARD_TYPE_CALL || view.kind == R_SYNTAX_AWAIT_OPERATION) {
        return true;
    }
    for (uint32_t index = 0U; index < view.child_count; ++index) {
        RAstRef child;
        if (r_ast_ref_child(context, ref, index, &child) && r_format_syntax(context, child)) {
            return true;
        }
    }
    return false;
}

static bool
r_format_stage_operand(RBodyContext *body, RExpressionResult *value, RHirVector *prefix) {
    RSymbolId origin = value->borrow_origin;
    uint32_t origin_set = value->borrow_origin_set;
    bool origin_multiple = value->borrow_origin_multiple;
    if (!r_body_place_to_value(body, value) || !r_format_materialize(body, value, prefix)) {
        return false;
    }
    RHirNodeId place = value->node;
    RHirVector children = {&place, 1U, 1U};
    bool copy = r_semantic_type_is_copy(body->frontend, value->type);
    if (!r_body_append_node(body,
                            copy ? R_HIR_LOAD : R_HIR_MOVE,
                            r_body_hir_node(body, place)->span,
                            value->type,
                            value->symbol,
                            copy ? R_TOKEN_INVALID : R_TOKEN_KW_MOVE,
                            0U,
                            false,
                            &children,
                            &value->node)) {
        return false;
    }
    value->is_place = false;
    /* The staged local has a settled type; its load is no longer a literal AST. */
    value->is_integer_literal_form = false;
    value->is_unsuffixed_integer_literal = false;
    value->borrow_origin = origin;
    value->borrow_origin_set = origin_set;
    value->borrow_origin_multiple = origin_multiple;
    r_body_hir_node(body, value->node)->borrow_origin = origin;
    r_body_hir_node(body, value->node)->borrow_referent = value->borrow_referent;
    r_body_hir_node(body, value->node)->projected_borrow_state = value->projected_borrow_state;
    r_body_hir_node(body, value->node)->borrow_origin_set = origin_set;
    r_body_hir_node(body, value->node)->borrow_origin_multiple = value->borrow_origin_multiple;
    return true;
}

static void r_format_commit_operands(RBodyContext *body, size_t scope_base) {
    for (size_t index = scope_base; index < body->active_symbols.count; ++index) {
        RSemanticSymbol *symbol =
            &body->frontend->semantic_symbols[body->active_symbols.items[index] - 1U];
        if (!r_semantic_type_is_copy(body->frontend, symbol->type)) {
            symbol->object_state = R_SEMANTIC_OBJECT_STATE_MOVED;
        }
    }
}

static bool
r_format_wrap_operands(RBodyContext *body, RHirVector *prefix, RExpressionResult *value) {
    if (prefix->count == 0U || !value->valid) {
        return true;
    }
    uint64_t result_index = (uint64_t)prefix->count;
    RHirNodeId scope;
    RSourceSpan span = r_body_hir_node(body, value->node)->span;
    if (!r_hir_vector_push(body->frontend, prefix, value->node) ||
        !r_body_append_node(body,
                            R_HIR_VALUE_SCOPE,
                            span,
                            value->type,
                            R_SYMBOL_ID_INVALID,
                            R_TOKEN_INVALID,
                            result_index,
                            false,
                            prefix,
                            &scope)) {
        return false;
    }
    r_body_hir_node(body, scope)->borrow_origin = value->borrow_origin;
    r_body_hir_node(body, scope)->borrow_origin_set = value->borrow_origin_set;
    r_body_hir_node(body, scope)->borrow_origin_multiple = value->borrow_origin_multiple;
    value->node = scope;
    return true;
}

/* R-TYPE-0046 (L32): the standard call RENDER (a new string) or APPEND (into a builder) over
 * `children`, whose first operand borrows the formatted value of `value_type`. */
static bool r_format_standard_call(RBodyContext *body,
                                   RStandardCallOperation operation,
                                   RSourceSpan span,
                                   RTypeId value_type,
                                   RHirVector *children,
                                   RTypeId result_type,
                                   RExpressionResult *result) {
    RFrontendContext *context = body->frontend;
    RTypeId alloc_error;
    RTypeId effects;
    RTypeId carrier;
    RHirNodeId node_id;
    RHirNode *node;

    if (!r_format_bind_type(context, value_type, 0U) ||
        !r_semantic_intern_named_standard_type(context, "std.alloc::alloc_error", &alloc_error) ||
        !r_semantic_single_effect_set(context, alloc_error, &effects) ||
        !r_semantic_intern_effect_carrier(context, result_type, effects, &carrier) ||
        !r_body_validate_effect_set(body, effects, span) ||
        !r_body_append_node(body,
                            R_HIR_STANDARD_CALL,
                            span,
                            result_type,
                            R_SYMBOL_ID_INVALID,
                            R_TOKEN_INVALID,
                            0U,
                            false,
                            children,
                            &node_id)) {
        return false;
    }
    node = r_body_hir_node(body, node_id);
    node->standard_operation = operation;
    node->auxiliary_type = carrier;
    node->runtime_type = r_semantic_value_type(context, value_type);
    node->borrow_origin = R_SYMBOL_ID_INVALID;
    if (!r_body_attach_effect_exits(body, node_id, effects, span)) {
        return false;
    }
    r_body_expression_invalid(result);
    result->node = node_id;
    result->type = result_type;
    result->valid = true;
    return true;
}

/* A borrow of `value` for the render call: a place is borrowed, a borrow is used as it is. */
static bool r_format_render(RBodyContext *body, RExpressionResult *value, RSourceSpan span) {
    const RSemanticType *type =
        r_semantic_type(body->frontend, r_semantic_value_type(body->frontend, value->type));
    RTypeId formatted = value->type;
    RTypeId string_type;
    RHirNodeId operand;
    RHirVector children = {&operand, 1U, 1U};

    if ((type != NULL) && (type->kind == R_SEMANTIC_TYPE_BORROW)) {
        formatted = type->base;
        if (!r_body_place_to_value(body, value)) {
            return false;
        }
    } else if (!r_format_borrow(body, value)) {
        return false;
    }
    operand = value->node;
    return r_semantic_intern_named_standard_type(body->frontend, "std.string::string", &string_type) &&
           r_format_standard_call(body,
                                  R_STANDARD_CALL_CORE_FORMAT_RENDER,
                                  span,
                                  formatted,
                                  &children,
                                  string_type,
                                  value);
}

/* R-TYPE-0046 (L32): `receiver->format(out)` appends the text of a core::Format value to a
 * builder; types with standard formatting and parameters bound by core::Format reach it through
 * one standard call, which each instantiation formats by its own type. */
static bool r_format_lower_append(RBodyContext *body,
                                  RAstRef argument_list,
                                  const RExpressionResult *receiver,
                                  RTypeId owner_type,
                                  RSourceSpan span,
                                  RExpressionResult *result) {
    RFrontendContext *context = body->frontend;
    RAstNodeView arguments_view;
    RAstRef argument_ref;
    RExpressionResult subject = *receiver;
    RExpressionResult output;
    RTypeId builder_type;
    RTypeId output_type;
    RTypeId void_type;
    RHirNodeId operands[2];
    RHirVector children = {operands, 2U, 2U};

    r_body_expression_invalid(result);
    if (!r_ast_ref_view(context, argument_list, &arguments_view)) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    if (arguments_view.child_count != UINT32_C(1)) {
        return r_body_diagnostic(body,
                                 "R-DIAG-TYPE-001",
                                 "R-TYPE-0046",
                                 "core::Format::format takes one std.format::builder* argument",
                                 span);
    }
    if (!r_semantic_intern_named_standard_type(context, "std.format::builder", &builder_type) ||
        !r_semantic_intern_derived_type(context,
                                        R_SEMANTIC_TYPE_BORROW,
                                        builder_type,
                                        R_TYPE_ID_INVALID,
                                        UINT64_C(0),
                                        R_SEMANTIC_TYPE_FLAG_NONE,
                                        &output_type) ||
        !r_semantic_type_from_token(context, R_TOKEN_KW_VOID, &void_type) ||
        !r_ast_ref_child(context, argument_list, UINT32_C(0), &argument_ref)) {
        return false;
    }
    r_body_expression_invalid(&output);
    if (!r_body_place_to_value(body, &subject) ||
        !r_body_lower_value(body, argument_ref, output_type, &output)) {
        return false;
    }
    if (!subject.valid || !output.valid) {
        return true;
    }
    if (output.type != output_type) {
        return r_body_diagnostic(body,
                                 "R-DIAG-TYPE-001",
                                 "R-TYPE-0046",
                                 "core::Format::format takes one std.format::builder* argument",
                                 span);
    }
    operands[0] = subject.node;
    operands[1] = output.node;
    return r_format_standard_call(
        body, R_STANDARD_CALL_CORE_FORMAT_APPEND, span, owner_type, &children, void_type, result);
}

static bool r_format_lower_input(RBodyContext *body,
                                 RAstRef ref,
                                 RHirVector *prefix,
                                 bool named,
                                 RExpressionResult *value) {
    RAstNodeView view;
    const RFormatSpec plain = {.radix = 10U};
    const RSemanticType *type;
    if (!r_ast_ref_view(body->frontend, ref, &view)) {
        return false;
    }
    if (r_format_has_move(body->frontend, ref)) {
        return r_format_diagnostic(
            body, view.span, "formatting borrows values; move is not a formatting argument");
    }
    if (!r_body_lower_expression(body, ref, R_TYPE_ID_INVALID, value) || !value->valid) {
        return body->frontend->resource_status == R_FRONTEND_OK;
    }
    if (named) {
        const RSemanticSymbol *root = r_body_symbol(body, value->symbol);
        if (root == NULL ||
            (root->kind != R_SEMANTIC_SYMBOL_LOCAL && root->kind != R_SEMANTIC_SYMBOL_PARAMETER)) {
            return r_format_diagnostic(
                body, view.span, "named capture must start at a local or parameter");
        }
    }
    const size_t diagnostics = body->frontend->diagnostic_count;
    if (!r_format_check_type(body, value->type, view.span, &plain)) {
        return false;
    }
    /* A value without a formatting contract is reported once and not rendered. */
    const bool contracted = body->frontend->diagnostic_count == diagnostics;
    type = r_semantic_type(body->frontend, r_semantic_value_type(body->frontend, value->type));
    if (contracted &&
        r_format_trait_type(body,
                            (type != NULL) && (type->kind == R_SEMANTIC_TYPE_BORROW) ? type->base
                                                                                   : value->type)) {
        /* R-TYPE-0046 (L32): the value is rendered first; its text is the capture. */
        return r_format_render(body, value, view.span) &&
               r_format_materialize(body, value, prefix) && r_format_borrow(body, value);
    }
    if (contracted && (type != NULL) && (type->kind == R_SEMANTIC_TYPE_BORROW) &&
        !r_format_named_type(body->frontend, type->base, "std.string::string")) {
        /* A borrow of a scalar formats the value it designates. */
        RExpressionResult place;
        if (!r_body_place_to_value(body, value) ||
            !r_body_form_deref_place(body, *value, view.span, &place)) {
            return false;
        }
        *value = place;
    }
    if (r_format_named_type(body->frontend, value->type, "std.string::string")) {
        if (!value->is_place && !r_format_materialize(body, value, prefix)) {
            return false;
        }
        return r_format_borrow(body, value);
    }
    if (!r_body_place_to_value(body, value)) {
        return false;
    }
    if (value->is_integer_literal_form) {
        return r_body_finalize_integer_literal(
            body, value, R_TYPE_ID_INVALID, r_body_literal_is_negative(body, value));
    }
    return true;
}

static bool r_format_collect(RBodyContext *body, RAstRef ref, RFormatBuild *build) {
    RFrontendContext *context = body->frontend;
    RAstNodeView view;
    RAstTokenView token;
    RFormatPart part = {0};
    uint32_t index;
    if (!r_ast_ref_view(context, ref, &view)) {
        return false;
    }
    part.span = view.span;
    part.spec.radix = 10U;
    if (r_ast_ref_token(context, ref, &token)) {
        part.kind = R_FORMAT_TEXT;
        if (token.kind == R_TOKEN_FORMAT_TEXT) {
            part.index = token.intern_id;
        } else if (token.kind == R_TOKEN_STRING_LITERAL) {
            size_t length;
            if (!r_intern_string_sequence(context, token.span, &part.index, &length)) {
                return false;
            }
        } else {
            return true;
        }
    } else if (view.kind == R_SYNTAX_FORMAT_SLOT) {
        RAstRef value_ref;
        RAstTokenView value_token;
        if (!r_ast_ref_child(context, ref, 0U, &value_ref)) {
            return false;
        }
        if (r_ast_ref_token(context, value_ref, &value_token) &&
            value_token.kind == R_TOKEN_INTEGER_LITERAL) {
            RAstTextView text;
            size_t offset = 0U;
            uint64_t position;
            if (!r_ast_ref_text(context, value_ref, &text)) {
                return false;
            }
            if (!r_format_decimal(text.bytes, text.length, &offset, &position) ||
                offset != text.length || position == 0U || position > UINT32_MAX) {
                return r_format_diagnostic(
                    body, value_token.span, "format arguments are numbered from 1");
            }
            part.kind = R_FORMAT_POSITIONAL;
            part.index = (uint32_t)position - 1U;
        } else {
            RExpressionResult value = {0};
            size_t capture;
            part.kind = R_FORMAT_CAPTURE;
            for (capture = 0U; capture < build->path_count; ++capture) {
                if (r_format_path_equal(context, value_ref, build->paths[capture])) {
                    break;
                }
            }
            if (capture == build->path_count) {
                if (!r_format_lower_input(body, value_ref, build->prefix, true, &value)) {
                    return false;
                }
                if (!value.valid || body->poisoned) {
                    return true;
                }
                if (!r_grow_array(context,
                                  (void **)&build->paths,
                                  &build->path_capacity,
                                  sizeof(*build->paths),
                                  build->path_count + 1U) ||
                    !r_hir_vector_push(context, &build->captures, value.node)) {
                    return false;
                }
                build->paths[build->path_count++] = value_ref;
            }
            part.index = (uint32_t)capture;
        }
        for (index = 1U; index < view.child_count; ++index) {
            RAstRef child;
            if (!r_ast_ref_child(context, ref, index, &child) ||
                !r_format_parse_spec(body, child, &part.spec)) {
                return false;
            }
        }
        if (part.kind == R_FORMAT_CAPTURE) {
            if (build->captures.items == NULL || part.index >= build->captures.count) {
                context->resource_status = R_FRONTEND_INTERNAL_ERROR;
                return false;
            }
            if (!r_format_check_type(body,
                                     r_body_hir_node(body, build->captures.items[part.index])->type,
                                     part.span,
                                     &part.spec)) {
                return false;
            }
        }
    } else {
        for (index = 0U; index < view.child_count; ++index) {
            RAstRef child;
            if (!r_ast_ref_child(context, ref, index, &child) ||
                !r_format_collect(body, child, build)) {
                return false;
            }
            if (body->poisoned) {
                return true;
            }
        }
        return true;
    }
    if (part.kind == R_FORMAT_TEXT && build->part_count != 0U &&
        build->parts[build->part_count - 1U].kind == R_FORMAT_TEXT) {
        RFormatPart *previous = &build->parts[build->part_count - 1U];
        RSemanticByteVector bytes = {0};
        const RInternEntry left = context->intern_entries[previous->index - 1U];
        const RInternEntry right = context->intern_entries[part.index - 1U];
        bool merged = r_semantic_byte_vector_append(
                          context, &bytes, (const uint8_t *)left.bytes, left.length) &&
                      r_semantic_byte_vector_append(
                          context, &bytes, (const uint8_t *)right.bytes, right.length) &&
                      r_intern_bytes(context, bytes.items, bytes.count, &previous->index);
        previous->span.end = part.span.end;
        r_context_free(context, bytes.items);
        return merged;
    }
    if (!r_grow_array(context,
                      (void **)&build->parts,
                      &build->part_capacity,
                      sizeof(*build->parts),
                      build->part_count + 1U)) {
        return false;
    }
    build->parts[build->part_count++] = part;
    return true;
}

static bool r_format_create_schema(RBodyContext *body,
                                   RSourceSpan span,
                                   RFormatBuild *build,
                                   uint32_t *schema_id) {
    RFrontendContext *context = body->frontend;
    RFormatSchema schema = {0};
    RSemanticAggregate aggregate = {0};
    char name[96];
    size_t index;
    uint32_t aggregate_id;
    if (context->format_schema_count >= UINT32_MAX ||
        context->semantic_aggregate_count >= UINT32_MAX ||
        context->format_part_count > UINT32_MAX - build->part_count ||
        context->semantic_field_count > UINT32_MAX - build->captures.count) {
        context->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
        return false;
    }
    *schema_id = (uint32_t)context->format_schema_count + 1U;
    (void)snprintf(name, sizeof(name), "$format_%u", span.start);
    if (!r_intern_bytes(context, (const uint8_t *)name, strlen(name), &aggregate.name_intern_id) ||
        !r_grow_array(context,
                      (void **)&context->semantic_aggregates,
                      &context->semantic_aggregate_capacity,
                      sizeof(*context->semantic_aggregates),
                      context->semantic_aggregate_count + 1U) ||
        !r_grow_array(context,
                      (void **)&context->semantic_fields,
                      &context->semantic_field_capacity,
                      sizeof(*context->semantic_fields),
                      context->semantic_field_count + build->captures.count) ||
        !r_grow_array(context,
                      (void **)&context->format_schemas,
                      &context->format_schema_capacity,
                      sizeof(*context->format_schemas),
                      context->format_schema_count + 1U) ||
        !r_grow_array(context,
                      (void **)&context->format_parts,
                      &context->format_part_capacity,
                      sizeof(*context->format_parts),
                      context->format_part_count + build->part_count)) {
        return false;
    }
    aggregate_id = (uint32_t)context->semantic_aggregate_count + 1U;
    if (!r_semantic_intern_type(context, R_SEMANTIC_TYPE_STRUCT, aggregate_id, &schema.type)) {
        return false;
    }
    schema.span = span;
    schema.capture_count = (uint32_t)build->captures.count;
    schema.first_part = (uint32_t)context->format_part_count;
    schema.part_count = (uint32_t)build->part_count;
    aggregate.kind = R_SEMANTIC_AGGREGATE_STRUCT;
    aggregate.name_span = span;
    aggregate.module_source = body->source;
    aggregate.type = schema.type;
    aggregate.first_field = (uint32_t)context->semantic_field_count;
    aggregate.field_count = schema.capture_count;
    aggregate.format_schema = *schema_id;
    aggregate.is_protected = true;
    aggregate.complete = true;
    for (index = 0U; index < build->captures.count; ++index) {
        RSemanticField field = {0};
        const RSemanticType *input =
            r_semantic_type(context, r_body_hir_node(body, build->captures.items[index])->type);
        field.type = r_body_hir_node(body, build->captures.items[index])->type;
        if (input->kind == R_SEMANTIC_TYPE_STR || input->kind == R_SEMANTIC_TYPE_BORROW) {
            if (!r_semantic_intern_named_standard_type(
                    context, "std.string::string", &field.type)) {
                return false;
            }
        }
        (void)snprintf(name, sizeof(name), "$capture_%zu", index);
        if (!r_intern_bytes(context, (const uint8_t *)name, strlen(name), &field.name_intern_id)) {
            return false;
        }
        field.name_span = r_body_hir_node(body, build->captures.items[index])->span;
        field.aggregate = aggregate_id;
        field.layout_index = (uint32_t)index;
        context->semantic_fields[context->semantic_field_count++] = field;
    }
    context->semantic_aggregates[context->semantic_aggregate_count++] = aggregate;
    context->format_schemas[context->format_schema_count++] = schema;
    if (build->part_count != 0U) {
        (void)memcpy(context->format_parts + context->format_part_count,
                     build->parts,
                     build->part_count * sizeof(*build->parts));
    }
    context->format_part_count += build->part_count;
    return true;
}

static bool r_format_call(RBodyContext *body,
                          RSourceSpan span,
                          RFormatRecipeKind kind,
                          uint32_t schema,
                          RTypeId result_type,
                          RHirVector *arguments,
                          bool checked,
                          RExpressionResult *result) {
    RFrontendContext *context = body->frontend;
    RSemanticSymbol function = {0};
    RSymbolId function_id;
    RHirNode function_node = {0};
    RHirNodeId function_node_id;
    RHirVector children = {0};
    RTypeId effects = R_TYPE_ID_INVALID;
    char name[96];
    size_t index;
    bool success = false;
    for (index = 0U; index < arguments->count; ++index) {
        if (r_body_hir_node(body, arguments->items[index]) == NULL) {
            context->resource_status = R_FRONTEND_INTERNAL_ERROR;
            return false;
        }
    }
    if (checked) {
        RTypeId error_type;
        if (!r_semantic_intern_named_standard_type(
                context, "std.alloc::alloc_error", &error_type) ||
            !r_semantic_single_effect_set(context, error_type, &effects) ||
            !r_semantic_intern_effect_carrier(
                context, result_type, effects, &function.effect_carrier_type) ||
            !r_body_validate_effect_set(body, effects, span)) {
            return false;
        }
    }
    if (context->format_recipe_count >= UINT32_MAX ||
        context->semantic_parameter_type_count > UINT32_MAX - arguments->count ||
        !r_grow_array(context,
                      (void **)&context->format_recipes,
                      &context->format_recipe_capacity,
                      sizeof(*context->format_recipes),
                      context->format_recipe_count + 1U) ||
        !r_grow_array(context,
                      (void **)&context->semantic_parameter_types,
                      &context->semantic_parameter_type_capacity,
                      sizeof(*context->semantic_parameter_types),
                      context->semantic_parameter_type_count + arguments->count)) {
        return false;
    }
    context->format_recipes[context->format_recipe_count++] = (RFormatRecipe){kind, schema};
    function.format_recipe = (uint32_t)context->format_recipe_count;
    (void)snprintf(name, sizeof(name), "$format_call_%u_%u", span.start, (unsigned)kind);
    if (!r_intern_bytes(context, (const uint8_t *)name, strlen(name), &function.name_intern_id)) {
        return false;
    }
    function.kind = R_SEMANTIC_SYMBOL_FUNCTION;
    function.name_span = span;
    function.module_source = body->source;
    function.return_type = result_type;
    function.throws_type = effects;
    function.is_protected = true;
    function.has_definition = true;
    function.signature_supported = true;
    function.first_parameter_type = (uint32_t)context->semantic_parameter_type_count;
    function.parameter_count = (uint32_t)arguments->count;
    for (index = 0U; index < arguments->count; ++index) {
        context->semantic_parameter_types[context->semantic_parameter_type_count++] =
            r_body_hir_node(body, arguments->items[index])->type;
    }
    if (!r_semantic_append_symbol(context, function, &function_id)) {
        return false;
    }
    for (index = 0U; index < arguments->count; ++index) {
        RSemanticSymbol parameter = {0};
        RSymbolId parameter_id;
        RHirNode parameter_node = {0};
        RHirNodeId node;
        parameter.kind = R_SEMANTIC_SYMBOL_PARAMETER;
        parameter.integer_value = (uint64_t)index;
        parameter.name_span = span;
        parameter.module_source = body->source;
        parameter.parent = function_id;
        parameter.type = context->semantic_parameter_types[function.first_parameter_type + index];
        parameter.signature_supported = true;
        parameter.object_state = R_SEMANTIC_OBJECT_STATE_INITIALIZED;
        (void)snprintf(name, sizeof(name), "$argument_%zu", index);
        if (!r_intern_bytes(
                context, (const uint8_t *)name, strlen(name), &parameter.name_intern_id) ||
            !r_semantic_append_symbol(context, parameter, &parameter_id)) {
            goto cleanup;
        }
        parameter_node.kind = R_HIR_PARAMETER;
        parameter_node.span = span;
        parameter_node.type = parameter.type;
        parameter_node.symbol = parameter_id;
        if (!r_hir_append_node(context, parameter_node, &node) ||
            !r_hir_vector_push(context, &children, node)) {
            goto cleanup;
        }
    }
    {
        RHirNode block = {0};
        RHirNodeId node;
        block.kind = R_HIR_BLOCK;
        block.span = span;
        if (!r_semantic_type_from_token(context, R_TOKEN_KW_VOID, &block.type)) {
            goto cleanup;
        }
        if (!r_hir_append_node(context, block, &node) ||
            !r_hir_vector_push(context, &children, node)) {
            goto cleanup;
        }
    }
    function_node.kind = R_HIR_FUNCTION;
    function_node.span = span;
    function_node.symbol = function_id;
    function_node.type = result_type;
    if (!r_hir_append_node(context, function_node, &function_node_id) ||
        !r_hir_commit_children(context, function_node_id, &children)) {
        goto cleanup;
    }
    context->semantic_symbols[function_id - 1U].hir_node = function_node_id;
    if (!r_body_append_node(body,
                            R_HIR_CALL,
                            span,
                            result_type,
                            function_id,
                            R_TOKEN_INVALID,
                            0U,
                            false,
                            arguments,
                            &result->node)) {
        goto cleanup;
    }
    r_body_hir_node(body, result->node)->auxiliary_type = function.effect_carrier_type;
    r_body_hir_node(body, result->node)->borrow_origin = R_SYMBOL_ID_INVALID;
    if (checked && !r_body_attach_effect_exits(body, result->node, effects, span)) {
        goto cleanup;
    }
    result->type = result_type;
    result->symbol = function_id;
    result->borrow_origin = R_SYMBOL_ID_INVALID;
    result->is_place = false;
    result->valid = true;
    success = true;
cleanup:
    r_context_free(context, children.items);
    return success;
}

/* R-TYPE-0046 (L32): `place.format(out)` as the last suffix of a chain calls core::Format with
 * the place as a shared receiver when its type satisfies core::Format and is not a format
 * template. */
static bool r_format_lower_member(RBodyContext *body,
                                  RExpressionResult *receiver,
                                  RAstRef suffix,
                                  RSourceSpan span,
                                  RExpressionResult *result) {
    RFrontendContext *context = body->frontend;
    RAstRef argument_list;
    const RTypeId owner = r_semantic_value_type(context, receiver->type);
    const RSemanticAggregate *aggregate = r_semantic_aggregate_for_type(context, owner);

    r_body_expression_invalid(result);
    if (!r_ast_find_direct_child(context, suffix, R_SYNTAX_ARGUMENT_LIST, &argument_list)) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    if (!receiver->is_place || ((aggregate != NULL) && (aggregate->format_schema != 0U)) ||
        !r_semantic_declare_core_traits(context) ||
        !r_semantic_type_implements_trait(context, owner, context->core_format_trait)) {
        return context->resource_status == R_FRONTEND_OK &&
               r_body_diagnostic(body,
                                 "R-DIAG-FORMAT-001",
                                 "R-EXPR-0028",
                                 ".format requires a formatted literal, a local "
                                 "std.format::format or a place whose type satisfies core::Format",
                                 span);
    }
    return r_format_borrow(body, receiver) &&
           r_format_lower_append(body, argument_list, receiver, owner, span, result);
}

static bool r_format_lower(RBodyContext *body,
                           RAstRef receiver,
                           const RAstRef *argument_list,
                           RTypeId contextual_type,
                           RExpressionResult *result) {
    RFrontendContext *context = body->frontend;
    RAstNodeView view;
    RAstRef sequence;
    RFormatBuild build = {0};
    RHirVector arguments = {0};
    RHirVector prefix = {0};
    const size_t scope_base = body->active_symbols.count;
    uint32_t schema_id = 0U;
    RFormatRecipeKind kind;
    RTypeId result_type;
    bool stored = r_format_named_type(context, contextual_type, "std.format::format");
    bool success = false;
    bool literal;
    size_t first_explicit;
    size_t index;
    if (!r_ast_ref_view(context, receiver, &view)) {
        return false;
    }
    literal = view.kind == R_SYNTAX_FORMAT_SEQUENCE;
    sequence = receiver;
    if (!literal && view.kind == R_SYNTAX_PRIMARY_EXPRESSION) {
        literal = r_ast_find_direct_child(context, receiver, R_SYNTAX_FORMAT_SEQUENCE, &sequence);
    }
    if (literal) {
        build.prefix = &prefix;
        if (!r_format_collect(body, sequence, &build) || body->poisoned ||
            !r_format_create_schema(body, view.span, &build, &schema_id)) {
            success = context->resource_status == R_FRONTEND_OK && body->poisoned;
            goto cleanup;
        }
        for (index = 0U; index < build.captures.count; ++index) {
            if (!r_hir_vector_push(context, &arguments, build.captures.items[index])) {
                goto cleanup;
            }
        }
        kind = stored && argument_list == NULL ? R_FORMAT_CREATE : R_FORMAT_IMMEDIATE;
    } else {
        RExpressionResult value = {0};
        const RSemanticAggregate *aggregate;
        if (!r_body_lower_expression(body, receiver, R_TYPE_ID_INVALID, &value)) {
            goto cleanup;
        }
        if (!value.valid) {
            /* The receiver was reported where it failed. */
            success = context->resource_status == R_FRONTEND_OK;
            goto cleanup;
        }
        aggregate =
            value.valid
                ? r_semantic_aggregate_for_type(context, r_semantic_value_type(context, value.type))
                : NULL;
        if (value.valid && value.is_place && (argument_list != NULL) &&
            ((aggregate == NULL) || (aggregate->format_schema == 0U)) &&
            r_semantic_declare_core_traits(context) &&
            r_semantic_type_implements_trait(
                context, r_semantic_value_type(context, value.type), context->core_format_trait)) {
            /* R-TYPE-0046 (L32): `value.format(out)` on any other place is the core::Format
               method with a shared receiver. */
            const RTypeId owner = r_semantic_value_type(context, value.type);
            success = r_format_borrow(body, &value) &&
                      r_format_lower_append(body, *argument_list, &value, owner, view.span, result);
            goto cleanup;
        }
        if (aggregate == NULL || aggregate->format_schema == 0U || !value.is_place) {
            success = r_format_diagnostic(
                body,
                view.span,
                ".format requires a formatted literal, a local std.format::format or a place "
                "whose type satisfies core::Format");
            goto cleanup;
        }
        schema_id = aggregate->format_schema;
        if (!r_format_borrow(body, &value)) {
            goto cleanup;
        }
        if (!value.valid || body->poisoned) {
            success = context->resource_status == R_FRONTEND_OK;
            goto cleanup;
        }
        if (!r_hir_vector_push(context, &arguments, value.node)) {
            goto cleanup;
        }
        kind = R_FORMAT_RENDER;
    }
    if (kind == R_FORMAT_IMMEDIATE) {
        RExpressionResult captured = {0};
        RTypeId template_type = context->format_schemas[schema_id - 1U].type;
        if (!r_format_call(body,
                           view.span,
                           R_FORMAT_CREATE,
                           schema_id,
                           template_type,
                           &arguments,
                           true,
                           &captured) ||
            !r_format_materialize(body, &captured, &prefix) || !r_format_borrow(body, &captured)) {
            goto cleanup;
        }
        arguments.count = 0U;
        if (!r_hir_vector_push(context, &arguments, captured.node)) {
            goto cleanup;
        }
        kind = R_FORMAT_RENDER;
    }
    first_explicit = arguments.count;
    if (argument_list != NULL) {
        RAstNodeView argument_view;
        if (!r_ast_ref_view(context, *argument_list, &argument_view)) {
            goto cleanup;
        }
        for (index = 0U; index < argument_view.child_count; ++index) {
            RAstRef ref;
            RExpressionResult value = {0};
            if (!r_ast_ref_child(context, *argument_list, (uint32_t)index, &ref) ||
                !r_format_lower_input(body, ref, &prefix, false, &value)) {
                goto cleanup;
            }
            if (!value.valid || body->poisoned) {
                success = true;
                goto cleanup;
            }
            if (!r_hir_vector_push(context, &arguments, value.node)) {
                goto cleanup;
            }
        }
    }
    {
        const RFormatSchema schema = context->format_schemas[schema_id - 1U];
        if (kind != R_FORMAT_CREATE) {
            for (index = 0U; index < schema.part_count; ++index) {
                const RFormatPart *part = &context->format_parts[schema.first_part + index];
                if (part->kind != R_FORMAT_POSITIONAL) {
                    continue;
                }
                if (part->index >= arguments.count - first_explicit) {
                    success =
                        r_format_diagnostic(body, part->span, "missing positional format argument");
                    goto cleanup;
                }
                if (!r_format_check_type(
                        body,
                        r_body_hir_node(body, arguments.items[first_explicit + part->index])->type,
                        part->span,
                        &part->spec) ||
                    body->poisoned) {
                    success = context->resource_status == R_FRONTEND_OK;
                    goto cleanup;
                }
            }
            for (index = first_explicit; index < arguments.count; ++index) {
                size_t part_index;
                bool used = false;
                for (part_index = 0U; part_index < schema.part_count; ++part_index) {
                    const RFormatPart *part =
                        &context->format_parts[schema.first_part + part_index];
                    used = used || (part->kind == R_FORMAT_POSITIONAL &&
                                    part->index == index - first_explicit);
                }
                if (!used) {
                    success =
                        r_format_diagnostic(body,
                                            r_body_hir_node(body, arguments.items[index])->span,
                                            "unused positional format argument");
                    goto cleanup;
                }
            }
        }
        result_type = schema.type;
    }
    if (kind != R_FORMAT_CREATE &&
        !r_semantic_intern_named_standard_type(context, "std.string::string", &result_type)) {
        goto cleanup;
    }
    success =
        r_format_call(body, view.span, kind, schema_id, result_type, &arguments, true, result);
    if (success && result->valid && prefix.count != 0U) {
        uint64_t result_index = (uint64_t)prefix.count;
        size_t drop_index = body->active_symbols.count;
        RHirNodeId value_scope;
        if (!r_hir_vector_push(context, &prefix, result->node)) {
            success = false;
            goto cleanup;
        }
        while (drop_index > scope_base) {
            --drop_index;
            if (!r_body_append_implicit_drop_with_state(
                    body, body->active_symbols.items[drop_index], view.span, &prefix, false, 0U)) {
                success = false;
                goto cleanup;
            }
        }
        if (!r_body_append_node(body,
                                R_HIR_VALUE_SCOPE,
                                view.span,
                                result->type,
                                R_SYMBOL_ID_INVALID,
                                R_TOKEN_INVALID,
                                result_index,
                                false,
                                &prefix,
                                &value_scope)) {
            success = false;
            goto cleanup;
        }
        result->node = value_scope;
    }
cleanup:
    body->active_symbols.count = scope_base;
    r_context_free(context, prefix.items);
    r_context_free(context, arguments.items);
    r_context_free(context, build.parts);
    r_context_free(context, build.paths);
    r_context_free(context, build.captures.items);
    return success;
}

static bool r_format_string_operation(RBodyContext *body,
                                      RAstRef name_ref,
                                      RAstRef arguments_ref,
                                      RSourceSpan span,
                                      bool *handled,
                                      RExpressionResult *result) {
    uint32_t name_id;
    RFormatRecipeKind kind;
    RTypeId source_type;
    RTypeId result_type;
    RTypeId string_type;
    RAstNodeView arguments;
    RAstRef argument;
    RExpressionResult value = {0};
    RHirVector children = {0};
    RHirNodeId storage[1];
    bool checked;
    *handled = false;
    if (!r_semantic_standard_name(body->frontend, name_ref, &name_id)) {
        return body->frontend->resource_status == R_FRONTEND_OK;
    }
    if (r_semantic_intern_name_equal(body->frontend, name_id, "std.string::from_str")) {
        kind = R_FORMAT_STRING_FROM_STR;
    } else if (r_semantic_intern_name_equal(body->frontend, name_id, "std.string::as_str")) {
        kind = R_FORMAT_STRING_AS_STR;
    } else if (r_semantic_intern_name_equal(body->frontend, name_id, "std.string::as_bytes")) {
        kind = R_FORMAT_STRING_AS_BYTES;
    } else if (r_semantic_intern_name_equal(body->frontend, name_id, "std.string::into_bytes")) {
        kind = R_FORMAT_STRING_INTO_BYTES;
    } else {
        return true;
    }
    *handled = true;
    if (!r_ast_ref_view(body->frontend, arguments_ref, &arguments)) {
        return false;
    }
    if (arguments.child_count != 1U) {
        return r_format_diagnostic(body, span, "string operation requires one argument");
    }
    if (!r_semantic_intern_named_standard_type(
            body->frontend, "std.string::string", &string_type) ||
        !r_ast_ref_child(body->frontend, arguments_ref, 0U, &argument)) {
        return false;
    }
    checked = kind == R_FORMAT_STRING_FROM_STR;
    if (checked) {
        result_type = string_type;
        if (!r_semantic_type_from_token(body->frontend, R_TOKEN_KW_STR, &source_type)) {
            return false;
        }
    } else if (kind == R_FORMAT_STRING_INTO_BYTES) {
        source_type = string_type;
        if (!r_semantic_bytes_type(body->frontend, &result_type)) {
            return false;
        }
    } else {
        if (!r_semantic_intern_derived_type(body->frontend,
                                            R_SEMANTIC_TYPE_BORROW,
                                            string_type,
                                            R_TYPE_ID_INVALID,
                                            0U,
                                            R_SEMANTIC_TYPE_FLAG_SHARED,
                                            &source_type)) {
            return false;
        }
        if (kind == R_FORMAT_STRING_AS_STR) {
            if (!r_semantic_type_from_token(body->frontend, R_TOKEN_KW_STR, &result_type)) {
                return false;
            }
        } else {
            RTypeId byte_type;
            if (!r_semantic_type_from_token(body->frontend, R_TOKEN_KW_U8, &byte_type) ||
                !r_semantic_intern_derived_type(body->frontend,
                                                R_SEMANTIC_TYPE_SLICE,
                                                byte_type,
                                                R_TYPE_ID_INVALID,
                                                0U,
                                                R_SEMANTIC_TYPE_FLAG_SHARED,
                                                &result_type)) {
                return false;
            }
        }
    }
    if (!r_body_lower_value(body, argument, source_type, &value) || !value.valid) {
        return body->frontend->resource_status == R_FRONTEND_OK;
    }
    if (value.type != source_type) {
        return r_format_diagnostic(body, span, "string operation argument type does not match");
    }
    storage[0] = value.node;
    children.items = storage;
    children.count = children.capacity = 1U;
    if (!r_format_call(body, span, kind, 0U, result_type, &children, checked, result)) {
        return false;
    }
    if (kind == R_FORMAT_STRING_AS_STR || kind == R_FORMAT_STRING_AS_BYTES) {
        result->borrow_origin = value.borrow_origin;
        result->borrow_origin_set = value.borrow_origin_set;
        result->borrow_origin_multiple = value.borrow_origin_multiple;
        r_body_hir_node(body, result->node)->borrow_origin = value.borrow_origin;
        r_body_hir_node(body, result->node)->borrow_origin_set = value.borrow_origin_set;
        r_body_hir_node(body, result->node)->borrow_origin_multiple = value.borrow_origin_multiple;
        body->frontend->semantic_symbols[result->symbol - 1U].return_borrow_parameter = 1U;
        body->frontend->semantic_symbols[result->symbol - 1U].return_borrow_parameters_low =
            UINT64_C(1);
    }
    return true;
}

/* R-FFI-0063, Library R-SLIB-DL-0004 (B9.3): `std.dl::resolve(&library, import)`.
 *
 * A function of an extern "C" block whose link kind is "loadable" is verified against the
 * headers of its block like every import, but the link step never names its library: the
 * program opens the library itself (std.dl::open) and obtains the function from it. resolve is
 * written with the import as its second argument and lowers to the R function
 * `std.dl::symbol(library, "c_name")`, whose `raw void*?` result becomes a nullable raw function
 * pointer of the import's verified prototype; null when the opened library defines no such
 * symbol. Calling through the pointer is unsafe (R-FUNC-0007), as for every raw function
 * pointer. */

static bool r_dl_diagnostic(RBodyContext *body, RSourceSpan span, const char *message) {
    return r_body_diagnostic(body, "R-DIAG-FFI-003", "R-FFI-0063", message, span);
}

/* Whether `qualified` spells `std.dl::resolve`. */
static bool r_dl_resolve_name(const RFrontendContext *context, RAstRef qualified) {
    RAstNodeView view;
    RAstTokenView components[3];
    uint32_t count = 0U;

    if (!r_ast_view_kind(context, qualified, R_SYNTAX_QUALIFIED_NAME, &view)) {
        return false;
    }
    for (uint32_t index = 0U; index < view.child_count; ++index) {
        RAstRef child;
        RAstTokenView token;
        if (!r_ast_ref_child(context, qualified, index, &child) ||
            !r_ast_ref_token(context, child, &token) || (token.kind != R_TOKEN_IDENTIFIER)) {
            continue;
        }
        if (count == 3U) {
            return false;
        }
        components[count++] = token;
    }
    return (count == 3U) && r_semantic_token_text_equal_owned(context, &components[0], "std") &&
           r_semantic_token_text_equal_owned(context, &components[1], "dl") &&
           r_semantic_token_text_equal_owned(context, &components[2], "resolve");
}

/* The R function std.dl::symbol when the calling module imports std.dl, else zero. */
static RSymbolId r_dl_symbol_function(const RBodyContext *body) {
    const RFrontendContext *context = body->frontend;
    bool imported = false;

    for (size_t index = 0U; index < context->import_count; ++index) {
        const RImportEntry *import = &context->imports[index];
        imported = imported || ((import->source == body->source) && !import->imports_name &&
                                (strcmp(import->module_name, "std.dl") == 0));
    }
    if (!imported) {
        return R_SYMBOL_ID_INVALID;
    }
    for (size_t index = 0U; index < context->semantic_symbol_count; ++index) {
        const RSemanticSymbol *symbol = &context->semantic_symbols[index];
        const RSource *source;
        const char *bytes;
        size_t length;
        if ((symbol->kind != R_SEMANTIC_SYMBOL_FUNCTION) || symbol->is_method ||
            !r_semantic_intern_text(context, symbol->name_intern_id, &bytes, &length) ||
            !r_semantic_bytes_equal_text(bytes, length, "symbol")) {
            continue;
        }
        source = r_get_source_const(context, symbol->module_source);
        if ((source != NULL) && (source->module_name != NULL) &&
            (strcmp(source->module_name, "std.dl") == 0)) {
            return (RSymbolId)(index + 1U);
        }
    }
    return R_SYMBOL_ID_INVALID;
}

/* The import the second argument names: an identifier or a qualified name of a function. */
static RSymbolId r_dl_import(RBodyContext *body, RAstRef argument) {
    RAstRef direct = argument;
    RAstNodeView view;
    RAstTokenView token;
    RBodyLookupIssue issue = R_BODY_LOOKUP_UNRESOLVED;

    for (uint32_t depth = 0U; r_ast_ref_view(body->frontend, direct, &view) &&
                              (depth <= r_frontend_tree_depth_limit(body->frontend));
         ++depth) {
        RAstRef inner;
        if (view.kind == R_SYNTAX_QUALIFIED_NAME) {
            return r_body_resolve_qualified_name(body, direct, &token, &issue);
        }
        if ((view.child_count == 1U) && r_ast_ref_child(body->frontend, direct, 0U, &inner) &&
            r_ast_ref_token(body->frontend, inner, &token)) {
            return (token.kind == R_TOKEN_IDENTIFIER) ? r_body_resolve_name(body, &token, &issue)
                                                      : R_SYMBOL_ID_INVALID;
        }
        if (!r_body_ast_single_node_child(body->frontend, direct, &inner)) {
            break;
        }
        direct = inner;
    }
    return R_SYMBOL_ID_INVALID;
}

static bool r_dl_operation(RBodyContext *body,
                           RAstRef qualified,
                           RAstRef argument_list,
                           RSourceSpan span,
                           bool *handled,
                           RExpressionResult *result) {
    RFrontendContext *context = body->frontend;
    RAstNodeView arguments;
    RAstRef library_ref;
    RAstRef import_ref;
    RSymbolId import_id;
    RSymbolId symbol_function;
    const RSemanticSymbol *import;
    const RSemanticSymbol *function;
    const RSemanticType *signature;
    RTypeId raw_type;
    RTypeId resolved_type;
    RExpressionResult library = {0};
    RExpressionResult name = {0};
    RHirNodeId operands[2];
    RHirNodeId call;
    const char *c_name;
    size_t c_name_length;

    *handled = r_dl_resolve_name(context, qualified);
    if (!*handled) {
        return true;
    }
    r_body_expression_invalid(result);
    if (!r_ast_ref_view(context, argument_list, &arguments) || (arguments.child_count != 2U) ||
        !r_ast_ref_child(context, argument_list, 0U, &library_ref) ||
        !r_ast_ref_child(context, argument_list, 1U, &import_ref)) {
        return r_dl_diagnostic(
            body, span, "std.dl::resolve takes a library and an import of a loadable block");
    }
    symbol_function = r_dl_symbol_function(body);
    if (symbol_function == R_SYMBOL_ID_INVALID) {
        return r_body_diagnostic(body,
                                 "R-DIAG-NAME-001",
                                 "R-MOD-0007",
                                 "std.dl::resolve requires `import std.dl;`",
                                 span);
    }
    import_id = r_dl_import(body, import_ref);
    import = r_body_symbol(body, import_id);
    if ((import == NULL) || (import->kind != R_SEMANTIC_SYMBOL_FUNCTION) || !import->is_import ||
        (import->link_kind != R_LINK_KIND_LOADABLE)) {
        return r_dl_diagnostic(
            body, span, "the second argument of std.dl::resolve names an import of a loadable "
                        "block");
    }
    if (import->is_variadic) {
        return r_dl_diagnostic(
            body, span, "a variadic C function has no raw function pointer type (R-FFI-0019)");
    }
    if (!import->signature_supported) {
        return r_body_cascade(body);
    }
    /* The pointer type of the verified prototype, nullable: the library may lack the symbol. */
    if (!r_semantic_import_raw_function_type(context, import_id, &raw_type)) {
        return false;
    }
    signature = r_semantic_type(context, raw_type);
    if ((signature == NULL) ||
        !r_semantic_intern_derived_type(context,
                                        R_SEMANTIC_TYPE_RAW_FUNCTION,
                                        signature->base,
                                        signature->second,
                                        signature->length,
                                        signature->flags | R_SEMANTIC_TYPE_FLAG_NULLABLE,
                                        &resolved_type)) {
        return false;
    }
    import = r_body_symbol(body, import_id);
    function = r_body_symbol(body, symbol_function);
    if ((import == NULL) || (function == NULL) || (function->parameter_count != 2U) ||
        !r_semantic_intern_text(context,
                                import->c_name_intern_id != 0U ? import->c_name_intern_id
                                                               : import->name_intern_id,
                                &c_name,
                                &c_name_length)) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    {
        const RTypeId library_type =
            context->semantic_parameter_types[function->first_parameter_type];
        const RTypeId name_type =
            context->semantic_parameter_types[function->first_parameter_type + 1U];
        if (!r_body_lower_value(body, library_ref, library_type, &library) ||
            (library.valid && !r_body_finish_value(body, library_type, &library)) ||
            !r_reflection_append_text_node(body, span, c_name, c_name_length, &name) ||
            (name.valid && !r_body_finish_value(body, name_type, &name))) {
            return false;
        }
    }
    if (!library.valid || !name.valid) {
        return true;
    }
    function = r_body_symbol(body, symbol_function);
    operands[0] = library.node;
    operands[1] = name.node;
    {
        RHirVector children = {operands, 2U, 2U};
        if (!r_body_append_node(body,
                                R_HIR_CALL,
                                span,
                                function->return_type,
                                symbol_function,
                                R_TOKEN_INVALID,
                                UINT64_C(0),
                                false,
                                &children,
                                &call)) {
            return false;
        }
    }
    r_body_hir_node(body, call)->auxiliary_type = function->effect_carrier_type;
    r_body_hir_node(body, call)->borrow_origin = R_SYMBOL_ID_INVALID;
    {
        /* The address becomes a pointer of the import's prototype: a conversion source code
           cannot write (R-EXPR-0019), made here because the headers proved the prototype. */
        RHirVector children = {&call, 1U, 1U};
        if (!r_body_append_node(body,
                                R_HIR_CAST,
                                span,
                                resolved_type,
                                R_SYMBOL_ID_INVALID,
                                R_TOKEN_INVALID,
                                UINT64_C(0),
                                false,
                                &children,
                                &result->node)) {
            return false;
        }
    }
    result->type = resolved_type;
    result->symbol = R_SYMBOL_ID_INVALID;
    result->borrow_origin = R_SYMBOL_ID_INVALID;
    result->valid = true;
    return true;
}

/* Standard methods are registered aliases of existing operations. The generated
 * call retains the source span and evaluates the receiver exactly once. */
typedef struct RStandardMethodDescriptor {
    const char *owner;
    const char *name;
    const char *module;
    const char *operation;
    uint32_t receiver;
    uint32_t argument_index;
    bool is_async;
} RStandardMethodDescriptor;

typedef struct RStandardResultDescriptor {
    const char *operation;
    const char *result;
} RStandardResultDescriptor;

#include "standard_methods.generated.inc"

static bool r_standard_method_owner(const RFrontendContext *context,
                                    const RSemanticType *type,
                                    const char *owner) {
    if (type == NULL) {
        return false;
    }
    if (type->kind == R_SEMANTIC_TYPE_STANDARD) {
        return r_semantic_standard_type_name_equal(context, type, owner);
    }
    if (type->kind == R_SEMANTIC_TYPE_STRUCT || type->kind == R_SEMANTIC_TYPE_ENUM) {
        const RSemanticAggregate *aggregate = &context->semantic_aggregates[type->base - 1U];
        const RSource *source = r_get_source_const(context, aggregate->module_source);
        if (source != NULL && source->module_name != NULL) {
            const RInternEntry *name = &context->intern_entries[aggregate->name_intern_id - 1U];
            const size_t module_length = strlen(source->module_name);
            return strlen(owner) == module_length + 2U + name->length &&
                   memcmp(owner, source->module_name, module_length) == 0 &&
                   memcmp(owner + module_length, "::", 2U) == 0 &&
                   memcmp(owner + module_length + 2U, name->bytes, name->length) == 0;
        }
        return false;
    }
    if (strcmp(owner, "bytes") == 0) {
        return type->kind == R_SEMANTIC_TYPE_ARRAY &&
               r_semantic_value_kind(context, type->base) == R_SEMANTIC_TYPE_U8;
    }
    if (type->kind == R_SEMANTIC_TYPE_WEAK) {
        return strcmp(owner,
                      (type->flags & R_SEMANTIC_TYPE_FLAG_RC_OWNER) != 0U ? "weak_rc"
                                                                          : "weak_arc") == 0;
    }
    return strcmp(owner, r_semantic_type_kind_name(type->kind)) == 0;
}

static bool r_standard_call_ast_node(RFrontendContext *context,
                                     RSourceSpan span,
                                     RSyntaxKind kind,
                                     const char *name,
                                     const RAstNodeId *children,
                                     size_t count,
                                     RAstRef *result) {
    RSource *source = r_get_source(context, span.source);
    RAstNode node = {0};
    if (source == NULL || source->ast_node_count >= UINT32_MAX ||
        count > UINT32_MAX - source->ast_child_count) {
        context->resource_status = R_FRONTEND_LIMIT_EXCEEDED;
        return false;
    }
    node.kind = kind;
    node.span = span;
    node.first_child = (uint32_t)source->ast_child_count;
    node.child_count = (uint32_t)count;
    if ((name != NULL &&
         !r_intern_bytes(context, (const uint8_t *)name, strlen(name), &node.generated_name)) ||
        !r_grow_array(context,
                      (void **)&source->ast_nodes,
                      &source->ast_node_capacity,
                      sizeof(*source->ast_nodes),
                      source->ast_node_count + 1U) ||
        !r_grow_array(context,
                      (void **)&source->ast_children,
                      &source->ast_child_capacity,
                      sizeof(*source->ast_children),
                      source->ast_child_count + count)) {
        return false;
    }
    if (count != 0U) {
        memcpy(source->ast_children + source->ast_child_count, children, count * sizeof(*children));
        source->ast_child_count += count;
    }
    source->ast_nodes[source->ast_node_count++] = node;
    *result = (RAstRef){span.source, (RAstNodeId)source->ast_node_count};
    return true;
}

static bool r_standard_call_name(RFrontendContext *context,
                                 RSourceSpan span,
                                 const char *module,
                                 const char *operation,
                                 RAstRef *primary) {
    RAstNodeId parts[3];
    const char *names[] = {"std", module, operation};
    RAstRef ref;
    uint32_t index;
    for (index = 0U; index < 3U; ++index) {
        if (!r_standard_call_ast_node(
                context, span, R_SYNTAX_TOKEN, names[index], NULL, 0U, &ref)) {
            return false;
        }
        parts[index] = ref.node;
    }
    return r_standard_call_ast_node(
               context, span, R_SYNTAX_QUALIFIED_NAME, NULL, parts, 3U, &ref) &&
           r_standard_call_ast_node(
               context, span, R_SYNTAX_PRIMARY_EXPRESSION, NULL, &ref.node, 1U, primary);
}

static bool r_standard_call_unary(RFrontendContext *context,
                                  RSourceSpan span,
                                  RTokenKind operation,
                                  RAstRef operand,
                                  RAstRef *result) {
    RAstRef token;
    RAstNodeId children[2];
    if (!r_standard_call_ast_node(
            context, span, R_SYNTAX_TOKEN, r_token_kind_name(operation), NULL, 0U, &token)) {
        return false;
    }
    r_get_source(context, span.source)->ast_nodes[token.node - 1U].generated_token_kind = operation;
    children[0] = token.node;
    children[1] = operand.node;
    return r_standard_call_ast_node(
        context, span, R_SYNTAX_UNARY_EXPRESSION, NULL, children, 2U, result);
}

/* R-SLIB-ASYNC-0008: a call may omit the trailing deadline argument of a standard operation.
 * The omitted argument is o::none, so the operation keeps the deadline of its task (Core
 * R-STMT-0019). */
static bool r_standard_complete_deadline(RBodyContext *body,
                                         RAstRef name_ref,
                                         RSourceSpan span,
                                         RAstRef *arguments,
                                         RAstNodeView *view) {
    RFrontendContext *context = body->frontend;
    RAstNodeView name_view;
    RAstTokenView components[3];
    uint32_t count = 0U;
    size_t entry;

    if (!r_ast_view_kind(context, name_ref, R_SYNTAX_QUALIFIED_NAME, &name_view)) {
        return true;
    }
    for (uint32_t index = 0U; index < name_view.child_count; ++index) {
        RAstRef child;
        RAstTokenView token;
        if (!r_ast_ref_child(context, name_ref, index, &child) ||
            !r_ast_ref_token(context, child, &token)) {
            continue;
        }
        if (count == 3U) {
            return true;
        }
        components[count++] = token;
    }
    if (count != 3U || !r_semantic_token_text_equal_owned(context, &components[0], "std")) {
        return true;
    }
    for (entry = 0U;
         entry < sizeof(r_standard_deadline_operations) / sizeof(r_standard_deadline_operations[0]);
         ++entry) {
        const RStandardDeadlineOperation *operation = &r_standard_deadline_operations[entry];
        if (r_semantic_token_text_equal_owned(context, &components[1], operation->module) &&
            r_semantic_token_text_equal_owned(context, &components[2], operation->operation)) {
            break;
        }
    }
    if (entry ==
            sizeof(r_standard_deadline_operations) / sizeof(r_standard_deadline_operations[0]) ||
        view->child_count + 1U != r_standard_deadline_operations[entry].argument_count) {
        return true;
    }
    RAstNodeId *children =
        r_context_allocate(context, ((size_t)view->child_count + 1U) * sizeof(*children));
    RAstRef family, none, qualified, primary;
    bool success = children != NULL;
    for (uint32_t index = 0U; success && index < view->child_count; ++index) {
        RAstRef argument;
        success = r_ast_ref_child(context, *arguments, index, &argument);
        children[index] = argument.node;
    }
    success =
        success && r_standard_call_ast_node(context, span, R_SYNTAX_TOKEN, "o", NULL, 0U, &family);
    if (success) {
        r_get_source(context, span.source)->ast_nodes[family.node - 1U].generated_token_kind =
            R_TOKEN_KW_O;
        success = r_standard_call_ast_node(context, span, R_SYNTAX_TOKEN, "none", NULL, 0U, &none);
    }
    if (success) {
        const RAstNodeId parts[2] = {family.node, none.node};
        success =
            r_standard_call_ast_node(
                context, span, R_SYNTAX_QUALIFIED_NAME, NULL, parts, 2U, &qualified) &&
            r_standard_call_ast_node(
                context, span, R_SYNTAX_PRIMARY_EXPRESSION, NULL, &qualified.node, 1U, &primary);
    }
    if (success) {
        children[view->child_count] = primary.node;
        success = r_standard_call_ast_node(context,
                                           span,
                                           R_SYNTAX_ARGUMENT_LIST,
                                           NULL,
                                           children,
                                           (size_t)view->child_count + 1U,
                                           arguments) &&
                  r_ast_ref_view(context, *arguments, view);
    }
    r_context_free(context, children);
    return success;
}

static bool r_standard_record_operation(RBodyContext *body,
                                        const char *module,
                                        const char *operation,
                                        RExpressionResult *result) {
    char name[192];
    if (!result->valid || result->node == 0U) {
        return true;
    }
    if (r_body_hir_node(body, result->node)->source_operation != 0U) {
        return true;
    }
    int length = snprintf(name, sizeof(name), "%s::%s", module, operation);
    uint32_t identity;
    if (length < 0 || (size_t)length >= sizeof(name) ||
        !r_intern_bytes(body->frontend, (const uint8_t *)name, (size_t)length, &identity)) {
        return false;
    }
    r_body_hir_node(body, result->node)->source_operation = identity;
    return true;
}

/* R-NAME-0010 (M24-10): `std.module::type::method` names the method that R-FUNC-0014 registers
   for the standard type, or NULL when the qualified name has another form; *owner_known reports
   that the prefix names a standard type with registered methods. */
static const RStandardMethodDescriptor *
r_standard_type_method(RFrontendContext *context, RAstRef name_ref, bool *owner_known) {
    RAstNodeView view;
    RAstTextView method_name = {0};
    char owner[256];
    size_t length = 0U;
    *owner_known = false;
    if (!r_ast_view_kind(context, name_ref, R_SYNTAX_QUALIFIED_NAME, &view) ||
        view.child_count < 4U) {
        return NULL;
    }
    for (uint32_t component = 0U; component < view.child_count; ++component) {
        RAstRef part;
        RAstTokenView token;
        RAstTextView text;
        if (!r_ast_ref_child(context, name_ref, component, &part) ||
            !r_ast_ref_token(context, part, &token) || token.kind != R_TOKEN_IDENTIFIER ||
            !r_ast_ref_text(context, part, &text)) {
            return NULL;
        }
        if (component + 1U == view.child_count) {
            method_name = text;
            break;
        }
        const char *separator = component == 0U                      ? ""
                                : component + 2U == view.child_count ? "::"
                                                                     : ".";
        const size_t separator_length = strlen(separator);
        if (length + separator_length + text.length >= sizeof(owner)) {
            return NULL;
        }
        memcpy(owner + length, separator, separator_length);
        length += separator_length;
        memcpy(owner + length, text.bytes, text.length);
        length += text.length;
    }
    owner[length] = '\0';
    for (size_t index = 0U; index < sizeof(r_standard_methods) / sizeof(r_standard_methods[0]);
         ++index) {
        const RStandardMethodDescriptor *method = &r_standard_methods[index];
        if (strcmp(method->owner, owner) != 0) {
            continue;
        }
        *owner_known = true;
        if (strlen(method->name) == method_name.length &&
            memcmp(method->name, method_name.bytes, method_name.length) == 0) {
            return method;
        }
    }
    return NULL;
}

/* R-NAME-0010 (M24-10): `std.module::type::method(receiver, arguments)` calls the operation of
   the receiver form `receiver.method(arguments)`; the first argument is the receiver as the
   operation takes it (a borrow for shared and exclusive receivers, the value otherwise). */
static bool r_standard_lower_type_method(RBodyContext *body,
                                         RAstRef postfix,
                                         RAstRef name_ref,
                                         RAstRef arguments,
                                         const RAstNodeView *view,
                                         RSourceSpan span,
                                         RTypeId expected,
                                         RExpressionResult *result,
                                         bool *matched) {
    RFrontendContext *context = body->frontend;
    bool owner_known;
    const RStandardMethodDescriptor *method =
        r_standard_type_method(context, name_ref, &owner_known);
    RAstRef receiver, primary, call, list = arguments;
    RTypeId receiver_type;
    RAstNodeId *children = NULL;
    bool success = false;
    *matched = owner_known;
    if (method == NULL) {
        return !owner_known || r_body_diagnostic(body,
                                                 "R-DIAG-NAME-001",
                                                 "R-NAME-0010",
                                                 "the standard type has no method of this name",
                                                 span);
    }
    if (view->child_count == 0U || !r_ast_ref_child(context, arguments, 0U, &receiver)) {
        return r_body_diagnostic(body,
                                 "R-DIAG-TYPE-001",
                                 "R-NAME-0010",
                                 "a method named through its type takes the receiver as its "
                                 "first argument",
                                 span);
    }
    if (!r_overload_argument_type(body, receiver, 0U, false, &receiver_type)) {
        return false;
    }
    if (receiver_type != R_TYPE_ID_INVALID) {
        const RSemanticType *type =
            r_semantic_type(context, r_semantic_value_type(context, receiver_type));
        if (type != NULL && type->kind == R_SEMANTIC_TYPE_BORROW) {
            type = r_semantic_type(context, type->base);
        }
        if (!r_standard_method_owner(context, type, method->owner)) {
            return r_body_diagnostic(body,
                                     "R-DIAG-TYPE-001",
                                     "R-NAME-0010",
                                     "the first argument is not a receiver of the type that owns "
                                     "this method",
                                     span);
        }
    }
    if (method->argument_index >= view->child_count) {
        return r_body_diagnostic(body,
                                 "R-DIAG-TYPE-001",
                                 "R-FUNC-0014",
                                 "standard method has too few arguments",
                                 span);
    }
    if (method->argument_index != 0U) {
        children = r_context_allocate(context, view->child_count * sizeof(*children));
        if (children == NULL) {
            return false;
        }
        for (uint32_t index = 0U; index < view->child_count; ++index) {
            const uint32_t from = index < method->argument_index    ? index + 1U
                                  : index == method->argument_index ? 0U
                                                                    : index;
            RAstRef argument;
            if (!r_ast_ref_child(context, arguments, from, &argument)) {
                goto cleanup;
            }
            children[index] = argument.node;
        }
        if (!r_standard_call_ast_node(
                context, span, R_SYNTAX_ARGUMENT_LIST, NULL, children, view->child_count, &list)) {
            goto cleanup;
        }
    }
    if (!r_standard_call_name(context, span, method->module, method->operation, &primary) ||
        !r_standard_call_ast_node(
            context, span, R_SYNTAX_CALL_SUFFIX, NULL, &list.node, 1U, &call)) {
        goto cleanup;
    }
    success = r_body_lower_call(body, postfix, primary, call, expected, result);
    if (success) {
        char module[96];
        (void)snprintf(module, sizeof(module), "std.%s", method->module);
        success = r_standard_record_operation(body, module, method->operation, result);
    }
cleanup:
    r_context_free(context, children);
    return success;
}

static bool r_standard_lower_postfix_method(RBodyContext *body,
                                            RAstRef postfix,
                                            RTypeId expected,
                                            RExpressionResult *result,
                                            bool *matched) {
    RFrontendContext *context = body->frontend;
    RAstNodeView view, suffix_view, arguments_view;
    RAstRef suffix, receiver, arguments, primary, call;
    RAstTokenView name;
    RTypeId owner;
    const RSemanticType *type;
    const RStandardMethodDescriptor *method = NULL;
    RAstNodeId *children = NULL;
    bool arrow;
    bool success = false;
    size_t index;
    *matched = false;
    if (!r_ast_ref_view(context, postfix, &view) || view.child_count < 2U ||
        !r_ast_ref_child(context, postfix, view.child_count - 1U, &suffix) ||
        !r_ast_ref_view(context, suffix, &suffix_view) ||
        suffix_view.kind != R_SYNTAX_METHOD_CALL_SUFFIX ||
        !r_semantic_first_identifier(context, suffix, &name)) {
        return true;
    }
    children = r_context_allocate(context, view.child_count * sizeof(*children));
    if (children == NULL) {
        return false;
    }
    for (index = 0U; index + 1U < view.child_count; ++index) {
        RAstRef child;
        if (!r_ast_ref_child(context, postfix, (uint32_t)index, &child)) {
            goto cleanup;
        }
        children[index] = child.node;
    }
    if (!r_standard_call_ast_node(context,
                                  view.span,
                                  R_SYNTAX_POSTFIX_EXPRESSION,
                                  NULL,
                                  children,
                                  view.child_count - 1U,
                                  &receiver) ||
        !r_overload_argument_type(body, receiver, 0U, false, &owner)) {
        goto cleanup;
    }
    owner = r_semantic_value_type(context, owner);
    type = r_semantic_type(context, owner);
    arrow = r_ast_has_direct_token(context, suffix, R_TOKEN_ARROW);
    if (arrow && type != NULL && type->kind == R_SEMANTIC_TYPE_BORROW &&
        (type->flags & R_SEMANTIC_TYPE_FLAG_NULLABLE) == 0U) {
        owner = type->base;
        type = r_semantic_type(context, owner);
    } else if (arrow) {
        success = true;
        goto cleanup;
    }
    for (index = 0U; index < sizeof(r_standard_methods) / sizeof(r_standard_methods[0]); ++index) {
        const RStandardMethodDescriptor *candidate = &r_standard_methods[index];
        if (r_standard_method_owner(context, type, candidate->owner) &&
            r_semantic_token_text_equal_owned(context, &name, candidate->name)) {
            method = candidate;
            break;
        }
    }
    if (method == NULL) {
        success = true;
        goto cleanup;
    }
    *matched = true;
    if (arrow && method->receiver == R_SEMANTIC_RECEIVER_VALUE) {
        success = r_body_diagnostic(body,
                                    "R-DIAG-MOVE-003",
                                    "R-FUNC-0014",
                                    "a consuming method requires an owned value receiver",
                                    view.span);
        goto cleanup;
    }
    if (!r_ast_find_direct_child(context, suffix, R_SYNTAX_ARGUMENT_LIST, &arguments) ||
        !r_ast_ref_view(context, arguments, &arguments_view) ||
        !r_standard_call_name(context, view.span, method->module, method->operation, &primary)) {
        goto cleanup;
    }
    if (method->receiver == R_SEMANTIC_RECEIVER_VALUE) {
        /* The parentheses belong to method syntax. Pass the inner move directly
         * to the existing two-phase async startup and consuming-operation checks. */
        for (uint32_t depth = 0U; depth < context->options.limits.max_nesting; ++depth) {
            RAstNodeView receiver_view;
            RAstRef child;
            if (!r_ast_ref_view(context, receiver, &receiver_view)) {
                goto cleanup;
            }
            if (receiver_view.kind != R_SYNTAX_PRIMARY_EXPRESSION &&
                receiver_view.kind != R_SYNTAX_POSTFIX_EXPRESSION &&
                receiver_view.kind != R_SYNTAX_EXPRESSION &&
                receiver_view.kind != R_SYNTAX_BINARY_EXPRESSION) {
                break;
            }
            if (!r_body_ast_single_node_child(context, receiver, &child)) {
                break;
            }
            {
                RAstNodeView child_view;

                /* M22-1: a qualified constant such as `std.signal::kind::user1` is the only
                   child of its primary expression but no expression itself. */
                if (!r_ast_ref_view(context, child, &child_view)) {
                    goto cleanup;
                }
                if (child_view.kind != R_SYNTAX_PRIMARY_EXPRESSION &&
                    child_view.kind != R_SYNTAX_POSTFIX_EXPRESSION &&
                    child_view.kind != R_SYNTAX_EXPRESSION &&
                    child_view.kind != R_SYNTAX_BINARY_EXPRESSION &&
                    child_view.kind != R_SYNTAX_UNARY_EXPRESSION) {
                    break;
                }
            }
            receiver = child;
        }
    }
    if ((arrow && !r_standard_call_unary(context, view.span, R_TOKEN_STAR, receiver, &receiver)) ||
        (method->receiver != R_SEMANTIC_RECEIVER_VALUE &&
         !r_standard_call_unary(context, view.span, R_TOKEN_AMP, receiver, &receiver))) {
        goto cleanup;
    }
    if (method->argument_index > arguments_view.child_count) {
        success = r_body_diagnostic(body,
                                    "R-DIAG-TYPE-001",
                                    "R-FUNC-0014",
                                    "standard method has too few arguments",
                                    view.span);
        goto cleanup;
    }
    r_context_free(context, children);
    children =
        r_context_allocate(context, ((size_t)arguments_view.child_count + 1U) * sizeof(*children));
    if (children == NULL) {
        goto cleanup;
    }
    for (index = 0U; index <= arguments_view.child_count; ++index) {
        RAstRef argument;
        if (index == method->argument_index) {
            children[index] = receiver.node;
        } else {
            if (!r_ast_ref_child(context,
                                 arguments,
                                 (uint32_t)index - (index > method->argument_index ? 1U : 0U),
                                 &argument)) {
                goto cleanup;
            }
            children[index] = argument.node;
        }
    }
    if (!r_standard_call_ast_node(context,
                                  view.span,
                                  R_SYNTAX_ARGUMENT_LIST,
                                  NULL,
                                  children,
                                  (size_t)arguments_view.child_count + 1U,
                                  &arguments) ||
        !r_standard_call_ast_node(
            context, view.span, R_SYNTAX_CALL_SUFFIX, NULL, &arguments.node, 1U, &call)) {
        goto cleanup;
    }
    success = r_body_lower_call(body, postfix, primary, call, expected, result);
    if (success) {
        char module[96];
        (void)snprintf(module, sizeof(module), "std.%s", method->module);
        success = r_standard_record_operation(body, module, method->operation, result);
    }
cleanup:
    r_context_free(context, children);
    return success;
}

static bool r_standard_select_math(RBodyContext *body,
                                   RAstRef name_ref,
                                   RAstRef arguments,
                                   const RAstNodeView *view,
                                   RSourceSpan span,
                                   uint64_t *operation_id,
                                   bool *matched) {
    RFrontendContext *context = body->frontend;
    RAstTokenView name;
    *operation_id = 0U;
    *matched = false;
    if (r_body_standard_module_operation_name(context, name_ref, "math", &name)) {
        uint64_t existing_operation;
        if (r_body_standard_math_operation(context, name_ref, &existing_operation, NULL)) {
            return true;
        }
        static const char *const suffixes[] = {"_complex_f32",
                                               "_complex_f64",
                                               "_c_long_double",
                                               "_c_double",
                                               "_c_float",
                                               "_f32",
                                               "_f64"};
        RTypeVector types = {0};
        uint64_t selected = 0U, exact_selected = 0U;
        size_t viable_count = 0U, exact_count = 0U;
        size_t index;
        bool success = false;
        for (index = 0U; index < view->child_count; ++index) {
            RAstRef argument;
            RTypeId type;
            if (!r_ast_ref_child(context, arguments, (uint32_t)index, &argument) ||
                !r_overload_argument_type(body, argument, 0U, false, &type) ||
                !r_type_vector_push(context, &types, r_semantic_value_type(context, type))) {
                goto cleanup;
            }
        }
        for (index = 0U; index < r_standard_math_operation_count(); ++index) {
            const RStandardMathOperationDescriptor *descriptor =
                r_standard_math_operation(index + 1U);
            const size_t length = strlen(descriptor->name);
            size_t suffix_index;
            bool named = false;
            bool viable = true;
            bool exact = true;
            uint32_t parameter;
            for (suffix_index = 0U; suffix_index < sizeof(suffixes) / sizeof(suffixes[0]);
                 ++suffix_index) {
                const size_t suffix_length = strlen(suffixes[suffix_index]);
                if (length > suffix_length && strcmp(descriptor->name + length - suffix_length,
                                                     suffixes[suffix_index]) == 0) {
                    const RInternEntry *entry = &context->intern_entries[name.intern_id - 1U];
                    named = entry->length == length - suffix_length &&
                            memcmp(entry->bytes, descriptor->name, entry->length) == 0;
                    break;
                }
            }
            if (!named) {
                continue;
            }
            *matched = true;
            if (descriptor->parameter_count != types.count) {
                continue;
            }
            for (parameter = 0U; parameter < descriptor->parameter_count; ++parameter) {
                RTypeId type;
                if (!r_body_standard_math_type(context,
                                               parameter == 0U ? descriptor->parameter0_type
                                                               : descriptor->parameter1_type,
                                               &type)) {
                    goto cleanup;
                }
                viable = viable && r_overload_compatible(body, types.items[parameter], type);
                exact = exact && types.items[parameter] == type;
            }
            if (viable) {
                selected = index + 1U;
                ++viable_count;
                if (exact) {
                    exact_selected = selected;
                    ++exact_count;
                }
            }
        }
        if (!*matched) {
            success = true;
        } else if (exact_count == 1U || (exact_count == 0U && viable_count == 1U)) {
            *operation_id = exact_count == 1U ? exact_selected : selected;
            success = true;
        } else {
            success = r_body_diagnostic(body,
                                        "R-DIAG-TYPE-001",
                                        "R-FUNC-0004",
                                        viable_count == 0U
                                            ? "no math overload accepts these argument types"
                                            : "math call is ambiguous between overloads",
                                        span);
        }
    cleanup:
        r_context_free(context, types.items);
        return success;
    }
    return true;
}

static bool r_standard_select_integer(
    RBodyContext *body, RTypeId type, RTypeId other, RSourceSpan span, RTypeId *result) {
    RFrontendContext *context = body->frontend;
    *result = R_TYPE_ID_INVALID;
    RTypeId selected = R_TYPE_ID_INVALID, exact_type = R_TYPE_ID_INVALID;
    size_t viable_count = 0U, exact_count = 0U;
    for (size_t numeric = 0U; numeric < 10U; ++numeric) {
        RTypeId candidate;
        if (!r_semantic_type_from_token(
                context, r_checked_numeric_destinations[numeric].token_kind, &candidate)) {
            return false;
        }
        if (r_overload_compatible(body, type, candidate) &&
            r_overload_compatible(body, other, candidate)) {
            selected = candidate;
            ++viable_count;
            if (type == candidate && other == candidate) {
                exact_type = candidate;
                ++exact_count;
            }
        }
    }
    if (exact_count != 1U && (exact_count != 0U || viable_count != 1U)) {
        return r_body_diagnostic(body,
                                 "R-DIAG-TYPE-001",
                                 "R-FUNC-0004",
                                 viable_count == 0U
                                     ? "no core arithmetic overload accepts these types"
                                     : "core arithmetic call is ambiguous between overloads",
                                 span);
    }
    *result = exact_count == 1U ? exact_type : selected;
    return true;
}

static bool r_standard_lower_overload(RBodyContext *body,
                                      RAstRef name_ref,
                                      RAstRef arguments,
                                      const RAstNodeView *view,
                                      RSourceSpan span,
                                      RTypeId expected,
                                      RExpressionResult *result,
                                      bool *matched) {
    RFrontendContext *context = body->frontend;
    RAstTokenView name;
    bool formatting = r_body_standard_module_operation_name(context, name_ref, "format", &name);
    bool string =
        !formatting && r_body_standard_module_operation_name(context, name_ref, "string", &name);
    *matched = false;
    {
        static const struct {
            const char *module;
            const char *name;
        } aliases[] = {{"arc", "clone"},
                       {"rc", "clone"},
                       {"io", "close"},
                       {"fs", "close"},
                       {"net", "close"},
                       {"time", "add"},
                       {"time", "sub"},
                       {"time", "multiply"},
                       {"time", "compare"}};
        for (size_t alias = 0U; alias < sizeof(aliases) / sizeof(aliases[0]); ++alias) {
            RAstTokenView operation;
            if (!r_body_standard_module_operation_name(
                    context, name_ref, aliases[alias].module, &operation) ||
                !r_semantic_token_text_equal_owned(context, &operation, aliases[alias].name)) {
                continue;
            }
            RAstRef argument;
            RTypeId source;
            if (view->child_count == 0U || !r_ast_ref_child(context, arguments, 0U, &argument) ||
                !r_overload_argument_type(body, argument, 0U, false, &source)) {
                return true;
            }
            const RSemanticType *owner =
                r_semantic_type(context, r_semantic_value_type(context, source));
            if (owner != NULL && owner->kind == R_SEMANTIC_TYPE_BORROW) {
                owner = r_semantic_type(context, owner->base);
            }
            for (size_t index = 0U;
                 index < sizeof(r_standard_methods) / sizeof(r_standard_methods[0]);
                 ++index) {
                const RStandardMethodDescriptor *method = &r_standard_methods[index];
                if (strcmp(method->module, aliases[alias].module) != 0 ||
                    strcmp(method->name, aliases[alias].name) != 0 ||
                    strcmp(method->operation, aliases[alias].name) == 0 ||
                    !r_standard_method_owner(context, owner, method->owner)) {
                    continue;
                }
                RAstRef primary, call;
                *matched = true;
                return r_standard_call_name(
                           context, span, method->module, method->operation, &primary) &&
                       r_standard_call_ast_node(
                           context, span, R_SYNTAX_CALL_SUFFIX, NULL, &arguments.node, 1U, &call) &&
                       r_body_lower_call(body, primary, primary, call, expected, result);
            }
        }
    }
    {
        RAstTokenView operation;
        if (view->child_count == 2U &&
            r_body_standard_module_operation_name(context, name_ref, "bytes", &operation)) {
            const bool append = r_semantic_token_text_equal_owned(context, &operation, "append");
            const bool find = r_semantic_token_text_equal_owned(context, &operation, "find");
            if (append || find) {
                RAstRef argument;
                RTypeId value;
                if (!r_ast_ref_child(context, arguments, 1U, &argument) ||
                    !r_overload_argument_type(body, argument, 0U, false, &value)) {
                    return false;
                }
                if ((append && r_semantic_value_kind(context, value) == R_SEMANTIC_TYPE_U8) ||
                    (find &&
                     (r_semantic_value_kind(context, value) == R_SEMANTIC_TYPE_SLICE ||
                      r_semantic_value_kind(context, value) == R_SEMANTIC_TYPE_FIXED_ARRAY ||
                      r_semantic_value_kind(context, value) == R_SEMANTIC_TYPE_ARRAY ||
                      r_semantic_value_kind(context, value) == R_SEMANTIC_TYPE_STR ||
                      r_semantic_value_kind(context, value) == R_SEMANTIC_TYPE_CONSTEXPR_STR))) {
                    *matched = true;
                    return r_body_lower_standard_bytes_call(body,
                                                            arguments,
                                                            view,
                                                            span,
                                                            operation.span,
                                                            expected,
                                                            append
                                                                ? R_STANDARD_CALL_BYTES_APPEND_U8
                                                                : R_STANDARD_CALL_BYTES_FIND_SLICE,
                                                            result) &&
                           r_standard_record_operation(
                               body, "std.bytes", append ? "append_u8" : "find_slice", result);
                }
            }
        }
    }
    {
        static const struct {
            const char *name;
            RStandardCallOperation operation;
        } operations[] = {
            {"checked_add", R_STANDARD_CALL_CORE_CHECKED_ADD},
            {"checked_sub", R_STANDARD_CALL_CORE_CHECKED_SUB},
            {"checked_mul", R_STANDARD_CALL_CORE_CHECKED_MUL},
            {"wrapping_add", R_STANDARD_CALL_CORE_WRAPPING_ADD},
            {"wrapping_sub", R_STANDARD_CALL_CORE_WRAPPING_SUB},
            {"wrapping_mul", R_STANDARD_CALL_CORE_WRAPPING_MUL},
            {"saturating_add", R_STANDARD_CALL_CORE_SATURATING_ADD},
            {"saturating_sub", R_STANDARD_CALL_CORE_SATURATING_SUB},
            {"saturating_mul", R_STANDARD_CALL_CORE_SATURATING_MUL},
        };
        RAstNodeView qualified_view;
        RAstRef root, item;
        RAstTokenView scope, operation;
        size_t index;
        if (r_ast_ref_view(context, name_ref, &qualified_view) &&
            qualified_view.kind == R_SYNTAX_QUALIFIED_NAME && qualified_view.child_count == 2U &&
            r_ast_ref_child(context, name_ref, 0U, &root) &&
            r_ast_ref_token(context, root, &scope) &&
            r_semantic_token_text_equal_owned(context, &scope, "core") &&
            r_ast_ref_child(context, name_ref, 1U, &item) &&
            r_ast_ref_token(context, item, &operation)) {
            for (index = 0U; index < sizeof(operations) / sizeof(operations[0]); ++index) {
                if (r_semantic_token_text_equal_owned(
                        context, &operation, operations[index].name)) {
                    RAstRef operand;
                    RTypeId type, other;
                    uint64_t tag;
                    *matched = true;
                    if (view->child_count != 2U ||
                        !r_ast_ref_child(context, arguments, 0U, &operand) ||
                        !r_overload_argument_type(body, operand, 0U, false, &type) ||
                        !r_ast_ref_child(context, arguments, 1U, &operand) ||
                        !r_overload_argument_type(body, operand, 0U, false, &other)) {
                        return r_body_diagnostic(body,
                                                 "R-DIAG-TYPE-001",
                                                 "R-FUNC-0004",
                                                 "core arithmetic requires two integer arguments",
                                                 span);
                    }
                    if (!r_standard_select_integer(body, type, other, span, &type)) {
                        return false;
                    }
                    if (type == R_TYPE_ID_INVALID) {
                        return true;
                    }
                    if (!r_semantic_checked_numeric_type_tag(context, type, &tag)) {
                        return false;
                    }
                    char spelling[96];
                    const RTokenKind token = r_checked_numeric_destinations[tag].token_kind;
                    (void)snprintf(spelling,
                                   sizeof(spelling),
                                   "%s_%s",
                                   operations[index].name,
                                   r_token_kind_name(token));
                    return r_body_lower_core_integer(body,
                                                     arguments,
                                                     view,
                                                     span,
                                                     operation.span,
                                                     operations[index].operation,
                                                     token,
                                                     result) &&
                           r_standard_record_operation(body, "core", spelling, result);
                }
            }
        }
    }
    if ((formatting || string) && r_semantic_token_text_equal_owned(context, &name, "append")) {
        RExpressionResult prelowered;
        RTypeId value_type;
        RAstRef value;
        RSemanticTypeKind kind;
        RStandardCallOperation operation = R_STANDARD_CALL_INVALID;
        uint64_t numeric_tag;
        *matched = true;
        if (view->child_count < 2U || !r_ast_ref_child(context, arguments, 1U, &value)) {
            return r_body_diagnostic(body,
                                     "R-DIAG-TYPE-001",
                                     "R-FUNC-0004",
                                     "append requires a builder or string receiver and a value",
                                     span);
        }
        if (!r_overload_argument_type(body, value, 0U, false, &value_type)) {
            return false;
        }
        if (value_type == R_TYPE_ID_INVALID) {
            /* M24-5: a value whose type only its lowering determines (a generic call, ...) is
               lowered once here; the call below takes this result as its argument. */
            if (!r_body_lower_expression(body, value, R_TYPE_ID_INVALID, &prelowered)) {
                return false;
            }
            if (!prelowered.valid) {
                return true;
            }
            value_type = prelowered.type;
            body->prelowered_expression = value;
            body->prelowered_result = &prelowered;
        }
        kind = r_semantic_value_kind(context, value_type);
        RTypeId string_type;
        if (!r_semantic_intern_named_standard_type(context, "std.string::string", &string_type)) {
            body->prelowered_result = NULL;
            return false;
        }
        /* A string place appends through its str view (R-EXPR-0015, L41). */
        if (kind == R_SEMANTIC_TYPE_STR || kind == R_SEMANTIC_TYPE_CONSTEXPR_STR ||
            r_semantic_value_type(context, value_type) == string_type) {
            operation =
                formatting ? R_STANDARD_CALL_FORMAT_APPEND_STR : R_STANDARD_CALL_STRING_APPEND_STR;
        } else if (kind == R_SEMANTIC_TYPE_CHAR) {
            operation = formatting ? R_STANDARD_CALL_FORMAT_APPEND_CHAR
                                   : R_STANDARD_CALL_STRING_PUSH_SCALAR;
        } else if (formatting &&
                   r_semantic_checked_numeric_type_tag(context, value_type, &numeric_tag)) {
            const RTokenKind token = r_checked_numeric_destinations[numeric_tag].token_kind;
            if (r_semantic_type_is_integer(context, value_type) ||
                r_semantic_type_is_c_abi_integer(context, value_type)) {
                char spelling[96];
                (void)snprintf(spelling, sizeof(spelling), "append_%s", r_token_kind_name(token));
                const bool lowered =
                    r_body_lower_standard_format_integer_call(body,
                                                              arguments,
                                                              view,
                                                              span,
                                                              name.span,
                                                              expected,
                                                              token,
                                                              numeric_tag,
                                                              result) &&
                    r_standard_record_operation(body, "std.format", spelling, result);
                body->prelowered_result = NULL;
                return lowered;
            }
            switch (token) {
            case R_TOKEN_KW_F32:
                operation = R_STANDARD_CALL_FORMAT_APPEND_F32;
                break;
            case R_TOKEN_KW_F64:
                operation = R_STANDARD_CALL_FORMAT_APPEND_F64;
                break;
            case R_TOKEN_KW_C_FLOAT:
                operation = R_STANDARD_CALL_FORMAT_APPEND_C_FLOAT;
                break;
            case R_TOKEN_KW_C_DOUBLE:
                operation = R_STANDARD_CALL_FORMAT_APPEND_C_DOUBLE;
                break;
            case R_TOKEN_KW_C_LONG_DOUBLE:
                operation = R_STANDARD_CALL_FORMAT_APPEND_C_LONG_DOUBLE;
                break;
            default:
                break;
            }
        }
        if (operation == R_STANDARD_CALL_INVALID) {
            body->prelowered_result = NULL;
            return r_body_diagnostic(body,
                                     "R-DIAG-TYPE-001",
                                     "R-FUNC-0004",
                                     "no append overload accepts this value type",
                                     span);
        }
        const bool lowered = r_body_lower_standard_simple_call(
            body, arguments, view, span, name.span, expected, operation, result);
        body->prelowered_result = NULL;
        return lowered;
    }
    uint64_t math_operation;
    if (!r_standard_select_math(body, name_ref, arguments, view, span, &math_operation, matched)) {
        return false;
    }
    if (math_operation != 0U) {
        return r_body_lower_standard_math_call(
            body, arguments, view, span, span, expected, math_operation, result);
    }
    return true;
}

/* Interpret result schemas from the same inventory used by the runtime bindings.
 * This only interns types; argument evaluation and access checks stay in lowering. */
static bool r_standard_query_type(RFrontendContext *context,
                                  const char *name,
                                  size_t length,
                                  RTypeId owner,
                                  uint32_t depth,
                                  RTypeId *result) {
    const RSemanticType *source = r_semantic_type(context, owner);
    RTypeId base = R_TYPE_ID_INVALID;
    RTypeId second = R_TYPE_ID_INVALID;
    char spelling[256];
    size_t index;
    *result = R_TYPE_ID_INVALID;
    if (depth > context->options.limits.max_nesting || length >= sizeof(spelling)) {
        return true;
    }
    while (length != 0U && name[0] == ' ') {
        ++name;
        --length;
    }
    while (length != 0U && name[length - 1U] == ' ') {
        --length;
    }
    if (length == 1U &&
        (name[0] == 'T' || name[0] == 'K' || name[0] == 'V' || name[0] == 'S' || name[0] == 'R')) {
        if (source != NULL) {
            const bool has_type_base =
                source->kind == R_SEMANTIC_TYPE_BORROW || source->kind == R_SEMANTIC_TYPE_RAW ||
                source->kind == R_SEMANTIC_TYPE_OWN || source->kind == R_SEMANTIC_TYPE_ARC ||
                source->kind == R_SEMANTIC_TYPE_RC || source->kind == R_SEMANTIC_TYPE_WEAK ||
                source->kind == R_SEMANTIC_TYPE_FIXED_ARRAY ||
                source->kind == R_SEMANTIC_TYPE_SLICE || source->kind == R_SEMANTIC_TYPE_ARRAY ||
                source->kind == R_SEMANTIC_TYPE_LIST || source->kind == R_SEMANTIC_TYPE_DICT ||
                source->kind == R_SEMANTIC_TYPE_TASK || source->kind == R_SEMANTIC_TYPE_OPTION ||
                source->kind == R_SEMANTIC_TYPE_STANDARD;
            *result = has_type_base && source->base != 0U ? source->base : owner;
            if (name[0] == 'V') {
                *result = source->second;
            }
        }
        return true;
    }
    {
        static const struct {
            const char *prefix;
            RSemanticTypeKind kind;
        } owners[] = {{"own ", R_SEMANTIC_TYPE_OWN},
                      {"arc ", R_SEMANTIC_TYPE_ARC},
                      {"rc ", R_SEMANTIC_TYPE_RC},
                      {"weak ", R_SEMANTIC_TYPE_WEAK},
                      {"raw ", R_SEMANTIC_TYPE_RAW}};
        for (size_t owner_index = 0U; owner_index < sizeof(owners) / sizeof(owners[0]);
             ++owner_index) {
            const size_t prefix = strlen(owners[owner_index].prefix);
            if (length <= prefix || strncmp(name, owners[owner_index].prefix, prefix) != 0) {
                continue;
            }
            const char *pointee = name + prefix;
            size_t pointee_length = length - prefix;
            uint32_t flags = 0U;
            if (owners[owner_index].kind == R_SEMANTIC_TYPE_WEAK) {
                if (pointee_length > 3U && strncmp(pointee, "rc ", 3U) == 0) {
                    flags = R_SEMANTIC_TYPE_FLAG_RC_OWNER;
                    pointee += 3U;
                    pointee_length -= 3U;
                } else if (pointee_length > 4U && strncmp(pointee, "arc ", 4U) == 0) {
                    pointee += 4U;
                    pointee_length -= 4U;
                }
            }
            if (owners[owner_index].kind == R_SEMANTIC_TYPE_RAW ||
                owners[owner_index].kind == R_SEMANTIC_TYPE_OWN) {
                if (pointee_length > 6U && strncmp(pointee, "const ", 6U) == 0) {
                    flags = R_SEMANTIC_TYPE_FLAG_SHARED;
                    pointee += 6U;
                    pointee_length -= 6U;
                }
                if (pointee_length != 0U && pointee[pointee_length - 1U] == '*') {
                    --pointee_length;
                }
                if (pointee_length > 2U && pointee[0] == '(' &&
                    pointee[pointee_length - 1U] == ')') {
                    ++pointee;
                    pointee_length -= 2U;
                }
            }
            if (!r_standard_query_type(
                    context, pointee, pointee_length, owner, depth + 1U, &base)) {
                return false;
            }
            return base == 0U ||
                   r_semantic_intern_derived_type(
                       context, owners[owner_index].kind, base, 0U, 0U, flags, result);
        }
    }
    if (length > 1U && name[length - 1U] == '*') {
        uint32_t flags = 0U;
        if (length > 6U && strncmp(name, "const ", 6U) == 0) {
            flags = R_SEMANTIC_TYPE_FLAG_SHARED;
            name += 6U;
            length -= 6U;
        }
        if (!r_standard_query_type(context, name, length - 1U, owner, depth + 1U, &base)) {
            return false;
        }
        return base == 0U || r_semantic_intern_derived_type(
                                 context, R_SEMANTIC_TYPE_BORROW, base, 0U, 0U, flags, result);
    }
    if (length > 2U && name[length - 2U] == '[' && name[length - 1U] == ']') {
        uint32_t flags = 0U;
        if (length > 6U && strncmp(name, "const ", 6U) == 0) {
            flags = R_SEMANTIC_TYPE_FLAG_SHARED;
            name += 6U;
            length -= 6U;
        }
        if (!r_standard_query_type(context, name, length - 2U, owner, depth + 1U, &base)) {
            return false;
        }
        return base == 0U || r_semantic_intern_derived_type(
                                 context, R_SEMANTIC_TYPE_SLICE, base, 0U, 0U, flags, result);
    }
    for (index = 0U; index < length; ++index) {
        /* R-TYPE-0031: `name<arguments>` applies a standard type constructor. */
        if (name[index] == '<' && name[length - 1U] == '>') {
            const size_t prefix = index;
            size_t comma = length - 1U;
            uint32_t nesting = 0U;
            RSemanticTypeKind kind = R_SEMANTIC_TYPE_STANDARD;
            for (++index; index + 1U < length; ++index) {
                if (name[index] == '<' || name[index] == '(') {
                    ++nesting;
                } else if (name[index] == '>' || name[index] == ')') {
                    --nesting;
                } else if (name[index] == ',' && nesting == 0U) {
                    comma = index;
                    break;
                }
            }
            if (!r_standard_query_type(
                    context, name + prefix + 1U, comma - prefix - 1U, owner, depth + 1U, &base) ||
                (comma + 1U < length && !r_standard_query_type(context,
                                                               name + comma + 1U,
                                                               length - comma - 2U,
                                                               owner,
                                                               depth + 1U,
                                                               &second))) {
                return false;
            }
            if (base == 0U) {
                return true;
            }
            memcpy(spelling, name, prefix);
            spelling[prefix] = '\0';
            if (strcmp(spelling, "o") == 0) {
                kind = R_SEMANTIC_TYPE_OPTION;
            } else if (strcmp(spelling, "array") == 0) {
                kind = R_SEMANTIC_TYPE_ARRAY;
            } else if (strcmp(spelling, "list") == 0) {
                kind = R_SEMANTIC_TYPE_LIST;
            } else if (strcmp(spelling, "dict") == 0) {
                kind = R_SEMANTIC_TYPE_DICT;
            }
            if (kind != R_SEMANTIC_TYPE_STANDARD) {
                return r_semantic_intern_derived_type(context, kind, base, second, 0U, 0U, result);
            }
            return r_semantic_intern_parametric_standard_type(
                context, spelling, base, second, result);
        }
    }
    memcpy(spelling, name, length);
    spelling[length] = '\0';
    if (strcmp(spelling, "complex_f32") == 0 || strcmp(spelling, "complex_f64") == 0) {
        return r_body_standard_math_type(context, spelling, result);
    }
    if (strcmp(spelling, "bytes") == 0) {
        return r_semantic_bytes_type(context, result);
    }
    if (strcmp(spelling, "constexpr str") == 0) {
        return r_semantic_intern_type(context, R_SEMANTIC_TYPE_CONSTEXPR_STR, 0U, result);
    }
    for (index = R_TOKEN_KW_BOOL; index <= R_TOKEN_KW_VOID; ++index) {
        if (strcmp(spelling, r_token_kind_name((RTokenKind)index)) == 0) {
            return r_semantic_type_from_token(context, (RTokenKind)index, result);
        }
    }
    if (strncmp(spelling, "std.", 4U) == 0) {
        return r_semantic_intern_named_standard_type(context, spelling, result);
    }
    return true;
}

static bool r_standard_query_call(RBodyContext *body,
                                  RAstRef primary,
                                  RAstRef suffix,
                                  RTypeId receiver,
                                  bool method_call,
                                  RTypeId *result) {
    RFrontendContext *context = body->frontend;
    RAstRef name_ref = primary;
    RAstRef arguments;
    RAstNodeView view;
    RAstTokenView token;
    char operation[256] = {0};
    RTypeId owner = r_semantic_value_type(context, receiver);
    size_t index;
    *result = R_TYPE_ID_INVALID;
    if (!r_ast_find_direct_child(context, suffix, R_SYNTAX_ARGUMENT_LIST, &arguments) ||
        !r_ast_ref_view(context, arguments, &view)) {
        return true;
    }
    if (method_call) {
        const RSemanticType *type = r_semantic_type(context, owner);
        if (r_ast_has_direct_token(context, suffix, R_TOKEN_ARROW) && type != NULL) {
            owner = type->base;
            type = r_semantic_type(context, owner);
        }
        if (!r_semantic_first_identifier(context, suffix, &token)) {
            return true;
        }
        for (index = 0U; index < sizeof(r_standard_methods) / sizeof(r_standard_methods[0]);
             ++index) {
            const RStandardMethodDescriptor *method = &r_standard_methods[index];
            if (r_standard_method_owner(context, type, method->owner) &&
                r_semantic_token_text_equal_owned(context, &token, method->name)) {
                (void)snprintf(
                    operation, sizeof(operation), "std.%s::%s", method->module, method->operation);
                break;
            }
        }
    } else {
        RAstRef argument;
        RAstNodeView qualified;
        if ((r_ast_ref_view(context, primary, &qualified) &&
             qualified.kind == R_SYNTAX_QUALIFIED_NAME) ||
            r_ast_find_direct_child(context, primary, R_SYNTAX_QUALIFIED_NAME, &name_ref)) {
            if (!r_ast_ref_view(context, name_ref, &qualified)) {
                return false;
            }
            size_t length = 0U;
            for (uint32_t component = 0U; component < qualified.child_count; ++component) {
                RAstRef part;
                RAstTextView text;
                if (!r_ast_ref_child(context, name_ref, component, &part) ||
                    !r_ast_ref_text(context, part, &text)) {
                    return true;
                }
                const char *separator = component == 0U                           ? ""
                                        : component + 1U == qualified.child_count ? "::"
                                                                                  : ".";
                const size_t separator_length = strlen(separator);
                if (length + separator_length + text.length >= sizeof(operation)) {
                    return true;
                }
                memcpy(operation + length, separator, separator_length);
                length += separator_length;
                memcpy(operation + length, text.bytes, text.length);
                length += text.length;
            }
        } else if (r_body_ast_first_token(context, primary, &token)) {
            if (r_semantic_token_text_equal_owned(context, &token, "len") ||
                token.kind == R_TOKEN_KW_SIZEOF || token.kind == R_TOKEN_KW_ALIGNOF) {
                return r_semantic_type_from_token(context, R_TOKEN_KW_USIZE, result);
            }
        }
        if (view.child_count != 0U && r_ast_ref_child(context, arguments, 0U, &argument)) {
            if (!r_overload_argument_type(body, argument, 0U, false, &owner)) {
                return false;
            }
            const RSemanticType *type =
                r_semantic_type(context, r_semantic_value_type(context, owner));
            if (type != NULL && type->kind == R_SEMANTIC_TYPE_BORROW) {
                owner = type->base;
            }
        }
    }
    if (!method_call) {
        bool owner_known;
        const RStandardMethodDescriptor *type_method =
            r_standard_type_method(context, name_ref, &owner_known);
        if (type_method != NULL) {
            (void)snprintf(operation,
                           sizeof(operation),
                           "std.%s::%s",
                           type_method->module,
                           type_method->operation);
        }
        for (index = 0U; index < sizeof(r_standard_methods) / sizeof(r_standard_methods[0]);
             ++index) {
            const RStandardMethodDescriptor *method = &r_standard_methods[index];
            char alias[256];
            (void)snprintf(alias, sizeof(alias), "std.%s::%s", method->module, method->name);
            if (strcmp(operation, alias) == 0 &&
                r_standard_method_owner(context, r_semantic_type(context, owner), method->owner)) {
                (void)snprintf(
                    operation, sizeof(operation), "std.%s::%s", method->module, method->operation);
                break;
            }
        }
        uint64_t math_operation;
        bool matched;
        if (!r_standard_select_math(
                body, name_ref, arguments, &view, view.span, &math_operation, &matched)) {
            return false;
        }
        if (matched) {
            return math_operation == 0U ||
                   r_body_standard_math_type(
                       context, r_standard_math_operation(math_operation)->result_type, result);
        }
    }
    if (!method_call && view.child_count == 2U && strncmp(operation, "core::", 6U) == 0) {
        static const char *const prefixes[] = {"checked_", "wrapping_", "saturating_"};
        static const char *const operators[] = {"add", "sub", "mul"};
        for (size_t mode = 0U; mode < 3U; ++mode) {
            for (size_t op = 0U; op < 3U; ++op) {
                char name[64];
                (void)snprintf(name, sizeof(name), "core::%s%s", prefixes[mode], operators[op]);
                if (strcmp(operation, name) != 0) {
                    continue;
                }
                RAstRef argument;
                RTypeId other;
                if (!r_ast_ref_child(context, arguments, 1U, &argument) ||
                    !r_overload_argument_type(body, argument, 0U, false, &other) ||
                    !r_standard_select_integer(body, owner, other, view.span, result)) {
                    return false;
                }
                return *result == 0U || mode != 0U ||
                       r_semantic_intern_derived_type(
                           context, R_SEMANTIC_TYPE_OPTION, *result, 0U, 0U, 0U, result);
            }
        }
    }
    for (index = 0U; index < sizeof(r_standard_results) / sizeof(r_standard_results[0]); ++index) {
        if (strcmp(operation, r_standard_results[index].operation) == 0) {
            return r_standard_query_type(context,
                                         r_standard_results[index].result,
                                         strlen(r_standard_results[index].result),
                                         owner,
                                         0U,
                                         result);
        }
    }
    /* M24-5 (R-FUNC-0004): a call of an ordinary R function or method, neither generic, async
       nor overloaded, has its declared result, so that an overload can select by it. */
    {
        const RSemanticSymbol *callee = NULL;
        if (method_call) {
            RAstTokenView method_name;
            bool ambiguous = false;
            if (r_semantic_first_identifier(context, suffix, &method_name)) {
                const RSemanticMethod *method =
                    r_semantic_find_method(context, owner, method_name.intern_id, &ambiguous);
                if ((method != NULL) && !ambiguous && (method->function != R_SYMBOL_ID_INVALID)) {
                    callee = &context->semantic_symbols[method->function - 1U];
                }
            }
        } else {
            RBodyLookupIssue issue;
            RAstNodeView name_view;
            RAstTokenView name;
            RSymbolId symbol_id = R_SYMBOL_ID_INVALID;
            if (r_ast_ref_view(context, name_ref, &name_view) &&
                (name_view.kind == R_SYNTAX_QUALIFIED_NAME)) {
                symbol_id = r_body_resolve_qualified_name(body, name_ref, &name, &issue);
                if (symbol_id == R_SYMBOL_ID_INVALID) {
                    symbol_id = r_generic_associated_method_quiet(body, name_ref);
                }
            } else if ((r_ast_ref_token(context, primary, &name) ||
                        (r_ast_ref_view(context, primary, &name_view) &&
                         (name_view.kind == R_SYNTAX_PRIMARY_EXPRESSION) &&
                         (name_view.child_count == 1U) &&
                         r_body_ast_first_token(context, primary, &name))) &&
                       (name.kind == R_TOKEN_IDENTIFIER)) {
                symbol_id = r_body_resolve_name(body, &name, &issue);
            }
            callee = r_body_symbol(body, symbol_id);
        }
        if ((callee != NULL) && (callee->kind == R_SEMANTIC_SYMBOL_FUNCTION) &&
            !callee->is_async && (callee->generic_schema == 0U) && !callee->is_overloaded) {
            *result = callee->return_type;
        }
    }
    return true;
}

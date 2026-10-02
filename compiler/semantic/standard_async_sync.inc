/* R-SLIB-ASYNC-0013..0016 and R-LIB-0016 (L30): asynchronous locks, semaphore, notify, broadcast
 * and channel reservations, lowered from the descriptors of compiler/source/standard_async_sync.h.
 * Every call becomes one R_HIR_STANDARD_CALL whose runtime_type is the protected or carried value
 * type; auxiliary_type is the effect carrier of an allocating call, the start carrier of a task
 * start, and the value type otherwise. */

/* `std.async::name` or `std.sync::name` of one described operation. */
static bool r_body_standard_async_sync_operation(const RFrontendContext *context,
                                                 RAstRef qualified,
                                                 const RAsyncSyncDescriptor **descriptor,
                                                 RSourceSpan *operation_span) {
    RAstNodeView view;
    RAstTokenView components[3];
    uint32_t component_count = UINT32_C(0);
    const char *module;

    *descriptor = NULL;
    if (!r_ast_view_kind(context, qualified, R_SYNTAX_QUALIFIED_NAME, &view)) {
        return false;
    }
    (void)memset(components, 0, sizeof(components));
    for (uint32_t child_index = UINT32_C(0); child_index < view.child_count; ++child_index) {
        RAstRef child;
        RAstTokenView token;

        if (!r_ast_ref_child(context, qualified, child_index, &child) ||
            !r_ast_ref_token(context, child, &token)) {
            continue;
        }
        if (component_count >= UINT32_C(3)) {
            return false;
        }
        components[component_count] = token;
        component_count += UINT32_C(1);
    }
    if ((component_count != UINT32_C(3)) ||
        !r_semantic_token_text_equal_owned(context, &components[0], "std")) {
        return false;
    }
    if (r_semantic_token_text_equal_owned(context, &components[1], "async")) {
        module = "async";
    } else if (r_semantic_token_text_equal_owned(context, &components[1], "sync")) {
        module = "sync";
    } else if (r_semantic_token_text_equal_owned(context, &components[1], "test")) {
        module = "test";
    } else {
        return false;
    }
    for (RStandardCallOperation operation = R_STANDARD_CALL_ASYNC_MUTEX_NEW;
         operation <= R_STANDARD_CALL_SYNC_SEND_PERMIT;
         operation = (RStandardCallOperation)(operation + 1)) {
        const RAsyncSyncDescriptor *candidate = r_async_sync_descriptor(operation);
        if ((candidate != NULL) && (candidate->shape != R_ASYNC_SYNC_CONSTRUCT_TYPED) &&
            (strcmp(candidate->module, module) == 0) &&
            r_semantic_token_text_equal_owned(context, &components[2], candidate->name)) {
            *descriptor = candidate;
            break;
        }
    }
    if (*descriptor == NULL) {
        return false;
    }
    if (operation_span != NULL) {
        *operation_span = components[2].span;
    }
    return true;
}

/* `std.async::broadcast`, the constructor of R-SLIB-ASYNC-0016 with a type operand. */
static bool r_body_standard_async_broadcast_name(const RFrontendContext *context,
                                                 RAstRef qualified) {
    RAstNodeView view;
    RAstTokenView components[3];
    uint32_t component_count = UINT32_C(0);

    if (!r_ast_view_kind(context, qualified, R_SYNTAX_QUALIFIED_NAME, &view)) {
        return false;
    }
    (void)memset(components, 0, sizeof(components));
    for (uint32_t child_index = UINT32_C(0); child_index < view.child_count; ++child_index) {
        RAstRef child;
        RAstTokenView token;

        if (!r_ast_ref_child(context, qualified, child_index, &child) ||
            !r_ast_ref_token(context, child, &token)) {
            continue;
        }
        if (component_count >= UINT32_C(3)) {
            return false;
        }
        components[component_count] = token;
        component_count += UINT32_C(1);
    }
    return (component_count == UINT32_C(3)) &&
           r_semantic_token_text_equal_owned(context, &components[0], "std") &&
           r_semantic_token_text_equal_owned(context, &components[1], "async") &&
           r_semantic_token_text_equal_owned(context, &components[2], "broadcast");
}

/* A clone of the element that runs no clone hook, so a receive may clone it anywhere. */
static bool r_async_sync_no_hook(void *user, RSymbolId hook) {
    (void)user;
    (void)hook;
    return false;
}

/* The element of a protected value: owned, storable and free of views. */
static bool r_async_sync_storable(RBodyContext *body, RTypeId element) {
    return (r_semantic_value_kind(body->frontend, element) != R_SEMANTIC_TYPE_VOID) &&
           !r_semantic_type_contains_borrow(body->frontend, element) &&
           !r_semantic_type_contains_slice(body->frontend, element) &&
           (r_semantic_type_is_copy(body->frontend, element) ||
            r_semantic_type_is_supported_move(body->frontend, element));
}

/* The standard type `name`, applied to `element` when the schema is generic. */
static bool r_async_sync_type(RBodyContext *body, const char *name, RTypeId element, RTypeId *out) {
    return element == R_TYPE_ID_INVALID
               ? r_semantic_intern_named_standard_type(body->frontend, name, out)
               : r_semantic_intern_parametric_standard_type(
                     body->frontend, name, element, R_TYPE_ID_INVALID, out);
}

/* The borrowed resource of the first operand: a borrow of `subject` with the needed access. */
static bool r_async_sync_resource(RBodyContext *body,
                                  const RAsyncSyncDescriptor *descriptor,
                                  const RExpressionResult *source,
                                  bool exclusive,
                                  RSourceSpan span,
                                  const RSemanticType **resource,
                                  bool *valid) {
    const RSemanticType *borrow =
        r_semantic_type(body->frontend, r_semantic_value_type(body->frontend, source->type));

    *resource =
        borrow != NULL && borrow->kind == R_SEMANTIC_TYPE_BORROW
            ? r_semantic_type(body->frontend, r_semantic_value_type(body->frontend, borrow->base))
            : NULL;
    *valid = (*resource != NULL) &&
             r_semantic_standard_type_name_equal(body->frontend, *resource, descriptor->subject) &&
             ((borrow->flags & R_SEMANTIC_TYPE_FLAG_NULLABLE) == 0U) &&
             (!exclusive || ((borrow->flags & R_SEMANTIC_TYPE_FLAG_SHARED) == 0U));
    if (*valid) {
        return true;
    }
    return r_body_diagnostic(body,
                             "R-DIAG-TYPE-001",
                             descriptor->rule,
                             exclusive ? "the operation requires an exclusive borrow of its guard"
                                       : "the operation requires a borrow of its matching resource",
                             span);
}

static bool r_async_sync_guard_type(const RFrontendContext *context, const RSemanticType *type) {
    return (type != NULL) &&
           (r_semantic_standard_type_name_equal(context, type, "std.async::mutex_guard") ||
            r_semantic_standard_type_name_equal(context, type, "std.async::rw_read_guard") ||
            r_semantic_standard_type_name_equal(context, type, "std.async::rw_write_guard"));
}

static bool r_body_lower_standard_async_sync_call(RBodyContext *body,
                                                  RAstRef argument_list,
                                                  const RAstNodeView *arguments_view,
                                                  RSourceSpan call_span,
                                                  RSourceSpan operation_span,
                                                  RTypeId contextual_type,
                                                  const RAsyncSyncDescriptor *descriptor,
                                                  RExpressionResult *result) {
    const RAsyncSyncShape shape = descriptor->shape;
    const bool borrows = (shape != R_ASYNC_SYNC_CONSTRUCT_VALUE) &&
                         (shape != R_ASYNC_SYNC_CONSTRUCT_COUNT) &&
                         (shape != R_ASYNC_SYNC_CONSTRUCT_EMPTY) &&
                         (shape != R_ASYNC_SYNC_CONSUME) && (shape != R_ASYNC_SYNC_SEND_PERMIT) &&
                         (shape != R_ASYNC_SYNC_QUERY_U64) && (shape != R_ASYNC_SYNC_SET_U64);
    const uint32_t expected_count =
        ((shape == R_ASYNC_SYNC_CONSTRUCT_EMPTY) || (shape == R_ASYNC_SYNC_QUERY_U64)) ? 0U
        : ((shape == R_ASYNC_SYNC_PUBLISH) || (shape == R_ASYNC_SYNC_HANDLE_COUNT) ||
           (shape == R_ASYNC_SYNC_SEND_PERMIT))
            ? 2U
            : 1U;
    RExpressionResult first;
    RExpressionResult second;
    const RSemanticType *resource = NULL;
    RTypeId element = R_TYPE_ID_INVALID;
    RTypeId output = R_TYPE_ID_INVALID;
    RTypeId effect_set = R_TYPE_ID_INVALID;
    RTypeId carrier = R_TYPE_ID_INVALID;
    RTypeId usize_type;
    RHirNodeId operands[2];
    RHirVector children = {operands, expected_count, expected_count};
    RHirNodeId id;
    RHirNode *node;
    RAstRef reference;
    bool valid = false;

    r_body_expression_invalid(result);
    r_body_expression_invalid(&first);
    r_body_expression_invalid(&second);
    if (arguments_view->child_count != expected_count) {
        return r_body_diagnostic(body,
                                 "R-DIAG-TYPE-001",
                                 descriptor->rule,
                                 "the operation has an incorrect argument count",
                                 call_span);
    }
    if (!r_semantic_type_from_token(body->frontend, R_TOKEN_KW_USIZE, &usize_type)) {
        return false;
    }
    /* The first operand: a borrow of the resource, a consumed guard or permit, or the value or
       count of a constructor. */
    if (expected_count != 0U) {
        const bool exclusive = shape == R_ASYNC_SYNC_ACCESS_MUT;
        if (!r_ast_ref_child(body->frontend, argument_list, 0U, &reference)) {
            return false;
        }
        if (borrows) {
            if (!(exclusive ? r_body_lower_resource_argument(body,
                                                             reference,
                                                             false,
                                                             descriptor->rule,
                                                             "a mutable guard operand shall borrow "
                                                             "a named place",
                                                             &first)
                            : r_body_lower_sync_resource_argument(body, reference, &first))) {
                return false;
            }
            if (!first.valid) {
                return true;
            }
            if (!r_async_sync_resource(
                    body, descriptor, &first, exclusive, call_span, &resource, &valid)) {
                return false;
            }
            if (!valid) {
                return true;
            }
            element = resource->base;
        } else if (shape == R_ASYNC_SYNC_CONSTRUCT_VALUE) {
            RTypeId expected = R_TYPE_ID_INVALID;
            (void)r_body_standard_operand_from_context(body,
                                                       contextual_type,
                                                       R_SEMANTIC_TYPE_STANDARD,
                                                       descriptor->subject,
                                                       0U,
                                                       &expected);
            if (!r_body_lower_value(body, reference, expected, &first)) {
                return false;
            }
            if (!first.valid) {
                return true;
            }
            element = r_semantic_value_type(body->frontend, first.type);
            if (!r_async_sync_storable(body, element)) {
                return r_body_diagnostic(body,
                                         "R-DIAG-TYPE-001",
                                         descriptor->rule,
                                         "the protected value requires an owned storable type",
                                         call_span);
            }
        } else if ((shape == R_ASYNC_SYNC_CONSTRUCT_COUNT) || (shape == R_ASYNC_SYNC_SET_U64)) {
            RTypeId count_type = usize_type;
            if ((shape == R_ASYNC_SYNC_SET_U64) &&
                !r_semantic_type_from_token(body->frontend, R_TOKEN_KW_U64, &count_type)) {
                return false;
            }
            if (!r_body_lower_value(body, reference, count_type, &first)) {
                return false;
            }
            if (!first.valid) {
                return true;
            }
            if (r_semantic_value_type(body->frontend, first.type) != count_type) {
                return r_body_diagnostic(body,
                                         "R-DIAG-TYPE-001",
                                         descriptor->rule,
                                         shape == R_ASYNC_SYNC_SET_U64 ? "the operand shall be u64"
                                                                       : "the count shall be usize",
                                         call_span);
            }
        } else {
            const RSemanticType *consumed;
            if (!r_body_lower_value(body, reference, R_TYPE_ID_INVALID, &first)) {
                return false;
            }
            if (!first.valid) {
                return true;
            }
            consumed =
                r_semantic_type(body->frontend, r_semantic_value_type(body->frontend, first.type));
            valid =
                (descriptor->subject == NULL)
                    ? r_async_sync_guard_type(body->frontend, consumed)
                    : ((consumed != NULL) && r_semantic_standard_type_name_equal(
                                                 body->frontend, consumed, descriptor->subject));
            if (!valid || (r_body_hir_node(body, first.node) == NULL) ||
                ((r_body_hir_node(body, first.node)->kind != R_HIR_MOVE) && first.is_place)) {
                return r_body_diagnostic(body,
                                         "R-DIAG-MOVE-001",
                                         descriptor->rule,
                                         descriptor->subject == NULL
                                             ? "std.async::unlock consumes a lock guard through "
                                               "move"
                                             : "the operation consumes its operand through move",
                                         call_span);
            }
            element = consumed->base;
        }
    }
    /* The second operand: a published or sent value, or a count of permits. */
    if (expected_count == 2U) {
        const RTypeId expected = shape == R_ASYNC_SYNC_HANDLE_COUNT ? usize_type : element;
        if (!r_ast_ref_child(body->frontend, argument_list, 1U, &reference) ||
            !r_body_lower_value(body, reference, expected, &second)) {
            return false;
        }
        if (!second.valid) {
            return true;
        }
        if (r_semantic_value_type(body->frontend, second.type) !=
            r_semantic_value_type(body->frontend, expected)) {
            return r_body_diagnostic(body,
                                     "R-DIAG-TYPE-001",
                                     descriptor->rule,
                                     shape == R_ASYNC_SYNC_HANDLE_COUNT
                                         ? "the count shall be usize"
                                         : "the value does not match the element type",
                                     call_span);
        }
    }
    /* Asynchronous results cross threads and suspensions. */
    if ((shape == R_ASYNC_SYNC_START) || (shape == R_ASYNC_SYNC_START_OUTCOME)) {
        if ((element != R_TYPE_ID_INVALID) &&
            (!r_semantic_type_is_send(body->frontend, element) ||
             r_semantic_type_crosses_await(body->frontend, element))) {
            return r_body_diagnostic(body,
                                     "R-DIAG-ASYNC-001",
                                     descriptor->rule,
                                     "an asynchronous result requires a Send value without a "
                                     "borrow, slice or runtime str",
                                     call_span);
        }
    }
    switch (shape) {
    case R_ASYNC_SYNC_CONSTRUCT_VALUE:
    case R_ASYNC_SYNC_CLONE:
        if (!r_async_sync_type(body, descriptor->subject, element, &output)) {
            return false;
        }
        break;
    case R_ASYNC_SYNC_CONSTRUCT_COUNT:
    case R_ASYNC_SYNC_CONSTRUCT_EMPTY:
        if (!r_semantic_intern_named_standard_type(body->frontend, descriptor->subject, &output)) {
            return false;
        }
        break;
    case R_ASYNC_SYNC_SUBSCRIBE:
    case R_ASYNC_SYNC_TRY_OUTCOME:
        if (!r_async_sync_type(body, descriptor->result, element, &output)) {
            return false;
        }
        break;
    case R_ASYNC_SYNC_TRY: {
        RTypeId payload;
        if (!r_async_sync_type(body, descriptor->result, element, &payload) ||
            !r_semantic_intern_type(body->frontend, R_SEMANTIC_TYPE_OPTION, payload, &output)) {
            return false;
        }
        break;
    }
    case R_ASYNC_SYNC_ACCESS_REF:
    case R_ASYNC_SYNC_ACCESS_MUT:
        if (!r_semantic_intern_derived_type(body->frontend,
                                            R_SEMANTIC_TYPE_BORROW,
                                            element,
                                            R_TYPE_ID_INVALID,
                                            UINT64_C(0),
                                            shape == R_ASYNC_SYNC_ACCESS_REF
                                                ? R_SEMANTIC_TYPE_FLAG_SHARED
                                                : R_SEMANTIC_TYPE_FLAG_NONE,
                                            &output)) {
            return false;
        }
        break;
    case R_ASYNC_SYNC_CONSUME:
    case R_ASYNC_SYNC_HANDLE_VOID:
    case R_ASYNC_SYNC_HANDLE_COUNT:
    case R_ASYNC_SYNC_SEND_PERMIT:
    case R_ASYNC_SYNC_SET_U64:
        if (!r_semantic_type_from_token(body->frontend, R_TOKEN_KW_VOID, &output)) {
            return false;
        }
        break;
    case R_ASYNC_SYNC_PUBLISH:
    case R_ASYNC_SYNC_HANDLE_USIZE:
        output = usize_type;
        break;
    case R_ASYNC_SYNC_QUERY_U64:
        if (!r_semantic_type_from_token(body->frontend, R_TOKEN_KW_U64, &output)) {
            return false;
        }
        break;
    case R_ASYNC_SYNC_START:
    case R_ASYNC_SYNC_START_VOID:
    case R_ASYNC_SYNC_START_OUTCOME: {
        RTypeId logical;
        if (shape == R_ASYNC_SYNC_START_VOID) {
            if (!r_semantic_type_from_token(body->frontend, R_TOKEN_KW_VOID, &logical)) {
                return false;
            }
        } else if (!r_async_sync_type(body, descriptor->result, element, &logical)) {
            return false;
        }
        if (!r_semantic_checked_async_types(
                body->frontend, logical, R_TYPE_ID_INVALID, &output, &carrier) ||
            !r_body_standard_async_start_effects(body, carrier, operation_span, &effect_set)) {
            return false;
        }
        break;
    }
    case R_ASYNC_SYNC_CONSTRUCT_TYPED:
        body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    if (r_async_sync_allocates(descriptor)) {
        RTypeId alloc_error;
        if (!r_semantic_intern_named_standard_type(
                body->frontend, "std.alloc::alloc_error", &alloc_error) ||
            !r_semantic_single_effect_set(body->frontend, alloc_error, &effect_set) ||
            !r_semantic_intern_effect_carrier(body->frontend, output, effect_set, &carrier) ||
            !r_body_validate_effect_set(body, effect_set, operation_span)) {
            return false;
        }
    }
    if ((contextual_type != R_TYPE_ID_INVALID) &&
        (r_semantic_value_type(body->frontend, contextual_type) != output)) {
        return r_body_diagnostic(body,
                                 "R-DIAG-TYPE-001",
                                 descriptor->rule,
                                 "the operation result does not match its destination type",
                                 operation_span);
    }
    if (expected_count >= 1U) {
        operands[0] = first.node;
    }
    if (expected_count == 2U) {
        operands[1] = second.node;
    }
    if (!r_body_append_node(body,
                            R_HIR_STANDARD_CALL,
                            call_span,
                            output,
                            R_SYMBOL_ID_INVALID,
                            R_TOKEN_INVALID,
                            UINT64_C(0),
                            false,
                            expected_count == 0U ? NULL : &children,
                            &id)) {
        return false;
    }
    node = r_body_hir_node(body, id);
    node->standard_operation = descriptor->operation;
    node->auxiliary_type = carrier != R_TYPE_ID_INVALID   ? carrier
                           : element != R_TYPE_ID_INVALID ? element
                                                          : output;
    node->runtime_type = element;
    if ((shape == R_ASYNC_SYNC_ACCESS_REF) || (shape == R_ASYNC_SYNC_ACCESS_MUT)) {
        /* The value borrow is bounded by the borrow of the guard (R-SLIB-ASYNC-0013). */
        node->borrow_origin = first.symbol;
        node->borrow_referent = first.borrow_referent;
        node->borrow_origin_set = UINT32_C(0);
        node->borrow_origin_multiple = false;
    }
    result->node = id;
    result->type = output;
    result->borrow_origin = node->borrow_origin;
    result->borrow_referent = node->borrow_referent;
    result->borrow_origin_set = node->borrow_origin_set;
    result->borrow_origin_multiple = node->borrow_origin_multiple;
    result->valid = true;
    return effect_set == R_TYPE_ID_INVALID ||
           r_body_attach_effect_exits(body, id, effect_set, operation_span);
}

/* R-SLIB-ASYNC-0016: `std.async::broadcast::<T>(capacity)`. */
static bool r_body_lower_standard_async_broadcast_type_call(RBodyContext *body,
                                                            RAstRef call,
                                                            RTypeId contextual_type,
                                                            RExpressionResult *result) {
    const RAsyncSyncDescriptor *descriptor =
        r_async_sync_descriptor(R_STANDARD_CALL_ASYNC_BROADCAST);
    RAstNodeView view;
    RAstRef element_ref = {0};
    RAstRef capacity_ref = {0};
    RTypeId element = R_TYPE_ID_INVALID;
    RTypeId output;
    RTypeId alloc_error;
    RTypeId effect_set;
    RTypeId carrier;
    RTypeId usize_type;
    RExpressionResult capacity;
    RHirNodeId operands[1];
    RHirVector children = {operands, 1U, 1U};
    RHirNodeId id;
    RHirNode *node;

    r_body_expression_invalid(result);
    r_body_expression_invalid(&capacity);
    if (!r_ast_view_kind(body->frontend, call, R_SYNTAX_STANDARD_TYPE_CALL, &view)) {
        body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    for (uint32_t child_index = UINT32_C(0); child_index < view.child_count; ++child_index) {
        RAstRef child;
        RAstNodeView child_view;

        if (!r_ast_ref_child(body->frontend, call, child_index, &child) ||
            !r_ast_ref_view(body->frontend, child, &child_view)) {
            continue;
        }
        if (child_view.kind == R_SYNTAX_TYPE) {
            element_ref = child;
        } else if (child_view.kind == R_SYNTAX_EXPRESSION) {
            capacity_ref = child;
        }
    }
    if (capacity_ref.node == R_AST_NODE_ID_INVALID) {
        return r_body_diagnostic(body,
                                 "R-DIAG-TYPE-001",
                                 descriptor->rule,
                                 "a broadcast constructor takes its capacity",
                                 view.span);
    }
    if (element_ref.node != R_AST_NODE_ID_INVALID) {
        if (!r_semantic_type_from_ast(body->frontend, element_ref, &element)) {
            return false;
        }
    } else if (!r_body_standard_operand_from_context(body,
                                                     contextual_type,
                                                     R_SEMANTIC_TYPE_STANDARD,
                                                     descriptor->subject,
                                                     UINT32_C(0),
                                                     &element)) {
        return r_body_missing_standard_operands(body,
                                                "a broadcast constructor takes its element type "
                                                "from ::<T> or from an expected broadcast type",
                                                view.span);
    }
    element = r_semantic_value_type(body->frontend, element);
    if (!r_async_sync_storable(body, element) ||
        !r_semantic_type_is_send(body->frontend, element) ||
        !r_clone_capability(body->frontend, element, 0U) ||
        !r_clone_visit_hooks(body->frontend, element, 0U, r_async_sync_no_hook, NULL)) {
        return r_body_diagnostic(body,
                                 "R-DIAG-TYPE-001",
                                 descriptor->rule,
                                 "a broadcast element requires a Send, unborrowed type cloned "
                                 "structurally without a clone hook",
                                 view.span);
    }
    if (!r_semantic_type_from_token(body->frontend, R_TOKEN_KW_USIZE, &usize_type) ||
        !r_async_sync_type(body, descriptor->subject, element, &output) ||
        !r_semantic_intern_named_standard_type(
            body->frontend, "std.alloc::alloc_error", &alloc_error) ||
        !r_semantic_single_effect_set(body->frontend, alloc_error, &effect_set) ||
        !r_semantic_intern_effect_carrier(body->frontend, output, effect_set, &carrier) ||
        !r_body_validate_effect_set(body, effect_set, view.span)) {
        return false;
    }
    if ((contextual_type != R_TYPE_ID_INVALID) &&
        (r_semantic_value_type(body->frontend, contextual_type) != output)) {
        return r_body_diagnostic(body,
                                 "R-DIAG-TYPE-001",
                                 descriptor->rule,
                                 "broadcast constructor result does not match its destination type",
                                 view.span);
    }
    if (!r_body_lower_value(body, capacity_ref, usize_type, &capacity)) {
        return false;
    }
    if (!capacity.valid) {
        return true;
    }
    if (r_semantic_value_type(body->frontend, capacity.type) != usize_type) {
        return r_body_diagnostic(
            body, "R-DIAG-TYPE-001", descriptor->rule, "the capacity shall be usize", view.span);
    }
    operands[0] = capacity.node;
    if (!r_body_append_node(body,
                            R_HIR_STANDARD_CALL,
                            view.span,
                            output,
                            R_SYMBOL_ID_INVALID,
                            R_TOKEN_INVALID,
                            UINT64_C(0),
                            false,
                            &children,
                            &id)) {
        return false;
    }
    node = r_body_hir_node(body, id);
    node->standard_operation = R_STANDARD_CALL_ASYNC_BROADCAST;
    node->auxiliary_type = carrier;
    node->runtime_type = element;
    result->node = id;
    result->type = output;
    result->valid = true;
    return r_body_attach_effect_exits(body, id, effect_set, view.span);
}

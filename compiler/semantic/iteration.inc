/* R-STMT-0014 and R-EXPR-0029: range-for statements and membership tests.
 * A range-for lowers to the loop core the source-level loops use: hidden locals hold the
 * counter and bound, the borrowed sequence or the iterator, and a per-iteration prologue
 * binds the loop variable before the body. A membership test lowers to the containment
 * operation of its right operand. Neither form adds a node kind of its own beyond the tag
 * and payload projections of an option value. */

typedef enum RIterationForm {
    R_ITERATION_RANGE = 0,
    R_ITERATION_SEQUENCE,
    R_ITERATION_CURSOR,
    R_ITERATION_ITERATOR,
    /* R-STMT-0021 (L36): a receiver or a core::AsyncIterator, awaited at each iteration. */
    R_ITERATION_ASYNC
} RIterationForm;

/* A comprehension appends its per-iteration statements to the loop body directly, so that
   nested clauses do not add block levels of their own (R-EXPR-0030). */
typedef bool (*RIterationBodyFn)(RBodyContext *, void *, RHirVector *, bool *);

typedef struct RIterationPlan {
    RIterationForm form;
    RAstRef statement;
    RAstRef block;
    RAstTokenView name;
    RSourceSpan span;
    /* The declared loop variable type; invalid for auto. */
    RTypeId declared_type;
    /* The type the loop variable receives. */
    RTypeId item_type;
    /* SEQUENCE: the element type. */
    RTypeId element_type;
    /* RANGE and SEQUENCE: the hidden counter; RANGE: the hidden bound. */
    RSymbolId counter;
    RSymbolId limit;
    /* SEQUENCE: the hidden slice of an array, or the hidden borrow of a fixed array or slice
       place. */
    RSymbolId sequence;
    bool sequence_is_slice;
    /* CURSOR and ITERATOR: the hidden iterator, held by value or as an exclusive borrow. */
    RSymbolId iterator;
    bool iterator_is_borrow;
    bool dict_cursor;
    /* SEQUENCE: how the loop variable receives an element. */
    bool element_by_value;
    bool element_exclusive;
    /* ITERATOR and ASYNC: the resolved next. */
    RSymbolId next_function;
    /* ASYNC: the named object advanced at each iteration, and whether it is a receiver. */
    RSymbolId source;
    bool async_receive;
    /* R-EXPR-0030: a comprehension supplies the loop body instead of a block. */
    RIterationBodyFn lower_body;
    void *user;
} RIterationPlan;

static bool r_iteration_hidden_name(RBodyContext *body,
                                    const char *label,
                                    RSourceSpan span,
                                    RAstTokenView *name) {
    (void)memset(name, 0, sizeof(*name));
    name->kind = R_TOKEN_IDENTIFIER;
    name->span = span;
    return r_intern_bytes(body->frontend, (const uint8_t *)label, strlen(label), &name->intern_id);
}

/* A hidden local of the enclosing scope; its declaration joins the statements. */
static bool r_iteration_declare_hidden(RBodyContext *body,
                                       size_t scope_base,
                                       const char *label,
                                       RTypeId type,
                                       RAstNodeId declaration_ast,
                                       RSourceSpan span,
                                       const RExpressionResult *value,
                                       RHirVector *statements,
                                       RSymbolId *symbol) {
    RAstTokenView name;
    RHirNodeId node = R_HIR_NODE_ID_INVALID;

    *symbol = R_SYMBOL_ID_INVALID;
    if (!r_iteration_hidden_name(body, label, span, &name) || !r_body_declare_local(body,
                                                                                    scope_base,
                                                                                    name,
                                                                                    type,
                                                                                    declaration_ast,
                                                                                    false,
                                                                                    false,
                                                                                    span,
                                                                                    value,
                                                                                    false,
                                                                                    false,
                                                                                    &node)) {
        return false;
    }
    if (body->active_symbols.count == 0U) {
        body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    *symbol = body->active_symbols.items[body->active_symbols.count - 1U];
    body->frontend->semantic_symbols[(size_t)*symbol - 1U].is_hidden_local = true;
    return (node == R_HIR_NODE_ID_INVALID) || r_hir_vector_push(body->frontend, statements, node);
}

static bool
r_iteration_load(RBodyContext *body, RSymbolId symbol, RSourceSpan span, RExpressionResult *value) {
    r_body_expression_invalid(value);
    return r_body_symbol_place(body, symbol, span, value) && r_body_place_to_value(body, value);
}

static bool r_iteration_literal(RBodyContext *body,
                                RSourceSpan span,
                                RTypeId type,
                                uint64_t integer_value,
                                RExpressionResult *result) {
    RHirNodeId node;

    r_body_expression_invalid(result);
    if (!r_body_append_node(body,
                            R_HIR_LITERAL,
                            span,
                            type,
                            R_SYMBOL_ID_INVALID,
                            R_TOKEN_INVALID,
                            integer_value,
                            false,
                            NULL,
                            &node)) {
        return false;
    }
    result->node = node;
    result->type = type;
    result->valid = true;
    return true;
}

/* A comparison of two lowered values of one type (R-EXPR-0009). */
static bool r_iteration_compare(RBodyContext *body,
                                RSourceSpan span,
                                RTokenKind operation,
                                const RExpressionResult *left,
                                const RExpressionResult *right,
                                RExpressionResult *result) {
    RHirNodeId children_storage[2];
    RHirVector children;
    RTypeId bool_type;
    RHirNodeId node;

    r_body_expression_invalid(result);
    if (!left->valid || !right->valid) {
        return true;
    }
    if (!r_semantic_type_from_token(body->frontend, R_TOKEN_KW_BOOL, &bool_type)) {
        return false;
    }
    children_storage[0] = left->node;
    children_storage[1] = right->node;
    children.items = children_storage;
    children.count = UINT32_C(2);
    children.capacity = UINT32_C(2);
    if (!r_body_append_node(body,
                            R_HIR_BINARY,
                            span,
                            bool_type,
                            R_SYMBOL_ID_INVALID,
                            operation,
                            UINT64_C(0),
                            false,
                            &children,
                            &node)) {
        return false;
    }
    result->node = node;
    result->type = bool_type;
    result->valid = true;
    return true;
}

static bool r_iteration_expression_statement(RBodyContext *body,
                                             RSourceSpan span,
                                             const RExpressionResult *value,
                                             RHirVector *statements) {
    RHirNodeId child_storage[1];
    RHirVector children = {0};
    RTypeId void_type;
    RHirNodeId node;

    if (!value->valid) {
        return true;
    }
    if (!r_semantic_type_from_token(body->frontend, R_TOKEN_KW_VOID, &void_type)) {
        return false;
    }
    child_storage[0] = value->node;
    children.items = child_storage;
    children.count = UINT32_C(1);
    children.capacity = UINT32_C(1);
    return r_body_append_node(body,
                              R_HIR_EXPRESSION_STATEMENT,
                              span,
                              void_type,
                              R_SYMBOL_ID_INVALID,
                              R_TOKEN_INVALID,
                              UINT64_C(0),
                              false,
                              &children,
                              &node) &&
           r_hir_vector_push(body->frontend, statements, node);
}

/* A synthesized `break` of the innermost loop, with the drops and state recording of the
 * source-level statement (R-STMT-0004). */
static bool r_body_lower_loop_break(RBodyContext *body, RSourceSpan span, RHirNodeId *node_id) {
    RHirVector cleanup = {0};
    RTypeId void_type;
    size_t target_scope_base;
    bool transfer_reachable;
    bool success;

    *node_id = R_HIR_NODE_ID_INVALID;
    if (body->active_loop_scope_bases.count == 0U) {
        body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    if ((body->finally_depth != UINT32_C(0)) && (body->loop_depth <= body->finally_loop_depth)) {
        return r_body_diagnostic(body,
                                 "R-DIAG-EFFECT-001",
                                 "R-ERR-0003",
                                 "loop jump cannot leave a finally clause",
                                 span);
    }
    target_scope_base =
        body->active_loop_scope_bases.items[body->active_loop_scope_bases.count - 1U];
    transfer_reachable = r_body_loop_transfer_reaches_current_loop(body);
    if (!r_body_record_loop_object_state(body, &body->active_loop_break_states, span) ||
        (transfer_reachable &&
         !r_body_record_pending_finally_transfer(body, target_scope_base, SIZE_MAX, true, span)) ||
        !r_body_append_scope_exit_drops(body, target_scope_base, true, span, &cleanup)) {
        if (body->frontend->resource_status == R_FRONTEND_OK) {
            body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
        }
        r_context_free(body->frontend, cleanup.items);
        return false;
    }
    success = r_semantic_type_from_token(body->frontend, R_TOKEN_KW_VOID, &void_type) &&
              r_body_append_node(body,
                                 R_HIR_BREAK,
                                 span,
                                 void_type,
                                 R_SYMBOL_ID_INVALID,
                                 R_TOKEN_KW_BREAK,
                                 UINT64_C(0),
                                 false,
                                 &cleanup,
                                 node_id);
    r_context_free(body->frontend, cleanup.items);
    if (success && transfer_reachable) {
        body->current_loop_has_break = true;
    }
    return success;
}

/* The prototype of a core trait method bound to one closed type or one constrained parameter
 * (R-TYPE-0046). */
static bool r_iteration_resolve_trait_method(RBodyContext *body,
                                             RTypeId owner_type,
                                             uint32_t trait,
                                             const char *method,
                                             RSourceSpan span,
                                             RSymbolId *function_id) {
    RFrontendContext *context = body->frontend;
    const RSemanticType *type = r_semantic_type(context, owner_type);
    const RSemanticMethod *prototype;
    RAstTokenView name;
    const RSemanticSymbol *symbol;

    *function_id = R_SYMBOL_ID_INVALID;
    if (trait == UINT32_C(0)) {
        /* The core trait was never mentioned, so nothing implements it. */
        return true;
    }
    if ((type == NULL) || !r_iteration_hidden_name(body, method, span, &name)) {
        return false;
    }
    if (type->kind == R_SEMANTIC_TYPE_PARAMETER) {
        if (!r_body_resolve_method(body, owner_type, &name, span, function_id)) {
            return false;
        }
        symbol = *function_id == R_SYMBOL_ID_INVALID ? NULL : r_body_symbol(body, *function_id);
        if ((symbol != NULL) && (symbol->trait != trait)) {
            *function_id = R_SYMBOL_ID_INVALID;
            return r_body_diagnostic(body,
                                     "R-DIAG-TYPE-001",
                                     "R-TYPE-0046",
                                     "the type parameter does not carry the required core trait",
                                     span);
        }
        return true;
    }
    prototype = r_semantic_find_trait_method(context, trait, name.intern_id);
    if ((prototype == NULL) || !r_semantic_type_implements_trait(context, owner_type, trait) ||
        !r_generic_bind_trait_call(context, prototype->function, owner_type, function_id, NULL)) {
        *function_id = R_SYMBOL_ID_INVALID;
        return true;
    }
    return true;
}

/* --- the loop condition --------------------------------------------------------------- */

static bool r_iteration_sequence_place(RBodyContext *body,
                                       const RIterationPlan *plan,
                                       RExpressionResult *place) {
    RExpressionResult hidden = {0};

    r_body_expression_invalid(place);
    if (!r_body_symbol_place(body, plan->sequence, plan->span, &hidden)) {
        return false;
    }
    if (plan->sequence_is_slice) {
        *place = hidden;
        return true;
    }
    return r_body_form_deref_place(body, hidden, plan->span, place);
}

static bool
r_iteration_length(RBodyContext *body, const RIterationPlan *plan, RExpressionResult *result) {
    RExpressionResult place = {0};
    const RSemanticType *type;
    RHirNodeId children_storage[1];
    RHirVector children;
    RTypeId usize_type;
    RHirNodeId node;

    r_body_expression_invalid(result);
    if (!r_iteration_sequence_place(body, plan, &place)) {
        return false;
    }
    if (!place.valid) {
        return true;
    }
    type = r_semantic_type(body->frontend, r_semantic_value_type(body->frontend, place.type));
    if ((type == NULL) ||
        !r_semantic_type_from_token(body->frontend, R_TOKEN_KW_USIZE, &usize_type)) {
        body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    children_storage[0] = place.node;
    children.items = children_storage;
    children.count = UINT32_C(1);
    children.capacity = UINT32_C(1);
    if (!r_body_append_node(body,
                            R_HIR_LENGTH,
                            plan->span,
                            usize_type,
                            place.symbol,
                            R_TOKEN_INVALID,
                            type->kind == R_SEMANTIC_TYPE_FIXED_ARRAY ? type->length : UINT64_MAX,
                            false,
                            &children,
                            &node)) {
        return false;
    }
    result->node = node;
    result->type = usize_type;
    result->valid = true;
    return true;
}

static bool r_iteration_lower_condition(RBodyContext *body, void *user, RExpressionResult *result) {
    const RIterationPlan *plan = (const RIterationPlan *)user;
    RExpressionResult left = {0};
    RExpressionResult right = {0};
    RTypeId bool_type;

    r_body_expression_invalid(result);
    switch (plan->form) {
    case R_ITERATION_RANGE:
        return r_iteration_load(body, plan->counter, plan->span, &left) &&
               r_iteration_load(body, plan->limit, plan->span, &right) &&
               r_iteration_compare(body, plan->span, R_TOKEN_LESS, &left, &right, result);
    case R_ITERATION_SEQUENCE:
        return r_iteration_load(body, plan->counter, plan->span, &left) &&
               r_iteration_length(body, plan, &right) &&
               r_iteration_compare(body, plan->span, R_TOKEN_LESS, &left, &right, result);
    case R_ITERATION_CURSOR:
    case R_ITERATION_ITERATOR:
    case R_ITERATION_ASYNC:
    default:
        return r_semantic_type_from_token(body->frontend, R_TOKEN_KW_BOOL, &bool_type) &&
               r_iteration_literal(body, plan->span, bool_type, UINT64_C(1), result);
    }
}

/* --- the loop body -------------------------------------------------------------------- */

static bool r_iteration_bind_variable(RBodyContext *body,
                                      const RIterationPlan *plan,
                                      size_t scope_base,
                                      const RExpressionResult *value,
                                      RHirVector *statements) {
    RHirNodeId node = R_HIR_NODE_ID_INVALID;

    if (!r_body_declare_local(body,
                              scope_base,
                              plan->name,
                              plan->item_type,
                              plan->statement.node,
                              false,
                              false,
                              plan->span,
                              value,
                              false,
                              false,
                              &node)) {
        return false;
    }
    return (node == R_HIR_NODE_ID_INVALID) || r_hir_vector_push(body->frontend, statements, node);
}

static bool
r_iteration_step(RBodyContext *body, const RIterationPlan *plan, RHirVector *statements) {
    RExpressionResult place = {0};
    RExpressionResult update = {0};

    r_body_expression_invalid(&place);
    if (!r_body_symbol_place(body, plan->counter, plan->span, &place) ||
        !r_body_lower_update(body, plan->span, R_TOKEN_PLUS_PLUS, &place, &update)) {
        return false;
    }
    return r_iteration_expression_statement(body, plan->span, &update, statements);
}

static bool r_iteration_bind_element(RBodyContext *body,
                                     const RIterationPlan *plan,
                                     size_t scope_base,
                                     RHirVector *statements) {
    RExpressionResult base = {0};
    RExpressionResult index = {0};
    RExpressionResult element = {0};
    RExpressionResult value = {0};

    if (!r_iteration_sequence_place(body, plan, &base) ||
        !r_iteration_load(body, plan->counter, plan->span, &index)) {
        return false;
    }
    if (!base.valid || !index.valid) {
        return true;
    }
    if (!r_body_form_index_place_lowered(body, plan->span, &base, &index, plan->span, &element)) {
        return false;
    }
    if (!element.valid) {
        return true;
    }
    if (plan->element_by_value) {
        value = element;
        if (!r_body_place_to_value(body, &value)) {
            return false;
        }
    } else if (!r_body_form_safe_borrow(body, plan->span, plan->item_type, &element, &value)) {
        return false;
    }
    if (!value.valid) {
        return true;
    }
    return r_iteration_bind_variable(body, plan, scope_base, &value, statements);
}

/* `next` on the hidden iterator: a standard cursor call or the bound trait method. */
static bool
r_iteration_next(RBodyContext *body, const RIterationPlan *plan, RExpressionResult *result) {
    RExpressionResult hidden = {0};
    RExpressionResult place = {0};
    RExpressionResult receiver = {0};
    const RSemanticSymbol *function;
    RTypeId receiver_type;
    RHirNodeId children_storage[1];
    RHirVector children;
    RHirNodeId node;

    r_body_expression_invalid(result);
    if (!r_body_symbol_place(body, plan->iterator, plan->span, &hidden)) {
        return false;
    }
    if (plan->iterator_is_borrow) {
        if (!r_body_form_deref_place(body, hidden, plan->span, &place)) {
            return false;
        }
    } else {
        place = hidden;
    }
    if (!place.valid) {
        return true;
    }
    if (plan->form == R_ITERATION_CURSOR) {
        return r_body_form_standard_next(body,
                                         &place,
                                         plan->span,
                                         plan->span,
                                         plan->span,
                                         plan->dict_cursor,
                                         R_TYPE_ID_INVALID,
                                         result);
    }
    function = r_body_symbol(body, plan->next_function);
    if ((function == NULL) || (function->parameter_count != UINT32_C(1))) {
        body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    receiver_type = body->frontend->semantic_parameter_types[function->first_parameter_type];
    const size_t read_base = body->loop_reads.count;
    if (!r_body_form_safe_borrow(body, plan->span, receiver_type, &place, &receiver)) {
        return false;
    }
    if (!receiver.valid) {
        return true;
    }
    /* R-TYPE-0045: the hidden iterator only reaches `next`, whose item holds what the iterator
       holds; the store of the item already carries that into the next iteration, so reaching
       the iterator passes nothing on (loop_dataflow.inc). An item a lending `next` returns
       borrows the iterator instead (L16.2). */
    for (size_t read = read_base; !function->is_lending && (read < body->loop_reads.count);
         ++read) {
        if ((body->loop_reads.items[read].symbol == plan->iterator) &&
            (body->loop_reads.items[read].kind == R_LOOP_READ_VALUE)) {
            body->loop_reads.items[read].kind = R_LOOP_READ_USE;
        }
    }
    children_storage[0] = receiver.node;
    children.items = children_storage;
    children.count = UINT32_C(1);
    children.capacity = UINT32_C(1);
    if (!r_body_append_node(body,
                            R_HIR_CALL,
                            plan->span,
                            function->return_type,
                            plan->next_function,
                            R_TOKEN_INVALID,
                            UINT64_C(0),
                            false,
                            &children,
                            &node)) {
        return false;
    }
    r_body_hir_node(body, node)->auxiliary_type = function->effect_carrier_type;
    result->node = node;
    result->type = function->return_type;
    result->symbol = plan->next_function;
    result->borrow_origin = receiver.borrow_origin;
    result->borrow_referent = receiver.borrow_referent;
    result->borrow_origin_set = receiver.borrow_origin_set;
    result->borrow_origin_multiple = receiver.borrow_origin_multiple;
    /* R-TYPE-0045: `o<Self::Item>` holds what the iterator holds, never the iterator, unless
       `next` lends (L16.2). */
    if (!function->is_lending && !r_body_result_held_content(body, result)) {
        return false;
    }
    r_body_hir_node(body, node)->borrow_origin = result->borrow_origin;
    r_body_hir_node(body, node)->borrow_referent = result->borrow_referent;
    r_body_hir_node(body, node)->borrow_origin_set = result->borrow_origin_set;
    r_body_hir_node(body, node)->borrow_origin_multiple = result->borrow_origin_multiple;
    result->valid = true;
    return true;
}

/* `if (tag == none) break;` over the hidden option; the branch never falls through, so the
 * object states after it are the states before it. */
static bool r_iteration_break_when_none(RBodyContext *body,
                                        const RIterationPlan *plan,
                                        RSymbolId option,
                                        RHirVector *statements) {
    RExpressionResult place = {0};
    RExpressionResult value = {0};
    RExpressionResult tag = {0};
    RExpressionResult zero = {0};
    RExpressionResult condition = {0};
    RSemanticObjectSnapshot *entry_states = NULL;
    size_t entry_state_count = 0U;
    RHirNodeId children_storage[2];
    RHirVector children;
    RHirNodeId break_node;
    RHirNodeId block_node;
    RHirNodeId if_node;
    RTypeId borrow_type;
    RTypeId u32_type;
    RTypeId void_type;
    bool success = false;

    r_body_expression_invalid(&tag);
    r_body_expression_invalid(&place);
    /* The tag is read through a shared borrow: the option may hold a Move payload. */
    if (!r_body_symbol_place(body, option, plan->span, &place) ||
        !r_body_standard_borrow_type(
            body, r_semantic_value_type(body->frontend, place.type), true, &borrow_type) ||
        !r_body_form_safe_borrow(body, plan->span, borrow_type, &place, &value)) {
        return false;
    }
    if (!value.valid) {
        return true;
    }
    if (!r_semantic_type_from_token(body->frontend, R_TOKEN_KW_U32, &u32_type) ||
        !r_semantic_type_from_token(body->frontend, R_TOKEN_KW_VOID, &void_type)) {
        return false;
    }
    children_storage[0] = value.node;
    children.items = children_storage;
    children.count = UINT32_C(1);
    children.capacity = UINT32_C(1);
    if (!r_body_append_node(body,
                            R_HIR_VARIANT_TAG,
                            plan->span,
                            u32_type,
                            R_SYMBOL_ID_INVALID,
                            R_TOKEN_INVALID,
                            UINT64_C(0),
                            false,
                            &children,
                            &tag.node)) {
        return false;
    }
    tag.type = u32_type;
    tag.valid = true;
    if (!r_iteration_literal(body, plan->span, u32_type, UINT64_C(0), &zero) ||
        !r_iteration_compare(body, plan->span, R_TOKEN_EQUAL_EQUAL, &tag, &zero, &condition) ||
        !condition.valid ||
        !r_body_capture_active_object_states(body, &entry_states, &entry_state_count) ||
        !r_body_lower_loop_break(body, plan->span, &break_node)) {
        goto cleanup;
    }
    children_storage[0] = break_node;
    children.count = UINT32_C(1);
    if (!r_body_append_node(body,
                            R_HIR_BLOCK,
                            plan->span,
                            void_type,
                            R_SYMBOL_ID_INVALID,
                            R_TOKEN_INVALID,
                            UINT64_C(0),
                            false,
                            &children,
                            &block_node) ||
        !r_body_restore_active_object_states(body, entry_states, entry_state_count)) {
        goto cleanup;
    }
    children_storage[0] = condition.node;
    children_storage[1] = block_node;
    children.count = UINT32_C(2);
    children.capacity = UINT32_C(2);
    success = r_body_append_node(body,
                                 R_HIR_IF,
                                 plan->span,
                                 void_type,
                                 R_SYMBOL_ID_INVALID,
                                 R_TOKEN_KW_IF,
                                 UINT64_C(0),
                                 false,
                                 &children,
                                 &if_node) &&
              r_hir_vector_push(body->frontend, statements, if_node);

cleanup:
    r_context_free(body->frontend, entry_states);
    return success;
}

/* The loop variable takes the payload of the hidden option (R-STMT-0010 by construction:
 * the whole option is consumed and its payload transferred exactly once). */
static bool r_iteration_bind_payload(RBodyContext *body,
                                     const RIterationPlan *plan,
                                     RSymbolId option,
                                     size_t scope_base,
                                     RHirVector *statements) {
    RExpressionResult place = {0};
    RExpressionResult value = {0};
    RExpressionResult payload = {0};
    RHirNodeId children_storage[1];
    RHirVector children;
    RHirNodeId node;

    r_body_expression_invalid(&place);
    r_body_expression_invalid(&payload);
    if (!r_body_symbol_place(body, option, plan->span, &place)) {
        return false;
    }
    if (!place.valid) {
        return true;
    }
    if (r_semantic_type_is_copy(body->frontend, place.type)) {
        value = place;
        if (!r_body_place_to_value(body, &value)) {
            return false;
        }
    } else if (!r_body_move_from_place(body, place, plan->span, &value)) {
        return false;
    }
    if (!value.valid) {
        return true;
    }
    children_storage[0] = value.node;
    children.items = children_storage;
    children.count = UINT32_C(1);
    children.capacity = UINT32_C(1);
    if (!r_body_append_node(body,
                            R_HIR_VARIANT_PAYLOAD,
                            plan->span,
                            plan->item_type,
                            R_SYMBOL_ID_INVALID,
                            R_TOKEN_KW_MOVE,
                            UINT64_C(1),
                            false,
                            &children,
                            &node)) {
        return false;
    }
    r_body_hir_node(body, node)->borrow_origin = value.borrow_origin;
    r_body_hir_node(body, node)->borrow_referent = value.borrow_referent;
    r_body_hir_node(body, node)->borrow_origin_set = value.borrow_origin_set;
    r_body_hir_node(body, node)->borrow_origin_multiple = value.borrow_origin_multiple;
    payload.node = node;
    payload.type = plan->item_type;
    payload.borrow_origin = value.borrow_origin;
    payload.borrow_referent = value.borrow_referent;
    payload.borrow_origin_set = value.borrow_origin_set;
    payload.borrow_origin_multiple = value.borrow_origin_multiple;
    payload.valid = true;
    return r_iteration_bind_variable(body, plan, scope_base, &payload, statements);
}

/* R-STMT-0021 (L36): each iteration borrows the source again and awaits the start of the next
   item: an asynchronous receive (R-LIB-0016) without a group, or the scoped `next` of a
   core::AsyncIterator started in the innermost task group with the borrow as its loan. The
   started task lives in a hidden `$await` local, as for `await f(...)`, which ends the loan. */
static bool r_iteration_async_next(RBodyContext *body,
                                   const RIterationPlan *plan,
                                   RHirNodeId *task_local,
                                   RExpressionResult *result) {
    RFrontendContext *context = body->frontend;
    RExpressionResult place = {0};
    RExpressionResult borrow = {0};
    RExpressionResult task = {0};
    RSymbolId awaited = R_SYMBOL_ID_INVALID;

    r_body_expression_invalid(result);
    r_body_expression_invalid(&task);
    *task_local = R_HIR_NODE_ID_INVALID;
    if (!r_body_symbol_place(body, plan->source, plan->span, &place)) {
        return false;
    }
    if (!place.valid) {
        return true;
    }
    if (plan->async_receive) {
        RTypeId borrow_type;
        if (!r_semantic_intern_derived_type(context,
                                            R_SEMANTIC_TYPE_BORROW,
                                            r_semantic_value_type(context, place.type),
                                            R_TYPE_ID_INVALID,
                                            UINT64_C(0),
                                            R_SEMANTIC_TYPE_FLAG_SHARED,
                                            &borrow_type) ||
            !r_body_form_safe_borrow(body, plan->span, borrow_type, &place, &borrow)) {
            return false;
        }
        if (!borrow.valid) {
            return true;
        }
        if (!r_body_sync_receive_start(
                body, &borrow, plan->span, plan->span, R_TYPE_ID_INVALID, &task)) {
            return false;
        }
    } else {
        const RSemanticSymbol *function = r_body_symbol(body, plan->next_function);
        RTypeId start_effects;
        RHirNodeId argument_storage[1];
        RHirVector arguments = {argument_storage, UINT32_C(1), UINT32_C(1)};
        RHirNodeId node;
        if ((function == NULL) || (function->parameter_count != UINT32_C(1)) ||
            !function->is_async || (function->async_task_type == R_TYPE_ID_INVALID)) {
            context->resource_status = R_FRONTEND_INTERNAL_ERROR;
            return false;
        }
        const RSemanticSymbol next = *function;
        if (!r_body_form_safe_borrow(body,
                                     plan->span,
                                     context->semantic_parameter_types[next.first_parameter_type],
                                     &place,
                                     &borrow)) {
            return false;
        }
        if (!borrow.valid) {
            return true;
        }
        if (!r_body_standard_async_start_effects(
                body, next.async_start_type, plan->span, &start_effects)) {
            return false;
        }
        argument_storage[0] = borrow.node;
        if (!r_body_append_node(body,
                                R_HIR_ASYNC_START,
                                plan->span,
                                next.async_task_type,
                                plan->next_function,
                                R_TOKEN_INVALID,
                                UINT64_C(0),
                                false,
                                &arguments,
                                &node)) {
            return false;
        }
        r_body_hir_node(body, node)->auxiliary_type = next.async_start_type;
        if (((r_semantic_effect_count(context, start_effects) != UINT32_C(0)) &&
             !r_body_attach_effect_exits(body, node, start_effects, plan->span)) ||
            !r_scope_record_call(body, node, &arguments)) {
            return false;
        }
        task.node = node;
        task.type = next.async_task_type;
        task.symbol = plan->next_function;
        task.valid = true;
    }
    if (!task.valid) {
        return true;
    }
    return r_body_await_materialize(body, &task, plan->span, task_local, &awaited) &&
           r_body_await_task_symbol(body,
                                    awaited,
                                    R_BODY_LOOKUP_UNRESOLVED,
                                    plan->span,
                                    plan->span,
                                    plan->span,
                                    R_TYPE_ID_INVALID,
                                    false,
                                    false,
                                    *task_local,
                                    false,
                                    result);
}

static bool
r_iteration_lower_body(RBodyContext *body, void *user, RHirNodeId *node_id, bool *always_returns) {
    const RIterationPlan *plan = (const RIterationPlan *)user;
    const size_t scope_base = body->active_symbols.count;
    RHirVector statements = {0};
    RHirNodeId block_node = R_HIR_NODE_ID_INVALID;
    RExpressionResult value = {0};
    RSymbolId option = R_SYMBOL_ID_INVALID;
    RTypeId void_type;
    bool block_returns = false;
    bool success = false;

    *node_id = R_HIR_NODE_ID_INVALID;
    *always_returns = false;
    if (!r_semantic_type_from_token(body->frontend, R_TOKEN_KW_VOID, &void_type)) {
        goto cleanup;
    }
    switch (plan->form) {
    case R_ITERATION_RANGE:
        if (!r_iteration_load(body, plan->counter, plan->span, &value) ||
            !r_iteration_bind_variable(body, plan, scope_base, &value, &statements) ||
            !r_iteration_step(body, plan, &statements)) {
            goto cleanup;
        }
        break;
    case R_ITERATION_SEQUENCE:
        if (!r_iteration_bind_element(body, plan, scope_base, &statements) ||
            !r_iteration_step(body, plan, &statements)) {
            goto cleanup;
        }
        break;
    case R_ITERATION_CURSOR:
    case R_ITERATION_ITERATOR:
    case R_ITERATION_ASYNC:
    default:
        if (plan->form == R_ITERATION_ASYNC) {
            RHirNodeId task_local = R_HIR_NODE_ID_INVALID;
            if (!r_iteration_async_next(body, plan, &task_local, &value) ||
                ((task_local != R_HIR_NODE_ID_INVALID) && value.valid &&
                 !r_hir_vector_push(body->frontend, &statements, task_local))) {
                goto cleanup;
            }
        } else if (!r_iteration_next(body, plan, &value)) {
            goto cleanup;
        }
        if (!value.valid) {
            success = true;
            goto cleanup;
        }
        if (!r_iteration_declare_hidden(body,
                                        scope_base,
                                        "$next",
                                        r_semantic_value_type(body->frontend, value.type),
                                        plan->statement.node,
                                        plan->span,
                                        &value,
                                        &statements,
                                        &option) ||
            !r_iteration_break_when_none(body, plan, option, &statements) ||
            !r_iteration_bind_payload(body, plan, option, scope_base, &statements)) {
            goto cleanup;
        }
        break;
    }
    if (plan->lower_body != NULL) {
        if (!plan->lower_body(body, plan->user, &statements, &block_returns)) {
            goto cleanup;
        }
    } else if (!r_body_lower_block(body, plan->block, false, &block_node, &block_returns) ||
               ((block_node != R_HIR_NODE_ID_INVALID) &&
                !r_hir_vector_push(body->frontend, &statements, block_node))) {
        goto cleanup;
    }
    if (!block_returns) {
        size_t drop_index = body->active_symbols.count;
        while (drop_index > scope_base) {
            drop_index -= 1U;
            if (!r_body_append_implicit_drop(
                    body, body->active_symbols.items[drop_index], plan->span, &statements)) {
                goto cleanup;
            }
        }
    }
    if (!r_body_append_node(body,
                            R_HIR_BLOCK,
                            plan->span,
                            void_type,
                            R_SYMBOL_ID_INVALID,
                            R_TOKEN_INVALID,
                            UINT64_C(0),
                            false,
                            &statements,
                            node_id)) {
        goto cleanup;
    }
    *always_returns = block_returns;
    success = true;

cleanup:
    body->active_symbols.count = scope_base;
    r_context_free(body->frontend, statements.items);
    return success;
}

/* --- planning the header ---------------------------------------------------------------- */

static bool r_iteration_diagnostic(RBodyContext *body, RSourceSpan span, const char *message) {
    return r_body_diagnostic(body, "R-DIAG-TYPE-001", "R-STMT-0014", message, span);
}

/* Lowers a bound with the counter type as context and settles unsuffixed literals. */
static bool r_iteration_lower_bound(RBodyContext *body,
                                    RAstRef expression,
                                    RTypeId contextual_type,
                                    RExpressionResult *result) {
    if (!r_body_lower_value(body, expression, contextual_type, result)) {
        return false;
    }
    if (result->valid && result->is_integer_literal_form &&
        !r_body_finalize_integer_literal(
            body, result, contextual_type, r_body_literal_is_negative(body, result))) {
        return false;
    }
    return true;
}

static bool r_iteration_plan_range(RBodyContext *body,
                                   RIterationPlan *plan,
                                   RAstRef range,
                                   size_t scope_base,
                                   RHirVector *statements) {
    RAstNodeView view;
    RAstRef bounds[2];
    uint32_t bound_count = 0U;
    uint32_t child_index;
    RExpressionResult low = {0};
    RExpressionResult high = {0};
    RTypeId counter_type;

    if (!r_ast_ref_view(body->frontend, range, &view)) {
        body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    for (child_index = 0U; child_index < view.child_count; ++child_index) {
        RAstRef child;
        RAstTokenView token;
        if (!r_ast_ref_child(body->frontend, range, child_index, &child)) {
            body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
            return false;
        }
        if (r_ast_ref_token(body->frontend, child, &token)) {
            continue;
        }
        if (bound_count == 2U) {
            body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
            return false;
        }
        bounds[bound_count++] = child;
    }
    if (bound_count != 2U) {
        body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    if ((plan->declared_type != R_TYPE_ID_INVALID) &&
        !r_semantic_type_is_integer(body->frontend, plan->declared_type)) {
        return r_iteration_diagnostic(
            body, plan->span, "a range-for over lo..hi declares an integer loop variable");
    }
    if (!r_iteration_lower_bound(body, bounds[0], plan->declared_type, &low)) {
        return false;
    }
    if (!low.valid) {
        return true;
    }
    counter_type = plan->declared_type != R_TYPE_ID_INVALID
                       ? plan->declared_type
                       : r_semantic_value_type(body->frontend, low.type);
    if (!r_semantic_type_is_integer(body->frontend, counter_type) ||
        (r_semantic_value_type(body->frontend, low.type) != counter_type)) {
        return r_iteration_diagnostic(
            body, plan->span, "the bounds of a range have the integer type of the loop variable");
    }
    if (!r_iteration_declare_hidden(body,
                                    scope_base,
                                    "$counter",
                                    counter_type,
                                    plan->statement.node,
                                    plan->span,
                                    &low,
                                    statements,
                                    &plan->counter) ||
        !r_iteration_lower_bound(body, bounds[1], counter_type, &high)) {
        return false;
    }
    if (!high.valid) {
        return true;
    }
    if (r_semantic_value_type(body->frontend, high.type) != counter_type) {
        return r_iteration_diagnostic(
            body, plan->span, "the bounds of a range have the integer type of the loop variable");
    }
    if (!r_iteration_declare_hidden(body,
                                    scope_base,
                                    "$limit",
                                    counter_type,
                                    plan->statement.node,
                                    plan->span,
                                    &high,
                                    statements,
                                    &plan->limit)) {
        return false;
    }
    plan->form = R_ITERATION_RANGE;
    plan->item_type = counter_type;
    return true;
}

/* Unwraps expression wrappers around the iterable and reports a borrowed operand. */
static bool r_iteration_borrowed_operand(RBodyContext *body, RAstRef iterable, RAstRef *operand) {
    RAstRef direct = iterable;
    RAstNodeView view;
    RAstTokenView token;
    uint32_t depth = 0U;

    for (;;) {
        RAstRef child;
        if (!r_ast_ref_view(body->frontend, direct, &view)) {
            return false;
        }
        if ((view.kind != R_SYNTAX_EXPRESSION) && (view.kind != R_SYNTAX_BINARY_EXPRESSION) &&
            (view.kind != R_SYNTAX_POSTFIX_EXPRESSION)) {
            break;
        }
        if (!r_body_ast_single_node_child(body->frontend, direct, &child)) {
            break;
        }
        direct = child;
        if (++depth > r_frontend_tree_depth_limit(body->frontend)) {
            return false;
        }
    }
    if ((view.kind != R_SYNTAX_UNARY_EXPRESSION) ||
        !r_body_ast_first_token(body->frontend, direct, &token) || (token.kind != R_TOKEN_AMP)) {
        return false;
    }
    return r_body_ast_single_node_child(body->frontend, direct, operand);
}

/* The loop variable of a sequence: `const T*` (also for auto), `T*`, or a copied `T`. */
static bool r_iteration_plan_element(RBodyContext *body, RIterationPlan *plan) {
    const RSemanticType *declared = plan->declared_type == R_TYPE_ID_INVALID
                                        ? NULL
                                        : r_semantic_type(body->frontend, plan->declared_type);

    plan->element_by_value = false;
    plan->element_exclusive = false;
    if (declared == NULL) {
        return r_body_standard_borrow_type(body, plan->element_type, true, &plan->item_type);
    }
    if ((declared->kind == R_SEMANTIC_TYPE_BORROW) &&
        ((declared->flags & R_SEMANTIC_TYPE_FLAG_NULLABLE) == 0U) &&
        (declared->base == plan->element_type)) {
        plan->element_exclusive = (declared->flags & R_SEMANTIC_TYPE_FLAG_SHARED) == 0U;
        plan->item_type = plan->declared_type;
        return true;
    }
    if (plan->declared_type == plan->element_type) {
        if (!r_semantic_type_is_copy(body->frontend, plan->element_type)) {
            return r_iteration_diagnostic(
                body,
                plan->span,
                "a Move element is borrowed by the loop variable, as const T* or T*");
        }
        plan->element_by_value = true;
        plan->item_type = plan->declared_type;
        return true;
    }
    return r_iteration_diagnostic(
        body, plan->span, "the loop variable of a sequence is const T*, T* or a copied T");
}

/* Declares the hidden borrow or slice of a sequence place and the counter. */
static bool r_iteration_plan_sequence(RBodyContext *body,
                                      RIterationPlan *plan,
                                      const RExpressionResult *place,
                                      size_t scope_base,
                                      RHirVector *statements) {
    const RSemanticType *type =
        r_semantic_type(body->frontend, r_semantic_value_type(body->frontend, place->type));
    RTypeId borrow_type;
    RExpressionResult borrow = {0};
    RExpressionResult zero = {0};
    RTypeId usize_type;
    bool shared;
    bool is_array;

    if (type == NULL) {
        body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    /* The type table may reallocate while types are interned below; `type` is not kept. */
    is_array = type->kind == R_SEMANTIC_TYPE_ARRAY;
    plan->form = R_ITERATION_SEQUENCE;
    plan->element_type = type->base;
    if (!r_iteration_plan_element(body, plan)) {
        return false;
    }
    if (!body->poisoned) {
        shared = !plan->element_exclusive;
        if (!r_body_standard_borrow_type(
                body, r_semantic_value_type(body->frontend, place->type), shared, &borrow_type) ||
            !r_body_form_safe_borrow(body, plan->span, borrow_type, place, &borrow)) {
            return false;
        }
        if (!borrow.valid) {
            return true;
        }
        if (is_array) {
            /* std.array::as_slice / as_slice_mut over the borrow (R-LIB-0019). */
            RTypeId slice_type;
            RHirNodeId children_storage[1];
            RHirVector children;
            RHirNodeId node;
            RExpressionResult slice = {0};
            if (!r_semantic_intern_derived_type(body->frontend,
                                                R_SEMANTIC_TYPE_SLICE,
                                                plan->element_type,
                                                R_TYPE_ID_INVALID,
                                                UINT64_C(0),
                                                shared ? R_SEMANTIC_TYPE_FLAG_SHARED
                                                       : R_SEMANTIC_TYPE_FLAG_NONE,
                                                &slice_type)) {
                return false;
            }
            children_storage[0] = borrow.node;
            children.items = children_storage;
            children.count = UINT32_C(1);
            children.capacity = UINT32_C(1);
            if (!r_body_append_node(body,
                                    R_HIR_SLICE,
                                    plan->span,
                                    slice_type,
                                    borrow.symbol,
                                    R_TOKEN_KW_ARRAY,
                                    UINT64_MAX,
                                    false,
                                    &children,
                                    &node)) {
                return false;
            }
            r_body_hir_node(body, node)->auxiliary_type = borrow_type;
            r_body_hir_node(body, node)->borrow_origin = borrow.borrow_origin;
            r_body_hir_node(body, node)->borrow_referent = borrow.borrow_referent;
            r_body_hir_node(body, node)->borrow_origin_set = borrow.borrow_origin_set;
            r_body_hir_node(body, node)->borrow_origin_multiple = borrow.borrow_origin_multiple;
            r_body_expression_invalid(&slice);
            slice.node = node;
            slice.type = slice_type;
            slice.symbol = borrow.symbol;
            slice.borrow_origin = borrow.borrow_origin;
            slice.borrow_referent = borrow.borrow_referent;
            slice.borrow_origin_set = borrow.borrow_origin_set;
            slice.borrow_origin_multiple = borrow.borrow_origin_multiple;
            slice.valid = true;
            plan->sequence_is_slice = true;
            if (!r_iteration_declare_hidden(body,
                                            scope_base,
                                            "$sequence",
                                            slice_type,
                                            plan->statement.node,
                                            plan->span,
                                            &slice,
                                            statements,
                                            &plan->sequence)) {
                return false;
            }
        } else {
            plan->sequence_is_slice = false;
            if (!r_iteration_declare_hidden(body,
                                            scope_base,
                                            "$sequence",
                                            borrow_type,
                                            plan->statement.node,
                                            plan->span,
                                            &borrow,
                                            statements,
                                            &plan->sequence)) {
                return false;
            }
        }
        if (!r_semantic_type_from_token(body->frontend, R_TOKEN_KW_USIZE, &usize_type) ||
            !r_iteration_literal(body, plan->span, usize_type, UINT64_C(0), &zero) ||
            !r_iteration_declare_hidden(body,
                                        scope_base,
                                        "$counter",
                                        usize_type,
                                        plan->statement.node,
                                        plan->span,
                                        &zero,
                                        statements,
                                        &plan->counter)) {
            return false;
        }
    }
    return true;
}

/* The item type of a standard cursor: `const T*` for a list, an entry reference for a dict. */
static bool r_iteration_cursor_item(RBodyContext *body,
                                    const RSemanticType *cursor,
                                    bool dict_cursor,
                                    RTypeId *item_type) {
    if (dict_cursor) {
        return r_semantic_intern_parametric_standard_type(body->frontend,
                                                          "std.dict::entry_ref",
                                                          cursor->base,
                                                          cursor->second,
                                                          item_type) &&
               r_semantic_prepare_dict_entry_ref_schema(body->frontend, *item_type, item_type);
    }
    return r_body_standard_borrow_type(body, cursor->base, true, item_type);
}

/* Declares the hidden cursor created from a borrowed list or dict place. */
static bool r_iteration_plan_container_cursor(RBodyContext *body,
                                              RIterationPlan *plan,
                                              const RExpressionResult *place,
                                              size_t scope_base,
                                              RHirVector *statements) {
    const RTypeId container_type = r_semantic_value_type(body->frontend, place->type);
    const RSemanticType *type = r_semantic_type(body->frontend, container_type);
    const bool dict_cursor = (type != NULL) && (type->kind == R_SEMANTIC_TYPE_DICT);
    /* Interning below may grow the type table, so the container's components are copied. */
    const RTypeId element_type = type == NULL ? R_TYPE_ID_INVALID : type->base;
    const RTypeId value_type = type == NULL ? R_TYPE_ID_INVALID : type->second;
    RTypeId borrow_type;
    RTypeId cursor_type;
    RExpressionResult target = {0};
    RExpressionResult cursor = {0};
    RHirNodeId children_storage[1];
    RHirVector children;
    const RSemanticType *cursor_description;

    if (type == NULL) {
        body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    if (!r_body_standard_borrow_type(body, container_type, true, &borrow_type) ||
        !r_body_form_safe_borrow(body, plan->span, borrow_type, place, &target)) {
        return false;
    }
    if (!target.valid) {
        return true;
    }
    if (!r_semantic_intern_parametric_standard_type(body->frontend,
                                                    dict_cursor ? "std.dict::iter"
                                                                : "std.list::iter",
                                                    element_type,
                                                    dict_cursor ? value_type : R_TYPE_ID_INVALID,
                                                    &cursor_type)) {
        return false;
    }
    children_storage[0] = target.node;
    children.items = children_storage;
    children.count = UINT32_C(1);
    children.capacity = UINT32_C(1);
    if (!r_body_append_standard_call(body,
                                     plan->span,
                                     cursor_type,
                                     dict_cursor ? value_type : element_type,
                                     dict_cursor ? R_STANDARD_CALL_DICT_ITER
                                                 : R_STANDARD_CALL_LIST_ITER,
                                     &children,
                                     &target,
                                     &cursor)) {
        return false;
    }
    if (dict_cursor) {
        r_body_hir_node(body, cursor.node)->runtime_type = element_type;
    }
    cursor_description = r_semantic_type(body->frontend, cursor_type);
    if ((cursor_description == NULL) ||
        !r_iteration_cursor_item(body, cursor_description, dict_cursor, &plan->item_type)) {
        return false;
    }
    plan->form = R_ITERATION_CURSOR;
    plan->dict_cursor = dict_cursor;
    plan->iterator_is_borrow = false;
    return r_iteration_declare_hidden(body,
                                      scope_base,
                                      "$iterator",
                                      cursor_type,
                                      plan->statement.node,
                                      plan->span,
                                      &cursor,
                                      statements,
                                      &plan->iterator);
}

/* Classifies an iterator held by value or by exclusive borrow: a standard cursor or a type
 * implementing core::Iterator. */
static bool r_iteration_plan_iterator(RBodyContext *body,
                                      RIterationPlan *plan,
                                      const RExpressionResult *value,
                                      bool is_borrow,
                                      size_t scope_base,
                                      RHirVector *statements) {
    RFrontendContext *context = body->frontend;
    const RTypeId held_type = r_semantic_value_type(context, value->type);
    const RSemanticType *held = r_semantic_type(context, held_type);
    RTypeId iterator_type = held_type;
    const RSemanticType *iterator;

    if (held == NULL) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    if (is_borrow) {
        if ((held->kind != R_SEMANTIC_TYPE_BORROW) ||
            ((held->flags & (R_SEMANTIC_TYPE_FLAG_SHARED | R_SEMANTIC_TYPE_FLAG_NULLABLE)) != 0U)) {
            return r_iteration_diagnostic(
                body, plan->span, "an iterator is advanced through an exclusive borrow");
        }
        iterator_type = r_semantic_value_type(context, held->base);
    }
    iterator = r_semantic_type(context, iterator_type);
    if (iterator == NULL) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    plan->iterator_is_borrow = is_borrow;
    if ((iterator->kind == R_SEMANTIC_TYPE_STANDARD) &&
        (r_semantic_standard_type_name_equal(context, iterator, "std.list::iter") ||
         r_semantic_standard_type_name_equal(context, iterator, "std.dict::iter"))) {
        plan->form = R_ITERATION_CURSOR;
        plan->dict_cursor =
            r_semantic_standard_type_name_equal(context, iterator, "std.dict::iter");
        if (!r_iteration_cursor_item(body, iterator, plan->dict_cursor, &plan->item_type)) {
            return false;
        }
    } else {
        const RSemanticSymbol *next;
        const RSemanticType *option;
        if (!r_iteration_resolve_trait_method(body,
                                              iterator_type,
                                              context->core_iterator_trait,
                                              "next",
                                              plan->span,
                                              &plan->next_function)) {
            return false;
        }
        /* R-STMT-0014 (L16.2): an iterator that only lends its items is advanced the same
           way; each item then borrows the iterator until the next advance. */
        if ((plan->next_function == R_SYMBOL_ID_INVALID) && !body->poisoned &&
            (context->core_lending_iterator_trait != UINT32_C(0)) &&
            !r_iteration_resolve_trait_method(body,
                                              iterator_type,
                                              context->core_lending_iterator_trait,
                                              "next",
                                              plan->span,
                                              &plan->next_function)) {
            return false;
        }
        if (plan->next_function == R_SYMBOL_ID_INVALID) {
            if (body->poisoned) {
                return true;
            }
            return r_iteration_diagnostic(body,
                                          plan->span,
                                          "the iterable is a range, a borrowed sequence, list "
                                          "or dict, a standard cursor, a core::Iterator or "
                                          "core::LendingIterator, or in an async function a "
                                          "borrowed std.sync::receiver or core::AsyncIterator");
        }
        next = r_body_symbol(body, plan->next_function);
        option = next == NULL
                     ? NULL
                     : r_semantic_type(context, r_semantic_value_type(context, next->return_type));
        if ((next == NULL) || (next->parameter_count != UINT32_C(1)) || next->is_async ||
            (option == NULL) || (option->kind != R_SEMANTIC_TYPE_OPTION)) {
            return r_iteration_diagnostic(
                body, plan->span, "core::Iterator::next returns o<Item> for one receiver");
        }
        plan->form = R_ITERATION_ITERATOR;
        plan->item_type = r_semantic_value_type(context, option->base);
    }
    if ((plan->declared_type != R_TYPE_ID_INVALID) && (plan->declared_type != plan->item_type)) {
        return r_iteration_diagnostic(
            body, plan->span, "the loop variable has the item type of the iterator");
    }
    return r_iteration_declare_hidden(body,
                                      scope_base,
                                      "$iterator",
                                      held_type,
                                      plan->statement.node,
                                      plan->span,
                                      value,
                                      statements,
                                      &plan->iterator);
}

/* R-STMT-0021 (L36): a borrowed receiver, or a borrowed object whose type implements
   core::AsyncIterator and neither core::Iterator nor core::LendingIterator, is advanced by
   awaiting; the object is a named local or parameter, borrowed again at each iteration. */
static bool r_iteration_plan_async(RBodyContext *body,
                                   RIterationPlan *plan,
                                   const RExpressionResult *value,
                                   bool *handled) {
    RFrontendContext *context = body->frontend;
    const RTypeId source_type = r_semantic_value_type(context, value->type);
    const RSemanticType *source = r_semantic_type(context, source_type);
    const RHirNode *place = r_body_hir_node(body, value->node);
    RSymbolId next = R_SYMBOL_ID_INVALID;
    RTypeId item = R_TYPE_ID_INVALID;
    bool receives;

    *handled = false;
    if (source == NULL) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    receives = (source->kind == R_SEMANTIC_TYPE_STANDARD) && (source->base != R_TYPE_ID_INVALID) &&
               r_semantic_standard_type_name_equal(context, source, "std.sync::receiver");
    if (receives) {
        item = source->base;
    } else {
        RSymbolId synchronous = R_SYMBOL_ID_INVALID;
        if (context->core_async_iterator_trait == UINT32_C(0)) {
            return true;
        }
        if (!r_iteration_resolve_trait_method(body,
                                              source_type,
                                              context->core_iterator_trait,
                                              "next",
                                              plan->span,
                                              &synchronous) ||
            ((synchronous == R_SYMBOL_ID_INVALID) &&
             !r_iteration_resolve_trait_method(body,
                                               source_type,
                                               context->core_lending_iterator_trait,
                                               "next",
                                               plan->span,
                                               &synchronous))) {
            return false;
        }
        if ((synchronous != R_SYMBOL_ID_INVALID) || body->poisoned) {
            return true;
        }
        if (!r_iteration_resolve_trait_method(body,
                                              source_type,
                                              context->core_async_iterator_trait,
                                              "next",
                                              plan->span,
                                              &next)) {
            return false;
        }
        if (next == R_SYMBOL_ID_INVALID) {
            return true;
        }
        const RSemanticSymbol *function = r_body_symbol(body, next);
        const RSemanticType *option =
            function == NULL
                ? NULL
                : r_semantic_type(context, r_semantic_value_type(context, function->return_type));
        if ((option == NULL) || (option->kind != R_SEMANTIC_TYPE_OPTION) || !function->is_async) {
            context->resource_status = R_FRONTEND_INTERNAL_ERROR;
            return false;
        }
        item = r_semantic_value_type(context, option->base);
    }
    *handled = true;
    if (!body->is_async || (body->finally_depth != UINT32_C(0))) {
        return r_body_diagnostic(body,
                                 "R-DIAG-ASYNC-001",
                                 "R-STMT-0021",
                                 "an asynchronous for requires an async body outside finally",
                                 plan->span);
    }
    if ((place == NULL) || (place->kind != R_HIR_PLACE) || (value->symbol == R_SYMBOL_ID_INVALID) ||
        (place->symbol != value->symbol)) {
        return r_body_diagnostic(body,
                                 "R-DIAG-TYPE-001",
                                 "R-STMT-0021",
                                 "an asynchronous for advances a named local object or parameter",
                                 plan->span);
    }
    if (!receives && (body->current_task_scope == R_SYMBOL_ID_INVALID)) {
        return r_body_diagnostic(body,
                                 "R-DIAG-ASYNC-001",
                                 "R-STMT-0017",
                                 "a for over a core::AsyncIterator starts its scoped next in an "
                                 "enclosing task_scope",
                                 plan->span);
    }
    if ((plan->declared_type != R_TYPE_ID_INVALID) && (plan->declared_type != item)) {
        return r_iteration_diagnostic(
            body, plan->span, "the loop variable has the item type of the iterator");
    }
    plan->form = R_ITERATION_ASYNC;
    plan->source = value->symbol;
    plan->async_receive = receives;
    plan->next_function = next;
    plan->item_type = item;
    return true;
}

static bool r_iteration_plan_iterable(RBodyContext *body,
                                      RIterationPlan *plan,
                                      RAstRef iterable,
                                      size_t scope_base,
                                      RHirVector *statements) {
    RFrontendContext *context = body->frontend;
    RAstRef operand;
    RExpressionResult value = {0};
    const RSemanticType *type;

    r_body_expression_invalid(&value);
    if (r_iteration_borrowed_operand(body, iterable, &operand)) {
        /* `&place`: a sequence, a container cursor, or an iterator advanced in place. */
        if (!r_body_lower_expression(body, operand, R_TYPE_ID_INVALID, &value)) {
            return false;
        }
        if (!value.valid) {
            return true;
        }
        if (!value.is_place) {
            return r_iteration_diagnostic(body, plan->span, "a borrowed iterable shall be a place");
        }
        if (!r_body_require_initialized_symbol(body, value.symbol, plan->span)) {
            return true;
        }
        type = r_semantic_type(context, r_semantic_value_type(context, value.type));
        if (type == NULL) {
            context->resource_status = R_FRONTEND_INTERNAL_ERROR;
            return false;
        }
        switch (type->kind) {
        case R_SEMANTIC_TYPE_FIXED_ARRAY:
        case R_SEMANTIC_TYPE_SLICE:
        case R_SEMANTIC_TYPE_ARRAY:
            return r_iteration_plan_sequence(body, plan, &value, scope_base, statements);
        case R_SEMANTIC_TYPE_LIST:
        case R_SEMANTIC_TYPE_DICT:
            if (plan->declared_type != R_TYPE_ID_INVALID) {
                RTypeId item;
                RTypeId cursor_type;
                const RSemanticType *cursor;
                if (!r_semantic_intern_parametric_standard_type(
                        context,
                        type->kind == R_SEMANTIC_TYPE_DICT ? "std.dict::iter" : "std.list::iter",
                        type->base,
                        type->kind == R_SEMANTIC_TYPE_DICT ? type->second : R_TYPE_ID_INVALID,
                        &cursor_type)) {
                    return false;
                }
                cursor = r_semantic_type(context, cursor_type);
                if ((cursor == NULL) ||
                    !r_iteration_cursor_item(
                        body, cursor, type->kind == R_SEMANTIC_TYPE_DICT, &item)) {
                    return false;
                }
                if (item != plan->declared_type) {
                    return r_iteration_diagnostic(
                        body,
                        plan->span,
                        "a borrowed list binds const T* and a borrowed dict binds "
                        "std.dict::entry_ref<K, V>");
                }
            }
            return r_iteration_plan_container_cursor(body, plan, &value, scope_base, statements);
        default: {
            RTypeId borrow_type;
            RExpressionResult borrow = {0};
            bool asynchronous = false;
            if (!r_iteration_plan_async(body, plan, &value, &asynchronous)) {
                return false;
            }
            if (asynchronous) {
                return true;
            }
            if (!r_body_standard_borrow_type(
                    body, r_semantic_value_type(context, value.type), false, &borrow_type) ||
                !r_body_form_safe_borrow(body, plan->span, borrow_type, &value, &borrow)) {
                return false;
            }
            if (!borrow.valid) {
                return true;
            }
            return r_iteration_plan_iterator(body, plan, &borrow, true, scope_base, statements);
        }
        }
    }
    /* A value: a moved iterator or cursor, a call result, or an exclusive borrow of one; a
       named borrow is reborrowed, a named Copy iterator is copied. */
    if (!r_body_lower_expression(body, iterable, R_TYPE_ID_INVALID, &value)) {
        return false;
    }
    if (!value.valid) {
        return true;
    }
    type = r_semantic_type(context, r_semantic_value_type(context, value.type));
    if (type == NULL) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    if (value.is_place && !r_body_require_initialized_symbol(body, value.symbol, plan->span)) {
        return true;
    }
    if (value.is_place && (type->kind == R_SEMANTIC_TYPE_BORROW) &&
        ((type->flags & R_SEMANTIC_TYPE_FLAG_NULLABLE) == 0U)) {
        const RSemanticType *container =
            r_semantic_type(context, r_semantic_value_type(context, type->base));
        RExpressionResult referent = {0};

        /* R-STMT-0014: a named borrow of a sequence or container iterates its referent. */
        if ((container != NULL) && ((container->kind == R_SEMANTIC_TYPE_FIXED_ARRAY) ||
                                    (container->kind == R_SEMANTIC_TYPE_SLICE) ||
                                    (container->kind == R_SEMANTIC_TYPE_ARRAY) ||
                                    (container->kind == R_SEMANTIC_TYPE_LIST) ||
                                    (container->kind == R_SEMANTIC_TYPE_DICT))) {
            if (!r_body_form_deref_place(body, value, plan->span, &referent)) {
                return false;
            }
            if (!referent.valid) {
                return true;
            }
            if ((container->kind == R_SEMANTIC_TYPE_LIST) ||
                (container->kind == R_SEMANTIC_TYPE_DICT)) {
                return r_iteration_plan_container_cursor(
                    body, plan, &referent, scope_base, statements);
            }
            return r_iteration_plan_sequence(body, plan, &referent, scope_base, statements);
        }
    }
    if (value.is_place && (type->kind == R_SEMANTIC_TYPE_BORROW)) {
        RExpressionResult referent = {0};
        RExpressionResult reborrow = {0};
        RTypeId borrow_type;
        if ((type->flags & (R_SEMANTIC_TYPE_FLAG_SHARED | R_SEMANTIC_TYPE_FLAG_NULLABLE)) != 0U) {
            return r_iteration_diagnostic(
                body, plan->span, "an iterator is advanced through an exclusive borrow");
        }
        if (!r_body_form_deref_place(body, value, plan->span, &referent)) {
            return false;
        }
        if (!referent.valid) {
            return true;
        }
        if (!r_body_standard_borrow_type(
                body, r_semantic_value_type(context, referent.type), false, &borrow_type) ||
            !r_body_form_safe_borrow(body, plan->span, borrow_type, &referent, &reborrow)) {
            return false;
        }
        if (!reborrow.valid) {
            return true;
        }
        return r_iteration_plan_iterator(body, plan, &reborrow, true, scope_base, statements);
    }
    if (value.is_place) {
        if (!r_semantic_type_is_copy(context, value.type)) {
            return r_body_diagnostic(body,
                                     "R-DIAG-MOVE-001",
                                     "R-OWN-0003",
                                     "a named Move iterator is consumed with move or advanced "
                                     "in place through &",
                                     plan->span);
        }
        if (!r_body_place_to_value(body, &value)) {
            return false;
        }
    }
    return r_iteration_plan_iterator(
        body, plan, &value, type->kind == R_SEMANTIC_TYPE_BORROW, scope_base, statements);
}

static bool r_iteration_parts(RBodyContext *body,
                              RAstRef statement,
                              RAstRef *type_ref,
                              RAstTokenView *name,
                              RAstRef *iterable,
                              RAstRef *block) {
    RAstNodeView view;
    uint32_t child_index;
    bool has_name = false;
    bool has_iterable = false;

    if (!r_ast_view_kind(body->frontend, statement, R_SYNTAX_FOR_IN_STATEMENT, &view) ||
        !r_ast_find_direct_child(body->frontend, statement, R_SYNTAX_TYPE, type_ref) ||
        !r_ast_find_direct_child(body->frontend, statement, R_SYNTAX_BLOCK, block)) {
        return false;
    }
    for (child_index = 0U; child_index < view.child_count; ++child_index) {
        RAstRef child;
        RAstNodeView child_view;
        RAstTokenView token;
        if (!r_ast_ref_child(body->frontend, statement, child_index, &child)) {
            return false;
        }
        if (r_ast_ref_token(body->frontend, child, &token)) {
            if (!has_name && (token.kind == R_TOKEN_IDENTIFIER)) {
                *name = token;
                has_name = true;
            }
            continue;
        }
        if (!r_ast_ref_view(body->frontend, child, &child_view)) {
            return false;
        }
        if (!has_iterable && ((child_view.kind == R_SYNTAX_EXPRESSION) ||
                              (child_view.kind == R_SYNTAX_RANGE_EXPRESSION))) {
            *iterable = child;
            has_iterable = true;
        }
    }
    return has_name && has_iterable;
}

/* The range-for core over explicit header parts; a comprehension clause supplies its own
   body through `lower_body` and receives the hidden locals, the loop and the trailing drops
   in `splice` instead of a block of their own (R-STMT-0014, R-EXPR-0030). */
static bool r_body_lower_for_in_parts(RBodyContext *body,
                                      RAstRef declaration,
                                      RSourceSpan span,
                                      RAstRef type_ref,
                                      RAstTokenView name,
                                      RAstRef iterable,
                                      RAstRef block,
                                      RIterationBodyFn lower_body,
                                      void *user,
                                      RHirVector *splice,
                                      RHirNodeId *node_id,
                                      bool *always_returns) {
    RAstNodeView iterable_view;
    RIterationPlan plan;
    const size_t for_scope_base = body->active_symbols.count;
    const size_t diagnostics_before = body->frontend->diagnostic_count;
    RHirVector wrapper = {0};
    RHirNodeId loop_node = R_HIR_NODE_ID_INVALID;
    RTypeId void_type;
    bool success = false;

    *node_id = R_HIR_NODE_ID_INVALID;
    *always_returns = false;
    (void)memset(&plan, 0, sizeof(plan));
    plan.statement = declaration;
    plan.block = block;
    plan.name = name;
    plan.lower_body = lower_body;
    plan.user = user;
    plan.declared_type = R_TYPE_ID_INVALID;
    if (!r_ast_ref_view(body->frontend, iterable, &iterable_view) ||
        !r_semantic_type_from_token(body->frontend, R_TOKEN_KW_VOID, &void_type)) {
        body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
        goto cleanup;
    }
    plan.span = span;
    if (!r_semantic_type_has_direct_token(body->frontend, type_ref, R_TOKEN_KW_AUTO)) {
        if (!r_semantic_type_from_ast(body->frontend, type_ref, &plan.declared_type)) {
            if (body->frontend->resource_status != R_FRONTEND_OK) {
                goto cleanup;
            }
            if (body->frontend->diagnostic_count == diagnostics_before) {
                success = r_iteration_diagnostic(body, span, "unresolved loop variable type");
                goto cleanup;
            }
            r_body_poison(body);
            success = true;
            goto cleanup;
        }
        if (r_semantic_value_kind(body->frontend, plan.declared_type) == R_SEMANTIC_TYPE_VOID) {
            success = r_iteration_diagnostic(body, span, "a loop variable cannot have void type");
            goto cleanup;
        }
    }
    if (iterable_view.kind == R_SYNTAX_RANGE_EXPRESSION) {
        if (!r_iteration_plan_range(body, &plan, iterable, for_scope_base, &wrapper)) {
            goto cleanup;
        }
    } else if (!r_iteration_plan_iterable(body, &plan, iterable, for_scope_base, &wrapper)) {
        goto cleanup;
    }
    if (body->poisoned || (body->frontend->diagnostic_count != diagnostics_before)) {
        /* The header did not lower; the body still names its variable, so it is skipped. */
        success = true;
        goto cleanup;
    }
    if (!r_body_lower_loop(body,
                           span,
                           plan.block,
                           r_iteration_lower_condition,
                           r_iteration_lower_body,
                           &plan,
                           &loop_node,
                           always_returns) ||
        ((loop_node != R_HIR_NODE_ID_INVALID) &&
         !r_hir_vector_push(body->frontend, &wrapper, loop_node))) {
        goto cleanup;
    }
    if (!*always_returns) {
        size_t drop_index = body->active_symbols.count;
        while (drop_index > for_scope_base) {
            drop_index -= 1U;
            if (!r_body_append_implicit_drop(
                    body, body->active_symbols.items[drop_index], span, &wrapper)) {
                goto cleanup;
            }
        }
    }
    if (splice != NULL) {
        uint32_t index;
        for (index = UINT32_C(0); index < wrapper.count; ++index) {
            if (!r_hir_vector_push(body->frontend, splice, wrapper.items[index])) {
                goto cleanup;
            }
        }
    } else if (!r_body_append_node(body,
                                   R_HIR_BLOCK,
                                   span,
                                   void_type,
                                   R_SYMBOL_ID_INVALID,
                                   R_TOKEN_INVALID,
                                   UINT64_C(0),
                                   false,
                                   &wrapper,
                                   node_id)) {
        goto cleanup;
    }
    success = true;

cleanup:
    body->active_symbols.count = for_scope_base;
    r_context_free(body->frontend, wrapper.items);
    return success;
}

/* for-in-statement = "for", "(", ( type | "auto" ), identifier, "in", iterable, ")", block */
static bool r_body_lower_for_in(RBodyContext *body,
                                RAstRef statement,
                                RHirNodeId *node_id,
                                bool *always_returns) {
    RAstNodeView view;
    RAstRef type_ref;
    RAstRef iterable;
    RAstRef block;
    RAstTokenView name;

    *node_id = R_HIR_NODE_ID_INVALID;
    *always_returns = false;
    if (!r_ast_ref_view(body->frontend, statement, &view) ||
        !r_iteration_parts(body, statement, &type_ref, &name, &iterable, &block)) {
        body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    return r_body_lower_for_in_parts(body,
                                     statement,
                                     view.span,
                                     type_ref,
                                     name,
                                     iterable,
                                     block,
                                     NULL,
                                     NULL,
                                     NULL,
                                     node_id,
                                     always_returns);
}

/* --- membership --------------------------------------------------------------------- */

static bool r_membership_diagnostic(RBodyContext *body, RSourceSpan span, const char *message) {
    return r_body_diagnostic(body, "R-DIAG-TYPE-001", "R-EXPR-0029", message, span);
}

static bool r_membership_range(RBodyContext *body,
                               RAstRef left_ref,
                               RAstRef range,
                               RSourceSpan span,
                               RExpressionResult *result) {
    RAstNodeView view;
    RAstRef bounds[2];
    uint32_t bound_count = 0U;
    RExpressionResult subject = {0}, subject_again = {0}, low = {0}, high = {0};
    RExpressionResult lower_bound = {0}, upper_bound = {0};
    RHirVector prefix = {0};
    const size_t scope_base = body->active_symbols.count;
    RSymbolId temporary;
    bool success = false;
    r_body_expression_invalid(result);
    if (!r_ast_ref_view(body->frontend, range, &view))
        return r_body_invariant(body);
    for (uint32_t i = 0U; i < view.child_count; ++i) {
        RAstRef child;
        RAstTokenView token;
        if (!r_ast_ref_child(body->frontend, range, i, &child))
            return r_body_invariant(body);
        if (r_ast_ref_token(body->frontend, child, &token))
            continue;
        if (bound_count == 2U)
            return r_body_invariant(body);
        bounds[bound_count++] = child;
    }
    if (bound_count != 2U)
        return r_body_invariant(body);
    if (!r_iteration_lower_bound(body, left_ref, R_TYPE_ID_INVALID, &subject))
        goto cleanup;
    if (!subject.valid) {
        success = true;
        goto cleanup;
    }
    const RTypeId subject_type = r_semantic_value_type(body->frontend, subject.type);
    if (!r_semantic_type_is_integer(body->frontend, subject_type) &&
        r_semantic_value_kind(body->frontend, subject_type) != R_SEMANTIC_TYPE_CHAR) {
        success = r_membership_diagnostic(
            body, span, "membership in a range requires an integer or char operand");
        goto cleanup;
    }
    /* One subject evaluation precedes both comparisons. The upper bound stays lazy. */
    if (!r_iteration_declare_hidden(body,
                                    scope_base,
                                    "$membership",
                                    subject_type,
                                    left_ref.node,
                                    span,
                                    &subject,
                                    &prefix,
                                    &temporary) ||
        !r_body_symbol_place(body, temporary, span, &subject) ||
        !r_body_place_to_value(body, &subject) ||
        !r_iteration_lower_bound(body, bounds[0], subject_type, &low))
        goto cleanup;
    if (!low.valid) {
        success = true;
        goto cleanup;
    }
    if (r_semantic_value_type(body->frontend, low.type) != subject_type) {
        success = r_membership_diagnostic(
            body, span, "the bounds of a range have the type of the tested operand");
        goto cleanup;
    }
    if (!r_iteration_compare(body, span, R_TOKEN_GREATER_EQUAL, &subject, &low, &lower_bound))
        goto cleanup;
    if (!lower_bound.valid) {
        success = true;
        goto cleanup;
    }
    RLogicalRightObjectState state;
    if (!r_body_begin_logical_right_operand(body, &lower_bound, R_TOKEN_AMP_AMP, &state))
        goto cleanup;
    const bool lowered = r_body_symbol_place(body, temporary, span, &subject_again) &&
                         r_body_place_to_value(body, &subject_again) &&
                         r_iteration_lower_bound(body, bounds[1], subject_type, &high);
    if (!r_body_finish_logical_right_operand(body, &state, span, lowered))
        goto cleanup;
    if (!high.valid) {
        success = true;
        goto cleanup;
    }
    if (r_semantic_value_type(body->frontend, high.type) != subject_type) {
        success = r_membership_diagnostic(
            body, span, "the bounds of a range have the type of the tested operand");
        goto cleanup;
    }
    if (!r_iteration_compare(body, span, R_TOKEN_LESS, &subject_again, &high, &upper_bound))
        goto cleanup;
    if (!upper_bound.valid) {
        success = true;
        goto cleanup;
    }
    success =
        r_body_combine_boolean(body, R_TOKEN_AMP_AMP, span, &lower_bound, &upper_bound, result) &&
        r_body_wrap_value_scope(body, &prefix, result, NULL);
cleanup:
    body->active_symbols.count = scope_base;
    r_context_free(body->frontend, prefix.items);
    return success;
}

/* The tested operand of a container membership is borrowed, so it names a place. */
static bool r_membership_subject(RBodyContext *body,
                                 RAstRef left_ref,
                                 RTypeId expected_type,
                                 RSourceSpan span,
                                 RExpressionResult *subject) {
    if (!r_body_lower_expression(body, left_ref, expected_type, subject)) {
        return false;
    }
    if (!subject->valid) {
        return true;
    }
    if (!subject->is_place) {
        return r_membership_diagnostic(
            body, span, "the operand tested against a container shall name a place");
    }
    if (r_semantic_value_type(body->frontend, subject->type) != expected_type) {
        return r_membership_diagnostic(
            body, span, "the tested operand has the item type of the container");
    }
    if (!r_body_require_initialized_symbol(body, subject->symbol, span)) {
        r_body_expression_invalid(subject);
    }
    return true;
}

static bool r_membership_container(RBodyContext *body,
                                   RAstRef left_ref,
                                   RAstRef right_ref,
                                   RSourceSpan span,
                                   RExpressionResult *result) {
    RFrontendContext *context = body->frontend;
    RExpressionResult container = {0};
    RExpressionResult subject = {0};
    RExpressionResult target = {0};
    RExpressionResult key = {0};
    const RSemanticType *type;
    RTypeId container_type;
    RTypeId bool_type;
    RHirNodeId children_storage[2];
    RHirVector children;

    r_body_expression_invalid(result);
    if (!r_body_lower_expression(body, right_ref, R_TYPE_ID_INVALID, &container) ||
        !r_semantic_type_from_token(context, R_TOKEN_KW_BOOL, &bool_type)) {
        return false;
    }
    if (!container.valid) {
        return true;
    }
    container_type = r_semantic_value_type(context, container.type);
    type = r_semantic_type(context, container_type);
    if ((type != NULL) && (type->kind == R_SEMANTIC_TYPE_BORROW) &&
        ((type->flags & R_SEMANTIC_TYPE_FLAG_NULLABLE) == 0U)) {
        /* A borrowed container is tested through the borrow. */
        RExpressionResult referent = {0};
        if (!r_body_form_deref_place(body, container, span, &referent)) {
            return false;
        }
        if (!referent.valid) {
            return true;
        }
        container = referent;
        container_type = r_semantic_value_type(context, container.type);
        type = r_semantic_type(context, container_type);
    }
    if (type == NULL) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    if (!container.is_place) {
        return r_membership_diagnostic(body, span, "a container is tested for membership in place");
    }
    if (type->kind == R_SEMANTIC_TYPE_DICT) {
        const RTypeId key_type = type->base;
        const RTypeId value_type = type->second;
        RTypeId key_borrow_type;
        RTypeId target_borrow_type;
        if (!r_membership_subject(body, left_ref, key_type, span, &subject)) {
            return false;
        }
        if (!subject.valid || body->poisoned) {
            return true;
        }
        if (!r_body_standard_borrow_type(body, key_type, true, &key_borrow_type) ||
            !r_body_form_safe_borrow(body, span, key_borrow_type, &subject, &key) ||
            !r_body_standard_borrow_type(body, container_type, true, &target_borrow_type) ||
            !r_body_form_safe_borrow(body, span, target_borrow_type, &container, &target)) {
            return false;
        }
        if (!key.valid || !target.valid) {
            return true;
        }
        children_storage[0] = target.node;
        children_storage[1] = key.node;
        children.items = children_storage;
        children.count = UINT32_C(2);
        children.capacity = UINT32_C(2);
        if (!r_body_append_standard_call(body,
                                         span,
                                         bool_type,
                                         value_type,
                                         R_STANDARD_CALL_DICT_CONTAINS,
                                         &children,
                                         NULL,
                                         result)) {
            return false;
        }
        r_body_hir_node(body, result->node)->runtime_type = key_type;
        return true;
    }
    {
        /* core::Contains: `contains(const Self* this, const Item* value)` (R-TYPE-0046). */
        RSymbolId function_id;
        const RSemanticSymbol *function;
        RTypeId receiver_type;
        RTypeId argument_type;
        const RSemanticType *argument;
        RTypeId item_type;
        RHirNodeId call;
        if (!r_iteration_resolve_trait_method(body,
                                              container_type,
                                              context->core_contains_trait,
                                              "contains",
                                              span,
                                              &function_id)) {
            return false;
        }
        if (function_id == R_SYMBOL_ID_INVALID) {
            if (body->poisoned) {
                return true;
            }
            return r_membership_diagnostic(
                body,
                span,
                "membership requires a dict, a range, or a core::Contains implementation");
        }
        function = r_body_symbol(body, function_id);
        if ((function == NULL) || (function->parameter_count != UINT32_C(2))) {
            context->resource_status = R_FRONTEND_INTERNAL_ERROR;
            return false;
        }
        receiver_type = context->semantic_parameter_types[function->first_parameter_type];
        argument_type = context->semantic_parameter_types[function->first_parameter_type + 1U];
        argument = r_semantic_type(context, argument_type);
        if ((argument == NULL) || (argument->kind != R_SEMANTIC_TYPE_BORROW)) {
            context->resource_status = R_FRONTEND_INTERNAL_ERROR;
            return false;
        }
        item_type = r_semantic_value_type(context, argument->base);
        if (!r_membership_subject(body, left_ref, item_type, span, &subject)) {
            return false;
        }
        if (!subject.valid || body->poisoned) {
            return true;
        }
        if (!r_body_form_safe_borrow(body, span, receiver_type, &container, &target) ||
            !r_body_form_safe_borrow(body, span, argument_type, &subject, &key)) {
            return false;
        }
        if (!target.valid || !key.valid) {
            return true;
        }
        children_storage[0] = target.node;
        children_storage[1] = key.node;
        children.items = children_storage;
        children.count = UINT32_C(2);
        children.capacity = UINT32_C(2);
        if (!r_body_append_node(body,
                                R_HIR_CALL,
                                span,
                                function->return_type,
                                function_id,
                                R_TOKEN_INVALID,
                                UINT64_C(0),
                                false,
                                &children,
                                &call)) {
            return false;
        }
        r_body_hir_node(body, call)->auxiliary_type = function->effect_carrier_type;
        r_body_hir_node(body, call)->borrow_origin = R_SYMBOL_ID_INVALID;
        result->node = call;
        result->type = function->return_type;
        result->symbol = function_id;
        result->valid = true;
        return true;
    }
}

/* membership-expression = value-expression, [ "not" ], "in", ( expression | range-expression ) */
static bool r_body_lower_membership(RBodyContext *body,
                                    RAstRef node,
                                    RTypeId contextual_type,
                                    RExpressionResult *result) {
    RAstNodeView view;
    RAstRef operands[2];
    RAstNodeView operand_views[2];
    uint32_t operand_count = 0U;
    uint32_t child_index;
    bool negated = false;
    RExpressionResult test = {0};

    (void)contextual_type;
    r_body_expression_invalid(result);
    r_body_expression_invalid(&test);
    if (!r_ast_view_kind(body->frontend, node, R_SYNTAX_MEMBERSHIP_EXPRESSION, &view)) {
        body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    for (child_index = 0U; child_index < view.child_count; ++child_index) {
        RAstRef child;
        RAstTokenView token;
        if (!r_ast_ref_child(body->frontend, node, child_index, &child)) {
            body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
            return false;
        }
        if (r_ast_ref_token(body->frontend, child, &token)) {
            negated = negated || (token.kind == R_TOKEN_IDENTIFIER);
            continue;
        }
        if (operand_count == 2U) {
            body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
            return false;
        }
        if (!r_ast_ref_view(body->frontend, child, &operand_views[operand_count])) {
            body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
            return false;
        }
        operands[operand_count++] = child;
    }
    if (operand_count != 2U) {
        body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    if (operand_views[1].kind == R_SYNTAX_RANGE_EXPRESSION) {
        if (!r_membership_range(body, operands[0], operands[1], view.span, &test)) {
            return false;
        }
    } else if (!r_membership_container(body, operands[0], operands[1], view.span, &test)) {
        return false;
    }
    if (!test.valid) {
        return true;
    }
    if (negated) {
        RHirNodeId children_storage[1];
        RHirVector children;
        RHirNodeId negation;
        children_storage[0] = test.node;
        children.items = children_storage;
        children.count = UINT32_C(1);
        children.capacity = UINT32_C(1);
        if (!r_body_append_node(body,
                                R_HIR_UNARY,
                                view.span,
                                test.type,
                                R_SYMBOL_ID_INVALID,
                                R_TOKEN_BANG,
                                UINT64_C(0),
                                false,
                                &children,
                                &negation)) {
            return false;
        }
        test.node = negation;
    }
    *result = test;
    return true;
}

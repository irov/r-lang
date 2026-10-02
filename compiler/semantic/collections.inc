/* R-EXPR-0030: collection expressions. An array expression `[e, ...]`, a dict expression
 * `{k: v, ...}` and their comprehension forms lower to a hidden container created before the
 * value, filled by the push and insert operations of the standard library (each with its own
 * checked effect), and moved out as the value of the expression. A comprehension clause is the
 * range-for core of R-STMT-0014 whose body is the next clause or the element insertion. */

typedef struct RCollectionPlan {
    RAstRef node;
    RSourceSpan span;
    bool is_dict;
    RTypeId container_type;
    /* The element type of an array or the value type of a dict. */
    RTypeId element_type;
    RTypeId key_type;
    RSymbolId container;
    /* The element expression, or the dict entry, of a comprehension. */
    RAstRef element;
    RAstRef clauses[16];
    uint32_t clause_count;
    uint32_t next_clause;
} RCollectionPlan;

static bool r_collection_diagnostic(RBodyContext *body, RSourceSpan span, const char *message) {
    return r_body_diagnostic(body, "R-DIAG-TYPE-001", "R-EXPR-0030", message, span);
}

/* Wraps hidden declarations before and drops after the value of one expression. */
static bool r_body_wrap_value_scope(RBodyContext *body,
                                    RHirVector *prefix,
                                    RExpressionResult *value,
                                    const RHirVector *suffix) {
    const uint64_t result_index = (uint64_t)prefix->count;
    RHirNodeId scope;
    RSourceSpan span;
    uint32_t index;

    if (((prefix->count == 0U) && ((suffix == NULL) || (suffix->count == 0U))) || !value->valid) {
        return true;
    }
    span = r_body_hir_node(body, value->node)->span;
    if (!r_hir_vector_push(body->frontend, prefix, value->node)) {
        return false;
    }
    for (index = 0U; (suffix != NULL) && (index < suffix->count); ++index) {
        if (!r_hir_vector_push(body->frontend, prefix, suffix->items[index])) {
            return false;
        }
    }
    if (!r_body_append_node(body,
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

/* std.array::create::<T>() or std.dict::create::<K, V>() for the hidden container. */
static bool
r_collection_create(RBodyContext *body, const RCollectionPlan *plan, RExpressionResult *result) {
    RHirNodeId node;

    r_body_expression_invalid(result);
    if (plan->is_dict) {
        if (!r_body_append_standard_call(body,
                                         plan->span,
                                         plan->container_type,
                                         plan->element_type,
                                         R_STANDARD_CALL_DICT_CREATE,
                                         NULL,
                                         NULL,
                                         result)) {
            return false;
        }
        r_body_hir_node(body, result->node)->runtime_type = plan->key_type;
        return true;
    }
    if (!r_body_append_node(body,
                            R_HIR_STANDARD_CALL,
                            plan->span,
                            plan->container_type,
                            R_SYMBOL_ID_INVALID,
                            R_TOKEN_KW_ARRAY,
                            UINT64_C(0),
                            false,
                            NULL,
                            &node)) {
        return false;
    }
    r_body_hir_node(body, node)->auxiliary_type = plan->element_type;
    r_body_hir_node(body, node)->standard_operation = R_STANDARD_CALL_ARRAY_CREATE;
    result->node = node;
    result->type = plan->container_type;
    result->valid = true;
    return true;
}

/* The exclusive borrow of the hidden container that every insertion takes. */
static bool
r_collection_target(RBodyContext *body, const RCollectionPlan *plan, RExpressionResult *target) {
    RExpressionResult place = {0};
    RTypeId borrow_type;

    r_body_expression_invalid(target);
    return r_body_symbol_place(body, plan->container, plan->span, &place) &&
           r_body_standard_borrow_type(body, plan->container_type, false, &borrow_type) &&
           r_body_form_safe_borrow(body, plan->span, borrow_type, &place, target);
}

/* The effect set and carrier of one insertion: std.array::push_error(T) or
   std.dict::insert_error(K, V). */
static bool r_collection_effect(RBodyContext *body,
                                const RCollectionPlan *plan,
                                RTypeId result_type,
                                RTypeId *effect_set,
                                RTypeId *carrier) {
    RTypeId error_name_type;
    RTypeId error_type;
    const RSemanticType *error_name;
    uint64_t error_name_length;

    if (!r_semantic_intern_named_standard_type(body->frontend,
                                               plan->is_dict ? "std.dict::insert_error"
                                                             : "std.array::push_error",
                                               &error_name_type)) {
        return false;
    }
    error_name = r_semantic_type(body->frontend, error_name_type);
    if (error_name == NULL) {
        body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    error_name_length = error_name->length;
    return r_semantic_intern_derived_type(body->frontend,
                                          R_SEMANTIC_TYPE_STANDARD,
                                          plan->is_dict ? plan->key_type : plan->element_type,
                                          plan->is_dict ? plan->element_type : R_TYPE_ID_INVALID,
                                          error_name_length,
                                          R_SEMANTIC_TYPE_FLAG_NONE,
                                          &error_type) &&
           r_semantic_single_effect_set(body->frontend, error_type, effect_set) &&
           r_semantic_intern_effect_carrier(body->frontend, result_type, *effect_set, carrier) &&
           r_body_validate_effect_set(body, *effect_set, plan->span);
}

/* `std.array::push(&container, value);` as one statement. */
static bool r_collection_push(RBodyContext *body,
                              const RCollectionPlan *plan,
                              const RExpressionResult *value,
                              RHirVector *statements) {
    RExpressionResult target = {0};
    RExpressionResult call = {0};
    RTypeId void_type;
    RTypeId effect_set;
    RTypeId carrier;
    RHirNodeId children_storage[2];
    RHirVector children;
    RHirNodeId node;

    if (!value->valid) {
        return true;
    }
    if (!r_collection_target(body, plan, &target)) {
        return false;
    }
    if (!target.valid) {
        return true;
    }
    if (r_semantic_type_contains_borrow(body->frontend, plan->element_type) ||
        r_semantic_type_contains_slice(body->frontend, plan->element_type)) {
        bool rejected = false;

        if (!r_body_store_container_value(body, &target, value, plan->span, &rejected)) {
            return false;
        }
        if (rejected) {
            return true;
        }
    }
    if (!r_semantic_type_from_token(body->frontend, R_TOKEN_KW_VOID, &void_type) ||
        !r_collection_effect(body, plan, void_type, &effect_set, &carrier)) {
        return false;
    }
    children_storage[0] = target.node;
    children_storage[1] = value->node;
    children.items = children_storage;
    children.count = UINT32_C(2);
    children.capacity = UINT32_C(2);
    if (!r_body_append_node(body,
                            R_HIR_STANDARD_CALL,
                            plan->span,
                            void_type,
                            R_SYMBOL_ID_INVALID,
                            R_TOKEN_KW_ARRAY,
                            UINT64_C(0),
                            false,
                            &children,
                            &node)) {
        return false;
    }
    r_body_hir_node(body, node)->auxiliary_type = carrier;
    r_body_hir_node(body, node)->standard_operation = R_STANDARD_CALL_ARRAY_PUSH;
    r_body_hir_node(body, node)->borrow_origin = R_SYMBOL_ID_INVALID;
    const RExpressionResult *const staged[1] = {value};
    if (!r_body_propagate_staged_error_origins(body, effect_set, staged, 1U, plan->span) ||
        !r_body_attach_effect_exits(body, node, effect_set, plan->span)) {
        return false;
    }
    call.node = node;
    call.type = void_type;
    call.valid = true;
    return r_iteration_expression_statement(body, plan->span, &call, statements);
}

/* `o(V) previous = std.dict::insert(&container, key, value);` followed by the drop of the
   replaced value; the hidden local leaves the scope immediately. */
static bool r_collection_insert(RBodyContext *body,
                                const RCollectionPlan *plan,
                                const RExpressionResult *key,
                                const RExpressionResult *value,
                                RHirVector *statements) {
    RExpressionResult target = {0};
    RExpressionResult call = {0};
    RTypeId result_type;
    RTypeId effect_set;
    RTypeId carrier;
    RTypeId void_type;
    RHirNodeId children_storage[3];
    RHirVector children;
    RSymbolId previous = R_SYMBOL_ID_INVALID;
    const size_t scope_base = body->active_symbols.count;
    bool success = false;

    if (!key->valid || !value->valid) {
        return true;
    }
    if (!r_collection_target(body, plan, &target)) {
        return false;
    }
    if (!target.valid) {
        return true;
    }
    for (uint32_t part = 0U; part < 2U; ++part) {
        const RExpressionResult *stored = part == 0U ? key : value;
        const RTypeId stored_type = part == 0U ? plan->key_type : plan->element_type;
        bool rejected = false;

        if ((r_semantic_type_contains_borrow(body->frontend, stored_type) ||
             r_semantic_type_contains_slice(body->frontend, stored_type)) &&
            !r_body_store_container_value(body, &target, stored, plan->span, &rejected)) {
            return false;
        }
        if (rejected) {
            return true;
        }
    }
    if (!r_semantic_type_from_token(body->frontend, R_TOKEN_KW_VOID, &void_type) ||
        !r_body_standard_option_type(body, plan->element_type, &result_type) ||
        !r_collection_effect(body, plan, result_type, &effect_set, &carrier)) {
        return false;
    }
    children_storage[0] = target.node;
    children_storage[1] = key->node;
    children_storage[2] = value->node;
    children.items = children_storage;
    children.count = UINT32_C(3);
    children.capacity = UINT32_C(3);
    if (!r_body_append_standard_call(body,
                                     plan->span,
                                     result_type,
                                     carrier,
                                     R_STANDARD_CALL_DICT_INSERT,
                                     &children,
                                     NULL,
                                     &call)) {
        return false;
    }
    r_body_hir_node(body, call.node)->runtime_type = plan->key_type;
    const RExpressionResult *const staged[2] = {key, value};
    if (!r_body_propagate_staged_error_origins(body, effect_set, staged, 2U, plan->span) ||
        !r_body_attach_effect_exits(body, call.node, effect_set, plan->span) ||
        !r_iteration_declare_hidden(body,
                                    scope_base,
                                    "$previous",
                                    result_type,
                                    plan->node.node,
                                    plan->span,
                                    &call,
                                    statements,
                                    &previous)) {
        goto cleanup;
    }
    if (!r_body_append_implicit_drop(body, previous, plan->span, statements)) {
        goto cleanup;
    }
    success = true;

cleanup:
    body->active_symbols.count = scope_base;
    return success;
}

/* One element or entry value with the element type as its context. */
static bool r_collection_value(RBodyContext *body,
                               RAstRef expression,
                               RTypeId expected_type,
                               RExpressionResult *value) {
    if (!r_iteration_lower_bound(body, expression, expected_type, value)) {
        return false;
    }
    if (value->valid && (r_semantic_value_type(body->frontend, value->type) != expected_type)) {
        RAstNodeView view;
        r_body_expression_invalid(value);
        return r_ast_ref_view(body->frontend, expression, &view) &&
               r_collection_diagnostic(
                   body, view.span, "the element does not have the element type of the collection");
    }
    return true;
}

/* The key and value of one dict entry. */
static bool
r_collection_entry_parts(RBodyContext *body, RAstRef entry, RAstRef *key_ref, RAstRef *value_ref) {
    RAstNodeView view;
    uint32_t child_index;
    uint32_t found = 0U;

    if (!r_ast_view_kind(body->frontend, entry, R_SYNTAX_DICT_ENTRY, &view)) {
        return false;
    }
    for (child_index = 0U; child_index < view.child_count; ++child_index) {
        RAstRef child;
        RAstTokenView token;
        if (!r_ast_ref_child(body->frontend, entry, child_index, &child)) {
            return false;
        }
        if (r_ast_ref_token(body->frontend, child, &token)) {
            continue;
        }
        if (found == 0U) {
            *key_ref = child;
        } else if (found == 1U) {
            *value_ref = child;
        } else {
            return false;
        }
        found += 1U;
    }
    return found == 2U;
}

/* The insertion of one element or entry, as the innermost statement of a comprehension or as
   one item of a literal. */
static bool r_collection_insert_element(RBodyContext *body,
                                        const RCollectionPlan *plan,
                                        RAstRef element,
                                        RHirVector *statements) {
    RExpressionResult value = {0};
    RExpressionResult key = {0};
    RAstRef key_ref;
    RAstRef value_ref;

    if (!plan->is_dict) {
        return r_collection_value(body, element, plan->element_type, &value) &&
               r_collection_push(body, plan, &value, statements);
    }
    if (!r_collection_entry_parts(body, element, &key_ref, &value_ref)) {
        body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    return r_collection_value(body, key_ref, plan->key_type, &key) &&
           r_collection_value(body, value_ref, plan->element_type, &value) &&
           r_collection_insert(body, plan, &key, &value, statements);
}

static bool r_collection_lower_rest(RBodyContext *body,
                                    void *user,
                                    RHirVector *statements,
                                    bool *always_returns);

/* `if (condition) { rest }` inside a comprehension, with the object-state merge of an
   else-less if statement. */
static bool r_collection_lower_if(RBodyContext *body,
                                  RCollectionPlan *plan,
                                  RAstRef condition_ref,
                                  RHirNodeId *node_id) {
    RExpressionResult condition = {0};
    RSemanticObjectSnapshot *entry_states = NULL;
    RSemanticObjectSnapshot *then_states = NULL;
    RSemanticObjectSnapshot *else_states = NULL;
    size_t entry_state_count = 0U;
    size_t then_state_count = 0U;
    size_t else_state_count = 0U;
    RHirNodeId block_node = R_HIR_NODE_ID_INVALID;
    RHirNodeId children_storage[2];
    RHirVector children;
    RHirVector block = {0};
    RTypeId void_type;
    bool then_returns = false;
    bool condition_value = false;
    bool condition_is_constant;
    const bool previous_transfer_suppression = body->suppress_enclosing_loop_transfers;
    const bool previous_control_transfer_suppression =
        body->suppress_control_transfer_state_recording;
    const size_t previous_transfer_depth = body->suppressed_loop_transfer_depth;
    const size_t scope_base = body->active_symbols.count;
    bool success = false;

    *node_id = R_HIR_NODE_ID_INVALID;
    if (!r_semantic_type_from_token(body->frontend, R_TOKEN_KW_VOID, &void_type) ||
        !r_body_lower_condition(body, condition_ref, &condition) ||
        !r_body_capture_active_object_states(body, &entry_states, &entry_state_count) ||
        !r_body_refine_nullable_condition(body, condition.node, true)) {
        goto cleanup;
    }
    condition_is_constant = r_body_constant_bool(body, condition.node, &condition_value);
    if (condition_is_constant && !condition_value) {
        body->suppress_enclosing_loop_transfers = true;
        body->suppressed_loop_transfer_depth = body->active_loop_scope_bases.count;
        body->suppress_control_transfer_state_recording = true;
    }
    if (!r_collection_lower_rest(body, plan, &block, &then_returns)) {
        body->suppress_enclosing_loop_transfers = previous_transfer_suppression;
        body->suppress_control_transfer_state_recording = previous_control_transfer_suppression;
        body->suppressed_loop_transfer_depth = previous_transfer_depth;
        goto cleanup;
    }
    body->suppress_enclosing_loop_transfers = previous_transfer_suppression;
    body->suppress_control_transfer_state_recording = previous_control_transfer_suppression;
    body->suppressed_loop_transfer_depth = previous_transfer_depth;
    body->active_symbols.count = scope_base;
    if (!r_body_append_node(body,
                            R_HIR_BLOCK,
                            plan->span,
                            void_type,
                            R_SYMBOL_ID_INVALID,
                            R_TOKEN_INVALID,
                            UINT64_C(0),
                            false,
                            &block,
                            &block_node) ||
        !r_body_capture_active_object_states(body, &then_states, &then_state_count) ||
        (then_state_count != entry_state_count) ||
        !r_body_restore_active_object_states(body, entry_states, entry_state_count) ||
        !r_body_refine_nullable_condition(body, condition.node, false) ||
        !r_body_capture_active_object_states(body, &else_states, &else_state_count) ||
        (else_state_count != entry_state_count)) {
        goto cleanup;
    }
    if (!then_returns && (!condition_is_constant || condition_value)) {
        if (!r_body_merge_branch_object_states(
                body, then_states, else_states, entry_state_count, plan->span)) {
            goto cleanup;
        }
    }
    children_storage[0] = condition.node;
    children_storage[1] = block_node;
    children.items = children_storage;
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
                                 node_id);

cleanup:
    body->active_symbols.count = scope_base;
    r_context_free(body->frontend, block.items);
    r_context_free(body->frontend, entry_states);
    r_context_free(body->frontend, then_states);
    r_context_free(body->frontend, else_states);
    return success;
}

/* The next comprehension clause, or the innermost insertion, appended to the statements of the
   enclosing loop body. */
static bool r_collection_lower_rest(RBodyContext *body,
                                    void *user,
                                    RHirVector *statements,
                                    bool *always_returns) {
    RCollectionPlan *plan = (RCollectionPlan *)user;
    RAstRef clause;
    RAstNodeView clause_view;

    *always_returns = false;
    if (plan->next_clause == plan->clause_count) {
        const size_t scope_base = body->active_symbols.count;
        const bool success = r_collection_insert_element(body, plan, plan->element, statements);
        body->active_symbols.count = scope_base;
        return success;
    }
    clause = plan->clauses[plan->next_clause];
    plan->next_clause += 1U;
    if (!r_ast_ref_view(body->frontend, clause, &clause_view)) {
        body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    if (clause_view.kind == R_SYNTAX_COMPREHENSION_IF) {
        RAstRef condition_ref;
        RHirNodeId node_id = R_HIR_NODE_ID_INVALID;
        if (!r_ast_find_direct_child(body->frontend, clause, R_SYNTAX_EXPRESSION, &condition_ref)) {
            body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
            return false;
        }
        return r_collection_lower_if(body, plan, condition_ref, &node_id) &&
               ((node_id == R_HIR_NODE_ID_INVALID) ||
                r_hir_vector_push(body->frontend, statements, node_id));
    }
    {
        RAstRef type_ref;
        RAstRef iterable;
        RAstTokenView name;
        RHirNodeId node_id = R_HIR_NODE_ID_INVALID;
        uint32_t child_index;
        bool has_name = false;
        bool has_iterable = false;
        if (!r_ast_find_direct_child(body->frontend, clause, R_SYNTAX_TYPE, &type_ref)) {
            body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
            return false;
        }
        for (child_index = 0U; child_index < clause_view.child_count; ++child_index) {
            RAstRef child;
            RAstNodeView child_view;
            RAstTokenView token;
            if (!r_ast_ref_child(body->frontend, clause, child_index, &child)) {
                body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
                return false;
            }
            if (r_ast_ref_token(body->frontend, child, &token)) {
                if (!has_name && (token.kind == R_TOKEN_IDENTIFIER)) {
                    name = token;
                    has_name = true;
                }
                continue;
            }
            if (r_ast_ref_view(body->frontend, child, &child_view) && !has_iterable &&
                ((child_view.kind == R_SYNTAX_EXPRESSION) ||
                 (child_view.kind == R_SYNTAX_RANGE_EXPRESSION))) {
                iterable = child;
                has_iterable = true;
            }
        }
        if (!has_name || !has_iterable) {
            body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
            return false;
        }
        return r_body_lower_for_in_parts(body,
                                         clause,
                                         clause_view.span,
                                         type_ref,
                                         name,
                                         iterable,
                                         plan->node,
                                         r_collection_lower_rest,
                                         plan,
                                         statements,
                                         &node_id,
                                         always_returns);
    }
}

/* Collects the elements or entries and the comprehension clauses of one expression. */
static bool r_collection_parts(RBodyContext *body,
                               RAstRef node,
                               RAstRef *items,
                               uint32_t item_capacity,
                               uint32_t *item_count,
                               RCollectionPlan *plan) {
    RAstNodeView view;
    uint32_t child_index;

    *item_count = 0U;
    plan->clause_count = 0U;
    if (!r_ast_ref_view(body->frontend, node, &view)) {
        return false;
    }
    for (child_index = 0U; child_index < view.child_count; ++child_index) {
        RAstRef child;
        RAstNodeView child_view;
        RAstTokenView token;
        if (!r_ast_ref_child(body->frontend, node, child_index, &child)) {
            return false;
        }
        if (r_ast_ref_token(body->frontend, child, &token)) {
            continue;
        }
        if (!r_ast_ref_view(body->frontend, child, &child_view)) {
            return false;
        }
        if ((child_view.kind == R_SYNTAX_COMPREHENSION_FOR) ||
            (child_view.kind == R_SYNTAX_COMPREHENSION_IF)) {
            if (plan->clause_count ==
                (uint32_t)(sizeof(plan->clauses) / sizeof(plan->clauses[0]))) {
                return r_collection_diagnostic(
                    body, child_view.span, "a comprehension has at most sixteen clauses");
            }
            plan->clauses[plan->clause_count++] = child;
            continue;
        }
        if (*item_count == item_capacity) {
            return r_collection_diagnostic(
                body, child_view.span, "a collection expression has at most 256 items");
        }
        items[(*item_count)++] = child;
    }
    return true;
}

static bool r_body_lower_collection_plan(RBodyContext *body,
                                         RCollectionPlan *plan,
                                         RTypeId contextual_type,
                                         RExpressionResult *result) {
    RAstRef items[256];
    uint32_t item_count = 0U;
    uint32_t index;
    const RSemanticType *type =
        r_semantic_type(body->frontend, r_semantic_value_type(body->frontend, contextual_type));
    RHirVector prefix = {0};
    RExpressionResult created = {0};
    RExpressionResult place = {0};
    RExpressionResult value = {0};
    bool body_returns = false;
    bool success = false;

    r_body_expression_invalid(result);
    if ((type == NULL) ||
        (type->kind != (plan->is_dict ? R_SEMANTIC_TYPE_DICT : R_SEMANTIC_TYPE_ARRAY))) {
        return r_collection_diagnostic(body,
                                       plan->span,
                                       plan->is_dict
                                           ? "a dict expression requires a dict<K, V> destination"
                                           : "an array expression requires an array<T> "
                                             "destination");
    }
    plan->container_type = r_semantic_value_type(body->frontend, contextual_type);
    plan->element_type = plan->is_dict ? type->second : type->base;
    plan->key_type = plan->is_dict ? type->base : R_TYPE_ID_INVALID;
    if (!r_semantic_type_is_container_element(body->frontend, plan->element_type)) {
        return r_body_unsupported(
            body, plan->span, "collection element has no storable value representation");
    }
    if (!r_collection_parts(body, plan->node, items, 256U, &item_count, plan)) {
        if (body->frontend->resource_status != R_FRONTEND_OK) {
            return false;
        }
        return true;
    }
    if ((plan->clause_count != 0U) && (item_count != 1U)) {
        return r_collection_diagnostic(
            body, plan->span, "a comprehension produces exactly one element per iteration");
    }
    if (!r_collection_create(body, plan, &created) ||
        !r_iteration_declare_hidden(body,
                                    body->active_symbols.count,
                                    "$collection",
                                    plan->container_type,
                                    plan->node.node,
                                    plan->span,
                                    &created,
                                    &prefix,
                                    &plan->container)) {
        goto cleanup;
    }
    if (plan->clause_count == 0U) {
        for (index = 0U; index < item_count; ++index) {
            if (!r_collection_insert_element(body, plan, items[index], &prefix)) {
                goto cleanup;
            }
        }
    } else {
        plan->element = items[0];
        plan->next_clause = 0U;
        if (!r_collection_lower_rest(body, plan, &prefix, &body_returns)) {
            goto cleanup;
        }
    }
    if (body->poisoned) {
        success = true;
        goto cleanup;
    }
    if (!r_body_symbol_place(body, plan->container, plan->span, &place) ||
        !r_body_move_from_place(body, place, plan->span, &value)) {
        goto cleanup;
    }
    if (!value.valid) {
        success = true;
        goto cleanup;
    }
    if (!r_body_wrap_value_scope(body, &prefix, &value, NULL)) {
        goto cleanup;
    }
    *result = value;
    success = true;

cleanup:
    r_context_free(body->frontend, prefix.items);
    return success;
}

/* collection-expression = "[", ... "]" (R-EXPR-0030) */
static bool r_body_lower_collection(RBodyContext *body,
                                    RAstRef node,
                                    RTypeId contextual_type,
                                    RExpressionResult *result) {
    RAstNodeView view;
    RCollectionPlan plan;

    r_body_expression_invalid(result);
    if (!r_ast_view_kind(body->frontend, node, R_SYNTAX_COLLECTION_EXPRESSION, &view)) {
        body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    (void)memset(&plan, 0, sizeof(plan));
    plan.node = node;
    plan.span = view.span;
    plan.is_dict = false;
    return r_body_lower_collection_plan(body, &plan, contextual_type, result);
}

/* dict-expression = "{", dict-entry, ... "}" (R-EXPR-0030) */
static bool r_body_lower_dict(RBodyContext *body,
                              RAstRef node,
                              RTypeId contextual_type,
                              RExpressionResult *result) {
    RAstNodeView view;
    RCollectionPlan plan;

    r_body_expression_invalid(result);
    if (!r_ast_view_kind(body->frontend, node, R_SYNTAX_DICT_EXPRESSION, &view)) {
        body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    (void)memset(&plan, 0, sizeof(plan));
    plan.node = node;
    plan.span = view.span;
    plan.is_dict = true;
    return r_body_lower_collection_plan(body, &plan, contextual_type, result);
}

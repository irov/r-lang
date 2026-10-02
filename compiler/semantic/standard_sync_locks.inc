static bool r_body_lower_standard_sync_lock_call(RBodyContext *body,
                                                   RAstRef arguments,
                                                   const RAstNodeView *view,
                                                   RSourceSpan span,
                                                   RTypeId context_type,
                                                   RStandardCallOperation operation,
                                                   RExpressionResult *result) {
    const bool constructor = operation == R_STANDARD_CALL_SYNC_MUTEX_NEW ||
                             operation == R_STANDARD_CALL_SYNC_RWLOCK_NEW;
    const bool waiting = operation == R_STANDARD_CALL_SYNC_WAIT;
    const bool unlocking = operation == R_STANDARD_CALL_SYNC_UNLOCK;
    const bool mutating = operation == R_STANDARD_CALL_SYNC_MUTEX_GUARD_MUT ||
                          operation == R_STANDARD_CALL_SYNC_RW_WRITE_GUARD_MUT;
    const bool accessor = operation >= R_STANDARD_CALL_SYNC_MUTEX_GUARD_REF &&
                          operation <= R_STANDARD_CALL_SYNC_RW_WRITE_GUARD_MUT;
    const bool mutex = operation == R_STANDARD_CALL_SYNC_MUTEX_NEW ||
                       operation == R_STANDARD_CALL_SYNC_LOCK ||
                       operation == R_STANDARD_CALL_SYNC_TRY_LOCK;
    const char *source_name = operation == R_STANDARD_CALL_SYNC_MUTEX_GUARD_REF ||
                             operation == R_STANDARD_CALL_SYNC_MUTEX_GUARD_MUT ? "std.sync::mutex_guard" :
                             operation == R_STANDARD_CALL_SYNC_RW_READ_GUARD_REF ? "std.sync::rw_read_guard" :
                             accessor ? "std.sync::rw_write_guard" :
                             waiting ? "std.sync::condvar" : mutex ? "std.sync::mutex" : "std.sync::rw_lock";
    const char *result_name = operation == R_STANDARD_CALL_SYNC_MUTEX_NEW ? "std.sync::mutex" :
                             operation == R_STANDARD_CALL_SYNC_RWLOCK_NEW ? "std.sync::rw_lock" :
                             operation == R_STANDARD_CALL_SYNC_TRY_LOCK ? "std.sync::try_lock_result" :
                             operation == R_STANDARD_CALL_SYNC_READ ? "std.sync::read_lock_result" :
                             operation == R_STANDARD_CALL_SYNC_TRY_READ ? "std.sync::try_read_lock_result" :
                             operation == R_STANDARD_CALL_SYNC_WRITE ? "std.sync::write_lock_result" :
                             operation == R_STANDARD_CALL_SYNC_TRY_WRITE ? "std.sync::try_write_lock_result" : "std.sync::lock_result";
    RAstRef reference;
    RExpressionResult source;
    RExpressionResult guard;
    const RSemanticType *source_type;
    const RSemanticType *resource;
    RTypeId element = R_TYPE_ID_INVALID;
    RTypeId output;
    RHirNodeId operands[2];
    RHirVector children = {operands, waiting ? 2U : 1U, waiting ? 2U : 1U};
    RHirNodeId id;
    RHirNode *node;
    r_body_expression_invalid(result);
    if (view->child_count != children.count)
        return r_body_diagnostic(body, "R-DIAG-TYPE-001", "R-LIB-0014",
                                 "std.sync lock operation has an incorrect argument count", span);
    if (constructor && context_type != R_TYPE_ID_INVALID) {
        const RSemanticType *expected = r_semantic_type(body->frontend,
            r_semantic_value_type(body->frontend, context_type));
        if (r_semantic_standard_type_name_equal(body->frontend, expected, result_name)) element = expected->base;
    }
    if (!r_ast_ref_child(body->frontend, arguments, 0U, &reference)) return false;
    if (constructor || unlocking) {
        if (!r_body_lower_value(body, reference, element, &source)) return false;
    } else if (mutating) {
        if (!r_body_lower_resource_argument(body,
                                            reference,
                                            false,
                                            "R-LIB-0014",
                                            "std.sync mutable guard operand shall borrow a named place",
                                            &source))
            return false;
    } else if (!r_body_lower_sync_resource_argument(body, reference, &source)) {
        return false;
    }
    if (!source.valid) return true;
    source_type = r_semantic_type(body->frontend, r_semantic_value_type(body->frontend, source.type));
    resource = source_type != NULL && source_type->kind == R_SEMANTIC_TYPE_BORROW
                   ? r_semantic_type(body->frontend, r_semantic_value_type(body->frontend, source_type->base)) : NULL;
    operands[0] = source.node;
    if (constructor) {
        element = r_semantic_value_type(body->frontend, source.type);
        if (r_semantic_value_kind(body->frontend, element) == R_SEMANTIC_TYPE_VOID ||
            r_semantic_type_contains_borrow(body->frontend, element) ||
            r_semantic_type_contains_slice(body->frontend, element) ||
            (!r_semantic_type_is_copy(body->frontend, element) &&
             !r_semantic_type_is_supported_move(body->frontend, element)))
            return r_body_diagnostic(body, "R-DIAG-TYPE-001", "R-LIB-0014",
                                     "protected value requires an owned storable type", span);
    } else if (unlocking) {
        if (!r_semantic_sync_guard(body->frontend, source_type) ||
            r_body_hir_node(body, source.node)->kind != R_HIR_MOVE)
            return r_body_diagnostic(body, "R-DIAG-MOVE-001", "R-LIB-0014",
                                     "std.sync::unlock consumes a lock guard through move", span);
        element = source_type->base;
    } else {
        if (resource == NULL || !r_semantic_standard_type_name_equal(body->frontend, resource, source_name) ||
            (mutating && (source_type->flags & R_SEMANTIC_TYPE_FLAG_SHARED) != 0U))
            return r_body_diagnostic(body, "R-DIAG-TYPE-001", "R-LIB-0014",
                                     "std.sync lock operation requires a matching resource borrow", span);
        element = resource->base;
    }
    if (waiting) {
        if (!r_ast_ref_child(body->frontend, arguments, 1U, &reference) ||
            !r_body_lower_value(body, reference, R_TYPE_ID_INVALID, &guard)) return false;
        if (!guard.valid) return true;
        const RSemanticType *guard_type = r_semantic_type(body->frontend,
            r_semantic_value_type(body->frontend, guard.type));
        if (!r_semantic_standard_type_name_equal(body->frontend, guard_type, "std.sync::mutex_guard") ||
            r_body_hir_node(body, guard.node)->kind != R_HIR_MOVE)
            return r_body_diagnostic(body, "R-DIAG-MOVE-001", "R-LIB-0015",
                                     "std.sync::wait consumes a mutex guard through move", span);
        element = guard_type->base;
        operands[1] = guard.node;
    }
    if (unlocking) {
        if (!r_semantic_type_from_token(body->frontend, R_TOKEN_KW_VOID, &output)) return false;
    } else if (accessor) {
        if (!r_semantic_intern_derived_type(body->frontend, R_SEMANTIC_TYPE_BORROW, element,
                R_TYPE_ID_INVALID, 0U, mutating ? R_SEMANTIC_TYPE_FLAG_NONE : R_SEMANTIC_TYPE_FLAG_SHARED, &output)) return false;
    } else {
        const RStandardOutcomeSchema *schema;
        RTypeId payloads[4];
        if (!r_semantic_intern_parametric_standard_type(body->frontend, result_name, element,
                                                        R_TYPE_ID_INVALID, &output) ||
            (!constructor && !r_semantic_prepare_standard_outcome(body->frontend, output, &schema, payloads))) return false;
    }
    if (context_type != R_TYPE_ID_INVALID && r_semantic_value_type(body->frontend, context_type) != output)
        return r_body_diagnostic(body, "R-DIAG-TYPE-001", "R-LIB-0014",
                                 "std.sync lock operation result does not match its destination type", span);
    if (!r_body_append_node(body, R_HIR_STANDARD_CALL, span, output, R_SYMBOL_ID_INVALID,
                            R_TOKEN_INVALID, 0U, false, &children, &id)) return false;
    node = r_body_hir_node(body, id);
    node->standard_operation = operation;
    node->auxiliary_type = element;
    if (!constructor && !unlocking) {
        const RExpressionResult *origin = waiting ? &guard : &source;
        node->borrow_origin = accessor ? source.symbol : origin->borrow_origin;
        node->borrow_referent = origin->borrow_referent;
        node->borrow_origin_set = accessor ? UINT32_C(0) : origin->borrow_origin_set;
        node->borrow_origin_multiple = accessor ? false : origin->borrow_origin_multiple;
    }
    result->node = id;
    result->type = output;
    result->borrow_origin = node->borrow_origin;
    result->borrow_referent = node->borrow_referent;
    result->borrow_origin_set = node->borrow_origin_set;
    result->borrow_origin_multiple = node->borrow_origin_multiple;
    result->valid = true;
    return true;
}

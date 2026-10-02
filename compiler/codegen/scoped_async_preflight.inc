/* Scoped standard operations (Library R-SLIB-ASYNC-0012): the caller's view is a task-group
   loan, so the emitter passes it by value and never copies or owns the bytes. */

static bool r_c17_scoped_argument_matches(const RC17Emitter *emitter,
                                          const RStandardScopedOperationDescriptor *descriptor,
                                          uint32_t index,
                                          RTypeId id) {
    const RSemanticType *type = r_c17_type(emitter, r_c17_value_type(emitter, id));
    const uint32_t view_index = r_standard_scoped_view_index(descriptor);

    if (type == NULL) {
        return false;
    }
    if (index == UINT32_C(0)) {
        return (type->kind == R_SEMANTIC_TYPE_BORROW) &&
               (type->flags == R_SEMANTIC_TYPE_FLAG_SHARED) &&
               r_c17_type_is_exact_named_standard(emitter, type->base, descriptor->handle_type);
    }
    if (index == view_index) {
        return (type->kind == R_SEMANTIC_TYPE_SLICE) &&
               (type->flags == (descriptor->exclusive_view ? R_SEMANTIC_TYPE_FLAG_NONE
                                                           : R_SEMANTIC_TYPE_FLAG_SHARED)) &&
               r_c17_type_is_exact_scalar(emitter, type->base, R_SEMANTIC_TYPE_U8);
    }
    if (index == view_index + UINT32_C(1)) {
        return (type->kind == R_SEMANTIC_TYPE_OPTION) && (type->flags == 0U) &&
               r_c17_type_is_exact_named_standard(emitter, type->base, "std.time::instant");
    }
    if ((descriptor->extra_type != NULL) && (strcmp(descriptor->extra_type, "u64") == 0)) {
        return r_c17_type_is_exact_scalar(emitter, id, R_SEMANTIC_TYPE_U64);
    }
    return (descriptor->extra_type != NULL) &&
           r_c17_type_is_exact_named_standard(emitter, id, descriptor->extra_type);
}

static bool r_c17_scoped_result_matches(const RC17Emitter *emitter,
                                        const RStandardScopedOperationDescriptor *descriptor,
                                        RTypeId task_id,
                                        RTypeId start_id) {
    const RSemanticType *task = r_c17_type(emitter, task_id);
    bool logical;

    if ((task == NULL) || (task->kind != R_SEMANTIC_TYPE_TASK) || (task->flags != 0U)) {
        return false;
    }
    if (strcmp(descriptor->result_type, "usize") == 0) {
        logical = r_c17_type_is_exact_scalar(emitter, task->base, R_SEMANTIC_TYPE_USIZE);
    } else if (strcmp(descriptor->result_type, "void") == 0) {
        logical = r_c17_type_is_exact_scalar(emitter, task->base, R_SEMANTIC_TYPE_VOID);
    } else {
        logical = r_c17_type_is_exact_named_standard(emitter, task->base, descriptor->result_type);
    }
    return logical &&
           r_c17_effect_set_is_single_named(emitter, task->second, descriptor->error_type) &&
           r_c17_type_is_async_start_carrier(emitter, start_id, task_id);
}

static bool r_c17_preflight_async_scoped(RC17Emitter *emitter,
                                         const RMirFunction *mir,
                                         const RMirInstruction *instruction) {
    const RStandardScopedOperationDescriptor *descriptor =
        r_standard_scoped_operation(instruction->standard_operation);
    uint32_t argument_count;
    uint64_t mask;
    uint32_t index;

    if (descriptor == NULL) {
        return r_c17_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    argument_count = r_standard_scoped_argument_count(descriptor);
    mask = UINT64_C(1) | (UINT64_C(1) << r_standard_scoped_view_index(descriptor));
    if ((instruction->operand_count != argument_count) ||
        (instruction->result == R_MIR_VALUE_ID_INVALID) ||
        !r_c17_scoped_result_matches(
            emitter, descriptor, instruction->auxiliary_type, instruction->type) ||
        !r_c17_preflight_type_dependency(emitter, instruction->type, 0U)) {
        return r_c17_fail(emitter, R_FRONTEND_NOT_LOWERABLE);
    }
    for (index = 0U; index < argument_count; ++index) {
        const RMirInstruction *argument = r_c17_mir_value_definition(
            emitter, mir, r_c17_mir_operand(emitter, instruction, index));

        if ((argument == NULL) ||
            !r_c17_scoped_argument_matches(emitter, descriptor, index, argument->type) ||
            !r_c17_preflight_type_dependency(emitter, argument->type, 0U)) {
            return r_c17_fail(emitter, R_FRONTEND_NOT_LOWERABLE);
        }
        if ((index == UINT32_C(0)) &&
            !r_c17_mir_instruction_is_call_bounded_borrow(emitter, mir, argument)) {
            return r_c17_fail(emitter, R_FRONTEND_NOT_LOWERABLE);
        }
    }
    if (!r_c17_call_borrow_mask_equals(&instruction->call_borrow_mask, mask, 0U)) {
        return r_c17_fail(emitter, R_FRONTEND_NOT_LOWERABLE);
    }
    emitter->uses_async = true;
    return true;
}

static bool r_c17_preflight_scoped(RC17Emitter *emitter,
                                   const RHirNode *node,
                                   RSymbolId function_symbol,
                                   size_t depth) {
    const RStandardScopedOperationDescriptor *descriptor =
        r_standard_scoped_operation(node->standard_operation);
    uint32_t index;

    if ((descriptor == NULL) ||
        (node->child_count != r_standard_scoped_argument_count(descriptor)) ||
        !r_c17_scoped_result_matches(emitter, descriptor, node->type, node->auxiliary_type) ||
        !r_c17_preflight_type_dependency(emitter, node->auxiliary_type, depth + 1U)) {
        return r_c17_fail(emitter, R_FRONTEND_NOT_LOWERABLE);
    }
    for (index = 0U; index < node->child_count; ++index) {
        const RHirNode *argument = r_c17_node(emitter, r_c17_child(emitter, node, index));

        if ((argument == NULL) ||
            !r_c17_scoped_argument_matches(emitter, descriptor, index, argument->type) ||
            !r_c17_preflight_expression(
                emitter, r_c17_child(emitter, node, index), function_symbol, depth + 1U)) {
            return r_c17_fail(emitter, R_FRONTEND_NOT_LOWERABLE);
        }
    }
    emitter->uses_async = true;
    return true;
}

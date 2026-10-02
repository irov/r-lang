static bool r_c17_net_async_type_matches(const RC17Emitter *emitter, RTypeId id, const char *name) {
    const RSemanticType *type = r_c17_type(emitter, r_c17_value_type(emitter, id));
    if (type == NULL)
        return false;
    if (name[0] == '&')
        return type->kind == R_SEMANTIC_TYPE_BORROW &&
               type->flags == R_SEMANTIC_TYPE_FLAG_SHARED &&
               r_c17_net_async_type_matches(emitter, type->base, name + 1);
    if (strcmp(name, "deadline") == 0)
        return type->kind == R_SEMANTIC_TYPE_OPTION && type->flags == 0U &&
               r_c17_type_is_exact_named_standard(emitter, type->base, "std.time::instant");
    if (strcmp(name, "addresses") == 0)
        return type->kind == R_SEMANTIC_TYPE_ARRAY && type->flags == 0U &&
               r_c17_type_is_exact_named_standard(emitter, type->base, "std.net::socket_address");
    if (strcmp(name, "str") == 0)
        return r_c17_value_kind(emitter, id) == R_SEMANTIC_TYPE_STR;
    if (strcmp(name, "u16") == 0)
        return r_c17_type_is_exact_scalar(emitter, id, R_SEMANTIC_TYPE_U16);
    if (strcmp(name, "u32") == 0)
        return r_c17_type_is_exact_scalar(emitter, id, R_SEMANTIC_TYPE_U32);
    if (strcmp(name, "bytes") == 0)
        return r_c17_type_is_exact_byte_array(emitter, id);
    if (strcmp(name, "void") == 0)
        return r_c17_type_is_exact_scalar(emitter, id, R_SEMANTIC_TYPE_VOID);
    if (strcmp(name, "bool") == 0)
        return r_c17_type_is_exact_scalar(emitter, id, R_SEMANTIC_TYPE_BOOL);
    if (strcmp(name, "usize") == 0)
        return r_c17_type_is_exact_scalar(emitter, id, R_SEMANTIC_TYPE_USIZE);
    if (strcmp(name, "i64") == 0)
        return r_c17_type_is_exact_scalar(emitter, id, R_SEMANTIC_TYPE_I64);
    if (strcmp(name, "u64") == 0)
        return r_c17_type_is_exact_scalar(emitter, id, R_SEMANTIC_TYPE_U64);
    return r_c17_type_is_exact_named_standard(emitter, id, name);
}

static bool r_c17_net_async_result_matches(const RC17Emitter *emitter,
                                          const RStandardNetOperationDescriptor *descriptor,
                                          RTypeId task_id, RTypeId start_id) {
    const RSemanticType *task = r_c17_type(emitter, task_id);
    return task != NULL && task->kind == R_SEMANTIC_TYPE_TASK && task->flags == 0U &&
           r_c17_net_async_type_matches(emitter, task->base, descriptor->result_type) &&
           (descriptor->has_error
                ? r_c17_effect_set_is_single_named(emitter, task->second, "std.net::net_error")
                : task->second == R_TYPE_ID_INVALID) &&
           r_c17_type_is_async_start_carrier(emitter, start_id, task_id);
}

static bool r_c17_preflight_async_net(RC17Emitter *emitter,
                                              const RMirFunction *mir,
                                              const RMirInstruction *instruction) {
    const RStandardNetOperationDescriptor *descriptor =
        r_standard_net_operation(instruction->standard_operation);
    uint64_t mask = descriptor == NULL ? 0U : descriptor->borrow_mask;
    uint32_t index;
    if (descriptor == NULL || instruction->operand_count != descriptor->argument_count ||
        instruction->result == R_MIR_VALUE_ID_INVALID ||
        !r_c17_net_async_result_matches(emitter, descriptor, instruction->auxiliary_type,
                                       instruction->type) ||
        !r_c17_preflight_type_dependency(emitter, instruction->type, 0U))
        return r_c17_fail(emitter, R_FRONTEND_NOT_LOWERABLE);
    for (index = 0U; index < descriptor->argument_count; ++index) {
        const RMirInstruction *argument = r_c17_mir_value_definition(
            emitter, mir, r_c17_mir_operand(emitter, instruction, index));
        if (argument == NULL ||
            !r_c17_net_async_type_matches(emitter, argument->type, descriptor->argument_types[index]) ||
            !r_c17_preflight_type_dependency(emitter, argument->type, 0U))
            return r_c17_fail(emitter, R_FRONTEND_NOT_LOWERABLE);
        if (descriptor->argument_types[index][0] == '&') {
            mask |= UINT64_C(1) << index;
            if (!r_c17_mir_instruction_is_call_bounded_borrow(emitter, mir, argument))
                return r_c17_fail(emitter, R_FRONTEND_NOT_LOWERABLE);
        }
        if (index == descriptor->move_index &&
            (argument->kind != R_MIR_INSTRUCTION_MOVE || !argument->is_async_staged_move ||
             argument->operand1 != R_MIR_VALUE_ID_INVALID ||
             !r_c17_preflight_async_place(emitter, mir, argument->place_ordinal,
                                          argument->place_is_parameter, argument->operand1,
                                          argument->type, false, 0U)))
            return r_c17_fail(emitter, R_FRONTEND_NOT_LOWERABLE);
    }
    if (!r_c17_call_borrow_mask_equals(&instruction->call_borrow_mask, mask, 0U))
        return r_c17_fail(emitter, R_FRONTEND_NOT_LOWERABLE);
    emitter->uses_async = true;
    return true;
}

static bool r_c17_preflight_net(RC17Emitter *emitter, const RHirNode *node,
                                        RSymbolId function_symbol, size_t depth) {
    const RStandardNetOperationDescriptor *descriptor =
        r_standard_net_operation(node->standard_operation);
    uint32_t index;
    if (descriptor == NULL || node->child_count != descriptor->argument_count ||
        !r_c17_net_async_result_matches(emitter, descriptor, node->type, node->auxiliary_type) ||
        !r_c17_preflight_type_dependency(emitter, node->auxiliary_type, depth + 1U))
        return r_c17_fail(emitter, R_FRONTEND_NOT_LOWERABLE);
    for (index = 0U; index < descriptor->argument_count; ++index) {
        const RHirNode *argument = r_c17_node(emitter, r_c17_child(emitter, node, index));
        if (argument == NULL ||
            !r_c17_net_async_type_matches(emitter, argument->type, descriptor->argument_types[index]) ||
            (index == descriptor->move_index && argument->kind != R_HIR_MOVE) ||
            !r_c17_preflight_expression(emitter, r_c17_child(emitter, node, index), function_symbol,
                                        depth + 1U))
            return r_c17_fail(emitter, R_FRONTEND_NOT_LOWERABLE);
        if (index == descriptor->move_index && emitter->follow_calls &&
            r_c17_type_requires_drop(emitter, argument->type) &&
            !r_c17_mark_drop_glue_closure(emitter, argument->type))
            return false;
    }
    emitter->uses_async = true;
    return true;
}

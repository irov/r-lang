static bool r_mir_lower_task_scope(RMirBuildContext *build,
                                   const RHirNode *node,
                                   uint32_t depth,
                                   RMirExpressionResult *result) {
    RMirInstruction instruction = {0};
    RMirValueVector arguments = {0};
    bool success = false;
    instruction.span = node->span;
    instruction.type = node->type;
    instruction.symbol = node->symbol;
    instruction.operation = node->operation;
    instruction.integer_value = node->integer_value;
    (void)memset(result, 0, sizeof(*result));
    if (node->kind != R_HIR_TASK_SCOPE_WAIT) {
        instruction.kind = node->kind == R_HIR_TASK_SCOPE_ENTER
                               ? R_MIR_INSTRUCTION_TASK_SCOPE_ENTER
                               : R_MIR_INSTRUCTION_TASK_SCOPE_CLOSE;
        return node->child_count == 0U && r_mir_append_instruction(build, instruction);
    }
    instruction.kind = R_MIR_INSTRUCTION_TASK_SCOPE_WAIT;
    for (uint32_t i = 0U; i < node->child_count; ++i) {
        RMirExpressionResult argument;
        if (!r_mir_lower_expression(
                build, r_mir_hir_child(build->frontend, node, i), depth + 1U, &argument) ||
            argument.is_place || !r_mir_value_vector_push(build, &arguments, argument.value))
            goto cleanup;
        /* R-STMT-0018: a bounded wait takes its deadline as the first operand. */
        if (i == 0U && (node->integer_value & 2U) != 0U)
            continue;
        RMirInstruction *selection = r_mir_temporary_value_definition(build, argument.value);
        if (selection == NULL || selection->kind != R_MIR_INSTRUCTION_BORROW)
            goto cleanup;
        /* A scope selection is a frame-slot projection, reconstructed on each poll. */
        selection->operation = R_TOKEN_KW_AWAIT;
    }
    instruction.operand_count = (uint32_t)arguments.count;
    if (!r_mir_append_operands(build, &arguments, &instruction.first_operand))
        goto cleanup;
    size_t wait_block, resume_block, cancel_block;
    if (!r_mir_add_block(build, &wait_block) || !r_mir_add_block(build, &resume_block) ||
        !r_mir_append_jump(build, node->span, wait_block))
        goto cleanup;
    cancel_block = resume_block;
    if (node->operation != R_TOKEN_KW_FINALLY && !r_mir_add_block(build, &cancel_block))
        goto cleanup;
    instruction.target0 = (RMirBlockId)(resume_block + 1U);
    instruction.target1 = (RMirBlockId)(cancel_block + 1U);
    /* Flag 8, a vacancy wait (L25.3), has a value only when bounded by a deadline (flag 2). */
    if ((node->integer_value & 7U) != 0U && !r_mir_new_value(build, &instruction.result))
        goto cleanup;
    build->current_block = wait_block;
    if (!r_mir_append_instruction(build, instruction))
        goto cleanup;
    if (node->operation != R_TOKEN_KW_FINALLY) {
        build->current_block = cancel_block;
        const RMirPendingCompletion pending =
            r_mir_pending_completion(R_MIR_PENDING_COMPLETION_CANCEL,
                                     R_TYPE_ID_INVALID,
                                     R_MIR_VALUE_ID_INVALID,
                                     R_MIR_BLOCK_ID_INVALID);
        if (!r_mir_lower_active_finalies(build, 0U, node->span, &pending) ||
            !r_mir_append_cancel(build, node->span))
            goto cleanup;
    }
    build->current_block = resume_block;
    result->value = instruction.result;
    success = true;
cleanup:
    r_context_free(build->frontend, arguments.items);
    return success;
}

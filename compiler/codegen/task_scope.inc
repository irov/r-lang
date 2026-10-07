/* Scope storage is immovable and private to its enclosing resumable frame. It is named after the
   group symbol, not its source offset, since the repetitions of a translation-time loop repeat
   one source scope (R-STMT-0023, defect L44-12). */
static bool r_c17_scope_reference(RC17Emitter *emitter, RSymbolId symbol, const char *object) {
    const RSemanticSymbol *group = r_c17_symbol(emitter, symbol);
    return group != NULL && group->is_task_scope &&
           r_c17_format(emitter, "%s->r_scope_%08" PRIx32, object, (uint32_t)symbol);
}

static bool r_c17_emit_scope_fields(RC17Emitter *emitter, const RMirFunction *mir, uint32_t depth) {
    for (uint32_t b = 0U; b < mir->block_count; ++b) {
        const RMirBlock *block = r_c17_mir_block(emitter, mir, b);
        for (uint32_t i = 0U; i < block->instruction_count; ++i) {
            const RMirInstruction *op = r_c17_mir_instruction(emitter, block, i);
            if (op->kind != R_MIR_INSTRUCTION_TASK_SCOPE_ENTER)
                continue;
            const RSemanticSymbol *group = r_c17_symbol(emitter, op->symbol);
            if (group == NULL || !r_c17_indent(emitter, depth) ||
                !r_c17_format(
                    emitter, "RRuntimeTaskScope r_scope_%08" PRIx32 ";\n", (uint32_t)op->symbol) ||
                !r_c17_indent(emitter, depth) ||
                !r_c17_format(emitter,
                              "RRuntimeTaskScopeEntry r_scope_%08" PRIx32
                              "_entries[UINT64_C(%" PRIu64 ")];\n",
                              (uint32_t)op->symbol,
                              op->integer_value))
                return false;
        }
    }
    return true;
}

static bool r_c17_scope_discard_members(RC17Emitter *emitter,
                                        const RMirFunction *mir,
                                        RSymbolId group,
                                        uint32_t depth) {
    for (uint32_t b = mir->block_count; b != 0U; --b) {
        const RMirBlock *block = r_c17_mir_block(emitter, mir, b - 1U);
        for (uint32_t i = block->instruction_count; i != 0U; --i) {
            const RMirInstruction *op = r_c17_mir_instruction(emitter, block, i - 1U);
            const bool local = op->kind == R_MIR_INSTRUCTION_LOCAL;
            const bool parameter = op->kind == R_MIR_INSTRUCTION_PARAMETER;
            if ((!local && !parameter && op->result == 0U) ||
                r_c17_mir_instruction_is_place_projection(op))
                continue;
            const RSemanticType *type = r_c17_type(emitter, r_c17_value_type(emitter, op->type));
            const bool carrier = type != NULL && type->kind == R_SEMANTIC_TYPE_EFFECT_CARRIER &&
                                 r_c17_value_kind(emitter, type->base) == R_SEMANTIC_TYPE_TASK;
            if (type == NULL || (type->kind != R_SEMANTIC_TYPE_TASK && !carrier))
                continue;
            const RC17AsyncStorageKind storage = parameter ? R_C17_ASYNC_STORAGE_PARAMETER
                                                 : local   ? R_C17_ASYNC_STORAGE_LOCAL
                                                           : R_C17_ASYNC_STORAGE_VALUE;
            const uint32_t id = local || parameter ? op->place_ordinal : op->result;
            if (!r_c17_indent(emitter, depth) || !r_c17_write(emitter, "if (") ||
                !r_c17_emit_async_object_init_reference(emitter, "frame", storage, id))
                return false;
            if (carrier &&
                (!r_c17_write(emitter, " && ") ||
                 !r_c17_emit_async_object_storage_reference(emitter, "frame", storage, id) ||
                 !r_c17_write(emitter, ".r_tag == UINT32_C(0)")))
                return false;
            if (!r_c17_write(emitter, ") {\n") || !r_c17_indent(emitter, depth + 1U) ||
                !r_c17_write(emitter, "if (r_runtime_task_scope_drop(&") ||
                !r_c17_scope_reference(emitter, group, "frame") || !r_c17_write(emitter, ",") ||
                !(r_c17_current_column(emitter) + sizeof(" &frame->r_v00000000)) {") - 1U +
                              (carrier ? sizeof(".r_payload.r_ok") - 1U : 0U) >
                          100U
                      ? (r_c17_write(emitter, "\n") &&
                         r_c17_spaces(emitter, ((size_t)depth + 1U) * 4U + 30U))
                      : r_c17_write(emitter, " ")) ||
                !r_c17_write(emitter, "&") ||
                !r_c17_emit_async_object_storage_reference(emitter, "frame", storage, id) ||
                (carrier && !r_c17_write(emitter, ".r_payload.r_ok")) ||
                !r_c17_write(emitter, ")) {\n") || !r_c17_indent(emitter, depth + 2U) ||
                !r_c17_emit_async_object_init_reference(emitter, "frame", storage, id) ||
                !r_c17_write(emitter, " = 0;\n") || !r_c17_indent(emitter, depth + 1U) ||
                !r_c17_write(emitter, "}\n") || !r_c17_indent(emitter, depth) ||
                !r_c17_write(emitter, "}\n"))
                return false;
        }
    }
    return true;
}

/* R-STMT-0018: `o<usize>` of the ready member or none, whether all members finished, or the
 * select branch index. */
static bool r_c17_emit_scope_bounded_result(RC17Emitter *emitter,
                                            const RMirFunction *mir,
                                            const RSemanticSymbol *function,
                                            const RMirInstruction *op,
                                            bool first,
                                            uint32_t depth) {
    if ((op->integer_value & 4U) != 0U) {
        /* A select with a deadline takes the index after its members when the deadline passed. */
        return r_c17_indent(emitter, depth) &&
               r_c17_emit_async_storage_reference(emitter, mir, function, false, op->result) &&
               r_c17_format(emitter,
                            " = scope_expired ? (size_t)%" PRIu32 "U : scope_ready;\n",
                            op->operand_count - 1U);
    }
    if (!first) {
        return r_c17_indent(emitter, depth) &&
               r_c17_emit_async_storage_reference(emitter, mir, function, false, op->result) &&
               r_c17_write(emitter, " = !scope_expired;\n");
    }
    return r_c17_indent(emitter, depth) &&
           r_c17_emit_async_storage_reference(emitter, mir, function, false, op->result) &&
           r_c17_write(emitter, ".r_tag = scope_expired ? UINT32_C(0) : UINT32_C(1);\n") &&
           r_c17_indent(emitter, depth) && r_c17_write(emitter, "if (!scope_expired) {\n") &&
           r_c17_indent(emitter, depth + 1U) &&
           r_c17_emit_async_storage_reference(emitter, mir, function, false, op->result) &&
           r_c17_write(emitter, ".r_payload.r_some = scope_ready;\n") &&
           r_c17_indent(emitter, depth) && r_c17_write(emitter, "}\n");
}

static bool r_c17_emit_scope_instruction(RC17Emitter *emitter,
                                         const RMirFunction *mir,
                                         const RSemanticSymbol *function,
                                         const RMirInstruction *op,
                                         RMirBlockId block_id,
                                         uint32_t depth) {
    if (op->kind == R_MIR_INSTRUCTION_TASK_SCOPE_ENTER) {
        if (!r_c17_indent(emitter, depth) ||
            !r_c17_write(emitter, "if (!r_runtime_task_scope_open(execution,\n") ||
            !r_c17_spaces(emitter, (size_t)depth * 4U + 31U) || !r_c17_write(emitter, "&") ||
            !r_c17_scope_reference(emitter, op->symbol, "frame") || !r_c17_write(emitter, ",\n") ||
            !r_c17_spaces(emitter, (size_t)depth * 4U + 31U) ||
            !r_c17_scope_reference(emitter, op->symbol, "frame") ||
            !r_c17_write(emitter, "_entries,\n") ||
            !r_c17_spaces(emitter, (size_t)depth * 4U + 31U) ||
            !r_c17_format(emitter, "UINT64_C(%" PRIu64 "))) {\n", op->integer_value) ||
            !r_c17_emit_async_contract_panic(emitter, op->span, depth + 1U) ||
            !r_c17_indent(emitter, depth) || !r_c17_write(emitter, "}\n"))
            return false;
        return true;
    }
    if (op->kind == R_MIR_INSTRUCTION_TASK_SCOPE_CLOSE && op->operation == R_TOKEN_KW_DROP) {
        /* R-STMT-0018: group.cancel_all() consumes every unconsumed member, keeping the group. */
        return r_c17_scope_discard_members(emitter, mir, op->symbol, depth);
    }
    if (op->kind == R_MIR_INSTRUCTION_TASK_SCOPE_CLOSE) {
        return r_c17_indent(emitter, depth) &&
               r_c17_write(emitter, "if (!r_runtime_task_scope_close(&") &&
               r_c17_scope_reference(emitter, op->symbol, "frame") &&
               r_c17_write(emitter, ")) {\n") &&
               r_c17_emit_async_contract_panic(emitter, op->span, depth + 1U) &&
               r_c17_indent(emitter, depth) && r_c17_write(emitter, "}\n");
    }
    if (op->operation == R_TOKEN_KW_FINALLY &&
        (!r_c17_scope_discard_members(emitter, mir, op->symbol, depth) ||
         !r_c17_indent(emitter, depth) || !r_c17_write(emitter, "r_runtime_task_scope_cancel(&") ||
         !r_c17_scope_reference(emitter, op->symbol, "frame") || !r_c17_write(emitter, ");\n")))
        return false;
    /* R-STMT-0018: a bounded wait takes its deadline instant as the first operand; a vacancy
       wait (L25.3) names no members. */
    const bool until = (op->integer_value & 2U) != 0U;
    const bool first = (op->integer_value & 1U) != 0U;
    const bool vacancy = (op->integer_value & 8U) != 0U;
    const uint32_t first_member = until ? 1U : 0U;
    const uint32_t members = op->operand_count - first_member;
    if (members != 0U) {
        if (!r_c17_indent(emitter, depth) ||
            !r_c17_write(emitter, "RRuntimeTask *selection[] = {\n"))
            return false;
        for (uint32_t i = first_member; i < op->operand_count; ++i) {
            const RMirValueId id = emitter->frontend->mir_operands[op->first_operand + i];
            const RMirInstruction *borrow = r_c17_mir_value_definition(emitter, mir, id);
            if (borrow == NULL || borrow->kind != R_MIR_INSTRUCTION_BORROW ||
                !r_c17_indent(emitter, depth + 1U) ||
                !r_c17_emit_async_object_storage_reference(emitter,
                                                           "frame",
                                                           borrow->place_is_parameter
                                                               ? R_C17_ASYNC_STORAGE_PARAMETER
                                                               : R_C17_ASYNC_STORAGE_LOCAL,
                                                           borrow->place_ordinal) ||
                !r_c17_write(emitter, ",\n"))
                return false;
        }
        if (!r_c17_indent(emitter, depth) || !r_c17_write(emitter, "};\n"))
            return false;
    }
    if (until &&
        ((first &&
          (!r_c17_indent(emitter, depth) || !r_c17_write(emitter, "size_t scope_ready = 0U;\n"))) ||
         !r_c17_indent(emitter, depth) || !r_c17_write(emitter, "_Bool scope_expired = 0;\n")))
        return false;
    const size_t wait_start = emitter->output.length;
    if (!r_c17_indent(emitter, depth) ||
        !r_c17_write(emitter, "const RRuntimeTaskExecutionAwaitStatus scope_status = ") ||
        !r_c17_write(emitter,
                     vacancy ? (until ? "r_runtime_task_scope_wait_vacancy_until(execution, &"
                                      : "r_runtime_task_scope_wait_vacancy(execution, &")
                     : until ? "r_runtime_task_scope_wait_until(execution, &"
                             : "r_runtime_task_scope_wait(execution, &") ||
        !r_c17_scope_reference(emitter, op->symbol, "frame"))
        return false;
    if (vacancy) {
        const RMirValueId deadline =
            until ? emitter->frontend->mir_operands[op->first_operand] : R_MIR_VALUE_ID_INVALID;
        if ((members != 0U) ||
            (until &&
             (!r_c17_write(emitter, ", ") ||
              !r_c17_emit_async_storage_reference(emitter, mir, function, false, deadline) ||
              !r_c17_write(emitter, ".storage_seconds, ") ||
              !r_c17_emit_async_storage_reference(emitter, mir, function, false, deadline) ||
              !r_c17_write(emitter, ".storage_nanoseconds, &scope_expired"))))
            return false;
    } else if (!r_c17_write(emitter, ", ") || !r_c17_format(emitter,
                                                            "%s, UINT32_C(%" PRIu32 "), ",
                                                            members == 0U ? "NULL" : "selection",
                                                            members))
        return false;
    if (!vacancy && until) {
        const RMirValueId deadline = emitter->frontend->mir_operands[op->first_operand];
        if (!r_c17_write(emitter, first ? "&scope_ready, " : "NULL, ") ||
            !r_c17_emit_async_storage_reference(emitter, mir, function, false, deadline) ||
            !r_c17_write(emitter, ".storage_seconds, ") ||
            !r_c17_emit_async_storage_reference(emitter, mir, function, false, deadline) ||
            !r_c17_write(emitter, ".storage_nanoseconds, &scope_expired"))
            return false;
    } else if (!vacancy && (op->result != 0U)) {
        if (!r_c17_write(emitter, "&") ||
            !r_c17_emit_async_storage_reference(emitter, mir, function, false, op->result))
            return false;
    } else if (!vacancy && !r_c17_write(emitter, "NULL"))
        return false;
    if (!r_c17_write(emitter, ");\n") ||
        !r_c17_reflow_assignment_call(emitter, wait_start, depth) ||
        !r_c17_indent(emitter, depth) || !r_c17_write(emitter, "switch (scope_status) {\n") ||
        !r_c17_indent(emitter, depth) ||
        !r_c17_write(emitter, "case R_RUNTIME_TASK_EXECUTION_AWAIT_SUSPENDED:\n") ||
        !r_c17_indent(emitter, depth + 1U) ||
        !r_c17_write(emitter, "return R_RUNTIME_TASK_STEP_SUSPENDED;\n") ||
        !r_c17_indent(emitter, depth) ||
        !r_c17_write(emitter, "case R_RUNTIME_TASK_EXECUTION_AWAIT_OK:\n") ||
        (until &&
         !r_c17_emit_scope_bounded_result(emitter, mir, function, op, first, depth + 1U)) ||
        !r_c17_emit_async_state_transition(
            emitter, mir, function, block_id, op->target0, depth + 1U) ||
        !r_c17_indent(emitter, depth + 1U) || !r_c17_write(emitter, "continue;\n") ||
        !r_c17_indent(emitter, depth) ||
        !r_c17_write(emitter, "case R_RUNTIME_TASK_EXECUTION_AWAIT_CANCELLED:\n") ||
        !r_c17_emit_async_state_transition(
            emitter, mir, function, block_id, op->target1, depth + 1U) ||
        !r_c17_indent(emitter, depth + 1U) || !r_c17_write(emitter, "continue;\n") ||
        !r_c17_indent(emitter, depth) || !r_c17_write(emitter, "default:\n") ||
        !r_c17_emit_async_contract_panic(emitter, op->span, depth + 1U) ||
        !r_c17_indent(emitter, depth) || !r_c17_write(emitter, "}\n"))
        return false;
    return true;
}

static bool r_c17_emit_async_instruction(RC17Emitter *,
                                         const RMirFunction *,
                                         const RSemanticSymbol *,
                                         const RMirInstruction *,
                                         RMirBlockId,
                                         uint32_t);

static bool r_c17_emit_scoped_start(RC17Emitter *emitter,
                                    const RMirFunction *mir,
                                    const RSemanticSymbol *function,
                                    const RMirInstruction *op,
                                    RMirBlockId block_id,
                                    uint32_t depth) {
    const RSemanticType *carrier = r_c17_type(emitter, op->type);
    if (carrier == NULL || carrier->kind != R_SEMANTIC_TYPE_EFFECT_CARRIER ||
        r_c17_value_kind(emitter, carrier->base) != R_SEMANTIC_TYPE_TASK)
        return r_c17_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    uint32_t error_index = 0U;
    for (uint32_t i = 0U; i < r_semantic_effect_count(emitter->frontend, carrier->second); ++i) {
        if (r_c17_type_is_exact_named_standard(
                emitter,
                r_semantic_effect_at(emitter->frontend, carrier->second, i),
                "std.async::start_error")) {
            error_index = i + 1U;
            break;
        }
    }
    if (error_index == 0U)
        return r_c17_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    if (!r_c17_indent(emitter, depth) || !r_c17_write(emitter, "{\n") ||
        !r_c17_indent(emitter, depth + 1U) ||
        !r_c17_write(emitter, "const size_t reservation = r_runtime_task_scope_reserve(&") ||
        !r_c17_scope_reference(emitter, op->task_scope, "frame") || !r_c17_write(emitter, ");\n") ||
        !r_c17_indent(emitter, depth + 1U) || !r_c17_write(emitter, "if (reservation == 0U) {\n") ||
        !r_c17_indent(emitter, depth + 2U) ||
        !r_c17_emit_async_field_reference(emitter, "frame", false, op->result) ||
        !r_c17_format(emitter,
                      ".r_payload.r_error_%08" PRIx32 " = R_STD_ASYNC_START_SCOPE_FULL;\n",
                      error_index) ||
        !r_c17_indent(emitter, depth + 2U) ||
        !r_c17_emit_async_field_reference(emitter, "frame", false, op->result) ||
        !r_c17_format(emitter, ".r_tag = UINT32_C(%" PRIu32 ");\n", error_index) ||
        !r_c17_indent(emitter, depth + 2U) ||
        !r_c17_emit_async_init_reference(emitter, "frame", false, op->result) ||
        !r_c17_write(emitter, " = 1;\n") || !r_c17_indent(emitter, depth + 1U) ||
        !r_c17_write(emitter, "} else {\n"))
        return false;
    RMirInstruction call = *op;
    call.task_scope = 0U;
    call.deferred_start = op->immediate_await;
    if (!r_c17_emit_async_instruction(emitter, mir, function, &call, block_id, depth + 2U) ||
        !r_c17_indent(emitter, depth + 2U) || !r_c17_write(emitter, "if (") ||
        !r_c17_emit_async_field_reference(emitter, "frame", false, op->result) ||
        !r_c17_write(emitter, ".r_tag == UINT32_C(0)) {\n") || !r_c17_indent(emitter, depth + 3U) ||
        !r_c17_write(emitter, "r_runtime_task_scope_bind(&") ||
        !r_c17_scope_reference(emitter, op->task_scope, "frame") || !r_c17_write(emitter, ",\n") ||
        !r_c17_spaces(emitter, ((size_t)depth + 3U) * 4U + 26U) ||
        !r_c17_write(emitter, "reservation,\n") ||
        !r_c17_spaces(emitter, ((size_t)depth + 3U) * 4U + 26U) ||
        !r_c17_emit_async_field_reference(emitter, "frame", false, op->result) ||
        !r_c17_write(emitter, ".r_payload.r_ok);\n"))
        return false;
    if (op->immediate_await &&
        (r_semantic_dyn_dispatcher(emitter->frontend, op->symbol) ||
         r_semantic_function_value_dispatcher(emitter->frontend, op->symbol))) {
        /* R-TYPE-0051, R-TYPE-0054: the first step of the target that the tag selects; a
           function value is itself the tag. */
        const RFrontendContext *context = emitter->frontend;
        const bool function_value = r_semantic_function_value_dispatcher(context, op->symbol);
        const RTypeId interface =
            function_value
                ? R_TYPE_ID_INVALID
                : r_semantic_dyn_referent(
                      context,
                      context->semantic_parameter_types[context->semantic_symbols[op->symbol - 1U]
                                                            .first_parameter_type]);
        if (!r_c17_indent(emitter, depth + 3U) || !r_c17_write(emitter, "switch (") ||
            !r_c17_emit_async_field_reference(
                emitter, "frame", false, r_c17_mir_operand(emitter, op, 0U)) ||
            !r_c17_write(emitter, function_value ? ") {\n" : ".r_tag) {\n"))
            return false;
        for (size_t entry = 0U; entry < context->dyn_target_count; ++entry) {
            const RDynTarget target = context->dyn_targets[entry];
            uint32_t tag = target.target;
            if (target.dispatcher != op->symbol)
                continue;
            if (!function_value && !r_semantic_dyn_tag(context, interface, target.member, &tag))
                return r_c17_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
            if (!r_c17_indent(emitter, depth + 3U) ||
                !r_c17_format(emitter, "case UINT32_C(%" PRIu32 "):\n", tag) ||
                !r_c17_indent(emitter, depth + 4U) ||
                !r_c17_write(emitter, "r_runtime_task_run_deferred(") ||
                !r_c17_emit_async_field_reference(emitter, "frame", false, op->result) ||
                !r_c17_write(emitter, ".r_payload.r_ok,\n") ||
                !r_c17_spaces(emitter, ((size_t)depth + 4U) * 4U + 28U) ||
                !r_c17_write(emitter, "R_STACK_ENTRY(") ||
                !r_c17_emit_async_step_name(emitter, target.target) ||
                !r_c17_write(emitter, "));\n") || !r_c17_indent(emitter, depth + 4U) ||
                !r_c17_write(emitter, "break;\n"))
                return false;
        }
        if (!r_c17_indent(emitter, depth + 3U) || !r_c17_write(emitter, "default:\n") ||
            !r_c17_emit_async_contract_panic(emitter, op->span, depth + 4U) ||
            !r_c17_indent(emitter, depth + 3U) || !r_c17_write(emitter, "}\n"))
            return false;
    } else if (op->immediate_await &&
               (!r_c17_indent(emitter, depth + 3U) ||
                !r_c17_write(emitter, "r_runtime_task_run_deferred(") ||
                !r_c17_emit_async_field_reference(emitter, "frame", false, op->result) ||
                !r_c17_write(emitter, ".r_payload.r_ok,\n") ||
                !r_c17_spaces(emitter, ((size_t)depth + 3U) * 4U + 28U) ||
                !r_c17_write(emitter, "R_STACK_ENTRY(") ||
                !r_c17_emit_async_step_name(emitter, op->symbol) || !r_c17_write(emitter, "));\n")))
        return false;
    if (!r_c17_indent(emitter, depth + 2U) || !r_c17_write(emitter, "} else {\n") ||
        !r_c17_indent(emitter, depth + 3U) ||
        !r_c17_write(emitter, "r_runtime_task_scope_abandon(&") ||
        !r_c17_scope_reference(emitter, op->task_scope, "frame") ||
        !r_c17_write(emitter, ", reservation);\n") || !r_c17_indent(emitter, depth + 2U) ||
        !r_c17_write(emitter, "}\n") || !r_c17_indent(emitter, depth + 1U) ||
        !r_c17_write(emitter, "}\n") || !r_c17_indent(emitter, depth) ||
        !r_c17_write(emitter, "}\n"))
        return false;
    return true;
}

/* Direct calls of async bodies that cannot suspend (P4.4), an optimization under Core
 * R-AM-0003.
 *
 * `await f(...)` of an unscoped async function starts a task and waits for it (R-STMT-0012). When
 * the body of f completes in its first step, the task only carries the call: a frame allocation,
 * a commit, an inline step and a consuming await. Such a call may instead run the body of f as an
 * ordinary C function on the awaiting task, its direct twin r_fN_direct, provided nothing can tell
 * the two apart:
 *   - the MIR of f has no start, await, task scope or cancel, so the body never suspends and the
 *     first step is the whole body; nor a deadline or budget block, whose runtime entries act on
 *     the running task (Core R-STMT-0019, R-STMT-0020);
 *   - neither the parameters of f nor its result (or outcome, with checked errors, which the twin
 *     writes through its effect output) carry a drop obligation, so nothing moves into a frame and
 *     a cancelled await has nothing to drop;
 *   - the start is consumed by the await that directly follows it: the block of the start only
 *     selects on its outcome, the success path stores the task in the hidden local of the await
 *     and jumps to it, and neither await target begins with a phi.
 * At run time r_runtime_task_direct_begin refuses the direct path under a budget (which counts the
 * task and charges its frame), while the executor drains or stops (which would refuse or cancel the
 * start), and when this stack cannot hold the measured bound of the twin; the call then starts as
 * before. Otherwise it takes the identifier the start would have taken (R-SLIB-ASYNC-0018), which
 * std.async::task_id reports inside the twin until r_runtime_task_direct_end. The await then reads
 * the cancellation request of the awaiting task, as the await of a completed task does.
 *
 * The twin is emitted by the synchronous emitter from the HIR body of f, so the preflight of a
 * call site that qualifies also preflights that body as an ordinary function. */

static bool r_c17_emit_sync_function(RC17Emitter *emitter,
                                     RSymbolId function_id,
                                     const RSemanticSymbol *function,
                                     const RHirNode *node);

/* What the await of a call receives: the result, or the outcome carrier of a function with
   checked errors, which the twin writes through its effect output. */
static RTypeId r_c17_direct_call_outcome(const RSemanticSymbol *callee) {
    return callee->effect_carrier_type != R_TYPE_ID_INVALID ? callee->effect_carrier_type
                                                            : callee->return_type;
}

static bool r_c17_direct_call_eligible(RC17Emitter *emitter, RSymbolId callee_id) {
    const RSemanticSymbol *callee = r_c17_symbol(emitter, callee_id);
    const RHirNode *node;
    const RMirFunction *mir;
    uint32_t block_index;
    uint32_t parameter_index;

    if ((callee == NULL) || ((size_t)callee_id > emitter->frontend->semantic_symbol_count)) {
        return false;
    }
    if (emitter->direct_states[callee_id] != 0U) {
        return emitter->direct_states[callee_id] == 1U;
    }
    emitter->direct_states[callee_id] = 2U;
    if ((callee->kind != R_SEMANTIC_SYMBOL_FUNCTION) || !callee->is_async || callee->is_scoped ||
        callee->is_import || callee->is_extern_c || !callee->has_definition || callee->poisoned ||
        (callee->recursion_depth != 0U) || (callee->json_operation != 0U) ||
        (callee->format_recipe != 0U) || r_semantic_dyn_dispatcher(emitter->frontend, callee_id) ||
        r_semantic_function_value_dispatcher(emitter->frontend, callee_id) ||
        r_c17_function_has_startup_arguments(emitter, callee) ||
        (r_c17_value_kind(emitter, callee->return_type) == R_SEMANTIC_TYPE_NEVER) ||
        r_c17_type_requires_drop(emitter, r_c17_direct_call_outcome(callee)) ||
        r_c17_type_requires_init_flag(emitter, r_c17_direct_call_outcome(callee))) {
        return false;
    }
    node = r_c17_node(emitter, emitter->function_nodes[callee_id]);
    if ((node == NULL) || (node->kind != R_HIR_FUNCTION) || (node->symbol != callee_id) ||
        (node->child_count != callee->parameter_count + UINT32_C(1))) {
        return false;
    }
    for (parameter_index = 0U; parameter_index < callee->parameter_count; ++parameter_index) {
        const RHirNode *parameter =
            r_c17_node(emitter, r_c17_child(emitter, node, parameter_index));

        if ((parameter == NULL) || (parameter->kind != R_HIR_PARAMETER) ||
            r_c17_type_requires_drop(emitter, parameter->type) ||
            r_c17_type_requires_init_flag(emitter, parameter->type)) {
            return false;
        }
    }
    mir = r_c17_mir_function(emitter, callee_id);
    if (mir == NULL) {
        return false;
    }
    for (block_index = 0U; block_index < mir->block_count; ++block_index) {
        const RMirBlock *block = r_c17_mir_block(emitter, mir, block_index);
        uint32_t instruction_index;

        if (block == NULL) {
            return false;
        }
        for (instruction_index = 0U; instruction_index < block->instruction_count;
             ++instruction_index) {
            const RMirInstruction *instruction =
                r_c17_mir_instruction(emitter, block, instruction_index);

            if (instruction == NULL) {
                return false;
            }
            switch (instruction->kind) {
            case R_MIR_INSTRUCTION_ASYNC_START:
            case R_MIR_INSTRUCTION_AWAIT:
            case R_MIR_INSTRUCTION_TASK_SCOPE_ENTER:
            case R_MIR_INSTRUCTION_TASK_SCOPE_WAIT:
            case R_MIR_INSTRUCTION_TASK_SCOPE_CLOSE:
            case R_MIR_INSTRUCTION_CANCEL:
                return false;
            case R_MIR_INSTRUCTION_STANDARD_CALL:
                if ((instruction->standard_operation == R_STANDARD_CALL_ASYNC_DEADLINE_ENTER) ||
                    (instruction->standard_operation == R_STANDARD_CALL_ASYNC_DEADLINE_LEAVE) ||
                    (instruction->standard_operation == R_STANDARD_CALL_ASYNC_BUDGET_ENTER) ||
                    (instruction->standard_operation == R_STANDARD_CALL_ASYNC_BUDGET_LEAVE)) {
                    return false;
                }
                break;
            default:
                break;
            }
        }
    }
    emitter->direct_states[callee_id] = 1U;
    return true;
}

/* The await that directly consumes the task `start` starts, else NULL: after the start its
   block only selects on its outcome, the success path stores the task in the hidden local of the
   await and jumps to it, and neither target of the await begins with a phi. */
static const RMirInstruction *
r_c17_immediate_await(RC17Emitter *emitter, const RMirFunction *mir, const RMirInstruction *start) {
    const RMirBlock *start_block = NULL;
    const RMirBlock *success_block = NULL;
    const RMirBlock *await_block;
    const RMirInstruction *branch;
    const RMirInstruction *store;
    const RMirInstruction *jump;
    const RMirInstruction *await;
    uint32_t start_index = UINT32_MAX;
    uint32_t block_index;
    uint32_t instruction_index;
    uint32_t side;

    /* A start inside a task scope joins its group and takes a place of its capacity (P4.4-7);
       it keeps its task. */
    if ((mir == NULL) || (start == NULL) || (start->result == R_MIR_VALUE_ID_INVALID) ||
        (start->task_scope != R_SYMBOL_ID_INVALID) || emitter->dyn_start_member) {
        return NULL;
    }
    for (block_index = 0U; (block_index < mir->block_count) && (start_block == NULL);
         ++block_index) {
        const RMirBlock *block = r_c17_mir_block(emitter, mir, block_index);

        if (block == NULL) {
            return NULL;
        }
        for (instruction_index = 0U; instruction_index < block->instruction_count;
             ++instruction_index) {
            if (r_c17_mir_instruction(emitter, block, instruction_index) == start) {
                start_block = block;
                start_index = instruction_index;
                break;
            }
        }
    }
    if ((start_block == NULL) || (start_index + UINT32_C(1) >= start_block->instruction_count)) {
        return NULL;
    }
    /* After the start, its block only computes the outcome tag and branches on it. */
    for (instruction_index = start_index + UINT32_C(1);
         instruction_index + UINT32_C(1) < start_block->instruction_count;
         ++instruction_index) {
        const RMirInstruction *instruction =
            r_c17_mir_instruction(emitter, start_block, instruction_index);

        if ((instruction == NULL) || ((instruction->kind != R_MIR_INSTRUCTION_EFFECT_TAG) &&
                                      (instruction->kind != R_MIR_INSTRUCTION_CONSTANT) &&
                                      (instruction->kind != R_MIR_INSTRUCTION_BINARY))) {
            return NULL;
        }
    }
    branch =
        r_c17_mir_instruction(emitter, start_block, start_block->instruction_count - UINT32_C(1));
    if ((branch == NULL) || (branch->kind != R_MIR_INSTRUCTION_BRANCH)) {
        return NULL;
    }
    for (side = 0U; (side < 2U) && (success_block == NULL); ++side) {
        const RMirBlockId target = side == 0U ? branch->target0 : branch->target1;
        const RMirBlock *block = r_c17_mir_target_is_valid(mir, target)
                                     ? r_c17_mir_block(emitter, mir, target - UINT32_C(1))
                                     : NULL;
        const RMirInstruction *payload =
            block == NULL ? NULL : r_c17_mir_instruction(emitter, block, UINT32_C(0));

        if ((payload != NULL) && (payload->kind == R_MIR_INSTRUCTION_EFFECT_PAYLOAD) &&
            (payload->operand0 == start->result) && (payload->integer_value == UINT64_C(0)) &&
            (block->instruction_count == UINT32_C(3))) {
            success_block = block;
        }
    }
    if (success_block == NULL) {
        return NULL;
    }
    store = r_c17_mir_instruction(emitter, success_block, UINT32_C(1));
    jump = r_c17_mir_instruction(emitter, success_block, UINT32_C(2));
    if ((store == NULL) || (jump == NULL) || (store->kind != R_MIR_INSTRUCTION_STORE) ||
        (store->operand0 != r_c17_mir_instruction(emitter, success_block, UINT32_C(0))->result) ||
        (store->operand1 != R_MIR_VALUE_ID_INVALID) || store->place_is_parameter ||
        (jump->kind != R_MIR_INSTRUCTION_JUMP) || !r_c17_mir_target_is_valid(mir, jump->target0)) {
        return NULL;
    }
    await_block = r_c17_mir_block(emitter, mir, jump->target0 - UINT32_C(1));
    await = await_block == NULL ? NULL : r_c17_mir_instruction(emitter, await_block, UINT32_C(0));
    if ((await == NULL) || (await_block->instruction_count != UINT32_C(1)) ||
        (await->kind != R_MIR_INSTRUCTION_AWAIT) || await->place_is_parameter ||
        (await->place_ordinal != store->place_ordinal) ||
        (await->operand1 != R_MIR_VALUE_ID_INVALID) ||
        !r_c17_mir_target_is_valid(mir, await->target0) ||
        !r_c17_mir_target_is_valid(mir, await->target1) ||
        r_c17_async_block_starts_with_phi(emitter, mir, await->target0) ||
        r_c17_async_block_starts_with_phi(emitter, mir, await->target1)) {
        return NULL;
    }
    return await;
}

/* The await that consumes `start` when the call may run directly, else NULL. */
static const RMirInstruction *r_c17_direct_call_await(RC17Emitter *emitter,
                                                      const RMirFunction *mir,
                                                      const RMirInstruction *start) {
    const RMirInstruction *await;

    if ((start == NULL) || (start->kind != R_MIR_INSTRUCTION_ASYNC_START) ||
        !start->immediate_await || start->deferred_start ||
        !r_c17_direct_call_eligible(emitter, start->symbol)) {
        return NULL;
    }
    await = r_c17_immediate_await(emitter, mir, start);
    if ((await == NULL) ||
        (r_c17_value_type(emitter, await->type) !=
         r_c17_value_type(emitter,
                          r_c17_direct_call_outcome(r_c17_symbol(emitter, start->symbol))))) {
        return NULL;
    }
    return await;
}

/* Called for every start the preflight visits: a start that may run directly needs the twin of
   its callee, whose body is then preflighted as the body of an ordinary function. */
static bool r_c17_preflight_direct_call(RC17Emitter *emitter,
                                        const RMirFunction *mir,
                                        const RMirInstruction *start) {
    const RSemanticSymbol *callee;
    const RHirNode *node;

    if ((r_c17_direct_call_await(emitter, mir, start) == NULL) ||
        (emitter->direct_twins[start->symbol] != 0U)) {
        return true;
    }
    callee = r_c17_symbol(emitter, start->symbol);
    node = r_c17_node(emitter, emitter->function_nodes[start->symbol]);
    if ((callee == NULL) || (node == NULL)) {
        return r_c17_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    emitter->direct_twins[start->symbol] = 1U;
    return r_c17_preflight_statement(
        emitter, r_c17_child(emitter, node, callee->parameter_count), start->symbol, 1U);
}

static bool r_c17_emit_direct_twin_prototype(RC17Emitter *emitter, RSymbolId function_id) {
    bool success;

    if (emitter->direct_twins[function_id] == 0U) {
        return true;
    }
    emitter->direct_twin_signature = true;
    success =
        r_c17_emit_function_signature(emitter, function_id, false) && r_c17_write(emitter, ";\n");
    emitter->direct_twin_signature = false;
    return success;
}

static bool r_c17_emit_direct_twin(RC17Emitter *emitter,
                                   RSymbolId function_id,
                                   const RSemanticSymbol *function,
                                   const RHirNode *node) {
    bool success;

    if (emitter->direct_twins[function_id] == 0U) {
        return true;
    }
    emitter->direct_twin_signature = true;
    success = r_c17_emit_sync_function(emitter, function_id, function, node);
    emitter->direct_twin_signature = false;
    return success;
}

static bool r_c17_emit_direct_call_name(RC17Emitter *emitter, RSymbolId function_id) {
    return r_c17_emit_function_name(emitter, function_id) && r_c17_write(emitter, "_direct");
}

/* The source bytes of an await name its fast path for readers and for check_direct_calls.py. A
   comment line stays with the line after it in the layout pass, so comments precede a condition,
   never a statement that may need to be laid out. */
static bool r_c17_emit_await_comment(RC17Emitter *emitter,
                                     const char *kind,
                                     const RMirInstruction *await,
                                     uint32_t depth) {
    if ((await->span.end < await->span.start) ||
        ((size_t)await->span.source > emitter->frontend->source_count) ||
        (emitter->source_ordinals[await->span.source] == 0U)) {
        return r_c17_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    return r_c17_indent(emitter, depth) &&
           r_c17_format(emitter,
                        "/* %s of the await at source %" PRIu32 ", bytes %" PRIu32 "..%" PRIu32
                        " (P4.4). */\n",
                        kind,
                        emitter->source_ordinals[await->span.source],
                        await->span.start,
                        await->span.end);
}

/* The rest of the fast path once the value of the await is in place: the await of a completed
   task reads the cancellation request of the awaiting task (Core R-STMT-0012). */
static bool
r_c17_emit_await_completion(RC17Emitter *emitter, const RMirInstruction *await, uint32_t depth) {
    if (r_c17_result_has_value(emitter, await->type) &&
        r_c17_type_requires_init_flag(emitter, await->type) &&
        (!r_c17_indent(emitter, depth) ||
         !r_c17_emit_async_init_reference(emitter, "frame", false, await->result) ||
         !r_c17_write(emitter, " = 1;\n"))) {
        return false;
    }
    if (!r_c17_indent(emitter, depth) ||
        !r_c17_write(emitter, "if (r_runtime_task_execution_cancel_requested(execution)) {\n")) {
        return false;
    }
    if (r_c17_result_has_value(emitter, await->type) &&
        r_c17_type_requires_init_flag(emitter, await->type) &&
        !r_c17_emit_async_drop_field(
            emitter, await->type, R_C17_ASYNC_STORAGE_VALUE, await->result, depth + UINT32_C(1))) {
        return false;
    }
    return r_c17_indent(emitter, depth + UINT32_C(1)) &&
           r_c17_format(emitter, "frame->r_state = UINT32_C(%" PRIu32 ");\n", await->target1) &&
           r_c17_indent(emitter, depth + UINT32_C(1)) && r_c17_write(emitter, "continue;\n") &&
           r_c17_indent(emitter, depth) && r_c17_write(emitter, "}\n") &&
           r_c17_indent(emitter, depth) &&
           r_c17_format(emitter, "frame->r_state = UINT32_C(%" PRIu32 ");\n", await->target0) &&
           r_c17_indent(emitter, depth) && r_c17_write(emitter, "continue;\n");
}

/* The direct path of a start that qualifies, emitted before its ordinary start. */
static bool r_c17_emit_direct_call(RC17Emitter *emitter,
                                   const RMirFunction *mir,
                                   const RMirInstruction *start,
                                   uint32_t depth) {
    const RMirInstruction *await = r_c17_direct_call_await(emitter, mir, start);
    const RSemanticSymbol *function = mir == NULL ? NULL : r_c17_symbol(emitter, mir->symbol);
    const RSemanticSymbol *callee = start == NULL ? NULL : r_c17_symbol(emitter, start->symbol);
    bool has_effect_output;
    uint32_t argument_index;

    if ((await == NULL) || (emitter->direct_twins[start->symbol] == 0U)) {
        return true;
    }
    if ((function == NULL) || (callee == NULL)) {
        return r_c17_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    has_effect_output = callee->effect_carrier_type != R_TYPE_ID_INVALID;
    if (!r_c17_emit_await_comment(emitter, "Direct call", await, depth) ||
        !r_c17_indent(emitter, depth) || !r_c17_write(emitter, "/* R_STACK_GUARDED: ") ||
        !r_c17_emit_direct_call_name(emitter, start->symbol) || !r_c17_write(emitter, " */\n") ||
        !r_c17_indent(emitter, depth) ||
        !r_c17_write(emitter, "if (r_runtime_task_direct_begin(R_STACK_ENTRY(") ||
        !r_c17_emit_direct_call_name(emitter, start->symbol) || !r_c17_write(emitter, "))) {\n") ||
        !r_c17_indent(emitter, depth + UINT32_C(1))) {
        return false;
    }
    if (!has_effect_output && r_c17_result_has_value(emitter, await->type) &&
        (!r_c17_emit_async_field_reference(emitter, "frame", false, await->result) ||
         !r_c17_write(emitter, " = "))) {
        return false;
    }
    if (!r_c17_emit_direct_call_name(emitter, start->symbol) || !r_c17_write(emitter, "(")) {
        return false;
    }
    if (has_effect_output &&
        (!r_c17_write(emitter, "&") ||
         !r_c17_emit_async_field_reference(emitter, "frame", false, await->result))) {
        return false;
    }
    for (argument_index = 0U; argument_index < start->operand_count; ++argument_index) {
        const RMirValueId argument = r_c17_mir_operand(emitter, start, argument_index);
        const RMirInstruction *definition = r_c17_mir_value_definition(emitter, mir, argument);

        if (definition == NULL) {
            return r_c17_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
        if (((argument_index != 0U) || has_effect_output) && !r_c17_write(emitter, ", ")) {
            return false;
        }
        /* A staged move of a value without drop glue commits by copy from its place, as the
           call initializer of the start does. */
        if (definition->is_async_staged_move
                ? !r_c17_emit_async_transfer_storage_reference(emitter,
                                                               mir,
                                                               function,
                                                               definition->place_is_parameter
                                                                   ? R_C17_ASYNC_STORAGE_PARAMETER
                                                                   : R_C17_ASYNC_STORAGE_LOCAL,
                                                               definition->place_ordinal)
                : !r_c17_emit_async_storage_reference(emitter, mir, function, false, argument)) {
            return false;
        }
    }
    /* L39: a twin that panicked returns with its panic pending; the step continues in the
       panic block of the await. */
    emitter->async_panic_target = await->panic_target;
    return r_c17_write(emitter, ");\n") && r_c17_indent(emitter, depth + UINT32_C(1)) &&
           r_c17_write(emitter, "r_runtime_task_direct_end();\n") &&
           r_c17_emit_panic_test(emitter, depth + UINT32_C(1)) &&
           r_c17_emit_await_completion(emitter, await, depth + UINT32_C(1)) &&
           r_c17_indent(emitter, depth) && r_c17_write(emitter, "}\n");
}

/* An awaited std.sync receive (R-LIB-0016) whose channel holds a value, or has lost every
   sender, completes at once without its task: the value moves out of the channel as the started
   receive would move it, and the empty channel keeps the ordinary path, which waits. The receive
   task would take no part in anything else: it is not counted (R-STMT-0020), and under a budget,
   which still charges its frame, the ordinary path stays. */
static bool r_c17_emit_receive_now(RC17Emitter *emitter,
                                   const RMirFunction *mir,
                                   const RSemanticSymbol *function,
                                   const RMirInstruction *instruction,
                                   uint32_t depth) {
    const RMirInstruction *await = r_c17_immediate_await(emitter, mir, instruction);
    const RMirValueId argument = r_c17_mir_operand(emitter, instruction, UINT32_C(0));
    const RSemanticType *task_type =
        r_c17_type(emitter, r_c17_value_type(emitter, instruction->auxiliary_type));

    if ((await == NULL) || (argument == R_MIR_VALUE_ID_INVALID) || (task_type == NULL) ||
        (r_c17_value_type(emitter, await->type) != r_c17_value_type(emitter, task_type->base))) {
        return true;
    }
    return r_c17_emit_await_comment(emitter, "Synchronous receive", await, depth) &&
           r_c17_indent(emitter, depth) &&
           r_c17_write(emitter, "if (r_runtime_task_inline_completion_allowed()) {\n") &&
           r_c17_indent(emitter, depth + UINT32_C(1)) &&
           r_c17_format(emitter,
                        "const RStdSyncTryRecvResult r_receive_now_%08" PRIu32
                        " = r_std_sync_try_recv(",
                        instruction->result) &&
           r_c17_emit_async_storage_reference(emitter, mir, function, false, argument) &&
           r_c17_write(emitter, ", &") &&
           r_c17_emit_async_field_reference(emitter, "frame", false, await->result) &&
           r_c17_write(emitter, ".r_payload.r_some);\n") &&
           r_c17_indent(emitter, depth + UINT32_C(1)) &&
           r_c17_format(emitter,
                        "if (r_receive_now_%08" PRIu32
                        ".kind != R_STD_SYNC_TRY_RECV_RESULT_EMPTY) {\n",
                        instruction->result) &&
           r_c17_indent(emitter, depth + UINT32_C(2)) &&
           r_c17_emit_async_field_reference(emitter, "frame", false, await->result) &&
           r_c17_format(emitter,
                        ".r_tag = r_receive_now_%08" PRIu32
                        ".kind == R_STD_SYNC_TRY_RECV_RESULT_RECEIVED ? UINT32_C(1) : "
                        "UINT32_C(0);\n",
                        instruction->result) &&
           r_c17_emit_await_completion(emitter, await, depth + UINT32_C(2)) &&
           r_c17_indent(emitter, depth + UINT32_C(1)) && r_c17_write(emitter, "}\n") &&
           r_c17_indent(emitter, depth) && r_c17_write(emitter, "}\n");
}

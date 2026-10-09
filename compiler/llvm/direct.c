#include "emit_internal.h"

#include <string.h>

/* Direct calls of async bodies that cannot suspend (B7, an optimization under Core R-AM-0003;
   P4.4 of the C17 emitter).

   `await f(...)` of an unscoped async function starts a task and waits for it (R-STMT-0012). When
   the body of f completes in its first step, the task only carries the call: a frame allocation,
   a commit, an inline step and a consuming await. Such a call may instead run the body of f as
   an ordinary function on the awaiting task, its direct twin, provided nothing can tell the two
   apart:
     - the MIR of f has no start, await, task scope or cancel, so the body never suspends and the
       first step is the whole body; nor a deadline or budget block, whose runtime entries act on
       the running task (Core R-STMT-0019, R-STMT-0020);
     - neither the parameters of f nor its outcome carry a drop obligation, so nothing moves into
       a frame and a cancelled await has nothing to drop;
     - the start is consumed by the await that directly follows it: the block of the start only
       selects on its outcome, the success path stores the task in the hidden local of the await
       and jumps to it, and neither await target begins with a phi.
   At run time r_runtime_task_direct_begin refuses the direct path under a budget (which counts the
   task and charges its frame), while the executor drains or stops (which would refuse or cancel
   the start), and when this stack cannot hold the measured bound of the twin; the call then
   starts as before. Otherwise it takes the identifier the start would have taken
   (R-SLIB-ASYNC-0018), which std.async::task_id reports inside the twin until
   r_runtime_task_direct_end. The await then reads the cancellation request of the awaiting task,
   as the await of a completed task does. */

static const RMirBlock *
r_llvm_direct_block(const RLlvmEmitter *emitter, const RMirFunction *mir, uint32_t index) {
    return index < mir->block_count
               ? &emitter->frontend->mir_blocks[(size_t)mir->first_block + index]
               : NULL;
}

static const RMirInstruction *
r_llvm_direct_instruction(const RLlvmEmitter *emitter, const RMirBlock *block, uint32_t index) {
    return (block != NULL) && (index < block->instruction_count)
               ? &emitter->frontend->mir_instructions[(size_t)block->first_instruction + index]
               : NULL;
}

static bool r_llvm_direct_target_is_valid(const RMirFunction *mir, RMirBlockId target) {
    return (target != R_MIR_BLOCK_ID_INVALID) && ((size_t)target <= mir->block_count);
}

static bool r_llvm_direct_starts_with_phi(const RLlvmEmitter *emitter,
                                          const RMirFunction *mir,
                                          RMirBlockId target) {
    const RMirInstruction *first =
        r_llvm_direct_instruction(emitter, r_llvm_direct_block(emitter, mir, target - 1U), 0U);
    return (first == NULL) || (first->kind == R_MIR_INSTRUCTION_PHI);
}

/* What the await of a call receives: the result, or the outcome carrier of a function with
   checked errors, which the twin writes through its effect output. */
static RTypeId r_llvm_direct_outcome(const RSemanticSymbol *callee) {
    return callee->effect_carrier_type != R_TYPE_ID_INVALID ? callee->effect_carrier_type
                                                            : callee->return_type;
}

static bool r_llvm_direct_eligible(RLlvmEmitter *emitter, RSymbolId callee_id) {
    const RFrontendContext *context = emitter->frontend;
    const RSemanticSymbol *callee;
    const RSemanticType *result;
    const RMirFunction *mir;
    uint32_t block_index;
    uint32_t parameter_index;

    if ((callee_id == R_SYMBOL_ID_INVALID) ||
        ((size_t)callee_id > context->semantic_symbol_count)) {
        return false;
    }
    if (emitter->direct_states[callee_id] != 0U) {
        return emitter->direct_states[callee_id] == 1U;
    }
    emitter->direct_states[callee_id] = 2U;
    callee = &context->semantic_symbols[(size_t)callee_id - 1U];
    mir = emitter->mir_by_symbol[callee_id];
    result = r_llvm_type(emitter, r_llvm_value_type(emitter, callee->return_type));
    if ((callee->kind != R_SEMANTIC_SYMBOL_FUNCTION) || !callee->is_async || callee->is_scoped ||
        callee->is_import || callee->is_extern_c || !callee->has_definition || callee->poisoned ||
        (callee->recursion_depth != 0U) || (callee->json_operation != 0U) ||
        (callee->format_recipe != 0U) || r_semantic_dyn_dispatcher(context, callee_id) ||
        r_semantic_function_value_dispatcher(context, callee_id) ||
        !emitter->reachable[callee_id] || (mir == NULL) || (mir->block_count == 0U) ||
        (result == NULL) || (result->kind == R_SEMANTIC_TYPE_NEVER) ||
        r_llvm_type_requires_drop(emitter, r_llvm_direct_outcome(callee))) {
        return false;
    }
    for (parameter_index = 0U; parameter_index < callee->parameter_count; ++parameter_index) {
        if (r_llvm_type_requires_drop(
                emitter,
                context
                    ->semantic_parameter_types[callee->first_parameter_type + parameter_index])) {
            return false;
        }
    }
    for (block_index = 0U; block_index < mir->block_count; ++block_index) {
        const RMirBlock *block = r_llvm_direct_block(emitter, mir, block_index);
        uint32_t instruction_index;
        for (instruction_index = 0U; instruction_index < block->instruction_count;
             ++instruction_index) {
            const RMirInstruction *instruction =
                r_llvm_direct_instruction(emitter, block, instruction_index);
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
                    (instruction->standard_operation == R_STANDARD_CALL_ASYNC_BUDGET_LEAVE) ||
                    (instruction->task_scope != R_SYMBOL_ID_INVALID)) {
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
static const RMirInstruction *r_llvm_immediate_await(RLlvmEmitter *emitter,
                                                     const RMirFunction *mir,
                                                     const RMirInstruction *start) {
    const RMirBlock *start_block = NULL;
    const RMirBlock *success_block = NULL;
    const RMirBlock *await_block;
    const RMirInstruction *branch;
    const RMirInstruction *store;
    const RMirInstruction *jump;
    const RMirInstruction *await;
    const RMirInstruction *payload;
    uint32_t start_index = UINT32_MAX;
    uint32_t block_index;
    uint32_t instruction_index;
    uint32_t side;

    /* A start inside a task scope joins its group and takes a place of its capacity; it keeps
       its task. */
    if ((mir == NULL) || (start == NULL) || (start->result == R_MIR_VALUE_ID_INVALID) ||
        (start->task_scope != R_SYMBOL_ID_INVALID)) {
        return NULL;
    }
    for (block_index = 0U; (block_index < mir->block_count) && (start_block == NULL);
         ++block_index) {
        const RMirBlock *block = r_llvm_direct_block(emitter, mir, block_index);
        for (instruction_index = 0U; instruction_index < block->instruction_count;
             ++instruction_index) {
            if (r_llvm_direct_instruction(emitter, block, instruction_index) == start) {
                start_block = block;
                start_index = instruction_index;
                break;
            }
        }
    }
    if ((start_block == NULL) || (start_index + 1U >= start_block->instruction_count)) {
        return NULL;
    }
    /* After the start, its block only computes the outcome tag and branches on it. */
    for (instruction_index = start_index + 1U;
         instruction_index + 1U < start_block->instruction_count;
         ++instruction_index) {
        const RMirInstruction *instruction =
            r_llvm_direct_instruction(emitter, start_block, instruction_index);
        if ((instruction->kind != R_MIR_INSTRUCTION_EFFECT_TAG) &&
            (instruction->kind != R_MIR_INSTRUCTION_CONSTANT) &&
            (instruction->kind != R_MIR_INSTRUCTION_BINARY)) {
            return NULL;
        }
    }
    branch = r_llvm_direct_instruction(emitter, start_block, start_block->instruction_count - 1U);
    if (branch->kind != R_MIR_INSTRUCTION_BRANCH) {
        return NULL;
    }
    for (side = 0U; (side < 2U) && (success_block == NULL); ++side) {
        const RMirBlockId target = side == 0U ? branch->target0 : branch->target1;
        const RMirBlock *block = r_llvm_direct_target_is_valid(mir, target)
                                     ? r_llvm_direct_block(emitter, mir, target - 1U)
                                     : NULL;
        payload = r_llvm_direct_instruction(emitter, block, 0U);
        if ((payload != NULL) && (payload->kind == R_MIR_INSTRUCTION_EFFECT_PAYLOAD) &&
            (payload->operand0 == start->result) && (payload->integer_value == 0U) &&
            (block->instruction_count == 3U)) {
            success_block = block;
        }
    }
    if (success_block == NULL) {
        return NULL;
    }
    payload = r_llvm_direct_instruction(emitter, success_block, 0U);
    store = r_llvm_direct_instruction(emitter, success_block, 1U);
    jump = r_llvm_direct_instruction(emitter, success_block, 2U);
    if ((store->kind != R_MIR_INSTRUCTION_STORE) || (store->operand0 != payload->result) ||
        (store->operand1 != R_MIR_VALUE_ID_INVALID) || store->place_is_parameter ||
        (jump->kind != R_MIR_INSTRUCTION_JUMP) ||
        !r_llvm_direct_target_is_valid(mir, jump->target0)) {
        return NULL;
    }
    await_block = r_llvm_direct_block(emitter, mir, jump->target0 - 1U);
    await = r_llvm_direct_instruction(emitter, await_block, 0U);
    if ((await == NULL) || (await_block->instruction_count != 1U) ||
        (await->kind != R_MIR_INSTRUCTION_AWAIT) || await->place_is_parameter ||
        (await->place_ordinal != store->place_ordinal) ||
        (await->operand1 != R_MIR_VALUE_ID_INVALID) || (await->integer_value != 0U) ||
        !r_llvm_direct_target_is_valid(mir, await->target0) ||
        !r_llvm_direct_target_is_valid(mir, await->target1) ||
        r_llvm_direct_starts_with_phi(emitter, mir, await->target0) ||
        r_llvm_direct_starts_with_phi(emitter, mir, await->target1)) {
        return NULL;
    }
    return await;
}

const RMirInstruction *
r_llvm_direct_await(RLlvmEmitter *emitter, const RMirFunction *mir, const RMirInstruction *start) {
    const RMirInstruction *await;

    if ((start == NULL) || (start->kind != R_MIR_INSTRUCTION_ASYNC_START) ||
        !start->immediate_await || start->deferred_start ||
        !r_llvm_direct_eligible(emitter, start->symbol)) {
        return NULL;
    }
    await = r_llvm_immediate_await(emitter, mir, start);
    if ((await == NULL) ||
        (r_llvm_value_type(emitter, await->type) !=
         r_llvm_value_type(
             emitter,
             r_llvm_direct_outcome(&emitter->frontend->semantic_symbols[start->symbol - 1U])))) {
        return NULL;
    }
    return await;
}

/* Every start of a lowered function that may run directly needs the twin of its callee, declared
   before any body is written. */
bool r_llvm_prepare_direct_calls(RLlvmEmitter *emitter) {
    const RFrontendContext *context = emitter->frontend;
    size_t index;

    for (index = 0U; index < context->mir_function_count; ++index) {
        const RMirFunction *mir = &context->mir_functions[index];
        if ((mir->symbol != R_SYMBOL_ID_INVALID) &&
            ((size_t)mir->symbol <= context->semantic_symbol_count)) {
            emitter->mir_by_symbol[mir->symbol] = mir;
        }
    }
    for (index = 0U; index < context->mir_function_count; ++index) {
        const RMirFunction *mir = &context->mir_functions[index];
        uint32_t block_index;
        if (!emitter->reachable[mir->symbol] ||
            context->semantic_symbols[(size_t)mir->symbol - 1U].is_import) {
            continue;
        }
        for (block_index = 0U; block_index < mir->block_count; ++block_index) {
            const RMirBlock *block = r_llvm_direct_block(emitter, mir, block_index);
            uint32_t instruction_index;
            for (instruction_index = 0U; instruction_index < block->instruction_count;
                 ++instruction_index) {
                const RMirInstruction *start =
                    r_llvm_direct_instruction(emitter, block, instruction_index);
                if ((start->kind == R_MIR_INSTRUCTION_ASYNC_START) &&
                    (r_llvm_direct_await(emitter, mir, start) != NULL) &&
                    (emitter->direct_twins[start->symbol] == NULL) &&
                    !r_llvm_declare_direct_twin(emitter, start->symbol)) {
                    return false;
                }
            }
        }
    }
    return emitter->status == R_FRONTEND_OK;
}

/* An awaited std.sync receive (R-LIB-0016) whose await directly consumes its task, else NULL: its
   value may be taken without the task (r_llvm_emit_receive_now). */
const RMirInstruction *r_llvm_receive_await(RLlvmEmitter *emitter,
                                            const RMirFunction *mir,
                                            const RMirInstruction *receive) {
    const RSemanticType *task =
        r_llvm_type(emitter, r_llvm_value_type(emitter, receive->auxiliary_type));
    const RMirInstruction *await;

    if ((receive->kind != R_MIR_INSTRUCTION_STANDARD_CALL) ||
        (receive->standard_operation != R_STANDARD_CALL_SYNC_RECEIVE) ||
        (receive->operand_count == 0U) || (task == NULL)) {
        return NULL;
    }
    await = r_llvm_immediate_await(emitter, mir, receive);
    if ((await == NULL) ||
        (r_llvm_value_type(emitter, await->type) != r_llvm_value_type(emitter, task->base))) {
        return NULL;
    }
    return await;
}

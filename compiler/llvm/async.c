#include "emit_internal.h"
#include "standard_internal.h"

#include <string.h>

/* Tasks of async functions (Core R-FUNC-0012, R-STMT-0018), as the C17 emitter's
   r_c17_emit_async_start_instruction, r_async_start_N, r_async_launch_N and r_c17_emit_async_await.
   An async function is a step over a frame in the task payload (emit.c, frame mode); a start
   prepares a resumable task of that step and commits it with the frame initializer, which takes
   the arguments from a context of (address, initialization flag) pairs: an argument that owns
   resources moves into the frame and its flag is cleared, any other one is copied. A failed
   commit leaves the context untouched, so the arguments stay with the caller. */

enum {
    R_LLVM_TASK_START_OK = 0,
    R_LLVM_TASK_START_INVALID = 1,
    R_LLVM_TASK_START_ALLOCATION_FAILED = 2,
    R_LLVM_TASK_START_RUNTIME_STOPPING = 3
};

/* RRuntimeTypeInfo {size, alignment, move_initialize, drop} in memory. */
static LLVMValueRef r_llvm_task_type_info(RLlvmEmitter *emitter,
                                          LLVMValueRef size,
                                          LLVMValueRef alignment,
                                          LLVMValueRef move,
                                          LLVMValueRef drop) {
    uint32_t info_size = 0U;
    uint32_t info_align = 0U;
    uint32_t offsets[4];
    LLVMValueRef info;

    if (!r_llvm_runtime_layout(emitter, "RRuntimeTypeInfo", &info_size, &info_align) ||
        !r_llvm_runtime_field(emitter, "RRuntimeTypeInfo", "size", &offsets[0], NULL) ||
        !r_llvm_runtime_field(emitter, "RRuntimeTypeInfo", "alignment", &offsets[1], NULL) ||
        !r_llvm_runtime_field(emitter, "RRuntimeTypeInfo", "move_initialize", &offsets[2], NULL) ||
        !r_llvm_runtime_field(emitter, "RRuntimeTypeInfo", "drop", &offsets[3], NULL)) {
        return NULL;
    }
    info = r_llvm_entry_alloca(emitter, info_size, info_align, "type_info");
    (void)LLVMBuildStore(emitter->builder, size, r_llvm_byte_offset(emitter, info, offsets[0]));
    (void)LLVMBuildStore(
        emitter->builder, alignment, r_llvm_byte_offset(emitter, info, offsets[1]));
    (void)LLVMBuildStore(emitter->builder,
                         move == NULL ? LLVMConstNull(r_llvm_pointer(emitter)) : move,
                         r_llvm_byte_offset(emitter, info, offsets[2]));
    (void)LLVMBuildStore(emitter->builder,
                         drop == NULL ? LLVMConstNull(r_llvm_pointer(emitter)) : drop,
                         r_llvm_byte_offset(emitter, info, offsets[3]));
    return info;
}

/* The type of what the task of an async function completes with: its effect carrier, its result,
   or none. */
static RTypeId r_llvm_task_completion(RLlvmEmitter *emitter, const RSemanticSymbol *function) {
    if (function->effect_carrier_type != R_TYPE_ID_INVALID) {
        return function->effect_carrier_type;
    }
    return r_llvm_type_is_void(emitter, function->return_type) ? R_TYPE_ID_INVALID
                                                               : function->return_type;
}

/* The context of a start: per argument its address and the address of its flag (or null). A
   staged move passes the place it would move from (R-OWN, is_async_staged_move). */
static LLVMValueRef r_llvm_task_context(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const uint32_t count = instruction->operand_count;
    LLVMValueRef context =
        r_llvm_entry_alloca(emitter, 16U * (count == 0U ? 1U : count), 8U, "context");
    uint32_t index;

    for (index = 0U; index < count; ++index) {
        const RMirValueId argument =
            emitter->frontend->mir_operands[(size_t)instruction->first_operand + index];
        const RMirInstruction *definition = r_llvm_definition(emitter, argument);
        LLVMValueRef address;
        LLVMValueRef flag;
        if (definition == NULL) {
            (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
            return NULL;
        }
        if (r_llvm_staged_move(emitter, definition)) {
            address = r_llvm_place_address(emitter,
                                           definition->place_ordinal,
                                           definition->place_is_parameter,
                                           definition->operand1);
            flag = definition->operand1 == R_MIR_VALUE_ID_INVALID
                       ? r_llvm_place_flag(
                             emitter, definition->place_ordinal, definition->place_is_parameter)
                       : NULL;
        } else if (r_llvm_scalar_type(emitter, definition->type) != NULL) {
            uint32_t size = 0U;
            uint32_t align = 0U;
            LLVMValueRef value = r_llvm_value(emitter, argument);
            if ((value == NULL) || !r_llvm_layout(emitter, definition->type, &size, &align)) {
                return NULL;
            }
            address = r_llvm_entry_alloca(emitter, size, align, "argument");
            r_llvm_store_scalar(emitter, definition->type, value, address);
            flag = NULL;
        } else {
            address = r_llvm_value(emitter, argument);
            flag = r_llvm_value_flag(emitter, argument);
        }
        if (address == NULL) {
            return NULL;
        }
        (void)LLVMBuildStore(
            emitter->builder, address, r_llvm_byte_offset(emitter, context, 16U * index));
        (void)LLVMBuildStore(emitter->builder,
                             flag == NULL ? LLVMConstNull(r_llvm_pointer(emitter)) : flag,
                             r_llvm_byte_offset(emitter, context, 16U * index + 8U));
    }
    return context;
}

/* A staged MOVE of a value that requires drop is done by the start that uses it, which takes it
   from its place once the start commits; a staged MOVE of any other value is a plain copy
   (r_c17_emit_async_instruction). */
bool r_llvm_staged_move(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    return (instruction->kind == R_MIR_INSTRUCTION_MOVE) && instruction->is_async_staged_move &&
           r_llvm_type_requires_drop(emitter, instruction->type);
}

/* r_async_start_N: prepares the resumable task of an async function and commits it with its frame
   initializer and the context; the RRuntimeTaskStartResult in memory, with the status of a
   failed preparation. */
LLVMValueRef
r_llvm_start_task(RLlvmEmitter *emitter, RSymbolId callee, LLVMValueRef context, unsigned mode) {
    const RSemanticSymbol *function = &emitter->frontend->semantic_symbols[(size_t)callee - 1U];
    LLVMValueRef payload_type;
    LLVMValueRef result_type;
    LLVMValueRef prepared;
    LLVMValueRef started;
    LLVMValueRef arguments[4];
    LLVMBasicBlockRef commit;
    LLVMBasicBlockRef done;
    RTypeId completion;
    uint32_t prepared_size = 0U;
    uint32_t prepared_align = 0U;
    uint32_t started_size = 0U;
    uint32_t started_align = 0U;
    uint32_t transaction_offset = 0U;
    uint32_t prepared_status_offset = 0U;
    uint32_t task_offset = 0U;
    uint32_t started_status_offset = 0U;

    if ((emitter->async_initializers[callee] == NULL) || (emitter->functions[callee] == NULL)) {
        (void)r_llvm_unsupported(emitter, "a start of a function that is not lowered");
        return NULL;
    }
    if (!r_llvm_runtime_layout(
            emitter, "RRuntimeTaskPrepareResult", &prepared_size, &prepared_align) ||
        !r_llvm_runtime_field(
            emitter, "RRuntimeTaskPrepareResult", "transaction", &transaction_offset, NULL) ||
        !r_llvm_runtime_field(
            emitter, "RRuntimeTaskPrepareResult", "status", &prepared_status_offset, NULL) ||
        !r_llvm_runtime_layout(emitter, "RRuntimeTaskStartResult", &started_size, &started_align) ||
        !r_llvm_runtime_field(emitter, "RRuntimeTaskStartResult", "task", &task_offset, NULL) ||
        !r_llvm_runtime_field(
            emitter, "RRuntimeTaskStartResult", "status", &started_status_offset, NULL)) {
        return NULL;
    }
    payload_type = r_llvm_task_type_info(
        emitter,
        LLVMBuildLoad2(
            emitter->builder, r_llvm_int(emitter, 64U), emitter->async_frame_sizes[callee], ""),
        r_llvm_u64(emitter, 16U),
        NULL,
        emitter->async_drops[callee]);
    completion = r_llvm_task_completion(emitter, function);
    result_type = completion == R_TYPE_ID_INVALID
                      ? r_llvm_task_type_info(
                            emitter, r_llvm_u64(emitter, 0U), r_llvm_u64(emitter, 1U), NULL, NULL)
                      : r_llvm_type_info(emitter, completion);
    if ((payload_type == NULL) || (result_type == NULL)) {
        return NULL;
    }
    prepared = r_llvm_entry_alloca(emitter, prepared_size, prepared_align, "prepared");
    started = r_llvm_entry_alloca(emitter, started_size, started_align, "started");
    arguments[0] = payload_type;
    arguments[1] = result_type;
    arguments[2] = emitter->functions[callee];
    if (r_llvm_call_runtime(
            emitter, "r_runtime_task_resumable_start_prepare", arguments, 3U, prepared) == NULL) {
        return NULL;
    }
    (void)LLVMBuildStore(emitter->builder,
                         LLVMConstNull(r_llvm_pointer(emitter)),
                         r_llvm_byte_offset(emitter, started, task_offset));
    (void)LLVMBuildStore(
        emitter->builder,
        LLVMBuildLoad2(emitter->builder,
                       r_llvm_int(emitter, 32U),
                       r_llvm_byte_offset(emitter, prepared, prepared_status_offset),
                       ""),
        r_llvm_byte_offset(emitter, started, started_status_offset));
    commit = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    done = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    (void)LLVMBuildCondBr(
        emitter->builder,
        LLVMBuildICmp(emitter->builder,
                      LLVMIntEQ,
                      LLVMBuildLoad2(emitter->builder,
                                     r_llvm_int(emitter, 32U),
                                     r_llvm_byte_offset(emitter, prepared, prepared_status_offset),
                                     ""),
                      r_llvm_u32(emitter, R_LLVM_TASK_START_OK),
                      ""),
        commit,
        done);
    LLVMPositionBuilderAtEnd(emitter->builder, commit);
    arguments[0] = r_llvm_byte_offset(emitter, prepared, transaction_offset);
    arguments[1] = emitter->async_initializers[callee];
    arguments[2] = context == NULL ? LLVMConstNull(r_llvm_pointer(emitter)) : context;
    if (mode == 2U) {
        if (r_llvm_call_runtime(emitter,
                                "r_runtime_task_start_commit_initialize_deferred",
                                arguments,
                                3U,
                                started) == NULL) {
            return NULL;
        }
    } else if (mode == 1U) {
        arguments[3] = r_llvm_stack_entry(emitter, callee);
        if ((arguments[3] == NULL) ||
            (r_llvm_call_runtime(emitter,
                                 "r_runtime_task_start_commit_initialize_inline",
                                 arguments,
                                 4U,
                                 started) == NULL)) {
            return NULL;
        }
    } else if (r_llvm_call_runtime(
                   emitter, "r_runtime_task_start_commit_initialize", arguments, 3U, started) ==
               NULL) {
        return NULL;
    }
    /* The initializer runs inside the commit, on this stack. */
    if (!r_llvm_add_stack_edge(emitter, emitter->function, emitter->async_initializers[callee])) {
        return NULL;
    }
    (void)LLVMBuildBr(emitter->builder, done);
    LLVMPositionBuilderAtEnd(emitter->builder, done);
    return started;
}

/* The tag of std.async::start_error in the carrier of a start. */
static uint32_t r_llvm_start_error_tag(RLlvmEmitter *emitter, RTypeId carrier_type) {
    const RSemanticType *carrier = r_llvm_type(emitter, r_llvm_value_type(emitter, carrier_type));
    uint32_t index;

    if ((carrier == NULL) || (carrier->kind != R_SEMANTIC_TYPE_EFFECT_CARRIER)) {
        return 0U;
    }
    for (index = 0U; index < r_semantic_effect_count(emitter->frontend, carrier->second); ++index) {
        if (r_llvm_standard_named(emitter,
                                  r_semantic_effect_at(emitter->frontend, carrier->second, index),
                                  "std.async::start_error")) {
            return index + 1U;
        }
    }
    return 0U;
}

/* r_async_launch_N: the start carrier of a started task, or of the start error. */
static bool r_llvm_launch(RLlvmEmitter *emitter,
                          const RMirInstruction *instruction,
                          const RSemanticSymbol *function,
                          LLVMValueRef started,
                          LLVMValueRef result,
                          uint32_t error_tag) {
    LLVMValueRef status;
    LLVMBasicBlockRef done;
    LLVMBasicBlockRef ok;
    LLVMBasicBlockRef refused;
    LLVMBasicBlockRef stopping;
    LLVMBasicBlockRef invalid;
    LLVMValueRef choice;
    uint32_t task_offset = 0U;
    uint32_t started_status_offset = 0U;
    uint32_t payload = 0U;
    int64_t budget = 0;
    int64_t allocation = 0;
    int64_t runtime_stopping = 0;

    if (!r_llvm_runtime_field(emitter, "RRuntimeTaskStartResult", "task", &task_offset, NULL) ||
        !r_llvm_runtime_field(
            emitter, "RRuntimeTaskStartResult", "status", &started_status_offset, NULL) ||
        !r_llvm_payload_offset(emitter, instruction->type, &payload) ||
        !r_llvm_runtime_constant(emitter, "R_STD_ASYNC_START_BUDGET_EXHAUSTED", &budget) ||
        !r_llvm_runtime_constant(emitter, "R_STD_ASYNC_START_ALLOCATION_FAILED", &allocation) ||
        !r_llvm_runtime_constant(
            emitter, "R_STD_ASYNC_START_RUNTIME_STOPPING", &runtime_stopping)) {
        return false;
    }
    status = LLVMBuildLoad2(emitter->builder,
                            r_llvm_int(emitter, 32U),
                            r_llvm_byte_offset(emitter, started, started_status_offset),
                            "");
    ok = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    refused = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    stopping = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    invalid = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    done = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    choice = LLVMBuildSwitch(emitter->builder, status, invalid, 3U);
    LLVMAddCase(choice, r_llvm_u32(emitter, R_LLVM_TASK_START_OK), ok);
    LLVMAddCase(choice, r_llvm_u32(emitter, R_LLVM_TASK_START_ALLOCATION_FAILED), refused);
    LLVMAddCase(choice, r_llvm_u32(emitter, R_LLVM_TASK_START_RUNTIME_STOPPING), stopping);
    LLVMPositionBuilderAtEnd(emitter->builder, ok);
    (void)LLVMBuildStore(emitter->builder,
                         LLVMBuildLoad2(emitter->builder,
                                        r_llvm_pointer(emitter),
                                        r_llvm_byte_offset(emitter, started, task_offset),
                                        ""),
                         r_llvm_byte_offset(emitter, result, payload));
    (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 0U), result);
    (void)LLVMBuildBr(emitter->builder, done);
    LLVMPositionBuilderAtEnd(emitter->builder, refused);
    {
        LLVMValueRef by_budget =
            r_llvm_call_runtime(emitter, "r_runtime_allocation_refused_by_budget", NULL, 0U, NULL);
        if (by_budget == NULL) {
            return false;
        }
        (void)LLVMBuildStore(emitter->builder,
                             LLVMBuildSelect(emitter->builder,
                                             by_budget,
                                             r_llvm_u32(emitter, (uint64_t)budget),
                                             r_llvm_u32(emitter, (uint64_t)allocation),
                                             ""),
                             r_llvm_byte_offset(emitter, result, payload));
        (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, error_tag), result);
        (void)LLVMBuildBr(emitter->builder, done);
    }
    LLVMPositionBuilderAtEnd(emitter->builder, stopping);
    (void)LLVMBuildStore(emitter->builder,
                         r_llvm_u32(emitter, (uint64_t)runtime_stopping),
                         r_llvm_byte_offset(emitter, result, payload));
    (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, error_tag), result);
    (void)LLVMBuildBr(emitter->builder, done);
    LLVMPositionBuilderAtEnd(emitter->builder, invalid);
    if (!r_llvm_panic(emitter,
                      "R_RUNTIME_PANIC_CONTRACT_VIOLATION",
                      function->name_span,
                      R_MIR_BLOCK_ID_INVALID)) {
        return false;
    }
    LLVMPositionBuilderAtEnd(emitter->builder, done);
    return true;
}

/* The frame storage of a task scope of the step (r_c17_scope_reference): the scope and its
   entries, reserved at the first reference. */
static LLVMValueRef
r_llvm_scope_storage(RLlvmEmitter *emitter, RSymbolId symbol, LLVMValueRef *entries) {
    uint32_t scope_size = 0U;
    uint32_t scope_align = 0U;
    uint32_t entry_size = 0U;
    uint32_t entry_align = 0U;
    uint64_t capacity = 0U;
    uint32_t block_index;
    size_t index;

    for (index = 0U; index < emitter->scope_count; ++index) {
        if (emitter->scope_symbols[index] == symbol) {
            *entries = emitter->scope_entries[index];
            return emitter->scope_storage[index];
        }
    }
    for (block_index = 0U; block_index < emitter->mir->block_count; ++block_index) {
        const RMirBlock *block =
            &emitter->frontend->mir_blocks[(size_t)emitter->mir->first_block + block_index];
        uint32_t position;
        for (position = 0U; position < block->instruction_count; ++position) {
            const RMirInstruction *candidate =
                &emitter->frontend->mir_instructions[(size_t)block->first_instruction + position];
            if ((candidate->kind == R_MIR_INSTRUCTION_TASK_SCOPE_ENTER) &&
                (candidate->symbol == symbol)) {
                capacity = candidate->integer_value;
            }
        }
    }
    if ((emitter->frame == NULL) ||
        !r_llvm_runtime_layout(emitter, "RRuntimeTaskScope", &scope_size, &scope_align) ||
        !r_llvm_runtime_layout(emitter, "RRuntimeTaskScopeEntry", &entry_size, &entry_align)) {
        (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        return NULL;
    }
    if (emitter->scope_count == emitter->scope_capacity) {
        const size_t grown = emitter->scope_capacity == 0U ? 4U : emitter->scope_capacity * 2U;
        RSymbolId *symbols = r_llvm_allocate(emitter, grown * sizeof(*symbols));
        LLVMValueRef *storage = r_llvm_allocate(emitter, grown * sizeof(*storage));
        LLVMValueRef *entry_storage = r_llvm_allocate(emitter, grown * sizeof(*entry_storage));
        if ((symbols == NULL) || (storage == NULL) || (entry_storage == NULL)) {
            return NULL;
        }
        if (emitter->scope_count != 0U) {
            (void)memcpy(symbols, emitter->scope_symbols, emitter->scope_count * sizeof(*symbols));
            (void)memcpy(storage, emitter->scope_storage, emitter->scope_count * sizeof(*storage));
            (void)memcpy(entry_storage,
                         emitter->scope_entries,
                         emitter->scope_count * sizeof(*entry_storage));
        }
        r_llvm_free(emitter, emitter->scope_symbols);
        r_llvm_free(emitter, emitter->scope_storage);
        r_llvm_free(emitter, emitter->scope_entries);
        emitter->scope_symbols = symbols;
        emitter->scope_storage = storage;
        emitter->scope_entries = entry_storage;
        emitter->scope_capacity = grown;
    }
    emitter->scope_symbols[emitter->scope_count] = symbol;
    emitter->scope_storage[emitter->scope_count] =
        r_llvm_entry_alloca(emitter, scope_size, scope_align, "scope");
    emitter->scope_entries[emitter->scope_count] = r_llvm_entry_alloca(
        emitter, (uint32_t)(entry_size * (capacity == 0U ? 1U : capacity)), entry_align, "entries");
    *entries = emitter->scope_entries[emitter->scope_count];
    emitter->scope_count += 1U;
    return emitter->scope_storage[emitter->scope_count - 1U];
}

static bool
r_llvm_unscoped_start(RLlvmEmitter *emitter, const RMirInstruction *instruction, unsigned mode);

/* The tag that selects the target of a dispatcher's start: the tag of the receiver of an
   interface, or the number a function value holds (R-TYPE-0051, R-TYPE-0054). */
static LLVMValueRef r_llvm_dispatch_selector(RLlvmEmitter *emitter,
                                             const RMirInstruction *instruction,
                                             bool function_value) {
    const RMirValueId receiver = emitter->frontend->mir_operands[instruction->first_operand];
    const RMirInstruction *definition = r_llvm_definition(emitter, receiver);
    LLVMValueRef value = r_llvm_value(emitter, receiver);

    if ((value == NULL) || (definition == NULL)) {
        return NULL;
    }
    if (!function_value) {
        /* struct { void *r_pointer; uint32_t r_tag; } */
        return LLVMBuildLoad2(
            emitter->builder, r_llvm_int(emitter, 32U), r_llvm_byte_offset(emitter, value, 8U), "");
    }
    return r_llvm_value_kind(emitter, definition->type) == R_SEMANTIC_TYPE_BORROW
               ? LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), value, "")
               : value;
}

/* The member start of a target: the receiver of an interface passes its pointer, which the
   context finds at the start of the receiver; a function value is not an argument. */
static bool r_llvm_dispatch_member(RLlvmEmitter *emitter,
                                   const RMirInstruction *instruction,
                                   bool function_value,
                                   RDynTarget target,
                                   RMirInstruction *member,
                                   uint32_t *tag) {
    const RFrontendContext *context = emitter->frontend;

    *member = *instruction;
    member->symbol = target.target;
    *tag = (uint32_t)target.target;
    if (function_value) {
        member->first_operand += 1U;
        member->operand_count -= 1U;
        return true;
    }
    {
        const RTypeId interface = r_semantic_dyn_referent(
            context,
            context->semantic_parameter_types[context->semantic_symbols[instruction->symbol - 1U]
                                                  .first_parameter_type]);
        if (r_semantic_dyn_format_target(context, target.target)) {
            return r_llvm_unsupported(emitter, "the format glue of an interface member");
        }
        return r_semantic_dyn_tag(context, interface, target.member, tag)
                   ? true
                   : r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
}

/* R-TYPE-0051, R-TYPE-0054: the start of a dispatcher starts the target its tag selects
   (r_c17_emit_dyn_start_members). */
static bool
r_llvm_dispatch_start(RLlvmEmitter *emitter, const RMirInstruction *instruction, unsigned mode) {
    const RFrontendContext *context = emitter->frontend;
    const bool function_value = r_semantic_function_value_dispatcher(context, instruction->symbol);
    LLVMValueRef selector;
    LLVMValueRef choice;
    LLVMBasicBlockRef unknown;
    LLVMBasicBlockRef done;
    unsigned cases = 0U;
    size_t index;

    if (instruction->operand_count == 0U) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    /* The result has storage even when no target can be selected and the start only panics:
       later instructions of the block still name it. */
    if (r_llvm_value_memory(emitter, instruction->result, instruction->type) == NULL) {
        return false;
    }
    selector = r_llvm_dispatch_selector(emitter, instruction, function_value);
    if (selector == NULL) {
        return false;
    }
    for (index = 0U; index < context->dyn_target_count; ++index) {
        cases += context->dyn_targets[index].dispatcher == instruction->symbol ? 1U : 0U;
    }
    unknown = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    done = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    choice = LLVMBuildSwitch(emitter->builder, selector, unknown, cases);
    for (index = 0U; index < context->dyn_target_count; ++index) {
        RMirInstruction member;
        LLVMBasicBlockRef block;
        uint32_t tag = 0U;
        if (context->dyn_targets[index].dispatcher != instruction->symbol) {
            continue;
        }
        if (!r_llvm_dispatch_member(
                emitter, instruction, function_value, context->dyn_targets[index], &member, &tag)) {
            return false;
        }
        block = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        LLVMAddCase(choice, r_llvm_u32(emitter, tag), block);
        LLVMPositionBuilderAtEnd(emitter->builder, block);
        if (!r_llvm_unscoped_start(emitter, &member, mode)) {
            return false;
        }
        (void)LLVMBuildBr(emitter->builder, done);
    }
    LLVMPositionBuilderAtEnd(emitter->builder, unknown);
    if (!r_llvm_panic(emitter,
                      "R_RUNTIME_PANIC_CONTRACT_VIOLATION",
                      instruction->span,
                      R_MIR_BLOCK_ID_INVALID)) {
        return false;
    }
    LLVMPositionBuilderAtEnd(emitter->builder, done);
    return true;
}

/* The stack bound of the target a dispatcher's start selected, for its first step. */
static LLVMValueRef r_llvm_dispatch_stack_entry(RLlvmEmitter *emitter,
                                                const RMirInstruction *instruction) {
    const RFrontendContext *context = emitter->frontend;
    const bool function_value = r_semantic_function_value_dispatcher(context, instruction->symbol);
    LLVMValueRef selector = r_llvm_dispatch_selector(emitter, instruction, function_value);
    LLVMValueRef bound = r_llvm_u64(emitter, 0U);
    size_t index;

    if (selector == NULL) {
        return NULL;
    }
    for (index = 0U; index < context->dyn_target_count; ++index) {
        RMirInstruction member;
        LLVMValueRef entry;
        uint32_t tag = 0U;
        if (context->dyn_targets[index].dispatcher != instruction->symbol) {
            continue;
        }
        if (!r_llvm_dispatch_member(
                emitter, instruction, function_value, context->dyn_targets[index], &member, &tag)) {
            return NULL;
        }
        entry = r_llvm_stack_entry(emitter, member.symbol);
        if (entry == NULL) {
            return NULL;
        }
        bound = LLVMBuildSelect(
            emitter->builder,
            LLVMBuildICmp(emitter->builder, LLVMIntEQ, selector, r_llvm_u32(emitter, tag), ""),
            entry,
            bound,
            "");
    }
    return bound;
}

/* The start of an async function, outside a group or as the start of a reserved member. */
static bool
r_llvm_unscoped_start(RLlvmEmitter *emitter, const RMirInstruction *instruction, unsigned mode) {
    const RSymbolId callee = instruction->symbol;
    const RSemanticSymbol *function =
        (callee == R_SYMBOL_ID_INVALID) ||
                ((size_t)callee > emitter->frontend->semantic_symbol_count)
            ? NULL
            : &emitter->frontend->semantic_symbols[(size_t)callee - 1U];
    const uint32_t error_tag = r_llvm_start_error_tag(emitter, instruction->type);
    LLVMValueRef context;
    LLVMValueRef started;
    LLVMValueRef result;

    if ((function == NULL) || (error_tag == 0U)) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    if (r_semantic_dyn_dispatcher(emitter->frontend, callee) ||
        r_semantic_function_value_dispatcher(emitter->frontend, callee)) {
        return r_llvm_dispatch_start(emitter, instruction, mode);
    }
    if ((mode == 1U) && (emitter->frame != NULL) && (emitter->direct_twins[callee] != NULL)) {
        const RMirInstruction *await = r_llvm_direct_await(emitter, emitter->mir, instruction);
        if ((await != NULL) && !r_llvm_emit_direct_call(emitter, instruction, await)) {
            return false;
        }
    }
    result = r_llvm_value_memory(emitter, instruction->result, instruction->type);
    context = result == NULL ? NULL : r_llvm_task_context(emitter, instruction);
    started = context == NULL ? NULL : r_llvm_start_task(emitter, callee, context, mode);
    if ((started == NULL) ||
        !r_llvm_launch(emitter, instruction, function, started, result, error_tag)) {
        return false;
    }
    r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, instruction->result), true);
    return true;
}

/* r_c17_emit_scoped_start: a start in a group, of an async function or of a library task (an
   ASYNC_START or a STANDARD_CALL with a task scope): a reserved slot of the group, then the start,
   bound to that slot when it succeeds and abandoned otherwise; an immediately awaited member runs
   its first step once it is bound. A full group is the start error SCOPE_FULL. */
static bool r_llvm_scoped_start(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const uint32_t error_tag = r_llvm_start_error_tag(emitter, instruction->type);
    const bool function_start = instruction->kind == R_MIR_INSTRUCTION_ASYNC_START;
    LLVMValueRef entries = NULL;
    LLVMValueRef scope = r_llvm_scope_storage(emitter, instruction->task_scope, &entries);
    LLVMValueRef result = r_llvm_value_memory(emitter, instruction->result, instruction->type);
    LLVMValueRef reservation;
    LLVMValueRef arguments[3];
    LLVMBasicBlockRef full;
    LLVMBasicBlockRef start;
    LLVMBasicBlockRef bind;
    LLVMBasicBlockRef abandon;
    LLVMBasicBlockRef done;
    RMirInstruction member = *instruction;
    uint32_t payload = 0U;
    int64_t scope_full = 0;

    if (error_tag == 0U) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    if ((scope == NULL) || (result == NULL) ||
        !r_llvm_payload_offset(emitter, instruction->type, &payload) ||
        !r_llvm_runtime_constant(emitter, "R_STD_ASYNC_START_SCOPE_FULL", &scope_full)) {
        return false;
    }
    reservation = r_llvm_call_runtime(emitter, "r_runtime_task_scope_reserve", &scope, 1U, NULL);
    if (reservation == NULL) {
        return false;
    }
    full = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    start = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    bind = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    abandon = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    done = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    (void)LLVMBuildCondBr(emitter->builder,
                          LLVMBuildICmp(emitter->builder,
                                        LLVMIntEQ,
                                        reservation,
                                        LLVMConstInt(LLVMTypeOf(reservation), 0U, 0),
                                        ""),
                          full,
                          start);
    LLVMPositionBuilderAtEnd(emitter->builder, full);
    (void)LLVMBuildStore(emitter->builder,
                         r_llvm_u32(emitter, (uint64_t)scope_full),
                         r_llvm_byte_offset(emitter, result, payload));
    (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, error_tag), result);
    (void)LLVMBuildBr(emitter->builder, done);
    LLVMPositionBuilderAtEnd(emitter->builder, start);
    member.task_scope = R_SYMBOL_ID_INVALID;
    if (function_start
            ? !r_llvm_unscoped_start(emitter, &member, instruction->immediate_await ? 2U : 0U)
            : !r_llvm_emit_standard_call(emitter, &member)) {
        return false;
    }
    (void)LLVMBuildCondBr(
        emitter->builder,
        LLVMBuildICmp(emitter->builder,
                      LLVMIntEQ,
                      LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), result, ""),
                      r_llvm_u32(emitter, 0U),
                      ""),
        bind,
        abandon);
    LLVMPositionBuilderAtEnd(emitter->builder, bind);
    arguments[0] = scope;
    arguments[1] = reservation;
    arguments[2] = LLVMBuildLoad2(emitter->builder,
                                  r_llvm_pointer(emitter),
                                  r_llvm_byte_offset(emitter, result, payload),
                                  "");
    if (r_llvm_call_runtime(emitter, "r_runtime_task_scope_bind", arguments, 3U, NULL) == NULL) {
        return false;
    }
    if (function_start && instruction->immediate_await) {
        arguments[0] = arguments[2];
        arguments[1] =
            r_semantic_dyn_dispatcher(emitter->frontend, instruction->symbol) ||
                    r_semantic_function_value_dispatcher(emitter->frontend, instruction->symbol)
                ? r_llvm_dispatch_stack_entry(emitter, instruction)
                : r_llvm_stack_entry(emitter, instruction->symbol);
        if ((arguments[1] == NULL) ||
            (r_llvm_call_runtime(emitter, "r_runtime_task_run_deferred", arguments, 2U, NULL) ==
             NULL)) {
            return false;
        }
    }
    (void)LLVMBuildBr(emitter->builder, done);
    LLVMPositionBuilderAtEnd(emitter->builder, abandon);
    arguments[0] = scope;
    arguments[1] = reservation;
    if (r_llvm_call_runtime(emitter, "r_runtime_task_scope_abandon", arguments, 2U, NULL) == NULL) {
        return false;
    }
    (void)LLVMBuildBr(emitter->builder, done);
    LLVMPositionBuilderAtEnd(emitter->builder, done);
    r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, instruction->result), true);
    return true;
}

bool r_llvm_emit_async_start(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    if (instruction->task_scope != R_SYMBOL_ID_INVALID) {
        return r_llvm_scoped_start(emitter, instruction);
    }
    return r_llvm_unscoped_start(emitter, instruction, instruction->immediate_await ? 1U : 0U);
}

/* r_c17_scope_discard_members: every initialized task of the frame, or started carrier, is
   dropped from the group; a dropped one is no longer initialized. */
static bool r_llvm_scope_discard(RLlvmEmitter *emitter, LLVMValueRef scope) {
    uint32_t block_index = emitter->mir->block_count;

    while (block_index != 0U) {
        const RMirBlock *block;
        uint32_t position;
        block_index -= 1U;
        block = &emitter->frontend->mir_blocks[(size_t)emitter->mir->first_block + block_index];
        position = block->instruction_count;
        while (position != 0U) {
            const RMirInstruction *instruction;
            const RSemanticType *type;
            bool carrier;
            LLVMValueRef storage;
            LLVMValueRef flag;
            LLVMBasicBlockRef test;
            LLVMBasicBlockRef dropped;
            LLVMBasicBlockRef next;
            LLVMValueRef slot;
            uint32_t payload = 0U;
            position -= 1U;
            instruction =
                &emitter->frontend->mir_instructions[(size_t)block->first_instruction + position];
            if ((instruction->kind == R_MIR_INSTRUCTION_FIELD) ||
                (instruction->kind == R_MIR_INSTRUCTION_INDEX) ||
                (instruction->kind == R_MIR_INSTRUCTION_DEREF) ||
                ((instruction->kind != R_MIR_INSTRUCTION_LOCAL) &&
                 (instruction->kind != R_MIR_INSTRUCTION_PARAMETER) &&
                 (instruction->result == R_MIR_VALUE_ID_INVALID))) {
                continue;
            }
            type = r_llvm_type(emitter, r_llvm_value_type(emitter, instruction->type));
            carrier = (type != NULL) && (type->kind == R_SEMANTIC_TYPE_EFFECT_CARRIER) &&
                      (r_llvm_value_kind(emitter, type->base) == R_SEMANTIC_TYPE_TASK);
            if ((type == NULL) || ((type->kind != R_SEMANTIC_TYPE_TASK) && !carrier)) {
                continue;
            }
            if ((instruction->kind == R_MIR_INSTRUCTION_LOCAL) ||
                (instruction->kind == R_MIR_INSTRUCTION_PARAMETER)) {
                const bool parameter = instruction->kind == R_MIR_INSTRUCTION_PARAMETER;
                storage = r_llvm_place_address(
                    emitter, instruction->place_ordinal, parameter, R_MIR_VALUE_ID_INVALID);
                flag = r_llvm_place_flag(emitter, instruction->place_ordinal, parameter);
            } else {
                storage = r_llvm_value_memory(emitter, instruction->result, instruction->type);
                flag = r_llvm_value_flag(emitter, instruction->result);
            }
            if ((storage == NULL) || (flag == NULL) ||
                (carrier && !r_llvm_payload_offset(emitter, instruction->type, &payload))) {
                continue;
            }
            test = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
            dropped = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
            next = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
            {
                LLVMValueRef initialized = LLVMBuildICmp(
                    emitter->builder,
                    LLVMIntNE,
                    LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 8U), flag, ""),
                    LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                    "");
                if (carrier) {
                    initialized = LLVMBuildAnd(
                        emitter->builder,
                        initialized,
                        LLVMBuildICmp(
                            emitter->builder,
                            LLVMIntEQ,
                            LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), storage, ""),
                            r_llvm_u32(emitter, 0U),
                            ""),
                        "");
                }
                (void)LLVMBuildCondBr(emitter->builder, initialized, test, next);
            }
            LLVMPositionBuilderAtEnd(emitter->builder, test);
            {
                LLVMValueRef arguments[2];
                LLVMValueRef taken;
                slot = carrier ? r_llvm_byte_offset(emitter, storage, payload) : storage;
                arguments[0] = scope;
                arguments[1] = slot;
                taken =
                    r_llvm_call_runtime(emitter, "r_runtime_task_scope_drop", arguments, 2U, NULL);
                if (taken == NULL) {
                    return false;
                }
                (void)LLVMBuildCondBr(emitter->builder, taken, dropped, next);
            }
            LLVMPositionBuilderAtEnd(emitter->builder, dropped);
            r_llvm_set_flag(emitter, flag, false);
            (void)LLVMBuildBr(emitter->builder, next);
            LLVMPositionBuilderAtEnd(emitter->builder, next);
        }
    }
    return true;
}

/* TASK_SCOPE_ENTER, TASK_SCOPE_CLOSE and TASK_SCOPE_WAIT (task_scope.inc, R-STMT-0018). */
bool r_llvm_emit_task_scope(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    LLVMValueRef entries = NULL;
    LLVMValueRef scope;

    if (emitter->frame == NULL) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    scope = r_llvm_scope_storage(emitter, instruction->symbol, &entries);
    if (scope == NULL) {
        return false;
    }
    if (instruction->kind == R_MIR_INSTRUCTION_TASK_SCOPE_ENTER) {
        LLVMValueRef arguments[4];
        LLVMValueRef opened;
        arguments[0] = emitter->execution;
        arguments[1] = scope;
        arguments[2] = entries;
        arguments[3] = r_llvm_u64(emitter, instruction->integer_value);
        opened = r_llvm_call_runtime(emitter, "r_runtime_task_scope_open", arguments, 4U, NULL);
        return (opened != NULL) && r_llvm_check(emitter,
                                                LLVMBuildNot(emitter->builder, opened, ""),
                                                "R_RUNTIME_PANIC_CONTRACT_VIOLATION",
                                                instruction->span,
                                                R_MIR_BLOCK_ID_INVALID);
    }
    if (instruction->kind == R_MIR_INSTRUCTION_TASK_SCOPE_CLOSE) {
        LLVMValueRef closed;
        if (instruction->operation == R_TOKEN_KW_DROP) {
            /* group.cancel_all() consumes every unconsumed member and keeps the group. */
            return r_llvm_scope_discard(emitter, scope);
        }
        closed = r_llvm_call_runtime(emitter, "r_runtime_task_scope_close", &scope, 1U, NULL);
        return (closed != NULL) && r_llvm_check(emitter,
                                                LLVMBuildNot(emitter->builder, closed, ""),
                                                "R_RUNTIME_PANIC_CONTRACT_VIOLATION",
                                                instruction->span,
                                                R_MIR_BLOCK_ID_INVALID);
    }
    {
        const bool until = (instruction->integer_value & 2U) != 0U;
        const bool first = (instruction->integer_value & 1U) != 0U;
        const bool select = (instruction->integer_value & 4U) != 0U;
        const bool vacancy = (instruction->integer_value & 8U) != 0U;
        const uint32_t first_member = until ? 1U : 0U;
        const uint32_t members = instruction->operand_count - first_member;
        LLVMValueRef selection = NULL;
        LLVMValueRef ready = NULL;
        LLVMValueRef expired = NULL;
        LLVMValueRef deadline_seconds = NULL;
        LLVMValueRef deadline_nanoseconds = NULL;
        LLVMValueRef arguments[8];
        LLVMValueRef status;
        LLVMValueRef choice;
        LLVMBasicBlockRef suspended;
        LLVMBasicBlockRef ok;
        LLVMBasicBlockRef cancelled;
        LLVMBasicBlockRef invalid;
        const char *name;
        unsigned count = 0U;
        uint32_t index;
        if ((instruction->target0 == R_MIR_BLOCK_ID_INVALID) ||
            ((size_t)instruction->target0 > emitter->mir->block_count) ||
            (instruction->target1 == R_MIR_BLOCK_ID_INVALID) ||
            ((size_t)instruction->target1 > emitter->mir->block_count)) {
            return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
        if ((instruction->operation == R_TOKEN_KW_FINALLY) &&
            (!r_llvm_scope_discard(emitter, scope) ||
             (r_llvm_call_runtime(emitter, "r_runtime_task_scope_cancel", &scope, 1U, NULL) ==
              NULL))) {
            return false;
        }
        if (members != 0U) {
            selection = r_llvm_entry_alloca(emitter, 8U * members, 8U, "selection");
            for (index = first_member; index < instruction->operand_count; ++index) {
                const RMirValueId id =
                    emitter->frontend->mir_operands[(size_t)instruction->first_operand + index];
                const RMirInstruction *borrow = r_llvm_definition(emitter, id);
                LLVMValueRef member;
                if ((borrow == NULL) || (borrow->kind != R_MIR_INSTRUCTION_BORROW)) {
                    return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
                }
                member = r_llvm_place_address(emitter,
                                              borrow->place_ordinal,
                                              borrow->place_is_parameter,
                                              R_MIR_VALUE_ID_INVALID);
                if (member == NULL) {
                    return false;
                }
                (void)LLVMBuildStore(
                    emitter->builder,
                    LLVMBuildLoad2(emitter->builder, r_llvm_pointer(emitter), member, ""),
                    r_llvm_byte_offset(emitter, selection, 8U * (index - first_member)));
            }
        }
        if (until) {
            /* The deadline is an instant, RStdTimeInstant {storage_seconds, storage_nanoseconds}.
             */
            const RMirValueId deadline =
                emitter->frontend->mir_operands[(size_t)instruction->first_operand];
            LLVMValueRef instant = r_llvm_value(emitter, deadline);
            uint32_t seconds = 0U;
            uint32_t nanoseconds = 0U;
            if ((instant == NULL) ||
                !r_llvm_runtime_field(
                    emitter, "RStdTimeInstant", "storage_seconds", &seconds, NULL) ||
                !r_llvm_runtime_field(
                    emitter, "RStdTimeInstant", "storage_nanoseconds", &nanoseconds, NULL)) {
                return false;
            }
            deadline_seconds = LLVMBuildLoad2(emitter->builder,
                                              r_llvm_int(emitter, 64U),
                                              r_llvm_byte_offset(emitter, instant, seconds),
                                              "");
            deadline_nanoseconds = LLVMBuildLoad2(emitter->builder,
                                                  r_llvm_int(emitter, 32U),
                                                  r_llvm_byte_offset(emitter, instant, nanoseconds),
                                                  "");
            expired = r_llvm_entry_alloca(emitter, 1U, 1U, "expired");
            (void)LLVMBuildStore(
                emitter->builder, LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0), expired);
            if (first || select) {
                ready = r_llvm_entry_alloca(emitter, 8U, 8U, "ready");
                (void)LLVMBuildStore(emitter->builder, r_llvm_u64(emitter, 0U), ready);
            }
        }
        /* A suspension resumes this block: the state names it. */
        (void)LLVMBuildStore(
            emitter->builder, r_llvm_u32(emitter, emitter->current_block + 1U), emitter->frame);
        arguments[count++] = emitter->execution;
        arguments[count++] = scope;
        if (vacancy) {
            if (members != 0U) {
                return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
            }
            name = until ? "r_runtime_task_scope_wait_vacancy_until"
                         : "r_runtime_task_scope_wait_vacancy";
            if (until) {
                arguments[count++] = deadline_seconds;
                arguments[count++] = deadline_nanoseconds;
                arguments[count++] = expired;
            }
        } else {
            name = until ? "r_runtime_task_scope_wait_until" : "r_runtime_task_scope_wait";
            arguments[count++] =
                selection == NULL ? LLVMConstNull(r_llvm_pointer(emitter)) : selection;
            arguments[count++] = r_llvm_u64(emitter, members);
            if (until) {
                arguments[count++] = ready == NULL ? LLVMConstNull(r_llvm_pointer(emitter)) : ready;
                arguments[count++] = deadline_seconds;
                arguments[count++] = deadline_nanoseconds;
                arguments[count++] = expired;
            } else if (instruction->result != R_MIR_VALUE_ID_INVALID) {
                /* The index of the ready member, a usize in its frame slot. */
                LLVMValueRef slot =
                    r_llvm_value_memory(emitter, instruction->result, instruction->type);
                if (slot == NULL) {
                    return false;
                }
                emitter->frame_scalars[instruction->result] = 1U;
                arguments[count++] = slot;
            } else {
                arguments[count++] = LLVMConstNull(r_llvm_pointer(emitter));
            }
        }
        status = r_llvm_call_runtime(emitter, name, arguments, count, NULL);
        if (status == NULL) {
            return false;
        }
        suspended = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        ok = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        cancelled = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        invalid = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        choice = LLVMBuildSwitch(emitter->builder, status, invalid, 3U);
        LLVMAddCase(choice, r_llvm_u32(emitter, 3U), suspended); /* SUSPENDED */
        LLVMAddCase(choice, r_llvm_u32(emitter, 0U), ok);        /* OK */
        LLVMAddCase(choice, r_llvm_u32(emitter, 2U), cancelled); /* CANCELLED */
        LLVMPositionBuilderAtEnd(emitter->builder, suspended);
        (void)LLVMBuildRet(emitter->builder, r_llvm_u32(emitter, 1U));
        LLVMPositionBuilderAtEnd(emitter->builder, ok);
        if (until) {
            /* r_c17_emit_scope_bounded_result. */
            LLVMValueRef is_expired = LLVMBuildICmp(
                emitter->builder,
                LLVMIntNE,
                LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 8U), expired, ""),
                LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                "");
            if (select) {
                if (!r_llvm_set_value(
                        emitter,
                        instruction->result,
                        LLVMBuildSelect(
                            emitter->builder,
                            is_expired,
                            r_llvm_u64(emitter, (uint64_t)instruction->operand_count - 1U),
                            LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 64U), ready, ""),
                            ""))) {
                    return false;
                }
            } else if (!first) {
                if (!r_llvm_set_value(emitter,
                                      instruction->result,
                                      LLVMBuildNot(emitter->builder, is_expired, ""))) {
                    return false;
                }
            } else {
                LLVMValueRef option =
                    r_llvm_value_memory(emitter, instruction->result, instruction->type);
                uint32_t payload = 0U;
                if ((option == NULL) ||
                    !r_llvm_payload_offset(emitter, instruction->type, &payload)) {
                    return false;
                }
                (void)LLVMBuildStore(emitter->builder,
                                     LLVMBuildSelect(emitter->builder,
                                                     is_expired,
                                                     r_llvm_u32(emitter, 0U),
                                                     r_llvm_u32(emitter, 1U),
                                                     ""),
                                     option);
                (void)LLVMBuildStore(
                    emitter->builder,
                    LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 64U), ready, ""),
                    r_llvm_byte_offset(emitter, option, payload));
            }
        }
        (void)LLVMBuildBr(emitter->builder, emitter->blocks[instruction->target0 - 1U]);
        LLVMPositionBuilderAtEnd(emitter->builder, cancelled);
        (void)LLVMBuildBr(emitter->builder, emitter->blocks[instruction->target1 - 1U]);
        LLVMPositionBuilderAtEnd(emitter->builder, invalid);
        return r_llvm_panic(emitter,
                            "R_RUNTIME_PANIC_CONTRACT_VIOLATION",
                            instruction->span,
                            R_MIR_BLOCK_ID_INVALID);
    }
}

/* The end of a completed await (r_c17_emit_async_await_completed): the result is initialized; a
   cancellation requested meanwhile drops it and leaves through the cancel block. */
static bool r_llvm_await_completed(RLlvmEmitter *emitter,
                                   const RMirInstruction *instruction,
                                   LLVMValueRef result,
                                   LLVMBasicBlockRef resume,
                                   LLVMBasicBlockRef cancel) {
    LLVMValueRef flag = result == NULL ? NULL : r_llvm_value_flag(emitter, instruction->result);
    LLVMValueRef requested;
    LLVMBasicBlockRef dropped;

    r_llvm_set_flag(emitter, flag, true);
    requested = r_llvm_call_runtime(
        emitter, "r_runtime_task_execution_cancel_requested", &emitter->execution, 1U, NULL);
    if (requested == NULL) {
        return false;
    }
    dropped = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    (void)LLVMBuildCondBr(emitter->builder, requested, dropped, resume);
    LLVMPositionBuilderAtEnd(emitter->builder, dropped);
    if ((flag != NULL) && !r_llvm_drop_flagged(emitter, instruction->type, result, flag)) {
        return false;
    }
    (void)LLVMBuildBr(emitter->builder, cancel);
    return true;
}

/* The direct path of a start that may run directly (direct.c), written before its ordinary start:
   when r_runtime_task_direct_begin admits the bound of the twin on this stack, the twin runs here
   and the await completes as the await of a completed task does; otherwise the start follows. */
static void r_llvm_report_direct(RLlvmEmitter *emitter, const RMirInstruction *await);

bool r_llvm_emit_direct_call(RLlvmEmitter *emitter,
                             const RMirInstruction *start,
                             const RMirInstruction *await) {
    const RSemanticSymbol *callee = &emitter->frontend->semantic_symbols[start->symbol - 1U];
    const bool has_result = !r_llvm_type_is_void(emitter, await->type);
    const bool in_memory = (callee->effect_carrier_type != R_TYPE_ID_INVALID) ||
                           (!r_llvm_type_is_void(emitter, callee->return_type) &&
                            (r_llvm_scalar_type(emitter, callee->return_type) == NULL));
    LLVMValueRef twin = emitter->direct_twins[start->symbol];
    LLVMValueRef result = NULL;
    LLVMValueRef bound;
    LLVMValueRef admitted;
    LLVMValueRef call;
    LLVMValueRef *arguments;
    LLVMBasicBlockRef direct;
    LLVMBasicBlockRef ordinary;
    unsigned count = 0U;
    uint32_t index;

    if ((twin == NULL) || (emitter->frame == NULL)) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    bound = r_llvm_direct_stack_entry(emitter, start->symbol);
    admitted = bound == NULL
                   ? NULL
                   : r_llvm_call_runtime(emitter, "r_runtime_task_direct_begin", &bound, 1U, NULL);
    if (admitted == NULL) {
        return false;
    }
    direct = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    ordinary = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    (void)LLVMBuildCondBr(emitter->builder, admitted, direct, ordinary);
    LLVMPositionBuilderAtEnd(emitter->builder, direct);
    arguments = r_llvm_allocate(emitter, ((size_t)start->operand_count + 1U) * sizeof(*arguments));
    if (arguments == NULL) {
        return false;
    }
    if (has_result) {
        result = r_llvm_value_memory(emitter, await->result, await->type);
        if (result == NULL) {
            r_llvm_free(emitter, arguments);
            return false;
        }
    }
    if (in_memory) {
        arguments[count++] = result;
    }
    for (index = 0U; index < start->operand_count; ++index) {
        const RMirValueId operand =
            emitter->frontend->mir_operands[(size_t)start->first_operand + index];
        const RMirInstruction *definition = r_llvm_definition(emitter, operand);
        LLVMValueRef value = r_llvm_value(emitter, operand);
        if ((value == NULL) || (definition == NULL)) {
            r_llvm_free(emitter, arguments);
            return false;
        }
        if (r_llvm_scalar_type(emitter, definition->type) == NULL) {
            /* The twin owns a copy of an argument in memory, as an ordinary callee does; none of
               them requires drop (direct.c). */
            uint32_t size = 0U;
            uint32_t align = 0U;
            LLVMValueRef copy;
            if (!r_llvm_layout(emitter, definition->type, &size, &align)) {
                r_llvm_free(emitter, arguments);
                return false;
            }
            copy = r_llvm_entry_alloca(emitter, size, align, "argument");
            (void)LLVMBuildMemCpy(
                emitter->builder, copy, align, value, align, r_llvm_u64(emitter, size));
            value = copy;
        }
        arguments[count++] = value;
    }
    call = LLVMBuildCall2(
        emitter->builder, emitter->direct_types[start->symbol], twin, arguments, count, "");
    r_llvm_free(emitter, arguments);
    r_llvm_mark_guarded_call(emitter, call);
    if (r_llvm_call_runtime(emitter, "r_runtime_task_direct_end", NULL, 0U, NULL) == NULL) {
        return false;
    }
    /* L39: a twin that panicked returns with its panic pending; the step continues in the panic
       block of the await. */
    if (!r_llvm_unwind_test(emitter, await->panic_target)) {
        return false;
    }
    if (has_result && !in_memory && !r_llvm_set_value(emitter, await->result, call)) {
        return false;
    }
    if (!r_llvm_await_completed(emitter,
                                await,
                                result,
                                emitter->blocks[await->target0 - 1U],
                                emitter->blocks[await->target1 - 1U])) {
        return false;
    }
    r_llvm_report_direct(emitter, await);
    LLVMPositionBuilderAtEnd(emitter->builder, ordinary);
    return true;
}

/* The await sources !r.direct lists (tests/check_direct_calls.py). */
static void r_llvm_report_direct(RLlvmEmitter *emitter, const RMirInstruction *await) {
    LLVMMetadataRef span[3];

    span[0] =
        LLVMValueAsMetadata(r_llvm_u32(emitter, r_llvm_source_key(emitter, await->span.source)));
    span[1] = LLVMValueAsMetadata(r_llvm_u32(emitter, await->span.start));
    span[2] = LLVMValueAsMetadata(r_llvm_u32(emitter, await->span.end));
    LLVMAddNamedMetadataOperand(
        emitter->module,
        "r.direct",
        LLVMMetadataAsValue(emitter->context, LLVMMDNodeInContext2(emitter->context, span, 3U)));
}

/* An awaited std.sync receive (R-LIB-0016) whose channel holds a value, or has lost every
   sender, completes at once without its task: the value moves out of the channel as the started
   receive would move it, and the empty channel keeps the ordinary path, which waits. The receive
   task would take no part in anything else: it is not counted (R-STMT-0020), and under a budget,
   which still charges its frame, the ordinary path stays
   (r_runtime_task_inline_completion_allowed). */
bool r_llvm_emit_receive_now(RLlvmEmitter *emitter,
                             const RMirInstruction *receive,
                             const RMirInstruction *await) {
    LLVMValueRef result = r_llvm_value_memory(emitter, await->result, await->type);
    LLVMValueRef arguments[2];
    LLVMValueRef allowed;
    LLVMValueRef kind;
    LLVMBasicBlockRef attempt;
    LLVMBasicBlockRef taken;
    LLVMBasicBlockRef ordinary;
    uint32_t payload = 0U;
    int64_t received = 0;
    int64_t empty = 0;

    if ((result == NULL) || !r_llvm_payload_offset(emitter, await->type, &payload) ||
        !r_llvm_runtime_constant(emitter, "R_STD_SYNC_TRY_RECV_RESULT_RECEIVED", &received) ||
        !r_llvm_runtime_constant(emitter, "R_STD_SYNC_TRY_RECV_RESULT_EMPTY", &empty)) {
        return false;
    }
    allowed =
        r_llvm_call_runtime(emitter, "r_runtime_task_inline_completion_allowed", NULL, 0U, NULL);
    arguments[0] = r_llvm_value(emitter, r_llvm_std_operand(emitter, receive, 0U));
    if ((allowed == NULL) || (arguments[0] == NULL)) {
        return false;
    }
    attempt = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    taken = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    ordinary = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    (void)LLVMBuildCondBr(emitter->builder, allowed, attempt, ordinary);
    LLVMPositionBuilderAtEnd(emitter->builder, attempt);
    arguments[1] = r_llvm_byte_offset(emitter, result, payload);
    {
        uint32_t size = 0U;
        uint32_t align = 0U;
        uint32_t kind_offset = 0U;
        LLVMValueRef outcome;
        if (!r_llvm_runtime_layout(emitter, "RStdSyncTryRecvResult", &size, &align) ||
            !r_llvm_runtime_field(emitter, "RStdSyncTryRecvResult", "kind", &kind_offset, NULL)) {
            return false;
        }
        outcome = r_llvm_entry_alloca(emitter, size, align, "received");
        if (r_llvm_call_runtime(emitter, "r_std_sync_try_recv", arguments, 2U, outcome) == NULL) {
            return false;
        }
        kind = LLVMBuildLoad2(emitter->builder,
                              r_llvm_int(emitter, 32U),
                              r_llvm_byte_offset(emitter, outcome, kind_offset),
                              "");
    }
    (void)LLVMBuildCondBr(emitter->builder,
                          LLVMBuildICmp(emitter->builder,
                                        LLVMIntEQ,
                                        kind,
                                        LLVMConstInt(LLVMTypeOf(kind), (uint64_t)empty, 0),
                                        ""),
                          ordinary,
                          taken);
    LLVMPositionBuilderAtEnd(emitter->builder, taken);
    /* o<T>: some when a value was received, none when every sender is gone. */
    (void)LLVMBuildStore(
        emitter->builder,
        LLVMBuildZExt(emitter->builder,
                      LLVMBuildICmp(emitter->builder,
                                    LLVMIntEQ,
                                    kind,
                                    LLVMConstInt(LLVMTypeOf(kind), (uint64_t)received, 0),
                                    ""),
                      r_llvm_int(emitter, 32U),
                      ""),
        result);
    if (!r_llvm_await_completed(emitter,
                                await,
                                result,
                                emitter->blocks[await->target0 - 1U],
                                emitter->blocks[await->target1 - 1U])) {
        return false;
    }
    r_llvm_report_direct(emitter, await);
    LLVMPositionBuilderAtEnd(emitter->builder, ordinary);
    return true;
}

bool r_llvm_emit_await(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const bool has_result = !r_llvm_type_is_void(emitter, instruction->type);
    const bool join = instruction->integer_value == 1U;
    const RSemanticType *join_type =
        join ? r_llvm_type(emitter, r_llvm_value_type(emitter, instruction->type)) : NULL;
    const bool join_returns = (join_type != NULL) && !r_llvm_type_is_void(emitter, join_type->base);
    LLVMValueRef task = r_llvm_place_address(emitter,
                                             instruction->place_ordinal,
                                             instruction->place_is_parameter,
                                             R_MIR_VALUE_ID_INVALID);
    LLVMValueRef task_flag =
        r_llvm_place_flag(emitter, instruction->place_ordinal, instruction->place_is_parameter);
    LLVMValueRef result =
        has_result ? r_llvm_value_memory(emitter, instruction->result, instruction->type) : NULL;
    LLVMValueRef arguments[3];
    LLVMValueRef status;
    LLVMValueRef choice;
    LLVMBasicBlockRef suspended;
    LLVMBasicBlockRef ok;
    LLVMBasicBlockRef cancelled;
    LLVMBasicBlockRef invalid;
    LLVMBasicBlockRef panicked = NULL;
    LLVMBasicBlockRef resume;
    LLVMBasicBlockRef cancel;
    uint32_t payload = 0U;

    if ((emitter->frame == NULL) || (task == NULL) || (has_result && (result == NULL)) ||
        (instruction->target0 == R_MIR_BLOCK_ID_INVALID) ||
        (instruction->target1 == R_MIR_BLOCK_ID_INVALID) ||
        ((size_t)instruction->target0 > emitter->mir->block_count) ||
        ((size_t)instruction->target1 > emitter->mir->block_count) ||
        (join && !r_llvm_payload_offset(emitter, instruction->type, &payload))) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    if (has_result && (r_llvm_scalar_type(emitter, instruction->type) != NULL) &&
        !r_llvm_type_requires_drop(emitter, instruction->type)) {
        /* The runtime writes a scalar result into its frame slot. */
        emitter->frame_scalars[instruction->result] = 1U;
    }
    resume = emitter->blocks[instruction->target0 - 1U];
    cancel = emitter->blocks[instruction->target1 - 1U];
    /* A suspension resumes this block: the state names it. */
    (void)LLVMBuildStore(
        emitter->builder, r_llvm_u32(emitter, emitter->current_block + 1U), emitter->frame);
    arguments[0] = emitter->execution;
    arguments[1] = task;
    arguments[2] = join ? (join_returns ? r_llvm_byte_offset(emitter, result, payload)
                                        : LLVMConstNull(r_llvm_pointer(emitter)))
                        : (has_result ? result : LLVMConstNull(r_llvm_pointer(emitter)));
    status = r_llvm_call_runtime(emitter, "r_runtime_task_execution_await", arguments, 3U, NULL);
    if (status == NULL) {
        return false;
    }
    suspended = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    ok = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    cancelled = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    invalid = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    choice = LLVMBuildSwitch(emitter->builder, status, invalid, 4U);
    LLVMAddCase(choice, r_llvm_u32(emitter, 3U), suspended); /* SUSPENDED */
    LLVMAddCase(choice, r_llvm_u32(emitter, 0U), ok);        /* OK */
    LLVMAddCase(choice, r_llvm_u32(emitter, 2U), cancelled); /* CANCELLED */
    if (join || (instruction->panic_target != R_MIR_BLOCK_ID_INVALID)) {
        panicked = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        LLVMAddCase(choice, r_llvm_u32(emitter, 4U), panicked); /* PANICKED */
    }
    LLVMPositionBuilderAtEnd(emitter->builder, suspended);
    (void)LLVMBuildRet(emitter->builder, r_llvm_u32(emitter, 1U)); /* SUSPENDED */
    LLVMPositionBuilderAtEnd(emitter->builder, ok);
    r_llvm_set_flag(emitter, task_flag, false);
    if (join) {
        (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 0U), result);
    }
    if (!r_llvm_await_completed(emitter, instruction, result, resume, cancel)) {
        return false;
    }
    LLVMPositionBuilderAtEnd(emitter->builder, cancelled);
    r_llvm_set_flag(emitter, task_flag, false);
    (void)LLVMBuildBr(emitter->builder, cancel);
    if (panicked != NULL) {
        LLVMPositionBuilderAtEnd(emitter->builder, panicked);
        r_llvm_set_flag(emitter, task_flag, false);
        if (join) {
            /* R-SLIB-ASYNC-0020 (L39): the panic becomes the panicked report of the result. */
            LLVMValueRef report = r_llvm_byte_offset(emitter, result, payload);
            (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 1U), result);
            if ((r_llvm_call_runtime(emitter, "r_std_async_join", &report, 1U, NULL) == NULL) ||
                !r_llvm_await_completed(emitter, instruction, result, resume, cancel)) {
                return false;
            }
        } else {
            if ((size_t)instruction->panic_target > emitter->mir->block_count) {
                return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
            }
            if (r_llvm_call_runtime(
                    emitter, "r_runtime_task_panic_park", &emitter->execution, 1U, NULL) == NULL) {
                return false;
            }
            (void)LLVMBuildBr(emitter->builder, emitter->blocks[instruction->panic_target - 1U]);
        }
    }
    LLVMPositionBuilderAtEnd(emitter->builder, invalid);
    return r_llvm_panic(
        emitter, "R_RUNTIME_PANIC_CONTRACT_VIOLATION", instruction->span, R_MIR_BLOCK_ID_INVALID);
}

#include "emit_internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* Values and places of the LLVM emitter. A MIR value of a scalar type is an SSA value; a value of
   any other type is the address of memory that holds it, one alloca per MIR value, so a value is
   never changed after its instruction. A place (a parameter or a local) is memory; a projection
   chain of FIELD, INDEX and DEREF instructions names a part of it and becomes an address. The
   semantics of each instruction are those of the C17 emitter's lowering of MIR. */

/* Mirrors the C17 emitter's r_c17_type_requires_drop: a value of the type owns something its
   drop releases. A tagged enum also counts when a variant's payload requires drop, which keeps
   the emitter from lowering such a program until drop glue exists (B5.2). */
static bool r_llvm_requires_drop_depth(RLlvmEmitter *emitter, RTypeId id, uint32_t depth) {
    const RSemanticType *type = r_llvm_type(emitter, r_llvm_value_type(emitter, id));

    if ((type == NULL) || (depth > 256U)) {
        return false;
    }
    switch (type->kind) {
    case R_SEMANTIC_TYPE_OWN:
    case R_SEMANTIC_TYPE_TASK:
    case R_SEMANTIC_TYPE_ARRAY:
    case R_SEMANTIC_TYPE_LIST:
    case R_SEMANTIC_TYPE_DICT:
    case R_SEMANTIC_TYPE_ARC:
    case R_SEMANTIC_TYPE_RC:
    case R_SEMANTIC_TYPE_WEAK:
    case R_SEMANTIC_TYPE_ATOMIC:
        return true;
    case R_SEMANTIC_TYPE_STANDARD: {
        uint32_t size = 0U;
        uint32_t align = 0U;
        if (r_llvm_recovering_error(emitter, id) != R_LLVM_RECOVERING_NONE) {
            return r_llvm_requires_drop_depth(emitter, type->base, depth + 1U) ||
                   ((type->second != R_TYPE_ID_INVALID) &&
                    r_llvm_requires_drop_depth(emitter, type->second, depth + 1U));
        }
        if (r_llvm_compare_exchange_result(emitter, id)) {
            return r_llvm_requires_drop_depth(emitter, type->base, depth + 1U);
        }
        if (r_llvm_standard_c_type(emitter, type, &size, &align) == NULL) {
            /* A standard type the emitter does not lay out yet is treated as owning, which
               keeps it visible. */
            return true;
        }
        return r_llvm_standard_is_move(emitter, type);
    }
    case R_SEMANTIC_TYPE_STRUCT:
    case R_SEMANTIC_TYPE_ENUM: {
        const RSemanticAggregate *aggregate =
            r_llvm_aggregate(emitter, r_llvm_value_type(emitter, id));
        uint32_t index;
        if ((aggregate == NULL) ||
            ((aggregate->kind != R_SEMANTIC_AGGREGATE_STRUCT) && !aggregate->is_tagged)) {
            return false;
        }
        if ((aggregate->drop_function != R_SYMBOL_ID_INVALID) || (aggregate->format_schema != 0U)) {
            return true;
        }
        for (index = 0U; index < aggregate->field_count; ++index) {
            if (r_llvm_requires_drop_depth(
                    emitter,
                    emitter->frontend->semantic_fields[(size_t)aggregate->first_field + index].type,
                    depth + 1U)) {
                return true;
            }
        }
        if (aggregate->is_tagged) {
            for (index = 0U; index < aggregate->variant_count; ++index) {
                const RSemanticVariant *variant =
                    r_semantic_tagged_variant(emitter->frontend, aggregate->type, index);
                if ((variant != NULL) && (variant->payload_type != R_TYPE_ID_INVALID) &&
                    r_llvm_requires_drop_depth(emitter, variant->payload_type, depth + 1U)) {
                    return true;
                }
            }
        }
        return false;
    }
    case R_SEMANTIC_TYPE_FIXED_ARRAY:
    case R_SEMANTIC_TYPE_OPTION:
        return r_llvm_requires_drop_depth(emitter, type->base, depth + 1U);
    case R_SEMANTIC_TYPE_RESULT:
        return (!r_llvm_type_is_void(emitter, type->base) &&
                r_llvm_requires_drop_depth(emitter, type->base, depth + 1U)) ||
               r_llvm_requires_drop_depth(emitter, type->second, depth + 1U);
    case R_SEMANTIC_TYPE_EFFECT_CARRIER: {
        const uint32_t count = r_semantic_effect_count(emitter->frontend, type->second);
        uint32_t index;
        if (!r_llvm_type_is_void(emitter, type->base) &&
            r_llvm_requires_drop_depth(emitter, type->base, depth + 1U)) {
            return true;
        }
        for (index = 0U; index < count; ++index) {
            if (r_llvm_requires_drop_depth(
                    emitter,
                    r_semantic_effect_at(emitter->frontend, type->second, index),
                    depth + 1U)) {
                return true;
            }
        }
        return false;
    }
    default:
        return false;
    }
}

bool r_llvm_type_requires_drop(RLlvmEmitter *emitter, RTypeId id) {
    return r_llvm_requires_drop_depth(emitter, id, 0U);
}

/* ---- Values ---- */

const RMirInstruction *r_llvm_definition(const RLlvmEmitter *emitter, RMirValueId id) {
    return ((id == R_MIR_VALUE_ID_INVALID) || ((size_t)id >= emitter->value_count))
               ? NULL
               : emitter->definitions[id];
}

LLVMValueRef r_llvm_value(RLlvmEmitter *emitter, RMirValueId id) {
    if ((id != R_MIR_VALUE_ID_INVALID) && ((size_t)id < emitter->value_count) &&
        (emitter->values[id] == NULL) && (emitter->definitions[id] != NULL) &&
        ((emitter->definitions[id]->kind == R_MIR_INSTRUCTION_CALL) ||
         r_llvm_type_is_void(emitter, emitter->definitions[id]->type))) {
        /* A value of a call that does not return one (never, void) has no representation. */
        (void)r_llvm_unsupported(emitter, "a use of a value that does not exist");
        return NULL;
    }
    if ((id == R_MIR_VALUE_ID_INVALID) || ((size_t)id >= emitter->value_count) ||
        (emitter->values[id] == NULL)) {
        (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        return NULL;
    }
    if (emitter->frame_scalars[id] != 0U) {
        /* A scalar a step keeps in its frame is read from where it was written. A value that
           owns resources is its memory, whatever its type (the moves take its address). */
        return r_llvm_load_scalar(emitter, emitter->definitions[id]->type, emitter->values[id]);
    }
    return emitter->values[id];
}

bool r_llvm_set_value(RLlvmEmitter *emitter, RMirValueId id, LLVMValueRef value) {
    if ((id == R_MIR_VALUE_ID_INVALID) || ((size_t)id >= emitter->value_count) || (value == NULL)) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    if ((emitter->frame != NULL) && (emitter->definitions[id] != NULL)) {
        /* A step keeps every value in the frame, since a use may follow a suspension. */
        const RTypeId type = emitter->definitions[id]->type;
        LLVMValueRef slot = r_llvm_value_memory(emitter, id, type);
        if (slot == NULL) {
            return false;
        }
        if (r_llvm_scalar_type(emitter, type) != NULL) {
            r_llvm_store_scalar(emitter, type, value, slot);
            emitter->frame_scalars[id] = r_llvm_type_requires_drop(emitter, type) ? 0U : 1U;
            return true;
        }
        return (slot == value) || r_llvm_store_value(emitter, type, value, slot);
    }
    emitter->values[id] = value;
    emitter->frame_scalars[id] = 0U;
    return true;
}

static LLVMValueRef
r_llvm_destination_memory(RLlvmEmitter *emitter, RMirValueId id, const RMirInstruction *from);

static LLVMValueRef r_llvm_value_memory_from(RLlvmEmitter *emitter,
                                             RMirValueId id,
                                             RTypeId type,
                                             const RMirInstruction *from) {
    uint32_t size = 0U;
    uint32_t align = 0U;

    if ((id == R_MIR_VALUE_ID_INVALID) || ((size_t)id >= emitter->value_count)) {
        (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        return NULL;
    }
    if (emitter->values[id] != NULL) {
        return emitter->values[id];
    }
    emitter->values[id] = r_llvm_destination_memory(emitter, id, from);
    if (emitter->values[id] != NULL) {
        return emitter->values[id];
    }
    if ((emitter->status != R_FRONTEND_OK) || !r_llvm_layout(emitter, type, &size, &align)) {
        return NULL;
    }
    emitter->values[id] = r_llvm_entry_alloca(emitter, size, align, "");
    return emitter->values[id];
}

LLVMValueRef r_llvm_value_memory(RLlvmEmitter *emitter, RMirValueId id, RTypeId type) {
    return r_llvm_value_memory_from(
        emitter, id, type, ((size_t)id < emitter->value_count) ? emitter->definitions[id] : NULL);
}

LLVMValueRef r_llvm_load_scalar(RLlvmEmitter *emitter, RTypeId type, LLVMValueRef pointer) {
    LLVMTypeRef scalar = r_llvm_scalar_type(emitter, type);

    if (scalar == NULL) {
        (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        return NULL;
    }
    if (r_llvm_value_kind(emitter, type) == R_SEMANTIC_TYPE_BOOL) {
        LLVMValueRef byte = LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 8U), pointer, "");
        return LLVMBuildTrunc(emitter->builder, byte, r_llvm_int(emitter, 1U), "");
    }
    return LLVMBuildLoad2(emitter->builder, scalar, pointer, "");
}

void r_llvm_store_scalar(RLlvmEmitter *emitter,
                         RTypeId type,
                         LLVMValueRef value,
                         LLVMValueRef pointer) {
    if (r_llvm_value_kind(emitter, type) == R_SEMANTIC_TYPE_BOOL) {
        value = LLVMBuildZExt(emitter->builder, value, r_llvm_int(emitter, 8U), "");
    }
    (void)LLVMBuildStore(emitter->builder, value, pointer);
}

bool r_llvm_store_value(RLlvmEmitter *emitter,
                        RTypeId type,
                        LLVMValueRef value,
                        LLVMValueRef pointer) {
    uint32_t size = 0U;
    uint32_t align = 0U;

    if ((value == NULL) || (pointer == NULL)) {
        return false;
    }
    if (r_llvm_scalar_type(emitter, type) != NULL) {
        r_llvm_store_scalar(emitter, type, value, pointer);
        return true;
    }
    if (!r_llvm_layout(emitter, type, &size, &align)) {
        return false;
    }
    (void)LLVMBuildMemCpy(
        emitter->builder, pointer, align, value, align, r_llvm_u64(emitter, size));
    return true;
}

bool r_llvm_load_into(RLlvmEmitter *emitter,
                      RMirValueId result,
                      RTypeId type,
                      LLVMValueRef pointer) {
    LLVMValueRef memory;

    if (pointer == NULL) {
        return false;
    }
    if (r_llvm_scalar_type(emitter, type) != NULL) {
        return r_llvm_set_value(emitter, result, r_llvm_load_scalar(emitter, type, pointer));
    }
    memory = r_llvm_value_memory(emitter, result, type);
    return (memory != NULL) && r_llvm_store_value(emitter, type, pointer, memory);
}

/* ---- Initialization flags ---- */

/* An i8 flag in the entry block with its initial value, stored before anything reads it. */
static LLVMValueRef r_llvm_entry_flag(RLlvmEmitter *emitter, bool initial) {
    LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(emitter->function);
    LLVMBuilderRef builder;
    LLVMValueRef first = LLVMGetFirstInstruction(entry);
    LLVMValueRef slot;

    if (emitter->frame != NULL) {
        /* The frame initializer sets the flag; the step only reads and writes it. */
        const uint32_t offset = r_llvm_frame_reserve(emitter, 1U, 1U);
        if (initial) {
            if (emitter->frame_one_count == emitter->frame_one_capacity) {
                const size_t capacity =
                    emitter->frame_one_capacity == 0U ? 8U : emitter->frame_one_capacity * 2U;
                uint32_t *grown = r_llvm_allocate(emitter, capacity * sizeof(*grown));
                if (grown == NULL) {
                    return NULL;
                }
                if (emitter->frame_one_count != 0U) {
                    (void)memcpy(
                        grown, emitter->frame_ones, emitter->frame_one_count * sizeof(*grown));
                }
                r_llvm_free(emitter, emitter->frame_ones);
                emitter->frame_ones = grown;
                emitter->frame_one_capacity = capacity;
            }
            emitter->frame_ones[emitter->frame_one_count++] = offset;
        }
        return r_llvm_frame_address(emitter, offset);
    }
    builder = LLVMCreateBuilderInContext(emitter->context);

    if (first != NULL) {
        LLVMPositionBuilderBefore(builder, first);
    } else {
        LLVMPositionBuilderAtEnd(builder, entry);
    }
    slot = LLVMBuildAlloca(builder, r_llvm_int(emitter, 8U), "initialized");
    (void)LLVMBuildStore(
        builder, LLVMConstInt(r_llvm_int(emitter, 8U), initial ? 1U : 0U, 0), slot);
    LLVMDisposeBuilder(builder);
    return slot;
}

LLVMValueRef r_llvm_place_flag(RLlvmEmitter *emitter, uint32_t ordinal, bool parameter) {
    LLVMValueRef *flags = parameter ? emitter->parameter_flags : emitter->local_flags;
    const RTypeId *types = parameter ? emitter->parameter_types : emitter->local_types;

    if (((size_t)ordinal >= (parameter ? emitter->parameter_count : emitter->local_count)) ||
        !r_llvm_type_requires_drop(emitter, types[ordinal])) {
        return NULL;
    }
    if (!parameter && (emitter->locals[ordinal] != NULL) &&
        ((LLVMIsAGlobalVariable(emitter->locals[ordinal]) != NULL) ||
         (LLVMIsAFunction(emitter->locals[ordinal]) != NULL))) {
        /* A static object is always initialized and has no flag; it is dropped at exit. */
        return NULL;
    }
    if (flags[ordinal] == NULL) {
        /* An owned parameter is initialized on entry; a local when it is first stored. */
        flags[ordinal] = r_llvm_entry_flag(emitter, parameter);
    }
    return flags[ordinal];
}

LLVMValueRef r_llvm_value_flag(RLlvmEmitter *emitter, RMirValueId id) {
    const RMirInstruction *definition = r_llvm_definition(emitter, id);

    if ((definition == NULL) || !r_llvm_type_requires_drop(emitter, definition->type)) {
        return NULL;
    }
    if (emitter->value_flags[id] == NULL) {
        emitter->value_flags[id] = r_llvm_entry_flag(emitter, false);
    }
    return emitter->value_flags[id];
}

void r_llvm_set_flag(RLlvmEmitter *emitter, LLVMValueRef flag, bool value) {
    if (flag != NULL) {
        (void)LLVMBuildStore(
            emitter->builder, LLVMConstInt(r_llvm_int(emitter, 8U), value ? 1U : 0U, 0), flag);
    }
}

/* Runs `then` when the flag is set; the builder continues after. */
static LLVMBasicBlockRef r_llvm_if_flag(RLlvmEmitter *emitter, LLVMValueRef flag) {
    LLVMBasicBlockRef then = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef next = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");

    (void)LLVMBuildCondBr(
        emitter->builder,
        LLVMBuildICmp(emitter->builder,
                      LLVMIntNE,
                      LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 8U), flag, ""),
                      LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                      ""),
        then,
        next);
    LLVMPositionBuilderAtEnd(emitter->builder, then);
    return next;
}

bool r_llvm_drop_flagged(RLlvmEmitter *emitter,
                         RTypeId type,
                         LLVMValueRef pointer,
                         LLVMValueRef flag) {
    LLVMBasicBlockRef next;

    if ((flag == NULL) || !r_llvm_type_requires_drop(emitter, type)) {
        return true;
    }
    next = r_llvm_if_flag(emitter, flag);
    if (!r_llvm_call_drop(emitter, type, pointer)) {
        return false;
    }
    r_llvm_set_flag(emitter, flag, false);
    (void)LLVMBuildBr(emitter->builder, next);
    LLVMPositionBuilderAtEnd(emitter->builder, next);
    return true;
}

/* The C17 emitter's r_c17_emit_async_transfer: a value that requires drop moves only when its
   source is initialized, and the flags follow it; any other value is copied. */
bool r_llvm_transfer(RLlvmEmitter *emitter,
                     RTypeId type,
                     LLVMValueRef destination,
                     LLVMValueRef destination_flag,
                     LLVMValueRef source,
                     LLVMValueRef source_flag) {
    LLVMBasicBlockRef next;

    if ((destination == NULL) || (source == NULL)) {
        return false;
    }
    if (!r_llvm_type_requires_drop(emitter, type)) {
        return r_llvm_store_value(emitter,
                                  type,
                                  r_llvm_scalar_type(emitter, type) == NULL
                                      ? source
                                      : r_llvm_load_scalar(emitter, type, source),
                                  destination);
    }
    if (source_flag == NULL) {
        /* A static object moves out unconditionally, as in the C17 emitter. */
        if (!r_llvm_call_move(emitter, type, destination, source)) {
            return false;
        }
        r_llvm_set_flag(emitter, destination_flag, true);
        return true;
    }
    next = r_llvm_if_flag(emitter, source_flag);
    if (!r_llvm_call_move(emitter, type, destination, source)) {
        return false;
    }
    r_llvm_set_flag(emitter, destination_flag, true);
    r_llvm_set_flag(emitter, source_flag, false);
    (void)LLVMBuildBr(emitter->builder, next);
    LLVMPositionBuilderAtEnd(emitter->builder, next);
    return true;
}

/* ---- Panics ---- */

bool r_llvm_panic(RLlvmEmitter *emitter,
                  const char *category,
                  RSourceSpan span,
                  RMirBlockId panic_target) {
    const bool unwinds = (panic_target != R_MIR_BLOCK_ID_INVALID) &&
                         (strcmp(category, "R_RUNTIME_PANIC_CONTRACT_VIOLATION") != 0);
    int64_t value = 0;
    LLVMValueRef arguments[2];

    if (!r_llvm_runtime_constant(emitter, category, &value)) {
        return false;
    }
    arguments[0] = r_llvm_u32(emitter, (uint64_t)value);
    arguments[1] = r_llvm_span(emitter, span);
    if ((arguments[1] == NULL) ||
        (r_llvm_call_runtime(
             emitter, unwinds ? "r_runtime_raise" : "r_runtime_panic", arguments, 2U, NULL) ==
         NULL)) {
        return false;
    }
    if (!unwinds) {
        (void)LLVMBuildUnreachable(emitter->builder);
        return true;
    }
    if ((size_t)panic_target > emitter->mir->block_count) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    /* The panic block runs its finalies and drops outside the unwinding state, as the C17
       emitter's panic exit does; its `panic` terminator makes the panic pending again. A step
       parks the panic in its task instead (r_c17_emit_panic_exit). */
    if ((emitter->frame != NULL)
            ? (r_llvm_call_runtime(
                   emitter, "r_runtime_task_panic_park", &emitter->execution, 1U, NULL) == NULL)
            : (r_llvm_call_runtime(emitter, "r_runtime_unwind_cleanup_enter", NULL, 0U, NULL) ==
               NULL)) {
        return false;
    }
    (void)LLVMBuildBr(emitter->builder, emitter->blocks[panic_target - 1U]);
    return true;
}

bool r_llvm_check(RLlvmEmitter *emitter,
                  LLVMValueRef failed,
                  const char *category,
                  RSourceSpan span,
                  RMirBlockId panic_target) {
    LLVMBasicBlockRef raise =
        LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef next = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");

    if (failed == NULL) {
        return false;
    }
    (void)LLVMBuildCondBr(emitter->builder, failed, raise, next);
    LLVMPositionBuilderAtEnd(emitter->builder, raise);
    if (!r_llvm_panic(emitter, category, span, panic_target)) {
        return false;
    }
    LLVMPositionBuilderAtEnd(emitter->builder, next);
    return true;
}

/* ---- Places ---- */

static LLVMValueRef r_llvm_place_root(RLlvmEmitter *emitter, uint32_t ordinal, bool parameter) {
    LLVMValueRef *slots = parameter ? emitter->parameters : emitter->locals;
    const size_t count = parameter ? emitter->parameter_count : emitter->local_count;

    if (((size_t)ordinal >= count) || (slots[ordinal] == NULL)) {
        (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        return NULL;
    }
    if (LLVMIsAFunction(slots[ordinal]) != NULL) {
        /* An owning thread-local object is reached through its accessor at each use. */
        return LLVMBuildCall2(
            emitter->builder, LLVMGlobalGetValueType(slots[ordinal]), slots[ordinal], NULL, 0U, "");
    }
    return slots[ordinal];
}

static bool r_llvm_is_projection(const RMirInstruction *instruction) {
    return (instruction != NULL) && ((instruction->kind == R_MIR_INSTRUCTION_FIELD) ||
                                     (instruction->kind == R_MIR_INSTRUCTION_INDEX) ||
                                     (instruction->kind == R_MIR_INSTRUCTION_DEREF));
}

/* ---- Destinations (B6-3) ---- */

/* MIR names a value built in memory and then moved by a separate instruction: an aggregate into
   the local it initializes, an array into the member of the aggregate that takes it. A slot for
   each would hold the value twice or three times on the stack (R-FUNC-0004 bounds every entry),
   so a value whose single use is such a move is built where the move puts it. */
enum {
    R_LLVM_FORWARDED_PLACE = 1U,
    R_LLVM_FORWARDED_MEMBER = 2U,
    /* Never built: zero bytes that a `new own` allocation is initialized with (B6-4). */
    R_LLVM_FORWARDED_ELIDED = 3U
};

/* The block of the current function that holds `instruction`, and its index there. */
static const RMirBlock *
r_llvm_block_of(const RLlvmEmitter *emitter, const RMirInstruction *instruction, uint32_t *index) {
    const RFrontendContext *context = emitter->frontend;
    uint32_t block_index;

    for (block_index = 0U; block_index < emitter->mir->block_count; ++block_index) {
        const RMirBlock *block =
            &context->mir_blocks[(size_t)emitter->mir->first_block + block_index];
        const RMirInstruction *first = &context->mir_instructions[block->first_instruction];
        if ((instruction >= first) && (instruction < first + block->instruction_count)) {
            *index = (uint32_t)(instruction - first);
            return block;
        }
    }
    return NULL;
}

/* How often an instruction names `value`, among its fixed and listed operands. */
static uint32_t r_llvm_names_value(const RLlvmEmitter *emitter,
                                   const RMirInstruction *candidate,
                                   RMirValueId value) {
    uint32_t uses = 0U;
    uint32_t operand;

    uses += (candidate->operand0 == value) ? 1U : 0U;
    uses += (candidate->operand1 == value) ? 1U : 0U;
    uses += (candidate->operand2 == value) ? 1U : 0U;
    for (operand = 0U; operand < candidate->operand_count; ++operand) {
        uses +=
            (emitter->frontend->mir_operands[(size_t)candidate->first_operand + operand] == value)
                ? 1U
                : 0U;
    }
    return uses;
}

/* How often the current function names `value`. */
static uint32_t r_llvm_value_uses(const RLlvmEmitter *emitter, RMirValueId value) {
    const RFrontendContext *context = emitter->frontend;
    uint32_t uses = 0U;
    uint32_t block_index;

    for (block_index = 0U; block_index < emitter->mir->block_count; ++block_index) {
        const RMirBlock *block =
            &context->mir_blocks[(size_t)emitter->mir->first_block + block_index];
        uint32_t index;
        for (index = 0U; index < block->instruction_count; ++index) {
            uses += r_llvm_names_value(
                emitter,
                &context->mir_instructions[(size_t)block->first_instruction + index],
                value);
        }
    }
    return uses;
}

/* Whether an instruction of [first, last) of `block` names the place, through any kind of access:
   such a place may not be written before the store that initializes it. */
static bool r_llvm_place_named(const RLlvmEmitter *emitter,
                               const RMirBlock *block,
                               uint32_t first,
                               uint32_t last,
                               uint32_t ordinal,
                               bool parameter) {
    uint32_t index;

    for (index = first; index < last; ++index) {
        const RMirInstruction *instruction =
            &emitter->frontend->mir_instructions[(size_t)block->first_instruction + index];
        switch (instruction->kind) {
        case R_MIR_INSTRUCTION_LOCAL:
        case R_MIR_INSTRUCTION_FIELD:
        case R_MIR_INSTRUCTION_INDEX:
        case R_MIR_INSTRUCTION_DEREF:
        case R_MIR_INSTRUCTION_BORROW:
        case R_MIR_INSTRUCTION_RAW_ADDRESS:
        case R_MIR_INSTRUCTION_SLICE:
        case R_MIR_INSTRUCTION_LENGTH:
        case R_MIR_INSTRUCTION_LOAD:
        case R_MIR_INSTRUCTION_MOVE:
        case R_MIR_INSTRUCTION_DROP:
        case R_MIR_INSTRUCTION_STORE:
        case R_MIR_INSTRUCTION_STANDARD_CALL:
            if ((instruction->place_ordinal == ordinal) &&
                (instruction->place_is_parameter == parameter)) {
                return true;
            }
            break;
        default:
            break;
        }
    }
    return false;
}

/* The memory value `id` is built in when its single use moves it whole: the place an
   initializing store of the same block fills, or the member of an aggregate or array of the
   same block that takes it; NULL for a slot of its own. `from` is the first instruction that
   writes the memory, from which on nothing else may name the place. */
static LLVMValueRef
r_llvm_destination_memory(RLlvmEmitter *emitter, RMirValueId id, const RMirInstruction *from) {
    const RMirInstruction *definition = emitter->definitions[id];
    const RMirBlock *block;
    const RMirInstruction *user = NULL;
    uint32_t definition_index = 0U;
    uint32_t from_index = 0U;
    uint32_t user_index;

    if ((definition == NULL) || (from == NULL) || (emitter->mir == NULL) ||
        ((definition->kind != R_MIR_INSTRUCTION_AGGREGATE) &&
         (definition->kind != R_MIR_INSTRUCTION_ARRAY) &&
         (definition->kind != R_MIR_INSTRUCTION_CALL)) ||
        (r_llvm_scalar_type(emitter, definition->type) != NULL) ||
        ((block = r_llvm_block_of(emitter, definition, &definition_index)) == NULL) ||
        (r_llvm_block_of(emitter, from, &from_index) != block) ||
        (r_llvm_value_uses(emitter, id) != 1U)) {
        return NULL;
    }
    for (user_index = definition_index + 1U; user_index < block->instruction_count; ++user_index) {
        const RMirInstruction *candidate =
            &emitter->frontend->mir_instructions[(size_t)block->first_instruction + user_index];
        if (r_llvm_names_value(emitter, candidate, id) != 0U) {
            user = candidate;
            break;
        }
    }
    if (user == NULL) {
        return NULL;
    }
    if ((user->kind == R_MIR_INSTRUCTION_STORE) && (user->operand0 == id) &&
        (user->operand1 == R_MIR_VALUE_ID_INVALID) && !user->replaces_initialized) {
        const LLVMValueRef *slots =
            user->place_is_parameter ? emitter->parameters : emitter->locals;
        const size_t count =
            user->place_is_parameter ? emitter->parameter_count : emitter->local_count;
        const RTypeId place_type =
            ((size_t)user->place_ordinal >= count)
                ? R_TYPE_ID_INVALID
                : (user->place_is_parameter ? emitter->parameter_types[user->place_ordinal]
                                            : emitter->local_types[user->place_ordinal]);
        if ((place_type == R_TYPE_ID_INVALID) || (slots[user->place_ordinal] == NULL) ||
            (LLVMIsAFunction(slots[user->place_ordinal]) != NULL) ||
            (r_llvm_value_type(emitter, place_type) !=
             r_llvm_value_type(emitter, definition->type)) ||
            r_llvm_place_named(emitter,
                               block,
                               from_index,
                               user_index,
                               user->place_ordinal,
                               user->place_is_parameter)) {
            return NULL;
        }
        emitter->forwarded[id] = R_LLVM_FORWARDED_PLACE;
        return slots[user->place_ordinal];
    }
    if (((user->kind == R_MIR_INSTRUCTION_AGGREGATE) || (user->kind == R_MIR_INSTRUCTION_ARRAY)) &&
        (user->operand0 != id) && (user->operand1 != id) && (user->operand2 != id)) {
        const bool is_array = user->kind == R_MIR_INSTRUCTION_ARRAY;
        const RSemanticType *type = r_llvm_type(emitter, r_llvm_value_type(emitter, user->type));
        uint32_t index;
        for (index = 0U; index < user->operand_count; ++index) {
            const size_t position = (size_t)user->first_operand + index;
            if (emitter->frontend->mir_operands[position] == id) {
                break;
            }
        }
        if ((type != NULL) && (index < user->operand_count)) {
            const size_t position = (size_t)user->first_operand + index;
            const uint32_t member = emitter->frontend->mir_operand_members[position];
            const RSemanticField *field = is_array ? NULL : r_llvm_field(emitter, member);
            const RTypeId member_type =
                is_array ? type->base : (field == NULL ? R_TYPE_ID_INVALID : field->type);
            uint32_t offset = 0U;
            uint32_t member_size = 0U;
            uint32_t member_align = 0U;
            LLVMValueRef base;
            LLVMValueRef address;
            if ((member_type == R_TYPE_ID_INVALID) ||
                (r_llvm_value_type(emitter, member_type) !=
                 r_llvm_value_type(emitter, definition->type)) ||
                (is_array ? !r_llvm_layout(emitter, member_type, &member_size, &member_align)
                          : !r_llvm_field_offset(emitter, member, &offset))) {
                return NULL;
            }
            if (is_array) {
                offset = member_size * index;
            }
            base = r_llvm_value_memory_from(emitter, user->result, user->type, from);
            address = base == NULL ? NULL : r_llvm_entry_offset(emitter, base, offset);
            if (address != NULL) {
                emitter->forwarded[id] = R_LLVM_FORWARDED_MEMBER;
            }
            return address;
        }
    }
    return NULL;
}

/* The single instruction of the current function that names `id`, or NULL. */
static const RMirInstruction *r_llvm_single_user(const RLlvmEmitter *emitter, RMirValueId id) {
    const RFrontendContext *context = emitter->frontend;
    const RMirInstruction *user = NULL;
    uint32_t block_index;

    for (block_index = 0U; block_index < emitter->mir->block_count; ++block_index) {
        const RMirBlock *block =
            &context->mir_blocks[(size_t)emitter->mir->first_block + block_index];
        uint32_t index;
        for (index = 0U; index < block->instruction_count; ++index) {
            const RMirInstruction *candidate =
                &context->mir_instructions[(size_t)block->first_instruction + index];
            const uint32_t names = r_llvm_names_value(emitter, candidate, id);
            if (names == 0U) {
                continue;
            }
            if ((names != 1U) || (user != NULL)) {
                return NULL;
            }
            user = candidate;
        }
    }
    return user;
}

/* Whether `id` is all zero bytes, built only for its single user: an array literal without
   elements, which fills itself with zeros, a zero integer or bool, or an aggregate of such
   members (its other members are zero too, as every aggregate starts from zero bytes). */
static bool r_llvm_zero_tree(RLlvmEmitter *emitter, RMirValueId id, uint32_t depth) {
    const RMirInstruction *definition;
    unsigned bits = 0U;
    bool is_signed = false;
    uint32_t index;

    if ((depth > emitter->frontend->options.limits.max_nesting) ||
        ((size_t)id >= emitter->value_count) || ((definition = emitter->definitions[id]) == NULL) ||
        (r_llvm_single_user(emitter, id) == NULL)) {
        return false;
    }
    switch (definition->kind) {
    case R_MIR_INSTRUCTION_CONSTANT:
        return (definition->integer_value == 0U) &&
               (r_llvm_kind_integer(
                    r_llvm_value_kind(emitter, definition->type), &bits, &is_signed) ||
                (r_llvm_value_kind(emitter, definition->type) == R_SEMANTIC_TYPE_BOOL));
    case R_MIR_INSTRUCTION_ARRAY:
        return definition->operand_count == 0U;
    case R_MIR_INSTRUCTION_AGGREGATE:
        if (definition->integer_value != 0U) {
            return false;
        }
        for (index = 0U; index < definition->operand_count; ++index) {
            if (!r_llvm_zero_tree(
                    emitter,
                    emitter->frontend->mir_operands[(size_t)definition->first_operand + index],
                    depth + 1U)) {
                return false;
            }
        }
        return true;
    default:
        return false;
    }
}

/* Whether the payload of a `new own` is zero bytes without drop glue: the allocation is filled
   with zeros instead of copying a value built on the stack or in the frame (B6-4). */
static bool r_llvm_zero_new(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RMirInstruction *payload = r_llvm_definition(emitter, instruction->operand0);

    return (instruction->kind == R_MIR_INSTRUCTION_NEW) &&
           (instruction->operation == R_TOKEN_KW_OWN) && (payload != NULL) &&
           ((payload->kind == R_MIR_INSTRUCTION_AGGREGATE) ||
            (payload->kind == R_MIR_INSTRUCTION_ARRAY)) &&
           (r_llvm_scalar_type(emitter, payload->type) == NULL) &&
           !r_llvm_type_requires_drop(emitter, payload->type) &&
           r_llvm_zero_tree(emitter, instruction->operand0, 0U);
}

/* Whether the aggregate or array `id` belongs to the zero payload of a `new own`, through single
   uses as members, and so is never built. */
static bool r_llvm_zero_new_member(RLlvmEmitter *emitter, RMirValueId id) {
    uint32_t depth;

    for (depth = 0U; depth <= emitter->frontend->options.limits.max_nesting; ++depth) {
        const RMirInstruction *user = r_llvm_single_user(emitter, id);
        if (user == NULL) {
            return false;
        }
        if (user->kind == R_MIR_INSTRUCTION_NEW) {
            return (user->operand0 == id) && r_llvm_zero_new(emitter, user);
        }
        if (((user->kind != R_MIR_INSTRUCTION_AGGREGATE) &&
             (user->kind != R_MIR_INSTRUCTION_ARRAY)) ||
            (user->operand0 == id) || (user->operand1 == id) || (user->operand2 == id)) {
            return false;
        }
        id = user->result;
    }
    return false;
}

/* A local whose single initializing store takes the success payload of a checked call declared
   just before it in the same block lives inside that call's carrier, where the callee writes the
   payload (B6-3): the payload is not copied out of a second slot. The local's lifetime begins at
   its declaration before each execution of the call, and the carrier is written only by the call,
   so nothing else uses that memory while the local holds a value. NULL when the pattern does not
   hold. */
LLVMValueRef r_llvm_local_in_carrier(RLlvmEmitter *emitter, const RMirInstruction *local) {
    const RFrontendContext *context = emitter->frontend;
    const RMirInstruction *store = NULL;
    const RMirInstruction *payload;
    const RMirInstruction *call;
    const RMirBlock *block;
    uint32_t local_index = 0U;
    uint32_t call_index = 0U;
    uint32_t success_payloads = 0U;
    uint32_t block_index;
    uint32_t offset = 0U;
    LLVMValueRef carrier;

    if ((emitter->mir == NULL) || (local->kind != R_MIR_INSTRUCTION_LOCAL) ||
        ((block = r_llvm_block_of(emitter, local, &local_index)) == NULL)) {
        return NULL;
    }
    for (block_index = 0U; block_index < emitter->mir->block_count; ++block_index) {
        const RMirBlock *current =
            &context->mir_blocks[(size_t)emitter->mir->first_block + block_index];
        uint32_t index;
        for (index = 0U; index < current->instruction_count; ++index) {
            const RMirInstruction *candidate =
                &context->mir_instructions[(size_t)current->first_instruction + index];
            if ((candidate->kind == R_MIR_INSTRUCTION_STORE) && !candidate->place_is_parameter &&
                (candidate->place_ordinal == local->place_ordinal)) {
                if (store != NULL) {
                    return NULL;
                }
                store = candidate;
            }
        }
    }
    if ((store == NULL) || (store->operand1 != R_MIR_VALUE_ID_INVALID) ||
        store->replaces_initialized || ((size_t)store->operand0 >= emitter->value_count) ||
        ((payload = emitter->definitions[store->operand0]) == NULL) ||
        (payload->kind != R_MIR_INSTRUCTION_EFFECT_PAYLOAD) ||
        (payload->operand0 == R_MIR_VALUE_ID_INVALID) || (payload->integer_value != 0U) ||
        (r_llvm_scalar_type(emitter, payload->type) != NULL) ||
        (r_llvm_value_type(emitter, payload->type) != r_llvm_value_type(emitter, local->type)) ||
        (r_llvm_value_uses(emitter, store->operand0) != 1U) ||
        ((size_t)payload->operand0 >= emitter->value_count) ||
        ((call = emitter->definitions[payload->operand0]) == NULL) ||
        (call->kind != R_MIR_INSTRUCTION_CALL) ||
        (r_llvm_block_of(emitter, call, &call_index) != block) || (call_index <= local_index) ||
        r_llvm_place_named(
            emitter, block, local_index + 1U, call_index, local->place_ordinal, false)) {
        return NULL;
    }
    /* The carrier is read only by its tag and its payloads, and the success payload is the
       local's value alone. */
    for (block_index = 0U; block_index < emitter->mir->block_count; ++block_index) {
        const RMirBlock *current =
            &context->mir_blocks[(size_t)emitter->mir->first_block + block_index];
        uint32_t index;
        for (index = 0U; index < current->instruction_count; ++index) {
            const RMirInstruction *candidate =
                &context->mir_instructions[(size_t)current->first_instruction + index];
            if (r_llvm_names_value(emitter, candidate, call->result) == 0U) {
                continue;
            }
            if ((candidate->kind != R_MIR_INSTRUCTION_EFFECT_TAG) &&
                (candidate->kind != R_MIR_INSTRUCTION_EFFECT_PAYLOAD)) {
                return NULL;
            }
            success_payloads += (candidate->kind == R_MIR_INSTRUCTION_EFFECT_PAYLOAD) &&
                                        (candidate->integer_value == 0U)
                                    ? 1U
                                    : 0U;
        }
    }
    if ((success_payloads != 1U) || !r_llvm_payload_offset(emitter, call->type, &offset)) {
        return NULL;
    }
    carrier = r_llvm_value_memory(emitter, call->result, call->type);
    if (carrier == NULL) {
        return NULL;
    }
    carrier = r_llvm_entry_offset(emitter, carrier, offset);
    if (carrier != NULL) {
        emitter->forwarded[store->operand0] = R_LLVM_FORWARDED_PLACE;
    }
    return carrier;
}

/* The type of the place a projection's base names: the root place or the parent projection. */
static RTypeId r_llvm_projection_base_type(RLlvmEmitter *emitter,
                                           uint32_t ordinal,
                                           bool parameter,
                                           RMirValueId base) {
    const RMirInstruction *definition;

    if (base != R_MIR_VALUE_ID_INVALID) {
        definition = r_llvm_definition(emitter, base);
        return definition == NULL ? R_TYPE_ID_INVALID : definition->type;
    }
    if ((size_t)ordinal >= (parameter ? emitter->parameter_count : emitter->local_count)) {
        return R_TYPE_ID_INVALID;
    }
    return parameter ? emitter->parameter_types[ordinal] : emitter->local_types[ordinal];
}

static LLVMValueRef r_llvm_element_address(RLlvmEmitter *emitter,
                                           LLVMValueRef base,
                                           LLVMValueRef index,
                                           RTypeId element_type) {
    uint32_t size = 0U;
    uint32_t align = 0U;

    if ((base == NULL) || (index == NULL) || !r_llvm_layout(emitter, element_type, &size, &align)) {
        return NULL;
    }
    /* The INDEX instruction of this projection checked the index (r_llvm_emit_index). */
    index = LLVMBuildZExtOrBitCast(emitter->builder, index, r_llvm_int(emitter, 64U), "");
    return r_llvm_element_offset(emitter, base, index, size);
}

LLVMValueRef r_llvm_place_address(RLlvmEmitter *emitter,
                                  uint32_t ordinal,
                                  bool parameter,
                                  RMirValueId projection) {
    const RMirInstruction *definition;

    if (projection == R_MIR_VALUE_ID_INVALID) {
        return r_llvm_place_root(emitter, ordinal, parameter);
    }
    definition = r_llvm_definition(emitter, projection);
    if (!r_llvm_is_projection(definition) || (definition->place_ordinal != ordinal) ||
        (definition->place_is_parameter != parameter)) {
        (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        return NULL;
    }
    switch (definition->kind) {
    case R_MIR_INSTRUCTION_FIELD: {
        uint32_t offset = 0U;
        LLVMValueRef base = r_llvm_place_address(emitter, ordinal, parameter, definition->operand0);
        const RSemanticField *field = r_llvm_field(emitter, definition->aggregate_member);
        const RSemanticAggregate *aggregate =
            (field == NULL) || (field->aggregate == 0U) ||
                    ((size_t)field->aggregate > emitter->frontend->semantic_aggregate_count)
                ? NULL
                : &emitter->frontend->semantic_aggregates[(size_t)field->aggregate - 1U];
        if (base == NULL) {
            return NULL;
        }
        /* A C-declared aggregate has the natural layout of its members, which its ABI record
           proves one for one (R-FFI-0017). */
        if ((aggregate == NULL) || aggregate->is_opaque) {
            (void)r_llvm_unsupported(emitter, "a field of this aggregate");
            return NULL;
        }
        if (!r_llvm_field_offset(emitter, definition->aggregate_member, &offset)) {
            return NULL;
        }
        return r_llvm_byte_offset(emitter, base, offset);
    }
    case R_MIR_INSTRUCTION_INDEX: {
        const RTypeId base_type =
            r_llvm_projection_base_type(emitter, ordinal, parameter, definition->operand0);
        const RSemanticType *array = r_llvm_type(emitter, r_llvm_value_type(emitter, base_type));
        LLVMValueRef base = r_llvm_place_address(emitter, ordinal, parameter, definition->operand0);
        LLVMValueRef index = r_llvm_value(emitter, definition->operand1);
        if ((base == NULL) || (index == NULL) || (array == NULL)) {
            return NULL;
        }
        switch (array->kind) {
        case R_SEMANTIC_TYPE_FIXED_ARRAY:
            return r_llvm_element_address(emitter, base, index, array->base);
        case R_SEMANTIC_TYPE_SLICE:
            return r_llvm_element_address(
                emitter,
                LLVMBuildLoad2(emitter->builder, r_llvm_pointer(emitter), base, ""),
                index,
                array->base);
        case R_SEMANTIC_TYPE_ARRAY: {
            uint32_t data = 0U;
            if (!r_llvm_runtime_field(emitter, "RRuntimeArray", "data", &data, NULL)) {
                return NULL;
            }
            return r_llvm_element_address(emitter,
                                          LLVMBuildLoad2(emitter->builder,
                                                         r_llvm_pointer(emitter),
                                                         r_llvm_byte_offset(emitter, base, data),
                                                         ""),
                                          index,
                                          array->base);
        }
        default:
            (void)r_llvm_unsupported(emitter, "an index of this place");
            return NULL;
        }
    }
    case R_MIR_INSTRUCTION_DEREF: {
        const RSemanticType *pointer =
            r_llvm_type(emitter, r_llvm_value_type(emitter, definition->auxiliary_type));
        LLVMValueRef holder;
        if (definition->operand1 != R_MIR_VALUE_ID_INVALID) {
            return r_llvm_value(emitter, definition->operand1);
        }
        if ((pointer == NULL) ||
            (r_semantic_dyn_referent(emitter->frontend, definition->auxiliary_type) !=
             R_TYPE_ID_INVALID) ||
            (r_semantic_dyn_owned(emitter->frontend, definition->auxiliary_type) !=
             R_TYPE_ID_INVALID)) {
            (void)r_llvm_unsupported(emitter, "a dereference of an interface");
            return NULL;
        }
        holder = r_llvm_place_address(emitter, ordinal, parameter, definition->operand0);
        if (holder == NULL) {
            return NULL;
        }
        switch (pointer->kind) {
        case R_SEMANTIC_TYPE_BORROW:
        case R_SEMANTIC_TYPE_RAW:
            return LLVMBuildLoad2(emitter->builder, r_llvm_pointer(emitter), holder, "");
        case R_SEMANTIC_TYPE_OWN: {
            /* P4.4: the owned value is at the allocation of the owner. */
            uint32_t allocation = 0U;
            if (!r_llvm_runtime_field(emitter, "RRuntimeOwn", "allocation", &allocation, NULL)) {
                return NULL;
            }
            return LLVMBuildLoad2(emitter->builder,
                                  r_llvm_pointer(emitter),
                                  r_llvm_byte_offset(emitter, holder, allocation),
                                  "");
        }
        case R_SEMANTIC_TYPE_ARC:
            return r_llvm_call_runtime(emitter, "r_runtime_arc_get", &holder, 1U, NULL);
        case R_SEMANTIC_TYPE_RC:
            return r_llvm_call_runtime(emitter, "r_runtime_rc_get", &holder, 1U, NULL);
        default:
            (void)r_llvm_unsupported(emitter, "a dereference of this owner");
            return NULL;
        }
    }
    default:
        (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        return NULL;
    }
}

/* ---- Arithmetic ---- */

/* L39: the C17 emitter's arithmetic helpers raise only in the hosted profiles; in the allocation
   profile integer overflow, division by zero and invalid shifts end the process. */
static RMirBlockId r_llvm_helper_target(const RLlvmEmitter *emitter, RMirBlockId panic_target) {
    return (emitter->frontend->profile == R_FRONTEND_PROFILE_ALLOCATION) ||
                   (emitter->frontend->profile == R_FRONTEND_PROFILE_FREESTANDING)
               ? R_MIR_BLOCK_ID_INVALID
               : panic_target;
}

/* The integer semantics of a type: its own kind, the kind of the integer a C ABI type or an enum
   without payloads is represented by. */
static bool
r_llvm_integer_semantics(RLlvmEmitter *emitter, RTypeId id, unsigned *bits, bool *is_signed) {
    return r_llvm_kind_integer(r_llvm_value_kind(emitter, id), bits, is_signed) ||
           r_llvm_enum_integer(emitter, id, bits, is_signed);
}

static LLVMValueRef r_llvm_overflow_intrinsic(RLlvmEmitter *emitter,
                                              const char *name,
                                              LLVMValueRef left,
                                              LLVMValueRef right) {
    LLVMTypeRef type = LLVMTypeOf(left);
    const unsigned id = LLVMLookupIntrinsicID(name, strlen(name));
    LLVMValueRef function = LLVMGetIntrinsicDeclaration(emitter->module, id, &type, 1U);
    LLVMValueRef arguments[2];

    arguments[0] = left;
    arguments[1] = right;
    return LLVMBuildCall2(emitter->builder,
                          LLVMIntrinsicGetType(emitter->context, id, &type, 1U),
                          function,
                          arguments,
                          2U,
                          "");
}

/* r_iN_shl, r_iN_shr, r_uN_shl, r_uN_shr of the C17 emitter: a count of the width or more is an
   invalid shift; a signed left shift whose result does not fit overflows; a signed right shift
   rounds toward negative infinity. The count is converted to size_t first. */
static LLVMValueRef r_llvm_shift(RLlvmEmitter *emitter,
                                 const RMirInstruction *instruction,
                                 LLVMValueRef value,
                                 LLVMValueRef count,
                                 bool left,
                                 bool is_signed,
                                 unsigned bits) {
    const RMirBlockId target = r_llvm_helper_target(emitter, instruction->panic_target);
    LLVMTypeRef type = LLVMTypeOf(value);
    LLVMValueRef wide = LLVMGetIntTypeWidth(LLVMTypeOf(count)) < 64U
                            ? LLVMBuildZExt(emitter->builder, count, r_llvm_int(emitter, 64U), "")
                            : LLVMBuildTrunc(emitter->builder, count, r_llvm_int(emitter, 64U), "");
    LLVMValueRef narrow;
    LLVMValueRef shifted;

    if (LLVMGetIntTypeWidth(LLVMTypeOf(count)) == 64U) {
        wide = count;
    }
    if (!r_llvm_check(
            emitter,
            LLVMBuildICmp(emitter->builder, LLVMIntUGE, wide, r_llvm_u64(emitter, bits), ""),
            "R_RUNTIME_PANIC_INVALID_SHIFT",
            instruction->span,
            target)) {
        return NULL;
    }
    narrow = LLVMBuildTrunc(emitter->builder, wide, type, "");
    if (bits == 64U) {
        narrow = wide;
    }
    if (!left) {
        return is_signed ? LLVMBuildAShr(emitter->builder, value, narrow, "")
                         : LLVMBuildLShr(emitter->builder, value, narrow, "");
    }
    shifted = LLVMBuildShl(emitter->builder, value, narrow, "");
    if (is_signed) {
        /* The shift overflows when shifting back does not give the value. */
        LLVMValueRef back = LLVMBuildAShr(emitter->builder, shifted, narrow, "");
        if (!r_llvm_check(emitter,
                          LLVMBuildICmp(emitter->builder, LLVMIntNE, back, value, ""),
                          "R_RUNTIME_PANIC_INTEGER_OVERFLOW",
                          instruction->span,
                          target)) {
            return NULL;
        }
    }
    return shifted;
}

static bool r_llvm_emit_binary(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    LLVMValueRef left = r_llvm_value(emitter, instruction->operand0);
    LLVMValueRef right = r_llvm_value(emitter, instruction->operand1);
    const RMirInstruction *left_definition = r_llvm_definition(emitter, instruction->operand0);
    const RMirBlockId target = r_llvm_helper_target(emitter, instruction->panic_target);
    unsigned bits = 0U;
    bool is_signed = false;
    bool integer;
    bool floating;
    LLVMValueRef result = NULL;

    if ((left == NULL) || (right == NULL) || (left_definition == NULL)) {
        return false;
    }
    integer = LLVMGetTypeKind(LLVMTypeOf(left)) == LLVMIntegerTypeKind;
    floating = (LLVMGetTypeKind(LLVMTypeOf(left)) == LLVMFloatTypeKind) ||
               (LLVMGetTypeKind(LLVMTypeOf(left)) == LLVMDoubleTypeKind);
    if (integer && !r_llvm_integer_semantics(emitter, left_definition->type, &bits, &is_signed)) {
        bits = LLVMGetIntTypeWidth(LLVMTypeOf(left));
        is_signed = false;
    }
    if (!integer && !floating && (LLVMGetTypeKind(LLVMTypeOf(left)) != LLVMPointerTypeKind)) {
        return r_llvm_unsupported(emitter, "a binary operator on values in memory");
    }
    switch (instruction->operation) {
    case R_TOKEN_EQUAL_EQUAL:
    case R_TOKEN_BANG_EQUAL:
    case R_TOKEN_LESS:
    case R_TOKEN_LESS_EQUAL:
    case R_TOKEN_GREATER:
    case R_TOKEN_GREATER_EQUAL: {
        const unsigned which = instruction->operation == R_TOKEN_EQUAL_EQUAL  ? 0U
                               : instruction->operation == R_TOKEN_BANG_EQUAL ? 1U
                               : instruction->operation == R_TOKEN_LESS       ? 2U
                               : instruction->operation == R_TOKEN_LESS_EQUAL ? 3U
                               : instruction->operation == R_TOKEN_GREATER    ? 4U
                                                                              : 5U;
        if (floating) {
            static const LLVMRealPredicate predicates[] = {
                LLVMRealOEQ, LLVMRealUNE, LLVMRealOLT, LLVMRealOLE, LLVMRealOGT, LLVMRealOGE};
            result = LLVMBuildFCmp(emitter->builder, predicates[which], left, right, "");
        } else {
            static const LLVMIntPredicate signed_predicates[] = {
                LLVMIntEQ, LLVMIntNE, LLVMIntSLT, LLVMIntSLE, LLVMIntSGT, LLVMIntSGE};
            static const LLVMIntPredicate unsigned_predicates[] = {
                LLVMIntEQ, LLVMIntNE, LLVMIntULT, LLVMIntULE, LLVMIntUGT, LLVMIntUGE};
            result =
                LLVMBuildICmp(emitter->builder,
                              is_signed ? signed_predicates[which] : unsigned_predicates[which],
                              left,
                              right,
                              "");
        }
        break;
    }
    case R_TOKEN_AMP:
    case R_TOKEN_AMP_EQUAL:
        result = LLVMBuildAnd(emitter->builder, left, right, "");
        break;
    case R_TOKEN_PIPE:
    case R_TOKEN_PIPE_EQUAL:
        result = LLVMBuildOr(emitter->builder, left, right, "");
        break;
    case R_TOKEN_CARET:
    case R_TOKEN_CARET_EQUAL:
        result = LLVMBuildXor(emitter->builder, left, right, "");
        break;
    case R_TOKEN_LESS_LESS:
    case R_TOKEN_LESS_LESS_EQUAL:
    case R_TOKEN_GREATER_GREATER:
    case R_TOKEN_GREATER_GREATER_EQUAL:
        if (!integer || (LLVMGetTypeKind(LLVMTypeOf(right)) != LLVMIntegerTypeKind)) {
            return r_llvm_unsupported(emitter, "a shift of this type");
        }
        result = r_llvm_shift(emitter,
                              instruction,
                              left,
                              right,
                              (instruction->operation == R_TOKEN_LESS_LESS) ||
                                  (instruction->operation == R_TOKEN_LESS_LESS_EQUAL),
                              is_signed,
                              bits);
        break;
    case R_TOKEN_PLUS:
    case R_TOKEN_PLUS_EQUAL:
    case R_TOKEN_MINUS:
    case R_TOKEN_MINUS_EQUAL:
    case R_TOKEN_STAR:
    case R_TOKEN_STAR_EQUAL: {
        const bool add = (instruction->operation == R_TOKEN_PLUS) ||
                         (instruction->operation == R_TOKEN_PLUS_EQUAL);
        const bool subtract = (instruction->operation == R_TOKEN_MINUS) ||
                              (instruction->operation == R_TOKEN_MINUS_EQUAL);
        if (floating) {
            result = add        ? LLVMBuildFAdd(emitter->builder, left, right, "")
                     : subtract ? LLVMBuildFSub(emitter->builder, left, right, "")
                                : LLVMBuildFMul(emitter->builder, left, right, "");
        } else if (!is_signed) {
            /* Unsigned arithmetic is modular (the C17 emitter's unsigned helpers). */
            result = add        ? LLVMBuildAdd(emitter->builder, left, right, "")
                     : subtract ? LLVMBuildSub(emitter->builder, left, right, "")
                                : LLVMBuildMul(emitter->builder, left, right, "");
        } else {
            LLVMValueRef pair = r_llvm_overflow_intrinsic(emitter,
                                                          add        ? "llvm.sadd.with.overflow"
                                                          : subtract ? "llvm.ssub.with.overflow"
                                                                     : "llvm.smul.with.overflow",
                                                          left,
                                                          right);
            result = LLVMBuildExtractValue(emitter->builder, pair, 0U, "");
            if (!r_llvm_check(emitter,
                              LLVMBuildExtractValue(emitter->builder, pair, 1U, ""),
                              "R_RUNTIME_PANIC_INTEGER_OVERFLOW",
                              instruction->span,
                              target)) {
                return false;
            }
        }
        break;
    }
    case R_TOKEN_SLASH:
    case R_TOKEN_SLASH_EQUAL:
    case R_TOKEN_PERCENT:
    case R_TOKEN_PERCENT_EQUAL: {
        const bool divide = (instruction->operation == R_TOKEN_SLASH) ||
                            (instruction->operation == R_TOKEN_SLASH_EQUAL);
        if (floating) {
            result = divide ? LLVMBuildFDiv(emitter->builder, left, right, "")
                            : LLVMBuildFRem(emitter->builder, left, right, "");
            break;
        }
        if (!r_llvm_check(
                emitter,
                LLVMBuildICmp(
                    emitter->builder, LLVMIntEQ, right, LLVMConstNull(LLVMTypeOf(right)), ""),
                "R_RUNTIME_PANIC_DIVISION_BY_ZERO",
                instruction->span,
                target)) {
            return false;
        }
        if (is_signed) {
            LLVMValueRef minimum = LLVMConstInt(LLVMTypeOf(left), UINT64_C(1) << (bits - 1U), 0);
            LLVMValueRef overflow = LLVMBuildAnd(
                emitter->builder,
                LLVMBuildICmp(emitter->builder, LLVMIntEQ, left, minimum, ""),
                LLVMBuildICmp(
                    emitter->builder, LLVMIntEQ, right, LLVMConstAllOnes(LLVMTypeOf(right)), ""),
                "");
            if (!r_llvm_check(emitter,
                              overflow,
                              "R_RUNTIME_PANIC_INTEGER_OVERFLOW",
                              instruction->span,
                              target)) {
                return false;
            }
            result = divide ? LLVMBuildSDiv(emitter->builder, left, right, "")
                            : LLVMBuildSRem(emitter->builder, left, right, "");
        } else {
            result = divide ? LLVMBuildUDiv(emitter->builder, left, right, "")
                            : LLVMBuildURem(emitter->builder, left, right, "");
        }
        break;
    }
    default:
        return r_llvm_unsupported(emitter, "this binary operator");
    }
    return (result != NULL) && r_llvm_set_value(emitter, instruction->result, result);
}

static bool r_llvm_emit_unary(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    LLVMValueRef operand = r_llvm_value(emitter, instruction->operand0);
    unsigned bits = 0U;
    bool is_signed = false;
    LLVMValueRef result;

    if (operand == NULL) {
        return false;
    }
    switch (instruction->operation) {
    case R_TOKEN_MINUS:
        if (LLVMGetTypeKind(LLVMTypeOf(operand)) != LLVMIntegerTypeKind) {
            result = LLVMBuildFNeg(emitter->builder, operand, "");
            break;
        }
        if (!r_llvm_integer_semantics(emitter, instruction->type, &bits, &is_signed)) {
            return r_llvm_unsupported(emitter, "a negation of this type");
        }
        if (is_signed) {
            /* r_iN_neg: the minimum has no negation. */
            if (!r_llvm_check(
                    emitter,
                    LLVMBuildICmp(emitter->builder,
                                  LLVMIntEQ,
                                  operand,
                                  LLVMConstInt(LLVMTypeOf(operand), UINT64_C(1) << (bits - 1U), 0),
                                  ""),
                    "R_RUNTIME_PANIC_INTEGER_OVERFLOW",
                    instruction->span,
                    r_llvm_helper_target(emitter, instruction->panic_target))) {
                return false;
            }
        }
        result = LLVMBuildNeg(emitter->builder, operand, "");
        break;
    case R_TOKEN_BANG:
    case R_TOKEN_TILDE:
        result = LLVMBuildNot(emitter->builder, operand, "");
        break;
    case R_TOKEN_PLUS:
        result = operand;
        break;
    default:
        return r_llvm_unsupported(emitter, "this unary operator");
    }
    return r_llvm_set_value(emitter, instruction->result, result);
}

/* ---- Conversions ---- */

static bool r_llvm_float_kind(RSemanticTypeKind kind) {
    return (kind == R_SEMANTIC_TYPE_F32) || (kind == R_SEMANTIC_TYPE_F64);
}

/* The integer conversion guard of the C17 emitter: an explicit `as` that does not fit is an
   invalid conversion; a checked implicit conversion to a signed type overflows. */
static bool r_llvm_integer_guard(RLlvmEmitter *emitter,
                                 const RMirInstruction *instruction,
                                 LLVMValueRef value,
                                 unsigned source_bits,
                                 bool source_signed,
                                 unsigned target_bits,
                                 bool target_signed) {
    const bool overflow_panic = instruction->operation != R_TOKEN_KW_AS;
    LLVMValueRef failed = NULL;
    LLVMValueRef wide;

    if (instruction->operation == R_TOKEN_INVALID) {
        return true;
    }
    if (overflow_panic && !target_signed) {
        return true;
    }
    wide =
        source_bits < 64U
            ? (source_signed ? LLVMBuildSExt(emitter->builder, value, r_llvm_int(emitter, 64U), "")
                             : LLVMBuildZExt(emitter->builder, value, r_llvm_int(emitter, 64U), ""))
            : value;
    if (target_signed && source_signed && (target_bits < source_bits)) {
        const uint64_t maximum = (UINT64_C(1) << (target_bits - 1U)) - 1U;
        failed = LLVMBuildOr(
            emitter->builder,
            LLVMBuildICmp(emitter->builder,
                          LLVMIntSLT,
                          wide,
                          LLVMConstInt(r_llvm_int(emitter, 64U), ~maximum, 0),
                          ""),
            LLVMBuildICmp(emitter->builder, LLVMIntSGT, wide, r_llvm_u64(emitter, maximum), ""),
            "");
    } else if (target_signed && !source_signed && (target_bits <= source_bits)) {
        failed = LLVMBuildICmp(emitter->builder,
                               LLVMIntUGT,
                               wide,
                               r_llvm_u64(emitter, (UINT64_C(1) << (target_bits - 1U)) - 1U),
                               "");
    } else if (!target_signed && source_signed) {
        failed = LLVMBuildICmp(emitter->builder, LLVMIntSLT, wide, r_llvm_u64(emitter, 0U), "");
        if (source_bits > target_bits) {
            failed = LLVMBuildOr(
                emitter->builder,
                failed,
                LLVMBuildICmp(
                    emitter->builder,
                    LLVMIntUGT,
                    wide,
                    r_llvm_u64(emitter,
                               target_bits == 64U ? UINT64_MAX : (UINT64_C(1) << target_bits) - 1U),
                    ""),
                "");
        }
    } else if (!target_signed && !source_signed && (target_bits < source_bits)) {
        failed = LLVMBuildICmp(emitter->builder,
                               LLVMIntUGT,
                               wide,
                               r_llvm_u64(emitter, (UINT64_C(1) << target_bits) - 1U),
                               "");
    }
    if (failed == NULL) {
        return true;
    }
    return r_llvm_check(emitter,
                        failed,
                        overflow_panic ? "R_RUNTIME_PANIC_INTEGER_OVERFLOW"
                                       : "R_RUNTIME_PANIC_INVALID_CONVERSION",
                        instruction->span,
                        instruction->panic_target);
}

/* The tag offset of an owner of an interface: struct { <owner> r_owner; uint32_t r_tag; }. */
bool r_llvm_dyn_owner_tag_offset(RLlvmEmitter *emitter, RTypeId type, uint32_t *offset) {
    const RSemanticType *owner = r_llvm_type(emitter, r_llvm_value_type(emitter, type));
    static const char *const owners[] = {
        "RRuntimeOwn", "RRuntimeArc", "RRuntimeRc", "RRuntimeWeakArc", "RRuntimeWeakRc"};
    const size_t which = owner == NULL                                          ? 0U
                         : owner->kind == R_SEMANTIC_TYPE_OWN                   ? 0U
                         : owner->kind == R_SEMANTIC_TYPE_ARC                   ? 1U
                         : owner->kind == R_SEMANTIC_TYPE_RC                    ? 2U
                         : (owner->flags & R_SEMANTIC_TYPE_FLAG_RC_OWNER) != 0U ? 4U
                                                                                : 3U;
    uint32_t size = 0U;
    uint32_t align = 0U;

    if ((owner == NULL) || !r_llvm_runtime_layout(emitter, owners[which], &size, &align)) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    *offset = (size + 3U) & ~3U;
    return true;
}

/* The tag of a member of `wide` in the narrower interface `narrow`, through the table
   r_dyn_narrow_<wide>_<narrow> of the C17 emitter (a member not in `narrow` maps to 0). */
static LLVMValueRef
r_llvm_dyn_narrow(RLlvmEmitter *emitter, RTypeId wide, RTypeId narrow, LLVMValueRef tag) {
    const RFrontendContext *context = emitter->frontend;
    char name[64];
    LLVMValueRef table;
    LLVMValueRef index[2];

    (void)snprintf(name,
                   sizeof(name),
                   "r_dyn_narrow.%" PRIu32 ".%" PRIu32,
                   r_llvm_type_key(emitter, wide),
                   r_llvm_type_key(emitter, narrow));
    table = LLVMGetNamedGlobal(emitter->module, name);
    if (table == NULL) {
        uint32_t size = 0U;
        uint32_t entry_tag;
        size_t entry;
        LLVMValueRef *values;
        for (entry = 0U; entry < context->dyn_member_count; ++entry) {
            if ((context->dyn_members[entry].interface == wide) &&
                (context->dyn_members[entry].tag + 1U > size)) {
                size = context->dyn_members[entry].tag + 1U;
            }
        }
        size = size == 0U ? 1U : size;
        values = r_llvm_allocate(emitter, (size_t)size * sizeof(*values));
        if (values == NULL) {
            return NULL;
        }
        for (entry_tag = 0U; entry_tag < size; ++entry_tag) {
            uint32_t narrowed = 0U;
            for (entry = 0U; entry < context->dyn_member_count; ++entry) {
                if ((context->dyn_members[entry].interface == wide) &&
                    (context->dyn_members[entry].tag == entry_tag) &&
                    !r_semantic_dyn_tag(
                        context, narrow, context->dyn_members[entry].member, &narrowed)) {
                    r_llvm_free(emitter, values);
                    (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
                    return NULL;
                }
            }
            values[entry_tag] = r_llvm_u32(emitter, narrowed);
        }
        table =
            LLVMAddGlobal(emitter->module, LLVMArrayType2(r_llvm_int(emitter, 32U), size), name);
        LLVMSetInitializer(table, LLVMConstArray2(r_llvm_int(emitter, 32U), values, size));
        LLVMSetLinkage(table, LLVMPrivateLinkage);
        LLVMSetGlobalConstant(table, 1);
        r_llvm_free(emitter, values);
    }
    index[0] = r_llvm_u64(emitter, 0U);
    index[1] = LLVMBuildZExt(emitter->builder, tag, r_llvm_int(emitter, 64U), "");
    return LLVMBuildLoad2(
        emitter->builder,
        r_llvm_int(emitter, 32U),
        LLVMBuildGEP2(emitter->builder, LLVMGlobalGetValueType(table), table, index, 2U, ""),
        "");
}

/* R-TYPE-0051, R-TYPE-0055 (r_c17_emit_dyn_cast): a borrow becomes an interface borrow
   {address, tag of its member}, an owner an owner of the interface {owner, tag} that it moves
   into, an interface value of the same interface stays as it is, and a borrow of an owner of an
   interface borrows the owned value. Narrowing to another interface is not lowered yet. */
static bool r_llvm_emit_dyn_cast(RLlvmEmitter *emitter,
                                 const RMirInstruction *instruction,
                                 RTypeId from,
                                 LLVMValueRef value) {
    const RFrontendContext *context = emitter->frontend;
    const RTypeId borrowed = r_semantic_dyn_referent(context, instruction->type);
    const RTypeId interface =
        borrowed != R_TYPE_ID_INVALID ? borrowed : r_semantic_dyn_owned(context, instruction->type);
    const RTypeId source_interface = borrowed != R_TYPE_ID_INVALID
                                         ? r_semantic_dyn_referent(context, from)
                                         : r_semantic_dyn_owned(context, from);
    const RSemanticType *source = r_llvm_type(emitter, r_llvm_value_type(emitter, from));
    LLVMValueRef result;
    uint32_t tag = 0U;

    if ((source == NULL) || (interface == R_TYPE_ID_INVALID)) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    result = r_llvm_value_memory(emitter, instruction->result, instruction->type);
    if (result == NULL) {
        return false;
    }
    if ((borrowed != R_TYPE_ID_INVALID) && (source->kind == R_SEMANTIC_TYPE_BORROW) &&
        (r_semantic_dyn_owned(context, source->base) != R_TYPE_ID_INVALID)) {
        /* A borrow of an owner of an interface: the owned value and the owner's tag. */
        const RSemanticType *owner = r_llvm_type(emitter, r_llvm_value_type(emitter, source->base));
        uint32_t tag_offset = 0U;
        LLVMValueRef address;
        const RTypeId owned = r_semantic_dyn_owned(context, source->base);
        LLVMValueRef owner_tag;
        if ((owner == NULL) || !r_llvm_dyn_owner_tag_offset(emitter, source->base, &tag_offset)) {
            return false;
        }
        if (owner->kind == R_SEMANTIC_TYPE_OWN) {
            uint32_t allocation = 0U;
            if (!r_llvm_runtime_field(emitter, "RRuntimeOwn", "allocation", &allocation, NULL)) {
                return false;
            }
            address = LLVMBuildLoad2(emitter->builder,
                                     r_llvm_pointer(emitter),
                                     r_llvm_byte_offset(emitter, value, allocation),
                                     "");
        } else {
            address = r_llvm_call_runtime(emitter,
                                          owner->kind == R_SEMANTIC_TYPE_ARC ? "r_runtime_arc_get"
                                                                             : "r_runtime_rc_get",
                                          &value,
                                          1U,
                                          NULL);
            if (address == NULL) {
                return false;
            }
        }
        owner_tag = LLVMBuildLoad2(emitter->builder,
                                   r_llvm_int(emitter, 32U),
                                   r_llvm_byte_offset(emitter, value, tag_offset),
                                   "");
        if (owned != interface) {
            owner_tag = r_llvm_dyn_narrow(emitter, owned, interface, owner_tag);
            if (owner_tag == NULL) {
                return false;
            }
        }
        (void)LLVMBuildStore(emitter->builder, address, result);
        (void)LLVMBuildStore(emitter->builder, owner_tag, r_llvm_byte_offset(emitter, result, 8U));
        return true;
    }
    if (source_interface != R_TYPE_ID_INVALID) {
        uint32_t size = 0U;
        uint32_t align = 0U;
        if (!r_llvm_layout(emitter, instruction->type, &size, &align)) {
            return false;
        }
        (void)LLVMBuildMemCpy(
            emitter->builder, result, align, value, align, r_llvm_u64(emitter, size));
        if (source_interface != interface) {
            /* The member keeps its address or owner and takes its tag in the narrower
               interface. */
            uint32_t tag_offset = 8U;
            LLVMValueRef narrowed;
            if ((borrowed == R_TYPE_ID_INVALID) &&
                !r_llvm_dyn_owner_tag_offset(emitter, instruction->type, &tag_offset)) {
                return false;
            }
            narrowed =
                r_llvm_dyn_narrow(emitter,
                                  source_interface,
                                  interface,
                                  LLVMBuildLoad2(emitter->builder,
                                                 r_llvm_int(emitter, 32U),
                                                 r_llvm_byte_offset(emitter, value, tag_offset),
                                                 ""));
            if (narrowed == NULL) {
                return false;
            }
            (void)LLVMBuildStore(
                emitter->builder, narrowed, r_llvm_byte_offset(emitter, result, tag_offset));
        }
        if (r_llvm_type_requires_drop(emitter, instruction->type)) {
            r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, instruction->operand0), false);
            r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, instruction->result), true);
        }
        return true;
    }
    if (!r_semantic_dyn_tag(context, interface, source->base, &tag)) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    if (borrowed != R_TYPE_ID_INVALID) {
        (void)LLVMBuildStore(emitter->builder, value, result);
        (void)LLVMBuildStore(
            emitter->builder, r_llvm_u32(emitter, tag), r_llvm_byte_offset(emitter, result, 8U));
        return true;
    }
    {
        /* The owner moves into the owner of the interface. */
        uint32_t tag_offset = 0U;
        uint32_t size = 0U;
        uint32_t align = 0U;
        if (!r_llvm_dyn_owner_tag_offset(emitter, instruction->type, &tag_offset) ||
            !r_llvm_layout(emitter, from, &size, &align)) {
            return false;
        }
        (void)LLVMBuildMemCpy(
            emitter->builder, result, align, value, align, r_llvm_u64(emitter, size));
        (void)LLVMBuildStore(emitter->builder,
                             r_llvm_u32(emitter, tag),
                             r_llvm_byte_offset(emitter, result, tag_offset));
        r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, instruction->operand0), false);
        r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, instruction->result), true);
        return true;
    }
}

static bool r_llvm_emit_cast(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RMirInstruction *operand = r_llvm_definition(emitter, instruction->operand0);
    const RSemanticTypeKind source_kind =
        operand == NULL ? R_SEMANTIC_TYPE_INVALID : r_llvm_value_kind(emitter, operand->type);
    const RSemanticTypeKind target_kind = r_llvm_value_kind(emitter, instruction->type);
    LLVMTypeRef target = r_llvm_scalar_type(emitter, instruction->type);
    unsigned source_bits = 0U;
    unsigned target_bits = 0U;
    bool source_signed = false;
    bool target_signed = false;
    LLVMValueRef value;
    LLVMValueRef result;

    if (instruction->operand0 == R_MIR_VALUE_ID_INVALID) {
        /* R-EXPR-0015: reaching a conversion from never breaks the contract of its operand. */
        return r_llvm_panic(emitter,
                            "R_RUNTIME_PANIC_CONTRACT_VIOLATION",
                            instruction->span,
                            R_MIR_BLOCK_ID_INVALID);
    }
    value = r_llvm_value(emitter, instruction->operand0);
    if ((operand == NULL) || (value == NULL)) {
        return false;
    }
    if ((r_semantic_dyn_referent(emitter->frontend, instruction->type) != R_TYPE_ID_INVALID) ||
        (r_semantic_dyn_owned(emitter->frontend, instruction->type) != R_TYPE_ID_INVALID)) {
        return r_llvm_emit_dyn_cast(emitter, instruction, operand->type, value);
    }
    if ((instruction->operation == R_TOKEN_INVALID) && (source_kind == R_SEMANTIC_TYPE_OWN) &&
        (target_kind == R_SEMANTIC_TYPE_OWN)) {
        /* An owner becomes a nullable owner: it moves with its flag. */
        LLVMValueRef memory = r_llvm_value_memory(emitter, instruction->result, instruction->type);
        return (memory != NULL) &&
               r_llvm_transfer(emitter,
                               instruction->type,
                               memory,
                               r_llvm_value_flag(emitter, instruction->result),
                               value,
                               r_llvm_value_flag(emitter, instruction->operand0));
    }
    if ((instruction->operation == R_TOKEN_INVALID) && (target_kind == R_SEMANTIC_TYPE_ATOMIC) &&
        (r_llvm_scalar_type(emitter, operand->type) != NULL)) {
        /* R-INIT-0012: an atomic object is initialized by a value of its type (atomic_init). */
        LLVMValueRef memory = r_llvm_value_memory(emitter, instruction->result, instruction->type);
        if (memory == NULL) {
            return false;
        }
        r_llvm_store_scalar(emitter, operand->type, value, memory);
        r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, instruction->result), true);
        return true;
    }
    if (target == NULL) {
        /* Conversions between values in memory of the same layout: a string literal to a str view,
           a str view to a byte slice, a slice to a shared slice. */
        uint32_t source_size = 0U;
        uint32_t source_align = 0U;
        uint32_t target_size = 0U;
        uint32_t target_align = 0U;
        const bool view =
            ((source_kind == R_SEMANTIC_TYPE_CONSTEXPR_STR) &&
             (target_kind == R_SEMANTIC_TYPE_STR)) ||
            ((source_kind == R_SEMANTIC_TYPE_STR) && (target_kind == R_SEMANTIC_TYPE_SLICE)) ||
            ((source_kind == R_SEMANTIC_TYPE_SLICE) && (target_kind == R_SEMANTIC_TYPE_SLICE));
        LLVMValueRef memory;
        if ((instruction->operation != R_TOKEN_INVALID) && !view) {
            return r_llvm_unsupported(emitter, "this conversion of a value in memory");
        }
        if (r_llvm_value_type(emitter, operand->type) !=
                r_llvm_value_type(emitter, instruction->type) &&
            !view) {
            return r_llvm_unsupported(emitter, "this implicit conversion of a value in memory");
        }
        if (!r_llvm_layout(emitter, operand->type, &source_size, &source_align) ||
            !r_llvm_layout(emitter, instruction->type, &target_size, &target_align)) {
            return false;
        }
        if (source_size != target_size) {
            return r_llvm_unsupported(emitter, "a conversion between layouts");
        }
        memory = r_llvm_value_memory(emitter, instruction->result, instruction->type);
        return (memory != NULL) && r_llvm_store_value(emitter, operand->type, value, memory);
    }
    if (r_llvm_scalar_type(emitter, operand->type) == NULL) {
        return r_llvm_unsupported(emitter, "a conversion of a value in memory to a scalar");
    }
    if (r_llvm_float_kind(source_kind)) {
        if (r_llvm_float_kind(target_kind)) {
            result = LLVMBuildFPCast(emitter->builder, value, target, "");
        } else if (r_llvm_integer_semantics(
                       emitter, instruction->type, &target_bits, &target_signed)) {
            /* The value must lie in the target's range after truncation toward zero. */
            const double limit = target_signed
                                     ? (double)(UINT64_C(1) << (target_bits - 1U))
                                     : (target_bits == 64U ? 18446744073709551616.0
                                                           : (double)(UINT64_C(1) << target_bits));
            LLVMTypeRef floating = LLVMTypeOf(value);
            LLVMValueRef low =
                target_signed
                    ? LLVMBuildFCmp(
                          emitter->builder, LLVMRealOGE, value, LLVMConstReal(floating, -limit), "")
                    : LLVMBuildFCmp(
                          emitter->builder, LLVMRealOGT, value, LLVMConstReal(floating, -1.0), "");
            LLVMValueRef high = LLVMBuildFCmp(
                emitter->builder, LLVMRealOLT, value, LLVMConstReal(floating, limit), "");
            if (!r_llvm_check(emitter,
                              LLVMBuildNot(emitter->builder,
                                           LLVMBuildAnd(emitter->builder, low, high, ""),
                                           ""),
                              "R_RUNTIME_PANIC_INVALID_CONVERSION",
                              instruction->span,
                              instruction->panic_target)) {
                return false;
            }
            result = target_signed ? LLVMBuildFPToSI(emitter->builder, value, target, "")
                                   : LLVMBuildFPToUI(emitter->builder, value, target, "");
        } else {
            return r_llvm_unsupported(emitter, "this conversion of a float");
        }
    } else if (source_kind == R_SEMANTIC_TYPE_BOOL) {
        if (target_kind == R_SEMANTIC_TYPE_BOOL) {
            result = value;
        } else if (r_llvm_float_kind(target_kind)) {
            result = LLVMBuildUIToFP(emitter->builder, value, target, "");
        } else {
            result = LLVMBuildZExt(emitter->builder, value, target, "");
        }
    } else if (LLVMGetTypeKind(LLVMTypeOf(value)) == LLVMPointerTypeKind) {
        result = LLVMGetTypeKind(target) == LLVMPointerTypeKind
                     ? value
                     : LLVMBuildPtrToInt(emitter->builder, value, target, "");
    } else if (LLVMGetTypeKind(target) == LLVMPointerTypeKind) {
        result = LLVMBuildIntToPtr(emitter->builder, value, target, "");
    } else if ((source_kind == R_SEMANTIC_TYPE_FUNCTION) &&
               (target_kind == R_SEMANTIC_TYPE_FUNCTION)) {
        /* R-TYPE-0054: function values of either type are the same symbol number. */
        result = value;
    } else if (!r_llvm_integer_semantics(emitter, operand->type, &source_bits, &source_signed)) {
        return r_llvm_unsupported(emitter, "a conversion from this type");
    } else if (target_kind == R_SEMANTIC_TYPE_BOOL) {
        result =
            LLVMBuildICmp(emitter->builder, LLVMIntNE, value, LLVMConstNull(LLVMTypeOf(value)), "");
    } else if (r_llvm_float_kind(target_kind)) {
        result = source_signed ? LLVMBuildSIToFP(emitter->builder, value, target, "")
                               : LLVMBuildUIToFP(emitter->builder, value, target, "");
    } else if (r_llvm_integer_semantics(emitter, instruction->type, &target_bits, &target_signed)) {
        const RSemanticAggregate *enumeration =
            r_llvm_aggregate(emitter, r_llvm_value_type(emitter, instruction->type));
        if (!r_llvm_integer_guard(emitter,
                                  instruction,
                                  value,
                                  source_bits,
                                  source_signed,
                                  target_bits,
                                  target_signed)) {
            return false;
        }
        result = target_bits == source_bits  ? value
                 : target_bits < source_bits ? LLVMBuildTrunc(emitter->builder, value, target, "")
                 : source_signed             ? LLVMBuildSExt(emitter->builder, value, target, "")
                                             : LLVMBuildZExt(emitter->builder, value, target, "");
        if ((enumeration != NULL) && (instruction->operation == R_TOKEN_KW_AS) &&
            (r_llvm_value_type(emitter, operand->type) !=
             r_llvm_value_type(emitter, instruction->type))) {
            /* R-EXPR-0018: an integer becomes a fieldless enum only as a declared discriminant
               (r_c17_emit_enum_discriminant_guard). */
            LLVMBasicBlockRef valid =
                LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
            LLVMBasicBlockRef invalid =
                LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
            LLVMValueRef choice =
                LLVMBuildSwitch(emitter->builder, result, invalid, enumeration->variant_count);
            uint32_t variant;
            for (variant = 0U; variant < enumeration->variant_count; ++variant) {
                const RSemanticVariant *current =
                    &emitter->frontend
                         ->semantic_variants[(size_t)enumeration->first_variant + variant];
                uint32_t earlier;
                for (earlier = 0U; earlier < variant; ++earlier) {
                    if (emitter->frontend
                            ->semantic_variants[(size_t)enumeration->first_variant + earlier]
                            .value == current->value) {
                        break;
                    }
                }
                if (earlier == variant) {
                    LLVMAddCase(choice, LLVMConstInt(target, current->value, 0), valid);
                }
            }
            LLVMPositionBuilderAtEnd(emitter->builder, invalid);
            if (!r_llvm_panic(emitter,
                              "R_RUNTIME_PANIC_INVALID_CONVERSION",
                              instruction->span,
                              instruction->panic_target)) {
                return false;
            }
            LLVMPositionBuilderAtEnd(emitter->builder, valid);
        }
        if ((target_kind == R_SEMANTIC_TYPE_CHAR) && (instruction->operation == R_TOKEN_KW_AS) &&
            (r_llvm_value_type(emitter, operand->type) !=
             r_llvm_value_type(emitter, instruction->type))) {
            /* A char is a Unicode scalar value: below 0x110000 and outside the surrogates. */
            LLVMValueRef above = LLVMBuildICmp(
                emitter->builder, LLVMIntUGT, result, r_llvm_u32(emitter, 0x10FFFFU), "");
            LLVMValueRef surrogate = LLVMBuildICmp(
                emitter->builder,
                LLVMIntULT,
                LLVMBuildSub(emitter->builder, result, r_llvm_u32(emitter, 0xD800U), ""),
                r_llvm_u32(emitter, 0x800U),
                "");
            if (!r_llvm_check(emitter,
                              LLVMBuildOr(emitter->builder, above, surrogate, ""),
                              "R_RUNTIME_PANIC_INVALID_CONVERSION",
                              instruction->span,
                              instruction->panic_target)) {
                return false;
            }
        }
    } else {
        return r_llvm_unsupported(emitter, "a conversion to this type");
    }
    return r_llvm_set_value(emitter, instruction->result, result);
}

/* ---- Constants and literals ---- */

static bool r_llvm_emit_constant(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RSemanticTypeKind kind = r_llvm_value_kind(emitter, instruction->type);
    LLVMTypeRef type = r_llvm_scalar_type(emitter, instruction->type);
    LLVMValueRef value;

    if ((instruction->operation == R_TOKEN_KW_NULL) && (type != NULL)) {
        return r_llvm_set_value(emitter, instruction->result, LLVMConstNull(type));
    }
    if (type == NULL) {
        /* In memory: a null owner or interface borrow is zero (an owner's flag is set, as in
           C17), an empty byte array is initialized by the runtime. */
        const RSemanticType *value_type =
            r_llvm_type(emitter, r_llvm_value_type(emitter, instruction->type));
        LLVMValueRef memory;
        uint32_t size = 0U;
        uint32_t align = 0U;
        const bool bytes = (value_type != NULL) && (value_type->kind == R_SEMANTIC_TYPE_ARRAY) &&
                           (value_type->flags == R_SEMANTIC_TYPE_FLAG_NONE) &&
                           (value_type->second == R_TYPE_ID_INVALID) &&
                           (value_type->length == 0U) &&
                           (r_llvm_value_kind(emitter, value_type->base) == R_SEMANTIC_TYPE_U8);
        if ((instruction->operation != R_TOKEN_KW_NULL) && (instruction->integer_value == 0U) &&
            (instruction->aggregate_member == 0U) && (instruction->operand_count == 0U) &&
            (value_type != NULL) &&
            ((value_type->kind == R_SEMANTIC_TYPE_SLICE) ||
             (value_type->kind == R_SEMANTIC_TYPE_STR) ||
             (value_type->kind == R_SEMANTIC_TYPE_CONSTEXPR_STR))) {
            /* The empty view: a slice is {NULL, 0} ((T[]){0} in C17); the data of a string is
               not null. */
            memory = r_llvm_value_memory(emitter, instruction->result, instruction->type);
            if (memory == NULL) {
                return false;
            }
            (void)LLVMBuildStore(emitter->builder,
                                 value_type->kind == R_SEMANTIC_TYPE_SLICE
                                     ? LLVMConstNull(r_llvm_pointer(emitter))
                                     : r_llvm_empty_string(emitter),
                                 memory);
            (void)LLVMBuildStore(
                emitter->builder, r_llvm_u64(emitter, 0U), r_llvm_byte_offset(emitter, memory, 8U));
            return true;
        }
        if ((instruction->operation != R_TOKEN_KW_NULL) && !bytes) {
            return r_llvm_unsupported(emitter, "a constant in memory");
        }
        if ((instruction->operation == R_TOKEN_KW_NULL) && (kind != R_SEMANTIC_TYPE_OWN) &&
            (r_semantic_dyn_referent(emitter->frontend, instruction->type) == R_TYPE_ID_INVALID)) {
            return r_llvm_unsupported(emitter, "a null of this value in memory");
        }
        memory = r_llvm_value_memory(emitter, instruction->result, instruction->type);
        if ((memory == NULL) || !r_llvm_layout(emitter, instruction->type, &size, &align)) {
            return false;
        }
        (void)LLVMBuildMemSet(emitter->builder,
                              memory,
                              LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                              r_llvm_u64(emitter, size),
                              align);
        if (bytes) {
            LLVMValueRef arguments[3];
            arguments[0] = memory;
            arguments[1] =
                r_llvm_call_runtime(emitter, "r_runtime_hosted_allocator", NULL, 0U, NULL);
            arguments[2] = r_llvm_type_info(emitter, value_type->base);
            if ((arguments[1] == NULL) || (arguments[2] == NULL) ||
                (r_llvm_call_runtime(emitter, "r_runtime_array_initialize", arguments, 3U, NULL) ==
                 NULL)) {
                return false;
            }
        }
        r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, instruction->result), true);
        return true;
    }
    if (kind == R_SEMANTIC_TYPE_F64) {
        value = LLVMConstBitCast(
            LLVMConstInt(r_llvm_int(emitter, 64U), instruction->integer_value, 0), type);
    } else if (kind == R_SEMANTIC_TYPE_F32) {
        value = LLVMConstBitCast(
            LLVMConstInt(r_llvm_int(emitter, 32U), instruction->integer_value & UINT32_MAX, 0),
            type);
    } else if (LLVMGetTypeKind(type) == LLVMPointerTypeKind) {
        return r_llvm_unsupported(emitter, "a pointer constant");
    } else {
        value = LLVMConstInt(type, instruction->integer_value, 0);
    }
    return r_llvm_set_value(emitter, instruction->result, value);
}

/* A program string: the bytes of an interned text as a private constant. */
LLVMValueRef r_llvm_program_string(RLlvmEmitter *emitter, uint32_t intern_id) {
    const RInternEntry *entry;

    if ((intern_id == 0U) || ((size_t)intern_id > emitter->frontend->intern_count)) {
        (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        return NULL;
    }
    if (emitter->strings[intern_id] == NULL) {
        LLVMValueRef global;
        entry = &emitter->frontend->intern_entries[(size_t)intern_id - 1U];
        /* An empty string still has an address that is not null. */
        global = LLVMAddGlobal(emitter->module,
                               LLVMArrayType2(r_llvm_int(emitter, 8U), entry->length + 1U),
                               "r_string");
        LLVMSetInitializer(
            global, LLVMConstStringInContext2(emitter->context, entry->bytes, entry->length, 0));
        LLVMSetLinkage(global, LLVMPrivateLinkage);
        LLVMSetGlobalConstant(global, 1);
        LLVMSetUnnamedAddress(global, LLVMGlobalUnnamedAddr);
        LLVMSetAlignment(global, 1U);
        emitter->strings[intern_id] = global;
    }
    return emitter->strings[intern_id];
}

/* The data of an empty string that no program text names (r_empty_program_string of the C17
   emitter): one zero byte, kept in the unused intern slot 0. */
LLVMValueRef r_llvm_empty_string(RLlvmEmitter *emitter) {
    if (emitter->strings[0] == NULL) {
        LLVMValueRef global =
            LLVMAddGlobal(emitter->module, LLVMArrayType2(r_llvm_int(emitter, 8U), 1U), "r_string");
        LLVMSetInitializer(global, LLVMConstNull(LLVMArrayType2(r_llvm_int(emitter, 8U), 1U)));
        LLVMSetLinkage(global, LLVMPrivateLinkage);
        LLVMSetGlobalConstant(global, 1);
        LLVMSetUnnamedAddress(global, LLVMGlobalUnnamedAddr);
        LLVMSetAlignment(global, 1U);
        emitter->strings[0] = global;
    }
    return emitter->strings[0];
}

static bool r_llvm_emit_string(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    LLVMValueRef data = r_llvm_program_string(emitter, instruction->aggregate_member);
    LLVMValueRef memory = r_llvm_value_memory(emitter, instruction->result, instruction->type);

    if ((data == NULL) || (memory == NULL)) {
        return false;
    }
    /* { const uint8_t *r_data; size_t r_len; } and RRuntimeStringView alike. */
    (void)LLVMBuildStore(emitter->builder, data, memory);
    (void)LLVMBuildStore(emitter->builder,
                         r_llvm_u64(emitter, instruction->integer_value),
                         r_llvm_byte_offset(emitter, memory, 8U));
    return true;
}

/* ---- Places, aggregates and variants ---- */

/* LOAD and MOVE of a place. A value that requires drop moves out of a whole place with the
   place's flag; read through a projection it is a bitwise copy without a flag, as in the C17
   emitter. MOVE of a variant moves the payload out of the projected place. */
/* Whether `instruction`'s result is used once, by the RETURN that follows it in its block. */
static bool r_llvm_returned_at_once(const RLlvmEmitter *emitter,
                                    const RMirInstruction *instruction) {
    const RFrontendContext *context = emitter->frontend;
    const RMirBlock *block =
        &context->mir_blocks[(size_t)emitter->mir->first_block + emitter->current_block];
    const RMirInstruction *first = &context->mir_instructions[block->first_instruction];
    const RMirInstruction *next = instruction + 1;
    const RMirValueId value = instruction->result;

    if ((instruction < first) || (next >= first + block->instruction_count) ||
        (next->kind != R_MIR_INSTRUCTION_RETURN) || (next->operand0 != value)) {
        return false;
    }
    return r_llvm_value_uses(emitter, value) == 1U;
}

static bool r_llvm_emit_load_place(RLlvmEmitter *emitter, const RMirInstruction *instruction);

/* A read through an imported C object, a field or an element of it, is checked like any value
   that comes from C (R-FFI-0056). */
static bool r_llvm_emit_load(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    LLVMValueRef value;

    if (!r_llvm_emit_load_place(emitter, instruction)) {
        return false;
    }
    if ((instruction->kind != R_MIR_INSTRUCTION_LOAD) || instruction->place_is_parameter ||
        (emitter->local_imports == NULL) || (instruction->place_ordinal >= emitter->local_count) ||
        (emitter->local_imports[instruction->place_ordinal] == 0U)) {
        return true;
    }
    value = r_llvm_value(emitter, instruction->result);
    return (value != NULL) &&
           r_llvm_ffi_ingress(emitter, instruction->type, value, instruction->span);
}

static bool r_llvm_emit_load_place(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const bool owned = r_llvm_type_requires_drop(emitter, instruction->type);
    LLVMValueRef address;

    if (instruction->operation == R_TOKEN_AMP) {
        /* The address of the allocation of an owner, as an integer (`(uintptr_t)place.allocation`).
         */
        uint32_t allocation = 0U;
        LLVMTypeRef integer = r_llvm_scalar_type(emitter, instruction->type);
        address = r_llvm_place_address(emitter,
                                       instruction->place_ordinal,
                                       instruction->place_is_parameter,
                                       instruction->operand1);
        if ((address == NULL) || (integer == NULL) ||
            !r_llvm_runtime_field(emitter, "RRuntimeOwn", "allocation", &allocation, NULL)) {
            return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
        return r_llvm_set_value(
            emitter,
            instruction->result,
            LLVMBuildPtrToInt(emitter->builder,
                              LLVMBuildLoad2(emitter->builder,
                                             r_llvm_pointer(emitter),
                                             r_llvm_byte_offset(emitter, address, allocation),
                                             ""),
                              integer,
                              ""));
    }
    if ((instruction->kind == R_MIR_INSTRUCTION_MOVE) &&
        (instruction->operation == R_TOKEN_KW_VARIANT)) {
        LLVMValueRef memory;
        address = r_llvm_place_address(emitter,
                                       instruction->place_ordinal,
                                       instruction->place_is_parameter,
                                       instruction->operand1);
        if (!owned) {
            return r_llvm_load_into(emitter, instruction->result, instruction->type, address);
        }
        memory = r_llvm_value_memory(emitter, instruction->result, instruction->type);
        if ((memory == NULL) || (address == NULL) ||
            !r_llvm_call_move(emitter, instruction->type, memory, address)) {
            return false;
        }
        r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, instruction->result), true);
        return true;
    }
    if (!owned || ((instruction->kind == R_MIR_INSTRUCTION_LOAD) &&
                   (instruction->operand1 != R_MIR_VALUE_ID_INVALID))) {
        address = r_llvm_place_address(emitter,
                                       instruction->place_ordinal,
                                       instruction->place_is_parameter,
                                       instruction->operand1);
        /* `return place;` of a value in memory without drop glue copies the place into the
           result, as C17 does, instead of through a slot of its own: nothing runs between the
           load and the return, which moves the value out first (R-FUNC-0004 budgets). */
        if (!owned && (address != NULL) &&
            (r_llvm_scalar_type(emitter, instruction->type) == NULL) &&
            (emitter->values[instruction->result] == NULL) &&
            r_llvm_returned_at_once(emitter, instruction)) {
            return r_llvm_set_value(emitter, instruction->result, address);
        }
        return r_llvm_load_into(emitter, instruction->result, instruction->type, address);
    }
    if (instruction->operand1 != R_MIR_VALUE_ID_INVALID) {
        /* A move out of a part of a place: the part has no flag of its own. */
        LLVMValueRef memory = r_llvm_value_memory(emitter, instruction->result, instruction->type);
        address = r_llvm_place_address(emitter,
                                       instruction->place_ordinal,
                                       instruction->place_is_parameter,
                                       instruction->operand1);
        if ((memory == NULL) || (address == NULL) ||
            !r_llvm_call_move(emitter, instruction->type, memory, address)) {
            return false;
        }
        r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, instruction->result), true);
        return true;
    }
    {
        LLVMValueRef memory = r_llvm_value_memory(emitter, instruction->result, instruction->type);
        return (memory != NULL) &&
               r_llvm_transfer(emitter,
                               instruction->type,
                               memory,
                               r_llvm_value_flag(emitter, instruction->result),
                               r_llvm_place_address(emitter,
                                                    instruction->place_ordinal,
                                                    instruction->place_is_parameter,
                                                    R_MIR_VALUE_ID_INVALID),
                               r_llvm_place_flag(emitter,
                                                 instruction->place_ordinal,
                                                 instruction->place_is_parameter));
    }
}

static bool r_llvm_emit_store(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RMirInstruction *value_definition = r_llvm_definition(emitter, instruction->operand0);
    LLVMValueRef address;
    LLVMValueRef value;

    if (value_definition == NULL) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    if (emitter->forwarded[instruction->operand0] == R_LLVM_FORWARDED_PLACE) {
        /* The value was built in the place (B6-3): it moves in without a copy. */
        if (r_llvm_type_requires_drop(emitter, value_definition->type)) {
            r_llvm_set_flag(emitter,
                            r_llvm_place_flag(emitter,
                                              instruction->place_ordinal,
                                              instruction->place_is_parameter),
                            true);
            r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, instruction->operand0), false);
        }
        return true;
    }
    address = r_llvm_place_address(emitter,
                                   instruction->place_ordinal,
                                   instruction->place_is_parameter,
                                   instruction->operand1);
    value = r_llvm_value(emitter, instruction->operand0);
    if ((address == NULL) || (value == NULL)) {
        return false;
    }
    if (!r_llvm_type_requires_drop(emitter, value_definition->type)) {
        return r_llvm_store_value(emitter, value_definition->type, value, address);
    }
    if (instruction->operand1 != R_MIR_VALUE_ID_INVALID) {
        /* As the C17 emitter's assignment: a part that holds a value has it dropped before the
           new one moves in; a part that does not (the first store through an `out` parameter)
           only receives it. */
        if ((instruction->replaces_initialized &&
             !r_llvm_call_drop(emitter, value_definition->type, address)) ||
            !r_llvm_call_move(emitter, value_definition->type, address, value)) {
            return false;
        }
        r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, instruction->operand0), false);
        return true;
    }
    {
        LLVMValueRef flag =
            r_llvm_place_flag(emitter, instruction->place_ordinal, instruction->place_is_parameter);
        RTypeId place_type = instruction->place_is_parameter
                                 ? emitter->parameter_types[instruction->place_ordinal]
                                 : emitter->local_types[instruction->place_ordinal];
        if (instruction->replaces_initialized &&
            ((flag == NULL) ? !r_llvm_call_drop(emitter, place_type, address)
                            : !r_llvm_drop_flagged(emitter, place_type, address, flag))) {
            return false;
        }
        return r_llvm_transfer(emitter,
                               value_definition->type,
                               address,
                               flag,
                               value,
                               r_llvm_value_flag(emitter, instruction->operand0));
    }
}

static bool r_llvm_emit_borrow(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    LLVMValueRef address;

    if (instruction->operation == R_TOKEN_KW_AWAIT) {
        return true;
    }
    if (r_llvm_scalar_type(emitter, instruction->type) == NULL) {
        return r_llvm_unsupported(emitter, "a borrow of an interface");
    }
    /* The projection of a BORROW is its first operand. */
    address = r_llvm_place_address(emitter,
                                   instruction->place_ordinal,
                                   instruction->place_is_parameter,
                                   instruction->operand0);
    return (address != NULL) && r_llvm_set_value(emitter, instruction->result, address);
}

/* The length of the array, list, dict or str a place holds, or of its slice. */
static bool r_llvm_emit_length(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RMirInstruction *base = instruction->operand0 == R_MIR_VALUE_ID_INVALID
                                      ? NULL
                                      : r_llvm_definition(emitter, instruction->operand0);
    const RTypeId base_type = base != NULL ? base->type
                              : instruction->place_is_parameter
                                  ? emitter->parameter_types[instruction->place_ordinal]
                                  : emitter->local_types[instruction->place_ordinal];
    const RSemanticTypeKind kind = r_llvm_value_kind(emitter, base_type);
    LLVMValueRef address = r_llvm_place_address(emitter,
                                                instruction->place_ordinal,
                                                instruction->place_is_parameter,
                                                instruction->operand0);
    uint32_t offset = 8U;

    if (address == NULL) {
        return false;
    }
    if (kind == R_SEMANTIC_TYPE_ARRAY) {
        if (!r_llvm_runtime_field(emitter, "RRuntimeArray", "length", &offset, NULL)) {
            return false;
        }
    } else if ((kind == R_SEMANTIC_TYPE_LIST) || (kind == R_SEMANTIC_TYPE_DICT)) {
        if (!r_llvm_runtime_field(emitter,
                                  kind == R_SEMANTIC_TYPE_LIST ? "RRuntimeList" : "RRuntimeDict",
                                  "length",
                                  &offset,
                                  NULL)) {
            return false;
        }
    } else if ((kind != R_SEMANTIC_TYPE_STR) && (kind != R_SEMANTIC_TYPE_SLICE) &&
               (kind != R_SEMANTIC_TYPE_CONSTEXPR_STR)) {
        return r_llvm_unsupported(emitter, "the length of this type");
    }
    return r_llvm_set_value(emitter,
                            instruction->result,
                            LLVMBuildLoad2(emitter->builder,
                                           r_llvm_int(emitter, 64U),
                                           r_llvm_byte_offset(emitter, address, offset),
                                           ""));
}

/* An index value converted to size_t, and whether that conversion fails (a negative value). */
static LLVMValueRef r_llvm_size_index(RLlvmEmitter *emitter, RMirValueId id, LLVMValueRef *failed) {
    const RMirInstruction *definition = r_llvm_definition(emitter, id);
    LLVMValueRef value = r_llvm_value(emitter, id);
    unsigned bits = 0U;
    bool is_signed = false;

    if ((definition == NULL) || (value == NULL) ||
        !r_llvm_integer_semantics(emitter, definition->type, &bits, &is_signed)) {
        (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        return NULL;
    }
    *failed = is_signed
                  ? LLVMBuildICmp(
                        emitter->builder, LLVMIntSLT, value, LLVMConstNull(LLVMTypeOf(value)), "")
                  : LLVMConstInt(r_llvm_int(emitter, 1U), 0U, 0);
    if (bits == 64U) {
        return value;
    }
    return is_signed ? LLVMBuildSExt(emitter->builder, value, r_llvm_int(emitter, 64U), "")
                     : LLVMBuildZExt(emitter->builder, value, r_llvm_int(emitter, 64U), "");
}

/* SLICE: a view { data, length } of a fixed array, a slice or an array<T> a place holds, narrowed
   to [lo..] or [lo..hi] with the bounds checks of the C17 emitter's r_c17_emit_async_slice. */
static bool r_llvm_emit_slice(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RSemanticType *slice =
        r_llvm_type(emitter, r_llvm_value_type(emitter, instruction->type));
    LLVMValueRef memory = r_llvm_value_memory(emitter, instruction->result, instruction->type);
    LLVMValueRef data;
    LLVMValueRef length;
    uint32_t element_size = 0U;
    uint32_t element_align = 0U;

    if ((slice == NULL) || (memory == NULL) || (slice->kind != R_SEMANTIC_TYPE_SLICE) ||
        !r_llvm_layout(emitter, slice->base, &element_size, &element_align)) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    if (instruction->operation == R_TOKEN_KW_ARRAY) {
        /* A view of the array<T> a pointer designates. */
        LLVMValueRef array = r_llvm_value(emitter, instruction->operand0);
        uint32_t data_offset = 0U;
        uint32_t length_offset = 0U;
        if ((array == NULL) ||
            !r_llvm_runtime_field(emitter, "RRuntimeArray", "data", &data_offset, NULL) ||
            !r_llvm_runtime_field(emitter, "RRuntimeArray", "length", &length_offset, NULL)) {
            return false;
        }
        data = LLVMBuildLoad2(emitter->builder,
                              r_llvm_pointer(emitter),
                              r_llvm_byte_offset(emitter, array, data_offset),
                              "");
        length = LLVMBuildLoad2(emitter->builder,
                                r_llvm_int(emitter, 64U),
                                r_llvm_byte_offset(emitter, array, length_offset),
                                "");
    } else {
        const RMirInstruction *base = instruction->operand0 == R_MIR_VALUE_ID_INVALID
                                          ? NULL
                                          : r_llvm_definition(emitter, instruction->operand0);
        const RTypeId base_type = base != NULL ? base->type
                                  : instruction->place_is_parameter
                                      ? emitter->parameter_types[instruction->place_ordinal]
                                      : emitter->local_types[instruction->place_ordinal];
        const RSemanticType *container =
            r_llvm_type(emitter, r_llvm_value_type(emitter, base_type));
        LLVMValueRef address = r_llvm_place_address(emitter,
                                                    instruction->place_ordinal,
                                                    instruction->place_is_parameter,
                                                    instruction->operand0);
        if ((container == NULL) || (address == NULL)) {
            return false;
        }
        switch (container->kind) {
        case R_SEMANTIC_TYPE_FIXED_ARRAY:
            data = address;
            length = r_llvm_u64(emitter, container->length);
            break;
        case R_SEMANTIC_TYPE_SLICE:
            data = LLVMBuildLoad2(emitter->builder, r_llvm_pointer(emitter), address, "");
            length = LLVMBuildLoad2(emitter->builder,
                                    r_llvm_int(emitter, 64U),
                                    r_llvm_byte_offset(emitter, address, 8U),
                                    "");
            break;
        case R_SEMANTIC_TYPE_ARRAY: {
            uint32_t data_offset = 0U;
            uint32_t length_offset = 0U;
            if (!r_llvm_runtime_field(emitter, "RRuntimeArray", "data", &data_offset, NULL) ||
                !r_llvm_runtime_field(emitter, "RRuntimeArray", "length", &length_offset, NULL)) {
                return false;
            }
            data = LLVMBuildLoad2(emitter->builder,
                                  r_llvm_pointer(emitter),
                                  r_llvm_byte_offset(emitter, address, data_offset),
                                  "");
            length = LLVMBuildLoad2(emitter->builder,
                                    r_llvm_int(emitter, 64U),
                                    r_llvm_byte_offset(emitter, address, length_offset),
                                    "");
            break;
        }
        default:
            return r_llvm_unsupported(emitter, "a slice of this place");
        }
    }
    if (instruction->operand1 != R_MIR_VALUE_ID_INVALID) {
        LLVMValueRef lower_failed = NULL;
        LLVMValueRef lower = r_llvm_size_index(emitter, instruction->operand1, &lower_failed);
        LLVMValueRef failed;
        LLVMValueRef upper;
        if (lower == NULL) {
            return false;
        }
        if (instruction->operand2 == R_MIR_VALUE_ID_INVALID) {
            /* `a[lo..]`: the upper bound is the length the view already has. */
            upper = length;
            failed = LLVMBuildOr(emitter->builder,
                                 lower_failed,
                                 LLVMBuildICmp(emitter->builder, LLVMIntUGT, lower, length, ""),
                                 "");
        } else {
            LLVMValueRef upper_failed = NULL;
            upper = r_llvm_size_index(emitter, instruction->operand2, &upper_failed);
            if (upper == NULL) {
                return false;
            }
            failed = LLVMBuildOr(
                emitter->builder,
                LLVMBuildOr(emitter->builder, lower_failed, upper_failed, ""),
                LLVMBuildOr(emitter->builder,
                            LLVMBuildICmp(emitter->builder, LLVMIntUGT, lower, upper, ""),
                            LLVMBuildICmp(emitter->builder, LLVMIntUGT, upper, length, ""),
                            ""),
                "");
        }
        if (!r_llvm_check(emitter,
                          failed,
                          "R_RUNTIME_PANIC_BOUNDS",
                          instruction->span,
                          instruction->panic_target)) {
            return false;
        }
        {
            LLVMValueRef moved = r_llvm_element_offset(emitter, data, lower, element_size);
            /* The data pointer moves only for a lower bound that is not zero. */
            data = LLVMBuildSelect(
                emitter->builder,
                LLVMBuildICmp(emitter->builder, LLVMIntNE, lower, r_llvm_u64(emitter, 0U), ""),
                moved,
                data,
                "");
        }
        length = LLVMBuildNUWSub(emitter->builder, upper, lower, "");
    }
    (void)LLVMBuildStore(emitter->builder, data, memory);
    (void)LLVMBuildStore(emitter->builder, length, r_llvm_byte_offset(emitter, memory, 8U));
    return true;
}

/* NEW: an own, arc or rc owner of a value, created by the runtime from its type information (the
   C17 emitter's r_new_own, r_new_arc and r_new_rc helpers); the value moves into the owner. */
/* The initializer of a zero `new own` payload (RRuntimeOwnInitializeFn): the context points at
   the size of the storage, which it fills with zeros. */
static LLVMValueRef r_llvm_zero_initializer(RLlvmEmitter *emitter) {
    static const char name[] = "r_own_zero";
    LLVMValueRef function = LLVMGetNamedFunction(emitter->module, name);

    if (function == NULL) {
        LLVMTypeRef parameters[2];
        LLVMBuilderRef builder = LLVMCreateBuilderInContext(emitter->context);
        LLVMValueRef size;
        parameters[0] = r_llvm_pointer(emitter);
        parameters[1] = r_llvm_pointer(emitter);
        function = LLVMAddFunction(
            emitter->module,
            name,
            LLVMFunctionType(LLVMVoidTypeInContext(emitter->context), parameters, 2U, 0));
        LLVMSetLinkage(function, LLVMInternalLinkage);
        LLVMPositionBuilderAtEnd(builder,
                                 LLVMAppendBasicBlockInContext(emitter->context, function, ""));
        size = LLVMBuildLoad2(builder, r_llvm_int(emitter, 64U), LLVMGetParam(function, 1U), "");
        (void)LLVMBuildMemSet(builder,
                              LLVMGetParam(function, 0U),
                              LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                              size,
                              1U);
        (void)LLVMBuildRetVoid(builder);
        LLVMDisposeBuilder(builder);
    }
    return function;
}

/* `new own T{}` whose payload is zero bytes (B6-4): the runtime allocates the storage and the
   initializer fills it, so no copy of the value exists on the stack or in a frame. */
static bool r_llvm_emit_zero_new(RLlvmEmitter *emitter,
                                 const RMirInstruction *instruction,
                                 LLVMValueRef memory,
                                 uint32_t size,
                                 uint32_t align) {
    uint32_t payload_size = 0U;
    uint32_t payload_align = 0U;
    char name[48];
    LLVMValueRef context;
    LLVMValueRef arguments[5];
    LLVMValueRef status;

    if (!r_llvm_layout(emitter, instruction->auxiliary_type, &payload_size, &payload_align)) {
        return false;
    }
    (void)snprintf(name, sizeof(name), "r_own_zero.%" PRIu32, payload_size);
    context = LLVMGetNamedGlobal(emitter->module, name);
    if (context == NULL) {
        context = LLVMAddGlobal(emitter->module, r_llvm_int(emitter, 64U), name);
        LLVMSetInitializer(context, r_llvm_u64(emitter, payload_size));
        LLVMSetGlobalConstant(context, 1);
        LLVMSetLinkage(context, LLVMPrivateLinkage);
    }
    (void)LLVMBuildMemSet(emitter->builder,
                          memory,
                          LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                          r_llvm_u64(emitter, size),
                          align);
    arguments[0] = r_llvm_call_runtime(emitter, "r_runtime_hosted_allocator", NULL, 0U, NULL);
    arguments[1] = r_llvm_type_info(emitter, instruction->auxiliary_type);
    arguments[2] = r_llvm_zero_initializer(emitter);
    arguments[3] = context;
    arguments[4] = memory;
    if ((arguments[0] == NULL) || (arguments[1] == NULL)) {
        return false;
    }
    status = r_llvm_call_runtime(emitter, "r_runtime_own_create_initialize", arguments, 5U, NULL);
    if ((status == NULL) ||
        !r_llvm_check(
            emitter,
            LLVMBuildICmp(
                emitter->builder, LLVMIntNE, status, LLVMConstNull(LLVMTypeOf(status)), ""),
            "R_RUNTIME_PANIC_ALLOCATION_FAILURE",
            instruction->span,
            r_llvm_helper_target(emitter, instruction->panic_target))) {
        return false;
    }
    r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, instruction->result), true);
    return true;
}

static bool r_llvm_emit_new(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const char *create = instruction->operation == R_TOKEN_KW_OWN   ? "r_runtime_own_create"
                         : instruction->operation == R_TOKEN_KW_ARC ? "r_runtime_arc_create"
                         : instruction->operation == R_TOKEN_KW_RC  ? "r_runtime_rc_create"
                                                                    : NULL;
    LLVMValueRef memory = r_llvm_value_memory(emitter, instruction->result, instruction->type);
    const RMirInstruction *payload = r_llvm_definition(emitter, instruction->operand0);
    LLVMValueRef value;
    LLVMValueRef arguments[4];
    LLVMValueRef status;
    uint32_t size = 0U;
    uint32_t align = 0U;

    if (create == NULL) {
        return r_llvm_unsupported(emitter, "this new");
    }
    if ((memory != NULL) && r_llvm_layout(emitter, instruction->type, &size, &align) &&
        r_llvm_zero_new(emitter, instruction)) {
        return r_llvm_emit_zero_new(emitter, instruction, memory, size, align);
    }
    value = r_llvm_value(emitter, instruction->operand0);
    if ((memory == NULL) || (value == NULL) || (payload == NULL) ||
        !r_llvm_layout(emitter, instruction->type, &size, &align)) {
        return false;
    }
    if (r_llvm_scalar_type(emitter, payload->type) != NULL) {
        /* The runtime copies the value from memory. */
        uint32_t payload_size = 0U;
        uint32_t payload_align = 0U;
        LLVMValueRef spill;
        if (!r_llvm_layout(emitter, payload->type, &payload_size, &payload_align)) {
            return false;
        }
        spill = r_llvm_entry_alloca(emitter, payload_size, payload_align, "payload");
        r_llvm_store_scalar(emitter, payload->type, value, spill);
        value = spill;
    }
    (void)LLVMBuildMemSet(emitter->builder,
                          memory,
                          LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                          r_llvm_u64(emitter, size),
                          align);
    arguments[0] = r_llvm_call_runtime(emitter, "r_runtime_hosted_allocator", NULL, 0U, NULL);
    arguments[1] = r_llvm_type_info(emitter, instruction->auxiliary_type);
    arguments[2] = value;
    arguments[3] = memory;
    if ((arguments[0] == NULL) || (arguments[1] == NULL)) {
        return false;
    }
    status = r_llvm_call_runtime(emitter, create, arguments, 4U, NULL);
    if ((status == NULL) ||
        !r_llvm_check(
            emitter,
            LLVMBuildICmp(
                emitter->builder, LLVMIntNE, status, LLVMConstNull(LLVMTypeOf(status)), ""),
            "R_RUNTIME_PANIC_ALLOCATION_FAILURE",
            instruction->span,
            r_llvm_helper_target(emitter, instruction->panic_target))) {
        return false;
    }
    r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, instruction->result), true);
    if (r_llvm_type_requires_drop(emitter, instruction->auxiliary_type)) {
        r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, instruction->operand0), false);
    }
    return true;
}

/* INDEX: the bounds check at the point of the index (R-EXPR-0021). */
static bool r_llvm_emit_index(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RMirInstruction *base = instruction->operand0 == R_MIR_VALUE_ID_INVALID
                                      ? NULL
                                      : r_llvm_definition(emitter, instruction->operand0);
    const RMirInstruction *index_definition = r_llvm_definition(emitter, instruction->operand1);
    const RTypeId base_type = base != NULL ? base->type
                              : instruction->place_is_parameter
                                  ? emitter->parameter_types[instruction->place_ordinal]
                                  : emitter->local_types[instruction->place_ordinal];
    const RSemanticType *array = r_llvm_type(emitter, r_llvm_value_type(emitter, base_type));
    LLVMValueRef index = r_llvm_value(emitter, instruction->operand1);
    LLVMValueRef wide;
    LLVMValueRef failed;
    LLVMValueRef length;
    unsigned bits = 0U;
    bool is_signed = false;

    if ((array == NULL) || (index == NULL) || (index_definition == NULL) ||
        !r_llvm_integer_semantics(emitter, index_definition->type, &bits, &is_signed)) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    /* A negative index, or one above SIZE_MAX, cannot convert to size_t. */
    wide = bits < 64U
               ? (is_signed ? LLVMBuildSExt(emitter->builder, index, r_llvm_int(emitter, 64U), "")
                            : LLVMBuildZExt(emitter->builder, index, r_llvm_int(emitter, 64U), ""))
               : index;
    failed = is_signed
                 ? LLVMBuildICmp(emitter->builder, LLVMIntSLT, wide, r_llvm_u64(emitter, 0U), "")
                 : LLVMConstInt(r_llvm_int(emitter, 1U), 0U, 0);
    if (array->kind == R_SEMANTIC_TYPE_FIXED_ARRAY) {
        length = r_llvm_u64(emitter, array->length);
    } else if ((array->kind == R_SEMANTIC_TYPE_SLICE) || (array->kind == R_SEMANTIC_TYPE_ARRAY)) {
        uint32_t offset = 8U;
        LLVMValueRef address = r_llvm_place_address(emitter,
                                                    instruction->place_ordinal,
                                                    instruction->place_is_parameter,
                                                    instruction->operand0);
        if ((address == NULL) ||
            ((array->kind == R_SEMANTIC_TYPE_ARRAY) &&
             !r_llvm_runtime_field(emitter, "RRuntimeArray", "length", &offset, NULL))) {
            return false;
        }
        length = LLVMBuildLoad2(emitter->builder,
                                r_llvm_int(emitter, 64U),
                                r_llvm_byte_offset(emitter, address, offset),
                                "");
    } else {
        return r_llvm_unsupported(emitter, "an index of this type");
    }
    failed = LLVMBuildOr(emitter->builder,
                         failed,
                         LLVMBuildICmp(emitter->builder, LLVMIntUGE, wide, length, ""),
                         "");
    if (!is_signed && (bits == 64U)) {
        /* B7: an affine index of a loop is checked only where its largest value might not fit
           (versions.c). */
        LLVMValueRef fits = r_llvm_version_fits(emitter, instruction, length);
        if (fits != NULL) {
            failed = LLVMBuildAnd(
                emitter->builder, failed, LLVMBuildNot(emitter->builder, fits, ""), "");
        }
    }
    return r_llvm_check(
        emitter, failed, "R_RUNTIME_PANIC_BOUNDS", instruction->span, instruction->panic_target);
}

/* Zeroes the memory of an aggregate or array except the members built in place (B6-3), which
   hold their values already: the gaps between them, padding included, in order. */
static bool r_llvm_zero_around_members(RLlvmEmitter *emitter,
                                       const RMirInstruction *instruction,
                                       LLVMValueRef memory,
                                       uint32_t size,
                                       uint32_t align) {
    const bool is_array = instruction->kind == R_MIR_INSTRUCTION_ARRAY;
    const RSemanticType *type = r_llvm_type(emitter, r_llvm_value_type(emitter, instruction->type));
    uint32_t cursor = 0U;

    for (;;) {
        uint32_t next_start = size;
        uint32_t next_end = size;
        uint32_t index;
        for (index = 0U; index < instruction->operand_count; ++index) {
            const size_t position = (size_t)instruction->first_operand + index;
            const RMirValueId value_id = emitter->frontend->mir_operands[position];
            const uint32_t member = emitter->frontend->mir_operand_members[position];
            const RSemanticField *field = is_array ? NULL : r_llvm_field(emitter, member);
            const RTypeId member_type =
                is_array ? type->base : (field == NULL ? R_TYPE_ID_INVALID : field->type);
            uint32_t offset = 0U;
            uint32_t member_size = 0U;
            uint32_t member_align = 0U;
            if (((size_t)value_id >= emitter->value_count) ||
                (emitter->forwarded[value_id] != R_LLVM_FORWARDED_MEMBER)) {
                continue;
            }
            if ((member_type == R_TYPE_ID_INVALID) ||
                !r_llvm_layout(emitter, member_type, &member_size, &member_align) ||
                (!is_array && !r_llvm_field_offset(emitter, member, &offset))) {
                return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
            }
            if (is_array) {
                offset = member_size * index;
            }
            if ((offset >= cursor) && (offset < next_start)) {
                next_start = offset;
                next_end = offset + member_size;
            }
        }
        if (next_start > cursor) {
            (void)LLVMBuildMemSet(emitter->builder,
                                  r_llvm_byte_offset(emitter, memory, cursor),
                                  LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                                  r_llvm_u64(emitter, next_start - cursor),
                                  cursor == 0U ? align : 1U);
        }
        if (next_start >= size) {
            return true;
        }
        cursor = next_end;
    }
}

static bool
r_llvm_build_aggregate(RLlvmEmitter *emitter, const RMirInstruction *instruction, bool is_array);

static bool r_llvm_emit_aggregate(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const bool is_array = instruction->kind == R_MIR_INSTRUCTION_ARRAY;

    if (r_llvm_zero_new_member(emitter, instruction->result)) {
        emitter->forwarded[instruction->result] = R_LLVM_FORWARDED_ELIDED;
        return true;
    }
    return r_llvm_build_aggregate(emitter, instruction, is_array);
}

static bool
r_llvm_build_aggregate(RLlvmEmitter *emitter, const RMirInstruction *instruction, bool is_array) {
    const RSemanticType *type = r_llvm_type(emitter, r_llvm_value_type(emitter, instruction->type));
    LLVMValueRef memory = r_llvm_value_memory(emitter, instruction->result, instruction->type);
    uint32_t size = 0U;
    uint32_t align = 0U;
    uint32_t index;

    if ((memory == NULL) || (type == NULL) ||
        !r_llvm_layout(emitter, instruction->type, &size, &align)) {
        return false;
    }
    if (is_array && (type->kind != R_SEMANTIC_TYPE_FIXED_ARRAY)) {
        return r_llvm_unsupported(emitter, "an array literal of this type");
    }
    if (!is_array && (type->kind != R_SEMANTIC_TYPE_STANDARD)) {
        const RSemanticAggregate *aggregate = r_llvm_aggregate(emitter, instruction->type);
        /* A C-declared struct (R-FFI-0021) has the layout of its verified members, as C17
           declares it; only a tagged enum has no aggregate literal. */
        if ((aggregate == NULL) || aggregate->is_tagged) {
            return r_llvm_unsupported(emitter, "an aggregate of this type");
        }
    }
    if (!r_llvm_zero_around_members(emitter, instruction, memory, size, align)) {
        return false;
    }
    for (index = 0U; index < instruction->operand_count; ++index) {
        const size_t position = (size_t)instruction->first_operand + index;
        const RMirValueId value_id = emitter->frontend->mir_operands[position];
        const uint32_t member = emitter->frontend->mir_operand_members[position];
        const RSemanticField *field = is_array ? NULL : r_llvm_field(emitter, member);
        const RTypeId element_type =
            is_array ? type->base : (field == NULL ? R_TYPE_ID_INVALID : field->type);
        uint32_t element_size = 0U;
        uint32_t element_align = 0U;
        uint32_t offset = 0U;
        LLVMValueRef value = r_llvm_value(emitter, value_id);

        if ((element_type == R_TYPE_ID_INVALID) || (value == NULL)) {
            return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
        if (is_array) {
            if (!r_llvm_layout(emitter, element_type, &element_size, &element_align)) {
                return false;
            }
            offset = element_size * index;
        } else if (!r_llvm_field_offset(emitter, member, &offset)) {
            return false;
        }
        if (emitter->forwarded[value_id] == R_LLVM_FORWARDED_MEMBER) {
            /* Built in the member already (B6-3); it moved in. */
            r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, value_id), false);
            continue;
        }
        if (r_llvm_type_requires_drop(emitter, element_type)) {
            /* The member moves in and its value is no longer initialized. */
            if (!r_llvm_call_move(
                    emitter, element_type, r_llvm_byte_offset(emitter, memory, offset), value)) {
                return false;
            }
            r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, value_id), false);
        } else if (!r_llvm_store_value(
                       emitter, element_type, value, r_llvm_byte_offset(emitter, memory, offset))) {
            return false;
        }
    }
    r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, instruction->result), true);
    return true;
}

/* The payload type of a variant of a tagged enum, an option (some = 1) or a result (ok = 0). */
static RTypeId
r_llvm_variant_payload_type(RLlvmEmitter *emitter, RTypeId tagged_type, uint64_t tag) {
    const RSemanticType *type = r_llvm_type(emitter, r_llvm_value_type(emitter, tagged_type));
    const RSemanticVariant *variant;

    if (type == NULL) {
        return R_TYPE_ID_INVALID;
    }
    if (type->kind == R_SEMANTIC_TYPE_OPTION) {
        return tag == 1U ? type->base : R_TYPE_ID_INVALID;
    }
    if (type->kind == R_SEMANTIC_TYPE_RESULT) {
        return tag == 0U ? type->base : type->second;
    }
    if (r_llvm_compare_exchange_result(emitter, tagged_type)) {
        return type->base;
    }
    if (r_llvm_sync_tagged(emitter, tagged_type)) {
        return r_llvm_sync_variant_payload(emitter, tagged_type, tag);
    }
    if (r_llvm_standard_tagged(emitter, tagged_type)) {
        return r_llvm_standard_tagged_payload(emitter, tagged_type, tag);
    }
    if (r_llvm_thread_join_result(emitter, tagged_type)) {
        return tag == 0U
                   ? (r_llvm_type_is_void(emitter, type->base) ? R_TYPE_ID_INVALID : type->base)
                   : r_llvm_thread_panic_report(emitter);
    }
    variant =
        r_semantic_tagged_variant(emitter->frontend, r_llvm_value_type(emitter, tagged_type), tag);
    return variant == NULL ? R_TYPE_ID_INVALID : variant->payload_type;
}

/* Option, result, tagged enum, and the container errors of std.array, std.list and std.dict,
   whose allocation_failed payload {reason, (key,) value} is laid out as the R structure of that
   variant (r_c17_container_error_payload_member). */
static bool r_llvm_is_tagged(RLlvmEmitter *emitter, RTypeId id) {
    const RSemanticType *type = r_llvm_type(emitter, r_llvm_value_type(emitter, id));
    const RLlvmRecoveringKind recovering = r_llvm_recovering_error(emitter, id);
    return (type != NULL) &&
           ((type->kind == R_SEMANTIC_TYPE_OPTION) || (type->kind == R_SEMANTIC_TYPE_RESULT) ||
            (recovering == R_LLVM_RECOVERING_PUSH) || (recovering == R_LLVM_RECOVERING_INSERT) ||
            r_llvm_compare_exchange_result(emitter, id) || r_llvm_thread_join_result(emitter, id) ||
            r_llvm_sync_tagged(emitter, id) || r_llvm_standard_tagged(emitter, id) ||
            (r_semantic_tagged_enum(emitter->frontend, r_llvm_value_type(emitter, id)) != NULL));
}

static bool r_llvm_emit_variant(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RTypeId payload_type =
        r_llvm_variant_payload_type(emitter, instruction->type, instruction->integer_value);
    LLVMValueRef memory;
    uint32_t size = 0U;
    uint32_t align = 0U;
    uint32_t payload = 0U;

    /* The canonical options of std.time (RStdTimeDurationOption, RStdTimeSystemTimeOption) have
       the layout of any option: {uint32_t r_tag; union {T r_some;} r_payload}, some = 1. */
    if (!r_llvm_is_tagged(emitter, instruction->type)) {
        return r_llvm_unsupported(emitter, "a variant of this type");
    }
    memory = r_llvm_value_memory(emitter, instruction->result, instruction->type);
    if ((memory == NULL) || !r_llvm_layout(emitter, instruction->type, &size, &align) ||
        !r_llvm_payload_offset(emitter, instruction->type, &payload)) {
        return false;
    }
    (void)LLVMBuildMemSet(emitter->builder,
                          memory,
                          LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                          r_llvm_u64(emitter, size),
                          align);
    (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, instruction->integer_value), memory);
    if ((payload_type != R_TYPE_ID_INVALID) && !r_llvm_type_is_void(emitter, payload_type)) {
        LLVMValueRef value = r_llvm_value(emitter, instruction->operand0);
        if (value == NULL) {
            return false;
        }
        if (r_llvm_type_requires_drop(emitter, payload_type)) {
            if (!r_llvm_call_move(
                    emitter, payload_type, r_llvm_byte_offset(emitter, memory, payload), value)) {
                return false;
            }
            r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, instruction->operand0), false);
        } else if (!r_llvm_store_value(emitter,
                                       payload_type,
                                       value,
                                       r_llvm_byte_offset(emitter, memory, payload))) {
            return false;
        }
    }
    r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, instruction->result), true);
    return true;
}

/* The tagged value an operand holds directly or through a borrow, as an address, and its type. */
static LLVMValueRef r_llvm_tagged_operand(RLlvmEmitter *emitter,
                                          RMirValueId operand,
                                          RTypeId *tagged_type,
                                          bool *borrowed) {
    const RMirInstruction *definition = r_llvm_definition(emitter, operand);
    const RSemanticType *type =
        definition == NULL ? NULL
                           : r_llvm_type(emitter, r_llvm_value_type(emitter, definition->type));

    if (type == NULL) {
        (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        return NULL;
    }
    *borrowed = type->kind == R_SEMANTIC_TYPE_BORROW;
    *tagged_type = *borrowed ? type->base : definition->type;
    if (!r_llvm_is_tagged(emitter, *tagged_type)) {
        (void)r_llvm_unsupported(emitter, "the variant of a standard outcome");
        return NULL;
    }
    return r_llvm_value(emitter, operand);
}

static bool r_llvm_emit_variant_tag(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    RTypeId tagged_type = R_TYPE_ID_INVALID;
    bool borrowed = false;
    bool handled = false;
    bool lowered;
    LLVMValueRef address;

    lowered = r_llvm_emit_outcome_variant(emitter, instruction, &handled);
    if (handled || !lowered) {
        return lowered;
    }
    address = r_llvm_tagged_operand(emitter, instruction->operand0, &tagged_type, &borrowed);

    return (address != NULL) &&
           r_llvm_set_value(
               emitter,
               instruction->result,
               LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), address, ""));
}

static bool r_llvm_emit_variant_payload(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    RTypeId tagged_type = R_TYPE_ID_INVALID;
    bool borrowed = false;
    bool handled = false;
    bool lowered;
    LLVMValueRef address;
    uint32_t payload = 0U;

    lowered = r_llvm_emit_outcome_variant(emitter, instruction, &handled);
    if (handled || !lowered) {
        return lowered;
    }
    address = r_llvm_tagged_operand(emitter, instruction->operand0, &tagged_type, &borrowed);

    if ((address == NULL) || !r_llvm_payload_offset(emitter, tagged_type, &payload)) {
        return false;
    }
    address = r_llvm_byte_offset(emitter, address, payload);
    /* A binding by reference: the address of the payload in the tagged value, whether that is
       borrowed or a value of this function (values are never changed after their instruction). */
    if ((instruction->operation == R_TOKEN_AMP) ||
        ((r_llvm_value_kind(emitter, instruction->type) == R_SEMANTIC_TYPE_BORROW) &&
         (r_llvm_value_type(emitter, instruction->type) !=
          r_llvm_value_type(
              emitter,
              r_llvm_variant_payload_type(emitter, tagged_type, instruction->integer_value))))) {
        (void)borrowed;
        return r_llvm_set_value(emitter, instruction->result, address);
    }
    if (r_llvm_type_requires_drop(emitter, instruction->type)) {
        LLVMValueRef memory;
        LLVMValueRef tagged;
        if (!borrowed) {
            /* r_c17_emit_async_variant_payload: the payload of an option or result value moves
               out when that value is initialized, which then is not. */
            if ((r_llvm_value_kind(emitter, tagged_type) != R_SEMANTIC_TYPE_OPTION) &&
                (r_llvm_value_kind(emitter, tagged_type) != R_SEMANTIC_TYPE_RESULT) &&
                (r_llvm_recovering_error(emitter, tagged_type) != R_LLVM_RECOVERING_PUSH) &&
                (r_llvm_recovering_error(emitter, tagged_type) != R_LLVM_RECOVERING_INSERT) &&
                !r_llvm_thread_join_result(emitter, tagged_type) &&
                !r_llvm_sync_tagged(emitter, tagged_type) &&
                !r_llvm_standard_tagged(emitter, tagged_type)) {
                return r_llvm_unsupported(emitter,
                                          "a payload that owns resources moved out of a value");
            }
            return r_llvm_transfer(
                emitter,
                instruction->type,
                r_llvm_value_memory(emitter, instruction->result, instruction->type),
                r_llvm_value_flag(emitter, instruction->result),
                address,
                r_llvm_value_flag(emitter, instruction->operand0));
        }
        /* The payload moves out of the borrowed tagged value, which then holds no variant. */
        memory = r_llvm_value_memory(emitter, instruction->result, instruction->type);
        tagged = r_llvm_value(emitter, instruction->operand0);
        if ((memory == NULL) || (tagged == NULL) ||
            !r_llvm_call_move(emitter, instruction->type, memory, address)) {
            return false;
        }
        (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, UINT32_MAX), tagged);
        r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, instruction->result), true);
        return true;
    }
    return r_llvm_load_into(emitter, instruction->result, instruction->type, address);
}

/* EFFECT_TAG and EFFECT_PAYLOAD of a carrier a call returned. The payload of a catch, which a
   THROW in the same function writes, has no carrier operand: its value is a slot of its own. */
static bool r_llvm_emit_effect(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    LLVMValueRef carrier;
    uint32_t payload = 0U;

    if (instruction->kind == R_MIR_INSTRUCTION_EFFECT_PAYLOAD) {
        if (instruction->operand0 == R_MIR_VALUE_ID_INVALID) {
            LLVMValueRef slot = r_llvm_catch_slot(emitter, instruction->result, instruction->type);
            if (slot == NULL) {
                return false;
            }
            if (r_llvm_scalar_type(emitter, instruction->type) != NULL) {
                return r_llvm_set_value(emitter,
                                        instruction->result,
                                        r_llvm_load_scalar(emitter, instruction->type, slot));
            }
            return true;
        }
    }
    carrier = r_llvm_value(emitter, instruction->operand0);
    if (carrier == NULL) {
        return false;
    }
    if (instruction->kind == R_MIR_INSTRUCTION_EFFECT_TAG) {
        return r_llvm_set_value(
            emitter,
            instruction->result,
            LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), carrier, ""));
    }
    if (!r_llvm_payload_offset(
            emitter, r_llvm_definition(emitter, instruction->operand0)->type, &payload)) {
        return false;
    }
    /* A payload in memory is the carrier's memory, as `carrier.r_payload` is in C17: no copy of
       a large aggregate, so a frame stays within its stack budget (R-FUNC-0004). The carrier
       is written only by the call that defines it, before this instruction, and leaves its
       payload to the value; the address is computed in the entry block, where the carrier's
       slot is, so it holds after a suspension too. */
    if ((r_llvm_scalar_type(emitter, instruction->type) == NULL) &&
        (emitter->values[instruction->result] == NULL)) {
        LLVMValueRef alias = r_llvm_entry_offset(emitter, carrier, payload);
        if (alias != NULL) {
            emitter->values[instruction->result] = alias;
            if (r_llvm_type_requires_drop(emitter, instruction->type)) {
                r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, instruction->result), true);
            }
            r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, instruction->operand0), false);
            return true;
        }
    }
    /* The payload moves out of the carrier, which then has nothing left to drop. */
    if (r_llvm_type_requires_drop(emitter, instruction->type)) {
        LLVMValueRef memory = r_llvm_value_memory(emitter, instruction->result, instruction->type);
        if ((memory == NULL) || !r_llvm_call_move(emitter,
                                                  instruction->type,
                                                  memory,
                                                  r_llvm_byte_offset(emitter, carrier, payload))) {
            return false;
        }
        r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, instruction->result), true);
    } else if (!r_llvm_load_into(emitter,
                                 instruction->result,
                                 instruction->type,
                                 r_llvm_byte_offset(emitter, carrier, payload))) {
        return false;
    }
    r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, instruction->operand0), false);
    return true;
}

/* The slot of a catch payload: the memory of the value for a type in memory, a slot of its own
   for a scalar. */
LLVMValueRef r_llvm_catch_slot(RLlvmEmitter *emitter, RMirValueId id, RTypeId type) {
    uint32_t size = 0U;
    uint32_t align = 0U;

    if ((id == R_MIR_VALUE_ID_INVALID) || ((size_t)id >= emitter->value_count)) {
        (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        return NULL;
    }
    if (r_llvm_scalar_type(emitter, type) == NULL) {
        return r_llvm_value_memory(emitter, id, type);
    }
    if (emitter->catch_slots[id] == NULL) {
        if (!r_llvm_layout(emitter, type, &size, &align)) {
            return NULL;
        }
        emitter->catch_slots[id] = r_llvm_entry_alloca(emitter, size, align, "caught");
    }
    return emitter->catch_slots[id];
}

bool r_llvm_emit_value_instruction(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    switch (instruction->kind) {
    case R_MIR_INSTRUCTION_CONSTANT:
        return r_llvm_emit_constant(emitter, instruction);
    case R_MIR_INSTRUCTION_STRING:
        return r_llvm_emit_string(emitter, instruction);
    case R_MIR_INSTRUCTION_BINARY:
        return r_llvm_emit_binary(emitter, instruction);
    case R_MIR_INSTRUCTION_UNARY:
        return r_llvm_emit_unary(emitter, instruction);
    case R_MIR_INSTRUCTION_CAST:
        return r_llvm_emit_cast(emitter, instruction);
    case R_MIR_INSTRUCTION_LOAD:
        return r_llvm_emit_load(emitter, instruction);
    case R_MIR_INSTRUCTION_MOVE:
        if (r_llvm_staged_move(emitter, instruction)) {
            /* The start that uses it takes the place (async.c, library.c, threads.c). */
            return true;
        }
        return r_llvm_emit_load(emitter, instruction);
    case R_MIR_INSTRUCTION_STORE:
        return r_llvm_emit_store(emitter, instruction);
    case R_MIR_INSTRUCTION_DROP: {
        const RTypeId type = instruction->place_is_parameter
                                 ? emitter->parameter_types[instruction->place_ordinal]
                                 : emitter->local_types[instruction->place_ordinal];
        LLVMValueRef flag =
            r_llvm_place_flag(emitter, instruction->place_ordinal, instruction->place_is_parameter);
        if (instruction->operation == R_TOKEN_KW_VARIANT) {
            /* The payload was moved out; the place holds nothing to drop. */
            r_llvm_set_flag(emitter, flag, false);
            return true;
        }
        /* A user drop may begin a panic, which continues at the panic block (L39). */
        return r_llvm_drop_flagged(emitter,
                                   type,
                                   r_llvm_place_address(emitter,
                                                        instruction->place_ordinal,
                                                        instruction->place_is_parameter,
                                                        R_MIR_VALUE_ID_INVALID),
                                   flag) &&
               r_llvm_unwind_test(emitter, instruction->panic_target);
    }
    case R_MIR_INSTRUCTION_DISCARD: {
        const RMirInstruction *definition = r_llvm_definition(emitter, instruction->operand0);
        if ((definition == NULL) || !r_llvm_type_requires_drop(emitter, definition->type)) {
            return true;
        }
        return r_llvm_drop_flagged(emitter,
                                   definition->type,
                                   r_llvm_value(emitter, instruction->operand0),
                                   r_llvm_value_flag(emitter, instruction->operand0)) &&
               r_llvm_unwind_test(emitter, instruction->panic_target);
    }
    case R_MIR_INSTRUCTION_FIELD:
    case R_MIR_INSTRUCTION_DEREF:
        return true;
    case R_MIR_INSTRUCTION_INDEX:
        return r_llvm_emit_index(emitter, instruction);
    case R_MIR_INSTRUCTION_BORROW:
        return r_llvm_emit_borrow(emitter, instruction);
    case R_MIR_INSTRUCTION_RAW_ADDRESS: {
        /* `&place as T*` (R-EXPR, raw pointers): the address of the place, projection included. */
        LLVMValueRef address = r_llvm_place_address(emitter,
                                                    instruction->place_ordinal,
                                                    instruction->place_is_parameter,
                                                    instruction->operand0);
        return (address != NULL) && r_llvm_set_value(emitter, instruction->result, address);
    }
    case R_MIR_INSTRUCTION_LENGTH:
        return r_llvm_emit_length(emitter, instruction);
    case R_MIR_INSTRUCTION_SLICE:
        return r_llvm_emit_slice(emitter, instruction);
    case R_MIR_INSTRUCTION_NEW:
        return r_llvm_emit_new(emitter, instruction);
    case R_MIR_INSTRUCTION_AGGREGATE:
    case R_MIR_INSTRUCTION_ARRAY:
        return r_llvm_emit_aggregate(emitter, instruction);
    case R_MIR_INSTRUCTION_VARIANT:
        return r_llvm_emit_variant(emitter, instruction);
    case R_MIR_INSTRUCTION_VARIANT_TAG:
        return r_llvm_emit_variant_tag(emitter, instruction);
    case R_MIR_INSTRUCTION_VARIANT_PAYLOAD:
        return r_llvm_emit_variant_payload(emitter, instruction);
    case R_MIR_INSTRUCTION_EFFECT_TAG:
    case R_MIR_INSTRUCTION_EFFECT_PAYLOAD:
        return r_llvm_emit_effect(emitter, instruction);
    case R_MIR_INSTRUCTION_STANDARD_CALL:
        return r_llvm_emit_standard_call(emitter, instruction);
    case R_MIR_INSTRUCTION_INDIRECT_CALL:
        return r_llvm_ffi_indirect_call(emitter, instruction);
    case R_MIR_INSTRUCTION_FUNCTION_ADDRESS:
        /* R-TYPE-0054 (L28): a function value is the number of its target, which its dispatchers
           switch on; a raw fn value is the address of a C function (ffi.c). */
        if (r_llvm_value_kind(emitter, instruction->type) == R_SEMANTIC_TYPE_RAW_FUNCTION) {
            return r_llvm_ffi_function_address(emitter, instruction);
        }
        if (r_llvm_value_kind(emitter, instruction->type) != R_SEMANTIC_TYPE_FUNCTION) {
            return r_llvm_unsupported(emitter, "the address of a C function");
        }
        return r_llvm_set_value(
            emitter, instruction->result, r_llvm_u32(emitter, (uint64_t)instruction->symbol));
    default:
        return r_llvm_unsupported(emitter, r_mir_instruction_kind_name(instruction->kind));
    }
}

#include "standard_internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* Threads of std.thread and the blocking pool of std.async (R-SLIB-THREAD, R-SLIB-ASYNC-0017),
   as the C17 emitter lowers them (r_c17_emit_thread_entry_support, r_c17_emit_async_thread_*).

   A spawn stages its arguments: a value that requires drop by the address of the place it moves
   from, any other value by copy. The library moves the stage into a payload of its own with the
   entry's payload move, runs the entry's trampoline on the new thread and drops the payload with
   the entry's payload drop. The trampoline takes the moved arguments out of the payload, checks
   the stack the entry needs and calls the entry, which writes its carrier or result where the
   library asks. Per entry: payload {argument, flag of an argument that requires drop}..., stage
   {argument or its address}...; an entry without parameters has one byte of each. */

#define R_LLVM_THREAD_PARAMETERS 32U

typedef struct RLlvmThreadLayout {
    uint32_t count;
    RTypeId types[R_LLVM_THREAD_PARAMETERS];
    bool staged[R_LLVM_THREAD_PARAMETERS];
    uint32_t value[R_LLVM_THREAD_PARAMETERS];
    uint32_t flag[R_LLVM_THREAD_PARAMETERS];
    uint32_t stage[R_LLVM_THREAD_PARAMETERS];
    uint32_t payload_size;
    uint32_t payload_align;
    uint32_t stage_size;
    uint32_t stage_align;
} RLlvmThreadLayout;

static uint32_t r_llvm_thread_align_up(uint32_t value, uint32_t align) {
    return ((value + align - 1U) / align) * align;
}

static const RSemanticSymbol *r_llvm_thread_entry(const RLlvmEmitter *emitter, RSymbolId id) {
    return (id == R_SYMBOL_ID_INVALID) || ((size_t)id > emitter->frontend->semantic_symbol_count)
               ? NULL
               : &emitter->frontend->semantic_symbols[(size_t)id - 1U];
}

static bool
r_llvm_thread_layout(RLlvmEmitter *emitter, const RSemanticSymbol *entry, RLlvmThreadLayout *out) {
    uint32_t payload = 0U;
    uint32_t stage = 0U;
    uint32_t index;

    (void)memset(out, 0, sizeof(*out));
    out->payload_align = 1U;
    out->stage_align = 1U;
    if (entry->parameter_count > R_LLVM_THREAD_PARAMETERS) {
        return r_llvm_unsupported(emitter, "a thread entry with this many parameters");
    }
    out->count = entry->parameter_count;
    for (index = 0U; index < entry->parameter_count; ++index) {
        const RTypeId type =
            emitter->frontend->semantic_parameter_types[entry->first_parameter_type + index];
        uint32_t size = 0U;
        uint32_t align = 0U;
        if (!r_llvm_layout(emitter, type, &size, &align)) {
            return false;
        }
        /* r_c17_thread_argument_is_staged: a Copy type never requires drop. */
        out->types[index] = type;
        out->staged[index] = r_llvm_type_requires_drop(emitter, type);
        if (out->staged[index] && (r_llvm_scalar_type(emitter, type) != NULL)) {
            return r_llvm_unsupported(emitter,
                                      "a thread entry taking a scalar that owns resources");
        }
        payload = r_llvm_thread_align_up(payload, align);
        out->value[index] = payload;
        payload += size;
        out->flag[index] = UINT32_MAX;
        if (out->staged[index]) {
            out->flag[index] = payload;
            payload += 1U;
        }
        out->payload_align = align > out->payload_align ? align : out->payload_align;
        if (out->staged[index]) {
            size = 8U;
            align = 8U;
        }
        stage = r_llvm_thread_align_up(stage, align);
        out->stage[index] = stage;
        stage += size;
        out->stage_align = align > out->stage_align ? align : out->stage_align;
    }
    out->payload_size = r_llvm_thread_align_up(payload == 0U ? 1U : payload, out->payload_align);
    out->stage_size = r_llvm_thread_align_up(stage == 0U ? 1U : stage, out->stage_align);
    return true;
}

/* The trampoline, payload move and payload drop of an entry, declared at the first spawn; their
   bodies are written after the functions of the program (r_llvm_emit_thread_entries). */
static bool r_llvm_thread_functions(RLlvmEmitter *emitter, RSymbolId id) {
    LLVMTypeRef parameters[2];
    char name[64];

    if (emitter->thread_entries[id] != NULL) {
        return true;
    }
    if (emitter->functions[id] == NULL) {
        return r_llvm_unsupported(emitter, "a thread entry that is not lowered");
    }
    parameters[0] = r_llvm_pointer(emitter);
    parameters[1] = r_llvm_pointer(emitter);
    (void)snprintf(name, sizeof(name), "r_thread_entry.%" PRIu32, r_llvm_symbol_key(emitter, id));
    emitter->thread_entries[id] = LLVMAddFunction(
        emitter->module,
        name,
        LLVMFunctionType(LLVMVoidTypeInContext(emitter->context), parameters, 2U, 0));
    (void)snprintf(name, sizeof(name), "r_thread_move.%" PRIu32, r_llvm_symbol_key(emitter, id));
    emitter->thread_moves[id] = LLVMAddFunction(
        emitter->module,
        name,
        LLVMFunctionType(LLVMVoidTypeInContext(emitter->context), parameters, 2U, 0));
    (void)snprintf(name, sizeof(name), "r_thread_drop.%" PRIu32, r_llvm_symbol_key(emitter, id));
    emitter->thread_drops[id] = LLVMAddFunction(
        emitter->module,
        name,
        LLVMFunctionType(LLVMVoidTypeInContext(emitter->context), parameters, 1U, 0));
    LLVMSetLinkage(emitter->thread_entries[id], LLVMInternalLinkage);
    LLVMSetLinkage(emitter->thread_moves[id], LLVMInternalLinkage);
    LLVMSetLinkage(emitter->thread_drops[id], LLVMInternalLinkage);
    return true;
}

/* A runtime type info {size, alignment, move, drop} in memory. */
static LLVMValueRef r_llvm_thread_info(
    RLlvmEmitter *emitter, uint64_t size, uint64_t align, LLVMValueRef move, LLVMValueRef drop) {
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
    (void)LLVMBuildStore(
        emitter->builder, r_llvm_u64(emitter, size), r_llvm_byte_offset(emitter, info, offsets[0]));
    (void)LLVMBuildStore(emitter->builder,
                         r_llvm_u64(emitter, align),
                         r_llvm_byte_offset(emitter, info, offsets[1]));
    (void)LLVMBuildStore(emitter->builder,
                         move == NULL ? LLVMConstNull(r_llvm_pointer(emitter)) : move,
                         r_llvm_byte_offset(emitter, info, offsets[2]));
    (void)LLVMBuildStore(emitter->builder,
                         drop == NULL ? LLVMConstNull(r_llvm_pointer(emitter)) : drop,
                         r_llvm_byte_offset(emitter, info, offsets[3]));
    return info;
}

/* The info of what the entry writes: its carrier, its result or nothing. */
static LLVMValueRef r_llvm_thread_result_info(RLlvmEmitter *emitter, const RSemanticSymbol *entry) {
    const RTypeId storage = entry->effect_carrier_type != R_TYPE_ID_INVALID
                                ? entry->effect_carrier_type
                                : entry->return_type;

    if (r_llvm_type_is_void(emitter, storage)) {
        return r_llvm_thread_info(emitter, 0U, 1U, NULL, NULL);
    }
    return r_llvm_type_info(emitter, storage);
}

/* RStdThreadCompletionTypeInfo of a spawn: the result info and, for a checked entry, where its
   carrier keeps the tag and the payload and how many errors it has. */
static LLVMValueRef r_llvm_thread_completion(RLlvmEmitter *emitter, const RSemanticSymbol *entry) {
    const bool checked = entry->effect_carrier_type != R_TYPE_ID_INVALID;
    LLVMValueRef storage = r_llvm_thread_result_info(emitter, entry);
    LLVMValueRef completion;
    uint32_t size = 0U;
    uint32_t align = 0U;
    uint32_t info_size = 0U;
    uint32_t info_align = 0U;
    uint32_t storage_offset = 0U;
    uint32_t tag_offset = 0U;
    uint32_t payload_offset = 0U;
    uint32_t count_offset = 0U;
    uint32_t payload = 0U;

    if ((storage == NULL) ||
        !r_llvm_runtime_layout(emitter, "RStdThreadCompletionTypeInfo", &size, &align) ||
        !r_llvm_runtime_layout(emitter, "RRuntimeTypeInfo", &info_size, &info_align) ||
        !r_llvm_runtime_field(
            emitter, "RStdThreadCompletionTypeInfo", "storage_type", &storage_offset, NULL) ||
        !r_llvm_runtime_field(
            emitter, "RStdThreadCompletionTypeInfo", "tag_offset", &tag_offset, NULL) ||
        !r_llvm_runtime_field(
            emitter, "RStdThreadCompletionTypeInfo", "payload_offset", &payload_offset, NULL) ||
        !r_llvm_runtime_field(
            emitter, "RStdThreadCompletionTypeInfo", "error_count", &count_offset, NULL) ||
        (checked && !r_llvm_payload_offset(emitter, entry->effect_carrier_type, &payload))) {
        return NULL;
    }
    completion = r_llvm_entry_alloca(emitter, size, align, "completion");
    (void)LLVMBuildMemSet(emitter->builder,
                          completion,
                          LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                          r_llvm_u64(emitter, size),
                          align);
    (void)LLVMBuildMemCpy(emitter->builder,
                          r_llvm_byte_offset(emitter, completion, storage_offset),
                          info_align,
                          storage,
                          info_align,
                          r_llvm_u64(emitter, info_size));
    if (checked) {
        (void)LLVMBuildStore(emitter->builder,
                             r_llvm_u64(emitter, payload),
                             r_llvm_byte_offset(emitter, completion, payload_offset));
        (void)LLVMBuildStore(
            emitter->builder,
            r_llvm_u32(emitter, r_semantic_effect_count(emitter->frontend, entry->throws_type)),
            r_llvm_byte_offset(emitter, completion, count_offset));
    }
    (void)tag_offset;
    return completion;
}

/* std.thread::spawn, std.thread::spawn_scoped and std.async::blocking
   (r_c17_emit_async_thread_spawn): the stage, the start and the carrier of its handle or task. */
bool r_llvm_thread_spawn(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const bool scoped = instruction->standard_operation == R_STANDARD_CALL_THREAD_SPAWN_SCOPED;
    const bool blocking = instruction->standard_operation == R_STANDARD_CALL_ASYNC_BLOCKING;
    const RSemanticSymbol *entry = r_llvm_thread_entry(emitter, instruction->symbol);
    const char *native_type = blocking ? "RStdAsyncStartResult"
                              : scoped ? "RStdThreadScopedSpawnResult"
                                       : "RStdThreadSpawnResult";
    RLlvmThreadLayout layout;
    LLVMValueRef consumed[R_LLVM_THREAD_PARAMETERS];
    LLVMValueRef stage;
    LLVMValueRef native;
    LLVMValueRef arguments[5];
    LLVMValueRef is_ok;
    LLVMValueRef started;
    uint32_t index;
    unsigned count = 0U;

    if ((entry == NULL) || (instruction->operand_count != entry->parameter_count)) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    if (!r_llvm_thread_layout(emitter, entry, &layout) ||
        !r_llvm_thread_functions(emitter, instruction->symbol)) {
        return false;
    }
    stage = r_llvm_entry_alloca(emitter, layout.stage_size, layout.stage_align, "stage");
    for (index = 0U; index < layout.count; ++index) {
        const RMirValueId argument = r_llvm_std_operand(emitter, instruction, index);
        const RMirInstruction *definition = r_llvm_definition(emitter, argument);
        LLVMValueRef slot = r_llvm_byte_offset(emitter, stage, layout.stage[index]);
        consumed[index] = NULL;
        if (definition == NULL) {
            return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
        if (layout.staged[index]) {
            /* The address of the storage the payload moves from, and its flag. */
            LLVMValueRef address;
            if (r_llvm_staged_move(emitter, definition)) {
                if (definition->operand1 != R_MIR_VALUE_ID_INVALID) {
                    return r_llvm_unsupported(emitter, "a thread argument from a projected place");
                }
                address = r_llvm_place_address(emitter,
                                               definition->place_ordinal,
                                               definition->place_is_parameter,
                                               R_MIR_VALUE_ID_INVALID);
                consumed[index] = r_llvm_place_flag(
                    emitter, definition->place_ordinal, definition->place_is_parameter);
            } else {
                address = r_llvm_value(emitter, argument);
                consumed[index] = r_llvm_value_flag(emitter, argument);
            }
            if (address == NULL) {
                return false;
            }
            (void)LLVMBuildStore(emitter->builder, address, slot);
        } else {
            LLVMValueRef value = r_llvm_value(emitter, argument);
            if ((value == NULL) || !r_llvm_store_value(emitter, layout.types[index], value, slot)) {
                return false;
            }
        }
    }
    native = r_llvm_std_structure(emitter, native_type);
    if (native == NULL) {
        return false;
    }
    if (!blocking) {
        arguments[count++] = r_llvm_std_allocator(emitter);
    }
    arguments[count++] = r_llvm_thread_info(emitter,
                                            layout.payload_size,
                                            layout.payload_align,
                                            emitter->thread_moves[instruction->symbol],
                                            emitter->thread_drops[instruction->symbol]);
    arguments[count++] = blocking ? r_llvm_thread_result_info(emitter, entry)
                                  : r_llvm_thread_completion(emitter, entry);
    arguments[count++] = emitter->thread_entries[instruction->symbol];
    arguments[count++] = stage;
    for (index = 0U; index < count; ++index) {
        if (arguments[index] == NULL) {
            return false;
        }
    }
    if (r_llvm_call_runtime(emitter,
                            blocking ? "r_std_async_blocking"
                            : scoped ? "r_library_internal_thread_spawn_scoped_checked"
                                     : "r_library_internal_thread_spawn_checked",
                            arguments,
                            count,
                            native) == NULL) {
        return false;
    }
    is_ok = r_llvm_std_member(emitter, native, native_type, "is_ok");
    if (is_ok == NULL) {
        return false;
    }
    started = LLVMBuildICmp(emitter->builder,
                            LLVMIntNE,
                            LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 8U), is_ok, ""),
                            LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                            "");
    /* The thread owns what it moved from the staged places. */
    for (index = 0U; index < layout.count; ++index) {
        if (consumed[index] != NULL) {
            (void)LLVMBuildStore(
                emitter->builder,
                LLVMBuildSelect(
                    emitter->builder,
                    started,
                    LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                    LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 8U), consumed[index], ""),
                    ""),
                consumed[index]);
        }
    }
    return r_llvm_std_carrier(
        emitter,
        instruction,
        started,
        r_llvm_std_member(emitter, native, native_type, blocking ? "task" : "value"),
        r_llvm_std_member(emitter, native, native_type, "error"));
}

/* ---- std.thread::join_result<T> ---- */

bool r_llvm_thread_handle(RLlvmEmitter *emitter, RTypeId id) {
    const RSemanticType *type = r_llvm_type(emitter, r_llvm_value_type(emitter, id));

    return (type != NULL) && (type->kind == R_SEMANTIC_TYPE_STANDARD) &&
           (type->flags == R_SEMANTIC_TYPE_FLAG_NONE) && (type->base != R_TYPE_ID_INVALID) &&
           (r_llvm_standard_named(emitter, id, "std.thread::join_handle") ||
            r_llvm_standard_named(emitter, id, "std.thread::scoped_join_handle"));
}

bool r_llvm_thread_join_result(RLlvmEmitter *emitter, RTypeId id) {
    const RSemanticType *type = r_llvm_type(emitter, r_llvm_value_type(emitter, id));

    return (type != NULL) && (type->kind == R_SEMANTIC_TYPE_STANDARD) &&
           (type->flags == R_SEMANTIC_TYPE_FLAG_NONE) && (type->base != R_TYPE_ID_INVALID) &&
           (type->second == R_TYPE_ID_INVALID) &&
           r_llvm_standard_named(emitter, id, "std.thread::join_result");
}

/* The type std.thread::panic_report of the program, or none when the program never names it. */
RTypeId r_llvm_thread_panic_report(RLlvmEmitter *emitter) {
    size_t index;

    for (index = 0U; index < emitter->frontend->semantic_type_count; ++index) {
        const RSemanticType *type = &emitter->frontend->semantic_types[index];
        if ((type->kind == R_SEMANTIC_TYPE_STANDARD) && (type->base == R_TYPE_ID_INVALID) &&
            r_llvm_standard_named(emitter, (RTypeId)(index + 1U), "std.thread::panic_report")) {
            return (RTypeId)(index + 1U);
        }
    }
    return R_TYPE_ID_INVALID;
}

/* r_c17_emit_type_glue_thread_join_result_definition: returned moves or drops its value, panicked
   its report; the source of a move and a dropped value hold no variant afterwards. */
bool r_llvm_thread_join_glue(
    RLlvmEmitter *emitter, RTypeId id, bool move, LLVMValueRef first, LLVMValueRef second) {
    const RSemanticType *type = r_llvm_type(emitter, r_llvm_value_type(emitter, id));
    LLVMValueRef tagged = move ? second : first;
    LLVMBasicBlockRef returned =
        LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef panicked =
        LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMValueRef choice;
    uint32_t payload = 0U;
    uint32_t report_size = 0U;
    uint32_t report_align = 0U;

    if ((type == NULL) || !r_llvm_payload_offset(emitter, id, &payload) ||
        !r_llvm_runtime_layout(emitter, "RStdThreadPanicReport", &report_size, &report_align)) {
        return false;
    }
    choice = LLVMBuildSwitch(emitter->builder,
                             LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), tagged, ""),
                             done,
                             2U);
    LLVMAddCase(choice, r_llvm_u32(emitter, 0U), returned);
    LLVMAddCase(choice, r_llvm_u32(emitter, 1U), panicked);
    LLVMPositionBuilderAtEnd(emitter->builder, returned);
    if (!r_llvm_type_is_void(emitter, type->base)) {
        if (move) {
            if (r_llvm_type_requires_drop(emitter, type->base)
                    ? !r_llvm_call_move(emitter,
                                        type->base,
                                        r_llvm_byte_offset(emitter, first, payload),
                                        r_llvm_byte_offset(emitter, second, payload))
                    : !r_llvm_std_copy(emitter,
                                       type->base,
                                       r_llvm_byte_offset(emitter, first, payload),
                                       r_llvm_byte_offset(emitter, second, payload))) {
                return false;
            }
        } else if (r_llvm_type_requires_drop(emitter, type->base) &&
                   !r_llvm_call_drop(
                       emitter, type->base, r_llvm_byte_offset(emitter, first, payload))) {
            return false;
        }
    }
    (void)LLVMBuildBr(emitter->builder, done);
    LLVMPositionBuilderAtEnd(emitter->builder, panicked);
    if (move) {
        (void)LLVMBuildMemCpy(emitter->builder,
                              r_llvm_byte_offset(emitter, first, payload),
                              report_align,
                              r_llvm_byte_offset(emitter, second, payload),
                              report_align,
                              r_llvm_u64(emitter, report_size));
        (void)LLVMBuildMemSet(emitter->builder,
                              r_llvm_byte_offset(emitter, second, payload),
                              LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                              r_llvm_u64(emitter, report_size),
                              report_align);
    } else {
        LLVMValueRef report = r_llvm_byte_offset(emitter, first, payload);
        if (r_llvm_call_runtime(emitter, "r_std_thread_panic_report_destroy", &report, 1U, NULL) ==
            NULL) {
            return false;
        }
    }
    (void)LLVMBuildBr(emitter->builder, done);
    LLVMPositionBuilderAtEnd(emitter->builder, done);
    if (move) {
        (void)LLVMBuildStore(emitter->builder,
                             LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), second, ""),
                             first);
    }
    (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, UINT32_MAX), tagged);
    return true;
}

/* A value moves between memories by its glue when it requires drop, bitwise otherwise. */
static bool
r_llvm_thread_transfer(RLlvmEmitter *emitter, RTypeId type, LLVMValueRef to, LLVMValueRef from) {
    return r_llvm_type_requires_drop(emitter, type) ? r_llvm_call_move(emitter, type, to, from)
                                                    : r_llvm_std_copy(emitter, type, to, from);
}

/* std.thread::join (r_c17_emit_async_thread_join): the native join result becomes a join_result,
   inside the carrier of a checked entry whose errors are passed on. A panicked thread hands over
   its report; any other kind than the expected one breaks the contract. */
bool r_llvm_thread_join(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RMirValueId argument = r_llvm_std_operand(emitter, instruction, 0U);
    const bool fallible = instruction->integer_value != 0U;
    const RTypeId join_type = fallible ? instruction->auxiliary_type : instruction->type;
    const RSemanticType *join = r_llvm_type(emitter, r_llvm_value_type(emitter, join_type));
    const RSemanticType *completion_type =
        r_llvm_type(emitter, r_llvm_value_type(emitter, instruction->runtime_type));
    const bool has_returned = (join != NULL) && !r_llvm_type_is_void(emitter, join->base);
    LLVMValueRef handle = r_llvm_value(emitter, argument);
    LLVMValueRef native = r_llvm_std_structure(emitter, "RStdThreadJoinResult");
    LLVMValueRef result;
    LLVMValueRef joined;
    LLVMValueRef panic_member;
    LLVMValueRef kind;
    LLVMBasicBlockRef panicked;
    LLVMBasicBlockRef finished;
    LLVMBasicBlockRef unexpected;
    LLVMBasicBlockRef done;
    uint32_t join_payload = 0U;
    uint32_t outer_payload = 0U;
    uint32_t report_size = 0U;
    uint32_t report_align = 0U;
    int64_t panicked_kind = 0;
    int64_t expected_kind = 0;

    if ((join == NULL) || !r_llvm_thread_join_result(emitter, join_type)) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    if ((handle == NULL) || (native == NULL) ||
        !r_llvm_payload_offset(emitter, join_type, &join_payload) ||
        (fallible && !r_llvm_payload_offset(emitter, instruction->type, &outer_payload)) ||
        !r_llvm_runtime_layout(emitter, "RStdThreadPanicReport", &report_size, &report_align) ||
        !r_llvm_runtime_constant(emitter, "R_STD_THREAD_JOIN_PANICKED", &panicked_kind) ||
        !r_llvm_runtime_constant(emitter,
                                 (fallible || has_returned) ? "R_STD_THREAD_JOIN_RETURNED"
                                                            : "R_STD_THREAD_JOIN_COMPLETED",
                                 &expected_kind)) {
        return false;
    }
    if (r_llvm_call_runtime(emitter, "r_std_thread_join", &handle, 1U, native) == NULL) {
        return false;
    }
    r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, argument), false);
    result = r_llvm_std_result(emitter, instruction);
    if (result == NULL) {
        return false;
    }
    joined = fallible ? r_llvm_byte_offset(emitter, result, outer_payload) : result;
    kind = LLVMBuildLoad2(emitter->builder,
                          r_llvm_int(emitter, 32U),
                          r_llvm_std_member(emitter, native, "RStdThreadJoinResult", "kind"),
                          "");
    panicked = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    finished = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    unexpected = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    done = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    {
        LLVMValueRef choice = LLVMBuildSwitch(emitter->builder, kind, unexpected, 2U);
        LLVMAddCase(choice, r_llvm_u32(emitter, (uint64_t)panicked_kind), panicked);
        LLVMAddCase(choice, r_llvm_u32(emitter, (uint64_t)expected_kind), finished);
    }
    /* panicked(report): the report moves out of the native result, which is then destroyed. */
    LLVMPositionBuilderAtEnd(emitter->builder, panicked);
    panic_member = r_llvm_std_member(emitter, native, "RStdThreadJoinResult", "panic");
    if (panic_member == NULL) {
        return false;
    }
    (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 1U), joined);
    (void)LLVMBuildMemCpy(emitter->builder,
                          r_llvm_byte_offset(emitter, joined, join_payload),
                          report_align,
                          panic_member,
                          report_align,
                          r_llvm_u64(emitter, report_size));
    (void)LLVMBuildMemSet(emitter->builder,
                          panic_member,
                          LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                          r_llvm_u64(emitter, report_size),
                          report_align);
    if (r_llvm_call_runtime(emitter, "r_std_thread_join_result_destroy", &native, 1U, NULL) ==
        NULL) {
        return false;
    }
    (void)LLVMBuildBr(emitter->builder, done);
    LLVMPositionBuilderAtEnd(emitter->builder, unexpected);
    if (!r_llvm_panic(emitter,
                      "R_RUNTIME_PANIC_CONTRACT_VIOLATION",
                      instruction->span,
                      R_MIR_BLOCK_ID_INVALID)) {
        return false;
    }
    LLVMPositionBuilderAtEnd(emitter->builder, finished);
    if (!fallible) {
        if (has_returned) {
            LLVMValueRef arguments[2];
            arguments[0] = native;
            arguments[1] = r_llvm_byte_offset(emitter, result, join_payload);
            if (r_llvm_call_runtime(
                    emitter, "r_library_internal_thread_join_result_move", arguments, 2U, NULL) ==
                NULL) {
                return false;
            }
        }
        (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 0U), result);
        (void)LLVMBuildBr(emitter->builder, done);
    } else {
        /* The completion carrier of the entry: success becomes returned, an error passes on. */
        const uint32_t errors =
            completion_type == NULL
                ? 0U
                : r_semantic_effect_count(emitter->frontend, completion_type->second);
        uint32_t completion_size = 0U;
        uint32_t completion_align = 0U;
        uint32_t completion_payload = 0U;
        LLVMValueRef completion;
        LLVMValueRef arguments[2];
        LLVMValueRef choice;
        LLVMBasicBlockRef succeeded;
        LLVMBasicBlockRef invalid;
        uint32_t error;
        if ((completion_type == NULL) ||
            (completion_type->kind != R_SEMANTIC_TYPE_EFFECT_CARRIER) || (errors == 0U) ||
            !r_llvm_layout(
                emitter, instruction->runtime_type, &completion_size, &completion_align) ||
            !r_llvm_payload_offset(emitter, instruction->runtime_type, &completion_payload)) {
            return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
        completion = r_llvm_entry_alloca(emitter, completion_size, completion_align, "completion");
        (void)LLVMBuildMemSet(emitter->builder,
                              completion,
                              LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                              r_llvm_u64(emitter, completion_size),
                              completion_align);
        arguments[0] = native;
        arguments[1] = completion;
        if (r_llvm_call_runtime(
                emitter, "r_library_internal_thread_join_result_move", arguments, 2U, NULL) ==
            NULL) {
            return false;
        }
        succeeded = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        invalid = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        choice = LLVMBuildSwitch(
            emitter->builder,
            LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), completion, ""),
            invalid,
            errors + 1U);
        LLVMAddCase(choice, r_llvm_u32(emitter, 0U), succeeded);
        LLVMPositionBuilderAtEnd(emitter->builder, succeeded);
        if (has_returned &&
            !r_llvm_thread_transfer(emitter,
                                    join->base,
                                    r_llvm_byte_offset(emitter, joined, join_payload),
                                    r_llvm_byte_offset(emitter, completion, completion_payload))) {
            return false;
        }
        (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 0U), joined);
        (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 0U), result);
        (void)LLVMBuildBr(emitter->builder, done);
        for (error = 0U; error < errors; ++error) {
            const RTypeId error_type =
                r_semantic_effect_at(emitter->frontend, completion_type->second, error);
            LLVMBasicBlockRef failed =
                LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
            LLVMAddCase(choice, r_llvm_u32(emitter, error + 1U), failed);
            LLVMPositionBuilderAtEnd(emitter->builder, failed);
            if (!r_llvm_thread_transfer(
                    emitter,
                    error_type,
                    r_llvm_byte_offset(emitter, result, outer_payload),
                    r_llvm_byte_offset(emitter, completion, completion_payload))) {
                return false;
            }
            (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, error + 1U), result);
            (void)LLVMBuildBr(emitter->builder, done);
        }
        LLVMPositionBuilderAtEnd(emitter->builder, invalid);
        if (!r_llvm_panic(emitter,
                          "R_RUNTIME_PANIC_CONTRACT_VIOLATION",
                          instruction->span,
                          R_MIR_BLOCK_ID_INVALID)) {
            return false;
        }
    }
    LLVMPositionBuilderAtEnd(emitter->builder, done);
    r_llvm_std_initialized(emitter, instruction);
    return true;
}

/* ---- Entry trampolines (r_c17_emit_thread_entry_definition) ---- */

static bool r_llvm_thread_begin(RLlvmEmitter *emitter, LLVMValueRef function) {
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(emitter->context, function, "entry");
    LLVMBasicBlockRef body = LLVMAppendBasicBlockInContext(emitter->context, function, "");

    emitter->function = function;
    emitter->mir = NULL;
    emitter->frame = NULL;
    LLVMPositionBuilderAtEnd(emitter->builder, entry);
    (void)LLVMBuildBr(emitter->builder, body);
    LLVMPositionBuilderAtEnd(emitter->builder, body);
    return true;
}

static bool r_llvm_thread_entry_bodies(RLlvmEmitter *emitter, RSymbolId id) {
    const RSemanticSymbol *entry = r_llvm_thread_entry(emitter, id);
    const bool checked = (entry != NULL) && (entry->effect_carrier_type != R_TYPE_ID_INVALID);
    const bool in_memory =
        (entry != NULL) && (checked || (!r_llvm_type_is_void(emitter, entry->return_type) &&
                                        (r_llvm_scalar_type(emitter, entry->return_type) == NULL)));
    RLlvmThreadLayout layout;
    LLVMValueRef arguments[R_LLVM_THREAD_PARAMETERS + 1U];
    LLVMValueRef payload;
    LLVMValueRef source;
    LLVMValueRef call;
    LLVMValueRef stack[2];
    unsigned count = 0U;
    uint32_t index;

    if ((entry == NULL) || !r_llvm_thread_layout(emitter, entry, &layout)) {
        return entry == NULL ? r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR) : false;
    }
    /* The payload move takes each argument from the stage. */
    r_llvm_thread_begin(emitter, emitter->thread_moves[id]);
    payload = LLVMGetParam(emitter->function, 0U);
    source = LLVMGetParam(emitter->function, 1U);
    for (index = 0U; index < layout.count; ++index) {
        LLVMValueRef to = r_llvm_byte_offset(emitter, payload, layout.value[index]);
        LLVMValueRef from = r_llvm_byte_offset(emitter, source, layout.stage[index]);
        if (layout.staged[index]) {
            from = LLVMBuildLoad2(emitter->builder, r_llvm_pointer(emitter), from, "");
            if (!r_llvm_call_move(emitter, layout.types[index], to, from)) {
                return false;
            }
            (void)LLVMBuildStore(emitter->builder,
                                 LLVMConstInt(r_llvm_int(emitter, 8U), 1U, 0),
                                 r_llvm_byte_offset(emitter, payload, layout.flag[index]));
        } else if (!r_llvm_std_copy(emitter, layout.types[index], to, from)) {
            return false;
        }
    }
    (void)LLVMBuildRetVoid(emitter->builder);
    /* The payload drop: what the trampoline did not take, last argument first. */
    r_llvm_thread_begin(emitter, emitter->thread_drops[id]);
    payload = LLVMGetParam(emitter->function, 0U);
    index = layout.count;
    while (index != 0U) {
        LLVMValueRef flag;
        LLVMBasicBlockRef owned;
        LLVMBasicBlockRef next;
        index -= 1U;
        if (!layout.staged[index]) {
            continue;
        }
        flag = r_llvm_byte_offset(emitter, payload, layout.flag[index]);
        owned = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        next = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        (void)LLVMBuildCondBr(
            emitter->builder,
            LLVMBuildICmp(emitter->builder,
                          LLVMIntNE,
                          LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 8U), flag, ""),
                          LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                          ""),
            owned,
            next);
        LLVMPositionBuilderAtEnd(emitter->builder, owned);
        if (!r_llvm_call_drop(emitter,
                              layout.types[index],
                              r_llvm_byte_offset(emitter, payload, layout.value[index]))) {
            return false;
        }
        (void)LLVMBuildStore(emitter->builder, LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0), flag);
        (void)LLVMBuildBr(emitter->builder, next);
        LLVMPositionBuilderAtEnd(emitter->builder, next);
    }
    (void)LLVMBuildRetVoid(emitter->builder);
    /* The trampoline: arguments out of the payload, the stack check, the call. */
    r_llvm_thread_begin(emitter, emitter->thread_entries[id]);
    payload = LLVMGetParam(emitter->function, 0U);
    if (in_memory) {
        arguments[count++] = LLVMGetParam(emitter->function, 1U);
    }
    for (index = 0U; index < layout.count; ++index) {
        const RTypeId type = layout.types[index];
        LLVMValueRef value = r_llvm_byte_offset(emitter, payload, layout.value[index]);
        if (r_llvm_scalar_type(emitter, type) != NULL) {
            arguments[count++] = r_llvm_load_scalar(emitter, type, value);
            continue;
        }
        {
            /* The entry owns a copy of an argument in memory; a moved one leaves the payload. */
            uint32_t size = 0U;
            uint32_t align = 0U;
            LLVMValueRef copy;
            if (!r_llvm_layout(emitter, type, &size, &align)) {
                return false;
            }
            copy = r_llvm_entry_alloca(emitter, size, align, "argument");
            if (layout.staged[index]) {
                if (!r_llvm_call_move(emitter, type, copy, value)) {
                    return false;
                }
                (void)LLVMBuildStore(emitter->builder,
                                     LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                                     r_llvm_byte_offset(emitter, payload, layout.flag[index]));
            } else {
                (void)LLVMBuildMemCpy(
                    emitter->builder, copy, align, value, align, r_llvm_u64(emitter, size));
            }
            arguments[count++] = copy;
        }
    }
    stack[0] = r_llvm_stack_entry(emitter, id);
    stack[1] = r_llvm_span(emitter, entry->name_span);
    if ((stack[0] == NULL) || (stack[1] == NULL) ||
        (r_llvm_call_runtime(emitter, "r_runtime_stack_require", stack, 2U, NULL) == NULL)) {
        return false;
    }
    call = LLVMBuildCall2(emitter->builder,
                          emitter->function_types[id],
                          emitter->functions[id],
                          arguments,
                          count,
                          "");
    if (!in_memory && !r_llvm_type_is_void(emitter, entry->return_type)) {
        r_llvm_store_scalar(emitter, entry->return_type, call, LLVMGetParam(emitter->function, 1U));
    }
    (void)LLVMBuildRetVoid(emitter->builder);
    return true;
}

/* The bodies of every entry a spawn declared. */
bool r_llvm_emit_thread_entries(RLlvmEmitter *emitter) {
    size_t id;

    for (id = 1U; id <= emitter->frontend->semantic_symbol_count; ++id) {
        if ((emitter->thread_entries[id] != NULL) &&
            !r_llvm_thread_entry_bodies(emitter, (RSymbolId)id)) {
            return false;
        }
    }
    return true;
}

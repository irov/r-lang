#include "emit_internal.h"

#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>
#include <llvm-c/Error.h>
#include <llvm-c/Support.h>
#include <llvm-c/Transforms/PassBuilder.h>

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* The LLVM emitter (B4 of the LLVM transition). A program is lowered from MIR: every function
   with a definition becomes an LLVM function, its parameters and locals live in stack slots that
   mem2reg promotes, values are lowered by values.c, and the hosted `main` of hosted_main.c calls
   the entry. The emitter grows construct by construct; a program it cannot lower yet is
   R_FRONTEND_NOT_LOWERABLE, and the first construct it misses is named on stderr. */

static const RSemanticSymbol *r_llvm_semantic_symbol(const RLlvmEmitter *emitter, RSymbolId id) {
    return ((id == R_SYMBOL_ID_INVALID) || ((size_t)id > emitter->frontend->semantic_symbol_count))
               ? NULL
               : &emitter->frontend->semantic_symbols[(size_t)id - 1U];
}

/* ---- Functions ---- */

/* The calling convention between R functions of the program: a scalar parameter in its own type,
   a parameter in memory as the address of a copy the callee owns; a carrier or a result in memory
   is written to memory the caller passes first. Functions that C calls keep the C ABI (B5). */
static bool
r_llvm_function_name(RLlvmEmitter *emitter, RSymbolId id, char *buffer, size_t capacity) {
    const RSemanticSymbol *symbol = r_llvm_semantic_symbol(emitter, id);
    const RSource *source;
    const RInternEntry *name;
    int written;

    if ((symbol == NULL) || (symbol->name_intern_id == 0U) ||
        ((size_t)symbol->name_intern_id > emitter->frontend->intern_count)) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    source = r_get_source_const(emitter->frontend, symbol->module_source);
    name = &emitter->frontend->intern_entries[(size_t)symbol->name_intern_id - 1U];
    written =
        snprintf(buffer,
                 capacity,
                 "%s::%.*s",
                 (source != NULL) && (source->module_name != NULL) ? source->module_name : "?",
                 (int)name->length,
                 name->bytes);
    return ((written > 0) && ((size_t)written < capacity)) ||
           r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
}

/* Whether the result of the function is written to memory its caller passes. */
static bool r_llvm_result_in_memory(RLlvmEmitter *emitter, const RSemanticSymbol *symbol) {
    return (symbol->effect_carrier_type != R_TYPE_ID_INVALID) ||
           (!r_llvm_type_is_void(emitter, symbol->return_type) &&
            (r_llvm_scalar_type(emitter, symbol->return_type) == NULL));
}

/* Functions whose MIR body is a placeholder because the backend writes their body from a recipe:
   interface and function-value dispatchers, JSON operations and format recipes (the C17 emitter's
   r_c17_emit_function_definition). They are glue of B5.2; the name says which, or NULL. */
static const char *r_llvm_synthesized_body(RLlvmEmitter *emitter, RSymbolId id) {
    const RSemanticSymbol *symbol = r_llvm_semantic_symbol(emitter, id);
    const RSource *source =
        symbol == NULL ? NULL : r_get_source_const(emitter->frontend, symbol->name_span.source);

    /* Dispatchers are written by dispatch.c and format recipes by format.c, whether or not the
       function has source. */
    if ((symbol != NULL) && (r_semantic_dyn_dispatcher(emitter->frontend, id) ||
                             r_semantic_function_value_dispatcher(emitter->frontend, id) ||
                             (symbol->format_recipe != 0U))) {
        return NULL;
    }
    if ((symbol != NULL) && (symbol->json_operation != 0U)) {
        return r_llvm_json_supported(symbol->json_operation) ? NULL : "a JSON operation";
    }
    if ((symbol == NULL) || (source == NULL) || ((size_t)symbol->name_span.end > source->length)) {
        return "a function without source";
    }
    return NULL;
}

/* An async function is its step `RRuntimeTaskStepStatus (execution, payload, result)` over a
   frame in the task payload, with the initializer `(payload, context)` and the drop `(payload)`
   of that frame and the size of the frame (async.c, emit.c frame mode). */
static bool
r_llvm_declare_async_function(RLlvmEmitter *emitter, RSymbolId id, const RSemanticSymbol *symbol) {
    LLVMTypeRef parameters[3];
    char name[512];
    char derived[600];

    if (symbol->is_import || symbol->is_extern_c || symbol->is_callback ||
        (r_llvm_synthesized_body(emitter, id) != NULL)) {
        return r_llvm_unsupported(emitter, "an imported or synthesized async function");
    }
    if (!r_llvm_function_name(emitter, id, name, sizeof(name))) {
        return false;
    }
    parameters[0] = r_llvm_pointer(emitter);
    parameters[1] = r_llvm_pointer(emitter);
    parameters[2] = r_llvm_pointer(emitter);
    emitter->function_types[id] = LLVMFunctionType(r_llvm_int(emitter, 32U), parameters, 3U, 0);
    (void)snprintf(derived, sizeof(derived), "%s$step", name);
    emitter->functions[id] = LLVMAddFunction(emitter->module, derived, emitter->function_types[id]);
    LLVMSetLinkage(emitter->functions[id], LLVMInternalLinkage);
    (void)snprintf(derived, sizeof(derived), "%s$initialize", name);
    emitter->async_initializers[id] = LLVMAddFunction(
        emitter->module,
        derived,
        LLVMFunctionType(LLVMVoidTypeInContext(emitter->context), parameters, 2U, 0));
    LLVMSetLinkage(emitter->async_initializers[id], LLVMInternalLinkage);
    (void)snprintf(derived, sizeof(derived), "%s$drop", name);
    emitter->async_drops[id] = LLVMAddFunction(
        emitter->module,
        derived,
        LLVMFunctionType(LLVMVoidTypeInContext(emitter->context), parameters, 1U, 0));
    LLVMSetLinkage(emitter->async_drops[id], LLVMInternalLinkage);
    (void)snprintf(derived, sizeof(derived), "%s$frame_size", name);
    emitter->async_frame_sizes[id] =
        LLVMAddGlobal(emitter->module, r_llvm_int(emitter, 64U), derived);
    LLVMSetLinkage(emitter->async_frame_sizes[id], LLVMPrivateLinkage);
    LLVMSetGlobalConstant(emitter->async_frame_sizes[id], 1);
    LLVMSetInitializer(emitter->async_frame_sizes[id], r_llvm_u64(emitter, 0U));
    return true;
}

/* The type of an ordinary function of the program (the convention above). */
static LLVMTypeRef r_llvm_sync_signature(RLlvmEmitter *emitter, const RSemanticSymbol *symbol) {
    LLVMTypeRef *parameters;
    LLVMTypeRef result;
    LLVMTypeRef type;
    unsigned count = 0U;
    uint32_t index;

    parameters =
        r_llvm_allocate(emitter, ((size_t)symbol->parameter_count + 1U) * sizeof(*parameters));
    if (parameters == NULL) {
        return NULL;
    }
    if (r_llvm_result_in_memory(emitter, symbol)) {
        uint32_t size = 0U;
        uint32_t align = 0U;
        if (!r_llvm_layout(emitter,
                           symbol->effect_carrier_type != R_TYPE_ID_INVALID
                               ? symbol->effect_carrier_type
                               : symbol->return_type,
                           &size,
                           &align)) {
            r_llvm_free(emitter, parameters);
            return NULL;
        }
        parameters[count++] = r_llvm_pointer(emitter);
        result = LLVMVoidTypeInContext(emitter->context);
    } else if (r_llvm_type_is_void(emitter, symbol->return_type)) {
        result = LLVMVoidTypeInContext(emitter->context);
    } else {
        result = r_llvm_scalar_type(emitter, symbol->return_type);
    }
    for (index = 0U; index < symbol->parameter_count; ++index) {
        const RTypeId parameter =
            emitter->frontend->semantic_parameter_types[symbol->first_parameter_type + index];
        LLVMTypeRef scalar = r_llvm_scalar_type(emitter, parameter);
        if (scalar == NULL) {
            uint32_t size = 0U;
            uint32_t align = 0U;
            if (!r_llvm_layout(emitter, parameter, &size, &align)) {
                r_llvm_free(emitter, parameters);
                return NULL;
            }
            scalar = r_llvm_pointer(emitter);
        }
        parameters[count++] = scalar;
    }
    type = LLVMFunctionType(result, parameters, count, 0);
    r_llvm_free(emitter, parameters);
    return type;
}

static void r_llvm_enum_attribute(RLlvmEmitter *emitter,
                                  LLVMValueRef function,
                                  unsigned position,
                                  const char *name,
                                  uint64_t value) {
    LLVMAddAttributeAtIndex(
        function,
        position,
        LLVMCreateEnumAttribute(
            emitter->context, LLVMGetEnumAttributeKindForName(name, strlen(name)), value));
}

/* Valid, aligned storage of `size` bytes at a pointer parameter. */
static void r_llvm_storage_facts(RLlvmEmitter *emitter,
                                 LLVMValueRef function,
                                 unsigned position,
                                 uint32_t size,
                                 uint32_t align) {
    r_llvm_enum_attribute(emitter, function, position, "nonnull", 0U);
    r_llvm_enum_attribute(emitter, function, position, "noundef", 0U);
    if (size != 0U) {
        r_llvm_enum_attribute(emitter, function, position, "dereferenceable", size);
    }
    if (align != 0U) {
        r_llvm_enum_attribute(emitter, function, position, "align", align);
    }
}

/* B7.1: what the convention guarantees about the pointer parameters of an ordinary function of the
   program, for the optimizer. The result or carrier memory and every parameter in memory are
   valid, aligned storage of their type. A borrow of the non-null form (R-BORROW-0005) is
   dereferenceable for its referent, and an exclusive borrow excludes every other overlapping
   access while the call runs (R-BORROW-0002), so no other pointer the function uses reaches its
   referent. */
static void r_llvm_parameter_facts(RLlvmEmitter *emitter,
                                   LLVMValueRef function,
                                   const RSemanticSymbol *symbol) {
    unsigned position = 1U;
    uint32_t index;

    if (r_llvm_result_in_memory(emitter, symbol)) {
        uint32_t size = 0U;
        uint32_t align = 0U;
        if (r_llvm_layout(emitter,
                          symbol->effect_carrier_type != R_TYPE_ID_INVALID
                              ? symbol->effect_carrier_type
                              : symbol->return_type,
                          &size,
                          &align)) {
            r_llvm_storage_facts(emitter, function, position, size, align);
        }
        position += 1U;
    }
    for (index = 0U; index < symbol->parameter_count; ++index, ++position) {
        const RTypeId parameter =
            emitter->frontend->semantic_parameter_types[symbol->first_parameter_type + index];
        const RSemanticType *type = r_llvm_type(emitter, r_llvm_value_type(emitter, parameter));
        uint32_t size = 0U;
        uint32_t align = 0U;
        if (type == NULL) {
            continue;
        }
        if (r_llvm_scalar_type(emitter, parameter) == NULL) {
            if (r_llvm_layout(emitter, parameter, &size, &align)) {
                r_llvm_storage_facts(emitter, function, position, size, align);
            }
            continue;
        }
        if ((type->kind != R_SEMANTIC_TYPE_BORROW) ||
            ((type->flags & R_SEMANTIC_TYPE_FLAG_NULLABLE) != 0U) ||
            (LLVMGetTypeKind(r_llvm_scalar_type(emitter, parameter)) != LLVMPointerTypeKind)) {
            continue;
        }
        {
            const RSemanticType *referent =
                r_llvm_type(emitter, r_llvm_value_type(emitter, type->base));
            const bool sized =
                (referent != NULL) && ((r_llvm_scalar_type(emitter, type->base) != NULL) ||
                                       (referent->kind == R_SEMANTIC_TYPE_STRUCT) ||
                                       (referent->kind == R_SEMANTIC_TYPE_FIXED_ARRAY));
            if (!sized || !r_llvm_layout(emitter, type->base, &size, &align)) {
                size = 0U;
                align = 0U;
            }
        }
        r_llvm_storage_facts(emitter, function, position, size, align);
        if ((type->flags & R_SEMANTIC_TYPE_FLAG_SHARED) == 0U) {
            r_llvm_enum_attribute(emitter, function, position, "noalias", 0U);
        }
    }
}

static bool r_llvm_declare_function(RLlvmEmitter *emitter, RSymbolId id) {
    const RSemanticSymbol *symbol = r_llvm_semantic_symbol(emitter, id);
    char name[512];

    if (symbol == NULL) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    if (symbol->is_async) {
        /* An async dispatcher has no task of its own: its start starts the selected target
           (async.c). */
        if (r_semantic_dyn_dispatcher(emitter->frontend, id) ||
            r_semantic_function_value_dispatcher(emitter->frontend, id)) {
            return true;
        }
        return r_llvm_declare_async_function(emitter, id, symbol);
    }
    if (symbol->is_import) {
        /* An imported C function has no R body: calls and raw fn values reach the C function
           itself (ffi.c). */
        return true;
    }
    if (r_llvm_synthesized_body(emitter, id) != NULL) {
        char function_name[512];
        if (!r_llvm_function_name(emitter, id, function_name, sizeof(function_name))) {
            return false;
        }
        return r_llvm_unsupported_detail(
            emitter, r_llvm_synthesized_body(emitter, id), function_name, strlen(function_name));
    }
    if (!r_llvm_function_name(emitter, id, name, sizeof(name))) {
        return false;
    }
    emitter->function_types[id] = r_llvm_sync_signature(emitter, symbol);
    if (emitter->function_types[id] == NULL) {
        return false;
    }
    emitter->functions[id] = LLVMAddFunction(emitter->module, name, emitter->function_types[id]);
    LLVMSetLinkage(emitter->functions[id], LLVMInternalLinkage);
    r_llvm_parameter_facts(emitter, emitter->functions[id], symbol);
    return emitter->status == R_FRONTEND_OK;
}

/* B7: the direct twin of an async function, an ordinary function of the same parameters and
   outcome (direct.c). */
bool r_llvm_declare_direct_twin(RLlvmEmitter *emitter, RSymbolId id) {
    const RSemanticSymbol *symbol = r_llvm_semantic_symbol(emitter, id);
    char name[520];
    size_t length;

    if ((symbol == NULL) || !r_llvm_function_name(emitter, id, name, sizeof(name) - 8U)) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    length = strlen(name);
    (void)memcpy(name + length, ".direct", 8U);
    emitter->direct_types[id] = r_llvm_sync_signature(emitter, symbol);
    if (emitter->direct_types[id] == NULL) {
        return false;
    }
    emitter->direct_twins[id] = LLVMAddFunction(emitter->module, name, emitter->direct_types[id]);
    LLVMSetLinkage(emitter->direct_twins[id], LLVMInternalLinkage);
    r_llvm_parameter_facts(emitter, emitter->direct_twins[id], symbol);
    return emitter->status == R_FRONTEND_OK;
}

/* ---- Control ---- */

static const RMirInstruction *
r_llvm_instruction(const RLlvmEmitter *emitter, const RMirBlock *block, uint32_t index) {
    return &emitter->frontend->mir_instructions[(size_t)block->first_instruction + index];
}

static const RMirBlock *r_llvm_block(const RLlvmEmitter *emitter, uint32_t index) {
    return &emitter->frontend->mir_blocks[(size_t)emitter->mir->first_block + index];
}

/* The LLVM block of a MIR block reference (one-based within the function). */
static LLVMBasicBlockRef r_llvm_target(RLlvmEmitter *emitter, RMirBlockId id) {
    if ((id == R_MIR_BLOCK_ID_INVALID) || ((size_t)id > emitter->mir->block_count)) {
        (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        return emitter->blocks[0];
    }
    return emitter->blocks[id - 1U];
}

/* After a call that may run R code: a pending panic continues at the panic block (L39). */
bool r_llvm_unwind_test(RLlvmEmitter *emitter, RMirBlockId panic_target) {
    LLVMValueRef unwinding;
    LLVMBasicBlockRef next;
    LLVMBasicBlockRef cleanup;

    if (panic_target == R_MIR_BLOCK_ID_INVALID) {
        return true;
    }
    unwinding = r_llvm_unwinding(emitter);
    if (unwinding == NULL) {
        return false;
    }
    next = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    cleanup = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    (void)LLVMBuildCondBr(emitter->builder, unwinding, cleanup, next);
    /* The panic block runs outside the unwinding state (r_llvm_panic); a step parks the panic
       in its task instead. */
    LLVMPositionBuilderAtEnd(emitter->builder, cleanup);
    if ((emitter->frame != NULL)
            ? (r_llvm_call_runtime(
                   emitter, "r_runtime_task_panic_park", &emitter->execution, 1U, NULL) == NULL)
            : (r_llvm_call_runtime(emitter, "r_runtime_unwind_cleanup_enter", NULL, 0U, NULL) ==
               NULL)) {
        return false;
    }
    (void)LLVMBuildBr(emitter->builder, r_llvm_target(emitter, panic_target));
    LLVMPositionBuilderAtEnd(emitter->builder, next);
    return true;
}

/* The private storage an `out` argument borrows (r_c17_mir_out_storage_borrow): the BORROW of a
   local without projection, directly or through the hidden local it was stored in. */
static const RMirInstruction *r_llvm_out_storage(RLlvmEmitter *emitter, RMirValueId argument) {
    const RMirInstruction *definition = r_llvm_definition(emitter, argument);

    if ((definition != NULL) && (definition->kind == R_MIR_INSTRUCTION_MOVE) &&
        !definition->place_is_parameter) {
        const RMirInstruction *stored = NULL;
        uint32_t block;
        for (block = 0U; (block < emitter->mir->block_count) && (stored == NULL); ++block) {
            const RMirBlock *current =
                &emitter->frontend->mir_blocks[emitter->mir->first_block + block];
            uint32_t index;
            for (index = 0U; index < current->instruction_count; ++index) {
                const RMirInstruction *store =
                    &emitter->frontend->mir_instructions[current->first_instruction + index];
                if ((store->kind == R_MIR_INSTRUCTION_STORE) && !store->place_is_parameter &&
                    (store->place_ordinal == definition->place_ordinal) &&
                    (store->operand1 == R_MIR_VALUE_ID_INVALID)) {
                    stored = r_llvm_definition(emitter, store->operand0);
                    break;
                }
            }
        }
        definition = stored;
    }
    return ((definition != NULL) && (definition->kind == R_MIR_INSTRUCTION_BORROW) &&
            !definition->place_is_parameter && (definition->operand0 == R_MIR_VALUE_ID_INVALID))
               ? definition
               : NULL;
}

/* R-FUNC-0022: a call that completes normally has initialized every private out storage it was
   given, which the publication that follows moves to its destination; a checked call only when
   its carrier holds no error. */
static bool r_llvm_out_storages_initialized(RLlvmEmitter *emitter,
                                            const RSemanticSymbol *callee,
                                            const RMirInstruction *instruction) {
    uint32_t index;

    for (index = 0U; index < instruction->operand_count; ++index) {
        const RTypeId parameter_type =
            emitter->frontend->semantic_parameter_types[callee->first_parameter_type + index];
        const RSemanticType *out = r_llvm_type(emitter, r_llvm_value_type(emitter, parameter_type));
        const RMirInstruction *storage = r_llvm_out_storage(
            emitter, emitter->frontend->mir_operands[(size_t)instruction->first_operand + index]);
        LLVMValueRef flag;
        if ((out == NULL) || (out->kind != R_SEMANTIC_TYPE_BORROW) ||
            ((out->flags & R_SEMANTIC_TYPE_FLAG_OUT) == 0U) || (storage == NULL)) {
            continue;
        }
        flag = r_llvm_place_flag(emitter, storage->place_ordinal, false);
        if (flag == NULL) {
            continue;
        }
        if (callee->effect_carrier_type == R_TYPE_ID_INVALID) {
            r_llvm_set_flag(emitter, flag, true);
        } else {
            LLVMValueRef carrier = r_llvm_value(emitter, instruction->result);
            if (carrier == NULL) {
                return false;
            }
            (void)LLVMBuildStore(
                emitter->builder,
                LLVMBuildZExt(
                    emitter->builder,
                    LLVMBuildICmp(
                        emitter->builder,
                        LLVMIntEQ,
                        LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), carrier, ""),
                        r_llvm_u32(emitter, 0U),
                        ""),
                    r_llvm_int(emitter, 8U),
                    ""),
                flag);
        }
    }
    return true;
}

static bool r_llvm_emit_call_into(RLlvmEmitter *emitter,
                                  const RMirInstruction *instruction,
                                  LLVMValueRef *arguments) {
    const RSemanticSymbol *callee = r_llvm_semantic_symbol(emitter, instruction->symbol);
    unsigned count = 0U;
    uint32_t index;
    LLVMValueRef call;

    if (callee == NULL) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    if (emitter->functions[instruction->symbol] == NULL) {
        return r_llvm_unsupported(emitter, "a call of a function without an R definition");
    }
    if (r_llvm_result_in_memory(emitter, callee)) {
        LLVMValueRef memory = r_llvm_value_memory(emitter, instruction->result, instruction->type);
        if (memory == NULL) {
            return false;
        }
        arguments[count++] = memory;
    }
    for (index = 0U; index < instruction->operand_count; ++index) {
        const RMirValueId operand =
            emitter->frontend->mir_operands[(size_t)instruction->first_operand + index];
        const RMirInstruction *definition = r_llvm_definition(emitter, operand);
        LLVMValueRef value = r_llvm_value(emitter, operand);
        if ((value == NULL) || (definition == NULL)) {
            return false;
        }
        if (r_llvm_scalar_type(emitter, definition->type) == NULL) {
            /* The callee owns a copy of an argument in memory. */
            uint32_t size = 0U;
            uint32_t align = 0U;
            LLVMValueRef copy;
            if (!r_llvm_layout(emitter, definition->type, &size, &align)) {
                return false;
            }
            copy = r_llvm_entry_alloca(emitter, size, align, "argument");
            (void)LLVMBuildMemCpy(
                emitter->builder, copy, align, value, align, r_llvm_u64(emitter, size));
            /* An argument that requires drop moves to the callee (r_c17_emit_async_call). */
            r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, operand), false);
            value = copy;
        }
        arguments[count++] = value;
    }
    call = LLVMBuildCall2(emitter->builder,
                          emitter->function_types[instruction->symbol],
                          emitter->functions[instruction->symbol],
                          arguments,
                          count,
                          "");
    if (!r_llvm_result_in_memory(emitter, callee) &&
        (instruction->result != R_MIR_VALUE_ID_INVALID) &&
        !r_llvm_type_is_void(emitter, callee->return_type) &&
        !r_llvm_set_value(emitter, instruction->result, call)) {
        return false;
    }
    if (instruction->result != R_MIR_VALUE_ID_INVALID) {
        r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, instruction->result), true);
    }
    return r_llvm_unwind_test(emitter, instruction->panic_target) &&
           r_llvm_out_storages_initialized(emitter, callee, instruction);
}

static bool r_llvm_emit_call(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    LLVMValueRef *arguments =
        r_llvm_allocate(emitter, ((size_t)instruction->operand_count + 1U) * sizeof(*arguments));
    bool success;

    if (arguments == NULL) {
        return false;
    }
    success = r_llvm_emit_call_into(emitter, instruction, arguments);
    r_llvm_free(emitter, arguments);
    return success;
}

/* A value leaves for memory outside this frame (a result, an error): one that requires drop moves
   by its glue and is no longer initialized here. */
static bool r_llvm_move_out(RLlvmEmitter *emitter,
                            RMirValueId id,
                            RTypeId type,
                            LLVMValueRef value,
                            LLVMValueRef destination) {
    if (!r_llvm_type_requires_drop(emitter, type)) {
        return r_llvm_store_value(emitter, type, value, destination);
    }
    if (!r_llvm_call_move(emitter, type, destination, value)) {
        return false;
    }
    r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, id), false);
    return true;
}

/* The normal end of a function, or the completion of a step in frame mode. */
static void r_llvm_build_leave(RLlvmEmitter *emitter) {
    if (emitter->frame != NULL) {
        (void)LLVMBuildRet(emitter->builder, r_llvm_u32(emitter, 0U)); /* COMPLETED */
    } else {
        (void)LLVMBuildRetVoid(emitter->builder);
    }
}

static bool r_llvm_emit_return(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RSemanticSymbol *symbol = emitter->symbol;
    const RMirInstruction *definition = r_llvm_definition(emitter, instruction->operand0);
    LLVMValueRef value = NULL;

    if (instruction->operand0 != R_MIR_VALUE_ID_INVALID) {
        value = r_llvm_value(emitter, instruction->operand0);
        if ((value == NULL) || (definition == NULL)) {
            return false;
        }
    }
    if (symbol->effect_carrier_type != R_TYPE_ID_INVALID) {
        uint32_t payload = 0U;
        if (!r_llvm_payload_offset(emitter, symbol->effect_carrier_type, &payload)) {
            return false;
        }
        if ((value != NULL) &&
            !r_llvm_move_out(emitter,
                             instruction->operand0,
                             definition->type,
                             value,
                             r_llvm_byte_offset(emitter, emitter->effect_out, payload))) {
            return false;
        }
        (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 0U), emitter->effect_out);
        r_llvm_build_leave(emitter);
        return true;
    }
    if (emitter->result_out != NULL) {
        if ((value == NULL) ||
            !r_llvm_move_out(
                emitter, instruction->operand0, definition->type, value, emitter->result_out)) {
            return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
        r_llvm_build_leave(emitter);
        return true;
    }
    if ((value != NULL) && (emitter->frame != NULL)) {
        /* A scalar result of a step is written to the task's result storage. */
        r_llvm_store_scalar(emitter, definition->type, value, emitter->step_result);
        r_llvm_build_leave(emitter);
    } else if (value == NULL) {
        r_llvm_build_leave(emitter);
    } else {
        (void)LLVMBuildRet(emitter->builder, value);
    }
    return true;
}

/* The catch payload a THROW to a catch of this function writes: the payload slot of the block. */
static const RMirInstruction *r_llvm_catch_payload(RLlvmEmitter *emitter, RMirBlockId target) {
    const RMirBlock *block;
    uint32_t index;

    if ((target == R_MIR_BLOCK_ID_INVALID) || ((size_t)target > emitter->mir->block_count)) {
        return NULL;
    }
    block = r_llvm_block(emitter, target - 1U);
    for (index = 0U; index < block->instruction_count; ++index) {
        const RMirInstruction *candidate = r_llvm_instruction(emitter, block, index);
        if ((candidate->kind == R_MIR_INSTRUCTION_EFFECT_PAYLOAD) &&
            (candidate->operand0 == R_MIR_VALUE_ID_INVALID)) {
            return candidate;
        }
    }
    return NULL;
}

/* L25.4: a throw that relays the carrier of a call moves its error, whichever it is, into the
   catch binding (an error family) or into the outcome of the function in one step, with the tag
   the destination gives it (r_c17_emit_effect_relay_definition). */
static bool r_llvm_emit_relay_throw(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const bool caught = instruction->target0 != R_MIR_BLOCK_ID_INVALID;
    const RMirInstruction *slot_definition =
        caught ? r_llvm_catch_payload(emitter, instruction->target0) : NULL;
    const RTypeId destination_type =
        caught ? instruction->type : emitter->symbol->effect_carrier_type;
    const RSemanticType *source =
        r_llvm_type(emitter, r_llvm_value_type(emitter, instruction->runtime_type));
    const RSemanticType *destination =
        r_llvm_type(emitter, r_llvm_value_type(emitter, destination_type));
    LLVMValueRef carrier = r_llvm_value(emitter, instruction->operand0);
    LLVMValueRef target;
    LLVMBasicBlockRef invalid;
    LLVMBasicBlockRef done;
    LLVMValueRef choice;
    uint32_t source_payload = 0U;
    uint32_t destination_payload = 0U;
    uint32_t count;
    uint32_t index;

    if ((source == NULL) || (destination == NULL) || (carrier == NULL) ||
        (source->kind != R_SEMANTIC_TYPE_EFFECT_CARRIER) || (caught && (slot_definition == NULL)) ||
        (!caught && (emitter->effect_out == NULL))) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    target = caught ? r_llvm_catch_slot(emitter, slot_definition->result, slot_definition->type)
                    : emitter->effect_out;
    if ((target == NULL) ||
        !r_llvm_payload_offset(emitter, instruction->runtime_type, &source_payload) ||
        !r_llvm_payload_offset(emitter, destination_type, &destination_payload)) {
        return false;
    }
    count = r_semantic_effect_count(emitter->frontend, source->second);
    invalid = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    done = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    choice =
        LLVMBuildSwitch(emitter->builder,
                        LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), carrier, ""),
                        invalid,
                        count);
    for (index = 0U; index < count; ++index) {
        const RTypeId error_type = r_semantic_effect_at(emitter->frontend, source->second, index);
        uint32_t tag = UINT32_MAX;
        LLVMBasicBlockRef member;
        if (destination->kind == R_SEMANTIC_TYPE_EFFECT_CARRIER) {
            const uint32_t destination_count =
                r_semantic_effect_count(emitter->frontend, destination->second);
            uint32_t position;
            for (position = 0U; position < destination_count; ++position) {
                if (r_semantic_effect_at(emitter->frontend, destination->second, position) ==
                    error_type) {
                    tag = position + 1U;
                    break;
                }
            }
        } else {
            tag = r_semantic_error_family_tag(
                emitter->frontend, r_llvm_value_type(emitter, destination_type), error_type);
        }
        if (tag == UINT32_MAX) {
            /* A member the destination does not hold never reaches it. */
            continue;
        }
        member = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        LLVMAddCase(choice, r_llvm_u32(emitter, index + 1U), member);
        LLVMPositionBuilderAtEnd(emitter->builder, member);
        if (r_llvm_type_requires_drop(emitter, error_type)) {
            if (!r_llvm_call_move(emitter,
                                  error_type,
                                  r_llvm_byte_offset(emitter, target, destination_payload),
                                  r_llvm_byte_offset(emitter, carrier, source_payload))) {
                return false;
            }
        } else if (!r_llvm_store_value(
                       emitter,
                       error_type,
                       r_llvm_scalar_type(emitter, error_type) == NULL
                           ? r_llvm_byte_offset(emitter, carrier, source_payload)
                           : r_llvm_load_scalar(
                                 emitter,
                                 error_type,
                                 r_llvm_byte_offset(emitter, carrier, source_payload)),
                       r_llvm_byte_offset(emitter, target, destination_payload))) {
            return false;
        }
        (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, tag), target);
        (void)LLVMBuildBr(emitter->builder, done);
    }
    LLVMPositionBuilderAtEnd(emitter->builder, invalid);
    if (!r_llvm_panic(emitter,
                      "R_RUNTIME_PANIC_CONTRACT_VIOLATION",
                      instruction->span,
                      R_MIR_BLOCK_ID_INVALID)) {
        return false;
    }
    LLVMPositionBuilderAtEnd(emitter->builder, done);
    if (caught) {
        r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, slot_definition->result), true);
    }
    r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, instruction->operand0), false);
    if (caught) {
        (void)LLVMBuildBr(emitter->builder, r_llvm_target(emitter, instruction->target0));
    } else {
        r_llvm_build_leave(emitter);
    }
    return true;
}

/* THROW: to a catch of the same function, the error goes to the catch's payload and control to
   its block; otherwise it is the outcome of the function (R-ERR-0002). */
static bool r_llvm_emit_throw(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RMirInstruction *definition = r_llvm_definition(emitter, instruction->operand0);
    LLVMValueRef value = r_llvm_value(emitter, instruction->operand0);
    uint32_t payload = 0U;

    if ((value == NULL) || (definition == NULL)) {
        return false;
    }
    if (instruction->runtime_type != R_TYPE_ID_INVALID) {
        return r_llvm_emit_relay_throw(emitter, instruction);
    }
    if (instruction->target0 != R_MIR_BLOCK_ID_INVALID) {
        const RMirBlock *block;
        uint32_t index;
        if ((size_t)instruction->target0 > emitter->mir->block_count) {
            return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
        block = r_llvm_block(emitter, instruction->target0 - 1U);
        for (index = 0U; index < block->instruction_count; ++index) {
            const RMirInstruction *candidate = r_llvm_instruction(emitter, block, index);
            if ((candidate->kind == R_MIR_INSTRUCTION_EFFECT_PAYLOAD) &&
                (candidate->operand0 == R_MIR_VALUE_ID_INVALID)) {
                LLVMValueRef slot = r_llvm_catch_slot(emitter, candidate->result, candidate->type);
                if ((slot == NULL) ||
                    !r_llvm_move_out(
                        emitter, instruction->operand0, definition->type, value, slot)) {
                    return false;
                }
                r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, candidate->result), true);
                (void)LLVMBuildBr(emitter->builder, r_llvm_target(emitter, instruction->target0));
                return true;
            }
        }
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    if ((emitter->effect_out == NULL) ||
        !r_llvm_payload_offset(emitter, emitter->symbol->effect_carrier_type, &payload) ||
        !r_llvm_move_out(emitter,
                         instruction->operand0,
                         definition->type,
                         value,
                         r_llvm_byte_offset(emitter, emitter->effect_out, payload))) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    (void)LLVMBuildStore(
        emitter->builder, r_llvm_u32(emitter, instruction->integer_value), emitter->effect_out);
    r_llvm_build_leave(emitter);
    return true;
}

static bool r_llvm_drop_pending(RLlvmEmitter *emitter);

/* The payloads of pending finally routes, then every initialized place and value in the reverse
   order of the MIR (the C17 emitter's async frame drop). */
static bool r_llvm_drop_storage(RLlvmEmitter *emitter) {
    uint32_t block_index = emitter->mir->block_count;

    if (!r_llvm_drop_pending(emitter)) {
        return false;
    }
    while (block_index != 0U) {
        const RMirBlock *block;
        uint32_t index;
        block_index -= 1U;
        block = r_llvm_block(emitter, block_index);
        index = block->instruction_count;
        while (index != 0U) {
            const RMirInstruction *instruction;
            LLVMValueRef pointer = NULL;
            LLVMValueRef flag = NULL;
            index -= 1U;
            instruction = r_llvm_instruction(emitter, block, index);
            if ((instruction->kind == R_MIR_INSTRUCTION_FIELD) ||
                (instruction->kind == R_MIR_INSTRUCTION_INDEX) ||
                (instruction->kind == R_MIR_INSTRUCTION_DEREF) ||
                !r_llvm_type_requires_drop(emitter, instruction->type)) {
                continue;
            }
            if ((instruction->kind == R_MIR_INSTRUCTION_PARAMETER) ||
                (instruction->kind == R_MIR_INSTRUCTION_LOCAL)) {
                const bool parameter = instruction->kind == R_MIR_INSTRUCTION_PARAMETER;
                flag = r_llvm_place_flag(emitter, instruction->place_ordinal, parameter);
                pointer = r_llvm_place_address(
                    emitter, instruction->place_ordinal, parameter, R_MIR_VALUE_ID_INVALID);
            } else if ((instruction->result != R_MIR_VALUE_ID_INVALID) &&
                       (r_llvm_scalar_type(emitter, instruction->type) == NULL)) {
                flag = r_llvm_value_flag(emitter, instruction->result);
                pointer = r_llvm_value_memory(emitter, instruction->result, instruction->type);
            }
            if ((flag != NULL) && (pointer != NULL) &&
                !r_llvm_drop_flagged(emitter, instruction->type, pointer, flag)) {
                return false;
            }
        }
    }
    return true;
}

/* PANIC: the frame is in the cleanup state its panic edge entered (r_llvm_panic). The payloads
   of pending finally routes, then every initialized place and value in the reverse order of the
   MIR, are dropped as the C17 emitter's async frame drop does; the panic becomes pending again
   and the frame leaves with a zero result that the caller never reads (L39). */
static bool r_llvm_emit_panic_exit(RLlvmEmitter *emitter) {
    LLVMTypeRef result = LLVMGetReturnType(LLVMGlobalGetValueType(emitter->function));
    uint32_t block_index = emitter->mir->block_count;

    if (emitter->frame != NULL) {
        /* A step hands its parked panic back; the runtime drops the frame (C17 PANIC). */
        if (r_llvm_call_runtime(
                emitter, "r_runtime_task_panic_unpark", &emitter->execution, 1U, NULL) == NULL) {
            return false;
        }
        (void)LLVMBuildRet(emitter->builder, r_llvm_u32(emitter, 3U)); /* PANICKED */
        return true;
    }
    (void)block_index;
    if (!r_llvm_drop_storage(emitter)) {
        return false;
    }
    if (r_llvm_call_runtime(emitter, "r_runtime_unwind_cleanup_leave", NULL, 0U, NULL) == NULL) {
        return false;
    }
    if (LLVMGetTypeKind(result) == LLVMVoidTypeKind) {
        (void)LLVMBuildRetVoid(emitter->builder);
    } else {
        (void)LLVMBuildRet(emitter->builder, LLVMConstNull(result));
    }
    return true;
}

/* ---- Finally routes ---- */

/* The pending record at an index (an i32 value). */
static LLVMValueRef r_llvm_pending_record(RLlvmEmitter *emitter, LLVMValueRef index) {
    LLVMValueRef offset =
        LLVMBuildMul(emitter->builder,
                     LLVMBuildZExt(emitter->builder, index, r_llvm_int(emitter, 64U), ""),
                     r_llvm_u64(emitter, emitter->pending_size),
                     "");
    return LLVMBuildGEP2(
        emitter->builder, r_llvm_int(emitter, 8U), emitter->pending, &offset, 1U, "");
}

static LLVMValueRef r_llvm_load_u32(RLlvmEmitter *emitter, LLVMValueRef pointer) {
    return LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), pointer, "");
}

static uint32_t r_llvm_pending_tag(RLlvmEmitter *emitter, RTypeId type) {
    uint32_t index;

    for (index = 0U; index < emitter->pending_type_count; ++index) {
        if (emitter->pending_types[index] == r_llvm_value_type(emitter, type)) {
            return index + 1U;
        }
    }
    (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    return 0U;
}

/* Sizes the finally stack and the pending records of the function, as the C17 emitter's
   r_c17_collect_async_finally_metadata: one entry per FINALLY_PUSH, a payload union of every type
   a PENDING_SET carries. */
static bool r_llvm_prepare_finally(RLlvmEmitter *emitter) {
    uint32_t block_index;
    uint32_t payload_size = 1U;
    uint32_t payload_align = 1U;
    uint32_t types = 0U;

    emitter->finally_capacity = 0U;
    for (block_index = 0U; block_index < emitter->mir->block_count; ++block_index) {
        const RMirBlock *block = r_llvm_block(emitter, block_index);
        uint32_t index;
        for (index = 0U; index < block->instruction_count; ++index) {
            const RMirInstruction *instruction = r_llvm_instruction(emitter, block, index);
            emitter->finally_capacity +=
                instruction->kind == R_MIR_INSTRUCTION_FINALLY_PUSH ? 1U : 0U;
            types += (instruction->kind == R_MIR_INSTRUCTION_PENDING_SET) &&
                             (instruction->operand0 != R_MIR_VALUE_ID_INVALID)
                         ? 1U
                         : 0U;
        }
    }
    if (emitter->finally_capacity == 0U) {
        return true;
    }
    if ((types != 0U) && ((emitter->pending_types = r_llvm_allocate(
                               emitter, types * sizeof(*emitter->pending_types))) == NULL)) {
        return false;
    }
    emitter->pending_type_count = 0U;
    for (block_index = 0U; block_index < emitter->mir->block_count; ++block_index) {
        const RMirBlock *block = r_llvm_block(emitter, block_index);
        uint32_t index;
        for (index = 0U; index < block->instruction_count; ++index) {
            const RMirInstruction *instruction = r_llvm_instruction(emitter, block, index);
            const RTypeId type = r_llvm_value_type(emitter, instruction->type);
            uint32_t size = 0U;
            uint32_t align = 0U;
            uint32_t known;
            bool seen = false;
            if ((instruction->kind != R_MIR_INSTRUCTION_PENDING_SET) ||
                (instruction->operand0 == R_MIR_VALUE_ID_INVALID)) {
                continue;
            }
            for (known = 0U; known < emitter->pending_type_count; ++known) {
                seen = seen || (emitter->pending_types[known] == type);
            }
            if (seen) {
                continue;
            }
            if (!r_llvm_layout(emitter, type, &size, &align)) {
                return false;
            }
            emitter->pending_types[emitter->pending_type_count++] = type;
            payload_size = size > payload_size ? size : payload_size;
            payload_align = align > payload_align ? align : payload_align;
        }
    }
    /* struct { u32 reason; u32 resume_state; u32 stop_depth; u32 payload_tag; union payload; } */
    emitter->pending_payload_offset = ((16U + payload_align - 1U) / payload_align) * payload_align;
    {
        const uint32_t record_align = payload_align > 4U ? payload_align : 4U;
        emitter->pending_size =
            ((emitter->pending_payload_offset + payload_size + record_align - 1U) / record_align) *
            record_align;
        emitter->finally_stack =
            r_llvm_entry_alloca(emitter, 4U * emitter->finally_capacity, 4U, "finally_stack");
        emitter->pending = r_llvm_entry_alloca(
            emitter, emitter->pending_size * emitter->finally_capacity, record_align, "pending");
    }
    emitter->finally_depth = r_llvm_entry_alloca(emitter, 4U, 4U, "finally_depth");
    emitter->pending_depth = r_llvm_entry_alloca(emitter, 4U, 4U, "pending_depth");
    if (emitter->frame == NULL) {
        /* A frame starts with zero depths (its initializer clears it). */
        (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 0U), emitter->finally_depth);
        (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 0U), emitter->pending_depth);
    }
    return true;
}

/* The payloads of pending routes, dropped from the top (r_c17_emit_async_pending_fallback_drop). */
static bool r_llvm_drop_pending(RLlvmEmitter *emitter) {
    LLVMBasicBlockRef test;
    LLVMBasicBlockRef body;
    LLVMBasicBlockRef done;
    LLVMValueRef depth;
    LLVMValueRef top;
    LLVMValueRef record;
    uint32_t index;
    bool owned = false;

    for (index = 0U; index < emitter->pending_type_count; ++index) {
        owned = owned || r_llvm_type_requires_drop(emitter, emitter->pending_types[index]);
    }
    if ((emitter->finally_capacity == 0U) || !owned) {
        return true;
    }
    test = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    body = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    done = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    (void)LLVMBuildBr(emitter->builder, test);
    LLVMPositionBuilderAtEnd(emitter->builder, test);
    depth = r_llvm_load_u32(emitter, emitter->pending_depth);
    (void)LLVMBuildCondBr(
        emitter->builder,
        LLVMBuildICmp(emitter->builder, LLVMIntNE, depth, r_llvm_u32(emitter, 0U), ""),
        body,
        done);
    LLVMPositionBuilderAtEnd(emitter->builder, body);
    top = LLVMBuildSub(emitter->builder, depth, r_llvm_u32(emitter, 1U), "");
    record = r_llvm_pending_record(emitter, top);
    for (index = 0U; index < emitter->pending_type_count; ++index) {
        LLVMBasicBlockRef drop;
        LLVMBasicBlockRef next;
        if (!r_llvm_type_requires_drop(emitter, emitter->pending_types[index])) {
            continue;
        }
        drop = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        next = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        (void)LLVMBuildCondBr(
            emitter->builder,
            LLVMBuildICmp(emitter->builder,
                          LLVMIntEQ,
                          r_llvm_load_u32(emitter, r_llvm_byte_offset(emitter, record, 12U)),
                          r_llvm_u32(emitter, index + 1U),
                          ""),
            drop,
            next);
        LLVMPositionBuilderAtEnd(emitter->builder, drop);
        if (!r_llvm_call_drop(
                emitter,
                emitter->pending_types[index],
                r_llvm_byte_offset(emitter, record, emitter->pending_payload_offset))) {
            return false;
        }
        (void)LLVMBuildBr(emitter->builder, next);
        LLVMPositionBuilderAtEnd(emitter->builder, next);
    }
    (void)LLVMBuildStore(emitter->builder, top, emitter->pending_depth);
    (void)LLVMBuildBr(emitter->builder, test);
    LLVMPositionBuilderAtEnd(emitter->builder, done);
    return true;
}

static bool r_llvm_emit_finally(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    LLVMValueRef depth;

    if (emitter->finally_capacity == 0U) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    switch (instruction->kind) {
    case R_MIR_INSTRUCTION_FINALLY_PUSH: {
        LLVMValueRef offset;
        depth = r_llvm_load_u32(emitter, emitter->finally_depth);
        offset = LLVMBuildMul(emitter->builder,
                              LLVMBuildZExt(emitter->builder, depth, r_llvm_int(emitter, 64U), ""),
                              r_llvm_u64(emitter, 4U),
                              "");
        (void)LLVMBuildStore(emitter->builder,
                             r_llvm_u32(emitter, instruction->integer_value),
                             LLVMBuildGEP2(emitter->builder,
                                           r_llvm_int(emitter, 8U),
                                           emitter->finally_stack,
                                           &offset,
                                           1U,
                                           ""));
        (void)LLVMBuildStore(emitter->builder,
                             LLVMBuildAdd(emitter->builder, depth, r_llvm_u32(emitter, 1U), ""),
                             emitter->finally_depth);
        return true;
    }
    case R_MIR_INSTRUCTION_FINALLY_ENTER:
        depth = r_llvm_load_u32(emitter, emitter->finally_depth);
        (void)LLVMBuildStore(emitter->builder,
                             LLVMBuildSub(emitter->builder, depth, r_llvm_u32(emitter, 1U), ""),
                             emitter->finally_depth);
        return true;
    case R_MIR_INSTRUCTION_PENDING_SET: {
        LLVMValueRef record;
        uint32_t tag = 0U;
        depth = r_llvm_load_u32(emitter, emitter->pending_depth);
        record = r_llvm_pending_record(emitter, depth);
        if (instruction->operand0 != R_MIR_VALUE_ID_INVALID) {
            LLVMValueRef value = r_llvm_value(emitter, instruction->operand0);
            LLVMValueRef payload =
                r_llvm_byte_offset(emitter, record, emitter->pending_payload_offset);
            tag = r_llvm_pending_tag(emitter, instruction->type);
            if ((value == NULL) || (tag == 0U)) {
                return false;
            }
            if (r_llvm_type_requires_drop(emitter, instruction->type)) {
                if (!r_llvm_call_move(emitter, instruction->type, payload, value)) {
                    return false;
                }
                r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, instruction->operand0), false);
            } else if (!r_llvm_store_value(emitter, instruction->type, value, payload)) {
                return false;
            }
        }
        (void)LLVMBuildStore(
            emitter->builder, r_llvm_u32(emitter, instruction->integer_value), record);
        (void)LLVMBuildStore(emitter->builder,
                             r_llvm_u32(emitter, instruction->target1),
                             r_llvm_byte_offset(emitter, record, 4U));
        (void)LLVMBuildStore(emitter->builder,
                             r_llvm_u32(emitter, instruction->place_ordinal),
                             r_llvm_byte_offset(emitter, record, 8U));
        (void)LLVMBuildStore(
            emitter->builder, r_llvm_u32(emitter, tag), r_llvm_byte_offset(emitter, record, 12U));
        (void)LLVMBuildStore(emitter->builder,
                             LLVMBuildAdd(emitter->builder, depth, r_llvm_u32(emitter, 1U), ""),
                             emitter->pending_depth);
        return true;
    }
    case R_MIR_INSTRUCTION_PENDING_RESUME: {
        LLVMValueRef top;
        LLVMValueRef record;
        depth = r_llvm_load_u32(emitter, emitter->pending_depth);
        top = LLVMBuildSub(emitter->builder, depth, r_llvm_u32(emitter, 1U), "");
        record = r_llvm_pending_record(emitter, top);
        if (instruction->operand0 != R_MIR_VALUE_ID_INVALID) {
            LLVMValueRef payload =
                r_llvm_byte_offset(emitter, record, emitter->pending_payload_offset);
            if (r_llvm_type_requires_drop(emitter, instruction->type)) {
                LLVMValueRef memory =
                    r_llvm_value_memory(emitter, instruction->operand0, instruction->type);
                if ((memory == NULL) ||
                    !r_llvm_call_move(emitter, instruction->type, memory, payload)) {
                    return false;
                }
                r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, instruction->operand0), true);
            } else if (r_llvm_scalar_type(emitter, instruction->type) != NULL) {
                /* The value is the payload again; the code after the resume reads it. */
                if (!r_llvm_set_value(emitter,
                                      instruction->operand0,
                                      r_llvm_load_scalar(emitter, instruction->type, payload))) {
                    return false;
                }
            } else {
                LLVMValueRef memory =
                    r_llvm_value_memory(emitter, instruction->operand0, instruction->type);
                if ((memory == NULL) ||
                    !r_llvm_store_value(emitter, instruction->type, payload, memory)) {
                    return false;
                }
            }
        }
        (void)LLVMBuildStore(
            emitter->builder, r_llvm_u32(emitter, 0U), r_llvm_byte_offset(emitter, record, 12U));
        (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 0U), record);
        (void)LLVMBuildStore(emitter->builder, top, emitter->pending_depth);
        return true;
    }
    default:
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
}

/* Whether an earlier PENDING_SET of the function resumes at the same block (a switch takes each
   case once). */
static bool r_llvm_resume_seen(RLlvmEmitter *emitter,
                               const RMirInstruction *candidate,
                               uint32_t block_limit,
                               uint32_t index_limit) {
    uint32_t block_index;

    for (block_index = 0U; block_index <= block_limit; ++block_index) {
        const RMirBlock *block = r_llvm_block(emitter, block_index);
        const uint32_t end = block_index == block_limit ? index_limit : block->instruction_count;
        uint32_t index;
        for (index = 0U; index < end; ++index) {
            const RMirInstruction *earlier = r_llvm_instruction(emitter, block, index);
            if ((earlier->kind == R_MIR_INSTRUCTION_PENDING_SET) &&
                (earlier->target1 == candidate->target1)) {
                return true;
            }
        }
    }
    return false;
}

/* FINALLY_EXIT: to the next finally the route still crosses, or to where the route resumes
   (r_c17_emit_async_finally_exit). */
static bool r_llvm_emit_finally_exit(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    LLVMBasicBlockRef next_finally =
        LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef resume =
        LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef invalid =
        LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMValueRef finally_depth;
    LLVMValueRef record;
    LLVMValueRef finally_switch;
    LLVMValueRef resume_switch;
    uint32_t block_index;

    if (emitter->finally_capacity == 0U) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    finally_depth = r_llvm_load_u32(emitter, emitter->finally_depth);
    record = r_llvm_pending_record(emitter,
                                   LLVMBuildSub(emitter->builder,
                                                r_llvm_load_u32(emitter, emitter->pending_depth),
                                                r_llvm_u32(emitter, 1U),
                                                ""));
    (void)LLVMBuildCondBr(
        emitter->builder,
        LLVMBuildICmp(emitter->builder,
                      LLVMIntUGT,
                      finally_depth,
                      r_llvm_load_u32(emitter, r_llvm_byte_offset(emitter, record, 8U)),
                      ""),
        next_finally,
        resume);
    LLVMPositionBuilderAtEnd(emitter->builder, next_finally);
    {
        LLVMValueRef offset = LLVMBuildMul(
            emitter->builder,
            LLVMBuildZExt(
                emitter->builder,
                LLVMBuildSub(emitter->builder, finally_depth, r_llvm_u32(emitter, 1U), ""),
                r_llvm_int(emitter, 64U),
                ""),
            r_llvm_u64(emitter, 4U),
            "");
        finally_switch = LLVMBuildSwitch(emitter->builder,
                                         r_llvm_load_u32(emitter,
                                                         LLVMBuildGEP2(emitter->builder,
                                                                       r_llvm_int(emitter, 8U),
                                                                       emitter->finally_stack,
                                                                       &offset,
                                                                       1U,
                                                                       "")),
                                         invalid,
                                         emitter->finally_capacity);
    }
    LLVMPositionBuilderAtEnd(emitter->builder, resume);
    resume_switch =
        LLVMBuildSwitch(emitter->builder,
                        r_llvm_load_u32(emitter, r_llvm_byte_offset(emitter, record, 4U)),
                        invalid,
                        4U);
    for (block_index = 0U; block_index < emitter->mir->block_count; ++block_index) {
        const RMirBlock *block = r_llvm_block(emitter, block_index);
        uint32_t index;
        for (index = 0U; index < block->instruction_count; ++index) {
            const RMirInstruction *candidate = r_llvm_instruction(emitter, block, index);
            if (candidate->kind == R_MIR_INSTRUCTION_FINALLY_PUSH) {
                LLVMAddCase(finally_switch,
                            r_llvm_u32(emitter, candidate->integer_value),
                            r_llvm_target(emitter, candidate->target0));
            } else if ((candidate->kind == R_MIR_INSTRUCTION_PENDING_SET) &&
                       !r_llvm_resume_seen(emitter, candidate, block_index, index)) {
                LLVMAddCase(resume_switch,
                            r_llvm_u32(emitter, candidate->target1),
                            r_llvm_target(emitter, candidate->target1));
            }
        }
    }
    LLVMPositionBuilderAtEnd(emitter->builder, invalid);
    return r_llvm_panic(
        emitter, "R_RUNTIME_PANIC_CONTRACT_VIOLATION", instruction->span, R_MIR_BLOCK_ID_INVALID);
}

static bool r_llvm_emit_phi(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    LLVMTypeRef type = r_llvm_scalar_type(emitter, instruction->type);
    LLVMValueRef phi;

    if (emitter->frame != NULL) {
        /* In a step every phi is frame storage the edges move the incoming value to. */
        if (r_llvm_value_memory(emitter, instruction->result, instruction->type) == NULL) {
            return false;
        }
        emitter->frame_scalars[instruction->result] =
            (type != NULL) && !r_llvm_type_requires_drop(emitter, instruction->type) ? 1U : 0U;
        return true;
    }
    if (type == NULL) {
        uint32_t size = 0U;
        uint32_t align = 0U;
        if (r_llvm_type_requires_drop(emitter, instruction->type)) {
            /* Storage the edges move the incoming value to. */
            return r_llvm_value_memory(emitter, instruction->result, instruction->type) != NULL;
        }
        if (!r_llvm_layout(emitter, instruction->type, &size, &align)) {
            return false;
        }
        type = r_llvm_pointer(emitter);
    }
    phi = LLVMBuildPhi(emitter->builder, type, "");
    emitter->phi_blocks[emitter->phi_count] = emitter->current_block;
    emitter->phis[emitter->phi_count++] = instruction;
    return r_llvm_set_value(emitter, instruction->result, phi);
}

/* Whether the phis at the start of a MIR block include one of a value that owns resources. */
static bool r_llvm_block_has_owned_phi(RLlvmEmitter *emitter, RMirBlockId target) {
    const RMirBlock *block;
    uint32_t index;

    if ((target == R_MIR_BLOCK_ID_INVALID) || ((size_t)target > emitter->mir->block_count)) {
        return false;
    }
    block = r_llvm_block(emitter, target - 1U);
    for (index = 0U; index < block->instruction_count; ++index) {
        const RMirInstruction *instruction = r_llvm_instruction(emitter, block, index);
        if (instruction->kind != R_MIR_INSTRUCTION_PHI) {
            break;
        }
        if ((emitter->frame != NULL) || ((r_llvm_scalar_type(emitter, instruction->type) == NULL) &&
                                         r_llvm_type_requires_drop(emitter, instruction->type))) {
            return true;
        }
    }
    return false;
}

/* The edge from the current block to `target`: the incoming values of its owned phis move into
   them (r_c17_emit_async_phi_edge), and the block the edge leaves from is recorded. */
static bool r_llvm_phi_edge(RLlvmEmitter *emitter, RMirBlockId target) {
    const RMirBlock *block = r_llvm_block(emitter, target - 1U);
    uint32_t index;

    for (index = 0U; index < block->instruction_count; ++index) {
        const RMirInstruction *instruction = r_llvm_instruction(emitter, block, index);
        RMirValueId incoming;
        if (instruction->kind != R_MIR_INSTRUCTION_PHI) {
            break;
        }
        if ((emitter->frame == NULL) && ((r_llvm_scalar_type(emitter, instruction->type) != NULL) ||
                                         !r_llvm_type_requires_drop(emitter, instruction->type))) {
            continue;
        }
        if (instruction->target0 == emitter->current_block + 1U) {
            incoming = instruction->operand0;
        } else if (instruction->target1 == emitter->current_block + 1U) {
            incoming = instruction->operand1;
        } else {
            return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
        if (!r_llvm_transfer(emitter,
                             instruction->type,
                             r_llvm_value_memory(emitter, instruction->result, instruction->type),
                             r_llvm_value_flag(emitter, instruction->result),
                             /* In a step a scalar is read from its frame slot. */
                             emitter->frame != NULL
                                 ? r_llvm_value_memory(emitter, incoming, instruction->type)
                                 : r_llvm_value(emitter, incoming),
                             r_llvm_value_flag(emitter, incoming))) {
            return false;
        }
    }
    emitter->edges[emitter->edge_count].source = emitter->current_block;
    emitter->edges[emitter->edge_count].target = target;
    emitter->edges[emitter->edge_count].block = LLVMGetInsertBlock(emitter->builder);
    emitter->edge_count += 1U;
    return true;
}

/* The LLVM block the edge from MIR block index `source` to the block of a phi leaves from. */
static LLVMBasicBlockRef
r_llvm_edge_block(RLlvmEmitter *emitter, uint32_t source, uint32_t phi_block) {
    size_t index;

    for (index = 0U; index < emitter->edge_count; ++index) {
        if ((emitter->edges[index].source == source) &&
            (emitter->edges[index].target == phi_block + 1U)) {
            return emitter->edges[index].block;
        }
    }
    return emitter->block_ends[source];
}

/* A branch or jump to `target`, through an edge that moves owned phi values when it has any. */
static LLVMBasicBlockRef r_llvm_edge_target(RLlvmEmitter *emitter, RMirBlockId target) {
    LLVMBasicBlockRef edge;
    LLVMBasicBlockRef current;

    if (!r_llvm_block_has_owned_phi(emitter, target)) {
        return r_llvm_target(emitter, target);
    }
    current = LLVMGetInsertBlock(emitter->builder);
    edge = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMPositionBuilderAtEnd(emitter->builder, edge);
    if (r_llvm_phi_edge(emitter, target)) {
        (void)LLVMBuildBr(emitter->builder, r_llvm_target(emitter, target));
    }
    LLVMPositionBuilderAtEnd(emitter->builder, current);
    return edge;
}

static bool r_llvm_complete_phis(RLlvmEmitter *emitter) {
    size_t index;

    for (index = 0U; index < emitter->phi_count; ++index) {
        const RMirInstruction *instruction = emitter->phis[index];
        LLVMValueRef phi = r_llvm_value(emitter, instruction->result);
        LLVMValueRef values[2];
        LLVMBasicBlockRef blocks[2];
        unsigned count = 0U;

        if ((phi == NULL) || (instruction->target0 == R_MIR_BLOCK_ID_INVALID) ||
            ((size_t)instruction->target0 > emitter->mir->block_count)) {
            return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
        /* An incoming block that ended before its terminator does not reach the phi. */
        count = 0U;
        if (emitter->block_ends[instruction->target0 - 1U] != NULL) {
            values[count] = r_llvm_value(emitter, instruction->operand0);
            blocks[count] =
                r_llvm_edge_block(emitter, instruction->target0 - 1U, emitter->phi_blocks[index]);
            count += 1U;
        }
        if (instruction->operand1 != R_MIR_VALUE_ID_INVALID) {
            if ((instruction->target1 == R_MIR_BLOCK_ID_INVALID) ||
                ((size_t)instruction->target1 > emitter->mir->block_count)) {
                return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
            }
            if (emitter->block_ends[instruction->target1 - 1U] != NULL) {
                values[count] = r_llvm_value(emitter, instruction->operand1);
                blocks[count] = r_llvm_edge_block(
                    emitter, instruction->target1 - 1U, emitter->phi_blocks[index]);
                count += 1U;
            }
        }
        if (((count >= 1U) && (values[0] == NULL)) || ((count == 2U) && (values[1] == NULL))) {
            return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
        if (count != 0U) {
            LLVMAddIncoming(phi, values, blocks, count);
        }
    }
    return true;
}

static bool r_llvm_is_terminator(RMirInstructionKind kind) {
    return (kind == R_MIR_INSTRUCTION_BRANCH) || (kind == R_MIR_INSTRUCTION_JUMP) ||
           (kind == R_MIR_INSTRUCTION_AWAIT) || (kind == R_MIR_INSTRUCTION_CANCEL) ||
           (kind == R_MIR_INSTRUCTION_TASK_SCOPE_WAIT) || (kind == R_MIR_INSTRUCTION_RETURN) ||
           (kind == R_MIR_INSTRUCTION_THROW) || (kind == R_MIR_INSTRUCTION_PANIC) ||
           (kind == R_MIR_INSTRUCTION_UNREACHABLE) || (kind == R_MIR_INSTRUCTION_FINALLY_EXIT);
}

static bool r_llvm_emit_instruction(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    switch (instruction->kind) {
    case R_MIR_INSTRUCTION_PARAMETER:
    case R_MIR_INSTRUCTION_LOCAL:
        return true;
    case R_MIR_INSTRUCTION_TYPE_QUERY: {
        uint32_t size = 0U;
        uint32_t align = 0U;
        LLVMTypeRef type = r_llvm_scalar_type(emitter, instruction->type);
        return (type != NULL) &&
               r_llvm_layout(emitter, instruction->auxiliary_type, &size, &align) &&
               r_llvm_set_value(
                   emitter,
                   instruction->result,
                   LLVMConstInt(
                       type, instruction->operation == R_TOKEN_KW_SIZEOF ? size : align, 0));
    }
    case R_MIR_INSTRUCTION_CALL:
        return r_llvm_emit_call(emitter, instruction);
    case R_MIR_INSTRUCTION_PHI:
        return r_llvm_emit_phi(emitter, instruction);
    case R_MIR_INSTRUCTION_BRANCH: {
        LLVMValueRef condition = r_llvm_value(emitter, instruction->operand0);
        if (condition == NULL) {
            return false;
        }
        {
            LLVMBasicBlockRef then = r_llvm_edge_target(emitter, instruction->target0);
            LLVMBasicBlockRef otherwise = r_llvm_edge_target(emitter, instruction->target1);
            (void)LLVMBuildCondBr(emitter->builder, condition, then, otherwise);
        }
        return emitter->status == R_FRONTEND_OK;
    }
    case R_MIR_INSTRUCTION_JUMP:
        if (r_llvm_block_has_owned_phi(emitter, instruction->target0)) {
            if (!r_llvm_phi_edge(emitter, instruction->target0)) {
                return false;
            }
            emitter->block_ends[emitter->current_block] = LLVMGetInsertBlock(emitter->builder);
        }
        (void)LLVMBuildBr(emitter->builder, r_llvm_target(emitter, instruction->target0));
        return true;
    case R_MIR_INSTRUCTION_RETURN:
        return r_llvm_emit_return(emitter, instruction);
    case R_MIR_INSTRUCTION_THROW:
        return r_llvm_emit_throw(emitter, instruction);
    case R_MIR_INSTRUCTION_ASYNC_START:
        return r_llvm_emit_async_start(emitter, instruction);
    case R_MIR_INSTRUCTION_AWAIT:
        return r_llvm_emit_await(emitter, instruction);
    case R_MIR_INSTRUCTION_TASK_SCOPE_ENTER:
    case R_MIR_INSTRUCTION_TASK_SCOPE_WAIT:
    case R_MIR_INSTRUCTION_TASK_SCOPE_CLOSE:
        return r_llvm_emit_task_scope(emitter, instruction);
    case R_MIR_INSTRUCTION_CANCEL:
        if (emitter->frame == NULL) {
            return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
        (void)LLVMBuildRet(emitter->builder, r_llvm_u32(emitter, 2U)); /* CANCELLED */
        return true;
    case R_MIR_INSTRUCTION_PANIC:
        return r_llvm_emit_panic_exit(emitter);
    case R_MIR_INSTRUCTION_FINALLY_PUSH:
    case R_MIR_INSTRUCTION_FINALLY_ENTER:
    case R_MIR_INSTRUCTION_PENDING_SET:
    case R_MIR_INSTRUCTION_PENDING_RESUME:
        return r_llvm_emit_finally(emitter, instruction);
    case R_MIR_INSTRUCTION_FINALLY_EXIT:
        return r_llvm_emit_finally_exit(emitter, instruction);
    case R_MIR_INSTRUCTION_UNREACHABLE:
        (void)LLVMBuildUnreachable(emitter->builder);
        return true;
    default:
        return r_llvm_emit_value_instruction(emitter, instruction);
    }
}

static void r_llvm_release_function_state(RLlvmEmitter *emitter) {
    r_llvm_free(emitter, emitter->version_dominators);
    emitter->version_dominators = NULL;
    r_llvm_free(emitter, emitter->blocks);
    r_llvm_free(emitter, emitter->block_ends);
    r_llvm_free(emitter, emitter->definitions);
    r_llvm_free(emitter, emitter->values);
    r_llvm_free(emitter, emitter->catch_slots);
    r_llvm_free(emitter, emitter->value_flags);
    r_llvm_free(emitter, emitter->forwarded);
    r_llvm_free(emitter, emitter->frame_scalars);
    r_llvm_free(emitter, emitter->scope_symbols);
    r_llvm_free(emitter, emitter->scope_storage);
    r_llvm_free(emitter, emitter->scope_entries);
    r_llvm_free(emitter, emitter->pending_types);
    r_llvm_free(emitter, emitter->local_flags);
    r_llvm_free(emitter, emitter->parameter_flags);
    r_llvm_free(emitter, emitter->phis);
    r_llvm_free(emitter, emitter->phi_blocks);
    r_llvm_free(emitter, emitter->edges);
    r_llvm_free(emitter, emitter->locals);
    r_llvm_free(emitter, emitter->local_types);
    r_llvm_free(emitter, emitter->local_imports);
    r_llvm_free(emitter, emitter->parameters);
    r_llvm_free(emitter, emitter->parameter_types);
    emitter->blocks = NULL;
    emitter->block_ends = NULL;
    emitter->definitions = NULL;
    emitter->values = NULL;
    emitter->catch_slots = NULL;
    emitter->value_flags = NULL;
    emitter->frame_scalars = NULL;
    emitter->forwarded = NULL;
    emitter->scope_symbols = NULL;
    emitter->scope_storage = NULL;
    emitter->scope_entries = NULL;
    emitter->scope_count = 0U;
    emitter->scope_capacity = 0U;
    emitter->pending_types = NULL;
    emitter->pending_type_count = 0U;
    emitter->finally_capacity = 0U;
    emitter->local_flags = NULL;
    emitter->parameter_flags = NULL;
    emitter->phis = NULL;
    emitter->phi_blocks = NULL;
    emitter->edges = NULL;
    emitter->edge_count = 0U;
    emitter->locals = NULL;
    emitter->local_types = NULL;
    emitter->local_imports = NULL;
    emitter->parameters = NULL;
    emitter->parameter_types = NULL;
    emitter->value_count = 0U;
    emitter->local_count = 0U;
    emitter->parameter_count = 0U;
    emitter->phi_count = 0U;
    emitter->effect_out = NULL;
    emitter->result_out = NULL;
}

/* Sizes the per-function tables from the MIR and records each value's definition. */
static bool r_llvm_prepare_function(RLlvmEmitter *emitter, const RMirFunction *mir) {
    size_t max_value = 0U;
    size_t max_local = 0U;
    size_t phis = 0U;
    uint32_t block_index;

    for (block_index = 0U; block_index < mir->block_count; ++block_index) {
        const RMirBlock *block = r_llvm_block(emitter, block_index);
        uint32_t index;
        for (index = 0U; index < block->instruction_count; ++index) {
            const RMirInstruction *instruction = r_llvm_instruction(emitter, block, index);
            max_value = instruction->result > max_value ? instruction->result : max_value;
            if ((instruction->kind == R_MIR_INSTRUCTION_LOCAL) &&
                (instruction->place_ordinal + 1U > max_local)) {
                max_local = instruction->place_ordinal + 1U;
            }
            phis += instruction->kind == R_MIR_INSTRUCTION_PHI ? 1U : 0U;
        }
    }
    emitter->value_count = max_value + 1U;
    emitter->local_count = max_local;
    emitter->parameter_count = emitter->symbol->parameter_count;
    emitter->blocks = r_llvm_allocate(emitter, mir->block_count * sizeof(*emitter->blocks));
    emitter->block_ends = r_llvm_allocate(emitter, mir->block_count * sizeof(*emitter->block_ends));
    emitter->definitions =
        r_llvm_allocate(emitter, emitter->value_count * sizeof(*emitter->definitions));
    emitter->values = r_llvm_allocate(emitter, emitter->value_count * sizeof(*emitter->values));
    emitter->catch_slots =
        r_llvm_allocate(emitter, emitter->value_count * sizeof(*emitter->catch_slots));
    emitter->value_flags =
        r_llvm_allocate(emitter, emitter->value_count * sizeof(*emitter->value_flags));
    emitter->frame_scalars = r_llvm_allocate(emitter, emitter->value_count);
    emitter->forwarded = r_llvm_allocate(emitter, emitter->value_count);
    if ((emitter->blocks == NULL) || (emitter->block_ends == NULL) ||
        (emitter->definitions == NULL) || (emitter->values == NULL) ||
        (emitter->catch_slots == NULL) || (emitter->value_flags == NULL) ||
        (emitter->frame_scalars == NULL) || (emitter->forwarded == NULL)) {
        return false;
    }
    if ((phis != 0U) &&
        (((emitter->phis = r_llvm_allocate(emitter, phis * sizeof(*emitter->phis))) == NULL) ||
         ((emitter->phi_blocks = r_llvm_allocate(emitter, phis * sizeof(*emitter->phi_blocks))) ==
          NULL))) {
        return false;
    }
    /* A branch or jump leaves at most two edges per block. */
    emitter->edges =
        r_llvm_allocate(emitter, (2U * (size_t)mir->block_count + 1U) * sizeof(*emitter->edges));
    if (emitter->edges == NULL) {
        return false;
    }
    if ((max_local != 0U) &&
        (((emitter->locals = r_llvm_allocate(emitter, max_local * sizeof(*emitter->locals))) ==
          NULL) ||
         ((emitter->local_types =
               r_llvm_allocate(emitter, max_local * sizeof(*emitter->local_types))) == NULL) ||
         ((emitter->local_flags =
               r_llvm_allocate(emitter, max_local * sizeof(*emitter->local_flags))) == NULL) ||
         ((emitter->local_imports = r_llvm_allocate(emitter, max_local)) == NULL))) {
        return false;
    }
    if ((emitter->parameter_count != 0U) &&
        (((emitter->parameters = r_llvm_allocate(
               emitter, emitter->parameter_count * sizeof(*emitter->parameters))) == NULL) ||
         ((emitter->parameter_types = r_llvm_allocate(
               emitter, emitter->parameter_count * sizeof(*emitter->parameter_types))) == NULL) ||
         ((emitter->parameter_flags = r_llvm_allocate(
               emitter, emitter->parameter_count * sizeof(*emitter->parameter_flags))) == NULL))) {
        return false;
    }
    for (block_index = 0U; block_index < mir->block_count; ++block_index) {
        const RMirBlock *block = r_llvm_block(emitter, block_index);
        uint32_t index;
        for (index = 0U; index < block->instruction_count; ++index) {
            const RMirInstruction *instruction = r_llvm_instruction(emitter, block, index);
            if (instruction->result != R_MIR_VALUE_ID_INVALID) {
                emitter->definitions[instruction->result] = instruction;
            }
        }
    }
    return true;
}

/* Storage of the places: a scalar parameter is stored into a slot, a parameter in memory is the
   copy its caller passed, a local is a slot of its layout. */
static bool r_llvm_prepare_places(RLlvmEmitter *emitter) {
    uint32_t block_index;
    const unsigned first = (emitter->effect_out != NULL) || (emitter->result_out != NULL) ? 1U : 0U;

    for (block_index = 0U; block_index < emitter->mir->block_count; ++block_index) {
        const RMirBlock *block = r_llvm_block(emitter, block_index);
        uint32_t index;
        for (index = 0U; index < block->instruction_count; ++index) {
            const RMirInstruction *instruction = r_llvm_instruction(emitter, block, index);
            uint32_t size = 0U;
            uint32_t align = 0U;
            if (instruction->kind == R_MIR_INSTRUCTION_PARAMETER) {
                const uint32_t ordinal = instruction->place_ordinal;
                LLVMValueRef incoming;
                if (((size_t)ordinal >= emitter->parameter_count) ||
                    (emitter->parameters[ordinal] != NULL)) {
                    return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
                }
                emitter->parameter_types[ordinal] = instruction->type;
                (void)r_llvm_place_flag(emitter, ordinal, true);
                if (emitter->frame != NULL) {
                    /* The frame initializer moved the argument into this slot. */
                    if (!r_llvm_layout(emitter, instruction->type, &size, &align)) {
                        return false;
                    }
                    emitter->parameters[ordinal] =
                        r_llvm_entry_alloca(emitter, size, align, "parameter");
                    continue;
                }
                incoming = LLVMGetParam(emitter->function, first + ordinal);
                if (r_llvm_scalar_type(emitter, instruction->type) == NULL) {
                    emitter->parameters[ordinal] = incoming;
                    continue;
                }
                if (!r_llvm_layout(emitter, instruction->type, &size, &align)) {
                    return false;
                }
                emitter->parameters[ordinal] =
                    r_llvm_entry_alloca(emitter, size, align, "parameter");
                r_llvm_store_scalar(
                    emitter, instruction->type, incoming, emitter->parameters[ordinal]);
            } else if (instruction->kind == R_MIR_INSTRUCTION_LOCAL) {
                const RSemanticSymbol *local = r_llvm_semantic_symbol(emitter, instruction->symbol);
                const uint32_t ordinal = instruction->place_ordinal;
                if (emitter->locals[ordinal] != NULL) {
                    continue;
                }
                if ((local != NULL) && (local->is_static || local->is_import ||
                                        (local->kind == R_SEMANTIC_SYMBOL_MODULE_OBJECT))) {
                    /* A static object: the place is its global, which is never dropped here; an
                       imported C object is the C object itself (R-FFI-0022). */
                    LLVMValueRef global = local->is_import
                                              ? r_llvm_ffi_object(emitter, instruction->symbol)
                                              : r_llvm_static_place(emitter, instruction->symbol);
                    if (global == NULL) {
                        return false;
                    }
                    emitter->local_imports[ordinal] = local->is_import ? 1U : 0U;
                    emitter->local_types[ordinal] = instruction->type;
                    emitter->locals[ordinal] = global;
                    continue;
                }
                if (!r_llvm_layout(emitter, instruction->type, &size, &align)) {
                    return false;
                }
                emitter->local_types[ordinal] = instruction->type;
                emitter->locals[ordinal] = r_llvm_local_in_carrier(emitter, instruction);
                if (emitter->locals[ordinal] == NULL) {
                    if (emitter->status != R_FRONTEND_OK) {
                        return false;
                    }
                    emitter->locals[ordinal] = r_llvm_entry_alloca(emitter, size, align, "local");
                }
            }
        }
    }
    return true;
}

/* The MIR blocks of the function, in order, into their LLVM blocks. */
static bool r_llvm_emit_blocks(RLlvmEmitter *emitter) {
    uint32_t block_index;

    for (block_index = 0U; block_index < emitter->mir->block_count; ++block_index) {
        const RMirBlock *block = r_llvm_block(emitter, block_index);
        uint32_t index;
        LLVMPositionBuilderAtEnd(emitter->builder, emitter->blocks[block_index]);
        emitter->current_block = block_index;
        for (index = 0U; index < block->instruction_count; ++index) {
            const RMirInstruction *instruction = r_llvm_instruction(emitter, block, index);
            if (r_llvm_is_terminator(instruction->kind)) {
                emitter->block_ends[block_index] = LLVMGetInsertBlock(emitter->builder);
            }
            if (!r_llvm_emit_instruction(emitter, instruction)) {
                return false;
            }
            if (!r_llvm_is_terminator(instruction->kind) &&
                (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(emitter->builder)) != NULL)) {
                /* The instruction ended the path (a panic that ends the process): the rest of
                   the block is not reached. */
                break;
            }
        }
        if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(emitter->builder)) == NULL) {
            (void)LLVMBuildUnreachable(emitter->builder);
        }
    }
    return true;
}

/* A frame address of the step, at the same offset in the frame of the current function. */
static LLVMValueRef
r_llvm_frame_rebase(RLlvmEmitter *emitter, LLVMValueRef step_frame, LLVMValueRef address) {
    if ((address != NULL) && (LLVMIsAGetElementPtrInst(address) != NULL) &&
        (LLVMGetOperand(address, 0U) == step_frame)) {
        return r_llvm_frame_address(
            emitter, (uint32_t)LLVMConstIntGetZExtValue(LLVMGetOperand(address, 1U)));
    }
    return address;
}

/* Moves every frame address the tables of the function hold into the current function. */
static void r_llvm_frame_rebase_all(RLlvmEmitter *emitter, LLVMValueRef step_frame) {
    size_t index;

    for (index = 0U; index < emitter->value_count; ++index) {
        emitter->values[index] = r_llvm_frame_rebase(emitter, step_frame, emitter->values[index]);
        emitter->value_flags[index] =
            r_llvm_frame_rebase(emitter, step_frame, emitter->value_flags[index]);
        emitter->catch_slots[index] =
            r_llvm_frame_rebase(emitter, step_frame, emitter->catch_slots[index]);
    }
    for (index = 0U; index < emitter->local_count; ++index) {
        emitter->locals[index] = r_llvm_frame_rebase(emitter, step_frame, emitter->locals[index]);
        emitter->local_flags[index] =
            r_llvm_frame_rebase(emitter, step_frame, emitter->local_flags[index]);
    }
    for (index = 0U; index < emitter->parameter_count; ++index) {
        emitter->parameters[index] =
            r_llvm_frame_rebase(emitter, step_frame, emitter->parameters[index]);
        emitter->parameter_flags[index] =
            r_llvm_frame_rebase(emitter, step_frame, emitter->parameter_flags[index]);
    }
    emitter->finally_stack = r_llvm_frame_rebase(emitter, step_frame, emitter->finally_stack);
    emitter->finally_depth = r_llvm_frame_rebase(emitter, step_frame, emitter->finally_depth);
    emitter->pending = r_llvm_frame_rebase(emitter, step_frame, emitter->pending);
    emitter->pending_depth = r_llvm_frame_rebase(emitter, step_frame, emitter->pending_depth);
}

/* Starts the body of a frame helper (the drop or the initializer) over its frame parameter. */
static void
r_llvm_begin_frame_helper(RLlvmEmitter *emitter, LLVMValueRef function, LLVMValueRef step_frame) {
    LLVMBasicBlockRef entry;
    LLVMBasicBlockRef body;

    /* The tables hold addresses in the frame of the function written last. */
    (void)step_frame;
    step_frame = emitter->frame;
    emitter->function = function;
    emitter->frame = LLVMGetParam(function, 0U);
    entry = LLVMAppendBasicBlockInContext(emitter->context, function, "entry");
    body = LLVMAppendBasicBlockInContext(emitter->context, function, "");
    LLVMPositionBuilderAtEnd(emitter->builder, entry);
    (void)LLVMBuildBr(emitter->builder, body);
    LLVMPositionBuilderAtEnd(emitter->builder, body);
    r_llvm_frame_rebase_all(emitter, step_frame);
}

/* r_async_frame_drop_N: the pending payloads and every initialized slot of the frame. */
static bool r_llvm_emit_frame_drop(RLlvmEmitter *emitter, RSymbolId id, LLVMValueRef step_frame) {
    r_llvm_begin_frame_helper(emitter, emitter->async_drops[id], step_frame);
    if (!r_llvm_drop_storage(emitter)) {
        return false;
    }
    (void)LLVMBuildRetVoid(emitter->builder);
    return true;
}

/* The frame initializer: a cleared frame in its first state with its parameters taken from the
   context of the start (async.c). */
static bool
r_llvm_emit_frame_initializer(RLlvmEmitter *emitter, RSymbolId id, LLVMValueRef step_frame) {
    LLVMValueRef context;
    size_t index;

    r_llvm_begin_frame_helper(emitter, emitter->async_initializers[id], step_frame);
    context = LLVMGetParam(emitter->function, 1U);
    (void)LLVMBuildMemSet(emitter->builder,
                          emitter->frame,
                          LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                          r_llvm_u64(emitter, emitter->frame_size),
                          1U);
    (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 1U), emitter->frame);
    for (index = 0U; index < emitter->frame_one_count; ++index) {
        (void)LLVMBuildStore(emitter->builder,
                             LLVMConstInt(r_llvm_int(emitter, 8U), 1U, 0),
                             r_llvm_frame_address(emitter, emitter->frame_ones[index]));
    }
    for (index = 0U; index < emitter->parameter_count; ++index) {
        const RTypeId type = emitter->parameter_types[index];
        LLVMValueRef source = LLVMBuildLoad2(emitter->builder,
                                             r_llvm_pointer(emitter),
                                             r_llvm_byte_offset(emitter, context, 16U * index),
                                             "");
        LLVMValueRef flag = LLVMBuildLoad2(emitter->builder,
                                           r_llvm_pointer(emitter),
                                           r_llvm_byte_offset(emitter, context, 16U * index + 8U),
                                           "");
        uint32_t size = 0U;
        uint32_t align = 0U;
        if ((emitter->parameters[index] == NULL) || !r_llvm_layout(emitter, type, &size, &align)) {
            return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
        if (r_llvm_type_requires_drop(emitter, type)) {
            /* The argument moves into the frame; the caller's storage is no longer initialized. */
            LLVMBasicBlockRef clear =
                LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
            LLVMBasicBlockRef next =
                LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
            if (!r_llvm_call_move(emitter, type, emitter->parameters[index], source)) {
                return false;
            }
            (void)LLVMBuildCondBr(
                emitter->builder, LLVMBuildIsNotNull(emitter->builder, flag, ""), clear, next);
            LLVMPositionBuilderAtEnd(emitter->builder, clear);
            (void)LLVMBuildStore(
                emitter->builder, LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0), flag);
            (void)LLVMBuildBr(emitter->builder, next);
            LLVMPositionBuilderAtEnd(emitter->builder, next);
        } else if (size != 0U) {
            (void)LLVMBuildMemCpy(emitter->builder,
                                  emitter->parameters[index],
                                  1U,
                                  source,
                                  1U,
                                  r_llvm_u64(emitter, size));
        }
    }
    (void)LLVMBuildRetVoid(emitter->builder);
    return true;
}

/* The step of an async function (r_async_step_N): a dispatch on the state of the frame to the
   block it names, then the blocks with every slot in the frame; then the frame's drop and
   initializer, and its size. */
static bool r_llvm_define_async_function(RLlvmEmitter *emitter,
                                         const RMirFunction *mir,
                                         const RSemanticSymbol *symbol) {
    const RSymbolId id = mir->symbol;
    LLVMValueRef step = emitter->functions[id];
    LLVMValueRef step_frame;
    LLVMBasicBlockRef entry;
    LLVMBasicBlockRef dispatch;
    LLVMBasicBlockRef invalid;
    LLVMValueRef choice;
    LLVMValueRef arguments[2];
    uint32_t block_index;
    bool success = false;

    if (mir->block_count == 0U) {
        return r_llvm_unsupported(emitter, "a function without a MIR body");
    }
    if (symbol->parameter_count != 0U) {
        /* Startup arguments of an async entry are taken by hosted_main.c. */
    }
    emitter->mir = mir;
    emitter->symbol = symbol;
    emitter->symbol_id = id;
    emitter->function = step;
    if (!r_llvm_prepare_function(emitter, mir)) {
        goto cleanup;
    }
    emitter->execution = LLVMGetParam(step, 0U);
    emitter->frame = LLVMGetParam(step, 1U);
    emitter->step_result = LLVMGetParam(step, 2U);
    step_frame = emitter->frame;
    emitter->frame_size = 0U;
    emitter->frame_align = 8U;
    emitter->frame_one_count = 0U;
    entry = LLVMAppendBasicBlockInContext(emitter->context, step, "entry");
    dispatch = LLVMAppendBasicBlockInContext(emitter->context, step, "dispatch");
    for (block_index = 0U; block_index < mir->block_count; ++block_index) {
        emitter->blocks[block_index] = LLVMAppendBasicBlockInContext(emitter->context, step, "");
    }
    invalid = LLVMAppendBasicBlockInContext(emitter->context, step, "");
    LLVMPositionBuilderAtEnd(emitter->builder, entry);
    /* The state is the first member of the frame. */
    (void)r_llvm_frame_reserve(emitter, 4U, 4U);
    if (symbol->effect_carrier_type != R_TYPE_ID_INVALID) {
        emitter->effect_out = emitter->step_result;
    } else if (r_llvm_result_in_memory(emitter, symbol)) {
        emitter->result_out = emitter->step_result;
    }
    if (!r_llvm_prepare_places(emitter) || !r_llvm_prepare_finally(emitter)) {
        goto cleanup;
    }
    /* r_async_step_gate_N: the bound of the step on this stack. */
    arguments[0] = r_llvm_stack_entry(emitter, id);
    arguments[1] = r_llvm_span(emitter, symbol->name_span);
    if ((arguments[0] == NULL) || (arguments[1] == NULL) ||
        (r_llvm_call_runtime(emitter, "r_runtime_stack_require", arguments, 2U, NULL) == NULL)) {
        goto cleanup;
    }
    (void)LLVMBuildBr(emitter->builder, dispatch);
    LLVMPositionBuilderAtEnd(emitter->builder, dispatch);
    choice = LLVMBuildSwitch(
        emitter->builder,
        LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), emitter->frame, ""),
        invalid,
        mir->block_count);
    for (block_index = 0U; block_index < mir->block_count; ++block_index) {
        LLVMAddCase(choice, r_llvm_u32(emitter, block_index + 1U), emitter->blocks[block_index]);
    }
    LLVMPositionBuilderAtEnd(emitter->builder, invalid);
    if (!r_llvm_panic(emitter,
                      "R_RUNTIME_PANIC_CONTRACT_VIOLATION",
                      symbol->name_span,
                      R_MIR_BLOCK_ID_INVALID) ||
        !r_llvm_emit_blocks(emitter)) {
        goto cleanup;
    }
    /* The runtime drops the frame after the step returns, at this depth of the stack. */
    if (!r_llvm_add_stack_edge(emitter, step, emitter->async_drops[id]) ||
        !r_llvm_emit_frame_drop(emitter, id, step_frame) ||
        !r_llvm_emit_frame_initializer(emitter, id, step_frame)) {
        goto cleanup;
    }
    LLVMSetInitializer(emitter->async_frame_sizes[id], r_llvm_u64(emitter, emitter->frame_size));
    success = emitter->status == R_FRONTEND_OK;

cleanup:
    emitter->frame = NULL;
    emitter->execution = NULL;
    emitter->step_result = NULL;
    r_llvm_release_function_state(emitter);
    return success;
}

static bool r_llvm_define_sync(RLlvmEmitter *emitter,
                               const RMirFunction *mir,
                               const RSemanticSymbol *symbol,
                               LLVMValueRef function);

static bool r_llvm_define_function(RLlvmEmitter *emitter, const RMirFunction *mir) {
    const RSemanticSymbol *symbol = r_llvm_semantic_symbol(emitter, mir->symbol);
    bool success = false;

    if ((symbol != NULL) && symbol->is_async &&
        (r_semantic_dyn_dispatcher(emitter->frontend, mir->symbol) ||
         r_semantic_function_value_dispatcher(emitter->frontend, mir->symbol))) {
        return true;
    }
    if ((symbol == NULL) || (emitter->functions[mir->symbol] == NULL)) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    if (symbol->format_recipe != 0U) {
        success = r_llvm_define_format_function(emitter, mir->symbol, symbol);
        emitter->function = NULL;
        return success;
    }
    if (symbol->json_operation != 0U) {
        success = r_llvm_define_json_function(emitter, mir->symbol, symbol);
        emitter->function = NULL;
        return success;
    }
    if (r_semantic_dyn_dispatcher(emitter->frontend, mir->symbol) ||
        r_semantic_function_value_dispatcher(emitter->frontend, mir->symbol)) {
        success = r_llvm_define_dispatcher(emitter, mir->symbol, symbol);
        emitter->function = NULL;
        return success;
    }
    if (symbol->is_async) {
        return r_llvm_define_async_function(emitter, mir, symbol) &&
               ((emitter->direct_twins[mir->symbol] == NULL) ||
                r_llvm_define_sync(emitter, mir, symbol, emitter->direct_twins[mir->symbol]));
    }
    return r_llvm_define_sync(emitter, mir, symbol, emitter->functions[mir->symbol]);
}

/* An ordinary function from its MIR, or the direct twin of an async function whose body never
   suspends (direct.c): the same body over the parameters of the function. */
static bool r_llvm_define_sync(RLlvmEmitter *emitter,
                               const RMirFunction *mir,
                               const RSemanticSymbol *symbol,
                               LLVMValueRef function) {
    LLVMBasicBlockRef entry;
    uint32_t block_index;
    bool success = false;

    if (mir->block_count == 0U) {
        return r_llvm_unsupported(emitter, "a function without a MIR body");
    }
    emitter->mir = mir;
    emitter->symbol = symbol;
    emitter->symbol_id = mir->symbol;
    emitter->function = function;
    if (!r_llvm_prepare_function(emitter, mir)) {
        goto cleanup;
    }
    entry = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "entry");
    for (block_index = 0U; block_index < mir->block_count; ++block_index) {
        emitter->blocks[block_index] =
            LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    }
    LLVMPositionBuilderAtEnd(emitter->builder, entry);
    if (symbol->effect_carrier_type != R_TYPE_ID_INVALID) {
        emitter->effect_out = LLVMGetParam(emitter->function, 0U);
    } else if (r_llvm_result_in_memory(emitter, symbol)) {
        emitter->result_out = LLVMGetParam(emitter->function, 0U);
    }
    if (!r_llvm_prepare_places(emitter) || !r_llvm_prepare_finally(emitter)) {
        goto cleanup;
    }
    (void)LLVMBuildBr(emitter->builder, emitter->blocks[0]);
    if (!r_llvm_emit_blocks(emitter) || !r_llvm_complete_phis(emitter)) {
        goto cleanup;
    }
    success = emitter->status == R_FRONTEND_OK;

cleanup:
    r_llvm_release_function_state(emitter);
    return success;
}

LLVMValueRef r_llvm_unwinding(RLlvmEmitter *emitter) {
    LLVMValueRef threads = LLVMGetNamedGlobal(emitter->module, "r_runtime_unwinding_threads");
    LLVMBasicBlockRef current = LLVMGetInsertBlock(emitter->builder);
    LLVMBasicBlockRef thread =
        LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef join = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMValueRef count;
    LLVMValueRef any;
    LLVMValueRef mine;
    LLVMValueRef phi;
    LLVMValueRef incoming[2];
    LLVMBasicBlockRef blocks[2];

    if (threads == NULL) {
        threads =
            LLVMAddGlobal(emitter->module, r_llvm_int(emitter, 32U), "r_runtime_unwinding_threads");
        LLVMSetLinkage(threads, LLVMExternalLinkage);
    }
    count = LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), threads, "");
    LLVMSetOrdering(count, LLVMAtomicOrderingMonotonic);
    LLVMSetAlignment(count, 4U);
    any = LLVMBuildICmp(emitter->builder, LLVMIntNE, count, r_llvm_u32(emitter, 0U), "");
    (void)LLVMBuildCondBr(emitter->builder, any, thread, join);
    LLVMPositionBuilderAtEnd(emitter->builder, thread);
    mine = r_llvm_call_runtime(emitter, "r_runtime_unwinding_current_thread", NULL, 0U, NULL);
    if (mine == NULL) {
        return NULL;
    }
    (void)LLVMBuildBr(emitter->builder, join);
    LLVMPositionBuilderAtEnd(emitter->builder, join);
    phi = LLVMBuildPhi(emitter->builder, r_llvm_int(emitter, 1U), "");
    incoming[0] = LLVMConstInt(r_llvm_int(emitter, 1U), 0U, 0);
    incoming[1] = mine;
    blocks[0] = current;
    blocks[1] = thread;
    LLVMAddIncoming(phi, incoming, blocks, 2U);
    return phi;
}

/* Begins a panic of the category and continues at the panic block of the instruction: the
   pending panic is carried there as after an R call (L39, R-ERR-0005). */

/* ---- Program ---- */

/* The functions the program reaches from its entry through calls and function addresses: only
   those are lowered, unless the artifact options ask for every function. */
static bool r_llvm_reachable_functions(RLlvmEmitter *emitter) {
    const RFrontendContext *context = emitter->frontend;
    const RMirFunction **by_symbol;
    RSymbolId *work;
    size_t count = 0U;
    size_t index;
    const RSymbolId entry = r_llvm_entry(emitter);
    bool success = false;

    if (entry == R_SYMBOL_ID_INVALID) {
        /* A program is emitted from its entry; a module without one is no program. */
        return r_llvm_fail(emitter, R_FRONTEND_INVALID_ARGUMENT);
    }
    by_symbol =
        r_llvm_allocate(emitter, (context->semantic_symbol_count + 1U) * sizeof(*by_symbol));
    work = r_llvm_allocate(emitter, (context->semantic_symbol_count + 1U) * sizeof(*work));
    emitter->reachable = r_llvm_allocate(emitter, context->semantic_symbol_count + 1U);
    if ((by_symbol == NULL) || (work == NULL) || (emitter->reachable == NULL)) {
        goto cleanup;
    }
    for (index = 0U; index < context->mir_function_count; ++index) {
        const RMirFunction *mir = &context->mir_functions[index];
        if ((mir->symbol != R_SYMBOL_ID_INVALID) &&
            ((size_t)mir->symbol <= context->semantic_symbol_count)) {
            by_symbol[mir->symbol] = mir;
        }
    }
    emitter->reachable[entry] = 1U;
    work[count++] = entry;
    /* Every extern "C" definition is an export C code may call (R-FFI-0012); with all_functions
       every function is a root. */
    for (index = 0U; index < context->mir_function_count; ++index) {
        const RSymbolId symbol = context->mir_functions[index].symbol;
        if ((symbol != R_SYMBOL_ID_INVALID) && ((size_t)symbol <= context->semantic_symbol_count) &&
            (((emitter->artifact_options != NULL) && emitter->artifact_options->all_functions) ||
             (context->semantic_symbols[(size_t)symbol - 1U].is_extern_c &&
              !context->semantic_symbols[(size_t)symbol - 1U].is_import)) &&
            !emitter->reachable[symbol]) {
            emitter->reachable[symbol] = 1U;
            work[count++] = symbol;
        }
    }
    /* The drop, hash, equality, clone and format functions of aggregates, which the glue and the
       key functions of std.dict call. */
    for (index = 0U; index < context->semantic_aggregate_count; ++index) {
        const RSymbolId hooks[8] = {context->semantic_aggregates[index].drop_function,
                                    context->semantic_aggregates[index].hash_function,
                                    context->semantic_aggregates[index].equal_function,
                                    context->semantic_aggregates[index].clone_function,
                                    context->semantic_aggregates[index].format_function,
                                    context->semantic_aggregates[index].json_marshal_function,
                                    context->semantic_aggregates[index].json_unmarshal_function,
                                    context->semantic_aggregates[index].json_is_zero_function};
        size_t hook;
        for (hook = 0U; hook < 8U; ++hook) {
            const RSymbolId symbol = hooks[hook];
            if ((symbol != R_SYMBOL_ID_INVALID) &&
                ((size_t)symbol <= context->semantic_symbol_count) && (by_symbol[symbol] != NULL) &&
                !emitter->reachable[symbol]) {
                emitter->reachable[symbol] = 1U;
                work[count++] = symbol;
            }
        }
    }
    /* The schema hooks of std.json::schema. */
    for (index = 0U; index < context->json_schema_hook_count; ++index) {
        const RSymbolId symbol = context->json_schema_hooks[index].hook;
        if ((symbol != R_SYMBOL_ID_INVALID) && ((size_t)symbol <= context->semantic_symbol_count) &&
            (by_symbol[symbol] != NULL) && !emitter->reachable[symbol]) {
            emitter->reachable[symbol] = 1U;
            work[count++] = symbol;
        }
    }
    /* The default factories of JSON fields, which the decoders call. */
    for (index = 0U; index < context->semantic_field_count; ++index) {
        const RSymbolId symbol = context->semantic_fields[index].json.default_factory;
        if ((symbol != R_SYMBOL_ID_INVALID) && ((size_t)symbol <= context->semantic_symbol_count) &&
            (by_symbol[symbol] != NULL) && !emitter->reachable[symbol]) {
            emitter->reachable[symbol] = 1U;
            work[count++] = symbol;
        }
    }
    while (count != 0U) {
        const RSymbolId current = work[--count];
        const RMirFunction *mir = by_symbol[current];
        uint32_t block_index;
        size_t target_index;
        if (mir == NULL) {
            continue;
        }
        /* A dispatcher reaches each of its targets. */
        for (target_index = 0U; target_index < context->dyn_target_count; ++target_index) {
            const RSymbolId target = context->dyn_targets[target_index].target;
            if ((context->dyn_targets[target_index].dispatcher == current) &&
                (target != R_SYMBOL_ID_INVALID) &&
                ((size_t)target <= context->semantic_symbol_count) && (by_symbol[target] != NULL) &&
                !emitter->reachable[target]) {
                emitter->reachable[target] = 1U;
                work[count++] = target;
            }
        }
        for (block_index = 0U; block_index < mir->block_count; ++block_index) {
            const RMirBlock *block = &context->mir_blocks[(size_t)mir->first_block + block_index];
            uint32_t instruction_index;
            for (instruction_index = 0U; instruction_index < block->instruction_count;
                 ++instruction_index) {
                const RMirInstruction *instruction =
                    &context
                         ->mir_instructions[(size_t)block->first_instruction + instruction_index];
                const RSymbolId target = instruction->symbol;
                /* The entry of a thread or of a blocking call is reached by its spawn, the
                   initializer of a once by its call. */
                const bool spawn =
                    (instruction->kind == R_MIR_INSTRUCTION_STANDARD_CALL) &&
                    ((instruction->standard_operation == R_STANDARD_CALL_THREAD_SPAWN) ||
                     (instruction->standard_operation == R_STANDARD_CALL_THREAD_SPAWN_SCOPED) ||
                     (instruction->standard_operation == R_STANDARD_CALL_ASYNC_BLOCKING) ||
                     (instruction->standard_operation == R_STANDARD_CALL_SYNC_CALL_ONCE) ||
                     (instruction->standard_operation == R_STANDARD_CALL_SYNC_CALL_ONCE_FORCE) ||
                     (instruction->standard_operation == R_STANDARD_CALL_SYNC_GET_OR_INIT));
                if (((instruction->kind != R_MIR_INSTRUCTION_CALL) &&
                     (instruction->kind != R_MIR_INSTRUCTION_FUNCTION_ADDRESS) &&
                     (instruction->kind != R_MIR_INSTRUCTION_ASYNC_START) && !spawn) ||
                    (target == R_SYMBOL_ID_INVALID) ||
                    ((size_t)target > context->semantic_symbol_count) ||
                    emitter->reachable[target] || (by_symbol[target] == NULL)) {
                    continue;
                }
                emitter->reachable[target] = 1U;
                work[count++] = target;
            }
        }
    }
    success = true;

cleanup:
    r_llvm_free(emitter, by_symbol);
    r_llvm_free(emitter, work);
    return success;
}

static bool r_llvm_create_module(RLlvmEmitter *emitter) {
    LLVMTargetRef target = NULL;
    char *error = NULL;
    char *layout;

    LLVMInitializeAArch64TargetInfo();
    LLVMInitializeAArch64Target();
    LLVMInitializeAArch64TargetMC();
    LLVMInitializeAArch64AsmPrinter();
    if (LLVMGetTargetFromTriple(r_frontend_backend_triple(), &target, &error) != 0) {
        LLVMDisposeMessage(error);
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    emitter->machine = LLVMCreateTargetMachine(target,
                                               r_frontend_backend_triple(),
                                               r_frontend_backend_cpu(),
                                               "",
                                               LLVMCodeGenLevelDefault,
                                               LLVMRelocPIC,
                                               LLVMCodeModelDefault);
    if (emitter->machine == NULL) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    emitter->context = LLVMContextCreate();
    emitter->module = LLVMModuleCreateWithNameInContext("r-program", emitter->context);
    emitter->builder = LLVMCreateBuilderInContext(emitter->context);
    emitter->data = LLVMCreateTargetDataLayout(emitter->machine);
    LLVMSetTarget(emitter->module, r_frontend_backend_triple());
    layout = LLVMCopyStringRepOfTargetData(emitter->data);
    LLVMSetDataLayout(emitter->module, layout);
    LLVMDisposeMessage(layout);
    return true;
}

static void r_llvm_dispose(RLlvmEmitter *emitter) {
    size_t index;

    r_llvm_release_function_state(emitter);
    for (index = 0U; index < emitter->runtime_count; ++index) {
        r_llvm_free(emitter, emitter->runtime[index].parameters);
        r_llvm_free(emitter, emitter->runtime[index].parameter_types);
    }
    r_llvm_free(emitter, emitter->runtime);
    r_llvm_free(emitter, emitter->stack_entries);
    r_llvm_free(emitter, emitter->layouts);
    r_llvm_free(emitter, emitter->drop_glue);
    r_llvm_free(emitter, emitter->statics);
    r_llvm_free(emitter, emitter->move_glue);
    r_llvm_free(emitter, emitter->drop_cursors);
    r_llvm_free(emitter, emitter->drop_counts);
    r_llvm_free(emitter, emitter->drop_nodes);
    r_llvm_free(emitter, emitter->drop_hooks);
    r_llvm_free(emitter, emitter->runtime_callees);
    r_llvm_free(emitter, emitter->key_hashes);
    r_llvm_free(emitter, emitter->async_initializers);
    r_llvm_free(emitter, emitter->async_drops);
    r_llvm_free(emitter, emitter->thread_entries);
    r_llvm_free(emitter, emitter->sync_initializers);
    r_llvm_free(emitter, emitter->clone_glue);
    r_llvm_free(emitter, emitter->clone_written);
    r_llvm_free(emitter, emitter->format_glue);
    r_llvm_free(emitter, emitter->format_written);
    r_llvm_free(emitter, emitter->json_encoders);
    r_llvm_free(emitter, emitter->json_zeros);
    r_llvm_free(emitter, emitter->json_decoders);
    r_llvm_free(emitter, emitter->json_ordinaries);
    r_llvm_free(emitter, emitter->json_streams);
    r_llvm_free(emitter, emitter->c_functions);
    r_llvm_ffi_release(emitter);
    r_llvm_free(emitter, emitter->json_written);
    r_llvm_free(emitter, emitter->broadcast_clones);
    r_llvm_free(emitter, emitter->thread_local_access);
    r_llvm_free(emitter, emitter->thread_local_drop);
    r_llvm_free(emitter, emitter->thread_local_state);
    r_llvm_free(emitter, emitter->thread_moves);
    r_llvm_free(emitter, emitter->thread_drops);
    r_llvm_free(emitter, emitter->async_frame_sizes);
    r_llvm_free(emitter, emitter->frame_ones);
    r_llvm_free(emitter, emitter->key_equals);
    r_llvm_free(emitter, emitter->dict_probes);
    r_llvm_free(emitter, emitter->dict_lookups);
    r_llvm_free(emitter, emitter->stack_edges);
    r_llvm_free(emitter, emitter->recursion_marks);
    r_llvm_free(emitter, emitter->glue);
    r_llvm_free(emitter, emitter->reachable);
    r_llvm_free(emitter, emitter->direct_twins);
    r_llvm_free(emitter, emitter->direct_types);
    r_llvm_free(emitter, emitter->direct_states);
    r_llvm_free(emitter, emitter->mir_by_symbol);
    r_llvm_release_keys(emitter);
    r_llvm_free(emitter, emitter->strings);
    r_llvm_free(emitter, emitter->functions);
    r_llvm_free(emitter, emitter->function_types);
    if (emitter->builder != NULL) {
        LLVMDisposeBuilder(emitter->builder);
    }
    if (emitter->module != NULL) {
        LLVMDisposeModule(emitter->module);
    }
    if (emitter->data != NULL) {
        LLVMDisposeTargetData(emitter->data);
    }
    if (emitter->machine != NULL) {
        LLVMDisposeTargetMachine(emitter->machine);
    }
    if (emitter->context != NULL) {
        LLVMContextDispose(emitter->context);
    }
}

static bool
r_llvm_write(RFrontendWriteFn writer, void *user_data, const char *bytes, size_t length) {
    return writer(user_data, bytes, length);
}

/* The frame remarks that the stack measurement asks the code generator for (stack.c) come from
   the compilation for output as well; they are no diagnostics of the program and stay off stderr.
   Errors and warnings of the code generator are reported. */
/* The LLVM pipeline of the requested level runs on the verified module before the stack bounds
   are measured, so the bounds are those of the optimized code (stack.c). Core R-AM-0003 admits
   every transformation that keeps the observable behaviour; the emitter's IR has none that
   depends on more. */
/* The options of LLVM, set once per process: the prologue-epilogue inserter reports each frame
   (stack.c), loop unswitching may copy a loop for a condition that does not leave it, so that a
   versioned index runs without its check where its test holds (versions.c), and the profile of
   profile-guided optimization is read from the file the first emission that uses one names; a
   later emission naming another file fails. */
static bool r_llvm_set_options(RLlvmEmitter *emitter, const char *profile) {
    static bool set = false;
    static char used_profile[1024];
    char profile_option[1100];
    const char *arguments[4] = {
        "r-front", "-pass-remarks-analysis=prologepilog", "-enable-nontrivial-unswitch", NULL};
    int count = 3;

    if (set) {
        if ((profile != NULL) && (strcmp(profile, used_profile) != 0)) {
            (void)fprintf(stderr,
                          "r-front: this process already optimizes with the profile '%s'\n",
                          used_profile);
            return r_llvm_fail(emitter, R_FRONTEND_INVALID_ARGUMENT);
        }
        return true;
    }
    if (profile != NULL) {
        const int written =
            snprintf(profile_option, sizeof(profile_option), "-pgo-test-profile-file=%s", profile);
        if ((written <= 0) || ((size_t)written >= sizeof(profile_option)) ||
            (strlen(profile) >= sizeof(used_profile))) {
            return r_llvm_fail(emitter, R_FRONTEND_INVALID_ARGUMENT);
        }
        (void)memcpy(used_profile, profile, strlen(profile) + 1U);
        arguments[count++] = profile_option;
    }
    LLVMParseCommandLineOptions(count, arguments, NULL);
    set = true;
    return true;
}

static bool r_llvm_optimize(RLlvmEmitter *emitter, uint32_t level) {
    /* After the default pipeline, inductive range check elimination splits a loop whose index
       checks the optimizer bounded (a constant trip count, or nuw arithmetic it inferred) into a
       part without them; versions.c covers the bounds known only at run time. */
    static const char *const pipelines[] = {
        "default<O1>",
        "default<O2>,function(irce,loop-unroll<O2>,simplifycfg,instcombine<no-verify-fixpoint>)",
        "default<O3>,function(irce,loop-unroll<O3>,simplifycfg,instcombine<no-verify-fixpoint>)"};
    const RFrontendArtifactOptions *artifact = emitter->artifact_options;
    char pipeline[256];
    LLVMPassBuilderOptionsRef options;
    LLVMErrorRef error;
    int written;

    if (!r_llvm_unswitch_versions(emitter, level > 1U)) {
        return false;
    }
    if (level == 0U) {
        return true;
    }
    if (!r_llvm_preserve_stack_functions(emitter) || !r_llvm_import_bitcode(emitter, level)) {
        return false;
    }
    /* B7.2: the profile counters are placed, or the profile read, on the module as the emitter
       wrote it, before any other pass, so that the functions and their control flow match
       between the instrumented build and the build the profile guides. */
    written = snprintf(pipeline,
                       sizeof(pipeline),
                       "%s%s",
                       (artifact != NULL) && artifact->profile_generate ? "pgo-instr-gen,instrprof,"
                       : (artifact != NULL) && (artifact->profile_use != NULL) ? "pgo-instr-use,"
                                                                               : "",
                       pipelines[level - 1U]);
    if ((written <= 0) || ((size_t)written >= sizeof(pipeline))) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    options = LLVMCreatePassBuilderOptions();
    error = LLVMRunPasses(emitter->module, pipeline, emitter->machine, options);
    LLVMDisposePassBuilderOptions(options);
    if (error != NULL) {
        char *message = LLVMGetErrorMessage(error);
        (void)fprintf(stderr, "r-front: the LLVM optimizer failed: %s\n", message);
        LLVMDisposeErrorMessage(message);
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    return true;
}

static void r_llvm_quiet_remarks(LLVMDiagnosticInfoRef info, void *user_data) {
    const LLVMDiagnosticSeverity severity = LLVMGetDiagInfoSeverity(info);
    char *text;

    (void)user_data;
    if ((severity == LLVMDSRemark) || (severity == LLVMDSNote)) {
        return;
    }
    text = LLVMGetDiagInfoDescription(info);
    (void)fprintf(stderr, "r-front: LLVM: %s\n", text == NULL ? "" : text);
    LLVMDisposeMessage(text);
}

RFrontendStatus r_frontend_emit_llvm(const RFrontendContext *context,
                                     const RFrontendArtifactOptions *options,
                                     RFrontendLlvmOutput output,
                                     RFrontendWriteFn writer,
                                     void *user_data) {
    RLlvmEmitter emitter;
    size_t index;
    RFrontendStatus status;

    /* A manifest is given with its length or not at all, as for every artifact of the program
       (r_mir_artifact_options_are_valid). */
    if ((context == NULL) || (writer == NULL) || !context->mir_lowered ||
        ((options != NULL) &&
         (((options->link_manifest == NULL) != (options->link_manifest_length == 0U)) ||
          ((options->target_manifest == NULL) != (options->target_manifest_length == 0U)) ||
          (options->optimization_level > 3U) ||
          ((options->profile_generate || (options->profile_use != NULL)) &&
           (options->optimization_level == 0U)) ||
          (options->profile_generate && (options->profile_use != NULL))))) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    (void)memset(&emitter, 0, sizeof(emitter));
    emitter.frontend = context;
    emitter.status = R_FRONTEND_OK;
    if (!r_llvm_create_module(&emitter)) {
        goto cleanup;
    }
    emitter.functions = r_llvm_allocate(
        &emitter, (context->semantic_symbol_count + 1U) * sizeof(*emitter.functions));
    emitter.function_types = r_llvm_allocate(
        &emitter, (context->semantic_symbol_count + 1U) * sizeof(*emitter.function_types));
    emitter.layouts =
        r_llvm_allocate(&emitter, (context->semantic_type_count + 1U) * sizeof(*emitter.layouts));
    emitter.strings =
        r_llvm_allocate(&emitter, (context->intern_count + 1U) * sizeof(*emitter.strings));
    emitter.drop_glue =
        r_llvm_allocate(&emitter, (context->semantic_type_count + 1U) * sizeof(*emitter.drop_glue));
    emitter.statics =
        r_llvm_allocate(&emitter, (context->semantic_symbol_count + 1U) * sizeof(*emitter.statics));
    emitter.move_glue =
        r_llvm_allocate(&emitter, (context->semantic_type_count + 1U) * sizeof(*emitter.move_glue));
    emitter.drop_cursors = r_llvm_allocate(
        &emitter, (context->semantic_type_count + 1U) * sizeof(*emitter.drop_cursors));
    emitter.drop_counts = r_llvm_allocate(
        &emitter, (context->semantic_type_count + 1U) * sizeof(*emitter.drop_counts));
    emitter.drop_nodes = r_llvm_allocate(
        &emitter, (context->semantic_type_count + 1U) * sizeof(*emitter.drop_nodes));
    emitter.drop_hooks = r_llvm_allocate(
        &emitter, (context->semantic_type_count + 1U) * sizeof(*emitter.drop_hooks));
    emitter.async_initializers = r_llvm_allocate(
        &emitter, (context->semantic_symbol_count + 1U) * sizeof(*emitter.async_initializers));
    emitter.async_drops = r_llvm_allocate(
        &emitter, (context->semantic_symbol_count + 1U) * sizeof(*emitter.async_drops));
    emitter.async_frame_sizes = r_llvm_allocate(
        &emitter, (context->semantic_symbol_count + 1U) * sizeof(*emitter.async_frame_sizes));
    emitter.thread_local_access = r_llvm_allocate(
        &emitter, (context->semantic_symbol_count + 1U) * sizeof(*emitter.thread_local_access));
    emitter.thread_local_drop = r_llvm_allocate(
        &emitter, (context->semantic_symbol_count + 1U) * sizeof(*emitter.thread_local_drop));
    emitter.thread_local_state = r_llvm_allocate(
        &emitter, (context->semantic_symbol_count + 1U) * sizeof(*emitter.thread_local_state));
    emitter.clone_glue = r_llvm_allocate(
        &emitter, (context->semantic_type_count + 1U) * sizeof(*emitter.clone_glue));
    emitter.clone_written = r_llvm_allocate(
        &emitter, (context->semantic_type_count + 1U) * sizeof(*emitter.clone_written));
    emitter.format_glue = r_llvm_allocate(
        &emitter, (context->semantic_type_count + 1U) * sizeof(*emitter.format_glue));
    emitter.format_written = r_llvm_allocate(
        &emitter, (context->semantic_type_count + 1U) * sizeof(*emitter.format_written));
    emitter.json_encoders = r_llvm_allocate(
        &emitter, (context->semantic_type_count + 1U) * sizeof(*emitter.json_encoders));
    emitter.json_zeros = r_llvm_allocate(
        &emitter, (context->semantic_type_count + 1U) * sizeof(*emitter.json_zeros));
    emitter.json_decoders = r_llvm_allocate(
        &emitter, (context->semantic_type_count + 1U) * sizeof(*emitter.json_decoders));
    emitter.json_ordinaries = r_llvm_allocate(
        &emitter, (context->semantic_type_count + 1U) * sizeof(*emitter.json_ordinaries));
    emitter.artifact_options = options;
    emitter.c_functions = r_llvm_allocate(
        &emitter, (context->semantic_symbol_count + 1U) * sizeof(*emitter.c_functions));
    emitter.json_streams = r_llvm_allocate(
        &emitter, (context->semantic_type_count + 1U) * sizeof(*emitter.json_streams));
    emitter.json_written = r_llvm_allocate(
        &emitter, (context->semantic_type_count + 1U) * sizeof(*emitter.json_written));
    emitter.broadcast_clones = r_llvm_allocate(
        &emitter, (context->semantic_type_count + 1U) * sizeof(*emitter.broadcast_clones));
    emitter.sync_initializers = r_llvm_allocate(
        &emitter, (context->semantic_symbol_count + 1U) * sizeof(*emitter.sync_initializers));
    emitter.thread_entries = r_llvm_allocate(
        &emitter, (context->semantic_symbol_count + 1U) * sizeof(*emitter.thread_entries));
    emitter.thread_moves = r_llvm_allocate(
        &emitter, (context->semantic_symbol_count + 1U) * sizeof(*emitter.thread_moves));
    emitter.thread_drops = r_llvm_allocate(
        &emitter, (context->semantic_symbol_count + 1U) * sizeof(*emitter.thread_drops));
    emitter.key_hashes = r_llvm_allocate(
        &emitter, (context->semantic_type_count + 1U) * sizeof(*emitter.key_hashes));
    emitter.key_equals = r_llvm_allocate(
        &emitter, (context->semantic_type_count + 1U) * sizeof(*emitter.key_equals));
    emitter.dict_probes = r_llvm_allocate(
        &emitter, (context->semantic_type_count + 1U) * sizeof(*emitter.dict_probes));
    emitter.dict_lookups = r_llvm_allocate(
        &emitter, (context->semantic_type_count + 1U) * sizeof(*emitter.dict_lookups));
    emitter.direct_twins = r_llvm_allocate(
        &emitter, (context->semantic_symbol_count + 1U) * sizeof(*emitter.direct_twins));
    emitter.direct_types = r_llvm_allocate(
        &emitter, (context->semantic_symbol_count + 1U) * sizeof(*emitter.direct_types));
    emitter.direct_states = r_llvm_allocate(&emitter, context->semantic_symbol_count + 1U);
    emitter.mir_by_symbol = r_llvm_allocate(
        &emitter, (context->semantic_symbol_count + 1U) * sizeof(*emitter.mir_by_symbol));
    if ((emitter.functions == NULL) || (emitter.function_types == NULL) ||
        (emitter.layouts == NULL) || (emitter.strings == NULL) || (emitter.drop_glue == NULL) ||
        (emitter.move_glue == NULL) || (emitter.statics == NULL) ||
        (emitter.drop_cursors == NULL) || (emitter.drop_counts == NULL) ||
        (emitter.drop_nodes == NULL) || (emitter.drop_hooks == NULL) ||
        (emitter.key_hashes == NULL) || (emitter.key_equals == NULL) ||
        (emitter.dict_probes == NULL) || (emitter.dict_lookups == NULL) ||
        (emitter.async_initializers == NULL) || (emitter.async_drops == NULL) ||
        (emitter.async_frame_sizes == NULL) || (emitter.thread_entries == NULL) ||
        (emitter.thread_moves == NULL) || (emitter.thread_drops == NULL) ||
        (emitter.thread_local_access == NULL) || (emitter.thread_local_drop == NULL) ||
        (emitter.thread_local_state == NULL) || (emitter.sync_initializers == NULL) ||
        (emitter.clone_glue == NULL) || (emitter.clone_written == NULL) ||
        (emitter.format_glue == NULL) || (emitter.format_written == NULL) ||
        (emitter.json_encoders == NULL) || (emitter.json_zeros == NULL) ||
        (emitter.json_decoders == NULL) || (emitter.json_ordinaries == NULL) ||
        (emitter.json_streams == NULL) || (emitter.json_written == NULL) ||
        (emitter.c_functions == NULL) || (emitter.broadcast_clones == NULL) ||
        (emitter.direct_twins == NULL) || (emitter.direct_types == NULL) ||
        (emitter.direct_states == NULL) || (emitter.mir_by_symbol == NULL)) {
        goto cleanup;
    }
    if (!r_llvm_prepare_keys(&emitter) || !r_llvm_reachable_functions(&emitter)) {
        goto cleanup;
    }
    for (index = 0U; index < context->mir_function_count; ++index) {
        const RMirFunction *mir = &context->mir_functions[index];
        if (emitter.reachable[mir->symbol] && !r_llvm_declare_function(&emitter, mir->symbol)) {
            goto cleanup;
        }
    }
    if (!r_llvm_prepare_direct_calls(&emitter)) {
        goto cleanup;
    }
    for (index = 0U; index < context->mir_function_count; ++index) {
        const RMirFunction *mir = &context->mir_functions[index];
        if (emitter.reachable[mir->symbol] &&
            !context->semantic_symbols[(size_t)mir->symbol - 1U].is_import &&
            !r_llvm_define_function(&emitter, mir)) {
            goto cleanup;
        }
    }
    if (!r_llvm_emit_thread_locals(&emitter) || !r_llvm_emit_hosted_main(&emitter) ||
        !r_llvm_emit_thread_entries(&emitter) || !r_llvm_emit_sync_initializers(&emitter) ||
        !r_llvm_emit_clone_glue(&emitter) || !r_llvm_emit_json_encoders(&emitter) ||
        !r_llvm_emit_json_streams(&emitter) || !r_llvm_emit_json_decoders(&emitter) ||
        !r_llvm_emit_c_exports(&emitter) || !r_llvm_emit_format_glue(&emitter) ||
        !r_llvm_emit_pending_glue(&emitter)) {
        goto cleanup;
    }
    {
        char *message = NULL;
        if (LLVMVerifyModule(emitter.module, LLVMReturnStatusAction, &message) != 0) {
            (void)fprintf(stderr, "r-front: LLVM module verification failed:\n%s\n", message);
            LLVMDisposeMessage(message);
            (void)r_llvm_fail(&emitter, R_FRONTEND_INTERNAL_ERROR);
            goto cleanup;
        }
        LLVMDisposeMessage(message);
    }
    if (!r_llvm_set_options(&emitter, options == NULL ? NULL : options->profile_use) ||
        !r_llvm_check_indirect_calls(&emitter) ||
        !r_llvm_optimize(&emitter, options == NULL ? 0U : options->optimization_level) ||
        !r_llvm_measure_stack(&emitter)) {
        goto cleanup;
    }
    if (output == R_FRONTEND_LLVM_IR) {
        char *text;
        r_llvm_source_report(&emitter);
        text = LLVMPrintModuleToString(emitter.module);
        const bool written = r_llvm_write(writer, user_data, text, strlen(text));
        LLVMDisposeMessage(text);
        if (!written) {
            (void)r_llvm_fail(&emitter, R_FRONTEND_IO_ERROR);
        }
    } else {
        LLVMMemoryBufferRef buffer = NULL;
        char *error = NULL;
        const LLVMDiagnosticHandler previous_handler =
            LLVMContextGetDiagnosticHandler(emitter.context);
        void *const previous_context = LLVMContextGetDiagnosticContext(emitter.context);
        int failed;
        LLVMContextSetDiagnosticHandler(emitter.context, r_llvm_quiet_remarks, NULL);
        failed = LLVMTargetMachineEmitToMemoryBuffer(
            emitter.machine, emitter.module, LLVMObjectFile, &error, &buffer);
        LLVMContextSetDiagnosticHandler(emitter.context, previous_handler, previous_context);
        if (failed != 0) {
            (void)fprintf(stderr, "r-front: LLVM code generation failed: %s\n", error);
            LLVMDisposeMessage(error);
            (void)r_llvm_fail(&emitter, R_FRONTEND_INTERNAL_ERROR);
        } else {
            if (!r_llvm_write(
                    writer, user_data, LLVMGetBufferStart(buffer), LLVMGetBufferSize(buffer))) {
                (void)r_llvm_fail(&emitter, R_FRONTEND_IO_ERROR);
            }
            LLVMDisposeMemoryBuffer(buffer);
        }
    }

cleanup:
    status = emitter.status;
    r_llvm_dispose(&emitter);
    return status;
}

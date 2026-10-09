#include "standard_internal.h"

#include "standard_async_sync.h"
#include "standard_sync.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* std.sync (R-SLIB-SYNC), as the C17 emitter lowers it (standard_sync_*.inc, the std.sync parts of
   r_c17_emit_async_simple_call and r_c17_emit_async_sync_call).

   Types: a storage mutex<T>, rw_lock<T> or once_lock<T> is {native r_runtime; T r_value}; a guard
   is its native structure; an outcome of std_sync.h is {uint32_t r_tag; union {P r_value;}}, P the
   value or, for a lock outcome, the guard of the same value type, present in the variants of the
   outcome's payload mask. The library moves and destroys storages and guards; outcomes move their
   payload by its glue. */

typedef struct RLlvmSyncNative {
    const char *name;
    const char *native;
    const char *move;
    const char *destroy;
    bool storage;
} RLlvmSyncNative;

static const RLlvmSyncNative r_llvm_sync_natives[] = {
    {"std.sync::mutex",
     "RStdSyncMutex",
     "r_library_internal_sync_mutex_move",
     "r_std_sync_mutex_destroy",
     true},
    {"std.sync::rw_lock",
     "RStdSyncRwLock",
     "r_library_internal_sync_rw_lock_move",
     "r_std_sync_rw_lock_destroy",
     true},
    {"std.sync::once_lock",
     "RStdSyncOnceLock",
     "r_library_internal_sync_once_lock_move",
     "r_std_sync_once_lock_destroy",
     true},
    {"std.sync::mutex_guard",
     "RStdSyncMutexGuard",
     "r_library_internal_sync_mutex_guard_move",
     "r_std_sync_mutex_guard_destroy",
     false},
    {"std.sync::rw_read_guard",
     "RStdSyncRwReadGuard",
     "r_library_internal_sync_rw_read_guard_move",
     "r_std_sync_rw_read_guard_destroy",
     false},
    {"std.sync::rw_write_guard",
     "RStdSyncRwWriteGuard",
     "r_library_internal_sync_rw_write_guard_move",
     "r_std_sync_rw_write_guard_destroy",
     false},
};

static const RLlvmSyncNative *r_llvm_sync_native(RLlvmEmitter *emitter, RTypeId id) {
    const RSemanticType *type = r_llvm_type(emitter, r_llvm_value_type(emitter, id));
    size_t index;

    if ((type == NULL) || (type->kind != R_SEMANTIC_TYPE_STANDARD) ||
        (type->flags != R_SEMANTIC_TYPE_FLAG_NONE) || (type->base == R_TYPE_ID_INVALID) ||
        (type->second != R_TYPE_ID_INVALID)) {
        return NULL;
    }
    for (index = 0U; index < sizeof(r_llvm_sync_natives) / sizeof(r_llvm_sync_natives[0]);
         ++index) {
        if (r_llvm_standard_named(emitter, id, r_llvm_sync_natives[index].name)) {
            return &r_llvm_sync_natives[index];
        }
    }
    return NULL;
}

static const RStandardSyncOutcome *r_llvm_sync_outcome(RLlvmEmitter *emitter, RTypeId id) {
    const RSemanticType *type = r_llvm_type(emitter, r_llvm_value_type(emitter, id));
    size_t index;

    if ((type == NULL) || (type->kind != R_SEMANTIC_TYPE_STANDARD) ||
        (type->base == R_TYPE_ID_INVALID) || (type->second != R_TYPE_ID_INVALID)) {
        return NULL;
    }
    for (index = 0U; r_standard_sync_outcome_at(index) != NULL; ++index) {
        if (r_llvm_standard_named(emitter, id, r_standard_sync_outcome_at(index)->name)) {
            return r_standard_sync_outcome_at(index);
        }
    }
    return NULL;
}

/* The payload of an outcome: its value, or the guard of that value for a lock outcome
   (r_c17_sync_payload_type); the guard type exists when the program binds the payload. */
static RTypeId r_llvm_sync_payload(RLlvmEmitter *emitter, RTypeId id) {
    const RStandardSyncOutcome *schema = r_llvm_sync_outcome(emitter, id);
    const RSemanticType *type = r_llvm_type(emitter, r_llvm_value_type(emitter, id));
    size_t index;

    if ((schema == NULL) || (type == NULL)) {
        return R_TYPE_ID_INVALID;
    }
    if (schema->guard == NULL) {
        return type->base;
    }
    for (index = 0U; index < emitter->frontend->semantic_type_count; ++index) {
        const RSemanticType *candidate = &emitter->frontend->semantic_types[index];
        if ((candidate->kind == R_SEMANTIC_TYPE_STANDARD) && (candidate->base == type->base) &&
            (candidate->second == R_TYPE_ID_INVALID) &&
            r_llvm_standard_named(emitter, (RTypeId)(index + 1U), schema->guard)) {
            return (RTypeId)(index + 1U);
        }
    }
    return R_TYPE_ID_INVALID;
}

bool r_llvm_sync_tagged(RLlvmEmitter *emitter, RTypeId id) {
    return r_llvm_sync_outcome(emitter, id) != NULL;
}

/* The payload type of a variant of an outcome, none for a variant without one. */
RTypeId r_llvm_sync_variant_payload(RLlvmEmitter *emitter, RTypeId id, uint64_t tag) {
    const RStandardSyncOutcome *schema = r_llvm_sync_outcome(emitter, id);

    if ((schema == NULL) || (tag >= schema->variant_count) ||
        (((schema->payload_mask >> tag) & 1U) == 0U)) {
        return R_TYPE_ID_INVALID;
    }
    return r_llvm_sync_payload(emitter, id);
}

/* The size and alignment of the native structure of a guard of the outcome when the program
   never names the guard type. */
static bool r_llvm_sync_guard_native_layout(RLlvmEmitter *emitter,
                                            const RStandardSyncOutcome *schema,
                                            uint32_t *size,
                                            uint32_t *align) {
    size_t index;

    for (index = 0U; index < sizeof(r_llvm_sync_natives) / sizeof(r_llvm_sync_natives[0]);
         ++index) {
        if (strcmp(r_llvm_sync_natives[index].name, schema->guard) == 0) {
            return r_llvm_runtime_layout(emitter, r_llvm_sync_natives[index].native, size, align);
        }
    }
    return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
}

static uint32_t r_llvm_sync_align_up(uint32_t value, uint32_t align) {
    return ((value + align - 1U) / align) * align;
}

/* The offset of r_value in a storage. */
static bool r_llvm_sync_value_offset(RLlvmEmitter *emitter, RTypeId id, uint32_t *offset) {
    const RLlvmSyncNative *native = r_llvm_sync_native(emitter, id);
    const RSemanticType *type = r_llvm_type(emitter, r_llvm_value_type(emitter, id));
    uint32_t native_size = 0U;
    uint32_t native_align = 0U;
    uint32_t size = 0U;
    uint32_t align = 0U;

    if ((native == NULL) || !native->storage || (type == NULL) ||
        !r_llvm_runtime_layout(emitter, native->native, &native_size, &native_align) ||
        !r_llvm_layout(emitter, type->base, &size, &align)) {
        return (native == NULL) ? r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR) : false;
    }
    *offset = r_llvm_sync_align_up(native_size, align == 0U ? 1U : align);
    return true;
}

/* The layout of a std.sync type; false in *handled for any other type. */
bool r_llvm_sync_layout(
    RLlvmEmitter *emitter, RTypeId id, uint32_t *size, uint32_t *align, bool *handled) {
    const RLlvmSyncNative *native = r_llvm_sync_native(emitter, id);
    const RStandardSyncOutcome *schema = r_llvm_sync_outcome(emitter, id);
    const RSemanticType *type = r_llvm_type(emitter, r_llvm_value_type(emitter, id));

    *handled = (native != NULL) || (schema != NULL);
    if (!*handled) {
        return true;
    }
    if ((native != NULL) && !native->storage) {
        return r_llvm_runtime_layout(emitter, native->native, size, align);
    }
    if (native != NULL) {
        uint32_t native_size = 0U;
        uint32_t native_align = 0U;
        uint32_t value_size = 0U;
        uint32_t value_align = 0U;
        uint32_t offset = 0U;
        if (!r_llvm_runtime_layout(emitter, native->native, &native_size, &native_align) ||
            !r_llvm_layout(emitter, type->base, &value_size, &value_align) ||
            !r_llvm_sync_value_offset(emitter, id, &offset)) {
            return false;
        }
        *align = native_align > value_align ? native_align : value_align;
        *size = r_llvm_sync_align_up(offset + value_size, *align);
        return true;
    }
    {
        /* {uint32_t r_tag; union {P r_value;}} */
        const RTypeId payload = r_llvm_sync_payload(emitter, id);
        uint32_t payload_size = 0U;
        uint32_t payload_align = 1U;
        if (payload != R_TYPE_ID_INVALID) {
            if (!r_llvm_layout(emitter, payload, &payload_size, &payload_align)) {
                return false;
            }
        } else if (schema->guard != NULL) {
            if (!r_llvm_sync_guard_native_layout(emitter, schema, &payload_size, &payload_align)) {
                return false;
            }
        }
        *align = payload_align > 4U ? payload_align : 4U;
        *size =
            r_llvm_sync_align_up(r_llvm_sync_align_up(4U, payload_align) + payload_size, *align);
        return true;
    }
}

/* The offset of the payload union of an outcome. */
static bool r_llvm_sync_payload_offset(RLlvmEmitter *emitter, RTypeId id, uint32_t *offset) {
    const RStandardSyncOutcome *schema = r_llvm_sync_outcome(emitter, id);
    const RTypeId payload = r_llvm_sync_payload(emitter, id);
    uint32_t size = 0U;
    uint32_t align = 1U;

    if (schema == NULL) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    if ((payload != R_TYPE_ID_INVALID) ? !r_llvm_layout(emitter, payload, &size, &align)
        : (schema->guard != NULL) ? !r_llvm_sync_guard_native_layout(emitter, schema, &size, &align)
                                  : false) {
        return false;
    }
    *offset = r_llvm_sync_align_up(4U, align);
    return true;
}

bool r_llvm_sync_payload_offset_of(RLlvmEmitter *emitter, RTypeId id, uint32_t *offset) {
    return r_llvm_sync_payload_offset(emitter, id, offset);
}

/* The move and drop glue of a std.sync type; false in *handled for any other type. */
bool r_llvm_sync_glue(RLlvmEmitter *emitter,
                      RTypeId id,
                      bool move,
                      LLVMValueRef first,
                      LLVMValueRef second,
                      bool *handled) {
    const RLlvmSyncNative *native = r_llvm_sync_native(emitter, id);
    const RStandardSyncOutcome *schema = r_llvm_sync_outcome(emitter, id);
    LLVMValueRef arguments[3];

    *handled = (native != NULL) || (schema != NULL);
    if (!*handled) {
        return true;
    }
    if (native != NULL) {
        if (!move) {
            arguments[0] = first;
            return r_llvm_call_runtime(emitter, native->destroy, arguments, 1U, NULL) != NULL;
        }
        if (!native->storage) {
            arguments[0] = first;
            arguments[1] = second;
            return r_llvm_call_runtime(emitter, native->move, arguments, 2U, NULL) != NULL;
        }
        {
            uint32_t offset = 0U;
            if (!r_llvm_sync_value_offset(emitter, id, &offset)) {
                return false;
            }
            arguments[0] = first;
            arguments[1] = r_llvm_byte_offset(emitter, first, offset);
            arguments[2] = second;
            return r_llvm_call_runtime(emitter, native->move, arguments, 3U, NULL) != NULL;
        }
    }
    {
        /* The payload of the variants that carry one moves or drops; the tag follows. */
        const RTypeId payload = r_llvm_sync_payload(emitter, id);
        LLVMValueRef tagged = move ? second : first;
        LLVMValueRef tag = LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), tagged, "");
        LLVMValueRef present = LLVMBuildICmp(
            emitter->builder, LLVMIntULT, tag, r_llvm_u32(emitter, schema->variant_count), "");
        LLVMBasicBlockRef carry =
            LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        LLVMBasicBlockRef done =
            LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        uint32_t offset = 0U;
        present = LLVMBuildAnd(
            emitter->builder,
            present,
            LLVMBuildICmp(emitter->builder,
                          LLVMIntNE,
                          LLVMBuildAnd(emitter->builder,
                                       LLVMBuildLShr(emitter->builder,
                                                     r_llvm_u32(emitter, schema->payload_mask),
                                                     tag,
                                                     ""),
                                       r_llvm_u32(emitter, 1U),
                                       ""),
                          r_llvm_u32(emitter, 0U),
                          ""),
            "");
        if ((payload == R_TYPE_ID_INVALID) || !r_llvm_sync_payload_offset(emitter, id, &offset)) {
            /* No program type for the payload: nothing was ever stored in it. */
            if (payload != R_TYPE_ID_INVALID) {
                return false;
            }
            (void)LLVMBuildBr(emitter->builder, done);
        } else {
            (void)LLVMBuildCondBr(emitter->builder, present, carry, done);
            LLVMPositionBuilderAtEnd(emitter->builder, carry);
            if (move) {
                if (r_llvm_type_requires_drop(emitter, payload)
                        ? !r_llvm_call_move(emitter,
                                            payload,
                                            r_llvm_byte_offset(emitter, first, offset),
                                            r_llvm_byte_offset(emitter, second, offset))
                        : !r_llvm_std_copy(emitter,
                                           payload,
                                           r_llvm_byte_offset(emitter, first, offset),
                                           r_llvm_byte_offset(emitter, second, offset))) {
                    return false;
                }
            } else if (r_llvm_type_requires_drop(emitter, payload) &&
                       !r_llvm_call_drop(
                           emitter, payload, r_llvm_byte_offset(emitter, first, offset))) {
                return false;
            }
            (void)LLVMBuildBr(emitter->builder, done);
        }
        LLVMPositionBuilderAtEnd(emitter->builder, done);
        if (move) {
            (void)LLVMBuildStore(emitter->builder, tag, first);
        }
        (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, UINT32_MAX), tagged);
        return true;
    }
}

/* ---- Operations ---- */

/* The argument an operand gives a std.sync call: a value that requires drop moves into storage of
   its own and is no longer initialized; any other value is staged in memory when `memory` asks for
   an address, else passed as it is. */
static LLVMValueRef r_llvm_sync_argument(RLlvmEmitter *emitter,
                                         const RMirInstruction *instruction,
                                         uint32_t index,
                                         bool memory) {
    const RMirValueId operand = r_llvm_std_operand(emitter, instruction, index);
    const RTypeId type = r_llvm_std_operand_type(emitter, operand);

    if (r_llvm_type_requires_drop(emitter, type)) {
        uint32_t size = 0U;
        uint32_t align = 0U;
        LLVMValueRef staged;
        LLVMValueRef value = r_llvm_value(emitter, operand);
        if ((value == NULL) || !r_llvm_layout(emitter, type, &size, &align)) {
            return NULL;
        }
        staged = r_llvm_entry_alloca(emitter, size, align, "staged");
        if (!r_llvm_call_move(emitter, type, staged, value)) {
            return NULL;
        }
        r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, operand), false);
        return staged;
    }
    if (memory && (r_llvm_scalar_type(emitter, type) != NULL)) {
        return r_llvm_std_stage(emitter, operand, type);
    }
    return r_llvm_value(emitter, operand);
}

/* The wrapper an initializer of call_once or get_or_init runs through
   (r_c17_emit_sync_initializer_support), declared at its first use. */
static LLVMValueRef r_llvm_sync_initializer(RLlvmEmitter *emitter, RSymbolId id) {
    const RSemanticSymbol *initializer =
        (id == R_SYMBOL_ID_INVALID) || ((size_t)id > emitter->frontend->semantic_symbol_count)
            ? NULL
            : &emitter->frontend->semantic_symbols[(size_t)id - 1U];
    LLVMTypeRef parameters[2];
    char name[64];

    if ((initializer == NULL) || (emitter->functions[id] == NULL)) {
        (void)r_llvm_unsupported(emitter, "an initializer of std.sync that is not lowered");
        return NULL;
    }
    if (emitter->sync_initializers[id] == NULL) {
        const bool returns_void = r_llvm_type_is_void(emitter, initializer->return_type);
        parameters[0] = r_llvm_pointer(emitter);
        parameters[1] = r_llvm_pointer(emitter);
        (void)snprintf(
            name, sizeof(name), "r_sync_initializer.%" PRIu32, r_llvm_symbol_key(emitter, id));
        emitter->sync_initializers[id] = LLVMAddFunction(
            emitter->module,
            name,
            LLVMFunctionType(r_llvm_int(emitter, 1U), parameters, returns_void ? 1U : 2U, 0));
        LLVMSetLinkage(emitter->sync_initializers[id], LLVMInternalLinkage);
    }
    return emitter->sync_initializers[id];
}

/* The bodies of the initializer wrappers: the stack check, the call and, for a checked
   initializer, its outcome; a pending panic or an error is a failed attempt. */
bool r_llvm_emit_sync_initializers(RLlvmEmitter *emitter) {
    size_t id;

    for (id = 1U; id <= emitter->frontend->semantic_symbol_count; ++id) {
        const RSemanticSymbol *initializer = &emitter->frontend->semantic_symbols[id - 1U];
        const bool checked = initializer->effect_carrier_type != R_TYPE_ID_INVALID;
        const bool returns_void = r_llvm_type_is_void(emitter, initializer->return_type);
        const bool in_memory =
            checked ||
            (!returns_void && (r_llvm_scalar_type(emitter, initializer->return_type) == NULL));
        LLVMValueRef arguments[2];
        LLVMValueRef stack[2];
        LLVMValueRef result;
        LLVMValueRef context;
        LLVMValueRef call;
        LLVMBasicBlockRef body;
        if (emitter->sync_initializers[id] == NULL) {
            continue;
        }
        emitter->function = emitter->sync_initializers[id];
        emitter->mir = NULL;
        emitter->frame = NULL;
        body = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "entry");
        LLVMPositionBuilderAtEnd(emitter->builder, body);
        result = returns_void ? NULL : LLVMGetParam(emitter->function, 0U);
        context = LLVMGetParam(emitter->function, returns_void ? 0U : 1U);
        stack[0] = r_llvm_stack_entry(emitter, (RSymbolId)id);
        stack[1] = r_llvm_span(emitter, initializer->name_span);
        if ((stack[0] == NULL) || (stack[1] == NULL) ||
            (r_llvm_call_runtime(emitter, "r_runtime_stack_require", stack, 2U, NULL) == NULL)) {
            return false;
        }
        if (checked) {
            LLVMValueRef unwinding;
            LLVMBasicBlockRef failed =
                LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
            LLVMBasicBlockRef returned =
                LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
            LLVMBasicBlockRef succeeded =
                LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
            uint32_t payload = 0U;
            arguments[0] = context;
            (void)LLVMBuildCall2(emitter->builder,
                                 emitter->function_types[id],
                                 emitter->functions[id],
                                 arguments,
                                 1U,
                                 "");
            /* L39: an initializer that panicked wrote no carrier; the once stays unset. */
            unwinding = r_llvm_unwinding(emitter);
            if (unwinding == NULL) {
                return false;
            }
            (void)LLVMBuildCondBr(emitter->builder, unwinding, failed, returned);
            LLVMPositionBuilderAtEnd(emitter->builder, returned);
            (void)LLVMBuildCondBr(
                emitter->builder,
                LLVMBuildICmp(
                    emitter->builder,
                    LLVMIntNE,
                    LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), context, ""),
                    r_llvm_u32(emitter, 0U),
                    ""),
                failed,
                succeeded);
            LLVMPositionBuilderAtEnd(emitter->builder, failed);
            (void)LLVMBuildRet(emitter->builder, LLVMConstInt(r_llvm_int(emitter, 1U), 0U, 0));
            LLVMPositionBuilderAtEnd(emitter->builder, succeeded);
            if (!returns_void) {
                if (!r_llvm_payload_offset(emitter, initializer->effect_carrier_type, &payload) ||
                    (r_llvm_type_requires_drop(emitter, initializer->return_type)
                         ? !r_llvm_call_move(emitter,
                                             initializer->return_type,
                                             result,
                                             r_llvm_byte_offset(emitter, context, payload))
                         : !r_llvm_std_copy(emitter,
                                            initializer->return_type,
                                            result,
                                            r_llvm_byte_offset(emitter, context, payload)))) {
                    return false;
                }
            }
        } else if (returns_void) {
            (void)LLVMBuildCall2(emitter->builder,
                                 emitter->function_types[id],
                                 emitter->functions[id],
                                 NULL,
                                 0U,
                                 "");
        } else if (in_memory) {
            /* The result storage is uninitialized: the initializer writes its value there. */
            arguments[0] = result;
            (void)LLVMBuildCall2(emitter->builder,
                                 emitter->function_types[id],
                                 emitter->functions[id],
                                 arguments,
                                 1U,
                                 "");
        } else {
            call = LLVMBuildCall2(emitter->builder,
                                  emitter->function_types[id],
                                  emitter->functions[id],
                                  NULL,
                                  0U,
                                  "");
            r_llvm_store_scalar(emitter, initializer->return_type, call, result);
        }
        (void)LLVMBuildRet(emitter->builder, LLVMConstInt(r_llvm_int(emitter, 1U), 1U, 0));
    }
    return true;
}

/* call_once, call_once_force and get_or_init (r_c17_emit_async_sync_call): the initializer runs
   at most once; a checked one hands its errors to the carrier of the operation. */
static bool r_llvm_sync_once_call(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const bool is_get = instruction->standard_operation == R_STANDARD_CALL_SYNC_GET_OR_INIT;
    const bool checked = instruction->integer_value != 0U;
    const char *name = is_get ? "r_std_sync_get_or_init"
                       : instruction->standard_operation == R_STANDARD_CALL_SYNC_CALL_ONCE_FORCE
                           ? "r_std_sync_call_once_force"
                           : "r_std_sync_call_once";
    const RSemanticType *carrier =
        checked ? r_llvm_type(emitter, r_llvm_value_type(emitter, instruction->runtime_type))
                : NULL;
    LLVMValueRef arguments[3];
    LLVMValueRef native;
    LLVMValueRef succeeded;
    LLVMValueRef context = NULL;

    arguments[0] = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 0U));
    arguments[1] = r_llvm_sync_initializer(emitter, instruction->symbol);
    if ((arguments[0] == NULL) || (arguments[1] == NULL)) {
        return false;
    }
    if (checked) {
        uint32_t size = 0U;
        uint32_t align = 0U;
        if ((carrier == NULL) || (carrier->kind != R_SEMANTIC_TYPE_EFFECT_CARRIER) ||
            !r_llvm_layout(emitter, instruction->runtime_type, &size, &align)) {
            return carrier == NULL ? r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR) : false;
        }
        context = r_llvm_entry_alloca(emitter, size, align, "context");
        (void)LLVMBuildMemSet(emitter->builder,
                              context,
                              LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                              r_llvm_u64(emitter, size),
                              align);
    }
    arguments[2] = context == NULL ? LLVMConstNull(r_llvm_pointer(emitter)) : context;
    native = r_llvm_call_runtime(emitter, name, arguments, 3U, NULL);
    if (native == NULL) {
        return false;
    }
    succeeded =
        is_get
            ? LLVMBuildICmp(
                  emitter->builder, LLVMIntNE, native, LLVMConstNull(r_llvm_pointer(emitter)), "")
            : native;
    if (!checked) {
        if (!r_llvm_check(emitter,
                          LLVMBuildNot(emitter->builder, succeeded, ""),
                          "R_RUNTIME_PANIC_CONTRACT_VIOLATION",
                          instruction->span,
                          R_MIR_BLOCK_ID_INVALID)) {
            return false;
        }
        return !is_get || r_llvm_set_value(emitter, instruction->result, native);
    }
    {
        const uint32_t errors = r_semantic_effect_count(emitter->frontend, carrier->second);
        LLVMValueRef result = r_llvm_std_result(emitter, instruction);
        LLVMBasicBlockRef ok =
            LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        LLVMBasicBlockRef failed =
            LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        LLVMBasicBlockRef invalid =
            LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        LLVMBasicBlockRef done =
            LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        uint32_t context_payload = 0U;
        uint32_t result_payload = 0U;
        LLVMValueRef choice;
        uint32_t error;
        if ((result == NULL) ||
            !r_llvm_payload_offset(emitter, instruction->runtime_type, &context_payload) ||
            !r_llvm_payload_offset(emitter, instruction->type, &result_payload)) {
            return false;
        }
        (void)LLVMBuildCondBr(emitter->builder, succeeded, ok, failed);
        LLVMPositionBuilderAtEnd(emitter->builder, ok);
        if (is_get) {
            (void)LLVMBuildStore(
                emitter->builder, native, r_llvm_byte_offset(emitter, result, result_payload));
        }
        (void)LLVMBuildBr(emitter->builder, done);
        LLVMPositionBuilderAtEnd(emitter->builder, failed);
        choice =
            LLVMBuildSwitch(emitter->builder,
                            LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), context, ""),
                            invalid,
                            errors);
        for (error = 0U; error < errors; ++error) {
            const RTypeId error_type =
                r_semantic_effect_at(emitter->frontend, carrier->second, error);
            LLVMBasicBlockRef branch =
                LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
            LLVMAddCase(choice, r_llvm_u32(emitter, error + 1U), branch);
            LLVMPositionBuilderAtEnd(emitter->builder, branch);
            if (r_llvm_type_requires_drop(emitter, error_type)
                    ? !r_llvm_call_move(emitter,
                                        error_type,
                                        r_llvm_byte_offset(emitter, result, result_payload),
                                        r_llvm_byte_offset(emitter, context, context_payload))
                    : !r_llvm_std_copy(emitter,
                                       error_type,
                                       r_llvm_byte_offset(emitter, result, result_payload),
                                       r_llvm_byte_offset(emitter, context, context_payload))) {
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
        LLVMPositionBuilderAtEnd(emitter->builder, done);
        r_llvm_std_initialized(emitter, instruction);
        return true;
    }
}

/* send, sync_send, try_send, recv, try_recv, get and set (r_c17_emit_sync_value_operation): the
   outcome's tag is the library's kind; a value the library refused comes back as the payload. */
static bool r_llvm_sync_value_call(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RStandardCallOperation operation = instruction->standard_operation;
    const bool getting = operation == R_STANDARD_CALL_SYNC_GET;
    const bool setting = operation == R_STANDARD_CALL_SYNC_SET;
    const bool receiving =
        (operation == R_STANDARD_CALL_SYNC_RECV) || (operation == R_STANDARD_CALL_SYNC_TRY_RECV);
    const char *name = operation == R_STANDARD_CALL_SYNC_SEND        ? "r_std_sync_send"
                       : operation == R_STANDARD_CALL_SYNC_SYNC_SEND ? "r_std_sync_sync_send"
                       : operation == R_STANDARD_CALL_SYNC_TRY_SEND  ? "r_std_sync_try_send"
                       : operation == R_STANDARD_CALL_SYNC_RECV      ? "r_std_sync_recv"
                       : operation == R_STANDARD_CALL_SYNC_TRY_RECV  ? "r_std_sync_try_recv"
                       : setting                                     ? "r_std_sync_set"
                                                                     : "r_std_sync_get";
    const char *native_type = operation == R_STANDARD_CALL_SYNC_TRY_SEND   ? "RStdSyncTrySendResult"
                              : operation == R_STANDARD_CALL_SYNC_RECV     ? "RStdSyncRecvResult"
                              : operation == R_STANDARD_CALL_SYNC_TRY_RECV ? "RStdSyncTryRecvResult"
                                                                           : "RStdSyncSendResult";
    LLVMValueRef result = r_llvm_std_result(emitter, instruction);
    LLVMValueRef arguments[2];
    LLVMValueRef value = NULL;
    LLVMValueRef native;
    uint32_t payload = 0U;

    arguments[0] = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 0U));
    if ((result == NULL) || (arguments[0] == NULL)) {
        return false;
    }
    if (getting) {
        LLVMValueRef found = r_llvm_call_runtime(emitter, name, arguments, 1U, NULL);
        if ((found == NULL) || !r_llvm_payload_offset(emitter, instruction->type, &payload)) {
            return false;
        }
        (void)LLVMBuildStore(
            emitter->builder,
            LLVMBuildZExt(
                emitter->builder,
                LLVMBuildICmp(
                    emitter->builder, LLVMIntNE, found, LLVMConstNull(r_llvm_pointer(emitter)), ""),
                r_llvm_int(emitter, 32U),
                ""),
            result);
        (void)LLVMBuildStore(emitter->builder, found, r_llvm_byte_offset(emitter, result, payload));
        r_llvm_std_initialized(emitter, instruction);
        return true;
    }
    if (!r_llvm_sync_payload_offset(emitter, instruction->type, &payload)) {
        return false;
    }
    if (receiving) {
        arguments[1] = r_llvm_byte_offset(emitter, result, payload);
    } else {
        value = r_llvm_sync_argument(emitter, instruction, 1U, true);
        arguments[1] = value;
    }
    /* set answers with its kind; the others with a structure holding it. */
    native = setting ? NULL : r_llvm_std_structure(emitter, native_type);
    if ((arguments[1] == NULL) || (!setting && (native == NULL))) {
        return false;
    }
    {
        LLVMValueRef answer = r_llvm_call_runtime(emitter, name, arguments, 2U, native);
        LLVMValueRef kind;
        if (answer == NULL) {
            return false;
        }
        kind = setting ? answer
                       : LLVMBuildLoad2(emitter->builder,
                                        r_llvm_int(emitter, 32U),
                                        r_llvm_std_member(emitter, native, native_type, "kind"),
                                        "");
        (void)LLVMBuildStore(emitter->builder, kind, result);
        if (!receiving) {
            /* A refused value returns as the payload of the outcome. */
            const RSemanticType *outcome =
                r_llvm_type(emitter, r_llvm_value_type(emitter, instruction->type));
            LLVMBasicBlockRef back =
                LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
            LLVMBasicBlockRef done =
                LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
            if (outcome == NULL) {
                return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
            }
            (void)LLVMBuildCondBr(
                emitter->builder,
                LLVMBuildICmp(emitter->builder, LLVMIntNE, kind, r_llvm_u32(emitter, 0U), ""),
                back,
                done);
            LLVMPositionBuilderAtEnd(emitter->builder, back);
            if (r_llvm_type_requires_drop(emitter, outcome->base)
                    ? !r_llvm_call_move(emitter,
                                        outcome->base,
                                        r_llvm_byte_offset(emitter, result, payload),
                                        value)
                    : !r_llvm_std_copy(emitter,
                                       outcome->base,
                                       r_llvm_byte_offset(emitter, result, payload),
                                       value)) {
                return false;
            }
            (void)LLVMBuildBr(emitter->builder, done);
            LLVMPositionBuilderAtEnd(emitter->builder, done);
        }
    }
    r_llvm_std_initialized(emitter, instruction);
    return true;
}

/* The locks (r_c17_emit_sync_lock_operation): constructors, acquisitions, guard accessors, unlock
   and the wait of a condition variable. */
static bool r_llvm_sync_lock_call(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RStandardCallOperation operation = instruction->standard_operation;
    const bool waiting = operation == R_STANDARD_CALL_SYNC_WAIT;
    const bool mutex = (operation == R_STANDARD_CALL_SYNC_LOCK) ||
                       (operation == R_STANDARD_CALL_SYNC_TRY_LOCK) || waiting;
    const bool reading =
        (operation == R_STANDARD_CALL_SYNC_READ) || (operation == R_STANDARD_CALL_SYNC_TRY_READ);
    const bool trying = (operation == R_STANDARD_CALL_SYNC_TRY_LOCK) ||
                        (operation == R_STANDARD_CALL_SYNC_TRY_READ) ||
                        (operation == R_STANDARD_CALL_SYNC_TRY_WRITE);
    const char *name = NULL;
    const char *native_type;
    const char *guard_move;
    LLVMValueRef arguments[4];
    LLVMValueRef native;
    LLVMValueRef result;
    uint32_t payload = 0U;

#define R_SYNC_NAME(id, spelling)                                                                  \
    if (operation == R_STANDARD_CALL_SYNC_##id) {                                                  \
        name = #spelling;                                                                          \
    }
    R_STANDARD_SYNC_LOCK_OPERATIONS(R_SYNC_NAME)
#undef R_SYNC_NAME
    if (name == NULL) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    if (operation == R_STANDARD_CALL_SYNC_UNLOCK) {
        arguments[0] = r_llvm_sync_argument(emitter, instruction, 0U, true);
        return (arguments[0] != NULL) &&
               (r_llvm_call_runtime(emitter, name, arguments, 1U, NULL) != NULL);
    }
    if ((operation >= R_STANDARD_CALL_SYNC_MUTEX_GUARD_REF) &&
        (operation <= R_STANDARD_CALL_SYNC_RW_WRITE_GUARD_MUT)) {
        arguments[0] = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 0U));
        native =
            arguments[0] == NULL ? NULL : r_llvm_call_runtime(emitter, name, arguments, 1U, NULL);
        return (native != NULL) && r_llvm_set_value(emitter, instruction->result, native);
    }
    result = r_llvm_std_result(emitter, instruction);
    if (result == NULL) {
        return false;
    }
    if ((operation == R_STANDARD_CALL_SYNC_MUTEX_NEW) ||
        (operation == R_STANDARD_CALL_SYNC_RWLOCK_NEW)) {
        uint32_t offset = 0U;
        arguments[3] = r_llvm_sync_argument(emitter, instruction, 0U, true);
        arguments[2] = r_llvm_type_info(emitter, instruction->auxiliary_type);
        if ((arguments[3] == NULL) || (arguments[2] == NULL) ||
            !r_llvm_sync_value_offset(emitter, instruction->type, &offset)) {
            return false;
        }
        arguments[0] = result;
        arguments[1] = r_llvm_byte_offset(emitter, result, offset);
        if (r_llvm_call_runtime(emitter, name, arguments, 4U, NULL) == NULL) {
            return false;
        }
        r_llvm_std_initialized(emitter, instruction);
        return true;
    }
    native_type = mutex     ? (trying ? "RStdSyncTryLockResult" : "RStdSyncLockResult")
                  : reading ? (trying ? "RStdSyncTryReadLockResult" : "RStdSyncReadLockResult")
                            : (trying ? "RStdSyncTryWriteLockResult" : "RStdSyncWriteLockResult");
    guard_move = mutex     ? "r_library_internal_sync_mutex_guard_move"
                 : reading ? "r_library_internal_sync_rw_read_guard_move"
                           : "r_library_internal_sync_rw_write_guard_move";
    native = r_llvm_std_structure(emitter, native_type);
    if ((native == NULL) || !r_llvm_sync_payload_offset(emitter, instruction->type, &payload)) {
        return false;
    }
    if (mutex) {
        arguments[0] = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 0U));
        if (waiting) {
            arguments[1] = r_llvm_sync_argument(emitter, instruction, 1U, true);
        }
        if ((arguments[0] == NULL) || (waiting && (arguments[1] == NULL)) ||
            (r_llvm_call_runtime(emitter, name, arguments, waiting ? 2U : 1U, native) == NULL)) {
            return false;
        }
    } else {
        arguments[0] = native;
        arguments[1] = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 0U));
        if ((arguments[1] == NULL) ||
            (r_llvm_call_runtime(emitter, name, arguments, 2U, NULL) == NULL)) {
            return false;
        }
    }
    {
        LLVMValueRef kind = LLVMBuildLoad2(emitter->builder,
                                           r_llvm_int(emitter, 32U),
                                           r_llvm_std_member(emitter, native, native_type, "kind"),
                                           "");
        LLVMBasicBlockRef locked =
            LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        LLVMBasicBlockRef done =
            LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        (void)LLVMBuildStore(emitter->builder, kind, result);
        /* locked and poisoned carry the guard. */
        (void)LLVMBuildCondBr(
            emitter->builder,
            LLVMBuildICmp(emitter->builder, LLVMIntULT, kind, r_llvm_u32(emitter, 2U), ""),
            locked,
            done);
        LLVMPositionBuilderAtEnd(emitter->builder, locked);
        arguments[0] = r_llvm_byte_offset(emitter, result, payload);
        arguments[1] = r_llvm_std_member(emitter, native, native_type, "guard");
        if (r_llvm_call_runtime(emitter, guard_move, arguments, 2U, NULL) == NULL) {
            return false;
        }
        (void)LLVMBuildBr(emitter->builder, done);
        LLVMPositionBuilderAtEnd(emitter->builder, done);
    }
    r_llvm_std_initialized(emitter, instruction);
    return true;
}

/* The operations of std.sync (the range ONCE_NEW..WAIT of the standard operations). */
bool r_llvm_std_sync(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RStandardCallOperation operation = instruction->standard_operation;
    LLVMValueRef arguments[3];
    LLVMValueRef result;

    switch (operation) {
    case R_STANDARD_CALL_SYNC_ONCE_NEW:
    case R_STANDARD_CALL_SYNC_CONDVAR_NEW:
        result = r_llvm_std_result(emitter, instruction);
        if ((result == NULL) || (r_llvm_call_runtime(emitter,
                                                     operation == R_STANDARD_CALL_SYNC_ONCE_NEW
                                                         ? "r_std_sync_once_new"
                                                         : "r_std_sync_condvar_new",
                                                     &result,
                                                     1U,
                                                     NULL) == NULL)) {
            return false;
        }
        r_llvm_std_initialized(emitter, instruction);
        return true;
    case R_STANDARD_CALL_SYNC_ONCE_LOCK: {
        uint32_t offset = 0U;
        result = r_llvm_std_result(emitter, instruction);
        arguments[2] = r_llvm_type_info(emitter, instruction->auxiliary_type);
        if ((result == NULL) || (arguments[2] == NULL) ||
            !r_llvm_sync_value_offset(emitter, instruction->type, &offset)) {
            return false;
        }
        arguments[0] = result;
        arguments[1] = r_llvm_byte_offset(emitter, result, offset);
        if (r_llvm_call_runtime(emitter, "r_std_sync_once_lock", arguments, 3U, NULL) == NULL) {
            return false;
        }
        r_llvm_std_initialized(emitter, instruction);
        return true;
    }
    case R_STANDARD_CALL_SYNC_CALL_ONCE:
    case R_STANDARD_CALL_SYNC_CALL_ONCE_FORCE:
    case R_STANDARD_CALL_SYNC_GET_OR_INIT:
        return r_llvm_sync_once_call(emitter, instruction);
    case R_STANDARD_CALL_SYNC_CHANNEL:
    case R_STANDARD_CALL_SYNC_SYNC_CHANNEL: {
        const bool bounded = operation == R_STANDARD_CALL_SYNC_SYNC_CHANNEL;
        const char *type_name =
            bounded ? "RStdSyncSyncChannelCreateResult" : "RStdSyncChannelCreateResult";
        LLVMValueRef native = r_llvm_std_structure(emitter, type_name);
        arguments[0] = r_llvm_std_allocator(emitter);
        arguments[1] = r_llvm_type_info(emitter, instruction->runtime_type);
        if (bounded) {
            arguments[2] = r_llvm_std_size(emitter, r_llvm_std_operand(emitter, instruction, 0U));
        }
        return (native != NULL) && (arguments[0] != NULL) && (arguments[1] != NULL) &&
               (!bounded || (arguments[2] != NULL)) &&
               (r_llvm_call_runtime(emitter,
                                    bounded ? "r_std_sync_sync_channel" : "r_std_sync_channel",
                                    arguments,
                                    bounded ? 3U : 2U,
                                    native) != NULL) &&
               r_llvm_std_require_status(emitter,
                                         instruction,
                                         native,
                                         type_name,
                                         "R_STD_SYNC_CHANNEL_CALL_CONTRACT_VIOLATION",
                                         true) &&
               r_llvm_std_alloc_carrier(emitter,
                                        instruction,
                                        native,
                                        type_name,
                                        "R_STD_SYNC_CHANNEL_CALL_SUCCESS",
                                        "value");
    }
    case R_STANDARD_CALL_SYNC_SENDER:
    case R_STANDARD_CALL_SYNC_SYNC_SENDER:
    case R_STANDARD_CALL_SYNC_CLONE_SENDER:
    case R_STANDARD_CALL_SYNC_CLONE_SYNC_SENDER:
    case R_STANDARD_CALL_SYNC_SYNC_RECEIVER:
    case R_STANDARD_CALL_SYNC_RECEIVER: {
        const char *name =
            operation == R_STANDARD_CALL_SYNC_SENDER              ? "r_std_sync_sender"
            : operation == R_STANDARD_CALL_SYNC_SYNC_SENDER       ? "r_std_sync_sync_sender"
            : operation == R_STANDARD_CALL_SYNC_CLONE_SENDER      ? "r_std_sync_clone_sender"
            : operation == R_STANDARD_CALL_SYNC_CLONE_SYNC_SENDER ? "r_std_sync_clone_sync_sender"
            : operation == R_STANDARD_CALL_SYNC_SYNC_RECEIVER     ? "r_std_sync_sync_receiver"
                                                                  : "r_std_sync_receiver";
        result = r_llvm_std_result(emitter, instruction);
        arguments[0] = r_llvm_sync_argument(emitter, instruction, 0U, false);
        if ((result == NULL) || (arguments[0] == NULL) ||
            (r_llvm_call_runtime(emitter, name, arguments, 1U, result) == NULL)) {
            return false;
        }
        r_llvm_std_initialized(emitter, instruction);
        return true;
    }
    case R_STANDARD_CALL_SYNC_SEND:
    case R_STANDARD_CALL_SYNC_SYNC_SEND:
    case R_STANDARD_CALL_SYNC_TRY_SEND:
    case R_STANDARD_CALL_SYNC_RECV:
    case R_STANDARD_CALL_SYNC_TRY_RECV:
    case R_STANDARD_CALL_SYNC_GET:
    case R_STANDARD_CALL_SYNC_SET:
        return r_llvm_sync_value_call(emitter, instruction);
    case R_STANDARD_CALL_SYNC_BARRIER_NEW: {
        LLVMValueRef native = r_llvm_std_structure(emitter, "RStdSyncBarrierCreateResult");
        uint32_t payload = 0U;
        result = r_llvm_std_result(emitter, instruction);
        if ((native == NULL) || (result == NULL) ||
            !r_llvm_payload_offset(emitter, instruction->type, &payload)) {
            return false;
        }
        arguments[0] = r_llvm_byte_offset(emitter, result, payload);
        arguments[1] = r_llvm_std_size(emitter, r_llvm_std_operand(emitter, instruction, 0U));
        if ((arguments[1] == NULL) ||
            (r_llvm_call_runtime(emitter, "r_std_sync_barrier_new", arguments, 2U, native) ==
             NULL)) {
            return false;
        }
        {
            LLVMValueRef status = LLVMBuildLoad2(
                emitter->builder,
                r_llvm_int(emitter, 32U),
                r_llvm_std_member(emitter, native, "RStdSyncBarrierCreateResult", "status"),
                "");
            LLVMBasicBlockRef failed =
                LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
            LLVMBasicBlockRef invalid =
                LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
            LLVMBasicBlockRef done =
                LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
            LLVMValueRef choice = LLVMBuildSwitch(emitter->builder, status, invalid, 2U);
            int64_t success = 0;
            int64_t error = 0;
            if (!r_llvm_runtime_constant(emitter, "R_STD_SYNC_BARRIER_CREATE_SUCCESS", &success) ||
                !r_llvm_runtime_constant(emitter, "R_STD_SYNC_BARRIER_CREATE_ERROR", &error)) {
                return false;
            }
            LLVMAddCase(choice, r_llvm_u32(emitter, (uint64_t)success), done);
            LLVMAddCase(choice, r_llvm_u32(emitter, (uint64_t)error), failed);
            LLVMPositionBuilderAtEnd(emitter->builder, failed);
            (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 1U), result);
            if (!r_llvm_std_copy(
                    emitter,
                    r_llvm_std_single_effect(emitter, instruction->type),
                    r_llvm_byte_offset(emitter, result, payload),
                    r_llvm_std_member(emitter, native, "RStdSyncBarrierCreateResult", "error"))) {
                return false;
            }
            (void)LLVMBuildBr(emitter->builder, done);
            LLVMPositionBuilderAtEnd(emitter->builder, invalid);
            if (!r_llvm_panic(emitter,
                              "R_RUNTIME_PANIC_CONTRACT_VIOLATION",
                              instruction->span,
                              R_MIR_BLOCK_ID_INVALID)) {
                return false;
            }
            LLVMPositionBuilderAtEnd(emitter->builder, done);
        }
        r_llvm_std_initialized(emitter, instruction);
        return true;
    }
    case R_STANDARD_CALL_SYNC_BARRIER_WAIT:
    case R_STANDARD_CALL_SYNC_NOTIFY_ONE:
    case R_STANDARD_CALL_SYNC_NOTIFY_ALL: {
        const char *name = operation == R_STANDARD_CALL_SYNC_BARRIER_WAIT
                               ? "r_std_sync_barrier_wait"
                           : operation == R_STANDARD_CALL_SYNC_NOTIFY_ONE ? "r_std_sync_notify_one"
                                                                          : "r_std_sync_notify_all";
        arguments[0] = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 0U));
        if (arguments[0] == NULL) {
            return false;
        }
        if (instruction->result == R_MIR_VALUE_ID_INVALID) {
            return r_llvm_call_runtime(emitter, name, arguments, 1U, NULL) != NULL;
        }
        if (r_llvm_scalar_type(emitter, instruction->type) != NULL) {
            LLVMValueRef waited = r_llvm_call_runtime(emitter, name, arguments, 1U, NULL);
            return (waited != NULL) && r_llvm_set_value(emitter, instruction->result, waited);
        }
        result = r_llvm_std_result(emitter, instruction);
        if ((result == NULL) ||
            (r_llvm_call_runtime(emitter, name, arguments, 1U, result) == NULL)) {
            return false;
        }
        r_llvm_std_initialized(emitter, instruction);
        return true;
    }
    default:
        return r_llvm_sync_lock_call(emitter, instruction);
    }
}

/* ---- std.sync::receive and the asynchronous resources of std.async (standard_async_sync.inc) ----
 */

/* The layout argument of a start whose task value is an outcome the library fills in: its type
   information and where the tag, the value and, for a broadcast, the lag are. */
static LLVMValueRef r_llvm_async_sync_layout(RLlvmEmitter *emitter,
                                             const char *type_name,
                                             RTypeId logical,
                                             bool lagged) {
    LLVMValueRef info = r_llvm_type_info(emitter, logical);
    LLVMValueRef layout = r_llvm_std_structure(emitter, type_name);
    uint32_t info_size = 0U;
    uint32_t info_align = 0U;
    uint32_t payload = 0U;

    if ((info == NULL) || (layout == NULL) ||
        !r_llvm_runtime_layout(emitter, "RRuntimeTypeInfo", &info_size, &info_align) ||
        !r_llvm_payload_offset(emitter, logical, &payload)) {
        return NULL;
    }
    (void)LLVMBuildMemCpy(emitter->builder,
                          r_llvm_std_member(emitter, layout, type_name, "result"),
                          info_align,
                          info,
                          info_align,
                          r_llvm_u64(emitter, info_size));
    (void)LLVMBuildStore(emitter->builder,
                         r_llvm_u64(emitter, 0U),
                         r_llvm_std_member(emitter, layout, type_name, "tag_offset"));
    (void)LLVMBuildStore(emitter->builder,
                         r_llvm_u64(emitter, payload),
                         r_llvm_std_member(emitter, layout, type_name, "value_offset"));
    if (lagged) {
        (void)LLVMBuildStore(emitter->builder,
                             r_llvm_u64(emitter, payload),
                             r_llvm_std_member(emitter, layout, type_name, "lag_offset"));
    }
    return layout;
}

/* A start {is_ok, task, error} as the start carrier of the instruction. */
static bool r_llvm_async_sync_started(RLlvmEmitter *emitter,
                                      const RMirInstruction *instruction,
                                      const char *name,
                                      const char *type_name,
                                      LLVMValueRef *arguments,
                                      unsigned count) {
    LLVMValueRef native = r_llvm_std_structure(emitter, type_name);
    LLVMValueRef is_ok;

    if ((native == NULL) ||
        (r_llvm_call_runtime(emitter, name, arguments, count, native) == NULL)) {
        return false;
    }
    is_ok = r_llvm_std_member(emitter, native, type_name, "is_ok");
    return (is_ok != NULL) &&
           r_llvm_std_carrier(
               emitter,
               instruction,
               LLVMBuildICmp(emitter->builder,
                             LLVMIntNE,
                             LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 8U), is_ok, ""),
                             LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                             ""),
               r_llvm_std_member(emitter, native, type_name, "task"),
               r_llvm_std_member(emitter, native, type_name, "error"));
}

/* std.sync::receive (r_c17_emit_async_sync_receive): a task that completes with o<T>. */
bool r_llvm_std_receive(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RSemanticType *task =
        r_llvm_type(emitter, r_llvm_value_type(emitter, instruction->auxiliary_type));
    const RMirInstruction *await =
        emitter->frame == NULL ? NULL : r_llvm_receive_await(emitter, emitter->mir, instruction);
    LLVMValueRef arguments[2];

    if (task == NULL) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    /* B7: a channel that holds a value, or has lost every sender, answers at once. */
    if ((await != NULL) && !r_llvm_emit_receive_now(emitter, instruction, await)) {
        return false;
    }
    arguments[0] = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 0U));
    arguments[1] = r_llvm_async_sync_layout(emitter, "RStdSyncReceiveLayout", task->base, false);
    return (arguments[0] != NULL) && (arguments[1] != NULL) &&
           r_llvm_async_sync_started(emitter,
                                     instruction,
                                     "r_std_sync_receive",
                                     "RStdSyncReceiveStartResult",
                                     arguments,
                                     2U);
}

/* The guard kind std.async::unlock passes for a guard. */
static const char *r_llvm_async_guard_kind(RLlvmEmitter *emitter, RTypeId type) {
    return r_llvm_standard_named(emitter, type, "std.async::mutex_guard")
               ? "R_STD_ASYNC_GUARD_MUTEX"
           : r_llvm_standard_named(emitter, type, "std.async::rw_read_guard")
               ? "R_STD_ASYNC_GUARD_RW_READ"
           : r_llvm_standard_named(emitter, type, "std.async::rw_write_guard")
               ? "R_STD_ASYNC_GUARD_RW_WRITE"
               : NULL;
}

/* The operations of std.async's locks, semaphore, notify and broadcast and of the channel
   reservations, by the shape of their descriptor (r_c17_emit_async_sync_mir). */
bool r_llvm_std_async_sync(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RAsyncSyncDescriptor *descriptor =
        r_async_sync_descriptor(instruction->standard_operation);
    LLVMValueRef first = NULL;
    LLVMValueRef second = NULL;
    LLVMValueRef arguments[5];
    LLVMValueRef result;
    RTypeId first_type = R_TYPE_ID_INVALID;

    if ((descriptor == NULL) ||
        (instruction->operand_count != r_async_sync_operand_count(descriptor))) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    if (instruction->operand_count > 0U) {
        first_type = r_llvm_std_operand_type(emitter, r_llvm_std_operand(emitter, instruction, 0U));
        first = r_llvm_sync_argument(emitter, instruction, 0U, false);
        if (first == NULL) {
            return false;
        }
    }
    if (instruction->operand_count > 1U) {
        second = r_llvm_sync_argument(emitter, instruction, 1U, true);
        if (second == NULL) {
            return false;
        }
    }
    if (r_async_sync_starts(descriptor)) {
        const RSemanticType *task =
            r_llvm_type(emitter, r_llvm_value_type(emitter, instruction->auxiliary_type));
        unsigned count = 1U;
        if (task == NULL) {
            return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
        arguments[0] = first;
        if (descriptor->shape == R_ASYNC_SYNC_START) {
            arguments[count++] = r_llvm_type_info(emitter, task->base);
        } else if (descriptor->shape == R_ASYNC_SYNC_START_OUTCOME) {
            const bool broadcast = descriptor->operation == R_STANDARD_CALL_ASYNC_BROADCAST_RECEIVE;
            arguments[count++] = r_llvm_async_sync_layout(
                emitter,
                broadcast ? "RStdAsyncBroadcastReceiveLayout" : "RStdSyncReserveLayout",
                task->base,
                broadcast);
        }
        if ((count == 2U) && (arguments[1] == NULL)) {
            return false;
        }
        return r_llvm_async_sync_started(
            emitter, instruction, descriptor->native, "RStdAsyncStartResult", arguments, count);
    }
    switch (descriptor->shape) {
    case R_ASYNC_SYNC_CONSTRUCT_VALUE:
    case R_ASYNC_SYNC_CONSTRUCT_COUNT:
    case R_ASYNC_SYNC_CONSTRUCT_EMPTY:
    case R_ASYNC_SYNC_CONSTRUCT_TYPED:
    case R_ASYNC_SYNC_SUBSCRIBE:
    case R_ASYNC_SYNC_PUBLISH: {
        const char *type_name =
            descriptor->shape == R_ASYNC_SYNC_CONSTRUCT_VALUE
                ? (strcmp(descriptor->subject, "std.async::mutex") == 0
                       ? "RStdAsyncMutexNewResult"
                       : "RStdAsyncRwLockNewResult")
            : descriptor->shape == R_ASYNC_SYNC_CONSTRUCT_COUNT ? "RStdAsyncSemaphoreNewResult"
            : descriptor->shape == R_ASYNC_SYNC_CONSTRUCT_EMPTY ? "RStdAsyncNotifyNewResult"
            : descriptor->shape == R_ASYNC_SYNC_CONSTRUCT_TYPED ? "RStdAsyncBroadcastNewResult"
            : descriptor->shape == R_ASYNC_SYNC_SUBSCRIBE       ? "RStdAsyncSubscribeResult"
                                                                : "RStdAsyncPublishResult";
        LLVMValueRef native = r_llvm_std_structure(emitter, type_name);
        unsigned count = 0U;
        if (native == NULL) {
            return false;
        }
        switch (descriptor->shape) {
        case R_ASYNC_SYNC_CONSTRUCT_VALUE:
            arguments[count++] = r_llvm_std_allocator(emitter);
            arguments[count++] = r_llvm_type_info(emitter, instruction->runtime_type);
            arguments[count++] =
                r_llvm_scalar_type(emitter, first_type) != NULL
                    ? r_llvm_std_stage(
                          emitter, r_llvm_std_operand(emitter, instruction, 0U), first_type)
                    : first;
            break;
        case R_ASYNC_SYNC_CONSTRUCT_COUNT:
            arguments[count++] = r_llvm_std_allocator(emitter);
            arguments[count++] =
                r_llvm_std_size(emitter, r_llvm_std_operand(emitter, instruction, 0U));
            break;
        case R_ASYNC_SYNC_CONSTRUCT_EMPTY:
            arguments[count++] = r_llvm_std_allocator(emitter);
            break;
        case R_ASYNC_SYNC_CONSTRUCT_TYPED:
            /* A Copy element is copied by the library; any other is cloned through its
               adapter (R-SLIB-ASYNC-0016). */
            arguments[count++] = r_llvm_std_allocator(emitter);
            arguments[count++] = r_llvm_type_info(emitter, instruction->runtime_type);
            arguments[count++] = r_llvm_type_requires_drop(emitter, instruction->runtime_type)
                                     ? r_llvm_broadcast_clone(emitter, instruction->runtime_type)
                                     : LLVMConstNull(r_llvm_pointer(emitter));
            arguments[count++] =
                r_llvm_std_size(emitter, r_llvm_std_operand(emitter, instruction, 0U));
            break;
        case R_ASYNC_SYNC_SUBSCRIBE:
            arguments[count++] = first;
            break;
        default:
            arguments[count++] = first;
            arguments[count++] = second;
            break;
        }
        for (unsigned index = 0U; index < count; ++index) {
            if (arguments[index] == NULL) {
                return false;
            }
        }
        if (r_llvm_call_runtime(emitter, descriptor->native, arguments, count, native) == NULL) {
            return false;
        }
        return r_llvm_std_alloc_carrier(emitter,
                                        instruction,
                                        native,
                                        type_name,
                                        "R_STD_ASYNC_CALL_SUCCESS",
                                        descriptor->shape == R_ASYNC_SYNC_PUBLISH ? "count"
                                                                                  : "value");
    }
    case R_ASYNC_SYNC_QUERY_U64:
    case R_ASYNC_SYNC_CLONE:
    case R_ASYNC_SYNC_HANDLE_USIZE:
    case R_ASYNC_SYNC_ACCESS_REF:
    case R_ASYNC_SYNC_ACCESS_MUT: {
        LLVMValueRef value;
        const unsigned count = descriptor->shape == R_ASYNC_SYNC_QUERY_U64 ? 0U : 1U;
        arguments[0] = first;
        if (r_llvm_scalar_type(emitter, instruction->type) != NULL) {
            value = r_llvm_call_runtime(emitter, descriptor->native, arguments, count, NULL);
            return (value != NULL) && r_llvm_set_value(emitter, instruction->result, value);
        }
        result = r_llvm_std_result(emitter, instruction);
        if ((result == NULL) ||
            (r_llvm_call_runtime(emitter, descriptor->native, arguments, count, result) == NULL)) {
            return false;
        }
        r_llvm_std_initialized(emitter, instruction);
        return true;
    }
    case R_ASYNC_SYNC_SET_U64:
        arguments[0] = LLVMBuildIntCast2(emitter->builder, first, r_llvm_int(emitter, 64U), 0, "");
        return r_llvm_call_runtime(emitter, descriptor->native, arguments, 1U, NULL) != NULL;
    case R_ASYNC_SYNC_TRY: {
        uint32_t payload = 0U;
        LLVMValueRef found;
        result = r_llvm_std_result(emitter, instruction);
        if ((result == NULL) || !r_llvm_payload_offset(emitter, instruction->type, &payload)) {
            return false;
        }
        arguments[0] = first;
        arguments[1] = r_llvm_byte_offset(emitter, result, payload);
        found = r_llvm_call_runtime(emitter, descriptor->native, arguments, 2U, NULL);
        if (found == NULL) {
            return false;
        }
        (void)LLVMBuildStore(emitter->builder,
                             LLVMBuildZExt(emitter->builder, found, r_llvm_int(emitter, 32U), ""),
                             result);
        r_llvm_std_initialized(emitter, instruction);
        return true;
    }
    case R_ASYNC_SYNC_CONSUME: {
        const char *kind =
            descriptor->subject == NULL ? r_llvm_async_guard_kind(emitter, first_type) : NULL;
        int64_t value = 0;
        if ((descriptor->subject == NULL) &&
            ((kind == NULL) || !r_llvm_runtime_constant(emitter, kind, &value))) {
            return kind == NULL ? r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR) : false;
        }
        arguments[0] = first;
        arguments[1] = r_llvm_u32(emitter, (uint64_t)value);
        return r_llvm_call_runtime(
                   emitter, descriptor->native, arguments, kind == NULL ? 1U : 2U, NULL) != NULL;
    }
    case R_ASYNC_SYNC_HANDLE_VOID:
        arguments[0] = first;
        return r_llvm_call_runtime(emitter, descriptor->native, arguments, 1U, NULL) != NULL;
    case R_ASYNC_SYNC_HANDLE_COUNT:
        arguments[0] = first;
        arguments[1] = r_llvm_std_size(emitter, r_llvm_std_operand(emitter, instruction, 1U));
        return (arguments[1] != NULL) &&
               (r_llvm_call_runtime(emitter, descriptor->native, arguments, 2U, NULL) != NULL);
    case R_ASYNC_SYNC_SEND_PERMIT:
        arguments[0] = first;
        arguments[1] = second;
        return r_llvm_call_runtime(emitter, descriptor->native, arguments, 2U, NULL) != NULL;
    case R_ASYNC_SYNC_TRY_OUTCOME: {
        /* RStdSyncTryReserveResult {kind, permit}: reserved moves the permit into the outcome. */
        LLVMValueRef reserved = r_llvm_std_structure(emitter, "RStdSyncTryReserveResult");
        LLVMValueRef kind;
        LLVMBasicBlockRef moved;
        LLVMBasicBlockRef done;
        int64_t reserved_kind = 0;
        uint32_t payload = 0U;
        result = r_llvm_std_result(emitter, instruction);
        arguments[0] = first;
        if ((reserved == NULL) || (result == NULL) ||
            !r_llvm_runtime_constant(
                emitter, "R_STD_SYNC_TRY_RESERVE_RESULT_RESERVED", &reserved_kind) ||
            !r_llvm_sync_payload_offset(emitter, instruction->type, &payload) ||
            (r_llvm_call_runtime(emitter, descriptor->native, arguments, 1U, reserved) == NULL)) {
            return false;
        }
        kind =
            LLVMBuildLoad2(emitter->builder,
                           r_llvm_int(emitter, 32U),
                           r_llvm_std_member(emitter, reserved, "RStdSyncTryReserveResult", "kind"),
                           "");
        (void)LLVMBuildStore(emitter->builder, kind, result);
        moved = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        done = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        (void)LLVMBuildCondBr(emitter->builder,
                              LLVMBuildICmp(emitter->builder,
                                            LLVMIntEQ,
                                            kind,
                                            r_llvm_u32(emitter, (uint64_t)reserved_kind),
                                            ""),
                              moved,
                              done);
        LLVMPositionBuilderAtEnd(emitter->builder, moved);
        arguments[0] = r_llvm_byte_offset(emitter, result, payload);
        arguments[1] = r_llvm_std_member(emitter, reserved, "RStdSyncTryReserveResult", "permit");
        if (r_llvm_call_runtime(
                emitter, "r_std_sync_permit_move_initialize", arguments, 2U, NULL) == NULL) {
            return false;
        }
        (void)LLVMBuildBr(emitter->builder, done);
        LLVMPositionBuilderAtEnd(emitter->builder, done);
        r_llvm_std_initialized(emitter, instruction);
        return true;
    }
    default:
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
}

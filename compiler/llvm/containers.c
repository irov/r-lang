#include "standard_internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* The standard containers of the LLVM emitter (std.array, std.list, std.dict), as the C17 emitter's
   r_c17_emit_async_array_call and r_c17_emit_async_*_container_call. The per-element C17 helpers
   (`ap`, `lpb`, `di`, ...) are inline fast paths of the library entries; these calls go to the
   library entries themselves. */

/* ---- std.array ---- */

static bool r_llvm_std_array_create(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RSemanticType *array =
        r_llvm_type(emitter, r_llvm_value_type(emitter, instruction->type));
    LLVMValueRef native = r_llvm_std_structure(emitter, "RStdArrayCreateResult");
    LLVMValueRef arguments[2];
    LLVMValueRef result;

    if ((native == NULL) || (array == NULL) || (array->kind != R_SEMANTIC_TYPE_ARRAY)) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    arguments[0] = r_llvm_std_allocator(emitter);
    arguments[1] = r_llvm_type_info(emitter, array->base);
    if ((arguments[0] == NULL) || (arguments[1] == NULL) ||
        (r_llvm_call_runtime(emitter, "r_std_array_create", arguments, 2U, native) == NULL) ||
        !r_llvm_check(
            emitter,
            LLVMBuildNot(
                emitter->builder,
                r_llvm_std_status_is(
                    emitter, native, "RStdArrayCreateResult", "status", "R_STD_ARRAY_CALL_SUCCESS"),
                ""),
            "R_RUNTIME_PANIC_CONTRACT_VIOLATION",
            instruction->span,
            R_MIR_BLOCK_ID_INVALID)) {
        return false;
    }
    result = r_llvm_std_result(emitter, instruction);
    if ((result == NULL) ||
        !r_llvm_std_copy(emitter,
                         instruction->type,
                         result,
                         r_llvm_std_member(emitter, native, "RStdArrayCreateResult", "value"))) {
        return false;
    }
    r_llvm_std_initialized(emitter, instruction);
    return true;
}

/* with_capacity and filled: the array, or the allocation error. */
static bool r_llvm_std_array_allocate(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const bool filled = instruction->standard_operation == R_STANDARD_CALL_ARRAY_FILLED;
    const RSemanticType *carrier =
        r_llvm_type(emitter, r_llvm_value_type(emitter, instruction->type));
    const RSemanticType *array =
        carrier == NULL ? NULL : r_llvm_type(emitter, r_llvm_value_type(emitter, carrier->base));
    LLVMValueRef native = r_llvm_std_structure(emitter, "RStdArrayAllocValueResult");
    LLVMValueRef arguments[4];
    unsigned count = 3U;

    if ((native == NULL) || (array == NULL) || (array->kind != R_SEMANTIC_TYPE_ARRAY)) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    arguments[0] = r_llvm_std_allocator(emitter);
    arguments[1] = r_llvm_type_info(emitter, array->base);
    arguments[2] = r_llvm_std_size(emitter, r_llvm_std_operand(emitter, instruction, 0U));
    if (filled) {
        /* R-LIB-0019 (P4.2): the Copy value is staged once and copied by the library. */
        arguments[3] =
            r_llvm_std_stage(emitter, r_llvm_std_operand(emitter, instruction, 1U), array->base);
        count = 4U;
    }
    if ((arguments[0] == NULL) || (arguments[1] == NULL) || (arguments[2] == NULL) ||
        (filled && (arguments[3] == NULL)) ||
        (r_llvm_call_runtime(emitter,
                             filled ? "r_std_array_filled" : "r_std_array_with_capacity",
                             arguments,
                             count,
                             native) == NULL)) {
        return false;
    }
    return r_llvm_std_carrier(
        emitter,
        instruction,
        r_llvm_std_status_is(
            emitter, native, "RStdArrayAllocValueResult", "status", "R_STD_ARRAY_CALL_SUCCESS"),
        r_llvm_std_member(emitter, native, "RStdArrayAllocValueResult", "value"),
        r_llvm_std_member(emitter, native, "RStdArrayAllocValueResult", "error"));
}

/* B7: get and get_mut address the element at the static size of the element type, as
   r_runtime_array_get does at the header's element size. The optimizer cannot know the header's
   size once a call received the array, and a constant stride lets it widen the loads of a loop.
   The option is built from the native result the call would have written. */
static bool r_llvm_std_array_get_now(RLlvmEmitter *emitter,
                                     const RMirInstruction *instruction,
                                     LLVMValueRef array,
                                     LLVMValueRef index,
                                     LLVMValueRef native,
                                     const char *type_name) {
    const RSemanticType *option =
        r_llvm_type(emitter, r_llvm_value_type(emitter, instruction->type));
    const RSemanticType *borrow =
        option == NULL ? NULL : r_llvm_type(emitter, r_llvm_value_type(emitter, option->base));
    uint32_t data = 0U;
    uint32_t length = 0U;
    uint32_t size = 0U;
    uint32_t align = 0U;
    int64_t success = 0;
    LLVMValueRef present;
    LLVMValueRef element;

    if ((borrow == NULL) || !r_llvm_runtime_field(emitter, "RRuntimeArray", "data", &data, NULL) ||
        !r_llvm_runtime_field(emitter, "RRuntimeArray", "length", &length, NULL) ||
        !r_llvm_layout(emitter, borrow->base, &size, &align) ||
        !r_llvm_runtime_constant(emitter, "R_STD_ARRAY_CALL_SUCCESS", &success)) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    {
        LLVMValueRef count = LLVMBuildLoad2(emitter->builder,
                                            r_llvm_int(emitter, 64U),
                                            r_llvm_byte_offset(emitter, array, length),
                                            "");
        /* A masked index below the count is present in every iteration (versions.c). */
        LLVMValueRef fits = r_llvm_version_mask_fits(
            emitter, instruction, r_llvm_std_operand(emitter, instruction, 1U), count);
        present = LLVMBuildICmp(emitter->builder, LLVMIntULT, index, count, "");
        if (fits != NULL) {
            present = LLVMBuildOr(emitter->builder, fits, present, "");
        }
    }
    /* Out of range the address is poison, which the select does not choose. */
    element = r_llvm_element_offset(emitter,
                                    LLVMBuildLoad2(emitter->builder,
                                                   r_llvm_pointer(emitter),
                                                   r_llvm_byte_offset(emitter, array, data),
                                                   ""),
                                    index,
                                    size);
    (void)LLVMBuildStore(emitter->builder,
                         r_llvm_u32(emitter, (uint64_t)success),
                         r_llvm_std_member(emitter, native, type_name, "status"));
    (void)LLVMBuildStore(emitter->builder,
                         LLVMBuildZExt(emitter->builder, present, r_llvm_int(emitter, 8U), ""),
                         r_llvm_std_member(emitter, native, type_name, "has_value"));
    (void)LLVMBuildStore(
        emitter->builder,
        LLVMBuildSelect(
            emitter->builder, present, element, LLVMConstNull(r_llvm_pointer(emitter)), ""),
        r_llvm_std_member(emitter, native, type_name, "value"));
    return true;
}

static bool r_llvm_std_array_operation(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    LLVMValueRef arguments[3];

    arguments[0] = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 0U));
    if (arguments[0] == NULL) {
        return false;
    }
    switch (instruction->standard_operation) {
    case R_STANDARD_CALL_ARRAY_CAPACITY: {
        LLVMValueRef capacity =
            r_llvm_call_runtime(emitter, "r_std_array_capacity", arguments, 1U, NULL);
        return (capacity != NULL) && r_llvm_set_value(emitter, instruction->result, capacity);
    }
    case R_STANDARD_CALL_ARRAY_CLEAR:
        return r_llvm_call_runtime(emitter, "r_std_array_clear", arguments, 1U, NULL) != NULL;
    case R_STANDARD_CALL_ARRAY_RESERVE: {
        LLVMValueRef native = r_llvm_std_structure(emitter, "RStdArrayAllocResult");
        arguments[1] = r_llvm_std_size(emitter, r_llvm_std_operand(emitter, instruction, 1U));
        return (native != NULL) && (arguments[1] != NULL) &&
               (r_llvm_call_runtime(emitter, "r_std_array_reserve", arguments, 2U, native) !=
                NULL) &&
               r_llvm_std_require_status(emitter,
                                         instruction,
                                         native,
                                         "RStdArrayAllocResult",
                                         "R_STD_ARRAY_CALL_CONTRACT_VIOLATION",
                                         true) &&
               r_llvm_std_alloc_carrier(emitter,
                                        instruction,
                                        native,
                                        "RStdArrayAllocResult",
                                        "R_STD_ARRAY_CALL_SUCCESS",
                                        "value");
    }
    case R_STANDARD_CALL_ARRAY_POP:
        return r_llvm_std_value_option(emitter,
                                       instruction,
                                       "r_std_array_pop",
                                       "RStdArrayValueOptionResult",
                                       "R_STD_ARRAY_CALL_SUCCESS",
                                       arguments,
                                       2U);
    case R_STANDARD_CALL_ARRAY_REMOVE:
        arguments[1] = r_llvm_std_size(emitter, r_llvm_std_operand(emitter, instruction, 1U));
        return (arguments[1] != NULL) && r_llvm_std_value_option(emitter,
                                                                 instruction,
                                                                 "r_std_array_remove",
                                                                 "RStdArrayValueOptionResult",
                                                                 "R_STD_ARRAY_CALL_SUCCESS",
                                                                 arguments,
                                                                 3U);
    case R_STANDARD_CALL_ARRAY_GET:
    case R_STANDARD_CALL_ARRAY_GET_MUT: {
        const char *type_name = instruction->standard_operation == R_STANDARD_CALL_ARRAY_GET_MUT
                                    ? "RStdArrayMutPointerOption"
                                    : "RStdArrayConstPointerOption";
        LLVMValueRef native = r_llvm_std_structure(emitter, type_name);
        arguments[1] = r_llvm_std_size(emitter, r_llvm_std_operand(emitter, instruction, 1U));
        return (arguments[1] != NULL) && (native != NULL) &&
               r_llvm_std_array_get_now(
                   emitter, instruction, arguments[0], arguments[1], native, type_name) &&
               r_llvm_std_option_from_native(emitter, instruction, native, type_name);
    }
    default:
        return r_llvm_unsupported(emitter, "this std.array operation");
    }
}

/* iter and next of std.list and std.dict: the iterator, and the option of the next element. */
static bool r_llvm_std_iteration(RLlvmEmitter *emitter,
                                 const RMirInstruction *instruction,
                                 const char *function_name,
                                 const char *type_name,
                                 const char *success,
                                 bool next) {
    LLVMValueRef native = r_llvm_std_structure(emitter, type_name);
    LLVMValueRef source = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 0U));
    LLVMValueRef result;

    if ((native == NULL) || (source == NULL) ||
        (r_llvm_call_runtime(emitter, function_name, &source, 1U, native) == NULL) ||
        !r_llvm_std_require_status(emitter, instruction, native, type_name, success, false)) {
        return false;
    }
    if (next) {
        return r_llvm_std_option_from_native(emitter, instruction, native, type_name);
    }
    result = r_llvm_std_result(emitter, instruction);
    if ((result == NULL) ||
        !r_llvm_std_copy(emitter,
                         instruction->type,
                         result,
                         r_llvm_std_member(emitter, native, type_name, "value"))) {
        return false;
    }
    r_llvm_std_initialized(emitter, instruction);
    return true;
}

static bool r_llvm_std_list_operation(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RStandardCallOperation operation = instruction->standard_operation;
    LLVMValueRef arguments[3];

    if (operation == R_STANDARD_CALL_LIST_CREATE) {
        LLVMValueRef native = r_llvm_std_structure(emitter, "RStdListCreateResult");
        LLVMValueRef result;
        arguments[0] = r_llvm_std_allocator(emitter);
        arguments[1] = r_llvm_type_info(emitter, instruction->auxiliary_type);
        if ((native == NULL) || (arguments[0] == NULL) || (arguments[1] == NULL) ||
            (r_llvm_call_runtime(emitter, "r_std_list_create", arguments, 2U, native) == NULL) ||
            !r_llvm_std_require_status(emitter,
                                       instruction,
                                       native,
                                       "RStdListCreateResult",
                                       "R_STD_LIST_CALL_SUCCESS",
                                       false)) {
            return false;
        }
        result = r_llvm_std_result(emitter, instruction);
        if ((result == NULL) ||
            !r_llvm_std_copy(emitter,
                             instruction->type,
                             result,
                             r_llvm_std_member(emitter, native, "RStdListCreateResult", "value"))) {
            return false;
        }
        r_llvm_std_initialized(emitter, instruction);
        return true;
    }
    arguments[0] = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 0U));
    if (arguments[0] == NULL) {
        return false;
    }
    switch (operation) {
    case R_STANDARD_CALL_LIST_CLEAR:
        return r_llvm_call_runtime(emitter, "r_std_list_clear", arguments, 1U, NULL) != NULL;
    case R_STANDARD_CALL_LIST_ITER:
    case R_STANDARD_CALL_LIST_NEXT:
        return r_llvm_std_iteration(
            emitter,
            instruction,
            operation == R_STANDARD_CALL_LIST_NEXT ? "r_std_list_next" : "r_std_list_iter",
            operation == R_STANDARD_CALL_LIST_NEXT ? "RStdListConstPointerOption"
                                                   : "RStdListIteratorResult",
            "R_STD_LIST_CALL_SUCCESS",
            operation == R_STANDARD_CALL_LIST_NEXT);
    case R_STANDARD_CALL_LIST_PUSH_FRONT:
    case R_STANDARD_CALL_LIST_PUSH_BACK:
        return r_llvm_std_staged_insert(emitter,
                                        instruction,
                                        operation == R_STANDARD_CALL_LIST_PUSH_FRONT
                                            ? "r_std_list_push_front"
                                            : "r_std_list_push_back",
                                        "RStdListInsertResult",
                                        "R_STD_LIST_CALL_SUCCESS",
                                        "R_STD_LIST_CALL_CONTRACT_VIOLATION",
                                        arguments,
                                        2U,
                                        r_llvm_std_operand(emitter, instruction, 1U));
    case R_STANDARD_CALL_LIST_INSERT_BEFORE:
    case R_STANDARD_CALL_LIST_INSERT_AFTER:
        arguments[1] = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 1U));
        return (arguments[1] != NULL) &&
               r_llvm_std_staged_insert(emitter,
                                        instruction,
                                        operation == R_STANDARD_CALL_LIST_INSERT_BEFORE
                                            ? "r_std_list_insert_before"
                                            : "r_std_list_insert_after",
                                        "RStdListInsertResult",
                                        "R_STD_LIST_CALL_SUCCESS",
                                        "R_STD_LIST_CALL_CONTRACT_VIOLATION",
                                        arguments,
                                        3U,
                                        r_llvm_std_operand(emitter, instruction, 2U));
    case R_STANDARD_CALL_LIST_FRONT:
    case R_STANDARD_CALL_LIST_BACK:
    case R_STANDARD_CALL_LIST_FRONT_MUT:
    case R_STANDARD_CALL_LIST_BACK_MUT:
    case R_STANDARD_CALL_LIST_GET:
    case R_STANDARD_CALL_LIST_GET_MUT: {
        static const char *const names[] = {"r_std_list_front",
                                            "r_std_list_back",
                                            "r_std_list_front_mut",
                                            "r_std_list_back_mut",
                                            "r_std_list_get",
                                            "r_std_list_get_mut"};
        const unsigned which = (unsigned)operation - (unsigned)R_STANDARD_CALL_LIST_FRONT;
        const bool is_mutable = (operation == R_STANDARD_CALL_LIST_FRONT_MUT) ||
                                (operation == R_STANDARD_CALL_LIST_BACK_MUT) ||
                                (operation == R_STANDARD_CALL_LIST_GET_MUT);
        const bool indexed =
            (operation == R_STANDARD_CALL_LIST_GET) || (operation == R_STANDARD_CALL_LIST_GET_MUT);
        if (which >= sizeof(names) / sizeof(names[0])) {
            return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
        if (indexed) {
            arguments[1] = r_llvm_std_size(emitter, r_llvm_std_operand(emitter, instruction, 1U));
            if (arguments[1] == NULL) {
                return false;
            }
        }
        return r_llvm_std_pointer_option(emitter,
                                         instruction,
                                         names[which],
                                         is_mutable ? "RStdListMutPointerOption"
                                                    : "RStdListConstPointerOption",
                                         "R_STD_LIST_CALL_SUCCESS",
                                         arguments,
                                         indexed ? 2U : 1U);
    }
    case R_STANDARD_CALL_LIST_POP_FRONT:
    case R_STANDARD_CALL_LIST_POP_BACK:
        return r_llvm_std_value_option(emitter,
                                       instruction,
                                       operation == R_STANDARD_CALL_LIST_POP_FRONT
                                           ? "r_std_list_pop_front"
                                           : "r_std_list_pop_back",
                                       "RStdListValueOptionResult",
                                       "R_STD_LIST_CALL_SUCCESS",
                                       arguments,
                                       2U);
    case R_STANDARD_CALL_LIST_REMOVE: {
        /* The element moves into storage of its own, then into the result (a scalar result is
           an SSA value). */
        LLVMValueRef native = r_llvm_std_structure(emitter, "RStdListValueResult");
        uint32_t size = 0U;
        uint32_t align = 0U;
        LLVMValueRef storage;
        if ((native == NULL) || !r_llvm_layout(emitter, instruction->type, &size, &align)) {
            return false;
        }
        storage = r_llvm_entry_alloca(emitter, size, align, "removed");
        arguments[1] = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 1U));
        arguments[2] = storage;
        if ((arguments[1] == NULL) ||
            (r_llvm_call_runtime(emitter, "r_std_list_remove", arguments, 3U, native) == NULL) ||
            !r_llvm_std_require_status(emitter,
                                       instruction,
                                       native,
                                       "RStdListValueResult",
                                       "R_STD_LIST_CALL_SUCCESS",
                                       false) ||
            !r_llvm_load_into(emitter, instruction->result, instruction->type, storage)) {
            return false;
        }
        r_llvm_std_initialized(emitter, instruction);
        return true;
    }
    default:
        return r_llvm_unsupported(emitter, "this std.list operation");
    }
}

/* The RStdDictKeyInfo of a key type in memory: its type information and key functions. The
   library calls those back while the operations of the current function run. */
static LLVMValueRef r_llvm_std_dict_key_info(RLlvmEmitter *emitter, RTypeId key) {
    LLVMValueRef info = r_llvm_std_structure(emitter, "RRuntimeDictKeyInfo");
    LLVMValueRef type_info = r_llvm_type_info(emitter, key);
    LLVMValueRef hash = r_llvm_key_function(emitter, key, true);
    LLVMValueRef equal = r_llvm_key_function(emitter, key, false);
    uint32_t size = 0U;
    uint32_t align = 0U;

    if ((info == NULL) || (type_info == NULL) || (hash == NULL) || (equal == NULL) ||
        !r_llvm_runtime_layout(emitter, "RRuntimeTypeInfo", &size, &align)) {
        return NULL;
    }
    (void)LLVMBuildMemCpy(emitter->builder,
                          r_llvm_std_member(emitter, info, "RRuntimeDictKeyInfo", "type"),
                          align,
                          type_info,
                          align,
                          r_llvm_u64(emitter, size));
    (void)LLVMBuildStore(
        emitter->builder, hash, r_llvm_std_member(emitter, info, "RRuntimeDictKeyInfo", "hash"));
    (void)LLVMBuildStore(
        emitter->builder, equal, r_llvm_std_member(emitter, info, "RRuntimeDictKeyInfo", "equal"));
    return info;
}

/* An operation that hashes or compares keys runs the key functions on top of this frame. */
static bool r_llvm_std_dict_callbacks(RLlvmEmitter *emitter, RTypeId key) {
    LLVMValueRef hash = r_llvm_key_function(emitter, key, true);
    LLVMValueRef equal = r_llvm_key_function(emitter, key, false);

    return (hash != NULL) && (equal != NULL) &&
           r_llvm_add_stack_edge(emitter, emitter->function, hash) &&
           r_llvm_add_stack_edge(emitter, emitter->function, equal);
}

/* The key type of a dict operand (a borrow of dict<K, V>). */
static RTypeId r_llvm_std_dict_key(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RSemanticType *borrow =
        r_llvm_type(emitter,
                    r_llvm_value_type(emitter,
                                      r_llvm_std_operand_type(
                                          emitter, r_llvm_std_operand(emitter, instruction, 0U))));
    const RSemanticType *dict =
        borrow == NULL ? NULL : r_llvm_type(emitter, r_llvm_value_type(emitter, borrow->base));

    return (dict == NULL) || (dict->kind != R_SEMANTIC_TYPE_DICT) ? R_TYPE_ID_INVALID : dict->base;
}

/* insert: the replaced value, if any, in the option of the success; on an allocation failure
   the staged key and value return in insert_error<K, V>. */
static bool r_llvm_std_dict_insert(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RTypeId error_type = r_llvm_std_single_effect(emitter, instruction->type);
    const RSemanticType *carrier =
        r_llvm_type(emitter, r_llvm_value_type(emitter, instruction->type));
    const RMirValueId key = r_llvm_std_operand(emitter, instruction, 1U);
    const RMirValueId value = r_llvm_std_operand(emitter, instruction, 2U);
    LLVMValueRef native = r_llvm_std_structure(emitter, "RStdDictInsertResult");
    RLlvmRecoveringMembers members;
    LLVMValueRef arguments[4];
    LLVMValueRef result;
    LLVMBasicBlockRef joined;
    LLVMBasicBlockRef inserted;
    LLVMBasicBlockRef replaced;
    LLVMBasicBlockRef failure;
    LLVMBasicBlockRef next;
    uint32_t payload = 0U;
    uint32_t option_payload = 0U;
    uint32_t error_payload = 0U;
    uint32_t reason_size = 0U;
    uint32_t reason_align = 0U;

    if ((native == NULL) || (carrier == NULL) || (error_type == R_TYPE_ID_INVALID) ||
        !r_llvm_recovering_members(emitter, error_type, &members) ||
        !r_llvm_payload_offset(emitter, instruction->type, &payload) ||
        !r_llvm_payload_offset(emitter, carrier->base, &option_payload) ||
        !r_llvm_payload_offset(emitter, error_type, &error_payload) ||
        !r_llvm_runtime_layout(emitter, "RStdAllocError", &reason_size, &reason_align) ||
        !r_llvm_std_dict_callbacks(emitter, members.key_type)) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    arguments[0] = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 0U));
    arguments[1] = r_llvm_std_stage(emitter, key, members.key_type);
    arguments[2] = r_llvm_std_stage(emitter, value, members.value_type);
    result = r_llvm_std_result(emitter, instruction);
    if ((arguments[0] == NULL) || (arguments[1] == NULL) || (arguments[2] == NULL) ||
        (result == NULL)) {
        return false;
    }
    arguments[3] =
        r_llvm_byte_offset(emitter, r_llvm_byte_offset(emitter, result, payload), option_payload);
    joined = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    if (!r_llvm_dict_insert_now(emitter,
                                members.key_type,
                                members.value_type,
                                arguments[0],
                                arguments[1],
                                arguments[2],
                                arguments[3],
                                native,
                                joined) ||
        (r_llvm_call_runtime(emitter, "r_std_dict_insert", arguments, 4U, native) == NULL)) {
        return false;
    }
    (void)LLVMBuildBr(emitter->builder, joined);
    LLVMPositionBuilderAtEnd(emitter->builder, joined);
    if (!r_llvm_std_require_status(emitter,
                                   instruction,
                                   native,
                                   "RStdDictInsertResult",
                                   "R_STD_DICT_CALL_CONTRACT_VIOLATION",
                                   true)) {
        return false;
    }
    inserted = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    replaced = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    failure = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    next = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    (void)LLVMBuildCondBr(
        emitter->builder,
        r_llvm_std_status_is(
            emitter, native, "RStdDictInsertResult", "status", "R_STD_DICT_CALL_SUCCESS"),
        inserted,
        failure);
    LLVMPositionBuilderAtEnd(emitter->builder, inserted);
    (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 0U), result);
    (void)LLVMBuildCondBr(
        emitter->builder,
        LLVMBuildICmp(emitter->builder,
                      LLVMIntNE,
                      LLVMBuildLoad2(
                          emitter->builder,
                          r_llvm_int(emitter, 8U),
                          r_llvm_std_member(emitter, native, "RStdDictInsertResult", "did_replace"),
                          ""),
                      LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                      ""),
        replaced,
        next);
    LLVMPositionBuilderAtEnd(emitter->builder, replaced);
    (void)LLVMBuildStore(
        emitter->builder, r_llvm_u32(emitter, 1U), r_llvm_byte_offset(emitter, result, payload));
    (void)LLVMBuildBr(emitter->builder, next);
    LLVMPositionBuilderAtEnd(emitter->builder, failure);
    {
        /* insert_error<K, V>::allocation_failed {reason, key, value}. */
        LLVMValueRef error = r_llvm_byte_offset(emitter, result, payload);
        LLVMValueRef fields = r_llvm_byte_offset(emitter, error, error_payload);
        (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 1U), result);
        (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 0U), error);
        (void)LLVMBuildMemCpy(emitter->builder,
                              fields,
                              reason_align,
                              r_llvm_std_member(emitter, native, "RStdDictInsertResult", "reason"),
                              reason_align,
                              r_llvm_u64(emitter, reason_size));
        if (!r_llvm_std_copy(emitter,
                             members.key_type,
                             r_llvm_byte_offset(emitter, fields, members.key_offset),
                             arguments[1]) ||
            !r_llvm_std_copy(emitter,
                             members.value_type,
                             r_llvm_byte_offset(emitter, fields, members.value_offset),
                             arguments[2])) {
            return false;
        }
    }
    (void)LLVMBuildBr(emitter->builder, next);
    LLVMPositionBuilderAtEnd(emitter->builder, next);
    r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, key), false);
    r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, value), false);
    r_llvm_std_initialized(emitter, instruction);
    return true;
}

static bool r_llvm_std_dict_operation(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RStandardCallOperation operation = instruction->standard_operation;
    LLVMValueRef arguments[5];

    if (operation == R_STANDARD_CALL_DICT_CREATE) {
        LLVMValueRef native = r_llvm_std_structure(emitter, "RStdDictCreateResult");
        LLVMValueRef result;
        arguments[0] = r_llvm_std_allocator(emitter);
        arguments[1] = r_llvm_std_dict_key_info(emitter, instruction->runtime_type);
        arguments[2] = r_llvm_type_info(emitter, instruction->auxiliary_type);
        arguments[3] = r_llvm_u64(emitter, 0U);
        if ((native == NULL) || (arguments[0] == NULL) || (arguments[1] == NULL) ||
            (arguments[2] == NULL) ||
            (r_llvm_call_runtime(emitter, "r_std_dict_create", arguments, 4U, native) == NULL) ||
            !r_llvm_std_require_status(emitter,
                                       instruction,
                                       native,
                                       "RStdDictCreateResult",
                                       "R_STD_DICT_CALL_SUCCESS",
                                       false)) {
            return false;
        }
        result = r_llvm_std_result(emitter, instruction);
        if ((result == NULL) ||
            !r_llvm_std_copy(emitter,
                             instruction->type,
                             result,
                             r_llvm_std_member(emitter, native, "RStdDictCreateResult", "value"))) {
            return false;
        }
        r_llvm_std_initialized(emitter, instruction);
        return true;
    }
    if (operation == R_STANDARD_CALL_DICT_WITH_CAPACITY) {
        const RSemanticType *carrier =
            r_llvm_type(emitter, r_llvm_value_type(emitter, instruction->type));
        const RSemanticType *dict =
            carrier == NULL ? NULL
                            : r_llvm_type(emitter, r_llvm_value_type(emitter, carrier->base));
        LLVMValueRef native = r_llvm_std_structure(emitter, "RStdDictAllocValueResult");
        if ((dict == NULL) || (dict->kind != R_SEMANTIC_TYPE_DICT) || (native == NULL)) {
            return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
        arguments[0] = r_llvm_std_allocator(emitter);
        arguments[1] = r_llvm_std_dict_key_info(emitter, dict->base);
        arguments[2] = r_llvm_type_info(emitter, dict->second);
        arguments[3] = r_llvm_u64(emitter, 0U);
        arguments[4] = r_llvm_std_size(emitter, r_llvm_std_operand(emitter, instruction, 0U));
        return (arguments[0] != NULL) && (arguments[1] != NULL) && (arguments[2] != NULL) &&
               (arguments[4] != NULL) &&
               (r_llvm_call_runtime(emitter, "r_std_dict_with_capacity", arguments, 5U, native) !=
                NULL) &&
               r_llvm_std_require_status(emitter,
                                         instruction,
                                         native,
                                         "RStdDictAllocValueResult",
                                         "R_STD_DICT_CALL_CONTRACT_VIOLATION",
                                         true) &&
               r_llvm_std_alloc_carrier(emitter,
                                        instruction,
                                        native,
                                        "RStdDictAllocValueResult",
                                        "R_STD_DICT_CALL_SUCCESS",
                                        "value");
    }
    if (operation == R_STANDARD_CALL_DICT_INSERT) {
        return r_llvm_std_dict_insert(emitter, instruction);
    }
    arguments[0] = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 0U));
    if (arguments[0] == NULL) {
        return false;
    }
    if ((operation == R_STANDARD_CALL_DICT_ITER) || (operation == R_STANDARD_CALL_DICT_NEXT)) {
        return r_llvm_std_iteration(
            emitter,
            instruction,
            operation == R_STANDARD_CALL_DICT_NEXT ? "r_std_dict_next" : "r_std_dict_iter",
            operation == R_STANDARD_CALL_DICT_NEXT ? "RStdDictEntryOption"
                                                   : "RStdDictIteratorResult",
            "R_STD_DICT_CALL_SUCCESS",
            operation == R_STANDARD_CALL_DICT_NEXT);
    }
    /* The lookups call the key functions in the program (dicts.c); the runtime calls them for the
       other operations. */
    if ((operation != R_STANDARD_CALL_DICT_CLEAR) && (operation != R_STANDARD_CALL_DICT_GET) &&
        (operation != R_STANDARD_CALL_DICT_GET_MUT) &&
        (operation != R_STANDARD_CALL_DICT_CONTAINS) &&
        !r_llvm_std_dict_callbacks(emitter, r_llvm_std_dict_key(emitter, instruction))) {
        return false;
    }
    switch (operation) {
    case R_STANDARD_CALL_DICT_CLEAR:
        return r_llvm_call_runtime(emitter, "r_std_dict_clear", arguments, 1U, NULL) != NULL;
    case R_STANDARD_CALL_DICT_RESERVE: {
        LLVMValueRef native = r_llvm_std_structure(emitter, "RStdDictAllocResult");
        arguments[1] = r_llvm_std_size(emitter, r_llvm_std_operand(emitter, instruction, 1U));
        return (native != NULL) && (arguments[1] != NULL) &&
               (r_llvm_call_runtime(emitter, "r_std_dict_reserve", arguments, 2U, native) !=
                NULL) &&
               r_llvm_std_require_status(emitter,
                                         instruction,
                                         native,
                                         "RStdDictAllocResult",
                                         "R_STD_DICT_CALL_CONTRACT_VIOLATION",
                                         true) &&
               r_llvm_std_alloc_carrier(emitter,
                                        instruction,
                                        native,
                                        "RStdDictAllocResult",
                                        "R_STD_DICT_CALL_SUCCESS",
                                        "value");
    }
    case R_STANDARD_CALL_DICT_CONTAINS:
    case R_STANDARD_CALL_DICT_GET:
    case R_STANDARD_CALL_DICT_GET_MUT: {
        /* r_std_dict_get, r_std_dict_get_mut and r_std_dict_contains in the program (dicts.c). */
        LLVMValueRef lookup =
            r_llvm_dict_lookup(emitter, r_llvm_std_dict_key(emitter, instruction));
        const char *type_name = operation == R_STANDARD_CALL_DICT_GET_MUT
                                    ? "RStdDictMutPointerOption"
                                    : "RStdDictConstPointerOption";
        LLVMValueRef native;
        LLVMValueRef value;
        LLVMValueRef present;
        int64_t success = 0;
        arguments[1] = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 1U));
        if ((lookup == NULL) || (arguments[1] == NULL)) {
            return false;
        }
        value = LLVMBuildCall2(
            emitter->builder, LLVMGlobalGetValueType(lookup), lookup, arguments, 2U, "");
        present = LLVMBuildICmp(
            emitter->builder, LLVMIntNE, value, LLVMConstNull(r_llvm_pointer(emitter)), "");
        if (operation == R_STANDARD_CALL_DICT_CONTAINS) {
            return r_llvm_set_value(emitter, instruction->result, present);
        }
        native = r_llvm_std_structure(emitter, type_name);
        if ((native == NULL) ||
            !r_llvm_runtime_constant(emitter, "R_STD_DICT_CALL_SUCCESS", &success)) {
            return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
        (void)LLVMBuildStore(emitter->builder,
                             r_llvm_u32(emitter, (uint64_t)success),
                             r_llvm_std_member(emitter, native, type_name, "status"));
        (void)LLVMBuildStore(emitter->builder,
                             LLVMBuildZExt(emitter->builder, present, r_llvm_int(emitter, 8U), ""),
                             r_llvm_std_member(emitter, native, type_name, "has_value"));
        (void)LLVMBuildStore(
            emitter->builder, value, r_llvm_std_member(emitter, native, type_name, "value"));
        return r_llvm_std_option_from_native(emitter, instruction, native, type_name);
    }
    case R_STANDARD_CALL_DICT_REMOVE:
        arguments[1] = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 1U));
        return (arguments[1] != NULL) && r_llvm_std_value_option(emitter,
                                                                 instruction,
                                                                 "r_std_dict_remove",
                                                                 "RStdDictValueOptionResult",
                                                                 "R_STD_DICT_CALL_SUCCESS",
                                                                 arguments,
                                                                 3U);
    default:
        return r_llvm_unsupported(emitter, "this std.dict operation");
    }
}

bool r_llvm_std_container(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    switch (instruction->standard_operation) {
    case R_STANDARD_CALL_ARRAY_CREATE:
        return r_llvm_std_array_create(emitter, instruction);
    case R_STANDARD_CALL_ARRAY_WITH_CAPACITY:
    case R_STANDARD_CALL_ARRAY_FILLED:
        return r_llvm_std_array_allocate(emitter, instruction);
    case R_STANDARD_CALL_ARRAY_PUSH: {
        LLVMValueRef arguments[2];
        arguments[0] = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 0U));
        return (arguments[0] != NULL) &&
               r_llvm_std_staged_insert(emitter,
                                        instruction,
                                        "r_std_array_push",
                                        "RStdArrayPushResult",
                                        "R_STD_ARRAY_CALL_SUCCESS",
                                        NULL,
                                        arguments,
                                        2U,
                                        r_llvm_std_operand(emitter, instruction, 1U));
    }
    case R_STANDARD_CALL_ARRAY_CAPACITY:
    case R_STANDARD_CALL_ARRAY_RESERVE:
    case R_STANDARD_CALL_ARRAY_POP:
    case R_STANDARD_CALL_ARRAY_REMOVE:
    case R_STANDARD_CALL_ARRAY_GET:
    case R_STANDARD_CALL_ARRAY_GET_MUT:
    case R_STANDARD_CALL_ARRAY_CLEAR:
        return r_llvm_std_array_operation(emitter, instruction);
    default:
        if ((instruction->standard_operation >= R_STANDARD_CALL_LIST_CREATE) &&
            (instruction->standard_operation <= R_STANDARD_CALL_LIST_NEXT)) {
            return r_llvm_std_list_operation(emitter, instruction);
        }
        return r_llvm_std_dict_operation(emitter, instruction);
    }
}

#include "standard_internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* Clone glue (R-OWN-0020, L26; the C17 emitter's clone.inc). core::clone of a Move value calls one
   internal function per cloned type,

       i32 r_clone.<type>(ptr to, ptr from, ptr span)

   which builds an independent copy in uninitialized storage and answers 0, or on failure destroys
   every component it already built, leaves the destination unowned and answers the
   std.alloc::alloc_error plus one; the source is never changed. A Copy component is copied
   bitwise. A use declares the function; its body is written after the functions of the program
   (r_llvm_emit_clone_glue), and writing it may declare more. */

static bool r_llvm_clone_is_copy(RLlvmEmitter *emitter, RTypeId type) {
    return !r_llvm_type_requires_drop(emitter, type);
}

/* The clone function of a type, declared at its first use. */
static LLVMValueRef r_llvm_clone_function(RLlvmEmitter *emitter, RTypeId type) {
    const RTypeId value = r_llvm_value_type(emitter, type);
    LLVMTypeRef parameters[3];
    char name[64];

    if ((value == R_TYPE_ID_INVALID) || ((size_t)value > emitter->frontend->semantic_type_count)) {
        (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        return NULL;
    }
    if (emitter->clone_glue[value] == NULL) {
        parameters[0] = r_llvm_pointer(emitter);
        parameters[1] = r_llvm_pointer(emitter);
        parameters[2] = r_llvm_pointer(emitter);
        (void)snprintf(name, sizeof(name), "r_clone.%" PRIu32, r_llvm_type_key(emitter, value));
        emitter->clone_glue[value] = LLVMAddFunction(
            emitter->module, name, LLVMFunctionType(r_llvm_int(emitter, 32U), parameters, 3U, 0));
        LLVMSetLinkage(emitter->clone_glue[value], LLVMInternalLinkage);
    }
    return emitter->clone_glue[value];
}

/* Calls the clone of a type: 0 or the error plus one. */
static LLVMValueRef r_llvm_clone_call(
    RLlvmEmitter *emitter, RTypeId type, LLVMValueRef to, LLVMValueRef from, LLVMValueRef span) {
    LLVMValueRef function = r_llvm_clone_function(emitter, type);
    LLVMValueRef arguments[3];

    if (function == NULL) {
        return NULL;
    }
    arguments[0] = to;
    arguments[1] = from;
    arguments[2] = span;
    return LLVMBuildCall2(
        emitter->builder, LLVMGlobalGetValueType(function), function, arguments, 3U, "");
}

/* r_clone_failure: the alloc_error plus one of a failed runtime status (2 out of memory or
   budget, 3 size overflow, 4 unsupported alignment); any other status breaks the contract. */
static LLVMValueRef
r_llvm_clone_failure(RLlvmEmitter *emitter, LLVMValueRef status, LLVMValueRef span) {
    LLVMBasicBlockRef invalid =
        LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef valid =
        LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMValueRef budget;
    LLVMValueRef error;
    LLVMValueRef arguments[2];
    int64_t memory = 0;
    int64_t exhausted = 0;
    int64_t overflow = 0;
    int64_t alignment = 0;
    int64_t contract = 0;

    if (!r_llvm_runtime_constant(emitter, "R_STD_ALLOC_ERROR_OUT_OF_MEMORY", &memory) ||
        !r_llvm_runtime_constant(emitter, "R_STD_ALLOC_ERROR_BUDGET_EXHAUSTED", &exhausted) ||
        !r_llvm_runtime_constant(emitter, "R_STD_ALLOC_ERROR_SIZE_OVERFLOW", &overflow) ||
        !r_llvm_runtime_constant(emitter, "R_STD_ALLOC_ERROR_UNSUPPORTED_ALIGNMENT", &alignment) ||
        !r_llvm_runtime_constant(emitter, "R_RUNTIME_PANIC_CONTRACT_VIOLATION", &contract)) {
        return NULL;
    }
    status = LLVMBuildIntCast2(emitter->builder, status, r_llvm_int(emitter, 32U), 0, "");
    (void)LLVMBuildCondBr(
        emitter->builder,
        LLVMBuildAnd(
            emitter->builder,
            LLVMBuildICmp(emitter->builder, LLVMIntUGE, status, r_llvm_u32(emitter, 2U), ""),
            LLVMBuildICmp(emitter->builder, LLVMIntULE, status, r_llvm_u32(emitter, 4U), ""),
            ""),
        valid,
        invalid);
    LLVMPositionBuilderAtEnd(emitter->builder, invalid);
    arguments[0] = r_llvm_u32(emitter, (uint64_t)contract);
    arguments[1] = span;
    if (r_llvm_call_runtime(emitter, "r_runtime_panic", arguments, 2U, NULL) == NULL) {
        return NULL;
    }
    (void)LLVMBuildUnreachable(emitter->builder);
    LLVMPositionBuilderAtEnd(emitter->builder, valid);
    budget = r_llvm_call_runtime(emitter, "r_runtime_allocation_refused_by_budget", NULL, 0U, NULL);
    if (budget == NULL) {
        return NULL;
    }
    error = LLVMBuildSelect(emitter->builder,
                            budget,
                            r_llvm_u32(emitter, (uint64_t)exhausted),
                            r_llvm_u32(emitter, (uint64_t)memory),
                            "");
    error = LLVMBuildSelect(
        emitter->builder,
        LLVMBuildICmp(emitter->builder, LLVMIntEQ, status, r_llvm_u32(emitter, 3U), ""),
        r_llvm_u32(emitter, (uint64_t)overflow),
        error,
        "");
    error = LLVMBuildSelect(
        emitter->builder,
        LLVMBuildICmp(emitter->builder, LLVMIntEQ, status, r_llvm_u32(emitter, 4U), ""),
        r_llvm_u32(emitter, (uint64_t)alignment),
        error,
        "");
    return LLVMBuildAdd(emitter->builder, error, r_llvm_u32(emitter, 1U), "");
}

/* One component: a Copy one is copied, a Move one cloned; on failure `undo` runs (the components
   built before it are destroyed by the caller's block) and the failure is answered. */
static bool r_llvm_clone_component(RLlvmEmitter *emitter,
                                   RTypeId type,
                                   LLVMValueRef to,
                                   LLVMValueRef from,
                                   LLVMValueRef span,
                                   LLVMBasicBlockRef failed,
                                   LLVMValueRef failure) {
    LLVMValueRef answer;
    LLVMBasicBlockRef next;

    if (r_llvm_clone_is_copy(emitter, type)) {
        return r_llvm_std_copy(emitter, type, to, from);
    }
    answer = r_llvm_clone_call(emitter, type, to, from, span);
    if (answer == NULL) {
        return false;
    }
    (void)LLVMBuildStore(emitter->builder, answer, failure);
    next = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    (void)LLVMBuildCondBr(
        emitter->builder,
        LLVMBuildICmp(emitter->builder, LLVMIntNE, answer, r_llvm_u32(emitter, 0U), ""),
        failed,
        next);
    LLVMPositionBuilderAtEnd(emitter->builder, next);
    return true;
}

static LLVMValueRef r_llvm_clone_member(RLlvmEmitter *emitter,
                                        LLVMValueRef base,
                                        const char *type_name,
                                        const char *member) {
    uint32_t offset = 0U;

    return r_llvm_runtime_field(emitter, type_name, member, &offset, NULL)
               ? r_llvm_byte_offset(emitter, base, offset)
               : NULL;
}

/* array<T>: one allocation of the source length, then the elements in order. */
static bool r_llvm_clone_array(
    RLlvmEmitter *emitter, RTypeId element, LLVMValueRef to, LLVMValueRef from, LLVMValueRef span) {
    LLVMValueRef arguments[4];
    LLVMValueRef status;
    LLVMValueRef length;
    LLVMValueRef index = r_llvm_entry_alloca(emitter, 8U, 8U, "index");
    LLVMValueRef failure = r_llvm_entry_alloca(emitter, 4U, 4U, "failure");
    LLVMBasicBlockRef allocated =
        LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef refused =
        LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    uint32_t size = 0U;
    uint32_t align = 0U;

    arguments[0] = to;
    arguments[1] = r_llvm_std_allocator(emitter);
    arguments[2] = r_llvm_clone_member(emitter, from, "RRuntimeArray", "element");
    length = LLVMBuildLoad2(emitter->builder,
                            r_llvm_int(emitter, 64U),
                            r_llvm_clone_member(emitter, from, "RRuntimeArray", "length"),
                            "");
    arguments[3] = length;
    if ((arguments[1] == NULL) || (arguments[2] == NULL) ||
        !r_llvm_layout(emitter, element, &size, &align)) {
        return false;
    }
    status = r_llvm_call_runtime(emitter, "r_runtime_array_with_capacity", arguments, 4U, NULL);
    if (status == NULL) {
        return false;
    }
    (void)LLVMBuildCondBr(
        emitter->builder,
        LLVMBuildICmp(
            emitter->builder, LLVMIntEQ, status, LLVMConstInt(LLVMTypeOf(status), 0U, 0), ""),
        allocated,
        refused);
    LLVMPositionBuilderAtEnd(emitter->builder, refused);
    {
        LLVMValueRef error = r_llvm_clone_failure(emitter, status, span);
        if (error == NULL) {
            return false;
        }
        (void)LLVMBuildRet(emitter->builder, error);
    }
    LLVMPositionBuilderAtEnd(emitter->builder, allocated);
    {
        LLVMValueRef target =
            LLVMBuildLoad2(emitter->builder,
                           r_llvm_pointer(emitter),
                           r_llvm_clone_member(emitter, to, "RRuntimeArray", "data"),
                           "");
        LLVMValueRef source =
            LLVMBuildLoad2(emitter->builder,
                           r_llvm_pointer(emitter),
                           r_llvm_clone_member(emitter, from, "RRuntimeArray", "data"),
                           "");
        LLVMValueRef destination_length =
            r_llvm_clone_member(emitter, to, "RRuntimeArray", "length");
        LLVMBasicBlockRef test;
        LLVMBasicBlockRef body;
        LLVMBasicBlockRef failed;
        LLVMBasicBlockRef done;
        LLVMValueRef current;
        LLVMValueRef offset;
        if (r_llvm_clone_is_copy(emitter, element)) {
            (void)LLVMBuildMemCpy(
                emitter->builder,
                target,
                1U,
                source,
                1U,
                LLVMBuildMul(emitter->builder, length, r_llvm_u64(emitter, size), ""));
            (void)LLVMBuildStore(emitter->builder, length, destination_length);
            (void)LLVMBuildRet(emitter->builder, r_llvm_u32(emitter, 0U));
            return true;
        }
        test = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        body = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        failed = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        done = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        (void)LLVMBuildStore(emitter->builder, r_llvm_u64(emitter, 0U), index);
        (void)LLVMBuildBr(emitter->builder, test);
        LLVMPositionBuilderAtEnd(emitter->builder, test);
        current = LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 64U), index, "");
        (void)LLVMBuildCondBr(emitter->builder,
                              LLVMBuildICmp(emitter->builder, LLVMIntULT, current, length, ""),
                              body,
                              done);
        LLVMPositionBuilderAtEnd(emitter->builder, body);
        offset = LLVMBuildMul(emitter->builder, current, r_llvm_u64(emitter, size), "");
        if (!r_llvm_clone_component(
                emitter,
                element,
                LLVMBuildGEP2(emitter->builder, r_llvm_int(emitter, 8U), target, &offset, 1U, ""),
                LLVMBuildGEP2(emitter->builder, r_llvm_int(emitter, 8U), source, &offset, 1U, ""),
                span,
                failed,
                failure)) {
            return false;
        }
        current = LLVMBuildAdd(emitter->builder, current, r_llvm_u64(emitter, 1U), "");
        (void)LLVMBuildStore(emitter->builder, current, index);
        (void)LLVMBuildStore(emitter->builder, current, destination_length);
        (void)LLVMBuildBr(emitter->builder, test);
        LLVMPositionBuilderAtEnd(emitter->builder, failed);
        /* The length counts the elements built; the array destroys them. */
        if (r_llvm_call_runtime(emitter, "r_runtime_array_destroy", &to, 1U, NULL) == NULL) {
            return false;
        }
        (void)LLVMBuildRet(emitter->builder,
                           LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), failure, ""));
        LLVMPositionBuilderAtEnd(emitter->builder, done);
        (void)LLVMBuildRet(emitter->builder, r_llvm_u32(emitter, 0U));
        return true;
    }
}

/* list<T>: the elements are appended in order; a failed append destroys the staged element. */
static bool r_llvm_clone_list(
    RLlvmEmitter *emitter, RTypeId element, LLVMValueRef to, LLVMValueRef from, LLVMValueRef span) {
    LLVMValueRef failure = r_llvm_entry_alloca(emitter, 4U, 4U, "failure");
    LLVMValueRef stored = r_llvm_entry_alloca(emitter, 8U, 8U, "stored");
    LLVMValueRef cursor = r_llvm_std_structure(emitter, "RRuntimeListIterator");
    LLVMValueRef arguments[3];
    LLVMValueRef status;
    LLVMValueRef staged;
    LLVMValueRef current;
    LLVMBasicBlockRef refused =
        LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef test = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef body = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef failed =
        LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef appended =
        LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef unappended =
        LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    uint32_t size = 0U;
    uint32_t align = 0U;

    if ((cursor == NULL) || !r_llvm_layout(emitter, element, &size, &align)) {
        return false;
    }
    staged = r_llvm_entry_alloca(emitter, size, align, "staged");
    arguments[0] = to;
    arguments[1] = r_llvm_std_allocator(emitter);
    arguments[2] = r_llvm_clone_member(emitter, from, "RRuntimeList", "element");
    status = (arguments[1] == NULL) || (arguments[2] == NULL)
                 ? NULL
                 : r_llvm_call_runtime(emitter, "r_runtime_list_initialize", arguments, 3U, NULL);
    if ((status == NULL) ||
        (r_llvm_call_runtime(emitter, "r_runtime_list_iter", &from, 1U, cursor) == NULL)) {
        return false;
    }
    (void)LLVMBuildCondBr(
        emitter->builder,
        LLVMBuildICmp(
            emitter->builder, LLVMIntEQ, status, LLVMConstInt(LLVMTypeOf(status), 0U, 0), ""),
        test,
        refused);
    LLVMPositionBuilderAtEnd(emitter->builder, refused);
    {
        LLVMValueRef error = r_llvm_clone_failure(emitter, status, span);
        if (error == NULL) {
            return false;
        }
        (void)LLVMBuildRet(emitter->builder, error);
    }
    LLVMPositionBuilderAtEnd(emitter->builder, test);
    current = r_llvm_call_runtime(emitter, "r_runtime_list_next", &cursor, 1U, NULL);
    if (current == NULL) {
        return false;
    }
    (void)LLVMBuildCondBr(
        emitter->builder,
        LLVMBuildICmp(
            emitter->builder, LLVMIntEQ, current, LLVMConstNull(r_llvm_pointer(emitter)), ""),
        done,
        body);
    LLVMPositionBuilderAtEnd(emitter->builder, body);
    if (!r_llvm_clone_component(emitter, element, staged, current, span, failed, failure)) {
        return false;
    }
    arguments[0] = to;
    arguments[1] = staged;
    arguments[2] = stored;
    status = r_llvm_call_runtime(emitter, "r_runtime_list_push_back", arguments, 3U, NULL);
    if (status == NULL) {
        return false;
    }
    (void)LLVMBuildCondBr(
        emitter->builder,
        LLVMBuildICmp(
            emitter->builder, LLVMIntEQ, status, LLVMConstInt(LLVMTypeOf(status), 0U, 0), ""),
        appended,
        unappended);
    LLVMPositionBuilderAtEnd(emitter->builder, appended);
    (void)LLVMBuildBr(emitter->builder, test);
    LLVMPositionBuilderAtEnd(emitter->builder, unappended);
    if (!r_llvm_clone_is_copy(emitter, element) && !r_llvm_call_drop(emitter, element, staged)) {
        return false;
    }
    if (r_llvm_call_runtime(emitter, "r_runtime_list_destroy", &to, 1U, NULL) == NULL) {
        return false;
    }
    {
        LLVMValueRef error = r_llvm_clone_failure(emitter, status, span);
        if (error == NULL) {
            return false;
        }
        (void)LLVMBuildRet(emitter->builder, error);
    }
    LLVMPositionBuilderAtEnd(emitter->builder, failed);
    if (r_llvm_call_runtime(emitter, "r_runtime_list_destroy", &to, 1U, NULL) == NULL) {
        return false;
    }
    (void)LLVMBuildRet(emitter->builder,
                       LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), failure, ""));
    LLVMPositionBuilderAtEnd(emitter->builder, done);
    (void)LLVMBuildRet(emitter->builder, r_llvm_u32(emitter, 0U));
    return true;
}

/* dict<K, V>: the same capacity and seed, then each entry's key and value; a key that replaces
   another breaks the contract of the source. */
static bool r_llvm_clone_dict(RLlvmEmitter *emitter,
                              RTypeId key,
                              RTypeId value,
                              LLVMValueRef to,
                              LLVMValueRef from,
                              LLVMValueRef span) {
    LLVMValueRef failure = r_llvm_entry_alloca(emitter, 4U, 4U, "failure");
    LLVMValueRef cursor = r_llvm_std_structure(emitter, "RRuntimeDictIterator");
    LLVMValueRef entry = r_llvm_std_structure(emitter, "RRuntimeDictEntryRef");
    LLVMValueRef replaced_flag = r_llvm_entry_alloca(emitter, 1U, 1U, "did_replace");
    LLVMValueRef arguments[6];
    LLVMValueRef status;
    LLVMValueRef staged_key;
    LLVMValueRef staged_value;
    LLVMValueRef replaced;
    LLVMBasicBlockRef refused =
        LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef test = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef body = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef key_failed =
        LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef value_failed =
        LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef inserted =
        LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef rejected =
        LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef duplicate =
        LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef unstaged =
        LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    uint32_t key_size = 0U;
    uint32_t key_align = 0U;
    uint32_t value_size = 0U;
    uint32_t value_align = 0U;
    uint32_t key_offset = 0U;
    uint32_t value_offset = 0U;
    int64_t contract = 0;

    if ((cursor == NULL) || (entry == NULL) ||
        !r_llvm_layout(emitter, key, &key_size, &key_align) ||
        !r_llvm_layout(emitter, value, &value_size, &value_align) ||
        !r_llvm_runtime_field(emitter, "RRuntimeDictEntryRef", "key", &key_offset, NULL) ||
        !r_llvm_runtime_field(emitter, "RRuntimeDictEntryRef", "value", &value_offset, NULL) ||
        !r_llvm_runtime_constant(emitter, "R_RUNTIME_PANIC_CONTRACT_VIOLATION", &contract)) {
        return false;
    }
    staged_key = r_llvm_entry_alloca(emitter, key_size, key_align, "staged_key");
    staged_value = r_llvm_entry_alloca(emitter, value_size, value_align, "staged_value");
    replaced = r_llvm_entry_alloca(emitter, value_size, value_align, "replaced");
    arguments[0] = to;
    arguments[1] = r_llvm_std_allocator(emitter);
    arguments[2] = r_llvm_clone_member(emitter, from, "RRuntimeDict", "key");
    arguments[3] = r_llvm_clone_member(emitter, from, "RRuntimeDict", "value");
    arguments[4] = LLVMBuildLoad2(emitter->builder,
                                  r_llvm_int(emitter, 64U),
                                  r_llvm_clone_member(emitter, from, "RRuntimeDict", "seed"),
                                  "");
    arguments[5] = LLVMBuildLoad2(emitter->builder,
                                  r_llvm_int(emitter, 64U),
                                  r_llvm_clone_member(emitter, from, "RRuntimeDict", "length"),
                                  "");
    status =
        (arguments[1] == NULL) || (arguments[2] == NULL) || (arguments[3] == NULL)
            ? NULL
            : r_llvm_call_runtime(emitter, "r_runtime_dict_with_capacity", arguments, 6U, NULL);
    if ((status == NULL) ||
        (r_llvm_call_runtime(emitter, "r_runtime_dict_iter", &from, 1U, cursor) == NULL)) {
        return false;
    }
    (void)LLVMBuildCondBr(
        emitter->builder,
        LLVMBuildICmp(
            emitter->builder, LLVMIntEQ, status, LLVMConstInt(LLVMTypeOf(status), 0U, 0), ""),
        test,
        refused);
    LLVMPositionBuilderAtEnd(emitter->builder, refused);
    {
        LLVMValueRef error = r_llvm_clone_failure(emitter, status, span);
        if (error == NULL) {
            return false;
        }
        (void)LLVMBuildRet(emitter->builder, error);
    }
    LLVMPositionBuilderAtEnd(emitter->builder, test);
    arguments[0] = cursor;
    arguments[1] = entry;
    {
        LLVMValueRef more =
            r_llvm_call_runtime(emitter, "r_runtime_dict_next", arguments, 2U, NULL);
        if (more == NULL) {
            return false;
        }
        (void)LLVMBuildCondBr(emitter->builder, more, body, done);
    }
    LLVMPositionBuilderAtEnd(emitter->builder, body);
    if (!r_llvm_clone_component(emitter,
                                key,
                                staged_key,
                                LLVMBuildLoad2(emitter->builder,
                                               r_llvm_pointer(emitter),
                                               r_llvm_byte_offset(emitter, entry, key_offset),
                                               ""),
                                span,
                                key_failed,
                                failure) ||
        !r_llvm_clone_component(emitter,
                                value,
                                staged_value,
                                LLVMBuildLoad2(emitter->builder,
                                               r_llvm_pointer(emitter),
                                               r_llvm_byte_offset(emitter, entry, value_offset),
                                               ""),
                                span,
                                value_failed,
                                failure)) {
        return false;
    }
    (void)LLVMBuildStore(
        emitter->builder, LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0), replaced_flag);
    arguments[0] = to;
    arguments[1] = staged_key;
    arguments[2] = staged_value;
    arguments[3] = replaced;
    arguments[4] = replaced_flag;
    status = r_llvm_call_runtime(emitter, "r_runtime_dict_insert", arguments, 5U, NULL);
    if (status == NULL) {
        return false;
    }
    (void)LLVMBuildCondBr(
        emitter->builder,
        LLVMBuildOr(
            emitter->builder,
            LLVMBuildICmp(
                emitter->builder, LLVMIntNE, status, LLVMConstInt(LLVMTypeOf(status), 0U, 0), ""),
            LLVMBuildICmp(
                emitter->builder,
                LLVMIntNE,
                LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 8U), replaced_flag, ""),
                LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                ""),
            ""),
        rejected,
        inserted);
    LLVMPositionBuilderAtEnd(emitter->builder, inserted);
    (void)LLVMBuildBr(emitter->builder, test);
    LLVMPositionBuilderAtEnd(emitter->builder, rejected);
    (void)LLVMBuildCondBr(
        emitter->builder,
        LLVMBuildICmp(
            emitter->builder, LLVMIntEQ, status, LLVMConstInt(LLVMTypeOf(status), 0U, 0), ""),
        duplicate,
        unstaged);
    LLVMPositionBuilderAtEnd(emitter->builder, duplicate);
    arguments[0] = r_llvm_u32(emitter, (uint64_t)contract);
    arguments[1] = span;
    if (r_llvm_call_runtime(emitter, "r_runtime_panic", arguments, 2U, NULL) == NULL) {
        return false;
    }
    (void)LLVMBuildUnreachable(emitter->builder);
    LLVMPositionBuilderAtEnd(emitter->builder, unstaged);
    if ((!r_llvm_clone_is_copy(emitter, value) &&
         !r_llvm_call_drop(emitter, value, staged_value)) ||
        (!r_llvm_clone_is_copy(emitter, key) && !r_llvm_call_drop(emitter, key, staged_key)) ||
        (r_llvm_call_runtime(emitter, "r_runtime_dict_destroy", &to, 1U, NULL) == NULL)) {
        return false;
    }
    {
        LLVMValueRef error = r_llvm_clone_failure(emitter, status, span);
        if (error == NULL) {
            return false;
        }
        (void)LLVMBuildRet(emitter->builder, error);
    }
    LLVMPositionBuilderAtEnd(emitter->builder, value_failed);
    if (!r_llvm_clone_is_copy(emitter, key) && !r_llvm_call_drop(emitter, key, staged_key)) {
        return false;
    }
    (void)LLVMBuildBr(emitter->builder, key_failed);
    LLVMPositionBuilderAtEnd(emitter->builder, key_failed);
    if (r_llvm_call_runtime(emitter, "r_runtime_dict_destroy", &to, 1U, NULL) == NULL) {
        return false;
    }
    (void)LLVMBuildRet(emitter->builder,
                       LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), failure, ""));
    LLVMPositionBuilderAtEnd(emitter->builder, done);
    (void)LLVMBuildRet(emitter->builder, r_llvm_u32(emitter, 0U));
    return true;
}

/* own<T>: the value is cloned, then moved into a new allocation of its type. */
static bool r_llvm_clone_own(
    RLlvmEmitter *emitter, RTypeId base, LLVMValueRef to, LLVMValueRef from, LLVMValueRef span) {
    LLVMValueRef failure = r_llvm_entry_alloca(emitter, 4U, 4U, "failure");
    LLVMBasicBlockRef failed =
        LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef refused =
        LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef created =
        LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMValueRef arguments[4];
    LLVMValueRef staged;
    LLVMValueRef value;
    LLVMValueRef status;
    uint32_t size = 0U;
    uint32_t align = 0U;

    if (!r_llvm_layout(emitter, base, &size, &align)) {
        return false;
    }
    staged = r_llvm_entry_alloca(emitter, size, align, "staged");
    value = r_llvm_call_runtime(emitter, "r_runtime_own_get", &from, 1U, NULL);
    if ((value == NULL) ||
        !r_llvm_clone_component(emitter, base, staged, value, span, failed, failure)) {
        return false;
    }
    arguments[0] = r_llvm_std_allocator(emitter);
    arguments[1] = r_llvm_clone_member(emitter, from, "RRuntimeOwn", "type");
    arguments[2] = staged;
    arguments[3] = to;
    if ((arguments[0] == NULL) || (arguments[1] == NULL)) {
        return false;
    }
    status = r_llvm_call_runtime(emitter, "r_runtime_own_create", arguments, 4U, NULL);
    if (status == NULL) {
        return false;
    }
    (void)LLVMBuildCondBr(
        emitter->builder,
        LLVMBuildICmp(
            emitter->builder, LLVMIntEQ, status, LLVMConstInt(LLVMTypeOf(status), 0U, 0), ""),
        created,
        refused);
    LLVMPositionBuilderAtEnd(emitter->builder, refused);
    if (!r_llvm_clone_is_copy(emitter, base) && !r_llvm_call_drop(emitter, base, staged)) {
        return false;
    }
    {
        LLVMValueRef error = r_llvm_clone_failure(emitter, status, span);
        if (error == NULL) {
            return false;
        }
        (void)LLVMBuildRet(emitter->builder, error);
    }
    LLVMPositionBuilderAtEnd(emitter->builder, failed);
    (void)LLVMBuildRet(emitter->builder,
                       LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), failure, ""));
    LLVMPositionBuilderAtEnd(emitter->builder, created);
    (void)LLVMBuildRet(emitter->builder, r_llvm_u32(emitter, 0U));
    return true;
}

/* An aggregate with fields cloned in declaration order: a failure destroys the Move fields
   already built, newest first. */
static bool r_llvm_clone_struct(RLlvmEmitter *emitter,
                                const RSemanticAggregate *aggregate,
                                LLVMValueRef to,
                                LLVMValueRef from,
                                LLVMValueRef span) {
    LLVMValueRef failure = r_llvm_entry_alloca(emitter, 4U, 4U, "failure");
    LLVMBasicBlockRef unwind[256];
    uint32_t index;

    if (aggregate->field_count > 256U) {
        return r_llvm_unsupported(emitter, "a clone of an aggregate with this many fields");
    }
    for (index = 0U; index < aggregate->field_count; ++index) {
        const uint32_t field_id = aggregate->first_field + index + 1U;
        const RSemanticField *field = r_llvm_field(emitter, field_id);
        uint32_t offset = 0U;
        LLVMBasicBlockRef failed;
        if ((field == NULL) || !r_llvm_field_offset(emitter, field_id, &offset)) {
            return field == NULL ? r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR) : false;
        }
        failed = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        if (!r_llvm_clone_component(emitter,
                                    field->type,
                                    r_llvm_byte_offset(emitter, to, offset),
                                    r_llvm_byte_offset(emitter, from, offset),
                                    span,
                                    failed,
                                    failure)) {
            return false;
        }
        unwind[index] = failed;
    }
    (void)LLVMBuildRet(emitter->builder, r_llvm_u32(emitter, 0U));
    /* The failure block of field i destroys the Move fields before it. */
    for (index = 0U; index < aggregate->field_count; ++index) {
        uint32_t earlier = index;
        LLVMPositionBuilderAtEnd(emitter->builder, unwind[index]);
        while (earlier != 0U) {
            const uint32_t field_id = aggregate->first_field + earlier;
            const RSemanticField *field = r_llvm_field(emitter, field_id);
            uint32_t offset = 0U;
            earlier -= 1U;
            if ((field == NULL) || !r_llvm_field_offset(emitter, field_id, &offset)) {
                return false;
            }
            if (!r_llvm_clone_is_copy(emitter, field->type) &&
                !r_llvm_call_drop(emitter, field->type, r_llvm_byte_offset(emitter, to, offset))) {
                return false;
            }
        }
        (void)LLVMBuildRet(emitter->builder,
                           LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), failure, ""));
    }
    return true;
}

/* A tagged value: the payload of the active variant, then the tag. */
static bool r_llvm_clone_tagged(
    RLlvmEmitter *emitter, RTypeId type, LLVMValueRef to, LLVMValueRef from, LLVMValueRef span) {
    const RSemanticType *semantic = r_llvm_type(emitter, r_llvm_value_type(emitter, type));
    const RSemanticAggregate *aggregate = r_semantic_tagged_enum(emitter->frontend, type);
    LLVMValueRef failure = r_llvm_entry_alloca(emitter, 4U, 4U, "failure");
    LLVMBasicBlockRef failed =
        LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMValueRef tag = LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), from, "");
    LLVMValueRef choice;
    uint32_t payload = 0U;
    uint32_t variants;
    uint32_t index;

    if ((semantic == NULL) || !r_llvm_payload_offset(emitter, type, &payload)) {
        return false;
    }
    variants = semantic->kind == R_SEMANTIC_TYPE_OPTION ? 2U
               : aggregate != NULL                      ? aggregate->variant_count
                                                        : 0U;
    choice = LLVMBuildSwitch(emitter->builder, tag, done, variants);
    for (index = 0U; index < variants; ++index) {
        const RTypeId payload_type =
            semantic->kind == R_SEMANTIC_TYPE_OPTION
                ? (index == 1U ? semantic->base : R_TYPE_ID_INVALID)
                : (r_semantic_tagged_variant(emitter->frontend, type, index) == NULL
                       ? R_TYPE_ID_INVALID
                       : r_semantic_tagged_variant(emitter->frontend, type, index)->payload_type);
        LLVMBasicBlockRef branch;
        if (payload_type == R_TYPE_ID_INVALID) {
            continue;
        }
        branch = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        LLVMAddCase(choice, r_llvm_u32(emitter, index), branch);
        LLVMPositionBuilderAtEnd(emitter->builder, branch);
        if (!r_llvm_clone_component(emitter,
                                    payload_type,
                                    r_llvm_byte_offset(emitter, to, payload),
                                    r_llvm_byte_offset(emitter, from, payload),
                                    span,
                                    failed,
                                    failure)) {
            return false;
        }
        (void)LLVMBuildBr(emitter->builder, done);
    }
    LLVMPositionBuilderAtEnd(emitter->builder, failed);
    (void)LLVMBuildRet(emitter->builder,
                       LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), failure, ""));
    LLVMPositionBuilderAtEnd(emitter->builder, done);
    (void)LLVMBuildStore(emitter->builder, tag, to);
    (void)LLVMBuildRet(emitter->builder, r_llvm_u32(emitter, 0U));
    return true;
}

/* A clone hook (R-OWN-0020): `T clone(const T*)`, or one that throws std.alloc::alloc_error. */
static bool r_llvm_clone_hook(
    RLlvmEmitter *emitter, RTypeId type, RSymbolId hook_id, LLVMValueRef to, LLVMValueRef from) {
    const RSemanticSymbol *hook = &emitter->frontend->semantic_symbols[(size_t)hook_id - 1U];
    LLVMValueRef arguments[2];

    if (emitter->functions[hook_id] == NULL) {
        return r_llvm_unsupported(emitter, "a clone hook that is not lowered");
    }
    if (hook->effect_carrier_type == R_TYPE_ID_INVALID) {
        /* The result in memory is the destination itself. */
        arguments[0] = to;
        arguments[1] = from;
        (void)LLVMBuildCall2(emitter->builder,
                             emitter->function_types[hook_id],
                             emitter->functions[hook_id],
                             arguments,
                             2U,
                             "");
        (void)LLVMBuildRet(emitter->builder, r_llvm_u32(emitter, 0U));
        return true;
    }
    {
        uint32_t size = 0U;
        uint32_t align = 0U;
        uint32_t payload = 0U;
        LLVMValueRef carrier;
        LLVMBasicBlockRef failed;
        LLVMBasicBlockRef succeeded;
        if (!r_llvm_layout(emitter, hook->effect_carrier_type, &size, &align) ||
            !r_llvm_payload_offset(emitter, hook->effect_carrier_type, &payload)) {
            return false;
        }
        carrier = r_llvm_entry_alloca(emitter, size, align, "carrier");
        arguments[0] = carrier;
        arguments[1] = from;
        (void)LLVMBuildCall2(emitter->builder,
                             emitter->function_types[hook_id],
                             emitter->functions[hook_id],
                             arguments,
                             2U,
                             "");
        failed = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        succeeded = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        (void)LLVMBuildCondBr(
            emitter->builder,
            LLVMBuildICmp(emitter->builder,
                          LLVMIntNE,
                          LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), carrier, ""),
                          r_llvm_u32(emitter, 0U),
                          ""),
            failed,
            succeeded);
        LLVMPositionBuilderAtEnd(emitter->builder, failed);
        (void)LLVMBuildRet(
            emitter->builder,
            LLVMBuildAdd(emitter->builder,
                         LLVMBuildLoad2(emitter->builder,
                                        r_llvm_int(emitter, 32U),
                                        r_llvm_byte_offset(emitter, carrier, payload),
                                        ""),
                         r_llvm_u32(emitter, 1U),
                         ""));
        LLVMPositionBuilderAtEnd(emitter->builder, succeeded);
        if (!r_llvm_call_move(emitter, type, to, r_llvm_byte_offset(emitter, carrier, payload))) {
            return false;
        }
        (void)LLVMBuildRet(emitter->builder, r_llvm_u32(emitter, 0U));
        return true;
    }
}

static bool r_llvm_clone_body(RLlvmEmitter *emitter, RTypeId type_id) {
    const RSemanticType *type = r_llvm_type(emitter, type_id);
    LLVMValueRef to = LLVMGetParam(emitter->function, 0U);
    LLVMValueRef from = LLVMGetParam(emitter->function, 1U);
    LLVMValueRef span = LLVMGetParam(emitter->function, 2U);

    if (type == NULL) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    switch (type->kind) {
    case R_SEMANTIC_TYPE_ARRAY:
        return r_llvm_clone_array(emitter, type->base, to, from, span);
    case R_SEMANTIC_TYPE_LIST:
        return r_llvm_clone_list(emitter, type->base, to, from, span);
    case R_SEMANTIC_TYPE_DICT:
        return r_llvm_clone_dict(emitter, type->base, type->second, to, from, span);
    case R_SEMANTIC_TYPE_OWN:
        return r_llvm_clone_own(emitter, type->base, to, from, span);
    case R_SEMANTIC_TYPE_OPTION:
        return r_llvm_clone_tagged(emitter, type_id, to, from, span);
    case R_SEMANTIC_TYPE_FIXED_ARRAY: {
        /* The elements in order; a failure destroys those built, newest first. */
        LLVMValueRef failure = r_llvm_entry_alloca(emitter, 4U, 4U, "failure");
        LLVMBasicBlockRef unwind[64];
        uint32_t size = 0U;
        uint32_t align = 0U;
        uint64_t element;
        if (type->length > 64U) {
            return r_llvm_unsupported(emitter, "a clone of a fixed array of this length");
        }
        if (!r_llvm_layout(emitter, type->base, &size, &align)) {
            return false;
        }
        for (element = 0U; element < type->length; ++element) {
            unwind[element] =
                LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
            if (!r_llvm_clone_component(emitter,
                                        type->base,
                                        r_llvm_byte_offset(emitter, to, element * size),
                                        r_llvm_byte_offset(emitter, from, element * size),
                                        span,
                                        unwind[element],
                                        failure)) {
                return false;
            }
        }
        (void)LLVMBuildRet(emitter->builder, r_llvm_u32(emitter, 0U));
        for (element = 0U; element < type->length; ++element) {
            uint64_t earlier = element;
            LLVMPositionBuilderAtEnd(emitter->builder, unwind[element]);
            while (earlier != 0U) {
                earlier -= 1U;
                if (!r_llvm_call_drop(
                        emitter, type->base, r_llvm_byte_offset(emitter, to, earlier * size))) {
                    return false;
                }
            }
            (void)LLVMBuildRet(
                emitter->builder,
                LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), failure, ""));
        }
        return true;
    }
    case R_SEMANTIC_TYPE_ARC:
    case R_SEMANTIC_TYPE_RC:
    case R_SEMANTIC_TYPE_WEAK: {
        const bool rc = (type->kind == R_SEMANTIC_TYPE_RC) ||
                        ((type->kind == R_SEMANTIC_TYPE_WEAK) &&
                         ((type->flags & R_SEMANTIC_TYPE_FLAG_RC_OWNER) != 0U));
        const bool weak = type->kind == R_SEMANTIC_TYPE_WEAK;
        const char *name = weak ? (rc ? "r_runtime_weak_rc_clone" : "r_runtime_weak_arc_clone")
                                : (rc ? "r_runtime_rc_clone" : "r_runtime_arc_clone");
        LLVMValueRef arguments[2];
        LLVMValueRef status;
        int64_t overflow = 0;
        LLVMBasicBlockRef overflowed;
        LLVMBasicBlockRef counted;
        arguments[0] = from;
        arguments[1] = to;
        status = r_llvm_call_runtime(emitter, name, arguments, 2U, NULL);
        if ((status == NULL) || !r_llvm_runtime_constant(emitter,
                                                         "R_RUNTIME_PANIC_REFERENCE_COUNT_OVERFLOW",
                                                         &overflow)) {
            return false;
        }
        overflowed = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        counted = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        (void)LLVMBuildCondBr(
            emitter->builder,
            LLVMBuildICmp(
                emitter->builder, LLVMIntNE, status, LLVMConstInt(LLVMTypeOf(status), 0U, 0), ""),
            overflowed,
            counted);
        LLVMPositionBuilderAtEnd(emitter->builder, overflowed);
        arguments[0] = r_llvm_u32(emitter, (uint64_t)overflow);
        arguments[1] = span;
        if (r_llvm_call_runtime(emitter, "r_runtime_panic", arguments, 2U, NULL) == NULL) {
            return false;
        }
        (void)LLVMBuildUnreachable(emitter->builder);
        LLVMPositionBuilderAtEnd(emitter->builder, counted);
        (void)LLVMBuildRet(emitter->builder, r_llvm_u32(emitter, 0U));
        return true;
    }
    case R_SEMANTIC_TYPE_STANDARD:
        if (r_llvm_standard_named(emitter, type_id, "std.fs::path")) {
            LLVMValueRef copy = r_llvm_std_structure(emitter, "RStdFsPathAllocResult");
            LLVMBasicBlockRef failed;
            LLVMBasicBlockRef copied;
            LLVMValueRef ok;
            if ((copy == NULL) ||
                (r_llvm_call_runtime(emitter, "r_std_fs_path_clone", &from, 1U, copy) == NULL)) {
                return false;
            }
            ok = r_llvm_std_status_is(
                emitter, copy, "RStdFsPathAllocResult", "status", "R_STD_FS_CALL_SUCCESS");
            if (ok == NULL) {
                return false;
            }
            failed = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
            copied = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
            (void)LLVMBuildCondBr(emitter->builder, ok, copied, failed);
            LLVMPositionBuilderAtEnd(emitter->builder, failed);
            (void)LLVMBuildRet(
                emitter->builder,
                LLVMBuildAdd(emitter->builder,
                             LLVMBuildLoad2(
                                 emitter->builder,
                                 r_llvm_int(emitter, 32U),
                                 r_llvm_std_member(emitter, copy, "RStdFsPathAllocResult", "error"),
                                 ""),
                             r_llvm_u32(emitter, 1U),
                             ""));
            LLVMPositionBuilderAtEnd(emitter->builder, copied);
            if (!r_llvm_std_copy(
                    emitter,
                    type_id,
                    to,
                    r_llvm_std_member(emitter, copy, "RStdFsPathAllocResult", "value"))) {
                return false;
            }
            (void)LLVMBuildRet(emitter->builder, r_llvm_u32(emitter, 0U));
            return true;
        }
        if (r_llvm_standard_named(emitter, type_id, "std.string::string")) {
            /* RRuntimeString {RRuntimeArray bytes}: the bytes are valid UTF-8 already. */
            LLVMValueRef bytes = r_llvm_clone_member(emitter, from, "RRuntimeString", "bytes");
            LLVMValueRef arguments[4];
            LLVMValueRef status;
            LLVMBasicBlockRef refused;
            LLVMBasicBlockRef copied;
            if (bytes == NULL) {
                return false;
            }
            arguments[0] = to;
            arguments[1] = r_llvm_std_allocator(emitter);
            arguments[2] =
                LLVMBuildLoad2(emitter->builder,
                               r_llvm_pointer(emitter),
                               r_llvm_clone_member(emitter, bytes, "RRuntimeArray", "data"),
                               "");
            arguments[3] =
                LLVMBuildLoad2(emitter->builder,
                               r_llvm_int(emitter, 64U),
                               r_llvm_clone_member(emitter, bytes, "RRuntimeArray", "length"),
                               "");
            status = arguments[1] == NULL
                         ? NULL
                         : r_llvm_call_runtime(
                               emitter, "r_runtime_string_from_valid_utf8", arguments, 4U, NULL);
            if (status == NULL) {
                return false;
            }
            refused = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
            copied = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
            (void)LLVMBuildCondBr(emitter->builder,
                                  LLVMBuildICmp(emitter->builder,
                                                LLVMIntEQ,
                                                status,
                                                LLVMConstInt(LLVMTypeOf(status), 0U, 0),
                                                ""),
                                  copied,
                                  refused);
            LLVMPositionBuilderAtEnd(emitter->builder, refused);
            {
                LLVMValueRef error = r_llvm_clone_failure(emitter, status, span);
                if (error == NULL) {
                    return false;
                }
                (void)LLVMBuildRet(emitter->builder, error);
            }
            LLVMPositionBuilderAtEnd(emitter->builder, copied);
            (void)LLVMBuildRet(emitter->builder, r_llvm_u32(emitter, 0U));
            return true;
        }
        return r_llvm_unsupported(emitter, "a clone of this standard type");
    case R_SEMANTIC_TYPE_STRUCT:
    case R_SEMANTIC_TYPE_ENUM: {
        const RSemanticAggregate *aggregate = r_llvm_aggregate(emitter, type_id);
        if (aggregate == NULL) {
            return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
        if (aggregate->clone_function != R_SYMBOL_ID_INVALID) {
            return r_llvm_clone_hook(emitter, type_id, aggregate->clone_function, to, from);
        }
        return type->kind == R_SEMANTIC_TYPE_STRUCT
                   ? r_llvm_clone_struct(emitter, aggregate, to, from, span)
                   : r_llvm_clone_tagged(emitter, type_id, to, from, span);
    }
    default:
        return r_llvm_unsupported(emitter, "a clone of this type");
    }
}

/* R-SLIB-ASYNC-0016: the adapter through which a broadcast clones its element,
   `bool (ptr to, ptr from, ptr error)`, over the clone glue (r_broadcast_clone_*). */
LLVMValueRef r_llvm_broadcast_clone(RLlvmEmitter *emitter, RTypeId type) {
    const RTypeId value = r_llvm_value_type(emitter, type);
    LLVMTypeRef parameters[3];
    char name[64];

    if ((value == R_TYPE_ID_INVALID) || ((size_t)value > emitter->frontend->semantic_type_count)) {
        (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        return NULL;
    }
    if (emitter->broadcast_clones[value] == NULL) {
        parameters[0] = r_llvm_pointer(emitter);
        parameters[1] = r_llvm_pointer(emitter);
        parameters[2] = r_llvm_pointer(emitter);
        (void)snprintf(
            name, sizeof(name), "r_broadcast_clone.%" PRIu32, r_llvm_type_key(emitter, value));
        emitter->broadcast_clones[value] = LLVMAddFunction(
            emitter->module, name, LLVMFunctionType(r_llvm_int(emitter, 1U), parameters, 3U, 0));
        LLVMSetLinkage(emitter->broadcast_clones[value], LLVMInternalLinkage);
        if (r_llvm_clone_function(emitter, value) == NULL) {
            return NULL;
        }
    }
    return emitter->broadcast_clones[value];
}

/* The bodies of the clone functions the program declared; writing one may declare more. */
bool r_llvm_emit_clone_glue(RLlvmEmitter *emitter) {
    bool progress = true;
    size_t adapter;

    for (adapter = 1U; adapter <= emitter->frontend->semantic_type_count; ++adapter) {
        LLVMValueRef answer;
        LLVMValueRef function = emitter->broadcast_clones[adapter];
        LLVMBasicBlockRef failed;
        LLVMBasicBlockRef cloned;
        if (function == NULL) {
            continue;
        }
        emitter->function = function;
        emitter->mir = NULL;
        emitter->frame = NULL;
        LLVMPositionBuilderAtEnd(
            emitter->builder, LLVMAppendBasicBlockInContext(emitter->context, function, "entry"));
        answer = r_llvm_clone_call(emitter,
                                   (RTypeId)adapter,
                                   LLVMGetParam(function, 0U),
                                   LLVMGetParam(function, 1U),
                                   r_llvm_span(emitter, (RSourceSpan){0}));
        if (answer == NULL) {
            return false;
        }
        failed = LLVMAppendBasicBlockInContext(emitter->context, function, "");
        cloned = LLVMAppendBasicBlockInContext(emitter->context, function, "");
        (void)LLVMBuildCondBr(
            emitter->builder,
            LLVMBuildICmp(emitter->builder, LLVMIntNE, answer, r_llvm_u32(emitter, 0U), ""),
            failed,
            cloned);
        LLVMPositionBuilderAtEnd(emitter->builder, failed);
        (void)LLVMBuildStore(emitter->builder,
                             LLVMBuildSub(emitter->builder, answer, r_llvm_u32(emitter, 1U), ""),
                             LLVMGetParam(function, 2U));
        (void)LLVMBuildRet(emitter->builder, LLVMConstInt(r_llvm_int(emitter, 1U), 0U, 0));
        LLVMPositionBuilderAtEnd(emitter->builder, cloned);
        (void)LLVMBuildRet(emitter->builder, LLVMConstInt(r_llvm_int(emitter, 1U), 1U, 0));
    }

    while (progress) {
        size_t index;
        progress = false;
        for (index = 1U; index <= emitter->frontend->semantic_type_count; ++index) {
            if ((emitter->clone_glue[index] == NULL) || (emitter->clone_written[index] != 0U)) {
                continue;
            }
            emitter->clone_written[index] = 1U;
            progress = true;
            emitter->function = emitter->clone_glue[index];
            emitter->mir = NULL;
            emitter->frame = NULL;
            LLVMPositionBuilderAtEnd(
                emitter->builder,
                LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "entry"));
            if (!r_llvm_clone_body(emitter, (RTypeId)index)) {
                return false;
            }
        }
    }
    return true;
}

/* core::clone (r_c17_emit_async_core_clone): a clone that cannot fail is built in the result; a
   checked one in the ok payload of its carrier, or the carrier holds the alloc_error. */
bool r_llvm_std_clone(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RTypeId type = instruction->auxiliary_type;
    const bool checked =
        r_llvm_value_kind(emitter, instruction->type) == R_SEMANTIC_TYPE_EFFECT_CARRIER;
    LLVMValueRef source = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 0U));
    LLVMValueRef span = r_llvm_span(emitter, instruction->span);
    LLVMValueRef result;
    LLVMValueRef answer;
    uint32_t payload = 0U;

    if ((source == NULL) || (span == NULL)) {
        return false;
    }
    if (!checked) {
        if (r_llvm_clone_is_copy(emitter, type)) {
            /* A generic T: clone instance with a Copy T: the clone is a read. */
            if (r_llvm_scalar_type(emitter, type) != NULL) {
                return r_llvm_set_value(
                    emitter, instruction->result, r_llvm_load_scalar(emitter, type, source));
            }
            result = r_llvm_std_result(emitter, instruction);
            if ((result == NULL) || !r_llvm_std_copy(emitter, type, result, source)) {
                return false;
            }
            r_llvm_std_initialized(emitter, instruction);
            return true;
        }
        result = r_llvm_std_result(emitter, instruction);
        if ((result == NULL) || (r_llvm_clone_call(emitter, type, result, source, span) == NULL)) {
            return false;
        }
        r_llvm_std_initialized(emitter, instruction);
        return true;
    }
    result = r_llvm_std_result(emitter, instruction);
    if ((result == NULL) || !r_llvm_payload_offset(emitter, instruction->type, &payload)) {
        return false;
    }
    if (r_llvm_clone_is_copy(emitter, type)) {
        if (!r_llvm_std_copy(emitter, type, r_llvm_byte_offset(emitter, result, payload), source)) {
            return false;
        }
        r_llvm_std_initialized(emitter, instruction);
        return true;
    }
    answer = r_llvm_clone_call(
        emitter, type, r_llvm_byte_offset(emitter, result, payload), source, span);
    if (answer == NULL) {
        return false;
    }
    {
        /* tag 1 with the error on failure; the payload then holds the alloc_error. */
        LLVMValueRef failed =
            LLVMBuildICmp(emitter->builder, LLVMIntNE, answer, r_llvm_u32(emitter, 0U), "");
        LLVMBasicBlockRef failure =
            LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        LLVMBasicBlockRef done =
            LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        (void)LLVMBuildCondBr(emitter->builder, failed, failure, done);
        LLVMPositionBuilderAtEnd(emitter->builder, failure);
        (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 1U), result);
        (void)LLVMBuildStore(emitter->builder,
                             LLVMBuildSub(emitter->builder, answer, r_llvm_u32(emitter, 1U), ""),
                             r_llvm_byte_offset(emitter, result, payload));
        (void)LLVMBuildBr(emitter->builder, done);
        LLVMPositionBuilderAtEnd(emitter->builder, done);
    }
    r_llvm_std_initialized(emitter, instruction);
    return true;
}

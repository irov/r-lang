#include "standard_internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* Standard operations of the LLVM emitter (R-LIB-0018 and the Library specification). Each one
   is the call of its library or runtime function with the C17 emitter's argument and result
   conventions (r_c17_emit_async_*): a library result structure lands in a temporary, its status
   selects the tag of the R carrier, and the members move into the carrier's payload. The inline
   fast paths of the C17 container helpers are left to inlining of the library (B7); the
   operations call the library entry they stand for. */

/* ---- Operands, temporaries and library structures ---- */

RMirValueId r_llvm_std_operand(const RLlvmEmitter *emitter,
                               const RMirInstruction *instruction,
                               uint32_t index) {
    size_t position;

    if (index >= instruction->operand_count) {
        return R_MIR_VALUE_ID_INVALID;
    }
    position = (size_t)instruction->first_operand + (size_t)index;
    return position < emitter->frontend->mir_operand_count
               ? emitter->frontend->mir_operands[position]
               : R_MIR_VALUE_ID_INVALID;
}

RTypeId r_llvm_std_operand_type(const RLlvmEmitter *emitter, RMirValueId value) {
    const RMirInstruction *definition = r_llvm_definition(emitter, value);

    return definition == NULL ? R_TYPE_ID_INVALID : definition->type;
}

/* Zeroed memory of a runtime or library structure named by its typedef. */
LLVMValueRef r_llvm_std_structure(RLlvmEmitter *emitter, const char *type_name) {
    uint32_t size = 0U;
    uint32_t align = 0U;
    LLVMValueRef memory;

    if (!r_llvm_runtime_layout(emitter, type_name, &size, &align)) {
        return NULL;
    }
    memory = r_llvm_entry_alloca(emitter, size, align, "native");
    (void)LLVMBuildMemSet(emitter->builder,
                          memory,
                          LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                          r_llvm_u64(emitter, size),
                          align);
    return memory;
}

/* The address of a member of a structure in memory. */
LLVMValueRef r_llvm_std_member(RLlvmEmitter *emitter,
                               LLVMValueRef memory,
                               const char *type_name,
                               const char *field_name) {
    uint32_t offset = 0U;

    if ((memory == NULL) || !r_llvm_runtime_field(emitter, type_name, field_name, &offset, NULL)) {
        return NULL;
    }
    return r_llvm_byte_offset(emitter, memory, offset);
}

/* Whether a 32-bit status member holds the enumerator. */
LLVMValueRef r_llvm_std_status_is(RLlvmEmitter *emitter,
                                  LLVMValueRef memory,
                                  const char *type_name,
                                  const char *field_name,
                                  const char *enumerator) {
    LLVMValueRef member = r_llvm_std_member(emitter, memory, type_name, field_name);
    int64_t value = 0;

    if ((member == NULL) || !r_llvm_runtime_constant(emitter, enumerator, &value)) {
        return NULL;
    }
    return LLVMBuildICmp(emitter->builder,
                         LLVMIntEQ,
                         LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), member, ""),
                         r_llvm_u32(emitter, (uint64_t)value),
                         "");
}

/* Copies a value of the type between memories, bitwise (the library already moved it). */
bool r_llvm_std_copy(RLlvmEmitter *emitter,
                     RTypeId type,
                     LLVMValueRef destination,
                     LLVMValueRef source) {
    uint32_t size = 0U;
    uint32_t align = 0U;

    if ((destination == NULL) || (source == NULL) || !r_llvm_layout(emitter, type, &size, &align)) {
        return false;
    }
    if (size != 0U) {
        (void)LLVMBuildMemCpy(
            emitter->builder, destination, align, source, align, r_llvm_u64(emitter, size));
    }
    return true;
}

/* A value staged in memory of its own for a library call that may move it (`T staged = value`):
   a value in memory is copied bitwise, a scalar stored. */
LLVMValueRef r_llvm_std_stage(RLlvmEmitter *emitter, RMirValueId value, RTypeId type) {
    LLVMValueRef source = r_llvm_value(emitter, value);
    uint32_t size = 0U;
    uint32_t align = 0U;
    LLVMValueRef staged;

    if ((source == NULL) || !r_llvm_layout(emitter, type, &size, &align)) {
        return NULL;
    }
    staged = r_llvm_entry_alloca(emitter, size, align, "staged");
    if (r_llvm_scalar_type(emitter, type) != NULL) {
        r_llvm_store_scalar(emitter, type, source, staged);
    } else if (size != 0U) {
        (void)LLVMBuildMemCpy(
            emitter->builder, staged, align, source, align, r_llvm_u64(emitter, size));
    }
    return staged;
}

/* An integer operand as a C size_t. */
LLVMValueRef r_llvm_std_size(RLlvmEmitter *emitter, RMirValueId value) {
    const RTypeId type = r_llvm_std_operand_type(emitter, value);
    LLVMValueRef scalar = r_llvm_value(emitter, value);
    unsigned bits = 0U;
    bool is_signed = false;

    if ((scalar == NULL) ||
        !r_llvm_kind_integer(r_llvm_value_kind(emitter, type), &bits, &is_signed)) {
        (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        return NULL;
    }
    return LLVMBuildIntCast2(emitter->builder, scalar, r_llvm_int(emitter, 64U), is_signed, "");
}

LLVMValueRef r_llvm_std_allocator(RLlvmEmitter *emitter) {
    return r_llvm_call_runtime(emitter, "r_runtime_hosted_allocator", NULL, 0U, NULL);
}

/* The memory of the instruction's result, which is initialized once the operation completes. A
   scalar result without drop is an SSA value, never memory: an operation that produces one sets
   it from a temporary (B6-1). */
LLVMValueRef r_llvm_std_result(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    uint32_t size = 0U;
    uint32_t align = 0U;
    LLVMValueRef memory;

    if ((instruction->result == R_MIR_VALUE_ID_INVALID) ||
        ((r_llvm_scalar_type(emitter, instruction->type) != NULL) &&
         !r_llvm_type_requires_drop(emitter, instruction->type)) ||
        !r_llvm_layout(emitter, instruction->type, &size, &align)) {
        (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        return NULL;
    }
    memory = r_llvm_value_memory(emitter, instruction->result, instruction->type);
    if ((memory != NULL) && (size != 0U)) {
        (void)LLVMBuildMemSet(emitter->builder,
                              memory,
                              LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                              r_llvm_u64(emitter, size),
                              align);
    }
    return memory;
}

void r_llvm_std_initialized(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, instruction->result), true);
}

/* The single error type of a carrier with one effect (r_c17_single_effect_type). */
RTypeId r_llvm_std_single_effect(const RLlvmEmitter *emitter, RTypeId carrier) {
    const RSemanticType *type = r_llvm_type(emitter, r_llvm_value_type(emitter, carrier));

    if ((type == NULL) || (type->kind != R_SEMANTIC_TYPE_EFFECT_CARRIER) ||
        (type->flags != R_SEMANTIC_TYPE_FLAG_NONE) || (type->length != 0U) ||
        (r_semantic_effect_count(emitter->frontend, type->second) != 1U)) {
        return R_TYPE_ID_INVALID;
    }
    return r_semantic_effect_at(emitter->frontend, type->second, 0U);
}

/* A carrier result by the status of a library structure: tag 0 with the success member (none for
   void), tag 1 with the error member. `status` is the success condition. */
bool r_llvm_std_carrier(RLlvmEmitter *emitter,
                        const RMirInstruction *instruction,
                        LLVMValueRef status,
                        LLVMValueRef success_member,
                        LLVMValueRef error_member) {
    const RSemanticType *carrier =
        r_llvm_type(emitter, r_llvm_value_type(emitter, instruction->type));
    const RTypeId error_type = r_llvm_std_single_effect(emitter, instruction->type);
    LLVMValueRef result = r_llvm_std_result(emitter, instruction);
    LLVMBasicBlockRef success;
    LLVMBasicBlockRef failure;
    LLVMBasicBlockRef next;
    uint32_t payload = 0U;

    if ((status == NULL) || (result == NULL) || (carrier == NULL) ||
        (error_type == R_TYPE_ID_INVALID) ||
        !r_llvm_payload_offset(emitter, instruction->type, &payload)) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    success = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    failure = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    next = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    (void)LLVMBuildCondBr(emitter->builder, status, success, failure);
    LLVMPositionBuilderAtEnd(emitter->builder, success);
    (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 0U), result);
    if ((success_member != NULL) &&
        !r_llvm_std_copy(
            emitter, carrier->base, r_llvm_byte_offset(emitter, result, payload), success_member)) {
        return false;
    }
    (void)LLVMBuildBr(emitter->builder, next);
    LLVMPositionBuilderAtEnd(emitter->builder, failure);
    (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 1U), result);
    if ((error_member != NULL) &&
        !r_llvm_std_copy(
            emitter, error_type, r_llvm_byte_offset(emitter, result, payload), error_member)) {
        return false;
    }
    (void)LLVMBuildBr(emitter->builder, next);
    LLVMPositionBuilderAtEnd(emitter->builder, next);
    r_llvm_std_initialized(emitter, instruction);
    return true;
}

/* ---- core ---- */

/* core::panic (R-LIB-0013): under unwind the message is the text of the report; otherwise the
   runtime reports the category and the process ends. */
static bool r_llvm_std_panic(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RMirValueId message = r_llvm_std_operand(emitter, instruction, 0U);
    LLVMValueRef text = r_llvm_value(emitter, message);
    LLVMValueRef arguments[4];
    int64_t category = 0;

    if ((instruction->operand_count != 1U) || (text == NULL)) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    if (instruction->panic_target == R_MIR_BLOCK_ID_INVALID) {
        return r_llvm_panic(
            emitter, "R_RUNTIME_PANIC_EXPLICIT", instruction->span, R_MIR_BLOCK_ID_INVALID);
    }
    if (!r_llvm_runtime_constant(emitter, "R_RUNTIME_PANIC_EXPLICIT", &category)) {
        return false;
    }
    arguments[0] = r_llvm_u32(emitter, (uint64_t)category);
    arguments[1] = r_llvm_span(emitter, instruction->span);
    arguments[2] = LLVMBuildLoad2(emitter->builder, r_llvm_pointer(emitter), text, "");
    arguments[3] = LLVMBuildLoad2(
        emitter->builder, r_llvm_int(emitter, 64U), r_llvm_byte_offset(emitter, text, 8U), "");
    if ((arguments[1] == NULL) ||
        (r_llvm_call_runtime(emitter, "r_runtime_raise_text", arguments, 4U, NULL) == NULL) ||
        /* As r_llvm_panic: a step parks the panic in its task. */
        ((emitter->frame != NULL)
             ? (r_llvm_call_runtime(
                    emitter, "r_runtime_task_panic_park", &emitter->execution, 1U, NULL) == NULL)
             : (r_llvm_call_runtime(emitter, "r_runtime_unwind_cleanup_enter", NULL, 0U, NULL) ==
                NULL))) {
        return false;
    }
    if ((size_t)instruction->panic_target > emitter->mir->block_count) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    (void)LLVMBuildBr(emitter->builder, emitter->blocks[instruction->panic_target - 1U]);
    return true;
}

/* An intrinsic overloaded on one type. */
LLVMValueRef r_llvm_std_intrinsic(RLlvmEmitter *emitter,
                                  const char *name,
                                  LLVMTypeRef type,
                                  LLVMValueRef *arguments,
                                  unsigned count) {
    const unsigned id = LLVMLookupIntrinsicID(name, strlen(name));

    return LLVMBuildCall2(emitter->builder,
                          LLVMIntrinsicGetType(emitter->context, id, &type, 1U),
                          LLVMGetIntrinsicDeclaration(emitter->module, id, &type, 1U),
                          arguments,
                          count,
                          "");
}

/* The address of part `index` of a two-part tuple result in memory. */
static LLVMValueRef r_llvm_std_tuple_part(RLlvmEmitter *emitter,
                                          LLVMValueRef tuple,
                                          RTypeId tuple_type,
                                          uint32_t index,
                                          RTypeId *part_type) {
    const RSemanticAggregate *aggregate = r_llvm_aggregate(emitter, tuple_type);
    const RSemanticField *field;
    uint32_t offset = 0U;

    if ((aggregate == NULL) || !aggregate->is_tuple || (index >= aggregate->field_count)) {
        (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        return NULL;
    }
    field = r_llvm_field(emitter, aggregate->first_field + index + 1U);
    if ((field == NULL) ||
        !r_llvm_field_offset(emitter, aggregate->first_field + index + 1U, &offset)) {
        (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        return NULL;
    }
    *part_type = field->type;
    return r_llvm_byte_offset(emitter, tuple, offset);
}

/* Library R-LIB-0027 (L45), as the helpers of core_bits.inc: counts are u32 and count the bits of
   the width (all of them for zero); swap_bytes reverses the bytes; a rotation takes its count
   modulo the width; the wide operations are of unsigned types and return two parts. A narrowing
   division panics with division_by_zero for a zero divisor and with integer_overflow when the
   high half is not below the divisor. */
static bool r_llvm_std_core_bits(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RStandardCallOperation operation = instruction->standard_operation;
    LLVMTypeRef integer = r_llvm_scalar_type(emitter, instruction->auxiliary_type);
    unsigned bits = 0U;
    bool is_signed = false;
    LLVMValueRef operands[3] = {NULL, NULL, NULL};
    LLVMValueRef first = NULL;
    LLVMValueRef second = NULL;
    uint32_t index;

    if ((integer == NULL) ||
        !r_llvm_kind_integer(
            r_llvm_value_kind(emitter, instruction->auxiliary_type), &bits, &is_signed) ||
        (instruction->operand_count > 3U) || (instruction->result == R_MIR_VALUE_ID_INVALID)) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    for (index = 0U; index < instruction->operand_count; ++index) {
        operands[index] = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, index));
        if (operands[index] == NULL) {
            return false;
        }
    }
    switch (operation) {
    case R_STANDARD_CALL_CORE_LEADING_ZEROS:
    case R_STANDARD_CALL_CORE_TRAILING_ZEROS:
    case R_STANDARD_CALL_CORE_COUNT_ONES: {
        LLVMValueRef arguments[2];
        arguments[0] = operands[0];
        arguments[1] = LLVMConstInt(r_llvm_int(emitter, 1U), 0U, 0);
        first =
            r_llvm_std_intrinsic(emitter,
                                 operation == R_STANDARD_CALL_CORE_LEADING_ZEROS    ? "llvm.ctlz"
                                 : operation == R_STANDARD_CALL_CORE_TRAILING_ZEROS ? "llvm.cttz"
                                                                                    : "llvm.ctpop",
                                 integer,
                                 arguments,
                                 operation == R_STANDARD_CALL_CORE_COUNT_ONES ? 1U : 2U);
        if (bits > 32U) {
            first = LLVMBuildTrunc(emitter->builder, first, r_llvm_int(emitter, 32U), "");
        } else if (bits < 32U) {
            first = LLVMBuildZExt(emitter->builder, first, r_llvm_int(emitter, 32U), "");
        }
        break;
    }
    case R_STANDARD_CALL_CORE_SWAP_BYTES:
        first = bits == 8U ? operands[0]
                           : r_llvm_std_intrinsic(emitter, "llvm.bswap", integer, operands, 1U);
        break;
    case R_STANDARD_CALL_CORE_ROTATE_LEFT:
    case R_STANDARD_CALL_CORE_ROTATE_RIGHT: {
        LLVMValueRef arguments[3];
        LLVMValueRef count = LLVMBuildAnd(
            emitter->builder, operands[1], r_llvm_u32(emitter, (uint64_t)bits - 1U), "");
        arguments[0] = operands[0];
        arguments[1] = operands[0];
        arguments[2] = LLVMBuildZExtOrBitCast(
            emitter->builder,
            bits < 32U ? LLVMBuildTrunc(emitter->builder, count, integer, "") : count,
            integer,
            "");
        first = r_llvm_std_intrinsic(emitter,
                                     operation == R_STANDARD_CALL_CORE_ROTATE_LEFT ? "llvm.fshl"
                                                                                   : "llvm.fshr",
                                     integer,
                                     arguments,
                                     3U);
        break;
    }
    case R_STANDARD_CALL_CORE_WIDENING_MUL: {
        LLVMTypeRef wide = r_llvm_int(emitter, bits * 2U);
        LLVMValueRef product = LLVMBuildMul(emitter->builder,
                                            LLVMBuildZExt(emitter->builder, operands[0], wide, ""),
                                            LLVMBuildZExt(emitter->builder, operands[1], wide, ""),
                                            "");
        first = LLVMBuildTrunc(emitter->builder, product, integer, "");
        second = LLVMBuildTrunc(
            emitter->builder,
            LLVMBuildLShr(emitter->builder, product, LLVMConstInt(wide, bits, 0), ""),
            integer,
            "");
        break;
    }
    case R_STANDARD_CALL_CORE_CARRYING_ADD: {
        LLVMValueRef carry = LLVMBuildZExt(emitter->builder, operands[2], integer, "");
        LLVMValueRef sum = LLVMBuildAdd(emitter->builder, operands[0], operands[1], "");
        first = LLVMBuildAdd(emitter->builder, sum, carry, "");
        second = LLVMBuildOr(emitter->builder,
                             LLVMBuildICmp(emitter->builder, LLVMIntULT, sum, operands[0], ""),
                             LLVMBuildICmp(emitter->builder, LLVMIntULT, first, sum, ""),
                             "");
        break;
    }
    case R_STANDARD_CALL_CORE_BORROWING_SUB: {
        LLVMValueRef borrow = LLVMBuildZExt(emitter->builder, operands[2], integer, "");
        LLVMValueRef difference = LLVMBuildSub(emitter->builder, operands[0], operands[1], "");
        second =
            LLVMBuildOr(emitter->builder,
                        LLVMBuildICmp(emitter->builder, LLVMIntULT, operands[0], operands[1], ""),
                        LLVMBuildICmp(emitter->builder, LLVMIntULT, difference, borrow, ""),
                        "");
        first = LLVMBuildSub(emitter->builder, difference, borrow, "");
        break;
    }
    case R_STANDARD_CALL_CORE_NARROWING_DIV: {
        LLVMTypeRef wide = r_llvm_int(emitter, bits * 2U);
        LLVMValueRef dividend;
        LLVMValueRef divisor;
        if (!r_llvm_check(
                emitter,
                LLVMBuildICmp(
                    emitter->builder, LLVMIntEQ, operands[2], LLVMConstInt(integer, 0U, 0), ""),
                "R_RUNTIME_PANIC_DIVISION_BY_ZERO",
                instruction->span,
                instruction->panic_target) ||
            !r_llvm_check(emitter,
                          LLVMBuildICmp(emitter->builder, LLVMIntUGE, operands[0], operands[2], ""),
                          "R_RUNTIME_PANIC_INTEGER_OVERFLOW",
                          instruction->span,
                          instruction->panic_target)) {
            return false;
        }
        dividend = LLVMBuildOr(emitter->builder,
                               LLVMBuildShl(emitter->builder,
                                            LLVMBuildZExt(emitter->builder, operands[0], wide, ""),
                                            LLVMConstInt(wide, bits, 0),
                                            ""),
                               LLVMBuildZExt(emitter->builder, operands[1], wide, ""),
                               "");
        divisor = LLVMBuildZExt(emitter->builder, operands[2], wide, "");
        first = LLVMBuildTrunc(
            emitter->builder, LLVMBuildUDiv(emitter->builder, dividend, divisor, ""), integer, "");
        second = LLVMBuildTrunc(
            emitter->builder, LLVMBuildURem(emitter->builder, dividend, divisor, ""), integer, "");
        break;
    }
    default:
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    if (second == NULL) {
        return r_llvm_set_value(emitter, instruction->result, first);
    }
    {
        LLVMValueRef tuple = r_llvm_value_memory(emitter, instruction->result, instruction->type);
        RTypeId first_type = R_TYPE_ID_INVALID;
        RTypeId second_type = R_TYPE_ID_INVALID;
        LLVMValueRef first_part =
            tuple == NULL
                ? NULL
                : r_llvm_std_tuple_part(emitter, tuple, instruction->type, 0U, &first_type);
        LLVMValueRef second_part =
            tuple == NULL
                ? NULL
                : r_llvm_std_tuple_part(emitter, tuple, instruction->type, 1U, &second_type);
        if ((first_part == NULL) || (second_part == NULL)) {
            return false;
        }
        r_llvm_store_scalar(emitter, first_type, first, first_part);
        r_llvm_store_scalar(emitter, second_type, second, second_part);
    }
    return true;
}

/* std.arc::clone and std.rc::clone of the place (r_clone_arc, r_clone_rc): a count overflow
   ends the process; the clone of an owner of an interface keeps its tag (R-TYPE-0055). */
static bool r_llvm_std_shared_clone(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const bool is_arc = instruction->standard_operation == R_STANDARD_CALL_ARC_CLONE;
    /* The source is a place (`&owner`), or a borrow value as its one operand (a borrowed
       parameter passed on). */
    LLVMValueRef source = instruction->operand_count == 1U
                              ? r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 0U))
                              : r_llvm_place_address(emitter,
                                                     instruction->place_ordinal,
                                                     instruction->place_is_parameter,
                                                     instruction->operand0);
    LLVMValueRef result = r_llvm_std_result(emitter, instruction);
    LLVMValueRef arguments[2];
    LLVMValueRef status;
    int64_t overflow = 0;

    if ((source == NULL) || (result == NULL) ||
        !r_llvm_std_copy(emitter, instruction->type, result, source) ||
        !r_llvm_runtime_constant(emitter,
                                 is_arc ? "R_STD_ARC_CALL_COUNT_OVERFLOW"
                                        : "R_STD_RC_CALL_COUNT_OVERFLOW",
                                 &overflow)) {
        return false;
    }
    /* The owner is the first member of an owner of an interface. */
    arguments[0] = source;
    arguments[1] = result;
    status = r_llvm_call_runtime(
        emitter, is_arc ? "r_std_arc_clone" : "r_std_rc_clone", arguments, 2U, NULL);
    if ((status == NULL) ||
        !r_llvm_check(emitter,
                      LLVMBuildICmp(emitter->builder,
                                    LLVMIntEQ,
                                    status,
                                    LLVMConstInt(LLVMTypeOf(status), (uint64_t)overflow, 0),
                                    ""),
                      "R_RUNTIME_PANIC_REFERENCE_COUNT_OVERFLOW",
                      instruction->span,
                      R_MIR_BLOCK_ID_INVALID)) {
        return false;
    }
    r_llvm_std_initialized(emitter, instruction);
    return true;
}

/* The other operations of std.arc and std.rc (r_c17_emit_async_shared_owner_operation): those
   that make an owner report a count overflow by status, upgrade gives an option, and the rest
   return their value directly. An owner of an interface is not lowered yet. */
static bool r_llvm_std_shared_owner(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RStandardCallOperation operation = instruction->standard_operation;
    const bool is_arc = (operation >= R_STANDARD_CALL_ARC_CLONE_WEAK) &&
                        (operation <= R_STANDARD_CALL_ARC_TRY_UNWRAP);
    const RMirValueId first = r_llvm_std_operand(emitter, instruction, 0U);
    const RMirInstruction *definition = r_llvm_definition(emitter, first);
    const RSemanticType *operand =
        definition == NULL ? NULL
                           : r_llvm_type(emitter, r_llvm_value_type(emitter, definition->type));
    static const char *const arc_names[] = {"r_std_arc_clone_weak",
                                            "r_std_arc_downgrade",
                                            "r_std_arc_upgrade",
                                            "r_std_arc_get_mut",
                                            "r_std_arc_strong_count",
                                            "r_std_arc_weak_count",
                                            "r_std_arc_ptr_eq",
                                            "r_std_arc_into_raw",
                                            "r_std_arc_from_raw",
                                            "r_std_arc_try_unwrap"};
    static const char *const rc_names[] = {"r_std_rc_clone_weak",
                                           "r_std_rc_downgrade",
                                           "r_std_rc_upgrade",
                                           "r_std_rc_get_mut",
                                           "r_std_rc_strong_count",
                                           "r_std_rc_weak_count",
                                           "r_std_rc_ptr_eq",
                                           "r_std_rc_into_raw",
                                           "r_std_rc_from_raw",
                                           "r_std_rc_try_unwrap"};
    const unsigned which = (unsigned)operation - (unsigned)(is_arc ? R_STANDARD_CALL_ARC_CLONE_WEAK
                                                                   : R_STANDARD_CALL_RC_CLONE_WEAK);
    const char *name = which < 10U ? (is_arc ? arc_names[which] : rc_names[which]) : NULL;
    const char *overflow =
        is_arc ? "R_STD_ARC_CALL_COUNT_OVERFLOW" : "R_STD_RC_CALL_COUNT_OVERFLOW";
    LLVMValueRef arguments[2];
    int64_t overflow_value = 0;

    if ((name == NULL) || (operand == NULL) ||
        !r_llvm_runtime_constant(emitter, overflow, &overflow_value)) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    if ((operand->kind == R_SEMANTIC_TYPE_BORROW) &&
        (r_semantic_dyn_owned(emitter->frontend, operand->base) != R_TYPE_ID_INVALID)) {
        /* R-TYPE-0055 (L29, r_c17_emit_dyn_owner_operation): the runtime owner is the first
           field of the pair, so counts and ptr_eq take the pair as it is; an owner the
           operation makes takes the member's tag. */
        uint32_t source_tag = 0U;
        if (!r_llvm_dyn_owner_tag_offset(emitter, operand->base, &source_tag)) {
            return false;
        }
        if ((which == 0U) || (which == 1U) || (which == 2U)) {
            LLVMValueRef tag;
            arguments[0] = r_llvm_value(emitter, first);
            if (arguments[0] == NULL) {
                return false;
            }
            tag = LLVMBuildLoad2(emitter->builder,
                                 r_llvm_int(emitter, 32U),
                                 r_llvm_byte_offset(emitter, arguments[0], source_tag),
                                 "");
            if (which != 2U) {
                LLVMValueRef status;
                uint32_t result_tag = 0U;
                arguments[1] = r_llvm_std_result(emitter, instruction);
                if ((arguments[1] == NULL) ||
                    !r_llvm_dyn_owner_tag_offset(emitter, instruction->type, &result_tag)) {
                    return false;
                }
                status = r_llvm_call_runtime(emitter, name, arguments, 2U, NULL);
                if ((status == NULL) ||
                    !r_llvm_check(
                        emitter,
                        LLVMBuildICmp(emitter->builder,
                                      LLVMIntEQ,
                                      status,
                                      LLVMConstInt(LLVMTypeOf(status), (uint64_t)overflow_value, 0),
                                      ""),
                        "R_RUNTIME_PANIC_REFERENCE_COUNT_OVERFLOW",
                        instruction->span,
                        instruction->panic_target)) {
                    return false;
                }
                (void)LLVMBuildStore(
                    emitter->builder, tag, r_llvm_byte_offset(emitter, arguments[1], result_tag));
                r_llvm_std_initialized(emitter, instruction);
                return true;
            }
            {
                /* upgrade: o<owner of the interface>, some while a strong owner remains. */
                const char *type_name = is_arc ? "RStdArcUpgradeResult" : "RStdRcUpgradeResult";
                const RSemanticType *option =
                    r_llvm_type(emitter, r_llvm_value_type(emitter, instruction->type));
                LLVMValueRef native = r_llvm_std_structure(emitter, type_name);
                LLVMValueRef result = r_llvm_std_result(emitter, instruction);
                LLVMBasicBlockRef some;
                LLVMBasicBlockRef next;
                uint32_t payload = 0U;
                uint32_t result_tag = 0U;
                uint32_t owner_size = 0U;
                uint32_t owner_align = 0U;
                if ((option == NULL) || (native == NULL) || (result == NULL) ||
                    !r_llvm_payload_offset(emitter, instruction->type, &payload) ||
                    !r_llvm_dyn_owner_tag_offset(emitter, option->base, &result_tag) ||
                    !r_llvm_runtime_layout(emitter,
                                           is_arc ? "RRuntimeArc" : "RRuntimeRc",
                                           &owner_size,
                                           &owner_align) ||
                    (r_llvm_call_runtime(emitter, name, arguments, 1U, native) == NULL) ||
                    !r_llvm_check(
                        emitter,
                        r_llvm_std_status_is(emitter, native, type_name, "status", overflow),
                        "R_RUNTIME_PANIC_REFERENCE_COUNT_OVERFLOW",
                        instruction->span,
                        instruction->panic_target)) {
                    return false;
                }
                some = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
                next = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
                (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 0U), result);
                (void)LLVMBuildCondBr(
                    emitter->builder,
                    LLVMBuildICmp(
                        emitter->builder,
                        LLVMIntNE,
                        LLVMBuildLoad2(emitter->builder,
                                       r_llvm_int(emitter, 8U),
                                       r_llvm_std_member(emitter, native, type_name, "has_value"),
                                       ""),
                        LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                        ""),
                    some,
                    next);
                LLVMPositionBuilderAtEnd(emitter->builder, some);
                (void)LLVMBuildMemCpy(emitter->builder,
                                      r_llvm_byte_offset(emitter, result, payload),
                                      owner_align,
                                      r_llvm_std_member(emitter, native, type_name, "value"),
                                      owner_align,
                                      r_llvm_u64(emitter, owner_size));
                (void)LLVMBuildStore(emitter->builder,
                                     tag,
                                     r_llvm_byte_offset(emitter, result, payload + result_tag));
                (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 1U), result);
                (void)LLVMBuildBr(emitter->builder, next);
                LLVMPositionBuilderAtEnd(emitter->builder, next);
                r_llvm_std_initialized(emitter, instruction);
                return true;
            }
        }
        if ((which != 4U) && (which != 5U) && (which != 6U)) {
            return r_llvm_unsupported(emitter, "this operation on an owner of an interface");
        }
    }
    if (which == 9U) {
        /* try_unwrap consumes the owner: the value when it was the last strong owner
           (unwrapped, tag 0), else the owner itself back (shared, tag 1). */
        const char *type_name = is_arc ? "RStdArcTryUnwrapResult" : "RStdRcTryUnwrapResult";
        LLVMValueRef native = r_llvm_std_structure(emitter, type_name);
        LLVMValueRef result = r_llvm_std_result(emitter, instruction);
        LLVMValueRef kind;
        LLVMBasicBlockRef shared;
        LLVMBasicBlockRef unwrapped;
        LLVMBasicBlockRef invalid;
        LLVMBasicBlockRef done;
        LLVMValueRef choice;
        int64_t success = 0;
        int64_t value_kind = 0;
        int64_t owner_kind = 0;
        uint32_t payload = 0U;
        uint32_t owner_size = 0U;
        uint32_t owner_align = 0U;
        arguments[0] = r_llvm_value(emitter, first);
        if ((native == NULL) || (result == NULL) || (arguments[0] == NULL) ||
            !r_llvm_payload_offset(emitter, instruction->type, &payload) ||
            !r_llvm_runtime_layout(
                emitter, is_arc ? "RRuntimeArc" : "RRuntimeRc", &owner_size, &owner_align) ||
            !r_llvm_runtime_constant(
                emitter, is_arc ? "R_STD_ARC_CALL_SUCCESS" : "R_STD_RC_CALL_SUCCESS", &success) ||
            !r_llvm_runtime_constant(emitter,
                                     is_arc ? "R_STD_ARC_TRY_UNWRAP_VALUE"
                                            : "R_STD_RC_TRY_UNWRAP_VALUE",
                                     &value_kind) ||
            !r_llvm_runtime_constant(emitter,
                                     is_arc ? "R_STD_ARC_TRY_UNWRAP_OWNER"
                                            : "R_STD_RC_TRY_UNWRAP_OWNER",
                                     &owner_kind)) {
            return false;
        }
        arguments[1] = r_llvm_byte_offset(emitter, result, payload);
        if ((r_llvm_call_runtime(emitter, name, arguments, 2U, native) == NULL) ||
            !r_llvm_check(emitter,
                          r_llvm_std_status_is(emitter, native, type_name, "status", overflow),
                          "R_RUNTIME_PANIC_REFERENCE_COUNT_OVERFLOW",
                          instruction->span,
                          R_MIR_BLOCK_ID_INVALID) ||
            !r_llvm_check(emitter,
                          LLVMBuildNot(emitter->builder,
                                       r_llvm_std_status_is(emitter,
                                                            native,
                                                            type_name,
                                                            "status",
                                                            is_arc ? "R_STD_ARC_CALL_SUCCESS"
                                                                   : "R_STD_RC_CALL_SUCCESS"),
                                       ""),
                          "R_RUNTIME_PANIC_CONTRACT_VIOLATION",
                          instruction->span,
                          R_MIR_BLOCK_ID_INVALID)) {
            return false;
        }
        kind = LLVMBuildLoad2(emitter->builder,
                              r_llvm_int(emitter, 32U),
                              r_llvm_std_member(emitter, native, type_name, "kind"),
                              "");
        unwrapped = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        shared = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        invalid = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        done = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        choice = LLVMBuildSwitch(emitter->builder, kind, invalid, 2U);
        LLVMAddCase(choice, r_llvm_u32(emitter, (uint64_t)value_kind), unwrapped);
        LLVMAddCase(choice, r_llvm_u32(emitter, (uint64_t)owner_kind), shared);
        LLVMPositionBuilderAtEnd(emitter->builder, unwrapped);
        (void)LLVMBuildBr(emitter->builder, done);
        LLVMPositionBuilderAtEnd(emitter->builder, shared);
        (void)LLVMBuildMemCpy(emitter->builder,
                              r_llvm_byte_offset(emitter, result, payload),
                              owner_align,
                              r_llvm_std_member(emitter, native, type_name, "owner"),
                              owner_align,
                              r_llvm_u64(emitter, owner_size));
        (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 1U), result);
        (void)LLVMBuildBr(emitter->builder, done);
        LLVMPositionBuilderAtEnd(emitter->builder, invalid);
        if (!r_llvm_panic(emitter,
                          "R_RUNTIME_PANIC_CONTRACT_VIOLATION",
                          instruction->span,
                          R_MIR_BLOCK_ID_INVALID)) {
            return false;
        }
        LLVMPositionBuilderAtEnd(emitter->builder, done);
        r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, first), false);
        r_llvm_std_initialized(emitter, instruction);
        return true;
    }
    arguments[0] = r_llvm_value(emitter, first);
    if (arguments[0] == NULL) {
        return false;
    }
    if ((which == 0U) || (which == 1U) || (which == 8U)) {
        /* clone_weak, downgrade, from_raw: the owner is written to the result. */
        LLVMValueRef status;
        arguments[1] = r_llvm_std_result(emitter, instruction);
        if (arguments[1] == NULL) {
            return false;
        }
        status = r_llvm_call_runtime(emitter, name, arguments, 2U, NULL);
        if ((status == NULL) ||
            !r_llvm_check(
                emitter,
                LLVMBuildICmp(emitter->builder,
                              LLVMIntEQ,
                              status,
                              LLVMConstInt(LLVMTypeOf(status), (uint64_t)overflow_value, 0),
                              ""),
                "R_RUNTIME_PANIC_REFERENCE_COUNT_OVERFLOW",
                instruction->span,
                instruction->panic_target)) {
            return false;
        }
        r_llvm_std_initialized(emitter, instruction);
        return true;
    }
    if (which == 2U) {
        /* upgrade: an owner while one remains. */
        const char *type_name = is_arc ? "RStdArcUpgradeResult" : "RStdRcUpgradeResult";
        LLVMValueRef native = r_llvm_std_structure(emitter, type_name);
        return (native != NULL) &&
               (r_llvm_call_runtime(emitter, name, arguments, 1U, native) != NULL) &&
               r_llvm_check(emitter,
                            r_llvm_std_status_is(emitter, native, type_name, "status", overflow),
                            "R_RUNTIME_PANIC_REFERENCE_COUNT_OVERFLOW",
                            instruction->span,
                            instruction->panic_target) &&
               r_llvm_std_option_from_native(emitter, instruction, native, type_name);
    }
    {
        LLVMValueRef value;
        unsigned count = 1U;
        if (which == 6U) {
            arguments[1] = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 1U));
            if (arguments[1] == NULL) {
                return false;
            }
            count = 2U;
        }
        value = r_llvm_call_runtime(emitter, name, arguments, count, NULL);
        return (value != NULL) && r_llvm_set_value(emitter, instruction->result, value);
    }
}

/* core::replace and core::take: the value of the destination moves out and the replacement in. */
static bool r_llvm_std_replace(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RMirValueId replacement = r_llvm_std_operand(emitter, instruction, 1U);
    LLVMValueRef destination = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 0U));
    LLVMValueRef value = r_llvm_value(emitter, replacement);

    if ((destination == NULL) || (value == NULL)) {
        return false;
    }
    if (r_llvm_scalar_type(emitter, instruction->type) != NULL) {
        if (!r_llvm_set_value(emitter,
                              instruction->result,
                              r_llvm_load_scalar(emitter, instruction->type, destination))) {
            return false;
        }
        r_llvm_store_scalar(emitter, instruction->type, value, destination);
        return true;
    }
    {
        LLVMValueRef result = r_llvm_value_memory(emitter, instruction->result, instruction->type);
        if (result == NULL) {
            return false;
        }
        if (!r_llvm_type_requires_drop(emitter, instruction->type)) {
            return r_llvm_std_copy(emitter, instruction->type, result, destination) &&
                   r_llvm_std_copy(emitter, instruction->type, destination, value);
        }
        if (!r_llvm_call_move(emitter, instruction->type, result, destination) ||
            !r_llvm_call_move(emitter, instruction->type, destination, value)) {
            return false;
        }
        r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, replacement), false);
        r_llvm_std_initialized(emitter, instruction);
        return true;
    }
}

/* core::swap of two exclusive borrows, through a temporary. */
static bool r_llvm_std_swap(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RTypeId type = instruction->auxiliary_type;
    LLVMValueRef first = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 0U));
    LLVMValueRef second = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 1U));
    uint32_t size = 0U;
    uint32_t align = 0U;
    LLVMValueRef temporary;

    if ((first == NULL) || (second == NULL) || !r_llvm_layout(emitter, type, &size, &align)) {
        return false;
    }
    temporary = r_llvm_entry_alloca(emitter, size, align, "swap");
    if (!r_llvm_type_requires_drop(emitter, type)) {
        return r_llvm_std_copy(emitter, type, temporary, first) &&
               r_llvm_std_copy(emitter, type, first, second) &&
               r_llvm_std_copy(emitter, type, second, temporary);
    }
    return r_llvm_call_move(emitter, type, temporary, first) &&
           r_llvm_call_move(emitter, type, first, second) &&
           r_llvm_call_move(emitter, type, second, temporary);
}

/* core::assume: a false condition is undefined behaviour the optimizer may rely on. */
static bool r_llvm_std_assume(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    LLVMValueRef condition = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 0U));
    const unsigned id = LLVMLookupIntrinsicID("llvm.assume", 11U);

    if (condition == NULL) {
        return false;
    }
    (void)LLVMBuildCall2(emitter->builder,
                         LLVMIntrinsicGetType(emitter->context, id, NULL, 0U),
                         LLVMGetIntrinsicDeclaration(emitter->module, id, NULL, 0U),
                         &condition,
                         1U,
                         "");
    return true;
}

/* core::checked_*, wrapping_* and saturating_* (r_core_<operation>_<suffix> of the C17 emitter):
   a checked operation gives none on overflow, a wrapping one the result modulo the width, a
   saturating one the bound the exact result passed. */
static bool r_llvm_std_core_integer(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const unsigned index =
        (unsigned)instruction->standard_operation - (unsigned)R_STANDARD_CALL_CORE_CHECKED_ADD;
    const unsigned arithmetic = index % 3U; /* add, sub, mul */
    LLVMValueRef left = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 0U));
    LLVMValueRef right = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 1U));
    LLVMTypeRef integer = r_llvm_scalar_type(emitter, instruction->auxiliary_type);
    unsigned bits = 0U;
    bool is_signed = false;
    LLVMValueRef value;

    if ((left == NULL) || (right == NULL) || (integer == NULL) || (index > 8U) ||
        !r_llvm_kind_integer(
            r_llvm_value_kind(emitter, instruction->auxiliary_type), &bits, &is_signed)) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    if (index >= 3U && index < 6U) {
        value = arithmetic == 0U   ? LLVMBuildAdd(emitter->builder, left, right, "")
                : arithmetic == 1U ? LLVMBuildSub(emitter->builder, left, right, "")
                                   : LLVMBuildMul(emitter->builder, left, right, "");
        return r_llvm_set_value(emitter, instruction->result, value);
    }
    if ((index >= 6U) && (arithmetic != 2U)) {
        LLVMValueRef arguments[2];
        arguments[0] = left;
        arguments[1] = right;
        value =
            r_llvm_std_intrinsic(emitter,
                                 arithmetic == 0U ? (is_signed ? "llvm.sadd.sat" : "llvm.uadd.sat")
                                                  : (is_signed ? "llvm.ssub.sat" : "llvm.usub.sat"),
                                 integer,
                                 arguments,
                                 2U);
        return r_llvm_set_value(emitter, instruction->result, value);
    }
    {
        static const char *const names[2][3] = {
            {"llvm.uadd.with.overflow", "llvm.usub.with.overflow", "llvm.umul.with.overflow"},
            {"llvm.sadd.with.overflow", "llvm.ssub.with.overflow", "llvm.smul.with.overflow"}};
        LLVMValueRef arguments[2];
        LLVMValueRef pair;
        LLVMValueRef overflow;
        arguments[0] = left;
        arguments[1] = right;
        pair = r_llvm_std_intrinsic(
            emitter, names[is_signed ? 1 : 0][arithmetic], integer, arguments, 2U);
        value = LLVMBuildExtractValue(emitter->builder, pair, 0U, "");
        overflow = LLVMBuildExtractValue(emitter->builder, pair, 1U, "");
        if (index >= 6U) {
            /* saturating_mul: the sign of the exact product picks the bound. */
            LLVMValueRef bound;
            if (is_signed) {
                LLVMValueRef negative = LLVMBuildXor(
                    emitter->builder,
                    LLVMBuildICmp(
                        emitter->builder, LLVMIntSLT, left, LLVMConstInt(integer, 0U, 0), ""),
                    LLVMBuildICmp(
                        emitter->builder, LLVMIntSLT, right, LLVMConstInt(integer, 0U, 0), ""),
                    "");
                bound =
                    LLVMBuildSelect(emitter->builder,
                                    negative,
                                    LLVMConstInt(integer, (uint64_t)1U << (bits - 1U), 0),
                                    LLVMConstInt(integer, ((uint64_t)1U << (bits - 1U)) - 1U, 0),
                                    "");
            } else {
                bound = LLVMConstAllOnes(integer);
            }
            return r_llvm_set_value(emitter,
                                    instruction->result,
                                    LLVMBuildSelect(emitter->builder, overflow, bound, value, ""));
        }
        {
            /* checked: some when it did not overflow. */
            LLVMValueRef result = r_llvm_std_result(emitter, instruction);
            LLVMBasicBlockRef some;
            LLVMBasicBlockRef next;
            uint32_t payload = 0U;
            if ((result == NULL) || !r_llvm_payload_offset(emitter, instruction->type, &payload)) {
                return false;
            }
            some = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
            next = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
            (void)LLVMBuildCondBr(emitter->builder, overflow, next, some);
            LLVMPositionBuilderAtEnd(emitter->builder, some);
            (void)LLVMBuildStore(
                emitter->builder, value, r_llvm_byte_offset(emitter, result, payload));
            (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 1U), result);
            (void)LLVMBuildBr(emitter->builder, next);
            LLVMPositionBuilderAtEnd(emitter->builder, next);
            return true;
        }
    }
}

/* ---- Containers (r_c17_emit_async_array_container_call, _list_, _dict_) ---- */

/* A library status other than success ends the process as a contract violation
   (r_c17_emit_container_status_check), or one status does (r_c17_emit_container_contract_check). */
/* core::slice_from_raw_parts(_mut) (R-LIB-0030): the slice of `length` elements at the pointer;
   the anchored forms are checked by the semantic analysis and lower the same way. */
static bool r_llvm_std_slice_from_raw_parts(RLlvmEmitter *emitter,
                                            const RMirInstruction *instruction) {
    LLVMValueRef result = r_llvm_std_result(emitter, instruction);
    LLVMValueRef pointer = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 0U));
    LLVMValueRef length = r_llvm_std_size(emitter, r_llvm_std_operand(emitter, instruction, 1U));

    if ((result == NULL) || (pointer == NULL) || (length == NULL)) {
        return false;
    }
    (void)LLVMBuildStore(emitter->builder, pointer, result);
    (void)LLVMBuildStore(emitter->builder, length, r_llvm_byte_offset(emitter, result, 8U));
    r_llvm_std_initialized(emitter, instruction);
    return true;
}

/* core::volatile_load and core::volatile_store of a scalar through a raw pointer. */
static bool r_llvm_std_volatile(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RTypeId type = instruction->auxiliary_type;
    LLVMTypeRef scalar = r_llvm_scalar_type(emitter, type);
    LLVMValueRef pointer = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 0U));
    LLVMValueRef access;

    if (scalar == NULL) {
        return r_llvm_unsupported(emitter, "a volatile access to an aggregate");
    }
    if (pointer == NULL) {
        return false;
    }
    if (instruction->standard_operation == R_STANDARD_CALL_CORE_VOLATILE_LOAD) {
        access = r_llvm_load_scalar(emitter, type, pointer);
        if (access == NULL) {
            return false;
        }
        LLVMSetVolatile(LLVMIsALoadInst(access) != NULL ? access : LLVMGetOperand(access, 0U), 1);
        return r_llvm_set_value(emitter, instruction->result, access);
    }
    {
        LLVMValueRef value = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 1U));
        LLVMValueRef stored;
        if (value == NULL) {
            return false;
        }
        if (r_llvm_value_kind(emitter, type) == R_SEMANTIC_TYPE_BOOL) {
            value = LLVMBuildZExt(emitter->builder, value, r_llvm_int(emitter, 8U), "");
        }
        stored = LLVMBuildStore(emitter->builder, value, pointer);
        LLVMSetVolatile(stored, 1);
        return true;
    }
}

/* core::adopt (an allocation becomes an owner of its type) and core::release (an owner gives up
   its allocation as a raw pointer). */
static bool r_llvm_std_adopt_release(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RMirValueId argument = r_llvm_std_operand(emitter, instruction, 0U);
    LLVMValueRef arguments[3];

    if (instruction->standard_operation == R_STANDARD_CALL_CORE_ADOPT) {
        arguments[0] = r_llvm_type_info(emitter, instruction->auxiliary_type);
        arguments[1] = r_llvm_value(emitter, argument);
        arguments[2] = r_llvm_std_result(emitter, instruction);
        if ((arguments[0] == NULL) || (arguments[1] == NULL) || (arguments[2] == NULL) ||
            (r_llvm_call_runtime(emitter, "r_runtime_own_adopt", arguments, 3U, NULL) == NULL)) {
            return false;
        }
        r_llvm_std_initialized(emitter, instruction);
        return true;
    }
    arguments[0] = r_llvm_value(emitter, argument);
    arguments[1] = r_llvm_entry_alloca(emitter, 8U, 8U, "released");
    if (arguments[0] == NULL) {
        return false;
    }
    (void)LLVMBuildStore(emitter->builder, LLVMConstNull(r_llvm_pointer(emitter)), arguments[1]);
    if (r_llvm_call_runtime(emitter, "r_runtime_own_into_raw", arguments, 2U, NULL) == NULL) {
        return false;
    }
    r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, argument), false);
    return r_llvm_set_value(
        emitter,
        instruction->result,
        LLVMBuildLoad2(emitter->builder, r_llvm_pointer(emitter), arguments[1], ""));
}

/* std.async::cancel, std.async::detach and std.thread::detach take the task or handle from its
   storage, which then holds nothing to drop. */
static bool r_llvm_std_let_go(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RMirValueId operand = r_llvm_std_operand(emitter, instruction, 0U);
    LLVMValueRef address = r_llvm_value(emitter, operand);
    const char *name =
        instruction->standard_operation == R_STANDARD_CALL_ASYNC_CANCEL   ? "r_std_async_cancel"
        : instruction->standard_operation == R_STANDARD_CALL_ASYNC_DETACH ? "r_std_async_detach"
                                                                          : "r_std_thread_detach";

    if ((address == NULL) || (r_llvm_call_runtime(emitter, name, &address, 1U, NULL) == NULL)) {
        return false;
    }
    r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, operand), false);
    return true;
}

bool r_llvm_std_require_status(RLlvmEmitter *emitter,
                               const RMirInstruction *instruction,
                               LLVMValueRef native,
                               const char *type_name,
                               const char *status,
                               bool fails_when_equal) {
    LLVMValueRef equal = r_llvm_std_status_is(emitter, native, type_name, "status", status);

    if (equal == NULL) {
        return false;
    }
    return r_llvm_check(emitter,
                        fails_when_equal ? equal : LLVMBuildNot(emitter->builder, equal, ""),
                        "R_RUNTIME_PANIC_CONTRACT_VIOLATION",
                        instruction->span,
                        R_MIR_BLOCK_ID_INVALID);
}

/* An option result from a library pointer option (r_c17_emit_async_option_from_native): some
   with the pointer when has_value is set. */
bool r_llvm_std_option_from_native(RLlvmEmitter *emitter,
                                   const RMirInstruction *instruction,
                                   LLVMValueRef native,
                                   const char *type_name) {
    const RSemanticType *option =
        r_llvm_type(emitter, r_llvm_value_type(emitter, instruction->type));
    LLVMValueRef result = r_llvm_std_result(emitter, instruction);
    LLVMValueRef has_value = r_llvm_std_member(emitter, native, type_name, "has_value");
    LLVMValueRef value = r_llvm_std_member(emitter, native, type_name, "value");
    LLVMBasicBlockRef some;
    LLVMBasicBlockRef next;
    uint32_t payload = 0U;

    if ((option == NULL) || (result == NULL) || (has_value == NULL) || (value == NULL) ||
        !r_llvm_payload_offset(emitter, instruction->type, &payload)) {
        return false;
    }
    some = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    next = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    (void)LLVMBuildCondBr(
        emitter->builder,
        LLVMBuildICmp(emitter->builder,
                      LLVMIntNE,
                      LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 8U), has_value, ""),
                      LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                      ""),
        some,
        next);
    LLVMPositionBuilderAtEnd(emitter->builder, some);
    /* The payload is a borrow, or a pair of borrows for an entry of std.dict. */
    if (!r_llvm_std_copy(
            emitter, option->base, r_llvm_byte_offset(emitter, result, payload), value)) {
        return false;
    }
    (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 1U), result);
    (void)LLVMBuildBr(emitter->builder, next);
    LLVMPositionBuilderAtEnd(emitter->builder, next);
    r_llvm_std_initialized(emitter, instruction);
    return true;
}

/* A value option the library moved into the payload of the result: some when has_value. */
bool r_llvm_std_value_option(RLlvmEmitter *emitter,
                             const RMirInstruction *instruction,
                             const char *function_name,
                             const char *type_name,
                             const char *success,
                             LLVMValueRef *arguments,
                             unsigned argument_count) {
    LLVMValueRef native = r_llvm_std_structure(emitter, type_name);
    LLVMValueRef result = r_llvm_std_result(emitter, instruction);
    LLVMBasicBlockRef some;
    LLVMBasicBlockRef next;
    uint32_t payload = 0U;

    if ((native == NULL) || (result == NULL) ||
        !r_llvm_payload_offset(emitter, instruction->type, &payload)) {
        return false;
    }
    arguments[argument_count - 1U] = r_llvm_byte_offset(emitter, result, payload);
    if ((r_llvm_call_runtime(emitter, function_name, arguments, argument_count, native) == NULL) ||
        !r_llvm_std_require_status(emitter, instruction, native, type_name, success, false)) {
        return false;
    }
    some = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    next = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    (void)LLVMBuildCondBr(
        emitter->builder,
        LLVMBuildICmp(emitter->builder,
                      LLVMIntNE,
                      LLVMBuildLoad2(emitter->builder,
                                     r_llvm_int(emitter, 8U),
                                     r_llvm_std_member(emitter, native, type_name, "has_value"),
                                     ""),
                      LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                      ""),
        some,
        next);
    LLVMPositionBuilderAtEnd(emitter->builder, some);
    (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 1U), result);
    (void)LLVMBuildBr(emitter->builder, next);
    LLVMPositionBuilderAtEnd(emitter->builder, next);
    r_llvm_std_initialized(emitter, instruction);
    return true;
}

/* A pointer option of a library call checked for success. */
bool r_llvm_std_pointer_option(RLlvmEmitter *emitter,
                               const RMirInstruction *instruction,
                               const char *function_name,
                               const char *type_name,
                               const char *success,
                               LLVMValueRef *arguments,
                               unsigned argument_count) {
    LLVMValueRef native = r_llvm_std_structure(emitter, type_name);

    return (native != NULL) &&
           (r_llvm_call_runtime(emitter, function_name, arguments, argument_count, native) !=
            NULL) &&
           r_llvm_std_require_status(emitter, instruction, native, type_name, success, false) &&
           r_llvm_std_option_from_native(emitter, instruction, native, type_name);
}

/* A carrier of an allocation error (r_c17_emit_async_alloc_error_carrier): tag 0 with the
   success member when the carrier has a value, tag 1 with the error. */
bool r_llvm_std_alloc_carrier(RLlvmEmitter *emitter,
                              const RMirInstruction *instruction,
                              LLVMValueRef native,
                              const char *type_name,
                              const char *success,
                              const char *success_member) {
    const RSemanticType *carrier =
        r_llvm_type(emitter, r_llvm_value_type(emitter, instruction->type));

    if (carrier == NULL) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    return r_llvm_std_carrier(emitter,
                              instruction,
                              r_llvm_std_status_is(emitter, native, type_name, "status", success),
                              r_llvm_type_is_void(emitter, carrier->base)
                                  ? NULL
                                  : r_llvm_std_member(emitter, native, type_name, success_member),
                              r_llvm_std_member(emitter, native, type_name, "error"));
}

/* B7 (P4.2 of the C17 emitter): a push into spare capacity moves the staged value into the next
   element and counts it, as r_runtime_array_push does once its reservation holds; only a full
   array calls the library, which grows it or reports the allocation failure. The element moves
   by the glue its type information names, or by a copy (r_runtime_array_move). */
static bool r_llvm_std_array_push_now(RLlvmEmitter *emitter,
                                      LLVMValueRef target,
                                      LLVMValueRef staged,
                                      RTypeId element,
                                      LLVMValueRef result,
                                      LLVMBasicBlockRef next) {
    uint32_t data = 0U;
    uint32_t length_offset = 0U;
    uint32_t capacity = 0U;
    uint32_t size = 0U;
    uint32_t align = 0U;
    LLVMValueRef length;
    LLVMValueRef slot;
    LLVMBasicBlockRef now;
    LLVMBasicBlockRef full;

    if (!r_llvm_runtime_field(emitter, "RRuntimeArray", "data", &data, NULL) ||
        !r_llvm_runtime_field(emitter, "RRuntimeArray", "length", &length_offset, NULL) ||
        !r_llvm_runtime_field(emitter, "RRuntimeArray", "capacity", &capacity, NULL) ||
        !r_llvm_layout(emitter, element, &size, &align)) {
        return false;
    }
    length = LLVMBuildLoad2(emitter->builder,
                            r_llvm_int(emitter, 64U),
                            r_llvm_byte_offset(emitter, target, length_offset),
                            "");
    now = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    full = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    (void)LLVMBuildCondBr(
        emitter->builder,
        LLVMBuildICmp(emitter->builder,
                      LLVMIntULT,
                      length,
                      LLVMBuildLoad2(emitter->builder,
                                     r_llvm_int(emitter, 64U),
                                     r_llvm_byte_offset(emitter, target, capacity),
                                     ""),
                      ""),
        now,
        full);
    LLVMPositionBuilderAtEnd(emitter->builder, now);
    slot = r_llvm_element_offset(emitter,
                                 LLVMBuildLoad2(emitter->builder,
                                                r_llvm_pointer(emitter),
                                                r_llvm_byte_offset(emitter, target, data),
                                                ""),
                                 length,
                                 size);
    if (r_llvm_type_requires_drop(emitter, element)) {
        if (!r_llvm_call_move(emitter, element, slot, staged)) {
            return false;
        }
    } else if (size != 0U) {
        (void)LLVMBuildMemCpy(emitter->builder, slot, 1U, staged, 1U, r_llvm_u64(emitter, size));
    }
    (void)LLVMBuildStore(emitter->builder,
                         LLVMBuildNUWAdd(emitter->builder, length, r_llvm_u64(emitter, 1U), ""),
                         r_llvm_byte_offset(emitter, target, length_offset));
    (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 0U), result);
    (void)LLVMBuildBr(emitter->builder, next);
    LLVMPositionBuilderAtEnd(emitter->builder, full);
    return true;
}

/* An insertion that stages the value and returns it in push_error<T> on an allocation failure
   (std.array::push and the std.list insertions). */
bool r_llvm_std_staged_insert(RLlvmEmitter *emitter,
                              const RMirInstruction *instruction,
                              const char *function_name,
                              const char *type_name,
                              const char *success,
                              const char *contract,
                              LLVMValueRef *arguments,
                              unsigned argument_count,
                              RMirValueId value) {
    const RSemanticType *carrier =
        r_llvm_type(emitter, r_llvm_value_type(emitter, instruction->type));
    const RTypeId error_type = r_llvm_std_single_effect(emitter, instruction->type);
    LLVMValueRef native = r_llvm_std_structure(emitter, type_name);
    RLlvmRecoveringMembers members;
    LLVMValueRef staged;
    LLVMValueRef result;
    LLVMBasicBlockRef inserted;
    LLVMBasicBlockRef failure;
    LLVMBasicBlockRef next;
    uint32_t payload = 0U;
    uint32_t error_payload = 0U;
    uint32_t reason_size = 0U;
    uint32_t reason_align = 0U;

    if ((native == NULL) || (carrier == NULL) || (error_type == R_TYPE_ID_INVALID) ||
        !r_llvm_recovering_members(emitter, error_type, &members) ||
        !r_llvm_payload_offset(emitter, instruction->type, &payload) ||
        !r_llvm_payload_offset(emitter, error_type, &error_payload) ||
        !r_llvm_runtime_layout(emitter, "RStdAllocError", &reason_size, &reason_align)) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    staged = r_llvm_std_stage(emitter, value, members.value_type);
    if (staged == NULL) {
        return false;
    }
    arguments[argument_count - 1U] = staged;
    result = r_llvm_std_result(emitter, instruction);
    if (result == NULL) {
        return false;
    }
    next = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    if ((strcmp(function_name, "r_std_array_push") == 0) &&
        !r_llvm_std_array_push_now(
            emitter, arguments[0], staged, members.value_type, result, next)) {
        return false;
    }
    if ((r_llvm_call_runtime(emitter, function_name, arguments, argument_count, native) == NULL) ||
        ((contract != NULL) &&
         !r_llvm_std_require_status(emitter, instruction, native, type_name, contract, true))) {
        return false;
    }
    inserted = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    failure = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    (void)LLVMBuildCondBr(emitter->builder,
                          r_llvm_std_status_is(emitter, native, type_name, "status", success),
                          inserted,
                          failure);
    LLVMPositionBuilderAtEnd(emitter->builder, inserted);
    (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 0U), result);
    if (!r_llvm_type_is_void(emitter, carrier->base) &&
        !r_llvm_std_copy(emitter,
                         carrier->base,
                         r_llvm_byte_offset(emitter, result, payload),
                         r_llvm_std_member(emitter, native, type_name, "value"))) {
        return false;
    }
    (void)LLVMBuildBr(emitter->builder, next);
    LLVMPositionBuilderAtEnd(emitter->builder, failure);
    {
        /* push_error<T>::allocation_failed {reason, value}. */
        LLVMValueRef error = r_llvm_byte_offset(emitter, result, payload);
        LLVMValueRef fields = r_llvm_byte_offset(emitter, error, error_payload);
        (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 1U), result);
        (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 0U), error);
        (void)LLVMBuildMemCpy(emitter->builder,
                              fields,
                              reason_align,
                              r_llvm_std_member(emitter, native, type_name, "reason"),
                              reason_align,
                              r_llvm_u64(emitter, reason_size));
        if (!r_llvm_std_copy(emitter,
                             members.value_type,
                             r_llvm_byte_offset(emitter, fields, members.value_offset),
                             staged)) {
            return false;
        }
    }
    (void)LLVMBuildBr(emitter->builder, next);
    LLVMPositionBuilderAtEnd(emitter->builder, next);
    /* The value moved into the container or into the error. */
    r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, value), false);
    r_llvm_std_initialized(emitter, instruction);
    return true;
}

/* ---- Dispatch ---- */

bool r_llvm_emit_standard_call(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    bool lowered;

    if (instruction->task_scope != R_SYMBOL_ID_INVALID) {
        /* A library task started in a group is a member of it (r_c17_emit_scoped_start). */
        return r_llvm_emit_async_start(emitter, instruction);
    }
    switch (instruction->standard_operation) {
    case R_STANDARD_CALL_CORE_PANIC:
        return r_llvm_std_panic(emitter, instruction);
    case R_STANDARD_CALL_C_LINK_AVAILABLE:
        return r_llvm_ffi_link_available(emitter, instruction);
    case R_STANDARD_CALL_C_STRING_FROM_STR:
    case R_STANDARD_CALL_C_STRING_AS_SLICE:
    case R_STANDARD_CALL_C_STRING_AS_PTR:
    case R_STANDARD_CALL_C_VALIDATE_UTF8:
    case R_STANDARD_CALL_C_COPY_UTF8:
    case R_STANDARD_CALL_C_ATTACH_THREAD:
    case R_STANDARD_CALL_C_DETACH_THREAD:
    case R_STANDARD_CALL_C_ADOPT_HANDLE:
    case R_STANDARD_CALL_C_HANDLE_POINTER:
    case R_STANDARD_CALL_C_RELEASE_HANDLE:
        return r_llvm_ffi_std_c(emitter, instruction);
    case R_STANDARD_CALL_CORE_LEADING_ZEROS:
    case R_STANDARD_CALL_CORE_TRAILING_ZEROS:
    case R_STANDARD_CALL_CORE_COUNT_ONES:
    case R_STANDARD_CALL_CORE_SWAP_BYTES:
    case R_STANDARD_CALL_CORE_ROTATE_LEFT:
    case R_STANDARD_CALL_CORE_ROTATE_RIGHT:
    case R_STANDARD_CALL_CORE_WIDENING_MUL:
    case R_STANDARD_CALL_CORE_CARRYING_ADD:
    case R_STANDARD_CALL_CORE_BORROWING_SUB:
    case R_STANDARD_CALL_CORE_NARROWING_DIV:
        return r_llvm_std_core_bits(emitter, instruction);
    case R_STANDARD_CALL_CORE_ATOMIC_LOAD:
    case R_STANDARD_CALL_CORE_ATOMIC_STORE:
    case R_STANDARD_CALL_CORE_ATOMIC_EXCHANGE:
    case R_STANDARD_CALL_CORE_ATOMIC_COMPARE_EXCHANGE:
    case R_STANDARD_CALL_CORE_ATOMIC_FETCH_ADD:
    case R_STANDARD_CALL_CORE_ATOMIC_FETCH_SUB:
    case R_STANDARD_CALL_CORE_ATOMIC_FETCH_AND:
    case R_STANDARD_CALL_CORE_ATOMIC_FETCH_OR:
    case R_STANDARD_CALL_CORE_ATOMIC_FETCH_XOR:
    case R_STANDARD_CALL_CORE_ATOMIC_IS_LOCK_FREE:
        return r_llvm_std_atomic(emitter, instruction);
    case R_STANDARD_CALL_CORE_CHECKED_ADD:
    case R_STANDARD_CALL_CORE_CHECKED_SUB:
    case R_STANDARD_CALL_CORE_CHECKED_MUL:
    case R_STANDARD_CALL_CORE_WRAPPING_ADD:
    case R_STANDARD_CALL_CORE_WRAPPING_SUB:
    case R_STANDARD_CALL_CORE_WRAPPING_MUL:
    case R_STANDARD_CALL_CORE_SATURATING_ADD:
    case R_STANDARD_CALL_CORE_SATURATING_SUB:
    case R_STANDARD_CALL_CORE_SATURATING_MUL:
        return r_llvm_std_core_integer(emitter, instruction);
    case R_STANDARD_CALL_CORE_REPLACE:
    case R_STANDARD_CALL_CORE_TAKE:
        return r_llvm_std_replace(emitter, instruction);
    case R_STANDARD_CALL_CORE_SWAP:
        return r_llvm_std_swap(emitter, instruction);
    case R_STANDARD_CALL_CORE_ASSUME:
        return r_llvm_std_assume(emitter, instruction);
    case R_STANDARD_CALL_CORE_SLICE_FROM_RAW_PARTS:
    case R_STANDARD_CALL_CORE_SLICE_FROM_RAW_PARTS_MUT:
        return r_llvm_std_slice_from_raw_parts(emitter, instruction);
    case R_STANDARD_CALL_CORE_VOLATILE_LOAD:
    case R_STANDARD_CALL_CORE_VOLATILE_STORE:
        return r_llvm_std_volatile(emitter, instruction);
    case R_STANDARD_CALL_CORE_ADOPT:
    case R_STANDARD_CALL_CORE_RELEASE:
        return r_llvm_std_adopt_release(emitter, instruction);
    case R_STANDARD_CALL_ASYNC_CANCEL:
    case R_STANDARD_CALL_ASYNC_DETACH:
    case R_STANDARD_CALL_THREAD_DETACH:
        return r_llvm_std_let_go(emitter, instruction);
    case R_STANDARD_CALL_THREAD_SPAWN:
    case R_STANDARD_CALL_THREAD_SPAWN_SCOPED:
    case R_STANDARD_CALL_ASYNC_BLOCKING:
        return r_llvm_thread_spawn(emitter, instruction);
    case R_STANDARD_CALL_THREAD_JOIN:
        return r_llvm_thread_join(emitter, instruction);
    case R_STANDARD_CALL_CORE_FORMAT_RENDER:
    case R_STANDARD_CALL_CORE_FORMAT_APPEND:
        return r_llvm_std_core_format(emitter, instruction);
    case R_STANDARD_CALL_CORE_CLONE:
        return r_llvm_std_clone(emitter, instruction);
    case R_STANDARD_CALL_ARC_CLONE:
    case R_STANDARD_CALL_RC_CLONE:
        return r_llvm_std_shared_clone(emitter, instruction);
    case R_STANDARD_CALL_ARC_CLONE_WEAK:
    case R_STANDARD_CALL_ARC_DOWNGRADE:
    case R_STANDARD_CALL_ARC_UPGRADE:
    case R_STANDARD_CALL_ARC_GET_MUT:
    case R_STANDARD_CALL_ARC_STRONG_COUNT:
    case R_STANDARD_CALL_ARC_WEAK_COUNT:
    case R_STANDARD_CALL_ARC_PTR_EQ:
    case R_STANDARD_CALL_ARC_INTO_RAW:
    case R_STANDARD_CALL_ARC_FROM_RAW:
    case R_STANDARD_CALL_ARC_TRY_UNWRAP:
    case R_STANDARD_CALL_RC_CLONE_WEAK:
    case R_STANDARD_CALL_RC_DOWNGRADE:
    case R_STANDARD_CALL_RC_UPGRADE:
    case R_STANDARD_CALL_RC_GET_MUT:
    case R_STANDARD_CALL_RC_STRONG_COUNT:
    case R_STANDARD_CALL_RC_WEAK_COUNT:
    case R_STANDARD_CALL_RC_PTR_EQ:
    case R_STANDARD_CALL_RC_INTO_RAW:
    case R_STANDARD_CALL_RC_FROM_RAW:
    case R_STANDARD_CALL_RC_TRY_UNWRAP:
        return r_llvm_std_shared_owner(emitter, instruction);
    case R_STANDARD_CALL_UTF8_IS_VALID:
    case R_STANDARD_CALL_UTF8_VALIDATE:
    case R_STANDARD_CALL_CORE_HASH:
    case R_STANDARD_CALL_CORE_KEY_EQUAL:
    case R_STANDARD_CALL_BYTES_EQUAL:
    case R_STANDARD_CALL_BYTES_COMPARE:
    case R_STANDARD_CALL_BYTES_WITH_CAPACITY:
    case R_STANDARD_CALL_BYTES_APPEND:
    case R_STANDARD_CALL_BYTES_APPEND_U8:
    case R_STANDARD_CALL_BYTES_APPEND_U16_LE:
    case R_STANDARD_CALL_BYTES_APPEND_U32_LE:
    case R_STANDARD_CALL_BYTES_APPEND_U64_LE:
    case R_STANDARD_CALL_HASH_CRC32:
    case R_STANDARD_CALL_HASH_MD5:
    case R_STANDARD_CALL_HASH_SHA1:
    case R_STANDARD_CALL_HASH_SHA256:
    case R_STANDARD_CALL_HASH_SHA512:
        lowered = r_llvm_std_text(emitter, instruction);
        break;
    case R_STANDARD_CALL_ARRAY_CREATE:
    case R_STANDARD_CALL_ARRAY_WITH_CAPACITY:
    case R_STANDARD_CALL_ARRAY_FILLED:
    case R_STANDARD_CALL_ARRAY_PUSH:
    case R_STANDARD_CALL_ARRAY_CAPACITY:
    case R_STANDARD_CALL_ARRAY_RESERVE:
    case R_STANDARD_CALL_ARRAY_POP:
    case R_STANDARD_CALL_ARRAY_REMOVE:
    case R_STANDARD_CALL_ARRAY_GET:
    case R_STANDARD_CALL_ARRAY_GET_MUT:
    case R_STANDARD_CALL_ARRAY_CLEAR:
    case R_STANDARD_CALL_LIST_CREATE:
    case R_STANDARD_CALL_LIST_PUSH_FRONT:
    case R_STANDARD_CALL_LIST_PUSH_BACK:
    case R_STANDARD_CALL_LIST_INSERT_BEFORE:
    case R_STANDARD_CALL_LIST_INSERT_AFTER:
    case R_STANDARD_CALL_LIST_FRONT:
    case R_STANDARD_CALL_LIST_BACK:
    case R_STANDARD_CALL_LIST_FRONT_MUT:
    case R_STANDARD_CALL_LIST_BACK_MUT:
    case R_STANDARD_CALL_LIST_GET:
    case R_STANDARD_CALL_LIST_GET_MUT:
    case R_STANDARD_CALL_LIST_REMOVE:
    case R_STANDARD_CALL_LIST_POP_FRONT:
    case R_STANDARD_CALL_LIST_POP_BACK:
    case R_STANDARD_CALL_LIST_CLEAR:
    case R_STANDARD_CALL_LIST_ITER:
    case R_STANDARD_CALL_LIST_NEXT:
    case R_STANDARD_CALL_DICT_ITER:
    case R_STANDARD_CALL_DICT_NEXT:
    case R_STANDARD_CALL_DICT_CREATE:
    case R_STANDARD_CALL_DICT_WITH_CAPACITY:
    case R_STANDARD_CALL_DICT_RESERVE:
    case R_STANDARD_CALL_DICT_INSERT:
    case R_STANDARD_CALL_DICT_CONTAINS:
    case R_STANDARD_CALL_DICT_GET:
    case R_STANDARD_CALL_DICT_GET_MUT:
    case R_STANDARD_CALL_DICT_REMOVE:
    case R_STANDARD_CALL_DICT_CLEAR:
        lowered = r_llvm_std_container(emitter, instruction);
        break;
    default: {
        char number[24];
        int length;
        if (((instruction->standard_operation >= R_STANDARD_CALL_CORE_ENUM_NAME) &&
             (instruction->standard_operation <= R_STANDARD_CALL_CORE_VARIANT_NAME)) ||
            ((instruction->standard_operation >= R_STANDARD_CALL_CORE_TYPE_ATTRIBUTE) &&
             (instruction->standard_operation <= R_STANDARD_CALL_CORE_FIELD_NAME_AT))) {
            lowered = r_llvm_std_reflection(emitter, instruction);
            break;
        }
        if (instruction->standard_operation == R_STANDARD_CALL_SYNC_RECEIVE) {
            lowered = r_llvm_std_receive(emitter, instruction);
            break;
        }
        if ((instruction->standard_operation >= R_STANDARD_CALL_ASYNC_MUTEX_NEW) &&
            (instruction->standard_operation <= R_STANDARD_CALL_SYNC_SEND_PERMIT) &&
            !r_llvm_std_library_handles(instruction->standard_operation) &&
            (instruction->standard_operation != R_STANDARD_CALL_ASYNC_TASK_ID)) {
            lowered = r_llvm_std_async_sync(emitter, instruction);
            break;
        }
        if ((instruction->standard_operation >= R_STANDARD_CALL_SYNC_ONCE_NEW) &&
            (instruction->standard_operation <= R_STANDARD_CALL_SYNC_WAIT)) {
            lowered = r_llvm_std_sync(emitter, instruction);
            break;
        }
        if (r_llvm_std_library_handles(instruction->standard_operation)) {
            lowered = r_llvm_std_library(emitter, instruction);
            break;
        }
        if ((instruction->standard_operation >= R_STANDARD_CALL_STRING_CREATE) &&
            (instruction->standard_operation <= R_STANDARD_CALL_STRING_TRUNCATE)) {
            lowered = r_llvm_std_text(emitter, instruction);
            break;
        }
        length = snprintf(number, sizeof(number), "%u", (unsigned)instruction->standard_operation);
        return r_llvm_unsupported_detail(
            emitter, "the standard operation", number, length < 0 ? 0U : (size_t)length);
    }
    }
    /* An operation that may run R code (glue of the elements) tests for its panic (L39). */
    return lowered &&
           (!instruction->runs_code || r_llvm_unwind_test(emitter, instruction->panic_target));
}

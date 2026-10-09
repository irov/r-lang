#include "standard_internal.h"

#include <string.h>

/* Text operations of the LLVM emitter: std.string, std.utf8 and the key functions of core, as the
   C17 emitter's string, utf8 and core key paths. The inline view helpers of the C17 emitter
   (r_c17_emit_string_view_fast, the append fast path) are fast paths of the library entries these
   calls go to. */

/* A library result with SUCCESS, ERROR and contract statuses (the C17 string and math pattern):
   success gives tag 0 with `value` when the carrier has one, an error tag 1 with `error`, and any
   other status ends the process as a contract violation. */
static bool r_llvm_std_three_way(RLlvmEmitter *emitter,
                                 const RMirInstruction *instruction,
                                 LLVMValueRef native,
                                 const char *type_name,
                                 const char *success,
                                 const char *error) {
    const RSemanticType *carrier =
        r_llvm_type(emitter, r_llvm_value_type(emitter, instruction->type));
    LLVMValueRef is_error = r_llvm_std_status_is(emitter, native, type_name, "status", error);
    LLVMValueRef is_success = r_llvm_std_status_is(emitter, native, type_name, "status", success);

    if ((carrier == NULL) || (is_error == NULL) || (is_success == NULL)) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    if (!r_llvm_check(emitter,
                      LLVMBuildNot(emitter->builder,
                                   LLVMBuildOr(emitter->builder, is_success, is_error, ""),
                                   ""),
                      "R_RUNTIME_PANIC_CONTRACT_VIOLATION",
                      instruction->span,
                      R_MIR_BLOCK_ID_INVALID)) {
        return false;
    }
    return r_llvm_std_carrier(emitter,
                              instruction,
                              is_success,
                              r_llvm_type_is_void(emitter, carrier->base)
                                  ? NULL
                                  : r_llvm_std_member(emitter, native, type_name, "value"),
                              r_llvm_std_member(emitter, native, type_name, "error"));
}

/* The memory of a `str` or `const u8[]` operand, which is laid out as RStdStringView. */
static LLVMValueRef
r_llvm_std_view(RLlvmEmitter *emitter, const RMirInstruction *instruction, uint32_t index) {
    return r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, index));
}

/* Member `name` of the RRuntimeArray of an owned string at `string`. */
static LLVMValueRef
r_llvm_std_string_bytes(RLlvmEmitter *emitter, LLVMValueRef string, const char *name) {
    uint32_t bytes = 0U;
    uint32_t member = 0U;

    if (!r_llvm_runtime_field(emitter, "RRuntimeString", "bytes", &bytes, NULL) ||
        !r_llvm_runtime_field(emitter, "RRuntimeArray", name, &member, NULL)) {
        return NULL;
    }
    return r_llvm_byte_offset(emitter, string, (uint64_t)bytes + member);
}

static bool r_llvm_std_string(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RStandardCallOperation operation = instruction->standard_operation;
    const RMirValueId first = r_llvm_std_operand(emitter, instruction, 0U);
    LLVMValueRef arguments[2];

    switch (operation) {
    case R_STANDARD_CALL_STRING_CREATE: {
        LLVMValueRef native = r_llvm_std_structure(emitter, "RStdStringCreateResult");
        LLVMValueRef result;
        arguments[0] = r_llvm_std_allocator(emitter);
        if ((native == NULL) || (arguments[0] == NULL) ||
            (r_llvm_call_runtime(emitter, "r_std_string_create", arguments, 1U, native) == NULL) ||
            !r_llvm_std_require_status(emitter,
                                       instruction,
                                       native,
                                       "RStdStringCreateResult",
                                       "R_STD_STRING_CALL_SUCCESS",
                                       false)) {
            return false;
        }
        result = r_llvm_std_result(emitter, instruction);
        if ((result == NULL) ||
            !r_llvm_std_copy(
                emitter,
                instruction->type,
                result,
                r_llvm_std_member(emitter, native, "RStdStringCreateResult", "value"))) {
            return false;
        }
        r_llvm_std_initialized(emitter, instruction);
        return true;
    }
    case R_STANDARD_CALL_STRING_AS_STR:
    case R_STANDARD_CALL_STRING_AS_BYTES: {
        /* `str` and `const u8[]` are both {data, length}, as RStdStringView. */
        LLVMValueRef result = r_llvm_std_result(emitter, instruction);
        arguments[0] = r_llvm_value(emitter, first);
        return (result != NULL) && (arguments[0] != NULL) &&
               (r_llvm_call_runtime(emitter, "r_std_string_as_str", arguments, 1U, result) != NULL);
    }
    case R_STANDARD_CALL_STRING_FROM_BYTES: {
        LLVMValueRef native = r_llvm_std_structure(emitter, "RStdStringFromBytesCallResult");
        LLVMValueRef result;
        arguments[0] = r_llvm_value(emitter, first);
        if ((native == NULL) || (arguments[0] == NULL) ||
            (r_llvm_call_runtime(emitter, "r_std_string_from_bytes", arguments, 1U, native) ==
             NULL) ||
            !r_llvm_std_require_status(emitter,
                                       instruction,
                                       native,
                                       "RStdStringFromBytesCallResult",
                                       "R_STD_STRING_CALL_SUCCESS",
                                       false)) {
            return false;
        }
        result = r_llvm_std_result(emitter, instruction);
        if ((result == NULL) ||
            !r_llvm_std_copy(
                emitter,
                instruction->type,
                result,
                r_llvm_std_member(emitter, native, "RStdStringFromBytesCallResult", "outcome"))) {
            return false;
        }
        /* The bytes moved into the outcome. */
        r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, first), false);
        r_llvm_std_initialized(emitter, instruction);
        return true;
    }
    case R_STANDARD_CALL_STRING_INTO_BYTES: {
        LLVMValueRef result = r_llvm_std_result(emitter, instruction);
        LLVMValueRef status;
        int64_t success = 0;
        arguments[0] = r_llvm_value(emitter, first);
        arguments[1] = result;
        if ((result == NULL) || (arguments[0] == NULL) ||
            !r_llvm_runtime_constant(emitter, "R_STD_STRING_CALL_SUCCESS", &success)) {
            return false;
        }
        status = r_llvm_call_runtime(emitter, "r_std_string_into_bytes", arguments, 2U, NULL);
        if ((status == NULL) ||
            !r_llvm_check(emitter,
                          LLVMBuildICmp(emitter->builder,
                                        LLVMIntNE,
                                        status,
                                        LLVMConstInt(LLVMTypeOf(status), (uint64_t)success, 0),
                                        ""),
                          "R_RUNTIME_PANIC_CONTRACT_VIOLATION",
                          instruction->span,
                          R_MIR_BLOCK_ID_INVALID)) {
            return false;
        }
        r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, first), false);
        r_llvm_std_initialized(emitter, instruction);
        return true;
    }
    case R_STANDARD_CALL_STRING_LEN:
    case R_STANDARD_CALL_STRING_CAPACITY: {
        LLVMValueRef string = r_llvm_value(emitter, first);
        LLVMValueRef member =
            string == NULL ? NULL
                           : r_llvm_std_string_bytes(
                                 emitter,
                                 string,
                                 operation == R_STANDARD_CALL_STRING_LEN ? "length" : "capacity");
        return (member != NULL) &&
               r_llvm_set_value(
                   emitter,
                   instruction->result,
                   LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 64U), member, ""));
    }
    case R_STANDARD_CALL_STRING_CLEAR:
        arguments[0] = r_llvm_value(emitter, first);
        return (arguments[0] != NULL) &&
               (r_llvm_call_runtime(emitter, "r_std_string_clear", arguments, 1U, NULL) != NULL);
    default:
        break;
    }
    {
        /* from_str, with_capacity, from_utf8, reserve, append_str, append_utf8, push_scalar and
           truncate. */
        const char *native_type =
            (operation == R_STANDARD_CALL_STRING_FROM_STR) ||
                    (operation == R_STANDARD_CALL_STRING_WITH_CAPACITY)
                ? "RStdStringAllocValueResult"
            : operation == R_STANDARD_CALL_STRING_FROM_UTF8   ? "RStdStringUtf8ValueResult"
            : operation == R_STANDARD_CALL_STRING_APPEND_UTF8 ? "RStdStringErrorResult"
            : operation == R_STANDARD_CALL_STRING_TRUNCATE    ? "RStdStringBoundaryResult"
                                                              : "RStdStringAllocResult";
        const char *name =
            operation == R_STANDARD_CALL_STRING_FROM_STR        ? "r_std_string_from_str"
            : operation == R_STANDARD_CALL_STRING_WITH_CAPACITY ? "r_std_string_with_capacity"
            : operation == R_STANDARD_CALL_STRING_FROM_UTF8     ? "r_std_string_from_utf8"
            : operation == R_STANDARD_CALL_STRING_RESERVE       ? "r_std_string_reserve"
            : operation == R_STANDARD_CALL_STRING_APPEND_STR    ? "r_std_string_append_str"
            : operation == R_STANDARD_CALL_STRING_APPEND_UTF8   ? "r_std_string_append_utf8"
            : operation == R_STANDARD_CALL_STRING_PUSH_SCALAR   ? "r_std_string_push_scalar"
                                                                : "r_std_string_truncate";
        LLVMValueRef native = r_llvm_std_structure(emitter, native_type);
        if (native == NULL) {
            return false;
        }
        if ((operation == R_STANDARD_CALL_STRING_FROM_STR) ||
            (operation == R_STANDARD_CALL_STRING_FROM_UTF8)) {
            arguments[0] = r_llvm_std_allocator(emitter);
            arguments[1] = r_llvm_std_view(emitter, instruction, 0U);
        } else if (operation == R_STANDARD_CALL_STRING_WITH_CAPACITY) {
            arguments[0] = r_llvm_std_allocator(emitter);
            arguments[1] = r_llvm_std_size(emitter, first);
        } else {
            arguments[0] = r_llvm_value(emitter, first);
            arguments[1] =
                (operation == R_STANDARD_CALL_STRING_APPEND_STR) ||
                        (operation == R_STANDARD_CALL_STRING_APPEND_UTF8)
                    ? r_llvm_std_view(emitter, instruction, 1U)
                : operation == R_STANDARD_CALL_STRING_PUSH_SCALAR
                    ? r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 1U))
                    : r_llvm_std_size(emitter, r_llvm_std_operand(emitter, instruction, 1U));
        }
        return (arguments[0] != NULL) && (arguments[1] != NULL) &&
               (r_llvm_call_runtime(emitter, name, arguments, 2U, native) != NULL) &&
               r_llvm_std_three_way(emitter,
                                    instruction,
                                    native,
                                    native_type,
                                    "R_STD_STRING_CALL_SUCCESS",
                                    "R_STD_STRING_CALL_ERROR");
    }
}

/* std.utf8::is_valid and validate: the view of valid text, or the index of the first invalid
   byte. */
static bool r_llvm_std_utf8(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    LLVMValueRef source = r_llvm_std_view(emitter, instruction, 0U);

    if (source == NULL) {
        return false;
    }
    if (instruction->standard_operation == R_STANDARD_CALL_UTF8_IS_VALID) {
        LLVMValueRef valid = r_llvm_call_runtime(emitter, "r_std_utf8_is_valid", &source, 1U, NULL);
        return (valid != NULL) && r_llvm_set_value(emitter, instruction->result, valid);
    }
    {
        LLVMValueRef native = r_llvm_std_structure(emitter, "RCoreValidateUtf8Result");
        LLVMValueRef ok;
        if ((native == NULL) ||
            (r_llvm_call_runtime(emitter, "r_std_utf8_validate", &source, 1U, native) == NULL)) {
            return false;
        }
        ok = LLVMBuildICmp(
            emitter->builder,
            LLVMIntNE,
            LLVMBuildLoad2(emitter->builder,
                           r_llvm_int(emitter, 8U),
                           r_llvm_std_member(emitter, native, "RCoreValidateUtf8Result", "is_ok"),
                           ""),
            LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
            "");
        return r_llvm_std_carrier(
            emitter,
            instruction,
            ok,
            r_llvm_std_member(emitter, native, "RCoreValidateUtf8Result", "value"),
            r_llvm_std_member(emitter, native, "RCoreValidateUtf8Result", "error"));
    }
}

/* core::hash and core::key_equal: the key functions of the type on the borrowed operands. */
static bool r_llvm_std_core_key(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const bool hash = instruction->standard_operation == R_STANDARD_CALL_CORE_HASH;
    LLVMValueRef function = r_llvm_key_function(emitter, instruction->runtime_type, hash);
    LLVMValueRef arguments[2];
    uint32_t index;

    if ((function == NULL) || (instruction->operand_count != (hash ? 1U : 2U))) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    for (index = 0U; index < instruction->operand_count; ++index) {
        arguments[index] = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, index));
        if (arguments[index] == NULL) {
            return false;
        }
    }
    return r_llvm_set_value(emitter,
                            instruction->result,
                            LLVMBuildCall2(emitter->builder,
                                           LLVMGlobalGetValueType(function),
                                           function,
                                           arguments,
                                           instruction->operand_count,
                                           ""));
}

/* std.bytes: comparisons of two views, an array of a capacity and the appends, whose result
   carries the allocation error (r_c17_emit_async_bytes_alloc_result). */
static bool r_llvm_std_bytes(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RStandardCallOperation operation = instruction->standard_operation;
    LLVMValueRef arguments[2];
    const char *name;
    LLVMValueRef native;

    if ((operation == R_STANDARD_CALL_BYTES_EQUAL) ||
        (operation == R_STANDARD_CALL_BYTES_COMPARE)) {
        LLVMValueRef result;
        arguments[0] = r_llvm_std_view(emitter, instruction, 0U);
        arguments[1] = r_llvm_std_view(emitter, instruction, 1U);
        if ((arguments[0] == NULL) || (arguments[1] == NULL)) {
            return false;
        }
        result = r_llvm_call_runtime(
            emitter,
            operation == R_STANDARD_CALL_BYTES_EQUAL ? "r_std_bytes_equal" : "r_std_bytes_compare",
            arguments,
            2U,
            NULL);
        return (result != NULL) && r_llvm_set_value(emitter, instruction->result, result);
    }
    switch (operation) {
    case R_STANDARD_CALL_BYTES_WITH_CAPACITY:
        name = "r_std_bytes_with_capacity";
        arguments[0] = r_llvm_std_allocator(emitter);
        arguments[1] = r_llvm_std_size(emitter, r_llvm_std_operand(emitter, instruction, 0U));
        break;
    case R_STANDARD_CALL_BYTES_APPEND:
        name = "r_std_bytes_append";
        arguments[0] = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 0U));
        arguments[1] = r_llvm_std_view(emitter, instruction, 1U);
        break;
    case R_STANDARD_CALL_BYTES_APPEND_U8:
    case R_STANDARD_CALL_BYTES_APPEND_U16_LE:
    case R_STANDARD_CALL_BYTES_APPEND_U32_LE:
    case R_STANDARD_CALL_BYTES_APPEND_U64_LE:
        name = operation == R_STANDARD_CALL_BYTES_APPEND_U8       ? "r_std_bytes_append_u8"
               : operation == R_STANDARD_CALL_BYTES_APPEND_U16_LE ? "r_std_bytes_append_u16_le"
               : operation == R_STANDARD_CALL_BYTES_APPEND_U32_LE ? "r_std_bytes_append_u32_le"
                                                                  : "r_std_bytes_append_u64_le";
        arguments[0] = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 0U));
        arguments[1] = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 1U));
        break;
    default:
        return r_llvm_unsupported(emitter, "this std.bytes operation");
    }
    native = r_llvm_std_structure(emitter,
                                  operation == R_STANDARD_CALL_BYTES_WITH_CAPACITY
                                      ? "RStdBytesArrayResult"
                                      : "RStdBytesAllocResult");
    return (native != NULL) && (arguments[0] != NULL) && (arguments[1] != NULL) &&
           (r_llvm_call_runtime(emitter, name, arguments, 2U, native) != NULL) &&
           r_llvm_std_alloc_carrier(emitter,
                                    instruction,
                                    native,
                                    operation == R_STANDARD_CALL_BYTES_WITH_CAPACITY
                                        ? "RStdBytesArrayResult"
                                        : "RStdBytesAllocResult",
                                    "R_STD_ALLOC_CALL_SUCCESS",
                                    "value");
}

/* std.hash digests of a byte view: a digest structure, or the u32 of crc32. */
static bool r_llvm_std_hash(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RStandardCallOperation operation = instruction->standard_operation;
    const char *name = operation == R_STANDARD_CALL_HASH_CRC32    ? "r_std_hash_crc32"
                       : operation == R_STANDARD_CALL_HASH_MD5    ? "r_std_hash_md5"
                       : operation == R_STANDARD_CALL_HASH_SHA1   ? "r_std_hash_sha1"
                       : operation == R_STANDARD_CALL_HASH_SHA256 ? "r_std_hash_sha256"
                                                                  : "r_std_hash_sha512";
    LLVMValueRef source = r_llvm_std_view(emitter, instruction, 0U);

    if (source == NULL) {
        return false;
    }
    if (r_llvm_scalar_type(emitter, instruction->type) != NULL) {
        LLVMValueRef value = r_llvm_call_runtime(emitter, name, &source, 1U, NULL);
        return (value != NULL) && r_llvm_set_value(emitter, instruction->result, value);
    }
    {
        LLVMValueRef result = r_llvm_std_result(emitter, instruction);
        return (result != NULL) &&
               (r_llvm_call_runtime(emitter, name, &source, 1U, result) != NULL);
    }
}

bool r_llvm_std_text(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    switch (instruction->standard_operation) {
    case R_STANDARD_CALL_UTF8_IS_VALID:
    case R_STANDARD_CALL_UTF8_VALIDATE:
        return r_llvm_std_utf8(emitter, instruction);
    case R_STANDARD_CALL_CORE_HASH:
    case R_STANDARD_CALL_CORE_KEY_EQUAL:
        return r_llvm_std_core_key(emitter, instruction);
    case R_STANDARD_CALL_BYTES_EQUAL:
    case R_STANDARD_CALL_BYTES_COMPARE:
    case R_STANDARD_CALL_BYTES_WITH_CAPACITY:
    case R_STANDARD_CALL_BYTES_APPEND:
    case R_STANDARD_CALL_BYTES_APPEND_U8:
    case R_STANDARD_CALL_BYTES_APPEND_U16_LE:
    case R_STANDARD_CALL_BYTES_APPEND_U32_LE:
    case R_STANDARD_CALL_BYTES_APPEND_U64_LE:
        return r_llvm_std_bytes(emitter, instruction);
    case R_STANDARD_CALL_HASH_CRC32:
    case R_STANDARD_CALL_HASH_MD5:
    case R_STANDARD_CALL_HASH_SHA1:
    case R_STANDARD_CALL_HASH_SHA256:
    case R_STANDARD_CALL_HASH_SHA512:
        return r_llvm_std_hash(emitter, instruction);
    default:
        if ((instruction->standard_operation >= R_STANDARD_CALL_STRING_CREATE) &&
            (instruction->standard_operation <= R_STANDARD_CALL_STRING_TRUNCATE)) {
            return r_llvm_std_string(emitter, instruction);
        }
        return r_llvm_unsupported(emitter, "this text operation");
    }
}

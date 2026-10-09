#include "standard_internal.h"

#include "standard_fs_async.h"
#include "standard_net_operations.h"
#include "standard_scoped_operations.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* Library operations that are one call of their C entry (r_c17_emit_async_simple_call and its
   neighbours): the operands are the arguments, a scalar by value and a value in memory by its
   address, which r_llvm_call_runtime passes as the C ABI of the entry asks. */

/* The operands of the instruction from `first` on, after `prefix` leading arguments. */
static bool r_llvm_library_arguments(RLlvmEmitter *emitter,
                                     const RMirInstruction *instruction,
                                     LLVMValueRef *arguments,
                                     unsigned prefix,
                                     unsigned *count) {
    uint32_t index;

    *count = prefix;
    for (index = 0U; index < instruction->operand_count; ++index) {
        LLVMValueRef value = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, index));
        if ((value == NULL) || (*count >= 8U)) {
            return value == NULL ? false : r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
        arguments[(*count)++] = value;
    }
    return true;
}

/* The entry's result is the instruction's result: a scalar becomes its value, anything else
   lands in its memory. */
static bool r_llvm_library_direct(RLlvmEmitter *emitter,
                                  const RMirInstruction *instruction,
                                  const char *name,
                                  LLVMValueRef *arguments,
                                  unsigned count) {
    LLVMValueRef memory;
    LLVMValueRef call;

    if (instruction->result == R_MIR_VALUE_ID_INVALID) {
        return r_llvm_call_runtime(emitter, name, arguments, count, NULL) != NULL;
    }
    if ((r_llvm_scalar_type(emitter, instruction->type) != NULL) &&
        !r_llvm_type_requires_drop(emitter, instruction->type)) {
        call = r_llvm_call_runtime(emitter, name, arguments, count, NULL);
        return (call != NULL) && r_llvm_set_value(emitter, instruction->result, call);
    }
    memory = r_llvm_std_result(emitter, instruction);
    if ((memory == NULL) ||
        (r_llvm_call_runtime(emitter, name, arguments, count, memory) == NULL)) {
        return false;
    }
    r_llvm_std_initialized(emitter, instruction);
    return true;
}

static bool r_llvm_library_operands_direct(RLlvmEmitter *emitter,
                                           const RMirInstruction *instruction,
                                           const char *name) {
    LLVMValueRef arguments[8];
    unsigned count = 0U;

    return r_llvm_library_arguments(emitter, instruction, arguments, 0U, &count) &&
           r_llvm_library_direct(emitter, instruction, name, arguments, count);
}

/* A library result {is_ok, value, error} as the carrier of the instruction. */
static bool r_llvm_library_checked(RLlvmEmitter *emitter,
                                   const RMirInstruction *instruction,
                                   const char *name,
                                   const char *type_name) {
    LLVMValueRef native = r_llvm_std_structure(emitter, type_name);
    LLVMValueRef arguments[8];
    LLVMValueRef is_ok;
    unsigned count = 0U;

    if ((native == NULL) ||
        !r_llvm_library_arguments(emitter, instruction, arguments, 0U, &count) ||
        (r_llvm_call_runtime(emitter, name, arguments, count, native) == NULL)) {
        return false;
    }
    is_ok = r_llvm_std_member(emitter, native, type_name, "is_ok");
    if (is_ok == NULL) {
        return false;
    }
    {
        const RSemanticType *carrier =
            r_llvm_type(emitter, r_llvm_value_type(emitter, instruction->type));
        const bool has_value = (carrier != NULL) && !r_llvm_type_is_void(emitter, carrier->base);
        return r_llvm_std_carrier(
            emitter,
            instruction,
            LLVMBuildICmp(emitter->builder,
                          LLVMIntNE,
                          LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 8U), is_ok, ""),
                          LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                          ""),
            has_value ? r_llvm_std_member(emitter, native, type_name, "value") : NULL,
            r_llvm_std_member(emitter, native, type_name, "error"));
    }
}

/* A library result {status, value, error} of an entry that takes the operands. */
static bool r_llvm_library_operands_status(RLlvmEmitter *emitter,
                                           const RMirInstruction *instruction,
                                           const char *name,
                                           const char *type_name,
                                           const char *success) {
    LLVMValueRef native = r_llvm_std_structure(emitter, type_name);
    LLVMValueRef arguments[8];
    unsigned count = 0U;

    return (native != NULL) &&
           r_llvm_library_arguments(emitter, instruction, arguments, 0U, &count) &&
           (r_llvm_call_runtime(emitter, name, arguments, count, native) != NULL) &&
           r_llvm_std_alloc_carrier(emitter, instruction, native, type_name, success, "value");
}

/* A library result {status, value, error} of an allocating entry that takes the hosted
   allocator first. */
static bool r_llvm_library_allocating(RLlvmEmitter *emitter,
                                      const RMirInstruction *instruction,
                                      const char *name,
                                      const char *type_name,
                                      const char *success) {
    LLVMValueRef native = r_llvm_std_structure(emitter, type_name);
    LLVMValueRef arguments[8];
    unsigned count = 0U;

    arguments[0] = r_llvm_std_allocator(emitter);
    return (native != NULL) && (arguments[0] != NULL) &&
           r_llvm_library_arguments(emitter, instruction, arguments, 1U, &count) &&
           (r_llvm_call_runtime(emitter, name, arguments, count, native) != NULL) &&
           r_llvm_std_alloc_carrier(emitter, instruction, native, type_name, success, "value");
}

/* The storage a consuming entry takes and the flag it clears once it has taken it: the place of
   a staged move (is_async_staged_move), whose MOVE is not emitted, or the moved value. */
static bool r_llvm_library_consumed(RLlvmEmitter *emitter,
                                    RMirValueId value,
                                    LLVMValueRef *address,
                                    LLVMValueRef *flag) {
    const RMirInstruction *definition = r_llvm_definition(emitter, value);

    if (definition == NULL) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    if (r_llvm_staged_move(emitter, definition)) {
        if (definition->operand1 != R_MIR_VALUE_ID_INVALID) {
            return r_llvm_unsupported(emitter, "a library operation consuming a projected place");
        }
        *address = r_llvm_place_address(emitter,
                                        definition->place_ordinal,
                                        definition->place_is_parameter,
                                        R_MIR_VALUE_ID_INVALID);
        *flag =
            r_llvm_place_flag(emitter, definition->place_ordinal, definition->place_is_parameter);
    } else {
        *address = r_llvm_value(emitter, value);
        *flag = r_llvm_value_flag(emitter, value);
    }
    return *address != NULL;
}

/* R-SLIB-ASYNC-0008: the deadline structure {has_value, value} of an entry from an optional
   instant, narrowed to the deadline of the starting task (Core R-STMT-0019). */
static LLVMValueRef
r_llvm_library_deadline(RLlvmEmitter *emitter, RMirValueId option, const char *type_name) {
    const RTypeId option_type = r_llvm_std_operand_type(emitter, option);
    LLVMValueRef native = r_llvm_std_structure(emitter, type_name);
    LLVMValueRef source = r_llvm_value(emitter, option);
    LLVMValueRef has_value = r_llvm_std_member(emitter, native, type_name, "has_value");
    LLVMValueRef value = r_llvm_std_member(emitter, native, type_name, "value");
    LLVMValueRef arguments[3];
    LLVMBasicBlockRef some;
    LLVMBasicBlockRef next;
    uint32_t payload = 0U;
    uint32_t seconds = 0U;
    uint32_t nanoseconds = 0U;
    uint32_t size = 0U;
    uint32_t align = 0U;

    if ((native == NULL) || (source == NULL) || (has_value == NULL) || (value == NULL) ||
        !r_llvm_payload_offset(emitter, option_type, &payload) ||
        !r_llvm_runtime_layout(emitter, "RStdTimeInstant", &size, &align) ||
        !r_llvm_runtime_field(emitter, "RStdTimeInstant", "storage_seconds", &seconds, NULL) ||
        !r_llvm_runtime_field(
            emitter, "RStdTimeInstant", "storage_nanoseconds", &nanoseconds, NULL)) {
        return NULL;
    }
    some = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    next = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    (void)LLVMBuildCondBr(
        emitter->builder,
        LLVMBuildICmp(emitter->builder,
                      LLVMIntEQ,
                      LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), source, ""),
                      r_llvm_u32(emitter, 1U),
                      ""),
        some,
        next);
    LLVMPositionBuilderAtEnd(emitter->builder, some);
    (void)LLVMBuildStore(emitter->builder, LLVMConstInt(r_llvm_int(emitter, 8U), 1U, 0), has_value);
    (void)LLVMBuildMemCpy(emitter->builder,
                          value,
                          align,
                          r_llvm_byte_offset(emitter, source, payload),
                          align,
                          r_llvm_u64(emitter, size));
    (void)LLVMBuildBr(emitter->builder, next);
    LLVMPositionBuilderAtEnd(emitter->builder, next);
    arguments[0] = has_value;
    arguments[1] = r_llvm_byte_offset(emitter, value, seconds);
    arguments[2] = r_llvm_byte_offset(emitter, value, nanoseconds);
    return r_llvm_call_runtime(emitter, "r_runtime_task_deadline_narrow", arguments, 3U, NULL) ==
                   NULL
               ? NULL
               : native;
}

/* A library task start {is_ok, task, error} as the start carrier of the instruction; a consumed
   operand is no longer initialized once the task owns it. */
static bool r_llvm_library_start(RLlvmEmitter *emitter,
                                 const RMirInstruction *instruction,
                                 const char *name,
                                 const char *type_name,
                                 LLVMValueRef *arguments,
                                 unsigned count,
                                 LLVMValueRef consumed_flag) {
    LLVMValueRef native = r_llvm_std_structure(emitter, type_name);
    LLVMValueRef is_ok;
    LLVMValueRef started;

    if ((native == NULL) ||
        (r_llvm_call_runtime(emitter, name, arguments, count, native) == NULL)) {
        return false;
    }
    is_ok = r_llvm_std_member(emitter, native, type_name, "is_ok");
    if (is_ok == NULL) {
        return false;
    }
    started = LLVMBuildICmp(emitter->builder,
                            LLVMIntNE,
                            LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 8U), is_ok, ""),
                            LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                            "");
    if (consumed_flag != NULL) {
        (void)LLVMBuildStore(
            emitter->builder,
            LLVMBuildSelect(
                emitter->builder,
                started,
                LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 8U), consumed_flag, ""),
                ""),
            consumed_flag);
    }
    return r_llvm_std_carrier(emitter,
                              instruction,
                              started,
                              r_llvm_std_member(emitter, native, type_name, "task"),
                              r_llvm_std_member(emitter, native, type_name, "error"));
}

/* A library task start whose last argument is the deadline (r_c17_emit_async_fs_extended,
   r_c17_emit_async_net, r_c17_emit_async_scoped): the operands in order, the one at move_index
   consumed from its place, and the deadline structure of the module. */
static bool r_llvm_library_task(RLlvmEmitter *emitter,
                                const RMirInstruction *instruction,
                                const char *name,
                                const char *deadline_type,
                                const char *start_type,
                                uint32_t argument_count,
                                uint32_t move_index) {
    LLVMValueRef arguments[8];
    LLVMValueRef flag = NULL;
    uint32_t index;

    if ((argument_count == 0U) || (argument_count > 8U) ||
        (instruction->operand_count != argument_count)) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    for (index = 0U; index < argument_count; ++index) {
        const RMirValueId operand = r_llvm_std_operand(emitter, instruction, index);
        if (index + 1U == argument_count) {
            arguments[index] = r_llvm_library_deadline(emitter, operand, deadline_type);
        } else if (index == move_index) {
            if (!r_llvm_library_consumed(emitter, operand, &arguments[index], &flag)) {
                return false;
            }
        } else {
            arguments[index] = r_llvm_value(emitter, operand);
        }
        if (arguments[index] == NULL) {
            return false;
        }
    }
    return r_llvm_library_start(
        emitter, instruction, name, start_type, arguments, argument_count, flag);
}

/* The asynchronous operations of std.fs, std.net and the scoped transfers described by tables. */
static bool r_llvm_library_described(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RStandardCallOperation operation = instruction->standard_operation;
    const RStandardScopedOperationDescriptor *scoped = r_standard_scoped_operation(operation);
    const RStandardFsAsyncDescriptor *fs = r_standard_fs_async_descriptor(operation);
    const RStandardNetOperationDescriptor *net = r_standard_net_operation(operation);

    if (scoped != NULL) {
        return r_llvm_library_task(emitter,
                                   instruction,
                                   scoped->native_name,
                                   scoped->c_deadline,
                                   scoped->c_start,
                                   r_standard_scoped_argument_count(scoped),
                                   UINT32_MAX);
    }
    if (operation == R_STANDARD_CALL_FS_CREATE_DIRECTORY_BENEATH) {
        return r_llvm_library_task(emitter,
                                   instruction,
                                   "r_std_fs_create_directory_beneath",
                                   "RStdFsDeadline",
                                   "RStdFsTaskStartResult",
                                   4U,
                                   UINT32_MAX);
    }
    if (operation == R_STANDARD_CALL_FS_WRITE_FILE_ATOMIC_NO_REPLACE_BENEATH) {
        /* The data moves into the task once it starts. */
        return r_llvm_library_task(emitter,
                                   instruction,
                                   "r_std_fs_write_file_atomic_no_replace_beneath",
                                   "RStdFsDeadline",
                                   "RStdFsTaskStartResult",
                                   4U,
                                   2U);
    }
    if (fs != NULL) {
        return r_llvm_library_task(emitter,
                                   instruction,
                                   fs->native_name,
                                   "RStdFsDeadline",
                                   "RStdFsTaskStartResult",
                                   fs->argument_count,
                                   fs->move_index);
    }
    if ((net != NULL) && net->is_async) {
        char name[96];
        (void)snprintf(name, sizeof(name), "r_std_net_%s", net->name);
        return r_llvm_library_task(emitter,
                                   instruction,
                                   name,
                                   "RStdNetDeadline",
                                   "RStdNetTaskStartResult",
                                   net->argument_count,
                                   net->move_index);
    }
    return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
}

/* std.io: console streams, the owned-buffer transfers, flush and close (R-SLIB-IO). */
static bool r_llvm_library_io(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RStandardCallOperation operation = instruction->standard_operation;
    LLVMValueRef arguments[5];
    LLVMValueRef consumed = NULL;
    LLVMValueRef flag = NULL;
    const char *name;
    unsigned count = 0U;

    switch (operation) {
    case R_STANDARD_CALL_IO_READ:
    case R_STANDARD_CALL_IO_WRITE:
    case R_STANDARD_CALL_IO_WRITE_ALL:
        name = operation == R_STANDARD_CALL_IO_READ    ? "r_std_io_read"
               : operation == R_STANDARD_CALL_IO_WRITE ? "r_std_io_write"
                                                       : "r_std_io_write_all";
        arguments[count++] = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 0U));
        if (!r_llvm_library_consumed(
                emitter, r_llvm_std_operand(emitter, instruction, 1U), &consumed, &flag)) {
            return false;
        }
        arguments[count++] = consumed;
        arguments[count++] = r_llvm_library_deadline(
            emitter, r_llvm_std_operand(emitter, instruction, 2U), "RStdIoDeadline");
        break;
    case R_STANDARD_CALL_IO_WRITE_SHARED:
        name = "r_std_io_write_shared";
        arguments[count++] = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 0U));
        if (!r_llvm_library_consumed(
                emitter, r_llvm_std_operand(emitter, instruction, 1U), &consumed, &flag)) {
            return false;
        }
        arguments[count++] = consumed;
        arguments[count++] = r_llvm_std_size(emitter, r_llvm_std_operand(emitter, instruction, 2U));
        arguments[count++] = r_llvm_std_size(emitter, r_llvm_std_operand(emitter, instruction, 3U));
        arguments[count++] = r_llvm_library_deadline(
            emitter, r_llvm_std_operand(emitter, instruction, 4U), "RStdIoDeadline");
        break;
    case R_STANDARD_CALL_IO_CLOSE_INPUT:
    case R_STANDARD_CALL_IO_CLOSE_OUTPUT:
        name = operation == R_STANDARD_CALL_IO_CLOSE_INPUT ? "r_std_io_close_input"
                                                           : "r_std_io_close_output";
        if (!r_llvm_library_consumed(
                emitter, r_llvm_std_operand(emitter, instruction, 0U), &consumed, &flag)) {
            return false;
        }
        arguments[count++] = consumed;
        arguments[count++] = r_llvm_library_deadline(
            emitter, r_llvm_std_operand(emitter, instruction, 1U), "RStdIoDeadline");
        break;
    case R_STANDARD_CALL_IO_FLUSH:
        /* M18-3: a borrowed stream is the first of two operands, an owned one the place. */
        name = "r_std_io_flush";
        arguments[count++] =
            instruction->operand_count == 2U
                ? r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 0U))
                : r_llvm_place_address(emitter,
                                       instruction->place_ordinal,
                                       instruction->place_is_parameter,
                                       instruction->operand0);
        arguments[count++] = r_llvm_library_deadline(
            emitter,
            r_llvm_std_operand(emitter, instruction, instruction->operand_count - 1U),
            "RStdIoDeadline");
        break;
    default:
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    for (unsigned index = 0U; index < count; ++index) {
        if (arguments[index] == NULL) {
            return false;
        }
    }
    return r_llvm_library_start(
        emitter, instruction, name, "RStdIoTaskStartResult", arguments, count, flag);
}

/* std.fs: the path operations and the task starts on a path or a file (R-SLIB-FS). */
static bool r_llvm_library_fs(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RStandardCallOperation operation = instruction->standard_operation;
    const bool extra =
        (operation == R_STANDARD_CALL_FS_READ_FILE) || (operation == R_STANDARD_CALL_FS_OPEN_FILE);
    LLVMValueRef arguments[3];
    const char *name;
    unsigned count = 0U;

    switch (operation) {
    case R_STANDARD_CALL_FS_PATH_CLONE:
        return r_llvm_library_operands_status(emitter,
                                              instruction,
                                              "r_std_fs_path_clone",
                                              "RStdFsPathAllocResult",
                                              "R_STD_FS_CALL_SUCCESS");
    case R_STANDARD_CALL_FS_PATH_JOIN:
        return r_llvm_library_operands_status(emitter,
                                              instruction,
                                              "r_std_fs_path_join",
                                              "RStdFsPathResult",
                                              "R_STD_FS_CALL_SUCCESS");
    case R_STANDARD_CALL_FS_PATH_TO_UTF8:
        return r_llvm_library_operands_status(emitter,
                                              instruction,
                                              "r_std_fs_path_to_utf8",
                                              "RStdFsStringResult",
                                              "R_STD_FS_CALL_SUCCESS");
    case R_STANDARD_CALL_FS_READ_FILE:
        name = "r_std_fs_read_file";
        break;
    case R_STANDARD_CALL_FS_OPEN_FILE:
        name = "r_std_fs_open_file";
        break;
    case R_STANDARD_CALL_FS_OPEN_DIRECTORY:
        name = "r_std_fs_open_directory";
        break;
    case R_STANDARD_CALL_FS_FILE_METADATA:
        name = "r_std_fs_file_metadata";
        break;
    default:
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    /* The path or file is the place of the instruction, borrowed for the start; a path that is
       itself a borrow (a parameter of a synchronous function) is the first operand instead, as the
       MIR writer distinguishes them by the operand count (B6-5). */
    {
        const bool path_value = (operation != R_STANDARD_CALL_FS_FILE_METADATA) &&
                                (instruction->operand_count == (extra ? 3U : 2U));
        const uint32_t first = path_value ? 1U : 0U;
        arguments[count++] =
            path_value ? r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 0U))
                       : r_llvm_place_address(emitter,
                                              instruction->place_ordinal,
                                              instruction->place_is_parameter,
                                              instruction->operand0);
        if (operation == R_STANDARD_CALL_FS_READ_FILE) {
            arguments[count++] =
                r_llvm_std_size(emitter, r_llvm_std_operand(emitter, instruction, first));
        } else if (extra) {
            arguments[count++] =
                r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, first));
        }
        arguments[count++] = r_llvm_library_deadline(
            emitter,
            r_llvm_std_operand(emitter, instruction, first + (extra ? 1U : 0U)),
            "RStdFsDeadline");
    }
    for (unsigned index = 0U; index < count; ++index) {
        if (arguments[index] == NULL) {
            return false;
        }
    }
    return r_llvm_library_start(
        emitter, instruction, name, "RStdFsTaskStartResult", arguments, count, NULL);
}

/* std.time without the sleeps (R-SLIB-TIME). */
static bool r_llvm_library_time(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    switch (instruction->standard_operation) {
    case R_STANDARD_CALL_TIME_DURATION_FROM_SECONDS:
        return r_llvm_library_operands_direct(
            emitter, instruction, "r_std_time_duration_from_seconds");
    case R_STANDARD_CALL_TIME_DURATION_SECONDS:
        return r_llvm_library_operands_direct(emitter, instruction, "r_std_time_duration_seconds");
    case R_STANDARD_CALL_TIME_DURATION_NANOSECONDS:
        return r_llvm_library_operands_direct(
            emitter, instruction, "r_std_time_duration_nanoseconds");
    case R_STANDARD_CALL_TIME_DURATION_COMPARE:
        return r_llvm_library_operands_direct(emitter, instruction, "r_std_time_duration_compare");
    case R_STANDARD_CALL_TIME_DURATION_FROM_PARTS:
        return r_llvm_library_checked(
            emitter, instruction, "r_std_time_duration_from_parts", "RStdTimeDurationResult");
    case R_STANDARD_CALL_TIME_DURATION_ADD:
        return r_llvm_library_checked(
            emitter, instruction, "r_std_time_duration_add", "RStdTimeDurationResult");
    case R_STANDARD_CALL_TIME_DURATION_SUB:
        return r_llvm_library_checked(
            emitter, instruction, "r_std_time_duration_sub", "RStdTimeDurationResult");
    case R_STANDARD_CALL_TIME_DURATION_MULTIPLY:
        return r_llvm_library_checked(
            emitter, instruction, "r_std_time_duration_multiply", "RStdTimeDurationResult");
    case R_STANDARD_CALL_TIME_MONOTONIC_NOW:
        return r_llvm_library_checked(
            emitter, instruction, "r_std_time_monotonic_now", "RStdTimeInstantResult");
    case R_STANDARD_CALL_TIME_SYSTEM_NOW:
        return r_llvm_library_checked(
            emitter, instruction, "r_std_time_system_now", "RStdTimeSystemTimeResult");
    case R_STANDARD_CALL_TIME_INSTANT_ADD:
        return r_llvm_library_checked(
            emitter, instruction, "r_std_time_instant_add", "RStdTimeInstantResult");
    case R_STANDARD_CALL_TIME_INSTANT_DURATION:
        return r_llvm_library_checked(
            emitter, instruction, "r_std_time_instant_duration", "RStdTimeDurationTimeResult");
    case R_STANDARD_CALL_TIME_SYSTEM_ADD:
        return r_llvm_library_checked(
            emitter, instruction, "r_std_time_system_add", "RStdTimeSystemTimeResult");
    case R_STANDARD_CALL_TIME_TO_UTC:
        return r_llvm_library_checked(
            emitter, instruction, "r_std_time_to_utc", "RStdTimeUtcDateTimeResult");
    case R_STANDARD_CALL_TIME_FROM_UTC:
        return r_llvm_library_checked(
            emitter, instruction, "r_std_time_from_utc", "RStdTimeSystemTimeResult");
    case R_STANDARD_CALL_TIME_AS_ERROR:
        return r_llvm_library_operands_direct(emitter, instruction, "r_std_time_as_error");
    case R_STANDARD_CALL_TIME_SLEEP_FOR:
    case R_STANDARD_CALL_TIME_SLEEP_UNTIL: {
        LLVMValueRef arguments[1];
        arguments[0] = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 0U));
        return (arguments[0] != NULL) && r_llvm_library_start(emitter,
                                                              instruction,
                                                              instruction->standard_operation ==
                                                                      R_STANDARD_CALL_TIME_SLEEP_FOR
                                                                  ? "r_std_time_sleep_for"
                                                                  : "r_std_time_sleep_until",
                                                              "RStdTimeSleepStartResult",
                                                              arguments,
                                                              1U,
                                                              NULL);
    }
    default:
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
}

/* std.convert::parse (r_c17_emit_async_convert_parse_call): a float through its own entry, an
   integer through the shared one with the bounds of its type; tag 23 is the C bool. A parsed
   magnitude becomes the value by truncation, negated in two's complement when negative. */
static bool r_llvm_library_parse(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    static const struct {
        uint64_t tag;
        const char *native_type;
        const char *operation;
    } floats[] = {
        {10U, "RStdConvertParseF32Result", "r_std_convert_parse_f32"},
        {11U, "RStdConvertParseF64Result", "r_std_convert_parse_f64"},
        {38U, "RStdConvertParseCFloatResult", "r_std_convert_parse_c_float"},
        {39U, "RStdConvertParseCDoubleResult", "r_std_convert_parse_c_double"},
        {40U, "RStdConvertParseCLongDoubleResult", "r_std_convert_parse_c_long_double"},
    };
    const RTypeId target = instruction->auxiliary_type;
    const char *native_type = "RStdConvertParseIntegerResult";
    const char *operation = "r_std_convert_parse_suffix";
    bool floating = false;
    LLVMValueRef arguments[3];
    LLVMValueRef native;
    LLVMValueRef result;
    LLVMValueRef value;
    LLVMBasicBlockRef success;
    LLVMBasicBlockRef failure;
    LLVMBasicBlockRef invalid;
    LLVMBasicBlockRef done;
    LLVMValueRef choice;
    int64_t success_status = 0;
    int64_t error_status = 0;
    uint32_t payload = 0U;
    size_t index;

    if (instruction->integer_value >= 43U) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    for (index = 0U; index < sizeof(floats) / sizeof(floats[0]); ++index) {
        if (floats[index].tag == instruction->integer_value) {
            native_type = floats[index].native_type;
            operation = floats[index].operation;
            floating = true;
        }
    }
    native = r_llvm_std_structure(emitter, native_type);
    arguments[0] = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 0U));
    if ((native == NULL) || (arguments[0] == NULL) ||
        !r_llvm_runtime_constant(emitter, "R_STD_CONVERT_CALL_SUCCESS", &success_status) ||
        !r_llvm_runtime_constant(emitter, "R_STD_CONVERT_CALL_ERROR", &error_status) ||
        !r_llvm_payload_offset(emitter, instruction->type, &payload)) {
        return false;
    }
    if (!floating) {
        LLVMTypeRef integer = r_llvm_scalar_type(emitter, target);
        const RSemanticTypeKind kind = r_llvm_value_kind(emitter, target);
        const RSemanticTypeKind representation =
            kind == R_SEMANTIC_TYPE_C_ABI_NUMERIC
                ? r_semantic_integer_representation(emitter->frontend, target)
                : kind;
        LLVMValueRef bounds = r_llvm_std_structure(emitter, "RStdConvertIntegerBounds");
        LLVMValueRef radix = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 1U));
        unsigned bits = 0U;
        bool is_signed = false;
        uint64_t maximum;
        if ((integer == NULL) || (bounds == NULL) || (radix == NULL)) {
            return integer == NULL ? r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR) : false;
        }
        if (instruction->integer_value == 23U) {
            maximum = 1U;
        } else if (!r_llvm_kind_integer(representation, &bits, &is_signed)) {
            return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        } else {
            maximum = is_signed ? (UINT64_MAX >> (65U - bits))
                                : (bits == 64U ? UINT64_MAX : (UINT64_C(1) << bits) - 1U);
        }
        (void)LLVMBuildStore(
            emitter->builder,
            r_llvm_u64(emitter, maximum),
            r_llvm_std_member(emitter, bounds, "RStdConvertIntegerBounds", "max_positive"));
        (void)LLVMBuildStore(
            emitter->builder,
            r_llvm_u64(emitter, is_signed ? maximum + 1U : 0U),
            r_llvm_std_member(
                emitter, bounds, "RStdConvertIntegerBounds", "max_negative_magnitude"));
        arguments[1] = LLVMBuildIntCast2(emitter->builder, radix, r_llvm_int(emitter, 32U), 0, "");
        arguments[2] = bounds;
    }
    if (r_llvm_call_runtime(emitter, operation, arguments, floating ? 1U : 3U, native) == NULL) {
        return false;
    }
    result = r_llvm_std_result(emitter, instruction);
    if (result == NULL) {
        return false;
    }
    success = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    failure = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    invalid = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    done = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    choice =
        LLVMBuildSwitch(emitter->builder,
                        LLVMBuildLoad2(emitter->builder,
                                       r_llvm_int(emitter, 32U),
                                       r_llvm_std_member(emitter, native, native_type, "status"),
                                       ""),
                        invalid,
                        2U);
    LLVMAddCase(choice, r_llvm_u32(emitter, (uint64_t)success_status), success);
    LLVMAddCase(choice, r_llvm_u32(emitter, (uint64_t)error_status), failure);
    LLVMPositionBuilderAtEnd(emitter->builder, success);
    if (floating) {
        if (!r_llvm_std_copy(emitter,
                             target,
                             r_llvm_byte_offset(emitter, result, payload),
                             r_llvm_std_member(emitter, native, native_type, "value"))) {
            return false;
        }
    } else {
        LLVMValueRef parsed = r_llvm_std_member(emitter, native, native_type, "value");
        uint32_t magnitude_offset = 0U;
        uint32_t negative_offset = 0U;
        LLVMValueRef magnitude;
        LLVMValueRef negative;
        if ((parsed == NULL) ||
            !r_llvm_runtime_field(
                emitter, "RStdConvertParsedInteger", "magnitude", &magnitude_offset, NULL) ||
            !r_llvm_runtime_field(
                emitter, "RStdConvertParsedInteger", "negative", &negative_offset, NULL)) {
            return false;
        }
        magnitude =
            LLVMBuildTrunc(emitter->builder,
                           LLVMBuildLoad2(emitter->builder,
                                          r_llvm_int(emitter, 64U),
                                          r_llvm_byte_offset(emitter, parsed, magnitude_offset),
                                          ""),
                           r_llvm_scalar_type(emitter, target),
                           "");
        negative =
            LLVMBuildICmp(emitter->builder,
                          LLVMIntNE,
                          LLVMBuildLoad2(emitter->builder,
                                         r_llvm_int(emitter, 8U),
                                         r_llvm_byte_offset(emitter, parsed, negative_offset),
                                         ""),
                          LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                          "");
        value = LLVMBuildSelect(emitter->builder,
                                negative,
                                LLVMBuildNeg(emitter->builder, magnitude, ""),
                                magnitude,
                                "");
        r_llvm_store_scalar(emitter, target, value, r_llvm_byte_offset(emitter, result, payload));
    }
    (void)LLVMBuildBr(emitter->builder, done);
    LLVMPositionBuilderAtEnd(emitter->builder, failure);
    (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 1U), result);
    if (!r_llvm_std_copy(emitter,
                         r_llvm_std_single_effect(emitter, instruction->type),
                         r_llvm_byte_offset(emitter, result, payload),
                         r_llvm_std_member(emitter, native, native_type, "error"))) {
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
    r_llvm_std_initialized(emitter, instruction);
    return true;
}

/* The tag of a numeric type in RStdConvertNumericType (r_c17_numeric_type_tag): the R types in
   their own order, then the C types in token order from c_char. */
static bool r_llvm_numeric_tag(RLlvmEmitter *emitter, RTypeId id, uint32_t *tag) {
    static const RSemanticTypeKind kinds[] = {R_SEMANTIC_TYPE_I8,
                                              R_SEMANTIC_TYPE_U8,
                                              R_SEMANTIC_TYPE_I16,
                                              R_SEMANTIC_TYPE_U16,
                                              R_SEMANTIC_TYPE_I32,
                                              R_SEMANTIC_TYPE_U32,
                                              R_SEMANTIC_TYPE_I64,
                                              R_SEMANTIC_TYPE_U64,
                                              R_SEMANTIC_TYPE_ISIZE,
                                              R_SEMANTIC_TYPE_USIZE,
                                              R_SEMANTIC_TYPE_F32,
                                              R_SEMANTIC_TYPE_F64};
    const RSemanticType *type = r_llvm_type(emitter, r_llvm_value_type(emitter, id));
    uint32_t index;

    if (type == NULL) {
        return false;
    }
    if (type->kind == R_SEMANTIC_TYPE_C_ABI_NUMERIC) {
        if ((type->length < (uint64_t)R_TOKEN_KW_C_CHAR) ||
            (type->length > (uint64_t)R_TOKEN_KW_C_PTRDIFF)) {
            return false;
        }
        *tag = 12U + (uint32_t)type->length - (uint32_t)R_TOKEN_KW_C_CHAR;
        return true;
    }
    for (index = 0U; index < sizeof(kinds) / sizeof(kinds[0]); ++index) {
        if (kinds[index] == type->kind) {
            *tag = index;
            return true;
        }
    }
    return false;
}

/* std.convert::checked_D and std.c::checked_D (r_c17_emit_checked_conversion_call): the source
   goes into the type-erased RStdConvertNumericValue (integers widened to 64 bits by their sign,
   a bool as 0 or 1, floats as their bits), the destination comes back the same way. */
static bool r_llvm_library_checked_number(RLlvmEmitter *emitter,
                                          const RMirInstruction *instruction) {
    const RMirValueId operand = r_llvm_std_operand(emitter, instruction, 0U);
    const RMirInstruction *source = r_llvm_definition(emitter, operand);
    const RTypeId target = instruction->auxiliary_type;
    const char *const native_type = "RStdConvertCheckedResult";
    LLVMValueRef value = r_llvm_value(emitter, operand);
    LLVMValueRef arguments[2];
    LLVMValueRef native;
    LLVMValueRef result;
    LLVMValueRef choice;
    LLVMValueRef bits;
    LLVMBasicBlockRef succeeded;
    LLVMBasicBlockRef failed;
    LLVMBasicBlockRef invalid;
    LLVMBasicBlockRef done;
    RSemanticTypeKind kind;
    uint32_t source_tag = 0U;
    uint32_t target_tag = 0U;
    uint32_t payload = 0U;
    uint32_t numeric = 0U;
    unsigned width = 0U;
    bool is_signed = false;
    int64_t success_status = 0;
    int64_t error_status = 0;

    if ((source == NULL) || (value == NULL) ||
        !r_llvm_numeric_tag(emitter, source->type, &source_tag) ||
        !r_llvm_numeric_tag(emitter, target, &target_tag) ||
        (target_tag != instruction->integer_value) ||
        ((instruction->standard_operation == R_STANDARD_CALL_C_CHECKED) && (target_tag < 12U))) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    arguments[0] = r_llvm_std_structure(emitter, "RStdConvertNumericValue");
    arguments[1] = r_llvm_std_structure(emitter, "RStdConvertDestination");
    if ((arguments[0] == NULL) || (arguments[1] == NULL) ||
        !r_llvm_runtime_field(emitter, "RStdConvertNumericValue", "payload", &numeric, NULL) ||
        !r_llvm_runtime_constant(emitter, "R_STD_CONVERT_CALL_SUCCESS", &success_status) ||
        !r_llvm_runtime_constant(emitter, "R_STD_CONVERT_CALL_ERROR", &error_status) ||
        !r_llvm_payload_offset(emitter, instruction->type, &payload)) {
        return false;
    }
    (void)LLVMBuildStore(
        emitter->builder,
        r_llvm_u32(emitter, source_tag),
        r_llvm_std_member(emitter, arguments[0], "RStdConvertNumericValue", "type"));
    (void)LLVMBuildStore(
        emitter->builder,
        r_llvm_u32(emitter, target_tag),
        r_llvm_std_member(emitter, arguments[1], "RStdConvertDestination", "type"));
    kind = r_llvm_value_kind(emitter, source->type);
    if (kind == R_SEMANTIC_TYPE_F32) {
        bits = LLVMBuildBitCast(emitter->builder, value, r_llvm_int(emitter, 32U), "");
    } else if (kind == R_SEMANTIC_TYPE_F64) {
        bits = LLVMBuildBitCast(emitter->builder, value, r_llvm_int(emitter, 64U), "");
    } else if (kind == R_SEMANTIC_TYPE_BOOL) {
        bits = LLVMBuildZExt(emitter->builder, value, r_llvm_int(emitter, 64U), "");
    } else if (r_llvm_kind_integer(kind, &width, &is_signed)) {
        bits = LLVMBuildIntCast2(emitter->builder, value, r_llvm_int(emitter, 64U), is_signed, "");
    } else {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    (void)LLVMBuildStore(
        emitter->builder, bits, r_llvm_byte_offset(emitter, arguments[0], numeric));
    native = r_llvm_std_structure(emitter, native_type);
    if ((native == NULL) ||
        (r_llvm_call_runtime(emitter,
                             instruction->standard_operation == R_STANDARD_CALL_C_CHECKED
                                 ? "r_std_c_checked"
                                 : "r_std_convert_checked",
                             arguments,
                             2U,
                             native) == NULL)) {
        return false;
    }
    result = r_llvm_std_result(emitter, instruction);
    if (result == NULL) {
        return false;
    }
    succeeded = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    failed = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    invalid = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    done = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    choice =
        LLVMBuildSwitch(emitter->builder,
                        LLVMBuildLoad2(emitter->builder,
                                       r_llvm_int(emitter, 32U),
                                       r_llvm_std_member(emitter, native, native_type, "status"),
                                       ""),
                        invalid,
                        2U);
    LLVMAddCase(choice, r_llvm_u32(emitter, (uint64_t)success_status), succeeded);
    LLVMAddCase(choice, r_llvm_u32(emitter, (uint64_t)error_status), failed);
    LLVMPositionBuilderAtEnd(emitter->builder, succeeded);
    {
        LLVMValueRef from = r_llvm_byte_offset(
            emitter, r_llvm_std_member(emitter, native, native_type, "value"), numeric);
        LLVMValueRef to = r_llvm_byte_offset(emitter, result, payload);
        kind = r_llvm_value_kind(emitter, target);
        if (kind == R_SEMANTIC_TYPE_F32) {
            value = LLVMBuildBitCast(
                emitter->builder,
                LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), from, ""),
                LLVMFloatTypeInContext(emitter->context),
                "");
        } else if (kind == R_SEMANTIC_TYPE_F64) {
            value = LLVMBuildBitCast(
                emitter->builder,
                LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 64U), from, ""),
                LLVMDoubleTypeInContext(emitter->context),
                "");
        } else if ((kind == R_SEMANTIC_TYPE_BOOL) ||
                   r_llvm_kind_integer(kind, &width, &is_signed)) {
            value =
                LLVMBuildTrunc(emitter->builder,
                               LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 64U), from, ""),
                               r_llvm_scalar_type(emitter, target),
                               "");
        } else {
            return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
        r_llvm_store_scalar(emitter, target, value, to);
    }
    (void)LLVMBuildBr(emitter->builder, done);
    LLVMPositionBuilderAtEnd(emitter->builder, failed);
    (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 1U), result);
    if (!r_llvm_std_copy(emitter,
                         r_llvm_std_single_effect(emitter, instruction->type),
                         r_llvm_byte_offset(emitter, result, payload),
                         r_llvm_std_member(emitter, native, native_type, "error"))) {
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
    r_llvm_std_initialized(emitter, instruction);
    return true;
}

/* std.process::exit and std.process::abort end the process; the code after them is unreachable. */
static bool r_llvm_library_end_process(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const bool exiting = instruction->standard_operation == R_STANDARD_CALL_PROCESS_EXIT;
    LLVMValueRef status = NULL;

    if (exiting) {
        status = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 0U));
        if (status == NULL) {
            return false;
        }
    }
    if (r_llvm_call_runtime(emitter,
                            exiting ? "r_std_process_exit" : "r_std_process_abort",
                            &status,
                            exiting ? 1U : 0U,
                            NULL) == NULL) {
        return false;
    }
    (void)LLVMBuildUnreachable(emitter->builder);
    LLVMPositionBuilderAtEnd(
        emitter->builder, LLVMAppendBasicBlockInContext(emitter->context, emitter->function, ""));
    return true;
}

/* A library result {status, value, error} whose status is success or error and nothing else: the
   carrier of the value or of the error; any other status breaks the contract. */
static bool r_llvm_library_status(RLlvmEmitter *emitter,
                                  const RMirInstruction *instruction,
                                  const char *name,
                                  const char *type_name,
                                  const char *success,
                                  const char *error,
                                  LLVMValueRef *arguments,
                                  unsigned count) {
    const RSemanticType *carrier =
        r_llvm_type(emitter, r_llvm_value_type(emitter, instruction->type));
    LLVMValueRef native = r_llvm_std_structure(emitter, type_name);
    LLVMValueRef result;
    LLVMValueRef choice;
    LLVMBasicBlockRef succeeded;
    LLVMBasicBlockRef failed;
    LLVMBasicBlockRef invalid;
    LLVMBasicBlockRef done;
    int64_t success_status = 0;
    int64_t error_status = 0;
    uint32_t payload = 0U;

    if ((carrier == NULL) || (native == NULL) ||
        !r_llvm_runtime_constant(emitter, success, &success_status) ||
        !r_llvm_runtime_constant(emitter, error, &error_status) ||
        !r_llvm_payload_offset(emitter, instruction->type, &payload) ||
        (r_llvm_call_runtime(emitter, name, arguments, count, native) == NULL)) {
        return false;
    }
    result = r_llvm_std_result(emitter, instruction);
    if (result == NULL) {
        return false;
    }
    succeeded = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    failed = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    invalid = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    done = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    choice = LLVMBuildSwitch(emitter->builder,
                             LLVMBuildLoad2(emitter->builder,
                                            r_llvm_int(emitter, 32U),
                                            r_llvm_std_member(emitter, native, type_name, "status"),
                                            ""),
                             invalid,
                             2U);
    LLVMAddCase(choice, r_llvm_u32(emitter, (uint64_t)success_status), succeeded);
    LLVMAddCase(choice, r_llvm_u32(emitter, (uint64_t)error_status), failed);
    LLVMPositionBuilderAtEnd(emitter->builder, succeeded);
    if (!r_llvm_type_is_void(emitter, carrier->base) &&
        !r_llvm_std_copy(emitter,
                         carrier->base,
                         r_llvm_byte_offset(emitter, result, payload),
                         r_llvm_std_member(emitter, native, type_name, "value"))) {
        return false;
    }
    (void)LLVMBuildBr(emitter->builder, done);
    LLVMPositionBuilderAtEnd(emitter->builder, failed);
    (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 1U), result);
    if (!r_llvm_std_copy(emitter,
                         r_llvm_std_single_effect(emitter, instruction->type),
                         r_llvm_byte_offset(emitter, result, payload),
                         r_llvm_std_member(emitter, native, type_name, "error"))) {
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
    r_llvm_std_initialized(emitter, instruction);
    return true;
}

/* R-FUNC-0026 (L35, r_c17_emit_recursion_call): each function with `@recursion(depth = N)`
   counts its activations on the thread in a thread-local counter. The entry answers
   core::recursion_error {depth = N} when N activations exist and otherwise begins one; the
   compiler-owned finally of the body ends it. The entry marks its body for the stack bounds. */
static bool r_llvm_library_recursion(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RSymbolId symbol =
        instruction->symbol != R_SYMBOL_ID_INVALID ? instruction->symbol : emitter->symbol_id;
    const RSemanticSymbol *function =
        ((symbol == R_SYMBOL_ID_INVALID) ||
         ((size_t)symbol > emitter->frontend->semantic_symbol_count))
            ? NULL
            : &emitter->frontend->semantic_symbols[(size_t)symbol - 1U];
    LLVMTypeRef counter_type = r_llvm_int(emitter, 64U);
    LLVMValueRef counter;
    LLVMValueRef count;
    char name[64];

    if ((function == NULL) || (function->recursion_depth == 0U)) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    (void)snprintf(
        name, sizeof(name), "r_activations.%" PRIu32, r_llvm_symbol_key(emitter, symbol));
    counter = LLVMGetNamedGlobal(emitter->module, name);
    if (counter == NULL) {
        counter = LLVMAddGlobal(emitter->module, counter_type, name);
        LLVMSetInitializer(counter, r_llvm_u64(emitter, 0U));
        LLVMSetLinkage(counter, LLVMInternalLinkage);
        LLVMSetThreadLocal(counter, 1);
    }
    count = LLVMBuildLoad2(emitter->builder, counter_type, counter, "");
    if (instruction->standard_operation == R_STANDARD_CALL_CORE_RECURSION_LEAVE) {
        (void)LLVMBuildStore(emitter->builder,
                             LLVMBuildSub(emitter->builder, count, r_llvm_u64(emitter, 1U), ""),
                             counter);
        return true;
    }
    {
        const uint64_t depth = instruction->integer_value;
        LLVMValueRef result = r_llvm_std_result(emitter, instruction);
        LLVMBasicBlockRef refused =
            LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        LLVMBasicBlockRef entered =
            LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        LLVMBasicBlockRef done =
            LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        uint32_t payload = 0U;
        uint32_t size = 0U;
        uint32_t align = 0U;
        if ((result == NULL) || (depth != (uint64_t)function->recursion_depth) ||
            !r_llvm_payload_offset(emitter, instruction->type, &payload) ||
            !r_llvm_layout(
                emitter, r_llvm_std_single_effect(emitter, instruction->type), &size, &align) ||
            (size != 8U) || !r_llvm_stack_recursion(emitter, emitter->function, (uint32_t)depth)) {
            return (result == NULL) || (emitter->status != R_FRONTEND_OK)
                       ? false
                       : r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
        (void)LLVMBuildCondBr(
            emitter->builder,
            LLVMBuildICmp(emitter->builder, LLVMIntUGE, count, r_llvm_u64(emitter, depth), ""),
            refused,
            entered);
        LLVMPositionBuilderAtEnd(emitter->builder, refused);
        /* core::recursion_error has the one field `usize depth`. */
        (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 1U), result);
        (void)LLVMBuildStore(emitter->builder,
                             r_llvm_u64(emitter, depth),
                             r_llvm_byte_offset(emitter, result, payload));
        (void)LLVMBuildBr(emitter->builder, done);
        LLVMPositionBuilderAtEnd(emitter->builder, entered);
        (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 0U), result);
        (void)LLVMBuildStore(emitter->builder,
                             LLVMBuildAdd(emitter->builder, count, r_llvm_u64(emitter, 1U), ""),
                             counter);
        (void)LLVMBuildBr(emitter->builder, done);
        LLVMPositionBuilderAtEnd(emitter->builder, done);
        r_llvm_std_initialized(emitter, instruction);
        return true;
    }
}

/* The synchronous operations of std.process and std.signal (r_c17_emit_async_process_simple_call,
   Library R-SLIB-SIGNAL-0001, R-SLIB-SIGNAL-0003): the checked ones answer {status, value, error},
   a taken stdio handle is an option {has_value, value}, the others are direct. */
static bool r_llvm_library_process(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RStandardCallOperation operation = instruction->standard_operation;
    LLVMValueRef arguments[8];
    unsigned count = 0U;

    switch (operation) {
    case R_STANDARD_CALL_PROCESS_COMMAND_CREATE:
    case R_STANDARD_CALL_SIGNAL_LISTEN: {
        const bool listen = operation == R_STANDARD_CALL_SIGNAL_LISTEN;
        arguments[0] = r_llvm_std_allocator(emitter);
        return (arguments[0] != NULL) &&
               r_llvm_library_arguments(emitter, instruction, arguments, 1U, &count) &&
               r_llvm_library_status(
                   emitter,
                   instruction,
                   listen ? "r_std_signal_listen" : "r_std_process_command_create",
                   listen ? "RStdSignalListenerResult" : "RStdProcessCommandResult",
                   "R_STD_PROCESS_CALL_SUCCESS",
                   "R_STD_PROCESS_CALL_ERROR",
                   arguments,
                   count);
    }
    case R_STANDARD_CALL_PROCESS_ARG:
    case R_STANDARD_CALL_PROCESS_ENVIRONMENT:
    case R_STANDARD_CALL_PROCESS_REMOVE_ENVIRONMENT:
    case R_STANDARD_CALL_PROCESS_WORKING_DIRECTORY:
    case R_STANDARD_CALL_SIGNAL_RAISE:
        return r_llvm_library_arguments(emitter, instruction, arguments, 0U, &count) &&
               r_llvm_library_status(emitter,
                                     instruction,
                                     operation == R_STANDARD_CALL_PROCESS_ARG ? "r_std_process_arg"
                                     : operation == R_STANDARD_CALL_PROCESS_ENVIRONMENT
                                         ? "r_std_process_environment"
                                     : operation == R_STANDARD_CALL_PROCESS_REMOVE_ENVIRONMENT
                                         ? "r_std_process_remove_environment"
                                     : operation == R_STANDARD_CALL_PROCESS_WORKING_DIRECTORY
                                         ? "r_std_process_working_directory"
                                         : "r_std_signal_raise",
                                     "RStdProcessVoidResult",
                                     "R_STD_PROCESS_CALL_SUCCESS",
                                     "R_STD_PROCESS_CALL_ERROR",
                                     arguments,
                                     count);
    case R_STANDARD_CALL_PROCESS_CLEAR_ENVIRONMENT:
        return r_llvm_library_operands_direct(
            emitter, instruction, "r_std_process_clear_environment");
    case R_STANDARD_CALL_PROCESS_SET_STDIO:
        return r_llvm_library_operands_direct(emitter, instruction, "r_std_process_set_stdio");
    case R_STANDARD_CALL_PROCESS_ID:
        return r_llvm_library_operands_direct(emitter, instruction, "r_std_process_id");
    case R_STANDARD_CALL_PROCESS_TAKE_STDIN:
    case R_STANDARD_CALL_PROCESS_TAKE_STDOUT:
    case R_STANDARD_CALL_PROCESS_TAKE_STDERR: {
        const bool input = operation == R_STANDARD_CALL_PROCESS_TAKE_STDIN;
        const char *type_name = input ? "RStdProcessOutputOption" : "RStdProcessInputOption";
        const RSemanticType *option =
            r_llvm_type(emitter, r_llvm_value_type(emitter, instruction->type));
        LLVMValueRef native = r_llvm_std_structure(emitter, type_name);
        LLVMValueRef result;
        LLVMValueRef present;
        uint32_t payload = 0U;
        if ((option == NULL) || (native == NULL) ||
            !r_llvm_library_arguments(emitter, instruction, arguments, 0U, &count) ||
            (r_llvm_call_runtime(emitter,
                                 input ? "r_std_process_take_stdin"
                                 : operation == R_STANDARD_CALL_PROCESS_TAKE_STDOUT
                                     ? "r_std_process_take_stdout"
                                     : "r_std_process_take_stderr",
                                 arguments,
                                 count,
                                 native) == NULL) ||
            !r_llvm_payload_offset(emitter, instruction->type, &payload)) {
            return false;
        }
        result = r_llvm_std_result(emitter, instruction);
        if (result == NULL) {
            return false;
        }
        present =
            LLVMBuildICmp(emitter->builder,
                          LLVMIntNE,
                          LLVMBuildLoad2(emitter->builder,
                                         r_llvm_int(emitter, 8U),
                                         r_llvm_std_member(emitter, native, type_name, "has_value"),
                                         ""),
                          LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                          "");
        (void)LLVMBuildStore(emitter->builder,
                             LLVMBuildZExt(emitter->builder, present, r_llvm_int(emitter, 32U), ""),
                             result);
        /* The value of an absent handle is zero, which the payload of none ignores. */
        if (!r_llvm_std_copy(emitter,
                             option->base,
                             r_llvm_byte_offset(emitter, result, payload),
                             r_llvm_std_member(emitter, native, type_name, "value"))) {
            return false;
        }
        r_llvm_std_initialized(emitter, instruction);
        return true;
    }
    default:
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
}

/* The synchronous operations of std.net (R-SLIB-NET-0006, 0012..0016): address observations and
   socket options with {is_ok, value, error} results, and the text form of IP addresses. */
static bool r_llvm_library_net(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RStandardCallOperation operation = instruction->standard_operation;
    const RStandardNetOperationDescriptor *descriptor = r_standard_net_operation(operation);
    LLVMValueRef arguments[2];
    char name[96];

    switch (operation) {
    case R_STANDARD_CALL_NET_PARSE_IP:
        arguments[0] = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 0U));
        return (arguments[0] != NULL) && r_llvm_library_status(emitter,
                                                               instruction,
                                                               "r_std_net_parse_ip",
                                                               "RStdNetIpAddressResult",
                                                               "R_STD_NET_CALL_SUCCESS",
                                                               "R_STD_NET_CALL_ERROR",
                                                               arguments,
                                                               1U);
    case R_STANDARD_CALL_NET_FORMAT_IP:
        arguments[0] = r_llvm_std_allocator(emitter);
        arguments[1] = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 0U));
        return (arguments[0] != NULL) && (arguments[1] != NULL) &&
               r_llvm_library_status(emitter,
                                     instruction,
                                     "r_std_net_format_ip",
                                     "RStdNetStringResult",
                                     "R_STD_NET_CALL_SUCCESS",
                                     "R_STD_NET_CALL_ERROR",
                                     arguments,
                                     2U);
    case R_STANDARD_CALL_NET_TCP_LOCAL_ADDRESS:
        return r_llvm_library_checked(
            emitter, instruction, "r_std_net_tcp_local_address", "RStdNetSocketAddressResult");
    default:
        break;
    }
    if (descriptor == NULL) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    (void)snprintf(name, sizeof(name), "r_std_net_%s", descriptor->name);
    if ((operation >= R_STANDARD_CALL_NET_TCP_LISTENER_LOCAL_ADDRESS) &&
        (operation <= R_STANDARD_CALL_NET_UDP_LOCAL_ADDRESS)) {
        return r_llvm_library_checked(emitter, instruction, name, "RStdNetSocketAddressResult");
    }
    return r_llvm_library_checked(
        emitter,
        instruction,
        name,
        operation == R_STANDARD_CALL_NET_TCP_GET_OPTIONS         ? "RStdNetTcpOptionsResult"
        : operation == R_STANDARD_CALL_NET_UDP_GET_OPTIONS       ? "RStdNetUdpOptionsResult"
        : operation == R_STANDARD_CALL_NET_UNIX_PEER_CREDENTIALS ? "RStdNetPeerCredentialsResult"
                                                                 : "RStdNetOptionResult");
}

/* std.env (R-SLIB-ENV): the hosted allocator first, then the str operands; get yields an option
   of the value inside the carrier. */
static bool r_llvm_library_env(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RStandardCallOperation operation = instruction->standard_operation;
    const char *type_name = operation == R_STANDARD_CALL_ENV_ARGUMENTS   ? "RStdEnvArgumentsResult"
                            : operation == R_STANDARD_CALL_ENV_VARIABLES ? "RStdEnvVariablesResult"
                            : operation == R_STANDARD_CALL_ENV_GET       ? "RStdEnvGetResult"
                                                                         : "RStdEnvVoidResult";
    const char *name = operation == R_STANDARD_CALL_ENV_ARGUMENTS   ? "r_std_env_arguments"
                       : operation == R_STANDARD_CALL_ENV_VARIABLES ? "r_std_env_variables"
                       : operation == R_STANDARD_CALL_ENV_GET       ? "r_std_env_get"
                       : operation == R_STANDARD_CALL_ENV_SET       ? "r_std_env_set"
                                                                    : "r_std_env_remove";
    LLVMValueRef arguments[4];
    unsigned count = 0U;
    uint32_t index;

    arguments[count++] = r_llvm_std_allocator(emitter);
    if (operation == R_STANDARD_CALL_ENV_VARIABLES) {
        arguments[count++] = r_llvm_u64(emitter, 0U);
    }
    for (index = 0U; index < instruction->operand_count; ++index) {
        arguments[count++] = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, index));
    }
    for (index = 0U; index < count; ++index) {
        if (arguments[index] == NULL) {
            return false;
        }
    }
    if (operation != R_STANDARD_CALL_ENV_GET) {
        return r_llvm_library_status(emitter,
                                     instruction,
                                     name,
                                     type_name,
                                     "R_STD_ENV_CALL_SUCCESS",
                                     "R_STD_ENV_CALL_ERROR",
                                     arguments,
                                     count);
    }
    {
        /* get: o<std.string::string> in the carrier, some when has_value. */
        const RSemanticType *carrier =
            r_llvm_type(emitter, r_llvm_value_type(emitter, instruction->type));
        LLVMValueRef native = r_llvm_std_structure(emitter, type_name);
        LLVMValueRef result;
        LLVMValueRef option;
        LLVMValueRef choice;
        LLVMBasicBlockRef succeeded;
        LLVMBasicBlockRef present;
        LLVMBasicBlockRef failed;
        LLVMBasicBlockRef invalid;
        LLVMBasicBlockRef done;
        int64_t success_status = 0;
        int64_t error_status = 0;
        uint32_t payload = 0U;
        uint32_t option_payload = 0U;
        if ((carrier == NULL) || (native == NULL) ||
            !r_llvm_runtime_constant(emitter, "R_STD_ENV_CALL_SUCCESS", &success_status) ||
            !r_llvm_runtime_constant(emitter, "R_STD_ENV_CALL_ERROR", &error_status) ||
            !r_llvm_payload_offset(emitter, instruction->type, &payload) ||
            !r_llvm_payload_offset(emitter, carrier->base, &option_payload) ||
            (r_llvm_call_runtime(emitter, name, arguments, count, native) == NULL)) {
            return false;
        }
        result = r_llvm_std_result(emitter, instruction);
        if (result == NULL) {
            return false;
        }
        option = r_llvm_byte_offset(emitter, result, payload);
        succeeded = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        present = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        failed = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        invalid = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        done = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        choice =
            LLVMBuildSwitch(emitter->builder,
                            LLVMBuildLoad2(emitter->builder,
                                           r_llvm_int(emitter, 32U),
                                           r_llvm_std_member(emitter, native, type_name, "status"),
                                           ""),
                            invalid,
                            2U);
        LLVMAddCase(choice, r_llvm_u32(emitter, (uint64_t)success_status), succeeded);
        LLVMAddCase(choice, r_llvm_u32(emitter, (uint64_t)error_status), failed);
        LLVMPositionBuilderAtEnd(emitter->builder, succeeded);
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
            present,
            done);
        LLVMPositionBuilderAtEnd(emitter->builder, present);
        (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 1U), option);
        {
            const RSemanticType *option_type =
                r_llvm_type(emitter, r_llvm_value_type(emitter, carrier->base));
            if ((option_type == NULL) ||
                !r_llvm_std_copy(emitter,
                                 option_type->base,
                                 r_llvm_byte_offset(emitter, option, option_payload),
                                 r_llvm_std_member(emitter, native, type_name, "value"))) {
                return false;
            }
        }
        (void)LLVMBuildBr(emitter->builder, done);
        LLVMPositionBuilderAtEnd(emitter->builder, failed);
        (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 1U), result);
        if (!r_llvm_std_copy(emitter,
                             r_llvm_std_single_effect(emitter, instruction->type),
                             option,
                             r_llvm_std_member(emitter, native, type_name, "error"))) {
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
        r_llvm_std_initialized(emitter, instruction);
        return true;
    }
}

/* The fixed-size operations of std.bytes on byte views (r_c17_emit_async_bytes_fixed_call): the
   views of R and of the library share their layout. */
static bool r_llvm_library_bytes(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    LLVMValueRef arguments[4];
    uint32_t index;

    switch (instruction->standard_operation) {
    case R_STANDARD_CALL_BYTES_COPY:
        return r_llvm_library_checked(
            emitter, instruction, "r_std_bytes_copy", "RStdBytesSizeResult");
    case R_STANDARD_CALL_BYTES_COPY_WITHIN: {
        LLVMValueRef native = r_llvm_std_structure(emitter, "RStdBytesSizeResult");
        LLVMValueRef is_ok;
        arguments[0] = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 0U));
        for (index = 1U; index < 4U; ++index) {
            arguments[index] =
                r_llvm_std_size(emitter, r_llvm_std_operand(emitter, instruction, index));
        }
        for (index = 0U; index < 4U; ++index) {
            if (arguments[index] == NULL) {
                return false;
            }
        }
        if ((native == NULL) ||
            (r_llvm_call_runtime(emitter, "r_std_bytes_copy_within", arguments, 4U, native) ==
             NULL)) {
            return false;
        }
        is_ok = r_llvm_std_member(emitter, native, "RStdBytesSizeResult", "is_ok");
        return (is_ok != NULL) &&
               r_llvm_std_carrier(
                   emitter,
                   instruction,
                   LLVMBuildICmp(
                       emitter->builder,
                       LLVMIntNE,
                       LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 8U), is_ok, ""),
                       LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                       ""),
                   r_llvm_std_member(emitter, native, "RStdBytesSizeResult", "value"),
                   r_llvm_std_member(emitter, native, "RStdBytesSizeResult", "error"));
    }
    case R_STANDARD_CALL_BYTES_FILL:
        return r_llvm_library_operands_direct(emitter, instruction, "r_std_bytes_fill");
    case R_STANDARD_CALL_BYTES_FIND:
    case R_STANDARD_CALL_BYTES_FIND_SLICE: {
        LLVMValueRef native = r_llvm_std_structure(emitter, "RStdBytesIndexOption");
        unsigned count = 0U;
        if ((native == NULL) ||
            !r_llvm_library_arguments(emitter, instruction, arguments, 0U, &count) ||
            (r_llvm_call_runtime(emitter,
                                 instruction->standard_operation == R_STANDARD_CALL_BYTES_FIND
                                     ? "r_std_bytes_find"
                                     : "r_std_bytes_find_slice",
                                 arguments,
                                 count,
                                 native) == NULL)) {
            return false;
        }
        return r_llvm_std_option_from_native(emitter, instruction, native, "RStdBytesIndexOption");
    }
    case R_STANDARD_CALL_BYTES_STARTS_WITH:
        return r_llvm_library_operands_direct(emitter, instruction, "r_std_bytes_starts_with");
    default:
        return r_llvm_library_operands_direct(emitter, instruction, "r_std_bytes_ends_with");
    }
}

/* std.alloc::try_new and into_value (r_c17_emit_async_alloc_container_call): try_new moves the
   value into a new owner, or hands it back inside new_error<T> with the reason; into_value moves it
   out of its owner. */
static bool r_llvm_library_alloc(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RMirValueId operand = r_llvm_std_operand(emitter, instruction, 0U);
    LLVMValueRef value = r_llvm_value(emitter, operand);
    LLVMValueRef result;
    LLVMValueRef arguments[3];

    if (value == NULL) {
        return false;
    }
    if (instruction->standard_operation == R_STANDARD_CALL_ALLOC_INTO_VALUE) {
        /* A scalar T is an SSA value: the entry writes it to a temporary it is loaded from. */
        const bool scalar = (r_llvm_scalar_type(emitter, instruction->type) != NULL) &&
                            !r_llvm_type_requires_drop(emitter, instruction->type);
        uint32_t size = 0U;
        uint32_t align = 0U;
        LLVMValueRef status;
        if (scalar && !r_llvm_layout(emitter, instruction->type, &size, &align)) {
            return false;
        }
        result = scalar ? r_llvm_entry_alloca(emitter, size, align, "")
                        : r_llvm_std_result(emitter, instruction);
        if (result == NULL) {
            return false;
        }
        arguments[0] = value;
        arguments[1] = result;
        status = r_llvm_call_runtime(emitter, "r_std_alloc_into_value", arguments, 2U, NULL);
        if ((status == NULL) || !r_llvm_check(emitter,
                                              LLVMBuildICmp(emitter->builder,
                                                            LLVMIntNE,
                                                            status,
                                                            LLVMConstInt(LLVMTypeOf(status), 0U, 0),
                                                            ""),
                                              "R_RUNTIME_PANIC_CONTRACT_VIOLATION",
                                              instruction->span,
                                              R_MIR_BLOCK_ID_INVALID)) {
            return false;
        }
        r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, operand), false);
        if (scalar) {
            return r_llvm_set_value(emitter,
                                    instruction->result,
                                    r_llvm_load_scalar(emitter, instruction->type, result));
        }
        r_llvm_std_initialized(emitter, instruction);
        return true;
    }
    result = r_llvm_std_result(emitter, instruction);
    if (result == NULL) {
        return false;
    }
    {
        const RTypeId error_type = r_llvm_std_single_effect(emitter, instruction->type);
        const RTypeId type = instruction->runtime_type;
        LLVMValueRef native = r_llvm_std_structure(emitter, "RStdAllocTryNewResult");
        LLVMValueRef staged = r_llvm_std_stage(emitter, operand, type);
        LLVMValueRef ok;
        LLVMBasicBlockRef created;
        LLVMBasicBlockRef refused;
        LLVMBasicBlockRef done;
        RLlvmRecoveringMembers members;
        uint32_t payload = 0U;
        uint32_t own_size = 0U;
        uint32_t own_align = 0U;
        uint32_t reason_size = 0U;
        uint32_t reason_align = 0U;
        if ((error_type == R_TYPE_ID_INVALID) || (native == NULL) || (staged == NULL) ||
            !r_llvm_recovering_members(emitter, error_type, &members) ||
            !r_llvm_payload_offset(emitter, instruction->type, &payload) ||
            !r_llvm_runtime_layout(emitter, "RRuntimeOwn", &own_size, &own_align) ||
            !r_llvm_runtime_layout(emitter, "RStdAllocError", &reason_size, &reason_align)) {
            return error_type == R_TYPE_ID_INVALID ? r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR)
                                                   : false;
        }
        arguments[0] = r_llvm_std_allocator(emitter);
        arguments[1] = r_llvm_type_info(emitter, type);
        arguments[2] = staged;
        if ((arguments[0] == NULL) || (arguments[1] == NULL) ||
            (r_llvm_call_runtime(emitter, "r_std_alloc_try_new", arguments, 3U, native) == NULL)) {
            return false;
        }
        ok = r_llvm_std_status_is(
            emitter, native, "RStdAllocTryNewResult", "status", "R_STD_ALLOC_CALL_SUCCESS");
        if (ok == NULL) {
            return false;
        }
        created = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        refused = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        done = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        (void)LLVMBuildCondBr(emitter->builder, ok, created, refused);
        LLVMPositionBuilderAtEnd(emitter->builder, created);
        (void)LLVMBuildMemCpy(emitter->builder,
                              r_llvm_byte_offset(emitter, result, payload),
                              own_align,
                              r_llvm_std_member(emitter, native, "RStdAllocTryNewResult", "object"),
                              own_align,
                              r_llvm_u64(emitter, own_size));
        (void)LLVMBuildBr(emitter->builder, done);
        LLVMPositionBuilderAtEnd(emitter->builder, refused);
        (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 1U), result);
        (void)LLVMBuildMemCpy(emitter->builder,
                              r_llvm_byte_offset(emitter, result, payload),
                              reason_align,
                              r_llvm_std_member(emitter, native, "RStdAllocTryNewResult", "error"),
                              reason_align,
                              r_llvm_u64(emitter, reason_size));
        if (!r_llvm_std_copy(emitter,
                             type,
                             r_llvm_byte_offset(emitter, result, payload + members.value_offset),
                             staged)) {
            return false;
        }
        (void)LLVMBuildBr(emitter->builder, done);
        LLVMPositionBuilderAtEnd(emitter->builder, done);
        /* The value is in the owner or in the error. */
        r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, operand), false);
        r_llvm_std_initialized(emitter, instruction);
        return true;
    }
}

/* std.math (R-SLIB-MATH): a checked operation reports its domain error in the carrier. */
static bool r_llvm_library_math(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    static const struct {
        const char *result_type;
        const char *native_type;
    } results[] = {
        {"f32", "RStdMathF32Result"},
        {"f64", "RStdMathF64Result"},
        {"c_float", "RStdMathCFloatResult"},
        {"c_double", "RStdMathCDoubleResult"},
        {"c_long_double", "RStdMathCLongDoubleResult"},
        {"complex_f32", "RStdMathComplexF32Result"},
        {"complex_f64", "RStdMathComplexF64Result"},
    };
    const RStandardMathOperationDescriptor *descriptor =
        r_standard_math_operation(instruction->integer_value);
    LLVMValueRef arguments[8];
    unsigned count = 0U;
    size_t index;

    if (descriptor == NULL) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    if (!descriptor->checked) {
        return r_llvm_library_operands_direct(emitter, instruction, descriptor->c_symbol);
    }
    if (!r_llvm_library_arguments(emitter, instruction, arguments, 0U, &count)) {
        return false;
    }
    for (index = 0U; index < sizeof(results) / sizeof(results[0]); ++index) {
        if (strcmp(results[index].result_type, descriptor->result_type) == 0) {
            return r_llvm_library_status(emitter,
                                         instruction,
                                         descriptor->c_symbol,
                                         results[index].native_type,
                                         "R_STD_MATH_CALL_SUCCESS",
                                         "R_STD_MATH_CALL_ERROR",
                                         arguments,
                                         count);
        }
    }
    return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
}

/* R-STMT-0019: entering a deadline block narrows the deadline of the executing task by the
   optional instant and yields the deadline it replaced; leaving restores that one. */
static bool r_llvm_library_deadline_block(RLlvmEmitter *emitter,
                                          const RMirInstruction *instruction) {
    const RMirValueId operand = r_llvm_std_operand(emitter, instruction, 0U);
    const bool enter = instruction->standard_operation == R_STANDARD_CALL_ASYNC_DEADLINE_ENTER;
    LLVMValueRef option = r_llvm_value(emitter, operand);
    LLVMValueRef deadline = r_llvm_std_structure(emitter, "RRuntimeTaskDeadline");
    LLVMValueRef previous;
    uint32_t payload = 0U;
    uint32_t active = 0U;
    uint32_t seconds = 0U;
    uint32_t nanoseconds = 0U;
    uint32_t instant_seconds = 0U;
    uint32_t instant_nanoseconds = 0U;

    if ((option == NULL) || (deadline == NULL) ||
        !r_llvm_payload_offset(emitter, r_llvm_std_operand_type(emitter, operand), &payload) ||
        !r_llvm_runtime_field(emitter, "RRuntimeTaskDeadline", "active", &active, NULL) ||
        !r_llvm_runtime_field(emitter, "RRuntimeTaskDeadline", "seconds", &seconds, NULL) ||
        !r_llvm_runtime_field(emitter, "RRuntimeTaskDeadline", "nanoseconds", &nanoseconds, NULL) ||
        !r_llvm_runtime_field(
            emitter, "RStdTimeInstant", "storage_seconds", &instant_seconds, NULL) ||
        !r_llvm_runtime_field(
            emitter, "RStdTimeInstant", "storage_nanoseconds", &instant_nanoseconds, NULL)) {
        return false;
    }
    {
        LLVMValueRef some =
            LLVMBuildICmp(emitter->builder,
                          LLVMIntEQ,
                          LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), option, ""),
                          r_llvm_u32(emitter, 1U),
                          "");
        LLVMValueRef instant = r_llvm_byte_offset(emitter, option, payload);
        (void)LLVMBuildStore(emitter->builder,
                             LLVMBuildZExt(emitter->builder, some, r_llvm_int(emitter, 8U), ""),
                             r_llvm_byte_offset(emitter, deadline, active));
        (void)LLVMBuildStore(
            emitter->builder,
            LLVMBuildSelect(emitter->builder,
                            some,
                            LLVMBuildLoad2(emitter->builder,
                                           r_llvm_int(emitter, 64U),
                                           r_llvm_byte_offset(emitter, instant, instant_seconds),
                                           ""),
                            r_llvm_u64(emitter, 0U),
                            ""),
            r_llvm_byte_offset(emitter, deadline, seconds));
        (void)LLVMBuildStore(
            emitter->builder,
            LLVMBuildSelect(
                emitter->builder,
                some,
                LLVMBuildLoad2(emitter->builder,
                               r_llvm_int(emitter, 32U),
                               r_llvm_byte_offset(emitter, instant, instant_nanoseconds),
                               ""),
                r_llvm_u32(emitter, 0U),
                ""),
            r_llvm_byte_offset(emitter, deadline, nanoseconds));
    }
    if (!enter) {
        return r_llvm_call_runtime(emitter, "r_runtime_task_deadline_leave", &deadline, 1U, NULL) !=
               NULL;
    }
    previous = r_llvm_std_structure(emitter, "RRuntimeTaskDeadline");
    if ((previous == NULL) ||
        (r_llvm_call_runtime(emitter, "r_runtime_task_deadline_enter", &deadline, 1U, previous) ==
         NULL)) {
        return false;
    }
    {
        /* The previous deadline as o<std.time::instant>. */
        LLVMValueRef result = r_llvm_std_result(emitter, instruction);
        uint32_t result_payload = 0U;
        LLVMValueRef was =
            LLVMBuildICmp(emitter->builder,
                          LLVMIntNE,
                          LLVMBuildLoad2(emitter->builder,
                                         r_llvm_int(emitter, 8U),
                                         r_llvm_byte_offset(emitter, previous, active),
                                         ""),
                          LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                          "");
        LLVMValueRef instant;
        if ((result == NULL) ||
            !r_llvm_payload_offset(emitter, instruction->type, &result_payload)) {
            return false;
        }
        instant = r_llvm_byte_offset(emitter, result, result_payload);
        (void)LLVMBuildStore(emitter->builder,
                             LLVMBuildZExt(emitter->builder, was, r_llvm_int(emitter, 32U), ""),
                             result);
        (void)LLVMBuildStore(emitter->builder,
                             LLVMBuildLoad2(emitter->builder,
                                            r_llvm_int(emitter, 64U),
                                            r_llvm_byte_offset(emitter, previous, seconds),
                                            ""),
                             r_llvm_byte_offset(emitter, instant, instant_seconds));
        (void)LLVMBuildStore(emitter->builder,
                             LLVMBuildLoad2(emitter->builder,
                                            r_llvm_int(emitter, 32U),
                                            r_llvm_byte_offset(emitter, previous, nanoseconds),
                                            ""),
                             r_llvm_byte_offset(emitter, instant, instant_nanoseconds));
        r_llvm_std_initialized(emitter, instruction);
    }
    return true;
}

/* R-STMT-0020: entering a budget block passes the limits of a std.alloc::limits value, a present
   option as a limit, and yields the previous budget of the task as a usize; leaving restores it. */
static bool r_llvm_library_budget_block(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RMirValueId operand = r_llvm_std_operand(emitter, instruction, 0U);
    LLVMValueRef value = r_llvm_value(emitter, operand);
    const RSemanticField *fields[2] = {NULL, NULL};
    LLVMValueRef arguments[4];
    LLVMValueRef previous;
    uint32_t index;

    if (value == NULL) {
        return false;
    }
    if (instruction->standard_operation == R_STANDARD_CALL_ASYNC_BUDGET_LEAVE) {
        LLVMValueRef budget = LLVMBuildIntToPtr(
            emitter->builder,
            LLVMBuildIntCast2(emitter->builder, value, r_llvm_int(emitter, 64U), 0, ""),
            r_llvm_pointer(emitter),
            "");
        return r_llvm_call_runtime(emitter, "r_runtime_task_budget_leave", &budget, 1U, NULL) !=
               NULL;
    }
    if (!r_semantic_budget_limits_fields(
            emitter->frontend,
            r_llvm_value_type(emitter, r_llvm_std_operand_type(emitter, operand)),
            &fields[0],
            &fields[1])) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    for (index = 0U; index < 2U; ++index) {
        const uint32_t field_id =
            (uint32_t)(fields[index] - emitter->frontend->semantic_fields) + 1U;
        uint32_t offset = 0U;
        uint32_t payload = 0U;
        LLVMValueRef option;
        if (!r_llvm_field_offset(emitter, field_id, &offset) ||
            !r_llvm_payload_offset(emitter, fields[index]->type, &payload)) {
            return false;
        }
        option = r_llvm_byte_offset(emitter, value, offset);
        arguments[2U * index] =
            LLVMBuildICmp(emitter->builder,
                          LLVMIntEQ,
                          LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), option, ""),
                          r_llvm_u32(emitter, 1U),
                          "");
        arguments[2U * index + 1U] = LLVMBuildLoad2(emitter->builder,
                                                    r_llvm_int(emitter, 64U),
                                                    r_llvm_byte_offset(emitter, option, payload),
                                                    "");
    }
    previous = r_llvm_call_runtime(emitter, "r_runtime_task_budget_enter", arguments, 4U, NULL);
    return (previous != NULL) &&
           r_llvm_set_value(
               emitter,
               instruction->result,
               LLVMBuildPtrToInt(emitter->builder, previous, r_llvm_int(emitter, 64U), ""));
}

/* std.format::builder (R-SLIB-FMT): creation, views, clearing, finishing and appends of text,
   characters and floats. */
static bool r_llvm_library_format(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RStandardCallOperation operation = instruction->standard_operation;
    LLVMValueRef arguments[3];
    unsigned count = 0U;

    switch (operation) {
    case R_STANDARD_CALL_FORMAT_CREATE: {
        /* A builder without capacity cannot fail to be created. */
        LLVMValueRef native = r_llvm_std_structure(emitter, "RStdFormatBuilderResult");
        LLVMValueRef result;
        arguments[0] = r_llvm_std_allocator(emitter);
        if ((native == NULL) || (arguments[0] == NULL) ||
            (r_llvm_call_runtime(emitter, "r_std_format_create", arguments, 1U, native) == NULL) ||
            !r_llvm_std_require_status(emitter,
                                       instruction,
                                       native,
                                       "RStdFormatBuilderResult",
                                       "R_STD_FORMAT_CALL_SUCCESS",
                                       false)) {
            return false;
        }
        result = r_llvm_std_result(emitter, instruction);
        if ((result == NULL) ||
            !r_llvm_std_copy(
                emitter,
                instruction->type,
                result,
                r_llvm_std_member(emitter, native, "RStdFormatBuilderResult", "value"))) {
            return false;
        }
        r_llvm_std_initialized(emitter, instruction);
        return true;
    }
    case R_STANDARD_CALL_FORMAT_WITH_CAPACITY:
        arguments[count++] = r_llvm_std_allocator(emitter);
        arguments[count++] = r_llvm_std_size(emitter, r_llvm_std_operand(emitter, instruction, 0U));
        return (arguments[0] != NULL) && (arguments[1] != NULL) &&
               r_llvm_library_status(emitter,
                                     instruction,
                                     "r_std_format_with_capacity",
                                     "RStdFormatBuilderResult",
                                     "R_STD_FORMAT_CALL_SUCCESS",
                                     "R_STD_FORMAT_CALL_ERROR",
                                     arguments,
                                     count);
    case R_STANDARD_CALL_FORMAT_AS_STR:
        return r_llvm_library_operands_direct(emitter, instruction, "r_std_format_as_str");
    case R_STANDARD_CALL_FORMAT_CLEAR:
        return r_llvm_library_operands_direct(emitter, instruction, "r_std_format_clear");
    case R_STANDARD_CALL_FORMAT_APPEND_INTEGER: {
        /* r_std_format_append_suffix with the sign and the magnitude of the value
           (RStdConvertParsedInteger) and the radix. */
        const RMirValueId operand = r_llvm_std_operand(emitter, instruction, 1U);
        const RMirInstruction *definition = r_llvm_definition(emitter, operand);
        LLVMValueRef value = r_llvm_value(emitter, operand);
        LLVMValueRef parsed = r_llvm_std_structure(emitter, "RStdConvertParsedInteger");
        LLVMValueRef wide;
        LLVMValueRef negative;
        unsigned bits = 0U;
        bool is_signed = false;
        arguments[0] = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 0U));
        arguments[2] = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 2U));
        if ((definition == NULL) || (value == NULL) || (parsed == NULL) || (arguments[0] == NULL) ||
            (arguments[2] == NULL)) {
            return false;
        }
        /* c_bool (tag 23) appends 0 or 1. */
        if ((r_llvm_value_kind(emitter, definition->type) != R_SEMANTIC_TYPE_BOOL) &&
            !r_llvm_kind_integer(r_llvm_value_kind(emitter, definition->type), &bits, &is_signed) &&
            !r_llvm_enum_integer(emitter, definition->type, &bits, &is_signed)) {
            return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
        wide = LLVMBuildIntCast2(emitter->builder, value, r_llvm_int(emitter, 64U), is_signed, "");
        negative =
            is_signed
                ? LLVMBuildICmp(emitter->builder, LLVMIntSLT, wide, r_llvm_u64(emitter, 0U), "")
                : LLVMConstInt(r_llvm_int(emitter, 1U), 0U, 0);
        (void)LLVMBuildStore(
            emitter->builder,
            LLVMBuildZExt(emitter->builder, negative, r_llvm_int(emitter, 8U), ""),
            r_llvm_std_member(emitter, parsed, "RStdConvertParsedInteger", "negative"));
        (void)LLVMBuildStore(
            emitter->builder,
            LLVMBuildSelect(emitter->builder,
                            negative,
                            LLVMBuildSub(emitter->builder, r_llvm_u64(emitter, 0U), wide, ""),
                            wide,
                            ""),
            r_llvm_std_member(emitter, parsed, "RStdConvertParsedInteger", "magnitude"));
        arguments[1] = parsed;
        arguments[2] =
            LLVMBuildIntCast2(emitter->builder, arguments[2], r_llvm_int(emitter, 32U), 0, "");
        return r_llvm_library_status(emitter,
                                     instruction,
                                     "r_std_format_append_suffix",
                                     "RStdFormatAppendResult",
                                     "R_STD_FORMAT_CALL_SUCCESS",
                                     "R_STD_FORMAT_CALL_ERROR",
                                     arguments,
                                     3U);
    }
    case R_STANDARD_CALL_FORMAT_FINISH: {
        LLVMValueRef flag = NULL;
        if (!r_llvm_library_consumed(
                emitter, r_llvm_std_operand(emitter, instruction, 0U), &arguments[0], &flag) ||
            !r_llvm_library_direct(emitter, instruction, "r_std_format_finish", arguments, 1U)) {
            return false;
        }
        r_llvm_set_flag(emitter, flag, false);
        return true;
    }
    default: {
        static const char *const names[] = {"r_std_format_append_str",
                                            "r_std_format_append_char",
                                            "r_std_format_append_f32",
                                            "r_std_format_append_f64",
                                            "r_std_format_append_c_float",
                                            "r_std_format_append_c_double",
                                            "r_std_format_append_c_long_double"};
        const bool allocating = (operation == R_STANDARD_CALL_FORMAT_APPEND_STR) ||
                                (operation == R_STANDARD_CALL_FORMAT_APPEND_CHAR);
        if (!r_llvm_library_arguments(emitter, instruction, arguments, 0U, &count)) {
            return false;
        }
        return r_llvm_library_status(emitter,
                                     instruction,
                                     names[operation - R_STANDARD_CALL_FORMAT_APPEND_STR],
                                     allocating ? "RStdFormatAllocResult"
                                                : "RStdFormatAppendResult",
                                     "R_STD_FORMAT_CALL_SUCCESS",
                                     "R_STD_FORMAT_CALL_ERROR",
                                     arguments,
                                     count);
    }
    }
}

/* std.thread, std.error, std.secret, std.random and std.async operations that are one call. */
static bool r_llvm_library_misc(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    LLVMValueRef arguments[2];
    LLVMValueRef flag = NULL;

    switch (instruction->standard_operation) {
    case R_STANDARD_CALL_THREAD_CURRENT:
        return r_llvm_library_operands_direct(emitter, instruction, "r_std_thread_current");
    case R_STANDARD_CALL_THREAD_CLONE:
        return r_llvm_library_operands_direct(emitter, instruction, "r_std_thread_clone_thread");
    case R_STANDARD_CALL_THREAD_UNPARK:
        return r_llvm_library_operands_direct(emitter, instruction, "r_std_thread_unpark");
    case R_STANDARD_CALL_THREAD_PARK:
        return r_llvm_library_operands_direct(emitter, instruction, "r_std_thread_park");
    case R_STANDARD_CALL_THREAD_YIELD_NOW:
        return r_llvm_library_operands_direct(emitter, instruction, "r_std_thread_yield_now");
    case R_STANDARD_CALL_THREAD_SLEEP_NANOSECONDS:
        return r_llvm_library_operands_direct(
            emitter, instruction, "r_std_thread_sleep_nanoseconds");
    case R_STANDARD_CALL_THREAD_PANIC_CATEGORY:
        return r_llvm_library_operands_direct(emitter, instruction, "r_std_thread_panic_category");
    case R_STANDARD_CALL_THREAD_PANIC_TEXT:
        return r_llvm_library_operands_direct(emitter, instruction, "r_std_thread_panic_text");
    case R_STANDARD_CALL_ERROR_NAME:
        return r_llvm_library_operands_direct(emitter, instruction, "r_std_error_name");
    case R_STANDARD_CALL_ERROR_DIAGNOSTIC:
        arguments[0] = r_llvm_std_allocator(emitter);
        arguments[1] = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 0U));
        return (arguments[0] != NULL) && (arguments[1] != NULL) &&
               r_llvm_library_status(emitter,
                                     instruction,
                                     "r_std_error_diagnostic",
                                     "RStdErrorDiagnosticResult",
                                     "R_STD_STRING_CALL_SUCCESS",
                                     "R_STD_STRING_CALL_ERROR",
                                     arguments,
                                     2U);
    case R_STANDARD_CALL_SECRET_WITH_LENGTH: {
        LLVMValueRef native = r_llvm_std_structure(emitter, "RStdSecretBufferResult");
        arguments[0] = r_llvm_std_allocator(emitter);
        arguments[1] = r_llvm_std_size(emitter, r_llvm_std_operand(emitter, instruction, 0U));
        return (native != NULL) && (arguments[0] != NULL) && (arguments[1] != NULL) &&
               (r_llvm_call_runtime(emitter, "r_std_secret_with_length", arguments, 2U, native) !=
                NULL) &&
               r_llvm_std_alloc_carrier(emitter,
                                        instruction,
                                        native,
                                        "RStdSecretBufferResult",
                                        "R_STD_ALLOC_CALL_SUCCESS",
                                        "value");
    }
    case R_STANDARD_CALL_SECRET_FROM_BYTES:
        if (!r_llvm_library_consumed(
                emitter, r_llvm_std_operand(emitter, instruction, 0U), &arguments[0], &flag) ||
            !r_llvm_library_direct(
                emitter, instruction, "r_std_secret_from_bytes", arguments, 1U)) {
            return false;
        }
        r_llvm_set_flag(emitter, flag, false);
        return true;
    case R_STANDARD_CALL_SECRET_LEN:
        return r_llvm_library_operands_direct(emitter, instruction, "r_std_secret_len");
    case R_STANDARD_CALL_SECRET_AS_SLICE:
        return r_llvm_library_operands_direct(emitter, instruction, "r_std_secret_as_slice");
    case R_STANDARD_CALL_SECRET_AS_SLICE_MUT:
        return r_llvm_library_operands_direct(emitter, instruction, "r_std_secret_as_slice_mut");
    case R_STANDARD_CALL_SECRET_ZEROIZE:
        /* A mutable byte view and RStdSecretMutSlice share their layout. */
        return r_llvm_library_operands_direct(emitter, instruction, "r_std_secret_zeroize");
    case R_STANDARD_CALL_SECRET_CONSTANT_TIME_EQUAL:
        return r_llvm_library_operands_direct(
            emitter, instruction, "r_std_secret_constant_time_equal");
    case R_STANDARD_CALL_RANDOM_FILL:
        return r_llvm_library_operands_direct(emitter, instruction, "r_std_random_fill");
    case R_STANDARD_CALL_ASYNC_TASK_ID:
        return r_llvm_library_operands_direct(emitter, instruction, "r_std_async_task_id");
    case R_STANDARD_CALL_MATH_OPERATION:
        return r_llvm_library_math(emitter, instruction);
    case R_STANDARD_CALL_ASYNC_DEADLINE_ENTER:
    case R_STANDARD_CALL_ASYNC_DEADLINE_LEAVE:
        return r_llvm_library_deadline_block(emitter, instruction);
    case R_STANDARD_CALL_ASYNC_BUDGET_ENTER:
    case R_STANDARD_CALL_ASYNC_BUDGET_LEAVE:
        return r_llvm_library_budget_block(emitter, instruction);
    default:
        return r_llvm_library_format(emitter, instruction);
    }
}

bool r_llvm_std_library_handles(RStandardCallOperation operation) {
    if ((r_standard_scoped_operation(operation) != NULL) ||
        (r_standard_fs_async_descriptor(operation) != NULL) || r_standard_net_is_async(operation) ||
        (operation == R_STANDARD_CALL_FS_CREATE_DIRECTORY_BENEATH) ||
        (operation == R_STANDARD_CALL_FS_WRITE_FILE_ATOMIC_NO_REPLACE_BENEATH)) {
        return true;
    }
    return (operation == R_STANDARD_CALL_ALLOC_TRY_NEW) ||
           (operation == R_STANDARD_CALL_ALLOC_INTO_VALUE) ||
           ((operation >= R_STANDARD_CALL_BYTES_COPY) &&
            (operation <= R_STANDARD_CALL_BYTES_ENDS_WITH)) ||
           (operation == R_STANDARD_CALL_MATH_ABS_F64) ||
           (operation == R_STANDARD_CALL_MATH_SIN_F64) ||
           ((operation >= R_STANDARD_CALL_ENV_ARGUMENTS) &&
            (operation <= R_STANDARD_CALL_ENV_REMOVE)) ||
           (operation == R_STANDARD_CALL_NET_PARSE_IP) ||
           (operation == R_STANDARD_CALL_NET_FORMAT_IP) ||
           (operation == R_STANDARD_CALL_NET_TCP_LOCAL_ADDRESS) ||
           ((operation >= R_STANDARD_CALL_NET_TCP_LISTENER_LOCAL_ADDRESS) &&
            (operation <= R_STANDARD_CALL_NET_UDP_LOCAL_ADDRESS)) ||
           ((operation >= R_STANDARD_CALL_NET_TCP_GET_OPTIONS) &&
            (operation <= R_STANDARD_CALL_NET_UNIX_PEER_CREDENTIALS)) ||
           ((operation >= R_STANDARD_CALL_TIME_DURATION_FROM_SECONDS) &&
            (operation <= R_STANDARD_CALL_TIME_SLEEP_UNTIL)) ||
           ((operation >= R_STANDARD_CALL_IO_READ) &&
            (operation <= R_STANDARD_CALL_IO_WRITE_SHARED)) ||
           (operation == R_STANDARD_CALL_FS_PATH_CLONE) ||
           (operation == R_STANDARD_CALL_FS_PATH_JOIN) ||
           (operation == R_STANDARD_CALL_FS_PATH_TO_UTF8) ||
           (operation == R_STANDARD_CALL_FS_READ_FILE) ||
           (operation == R_STANDARD_CALL_FS_OPEN_FILE) ||
           (operation == R_STANDARD_CALL_FS_OPEN_DIRECTORY) ||
           (operation == R_STANDARD_CALL_FS_FILE_METADATA) ||
           (operation == R_STANDARD_CALL_ALLOC_BYTES) || (operation == R_STANDARD_CALL_C_TARGET) ||
           (operation == R_STANDARD_CALL_CONVERT_PARSE) ||
           (operation == R_STANDARD_CALL_BITS_READ) ||
           (operation == R_STANDARD_CALL_BITS_ALIGN_BYTE) ||
           (operation == R_STANDARD_CALL_CORE_RECURSION_ENTER) ||
           (operation == R_STANDARD_CALL_CORE_RECURSION_LEAVE) ||
           (operation == R_STANDARD_CALL_CONVERT_CHECKED) ||
           (operation == R_STANDARD_CALL_C_CHECKED) ||
           ((operation >= R_STANDARD_CALL_THREAD_CURRENT) &&
            (operation <= R_STANDARD_CALL_THREAD_PANIC_TEXT)) ||
           (operation == R_STANDARD_CALL_ERROR_NAME) ||
           (operation == R_STANDARD_CALL_ERROR_DIAGNOSTIC) ||
           ((operation >= R_STANDARD_CALL_SECRET_WITH_LENGTH) &&
            (operation <= R_STANDARD_CALL_RANDOM_FILL)) ||
           (operation == R_STANDARD_CALL_ASYNC_TASK_ID) ||
           (operation == R_STANDARD_CALL_MATH_OPERATION) ||
           (operation == R_STANDARD_CALL_ASYNC_DEADLINE_ENTER) ||
           (operation == R_STANDARD_CALL_ASYNC_DEADLINE_LEAVE) ||
           (operation == R_STANDARD_CALL_ASYNC_BUDGET_ENTER) ||
           (operation == R_STANDARD_CALL_ASYNC_BUDGET_LEAVE) ||
           ((operation >= R_STANDARD_CALL_FORMAT_CREATE) &&
            (operation <= R_STANDARD_CALL_FORMAT_APPEND_INTEGER)) ||
           ((operation >= R_STANDARD_CALL_PROCESS_COMMAND_CREATE) &&
            (operation <= R_STANDARD_CALL_PROCESS_ABORT)) ||
           ((operation >= R_STANDARD_CALL_PROCESS_SPAWN) &&
            (operation <= R_STANDARD_CALL_SIGNAL_NEXT)) ||
           (operation == R_STANDARD_CALL_ERROR_ERASURE) ||
           (operation == R_STANDARD_CALL_FS_AS_ERROR) ||
           (operation == R_STANDARD_CALL_IO_AS_ERROR) ||
           (operation == R_STANDARD_CALL_FS_PATH_FROM_UTF8) ||
           (operation == R_STANDARD_CALL_FS_PATH_FROM_UTF8_BYTES) ||
           (operation == R_STANDARD_CALL_FS_PATH_IS_ABSOLUTE) ||
           (operation == R_STANDARD_CALL_IO_STDIN) || (operation == R_STANDARD_CALL_IO_STDOUT) ||
           (operation == R_STANDARD_CALL_IO_STDERR);
}

bool r_llvm_std_library(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    if ((r_standard_scoped_operation(instruction->standard_operation) != NULL) ||
        (r_standard_fs_async_descriptor(instruction->standard_operation) != NULL) ||
        r_standard_net_is_async(instruction->standard_operation) ||
        (instruction->standard_operation == R_STANDARD_CALL_FS_CREATE_DIRECTORY_BENEATH) ||
        (instruction->standard_operation ==
         R_STANDARD_CALL_FS_WRITE_FILE_ATOMIC_NO_REPLACE_BENEATH)) {
        return r_llvm_library_described(emitter, instruction);
    }
    switch (instruction->standard_operation) {
    case R_STANDARD_CALL_ALLOC_BYTES: {
        /* std.alloc::bytes(length, fill): the length is a size_t. */
        LLVMValueRef arguments[3];
        LLVMValueRef native = r_llvm_std_structure(emitter, "RStdAllocBytesResult");
        arguments[0] = r_llvm_std_allocator(emitter);
        arguments[1] = r_llvm_std_size(emitter, r_llvm_std_operand(emitter, instruction, 0U));
        arguments[2] = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 1U));
        return (native != NULL) && (arguments[0] != NULL) && (arguments[1] != NULL) &&
               (arguments[2] != NULL) &&
               (r_llvm_call_runtime(emitter, "r_std_alloc_bytes", arguments, 3U, native) != NULL) &&
               r_llvm_std_alloc_carrier(emitter,
                                        instruction,
                                        native,
                                        "RStdAllocBytesResult",
                                        "R_STD_ALLOC_CALL_SUCCESS",
                                        "value");
    }
    case R_STANDARD_CALL_C_TARGET:
        return r_llvm_library_direct(emitter, instruction, "r_std_c_target", NULL, 0U);
    case R_STANDARD_CALL_NET_PARSE_IP:
    case R_STANDARD_CALL_NET_FORMAT_IP:
    case R_STANDARD_CALL_NET_TCP_LOCAL_ADDRESS:
        return r_llvm_library_net(emitter, instruction);
    case R_STANDARD_CALL_BYTES_COPY:
    case R_STANDARD_CALL_BYTES_COPY_WITHIN:
    case R_STANDARD_CALL_BYTES_FILL:
    case R_STANDARD_CALL_BYTES_FIND:
    case R_STANDARD_CALL_BYTES_FIND_SLICE:
    case R_STANDARD_CALL_BYTES_STARTS_WITH:
    case R_STANDARD_CALL_BYTES_ENDS_WITH:
        return r_llvm_library_bytes(emitter, instruction);
    case R_STANDARD_CALL_MATH_ABS_F64:
        return r_llvm_library_operands_direct(emitter, instruction, "r_std_math_abs_f64");
    case R_STANDARD_CALL_ALLOC_TRY_NEW:
    case R_STANDARD_CALL_ALLOC_INTO_VALUE:
        return r_llvm_library_alloc(emitter, instruction);
    case R_STANDARD_CALL_MATH_SIN_F64: {
        LLVMValueRef argument = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 0U));
        return (argument != NULL) && r_llvm_library_status(emitter,
                                                           instruction,
                                                           "r_std_math_sin_f64",
                                                           "RStdMathF64Result",
                                                           "R_STD_MATH_CALL_SUCCESS",
                                                           "R_STD_MATH_CALL_ERROR",
                                                           &argument,
                                                           1U);
    }
    case R_STANDARD_CALL_ENV_ARGUMENTS:
    case R_STANDARD_CALL_ENV_VARIABLES:
    case R_STANDARD_CALL_ENV_GET:
    case R_STANDARD_CALL_ENV_SET:
    case R_STANDARD_CALL_ENV_REMOVE:
        return r_llvm_library_env(emitter, instruction);
    case R_STANDARD_CALL_CONVERT_PARSE:
        return r_llvm_library_parse(emitter, instruction);
    case R_STANDARD_CALL_BITS_READ:
        /* r_c17_emit_async_bits_call: the input is a byte view, {is_ok, value, error} answers. */
        return r_llvm_library_checked(
            emitter, instruction, "r_std_bits_read", "RStdBitsReadResult");
    case R_STANDARD_CALL_BITS_ALIGN_BYTE:
        return r_llvm_library_operands_direct(emitter, instruction, "r_std_bits_align_byte");
    case R_STANDARD_CALL_CORE_RECURSION_ENTER:
    case R_STANDARD_CALL_CORE_RECURSION_LEAVE:
        return r_llvm_library_recursion(emitter, instruction);
    case R_STANDARD_CALL_CONVERT_CHECKED:
    case R_STANDARD_CALL_C_CHECKED:
        return r_llvm_library_checked_number(emitter, instruction);
    case R_STANDARD_CALL_PROCESS_EXIT:
    case R_STANDARD_CALL_PROCESS_ABORT:
        return r_llvm_library_end_process(emitter, instruction);
    case R_STANDARD_CALL_PROCESS_COMMAND_CREATE:
    case R_STANDARD_CALL_PROCESS_ARG:
    case R_STANDARD_CALL_PROCESS_ENVIRONMENT:
    case R_STANDARD_CALL_PROCESS_REMOVE_ENVIRONMENT:
    case R_STANDARD_CALL_PROCESS_CLEAR_ENVIRONMENT:
    case R_STANDARD_CALL_PROCESS_WORKING_DIRECTORY:
    case R_STANDARD_CALL_PROCESS_SET_STDIO:
    case R_STANDARD_CALL_PROCESS_TAKE_STDIN:
    case R_STANDARD_CALL_PROCESS_TAKE_STDOUT:
    case R_STANDARD_CALL_PROCESS_TAKE_STDERR:
    case R_STANDARD_CALL_PROCESS_ID:
    case R_STANDARD_CALL_SIGNAL_LISTEN:
    case R_STANDARD_CALL_SIGNAL_RAISE:
        return r_llvm_library_process(emitter, instruction);
    case R_STANDARD_CALL_PROCESS_SPAWN:
    case R_STANDARD_CALL_PROCESS_WAIT:
    case R_STANDARD_CALL_PROCESS_TERMINATE:
    case R_STANDARD_CALL_SIGNAL_NEXT: {
        /* r_c17_emit_async_process_start: spawn consumes the command and wait the child once the
           task started; terminate and next borrow theirs. */
        const RStandardCallOperation operation = instruction->standard_operation;
        const bool consumes = (operation == R_STANDARD_CALL_PROCESS_SPAWN) ||
                              (operation == R_STANDARD_CALL_PROCESS_WAIT);
        return r_llvm_library_task(
            emitter,
            instruction,
            operation == R_STANDARD_CALL_PROCESS_SPAWN  ? "r_std_process_spawn"
            : operation == R_STANDARD_CALL_PROCESS_WAIT ? "r_std_process_wait"
            : operation == R_STANDARD_CALL_SIGNAL_NEXT  ? "r_std_signal_next"
                                                        : "r_std_process_terminate",
            "RStdProcessDeadline",
            "RStdProcessTaskStartResult",
            2U,
            consumes ? 0U : UINT32_MAX);
    }
    case R_STANDARD_CALL_ERROR_ERASURE: {
        const RStandardErrorErasureDescriptor *descriptor =
            r_standard_error_erasure(instruction->integer_value);
        if (descriptor == NULL) {
            return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
        if (descriptor->c_symbol == NULL) {
            return r_llvm_fault_portable(emitter, instruction);
        }
        return r_llvm_library_operands_direct(emitter, instruction, descriptor->c_symbol);
    }
    case R_STANDARD_CALL_FS_AS_ERROR:
        return r_llvm_library_operands_direct(emitter, instruction, "r_std_fs_as_error");
    case R_STANDARD_CALL_IO_AS_ERROR:
        return r_llvm_library_operands_direct(emitter, instruction, "r_std_io_as_error");
    case R_STANDARD_CALL_FS_PATH_FROM_UTF8:
        /* A str and a byte slice share the layout of RStdStringView. */
        return r_llvm_library_allocating(emitter,
                                         instruction,
                                         "r_std_fs_path_from_utf8",
                                         "RStdFsPathResult",
                                         "R_STD_FS_CALL_SUCCESS");
    case R_STANDARD_CALL_FS_PATH_FROM_UTF8_BYTES:
        return r_llvm_library_allocating(emitter,
                                         instruction,
                                         "r_std_fs_path_from_utf8_bytes",
                                         "RStdFsPathResult",
                                         "R_STD_FS_CALL_SUCCESS");
    case R_STANDARD_CALL_FS_PATH_IS_ABSOLUTE:
        return r_llvm_library_operands_direct(emitter, instruction, "r_std_fs_path_is_absolute");
    case R_STANDARD_CALL_IO_STDIN:
        return r_llvm_library_direct(emitter, instruction, "r_std_io_stdin", NULL, 0U);
    case R_STANDARD_CALL_IO_STDOUT:
        return r_llvm_library_direct(emitter, instruction, "r_std_io_stdout", NULL, 0U);
    case R_STANDARD_CALL_IO_STDERR:
        return r_llvm_library_direct(emitter, instruction, "r_std_io_stderr", NULL, 0U);
    case R_STANDARD_CALL_IO_READ:
    case R_STANDARD_CALL_IO_WRITE:
    case R_STANDARD_CALL_IO_WRITE_ALL:
    case R_STANDARD_CALL_IO_WRITE_SHARED:
    case R_STANDARD_CALL_IO_CLOSE_INPUT:
    case R_STANDARD_CALL_IO_CLOSE_OUTPUT:
    case R_STANDARD_CALL_IO_FLUSH:
        return r_llvm_library_io(emitter, instruction);
    case R_STANDARD_CALL_FS_PATH_CLONE:
    case R_STANDARD_CALL_FS_PATH_JOIN:
    case R_STANDARD_CALL_FS_PATH_TO_UTF8:
    case R_STANDARD_CALL_FS_READ_FILE:
    case R_STANDARD_CALL_FS_OPEN_FILE:
    case R_STANDARD_CALL_FS_OPEN_DIRECTORY:
    case R_STANDARD_CALL_FS_FILE_METADATA:
        return r_llvm_library_fs(emitter, instruction);
    default:
        if (((instruction->standard_operation >= R_STANDARD_CALL_NET_TCP_LISTENER_LOCAL_ADDRESS) &&
             (instruction->standard_operation <= R_STANDARD_CALL_NET_UDP_LOCAL_ADDRESS)) ||
            ((instruction->standard_operation >= R_STANDARD_CALL_NET_TCP_GET_OPTIONS) &&
             (instruction->standard_operation <= R_STANDARD_CALL_NET_UNIX_PEER_CREDENTIALS))) {
            return r_llvm_library_net(emitter, instruction);
        }
        if ((instruction->standard_operation >= R_STANDARD_CALL_TIME_DURATION_FROM_SECONDS) &&
            (instruction->standard_operation <= R_STANDARD_CALL_TIME_SLEEP_UNTIL)) {
            return r_llvm_library_time(emitter, instruction);
        }
        return r_llvm_library_misc(emitter, instruction);
    }
}

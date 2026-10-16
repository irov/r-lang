#include "standard_internal.h"

#include <string.h>

/* The machine operations of core (Library R-LIB-0028, Core R-UNSAFE-0008, B11). A fence is an
   LLVM fence, across threads or, for core::signal_fence, within the thread and its signal
   handlers (syncscope "singlethread"); its order is a run-time value of core::memory_order
   admitted without relaxed, as the atomics take theirs (atomics.c), and a constant order folds
   the switch away. core::trap is llvm.trap and core::unreachable nothing: the MIR block of either
   ends with an unreachable instruction. core::frame_address and core::stack_pointer read the frame
   record (llvm.frameaddress) and the sp register (llvm.read_register) as integers; a prefetch is
   llvm.prefetch of the data cache with the highest locality, which neither faults nor changes
   memory. B12: core::pointer_address is ptrtoint, core::offset_bytes an inbounds byte GEP, which
   R-SAFETY-OFFSET keeps within the allocated object (a null pointer only with count zero, which
   LLVM admits inbounds). */

static const LLVMAtomicOrdering r_llvm_fence_orders[5] = {LLVMAtomicOrderingMonotonic,
                                                          LLVMAtomicOrderingAcquire,
                                                          LLVMAtomicOrderingRelease,
                                                          LLVMAtomicOrderingAcquireRelease,
                                                          LLVMAtomicOrderingSequentiallyConsistent};

/* A fence of every order but relaxed, chosen by the run-time order; relaxed ends the process as a
   contract violation before any fence (R-MEM-0007). */
static bool r_llvm_machine_fence(RLlvmEmitter *emitter,
                                 const RMirInstruction *instruction,
                                 bool single_thread) {
    LLVMValueRef value = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 0U));
    LLVMBasicBlockRef done;
    LLVMValueRef choice;

    if (value == NULL) {
        return false;
    }
    value = LLVMBuildIntCast2(emitter->builder, value, r_llvm_int(emitter, 32U), 0, "");
    if (!r_llvm_check(
            emitter,
            LLVMBuildOr(
                emitter->builder,
                LLVMBuildICmp(emitter->builder, LLVMIntEQ, value, r_llvm_u32(emitter, 0U), ""),
                LLVMBuildICmp(emitter->builder, LLVMIntUGE, value, r_llvm_u32(emitter, 5U), ""),
                ""),
            "R_RUNTIME_PANIC_CONTRACT_VIOLATION",
            instruction->span,
            R_MIR_BLOCK_ID_INVALID)) {
        return false;
    }
    done = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    choice = LLVMBuildSwitch(emitter->builder, value, done, 4U);
    for (uint32_t order = 1U; order < 5U; ++order) {
        LLVMBasicBlockRef block =
            LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        LLVMAddCase(choice, r_llvm_u32(emitter, order), block);
        LLVMPositionBuilderAtEnd(emitter->builder, block);
        (void)LLVMBuildFence(
            emitter->builder, r_llvm_fence_orders[order], single_thread ? 1 : 0, "");
        (void)LLVMBuildBr(emitter->builder, done);
    }
    LLVMPositionBuilderAtEnd(emitter->builder, done);
    return true;
}

/* A call of a non-overloaded intrinsic. */
static LLVMValueRef r_llvm_machine_intrinsic(RLlvmEmitter *emitter,
                                             const char *name,
                                             LLVMTypeRef *types,
                                             size_t type_count,
                                             LLVMValueRef *arguments,
                                             unsigned count) {
    const unsigned id = LLVMLookupIntrinsicID(name, strlen(name));

    if (id == 0U) {
        return NULL;
    }
    return LLVMBuildCall2(emitter->builder,
                          LLVMIntrinsicGetType(emitter->context, id, types, type_count),
                          LLVMGetIntrinsicDeclaration(emitter->module, id, types, type_count),
                          arguments,
                          count,
                          "");
}

bool r_llvm_std_machine(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    LLVMTypeRef pointer = r_llvm_pointer(emitter);
    LLVMTypeRef word = r_llvm_int(emitter, 64U);
    LLVMValueRef arguments[4];
    LLVMValueRef value;

    switch (instruction->standard_operation) {
    case R_STANDARD_CALL_CORE_ATOMIC_FENCE:
        return r_llvm_machine_fence(emitter, instruction, false);
    case R_STANDARD_CALL_CORE_SIGNAL_FENCE:
        return r_llvm_machine_fence(emitter, instruction, true);
    case R_STANDARD_CALL_CORE_TRAP:
        return r_llvm_machine_intrinsic(emitter, "llvm.trap", NULL, 0U, NULL, 0U) != NULL;
    case R_STANDARD_CALL_CORE_UNREACHABLE:
        /* The unreachable instruction that ends the block is the whole operation. */
        return true;
    case R_STANDARD_CALL_CORE_FRAME_ADDRESS:
        arguments[0] = r_llvm_u32(emitter, 0U);
        value = r_llvm_machine_intrinsic(emitter, "llvm.frameaddress", &pointer, 1U, arguments, 1U);
        return (value != NULL) &&
               r_llvm_set_value(emitter,
                                instruction->result,
                                LLVMBuildPtrToInt(emitter->builder, value, word, ""));
    case R_STANDARD_CALL_CORE_STACK_POINTER: {
        LLVMMetadataRef name = LLVMMDStringInContext2(emitter->context, "sp", 2U);
        arguments[0] = LLVMMetadataAsValue(emitter->context,
                                           LLVMMDNodeInContext2(emitter->context, &name, 1U));
        value = r_llvm_machine_intrinsic(emitter, "llvm.read_register", &word, 1U, arguments, 1U);
        return (value != NULL) && r_llvm_set_value(emitter, instruction->result, value);
    }
    case R_STANDARD_CALL_CORE_PREFETCH_READ:
    case R_STANDARD_CALL_CORE_PREFETCH_WRITE:
        arguments[0] = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 0U));
        if (arguments[0] == NULL) {
            return false;
        }
        arguments[1] = r_llvm_u32(
            emitter,
            instruction->standard_operation == R_STANDARD_CALL_CORE_PREFETCH_WRITE ? 1U : 0U);
        arguments[2] = r_llvm_u32(emitter, 3U);
        arguments[3] = r_llvm_u32(emitter, 1U);
        return r_llvm_machine_intrinsic(emitter, "llvm.prefetch", &pointer, 1U, arguments, 4U) !=
               NULL;
    case R_STANDARD_CALL_CORE_POINTER_ADDRESS:
        value = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 0U));
        return (value != NULL) &&
               r_llvm_set_value(emitter,
                                instruction->result,
                                LLVMBuildPtrToInt(emitter->builder, value, word, ""));
    case R_STANDARD_CALL_CORE_CAST_MUTABLE:
        /* The permission is a property of the R type only. */
        value = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 0U));
        return (value != NULL) && r_llvm_set_value(emitter, instruction->result, value);
    case R_STANDARD_CALL_CORE_C_ERRNO:
    case R_STANDARD_CALL_CORE_SET_C_ERRNO: {
        /* Library R-LIB-0028 (B12): errno of the C library is the int that __error() of the
           target's C library (Darwin libc) addresses for the calling thread. */
        LLVMTypeRef locate_type = LLVMFunctionType(pointer, NULL, 0U, 0);
        LLVMValueRef locate = LLVMGetNamedFunction(emitter->module, "__error");
        LLVMValueRef place;
        LLVMValueRef access;

        if (locate == NULL) {
            locate = LLVMAddFunction(emitter->module, "__error", locate_type);
            r_llvm_add_function_attribute(emitter, locate, "nounwind");
        }
        place = LLVMBuildCall2(emitter->builder, locate_type, locate, NULL, 0U, "");
        if (instruction->standard_operation == R_STANDARD_CALL_CORE_C_ERRNO) {
            access = LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), place, "");
            LLVMSetAlignment(access, 4U);
            return r_llvm_set_value(emitter, instruction->result, access);
        }
        value = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 0U));
        if (value == NULL) {
            return false;
        }
        access = LLVMBuildStore(emitter->builder, value, place);
        LLVMSetAlignment(access, 4U);
        return true;
    }
    case R_STANDARD_CALL_CORE_OFFSET_BYTES: {
        LLVMValueRef base = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 0U));
        LLVMValueRef count = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 1U));
        if ((base == NULL) || (count == NULL)) {
            return false;
        }
        return r_llvm_set_value(
            emitter,
            instruction->result,
            LLVMBuildInBoundsGEP2(emitter->builder, r_llvm_int(emitter, 8U), base, &count, 1U, ""));
    }
    default:
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
}

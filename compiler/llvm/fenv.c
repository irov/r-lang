#include "emit_internal.h"

#include <llvm-c/Core.h>

#include <string.h>

/* The floating-point environment at the boundary with C (B8-3). R code runs in the default
   environment: a call of C saves it and gives the callee the default one
   (r_runtime_c_call_begin), a call from C saves the caller's and sets the default
   (r_runtime_c_entry_begin), and the matching end restores what was saved. LLVM assumes the
   default environment everywhere and moves floating-point operations across calls, so an
   operation of R could run in the environment of C on the other side of such a switch: an addition
   of a callback was computed after the caller's rounding mode came back, rounded upward and left
   an inexact flag in the caller.

   A function that switches the environment is therefore strictfp: its calls are marked strictfp
   and its floating-point operations become constrained intrinsics (round.tonearest,
   fpexcept.ignore, as LLVM's inliner converts them), which the optimizer and the code generator
   keep in order with the calls. LLVM converts the operations of an R function it inlines into
   such a function the same way, and never inlines a strictfp function into one that is not. */

static const char *const r_llvm_fenv_switches[] = {
    "r_runtime_c_call_begin",
    "r_runtime_c_call_end",
    "r_runtime_c_entry_begin",
    "r_runtime_c_entry_end",
};

static bool r_llvm_fenv_switches_environment(LLVMValueRef function) {
    LLVMBasicBlockRef block;

    for (block = LLVMGetFirstBasicBlock(function); block != NULL;
         block = LLVMGetNextBasicBlock(block)) {
        LLVMValueRef instruction;
        for (instruction = LLVMGetFirstInstruction(block); instruction != NULL;
             instruction = LLVMGetNextInstruction(instruction)) {
            LLVMValueRef callee;
            size_t length = 0U;
            const char *name;
            size_t index;
            if (LLVMIsACallInst(instruction) == NULL) {
                continue;
            }
            callee = LLVMGetCalledValue(instruction);
            if ((callee == NULL) || (LLVMIsAFunction(callee) == NULL)) {
                continue;
            }
            name = LLVMGetValueName2(callee, &length);
            for (index = 0U; index < sizeof(r_llvm_fenv_switches) / sizeof(r_llvm_fenv_switches[0]);
                 ++index) {
                if ((strlen(r_llvm_fenv_switches[index]) == length) &&
                    (memcmp(r_llvm_fenv_switches[index], name, length) == 0)) {
                    return true;
                }
            }
        }
    }
    return false;
}

static LLVMValueRef r_llvm_fenv_string(RLlvmEmitter *emitter, const char *text) {
    return LLVMMetadataAsValue(emitter->context,
                               LLVMMDStringInContext2(emitter->context, text, strlen(text)));
}

static const char *r_llvm_fenv_predicate(LLVMRealPredicate predicate) {
    switch (predicate) {
    case LLVMRealOEQ:
        return "oeq";
    case LLVMRealOGT:
        return "ogt";
    case LLVMRealOGE:
        return "oge";
    case LLVMRealOLT:
        return "olt";
    case LLVMRealOLE:
        return "ole";
    case LLVMRealONE:
        return "one";
    case LLVMRealORD:
        return "ord";
    case LLVMRealUNO:
        return "uno";
    case LLVMRealUEQ:
        return "ueq";
    case LLVMRealUGT:
        return "ugt";
    case LLVMRealUGE:
        return "uge";
    case LLVMRealULT:
        return "ult";
    case LLVMRealULE:
        return "ule";
    case LLVMRealUNE:
        return "une";
    default:
        return NULL;
    }
}

/* The constrained intrinsic of a floating-point instruction, or NULL for an instruction that
   reads no environment. `rounding` says whether it takes a rounding argument. */
static const char *r_llvm_fenv_intrinsic(LLVMOpcode opcode, bool *rounding, bool *two_types) {
    *rounding = true;
    *two_types = false;
    switch (opcode) {
    case LLVMFAdd:
        return "llvm.experimental.constrained.fadd";
    case LLVMFSub:
        return "llvm.experimental.constrained.fsub";
    case LLVMFMul:
        return "llvm.experimental.constrained.fmul";
    case LLVMFDiv:
        return "llvm.experimental.constrained.fdiv";
    case LLVMFRem:
        return "llvm.experimental.constrained.frem";
    case LLVMSIToFP:
        *two_types = true;
        return "llvm.experimental.constrained.sitofp";
    case LLVMUIToFP:
        *two_types = true;
        return "llvm.experimental.constrained.uitofp";
    case LLVMFPTrunc:
        *two_types = true;
        return "llvm.experimental.constrained.fptrunc";
    case LLVMFPToSI:
        *rounding = false;
        *two_types = true;
        return "llvm.experimental.constrained.fptosi";
    case LLVMFPToUI:
        *rounding = false;
        *two_types = true;
        return "llvm.experimental.constrained.fptoui";
    case LLVMFPExt:
        *rounding = false;
        *two_types = true;
        return "llvm.experimental.constrained.fpext";
    case LLVMFCmp:
        *rounding = false;
        return "llvm.experimental.constrained.fcmp";
    default:
        return NULL;
    }
}

/* Replaces one floating-point instruction by its constrained intrinsic. */
static bool r_llvm_fenv_constrain(RLlvmEmitter *emitter,
                                  LLVMBuilderRef builder,
                                  LLVMValueRef instruction,
                                  LLVMAttributeRef strict) {
    const LLVMOpcode opcode = LLVMGetInstructionOpcode(instruction);
    bool rounding = false;
    bool two_types = false;
    const char *name = r_llvm_fenv_intrinsic(opcode, &rounding, &two_types);
    LLVMTypeRef types[2];
    LLVMValueRef arguments[4];
    unsigned count = 0U;
    unsigned id;
    LLVMValueRef intrinsic;
    LLVMValueRef call;

    if (name == NULL) {
        return true;
    }
    id = LLVMLookupIntrinsicID(name, strlen(name));
    if (opcode == LLVMFCmp) {
        const char *predicate = r_llvm_fenv_predicate(LLVMGetFCmpPredicate(instruction));
        if (predicate == NULL) {
            return true;
        }
        types[0] = LLVMTypeOf(LLVMGetOperand(instruction, 0U));
        arguments[count++] = LLVMGetOperand(instruction, 0U);
        arguments[count++] = LLVMGetOperand(instruction, 1U);
        arguments[count++] = r_llvm_fenv_string(emitter, predicate);
    } else if (two_types) {
        types[0] = LLVMTypeOf(instruction);
        types[1] = LLVMTypeOf(LLVMGetOperand(instruction, 0U));
        arguments[count++] = LLVMGetOperand(instruction, 0U);
    } else {
        types[0] = LLVMTypeOf(instruction);
        arguments[count++] = LLVMGetOperand(instruction, 0U);
        arguments[count++] = LLVMGetOperand(instruction, 1U);
    }
    if (rounding) {
        arguments[count++] = r_llvm_fenv_string(emitter, "round.tonearest");
    }
    arguments[count++] = r_llvm_fenv_string(emitter, "fpexcept.ignore");
    intrinsic = LLVMGetIntrinsicDeclaration(emitter->module, id, types, two_types ? 2U : 1U);
    if (intrinsic == NULL) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    LLVMPositionBuilderBefore(builder, instruction);
    call =
        LLVMBuildCall2(builder, LLVMGlobalGetValueType(intrinsic), intrinsic, arguments, count, "");
    LLVMAddCallSiteAttribute(call, (LLVMAttributeIndex)LLVMAttributeFunctionIndex, strict);
    LLVMInstructionSetDebugLoc(call, LLVMInstructionGetDebugLoc(instruction));
    LLVMReplaceAllUsesWith(instruction, call);
    LLVMInstructionEraseFromParent(instruction);
    return true;
}

bool r_llvm_strict_fp(RLlvmEmitter *emitter) {
    const unsigned kind = LLVMGetEnumAttributeKindForName("strictfp", 8U);
    LLVMAttributeRef strict = LLVMCreateEnumAttribute(emitter->context, kind, 0U);
    LLVMBuilderRef builder = LLVMCreateBuilderInContext(emitter->context);
    LLVMValueRef function;
    bool success = true;

    for (function = LLVMGetFirstFunction(emitter->module); success && (function != NULL);
         function = LLVMGetNextFunction(function)) {
        LLVMBasicBlockRef block;
        if ((LLVMCountBasicBlocks(function) == 0U) || !r_llvm_fenv_switches_environment(function)) {
            continue;
        }
        LLVMAddAttributeAtIndex(function, (LLVMAttributeIndex)LLVMAttributeFunctionIndex, strict);
        for (block = LLVMGetFirstBasicBlock(function); success && (block != NULL);
             block = LLVMGetNextBasicBlock(block)) {
            LLVMValueRef instruction = LLVMGetFirstInstruction(block);
            while (success && (instruction != NULL)) {
                LLVMValueRef next = LLVMGetNextInstruction(instruction);
                if ((LLVMIsACallInst(instruction) != NULL) ||
                    (LLVMIsAInvokeInst(instruction) != NULL)) {
                    LLVMAddCallSiteAttribute(
                        instruction, (LLVMAttributeIndex)LLVMAttributeFunctionIndex, strict);
                } else {
                    success = r_llvm_fenv_constrain(emitter, builder, instruction, strict);
                }
                instruction = next;
            }
        }
    }
    LLVMDisposeBuilder(builder);
    return success;
}

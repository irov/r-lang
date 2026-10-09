#include "emit_internal.h"

/* Dispatchers (R-TYPE-0051, R-TYPE-0054), as the C17 emitter's r_c17_emit_dyn_dispatcher and
   r_c17_emit_function_value_dispatcher: a switch on the tag of an interface borrow, or on the
   symbol number a function value holds, with one direct call per target that forwards the result
   memory and every argument after the receiver. An interface member receives the address its
   borrow holds. A value no target answers to is a contract violation. */

bool r_llvm_define_dispatcher(RLlvmEmitter *emitter, RSymbolId id, const RSemanticSymbol *symbol) {
    const RFrontendContext *context = emitter->frontend;
    const bool dyn = r_semantic_dyn_dispatcher(context, id);
    const bool in_memory = (symbol->effect_carrier_type != R_TYPE_ID_INVALID) ||
                           (!r_llvm_type_is_void(emitter, symbol->return_type) &&
                            (r_llvm_scalar_type(emitter, symbol->return_type) == NULL));
    const bool never = (symbol->effect_carrier_type == R_TYPE_ID_INVALID) &&
                       (r_llvm_value_kind(emitter, symbol->return_type) == R_SEMANTIC_TYPE_NEVER);
    const unsigned first = in_memory ? 1U : 0U;
    const RTypeId receiver_type =
        symbol->parameter_count == 0U
            ? R_TYPE_ID_INVALID
            : context->semantic_parameter_types[symbol->first_parameter_type];
    LLVMValueRef receiver;
    LLVMValueRef selector;
    LLVMValueRef member = NULL;
    LLVMValueRef choice;
    LLVMValueRef *arguments;
    LLVMBasicBlockRef entry;
    LLVMBasicBlockRef unknown;
    RTypeId interface = R_TYPE_ID_INVALID;
    size_t index;
    unsigned cases = 0U;

    if (symbol->parameter_count == 0U) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    emitter->mir = NULL;
    emitter->symbol = symbol;
    emitter->symbol_id = id;
    emitter->function = emitter->functions[id];
    entry = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "entry");
    unknown = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMPositionBuilderAtEnd(emitter->builder, entry);
    receiver = LLVMGetParam(emitter->function, first);
    if (dyn) {
        /* struct { void *r_pointer; uint32_t r_tag; } */
        interface = r_semantic_dyn_referent(context, receiver_type);
        member = LLVMBuildLoad2(emitter->builder, r_llvm_pointer(emitter), receiver, "");
        selector = LLVMBuildLoad2(emitter->builder,
                                  r_llvm_int(emitter, 32U),
                                  r_llvm_byte_offset(emitter, receiver, 8U),
                                  "");
    } else {
        selector = r_llvm_value_kind(emitter, receiver_type) == R_SEMANTIC_TYPE_BORROW
                       ? LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), receiver, "")
                       : receiver;
    }
    for (index = 0U; index < context->dyn_target_count; ++index) {
        cases += context->dyn_targets[index].dispatcher == id ? 1U : 0U;
    }
    choice = LLVMBuildSwitch(emitter->builder, selector, unknown, cases);
    arguments =
        r_llvm_allocate(emitter, ((size_t)symbol->parameter_count + 1U) * sizeof(*arguments));
    if (arguments == NULL) {
        return false;
    }
    for (index = 0U; index < context->dyn_target_count; ++index) {
        const RDynTarget target = context->dyn_targets[index];
        LLVMBasicBlockRef block;
        LLVMValueRef call;
        unsigned count = 0U;
        uint32_t tag = (uint32_t)target.target;
        uint32_t parameter;
        if (target.dispatcher != id) {
            continue;
        }
        if (dyn && r_semantic_dyn_format_target(context, target.target)) {
            /* R-TYPE-0046 (L32): the format glue of a member with standard formatting. */
            LLVMBasicBlockRef glue =
                LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
            if (!in_memory || (symbol->effect_carrier_type == R_TYPE_ID_INVALID) ||
                (symbol->parameter_count != 2U) ||
                !r_semantic_dyn_tag(context, interface, target.member, &tag)) {
                r_llvm_free(emitter, arguments);
                return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
            }
            LLVMAddCase(choice, r_llvm_u32(emitter, tag), glue);
            LLVMPositionBuilderAtEnd(emitter->builder, glue);
            if (!r_llvm_format_glue_member(emitter,
                                           target.member,
                                           LLVMGetParam(emitter->function, 0U),
                                           symbol->effect_carrier_type,
                                           member,
                                           LLVMGetParam(emitter->function, first + 1U))) {
                r_llvm_free(emitter, arguments);
                return false;
            }
            (void)LLVMBuildRetVoid(emitter->builder);
            continue;
        }
        if ((emitter->functions[target.target] == NULL) ||
            (dyn && !r_semantic_dyn_tag(context, interface, target.member, &tag))) {
            r_llvm_free(emitter, arguments);
            return r_llvm_unsupported(emitter, "a dispatcher target that is not lowered");
        }
        block = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        LLVMAddCase(choice, r_llvm_u32(emitter, tag), block);
        LLVMPositionBuilderAtEnd(emitter->builder, block);
        if (in_memory) {
            arguments[count++] = LLVMGetParam(emitter->function, 0U);
        }
        if (dyn) {
            arguments[count++] = member;
        }
        for (parameter = 1U; parameter < symbol->parameter_count; ++parameter) {
            arguments[count++] = LLVMGetParam(emitter->function, first + parameter);
        }
        call = LLVMBuildCall2(emitter->builder,
                              emitter->function_types[target.target],
                              emitter->functions[target.target],
                              arguments,
                              count,
                              "");
        if (never) {
            (void)LLVMBuildUnreachable(emitter->builder);
        } else if (in_memory || r_llvm_type_is_void(emitter, symbol->return_type)) {
            (void)LLVMBuildRetVoid(emitter->builder);
        } else {
            (void)LLVMBuildRet(emitter->builder, call);
        }
    }
    r_llvm_free(emitter, arguments);
    LLVMPositionBuilderAtEnd(emitter->builder, unknown);
    return r_llvm_panic(emitter,
                        "R_RUNTIME_PANIC_CONTRACT_VIOLATION",
                        symbol->name_span,
                        R_MIR_BLOCK_ID_INVALID) &&
           (emitter->status == R_FRONTEND_OK);
}

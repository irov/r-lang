#include "emit_internal.h"

/* Standard tagged results with a fixed set of variants
   (r_c17_standard_outcome_uses_tagged_storage): std.async::broadcast_result<T> {r_tag; union {T
   r_received; uint64_t r_lagged;}}, closed without payload (R-SLIB-ASYNC-0016), and
   std.arc/std.rc::try_unwrap_result<T> {r_tag; union {T r_unwrapped; RRuntimeArc|RRuntimeRc
   r_shared;}}. A variant's payload moves by its glue; the owner of a shared result moves bitwise
   and is released by a drop. */

typedef enum RLlvmTaggedKind {
    R_LLVM_TAGGED_NONE = 0,
    R_LLVM_TAGGED_BROADCAST,
    R_LLVM_TAGGED_UNWRAP_ARC,
    R_LLVM_TAGGED_UNWRAP_RC
} RLlvmTaggedKind;

static RLlvmTaggedKind r_llvm_tagged_kind(RLlvmEmitter *emitter, RTypeId id) {
    const RSemanticType *type = r_llvm_type(emitter, r_llvm_value_type(emitter, id));

    if ((type == NULL) || (type->kind != R_SEMANTIC_TYPE_STANDARD) ||
        (type->flags != R_SEMANTIC_TYPE_FLAG_NONE) || (type->base == R_TYPE_ID_INVALID) ||
        (type->second != R_TYPE_ID_INVALID)) {
        return R_LLVM_TAGGED_NONE;
    }
    if (r_llvm_standard_named(emitter, id, "std.async::broadcast_result")) {
        return R_LLVM_TAGGED_BROADCAST;
    }
    if (r_llvm_standard_named(emitter, id, "std.arc::try_unwrap_result")) {
        return R_LLVM_TAGGED_UNWRAP_ARC;
    }
    return r_llvm_standard_named(emitter, id, "std.rc::try_unwrap_result") ? R_LLVM_TAGGED_UNWRAP_RC
                                                                           : R_LLVM_TAGGED_NONE;
}

bool r_llvm_standard_tagged(RLlvmEmitter *emitter, RTypeId id) {
    return r_llvm_tagged_kind(emitter, id) != R_LLVM_TAGGED_NONE;
}

/* The program type of a second payload: u64 for lagged, the owner for shared; none when the
   program never names it. */
static RTypeId r_llvm_tagged_second(RLlvmEmitter *emitter, RTypeId id, RLlvmTaggedKind kind) {
    const RSemanticType *type = r_llvm_type(emitter, r_llvm_value_type(emitter, id));
    size_t index;

    for (index = 0U; index < emitter->frontend->semantic_type_count; ++index) {
        const RSemanticType *candidate = &emitter->frontend->semantic_types[index];
        if ((kind == R_LLVM_TAGGED_BROADCAST) ? (candidate->kind == R_SEMANTIC_TYPE_U64)
            : (kind == R_LLVM_TAGGED_UNWRAP_ARC)
                ? ((candidate->kind == R_SEMANTIC_TYPE_ARC) && (candidate->base == type->base))
                : ((candidate->kind == R_SEMANTIC_TYPE_RC) && (candidate->base == type->base))) {
            return (RTypeId)(index + 1U);
        }
    }
    return R_TYPE_ID_INVALID;
}

RTypeId r_llvm_standard_tagged_payload(RLlvmEmitter *emitter, RTypeId id, uint64_t tag) {
    const RLlvmTaggedKind kind = r_llvm_tagged_kind(emitter, id);
    const RSemanticType *type = r_llvm_type(emitter, r_llvm_value_type(emitter, id));

    if ((kind == R_LLVM_TAGGED_NONE) || (type == NULL)) {
        return R_TYPE_ID_INVALID;
    }
    if (tag == 0U) {
        return r_llvm_type_is_void(emitter, type->base) ? R_TYPE_ID_INVALID : type->base;
    }
    return tag == 1U ? r_llvm_tagged_second(emitter, id, kind) : R_TYPE_ID_INVALID;
}

/* The size and alignment of the second payload, also when the program never names its type. */
static bool r_llvm_tagged_second_layout(RLlvmEmitter *emitter,
                                        RLlvmTaggedKind kind,
                                        uint32_t *size,
                                        uint32_t *align) {
    if (kind == R_LLVM_TAGGED_BROADCAST) {
        *size = 8U;
        *align = 8U;
        return true;
    }
    return r_llvm_runtime_layout(
        emitter, kind == R_LLVM_TAGGED_UNWRAP_ARC ? "RRuntimeArc" : "RRuntimeRc", size, align);
}

/* The union of the payloads: size and alignment. */
bool r_llvm_standard_tagged_union(RLlvmEmitter *emitter,
                                  RTypeId id,
                                  uint32_t *size,
                                  uint32_t *align) {
    const RLlvmTaggedKind kind = r_llvm_tagged_kind(emitter, id);
    const RSemanticType *type = r_llvm_type(emitter, r_llvm_value_type(emitter, id));
    uint32_t value_size = 0U;
    uint32_t value_align = 1U;
    uint32_t second_size = 0U;
    uint32_t second_align = 1U;

    if ((kind == R_LLVM_TAGGED_NONE) || (type == NULL)) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    if ((!r_llvm_type_is_void(emitter, type->base) &&
         !r_llvm_layout(emitter, type->base, &value_size, &value_align)) ||
        !r_llvm_tagged_second_layout(emitter, kind, &second_size, &second_align)) {
        return false;
    }
    *size = value_size > second_size ? value_size : second_size;
    *align = value_align > second_align ? value_align : second_align;
    return true;
}

/* The move and drop glue of these results. */
bool r_llvm_standard_tagged_glue(
    RLlvmEmitter *emitter, RTypeId id, bool move, LLVMValueRef first, LLVMValueRef second) {
    const RLlvmTaggedKind kind = r_llvm_tagged_kind(emitter, id);
    const RSemanticType *type = r_llvm_type(emitter, r_llvm_value_type(emitter, id));
    LLVMValueRef tagged = move ? second : first;
    LLVMValueRef tag = LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), tagged, "");
    LLVMBasicBlockRef value_block =
        LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef second_block =
        LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMValueRef choice;
    uint32_t union_size = 0U;
    uint32_t union_align = 0U;
    uint32_t payload;
    uint32_t second_size = 0U;
    uint32_t second_align = 0U;

    if ((kind == R_LLVM_TAGGED_NONE) || (type == NULL) ||
        !r_llvm_standard_tagged_union(emitter, id, &union_size, &union_align) ||
        !r_llvm_tagged_second_layout(emitter, kind, &second_size, &second_align)) {
        return (kind == R_LLVM_TAGGED_NONE) ? r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR)
                                            : false;
    }
    payload = ((4U + union_align - 1U) / union_align) * union_align;
    choice = LLVMBuildSwitch(emitter->builder, tag, done, 2U);
    LLVMAddCase(choice, r_llvm_u32(emitter, 0U), value_block);
    LLVMAddCase(choice, r_llvm_u32(emitter, 1U), second_block);
    LLVMPositionBuilderAtEnd(emitter->builder, value_block);
    if (!r_llvm_type_is_void(emitter, type->base)) {
        if (move) {
            if (r_llvm_type_requires_drop(emitter, type->base)) {
                if (!r_llvm_call_move(emitter,
                                      type->base,
                                      r_llvm_byte_offset(emitter, first, payload),
                                      r_llvm_byte_offset(emitter, second, payload))) {
                    return false;
                }
            } else {
                uint32_t size = 0U;
                uint32_t align = 0U;
                if (!r_llvm_layout(emitter, type->base, &size, &align)) {
                    return false;
                }
                (void)LLVMBuildMemCpy(emitter->builder,
                                      r_llvm_byte_offset(emitter, first, payload),
                                      1U,
                                      r_llvm_byte_offset(emitter, second, payload),
                                      1U,
                                      r_llvm_u64(emitter, size));
            }
        } else if (r_llvm_type_requires_drop(emitter, type->base) &&
                   !r_llvm_call_drop(
                       emitter, type->base, r_llvm_byte_offset(emitter, first, payload))) {
            return false;
        }
    }
    (void)LLVMBuildBr(emitter->builder, done);
    LLVMPositionBuilderAtEnd(emitter->builder, second_block);
    if (move) {
        (void)LLVMBuildMemCpy(emitter->builder,
                              r_llvm_byte_offset(emitter, first, payload),
                              1U,
                              r_llvm_byte_offset(emitter, second, payload),
                              1U,
                              r_llvm_u64(emitter, second_size));
        if (kind != R_LLVM_TAGGED_BROADCAST) {
            (void)LLVMBuildMemSet(emitter->builder,
                                  r_llvm_byte_offset(emitter, second, payload),
                                  LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                                  r_llvm_u64(emitter, second_size),
                                  1U);
        }
    } else if (kind != R_LLVM_TAGGED_BROADCAST) {
        LLVMValueRef owner = r_llvm_byte_offset(emitter, first, payload);
        if (r_llvm_call_runtime(emitter,
                                kind == R_LLVM_TAGGED_UNWRAP_ARC ? "r_runtime_arc_release"
                                                                 : "r_runtime_rc_release",
                                &owner,
                                1U,
                                NULL) == NULL) {
            return false;
        }
    }
    (void)LLVMBuildBr(emitter->builder, done);
    LLVMPositionBuilderAtEnd(emitter->builder, done);
    if (move) {
        (void)LLVMBuildStore(emitter->builder, tag, first);
    }
    (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, UINT32_MAX), tagged);
    return true;
}

#include "standard_internal.h"

#include <string.h>

/* The atomic operations of core (core::atomic_load ... core::atomic_is_lock_free), as the C17
   emitter's atomic path with r_core_memory_order: an order is a run-time value of
   core::memory_order (relaxed, acquire, release, acq_rel, seq_cst = 0..4) that the operation
   admits by a mask, else the process ends as a contract violation. LLVM orderings are constants,
   so the operation is written once per admitted order behind a switch on the value; a constant
   order folds the switch away. */

static const LLVMAtomicOrdering r_llvm_orders[5] = {LLVMAtomicOrderingMonotonic,
                                                    LLVMAtomicOrderingAcquire,
                                                    LLVMAtomicOrderingRelease,
                                                    LLVMAtomicOrderingAcquireRelease,
                                                    LLVMAtomicOrderingSequentiallyConsistent};

typedef struct RLlvmAtomic {
    const RMirInstruction *instruction;
    RStandardCallOperation operation;
    RTypeId value_type;
    LLVMTypeRef memory_type; /* the integer or pointer type of the object in memory */
    uint32_t size;
    LLVMValueRef object;
    LLVMValueRef operand;   /* the value of a store, exchange or fetch; the expected value */
    LLVMValueRef desired;   /* compare_exchange */
    LLVMValueRef slot;      /* the previous value, or the loaded one */
    LLVMValueRef exchanged; /* compare_exchange: an i8 slot */
} RLlvmAtomic;

/* A value in its memory representation (a bool is a byte). */
static LLVMValueRef
r_llvm_atomic_to_memory(RLlvmEmitter *emitter, const RLlvmAtomic *atomic, LLVMValueRef value) {
    LLVMTypeRef type = LLVMTypeOf(value);

    if ((LLVMGetTypeKind(type) == LLVMIntegerTypeKind) && (LLVMGetIntTypeWidth(type) == 1U)) {
        return LLVMBuildZExt(emitter->builder, value, atomic->memory_type, "");
    }
    return value;
}

static LLVMValueRef
r_llvm_atomic_from_memory(RLlvmEmitter *emitter, const RLlvmAtomic *atomic, LLVMValueRef value) {
    LLVMTypeRef scalar = r_llvm_scalar_type(emitter, atomic->value_type);

    if ((LLVMGetTypeKind(scalar) == LLVMIntegerTypeKind) && (LLVMGetIntTypeWidth(scalar) == 1U)) {
        return LLVMBuildICmp(
            emitter->builder, LLVMIntNE, value, LLVMConstInt(atomic->memory_type, 0U, 0), "");
    }
    return value;
}

/* A checked add or subtract of a signed value: the previous value is read and replaced until no
   other thread intervened; an overflow ends the process (r_core_checked_add_*). */
static bool r_llvm_atomic_checked_fetch(RLlvmEmitter *emitter,
                                        const RLlvmAtomic *atomic,
                                        LLVMAtomicOrdering order) {
    const bool add = atomic->operation == R_STANDARD_CALL_CORE_ATOMIC_FETCH_ADD;
    const LLVMAtomicOrdering failure =
        order == LLVMAtomicOrderingRelease          ? LLVMAtomicOrderingMonotonic
        : order == LLVMAtomicOrderingAcquireRelease ? LLVMAtomicOrderingAcquire
                                                    : order;
    LLVMBasicBlockRef entry = LLVMGetInsertBlock(emitter->builder);
    LLVMBasicBlockRef loop = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMValueRef first = LLVMBuildLoad2(emitter->builder, atomic->memory_type, atomic->object, "");
    LLVMValueRef current;
    LLVMValueRef arguments[2];
    LLVMValueRef pair;
    LLVMValueRef exchange;
    LLVMBasicBlockRef retry;
    LLVMValueRef incoming[2];
    LLVMBasicBlockRef blocks[2];

    LLVMSetOrdering(first, LLVMAtomicOrderingMonotonic);
    LLVMSetAlignment(first, atomic->size);
    (void)LLVMBuildBr(emitter->builder, loop);
    LLVMPositionBuilderAtEnd(emitter->builder, loop);
    current = LLVMBuildPhi(emitter->builder, atomic->memory_type, "");
    arguments[0] = current;
    arguments[1] = atomic->operand;
    pair = r_llvm_std_intrinsic(emitter,
                                add ? "llvm.sadd.with.overflow" : "llvm.ssub.with.overflow",
                                atomic->memory_type,
                                arguments,
                                2U);
    if (!r_llvm_check(emitter,
                      LLVMBuildExtractValue(emitter->builder, pair, 1U, ""),
                      "R_RUNTIME_PANIC_INTEGER_OVERFLOW",
                      atomic->instruction->span,
                      R_MIR_BLOCK_ID_INVALID)) {
        return false;
    }
    exchange = LLVMBuildAtomicCmpXchg(emitter->builder,
                                      atomic->object,
                                      current,
                                      LLVMBuildExtractValue(emitter->builder, pair, 0U, ""),
                                      order,
                                      failure,
                                      0);
    LLVMSetWeak(exchange, 1);
    /* A failed exchange retries with the value it observed. */
    incoming[0] = first;
    incoming[1] = LLVMBuildExtractValue(emitter->builder, exchange, 0U, "");
    retry = LLVMGetInsertBlock(emitter->builder);
    (void)LLVMBuildCondBr(
        emitter->builder, LLVMBuildExtractValue(emitter->builder, exchange, 1U, ""), done, loop);
    blocks[0] = entry;
    blocks[1] = retry;
    LLVMAddIncoming(current, incoming, blocks, 2U);
    LLVMPositionBuilderAtEnd(emitter->builder, done);
    (void)LLVMBuildStore(emitter->builder, current, atomic->slot);
    return true;
}

/* The operation with constant orders. */
static bool r_llvm_atomic_body(RLlvmEmitter *emitter,
                               const RLlvmAtomic *atomic,
                               LLVMAtomicOrdering order,
                               LLVMAtomicOrdering failure) {
    LLVMValueRef value;

    switch (atomic->operation) {
    case R_STANDARD_CALL_CORE_ATOMIC_LOAD:
        value = LLVMBuildLoad2(emitter->builder, atomic->memory_type, atomic->object, "");
        LLVMSetOrdering(value, order);
        LLVMSetAlignment(value, atomic->size);
        (void)LLVMBuildStore(emitter->builder, value, atomic->slot);
        return true;
    case R_STANDARD_CALL_CORE_ATOMIC_STORE:
        value = LLVMBuildStore(emitter->builder, atomic->operand, atomic->object);
        LLVMSetOrdering(value, order);
        LLVMSetAlignment(value, atomic->size);
        return true;
    case R_STANDARD_CALL_CORE_ATOMIC_COMPARE_EXCHANGE: {
        LLVMValueRef exchange = LLVMBuildAtomicCmpXchg(
            emitter->builder, atomic->object, atomic->operand, atomic->desired, order, failure, 0);
        (void)LLVMBuildStore(emitter->builder,
                             LLVMBuildExtractValue(emitter->builder, exchange, 0U, ""),
                             atomic->slot);
        (void)LLVMBuildStore(
            emitter->builder,
            LLVMBuildZExt(emitter->builder,
                          LLVMBuildExtractValue(emitter->builder, exchange, 1U, ""),
                          r_llvm_int(emitter, 8U),
                          ""),
            atomic->exchanged);
        return true;
    }
    default:
        break;
    }
    if (((atomic->operation == R_STANDARD_CALL_CORE_ATOMIC_FETCH_ADD) ||
         (atomic->operation == R_STANDARD_CALL_CORE_ATOMIC_FETCH_SUB))) {
        unsigned bits = 0U;
        bool is_signed = false;
        if (r_llvm_kind_integer(
                r_llvm_value_kind(emitter, atomic->value_type), &bits, &is_signed) &&
            is_signed) {
            return r_llvm_atomic_checked_fetch(emitter, atomic, order);
        }
    }
    {
        const LLVMAtomicRMWBinOp op =
            atomic->operation == R_STANDARD_CALL_CORE_ATOMIC_EXCHANGE    ? LLVMAtomicRMWBinOpXchg
            : atomic->operation == R_STANDARD_CALL_CORE_ATOMIC_FETCH_ADD ? LLVMAtomicRMWBinOpAdd
            : atomic->operation == R_STANDARD_CALL_CORE_ATOMIC_FETCH_SUB ? LLVMAtomicRMWBinOpSub
            : atomic->operation == R_STANDARD_CALL_CORE_ATOMIC_FETCH_AND ? LLVMAtomicRMWBinOpAnd
            : atomic->operation == R_STANDARD_CALL_CORE_ATOMIC_FETCH_OR  ? LLVMAtomicRMWBinOpOr
                                                                         : LLVMAtomicRMWBinOpXor;
        value = LLVMBuildAtomicRMW(emitter->builder, op, atomic->object, atomic->operand, order, 0);
        (void)LLVMBuildStore(emitter->builder, value, atomic->slot);
        return true;
    }
}

/* r_core_memory_order: a value of core::memory_order the mask admits, as an i32. */
static LLVMValueRef r_llvm_atomic_order(RLlvmEmitter *emitter,
                                        const RLlvmAtomic *atomic,
                                        uint32_t index,
                                        uint32_t mask) {
    LLVMValueRef value =
        r_llvm_value(emitter, r_llvm_std_operand(emitter, atomic->instruction, index));
    LLVMValueRef bit;

    if (value == NULL) {
        return NULL;
    }
    value = LLVMBuildIntCast2(emitter->builder, value, r_llvm_int(emitter, 32U), 0, "");
    bit = LLVMBuildAnd(
        emitter->builder,
        LLVMBuildShl(emitter->builder,
                     r_llvm_u32(emitter, 1U),
                     LLVMBuildAnd(emitter->builder, value, r_llvm_u32(emitter, 31U), ""),
                     ""),
        r_llvm_u32(emitter, mask),
        "");
    return r_llvm_check(
               emitter,
               LLVMBuildOr(
                   emitter->builder,
                   LLVMBuildICmp(emitter->builder, LLVMIntUGE, value, r_llvm_u32(emitter, 5U), ""),
                   LLVMBuildICmp(emitter->builder, LLVMIntEQ, bit, r_llvm_u32(emitter, 0U), ""),
                   ""),
               "R_RUNTIME_PANIC_CONTRACT_VIOLATION",
               atomic->instruction->span,
               R_MIR_BLOCK_ID_INVALID)
               ? value
               : NULL;
}

bool r_llvm_std_atomic(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RStandardCallOperation operation = instruction->standard_operation;
    const bool is_load = operation == R_STANDARD_CALL_CORE_ATOMIC_LOAD;
    const bool is_store = operation == R_STANDARD_CALL_CORE_ATOMIC_STORE;
    const bool is_compare = operation == R_STANDARD_CALL_CORE_ATOMIC_COMPARE_EXCHANGE;
    RLlvmAtomic atomic;
    uint32_t align = 0U;
    LLVMTypeRef scalar;
    LLVMBasicBlockRef done;
    LLVMValueRef selector;
    LLVMValueRef choice;
    uint32_t order;

    (void)memset(&atomic, 0, sizeof(atomic));
    atomic.instruction = instruction;
    atomic.operation = operation;
    atomic.value_type = instruction->auxiliary_type;
    scalar = r_llvm_scalar_type(emitter, atomic.value_type);
    if ((scalar == NULL) || !r_llvm_layout(emitter, atomic.value_type, &atomic.size, &align)) {
        return r_llvm_unsupported(emitter, "an atomic value in memory");
    }
    if (operation == R_STANDARD_CALL_CORE_ATOMIC_IS_LOCK_FREE) {
        /* Objects of 1, 2, 4, 8 and 16 bytes are lock-free on the target (AArch64 LSE). */
        return r_llvm_set_value(emitter,
                                instruction->result,
                                LLVMConstInt(r_llvm_int(emitter, 1U),
                                             (atomic.size == 1U) || (atomic.size == 2U) ||
                                                     (atomic.size == 4U) || (atomic.size == 8U) ||
                                                     (atomic.size == 16U)
                                                 ? 1U
                                                 : 0U,
                                             0));
    }
    atomic.memory_type =
        (LLVMGetTypeKind(scalar) == LLVMIntegerTypeKind) && (LLVMGetIntTypeWidth(scalar) == 1U)
            ? r_llvm_int(emitter, 8U)
            : scalar;
    if ((LLVMGetTypeKind(atomic.memory_type) != LLVMIntegerTypeKind) &&
        (LLVMGetTypeKind(atomic.memory_type) != LLVMPointerTypeKind)) {
        return r_llvm_unsupported(emitter, "an atomic of this type");
    }
    atomic.object = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 0U));
    if (atomic.object == NULL) {
        return false;
    }
    if (!is_load) {
        LLVMValueRef value = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 1U));
        if (value == NULL) {
            return false;
        }
        atomic.operand = r_llvm_atomic_to_memory(emitter, &atomic, value);
    }
    if (is_compare) {
        LLVMValueRef desired = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 2U));
        if (desired == NULL) {
            return false;
        }
        atomic.desired = r_llvm_atomic_to_memory(emitter, &atomic, desired);
        atomic.exchanged = r_llvm_entry_alloca(emitter, 1U, 1U, "exchanged");
    }
    atomic.slot = r_llvm_entry_alloca(emitter, atomic.size, atomic.size, "atomic");
    done = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    if (is_compare) {
        /* r_core_compare_exchange_orders: the failure orders each success order admits. */
        static const uint32_t allowed_failures[5] = {1U, 3U, 1U, 3U, 19U};
        LLVMValueRef success = r_llvm_atomic_order(emitter, &atomic, 3U, 31U);
        LLVMValueRef failure =
            success == NULL ? NULL : r_llvm_atomic_order(emitter, &atomic, 4U, 19U);
        LLVMValueRef admitted;
        LLVMValueRef table[5];
        uint32_t index;
        if (failure == NULL) {
            return false;
        }
        for (index = 0U; index < 5U; ++index) {
            table[index] = r_llvm_u32(emitter, allowed_failures[index]);
        }
        admitted = LLVMBuildAnd(
            emitter->builder,
            LLVMBuildLShr(
                emitter->builder,
                LLVMBuildSelect(
                    emitter->builder,
                    LLVMBuildICmp(
                        emitter->builder, LLVMIntEQ, success, r_llvm_u32(emitter, 0U), ""),
                    table[0],
                    LLVMBuildSelect(
                        emitter->builder,
                        LLVMBuildICmp(
                            emitter->builder, LLVMIntEQ, success, r_llvm_u32(emitter, 1U), ""),
                        table[1],
                        LLVMBuildSelect(
                            emitter->builder,
                            LLVMBuildICmp(
                                emitter->builder, LLVMIntEQ, success, r_llvm_u32(emitter, 2U), ""),
                            table[2],
                            LLVMBuildSelect(emitter->builder,
                                            LLVMBuildICmp(emitter->builder,
                                                          LLVMIntEQ,
                                                          success,
                                                          r_llvm_u32(emitter, 3U),
                                                          ""),
                                            table[3],
                                            table[4],
                                            ""),
                            ""),
                        ""),
                    ""),
                failure,
                ""),
            r_llvm_u32(emitter, 1U),
            "");
        if (!r_llvm_check(
                emitter,
                LLVMBuildICmp(emitter->builder, LLVMIntEQ, admitted, r_llvm_u32(emitter, 0U), ""),
                "R_RUNTIME_PANIC_CONTRACT_VIOLATION",
                instruction->span,
                R_MIR_BLOCK_ID_INVALID)) {
            return false;
        }
        selector =
            LLVMBuildAdd(emitter->builder,
                         LLVMBuildMul(emitter->builder, success, r_llvm_u32(emitter, 5U), ""),
                         failure,
                         "");
        choice = LLVMBuildSwitch(emitter->builder, selector, done, 9U);
        for (order = 0U; order < 5U; ++order) {
            uint32_t failure_order;
            for (failure_order = 0U; failure_order < 5U; ++failure_order) {
                LLVMBasicBlockRef block;
                if ((allowed_failures[order] & (1U << failure_order)) == 0U) {
                    continue;
                }
                block = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
                LLVMAddCase(choice, r_llvm_u32(emitter, order * 5U + failure_order), block);
                LLVMPositionBuilderAtEnd(emitter->builder, block);
                if (!r_llvm_atomic_body(
                        emitter, &atomic, r_llvm_orders[order], r_llvm_orders[failure_order])) {
                    return false;
                }
                (void)LLVMBuildBr(emitter->builder, done);
            }
        }
    } else {
        const uint32_t mask = is_load ? 19U : is_store ? 21U : 31U;
        selector = r_llvm_atomic_order(emitter, &atomic, is_load ? 1U : 2U, mask);
        if (selector == NULL) {
            return false;
        }
        choice = LLVMBuildSwitch(emitter->builder, selector, done, 5U);
        for (order = 0U; order < 5U; ++order) {
            LLVMBasicBlockRef block;
            if ((mask & (1U << order)) == 0U) {
                continue;
            }
            block = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
            LLVMAddCase(choice, r_llvm_u32(emitter, order), block);
            LLVMPositionBuilderAtEnd(emitter->builder, block);
            if (!r_llvm_atomic_body(emitter, &atomic, r_llvm_orders[order], r_llvm_orders[order])) {
                return false;
            }
            (void)LLVMBuildBr(emitter->builder, done);
        }
    }
    LLVMPositionBuilderAtEnd(emitter->builder, done);
    if (is_store) {
        return true;
    }
    if (is_compare) {
        /* The outcome: exchanged (tag 0) or failed (tag 1), with the observed value. */
        LLVMValueRef result = r_llvm_std_result(emitter, instruction);
        uint32_t payload = 0U;
        if ((result == NULL) || !r_llvm_payload_offset(emitter, instruction->type, &payload)) {
            return false;
        }
        (void)LLVMBuildStore(
            emitter->builder,
            LLVMBuildSelect(
                emitter->builder,
                LLVMBuildICmp(
                    emitter->builder,
                    LLVMIntNE,
                    LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 8U), atomic.exchanged, ""),
                    LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                    ""),
                r_llvm_u32(emitter, 0U),
                r_llvm_u32(emitter, 1U),
                ""),
            result);
        (void)LLVMBuildMemCpy(emitter->builder,
                              r_llvm_byte_offset(emitter, result, payload),
                              atomic.size,
                              atomic.slot,
                              atomic.size,
                              r_llvm_u64(emitter, atomic.size));
        return true;
    }
    return r_llvm_set_value(
        emitter,
        instruction->result,
        r_llvm_atomic_from_memory(
            emitter,
            &atomic,
            LLVMBuildLoad2(emitter->builder, atomic.memory_type, atomic.slot, "")));
}

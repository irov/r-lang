#include "emit_internal.h"

#include <llvm-c/Core.h>
#include <llvm-c/Transforms/PassBuilder.h>

#include <stdio.h>
#include <string.h>

/* The function attribute that marks a function with a versioned index until
   r_llvm_unswitch_versions has unswitched its loops. */
#define R_LLVM_VERSIONED "r.versioned"

/* Loop versioning of affine indices (B7; P4.4 of the C17 emitter), an optimization under Core
   R-AM-0003.

   An index `base[A + k * S]` checks its value against len(base) wherever it runs (R-EXPR-0021).
   When the check is dominated by a test `k < B` of an unsigned local k that nothing changes
   between the test and the index, k is at most B - 1 there, so the index is at most
   A + (B - 1) * S. If that value is computed without wrapping and lies below the length, the
   check cannot fail; in R unsigned arithmetic wraps, so the absence of wrapping at the largest
   value also shows that the index is the exact affine value. The emitter therefore writes the
   check as `index >= len && !fits`, where `fits` is that test of the largest value, computed from
   the same values A, S and B the index and the test use and from the length at the check. That
   is the check as written wherever `fits` is false, and a check that cannot fail wherever it is
   true. When A, S, B and the length do not change in the loop, `fits` is the same in every
   iteration; the optimizer computes it once before the loop and runs a copy of the loop without
   the check when it holds, the loop as written otherwise. That copy is made before the default
   pipeline (r_llvm_unswitch_versions): once inlining makes B and S constants, the optimizer can
   prove that the index check implies `!fits` and fold the check back to `index >= len` before its
   unswitching runs, and inductive range check elimination handles unit strides only.

   An index masked by a value, `x & m`, is at most m wherever it runs, so `fits` is `m < len`;
   there is no loop variable to find. Because `fits` follows from the index (a failing check
   implies `!fits`), the optimizer would fold it away; the emitter passes it through llvm.expect,
   which also weights the copy without the check as the likely one, and which the optimizer
   lowers only after the unswitching.

   The dominating test is found on the dominator tree of the MIR blocks. A function whose control
   flow leaves the plain blocks (finally routes, task scopes, cancellation) is not versioned, nor an
   index whose k is borrowed anywhere in the function or stored on a path from the test to the
   index. !r.versioned lists the sources of the indices written this way
   (tests/check_loop_versions.py). */

static const RMirBlock *r_llvm_version_block(const RLlvmEmitter *emitter, uint32_t index) {
    return &emitter->frontend->mir_blocks[(size_t)emitter->mir->first_block + index];
}

static const RMirInstruction *
r_llvm_version_instruction(const RLlvmEmitter *emitter, const RMirBlock *block, uint32_t index) {
    return &emitter->frontend->mir_instructions[(size_t)block->first_instruction + index];
}

/* The successors of a block: the targets of its terminator (a throw's catch block) and the panic
   targets of its instructions. */
static uint32_t r_llvm_version_successors(const RLlvmEmitter *emitter,
                                          uint32_t block_index,
                                          uint32_t *successors,
                                          uint32_t capacity) {
    const RMirBlock *block = r_llvm_version_block(emitter, block_index);
    uint32_t count = 0U;
    uint32_t index;

    for (index = 0U; index < block->instruction_count; ++index) {
        const RMirInstruction *instruction = r_llvm_version_instruction(emitter, block, index);
        const RMirBlockId targets[3] = {instruction->panic_target,
                                        ((instruction->kind == R_MIR_INSTRUCTION_BRANCH) ||
                                         (instruction->kind == R_MIR_INSTRUCTION_JUMP) ||
                                         (instruction->kind == R_MIR_INSTRUCTION_AWAIT) ||
                                         (instruction->kind == R_MIR_INSTRUCTION_THROW))
                                            ? instruction->target0
                                            : R_MIR_BLOCK_ID_INVALID,
                                        ((instruction->kind == R_MIR_INSTRUCTION_BRANCH) ||
                                         (instruction->kind == R_MIR_INSTRUCTION_AWAIT))
                                            ? instruction->target1
                                            : R_MIR_BLOCK_ID_INVALID};
        uint32_t side;
        for (side = 0U; side < 3U; ++side) {
            if ((targets[side] != R_MIR_BLOCK_ID_INVALID) &&
                ((size_t)targets[side] <= emitter->mir->block_count) && (count < capacity)) {
                successors[count++] = targets[side] - 1U;
            }
        }
    }
    return count;
}

/* Whether the control flow of the function is that of its terminators and panic targets alone. */
static bool r_llvm_version_plain(const RLlvmEmitter *emitter) {
    uint32_t block_index;

    for (block_index = 0U; block_index < emitter->mir->block_count; ++block_index) {
        const RMirBlock *block = r_llvm_version_block(emitter, block_index);
        uint32_t index;
        for (index = 0U; index < block->instruction_count; ++index) {
            switch (r_llvm_version_instruction(emitter, block, index)->kind) {
            case R_MIR_INSTRUCTION_PENDING_SET:
            case R_MIR_INSTRUCTION_PENDING_RESUME:
            case R_MIR_INSTRUCTION_FINALLY_PUSH:
            case R_MIR_INSTRUCTION_FINALLY_ENTER:
            case R_MIR_INSTRUCTION_FINALLY_EXIT:
            case R_MIR_INSTRUCTION_TASK_SCOPE_ENTER:
            case R_MIR_INSTRUCTION_TASK_SCOPE_WAIT:
            case R_MIR_INSTRUCTION_TASK_SCOPE_CLOSE:
            case R_MIR_INSTRUCTION_CANCEL:
                return false;
            default:
                break;
            }
        }
    }
    return true;
}

/* The immediate dominators of the blocks (Cooper, Harvey and Kennedy), UINT32_MAX for a block the
   entry does not reach; computed once per function. */
static bool r_llvm_version_dominators(RLlvmEmitter *emitter) {
    const uint32_t count = emitter->mir->block_count;
    uint32_t *order = NULL;
    uint32_t *position = NULL;
    uint32_t *stack = NULL;
    uint32_t *successors = NULL;
    uint32_t *idom;
    uint32_t visited = 0U;
    uint32_t depth = 0U;
    bool changed = true;
    bool success = false;

    if (emitter->version_dominators != NULL) {
        return true;
    }
    idom = r_llvm_allocate(emitter, (size_t)count * sizeof(*idom));
    order = r_llvm_allocate(emitter, (size_t)count * sizeof(*order));
    position = r_llvm_allocate(emitter, (size_t)count * sizeof(*position));
    stack = r_llvm_allocate(emitter, ((size_t)count + 1U) * 2U * sizeof(*stack));
    successors = r_llvm_allocate(emitter, (size_t)count * 4U * sizeof(*successors));
    if ((idom == NULL) || (order == NULL) || (position == NULL) || (stack == NULL) ||
        (successors == NULL)) {
        r_llvm_free(emitter, idom);
        goto cleanup;
    }
    {
        uint32_t index;
        for (index = 0U; index < count; ++index) {
            idom[index] = UINT32_MAX;
            position[index] = UINT32_MAX;
        }
    }
    /* Reverse postorder by an explicit depth-first walk: a frame is a block and the index of its
       next successor. */
    {
        uint8_t *seen = r_llvm_allocate(emitter, count);
        if (seen == NULL) {
            r_llvm_free(emitter, idom);
            goto cleanup;
        }
        (void)memset(seen, 0, count);
        seen[0] = 1U;
        stack[0] = 0U;
        stack[1] = 0U;
        depth = 1U;
        while (depth != 0U) {
            const uint32_t block = stack[(depth - 1U) * 2U];
            const uint32_t next = stack[(depth - 1U) * 2U + 1U];
            const uint32_t total =
                r_llvm_version_successors(emitter, block, successors, count * 4U);
            if (next < total) {
                const uint32_t successor = successors[next];
                stack[(depth - 1U) * 2U + 1U] = next + 1U;
                if (!seen[successor] && (depth <= count)) {
                    seen[successor] = 1U;
                    stack[depth * 2U] = successor;
                    stack[depth * 2U + 1U] = 0U;
                    depth += 1U;
                }
            } else {
                order[visited++] = block;
                depth -= 1U;
            }
        }
        r_llvm_free(emitter, seen);
    }
    {
        uint32_t index;
        /* order holds the postorder; position is the postorder number of each block. */
        for (index = 0U; index < visited; ++index) {
            position[order[index]] = index;
        }
    }
    idom[0] = 0U;
    while (changed) {
        uint32_t walk;
        changed = false;
        /* Reverse postorder, skipping the entry. */
        for (walk = visited; walk-- > 0U;) {
            const uint32_t block = order[walk];
            uint32_t candidate = UINT32_MAX;
            uint32_t predecessor;
            if (block == 0U) {
                continue;
            }
            for (predecessor = 0U; predecessor < count; ++predecessor) {
                const uint32_t total =
                    position[predecessor] == UINT32_MAX
                        ? 0U
                        : r_llvm_version_successors(emitter, predecessor, successors, count * 4U);
                uint32_t side;
                bool edge = false;
                for (side = 0U; side < total; ++side) {
                    edge = edge || (successors[side] == block);
                }
                if (!edge || (idom[predecessor] == UINT32_MAX)) {
                    continue;
                }
                if (candidate == UINT32_MAX) {
                    candidate = predecessor;
                    continue;
                }
                {
                    uint32_t left = candidate;
                    uint32_t right = predecessor;
                    while (left != right) {
                        while (position[left] < position[right]) {
                            left = idom[left];
                        }
                        while (position[right] < position[left]) {
                            right = idom[right];
                        }
                    }
                    candidate = left;
                }
            }
            if ((candidate != UINT32_MAX) && (idom[block] != candidate)) {
                idom[block] = candidate;
                changed = true;
            }
        }
    }
    emitter->version_dominators = idom;
    success = true;

cleanup:
    r_llvm_free(emitter, order);
    r_llvm_free(emitter, position);
    r_llvm_free(emitter, stack);
    r_llvm_free(emitter, successors);
    return success;
}

static bool r_llvm_version_dominates(const RLlvmEmitter *emitter, uint32_t over, uint32_t block) {
    const uint32_t *idom = emitter->version_dominators;

    if (idom[block] == UINT32_MAX) {
        return false;
    }
    while (block != over) {
        if (block == 0U) {
            return false;
        }
        block = idom[block];
    }
    return true;
}

static bool r_llvm_version_is_size(RLlvmEmitter *emitter, RTypeId type) {
    return r_llvm_value_kind(emitter, type) == R_SEMANTIC_TYPE_USIZE;
}

/* The local or parameter a value loads directly, else NULL. */
static const RMirInstruction *r_llvm_version_load(RLlvmEmitter *emitter, RMirValueId id) {
    const RMirInstruction *definition = r_llvm_definition(emitter, id);

    return (definition != NULL) && (definition->kind == R_MIR_INSTRUCTION_LOAD) &&
                   (definition->operand0 == R_MIR_VALUE_ID_INVALID) &&
                   r_llvm_version_is_size(emitter, definition->type)
               ? definition
               : NULL;
}

/* An unsigned affine index: index = offset + load(k) * stride, where offset or stride may be
   absent (0 or 1). */
typedef struct RLlvmAffine {
    const RMirInstruction *k;
    RMirValueId offset;
    RMirValueId stride;
} RLlvmAffine;

static bool r_llvm_version_term(RLlvmEmitter *emitter, RMirValueId id, RLlvmAffine *affine) {
    const RMirInstruction *definition = r_llvm_definition(emitter, id);

    affine->k = r_llvm_version_load(emitter, id);
    affine->stride = R_MIR_VALUE_ID_INVALID;
    if (affine->k != NULL) {
        return true;
    }
    if ((definition == NULL) || (definition->kind != R_MIR_INSTRUCTION_BINARY) ||
        (definition->operation != R_TOKEN_STAR) ||
        !r_llvm_version_is_size(emitter, definition->type)) {
        return false;
    }
    affine->k = r_llvm_version_load(emitter, definition->operand0);
    affine->stride = definition->operand1;
    if (affine->k == NULL) {
        affine->k = r_llvm_version_load(emitter, definition->operand1);
        affine->stride = definition->operand0;
    }
    return affine->k != NULL;
}

/* The candidate decompositions of an index value: up to four (k from either side of a sum). */
static uint32_t r_llvm_version_affines(RLlvmEmitter *emitter, RMirValueId id, RLlvmAffine *out) {
    const RMirInstruction *definition = r_llvm_definition(emitter, id);
    uint32_t count = 0U;

    if ((definition == NULL) || !r_llvm_version_is_size(emitter, definition->type)) {
        return 0U;
    }
    if (r_llvm_version_term(emitter, id, &out[count])) {
        out[count++].offset = R_MIR_VALUE_ID_INVALID;
    }
    if ((definition->kind == R_MIR_INSTRUCTION_BINARY) && (definition->operation == R_TOKEN_PLUS)) {
        if (r_llvm_version_term(emitter, definition->operand1, &out[count])) {
            out[count++].offset = definition->operand0;
        }
        if (r_llvm_version_term(emitter, definition->operand0, &out[count])) {
            out[count++].offset = definition->operand1;
        }
    }
    return count;
}

/* Whether an instruction writes or borrows the place k. */
static bool r_llvm_version_touches(const RMirInstruction *instruction, const RMirInstruction *k) {
    return ((instruction->kind == R_MIR_INSTRUCTION_STORE) ||
            (instruction->kind == R_MIR_INSTRUCTION_BORROW) ||
            (instruction->kind == R_MIR_INSTRUCTION_RAW_ADDRESS) ||
            (instruction->kind == R_MIR_INSTRUCTION_MOVE)) &&
           (instruction->place_ordinal == k->place_ordinal) &&
           (instruction->place_is_parameter == k->place_is_parameter);
}

/* The block and position of an instruction of the function. */
static bool r_llvm_version_locate(const RLlvmEmitter *emitter,
                                  const RMirInstruction *instruction,
                                  uint32_t *block_out,
                                  uint32_t *index_out) {
    uint32_t block_index;

    for (block_index = 0U; block_index < emitter->mir->block_count; ++block_index) {
        const RMirBlock *block = r_llvm_version_block(emitter, block_index);
        if ((instruction >= r_llvm_version_instruction(emitter, block, 0U)) &&
            (instruction <
             r_llvm_version_instruction(emitter, block, 0U) + block->instruction_count)) {
            *block_out = block_index;
            *index_out = (uint32_t)(instruction - r_llvm_version_instruction(emitter, block, 0U));
            return true;
        }
    }
    return false;
}

/* Whether nothing writes k on a path from the end of block `test` to `index` of block `at`: no
   block that reaches `at` without passing `test` touches k, nor does `at` before the index. */
static bool r_llvm_version_unchanged(
    RLlvmEmitter *emitter, const RMirInstruction *k, uint32_t test, uint32_t at, uint32_t index) {
    const uint32_t count = emitter->mir->block_count;
    uint8_t *reaches = r_llvm_allocate(emitter, count);
    uint32_t *successors = r_llvm_allocate(emitter, (size_t)count * 4U * sizeof(*successors));
    bool changed = true;
    bool unchanged = true;
    uint32_t block_index;

    if ((reaches == NULL) || (successors == NULL)) {
        r_llvm_free(emitter, reaches);
        r_llvm_free(emitter, successors);
        return false;
    }
    (void)memset(reaches, 0, count);
    /* reaches[b]: b reaches `at` along blocks other than `test`. */
    while (changed) {
        changed = false;
        for (block_index = 0U; block_index < count; ++block_index) {
            uint32_t total;
            uint32_t side;
            if (reaches[block_index] || (block_index == test)) {
                continue;
            }
            total = r_llvm_version_successors(emitter, block_index, successors, count * 4U);
            for (side = 0U; side < total; ++side) {
                if ((successors[side] == at) || reaches[successors[side]]) {
                    reaches[block_index] = 1U;
                    changed = true;
                    break;
                }
            }
        }
    }
    for (block_index = 0U; unchanged && (block_index < count); ++block_index) {
        const RMirBlock *block = r_llvm_version_block(emitter, block_index);
        const uint32_t limit = block_index == at ? (reaches[at] ? block->instruction_count : index)
                               : reaches[block_index] ? block->instruction_count
                                                      : 0U;
        uint32_t instruction_index;
        for (instruction_index = 0U; instruction_index < limit; ++instruction_index) {
            if (r_llvm_version_touches(
                    r_llvm_version_instruction(emitter, block, instruction_index), k)) {
                unchanged = false;
                break;
            }
        }
    }
    r_llvm_free(emitter, reaches);
    r_llvm_free(emitter, successors);
    return unchanged;
}

/* Whether k is borrowed or its address taken anywhere in the function. */
static bool r_llvm_version_borrowed(const RLlvmEmitter *emitter, const RMirInstruction *k) {
    uint32_t block_index;

    for (block_index = 0U; block_index < emitter->mir->block_count; ++block_index) {
        const RMirBlock *block = r_llvm_version_block(emitter, block_index);
        uint32_t index;
        for (index = 0U; index < block->instruction_count; ++index) {
            const RMirInstruction *instruction = r_llvm_version_instruction(emitter, block, index);
            if (((instruction->kind == R_MIR_INSTRUCTION_BORROW) ||
                 (instruction->kind == R_MIR_INSTRUCTION_RAW_ADDRESS)) &&
                (instruction->place_ordinal == k->place_ordinal) &&
                (instruction->place_is_parameter == k->place_is_parameter)) {
                return true;
            }
        }
    }
    return false;
}

/* The bound B of a test `load(k) < B` that ends block `test` and whose true edge leads to a block
   that dominates `at` and has `test` as its only predecessor through that edge, else
   R_MIR_VALUE_ID_INVALID. */
static RMirValueId r_llvm_version_bound(RLlvmEmitter *emitter,
                                        uint32_t test,
                                        uint32_t at,
                                        const RMirInstruction *k,
                                        uint32_t *entered) {
    const RMirBlock *block = r_llvm_version_block(emitter, test);
    const RMirInstruction *branch;
    const RMirInstruction *compare;
    const RMirInstruction *loaded;
    uint32_t predecessor;
    uint32_t *successors;
    uint32_t edges = 0U;

    if (block->instruction_count == 0U) {
        return R_MIR_VALUE_ID_INVALID;
    }
    branch = r_llvm_version_instruction(emitter, block, block->instruction_count - 1U);
    if ((branch->kind != R_MIR_INSTRUCTION_BRANCH) || (branch->target0 == R_MIR_BLOCK_ID_INVALID) ||
        (branch->target0 == branch->target1) ||
        !r_llvm_version_dominates(emitter, branch->target0 - 1U, at)) {
        return R_MIR_VALUE_ID_INVALID;
    }
    compare = r_llvm_definition(emitter, branch->operand0);
    if ((compare == NULL) || (compare->kind != R_MIR_INSTRUCTION_BINARY) ||
        (compare->operation != R_TOKEN_LESS)) {
        return R_MIR_VALUE_ID_INVALID;
    }
    loaded = r_llvm_version_load(emitter, compare->operand0);
    if ((loaded == NULL) || (loaded->place_ordinal != k->place_ordinal) ||
        (loaded->place_is_parameter != k->place_is_parameter) ||
        !r_llvm_version_is_size(emitter,
                                r_llvm_definition(emitter, compare->operand1) == NULL
                                    ? R_TYPE_ID_INVALID
                                    : r_llvm_definition(emitter, compare->operand1)->type)) {
        return R_MIR_VALUE_ID_INVALID;
    }
    /* The true target is entered from the test alone, so reaching it means the test held. */
    successors =
        r_llvm_allocate(emitter, (size_t)emitter->mir->block_count * 4U * sizeof(*successors));
    if (successors == NULL) {
        return R_MIR_VALUE_ID_INVALID;
    }
    for (predecessor = 0U; predecessor < emitter->mir->block_count; ++predecessor) {
        const uint32_t total = r_llvm_version_successors(
            emitter, predecessor, successors, emitter->mir->block_count * 4U);
        uint32_t side;
        for (side = 0U; side < total; ++side) {
            edges += successors[side] == branch->target0 - 1U ? 1U : 0U;
        }
    }
    r_llvm_free(emitter, successors);
    if (edges != 1U) {
        return R_MIR_VALUE_ID_INVALID;
    }
    /* The load of k for the test and the end of its block: nothing writes k in between. */
    {
        uint32_t loaded_block = 0U;
        uint32_t loaded_index = 0U;
        uint32_t index;
        if (!r_llvm_version_locate(emitter, loaded, &loaded_block, &loaded_index) ||
            (loaded_block != test)) {
            return R_MIR_VALUE_ID_INVALID;
        }
        for (index = loaded_index + 1U; index < block->instruction_count; ++index) {
            if (r_llvm_version_touches(r_llvm_version_instruction(emitter, block, index), k)) {
                return R_MIR_VALUE_ID_INVALID;
            }
        }
    }
    *entered = branch->target0 - 1U;
    return compare->operand1;
}

/* `fits` of a versioned index through llvm.expect, listed in !r.versioned, and the mark of its
   function for r_llvm_unswitch_versions. */
static LLVMValueRef
r_llvm_version_mark(RLlvmEmitter *emitter, const RMirInstruction *instruction, LLVMValueRef fits) {
    LLVMTypeRef flag = r_llvm_int(emitter, 1U);
    LLVMValueRef expect = LLVMGetIntrinsicDeclaration(
        emitter->module, LLVMLookupIntrinsicID("llvm.expect", 11U), &flag, 1U);
    LLVMValueRef arguments[2];
    LLVMMetadataRef span[3];

    arguments[0] = fits;
    arguments[1] = LLVMConstInt(flag, 1U, 0);
    span[0] = LLVMValueAsMetadata(
        r_llvm_u32(emitter, r_llvm_source_key(emitter, instruction->span.source)));
    span[1] = LLVMValueAsMetadata(r_llvm_u32(emitter, instruction->span.start));
    span[2] = LLVMValueAsMetadata(r_llvm_u32(emitter, instruction->span.end));
    LLVMAddNamedMetadataOperand(
        emitter->module,
        "r.versioned",
        LLVMMetadataAsValue(emitter->context, LLVMMDNodeInContext2(emitter->context, span, 3U)));
    LLVMAddAttributeAtIndex(
        emitter->function,
        (LLVMAttributeIndex)LLVMAttributeFunctionIndex,
        LLVMCreateStringAttribute(
            emitter->context, R_LLVM_VERSIONED, (unsigned)strlen(R_LLVM_VERSIONED), "", 0U));
    return LLVMBuildCall2(
        emitter->builder, LLVMGlobalGetValueType(expect), expect, arguments, 2U, "");
}

/* `x & m`: the mask m, a constant operand if there is one, else the right one; NULL for any
   other index. */
static LLVMValueRef r_llvm_version_mask(RLlvmEmitter *emitter, RMirValueId id) {
    const RMirInstruction *definition = r_llvm_definition(emitter, id);
    const RMirInstruction *left;

    if ((definition == NULL) || (definition->kind != R_MIR_INSTRUCTION_BINARY) ||
        ((definition->operation != R_TOKEN_AMP) && (definition->operation != R_TOKEN_AMP_EQUAL)) ||
        !r_llvm_version_is_size(emitter, definition->type)) {
        return NULL;
    }
    left = r_llvm_definition(emitter, definition->operand0);
    return r_llvm_value(emitter,
                        (left != NULL) && (left->kind == R_MIR_INSTRUCTION_CONSTANT)
                            ? definition->operand0
                            : definition->operand1);
}

/* `fits` of a versioned index: the bound is at least 1 and offset + (bound - 1) * stride is
   computed without wrapping and lies below the length; the index is listed in !r.versioned. */
static LLVMValueRef r_llvm_version_build(RLlvmEmitter *emitter,
                                         const RMirInstruction *instruction,
                                         const RLlvmAffine *affine,
                                         RMirValueId bound,
                                         LLVMValueRef length) {
    LLVMTypeRef wide = r_llvm_int(emitter, 64U);
    LLVMValueRef limit = r_llvm_value(emitter, bound);
    LLVMValueRef offset = affine->offset == R_MIR_VALUE_ID_INVALID
                              ? r_llvm_u64(emitter, 0U)
                              : r_llvm_value(emitter, affine->offset);
    LLVMValueRef stride = affine->stride == R_MIR_VALUE_ID_INVALID
                              ? r_llvm_u64(emitter, 1U)
                              : r_llvm_value(emitter, affine->stride);
    LLVMValueRef product;
    LLVMValueRef top;
    LLVMValueRef fits;
    LLVMValueRef arguments[2];
    LLVMValueRef multiply;
    LLVMValueRef add;

    if ((limit == NULL) || (offset == NULL) || (stride == NULL)) {
        return NULL;
    }
    multiply = LLVMGetIntrinsicDeclaration(
        emitter->module, LLVMLookupIntrinsicID("llvm.umul.with.overflow", 23U), &wide, 1U);
    add = LLVMGetIntrinsicDeclaration(
        emitter->module, LLVMLookupIntrinsicID("llvm.uadd.with.overflow", 23U), &wide, 1U);
    arguments[0] = LLVMBuildSub(emitter->builder, limit, r_llvm_u64(emitter, 1U), "");
    arguments[1] = stride;
    product = LLVMBuildCall2(
        emitter->builder, LLVMGlobalGetValueType(multiply), multiply, arguments, 2U, "");
    arguments[0] = offset;
    arguments[1] = LLVMBuildExtractValue(emitter->builder, product, 0U, "");
    top = LLVMBuildCall2(emitter->builder, LLVMGlobalGetValueType(add), add, arguments, 2U, "");
    fits =
        LLVMBuildAnd(emitter->builder,
                     LLVMBuildICmp(emitter->builder, LLVMIntNE, limit, r_llvm_u64(emitter, 0U), ""),
                     LLVMBuildICmp(emitter->builder,
                                   LLVMIntULT,
                                   LLVMBuildExtractValue(emitter->builder, top, 0U, ""),
                                   length,
                                   ""),
                     "");
    fits = LLVMBuildAnd(
        emitter->builder,
        fits,
        LLVMBuildNot(emitter->builder,
                     LLVMBuildOr(emitter->builder,
                                 LLVMBuildExtractValue(emitter->builder, product, 1U, ""),
                                 LLVMBuildExtractValue(emitter->builder, top, 1U, ""),
                                 ""),
                     ""),
        "");
    return r_llvm_version_mark(emitter, instruction, fits);
}

bool r_llvm_unswitch_versions(RLlvmEmitter *emitter, bool run) {
    LLVMValueRef function;

    for (function = LLVMGetFirstFunction(emitter->module); function != NULL;
         function = LLVMGetNextFunction(function)) {
        LLVMPassBuilderOptionsRef options;
        LLVMErrorRef error;
        if (LLVMGetStringAttributeAtIndex(function,
                                          (LLVMAttributeIndex)LLVMAttributeFunctionIndex,
                                          R_LLVM_VERSIONED,
                                          (unsigned)strlen(R_LLVM_VERSIONED)) == NULL) {
            continue;
        }
        LLVMRemoveStringAttributeAtIndex(function,
                                         (LLVMAttributeIndex)LLVMAttributeFunctionIndex,
                                         R_LLVM_VERSIONED,
                                         (unsigned)strlen(R_LLVM_VERSIONED));
        if (!run) {
            continue;
        }
        options = LLVMCreatePassBuilderOptions();
        error =
            LLVMRunPassesOnFunction(function,
                                    "sroa<modify-cfg>,early-cse,instcombine<no-verify-fixpoint>,"
                                    "simplifycfg,loop-mssa(licm,simple-loop-unswitch<nontrivial>)",
                                    emitter->machine,
                                    options);
        LLVMDisposePassBuilderOptions(options);
        if (error != NULL) {
            char *message = LLVMGetErrorMessage(error);
            (void)fprintf(stderr, "r-front: the LLVM optimizer failed: %s\n", message);
            LLVMDisposeErrorMessage(message);
            return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
    }
    return true;
}

LLVMValueRef r_llvm_version_mask_fits(RLlvmEmitter *emitter,
                                      const RMirInstruction *instruction,
                                      RMirValueId index,
                                      LLVMValueRef length) {
    LLVMValueRef mask = r_llvm_version_mask(emitter, index);

    return mask == NULL
               ? NULL
               : r_llvm_version_mark(emitter,
                                     instruction,
                                     LLVMBuildICmp(emitter->builder, LLVMIntULT, mask, length, ""));
}

/* The `fits` test of an index instruction, or NULL when the index is not versioned. */
LLVMValueRef r_llvm_version_fits(RLlvmEmitter *emitter,
                                 const RMirInstruction *instruction,
                                 LLVMValueRef length) {
    RLlvmAffine affines[4];
    uint32_t at = 0U;
    uint32_t position = 0U;
    uint32_t count;
    uint32_t candidate;
    LLVMValueRef mask =
        r_llvm_version_mask_fits(emitter, instruction, instruction->operand1, length);

    if (mask != NULL) {
        return mask;
    }
    if ((emitter->mir == NULL) || !r_llvm_version_plain(emitter) ||
        !r_llvm_version_locate(emitter, instruction, &at, &position) ||
        !r_llvm_version_dominators(emitter)) {
        return NULL;
    }
    count = r_llvm_version_affines(emitter, instruction->operand1, affines);
    /* The tests that dominate the index, nearest first: the nearest test of any decomposition's
       k belongs to the innermost loop, whose largest index is the one worth testing once. */
    {
        uint32_t test = at;
        while ((count != 0U) && (emitter->version_dominators[test] != UINT32_MAX) && (test != 0U)) {
            test = emitter->version_dominators[test];
            for (candidate = 0U; candidate < count; ++candidate) {
                const RMirInstruction *k = affines[candidate].k;
                uint32_t k_block = 0U;
                uint32_t k_index = 0U;
                uint32_t entered = 0U;
                const RMirValueId bound = r_llvm_version_bound(emitter, test, at, k, &entered);
                /* The load of k the index uses follows the test along its true edge, and nothing
                   writes k from the test to that load, so it reads the value the test compared. */
                if ((bound == R_MIR_VALUE_ID_INVALID) || r_llvm_version_borrowed(emitter, k) ||
                    !r_llvm_version_locate(emitter, k, &k_block, &k_index) ||
                    !r_llvm_version_dominates(emitter, entered, k_block) ||
                    !r_llvm_version_unchanged(emitter, k, test, k_block, k_index)) {
                    continue;
                }
                return r_llvm_version_build(
                    emitter, instruction, &affines[candidate], bound, length);
            }
        }
    }
    return NULL;
}

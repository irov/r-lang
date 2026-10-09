#include "emit_internal.h"
#include "standard_net_operations.h"

#include <string.h>

/* Standard outcomes the library stores flat (r_c17_standard_outcome_abi): a native structure with
   a `kind` member and one member per datum, such as RStdIoReadResult {kind, buffer, count,
   error}. A variant's payload is the owner member itself, or an aggregate that takes the owner
   member as one field and the other fields from scalar members. Binding the payload moves the
   owner out; the outcome is then cleared and no longer initialized. */

#define R_LLVM_OUTCOME_WHOLE UINT32_MAX

typedef struct RLlvmOutcomeVariant {
    const char *native_tag;
    /* The member the payload takes ownership of; NULL for a variant without payload. */
    const char *owner;
    /* The payload field that receives it, or R_LLVM_OUTCOME_WHOLE for the payload itself. */
    uint32_t owner_field;
    /* The member of every other payload field, by field index. */
    const char *members[4];
} RLlvmOutcomeVariant;

typedef struct RLlvmOutcome {
    const char *name;
    const char *native_type;
    uint32_t variant_count;
    RLlvmOutcomeVariant variants[3];
} RLlvmOutcome;

/* r_c17_standard_outcome_payload_layout and r_c17_standard_outcome_scalar_member as a table. */
static const RLlvmOutcome r_llvm_outcomes[] = {
    {"std.io::read_result",
     "RStdIoReadResult",
     3U,
     {{"R_STD_IO_READ_RESULT_READ", "buffer", 0U, {NULL, "count"}},
      {"R_STD_IO_READ_RESULT_END", "buffer", R_LLVM_OUTCOME_WHOLE, {NULL}},
      {"R_STD_IO_READ_RESULT_FAILED", "buffer", 2U, {"error", "count"}}}},
    {"std.io::write_result",
     "RStdIoWriteResult",
     2U,
     {{"R_STD_IO_WRITE_RESULT_WRITTEN", "buffer", 0U, {NULL, "count"}},
      {"R_STD_IO_WRITE_RESULT_FAILED", "buffer", 2U, {"error", "count"}}}},
    {"std.io::write_all_result",
     "RStdIoWriteAllResult",
     2U,
     {{"R_STD_IO_WRITE_ALL_RESULT_WRITTEN", "buffer", R_LLVM_OUTCOME_WHOLE, {NULL}},
      {"R_STD_IO_WRITE_ALL_RESULT_FAILED", "buffer", 2U, {"error", "written"}}}},
    {"std.io::shared_write_result",
     "RStdIoSharedWriteResult",
     2U,
     {{"R_STD_IO_SHARED_WRITE_RESULT_WRITTEN", "buffer", R_LLVM_OUTCOME_WHOLE, {NULL}},
      {"R_STD_IO_SHARED_WRITE_RESULT_FAILED", "buffer", 2U, {"error", "written"}}}},
    {"std.fs::write_file_result",
     "RStdFsWriteFileResult",
     2U,
     {{"R_STD_FS_WRITE_FILE_COMMITTED", "data", R_LLVM_OUTCOME_WHOLE, {NULL}},
      {"R_STD_FS_WRITE_FILE_FAILED", "data", 1U, {"error"}}}},
    {"std.fs::directory_next_result",
     "RStdFsDirectoryNextResult",
     3U,
     {{"R_STD_FS_DIRECTORY_NEXT_ENTRY", "iterator", 0U, {NULL, "entry"}},
      {"R_STD_FS_DIRECTORY_NEXT_END", NULL, 0U, {NULL}},
      {"R_STD_FS_DIRECTORY_NEXT_FAILED", "iterator", 1U, {"error"}}}},
    {"std.string::from_bytes_result",
     "RStdStringFromBytesResult",
     2U,
     {{"R_STD_STRING_FROM_BYTES_VALID", "valid", R_LLVM_OUTCOME_WHOLE, {NULL}},
      {"R_STD_STRING_FROM_BYTES_INVALID", "invalid_bytes", 0U, {NULL, "invalid_index"}}}},
    {"std.process::spawn_result",
     "RStdProcessSpawnResult",
     2U,
     {{"R_STD_PROCESS_SPAWN_RESULT_SPAWNED", "child", R_LLVM_OUTCOME_WHOLE, {NULL}},
      {"R_STD_PROCESS_SPAWN_RESULT_FAILED", "command", 1U, {"error"}}}},
    {"std.process::wait_result",
     "RStdProcessWaitResult",
     2U,
     {{"R_STD_PROCESS_WAIT_RESULT_EXITED", "status", R_LLVM_OUTCOME_WHOLE, {NULL}},
      {"R_STD_PROCESS_WAIT_RESULT_FAILED", "child", 1U, {"error"}}}},
};

/* The outcome of a standard type without type arguments; a std.net outcome is built from its
   descriptor (a variant without fields hands back the buffer). */
static bool r_llvm_outcome(RLlvmEmitter *emitter, RTypeId id, RLlvmOutcome *outcome) {
    const RSemanticType *type = r_llvm_type(emitter, r_llvm_value_type(emitter, id));
    size_t index;

    if ((type == NULL) || (type->kind != R_SEMANTIC_TYPE_STANDARD) ||
        (type->base != R_TYPE_ID_INVALID) || (type->second != R_TYPE_ID_INVALID) ||
        (type->flags != R_SEMANTIC_TYPE_FLAG_NONE)) {
        return false;
    }
    for (index = 0U; index < sizeof(r_llvm_outcomes) / sizeof(r_llvm_outcomes[0]); ++index) {
        if (r_llvm_standard_named(emitter, id, r_llvm_outcomes[index].name)) {
            *outcome = r_llvm_outcomes[index];
            return true;
        }
    }
    for (index = 0U; r_standard_net_outcome_at(index) != NULL; ++index) {
        const RStandardNetOutcome *net = r_standard_net_outcome_at(index);
        uint32_t variant;
        if (!r_llvm_standard_named(emitter, id, net->name)) {
            continue;
        }
        (void)memset(outcome, 0, sizeof(*outcome));
        outcome->name = net->name;
        outcome->native_type = net->native_type;
        outcome->variant_count = net->variant_count;
        for (variant = 0U; variant < net->variant_count; ++variant) {
            const RStandardNetOutcomeVariant *source = &net->variants[variant];
            RLlvmOutcomeVariant *target = &outcome->variants[variant];
            uint32_t field;
            target->native_tag = source->native_tag;
            target->owner = "buffer";
            target->owner_field = R_LLVM_OUTCOME_WHOLE;
            for (field = 0U; field < source->field_count; ++field) {
                if (strcmp(source->fields[field].name, "buffer") == 0) {
                    target->owner_field = field;
                } else {
                    target->members[field] = source->fields[field].native_member;
                }
            }
        }
        return true;
    }
    return false;
}

/* Whether the native kinds are the variant indices, which VARIANT_TAG reports. */
static bool r_llvm_outcome_tags(RLlvmEmitter *emitter, const RLlvmOutcome *outcome) {
    uint32_t variant;

    for (variant = 0U; variant < outcome->variant_count; ++variant) {
        int64_t value = -1;
        if (!r_llvm_runtime_constant(emitter, outcome->variants[variant].native_tag, &value)) {
            return false;
        }
        if (value != (int64_t)variant) {
            return r_llvm_unsupported(emitter,
                                      "a standard outcome whose kinds are not its variants");
        }
    }
    return true;
}

/* The address of a member of the native outcome. */
static LLVMValueRef r_llvm_outcome_member(RLlvmEmitter *emitter,
                                          const RLlvmOutcome *outcome,
                                          LLVMValueRef address,
                                          const char *member) {
    uint32_t offset = 0U;

    if (!r_llvm_runtime_field(emitter, outcome->native_type, member, &offset, NULL)) {
        return NULL;
    }
    return r_llvm_byte_offset(emitter, address, offset);
}

static bool r_llvm_outcome_copy(RLlvmEmitter *emitter,
                                RTypeId type,
                                LLVMValueRef destination,
                                LLVMValueRef source) {
    uint32_t size = 0U;
    uint32_t align = 0U;

    if ((source == NULL) || !r_llvm_layout(emitter, type, &size, &align)) {
        return false;
    }
    if (size != 0U) {
        (void)LLVMBuildMemCpy(
            emitter->builder, destination, align, source, 1U, r_llvm_u64(emitter, size));
    }
    return true;
}

/* L37.5 (r_c17_mir_outcome_view): a payload binding of a borrowed outcome borrows the owner
   member itself, or a shadow of the payload aggregate built bitwise from the members. Neither the
   shadow nor the outcome changes owner: the borrowed outcome keeps its resources. */
static bool r_llvm_outcome_view(RLlvmEmitter *emitter,
                                const RMirInstruction *instruction,
                                const RLlvmOutcome *outcome,
                                LLVMValueRef address) {
    const RSemanticType *binding =
        r_llvm_type(emitter, r_llvm_value_type(emitter, instruction->type));
    const RLlvmOutcomeVariant *variant;
    const RSemanticAggregate *aggregate;
    LLVMValueRef shadow;
    uint32_t size = 0U;
    uint32_t align = 0U;
    uint32_t field;

    if ((binding == NULL) || (binding->kind != R_SEMANTIC_TYPE_BORROW) ||
        (instruction->integer_value >= outcome->variant_count)) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    variant = &outcome->variants[instruction->integer_value];
    if (variant->owner == NULL) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    if (variant->owner_field == R_LLVM_OUTCOME_WHOLE) {
        LLVMValueRef member = r_llvm_outcome_member(emitter, outcome, address, variant->owner);
        return (member != NULL) && r_llvm_set_value(emitter, instruction->result, member);
    }
    aggregate = r_llvm_aggregate(emitter, binding->base);
    if ((aggregate == NULL) || (variant->owner_field >= aggregate->field_count) ||
        (aggregate->field_count > 4U) || !r_llvm_layout(emitter, binding->base, &size, &align)) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    shadow = r_llvm_entry_alloca(emitter, size, align, "outcome_view");
    (void)LLVMBuildMemSet(emitter->builder,
                          shadow,
                          LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                          r_llvm_u64(emitter, size),
                          align);
    for (field = 0U; field < aggregate->field_count; ++field) {
        const uint32_t field_id = aggregate->first_field + field + 1U;
        const RSemanticField *semantic = r_llvm_field(emitter, field_id);
        const char *member =
            field == variant->owner_field ? variant->owner : variant->members[field];
        uint32_t offset = 0U;
        if ((semantic == NULL) || (member == NULL)) {
            return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
        if (!r_llvm_field_offset(emitter, field_id, &offset) ||
            !r_llvm_outcome_copy(emitter,
                                 semantic->type,
                                 r_llvm_byte_offset(emitter, shadow, offset),
                                 r_llvm_outcome_member(emitter, outcome, address, member))) {
            return false;
        }
    }
    return r_llvm_set_value(emitter, instruction->result, shadow);
}

/* VARIANT_TAG and VARIANT_PAYLOAD of a flat standard outcome; false in *handled for any other
   tagged operand. */
bool r_llvm_emit_outcome_variant(RLlvmEmitter *emitter,
                                 const RMirInstruction *instruction,
                                 bool *handled) {
    const RMirInstruction *definition = r_llvm_definition(emitter, instruction->operand0);
    const RSemanticType *type =
        definition == NULL ? NULL
                           : r_llvm_type(emitter, r_llvm_value_type(emitter, definition->type));
    const bool borrowed = (type != NULL) && (type->kind == R_SEMANTIC_TYPE_BORROW);
    RLlvmOutcome outcome;
    const RLlvmOutcomeVariant *variant;
    const RSemanticAggregate *aggregate = NULL;
    LLVMValueRef address;
    LLVMValueRef destination;
    uint32_t native_size = 0U;
    uint32_t native_align = 0U;
    uint32_t size = 0U;
    uint32_t align = 0U;
    uint32_t field;

    *handled = false;
    if ((type == NULL) ||
        !r_llvm_outcome(emitter, borrowed ? type->base : definition->type, &outcome)) {
        return true;
    }
    *handled = true;
    if (!r_llvm_outcome_tags(emitter, &outcome)) {
        return false;
    }
    address = r_llvm_value(emitter, instruction->operand0);
    if (address == NULL) {
        return false;
    }
    if (instruction->kind == R_MIR_INSTRUCTION_VARIANT_TAG) {
        LLVMValueRef kind = r_llvm_outcome_member(emitter, &outcome, address, "kind");
        return (kind != NULL) &&
               r_llvm_set_value(
                   emitter,
                   instruction->result,
                   LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), kind, ""));
    }
    if (borrowed) {
        return r_llvm_outcome_view(emitter, instruction, &outcome, address);
    }
    if (instruction->operation != R_TOKEN_KW_MOVE) {
        return r_llvm_unsupported(emitter, "a binding of a standard outcome by copy");
    }
    if (instruction->integer_value >= outcome.variant_count) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    variant = &outcome.variants[instruction->integer_value];
    if (variant->owner == NULL) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    if (variant->owner_field != R_LLVM_OUTCOME_WHOLE) {
        aggregate = r_llvm_aggregate(emitter, instruction->type);
        if ((aggregate == NULL) || (variant->owner_field >= aggregate->field_count) ||
            (aggregate->field_count > 4U)) {
            return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
    }
    destination = r_llvm_value_memory(emitter, instruction->result, instruction->type);
    if ((destination == NULL) || !r_llvm_layout(emitter, instruction->type, &size, &align) ||
        !r_llvm_runtime_layout(emitter, outcome.native_type, &native_size, &native_align)) {
        return false;
    }
    (void)LLVMBuildMemSet(emitter->builder,
                          destination,
                          LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                          r_llvm_u64(emitter, size),
                          align);
    if (aggregate == NULL) {
        if (!r_llvm_outcome_copy(
                emitter,
                instruction->type,
                destination,
                r_llvm_outcome_member(emitter, &outcome, address, variant->owner))) {
            return false;
        }
    } else {
        for (field = 0U; field < aggregate->field_count; ++field) {
            const uint32_t field_id = aggregate->first_field + field + 1U;
            const RSemanticField *semantic = r_llvm_field(emitter, field_id);
            const char *member =
                field == variant->owner_field ? variant->owner : variant->members[field];
            uint32_t offset = 0U;
            if ((semantic == NULL) || (member == NULL)) {
                return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
            }
            if (!r_llvm_field_offset(emitter, field_id, &offset) ||
                !r_llvm_outcome_copy(emitter,
                                     semantic->type,
                                     r_llvm_byte_offset(emitter, destination, offset),
                                     r_llvm_outcome_member(emitter, &outcome, address, member))) {
                return false;
            }
        }
    }
    /* The owner moved: the outcome holds nothing to drop. */
    (void)LLVMBuildMemSet(emitter->builder,
                          address,
                          LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                          r_llvm_u64(emitter, native_size),
                          native_align);
    r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, instruction->result), true);
    r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, instruction->operand0), false);
    return true;
}

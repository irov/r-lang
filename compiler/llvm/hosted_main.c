#include "emit_internal.h"

#include <stdio.h>
#include <string.h>

/* The hosted entry of a program (R-FUNC-0008, R-IDB-023): the process `main` that starts the
   runtime, calls the R entry, and turns its outcome into the process status. It does what the
   C17 emitter's hosted main does, in the same order. */

/* R-FUNC-0008: the exact errors of the implicit main set and their portable projection. */
static const struct {
    const char *name;
    const char *domain;
    const char *member;
    unsigned base;
    bool native;
} r_llvm_main_errors[] = {
#define R_MAIN_ERROR(name, domain, member, base, native) {name, domain, member, base, native},
#include "../semantic/main_errors.def"
#undef R_MAIN_ERROR
};

/* The domains of std.error and the process status each one ends main with. */
static const struct {
    const char *name;
    const char *constant;
    unsigned status;
} r_llvm_main_domains[] = {
    {"allocation", "R_STD_ERROR_DOMAIN_ALLOCATION", 112U},
    {"async_runtime", "R_STD_ERROR_DOMAIN_ASYNC_RUNTIME", 116U},
    {"bytes", "R_STD_ERROR_DOMAIN_BYTES", 117U},
    {"string", "R_STD_ERROR_DOMAIN_STRING", 117U},
    {"conversion", "R_STD_ERROR_DOMAIN_CONVERSION", 115U},
    {"format", "R_STD_ERROR_DOMAIN_FORMAT", 115U},
    {"math", "R_STD_ERROR_DOMAIN_MATH", 117U},
    {"time", "R_STD_ERROR_DOMAIN_TIME", 117U},
    {"environment", "R_STD_ERROR_DOMAIN_ENVIRONMENT", 117U},
    {"io", "R_STD_ERROR_DOMAIN_IO", 113U},
    {"filesystem", "R_STD_ERROR_DOMAIN_FILESYSTEM", 113U},
    {"network", "R_STD_ERROR_DOMAIN_NETWORK", 114U},
    {"process", "R_STD_ERROR_DOMAIN_PROCESS", 117U},
    {"threading", "R_STD_ERROR_DOMAIN_THREADING", 116U},
    {"c_abi", "R_STD_ERROR_DOMAIN_C_ABI", 117U},
};

RSymbolId r_llvm_entry(RLlvmEmitter *emitter) {
    const RFrontendContext *context = emitter->frontend;
    RSymbolId found = R_SYMBOL_ID_INVALID;
    size_t index;

    for (index = 0U; index < context->semantic_symbol_count; ++index) {
        const RSemanticSymbol *symbol = &context->semantic_symbols[index];
        const RSource *source = r_get_source_const(context, symbol->module_source);
        if ((symbol->kind != R_SEMANTIC_SYMBOL_FUNCTION) || symbol->is_protected ||
            symbol->is_unsafe || symbol->is_extern_c || (symbol->generic_origin != 0U) ||
            symbol->is_synthetic || (source == NULL) ||
            !r_source_text_equal(source, symbol->name_span, "main") ||
            (r_llvm_value_kind(emitter, symbol->return_type) != R_SEMANTIC_TYPE_I32)) {
            continue;
        }
        if (found != R_SYMBOL_ID_INVALID) {
            return R_SYMBOL_ID_INVALID;
        }
        found = (RSymbolId)(index + 1U);
    }
    return found;
}

/* Calls r_runtime_panic, which never returns. */
static bool r_llvm_contract_panic(RLlvmEmitter *emitter, const char *category, RSourceSpan span) {
    int64_t value = 0;
    LLVMValueRef arguments[2];

    if (!r_llvm_runtime_constant(emitter, category, &value)) {
        return false;
    }
    arguments[0] = r_llvm_u32(emitter, (uint64_t)value);
    arguments[1] = r_llvm_span(emitter, span);
    if ((arguments[1] == NULL) ||
        (r_llvm_call_runtime(emitter, "r_runtime_panic", arguments, 2U, NULL) == NULL)) {
        return false;
    }
    (void)LLVMBuildUnreachable(emitter->builder);
    return true;
}

/* An integer of a runtime type in memory, converted to u32 as a C cast would. */
static LLVMValueRef r_llvm_load_as_u32(RLlvmEmitter *emitter, LLVMValueRef pointer, uint32_t type) {
    const RLlvmSurfaceType *surface = r_llvm_surface_type(type);
    const RLlvmSurfaceType *integer = surface;
    LLVMValueRef value;

    if ((surface != NULL) && (surface->kind == R_LLVM_SURFACE_ENUM)) {
        integer = r_llvm_surface_type(surface->target);
    }
    if ((integer == NULL) ||
        ((integer->kind != R_LLVM_SURFACE_INTEGER) && (integer->kind != R_LLVM_SURFACE_BOOL)) ||
        (integer->size == 0U) || (integer->size > 8U)) {
        (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        return NULL;
    }
    value = LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, integer->size * 8U), pointer, "");
    if (integer->size > 4U) {
        return LLVMBuildTrunc(emitter->builder, value, r_llvm_int(emitter, 32U), "");
    }
    if (integer->size < 4U) {
        return (integer->flags & R_LLVM_SURFACE_SIGNED) != 0U
                   ? LLVMBuildSExt(emitter->builder, value, r_llvm_int(emitter, 32U), "")
                   : LLVMBuildZExt(emitter->builder, value, r_llvm_int(emitter, 32U), "");
    }
    return value;
}

typedef struct RLlvmMainError {
    LLVMValueRef memory; /* RStdError */
    uint32_t domain;
    uint32_t code;
    uint32_t native_code;
    uint32_t size;
    uint32_t align;
} RLlvmMainError;

/* Forms the portable error of one tag of the outcome in `error->memory`. */
static bool r_llvm_main_error_case(RLlvmEmitter *emitter,
                                   const RLlvmMainError *error,
                                   size_t which,
                                   LLVMValueRef payload,
                                   const char *c_type) {
    LLVMValueRef code;
    int64_t domain = 0;
    uint32_t payload_type = 0U;
    const RLlvmSurfaceName *payload_typedef = r_llvm_surface_typedef(c_type);

    if (payload_typedef == NULL) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    payload_type = payload_typedef->type;
    if (r_llvm_main_errors[which].domain[0] == '\0') {
        (void)LLVMBuildMemCpy(emitter->builder,
                              error->memory,
                              error->align,
                              payload,
                              error->align,
                              r_llvm_u64(emitter, error->size));
        return true;
    }
    {
        char constant[64];
        (void)snprintf(
            constant, sizeof(constant), "R_STD_ERROR_DOMAIN_%s", r_llvm_main_errors[which].domain);
        if (!r_llvm_runtime_constant(emitter, constant, &domain)) {
            return false;
        }
    }
    (void)LLVMBuildStore(emitter->builder,
                         r_llvm_u32(emitter, (uint64_t)domain),
                         r_llvm_byte_offset(emitter, error->memory, error->domain));
    if (strcmp(r_llvm_main_errors[which].member, "zero") == 0) {
        code = r_llvm_u32(emitter, 0U);
    } else {
        LLVMValueRef member = payload;
        uint32_t member_type = payload_type;
        if (r_llvm_main_errors[which].member[0] == '.') {
            uint32_t offset = 0U;
            if (!r_llvm_runtime_field(
                    emitter, c_type, r_llvm_main_errors[which].member + 1, &offset, &member_type)) {
                return false;
            }
            member = r_llvm_byte_offset(emitter, payload, offset);
        }
        code = r_llvm_load_as_u32(emitter, member, member_type);
        if (code == NULL) {
            return false;
        }
        code = LLVMBuildAdd(
            emitter->builder, r_llvm_u32(emitter, r_llvm_main_errors[which].base), code, "");
    }
    (void)LLVMBuildStore(
        emitter->builder, code, r_llvm_byte_offset(emitter, error->memory, error->code));
    if (r_llvm_main_errors[which].native) {
        uint32_t offset = 0U;
        if (!r_llvm_runtime_field(emitter, c_type, "native_code", &offset, NULL)) {
            return false;
        }
        (void)LLVMBuildStore(emitter->builder,
                             LLVMBuildLoad2(emitter->builder,
                                            r_llvm_int(emitter, 64U),
                                            r_llvm_byte_offset(emitter, payload, offset),
                                            ""),
                             r_llvm_byte_offset(emitter, error->memory, error->native_code));
    } else {
        (void)LLVMBuildStore(emitter->builder,
                             r_llvm_u64(emitter, 0U),
                             r_llvm_byte_offset(emitter, error->memory, error->native_code));
    }
    return true;
}

static size_t r_llvm_main_error_index(RLlvmEmitter *emitter, RTypeId type_id) {
    const size_t count = sizeof(r_llvm_main_errors) / sizeof(r_llvm_main_errors[0]);
    const RSemanticType *type =
        (type_id == R_TYPE_ID_INVALID) || ((size_t)type_id > emitter->frontend->semantic_type_count)
            ? NULL
            : &emitter->frontend->semantic_types[(size_t)type_id - 1U];
    const RInternEntry *entry;
    size_t which;

    if ((type == NULL) || (type->kind != R_SEMANTIC_TYPE_STANDARD) ||
        (type->base != R_TYPE_ID_INVALID) || (type->length == 0U) ||
        (type->length > (uint64_t)emitter->frontend->intern_count)) {
        return count;
    }
    entry = &emitter->frontend->intern_entries[(size_t)type->length - 1U];
    for (which = 0U; which < count; ++which) {
        if ((strlen(r_llvm_main_errors[which].name) == entry->length) &&
            (memcmp(r_llvm_main_errors[which].name, entry->bytes, entry->length) == 0)) {
            break;
        }
    }
    return which;
}

/* R-SLIB-ERR-0004: std.error::from_fault forms the portable error of the held standard error as
   the main boundary reports it (r_c17_emit_fault_portable); the variants of the fault are the
   main errors in their order. */
bool r_llvm_fault_portable(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RMirValueId operand = emitter->frontend->mir_operands[instruction->first_operand];
    const RMirInstruction *definition = r_llvm_definition(emitter, operand);
    const size_t count = sizeof(r_llvm_main_errors) / sizeof(r_llvm_main_errors[0]);
    LLVMValueRef fault = r_llvm_value(emitter, operand);
    LLVMValueRef choice;
    LLVMBasicBlockRef invalid;
    LLVMBasicBlockRef done;
    RLlvmMainError error;
    uint32_t payload = 0U;
    size_t index;

    (void)memset(&error, 0, sizeof(error));
    if ((definition == NULL) || (fault == NULL) ||
        !r_llvm_payload_offset(emitter, definition->type, &payload) ||
        !r_llvm_runtime_layout(emitter, "RStdError", &error.size, &error.align) ||
        !r_llvm_runtime_field(emitter, "RStdError", "domain", &error.domain, NULL) ||
        !r_llvm_runtime_field(emitter, "RStdError", "code", &error.code, NULL) ||
        !r_llvm_runtime_field(emitter, "RStdError", "native_code", &error.native_code, NULL)) {
        return definition == NULL ? r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR) : false;
    }
    error.memory = r_llvm_value_memory(emitter, instruction->result, instruction->type);
    if (error.memory == NULL) {
        return false;
    }
    invalid = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    done = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    choice = LLVMBuildSwitch(emitter->builder,
                             LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), fault, ""),
                             invalid,
                             (unsigned)count);
    for (index = 0U; index < count; ++index) {
        const RSemanticVariant *variant = r_semantic_tagged_variant(
            emitter->frontend, r_llvm_value_type(emitter, definition->type), (uint32_t)index);
        const RSemanticType *payload_type =
            (variant == NULL) || (variant->payload_type == R_TYPE_ID_INVALID)
                ? NULL
                : &emitter->frontend->semantic_types[(size_t)variant->payload_type - 1U];
        uint32_t size = 0U;
        uint32_t align = 0U;
        const char *c_type = payload_type == NULL
                                 ? NULL
                                 : r_llvm_standard_c_type(emitter, payload_type, &size, &align);
        LLVMBasicBlockRef block;
        if (c_type == NULL) {
            return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
        block = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        LLVMAddCase(choice, r_llvm_u32(emitter, index), block);
        LLVMPositionBuilderAtEnd(emitter->builder, block);
        if (!r_llvm_main_error_case(
                emitter, &error, index, r_llvm_byte_offset(emitter, fault, payload), c_type)) {
            return false;
        }
        (void)LLVMBuildBr(emitter->builder, done);
    }
    LLVMPositionBuilderAtEnd(emitter->builder, invalid);
    if (!r_llvm_panic(emitter,
                      "R_RUNTIME_PANIC_CONTRACT_VIOLATION",
                      instruction->span,
                      R_MIR_BLOCK_ID_INVALID)) {
        return false;
    }
    LLVMPositionBuilderAtEnd(emitter->builder, done);
    r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, instruction->result), true);
    return true;
}

/* R-FUNC-0008/R-IDB-023: the result or the error of the entry becomes the process status. */
static LLVMValueRef r_llvm_main_outcome(RLlvmEmitter *emitter,
                                        const RSemanticSymbol *entry,
                                        LLVMValueRef outcome,
                                        RSourceSpan span) {
    const uint32_t count = r_semantic_effect_count(emitter->frontend, entry->throws_type);
    const size_t domain_count = sizeof(r_llvm_main_domains) / sizeof(r_llvm_main_domains[0]);
    LLVMBasicBlockRef ok = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "ok");
    LLVMBasicBlockRef invalid =
        LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef failed =
        LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "error");
    LLVMBasicBlockRef bad_tag =
        LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef project =
        LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef bad_domain =
        LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef report =
        LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef ok_end;
    LLVMValueRef tag;
    LLVMValueRef value;
    LLVMValueRef in_range;
    LLVMValueRef tag_switch;
    LLVMValueRef domain_switch;
    LLVMValueRef domain_name;
    LLVMValueRef domain_length;
    LLVMValueRef status;
    LLVMValueRef result;
    RLlvmMainError error;
    uint32_t payload_offset = 0U;
    uint32_t name_size = 0U;
    uint32_t name_align = 0U;
    uint32_t name_data = 0U;
    uint32_t name_length = 0U;
    LLVMValueRef name;
    size_t index;

    (void)memset(&error, 0, sizeof(error));
    if (!r_llvm_carrier_payload_offset(emitter, entry->effect_carrier_type, &payload_offset) ||
        !r_llvm_runtime_layout(emitter, "RStdError", &error.size, &error.align) ||
        !r_llvm_runtime_field(emitter, "RStdError", "domain", &error.domain, NULL) ||
        !r_llvm_runtime_field(emitter, "RStdError", "code", &error.code, NULL) ||
        !r_llvm_runtime_field(emitter, "RStdError", "native_code", &error.native_code, NULL) ||
        !r_llvm_runtime_layout(emitter, "RStdStringView", &name_size, &name_align) ||
        !r_llvm_runtime_field(emitter, "RStdStringView", "data", &name_data, NULL) ||
        !r_llvm_runtime_field(emitter, "RStdStringView", "length", &name_length, NULL)) {
        return NULL;
    }
    error.memory = r_llvm_entry_alloca(emitter, error.size, error.align, "error");
    name = r_llvm_entry_alloca(emitter, name_size, name_align, "name");
    tag = LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), outcome, "tag");
    (void)LLVMBuildCondBr(
        emitter->builder,
        LLVMBuildICmp(emitter->builder, LLVMIntEQ, tag, r_llvm_u32(emitter, 0U), ""),
        ok,
        failed);

    /* The status of a normal return must lie in 0..111; any other ends the process with 124. */
    LLVMPositionBuilderAtEnd(emitter->builder, ok);
    value = LLVMBuildLoad2(emitter->builder,
                           r_llvm_int(emitter, 32U),
                           r_llvm_byte_offset(emitter, outcome, payload_offset),
                           "");
    in_range = LLVMBuildICmp(emitter->builder, LLVMIntULE, value, r_llvm_u32(emitter, 111U), "");
    ok_end = LLVMGetInsertBlock(emitter->builder);
    (void)LLVMBuildCondBr(emitter->builder, in_range, done, invalid);
    LLVMPositionBuilderAtEnd(emitter->builder, invalid);
    {
        static const char diagnostic[] = "R main error: invalid_main_status\n";
        LLVMValueRef arguments[2];
        arguments[0] = LLVMBuildGlobalString(emitter->builder, diagnostic, "");
        arguments[1] = r_llvm_u64(emitter, sizeof(diagnostic) - 1U);
        if (r_llvm_call_runtime(emitter, "r_runtime_emergency_write", arguments, 2U, NULL) ==
            NULL) {
            return NULL;
        }
    }
    (void)LLVMBuildBr(emitter->builder, done);

    /* An error: its portable projection, the domain's status and the report. */
    LLVMPositionBuilderAtEnd(emitter->builder, failed);
    tag_switch = LLVMBuildSwitch(emitter->builder, tag, bad_tag, count);
    for (index = 0U; index < count; ++index) {
        const RTypeId type =
            r_semantic_effect_at(emitter->frontend, entry->throws_type, (uint32_t)index);
        const size_t which = r_llvm_main_error_index(emitter, type);
        const RSemanticType *semantic = &emitter->frontend->semantic_types[(size_t)type - 1U];
        uint32_t size = 0U;
        uint32_t align = 0U;
        const char *c_type;
        LLVMBasicBlockRef block;

        if (which == sizeof(r_llvm_main_errors) / sizeof(r_llvm_main_errors[0])) {
            (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
            return NULL;
        }
        c_type = r_llvm_standard_c_type(emitter, semantic, &size, &align);
        if (c_type == NULL) {
            (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
            return NULL;
        }
        block = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        LLVMAddCase(tag_switch, r_llvm_u32(emitter, index + 1U), block);
        LLVMPositionBuilderAtEnd(emitter->builder, block);
        if (!r_llvm_main_error_case(emitter,
                                    &error,
                                    which,
                                    r_llvm_byte_offset(emitter, outcome, payload_offset),
                                    c_type)) {
            return NULL;
        }
        (void)LLVMBuildBr(emitter->builder, project);
    }
    LLVMPositionBuilderAtEnd(emitter->builder, bad_tag);
    if (!r_llvm_contract_panic(emitter, "R_RUNTIME_PANIC_CONTRACT_VIOLATION", span)) {
        return NULL;
    }

    LLVMPositionBuilderAtEnd(emitter->builder, project);
    domain_switch =
        LLVMBuildSwitch(emitter->builder,
                        LLVMBuildLoad2(emitter->builder,
                                       r_llvm_int(emitter, 32U),
                                       r_llvm_byte_offset(emitter, error.memory, error.domain),
                                       ""),
                        bad_domain,
                        (unsigned)domain_count);
    LLVMPositionBuilderAtEnd(emitter->builder, report);
    domain_name = LLVMBuildPhi(emitter->builder, r_llvm_pointer(emitter), "");
    domain_length = LLVMBuildPhi(emitter->builder, r_llvm_int(emitter, 64U), "");
    status = LLVMBuildPhi(emitter->builder, r_llvm_int(emitter, 32U), "");
    for (index = 0U; index < domain_count; ++index) {
        LLVMBasicBlockRef block =
            LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        int64_t constant = 0;
        LLVMValueRef values[3];
        if (!r_llvm_runtime_constant(emitter, r_llvm_main_domains[index].constant, &constant)) {
            return NULL;
        }
        LLVMAddCase(domain_switch, r_llvm_u32(emitter, (uint64_t)constant), block);
        LLVMPositionBuilderAtEnd(emitter->builder, block);
        values[0] = LLVMBuildGlobalString(emitter->builder, r_llvm_main_domains[index].name, "");
        values[1] = r_llvm_u64(emitter, strlen(r_llvm_main_domains[index].name));
        values[2] = r_llvm_u32(emitter, r_llvm_main_domains[index].status);
        (void)LLVMBuildBr(emitter->builder, report);
        LLVMAddIncoming(domain_name, &values[0], &block, 1U);
        LLVMAddIncoming(domain_length, &values[1], &block, 1U);
        LLVMAddIncoming(status, &values[2], &block, 1U);
    }
    LLVMPositionBuilderAtEnd(emitter->builder, bad_domain);
    if (!r_llvm_contract_panic(emitter, "R_RUNTIME_PANIC_CONTRACT_VIOLATION", span)) {
        return NULL;
    }
    LLVMPositionBuilderAtEnd(emitter->builder, report);
    {
        LLVMValueRef arguments[6];
        if (r_llvm_call_runtime(emitter, "r_std_error_name", &error.memory, 1U, name) == NULL) {
            return NULL;
        }
        arguments[0] = domain_name;
        arguments[1] = domain_length;
        arguments[2] = LLVMBuildLoad2(emitter->builder,
                                      r_llvm_pointer(emitter),
                                      r_llvm_byte_offset(emitter, name, name_data),
                                      "");
        arguments[3] = LLVMBuildLoad2(emitter->builder,
                                      r_llvm_int(emitter, 64U),
                                      r_llvm_byte_offset(emitter, name, name_length),
                                      "");
        arguments[4] = LLVMBuildLoad2(emitter->builder,
                                      r_llvm_int(emitter, 32U),
                                      r_llvm_byte_offset(emitter, error.memory, error.code),
                                      "");
        arguments[5] = LLVMBuildLoad2(emitter->builder,
                                      r_llvm_int(emitter, 64U),
                                      r_llvm_byte_offset(emitter, error.memory, error.native_code),
                                      "");
        if (r_llvm_call_runtime(emitter, "r_runtime_report_main_error", arguments, 6U, NULL) ==
            NULL) {
            return NULL;
        }
    }
    {
        LLVMBasicBlockRef report_end = LLVMGetInsertBlock(emitter->builder);
        LLVMValueRef values[3];
        LLVMBasicBlockRef blocks[3];
        (void)LLVMBuildBr(emitter->builder, done);
        LLVMPositionBuilderAtEnd(emitter->builder, done);
        result = LLVMBuildPhi(emitter->builder, r_llvm_int(emitter, 32U), "status");
        values[0] = value;
        blocks[0] = ok_end;
        values[1] = r_llvm_u32(emitter, 124U);
        blocks[1] = invalid;
        values[2] = status;
        blocks[2] = report_end;
        LLVMAddIncoming(result, values, blocks, 3U);
    }
    return result;
}

/* R-FUNC-0008: `main(const str[] args)` receives the arguments of the process as a view of the
   runtime's snapshot, taken before the stack check of the entry as the C17 hosted main does. */
static LLVMValueRef r_llvm_startup_arguments(RLlvmEmitter *emitter, RSourceSpan span) {
    uint32_t snapshot_size = 0U;
    uint32_t snapshot_align = 0U;
    uint32_t arguments_offset = 0U;
    uint32_t count_offset = 0U;
    LLVMValueRef snapshot;
    LLVMValueRef taken;
    LLVMValueRef view;
    LLVMBasicBlockRef refused =
        LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef ready =
        LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");

    if (!r_llvm_runtime_layout(
            emitter, "RRuntimeArgumentSnapshotView", &snapshot_size, &snapshot_align) ||
        !r_llvm_runtime_field(
            emitter, "RRuntimeArgumentSnapshotView", "arguments", &arguments_offset, NULL) ||
        !r_llvm_runtime_field(
            emitter, "RRuntimeArgumentSnapshotView", "count", &count_offset, NULL)) {
        return NULL;
    }
    snapshot = r_llvm_entry_alloca(emitter, snapshot_size, snapshot_align, "snapshot");
    view = r_llvm_entry_alloca(emitter, 16U, 8U, "args");
    (void)LLVMBuildMemSet(emitter->builder,
                          snapshot,
                          LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                          r_llvm_u64(emitter, snapshot_size),
                          snapshot_align);
    taken = r_llvm_call_runtime(emitter, "r_runtime_hosted_argument_snapshot", &snapshot, 1U, NULL);
    if (taken == NULL) {
        return NULL;
    }
    (void)LLVMBuildCondBr(emitter->builder, taken, ready, refused);
    LLVMPositionBuilderAtEnd(emitter->builder, refused);
    if (!r_llvm_contract_panic(emitter, "R_RUNTIME_PANIC_CONTRACT_VIOLATION", span)) {
        return NULL;
    }
    LLVMPositionBuilderAtEnd(emitter->builder, ready);
    (void)LLVMBuildStore(emitter->builder,
                         LLVMBuildLoad2(emitter->builder,
                                        r_llvm_pointer(emitter),
                                        r_llvm_byte_offset(emitter, snapshot, arguments_offset),
                                        ""),
                         view);
    (void)LLVMBuildStore(emitter->builder,
                         LLVMBuildLoad2(emitter->builder,
                                        r_llvm_int(emitter, 64U),
                                        r_llvm_byte_offset(emitter, snapshot, count_offset),
                                        ""),
                         r_llvm_byte_offset(emitter, view, 8U));
    return view;
}

/* R-CONF-0005: a freestanding program has no hosted entry. Its environment reaches the R main
   through r_freestanding_main after adopting the stack bounds; every @callback function is a
   further entry from C (r_c17_emit_freestanding_entry). */
static bool r_llvm_emit_freestanding_main(RLlvmEmitter *emitter,
                                          RSymbolId entry_id,
                                          const RSemanticSymbol *entry) {
    LLVMBasicBlockRef failed;
    LLVMBasicBlockRef running;
    LLVMValueRef started;
    LLVMValueRef result;
    LLVMValueRef arguments[2];
    RSourceSpan span;

    if (entry->is_async || (entry->parameter_count != 0U) ||
        (entry->effect_carrier_type != R_TYPE_ID_INVALID) ||
        (emitter->functions[entry_id] == NULL)) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    span = entry->hir_node == R_HIR_NODE_ID_INVALID
               ? entry->name_span
               : emitter->frontend->hir_nodes[(size_t)entry->hir_node - 1U].span;
    emitter->entry_symbol = entry_id;
    emitter->function = LLVMAddFunction(emitter->module,
                                        "r_freestanding_main",
                                        LLVMFunctionType(r_llvm_int(emitter, 32U), NULL, 0U, 0));
    LLVMPositionBuilderAtEnd(
        emitter->builder,
        LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "entry"));
    failed = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    running = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    started =
        r_llvm_call_runtime(emitter, "r_runtime_stack_initialize_current_thread", NULL, 0U, NULL);
    if (started == NULL) {
        return false;
    }
    (void)LLVMBuildCondBr(emitter->builder, started, running, failed);
    LLVMPositionBuilderAtEnd(emitter->builder, failed);
    if (!r_llvm_contract_panic(emitter, "R_RUNTIME_PANIC_STACK_EXHAUSTION", span)) {
        return false;
    }
    LLVMPositionBuilderAtEnd(emitter->builder, running);
    arguments[0] = r_llvm_u64(emitter, 0U);
    arguments[1] = r_llvm_span(emitter, span);
    if ((arguments[1] == NULL) ||
        (r_llvm_call_runtime(emitter, "r_runtime_stack_require", arguments, 2U, NULL) == NULL)) {
        return false;
    }
    arguments[0] = r_llvm_stack_entry(emitter, entry_id);
    arguments[1] = r_llvm_span(emitter, span);
    if ((arguments[0] == NULL) || (arguments[1] == NULL) ||
        (r_llvm_call_runtime(emitter, "r_runtime_stack_require", arguments, 2U, NULL) == NULL)) {
        return false;
    }
    result = LLVMBuildCall2(emitter->builder,
                            emitter->function_types[entry_id],
                            emitter->functions[entry_id],
                            NULL,
                            0U,
                            "");
    if (!r_llvm_emit_static_drops(emitter)) {
        return false;
    }
    (void)LLVMBuildRet(emitter->builder, result);
    return true;
}

bool r_llvm_emit_hosted_main(RLlvmEmitter *emitter) {
    const RSymbolId entry_id = r_llvm_entry(emitter);
    const RSemanticSymbol *entry = entry_id == R_SYMBOL_ID_INVALID
                                       ? NULL
                                       : &emitter->frontend->semantic_symbols[entry_id - 1U];
    LLVMTypeRef parameters[2];
    LLVMTypeRef type;
    LLVMValueRef outcome;
    LLVMValueRef started;
    LLVMValueRef result;
    LLVMValueRef arguments[2];
    LLVMBasicBlockRef body;
    LLVMBasicBlockRef bootstrap_failed;
    LLVMBasicBlockRef start_failed;
    LLVMBasicBlockRef running;
    LLVMBasicBlockRef terminate;
    LLVMBasicBlockRef returned;
    RSourceSpan span;
    uint32_t size = 0U;
    uint32_t align = 0U;
    uint32_t start_size = 0U;
    uint32_t start_align = 0U;
    uint32_t started_offset = 0U;
    uint32_t status_offset = 0U;
    LLVMValueRef start;
    LLVMValueRef startup = NULL;

    if ((entry != NULL) && (emitter->frontend->profile == R_FRONTEND_PROFILE_FREESTANDING)) {
        return r_llvm_emit_freestanding_main(emitter, entry_id, entry);
    }
    if ((entry == NULL) || (entry->effect_carrier_type == R_TYPE_ID_INVALID) ||
        (emitter->functions[entry_id] == NULL)) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    if (entry->parameter_count > 1U) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    span = entry->hir_node == R_HIR_NODE_ID_INVALID
               ? entry->name_span
               : emitter->frontend->hir_nodes[(size_t)entry->hir_node - 1U].span;
    emitter->entry_symbol = entry_id;
    parameters[0] = r_llvm_int(emitter, 32U);
    parameters[1] = r_llvm_pointer(emitter);
    type = LLVMFunctionType(r_llvm_int(emitter, 32U), parameters, 2U, 0);
    emitter->function = LLVMAddFunction(emitter->module, r_llvm_renamed(emitter, "main"), type);
    body = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "entry");
    LLVMPositionBuilderAtEnd(emitter->builder, body);
    if (!r_llvm_layout(emitter, entry->effect_carrier_type, &size, &align) ||
        !r_llvm_runtime_layout(emitter, "RRuntimeStartResult", &start_size, &start_align) ||
        !r_llvm_runtime_field(emitter, "RRuntimeStartResult", "started", &started_offset, NULL) ||
        !r_llvm_runtime_field(
            emitter, "RRuntimeStartResult", "process_status", &status_offset, NULL)) {
        return false;
    }
    outcome = r_llvm_entry_alloca(emitter, size, align, "outcome");
    start = r_llvm_entry_alloca(emitter, start_size, start_align, "start");
    (void)LLVMBuildMemSet(emitter->builder,
                          outcome,
                          LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                          r_llvm_u64(emitter, size),
                          align);

    /* The stack of the main thread, then the runtime. */
    bootstrap_failed = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    running = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    started =
        r_llvm_call_runtime(emitter, "r_runtime_stack_initialize_current_thread", NULL, 0U, NULL);
    if (started == NULL) {
        return false;
    }
    (void)LLVMBuildCondBr(emitter->builder, started, running, bootstrap_failed);
    LLVMPositionBuilderAtEnd(emitter->builder, bootstrap_failed);
    if (!r_llvm_contract_panic(emitter, "R_RUNTIME_PANIC_STACK_EXHAUSTION", span)) {
        return false;
    }
    LLVMPositionBuilderAtEnd(emitter->builder, running);
    arguments[0] = r_llvm_u64(emitter, 0U);
    arguments[1] = r_llvm_span(emitter, span);
    if ((arguments[1] == NULL) ||
        (r_llvm_call_runtime(emitter, "r_runtime_stack_require", arguments, 2U, NULL) == NULL)) {
        return false;
    }
    arguments[0] = LLVMGetParam(emitter->function, 0U);
    arguments[1] = LLVMGetParam(emitter->function, 1U);
    if (r_llvm_call_runtime(emitter, "r_runtime_hosted_start", arguments, 2U, start) == NULL) {
        return false;
    }
    start_failed = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    running = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    (void)LLVMBuildCondBr(
        emitter->builder,
        LLVMBuildTrunc(emitter->builder,
                       LLVMBuildLoad2(emitter->builder,
                                      r_llvm_int(emitter, 8U),
                                      r_llvm_byte_offset(emitter, start, started_offset),
                                      ""),
                       r_llvm_int(emitter, 1U),
                       ""),
        running,
        start_failed);
    LLVMPositionBuilderAtEnd(emitter->builder, start_failed);
    (void)LLVMBuildRet(emitter->builder,
                       LLVMBuildLoad2(emitter->builder,
                                      r_llvm_int(emitter, 32U),
                                      r_llvm_byte_offset(emitter, start, status_offset),
                                      ""));

    /* The entry, with the stack its deepest path needs (R-FUNC-0004). */
    LLVMPositionBuilderAtEnd(emitter->builder, running);
    if ((emitter->thread_local_drop_all != NULL) &&
        (r_llvm_call_runtime(emitter,
                             "r_runtime_thread_local_cleanup_install",
                             &emitter->thread_local_drop_all,
                             1U,
                             NULL) == NULL)) {
        return false;
    }
    if (!r_llvm_initialize_static_bytes(emitter)) {
        return false;
    }
    if ((entry->parameter_count == 1U) &&
        ((startup = r_llvm_startup_arguments(emitter, span)) == NULL)) {
        return false;
    }
    if (entry->is_async) {
        /* The root task of an async main, awaited by this thread (hosted_main.inc). Its startup
           arguments are the one argument of the start: the view's address, without a flag. */
        LLVMValueRef context = NULL;
        LLVMValueRef root;
        LLVMValueRef root_status;
        LLVMValueRef await_status;
        LLVMBasicBlockRef refused;
        LLVMBasicBlockRef checked;
        LLVMBasicBlockRef invalid;
        LLVMBasicBlockRef awaited;
        LLVMBasicBlockRef panicked;
        LLVMBasicBlockRef completed;
        uint32_t task_offset = 0U;
        uint32_t root_status_offset = 0U;
        if (startup != NULL) {
            context = r_llvm_entry_alloca(emitter, 16U, 8U, "context");
            (void)LLVMBuildStore(emitter->builder, startup, context);
            (void)LLVMBuildStore(emitter->builder,
                                 LLVMConstNull(r_llvm_pointer(emitter)),
                                 r_llvm_byte_offset(emitter, context, 8U));
        }
        root = r_llvm_start_task(emitter, entry_id, context, 0U);
        if ((root == NULL) ||
            !r_llvm_runtime_field(emitter, "RRuntimeTaskStartResult", "task", &task_offset, NULL) ||
            !r_llvm_runtime_field(
                emitter, "RRuntimeTaskStartResult", "status", &root_status_offset, NULL)) {
            return false;
        }
        root_status = LLVMBuildLoad2(emitter->builder,
                                     r_llvm_int(emitter, 32U),
                                     r_llvm_byte_offset(emitter, root, root_status_offset),
                                     "");
        refused = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        checked = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        invalid = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        awaited = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        panicked = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        completed = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        /* ALLOCATION_FAILED (2) and RUNTIME_STOPPING (3) end the program with its failure. */
        (void)LLVMBuildCondBr(
            emitter->builder,
            LLVMBuildICmp(emitter->builder, LLVMIntUGE, root_status, r_llvm_u32(emitter, 2U), ""),
            refused,
            checked);
        LLVMPositionBuilderAtEnd(emitter->builder, refused);
        {
            LLVMValueRef failure = r_llvm_call_runtime(
                emitter, "r_runtime_hosted_async_root_start_failure", NULL, 0U, NULL);
            if (failure == NULL) {
                return false;
            }
            (void)LLVMBuildRet(emitter->builder, failure);
        }
        LLVMPositionBuilderAtEnd(emitter->builder, checked);
        (void)LLVMBuildCondBr(
            emitter->builder,
            LLVMBuildOr(
                emitter->builder,
                LLVMBuildICmp(
                    emitter->builder, LLVMIntNE, root_status, r_llvm_u32(emitter, 0U), ""),
                LLVMBuildIsNull(emitter->builder,
                                LLVMBuildLoad2(emitter->builder,
                                               r_llvm_pointer(emitter),
                                               r_llvm_byte_offset(emitter, root, task_offset),
                                               ""),
                                ""),
                ""),
            invalid,
            awaited);
        LLVMPositionBuilderAtEnd(emitter->builder, invalid);
        if (!r_llvm_contract_panic(emitter, "R_RUNTIME_PANIC_CONTRACT_VIOLATION", span)) {
            return false;
        }
        LLVMPositionBuilderAtEnd(emitter->builder, awaited);
        arguments[0] = r_llvm_byte_offset(emitter, root, task_offset);
        arguments[1] = outcome;
        await_status = r_llvm_call_runtime(emitter, "r_runtime_task_await", arguments, 2U, NULL);
        if (await_status == NULL) {
            return false;
        }
        invalid = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        {
            LLVMValueRef choice = LLVMBuildSwitch(emitter->builder, await_status, invalid, 2U);
            LLVMAddCase(choice, r_llvm_u32(emitter, 0U), completed); /* OK */
            LLVMAddCase(choice, r_llvm_u32(emitter, 4U), panicked);  /* PANICKED */
        }
        LLVMPositionBuilderAtEnd(emitter->builder, panicked);
        if (r_llvm_call_runtime(emitter, "r_runtime_unwind_terminate", NULL, 0U, NULL) == NULL) {
            return false;
        }
        (void)LLVMBuildUnreachable(emitter->builder);
        LLVMPositionBuilderAtEnd(emitter->builder, invalid);
        if (!r_llvm_contract_panic(emitter, "R_RUNTIME_PANIC_CONTRACT_VIOLATION", span)) {
            return false;
        }
        LLVMPositionBuilderAtEnd(emitter->builder, completed);
        result = r_llvm_main_outcome(emitter, entry, outcome, span);
        if (result == NULL) {
            return false;
        }
        if ((r_llvm_call_runtime(emitter, "r_runtime_hosted_drain", NULL, 0U, NULL) == NULL) ||
            !r_llvm_emit_static_drops(emitter) ||
            (r_llvm_call_runtime(emitter, "r_runtime_hosted_drain", NULL, 0U, NULL) == NULL)) {
            return false;
        }
        result = r_llvm_call_runtime(emitter, "r_runtime_hosted_finish", &result, 1U, NULL);
        if (result == NULL) {
            return false;
        }
        (void)LLVMBuildRet(emitter->builder, result);
        return true;
    }
    arguments[0] = r_llvm_stack_entry(emitter, entry_id);
    arguments[1] = r_llvm_span(emitter, span);
    if ((arguments[0] == NULL) || (arguments[1] == NULL) ||
        (r_llvm_call_runtime(emitter, "r_runtime_stack_require", arguments, 2U, NULL) == NULL)) {
        return false;
    }
    {
        LLVMValueRef call_arguments[2];
        unsigned call_count = 1U;
        call_arguments[0] = outcome;
        if (startup != NULL) {
            call_arguments[call_count++] = startup;
        }
        (void)LLVMBuildCall2(emitter->builder,
                             emitter->function_types[entry_id],
                             emitter->functions[entry_id],
                             call_arguments,
                             call_count,
                             "");
    }
    /* L39 (R-ERR-0009): a panic that reaches main has unwound its frames; the process ends with
       its diagnostic. */
    {
        LLVMValueRef unwinding = r_llvm_unwinding(emitter);
        if (unwinding == NULL) {
            return false;
        }
        terminate = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        returned = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        (void)LLVMBuildCondBr(emitter->builder, unwinding, terminate, returned);
        LLVMPositionBuilderAtEnd(emitter->builder, terminate);
        if (r_llvm_call_runtime(emitter, "r_runtime_unwind_terminate", NULL, 0U, NULL) == NULL) {
            return false;
        }
        (void)LLVMBuildUnreachable(emitter->builder);
        LLVMPositionBuilderAtEnd(emitter->builder, returned);
    }
    result = r_llvm_main_outcome(emitter, entry, outcome, span);
    if (result == NULL) {
        return false;
    }
    if ((r_llvm_call_runtime(emitter, "r_runtime_hosted_drain", NULL, 0U, NULL) == NULL) ||
        !r_llvm_emit_static_drops(emitter) ||
        (r_llvm_call_runtime(emitter, "r_runtime_hosted_drain", NULL, 0U, NULL) == NULL)) {
        return false;
    }
    result = r_llvm_call_runtime(emitter, "r_runtime_hosted_finish", &result, 1U, NULL);
    if (result == NULL) {
        return false;
    }
    (void)LLVMBuildRet(emitter->builder, result);
    return true;
}

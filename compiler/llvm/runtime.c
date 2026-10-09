#include "emit_internal.h"

#include <stdio.h>
#include <string.h>

/* Calls into the C runtime and library through the C ABI of the target (B3). A runtime function
   is declared once with the lowered signature its classification gives; a call passes each
   argument as that classification requires: a scalar in its own type, a small aggregate loaded
   as its register type from a temporary of that size, a large aggregate through a pointer to a
   copy, and receives an aggregate result in registers or in memory the caller provides. */

void *r_llvm_allocate(RLlvmEmitter *emitter, size_t size) {
    void *result;

    if ((size == 0U) || (emitter->status != R_FRONTEND_OK)) {
        return NULL;
    }
    result =
        emitter->frontend->options.allocate(emitter->frontend->options.allocator_user_data, size);
    if (result == NULL) {
        emitter->status = R_FRONTEND_OUT_OF_MEMORY;
    } else {
        (void)memset(result, 0, size);
    }
    return result;
}

void r_llvm_free(RLlvmEmitter *emitter, void *pointer) {
    if (pointer != NULL) {
        emitter->frontend->options.free(emitter->frontend->options.allocator_user_data, pointer);
    }
}

bool r_llvm_fail_at(RLlvmEmitter *emitter, RFrontendStatus status, const char *file, int line) {
    if (emitter->status == R_FRONTEND_OK) {
        const char *base = strrchr(file, '/');
        emitter->status = status;
        if (status == R_FRONTEND_INTERNAL_ERROR) {
            (void)fprintf(stderr,
                          "r-front: internal error of the LLVM lowering (%s:%d)\n",
                          base == NULL ? file : base + 1,
                          line);
        }
    }
    return false;
}

bool r_llvm_unsupported_detail(RLlvmEmitter *emitter,
                               const char *what,
                               const char *detail,
                               size_t detail_length) {
    if (emitter->status == R_FRONTEND_OK) {
        size_t length = 0U;
        const char *function =
            emitter->function == NULL ? NULL : LLVMGetValueName2(emitter->function, &length);
        (void)fprintf(stderr,
                      "r-front: the LLVM lowering does not implement %s%s%.*s%s%s%.*s\n",
                      what,
                      detail == NULL ? "" : " (",
                      detail == NULL ? 0 : (int)detail_length,
                      detail == NULL ? "" : detail,
                      detail == NULL ? "" : ")",
                      function == NULL ? "" : " in ",
                      (int)length,
                      function == NULL ? "" : function);
    }
    return r_llvm_fail(emitter, R_FRONTEND_NOT_LOWERABLE);
}

bool r_llvm_unsupported(RLlvmEmitter *emitter, const char *what) {
    return r_llvm_unsupported_detail(emitter, what, NULL, 0U);
}

const char *r_llvm_renamed(const RLlvmEmitter *emitter, const char *name) {
    const size_t length = strlen(name);
    size_t index;

    if (emitter->artifact_options == NULL) {
        return name;
    }
    for (index = 0U; index < emitter->artifact_options->symbol_rename_count; ++index) {
        const char *pair = emitter->artifact_options->symbol_renames[index];
        if ((strncmp(pair, name, length) == 0) && (pair[length] == '=')) {
            return pair + length + 1U;
        }
    }
    return name;
}

LLVMTypeRef r_llvm_int(RLlvmEmitter *emitter, unsigned bits) {
    return LLVMIntTypeInContext(emitter->context, bits);
}

LLVMTypeRef r_llvm_pointer(RLlvmEmitter *emitter) {
    return LLVMPointerTypeInContext(emitter->context, 0U);
}

LLVMValueRef r_llvm_u32(RLlvmEmitter *emitter, uint64_t value) {
    return LLVMConstInt(r_llvm_int(emitter, 32U), value, 0);
}

LLVMValueRef r_llvm_u64(RLlvmEmitter *emitter, uint64_t value) {
    return LLVMConstInt(r_llvm_int(emitter, 64U), value, 0);
}

static LLVMTypeRef r_llvm_abi_element_type(RLlvmEmitter *emitter, const RLlvmAbiValue *value) {
    switch (value->element) {
    case R_LLVM_ABI_ELEMENT_I1:
        return r_llvm_int(emitter, 1U);
    case R_LLVM_ABI_ELEMENT_I8:
        return r_llvm_int(emitter, 8U);
    case R_LLVM_ABI_ELEMENT_I16:
        return r_llvm_int(emitter, 16U);
    case R_LLVM_ABI_ELEMENT_I32:
        return r_llvm_int(emitter, 32U);
    case R_LLVM_ABI_ELEMENT_I64:
        return r_llvm_int(emitter, 64U);
    case R_LLVM_ABI_ELEMENT_I128:
        return r_llvm_int(emitter, 128U);
    case R_LLVM_ABI_ELEMENT_INTEGER:
        return r_llvm_int(emitter, value->bits);
    case R_LLVM_ABI_ELEMENT_FLOAT:
        return LLVMFloatTypeInContext(emitter->context);
    case R_LLVM_ABI_ELEMENT_DOUBLE:
        return LLVMDoubleTypeInContext(emitter->context);
    case R_LLVM_ABI_ELEMENT_POINTER:
        return r_llvm_pointer(emitter);
    default:
        return NULL;
    }
}

/* The register type of a classified value: its element, an array of its elements, or for a
   homogeneous result a structure of them. */
LLVMTypeRef
r_llvm_abi_register_type(RLlvmEmitter *emitter, const RLlvmAbiValue *value, bool result) {
    LLVMTypeRef element = r_llvm_abi_element_type(emitter, value);

    if ((element == NULL) || (value->count <= 1U)) {
        return element;
    }
    if (value->homogeneous && result) {
        LLVMTypeRef members[4];
        unsigned index;

        for (index = 0U; (index < value->count) && (index < 4U); ++index) {
            members[index] = element;
        }
        return LLVMStructTypeInContext(emitter->context, members, value->count, 0);
    }
    return LLVMArrayType2(element, value->count);
}

void r_llvm_add_extension(RLlvmEmitter *emitter,
                          LLVMValueRef target,
                          LLVMAttributeIndex index,
                          RLlvmAbiExtend extend,
                          bool call_site) {
    const char *name = extend == R_LLVM_ABI_EXTEND_ZERO   ? "zeroext"
                       : extend == R_LLVM_ABI_EXTEND_SIGN ? "signext"
                                                          : NULL;
    LLVMAttributeRef attribute;

    if (name == NULL) {
        return;
    }
    attribute = LLVMCreateEnumAttribute(
        emitter->context, LLVMGetEnumAttributeKindForName(name, strlen(name)), 0U);
    if (call_site) {
        LLVMAddCallSiteAttribute(target, index, attribute);
    } else {
        LLVMAddAttributeAtIndex(target, index, attribute);
    }
}

LLVMAttributeRef r_llvm_sret_attribute(RLlvmEmitter *emitter, uint32_t size) {
    static const char name[] = "sret";
    return LLVMCreateTypeAttribute(emitter->context,
                                   LLVMGetEnumAttributeKindForName(name, sizeof(name) - 1U),
                                   LLVMArrayType2(r_llvm_int(emitter, 8U), size));
}

const RLlvmRuntimeFunction *r_llvm_runtime(RLlvmEmitter *emitter, const char *requested) {
    const RLlvmSurfaceFunction *surface = r_llvm_surface_function(requested);
    const char *name = requested;
    char shim[256];
    const RLlvmTypeTable table = r_llvm_surface_table();
    const RLlvmSurfaceType *type;
    RLlvmRuntimeFunction *entry;
    const RLlvmRuntimeFunction *created = NULL;
    LLVMTypeRef parameter_types[64];
    unsigned lowered_count = 0U;
    uint32_t parameter;
    size_t index;

    if (surface == NULL) {
        /* A static inline function of the headers is called through its shim. */
        const int written = snprintf(shim, sizeof(shim), "r_shim_%s", requested);
        if ((written > 0) && ((size_t)written < sizeof(shim))) {
            surface = r_llvm_surface_function(shim);
        }
    }
    if (surface != NULL) {
        name = surface->name;
    }
    if (r_llvm_renamed(emitter, requested) != requested) {
        /* A test harness calls its own function in place of this one (--rename-symbol). */
        name = r_llvm_renamed(emitter, requested);
    }
    for (index = 0U; index < emitter->runtime_count; ++index) {
        if (strcmp(emitter->runtime[index].name, name) == 0) {
            return &emitter->runtime[index];
        }
    }
    type = surface == NULL ? NULL : r_llvm_surface_type(surface->type);
    if (surface == NULL) {
        (void)fprintf(stderr, "r-front: the runtime surface has no function %s\n", requested);
    }
    if ((type == NULL) || (type->kind != R_LLVM_SURFACE_FUNCTION) || (type->count > 62U)) {
        (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        return NULL;
    }
    if (emitter->runtime_count == emitter->runtime_capacity) {
        const size_t capacity =
            emitter->runtime_capacity == 0U ? 64U : emitter->runtime_capacity * 2U;
        RLlvmRuntimeFunction *grown = r_llvm_allocate(emitter, capacity * sizeof(*grown));
        if (grown == NULL) {
            return NULL;
        }
        if (emitter->runtime_count != 0U) {
            (void)memcpy(grown, emitter->runtime, emitter->runtime_count * sizeof(*grown));
        }
        r_llvm_free(emitter, emitter->runtime);
        emitter->runtime = grown;
        emitter->runtime_capacity = capacity;
    }
    entry = &emitter->runtime[emitter->runtime_count];
    (void)memset(entry, 0, sizeof(*entry));
    entry->name = name;
    entry->variadic = (type->flags & R_LLVM_SURFACE_VARIADIC) != 0U;
    entry->result_type = type->target;
    entry->parameter_count = type->count;
    if (type->count != 0U) {
        entry->parameters = r_llvm_allocate(emitter, type->count * sizeof(*entry->parameters));
        entry->parameter_types =
            r_llvm_allocate(emitter, type->count * sizeof(*entry->parameter_types));
        if ((entry->parameters == NULL) || (entry->parameter_types == NULL)) {
            goto cleanup;
        }
    }
    if (!r_llvm_abi_classify_result(&table, type->target, &entry->result)) {
        (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        goto cleanup;
    }
    if (entry->result.abi_class == R_LLVM_ABI_INDIRECT) {
        parameter_types[lowered_count++] = r_llvm_pointer(emitter);
    }
    for (parameter = 0U; parameter < type->count; ++parameter) {
        uint32_t parameter_type = 0U;
        RLlvmAbiValue *value = &entry->parameters[parameter];

        if (!r_llvm_surface_parameter(type->first + parameter, &parameter_type) ||
            !r_llvm_abi_classify_argument(&table, parameter_type, value)) {
            (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
            goto cleanup;
        }
        entry->parameter_types[parameter] = parameter_type;
        parameter_types[lowered_count++] = value->abi_class == R_LLVM_ABI_INDIRECT
                                               ? r_llvm_pointer(emitter)
                                               : r_llvm_abi_register_type(emitter, value, false);
    }
    entry->type = LLVMFunctionType(entry->result.abi_class == R_LLVM_ABI_IGNORE ||
                                           entry->result.abi_class == R_LLVM_ABI_INDIRECT
                                       ? LLVMVoidTypeInContext(emitter->context)
                                       : r_llvm_abi_register_type(emitter, &entry->result, true),
                                   parameter_types,
                                   lowered_count,
                                   entry->variadic ? 1 : 0);
    entry->function = LLVMGetNamedFunction(emitter->module, name);
    if (entry->function == NULL) {
        entry->function = LLVMAddFunction(emitter->module, name, entry->type);
        if (entry->result.abi_class == R_LLVM_ABI_INDIRECT) {
            const RLlvmSurfaceType *result = r_llvm_surface_type(type->target);
            LLVMAddAttributeAtIndex(
                entry->function, 1U, r_llvm_sret_attribute(emitter, result->size));
        } else {
            r_llvm_add_extension(
                emitter, entry->function, LLVMAttributeReturnIndex, entry->result.extend, false);
        }
        for (parameter = 0U; parameter < type->count; ++parameter) {
            const unsigned position =
                parameter + (entry->result.abi_class == R_LLVM_ABI_INDIRECT ? 2U : 1U);
            r_llvm_add_extension(
                emitter, entry->function, position, entry->parameters[parameter].extend, false);
        }
    }
    emitter->runtime_count += 1U;
    created = entry;

cleanup:
    if (created == NULL) {
        /* The entry is not counted yet, so the cleanup of the emitter does not reach its arrays
           (B6-8). */
        r_llvm_free(emitter, entry->parameters);
        r_llvm_free(emitter, entry->parameter_types);
        entry->parameters = NULL;
        entry->parameter_types = NULL;
    }
    return created;
}

LLVMValueRef
r_llvm_entry_alloca(RLlvmEmitter *emitter, uint32_t size, uint32_t align, const char *name) {
    LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(emitter->function);
    LLVMBuilderRef builder;
    LLVMValueRef first = LLVMGetFirstInstruction(entry);
    LLVMValueRef slot;

    if (emitter->frame != NULL) {
        /* A step keeps its storage in the frame, which outlives its suspensions. */
        (void)name;
        return r_llvm_frame_address(emitter, r_llvm_frame_reserve(emitter, size, align));
    }
    builder = LLVMCreateBuilderInContext(emitter->context);
    if (first != NULL) {
        LLVMPositionBuilderBefore(builder, first);
    } else {
        LLVMPositionBuilderAtEnd(builder, entry);
    }
    slot = LLVMBuildAlloca(
        builder, LLVMArrayType2(r_llvm_int(emitter, 8U), size == 0U ? 1U : size), name);
    LLVMSetAlignment(slot, align == 0U ? 1U : align);
    LLVMDisposeBuilder(builder);
    return slot;
}

uint32_t r_llvm_frame_reserve(RLlvmEmitter *emitter, uint32_t size, uint32_t align) {
    const uint32_t alignment = align == 0U ? 1U : align;
    const uint32_t offset = ((emitter->frame_size + alignment - 1U) / alignment) * alignment;

    emitter->frame_size = offset + (size == 0U ? 1U : size);
    emitter->frame_align = alignment > emitter->frame_align ? alignment : emitter->frame_align;
    return offset;
}

LLVMValueRef r_llvm_frame_address(RLlvmEmitter *emitter, uint32_t offset) {
    LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(emitter->function);
    LLVMBuilderRef builder = LLVMCreateBuilderInContext(emitter->context);
    LLVMValueRef first = LLVMGetFirstInstruction(entry);
    LLVMValueRef index = r_llvm_u64(emitter, offset);
    LLVMValueRef address;

    if (first != NULL) {
        LLVMPositionBuilderBefore(builder, first);
    } else {
        LLVMPositionBuilderAtEnd(builder, entry);
    }
    address = LLVMBuildGEP2(builder, r_llvm_int(emitter, 8U), emitter->frame, &index, 1U, "");
    LLVMDisposeBuilder(builder);
    return address;
}

LLVMValueRef r_llvm_entry_offset(RLlvmEmitter *emitter, LLVMValueRef pointer, uint64_t offset) {
    LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(emitter->function);
    LLVMBuilderRef builder;
    LLVMValueRef index = r_llvm_u64(emitter, offset);
    LLVMValueRef address;

    if ((emitter->frame != NULL) && (LLVMIsAGetElementPtrInst(pointer) != NULL) &&
        (LLVMGetOperand(pointer, 0U) == emitter->frame) &&
        (LLVMIsAConstantInt(LLVMGetOperand(pointer, 1U)) != NULL)) {
        /* A slot of the frame: one offset from the frame, which the frame helpers of a step
           move into their own function (r_llvm_frame_rebase). */
        return r_llvm_frame_address(
            emitter, (uint32_t)(LLVMConstIntGetZExtValue(LLVMGetOperand(pointer, 1U)) + offset));
    }
    if (emitter->frame != NULL) {
        return NULL;
    }
    if (LLVMIsAArgument(pointer) != NULL) {
        LLVMValueRef first = LLVMGetFirstInstruction(entry);
        builder = LLVMCreateBuilderInContext(emitter->context);
        if (first != NULL) {
            LLVMPositionBuilderBefore(builder, first);
        } else {
            LLVMPositionBuilderAtEnd(builder, entry);
        }
    } else if ((LLVMIsAInstruction(pointer) != NULL) &&
               (LLVMGetInstructionParent(pointer) == entry) &&
               (LLVMGetNextInstruction(pointer) != NULL)) {
        builder = LLVMCreateBuilderInContext(emitter->context);
        LLVMPositionBuilderBefore(builder, LLVMGetNextInstruction(pointer));
    } else {
        return NULL;
    }
    address = offset == 0U
                  ? pointer
                  : LLVMBuildGEP2(builder, r_llvm_int(emitter, 8U), pointer, &index, 1U, "");
    LLVMDisposeBuilder(builder);
    return address;
}

LLVMValueRef r_llvm_byte_offset(RLlvmEmitter *emitter, LLVMValueRef pointer, uint64_t offset) {
    LLVMValueRef index;

    if (offset == 0U) {
        return pointer;
    }
    index = r_llvm_u64(emitter, offset);
    return LLVMBuildGEP2(emitter->builder, r_llvm_int(emitter, 8U), pointer, &index, 1U, "");
}

static void r_llvm_copy(RLlvmEmitter *emitter,
                        LLVMValueRef destination,
                        LLVMValueRef source,
                        uint32_t size,
                        uint32_t align) {
    (void)LLVMBuildMemCpy(
        emitter->builder, destination, align, source, align, r_llvm_u64(emitter, size));
}

LLVMValueRef r_llvm_call_runtime(RLlvmEmitter *emitter,
                                 const char *name,
                                 const LLVMValueRef *arguments,
                                 unsigned argument_count,
                                 LLVMValueRef result_memory) {
    const RLlvmRuntimeFunction *function = r_llvm_runtime(emitter, name);
    LLVMValueRef lowered[64];
    unsigned lowered_count = 0U;
    unsigned parameter;
    LLVMValueRef call;

    if (function == NULL) {
        return NULL;
    }
    if ((argument_count != function->parameter_count) || function->variadic ||
        (argument_count > 62U)) {
        (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        return NULL;
    }
    if (function->result.abi_class == R_LLVM_ABI_INDIRECT) {
        if (result_memory == NULL) {
            (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
            return NULL;
        }
        lowered[lowered_count++] = result_memory;
    }
    for (parameter = 0U; parameter < argument_count; ++parameter) {
        const RLlvmAbiValue *value = &function->parameters[parameter];
        const RLlvmSurfaceType *type = r_llvm_surface_type(function->parameter_types[parameter]);

        if (value->abi_class == R_LLVM_ABI_DIRECT) {
            lowered[lowered_count++] = arguments[parameter];
        } else if (value->abi_class == R_LLVM_ABI_COERCE) {
            LLVMTypeRef register_type = r_llvm_abi_register_type(emitter, value, false);
            const uint32_t register_size =
                (uint32_t)LLVMABISizeOfType(emitter->data, register_type);
            LLVMValueRef staging = r_llvm_entry_alloca(
                emitter, register_size, type->align > 8U ? type->align : 8U, "");
            r_llvm_copy(emitter, staging, arguments[parameter], type->size, type->align);
            lowered[lowered_count++] = LLVMBuildLoad2(emitter->builder, register_type, staging, "");
        } else if (value->abi_class == R_LLVM_ABI_INDIRECT) {
            LLVMValueRef copy = r_llvm_entry_alloca(emitter, type->size, type->align, "");
            r_llvm_copy(emitter, copy, arguments[parameter], type->size, type->align);
            lowered[lowered_count++] = copy;
        } else {
            (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
            return NULL;
        }
    }
    call = LLVMBuildCall2(
        emitter->builder, function->type, function->function, lowered, lowered_count, "");
    if (function->result.abi_class == R_LLVM_ABI_INDIRECT) {
        LLVMAddCallSiteAttribute(
            call,
            1U,
            r_llvm_sret_attribute(emitter, r_llvm_surface_type(function->result_type)->size));
    } else {
        r_llvm_add_extension(
            emitter, call, LLVMAttributeReturnIndex, function->result.extend, true);
    }
    for (parameter = 0U; parameter < argument_count; ++parameter) {
        r_llvm_add_extension(emitter,
                             call,
                             parameter +
                                 (function->result.abi_class == R_LLVM_ABI_INDIRECT ? 2U : 1U),
                             function->parameters[parameter].extend,
                             true);
    }
    if (function->result.abi_class == R_LLVM_ABI_COERCE) {
        const RLlvmSurfaceType *type = r_llvm_surface_type(function->result_type);
        LLVMTypeRef register_type = r_llvm_abi_register_type(emitter, &function->result, true);
        const uint32_t register_size = (uint32_t)LLVMABISizeOfType(emitter->data, register_type);
        LLVMValueRef staging =
            r_llvm_entry_alloca(emitter, register_size, type->align > 8U ? type->align : 8U, "");

        if (result_memory == NULL) {
            (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
            return NULL;
        }
        (void)LLVMBuildStore(emitter->builder, call, staging);
        r_llvm_copy(emitter, result_memory, staging, type->size, type->align);
    }
    return call;
}

bool r_llvm_runtime_constant(RLlvmEmitter *emitter, const char *name, int64_t *value) {
    const RLlvmSurfaceConstant *constant = r_llvm_surface_constant(name);

    if (constant == NULL) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    *value = constant->value;
    return true;
}

bool r_llvm_runtime_layout(RLlvmEmitter *emitter,
                           const char *name,
                           uint32_t *size,
                           uint32_t *align) {
    const RLlvmSurfaceName *typedef_name = r_llvm_surface_typedef(name);
    const RLlvmSurfaceType *type =
        typedef_name == NULL ? NULL : r_llvm_surface_type(typedef_name->type);

    if (type == NULL) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    *size = type->size;
    *align = type->align;
    return true;
}

bool r_llvm_runtime_field(RLlvmEmitter *emitter,
                          const char *type_name,
                          const char *field_name,
                          uint32_t *offset,
                          uint32_t *field_type) {
    const RLlvmSurfaceName *typedef_name = r_llvm_surface_typedef(type_name);
    const RLlvmSurfaceType *type =
        typedef_name == NULL ? NULL : r_llvm_surface_type(typedef_name->type);
    uint32_t member;

    if ((type == NULL) ||
        ((type->kind != R_LLVM_SURFACE_STRUCT) && (type->kind != R_LLVM_SURFACE_UNION))) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    for (member = 0U; member < type->count; ++member) {
        const RLlvmSurfaceField *field = r_llvm_surface_field(type->first + member);
        if ((field != NULL) && (strcmp(field->name, field_name) == 0)) {
            *offset = field->offset;
            if (field_type != NULL) {
                *field_type = field->type;
            }
            return true;
        }
    }
    return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
}

LLVMValueRef r_llvm_span(RLlvmEmitter *emitter, RSourceSpan span) {
    uint32_t size = 0U;
    uint32_t align = 0U;
    uint32_t source_offset = 0U;
    uint32_t start_offset = 0U;
    uint32_t end_offset = 0U;
    LLVMValueRef memory;

    if (!r_llvm_runtime_layout(emitter, "RRuntimeSourceSpan", &size, &align) ||
        !r_llvm_runtime_field(emitter, "RRuntimeSourceSpan", "module", &source_offset, NULL) ||
        !r_llvm_runtime_field(emitter, "RRuntimeSourceSpan", "start", &start_offset, NULL) ||
        !r_llvm_runtime_field(emitter, "RRuntimeSourceSpan", "end", &end_offset, NULL)) {
        return NULL;
    }
    memory = r_llvm_entry_alloca(emitter, size, align, "span");
    (void)LLVMBuildStore(emitter->builder,
                         r_llvm_u32(emitter, r_llvm_source_key(emitter, span.source)),
                         r_llvm_byte_offset(emitter, memory, source_offset));
    (void)LLVMBuildStore(emitter->builder,
                         r_llvm_u32(emitter, span.start),
                         r_llvm_byte_offset(emitter, memory, start_offset));
    (void)LLVMBuildStore(emitter->builder,
                         r_llvm_u32(emitter, span.end),
                         r_llvm_byte_offset(emitter, memory, end_offset));
    return memory;
}

#include "standard_internal.h"

#include "link_manifest.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* The C side of a program (R-FFI, the C17 emitter's imports, export wrappers and ingress
   checks): calls of imported C functions and of raw fn values, the export wrapper of every
   extern "C" definition, imported C objects and std.c::link_available.

   A C signature is classified by the C ABI of B3 (abi.c) over a table of the program's C types
   in the runtime surface format: the C ABI numerics, raw object and function pointers, fieldless
   @repr(C) enums by their integer and @repr(C) structs and fixed arrays by their members. An
   R-level value is what the R calling convention holds: a scalar as its SSA value, anything else
   as the address of its memory. A call sets the C floating-point environment aside around the
   C code (R-FFI-0058), and a value that comes from C is checked before it becomes an R value
   (R-FFI-0056): a null non-null pointer or an enum value naming no variant is a contract
   violation, which ends the process. An import that names a reserved or typedef C spelling is
   reached through the bridge C17 writes for it (`r_bridge_<c>`); one that passes an R-declared
   struct by value through `r_bridge_<c>_<symbol>` with the struct by address, and a thunk with
   the import's own prototype where its address is taken (R-CMAP-0026). */

enum {
    R_LLVM_C_MAX_PARAMETERS = 62
};

struct RLlvmCTypes {
    RLlvmSurfaceType *types;
    uint32_t type_count;
    uint32_t type_capacity;
    RLlvmSurfaceField *fields;
    uint32_t field_count;
    uint32_t field_capacity;
    uint32_t *by_type; /* semantic type id to table index plus one */
};

typedef struct RLlvmCSignature {
    LLVMTypeRef type;
    RLlvmAbiValue result;
    RTypeId result_type;
    uint32_t result_index;
    /* The bridge of an import that marshals returns its struct through a leading pointer. */
    bool out_result;
    uint32_t count;
    RLlvmAbiValue parameters[R_LLVM_C_MAX_PARAMETERS];
    uint32_t parameter_indexes[R_LLVM_C_MAX_PARAMETERS];
    RTypeId parameter_types[R_LLVM_C_MAX_PARAMETERS];
    bool variadic;
} RLlvmCSignature;

/* ---- the C type table ---- */

static const RLlvmSurfaceType *r_llvm_c_table_type(const void *context, uint32_t index) {
    const struct RLlvmCTypes *types = context;
    return index < types->type_count ? &types->types[index] : NULL;
}

static const RLlvmSurfaceField *r_llvm_c_table_field(const void *context, uint32_t index) {
    const struct RLlvmCTypes *types = context;
    return index < types->field_count ? &types->fields[index] : NULL;
}

static bool r_llvm_c_table_parameter(const void *context, uint32_t index, uint32_t *type) {
    (void)context;
    (void)index;
    (void)type;
    return false;
}

static RLlvmTypeTable r_llvm_c_table(const RLlvmEmitter *emitter) {
    RLlvmTypeTable table;
    table.type = r_llvm_c_table_type;
    table.field = r_llvm_c_table_field;
    table.parameter = r_llvm_c_table_parameter;
    table.context = emitter->c_types;
    return table;
}

static struct RLlvmCTypes *r_llvm_c_types(RLlvmEmitter *emitter) {
    if (emitter->c_types == NULL) {
        emitter->c_types = r_llvm_allocate(emitter, sizeof(*emitter->c_types));
        if (emitter->c_types == NULL) {
            return NULL;
        }
        emitter->c_types->by_type = r_llvm_allocate(emitter,
                                                    (emitter->frontend->semantic_type_count + 1U) *
                                                        sizeof(*emitter->c_types->by_type));
        if (emitter->c_types->by_type == NULL) {
            return NULL;
        }
    }
    return emitter->c_types;
}

void r_llvm_ffi_release(RLlvmEmitter *emitter) {
    if (emitter->c_types != NULL) {
        r_llvm_free(emitter, emitter->c_types->types);
        r_llvm_free(emitter, emitter->c_types->fields);
        r_llvm_free(emitter, emitter->c_types->by_type);
        r_llvm_free(emitter, emitter->c_types);
        emitter->c_types = NULL;
    }
}

static bool
r_llvm_c_add_type(RLlvmEmitter *emitter, const RLlvmSurfaceType *type, uint32_t *index) {
    struct RLlvmCTypes *types = emitter->c_types;

    if (types->type_count == types->type_capacity) {
        const uint32_t capacity = types->type_capacity == 0U ? 32U : types->type_capacity * 2U;
        RLlvmSurfaceType *grown = r_llvm_allocate(emitter, capacity * sizeof(*grown));
        if (grown == NULL) {
            return false;
        }
        if (types->type_count != 0U) {
            (void)memcpy(grown, types->types, types->type_count * sizeof(*grown));
        }
        r_llvm_free(emitter, types->types);
        types->types = grown;
        types->type_capacity = capacity;
    }
    types->types[types->type_count] = *type;
    *index = types->type_count;
    types->type_count += 1U;
    return true;
}

static bool r_llvm_c_add_field(RLlvmEmitter *emitter, uint32_t offset, uint32_t type) {
    struct RLlvmCTypes *types = emitter->c_types;

    if (types->field_count == types->field_capacity) {
        const uint32_t capacity = types->field_capacity == 0U ? 32U : types->field_capacity * 2U;
        RLlvmSurfaceField *grown = r_llvm_allocate(emitter, capacity * sizeof(*grown));
        if (grown == NULL) {
            return false;
        }
        if (types->field_count != 0U) {
            (void)memcpy(grown, types->fields, types->field_count * sizeof(*grown));
        }
        r_llvm_free(emitter, types->fields);
        types->fields = grown;
        types->field_capacity = capacity;
    }
    types->fields[types->field_count].name = NULL;
    types->fields[types->field_count].offset = offset;
    types->fields[types->field_count].type = type;
    types->field_count += 1U;
    return true;
}

/* The table index of a C type of the program. */
static bool r_llvm_c_type(RLlvmEmitter *emitter, RTypeId id, uint32_t depth, uint32_t *index) {
    const RTypeId value = r_llvm_value_type(emitter, id);
    const RSemanticType *type = r_llvm_type(emitter, value);
    const RSemanticTypeKind kind = r_llvm_value_kind(emitter, id);
    struct RLlvmCTypes *types = r_llvm_c_types(emitter);
    RLlvmSurfaceType entry;
    unsigned bits = 0U;
    bool is_signed = false;

    if ((types == NULL) || (type == NULL) ||
        ((size_t)value > emitter->frontend->semantic_type_count) ||
        (depth > emitter->frontend->options.limits.max_nesting)) {
        return types == NULL ? false : r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    if (types->by_type[value] != 0U) {
        *index = types->by_type[value] - 1U;
        return true;
    }
    (void)memset(&entry, 0, sizeof(entry));
    if ((kind == R_SEMANTIC_TYPE_VOID) || (kind == R_SEMANTIC_TYPE_NEVER)) {
        entry.kind = R_LLVM_SURFACE_VOID;
    } else if (kind == R_SEMANTIC_TYPE_BOOL) {
        entry.kind = R_LLVM_SURFACE_BOOL;
        entry.size = 1U;
        entry.align = 1U;
    } else if (r_llvm_kind_integer(kind, &bits, &is_signed)) {
        entry.kind = R_LLVM_SURFACE_INTEGER;
        entry.flags = is_signed ? R_LLVM_SURFACE_SIGNED : 0U;
        entry.size = bits / 8U;
        entry.align = bits / 8U;
    } else if ((kind == R_SEMANTIC_TYPE_F32) || (kind == R_SEMANTIC_TYPE_F64)) {
        entry.kind = R_LLVM_SURFACE_FLOAT;
        entry.size = kind == R_SEMANTIC_TYPE_F32 ? 4U : 8U;
        entry.align = entry.size;
    } else if ((kind == R_SEMANTIC_TYPE_RAW) || (kind == R_SEMANTIC_TYPE_RAW_FUNCTION)) {
        entry.kind = R_LLVM_SURFACE_POINTER;
        entry.size = 8U;
        entry.align = 8U;
    } else if (kind == R_SEMANTIC_TYPE_FIXED_ARRAY) {
        uint32_t element = 0U;
        if (!r_llvm_c_type(emitter, type->base, depth + 1U, &element) ||
            !r_llvm_layout(emitter, value, &entry.size, &entry.align)) {
            return false;
        }
        entry.kind = R_LLVM_SURFACE_ARRAY;
        entry.target = element;
        entry.count = (uint32_t)type->length;
    } else if (kind == R_SEMANTIC_TYPE_ENUM) {
        RLlvmSurfaceType integer;
        uint32_t target = 0U;
        if (!r_llvm_enum_integer(emitter, value, &bits, &is_signed)) {
            return r_llvm_unsupported(emitter, "a C enum with payloads");
        }
        (void)memset(&integer, 0, sizeof(integer));
        integer.kind = R_LLVM_SURFACE_INTEGER;
        integer.flags = is_signed ? R_LLVM_SURFACE_SIGNED : 0U;
        integer.size = bits / 8U;
        integer.align = bits / 8U;
        if (!r_llvm_c_add_type(emitter, &integer, &target)) {
            return false;
        }
        entry.kind = R_LLVM_SURFACE_ENUM;
        entry.target = target;
        entry.size = integer.size;
        entry.align = integer.align;
    } else if (kind == R_SEMANTIC_TYPE_STRUCT) {
        const RSemanticAggregate *aggregate = r_llvm_aggregate(emitter, value);
        uint32_t *members;
        uint32_t member;
        bool built = true;
        if ((aggregate == NULL) || !aggregate->is_repr_c || aggregate->is_opaque) {
            return r_llvm_unsupported(emitter, "a C value of a type without @repr(C) layout");
        }
        if (!r_llvm_layout(emitter, value, &entry.size, &entry.align)) {
            return false;
        }
        members =
            r_llvm_allocate(emitter, ((size_t)aggregate->field_count + 1U) * sizeof(*members));
        if (members == NULL) {
            return false;
        }
        for (member = 0U; built && (member < aggregate->field_count); ++member) {
            built = r_llvm_c_type(
                emitter,
                emitter->frontend->semantic_fields[(size_t)aggregate->first_field + member].type,
                depth + 1U,
                &members[member]);
        }
        entry.kind = aggregate->c_type_kind == R_C_TYPE_KIND_UNION ? R_LLVM_SURFACE_UNION
                                                                   : R_LLVM_SURFACE_STRUCT;
        entry.first = types->field_count;
        entry.count = aggregate->field_count;
        for (member = 0U; built && (member < aggregate->field_count); ++member) {
            uint32_t offset = 0U;
            built = r_llvm_field_offset(emitter, aggregate->first_field + member + 1U, &offset) &&
                    r_llvm_c_add_field(emitter, offset, members[member]);
        }
        r_llvm_free(emitter, members);
        if (!built) {
            return false;
        }
    } else {
        return r_llvm_unsupported(emitter, "a type outside the C ABI");
    }
    if (!r_llvm_c_add_type(emitter, &entry, index)) {
        return false;
    }
    types->by_type[value] = *index + 1U;
    return true;
}

/* ---- signatures ---- */

/* An R-declared @repr(C) struct passed by value to or from an import (R-FFI-0021). */
static bool r_llvm_c_r_struct(RLlvmEmitter *emitter, RTypeId id) {
    const RSemanticAggregate *aggregate =
        r_llvm_value_kind(emitter, id) == R_SEMANTIC_TYPE_STRUCT
            ? r_llvm_aggregate(emitter, r_llvm_value_type(emitter, id))
            : NULL;
    return (aggregate != NULL) && aggregate->is_repr_c && !aggregate->is_opaque &&
           !aggregate->is_c_declared;
}

static bool r_llvm_c_marshals(RLlvmEmitter *emitter, const RSemanticSymbol *symbol) {
    uint32_t index;

    if (!symbol->is_import || !symbol->passes_r_struct ||
        (symbol->kind != R_SEMANTIC_SYMBOL_FUNCTION)) {
        return false;
    }
    if (r_llvm_c_r_struct(emitter, symbol->return_type)) {
        return true;
    }
    for (index = 0U; index < symbol->parameter_count; ++index) {
        if (r_llvm_c_r_struct(
                emitter,
                emitter->frontend
                    ->semantic_parameter_types[symbol->first_parameter_type + index])) {
            return true;
        }
    }
    return false;
}

/* Classifies the result and the parameters and builds the LLVM function type. With `marshal`,
   an R-declared struct passes by address and returns through a leading pointer. */
static bool r_llvm_c_signature(RLlvmEmitter *emitter,
                               RTypeId result,
                               const RTypeId *parameters,
                               uint32_t count,
                               bool variadic,
                               bool marshal,
                               RLlvmCSignature *signature) {
    const RLlvmTypeTable *table;
    RLlvmTypeTable built;
    LLVMTypeRef lowered[R_LLVM_C_MAX_PARAMETERS + 1];
    unsigned lowered_count = 0U;
    uint32_t index;

    (void)memset(signature, 0, sizeof(*signature));
    if (count > (uint32_t)R_LLVM_C_MAX_PARAMETERS) {
        return r_llvm_unsupported(emitter, "a C function with more than 62 parameters");
    }
    signature->result_type = result;
    signature->count = count;
    signature->variadic = variadic;
    signature->out_result = marshal && r_llvm_c_r_struct(emitter, result);
    if (!r_llvm_c_type(emitter, result, 0U, &signature->result_index)) {
        return false;
    }
    for (index = 0U; index < count; ++index) {
        signature->parameter_types[index] = parameters[index];
        if (!r_llvm_c_type(emitter, parameters[index], 0U, &signature->parameter_indexes[index])) {
            return false;
        }
    }
    built = r_llvm_c_table(emitter);
    table = &built;
    if (signature->out_result) {
        signature->result.abi_class = R_LLVM_ABI_IGNORE;
        lowered[lowered_count++] = r_llvm_pointer(emitter);
    } else {
        if (!r_llvm_abi_classify_result(table, signature->result_index, &signature->result)) {
            return r_llvm_unsupported(emitter, "a C result the C ABI cannot return");
        }
        if (signature->result.abi_class == R_LLVM_ABI_INDIRECT) {
            lowered[lowered_count++] = r_llvm_pointer(emitter);
        }
    }
    for (index = 0U; index < count; ++index) {
        RLlvmAbiValue *value = &signature->parameters[index];
        if (marshal && r_llvm_c_r_struct(emitter, parameters[index])) {
            (void)memset(value, 0, sizeof(*value));
            value->abi_class = R_LLVM_ABI_DIRECT;
            value->element = R_LLVM_ABI_ELEMENT_POINTER;
            value->count = 1U;
        } else if (!r_llvm_abi_classify_argument(
                       table, signature->parameter_indexes[index], value)) {
            return r_llvm_unsupported(emitter, "a C parameter the C ABI cannot pass");
        }
        lowered[lowered_count++] = value->abi_class == R_LLVM_ABI_INDIRECT
                                       ? r_llvm_pointer(emitter)
                                       : r_llvm_abi_register_type(emitter, value, false);
    }
    signature->type =
        LLVMFunctionType((signature->result.abi_class == R_LLVM_ABI_IGNORE) ||
                                 (signature->result.abi_class == R_LLVM_ABI_INDIRECT)
                             ? LLVMVoidTypeInContext(emitter->context)
                             : r_llvm_abi_register_type(emitter, &signature->result, true),
                         lowered,
                         lowered_count,
                         variadic ? 1 : 0);
    return true;
}

static bool r_llvm_c_symbol_signature(RLlvmEmitter *emitter,
                                      const RSemanticSymbol *symbol,
                                      bool marshal,
                                      RLlvmCSignature *signature) {
    return r_llvm_c_signature(
        emitter,
        symbol->return_type,
        &emitter->frontend->semantic_parameter_types[symbol->first_parameter_type],
        symbol->parameter_count,
        symbol->is_variadic,
        marshal,
        signature);
}

static bool
r_llvm_c_function_type_signature(RLlvmEmitter *emitter, RTypeId id, RLlvmCSignature *signature) {
    const RSemanticType *type = r_llvm_type(emitter, r_llvm_value_type(emitter, id));
    RTypeId parameters[R_LLVM_C_MAX_PARAMETERS];
    RTypeId parameter;
    uint32_t count = 0U;

    if ((type == NULL) || (type->kind != R_SEMANTIC_TYPE_RAW_FUNCTION)) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    for (parameter = type->second; parameter != R_TYPE_ID_INVALID;) {
        const RSemanticType *item = r_llvm_type(emitter, parameter);
        if ((item == NULL) || (item->kind != R_SEMANTIC_TYPE_FUNCTION_PARAMETER) ||
            (count >= (uint32_t)R_LLVM_C_MAX_PARAMETERS)) {
            return item == NULL ? r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR)
                                : r_llvm_unsupported(emitter, "a raw fn type of this form");
        }
        parameters[count++] = item->base;
        parameter = item->second;
    }
    return r_llvm_c_signature(emitter,
                              type->base,
                              parameters,
                              count,
                              (type->flags & R_SEMANTIC_TYPE_FLAG_VARIADIC) != 0U,
                              false,
                              signature);
}

static void r_llvm_c_attributes(RLlvmEmitter *emitter,
                                const RLlvmCSignature *signature,
                                LLVMValueRef target,
                                bool call_site) {
    const unsigned first =
        ((signature->result.abi_class == R_LLVM_ABI_INDIRECT) || signature->out_result) ? 2U : 1U;
    uint32_t index;

    if (signature->result.abi_class == R_LLVM_ABI_INDIRECT) {
        const RLlvmSurfaceType *type = &emitter->c_types->types[signature->result_index];
        if (call_site) {
            LLVMAddCallSiteAttribute(target, 1U, r_llvm_sret_attribute(emitter, type->size));
        } else {
            LLVMAddAttributeAtIndex(target, 1U, r_llvm_sret_attribute(emitter, type->size));
        }
    } else {
        r_llvm_add_extension(
            emitter, target, LLVMAttributeReturnIndex, signature->result.extend, call_site);
    }
    for (index = 0U; index < signature->count; ++index) {
        r_llvm_add_extension(
            emitter, target, first + index, signature->parameters[index].extend, call_site);
    }
}

/* A function of the module with the C ABI of the signature. */
static LLVMValueRef r_llvm_c_declare(RLlvmEmitter *emitter,
                                     const char *name,
                                     const RLlvmCSignature *signature,
                                     bool internal) {
    LLVMValueRef function = LLVMGetNamedFunction(emitter->module, name);

    if (function == NULL) {
        function = LLVMAddFunction(emitter->module, name, signature->type);
        if (internal) {
            LLVMSetLinkage(function, LLVMInternalLinkage);
        }
        r_llvm_c_attributes(emitter, signature, function, false);
    }
    return function;
}

/* ---- values across the C boundary ---- */

static uint32_t r_llvm_c_size(const RLlvmEmitter *emitter, uint32_t index) {
    return emitter->c_types->types[index].size;
}

static uint32_t r_llvm_c_align(const RLlvmEmitter *emitter, uint32_t index) {
    const uint32_t align = emitter->c_types->types[index].align;
    return align == 0U ? 1U : align;
}

/* One argument as the C ABI passes it, from its R-level value. */
static LLVMValueRef r_llvm_c_lower_argument(RLlvmEmitter *emitter,
                                            const RLlvmAbiValue *value,
                                            uint32_t index,
                                            LLVMValueRef argument) {
    if (value->abi_class == R_LLVM_ABI_DIRECT) {
        return argument;
    }
    if (value->abi_class == R_LLVM_ABI_COERCE) {
        LLVMTypeRef register_type = r_llvm_abi_register_type(emitter, value, false);
        const uint32_t register_size = (uint32_t)LLVMABISizeOfType(emitter->data, register_type);
        const uint32_t size = r_llvm_c_size(emitter, index);
        const uint32_t align = r_llvm_c_align(emitter, index);
        LLVMValueRef staging = r_llvm_entry_alloca(
            emitter, register_size > size ? register_size : size, align > 8U ? align : 8U, "");
        (void)LLVMBuildMemCpy(
            emitter->builder, staging, align, argument, align, r_llvm_u64(emitter, size));
        return LLVMBuildLoad2(emitter->builder, register_type, staging, "");
    }
    if (value->abi_class == R_LLVM_ABI_INDIRECT) {
        const uint32_t size = r_llvm_c_size(emitter, index);
        const uint32_t align = r_llvm_c_align(emitter, index);
        LLVMValueRef copy = r_llvm_entry_alloca(emitter, size, align, "");
        (void)LLVMBuildMemCpy(
            emitter->builder, copy, align, argument, align, r_llvm_u64(emitter, size));
        return copy;
    }
    (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    return NULL;
}

/* Calls `callee` with R-level arguments; `extra` are the types of the variadic arguments after
   the fixed ones. A result in memory is written to `result_memory`; a scalar result is returned
   (or a dummy value for none). */
static LLVMValueRef r_llvm_c_call(RLlvmEmitter *emitter,
                                  const RLlvmCSignature *signature,
                                  LLVMValueRef callee,
                                  const LLVMValueRef *arguments,
                                  const RTypeId *argument_types,
                                  uint32_t argument_count,
                                  LLVMValueRef result_memory) {
    LLVMValueRef lowered[2U * R_LLVM_C_MAX_PARAMETERS + 1U];
    RLlvmAbiValue extra_values[R_LLVM_C_MAX_PARAMETERS];
    unsigned lowered_count = 0U;
    uint32_t index;
    LLVMValueRef call;

    if ((argument_count < signature->count) ||
        (!signature->variadic && (argument_count != signature->count)) ||
        (argument_count > (uint32_t)R_LLVM_C_MAX_PARAMETERS)) {
        (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        return NULL;
    }
    if ((signature->result.abi_class == R_LLVM_ABI_INDIRECT) || signature->out_result) {
        if (result_memory == NULL) {
            (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
            return NULL;
        }
        lowered[lowered_count++] = result_memory;
    }
    for (index = 0U; index < argument_count; ++index) {
        const RLlvmAbiValue *value;
        uint32_t table_index;
        if (index < signature->count) {
            value = &signature->parameters[index];
            table_index = signature->parameter_indexes[index];
        } else {
            /* A variadic argument passes as its promoted type (R-FFI-0005). */
            RLlvmTypeTable table;
            if (!r_llvm_c_type(emitter, argument_types[index], 0U, &table_index)) {
                return NULL;
            }
            table = r_llvm_c_table(emitter);
            if (!r_llvm_abi_classify_argument(&table, table_index, &extra_values[index])) {
                (void)r_llvm_unsupported(emitter, "a variadic C argument the C ABI cannot pass");
                return NULL;
            }
            value = &extra_values[index];
        }
        lowered[lowered_count] =
            r_llvm_c_lower_argument(emitter, value, table_index, arguments[index]);
        if (lowered[lowered_count] == NULL) {
            return NULL;
        }
        lowered_count += 1U;
    }
    call = LLVMBuildCall2(emitter->builder, signature->type, callee, lowered, lowered_count, "");
    if (LLVMIsAFunction(callee) == NULL) {
        /* The stack bound takes the candidates of this call from the edges the caller added. */
        r_llvm_mark_indirect_call(emitter, call);
    }
    r_llvm_c_attributes(emitter, signature, call, true);
    for (index = signature->count; index < argument_count; ++index) {
        const unsigned position =
            index + (((signature->result.abi_class == R_LLVM_ABI_INDIRECT) || signature->out_result)
                         ? 2U
                         : 1U);
        r_llvm_add_extension(emitter, call, position, extra_values[index].extend, true);
    }
    if (signature->result.abi_class == R_LLVM_ABI_COERCE) {
        LLVMTypeRef register_type = r_llvm_abi_register_type(emitter, &signature->result, true);
        const uint32_t register_size = (uint32_t)LLVMABISizeOfType(emitter->data, register_type);
        const uint32_t size = r_llvm_c_size(emitter, signature->result_index);
        const uint32_t align = r_llvm_c_align(emitter, signature->result_index);
        LLVMValueRef staging = r_llvm_entry_alloca(
            emitter, register_size > size ? register_size : size, align > 8U ? align : 8U, "");
        if (result_memory == NULL) {
            (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
            return NULL;
        }
        (void)LLVMBuildStore(emitter->builder, call, staging);
        (void)LLVMBuildMemCpy(
            emitter->builder, result_memory, align, staging, align, r_llvm_u64(emitter, size));
    }
    return call;
}

/* The R-level values of the parameters of a function with the C ABI of the signature. */
static bool r_llvm_c_receive(RLlvmEmitter *emitter,
                             const RLlvmCSignature *signature,
                             LLVMValueRef function,
                             LLVMValueRef *values) {
    const unsigned first =
        ((signature->result.abi_class == R_LLVM_ABI_INDIRECT) || signature->out_result) ? 1U : 0U;
    uint32_t index;

    for (index = 0U; index < signature->count; ++index) {
        const RLlvmAbiValue *value = &signature->parameters[index];
        LLVMValueRef parameter = LLVMGetParam(function, first + index);
        if ((value->abi_class == R_LLVM_ABI_DIRECT) || (value->abi_class == R_LLVM_ABI_INDIRECT)) {
            /* An indirect argument is a copy the caller made for this call. */
            values[index] = parameter;
        } else if (value->abi_class == R_LLVM_ABI_COERCE) {
            const uint32_t register_size =
                (uint32_t)LLVMABISizeOfType(emitter->data, LLVMTypeOf(parameter));
            const uint32_t size = r_llvm_c_size(emitter, signature->parameter_indexes[index]);
            const uint32_t align = r_llvm_c_align(emitter, signature->parameter_indexes[index]);
            values[index] = r_llvm_entry_alloca(
                emitter, register_size > size ? register_size : size, align > 8U ? align : 8U, "");
            (void)LLVMBuildStore(emitter->builder, parameter, values[index]);
        } else {
            return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
    }
    return true;
}

/* Returns an R-level value (NULL for none) with the C ABI of the signature. */
static bool r_llvm_c_return(RLlvmEmitter *emitter,
                            const RLlvmCSignature *signature,
                            LLVMValueRef function,
                            LLVMValueRef value) {
    if (signature->out_result) {
        const uint32_t size = r_llvm_c_size(emitter, signature->result_index);
        const uint32_t align = r_llvm_c_align(emitter, signature->result_index);
        (void)LLVMBuildMemCpy(emitter->builder,
                              LLVMGetParam(function, 0U),
                              align,
                              value,
                              align,
                              r_llvm_u64(emitter, size));
        (void)LLVMBuildRetVoid(emitter->builder);
        return true;
    }
    switch (signature->result.abi_class) {
    case R_LLVM_ABI_IGNORE:
        (void)LLVMBuildRetVoid(emitter->builder);
        return true;
    case R_LLVM_ABI_DIRECT:
        (void)LLVMBuildRet(emitter->builder, value);
        return true;
    case R_LLVM_ABI_COERCE: {
        LLVMTypeRef register_type = r_llvm_abi_register_type(emitter, &signature->result, true);
        const uint32_t register_size = (uint32_t)LLVMABISizeOfType(emitter->data, register_type);
        const uint32_t size = r_llvm_c_size(emitter, signature->result_index);
        const uint32_t align = r_llvm_c_align(emitter, signature->result_index);
        LLVMValueRef staging = r_llvm_entry_alloca(
            emitter, register_size > size ? register_size : size, align > 8U ? align : 8U, "");
        (void)LLVMBuildMemCpy(
            emitter->builder, staging, align, value, align, r_llvm_u64(emitter, size));
        (void)LLVMBuildRet(emitter->builder,
                           LLVMBuildLoad2(emitter->builder, register_type, staging, ""));
        return true;
    }
    case R_LLVM_ABI_INDIRECT: {
        const uint32_t size = r_llvm_c_size(emitter, signature->result_index);
        const uint32_t align = r_llvm_c_align(emitter, signature->result_index);
        (void)LLVMBuildMemCpy(emitter->builder,
                              LLVMGetParam(function, 0U),
                              align,
                              value,
                              align,
                              r_llvm_u64(emitter, size));
        (void)LLVMBuildRetVoid(emitter->builder);
        return true;
    }
    default:
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
}

/* ---- ingress (R-FFI-0056) ---- */

static bool r_llvm_ingress_needed(RLlvmEmitter *emitter, RTypeId id, uint32_t nesting) {
    const RTypeId value = r_llvm_value_type(emitter, id);
    const RSemanticType *type = r_llvm_type(emitter, value);
    const RSemanticAggregate *aggregate;
    uint32_t index;

    if ((type == NULL) || (nesting > emitter->frontend->options.limits.max_nesting)) {
        return false;
    }
    switch (type->kind) {
    case R_SEMANTIC_TYPE_RAW:
    case R_SEMANTIC_TYPE_RAW_FUNCTION:
        return (type->flags & R_SEMANTIC_TYPE_FLAG_NULLABLE) == 0U;
    case R_SEMANTIC_TYPE_FIXED_ARRAY:
        return (type->length != 0U) && r_llvm_ingress_needed(emitter, type->base, nesting + 1U);
    case R_SEMANTIC_TYPE_ENUM:
        aggregate = r_llvm_aggregate(emitter, value);
        return (aggregate != NULL) && !aggregate->is_tagged;
    case R_SEMANTIC_TYPE_STRUCT:
        aggregate = r_llvm_aggregate(emitter, value);
        if ((aggregate == NULL) || !aggregate->is_repr_c) {
            return false;
        }
        for (index = 0U; index < aggregate->field_count; ++index) {
            if (r_llvm_ingress_needed(
                    emitter,
                    emitter->frontend->semantic_fields[(size_t)aggregate->first_field + index].type,
                    nesting + 1U)) {
                return true;
            }
        }
        return false;
    default:
        return false;
    }
}

/* A contract violation when `failed`; the builder continues on the other path. */
static bool r_llvm_ingress_fail_if(RLlvmEmitter *emitter, LLVMValueRef failed, RSourceSpan span) {
    LLVMBasicBlockRef violation =
        LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef next = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");

    (void)LLVMBuildCondBr(emitter->builder, failed, violation, next);
    LLVMPositionBuilderAtEnd(emitter->builder, violation);
    if (!r_llvm_panic(
            emitter, "R_RUNTIME_PANIC_CONTRACT_VIOLATION", span, R_MIR_BLOCK_ID_INVALID)) {
        return false;
    }
    LLVMPositionBuilderAtEnd(emitter->builder, next);
    return true;
}

/* Checks a value that comes from C: `value` is the scalar, or `memory` its storage. */
static bool r_llvm_ingress(RLlvmEmitter *emitter,
                           RTypeId id,
                           LLVMValueRef value,
                           LLVMValueRef memory,
                           RSourceSpan span,
                           uint32_t nesting) {
    const RTypeId value_type = r_llvm_value_type(emitter, id);
    const RSemanticType *type = r_llvm_type(emitter, value_type);
    const RSemanticAggregate *aggregate;
    uint32_t index;

    if (!r_llvm_ingress_needed(emitter, id, nesting)) {
        return true;
    }
    if ((value == NULL) && (r_llvm_scalar_type(emitter, id) != NULL)) {
        value = r_llvm_load_scalar(emitter, id, memory);
    }
    switch (type->kind) {
    case R_SEMANTIC_TYPE_RAW:
    case R_SEMANTIC_TYPE_RAW_FUNCTION:
        return r_llvm_ingress_fail_if(emitter, LLVMBuildIsNull(emitter->builder, value, ""), span);
    case R_SEMANTIC_TYPE_ENUM: {
        /* A C-origin enum value names a declared variant (R-CONF-G009). */
        LLVMValueRef named = LLVMConstInt(r_llvm_int(emitter, 1U), 0U, 0);
        aggregate = r_llvm_aggregate(emitter, value_type);
        for (index = 0U; index < aggregate->variant_count; ++index) {
            const RSemanticVariant *variant =
                &emitter->frontend->semantic_variants[(size_t)aggregate->first_variant + index];
            named = LLVMBuildOr(emitter->builder,
                                named,
                                LLVMBuildICmp(emitter->builder,
                                              LLVMIntEQ,
                                              value,
                                              LLVMConstInt(LLVMTypeOf(value), variant->value, 0),
                                              ""),
                                "");
        }
        return r_llvm_ingress_fail_if(emitter, LLVMBuildNot(emitter->builder, named, ""), span);
    }
    case R_SEMANTIC_TYPE_STRUCT:
        aggregate = r_llvm_aggregate(emitter, value_type);
        for (index = 0U; index < aggregate->field_count; ++index) {
            const RSemanticField *field =
                &emitter->frontend->semantic_fields[(size_t)aggregate->first_field + index];
            uint32_t offset = 0U;
            if (!r_llvm_ingress_needed(emitter, field->type, nesting + 1U)) {
                continue;
            }
            if (!r_llvm_field_offset(emitter, aggregate->first_field + index + 1U, &offset) ||
                !r_llvm_ingress(emitter,
                                field->type,
                                NULL,
                                r_llvm_byte_offset(emitter, memory, offset),
                                span,
                                nesting + 1U)) {
                return false;
            }
        }
        return true;
    case R_SEMANTIC_TYPE_FIXED_ARRAY: {
        uint32_t size = 0U;
        uint32_t align = 0U;
        LLVMBasicBlockRef header =
            LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        LLVMBasicBlockRef body =
            LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        LLVMBasicBlockRef done =
            LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        LLVMValueRef counter = r_llvm_entry_alloca(emitter, 8U, 8U, "ingress_index");
        LLVMValueRef current;
        LLVMValueRef offset;
        if (!r_llvm_layout(emitter, type->base, &size, &align)) {
            return false;
        }
        (void)LLVMBuildStore(emitter->builder, r_llvm_u64(emitter, 0U), counter);
        (void)LLVMBuildBr(emitter->builder, header);
        LLVMPositionBuilderAtEnd(emitter->builder, header);
        current = LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 64U), counter, "");
        (void)LLVMBuildCondBr(
            emitter->builder,
            LLVMBuildICmp(
                emitter->builder, LLVMIntULT, current, r_llvm_u64(emitter, type->length), ""),
            body,
            done);
        LLVMPositionBuilderAtEnd(emitter->builder, body);
        offset = LLVMBuildMul(emitter->builder, current, r_llvm_u64(emitter, size), "");
        if (!r_llvm_ingress(
                emitter,
                type->base,
                NULL,
                LLVMBuildGEP2(emitter->builder, r_llvm_int(emitter, 8U), memory, &offset, 1U, ""),
                span,
                nesting + 1U)) {
            return false;
        }
        (void)LLVMBuildStore(emitter->builder,
                             LLVMBuildAdd(emitter->builder, current, r_llvm_u64(emitter, 1U), ""),
                             counter);
        (void)LLVMBuildBr(emitter->builder, header);
        LLVMPositionBuilderAtEnd(emitter->builder, done);
        return true;
    }
    default:
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
}

bool r_llvm_ffi_ingress(RLlvmEmitter *emitter, RTypeId type, LLVMValueRef value, RSourceSpan span) {
    const bool scalar = r_llvm_scalar_type(emitter, type) != NULL;
    return r_llvm_ingress(emitter, type, scalar ? value : NULL, scalar ? NULL : value, span, 0U);
}

/* ---- C names, imports and thunks ---- */

static bool
r_llvm_c_name(RLlvmEmitter *emitter, const RSemanticSymbol *symbol, char *buffer, size_t capacity) {
    const char *bytes;
    size_t length;
    int written;

    if (symbol->c_name_intern_id != 0U) {
        const RInternEntry *name;
        if ((size_t)symbol->c_name_intern_id > emitter->frontend->intern_count) {
            return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
        name = &emitter->frontend->intern_entries[symbol->c_name_intern_id - 1U];
        bytes = (const char *)name->bytes;
        length = name->length;
    } else {
        const RSourceSpan span = symbol->c_name_span.end > symbol->c_name_span.start
                                     ? symbol->c_name_span
                                     : symbol->name_span;
        const RSource *source = r_get_source_const(emitter->frontend, span.source);
        if ((source == NULL) || (span.end <= span.start) || (span.end > source->length)) {
            return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
        bytes = (const char *)source->bytes + span.start;
        length = (size_t)(span.end - span.start);
    }
    written = snprintf(buffer, capacity, "%.*s", (int)length, bytes);
    return ((written > 0) && ((size_t)written < capacity))
               ? true
               : r_llvm_unsupported(emitter, "a C name of this length");
}

static LLVMValueRef r_llvm_c_bridge_marshal(RLlvmEmitter *emitter,
                                            RSymbolId id,
                                            const RSemanticSymbol *symbol,
                                            RLlvmCSignature *signature) {
    char c_name[256];
    char name[320];

    if (!r_llvm_c_name(emitter, symbol, c_name, sizeof(c_name)) ||
        !r_llvm_c_symbol_signature(emitter, symbol, true, signature)) {
        return NULL;
    }
    (void)snprintf(name, sizeof(name), "r_bridge_%s_%08" PRIu32, c_name, (uint32_t)id);
    return r_llvm_c_declare(emitter, name, signature, false);
}

/* The C function a call of an import reaches directly, with its signature: the import itself,
   its bridge, or for one that marshals the bridge with structs by address. */
static LLVMValueRef
r_llvm_c_import_callee(RLlvmEmitter *emitter, RSymbolId id, RLlvmCSignature *signature) {
    const RSemanticSymbol *symbol = &emitter->frontend->semantic_symbols[(size_t)id - 1U];
    char c_name[256];
    char name[320];

    if (symbol->poisoned) {
        (void)r_llvm_unsupported(emitter, "a poisoned C import");
        return NULL;
    }
    if (r_llvm_c_marshals(emitter, symbol)) {
        return r_llvm_c_bridge_marshal(emitter, id, symbol, signature);
    }
    if (!r_llvm_c_name(emitter, symbol, c_name, sizeof(c_name)) ||
        !r_llvm_c_symbol_signature(emitter, symbol, false, signature)) {
        return NULL;
    }
    if (symbol->passes_r_struct) {
        (void)snprintf(name, sizeof(name), "r_bridge_%s_%08" PRIu32, c_name, (uint32_t)id);
    } else {
        (void)snprintf(name, sizeof(name), "%s%s", symbol->uses_bridge ? "r_bridge_" : "", c_name);
    }
    return r_llvm_c_declare(emitter, name, signature, false);
}

/* The thunk with the import's own prototype that forwards to its marshalling bridge. */
static LLVMValueRef r_llvm_c_thunk(RLlvmEmitter *emitter, RSymbolId id) {
    const RSemanticSymbol *symbol = &emitter->frontend->semantic_symbols[(size_t)id - 1U];
    RLlvmCSignature own;
    RLlvmCSignature bridge;
    LLVMValueRef bridge_function;
    LLVMValueRef thunk;
    LLVMValueRef saved_function = emitter->function;
    LLVMBasicBlockRef saved_block = LLVMGetInsertBlock(emitter->builder);
    LLVMValueRef saved_frame = emitter->frame;
    LLVMValueRef values[R_LLVM_C_MAX_PARAMETERS];
    LLVMValueRef result = NULL;
    char c_name[256];
    char name[320];
    uint32_t size = 0U;
    uint32_t align = 0U;
    bool success;

    if (!r_llvm_c_name(emitter, symbol, c_name, sizeof(c_name)) ||
        !r_llvm_c_symbol_signature(emitter, symbol, false, &own)) {
        return NULL;
    }
    (void)snprintf(name, sizeof(name), "r_thunk_%s_%08" PRIu32, c_name, (uint32_t)id);
    thunk = LLVMGetNamedFunction(emitter->module, name);
    if (thunk != NULL) {
        return thunk;
    }
    bridge_function = r_llvm_c_bridge_marshal(emitter, id, symbol, &bridge);
    if (bridge_function == NULL) {
        return NULL;
    }
    thunk = r_llvm_c_declare(emitter, name, &own, true);
    emitter->function = thunk;
    emitter->frame = NULL;
    LLVMPositionBuilderAtEnd(emitter->builder,
                             LLVMAppendBasicBlockInContext(emitter->context, thunk, "entry"));
    success = r_llvm_c_receive(emitter, &own, thunk, values);
    if (success && !r_llvm_type_is_void(emitter, symbol->return_type) &&
        (r_llvm_scalar_type(emitter, symbol->return_type) == NULL)) {
        success = r_llvm_layout(emitter, symbol->return_type, &size, &align);
        result = success ? r_llvm_entry_alloca(emitter, size, align, "result") : NULL;
    }
    if (success) {
        LLVMValueRef call = r_llvm_c_call(
            emitter, &bridge, bridge_function, values, own.parameter_types, own.count, result);
        success = (call != NULL) &&
                  r_llvm_c_return(
                      emitter,
                      &own,
                      thunk,
                      (result != NULL) || r_llvm_type_is_void(emitter, symbol->return_type) ? result
                                                                                            : call);
    }
    emitter->function = saved_function;
    emitter->frame = saved_frame;
    if (saved_block != NULL) {
        LLVMPositionBuilderAtEnd(emitter->builder, saved_block);
    }
    return success ? thunk : NULL;
}

/* The export wrapper of an extern "C" definition, declared; its body is written after the
   functions of the program (r_llvm_emit_c_exports). */
static LLVMValueRef r_llvm_c_export(RLlvmEmitter *emitter, RSymbolId id) {
    const RSemanticSymbol *symbol = &emitter->frontend->semantic_symbols[(size_t)id - 1U];
    RLlvmCSignature signature;
    char name[256];

    if (emitter->c_functions[id] != NULL) {
        return emitter->c_functions[id];
    }
    if (symbol->is_async || (symbol->effect_carrier_type != R_TYPE_ID_INVALID) ||
        !r_llvm_c_name(emitter, symbol, name, sizeof(name)) ||
        !r_llvm_c_symbol_signature(emitter, symbol, false, &signature)) {
        return (emitter->status == R_FRONTEND_OK)
                   ? (r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR), NULL)
                   : NULL;
    }
    emitter->c_functions[id] = r_llvm_c_declare(emitter, name, &signature, false);
    return emitter->c_functions[id];
}

/* ---- instructions ---- */

/* R-FFI-0025: the address of a C function as a raw fn value. */
bool r_llvm_ffi_function_address(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RSemanticSymbol *symbol =
        ((instruction->symbol == R_SYMBOL_ID_INVALID) ||
         ((size_t)instruction->symbol > emitter->frontend->semantic_symbol_count))
            ? NULL
            : &emitter->frontend->semantic_symbols[(size_t)instruction->symbol - 1U];
    LLVMValueRef function;
    RLlvmCSignature signature;

    if ((symbol == NULL) || !symbol->is_extern_c) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    if (symbol->is_import) {
        function = r_llvm_c_marshals(emitter, symbol)
                       ? r_llvm_c_thunk(emitter, instruction->symbol)
                       : r_llvm_c_import_callee(emitter, instruction->symbol, &signature);
    } else {
        function = r_llvm_c_export(emitter, instruction->symbol);
    }
    return (function != NULL) && r_llvm_set_value(emitter, instruction->result, function);
}

/* A call of a raw fn value or of an import (r_c17_emit_indirect_call). */
bool r_llvm_ffi_indirect_call(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RFrontendContext *context = emitter->frontend;
    const RMirValueId *operands = &context->mir_operands[instruction->first_operand];
    const RMirInstruction *callee_definition;
    const RSemanticTypeKind kind = r_llvm_value_kind(emitter, instruction->type);
    const bool never_returns = kind == R_SEMANTIC_TYPE_NEVER;
    const bool has_result = !never_returns && (kind != R_SEMANTIC_TYPE_VOID);
    const bool hosted = context->profile != R_FRONTEND_PROFILE_FREESTANDING;
    LLVMValueRef arguments[R_LLVM_C_MAX_PARAMETERS];
    RTypeId argument_types[R_LLVM_C_MAX_PARAMETERS];
    RLlvmCSignature signature;
    LLVMValueRef callee;
    LLVMValueRef environment = NULL;
    LLVMValueRef result_memory = NULL;
    LLVMValueRef call;
    uint32_t index;

    if ((instruction->operand_count == 0U) ||
        (instruction->operand_count - 1U > (uint32_t)R_LLVM_C_MAX_PARAMETERS)) {
        return r_llvm_unsupported(emitter, "a C call with this many arguments");
    }
    callee_definition = r_llvm_definition(emitter, operands[0]);
    if (callee_definition == NULL) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    if ((callee_definition->kind == R_MIR_INSTRUCTION_FUNCTION_ADDRESS) &&
        (callee_definition->symbol != R_SYMBOL_ID_INVALID) &&
        context->semantic_symbols[(size_t)callee_definition->symbol - 1U].is_import) {
        /* A direct call of a C import reaches no R callback directly (R-FUNC-0004). */
        callee = r_llvm_c_import_callee(emitter, callee_definition->symbol, &signature);
        if (callee == NULL) {
            return false;
        }
    } else {
        const RSemanticType *type =
            r_llvm_type(emitter, r_llvm_value_type(emitter, callee_definition->type));
        size_t symbol_index;
        if ((type == NULL) ||
            !r_llvm_c_function_type_signature(emitter, callee_definition->type, &signature)) {
            return type == NULL ? r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR) : false;
        }
        callee = r_llvm_value(emitter, operands[0]);
        if (callee == NULL) {
            return false;
        }
        if (((type->flags & R_SEMANTIC_TYPE_FLAG_NULLABLE) != 0U) &&
            !r_llvm_ingress_fail_if(
                emitter, LLVMBuildIsNull(emitter->builder, callee, ""), instruction->span)) {
            return false;
        }
        /* R-FUNC-0007: the call may reach every R callback whose address matches the signature,
           which the stack bound counts on this path (R_STACK_INDIRECT of the C17 emitter). */
        for (symbol_index = 1U; symbol_index <= context->semantic_symbol_count; ++symbol_index) {
            const RSemanticSymbol *candidate = &context->semantic_symbols[symbol_index - 1U];
            if ((candidate->kind != R_SEMANTIC_SYMBOL_FUNCTION) || !candidate->is_callback ||
                candidate->is_import || (emitter->functions[symbol_index] == NULL) ||
                !r_semantic_callback_may_be_target(
                    context, (RSymbolId)symbol_index, callee_definition->type)) {
                continue;
            }
            if (!r_llvm_add_stack_edge(
                    emitter, emitter->function, emitter->functions[symbol_index])) {
                return false;
            }
        }
    }
    for (index = 1U; index < instruction->operand_count; ++index) {
        const RMirInstruction *definition = r_llvm_definition(emitter, operands[index]);
        arguments[index - 1U] = r_llvm_value(emitter, operands[index]);
        if ((definition == NULL) || (arguments[index - 1U] == NULL)) {
            return definition == NULL ? r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR) : false;
        }
        argument_types[index - 1U] = definition->type;
    }
    if (has_result && (r_llvm_scalar_type(emitter, instruction->type) == NULL)) {
        result_memory = r_llvm_value_memory(emitter, instruction->result, instruction->type);
        if (result_memory == NULL) {
            return false;
        }
    }
    if (hosted) {
        uint32_t size = 0U;
        uint32_t align = 0U;
        if (!r_llvm_runtime_layout(emitter, "RRuntimeCEnvironment", &size, &align)) {
            return false;
        }
        environment = r_llvm_entry_alloca(emitter, size, align, "c_environment");
        if (r_llvm_call_runtime(emitter, "r_runtime_c_call_begin", &environment, 1U, NULL) ==
            NULL) {
            return false;
        }
    }
    call = r_llvm_c_call(emitter,
                         &signature,
                         callee,
                         arguments,
                         argument_types,
                         instruction->operand_count - 1U,
                         result_memory);
    if (call == NULL) {
        return false;
    }
    if (hosted &&
        (r_llvm_call_runtime(emitter, "r_runtime_c_call_end", &environment, 1U, NULL) == NULL)) {
        return false;
    }
    /* No unwind test: C code cannot leave a panic pending, a callback ends the process at its
       own boundary (R-ERR-0006). */
    if (never_returns) {
        /* R-FUNC-0003: a C function whose R result is never shall not return. */
        return r_llvm_panic(emitter,
                            "R_RUNTIME_PANIC_CONTRACT_VIOLATION",
                            instruction->span,
                            R_MIR_BLOCK_ID_INVALID);
    }
    if (!has_result) {
        return true;
    }
    if (result_memory != NULL) {
        return r_llvm_ffi_ingress(emitter, instruction->type, result_memory, instruction->span);
    }
    return r_llvm_ffi_ingress(emitter, instruction->type, call, instruction->span) &&
           r_llvm_set_value(emitter, instruction->result, call);
}

/* ---- export wrappers ---- */

/* r_c17_emit_c_export_wrapper: enters R from C, checks the arguments, calls the R body and ends
   the process on a panic that reaches the boundary (R-ERR-0006). */
static bool r_llvm_c_export_body(RLlvmEmitter *emitter, RSymbolId id) {
    const RSemanticSymbol *symbol = &emitter->frontend->semantic_symbols[(size_t)id - 1U];
    const bool hosted = emitter->frontend->profile != R_FRONTEND_PROFILE_FREESTANDING;
    LLVMValueRef wrapper = emitter->c_functions[id];
    LLVMValueRef values[R_LLVM_C_MAX_PARAMETERS];
    LLVMValueRef call_arguments[R_LLVM_C_MAX_PARAMETERS + 1];
    LLVMValueRef guard = NULL;
    LLVMValueRef result = NULL;
    LLVMValueRef arguments[2];
    RLlvmCSignature signature;
    unsigned count = 0U;
    uint32_t index;
    const bool in_memory = !r_llvm_type_is_void(emitter, symbol->return_type) &&
                           (r_llvm_scalar_type(emitter, symbol->return_type) == NULL);

    if ((wrapper == NULL) || (emitter->functions[id] == NULL) ||
        !r_llvm_c_symbol_signature(emitter, symbol, false, &signature)) {
        return (emitter->status == R_FRONTEND_OK) ? r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR)
                                                  : false;
    }
    emitter->function = wrapper;
    emitter->frame = NULL;
    emitter->mir = NULL;
    LLVMPositionBuilderAtEnd(emitter->builder,
                             LLVMAppendBasicBlockInContext(emitter->context, wrapper, "entry"));
    if (hosted) {
        uint32_t size = 0U;
        uint32_t align = 0U;
        if (!r_llvm_runtime_layout(emitter, "RRuntimeCEntryGuard", &size, &align)) {
            return false;
        }
        guard = r_llvm_entry_alloca(emitter, size, align, "c_entry");
        if (r_llvm_call_runtime(emitter, "r_runtime_c_entry_begin", &guard, 1U, NULL) == NULL) {
            return false;
        }
    }
    if (!r_llvm_c_receive(emitter, &signature, wrapper, values)) {
        return false;
    }
    /* R-CMAP-0020: every C-origin argument is valid R before the body runs. */
    for (index = 0U; index < symbol->parameter_count; ++index) {
        if (!r_llvm_ffi_ingress(
                emitter, signature.parameter_types[index], values[index], symbol->name_span)) {
            return false;
        }
    }
    arguments[0] = r_llvm_stack_entry(emitter, id);
    arguments[1] = r_llvm_span(emitter, symbol->name_span);
    if ((arguments[0] == NULL) || (arguments[1] == NULL) ||
        (r_llvm_call_runtime(emitter, "r_runtime_stack_require", arguments, 2U, NULL) == NULL)) {
        return false;
    }
    if (in_memory) {
        uint32_t size = 0U;
        uint32_t align = 0U;
        if (!r_llvm_layout(emitter, symbol->return_type, &size, &align)) {
            return false;
        }
        result = r_llvm_entry_alloca(emitter, size, align, "result");
        call_arguments[count++] = result;
    }
    for (index = 0U; index < symbol->parameter_count; ++index) {
        /* A parameter in memory is a copy the body owns. */
        if (r_llvm_scalar_type(emitter, signature.parameter_types[index]) == NULL) {
            uint32_t size = 0U;
            uint32_t align = 0U;
            LLVMValueRef copy;
            if (!r_llvm_layout(emitter, signature.parameter_types[index], &size, &align)) {
                return false;
            }
            copy = r_llvm_entry_alloca(emitter, size, align, "argument");
            (void)LLVMBuildMemCpy(
                emitter->builder, copy, align, values[index], align, r_llvm_u64(emitter, size));
            call_arguments[count++] = copy;
        } else {
            call_arguments[count++] = values[index];
        }
    }
    {
        LLVMValueRef call = LLVMBuildCall2(emitter->builder,
                                           emitter->function_types[id],
                                           emitter->functions[id],
                                           call_arguments,
                                           count,
                                           "");
        if (!in_memory && !r_llvm_type_is_void(emitter, symbol->return_type)) {
            result = call;
        }
    }
    if (hosted) {
        LLVMBasicBlockRef terminate = LLVMAppendBasicBlockInContext(emitter->context, wrapper, "");
        LLVMBasicBlockRef next = LLVMAppendBasicBlockInContext(emitter->context, wrapper, "");
        LLVMValueRef unwinding = r_llvm_unwinding(emitter);
        if (unwinding == NULL) {
            return false;
        }
        (void)LLVMBuildCondBr(emitter->builder, unwinding, terminate, next);
        LLVMPositionBuilderAtEnd(emitter->builder, terminate);
        if (r_llvm_call_runtime(emitter, "r_runtime_unwind_terminate", NULL, 0U, NULL) == NULL) {
            return false;
        }
        (void)LLVMBuildUnreachable(emitter->builder);
        LLVMPositionBuilderAtEnd(emitter->builder, next);
        if (r_llvm_call_runtime(emitter, "r_runtime_c_entry_end", &guard, 1U, NULL) == NULL) {
            return false;
        }
    }
    if (r_llvm_value_kind(emitter, symbol->return_type) == R_SEMANTIC_TYPE_NEVER) {
        (void)LLVMBuildUnreachable(emitter->builder);
        return true;
    }
    return r_llvm_c_return(emitter, &signature, wrapper, result);
}

bool r_llvm_emit_c_exports(RLlvmEmitter *emitter) {
    size_t index;

    for (index = 1U; index <= emitter->frontend->semantic_symbol_count; ++index) {
        const RSemanticSymbol *symbol = &emitter->frontend->semantic_symbols[index - 1U];
        if ((symbol->kind != R_SEMANTIC_SYMBOL_FUNCTION) || !symbol->is_extern_c ||
            symbol->is_import || (emitter->functions[index] == NULL)) {
            continue;
        }
        if ((r_llvm_c_export(emitter, (RSymbolId)index) == NULL) ||
            !r_llvm_c_export_body(emitter, (RSymbolId)index)) {
            return false;
        }
    }
    emitter->function = NULL;
    return true;
}

/* ---- imported objects ---- */

/* R-FFI-0022: the address of an imported C object: its extern global, or the address the
   bridge accessor returns for this thread. */
LLVMValueRef r_llvm_ffi_object(RLlvmEmitter *emitter, RSymbolId id) {
    const RSemanticSymbol *symbol = &emitter->frontend->semantic_symbols[(size_t)id - 1U];
    char c_name[256];
    char name[300];
    uint32_t size = 0U;
    uint32_t align = 0U;
    LLVMValueRef global;

    if (symbol->poisoned || !r_llvm_c_name(emitter, symbol, c_name, sizeof(c_name)) ||
        !r_llvm_layout(emitter, symbol->type, &size, &align)) {
        if (symbol->poisoned) {
            (void)r_llvm_unsupported(emitter, "a poisoned C object");
        }
        return NULL;
    }
    if (symbol->uses_bridge || symbol->passes_r_struct) {
        LLVMTypeRef type = LLVMFunctionType(r_llvm_pointer(emitter), NULL, 0U, 0);
        LLVMValueRef accessor;
        if (symbol->passes_r_struct) {
            (void)snprintf(name, sizeof(name), "r_bridge_%s_%08" PRIu32, c_name, (uint32_t)id);
        } else {
            (void)snprintf(name, sizeof(name), "r_bridge_%s", c_name);
        }
        accessor = LLVMGetNamedFunction(emitter->module, name);
        if (accessor == NULL) {
            accessor = LLVMAddFunction(emitter->module, name, type);
        }
        return LLVMBuildCall2(emitter->builder, type, accessor, NULL, 0U, "");
    }
    global = LLVMGetNamedGlobal(emitter->module, c_name);
    if (global == NULL) {
        global = LLVMAddGlobal(emitter->module,
                               LLVMArrayType2(r_llvm_int(emitter, 8U), size == 0U ? 1U : size),
                               c_name);
        LLVMSetLinkage(global, LLVMExternalLinkage);
        LLVMSetAlignment(global, align == 0U ? 1U : align);
        if (symbol->is_thread_local) {
            LLVMSetThreadLocal(global, 1);
        }
    }
    return global;
}

/* ---- std.c::link_available (R-SLIB-C-0005) ---- */

typedef struct RLlvmLinkEntry {
    uint8_t *name;
    size_t length;
    bool available;
} RLlvmLinkEntry;

typedef struct RLlvmLinkEntries {
    RLlvmEmitter *emitter;
    RLlvmLinkEntry *items;
    size_t count;
    size_t capacity;
} RLlvmLinkEntries;

static bool r_llvm_link_collect(void *user_data, RLinkManifestEntryView source) {
    RLlvmLinkEntries *entries = user_data;

    if (entries->count == entries->capacity) {
        const size_t capacity = entries->capacity == 0U ? 8U : entries->capacity * 2U;
        RLlvmLinkEntry *grown = r_llvm_allocate(entries->emitter, capacity * sizeof(*grown));
        if (grown == NULL) {
            return false;
        }
        if (entries->count != 0U) {
            (void)memcpy(grown, entries->items, entries->count * sizeof(*grown));
        }
        r_llvm_free(entries->emitter, entries->items);
        entries->items = grown;
        entries->capacity = capacity;
    }
    entries->items[entries->count].name =
        r_llvm_allocate(entries->emitter, source.logical_name_length + 1U);
    if (entries->items[entries->count].name == NULL) {
        return false;
    }
    (void)memcpy(
        entries->items[entries->count].name, source.logical_name, source.logical_name_length);
    entries->items[entries->count].length = source.logical_name_length;
    entries->items[entries->count].available = source.available;
    entries->count += 1U;
    return true;
}

static int r_llvm_link_compare(const RLlvmLinkEntry *left, const RLlvmLinkEntry *right) {
    const size_t common = left->length < right->length ? left->length : right->length;
    const int comparison = common == 0U ? 0 : memcmp(left->name, right->name, common);

    if (comparison != 0) {
        return comparison;
    }
    return left->length < right->length ? -1 : (left->length > right->length ? 1 : 0);
}

/* The manifest view the program reads, sorted by logical name (r_c17_emit_link_manifest). */
static LLVMValueRef r_llvm_link_manifest(RLlvmEmitter *emitter) {
    RLlvmLinkEntries entries;
    uint32_t view_size = 0U;
    uint32_t view_align = 0U;
    uint32_t entry_size = 0U;
    uint32_t entry_align = 0U;
    uint32_t entries_offset = 0U;
    uint32_t count_offset = 0U;
    uint32_t name_offset = 0U;
    uint32_t available_offset = 0U;
    uint32_t data_offset = 0U;
    uint32_t length_offset = 0U;
    LLVMValueRef table = NULL;
    LLVMValueRef manifest = NULL;
    size_t index;
    bool success = false;

    if (emitter->link_manifest != NULL) {
        return emitter->link_manifest;
    }
    (void)memset(&entries, 0, sizeof(entries));
    entries.emitter = emitter;
    if (!r_llvm_runtime_layout(emitter, "RStdCLinkManifestView", &view_size, &view_align) ||
        !r_llvm_runtime_layout(emitter, "RStdCLinkManifestEntry", &entry_size, &entry_align) ||
        !r_llvm_runtime_field(emitter, "RStdCLinkManifestView", "entries", &entries_offset, NULL) ||
        !r_llvm_runtime_field(emitter, "RStdCLinkManifestView", "count", &count_offset, NULL) ||
        !r_llvm_runtime_field(
            emitter, "RStdCLinkManifestEntry", "logical_name", &name_offset, NULL) ||
        !r_llvm_runtime_field(
            emitter, "RStdCLinkManifestEntry", "available", &available_offset, NULL) ||
        !r_llvm_runtime_field(emitter, "RStdStringView", "data", &data_offset, NULL) ||
        !r_llvm_runtime_field(emitter, "RStdStringView", "length", &length_offset, NULL)) {
        return NULL;
    }
    if ((emitter->artifact_options != NULL) && (emitter->artifact_options->link_manifest != NULL)) {
        if (r_link_manifest_visit(emitter->artifact_options->link_manifest,
                                  emitter->artifact_options->link_manifest_length,
                                  r_llvm_link_collect,
                                  &entries,
                                  NULL) != R_LINK_MANIFEST_VISIT_OK) {
            (void)r_llvm_fail(emitter, R_FRONTEND_INVALID_ARGUMENT);
            goto cleanup;
        }
    }
    for (index = 1U; index < entries.count; ++index) {
        const RLlvmLinkEntry value = entries.items[index];
        size_t position = index;
        while ((position != 0U) &&
               (r_llvm_link_compare(&value, &entries.items[position - 1U]) < 0)) {
            entries.items[position] = entries.items[position - 1U];
            position -= 1U;
        }
        entries.items[position] = value;
    }
    for (index = 1U; index < entries.count; ++index) {
        if (r_llvm_link_compare(&entries.items[index - 1U], &entries.items[index]) == 0) {
            (void)r_llvm_fail(emitter, R_FRONTEND_INVALID_ARGUMENT);
            goto cleanup;
        }
    }
    {
        /* Entries {name {data, length}, available} and the view {entries, count} as byte images
           with the pointers patched in as relocations. */
        LLVMTypeRef byte = r_llvm_int(emitter, 8U);
        LLVMValueRef *rows = r_llvm_allocate(emitter, (entries.count + 1U) * sizeof(*rows));
        LLVMTypeRef row_fields[3];
        LLVMTypeRef row_type;
        if (rows == NULL) {
            goto cleanup;
        }
        /* The row: {ptr data, i64 length, [entry_size - 16] tail} with `available` in the tail. */
        if ((name_offset != 0U) || (data_offset != 0U) || (length_offset != 8U) ||
            (available_offset < 16U) || (entry_size < available_offset + 1U)) {
            r_llvm_free(emitter, rows);
            (void)r_llvm_unsupported(emitter, "this layout of RStdCLinkManifestEntry");
            goto cleanup;
        }
        row_fields[0] = r_llvm_pointer(emitter);
        row_fields[1] = r_llvm_int(emitter, 64U);
        row_fields[2] = LLVMArrayType2(byte, entry_size - 16U);
        row_type = LLVMStructTypeInContext(emitter->context, row_fields, 3U, 1);
        for (index = 0U; index < entries.count; ++index) {
            LLVMValueRef name = LLVMConstStringInContext2(emitter->context,
                                                          (const char *)entries.items[index].name,
                                                          entries.items[index].length,
                                                          1);
            LLVMValueRef name_global =
                LLVMAddGlobal(emitter->module, LLVMTypeOf(name), "r_link_name");
            LLVMValueRef *tail = r_llvm_allocate(emitter, (entry_size - 16U + 1U) * sizeof(*tail));
            LLVMValueRef members[3];
            uint32_t byte_index;
            if (tail == NULL) {
                r_llvm_free(emitter, rows);
                goto cleanup;
            }
            LLVMSetInitializer(name_global, name);
            LLVMSetGlobalConstant(name_global, 1);
            LLVMSetLinkage(name_global, LLVMPrivateLinkage);
            for (byte_index = 0U; byte_index < entry_size - 16U; ++byte_index) {
                tail[byte_index] = LLVMConstInt(
                    byte,
                    (byte_index + 16U == available_offset) && entries.items[index].available ? 1U
                                                                                             : 0U,
                    0);
            }
            members[0] = name_global;
            members[1] = r_llvm_u64(emitter, entries.items[index].length);
            members[2] = LLVMConstArray2(byte, tail, entry_size - 16U);
            rows[index] = LLVMConstStructInContext(emitter->context, members, 3U, 1);
            r_llvm_free(emitter, tail);
        }
        if (entries.count != 0U) {
            LLVMValueRef rows_value = LLVMConstArray2(row_type, rows, entries.count);
            table =
                LLVMAddGlobal(emitter->module, LLVMTypeOf(rows_value), "r_link_manifest_entries");
            LLVMSetInitializer(table, rows_value);
            LLVMSetGlobalConstant(table, 1);
            LLVMSetLinkage(table, LLVMPrivateLinkage);
            LLVMSetAlignment(table, entry_align);
        }
        r_llvm_free(emitter, rows);
        if ((entries_offset != 0U) || (count_offset != 8U) || (view_size != 16U)) {
            (void)r_llvm_unsupported(emitter, "this layout of RStdCLinkManifestView");
            goto cleanup;
        }
        {
            LLVMValueRef view_members[2];
            LLVMValueRef view;
            view_members[0] = table == NULL ? LLVMConstNull(r_llvm_pointer(emitter)) : table;
            view_members[1] = r_llvm_u64(emitter, entries.count);
            view = LLVMConstStructInContext(emitter->context, view_members, 2U, 0);
            manifest = LLVMAddGlobal(emitter->module, LLVMTypeOf(view), "r_link_manifest");
            LLVMSetInitializer(manifest, view);
            LLVMSetGlobalConstant(manifest, 1);
            LLVMSetLinkage(manifest, LLVMPrivateLinkage);
            LLVMSetAlignment(manifest, view_align);
        }
    }
    emitter->link_manifest = manifest;
    success = true;

cleanup:
    for (index = 0U; index < entries.count; ++index) {
        r_llvm_free(emitter, entries.items[index].name);
    }
    r_llvm_free(emitter, entries.items);
    return success ? manifest : NULL;
}

/* `std.c::link_available(name)`: the availability the link manifest resolved, false for a name
   of invalid syntax (r_std_c_link_available). */
bool r_llvm_ffi_link_available(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    LLVMValueRef arguments[2];
    LLVMValueRef available;

    arguments[0] = r_llvm_link_manifest(emitter);
    arguments[1] = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 0U));
    if ((arguments[0] == NULL) || (arguments[1] == NULL)) {
        return false;
    }
    available = r_llvm_call_runtime(emitter, "r_std_c_link_available", arguments, 2U, NULL);
    if (available == NULL) {
        return false;
    }
    if (LLVMTypeOf(available) != r_llvm_scalar_type(emitter, instruction->type)) {
        available = LLVMBuildTrunc(
            emitter->builder, available, r_llvm_scalar_type(emitter, instruction->type), "");
    }
    return r_llvm_set_value(emitter, instruction->result, available);
}

/* ---- std.c strings, handles and thread attachments (R-SLIB-C) ---- */

/* A checked std.c call: {status, value, error} into the carrier of the instruction. */
static bool r_llvm_std_c_checked(RLlvmEmitter *emitter,
                                 const RMirInstruction *instruction,
                                 const char *function,
                                 const char *result_type,
                                 LLVMValueRef *arguments,
                                 unsigned count) {
    LLVMValueRef native = r_llvm_std_structure(emitter, result_type);

    return (native != NULL) &&
           (r_llvm_call_runtime(emitter, function, arguments, count, native) != NULL) &&
           r_llvm_std_carrier(
               emitter,
               instruction,
               r_llvm_std_status_is(emitter, native, result_type, "status", "R_STD_C_CALL_SUCCESS"),
               r_llvm_std_member(emitter, native, result_type, "value"),
               r_llvm_std_member(emitter, native, result_type, "error"));
}

bool r_llvm_ffi_std_c(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RStandardCallOperation operation = instruction->standard_operation;
    const RMirValueId operand = instruction->operand_count == 0U
                                    ? R_MIR_VALUE_ID_INVALID
                                    : r_llvm_std_operand(emitter, instruction, 0U);
    LLVMValueRef arguments[2];

    switch (operation) {
    case R_STANDARD_CALL_C_STRING_FROM_STR:
        /* A str is {data, length}, the layout of RStdStringView. */
        arguments[0] = r_llvm_std_allocator(emitter);
        arguments[1] = r_llvm_value(emitter, operand);
        return (arguments[0] != NULL) && (arguments[1] != NULL) &&
               r_llvm_std_c_checked(emitter,
                                    instruction,
                                    "r_std_c_string_from_str",
                                    "RStdCStringResult",
                                    arguments,
                                    2U);
    case R_STANDARD_CALL_C_VALIDATE_UTF8:
        /* A const c_char[] slice is {data, length}, the layout of RStdCCharSlice. */
        arguments[0] = r_llvm_value(emitter, operand);
        return (arguments[0] != NULL) && r_llvm_std_c_checked(emitter,
                                                              instruction,
                                                              "r_std_c_validate_utf8",
                                                              "RStdCValidateUtf8Result",
                                                              arguments,
                                                              1U);
    case R_STANDARD_CALL_C_COPY_UTF8:
        arguments[0] = r_llvm_std_allocator(emitter);
        arguments[1] = r_llvm_value(emitter, operand);
        return (arguments[0] != NULL) && (arguments[1] != NULL) &&
               r_llvm_std_c_checked(
                   emitter, instruction, "r_std_c_copy_utf8", "RStdCCopyUtf8Result", arguments, 2U);
    case R_STANDARD_CALL_C_ATTACH_THREAD:
        return r_llvm_std_c_checked(
            emitter, instruction, "r_std_c_attach_thread", "RStdCAttachThreadResult", NULL, 0U);
    case R_STANDARD_CALL_C_STRING_AS_SLICE: {
        LLVMValueRef result = r_llvm_std_result(emitter, instruction);
        arguments[0] = r_llvm_value(emitter, operand);
        return (result != NULL) && (arguments[0] != NULL) &&
               (r_llvm_call_runtime(emitter, "r_std_c_string_as_slice", arguments, 1U, result) !=
                NULL);
    }
    case R_STANDARD_CALL_C_STRING_AS_PTR:
    case R_STANDARD_CALL_C_HANDLE_POINTER: {
        LLVMValueRef pointer;
        arguments[0] = r_llvm_value(emitter, operand);
        pointer = arguments[0] == NULL
                      ? NULL
                      : r_llvm_call_runtime(emitter,
                                            operation == R_STANDARD_CALL_C_STRING_AS_PTR
                                                ? "r_std_c_string_as_ptr"
                                                : "r_std_c_handle_pointer",
                                            arguments,
                                            1U,
                                            NULL);
        return (pointer != NULL) && r_llvm_set_value(emitter, instruction->result, pointer);
    }
    case R_STANDARD_CALL_C_RELEASE_HANDLE:
    case R_STANDARD_CALL_C_DETACH_THREAD: {
        /* The handle or attachment moves into the call, which leaves nothing to drop. */
        const bool release = operation == R_STANDARD_CALL_C_RELEASE_HANDLE;
        LLVMValueRef pointer;
        arguments[0] = r_llvm_value(emitter, operand);
        pointer = arguments[0] == NULL ? NULL
                                       : r_llvm_call_runtime(emitter,
                                                             release ? "r_std_c_release_handle"
                                                                     : "r_std_c_detach_thread",
                                                             arguments,
                                                             1U,
                                                             NULL);
        if (pointer == NULL) {
            return false;
        }
        r_llvm_set_flag(emitter, r_llvm_value_flag(emitter, operand), false);
        return !release || r_llvm_set_value(emitter, instruction->result, pointer);
    }
    case R_STANDARD_CALL_C_ADOPT_HANDLE: {
        LLVMValueRef result = r_llvm_std_result(emitter, instruction);
        arguments[0] = r_llvm_value(emitter, operand);
        arguments[1] = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 1U));
        if ((result == NULL) || (arguments[0] == NULL) || (arguments[1] == NULL) ||
            (r_llvm_call_runtime(emitter, "r_std_c_adopt_handle", arguments, 2U, result) == NULL)) {
            return false;
        }
        r_llvm_std_initialized(emitter, instruction);
        return true;
    }
    default:
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
}

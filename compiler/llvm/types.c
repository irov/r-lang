#include "emit_internal.h"

#include "named_standard_copy_abi.h"
#include "named_standard_move_abi.h"

#include <string.h>

/* Types of the LLVM emitter. A value has the representation the C17 emitter gives it: a scalar is
   an SSA value of its LLVM type, every other value lives in memory laid out as the C type the C17
   emitter declares for it (r_a structs, r_d derived structs, runtime and library types from the
   runtime surface). Layouts follow the C rules of the target: members in order, each at its
   alignment, the size rounded up to the largest alignment. */

const RSemanticType *r_llvm_type(const RLlvmEmitter *emitter, RTypeId id) {
    return ((id == R_TYPE_ID_INVALID) || ((size_t)id > emitter->frontend->semantic_type_count))
               ? NULL
               : &emitter->frontend->semantic_types[(size_t)id - 1U];
}

RTypeId r_llvm_value_type(const RLlvmEmitter *emitter, RTypeId id) {
    const RSemanticType *type;

    id = r_semantic_representation_type(emitter->frontend, id);
    type = r_llvm_type(emitter, id);
    return ((type != NULL) && (type->kind == R_SEMANTIC_TYPE_CONST))
               ? r_semantic_representation_type(emitter->frontend, type->base)
               : id;
}

RSemanticTypeKind r_llvm_value_kind(const RLlvmEmitter *emitter, RTypeId id) {
    const RTypeId value_id = r_llvm_value_type(emitter, id);
    const RSemanticType *type = r_llvm_type(emitter, value_id);

    if (type == NULL) {
        return R_SEMANTIC_TYPE_INVALID;
    }
    if (type->kind == R_SEMANTIC_TYPE_C_ABI_NUMERIC) {
        /* The non-integer C types by the target manifest: c_bool is _Bool, c_float binary32,
           c_double and c_long_double binary64 (arm64-apple-darwin, `representation`). */
        switch ((RTokenKind)type->length) {
        case R_TOKEN_KW_C_BOOL:
            return R_SEMANTIC_TYPE_BOOL;
        case R_TOKEN_KW_C_FLOAT:
            return R_SEMANTIC_TYPE_F32;
        case R_TOKEN_KW_C_DOUBLE:
        case R_TOKEN_KW_C_LONG_DOUBLE:
            return R_SEMANTIC_TYPE_F64;
        default:
            return r_semantic_integer_representation(emitter->frontend, value_id);
        }
    }
    return type->kind;
}

bool r_llvm_kind_integer(RSemanticTypeKind kind, unsigned *bits, bool *is_signed) {
    switch (kind) {
    case R_SEMANTIC_TYPE_I8:
        *bits = 8U;
        *is_signed = true;
        return true;
    case R_SEMANTIC_TYPE_I16:
        *bits = 16U;
        *is_signed = true;
        return true;
    case R_SEMANTIC_TYPE_I32:
        *bits = 32U;
        *is_signed = true;
        return true;
    case R_SEMANTIC_TYPE_I64:
    case R_SEMANTIC_TYPE_ISIZE:
        *bits = 64U;
        *is_signed = true;
        return true;
    case R_SEMANTIC_TYPE_U8:
    case R_SEMANTIC_TYPE_NULL_T:
        *bits = 8U;
        *is_signed = false;
        return true;
    case R_SEMANTIC_TYPE_U16:
        *bits = 16U;
        *is_signed = false;
        return true;
    case R_SEMANTIC_TYPE_U32:
    case R_SEMANTIC_TYPE_CHAR:
        *bits = 32U;
        *is_signed = false;
        return true;
    case R_SEMANTIC_TYPE_U64:
    case R_SEMANTIC_TYPE_USIZE:
        *bits = 64U;
        *is_signed = false;
        return true;
    default:
        return false;
    }
}

/* An enumeration without payloads is its integer: the underlying type of an enum of the program
   (the C17 emitter's typedef of it), the C enumeration of a named standard enum. */
bool r_llvm_enum_integer(RLlvmEmitter *emitter, RTypeId id, unsigned *bits, bool *is_signed) {
    const RTypeId value_id = r_llvm_value_type(emitter, id);
    const RSemanticType *type = r_llvm_type(emitter, value_id);

    if (type == NULL) {
        return false;
    }
    if ((type->kind == R_SEMANTIC_TYPE_ENUM) && (type->base != R_TYPE_ID_INVALID) &&
        ((size_t)type->base <= emitter->frontend->semantic_aggregate_count)) {
        const RSemanticAggregate *aggregate =
            &emitter->frontend->semantic_aggregates[(size_t)type->base - 1U];
        return (aggregate->kind == R_SEMANTIC_AGGREGATE_ENUM) && !aggregate->is_tagged &&
               r_llvm_kind_integer(
                   r_llvm_value_kind(emitter, aggregate->enum_underlying_type), bits, is_signed);
    }
    if ((type->kind == R_SEMANTIC_TYPE_STANDARD) && (type->base == R_TYPE_ID_INVALID)) {
        const RInternEntry *entry = r_llvm_standard_name(emitter, type);
        const RNamedStandardCopyAbi *copy =
            entry == NULL ? NULL : r_named_standard_copy_abi_find(entry->bytes, entry->length);
        const RLlvmSurfaceName *name = copy == NULL ? NULL : r_llvm_surface_typedef(copy->c_type);
        const RLlvmSurfaceType *surface = name == NULL ? NULL : r_llvm_surface_type(name->type);
        const RLlvmSurfaceType *integer =
            (surface == NULL) || (surface->kind != R_LLVM_SURFACE_ENUM)
                ? NULL
                : r_llvm_surface_type(surface->target);
        if ((integer == NULL) || (integer->kind != R_LLVM_SURFACE_INTEGER)) {
            return false;
        }
        *bits = integer->size * 8U;
        *is_signed = (integer->flags & R_LLVM_SURFACE_SIGNED) != 0U;
        return true;
    }
    return false;
}

/* A function value is the number of its symbol (R-TYPE-0054, L28). */
static bool r_llvm_type_is_function_value(const RSemanticType *type) {
    return (type != NULL) && (type->kind == R_SEMANTIC_TYPE_FUNCTION);
}

LLVMTypeRef r_llvm_scalar_type(RLlvmEmitter *emitter, RTypeId id) {
    const RTypeId value_id = r_llvm_value_type(emitter, id);
    const RSemanticType *type = r_llvm_type(emitter, value_id);
    const RSemanticTypeKind kind = r_llvm_value_kind(emitter, value_id);
    unsigned bits = 0U;
    bool is_signed = false;

    if (r_llvm_kind_integer(kind, &bits, &is_signed) ||
        r_llvm_enum_integer(emitter, value_id, &bits, &is_signed)) {
        return r_llvm_int(emitter, bits);
    }
    if (r_llvm_type_is_function_value(type)) {
        return r_llvm_int(emitter, 32U);
    }
    switch (kind) {
    case R_SEMANTIC_TYPE_BOOL:
        return r_llvm_int(emitter, 1U);
    case R_SEMANTIC_TYPE_F32:
        return LLVMFloatTypeInContext(emitter->context);
    case R_SEMANTIC_TYPE_F64:
        return LLVMDoubleTypeInContext(emitter->context);
    case R_SEMANTIC_TYPE_TASK:
        return r_llvm_pointer(emitter);
    case R_SEMANTIC_TYPE_BORROW:
        /* R-TYPE-0051: a borrow of an interface is the address and the tag of its member. */
        return r_semantic_dyn_referent(emitter->frontend, value_id) != R_TYPE_ID_INVALID
                   ? NULL
                   : r_llvm_pointer(emitter);
    case R_SEMANTIC_TYPE_RAW:
    case R_SEMANTIC_TYPE_RAW_FUNCTION:
        return r_llvm_pointer(emitter);
    default:
        return NULL;
    }
}

bool r_llvm_type_is_void(RLlvmEmitter *emitter, RTypeId id) {
    const RSemanticTypeKind kind = r_llvm_value_kind(emitter, id);
    return (kind == R_SEMANTIC_TYPE_VOID) || (kind == R_SEMANTIC_TYPE_NEVER);
}

/* The R name of a named standard type, NULL for any other type. */
const RInternEntry *r_llvm_standard_name(const RLlvmEmitter *emitter, const RSemanticType *type) {
    if ((type == NULL) || (type->kind != R_SEMANTIC_TYPE_STANDARD) || (type->length == 0U) ||
        (type->length > (uint64_t)emitter->frontend->intern_count)) {
        return NULL;
    }
    return &emitter->frontend->intern_entries[(size_t)type->length - 1U];
}

bool r_llvm_standard_named(const RLlvmEmitter *emitter, RTypeId id, const char *name) {
    const RInternEntry *entry =
        r_llvm_standard_name(emitter, r_llvm_type(emitter, r_llvm_value_type(emitter, id)));
    return (entry != NULL) && (strlen(name) == entry->length) &&
           (memcmp(name, entry->bytes, entry->length) == 0);
}

uint32_t r_llvm_standard_arity(const RSemanticType *type) {
    return type->base == R_TYPE_ID_INVALID ? 0U : type->second == R_TYPE_ID_INVALID ? 1U : 2U;
}

bool r_llvm_compare_exchange_result(const RLlvmEmitter *emitter, RTypeId id) {
    const RSemanticType *type = r_llvm_type(emitter, r_llvm_value_type(emitter, id));
    return (type != NULL) && (type->kind == R_SEMANTIC_TYPE_STANDARD) &&
           (type->flags == R_SEMANTIC_TYPE_FLAG_NONE) && (type->base != R_TYPE_ID_INVALID) &&
           (type->second == R_TYPE_ID_INVALID) &&
           r_llvm_standard_named(emitter, id, "core::atomic_compare_exchange_result");
}

bool r_llvm_standard_is_move(const RLlvmEmitter *emitter, const RSemanticType *type) {
    const RInternEntry *entry = r_llvm_standard_name(emitter, type);
    const RNamedStandardMoveAbi *abi =
        entry == NULL ? NULL : r_named_standard_move_abi_find(entry->bytes, entry->length);
    return (abi != NULL) && (abi->generic_arity == r_llvm_standard_arity(type));
}

const char *r_llvm_standard_c_type(RLlvmEmitter *emitter,
                                   const RSemanticType *type,
                                   uint32_t *size,
                                   uint32_t *align) {
    const RInternEntry *entry = r_llvm_standard_name(emitter, type);
    const RNamedStandardCopyAbi *copy;
    const RNamedStandardMoveAbi *move;
    const RNamedStandardLayoutAbi *layout;
    const char *c_type = NULL;
    const uint32_t arity = r_llvm_standard_arity(type);

    if (entry == NULL) {
        return NULL;
    }
    copy = r_named_standard_copy_abi_find(entry->bytes, entry->length);
    move = r_named_standard_move_abi_find(entry->bytes, entry->length);
    layout = r_named_standard_layout_abi_find(entry->bytes, entry->length);
    /* A record serves the type when its generic arity is the number of type arguments, as in the
       C17 emitter; a generic record has one C type for every instantiation. */
    if ((copy != NULL) && (copy->generic_arity == arity)) {
        c_type = copy->c_type;
    } else if ((move != NULL) && (move->generic_arity == arity)) {
        c_type = move->c_type;
    } else if ((layout != NULL) && (layout->generic_arity == arity)) {
        c_type = layout->c_type;
    }
    if ((c_type == NULL) || (r_llvm_surface_typedef(c_type) == NULL) ||
        !r_llvm_runtime_layout(emitter, c_type, size, align)) {
        return NULL;
    }
    return c_type;
}

static uint32_t r_llvm_align_up(uint32_t value, uint32_t align) {
    return align <= 1U ? value : ((value + align - 1U) / align) * align;
}

const RSemanticAggregate *r_llvm_aggregate(const RLlvmEmitter *emitter, RTypeId id) {
    const RSemanticType *type = r_llvm_type(emitter, r_llvm_value_type(emitter, id));

    if ((type == NULL) ||
        ((type->kind != R_SEMANTIC_TYPE_STRUCT) && (type->kind != R_SEMANTIC_TYPE_ENUM)) ||
        (type->base == R_TYPE_ID_INVALID) ||
        ((size_t)type->base > emitter->frontend->semantic_aggregate_count)) {
        return NULL;
    }
    return &emitter->frontend->semantic_aggregates[(size_t)type->base - 1U];
}

const RSemanticField *r_llvm_field(const RLlvmEmitter *emitter, uint32_t field_id) {
    return ((field_id == 0U) || ((size_t)field_id > emitter->frontend->semantic_field_count))
               ? NULL
               : &emitter->frontend->semantic_fields[(size_t)field_id - 1U];
}

/* A result whose error is std.fs::fs_error or std.io::io_error, or a carrier of a void time
   operation, has a native C representation of the library (the C17 emitter's native result ABI);
   the LLVM emitter does not lay those out yet. */
/* The library typedef of a result a library task completes with (r_c17_native_result_abi): the
   checked carriers of std.fs, std.io and std.time. NULL for any other type. */
static const char *r_llvm_native_result(RLlvmEmitter *emitter, const RSemanticType *type) {
    RTypeId value;

    if (type->kind == R_SEMANTIC_TYPE_EFFECT_CARRIER) {
        return (r_semantic_effect_count(emitter->frontend, type->second) == 1U) &&
                       r_llvm_type_is_void(emitter, type->base) &&
                       r_llvm_standard_named(
                           emitter,
                           r_semantic_effect_at(emitter->frontend, type->second, 0U),
                           "std.time::time_error")
                   ? "RStdTimeTaskResult"
                   : NULL;
    }
    if (type->kind != R_SEMANTIC_TYPE_RESULT) {
        return NULL;
    }
    value = type->base;
    if (r_llvm_standard_named(emitter, type->second, "std.io::io_error")) {
        return r_llvm_type_is_void(emitter, value) ? "RStdIoVoidResult" : NULL;
    }
    if (!r_llvm_standard_named(emitter, type->second, "std.fs::fs_error")) {
        return NULL;
    }
    if (r_llvm_type_is_void(emitter, value)) {
        return "RStdFsVoidResult";
    }
    if (r_llvm_value_kind(emitter, value) == R_SEMANTIC_TYPE_ARRAY) {
        return "RStdFsArrayResult";
    }
    if (r_llvm_value_kind(emitter, value) == R_SEMANTIC_TYPE_U64) {
        return "RStdFsU64Result";
    }
    if (r_llvm_value_kind(emitter, value) == R_SEMANTIC_TYPE_BOOL) {
        return "RStdFsBoolResult";
    }
    return r_llvm_standard_named(emitter, value, "std.fs::directory")  ? "RStdFsDirectoryResult"
           : r_llvm_standard_named(emitter, value, "std.fs::file")     ? "RStdFsFileResult"
           : r_llvm_standard_named(emitter, value, "std.fs::metadata") ? "RStdFsMetadataResult"
           : r_llvm_standard_named(emitter, value, "std.fs::directory_iter")
               ? "RStdFsDirectoryIterResult"
               : NULL;
}

/* The errors of standard operations that hand an element back when they could not store it
   (the C17 emitter's recovering errors): std.alloc::new_error<T> is the structure
   { RStdAllocError r_reason; T r_value; }; std.array::push_error<T>, std.list::push_error<T> and
   std.dict::insert_error<K, V> tag one variant, allocation_failed, whose payload is that structure
   (with K r_key before V r_value for the dict). */
RLlvmRecoveringKind r_llvm_recovering_error(const RLlvmEmitter *emitter, RTypeId id) {
    const RSemanticType *type = r_llvm_type(emitter, r_llvm_value_type(emitter, id));

    if ((type == NULL) || (type->kind != R_SEMANTIC_TYPE_STANDARD) ||
        (type->base == R_TYPE_ID_INVALID) || (type->flags != R_SEMANTIC_TYPE_FLAG_NONE)) {
        return R_LLVM_RECOVERING_NONE;
    }
    if (r_llvm_standard_named(emitter, id, "std.alloc::new_error") &&
        (type->second == R_TYPE_ID_INVALID)) {
        return R_LLVM_RECOVERING_NEW;
    }
    if ((r_llvm_standard_named(emitter, id, "std.array::push_error") ||
         r_llvm_standard_named(emitter, id, "std.list::push_error")) &&
        (type->second == R_TYPE_ID_INVALID)) {
        return R_LLVM_RECOVERING_PUSH;
    }
    if (r_llvm_standard_named(emitter, id, "std.dict::insert_error") &&
        (type->second != R_TYPE_ID_INVALID)) {
        return R_LLVM_RECOVERING_INSERT;
    }
    return R_LLVM_RECOVERING_NONE;
}

static uint32_t r_llvm_align_up(uint32_t value, uint32_t align);

/* The structure of a recovering error's payload: the reason, then the key and the value. */
bool r_llvm_recovering_members(RLlvmEmitter *emitter, RTypeId id, RLlvmRecoveringMembers *members) {
    const RSemanticType *type = r_llvm_type(emitter, r_llvm_value_type(emitter, id));
    const RLlvmRecoveringKind kind = r_llvm_recovering_error(emitter, id);
    uint32_t size = 0U;
    uint32_t align = 0U;
    uint32_t offset;

    (void)memset(members, 0, sizeof(*members));
    if ((kind == R_LLVM_RECOVERING_NONE) ||
        !r_llvm_runtime_layout(emitter, "RStdAllocError", &size, &align)) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    members->align = align;
    offset = size;
    members->key_type = kind == R_LLVM_RECOVERING_INSERT ? type->base : R_TYPE_ID_INVALID;
    members->value_type = kind == R_LLVM_RECOVERING_INSERT ? type->second : type->base;
    if (members->key_type != R_TYPE_ID_INVALID) {
        if (!r_llvm_layout(emitter, members->key_type, &size, &align)) {
            return false;
        }
        members->key_offset = r_llvm_align_up(offset, align);
        offset = members->key_offset + size;
        members->align = align > members->align ? align : members->align;
    }
    if (!r_llvm_layout(emitter, members->value_type, &size, &align)) {
        return false;
    }
    members->value_offset = r_llvm_align_up(offset, align);
    offset = members->value_offset + size;
    members->align = align > members->align ? align : members->align;
    members->size = r_llvm_align_up(offset, members->align);
    return true;
}

typedef struct RLlvmUnion {
    uint32_t size;
    uint32_t align;
} RLlvmUnion;

static bool r_llvm_union_member(RLlvmEmitter *emitter, RLlvmUnion *layout, RTypeId member) {
    uint32_t size = 0U;
    uint32_t align = 0U;

    if (r_llvm_type_is_void(emitter, member)) {
        return true;
    }
    if (!r_llvm_layout(emitter, member, &size, &align)) {
        return false;
    }
    layout->size = size > layout->size ? size : layout->size;
    layout->align = align > layout->align ? align : layout->align;
    return true;
}

/* struct { uint32_t r_tag; union { ... } r_payload; } */
static void r_llvm_tagged_layout(const RLlvmUnion *payload, uint32_t *size, uint32_t *align) {
    const uint32_t union_align = payload->align == 0U ? 1U : payload->align;
    const uint32_t union_size = r_llvm_align_up(payload->size, union_align);

    *align = union_align > 4U ? union_align : 4U;
    *size = r_llvm_align_up(r_llvm_align_up(4U, union_align) + union_size, *align);
}

static bool r_llvm_runtime_type_layout(RLlvmEmitter *emitter,
                                       const char *name,
                                       uint32_t *size,
                                       uint32_t *align) {
    return r_llvm_runtime_layout(emitter, name, size, align);
}

/* The payload union of a tagged derived type or a tagged enum. */
static bool r_llvm_payload_union(RLlvmEmitter *emitter, RTypeId id, RLlvmUnion *payload) {
    const RTypeId value_id = r_llvm_value_type(emitter, id);
    const RSemanticType *type = r_llvm_type(emitter, value_id);
    const RSemanticAggregate *aggregate = r_llvm_aggregate(emitter, value_id);

    (void)memset(payload, 0, sizeof(*payload));
    if (type == NULL) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    if ((aggregate != NULL) && aggregate->is_tagged) {
        uint32_t index;
        for (index = 0U; index < aggregate->variant_count; ++index) {
            const RSemanticVariant *variant =
                r_semantic_tagged_variant(emitter->frontend, aggregate->type, index);
            if ((variant != NULL) && (variant->payload_type != R_TYPE_ID_INVALID) &&
                !r_llvm_union_member(emitter, payload, variant->payload_type)) {
                return false;
            }
        }
        return true;
    }
    switch (type->kind) {
    case R_SEMANTIC_TYPE_EFFECT_CARRIER: {
        const uint32_t count = r_semantic_effect_count(emitter->frontend, type->second);
        uint32_t index;
        if (!r_llvm_union_member(emitter, payload, type->base)) {
            return false;
        }
        for (index = 0U; index < count; ++index) {
            if (!r_llvm_union_member(
                    emitter,
                    payload,
                    r_semantic_effect_at(emitter->frontend, type->second, index))) {
                return false;
            }
        }
        return true;
    }
    case R_SEMANTIC_TYPE_OPTION:
        return r_llvm_union_member(emitter, payload, type->base);
    case R_SEMANTIC_TYPE_STANDARD:
        /* core::atomic_compare_exchange_result<T>: { r_tag; union { T r_observed; } }. */
        if (r_llvm_compare_exchange_result(emitter, value_id)) {
            return r_llvm_union_member(emitter, payload, type->base);
        }
        if (r_llvm_standard_tagged(emitter, value_id)) {
            return r_llvm_standard_tagged_union(emitter, value_id, &payload->size, &payload->align);
        }
        /* An outcome of std.sync: { r_tag; union { P r_value; } }. */
        if (r_llvm_sync_tagged(emitter, value_id)) {
            uint32_t tag;
            for (tag = 0U; tag < 4U; ++tag) {
                const RTypeId member = r_llvm_sync_variant_payload(emitter, value_id, tag);
                if (member != R_TYPE_ID_INVALID) {
                    return r_llvm_union_member(emitter, payload, member);
                }
            }
            return true;
        }
        /* std.thread::join_result<T>: { r_tag; union { T r_returned; RStdThreadPanicReport
           r_panicked; } }. */
        if (r_llvm_thread_join_result(emitter, value_id)) {
            uint32_t report_size = 0U;
            uint32_t report_align = 0U;
            if ((!r_llvm_type_is_void(emitter, type->base) &&
                 !r_llvm_union_member(emitter, payload, type->base)) ||
                !r_llvm_runtime_type_layout(
                    emitter, "RStdThreadPanicReport", &report_size, &report_align)) {
                return false;
            }
            payload->size = report_size > payload->size ? report_size : payload->size;
            payload->align = report_align > payload->align ? report_align : payload->align;
            return true;
        }
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    case R_SEMANTIC_TYPE_RESULT:
        return r_llvm_union_member(emitter, payload, type->base) &&
               r_llvm_union_member(emitter, payload, type->second);
    default:
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
}

static bool
r_llvm_compute_layout(RLlvmEmitter *emitter, RTypeId id, uint32_t *size, uint32_t *align) {
    const RTypeId value_id = r_llvm_value_type(emitter, id);
    const RSemanticType *type = r_llvm_type(emitter, value_id);
    const LLVMTypeRef scalar = r_llvm_scalar_type(emitter, value_id);
    RLlvmUnion payload;

    if (type == NULL) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    if (scalar != NULL) {
        if (r_llvm_value_kind(emitter, value_id) == R_SEMANTIC_TYPE_BOOL) {
            *size = 1U;
            *align = 1U;
        } else {
            *size = (uint32_t)LLVMABISizeOfType(emitter->data, scalar);
            *align = (uint32_t)LLVMABIAlignmentOfType(emitter->data, scalar);
        }
        return true;
    }
    if (r_semantic_dyn_owned(emitter->frontend, value_id) != R_TYPE_ID_INVALID) {
        /* struct { <owner> r_owner; uint32_t r_tag; } of an owner of an interface. */
        static const char *const owners[] = {
            "RRuntimeOwn", "RRuntimeArc", "RRuntimeRc", "RRuntimeWeakArc", "RRuntimeWeakRc"};
        const size_t which = type->kind == R_SEMANTIC_TYPE_OWN                     ? 0U
                             : type->kind == R_SEMANTIC_TYPE_ARC                   ? 1U
                             : type->kind == R_SEMANTIC_TYPE_RC                    ? 2U
                             : (type->flags & R_SEMANTIC_TYPE_FLAG_RC_OWNER) != 0U ? 4U
                                                                                   : 3U;
        uint32_t owner_size = 0U;
        uint32_t owner_align = 0U;
        if (!r_llvm_runtime_type_layout(emitter, owners[which], &owner_size, &owner_align)) {
            return false;
        }
        *align = owner_align > 4U ? owner_align : 4U;
        *size = r_llvm_align_up(r_llvm_align_up(owner_size, 4U) + 4U, *align);
        return true;
    }
    switch (type->kind) {
    case R_SEMANTIC_TYPE_STRUCT:
    case R_SEMANTIC_TYPE_ENUM: {
        const RSemanticAggregate *aggregate = r_llvm_aggregate(emitter, value_id);
        uint32_t index;
        uint32_t offset = 0U;
        uint32_t largest = 1U;
        const bool is_union = aggregate != NULL && aggregate->c_type_kind == R_C_TYPE_KIND_UNION;

        if ((aggregate == NULL) || aggregate->is_opaque) {
            return r_llvm_unsupported(emitter, "an opaque aggregate by value");
        }
        if ((aggregate->kind == R_SEMANTIC_AGGREGATE_ENUM) && !aggregate->is_tagged) {
            return r_llvm_layout(emitter, aggregate->enum_underlying_type, size, align);
        }
        if (aggregate->is_tagged) {
            if (!r_llvm_payload_union(emitter, value_id, &payload)) {
                return false;
            }
            r_llvm_tagged_layout(&payload, size, align);
            return true;
        }
        if (aggregate->field_count == 0U) {
            *size = 1U;
            *align = 1U;
            return true;
        }
        for (index = 0U; index < aggregate->field_count; ++index) {
            const RSemanticField *field =
                &emitter->frontend->semantic_fields[(size_t)aggregate->first_field + index];
            uint32_t field_size = 0U;
            uint32_t field_align = 0U;
            if (!r_llvm_layout(emitter, field->type, &field_size, &field_align)) {
                return false;
            }
            if (is_union) {
                offset = field_size > offset ? field_size : offset;
            } else {
                offset = r_llvm_align_up(offset, field_align) + field_size;
            }
            largest = field_align > largest ? field_align : largest;
        }
        *align = largest;
        *size = r_llvm_align_up(offset, largest);
        return true;
    }
    case R_SEMANTIC_TYPE_FIXED_ARRAY: {
        uint32_t element_size = 0U;
        uint32_t element_align = 0U;
        if ((type->length == 0U) || (type->length > UINT32_MAX) ||
            !r_llvm_layout(emitter, type->base, &element_size, &element_align)) {
            return (type->length == 0U) || (type->length > UINT32_MAX)
                       ? r_llvm_unsupported(emitter, "a fixed array of this length")
                       : false;
        }
        if ((uint64_t)element_size * type->length > UINT32_MAX) {
            return r_llvm_unsupported(emitter, "a fixed array of this size");
        }
        *size = (uint32_t)(element_size * type->length);
        *align = element_align;
        return true;
    }
    case R_SEMANTIC_TYPE_SLICE:
    case R_SEMANTIC_TYPE_CONSTEXPR_STR:
    case R_SEMANTIC_TYPE_BORROW: /* of an interface: struct { void *r_pointer; uint32_t r_tag; } */
        *size = 16U;
        *align = 8U;
        return true;
    case R_SEMANTIC_TYPE_OPTION:
        if (r_llvm_standard_named(emitter, type->base, "std.time::system_time")) {
            return r_llvm_runtime_type_layout(emitter, "RStdTimeSystemTimeOption", size, align);
        }
        if (r_llvm_standard_named(emitter, type->base, "std.time::duration")) {
            return r_llvm_runtime_type_layout(emitter, "RStdTimeDurationOption", size, align);
        }
        /* fallthrough */
    case R_SEMANTIC_TYPE_RESULT:
    case R_SEMANTIC_TYPE_EFFECT_CARRIER:
        if (!r_llvm_payload_union(emitter, value_id, &payload)) {
            return false;
        }
        r_llvm_tagged_layout(&payload, size, align);
        {
            /* A library task writes these results as its typedef: {uint32_t r_tag; union {ok;
               error} r_payload}, tag 0 for success, which is the layout of any carrier. */
            const char *native = r_llvm_native_result(emitter, type);
            uint32_t native_size = 0U;
            uint32_t native_align = 0U;
            if ((native != NULL) &&
                (!r_llvm_runtime_type_layout(emitter, native, &native_size, &native_align) ||
                 (native_size != *size) || (native_align != *align))) {
                return r_llvm_unsupported(emitter, "a native result of the library");
            }
        }
        return true;
    case R_SEMANTIC_TYPE_NEVER:
        /* R-TYPE-0007: a never object is a placeholder byte (r_c17_emit_declared_type). */
        *size = 1U;
        *align = 1U;
        return true;
    case R_SEMANTIC_TYPE_STR:
        return r_llvm_runtime_type_layout(emitter, "RRuntimeStringView", size, align);
    case R_SEMANTIC_TYPE_OWN:
        return r_llvm_runtime_type_layout(emitter, "RRuntimeOwn", size, align);
    case R_SEMANTIC_TYPE_ARRAY:
        return r_llvm_runtime_type_layout(emitter, "RRuntimeArray", size, align);
    case R_SEMANTIC_TYPE_LIST:
        return r_llvm_runtime_type_layout(emitter, "RRuntimeList", size, align);
    case R_SEMANTIC_TYPE_DICT:
        return r_llvm_runtime_type_layout(emitter, "RRuntimeDict", size, align);
    case R_SEMANTIC_TYPE_ARC:
        return r_llvm_runtime_type_layout(emitter, "RRuntimeArc", size, align);
    case R_SEMANTIC_TYPE_RC:
        return r_llvm_runtime_type_layout(emitter, "RRuntimeRc", size, align);
    case R_SEMANTIC_TYPE_WEAK:
        return r_llvm_runtime_type_layout(emitter,
                                          (type->flags & R_SEMANTIC_TYPE_FLAG_RC_OWNER) != 0U
                                              ? "RRuntimeWeakRc"
                                              : "RRuntimeWeakArc",
                                          size,
                                          align);
    case R_SEMANTIC_TYPE_ATOMIC: {
        uint32_t base_size = 0U;
        uint32_t base_align = 0U;
        if (!r_llvm_layout(emitter, type->base, &base_size, &base_align)) {
            return false;
        }
        /* _Atomic of a power-of-two size up to 16 bytes is aligned to its size. */
        *size = base_size;
        *align = ((base_size == 1U) || (base_size == 2U) || (base_size == 4U) ||
                  (base_size == 8U) || (base_size == 16U)) &&
                         (base_size > base_align)
                     ? base_size
                     : base_align;
        return true;
    }
    case R_SEMANTIC_TYPE_STANDARD: {
        const RInternEntry *name = r_llvm_standard_name(emitter, type);
        if (r_llvm_recovering_error(emitter, value_id) != R_LLVM_RECOVERING_NONE) {
            RLlvmRecoveringMembers members;
            if (!r_llvm_recovering_members(emitter, value_id, &members)) {
                return false;
            }
            if (r_llvm_recovering_error(emitter, value_id) == R_LLVM_RECOVERING_NEW) {
                *size = members.size;
                *align = members.align;
                return true;
            }
            payload.size = members.size;
            payload.align = members.align;
            r_llvm_tagged_layout(&payload, size, align);
            return true;
        }
        if (r_llvm_thread_handle(emitter, value_id)) {
            return r_llvm_runtime_type_layout(emitter, "RStdThreadJoinHandle", size, align);
        }
        {
            bool handled = false;
            const bool laid = r_llvm_sync_layout(emitter, value_id, size, align, &handled);
            if (handled || !laid) {
                return laid;
            }
        }
        if (r_llvm_thread_join_result(emitter, value_id) ||
            r_llvm_standard_tagged(emitter, value_id)) {
            if (!r_llvm_payload_union(emitter, value_id, &payload)) {
                return false;
            }
            r_llvm_tagged_layout(&payload, size, align);
            return true;
        }
        if (r_llvm_compare_exchange_result(emitter, value_id)) {
            RLlvmUnion observed;
            if (!r_llvm_payload_union(emitter, value_id, &observed)) {
                return false;
            }
            r_llvm_tagged_layout(&observed, size, align);
            return true;
        }
        if ((type->base != R_TYPE_ID_INVALID) &&
            (r_llvm_standard_c_type(emitter, type, size, align) == NULL)) {
            return r_llvm_unsupported_detail(emitter,
                                             "a standard type with type arguments",
                                             name == NULL ? "?" : name->bytes,
                                             name == NULL ? 1U : name->length);
        }
        if (r_llvm_standard_c_type(emitter, type, size, align) != NULL) {
            return true;
        }
        return r_llvm_unsupported_detail(emitter,
                                         "a standard type without a C layout",
                                         name == NULL ? "?" : name->bytes,
                                         name == NULL ? 1U : name->length);
    }
    default:
        return r_llvm_unsupported(emitter, "a value of this type in memory");
    }
}

bool r_llvm_layout(RLlvmEmitter *emitter, RTypeId id, uint32_t *size, uint32_t *align) {
    const RTypeId value_id = r_llvm_value_type(emitter, id);
    RLlvmTypeLayout *cached;

    if ((value_id == R_TYPE_ID_INVALID) ||
        ((size_t)value_id > emitter->frontend->semantic_type_count) || (emitter->layouts == NULL)) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    cached = &emitter->layouts[value_id];
    if (cached->state == 2U) {
        *size = cached->size;
        *align = cached->align;
        return true;
    }
    if (cached->state == 1U) {
        /* A type that contains itself by value has no layout. */
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    cached->state = 1U;
    if (!r_llvm_compute_layout(emitter, value_id, &cached->size, &cached->align)) {
        cached->state = 0U;
        return false;
    }
    cached->state = 2U;
    *size = cached->size;
    *align = cached->align;
    return true;
}

bool r_llvm_payload_offset(RLlvmEmitter *emitter, RTypeId id, uint32_t *offset) {
    const RTypeId value_id = r_llvm_value_type(emitter, id);
    RLlvmUnion payload;

    if (r_llvm_recovering_error(emitter, value_id) == R_LLVM_RECOVERING_PUSH ||
        r_llvm_recovering_error(emitter, value_id) == R_LLVM_RECOVERING_INSERT) {
        RLlvmRecoveringMembers members;
        if (!r_llvm_recovering_members(emitter, value_id, &members)) {
            return false;
        }
        *offset = r_llvm_align_up(4U, members.align);
        return true;
    }

    if (!r_llvm_payload_union(emitter, value_id, &payload)) {
        return false;
    }
    *offset = r_llvm_align_up(4U, payload.align == 0U ? 1U : payload.align);
    return true;
}

bool r_llvm_carrier_payload_offset(RLlvmEmitter *emitter, RTypeId id, uint32_t *offset) {
    return r_llvm_payload_offset(emitter, id, offset);
}

/* The C member of a field of a named standard type, as the C17 emitter's
   r_c17_emit_field_access_name spells it: some library structures name their members apart from
   the R fields, the others use the R names. NULL for a field it cannot name. */
static const char *r_llvm_standard_member(RLlvmEmitter *emitter,
                                          const RSemanticAggregate *aggregate,
                                          const RSemanticField *field,
                                          char *buffer,
                                          size_t capacity) {
    static const struct {
        const char *type;
        const char *members[6];
        uint32_t count;
    } tables[] = {
        {"std.json::feed_result", {"consumed", "state"}, 2U},
        {"std.json::error", {"code", "offset", "pointer"}, 3U},
        {"std.json::options",
         {"max_depth", "max_value_bytes", "indent", "reject_unknown_fields", "ignore_case", "mode"},
         6U},
        {"std.bits::lsb_reader", {"byte_index", "hold", "bit_count"}, 3U},
        {"std.bits::read_error", {"code", "byte_index"}, 2U},
        {"std.convert::parse_error", {"code", "index"}, 2U},
        {"std.net::address_error", {"code", "index"}, 2U},
        {"std.c::target_info",
         {"pointer_bits", "c_wint_available", "c_long_double_available", "hosted_native_async"},
         4U},
        {"std.error::error", {"domain", "code", "native_code"}, 3U},
        {"std.io::io_error", {"code", "native_code"}, 2U},
        {"std.fs::fs_error", {"code", "native_code"}, 2U},
        {"std.dict::entry_ref", {"key", "value"}, 2U},
    };
    const RInternEntry *name;
    size_t index;

    for (index = 0U; index < sizeof(tables) / sizeof(tables[0]); ++index) {
        if (r_llvm_standard_named(emitter, aggregate->type, tables[index].type)) {
            return field->layout_index < tables[index].count
                       ? tables[index].members[field->layout_index]
                       : NULL;
        }
    }
    if (r_llvm_standard_named(emitter, aggregate->type, "std.hash::md5_digest") ||
        r_llvm_standard_named(emitter, aggregate->type, "std.hash::sha1_digest") ||
        r_llvm_standard_named(emitter, aggregate->type, "std.hash::sha256_digest") ||
        r_llvm_standard_named(emitter, aggregate->type, "std.hash::sha512_digest")) {
        /* A digest is its byte array (r_c17_field_is_hash_digest_array): u8[N] lays out as the
           uint8_t bytes[N] of the library. */
        return field->layout_index == 0U ? "bytes" : NULL;
    }
    name = (field->name_intern_id == 0U) ||
                   ((size_t)field->name_intern_id > emitter->frontend->intern_count)
               ? NULL
               : &emitter->frontend->intern_entries[(size_t)field->name_intern_id - 1U];
    if ((name == NULL) || (name->length >= capacity)) {
        return NULL;
    }
    (void)memcpy(buffer, name->bytes, name->length);
    buffer[name->length] = '\0';
    return buffer;
}

bool r_llvm_field_offset(RLlvmEmitter *emitter, uint32_t field_id, uint32_t *offset) {
    const RSemanticField *field = r_llvm_field(emitter, field_id);
    const RSemanticAggregate *aggregate =
        (field == NULL) || (field->aggregate == 0U) ||
                ((size_t)field->aggregate > emitter->frontend->semantic_aggregate_count)
            ? NULL
            : &emitter->frontend->semantic_aggregates[(size_t)field->aggregate - 1U];
    uint32_t index;
    uint32_t position = 0U;

    if ((aggregate == NULL) || aggregate->is_tagged || (field_id <= aggregate->first_field) ||
        (field_id > aggregate->first_field + aggregate->field_count)) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    if ((aggregate->module_source == R_SOURCE_ID_INVALID) &&
        (r_llvm_recovering_error(emitter, aggregate->type) == R_LLVM_RECOVERING_NEW)) {
        /* std.alloc::new_error<T> { RStdAllocError r_reason; T r_value; } (C17 r_reason, r_value).
         */
        RLlvmRecoveringMembers members;
        if ((field->layout_index > 1U) ||
            !r_llvm_recovering_members(emitter, aggregate->type, &members)) {
            return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
        *offset = field->layout_index == 0U ? 0U : members.value_offset;
        return true;
    }
    if ((aggregate->module_source == R_SOURCE_ID_INVALID) &&
        (r_llvm_value_kind(emitter, aggregate->type) == R_SEMANTIC_TYPE_STANDARD)) {
        const RSemanticType *type =
            r_llvm_type(emitter, r_llvm_value_type(emitter, aggregate->type));
        uint32_t size = 0U;
        uint32_t align = 0U;
        char buffer[128];
        const char *c_type = r_llvm_standard_c_type(emitter, type, &size, &align);
        const char *member =
            r_llvm_standard_member(emitter, aggregate, field, buffer, sizeof(buffer));
        if ((c_type == NULL) || (member == NULL)) {
            const RInternEntry *name = r_llvm_standard_name(emitter, type);
            return r_llvm_unsupported_detail(emitter,
                                             "a field of this standard type",
                                             name == NULL ? "?" : name->bytes,
                                             name == NULL ? 1U : name->length);
        }
        return r_llvm_runtime_field(emitter, c_type, member, offset, NULL);
    }
    if (aggregate->c_type_kind == R_C_TYPE_KIND_UNION) {
        *offset = 0U;
        return true;
    }
    for (index = 0U; index < aggregate->field_count; ++index) {
        const RSemanticField *member =
            &emitter->frontend->semantic_fields[(size_t)aggregate->first_field + index];
        uint32_t size = 0U;
        uint32_t align = 0U;
        if (!r_llvm_layout(emitter, member->type, &size, &align)) {
            return false;
        }
        position = r_llvm_align_up(position, align);
        if (aggregate->first_field + index + 1U == field_id) {
            *offset = position;
            return true;
        }
        position += size;
    }
    return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
}

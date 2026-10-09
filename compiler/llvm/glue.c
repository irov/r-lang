#include "emit_internal.h"

#include "named_standard_copy_abi.h"
#include "named_standard_move_abi.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* Type glue of the LLVM emitter: the drop of a value that owns resources and its move to other
   memory, one internal function of each per type, as the C17 emitter's r_type_drop_* and
   r_type_move_* glue. A use declares the function at once; its body is written after the
   functions of the program (r_llvm_emit_pending_glue), and writing it may declare more. */

enum {
    R_LLVM_GLUE_DROP = 0,
    R_LLVM_GLUE_MOVE = 1,
    R_LLVM_GLUE_CURSOR = 2, /* the cursor and the member count of an iterative drop */
    R_LLVM_GLUE_HOOK = 3,   /* the user drop of a self-nesting aggregate */
    R_LLVM_GLUE_KEY_HASH = 4,
    R_LLVM_GLUE_KEY_EQUAL = 5
};

static bool r_llvm_queue_glue(RLlvmEmitter *emitter, RTypeId type, unsigned kind) {
    if (emitter->glue_count == emitter->glue_capacity) {
        const size_t capacity = emitter->glue_capacity == 0U ? 64U : emitter->glue_capacity * 2U;
        RLlvmGlue *grown = r_llvm_allocate(emitter, capacity * sizeof(*grown));
        if (grown == NULL) {
            return false;
        }
        if (emitter->glue_count != 0U) {
            (void)memcpy(grown, emitter->glue, emitter->glue_count * sizeof(*grown));
        }
        r_llvm_free(emitter, emitter->glue);
        emitter->glue = grown;
        emitter->glue_capacity = capacity;
    }
    emitter->glue[emitter->glue_count].type = type;
    emitter->glue[emitter->glue_count].kind = kind;
    emitter->glue_count += 1U;
    return true;
}

static LLVMValueRef r_llvm_glue_function(RLlvmEmitter *emitter, RTypeId type, unsigned kind) {
    const RTypeId value_type = r_llvm_value_type(emitter, type);
    LLVMValueRef *slot;
    LLVMTypeRef parameters[2];
    char name[64];

    if ((value_type == R_TYPE_ID_INVALID) ||
        ((size_t)value_type > emitter->frontend->semantic_type_count)) {
        (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        return NULL;
    }
    slot = kind == R_LLVM_GLUE_DROP ? &emitter->drop_glue[value_type]
                                    : &emitter->move_glue[value_type];
    if (*slot != NULL) {
        return *slot;
    }
    parameters[0] = r_llvm_pointer(emitter);
    parameters[1] = r_llvm_pointer(emitter);
    (void)snprintf(name,
                   sizeof(name),
                   kind == R_LLVM_GLUE_DROP ? "r_drop.%" PRIu32 : "r_move.%" PRIu32,
                   r_llvm_type_key(emitter, value_type));
    *slot = LLVMAddFunction(emitter->module,
                            name,
                            LLVMFunctionType(LLVMVoidTypeInContext(emitter->context),
                                             parameters,
                                             kind == R_LLVM_GLUE_DROP ? 1U : 2U,
                                             0));
    LLVMSetLinkage(*slot, LLVMInternalLinkage);
    if (!r_llvm_queue_glue(emitter, value_type, kind)) {
        return NULL;
    }
    return *slot;
}

bool r_llvm_call_drop(RLlvmEmitter *emitter, RTypeId type, LLVMValueRef pointer) {
    LLVMValueRef function = r_llvm_glue_function(emitter, type, R_LLVM_GLUE_DROP);

    if ((function == NULL) || (pointer == NULL)) {
        return false;
    }
    (void)LLVMBuildCall2(
        emitter->builder, LLVMGlobalGetValueType(function), function, &pointer, 1U, "");
    return true;
}

bool r_llvm_call_move(RLlvmEmitter *emitter,
                      RTypeId type,
                      LLVMValueRef destination,
                      LLVMValueRef source) {
    LLVMValueRef function = r_llvm_glue_function(emitter, type, R_LLVM_GLUE_MOVE);
    LLVMValueRef arguments[2];

    if ((function == NULL) || (destination == NULL) || (source == NULL)) {
        return false;
    }
    arguments[0] = destination;
    arguments[1] = source;
    (void)LLVMBuildCall2(
        emitter->builder, LLVMGlobalGetValueType(function), function, arguments, 2U, "");
    return true;
}

/* A member that owns resources moves by its glue, any other is copied. */
static bool r_llvm_move_member(RLlvmEmitter *emitter,
                               RTypeId type,
                               LLVMValueRef destination,
                               LLVMValueRef source) {
    if (r_llvm_type_is_void(emitter, type)) {
        return true;
    }
    if (r_llvm_type_requires_drop(emitter, type)) {
        return r_llvm_call_move(emitter, type, destination, source);
    }
    return r_llvm_store_value(emitter,
                              type,
                              r_llvm_scalar_type(emitter, type) == NULL
                                  ? source
                                  : r_llvm_load_scalar(emitter, type, source),
                              destination);
}

static bool r_llvm_drop_member(RLlvmEmitter *emitter, RTypeId type, LLVMValueRef pointer) {
    return r_llvm_type_is_void(emitter, type) || !r_llvm_type_requires_drop(emitter, type) ||
           r_llvm_call_drop(emitter, type, pointer);
}

/* The body runs `then` when the 32-bit tag at `tagged` equals `tag`; the builder continues after.
 */
static LLVMBasicBlockRef
r_llvm_if_tag(RLlvmEmitter *emitter, LLVMValueRef tagged, uint64_t tag, bool equal) {
    LLVMBasicBlockRef then = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef next = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMValueRef value = LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), tagged, "");

    (void)LLVMBuildCondBr(
        emitter->builder,
        LLVMBuildICmp(
            emitter->builder, equal ? LLVMIntEQ : LLVMIntNE, value, r_llvm_u32(emitter, tag), ""),
        then,
        next);
    LLVMPositionBuilderAtEnd(emitter->builder, then);
    return next;
}

static void r_llvm_end_if(RLlvmEmitter *emitter, LLVMBasicBlockRef next) {
    (void)LLVMBuildBr(emitter->builder, next);
    LLVMPositionBuilderAtEnd(emitter->builder, next);
}

static const RNamedStandardMoveAbi *r_llvm_standard_move_abi(RLlvmEmitter *emitter,
                                                             const RSemanticType *type) {
    const RInternEntry *entry;
    const RNamedStandardMoveAbi *abi;

    if ((type == NULL) || (type->kind != R_SEMANTIC_TYPE_STANDARD) || (type->length == 0U) ||
        (type->length > (uint64_t)emitter->frontend->intern_count)) {
        return NULL;
    }
    entry = &emitter->frontend->intern_entries[(size_t)type->length - 1U];
    abi = r_named_standard_move_abi_find(entry->bytes, entry->length);
    /* r_c17_standard_move_abi: the record's generic arity is the number of type arguments. */
    return (abi != NULL) && (abi->generic_arity == r_llvm_standard_arity(type)) ? abi : NULL;
}

/* A call of a runtime or library function on the glue's arguments. */
static bool r_llvm_glue_runtime(RLlvmEmitter *emitter,
                                const char *name,
                                LLVMValueRef first,
                                LLVMValueRef second) {
    LLVMValueRef arguments[2];

    arguments[0] = first;
    arguments[1] = second;
    return r_llvm_call_runtime(emitter, name, arguments, second == NULL ? 1U : 2U, NULL) != NULL;
}

/* The release of a runtime owner, which C17 moves bitwise; an owner of an interface releases its
   owner member, which is at offset zero. */
static const char *r_llvm_owner_release(RLlvmEmitter *emitter, RTypeId type_id) {
    const RSemanticType *type = r_llvm_type(emitter, r_llvm_value_type(emitter, type_id));

    if (r_llvm_thread_handle(emitter, type_id)) {
        /* A join handle releases its thread descriptor (r_type_drop_h). */
        return "r_library_internal_thread_handle_destroy";
    }
    switch (type == NULL ? R_SEMANTIC_TYPE_INVALID : type->kind) {
    case R_SEMANTIC_TYPE_OWN:
        return "r_runtime_own_release";
    case R_SEMANTIC_TYPE_TASK:
        return "r_runtime_task_destroy";
    case R_SEMANTIC_TYPE_ARRAY:
        return "r_runtime_array_destroy";
    case R_SEMANTIC_TYPE_LIST:
        return "r_runtime_list_destroy";
    case R_SEMANTIC_TYPE_DICT:
        return "r_runtime_dict_destroy";
    case R_SEMANTIC_TYPE_ARC:
        return "r_runtime_arc_release";
    case R_SEMANTIC_TYPE_RC:
        return "r_runtime_rc_release";
    case R_SEMANTIC_TYPE_WEAK:
        return (type->flags & R_SEMANTIC_TYPE_FLAG_RC_OWNER) != 0U ? "r_runtime_weak_rc_release"
                                                                   : "r_runtime_weak_arc_release";
    default:
        return NULL;
    }
}

/* The user drop of an aggregate: under a pending panic it runs between the cleanup markers, as
   the C17 emitter's r_c17_emit_user_drop_call does. */
static bool r_llvm_user_drop(RLlvmEmitter *emitter, RSymbolId drop_function, LLVMValueRef value) {
    LLVMValueRef function =
        drop_function == R_SYMBOL_ID_INVALID ? NULL : emitter->functions[drop_function];
    LLVMValueRef unwinding;
    LLVMBasicBlockRef cleanup;
    LLVMBasicBlockRef plain;
    LLVMBasicBlockRef next;

    if (function == NULL) {
        return r_llvm_unsupported(emitter, "a drop function that is not lowered");
    }
    if ((emitter->frontend->profile == R_FRONTEND_PROFILE_FREESTANDING)) {
        (void)LLVMBuildCall2(
            emitter->builder, emitter->function_types[drop_function], function, &value, 1U, "");
        return true;
    }
    unwinding = r_llvm_unwinding(emitter);
    if (unwinding == NULL) {
        return false;
    }
    cleanup = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    plain = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    next = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    (void)LLVMBuildCondBr(emitter->builder, unwinding, cleanup, plain);
    LLVMPositionBuilderAtEnd(emitter->builder, cleanup);
    if (r_llvm_call_runtime(emitter, "r_runtime_unwind_cleanup_enter", NULL, 0U, NULL) == NULL) {
        return false;
    }
    (void)LLVMBuildCall2(
        emitter->builder, emitter->function_types[drop_function], function, &value, 1U, "");
    if (r_llvm_call_runtime(emitter, "r_runtime_unwind_cleanup_leave", NULL, 0U, NULL) == NULL) {
        return false;
    }
    (void)LLVMBuildBr(emitter->builder, next);
    LLVMPositionBuilderAtEnd(emitter->builder, plain);
    (void)LLVMBuildCall2(
        emitter->builder, emitter->function_types[drop_function], function, &value, 1U, "");
    (void)LLVMBuildBr(emitter->builder, next);
    LLVMPositionBuilderAtEnd(emitter->builder, next);
    return true;
}

/* Self-nesting aggregates (R-FUNC-0004), as the C17 emitter's r_type_cursor_*: the drop glue of
   an aggregate of a recursive ownership group hands the value to r_runtime_drop_iterative with
   the RRuntimeDropNode of its type, so the drop never recurses along the data. A node's cursor
   describes member `index` in drop order: for an aggregate the user drop hook first, then the
   fields from the last or the payload of the active variant; a member that reaches a recursive
   aggregate is delegated to the cursor of its type, any other is a leaf drop. Owners and
   containers on the way are a single member whose child node the runtime walks; options and
   fixed arrays delegate to their payload or elements. */

static bool r_llvm_drop_wrapper_kind(RSemanticTypeKind kind) {
    return (kind == R_SEMANTIC_TYPE_OWN) || (kind == R_SEMANTIC_TYPE_RC) ||
           (kind == R_SEMANTIC_TYPE_ARC) || (kind == R_SEMANTIC_TYPE_OPTION) ||
           (kind == R_SEMANTIC_TYPE_FIXED_ARRAY) || (kind == R_SEMANTIC_TYPE_ARRAY) ||
           (kind == R_SEMANTIC_TYPE_LIST) || (kind == R_SEMANTIC_TYPE_DICT);
}

static RTypeId r_llvm_drop_wrapper_inner(const RSemanticType *type) {
    return type->kind == R_SEMANTIC_TYPE_DICT ? type->second : type->base;
}

/* Whether the type owns, through traversable storage only, an aggregate of a recursive group. */
static bool r_llvm_reaches_recursive(RLlvmEmitter *emitter, RTypeId id, uint32_t depth) {
    const RTypeId value_id = r_llvm_value_type(emitter, id);
    const RSemanticType *type = r_llvm_type(emitter, value_id);

    if ((type == NULL) || (depth > emitter->frontend->options.limits.max_nesting)) {
        return false;
    }
    if ((type->kind == R_SEMANTIC_TYPE_STRUCT) || (type->kind == R_SEMANTIC_TYPE_ENUM)) {
        const RSemanticAggregate *aggregate = r_llvm_aggregate(emitter, value_id);
        return (aggregate != NULL) && (aggregate->recursive_group != 0U);
    }
    return r_llvm_drop_wrapper_kind(type->kind) &&
           r_llvm_reaches_recursive(emitter, r_llvm_drop_wrapper_inner(type), depth + 1U);
}

static bool r_llvm_add_runtime_callee(RLlvmEmitter *emitter, LLVMValueRef function) {
    size_t index;

    for (index = 0U; index < emitter->runtime_callee_count; ++index) {
        if (emitter->runtime_callees[index] == function) {
            return true;
        }
    }
    if (emitter->runtime_callee_count == emitter->runtime_callee_capacity) {
        const size_t capacity =
            emitter->runtime_callee_capacity == 0U ? 16U : emitter->runtime_callee_capacity * 2U;
        LLVMValueRef *grown = r_llvm_allocate(emitter, capacity * sizeof(*grown));
        if (grown == NULL) {
            return false;
        }
        if (emitter->runtime_callee_count != 0U) {
            (void)memcpy(
                grown, emitter->runtime_callees, emitter->runtime_callee_count * sizeof(*grown));
        }
        r_llvm_free(emitter, emitter->runtime_callees);
        emitter->runtime_callees = grown;
        emitter->runtime_callee_capacity = capacity;
    }
    emitter->runtime_callees[emitter->runtime_callee_count] = function;
    emitter->runtime_callee_count += 1U;
    return true;
}

/* Declares the cursor, the member count and the RRuntimeDropNode of a type; the bodies follow
   with the other glue. */
static bool r_llvm_drop_cursor(RLlvmEmitter *emitter, RTypeId id) {
    const RTypeId value_id = r_llvm_value_type(emitter, id);
    LLVMTypeRef cursor_parameters[3];
    LLVMTypeRef node_members[3];
    LLVMValueRef node_values[3];
    LLVMTypeRef count_parameter = r_llvm_pointer(emitter);
    uint32_t size = 0U;
    uint32_t align = 0U;
    char name[64];

    if ((value_id == R_TYPE_ID_INVALID) ||
        ((size_t)value_id > emitter->frontend->semantic_type_count)) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    if (emitter->drop_cursors[value_id] != NULL) {
        return true;
    }
    if (!r_llvm_layout(emitter, value_id, &size, &align)) {
        return false;
    }
    /* RRuntimeDropCursorFn: _Bool (*)(void *node, size_t index, RRuntimeDropMember *member). */
    cursor_parameters[0] = r_llvm_pointer(emitter);
    cursor_parameters[1] = r_llvm_int(emitter, 64U);
    cursor_parameters[2] = r_llvm_pointer(emitter);
    (void)snprintf(
        name, sizeof(name), "r_drop_cursor.%" PRIu32, r_llvm_type_key(emitter, value_id));
    emitter->drop_cursors[value_id] = LLVMAddFunction(
        emitter->module, name, LLVMFunctionType(r_llvm_int(emitter, 1U), cursor_parameters, 3U, 0));
    LLVMSetLinkage(emitter->drop_cursors[value_id], LLVMInternalLinkage);
    LLVMAddAttributeAtIndex(emitter->drop_cursors[value_id],
                            LLVMAttributeReturnIndex,
                            LLVMCreateEnumAttribute(emitter->context,
                                                    LLVMGetEnumAttributeKindForName("zeroext", 7U),
                                                    0U));
    (void)snprintf(name, sizeof(name), "r_drop_count.%" PRIu32, r_llvm_type_key(emitter, value_id));
    emitter->drop_counts[value_id] = LLVMAddFunction(
        emitter->module, name, LLVMFunctionType(r_llvm_int(emitter, 64U), &count_parameter, 1U, 0));
    LLVMSetLinkage(emitter->drop_counts[value_id], LLVMInternalLinkage);
    node_members[0] = r_llvm_pointer(emitter);
    node_members[1] = r_llvm_int(emitter, 64U);
    node_members[2] = r_llvm_int(emitter, 64U);
    node_values[0] = emitter->drop_cursors[value_id];
    node_values[1] = r_llvm_u64(emitter, size);
    node_values[2] = r_llvm_u64(emitter, align);
    (void)snprintf(name, sizeof(name), "r_drop_node.%" PRIu32, r_llvm_type_key(emitter, value_id));
    emitter->drop_nodes[value_id] = LLVMAddGlobal(
        emitter->module, LLVMStructTypeInContext(emitter->context, node_members, 3U, 0), name);
    LLVMSetInitializer(emitter->drop_nodes[value_id],
                       LLVMConstStructInContext(emitter->context, node_values, 3U, 0));
    LLVMSetLinkage(emitter->drop_nodes[value_id], LLVMInternalLinkage);
    LLVMSetGlobalConstant(emitter->drop_nodes[value_id], 1);
    return r_llvm_add_runtime_callee(emitter, emitter->drop_cursors[value_id]) &&
           r_llvm_queue_glue(emitter, value_id, R_LLVM_GLUE_CURSOR);
}

/* The wrapper of an aggregate's user drop: member zero of its cursor. */
static LLVMValueRef r_llvm_drop_hook(RLlvmEmitter *emitter, RTypeId value_id) {
    LLVMTypeRef parameter = r_llvm_pointer(emitter);
    char name[64];

    if (emitter->drop_hooks[value_id] == NULL) {
        (void)snprintf(
            name, sizeof(name), "r_drop_hook.%" PRIu32, r_llvm_type_key(emitter, value_id));
        emitter->drop_hooks[value_id] = LLVMAddFunction(
            emitter->module,
            name,
            LLVMFunctionType(LLVMVoidTypeInContext(emitter->context), &parameter, 1U, 0));
        LLVMSetLinkage(emitter->drop_hooks[value_id], LLVMInternalLinkage);
        if (!r_llvm_add_runtime_callee(emitter, emitter->drop_hooks[value_id]) ||
            !r_llvm_queue_glue(emitter, value_id, R_LLVM_GLUE_HOOK)) {
            return NULL;
        }
    }
    return emitter->drop_hooks[value_id];
}

/* The writer of a cursor or member count body. */
typedef struct RLlvmDropWalk {
    bool count_only;
    LLVMValueRef node;
    LLVMValueRef member;
    /* The index still to find (cursor) or the members counted (count). */
    LLVMValueRef slot;
    uint32_t kind_offset;
    uint32_t address_offset;
    uint32_t drop_offset;
    uint32_t child_offset;
} RLlvmDropWalk;

static LLVMValueRef r_llvm_drop_walk_load(RLlvmEmitter *emitter, const RLlvmDropWalk *walk) {
    return LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 64U), walk->slot, "");
}

/* Writes the member description and returns 1. */
static bool r_llvm_drop_describe(RLlvmEmitter *emitter,
                                 const RLlvmDropWalk *walk,
                                 const char *kind,
                                 LLVMValueRef address,
                                 LLVMValueRef drop,
                                 LLVMValueRef child) {
    int64_t value = 0;

    if (!r_llvm_runtime_constant(emitter, kind, &value)) {
        return false;
    }
    (void)LLVMBuildStore(emitter->builder,
                         r_llvm_u32(emitter, (uint64_t)value),
                         r_llvm_byte_offset(emitter, walk->member, walk->kind_offset));
    (void)LLVMBuildStore(
        emitter->builder, address, r_llvm_byte_offset(emitter, walk->member, walk->address_offset));
    (void)LLVMBuildStore(emitter->builder,
                         drop == NULL ? LLVMConstNull(r_llvm_pointer(emitter)) : drop,
                         r_llvm_byte_offset(emitter, walk->member, walk->drop_offset));
    (void)LLVMBuildStore(emitter->builder,
                         child == NULL ? LLVMConstNull(r_llvm_pointer(emitter)) : child,
                         r_llvm_byte_offset(emitter, walk->member, walk->child_offset));
    (void)LLVMBuildRet(emitter->builder, LLVMConstInt(r_llvm_int(emitter, 1U), 1U, 0));
    return true;
}

/* A leaf member (a drop or the user hook): index zero of what is left answers. */
static bool r_llvm_drop_walk_leaf(RLlvmEmitter *emitter,
                                  const RLlvmDropWalk *walk,
                                  LLVMValueRef address,
                                  LLVMValueRef drop) {
    LLVMValueRef index;
    LLVMBasicBlockRef found;
    LLVMBasicBlockRef next;

    if (walk->count_only) {
        (void)LLVMBuildStore(emitter->builder,
                             LLVMBuildAdd(emitter->builder,
                                          r_llvm_drop_walk_load(emitter, walk),
                                          r_llvm_u64(emitter, 1U),
                                          ""),
                             walk->slot);
        return true;
    }
    index = r_llvm_drop_walk_load(emitter, walk);
    found = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    next = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    (void)LLVMBuildCondBr(
        emitter->builder,
        LLVMBuildICmp(emitter->builder, LLVMIntEQ, index, r_llvm_u64(emitter, 0U), ""),
        found,
        next);
    LLVMPositionBuilderAtEnd(emitter->builder, found);
    if (!r_llvm_drop_describe(emitter, walk, "R_RUNTIME_DROP_MEMBER_LEAF", address, drop, NULL)) {
        return false;
    }
    LLVMPositionBuilderAtEnd(emitter->builder, next);
    (void)LLVMBuildStore(emitter->builder,
                         LLVMBuildSub(emitter->builder, index, r_llvm_u64(emitter, 1U), ""),
                         walk->slot);
    return true;
}

/* A member of type `member_type` at `address`: skipped without drop, delegated to the cursor of
   its type when it reaches a recursive aggregate (answering for the next `count` indexes), a
   leaf drop otherwise. */
static bool r_llvm_drop_walk_member(RLlvmEmitter *emitter,
                                    const RLlvmDropWalk *walk,
                                    RTypeId member_type,
                                    LLVMValueRef address) {
    const RTypeId value_id = r_llvm_value_type(emitter, member_type);
    LLVMValueRef count;
    LLVMValueRef index;
    LLVMValueRef arguments[3];
    LLVMBasicBlockRef found;
    LLVMBasicBlockRef next;

    if (!r_llvm_type_requires_drop(emitter, value_id)) {
        return true;
    }
    if (!r_llvm_reaches_recursive(emitter, value_id, 0U)) {
        LLVMValueRef drop = r_llvm_glue_function(emitter, value_id, R_LLVM_GLUE_DROP);
        return (drop != NULL) && r_llvm_add_runtime_callee(emitter, drop) &&
               r_llvm_drop_walk_leaf(emitter, walk, address, drop);
    }
    if (!r_llvm_drop_cursor(emitter, value_id)) {
        return false;
    }
    count = LLVMBuildCall2(emitter->builder,
                           LLVMGlobalGetValueType(emitter->drop_counts[value_id]),
                           emitter->drop_counts[value_id],
                           &address,
                           1U,
                           "");
    index = r_llvm_drop_walk_load(emitter, walk);
    if (walk->count_only) {
        (void)LLVMBuildStore(
            emitter->builder, LLVMBuildAdd(emitter->builder, index, count, ""), walk->slot);
        return true;
    }
    found = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    next = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    (void)LLVMBuildCondBr(emitter->builder,
                          LLVMBuildICmp(emitter->builder, LLVMIntULT, index, count, ""),
                          found,
                          next);
    LLVMPositionBuilderAtEnd(emitter->builder, found);
    arguments[0] = address;
    arguments[1] = index;
    arguments[2] = walk->member;
    (void)LLVMBuildRet(emitter->builder,
                       LLVMBuildCall2(emitter->builder,
                                      LLVMGlobalGetValueType(emitter->drop_cursors[value_id]),
                                      emitter->drop_cursors[value_id],
                                      arguments,
                                      3U,
                                      ""));
    LLVMPositionBuilderAtEnd(emitter->builder, next);
    (void)LLVMBuildStore(
        emitter->builder, LLVMBuildSub(emitter->builder, index, count, ""), walk->slot);
    return true;
}

static const char *r_llvm_drop_wrapper_member_kind(RSemanticTypeKind kind) {
    switch (kind) {
    case R_SEMANTIC_TYPE_OWN:
        return "R_RUNTIME_DROP_MEMBER_OWN";
    case R_SEMANTIC_TYPE_RC:
        return "R_RUNTIME_DROP_MEMBER_RC";
    case R_SEMANTIC_TYPE_ARC:
        return "R_RUNTIME_DROP_MEMBER_ARC";
    case R_SEMANTIC_TYPE_ARRAY:
        return "R_RUNTIME_DROP_MEMBER_ARRAY";
    case R_SEMANTIC_TYPE_LIST:
        return "R_RUNTIME_DROP_MEMBER_LIST";
    case R_SEMANTIC_TYPE_DICT:
        return "R_RUNTIME_DROP_MEMBER_DICT";
    default:
        return NULL;
    }
}

/* The body of the cursor (count_only false) or the member count of a type. */
static bool r_llvm_drop_walk_body(RLlvmEmitter *emitter, RTypeId type_id, bool count_only) {
    const RSemanticType *type = r_llvm_type(emitter, type_id);
    const RSemanticAggregate *aggregate = r_llvm_aggregate(emitter, type_id);
    const char *member_kind = type == NULL ? NULL : r_llvm_drop_wrapper_member_kind(type->kind);
    RLlvmDropWalk walk;
    uint32_t payload = 0U;
    uint32_t index;

    (void)memset(&walk, 0, sizeof(walk));
    walk.count_only = count_only;
    walk.node = LLVMGetParam(emitter->function, 0U);
    if (type == NULL) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    if (!count_only &&
        (!r_llvm_runtime_field(emitter, "RRuntimeDropMember", "kind", &walk.kind_offset, NULL) ||
         !r_llvm_runtime_field(
             emitter, "RRuntimeDropMember", "address", &walk.address_offset, NULL) ||
         !r_llvm_runtime_field(emitter, "RRuntimeDropMember", "drop", &walk.drop_offset, NULL) ||
         !r_llvm_runtime_field(emitter, "RRuntimeDropMember", "child", &walk.child_offset, NULL))) {
        return false;
    }
    walk.member = count_only ? NULL : LLVMGetParam(emitter->function, 2U);
    if (member_kind != NULL) {
        /* Owners and containers: the storage itself is the single member, and the runtime walks
           its contents with the node of the inner type. */
        const RTypeId inner = r_llvm_value_type(emitter, r_llvm_drop_wrapper_inner(type));
        LLVMBasicBlockRef found;
        LLVMBasicBlockRef none;
        if (count_only) {
            (void)LLVMBuildRet(emitter->builder, r_llvm_u64(emitter, 1U));
            return true;
        }
        if (!r_llvm_drop_cursor(emitter, inner)) {
            return false;
        }
        found = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        none = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        (void)LLVMBuildCondBr(emitter->builder,
                              LLVMBuildICmp(emitter->builder,
                                            LLVMIntEQ,
                                            LLVMGetParam(emitter->function, 1U),
                                            r_llvm_u64(emitter, 0U),
                                            ""),
                              found,
                              none);
        LLVMPositionBuilderAtEnd(emitter->builder, found);
        if (!r_llvm_drop_describe(
                emitter, &walk, member_kind, walk.node, NULL, emitter->drop_nodes[inner])) {
            return false;
        }
        LLVMPositionBuilderAtEnd(emitter->builder, none);
        (void)LLVMBuildRet(emitter->builder, LLVMConstInt(r_llvm_int(emitter, 1U), 0U, 0));
        return true;
    }
    walk.slot = r_llvm_entry_alloca(emitter, 8U, 8U, count_only ? "count" : "index");
    (void)LLVMBuildStore(emitter->builder,
                         count_only ? r_llvm_u64(emitter, 0U) : LLVMGetParam(emitter->function, 1U),
                         walk.slot);
    if (type->kind == R_SEMANTIC_TYPE_OPTION) {
        LLVMBasicBlockRef next;
        if (!r_llvm_payload_offset(emitter, type_id, &payload)) {
            return false;
        }
        next = r_llvm_if_tag(emitter, walk.node, 1U, true);
        if (!r_llvm_drop_walk_member(
                emitter, &walk, type->base, r_llvm_byte_offset(emitter, walk.node, payload))) {
            return false;
        }
        r_llvm_end_if(emitter, next);
    } else if (type->kind == R_SEMANTIC_TYPE_FIXED_ARRAY) {
        uint32_t element_size = 0U;
        uint32_t element_align = 0U;
        uint64_t element = type->length;
        if (!r_llvm_layout(emitter, type->base, &element_size, &element_align)) {
            return false;
        }
        while (element != 0U) {
            element -= 1U;
            if (!r_llvm_drop_walk_member(
                    emitter,
                    &walk,
                    type->base,
                    r_llvm_byte_offset(emitter, walk.node, element * element_size))) {
                return false;
            }
        }
    } else if ((aggregate != NULL) && (aggregate->recursive_group != 0U)) {
        if (aggregate->is_tagged) {
            /* A value whose payload moved out holds no variant and no members. */
            LLVMBasicBlockRef empty =
                LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
            LLVMBasicBlockRef live =
                LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
            (void)LLVMBuildCondBr(
                emitter->builder,
                LLVMBuildICmp(
                    emitter->builder,
                    LLVMIntEQ,
                    LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), walk.node, ""),
                    r_llvm_u32(emitter, UINT32_MAX),
                    ""),
                empty,
                live);
            LLVMPositionBuilderAtEnd(emitter->builder, empty);
            (void)LLVMBuildRet(emitter->builder,
                               count_only ? r_llvm_u64(emitter, 0U)
                                          : LLVMConstInt(r_llvm_int(emitter, 1U), 0U, 0));
            LLVMPositionBuilderAtEnd(emitter->builder, live);
        }
        if (aggregate->drop_function != R_SYMBOL_ID_INVALID) {
            LLVMValueRef hook = r_llvm_drop_hook(emitter, type_id);
            if ((hook == NULL) || !r_llvm_drop_walk_leaf(emitter, &walk, walk.node, hook)) {
                return false;
            }
        }
        if (aggregate->is_tagged) {
            if (!r_llvm_payload_offset(emitter, type_id, &payload)) {
                return false;
            }
            for (index = 0U; index < aggregate->variant_count; ++index) {
                const RSemanticVariant *variant =
                    r_semantic_tagged_variant(emitter->frontend, type_id, index);
                LLVMBasicBlockRef next;
                if ((variant == NULL) || (variant->payload_type == R_TYPE_ID_INVALID) ||
                    !r_llvm_type_requires_drop(emitter, variant->payload_type)) {
                    continue;
                }
                next = r_llvm_if_tag(emitter, walk.node, index, true);
                if (!r_llvm_drop_walk_member(emitter,
                                             &walk,
                                             variant->payload_type,
                                             r_llvm_byte_offset(emitter, walk.node, payload))) {
                    return false;
                }
                r_llvm_end_if(emitter, next);
            }
        } else {
            index = aggregate->field_count;
            while (index != 0U) {
                const uint32_t field_id = aggregate->first_field + index;
                const RSemanticField *field = r_llvm_field(emitter, field_id);
                uint32_t offset = 0U;
                index -= 1U;
                if ((field == NULL) || !r_llvm_field_offset(emitter, field_id, &offset)) {
                    return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
                }
                if (!r_llvm_drop_walk_member(emitter,
                                             &walk,
                                             field->type,
                                             r_llvm_byte_offset(emitter, walk.node, offset))) {
                    return false;
                }
            }
        }
    } else {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    (void)LLVMBuildRet(emitter->builder,
                       count_only ? r_llvm_drop_walk_load(emitter, &walk)
                                  : LLVMConstInt(r_llvm_int(emitter, 1U), 0U, 0));
    return true;
}

/* The drop glue of a self-nesting aggregate; a tagged one is left without a variant. */
static bool r_llvm_iterative_drop(RLlvmEmitter *emitter, RTypeId type_id, LLVMValueRef value) {
    const RTypeId value_id = r_llvm_value_type(emitter, type_id);
    const RSemanticAggregate *aggregate = r_llvm_aggregate(emitter, value_id);
    LLVMValueRef arguments[2];

    if ((aggregate == NULL) || !r_llvm_drop_cursor(emitter, value_id)) {
        return false;
    }
    arguments[0] = value;
    arguments[1] = emitter->drop_nodes[value_id];
    if (r_llvm_call_runtime(emitter, "r_runtime_drop_iterative", arguments, 2U, NULL) == NULL) {
        return false;
    }
    if (aggregate->is_tagged) {
        (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, UINT32_MAX), value);
    }
    return true;
}

static bool r_llvm_glue_body(RLlvmEmitter *emitter, RTypeId type_id, unsigned kind) {
    const RSemanticType *type = r_llvm_type(emitter, type_id);
    const RSemanticAggregate *aggregate = r_llvm_aggregate(emitter, type_id);
    LLVMValueRef first = LLVMGetParam(emitter->function, 0U);
    LLVMValueRef second = kind == R_LLVM_GLUE_MOVE ? LLVMGetParam(emitter->function, 1U) : NULL;
    const RNamedStandardMoveAbi *standard = r_llvm_standard_move_abi(emitter, type);
    const char *release = r_llvm_owner_release(emitter, type_id);
    uint32_t size = 0U;
    uint32_t align = 0U;
    uint32_t payload = 0U;
    uint32_t index;

    if ((type == NULL) || !r_llvm_layout(emitter, type_id, &size, &align)) {
        return false;
    }
    if (r_llvm_thread_join_result(emitter, type_id)) {
        return r_llvm_thread_join_glue(emitter, type_id, kind == R_LLVM_GLUE_MOVE, first, second);
    }
    if (r_llvm_standard_tagged(emitter, type_id)) {
        return r_llvm_standard_tagged_glue(
            emitter, type_id, kind == R_LLVM_GLUE_MOVE, first, second);
    }
    {
        bool handled = false;
        const bool glued =
            r_llvm_sync_glue(emitter, type_id, kind == R_LLVM_GLUE_MOVE, first, second, &handled);
        if (handled || !glued) {
            return glued;
        }
    }
    if (release != NULL) {
        /* Owners move bitwise and leave a zero source; a drop releases (an owner of an
           interface releases its owner member, at offset zero). */
        if (kind == R_LLVM_GLUE_MOVE) {
            (void)LLVMBuildMemCpy(
                emitter->builder, first, align, second, align, r_llvm_u64(emitter, size));
            (void)LLVMBuildMemSet(emitter->builder,
                                  second,
                                  LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                                  r_llvm_u64(emitter, size),
                                  align);
            return true;
        }
        return r_llvm_glue_runtime(emitter, release, first, NULL);
    }
    if (standard != NULL) {
        return kind == R_LLVM_GLUE_MOVE
                   ? r_llvm_glue_runtime(emitter, standard->move_initialize, first, second)
                   : r_llvm_glue_runtime(emitter, standard->drop, first, NULL);
    }
    if (r_llvm_recovering_error(emitter, type_id) != R_LLVM_RECOVERING_NONE) {
        /* r_c17_emit_type_glue_array_error: the reason is copied, the key and the value move;
           a tagged error moves only its allocation_failed variant and leaves no variant behind. */
        const bool plain = r_llvm_recovering_error(emitter, type_id) == R_LLVM_RECOVERING_NEW;
        RLlvmRecoveringMembers members;
        uint32_t reason_size = 0U;
        uint32_t reason_align = 0U;
        LLVMValueRef base_first;
        LLVMValueRef base_second;
        LLVMBasicBlockRef next = NULL;
        if (!r_llvm_recovering_members(emitter, type_id, &members) ||
            !r_llvm_runtime_layout(emitter, "RStdAllocError", &reason_size, &reason_align) ||
            (!plain && !r_llvm_payload_offset(emitter, type_id, &payload))) {
            return false;
        }
        base_first = plain ? first : r_llvm_byte_offset(emitter, first, payload);
        base_second =
            plain || (second == NULL) ? second : r_llvm_byte_offset(emitter, second, payload);
        if (!plain) {
            next = r_llvm_if_tag(emitter, kind == R_LLVM_GLUE_MOVE ? second : first, 0U, true);
        }
        if (kind == R_LLVM_GLUE_MOVE) {
            (void)LLVMBuildMemCpy(emitter->builder,
                                  base_first,
                                  reason_align,
                                  base_second,
                                  reason_align,
                                  r_llvm_u64(emitter, reason_size));
            if ((members.key_type != R_TYPE_ID_INVALID) &&
                !r_llvm_move_member(emitter,
                                    members.key_type,
                                    r_llvm_byte_offset(emitter, base_first, members.key_offset),
                                    r_llvm_byte_offset(emitter, base_second, members.key_offset))) {
                return false;
            }
            if (!r_llvm_move_member(
                    emitter,
                    members.value_type,
                    r_llvm_byte_offset(emitter, base_first, members.value_offset),
                    r_llvm_byte_offset(emitter, base_second, members.value_offset))) {
                return false;
            }
        } else {
            if ((members.key_type != R_TYPE_ID_INVALID) &&
                !r_llvm_drop_member(emitter,
                                    members.key_type,
                                    r_llvm_byte_offset(emitter, base_first, members.key_offset))) {
                return false;
            }
            if (!r_llvm_drop_member(
                    emitter,
                    members.value_type,
                    r_llvm_byte_offset(emitter, base_first, members.value_offset))) {
                return false;
            }
        }
        if (plain) {
            return true;
        }
        r_llvm_end_if(emitter, next);
        if (kind == R_LLVM_GLUE_MOVE) {
            (void)LLVMBuildStore(
                emitter->builder,
                LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), second, ""),
                first);
        }
        (void)LLVMBuildStore(emitter->builder,
                             r_llvm_u32(emitter, UINT32_MAX),
                             kind == R_LLVM_GLUE_MOVE ? second : first);
        return true;
    }
    if (type->kind == R_SEMANTIC_TYPE_ATOMIC) {
        if (kind == R_LLVM_GLUE_DROP) {
            return true;
        }
        if ((size != 1U) && (size != 2U) && (size != 4U) && (size != 8U)) {
            return r_llvm_unsupported(emitter, "a move of this atomic value");
        }
        {
            LLVMValueRef load =
                LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, size * 8U), second, "");
            LLVMSetOrdering(load, LLVMAtomicOrderingMonotonic);
            LLVMSetAlignment(load, size);
            (void)LLVMBuildStore(emitter->builder, load, first);
        }
        return true;
    }
    if ((aggregate != NULL) && (aggregate->kind == R_SEMANTIC_AGGREGATE_STRUCT) &&
        !aggregate->is_tagged) {
        if ((kind == R_LLVM_GLUE_DROP) && (aggregate->recursive_group != 0U)) {
            /* Its move is the ordinary one. */
            return r_llvm_iterative_drop(emitter, type_id, first);
        }
        if (kind == R_LLVM_GLUE_MOVE) {
            for (index = 0U; index < aggregate->field_count; ++index) {
                const uint32_t field_id = aggregate->first_field + index + 1U;
                const RSemanticField *field = r_llvm_field(emitter, field_id);
                uint32_t offset = 0U;
                if ((field == NULL) || !r_llvm_field_offset(emitter, field_id, &offset) ||
                    !r_llvm_move_member(emitter,
                                        field->type,
                                        r_llvm_byte_offset(emitter, first, offset),
                                        r_llvm_byte_offset(emitter, second, offset))) {
                    return false;
                }
            }
            return true;
        }
        if ((aggregate->drop_function != R_SYMBOL_ID_INVALID) &&
            !r_llvm_user_drop(emitter, aggregate->drop_function, first)) {
            return false;
        }
        index = aggregate->field_count;
        while (index != 0U) {
            const uint32_t field_id = aggregate->first_field + index;
            const RSemanticField *field = r_llvm_field(emitter, field_id);
            uint32_t offset = 0U;
            index -= 1U;
            if ((field == NULL) || !r_llvm_field_offset(emitter, field_id, &offset) ||
                !r_llvm_drop_member(
                    emitter, field->type, r_llvm_byte_offset(emitter, first, offset))) {
                return false;
            }
        }
        return true;
    }
    if ((aggregate != NULL) && aggregate->is_tagged) {
        if ((kind == R_LLVM_GLUE_DROP) && (aggregate->recursive_group != 0U)) {
            /* Its move is the ordinary one. */
            return r_llvm_iterative_drop(emitter, type_id, first);
        }
        if (!r_llvm_payload_offset(emitter, type_id, &payload)) {
            return false;
        }
        if ((kind == R_LLVM_GLUE_DROP) && (aggregate->drop_function != R_SYMBOL_ID_INVALID)) {
            LLVMBasicBlockRef next = r_llvm_if_tag(emitter, first, UINT32_MAX, false);
            if (!r_llvm_user_drop(emitter, aggregate->drop_function, first)) {
                return false;
            }
            r_llvm_end_if(emitter, next);
        }
        for (index = 0U; index < aggregate->variant_count; ++index) {
            const RSemanticVariant *variant =
                r_semantic_tagged_variant(emitter->frontend, type_id, index);
            LLVMBasicBlockRef next;
            if ((variant == NULL) || (variant->payload_type == R_TYPE_ID_INVALID) ||
                ((kind == R_LLVM_GLUE_DROP) &&
                 !r_llvm_type_requires_drop(emitter, variant->payload_type))) {
                continue;
            }
            next = r_llvm_if_tag(emitter, kind == R_LLVM_GLUE_MOVE ? second : first, index, true);
            if (!(kind == R_LLVM_GLUE_MOVE
                      ? r_llvm_move_member(emitter,
                                           variant->payload_type,
                                           r_llvm_byte_offset(emitter, first, payload),
                                           r_llvm_byte_offset(emitter, second, payload))
                      : r_llvm_drop_member(emitter,
                                           variant->payload_type,
                                           r_llvm_byte_offset(emitter, first, payload)))) {
                return false;
            }
            r_llvm_end_if(emitter, next);
        }
        if (kind == R_LLVM_GLUE_MOVE) {
            (void)LLVMBuildStore(
                emitter->builder,
                LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), second, ""),
                first);
        }
        (void)LLVMBuildStore(emitter->builder,
                             r_llvm_u32(emitter, UINT32_MAX),
                             kind == R_LLVM_GLUE_MOVE ? second : first);
        return true;
    }
    switch (type->kind) {
    case R_SEMANTIC_TYPE_FIXED_ARRAY: {
        uint32_t element_size = 0U;
        uint32_t element_align = 0U;
        uint64_t element;
        if (!r_llvm_layout(emitter, type->base, &element_size, &element_align)) {
            return false;
        }
        if (kind == R_LLVM_GLUE_MOVE) {
            for (element = 0U; element < type->length; ++element) {
                if (!r_llvm_move_member(
                        emitter,
                        type->base,
                        r_llvm_byte_offset(emitter, first, element * element_size),
                        r_llvm_byte_offset(emitter, second, element * element_size))) {
                    return false;
                }
            }
            return true;
        }
        element = type->length;
        while (element != 0U) {
            element -= 1U;
            if (!r_llvm_drop_member(emitter,
                                    type->base,
                                    r_llvm_byte_offset(emitter, first, element * element_size))) {
                return false;
            }
        }
        return true;
    }
    case R_SEMANTIC_TYPE_OPTION: {
        LLVMBasicBlockRef next;
        if (r_llvm_standard_named(emitter, type->base, "std.time::system_time") ||
            r_llvm_standard_named(emitter, type->base, "std.time::duration") ||
            !r_llvm_payload_offset(emitter, type_id, &payload)) {
            return r_llvm_unsupported(emitter, "the glue of this option");
        }
        if (kind == R_LLVM_GLUE_MOVE) {
            (void)LLVMBuildStore(
                emitter->builder,
                LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), second, ""),
                first);
        }
        next = r_llvm_if_tag(emitter, kind == R_LLVM_GLUE_MOVE ? second : first, 1U, true);
        if (!(kind == R_LLVM_GLUE_MOVE
                  ? r_llvm_move_member(emitter,
                                       type->base,
                                       r_llvm_byte_offset(emitter, first, payload),
                                       r_llvm_byte_offset(emitter, second, payload))
                  : r_llvm_drop_member(
                        emitter, type->base, r_llvm_byte_offset(emitter, first, payload)))) {
            return false;
        }
        r_llvm_end_if(emitter, next);
        return true;
    }
    case R_SEMANTIC_TYPE_RESULT: {
        const bool has_ok = !r_llvm_type_is_void(emitter, type->base);
        LLVMValueRef tagged = kind == R_LLVM_GLUE_MOVE ? second : first;
        LLVMBasicBlockRef ok;
        LLVMBasicBlockRef error;
        LLVMBasicBlockRef next;
        if (!r_llvm_payload_offset(emitter, type_id, &payload)) {
            return false;
        }
        if (kind == R_LLVM_GLUE_MOVE) {
            (void)LLVMBuildStore(
                emitter->builder,
                LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), second, ""),
                first);
        }
        if (!has_ok ||
            ((kind == R_LLVM_GLUE_DROP) && !r_llvm_type_requires_drop(emitter, type->base))) {
            /* Only the error has glue: tag one. */
            next = r_llvm_if_tag(emitter, tagged, 1U, true);
        } else {
            ok = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
            error = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
            next = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
            (void)LLVMBuildCondBr(
                emitter->builder,
                LLVMBuildICmp(
                    emitter->builder,
                    LLVMIntEQ,
                    LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), tagged, ""),
                    r_llvm_u32(emitter, 0U),
                    ""),
                ok,
                error);
            LLVMPositionBuilderAtEnd(emitter->builder, ok);
            if (!(kind == R_LLVM_GLUE_MOVE
                      ? r_llvm_move_member(emitter,
                                           type->base,
                                           r_llvm_byte_offset(emitter, first, payload),
                                           r_llvm_byte_offset(emitter, second, payload))
                      : r_llvm_drop_member(
                            emitter, type->base, r_llvm_byte_offset(emitter, first, payload)))) {
                return false;
            }
            (void)LLVMBuildBr(emitter->builder, next);
            LLVMPositionBuilderAtEnd(emitter->builder, error);
        }
        if (!(kind == R_LLVM_GLUE_MOVE
                  ? r_llvm_move_member(emitter,
                                       type->second,
                                       r_llvm_byte_offset(emitter, first, payload),
                                       r_llvm_byte_offset(emitter, second, payload))
                  : r_llvm_drop_member(
                        emitter, type->second, r_llvm_byte_offset(emitter, first, payload)))) {
            return false;
        }
        r_llvm_end_if(emitter, next);
        return true;
    }
    case R_SEMANTIC_TYPE_EFFECT_CARRIER: {
        const uint32_t count = r_semantic_effect_count(emitter->frontend, type->second);
        LLVMValueRef tagged = kind == R_LLVM_GLUE_MOVE ? second : first;
        if (!r_llvm_payload_offset(emitter, type_id, &payload)) {
            return false;
        }
        for (index = 0U; index <= count; ++index) {
            const RTypeId member =
                index == 0U ? type->base
                            : r_semantic_effect_at(emitter->frontend, type->second, index - 1U);
            LLVMBasicBlockRef next;
            if (r_llvm_type_is_void(emitter, member) ||
                ((kind == R_LLVM_GLUE_DROP) && !r_llvm_type_requires_drop(emitter, member))) {
                continue;
            }
            next = r_llvm_if_tag(emitter, tagged, index, true);
            if (!(kind == R_LLVM_GLUE_MOVE
                      ? r_llvm_move_member(emitter,
                                           member,
                                           r_llvm_byte_offset(emitter, first, payload),
                                           r_llvm_byte_offset(emitter, second, payload))
                      : r_llvm_drop_member(
                            emitter, member, r_llvm_byte_offset(emitter, first, payload)))) {
                return false;
            }
            r_llvm_end_if(emitter, next);
        }
        if (kind == R_LLVM_GLUE_MOVE) {
            (void)LLVMBuildStore(
                emitter->builder,
                LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), second, ""),
                first);
        }
        return true;
    }
    default: {
        const RInternEntry *name = r_llvm_standard_name(emitter, type);
        const char *kind_name = r_semantic_type_kind_name(type->kind);
        return name != NULL ? r_llvm_unsupported_detail(
                                  emitter, "the glue of this type", name->bytes, name->length)
                            : r_llvm_unsupported_detail(emitter,
                                                        "the glue of this type",
                                                        kind_name,
                                                        kind_name == NULL ? 0U : strlen(kind_name));
    }
    }
}

/* ---- Keys of std.dict (r_c17_emit_core_key_helper) ---- */

LLVMValueRef r_llvm_key_function(RLlvmEmitter *emitter, RTypeId key, bool hash) {
    const RTypeId value_id = r_llvm_value_type(emitter, key);
    LLVMValueRef *slot;
    LLVMTypeRef parameters[2];
    char name[64];

    if ((value_id == R_TYPE_ID_INVALID) ||
        ((size_t)value_id > emitter->frontend->semantic_type_count)) {
        (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        return NULL;
    }
    slot = hash ? &emitter->key_hashes[value_id] : &emitter->key_equals[value_id];
    if (*slot != NULL) {
        return *slot;
    }
    parameters[0] = r_llvm_pointer(emitter);
    parameters[1] = r_llvm_pointer(emitter);
    (void)snprintf(name,
                   sizeof(name),
                   hash ? "r_key_hash.%" PRIu32 : "r_key_equal.%" PRIu32,
                   r_llvm_type_key(emitter, value_id));
    *slot = LLVMAddFunction(
        emitter->module,
        name,
        LLVMFunctionType(r_llvm_int(emitter, hash ? 64U : 1U), parameters, hash ? 1U : 2U, 0));
    LLVMSetLinkage(*slot, LLVMInternalLinkage);
    if (!hash) {
        /* `_Bool` results are extended by the callee (AAPCS64 darwin). */
        LLVMAddAttributeAtIndex(
            *slot,
            LLVMAttributeReturnIndex,
            LLVMCreateEnumAttribute(
                emitter->context, LLVMGetEnumAttributeKindForName("zeroext", 7U), 0U));
    }
    if (!r_llvm_queue_glue(
            emitter, value_id, hash ? R_LLVM_GLUE_KEY_HASH : R_LLVM_GLUE_KEY_EQUAL)) {
        return NULL;
    }
    return *slot;
}

/* The bytes of a string key: `str`, `constexpr str` (both {data, length}) or an owned string. */
static bool r_llvm_key_bytes(RLlvmEmitter *emitter,
                             RTypeId type,
                             LLVMValueRef value,
                             LLVMValueRef *data,
                             LLVMValueRef *length) {
    uint32_t data_offset = 0U;
    uint32_t length_offset = 8U;

    if (r_llvm_standard_named(emitter, type, "std.string::string")) {
        uint32_t bytes = 0U;
        if (!r_llvm_runtime_field(emitter, "RRuntimeString", "bytes", &bytes, NULL) ||
            !r_llvm_runtime_field(emitter, "RRuntimeArray", "data", &data_offset, NULL) ||
            !r_llvm_runtime_field(emitter, "RRuntimeArray", "length", &length_offset, NULL)) {
            return false;
        }
        data_offset += bytes;
        length_offset += bytes;
    }
    *data = LLVMBuildLoad2(emitter->builder,
                           r_llvm_pointer(emitter),
                           r_llvm_byte_offset(emitter, value, data_offset),
                           "");
    *length = LLVMBuildLoad2(emitter->builder,
                             r_llvm_int(emitter, 64U),
                             r_llvm_byte_offset(emitter, value, length_offset),
                             "");
    return true;
}

static bool r_llvm_key_body(RLlvmEmitter *emitter, RTypeId type_id, bool hash) {
    const RSemanticType *type = r_llvm_type(emitter, type_id);
    const RSemanticAggregate *aggregate = r_llvm_aggregate(emitter, type_id);
    LLVMValueRef left = LLVMGetParam(emitter->function, 0U);
    LLVMValueRef right = hash ? NULL : LLVMGetParam(emitter->function, 1U);
    LLVMTypeRef scalar;

    if (type == NULL) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    if (((type->kind == R_SEMANTIC_TYPE_STRUCT) || (type->kind == R_SEMANTIC_TYPE_ENUM)) &&
        (aggregate != NULL)) {
        const RSymbolId hook = hash ? aggregate->hash_function : aggregate->equal_function;
        if (hook != R_SYMBOL_ID_INVALID) {
            LLVMValueRef arguments[2];
            LLVMValueRef result;
            if (emitter->functions[hook] == NULL) {
                return r_llvm_unsupported(emitter, "a key function that is not lowered");
            }
            arguments[0] = left;
            arguments[1] = right;
            result = LLVMBuildCall2(emitter->builder,
                                    emitter->function_types[hook],
                                    emitter->functions[hook],
                                    arguments,
                                    hash ? 1U : 2U,
                                    "");
            (void)LLVMBuildRet(emitter->builder, result);
            return true;
        }
        if ((type->kind == R_SEMANTIC_TYPE_STRUCT) || aggregate->is_tagged) {
            return r_llvm_unsupported(emitter, "a key without hash and equality functions");
        }
    }
    if ((type->kind == R_SEMANTIC_TYPE_STR) || (type->kind == R_SEMANTIC_TYPE_CONSTEXPR_STR) ||
        r_llvm_standard_named(emitter, type_id, "std.string::string")) {
        LLVMValueRef data = NULL;
        LLVMValueRef length = NULL;
        if (!r_llvm_key_bytes(emitter, type_id, left, &data, &length)) {
            return false;
        }
        if (hash) {
            /* FNV-1a over the bytes. */
            LLVMBasicBlockRef entry = LLVMGetInsertBlock(emitter->builder);
            LLVMBasicBlockRef loop =
                LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
            LLVMBasicBlockRef body =
                LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
            LLVMBasicBlockRef done =
                LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
            LLVMValueRef index;
            LLVMValueRef state;
            LLVMValueRef next_index;
            LLVMValueRef next_state;
            LLVMValueRef incoming_values[2];
            LLVMBasicBlockRef incoming_blocks[2];
            (void)LLVMBuildBr(emitter->builder, loop);
            LLVMPositionBuilderAtEnd(emitter->builder, loop);
            index = LLVMBuildPhi(emitter->builder, r_llvm_int(emitter, 64U), "");
            state = LLVMBuildPhi(emitter->builder, r_llvm_int(emitter, 64U), "");
            (void)LLVMBuildCondBr(emitter->builder,
                                  LLVMBuildICmp(emitter->builder, LLVMIntULT, index, length, ""),
                                  body,
                                  done);
            LLVMPositionBuilderAtEnd(emitter->builder, body);
            next_state = LLVMBuildMul(
                emitter->builder,
                LLVMBuildXor(
                    emitter->builder,
                    state,
                    LLVMBuildZExt(
                        emitter->builder,
                        LLVMBuildLoad2(
                            emitter->builder,
                            r_llvm_int(emitter, 8U),
                            LLVMBuildGEP2(
                                emitter->builder, r_llvm_int(emitter, 8U), data, &index, 1U, ""),
                            ""),
                        r_llvm_int(emitter, 64U),
                        ""),
                    ""),
                r_llvm_u64(emitter, UINT64_C(1099511628211)),
                "");
            next_index = LLVMBuildAdd(emitter->builder, index, r_llvm_u64(emitter, 1U), "");
            (void)LLVMBuildBr(emitter->builder, loop);
            incoming_values[0] = r_llvm_u64(emitter, 0U);
            incoming_values[1] = next_index;
            incoming_blocks[0] = entry;
            incoming_blocks[1] = body;
            LLVMAddIncoming(index, incoming_values, incoming_blocks, 2U);
            incoming_values[0] = r_llvm_u64(emitter, UINT64_C(14695981039346656037));
            incoming_values[1] = next_state;
            LLVMAddIncoming(state, incoming_values, incoming_blocks, 2U);
            LLVMPositionBuilderAtEnd(emitter->builder, done);
            (void)LLVMBuildRet(emitter->builder, state);
            return true;
        }
        {
            LLVMValueRef right_data = NULL;
            LLVMValueRef right_length = NULL;
            LLVMTypeRef memcmp_parameters[3];
            LLVMTypeRef memcmp_type;
            LLVMValueRef memcmp_function = LLVMGetNamedFunction(emitter->module, "memcmp");
            LLVMValueRef arguments[3];
            LLVMBasicBlockRef compare =
                LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
            LLVMBasicBlockRef unequal =
                LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
            if (!r_llvm_key_bytes(emitter, type_id, right, &right_data, &right_length)) {
                return false;
            }
            memcmp_parameters[0] = r_llvm_pointer(emitter);
            memcmp_parameters[1] = r_llvm_pointer(emitter);
            memcmp_parameters[2] = r_llvm_int(emitter, 64U);
            memcmp_type = LLVMFunctionType(r_llvm_int(emitter, 32U), memcmp_parameters, 3U, 0);
            if (memcmp_function == NULL) {
                memcmp_function = LLVMAddFunction(emitter->module, "memcmp", memcmp_type);
            }
            (void)LLVMBuildCondBr(
                emitter->builder,
                LLVMBuildICmp(emitter->builder, LLVMIntEQ, length, right_length, ""),
                compare,
                unequal);
            LLVMPositionBuilderAtEnd(emitter->builder, unequal);
            (void)LLVMBuildRet(emitter->builder, LLVMConstInt(r_llvm_int(emitter, 1U), 0U, 0));
            LLVMPositionBuilderAtEnd(emitter->builder, compare);
            arguments[0] = data;
            arguments[1] = right_data;
            arguments[2] = length;
            /* A zero length compares equal without reading the bytes. */
            (void)LLVMBuildRet(
                emitter->builder,
                LLVMBuildOr(
                    emitter->builder,
                    LLVMBuildICmp(emitter->builder, LLVMIntEQ, length, r_llvm_u64(emitter, 0U), ""),
                    LLVMBuildICmp(
                        emitter->builder,
                        LLVMIntEQ,
                        LLVMBuildCall2(
                            emitter->builder, memcmp_type, memcmp_function, arguments, 3U, ""),
                        LLVMConstInt(r_llvm_int(emitter, 32U), 0U, 0),
                        ""),
                    ""));
            return true;
        }
    }
    if ((type->kind == R_SEMANTIC_TYPE_F32) || (type->kind == R_SEMANTIC_TYPE_F64)) {
        LLVMTypeRef floating = r_llvm_scalar_type(emitter, type_id);
        LLVMValueRef value = LLVMBuildLoad2(emitter->builder, floating, left, "");
        if (hash) {
            /* Zeros hash alike, every NaN as the quiet NaN, others by the bits of the double. */
            LLVMValueRef wide =
                type->kind == R_SEMANTIC_TYPE_F64
                    ? value
                    : LLVMBuildFPExt(
                          emitter->builder, value, LLVMDoubleTypeInContext(emitter->context), "");
            LLVMValueRef bits =
                LLVMBuildBitCast(emitter->builder, wide, r_llvm_int(emitter, 64U), "");
            LLVMValueRef zero =
                LLVMBuildFCmp(emitter->builder,
                              LLVMRealOEQ,
                              wide,
                              LLVMConstReal(LLVMDoubleTypeInContext(emitter->context), 0.0),
                              "");
            LLVMValueRef nan = LLVMBuildFCmp(emitter->builder, LLVMRealUNO, wide, wide, "");
            (void)LLVMBuildRet(
                emitter->builder,
                LLVMBuildSelect(emitter->builder,
                                zero,
                                r_llvm_u64(emitter, 0U),
                                LLVMBuildSelect(emitter->builder,
                                                nan,
                                                r_llvm_u64(emitter, UINT64_C(0x7ff8000000000000)),
                                                bits,
                                                ""),
                                ""));
            return true;
        }
        {
            LLVMValueRef other = LLVMBuildLoad2(emitter->builder, floating, right, "");
            (void)LLVMBuildRet(
                emitter->builder,
                LLVMBuildOr(
                    emitter->builder,
                    LLVMBuildFCmp(emitter->builder, LLVMRealOEQ, value, other, ""),
                    LLVMBuildAnd(emitter->builder,
                                 LLVMBuildFCmp(emitter->builder, LLVMRealUNO, value, value, ""),
                                 LLVMBuildFCmp(emitter->builder, LLVMRealUNO, other, other, ""),
                                 ""),
                    ""));
            return true;
        }
    }
    if ((type->kind == R_SEMANTIC_TYPE_OWN) || (type->kind == R_SEMANTIC_TYPE_ARC) ||
        (type->kind == R_SEMANTIC_TYPE_RC) || (type->kind == R_SEMANTIC_TYPE_WEAK)) {
        /* Owners compare by the identity of their allocation, the first member of each. */
        LLVMValueRef address = LLVMBuildLoad2(emitter->builder, r_llvm_pointer(emitter), left, "");
        if (hash) {
            (void)LLVMBuildRet(
                emitter->builder,
                LLVMBuildPtrToInt(emitter->builder, address, r_llvm_int(emitter, 64U), ""));
            return true;
        }
        (void)LLVMBuildRet(
            emitter->builder,
            LLVMBuildICmp(emitter->builder,
                          LLVMIntEQ,
                          address,
                          LLVMBuildLoad2(emitter->builder, r_llvm_pointer(emitter), right, ""),
                          ""));
        return true;
    }
    scalar = r_llvm_scalar_type(emitter, type_id);
    if (scalar == NULL) {
        return r_llvm_unsupported(emitter, "a key of this type");
    }
    {
        LLVMValueRef value = r_llvm_load_scalar(emitter, type_id, left);
        if (hash) {
            unsigned bits = 0U;
            bool is_signed = false;
            if (LLVMGetTypeKind(scalar) == LLVMPointerTypeKind) {
                value = LLVMBuildPtrToInt(emitter->builder, value, r_llvm_int(emitter, 64U), "");
            } else if (LLVMGetTypeKind(scalar) == LLVMIntegerTypeKind) {
                /* (uint64_t) of the C value: a signed value is sign-extended. */
                is_signed = (r_llvm_kind_integer(type->kind, &bits, &is_signed) ||
                             r_llvm_enum_integer(emitter, type_id, &bits, &is_signed)) &&
                            is_signed;
                value = LLVMBuildIntCast2(
                    emitter->builder, value, r_llvm_int(emitter, 64U), is_signed, "");
            } else {
                return r_llvm_unsupported(emitter, "a key of this type");
            }
            (void)LLVMBuildRet(emitter->builder, value);
            return true;
        }
        if ((LLVMGetTypeKind(scalar) != LLVMPointerTypeKind) &&
            (LLVMGetTypeKind(scalar) != LLVMIntegerTypeKind)) {
            return r_llvm_unsupported(emitter, "a key of this type");
        }
        (void)LLVMBuildRet(emitter->builder,
                           LLVMBuildICmp(emitter->builder,
                                         LLVMIntEQ,
                                         value,
                                         r_llvm_load_scalar(emitter, type_id, right),
                                         ""));
        return true;
    }
}

/* RRuntimeTypeInfo of a type in memory: its size and alignment, and the move and drop glue the
   runtime calls when the type requires drop (NULL otherwise), as r_c17_emit_runtime_type_info. */
LLVMValueRef r_llvm_type_info(RLlvmEmitter *emitter, RTypeId type) {
    uint32_t info_size = 0U;
    uint32_t info_align = 0U;
    uint32_t size_offset = 0U;
    uint32_t align_offset = 0U;
    uint32_t move_offset = 0U;
    uint32_t drop_offset = 0U;
    uint32_t size = 0U;
    uint32_t align = 0U;
    LLVMValueRef info;
    LLVMValueRef move = NULL;
    LLVMValueRef drop = NULL;

    if (!r_llvm_runtime_layout(emitter, "RRuntimeTypeInfo", &info_size, &info_align) ||
        !r_llvm_runtime_field(emitter, "RRuntimeTypeInfo", "size", &size_offset, NULL) ||
        !r_llvm_runtime_field(emitter, "RRuntimeTypeInfo", "alignment", &align_offset, NULL) ||
        !r_llvm_runtime_field(emitter, "RRuntimeTypeInfo", "move_initialize", &move_offset, NULL) ||
        !r_llvm_runtime_field(emitter, "RRuntimeTypeInfo", "drop", &drop_offset, NULL) ||
        !r_llvm_layout(emitter, type, &size, &align)) {
        return NULL;
    }
    if (r_llvm_type_requires_drop(emitter, type)) {
        move = r_llvm_glue_function(emitter, type, R_LLVM_GLUE_MOVE);
        drop = r_llvm_glue_function(emitter, type, R_LLVM_GLUE_DROP);
        if ((move == NULL) || (drop == NULL)) {
            return NULL;
        }
    }
    info = r_llvm_entry_alloca(emitter, info_size, info_align, "type_info");
    (void)LLVMBuildMemSet(emitter->builder,
                          info,
                          LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                          r_llvm_u64(emitter, info_size),
                          info_align);
    (void)LLVMBuildStore(emitter->builder,
                         r_llvm_u64(emitter, size),
                         r_llvm_byte_offset(emitter, info, size_offset));
    (void)LLVMBuildStore(emitter->builder,
                         r_llvm_u64(emitter, align),
                         r_llvm_byte_offset(emitter, info, align_offset));
    (void)LLVMBuildStore(emitter->builder,
                         move == NULL ? LLVMConstNull(r_llvm_pointer(emitter)) : move,
                         r_llvm_byte_offset(emitter, info, move_offset));
    (void)LLVMBuildStore(emitter->builder,
                         drop == NULL ? LLVMConstNull(r_llvm_pointer(emitter)) : drop,
                         r_llvm_byte_offset(emitter, info, drop_offset));
    return info;
}

bool r_llvm_emit_pending_glue(RLlvmEmitter *emitter) {
    size_t index;

    /* The queue grows while bodies are written. */
    for (index = 0U; index < emitter->glue_count; ++index) {
        const RTypeId type = emitter->glue[index].type;
        const unsigned kind = emitter->glue[index].kind;
        LLVMBasicBlockRef entry;
        LLVMBasicBlockRef body;

        unsigned pass;

        emitter->mir = NULL;
        /* A cursor entry writes the member count and then the cursor. */
        for (pass = 0U; pass < (kind == R_LLVM_GLUE_CURSOR ? 2U : 1U); ++pass) {
            switch (kind) {
            case R_LLVM_GLUE_DROP:
                emitter->function = emitter->drop_glue[type];
                break;
            case R_LLVM_GLUE_MOVE:
                emitter->function = emitter->move_glue[type];
                break;
            case R_LLVM_GLUE_HOOK:
                emitter->function = emitter->drop_hooks[type];
                break;
            case R_LLVM_GLUE_KEY_HASH:
                emitter->function = emitter->key_hashes[type];
                break;
            case R_LLVM_GLUE_KEY_EQUAL:
                emitter->function = emitter->key_equals[type];
                break;
            default:
                emitter->function =
                    pass == 0U ? emitter->drop_counts[type] : emitter->drop_cursors[type];
                break;
            }
            entry = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "entry");
            body = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
            LLVMPositionBuilderAtEnd(emitter->builder, entry);
            (void)LLVMBuildBr(emitter->builder, body);
            LLVMPositionBuilderAtEnd(emitter->builder, body);
            if ((kind == R_LLVM_GLUE_KEY_HASH) || (kind == R_LLVM_GLUE_KEY_EQUAL)) {
                if (!r_llvm_key_body(emitter, type, kind == R_LLVM_GLUE_KEY_HASH)) {
                    return false;
                }
                continue;
            }
            if (kind == R_LLVM_GLUE_CURSOR) {
                if (!r_llvm_drop_walk_body(emitter, type, pass == 0U)) {
                    return false;
                }
                continue;
            }
            if (kind == R_LLVM_GLUE_HOOK) {
                const RSemanticAggregate *aggregate = r_llvm_aggregate(emitter, type);
                if ((aggregate == NULL) || !r_llvm_user_drop(emitter,
                                                             aggregate->drop_function,
                                                             LLVMGetParam(emitter->function, 0U))) {
                    return false;
                }
            } else if (!r_llvm_glue_body(emitter, type, kind)) {
                return false;
            }
            (void)LLVMBuildRetVoid(emitter->builder);
        }
    }
    emitter->function = NULL;
    return true;
}

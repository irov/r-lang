#include "emit_internal.h"
#include "standard_internal.h"

#include <llvm-c/Core.h>

#include <inttypes.h>
#include <stdio.h>

/* Typed std.dict paths (B7; the per-key-type helpers of the C17 emitter), an optimization under
   Core R-AM-0003.

   The runtime hashes and compares keys through the key information of a dictionary
   (RRuntimeDictKeyInfo), which the optimizer cannot follow once a call has received the
   dictionary. The lookups of std.dict and an insertion that needs no growth therefore probe the
   table of RRuntimeDict (runtime/include/r_runtime_dict.h) in the program, calling the key
   functions of the key type directly, so that the optimizer inlines the hash and the comparison
   into the probe and the probe into its caller. The probe, the mix of a hash with the seed and
   the insertion are those of runtime/source/dict.c; growth, removal and iteration stay in the
   runtime, which finds what these paths insert and the other way round. */

static LLVMValueRef
r_llvm_dict_field(RLlvmEmitter *emitter, LLVMValueRef base, const char *type, const char *field) {
    uint32_t offset = 0U;

    if (!r_llvm_runtime_field(emitter, type, field, &offset, NULL)) {
        return NULL;
    }
    return r_llvm_byte_offset(emitter, base, offset);
}

static LLVMValueRef
r_llvm_dict_load(RLlvmEmitter *emitter, LLVMValueRef dict, const char *field, bool pointer) {
    LLVMValueRef address = r_llvm_dict_field(emitter, dict, "RRuntimeDict", field);

    return address == NULL
               ? NULL
               : LLVMBuildLoad2(emitter->builder,
                                pointer ? r_llvm_pointer(emitter) : r_llvm_int(emitter, 64U),
                                address,
                                "");
}

/* The record at an index of the slots or the entries, whose records are `size` bytes; the index
   lies below the count of records, so neither the product nor the address wraps. */
static LLVMValueRef r_llvm_dict_record(RLlvmEmitter *emitter,
                                       LLVMValueRef table,
                                       LLVMValueRef index,
                                       LLVMValueRef size) {
    LLVMValueRef offset = LLVMBuildNUWMul(emitter->builder, index, size, "");

    return LLVMBuildGEPWithNoWrapFlags(emitter->builder,
                                       r_llvm_int(emitter, 8U),
                                       table,
                                       &offset,
                                       1U,
                                       "",
                                       LLVMGEPFlagInBounds | LLVMGEPFlagNUW);
}

/* The member at a dynamic offset of an entry (its key or its value). */
static LLVMValueRef
r_llvm_dict_member(RLlvmEmitter *emitter, LLVMValueRef entry, LLVMValueRef offset) {
    return LLVMBuildGEPWithNoWrapFlags(emitter->builder,
                                       r_llvm_int(emitter, 8U),
                                       entry,
                                       &offset,
                                       1U,
                                       "",
                                       LLVMGEPFlagInBounds | LLVMGEPFlagNUW);
}

/* r_runtime_dict_mix: the hash of a key mixed with the seed of the dictionary. */
static LLVMValueRef r_llvm_dict_mix(RLlvmEmitter *emitter, LLVMValueRef hash, LLVMValueRef seed) {
    LLVMBuilderRef builder = emitter->builder;
    LLVMValueRef mixed = LLVMBuildXor(builder,
                                      LLVMBuildXor(builder, hash, seed, ""),
                                      r_llvm_u64(emitter, UINT64_C(0x9e3779b97f4a7c15)),
                                      "");

    mixed = LLVMBuildXor(
        builder, mixed, LLVMBuildLShr(builder, mixed, r_llvm_u64(emitter, 30U), ""), "");
    mixed = LLVMBuildMul(builder, mixed, r_llvm_u64(emitter, UINT64_C(0xbf58476d1ce4e5b9)), "");
    mixed = LLVMBuildXor(
        builder, mixed, LLVMBuildLShr(builder, mixed, r_llvm_u64(emitter, 27U), ""), "");
    mixed = LLVMBuildMul(builder, mixed, r_llvm_u64(emitter, UINT64_C(0x94d049bb133111eb)), "");
    return LLVMBuildXor(
        builder, mixed, LLVMBuildLShr(builder, mixed, r_llvm_u64(emitter, 31U), ""), "");
}

/* The mixed hash of a key in a dictionary. */
static LLVMValueRef
r_llvm_dict_hash(RLlvmEmitter *emitter, RTypeId key, LLVMValueRef dict, LLVMValueRef value) {
    LLVMValueRef hash = r_llvm_key_function(emitter, key, true);
    LLVMValueRef seed = r_llvm_dict_load(emitter, dict, "seed", false);

    if ((hash == NULL) || (seed == NULL)) {
        return NULL;
    }
    return r_llvm_dict_mix(
        emitter,
        LLVMBuildCall2(emitter->builder, LLVMGlobalGetValueType(hash), hash, &value, 1U, ""),
        seed);
}

/* A helper function of a key type, `r_dict_<kind>.<key>`, internal to the program. */
static LLVMValueRef
r_llvm_dict_helper(RLlvmEmitter *emitter, RTypeId value_id, const char *kind, LLVMTypeRef type) {
    char name[64];
    LLVMValueRef function;

    (void)snprintf(
        name, sizeof(name), "r_dict_%s.%" PRIu32, kind, r_llvm_type_key(emitter, value_id));
    function = LLVMAddFunction(emitter->module, name, type);
    LLVMSetLinkage(function, LLVMInternalLinkage);
    return function;
}

/* r_runtime_dict_probe: `{index, found}` of a mixed hash, where index is the slot of the key
   when found, otherwise the first tombstone or the empty slot that ended the probe. */
static bool r_llvm_dict_build_probe(RLlvmEmitter *emitter, LLVMValueRef function, RTypeId key) {
    LLVMContextRef context = emitter->context;
    LLVMTypeRef wide = r_llvm_int(emitter, 64U);
    LLVMTypeRef result = LLVMGetReturnType(LLVMGlobalGetValueType(function));
    LLVMValueRef dict = LLVMGetParam(function, 0U);
    LLVMValueRef wanted = LLVMGetParam(function, 1U);
    LLVMValueRef hash = LLVMGetParam(function, 2U);
    LLVMValueRef equal = r_llvm_key_function(emitter, key, false);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(context, function, "entry");
    LLVMBasicBlockRef loop = LLVMAppendBasicBlockInContext(context, function, "loop");
    LLVMBasicBlockRef body = LLVMAppendBasicBlockInContext(context, function, "body");
    LLVMBasicBlockRef occupied = LLVMAppendBasicBlockInContext(context, function, "occupied");
    LLVMBasicBlockRef empty = LLVMAppendBasicBlockInContext(context, function, "empty");
    LLVMBasicBlockRef tombstone = LLVMAppendBasicBlockInContext(context, function, "tombstone");
    LLVMBasicBlockRef live = LLVMAppendBasicBlockInContext(context, function, "live");
    LLVMBasicBlockRef compare = LLVMAppendBasicBlockInContext(context, function, "compare");
    LLVMBasicBlockRef found = LLVMAppendBasicBlockInContext(context, function, "found");
    LLVMBasicBlockRef advance = LLVMAppendBasicBlockInContext(context, function, "advance");
    LLVMBasicBlockRef exhausted = LLVMAppendBasicBlockInContext(context, function, "exhausted");
    LLVMValueRef capacity;
    LLVMValueRef mask;
    LLVMValueRef slots;
    LLVMValueRef slot_size;
    LLVMValueRef entries;
    LLVMValueRef entry_size;
    LLVMValueRef key_offset;
    LLVMValueRef start;
    LLVMValueRef index;
    LLVMValueRef first;
    LLVMValueRef visited;
    LLVMValueRef slot;
    LLVMValueRef state;
    LLVMValueRef state_address;
    LLVMValueRef none;
    LLVMValueRef marked;
    LLVMValueRef stored;
    LLVMValueRef entry_index;
    LLVMValueRef header;
    LLVMValueRef same;
    LLVMValueRef arguments[2];
    LLVMValueRef next_first;
    LLVMValueRef incoming[3];
    LLVMBasicBlockRef from[3];
    int64_t empty_state = 0;
    int64_t tombstone_state = 0;

    if ((equal == NULL) ||
        !r_llvm_runtime_constant(emitter, "R_RUNTIME_DICT_SLOT_EMPTY", &empty_state) ||
        !r_llvm_runtime_constant(emitter, "R_RUNTIME_DICT_SLOT_TOMBSTONE", &tombstone_state)) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    LLVMPositionBuilderAtEnd(emitter->builder, entry);
    capacity = r_llvm_dict_load(emitter, dict, "capacity", false);
    slots = r_llvm_dict_load(emitter, dict, "slots", true);
    slot_size = r_llvm_dict_load(emitter, dict, "slot_size", false);
    entries = r_llvm_dict_load(emitter, dict, "entries", true);
    entry_size = r_llvm_dict_load(emitter, dict, "entry_size", false);
    key_offset = r_llvm_dict_load(emitter, dict, "key_offset", false);
    if ((capacity == NULL) || (slots == NULL) || (slot_size == NULL) || (entries == NULL) ||
        (entry_size == NULL) || (key_offset == NULL)) {
        return false;
    }
    mask = LLVMBuildSub(emitter->builder, capacity, r_llvm_u64(emitter, 1U), "");
    start = LLVMBuildAnd(emitter->builder, hash, mask, "");
    none = LLVMConstAllOnes(wide);
    (void)LLVMBuildBr(emitter->builder, loop);

    LLVMPositionBuilderAtEnd(emitter->builder, loop);
    index = LLVMBuildPhi(emitter->builder, wide, "index");
    first = LLVMBuildPhi(emitter->builder, wide, "first");
    visited = LLVMBuildPhi(emitter->builder, wide, "visited");
    (void)LLVMBuildCondBr(emitter->builder,
                          LLVMBuildICmp(emitter->builder, LLVMIntULT, visited, capacity, ""),
                          body,
                          exhausted);

    LLVMPositionBuilderAtEnd(emitter->builder, body);
    slot = r_llvm_dict_record(emitter, slots, index, slot_size);
    state_address = r_llvm_dict_field(emitter, slot, "RRuntimeDictIndexSlot", "state");
    if (state_address == NULL) {
        return false;
    }
    state = LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), state_address, "");
    (void)LLVMBuildCondBr(
        emitter->builder,
        LLVMBuildICmp(
            emitter->builder, LLVMIntEQ, state, r_llvm_u32(emitter, (uint64_t)empty_state), ""),
        empty,
        occupied);

    LLVMPositionBuilderAtEnd(emitter->builder, occupied);
    (void)LLVMBuildCondBr(
        emitter->builder,
        LLVMBuildICmp(
            emitter->builder, LLVMIntEQ, state, r_llvm_u32(emitter, (uint64_t)tombstone_state), ""),
        tombstone,
        live);

    /* The key is absent: a new entry takes the first tombstone met, or this slot. */
    LLVMPositionBuilderAtEnd(emitter->builder, empty);
    (void)LLVMBuildRet(
        emitter->builder,
        LLVMBuildInsertValue(
            emitter->builder,
            LLVMBuildInsertValue(
                emitter->builder,
                LLVMGetPoison(result),
                LLVMBuildSelect(emitter->builder,
                                LLVMBuildICmp(emitter->builder, LLVMIntEQ, first, none, ""),
                                index,
                                first,
                                ""),
                0U,
                ""),
            LLVMConstInt(r_llvm_int(emitter, 1U), 0U, 0),
            1U,
            ""));

    LLVMPositionBuilderAtEnd(emitter->builder, tombstone);
    marked = LLVMBuildSelect(emitter->builder,
                             LLVMBuildICmp(emitter->builder, LLVMIntEQ, first, none, ""),
                             index,
                             first,
                             "");
    (void)LLVMBuildBr(emitter->builder, advance);

    LLVMPositionBuilderAtEnd(emitter->builder, live);
    stored = r_llvm_dict_field(emitter, slot, "RRuntimeDictIndexSlot", "entry_index");
    if (stored == NULL) {
        return false;
    }
    entry_index = LLVMBuildLoad2(emitter->builder, wide, stored, "");
    header = r_llvm_dict_record(emitter, entries, entry_index, entry_size);
    stored = r_llvm_dict_field(emitter, header, "RRuntimeDictEntryHeader", "hash");
    if (stored == NULL) {
        return false;
    }
    same = LLVMBuildICmp(
        emitter->builder, LLVMIntEQ, LLVMBuildLoad2(emitter->builder, wide, stored, ""), hash, "");
    (void)LLVMBuildCondBr(emitter->builder, same, compare, advance);

    LLVMPositionBuilderAtEnd(emitter->builder, compare);
    arguments[0] = r_llvm_dict_member(emitter, header, key_offset);
    arguments[1] = wanted;
    (void)LLVMBuildCondBr(
        emitter->builder,
        LLVMBuildCall2(emitter->builder, LLVMGlobalGetValueType(equal), equal, arguments, 2U, ""),
        found,
        advance);

    LLVMPositionBuilderAtEnd(emitter->builder, found);
    (void)LLVMBuildRet(
        emitter->builder,
        LLVMBuildInsertValue(
            emitter->builder,
            LLVMBuildInsertValue(emitter->builder, LLVMGetPoison(result), index, 0U, ""),
            LLVMConstInt(r_llvm_int(emitter, 1U), 1U, 0),
            1U,
            ""));

    LLVMPositionBuilderAtEnd(emitter->builder, advance);
    next_first = LLVMBuildPhi(emitter->builder, wide, "");
    incoming[0] = marked;
    incoming[1] = first;
    incoming[2] = first;
    from[0] = tombstone;
    from[1] = live;
    from[2] = compare;
    LLVMAddIncoming(next_first, incoming, from, 3U);
    {
        LLVMValueRef next_index =
            LLVMBuildAnd(emitter->builder,
                         LLVMBuildAdd(emitter->builder, index, r_llvm_u64(emitter, 1U), ""),
                         mask,
                         "");
        LLVMValueRef next_visited =
            LLVMBuildNUWAdd(emitter->builder, visited, r_llvm_u64(emitter, 1U), "");
        (void)LLVMBuildBr(emitter->builder, loop);
        incoming[0] = start;
        incoming[1] = next_index;
        from[0] = entry;
        from[1] = advance;
        LLVMAddIncoming(index, incoming, from, 2U);
        incoming[0] = none;
        incoming[1] = next_first;
        LLVMAddIncoming(first, incoming, from, 2U);
        incoming[0] = r_llvm_u64(emitter, 0U);
        incoming[1] = next_visited;
        LLVMAddIncoming(visited, incoming, from, 2U);
    }

    /* Every slot was visited (r_runtime_dict_probe returns the first tombstone). */
    LLVMPositionBuilderAtEnd(emitter->builder, exhausted);
    (void)LLVMBuildRet(
        emitter->builder,
        LLVMBuildInsertValue(
            emitter->builder,
            LLVMBuildInsertValue(emitter->builder, LLVMGetPoison(result), first, 0U, ""),
            LLVMConstInt(r_llvm_int(emitter, 1U), 0U, 0),
            1U,
            ""));
    return true;
}

/* The probe of a key type: `{i64, i1} r_dict_probe.K(ptr dict, ptr key, i64 hash)`. */
static LLVMValueRef r_llvm_dict_probe(RLlvmEmitter *emitter, RTypeId key) {
    const RTypeId value_id = r_llvm_value_type(emitter, key);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(emitter->builder);
    LLVMTypeRef parameters[3];
    LLVMTypeRef members[2];
    LLVMValueRef function;
    bool built;

    if ((value_id == R_TYPE_ID_INVALID) ||
        ((size_t)value_id > emitter->frontend->semantic_type_count)) {
        (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        return NULL;
    }
    if (emitter->dict_probes[value_id] != NULL) {
        return emitter->dict_probes[value_id];
    }
    parameters[0] = r_llvm_pointer(emitter);
    parameters[1] = r_llvm_pointer(emitter);
    parameters[2] = r_llvm_int(emitter, 64U);
    members[0] = r_llvm_int(emitter, 64U);
    members[1] = r_llvm_int(emitter, 1U);
    function = r_llvm_dict_helper(
        emitter,
        value_id,
        "probe",
        LLVMFunctionType(
            LLVMStructTypeInContext(emitter->context, members, 2U, 0), parameters, 3U, 0));
    emitter->dict_probes[value_id] = function;
    built = r_llvm_dict_build_probe(emitter, function, key);
    LLVMPositionBuilderAtEnd(emitter->builder, saved);
    return built ? function : NULL;
}

/* The slot and the entry of a probe's index. */
static LLVMValueRef r_llvm_dict_slot(RLlvmEmitter *emitter, LLVMValueRef dict, LLVMValueRef index) {
    LLVMValueRef slots = r_llvm_dict_load(emitter, dict, "slots", true);
    LLVMValueRef slot_size = r_llvm_dict_load(emitter, dict, "slot_size", false);

    return (slots == NULL) || (slot_size == NULL)
               ? NULL
               : r_llvm_dict_record(emitter, slots, index, slot_size);
}

static LLVMValueRef
r_llvm_dict_entry(RLlvmEmitter *emitter, LLVMValueRef dict, LLVMValueRef entry_index) {
    LLVMValueRef entries = r_llvm_dict_load(emitter, dict, "entries", true);
    LLVMValueRef entry_size = r_llvm_dict_load(emitter, dict, "entry_size", false);

    return (entries == NULL) || (entry_size == NULL)
               ? NULL
               : r_llvm_dict_record(emitter, entries, entry_index, entry_size);
}

/* r_runtime_dict_get: the value of a key, or null. */
static bool r_llvm_dict_build_lookup(RLlvmEmitter *emitter, LLVMValueRef function, RTypeId key) {
    LLVMContextRef context = emitter->context;
    LLVMValueRef dict = LLVMGetParam(function, 0U);
    LLVMValueRef wanted = LLVMGetParam(function, 1U);
    LLVMValueRef probe = r_llvm_dict_probe(emitter, key);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(context, function, "entry");
    LLVMBasicBlockRef search = LLVMAppendBasicBlockInContext(context, function, "search");
    LLVMBasicBlockRef some = LLVMAppendBasicBlockInContext(context, function, "some");
    LLVMBasicBlockRef none = LLVMAppendBasicBlockInContext(context, function, "none");
    LLVMValueRef capacity;
    LLVMValueRef arguments[3];
    LLVMValueRef outcome;
    LLVMValueRef slot;
    LLVMValueRef stored;
    LLVMValueRef header;
    LLVMValueRef value_offset;

    if (probe == NULL) {
        return false;
    }
    LLVMPositionBuilderAtEnd(emitter->builder, entry);
    capacity = r_llvm_dict_load(emitter, dict, "capacity", false);
    if (capacity == NULL) {
        return false;
    }
    (void)LLVMBuildCondBr(
        emitter->builder,
        LLVMBuildICmp(emitter->builder, LLVMIntEQ, capacity, r_llvm_u64(emitter, 0U), ""),
        none,
        search);

    LLVMPositionBuilderAtEnd(emitter->builder, search);
    arguments[0] = dict;
    arguments[1] = wanted;
    arguments[2] = r_llvm_dict_hash(emitter, key, dict, wanted);
    if (arguments[2] == NULL) {
        return false;
    }
    outcome =
        LLVMBuildCall2(emitter->builder, LLVMGlobalGetValueType(probe), probe, arguments, 3U, "");
    (void)LLVMBuildCondBr(
        emitter->builder, LLVMBuildExtractValue(emitter->builder, outcome, 1U, ""), some, none);

    LLVMPositionBuilderAtEnd(emitter->builder, some);
    slot =
        r_llvm_dict_slot(emitter, dict, LLVMBuildExtractValue(emitter->builder, outcome, 0U, ""));
    stored = slot == NULL
                 ? NULL
                 : r_llvm_dict_field(emitter, slot, "RRuntimeDictIndexSlot", "entry_index");
    header = stored == NULL
                 ? NULL
                 : r_llvm_dict_entry(
                       emitter,
                       dict,
                       LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 64U), stored, ""));
    value_offset = r_llvm_dict_load(emitter, dict, "value_offset", false);
    if ((header == NULL) || (value_offset == NULL)) {
        return false;
    }
    (void)LLVMBuildRet(emitter->builder, r_llvm_dict_member(emitter, header, value_offset));

    LLVMPositionBuilderAtEnd(emitter->builder, none);
    (void)LLVMBuildRet(emitter->builder, LLVMConstNull(r_llvm_pointer(emitter)));
    return true;
}

LLVMValueRef r_llvm_dict_lookup(RLlvmEmitter *emitter, RTypeId key) {
    const RTypeId value_id = r_llvm_value_type(emitter, key);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(emitter->builder);
    LLVMTypeRef parameters[2];
    LLVMValueRef function;
    bool built;

    if ((value_id == R_TYPE_ID_INVALID) ||
        ((size_t)value_id > emitter->frontend->semantic_type_count)) {
        (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        return NULL;
    }
    if (emitter->dict_lookups[value_id] != NULL) {
        return emitter->dict_lookups[value_id];
    }
    parameters[0] = r_llvm_pointer(emitter);
    parameters[1] = r_llvm_pointer(emitter);
    function = r_llvm_dict_helper(
        emitter, value_id, "lookup", LLVMFunctionType(r_llvm_pointer(emitter), parameters, 2U, 0));
    emitter->dict_lookups[value_id] = function;
    built = r_llvm_dict_build_lookup(emitter, function, key);
    LLVMPositionBuilderAtEnd(emitter->builder, saved);
    return built ? function : NULL;
}

/* A move between storage of a type, as r_runtime_dict_move does: by the move glue its type
   information names, or by a copy. */
static bool r_llvm_dict_move(RLlvmEmitter *emitter,
                             RTypeId type,
                             LLVMValueRef destination,
                             LLVMValueRef source) {
    uint32_t size = 0U;
    uint32_t align = 0U;

    if (r_llvm_type_requires_drop(emitter, type)) {
        return r_llvm_call_move(emitter, type, destination, source);
    }
    if (!r_llvm_layout(emitter, type, &size, &align)) {
        return false;
    }
    if (size != 0U) {
        (void)LLVMBuildMemCpy(
            emitter->builder, destination, 1U, source, 1U, r_llvm_u64(emitter, size));
    }
    return true;
}

bool r_llvm_dict_insert_now(RLlvmEmitter *emitter,
                            RTypeId key,
                            RTypeId value,
                            LLVMValueRef dict,
                            LLVMValueRef staged_key,
                            LLVMValueRef staged_value,
                            LLVMValueRef replaced,
                            LLVMValueRef native,
                            LLVMBasicBlockRef join) {
    LLVMContextRef context = emitter->context;
    LLVMTypeRef wide = r_llvm_int(emitter, 64U);
    LLVMValueRef probe = r_llvm_dict_probe(emitter, key);
    LLVMBasicBlockRef search = LLVMAppendBasicBlockInContext(context, emitter->function, "");
    LLVMBasicBlockRef replace = LLVMAppendBasicBlockInContext(context, emitter->function, "");
    LLVMBasicBlockRef room = LLVMAppendBasicBlockInContext(context, emitter->function, "");
    LLVMBasicBlockRef append = LLVMAppendBasicBlockInContext(context, emitter->function, "");
    LLVMBasicBlockRef slow = LLVMAppendBasicBlockInContext(context, emitter->function, "");
    LLVMValueRef capacity;
    LLVMValueRef arguments[3];
    LLVMValueRef outcome;
    LLVMValueRef slot;
    LLVMValueRef stored;
    LLVMValueRef header;
    LLVMValueRef key_offset;
    LLVMValueRef value_offset;
    LLVMValueRef length_place;
    LLVMValueRef tombstones_place;
    LLVMValueRef length;
    LLVMValueRef tombstones;
    LLVMValueRef state_place;
    int64_t success = 0;
    int64_t tombstone_state = 0;
    int64_t live_state = 0;

    if ((probe == NULL) || !r_llvm_runtime_constant(emitter, "R_STD_DICT_CALL_SUCCESS", &success) ||
        !r_llvm_runtime_constant(emitter, "R_RUNTIME_DICT_SLOT_TOMBSTONE", &tombstone_state) ||
        !r_llvm_runtime_constant(emitter, "R_RUNTIME_DICT_SLOT_LIVE", &live_state)) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    capacity = r_llvm_dict_load(emitter, dict, "capacity", false);
    if (capacity == NULL) {
        return false;
    }
    /* An empty table grows first (the runtime reserves before it probes). */
    (void)LLVMBuildCondBr(
        emitter->builder,
        LLVMBuildICmp(emitter->builder, LLVMIntEQ, capacity, r_llvm_u64(emitter, 0U), ""),
        slow,
        search);

    LLVMPositionBuilderAtEnd(emitter->builder, search);
    arguments[0] = dict;
    arguments[1] = staged_key;
    arguments[2] = r_llvm_dict_hash(emitter, key, dict, staged_key);
    if (arguments[2] == NULL) {
        return false;
    }
    outcome =
        LLVMBuildCall2(emitter->builder, LLVMGlobalGetValueType(probe), probe, arguments, 3U, "");
    slot =
        r_llvm_dict_slot(emitter, dict, LLVMBuildExtractValue(emitter->builder, outcome, 0U, ""));
    value_offset = r_llvm_dict_load(emitter, dict, "value_offset", false);
    if ((slot == NULL) || (value_offset == NULL)) {
        return false;
    }
    (void)LLVMBuildCondBr(
        emitter->builder, LLVMBuildExtractValue(emitter->builder, outcome, 1U, ""), replace, room);

    /* The key is present: the old value moves out into the option of the result, the staged key
       is dropped and the staged value takes the place of the old one. */
    LLVMPositionBuilderAtEnd(emitter->builder, replace);
    stored = r_llvm_dict_field(emitter, slot, "RRuntimeDictIndexSlot", "entry_index");
    header =
        stored == NULL
            ? NULL
            : r_llvm_dict_entry(emitter, dict, LLVMBuildLoad2(emitter->builder, wide, stored, ""));
    if (header == NULL) {
        return false;
    }
    stored = r_llvm_dict_member(emitter, header, value_offset);
    if (!r_llvm_dict_move(emitter, value, replaced, stored) ||
        (r_llvm_type_requires_drop(emitter, key) && !r_llvm_call_drop(emitter, key, staged_key)) ||
        !r_llvm_dict_move(emitter, value, stored, staged_value)) {
        return false;
    }
    (void)LLVMBuildStore(emitter->builder,
                         r_llvm_u32(emitter, (uint64_t)success),
                         r_llvm_std_member(emitter, native, "RStdDictInsertResult", "status"));
    (void)LLVMBuildStore(emitter->builder,
                         LLVMConstInt(r_llvm_int(emitter, 8U), 1U, 0),
                         r_llvm_std_member(emitter, native, "RStdDictInsertResult", "did_replace"));
    (void)LLVMBuildBr(emitter->builder, join);

    /* The key is absent: a new entry needs a free entry; otherwise the runtime grows the table. */
    LLVMPositionBuilderAtEnd(emitter->builder, room);
    length_place = r_llvm_dict_field(emitter, dict, "RRuntimeDict", "length");
    tombstones_place = r_llvm_dict_field(emitter, dict, "RRuntimeDict", "tombstones");
    if ((length_place == NULL) || (tombstones_place == NULL)) {
        return false;
    }
    length = LLVMBuildLoad2(emitter->builder, wide, length_place, "");
    tombstones = LLVMBuildLoad2(emitter->builder, wide, tombstones_place, "");
    {
        LLVMValueRef entry_capacity = r_llvm_dict_load(emitter, dict, "entry_capacity", false);
        if (entry_capacity == NULL) {
            return false;
        }
        (void)LLVMBuildCondBr(emitter->builder,
                              LLVMBuildICmp(emitter->builder,
                                            LLVMIntULT,
                                            LLVMBuildAdd(emitter->builder, length, tombstones, ""),
                                            entry_capacity,
                                            ""),
                              append,
                              slow);
    }

    LLVMPositionBuilderAtEnd(emitter->builder, append);
    state_place = r_llvm_dict_field(emitter, slot, "RRuntimeDictIndexSlot", "state");
    stored = r_llvm_dict_field(emitter, slot, "RRuntimeDictIndexSlot", "entry_index");
    header = r_llvm_dict_entry(emitter, dict, length);
    key_offset = r_llvm_dict_load(emitter, dict, "key_offset", false);
    if ((state_place == NULL) || (stored == NULL) || (header == NULL) || (key_offset == NULL)) {
        return false;
    }
    /* A reused tombstone is no longer one. */
    (void)LLVMBuildStore(
        emitter->builder,
        LLVMBuildNUWSub(
            emitter->builder,
            tombstones,
            LLVMBuildZExt(
                emitter->builder,
                LLVMBuildICmp(
                    emitter->builder,
                    LLVMIntEQ,
                    LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), state_place, ""),
                    r_llvm_u32(emitter, (uint64_t)tombstone_state),
                    ""),
                wide,
                ""),
            ""),
        tombstones_place);
    {
        LLVMValueRef hash_place =
            r_llvm_dict_field(emitter, header, "RRuntimeDictEntryHeader", "hash");
        LLVMValueRef first_order = r_llvm_dict_field(emitter, dict, "RRuntimeDict", "first_order");
        LLVMValueRef last_order = r_llvm_dict_field(emitter, dict, "RRuntimeDict", "last_order");
        if ((hash_place == NULL) || (first_order == NULL) || (last_order == NULL)) {
            return false;
        }
        (void)LLVMBuildStore(emitter->builder, arguments[2], hash_place);
        if (!r_llvm_dict_move(
                emitter, key, r_llvm_dict_member(emitter, header, key_offset), staged_key) ||
            !r_llvm_dict_move(
                emitter, value, r_llvm_dict_member(emitter, header, value_offset), staged_value)) {
            return false;
        }
        (void)LLVMBuildStore(
            emitter->builder, r_llvm_u32(emitter, (uint64_t)live_state), state_place);
        (void)LLVMBuildStore(emitter->builder, length, stored);
        (void)LLVMBuildStore(emitter->builder,
                             LLVMBuildNUWAdd(emitter->builder, length, r_llvm_u64(emitter, 1U), ""),
                             length_place);
        (void)LLVMBuildStore(emitter->builder, r_llvm_u64(emitter, 0U), first_order);
        (void)LLVMBuildStore(emitter->builder, length, last_order);
    }
    (void)LLVMBuildStore(emitter->builder,
                         r_llvm_u32(emitter, (uint64_t)success),
                         r_llvm_std_member(emitter, native, "RStdDictInsertResult", "status"));
    (void)LLVMBuildStore(emitter->builder,
                         LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                         r_llvm_std_member(emitter, native, "RStdDictInsertResult", "did_replace"));
    (void)LLVMBuildBr(emitter->builder, join);

    LLVMPositionBuilderAtEnd(emitter->builder, slow);
    return true;
}

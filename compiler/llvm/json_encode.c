#include "standard_internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* JSON encoders (R-SLIB-JSON-*, the C17 emitter's json_encode.inc). std.json::marshal builds a
   std.json::value tree of a concrete R value through one internal function per encoded type,

       void r_json_encode.<type>(ptr result, ptr context, ptr value, i1 quoted)

   which writes an RStdJsonValueResult: a type with a marshal hook calls it, std.json values are
   cloned, collections and structs nest one level deeper under the depth limit of the context's
   options, enums become their variant name (an object {name: payload} for a payload), options
   null or their payload, scalars the library's number, string and boolean values. omitzero asks
   the zero predicate of a field type, `i1 r_json_zero.<type>(ptr value)`. Encoders borrow the
   value; every member they build is destroyed once the tree owns its copy. The functions depend
   only on the type, so one per type serves every operation of the program (C17 writes one per
   operation and type). */

static LLVMValueRef r_llvm_json_encoder(RLlvmEmitter *emitter, RTypeId type);
static LLVMValueRef r_llvm_json_zero(RLlvmEmitter *emitter, RTypeId type);

/* ---- small builders ---- */

static LLVMBasicBlockRef r_llvm_json_block(RLlvmEmitter *emitter) {
    return LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
}

static bool r_llvm_json_constant(RLlvmEmitter *emitter, const char *name, uint64_t *value) {
    int64_t found = 0;
    if (!r_llvm_runtime_constant(emitter, name, &found)) {
        return false;
    }
    *value = (uint64_t)found;
    return true;
}

/* outcome.status == R_STD_JSON_CALL_SUCCESS of an RStdJsonValueResult or RStdJsonResult. */
static LLVMValueRef r_llvm_json_ok(RLlvmEmitter *emitter, LLVMValueRef result, bool value_result) {
    LLVMValueRef outcome =
        value_result ? r_llvm_std_member(emitter, result, "RStdJsonValueResult", "outcome")
                     : result;
    return r_llvm_std_status_is(
        emitter, outcome, "RStdJsonResult", "status", "R_STD_JSON_CALL_SUCCESS");
}

static bool r_llvm_json_destroy(RLlvmEmitter *emitter, LLVMValueRef value) {
    return r_llvm_call_runtime(emitter, "r_std_json_value_destroy", &value, 1U, NULL) != NULL;
}

static LLVMValueRef r_llvm_json_value_of(RLlvmEmitter *emitter, LLVMValueRef result) {
    return r_llvm_std_member(emitter, result, "RStdJsonValueResult", "value");
}

/* Copies an RStdJsonValueResult and returns from the encoder. */
static bool r_llvm_json_return(RLlvmEmitter *emitter, LLVMValueRef from) {
    uint32_t size = 0U;
    uint32_t align = 0U;
    LLVMValueRef to = LLVMGetParam(emitter->function, 0U);

    if (!r_llvm_runtime_layout(emitter, "RStdJsonValueResult", &size, &align)) {
        return false;
    }
    if (from != to) {
        (void)LLVMBuildMemCpy(emitter->builder, to, align, from, align, r_llvm_u64(emitter, size));
    }
    (void)LLVMBuildRetVoid(emitter->builder);
    return true;
}

/* A zeroed RStdJsonValueResult, or an RStdJsonResult with `json_result`. */
static LLVMValueRef r_llvm_json_result(RLlvmEmitter *emitter, bool json_result) {
    return r_llvm_std_structure(emitter, json_result ? "RStdJsonResult" : "RStdJsonValueResult");
}

/* Writes a JSON error with `code` into an RStdJsonResult. */
static bool r_llvm_json_fail(RLlvmEmitter *emitter, LLVMValueRef outcome, const char *code) {
    uint64_t status = 0U;
    uint64_t value = 0U;
    LLVMValueRef error = r_llvm_std_member(emitter, outcome, "RStdJsonResult", "error");

    if (!r_llvm_json_constant(emitter, "R_STD_JSON_CALL_JSON_ERROR", &status) ||
        !r_llvm_json_constant(emitter, code, &value) || (error == NULL)) {
        return false;
    }
    (void)LLVMBuildStore(emitter->builder,
                         r_llvm_u32(emitter, status),
                         r_llvm_std_member(emitter, outcome, "RStdJsonResult", "status"));
    (void)LLVMBuildStore(emitter->builder,
                         r_llvm_u32(emitter, value),
                         r_llvm_std_member(emitter, error, "RStdJsonError", "code"));
    return true;
}

/* Calls `name` into a fresh RStdJsonValueResult. */
static LLVMValueRef r_llvm_json_value_call(RLlvmEmitter *emitter,
                                           const char *name,
                                           LLVMValueRef *arguments,
                                           unsigned count) {
    LLVMValueRef result = r_llvm_json_result(emitter, false);
    return (result != NULL) &&
                   (r_llvm_call_runtime(emitter, name, arguments, count, result) != NULL)
               ? result
               : NULL;
}

/* An RStdJsonByteView over program bytes. */
static LLVMValueRef r_llvm_json_bytes(RLlvmEmitter *emitter, uint32_t intern_id) {
    LLVMValueRef view = r_llvm_std_structure(emitter, "RStdJsonByteView");
    const RInternEntry *entry =
        ((intern_id == 0U) || ((size_t)intern_id > emitter->frontend->intern_count))
            ? NULL
            : &emitter->frontend->intern_entries[(size_t)intern_id - 1U];

    if ((view == NULL) || (entry == NULL)) {
        return (entry == NULL) ? (r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR), NULL) : NULL;
    }
    (void)LLVMBuildStore(emitter->builder,
                         entry->length == 0U ? r_llvm_empty_string(emitter)
                                             : r_llvm_program_string(emitter, intern_id),
                         r_llvm_std_member(emitter, view, "RStdJsonByteView", "data"));
    (void)LLVMBuildStore(emitter->builder,
                         r_llvm_u64(emitter, entry->length),
                         r_llvm_std_member(emitter, view, "RStdJsonByteView", "length"));
    return view;
}

/* The JSON key of a field: its json name or its own name. */
uint32_t r_llvm_json_field_key(const RSemanticField *field) {
    return (field->json.flags & R_JSON_FIELD_NAME) != 0U ? field->json.name_intern_id
                                                         : field->name_intern_id;
}

/* The aggregate of a struct or enum type, or the protected declaration of a named standard type
   (r_c17_aggregate_for_type). */
const RSemanticAggregate *r_llvm_json_aggregate(RLlvmEmitter *emitter, RTypeId id) {
    const RTypeId value = r_llvm_value_type(emitter, id);
    const RSemanticType *type = r_llvm_type(emitter, value);
    size_t index;

    if ((type == NULL) || (type->kind != R_SEMANTIC_TYPE_STANDARD)) {
        return r_llvm_aggregate(emitter, id);
    }
    for (index = 0U; index < emitter->frontend->semantic_aggregate_count; ++index) {
        const RSemanticAggregate *candidate = &emitter->frontend->semantic_aggregates[index];
        if ((candidate->type == value) && (candidate->module_source == R_SOURCE_ID_INVALID) &&
            candidate->is_protected && candidate->complete && !candidate->poisoned) {
            return candidate;
        }
    }
    return NULL;
}

/* Calls the encoder of `type`: a fresh RStdJsonValueResult. */
static LLVMValueRef r_llvm_json_encode_call(RLlvmEmitter *emitter,
                                            RTypeId type,
                                            LLVMValueRef context,
                                            LLVMValueRef value,
                                            bool quoted,
                                            LLVMValueRef quoted_value) {
    LLVMValueRef function = r_llvm_json_encoder(emitter, type);
    LLVMValueRef result = r_llvm_json_result(emitter, false);
    LLVMValueRef arguments[4];

    if ((function == NULL) || (result == NULL)) {
        return NULL;
    }
    arguments[0] = result;
    arguments[1] = context;
    arguments[2] = value;
    arguments[3] = quoted_value != NULL
                       ? quoted_value
                       : LLVMConstInt(r_llvm_int(emitter, 1U), quoted ? 1U : 0U, 0);
    (void)LLVMBuildCall2(
        emitter->builder, LLVMGlobalGetValueType(function), function, arguments, 4U, "");
    return result;
}

/* If `member` failed: destroy the result being built (when given) and return the member. */
static bool
r_llvm_json_member_check(RLlvmEmitter *emitter, LLVMValueRef member, LLVMValueRef built) {
    LLVMBasicBlockRef failed = r_llvm_json_block(emitter);
    LLVMBasicBlockRef next = r_llvm_json_block(emitter);

    (void)LLVMBuildCondBr(emitter->builder, r_llvm_json_ok(emitter, member, true), next, failed);
    LLVMPositionBuilderAtEnd(emitter->builder, failed);
    if (((built != NULL) && !r_llvm_json_destroy(emitter, r_llvm_json_value_of(emitter, built))) ||
        !r_llvm_json_return(emitter, member)) {
        return false;
    }
    LLVMPositionBuilderAtEnd(emitter->builder, next);
    return true;
}

/* After `built.outcome = <call>`: destroy the built value and return it when the call failed. */
static bool r_llvm_json_built_check(RLlvmEmitter *emitter, LLVMValueRef built) {
    LLVMBasicBlockRef failed = r_llvm_json_block(emitter);
    LLVMBasicBlockRef next = r_llvm_json_block(emitter);

    (void)LLVMBuildCondBr(emitter->builder, r_llvm_json_ok(emitter, built, true), next, failed);
    LLVMPositionBuilderAtEnd(emitter->builder, failed);
    if (!r_llvm_json_destroy(emitter, r_llvm_json_value_of(emitter, built)) ||
        !r_llvm_json_return(emitter, built)) {
        return false;
    }
    LLVMPositionBuilderAtEnd(emitter->builder, next);
    return true;
}

/* A new array or object result; returns it from the encoder when it failed. */
static LLVMValueRef
r_llvm_json_container(RLlvmEmitter *emitter, bool object, LLVMValueRef allocator) {
    LLVMValueRef built = r_llvm_json_value_call(
        emitter, object ? "r_std_json_object" : "r_std_json_array", &allocator, 1U);
    LLVMBasicBlockRef failed;
    LLVMBasicBlockRef next;

    if (built == NULL) {
        return NULL;
    }
    failed = r_llvm_json_block(emitter);
    next = r_llvm_json_block(emitter);
    (void)LLVMBuildCondBr(emitter->builder, r_llvm_json_ok(emitter, built, true), next, failed);
    LLVMPositionBuilderAtEnd(emitter->builder, failed);
    if (!r_llvm_json_return(emitter, built)) {
        return NULL;
    }
    LLVMPositionBuilderAtEnd(emitter->builder, next);
    return built;
}

/* built.outcome = append(built.value, member.value) or insert(..., key, ...); the member is
   destroyed either way, and a failure returns the destroyed built result. */
static bool
r_llvm_json_add(RLlvmEmitter *emitter, LLVMValueRef built, LLVMValueRef key, LLVMValueRef member) {
    LLVMValueRef arguments[3];
    unsigned count = 0U;

    arguments[count++] = r_llvm_json_value_of(emitter, built);
    if (key != NULL) {
        arguments[count++] = key;
    }
    arguments[count++] = r_llvm_json_value_of(emitter, member);
    if ((r_llvm_call_runtime(emitter,
                             key != NULL ? "r_std_json_insert" : "r_std_json_append",
                             arguments,
                             count,
                             r_llvm_std_member(emitter, built, "RStdJsonValueResult", "outcome")) ==
         NULL) ||
        !r_llvm_json_destroy(emitter, r_llvm_json_value_of(emitter, member))) {
        return false;
    }
    return r_llvm_json_built_check(emitter, built);
}

/* ---- the context ---- */

/* A nested context one level deeper, or the depth error returned from the encoder
   (r_c17_json_encode_enter). */
static LLVMValueRef r_llvm_json_enter(RLlvmEmitter *emitter, LLVMValueRef context) {
    uint32_t size = 0U;
    uint32_t align = 0U;
    LLVMValueRef depth = r_llvm_std_member(emitter, context, "RStdJsonEncodeContext", "depth");
    LLVMValueRef options = r_llvm_std_member(emitter, context, "RStdJsonEncodeContext", "options");
    LLVMValueRef current;
    LLVMValueRef maximum;
    LLVMValueRef nested;
    LLVMBasicBlockRef deep = r_llvm_json_block(emitter);
    LLVMBasicBlockRef next = r_llvm_json_block(emitter);

    if ((depth == NULL) || (options == NULL) ||
        !r_llvm_runtime_layout(emitter, "RStdJsonEncodeContext", &size, &align)) {
        return NULL;
    }
    current = LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 64U), depth, "");
    maximum = LLVMBuildLoad2(emitter->builder,
                             r_llvm_int(emitter, 64U),
                             r_llvm_std_member(emitter, options, "RStdJsonOptions", "max_depth"),
                             "");
    (void)LLVMBuildCondBr(emitter->builder,
                          LLVMBuildICmp(emitter->builder, LLVMIntUGE, current, maximum, ""),
                          deep,
                          next);
    LLVMPositionBuilderAtEnd(emitter->builder, deep);
    {
        LLVMValueRef failure = r_llvm_json_result(emitter, false);
        if ((failure == NULL) ||
            !r_llvm_json_fail(emitter,
                              r_llvm_std_member(emitter, failure, "RStdJsonValueResult", "outcome"),
                              "R_STD_JSON_ERROR_DEPTH_LIMIT") ||
            !r_llvm_json_return(emitter, failure)) {
            return NULL;
        }
    }
    LLVMPositionBuilderAtEnd(emitter->builder, next);
    nested = r_llvm_entry_alloca(emitter, size, align, "nested");
    (void)LLVMBuildMemCpy(
        emitter->builder, nested, align, context, align, r_llvm_u64(emitter, size));
    (void)LLVMBuildStore(emitter->builder,
                         LLVMBuildAdd(emitter->builder, current, r_llvm_u64(emitter, 1U), ""),
                         r_llvm_std_member(emitter, nested, "RStdJsonEncodeContext", "depth"));
    return nested;
}

/* ---- hooks ---- */

/* The value a hook returns, or its error in `outcome`; returns from the encoder on failure
   (r_c17_json_hook_call). */
static bool r_llvm_json_hook_encode(RLlvmEmitter *emitter,
                                    RSymbolId hook_id,
                                    LLVMValueRef value,
                                    LLVMValueRef built) {
    const RSemanticSymbol *hook = &emitter->frontend->semantic_symbols[(size_t)hook_id - 1U];
    LLVMValueRef arguments[2];
    uint32_t size = 0U;
    uint32_t align = 0U;
    uint32_t payload = 0U;

    if (emitter->functions[hook_id] == NULL) {
        return r_llvm_unsupported(emitter, "a JSON hook that is not lowered");
    }
    if (hook->throws_type == R_TYPE_ID_INVALID) {
        arguments[0] = r_llvm_json_value_of(emitter, built);
        arguments[1] = value;
        (void)LLVMBuildCall2(emitter->builder,
                             emitter->function_types[hook_id],
                             emitter->functions[hook_id],
                             arguments,
                             2U,
                             "");
        return true;
    }
    if (!r_llvm_layout(emitter, hook->effect_carrier_type, &size, &align) ||
        !r_llvm_payload_offset(emitter, hook->effect_carrier_type, &payload)) {
        return false;
    }
    {
        LLVMValueRef carrier = r_llvm_entry_alloca(emitter, size, align, "hook");
        LLVMValueRef tag;
        uint32_t index;
        LLVMBasicBlockRef next = r_llvm_json_block(emitter);
        LLVMValueRef choice;
        (void)LLVMBuildMemSet(emitter->builder,
                              carrier,
                              LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                              r_llvm_u64(emitter, size),
                              align);
        arguments[0] = carrier;
        arguments[1] = value;
        (void)LLVMBuildCall2(emitter->builder,
                             emitter->function_types[hook_id],
                             emitter->functions[hook_id],
                             arguments,
                             2U,
                             "");
        tag = LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), carrier, "");
        choice = LLVMBuildSwitch(emitter->builder, tag, next, 0U);
        for (index = 0U; index < r_semantic_effect_count(emitter->frontend, hook->throws_type);
             ++index) {
            const RTypeId error = r_semantic_effect_at(emitter->frontend, hook->throws_type, index);
            const bool allocation = r_llvm_standard_named(emitter, error, "std.alloc::alloc_error");
            LLVMBasicBlockRef failed = r_llvm_json_block(emitter);
            LLVMValueRef outcome =
                r_llvm_std_member(emitter, built, "RStdJsonValueResult", "outcome");
            uint64_t status = 0U;
            uint32_t error_size = 0U;
            uint32_t error_align = 0U;
            LLVMAddCase(choice, r_llvm_u32(emitter, index + 1U), failed);
            LLVMPositionBuilderAtEnd(emitter->builder, failed);
            if (!r_llvm_json_constant(emitter,
                                      allocation ? "R_STD_JSON_CALL_ALLOCATION_ERROR"
                                                 : "R_STD_JSON_CALL_JSON_ERROR",
                                      &status) ||
                !r_llvm_runtime_layout(emitter,
                                       allocation ? "RStdAllocError" : "RStdJsonError",
                                       &error_size,
                                       &error_align)) {
                return false;
            }
            (void)LLVMBuildStore(emitter->builder,
                                 r_llvm_u32(emitter, status),
                                 r_llvm_std_member(emitter, outcome, "RStdJsonResult", "status"));
            (void)LLVMBuildMemCpy(
                emitter->builder,
                r_llvm_std_member(
                    emitter, outcome, "RStdJsonResult", allocation ? "allocation_error" : "error"),
                error_align,
                r_llvm_byte_offset(emitter, carrier, payload),
                error_align,
                r_llvm_u64(emitter, error_size));
            if (!r_llvm_json_return(emitter, built)) {
                return false;
            }
        }
        LLVMPositionBuilderAtEnd(emitter->builder, next);
        if (!r_llvm_runtime_layout(emitter, "RStdJsonValue", &size, &align)) {
            return false;
        }
        (void)LLVMBuildMemCpy(emitter->builder,
                              r_llvm_json_value_of(emitter, built),
                              align,
                              r_llvm_byte_offset(emitter, carrier, payload),
                              align,
                              r_llvm_u64(emitter, size));
    }
    return true;
}

/* When `quoted`: built.outcome = r_json_quote_number(&built.value), destroyed on failure. */
static bool
r_llvm_json_quote(RLlvmEmitter *emitter, LLVMValueRef built, LLVMValueRef quoted, bool only_ok) {
    LLVMBasicBlockRef quote = r_llvm_json_block(emitter);
    LLVMBasicBlockRef done = r_llvm_json_block(emitter);
    LLVMBasicBlockRef failed = r_llvm_json_block(emitter);
    LLVMValueRef value = r_llvm_json_value_of(emitter, built);
    LLVMValueRef condition = quoted;

    if (only_ok) {
        condition =
            LLVMBuildAnd(emitter->builder, quoted, r_llvm_json_ok(emitter, built, true), "");
    }
    (void)LLVMBuildCondBr(emitter->builder, condition, quote, done);
    LLVMPositionBuilderAtEnd(emitter->builder, quote);
    if (r_llvm_call_runtime(emitter,
                            "r_json_quote_number",
                            &value,
                            1U,
                            r_llvm_std_member(emitter, built, "RStdJsonValueResult", "outcome")) ==
        NULL) {
        return false;
    }
    (void)LLVMBuildCondBr(emitter->builder, r_llvm_json_ok(emitter, built, true), done, failed);
    LLVMPositionBuilderAtEnd(emitter->builder, failed);
    if (!r_llvm_json_destroy(emitter, value)) {
        return false;
    }
    (void)LLVMBuildBr(emitter->builder, done);
    LLVMPositionBuilderAtEnd(emitter->builder, done);
    return true;
}

/* ---- enums ---- */

/* The variant of an enum value as a block per variant: the tag of a tagged enum, the integer of
   a plain one. Returns the switch and positions at the default. */
static bool r_llvm_json_variants(RLlvmEmitter *emitter,
                                 RTypeId id,
                                 const RSemanticAggregate *aggregate,
                                 LLVMValueRef value,
                                 LLVMBasicBlockRef *blocks,
                                 LLVMBasicBlockRef otherwise) {
    LLVMValueRef selector;
    LLVMValueRef choice;
    uint32_t index;

    if (aggregate->is_tagged) {
        selector = LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), value, "");
    } else {
        LLVMTypeRef integer = r_llvm_scalar_type(emitter, id);
        if (integer == NULL) {
            return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
        selector = r_llvm_load_scalar(emitter, id, value);
    }
    choice = LLVMBuildSwitch(emitter->builder, selector, otherwise, aggregate->variant_count);
    for (index = 0U; index < aggregate->variant_count; ++index) {
        const RSemanticVariant *variant =
            &emitter->frontend->semantic_variants[(size_t)aggregate->first_variant + index];
        uint32_t earlier;
        blocks[index] = NULL;
        /* Equal enumerator values select the first variant, as the chain of C ifs does. */
        for (earlier = 0U; earlier < index; ++earlier) {
            if (!aggregate->is_tagged &&
                (emitter->frontend->semantic_variants[(size_t)aggregate->first_variant + earlier]
                     .value == variant->value)) {
                break;
            }
        }
        if (earlier != index) {
            continue;
        }
        blocks[index] = r_llvm_json_block(emitter);
        LLVMAddCase(choice,
                    aggregate->is_tagged ? r_llvm_u32(emitter, index)
                                         : LLVMConstInt(LLVMTypeOf(selector), variant->value, 0),
                    blocks[index]);
    }
    return true;
}

static bool r_llvm_json_encode_enum(RLlvmEmitter *emitter,
                                    RTypeId id,
                                    LLVMValueRef context,
                                    LLVMValueRef value,
                                    LLVMValueRef allocator) {
    const RSemanticAggregate *aggregate = r_llvm_json_aggregate(emitter, id);
    LLVMBasicBlockRef *blocks;
    LLVMBasicBlockRef otherwise = r_llvm_json_block(emitter);
    uint32_t payload = 0U;
    uint32_t index;
    bool success = false;

    if ((aggregate == NULL) || (aggregate->variant_count == 0U)) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    if (aggregate->is_tagged && !r_llvm_payload_offset(emitter, id, &payload)) {
        return false;
    }
    blocks = r_llvm_allocate(emitter, aggregate->variant_count * sizeof(*blocks));
    if ((blocks == NULL) ||
        !r_llvm_json_variants(emitter, id, aggregate, value, blocks, otherwise)) {
        r_llvm_free(emitter, blocks);
        return false;
    }
    for (index = 0U; index < aggregate->variant_count; ++index) {
        const RSemanticVariant *variant =
            &emitter->frontend->semantic_variants[(size_t)aggregate->first_variant + index];
        LLVMValueRef key;
        if (blocks[index] == NULL) {
            continue;
        }
        LLVMPositionBuilderAtEnd(emitter->builder, blocks[index]);
        key = r_llvm_json_bytes(emitter, variant->name_intern_id);
        if (key == NULL) {
            goto cleanup;
        }
        if (variant->payload_type == R_TYPE_ID_INVALID) {
            LLVMValueRef arguments[2];
            LLVMValueRef result;
            arguments[0] = allocator;
            arguments[1] = key;
            result = r_llvm_json_value_call(emitter, "r_std_json_from_string", arguments, 2U);
            if ((result == NULL) || !r_llvm_json_return(emitter, result)) {
                goto cleanup;
            }
        } else {
            LLVMValueRef nested = r_llvm_json_enter(emitter, context);
            LLVMValueRef member;
            LLVMValueRef built;
            if (nested == NULL) {
                goto cleanup;
            }
            member = r_llvm_json_encode_call(emitter,
                                             variant->payload_type,
                                             nested,
                                             r_llvm_byte_offset(emitter, value, payload),
                                             false,
                                             NULL);
            if ((member == NULL) || !r_llvm_json_member_check(emitter, member, NULL)) {
                goto cleanup;
            }
            built = r_llvm_json_value_call(emitter, "r_std_json_object", &allocator, 1U);
            if (built == NULL) {
                goto cleanup;
            }
            {
                LLVMBasicBlockRef insert = r_llvm_json_block(emitter);
                LLVMBasicBlockRef after = r_llvm_json_block(emitter);
                LLVMBasicBlockRef failed = r_llvm_json_block(emitter);
                LLVMBasicBlockRef done = r_llvm_json_block(emitter);
                LLVMValueRef arguments[3];
                (void)LLVMBuildCondBr(
                    emitter->builder, r_llvm_json_ok(emitter, built, true), insert, after);
                LLVMPositionBuilderAtEnd(emitter->builder, insert);
                arguments[0] = r_llvm_json_value_of(emitter, built);
                arguments[1] = key;
                arguments[2] = r_llvm_json_value_of(emitter, member);
                if (r_llvm_call_runtime(
                        emitter,
                        "r_std_json_insert",
                        arguments,
                        3U,
                        r_llvm_std_member(emitter, built, "RStdJsonValueResult", "outcome")) ==
                    NULL) {
                    goto cleanup;
                }
                (void)LLVMBuildBr(emitter->builder, after);
                LLVMPositionBuilderAtEnd(emitter->builder, after);
                if (!r_llvm_json_destroy(emitter, r_llvm_json_value_of(emitter, member))) {
                    goto cleanup;
                }
                (void)LLVMBuildCondBr(
                    emitter->builder, r_llvm_json_ok(emitter, built, true), done, failed);
                LLVMPositionBuilderAtEnd(emitter->builder, failed);
                if (!r_llvm_json_destroy(emitter, r_llvm_json_value_of(emitter, built))) {
                    goto cleanup;
                }
                (void)LLVMBuildBr(emitter->builder, done);
                LLVMPositionBuilderAtEnd(emitter->builder, done);
                if (!r_llvm_json_return(emitter, built)) {
                    goto cleanup;
                }
            }
        }
    }
    LLVMPositionBuilderAtEnd(emitter->builder, otherwise);
    {
        LLVMValueRef failure = r_llvm_json_result(emitter, false);
        success =
            (failure != NULL) &&
            r_llvm_json_fail(emitter,
                             r_llvm_std_member(emitter, failure, "RStdJsonValueResult", "outcome"),
                             "R_STD_JSON_ERROR_TYPE") &&
            r_llvm_json_return(emitter, failure);
    }

cleanup:
    r_llvm_free(emitter, blocks);
    return success;
}

/* ---- zero predicates (r_c17_json_zero_definition) ---- */

static bool r_llvm_json_zero_body(RLlvmEmitter *emitter, RTypeId id) {
    const RSemanticType *type = r_llvm_type(emitter, r_llvm_value_type(emitter, id));
    const RSemanticAggregate *custom = r_llvm_json_aggregate(emitter, id);
    LLVMValueRef value = LLVMGetParam(emitter->function, 0U);
    LLVMValueRef yes = LLVMConstInt(r_llvm_int(emitter, 1U), 1U, 0);
    LLVMValueRef no = LLVMConstInt(r_llvm_int(emitter, 1U), 0U, 0);

    if (type == NULL) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    if ((custom != NULL) && (custom->json_is_zero_function != R_SYMBOL_ID_INVALID)) {
        const RSymbolId hook = custom->json_is_zero_function;
        LLVMValueRef answer;
        if (emitter->functions[hook] == NULL) {
            return r_llvm_unsupported(emitter, "a JSON hook that is not lowered");
        }
        answer = LLVMBuildCall2(emitter->builder,
                                emitter->function_types[hook],
                                emitter->functions[hook],
                                &value,
                                1U,
                                "");
        (void)LLVMBuildRet(emitter->builder, answer);
        return true;
    }
    if (r_llvm_standard_named(emitter, id, "std.json::value")) {
        LLVMValueRef kind = r_llvm_call_runtime(emitter, "r_std_json_kind", &value, 1U, NULL);
        uint64_t null_kind = 0U;
        if ((kind == NULL) || !r_llvm_json_constant(emitter, "R_STD_JSON_NULL", &null_kind)) {
            return false;
        }
        (void)LLVMBuildRet(emitter->builder,
                           LLVMBuildICmp(emitter->builder,
                                         LLVMIntEQ,
                                         kind,
                                         LLVMConstInt(LLVMTypeOf(kind), null_kind, 0),
                                         ""));
        return true;
    }
    if (r_llvm_standard_named(emitter, id, "std.json::number")) {
        LLVMValueRef zero =
            r_llvm_call_runtime(emitter, "r_std_json_number_is_zero", &value, 1U, NULL);
        if (zero == NULL) {
            return false;
        }
        if (LLVMGetIntTypeWidth(LLVMTypeOf(zero)) != 1U) {
            zero = LLVMBuildICmp(
                emitter->builder, LLVMIntNE, zero, LLVMConstInt(LLVMTypeOf(zero), 0U, 0), "");
        }
        (void)LLVMBuildRet(emitter->builder, zero);
        return true;
    }
    if (type->kind == R_SEMANTIC_TYPE_ENUM) {
        const RSemanticAggregate *aggregate = r_llvm_json_aggregate(emitter, id);
        LLVMBasicBlockRef *blocks;
        LLVMBasicBlockRef otherwise = r_llvm_json_block(emitter);
        uint32_t payload = 0U;
        uint32_t index;
        if ((aggregate == NULL) || (aggregate->variant_count == 0U)) {
            return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
        if (aggregate->is_tagged && !r_llvm_payload_offset(emitter, id, &payload)) {
            return false;
        }
        blocks = r_llvm_allocate(emitter, aggregate->variant_count * sizeof(*blocks));
        if ((blocks == NULL) ||
            !r_llvm_json_variants(emitter, id, aggregate, value, blocks, otherwise)) {
            r_llvm_free(emitter, blocks);
            return false;
        }
        for (index = 0U; index < aggregate->variant_count; ++index) {
            const RSemanticVariant *variant =
                &emitter->frontend->semantic_variants[(size_t)aggregate->first_variant + index];
            if (blocks[index] == NULL) {
                continue;
            }
            LLVMPositionBuilderAtEnd(emitter->builder, blocks[index]);
            if (variant->value != 0U) {
                (void)LLVMBuildRet(emitter->builder, no);
            } else if (variant->payload_type == R_TYPE_ID_INVALID) {
                (void)LLVMBuildRet(emitter->builder, yes);
            } else {
                LLVMValueRef zero = r_llvm_json_zero(emitter, variant->payload_type);
                LLVMValueRef argument = r_llvm_byte_offset(emitter, value, payload);
                if (zero == NULL) {
                    r_llvm_free(emitter, blocks);
                    return false;
                }
                (void)LLVMBuildRet(
                    emitter->builder,
                    LLVMBuildCall2(
                        emitter->builder, LLVMGlobalGetValueType(zero), zero, &argument, 1U, ""));
            }
        }
        r_llvm_free(emitter, blocks);
        LLVMPositionBuilderAtEnd(emitter->builder, otherwise);
        (void)LLVMBuildRet(emitter->builder, no);
        return true;
    }
    if (((type->kind >= R_SEMANTIC_TYPE_BOOL) && (type->kind <= R_SEMANTIC_TYPE_CHAR)) ||
        (r_llvm_scalar_type(emitter, id) != NULL)) {
        LLVMValueRef scalar = r_llvm_load_scalar(emitter, id, value);
        LLVMTypeRef scalar_type = LLVMTypeOf(scalar);
        LLVMValueRef zero;
        if ((LLVMGetTypeKind(scalar_type) == LLVMFloatTypeKind) ||
            (LLVMGetTypeKind(scalar_type) == LLVMDoubleTypeKind)) {
            zero = LLVMBuildFCmp(
                emitter->builder, LLVMRealOEQ, scalar, LLVMConstReal(scalar_type, 0.0), "");
        } else if (LLVMGetTypeKind(scalar_type) == LLVMPointerTypeKind) {
            zero = LLVMBuildIsNull(emitter->builder, scalar, "");
        } else {
            zero =
                LLVMBuildICmp(emitter->builder, LLVMIntEQ, scalar, LLVMConstNull(scalar_type), "");
        }
        (void)LLVMBuildRet(emitter->builder, zero);
        return true;
    }
    if (type->kind == R_SEMANTIC_TYPE_OPTION) {
        (void)LLVMBuildRet(
            emitter->builder,
            LLVMBuildICmp(emitter->builder,
                          LLVMIntEQ,
                          LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), value, ""),
                          r_llvm_u32(emitter, 0U),
                          ""));
        return true;
    }
    if (r_llvm_standard_named(emitter, id, "std.string::string") ||
        (type->kind == R_SEMANTIC_TYPE_ARRAY) || (type->kind == R_SEMANTIC_TYPE_LIST) ||
        (type->kind == R_SEMANTIC_TYPE_DICT) || (type->kind == R_SEMANTIC_TYPE_STR) ||
        (type->kind == R_SEMANTIC_TYPE_SLICE) || (type->kind == R_SEMANTIC_TYPE_CONSTEXPR_STR)) {
        LLVMValueRef length;
        if (r_llvm_standard_named(emitter, id, "std.string::string")) {
            LLVMValueRef view = r_llvm_std_structure(emitter, "RStdStringView");
            if ((view == NULL) ||
                (r_llvm_call_runtime(emitter, "r_std_string_as_str", &value, 1U, view) == NULL)) {
                return false;
            }
            length = LLVMBuildLoad2(emitter->builder,
                                    r_llvm_int(emitter, 64U),
                                    r_llvm_std_member(emitter, view, "RStdStringView", "length"),
                                    "");
        } else if ((type->kind == R_SEMANTIC_TYPE_STR) || (type->kind == R_SEMANTIC_TYPE_SLICE) ||
                   (type->kind == R_SEMANTIC_TYPE_CONSTEXPR_STR)) {
            length = LLVMBuildLoad2(emitter->builder,
                                    r_llvm_int(emitter, 64U),
                                    r_llvm_byte_offset(emitter, value, 8U),
                                    "");
        } else {
            const char *runtime = type->kind == R_SEMANTIC_TYPE_ARRAY  ? "RRuntimeArray"
                                  : type->kind == R_SEMANTIC_TYPE_LIST ? "RRuntimeList"
                                                                       : "RRuntimeDict";
            length = LLVMBuildLoad2(emitter->builder,
                                    r_llvm_int(emitter, 64U),
                                    r_llvm_std_member(emitter, value, runtime, "length"),
                                    "");
        }
        (void)LLVMBuildRet(
            emitter->builder,
            LLVMBuildICmp(emitter->builder, LLVMIntEQ, length, r_llvm_u64(emitter, 0U), ""));
        return true;
    }
    if (type->kind == R_SEMANTIC_TYPE_FIXED_ARRAY) {
        LLVMValueRef zero = r_llvm_json_zero(emitter, type->base);
        uint32_t size = 0U;
        uint32_t align = 0U;
        uint64_t index;
        if ((zero == NULL) || !r_llvm_layout(emitter, type->base, &size, &align)) {
            return false;
        }
        for (index = 0U; index < type->length; ++index) {
            LLVMBasicBlockRef next = r_llvm_json_block(emitter);
            LLVMBasicBlockRef not_zero = r_llvm_json_block(emitter);
            LLVMValueRef element = r_llvm_byte_offset(emitter, value, (uint32_t)(index * size));
            (void)LLVMBuildCondBr(
                emitter->builder,
                LLVMBuildCall2(
                    emitter->builder, LLVMGlobalGetValueType(zero), zero, &element, 1U, ""),
                next,
                not_zero);
            LLVMPositionBuilderAtEnd(emitter->builder, not_zero);
            (void)LLVMBuildRet(emitter->builder, no);
            LLVMPositionBuilderAtEnd(emitter->builder, next);
        }
        (void)LLVMBuildRet(emitter->builder, yes);
        return true;
    }
    if (type->kind == R_SEMANTIC_TYPE_STRUCT) {
        const RSemanticAggregate *aggregate = r_llvm_json_aggregate(emitter, id);
        uint32_t index;
        if (aggregate == NULL) {
            return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
        for (index = 0U; index < aggregate->field_count; ++index) {
            const uint32_t field_id = aggregate->first_field + index + 1U;
            const RSemanticField *field = r_llvm_field(emitter, field_id);
            LLVMValueRef zero;
            LLVMValueRef member;
            LLVMBasicBlockRef next = r_llvm_json_block(emitter);
            LLVMBasicBlockRef not_zero = r_llvm_json_block(emitter);
            uint32_t offset = 0U;
            if ((field == NULL) || !r_llvm_field_offset(emitter, field_id, &offset)) {
                return field == NULL ? r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR) : false;
            }
            zero = r_llvm_json_zero(emitter, field->type);
            if (zero == NULL) {
                return false;
            }
            member = r_llvm_byte_offset(emitter, value, offset);
            (void)LLVMBuildCondBr(
                emitter->builder,
                LLVMBuildCall2(
                    emitter->builder, LLVMGlobalGetValueType(zero), zero, &member, 1U, ""),
                next,
                not_zero);
            LLVMPositionBuilderAtEnd(emitter->builder, not_zero);
            (void)LLVMBuildRet(emitter->builder, no);
            LLVMPositionBuilderAtEnd(emitter->builder, next);
        }
        (void)LLVMBuildRet(emitter->builder, yes);
        return true;
    }
    (void)LLVMBuildRet(emitter->builder, no);
    return true;
}

/* ---- structs ---- */

/* The members of an embedded field merged into the object being built
   (r_c17_json_encode_embed): the field's object must be an object whose keys do not conflict
   with a field declared elsewhere in the struct. */
static bool r_llvm_json_encode_embed(RLlvmEmitter *emitter,
                                     const RSemanticAggregate *aggregate,
                                     const RSemanticField *field,
                                     LLVMValueRef context,
                                     LLVMValueRef member_address,
                                     LLVMValueRef built) {
    const RFrontendContext *frontend = emitter->frontend;
    const RJsonSchemaField *root = NULL;
    uint32_t ordinal = UINT32_MAX;
    LLVMValueRef member;
    LLVMValueRef count;
    LLVMValueRef index;
    LLVMValueRef kind;
    uint64_t object_kind = 0U;
    uint32_t entry;
    LLVMBasicBlockRef header;
    LLVMBasicBlockRef body;
    LLVMBasicBlockRef exit;

    for (entry = 0U; entry < aggregate->json_field_count; ++entry) {
        const RJsonSchemaField *candidate =
            &frontend->json_fields[aggregate->first_json_field + entry];
        if ((candidate->parent == UINT32_MAX) &&
            (&frontend->semantic_fields[candidate->field] == field)) {
            ordinal = entry;
            root = candidate;
            break;
        }
    }
    if ((root == NULL) || !r_llvm_json_constant(emitter, "R_STD_JSON_OBJECT", &object_kind)) {
        return root == NULL ? r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR) : false;
    }
    member = r_llvm_json_encode_call(emitter, field->type, context, member_address, false, NULL);
    if ((member == NULL) || !r_llvm_json_member_check(emitter, member, built)) {
        return false;
    }
    {
        LLVMValueRef value = r_llvm_json_value_of(emitter, member);
        LLVMBasicBlockRef wrong = r_llvm_json_block(emitter);
        LLVMBasicBlockRef next = r_llvm_json_block(emitter);
        kind = r_llvm_call_runtime(emitter, "r_std_json_kind", &value, 1U, NULL);
        if (kind == NULL) {
            return false;
        }
        (void)LLVMBuildCondBr(emitter->builder,
                              LLVMBuildICmp(emitter->builder,
                                            LLVMIntNE,
                                            kind,
                                            LLVMConstInt(LLVMTypeOf(kind), object_kind, 0),
                                            ""),
                              wrong,
                              next);
        LLVMPositionBuilderAtEnd(emitter->builder, wrong);
        if (!r_llvm_json_destroy(emitter, value) ||
            !r_llvm_json_destroy(emitter, r_llvm_json_value_of(emitter, built)) ||
            !r_llvm_json_fail(emitter,
                              r_llvm_std_member(emitter, built, "RStdJsonValueResult", "outcome"),
                              "R_STD_JSON_ERROR_TYPE") ||
            !r_llvm_json_return(emitter, built)) {
            return false;
        }
        LLVMPositionBuilderAtEnd(emitter->builder, next);
        count = r_llvm_call_runtime(emitter, "r_std_json_len", &value, 1U, NULL);
        if (count == NULL) {
            return false;
        }
    }
    index = r_llvm_entry_alloca(emitter, 8U, 8U, "index");
    (void)LLVMBuildStore(emitter->builder, r_llvm_u64(emitter, 0U), index);
    header = r_llvm_json_block(emitter);
    body = r_llvm_json_block(emitter);
    exit = r_llvm_json_block(emitter);
    (void)LLVMBuildBr(emitter->builder, header);
    LLVMPositionBuilderAtEnd(emitter->builder, header);
    {
        LLVMValueRef current =
            LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 64U), index, "");
        LLVMValueRef arguments[3];
        LLVMValueRef key = r_llvm_std_structure(emitter, "RStdJsonByteView");
        LLVMValueRef declared_here = LLVMConstInt(r_llvm_int(emitter, 1U), 0U, 0);
        LLVMValueRef conflict = LLVMConstInt(r_llvm_int(emitter, 1U), 0U, 0);
        LLVMValueRef ignore_case;
        LLVMBasicBlockRef refuse = r_llvm_json_block(emitter);
        LLVMBasicBlockRef next = r_llvm_json_block(emitter);
        (void)LLVMBuildCondBr(emitter->builder,
                              LLVMBuildICmp(emitter->builder, LLVMIntULT, current, count, ""),
                              body,
                              exit);
        LLVMPositionBuilderAtEnd(emitter->builder, body);
        arguments[0] = r_llvm_json_value_of(emitter, member);
        arguments[1] = current;
        if ((key == NULL) ||
            (r_llvm_call_runtime(emitter, "r_std_json_key_at", arguments, 2U, key) == NULL)) {
            return false;
        }
        ignore_case = LLVMBuildICmp(
            emitter->builder,
            LLVMIntNE,
            LLVMBuildLoad2(
                emitter->builder,
                r_llvm_int(emitter, 8U),
                r_llvm_std_member(
                    emitter,
                    r_llvm_std_member(emitter, context, "RStdJsonEncodeContext", "options"),
                    "RStdJsonOptions",
                    "ignore_case"),
                ""),
            LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
            "");
        for (entry = 0U; entry < aggregate->json_field_count; ++entry) {
            const RJsonSchemaField *candidate =
                &frontend->json_fields[aggregate->first_json_field + entry];
            const RSemanticField *declared;
            LLVMValueRef other;
            const bool own = root->embedded && (entry > ordinal) && (entry < root->end);
            if (candidate->embedded || candidate->collector || candidate->excluded) {
                continue;
            }
            declared = &frontend->semantic_fields[candidate->field];
            other = r_llvm_json_bytes(emitter, r_llvm_json_field_key(declared));
            if (other == NULL) {
                return false;
            }
            arguments[0] = key;
            arguments[1] = other;
            if (own) {
                LLVMValueRef equal;
                arguments[2] = LLVMConstInt(r_llvm_int(emitter, 1U), 0U, 0);
                equal = r_llvm_call_runtime(emitter, "r_std_json_name_equal", arguments, 3U, NULL);
                if (equal == NULL) {
                    return false;
                }
                declared_here = LLVMBuildOr(
                    emitter->builder,
                    declared_here,
                    LLVMBuildTrunc(emitter->builder, equal, r_llvm_int(emitter, 1U), ""),
                    "");
            }
            arguments[2] = (declared->json.flags & R_JSON_FIELD_CASE) == 0U
                               ? ignore_case
                               : LLVMConstInt(r_llvm_int(emitter, 1U),
                                              declared->json.ignore_case ? 1U : 0U,
                                              0);
            {
                LLVMValueRef equal =
                    r_llvm_call_runtime(emitter, "r_std_json_name_equal", arguments, 3U, NULL);
                if (equal == NULL) {
                    return false;
                }
                conflict = LLVMBuildOr(
                    emitter->builder,
                    conflict,
                    LLVMBuildTrunc(emitter->builder, equal, r_llvm_int(emitter, 1U), ""),
                    "");
            }
        }
        (void)LLVMBuildCondBr(
            emitter->builder,
            LLVMBuildAnd(
                emitter->builder, LLVMBuildNot(emitter->builder, declared_here, ""), conflict, ""),
            refuse,
            next);
        LLVMPositionBuilderAtEnd(emitter->builder, refuse);
        if (!r_llvm_json_destroy(emitter, r_llvm_json_value_of(emitter, member)) ||
            !r_llvm_json_destroy(emitter, r_llvm_json_value_of(emitter, built)) ||
            !r_llvm_json_fail(emitter,
                              r_llvm_std_member(emitter, built, "RStdJsonValueResult", "outcome"),
                              "R_STD_JSON_ERROR_KEY_CONFLICT") ||
            !r_llvm_json_return(emitter, built)) {
            return false;
        }
        LLVMPositionBuilderAtEnd(emitter->builder, next);
        (void)LLVMBuildStore(emitter->builder,
                             LLVMBuildAdd(emitter->builder, current, r_llvm_u64(emitter, 1U), ""),
                             index);
        (void)LLVMBuildBr(emitter->builder, header);
    }
    LLVMPositionBuilderAtEnd(emitter->builder, exit);
    {
        LLVMValueRef arguments[2];
        arguments[0] = r_llvm_json_value_of(emitter, built);
        arguments[1] = r_llvm_json_value_of(emitter, member);
        if ((r_llvm_call_runtime(
                 emitter,
                 "r_json_object_extend",
                 arguments,
                 2U,
                 r_llvm_std_member(emitter, built, "RStdJsonValueResult", "outcome")) == NULL) ||
            !r_llvm_json_destroy(emitter, r_llvm_json_value_of(emitter, member))) {
            return false;
        }
    }
    return r_llvm_json_built_check(emitter, built);
}

static bool r_llvm_json_encode_struct(RLlvmEmitter *emitter,
                                      RTypeId id,
                                      LLVMValueRef context,
                                      LLVMValueRef value,
                                      LLVMValueRef allocator) {
    const RSemanticAggregate *aggregate = r_llvm_json_aggregate(emitter, id);
    LLVMValueRef built;
    uint32_t index;

    if (aggregate == NULL) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    built = r_llvm_json_container(emitter, true, allocator);
    if (built == NULL) {
        return false;
    }
    for (index = 0U; index < aggregate->field_count; ++index) {
        const uint32_t field_id = aggregate->first_field + index + 1U;
        const RSemanticField *field = r_llvm_field(emitter, field_id);
        const RJsonFieldContract *contract;
        LLVMValueRef member_address;
        LLVMValueRef omit = LLVMConstInt(r_llvm_int(emitter, 1U), 0U, 0);
        LLVMValueRef member;
        LLVMBasicBlockRef encode;
        LLVMBasicBlockRef next;
        uint32_t offset = 0U;
        if ((field == NULL) || !r_llvm_field_offset(emitter, field_id, &offset)) {
            return field == NULL ? r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR) : false;
        }
        contract = &field->json;
        if (field->is_protected || ((contract->flags & R_JSON_FIELD_SKIP) != 0U)) {
            continue;
        }
        member_address = r_llvm_byte_offset(emitter, value, offset);
        if ((contract->flags & R_JSON_FIELD_EMBED) != 0U) {
            if (!r_llvm_json_encode_embed(
                    emitter, aggregate, field, context, member_address, built)) {
                return false;
            }
            continue;
        }
        if ((contract->flags & R_JSON_FIELD_OMITNONE) != 0U) {
            omit = LLVMBuildICmp(
                emitter->builder,
                LLVMIntEQ,
                LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), member_address, ""),
                r_llvm_u32(emitter, 0U),
                "");
        }
        if ((contract->flags & R_JSON_FIELD_OMITZERO) != 0U) {
            LLVMValueRef zero = r_llvm_json_zero(emitter, field->type);
            if (zero == NULL) {
                return false;
            }
            omit = LLVMBuildOr(
                emitter->builder,
                omit,
                LLVMBuildCall2(
                    emitter->builder, LLVMGlobalGetValueType(zero), zero, &member_address, 1U, ""),
                "");
        }
        encode = r_llvm_json_block(emitter);
        next = r_llvm_json_block(emitter);
        (void)LLVMBuildCondBr(emitter->builder, omit, next, encode);
        LLVMPositionBuilderAtEnd(emitter->builder, encode);
        member = r_llvm_json_encode_call(emitter,
                                         field->type,
                                         context,
                                         member_address,
                                         (contract->flags & R_JSON_FIELD_STRING) != 0U,
                                         NULL);
        if ((member == NULL) || !r_llvm_json_member_check(emitter, member, built)) {
            return false;
        }
        {
            LLVMValueRef member_value = r_llvm_json_value_of(emitter, member);
            LLVMBasicBlockRef insert = r_llvm_json_block(emitter);
            LLVMBasicBlockRef after = r_llvm_json_block(emitter);
            LLVMValueRef omit_empty = LLVMConstInt(r_llvm_int(emitter, 1U), 0U, 0);
            LLVMValueRef key;
            LLVMValueRef arguments[3];
            if ((contract->flags & R_JSON_FIELD_OMITEMPTY) != 0U) {
                omit_empty =
                    r_llvm_call_runtime(emitter, "r_json_value_empty", &member_value, 1U, NULL);
                if (omit_empty == NULL) {
                    return false;
                }
                omit_empty =
                    LLVMBuildTrunc(emitter->builder, omit_empty, r_llvm_int(emitter, 1U), "");
            }
            (void)LLVMBuildCondBr(emitter->builder, omit_empty, after, insert);
            LLVMPositionBuilderAtEnd(emitter->builder, insert);
            key = r_llvm_json_bytes(emitter, r_llvm_json_field_key(field));
            if (key == NULL) {
                return false;
            }
            arguments[0] = r_llvm_json_value_of(emitter, built);
            arguments[1] = key;
            arguments[2] = member_value;
            if (r_llvm_call_runtime(
                    emitter,
                    "r_std_json_insert",
                    arguments,
                    3U,
                    r_llvm_std_member(emitter, built, "RStdJsonValueResult", "outcome")) == NULL) {
                return false;
            }
            (void)LLVMBuildBr(emitter->builder, after);
            LLVMPositionBuilderAtEnd(emitter->builder, after);
            if (!r_llvm_json_destroy(emitter, member_value) ||
                !r_llvm_json_built_check(emitter, built)) {
                return false;
            }
        }
        (void)LLVMBuildBr(emitter->builder, next);
        LLVMPositionBuilderAtEnd(emitter->builder, next);
    }
    return r_llvm_json_return(emitter, built);
}

/* ---- sequences and collections ---- */

/* The elements `[0, length)` of `stride` bytes from `data`, appended in order. */
static bool r_llvm_json_encode_sequence(RLlvmEmitter *emitter,
                                        RTypeId element,
                                        LLVMValueRef context,
                                        LLVMValueRef data,
                                        LLVMValueRef length,
                                        LLVMValueRef allocator) {
    LLVMValueRef built = r_llvm_json_container(emitter, false, allocator);
    LLVMValueRef index = r_llvm_entry_alloca(emitter, 8U, 8U, "index");
    LLVMBasicBlockRef header = r_llvm_json_block(emitter);
    LLVMBasicBlockRef body = r_llvm_json_block(emitter);
    LLVMBasicBlockRef exit = r_llvm_json_block(emitter);
    LLVMValueRef current;
    LLVMValueRef member;
    uint32_t size = 0U;
    uint32_t align = 0U;

    if ((built == NULL) || !r_llvm_layout(emitter, element, &size, &align)) {
        return false;
    }
    (void)LLVMBuildStore(emitter->builder, r_llvm_u64(emitter, 0U), index);
    (void)LLVMBuildBr(emitter->builder, header);
    LLVMPositionBuilderAtEnd(emitter->builder, header);
    current = LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 64U), index, "");
    (void)LLVMBuildCondBr(emitter->builder,
                          LLVMBuildICmp(emitter->builder, LLVMIntULT, current, length, ""),
                          body,
                          exit);
    LLVMPositionBuilderAtEnd(emitter->builder, body);
    {
        LLVMValueRef offset =
            LLVMBuildMul(emitter->builder, current, r_llvm_u64(emitter, size), "");
        member = r_llvm_json_encode_call(
            emitter,
            element,
            context,
            LLVMBuildGEP2(emitter->builder, r_llvm_int(emitter, 8U), data, &offset, 1U, ""),
            false,
            NULL);
    }
    if ((member == NULL) || !r_llvm_json_member_check(emitter, member, built) ||
        !r_llvm_json_add(emitter, built, NULL, member)) {
        return false;
    }
    (void)LLVMBuildStore(emitter->builder,
                         LLVMBuildAdd(emitter->builder, current, r_llvm_u64(emitter, 1U), ""),
                         index);
    (void)LLVMBuildBr(emitter->builder, header);
    LLVMPositionBuilderAtEnd(emitter->builder, exit);
    return r_llvm_json_return(emitter, built);
}

/* list<T>: its values in order; dict<string, V>: its entries as members keyed by the string. */
static bool r_llvm_json_encode_collection(RLlvmEmitter *emitter,
                                          const RSemanticType *type,
                                          LLVMValueRef context,
                                          LLVMValueRef value,
                                          LLVMValueRef allocator) {
    const bool dict = type->kind == R_SEMANTIC_TYPE_DICT;
    LLVMValueRef built = r_llvm_json_container(emitter, dict, allocator);
    LLVMValueRef iterator =
        r_llvm_std_structure(emitter, dict ? "RRuntimeDictIterator" : "RRuntimeListIterator");
    LLVMValueRef entry = dict ? r_llvm_std_structure(emitter, "RRuntimeDictEntryRef") : NULL;
    LLVMBasicBlockRef header = r_llvm_json_block(emitter);
    LLVMBasicBlockRef body = r_llvm_json_block(emitter);
    LLVMBasicBlockRef exit = r_llvm_json_block(emitter);
    LLVMValueRef element;
    LLVMValueRef key = NULL;
    LLVMValueRef member;

    if ((built == NULL) || (iterator == NULL) || (dict && (entry == NULL)) ||
        (r_llvm_call_runtime(
             emitter, dict ? "r_runtime_dict_iter" : "r_runtime_list_iter", &value, 1U, iterator) ==
         NULL)) {
        return false;
    }
    (void)LLVMBuildBr(emitter->builder, header);
    LLVMPositionBuilderAtEnd(emitter->builder, header);
    if (dict) {
        LLVMValueRef arguments[2];
        LLVMValueRef more;
        arguments[0] = iterator;
        arguments[1] = entry;
        more = r_llvm_call_runtime(emitter, "r_runtime_dict_next", arguments, 2U, NULL);
        if (more == NULL) {
            return false;
        }
        (void)LLVMBuildCondBr(
            emitter->builder,
            LLVMBuildICmp(emitter->builder, LLVMIntNE, more, LLVMConstNull(LLVMTypeOf(more)), ""),
            body,
            exit);
        LLVMPositionBuilderAtEnd(emitter->builder, body);
        {
            LLVMValueRef string =
                LLVMBuildLoad2(emitter->builder,
                               r_llvm_pointer(emitter),
                               r_llvm_std_member(emitter, entry, "RRuntimeDictEntryRef", "key"),
                               "");
            LLVMValueRef text = r_llvm_std_structure(emitter, "RStdStringView");
            if ((text == NULL) ||
                (r_llvm_call_runtime(emitter, "r_std_string_as_str", &string, 1U, text) == NULL)) {
                return false;
            }
            /* RStdStringView and RStdJsonByteView are both {data, length}. */
            key = text;
        }
        element = LLVMBuildLoad2(emitter->builder,
                                 r_llvm_pointer(emitter),
                                 r_llvm_std_member(emitter, entry, "RRuntimeDictEntryRef", "value"),
                                 "");
    } else {
        element = r_llvm_call_runtime(emitter, "r_runtime_list_next", &iterator, 1U, NULL);
        if (element == NULL) {
            return false;
        }
        (void)LLVMBuildCondBr(
            emitter->builder, LLVMBuildIsNotNull(emitter->builder, element, ""), body, exit);
        LLVMPositionBuilderAtEnd(emitter->builder, body);
    }
    member = r_llvm_json_encode_call(
        emitter, dict ? type->second : type->base, context, element, false, NULL);
    if ((member == NULL) || !r_llvm_json_member_check(emitter, member, built) ||
        !r_llvm_json_add(emitter, built, key, member)) {
        return false;
    }
    (void)LLVMBuildBr(emitter->builder, header);
    LLVMPositionBuilderAtEnd(emitter->builder, exit);
    return r_llvm_json_return(emitter, built);
}

/* ---- encoders (r_c17_json_encode_definition) ---- */

static bool r_llvm_json_encode_body(RLlvmEmitter *emitter, RTypeId id) {
    const RSemanticType *type = r_llvm_type(emitter, r_llvm_value_type(emitter, id));
    const RSemanticAggregate *custom = r_llvm_json_aggregate(emitter, id);
    LLVMValueRef context = LLVMGetParam(emitter->function, 1U);
    LLVMValueRef value = LLVMGetParam(emitter->function, 2U);
    LLVMValueRef quoted = LLVMGetParam(emitter->function, 3U);
    LLVMValueRef allocator;
    RSemanticTypeKind kind;
    RTokenKind c_token = R_TOKEN_INVALID;
    bool floating;

    if (type == NULL) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    kind = type->kind;
    if (kind == R_SEMANTIC_TYPE_C_ABI_NUMERIC) {
        c_token = (RTokenKind)type->length;
    }
    allocator =
        LLVMBuildLoad2(emitter->builder,
                       r_llvm_pointer(emitter),
                       r_llvm_std_member(emitter, context, "RStdJsonEncodeContext", "allocator"),
                       "");
    if ((custom != NULL) && (custom->json_marshal_function != R_SYMBOL_ID_INVALID)) {
        LLVMValueRef built = r_llvm_json_result(emitter, false);
        return (built != NULL) &&
               r_llvm_json_hook_encode(emitter, custom->json_marshal_function, value, built) &&
               r_llvm_json_quote(emitter, built, quoted, false) &&
               r_llvm_json_return(emitter, built);
    }
    if ((kind == R_SEMANTIC_TYPE_STRUCT) || (kind == R_SEMANTIC_TYPE_FIXED_ARRAY) ||
        (kind == R_SEMANTIC_TYPE_ARRAY) || (kind == R_SEMANTIC_TYPE_LIST) ||
        (kind == R_SEMANTIC_TYPE_DICT) || (kind == R_SEMANTIC_TYPE_SLICE)) {
        context = r_llvm_json_enter(emitter, context);
        if (context == NULL) {
            return false;
        }
    }
    if (r_llvm_standard_named(emitter, id, "std.json::value") ||
        r_llvm_standard_named(emitter, id, "std.json::number")) {
        LLVMValueRef arguments[2];
        LLVMValueRef built;
        arguments[0] = allocator;
        arguments[1] = value;
        built = r_llvm_json_value_call(emitter,
                                       r_llvm_standard_named(emitter, id, "std.json::value")
                                           ? "r_json_clone_value"
                                           : "r_std_json_from_number",
                                       arguments,
                                       2U);
        return (built != NULL) && r_llvm_json_quote(emitter, built, quoted, true) &&
               r_llvm_json_return(emitter, built);
    }
    if ((kind == R_SEMANTIC_TYPE_LIST) || (kind == R_SEMANTIC_TYPE_DICT)) {
        return r_llvm_json_encode_collection(emitter, type, context, value, allocator);
    }
    if (kind == R_SEMANTIC_TYPE_ENUM) {
        return r_llvm_json_encode_enum(emitter, id, context, value, allocator);
    }
    if (kind == R_SEMANTIC_TYPE_STRUCT) {
        return r_llvm_json_encode_struct(emitter, id, context, value, allocator);
    }
    floating = (kind == R_SEMANTIC_TYPE_F32) || (kind == R_SEMANTIC_TYPE_F64) ||
               (c_token == R_TOKEN_KW_C_FLOAT) || (c_token == R_TOKEN_KW_C_DOUBLE) ||
               (c_token == R_TOKEN_KW_C_LONG_DOUBLE);
    if ((kind == R_SEMANTIC_TYPE_BOOL) || (c_token == R_TOKEN_KW_C_BOOL)) {
        LLVMValueRef arguments[2];
        LLVMValueRef built;
        arguments[0] = allocator;
        arguments[1] = r_llvm_load_scalar(emitter, id, value);
        built = r_llvm_json_value_call(emitter, "r_std_json_from_bool", arguments, 2U);
        return (built != NULL) && r_llvm_json_return(emitter, built);
    }
    if (kind == R_SEMANTIC_TYPE_CHAR) {
        LLVMValueRef arguments[2];
        LLVMValueRef built;
        arguments[0] = allocator;
        arguments[1] = r_llvm_load_scalar(emitter, id, value);
        built = r_llvm_json_value_call(emitter, "r_json_encode_char", arguments, 2U);
        return (built != NULL) && r_llvm_json_return(emitter, built);
    }
    if (floating) {
        const uint32_t representation = kind == R_SEMANTIC_TYPE_F32      ? 0U
                                        : kind == R_SEMANTIC_TYPE_F64    ? 1U
                                        : c_token == R_TOKEN_KW_C_FLOAT  ? 2U
                                        : c_token == R_TOKEN_KW_C_DOUBLE ? 3U
                                                                         : 4U;
        LLVMValueRef arguments[4];
        LLVMValueRef built;
        arguments[0] = allocator;
        /* long double is the double of the target (manifest `representation`). */
        arguments[1] = LLVMBuildFPCast(emitter->builder,
                                       r_llvm_load_scalar(emitter, id, value),
                                       LLVMDoubleTypeInContext(emitter->context),
                                       "");
        arguments[2] = r_llvm_u32(emitter, representation);
        arguments[3] = quoted;
        built = r_llvm_json_value_call(emitter, "r_json_encode_float", arguments, 4U);
        return (built != NULL) && r_llvm_json_return(emitter, built);
    }
    if (((kind >= R_SEMANTIC_TYPE_I8) && (kind <= R_SEMANTIC_TYPE_USIZE)) ||
        (kind == R_SEMANTIC_TYPE_C_ABI_NUMERIC)) {
        const RSemanticTypeKind representation =
            r_semantic_integer_representation(emitter->frontend, id);
        const bool is_signed =
            (representation >= R_SEMANTIC_TYPE_I8) && (representation <= R_SEMANTIC_TYPE_ISIZE);
        LLVMValueRef scalar = r_llvm_load_scalar(emitter, id, value);
        LLVMValueRef wide =
            LLVMBuildIntCast2(emitter->builder, scalar, r_llvm_int(emitter, 64U), is_signed, "");
        LLVMValueRef negative =
            is_signed
                ? LLVMBuildICmp(emitter->builder, LLVMIntSLT, wide, r_llvm_u64(emitter, 0U), "")
                : LLVMConstInt(r_llvm_int(emitter, 1U), 0U, 0);
        LLVMValueRef arguments[4];
        LLVMValueRef built;
        arguments[0] = allocator;
        arguments[1] = negative;
        arguments[2] =
            LLVMBuildSelect(emitter->builder,
                            negative,
                            LLVMBuildSub(emitter->builder, r_llvm_u64(emitter, 0U), wide, ""),
                            wide,
                            "");
        arguments[3] = quoted;
        built = r_llvm_json_value_call(emitter, "r_json_encode_integer", arguments, 4U);
        return (built != NULL) && r_llvm_json_return(emitter, built);
    }
    if (r_llvm_standard_named(emitter, id, "std.string::string") || (kind == R_SEMANTIC_TYPE_STR) ||
        (kind == R_SEMANTIC_TYPE_CONSTEXPR_STR)) {
        LLVMValueRef arguments[2];
        LLVMValueRef built;
        arguments[0] = allocator;
        if (r_llvm_standard_named(emitter, id, "std.string::string")) {
            arguments[1] = r_llvm_std_structure(emitter, "RStdStringView");
            if ((arguments[1] == NULL) ||
                (r_llvm_call_runtime(emitter, "r_std_string_as_str", &value, 1U, arguments[1]) ==
                 NULL)) {
                return false;
            }
        } else {
            /* {data, length} is the layout of RStdJsonByteView. */
            arguments[1] = value;
        }
        built = r_llvm_json_value_call(emitter, "r_std_json_from_string", arguments, 2U);
        return (built != NULL) && r_llvm_json_return(emitter, built);
    }
    if (kind == R_SEMANTIC_TYPE_OPTION) {
        LLVMBasicBlockRef none = r_llvm_json_block(emitter);
        LLVMBasicBlockRef some = r_llvm_json_block(emitter);
        LLVMValueRef built;
        uint32_t payload = 0U;
        if (!r_llvm_payload_offset(emitter, id, &payload)) {
            return false;
        }
        (void)LLVMBuildCondBr(
            emitter->builder,
            LLVMBuildICmp(emitter->builder,
                          LLVMIntEQ,
                          LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), value, ""),
                          r_llvm_u32(emitter, 0U),
                          ""),
            none,
            some);
        LLVMPositionBuilderAtEnd(emitter->builder, none);
        /* A zeroed result: success with the null value. */
        built = r_llvm_json_result(emitter, false);
        if ((built == NULL) || !r_llvm_json_return(emitter, built)) {
            return false;
        }
        LLVMPositionBuilderAtEnd(emitter->builder, some);
        built = r_llvm_json_encode_call(emitter,
                                        type->base,
                                        context,
                                        r_llvm_byte_offset(emitter, value, payload),
                                        false,
                                        quoted);
        return (built != NULL) && r_llvm_json_return(emitter, built);
    }
    if (kind == R_SEMANTIC_TYPE_FIXED_ARRAY) {
        return r_llvm_json_encode_sequence(
            emitter, type->base, context, value, r_llvm_u64(emitter, type->length), allocator);
    }
    if (kind == R_SEMANTIC_TYPE_ARRAY) {
        return r_llvm_json_encode_sequence(
            emitter,
            type->base,
            context,
            LLVMBuildLoad2(emitter->builder,
                           r_llvm_pointer(emitter),
                           r_llvm_std_member(emitter, value, "RRuntimeArray", "data"),
                           ""),
            LLVMBuildLoad2(emitter->builder,
                           r_llvm_int(emitter, 64U),
                           r_llvm_std_member(emitter, value, "RRuntimeArray", "length"),
                           ""),
            allocator);
    }
    if (kind == R_SEMANTIC_TYPE_SLICE) {
        return r_llvm_json_encode_sequence(
            emitter,
            type->base,
            context,
            LLVMBuildLoad2(emitter->builder, r_llvm_pointer(emitter), value, ""),
            LLVMBuildLoad2(emitter->builder,
                           r_llvm_int(emitter, 64U),
                           r_llvm_byte_offset(emitter, value, 8U),
                           ""),
            allocator);
    }
    return r_llvm_unsupported(emitter, "a JSON encoding of this type");
}

/* ---- declarations and the post pass ---- */

static LLVMValueRef r_llvm_json_encoder(RLlvmEmitter *emitter, RTypeId type) {
    const RTypeId value = r_llvm_value_type(emitter, type);
    LLVMTypeRef parameters[4];
    char name[64];

    if ((value == R_TYPE_ID_INVALID) || ((size_t)value > emitter->frontend->semantic_type_count)) {
        (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        return NULL;
    }
    if (emitter->json_encoders[value] == NULL) {
        parameters[0] = r_llvm_pointer(emitter);
        parameters[1] = r_llvm_pointer(emitter);
        parameters[2] = r_llvm_pointer(emitter);
        parameters[3] = r_llvm_int(emitter, 1U);
        (void)snprintf(
            name, sizeof(name), "r_json_encode.%" PRIu32, r_llvm_type_key(emitter, value));
        emitter->json_encoders[value] = LLVMAddFunction(
            emitter->module,
            name,
            LLVMFunctionType(LLVMVoidTypeInContext(emitter->context), parameters, 4U, 0));
        LLVMSetLinkage(emitter->json_encoders[value], LLVMInternalLinkage);
    }
    return emitter->json_encoders[value];
}

static LLVMValueRef r_llvm_json_zero(RLlvmEmitter *emitter, RTypeId type) {
    const RTypeId value = r_llvm_value_type(emitter, type);
    LLVMTypeRef parameter = r_llvm_pointer(emitter);
    char name[64];

    if ((value == R_TYPE_ID_INVALID) || ((size_t)value > emitter->frontend->semantic_type_count)) {
        (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        return NULL;
    }
    if (emitter->json_zeros[value] == NULL) {
        (void)snprintf(name, sizeof(name), "r_json_zero.%" PRIu32, r_llvm_type_key(emitter, value));
        emitter->json_zeros[value] = LLVMAddFunction(
            emitter->module, name, LLVMFunctionType(r_llvm_int(emitter, 1U), &parameter, 1U, 0));
        LLVMSetLinkage(emitter->json_zeros[value], LLVMInternalLinkage);
    }
    return emitter->json_zeros[value];
}

/* std.json::marshal and marshal_with_options (r_c17_emit_json_marshal): the tree of the value
   under the options, stringified with them. */
bool r_llvm_define_json_marshal(RLlvmEmitter *emitter,
                                RSymbolId id,
                                const RSemanticSymbol *symbol) {
    LLVMValueRef context;
    LLVMValueRef tree;
    LLVMValueRef text;
    LLVMValueRef carrier;
    LLVMValueRef arguments[3];
    uint32_t size = 0U;
    uint32_t align = 0U;

    emitter->mir = NULL;
    emitter->frame = NULL;
    emitter->symbol = symbol;
    emitter->symbol_id = id;
    emitter->function = emitter->functions[id];
    LLVMPositionBuilderAtEnd(
        emitter->builder,
        LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "entry"));
    carrier = LLVMGetParam(emitter->function, 0U);
    context = r_llvm_std_structure(emitter, "RStdJsonEncodeContext");
    if ((context == NULL) || (symbol->effect_carrier_type == R_TYPE_ID_INVALID) ||
        !r_llvm_runtime_layout(emitter, "RStdJsonOptions", &size, &align)) {
        return (context == NULL) ? false : r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    {
        LLVMValueRef allocator = r_llvm_std_allocator(emitter);
        if (allocator == NULL) {
            return false;
        }
        (void)LLVMBuildStore(
            emitter->builder,
            allocator,
            r_llvm_std_member(emitter, context, "RStdJsonEncodeContext", "allocator"));
    }
    if (symbol->parameter_count == 2U) {
        (void)LLVMBuildMemCpy(
            emitter->builder,
            r_llvm_std_member(emitter, context, "RStdJsonEncodeContext", "options"),
            align,
            LLVMGetParam(emitter->function, 2U),
            align,
            r_llvm_u64(emitter, size));
    } else if (!r_llvm_json_default_options(
                   emitter,
                   r_llvm_std_member(emitter, context, "RStdJsonEncodeContext", "options"))) {
        return false;
    }
    tree = r_llvm_json_encode_call(
        emitter, symbol->json_type, context, LLVMGetParam(emitter->function, 1U), false, NULL);
    text = r_llvm_std_structure(emitter, "RStdJsonStringResult");
    if ((tree == NULL) || (text == NULL)) {
        return false;
    }
    {
        LLVMBasicBlockRef stringify = r_llvm_json_block(emitter);
        LLVMBasicBlockRef failed = r_llvm_json_block(emitter);
        LLVMBasicBlockRef done = r_llvm_json_block(emitter);
        uint32_t outcome_size = 0U;
        uint32_t outcome_align = 0U;
        if (!r_llvm_runtime_layout(emitter, "RStdJsonResult", &outcome_size, &outcome_align)) {
            return false;
        }
        (void)LLVMBuildCondBr(
            emitter->builder, r_llvm_json_ok(emitter, tree, true), stringify, failed);
        LLVMPositionBuilderAtEnd(emitter->builder, failed);
        (void)LLVMBuildMemCpy(emitter->builder,
                              r_llvm_std_member(emitter, text, "RStdJsonStringResult", "outcome"),
                              outcome_align,
                              r_llvm_std_member(emitter, tree, "RStdJsonValueResult", "outcome"),
                              outcome_align,
                              r_llvm_u64(emitter, outcome_size));
        (void)LLVMBuildBr(emitter->builder, done);
        LLVMPositionBuilderAtEnd(emitter->builder, stringify);
        arguments[0] = LLVMBuildLoad2(
            emitter->builder,
            r_llvm_pointer(emitter),
            r_llvm_std_member(emitter, context, "RStdJsonEncodeContext", "allocator"),
            "");
        arguments[1] = r_llvm_json_value_of(emitter, tree);
        arguments[2] = r_llvm_std_member(emitter, context, "RStdJsonEncodeContext", "options");
        if ((r_llvm_call_runtime(
                 emitter, "r_std_json_stringify_with_options", arguments, 3U, text) == NULL) ||
            !r_llvm_json_destroy(emitter, r_llvm_json_value_of(emitter, tree))) {
            return false;
        }
        (void)LLVMBuildBr(emitter->builder, done);
        LLVMPositionBuilderAtEnd(emitter->builder, done);
    }
    if (!r_llvm_json_carrier(emitter, symbol, carrier, text, "RStdJsonStringResult", false)) {
        return false;
    }
    (void)LLVMBuildRetVoid(emitter->builder);
    return emitter->status == R_FRONTEND_OK;
}

bool r_llvm_emit_json_encoders(RLlvmEmitter *emitter) {
    bool progress = true;

    while (progress) {
        size_t index;
        progress = false;
        for (index = 1U; index <= emitter->frontend->semantic_type_count; ++index) {
            const bool encoder = (emitter->json_encoders[index] != NULL) &&
                                 ((emitter->json_written[index] & 1U) == 0U);
            const bool zero =
                (emitter->json_zeros[index] != NULL) && ((emitter->json_written[index] & 2U) == 0U);
            if (!encoder && !zero) {
                continue;
            }
            progress = true;
            emitter->json_written[index] |= encoder ? 1U : 2U;
            emitter->function =
                encoder ? emitter->json_encoders[index] : emitter->json_zeros[index];
            emitter->mir = NULL;
            emitter->frame = NULL;
            LLVMPositionBuilderAtEnd(
                emitter->builder,
                LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "entry"));
            if (encoder ? !r_llvm_json_encode_body(emitter, (RTypeId)index)
                        : !r_llvm_json_zero_body(emitter, (RTypeId)index)) {
                return false;
            }
        }
    }
    return true;
}

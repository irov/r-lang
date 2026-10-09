#include "json_internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* JSON decoders (R-SLIB-JSON-*, the C17 emitter's json_decode.inc, json_defaults.inc,
   json_ordinary_default.inc and json_embedded.inc). std.json::unmarshal reads the tokens of an
   RStdJsonCursor into a concrete R value through one internal function per decoded type,

       i1 r_json_decode.<type>(ptr cursor, ptr out, i1 quoted, i1 defaulted)

   which initializes `out` and answers true, or leaves it uninitialized, records the failure in
   the cursor and answers false. `defaulted` asks for the value of a missing optional field
   instead of reading tokens. Missing fields of an aggregate take their JSON default (a string, a
   scalar or a factory), the ordinary R default of their type
   (`void r_json_default.<type>(ptr cursor, ptr out)`), or the decoder's default; on failure every
   field already initialized is destroyed. As the encoders, the functions depend only on the
   type. */

static LLVMValueRef r_llvm_json_decoder(RLlvmEmitter *emitter, RTypeId type);
static LLVMValueRef r_llvm_json_ordinary(RLlvmEmitter *emitter, RTypeId type);

LLVMBasicBlockRef r_llvm_jd_block(RLlvmEmitter *emitter) {
    return LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
}

LLVMValueRef r_llvm_jd_true(RLlvmEmitter *emitter) {
    return LLVMConstInt(r_llvm_int(emitter, 1U), 1U, 0);
}

LLVMValueRef r_llvm_jd_false(RLlvmEmitter *emitter) {
    return LLVMConstInt(r_llvm_int(emitter, 1U), 0U, 0);
}

/* A C bool result as i1. */
LLVMValueRef r_llvm_jd_truth(RLlvmEmitter *emitter, LLVMValueRef value) {
    if ((value == NULL) || (LLVMGetIntTypeWidth(LLVMTypeOf(value)) == 1U)) {
        return value;
    }
    return LLVMBuildICmp(emitter->builder, LLVMIntNE, value, LLVMConstNull(LLVMTypeOf(value)), "");
}

LLVMValueRef
r_llvm_jd_call(RLlvmEmitter *emitter, const char *name, LLVMValueRef *arguments, unsigned count) {
    return r_llvm_jd_truth(emitter, r_llvm_call_runtime(emitter, name, arguments, count, NULL));
}

bool r_llvm_jd_constant(RLlvmEmitter *emitter, const char *name, uint64_t *value) {
    int64_t found = 0;
    if (!r_llvm_runtime_constant(emitter, name, &found)) {
        return false;
    }
    *value = (uint64_t)found;
    return true;
}

LLVMValueRef r_llvm_jd_token(RLlvmEmitter *emitter, LLVMValueRef cursor, const char *kind) {
    uint64_t value = 0U;
    LLVMValueRef arguments[2];
    if (!r_llvm_jd_constant(emitter, kind, &value)) {
        return NULL;
    }
    arguments[0] = cursor;
    arguments[1] = r_llvm_u32(emitter, value);
    return r_llvm_jd_call(emitter, "r_json_cursor_expect", arguments, 2U);
}

LLVMValueRef r_llvm_jd_next(RLlvmEmitter *emitter, LLVMValueRef cursor) {
    return r_llvm_jd_call(emitter, "r_json_cursor_next", &cursor, 1U);
}

/* r_json_cursor_fail(cursor, code), whose answer is false. */
LLVMValueRef r_llvm_jd_fail(RLlvmEmitter *emitter, LLVMValueRef cursor, const char *code) {
    uint64_t value = 0U;
    LLVMValueRef arguments[2];
    if (!r_llvm_jd_constant(emitter, code, &value)) {
        return NULL;
    }
    arguments[0] = cursor;
    arguments[1] = r_llvm_u32(emitter, value);
    return r_llvm_jd_call(emitter, "r_json_cursor_fail", arguments, 2U);
}

/* cursor->has_token && cursor->token.kind <op> kind */
LLVMValueRef
r_llvm_jd_has(RLlvmEmitter *emitter, LLVMValueRef cursor, const char *kind, bool equal) {
    uint64_t value = 0U;
    LLVMValueRef has;
    LLVMValueRef current;
    if (!r_llvm_jd_constant(emitter, kind, &value)) {
        return NULL;
    }
    has = LLVMBuildICmp(
        emitter->builder,
        LLVMIntNE,
        LLVMBuildLoad2(emitter->builder,
                       r_llvm_int(emitter, 8U),
                       r_llvm_std_member(emitter, cursor, "RStdJsonCursor", "has_token"),
                       ""),
        LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
        "");
    current = LLVMBuildLoad2(
        emitter->builder,
        r_llvm_int(emitter, 32U),
        r_llvm_std_member(emitter,
                          r_llvm_std_member(emitter, cursor, "RStdJsonCursor", "token"),
                          "RStdJsonToken",
                          "kind"),
        "");
    return LLVMBuildAnd(emitter->builder,
                        has,
                        LLVMBuildICmp(emitter->builder,
                                      equal ? LLVMIntEQ : LLVMIntNE,
                                      current,
                                      r_llvm_u32(emitter, value),
                                      ""),
                        "");
}

LLVMValueRef r_llvm_jd_text(RLlvmEmitter *emitter, LLVMValueRef cursor) {
    return r_llvm_std_member(emitter,
                             r_llvm_std_member(emitter, cursor, "RStdJsonCursor", "token"),
                             "RStdJsonToken",
                             "text");
}

LLVMValueRef r_llvm_jd_options(RLlvmEmitter *emitter, LLVMValueRef cursor, const char *member) {
    return LLVMBuildICmp(
        emitter->builder,
        LLVMIntNE,
        LLVMBuildLoad2(
            emitter->builder,
            r_llvm_int(emitter, 8U),
            r_llvm_std_member(emitter,
                              r_llvm_std_member(emitter, cursor, "RStdJsonCursor", "options"),
                              "RStdJsonOptions",
                              member),
            ""),
        LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
        "");
}

LLVMValueRef r_llvm_jd_allocator(RLlvmEmitter *emitter, LLVMValueRef cursor) {
    return LLVMBuildLoad2(emitter->builder,
                          r_llvm_pointer(emitter),
                          r_llvm_std_member(emitter, cursor, "RStdJsonCursor", "allocator"),
                          "");
}

/* Branches to `failure` unless `condition`. */
void r_llvm_jd_require(RLlvmEmitter *emitter, LLVMValueRef condition, LLVMBasicBlockRef failure) {
    LLVMBasicBlockRef next = r_llvm_jd_block(emitter);
    (void)LLVMBuildCondBr(emitter->builder, condition, next, failure);
    LLVMPositionBuilderAtEnd(emitter->builder, next);
}

/* Returns `value` from the decoder when `condition`. */
void r_llvm_jd_return_if(RLlvmEmitter *emitter, LLVMValueRef condition, LLVMValueRef value) {
    LLVMBasicBlockRef leave = r_llvm_jd_block(emitter);
    LLVMBasicBlockRef next = r_llvm_jd_block(emitter);
    (void)LLVMBuildCondBr(emitter->builder, condition, leave, next);
    LLVMPositionBuilderAtEnd(emitter->builder, leave);
    (void)LLVMBuildRet(emitter->builder, value);
    LLVMPositionBuilderAtEnd(emitter->builder, next);
}

LLVMValueRef r_llvm_jd_decode(RLlvmEmitter *emitter,
                              RTypeId type,
                              LLVMValueRef cursor,
                              LLVMValueRef out,
                              LLVMValueRef quoted,
                              LLVMValueRef defaulted) {
    LLVMValueRef function = r_llvm_json_decoder(emitter, type);
    LLVMValueRef arguments[4];

    if (function == NULL) {
        return NULL;
    }
    arguments[0] = cursor;
    arguments[1] = out;
    arguments[2] = quoted;
    arguments[3] = defaulted;
    return LLVMBuildCall2(
        emitter->builder, LLVMGlobalGetValueType(function), function, arguments, 4U, "");
}

bool r_llvm_jd_ordinary_call(RLlvmEmitter *emitter,
                             RTypeId type,
                             LLVMValueRef cursor,
                             LLVMValueRef out) {
    LLVMValueRef function = r_llvm_json_ordinary(emitter, type);
    LLVMValueRef arguments[2];

    if (function == NULL) {
        return false;
    }
    arguments[0] = cursor;
    arguments[1] = out;
    (void)LLVMBuildCall2(
        emitter->builder, LLVMGlobalGetValueType(function), function, arguments, 2U, "");
    return true;
}

LLVMValueRef r_llvm_jd_bool(RLlvmEmitter *emitter, bool value) {
    return value ? r_llvm_jd_true(emitter) : r_llvm_jd_false(emitter);
}

/* The failure of a carrier of a hook or factory into cursor->outcome: tag i + 1 is the i-th error
   of `throws`, an allocation error or a JSON error; branches to `failure` after it. */
static bool r_llvm_jd_carrier_failure(RLlvmEmitter *emitter,
                                      RTypeId throws,
                                      RTypeId carrier_type,
                                      LLVMValueRef carrier,
                                      LLVMValueRef cursor,
                                      LLVMBasicBlockRef failure) {
    LLVMValueRef outcome = r_llvm_std_member(emitter, cursor, "RStdJsonCursor", "outcome");
    LLVMValueRef tag = LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), carrier, "");
    LLVMBasicBlockRef next = r_llvm_jd_block(emitter);
    LLVMValueRef choice;
    uint32_t payload = 0U;
    uint32_t index;

    if (!r_llvm_payload_offset(emitter, carrier_type, &payload)) {
        return false;
    }
    choice = LLVMBuildSwitch(emitter->builder, tag, next, 0U);
    for (index = 0U; index < r_semantic_effect_count(emitter->frontend, throws); ++index) {
        const RTypeId error = r_semantic_effect_at(emitter->frontend, throws, index);
        const bool allocation = r_llvm_standard_named(emitter, error, "std.alloc::alloc_error");
        LLVMBasicBlockRef failed = r_llvm_jd_block(emitter);
        uint64_t status = 0U;
        uint32_t size = 0U;
        uint32_t align = 0U;
        LLVMAddCase(choice, r_llvm_u32(emitter, index + 1U), failed);
        LLVMPositionBuilderAtEnd(emitter->builder, failed);
        if (!r_llvm_jd_constant(emitter,
                                allocation ? "R_STD_JSON_CALL_ALLOCATION_ERROR"
                                           : "R_STD_JSON_CALL_JSON_ERROR",
                                &status) ||
            !r_llvm_runtime_layout(
                emitter, allocation ? "RStdAllocError" : "RStdJsonError", &size, &align)) {
            return false;
        }
        (void)LLVMBuildStore(emitter->builder,
                             r_llvm_u32(emitter, status),
                             r_llvm_std_member(emitter, outcome, "RStdJsonResult", "status"));
        (void)LLVMBuildMemCpy(
            emitter->builder,
            r_llvm_std_member(
                emitter, outcome, "RStdJsonResult", allocation ? "allocation_error" : "error"),
            align,
            r_llvm_byte_offset(emitter, carrier, payload),
            align,
            r_llvm_u64(emitter, size));
        (void)LLVMBuildBr(emitter->builder, failure);
    }
    LLVMPositionBuilderAtEnd(emitter->builder, next);
    return true;
}

/* Calls `function` (a hook or a factory) with `argument` (NULL for none) and moves its value into
   `out`; a checked failure goes to `failure` after cursor->outcome is set. */
bool r_llvm_jd_call_function(RLlvmEmitter *emitter,
                             RSymbolId id,
                             RTypeId type,
                             LLVMValueRef argument,
                             LLVMValueRef cursor,
                             LLVMValueRef out,
                             LLVMBasicBlockRef failure) {
    const RSemanticSymbol *symbol = &emitter->frontend->semantic_symbols[(size_t)id - 1U];
    LLVMValueRef arguments[2];
    unsigned count = 0U;

    if (emitter->functions[id] == NULL) {
        return r_llvm_unsupported(emitter, "a JSON hook or default that is not lowered");
    }
    if (symbol->throws_type == R_TYPE_ID_INVALID) {
        LLVMTypeRef scalar = r_llvm_scalar_type(emitter, type);
        LLVMValueRef call;
        if (scalar == NULL) {
            arguments[count++] = out;
        }
        if (argument != NULL) {
            arguments[count++] = argument;
        }
        call = LLVMBuildCall2(emitter->builder,
                              emitter->function_types[id],
                              emitter->functions[id],
                              arguments,
                              count,
                              "");
        if (scalar != NULL) {
            r_llvm_store_scalar(emitter, type, call, out);
        }
        return true;
    }
    {
        uint32_t size = 0U;
        uint32_t align = 0U;
        uint32_t payload = 0U;
        uint32_t value_size = 0U;
        uint32_t value_align = 0U;
        LLVMValueRef carrier;
        if (!r_llvm_layout(emitter, symbol->effect_carrier_type, &size, &align) ||
            !r_llvm_payload_offset(emitter, symbol->effect_carrier_type, &payload) ||
            !r_llvm_layout(emitter, type, &value_size, &value_align)) {
            return false;
        }
        carrier = r_llvm_entry_alloca(emitter, size, align, "carrier");
        (void)LLVMBuildMemSet(emitter->builder,
                              carrier,
                              LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                              r_llvm_u64(emitter, size),
                              align);
        arguments[count++] = carrier;
        if (argument != NULL) {
            arguments[count++] = argument;
        }
        (void)LLVMBuildCall2(emitter->builder,
                             emitter->function_types[id],
                             emitter->functions[id],
                             arguments,
                             count,
                             "");
        if (!r_llvm_jd_carrier_failure(emitter,
                                       symbol->throws_type,
                                       symbol->effect_carrier_type,
                                       carrier,
                                       cursor,
                                       failure)) {
            return false;
        }
        (void)LLVMBuildMemCpy(emitter->builder,
                              out,
                              value_align,
                              r_llvm_byte_offset(emitter, carrier, payload),
                              value_align,
                              r_llvm_u64(emitter, value_size));
    }
    return true;
}

/* ---- defaults ---- */

/* The JSON default of a field into `place` (r_c17_json_default); a failure goes to `failure`. */
static bool r_llvm_jd_default(RLlvmEmitter *emitter,
                              const RSemanticField *field,
                              LLVMValueRef cursor,
                              LLVMValueRef place,
                              LLVMBasicBlockRef failure) {
    if (field->json.default_kind == R_JSON_DEFAULT_STRING) {
        const RInternEntry *text =
            ((field->json.default_string == 0U) ||
             ((size_t)field->json.default_string > emitter->frontend->intern_count))
                ? NULL
                : &emitter->frontend->intern_entries[(size_t)field->json.default_string - 1U];
        LLVMValueRef view = r_llvm_std_structure(emitter, "RStdJsonByteView");
        LLVMValueRef arguments[3];
        if ((text == NULL) || (view == NULL)) {
            return text == NULL ? r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR) : false;
        }
        (void)LLVMBuildStore(emitter->builder,
                             text->length == 0U
                                 ? r_llvm_empty_string(emitter)
                                 : r_llvm_program_string(emitter, field->json.default_string),
                             r_llvm_std_member(emitter, view, "RStdJsonByteView", "data"));
        (void)LLVMBuildStore(emitter->builder,
                             r_llvm_u64(emitter, text->length),
                             r_llvm_std_member(emitter, view, "RStdJsonByteView", "length"));
        arguments[0] = cursor;
        arguments[1] = place;
        arguments[2] = view;
        r_llvm_jd_require(emitter,
                          r_llvm_jd_call(emitter, "r_json_cursor_default_string", arguments, 3U),
                          failure);
        return true;
    }
    if (field->json.default_kind == R_JSON_DEFAULT_SCALAR) {
        const RSemanticTypeKind kind = field->json.default_scalar_kind;
        const uint64_t bits = field->json.default_bits;
        LLVMTypeRef scalar = r_llvm_scalar_type(emitter, field->type);
        LLVMValueRef value;
        if (scalar == NULL) {
            return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
        if (kind == R_SEMANTIC_TYPE_BOOL) {
            value = LLVMConstInt(scalar, bits != 0U ? 1U : 0U, 0);
        } else if (kind == R_SEMANTIC_TYPE_F32) {
            value = LLVMConstBitCast(LLVMConstInt(r_llvm_int(emitter, 32U), bits & UINT32_MAX, 0),
                                     LLVMFloatTypeInContext(emitter->context));
        } else if (kind == R_SEMANTIC_TYPE_F64) {
            value = LLVMConstBitCast(LLVMConstInt(r_llvm_int(emitter, 64U), bits, 0),
                                     LLVMDoubleTypeInContext(emitter->context));
        } else {
            value = LLVMConstInt(scalar, bits, 0);
        }
        if (LLVMTypeOf(value) != scalar) {
            /* A C type of the field with the representation of the default's kind. */
            if ((LLVMGetTypeKind(scalar) == LLVMIntegerTypeKind) &&
                (LLVMGetTypeKind(LLVMTypeOf(value)) == LLVMIntegerTypeKind)) {
                value = LLVMConstInt(scalar, bits, 0);
            } else {
                value = LLVMBuildFPCast(emitter->builder, value, scalar, "");
            }
        }
        r_llvm_store_scalar(emitter, field->type, value, place);
        return true;
    }
    if (field->json.default_kind != R_JSON_DEFAULT_FACTORY) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    return r_llvm_jd_call_function(
        emitter, field->json.default_factory, field->type, NULL, cursor, place, failure);
}

/* ---- the body of a decoder ---- */

/* The decoder of a type with an unmarshal hook (r_c17_json_decode_hook). */
static bool r_llvm_jd_hook(RLlvmEmitter *emitter, RTypeId id, const RSemanticAggregate *aggregate) {
    LLVMValueRef cursor = LLVMGetParam(emitter->function, 0U);
    LLVMValueRef out = LLVMGetParam(emitter->function, 1U);
    LLVMValueRef quoted = LLVMGetParam(emitter->function, 2U);
    LLVMValueRef defaulted = LLVMGetParam(emitter->function, 3U);
    LLVMBasicBlockRef missing = r_llvm_jd_block(emitter);
    LLVMBasicBlockRef read = r_llvm_jd_block(emitter);
    LLVMBasicBlockRef failure = r_llvm_jd_block(emitter);
    LLVMValueRef tree;
    LLVMValueRef arguments[3];
    uint32_t size = 0U;
    uint32_t align = 0U;

    (void)LLVMBuildCondBr(emitter->builder, defaulted, missing, read);
    LLVMPositionBuilderAtEnd(emitter->builder, missing);
    if (aggregate->is_default_initializable && (aggregate->kind == R_SEMANTIC_AGGREGATE_STRUCT)) {
        if (!r_llvm_jd_ordinary_call(emitter, id, cursor, out)) {
            return false;
        }
        (void)LLVMBuildRet(emitter->builder, r_llvm_jd_true(emitter));
    } else {
        LLVMValueRef failed = r_llvm_jd_fail(emitter, cursor, "R_STD_JSON_ERROR_TYPE");
        if (failed == NULL) {
            return false;
        }
        (void)LLVMBuildRet(emitter->builder, failed);
    }
    LLVMPositionBuilderAtEnd(emitter->builder, read);
    if (!r_llvm_runtime_layout(emitter, "RStdJsonValue", &size, &align)) {
        return false;
    }
    tree = r_llvm_entry_alloca(emitter, size, align, "tree");
    arguments[0] = cursor;
    arguments[1] = tree;
    arguments[2] = quoted;
    r_llvm_jd_return_if(emitter,
                        LLVMBuildNot(emitter->builder,
                                     r_llvm_jd_call(emitter, "r_json_cursor_value", arguments, 3U),
                                     ""),
                        r_llvm_jd_false(emitter));
    if (!r_llvm_jd_call_function(
            emitter, aggregate->json_unmarshal_function, id, tree, cursor, out, failure) ||
        (r_llvm_call_runtime(emitter, "r_std_json_value_destroy", &tree, 1U, NULL) == NULL)) {
        return false;
    }
    (void)LLVMBuildRet(emitter->builder, r_llvm_jd_true(emitter));
    LLVMPositionBuilderAtEnd(emitter->builder, failure);
    if (r_llvm_call_runtime(emitter, "r_std_json_value_destroy", &tree, 1U, NULL) == NULL) {
        return false;
    }
    (void)LLVMBuildRet(emitter->builder, r_llvm_jd_false(emitter));
    return true;
}

/* Selects the field of `key` among `count` candidates in two passes (exact, then case-insensitive
   where allowed); refuses an ambiguous name; `selected` holds the index or -1. */

bool r_llvm_jd_select(RLlvmEmitter *emitter,
                      const RLlvmJdField *fields,
                      uint32_t count,
                      LLVMValueRef cursor,
                      LLVMValueRef key,
                      LLVMValueRef selected,
                      LLVMBasicBlockRef failure) {
    uint32_t pass;
    uint32_t index;

    for (pass = 0U; pass < 2U; ++pass) {
        LLVMBasicBlockRef after = NULL;
        if (pass == 1U) {
            LLVMBasicBlockRef second = r_llvm_jd_block(emitter);
            after = r_llvm_jd_block(emitter);
            (void)LLVMBuildCondBr(
                emitter->builder,
                LLVMBuildICmp(
                    emitter->builder,
                    LLVMIntEQ,
                    LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), selected, ""),
                    LLVMConstInt(r_llvm_int(emitter, 32U), UINT64_MAX, 1),
                    ""),
                second,
                after);
            LLVMPositionBuilderAtEnd(emitter->builder, second);
        }
        for (index = 0U; index < count; ++index) {
            const RSemanticField *field = fields[index].field;
            LLVMValueRef name;
            LLVMValueRef arguments[3];
            LLVMValueRef condition;
            LLVMBasicBlockRef take;
            LLVMBasicBlockRef next;
            if ((pass == 1U) && ((field->json.flags & R_JSON_FIELD_CASE) != 0U) &&
                !field->json.ignore_case) {
                continue;
            }
            take = r_llvm_jd_block(emitter);
            next = r_llvm_jd_block(emitter);
            name = r_llvm_std_structure(emitter, "RStdJsonByteView");
            {
                const uint32_t intern_id = r_llvm_json_field_key(field);
                const RInternEntry *entry =
                    &emitter->frontend->intern_entries[(size_t)intern_id - 1U];
                (void)LLVMBuildStore(emitter->builder,
                                     entry->length == 0U
                                         ? r_llvm_empty_string(emitter)
                                         : r_llvm_program_string(emitter, intern_id),
                                     r_llvm_std_member(emitter, name, "RStdJsonByteView", "data"));
                (void)LLVMBuildStore(
                    emitter->builder,
                    r_llvm_u64(emitter, entry->length),
                    r_llvm_std_member(emitter, name, "RStdJsonByteView", "length"));
            }
            arguments[0] = key;
            arguments[1] = name;
            arguments[2] = r_llvm_jd_bool(emitter, pass == 1U);
            condition = r_llvm_jd_call(emitter, "r_std_json_name_equal", arguments, 3U);
            if (condition == NULL) {
                return false;
            }
            if ((pass == 1U) && ((field->json.flags & R_JSON_FIELD_CASE) == 0U)) {
                condition = LLVMBuildAnd(emitter->builder,
                                         r_llvm_jd_options(emitter, cursor, "ignore_case"),
                                         condition,
                                         "");
            }
            (void)LLVMBuildCondBr(emitter->builder, condition, take, next);
            LLVMPositionBuilderAtEnd(emitter->builder, take);
            if (pass == 1U) {
                LLVMBasicBlockRef ambiguous = r_llvm_jd_block(emitter);
                LLVMBasicBlockRef unique = r_llvm_jd_block(emitter);
                (void)LLVMBuildCondBr(
                    emitter->builder,
                    LLVMBuildICmp(
                        emitter->builder,
                        LLVMIntNE,
                        LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), selected, ""),
                        LLVMConstInt(r_llvm_int(emitter, 32U), UINT64_MAX, 1),
                        ""),
                    ambiguous,
                    unique);
                LLVMPositionBuilderAtEnd(emitter->builder, ambiguous);
                if (r_llvm_jd_fail(emitter, cursor, "R_STD_JSON_ERROR_AMBIGUOUS_FIELD") == NULL) {
                    return false;
                }
                (void)LLVMBuildBr(emitter->builder, failure);
                LLVMPositionBuilderAtEnd(emitter->builder, unique);
            }
            (void)LLVMBuildStore(
                emitter->builder, r_llvm_u32(emitter, fields[index].index), selected);
            (void)LLVMBuildBr(emitter->builder, next);
            LLVMPositionBuilderAtEnd(emitter->builder, next);
        }
        if (pass == 1U) {
            (void)LLVMBuildBr(emitter->builder, after);
            LLVMPositionBuilderAtEnd(emitter->builder, after);
        }
    }
    return true;
}

LLVMValueRef r_llvm_jd_present(RLlvmEmitter *emitter, LLVMValueRef present, uint32_t index) {
    return r_llvm_byte_offset(emitter, present, index);
}

LLVMValueRef r_llvm_jd_is_present(RLlvmEmitter *emitter, LLVMValueRef present, uint32_t index) {
    return LLVMBuildICmp(emitter->builder,
                         LLVMIntNE,
                         LLVMBuildLoad2(emitter->builder,
                                        r_llvm_int(emitter, 8U),
                                        r_llvm_jd_present(emitter, present, index),
                                        ""),
                         LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                         "");
}

void r_llvm_jd_set_present(RLlvmEmitter *emitter, LLVMValueRef present, uint32_t index) {
    (void)LLVMBuildStore(emitter->builder,
                         LLVMConstInt(r_llvm_int(emitter, 8U), 1U, 0),
                         r_llvm_jd_present(emitter, present, index));
}

/* The selected index is present already, or unknown under reject_unknown_fields. */
bool r_llvm_jd_refusals(RLlvmEmitter *emitter,
                        LLVMValueRef cursor,
                        LLVMValueRef selected,
                        LLVMValueRef present,
                        LLVMBasicBlockRef failure) {
    LLVMValueRef index = LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), selected, "");
    LLVMValueRef known =
        LLVMBuildICmp(emitter->builder, LLVMIntSGE, index, r_llvm_u32(emitter, 0U), "");
    LLVMBasicBlockRef check = r_llvm_jd_block(emitter);
    LLVMBasicBlockRef duplicate = r_llvm_jd_block(emitter);
    LLVMBasicBlockRef unknown = r_llvm_jd_block(emitter);
    LLVMBasicBlockRef reject = r_llvm_jd_block(emitter);
    LLVMBasicBlockRef next = r_llvm_jd_block(emitter);

    (void)LLVMBuildCondBr(emitter->builder, known, check, unknown);
    LLVMPositionBuilderAtEnd(emitter->builder, check);
    {
        LLVMValueRef wide = LLVMBuildZExt(emitter->builder, index, r_llvm_int(emitter, 64U), "");
        LLVMValueRef flag =
            LLVMBuildGEP2(emitter->builder, r_llvm_int(emitter, 8U), present, &wide, 1U, "");
        (void)LLVMBuildCondBr(
            emitter->builder,
            LLVMBuildICmp(emitter->builder,
                          LLVMIntNE,
                          LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 8U), flag, ""),
                          LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                          ""),
            duplicate,
            next);
    }
    LLVMPositionBuilderAtEnd(emitter->builder, duplicate);
    if (r_llvm_jd_fail(emitter, cursor, "R_STD_JSON_ERROR_DUPLICATE_KEY") == NULL) {
        return false;
    }
    (void)LLVMBuildBr(emitter->builder, failure);
    LLVMPositionBuilderAtEnd(emitter->builder, unknown);
    (void)LLVMBuildCondBr(emitter->builder,
                          r_llvm_jd_options(emitter, cursor, "reject_unknown_fields"),
                          reject,
                          next);
    LLVMPositionBuilderAtEnd(emitter->builder, reject);
    if (r_llvm_jd_fail(emitter, cursor, "R_STD_JSON_ERROR_UNKNOWN_FIELD") == NULL) {
        return false;
    }
    (void)LLVMBuildBr(emitter->builder, failure);
    LLVMPositionBuilderAtEnd(emitter->builder, next);
    return true;
}

/* A field read when present (`missing` false) or for its default (r_c17_json_field_read). */
static bool r_llvm_jd_field_read(RLlvmEmitter *emitter,
                                 const RSemanticField *field,
                                 LLVMValueRef cursor,
                                 LLVMValueRef place,
                                 bool missing,
                                 LLVMBasicBlockRef failure) {
    LLVMValueRef read;

    if (missing && (field->json.default_kind != R_JSON_DEFAULT_NONE)) {
        return r_llvm_jd_default(emitter, field, cursor, place, failure);
    }
    if (missing && (field->is_protected || ((field->json.flags & R_JSON_FIELD_SKIP) != 0U)) &&
        r_semantic_type_has_default(emitter->frontend, field->type)) {
        return r_llvm_jd_ordinary_call(emitter, field->type, cursor, place);
    }
    read =
        r_llvm_jd_decode(emitter,
                         field->type,
                         cursor,
                         place,
                         r_llvm_jd_bool(emitter, (field->json.flags & R_JSON_FIELD_STRING) != 0U),
                         r_llvm_jd_bool(emitter, missing));
    if (read == NULL) {
        return false;
    }
    r_llvm_jd_require(emitter, read, failure);
    return true;
}

static bool
r_llvm_jd_embedded(RLlvmEmitter *emitter, RTypeId id, const RSemanticAggregate *aggregate);

static bool r_llvm_jd_struct(RLlvmEmitter *emitter, RTypeId id) {
    const RSemanticAggregate *aggregate = r_llvm_json_aggregate(emitter, id);
    LLVMValueRef cursor = LLVMGetParam(emitter->function, 0U);
    LLVMValueRef out = LLVMGetParam(emitter->function, 1U);
    LLVMValueRef defaulted = LLVMGetParam(emitter->function, 3U);
    LLVMBasicBlockRef failure;
    LLVMBasicBlockRef parse;
    LLVMBasicBlockRef defaults;
    LLVMValueRef present;
    LLVMValueRef selected;
    RLlvmJdField *fields;
    uint32_t candidates = 0U;
    uint32_t index;

    if (aggregate == NULL) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    for (index = 0U; index < aggregate->field_count; ++index) {
        const RSemanticField *field = r_llvm_field(emitter, aggregate->first_field + index + 1U);
        if ((field != NULL) && ((field->json.flags & R_JSON_FIELD_EMBED) != 0U)) {
            return r_llvm_jd_embedded(emitter, id, aggregate);
        }
    }
    if (aggregate->is_default_initializable) {
        LLVMBasicBlockRef missing = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef next = r_llvm_jd_block(emitter);
        (void)LLVMBuildCondBr(emitter->builder, defaulted, missing, next);
        LLVMPositionBuilderAtEnd(emitter->builder, missing);
        if (!r_llvm_jd_ordinary_call(emitter, id, cursor, out)) {
            return false;
        }
        (void)LLVMBuildRet(emitter->builder, r_llvm_jd_true(emitter));
        LLVMPositionBuilderAtEnd(emitter->builder, next);
    }
    if (aggregate->field_count == 0U) {
        /* An empty struct is the byte r_empty of its C type. */
        (void)LLVMBuildStore(emitter->builder, LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0), out);
    }
    present = r_llvm_entry_alloca(
        emitter, aggregate->field_count == 0U ? 1U : aggregate->field_count, 1U, "present");
    (void)LLVMBuildMemSet(
        emitter->builder,
        present,
        LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
        r_llvm_u64(emitter, aggregate->field_count == 0U ? 1U : aggregate->field_count),
        1U);
    selected = r_llvm_entry_alloca(emitter, 4U, 4U, "selected");
    fields = r_llvm_allocate(emitter, ((size_t)aggregate->field_count + 1U) * sizeof(*fields));
    if (fields == NULL) {
        return false;
    }
    for (index = 0U; index < aggregate->field_count; ++index) {
        const RSemanticField *field = r_llvm_field(emitter, aggregate->first_field + index + 1U);
        if (field == NULL) {
            r_llvm_free(emitter, fields);
            return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
        if (field->is_protected || ((field->json.flags & R_JSON_FIELD_SKIP) != 0U)) {
            continue;
        }
        fields[candidates].field = field;
        fields[candidates].index = index;
        candidates += 1U;
    }
    failure = r_llvm_jd_block(emitter);
    parse = r_llvm_jd_block(emitter);
    defaults = r_llvm_jd_block(emitter);
    (void)LLVMBuildCondBr(emitter->builder, defaulted, defaults, parse);
    LLVMPositionBuilderAtEnd(emitter->builder, parse);
    {
        LLVMBasicBlockRef header = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef body = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef exit = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef skip = r_llvm_jd_block(emitter);
        LLVMValueRef key;
        LLVMValueRef choice;
        r_llvm_jd_require(
            emitter, r_llvm_jd_token(emitter, cursor, "R_STD_JSON_TOKEN_OBJECT_BEGIN"), failure);
        r_llvm_jd_require(emitter, r_llvm_jd_next(emitter, cursor), failure);
        (void)LLVMBuildBr(emitter->builder, header);
        LLVMPositionBuilderAtEnd(emitter->builder, header);
        (void)LLVMBuildCondBr(emitter->builder,
                              r_llvm_jd_has(emitter, cursor, "R_STD_JSON_TOKEN_OBJECT_END", false),
                              body,
                              exit);
        LLVMPositionBuilderAtEnd(emitter->builder, body);
        (void)LLVMBuildStore(
            emitter->builder, LLVMConstInt(r_llvm_int(emitter, 32U), UINT64_MAX, 1), selected);
        /* The key view stays valid until the cursor moves on. */
        key = r_llvm_std_structure(emitter, "RStdJsonByteView");
        {
            uint32_t size = 0U;
            uint32_t align = 0U;
            if ((key == NULL) ||
                !r_llvm_runtime_layout(emitter, "RStdJsonByteView", &size, &align)) {
                r_llvm_free(emitter, fields);
                return false;
            }
            (void)LLVMBuildMemCpy(emitter->builder,
                                  key,
                                  align,
                                  r_llvm_jd_text(emitter, cursor),
                                  align,
                                  r_llvm_u64(emitter, size));
        }
        r_llvm_jd_require(
            emitter, r_llvm_jd_token(emitter, cursor, "R_STD_JSON_TOKEN_KEY"), failure);
        if (!r_llvm_jd_select(emitter, fields, candidates, cursor, key, selected, failure) ||
            !r_llvm_jd_refusals(emitter, cursor, selected, present, failure)) {
            r_llvm_free(emitter, fields);
            return false;
        }
        r_llvm_jd_require(emitter, r_llvm_jd_next(emitter, cursor), failure);
        choice = LLVMBuildSwitch(
            emitter->builder,
            LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), selected, ""),
            skip,
            candidates);
        for (index = 0U; index < candidates; ++index) {
            const RSemanticField *field = fields[index].field;
            const uint32_t field_id = aggregate->first_field + fields[index].index + 1U;
            LLVMBasicBlockRef read = r_llvm_jd_block(emitter);
            uint32_t offset = 0U;
            LLVMAddCase(choice, r_llvm_u32(emitter, fields[index].index), read);
            LLVMPositionBuilderAtEnd(emitter->builder, read);
            if (!r_llvm_field_offset(emitter, field_id, &offset) ||
                !r_llvm_jd_field_read(emitter,
                                      field,
                                      cursor,
                                      r_llvm_byte_offset(emitter, out, offset),
                                      false,
                                      failure)) {
                r_llvm_free(emitter, fields);
                return false;
            }
            r_llvm_jd_set_present(emitter, present, fields[index].index);
            (void)LLVMBuildBr(emitter->builder, header);
        }
        LLVMPositionBuilderAtEnd(emitter->builder, skip);
        r_llvm_jd_require(
            emitter, r_llvm_jd_call(emitter, "r_json_cursor_skip", &cursor, 1U), failure);
        (void)LLVMBuildBr(emitter->builder, header);
        LLVMPositionBuilderAtEnd(emitter->builder, exit);
        r_llvm_jd_require(
            emitter, r_llvm_jd_token(emitter, cursor, "R_STD_JSON_TOKEN_OBJECT_END"), failure);
        for (index = 0U; index < candidates; ++index) {
            const RSemanticField *field = fields[index].field;
            LLVMBasicBlockRef missing;
            LLVMBasicBlockRef next;
            if ((field->json.flags & R_JSON_FIELD_OPTIONAL) != 0U) {
                continue;
            }
            missing = r_llvm_jd_block(emitter);
            next = r_llvm_jd_block(emitter);
            (void)LLVMBuildCondBr(emitter->builder,
                                  r_llvm_jd_is_present(emitter, present, fields[index].index),
                                  next,
                                  missing);
            LLVMPositionBuilderAtEnd(emitter->builder, missing);
            if (r_llvm_jd_fail(emitter, cursor, "R_STD_JSON_ERROR_MISSING_FIELD") == NULL) {
                r_llvm_free(emitter, fields);
                return false;
            }
            (void)LLVMBuildBr(emitter->builder, failure);
            LLVMPositionBuilderAtEnd(emitter->builder, next);
        }
        (void)LLVMBuildBr(emitter->builder, defaults);
    }
    r_llvm_free(emitter, fields);
    LLVMPositionBuilderAtEnd(emitter->builder, defaults);
    for (index = 0U; index < aggregate->field_count; ++index) {
        const uint32_t field_id = aggregate->first_field + index + 1U;
        const RSemanticField *field = r_llvm_field(emitter, field_id);
        LLVMBasicBlockRef missing = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef next = r_llvm_jd_block(emitter);
        uint32_t offset = 0U;
        if ((field == NULL) || !r_llvm_field_offset(emitter, field_id, &offset)) {
            return field == NULL ? r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR) : false;
        }
        (void)LLVMBuildCondBr(
            emitter->builder, r_llvm_jd_is_present(emitter, present, index), next, missing);
        LLVMPositionBuilderAtEnd(emitter->builder, missing);
        if (!r_llvm_jd_field_read(
                emitter, field, cursor, r_llvm_byte_offset(emitter, out, offset), true, failure)) {
            return false;
        }
        r_llvm_jd_set_present(emitter, present, index);
        (void)LLVMBuildBr(emitter->builder, next);
        LLVMPositionBuilderAtEnd(emitter->builder, next);
    }
    {
        LLVMBasicBlockRef done = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef advance = r_llvm_jd_block(emitter);
        (void)LLVMBuildCondBr(emitter->builder, defaulted, done, advance);
        LLVMPositionBuilderAtEnd(emitter->builder, advance);
        (void)LLVMBuildCondBr(emitter->builder, r_llvm_jd_next(emitter, cursor), done, failure);
        LLVMPositionBuilderAtEnd(emitter->builder, done);
        (void)LLVMBuildRet(emitter->builder, r_llvm_jd_true(emitter));
    }
    LLVMPositionBuilderAtEnd(emitter->builder, failure);
    for (index = aggregate->field_count; index > 0U; --index) {
        const uint32_t field_id = aggregate->first_field + index;
        const RSemanticField *field = r_llvm_field(emitter, field_id);
        LLVMBasicBlockRef drop;
        LLVMBasicBlockRef next;
        uint32_t offset = 0U;
        if ((field == NULL) || !r_llvm_type_requires_drop(emitter, field->type)) {
            continue;
        }
        drop = r_llvm_jd_block(emitter);
        next = r_llvm_jd_block(emitter);
        if (!r_llvm_field_offset(emitter, field_id, &offset)) {
            return false;
        }
        (void)LLVMBuildCondBr(
            emitter->builder, r_llvm_jd_is_present(emitter, present, index - 1U), drop, next);
        LLVMPositionBuilderAtEnd(emitter->builder, drop);
        if (!r_llvm_call_drop(emitter, field->type, r_llvm_byte_offset(emitter, out, offset))) {
            return false;
        }
        (void)LLVMBuildBr(emitter->builder, next);
        LLVMPositionBuilderAtEnd(emitter->builder, next);
    }
    (void)LLVMBuildRet(emitter->builder, r_llvm_jd_false(emitter));
    return true;
}

/* ---- embedded fields (r_c17_json_decode_embedded) ---- */

/* A collector (the embedded dict or json value that receives unknown keys) initialized when absent.
 */
bool r_llvm_jd_collector(RLlvmEmitter *emitter,
                         const RSemanticField *field,
                         LLVMValueRef slot,
                         LLVMValueRef present_flag,
                         LLVMValueRef cursor,
                         LLVMBasicBlockRef failure) {
    const RSemanticType *type = r_llvm_type(emitter, r_llvm_value_type(emitter, field->type));
    LLVMBasicBlockRef create = r_llvm_jd_block(emitter);
    LLVMBasicBlockRef next = r_llvm_jd_block(emitter);

    if (type == NULL) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    (void)LLVMBuildCondBr(
        emitter->builder,
        LLVMBuildICmp(emitter->builder,
                      LLVMIntNE,
                      LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 8U), present_flag, ""),
                      LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                      ""),
        next,
        create);
    LLVMPositionBuilderAtEnd(emitter->builder, create);
    if (type->kind == R_SEMANTIC_TYPE_DICT) {
        LLVMValueRef arguments[3];
        arguments[0] = cursor;
        arguments[1] = slot;
        arguments[2] = r_llvm_type_info(emitter, type->second);
        if ((arguments[2] == NULL) ||
            (r_llvm_call_runtime(emitter, "r_json_cursor_dict_initialize", arguments, 3U, NULL) ==
             NULL)) {
            return false;
        }
    } else {
        LLVMValueRef allocator = r_llvm_jd_allocator(emitter, cursor);
        LLVMValueRef object = r_llvm_std_structure(emitter, "RStdJsonValueResult");
        LLVMBasicBlockRef failed = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef made = r_llvm_jd_block(emitter);
        uint32_t size = 0U;
        uint32_t align = 0U;
        if ((object == NULL) ||
            (r_llvm_call_runtime(emitter, "r_std_json_object", &allocator, 1U, object) == NULL) ||
            !r_llvm_runtime_layout(emitter, "RStdJsonResult", &size, &align)) {
            return false;
        }
        (void)LLVMBuildCondBr(
            emitter->builder,
            r_llvm_std_status_is(
                emitter,
                r_llvm_std_member(emitter, object, "RStdJsonValueResult", "outcome"),
                "RStdJsonResult",
                "status",
                "R_STD_JSON_CALL_SUCCESS"),
            made,
            failed);
        LLVMPositionBuilderAtEnd(emitter->builder, failed);
        (void)LLVMBuildMemCpy(emitter->builder,
                              r_llvm_std_member(emitter, cursor, "RStdJsonCursor", "outcome"),
                              align,
                              r_llvm_std_member(emitter, object, "RStdJsonValueResult", "outcome"),
                              align,
                              r_llvm_u64(emitter, size));
        (void)LLVMBuildBr(emitter->builder, failure);
        LLVMPositionBuilderAtEnd(emitter->builder, made);
        if (!r_llvm_runtime_layout(emitter, "RStdJsonValue", &size, &align)) {
            return false;
        }
        (void)LLVMBuildMemCpy(emitter->builder,
                              slot,
                              align,
                              r_llvm_std_member(emitter, object, "RStdJsonValueResult", "value"),
                              align,
                              r_llvm_u64(emitter, size));
    }
    (void)LLVMBuildStore(
        emitter->builder, LLVMConstInt(r_llvm_int(emitter, 8U), 1U, 0), present_flag);
    (void)LLVMBuildBr(emitter->builder, next);
    LLVMPositionBuilderAtEnd(emitter->builder, next);
    return true;
}

bool r_llvm_jd_embedded_defaults(RLlvmEmitter *emitter,
                                 const RSemanticAggregate *aggregate,
                                 const RLlvmJdSlots *slots,
                                 LLVMValueRef present,
                                 LLVMValueRef cursor,
                                 uint32_t parent,
                                 uint32_t first,
                                 uint32_t end,
                                 LLVMBasicBlockRef failure) {
    uint32_t index = first;

    while (index < end) {
        const RJsonSchemaField *entry = &slots->entries[index];
        const RSemanticField *field = &emitter->frontend->semantic_fields[entry->field];
        if (entry->parent != parent) {
            return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
        if (entry->embedded) {
            const RSemanticAggregate *child = r_llvm_json_aggregate(emitter, field->type);
            if (!r_llvm_jd_embedded_defaults(emitter,
                                             aggregate,
                                             slots,
                                             present,
                                             cursor,
                                             index,
                                             index + 1U,
                                             entry->end,
                                             failure)) {
                return false;
            }
            if (child == NULL) {
                return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
            }
            if (child->field_count == 0U) {
                (void)LLVMBuildStore(emitter->builder,
                                     LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                                     slots->addresses[index]);
            }
            r_llvm_jd_set_present(emitter, present, index);
        } else if (entry->collector) {
            if (!r_llvm_jd_collector(emitter,
                                     field,
                                     slots->addresses[index],
                                     r_llvm_jd_present(emitter, present, index),
                                     cursor,
                                     failure)) {
                return false;
            }
        } else {
            LLVMBasicBlockRef missing = r_llvm_jd_block(emitter);
            LLVMBasicBlockRef next = r_llvm_jd_block(emitter);
            (void)LLVMBuildCondBr(
                emitter->builder, r_llvm_jd_is_present(emitter, present, index), next, missing);
            LLVMPositionBuilderAtEnd(emitter->builder, missing);
            if (field->json.default_kind != R_JSON_DEFAULT_NONE) {
                if (!r_llvm_jd_default(emitter, field, cursor, slots->addresses[index], failure)) {
                    return false;
                }
            } else if (entry->excluded &&
                       r_semantic_type_has_default(emitter->frontend, field->type)) {
                if (!r_llvm_jd_ordinary_call(
                        emitter, field->type, cursor, slots->addresses[index])) {
                    return false;
                }
            } else {
                LLVMValueRef read = r_llvm_jd_decode(emitter,
                                                     field->type,
                                                     cursor,
                                                     slots->addresses[index],
                                                     r_llvm_jd_false(emitter),
                                                     r_llvm_jd_true(emitter));
                if (read == NULL) {
                    return false;
                }
                r_llvm_jd_require(emitter, read, failure);
            }
            r_llvm_jd_set_present(emitter, present, index);
            (void)LLVMBuildBr(emitter->builder, next);
            LLVMPositionBuilderAtEnd(emitter->builder, next);
        }
        index = entry->end;
    }
    return true;
}

/* An unknown key goes into the collector: a member of its dict, or of its JSON object. */
static bool r_llvm_jd_embedded_unknown(RLlvmEmitter *emitter,
                                       const RLlvmJdSlots *slots,
                                       uint32_t collector,
                                       LLVMValueRef present,
                                       LLVMValueRef cursor,
                                       LLVMValueRef selected,
                                       LLVMBasicBlockRef header,
                                       LLVMBasicBlockRef failure) {
    const RSemanticField *field =
        &emitter->frontend->semantic_fields[slots->entries[collector].field];
    const RSemanticType *type = r_llvm_type(emitter, r_llvm_value_type(emitter, field->type));
    const bool dict = (type != NULL) && (type->kind == R_SEMANTIC_TYPE_DICT);
    LLVMBasicBlockRef unknown = r_llvm_jd_block(emitter);
    LLVMBasicBlockRef next = r_llvm_jd_block(emitter);
    LLVMBasicBlockRef dropped = r_llvm_jd_block(emitter);
    LLVMValueRef key;
    LLVMValueRef member;
    LLVMValueRef arguments[4];
    uint32_t size = 0U;
    uint32_t align = 0U;

    if (type == NULL) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    (void)LLVMBuildCondBr(
        emitter->builder,
        LLVMBuildICmp(emitter->builder,
                      LLVMIntEQ,
                      LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), selected, ""),
                      LLVMConstInt(r_llvm_int(emitter, 32U), UINT64_MAX, 1),
                      ""),
        unknown,
        next);
    LLVMPositionBuilderAtEnd(emitter->builder, unknown);
    if (!r_llvm_jd_collector(emitter,
                             field,
                             slots->addresses[collector],
                             r_llvm_jd_present(emitter, present, collector),
                             cursor,
                             failure) ||
        !r_llvm_runtime_layout(emitter, "RStdString", &size, &align)) {
        return false;
    }
    key = r_llvm_entry_alloca(emitter, size, align, "key");
    arguments[0] = cursor;
    arguments[1] = key;
    arguments[2] = r_llvm_jd_text(emitter, cursor);
    r_llvm_jd_require(
        emitter, r_llvm_jd_call(emitter, "r_json_cursor_default_string", arguments, 3U), failure);
    {
        LLVMBasicBlockRef moved = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef refuse = r_llvm_jd_block(emitter);
        (void)LLVMBuildCondBr(emitter->builder, r_llvm_jd_next(emitter, cursor), moved, refuse);
        LLVMPositionBuilderAtEnd(emitter->builder, refuse);
        if (r_llvm_call_runtime(emitter, "r_std_string_destroy", &key, 1U, NULL) == NULL) {
            return false;
        }
        (void)LLVMBuildBr(emitter->builder, failure);
        LLVMPositionBuilderAtEnd(emitter->builder, moved);
    }
    if (!(dict ? r_llvm_layout(emitter, type->second, &size, &align)
               : r_llvm_runtime_layout(emitter, "RStdJsonValue", &size, &align))) {
        return false;
    }
    member = r_llvm_entry_alloca(emitter, size, align, "member");
    {
        LLVMValueRef read;
        LLVMBasicBlockRef got = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef refuse = r_llvm_jd_block(emitter);
        if (dict) {
            read = r_llvm_jd_decode(emitter,
                                    type->second,
                                    cursor,
                                    member,
                                    r_llvm_jd_false(emitter),
                                    r_llvm_jd_false(emitter));
        } else {
            arguments[0] = cursor;
            arguments[1] = member;
            arguments[2] = r_llvm_jd_false(emitter);
            read = r_llvm_jd_call(emitter, "r_json_cursor_value", arguments, 3U);
        }
        if (read == NULL) {
            return false;
        }
        (void)LLVMBuildCondBr(emitter->builder, read, got, refuse);
        LLVMPositionBuilderAtEnd(emitter->builder, refuse);
        if (r_llvm_call_runtime(emitter, "r_std_string_destroy", &key, 1U, NULL) == NULL) {
            return false;
        }
        (void)LLVMBuildBr(emitter->builder, failure);
        LLVMPositionBuilderAtEnd(emitter->builder, got);
    }
    {
        LLVMValueRef inserted;
        LLVMBasicBlockRef kept = r_llvm_jd_block(emitter);
        if (dict) {
            arguments[0] = cursor;
            arguments[1] = slots->addresses[collector];
            arguments[2] = key;
            arguments[3] = member;
            inserted = r_llvm_jd_call(emitter, "r_json_cursor_dict_insert", arguments, 4U);
        } else {
            LLVMValueRef view = r_llvm_std_structure(emitter, "RStdStringView");
            LLVMValueRef outcome = r_llvm_std_member(emitter, cursor, "RStdJsonCursor", "outcome");
            if ((view == NULL) ||
                (r_llvm_call_runtime(emitter, "r_std_string_as_str", &key, 1U, view) == NULL)) {
                return false;
            }
            arguments[0] = slots->addresses[collector];
            arguments[1] = view;
            arguments[2] = member;
            if (r_llvm_call_runtime(emitter, "r_std_json_insert", arguments, 3U, outcome) == NULL) {
                return false;
            }
            inserted = r_llvm_std_status_is(
                emitter, outcome, "RStdJsonResult", "status", "R_STD_JSON_CALL_SUCCESS");
        }
        if ((inserted == NULL) ||
            (r_llvm_call_runtime(emitter, "r_std_string_destroy", &key, 1U, NULL) == NULL)) {
            return false;
        }
        (void)LLVMBuildCondBr(emitter->builder, inserted, kept, dropped);
        LLVMPositionBuilderAtEnd(emitter->builder, kept);
        (void)LLVMBuildBr(emitter->builder, header);
    }
    LLVMPositionBuilderAtEnd(emitter->builder, dropped);
    if (!dict) {
        if (r_llvm_call_runtime(emitter, "r_std_json_value_destroy", &member, 1U, NULL) == NULL) {
            return false;
        }
    } else if (r_llvm_type_requires_drop(emitter, type->second) &&
               !r_llvm_call_drop(emitter, type->second, member)) {
        return false;
    }
    (void)LLVMBuildBr(emitter->builder, failure);
    LLVMPositionBuilderAtEnd(emitter->builder, next);
    return true;
}

static bool
r_llvm_jd_embedded(RLlvmEmitter *emitter, RTypeId id, const RSemanticAggregate *aggregate) {
    const uint32_t count = aggregate->json_field_count;
    LLVMValueRef cursor = LLVMGetParam(emitter->function, 0U);
    LLVMValueRef out = LLVMGetParam(emitter->function, 1U);
    LLVMValueRef defaulted = LLVMGetParam(emitter->function, 3U);
    LLVMBasicBlockRef failure = r_llvm_jd_block(emitter);
    LLVMValueRef present;
    LLVMValueRef selected;
    RLlvmJdSlots slots;
    RLlvmJdField *fields;
    uint32_t candidates = 0U;
    uint32_t collector = UINT32_MAX;
    uint32_t index;
    bool success = false;

    {
        LLVMBasicBlockRef missing = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef next = r_llvm_jd_block(emitter);
        (void)LLVMBuildCondBr(emitter->builder, defaulted, missing, next);
        LLVMPositionBuilderAtEnd(emitter->builder, missing);
        if (aggregate->is_default_initializable) {
            if (!r_llvm_jd_ordinary_call(emitter, id, cursor, out)) {
                return false;
            }
            (void)LLVMBuildRet(emitter->builder, r_llvm_jd_true(emitter));
        } else {
            LLVMValueRef failed = r_llvm_jd_fail(emitter, cursor, "R_STD_JSON_ERROR_TYPE");
            if (failed == NULL) {
                return false;
            }
            (void)LLVMBuildRet(emitter->builder, failed);
        }
        LLVMPositionBuilderAtEnd(emitter->builder, next);
    }
    present = r_llvm_entry_alloca(emitter, count == 0U ? 1U : count, 1U, "present");
    (void)LLVMBuildMemSet(emitter->builder,
                          present,
                          LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                          r_llvm_u64(emitter, count == 0U ? 1U : count),
                          1U);
    selected = r_llvm_entry_alloca(emitter, 4U, 4U, "selected");
    slots.entries = &emitter->frontend->json_fields[aggregate->first_json_field];
    slots.addresses = r_llvm_allocate(emitter, ((size_t)count + 1U) * sizeof(*slots.addresses));
    fields = r_llvm_allocate(emitter, ((size_t)count + 1U) * sizeof(*fields));
    if ((slots.addresses == NULL) || (fields == NULL)) {
        goto cleanup;
    }
    for (index = 0U; index < count; ++index) {
        const RJsonSchemaField *entry = &slots.entries[index];
        const RSemanticField *field = &emitter->frontend->semantic_fields[entry->field];
        const uint32_t field_id = entry->field + 1U;
        uint32_t offset = 0U;
        if (!r_llvm_field_offset(emitter, field_id, &offset)) {
            goto cleanup;
        }
        slots.addresses[index] = r_llvm_byte_offset(
            emitter, entry->parent == UINT32_MAX ? out : slots.addresses[entry->parent], offset);
        if (entry->collector) {
            collector = index;
        }
        if (entry->embedded || entry->collector || entry->excluded) {
            continue;
        }
        fields[candidates].field = field;
        fields[candidates].index = index;
        candidates += 1U;
    }
    {
        LLVMBasicBlockRef header = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef body = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef exit = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef skip = r_llvm_jd_block(emitter);
        LLVMValueRef key = r_llvm_std_structure(emitter, "RStdJsonByteView");
        LLVMValueRef choice;
        uint32_t size = 0U;
        uint32_t align = 0U;
        if ((key == NULL) || !r_llvm_runtime_layout(emitter, "RStdJsonByteView", &size, &align)) {
            goto cleanup;
        }
        r_llvm_jd_require(
            emitter, r_llvm_jd_token(emitter, cursor, "R_STD_JSON_TOKEN_OBJECT_BEGIN"), failure);
        r_llvm_jd_require(emitter, r_llvm_jd_next(emitter, cursor), failure);
        (void)LLVMBuildBr(emitter->builder, header);
        LLVMPositionBuilderAtEnd(emitter->builder, header);
        (void)LLVMBuildCondBr(emitter->builder,
                              r_llvm_jd_has(emitter, cursor, "R_STD_JSON_TOKEN_OBJECT_END", false),
                              body,
                              exit);
        LLVMPositionBuilderAtEnd(emitter->builder, body);
        (void)LLVMBuildStore(
            emitter->builder, LLVMConstInt(r_llvm_int(emitter, 32U), UINT64_MAX, 1), selected);
        (void)LLVMBuildMemCpy(emitter->builder,
                              key,
                              align,
                              r_llvm_jd_text(emitter, cursor),
                              align,
                              r_llvm_u64(emitter, size));
        r_llvm_jd_require(
            emitter, r_llvm_jd_token(emitter, cursor, "R_STD_JSON_TOKEN_KEY"), failure);
        if (!r_llvm_jd_select(emitter, fields, candidates, cursor, key, selected, failure) ||
            !r_llvm_jd_refusals(emitter, cursor, selected, present, failure)) {
            goto cleanup;
        }
        if ((collector != UINT32_MAX) &&
            !r_llvm_jd_embedded_unknown(
                emitter, &slots, collector, present, cursor, selected, header, failure)) {
            goto cleanup;
        }
        r_llvm_jd_require(emitter, r_llvm_jd_next(emitter, cursor), failure);
        choice = LLVMBuildSwitch(
            emitter->builder,
            LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), selected, ""),
            skip,
            candidates);
        for (index = 0U; index < candidates; ++index) {
            const RSemanticField *field = fields[index].field;
            const uint32_t slot = fields[index].index;
            LLVMBasicBlockRef read = r_llvm_jd_block(emitter);
            LLVMValueRef got;
            LLVMAddCase(choice, r_llvm_u32(emitter, slot), read);
            LLVMPositionBuilderAtEnd(emitter->builder, read);
            got = r_llvm_jd_decode(
                emitter,
                field->type,
                cursor,
                slots.addresses[slot],
                r_llvm_jd_bool(emitter, (field->json.flags & R_JSON_FIELD_STRING) != 0U),
                r_llvm_jd_false(emitter));
            if (got == NULL) {
                goto cleanup;
            }
            r_llvm_jd_require(emitter, got, failure);
            r_llvm_jd_set_present(emitter, present, slot);
            (void)LLVMBuildBr(emitter->builder, header);
        }
        LLVMPositionBuilderAtEnd(emitter->builder, skip);
        r_llvm_jd_require(
            emitter, r_llvm_jd_call(emitter, "r_json_cursor_skip", &cursor, 1U), failure);
        (void)LLVMBuildBr(emitter->builder, header);
        LLVMPositionBuilderAtEnd(emitter->builder, exit);
        r_llvm_jd_require(
            emitter, r_llvm_jd_token(emitter, cursor, "R_STD_JSON_TOKEN_OBJECT_END"), failure);
    }
    for (index = 0U; index < count; ++index) {
        const RJsonSchemaField *entry = &slots.entries[index];
        const RSemanticField *field = &emitter->frontend->semantic_fields[entry->field];
        LLVMBasicBlockRef missing;
        LLVMBasicBlockRef next;
        if (entry->embedded || entry->collector || entry->excluded ||
            ((field->json.flags & R_JSON_FIELD_OPTIONAL) != 0U)) {
            continue;
        }
        missing = r_llvm_jd_block(emitter);
        next = r_llvm_jd_block(emitter);
        (void)LLVMBuildCondBr(
            emitter->builder, r_llvm_jd_is_present(emitter, present, index), next, missing);
        LLVMPositionBuilderAtEnd(emitter->builder, missing);
        if (r_llvm_jd_fail(emitter, cursor, "R_STD_JSON_ERROR_MISSING_FIELD") == NULL) {
            goto cleanup;
        }
        (void)LLVMBuildBr(emitter->builder, failure);
        LLVMPositionBuilderAtEnd(emitter->builder, next);
    }
    if (!r_llvm_jd_embedded_defaults(
            emitter, aggregate, &slots, present, cursor, UINT32_MAX, 0U, count, failure)) {
        goto cleanup;
    }
    {
        LLVMBasicBlockRef done = r_llvm_jd_block(emitter);
        (void)LLVMBuildCondBr(emitter->builder, r_llvm_jd_next(emitter, cursor), done, failure);
        LLVMPositionBuilderAtEnd(emitter->builder, done);
        (void)LLVMBuildRet(emitter->builder, r_llvm_jd_true(emitter));
    }
    LLVMPositionBuilderAtEnd(emitter->builder, failure);
    for (index = count; index != 0U; --index) {
        const RJsonSchemaField *entry = &slots.entries[index - 1U];
        const RSemanticField *field = &emitter->frontend->semantic_fields[entry->field];
        LLVMValueRef condition;
        LLVMBasicBlockRef drop;
        LLVMBasicBlockRef next;
        uint32_t parent;
        if (!r_llvm_type_requires_drop(emitter, field->type)) {
            continue;
        }
        condition = r_llvm_jd_is_present(emitter, present, index - 1U);
        for (parent = entry->parent; parent != UINT32_MAX; parent = slots.entries[parent].parent) {
            condition = LLVMBuildAnd(
                emitter->builder,
                condition,
                LLVMBuildNot(emitter->builder, r_llvm_jd_is_present(emitter, present, parent), ""),
                "");
        }
        drop = r_llvm_jd_block(emitter);
        next = r_llvm_jd_block(emitter);
        (void)LLVMBuildCondBr(emitter->builder, condition, drop, next);
        LLVMPositionBuilderAtEnd(emitter->builder, drop);
        if (!r_llvm_call_drop(emitter, field->type, slots.addresses[index - 1U])) {
            goto cleanup;
        }
        (void)LLVMBuildBr(emitter->builder, next);
        LLVMPositionBuilderAtEnd(emitter->builder, next);
    }
    (void)LLVMBuildRet(emitter->builder, r_llvm_jd_false(emitter));
    success = true;

cleanup:
    r_llvm_free(emitter, slots.addresses);
    r_llvm_free(emitter, fields);
    return success;
}

/* ---- enums, sequences, collections ---- */

static bool r_llvm_jd_enum(RLlvmEmitter *emitter, RTypeId id) {
    const RSemanticAggregate *aggregate = r_llvm_json_aggregate(emitter, id);
    LLVMValueRef cursor = LLVMGetParam(emitter->function, 0U);
    LLVMValueRef out = LLVMGetParam(emitter->function, 1U);
    LLVMValueRef defaulted = LLVMGetParam(emitter->function, 3U);
    LLVMBasicBlockRef missing = r_llvm_jd_block(emitter);
    LLVMBasicBlockRef read = r_llvm_jd_block(emitter);
    LLVMValueRef object;
    uint32_t payload = 0U;
    uint32_t index;
    bool zero = false;

    if ((aggregate == NULL) ||
        (aggregate->is_tagged && !r_llvm_payload_offset(emitter, id, &payload))) {
        return aggregate == NULL ? r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR) : false;
    }
    (void)LLVMBuildCondBr(emitter->builder, defaulted, missing, read);
    LLVMPositionBuilderAtEnd(emitter->builder, missing);
    for (index = 0U; index < aggregate->variant_count; ++index) {
        const RSemanticVariant *variant =
            &emitter->frontend->semantic_variants[(size_t)aggregate->first_variant + index];
        if (variant->value != 0U) {
            continue;
        }
        zero = true;
        if (variant->payload_type != R_TYPE_ID_INVALID) {
            LLVMValueRef got = r_llvm_jd_decode(emitter,
                                                variant->payload_type,
                                                cursor,
                                                r_llvm_byte_offset(emitter, out, payload),
                                                r_llvm_jd_false(emitter),
                                                r_llvm_jd_true(emitter));
            if (got == NULL) {
                return false;
            }
            r_llvm_jd_return_if(
                emitter, LLVMBuildNot(emitter->builder, got, ""), r_llvm_jd_false(emitter));
        }
        if (aggregate->is_tagged) {
            (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, index), out);
        } else {
            r_llvm_store_scalar(
                emitter, id, LLVMConstInt(r_llvm_scalar_type(emitter, id), 0U, 0), out);
        }
        (void)LLVMBuildRet(emitter->builder, r_llvm_jd_true(emitter));
        break;
    }
    if (!zero) {
        LLVMValueRef failed = r_llvm_jd_fail(emitter, cursor, "R_STD_JSON_ERROR_TYPE");
        if (failed == NULL) {
            return false;
        }
        (void)LLVMBuildRet(emitter->builder, failed);
    }
    LLVMPositionBuilderAtEnd(emitter->builder, read);
    object = r_llvm_jd_has(emitter, cursor, "R_STD_JSON_TOKEN_OBJECT_BEGIN", true);
    {
        LLVMBasicBlockRef open = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef next = r_llvm_jd_block(emitter);
        LLVMValueRef expected;
        uint64_t key_kind = 0U;
        uint64_t string_kind = 0U;
        LLVMValueRef arguments[2];
        (void)LLVMBuildCondBr(emitter->builder, object, open, next);
        LLVMPositionBuilderAtEnd(emitter->builder, open);
        r_llvm_jd_return_if(emitter,
                            LLVMBuildNot(emitter->builder, r_llvm_jd_next(emitter, cursor), ""),
                            r_llvm_jd_false(emitter));
        (void)LLVMBuildBr(emitter->builder, next);
        LLVMPositionBuilderAtEnd(emitter->builder, next);
        if (!r_llvm_jd_constant(emitter, "R_STD_JSON_TOKEN_KEY", &key_kind) ||
            !r_llvm_jd_constant(emitter, "R_STD_JSON_TOKEN_STRING", &string_kind)) {
            return false;
        }
        expected = LLVMBuildSelect(emitter->builder,
                                   object,
                                   r_llvm_u32(emitter, key_kind),
                                   r_llvm_u32(emitter, string_kind),
                                   "");
        arguments[0] = cursor;
        arguments[1] = expected;
        r_llvm_jd_return_if(
            emitter,
            LLVMBuildNot(emitter->builder,
                         r_llvm_jd_call(emitter, "r_json_cursor_expect", arguments, 2U),
                         ""),
            r_llvm_jd_false(emitter));
    }
    for (index = 0U; index < aggregate->variant_count; ++index) {
        const RSemanticVariant *variant =
            &emitter->frontend->semantic_variants[(size_t)aggregate->first_variant + index];
        const RInternEntry *name =
            &emitter->frontend->intern_entries[(size_t)variant->name_intern_id - 1U];
        LLVMValueRef view = r_llvm_std_structure(emitter, "RStdJsonByteView");
        LLVMValueRef arguments[3];
        LLVMValueRef equal;
        LLVMBasicBlockRef match = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef next = r_llvm_jd_block(emitter);
        if (view == NULL) {
            return false;
        }
        (void)LLVMBuildStore(emitter->builder,
                             name->length == 0U
                                 ? r_llvm_empty_string(emitter)
                                 : r_llvm_program_string(emitter, variant->name_intern_id),
                             r_llvm_std_member(emitter, view, "RStdJsonByteView", "data"));
        (void)LLVMBuildStore(emitter->builder,
                             r_llvm_u64(emitter, name->length),
                             r_llvm_std_member(emitter, view, "RStdJsonByteView", "length"));
        arguments[0] = r_llvm_jd_text(emitter, cursor);
        arguments[1] = view;
        arguments[2] = r_llvm_jd_false(emitter);
        equal = r_llvm_jd_call(emitter, "r_std_json_name_equal", arguments, 3U);
        if (equal == NULL) {
            return false;
        }
        (void)LLVMBuildCondBr(emitter->builder, equal, match, next);
        LLVMPositionBuilderAtEnd(emitter->builder, match);
        {
            LLVMValueRef wrong = variant->payload_type == R_TYPE_ID_INVALID
                                     ? object
                                     : LLVMBuildNot(emitter->builder, object, "");
            LLVMBasicBlockRef refuse = r_llvm_jd_block(emitter);
            LLVMBasicBlockRef fine = r_llvm_jd_block(emitter);
            LLVMValueRef failed;
            (void)LLVMBuildCondBr(emitter->builder, wrong, refuse, fine);
            LLVMPositionBuilderAtEnd(emitter->builder, refuse);
            failed = r_llvm_jd_fail(emitter, cursor, "R_STD_JSON_ERROR_TYPE");
            if (failed == NULL) {
                return false;
            }
            (void)LLVMBuildRet(emitter->builder, failed);
            LLVMPositionBuilderAtEnd(emitter->builder, fine);
        }
        r_llvm_jd_return_if(emitter,
                            LLVMBuildNot(emitter->builder, r_llvm_jd_next(emitter, cursor), ""),
                            r_llvm_jd_false(emitter));
        if (variant->payload_type != R_TYPE_ID_INVALID) {
            LLVMValueRef slot = r_llvm_byte_offset(emitter, out, payload);
            LLVMValueRef got = r_llvm_jd_decode(emitter,
                                                variant->payload_type,
                                                cursor,
                                                slot,
                                                r_llvm_jd_false(emitter),
                                                r_llvm_jd_false(emitter));
            LLVMBasicBlockRef closed = r_llvm_jd_block(emitter);
            LLVMBasicBlockRef unclosed = r_llvm_jd_block(emitter);
            LLVMBasicBlockRef advance = r_llvm_jd_block(emitter);
            if (got == NULL) {
                return false;
            }
            r_llvm_jd_return_if(
                emitter, LLVMBuildNot(emitter->builder, got, ""), r_llvm_jd_false(emitter));
            (void)LLVMBuildCondBr(emitter->builder,
                                  r_llvm_jd_token(emitter, cursor, "R_STD_JSON_TOKEN_OBJECT_END"),
                                  advance,
                                  unclosed);
            LLVMPositionBuilderAtEnd(emitter->builder, advance);
            (void)LLVMBuildCondBr(
                emitter->builder, r_llvm_jd_next(emitter, cursor), closed, unclosed);
            LLVMPositionBuilderAtEnd(emitter->builder, unclosed);
            if (r_llvm_type_requires_drop(emitter, variant->payload_type) &&
                !r_llvm_call_drop(emitter, variant->payload_type, slot)) {
                return false;
            }
            (void)LLVMBuildRet(emitter->builder, r_llvm_jd_false(emitter));
            LLVMPositionBuilderAtEnd(emitter->builder, closed);
        }
        if (aggregate->is_tagged) {
            (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, index), out);
        } else {
            r_llvm_store_scalar(
                emitter, id, LLVMConstInt(r_llvm_scalar_type(emitter, id), variant->value, 0), out);
        }
        (void)LLVMBuildRet(emitter->builder, r_llvm_jd_true(emitter));
        LLVMPositionBuilderAtEnd(emitter->builder, next);
    }
    {
        LLVMValueRef failed = r_llvm_jd_fail(emitter, cursor, "R_STD_JSON_ERROR_TYPE");
        if (failed == NULL) {
            return false;
        }
        (void)LLVMBuildRet(emitter->builder, failed);
    }
    return true;
}

static bool r_llvm_jd_array(RLlvmEmitter *emitter, const RSemanticType *type) {
    const bool list = type->kind == R_SEMANTIC_TYPE_LIST;
    LLVMValueRef cursor = LLVMGetParam(emitter->function, 0U);
    LLVMValueRef out = LLVMGetParam(emitter->function, 1U);
    LLVMValueRef defaulted = LLVMGetParam(emitter->function, 3U);
    LLVMBasicBlockRef failure = r_llvm_jd_block(emitter);
    LLVMBasicBlockRef header = r_llvm_jd_block(emitter);
    LLVMBasicBlockRef body = r_llvm_jd_block(emitter);
    LLVMBasicBlockRef exit = r_llvm_jd_block(emitter);
    LLVMValueRef arguments[3];
    LLVMValueRef member;
    uint32_t size = 0U;
    uint32_t align = 0U;

    arguments[0] = out;
    arguments[1] = r_llvm_jd_allocator(emitter, cursor);
    arguments[2] = r_llvm_type_info(emitter, type->base);
    if ((arguments[2] == NULL) ||
        (r_llvm_call_runtime(emitter,
                             list ? "r_runtime_list_initialize" : "r_runtime_array_initialize",
                             arguments,
                             3U,
                             NULL) == NULL) ||
        !r_llvm_layout(emitter, type->base, &size, &align)) {
        return false;
    }
    r_llvm_jd_return_if(emitter, defaulted, r_llvm_jd_true(emitter));
    r_llvm_jd_require(
        emitter, r_llvm_jd_token(emitter, cursor, "R_STD_JSON_TOKEN_ARRAY_BEGIN"), failure);
    r_llvm_jd_require(emitter, r_llvm_jd_next(emitter, cursor), failure);
    member = r_llvm_entry_alloca(emitter, size, align, "member");
    (void)LLVMBuildBr(emitter->builder, header);
    LLVMPositionBuilderAtEnd(emitter->builder, header);
    (void)LLVMBuildCondBr(emitter->builder,
                          r_llvm_jd_has(emitter, cursor, "R_STD_JSON_TOKEN_ARRAY_END", false),
                          body,
                          exit);
    LLVMPositionBuilderAtEnd(emitter->builder, body);
    {
        LLVMValueRef got = r_llvm_jd_decode(emitter,
                                            type->base,
                                            cursor,
                                            member,
                                            r_llvm_jd_false(emitter),
                                            r_llvm_jd_false(emitter));
        LLVMBasicBlockRef pushed = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef refused = r_llvm_jd_block(emitter);
        if (got == NULL) {
            return false;
        }
        r_llvm_jd_require(emitter, got, failure);
        arguments[0] = cursor;
        arguments[1] = out;
        arguments[2] = member;
        (void)LLVMBuildCondBr(
            emitter->builder,
            r_llvm_jd_call(emitter,
                           list ? "r_json_cursor_list_push" : "r_json_cursor_array_push",
                           arguments,
                           3U),
            pushed,
            refused);
        LLVMPositionBuilderAtEnd(emitter->builder, refused);
        if (r_llvm_type_requires_drop(emitter, type->base) &&
            !r_llvm_call_drop(emitter, type->base, member)) {
            return false;
        }
        (void)LLVMBuildBr(emitter->builder, failure);
        LLVMPositionBuilderAtEnd(emitter->builder, pushed);
        (void)LLVMBuildBr(emitter->builder, header);
    }
    LLVMPositionBuilderAtEnd(emitter->builder, exit);
    {
        LLVMBasicBlockRef closed = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef done = r_llvm_jd_block(emitter);
        (void)LLVMBuildCondBr(emitter->builder,
                              r_llvm_jd_token(emitter, cursor, "R_STD_JSON_TOKEN_ARRAY_END"),
                              closed,
                              failure);
        LLVMPositionBuilderAtEnd(emitter->builder, closed);
        (void)LLVMBuildCondBr(emitter->builder, r_llvm_jd_next(emitter, cursor), done, failure);
        LLVMPositionBuilderAtEnd(emitter->builder, done);
        (void)LLVMBuildRet(emitter->builder, r_llvm_jd_true(emitter));
    }
    LLVMPositionBuilderAtEnd(emitter->builder, failure);
    if (r_llvm_call_runtime(
            emitter, list ? "r_runtime_list_destroy" : "r_runtime_array_destroy", &out, 1U, NULL) ==
        NULL) {
        return false;
    }
    (void)LLVMBuildRet(emitter->builder, r_llvm_jd_false(emitter));
    return true;
}

static bool r_llvm_jd_dict(RLlvmEmitter *emitter, const RSemanticType *type) {
    LLVMValueRef cursor = LLVMGetParam(emitter->function, 0U);
    LLVMValueRef out = LLVMGetParam(emitter->function, 1U);
    LLVMValueRef defaulted = LLVMGetParam(emitter->function, 3U);
    LLVMBasicBlockRef failure = r_llvm_jd_block(emitter);
    LLVMBasicBlockRef header = r_llvm_jd_block(emitter);
    LLVMBasicBlockRef body = r_llvm_jd_block(emitter);
    LLVMBasicBlockRef exit = r_llvm_jd_block(emitter);
    LLVMValueRef arguments[4];
    LLVMValueRef key;
    LLVMValueRef member;
    uint32_t size = 0U;
    uint32_t align = 0U;

    arguments[0] = cursor;
    arguments[1] = out;
    arguments[2] = r_llvm_type_info(emitter, type->second);
    if ((arguments[2] == NULL) ||
        (r_llvm_call_runtime(emitter, "r_json_cursor_dict_initialize", arguments, 3U, NULL) ==
         NULL)) {
        return false;
    }
    r_llvm_jd_return_if(emitter, defaulted, r_llvm_jd_true(emitter));
    r_llvm_jd_require(
        emitter, r_llvm_jd_token(emitter, cursor, "R_STD_JSON_TOKEN_OBJECT_BEGIN"), failure);
    r_llvm_jd_require(emitter, r_llvm_jd_next(emitter, cursor), failure);
    if (!r_llvm_runtime_layout(emitter, "RStdString", &size, &align)) {
        return false;
    }
    key = r_llvm_entry_alloca(emitter, size, align, "key");
    if (!r_llvm_layout(emitter, type->second, &size, &align)) {
        return false;
    }
    member = r_llvm_entry_alloca(emitter, size, align, "member");
    (void)LLVMBuildBr(emitter->builder, header);
    LLVMPositionBuilderAtEnd(emitter->builder, header);
    (void)LLVMBuildCondBr(emitter->builder,
                          r_llvm_jd_has(emitter, cursor, "R_STD_JSON_TOKEN_OBJECT_END", false),
                          body,
                          exit);
    LLVMPositionBuilderAtEnd(emitter->builder, body);
    r_llvm_jd_require(emitter, r_llvm_jd_token(emitter, cursor, "R_STD_JSON_TOKEN_KEY"), failure);
    arguments[0] = cursor;
    arguments[1] = key;
    arguments[2] = r_llvm_jd_text(emitter, cursor);
    r_llvm_jd_require(
        emitter, r_llvm_jd_call(emitter, "r_json_cursor_default_string", arguments, 3U), failure);
    {
        LLVMBasicBlockRef moved = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef read = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef refuse = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef inserted = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef rejected = r_llvm_jd_block(emitter);
        LLVMValueRef got;
        (void)LLVMBuildCondBr(emitter->builder, r_llvm_jd_next(emitter, cursor), moved, refuse);
        LLVMPositionBuilderAtEnd(emitter->builder, moved);
        got = r_llvm_jd_decode(emitter,
                               type->second,
                               cursor,
                               member,
                               r_llvm_jd_false(emitter),
                               r_llvm_jd_false(emitter));
        if (got == NULL) {
            return false;
        }
        (void)LLVMBuildCondBr(emitter->builder, got, read, refuse);
        LLVMPositionBuilderAtEnd(emitter->builder, refuse);
        if (r_llvm_call_runtime(emitter, "r_std_string_destroy", &key, 1U, NULL) == NULL) {
            return false;
        }
        (void)LLVMBuildBr(emitter->builder, failure);
        LLVMPositionBuilderAtEnd(emitter->builder, read);
        arguments[0] = cursor;
        arguments[1] = out;
        arguments[2] = key;
        arguments[3] = member;
        (void)LLVMBuildCondBr(emitter->builder,
                              r_llvm_jd_call(emitter, "r_json_cursor_dict_insert", arguments, 4U),
                              inserted,
                              rejected);
        LLVMPositionBuilderAtEnd(emitter->builder, rejected);
        if ((r_llvm_call_runtime(emitter, "r_std_string_destroy", &key, 1U, NULL) == NULL) ||
            (r_llvm_type_requires_drop(emitter, type->second) &&
             !r_llvm_call_drop(emitter, type->second, member))) {
            return false;
        }
        (void)LLVMBuildBr(emitter->builder, failure);
        LLVMPositionBuilderAtEnd(emitter->builder, inserted);
        (void)LLVMBuildBr(emitter->builder, header);
    }
    LLVMPositionBuilderAtEnd(emitter->builder, exit);
    {
        LLVMBasicBlockRef closed = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef done = r_llvm_jd_block(emitter);
        (void)LLVMBuildCondBr(emitter->builder,
                              r_llvm_jd_token(emitter, cursor, "R_STD_JSON_TOKEN_OBJECT_END"),
                              closed,
                              failure);
        LLVMPositionBuilderAtEnd(emitter->builder, closed);
        (void)LLVMBuildCondBr(emitter->builder, r_llvm_jd_next(emitter, cursor), done, failure);
        LLVMPositionBuilderAtEnd(emitter->builder, done);
        (void)LLVMBuildRet(emitter->builder, r_llvm_jd_true(emitter));
    }
    LLVMPositionBuilderAtEnd(emitter->builder, failure);
    if (r_llvm_call_runtime(emitter, "r_runtime_dict_destroy", &out, 1U, NULL) == NULL) {
        return false;
    }
    (void)LLVMBuildRet(emitter->builder, r_llvm_jd_false(emitter));
    return true;
}

static bool r_llvm_jd_fixed(RLlvmEmitter *emitter, const RSemanticType *type) {
    LLVMValueRef cursor = LLVMGetParam(emitter->function, 0U);
    LLVMValueRef out = LLVMGetParam(emitter->function, 1U);
    LLVMValueRef defaulted = LLVMGetParam(emitter->function, 3U);
    LLVMBasicBlockRef failure = r_llvm_jd_block(emitter);
    LLVMValueRef initialized = r_llvm_entry_alloca(emitter, 8U, 8U, "initialized");
    uint32_t size = 0U;
    uint32_t align = 0U;
    uint64_t index;

    if (!r_llvm_layout(emitter, type->base, &size, &align)) {
        return false;
    }
    (void)LLVMBuildStore(emitter->builder, r_llvm_u64(emitter, 0U), initialized);
    {
        LLVMBasicBlockRef open = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef next = r_llvm_jd_block(emitter);
        (void)LLVMBuildCondBr(emitter->builder, defaulted, next, open);
        LLVMPositionBuilderAtEnd(emitter->builder, open);
        r_llvm_jd_require(
            emitter, r_llvm_jd_token(emitter, cursor, "R_STD_JSON_TOKEN_ARRAY_BEGIN"), failure);
        r_llvm_jd_require(emitter, r_llvm_jd_next(emitter, cursor), failure);
        (void)LLVMBuildBr(emitter->builder, next);
        LLVMPositionBuilderAtEnd(emitter->builder, next);
    }
    for (index = 0U; index < type->length; ++index) {
        LLVMValueRef got =
            r_llvm_jd_decode(emitter,
                             type->base,
                             cursor,
                             r_llvm_byte_offset(emitter, out, (uint32_t)(index * size)),
                             r_llvm_jd_false(emitter),
                             defaulted);
        if (got == NULL) {
            return false;
        }
        r_llvm_jd_require(emitter, got, failure);
        (void)LLVMBuildStore(emitter->builder, r_llvm_u64(emitter, index + 1U), initialized);
    }
    {
        LLVMBasicBlockRef done = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef close = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef closed = r_llvm_jd_block(emitter);
        (void)LLVMBuildCondBr(emitter->builder, defaulted, done, close);
        LLVMPositionBuilderAtEnd(emitter->builder, close);
        (void)LLVMBuildCondBr(emitter->builder,
                              r_llvm_jd_token(emitter, cursor, "R_STD_JSON_TOKEN_ARRAY_END"),
                              closed,
                              failure);
        LLVMPositionBuilderAtEnd(emitter->builder, closed);
        (void)LLVMBuildCondBr(emitter->builder, r_llvm_jd_next(emitter, cursor), done, failure);
        LLVMPositionBuilderAtEnd(emitter->builder, done);
        (void)LLVMBuildRet(emitter->builder, r_llvm_jd_true(emitter));
    }
    LLVMPositionBuilderAtEnd(emitter->builder, failure);
    if (r_llvm_type_requires_drop(emitter, type->base)) {
        LLVMBasicBlockRef header = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef body = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef exit = r_llvm_jd_block(emitter);
        LLVMValueRef count;
        (void)LLVMBuildBr(emitter->builder, header);
        LLVMPositionBuilderAtEnd(emitter->builder, header);
        count = LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 64U), initialized, "");
        (void)LLVMBuildCondBr(
            emitter->builder,
            LLVMBuildICmp(emitter->builder, LLVMIntNE, count, r_llvm_u64(emitter, 0U), ""),
            body,
            exit);
        LLVMPositionBuilderAtEnd(emitter->builder, body);
        {
            LLVMValueRef last = LLVMBuildSub(emitter->builder, count, r_llvm_u64(emitter, 1U), "");
            LLVMValueRef offset =
                LLVMBuildMul(emitter->builder, last, r_llvm_u64(emitter, size), "");
            (void)LLVMBuildStore(emitter->builder, last, initialized);
            if (!r_llvm_call_drop(
                    emitter,
                    type->base,
                    LLVMBuildGEP2(
                        emitter->builder, r_llvm_int(emitter, 8U), out, &offset, 1U, ""))) {
                return false;
            }
        }
        (void)LLVMBuildBr(emitter->builder, header);
        LLVMPositionBuilderAtEnd(emitter->builder, exit);
    }
    (void)LLVMBuildRet(emitter->builder, r_llvm_jd_false(emitter));
    return true;
}

/* ---- scalars and the dispatch (r_c17_json_decode_body) ---- */

static bool r_llvm_jd_body(RLlvmEmitter *emitter, RTypeId id) {
    const RSemanticType *type = r_llvm_type(emitter, r_llvm_value_type(emitter, id));
    const RSemanticAggregate *custom = r_llvm_json_aggregate(emitter, id);
    LLVMValueRef cursor = LLVMGetParam(emitter->function, 0U);
    LLVMValueRef out = LLVMGetParam(emitter->function, 1U);
    LLVMValueRef quoted = LLVMGetParam(emitter->function, 2U);
    LLVMValueRef defaulted = LLVMGetParam(emitter->function, 3U);
    LLVMValueRef arguments[6];
    RSemanticTypeKind kind;
    RTokenKind c_token = R_TOKEN_INVALID;

    if (type == NULL) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    kind = type->kind;
    if (kind == R_SEMANTIC_TYPE_C_ABI_NUMERIC) {
        c_token = (RTokenKind)type->length;
    }
    if ((custom != NULL) && (custom->json_unmarshal_function != R_SYMBOL_ID_INVALID)) {
        return r_llvm_jd_hook(emitter, id, custom);
    }
    if (r_llvm_standard_named(emitter, id, "std.json::value")) {
        uint32_t size = 0U;
        uint32_t align = 0U;
        LLVMBasicBlockRef missing = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef read = r_llvm_jd_block(emitter);
        if (!r_llvm_runtime_layout(emitter, "RStdJsonValue", &size, &align)) {
            return false;
        }
        (void)LLVMBuildCondBr(emitter->builder, defaulted, missing, read);
        LLVMPositionBuilderAtEnd(emitter->builder, missing);
        (void)LLVMBuildMemSet(emitter->builder,
                              out,
                              LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                              r_llvm_u64(emitter, size),
                              align);
        (void)LLVMBuildRet(emitter->builder, r_llvm_jd_true(emitter));
        LLVMPositionBuilderAtEnd(emitter->builder, read);
        arguments[0] = cursor;
        arguments[1] = out;
        arguments[2] = quoted;
        (void)LLVMBuildRet(emitter->builder,
                           r_llvm_jd_call(emitter, "r_json_cursor_value", arguments, 3U));
        return true;
    }
    if (r_llvm_standard_named(emitter, id, "std.json::number")) {
        LLVMValueRef failed;
        LLVMBasicBlockRef missing = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef read = r_llvm_jd_block(emitter);
        (void)LLVMBuildCondBr(emitter->builder, defaulted, missing, read);
        LLVMPositionBuilderAtEnd(emitter->builder, missing);
        failed = r_llvm_jd_fail(emitter, cursor, "R_STD_JSON_ERROR_TYPE");
        if (failed == NULL) {
            return false;
        }
        (void)LLVMBuildRet(emitter->builder, failed);
        LLVMPositionBuilderAtEnd(emitter->builder, read);
        arguments[0] = cursor;
        arguments[1] = out;
        arguments[2] = quoted;
        (void)LLVMBuildRet(emitter->builder,
                           r_llvm_jd_call(emitter, "r_json_cursor_number", arguments, 3U));
        return true;
    }
    if ((kind == R_SEMANTIC_TYPE_ARRAY) || (kind == R_SEMANTIC_TYPE_LIST)) {
        return r_llvm_jd_array(emitter, type);
    }
    if (kind == R_SEMANTIC_TYPE_DICT) {
        return r_llvm_jd_dict(emitter, type);
    }
    if (kind == R_SEMANTIC_TYPE_ENUM) {
        return r_llvm_jd_enum(emitter, id);
    }
    if (kind == R_SEMANTIC_TYPE_STRUCT) {
        return r_llvm_jd_struct(emitter, id);
    }
    if (r_llvm_standard_named(emitter, id, "std.string::string")) {
        LLVMBasicBlockRef missing = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef read = r_llvm_jd_block(emitter);
        LLVMValueRef empty = r_llvm_std_structure(emitter, "RStdJsonByteView");
        if (empty == NULL) {
            return false;
        }
        (void)LLVMBuildCondBr(emitter->builder, defaulted, missing, read);
        LLVMPositionBuilderAtEnd(emitter->builder, missing);
        arguments[0] = cursor;
        arguments[1] = out;
        arguments[2] = empty;
        (void)LLVMBuildRet(emitter->builder,
                           r_llvm_jd_call(emitter, "r_json_cursor_default_string", arguments, 3U));
        LLVMPositionBuilderAtEnd(emitter->builder, read);
        arguments[0] = cursor;
        arguments[1] = out;
        (void)LLVMBuildRet(emitter->builder,
                           r_llvm_jd_call(emitter, "r_json_cursor_string", arguments, 2U));
        return true;
    }
    if (kind == R_SEMANTIC_TYPE_OPTION) {
        LLVMBasicBlockRef none = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef some = r_llvm_jd_block(emitter);
        LLVMValueRef got;
        uint32_t payload = 0U;
        if (!r_llvm_payload_offset(emitter, id, &payload)) {
            return false;
        }
        (void)LLVMBuildCondBr(
            emitter->builder,
            LLVMBuildOr(emitter->builder,
                        defaulted,
                        r_llvm_jd_has(emitter, cursor, "R_STD_JSON_TOKEN_NULL", true),
                        ""),
            none,
            some);
        LLVMPositionBuilderAtEnd(emitter->builder, none);
        (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 0U), out);
        {
            LLVMBasicBlockRef advance = r_llvm_jd_block(emitter);
            LLVMBasicBlockRef stay = r_llvm_jd_block(emitter);
            (void)LLVMBuildCondBr(emitter->builder, defaulted, stay, advance);
            LLVMPositionBuilderAtEnd(emitter->builder, stay);
            (void)LLVMBuildRet(emitter->builder, r_llvm_jd_true(emitter));
            LLVMPositionBuilderAtEnd(emitter->builder, advance);
            (void)LLVMBuildRet(emitter->builder, r_llvm_jd_next(emitter, cursor));
        }
        LLVMPositionBuilderAtEnd(emitter->builder, some);
        got = r_llvm_jd_decode(emitter,
                               type->base,
                               cursor,
                               r_llvm_byte_offset(emitter, out, payload),
                               quoted,
                               r_llvm_jd_false(emitter));
        if (got == NULL) {
            return false;
        }
        r_llvm_jd_return_if(
            emitter, LLVMBuildNot(emitter->builder, got, ""), r_llvm_jd_false(emitter));
        (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 1U), out);
        (void)LLVMBuildRet(emitter->builder, r_llvm_jd_true(emitter));
        return true;
    }
    if (kind == R_SEMANTIC_TYPE_FIXED_ARRAY) {
        return r_llvm_jd_fixed(emitter, type);
    }
    {
        LLVMTypeRef scalar = r_llvm_scalar_type(emitter, id);
        LLVMBasicBlockRef missing = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef read = r_llvm_jd_block(emitter);
        if (scalar == NULL) {
            return r_llvm_unsupported(emitter, "a JSON decoding of this type");
        }
        (void)LLVMBuildCondBr(emitter->builder, defaulted, missing, read);
        LLVMPositionBuilderAtEnd(emitter->builder, missing);
        r_llvm_store_scalar(emitter, id, LLVMConstNull(scalar), out);
        (void)LLVMBuildRet(emitter->builder, r_llvm_jd_true(emitter));
        LLVMPositionBuilderAtEnd(emitter->builder, read);
    }
    arguments[0] = cursor;
    if ((kind == R_SEMANTIC_TYPE_BOOL) || (c_token == R_TOKEN_KW_C_BOOL)) {
        arguments[1] = out;
        (void)LLVMBuildRet(emitter->builder,
                           r_llvm_jd_call(emitter, "r_json_cursor_boolean", arguments, 2U));
        return true;
    }
    if (kind == R_SEMANTIC_TYPE_CHAR) {
        arguments[1] = out;
        (void)LLVMBuildRet(emitter->builder,
                           r_llvm_jd_call(emitter, "r_json_cursor_char", arguments, 2U));
        return true;
    }
    if (c_token == R_TOKEN_KW_C_LONG_DOUBLE) {
        /* long double is the double of the target. */
        arguments[1] = quoted;
        arguments[2] = out;
        (void)LLVMBuildRet(emitter->builder,
                           r_llvm_jd_call(emitter, "r_json_cursor_long_double", arguments, 3U));
        return true;
    }
    if ((kind == R_SEMANTIC_TYPE_F32) || (kind == R_SEMANTIC_TYPE_F64) ||
        (c_token == R_TOKEN_KW_C_FLOAT) || (c_token == R_TOKEN_KW_C_DOUBLE)) {
        const bool single = (kind == R_SEMANTIC_TYPE_F32) || (c_token == R_TOKEN_KW_C_FLOAT);
        LLVMValueRef value = r_llvm_entry_alloca(emitter, 8U, 8U, "value");
        arguments[1] = quoted;
        arguments[2] = r_llvm_jd_bool(emitter, single);
        arguments[3] = value;
        r_llvm_jd_return_if(
            emitter,
            LLVMBuildNot(emitter->builder,
                         r_llvm_jd_call(emitter, "r_json_cursor_float", arguments, 4U),
                         ""),
            r_llvm_jd_false(emitter));
        r_llvm_store_scalar(
            emitter,
            id,
            LLVMBuildFPCast(
                emitter->builder,
                LLVMBuildLoad2(
                    emitter->builder, LLVMDoubleTypeInContext(emitter->context), value, ""),
                r_llvm_scalar_type(emitter, id),
                ""),
            out);
        (void)LLVMBuildRet(emitter->builder, r_llvm_jd_true(emitter));
        return true;
    }
    {
        const RSemanticTypeKind representation =
            r_semantic_integer_representation(emitter->frontend, id);
        const bool is_signed =
            (representation >= R_SEMANTIC_TYPE_I8) && (representation <= R_SEMANTIC_TYPE_ISIZE);
        const unsigned bits =
            ((representation == R_SEMANTIC_TYPE_I8) || (representation == R_SEMANTIC_TYPE_U8)) ? 8U
            : ((representation == R_SEMANTIC_TYPE_I16) || (representation == R_SEMANTIC_TYPE_U16))
                ? 16U
            : ((representation == R_SEMANTIC_TYPE_I32) || (representation == R_SEMANTIC_TYPE_U32))
                ? 32U
                : 64U;
        const uint64_t positive =
            is_signed ? (UINT64_C(1) << (bits - 1U)) - 1U : UINT64_MAX >> (64U - bits);
        const uint64_t minimum = is_signed ? UINT64_C(1) << (bits - 1U) : 0U;
        LLVMValueRef negative = r_llvm_entry_alloca(emitter, 1U, 1U, "negative");
        LLVMValueRef magnitude = r_llvm_entry_alloca(emitter, 8U, 8U, "magnitude");
        LLVMValueRef wide;
        LLVMTypeRef scalar = r_llvm_scalar_type(emitter, id);
        if ((representation == R_SEMANTIC_TYPE_INVALID) || (scalar == NULL)) {
            return r_llvm_unsupported(emitter, "a JSON decoding of this type");
        }
        arguments[1] = quoted;
        arguments[2] = r_llvm_u64(emitter, positive);
        arguments[3] = r_llvm_u64(emitter, minimum);
        arguments[4] = negative;
        arguments[5] = magnitude;
        r_llvm_jd_return_if(
            emitter,
            LLVMBuildNot(emitter->builder,
                         r_llvm_jd_call(emitter, "r_json_cursor_integer", arguments, 6U),
                         ""),
            r_llvm_jd_false(emitter));
        wide = LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 64U), magnitude, "");
        if (is_signed) {
            /* -(int64_t)(magnitude - 1) - 1 is the two's complement negation of the magnitude. */
            LLVMValueRef sign = LLVMBuildICmp(
                emitter->builder,
                LLVMIntNE,
                LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 8U), negative, ""),
                LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                "");
            wide =
                LLVMBuildSelect(emitter->builder,
                                sign,
                                LLVMBuildSub(emitter->builder, r_llvm_u64(emitter, 0U), wide, ""),
                                wide,
                                "");
        }
        r_llvm_store_scalar(emitter, id, LLVMBuildTrunc(emitter->builder, wide, scalar, ""), out);
        (void)LLVMBuildRet(emitter->builder, r_llvm_jd_true(emitter));
        return true;
    }
}

/* ---- ordinary defaults (r_c17_json_ordinary_default) ---- */

static bool r_llvm_jd_ordinary_body(RLlvmEmitter *emitter, RTypeId id) {
    const RSemanticType *type = r_llvm_type(emitter, r_llvm_value_type(emitter, id));
    LLVMValueRef cursor = LLVMGetParam(emitter->function, 0U);
    LLVMValueRef out = LLVMGetParam(emitter->function, 1U);
    uint32_t index;

    if (type == NULL) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    if (type->kind == R_SEMANTIC_TYPE_STRUCT) {
        const RSemanticAggregate *aggregate = r_llvm_json_aggregate(emitter, id);
        if (aggregate == NULL) {
            return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
        if (aggregate->field_count == 0U) {
            (void)LLVMBuildStore(
                emitter->builder, LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0), out);
        }
        for (index = 0U; index < aggregate->field_count; ++index) {
            const uint32_t field_id = aggregate->first_field + index + 1U;
            const RSemanticField *field = r_llvm_field(emitter, field_id);
            uint32_t offset = 0U;
            if ((field == NULL) || !r_llvm_field_offset(emitter, field_id, &offset) ||
                !r_llvm_jd_ordinary_call(
                    emitter, field->type, cursor, r_llvm_byte_offset(emitter, out, offset))) {
                return field == NULL ? r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR) : false;
            }
        }
    } else if (type->kind == R_SEMANTIC_TYPE_FIXED_ARRAY) {
        uint32_t size = 0U;
        uint32_t align = 0U;
        uint64_t element;
        if (!r_llvm_layout(emitter, type->base, &size, &align)) {
            return false;
        }
        for (element = 0U; element < type->length; ++element) {
            if (!r_llvm_jd_ordinary_call(
                    emitter,
                    type->base,
                    cursor,
                    r_llvm_byte_offset(emitter, out, (uint32_t)(element * size)))) {
                return false;
            }
        }
    } else if (type->kind == R_SEMANTIC_TYPE_OPTION) {
        (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 0U), out);
    } else if (type->kind == R_SEMANTIC_TYPE_ARRAY) {
        LLVMValueRef arguments[3];
        arguments[0] = out;
        arguments[1] = r_llvm_jd_allocator(emitter, cursor);
        arguments[2] = r_llvm_type_info(emitter, type->base);
        if ((arguments[2] == NULL) ||
            (r_llvm_call_runtime(emitter, "r_runtime_array_initialize", arguments, 3U, NULL) ==
             NULL)) {
            return false;
        }
    } else if (type->kind == R_SEMANTIC_TYPE_CONSTEXPR_STR) {
        (void)LLVMBuildStore(emitter->builder, r_llvm_empty_string(emitter), out);
        (void)LLVMBuildStore(
            emitter->builder, r_llvm_u64(emitter, 0U), r_llvm_byte_offset(emitter, out, 8U));
    } else if (r_llvm_scalar_type(emitter, id) != NULL) {
        r_llvm_store_scalar(emitter, id, LLVMConstNull(r_llvm_scalar_type(emitter, id)), out);
    } else {
        return r_llvm_unsupported(emitter, "an ordinary default of this type in a JSON decoder");
    }
    (void)LLVMBuildRetVoid(emitter->builder);
    return true;
}

/* ---- declarations, the entry and the post pass ---- */

static LLVMValueRef r_llvm_json_decoder(RLlvmEmitter *emitter, RTypeId type) {
    const RTypeId value = r_llvm_value_type(emitter, type);
    LLVMTypeRef parameters[4];
    char name[64];

    if ((value == R_TYPE_ID_INVALID) || ((size_t)value > emitter->frontend->semantic_type_count)) {
        (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        return NULL;
    }
    if (emitter->json_decoders[value] == NULL) {
        parameters[0] = r_llvm_pointer(emitter);
        parameters[1] = r_llvm_pointer(emitter);
        parameters[2] = r_llvm_int(emitter, 1U);
        parameters[3] = r_llvm_int(emitter, 1U);
        (void)snprintf(
            name, sizeof(name), "r_json_decode.%" PRIu32, r_llvm_type_key(emitter, value));
        emitter->json_decoders[value] = LLVMAddFunction(
            emitter->module, name, LLVMFunctionType(r_llvm_int(emitter, 1U), parameters, 4U, 0));
        LLVMSetLinkage(emitter->json_decoders[value], LLVMInternalLinkage);
    }
    return emitter->json_decoders[value];
}

static LLVMValueRef r_llvm_json_ordinary(RLlvmEmitter *emitter, RTypeId type) {
    const RTypeId value = r_llvm_value_type(emitter, type);
    LLVMTypeRef parameters[2];
    char name[64];

    if ((value == R_TYPE_ID_INVALID) || ((size_t)value > emitter->frontend->semantic_type_count)) {
        (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        return NULL;
    }
    if (emitter->json_ordinaries[value] == NULL) {
        parameters[0] = r_llvm_pointer(emitter);
        parameters[1] = r_llvm_pointer(emitter);
        (void)snprintf(
            name, sizeof(name), "r_json_default.%" PRIu32, r_llvm_type_key(emitter, value));
        emitter->json_ordinaries[value] = LLVMAddFunction(
            emitter->module,
            name,
            LLVMFunctionType(LLVMVoidTypeInContext(emitter->context), parameters, 2U, 0));
        LLVMSetLinkage(emitter->json_ordinaries[value], LLVMInternalLinkage);
    }
    return emitter->json_ordinaries[value];
}

/* std.json::unmarshal and unmarshal_with_options (r_c17_emit_json_unmarshal). */
bool r_llvm_define_json_unmarshal(RLlvmEmitter *emitter,
                                  RSymbolId id,
                                  const RSemanticSymbol *symbol) {
    LLVMValueRef cursor;
    LLVMValueRef value;
    LLVMValueRef options;
    LLVMValueRef built;
    LLVMValueRef status;
    LLVMValueRef carrier;
    LLVMValueRef arguments[4];
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
    cursor = r_llvm_std_structure(emitter, "RStdJsonCursor");
    options = r_llvm_std_structure(emitter, "RStdJsonOptions");
    if ((cursor == NULL) || (options == NULL) ||
        (symbol->effect_carrier_type == R_TYPE_ID_INVALID) ||
        !r_llvm_layout(emitter, symbol->json_type, &size, &align)) {
        return (cursor == NULL) || (options == NULL)
                   ? false
                   : r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    value = r_llvm_entry_alloca(emitter, size, align, "value");
    if (symbol->parameter_count == 2U) {
        if (!r_llvm_runtime_layout(emitter, "RStdJsonOptions", &size, &align)) {
            return false;
        }
        (void)LLVMBuildMemCpy(emitter->builder,
                              options,
                              align,
                              LLVMGetParam(emitter->function, 2U),
                              align,
                              r_llvm_u64(emitter, size));
    } else if (!r_llvm_json_default_options(emitter, options)) {
        return false;
    }
    arguments[0] = cursor;
    arguments[1] = r_llvm_std_allocator(emitter);
    /* The source {data, length} is the layout of RStdJsonByteView. */
    arguments[2] = LLVMGetParam(emitter->function, 1U);
    arguments[3] = options;
    if ((arguments[1] == NULL) ||
        (r_llvm_call_runtime(emitter,
                             "r_json_cursor_initialize",
                             arguments,
                             4U,
                             r_llvm_std_structure(emitter, "RStdJsonResult")) == NULL)) {
        return false;
    }
    built = r_llvm_jd_decode(emitter,
                             symbol->json_type,
                             cursor,
                             value,
                             r_llvm_jd_false(emitter),
                             r_llvm_jd_false(emitter));
    status = r_llvm_std_structure(emitter, "RStdJsonResult");
    if ((built == NULL) || (status == NULL) ||
        (r_llvm_call_runtime(emitter, "r_json_cursor_finish", &cursor, 1U, status) == NULL)) {
        return false;
    }
    {
        const RSemanticType *carrier_type =
            r_llvm_type(emitter, r_llvm_value_type(emitter, symbol->effect_carrier_type));
        LLVMBasicBlockRef succeeded = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef failed = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef allocation = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef other = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef done = r_llvm_jd_block(emitter);
        uint32_t payload = 0U;
        uint64_t allocation_status = 0U;
        if ((carrier_type == NULL) ||
            !r_llvm_payload_offset(emitter, symbol->effect_carrier_type, &payload) ||
            !r_llvm_jd_constant(emitter, "R_STD_JSON_CALL_ALLOCATION_ERROR", &allocation_status)) {
            return carrier_type == NULL ? r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR) : false;
        }
        (void)LLVMBuildCondBr(
            emitter->builder,
            r_llvm_std_status_is(
                emitter, status, "RStdJsonResult", "status", "R_STD_JSON_CALL_SUCCESS"),
            succeeded,
            failed);
        LLVMPositionBuilderAtEnd(emitter->builder, succeeded);
        if (!r_llvm_layout(emitter, symbol->json_type, &size, &align)) {
            return false;
        }
        (void)LLVMBuildMemCpy(emitter->builder,
                              r_llvm_byte_offset(emitter, carrier, payload),
                              align,
                              value,
                              align,
                              r_llvm_u64(emitter, size));
        (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 0U), carrier);
        (void)LLVMBuildBr(emitter->builder, done);
        LLVMPositionBuilderAtEnd(emitter->builder, failed);
        if (r_llvm_type_requires_drop(emitter, symbol->json_type)) {
            LLVMBasicBlockRef drop = r_llvm_jd_block(emitter);
            LLVMBasicBlockRef kept = r_llvm_jd_block(emitter);
            (void)LLVMBuildCondBr(emitter->builder, built, drop, kept);
            LLVMPositionBuilderAtEnd(emitter->builder, drop);
            if (!r_llvm_call_drop(emitter, symbol->json_type, value)) {
                return false;
            }
            (void)LLVMBuildBr(emitter->builder, kept);
            LLVMPositionBuilderAtEnd(emitter->builder, kept);
        }
        (void)LLVMBuildCondBr(
            emitter->builder,
            LLVMBuildICmp(
                emitter->builder,
                LLVMIntEQ,
                LLVMBuildLoad2(emitter->builder,
                               r_llvm_int(emitter, 32U),
                               r_llvm_std_member(emitter, status, "RStdJsonResult", "status"),
                               ""),
                r_llvm_u32(emitter, allocation_status),
                ""),
            allocation,
            other);
        LLVMPositionBuilderAtEnd(emitter->builder, allocation);
        if (!r_llvm_runtime_layout(emitter, "RStdAllocError", &size, &align)) {
            return false;
        }
        (void)LLVMBuildMemCpy(
            emitter->builder,
            r_llvm_byte_offset(emitter, carrier, payload),
            align,
            r_llvm_std_member(emitter, status, "RStdJsonResult", "allocation_error"),
            align,
            r_llvm_u64(emitter, size));
        (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 1U), carrier);
        (void)LLVMBuildBr(emitter->builder, done);
        LLVMPositionBuilderAtEnd(emitter->builder, other);
        if (!r_llvm_runtime_layout(emitter, "RStdJsonError", &size, &align)) {
            return false;
        }
        (void)LLVMBuildMemCpy(emitter->builder,
                              r_llvm_byte_offset(emitter, carrier, payload),
                              align,
                              r_llvm_std_member(emitter, status, "RStdJsonResult", "error"),
                              align,
                              r_llvm_u64(emitter, size));
        (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 2U), carrier);
        (void)LLVMBuildBr(emitter->builder, done);
        LLVMPositionBuilderAtEnd(emitter->builder, done);
    }
    (void)LLVMBuildRetVoid(emitter->builder);
    return emitter->status == R_FRONTEND_OK;
}

bool r_llvm_emit_json_decoders(RLlvmEmitter *emitter) {
    bool progress = true;

    while (progress) {
        size_t index;
        progress = false;
        for (index = 1U; index <= emitter->frontend->semantic_type_count; ++index) {
            const bool decoder = (emitter->json_decoders[index] != NULL) &&
                                 ((emitter->json_written[index] & 4U) == 0U);
            const bool ordinary = (emitter->json_ordinaries[index] != NULL) &&
                                  ((emitter->json_written[index] & 8U) == 0U);
            if (!decoder && !ordinary) {
                continue;
            }
            progress = true;
            emitter->json_written[index] |= decoder ? 4U : 8U;
            emitter->function =
                decoder ? emitter->json_decoders[index] : emitter->json_ordinaries[index];
            emitter->mir = NULL;
            emitter->frame = NULL;
            LLVMPositionBuilderAtEnd(
                emitter->builder,
                LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "entry"));
            if (decoder ? !r_llvm_jd_body(emitter, (RTypeId)index)
                        : !r_llvm_jd_ordinary_body(emitter, (RTypeId)index)) {
                return false;
            }
        }
    }
    return true;
}

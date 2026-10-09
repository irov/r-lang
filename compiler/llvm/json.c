#include "standard_internal.h"

#include <string.h>

/* JSON operations of std.json (R-SLIB-JSON-*), as the C17 emitter's r_c17_emit_json_function: a
   function whose MIR body is a placeholder is written from its operation. The tree operations
   call the library entry with the hosted allocator where it allocates, the bytes of a slice or
   string argument as an RStdJsonByteView, and a borrow or the address of a value otherwise; a
   checked result {outcome, value} becomes the carrier (tag 1 the allocation error, tag 2 the
   JSON error). APPEND and INSERT consume their last argument, whose remainder the library leaves
   to be destroyed. Marshalling (json_encode.c), unmarshalling (json_decode.c), stream decoders
   and readers (json_stream.c) have files of their own. */

bool r_llvm_json_supported(uint32_t operation) {
    return ((operation >= R_JSON_OPERATION_PARSE) && (operation <= R_JSON_OPERATION_TAKE_FIELD)) ||
           (operation == R_JSON_OPERATION_MARSHAL) ||
           (operation == R_JSON_OPERATION_MARSHAL_WITH_OPTIONS) ||
           (operation == R_JSON_OPERATION_UNMARSHAL) ||
           (operation == R_JSON_OPERATION_UNMARSHAL_WITH_OPTIONS) ||
           ((operation >= R_JSON_OPERATION_NEW_DECODER) &&
            (operation <= R_JSON_OPERATION_TAKE_BYTES)) ||
           (operation == R_JSON_OPERATION_SCHEMA);
}

/* R_STD_JSON_DEFAULT_OPTIONS of r_std_json.h, a macro the surface does not carry:
   {256, 64 MiB, 0, false, false, R_STD_JSON_MODE_DOCUMENT}. */
bool r_llvm_json_default_options(RLlvmEmitter *emitter, LLVMValueRef options) {
    int64_t document = 0;
    if (!r_llvm_runtime_constant(emitter, "R_STD_JSON_MODE_DOCUMENT", &document)) {
        return false;
    }
    (void)LLVMBuildStore(emitter->builder,
                         r_llvm_u64(emitter, 256U),
                         r_llvm_std_member(emitter, options, "RStdJsonOptions", "max_depth"));
    (void)LLVMBuildStore(emitter->builder,
                         r_llvm_u64(emitter, UINT64_C(64) * 1024U * 1024U),
                         r_llvm_std_member(emitter, options, "RStdJsonOptions", "max_value_bytes"));
    (void)LLVMBuildStore(emitter->builder,
                         r_llvm_u64(emitter, 0U),
                         r_llvm_std_member(emitter, options, "RStdJsonOptions", "indent"));
    (void)LLVMBuildStore(
        emitter->builder,
        LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
        r_llvm_std_member(emitter, options, "RStdJsonOptions", "reject_unknown_fields"));
    (void)LLVMBuildStore(emitter->builder,
                         LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                         r_llvm_std_member(emitter, options, "RStdJsonOptions", "ignore_case"));
    (void)LLVMBuildStore(emitter->builder,
                         r_llvm_u32(emitter, (uint64_t)document),
                         r_llvm_std_member(emitter, options, "RStdJsonOptions", "mode"));
    return true;
}

typedef struct RLlvmJsonCall {
    const char *name;
    const char *result_type; /* the library result of a checked operation */
    bool allocator;
} RLlvmJsonCall;

static bool r_llvm_json_call(RJsonOperation operation, RLlvmJsonCall *call) {
    static const struct {
        RJsonOperation operation;
        const char *name;
        const char *result_type;
        bool allocator;
    } calls[] = {
        {R_JSON_OPERATION_PARSE, "r_std_json_parse", "RStdJsonValueResult", true},
        {R_JSON_OPERATION_PARSE_WITH_OPTIONS,
         "r_std_json_parse_with_options",
         "RStdJsonValueResult",
         true},
        {R_JSON_OPERATION_STRINGIFY, "r_std_json_stringify", "RStdJsonStringResult", true},
        {R_JSON_OPERATION_STRINGIFY_WITH_OPTIONS,
         "r_std_json_stringify_with_options",
         "RStdJsonStringResult",
         true},
        {R_JSON_OPERATION_KIND, "r_std_json_kind", NULL, false},
        {R_JSON_OPERATION_LEN, "r_std_json_len", NULL, false},
        {R_JSON_OPERATION_TEXT, "r_std_json_text", NULL, false},
        {R_JSON_OPERATION_BOOLEAN, "r_std_json_boolean", NULL, false},
        {R_JSON_OPERATION_NULL, "r_std_json_null", NULL, false},
        {R_JSON_OPERATION_FROM_BOOL, "r_std_json_from_bool", "RStdJsonValueResult", true},
        {R_JSON_OPERATION_FROM_STRING, "r_std_json_from_string", "RStdJsonValueResult", true},
        {R_JSON_OPERATION_FROM_NUMBER, "r_std_json_from_number", "RStdJsonValueResult", true},
        {R_JSON_OPERATION_ARRAY, "r_std_json_array", "RStdJsonValueResult", true},
        {R_JSON_OPERATION_OBJECT, "r_std_json_object", "RStdJsonValueResult", true},
        {R_JSON_OPERATION_PARSE_NUMBER, "r_std_json_parse_number", "RStdJsonNumberResult", true},
        {R_JSON_OPERATION_NUMBER_TEXT, "r_std_json_number_text", NULL, false},
        {R_JSON_OPERATION_NUMBER_IS_ZERO, "r_std_json_number_is_zero", NULL, false},
        {R_JSON_OPERATION_NAME_EQUAL, "r_std_json_name_equal", NULL, false},
        {R_JSON_OPERATION_APPEND, "r_std_json_append", "RStdJsonResult", false},
        {R_JSON_OPERATION_INSERT, "r_std_json_insert", "RStdJsonResult", false},
        {R_JSON_OPERATION_GET, "r_std_json_get", NULL, false},
        {R_JSON_OPERATION_FIND, "r_std_json_find", NULL, false},
        {R_JSON_OPERATION_KEY_AT, "r_std_json_key_at", NULL, false},
        {R_JSON_OPERATION_TAKE_INDEX, "r_std_json_take_index", "RStdJsonValueResult", false},
        {R_JSON_OPERATION_TAKE_FIELD, "r_std_json_take_field", "RStdJsonValueResult", false},
    };
    size_t index;

    for (index = 0U; index < sizeof(calls) / sizeof(calls[0]); ++index) {
        if (calls[index].operation == operation) {
            call->name = calls[index].name;
            call->result_type = calls[index].result_type;
            call->allocator = calls[index].allocator;
            return true;
        }
    }
    return false;
}

/* The carrier of a checked operation from its library result: success (with the value unless
   `consumes`), the allocation error, or the JSON error. */
bool r_llvm_json_carrier(RLlvmEmitter *emitter,
                         const RSemanticSymbol *symbol,
                         LLVMValueRef carrier,
                         LLVMValueRef native,
                         const char *result_type,
                         bool consumes) {
    const RSemanticType *carrier_type =
        r_llvm_type(emitter, r_llvm_value_type(emitter, symbol->effect_carrier_type));
    LLVMValueRef outcome =
        consumes ? native : r_llvm_std_member(emitter, native, result_type, "outcome");
    LLVMValueRef status;
    LLVMBasicBlockRef succeeded =
        LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef allocation =
        LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef failed =
        LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMValueRef choice;
    int64_t success_status = 0;
    int64_t allocation_status = 0;
    uint32_t payload = 0U;
    uint32_t size = 0U;
    uint32_t align = 0U;

    if ((carrier_type == NULL) || (outcome == NULL) ||
        !r_llvm_runtime_constant(emitter, "R_STD_JSON_CALL_SUCCESS", &success_status) ||
        !r_llvm_runtime_constant(emitter, "R_STD_JSON_CALL_ALLOCATION_ERROR", &allocation_status) ||
        !r_llvm_payload_offset(emitter, symbol->effect_carrier_type, &payload)) {
        return (carrier_type == NULL) ? r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR) : false;
    }
    status = LLVMBuildLoad2(emitter->builder,
                            r_llvm_int(emitter, 32U),
                            r_llvm_std_member(emitter, outcome, "RStdJsonResult", "status"),
                            "");
    choice = LLVMBuildSwitch(emitter->builder, status, failed, 2U);
    LLVMAddCase(choice, r_llvm_u32(emitter, (uint64_t)success_status), succeeded);
    LLVMAddCase(choice, r_llvm_u32(emitter, (uint64_t)allocation_status), allocation);
    LLVMPositionBuilderAtEnd(emitter->builder, succeeded);
    (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 0U), carrier);
    if (!consumes && !r_llvm_type_is_void(emitter, carrier_type->base) &&
        !r_llvm_std_copy(emitter,
                         carrier_type->base,
                         r_llvm_byte_offset(emitter, carrier, payload),
                         r_llvm_std_member(emitter, native, result_type, "value"))) {
        return false;
    }
    (void)LLVMBuildBr(emitter->builder, done);
    LLVMPositionBuilderAtEnd(emitter->builder, allocation);
    (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 1U), carrier);
    if (!r_llvm_runtime_layout(emitter, "RStdAllocError", &size, &align)) {
        return false;
    }
    (void)LLVMBuildMemCpy(emitter->builder,
                          r_llvm_byte_offset(emitter, carrier, payload),
                          align,
                          r_llvm_std_member(emitter, outcome, "RStdJsonResult", "allocation_error"),
                          align,
                          r_llvm_u64(emitter, size));
    (void)LLVMBuildBr(emitter->builder, done);
    LLVMPositionBuilderAtEnd(emitter->builder, failed);
    (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 2U), carrier);
    if (!r_llvm_runtime_layout(emitter, "RStdJsonError", &size, &align)) {
        return false;
    }
    (void)LLVMBuildMemCpy(emitter->builder,
                          r_llvm_byte_offset(emitter, carrier, payload),
                          align,
                          r_llvm_std_member(emitter, outcome, "RStdJsonResult", "error"),
                          align,
                          r_llvm_u64(emitter, size));
    (void)LLVMBuildBr(emitter->builder, done);
    LLVMPositionBuilderAtEnd(emitter->builder, done);
    return true;
}

/* An RStdJsonByteView over program bytes in a new temporary. */
static LLVMValueRef r_llvm_json_view(RLlvmEmitter *emitter, uint32_t intern_id) {
    const RInternEntry *entry =
        ((intern_id == 0U) || ((size_t)intern_id > emitter->frontend->intern_count))
            ? NULL
            : &emitter->frontend->intern_entries[(size_t)intern_id - 1U];
    LLVMValueRef view = r_llvm_std_structure(emitter, "RStdJsonByteView");

    if ((entry == NULL) || (view == NULL)) {
        if (entry == NULL) {
            (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
        return NULL;
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

/* A program string "$defs" as a view; the intern table may not hold it. */
static LLVMValueRef r_llvm_json_defs_key(RLlvmEmitter *emitter) {
    LLVMValueRef view = r_llvm_std_structure(emitter, "RStdJsonByteView");

    if (view == NULL) {
        return NULL;
    }
    (void)LLVMBuildStore(emitter->builder,
                         LLVMBuildGlobalStringPtr(emitter->builder, "$defs", ""),
                         r_llvm_std_member(emitter, view, "RStdJsonByteView", "data"));
    (void)LLVMBuildStore(emitter->builder,
                         r_llvm_u64(emitter, 5U),
                         r_llvm_std_member(emitter, view, "RStdJsonByteView", "length"));
    return view;
}

static bool r_llvm_json_destroy_value(RLlvmEmitter *emitter, LLVMValueRef value) {
    return r_llvm_call_runtime(emitter, "r_std_json_value_destroy", &value, 1U, NULL) != NULL;
}

static LLVMValueRef r_llvm_json_outcome_ok(RLlvmEmitter *emitter, LLVMValueRef outcome) {
    return r_llvm_std_status_is(
        emitter, outcome, "RStdJsonResult", "status", "R_STD_JSON_CALL_SUCCESS");
}

/* M32.6 (R-SLIB-JSON-0002): std.json::schema::<T>() parses the schema text the semantic pass
   built for T; the entry of each type with a schema hook is the value of its hook, inserted
   under $defs. A hook throws the errors of the operation itself, so its carrier is the
   carrier of the operation (r_c17_emit_json_schema). */
static bool
r_llvm_define_json_schema(RLlvmEmitter *emitter, RSymbolId id, const RSemanticSymbol *symbol) {
    const RFrontendContext *context = emitter->frontend;
    LLVMValueRef carrier = LLVMGetParam(emitter->function, 0U);
    LLVMValueRef arguments[3];
    LLVMValueRef result = r_llvm_std_structure(emitter, "RStdJsonValueResult");
    LLVMValueRef outcome;
    LLVMValueRef value;
    uint32_t carrier_size = 0U;
    uint32_t carrier_align = 0U;
    uint32_t outcome_size = 0U;
    uint32_t outcome_align = 0U;
    size_t entry;
    bool hooked = false;

    for (entry = 0U; entry < context->json_schema_hook_count; ++entry) {
        hooked = hooked || (context->json_schema_hooks[entry].function == id);
    }
    arguments[0] = r_llvm_std_allocator(emitter);
    arguments[1] = r_llvm_json_view(emitter, symbol->json_schema_text);
    if ((result == NULL) || (arguments[0] == NULL) || (arguments[1] == NULL) ||
        (symbol->effect_carrier_type == R_TYPE_ID_INVALID) ||
        !r_llvm_layout(emitter, symbol->effect_carrier_type, &carrier_size, &carrier_align) ||
        !r_llvm_runtime_layout(emitter, "RStdJsonResult", &outcome_size, &outcome_align) ||
        (r_llvm_call_runtime(emitter, "r_std_json_parse", arguments, 2U, result) == NULL)) {
        return (symbol->effect_carrier_type == R_TYPE_ID_INVALID)
                   ? r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR)
                   : false;
    }
    outcome = r_llvm_std_member(emitter, result, "RStdJsonValueResult", "outcome");
    value = r_llvm_std_member(emitter, result, "RStdJsonValueResult", "value");
    if (hooked) {
        LLVMValueRef defs = r_llvm_std_structure(emitter, "RStdJsonValueResult");
        LLVMValueRef defs_outcome;
        LLVMValueRef defs_value;
        LLVMBasicBlockRef parsed =
            LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        LLVMBasicBlockRef taken_failed =
            LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        LLVMBasicBlockRef taken =
            LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        LLVMBasicBlockRef finished =
            LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        if (defs == NULL) {
            return false;
        }
        defs_outcome = r_llvm_std_member(emitter, defs, "RStdJsonValueResult", "outcome");
        defs_value = r_llvm_std_member(emitter, defs, "RStdJsonValueResult", "value");
        (void)LLVMBuildCondBr(
            emitter->builder, r_llvm_json_outcome_ok(emitter, outcome), parsed, finished);
        LLVMPositionBuilderAtEnd(emitter->builder, parsed);
        arguments[0] = value;
        arguments[1] = r_llvm_json_defs_key(emitter);
        if ((arguments[1] == NULL) ||
            (r_llvm_call_runtime(emitter, "r_std_json_take_field", arguments, 2U, defs) == NULL)) {
            return false;
        }
        (void)LLVMBuildCondBr(
            emitter->builder, r_llvm_json_outcome_ok(emitter, defs_outcome), taken, taken_failed);
        LLVMPositionBuilderAtEnd(emitter->builder, taken_failed);
        if (!r_llvm_json_destroy_value(emitter, value)) {
            return false;
        }
        (void)LLVMBuildMemCpy(emitter->builder,
                              outcome,
                              outcome_align,
                              defs_outcome,
                              outcome_align,
                              r_llvm_u64(emitter, outcome_size));
        (void)LLVMBuildBr(emitter->builder, finished);
        LLVMPositionBuilderAtEnd(emitter->builder, taken);
        for (entry = 0U; entry < context->json_schema_hook_count; ++entry) {
            const RJsonSchemaHook *hook = &context->json_schema_hooks[entry];
            const RSemanticSymbol *hook_symbol;
            LLVMValueRef hook_carrier;
            LLVMBasicBlockRef call;
            LLVMBasicBlockRef thrown;
            LLVMBasicBlockRef returned;
            LLVMBasicBlockRef refused;
            LLVMBasicBlockRef next;
            uint32_t payload = 0U;
            if (hook->function != id) {
                continue;
            }
            hook_symbol = ((hook->hook == R_SYMBOL_ID_INVALID) ||
                           ((size_t)hook->hook > context->semantic_symbol_count))
                              ? NULL
                              : &context->semantic_symbols[(size_t)hook->hook - 1U];
            if ((hook_symbol == NULL) ||
                (hook_symbol->effect_carrier_type != symbol->effect_carrier_type) ||
                !r_llvm_payload_offset(emitter, symbol->effect_carrier_type, &payload)) {
                return (hook_symbol == NULL) ||
                               (hook_symbol->effect_carrier_type != symbol->effect_carrier_type)
                           ? r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR)
                           : false;
            }
            if (emitter->functions[hook->hook] == NULL) {
                return r_llvm_unsupported(emitter, "a JSON schema hook that is not lowered");
            }
            call = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
            thrown = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
            returned = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
            refused = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
            next = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
            (void)LLVMBuildCondBr(
                emitter->builder, r_llvm_json_outcome_ok(emitter, defs_outcome), call, next);
            LLVMPositionBuilderAtEnd(emitter->builder, call);
            hook_carrier = r_llvm_entry_alloca(emitter, carrier_size, carrier_align, "hook");
            (void)LLVMBuildCall2(emitter->builder,
                                 emitter->function_types[hook->hook],
                                 emitter->functions[hook->hook],
                                 &hook_carrier,
                                 1U,
                                 "");
            (void)LLVMBuildCondBr(
                emitter->builder,
                LLVMBuildICmp(
                    emitter->builder,
                    LLVMIntNE,
                    LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), hook_carrier, ""),
                    r_llvm_u32(emitter, 0U),
                    ""),
                thrown,
                returned);
            LLVMPositionBuilderAtEnd(emitter->builder, thrown);
            if (!r_llvm_json_destroy_value(emitter, defs_value) ||
                !r_llvm_json_destroy_value(emitter, value)) {
                return false;
            }
            (void)LLVMBuildMemCpy(emitter->builder,
                                  carrier,
                                  carrier_align,
                                  hook_carrier,
                                  carrier_align,
                                  r_llvm_u64(emitter, carrier_size));
            (void)LLVMBuildRetVoid(emitter->builder);
            LLVMPositionBuilderAtEnd(emitter->builder, returned);
            arguments[0] = defs_value;
            arguments[1] = r_llvm_json_view(emitter, hook->name_intern_id);
            arguments[2] = r_llvm_byte_offset(emitter, hook_carrier, payload);
            {
                LLVMValueRef inserted = r_llvm_std_structure(emitter, "RStdJsonResult");
                if ((arguments[1] == NULL) || (inserted == NULL) ||
                    (r_llvm_call_runtime(emitter, "r_std_json_insert", arguments, 3U, inserted) ==
                     NULL)) {
                    return false;
                }
                (void)LLVMBuildCondBr(
                    emitter->builder, r_llvm_json_outcome_ok(emitter, inserted), next, refused);
                LLVMPositionBuilderAtEnd(emitter->builder, refused);
                if (!r_llvm_json_destroy_value(
                        emitter, r_llvm_byte_offset(emitter, hook_carrier, payload))) {
                    return false;
                }
                (void)LLVMBuildMemCpy(emitter->builder,
                                      defs_outcome,
                                      outcome_align,
                                      inserted,
                                      outcome_align,
                                      r_llvm_u64(emitter, outcome_size));
                (void)LLVMBuildBr(emitter->builder, next);
            }
            LLVMPositionBuilderAtEnd(emitter->builder, next);
        }
        {
            LLVMBasicBlockRef back =
                LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
            LLVMBasicBlockRef dropped =
                LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
            LLVMBasicBlockRef refused =
                LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
            LLVMValueRef restored = r_llvm_std_structure(emitter, "RStdJsonResult");
            if (restored == NULL) {
                return false;
            }
            (void)LLVMBuildCondBr(
                emitter->builder, r_llvm_json_outcome_ok(emitter, defs_outcome), back, dropped);
            LLVMPositionBuilderAtEnd(emitter->builder, back);
            arguments[0] = value;
            arguments[1] = r_llvm_json_defs_key(emitter);
            arguments[2] = defs_value;
            if ((arguments[1] == NULL) ||
                (r_llvm_call_runtime(emitter, "r_std_json_insert", arguments, 3U, restored) ==
                 NULL)) {
                return false;
            }
            (void)LLVMBuildCondBr(
                emitter->builder, r_llvm_json_outcome_ok(emitter, restored), finished, refused);
            LLVMPositionBuilderAtEnd(emitter->builder, refused);
            if (!r_llvm_json_destroy_value(emitter, defs_value) ||
                !r_llvm_json_destroy_value(emitter, value)) {
                return false;
            }
            (void)LLVMBuildMemCpy(emitter->builder,
                                  outcome,
                                  outcome_align,
                                  restored,
                                  outcome_align,
                                  r_llvm_u64(emitter, outcome_size));
            (void)LLVMBuildBr(emitter->builder, finished);
            LLVMPositionBuilderAtEnd(emitter->builder, dropped);
            if (!r_llvm_json_destroy_value(emitter, defs_value) ||
                !r_llvm_json_destroy_value(emitter, value)) {
                return false;
            }
            (void)LLVMBuildMemCpy(emitter->builder,
                                  outcome,
                                  outcome_align,
                                  defs_outcome,
                                  outcome_align,
                                  r_llvm_u64(emitter, outcome_size));
            (void)LLVMBuildBr(emitter->builder, finished);
        }
        LLVMPositionBuilderAtEnd(emitter->builder, finished);
    }
    if (!r_llvm_json_carrier(emitter, symbol, carrier, result, "RStdJsonValueResult", false)) {
        return false;
    }
    (void)LLVMBuildRetVoid(emitter->builder);
    return emitter->status == R_FRONTEND_OK;
}

bool r_llvm_define_json_function(RLlvmEmitter *emitter,
                                 RSymbolId id,
                                 const RSemanticSymbol *symbol) {
    const RJsonOperation operation = (RJsonOperation)symbol->json_operation;
    const bool consumes =
        (operation == R_JSON_OPERATION_APPEND) || (operation == R_JSON_OPERATION_INSERT);
    const bool in_memory = (symbol->effect_carrier_type != R_TYPE_ID_INVALID) ||
                           (!r_llvm_type_is_void(emitter, symbol->return_type) &&
                            (r_llvm_scalar_type(emitter, symbol->return_type) == NULL));
    const unsigned first = in_memory ? 1U : 0U;
    LLVMValueRef arguments[4];
    LLVMValueRef native = NULL;
    LLVMValueRef result = NULL;
    LLVMBasicBlockRef entry;
    RLlvmJsonCall call;
    unsigned count = 0U;
    uint32_t index;

    if ((operation == R_JSON_OPERATION_MARSHAL) ||
        (operation == R_JSON_OPERATION_MARSHAL_WITH_OPTIONS)) {
        return r_llvm_define_json_marshal(emitter, id, symbol);
    }
    if ((operation == R_JSON_OPERATION_UNMARSHAL) ||
        (operation == R_JSON_OPERATION_UNMARSHAL_WITH_OPTIONS)) {
        return r_llvm_define_json_unmarshal(emitter, id, symbol);
    }
    if ((operation >= R_JSON_OPERATION_NEW_DECODER) && (operation <= R_JSON_OPERATION_TAKE_BYTES)) {
        return r_llvm_define_json_stream(emitter, id, symbol);
    }
    if (operation == R_JSON_OPERATION_SCHEMA) {
        emitter->mir = NULL;
        emitter->frame = NULL;
        emitter->symbol = symbol;
        emitter->symbol_id = id;
        emitter->function = emitter->functions[id];
        LLVMPositionBuilderAtEnd(
            emitter->builder,
            LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "entry"));
        return r_llvm_define_json_schema(emitter, id, symbol);
    }
    if (!r_llvm_json_call(operation, &call) || (symbol->parameter_count > 3U)) {
        return r_llvm_unsupported(emitter, "this JSON operation");
    }
    emitter->mir = NULL;
    emitter->frame = NULL;
    emitter->symbol = symbol;
    emitter->symbol_id = id;
    emitter->function = emitter->functions[id];
    entry = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "entry");
    LLVMPositionBuilderAtEnd(emitter->builder, entry);
    if (call.allocator) {
        arguments[count] = r_llvm_std_allocator(emitter);
        if (arguments[count] == NULL) {
            return false;
        }
        count += 1U;
    }
    /* A slice or a string arrives as the address of {data, length}, the layout of
       RStdJsonByteView; anything else as it is. */
    for (index = 0U; index < symbol->parameter_count; ++index) {
        arguments[count++] = LLVMGetParam(emitter->function, first + index);
    }
    if (call.result_type != NULL) {
        native = r_llvm_std_structure(emitter, call.result_type);
        if ((native == NULL) ||
            (r_llvm_call_runtime(emitter, call.name, arguments, count, native) == NULL)) {
            return false;
        }
        if (consumes) {
            LLVMValueRef consumed =
                LLVMGetParam(emitter->function, first + symbol->parameter_count - 1U);
            if (r_llvm_call_runtime(emitter, "r_std_json_value_destroy", &consumed, 1U, NULL) ==
                NULL) {
                return false;
            }
        }
        if (symbol->effect_carrier_type == R_TYPE_ID_INVALID) {
            return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
        if (!r_llvm_json_carrier(emitter,
                                 symbol,
                                 LLVMGetParam(emitter->function, 0U),
                                 native,
                                 call.result_type,
                                 consumes)) {
            return false;
        }
        (void)LLVMBuildRetVoid(emitter->builder);
        return emitter->status == R_FRONTEND_OK;
    }
    if ((operation == R_JSON_OPERATION_GET) || (operation == R_JSON_OPERATION_FIND)) {
        /* An option of the borrow: some while the library found the value. */
        LLVMValueRef found = r_llvm_call_runtime(emitter, call.name, arguments, count, NULL);
        uint32_t payload = 0U;
        if ((found == NULL) || !in_memory ||
            !r_llvm_payload_offset(emitter, symbol->return_type, &payload)) {
            return (found == NULL) ? false : r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
        result = LLVMGetParam(emitter->function, 0U);
        (void)LLVMBuildStore(emitter->builder,
                             LLVMBuildZExt(emitter->builder,
                                           LLVMBuildIsNotNull(emitter->builder, found, ""),
                                           r_llvm_int(emitter, 32U),
                                           ""),
                             result);
        (void)LLVMBuildStore(emitter->builder, found, r_llvm_byte_offset(emitter, result, payload));
        (void)LLVMBuildRetVoid(emitter->builder);
        return emitter->status == R_FRONTEND_OK;
    }
    if (in_memory) {
        /* A view {data, length} or a value written to the caller's memory. */
        if (r_llvm_call_runtime(
                emitter, call.name, arguments, count, LLVMGetParam(emitter->function, 0U)) ==
            NULL) {
            return false;
        }
        (void)LLVMBuildRetVoid(emitter->builder);
        return emitter->status == R_FRONTEND_OK;
    }
    result = r_llvm_call_runtime(emitter, call.name, arguments, count, NULL);
    if (result == NULL) {
        return false;
    }
    if (r_llvm_type_is_void(emitter, symbol->return_type)) {
        (void)LLVMBuildRetVoid(emitter->builder);
    } else {
        LLVMTypeRef type = r_llvm_scalar_type(emitter, symbol->return_type);
        if (LLVMTypeOf(result) != type) {
            result = LLVMBuildIntCast2(emitter->builder, result, type, 0, "");
        }
        (void)LLVMBuildRet(emitter->builder, result);
    }
    return emitter->status == R_FRONTEND_OK;
}

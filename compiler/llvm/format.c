#include "standard_internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* Format recipes (R-EXPR-0028, L32), as the C17 emitter's r_c17_emit_format_function: the body
   of a function whose MIR is a placeholder is written from its recipe. A recipe captures values
   into a structure of its schema (CREATE), renders such a structure (RENDER) or does both at once
   (IMMEDIATE); the remaining kinds are the views and conversions of std.string. A rendering
   appends the parts of the schema to a std.format builder, each only while the previous ones
   succeeded, and finishes the builder into the string of the result. */

typedef struct RLlvmFormat {
    const RSemanticSymbol *function;
    const RFormatRecipe *recipe;
    const RFormatSchema *schema;
    unsigned first_parameter; /* after the result memory */
    LLVMValueRef captures;    /* the capture structure: a local, or the RENDER parameter */
    LLVMValueRef status;      /* RStdFormatAllocResult */
    LLVMValueRef builder;     /* RStdFormatBuilder */
} RLlvmFormat;

static RTypeId
r_llvm_format_parameter_type(RLlvmEmitter *emitter, const RLlvmFormat *format, uint32_t index) {
    return emitter->frontend
        ->semantic_parameter_types[format->function->first_parameter_type + index];
}

/* The address of a parameter's value: the copy a value in memory is passed in, or a slot holding
   a scalar. */
static LLVMValueRef
r_llvm_format_parameter(RLlvmEmitter *emitter, const RLlvmFormat *format, uint32_t index) {
    const RTypeId type = r_llvm_format_parameter_type(emitter, format, index);
    LLVMValueRef value = LLVMGetParam(emitter->function, format->first_parameter + index);
    uint32_t size = 0U;
    uint32_t align = 0U;
    LLVMValueRef slot;

    if (r_llvm_scalar_type(emitter, type) == NULL) {
        return value;
    }
    if (!r_llvm_layout(emitter, type, &size, &align)) {
        return NULL;
    }
    slot = r_llvm_entry_alloca(emitter, size, align, "parameter");
    r_llvm_store_scalar(emitter, type, value, slot);
    return slot;
}

/* The address and type of member `index` of the capture structure. */
static LLVMValueRef r_llvm_format_capture(RLlvmEmitter *emitter,
                                          const RLlvmFormat *format,
                                          uint32_t index,
                                          RTypeId *type) {
    const RSemanticAggregate *aggregate = r_llvm_aggregate(emitter, format->schema->type);
    const RSemanticField *field;
    uint32_t offset = 0U;

    if ((aggregate == NULL) || (index >= aggregate->field_count)) {
        (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        return NULL;
    }
    field = r_llvm_field(emitter, aggregate->first_field + index + 1U);
    if ((field == NULL) ||
        !r_llvm_field_offset(emitter, aggregate->first_field + index + 1U, &offset)) {
        (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        return NULL;
    }
    *type = field->type;
    return r_llvm_byte_offset(emitter, format->captures, offset);
}

/* The address and type of the value a part formats: a capture, or an argument after the
   captures (after the capture structure for RENDER). */
static LLVMValueRef r_llvm_format_part_value(RLlvmEmitter *emitter,
                                             const RLlvmFormat *format,
                                             const RFormatPart *part,
                                             RTypeId *type) {
    uint32_t index;

    if (part->kind == R_FORMAT_CAPTURE) {
        return r_llvm_format_capture(emitter, format, part->index, type);
    }
    index = (format->recipe->kind == R_FORMAT_RENDER ? 1U : format->schema->capture_count) +
            part->index;
    *type = r_llvm_format_parameter_type(emitter, format, index);
    return r_llvm_format_parameter(emitter, format, index);
}

/* RStdFormatSpec of a part in memory. */
static LLVMValueRef r_llvm_format_spec(RLlvmEmitter *emitter, const RFormatSpec *spec) {
    LLVMValueRef memory = r_llvm_std_structure(emitter, "RStdFormatSpec");
    const char *const flags[3] = {"uppercase", "zero_fill", "fixed"};
    const bool values[3] = {spec->uppercase, spec->zero_fill, spec->fixed};
    size_t index;

    if (memory == NULL) {
        return NULL;
    }
    (void)LLVMBuildStore(emitter->builder,
                         r_llvm_u64(emitter, spec->width),
                         r_llvm_std_member(emitter, memory, "RStdFormatSpec", "width"));
    (void)LLVMBuildStore(emitter->builder,
                         r_llvm_u64(emitter, spec->precision),
                         r_llvm_std_member(emitter, memory, "RStdFormatSpec", "precision"));
    (void)LLVMBuildStore(emitter->builder,
                         r_llvm_u32(emitter, spec->radix),
                         r_llvm_std_member(emitter, memory, "RStdFormatSpec", "radix"));
    for (index = 0U; index < 3U; ++index) {
        (void)LLVMBuildStore(emitter->builder,
                             LLVMConstInt(r_llvm_int(emitter, 8U), values[index] ? 1U : 0U, 0),
                             r_llvm_std_member(emitter, memory, "RStdFormatSpec", flags[index]));
    }
    return memory;
}

/* RStdStringView in memory. */
static LLVMValueRef
r_llvm_format_view(RLlvmEmitter *emitter, LLVMValueRef data, LLVMValueRef length) {
    LLVMValueRef view = r_llvm_std_structure(emitter, "RStdStringView");

    if (view == NULL) {
        return NULL;
    }
    (void)LLVMBuildStore(
        emitter->builder, data, r_llvm_std_member(emitter, view, "RStdStringView", "data"));
    (void)LLVMBuildStore(
        emitter->builder, length, r_llvm_std_member(emitter, view, "RStdStringView", "length"));
    return view;
}

/* The view of a `str`, a `constexpr str` (both {data, length}) or an owned string at `value`
   (a borrow of one when `borrowed`). */
static LLVMValueRef r_llvm_format_text_view(RLlvmEmitter *emitter,
                                            RSemanticTypeKind kind,
                                            LLVMValueRef value,
                                            bool borrowed) {
    if ((kind == R_SEMANTIC_TYPE_STR) || (kind == R_SEMANTIC_TYPE_CONSTEXPR_STR)) {
        return r_llvm_format_view(
            emitter,
            LLVMBuildLoad2(emitter->builder, r_llvm_pointer(emitter), value, ""),
            LLVMBuildLoad2(emitter->builder,
                           r_llvm_int(emitter, 64U),
                           r_llvm_byte_offset(emitter, value, 8U),
                           ""));
    }
    {
        LLVMValueRef view = r_llvm_std_structure(emitter, "RStdStringView");
        LLVMValueRef string =
            borrowed ? LLVMBuildLoad2(emitter->builder, r_llvm_pointer(emitter), value, "") : value;
        return (view != NULL) && (r_llvm_call_runtime(
                                      emitter, "r_std_string_as_str", &string, 1U, view) != NULL)
                   ? view
                   : NULL;
    }
}

/* Runs the rest of a part when the status still holds success; returns the block after it. */
static LLVMBasicBlockRef r_llvm_format_while_success(RLlvmEmitter *emitter,
                                                     const RLlvmFormat *format) {
    LLVMBasicBlockRef then = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef next = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");

    (void)LLVMBuildCondBr(emitter->builder,
                          r_llvm_std_status_is(emitter,
                                               format->status,
                                               "RStdFormatAllocResult",
                                               "status",
                                               "R_STD_FORMAT_CALL_SUCCESS"),
                          then,
                          next);
    LLVMPositionBuilderAtEnd(emitter->builder, then);
    return next;
}

static bool r_llvm_format_append(RLlvmEmitter *emitter,
                                 const RLlvmFormat *format,
                                 const char *name,
                                 LLVMValueRef *arguments,
                                 unsigned count) {
    arguments[0] = format->builder;
    return r_llvm_call_runtime(emitter, name, arguments, count, format->status) != NULL;
}

static bool r_llvm_format_value(RLlvmEmitter *emitter,
                                const RLlvmFormat *format,
                                RTypeId type_id,
                                LLVMValueRef value,
                                const RFormatSpec *spec);

static bool
r_llvm_format_part(RLlvmEmitter *emitter, const RLlvmFormat *format, const RFormatPart *part) {
    LLVMBasicBlockRef next = r_llvm_format_while_success(emitter, format);
    LLVMValueRef arguments[2];
    RTypeId type_id = R_TYPE_ID_INVALID;
    LLVMValueRef value;

    if (part->kind == R_FORMAT_TEXT) {
        const RInternEntry *text;
        if ((part->index == 0U) || ((size_t)part->index > emitter->frontend->intern_count)) {
            return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
        text = &emitter->frontend->intern_entries[part->index - 1U];
        arguments[1] = r_llvm_format_view(emitter,
                                          r_llvm_program_string(emitter, part->index),
                                          r_llvm_u64(emitter, text->length));
        if ((arguments[1] == NULL) ||
            !r_llvm_format_append(emitter, format, "r_std_format_append_str", arguments, 2U)) {
            return false;
        }
        (void)LLVMBuildBr(emitter->builder, next);
        LLVMPositionBuilderAtEnd(emitter->builder, next);
        return true;
    }
    value = r_llvm_format_part_value(emitter, format, part, &type_id);
    if ((value == NULL) || !r_llvm_format_value(emitter, format, type_id, value, &part->spec)) {
        return false;
    }
    (void)LLVMBuildBr(emitter->builder, next);
    LLVMPositionBuilderAtEnd(emitter->builder, next);
    return true;
}

/* Appends the text of a scalar, a string or a network address at `value` with its spec. */
static bool r_llvm_format_value(RLlvmEmitter *emitter,
                                const RLlvmFormat *format,
                                RTypeId type_id,
                                LLVMValueRef value,
                                const RFormatSpec *spec) {
    LLVMValueRef arguments[4];
    const RSemanticType *type = r_llvm_type(emitter, r_llvm_value_type(emitter, type_id));
    RSemanticTypeKind kind;
    RTokenKind c_token = R_TOKEN_INVALID;
    bool floating;
    bool integer;

    if (type == NULL) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    kind = type->kind;
    if (kind == R_SEMANTIC_TYPE_C_ABI_NUMERIC) {
        c_token = (RTokenKind)type->length;
        if (c_token == R_TOKEN_KW_C_BOOL) {
            kind = R_SEMANTIC_TYPE_BOOL;
        }
    }
    floating = (kind == R_SEMANTIC_TYPE_F32) || (kind == R_SEMANTIC_TYPE_F64) ||
               (c_token == R_TOKEN_KW_C_FLOAT) || (c_token == R_TOKEN_KW_C_DOUBLE) ||
               (c_token == R_TOKEN_KW_C_LONG_DOUBLE);
    integer = ((kind >= R_SEMANTIC_TYPE_I8) && (kind <= R_SEMANTIC_TYPE_USIZE)) ||
              ((kind == R_SEMANTIC_TYPE_C_ABI_NUMERIC) && !floating);
    if (integer) {
        /* RStdConvertParsedInteger: the sign and the magnitude. */
        const RSemanticTypeKind representation =
            r_semantic_integer_representation(emitter->frontend, type_id);
        const bool is_signed =
            (representation >= R_SEMANTIC_TYPE_I8) && (representation <= R_SEMANTIC_TYPE_ISIZE);
        LLVMValueRef parsed = r_llvm_std_structure(emitter, "RStdConvertParsedInteger");
        LLVMValueRef scalar = r_llvm_load_scalar(emitter, type_id, value);
        LLVMValueRef wide =
            LLVMBuildIntCast2(emitter->builder, scalar, r_llvm_int(emitter, 64U), is_signed, "");
        LLVMValueRef negative =
            is_signed
                ? LLVMBuildICmp(emitter->builder, LLVMIntSLT, wide, r_llvm_u64(emitter, 0U), "")
                : LLVMConstInt(r_llvm_int(emitter, 1U), 0U, 0);
        if (parsed == NULL) {
            return false;
        }
        (void)LLVMBuildStore(
            emitter->builder,
            LLVMBuildZExt(emitter->builder, negative, r_llvm_int(emitter, 8U), ""),
            r_llvm_std_member(emitter, parsed, "RStdConvertParsedInteger", "negative"));
        (void)LLVMBuildStore(
            emitter->builder,
            LLVMBuildSelect(emitter->builder,
                            negative,
                            LLVMBuildSub(emitter->builder, r_llvm_u64(emitter, 0U), wide, ""),
                            wide,
                            ""),
            r_llvm_std_member(emitter, parsed, "RStdConvertParsedInteger", "magnitude"));
        arguments[1] = parsed;
        arguments[2] = r_llvm_format_spec(emitter, spec);
        if ((arguments[2] == NULL) ||
            !r_llvm_format_append(
                emitter, format, "r_library_internal_format_integer", arguments, 3U)) {
            return false;
        }
    } else if (floating) {
        const bool single = (kind == R_SEMANTIC_TYPE_F32) || (c_token == R_TOKEN_KW_C_FLOAT);
        const uint32_t source_kind = kind == R_SEMANTIC_TYPE_F32      ? 0U
                                     : kind == R_SEMANTIC_TYPE_F64    ? 1U
                                     : c_token == R_TOKEN_KW_C_FLOAT  ? 2U
                                     : c_token == R_TOKEN_KW_C_DOUBLE ? 3U
                                                                      : 4U;
        LLVMValueRef scalar = r_llvm_load_scalar(emitter, type_id, value);
        /* long double is the double of the target (AAPCS64 darwin). */
        arguments[1] =
            single ? scalar
                   : LLVMBuildFPCast(
                         emitter->builder, scalar, LLVMDoubleTypeInContext(emitter->context), "");
        arguments[2] = r_llvm_u32(emitter, source_kind);
        arguments[3] = r_llvm_format_spec(emitter, spec);
        if ((arguments[3] == NULL) ||
            !r_llvm_format_append(emitter,
                                  format,
                                  single ? "r_library_internal_format_float32"
                                         : "r_library_internal_format_float",
                                  arguments,
                                  4U)) {
            return false;
        }
    } else if (kind == R_SEMANTIC_TYPE_CHAR) {
        arguments[1] = r_llvm_load_scalar(emitter, type_id, value);
        if (spec->width != 0U) {
            /* R-EXPR-0028 (L32): a width pads text to that many Unicode scalar values. */
            arguments[2] = r_llvm_format_spec(emitter, spec);
            if ((arguments[2] == NULL) ||
                !r_llvm_format_append(
                    emitter, format, "r_library_internal_format_char", arguments, 3U)) {
                return false;
            }
        } else if (!r_llvm_format_append(
                       emitter, format, "r_std_format_append_char", arguments, 2U)) {
            return false;
        }
    } else {
        if (kind == R_SEMANTIC_TYPE_BOOL) {
            static const char true_text[] = "true";
            static const char false_text[] = "false";
            LLVMValueRef truth = r_llvm_load_scalar(emitter, type_id, value);
            LLVMValueRef yes = LLVMBuildGlobalStringPtr(emitter->builder, true_text, "");
            LLVMValueRef no = LLVMBuildGlobalStringPtr(emitter->builder, false_text, "");
            if (LLVMGetTypeKind(LLVMTypeOf(truth)) == LLVMIntegerTypeKind &&
                LLVMGetIntTypeWidth(LLVMTypeOf(truth)) != 1U) {
                truth = LLVMBuildICmp(
                    emitter->builder, LLVMIntNE, truth, LLVMConstInt(LLVMTypeOf(truth), 0U, 0), "");
            }
            arguments[1] = r_llvm_format_view(
                emitter,
                LLVMBuildSelect(emitter->builder, truth, yes, no, ""),
                LLVMBuildSelect(
                    emitter->builder, truth, r_llvm_u64(emitter, 4U), r_llvm_u64(emitter, 5U), ""));
        } else {
            arguments[1] =
                r_llvm_format_text_view(emitter, kind, value, kind == R_SEMANTIC_TYPE_BORROW);
        }
        if (arguments[1] == NULL) {
            return false;
        }
        if (spec->width != 0U) {
            arguments[2] = r_llvm_format_spec(emitter, spec);
            if ((arguments[2] == NULL) ||
                !r_llvm_format_append(
                    emitter, format, "r_library_internal_format_text", arguments, 3U)) {
                return false;
            }
        } else if (!r_llvm_format_append(
                       emitter, format, "r_std_format_append_str", arguments, 2U)) {
            return false;
        }
    }
    return true;
}

/* Copies the captured arguments into the capture structure: a string (a borrow of an owned one,
   or a `str`) as a new owned copy, any other value as it is. */
static bool r_llvm_format_captures(RLlvmEmitter *emitter, const RLlvmFormat *format) {
    uint32_t index;

    for (index = 0U; index < format->schema->capture_count; ++index) {
        const RTypeId parameter_type = r_llvm_format_parameter_type(emitter, format, index);
        const RSemanticType *type =
            r_llvm_type(emitter, r_llvm_value_type(emitter, parameter_type));
        LLVMValueRef parameter = r_llvm_format_parameter(emitter, format, index);
        RTypeId member_type = R_TYPE_ID_INVALID;
        LLVMValueRef member = r_llvm_format_capture(emitter, format, index, &member_type);
        if ((type == NULL) || (parameter == NULL) || (member == NULL)) {
            return false;
        }
        if ((type->kind == R_SEMANTIC_TYPE_BORROW) || (type->kind == R_SEMANTIC_TYPE_STR)) {
            LLVMBasicBlockRef next = r_llvm_format_while_success(emitter, format);
            LLVMValueRef copy = r_llvm_std_structure(emitter, "RStdStringAllocValueResult");
            LLVMValueRef arguments[2];
            arguments[0] = r_llvm_std_allocator(emitter);
            arguments[1] = r_llvm_format_text_view(
                emitter, type->kind, parameter, type->kind == R_SEMANTIC_TYPE_BORROW);
            if ((copy == NULL) || (arguments[0] == NULL) || (arguments[1] == NULL) ||
                (r_llvm_call_runtime(emitter, "r_std_string_from_str", arguments, 2U, copy) ==
                 NULL)) {
                return false;
            }
            {
                /* The status and error of the copy become those of the rendering. */
                LLVMBasicBlockRef copied =
                    LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
                uint32_t error_size = 0U;
                uint32_t error_align = 0U;
                (void)LLVMBuildStore(
                    emitter->builder,
                    LLVMBuildLoad2(
                        emitter->builder,
                        r_llvm_int(emitter, 32U),
                        r_llvm_std_member(emitter, copy, "RStdStringAllocValueResult", "status"),
                        ""),
                    r_llvm_std_member(emitter, format->status, "RStdFormatAllocResult", "status"));
                if (!r_llvm_runtime_layout(emitter, "RStdAllocError", &error_size, &error_align)) {
                    return false;
                }
                (void)LLVMBuildMemCpy(
                    emitter->builder,
                    r_llvm_std_member(emitter, format->status, "RStdFormatAllocResult", "error"),
                    error_align,
                    r_llvm_std_member(emitter, copy, "RStdStringAllocValueResult", "error"),
                    error_align,
                    r_llvm_u64(emitter, error_size));
                (void)LLVMBuildCondBr(emitter->builder,
                                      r_llvm_std_status_is(emitter,
                                                           copy,
                                                           "RStdStringAllocValueResult",
                                                           "status",
                                                           "R_STD_STRING_CALL_SUCCESS"),
                                      copied,
                                      next);
                LLVMPositionBuilderAtEnd(emitter->builder, copied);
                if (!r_llvm_std_copy(
                        emitter,
                        member_type,
                        member,
                        r_llvm_std_member(emitter, copy, "RStdStringAllocValueResult", "value"))) {
                    return false;
                }
            }
            (void)LLVMBuildBr(emitter->builder, next);
            LLVMPositionBuilderAtEnd(emitter->builder, next);
        } else if (!r_llvm_std_copy(emitter, member_type, member, parameter)) {
            return false;
        }
    }
    return true;
}

/* The views and conversions of std.string. */
static bool r_llvm_format_string_recipe(RLlvmEmitter *emitter, const RLlvmFormat *format) {
    LLVMValueRef result = LLVMGetParam(emitter->function, 0U);
    LLVMValueRef source = LLVMGetParam(emitter->function, 1U);

    switch (format->recipe->kind) {
    case R_FORMAT_STRING_FROM_STR: {
        LLVMValueRef copy = r_llvm_std_structure(emitter, "RStdStringAllocValueResult");
        LLVMValueRef arguments[2];
        uint32_t payload = 0U;
        uint32_t size = 0U;
        uint32_t align = 0U;
        LLVMBasicBlockRef success;
        LLVMBasicBlockRef failure;
        arguments[0] = r_llvm_std_allocator(emitter);
        arguments[1] = r_llvm_format_text_view(emitter, R_SEMANTIC_TYPE_STR, source, false);
        if ((copy == NULL) || (arguments[0] == NULL) || (arguments[1] == NULL) ||
            !r_llvm_payload_offset(emitter, format->function->effect_carrier_type, &payload) ||
            !r_llvm_runtime_layout(emitter, "RStdAllocError", &size, &align) ||
            (r_llvm_call_runtime(emitter, "r_std_string_from_str", arguments, 2U, copy) == NULL)) {
            return false;
        }
        success = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        failure = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        (void)LLVMBuildCondBr(
            emitter->builder,
            r_llvm_std_status_is(
                emitter, copy, "RStdStringAllocValueResult", "status", "R_STD_STRING_CALL_SUCCESS"),
            success,
            failure);
        LLVMPositionBuilderAtEnd(emitter->builder, success);
        if (!r_llvm_runtime_layout(emitter, "RRuntimeString", &size, &align)) {
            return false;
        }
        (void)LLVMBuildMemCpy(
            emitter->builder,
            r_llvm_byte_offset(emitter, result, payload),
            align,
            r_llvm_std_member(emitter, copy, "RStdStringAllocValueResult", "value"),
            align,
            r_llvm_u64(emitter, size));
        (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 0U), result);
        (void)LLVMBuildRetVoid(emitter->builder);
        LLVMPositionBuilderAtEnd(emitter->builder, failure);
        if (!r_llvm_runtime_layout(emitter, "RStdAllocError", &size, &align)) {
            return false;
        }
        (void)LLVMBuildMemCpy(
            emitter->builder,
            r_llvm_byte_offset(emitter, result, payload),
            align,
            r_llvm_std_member(emitter, copy, "RStdStringAllocValueResult", "error"),
            align,
            r_llvm_u64(emitter, size));
        (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 1U), result);
        (void)LLVMBuildRetVoid(emitter->builder);
        return true;
    }
    case R_FORMAT_STRING_AS_STR:
    case R_FORMAT_STRING_AS_BYTES: {
        /* `str` and `const u8[]` are both {data, length}. */
        LLVMValueRef view = r_llvm_std_structure(emitter, "RStdStringView");
        if ((view == NULL) ||
            (r_llvm_call_runtime(emitter, "r_std_string_as_str", &source, 1U, view) == NULL)) {
            return false;
        }
        (void)LLVMBuildMemCpy(emitter->builder, result, 8U, view, 8U, r_llvm_u64(emitter, 16U));
        (void)LLVMBuildRetVoid(emitter->builder);
        return true;
    }
    case R_FORMAT_STRING_INTO_BYTES: {
        LLVMValueRef arguments[2];
        uint32_t size = 0U;
        uint32_t align = 0U;
        if (!r_llvm_runtime_layout(emitter, "RRuntimeArray", &size, &align)) {
            return false;
        }
        (void)LLVMBuildMemSet(emitter->builder,
                              result,
                              LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                              r_llvm_u64(emitter, size),
                              align);
        arguments[0] = source;
        arguments[1] = result;
        if (r_llvm_call_runtime(emitter, "r_std_string_into_bytes", arguments, 2U, NULL) == NULL) {
            return false;
        }
        (void)LLVMBuildRetVoid(emitter->builder);
        return true;
    }
    default:
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
}

bool r_llvm_define_format_function(RLlvmEmitter *emitter,
                                   RSymbolId id,
                                   const RSemanticSymbol *symbol) {
    RLlvmFormat format;
    LLVMValueRef result;
    LLVMBasicBlockRef entry;
    LLVMBasicBlockRef body;
    LLVMBasicBlockRef success;
    LLVMBasicBlockRef failure;
    LLVMBasicBlockRef done;
    uint32_t payload = 0U;
    uint32_t index;
    bool captures;

    (void)memset(&format, 0, sizeof(format));
    format.function = symbol;
    if ((format.function->format_recipe == 0U) ||
        ((size_t)format.function->format_recipe > emitter->frontend->format_recipe_count)) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    format.recipe = &emitter->frontend->format_recipes[format.function->format_recipe - 1U];
    format.schema = format.recipe->schema == 0U
                        ? NULL
                        : &emitter->frontend->format_schemas[format.recipe->schema - 1U];
    format.first_parameter = 1U;
    emitter->mir = NULL;
    emitter->symbol = format.function;
    emitter->symbol_id = id;
    emitter->function = emitter->functions[id];
    entry = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "entry");
    body = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMPositionBuilderAtEnd(emitter->builder, entry);
    (void)LLVMBuildBr(emitter->builder, body);
    LLVMPositionBuilderAtEnd(emitter->builder, body);
    if (format.recipe->kind >= R_FORMAT_STRING_FROM_STR) {
        return r_llvm_format_string_recipe(emitter, &format) && (emitter->status == R_FRONTEND_OK);
    }
    if ((format.schema == NULL) ||
        !r_llvm_payload_offset(emitter, format.function->effect_carrier_type, &payload)) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    result = LLVMGetParam(emitter->function, 0U);
    format.status = r_llvm_std_structure(emitter, "RStdFormatAllocResult");
    format.builder = r_llvm_std_structure(emitter, "RStdFormatBuilder");
    if ((format.status == NULL) || (format.builder == NULL)) {
        return false;
    }
    captures =
        (format.recipe->kind == R_FORMAT_CREATE) || (format.recipe->kind == R_FORMAT_IMMEDIATE);
    if (captures) {
        uint32_t size = 0U;
        uint32_t align = 0U;
        if (!r_llvm_layout(emitter, format.schema->type, &size, &align)) {
            return false;
        }
        format.captures = r_llvm_entry_alloca(emitter, size, align, "captures");
        (void)LLVMBuildMemSet(emitter->builder,
                              format.captures,
                              LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                              r_llvm_u64(emitter, size),
                              align);
        if (!r_llvm_format_captures(emitter, &format)) {
            return false;
        }
    } else {
        /* RENDER: the capture structure is borrowed by the first parameter. */
        format.captures = LLVMGetParam(emitter->function, format.first_parameter);
    }
    if (format.recipe->kind != R_FORMAT_CREATE) {
        LLVMValueRef created = r_llvm_std_structure(emitter, "RStdFormatBuilderResult");
        LLVMValueRef allocator = r_llvm_std_allocator(emitter);
        uint32_t size = 0U;
        uint32_t align = 0U;
        if ((created == NULL) || (allocator == NULL) ||
            (r_llvm_call_runtime(emitter, "r_std_format_create", &allocator, 1U, created) ==
             NULL) ||
            !r_llvm_runtime_layout(emitter, "RStdFormatBuilder", &size, &align)) {
            return false;
        }
        (void)LLVMBuildMemCpy(
            emitter->builder,
            format.builder,
            align,
            r_llvm_std_member(emitter, created, "RStdFormatBuilderResult", "value"),
            align,
            r_llvm_u64(emitter, size));
        for (index = 0U; index < format.schema->part_count; ++index) {
            if (!r_llvm_format_part(
                    emitter,
                    &format,
                    &emitter->frontend->format_parts[format.schema->first_part + index])) {
                return false;
            }
        }
    }
    success = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    failure = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    done = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    (void)LLVMBuildCondBr(
        emitter->builder,
        r_llvm_std_status_is(
            emitter, format.status, "RStdFormatAllocResult", "status", "R_STD_FORMAT_CALL_SUCCESS"),
        success,
        failure);
    LLVMPositionBuilderAtEnd(emitter->builder, success);
    if (format.recipe->kind == R_FORMAT_CREATE) {
        /* The captures move into the result and leave nothing to drop. */
        uint32_t size = 0U;
        uint32_t align = 0U;
        if (!r_llvm_layout(emitter, format.schema->type, &size, &align)) {
            return false;
        }
        (void)LLVMBuildMemCpy(emitter->builder,
                              r_llvm_byte_offset(emitter, result, payload),
                              align,
                              format.captures,
                              align,
                              r_llvm_u64(emitter, size));
        (void)LLVMBuildMemSet(emitter->builder,
                              format.captures,
                              LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                              r_llvm_u64(emitter, size),
                              align);
    } else if (r_llvm_call_runtime(emitter,
                                   "r_std_format_finish",
                                   &format.builder,
                                   1U,
                                   r_llvm_byte_offset(emitter, result, payload)) == NULL) {
        return false;
    }
    (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 0U), result);
    (void)LLVMBuildBr(emitter->builder, done);
    LLVMPositionBuilderAtEnd(emitter->builder, failure);
    {
        uint32_t size = 0U;
        uint32_t align = 0U;
        if (!r_llvm_runtime_layout(emitter, "RStdAllocError", &size, &align)) {
            return false;
        }
        (void)LLVMBuildMemCpy(
            emitter->builder,
            r_llvm_byte_offset(emitter, result, payload),
            align,
            r_llvm_std_member(emitter, format.status, "RStdFormatAllocResult", "error"),
            align,
            r_llvm_u64(emitter, size));
    }
    (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 1U), result);
    (void)LLVMBuildBr(emitter->builder, done);
    LLVMPositionBuilderAtEnd(emitter->builder, done);
    if (format.recipe->kind != R_FORMAT_CREATE) {
        LLVMValueRef output =
            r_llvm_std_member(emitter, format.builder, "RStdFormatBuilder", "output");
        if (r_llvm_call_runtime(emitter, "r_std_string_destroy", &output, 1U, NULL) == NULL) {
            return false;
        }
    }
    if (captures && r_llvm_type_requires_drop(emitter, format.schema->type) &&
        !r_llvm_call_drop(emitter, format.schema->type, format.captures)) {
        return false;
    }
    (void)LLVMBuildRetVoid(emitter->builder);
    return emitter->status == R_FRONTEND_OK;
}

/* ---- Format glue (R-TYPE-0046, L32; the C17 emitter's format_glue.inc) ----

   A formatted value whose text the helpers do not append by themselves reaches one internal
   function per formatted type,

       void r_format.<type>(ptr status, ptr value, ptr out)

   which appends the text of the value to the builder `out` and writes the RStdFormatAllocResult
   at `status`: a nominal type calls its core::Format implementation, and standard formatting
   composes the glue of the components (`some(v)` or `none`, `[a, b]`, `(a, b)`) with the texts
   of scalars, strings and network addresses. On failure the builder keeps the text appended
   before it. A use declares the function; its body is written after the functions of the program
   (r_llvm_emit_format_glue), and writing it may declare more. */

static LLVMValueRef r_llvm_format_glue_function(RLlvmEmitter *emitter, RTypeId type) {
    const RTypeId value = r_llvm_value_type(emitter, type);
    LLVMTypeRef parameters[3];
    char name[64];

    if ((value == R_TYPE_ID_INVALID) || ((size_t)value > emitter->frontend->semantic_type_count)) {
        (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        return NULL;
    }
    if (emitter->format_glue[value] == NULL) {
        parameters[0] = r_llvm_pointer(emitter);
        parameters[1] = r_llvm_pointer(emitter);
        parameters[2] = r_llvm_pointer(emitter);
        (void)snprintf(name, sizeof(name), "r_format.%" PRIu32, r_llvm_type_key(emitter, value));
        emitter->format_glue[value] = LLVMAddFunction(
            emitter->module,
            name,
            LLVMFunctionType(LLVMVoidTypeInContext(emitter->context), parameters, 3U, 0));
        LLVMSetLinkage(emitter->format_glue[value], LLVMInternalLinkage);
    }
    return emitter->format_glue[value];
}

static bool r_llvm_format_glue_call(RLlvmEmitter *emitter,
                                    RTypeId type,
                                    LLVMValueRef status,
                                    LLVMValueRef value,
                                    LLVMValueRef out) {
    LLVMValueRef function = r_llvm_format_glue_function(emitter, type);
    LLVMValueRef arguments[3];

    if (function == NULL) {
        return false;
    }
    arguments[0] = status;
    arguments[1] = value;
    arguments[2] = out;
    (void)LLVMBuildCall2(
        emitter->builder, LLVMGlobalGetValueType(function), function, arguments, 3U, "");
    return true;
}

/* Continues in a new block while the status holds success, otherwise returns. */
static void
r_llvm_format_glue_check(RLlvmEmitter *emitter, const RLlvmFormat *format, LLVMBasicBlockRef done) {
    LLVMBasicBlockRef next = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");

    (void)LLVMBuildCondBr(emitter->builder,
                          r_llvm_std_status_is(emitter,
                                               format->status,
                                               "RStdFormatAllocResult",
                                               "status",
                                               "R_STD_FORMAT_CALL_SUCCESS"),
                          next,
                          done);
    LLVMPositionBuilderAtEnd(emitter->builder, next);
}

/* Appends a fixed text. */
static bool
r_llvm_format_glue_text(RLlvmEmitter *emitter, const RLlvmFormat *format, const char *text) {
    LLVMValueRef arguments[2];

    arguments[1] = r_llvm_format_view(emitter,
                                      LLVMBuildGlobalStringPtr(emitter->builder, text, ""),
                                      r_llvm_u64(emitter, (uint64_t)strlen(text)));
    return (arguments[1] != NULL) &&
           r_llvm_format_append(emitter, format, "r_std_format_append_str", arguments, 2U);
}

/* `[a, b]` over `count` elements of `element_type` from `data`. */
static bool r_llvm_format_glue_sequence(RLlvmEmitter *emitter,
                                        const RLlvmFormat *format,
                                        RTypeId element_type,
                                        LLVMValueRef data,
                                        LLVMValueRef count,
                                        LLVMBasicBlockRef done) {
    LLVMValueRef index = r_llvm_entry_alloca(emitter, 8U, 8U, "index");
    LLVMBasicBlockRef header =
        LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef body = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef separator =
        LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef element =
        LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef step = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef exit = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMValueRef current;
    LLVMValueRef success;
    uint32_t size = 0U;
    uint32_t align = 0U;

    if (!r_llvm_layout(emitter, element_type, &size, &align) ||
        !r_llvm_format_glue_text(emitter, format, "[")) {
        return false;
    }
    (void)LLVMBuildStore(emitter->builder, r_llvm_u64(emitter, 0U), index);
    (void)LLVMBuildBr(emitter->builder, header);
    LLVMPositionBuilderAtEnd(emitter->builder, header);
    current = LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 64U), index, "");
    success = r_llvm_std_status_is(
        emitter, format->status, "RStdFormatAllocResult", "status", "R_STD_FORMAT_CALL_SUCCESS");
    (void)LLVMBuildCondBr(
        emitter->builder,
        LLVMBuildAnd(emitter->builder,
                     success,
                     LLVMBuildICmp(emitter->builder, LLVMIntULT, current, count, ""),
                     ""),
        body,
        exit);
    LLVMPositionBuilderAtEnd(emitter->builder, body);
    (void)LLVMBuildCondBr(
        emitter->builder,
        LLVMBuildICmp(emitter->builder, LLVMIntNE, current, r_llvm_u64(emitter, 0U), ""),
        separator,
        element);
    LLVMPositionBuilderAtEnd(emitter->builder, separator);
    if (!r_llvm_format_glue_text(emitter, format, ", ")) {
        return false;
    }
    (void)LLVMBuildCondBr(emitter->builder,
                          r_llvm_std_status_is(emitter,
                                               format->status,
                                               "RStdFormatAllocResult",
                                               "status",
                                               "R_STD_FORMAT_CALL_SUCCESS"),
                          element,
                          step);
    LLVMPositionBuilderAtEnd(emitter->builder, element);
    if (!r_llvm_format_glue_call(
            emitter,
            element_type,
            format->status,
            LLVMBuildGEP2(emitter->builder,
                          r_llvm_int(emitter, 8U),
                          data,
                          (LLVMValueRef[]){LLVMBuildMul(
                              emitter->builder, current, r_llvm_u64(emitter, size), "")},
                          1U,
                          ""),
            format->builder)) {
        return false;
    }
    (void)LLVMBuildBr(emitter->builder, step);
    LLVMPositionBuilderAtEnd(emitter->builder, step);
    (void)LLVMBuildStore(emitter->builder,
                         LLVMBuildAdd(emitter->builder, current, r_llvm_u64(emitter, 1U), ""),
                         index);
    (void)LLVMBuildBr(emitter->builder, header);
    LLVMPositionBuilderAtEnd(emitter->builder, exit);
    r_llvm_format_glue_check(emitter, format, done);
    return r_llvm_format_glue_text(emitter, format, "]");
}

/* A nominal type calls its implementation; its checked alloc_error is the glue failure. */
static bool r_llvm_format_glue_hook(RLlvmEmitter *emitter,
                                    const RLlvmFormat *format,
                                    RSymbolId hook_id,
                                    LLVMValueRef value) {
    const RSemanticSymbol *hook = &emitter->frontend->semantic_symbols[(size_t)hook_id - 1U];
    LLVMValueRef arguments[3];
    unsigned count = 0U;
    LLVMValueRef carrier = NULL;
    uint32_t size = 0U;
    uint32_t align = 0U;
    uint32_t payload = 0U;

    if (hook->parameter_count != 2U) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    if (emitter->functions[hook_id] == NULL) {
        return r_llvm_unsupported(emitter, "a format implementation that is not lowered");
    }
    if (hook->effect_carrier_type != R_TYPE_ID_INVALID) {
        if (!r_llvm_layout(emitter, hook->effect_carrier_type, &size, &align) ||
            !r_llvm_payload_offset(emitter, hook->effect_carrier_type, &payload)) {
            return false;
        }
        carrier = r_llvm_entry_alloca(emitter, size, align, "carrier");
        (void)LLVMBuildMemSet(emitter->builder,
                              carrier,
                              LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                              r_llvm_u64(emitter, size),
                              align);
        arguments[count++] = carrier;
    }
    arguments[count++] = value;
    arguments[count++] = format->builder;
    (void)LLVMBuildCall2(emitter->builder,
                         emitter->function_types[hook_id],
                         emitter->functions[hook_id],
                         arguments,
                         count,
                         "");
    if (carrier != NULL) {
        LLVMBasicBlockRef failed =
            LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        LLVMBasicBlockRef done =
            LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        int64_t error_status = 0;
        if (!r_llvm_runtime_constant(emitter, "R_STD_FORMAT_CALL_ERROR", &error_status) ||
            !r_llvm_runtime_layout(emitter, "RStdAllocError", &size, &align)) {
            return false;
        }
        (void)LLVMBuildCondBr(
            emitter->builder,
            LLVMBuildICmp(emitter->builder,
                          LLVMIntNE,
                          LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), carrier, ""),
                          r_llvm_u32(emitter, 0U),
                          ""),
            failed,
            done);
        LLVMPositionBuilderAtEnd(emitter->builder, failed);
        (void)LLVMBuildStore(
            emitter->builder,
            r_llvm_u32(emitter, (uint64_t)error_status),
            r_llvm_std_member(emitter, format->status, "RStdFormatAllocResult", "status"));
        (void)LLVMBuildMemCpy(
            emitter->builder,
            r_llvm_std_member(emitter, format->status, "RStdFormatAllocResult", "error"),
            align,
            r_llvm_byte_offset(emitter, carrier, payload),
            align,
            r_llvm_u64(emitter, size));
        (void)LLVMBuildBr(emitter->builder, done);
        LLVMPositionBuilderAtEnd(emitter->builder, done);
    }
    return true;
}

static bool r_llvm_format_glue_body(RLlvmEmitter *emitter, RTypeId type_id) {
    const RSemanticType *type = r_llvm_type(emitter, type_id);
    LLVMValueRef value = LLVMGetParam(emitter->function, 1U);
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    RFormatSpec plain;
    RLlvmFormat format;

    if (type == NULL) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    (void)memset(&plain, 0, sizeof(plain));
    plain.radix = 10U;
    (void)memset(&format, 0, sizeof(format));
    format.status = LLVMGetParam(emitter->function, 0U);
    format.builder = LLVMGetParam(emitter->function, 2U);
    {
        uint32_t size = 0U;
        uint32_t align = 0U;
        if (!r_llvm_runtime_layout(emitter, "RStdFormatAllocResult", &size, &align)) {
            return false;
        }
        /* R_STD_FORMAT_CALL_SUCCESS is zero. */
        (void)LLVMBuildMemSet(emitter->builder,
                              format.status,
                              LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                              r_llvm_u64(emitter, size),
                              align);
    }
    switch (type->kind) {
    case R_SEMANTIC_TYPE_OPTION: {
        LLVMBasicBlockRef none =
            LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        LLVMBasicBlockRef some =
            LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        uint32_t payload = 0U;
        if (!r_llvm_payload_offset(emitter, type_id, &payload)) {
            return false;
        }
        (void)LLVMBuildCondBr(
            emitter->builder,
            LLVMBuildICmp(emitter->builder,
                          LLVMIntEQ,
                          LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), value, ""),
                          r_llvm_u32(emitter, 1U),
                          ""),
            some,
            none);
        LLVMPositionBuilderAtEnd(emitter->builder, none);
        if (!r_llvm_format_glue_text(emitter, &format, "none")) {
            return false;
        }
        (void)LLVMBuildBr(emitter->builder, done);
        LLVMPositionBuilderAtEnd(emitter->builder, some);
        if (!r_llvm_format_glue_text(emitter, &format, "some(")) {
            return false;
        }
        r_llvm_format_glue_check(emitter, &format, done);
        if (!r_llvm_format_glue_call(emitter,
                                     type->base,
                                     format.status,
                                     r_llvm_byte_offset(emitter, value, payload),
                                     format.builder)) {
            return false;
        }
        r_llvm_format_glue_check(emitter, &format, done);
        if (!r_llvm_format_glue_text(emitter, &format, ")")) {
            return false;
        }
        break;
    }
    case R_SEMANTIC_TYPE_FIXED_ARRAY:
        if (!r_llvm_format_glue_sequence(
                emitter, &format, type->base, value, r_llvm_u64(emitter, type->length), done)) {
            return false;
        }
        break;
    case R_SEMANTIC_TYPE_SLICE:
        if (!r_llvm_format_glue_sequence(
                emitter,
                &format,
                type->base,
                LLVMBuildLoad2(emitter->builder, r_llvm_pointer(emitter), value, ""),
                LLVMBuildLoad2(emitter->builder,
                               r_llvm_int(emitter, 64U),
                               r_llvm_byte_offset(emitter, value, 8U),
                               ""),
                done)) {
            return false;
        }
        break;
    case R_SEMANTIC_TYPE_ARRAY:
        if (!r_llvm_format_glue_sequence(
                emitter,
                &format,
                type->base,
                LLVMBuildLoad2(emitter->builder,
                               r_llvm_pointer(emitter),
                               r_llvm_std_member(emitter, value, "RRuntimeArray", "data"),
                               ""),
                LLVMBuildLoad2(emitter->builder,
                               r_llvm_int(emitter, 64U),
                               r_llvm_std_member(emitter, value, "RRuntimeArray", "length"),
                               ""),
                done)) {
            return false;
        }
        break;
    case R_SEMANTIC_TYPE_STANDARD:
        if (r_llvm_standard_named(emitter, type_id, "std.string::string")) {
            if (!r_llvm_format_value(emitter, &format, type_id, value, &plain)) {
                return false;
            }
        } else {
            const bool socket = r_llvm_standard_named(emitter, type_id, "std.net::socket_address");
            LLVMValueRef arguments[2];
            LLVMValueRef text = r_llvm_entry_alloca(emitter, 64U, 1U, "text");
            LLVMValueRef length;
            if (!socket && !r_llvm_standard_named(emitter, type_id, "std.net::ip_address")) {
                return r_llvm_unsupported(emitter, "the format glue of this standard type");
            }
            arguments[0] = value;
            arguments[1] = text;
            length = r_llvm_call_runtime(emitter,
                                         socket ? "r_library_internal_net_socket_text"
                                                : "r_library_internal_net_ip_text",
                                         arguments,
                                         2U,
                                         NULL);
            if (length == NULL) {
                return false;
            }
            arguments[1] = r_llvm_format_view(emitter, text, length);
            if ((arguments[1] == NULL) ||
                !r_llvm_format_append(emitter, &format, "r_std_format_append_str", arguments, 2U)) {
                return false;
            }
        }
        break;
    case R_SEMANTIC_TYPE_STRUCT:
    case R_SEMANTIC_TYPE_ENUM: {
        const RSemanticAggregate *aggregate = r_llvm_aggregate(emitter, type_id);
        uint32_t field;
        if (aggregate == NULL) {
            return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
        if (!aggregate->is_tuple) {
            if ((aggregate->format_function == R_SYMBOL_ID_INVALID) ||
                !r_llvm_format_glue_hook(emitter, &format, aggregate->format_function, value)) {
                return aggregate->format_function == R_SYMBOL_ID_INVALID
                           ? r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR)
                           : false;
            }
            break;
        }
        if (!r_llvm_format_glue_text(emitter, &format, "(")) {
            return false;
        }
        for (field = 0U; field < aggregate->field_count; ++field) {
            const uint32_t field_id = aggregate->first_field + field + 1U;
            const RSemanticField *member = r_llvm_field(emitter, field_id);
            uint32_t offset = 0U;
            if (member == NULL) {
                return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
            }
            r_llvm_format_glue_check(emitter, &format, done);
            if ((field != 0U) && !r_llvm_format_glue_text(emitter, &format, ", ")) {
                return false;
            }
            if (field != 0U) {
                r_llvm_format_glue_check(emitter, &format, done);
            }
            if (!r_llvm_field_offset(emitter, field_id, &offset) ||
                !r_llvm_format_glue_call(emitter,
                                         member->type,
                                         format.status,
                                         r_llvm_byte_offset(emitter, value, offset),
                                         format.builder)) {
                return false;
            }
        }
        r_llvm_format_glue_check(emitter, &format, done);
        if (!r_llvm_format_glue_text(emitter, &format, ")")) {
            return false;
        }
        break;
    }
    default:
        if (!r_llvm_format_value(emitter, &format, type_id, value, &plain)) {
            return false;
        }
        break;
    }
    (void)LLVMBuildBr(emitter->builder, done);
    LLVMPositionBuilderAtEnd(emitter->builder, done);
    (void)LLVMBuildRetVoid(emitter->builder);
    return emitter->status == R_FRONTEND_OK;
}

bool r_llvm_emit_format_glue(RLlvmEmitter *emitter) {
    bool progress = true;

    while (progress) {
        size_t index;
        progress = false;
        for (index = 1U; index <= emitter->frontend->semantic_type_count; ++index) {
            if ((emitter->format_glue[index] == NULL) || (emitter->format_written[index] != 0U)) {
                continue;
            }
            emitter->format_written[index] = 1U;
            progress = true;
            emitter->function = emitter->format_glue[index];
            emitter->mir = NULL;
            emitter->frame = NULL;
            LLVMPositionBuilderAtEnd(
                emitter->builder,
                LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "entry"));
            if (!r_llvm_format_glue_body(emitter, (RTypeId)index)) {
                return false;
            }
        }
    }
    return true;
}

/* The case of a dispatcher for an interface member with standard formatting
   (r_c17_emit_dyn_dispatcher): its glue writes the alloc_error into the carrier. */
bool r_llvm_format_glue_member(RLlvmEmitter *emitter,
                               RTypeId member,
                               LLVMValueRef carrier,
                               RTypeId carrier_type,
                               LLVMValueRef value,
                               LLVMValueRef out) {
    LLVMValueRef status = r_llvm_std_structure(emitter, "RStdFormatAllocResult");
    LLVMBasicBlockRef failed =
        LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    uint32_t payload = 0U;
    uint32_t size = 0U;
    uint32_t align = 0U;

    if ((status == NULL) || !r_llvm_payload_offset(emitter, carrier_type, &payload) ||
        !r_llvm_runtime_layout(emitter, "RStdAllocError", &size, &align) ||
        !r_llvm_format_glue_call(emitter, member, status, value, out)) {
        return false;
    }
    (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 0U), carrier);
    (void)LLVMBuildCondBr(
        emitter->builder,
        r_llvm_std_status_is(
            emitter, status, "RStdFormatAllocResult", "status", "R_STD_FORMAT_CALL_SUCCESS"),
        done,
        failed);
    LLVMPositionBuilderAtEnd(emitter->builder, failed);
    (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 1U), carrier);
    (void)LLVMBuildMemCpy(emitter->builder,
                          r_llvm_byte_offset(emitter, carrier, payload),
                          align,
                          r_llvm_std_member(emitter, status, "RStdFormatAllocResult", "error"),
                          align,
                          r_llvm_u64(emitter, size));
    (void)LLVMBuildBr(emitter->builder, done);
    LLVMPositionBuilderAtEnd(emitter->builder, done);
    return true;
}

/* core::format and core::format_into (r_c17_emit_async_core_format): RENDER builds a new string in
   a fresh builder, which a failure destroys; APPEND appends to the caller's builder. A failure is
   the alloc_error of the carrier. */
bool r_llvm_std_core_format(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const bool render = instruction->standard_operation == R_STANDARD_CALL_CORE_FORMAT_RENDER;
    const bool checked =
        r_llvm_value_kind(emitter, instruction->type) == R_SEMANTIC_TYPE_EFFECT_CARRIER;
    LLVMValueRef subject = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 0U));
    LLVMValueRef status = r_llvm_std_structure(emitter, "RStdFormatAllocResult");
    LLVMValueRef builder = NULL;
    LLVMValueRef result = NULL;
    LLVMBasicBlockRef succeeded;
    LLVMBasicBlockRef failed;
    LLVMBasicBlockRef done;
    uint32_t payload = 0U;
    uint32_t size = 0U;
    uint32_t align = 0U;

    if ((subject == NULL) || (status == NULL) ||
        !r_llvm_runtime_layout(emitter, "RStdAllocError", &size, &align)) {
        return false;
    }
    if (render) {
        LLVMValueRef created = r_llvm_std_structure(emitter, "RStdFormatBuilderResult");
        LLVMValueRef allocator = r_llvm_std_allocator(emitter);
        uint32_t builder_size = 0U;
        uint32_t builder_align = 0U;
        builder = r_llvm_std_structure(emitter, "RStdFormatBuilder");
        if ((created == NULL) || (allocator == NULL) || (builder == NULL) ||
            (r_llvm_call_runtime(emitter, "r_std_format_create", &allocator, 1U, created) ==
             NULL) ||
            !r_llvm_runtime_layout(emitter, "RStdFormatBuilder", &builder_size, &builder_align)) {
            return false;
        }
        (void)LLVMBuildMemCpy(
            emitter->builder,
            builder,
            builder_align,
            r_llvm_std_member(emitter, created, "RStdFormatBuilderResult", "value"),
            builder_align,
            r_llvm_u64(emitter, builder_size));
    } else {
        builder = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 1U));
        if (builder == NULL) {
            return false;
        }
    }
    if (!r_llvm_format_glue_call(emitter, instruction->runtime_type, status, subject, builder)) {
        return false;
    }
    if (checked || render) {
        result = r_llvm_std_result(emitter, instruction);
        if ((result == NULL) ||
            (checked && !r_llvm_payload_offset(emitter, instruction->type, &payload))) {
            return false;
        }
    }
    succeeded = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    failed = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    done = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    (void)LLVMBuildCondBr(
        emitter->builder,
        r_llvm_std_status_is(
            emitter, status, "RStdFormatAllocResult", "status", "R_STD_FORMAT_CALL_SUCCESS"),
        succeeded,
        failed);
    LLVMPositionBuilderAtEnd(emitter->builder, succeeded);
    if (render && (r_llvm_call_runtime(emitter,
                                       "r_std_format_finish",
                                       &builder,
                                       1U,
                                       r_llvm_byte_offset(emitter, result, payload)) == NULL)) {
        return false;
    }
    if (checked) {
        (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 0U), result);
    }
    (void)LLVMBuildBr(emitter->builder, done);
    LLVMPositionBuilderAtEnd(emitter->builder, failed);
    if (render && (r_llvm_call_runtime(
                       emitter, "r_std_format_builder_destroy", &builder, 1U, NULL) == NULL)) {
        return false;
    }
    if (checked) {
        (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 1U), result);
        (void)LLVMBuildMemCpy(emitter->builder,
                              r_llvm_byte_offset(emitter, result, payload),
                              align,
                              r_llvm_std_member(emitter, status, "RStdFormatAllocResult", "error"),
                              align,
                              r_llvm_u64(emitter, size));
    } else if (render) {
        uint32_t string_size = 0U;
        uint32_t string_align = 0U;
        if (!r_llvm_layout(emitter, instruction->type, &string_size, &string_align)) {
            return false;
        }
        (void)LLVMBuildMemSet(emitter->builder,
                              result,
                              LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                              r_llvm_u64(emitter, string_size),
                              string_align);
    }
    (void)LLVMBuildBr(emitter->builder, done);
    LLVMPositionBuilderAtEnd(emitter->builder, done);
    if (checked || render) {
        r_llvm_std_initialized(emitter, instruction);
    }
    return true;
}

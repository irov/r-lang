#include "json_internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* Stream decoders and readers of std.json (the C17 emitter's json_stream*.inc and
   json_reader.inc). A decoder of type T is a closed converter: per type, a frame of concrete
   state after the runtime's RStdJsonDecodeFrame, a step the runtime runs while tokens arrive and
   a drop for a frame left incomplete,

       i1   r_json_stream.<type>(ptr cursor, ptr out, i1 quoted)       create: pushes the frame
       i32  r_json_stream_step.<type>(ptr cursor, ptr frame)           RStdJsonDecodeStep
       void r_json_stream_drop.<type>(ptr frame, i1 complete)

   Scalars and strings decode at once with the typed decoder (json_decode.c); containers push
   the converters of their members as children. A reader adapts a transport (std.io::input,
   std.fs::file, std.net::tcp_stream) to the decoder with start, take and deadline callbacks
   and completes a read into the checked carrier of its task. Every function the library calls
   back follows the C ABI of its function type: a structure over 16 bytes travels by address,
   a result over 16 bytes in memory the caller gives (`sret`). */

typedef struct RLlvmJsFrame {
    uint32_t size;
    uint32_t align;
} RLlvmJsFrame;

static uint32_t r_llvm_js_align(uint32_t value, uint32_t align) {
    return align <= 1U ? value : ((value + align - 1U) / align) * align;
}

static uint32_t r_llvm_js_member(RLlvmJsFrame *frame, uint32_t size, uint32_t align) {
    const uint32_t offset = r_llvm_js_align(frame->size, align);
    frame->size = offset + size;
    frame->align = align > frame->align ? align : frame->align;
    return offset;
}

static bool r_llvm_js_base(RLlvmEmitter *emitter, RLlvmJsFrame *frame) {
    return r_llvm_runtime_layout(emitter, "RStdJsonDecodeFrame", &frame->size, &frame->align);
}

static void r_llvm_js_finish(RLlvmJsFrame *frame) {
    frame->size = r_llvm_js_align(frame->size, frame->align);
}

static LLVMValueRef r_llvm_js_state(RLlvmEmitter *emitter, LLVMValueRef frame) {
    return r_llvm_std_member(emitter, frame, "RStdJsonDecodeFrame", "state");
}

static LLVMValueRef r_llvm_js_load_state(RLlvmEmitter *emitter, LLVMValueRef frame) {
    return LLVMBuildLoad2(
        emitter->builder, r_llvm_int(emitter, 32U), r_llvm_js_state(emitter, frame), "");
}

static void r_llvm_js_set_state(RLlvmEmitter *emitter, LLVMValueRef frame, uint32_t state) {
    (void)LLVMBuildStore(
        emitter->builder, r_llvm_u32(emitter, state), r_llvm_js_state(emitter, frame));
}

static LLVMValueRef r_llvm_js_state_is(RLlvmEmitter *emitter, LLVMValueRef frame, uint32_t state) {
    return LLVMBuildICmp(emitter->builder,
                         LLVMIntEQ,
                         r_llvm_js_load_state(emitter, frame),
                         r_llvm_u32(emitter, state),
                         "");
}

static LLVMValueRef r_llvm_js_output(RLlvmEmitter *emitter, LLVMValueRef frame) {
    return LLVMBuildLoad2(emitter->builder,
                          r_llvm_pointer(emitter),
                          r_llvm_std_member(emitter, frame, "RStdJsonDecodeFrame", "output"),
                          "");
}

static LLVMValueRef r_llvm_js_quoted(RLlvmEmitter *emitter, LLVMValueRef frame) {
    return LLVMBuildICmp(
        emitter->builder,
        LLVMIntNE,
        LLVMBuildLoad2(emitter->builder,
                       r_llvm_int(emitter, 8U),
                       r_llvm_std_member(emitter, frame, "RStdJsonDecodeFrame", "quoted"),
                       ""),
        LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
        "");
}

/* A step answer of RStdJsonDecodeStep. */
static LLVMValueRef r_llvm_js_answer(RLlvmEmitter *emitter, const char *name) {
    uint64_t value = 0U;
    return r_llvm_jd_constant(emitter, name, &value) ? r_llvm_u32(emitter, value) : NULL;
}

static bool r_llvm_js_return(RLlvmEmitter *emitter, const char *name) {
    LLVMValueRef answer = r_llvm_js_answer(emitter, name);
    if (answer == NULL) {
        return false;
    }
    (void)LLVMBuildRet(emitter->builder, answer);
    return true;
}

/* Returns `answer` from the step when `condition`. */
static bool r_llvm_js_return_if(RLlvmEmitter *emitter, LLVMValueRef condition, const char *answer) {
    LLVMValueRef value = r_llvm_js_answer(emitter, answer);
    if (value == NULL) {
        return false;
    }
    r_llvm_jd_return_if(emitter, condition, value);
    return true;
}

static LLVMValueRef r_llvm_js_not(RLlvmEmitter *emitter, LLVMValueRef value) {
    return LLVMBuildNot(emitter->builder, value, "");
}

static LLVMValueRef r_llvm_js_has_token(RLlvmEmitter *emitter, LLVMValueRef cursor) {
    return LLVMBuildICmp(
        emitter->builder,
        LLVMIntNE,
        LLVMBuildLoad2(emitter->builder,
                       r_llvm_int(emitter, 8U),
                       r_llvm_std_member(emitter, cursor, "RStdJsonCursor", "has_token"),
                       ""),
        LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
        "");
}

/* token.kind == kind, without testing has_token. */
static LLVMValueRef
r_llvm_js_kind_is(RLlvmEmitter *emitter, LLVMValueRef cursor, const char *kind) {
    uint64_t value = 0U;
    if (!r_llvm_jd_constant(emitter, kind, &value)) {
        return NULL;
    }
    return LLVMBuildICmp(
        emitter->builder,
        LLVMIntEQ,
        LLVMBuildLoad2(
            emitter->builder,
            r_llvm_int(emitter, 32U),
            r_llvm_std_member(emitter,
                              r_llvm_std_member(emitter, cursor, "RStdJsonCursor", "token"),
                              "RStdJsonToken",
                              "kind"),
            ""),
        r_llvm_u32(emitter, value),
        "");
}

static void r_llvm_js_zeroext(RLlvmEmitter *emitter, LLVMValueRef function, unsigned index) {
    static const char name[] = "zeroext";
    LLVMAddAttributeAtIndex(
        function,
        index,
        LLVMCreateEnumAttribute(
            emitter->context, LLVMGetEnumAttributeKindForName(name, sizeof(name) - 1U), 0U));
}

static LLVMValueRef r_llvm_js_function(RLlvmEmitter *emitter,
                                       const char *prefix,
                                       uint32_t key,
                                       LLVMTypeRef result,
                                       LLVMTypeRef *parameters,
                                       unsigned count) {
    char name[96];
    LLVMValueRef function;

    (void)snprintf(
        name, sizeof(name), "%s.%" PRIu32, prefix, r_llvm_type_key(emitter, (RTypeId)key));
    function = LLVMGetNamedFunction(emitter->module, name);
    if (function == NULL) {
        function =
            LLVMAddFunction(emitter->module, name, LLVMFunctionType(result, parameters, count, 0));
        LLVMSetLinkage(function, LLVMInternalLinkage);
    }
    return function;
}

/* The create function of a type: `bool (RStdJsonCursor *, void *, bool)`. */
static LLVMValueRef r_llvm_js_create(RLlvmEmitter *emitter, RTypeId type) {
    const RTypeId value = r_llvm_value_type(emitter, type);
    LLVMTypeRef parameters[3];

    if ((value == R_TYPE_ID_INVALID) || ((size_t)value > emitter->frontend->semantic_type_count)) {
        (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        return NULL;
    }
    if (emitter->json_streams[value] == NULL) {
        parameters[0] = r_llvm_pointer(emitter);
        parameters[1] = r_llvm_pointer(emitter);
        parameters[2] = r_llvm_int(emitter, 1U);
        emitter->json_streams[value] = r_llvm_js_function(
            emitter, "r_json_stream", (uint32_t)value, r_llvm_int(emitter, 1U), parameters, 3U);
        r_llvm_js_zeroext(emitter, emitter->json_streams[value], LLVMAttributeReturnIndex);
        r_llvm_js_zeroext(emitter, emitter->json_streams[value], 3U);
    }
    return emitter->json_streams[value];
}

static LLVMValueRef r_llvm_js_create_call(RLlvmEmitter *emitter,
                                          RTypeId type,
                                          LLVMValueRef cursor,
                                          LLVMValueRef out,
                                          LLVMValueRef quoted) {
    LLVMValueRef function = r_llvm_js_create(emitter, type);
    LLVMValueRef arguments[3];

    if (function == NULL) {
        return NULL;
    }
    arguments[0] = cursor;
    arguments[1] = out;
    arguments[2] = quoted;
    return LLVMBuildCall2(
        emitter->builder, LLVMGlobalGetValueType(function), function, arguments, 3U, "");
}

/* `if (!create(child)) return ERROR; return PROGRESS;` (r_c17_json_stream_child). */
static bool r_llvm_js_child(RLlvmEmitter *emitter,
                            RTypeId child,
                            LLVMValueRef cursor,
                            LLVMValueRef place,
                            LLVMValueRef quoted) {
    LLVMValueRef created = r_llvm_js_create_call(emitter, child, cursor, place, quoted);
    return (created != NULL) &&
           r_llvm_js_return_if(
               emitter, r_llvm_js_not(emitter, created), "R_STD_JSON_DECODE_ERROR") &&
           r_llvm_js_return(emitter, "R_STD_JSON_DECODE_PROGRESS");
}

/* ---- the frames by kind ---- */

typedef struct RLlvmJsCode {
    LLVMValueRef step;
    LLVMValueRef drop; /* NULL when no frame needs one */
    RLlvmJsFrame frame;
} RLlvmJsCode;

static LLVMValueRef r_llvm_js_step(RLlvmEmitter *emitter, RTypeId type) {
    LLVMTypeRef parameters[2];
    parameters[0] = r_llvm_pointer(emitter);
    parameters[1] = r_llvm_pointer(emitter);
    return r_llvm_js_function(
        emitter, "r_json_stream_step", (uint32_t)type, r_llvm_int(emitter, 32U), parameters, 2U);
}

static LLVMValueRef r_llvm_js_drop(RLlvmEmitter *emitter, RTypeId type) {
    LLVMTypeRef parameters[2];
    LLVMValueRef function;
    parameters[0] = r_llvm_pointer(emitter);
    parameters[1] = r_llvm_int(emitter, 1U);
    function = r_llvm_js_function(emitter,
                                  "r_json_stream_drop",
                                  (uint32_t)type,
                                  LLVMVoidTypeInContext(emitter->context),
                                  parameters,
                                  2U);
    r_llvm_js_zeroext(emitter, function, 2U);
    return function;
}

static void r_llvm_js_begin(RLlvmEmitter *emitter, LLVMValueRef function) {
    emitter->function = function;
    emitter->mir = NULL;
    emitter->frame = NULL;
    LLVMPositionBuilderAtEnd(emitter->builder,
                             LLVMAppendBasicBlockInContext(emitter->context, function, "entry"));
}

/* Sequences: array, list and fixed array (r_c17_json_stream_sequence). */
static bool r_llvm_js_sequence(RLlvmEmitter *emitter,
                               RTypeId id,
                               const RSemanticType *type,
                               RLlvmJsCode *code) {
    const bool fixed = type->kind == R_SEMANTIC_TYPE_FIXED_ARRAY;
    const bool list = type->kind == R_SEMANTIC_TYPE_LIST;
    uint32_t element_size = 0U;
    uint32_t element_align = 0U;
    uint32_t extra;
    LLVMValueRef cursor;
    LLVMValueRef base;
    LLVMValueRef out;
    LLVMValueRef extra_address;

    if (!r_llvm_layout(emitter, type->base, &element_size, &element_align) ||
        !r_llvm_js_base(emitter, &code->frame)) {
        return false;
    }
    extra = fixed ? r_llvm_js_member(&code->frame, 8U, 8U)
                  : r_llvm_js_member(&code->frame, element_size, element_align);
    r_llvm_js_finish(&code->frame);
    code->step = r_llvm_js_step(emitter, id);
    code->drop = r_llvm_js_drop(emitter, id);
    r_llvm_js_begin(emitter, code->step);
    cursor = LLVMGetParam(code->step, 0U);
    base = LLVMGetParam(code->step, 1U);
    out = r_llvm_js_output(emitter, base);
    extra_address = r_llvm_byte_offset(emitter, base, extra);
    {
        LLVMBasicBlockRef open = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef next = r_llvm_jd_block(emitter);
        (void)LLVMBuildCondBr(emitter->builder, r_llvm_js_state_is(emitter, base, 0U), open, next);
        LLVMPositionBuilderAtEnd(emitter->builder, open);
        if (!r_llvm_js_return_if(emitter,
                                 r_llvm_js_not(emitter, r_llvm_js_has_token(emitter, cursor)),
                                 "R_STD_JSON_DECODE_WAIT") ||
            !r_llvm_js_return_if(
                emitter,
                r_llvm_js_not(emitter,
                              r_llvm_jd_token(emitter, cursor, "R_STD_JSON_TOKEN_ARRAY_BEGIN")),
                "R_STD_JSON_DECODE_ERROR")) {
            return false;
        }
        if (!fixed) {
            LLVMValueRef arguments[3];
            arguments[0] = out;
            arguments[1] = r_llvm_jd_allocator(emitter, cursor);
            arguments[2] = r_llvm_type_info(emitter, type->base);
            if ((arguments[2] == NULL) || (r_llvm_call_runtime(emitter,
                                                               list ? "r_runtime_list_initialize"
                                                                    : "r_runtime_array_initialize",
                                                               arguments,
                                                               3U,
                                                               NULL) == NULL)) {
                return false;
            }
        }
        r_llvm_js_set_state(emitter, base, 1U);
        (void)r_llvm_jd_next(emitter, cursor);
        (void)LLVMBuildBr(emitter->builder, next);
        LLVMPositionBuilderAtEnd(emitter->builder, next);
    }
    {
        LLVMBasicBlockRef took = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef next = r_llvm_jd_block(emitter);
        (void)LLVMBuildCondBr(emitter->builder, r_llvm_js_state_is(emitter, base, 2U), took, next);
        LLVMPositionBuilderAtEnd(emitter->builder, took);
        if (fixed) {
            (void)LLVMBuildStore(
                emitter->builder,
                LLVMBuildAdd(
                    emitter->builder,
                    LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 64U), extra_address, ""),
                    r_llvm_u64(emitter, 1U),
                    ""),
                extra_address);
        } else {
            LLVMValueRef arguments[3];
            r_llvm_js_set_state(emitter, base, 3U);
            arguments[0] = cursor;
            arguments[1] = out;
            arguments[2] = extra_address;
            if (!r_llvm_js_return_if(emitter,
                                     r_llvm_js_not(emitter,
                                                   r_llvm_jd_call(emitter,
                                                                  list ? "r_json_cursor_list_push"
                                                                       : "r_json_cursor_array_push",
                                                                  arguments,
                                                                  3U)),
                                     "R_STD_JSON_DECODE_ERROR")) {
                return false;
            }
        }
        r_llvm_js_set_state(emitter, base, 1U);
        (void)LLVMBuildBr(emitter->builder, next);
        LLVMPositionBuilderAtEnd(emitter->builder, next);
    }
    if (!r_llvm_js_return_if(emitter,
                             r_llvm_js_not(emitter, r_llvm_js_has_token(emitter, cursor)),
                             "R_STD_JSON_DECODE_WAIT")) {
        return false;
    }
    {
        LLVMBasicBlockRef closed = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef next = r_llvm_jd_block(emitter);
        (void)LLVMBuildCondBr(emitter->builder,
                              r_llvm_js_kind_is(emitter, cursor, "R_STD_JSON_TOKEN_ARRAY_END"),
                              closed,
                              next);
        LLVMPositionBuilderAtEnd(emitter->builder, closed);
        if (fixed) {
            LLVMBasicBlockRef short_array = r_llvm_jd_block(emitter);
            LLVMBasicBlockRef full = r_llvm_jd_block(emitter);
            (void)LLVMBuildCondBr(
                emitter->builder,
                LLVMBuildICmp(
                    emitter->builder,
                    LLVMIntNE,
                    LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 64U), extra_address, ""),
                    r_llvm_u64(emitter, type->length),
                    ""),
                short_array,
                full);
            LLVMPositionBuilderAtEnd(emitter->builder, short_array);
            if ((r_llvm_jd_fail(emitter, cursor, "R_STD_JSON_ERROR_TYPE") == NULL) ||
                !r_llvm_js_return(emitter, "R_STD_JSON_DECODE_ERROR")) {
                return false;
            }
            LLVMPositionBuilderAtEnd(emitter->builder, full);
        }
        (void)r_llvm_jd_next(emitter, cursor);
        if (!r_llvm_js_return(emitter, "R_STD_JSON_DECODE_COMPLETE")) {
            return false;
        }
        LLVMPositionBuilderAtEnd(emitter->builder, next);
    }
    if (fixed) {
        LLVMBasicBlockRef overflow = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef next = r_llvm_jd_block(emitter);
        (void)LLVMBuildCondBr(
            emitter->builder,
            LLVMBuildICmp(
                emitter->builder,
                LLVMIntEQ,
                LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 64U), extra_address, ""),
                r_llvm_u64(emitter, type->length),
                ""),
            overflow,
            next);
        LLVMPositionBuilderAtEnd(emitter->builder, overflow);
        if ((r_llvm_jd_fail(emitter, cursor, "R_STD_JSON_ERROR_TYPE") == NULL) ||
            !r_llvm_js_return(emitter, "R_STD_JSON_DECODE_ERROR")) {
            return false;
        }
        LLVMPositionBuilderAtEnd(emitter->builder, next);
    }
    r_llvm_js_set_state(emitter, base, 2U);
    {
        LLVMValueRef place = extra_address;
        if (fixed) {
            LLVMValueRef offset = LLVMBuildMul(
                emitter->builder,
                LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 64U), extra_address, ""),
                r_llvm_u64(emitter, element_size),
                "");
            place = LLVMBuildGEP2(emitter->builder, r_llvm_int(emitter, 8U), out, &offset, 1U, "");
        }
        if (!r_llvm_js_child(emitter, type->base, cursor, place, r_llvm_jd_false(emitter))) {
            return false;
        }
    }
    /* The drop of an incomplete frame. */
    r_llvm_js_begin(emitter, code->drop);
    base = LLVMGetParam(code->drop, 0U);
    {
        LLVMValueRef complete = LLVMGetParam(code->drop, 1U);
        extra_address = r_llvm_byte_offset(emitter, base, extra);
        if (fixed) {
            if (r_llvm_type_requires_drop(emitter, type->base)) {
                LLVMBasicBlockRef header = r_llvm_jd_block(emitter);
                LLVMBasicBlockRef body = r_llvm_jd_block(emitter);
                LLVMBasicBlockRef done = r_llvm_jd_block(emitter);
                LLVMValueRef count;
                LLVMBasicBlockRef unwind = r_llvm_jd_block(emitter);
                (void)LLVMBuildCondBr(emitter->builder, complete, done, unwind);
                LLVMPositionBuilderAtEnd(emitter->builder, unwind);
                out = r_llvm_js_output(emitter, base);
                (void)LLVMBuildBr(emitter->builder, header);
                LLVMPositionBuilderAtEnd(emitter->builder, header);
                count =
                    LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 64U), extra_address, "");
                (void)LLVMBuildCondBr(
                    emitter->builder,
                    LLVMBuildICmp(emitter->builder, LLVMIntNE, count, r_llvm_u64(emitter, 0U), ""),
                    body,
                    done);
                LLVMPositionBuilderAtEnd(emitter->builder, body);
                {
                    LLVMValueRef last =
                        LLVMBuildSub(emitter->builder, count, r_llvm_u64(emitter, 1U), "");
                    LLVMValueRef offset =
                        LLVMBuildMul(emitter->builder, last, r_llvm_u64(emitter, element_size), "");
                    (void)LLVMBuildStore(emitter->builder, last, extra_address);
                    if (!r_llvm_call_drop(
                            emitter,
                            type->base,
                            LLVMBuildGEP2(
                                emitter->builder, r_llvm_int(emitter, 8U), out, &offset, 1U, ""))) {
                        return false;
                    }
                }
                (void)LLVMBuildBr(emitter->builder, header);
                LLVMPositionBuilderAtEnd(emitter->builder, done);
            }
        } else {
            if (r_llvm_type_requires_drop(emitter, type->base)) {
                LLVMBasicBlockRef drop = r_llvm_jd_block(emitter);
                LLVMBasicBlockRef next = r_llvm_jd_block(emitter);
                (void)LLVMBuildCondBr(
                    emitter->builder, r_llvm_js_state_is(emitter, base, 3U), drop, next);
                LLVMPositionBuilderAtEnd(emitter->builder, drop);
                if (!r_llvm_call_drop(emitter, type->base, extra_address)) {
                    return false;
                }
                (void)LLVMBuildBr(emitter->builder, next);
                LLVMPositionBuilderAtEnd(emitter->builder, next);
            }
            {
                LLVMBasicBlockRef drop = r_llvm_jd_block(emitter);
                LLVMBasicBlockRef next = r_llvm_jd_block(emitter);
                (void)LLVMBuildCondBr(
                    emitter->builder,
                    LLVMBuildAnd(emitter->builder,
                                 r_llvm_js_not(emitter, complete),
                                 r_llvm_js_not(emitter, r_llvm_js_state_is(emitter, base, 0U)),
                                 ""),
                    drop,
                    next);
                LLVMPositionBuilderAtEnd(emitter->builder, drop);
                if (!r_llvm_call_drop(emitter, id, r_llvm_js_output(emitter, base))) {
                    return false;
                }
                (void)LLVMBuildBr(emitter->builder, next);
                LLVMPositionBuilderAtEnd(emitter->builder, next);
            }
        }
    }
    (void)LLVMBuildRetVoid(emitter->builder);
    return true;
}

/* An option: null, or the converter of its payload. */
static bool
r_llvm_js_option(RLlvmEmitter *emitter, RTypeId id, const RSemanticType *type, RLlvmJsCode *code) {
    LLVMValueRef cursor;
    LLVMValueRef base;
    LLVMValueRef out;
    uint32_t payload = 0U;

    if (!r_llvm_payload_offset(emitter, id, &payload) || !r_llvm_js_base(emitter, &code->frame)) {
        return false;
    }
    r_llvm_js_finish(&code->frame);
    code->step = r_llvm_js_step(emitter, id);
    code->drop = NULL;
    r_llvm_js_begin(emitter, code->step);
    cursor = LLVMGetParam(code->step, 0U);
    base = LLVMGetParam(code->step, 1U);
    out = r_llvm_js_output(emitter, base);
    {
        LLVMBasicBlockRef done = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef next = r_llvm_jd_block(emitter);
        (void)LLVMBuildCondBr(emitter->builder, r_llvm_js_state_is(emitter, base, 1U), done, next);
        LLVMPositionBuilderAtEnd(emitter->builder, done);
        (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 1U), out);
        if (!r_llvm_js_return(emitter, "R_STD_JSON_DECODE_COMPLETE")) {
            return false;
        }
        LLVMPositionBuilderAtEnd(emitter->builder, next);
    }
    if (!r_llvm_js_return_if(emitter,
                             r_llvm_js_not(emitter, r_llvm_js_has_token(emitter, cursor)),
                             "R_STD_JSON_DECODE_WAIT")) {
        return false;
    }
    {
        LLVMBasicBlockRef none = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef some = r_llvm_jd_block(emitter);
        (void)LLVMBuildCondBr(emitter->builder,
                              r_llvm_js_kind_is(emitter, cursor, "R_STD_JSON_TOKEN_NULL"),
                              none,
                              some);
        LLVMPositionBuilderAtEnd(emitter->builder, none);
        (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 0U), out);
        (void)r_llvm_jd_next(emitter, cursor);
        if (!r_llvm_js_return(emitter, "R_STD_JSON_DECODE_COMPLETE")) {
            return false;
        }
        LLVMPositionBuilderAtEnd(emitter->builder, some);
    }
    r_llvm_js_set_state(emitter, base, 1U);
    return r_llvm_js_child(emitter,
                           type->base,
                           cursor,
                           r_llvm_byte_offset(emitter, out, payload),
                           r_llvm_js_quoted(emitter, base));
}

/* A value read at once by the typed decoder. */
static bool r_llvm_js_scalar(RLlvmEmitter *emitter, RTypeId id, RLlvmJsCode *code) {
    LLVMValueRef cursor;
    LLVMValueRef base;
    LLVMValueRef read;

    if (!r_llvm_js_base(emitter, &code->frame)) {
        return false;
    }
    r_llvm_js_finish(&code->frame);
    code->step = r_llvm_js_step(emitter, id);
    code->drop = NULL;
    r_llvm_js_begin(emitter, code->step);
    cursor = LLVMGetParam(code->step, 0U);
    base = LLVMGetParam(code->step, 1U);
    if (!r_llvm_js_return_if(emitter,
                             r_llvm_js_not(emitter, r_llvm_js_has_token(emitter, cursor)),
                             "R_STD_JSON_DECODE_WAIT")) {
        return false;
    }
    read = r_llvm_jd_decode(emitter,
                            id,
                            cursor,
                            r_llvm_js_output(emitter, base),
                            r_llvm_js_quoted(emitter, base),
                            r_llvm_jd_false(emitter));
    return (read != NULL) &&
           r_llvm_js_return_if(emitter, r_llvm_js_not(emitter, read), "R_STD_JSON_DECODE_ERROR") &&
           r_llvm_js_return(emitter, "R_STD_JSON_DECODE_COMPLETE");
}

/* dict<string, V> (r_c17_json_stream_dict). */
static bool
r_llvm_js_dict(RLlvmEmitter *emitter, RTypeId id, const RSemanticType *type, RLlvmJsCode *code) {
    uint32_t key_size = 0U;
    uint32_t key_align = 0U;
    uint32_t item_size = 0U;
    uint32_t item_align = 0U;
    uint32_t key;
    uint32_t key_live;
    uint32_t item_live;
    uint32_t item;
    LLVMValueRef cursor;
    LLVMValueRef base;
    LLVMValueRef out;

    if (!r_llvm_runtime_layout(emitter, "RStdString", &key_size, &key_align) ||
        !r_llvm_layout(emitter, type->second, &item_size, &item_align) ||
        !r_llvm_js_base(emitter, &code->frame)) {
        return false;
    }
    key = r_llvm_js_member(&code->frame, key_size, key_align);
    key_live = r_llvm_js_member(&code->frame, 1U, 1U);
    item_live = r_llvm_js_member(&code->frame, 1U, 1U);
    item = r_llvm_js_member(&code->frame, item_size, item_align);
    r_llvm_js_finish(&code->frame);
    code->step = r_llvm_js_step(emitter, id);
    code->drop = r_llvm_js_drop(emitter, id);
    r_llvm_js_begin(emitter, code->step);
    cursor = LLVMGetParam(code->step, 0U);
    base = LLVMGetParam(code->step, 1U);
    out = r_llvm_js_output(emitter, base);
    {
        LLVMBasicBlockRef insert = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef next = r_llvm_jd_block(emitter);
        LLVMValueRef arguments[4];
        (void)LLVMBuildCondBr(
            emitter->builder, r_llvm_js_state_is(emitter, base, 3U), insert, next);
        LLVMPositionBuilderAtEnd(emitter->builder, insert);
        (void)LLVMBuildStore(emitter->builder,
                             LLVMConstInt(r_llvm_int(emitter, 8U), 1U, 0),
                             r_llvm_byte_offset(emitter, base, item_live));
        arguments[0] = cursor;
        arguments[1] = out;
        arguments[2] = r_llvm_byte_offset(emitter, base, key);
        arguments[3] = r_llvm_byte_offset(emitter, base, item);
        if (!r_llvm_js_return_if(
                emitter,
                r_llvm_js_not(emitter,
                              r_llvm_jd_call(emitter, "r_json_cursor_dict_insert", arguments, 4U)),
                "R_STD_JSON_DECODE_ERROR")) {
            return false;
        }
        (void)LLVMBuildStore(emitter->builder,
                             LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                             r_llvm_byte_offset(emitter, base, item_live));
        (void)LLVMBuildStore(emitter->builder,
                             LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                             r_llvm_byte_offset(emitter, base, key_live));
        r_llvm_js_set_state(emitter, base, 1U);
        (void)LLVMBuildBr(emitter->builder, next);
        LLVMPositionBuilderAtEnd(emitter->builder, next);
    }
    if (!r_llvm_js_return_if(emitter,
                             r_llvm_js_not(emitter, r_llvm_js_has_token(emitter, cursor)),
                             "R_STD_JSON_DECODE_WAIT")) {
        return false;
    }
    {
        LLVMBasicBlockRef open = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef next = r_llvm_jd_block(emitter);
        LLVMValueRef arguments[3];
        (void)LLVMBuildCondBr(emitter->builder, r_llvm_js_state_is(emitter, base, 0U), open, next);
        LLVMPositionBuilderAtEnd(emitter->builder, open);
        if (!r_llvm_js_return_if(
                emitter,
                r_llvm_js_not(emitter,
                              r_llvm_jd_token(emitter, cursor, "R_STD_JSON_TOKEN_OBJECT_BEGIN")),
                "R_STD_JSON_DECODE_ERROR")) {
            return false;
        }
        arguments[0] = cursor;
        arguments[1] = out;
        arguments[2] = r_llvm_type_info(emitter, type->second);
        if ((arguments[2] == NULL) ||
            (r_llvm_call_runtime(emitter, "r_json_cursor_dict_initialize", arguments, 3U, NULL) ==
             NULL)) {
            return false;
        }
        r_llvm_js_set_state(emitter, base, 1U);
        (void)r_llvm_jd_next(emitter, cursor);
        if (!r_llvm_js_return(emitter, "R_STD_JSON_DECODE_PROGRESS")) {
            return false;
        }
        LLVMPositionBuilderAtEnd(emitter->builder, next);
    }
    {
        LLVMBasicBlockRef member = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef next = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef closed = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef keyed = r_llvm_jd_block(emitter);
        LLVMValueRef scanner;
        (void)LLVMBuildCondBr(
            emitter->builder, r_llvm_js_state_is(emitter, base, 1U), member, next);
        LLVMPositionBuilderAtEnd(emitter->builder, member);
        (void)LLVMBuildCondBr(emitter->builder,
                              r_llvm_js_kind_is(emitter, cursor, "R_STD_JSON_TOKEN_OBJECT_END"),
                              closed,
                              keyed);
        LLVMPositionBuilderAtEnd(emitter->builder, closed);
        (void)r_llvm_jd_next(emitter, cursor);
        if (!r_llvm_js_return(emitter, "R_STD_JSON_DECODE_COMPLETE")) {
            return false;
        }
        LLVMPositionBuilderAtEnd(emitter->builder, keyed);
        if (!r_llvm_js_return_if(
                emitter,
                r_llvm_js_not(emitter, r_llvm_jd_token(emitter, cursor, "R_STD_JSON_TOKEN_KEY")),
                "R_STD_JSON_DECODE_ERROR")) {
            return false;
        }
        scanner = r_llvm_std_member(emitter, cursor, "RStdJsonCursor", "scanner");
        if (r_llvm_call_runtime(emitter,
                                "r_json_scanner_take_text",
                                &scanner,
                                1U,
                                r_llvm_byte_offset(emitter, base, key)) == NULL) {
            return false;
        }
        (void)LLVMBuildStore(emitter->builder,
                             LLVMConstInt(r_llvm_int(emitter, 8U), 1U, 0),
                             r_llvm_byte_offset(emitter, base, key_live));
        r_llvm_js_set_state(emitter, base, 2U);
        (void)r_llvm_jd_next(emitter, cursor);
        if (!r_llvm_js_return(emitter, "R_STD_JSON_DECODE_PROGRESS")) {
            return false;
        }
        LLVMPositionBuilderAtEnd(emitter->builder, next);
    }
    r_llvm_js_set_state(emitter, base, 3U);
    if (!r_llvm_js_child(emitter,
                         type->second,
                         cursor,
                         r_llvm_byte_offset(emitter, base, item),
                         r_llvm_jd_false(emitter))) {
        return false;
    }
    r_llvm_js_begin(emitter, code->drop);
    base = LLVMGetParam(code->drop, 0U);
    {
        LLVMValueRef complete = LLVMGetParam(code->drop, 1U);
        LLVMBasicBlockRef destroy = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef next = r_llvm_jd_block(emitter);
        LLVMValueRef key_address = r_llvm_byte_offset(emitter, base, key);
        (void)LLVMBuildCondBr(
            emitter->builder,
            LLVMBuildICmp(emitter->builder,
                          LLVMIntNE,
                          LLVMBuildLoad2(emitter->builder,
                                         r_llvm_int(emitter, 8U),
                                         r_llvm_byte_offset(emitter, base, key_live),
                                         ""),
                          LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                          ""),
            destroy,
            next);
        LLVMPositionBuilderAtEnd(emitter->builder, destroy);
        if (r_llvm_call_runtime(emitter, "r_std_string_destroy", &key_address, 1U, NULL) == NULL) {
            return false;
        }
        (void)LLVMBuildBr(emitter->builder, next);
        LLVMPositionBuilderAtEnd(emitter->builder, next);
        if (r_llvm_type_requires_drop(emitter, type->second)) {
            LLVMBasicBlockRef drop = r_llvm_jd_block(emitter);
            LLVMBasicBlockRef after = r_llvm_jd_block(emitter);
            (void)LLVMBuildCondBr(
                emitter->builder,
                LLVMBuildICmp(emitter->builder,
                              LLVMIntNE,
                              LLVMBuildLoad2(emitter->builder,
                                             r_llvm_int(emitter, 8U),
                                             r_llvm_byte_offset(emitter, base, item_live),
                                             ""),
                              LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                              ""),
                drop,
                after);
            LLVMPositionBuilderAtEnd(emitter->builder, drop);
            if (!r_llvm_call_drop(emitter, type->second, r_llvm_byte_offset(emitter, base, item))) {
                return false;
            }
            (void)LLVMBuildBr(emitter->builder, after);
            LLVMPositionBuilderAtEnd(emitter->builder, after);
        }
        {
            LLVMBasicBlockRef drop = r_llvm_jd_block(emitter);
            LLVMBasicBlockRef after = r_llvm_jd_block(emitter);
            LLVMValueRef output;
            (void)LLVMBuildCondBr(
                emitter->builder,
                LLVMBuildAnd(emitter->builder,
                             r_llvm_js_not(emitter, complete),
                             r_llvm_js_not(emitter, r_llvm_js_state_is(emitter, base, 0U)),
                             ""),
                drop,
                after);
            LLVMPositionBuilderAtEnd(emitter->builder, drop);
            output = r_llvm_js_output(emitter, base);
            if (r_llvm_call_runtime(emitter, "r_runtime_dict_destroy", &output, 1U, NULL) == NULL) {
                return false;
            }
            (void)LLVMBuildBr(emitter->builder, after);
            LLVMPositionBuilderAtEnd(emitter->builder, after);
        }
    }
    (void)LLVMBuildRetVoid(emitter->builder);
    return true;
}

/* A type with an unmarshal hook: the tree of the value, then the hook (r_c17_json_stream_hook). */
static bool r_llvm_js_hook(RLlvmEmitter *emitter,
                           RTypeId id,
                           const RSemanticAggregate *aggregate,
                           RLlvmJsCode *code) {
    uint32_t tree_size = 0U;
    uint32_t tree_align = 0U;
    uint32_t tree;
    LLVMValueRef cursor;
    LLVMValueRef base;
    LLVMValueRef tree_address;
    LLVMBasicBlockRef failure;

    if (!r_llvm_runtime_layout(emitter, "RStdJsonValue", &tree_size, &tree_align) ||
        !r_llvm_js_base(emitter, &code->frame)) {
        return false;
    }
    tree = r_llvm_js_member(&code->frame, tree_size, tree_align);
    r_llvm_js_finish(&code->frame);
    code->step = r_llvm_js_step(emitter, id);
    code->drop = NULL;
    r_llvm_js_begin(emitter, code->step);
    cursor = LLVMGetParam(code->step, 0U);
    base = LLVMGetParam(code->step, 1U);
    tree_address = r_llvm_byte_offset(emitter, base, tree);
    {
        LLVMBasicBlockRef start = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef next = r_llvm_jd_block(emitter);
        LLVMValueRef arguments[3];
        (void)LLVMBuildCondBr(emitter->builder, r_llvm_js_state_is(emitter, base, 0U), start, next);
        LLVMPositionBuilderAtEnd(emitter->builder, start);
        r_llvm_js_set_state(emitter, base, 1U);
        arguments[0] = cursor;
        arguments[1] = tree_address;
        arguments[2] = r_llvm_js_quoted(emitter, base);
        if (!r_llvm_js_return_if(
                emitter,
                r_llvm_js_not(emitter,
                              r_llvm_jd_call(emitter, "r_json_decode_value_create", arguments, 3U)),
                "R_STD_JSON_DECODE_ERROR") ||
            !r_llvm_js_return(emitter, "R_STD_JSON_DECODE_PROGRESS")) {
            return false;
        }
        LLVMPositionBuilderAtEnd(emitter->builder, next);
    }
    failure = r_llvm_jd_block(emitter);
    if (!r_llvm_jd_call_function(emitter,
                                 aggregate->json_unmarshal_function,
                                 id,
                                 tree_address,
                                 cursor,
                                 r_llvm_js_output(emitter, base),
                                 failure) ||
        (r_llvm_call_runtime(emitter, "r_std_json_value_destroy", &tree_address, 1U, NULL) ==
         NULL) ||
        !r_llvm_js_return(emitter, "R_STD_JSON_DECODE_COMPLETE")) {
        return false;
    }
    LLVMPositionBuilderAtEnd(emitter->builder, failure);
    return (r_llvm_call_runtime(emitter, "r_std_json_value_destroy", &tree_address, 1U, NULL) !=
            NULL) &&
           r_llvm_js_return(emitter, "R_STD_JSON_DECODE_ERROR");
}

/* An enum: a variant name, or an object {name: payload} (r_c17_json_stream_enum). */
static bool r_llvm_js_enum(RLlvmEmitter *emitter, RTypeId id, RLlvmJsCode *code) {
    const RSemanticAggregate *aggregate = r_llvm_json_aggregate(emitter, id);
    LLVMValueRef cursor;
    LLVMValueRef base;
    LLVMValueRef out;
    LLVMValueRef variant_address;
    LLVMBasicBlockRef type_error;
    LLVMBasicBlockRef failure;
    uint32_t variant;
    uint32_t payload = 0U;
    uint32_t index;
    bool drop;

    if ((aggregate == NULL) || !r_llvm_js_base(emitter, &code->frame) ||
        (aggregate->is_tagged && !r_llvm_payload_offset(emitter, id, &payload))) {
        return aggregate == NULL ? r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR) : false;
    }
    variant = r_llvm_js_member(&code->frame, 4U, 4U);
    r_llvm_js_finish(&code->frame);
    drop = aggregate->is_tagged && r_llvm_type_requires_drop(emitter, id);
    code->step = r_llvm_js_step(emitter, id);
    code->drop = drop ? r_llvm_js_drop(emitter, id) : NULL;
    r_llvm_js_begin(emitter, code->step);
    cursor = LLVMGetParam(code->step, 0U);
    base = LLVMGetParam(code->step, 1U);
    out = r_llvm_js_output(emitter, base);
    variant_address = r_llvm_byte_offset(emitter, base, variant);
    type_error = r_llvm_jd_block(emitter);
    failure = r_llvm_jd_block(emitter);
    if (aggregate->is_tagged) {
        LLVMBasicBlockRef tag = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef next = r_llvm_jd_block(emitter);
        (void)LLVMBuildCondBr(emitter->builder, r_llvm_js_state_is(emitter, base, 3U), tag, next);
        LLVMPositionBuilderAtEnd(emitter->builder, tag);
        (void)LLVMBuildStore(
            emitter->builder,
            LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), variant_address, ""),
            out);
        r_llvm_js_set_state(emitter, base, 4U);
        (void)LLVMBuildBr(emitter->builder, next);
        LLVMPositionBuilderAtEnd(emitter->builder, next);
    }
    if (!r_llvm_js_return_if(emitter,
                             r_llvm_js_not(emitter, r_llvm_js_has_token(emitter, cursor)),
                             "R_STD_JSON_DECODE_WAIT")) {
        return false;
    }
    if (aggregate->is_tagged) {
        LLVMBasicBlockRef close = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef next = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef open = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef after = r_llvm_jd_block(emitter);
        (void)LLVMBuildCondBr(emitter->builder, r_llvm_js_state_is(emitter, base, 4U), close, next);
        LLVMPositionBuilderAtEnd(emitter->builder, close);
        r_llvm_jd_require(
            emitter, r_llvm_jd_token(emitter, cursor, "R_STD_JSON_TOKEN_OBJECT_END"), failure);
        (void)r_llvm_jd_next(emitter, cursor);
        if (!r_llvm_js_return(emitter, "R_STD_JSON_DECODE_COMPLETE")) {
            return false;
        }
        LLVMPositionBuilderAtEnd(emitter->builder, next);
        (void)LLVMBuildCondBr(
            emitter->builder,
            LLVMBuildAnd(emitter->builder,
                         r_llvm_js_state_is(emitter, base, 0U),
                         r_llvm_js_kind_is(emitter, cursor, "R_STD_JSON_TOKEN_OBJECT_BEGIN"),
                         ""),
            open,
            after);
        LLVMPositionBuilderAtEnd(emitter->builder, open);
        r_llvm_js_set_state(emitter, base, 1U);
        (void)r_llvm_jd_next(emitter, cursor);
        if (!r_llvm_js_return(emitter, "R_STD_JSON_DECODE_PROGRESS")) {
            return false;
        }
        LLVMPositionBuilderAtEnd(emitter->builder, after);
    }
    {
        LLVMBasicBlockRef name = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef next = r_llvm_jd_block(emitter);
        (void)LLVMBuildCondBr(emitter->builder, r_llvm_js_state_is(emitter, base, 0U), name, next);
        LLVMPositionBuilderAtEnd(emitter->builder, name);
        r_llvm_jd_require(
            emitter, r_llvm_jd_token(emitter, cursor, "R_STD_JSON_TOKEN_STRING"), failure);
        for (index = 0U; index < aggregate->variant_count; ++index) {
            const RSemanticVariant *current =
                &emitter->frontend->semantic_variants[(size_t)aggregate->first_variant + index];
            const RInternEntry *text =
                &emitter->frontend->intern_entries[(size_t)current->name_intern_id - 1U];
            LLVMValueRef view;
            LLVMValueRef arguments[3];
            LLVMBasicBlockRef match;
            LLVMBasicBlockRef other;
            if (current->payload_type != R_TYPE_ID_INVALID) {
                continue;
            }
            view = r_llvm_std_structure(emitter, "RStdJsonByteView");
            if (view == NULL) {
                return false;
            }
            (void)LLVMBuildStore(emitter->builder,
                                 text->length == 0U
                                     ? r_llvm_empty_string(emitter)
                                     : r_llvm_program_string(emitter, current->name_intern_id),
                                 r_llvm_std_member(emitter, view, "RStdJsonByteView", "data"));
            (void)LLVMBuildStore(emitter->builder,
                                 r_llvm_u64(emitter, text->length),
                                 r_llvm_std_member(emitter, view, "RStdJsonByteView", "length"));
            arguments[0] = r_llvm_jd_text(emitter, cursor);
            arguments[1] = view;
            arguments[2] = r_llvm_jd_false(emitter);
            match = r_llvm_jd_block(emitter);
            other = r_llvm_jd_block(emitter);
            (void)LLVMBuildCondBr(emitter->builder,
                                  r_llvm_jd_call(emitter, "r_std_json_name_equal", arguments, 3U),
                                  match,
                                  other);
            LLVMPositionBuilderAtEnd(emitter->builder, match);
            if (aggregate->is_tagged) {
                (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, index), out);
            } else {
                r_llvm_store_scalar(
                    emitter,
                    id,
                    LLVMConstInt(r_llvm_scalar_type(emitter, id), current->value, 0),
                    out);
            }
            (void)r_llvm_jd_next(emitter, cursor);
            if (!r_llvm_js_return(emitter, "R_STD_JSON_DECODE_COMPLETE")) {
                return false;
            }
            LLVMPositionBuilderAtEnd(emitter->builder, other);
        }
        (void)LLVMBuildBr(emitter->builder, type_error);
        LLVMPositionBuilderAtEnd(emitter->builder, next);
    }
    if (aggregate->is_tagged) {
        LLVMBasicBlockRef keyed = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef next = r_llvm_jd_block(emitter);
        LLVMValueRef choice;
        (void)LLVMBuildCondBr(emitter->builder, r_llvm_js_state_is(emitter, base, 1U), keyed, next);
        LLVMPositionBuilderAtEnd(emitter->builder, keyed);
        r_llvm_jd_require(
            emitter, r_llvm_jd_token(emitter, cursor, "R_STD_JSON_TOKEN_KEY"), failure);
        for (index = 0U; index < aggregate->variant_count; ++index) {
            const RSemanticVariant *current =
                &emitter->frontend->semantic_variants[(size_t)aggregate->first_variant + index];
            const RInternEntry *text =
                &emitter->frontend->intern_entries[(size_t)current->name_intern_id - 1U];
            LLVMValueRef view;
            LLVMValueRef arguments[3];
            LLVMBasicBlockRef match;
            LLVMBasicBlockRef other;
            if (current->payload_type == R_TYPE_ID_INVALID) {
                continue;
            }
            view = r_llvm_std_structure(emitter, "RStdJsonByteView");
            if (view == NULL) {
                return false;
            }
            (void)LLVMBuildStore(emitter->builder,
                                 text->length == 0U
                                     ? r_llvm_empty_string(emitter)
                                     : r_llvm_program_string(emitter, current->name_intern_id),
                                 r_llvm_std_member(emitter, view, "RStdJsonByteView", "data"));
            (void)LLVMBuildStore(emitter->builder,
                                 r_llvm_u64(emitter, text->length),
                                 r_llvm_std_member(emitter, view, "RStdJsonByteView", "length"));
            arguments[0] = r_llvm_jd_text(emitter, cursor);
            arguments[1] = view;
            arguments[2] = r_llvm_jd_false(emitter);
            match = r_llvm_jd_block(emitter);
            other = r_llvm_jd_block(emitter);
            (void)LLVMBuildCondBr(emitter->builder,
                                  r_llvm_jd_call(emitter, "r_std_json_name_equal", arguments, 3U),
                                  match,
                                  other);
            LLVMPositionBuilderAtEnd(emitter->builder, match);
            (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, index), variant_address);
            r_llvm_js_set_state(emitter, base, 2U);
            (void)r_llvm_jd_next(emitter, cursor);
            if (!r_llvm_js_return(emitter, "R_STD_JSON_DECODE_PROGRESS")) {
                return false;
            }
            LLVMPositionBuilderAtEnd(emitter->builder, other);
        }
        (void)LLVMBuildBr(emitter->builder, type_error);
        LLVMPositionBuilderAtEnd(emitter->builder, next);
        r_llvm_js_set_state(emitter, base, 3U);
        choice = LLVMBuildSwitch(
            emitter->builder,
            LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), variant_address, ""),
            type_error,
            aggregate->variant_count);
        for (index = 0U; index < aggregate->variant_count; ++index) {
            const RSemanticVariant *current =
                &emitter->frontend->semantic_variants[(size_t)aggregate->first_variant + index];
            LLVMBasicBlockRef start;
            LLVMValueRef created;
            if (current->payload_type == R_TYPE_ID_INVALID) {
                continue;
            }
            start = r_llvm_jd_block(emitter);
            LLVMAddCase(choice, r_llvm_u32(emitter, index), start);
            LLVMPositionBuilderAtEnd(emitter->builder, start);
            created = r_llvm_js_create_call(emitter,
                                            current->payload_type,
                                            cursor,
                                            r_llvm_byte_offset(emitter, out, payload),
                                            r_llvm_jd_false(emitter));
            if (created == NULL) {
                return false;
            }
            r_llvm_jd_require(emitter, created, failure);
            if (!r_llvm_js_return(emitter, "R_STD_JSON_DECODE_PROGRESS")) {
                return false;
            }
        }
    } else {
        (void)LLVMBuildBr(emitter->builder, type_error);
    }
    LLVMPositionBuilderAtEnd(emitter->builder, type_error);
    if (r_llvm_jd_fail(emitter, cursor, "R_STD_JSON_ERROR_TYPE") == NULL) {
        return false;
    }
    (void)LLVMBuildBr(emitter->builder, failure);
    LLVMPositionBuilderAtEnd(emitter->builder, failure);
    if (!r_llvm_js_return(emitter, "R_STD_JSON_DECODE_ERROR")) {
        return false;
    }
    if (drop) {
        LLVMValueRef complete;
        LLVMBasicBlockRef leave;
        LLVMBasicBlockRef clean;
        LLVMValueRef choice;
        r_llvm_js_begin(emitter, code->drop);
        leave = r_llvm_jd_block(emitter);
        clean = r_llvm_jd_block(emitter);
        base = LLVMGetParam(code->drop, 0U);
        complete = LLVMGetParam(code->drop, 1U);
        (void)LLVMBuildCondBr(
            emitter->builder,
            LLVMBuildOr(emitter->builder,
                        complete,
                        r_llvm_js_not(emitter, r_llvm_js_state_is(emitter, base, 4U)),
                        ""),
            leave,
            clean);
        LLVMPositionBuilderAtEnd(emitter->builder, clean);
        out = r_llvm_js_output(emitter, base);
        choice =
            LLVMBuildSwitch(emitter->builder,
                            LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), out, ""),
                            leave,
                            aggregate->variant_count);
        for (index = 0U; index < aggregate->variant_count; ++index) {
            const RSemanticVariant *current =
                &emitter->frontend->semantic_variants[(size_t)aggregate->first_variant + index];
            LLVMBasicBlockRef dropping;
            if ((current->payload_type == R_TYPE_ID_INVALID) ||
                !r_llvm_type_requires_drop(emitter, current->payload_type)) {
                continue;
            }
            dropping = r_llvm_jd_block(emitter);
            LLVMAddCase(choice, r_llvm_u32(emitter, index), dropping);
            LLVMPositionBuilderAtEnd(emitter->builder, dropping);
            if (!r_llvm_call_drop(
                    emitter, current->payload_type, r_llvm_byte_offset(emitter, out, payload))) {
                return false;
            }
            (void)LLVMBuildBr(emitter->builder, leave);
        }
        LLVMPositionBuilderAtEnd(emitter->builder, leave);
        (void)LLVMBuildRetVoid(emitter->builder);
    }
    return true;
}

/* A struct: its fields by name, the flattened schema of embedded fields and an optional
   collector of unknown keys (r_c17_json_stream_struct). */
static bool r_llvm_js_struct(RLlvmEmitter *emitter, RTypeId id, RLlvmJsCode *code) {
    const RSemanticAggregate *aggregate = r_llvm_json_aggregate(emitter, id);
    const uint32_t count = aggregate == NULL ? 0U : aggregate->json_field_count;
    uint32_t collector = UINT32_MAX;
    RTypeId member_type = R_TYPE_ID_INVALID;
    bool dict = false;
    uint32_t selected;
    uint32_t present;
    uint32_t key = 0U;
    uint32_t key_live = 0U;
    uint32_t item_live = 0U;
    uint32_t item = 0U;
    RLlvmJdSlots slots;
    RLlvmJdField *fields = NULL;
    uint32_t candidates = 0U;
    uint32_t index;
    LLVMValueRef cursor;
    LLVMValueRef base;
    LLVMValueRef out;
    LLVMBasicBlockRef failure;
    LLVMBasicBlockRef finish;
    bool success = false;

    if ((aggregate == NULL) || (aggregate->json_unmarshal_function != R_SYMBOL_ID_INVALID) ||
        !r_llvm_js_base(emitter, &code->frame)) {
        return aggregate == NULL ? r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR) : false;
    }
    slots.entries = &emitter->frontend->json_fields[aggregate->first_json_field];
    for (index = 0U; index < count; ++index) {
        const RSemanticField *field =
            &emitter->frontend->semantic_fields[slots.entries[index].field];
        if (slots.entries[index].collector) {
            const RSemanticType *type =
                r_llvm_type(emitter, r_llvm_value_type(emitter, field->type));
            collector = index;
            dict = (type != NULL) && (type->kind == R_SEMANTIC_TYPE_DICT);
            member_type = dict ? type->second : field->type;
        }
    }
    selected = r_llvm_js_member(&code->frame, 4U, 4U);
    present = r_llvm_js_member(&code->frame, count == 0U ? 1U : count, 1U);
    if (collector != UINT32_MAX) {
        uint32_t size = 0U;
        uint32_t align = 0U;
        if (!r_llvm_runtime_layout(emitter, "RStdString", &size, &align)) {
            return false;
        }
        key = r_llvm_js_member(&code->frame, size, align);
        key_live = r_llvm_js_member(&code->frame, 1U, 1U);
        item_live = r_llvm_js_member(&code->frame, 1U, 1U);
        if (!r_llvm_layout(emitter, member_type, &size, &align)) {
            return false;
        }
        item = r_llvm_js_member(&code->frame, size, align);
    }
    r_llvm_js_finish(&code->frame);
    slots.addresses = r_llvm_allocate(emitter, ((size_t)count + 1U) * sizeof(*slots.addresses));
    fields = r_llvm_allocate(emitter, ((size_t)count + 1U) * sizeof(*fields));
    if ((slots.addresses == NULL) || (fields == NULL)) {
        goto cleanup;
    }
    code->step = r_llvm_js_step(emitter, id);
    code->drop = r_llvm_js_drop(emitter, id);
    r_llvm_js_begin(emitter, code->step);
    cursor = LLVMGetParam(code->step, 0U);
    base = LLVMGetParam(code->step, 1U);
    out = r_llvm_js_output(emitter, base);
    for (index = 0U; index < count; ++index) {
        const RJsonSchemaField *entry = &slots.entries[index];
        uint32_t offset = 0U;
        if (!r_llvm_field_offset(emitter, entry->field + 1U, &offset)) {
            goto cleanup;
        }
        slots.addresses[index] = r_llvm_byte_offset(
            emitter, entry->parent == UINT32_MAX ? out : slots.addresses[entry->parent], offset);
        if (entry->embedded || entry->collector || entry->excluded) {
            continue;
        }
        fields[candidates].field = &emitter->frontend->semantic_fields[entry->field];
        fields[candidates].index = index;
        candidates += 1U;
    }
    failure = r_llvm_jd_block(emitter);
    finish = r_llvm_jd_block(emitter);
    {
        LLVMValueRef present_address = r_llvm_byte_offset(emitter, base, present);
        LLVMValueRef selected_address = r_llvm_byte_offset(emitter, base, selected);
        /* state 3: the member converter completed. */
        {
            LLVMBasicBlockRef completed = r_llvm_jd_block(emitter);
            LLVMBasicBlockRef next = r_llvm_jd_block(emitter);
            LLVMBasicBlockRef mark = r_llvm_jd_block(emitter);
            LLVMBasicBlockRef marked = r_llvm_jd_block(emitter);
            LLVMValueRef which;
            (void)LLVMBuildCondBr(
                emitter->builder, r_llvm_js_state_is(emitter, base, 3U), completed, next);
            LLVMPositionBuilderAtEnd(emitter->builder, completed);
            which =
                LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), selected_address, "");
            (void)LLVMBuildCondBr(
                emitter->builder,
                LLVMBuildICmp(emitter->builder, LLVMIntSGE, which, r_llvm_u32(emitter, 0U), ""),
                mark,
                marked);
            LLVMPositionBuilderAtEnd(emitter->builder, mark);
            {
                LLVMValueRef wide =
                    LLVMBuildZExt(emitter->builder, which, r_llvm_int(emitter, 64U), "");
                (void)LLVMBuildStore(
                    emitter->builder,
                    LLVMConstInt(r_llvm_int(emitter, 8U), 1U, 0),
                    LLVMBuildGEP2(
                        emitter->builder, r_llvm_int(emitter, 8U), present_address, &wide, 1U, ""));
            }
            (void)LLVMBuildBr(emitter->builder, marked);
            LLVMPositionBuilderAtEnd(emitter->builder, marked);
            if (collector != UINT32_MAX) {
                LLVMBasicBlockRef unknown = r_llvm_jd_block(emitter);
                LLVMBasicBlockRef after = r_llvm_jd_block(emitter);
                LLVMValueRef key_address = r_llvm_byte_offset(emitter, base, key);
                LLVMValueRef item_address = r_llvm_byte_offset(emitter, base, item);
                (void)LLVMBuildCondBr(
                    emitter->builder,
                    LLVMBuildICmp(
                        emitter->builder,
                        LLVMIntEQ,
                        LLVMBuildLoad2(
                            emitter->builder, r_llvm_int(emitter, 32U), selected_address, ""),
                        LLVMConstInt(r_llvm_int(emitter, 32U), UINT64_MAX, 1),
                        ""),
                    unknown,
                    after);
                LLVMPositionBuilderAtEnd(emitter->builder, unknown);
                (void)LLVMBuildStore(emitter->builder,
                                     LLVMConstInt(r_llvm_int(emitter, 8U), 1U, 0),
                                     r_llvm_byte_offset(emitter, base, item_live));
                if (dict) {
                    LLVMValueRef arguments[4];
                    arguments[0] = cursor;
                    arguments[1] = slots.addresses[collector];
                    arguments[2] = key_address;
                    arguments[3] = item_address;
                    r_llvm_jd_require(
                        emitter,
                        r_llvm_jd_call(emitter, "r_json_cursor_dict_insert", arguments, 4U),
                        failure);
                } else {
                    LLVMValueRef view = r_llvm_std_structure(emitter, "RStdStringView");
                    LLVMValueRef outcome =
                        r_llvm_std_member(emitter, cursor, "RStdJsonCursor", "outcome");
                    LLVMValueRef arguments[3];
                    if ((view == NULL) ||
                        (r_llvm_call_runtime(
                             emitter, "r_std_string_as_str", &key_address, 1U, view) == NULL)) {
                        goto cleanup;
                    }
                    arguments[0] = slots.addresses[collector];
                    arguments[1] = view;
                    arguments[2] = item_address;
                    if (r_llvm_call_runtime(emitter, "r_std_json_insert", arguments, 3U, outcome) ==
                        NULL) {
                        goto cleanup;
                    }
                    r_llvm_jd_require(emitter,
                                      r_llvm_std_status_is(emitter,
                                                           outcome,
                                                           "RStdJsonResult",
                                                           "status",
                                                           "R_STD_JSON_CALL_SUCCESS"),
                                      failure);
                }
                (void)LLVMBuildStore(emitter->builder,
                                     LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                                     r_llvm_byte_offset(emitter, base, item_live));
                if (r_llvm_call_runtime(emitter, "r_std_string_destroy", &key_address, 1U, NULL) ==
                    NULL) {
                    goto cleanup;
                }
                (void)LLVMBuildStore(emitter->builder,
                                     LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                                     r_llvm_byte_offset(emitter, base, key_live));
                (void)LLVMBuildBr(emitter->builder, after);
                LLVMPositionBuilderAtEnd(emitter->builder, after);
            }
            r_llvm_js_set_state(emitter, base, 1U);
            (void)LLVMBuildBr(emitter->builder, next);
            LLVMPositionBuilderAtEnd(emitter->builder, next);
        }
        if (!r_llvm_js_return_if(emitter,
                                 r_llvm_js_not(emitter, r_llvm_js_has_token(emitter, cursor)),
                                 "R_STD_JSON_DECODE_WAIT")) {
            goto cleanup;
        }
        {
            LLVMBasicBlockRef open = r_llvm_jd_block(emitter);
            LLVMBasicBlockRef next = r_llvm_jd_block(emitter);
            (void)LLVMBuildCondBr(
                emitter->builder, r_llvm_js_state_is(emitter, base, 0U), open, next);
            LLVMPositionBuilderAtEnd(emitter->builder, open);
            r_llvm_jd_require(emitter,
                              r_llvm_jd_token(emitter, cursor, "R_STD_JSON_TOKEN_OBJECT_BEGIN"),
                              failure);
            r_llvm_js_set_state(emitter, base, 1U);
            (void)r_llvm_jd_next(emitter, cursor);
            if (!r_llvm_js_return(emitter, "R_STD_JSON_DECODE_PROGRESS")) {
                goto cleanup;
            }
            LLVMPositionBuilderAtEnd(emitter->builder, next);
        }
        {
            LLVMBasicBlockRef keyed = r_llvm_jd_block(emitter);
            LLVMBasicBlockRef next = r_llvm_jd_block(emitter);
            LLVMBasicBlockRef member = r_llvm_jd_block(emitter);
            (void)LLVMBuildCondBr(
                emitter->builder, r_llvm_js_state_is(emitter, base, 1U), keyed, next);
            LLVMPositionBuilderAtEnd(emitter->builder, keyed);
            (void)LLVMBuildCondBr(emitter->builder,
                                  r_llvm_js_kind_is(emitter, cursor, "R_STD_JSON_TOKEN_OBJECT_END"),
                                  finish,
                                  member);
            LLVMPositionBuilderAtEnd(emitter->builder, member);
            r_llvm_jd_require(
                emitter, r_llvm_jd_token(emitter, cursor, "R_STD_JSON_TOKEN_KEY"), failure);
            (void)LLVMBuildStore(emitter->builder,
                                 LLVMConstInt(r_llvm_int(emitter, 32U), UINT64_MAX, 1),
                                 selected_address);
            if (!r_llvm_jd_select(emitter,
                                  fields,
                                  candidates,
                                  cursor,
                                  r_llvm_jd_text(emitter, cursor),
                                  selected_address,
                                  failure) ||
                !r_llvm_jd_refusals(emitter, cursor, selected_address, present_address, failure)) {
                goto cleanup;
            }
            if (collector != UINT32_MAX) {
                LLVMBasicBlockRef unknown = r_llvm_jd_block(emitter);
                LLVMBasicBlockRef after = r_llvm_jd_block(emitter);
                LLVMValueRef scanner;
                (void)LLVMBuildCondBr(
                    emitter->builder,
                    LLVMBuildICmp(
                        emitter->builder,
                        LLVMIntEQ,
                        LLVMBuildLoad2(
                            emitter->builder, r_llvm_int(emitter, 32U), selected_address, ""),
                        LLVMConstInt(r_llvm_int(emitter, 32U), UINT64_MAX, 1),
                        ""),
                    unknown,
                    after);
                LLVMPositionBuilderAtEnd(emitter->builder, unknown);
                if (!r_llvm_jd_collector(
                        emitter,
                        &emitter->frontend->semantic_fields[slots.entries[collector].field],
                        slots.addresses[collector],
                        r_llvm_jd_present(emitter, present_address, collector),
                        cursor,
                        failure)) {
                    goto cleanup;
                }
                scanner = r_llvm_std_member(emitter, cursor, "RStdJsonCursor", "scanner");
                if (r_llvm_call_runtime(emitter,
                                        "r_json_scanner_take_text",
                                        &scanner,
                                        1U,
                                        r_llvm_byte_offset(emitter, base, key)) == NULL) {
                    goto cleanup;
                }
                (void)LLVMBuildStore(emitter->builder,
                                     LLVMConstInt(r_llvm_int(emitter, 8U), 1U, 0),
                                     r_llvm_byte_offset(emitter, base, key_live));
                (void)LLVMBuildBr(emitter->builder, after);
                LLVMPositionBuilderAtEnd(emitter->builder, after);
            }
            r_llvm_js_set_state(emitter, base, 2U);
            (void)r_llvm_jd_next(emitter, cursor);
            if (!r_llvm_js_return(emitter, "R_STD_JSON_DECODE_PROGRESS")) {
                goto cleanup;
            }
            LLVMPositionBuilderAtEnd(emitter->builder, next);
        }
        r_llvm_js_set_state(emitter, base, 3U);
        {
            LLVMBasicBlockRef other = r_llvm_jd_block(emitter);
            LLVMValueRef choice = LLVMBuildSwitch(
                emitter->builder,
                LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), selected_address, ""),
                other,
                candidates);
            for (index = 0U; index < candidates; ++index) {
                const RSemanticField *field = fields[index].field;
                LLVMBasicBlockRef start = r_llvm_jd_block(emitter);
                LLVMValueRef created;
                LLVMAddCase(choice, r_llvm_u32(emitter, fields[index].index), start);
                LLVMPositionBuilderAtEnd(emitter->builder, start);
                created = r_llvm_js_create_call(
                    emitter,
                    field->type,
                    cursor,
                    slots.addresses[fields[index].index],
                    r_llvm_jd_bool(emitter, (field->json.flags & R_JSON_FIELD_STRING) != 0U));
                if (created == NULL) {
                    goto cleanup;
                }
                r_llvm_jd_require(emitter, created, failure);
                if (!r_llvm_js_return(emitter, "R_STD_JSON_DECODE_PROGRESS")) {
                    goto cleanup;
                }
            }
            LLVMPositionBuilderAtEnd(emitter->builder, other);
            {
                LLVMValueRef created;
                if (collector == UINT32_MAX) {
                    LLVMValueRef arguments[3];
                    arguments[0] = cursor;
                    arguments[1] = out;
                    arguments[2] = r_llvm_jd_false(emitter);
                    created = r_llvm_jd_call(emitter, "r_json_decode_skip_create", arguments, 3U);
                } else {
                    created = r_llvm_js_create_call(emitter,
                                                    member_type,
                                                    cursor,
                                                    r_llvm_byte_offset(emitter, base, item),
                                                    r_llvm_jd_false(emitter));
                }
                if (created == NULL) {
                    goto cleanup;
                }
                r_llvm_jd_require(emitter, created, failure);
                if (!r_llvm_js_return(emitter, "R_STD_JSON_DECODE_PROGRESS")) {
                    goto cleanup;
                }
            }
        }
        LLVMPositionBuilderAtEnd(emitter->builder, finish);
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
            (void)LLVMBuildCondBr(emitter->builder,
                                  r_llvm_jd_is_present(emitter, present_address, index),
                                  next,
                                  missing);
            LLVMPositionBuilderAtEnd(emitter->builder, missing);
            if (r_llvm_jd_fail(emitter, cursor, "R_STD_JSON_ERROR_MISSING_FIELD") == NULL) {
                goto cleanup;
            }
            (void)LLVMBuildBr(emitter->builder, failure);
            LLVMPositionBuilderAtEnd(emitter->builder, next);
        }
        if (aggregate->field_count == 0U) {
            (void)LLVMBuildStore(
                emitter->builder, LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0), out);
        }
        if (!r_llvm_jd_embedded_defaults(emitter,
                                         aggregate,
                                         &slots,
                                         present_address,
                                         cursor,
                                         UINT32_MAX,
                                         0U,
                                         count,
                                         failure)) {
            goto cleanup;
        }
        (void)r_llvm_jd_next(emitter, cursor);
        if (!r_llvm_js_return(emitter, "R_STD_JSON_DECODE_COMPLETE")) {
            goto cleanup;
        }
        LLVMPositionBuilderAtEnd(emitter->builder, failure);
        if (!r_llvm_js_return(emitter, "R_STD_JSON_DECODE_ERROR")) {
            goto cleanup;
        }
    }
    /* The drop of an incomplete frame: the key and item in flight, then the present fields. */
    r_llvm_js_begin(emitter, code->drop);
    base = LLVMGetParam(code->drop, 0U);
    {
        LLVMValueRef complete = LLVMGetParam(code->drop, 1U);
        LLVMValueRef present_address;
        LLVMBasicBlockRef leave = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef clean = r_llvm_jd_block(emitter);
        (void)LLVMBuildCondBr(emitter->builder, complete, leave, clean);
        LLVMPositionBuilderAtEnd(emitter->builder, clean);
        out = r_llvm_js_output(emitter, base);
        present_address = r_llvm_byte_offset(emitter, base, present);
        for (index = 0U; index < count; ++index) {
            const RJsonSchemaField *entry = &slots.entries[index];
            uint32_t offset = 0U;
            if (!r_llvm_field_offset(emitter, entry->field + 1U, &offset)) {
                goto cleanup;
            }
            slots.addresses[index] = r_llvm_byte_offset(
                emitter,
                entry->parent == UINT32_MAX ? out : slots.addresses[entry->parent],
                offset);
        }
        if (collector != UINT32_MAX) {
            LLVMValueRef key_address = r_llvm_byte_offset(emitter, base, key);
            LLVMBasicBlockRef destroy = r_llvm_jd_block(emitter);
            LLVMBasicBlockRef next = r_llvm_jd_block(emitter);
            (void)LLVMBuildCondBr(
                emitter->builder,
                LLVMBuildICmp(emitter->builder,
                              LLVMIntNE,
                              LLVMBuildLoad2(emitter->builder,
                                             r_llvm_int(emitter, 8U),
                                             r_llvm_byte_offset(emitter, base, key_live),
                                             ""),
                              LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                              ""),
                destroy,
                next);
            LLVMPositionBuilderAtEnd(emitter->builder, destroy);
            if (r_llvm_call_runtime(emitter, "r_std_string_destroy", &key_address, 1U, NULL) ==
                NULL) {
                goto cleanup;
            }
            (void)LLVMBuildBr(emitter->builder, next);
            LLVMPositionBuilderAtEnd(emitter->builder, next);
            if (r_llvm_type_requires_drop(emitter, member_type)) {
                LLVMBasicBlockRef drop = r_llvm_jd_block(emitter);
                LLVMBasicBlockRef after = r_llvm_jd_block(emitter);
                LLVMValueRef item_address = r_llvm_byte_offset(emitter, base, item);
                (void)LLVMBuildCondBr(
                    emitter->builder,
                    LLVMBuildICmp(emitter->builder,
                                  LLVMIntNE,
                                  LLVMBuildLoad2(emitter->builder,
                                                 r_llvm_int(emitter, 8U),
                                                 r_llvm_byte_offset(emitter, base, item_live),
                                                 ""),
                                  LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                                  ""),
                    drop,
                    after);
                LLVMPositionBuilderAtEnd(emitter->builder, drop);
                if (!r_llvm_call_drop(emitter, member_type, item_address)) {
                    goto cleanup;
                }
                (void)LLVMBuildBr(emitter->builder, after);
                LLVMPositionBuilderAtEnd(emitter->builder, after);
            }
        }
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
            condition = r_llvm_jd_is_present(emitter, present_address, index - 1U);
            for (parent = entry->parent; parent != UINT32_MAX;
                 parent = slots.entries[parent].parent) {
                condition = LLVMBuildAnd(
                    emitter->builder,
                    condition,
                    r_llvm_js_not(emitter, r_llvm_jd_is_present(emitter, present_address, parent)),
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
        (void)LLVMBuildBr(emitter->builder, leave);
        LLVMPositionBuilderAtEnd(emitter->builder, leave);
        (void)LLVMBuildRetVoid(emitter->builder);
    }
    success = true;

cleanup:
    r_llvm_free(emitter, slots.addresses);
    r_llvm_free(emitter, fields);
    return success;
}

/* The create body: r_json_decode_push of the frame, or the value converter of the runtime. */
static bool r_llvm_js_body(RLlvmEmitter *emitter, RTypeId id) {
    const RSemanticType *type = r_llvm_type(emitter, r_llvm_value_type(emitter, id));
    const RSemanticAggregate *custom = r_llvm_json_aggregate(emitter, id);
    LLVMValueRef create = emitter->json_streams[id];
    LLVMValueRef arguments[7];
    RLlvmJsCode code;
    bool written;

    if (type == NULL) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    (void)memset(&code, 0, sizeof(code));
    if (r_llvm_standard_named(emitter, id, "std.json::value")) {
        r_llvm_js_begin(emitter, create);
        arguments[0] = LLVMGetParam(create, 0U);
        arguments[1] = LLVMGetParam(create, 1U);
        arguments[2] = LLVMGetParam(create, 2U);
        (void)LLVMBuildRet(emitter->builder,
                           r_llvm_jd_call(emitter, "r_json_decode_value_create", arguments, 3U));
        return true;
    }
    if ((custom != NULL) && (custom->json_unmarshal_function != R_SYMBOL_ID_INVALID)) {
        written = r_llvm_js_hook(emitter, id, custom, &code);
    } else if (type->kind == R_SEMANTIC_TYPE_ENUM) {
        written = r_llvm_js_enum(emitter, id, &code);
    } else if (type->kind == R_SEMANTIC_TYPE_DICT) {
        written = r_llvm_js_dict(emitter, id, type, &code);
    } else if (type->kind == R_SEMANTIC_TYPE_STRUCT) {
        written = r_llvm_js_struct(emitter, id, &code);
    } else if ((type->kind == R_SEMANTIC_TYPE_ARRAY) || (type->kind == R_SEMANTIC_TYPE_LIST) ||
               (type->kind == R_SEMANTIC_TYPE_FIXED_ARRAY)) {
        written = r_llvm_js_sequence(emitter, id, type, &code);
    } else if (type->kind == R_SEMANTIC_TYPE_OPTION) {
        written = r_llvm_js_option(emitter, id, type, &code);
    } else {
        written = r_llvm_js_scalar(emitter, id, &code);
    }
    if (!written) {
        return false;
    }
    r_llvm_js_begin(emitter, create);
    arguments[0] = LLVMGetParam(create, 0U);
    arguments[1] = r_llvm_u64(emitter, code.frame.size);
    arguments[2] = r_llvm_u64(emitter, code.frame.align == 0U ? 1U : code.frame.align);
    arguments[3] = code.step;
    arguments[4] = code.drop != NULL ? code.drop : LLVMConstNull(r_llvm_pointer(emitter));
    arguments[5] = LLVMGetParam(create, 1U);
    arguments[6] = LLVMGetParam(create, 2U);
    (void)LLVMBuildRet(emitter->builder,
                       r_llvm_jd_call(emitter, "r_json_decode_push", arguments, 7U));
    return true;
}

bool r_llvm_emit_json_streams(RLlvmEmitter *emitter) {
    bool progress = true;

    while (progress) {
        size_t index;
        progress = false;
        for (index = 1U; index <= emitter->frontend->semantic_type_count; ++index) {
            if ((emitter->json_streams[index] == NULL) ||
                ((emitter->json_written[index] & 16U) != 0U)) {
                continue;
            }
            progress = true;
            emitter->json_written[index] |= 16U;
            if (!r_llvm_js_body(emitter, (RTypeId)index)) {
                return false;
            }
        }
    }
    return true;
}

/* ---- decoders (r_c17_json_stream_function) ---- */

/* The carrier of a decoder operation: success with `value` (none when NULL), or the error of
   `outcome`. */
static bool r_llvm_js_carrier(RLlvmEmitter *emitter,
                              const RSemanticSymbol *symbol,
                              LLVMValueRef carrier,
                              LLVMValueRef outcome,
                              LLVMValueRef value,
                              RTypeId value_type) {
    LLVMBasicBlockRef succeeded = r_llvm_jd_block(emitter);
    LLVMBasicBlockRef allocation = r_llvm_jd_block(emitter);
    LLVMBasicBlockRef failed = r_llvm_jd_block(emitter);
    LLVMBasicBlockRef done = r_llvm_jd_block(emitter);
    LLVMValueRef choice;
    uint64_t success_status = 0U;
    uint64_t allocation_status = 0U;
    uint32_t payload = 0U;
    uint32_t size = 0U;
    uint32_t align = 0U;

    if ((symbol->effect_carrier_type == R_TYPE_ID_INVALID) ||
        !r_llvm_payload_offset(emitter, symbol->effect_carrier_type, &payload) ||
        !r_llvm_jd_constant(emitter, "R_STD_JSON_CALL_SUCCESS", &success_status) ||
        !r_llvm_jd_constant(emitter, "R_STD_JSON_CALL_ALLOCATION_ERROR", &allocation_status)) {
        return symbol->effect_carrier_type == R_TYPE_ID_INVALID
                   ? r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR)
                   : false;
    }
    choice = LLVMBuildSwitch(
        emitter->builder,
        LLVMBuildLoad2(emitter->builder,
                       r_llvm_int(emitter, 32U),
                       r_llvm_std_member(emitter, outcome, "RStdJsonResult", "status"),
                       ""),
        failed,
        2U);
    LLVMAddCase(choice, r_llvm_u32(emitter, success_status), succeeded);
    LLVMAddCase(choice, r_llvm_u32(emitter, allocation_status), allocation);
    LLVMPositionBuilderAtEnd(emitter->builder, succeeded);
    if (value != NULL) {
        if (!r_llvm_layout(emitter, value_type, &size, &align)) {
            return false;
        }
        (void)LLVMBuildMemCpy(emitter->builder,
                              r_llvm_byte_offset(emitter, carrier, payload),
                              align,
                              value,
                              align,
                              r_llvm_u64(emitter, size));
    }
    (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 0U), carrier);
    (void)LLVMBuildBr(emitter->builder, done);
    LLVMPositionBuilderAtEnd(emitter->builder, allocation);
    if (!r_llvm_runtime_layout(emitter, "RStdAllocError", &size, &align)) {
        return false;
    }
    (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 1U), carrier);
    (void)LLVMBuildMemCpy(emitter->builder,
                          r_llvm_byte_offset(emitter, carrier, payload),
                          align,
                          r_llvm_std_member(emitter, outcome, "RStdJsonResult", "allocation_error"),
                          align,
                          r_llvm_u64(emitter, size));
    (void)LLVMBuildBr(emitter->builder, done);
    LLVMPositionBuilderAtEnd(emitter->builder, failed);
    if (!r_llvm_runtime_layout(emitter, "RStdJsonError", &size, &align)) {
        return false;
    }
    (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 2U), carrier);
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

static bool r_llvm_js_options_argument(RLlvmEmitter *emitter,
                                       const RSemanticSymbol *symbol,
                                       uint32_t parameter,
                                       LLVMValueRef *options) {
    uint32_t size = 0U;
    uint32_t align = 0U;

    *options = r_llvm_std_structure(emitter, "RStdJsonOptions");
    if (*options == NULL) {
        return false;
    }
    if (symbol->parameter_count > parameter) {
        if (!r_llvm_runtime_layout(emitter, "RStdJsonOptions", &size, &align)) {
            return false;
        }
        (void)LLVMBuildMemCpy(emitter->builder,
                              *options,
                              align,
                              LLVMGetParam(emitter->function, 1U + parameter),
                              align,
                              r_llvm_u64(emitter, size));
        return true;
    }
    return r_llvm_json_default_options(emitter, *options);
}

static bool r_llvm_js_decoder_function(RLlvmEmitter *emitter, const RSemanticSymbol *symbol) {
    const RJsonOperation operation = (RJsonOperation)symbol->json_operation;
    LLVMValueRef carrier = LLVMGetParam(emitter->function, 0U);
    LLVMValueRef outcome = r_llvm_std_structure(emitter, "RStdJsonResult");
    LLVMValueRef value;
    RTypeId value_type;
    uint32_t size = 0U;
    uint32_t align = 0U;

    if (outcome == NULL) {
        return false;
    }
    if (operation == R_JSON_OPERATION_NEW_DECODER) {
        LLVMValueRef arguments[5];
        LLVMValueRef options;
        value_type = symbol->return_type;
        if (!r_llvm_layout(emitter, value_type, &size, &align) ||
            !r_llvm_js_options_argument(emitter, symbol, 0U, &options)) {
            return false;
        }
        value = r_llvm_entry_alloca(emitter, size, align, "decoder");
        arguments[0] = value;
        arguments[1] = r_llvm_std_allocator(emitter);
        arguments[2] = options;
        arguments[3] = r_llvm_type_info(emitter, symbol->json_type);
        arguments[4] = r_llvm_js_create(emitter, symbol->json_type);
        if ((arguments[1] == NULL) || (arguments[3] == NULL) || (arguments[4] == NULL) ||
            (r_llvm_call_runtime(emitter, "r_json_decoder_initialize", arguments, 5U, outcome) ==
             NULL)) {
            return false;
        }
    } else if (operation == R_JSON_OPERATION_FEED) {
        /* std.json::feed_result {consumed, state} is RStdJsonProgress, the state one below the
           runtime's feed state. */
        LLVMValueRef fed = r_llvm_std_structure(emitter, "RStdJsonFeedResult");
        LLVMValueRef arguments[3];
        uint32_t outcome_size = 0U;
        uint32_t outcome_align = 0U;
        value_type = symbol->return_type;
        if ((fed == NULL) || !r_llvm_layout(emitter, value_type, &size, &align) ||
            !r_llvm_runtime_layout(emitter, "RStdJsonResult", &outcome_size, &outcome_align)) {
            return false;
        }
        arguments[0] = LLVMGetParam(emitter->function, 1U);
        arguments[1] = LLVMGetParam(emitter->function, 2U);
        arguments[2] = LLVMGetParam(emitter->function, 3U);
        if (r_llvm_call_runtime(emitter, "r_json_decoder_feed", arguments, 3U, fed) == NULL) {
            return false;
        }
        (void)LLVMBuildMemCpy(emitter->builder,
                              outcome,
                              outcome_align,
                              r_llvm_std_member(emitter, fed, "RStdJsonFeedResult", "outcome"),
                              outcome_align,
                              r_llvm_u64(emitter, outcome_size));
        value = r_llvm_std_structure(emitter, "RStdJsonProgress");
        if (value == NULL) {
            return false;
        }
        (void)LLVMBuildStore(
            emitter->builder,
            LLVMBuildLoad2(emitter->builder,
                           r_llvm_int(emitter, 64U),
                           r_llvm_std_member(emitter, fed, "RStdJsonFeedResult", "consumed"),
                           ""),
            r_llvm_std_member(emitter, value, "RStdJsonProgress", "consumed"));
        (void)LLVMBuildStore(
            emitter->builder,
            LLVMBuildSub(
                emitter->builder,
                LLVMBuildLoad2(emitter->builder,
                               r_llvm_int(emitter, 32U),
                               r_llvm_std_member(emitter, fed, "RStdJsonFeedResult", "state"),
                               ""),
                r_llvm_u32(emitter, 1U),
                ""),
            r_llvm_std_member(emitter, value, "RStdJsonProgress", "state"));
    } else {
        LLVMValueRef arguments[2];
        value_type = symbol->return_type;
        if (!r_llvm_layout(emitter, value_type, &size, &align)) {
            return false;
        }
        value = r_llvm_entry_alloca(emitter, size, align, "value");
        arguments[0] = LLVMGetParam(emitter->function, 1U);
        arguments[1] = value;
        if (r_llvm_call_runtime(emitter, "r_json_decoder_take", arguments, 2U, outcome) == NULL) {
            return false;
        }
    }
    if (!r_llvm_js_carrier(emitter, symbol, carrier, outcome, value, value_type)) {
        return false;
    }
    (void)LLVMBuildRetVoid(emitter->builder);
    return true;
}

/* ---- readers (r_c17_json_reader_function) ---- */

static RTypeId r_llvm_js_parameter_type(const RLlvmEmitter *emitter,
                                        const RSemanticSymbol *symbol,
                                        uint32_t index) {
    return emitter->frontend->semantic_parameter_types[symbol->first_parameter_type + index];
}

/* The address of a parameter: a borrow is one, so is a memory parameter, a scalar is spilled. */
static LLVMValueRef
r_llvm_js_parameter_address(RLlvmEmitter *emitter, const RSemanticSymbol *symbol, uint32_t index) {
    const RTypeId type = r_llvm_js_parameter_type(emitter, symbol, index);
    LLVMValueRef parameter = LLVMGetParam(emitter->function, 1U + index);
    LLVMValueRef slot;
    uint32_t size = 0U;
    uint32_t align = 0U;

    if ((r_llvm_value_kind(emitter, type) == R_SEMANTIC_TYPE_BORROW) ||
        (r_llvm_scalar_type(emitter, type) == NULL)) {
        return parameter;
    }
    if (!r_llvm_layout(emitter, type, &size, &align)) {
        return NULL;
    }
    slot = r_llvm_entry_alloca(emitter, size, align, "argument");
    r_llvm_store_scalar(emitter, type, parameter, slot);
    return slot;
}

static LLVMAttributeRef r_llvm_js_sret(RLlvmEmitter *emitter, uint32_t size) {
    static const char name[] = "sret";
    return LLVMCreateTypeAttribute(emitter->context,
                                   LLVMGetEnumAttributeKindForName(name, sizeof(name) - 1U),
                                   LLVMArrayType2(r_llvm_int(emitter, 8U), size));
}

/* A structure over 16 bytes passes by address and returns through `sret` (AAPCS64); the
   callbacks below rely on it. */
static bool r_llvm_js_large(RLlvmEmitter *emitter, const char *type_name, uint32_t *size) {
    uint32_t align = 0U;
    if (!r_llvm_runtime_layout(emitter, type_name, size, &align)) {
        return false;
    }
    return (*size > 16U)
               ? true
               : r_llvm_unsupported(emitter, "a JSON reader callback with a small structure");
}

/* The transport callbacks of a reader of `transport` (r_c17_json_reader_transport). */
static bool r_llvm_js_transport(RLlvmEmitter *emitter,
                                RTypeId transport,
                                LLVMValueRef *start,
                                LLVMValueRef *take,
                                LLVMValueRef *deadline,
                                const char **read_type) {
    const bool fs = r_llvm_standard_named(emitter, transport, "std.fs::file");
    const bool net = r_llvm_standard_named(emitter, transport, "std.net::tcp_stream");
    const char *start_type = fs    ? "RStdFsTaskStartResult"
                             : net ? "RStdNetTaskStartResult"
                                   : "RStdIoTaskStartResult";
    const char *deadline_type = fs ? "RStdFsDeadline" : net ? "RStdNetDeadline" : "RStdIoDeadline";
    const char *start_name = fs ? "r_std_fs_read" : net ? "r_std_net_tcp_read" : "r_std_io_read";
    const uint32_t key = (uint32_t)r_llvm_value_type(emitter, transport);
    LLVMTypeRef parameters[3];
    uint32_t start_size = 0U;
    uint32_t deadline_size = 0U;
    uint32_t outcome_size = 0U;
    LLVMValueRef saved = emitter->function;

    *read_type = net ? "RStdNetTcpReadResult" : "RStdIoReadResult";
    if (!r_llvm_js_large(emitter, "RStdJsonTaskStartResult", &start_size) ||
        !r_llvm_js_large(emitter, "RStdJsonDeadline", &deadline_size) ||
        !r_llvm_js_large(emitter, "RStdJsonReadOutcome", &outcome_size)) {
        return false;
    }
    /* RStdJsonTaskStartResult start(const void *handle, RRuntimeArray *buffer, RStdJsonDeadline) */
    parameters[0] = r_llvm_pointer(emitter);
    parameters[1] = r_llvm_pointer(emitter);
    parameters[2] = r_llvm_pointer(emitter);
    {
        LLVMTypeRef with_result[4];
        with_result[0] = r_llvm_pointer(emitter);
        with_result[1] = parameters[0];
        with_result[2] = parameters[1];
        with_result[3] = parameters[2];
        *start = r_llvm_js_function(emitter,
                                    "r_json_reader_start",
                                    key,
                                    LLVMVoidTypeInContext(emitter->context),
                                    with_result,
                                    4U);
    }
    *take = r_llvm_js_function(emitter,
                               "r_json_reader_take",
                               key,
                               LLVMVoidTypeInContext(emitter->context),
                               parameters,
                               2U);
    *deadline = r_llvm_js_function(
        emitter, "r_json_reader_deadline", key, r_llvm_int(emitter, 1U), parameters, 2U);
    if (LLVMCountBasicBlocks(*start) != 0U) {
        return true;
    }
    LLVMAddAttributeAtIndex(*start, 1U, r_llvm_js_sret(emitter, start_size));
    r_llvm_js_zeroext(emitter, *deadline, LLVMAttributeReturnIndex);
    /* start */
    r_llvm_js_begin(emitter, *start);
    {
        LLVMValueRef result = LLVMGetParam(*start, 0U);
        LLVMValueRef deadline_in = LLVMGetParam(*start, 3U);
        LLVMValueRef native = r_llvm_std_structure(emitter, deadline_type);
        LLVMValueRef started = r_llvm_std_structure(emitter, start_type);
        LLVMValueRef arguments[3];
        uint32_t instant_size = 0U;
        uint32_t instant_align = 0U;
        if ((native == NULL) || (started == NULL) ||
            !r_llvm_runtime_layout(emitter, "RStdTimeInstant", &instant_size, &instant_align)) {
            return false;
        }
        (void)LLVMBuildStore(
            emitter->builder,
            LLVMBuildLoad2(emitter->builder,
                           r_llvm_int(emitter, 8U),
                           r_llvm_std_member(emitter, deadline_in, "RStdJsonDeadline", "has_value"),
                           ""),
            r_llvm_std_member(emitter, native, deadline_type, "has_value"));
        (void)LLVMBuildMemCpy(emitter->builder,
                              r_llvm_std_member(emitter, native, deadline_type, "value"),
                              instant_align,
                              r_llvm_std_member(emitter, deadline_in, "RStdJsonDeadline", "value"),
                              instant_align,
                              r_llvm_u64(emitter, instant_size));
        arguments[0] = LLVMGetParam(*start, 1U);
        arguments[1] = LLVMGetParam(*start, 2U);
        arguments[2] = native;
        if (r_llvm_call_runtime(emitter, start_name, arguments, 3U, started) == NULL) {
            return false;
        }
        (void)LLVMBuildMemSet(emitter->builder,
                              result,
                              LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                              r_llvm_u64(emitter, start_size),
                              8U);
        (void)LLVMBuildStore(
            emitter->builder,
            LLVMBuildLoad2(emitter->builder,
                           r_llvm_int(emitter, 8U),
                           r_llvm_std_member(emitter, started, start_type, "is_ok"),
                           ""),
            r_llvm_std_member(emitter, result, "RStdJsonTaskStartResult", "is_ok"));
        (void)LLVMBuildStore(emitter->builder,
                             LLVMBuildLoad2(emitter->builder,
                                            r_llvm_pointer(emitter),
                                            r_llvm_std_member(emitter, started, start_type, "task"),
                                            ""),
                             r_llvm_std_member(emitter, result, "RStdJsonTaskStartResult", "task"));
        {
            uint32_t error_size = 0U;
            uint32_t error_align = 0U;
            if (!r_llvm_runtime_layout(emitter, "RStdAsyncStartError", &error_size, &error_align)) {
                return false;
            }
            (void)LLVMBuildMemCpy(
                emitter->builder,
                r_llvm_std_member(emitter, result, "RStdJsonTaskStartResult", "error"),
                error_align,
                r_llvm_std_member(emitter, started, start_type, "error"),
                error_align,
                r_llvm_u64(emitter, error_size));
        }
        (void)LLVMBuildRetVoid(emitter->builder);
    }
    /* take */
    r_llvm_js_begin(emitter, *take);
    {
        LLVMValueRef read = LLVMGetParam(*take, 0U);
        LLVMValueRef out = LLVMGetParam(*take, 1U);
        LLVMValueRef kind = LLVMBuildLoad2(emitter->builder,
                                           r_llvm_int(emitter, 32U),
                                           r_llvm_std_member(emitter, read, *read_type, "kind"),
                                           "");
        LLVMValueRef error = r_llvm_std_member(emitter, read, *read_type, "error");
        const char *error_type = net ? "RStdNetError" : "RStdIoError";
        uint64_t end_kind = 0U;
        uint64_t failed_kind = 0U;
        uint32_t size = 0U;
        uint32_t align = 0U;
        if (!r_llvm_jd_constant(emitter,
                                net ? "R_STD_NET_TCP_READ_RESULT_END" : "R_STD_IO_READ_RESULT_END",
                                &end_kind) ||
            !r_llvm_jd_constant(emitter,
                                net ? "R_STD_NET_TCP_READ_RESULT_FAILED"
                                    : "R_STD_IO_READ_RESULT_FAILED",
                                &failed_kind) ||
            !r_llvm_runtime_layout(emitter, "RRuntimeArray", &size, &align)) {
            return false;
        }
        (void)LLVMBuildMemCpy(emitter->builder,
                              r_llvm_std_member(emitter, out, "RStdJsonTransportRead", "buffer"),
                              align,
                              r_llvm_std_member(emitter, read, *read_type, "buffer"),
                              align,
                              r_llvm_u64(emitter, size));
        (void)LLVMBuildStore(emitter->builder,
                             LLVMBuildLoad2(emitter->builder,
                                            r_llvm_int(emitter, 64U),
                                            r_llvm_std_member(emitter, read, *read_type, "count"),
                                            ""),
                             r_llvm_std_member(emitter, out, "RStdJsonTransportRead", "count"));
        (void)LLVMBuildStore(
            emitter->builder,
            LLVMBuildZExt(
                emitter->builder,
                LLVMBuildICmp(emitter->builder, LLVMIntEQ, kind, r_llvm_u32(emitter, end_kind), ""),
                r_llvm_int(emitter, 8U),
                ""),
            r_llvm_std_member(emitter, out, "RStdJsonTransportRead", "end"));
        (void)LLVMBuildStore(
            emitter->builder,
            LLVMBuildZExt(
                emitter->builder,
                LLVMBuildICmp(
                    emitter->builder, LLVMIntEQ, kind, r_llvm_u32(emitter, failed_kind), ""),
                r_llvm_int(emitter, 8U),
                ""),
            r_llvm_std_member(emitter, out, "RStdJsonTransportRead", "failed"));
        (void)LLVMBuildStore(emitter->builder,
                             LLVMBuildLoad2(emitter->builder,
                                            r_llvm_int(emitter, 32U),
                                            r_llvm_std_member(emitter, error, error_type, "code"),
                                            ""),
                             r_llvm_std_member(emitter, out, "RStdJsonTransportRead", "code"));
        (void)LLVMBuildStore(
            emitter->builder,
            LLVMBuildLoad2(emitter->builder,
                           r_llvm_int(emitter, 64U),
                           r_llvm_std_member(emitter, error, error_type, "native_code"),
                           ""),
            r_llvm_std_member(emitter, out, "RStdJsonTransportRead", "native_code"));
        if (!r_llvm_runtime_layout(emitter, *read_type, &size, &align)) {
            return false;
        }
        (void)LLVMBuildMemSet(emitter->builder,
                              read,
                              LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                              r_llvm_u64(emitter, size),
                              align);
        (void)LLVMBuildRetVoid(emitter->builder);
    }
    /* deadline */
    r_llvm_js_begin(emitter, *deadline);
    {
        LLVMValueRef limit = LLVMGetParam(*deadline, 0U);
        LLVMValueRef out = LLVMGetParam(*deadline, 1U);
        LLVMValueRef now = r_llvm_std_structure(emitter, "RStdTimeInstantResult");
        LLVMValueRef ok;
        LLVMValueRef value;
        LLVMValueRef bound;
        LLVMValueRef seconds;
        LLVMValueRef limit_seconds;
        LLVMValueRef nanoseconds;
        LLVMValueRef limit_nanoseconds;
        LLVMBasicBlockRef none = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef check = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef early = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef late = r_llvm_jd_block(emitter);
        uint64_t transport_error = 0U;
        uint64_t timed_out = 0U;
        uint64_t other = 0U;
        if ((now == NULL) ||
            !r_llvm_jd_constant(emitter, "R_STD_JSON_READ_TRANSPORT_ERROR", &transport_error) ||
            !r_llvm_jd_constant(emitter,
                                net ? "R_STD_NET_ERROR_TIMED_OUT" : "R_STD_IO_ERROR_TIMED_OUT",
                                &timed_out) ||
            !r_llvm_jd_constant(
                emitter, net ? "R_STD_NET_ERROR_OTHER" : "R_STD_IO_ERROR_OTHER", &other)) {
            return false;
        }
        (void)LLVMBuildCondBr(
            emitter->builder,
            LLVMBuildICmp(
                emitter->builder,
                LLVMIntEQ,
                LLVMBuildLoad2(emitter->builder,
                               r_llvm_int(emitter, 8U),
                               r_llvm_std_member(emitter, limit, "RStdJsonDeadline", "has_value"),
                               ""),
                LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                ""),
            none,
            check);
        LLVMPositionBuilderAtEnd(emitter->builder, none);
        (void)LLVMBuildRet(emitter->builder, r_llvm_jd_true(emitter));
        LLVMPositionBuilderAtEnd(emitter->builder, check);
        if (r_llvm_call_runtime(emitter, "r_std_time_monotonic_now", NULL, 0U, now) == NULL) {
            return false;
        }
        ok = LLVMBuildICmp(
            emitter->builder,
            LLVMIntNE,
            LLVMBuildLoad2(emitter->builder,
                           r_llvm_int(emitter, 8U),
                           r_llvm_std_member(emitter, now, "RStdTimeInstantResult", "is_ok"),
                           ""),
            LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
            "");
        value = r_llvm_std_member(emitter, now, "RStdTimeInstantResult", "value");
        bound = r_llvm_std_member(emitter, limit, "RStdJsonDeadline", "value");
        seconds =
            LLVMBuildLoad2(emitter->builder,
                           r_llvm_int(emitter, 64U),
                           r_llvm_std_member(emitter, value, "RStdTimeInstant", "storage_seconds"),
                           "");
        limit_seconds =
            LLVMBuildLoad2(emitter->builder,
                           r_llvm_int(emitter, 64U),
                           r_llvm_std_member(emitter, bound, "RStdTimeInstant", "storage_seconds"),
                           "");
        nanoseconds = LLVMBuildLoad2(
            emitter->builder,
            r_llvm_int(emitter, 32U),
            r_llvm_std_member(emitter, value, "RStdTimeInstant", "storage_nanoseconds"),
            "");
        limit_nanoseconds = LLVMBuildLoad2(
            emitter->builder,
            r_llvm_int(emitter, 32U),
            r_llvm_std_member(emitter, bound, "RStdTimeInstant", "storage_nanoseconds"),
            "");
        (void)LLVMBuildCondBr(
            emitter->builder,
            LLVMBuildAnd(
                emitter->builder,
                ok,
                LLVMBuildOr(
                    emitter->builder,
                    LLVMBuildICmp(emitter->builder, LLVMIntSLT, seconds, limit_seconds, ""),
                    LLVMBuildAnd(
                        emitter->builder,
                        LLVMBuildICmp(emitter->builder, LLVMIntEQ, seconds, limit_seconds, ""),
                        LLVMBuildICmp(
                            emitter->builder, LLVMIntULT, nanoseconds, limit_nanoseconds, ""),
                        ""),
                    ""),
                ""),
            early,
            late);
        LLVMPositionBuilderAtEnd(emitter->builder, early);
        (void)LLVMBuildRet(emitter->builder, r_llvm_jd_true(emitter));
        LLVMPositionBuilderAtEnd(emitter->builder, late);
        (void)LLVMBuildMemSet(emitter->builder,
                              out,
                              LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                              r_llvm_u64(emitter, outcome_size),
                              8U);
        (void)LLVMBuildStore(emitter->builder,
                             r_llvm_u32(emitter, transport_error),
                             r_llvm_std_member(emitter, out, "RStdJsonReadOutcome", "status"));
        (void)LLVMBuildStore(
            emitter->builder,
            LLVMBuildSelect(emitter->builder,
                            ok,
                            r_llvm_u32(emitter, timed_out),
                            r_llvm_u32(emitter, other),
                            ""),
            r_llvm_std_member(emitter, out, "RStdJsonReadOutcome", "transport_code"));
        {
            LLVMValueRef native_code = LLVMBuildLoad2(
                emitter->builder,
                r_llvm_int(emitter, 64U),
                r_llvm_std_member(emitter,
                                  r_llvm_std_member(emitter, now, "RStdTimeInstantResult", "error"),
                                  "RStdTimeError",
                                  "native_code"),
                "");
            (void)LLVMBuildStore(
                emitter->builder,
                LLVMBuildSelect(emitter->builder, ok, r_llvm_u64(emitter, 0U), native_code, ""),
                r_llvm_std_member(emitter, out, "RStdJsonReadOutcome", "native_code"));
        }
        (void)LLVMBuildRet(emitter->builder, r_llvm_jd_false(emitter));
    }
    emitter->function = saved;
    return true;
}

/* The completion of a read into the carrier of its task (r_c17_json_reader_complete). */
static LLVMValueRef r_llvm_js_complete(RLlvmEmitter *emitter, RTypeId completion) {
    const RSemanticType *carrier = r_llvm_type(emitter, r_llvm_value_type(emitter, completion));
    LLVMTypeRef parameters[3];
    LLVMValueRef function;
    LLVMValueRef saved = emitter->function;
    LLVMValueRef out;
    LLVMValueRef decoder;
    LLVMValueRef outcome;
    LLVMValueRef choice;
    LLVMBasicBlockRef done;
    uint32_t outcome_size = 0U;
    uint32_t payload = 0U;
    uint32_t option_payload = 0U;
    uint64_t value_status = 0U;
    uint64_t end_status = 0U;
    uint32_t index;

    parameters[0] = r_llvm_pointer(emitter);
    parameters[1] = r_llvm_pointer(emitter);
    parameters[2] = r_llvm_pointer(emitter);
    function = r_llvm_js_function(emitter,
                                  "r_json_reader_complete",
                                  (uint32_t)completion,
                                  LLVMVoidTypeInContext(emitter->context),
                                  parameters,
                                  3U);
    if (LLVMCountBasicBlocks(function) != 0U) {
        return function;
    }
    if ((carrier == NULL) || !r_llvm_js_large(emitter, "RStdJsonReadOutcome", &outcome_size) ||
        !r_llvm_payload_offset(emitter, completion, &payload) ||
        !r_llvm_payload_offset(emitter, carrier->base, &option_payload) ||
        !r_llvm_jd_constant(emitter, "R_STD_JSON_READ_VALUE", &value_status) ||
        !r_llvm_jd_constant(emitter, "R_STD_JSON_READ_END", &end_status)) {
        return (carrier == NULL) ? (r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR), NULL) : NULL;
    }
    r_llvm_js_begin(emitter, function);
    out = LLVMGetParam(function, 0U);
    decoder = LLVMGetParam(function, 1U);
    outcome = LLVMGetParam(function, 2U);
    done = r_llvm_jd_block(emitter);
    choice = LLVMBuildSwitch(
        emitter->builder,
        LLVMBuildLoad2(emitter->builder,
                       r_llvm_int(emitter, 32U),
                       r_llvm_std_member(emitter, outcome, "RStdJsonReadOutcome", "status"),
                       ""),
        done,
        2U);
    {
        LLVMBasicBlockRef value = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef end = r_llvm_jd_block(emitter);
        LLVMValueRef arguments[2];
        LLVMAddCase(choice, r_llvm_u32(emitter, value_status), value);
        LLVMAddCase(choice, r_llvm_u32(emitter, end_status), end);
        LLVMPositionBuilderAtEnd(emitter->builder, value);
        arguments[0] = decoder;
        arguments[1] = r_llvm_byte_offset(emitter, out, payload + option_payload);
        if (r_llvm_call_runtime(emitter,
                                "r_json_decoder_take",
                                arguments,
                                2U,
                                r_llvm_std_structure(emitter, "RStdJsonResult")) == NULL) {
            return NULL;
        }
        (void)LLVMBuildStore(
            emitter->builder, r_llvm_u32(emitter, 1U), r_llvm_byte_offset(emitter, out, payload));
        (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 0U), out);
        (void)LLVMBuildBr(emitter->builder, done);
        LLVMPositionBuilderAtEnd(emitter->builder, end);
        (void)LLVMBuildStore(
            emitter->builder, r_llvm_u32(emitter, 0U), r_llvm_byte_offset(emitter, out, payload));
        (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 0U), out);
        (void)LLVMBuildBr(emitter->builder, done);
    }
    for (index = 0U; index < r_semantic_effect_count(emitter->frontend, carrier->second); ++index) {
        const RTypeId error = r_semantic_effect_at(emitter->frontend, carrier->second, index);
        const bool allocation = r_llvm_standard_named(emitter, error, "std.alloc::alloc_error");
        const bool json = r_llvm_standard_named(emitter, error, "std.json::error");
        const bool net = r_llvm_standard_named(emitter, error, "std.net::net_error");
        LLVMBasicBlockRef failed = r_llvm_jd_block(emitter);
        uint64_t status = 0U;
        uint32_t size = 0U;
        uint32_t align = 0U;
        if (!r_llvm_jd_constant(emitter,
                                allocation ? "R_STD_JSON_READ_ALLOCATION_ERROR"
                                : json     ? "R_STD_JSON_READ_JSON_ERROR"
                                           : "R_STD_JSON_READ_TRANSPORT_ERROR",
                                &status)) {
            return NULL;
        }
        LLVMAddCase(choice, r_llvm_u32(emitter, status), failed);
        LLVMPositionBuilderAtEnd(emitter->builder, failed);
        (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, index + 1U), out);
        if (allocation || json) {
            LLVMValueRef json_outcome =
                r_llvm_std_member(emitter, outcome, "RStdJsonReadOutcome", "json");
            if (!r_llvm_runtime_layout(
                    emitter, allocation ? "RStdAllocError" : "RStdJsonError", &size, &align)) {
                return NULL;
            }
            (void)LLVMBuildMemCpy(emitter->builder,
                                  r_llvm_byte_offset(emitter, out, payload),
                                  align,
                                  r_llvm_std_member(emitter,
                                                    json_outcome,
                                                    "RStdJsonResult",
                                                    allocation ? "allocation_error" : "error"),
                                  align,
                                  r_llvm_u64(emitter, size));
        } else {
            const char *error_type = net ? "RStdNetError" : "RStdIoError";
            LLVMValueRef destination = r_llvm_byte_offset(emitter, out, payload);
            (void)LLVMBuildStore(
                emitter->builder,
                LLVMBuildLoad2(
                    emitter->builder,
                    r_llvm_int(emitter, 32U),
                    r_llvm_std_member(emitter, outcome, "RStdJsonReadOutcome", "transport_code"),
                    ""),
                r_llvm_std_member(emitter, destination, error_type, "code"));
            (void)LLVMBuildStore(
                emitter->builder,
                LLVMBuildLoad2(
                    emitter->builder,
                    r_llvm_int(emitter, 64U),
                    r_llvm_std_member(emitter, outcome, "RStdJsonReadOutcome", "native_code"),
                    ""),
                r_llvm_std_member(emitter, destination, error_type, "native_code"));
        }
        (void)LLVMBuildBr(emitter->builder, done);
    }
    LLVMPositionBuilderAtEnd(emitter->builder, done);
    (void)LLVMBuildRetVoid(emitter->builder);
    emitter->function = saved;
    return function;
}

/* The carrier of the task of read_next: the effect carrier whose payload and errors the task's
   type names (r_c17_json_reader_completion). */
static RTypeId r_llvm_js_completion(RLlvmEmitter *emitter, RTypeId task_id) {
    const RSemanticType *task = r_llvm_type(emitter, r_llvm_value_type(emitter, task_id));
    size_t index;

    if (task == NULL) {
        return R_TYPE_ID_INVALID;
    }
    for (index = 0U; index < emitter->frontend->semantic_type_count; ++index) {
        const RSemanticType *type = &emitter->frontend->semantic_types[index];
        if ((type->kind == R_SEMANTIC_TYPE_EFFECT_CARRIER) && (type->base == task->base) &&
            (type->second == task->second)) {
            return (RTypeId)(index + 1U);
        }
    }
    return R_TYPE_ID_INVALID;
}

static bool r_llvm_js_reader_function(RLlvmEmitter *emitter, const RSemanticSymbol *symbol) {
    const RJsonOperation operation = (RJsonOperation)symbol->json_operation;
    LLVMValueRef carrier = LLVMGetParam(emitter->function, 0U);
    LLVMValueRef saved = emitter->function;

    if (operation == R_JSON_OPERATION_READ_NEXT) {
        const RSemanticType *carrier_type =
            r_llvm_type(emitter, r_llvm_value_type(emitter, symbol->effect_carrier_type));
        const RTypeId completion = r_llvm_js_completion(emitter, symbol->return_type);
        LLVMValueRef complete;
        LLVMValueRef create;
        LLVMValueRef deadline = r_llvm_std_structure(emitter, "RStdJsonDeadline");
        LLVMValueRef started = r_llvm_std_structure(emitter, "RStdJsonTaskStartResult");
        LLVMValueRef option = LLVMGetParam(emitter->function, 2U);
        LLVMValueRef arguments[6];
        LLVMBasicBlockRef some = r_llvm_jd_block(emitter);
        LLVMBasicBlockRef next = r_llvm_jd_block(emitter);
        uint32_t option_payload = 0U;
        uint32_t payload = 0U;
        uint32_t size = 0U;
        uint32_t align = 0U;
        if (r_llvm_scalar_type(emitter, r_llvm_js_parameter_type(emitter, symbol, 1U)) != NULL) {
            return r_llvm_unsupported(emitter, "a JSON read deadline in a register");
        }
        if ((completion == R_TYPE_ID_INVALID) || (carrier_type == NULL) || (deadline == NULL) ||
            (started == NULL) ||
            !r_llvm_payload_offset(
                emitter, r_llvm_js_parameter_type(emitter, symbol, 1U), &option_payload) ||
            !r_llvm_payload_offset(emitter, symbol->effect_carrier_type, &payload) ||
            !r_llvm_runtime_layout(emitter, "RStdTimeInstant", &size, &align)) {
            return (completion == R_TYPE_ID_INVALID) || (carrier_type == NULL)
                       ? r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR)
                       : false;
        }
        (void)LLVMBuildCondBr(
            emitter->builder,
            LLVMBuildICmp(emitter->builder,
                          LLVMIntNE,
                          LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), option, ""),
                          r_llvm_u32(emitter, 0U),
                          ""),
            some,
            next);
        LLVMPositionBuilderAtEnd(emitter->builder, some);
        (void)LLVMBuildStore(emitter->builder,
                             LLVMConstInt(r_llvm_int(emitter, 8U), 1U, 0),
                             r_llvm_std_member(emitter, deadline, "RStdJsonDeadline", "has_value"));
        (void)LLVMBuildMemCpy(emitter->builder,
                              r_llvm_std_member(emitter, deadline, "RStdJsonDeadline", "value"),
                              align,
                              r_llvm_byte_offset(emitter, option, option_payload),
                              align,
                              r_llvm_u64(emitter, size));
        (void)LLVMBuildBr(emitter->builder, next);
        LLVMPositionBuilderAtEnd(emitter->builder, next);
        {
            LLVMValueRef value = r_llvm_std_member(emitter, deadline, "RStdJsonDeadline", "value");
            arguments[0] = r_llvm_std_member(emitter, deadline, "RStdJsonDeadline", "has_value");
            arguments[1] = r_llvm_std_member(emitter, value, "RStdTimeInstant", "storage_seconds");
            arguments[2] =
                r_llvm_std_member(emitter, value, "RStdTimeInstant", "storage_nanoseconds");
            if (r_llvm_call_runtime(
                    emitter, "r_runtime_task_deadline_narrow", arguments, 3U, NULL) == NULL) {
                return false;
            }
        }
        create = r_llvm_js_create(emitter, symbol->json_type);
        complete = r_llvm_js_complete(emitter, completion);
        emitter->function = saved;
        if ((create == NULL) || (complete == NULL)) {
            return false;
        }
        LLVMPositionBuilderAtEnd(emitter->builder, LLVMGetLastBasicBlock(saved));
        arguments[0] = LLVMGetParam(emitter->function, 1U);
        arguments[1] = deadline;
        arguments[2] = r_llvm_type_info(emitter, symbol->json_type);
        arguments[3] = create;
        arguments[4] = r_llvm_type_info(emitter, completion);
        arguments[5] = complete;
        if ((arguments[2] == NULL) || (arguments[4] == NULL) ||
            (r_llvm_call_runtime(emitter, "r_json_reader_read_next", arguments, 6U, started) ==
             NULL)) {
            return false;
        }
        {
            LLVMBasicBlockRef ok = r_llvm_jd_block(emitter);
            LLVMBasicBlockRef failed = r_llvm_jd_block(emitter);
            LLVMBasicBlockRef done = r_llvm_jd_block(emitter);
            uint32_t error_size = 0U;
            uint32_t error_align = 0U;
            if (!r_llvm_runtime_layout(emitter, "RStdAsyncStartError", &error_size, &error_align)) {
                return false;
            }
            (void)LLVMBuildCondBr(
                emitter->builder,
                LLVMBuildICmp(
                    emitter->builder,
                    LLVMIntNE,
                    LLVMBuildLoad2(
                        emitter->builder,
                        r_llvm_int(emitter, 8U),
                        r_llvm_std_member(emitter, started, "RStdJsonTaskStartResult", "is_ok"),
                        ""),
                    LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                    ""),
                ok,
                failed);
            LLVMPositionBuilderAtEnd(emitter->builder, ok);
            (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 0U), carrier);
            (void)LLVMBuildStore(
                emitter->builder,
                LLVMBuildLoad2(
                    emitter->builder,
                    r_llvm_pointer(emitter),
                    r_llvm_std_member(emitter, started, "RStdJsonTaskStartResult", "task"),
                    ""),
                r_llvm_byte_offset(emitter, carrier, payload));
            (void)LLVMBuildBr(emitter->builder, done);
            LLVMPositionBuilderAtEnd(emitter->builder, failed);
            (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 1U), carrier);
            (void)LLVMBuildMemCpy(
                emitter->builder,
                r_llvm_byte_offset(emitter, carrier, payload),
                error_align,
                r_llvm_std_member(emitter, started, "RStdJsonTaskStartResult", "error"),
                error_align,
                r_llvm_u64(emitter, error_size));
            (void)LLVMBuildBr(emitter->builder, done);
            LLVMPositionBuilderAtEnd(emitter->builder, done);
        }
        (void)LLVMBuildRetVoid(emitter->builder);
        return true;
    }
    {
        LLVMValueRef outcome = r_llvm_std_structure(emitter, "RStdJsonResult");
        LLVMValueRef value;
        uint32_t size = 0U;
        uint32_t align = 0U;
        if ((outcome == NULL) || !r_llvm_layout(emitter, symbol->return_type, &size, &align)) {
            return false;
        }
        value = r_llvm_entry_alloca(emitter, size, align, "value");
        if (operation == R_JSON_OPERATION_NEW_READER) {
            LLVMValueRef start = NULL;
            LLVMValueRef take = NULL;
            LLVMValueRef deadline = NULL;
            const char *read_type = NULL;
            LLVMValueRef transport = r_llvm_std_structure(emitter, "RStdJsonTransport");
            LLVMValueRef options;
            LLVMValueRef arguments[5];
            uint32_t info_size = 0U;
            uint32_t info_align = 0U;
            uint32_t read_size = 0U;
            uint32_t read_align = 0U;
            if ((transport == NULL) || !r_llvm_js_options_argument(emitter, symbol, 1U, &options) ||
                !r_llvm_js_transport(
                    emitter, symbol->json_type, &start, &take, &deadline, &read_type)) {
                return false;
            }
            emitter->function = saved;
            LLVMPositionBuilderAtEnd(emitter->builder, LLVMGetLastBasicBlock(saved));
            if (!r_llvm_runtime_layout(emitter, "RRuntimeTypeInfo", &info_size, &info_align) ||
                !r_llvm_runtime_layout(emitter, read_type, &read_size, &read_align)) {
                return false;
            }
            {
                LLVMValueRef handle_info = r_llvm_type_info(emitter, symbol->json_type);
                LLVMValueRef read_info =
                    r_llvm_std_member(emitter, transport, "RStdJsonTransport", "read_type");
                if (handle_info == NULL) {
                    return false;
                }
                (void)LLVMBuildMemCpy(
                    emitter->builder,
                    r_llvm_std_member(emitter, transport, "RStdJsonTransport", "handle_type"),
                    info_align,
                    handle_info,
                    info_align,
                    r_llvm_u64(emitter, info_size));
                (void)LLVMBuildStore(
                    emitter->builder,
                    r_llvm_u64(emitter, read_size),
                    r_llvm_std_member(emitter, read_info, "RRuntimeTypeInfo", "size"));
                (void)LLVMBuildStore(
                    emitter->builder,
                    r_llvm_u64(emitter, read_align),
                    r_llvm_std_member(emitter, read_info, "RRuntimeTypeInfo", "alignment"));
            }
            (void)LLVMBuildStore(
                emitter->builder,
                start,
                r_llvm_std_member(emitter, transport, "RStdJsonTransport", "start"));
            (void)LLVMBuildStore(
                emitter->builder,
                take,
                r_llvm_std_member(emitter, transport, "RStdJsonTransport", "take"));
            (void)LLVMBuildStore(
                emitter->builder,
                deadline,
                r_llvm_std_member(emitter, transport, "RStdJsonTransport", "deadline"));
            arguments[0] = value;
            arguments[1] = r_llvm_std_allocator(emitter);
            arguments[2] = options;
            arguments[3] = transport;
            arguments[4] = r_llvm_js_parameter_address(emitter, symbol, 0U);
            if ((arguments[1] == NULL) || (arguments[4] == NULL) ||
                (r_llvm_call_runtime(emitter, "r_json_reader_initialize", arguments, 5U, outcome) ==
                 NULL)) {
                return false;
            }
        } else {
            LLVMValueRef arguments[2];
            arguments[0] = LLVMGetParam(emitter->function, 1U);
            arguments[1] = value;
            if (r_llvm_call_runtime(emitter,
                                    operation == R_JSON_OPERATION_DETACH ? "r_json_reader_detach"
                                    : operation == R_JSON_OPERATION_TAKE_HANDLE
                                        ? "r_json_detached_take_handle"
                                        : "r_json_detached_take_bytes",
                                    arguments,
                                    2U,
                                    outcome) == NULL) {
                return false;
            }
        }
        if (!r_llvm_js_carrier(emitter, symbol, carrier, outcome, value, symbol->return_type)) {
            return false;
        }
        (void)LLVMBuildRetVoid(emitter->builder);
    }
    return true;
}

bool r_llvm_define_json_stream(RLlvmEmitter *emitter, RSymbolId id, const RSemanticSymbol *symbol) {
    const RJsonOperation operation = (RJsonOperation)symbol->json_operation;

    emitter->mir = NULL;
    emitter->frame = NULL;
    emitter->symbol = symbol;
    emitter->symbol_id = id;
    emitter->function = emitter->functions[id];
    LLVMPositionBuilderAtEnd(
        emitter->builder,
        LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "entry"));
    if ((operation >= R_JSON_OPERATION_NEW_DECODER) && (operation <= R_JSON_OPERATION_TAKE)) {
        return r_llvm_js_decoder_function(emitter, symbol) && (emitter->status == R_FRONTEND_OK);
    }
    return r_llvm_js_reader_function(emitter, symbol) && (emitter->status == R_FRONTEND_OK);
}

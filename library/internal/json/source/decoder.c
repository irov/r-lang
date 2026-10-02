#include "r_library_json_internal.h"

#include <stdalign.h>
#include <string.h>

struct RJsonDecoderState {
    RStdJsonCursor cursor;
    RStdJsonDecodeFrame *top;
    RStdJsonDecodeCreateFn create;
    RRuntimeTypeInfo result_type;
    void *value;
    bool initialized;
    bool ready;
    bool ended;
    bool poisoned;
};

static void r_json_decoder_clear(struct RJsonDecoderState *state) {
    while (state->top != NULL) {
        RStdJsonDecodeFrame *frame = state->top;
        state->top = frame->parent;
        if (frame->drop != NULL)
            frame->drop(frame, false);
        r_runtime_allocator_deallocate(frame, frame->alignment);
    }
    if (state->initialized && state->result_type.drop != NULL)
        state->result_type.drop(state->value);
    state->initialized = false;
    state->ready = false;
}

bool r_json_decode_push(RStdJsonCursor *cursor,
                        size_t size,
                        size_t alignment,
                        RStdJsonDecodeStepFn step,
                        RStdJsonDecodeDropFn drop,
                        void *output,
                        bool quoted) {
    void *allocation = NULL;
    if (cursor->outcome.status != R_STD_JSON_CALL_SUCCESS)
        return false;
    if (cursor->decoder == NULL || size < sizeof(RStdJsonDecodeFrame) ||
        alignment < alignof(RStdJsonDecodeFrame) || step == NULL || output == NULL)
        return r_json_cursor_fail(cursor, R_STD_JSON_ERROR_INVALID_STATE);
    cursor->outcome = r_json_allocation_result(
        r_runtime_allocator_allocate(cursor->allocator, size, alignment, &allocation));
    if (cursor->outcome.status != R_STD_JSON_CALL_SUCCESS)
        return false;
    /* Zero frame bookkeeping, not a constructed value: generated initialization flags control
     * which owner slots can subsequently be accessed or destroyed. */
    (void)memset(allocation, 0, size);
    RStdJsonDecodeFrame *frame = allocation;
    *frame = (RStdJsonDecodeFrame){.parent = cursor->decoder->top,
                                   .step = step,
                                   .drop = drop,
                                   .output = output,
                                   .alignment = alignment,
                                   .quoted = quoted};
    cursor->decoder->top = frame;
    return true;
}

RStdJsonResult r_json_decoder_initialize(RStdJsonDecoder *decoder,
                                         RRuntimeAllocator *allocator,
                                         RStdJsonOptions options,
                                         RRuntimeTypeInfo result_type,
                                         RStdJsonDecodeCreateFn create) {
    void *allocation = NULL;
    RStdJsonResult result = {0};
    *decoder = (RStdJsonDecoder){0};
    if (allocator == NULL || create == NULL || result_type.size == 0U)
        return r_json_failure(R_STD_JSON_ERROR_INVALID_STATE, 0U);
    result =
        r_json_allocation_result(r_runtime_allocator_allocate(allocator,
                                                              sizeof(struct RJsonDecoderState),
                                                              alignof(struct RJsonDecoderState),
                                                              &allocation));
    if (result.status != R_STD_JSON_CALL_SUCCESS)
        return result;
    struct RJsonDecoderState *state = allocation;
    *state = (struct RJsonDecoderState){.create = create, .result_type = result_type};
    state->cursor.allocator = allocator;
    state->cursor.options = options;
    state->cursor.decoder = state;
    result = r_json_allocation_result(r_runtime_allocator_allocate(
        allocator, result_type.size, result_type.alignment, &state->value));
    if (result.status == R_STD_JSON_CALL_SUCCESS)
        result = r_json_scanner_initialize(&state->cursor.scanner, allocator, options);
    if (result.status != R_STD_JSON_CALL_SUCCESS) {
        r_runtime_allocator_deallocate(state->value, result_type.alignment);
        r_runtime_allocator_deallocate(state, alignof(struct RJsonDecoderState));
        return result;
    }
    decoder->state = state;
    return result;
}

static bool r_json_decoder_run(struct RJsonDecoderState *state) {
    RStdJsonCursor *cursor = &state->cursor;
    if (state->top == NULL && !state->initialized && !state->create(cursor, state->value, false))
        return false;
    while (state->top != NULL && cursor->outcome.status == R_STD_JSON_CALL_SUCCESS) {
        RStdJsonDecodeFrame *frame = state->top;
        RStdJsonDecodeStep step = frame->step(cursor, frame);
        if (step == R_STD_JSON_DECODE_ERROR) {
            if (cursor->outcome.status == R_STD_JSON_CALL_SUCCESS)
                (void)r_json_cursor_fail(cursor, R_STD_JSON_ERROR_INVALID_STATE);
            break;
        }
        if (step == R_STD_JSON_DECODE_WAIT) {
            if (cursor->has_token || state->top != frame)
                return r_json_cursor_fail(cursor, R_STD_JSON_ERROR_INVALID_STATE);
            return true;
        }
        if (step == R_STD_JSON_DECODE_COMPLETE) {
            if (state->top != frame)
                return r_json_cursor_fail(cursor, R_STD_JSON_ERROR_INVALID_STATE);
            state->top = frame->parent;
            if (frame->drop != NULL)
                frame->drop(frame, true);
            r_runtime_allocator_deallocate(frame, frame->alignment);
            if (state->top == NULL)
                state->initialized = true;
        }
    }
    if (cursor->has_token && cursor->outcome.status == R_STD_JSON_CALL_SUCCESS)
        (void)r_json_cursor_fail(cursor, R_STD_JSON_ERROR_TYPE);
    return cursor->outcome.status == R_STD_JSON_CALL_SUCCESS;
}

RStdJsonFeedResult
r_json_decoder_feed(RStdJsonDecoder *decoder, RStdJsonByteView input, bool final) {
    RStdJsonFeedResult result = {.state = R_STD_JSON_FEED_NEED_INPUT};
    struct RJsonDecoderState *state = decoder->state;
    if (state == NULL || state->poisoned || state->ready ||
        (input.data == NULL && input.length != 0U)) {
        result.outcome = r_json_failure(R_STD_JSON_ERROR_INVALID_STATE, 0U);
        return result;
    }
    if (state->ended) {
        result.state = R_STD_JSON_FEED_END;
        if (input.length != 0U)
            result.outcome = r_json_failure(R_STD_JSON_ERROR_INVALID_STATE, state->cursor.consumed);
        return result;
    }
    RStdJsonCursor *cursor = &state->cursor;
    for (;;) {
        RStdJsonFeedResult scanned = r_json_scanner_feed(
            &cursor->scanner,
            (RStdJsonByteView){input.data == NULL ? NULL : input.data + result.consumed,
                               input.length - result.consumed},
            final);
        result.consumed += scanned.consumed;
        cursor->consumed += scanned.consumed;
        cursor->outcome = scanned.outcome;
        cursor->token = scanned.token;
        cursor->has_token = scanned.state == R_STD_JSON_FEED_TOKEN;
        if (cursor->outcome.status != R_STD_JSON_CALL_SUCCESS)
            break;
        if (cursor->has_token) {
            if (!r_json_decoder_run(state))
                break;
            continue;
        }
        result.state = scanned.state;
        if (scanned.state == R_STD_JSON_FEED_VALUE_READY) {
            if (!state->initialized || state->top != NULL) {
                (void)r_json_cursor_fail(cursor, R_STD_JSON_ERROR_UNEXPECTED_EOF);
                break;
            }
            state->ready = true;
        } else if (scanned.state == R_STD_JSON_FEED_END) {
            if (state->initialized || state->top != NULL) {
                (void)r_json_cursor_fail(cursor, R_STD_JSON_ERROR_UNEXPECTED_EOF);
                break;
            }
            state->ended = true;
        }
        return result;
    }
    result.outcome = cursor->outcome;
    cursor->outcome = (RStdJsonResult){0};
    state->poisoned = true;
    r_json_decoder_clear(state);
    return result;
}

RStdJsonResult r_json_decoder_take(RStdJsonDecoder *decoder, void *output) {
    struct RJsonDecoderState *state = decoder->state;
    if (state == NULL || !state->ready || state->poisoned || output == NULL)
        return r_json_failure(R_STD_JSON_ERROR_INVALID_STATE, 0U);
    if (state->result_type.move_initialize != NULL)
        state->result_type.move_initialize(output, state->value);
    else
        (void)memcpy(output, state->value, state->result_type.size);
    state->initialized = false;
    state->ready = false;
    return (RStdJsonResult){0};
}

RStdJsonResult r_json_decoder_rebind(RStdJsonDecoder *decoder,
                                     RRuntimeTypeInfo result_type,
                                     RStdJsonDecodeCreateFn create) {
    struct RJsonDecoderState *state = decoder->state;
    if (state == NULL || state->poisoned || state->ready || state->initialized ||
        state->top != NULL || create == NULL || result_type.size == 0U)
        return r_json_failure(R_STD_JSON_ERROR_INVALID_STATE, 0U);
    if (state->result_type.size != result_type.size ||
        state->result_type.alignment != result_type.alignment) {
        void *replacement = NULL;
        RStdJsonResult result = r_json_allocation_result(r_runtime_allocator_allocate(
            state->cursor.allocator, result_type.size, result_type.alignment, &replacement));
        if (result.status != R_STD_JSON_CALL_SUCCESS)
            return result;
        r_runtime_allocator_deallocate(state->value, state->result_type.alignment);
        state->value = replacement;
    }
    state->result_type = result_type;
    state->create = create;
    return (RStdJsonResult){0};
}

void r_json_decoder_abort(RStdJsonDecoder *decoder) {
    if (decoder->state != NULL) {
        decoder->state->poisoned = true;
        r_json_decoder_clear(decoder->state);
    }
}

void r_json_decoder_destroy(RStdJsonDecoder *decoder) {
    struct RJsonDecoderState *state = decoder->state;
    if (state == NULL)
        return;
    r_json_decoder_clear(state);
    r_json_cursor_destroy(&state->cursor);
    r_runtime_allocator_deallocate(state->value, state->result_type.alignment);
    r_runtime_allocator_deallocate(state, alignof(struct RJsonDecoderState));
    *decoder = (RStdJsonDecoder){0};
}

typedef struct RJsonValueDecodeFrame {
    RStdJsonDecodeFrame base;
    RStdJsonTreeBuilder builder;
} RJsonValueDecodeFrame;

static RStdJsonDecodeStep r_json_value_decode_step(RStdJsonCursor *cursor,
                                                   RStdJsonDecodeFrame *base) {
    RJsonValueDecodeFrame *frame = (RJsonValueDecodeFrame *)base;
    if (base->state == 0U) {
        r_json_tree_builder_initialize(&frame->builder, cursor->allocator);
        base->state = 1U;
    }
    if (!cursor->has_token)
        return R_STD_JSON_DECODE_WAIT;
    if (!r_json_tree_builder_token(&frame->builder, cursor, base->quoted) ||
        !r_json_cursor_next(cursor))
        return R_STD_JSON_DECODE_ERROR;
    if (!frame->builder.complete)
        return R_STD_JSON_DECODE_PROGRESS;
    *(RStdJsonValue *)base->output = frame->builder.value;
    frame->builder.value = (RStdJsonValue){0};
    return R_STD_JSON_DECODE_COMPLETE;
}

static void r_json_value_decode_drop(RStdJsonDecodeFrame *base, bool complete) {
    RJsonValueDecodeFrame *frame = (RJsonValueDecodeFrame *)base;
    (void)complete;
    if (base->state != 0U)
        r_json_tree_builder_destroy(&frame->builder);
}

bool r_json_decode_value_create(RStdJsonCursor *cursor, void *output, bool quoted) {
    return r_json_decode_push(cursor,
                              sizeof(RJsonValueDecodeFrame),
                              alignof(RJsonValueDecodeFrame),
                              r_json_value_decode_step,
                              r_json_value_decode_drop,
                              output,
                              quoted);
}

typedef struct RJsonSkipDecodeFrame {
    RStdJsonDecodeFrame base;
    size_t depth;
} RJsonSkipDecodeFrame;
static RStdJsonDecodeStep r_json_skip_decode_step(RStdJsonCursor *cursor,
                                                  RStdJsonDecodeFrame *base) {
    RJsonSkipDecodeFrame *frame = (RJsonSkipDecodeFrame *)base;
    if (!cursor->has_token)
        return R_STD_JSON_DECODE_WAIT;
    if (cursor->token.kind == R_STD_JSON_TOKEN_ARRAY_BEGIN ||
        cursor->token.kind == R_STD_JSON_TOKEN_OBJECT_BEGIN)
        ++frame->depth;
    else if (cursor->token.kind == R_STD_JSON_TOKEN_ARRAY_END ||
             cursor->token.kind == R_STD_JSON_TOKEN_OBJECT_END) {
        if (frame->depth == 0U) {
            (void)r_json_cursor_fail(cursor, R_STD_JSON_ERROR_TYPE);
            return R_STD_JSON_DECODE_ERROR;
        }
        --frame->depth;
    } else if (cursor->token.kind == R_STD_JSON_TOKEN_KEY && frame->depth == 0U) {
        (void)r_json_cursor_fail(cursor, R_STD_JSON_ERROR_TYPE);
        return R_STD_JSON_DECODE_ERROR;
    }
    (void)r_json_cursor_next(cursor);
    return frame->depth == 0U ? R_STD_JSON_DECODE_COMPLETE : R_STD_JSON_DECODE_PROGRESS;
}
bool r_json_decode_skip_create(RStdJsonCursor *cursor, void *output, bool quoted) {
    return r_json_decode_push(cursor,
                              sizeof(RJsonSkipDecodeFrame),
                              alignof(RJsonSkipDecodeFrame),
                              r_json_skip_decode_step,
                              NULL,
                              output,
                              quoted);
}

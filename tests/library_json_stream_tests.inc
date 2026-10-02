/* Exercise compiled continuations with direct typed storage, independently of the DOM builder. */
typedef struct StringArrayFrame {
    RStdJsonDecodeFrame base;
    RStdString item;
} StringArrayFrame;
static size_t stream_string_drops;
static void stream_string_drop(void *value) {
    ++stream_string_drops;
    r_runtime_string_destroy(value);
}
static void stream_array_drop(void *value) {
    r_runtime_array_destroy(value);
}
static RStdJsonDecodeStep stream_string_step(RStdJsonCursor *cursor, RStdJsonDecodeFrame *frame) {
    if (!cursor->has_token)
        return R_STD_JSON_DECODE_WAIT;
    return r_json_cursor_string(cursor, frame->output) ? R_STD_JSON_DECODE_COMPLETE
                                                       : R_STD_JSON_DECODE_ERROR;
}
static bool stream_string_create(RStdJsonCursor *cursor, void *output, bool quoted) {
    return r_json_decode_push(cursor,
                              sizeof(RStdJsonDecodeFrame),
                              _Alignof(RStdJsonDecodeFrame),
                              stream_string_step,
                              NULL,
                              output,
                              quoted);
}
static RStdJsonDecodeStep stream_array_step(RStdJsonCursor *cursor, RStdJsonDecodeFrame *base) {
    StringArrayFrame *frame = (StringArrayFrame *)base;
    RRuntimeArray *output = base->output;
    if (base->state == 0U) {
        if (!cursor->has_token)
            return R_STD_JSON_DECODE_WAIT;
        if (!r_json_cursor_expect(cursor, R_STD_JSON_TOKEN_ARRAY_BEGIN))
            return R_STD_JSON_DECODE_ERROR;
        r_runtime_array_initialize(
            output,
            cursor->allocator,
            (RRuntimeTypeInfo){sizeof(RStdString), _Alignof(RStdString), NULL, stream_string_drop});
        base->state = 1U;
        (void)r_json_cursor_next(cursor);
    }
    if (base->state == 2U) {
        /* Child completion is committed even if its last token exhausted the fragment. */
        base->state = 3U;
        if (!r_json_cursor_array_push(cursor, output, &frame->item))
            return R_STD_JSON_DECODE_ERROR;
        base->state = 1U;
    }
    if (!cursor->has_token)
        return R_STD_JSON_DECODE_WAIT;
    if (cursor->token.kind == R_STD_JSON_TOKEN_ARRAY_END) {
        (void)r_json_cursor_next(cursor);
        return R_STD_JSON_DECODE_COMPLETE;
    }
    base->state = 2U;
    return stream_string_create(cursor, &frame->item, false) ? R_STD_JSON_DECODE_PROGRESS
                                                             : R_STD_JSON_DECODE_ERROR;
}
static void stream_array_frame_drop(RStdJsonDecodeFrame *base, bool complete) {
    StringArrayFrame *frame = (StringArrayFrame *)base;
    if (base->state == 3U)
        stream_string_drop(&frame->item);
    if (!complete && base->state != 0U)
        r_runtime_array_destroy(base->output);
}
static bool stream_array_create(RStdJsonCursor *cursor, void *output, bool quoted) {
    return r_json_decode_push(cursor,
                              sizeof(StringArrayFrame),
                              _Alignof(StringArrayFrame),
                              stream_array_step,
                              stream_array_frame_drop,
                              output,
                              quoted);
}
static void stream_value_drop(void *value) {
    r_json_value_destroy(value);
}
static RRuntimeTypeInfo stream_array_type(void) {
    return (RRuntimeTypeInfo){
        sizeof(RRuntimeArray), _Alignof(RRuntimeArray), NULL, stream_array_drop};
}
static RRuntimeTypeInfo stream_value_type(void) {
    return (RRuntimeTypeInfo){
        sizeof(RStdJsonValue), _Alignof(RStdJsonValue), NULL, stream_value_drop};
}
static int test_stream_boundaries(void) {
    const char *source = "[\"Madrid\",\"\\uD83D\\uDE00\",\"\xe2\x82\xac\"]";
    size_t length = strlen(source);
    for (size_t split = 0U; split <= length; ++split) {
        RRuntimeAllocator allocator;
        RStdJsonDecoder decoder;
        RRuntimeArray result;
        uint8_t buffer[128];
        r_runtime_allocator_initialize(&allocator);
        stream_string_drops = 0U;
        CHECK(r_json_decoder_initialize(&decoder,
                                        &allocator,
                                        R_STD_JSON_DEFAULT_OPTIONS,
                                        stream_array_type(),
                                        stream_array_create)
                  .status == R_STD_JSON_CALL_SUCCESS);
        memcpy(buffer, source, split);
        RStdJsonFeedResult fed =
            r_json_decoder_feed(&decoder, (RStdJsonByteView){buffer, split}, false);
        CHECK(fed.outcome.status == R_STD_JSON_CALL_SUCCESS);
        CHECK(fed.state == R_STD_JSON_FEED_NEED_INPUT && fed.consumed == split);
        memset(buffer, 0xff, sizeof(buffer));
        memcpy(buffer, source + split, length - split);
        fed = r_json_decoder_feed(&decoder, (RStdJsonByteView){buffer, length - split}, true);
        CHECK(fed.outcome.status == R_STD_JSON_CALL_SUCCESS);
        CHECK(fed.state == R_STD_JSON_FEED_VALUE_READY && fed.consumed == length - split);
        memset(buffer, 0xff, sizeof(buffer));
        CHECK(r_json_decoder_take(&decoder, &result).status == R_STD_JSON_CALL_SUCCESS);
        CHECK(result.length == 3U);
        const RStdString *strings = result.data;
        const char *const expected[] = {"Madrid", "\xf0\x9f\x98\x80", "\xe2\x82\xac"};
        for (size_t i = 0U; i < 3U; ++i)
            CHECK(equal((RStdJsonByteView){r_runtime_string_bytes(&strings[i]),
                                           r_runtime_string_length(&strings[i])},
                        view(expected[i])));
        fed = r_json_decoder_feed(&decoder, (RStdJsonByteView){NULL, 0U}, true);
        CHECK(fed.outcome.status == R_STD_JSON_CALL_SUCCESS && fed.state == R_STD_JSON_FEED_END);
        r_json_decoder_destroy(&decoder);
        CHECK(stream_string_drops == 0U);
        r_runtime_array_destroy(&result);
        CHECK(stream_string_drops == 3U);
    }
    return 0;
}
static int test_stream_sequences(void) {
    const char *documents[] = {"{} [1,2] true null \"x\" -1e2", "[{},[1,2],true,null,\"x\",-1e2]"};
    for (size_t mode = 0U; mode < 2U; ++mode) {
        size_t length = strlen(documents[mode]);
        for (size_t chunk = 1U; chunk <= length; ++chunk) {
            RRuntimeAllocator allocator;
            RStdJsonDecoder decoder;
            RStdJsonOptions options = R_STD_JSON_DEFAULT_OPTIONS;
            options.mode = mode == 0U ? R_STD_JSON_MODE_SEQUENCE : R_STD_JSON_MODE_ARRAY_ELEMENTS;
            options.max_value_bytes = 5U;
            r_runtime_allocator_initialize(&allocator);
            CHECK(
                r_json_decoder_initialize(
                    &decoder, &allocator, options, stream_value_type(), r_json_decode_value_create)
                    .status == R_STD_JSON_CALL_SUCCESS);
            size_t offset = 0U, count = 0U;
            bool ended = false;
            while (!ended) {
                size_t size = length - offset;
                if (size > chunk)
                    size = chunk;
                uint8_t buffer[128];
                memcpy(buffer, documents[mode] + offset, size);
                RStdJsonFeedResult fed = r_json_decoder_feed(
                    &decoder, (RStdJsonByteView){buffer, size}, offset + size == length);
                memset(buffer, 0xff, sizeof(buffer));
                CHECK(fed.outcome.status == R_STD_JSON_CALL_SUCCESS);
                offset += fed.consumed;
                if (fed.state == R_STD_JSON_FEED_VALUE_READY) {
                    RStdJsonValue value;
                    CHECK(r_json_decoder_take(&decoder, &value).status == R_STD_JSON_CALL_SUCCESS);
                    RStdJsonKind expected[] = {R_STD_JSON_OBJECT,
                                               R_STD_JSON_ARRAY,
                                               R_STD_JSON_BOOLEAN,
                                               R_STD_JSON_NULL,
                                               R_STD_JSON_STRING,
                                               R_STD_JSON_NUMBER};
                    CHECK(count < 6U && r_std_json_kind(&value) == expected[count]);
                    ++count;
                    r_json_value_destroy(&value);
                } else if (fed.state == R_STD_JSON_FEED_END)
                    ended = true;
                else
                    CHECK(fed.state == R_STD_JSON_FEED_NEED_INPUT && fed.consumed == size);
            }
            CHECK(count == 6U && offset == length);
            r_json_decoder_destroy(&decoder);
        }
    }
    return 0;
}
static int test_stream_failures(void) {
    RRuntimeAllocator allocator;
    r_runtime_allocator_initialize(&allocator);
    const char *const bad[] = {"[\"kept\",false]", "[\"kept\",\"unfinished", "[\"kept\"] x"};
    for (size_t i = 0U; i < 3U; ++i) {
        RStdJsonDecoder decoder;
        stream_string_drops = 0U;
        CHECK(r_json_decoder_initialize(&decoder,
                                        &allocator,
                                        R_STD_JSON_DEFAULT_OPTIONS,
                                        stream_array_type(),
                                        stream_array_create)
                  .status == R_STD_JSON_CALL_SUCCESS);
        RStdJsonFeedResult fed = r_json_decoder_feed(&decoder, view(bad[i]), true);
        CHECK(fed.outcome.status == R_STD_JSON_CALL_JSON_ERROR);
        CHECK(stream_string_drops == 1U);
        r_json_error_destroy(&fed.outcome.error);
        fed = r_json_decoder_feed(&decoder, view("[]"), true);
        CHECK(fed.outcome.status == R_STD_JSON_CALL_JSON_ERROR &&
              fed.outcome.error.code == R_STD_JSON_ERROR_INVALID_STATE);
        r_json_error_destroy(&fed.outcome.error);
        r_json_decoder_destroy(&decoder);
        CHECK(stream_string_drops == 1U);
    }
    for (size_t ready = 0U; ready < 2U; ++ready) {
        RStdJsonDecoder decoder;
        stream_string_drops = 0U;
        CHECK(r_json_decoder_initialize(&decoder,
                                        &allocator,
                                        R_STD_JSON_DEFAULT_OPTIONS,
                                        stream_array_type(),
                                        stream_array_create)
                  .status == R_STD_JSON_CALL_SUCCESS);
        RStdJsonFeedResult fed =
            r_json_decoder_feed(&decoder, view(ready ? "[\"owned\"]" : "[\"owned\","), ready != 0U);
        CHECK(fed.outcome.status == R_STD_JSON_CALL_SUCCESS && stream_string_drops == 0U);
        r_json_decoder_abort(&decoder);
        CHECK(stream_string_drops == 1U);
        r_json_decoder_abort(&decoder);
        r_json_decoder_destroy(&decoder);
        CHECK(stream_string_drops == 1U);
    }
    for (size_t dynamic = 0U; dynamic < 2U; ++dynamic) {
        uint64_t attempts = 0U;
        for (uint64_t fail = 0U; fail <= attempts; ++fail) {
            RStdJsonDecoder decoder;
            r_runtime_allocator_set_failure(&allocator, fail);
            RStdJsonResult result = r_json_decoder_initialize(
                &decoder,
                &allocator,
                R_STD_JSON_DEFAULT_OPTIONS,
                dynamic ? stream_value_type() : stream_array_type(),
                dynamic ? r_json_decode_value_create : stream_array_create);
            if (result.status == R_STD_JSON_CALL_SUCCESS) {
                const char *source =
                    dynamic ? "{\"a\":[1,{\"b\":\"owned\"}]}" : "[\"a\",\"b\",\"c\"]";
                size_t length = strlen(source);
                for (size_t i = 0U; i < length && result.status == R_STD_JSON_CALL_SUCCESS; ++i) {
                    uint8_t byte = (uint8_t)source[i];
                    RStdJsonFeedResult fed = r_json_decoder_feed(
                        &decoder, (RStdJsonByteView){&byte, 1U}, i + 1U == length);
                    result = fed.outcome;
                    byte = 0xff;
                }
            }
            if (fail == 0U) {
                CHECK(result.status == R_STD_JSON_CALL_SUCCESS);
                attempts = r_runtime_allocator_attempt_count(&allocator);
            } else
                CHECK(result.status == R_STD_JSON_CALL_ALLOCATION_ERROR);
            r_json_decoder_destroy(&decoder);
        }
    }
    return 0;
}

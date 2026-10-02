#include "r_library_json_internal.h"

#include <stdalign.h>

/* The writer appends valid UTF-8 spans and JSON escapes; no locale-dependent conversion is used. */
typedef struct RJsonWriter {
    RStdString text;
    RStdJsonOptions options;
    RStdJsonResult result;
} RJsonWriter;
static void r_json_write(RJsonWriter *writer, const uint8_t *text, size_t length) {
    if (writer->result.status != R_STD_JSON_CALL_SUCCESS)
        return;
    if (length > writer->options.max_value_bytes - r_runtime_string_length(&writer->text)) {
        writer->result =
            r_json_failure(R_STD_JSON_ERROR_SIZE_LIMIT, r_runtime_string_length(&writer->text));
        return;
    }
    writer->result = r_json_string_result(r_runtime_string_append(&writer->text, text, length));
}
static void r_json_write_byte(RJsonWriter *writer, uint8_t c) {
    r_json_write(writer, &c, 1U);
}
static void r_json_write_indent(RJsonWriter *writer, size_t depth) {
    if (writer->options.indent == 0U)
        return;
    r_json_write_byte(writer, '\n');
    for (size_t i = 0U; i < depth && writer->result.status == R_STD_JSON_CALL_SUCCESS; ++i)
        for (size_t j = 0U;
             j < writer->options.indent && writer->result.status == R_STD_JSON_CALL_SUCCESS;
             ++j)
            r_json_write_byte(writer, ' ');
}
static void r_json_write_quoted(RJsonWriter *writer, RStdJsonByteView text) {
    size_t begin = 0U;
    static const uint8_t hex[] = "0123456789abcdef";
    r_json_write_byte(writer, '"');
    for (size_t i = 0U; i < text.length && writer->result.status == R_STD_JSON_CALL_SUCCESS; ++i) {
        uint8_t c = text.data[i];
        uint8_t escape[6] = {'\\', 'u', '0', '0', hex[c >> 4U], hex[c & 15U]};
        size_t length = 2U;
        if (c != '"' && c != '\\' && c >= 0x20U)
            continue;
        r_json_write(writer, text.data + begin, i - begin);
        switch (c) {
        case '"':
        case '\\':
            escape[1] = c;
            break;
        case '\b':
            escape[1] = 'b';
            break;
        case '\f':
            escape[1] = 'f';
            break;
        case '\n':
            escape[1] = 'n';
            break;
        case '\r':
            escape[1] = 'r';
            break;
        case '\t':
            escape[1] = 't';
            break;
        default:
            length = 6U;
            break;
        }
        r_json_write(writer, escape, length);
        begin = i + 1U;
    }
    if (begin < text.length)
        r_json_write(writer, text.data + begin, text.length - begin);
    r_json_write_byte(writer, '"');
}
typedef struct RJsonWriteFrame {
    const struct RJsonNode *node;
    size_t next;
} RJsonWriteFrame;

static void
r_json_write_value(RJsonWriter *writer, const RStdJsonValue *value, RRuntimeAllocator *allocator) {
    RRuntimeArray frames;
    r_runtime_array_initialize(
        &frames,
        allocator,
        (RRuntimeTypeInfo){sizeof(RJsonWriteFrame), alignof(RJsonWriteFrame), NULL, NULL});
    while (writer->result.status == R_STD_JSON_CALL_SUCCESS &&
           (value != NULL || frames.length != 0U)) {
        if (value != NULL) {
            const struct RJsonNode *node = value->node;
            value = NULL;
            if (node == NULL || node->kind == R_STD_JSON_NULL) {
                r_json_write(writer, (const uint8_t *)"null", 4U);
                continue;
            }
            switch (node->kind) {
            case R_STD_JSON_NULL:
                break;
            case R_STD_JSON_BOOLEAN:
                r_json_write(writer,
                             (const uint8_t *)(node->as.boolean ? "true" : "false"),
                             node->as.boolean ? 4U : 5U);
                break;
            case R_STD_JSON_NUMBER:
                r_json_write(writer,
                             r_runtime_string_bytes(&node->as.text),
                             r_runtime_string_length(&node->as.text));
                break;
            case R_STD_JSON_STRING:
                r_json_write_quoted(writer, r_json_string_view(&node->as.text));
                break;
            case R_STD_JSON_ARRAY:
            case R_STD_JSON_OBJECT: {
                RJsonWriteFrame frame = {node, 0U};
                if (frames.length >= writer->options.max_depth) {
                    writer->result = r_json_failure(R_STD_JSON_ERROR_DEPTH_LIMIT,
                                                    r_runtime_string_length(&writer->text));
                    break;
                }
                r_json_write_byte(writer, node->kind == R_STD_JSON_OBJECT ? '{' : '[');
                if (writer->result.status == R_STD_JSON_CALL_SUCCESS)
                    writer->result = r_json_array_result(r_runtime_array_push(&frames, &frame));
                break;
            }
            }
            continue;
        }
        RJsonWriteFrame *frame = &((RJsonWriteFrame *)frames.data)[frames.length - 1U];
        const struct RJsonNode *node = frame->node;
        bool object = node->kind == R_STD_JSON_OBJECT;
        size_t length = object ? node->as.members.length : node->as.elements.length;
        if (frame->next == length) {
            --frames.length;
            if (length != 0U)
                r_json_write_indent(writer, frames.length);
            r_json_write_byte(writer, object ? '}' : ']');
            continue;
        }
        if (frame->next != 0U)
            r_json_write_byte(writer, ',');
        r_json_write_indent(writer, frames.length);
        if (object) {
            const RJsonMember *member = &((const RJsonMember *)node->as.members.data)[frame->next];
            r_json_write_quoted(writer, r_json_string_view(&member->key));
            r_json_write_byte(writer, ':');
            if (writer->options.indent != 0U)
                r_json_write_byte(writer, ' ');
            value = &member->value;
        } else
            value = &((const RStdJsonValue *)node->as.elements.data)[frame->next];
        ++frame->next;
    }
    r_runtime_array_destroy(&frames);
}
RStdJsonStringResult r_json_stringify(RRuntimeAllocator *allocator,
                                      const RStdJsonValue *value,
                                      RStdJsonOptions options) {
    RStdJsonStringResult result = {0};
    RJsonWriter writer = {0};
    writer.options = options;
    writer.result = r_json_string_result(r_runtime_string_initialize(&writer.text, allocator));
    r_json_write_value(&writer, value, allocator);
    result.outcome = writer.result;
    if (result.outcome.status == R_STD_JSON_CALL_SUCCESS)
        result.value = writer.text;
    else
        r_runtime_string_destroy(&writer.text);
    return result;
}

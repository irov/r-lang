#include "frontend_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef R_IO_WRITE_SHARED_SOURCE_PATH
#error R_IO_WRITE_SHARED_SOURCE_PATH is required
#endif
#ifndef R_IO_WRITE_SHARED_ARITY_PATH
#error R_IO_WRITE_SHARED_ARITY_PATH is required
#endif
#ifndef R_IO_WRITE_SHARED_STREAM_TYPE_PATH
#error R_IO_WRITE_SHARED_STREAM_TYPE_PATH is required
#endif
#ifndef R_IO_WRITE_SHARED_BUFFER_TYPE_PATH
#error R_IO_WRITE_SHARED_BUFFER_TYPE_PATH is required
#endif
#ifndef R_IO_WRITE_SHARED_OFFSET_TYPE_PATH
#error R_IO_WRITE_SHARED_OFFSET_TYPE_PATH is required
#endif
#ifndef R_IO_WRITE_SHARED_LENGTH_TYPE_PATH
#error R_IO_WRITE_SHARED_LENGTH_TYPE_PATH is required
#endif
#ifndef R_IO_WRITE_SHARED_DEADLINE_TYPE_PATH
#error R_IO_WRITE_SHARED_DEADLINE_TYPE_PATH is required
#endif
#ifndef R_IO_WRITE_SHARED_CONTEXT_TYPE_PATH
#error R_IO_WRITE_SHARED_CONTEXT_TYPE_PATH is required
#endif
#ifndef R_IO_WRITE_SHARED_MISSING_MOVE_PATH
#error R_IO_WRITE_SHARED_MISSING_MOVE_PATH is required
#endif
#ifndef R_IO_WRITE_SHARED_NESTED_MOVE_PATH
#error R_IO_WRITE_SHARED_NESTED_MOVE_PATH is required
#endif
#ifndef R_IO_WRITE_SHARED_SUCCESS_REUSE_PATH
#error R_IO_WRITE_SHARED_SUCCESS_REUSE_PATH is required
#endif
#ifndef R_IO_WRITE_SHARED_MISSING_VARIANT_PATH
#error R_IO_WRITE_SHARED_MISSING_VARIANT_PATH is required
#endif
#ifndef R_IO_WRITE_SHARED_DUPLICATE_VARIANT_PATH
#error R_IO_WRITE_SHARED_DUPLICATE_VARIANT_PATH is required
#endif
#ifndef R_IO_WRITE_SHARED_WRONG_VARIANT_PATH
#error R_IO_WRITE_SHARED_WRONG_VARIANT_PATH is required
#endif

typedef struct RIoWriteSharedDiagnosticCase {
    const char *path;
    const char *code;
    const char *rule_id;
} RIoWriteSharedDiagnosticCase;

typedef struct RIoWriteSharedBuffer {
    char *bytes;
    size_t length;
    size_t capacity;
    size_t call_count;
} RIoWriteSharedBuffer;

typedef struct RIoWriteSharedView {
    RMirInstruction *call;
    RMirInstruction *stream_borrow;
    RMirInstruction *buffer_move;
    RMirInstruction *offset;
    RMirInstruction *length;
    RMirInstruction *deadline;
    RMirInstruction *written_variant_payload;
    RMirInstruction *failed_variant_payload;
    RSemanticAggregate *failed_payload;
    RTypeId u8_type;
    RTypeId u16_type;
    RTypeId stream_borrow_type;
    RTypeId buffer_type;
    RTypeId array_type;
    RTypeId deadline_type;
    RTypeId start_type;
    RTypeId task_type;
    RTypeId logical_type;
} RIoWriteSharedView;

typedef enum RIoWriteSharedMutation {
    R_IO_WRITE_SHARED_MUTATE_OPERATION_IDENTITY = 0,
    R_IO_WRITE_SHARED_MUTATE_OPERAND_COUNT,
    R_IO_WRITE_SHARED_MUTATE_BORROW_MASK,
    R_IO_WRITE_SHARED_MUTATE_STREAM_BORROW_KIND,
    R_IO_WRITE_SHARED_MUTATE_STREAM_BORROW_FLAGS,
    R_IO_WRITE_SHARED_MUTATE_BUFFER_MOVE_KIND,
    R_IO_WRITE_SHARED_MUTATE_BUFFER_STAGED_BIT,
    R_IO_WRITE_SHARED_MUTATE_BUFFER_OWNER_KIND,
    R_IO_WRITE_SHARED_MUTATE_BUFFER_OWNER_FLAGS,
    R_IO_WRITE_SHARED_MUTATE_BUFFER_OWNER_SECOND,
    R_IO_WRITE_SHARED_MUTATE_BUFFER_ARRAY_KIND,
    R_IO_WRITE_SHARED_MUTATE_BUFFER_ELEMENT,
    R_IO_WRITE_SHARED_MUTATE_OFFSET_TYPE,
    R_IO_WRITE_SHARED_MUTATE_LENGTH_TYPE,
    R_IO_WRITE_SHARED_MUTATE_DEADLINE_OPTION,
    R_IO_WRITE_SHARED_MUTATE_DEADLINE_INSTANT,
    R_IO_WRITE_SHARED_MUTATE_START_KIND,
    R_IO_WRITE_SHARED_MUTATE_START_FLAGS,
    R_IO_WRITE_SHARED_MUTATE_START_BASE,
    R_IO_WRITE_SHARED_MUTATE_START_SECOND,
    R_IO_WRITE_SHARED_MUTATE_TASK_KIND,
    R_IO_WRITE_SHARED_MUTATE_TASK_FLAGS,
    R_IO_WRITE_SHARED_MUTATE_TASK_BASE,
    R_IO_WRITE_SHARED_MUTATE_TASK_SECOND,
    R_IO_WRITE_SHARED_MUTATE_LOGICAL_KIND,
    R_IO_WRITE_SHARED_MUTATE_LOGICAL_FLAGS,
    R_IO_WRITE_SHARED_MUTATE_LOGICAL_BASE,
    R_IO_WRITE_SHARED_MUTATE_LOGICAL_SECOND,
    R_IO_WRITE_SHARED_MUTATE_LOGICAL_CONST_WRAPPER,
    R_IO_WRITE_SHARED_MUTATE_WRITTEN_TAG,
    R_IO_WRITE_SHARED_MUTATE_WRITTEN_PAYLOAD_TYPE,
    R_IO_WRITE_SHARED_MUTATE_FAILED_TAG,
    R_IO_WRITE_SHARED_MUTATE_FAILED_FIELD_ORDER,
    R_IO_WRITE_SHARED_MUTATE_FAILED_ERROR_TYPE,
    R_IO_WRITE_SHARED_MUTATE_FAILED_ERROR_NAME,
    R_IO_WRITE_SHARED_MUTATE_FAILED_WRITTEN_TYPE,
    R_IO_WRITE_SHARED_MUTATE_FAILED_WRITTEN_NAME,
    R_IO_WRITE_SHARED_MUTATE_FAILED_BUFFER_TYPE,
    R_IO_WRITE_SHARED_MUTATE_FAILED_BUFFER_NAME,
    R_IO_WRITE_SHARED_MUTATE_SWITCH_MISSING,
    R_IO_WRITE_SHARED_MUTATE_SWITCH_DUPLICATE,
    R_IO_WRITE_SHARED_MUTATION_COUNT
} RIoWriteSharedMutation;

static int failures;

#define R_IO_WRITE_SHARED_CHECK(condition)                                                         \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            (void)fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);    \
            failures += 1;                                                                         \
        }                                                                                          \
    } while (false)

static bool r_io_write_shared_read_file(const char *path, uint8_t **bytes, size_t *length) {
    FILE *stream;
    long file_length;
    uint8_t *result;

    *bytes = NULL;
    *length = 0U;
    stream = fopen(path, "rb");
    if (stream == NULL) {
        return false;
    }
    if ((fseek(stream, 0L, SEEK_END) != 0) || ((file_length = ftell(stream)) < 0L) ||
        (fseek(stream, 0L, SEEK_SET) != 0)) {
        (void)fclose(stream);
        return false;
    }
    result = malloc((size_t)file_length + 1U);
    if (result == NULL) {
        (void)fclose(stream);
        return false;
    }
    if (((size_t)file_length != 0U) &&
        (fread(result, 1U, (size_t)file_length, stream) != (size_t)file_length)) {
        free(result);
        (void)fclose(stream);
        return false;
    }
    if (fclose(stream) != 0) {
        free(result);
        return false;
    }
    result[(size_t)file_length] = UINT8_C(0);
    *bytes = result;
    *length = (size_t)file_length;
    return true;
}

static bool r_io_write_shared_add_file(RFrontendContext *context, const char *path) {
    uint8_t *bytes = NULL;
    size_t length = 0U;
    RSourceId source_id = R_SOURCE_ID_INVALID;
    bool added;

    if (!r_io_write_shared_read_file(path, &bytes, &length)) {
        return false;
    }
    added = r_frontend_add_source(context, path, bytes, length, &source_id) == R_FRONTEND_OK;
    free(bytes);
    return added && (source_id != R_SOURCE_ID_INVALID);
}

static bool r_io_write_shared_write(void *user_data, const char *bytes, size_t length) {
    RIoWriteSharedBuffer *buffer = user_data;
    size_t required;
    size_t capacity;
    char *replacement;

    buffer->call_count += 1U;
    if (length > (SIZE_MAX - buffer->length - 1U)) {
        return false;
    }
    required = buffer->length + length + 1U;
    if (required > buffer->capacity) {
        capacity = buffer->capacity == 0U ? 1024U : buffer->capacity;
        while (capacity < required) {
            if (capacity > (SIZE_MAX / 2U)) {
                return false;
            }
            capacity *= 2U;
        }
        replacement = realloc(buffer->bytes, capacity);
        if (replacement == NULL) {
            return false;
        }
        buffer->bytes = replacement;
        buffer->capacity = capacity;
    }
    if (length != 0U) {
        (void)memcpy(buffer->bytes + buffer->length, bytes, length);
    }
    buffer->length += length;
    buffer->bytes[buffer->length] = '\0';
    return true;
}

static void r_io_write_shared_dispose_buffer(RIoWriteSharedBuffer *buffer) {
    free(buffer->bytes);
    (void)memset(buffer, 0, sizeof(*buffer));
}

static bool r_io_write_shared_intern_equals(const RFrontendContext *context,
                                            uint32_t intern_id,
                                            const char *name) {
    const RInternEntry *entry;
    size_t name_length;

    if ((intern_id == UINT32_C(0)) || ((size_t)intern_id > context->intern_count)) {
        return false;
    }
    entry = &context->intern_entries[(size_t)intern_id - 1U];
    name_length = strlen(name);
    return (entry->length == name_length) && (memcmp(entry->bytes, name, name_length) == 0);
}

static bool r_io_write_shared_standard_type_equals(const RFrontendContext *context,
                                                   RTypeId type_id,
                                                   const char *name) {
    const RSemanticType *type = r_semantic_type(context, type_id);

    return (type != NULL) && (type->kind == R_SEMANTIC_TYPE_STANDARD) &&
           (type->flags == R_SEMANTIC_TYPE_FLAG_NONE) && (type->base == R_TYPE_ID_INVALID) &&
           (type->second == R_TYPE_ID_INVALID) && (type->length != UINT64_C(0)) &&
           (type->length <= (uint64_t)UINT32_MAX) &&
           r_io_write_shared_intern_equals(context, (uint32_t)type->length, name);
}

static RMirInstruction *r_io_write_shared_value_definition(RFrontendContext *context,
                                                           RMirValueId value) {
    size_t index;

    if (value == R_MIR_VALUE_ID_INVALID) {
        return NULL;
    }
    for (index = 0U; index < context->mir_instruction_count; ++index) {
        RMirInstruction *instruction = &context->mir_instructions[index];

        if (instruction->result == value) {
            return instruction;
        }
    }
    return NULL;
}

static RSemanticAggregate *r_io_write_shared_find_aggregate(RFrontendContext *context,
                                                            const char *name) {
    size_t index;

    for (index = 0U; index < context->semantic_aggregate_count; ++index) {
        RSemanticAggregate *aggregate = &context->semantic_aggregates[index];

        if (r_io_write_shared_intern_equals(context, aggregate->name_intern_id, name)) {
            return aggregate;
        }
    }
    return NULL;
}

static RFrontendContext *r_io_write_shared_build_context(void) {
    RFrontendContext *context = r_frontend_create(NULL);

    if ((context == NULL) || !r_io_write_shared_add_file(context, R_IO_WRITE_SHARED_SOURCE_PATH) ||
        (r_frontend_analyze(context) != R_FRONTEND_OK) ||
        (r_frontend_diagnostic_count(context) != 0U) ||
        (r_frontend_lower_mir(context) != R_FRONTEND_OK) ||
        (r_frontend_diagnostic_count(context) != 0U)) {
        r_frontend_destroy(context);
        return NULL;
    }
    return context;
}

static bool r_io_write_shared_find_view(RFrontendContext *context, RIoWriteSharedView *view) {
    size_t index;
    size_t operand_index;
    RMirValueId operands[5];
    const RSemanticType *buffer_type;
    const RSemanticType *task_type;

    (void)memset(view, 0, sizeof(*view));
    view->u8_type = R_TYPE_ID_INVALID;
    view->u16_type = R_TYPE_ID_INVALID;
    for (index = 0U; index < context->semantic_type_count; ++index) {
        const RSemanticType *type = &context->semantic_types[index];

        if (type->kind == R_SEMANTIC_TYPE_U8) {
            view->u8_type = (RTypeId)index + UINT32_C(1);
        } else if (type->kind == R_SEMANTIC_TYPE_U16) {
            view->u16_type = (RTypeId)index + UINT32_C(1);
        }
    }
    for (index = 0U; index < context->mir_instruction_count; ++index) {
        RMirInstruction *instruction = &context->mir_instructions[index];

        if ((instruction->kind == R_MIR_INSTRUCTION_STANDARD_CALL) &&
            (instruction->standard_operation == R_STANDARD_CALL_IO_WRITE_SHARED)) {
            if (view->call != NULL) {
                return false;
            }
            view->call = instruction;
        }
    }
    if ((view->call == NULL) || (view->call->operand_count != UINT32_C(5))) {
        return false;
    }
    operand_index = (size_t)view->call->first_operand;
    if ((operand_index > context->mir_operand_count) ||
        ((context->mir_operand_count - operand_index) < 5U)) {
        return false;
    }
    for (index = 0U; index < 5U; ++index) {
        operands[index] = context->mir_operands[operand_index + index];
    }
    view->stream_borrow = r_io_write_shared_value_definition(context, operands[0]);
    view->buffer_move = r_io_write_shared_value_definition(context, operands[1]);
    view->offset = r_io_write_shared_value_definition(context, operands[2]);
    view->length = r_io_write_shared_value_definition(context, operands[3]);
    view->deadline = r_io_write_shared_value_definition(context, operands[4]);
    if ((view->stream_borrow == NULL) || (view->buffer_move == NULL) || (view->offset == NULL) ||
        (view->length == NULL) || (view->deadline == NULL)) {
        return false;
    }
    view->stream_borrow_type = view->stream_borrow->type;
    view->buffer_type = view->buffer_move->type;
    view->deadline_type = view->deadline->type;
    view->start_type = view->call->type;
    view->task_type = view->call->auxiliary_type;
    buffer_type = r_semantic_type(context, view->buffer_type);
    task_type = r_semantic_type(context, view->task_type);
    if ((buffer_type == NULL) || (task_type == NULL)) {
        return false;
    }
    view->array_type = buffer_type->base;
    view->logical_type = task_type->base;
    view->failed_payload =
        r_io_write_shared_find_aggregate(context, "std.io::shared_write_result::failed");
    if (view->failed_payload == NULL) {
        return false;
    }
    for (index = 0U; index < context->mir_instruction_count; ++index) {
        RMirInstruction *instruction = &context->mir_instructions[index];

        if ((instruction->kind != R_MIR_INSTRUCTION_VARIANT_PAYLOAD) ||
            (instruction->type == R_TYPE_ID_INVALID)) {
            continue;
        }
        if ((instruction->integer_value == UINT64_C(0)) &&
            (instruction->type == view->buffer_type)) {
            if (view->written_variant_payload != NULL) {
                return false;
            }
            view->written_variant_payload = instruction;
        } else if ((instruction->integer_value == UINT64_C(1)) &&
                   (instruction->type == view->failed_payload->type)) {
            if (view->failed_variant_payload != NULL) {
                return false;
            }
            view->failed_variant_payload = instruction;
        }
    }
    return (view->u8_type != R_TYPE_ID_INVALID) && (view->u16_type != R_TYPE_ID_INVALID) &&
           (view->array_type != R_TYPE_ID_INVALID) && (view->logical_type != R_TYPE_ID_INVALID) &&
           (view->written_variant_payload != NULL) && (view->failed_variant_payload != NULL);
}

static bool r_io_write_shared_view_is_valid(const RFrontendContext *context,
                                            const RIoWriteSharedView *view) {
    const RSemanticType *stream_borrow;
    const RSemanticType *buffer;
    const RSemanticType *array;
    const RSemanticType *offset;
    const RSemanticType *length;
    const RSemanticType *deadline;
    const RSemanticType *start;
    const RSemanticType *start_effects;
    const RSemanticType *task;
    const RSemanticType *logical;
    const RSemanticField *failed_fields;
    const RSemanticType *failed_written;
    const RSemanticType *failed_buffer;

    if ((view->call == NULL) || (view->stream_borrow == NULL) || (view->buffer_move == NULL) ||
        (view->offset == NULL) || (view->length == NULL) || (view->deadline == NULL) ||
        (view->failed_payload == NULL)) {
        return false;
    }
    stream_borrow = r_semantic_type(context, view->stream_borrow_type);
    buffer = r_semantic_type(context, view->buffer_type);
    array = r_semantic_type(context, view->array_type);
    offset = r_semantic_type(context, view->offset->type);
    length = r_semantic_type(context, view->length->type);
    deadline = r_semantic_type(context, view->deadline_type);
    start = r_semantic_type(context, view->start_type);
    start_effects = start == NULL ? NULL : r_semantic_type(context, start->second);
    task = r_semantic_type(context, view->task_type);
    logical = r_semantic_type(context, view->logical_type);
    if ((stream_borrow == NULL) || (buffer == NULL) || (array == NULL) || (offset == NULL) ||
        (length == NULL) || (deadline == NULL) || (start == NULL) || (start_effects == NULL) ||
        (task == NULL) || (logical == NULL) ||
        (view->failed_payload->kind != R_SEMANTIC_AGGREGATE_STRUCT) ||
        (view->failed_payload->field_count != UINT32_C(3)) ||
        ((size_t)view->failed_payload->first_field + 3U > context->semantic_field_count)) {
        return false;
    }
    failed_fields = &context->semantic_fields[view->failed_payload->first_field];
    failed_written = r_semantic_type(context, failed_fields[1].type);
    failed_buffer = r_semantic_type(context, failed_fields[2].type);
    return (view->call->standard_operation == R_STANDARD_CALL_IO_WRITE_SHARED) &&
           (view->call->operand_count == UINT32_C(5)) &&
           (view->call->call_borrow_mask.low == UINT64_C(1)) &&
           (view->call->call_borrow_mask.high == UINT64_C(0)) &&
           (view->stream_borrow->kind == R_MIR_INSTRUCTION_BORROW) &&
           (stream_borrow->kind == R_SEMANTIC_TYPE_BORROW) &&
           (stream_borrow->flags == R_SEMANTIC_TYPE_FLAG_SHARED) &&
           r_io_write_shared_standard_type_equals(context, stream_borrow->base, "std.io::output") &&
           (view->buffer_move->kind == R_MIR_INSTRUCTION_MOVE) &&
           view->buffer_move->is_async_staged_move && (buffer->kind == R_SEMANTIC_TYPE_ARC) &&
           (buffer->flags == R_SEMANTIC_TYPE_FLAG_NONE) && (buffer->second == R_TYPE_ID_INVALID) &&
           (array->kind == R_SEMANTIC_TYPE_ARRAY) && (array->flags == R_SEMANTIC_TYPE_FLAG_NONE) &&
           (array->base == view->u8_type) && (array->second == R_TYPE_ID_INVALID) &&
           (offset->kind == R_SEMANTIC_TYPE_USIZE) && (length->kind == R_SEMANTIC_TYPE_USIZE) &&
           (deadline->kind == R_SEMANTIC_TYPE_OPTION) &&
           (deadline->flags == R_SEMANTIC_TYPE_FLAG_NONE) &&
           (deadline->second == R_TYPE_ID_INVALID) &&
           r_io_write_shared_standard_type_equals(context, deadline->base, "std.time::instant") &&
           (start->kind == R_SEMANTIC_TYPE_EFFECT_CARRIER) &&
           (start->flags == R_SEMANTIC_TYPE_FLAG_NONE) && (start->base == view->task_type) &&
           (start_effects->kind == R_SEMANTIC_TYPE_EFFECT_SET) &&
           (r_semantic_effect_count(context, start->second) == UINT32_C(1)) &&
           r_io_write_shared_standard_type_equals(
               context,
               r_semantic_effect_at(context, start->second, UINT32_C(0)),
               "std.async::start_error") &&
           (task->kind == R_SEMANTIC_TYPE_TASK) && (task->flags == R_SEMANTIC_TYPE_FLAG_NONE) &&
           (task->base == view->logical_type) && (task->second == R_TYPE_ID_INVALID) &&
           r_io_write_shared_standard_type_equals(
               context, view->logical_type, "std.io::shared_write_result") &&
           (view->written_variant_payload->integer_value == UINT64_C(0)) &&
           (view->written_variant_payload->type == view->buffer_type) &&
           (view->failed_variant_payload->integer_value == UINT64_C(1)) &&
           (view->failed_variant_payload->type == view->failed_payload->type) &&
           r_io_write_shared_intern_equals(context, failed_fields[0].name_intern_id, "error") &&
           r_io_write_shared_standard_type_equals(
               context, failed_fields[0].type, "std.io::io_error") &&
           r_io_write_shared_intern_equals(context, failed_fields[1].name_intern_id, "written") &&
           (failed_written != NULL) && (failed_written->kind == R_SEMANTIC_TYPE_USIZE) &&
           r_io_write_shared_intern_equals(context, failed_fields[2].name_intern_id, "buffer") &&
           (failed_buffer != NULL) && (failed_buffer->kind == R_SEMANTIC_TYPE_ARC) &&
           (failed_buffer->base == view->array_type);
}

static RTypeId r_io_write_shared_append_const_type(RFrontendContext *context, RTypeId base) {
    RSemanticType type;
    RTypeId type_id;

    if ((context->semantic_type_count >= (size_t)UINT32_MAX) ||
        !r_grow_array(context,
                      (void **)&context->semantic_types,
                      &context->semantic_type_capacity,
                      sizeof(*context->semantic_types),
                      context->semantic_type_count + 1U)) {
        return R_TYPE_ID_INVALID;
    }
    (void)memset(&type, 0, sizeof(type));
    type.kind = R_SEMANTIC_TYPE_CONST;
    type.base = base;
    type.second = R_TYPE_ID_INVALID;
    type_id = (RTypeId)context->semantic_type_count + UINT32_C(1);
    context->semantic_types[context->semantic_type_count] = type;
    context->semantic_type_count += 1U;
    return type_id;
}

static bool r_io_write_shared_apply_mutation(RFrontendContext *context,
                                             RIoWriteSharedView *view,
                                             RIoWriteSharedMutation mutation) {
    RSemanticType *stream_borrow = &context->semantic_types[(size_t)view->stream_borrow_type - 1U];
    RSemanticType *buffer = &context->semantic_types[(size_t)view->buffer_type - 1U];
    RSemanticType *array = &context->semantic_types[(size_t)view->array_type - 1U];
    RSemanticType *deadline = &context->semantic_types[(size_t)view->deadline_type - 1U];
    RSemanticType *start = &context->semantic_types[(size_t)view->start_type - 1U];
    RSemanticType *task = &context->semantic_types[(size_t)view->task_type - 1U];
    RSemanticType *logical = &context->semantic_types[(size_t)view->logical_type - 1U];
    RSemanticField *failed_fields =
        &context->semantic_fields[(size_t)view->failed_payload->first_field];

    switch (mutation) {
    case R_IO_WRITE_SHARED_MUTATE_OPERATION_IDENTITY:
        view->call->standard_operation = R_STANDARD_CALL_IO_WRITE;
        return true;
    case R_IO_WRITE_SHARED_MUTATE_OPERAND_COUNT:
        view->call->operand_count = UINT32_C(4);
        return true;
    case R_IO_WRITE_SHARED_MUTATE_BORROW_MASK:
        view->call->call_borrow_mask.low = UINT64_C(0);
        return true;
    case R_IO_WRITE_SHARED_MUTATE_STREAM_BORROW_KIND:
        view->stream_borrow->kind = R_MIR_INSTRUCTION_LOAD;
        return true;
    case R_IO_WRITE_SHARED_MUTATE_STREAM_BORROW_FLAGS:
        stream_borrow->flags = R_SEMANTIC_TYPE_FLAG_NONE;
        return true;
    case R_IO_WRITE_SHARED_MUTATE_BUFFER_MOVE_KIND:
        view->buffer_move->kind = R_MIR_INSTRUCTION_LOAD;
        return true;
    case R_IO_WRITE_SHARED_MUTATE_BUFFER_STAGED_BIT:
        view->buffer_move->is_async_staged_move = false;
        return true;
    case R_IO_WRITE_SHARED_MUTATE_BUFFER_OWNER_KIND:
        buffer->kind = R_SEMANTIC_TYPE_RC;
        return true;
    case R_IO_WRITE_SHARED_MUTATE_BUFFER_OWNER_FLAGS:
        buffer->flags = R_SEMANTIC_TYPE_FLAG_SHARED;
        return true;
    case R_IO_WRITE_SHARED_MUTATE_BUFFER_OWNER_SECOND:
        buffer->second = view->u8_type;
        return true;
    case R_IO_WRITE_SHARED_MUTATE_BUFFER_ARRAY_KIND:
        array->kind = R_SEMANTIC_TYPE_LIST;
        return true;
    case R_IO_WRITE_SHARED_MUTATE_BUFFER_ELEMENT:
        array->base = view->u16_type;
        return true;
    case R_IO_WRITE_SHARED_MUTATE_OFFSET_TYPE:
        view->offset->type = view->u8_type;
        return true;
    case R_IO_WRITE_SHARED_MUTATE_LENGTH_TYPE:
        view->length->type = view->u8_type;
        return true;
    case R_IO_WRITE_SHARED_MUTATE_DEADLINE_OPTION:
        view->deadline->type = view->buffer_type;
        return true;
    case R_IO_WRITE_SHARED_MUTATE_DEADLINE_INSTANT:
        deadline->base = view->buffer_type;
        return true;
    case R_IO_WRITE_SHARED_MUTATE_START_KIND:
        start->kind = R_SEMANTIC_TYPE_ARRAY;
        return true;
    case R_IO_WRITE_SHARED_MUTATE_START_FLAGS:
        start->flags = R_SEMANTIC_TYPE_FLAG_SHARED;
        return true;
    case R_IO_WRITE_SHARED_MUTATE_START_BASE:
        start->base = view->logical_type;
        return true;
    case R_IO_WRITE_SHARED_MUTATE_START_SECOND:
        start->second = view->buffer_type;
        return true;
    case R_IO_WRITE_SHARED_MUTATE_TASK_KIND:
        task->kind = R_SEMANTIC_TYPE_ARRAY;
        return true;
    case R_IO_WRITE_SHARED_MUTATE_TASK_FLAGS:
        task->flags = R_SEMANTIC_TYPE_FLAG_SHARED;
        return true;
    case R_IO_WRITE_SHARED_MUTATE_TASK_BASE:
        task->base = view->buffer_type;
        return true;
    case R_IO_WRITE_SHARED_MUTATE_TASK_SECOND:
        task->second = view->buffer_type;
        return true;
    case R_IO_WRITE_SHARED_MUTATE_LOGICAL_KIND:
        logical->kind = R_SEMANTIC_TYPE_ARRAY;
        return true;
    case R_IO_WRITE_SHARED_MUTATE_LOGICAL_FLAGS:
        logical->flags = R_SEMANTIC_TYPE_FLAG_SHARED;
        return true;
    case R_IO_WRITE_SHARED_MUTATE_LOGICAL_BASE:
        logical->base = view->u8_type;
        return true;
    case R_IO_WRITE_SHARED_MUTATE_LOGICAL_SECOND:
        logical->second = view->u8_type;
        return true;
    case R_IO_WRITE_SHARED_MUTATE_LOGICAL_CONST_WRAPPER: {
        const RTypeId const_type = r_io_write_shared_append_const_type(context, view->logical_type);

        if (const_type == R_TYPE_ID_INVALID) {
            return false;
        }
        task->base = const_type;
        return true;
    }
    case R_IO_WRITE_SHARED_MUTATE_WRITTEN_TAG:
        view->written_variant_payload->integer_value = UINT64_C(1);
        return true;
    case R_IO_WRITE_SHARED_MUTATE_WRITTEN_PAYLOAD_TYPE:
        view->written_variant_payload->type = view->failed_payload->type;
        return true;
    case R_IO_WRITE_SHARED_MUTATE_FAILED_TAG:
        view->failed_variant_payload->integer_value = UINT64_C(2);
        return true;
    case R_IO_WRITE_SHARED_MUTATE_FAILED_FIELD_ORDER: {
        const RSemanticField first = failed_fields[0];

        failed_fields[0] = failed_fields[1];
        failed_fields[1] = first;
        return true;
    }
    case R_IO_WRITE_SHARED_MUTATE_FAILED_ERROR_TYPE:
        failed_fields[0].type = failed_fields[1].type;
        return true;
    case R_IO_WRITE_SHARED_MUTATE_FAILED_ERROR_NAME:
        failed_fields[0].name_intern_id = failed_fields[1].name_intern_id;
        return true;
    case R_IO_WRITE_SHARED_MUTATE_FAILED_WRITTEN_TYPE:
        failed_fields[1].type = failed_fields[2].type;
        return true;
    case R_IO_WRITE_SHARED_MUTATE_FAILED_WRITTEN_NAME:
        failed_fields[1].name_intern_id = failed_fields[2].name_intern_id;
        return true;
    case R_IO_WRITE_SHARED_MUTATE_FAILED_BUFFER_TYPE:
        failed_fields[2].type = failed_fields[1].type;
        return true;
    case R_IO_WRITE_SHARED_MUTATE_FAILED_BUFFER_NAME:
        failed_fields[2].name_intern_id = failed_fields[1].name_intern_id;
        return true;
    case R_IO_WRITE_SHARED_MUTATE_SWITCH_MISSING:
        view->failed_variant_payload->kind = R_MIR_INSTRUCTION_INVALID;
        return true;
    case R_IO_WRITE_SHARED_MUTATE_SWITCH_DUPLICATE:
        view->failed_variant_payload->integer_value = UINT64_C(0);
        return true;
    case R_IO_WRITE_SHARED_MUTATION_COUNT:
    default:
        return false;
    }
}

static const char *r_io_write_shared_mutation_name(RIoWriteSharedMutation mutation) {
    static const char *const names[] = {
        "operation_identity",
        "operand_count",
        "borrow_mask",
        "stream_borrow_kind",
        "stream_borrow_flags",
        "buffer_move_kind",
        "buffer_staged_bit",
        "buffer_owner_kind",
        "buffer_owner_flags",
        "buffer_owner_second",
        "buffer_array_kind",
        "buffer_element",
        "offset_type",
        "length_type",
        "deadline_option",
        "deadline_instant",
        "start_kind",
        "start_flags",
        "start_base",
        "start_second",
        "task_kind",
        "task_flags",
        "task_base",
        "task_second",
        "logical_kind",
        "logical_flags",
        "logical_base",
        "logical_second",
        "logical_const_wrapper",
        "written_tag",
        "written_payload_type",
        "failed_tag",
        "failed_field_order",
        "failed_error_type",
        "failed_error_name",
        "failed_written_type",
        "failed_written_name",
        "failed_buffer_type",
        "failed_buffer_name",
        "switch_missing",
        "switch_duplicate",
    };

    if ((size_t)mutation >= (sizeof(names) / sizeof(names[0]))) {
        return "invalid";
    }
    return names[(size_t)mutation];
}

static void r_io_write_shared_test_positive(void) {
    RFrontendContext *context = r_io_write_shared_build_context();
    RIoWriteSharedView view;
    RIoWriteSharedBuffer first = {0};
    RIoWriteSharedBuffer second = {0};

    R_IO_WRITE_SHARED_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_IO_WRITE_SHARED_CHECK(r_io_write_shared_find_view(context, &view));
    R_IO_WRITE_SHARED_CHECK(r_io_write_shared_view_is_valid(context, &view));
    R_IO_WRITE_SHARED_CHECK(r_frontend_emit_c17(context, r_io_write_shared_write, &first) ==
                            R_FRONTEND_OK);
    R_IO_WRITE_SHARED_CHECK(first.call_count == 1U);
    R_IO_WRITE_SHARED_CHECK(first.length != 0U);
    R_IO_WRITE_SHARED_CHECK(r_frontend_emit_c17(context, r_io_write_shared_write, &second) ==
                            R_FRONTEND_OK);
    R_IO_WRITE_SHARED_CHECK(second.call_count == 1U);
    R_IO_WRITE_SHARED_CHECK(
        (first.length == second.length) &&
        ((first.length == 0U) || (memcmp(first.bytes, second.bytes, first.length) == 0)));
    r_io_write_shared_dispose_buffer(&second);
    r_io_write_shared_dispose_buffer(&first);
    r_frontend_destroy(context);
}

static void r_io_write_shared_test_mutations(void) {
    size_t mutation;

    for (mutation = 0U; mutation < (size_t)R_IO_WRITE_SHARED_MUTATION_COUNT; ++mutation) {
        RFrontendContext *context = r_io_write_shared_build_context();
        RIoWriteSharedView view;
        RIoWriteSharedBuffer output = {0};
        RFrontendStatus status;

        R_IO_WRITE_SHARED_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        if (!r_io_write_shared_find_view(context, &view)) {
            R_IO_WRITE_SHARED_CHECK(false);
            r_frontend_destroy(context);
            continue;
        }
        R_IO_WRITE_SHARED_CHECK(
            r_io_write_shared_apply_mutation(context, &view, (RIoWriteSharedMutation)mutation));
        status = r_frontend_emit_c17(context, r_io_write_shared_write, &output);
        if (status != R_FRONTEND_NOT_LOWERABLE) {
            (void)fprintf(stderr,
                          "mutation %zu (%s) unexpectedly returned status %d\n",
                          mutation,
                          r_io_write_shared_mutation_name((RIoWriteSharedMutation)mutation),
                          (int)status);
        }
        R_IO_WRITE_SHARED_CHECK(status == R_FRONTEND_NOT_LOWERABLE);
        R_IO_WRITE_SHARED_CHECK(output.call_count == 0U);
        R_IO_WRITE_SHARED_CHECK(output.length == 0U);
        r_io_write_shared_dispose_buffer(&output);
        r_frontend_destroy(context);
    }
}

static void r_io_write_shared_test_diagnostic(const RIoWriteSharedDiagnosticCase *test_case) {
    RFrontendContext *context = r_frontend_create(NULL);
    const RDiagnostic *diagnostic;

    R_IO_WRITE_SHARED_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_IO_WRITE_SHARED_CHECK(r_io_write_shared_add_file(context, test_case->path));
    R_IO_WRITE_SHARED_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
    R_IO_WRITE_SHARED_CHECK(r_frontend_diagnostic_count(context) == 1U);
    diagnostic = r_frontend_diagnostic(context, 0U);
    if ((diagnostic == NULL) || (strcmp(diagnostic->code, test_case->code) != 0) ||
        (strcmp(diagnostic->rule_id, test_case->rule_id) != 0) ||
        (diagnostic->phase != R_DIAGNOSTIC_PHASE_SEMANTIC)) {
        (void)fprintf(stderr,
                      "%s: expected %s/%s semantic diagnostic, observed %s/%s phase %d\n",
                      test_case->path,
                      test_case->code,
                      test_case->rule_id,
                      diagnostic == NULL ? "<none>" : diagnostic->code,
                      diagnostic == NULL ? "<none>" : diagnostic->rule_id,
                      diagnostic == NULL ? -1 : (int)diagnostic->phase);
        failures += 1;
    }
    r_frontend_destroy(context);
}

int main(void) {
    static const RIoWriteSharedDiagnosticCase cases[] = {
        {R_IO_WRITE_SHARED_ARITY_PATH, "R-DIAG-TYPE-001", "R-SLIB-IO-0005"},
        {R_IO_WRITE_SHARED_STREAM_TYPE_PATH, "R-DIAG-TYPE-001", "R-SLIB-IO-0005"},
        {R_IO_WRITE_SHARED_BUFFER_TYPE_PATH, "R-DIAG-TYPE-001", "R-SLIB-IO-0005"},
        {R_IO_WRITE_SHARED_OFFSET_TYPE_PATH, "R-DIAG-TYPE-001", "R-SLIB-IO-0005"},
        {R_IO_WRITE_SHARED_LENGTH_TYPE_PATH, "R-DIAG-TYPE-001", "R-SLIB-IO-0005"},
        {R_IO_WRITE_SHARED_DEADLINE_TYPE_PATH, "R-DIAG-TYPE-001", "R-SLIB-IO-0005"},
        {R_IO_WRITE_SHARED_CONTEXT_TYPE_PATH, "R-DIAG-TYPE-001", "R-SLIB-IO-0005"},
        {R_IO_WRITE_SHARED_MISSING_MOVE_PATH, "R-DIAG-MOVE-001", "R-OWN-0003"},
        {R_IO_WRITE_SHARED_NESTED_MOVE_PATH, "R-DIAG-MOVE-003", "R-FUNC-0010"},
        {R_IO_WRITE_SHARED_SUCCESS_REUSE_PATH, "R-DIAG-MOVE-002", "R-OWN-0004"},
        {R_IO_WRITE_SHARED_MISSING_VARIANT_PATH, "R-DIAG-SWITCH-001", "R-STMT-0010"},
        {R_IO_WRITE_SHARED_DUPLICATE_VARIANT_PATH, "R-DIAG-SWITCH-001", "R-STMT-0010"},
        {R_IO_WRITE_SHARED_WRONG_VARIANT_PATH, "R-DIAG-TYPE-001", "R-STMT-0010"},
    };
    size_t index;

    r_io_write_shared_test_positive();
    for (index = 0U; index < (sizeof(cases) / sizeof(cases[0])); ++index) {
        r_io_write_shared_test_diagnostic(&cases[index]);
    }
    r_io_write_shared_test_mutations();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

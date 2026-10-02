#include "frontend_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef R_IO_WRITE_RESULT_SOURCE_PATH
#error R_IO_WRITE_RESULT_SOURCE_PATH is required
#endif
#ifndef R_IO_WRITE_RESULT_MISSING_VARIANT_PATH
#error R_IO_WRITE_RESULT_MISSING_VARIANT_PATH is required
#endif
#ifndef R_IO_WRITE_RESULT_DUPLICATE_VARIANT_PATH
#error R_IO_WRITE_RESULT_DUPLICATE_VARIANT_PATH is required
#endif
#ifndef R_IO_WRITE_RESULT_WRONG_VARIANT_PATH
#error R_IO_WRITE_RESULT_WRONG_VARIANT_PATH is required
#endif

typedef struct RIoWriteResultBuffer {
    char *bytes;
    size_t length;
    size_t capacity;
    size_t call_count;
} RIoWriteResultBuffer;

typedef struct RIoWriteResultView {
    RTypeId logical_type;
    RTypeId task_type;
    RTypeId u8_type;
    RSemanticAggregate *written_payload;
    RSemanticAggregate *failed_payload;
    RMirInstruction *written_variant_payload;
    RMirInstruction *failed_variant_payload;
} RIoWriteResultView;

typedef enum RIoWriteResultMutation {
    R_IO_WRITE_RESULT_MUTATE_LOGICAL_FLAGS = 0,
    R_IO_WRITE_RESULT_MUTATE_LOGICAL_BASE,
    R_IO_WRITE_RESULT_MUTATE_LOGICAL_SECOND,
    R_IO_WRITE_RESULT_MUTATE_LOGICAL_CONST_WRAPPER,
    R_IO_WRITE_RESULT_MUTATE_VARIANT_TAG_TWO,
    R_IO_WRITE_RESULT_MUTATE_WRITTEN_TAG_ONE,
    R_IO_WRITE_RESULT_MUTATE_WRITTEN_FIELD_ORDER,
    R_IO_WRITE_RESULT_MUTATE_WRITTEN_BUFFER_TYPE,
    R_IO_WRITE_RESULT_MUTATE_WRITTEN_BUFFER_NAME,
    R_IO_WRITE_RESULT_MUTATE_WRITTEN_COUNT_TYPE,
    R_IO_WRITE_RESULT_MUTATE_WRITTEN_COUNT_NAME,
    R_IO_WRITE_RESULT_MUTATE_FAILED_FIELD_ORDER,
    R_IO_WRITE_RESULT_MUTATE_FAILED_ERROR_TYPE,
    R_IO_WRITE_RESULT_MUTATE_FAILED_ERROR_NAME,
    R_IO_WRITE_RESULT_MUTATE_FAILED_WRITTEN_TYPE,
    R_IO_WRITE_RESULT_MUTATE_FAILED_WRITTEN_NAME,
    R_IO_WRITE_RESULT_MUTATE_FAILED_BUFFER_TYPE,
    R_IO_WRITE_RESULT_MUTATE_FAILED_BUFFER_NAME,
    R_IO_WRITE_RESULT_MUTATE_SWITCH_MISSING,
    R_IO_WRITE_RESULT_MUTATE_SWITCH_DUPLICATE,
    R_IO_WRITE_RESULT_MUTATION_COUNT
} RIoWriteResultMutation;

static int failures;

#define R_IO_WRITE_RESULT_CHECK(condition)                                                         \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            (void)fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);    \
            failures += 1;                                                                         \
        }                                                                                          \
    } while (false)

static bool r_io_write_result_read_file(const char *path, uint8_t **bytes, size_t *length) {
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

static bool r_io_write_result_add_file(RFrontendContext *context, const char *path) {
    uint8_t *bytes = NULL;
    size_t length = 0U;
    RSourceId source_id = R_SOURCE_ID_INVALID;
    bool added;

    if (!r_io_write_result_read_file(path, &bytes, &length)) {
        return false;
    }
    added = r_frontend_add_source(context, path, bytes, length, &source_id) == R_FRONTEND_OK;
    free(bytes);
    return added && (source_id != R_SOURCE_ID_INVALID);
}

static bool r_io_write_result_write(void *user_data, const char *bytes, size_t length) {
    RIoWriteResultBuffer *buffer = user_data;
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

static void r_io_write_result_dispose_buffer(RIoWriteResultBuffer *buffer) {
    free(buffer->bytes);
    (void)memset(buffer, 0, sizeof(*buffer));
}

static bool r_io_write_result_intern_equals(const RFrontendContext *context,
                                            uint32_t intern_id,
                                            const char *name) {
    const RInternEntry *entry;
    size_t name_length;

    if ((intern_id == 0U) || ((size_t)intern_id > context->intern_count)) {
        return false;
    }
    entry = &context->intern_entries[(size_t)intern_id - 1U];
    name_length = strlen(name);
    return (entry->length == name_length) && (memcmp(entry->bytes, name, name_length) == 0);
}

static bool r_io_write_result_standard_type_equals(const RFrontendContext *context,
                                                   RTypeId type_id,
                                                   const char *name) {
    const RSemanticType *type = r_semantic_type(context, type_id);

    return (type != NULL) && (type->kind == R_SEMANTIC_TYPE_STANDARD) &&
           (type->length != UINT64_C(0)) && (type->length <= (uint64_t)UINT32_MAX) &&
           r_io_write_result_intern_equals(context, (uint32_t)type->length, name);
}

static RSemanticAggregate *r_io_write_result_find_aggregate(RFrontendContext *context,
                                                            const char *name) {
    size_t index;

    for (index = 0U; index < context->semantic_aggregate_count; ++index) {
        RSemanticAggregate *aggregate = &context->semantic_aggregates[index];

        if (r_io_write_result_intern_equals(context, aggregate->name_intern_id, name)) {
            return aggregate;
        }
    }
    return NULL;
}

static RFrontendContext *r_io_write_result_build_context(void) {
    RFrontendContext *context = r_frontend_create(NULL);

    if ((context == NULL) || !r_io_write_result_add_file(context, R_IO_WRITE_RESULT_SOURCE_PATH) ||
        (r_frontend_analyze(context) != R_FRONTEND_OK) ||
        (r_frontend_diagnostic_count(context) != 0U) ||
        (r_frontend_lower_mir(context) != R_FRONTEND_OK) ||
        (r_frontend_diagnostic_count(context) != 0U)) {
        r_frontend_destroy(context);
        return NULL;
    }
    return context;
}

static bool r_io_write_result_find_view(RFrontendContext *context, RIoWriteResultView *view) {
    size_t index;

    (void)memset(view, 0, sizeof(*view));
    view->logical_type = R_TYPE_ID_INVALID;
    view->task_type = R_TYPE_ID_INVALID;
    view->u8_type = R_TYPE_ID_INVALID;
    for (index = 0U; index < context->semantic_type_count; ++index) {
        const RTypeId type_id = (RTypeId)index + UINT32_C(1);
        const RSemanticType *type = &context->semantic_types[index];

        if (r_io_write_result_standard_type_equals(context, type_id, "std.io::write_result")) {
            if (view->logical_type != R_TYPE_ID_INVALID) {
                return false;
            }
            view->logical_type = type_id;
        } else if (type->kind == R_SEMANTIC_TYPE_U8) {
            view->u8_type = type_id;
        }
    }
    if ((view->logical_type == R_TYPE_ID_INVALID) || (view->u8_type == R_TYPE_ID_INVALID)) {
        return false;
    }
    for (index = 0U; index < context->mir_instruction_count; ++index) {
        RMirInstruction *instruction = &context->mir_instructions[index];

        if ((instruction->kind == R_MIR_INSTRUCTION_STANDARD_CALL) &&
            (instruction->standard_operation == R_STANDARD_CALL_IO_WRITE)) {
            const RSemanticType *task = r_semantic_type(context, instruction->auxiliary_type);

            if ((view->task_type != R_TYPE_ID_INVALID) || (task == NULL) ||
                (task->kind != R_SEMANTIC_TYPE_TASK) || (task->base != view->logical_type)) {
                return false;
            }
            view->task_type = instruction->auxiliary_type;
        }
    }
    if (view->task_type == R_TYPE_ID_INVALID) {
        return false;
    }
    view->written_payload =
        r_io_write_result_find_aggregate(context, "std.io::write_result::written");
    view->failed_payload =
        r_io_write_result_find_aggregate(context, "std.io::write_result::failed");
    if ((view->written_payload == NULL) || (view->failed_payload == NULL)) {
        return false;
    }
    for (index = 0U; index < context->mir_instruction_count; ++index) {
        RMirInstruction *instruction = &context->mir_instructions[index];

        if ((instruction->kind == R_MIR_INSTRUCTION_VARIANT_PAYLOAD) &&
            (instruction->type == view->written_payload->type) &&
            (instruction->integer_value == UINT64_C(0))) {
            if (view->written_variant_payload != NULL) {
                return false;
            }
            view->written_variant_payload = instruction;
        } else if ((instruction->kind == R_MIR_INSTRUCTION_VARIANT_PAYLOAD) &&
                   (instruction->type == view->failed_payload->type) &&
                   (instruction->integer_value == UINT64_C(1))) {
            if (view->failed_variant_payload != NULL) {
                return false;
            }
            view->failed_variant_payload = instruction;
        }
    }
    return (view->written_variant_payload != NULL) && (view->failed_variant_payload != NULL);
}

static bool r_io_write_result_fields_are_valid(const RFrontendContext *context,
                                               const RIoWriteResultView *view) {
    const RSemanticField *written_fields;
    const RSemanticField *failed_fields;
    const RSemanticType *written_buffer;
    const RSemanticType *written_count;
    const RSemanticType *failed_written;
    const RSemanticType *failed_buffer;

    if ((context == NULL) || (view == NULL) || (view->written_payload == NULL) ||
        (view->failed_payload == NULL) || (context->semantic_fields == NULL) ||
        (view->written_payload->kind != R_SEMANTIC_AGGREGATE_STRUCT) ||
        (view->written_payload->field_count != UINT32_C(2)) ||
        (view->failed_payload->kind != R_SEMANTIC_AGGREGATE_STRUCT) ||
        (view->failed_payload->field_count != UINT32_C(3)) ||
        ((size_t)view->written_payload->first_field + UINT32_C(2) >
         context->semantic_field_count) ||
        ((size_t)view->failed_payload->first_field + UINT32_C(3) > context->semantic_field_count)) {
        return false;
    }
    written_fields = &context->semantic_fields[view->written_payload->first_field];
    failed_fields = &context->semantic_fields[view->failed_payload->first_field];
    written_buffer = r_semantic_type(context, written_fields[0].type);
    written_count = r_semantic_type(context, written_fields[1].type);
    failed_written = r_semantic_type(context, failed_fields[1].type);
    failed_buffer = r_semantic_type(context, failed_fields[2].type);
    return r_io_write_result_intern_equals(context, written_fields[0].name_intern_id, "buffer") &&
           (written_buffer != NULL) && (written_buffer->kind == R_SEMANTIC_TYPE_ARRAY) &&
           (written_buffer->base == view->u8_type) &&
           r_io_write_result_intern_equals(context, written_fields[1].name_intern_id, "count") &&
           (written_count != NULL) && (written_count->kind == R_SEMANTIC_TYPE_USIZE) &&
           r_io_write_result_intern_equals(context, failed_fields[0].name_intern_id, "error") &&
           r_io_write_result_standard_type_equals(
               context, failed_fields[0].type, "std.io::io_error") &&
           r_io_write_result_intern_equals(context, failed_fields[1].name_intern_id, "written") &&
           (failed_written != NULL) && (failed_written->kind == R_SEMANTIC_TYPE_USIZE) &&
           r_io_write_result_intern_equals(context, failed_fields[2].name_intern_id, "buffer") &&
           (failed_buffer != NULL) && (failed_buffer->kind == R_SEMANTIC_TYPE_ARRAY) &&
           (failed_buffer->base == view->u8_type);
}

static void r_io_write_result_test_positive(void) {
    RFrontendContext *context = r_io_write_result_build_context();
    RIoWriteResultView view;
    RIoWriteResultBuffer first = {0};
    RIoWriteResultBuffer second = {0};

    R_IO_WRITE_RESULT_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_IO_WRITE_RESULT_CHECK(r_io_write_result_find_view(context, &view));
    R_IO_WRITE_RESULT_CHECK(r_io_write_result_fields_are_valid(context, &view));
    R_IO_WRITE_RESULT_CHECK(r_frontend_emit_c17(context, r_io_write_result_write, &first) ==
                            R_FRONTEND_OK);
    R_IO_WRITE_RESULT_CHECK(first.call_count == 1U);
    R_IO_WRITE_RESULT_CHECK(first.length != 0U);
    R_IO_WRITE_RESULT_CHECK(r_frontend_emit_c17(context, r_io_write_result_write, &second) ==
                            R_FRONTEND_OK);
    R_IO_WRITE_RESULT_CHECK(second.call_count == 1U);
    R_IO_WRITE_RESULT_CHECK(
        (first.length == second.length) &&
        ((first.length == 0U) || (memcmp(first.bytes, second.bytes, first.length) == 0)));
    r_io_write_result_dispose_buffer(&second);
    r_io_write_result_dispose_buffer(&first);
    r_frontend_destroy(context);
}

static void r_io_write_result_test_diagnostic(const char *path,
                                              const char *expected_code,
                                              const char *expected_rule) {
    RFrontendContext *context = r_frontend_create(NULL);
    const RDiagnostic *diagnostic;

    R_IO_WRITE_RESULT_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_IO_WRITE_RESULT_CHECK(r_io_write_result_add_file(context, path));
    R_IO_WRITE_RESULT_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
    R_IO_WRITE_RESULT_CHECK(r_frontend_diagnostic_count(context) == 1U);
    diagnostic = r_frontend_diagnostic(context, 0U);
    R_IO_WRITE_RESULT_CHECK((diagnostic != NULL) &&
                            (strcmp(diagnostic->code, expected_code) == 0) &&
                            (strcmp(diagnostic->rule_id, expected_rule) == 0) &&
                            (diagnostic->phase == R_DIAGNOSTIC_PHASE_SEMANTIC));
    r_frontend_destroy(context);
}

static RTypeId r_io_write_result_append_const_type(RFrontendContext *context, RTypeId base) {
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

static bool r_io_write_result_apply_mutation(RFrontendContext *context,
                                             RIoWriteResultView *view,
                                             RIoWriteResultMutation mutation) {
    RSemanticType *logical = &context->semantic_types[(size_t)view->logical_type - 1U];
    RSemanticField *written_fields =
        &context->semantic_fields[(size_t)view->written_payload->first_field];
    RSemanticField *failed_fields =
        &context->semantic_fields[(size_t)view->failed_payload->first_field];

    switch (mutation) {
    case R_IO_WRITE_RESULT_MUTATE_LOGICAL_FLAGS:
        logical->flags = R_SEMANTIC_TYPE_FLAG_SHARED;
        return true;
    case R_IO_WRITE_RESULT_MUTATE_LOGICAL_BASE:
        logical->base = view->u8_type;
        return true;
    case R_IO_WRITE_RESULT_MUTATE_LOGICAL_SECOND:
        logical->second = view->u8_type;
        return true;
    case R_IO_WRITE_RESULT_MUTATE_LOGICAL_CONST_WRAPPER: {
        const RTypeId const_type = r_io_write_result_append_const_type(context, view->logical_type);
        RSemanticType *task;

        if (const_type == R_TYPE_ID_INVALID) {
            return false;
        }
        task = &context->semantic_types[(size_t)view->task_type - 1U];
        task->base = const_type;
        return true;
    }
    case R_IO_WRITE_RESULT_MUTATE_VARIANT_TAG_TWO:
        view->failed_variant_payload->integer_value = UINT64_C(2);
        return true;
    case R_IO_WRITE_RESULT_MUTATE_WRITTEN_TAG_ONE:
        view->written_variant_payload->integer_value = UINT64_C(1);
        return true;
    case R_IO_WRITE_RESULT_MUTATE_WRITTEN_FIELD_ORDER: {
        const RSemanticField first = written_fields[0];

        written_fields[0] = written_fields[1];
        written_fields[1] = first;
        return true;
    }
    case R_IO_WRITE_RESULT_MUTATE_WRITTEN_BUFFER_TYPE:
        written_fields[0].type = written_fields[1].type;
        return true;
    case R_IO_WRITE_RESULT_MUTATE_WRITTEN_BUFFER_NAME:
        written_fields[0].name_intern_id = written_fields[1].name_intern_id;
        return true;
    case R_IO_WRITE_RESULT_MUTATE_WRITTEN_COUNT_TYPE:
        written_fields[1].type = written_fields[0].type;
        return true;
    case R_IO_WRITE_RESULT_MUTATE_WRITTEN_COUNT_NAME:
        written_fields[1].name_intern_id = written_fields[0].name_intern_id;
        return true;
    case R_IO_WRITE_RESULT_MUTATE_FAILED_FIELD_ORDER: {
        const RSemanticField first = failed_fields[0];

        failed_fields[0] = failed_fields[1];
        failed_fields[1] = first;
        return true;
    }
    case R_IO_WRITE_RESULT_MUTATE_FAILED_ERROR_TYPE:
        failed_fields[0].type = failed_fields[1].type;
        return true;
    case R_IO_WRITE_RESULT_MUTATE_FAILED_ERROR_NAME:
        failed_fields[0].name_intern_id = failed_fields[1].name_intern_id;
        return true;
    case R_IO_WRITE_RESULT_MUTATE_FAILED_WRITTEN_TYPE:
        failed_fields[1].type = failed_fields[2].type;
        return true;
    case R_IO_WRITE_RESULT_MUTATE_FAILED_WRITTEN_NAME:
        failed_fields[1].name_intern_id = failed_fields[2].name_intern_id;
        return true;
    case R_IO_WRITE_RESULT_MUTATE_FAILED_BUFFER_TYPE:
        failed_fields[2].type = failed_fields[1].type;
        return true;
    case R_IO_WRITE_RESULT_MUTATE_FAILED_BUFFER_NAME:
        failed_fields[2].name_intern_id = failed_fields[1].name_intern_id;
        return true;
    case R_IO_WRITE_RESULT_MUTATE_SWITCH_MISSING:
        view->failed_variant_payload->kind = R_MIR_INSTRUCTION_INVALID;
        return true;
    case R_IO_WRITE_RESULT_MUTATE_SWITCH_DUPLICATE:
        view->failed_variant_payload->integer_value = UINT64_C(0);
        return true;
    case R_IO_WRITE_RESULT_MUTATION_COUNT:
    default:
        return false;
    }
}

static const char *r_io_write_result_mutation_name(RIoWriteResultMutation mutation) {
    static const char *const names[] = {
        "logical_flags",       "logical_base",       "logical_second",      "logical_const_wrapper",
        "variant_tag_two",     "written_tag_one",    "written_field_order", "written_buffer_type",
        "written_buffer_name", "written_count_type", "written_count_name",  "failed_field_order",
        "failed_error_type",   "failed_error_name",  "failed_written_type", "failed_written_name",
        "failed_buffer_type",  "failed_buffer_name", "switch_missing",      "switch_duplicate",
    };

    if ((size_t)mutation >= (sizeof(names) / sizeof(names[0]))) {
        return "invalid";
    }
    return names[(size_t)mutation];
}

static void r_io_write_result_test_codegen_mutations(void) {
    size_t mutation;

    for (mutation = 0U; mutation < (size_t)R_IO_WRITE_RESULT_MUTATION_COUNT; ++mutation) {
        RFrontendContext *context = r_io_write_result_build_context();
        RIoWriteResultView view;
        RIoWriteResultBuffer output = {0};
        RFrontendStatus status;

        R_IO_WRITE_RESULT_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        R_IO_WRITE_RESULT_CHECK(r_io_write_result_find_view(context, &view));
        R_IO_WRITE_RESULT_CHECK(
            r_io_write_result_apply_mutation(context, &view, (RIoWriteResultMutation)mutation));
        status = r_frontend_emit_c17(context, r_io_write_result_write, &output);
        if (status != R_FRONTEND_NOT_LOWERABLE) {
            (void)fprintf(stderr,
                          "mutation %zu (%s) unexpectedly returned status %d\n",
                          mutation,
                          r_io_write_result_mutation_name((RIoWriteResultMutation)mutation),
                          (int)status);
        }
        R_IO_WRITE_RESULT_CHECK(status == R_FRONTEND_NOT_LOWERABLE);
        R_IO_WRITE_RESULT_CHECK(output.call_count == 0U);
        R_IO_WRITE_RESULT_CHECK(output.length == 0U);
        r_io_write_result_dispose_buffer(&output);
        r_frontend_destroy(context);
    }
}

int main(void) {
    r_io_write_result_test_positive();
    r_io_write_result_test_diagnostic(
        R_IO_WRITE_RESULT_MISSING_VARIANT_PATH, "R-DIAG-SWITCH-001", "R-STMT-0010");
    r_io_write_result_test_diagnostic(
        R_IO_WRITE_RESULT_DUPLICATE_VARIANT_PATH, "R-DIAG-SWITCH-001", "R-STMT-0010");
    r_io_write_result_test_diagnostic(
        R_IO_WRITE_RESULT_WRONG_VARIANT_PATH, "R-DIAG-TYPE-001", "R-STMT-0010");
    r_io_write_result_test_codegen_mutations();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

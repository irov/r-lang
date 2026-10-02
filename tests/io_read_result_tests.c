#include "frontend_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef R_IO_READ_RESULT_SOURCE_PATH
#error R_IO_READ_RESULT_SOURCE_PATH is required
#endif
#ifndef R_IO_READ_RESULT_MISSING_VARIANT_PATH
#error R_IO_READ_RESULT_MISSING_VARIANT_PATH is required
#endif
#ifndef R_IO_READ_RESULT_DUPLICATE_VARIANT_PATH
#error R_IO_READ_RESULT_DUPLICATE_VARIANT_PATH is required
#endif
#ifndef R_IO_READ_RESULT_WRONG_VARIANT_PATH
#error R_IO_READ_RESULT_WRONG_VARIANT_PATH is required
#endif

typedef struct RIoReadResultBuffer {
    char *bytes;
    size_t length;
    size_t capacity;
    size_t call_count;
} RIoReadResultBuffer;

typedef struct RIoReadResultView {
    RTypeId logical_type;
    RTypeId task_type;
    RTypeId u8_type;
    RSemanticAggregate *read_payload;
    RSemanticAggregate *failed_payload;
    RHirNode *outcome_switch;
    RHirNode *outcome_cases[3];
    RMirInstruction *failed_variant_payload;
} RIoReadResultView;

typedef enum RIoReadResultMutation {
    R_IO_READ_RESULT_MUTATE_LOGICAL_FLAGS = 0,
    R_IO_READ_RESULT_MUTATE_LOGICAL_BASE,
    R_IO_READ_RESULT_MUTATE_LOGICAL_SECOND,
    R_IO_READ_RESULT_MUTATE_LOGICAL_CONST_WRAPPER,
    R_IO_READ_RESULT_MUTATE_VARIANT_TAG_THREE,
    R_IO_READ_RESULT_MUTATE_READ_FIELD_ORDER,
    R_IO_READ_RESULT_MUTATE_READ_FIELD_TYPE,
    R_IO_READ_RESULT_MUTATE_READ_FIELD_NAME,
    R_IO_READ_RESULT_MUTATE_FAILED_FIELD_ORDER,
    R_IO_READ_RESULT_MUTATE_FAILED_COUNT_TYPE,
    R_IO_READ_RESULT_MUTATE_FAILED_COUNT_NAME,
    R_IO_READ_RESULT_MUTATE_FAILED_BUFFER_TYPE,
    R_IO_READ_RESULT_MUTATE_FAILED_BUFFER_NAME,
    R_IO_READ_RESULT_MUTATE_SWITCH_MISSING,
    R_IO_READ_RESULT_MUTATE_SWITCH_DUPLICATE,
    R_IO_READ_RESULT_MUTATION_COUNT
} RIoReadResultMutation;

static int failures;

#define R_IO_READ_RESULT_CHECK(condition)                                                          \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            (void)fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);    \
            failures += 1;                                                                         \
        }                                                                                          \
    } while (false)

static bool r_io_read_result_read_file(const char *path, uint8_t **bytes, size_t *length) {
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

static bool r_io_read_result_add_file(RFrontendContext *context, const char *path) {
    uint8_t *bytes = NULL;
    size_t length = 0U;
    RSourceId source_id = R_SOURCE_ID_INVALID;
    bool added;

    if (!r_io_read_result_read_file(path, &bytes, &length)) {
        return false;
    }
    added = r_frontend_add_source(context, path, bytes, length, &source_id) == R_FRONTEND_OK;
    free(bytes);
    return added && (source_id != R_SOURCE_ID_INVALID);
}

static bool r_io_read_result_write(void *user_data, const char *bytes, size_t length) {
    RIoReadResultBuffer *buffer = user_data;
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

static void r_io_read_result_dispose_buffer(RIoReadResultBuffer *buffer) {
    free(buffer->bytes);
    (void)memset(buffer, 0, sizeof(*buffer));
}

static bool r_io_read_result_intern_equals(const RFrontendContext *context,
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

static bool r_io_read_result_standard_type_equals(const RFrontendContext *context,
                                                  RTypeId type_id,
                                                  const char *name) {
    const RSemanticType *type = r_semantic_type(context, type_id);

    return (type != NULL) && (type->kind == R_SEMANTIC_TYPE_STANDARD) &&
           (type->length != UINT64_C(0)) && (type->length <= (uint64_t)UINT32_MAX) &&
           r_io_read_result_intern_equals(context, (uint32_t)type->length, name);
}

static RSemanticAggregate *r_io_read_result_find_aggregate(RFrontendContext *context,
                                                           const char *name) {
    size_t index;

    for (index = 0U; index < context->semantic_aggregate_count; ++index) {
        RSemanticAggregate *aggregate = &context->semantic_aggregates[index];

        if (r_io_read_result_intern_equals(context, aggregate->name_intern_id, name)) {
            return aggregate;
        }
    }
    return NULL;
}

static RHirNode *r_io_read_result_hir_node(RFrontendContext *context, RHirNodeId node_id) {
    if ((node_id == R_HIR_NODE_ID_INVALID) || ((size_t)node_id > context->hir_node_count)) {
        return NULL;
    }
    return &context->hir_nodes[(size_t)node_id - 1U];
}

static RFrontendContext *r_io_read_result_build_context(void) {
    RFrontendContext *context = r_frontend_create(NULL);

    if ((context == NULL) || !r_io_read_result_add_file(context, R_IO_READ_RESULT_SOURCE_PATH) ||
        (r_frontend_analyze(context) != R_FRONTEND_OK) ||
        (r_frontend_diagnostic_count(context) != 0U) ||
        (r_frontend_lower_mir(context) != R_FRONTEND_OK) ||
        (r_frontend_diagnostic_count(context) != 0U)) {
        r_frontend_destroy(context);
        return NULL;
    }
    return context;
}

static bool r_io_read_result_find_view(RFrontendContext *context, RIoReadResultView *view) {
    size_t index;

    (void)memset(view, 0, sizeof(*view));
    view->logical_type = R_TYPE_ID_INVALID;
    view->task_type = R_TYPE_ID_INVALID;
    view->u8_type = R_TYPE_ID_INVALID;
    for (index = 0U; index < context->semantic_type_count; ++index) {
        const RTypeId type_id = (RTypeId)index + UINT32_C(1);
        const RSemanticType *type = &context->semantic_types[index];

        if (r_io_read_result_standard_type_equals(context, type_id, "std.io::read_result")) {
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
            (instruction->standard_operation == R_STANDARD_CALL_IO_READ)) {
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
    view->read_payload = r_io_read_result_find_aggregate(context, "std.io::read_result::read");
    view->failed_payload = r_io_read_result_find_aggregate(context, "std.io::read_result::failed");
    if ((view->read_payload == NULL) || (view->failed_payload == NULL)) {
        return false;
    }
    for (index = 0U; index < context->hir_node_count; ++index) {
        RHirNode *node = &context->hir_nodes[index];

        if ((node->kind == R_HIR_SWITCH) && (node->auxiliary_type == view->logical_type)) {
            uint32_t child_index;

            if ((view->outcome_switch != NULL) || (node->child_count != UINT32_C(4)) ||
                ((size_t)node->first_child + (size_t)node->child_count >
                 context->hir_child_count)) {
                return false;
            }
            view->outcome_switch = node;
            for (child_index = UINT32_C(0); child_index < UINT32_C(3); ++child_index) {
                RHirNode *case_node = r_io_read_result_hir_node(
                    context,
                    context->hir_children[(size_t)node->first_child + (size_t)child_index + 1U]);

                if ((case_node == NULL) || (case_node->kind != R_HIR_CASE) ||
                    (case_node->case_pattern != R_HIR_CASE_PATTERN_VARIANT) ||
                    (case_node->integer_value != (uint64_t)child_index)) {
                    return false;
                }
                view->outcome_cases[child_index] = case_node;
            }
        }
    }
    if (view->outcome_switch == NULL) {
        return false;
    }
    for (index = 0U; index < context->mir_instruction_count; ++index) {
        RMirInstruction *instruction = &context->mir_instructions[index];

        if ((instruction->kind == R_MIR_INSTRUCTION_VARIANT_PAYLOAD) &&
            (instruction->type == view->failed_payload->type) &&
            (instruction->integer_value == UINT64_C(2))) {
            if (view->failed_variant_payload != NULL) {
                return false;
            }
            view->failed_variant_payload = instruction;
        }
    }
    return view->failed_variant_payload != NULL;
}

static bool r_io_read_result_fields_are_valid(const RFrontendContext *context,
                                              const RIoReadResultView *view) {
    const RSemanticField *read_fields;
    const RSemanticField *failed_fields;
    const RSemanticType *read_buffer;
    const RSemanticType *read_count;
    const RSemanticType *failed_count;
    const RSemanticType *failed_buffer;

    if ((context == NULL) || (view == NULL) || (view->read_payload == NULL) ||
        (view->failed_payload == NULL) || (context->semantic_fields == NULL) ||
        (view->read_payload->kind != R_SEMANTIC_AGGREGATE_STRUCT) ||
        (view->read_payload->field_count != UINT32_C(2)) ||
        (view->failed_payload->kind != R_SEMANTIC_AGGREGATE_STRUCT) ||
        (view->failed_payload->field_count != UINT32_C(3)) ||
        ((size_t)view->read_payload->first_field + UINT32_C(2) > context->semantic_field_count) ||
        ((size_t)view->failed_payload->first_field + UINT32_C(3) > context->semantic_field_count)) {
        return false;
    }
    read_fields = &context->semantic_fields[view->read_payload->first_field];
    failed_fields = &context->semantic_fields[view->failed_payload->first_field];
    read_buffer = r_semantic_type(context, read_fields[0].type);
    read_count = r_semantic_type(context, read_fields[1].type);
    failed_count = r_semantic_type(context, failed_fields[1].type);
    failed_buffer = r_semantic_type(context, failed_fields[2].type);
    return r_io_read_result_intern_equals(context, read_fields[0].name_intern_id, "buffer") &&
           (read_buffer != NULL) && (read_buffer->kind == R_SEMANTIC_TYPE_ARRAY) &&
           (read_buffer->base == view->u8_type) &&
           r_io_read_result_intern_equals(context, read_fields[1].name_intern_id, "count") &&
           (read_count != NULL) && (read_count->kind == R_SEMANTIC_TYPE_USIZE) &&
           r_io_read_result_intern_equals(context, failed_fields[0].name_intern_id, "error") &&
           r_io_read_result_standard_type_equals(
               context, failed_fields[0].type, "std.io::io_error") &&
           r_io_read_result_intern_equals(context, failed_fields[1].name_intern_id, "count") &&
           (failed_count != NULL) && (failed_count->kind == R_SEMANTIC_TYPE_USIZE) &&
           r_io_read_result_intern_equals(context, failed_fields[2].name_intern_id, "buffer") &&
           (failed_buffer != NULL) && (failed_buffer->kind == R_SEMANTIC_TYPE_ARRAY) &&
           (failed_buffer->base == view->u8_type);
}

static void r_io_read_result_test_positive(void) {
    RFrontendContext *context = r_io_read_result_build_context();
    RIoReadResultView view;
    RIoReadResultBuffer first = {0};
    RIoReadResultBuffer second = {0};

    R_IO_READ_RESULT_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_IO_READ_RESULT_CHECK(r_io_read_result_find_view(context, &view));
    R_IO_READ_RESULT_CHECK(r_io_read_result_fields_are_valid(context, &view));
    R_IO_READ_RESULT_CHECK(r_frontend_emit_c17(context, r_io_read_result_write, &first) ==
                           R_FRONTEND_OK);
    R_IO_READ_RESULT_CHECK(first.call_count == 1U);
    R_IO_READ_RESULT_CHECK(first.length != 0U);
    R_IO_READ_RESULT_CHECK(r_frontend_emit_c17(context, r_io_read_result_write, &second) ==
                           R_FRONTEND_OK);
    R_IO_READ_RESULT_CHECK(second.call_count == 1U);
    R_IO_READ_RESULT_CHECK(
        (first.length == second.length) &&
        ((first.length == 0U) || (memcmp(first.bytes, second.bytes, first.length) == 0)));
    r_io_read_result_dispose_buffer(&second);
    r_io_read_result_dispose_buffer(&first);
    r_frontend_destroy(context);
}

static void r_io_read_result_test_diagnostic(const char *path,
                                             const char *expected_code,
                                             const char *expected_rule) {
    RFrontendContext *context = r_frontend_create(NULL);
    const RDiagnostic *diagnostic;

    R_IO_READ_RESULT_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_IO_READ_RESULT_CHECK(r_io_read_result_add_file(context, path));
    R_IO_READ_RESULT_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
    R_IO_READ_RESULT_CHECK(r_frontend_diagnostic_count(context) == 1U);
    diagnostic = r_frontend_diagnostic(context, 0U);
    R_IO_READ_RESULT_CHECK((diagnostic != NULL) && (strcmp(diagnostic->code, expected_code) == 0) &&
                           (strcmp(diagnostic->rule_id, expected_rule) == 0) &&
                           (diagnostic->phase == R_DIAGNOSTIC_PHASE_SEMANTIC));
    r_frontend_destroy(context);
}

static RTypeId r_io_read_result_append_const_type(RFrontendContext *context, RTypeId base) {
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

static bool r_io_read_result_apply_mutation(RFrontendContext *context,
                                            RIoReadResultView *view,
                                            RIoReadResultMutation mutation) {
    RSemanticType *logical = &context->semantic_types[(size_t)view->logical_type - 1U];
    RSemanticField *read_fields =
        &context->semantic_fields[(size_t)view->read_payload->first_field];
    RSemanticField *failed_fields =
        &context->semantic_fields[(size_t)view->failed_payload->first_field];

    switch (mutation) {
    case R_IO_READ_RESULT_MUTATE_LOGICAL_FLAGS:
        logical->flags = R_SEMANTIC_TYPE_FLAG_SHARED;
        return true;
    case R_IO_READ_RESULT_MUTATE_LOGICAL_BASE:
        logical->base = view->u8_type;
        return true;
    case R_IO_READ_RESULT_MUTATE_LOGICAL_SECOND:
        logical->second = view->u8_type;
        return true;
    case R_IO_READ_RESULT_MUTATE_LOGICAL_CONST_WRAPPER: {
        const RTypeId const_type = r_io_read_result_append_const_type(context, view->logical_type);
        RSemanticType *task;

        if (const_type == R_TYPE_ID_INVALID) {
            return false;
        }
        task = &context->semantic_types[(size_t)view->task_type - 1U];
        task->base = const_type;
        return true;
    }
    case R_IO_READ_RESULT_MUTATE_VARIANT_TAG_THREE:
        view->failed_variant_payload->integer_value = UINT64_C(3);
        return true;
    case R_IO_READ_RESULT_MUTATE_READ_FIELD_ORDER: {
        const RSemanticField first = read_fields[0];

        read_fields[0] = read_fields[1];
        read_fields[1] = first;
        return true;
    }
    case R_IO_READ_RESULT_MUTATE_READ_FIELD_TYPE:
        read_fields[0].type = read_fields[1].type;
        return true;
    case R_IO_READ_RESULT_MUTATE_READ_FIELD_NAME:
        read_fields[0].name_intern_id = read_fields[1].name_intern_id;
        return true;
    case R_IO_READ_RESULT_MUTATE_FAILED_FIELD_ORDER: {
        const RSemanticField count = failed_fields[1];

        failed_fields[1] = failed_fields[2];
        failed_fields[2] = count;
        return true;
    }
    case R_IO_READ_RESULT_MUTATE_FAILED_COUNT_TYPE:
        failed_fields[1].type = failed_fields[2].type;
        return true;
    case R_IO_READ_RESULT_MUTATE_FAILED_COUNT_NAME:
        failed_fields[1].name_intern_id = failed_fields[2].name_intern_id;
        return true;
    case R_IO_READ_RESULT_MUTATE_FAILED_BUFFER_TYPE:
        failed_fields[2].type = failed_fields[1].type;
        return true;
    case R_IO_READ_RESULT_MUTATE_FAILED_BUFFER_NAME:
        failed_fields[2].name_intern_id = failed_fields[1].name_intern_id;
        return true;
    case R_IO_READ_RESULT_MUTATE_SWITCH_MISSING:
        view->failed_variant_payload->kind = R_MIR_INSTRUCTION_INVALID;
        return true;
    case R_IO_READ_RESULT_MUTATE_SWITCH_DUPLICATE:
        view->failed_variant_payload->integer_value = UINT64_C(1);
        return true;
    case R_IO_READ_RESULT_MUTATION_COUNT:
    default:
        return false;
    }
}

static const char *r_io_read_result_mutation_name(RIoReadResultMutation mutation) {
    static const char *const names[] = {
        "logical_flags",
        "logical_base",
        "logical_second",
        "logical_const_wrapper",
        "variant_tag_three",
        "read_field_order",
        "read_field_type",
        "read_field_name",
        "failed_field_order",
        "failed_count_type",
        "failed_count_name",
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

static void r_io_read_result_test_codegen_mutations(void) {
    size_t mutation;

    for (mutation = 0U; mutation < (size_t)R_IO_READ_RESULT_MUTATION_COUNT; ++mutation) {
        RFrontendContext *context = r_io_read_result_build_context();
        RIoReadResultView view;
        RIoReadResultBuffer output = {0};
        RFrontendStatus status;

        R_IO_READ_RESULT_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        R_IO_READ_RESULT_CHECK(r_io_read_result_find_view(context, &view));
        R_IO_READ_RESULT_CHECK(
            r_io_read_result_apply_mutation(context, &view, (RIoReadResultMutation)mutation));
        status = r_frontend_emit_c17(context, r_io_read_result_write, &output);
        if (status != R_FRONTEND_NOT_LOWERABLE) {
            (void)fprintf(stderr,
                          "mutation %zu (%s) unexpectedly returned status %d\n",
                          mutation,
                          r_io_read_result_mutation_name((RIoReadResultMutation)mutation),
                          (int)status);
        }
        R_IO_READ_RESULT_CHECK(status == R_FRONTEND_NOT_LOWERABLE);
        R_IO_READ_RESULT_CHECK(output.call_count == 0U);
        R_IO_READ_RESULT_CHECK(output.length == 0U);
        r_io_read_result_dispose_buffer(&output);
        r_frontend_destroy(context);
    }
}

int main(void) {
    r_io_read_result_test_positive();
    r_io_read_result_test_diagnostic(
        R_IO_READ_RESULT_MISSING_VARIANT_PATH, "R-DIAG-SWITCH-001", "R-STMT-0010");
    r_io_read_result_test_diagnostic(
        R_IO_READ_RESULT_DUPLICATE_VARIANT_PATH, "R-DIAG-SWITCH-001", "R-STMT-0010");
    r_io_read_result_test_diagnostic(
        R_IO_READ_RESULT_WRONG_VARIANT_PATH, "R-DIAG-TYPE-001", "R-STMT-0010");
    r_io_read_result_test_codegen_mutations();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

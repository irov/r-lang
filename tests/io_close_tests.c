#include "frontend_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef R_IO_CLOSE_INPUT_ARITY_PATH
#error R_IO_CLOSE_INPUT_ARITY_PATH is required
#endif
#ifndef R_IO_CLOSE_INPUT_SOURCE_PATH
#error R_IO_CLOSE_INPUT_SOURCE_PATH is required
#endif
#ifndef R_IO_CLOSE_OUTPUT_SOURCE_PATH
#error R_IO_CLOSE_OUTPUT_SOURCE_PATH is required
#endif
#ifndef R_IO_CLOSE_OUTPUT_HANDLE_TYPE_PATH
#error R_IO_CLOSE_OUTPUT_HANDLE_TYPE_PATH is required
#endif
#ifndef R_IO_CLOSE_INPUT_CONTEXT_TYPE_PATH
#error R_IO_CLOSE_INPUT_CONTEXT_TYPE_PATH is required
#endif
#ifndef R_IO_CLOSE_OUTPUT_DEADLINE_TYPE_PATH
#error R_IO_CLOSE_OUTPUT_DEADLINE_TYPE_PATH is required
#endif
#ifndef R_IO_CLOSE_INPUT_MISSING_MOVE_PATH
#error R_IO_CLOSE_INPUT_MISSING_MOVE_PATH is required
#endif
#ifndef R_IO_CLOSE_OUTPUT_NESTED_MOVE_PATH
#error R_IO_CLOSE_OUTPUT_NESTED_MOVE_PATH is required
#endif
#ifndef R_IO_CLOSE_INPUT_SUCCESS_REUSE_PATH
#error R_IO_CLOSE_INPUT_SUCCESS_REUSE_PATH is required
#endif

typedef struct RIoCloseDiagnosticCase {
    const char *path;
    const char *code;
    const char *rule_id;
    size_t diagnostic_count;
} RIoCloseDiagnosticCase;

typedef struct RIoCloseBuffer {
    char *bytes;
    size_t length;
    size_t capacity;
    size_t call_count;
} RIoCloseBuffer;

typedef struct RIoCloseView {
    RMirInstruction *call;
    RMirInstruction *stream_move;
    RMirInstruction *deadline;
    RTypeId stream_type;
    RTypeId deadline_type;
    RTypeId start_type;
    RTypeId task_type;
    RTypeId logical_type;
} RIoCloseView;

typedef enum RIoCloseMutation {
    R_IO_CLOSE_MUTATE_OPERATION_IDENTITY = 0,
    R_IO_CLOSE_MUTATE_OPERAND_COUNT,
    R_IO_CLOSE_MUTATE_BORROW_MASK,
    R_IO_CLOSE_MUTATE_STAGED_MOVE_KIND,
    R_IO_CLOSE_MUTATE_STAGED_MOVE_BIT,
    R_IO_CLOSE_MUTATE_STREAM_TYPE,
    R_IO_CLOSE_MUTATE_DEADLINE_OPTION,
    R_IO_CLOSE_MUTATE_DEADLINE_INSTANT,
    R_IO_CLOSE_MUTATE_START_KIND,
    R_IO_CLOSE_MUTATE_START_FLAGS,
    R_IO_CLOSE_MUTATE_START_BASE,
    R_IO_CLOSE_MUTATE_START_SECOND,
    R_IO_CLOSE_MUTATE_TASK_KIND,
    R_IO_CLOSE_MUTATE_TASK_FLAGS,
    R_IO_CLOSE_MUTATE_TASK_BASE,
    R_IO_CLOSE_MUTATE_TASK_SECOND,
    R_IO_CLOSE_MUTATE_LOGICAL_KIND,
    R_IO_CLOSE_MUTATE_LOGICAL_FLAGS,
    R_IO_CLOSE_MUTATE_LOGICAL_BASE,
    R_IO_CLOSE_MUTATE_LOGICAL_SECOND,
    R_IO_CLOSE_MUTATION_COUNT
} RIoCloseMutation;

static int failures;

#define R_IO_CLOSE_CHECK(condition)                                                                \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            (void)fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);    \
            failures += 1;                                                                         \
        }                                                                                          \
    } while (false)

static bool r_io_close_read_file(const char *path, uint8_t **bytes, size_t *length) {
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

static bool r_io_close_add_file(RFrontendContext *context, const char *path) {
    uint8_t *bytes = NULL;
    size_t length = 0U;
    RSourceId source_id = R_SOURCE_ID_INVALID;
    bool added;

    if (!r_io_close_read_file(path, &bytes, &length)) {
        return false;
    }
    added = r_frontend_add_source(context, path, bytes, length, &source_id) == R_FRONTEND_OK;
    free(bytes);
    return added && (source_id != R_SOURCE_ID_INVALID);
}

static bool r_io_close_write(void *user_data, const char *bytes, size_t length) {
    RIoCloseBuffer *buffer = user_data;
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

static void r_io_close_dispose_buffer(RIoCloseBuffer *buffer) {
    free(buffer->bytes);
    (void)memset(buffer, 0, sizeof(*buffer));
}

static bool
r_io_close_intern_equals(const RFrontendContext *context, uint32_t intern_id, const char *name) {
    const RInternEntry *entry;
    size_t name_length;

    if ((intern_id == UINT32_C(0)) || ((size_t)intern_id > context->intern_count)) {
        return false;
    }
    entry = &context->intern_entries[(size_t)intern_id - 1U];
    name_length = strlen(name);
    return (entry->length == name_length) && (memcmp(entry->bytes, name, name_length) == 0);
}

static bool r_io_close_standard_type_equals(const RFrontendContext *context,
                                            RTypeId type_id,
                                            const char *name) {
    const RSemanticType *type = r_semantic_type(context, type_id);

    return (type != NULL) && (type->kind == R_SEMANTIC_TYPE_STANDARD) &&
           (type->flags == R_SEMANTIC_TYPE_FLAG_NONE) && (type->base == R_TYPE_ID_INVALID) &&
           (type->second == R_TYPE_ID_INVALID) && (type->length != UINT64_C(0)) &&
           (type->length <= (uint64_t)UINT32_MAX) &&
           r_io_close_intern_equals(context, (uint32_t)type->length, name);
}

static RMirInstruction *r_io_close_value_definition(RFrontendContext *context, RMirValueId value) {
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

static RFrontendContext *r_io_close_build_context(const char *path) {
    RFrontendContext *context = r_frontend_create(NULL);

    if ((context == NULL) || !r_io_close_add_file(context, path) ||
        (r_frontend_analyze(context) != R_FRONTEND_OK) ||
        (r_frontend_diagnostic_count(context) != 0U) ||
        (r_frontend_lower_mir(context) != R_FRONTEND_OK) ||
        (r_frontend_diagnostic_count(context) != 0U)) {
        r_frontend_destroy(context);
        return NULL;
    }
    return context;
}

static bool r_io_close_find_view(RFrontendContext *context,
                                 RStandardCallOperation operation,
                                 RIoCloseView *view) {
    size_t index;
    size_t operand_index;
    RMirValueId stream_value;
    RMirValueId deadline_value;

    (void)memset(view, 0, sizeof(*view));
    view->stream_type = R_TYPE_ID_INVALID;
    view->deadline_type = R_TYPE_ID_INVALID;
    view->start_type = R_TYPE_ID_INVALID;
    view->task_type = R_TYPE_ID_INVALID;
    view->logical_type = R_TYPE_ID_INVALID;
    for (index = 0U; index < context->mir_instruction_count; ++index) {
        RMirInstruction *instruction = &context->mir_instructions[index];

        if ((instruction->kind == R_MIR_INSTRUCTION_STANDARD_CALL) &&
            (instruction->standard_operation == operation)) {
            if (view->call != NULL) {
                return false;
            }
            view->call = instruction;
        }
    }
    if ((view->call == NULL) || (view->call->operand_count != UINT32_C(2))) {
        return false;
    }
    operand_index = (size_t)view->call->first_operand;
    if ((operand_index > context->mir_operand_count) ||
        ((context->mir_operand_count - operand_index) < 2U)) {
        return false;
    }
    stream_value = context->mir_operands[operand_index];
    deadline_value = context->mir_operands[operand_index + 1U];
    view->stream_move = r_io_close_value_definition(context, stream_value);
    view->deadline = r_io_close_value_definition(context, deadline_value);
    if ((view->stream_move == NULL) || (view->deadline == NULL)) {
        return false;
    }
    view->stream_type = view->stream_move->type;
    view->deadline_type = view->deadline->type;
    view->start_type = view->call->type;
    view->task_type = view->call->auxiliary_type;
    {
        const RSemanticType *task = r_semantic_type(context, view->task_type);

        if (task == NULL) {
            return false;
        }
        view->logical_type = task->base;
    }
    return view->logical_type != R_TYPE_ID_INVALID;
}

static bool r_io_close_view_is_valid(const RFrontendContext *context,
                                     const RIoCloseView *view,
                                     RStandardCallOperation operation) {
    const bool is_input = operation == R_STANDARD_CALL_IO_CLOSE_INPUT;
    const RSemanticType *start = r_semantic_type(context, view->start_type);
    const RSemanticType *start_effects =
        start == NULL ? NULL : r_semantic_type(context, start->second);
    const RSemanticType *task = r_semantic_type(context, view->task_type);
    const RSemanticType *completion_effects =
        task == NULL ? NULL : r_semantic_type(context, task->second);
    const RSemanticType *logical = r_semantic_type(context, view->logical_type);
    const RSemanticType *deadline = r_semantic_type(context, view->deadline_type);

    if ((start == NULL) || (start_effects == NULL) || (task == NULL) ||
        (completion_effects == NULL) || (logical == NULL) || (deadline == NULL) ||
        (view->call == NULL) || (view->stream_move == NULL) || (view->deadline == NULL)) {
        return false;
    }
    return (view->call->standard_operation == operation) &&
           (view->call->operand_count == UINT32_C(2)) &&
           (view->call->call_borrow_mask.low == UINT64_C(0)) &&
           (view->call->call_borrow_mask.high == UINT64_C(0)) &&
           (view->stream_move->kind == R_MIR_INSTRUCTION_MOVE) &&
           view->stream_move->is_async_staged_move &&
           r_io_close_standard_type_equals(
               context, view->stream_type, is_input ? "std.io::input" : "std.io::output") &&
           (deadline->kind == R_SEMANTIC_TYPE_OPTION) &&
           (deadline->flags == R_SEMANTIC_TYPE_FLAG_NONE) &&
           (deadline->second == R_TYPE_ID_INVALID) &&
           r_io_close_standard_type_equals(context, deadline->base, "std.time::instant") &&
           (start->kind == R_SEMANTIC_TYPE_EFFECT_CARRIER) &&
           (start->flags == R_SEMANTIC_TYPE_FLAG_NONE) && (start->base == view->task_type) &&
           (start_effects->kind == R_SEMANTIC_TYPE_EFFECT_SET) &&
           (r_semantic_effect_count(context, start->second) == UINT32_C(1)) &&
           r_io_close_standard_type_equals(
               context,
               r_semantic_effect_at(context, start->second, UINT32_C(0)),
               "std.async::start_error") &&
           (task->kind == R_SEMANTIC_TYPE_TASK) && (task->flags == R_SEMANTIC_TYPE_FLAG_NONE) &&
           (task->base == view->logical_type) &&
           (completion_effects->kind == R_SEMANTIC_TYPE_EFFECT_SET) &&
           (r_semantic_effect_count(context, task->second) == UINT32_C(1)) &&
           r_io_close_standard_type_equals(context,
                                           r_semantic_effect_at(context, task->second, UINT32_C(0)),
                                           "std.io::io_error") &&
           (logical->kind == R_SEMANTIC_TYPE_VOID) &&
           (logical->flags == R_SEMANTIC_TYPE_FLAG_NONE) && (logical->base == R_TYPE_ID_INVALID) &&
           (logical->second == R_TYPE_ID_INVALID);
}

static bool r_io_close_apply_mutation(RFrontendContext *context,
                                      RIoCloseView *view,
                                      RIoCloseMutation mutation) {
    RSemanticType *start = &context->semantic_types[(size_t)view->start_type - 1U];
    RSemanticType *task = &context->semantic_types[(size_t)view->task_type - 1U];
    RSemanticType *logical = &context->semantic_types[(size_t)view->logical_type - 1U];
    RSemanticType *deadline = &context->semantic_types[(size_t)view->deadline_type - 1U];

    switch (mutation) {
    case R_IO_CLOSE_MUTATE_OPERATION_IDENTITY:
        view->call->standard_operation =
            view->call->standard_operation == R_STANDARD_CALL_IO_CLOSE_INPUT
                ? R_STANDARD_CALL_IO_CLOSE_OUTPUT
                : R_STANDARD_CALL_IO_CLOSE_INPUT;
        return true;
    case R_IO_CLOSE_MUTATE_OPERAND_COUNT:
        view->call->operand_count = UINT32_C(1);
        return true;
    case R_IO_CLOSE_MUTATE_BORROW_MASK:
        view->call->call_borrow_mask.low = UINT64_C(1);
        return true;
    case R_IO_CLOSE_MUTATE_STAGED_MOVE_KIND:
        view->stream_move->kind = R_MIR_INSTRUCTION_LOAD;
        return true;
    case R_IO_CLOSE_MUTATE_STAGED_MOVE_BIT:
        view->stream_move->is_async_staged_move = false;
        return true;
    case R_IO_CLOSE_MUTATE_STREAM_TYPE:
        view->stream_move->type = view->deadline_type;
        return true;
    case R_IO_CLOSE_MUTATE_DEADLINE_OPTION:
        view->deadline->type = view->stream_type;
        return true;
    case R_IO_CLOSE_MUTATE_DEADLINE_INSTANT:
        deadline->base = view->stream_type;
        return true;
    case R_IO_CLOSE_MUTATE_START_KIND:
        start->kind = R_SEMANTIC_TYPE_ARRAY;
        return true;
    case R_IO_CLOSE_MUTATE_START_FLAGS:
        start->flags = R_SEMANTIC_TYPE_FLAG_SHARED;
        return true;
    case R_IO_CLOSE_MUTATE_START_BASE:
        start->base = view->logical_type;
        return true;
    case R_IO_CLOSE_MUTATE_START_SECOND:
        start->second = view->stream_type;
        return true;
    case R_IO_CLOSE_MUTATE_TASK_KIND:
        task->kind = R_SEMANTIC_TYPE_ARRAY;
        return true;
    case R_IO_CLOSE_MUTATE_TASK_FLAGS:
        task->flags = R_SEMANTIC_TYPE_FLAG_SHARED;
        return true;
    case R_IO_CLOSE_MUTATE_TASK_BASE:
        task->base = view->stream_type;
        return true;
    case R_IO_CLOSE_MUTATE_TASK_SECOND:
        task->second = view->stream_type;
        return true;
    case R_IO_CLOSE_MUTATE_LOGICAL_KIND:
        logical->kind = R_SEMANTIC_TYPE_ARRAY;
        return true;
    case R_IO_CLOSE_MUTATE_LOGICAL_FLAGS:
        logical->flags = R_SEMANTIC_TYPE_FLAG_SHARED;
        return true;
    case R_IO_CLOSE_MUTATE_LOGICAL_BASE:
        logical->base = view->stream_type;
        return true;
    case R_IO_CLOSE_MUTATE_LOGICAL_SECOND:
        logical->second = view->stream_type;
        return true;
    case R_IO_CLOSE_MUTATION_COUNT:
    default:
        return false;
    }
}

static const char *r_io_close_mutation_name(RIoCloseMutation mutation) {
    static const char *const names[] = {
        "operation_identity", "operand_count", "borrow_mask",     "staged_move_kind",
        "staged_move_bit",    "stream_type",   "deadline_option", "deadline_instant",
        "start_kind",         "start_flags",   "start_base",      "start_second",
        "task_kind",          "task_flags",    "task_base",       "task_second",
        "logical_kind",       "logical_flags", "logical_base",    "logical_second",
    };

    if ((size_t)mutation >= (sizeof(names) / sizeof(names[0]))) {
        return "invalid";
    }
    return names[(size_t)mutation];
}

static void r_io_close_test_positive(const char *path, RStandardCallOperation operation) {
    RFrontendContext *context = r_io_close_build_context(path);
    RIoCloseView view;
    RIoCloseBuffer first = {0};
    RIoCloseBuffer second = {0};

    R_IO_CLOSE_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_IO_CLOSE_CHECK(r_io_close_find_view(context, operation, &view));
    R_IO_CLOSE_CHECK(r_io_close_view_is_valid(context, &view, operation));
    R_IO_CLOSE_CHECK(r_frontend_emit_c17(context, r_io_close_write, &first) == R_FRONTEND_OK);
    R_IO_CLOSE_CHECK(first.call_count == 1U);
    R_IO_CLOSE_CHECK(first.length != 0U);
    R_IO_CLOSE_CHECK(r_frontend_emit_c17(context, r_io_close_write, &second) == R_FRONTEND_OK);
    R_IO_CLOSE_CHECK(second.call_count == 1U);
    R_IO_CLOSE_CHECK(
        (first.length == second.length) &&
        ((first.length == 0U) || (memcmp(first.bytes, second.bytes, first.length) == 0)));
    r_io_close_dispose_buffer(&second);
    r_io_close_dispose_buffer(&first);
    r_frontend_destroy(context);
}

static void r_io_close_test_mutations(const char *path,
                                      RStandardCallOperation operation,
                                      const char *operation_name) {
    size_t mutation;

    for (mutation = 0U; mutation < (size_t)R_IO_CLOSE_MUTATION_COUNT; ++mutation) {
        RFrontendContext *context = r_io_close_build_context(path);
        RIoCloseView view;
        RIoCloseBuffer output = {0};
        RFrontendStatus status;

        R_IO_CLOSE_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        R_IO_CLOSE_CHECK(r_io_close_find_view(context, operation, &view));
        R_IO_CLOSE_CHECK(r_io_close_apply_mutation(context, &view, (RIoCloseMutation)mutation));
        status = r_frontend_emit_c17(context, r_io_close_write, &output);
        if (status != R_FRONTEND_NOT_LOWERABLE) {
            (void)fprintf(stderr,
                          "%s mutation %zu (%s) unexpectedly returned status %d\n",
                          operation_name,
                          mutation,
                          r_io_close_mutation_name((RIoCloseMutation)mutation),
                          (int)status);
        }
        R_IO_CLOSE_CHECK(status == R_FRONTEND_NOT_LOWERABLE);
        R_IO_CLOSE_CHECK(output.call_count == 0U);
        R_IO_CLOSE_CHECK(output.length == 0U);
        r_io_close_dispose_buffer(&output);
        r_frontend_destroy(context);
    }
}

static void r_io_close_test_diagnostic(const RIoCloseDiagnosticCase *test_case) {
    RFrontendContext *context = r_frontend_create(NULL);
    const RDiagnostic *diagnostic;

    R_IO_CLOSE_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_IO_CLOSE_CHECK(r_io_close_add_file(context, test_case->path));
    R_IO_CLOSE_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
    R_IO_CLOSE_CHECK(r_frontend_diagnostic_count(context) == test_case->diagnostic_count);
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
    static const RIoCloseDiagnosticCase cases[] = {
        {R_IO_CLOSE_INPUT_ARITY_PATH, "R-DIAG-TYPE-001", "R-SLIB-IO-0006", 2U},
        {R_IO_CLOSE_OUTPUT_HANDLE_TYPE_PATH, "R-DIAG-TYPE-001", "R-SLIB-IO-0006", 2U},
        {R_IO_CLOSE_INPUT_CONTEXT_TYPE_PATH, "R-DIAG-TYPE-001", "R-SLIB-IO-0006", 1U},
        {R_IO_CLOSE_OUTPUT_DEADLINE_TYPE_PATH, "R-DIAG-TYPE-001", "R-SLIB-IO-0006", 2U},
        {R_IO_CLOSE_INPUT_MISSING_MOVE_PATH, "R-DIAG-MOVE-001", "R-OWN-0003", 2U},
        {R_IO_CLOSE_OUTPUT_NESTED_MOVE_PATH, "R-DIAG-MOVE-003", "R-FUNC-0010", 2U},
        {R_IO_CLOSE_INPUT_SUCCESS_REUSE_PATH, "R-DIAG-MOVE-002", "R-OWN-0004", 1U},
    };
    size_t index;

    r_io_close_test_positive(R_IO_CLOSE_INPUT_SOURCE_PATH, R_STANDARD_CALL_IO_CLOSE_INPUT);
    r_io_close_test_positive(R_IO_CLOSE_OUTPUT_SOURCE_PATH, R_STANDARD_CALL_IO_CLOSE_OUTPUT);
    for (index = 0U; index < (sizeof(cases) / sizeof(cases[0])); ++index) {
        r_io_close_test_diagnostic(&cases[index]);
    }
    r_io_close_test_mutations(
        R_IO_CLOSE_INPUT_SOURCE_PATH, R_STANDARD_CALL_IO_CLOSE_INPUT, "close_input");
    r_io_close_test_mutations(
        R_IO_CLOSE_OUTPUT_SOURCE_PATH, R_STANDARD_CALL_IO_CLOSE_OUTPUT, "close_output");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

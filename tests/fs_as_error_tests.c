#include "frontend_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef R_FS_AS_ERROR_SOURCE_PATH
#error R_FS_AS_ERROR_SOURCE_PATH is required
#endif
#ifndef R_FS_AS_ERROR_ARITY_PATH
#error R_FS_AS_ERROR_ARITY_PATH is required
#endif
#ifndef R_FS_AS_ERROR_ARGUMENT_TYPE_PATH
#error R_FS_AS_ERROR_ARGUMENT_TYPE_PATH is required
#endif
#ifndef R_FS_AS_ERROR_RESULT_TYPE_PATH
#error R_FS_AS_ERROR_RESULT_TYPE_PATH is required
#endif

typedef struct RFsAsErrorDiagnosticCase {
    const char *path;
    const char *message;
} RFsAsErrorDiagnosticCase;

typedef struct RFsAsErrorBuffer {
    char *bytes;
    size_t length;
    size_t capacity;
    size_t call_count;
} RFsAsErrorBuffer;

typedef struct RFsAsErrorView {
    RHirNode *sync_hir_call;
    RHirNode *async_hir_call;
    RHirNode *sync_hir_argument;
    RHirNode *async_hir_argument;
    RMirInstruction *sync_mir_call;
    RMirInstruction *async_mir_call;
    RMirInstruction *sync_mir_argument;
    RMirInstruction *async_mir_argument;
    RTypeId fs_error_type;
    RTypeId error_type;
} RFsAsErrorView;

static int failures;

#define R_FS_AS_ERROR_CHECK(condition)                                                             \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            (void)fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);    \
            failures += 1;                                                                         \
        }                                                                                          \
    } while (false)

static bool r_fs_as_error_read_file(const char *path, uint8_t **bytes, size_t *length) {
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

static bool r_fs_as_error_add_file(RFrontendContext *context, const char *path) {
    uint8_t *bytes = NULL;
    size_t length = 0U;
    RSourceId source_id = R_SOURCE_ID_INVALID;
    bool added;

    if (!r_fs_as_error_read_file(path, &bytes, &length)) {
        return false;
    }
    added = r_frontend_add_source(context, path, bytes, length, &source_id) == R_FRONTEND_OK;
    free(bytes);
    return added && (source_id != R_SOURCE_ID_INVALID);
}

static bool r_fs_as_error_write(void *user_data, const char *bytes, size_t length) {
    RFsAsErrorBuffer *buffer = user_data;
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

static void r_fs_as_error_dispose_buffer(RFsAsErrorBuffer *buffer) {
    free(buffer->bytes);
    (void)memset(buffer, 0, sizeof(*buffer));
}

static bool
r_fs_as_error_intern_equals(const RFrontendContext *context, uint32_t intern_id, const char *name) {
    const RInternEntry *entry;
    size_t name_length;

    if ((intern_id == UINT32_C(0)) || ((size_t)intern_id > context->intern_count)) {
        return false;
    }
    entry = &context->intern_entries[(size_t)intern_id - 1U];
    name_length = strlen(name);
    return (entry->length == name_length) && (memcmp(entry->bytes, name, name_length) == 0);
}

static bool r_fs_as_error_standard_type_equals(const RFrontendContext *context,
                                               RTypeId type_id,
                                               const char *name) {
    const RSemanticType *type = r_semantic_type(context, type_id);

    return (type != NULL) && (type->kind == R_SEMANTIC_TYPE_STANDARD) &&
           (type->flags == R_SEMANTIC_TYPE_FLAG_NONE) && (type->base == R_TYPE_ID_INVALID) &&
           (type->second == R_TYPE_ID_INVALID) && (type->length != UINT64_C(0)) &&
           (type->length <= (uint64_t)UINT32_MAX) &&
           r_fs_as_error_intern_equals(context, (uint32_t)type->length, name);
}

static RSemanticAggregate *r_fs_as_error_find_aggregate(RFrontendContext *context,
                                                        const char *name) {
    size_t index;

    for (index = 0U; index < context->semantic_aggregate_count; ++index) {
        RSemanticAggregate *aggregate = &context->semantic_aggregates[index];

        if (r_fs_as_error_intern_equals(context, aggregate->name_intern_id, name)) {
            return aggregate;
        }
    }
    return NULL;
}

static RMirInstruction *r_fs_as_error_function_value_definition(RFrontendContext *context,
                                                                const RMirFunction *function,
                                                                RMirValueId value) {
    size_t block_offset;

    if ((value == R_MIR_VALUE_ID_INVALID) ||
        ((size_t)function->first_block + (size_t)function->block_count >
         context->mir_block_count)) {
        return NULL;
    }
    for (block_offset = 0U; block_offset < (size_t)function->block_count; ++block_offset) {
        const RMirBlock *block = &context->mir_blocks[(size_t)function->first_block + block_offset];
        size_t instruction_offset;

        if ((size_t)block->first_instruction + (size_t)block->instruction_count >
            context->mir_instruction_count) {
            return NULL;
        }
        for (instruction_offset = 0U; instruction_offset < (size_t)block->instruction_count;
             ++instruction_offset) {
            RMirInstruction *instruction =
                &context->mir_instructions[(size_t)block->first_instruction + instruction_offset];

            if (instruction->result == value) {
                return instruction;
            }
        }
    }
    return NULL;
}

static RHirNode *
r_fs_as_error_hir_child(RFrontendContext *context, const RHirNode *node, uint32_t child_index) {
    size_t index;
    RHirNodeId child;

    if ((node == NULL) || (child_index >= node->child_count)) {
        return NULL;
    }
    index = (size_t)node->first_child + (size_t)child_index;
    if (index >= context->hir_child_count) {
        return NULL;
    }
    child = context->hir_children[index];
    if ((child == R_HIR_NODE_ID_INVALID) || ((size_t)child > context->hir_node_count)) {
        return NULL;
    }
    return &context->hir_nodes[(size_t)child - 1U];
}

static RFrontendContext *r_fs_as_error_build_context(void) {
    RFrontendContext *context = r_frontend_create(NULL);

    if ((context == NULL) || !r_fs_as_error_add_file(context, R_FS_AS_ERROR_SOURCE_PATH) ||
        (r_frontend_analyze(context) != R_FRONTEND_OK) ||
        (r_frontend_diagnostic_count(context) != 0U) ||
        (r_frontend_lower_mir(context) != R_FRONTEND_OK) ||
        (r_frontend_diagnostic_count(context) != 0U)) {
        r_frontend_destroy(context);
        return NULL;
    }
    return context;
}

static bool r_fs_as_error_find_view(RFrontendContext *context, RFsAsErrorView *view) {
    size_t index;
    size_t hir_count = 0U;
    size_t mir_count = 0U;

    (void)memset(view, 0, sizeof(*view));
    view->fs_error_type = R_TYPE_ID_INVALID;
    view->error_type = R_TYPE_ID_INVALID;
    for (index = 0U; index < context->hir_node_count; ++index) {
        RHirNode *node = &context->hir_nodes[index];

        if ((node->kind != R_HIR_STANDARD_CALL) ||
            (node->standard_operation != R_STANDARD_CALL_FS_AS_ERROR)) {
            continue;
        }
        if (hir_count == 0U) {
            view->sync_hir_call = node;
        } else if (hir_count == 1U) {
            view->async_hir_call = node;
        }
        hir_count += 1U;
    }
    for (index = 0U; index < context->mir_function_count; ++index) {
        const RMirFunction *function = &context->mir_functions[index];
        const RSemanticSymbol *symbol;
        size_t block_offset;

        if ((function->symbol == R_SYMBOL_ID_INVALID) ||
            ((size_t)function->symbol > context->semantic_symbol_count) ||
            ((size_t)function->first_block + (size_t)function->block_count >
             context->mir_block_count)) {
            return false;
        }
        symbol = &context->semantic_symbols[(size_t)function->symbol - 1U];
        for (block_offset = 0U; block_offset < (size_t)function->block_count; ++block_offset) {
            const RMirBlock *block =
                &context->mir_blocks[(size_t)function->first_block + block_offset];
            size_t instruction_offset;

            if ((size_t)block->first_instruction + (size_t)block->instruction_count >
                context->mir_instruction_count) {
                return false;
            }
            for (instruction_offset = 0U; instruction_offset < (size_t)block->instruction_count;
                 ++instruction_offset) {
                RMirInstruction *instruction =
                    &context
                         ->mir_instructions[(size_t)block->first_instruction + instruction_offset];

                if ((instruction->kind != R_MIR_INSTRUCTION_STANDARD_CALL) ||
                    (instruction->standard_operation != R_STANDARD_CALL_FS_AS_ERROR)) {
                    continue;
                }
                if (symbol->is_async) {
                    if (view->async_mir_call != NULL) {
                        return false;
                    }
                    view->async_mir_call = instruction;
                } else {
                    if (view->sync_mir_call != NULL) {
                        return false;
                    }
                    view->sync_mir_call = instruction;
                }
                if ((instruction->operand_count != UINT32_C(1)) ||
                    ((size_t)instruction->first_operand >= context->mir_operand_count)) {
                    return false;
                }
                if (symbol->is_async) {
                    view->async_mir_argument = r_fs_as_error_function_value_definition(
                        context,
                        function,
                        context->mir_operands[(size_t)instruction->first_operand]);
                } else {
                    view->sync_mir_argument = r_fs_as_error_function_value_definition(
                        context,
                        function,
                        context->mir_operands[(size_t)instruction->first_operand]);
                }
                mir_count += 1U;
            }
        }
    }
    if ((hir_count != 2U) || (mir_count != 2U) || (view->sync_hir_call == NULL) ||
        (view->async_hir_call == NULL) || (view->sync_mir_call == NULL) ||
        (view->async_mir_call == NULL) || (view->sync_hir_call->child_count != UINT32_C(1)) ||
        (view->async_hir_call->child_count != UINT32_C(1)) ||
        (view->sync_mir_call->operand_count != UINT32_C(1)) ||
        (view->async_mir_call->operand_count != UINT32_C(1))) {
        return false;
    }
    view->sync_hir_argument = r_fs_as_error_hir_child(context, view->sync_hir_call, UINT32_C(0));
    view->async_hir_argument = r_fs_as_error_hir_child(context, view->async_hir_call, UINT32_C(0));
    view->fs_error_type = view->sync_hir_call->auxiliary_type;
    view->error_type = view->sync_hir_call->type;
    return (view->sync_hir_argument != NULL) && (view->async_hir_argument != NULL) &&
           (view->sync_mir_argument != NULL) && (view->async_mir_argument != NULL);
}

static bool r_fs_as_error_view_is_valid(const RFrontendContext *context,
                                        const RFsAsErrorView *view) {
    if (view->sync_hir_call == NULL || view->async_hir_call == NULL ||
        view->sync_hir_argument == NULL || view->async_hir_argument == NULL ||
        view->sync_mir_call == NULL || view->async_mir_call == NULL ||
        view->sync_mir_argument == NULL || view->async_mir_argument == NULL) {
        return false;
    }
    return r_fs_as_error_standard_type_equals(context, view->fs_error_type, "std.fs::fs_error") &&
           r_fs_as_error_standard_type_equals(context, view->error_type, "std.error::error") &&
           (view->sync_hir_call->type == view->error_type) &&
           (view->sync_hir_call->auxiliary_type == view->fs_error_type) &&
           (view->async_hir_call->type == view->error_type) &&
           (view->async_hir_call->auxiliary_type == view->fs_error_type) &&
           (view->sync_hir_argument->type == view->fs_error_type) &&
           (view->async_hir_argument->type == view->fs_error_type) &&
           (view->sync_mir_call->type == view->error_type) &&
           (view->sync_mir_call->auxiliary_type == view->fs_error_type) &&
           (view->async_mir_call->type == view->error_type) &&
           (view->async_mir_call->auxiliary_type == view->fs_error_type) &&
           (view->sync_mir_call->call_borrow_mask.low == UINT64_C(0)) &&
           (view->sync_mir_call->call_borrow_mask.high == UINT64_C(0)) &&
           (view->async_mir_call->call_borrow_mask.low == UINT64_C(0)) &&
           (view->async_mir_call->call_borrow_mask.high == UINT64_C(0)) &&
           (view->sync_mir_argument->kind == R_MIR_INSTRUCTION_LOAD) &&
           (view->sync_mir_argument->type == view->fs_error_type) &&
           !view->sync_mir_argument->is_async_staged_move &&
           (view->async_mir_argument->kind == R_MIR_INSTRUCTION_LOAD) &&
           (view->async_mir_argument->type == view->fs_error_type) &&
           !view->async_mir_argument->is_async_staged_move;
}

static bool r_fs_as_error_struct_matches(RFrontendContext *context,
                                         const char *name,
                                         RTypeId expected_type,
                                         const char *const *field_names,
                                         const RTypeId *field_types,
                                         uint32_t field_count) {
    RSemanticAggregate *aggregate = r_fs_as_error_find_aggregate(context, name);
    uint32_t field_index;

    if ((aggregate == NULL) || (aggregate->kind != R_SEMANTIC_AGGREGATE_STRUCT) ||
        (aggregate->type != expected_type) || !aggregate->is_copy || !aggregate->complete ||
        (aggregate->field_count != field_count) ||
        ((size_t)aggregate->first_field + (size_t)field_count > context->semantic_field_count)) {
        return false;
    }
    for (field_index = UINT32_C(0); field_index < field_count; ++field_index) {
        const RSemanticField *field =
            &context->semantic_fields[(size_t)aggregate->first_field + (size_t)field_index];

        if (!r_fs_as_error_intern_equals(
                context, field->name_intern_id, field_names[field_index]) ||
            (field->type != field_types[field_index]) || (field->layout_index != field_index)) {
            return false;
        }
    }
    return true;
}

static bool r_fs_as_error_enum_matches(RFrontendContext *context,
                                       const char *name,
                                       RTypeId expected_type,
                                       const char *const *variant_names,
                                       uint32_t variant_count) {
    RSemanticAggregate *aggregate = r_fs_as_error_find_aggregate(context, name);
    uint32_t variant_index;

    if ((aggregate == NULL) || (aggregate->kind != R_SEMANTIC_AGGREGATE_ENUM) ||
        (aggregate->type != expected_type) || !aggregate->is_copy || !aggregate->complete ||
        (aggregate->variant_count != variant_count) ||
        ((size_t)aggregate->first_variant + (size_t)variant_count >
         context->semantic_variant_count)) {
        return false;
    }
    for (variant_index = UINT32_C(0); variant_index < variant_count; ++variant_index) {
        const RSemanticVariant *variant =
            &context->semantic_variants[(size_t)aggregate->first_variant + (size_t)variant_index];

        if (!r_fs_as_error_intern_equals(
                context, variant->name_intern_id, variant_names[variant_index]) ||
            (variant->declaration_index != variant_index) ||
            (variant->value != (uint64_t)variant_index)) {
            return false;
        }
    }
    return true;
}

static bool r_fs_as_error_schemas_are_valid(RFrontendContext *context, const RFsAsErrorView *view) {
    static const char *const fs_code_variants[] = {
        "invalid_path",
        "invalid_relative_path",
        "invalid_operation",
        "not_found",
        "already_exists",
        "not_directory",
        "is_directory",
        "directory_not_empty",
        "permission_denied",
        "read_only",
        "name_too_long",
        "too_many_links",
        "no_space",
        "file_too_large",
        "resource_exhausted",
        "cancelled",
        "timed_out",
        "unsupported",
        "other",
        "closed",
    };
    static const char *const domain_variants[] = {
        "allocation",
        "async_runtime",
        "bytes",
        "string",
        "conversion",
        "format",
        "math",
        "time",
        "environment",
        "io",
        "filesystem",
        "network",
        "process",
        "threading",
        "c_abi",
    };
    static const char *const fs_error_fields[] = {"code", "native_code"};
    static const char *const error_fields[] = {"domain", "code", "native_code"};
    RSemanticAggregate *fs_code = r_fs_as_error_find_aggregate(context, "std.fs::error_code");
    RSemanticAggregate *domain = r_fs_as_error_find_aggregate(context, "std.error::domain");
    RTypeId fs_error_field_types[2];
    RTypeId error_field_types[3];
    RTypeId u32_type = R_TYPE_ID_INVALID;
    RTypeId i64_type = R_TYPE_ID_INVALID;
    size_t index;

    for (index = 0U; index < context->semantic_type_count; ++index) {
        if (context->semantic_types[index].kind == R_SEMANTIC_TYPE_U32) {
            u32_type = (RTypeId)index + UINT32_C(1);
        } else if (context->semantic_types[index].kind == R_SEMANTIC_TYPE_I64) {
            i64_type = (RTypeId)index + UINT32_C(1);
        }
    }
    if ((fs_code == NULL) || (domain == NULL) || (u32_type == R_TYPE_ID_INVALID) ||
        (i64_type == R_TYPE_ID_INVALID)) {
        return false;
    }
    fs_error_field_types[0] = fs_code->type;
    fs_error_field_types[1] = i64_type;
    error_field_types[0] = domain->type;
    error_field_types[1] = u32_type;
    error_field_types[2] = i64_type;
    return r_fs_as_error_enum_matches(
               context,
               "std.fs::error_code",
               fs_code->type,
               fs_code_variants,
               (uint32_t)(sizeof(fs_code_variants) / sizeof(fs_code_variants[0]))) &&
           r_fs_as_error_enum_matches(
               context,
               "std.error::domain",
               domain->type,
               domain_variants,
               (uint32_t)(sizeof(domain_variants) / sizeof(domain_variants[0]))) &&
           r_fs_as_error_struct_matches(context,
                                        "std.fs::fs_error",
                                        view->fs_error_type,
                                        fs_error_fields,
                                        fs_error_field_types,
                                        UINT32_C(2)) &&
           r_fs_as_error_struct_matches(context,
                                        "std.error::error",
                                        view->error_type,
                                        error_fields,
                                        error_field_types,
                                        UINT32_C(3));
}

static void r_fs_as_error_test_positive(void) {
    RFrontendContext *context = r_fs_as_error_build_context();
    RFsAsErrorView view;
    RFsAsErrorBuffer first = {0};
    RFsAsErrorBuffer second = {0};

    R_FS_AS_ERROR_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_FS_AS_ERROR_CHECK(r_fs_as_error_find_view(context, &view));
    R_FS_AS_ERROR_CHECK(r_fs_as_error_view_is_valid(context, &view));
    R_FS_AS_ERROR_CHECK(r_fs_as_error_schemas_are_valid(context, &view));
    R_FS_AS_ERROR_CHECK(
        r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_fs_as_error_write, &first) ==
        R_FRONTEND_OK);
    R_FS_AS_ERROR_CHECK(first.call_count == 1U);
    R_FS_AS_ERROR_CHECK(first.length != 0U);
    R_FS_AS_ERROR_CHECK(
        r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_fs_as_error_write, &second) ==
        R_FRONTEND_OK);
    R_FS_AS_ERROR_CHECK(second.call_count == 1U);
    R_FS_AS_ERROR_CHECK(
        (first.length == second.length) &&
        ((first.length == 0U) || (memcmp(first.bytes, second.bytes, first.length) == 0)));
    r_fs_as_error_dispose_buffer(&second);
    r_fs_as_error_dispose_buffer(&first);
    r_frontend_destroy(context);
}

static void r_fs_as_error_test_diagnostic(const RFsAsErrorDiagnosticCase *test_case) {
    RFrontendContext *context = r_frontend_create(NULL);
    const RDiagnostic *diagnostic;

    R_FS_AS_ERROR_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_FS_AS_ERROR_CHECK(r_fs_as_error_add_file(context, test_case->path));
    R_FS_AS_ERROR_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
    R_FS_AS_ERROR_CHECK(r_frontend_diagnostic_count(context) == 1U);
    diagnostic = r_frontend_diagnostic(context, 0U);
    if ((diagnostic == NULL) || (strcmp(diagnostic->code, "R-DIAG-TYPE-001") != 0) ||
        (strcmp(diagnostic->rule_id, "R-SLIB-FS-0002") != 0) ||
        (strcmp(diagnostic->message, test_case->message) != 0) ||
        (diagnostic->severity != R_DIAGNOSTIC_ERROR) ||
        (diagnostic->phase != R_DIAGNOSTIC_PHASE_SEMANTIC)) {
        (void)fprintf(stderr,
                      "%s: expected R-DIAG-TYPE-001/R-SLIB-FS-0002 `%s`, observed "
                      "%s/%s `%s` phase %d\n",
                      test_case->path,
                      test_case->message,
                      diagnostic == NULL ? "<none>" : diagnostic->code,
                      diagnostic == NULL ? "<none>" : diagnostic->rule_id,
                      diagnostic == NULL ? "<none>" : diagnostic->message,
                      diagnostic == NULL ? -1 : (int)diagnostic->phase);
        failures += 1;
    }
    r_frontend_destroy(context);
}

int main(void) {
    static const RFsAsErrorDiagnosticCase cases[] = {
        {R_FS_AS_ERROR_ARITY_PATH, "std.fs::as_error requires exactly one std.fs::fs_error value"},
        {R_FS_AS_ERROR_ARGUMENT_TYPE_PATH,
         "std.fs::as_error argument shall have type std.fs::fs_error"},
        {R_FS_AS_ERROR_RESULT_TYPE_PATH,
         "std.fs::as_error result shall have type std.error::error"},
    };
    size_t index;

    r_fs_as_error_test_positive();
    for (index = 0U; index < (sizeof(cases) / sizeof(cases[0])); ++index) {
        r_fs_as_error_test_diagnostic(&cases[index]);
    }
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

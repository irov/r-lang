#include "frontend_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef R_FS_PATH_IS_ABSOLUTE_SOURCE_PATH
#error R_FS_PATH_IS_ABSOLUTE_SOURCE_PATH is required
#endif
#ifndef R_FS_PATH_IS_ABSOLUTE_ARITY_PATH
#error R_FS_PATH_IS_ABSOLUTE_ARITY_PATH is required
#endif
#ifndef R_FS_PATH_IS_ABSOLUTE_ARGUMENT_TYPE_PATH
#error R_FS_PATH_IS_ABSOLUTE_ARGUMENT_TYPE_PATH is required
#endif
#ifndef R_FS_PATH_IS_ABSOLUTE_RESULT_TYPE_PATH
#error R_FS_PATH_IS_ABSOLUTE_RESULT_TYPE_PATH is required
#endif

enum {
    R_FS_PATH_IS_ABSOLUTE_CALL_COUNT = 4,
    R_FS_PATH_IS_ABSOLUTE_ASYNC_CALL_COUNT = 3
};

typedef struct RFsPathIsAbsoluteDiagnosticCase {
    const char *path;
    const char *message;
} RFsPathIsAbsoluteDiagnosticCase;

typedef struct RFsPathIsAbsoluteBuffer {
    char *bytes;
    size_t length;
    size_t capacity;
    size_t call_count;
} RFsPathIsAbsoluteBuffer;

typedef struct RFsPathIsAbsoluteView {
    RHirNode *sync_hir_call;
    RHirNode *sync_hir_argument;
    RHirNode *async_hir_calls[R_FS_PATH_IS_ABSOLUTE_ASYNC_CALL_COUNT];
    RHirNode *async_hir_arguments[R_FS_PATH_IS_ABSOLUTE_ASYNC_CALL_COUNT];
    RMirInstruction *sync_mir_call;
    RMirInstruction *sync_mir_argument;
    RMirInstruction *async_mir_calls[R_FS_PATH_IS_ABSOLUTE_ASYNC_CALL_COUNT];
    RMirInstruction *async_mir_arguments[R_FS_PATH_IS_ABSOLUTE_ASYNC_CALL_COUNT];
    RTypeId bool_type;
    RTypeId borrow_type;
    RTypeId path_type;
} RFsPathIsAbsoluteView;

static int failures;

#define R_FS_PATH_IS_ABSOLUTE_CHECK(condition)                                                     \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            (void)fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);    \
            failures += 1;                                                                         \
        }                                                                                          \
    } while (false)

static bool r_fs_path_is_absolute_read_file(const char *path, uint8_t **bytes, size_t *length) {
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

static bool r_fs_path_is_absolute_add_file(RFrontendContext *context, const char *path) {
    uint8_t *bytes = NULL;
    size_t length = 0U;
    RSourceId source_id = R_SOURCE_ID_INVALID;
    bool added;

    if (!r_fs_path_is_absolute_read_file(path, &bytes, &length)) {
        return false;
    }
    added = r_frontend_add_source(context, path, bytes, length, &source_id) == R_FRONTEND_OK;
    free(bytes);
    return added && (source_id != R_SOURCE_ID_INVALID);
}

static bool r_fs_path_is_absolute_write(void *user_data, const char *bytes, size_t length) {
    RFsPathIsAbsoluteBuffer *buffer = user_data;
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

static void r_fs_path_is_absolute_dispose_buffer(RFsPathIsAbsoluteBuffer *buffer) {
    free(buffer->bytes);
    (void)memset(buffer, 0, sizeof(*buffer));
}

static bool r_fs_path_is_absolute_intern_equals(const RFrontendContext *context,
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

static bool r_fs_path_is_absolute_standard_type_equals(const RFrontendContext *context,
                                                       RTypeId type_id,
                                                       const char *name) {
    const RSemanticType *type = r_semantic_type(context, type_id);

    return (type != NULL) && (type->kind == R_SEMANTIC_TYPE_STANDARD) &&
           (type->flags == R_SEMANTIC_TYPE_FLAG_NONE) && (type->base == R_TYPE_ID_INVALID) &&
           (type->second == R_TYPE_ID_INVALID) && (type->length != UINT64_C(0)) &&
           (type->length <= (uint64_t)UINT32_MAX) &&
           r_fs_path_is_absolute_intern_equals(context, (uint32_t)type->length, name);
}

static RHirNode *r_fs_path_is_absolute_hir_child(RFrontendContext *context,
                                                 const RHirNode *node,
                                                 uint32_t child_index) {
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

static RMirInstruction *r_fs_path_is_absolute_function_value_definition(
    RFrontendContext *context, const RMirFunction *function, RMirValueId value) {
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

static bool r_fs_path_is_absolute_span_contains(RSourceSpan outer, RSourceSpan inner) {
    return (outer.source == inner.source) && (outer.start <= inner.start) &&
           (inner.end <= outer.end);
}

static RFrontendContext *r_fs_path_is_absolute_build_context(void) {
    RFrontendContext *context = r_frontend_create(NULL);

    if ((context == NULL) ||
        !r_fs_path_is_absolute_add_file(context, R_FS_PATH_IS_ABSOLUTE_SOURCE_PATH) ||
        (r_frontend_analyze(context) != R_FRONTEND_OK) ||
        (r_frontend_diagnostic_count(context) != 0U) ||
        (r_frontend_lower_mir(context) != R_FRONTEND_OK) ||
        (r_frontend_diagnostic_count(context) != 0U)) {
        r_frontend_destroy(context);
        return NULL;
    }
    return context;
}

static bool r_fs_path_is_absolute_find_view(RFrontendContext *context,
                                            RFsPathIsAbsoluteView *view) {
    RSourceSpan sync_span = {0};
    RSourceSpan async_span = {0};
    size_t async_hir_count = 0U;
    size_t async_mir_count = 0U;
    size_t index;

    (void)memset(view, 0, sizeof(*view));
    view->bool_type = R_TYPE_ID_INVALID;
    view->borrow_type = R_TYPE_ID_INVALID;
    view->path_type = R_TYPE_ID_INVALID;
    for (index = 0U; index < context->semantic_symbol_count; ++index) {
        const RSemanticSymbol *symbol = &context->semantic_symbols[index];
        const RHirNode *function;

        if ((symbol->kind != R_SEMANTIC_SYMBOL_FUNCTION) ||
            (symbol->hir_node == R_HIR_NODE_ID_INVALID) ||
            ((size_t)symbol->hir_node > context->hir_node_count)) {
            continue;
        }
        function = &context->hir_nodes[(size_t)symbol->hir_node - 1U];
        if (symbol->is_async) {
            async_span = function->span;
        } else if (r_fs_path_is_absolute_intern_equals(
                       context, symbol->name_intern_id, "observe")) {
            sync_span = function->span;
        }
    }
    if ((sync_span.source == R_SOURCE_ID_INVALID) || (async_span.source == R_SOURCE_ID_INVALID)) {
        return false;
    }
    for (index = 0U; index < context->hir_node_count; ++index) {
        RHirNode *node = &context->hir_nodes[index];
        RHirNode *argument;

        if ((node->kind != R_HIR_STANDARD_CALL) ||
            (node->standard_operation != R_STANDARD_CALL_FS_PATH_IS_ABSOLUTE)) {
            continue;
        }
        argument = r_fs_path_is_absolute_hir_child(context, node, UINT32_C(0));
        if (r_fs_path_is_absolute_span_contains(sync_span, node->span)) {
            if ((view->sync_hir_call != NULL) || (argument == NULL)) {
                return false;
            }
            view->sync_hir_call = node;
            view->sync_hir_argument = argument;
        } else if (r_fs_path_is_absolute_span_contains(async_span, node->span)) {
            if ((async_hir_count >= R_FS_PATH_IS_ABSOLUTE_ASYNC_CALL_COUNT) || (argument == NULL)) {
                return false;
            }
            view->async_hir_calls[async_hir_count] = node;
            view->async_hir_arguments[async_hir_count] = argument;
            async_hir_count += 1U;
        } else {
            return false;
        }
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
                RMirInstruction *argument;
                RMirValueId argument_value;

                if ((instruction->kind != R_MIR_INSTRUCTION_STANDARD_CALL) ||
                    (instruction->standard_operation != R_STANDARD_CALL_FS_PATH_IS_ABSOLUTE)) {
                    continue;
                }
                if ((instruction->operand_count != UINT32_C(1)) ||
                    ((size_t)instruction->first_operand >= context->mir_operand_count)) {
                    return false;
                }
                argument_value = context->mir_operands[(size_t)instruction->first_operand];
                argument = r_fs_path_is_absolute_function_value_definition(
                    context, function, argument_value);
                if (argument == NULL) {
                    return false;
                }
                if (symbol->is_async) {
                    if (async_mir_count >= R_FS_PATH_IS_ABSOLUTE_ASYNC_CALL_COUNT) {
                        return false;
                    }
                    view->async_mir_calls[async_mir_count] = instruction;
                    view->async_mir_arguments[async_mir_count] = argument;
                    async_mir_count += 1U;
                } else {
                    if (view->sync_mir_call != NULL) {
                        return false;
                    }
                    view->sync_mir_call = instruction;
                    view->sync_mir_argument = argument;
                }
            }
        }
    }
    if ((view->sync_hir_call == NULL) || (view->sync_hir_argument == NULL) ||
        (view->sync_mir_call == NULL) || (view->sync_mir_argument == NULL) ||
        (async_hir_count != R_FS_PATH_IS_ABSOLUTE_ASYNC_CALL_COUNT) ||
        (async_mir_count != R_FS_PATH_IS_ABSOLUTE_ASYNC_CALL_COUNT)) {
        return false;
    }
    view->bool_type = view->sync_hir_call->type;
    view->borrow_type = view->sync_hir_call->auxiliary_type;
    {
        const RSemanticType *borrow = r_semantic_type(context, view->borrow_type);

        if (borrow == NULL) {
            return false;
        }
        view->path_type = borrow->base;
    }
    return true;
}

static bool r_fs_path_is_absolute_view_is_valid(const RFrontendContext *context,
                                                const RFsPathIsAbsoluteView *view) {
    const RSemanticType *bool_type;
    const RSemanticType *borrow_type;
    size_t index;

    if ((view == NULL) || (view->sync_hir_call == NULL) || (view->sync_hir_argument == NULL) ||
        (view->sync_mir_call == NULL) || (view->sync_mir_argument == NULL)) {
        return false;
    }
    for (index = 0U; index < R_FS_PATH_IS_ABSOLUTE_ASYNC_CALL_COUNT; ++index) {
        if ((view->async_hir_calls[index] == NULL) || (view->async_hir_arguments[index] == NULL) ||
            (view->async_mir_calls[index] == NULL) || (view->async_mir_arguments[index] == NULL)) {
            return false;
        }
    }
    bool_type = r_semantic_type(context, view->bool_type);
    borrow_type = r_semantic_type(context, view->borrow_type);
    if ((bool_type == NULL) || (bool_type->kind != R_SEMANTIC_TYPE_BOOL) ||
        (bool_type->flags != R_SEMANTIC_TYPE_FLAG_NONE) || (borrow_type == NULL) ||
        (borrow_type->kind != R_SEMANTIC_TYPE_BORROW) ||
        (borrow_type->flags != R_SEMANTIC_TYPE_FLAG_SHARED) ||
        (borrow_type->base != view->path_type) ||
        !r_fs_path_is_absolute_standard_type_equals(context, view->path_type, "std.fs::path") ||
        (view->sync_hir_call->type != view->bool_type) ||
        (view->sync_hir_call->auxiliary_type != view->borrow_type) ||
        (view->sync_hir_call->child_count != UINT32_C(1)) ||
        (view->sync_hir_argument->type != view->borrow_type) ||
        (view->sync_hir_argument->kind != R_HIR_LOAD) || view->sync_hir_argument->is_move ||
        (view->sync_mir_call->type != view->bool_type) ||
        (view->sync_mir_call->auxiliary_type != view->borrow_type) ||
        (view->sync_mir_call->operand_count != UINT32_C(1)) ||
        (view->sync_mir_call->call_borrow_mask.low != UINT64_C(1)) ||
        (view->sync_mir_call->call_borrow_mask.high != UINT64_C(0)) ||
        (view->sync_mir_argument->type != view->borrow_type) ||
        (view->sync_mir_argument->kind != R_MIR_INSTRUCTION_LOAD) ||
        view->sync_mir_argument->is_async_staged_move) {
        return false;
    }
    for (index = 0U; index < R_FS_PATH_IS_ABSOLUTE_ASYNC_CALL_COUNT; ++index) {
        if ((view->async_hir_calls[index]->type != view->bool_type) ||
            (view->async_hir_calls[index]->auxiliary_type != view->borrow_type) ||
            (view->async_hir_calls[index]->child_count != UINT32_C(1)) ||
            (view->async_hir_arguments[index]->type != view->borrow_type) ||
            (view->async_hir_arguments[index]->kind != R_HIR_BORROW) ||
            view->async_hir_arguments[index]->is_move ||
            (view->async_mir_calls[index]->type != view->bool_type) ||
            (view->async_mir_calls[index]->auxiliary_type != view->borrow_type) ||
            (view->async_mir_calls[index]->operand_count != UINT32_C(1)) ||
            (view->async_mir_calls[index]->call_borrow_mask.low != UINT64_C(1)) ||
            (view->async_mir_calls[index]->call_borrow_mask.high != UINT64_C(0)) ||
            (view->async_mir_arguments[index]->type != view->borrow_type) ||
            (view->async_mir_arguments[index]->kind != R_MIR_INSTRUCTION_BORROW) ||
            view->async_mir_arguments[index]->is_async_staged_move) {
            return false;
        }
    }
    return true;
}

static void r_fs_path_is_absolute_test_positive(void) {
    RFrontendContext *context = r_fs_path_is_absolute_build_context();
    RFsPathIsAbsoluteView view;
    RFsPathIsAbsoluteBuffer first = {0};
    RFsPathIsAbsoluteBuffer second = {0};

    R_FS_PATH_IS_ABSOLUTE_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_FS_PATH_IS_ABSOLUTE_CHECK(r_fs_path_is_absolute_find_view(context, &view));
    R_FS_PATH_IS_ABSOLUTE_CHECK(r_fs_path_is_absolute_view_is_valid(context, &view));
    R_FS_PATH_IS_ABSOLUTE_CHECK(
        r_frontend_emit_llvm(
            context, NULL, R_FRONTEND_LLVM_IR, r_fs_path_is_absolute_write, &first) ==
        R_FRONTEND_OK);
    R_FS_PATH_IS_ABSOLUTE_CHECK(first.call_count == 1U);
    R_FS_PATH_IS_ABSOLUTE_CHECK(first.length != 0U);
    R_FS_PATH_IS_ABSOLUTE_CHECK(
        r_frontend_emit_llvm(
            context, NULL, R_FRONTEND_LLVM_IR, r_fs_path_is_absolute_write, &second) ==
        R_FRONTEND_OK);
    R_FS_PATH_IS_ABSOLUTE_CHECK(second.call_count == 1U);
    R_FS_PATH_IS_ABSOLUTE_CHECK(
        (first.length == second.length) &&
        ((first.length == 0U) || (memcmp(first.bytes, second.bytes, first.length) == 0)));
    r_fs_path_is_absolute_dispose_buffer(&second);
    r_fs_path_is_absolute_dispose_buffer(&first);
    r_frontend_destroy(context);
}

static void
r_fs_path_is_absolute_test_diagnostic(const RFsPathIsAbsoluteDiagnosticCase *test_case) {
    RFrontendContext *context = r_frontend_create(NULL);
    const RDiagnostic *diagnostic;

    R_FS_PATH_IS_ABSOLUTE_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_FS_PATH_IS_ABSOLUTE_CHECK(r_fs_path_is_absolute_add_file(context, test_case->path));
    R_FS_PATH_IS_ABSOLUTE_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
    R_FS_PATH_IS_ABSOLUTE_CHECK(r_frontend_diagnostic_count(context) == 1U);
    diagnostic = r_frontend_diagnostic(context, 0U);
    if ((diagnostic == NULL) || (strcmp(diagnostic->code, "R-DIAG-TYPE-001") != 0) ||
        (strcmp(diagnostic->rule_id, "R-SLIB-FS-0001") != 0) ||
        (strcmp(diagnostic->message, test_case->message) != 0) ||
        (diagnostic->severity != R_DIAGNOSTIC_ERROR) ||
        (diagnostic->phase != R_DIAGNOSTIC_PHASE_SEMANTIC)) {
        (void)fprintf(stderr,
                      "%s: expected R-DIAG-TYPE-001/R-SLIB-FS-0001 `%s`, observed "
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
    static const RFsPathIsAbsoluteDiagnosticCase cases[] = {
        {R_FS_PATH_IS_ABSOLUTE_ARITY_PATH,
         "std.fs::path_is_absolute requires exactly one shared std.fs::path borrow"},
        {R_FS_PATH_IS_ABSOLUTE_ARGUMENT_TYPE_PATH,
         "std.fs::path_is_absolute argument shall have type const std.fs::path*"},
        {R_FS_PATH_IS_ABSOLUTE_RESULT_TYPE_PATH,
         "std.fs::path_is_absolute result shall have type bool"},
    };
    size_t index;

    r_fs_path_is_absolute_test_positive();
    for (index = 0U; index < (sizeof(cases) / sizeof(cases[0])); ++index) {
        r_fs_path_is_absolute_test_diagnostic(&cases[index]);
    }
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

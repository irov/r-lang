#include "frontend_internal.h"

#include <errno.h>
#include <fenv.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef R_SEMANTIC_ALPHA_PATH
#error R_SEMANTIC_ALPHA_PATH is required
#endif
#ifndef R_SEMANTIC_BETA_PATH
#error R_SEMANTIC_BETA_PATH is required
#endif
#ifndef R_SEMANTIC_GOLDEN_PATH
#error R_SEMANTIC_GOLDEN_PATH is required
#endif
#ifndef R_SEMANTIC_BODY_PATH
#error R_SEMANTIC_BODY_PATH is required
#endif
#ifndef R_SEMANTIC_BODY_GOLDEN_PATH
#error R_SEMANTIC_BODY_GOLDEN_PATH is required
#endif
#ifndef R_SEMANTIC_MOVE_PATH
#error R_SEMANTIC_MOVE_PATH is required
#endif
#ifndef R_SEMANTIC_MOVE_NEGATIVE_PATH
#error R_SEMANTIC_MOVE_NEGATIVE_PATH is required
#endif
#ifndef R_SEMANTIC_ASYNC_COPY_ARGS_PATH
#error R_SEMANTIC_ASYNC_COPY_ARGS_PATH is required
#endif
#ifndef R_SEMANTIC_SHARED_OWNER_CLONE_PATH
#error R_SEMANTIC_SHARED_OWNER_CLONE_PATH is required
#endif
#ifndef R_SEMANTIC_STANDARD_OUTCOMES_PATH
#error R_SEMANTIC_STANDARD_OUTCOMES_PATH is required
#endif
#ifndef R_SEMANTIC_STANDARD_OUTCOMES_WRONG_VARIANT_PATH
#error R_SEMANTIC_STANDARD_OUTCOMES_WRONG_VARIANT_PATH is required
#endif
#ifndef R_SEMANTIC_STANDARD_OUTCOMES_MISSING_MOVE_PATH
#error R_SEMANTIC_STANDARD_OUTCOMES_MISSING_MOVE_PATH is required
#endif

typedef struct RSemanticTestBuffer {
    char *bytes;
    size_t length;
    size_t capacity;
} RSemanticTestBuffer;

typedef struct RSemanticTestAllocator {
    size_t call_count;
    size_t fail_at;
    size_t live_count;
} RSemanticTestAllocator;

static int failures = 0;

#define R_SEMANTIC_CHECK(condition)                                                                \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            (void)fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);    \
            failures += 1;                                                                         \
        }                                                                                          \
    } while (false)

static void *r_semantic_test_allocate(void *user_data, size_t size) {
    RSemanticTestAllocator *allocator = user_data;
    void *pointer;

    allocator->call_count += 1U;
    if ((allocator->fail_at != 0U) && (allocator->call_count == allocator->fail_at)) {
        return NULL;
    }
    pointer = malloc(size);
    if (pointer != NULL) {
        allocator->live_count += 1U;
    }
    return pointer;
}

static void r_semantic_test_free(void *user_data, void *pointer) {
    RSemanticTestAllocator *allocator = user_data;
    if (pointer != NULL) {
        R_SEMANTIC_CHECK(allocator->live_count != 0U);
        if (allocator->live_count != 0U) {
            allocator->live_count -= 1U;
        }
        free(pointer);
    }
}

static bool r_semantic_test_write(void *user_data, const char *bytes, size_t length) {
    RSemanticTestBuffer *buffer = user_data;
    size_t required;
    char *replacement;
    size_t capacity;

    if (length > (SIZE_MAX - buffer->length - 1U)) {
        return false;
    }
    required = buffer->length + length + 1U;
    if (required > buffer->capacity) {
        capacity = buffer->capacity == 0U ? 256U : buffer->capacity;
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

static bool r_semantic_test_buffers_equal(const RSemanticTestBuffer *left,
                                          const RSemanticTestBuffer *right) {
    if (left->length != right->length) {
        return false;
    }
    if (left->length == 0U) {
        return true;
    }
    if ((left->bytes == NULL) || (right->bytes == NULL)) {
        return false;
    }
    return memcmp(left->bytes, right->bytes, left->length) == 0;
}

static bool r_semantic_read_file(const char *path, uint8_t **bytes, size_t *length) {
    FILE *file;
    long file_size;
    uint8_t *result;

    *bytes = NULL;
    *length = 0U;
    file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    if ((fseek(file, 0L, SEEK_END) != 0) || ((file_size = ftell(file)) < 0L) ||
        (fseek(file, 0L, SEEK_SET) != 0)) {
        (void)fclose(file);
        return false;
    }
    result = malloc((size_t)file_size + 1U);
    if (result == NULL) {
        (void)fclose(file);
        return false;
    }
    if (((size_t)file_size != 0U) &&
        (fread(result, 1U, (size_t)file_size, file) != (size_t)file_size)) {
        free(result);
        (void)fclose(file);
        return false;
    }
    if (fclose(file) != 0) {
        free(result);
        return false;
    }
    result[(size_t)file_size] = UINT8_C(0);
    *bytes = result;
    *length = (size_t)file_size;
    return true;
}

static RSourceId r_semantic_add_bytes(RFrontendContext *context,
                                      const char *name,
                                      const uint8_t *bytes,
                                      size_t length) {
    RSourceId source_id = R_SOURCE_ID_INVALID;
    R_SEMANTIC_CHECK(r_frontend_add_source(context, name, bytes, length, &source_id) ==
                     R_FRONTEND_OK);
    return source_id;
}

static bool r_semantic_has_diagnostic(const RFrontendContext *context,
                                      const char *code,
                                      RDiagnosticPhase phase) {
    size_t index;
    for (index = 0U; index < r_frontend_diagnostic_count(context); ++index) {
        const RDiagnostic *diagnostic = r_frontend_diagnostic(context, index);
        if ((diagnostic != NULL) && (strcmp(diagnostic->code, code) == 0) &&
            (diagnostic->phase == phase)) {
            return true;
        }
    }
    return false;
}

static size_t r_semantic_diagnostic_code_count(const RFrontendContext *context, const char *code) {
    size_t count = 0U;
    size_t index;
    for (index = 0U; index < r_frontend_diagnostic_count(context); ++index) {
        const RDiagnostic *diagnostic = r_frontend_diagnostic(context, index);
        if ((diagnostic != NULL) && (strcmp(diagnostic->code, code) == 0)) {
            count += 1U;
        }
    }
    return count;
}

static void r_semantic_test_float_literals(void) {
    static const char positive_source[] = "module semantic.float_literals;\n"
                                          "f32 a() { return 1.000000059604644775390625f32; }\n"
                                          "f32 b() { return 1.0000000596046447753906251f32; }\n"
                                          "f32 c() { return 0x1p-149f32; }\n"
                                          "f32 d() { return -1e-100000f32; }\n"
                                          "f64 e() { return 0x1.0000000000001p+0f64; }\n";
    static const uint64_t expected_bits[] = {
        UINT64_C(0x3f800000),
        UINT64_C(0x3f800001),
        UINT64_C(0x00000001),
        UINT64_C(0x80000000),
        UINT64_C(0x3ff0000000000001),
    };
    static const char *negative_sources[] = {
        "module semantic.float_decimal_f32_overflow;\n"
        "f32 value() { return 340282346638528859811704183484516925440.1f32; }\n",
        "module semantic.float_decimal_f64_overflow;\n"
        "f64 value() { return "
        "179769313486231570814527423731704356798070567525844996598917476803157260780"
        "028538760589558632766878171540458953514382464234321326889464182768467546703"
        "537516986049910576551282076245490090389328944075868508455133942304583236903"
        "222948165808559332123348274797826204144723168738177180919299881250404026184"
        "124858368.1f64; }\n",
        "module semantic.float_hex_f32_overflow;\n"
        "f32 value() { return 0x1.ffffffp+127f32; }\n",
        "module semantic.float_hex_f64_overflow;\n"
        "f64 value() { return 0x1.fffffffffffff1p+1023f64; }\n",
        "module semantic.float_exponent_overflow;\n"
        "f32 value() { return 1e100000f32; }\n",
    };
    RFrontendContext *context = r_frontend_create(NULL);
    fenv_t saved_environment;
    const int saved_errno = errno;
    size_t literal_index = 0U;
    size_t index;

    R_SEMANTIC_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    if (fegetenv(&saved_environment) != 0) {
        R_SEMANTIC_CHECK(false);
        r_frontend_destroy(context);
        return;
    }
    R_SEMANTIC_CHECK(fesetround(FE_DOWNWARD) == 0);
    R_SEMANTIC_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    R_SEMANTIC_CHECK(feraiseexcept(FE_INVALID) == 0);
    errno = EDOM;
    (void)r_semantic_add_bytes(
        context, "float-literals.r", (const uint8_t *)positive_source, strlen(positive_source));
    R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    R_SEMANTIC_CHECK(errno == EDOM);
    R_SEMANTIC_CHECK(fegetround() == FE_DOWNWARD);
    R_SEMANTIC_CHECK((fetestexcept(FE_ALL_EXCEPT) & FE_INVALID) != 0);
    for (index = 0U; index < context->hir_node_count; ++index) {
        const RHirNode *node = &context->hir_nodes[index];
        const RSemanticType *type = r_semantic_type(context, node->type);

        if ((node->kind != R_HIR_LITERAL) || (type == NULL) ||
            ((type->kind != R_SEMANTIC_TYPE_F32) && (type->kind != R_SEMANTIC_TYPE_F64))) {
            continue;
        }
        R_SEMANTIC_CHECK(literal_index < (sizeof(expected_bits) / sizeof(expected_bits[0])));
        if (literal_index < (sizeof(expected_bits) / sizeof(expected_bits[0]))) {
            R_SEMANTIC_CHECK(node->integer_value == expected_bits[literal_index]);
        }
        literal_index += 1U;
    }
    R_SEMANTIC_CHECK(literal_index == (sizeof(expected_bits) / sizeof(expected_bits[0])));
    R_SEMANTIC_CHECK(fesetenv(&saved_environment) == 0);
    errno = saved_errno;
    r_frontend_destroy(context);

    for (index = 0U; index < (sizeof(negative_sources) / sizeof(negative_sources[0])); ++index) {
        const RDiagnostic *diagnostic;

        context = r_frontend_create(NULL);
        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        (void)r_semantic_add_bytes(context,
                                   "float-overflow.r",
                                   (const uint8_t *)negative_sources[index],
                                   strlen(negative_sources[index]));
        R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
        R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 1U);
        diagnostic = r_frontend_diagnostic(context, UINT32_C(0));
        R_SEMANTIC_CHECK((diagnostic != NULL) &&
                         (strcmp(diagnostic->code, "R-DIAG-CONST-001") == 0) &&
                         (strcmp(diagnostic->rule_id, "R-LEX-0011") == 0) &&
                         (diagnostic->phase == R_DIAGNOSTIC_PHASE_SEMANTIC));
        r_frontend_destroy(context);
    }
}

static bool r_semantic_test_standard_type_named(const RFrontendContext *context,
                                                const RSemanticType *type,
                                                const char *name) {
    const RInternEntry *entry;
    const size_t name_length = strlen(name);

    if ((type == NULL) || (type->kind != R_SEMANTIC_TYPE_STANDARD) ||
        (type->length == UINT64_C(0)) || (type->length > (uint64_t)context->intern_count)) {
        return false;
    }
    entry = &context->intern_entries[(size_t)type->length - 1U];
    return (entry->length == name_length) && (memcmp(entry->bytes, name, name_length) == 0);
}

static bool r_semantic_test_intern_named(const RFrontendContext *context,
                                         uint32_t intern_id,
                                         const char *name) {
    const RInternEntry *entry;
    const size_t name_length = strlen(name);

    if ((intern_id == 0U) || ((size_t)intern_id > context->intern_count)) {
        return false;
    }
    entry = &context->intern_entries[(size_t)intern_id - 1U];
    return (entry->length == name_length) && (memcmp(entry->bytes, name, name_length) == 0);
}

static bool r_semantic_test_symbol_named(const RFrontendContext *context,
                                         RSymbolId symbol_id,
                                         const char *name) {
    const RSemanticSymbol *symbol;
    const RInternEntry *entry;
    const size_t name_length = strlen(name);

    if ((symbol_id == R_SYMBOL_ID_INVALID) ||
        ((size_t)symbol_id > context->semantic_symbol_count)) {
        return false;
    }
    symbol = &context->semantic_symbols[(size_t)symbol_id - 1U];
    if ((symbol->name_intern_id == 0U) ||
        ((size_t)symbol->name_intern_id > context->intern_count)) {
        return false;
    }
    entry = &context->intern_entries[(size_t)symbol->name_intern_id - 1U];
    return (entry->length == name_length) && (memcmp(entry->bytes, name, name_length) == 0);
}

static const RHirNode *r_semantic_test_hir_child(const RFrontendContext *context,
                                                 const RHirNode *node,
                                                 uint32_t child_index) {
    RHirNodeId child_id;

    if ((node == NULL) || (child_index >= node->child_count) ||
        ((size_t)node->first_child + (size_t)child_index >= context->hir_child_count)) {
        return NULL;
    }
    child_id = context->hir_children[(size_t)node->first_child + (size_t)child_index];
    if ((child_id == R_HIR_NODE_ID_INVALID) || ((size_t)child_id > context->hir_node_count)) {
        return NULL;
    }
    return &context->hir_nodes[(size_t)child_id - 1U];
}

static const RMirBlock *r_semantic_test_mir_target_block(const RFrontendContext *context,
                                                         const RMirFunction *function,
                                                         RMirBlockId target) {
    size_t block_index;

    if ((target == R_MIR_BLOCK_ID_INVALID) || (target > function->block_count)) {
        return NULL;
    }
    block_index = (size_t)function->first_block + (size_t)target - 1U;
    if (block_index >= context->mir_block_count) {
        return NULL;
    }
    return &context->mir_blocks[block_index];
}

static const RMirInstruction *r_semantic_test_mir_block_instruction(const RFrontendContext *context,
                                                                    const RMirBlock *block,
                                                                    uint32_t instruction_index) {
    size_t global_index;

    if ((block == NULL) || (instruction_index >= block->instruction_count)) {
        return NULL;
    }
    global_index = (size_t)block->first_instruction + (size_t)instruction_index;
    if (global_index >= context->mir_instruction_count) {
        return NULL;
    }
    return &context->mir_instructions[global_index];
}

static bool r_semantic_test_mir_cleanup_segment(const RFrontendContext *context,
                                                const RMirBlock *block,
                                                uint32_t first_instruction,
                                                const char *drop_symbol,
                                                RMirInstructionKind terminal_kind) {
    size_t drop_count = 0U;
    size_t terminal_count = 0U;
    bool expected_drop_seen = false;
    bool terminal_follows_drop = false;
    uint32_t instruction_index;

    for (instruction_index = first_instruction;
         (block != NULL) && (instruction_index < block->instruction_count);
         ++instruction_index) {
        const RMirInstruction *instruction =
            r_semantic_test_mir_block_instruction(context, block, instruction_index);

        if (instruction == NULL) {
            return false;
        }
        if (instruction->kind == R_MIR_INSTRUCTION_DROP) {
            drop_count += 1U;
            expected_drop_seen =
                expected_drop_seen ||
                r_semantic_test_symbol_named(context, instruction->symbol, drop_symbol);
        }
        if (instruction->kind == terminal_kind) {
            terminal_count += 1U;
            terminal_follows_drop = terminal_follows_drop || expected_drop_seen;
        }
    }
    return (drop_count == 1U) && expected_drop_seen && (terminal_count == 1U) &&
           terminal_follows_drop;
}

static void r_semantic_test_atomic_canonicalization(void) {
    static const RTokenKind shorthand[] = {R_TOKEN_KW_AI8,
                                           R_TOKEN_KW_AI16,
                                           R_TOKEN_KW_AI32,
                                           R_TOKEN_KW_AI64,
                                           R_TOKEN_KW_AISIZE,
                                           R_TOKEN_KW_AU8,
                                           R_TOKEN_KW_AU16,
                                           R_TOKEN_KW_AU32,
                                           R_TOKEN_KW_AU64,
                                           R_TOKEN_KW_AUSIZE};
    static const RTokenKind base[] = {R_TOKEN_KW_I8,
                                      R_TOKEN_KW_I16,
                                      R_TOKEN_KW_I32,
                                      R_TOKEN_KW_I64,
                                      R_TOKEN_KW_ISIZE,
                                      R_TOKEN_KW_U8,
                                      R_TOKEN_KW_U16,
                                      R_TOKEN_KW_U32,
                                      R_TOKEN_KW_U64,
                                      R_TOKEN_KW_USIZE};
    RFrontendContext *context = r_frontend_create(NULL);
    size_t index;

    R_SEMANTIC_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    for (index = 0U; index < (sizeof(shorthand) / sizeof(shorthand[0])); ++index) {
        RTypeId shorthand_type = R_TYPE_ID_INVALID;
        RTypeId repeated_type = R_TYPE_ID_INVALID;
        RTypeId base_type = R_TYPE_ID_INVALID;
        const RSemanticType *description;
        size_t match_count = 0U;
        size_t type_index;

        R_SEMANTIC_CHECK(r_semantic_type_from_token(context, base[index], &base_type));
        R_SEMANTIC_CHECK(r_semantic_type_from_token(context, shorthand[index], &shorthand_type));
        R_SEMANTIC_CHECK(r_semantic_type_from_token(context, shorthand[index], &repeated_type));
        R_SEMANTIC_CHECK(shorthand_type == repeated_type);
        description = r_semantic_type(context, shorthand_type);
        R_SEMANTIC_CHECK((description != NULL) && (description->kind == R_SEMANTIC_TYPE_ATOMIC) &&
                         (description->base == base_type));
        for (type_index = 0U; type_index < context->semantic_type_count; ++type_index) {
            const RSemanticType *candidate = &context->semantic_types[type_index];
            if ((candidate->kind == R_SEMANTIC_TYPE_ATOMIC) && (candidate->base == base_type)) {
                match_count += 1U;
            }
        }
        R_SEMANTIC_CHECK(match_count == 1U);
    }
    r_frontend_destroy(context);
}

static void r_semantic_test_golden(void) {
    uint8_t *alpha = NULL;
    uint8_t *beta = NULL;
    uint8_t *golden = NULL;
    size_t alpha_length = 0U;
    size_t beta_length = 0U;
    size_t golden_length = 0U;
    RFrontendContext *context;
    RSemanticTestBuffer first = {0};
    RSemanticTestBuffer second = {0};
    RSourceId alpha_id;
    RSourceId beta_id;
    RSourceId rejected = R_SOURCE_ID_INVALID;
    static const uint8_t late_source[] = "module too.late;\n";

    R_SEMANTIC_CHECK(r_semantic_read_file(R_SEMANTIC_ALPHA_PATH, &alpha, &alpha_length));
    R_SEMANTIC_CHECK(r_semantic_read_file(R_SEMANTIC_BETA_PATH, &beta, &beta_length));
    R_SEMANTIC_CHECK(r_semantic_read_file(R_SEMANTIC_GOLDEN_PATH, &golden, &golden_length));
    if ((alpha == NULL) || (beta == NULL) || (golden == NULL)) {
        free(alpha);
        free(beta);
        free(golden);
        return;
    }
    context = r_frontend_create(NULL);
    R_SEMANTIC_CHECK(context != NULL);
    if (context == NULL) {
        free(alpha);
        free(beta);
        free(golden);
        return;
    }
    alpha_id = r_semantic_add_bytes(context, "semantic_alpha.r", alpha, alpha_length);
    beta_id = r_semantic_add_bytes(context, "semantic_beta.r", beta, beta_length);
    R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_hir_root(context) != R_HIR_NODE_ID_INVALID);
    R_SEMANTIC_CHECK(r_frontend_dump_hir(context, r_semantic_test_write, &first) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_dump_hir(context, r_semantic_test_write, &second) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_semantic_test_buffers_equal(&first, &second));
    R_SEMANTIC_CHECK((first.length == golden_length) &&
                     (memcmp(first.bytes, golden, golden_length) == 0));
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    R_SEMANTIC_CHECK(context->sources[(size_t)alpha_id - 1U].semantically_analyzed);
    R_SEMANTIC_CHECK(context->sources[(size_t)beta_id - 1U].semantically_analyzed);
    R_SEMANTIC_CHECK(r_frontend_add_source(
                         context, "late.r", late_source, sizeof(late_source) - 1U, &rejected) ==
                     R_FRONTEND_CONTEXT_SEALED);
    R_SEMANTIC_CHECK(rejected == R_SOURCE_ID_INVALID);
    r_frontend_destroy(context);
    free(alpha);
    free(beta);
    free(golden);
    free(first.bytes);
    free(second.bytes);
}

static void r_semantic_test_body_golden(void) {
    uint8_t *source = NULL;
    uint8_t *golden = NULL;
    size_t source_length = 0U;
    size_t golden_length = 0U;
    RFrontendContext *context;
    RSemanticTestBuffer dump = {0};
    RSourceId source_id;

    R_SEMANTIC_CHECK(r_semantic_read_file(R_SEMANTIC_BODY_PATH, &source, &source_length));
    R_SEMANTIC_CHECK(r_semantic_read_file(R_SEMANTIC_BODY_GOLDEN_PATH, &golden, &golden_length));
    if ((source == NULL) || (golden == NULL)) {
        free(source);
        free(golden);
        return;
    }
    context = r_frontend_create(NULL);
    R_SEMANTIC_CHECK(context != NULL);
    if (context == NULL) {
        free(source);
        free(golden);
        return;
    }
    source_id = r_semantic_add_bytes(context, "semantic_body.r", source, source_length);
    (void)source_id;
    R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    R_SEMANTIC_CHECK(r_frontend_dump_hir(context, r_semantic_test_write, &dump) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK((dump.length == golden_length) &&
                     (memcmp(dump.bytes, golden, golden_length) == 0));
    r_frontend_destroy(context);
    free(source);
    free(golden);
    free(dump.bytes);
}

static void r_semantic_test_diagnostics_and_terminal_status(void) {
    static const char body_source[] = "module semantic.body;\n"
                                      "protected i32 value() { return 1; }\n";
    static const char duplicate_source[] = "module semantic.duplicate;\n"
                                           "protected i32 value(i32 input);\n"
                                           "protected i32 value(i32 input);\n";
    static const char syntax_source[] = "module semantic.syntax;\n"
                                        "protected i32 broken( { return 0; }\n";
    static const char unresolved_module_source[] = "protected i32 declaration();\n";
    const char *sources[] = {duplicate_source};
    const char *codes[] = {"R-DIAG-NAME-002"};
    size_t index;

    for (index = 0U; index < (sizeof(sources) / sizeof(sources[0])); ++index) {
        RFrontendContext *context = r_frontend_create(NULL);
        RSourceId source_id;
        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        source_id = r_semantic_add_bytes(context,
                                         "semantic-negative.r",
                                         (const uint8_t *)sources[index],
                                         strlen(sources[index]));
        (void)source_id;
        R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
        R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
        R_SEMANTIC_CHECK(
            r_semantic_has_diagnostic(context, codes[index], R_DIAGNOSTIC_PHASE_SEMANTIC));
        R_SEMANTIC_CHECK(r_frontend_hir_root(context) != R_HIR_NODE_ID_INVALID);
        r_frontend_destroy(context);
    }

    {
        RFrontendContext *context = r_frontend_create(NULL);
        RSourceId source_id;
        R_SEMANTIC_CHECK(context != NULL);
        if (context != NULL) {
            source_id = r_semantic_add_bytes(
                context, "semantic-body.r", (const uint8_t *)body_source, strlen(body_source));
            (void)source_id;
            R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
            R_SEMANTIC_CHECK(!r_semantic_has_diagnostic(
                context, "R-DIAG-SLICE-001", R_DIAGNOSTIC_PHASE_SEMANTIC));
            r_frontend_destroy(context);
        }
    }

    {
        RFrontendContext *context = r_frontend_create(NULL);
        RSourceId source_id;
        R_SEMANTIC_CHECK(context != NULL);
        if (context != NULL) {
            source_id = r_semantic_add_bytes(
                context, "syntax-invalid.r", (const uint8_t *)syntax_source, strlen(syntax_source));
            (void)source_id;
            R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
            R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
            R_SEMANTIC_CHECK(r_frontend_hir_root(context) == R_HIR_NODE_ID_INVALID);
            r_frontend_destroy(context);
        }
    }

    {
        RFrontendContext *context = r_frontend_create(NULL);
        RSourceId source_id;
        RSemanticTestBuffer dump = {0};
        R_SEMANTIC_CHECK(context != NULL);
        if (context != NULL) {
            source_id = r_semantic_add_bytes(context,
                                             "unresolved-module.r",
                                             (const uint8_t *)unresolved_module_source,
                                             strlen(unresolved_module_source));
            (void)source_id;
            R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
            R_SEMANTIC_CHECK(
                r_semantic_has_diagnostic(context, "R-DIAG-MOD-001", R_DIAGNOSTIC_PHASE_SEMANTIC));
            R_SEMANTIC_CHECK(r_frontend_dump_hir(context, r_semantic_test_write, &dump) ==
                             R_FRONTEND_OK);
            R_SEMANTIC_CHECK((dump.bytes != NULL) && (strstr(dump.bytes, "<unresolved>") != NULL));
            free(dump.bytes);
            r_frontend_destroy(context);
        }
    }
}

static void r_semantic_test_atomic_signature_compatibility(void) {
    static const char source_text[] =
        "module semantic.atomic_compatibility;\n"
        "ai8 exchange_ai8(ai8 value);\n"
        "atomic i8 exchange_ai8(atomic i8 value) { return move value; }\n"
        "ai16 exchange_ai16(ai16 value);\n"
        "atomic i16 exchange_ai16(atomic i16 value) { return move value; }\n"
        "ai32 exchange_ai32(ai32 value);\n"
        "atomic i32 exchange_ai32(atomic i32 value) { return move value; }\n"
        "ai64 exchange_ai64(ai64 value);\n"
        "atomic i64 exchange_ai64(atomic i64 value) { return move value; }\n"
        "aisize exchange_aisize(aisize value);\n"
        "atomic isize exchange_aisize(atomic isize value) { return move value; }\n"
        "au8 exchange_au8(au8 value);\n"
        "atomic u8 exchange_au8(atomic u8 value) { return move value; }\n"
        "au16 exchange_au16(au16 value);\n"
        "atomic u16 exchange_au16(atomic u16 value) { return move value; }\n"
        "au32 exchange_au32(au32 value);\n"
        "atomic u32 exchange_au32(atomic u32 value) { return move value; }\n"
        "au64 exchange_au64(au64 value);\n"
        "atomic u64 exchange_au64(atomic u64 value) { return move value; }\n"
        "ausize exchange_ausize(ausize value);\n"
        "atomic usize exchange_ausize(atomic usize value) { return move value; }\n";
    static const RSemanticTypeKind expected_bases[] = {R_SEMANTIC_TYPE_I8,
                                                       R_SEMANTIC_TYPE_I16,
                                                       R_SEMANTIC_TYPE_I32,
                                                       R_SEMANTIC_TYPE_I64,
                                                       R_SEMANTIC_TYPE_ISIZE,
                                                       R_SEMANTIC_TYPE_U8,
                                                       R_SEMANTIC_TYPE_U16,
                                                       R_SEMANTIC_TYPE_U32,
                                                       R_SEMANTIC_TYPE_U64,
                                                       R_SEMANTIC_TYPE_USIZE};
    bool seen[sizeof(expected_bases) / sizeof(expected_bases[0])] = {false};
    RFrontendContext *context = r_frontend_create(NULL);
    RSourceId source_id;
    size_t function_count = 0U;
    size_t symbol_index;

    R_SEMANTIC_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    source_id = r_semantic_add_bytes(
        context, "atomic-compatibility.r", (const uint8_t *)source_text, strlen(source_text));
    (void)source_id;
    R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    R_SEMANTIC_CHECK(
        !r_semantic_has_diagnostic(context, "R-DIAG-NAME-002", R_DIAGNOSTIC_PHASE_SEMANTIC));
    for (symbol_index = 0U; symbol_index < context->semantic_symbol_count; ++symbol_index) {
        const RSemanticSymbol *symbol = &context->semantic_symbols[symbol_index];
        if (symbol->kind == R_SEMANTIC_SYMBOL_FUNCTION) {
            const RSemanticType *return_type = r_semantic_type(context, symbol->return_type);
            const RSemanticType *base_type =
                return_type == NULL ? NULL : r_semantic_type(context, return_type->base);
            const RTypeId parameter_type =
                context->semantic_parameter_types[(size_t)symbol->first_parameter_type];
            bool matched = false;
            size_t expected_index;
            R_SEMANTIC_CHECK((return_type != NULL) &&
                             (return_type->kind == R_SEMANTIC_TYPE_ATOMIC));
            for (expected_index = 0U;
                 expected_index < (sizeof(expected_bases) / sizeof(expected_bases[0]));
                 ++expected_index) {
                if ((base_type != NULL) && (base_type->kind == expected_bases[expected_index])) {
                    R_SEMANTIC_CHECK(!seen[expected_index]);
                    seen[expected_index] = true;
                    matched = true;
                    break;
                }
            }
            R_SEMANTIC_CHECK(matched);
            R_SEMANTIC_CHECK(parameter_type == symbol->return_type);
            R_SEMANTIC_CHECK(symbol->has_prototype);
            R_SEMANTIC_CHECK(symbol->has_definition);
            function_count += 1U;
        }
    }
    R_SEMANTIC_CHECK(function_count == (sizeof(expected_bases) / sizeof(expected_bases[0])));
    for (symbol_index = 0U; symbol_index < (sizeof(seen) / sizeof(seen[0])); ++symbol_index) {
        R_SEMANTIC_CHECK(seen[symbol_index]);
    }
    r_frontend_destroy(context);
}

static void r_semantic_test_symbol_growth(void) {
    static const char source_text[] = "module semantic.growth;\n"
                                      "i32 f00(i32 value);\n"
                                      "i32 f01(i32 value);\n"
                                      "i32 f02(i32 value);\n"
                                      "i32 f03(i32 value);\n"
                                      "i32 f04(i32 value);\n"
                                      "i32 f05(i32 value);\n"
                                      "i32 f06(i32 value);\n"
                                      "i32 f07(i32 value);\n"
                                      "i32 f08(i32 value);\n"
                                      "i32 f09(i32 value);\n"
                                      "i32 f10(i32 value);\n"
                                      "i32 f11(i32 value);\n"
                                      "i32 f12(i32 value);\n"
                                      "i32 f13(i32 value);\n"
                                      "i32 f14(i32 value);\n"
                                      "i32 f15(i32 value);\n";
    RFrontendContext *context = r_frontend_create(NULL);
    RSourceId source_id;

    R_SEMANTIC_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    source_id = r_semantic_add_bytes(
        context, "semantic-growth.r", (const uint8_t *)source_text, strlen(source_text));
    (void)source_id;
    R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(context->semantic_symbol_count == 32U);
    R_SEMANTIC_CHECK(r_frontend_hir_root(context) != R_HIR_NODE_ID_INVALID);
    r_frontend_destroy(context);
}

static void r_semantic_test_signature_names(void) {
    static const char source_text[] = "module semantic.names;\n"
                                      "i32 len();\n"
                                      "i32 duplicate(i32 value, i32 value);\n"
                                      "i32 duplicate(i32 left, i32 right) { return left; }\n";
    RFrontendContext *context = r_frontend_create(NULL);
    RSourceId source_id;

    R_SEMANTIC_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    source_id = r_semantic_add_bytes(
        context, "semantic-names.r", (const uint8_t *)source_text, strlen(source_text));
    (void)source_id;
    R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
    R_SEMANTIC_CHECK(
        r_semantic_has_diagnostic(context, "R-DIAG-NAME-002", R_DIAGNOSTIC_PHASE_SEMANTIC));
    R_SEMANTIC_CHECK(
        r_semantic_has_diagnostic(context, "R-DIAG-NAME-003", R_DIAGNOSTIC_PHASE_SEMANTIC));
    R_SEMANTIC_CHECK(r_semantic_diagnostic_code_count(context, "R-DIAG-NAME-002") == 1U);
    R_SEMANTIC_CHECK(r_frontend_hir_root(context) != R_HIR_NODE_ID_INVALID);
    r_frontend_destroy(context);
}

static void r_semantic_test_modifier_compatibility(void) {
    static const char source_text[] = "module semantic.modifiers;\n"
                                      "unsafe i32 operation();\n"
                                      "async i32 operation() { return 0; }\n";
    RFrontendContext *context = r_frontend_create(NULL);
    RSourceId source_id;

    R_SEMANTIC_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    source_id = r_semantic_add_bytes(
        context, "semantic-modifiers.r", (const uint8_t *)source_text, strlen(source_text));
    (void)source_id;
    R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
    R_SEMANTIC_CHECK(
        r_semantic_has_diagnostic(context, "R-DIAG-NAME-002", R_DIAGNOSTIC_PHASE_SEMANTIC));
    r_frontend_destroy(context);
}

static void r_semantic_test_checked_error_type(void) {
    static const char source_text[] = "module semantic.checked_error_type;\n"
                                      "void invalid() throws std.fs::path {}\n";
    RFrontendContext *context = r_frontend_create(NULL);
    const RDiagnostic *diagnostic;
    RSourceId source_id;

    R_SEMANTIC_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    source_id = r_semantic_add_bytes(context,
                                     "semantic-checked-error-type.r",
                                     (const uint8_t *)source_text,
                                     strlen(source_text));
    (void)source_id;
    R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 1U);
    diagnostic = r_frontend_diagnostic(context, UINT32_C(0));
    R_SEMANTIC_CHECK((diagnostic != NULL) && (strcmp(diagnostic->code, "R-DIAG-EFFECT-002") == 0) &&
                     (strcmp(diagnostic->rule_id, "R-TYPE-0012") == 0) &&
                     (diagnostic->phase == R_DIAGNOSTIC_PHASE_SEMANTIC));
    r_frontend_destroy(context);
}

static void r_semantic_test_unheaded_throw_error_set_arity(void) {
    static const char positive_source[] = "module semantic.unheaded_throw_single_error;\n"
                                          "error only_error { i32 code; };\n"
                                          "void fail() throws only_error { throw {.code = 7}; }\n";
    static const char negative_source[] =
        "module semantic.unheaded_throw_multiple_errors;\n"
        "error alpha_error { i32 alpha; };\n"
        "error beta_error { bool beta; };\n"
        "void fail() throws alpha_error, beta_error { throw {.alpha = 7}; }\n";
    RFrontendContext *context = r_frontend_create(NULL);
    RSourceId source_id = R_SOURCE_ID_INVALID;
    size_t throw_count = 0U;
    size_t index;

    R_SEMANTIC_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    source_id = r_semantic_add_bytes(context,
                                     "unheaded-throw-single-error.r",
                                     (const uint8_t *)positive_source,
                                     strlen(positive_source));
    (void)source_id;
    R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    for (index = 0U; index < context->hir_node_count; ++index) {
        if (context->hir_nodes[index].kind == R_HIR_THROW) {
            const RSemanticType *error_type =
                r_semantic_type(context, context->hir_nodes[index].auxiliary_type);

            R_SEMANTIC_CHECK((error_type != NULL) && (error_type->kind == R_SEMANTIC_TYPE_STRUCT));
            throw_count += 1U;
        }
    }
    R_SEMANTIC_CHECK(throw_count == 1U);
    R_SEMANTIC_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    r_frontend_destroy(context);

    context = r_frontend_create(NULL);
    R_SEMANTIC_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    source_id = r_semantic_add_bytes(context,
                                     "unheaded-throw-multiple-errors.r",
                                     (const uint8_t *)negative_source,
                                     strlen(negative_source));
    (void)source_id;
    R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 2U);
    if (r_frontend_diagnostic_count(context) == 2U) {
        const RDiagnostic *effect = r_frontend_diagnostic(context, UINT32_C(0));
        const RDiagnostic *type = r_frontend_diagnostic(context, UINT32_C(1));

        R_SEMANTIC_CHECK((effect != NULL) && (strcmp(effect->code, "R-DIAG-EFFECT-002") == 0) &&
                         (strcmp(effect->rule_id, "R-ERR-0002") == 0) &&
                         (effect->phase == R_DIAGNOSTIC_PHASE_SEMANTIC));
        R_SEMANTIC_CHECK((type != NULL) && (strcmp(type->code, "R-DIAG-TYPE-001") == 0) &&
                         (strcmp(type->rule_id, "R-AGG-0010") == 0) &&
                         (type->phase == R_DIAGNOSTIC_PHASE_SEMANTIC));
    }
    r_frontend_destroy(context);
}

static void r_semantic_test_error_declarations(void) {
    static const char *const sources[] = {
        "module errors.record; void fail() throws E { E first = {.code=1}; errors.record::E second "
        "= first; throw second; } error E { i32 code; };",
        "module errors.empty; error E {}; void fail() throws E { throw E {}; }",
        "module errors.enum_case; error E { A, B, }; void fail() throws E { throw E::A; }",
        "module errors.copy; error E { i32 code; }; void fail(E error) throws E { E cloned = "
        "error; "
        "throw cloned; }",
        "module errors.owner; error E { own i32* value; }; void fail(E error) throws E { throw "
        "move "
        "error; }",
        "module errors.named; struct error { i32 value; }; error identity(error error) { return "
        "error; }",
        "module errors.error_named; error error { i32 value; }; void fail(error error) throws "
        "error { throw error; }",
        "module errors.standard; void use(std.error::error error) throws std.error::error { throw "
        "error; }",
        "module errors.struct_throws; struct E { i32 code; }; void fail() throws E {}",
        "module errors.enum_throws; enum E { A, }; void fail() throws E {}",
        "module errors.struct_throw; struct E { i32 code; }; void fail(E value) { throw value; }",
        "module errors.enum_throw; enum E { A, }; void fail() { throw E::A; }",
        "module errors.struct_catch; struct E { i32 code; }; void fail() { try {} catch (E error) "
        "{} }",
        "module errors.enum_catch; enum E { A, }; void fail() { try {} catch (E error) {} }",
        "module errors.task_type; struct E { i32 code; }; void use(task<i32 throws E> value);",
        "module errors.join; enum E { A, }; void use(std.thread::join_handle<i32 throws E> value);",
        "module errors.scoped; struct E { i32 code; }; void use(std.thread::scoped_join_handle<i32 "
        "throws E> value);",
        "module errors.no_suffix; struct custom_error { i32 code; }; void use() throws "
        "custom_error {}",
        "module errors.code; enum Code { A, }; error E { Code code; }; void use() throws Code {}",
        "module errors.standard_code; void use() throws std.fs::error_code {}",
        "module errors.move_missing; error E { own i32* value; }; void fail(E error) throws E { "
        "throw error; }",
        "module errors.exact; error E { i32 code; }; error F { i32 code; }; void fail() { try { "
        "throw E {.code=1}; } catch (F error) {} }",
    };
    size_t index;
    for (index = 0U; index < sizeof(sources) / sizeof(sources[0]); ++index) {
        RFrontendContext *context = r_frontend_create(NULL);
        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL) {
            return;
        }
        (void)r_semantic_add_bytes(
            context, "errors.r", (const uint8_t *)sources[index], strlen(sources[index]));
        const RFrontendStatus status = r_frontend_analyze(context);
        const char *code = index < 8U     ? NULL
                           : index == 20U ? "R-DIAG-MOVE-001"
                           : index == 21U ? "R-DIAG-EFFECT-001"
                                          : "R-DIAG-EFFECT-002";
        if (code == NULL) {
            R_SEMANTIC_CHECK(status == R_FRONTEND_OK);
            R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
            R_SEMANTIC_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
        } else {
            R_SEMANTIC_CHECK(status == R_FRONTEND_INVALID_SOURCE);
            R_SEMANTIC_CHECK(r_semantic_has_diagnostic(context, code, R_DIAGNOSTIC_PHASE_SEMANTIC));
        }
        if ((code == NULL && status != R_FRONTEND_OK) ||
            (code != NULL &&
             !r_semantic_has_diagnostic(context, code, R_DIAGNOSTIC_PHASE_SEMANTIC))) {
            (void)fprintf(stderr, "error declaration case %zu\n", index);
            for (size_t d = 0U; d < r_frontend_diagnostic_count(context); ++d) {
                const RDiagnostic *diagnostic = r_frontend_diagnostic(context, (uint32_t)d);
                (void)fprintf(stderr, "%s: %s\n", diagnostic->code, diagnostic->message);
            }
        }
        r_frontend_destroy(context);
    }
}

static void r_semantic_test_error_imports(void) {
    static const char provider[] =
        "module errors.provider; error Failure { i32 code; }; protected error Hidden {};";
    static const char *const consumers[] = {
        "module errors.consumer; import errors.provider::{Failure}; void fail() throws Failure { "
        "throw Failure {.code=1}; }",
        "module errors.consumer; import errors.provider; void fail() throws "
        "errors.provider::Failure { throw errors.provider::Failure {.code=1}; }",
        "module errors.consumer; import errors.provider::{Hidden}; void fail() throws Hidden {}",
    };
    for (size_t index = 0U; index < sizeof(consumers) / sizeof(consumers[0]); ++index) {
        for (size_t reverse = 0U; reverse < 2U; ++reverse) {
            RFrontendContext *context = r_frontend_create(NULL);
            R_SEMANTIC_CHECK(context != NULL);
            if (context == NULL) {
                return;
            }
            const char *first = reverse != 0U ? provider : consumers[index];
            const char *second = reverse != 0U ? consumers[index] : provider;
            (void)r_semantic_add_bytes(context, "first.r", (const uint8_t *)first, strlen(first));
            (void)r_semantic_add_bytes(
                context, "second.r", (const uint8_t *)second, strlen(second));
            if (index < 2U) {
                R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
                R_SEMANTIC_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
            } else {
                R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
                R_SEMANTIC_CHECK(r_semantic_has_diagnostic(
                    context, "R-DIAG-NAME-001", R_DIAGNOSTIC_PHASE_SEMANTIC));
            }
            r_frontend_destroy(context);
        }
    }
}

static void r_semantic_test_conditional_expression(void) {
    static const char *const sources[] = {
        "module ternary.basic; i32 choose(bool flag) { i32 chosen = flag == true ? 7 : 9; return "
        "chosen; }",
        "module ternary.nested; i32 choose(i32 a, i32 b) { i32 chosen = a > 0 && b > 0 ? 1 : a == "
        "0 ? 2 : 3; return chosen; }",
        "module ternary.copy; i32 choose(bool flag, const i32 a, i32 b) { i32 chosen = flag == "
        "true ? a : b; return chosen; }",
        "module ternary.owner; own i32* choose(bool flag, own i32* value) { own i32* chosen = flag "
        "== true ? move value : move value; return move chosen; }",
        "module ternary.missing_move; own i32* choose(bool flag, own i32* value) { own i32* chosen "
        "= flag == true ? value : move value; return move chosen; }",
        "module ternary.inconsistent; void choose(bool flag, own i32* value) { own i32* selected = "
        "flag == true ? move value : new i32(1); }",
        "module ternary.constant; void choose(own i32* value) { own i32* selected = false ? move "
        "value : new i32(1); drop value; }",
        "module ternary.type; i32 choose(bool flag) { i32 chosen = flag == true ? 1 : false; "
        "return chosen; }",
        "module ternary.condition; i32 choose() { i32 chosen = 1 == false ? 1 : 2; return "
        "chosen; }",
        "module ternary.sign; i64 choose(bool flag, i32 a, u32 b) { i64 chosen = flag == true ? a "
        ": b; return chosen; }",
        "module ternary.widen; i64 choose(bool flag, i16 a, i64 b) { i64 chosen = flag == true ? a "
        ": b; return chosen; }",
        "module ternary.promote; i32 choose(bool flag, i16 a, u16 b) { i32 chosen = flag == true ? "
        "a : b; return chosen; }",
        "module ternary.floating; f64 choose(bool flag, f32 a, f64 b) { f64 chosen = flag == "
        "true ? a : b; return chosen; }",
        "module ternary.place; void choose(bool flag, i32 a, i32 b) { (flag == true ? a : b) = 4; "
        "}",
        "module ternary.address; void choose(bool flag, i32 a, i32 b) { const i32* value = &(flag "
        "== true ? a : b); }",
        "module ternary.effect; error Error {i32 code;}; i32 bad() throws Error {throw "
        "Error{.code=1};} i32 choose(){i32 value = false ? bad() : 3; return value;}",
        "module ternary.strings; str choose(bool flag, str a) { str value = flag == true ? a : "
        "\"literal\"; return value; }",
        "module ternary.chain; void choose(bool flag, const u8[] a) { const u8[] value = flag == "
        "true ? a : \"literal\"; }",
        "module ternary.same_borrow; const i32* choose(bool flag, const i32* a) { const i32* "
        "chosen = flag == true ? a : a; return chosen; }",
        "module ternary.void_arm; void empty(){} void choose(bool flag){ (flag == true ? empty() : "
        "empty()) as void; }",
        "module ternary.flow; i32 choose() { if ((true ? 1 : 2) == 1) { return 3; } }",
        "module ternary.origins; void choose(bool flag) { u8[1] a={1}; u8[1] b={2}; "
        "const u8[] x=&a; const u8[] y=&b; const u8[] z=flag == true ? x : y; }",
    };
    static const char *const codes[] = {
        NULL,
        NULL,
        NULL,
        NULL,
        "R-DIAG-MOVE-001",
        "R-DIAG-MOVE-003",
        NULL,
        "R-DIAG-TYPE-001",
        "R-DIAG-TYPE-001",
        "R-DIAG-TYPE-001",
        NULL,
        NULL,
        NULL,
        "R-DIAG-TYPE-001",
        "R-DIAG-BORROW-002",
        "R-DIAG-EFFECT-001",
        NULL,
        "R-DIAG-TYPE-001",
        NULL,
        "R-DIAG-TYPE-001",
        NULL,
        NULL,
    };
    size_t index;

    for (index = 0U; index < sizeof(sources) / sizeof(sources[0]); ++index) {
        RFrontendContext *context = r_frontend_create(NULL);
        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL) {
            return;
        }
        (void)r_semantic_add_bytes(
            context, "conditional.r", (const uint8_t *)sources[index], strlen(sources[index]));
        if (codes[index] == NULL) {
            R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
            R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
            R_SEMANTIC_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
        } else {
            const RFrontendStatus status = r_frontend_analyze(context);
            if (status != R_FRONTEND_INVALID_SOURCE) {
                (void)fprintf(
                    stderr, "conditional throw case %zu returned status %d\n", index, (int)status);
            }
            R_SEMANTIC_CHECK(status == R_FRONTEND_INVALID_SOURCE);
            if (!r_semantic_has_diagnostic(context, codes[index], R_DIAGNOSTIC_PHASE_SEMANTIC)) {
                size_t diagnostic_index;
                (void)fprintf(
                    stderr, "conditional expression case %zu expected %s\n", index, codes[index]);
                for (diagnostic_index = 0U; diagnostic_index < r_frontend_diagnostic_count(context);
                     ++diagnostic_index) {
                    const RDiagnostic *diagnostic =
                        r_frontend_diagnostic(context, (uint32_t)diagnostic_index);
                    (void)fprintf(stderr, "%s: %s\n", diagnostic->code, diagnostic->message);
                }
                R_SEMANTIC_CHECK(false);
            }
        }
        r_frontend_destroy(context);
    }
}

/* R-TYPE-0036, R-FUNC-0007: explicit generic arguments and method function items. */
static void r_semantic_test_explicit_generics(void) {
    static const char prelude[] =
        "error Bad { i32 code; }; "
        "@generic<T: copy> array<T> make() throws std.alloc::alloc_error { "
        "array<T> values = std.array::create::<T>(); return move values; } "
        "@generic<T: copy, const usize N> usize width(T value) { value as void; return N; } "
        "@generic<T: copy, E: errors> T relay(T value) throws E { return value; } "
        "@generic<T: copy> struct Box { T value; }; "
        "@generic<T: copy, U: copy> U Box<T>::map(const Box<T>* this, U other) { return "
        "other; } "
        "struct Point { i32 x; }; i32 Point::get(const Point* this) { return this->x; } "
        "i32 plain() { return 1; } ";
    static const struct {
        const char *name;
        const char *source;
        const char *diagnostic;
    } cases[] = {
        {"result-only",
         "void f() throws std.alloc::alloc_error { array<i32> a = make::<i32>(); drop a; }",
         NULL},
        {"constant", "usize f() { usize n = width::<u8, 4usize>(1u8); return n; }", NULL},
        {"empty-set", "i32 f() { i32 x = relay::<i32, throws()>(1); return x; }", NULL},
        {"named-set",
         "i32 f() throws Bad { i32 x = relay::<i32, throws(Bad)>(1); return x; }",
         NULL},
        {"forward-set",
         "@generic<E: errors> i32 f() throws E { i32 x = relay::<i32, throws(E)>(1); return x; }",
         NULL},
        {"forward-constant",
         "@generic<const usize N> usize f() { usize n = width::<u8, N>(1u8); return n; }",
         NULL},
        {"item", "usize f() { auto w = width::<u8, 2usize>; usize n = w(1u8); return n; }", NULL},
        {"owner-prefix",
         "u8 f() { Box<i32> b = {.value = 1}; u8 r = Box<i32>::map::<u8>(&b, 2u8); return r; }",
         NULL},
        {"receiver",
         "u8 f() { Box<i32> b = {.value = 1}; u8 r = b.map::<u8>(2u8); return r; }",
         NULL},
        {"method-item",
         "i32 f() { Point p = {.x = 3}; auto get = Point::get; i32 r = get(&p); return r; }",
         NULL},
        {"expected-result",
         "void f() throws std.alloc::alloc_error { array<i32> a = make(); drop a; }",
         NULL},
        {"missing",
         "void f() throws std.alloc::alloc_error { make() as void; }",
         "R-DIAG-TYPE-001"},
        {"too-many",
         "void f() throws std.alloc::alloc_error { array<i32> a = make::<i32, i32>(); drop a; }",
         "R-DIAG-TYPE-001"},
        {"constant-kind",
         "usize f() { usize n = width::<u8, u8>(1u8); return n; }",
         "R-DIAG-CONST-001"},
        {"set-kind", "i32 f() { i32 x = relay::<throws(), i32>(1); return x; }", "R-DIAG-TYPE-001"},
        {"non-generic", "i32 f() { i32 x = plain::<i32>(); return x; }", "R-DIAG-TYPE-001"},
        {"open-item", "void f() { auto m = make; }", "R-DIAG-TYPE-001"},
        {"owner-open-item", "void f() { auto m = Box<i32>::map; }", "R-DIAG-TYPE-001"},
        {"constraint",
         "void f() throws std.alloc::alloc_error { array<own i32*> a = make::<own i32*>(); drop "
         "a; }",
         "R-DIAG-TYPE-001"},
        {"set-member",
         "i32 f() { i32 x = relay::<i32, throws(i32)>(1); return x; }",
         "R-DIAG-EFFECT-002"},
        {"unhandled",
         "i32 f() { i32 x = relay::<i32, throws(Bad)>(1); return x; }",
         "R-DIAG-EFFECT-001"},
    };
    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        RFrontendContext *context = r_frontend_create(NULL);
        char source[2048];
        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        (void)snprintf(source,
                       sizeof(source),
                       "module test.explicit_generics;\n%s\n%s\n",
                       prelude,
                       cases[index].source);
        (void)r_semantic_add_bytes(
            context, cases[index].name, (const uint8_t *)source, strlen(source));
        const RFrontendStatus status = r_frontend_analyze(context);
        if (status !=
                (cases[index].diagnostic == NULL ? R_FRONTEND_OK : R_FRONTEND_INVALID_SOURCE) ||
            (cases[index].diagnostic != NULL &&
             r_semantic_diagnostic_code_count(context, cases[index].diagnostic) == 0U)) {
            RSemanticTestBuffer diagnostics = {0};
            (void)fprintf(stderr, "explicit generic case failed: %s\n", cases[index].name);
            (void)r_frontend_dump_diagnostics_json(context, r_semantic_test_write, &diagnostics);
            (void)fprintf(stderr, "%s\n", diagnostics.bytes == NULL ? "" : diagnostics.bytes);
            free(diagnostics.bytes);
            R_SEMANTIC_CHECK(false);
        }
        r_frontend_destroy(context);
    }
}

/* R-EXPR-0014: a conditional expression is named before it becomes an argument or result. */
static void r_semantic_test_conditional_positions(void) {
    static const struct {
        const char *name;
        const char *source;
        size_t forks;
    } cases[] = {
        {"return", "i32 f(bool b) { return b == true ? 1 : 2; }", 1U},
        {"return-parenthesized", "i32 f(bool b) { return (b == true ? 1 : 2); }", 1U},
        {"return-nested", "i32 f(bool b) { return 1 + (b == true ? 1 : 2); }", 1U},
        {"return-inner-fork", "i32 f(bool b) { return b == true ? (b == false ? 1 : 2) : 3; }", 1U},
        {"return-aggregate",
         "struct S { i32 v; }; S f(bool b) { return {.v = b == true ? 1 : 2}; }",
         1U},
        {"switch-return",
         "i32 f(i32 x, bool b) { switch (x) { case 1: return b == true ? 1 : 2; default: return 0; "
         "} }",
         1U},
        {"argument",
         "i32 g(i32 x) { return x; } i32 f(bool b) { i32 r = g(b == true ? 1 : 2); return r; }",
         1U},
        {"argument-nested",
         "i32 g(i32 x) { return x; } i32 f(bool b) { i32 r = g(1 + (b == true ? 1 : 2)); return "
         "r; }",
         1U},
        {"argument-twice",
         "i32 g(i32 x) { return x; } i32 f(bool b) { i32 r = g(b == true ? 1 : 2) + g(b == false "
         "? 3 : 4); return r; }",
         2U},
        {"method-argument",
         "struct S { i32 v; }; i32 S::get(const S* this, i32 x) { return x; } "
         "i32 f(bool b) { S s = {.v = 1}; i32 r = s.get(b == true ? 1 : 2); return r; }",
         1U},
        {"format-argument",
         "void f(bool b) throws std.alloc::alloc_error { std.string::string s = "
         "f\"v={1}\".format(b == true ? 1 : 2); drop s; }",
         1U},
        {"await-argument",
         "async i32 g(i32 x) { return x; } async i32 f(bool b) throws std.async::start_error { "
         "i32 r = await g(b == true ? 1 : 2); return r; }",
         1U},
        {"panic-operand", "void f(bool b) { panic(b == true ? \"a\" : \"b\"); }", 1U},
        {"initializer", "i32 f(bool b) { i32 r = b == true ? 1 : 2; return r; }", 0U},
        {"assignment",
         "i32 f(bool b) { i32 r = 0; if (b == false) { r = b == true ? 1 : 2; } return r; }",
         0U},
        {"aggregate-local",
         "struct S { i32 v; }; S f(bool b) { S s = {.v = b == true ? 1 : 2}; return s; }",
         0U},
        {"condition",
         "i32 f(bool b) { if ((b == true ? 1 : 2) == 1) { return 3; } return 4; }",
         0U},
    };
    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        RFrontendContext *context = r_frontend_create(NULL);
        char source[1024];
        size_t forks = 0U;
        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        (void)snprintf(source,
                       sizeof(source),
                       "module test.conditional_positions;\n%s\n",
                       cases[index].source);
        (void)r_semantic_add_bytes(
            context, cases[index].name, (const uint8_t *)source, strlen(source));
        const RFrontendStatus status = r_frontend_analyze(context);
        for (size_t item = 0U; item < r_frontend_diagnostic_count(context); ++item) {
            const RDiagnostic *diagnostic = r_frontend_diagnostic(context, (uint32_t)item);
            if ((diagnostic != NULL) && (strcmp(diagnostic->rule_id, "R-EXPR-0014") == 0) &&
                (strcmp(diagnostic->code, "R-DIAG-FLOW-001") == 0)) {
                forks += 1U;
            }
        }
        if ((forks != cases[index].forks) ||
            (status != (cases[index].forks == 0U ? R_FRONTEND_OK : R_FRONTEND_INVALID_SOURCE))) {
            RSemanticTestBuffer diagnostics = {0};
            (void)fprintf(stderr,
                          "conditional position case failed: %s (%zu forks)\n",
                          cases[index].name,
                          forks);
            (void)r_frontend_dump_diagnostics_json(context, r_semantic_test_write, &diagnostics);
            (void)fprintf(stderr, "%s\n", diagnostics.bytes == NULL ? "" : diagnostics.bytes);
            free(diagnostics.bytes);
            R_SEMANTIC_CHECK(false);
        }
        r_frontend_destroy(context);
    }
}

static void r_semantic_test_conditional_throw(void) {
    static const char *const sources[] = {
        "module conditional.positive; error Error { i32 code; }; "
        "i32 constant_true() throws Error { throw (true) {.code = 1}; } "
        "i32 variable(bool flag) throws Error { "
        "throw (flag == true) Error {.code = 2}; return 3; } "
        "void caught() { try { throw (false) {.code = 4}; } catch (Error error) {} }",
        "module conditional.owner; error Error { own i32* value; }; "
        "void test(bool flag, Error error) throws Error { "
        "throw (flag == true) move error; drop error; }",
        "module conditional.unhandled; error Error { i32 code; }; "
        "void test() { throw (false) Error {.code = 1}; }",
        "module conditional.ambiguous; error Error { i32 code; }; "
        "error Other { i32 value; }; "
        "void test() throws Error, Other { throw (true) {.code = 1}; }",
        "module conditional.missing_move; error Error { own i32* value; }; "
        "void test(Error error) throws Error { throw (true) error; }",
        "module conditional.type; error Error { i32 code; }; "
        "void test() throws Error { throw (1 < true) {.code = 1}; }",
        "module conditional.missing_return; error Error { i32 code; }; "
        "i32 test(bool flag) throws Error { throw (flag == true) {.code = 1}; }",
        "module conditional.finally_escape; error Error { i32 code; }; "
        "void test() throws Error { try {} finally { throw (false) {.code = 1}; } }",
        "module conditional.no_rethrow; "
        "void test() { throw (true); }",
        "module conditional.two; error Error { i32 code; }; "
        "i32 test(bool flag) throws Error { throw (flag == true) {.code = 1} else {.code = 2}; } "
        "i32 constant() throws Error { throw (false) Error {.code = 1} else Error {.code = 2}; }",
        "module conditional.two_move; error Error { own i32* value; }; "
        "void test(bool flag, Error error) throws Error { "
        "throw (flag == true) move error else move error; }",
        "module conditional.two_types; error Error { i32 code; }; error Other { i32 code; }; "
        "i32 test(bool flag) throws Error, Other { "
        "throw (flag == true) Error {.code = 1} else Other {.code = 2}; }",
        "module conditional.two_unhandled; error Error { i32 code; }; error Other { i32 code; }; "
        "void test() throws Error { throw (true) Error {.code = 1} else Other {.code = 2}; }",
        "module conditional.two_ambiguous; error Error { i32 code; }; error Other { i32 code; }; "
        "void test() throws Error, Other { throw (true) Error {.code = 1} else {.code = 2}; }",
        "module conditional.two_missing_move; error Error { own i32* value; }; "
        "void test(Error error) throws Error { throw (true) move error else error; }",
        "module conditional.two_invalid; error Error { i32 code; }; "
        "void test() throws Error { throw (true) Error {.code = 1} else 3; }",
    };
    static const char *const codes[] = {
        NULL,
        NULL,
        "R-DIAG-EFFECT-001",
        "R-DIAG-EFFECT-002",
        "R-DIAG-MOVE-001",
        "R-DIAG-TYPE-001",
        "R-DIAG-FLOW-001",
        "R-DIAG-EFFECT-001",
        "R-DIAG-EFFECT-002",
        NULL,
        NULL,
        NULL,
        "R-DIAG-EFFECT-001",
        "R-DIAG-EFFECT-002",
        "R-DIAG-MOVE-001",
        "R-DIAG-EFFECT-002",
    };
    size_t index;

    for (index = 0U; index < sizeof(sources) / sizeof(sources[0]); ++index) {
        RFrontendContext *context = r_frontend_create(NULL);
        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL) {
            return;
        }
        (void)r_semantic_add_bytes(
            context, "conditional.r", (const uint8_t *)sources[index], strlen(sources[index]));
        if (codes[index] == NULL) {
            R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
            R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
            R_SEMANTIC_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
        } else {
            const RFrontendStatus status = r_frontend_analyze(context);
            if (status != R_FRONTEND_INVALID_SOURCE) {
                (void)fprintf(
                    stderr, "conditional throw case %zu returned status %d\n", index, (int)status);
            }
            R_SEMANTIC_CHECK(status == R_FRONTEND_INVALID_SOURCE);
            if (!r_semantic_has_diagnostic(context, codes[index], R_DIAGNOSTIC_PHASE_SEMANTIC)) {
                size_t diagnostic_index;
                (void)fprintf(
                    stderr, "conditional throw case %zu expected %s\n", index, codes[index]);
                for (diagnostic_index = 0U; diagnostic_index < r_frontend_diagnostic_count(context);
                     ++diagnostic_index) {
                    const RDiagnostic *diagnostic =
                        r_frontend_diagnostic(context, (uint32_t)diagnostic_index);
                    (void)fprintf(stderr, "%s: %s\n", diagnostic->code, diagnostic->message);
                }
                R_SEMANTIC_CHECK(false);
            }
        }
        r_frontend_destroy(context);
    }
}

#include "borrow_component_tests.inc"
#include "format_semantic_tests.inc"
#include "json_semantic_tests.inc"
#include "l10_regression_tests.inc"
#include "l11_regression_tests.inc"
#include "l12_regression_tests.inc"
#include "l13_regression_tests.inc"
#include "l14_regression_tests.inc"
#include "l15_regression_tests.inc"
#include "l16_regression_tests.inc"
#include "l17_regression_tests.inc"
#include "l18_regression_tests.inc"
#include "l19_regression_tests.inc"
#include "l20_regression_tests.inc"
#include "l21_regression_tests.inc"
#include "l22_regression_tests.inc"
#include "l23_regression_tests.inc"
#include "l24_regression_tests.inc"
#include "l25_regression_tests.inc"
#include "l26_regression_tests.inc"
#include "l27_regression_tests.inc"
#include "l28_regression_tests.inc"
#include "l29_regression_tests.inc"
#include "l30_regression_tests.inc"
#include "l31_regression_tests.inc"
#include "l32_regression_tests.inc"
#include "l33_regression_tests.inc"
#include "m18_regression_tests.inc"
#include "m19_regression_tests.inc"
#include "m20_regression_tests.inc"
#include "m21_regression_tests.inc"
#include "m22_regression_tests.inc"
#include "m23_regression_tests.inc"
#include "m24_regression_tests.inc"

#include "l34_regression_tests.inc"
#include "l35_regression_tests.inc"
#include "l36_regression_tests.inc"
#include "l37_regression_tests.inc"
#include "l6_regression_tests.inc"
#include "match_tests.inc"
#include "opaque_result_tests.inc"
#include "scoped_task_tests.inc"
#include "trait_contract_tests.inc"

static void r_semantic_test_await_calls(void) {
    static const struct {
        const char *source;
        const char *code;
    } cases[] = {
        {"module semantic.await_calls; async i32 child() { return 1; } async i32 f() throws "
         "std.async::start_error { i32 value = await child(); return value; }",
         NULL},
        {"module semantic.await_calls; async void child() {} async void f() throws "
         "std.async::start_error { await child(); await child(); }",
         NULL},
        {"module semantic.await_calls; async void child(own i32* value) {} async void f(own i32* "
         "value) { try { await child(move value); } catch (std.async::start_error error) { own "
         "i32* recovered = move value; } }",
         NULL},
        {"module semantic.await_calls; async void child(list<own i32*> value) {} "
         "async usize f(list<own i32*> value) { try { await child(move value); return 0usize; } "
         "catch (std.async::start_error error) {} usize count = len(value); return count; }",
         NULL},
        {"module semantic.await_calls; async void child(list<own i32*> value) {} "
         "async usize f(list<own i32*> value) { try { await child(move value); } "
         "catch (std.async::start_error error) {} usize count = len(value); return count; }",
         "R-DIAG-MOVE-003"},
        {"module semantic.await_calls; async void child(list<rc i32> value) {}",
         "R-DIAG-ASYNC-001"},
        {"module semantic.await_calls; async void child(dict<i32, const i32*> value) {}",
         "R-DIAG-ASYNC-001"},
        {"module semantic.await_calls; error E { rc i32 value; }; "
         "@generic<T: send> void require_send(const T* value) {} "
         "void check(task<void throws E> value) { require_send(&value); }",
         "R-DIAG-ASYNC-001"},
        {"module semantic.await_calls; i32 child() { return 1; } async void f() { await child(); }",
         "R-DIAG-ASYNC-001"},
        {"module semantic.await_calls; void child() {} async void f() { await child(); }",
         "R-DIAG-ASYNC-001"},
        {"module semantic.await_calls; async i32 child() { return 1; } async void f() throws "
         "std.async::start_error { await child(); }",
         "R-DIAG-ASYNC-001"},
        {"module semantic.await_calls; async i32 child() { return 1; } async void f() throws "
         "std.async::start_error { u32 result = await child(); }",
         "R-DIAG-TYPE-001"},
        {"module semantic.await_calls; async void child() {} async void f() { await child(); }",
         "R-DIAG-EFFECT-001"},
        {"module semantic.await_calls; error E {}; async void child() throws E { throw E {}; } "
         "async void f() throws std.async::start_error { await child(); }",
         "R-DIAG-EFFECT-001"},
        {"module semantic.await_calls; async void child() {} async void f() throws "
         "std.async::start_error { try {} finally { await child(); } }",
         "R-DIAG-EFFECT-001"},
        {"module semantic.await_calls; async void child(own i32* value) {} async void f(own i32* "
         "value) throws std.async::start_error { await child(move value); own i32* other = move "
         "value; }",
         "R-DIAG-MOVE-002"},
        {"module semantic.await_calls; async void child(own i32* a, own i32* b) {} async void "
         "f(own i32* value) throws std.async::start_error { await child(move value, move value); }",
         "R-DIAG-BORROW-001"},
        {"module semantic.await_calls; async void child() {} async i32 f() throws "
         "std.async::start_error { i32 value = 1; const i32* pointer = &value; await child(); "
         "return *pointer; }",
         "R-DIAG-ASYNC-001"},
        {"module semantic.await_calls; async i32 child() { return 1; } async void f() throws "
         "std.async::start_error { static i32 value = await child(); }",
         "R-DIAG-ASYNC-001"},
        {"module semantic.await_calls; async void child() {} void f() throws "
         "std.async::start_error { await child(); }",
         "R-DIAG-ASYNC-001"},
    };
    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        RFrontendContext *context = r_frontend_create(NULL);
        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        (void)r_semantic_add_bytes(context,
                                   "await-calls.r",
                                   (const uint8_t *)cases[index].source,
                                   strlen(cases[index].source));
        RFrontendStatus status = r_frontend_analyze(context);
        if (status == R_FRONTEND_OK) {
            status = r_frontend_lower_mir(context);
        }
        bool found = cases[index].code == NULL
                         ? status == R_FRONTEND_OK && r_frontend_diagnostic_count(context) == 0U
                         : false;
        for (size_t diagnostic_index = 0U; diagnostic_index < r_frontend_diagnostic_count(context);
             ++diagnostic_index) {
            const RDiagnostic *diagnostic =
                r_frontend_diagnostic(context, (uint32_t)diagnostic_index);
            if (cases[index].code != NULL && strcmp(diagnostic->code, cases[index].code) == 0) {
                found = true;
            }
        }
        if (!found) {
            (void)fprintf(stderr, "await call case %zu status %d\n", index, (int)status);
            for (size_t diagnostic_index = 0U;
                 diagnostic_index < r_frontend_diagnostic_count(context);
                 ++diagnostic_index) {
                const RDiagnostic *diagnostic =
                    r_frontend_diagnostic(context, (uint32_t)diagnostic_index);
                (void)fprintf(stderr, "%s: %s\n", diagnostic->code, diagnostic->message);
            }
        }
        R_SEMANTIC_CHECK(found);
        r_frontend_destroy(context);
    }
}

static void r_semantic_test_async_hir(void) {
    static const char source_text[] = "module semantic.async_positive;\n"
                                      "async i32 produce(i32 value) { return value; }\n"
                                      "i32 launch() throws std.async::start_error {\n"
                                      "  task<i32> started = produce(7);\n"
                                      "  std.async::cancel(move started);\n"
                                      "  return 0;\n"
                                      "}\n"
                                      "async i32 consume(task<i32> work) {\n"
                                      "  i32 value = await move work;\n"
                                      "  return value;\n"
                                      "}\n"
                                      "async void drain(task<void> work) { await move work; }\n";
    RFrontendContext *context = r_frontend_create(NULL);
    RSemanticTestBuffer dump = {0};
    RSourceId source_id;
    size_t async_function_count = 0U;
    size_t async_start_count = 0U;
    size_t await_count = 0U;
    size_t symbol_index;
    size_t node_index;

    R_SEMANTIC_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    source_id = r_semantic_add_bytes(
        context, "semantic-async.r", (const uint8_t *)source_text, strlen(source_text));
    (void)source_id;
    R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    for (symbol_index = 0U; symbol_index < context->semantic_symbol_count; ++symbol_index) {
        const RSemanticSymbol *symbol = &context->semantic_symbols[symbol_index];

        if ((symbol->kind == R_SEMANTIC_SYMBOL_FUNCTION) && symbol->is_async) {
            const RSemanticType *task = r_semantic_type(context, symbol->async_task_type);
            const RSemanticType *start = r_semantic_type(context, symbol->async_start_type);
            const RSemanticType *error =
                start == NULL ? NULL : r_semantic_type(context, start->second);

            R_SEMANTIC_CHECK((task != NULL) && (task->kind == R_SEMANTIC_TYPE_TASK) &&
                             (task->base == symbol->return_type));
            R_SEMANTIC_CHECK((start != NULL) && (start->kind == R_SEMANTIC_TYPE_EFFECT_CARRIER) &&
                             (start->base == symbol->async_task_type));
            R_SEMANTIC_CHECK(
                (error != NULL) && (error->kind == R_SEMANTIC_TYPE_EFFECT_SET) &&
                (r_semantic_effect_count(context, start->second) == UINT32_C(1)) &&
                r_semantic_test_standard_type_named(
                    context,
                    r_semantic_type(context, r_semantic_effect_at(context, start->second, 0U)),
                    "std.async::start_error"));
            async_function_count += 1U;
        }
    }
    for (node_index = 0U; node_index < context->hir_node_count; ++node_index) {
        const RHirNode *node = &context->hir_nodes[node_index];

        if (node->kind == R_HIR_ASYNC_START) {
            const RSemanticType *start = r_semantic_type(context, node->auxiliary_type);
            R_SEMANTIC_CHECK((start != NULL) && (start->kind == R_SEMANTIC_TYPE_EFFECT_CARRIER) &&
                             (start->base == node->type));
            R_SEMANTIC_CHECK(node->symbol != R_SYMBOL_ID_INVALID);
            async_start_count += 1U;
        } else if (node->kind == R_HIR_AWAIT) {
            const RSemanticType *task = r_semantic_type(context, node->auxiliary_type);
            R_SEMANTIC_CHECK((task != NULL) && (task->kind == R_SEMANTIC_TYPE_TASK) &&
                             (task->base == node->type));
            R_SEMANTIC_CHECK(node->is_move && (node->operation == R_TOKEN_KW_AWAIT) &&
                             (node->child_count == UINT32_C(1)) &&
                             (node->symbol != R_SYMBOL_ID_INVALID));
            await_count += 1U;
        }
    }
    R_SEMANTIC_CHECK(async_function_count == 3U);
    R_SEMANTIC_CHECK(async_start_count == 1U);
    R_SEMANTIC_CHECK(await_count == 2U);
    R_SEMANTIC_CHECK(r_frontend_dump_hir(context, r_semantic_test_write, &dump) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK((dump.bytes != NULL) && (strstr(dump.bytes, "async_start") != NULL) &&
                     (strstr(dump.bytes, "await") != NULL));
    free(dump.bytes);
    r_frontend_destroy(context);
}

static void r_semantic_test_implicit_final_return(void) {
    static const char source_text[] = "module semantic.implicit_final_return;\n"
                                      "error issue { i32 code; };\n"
                                      "void plain_void() {}\n"
                                      "async void async_plain_void() {}\n"
                                      "void sync_success() throws issue {}\n"
                                      "async void async_success() throws issue {}\n"
                                      "void conditional_success(bool fail) throws issue {\n"
                                      "    if (fail == true) {\n"
                                      "        issue error = {.code = 1};\n"
                                      "        throw error;\n"
                                      "    }\n"
                                      "}\n"
                                      "void consume(array<u8> value) throws issue {}\n"
                                      "void explicit_success() throws issue { return; }\n";
    RFrontendContext *context = r_frontend_create(NULL);
    RSourceId source_id;
    size_t throwing_function_count = 0U;
    size_t throw_count = 0U;
    size_t drop_count = 0U;
    size_t explicit_return_count = 0U;
    size_t mir_return_count = 0U;
    size_t mir_unreachable_count = 0U;
    size_t index;

    R_SEMANTIC_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    source_id = r_semantic_add_bytes(context,
                                     "semantic-implicit-final-return.r",
                                     (const uint8_t *)source_text,
                                     strlen(source_text));
    (void)source_id;
    R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    for (index = 0U; index < context->semantic_symbol_count; ++index) {
        const RSemanticSymbol *symbol = &context->semantic_symbols[index];

        if ((symbol->kind == R_SEMANTIC_SYMBOL_FUNCTION) &&
            (r_semantic_effect_count(context, symbol->throws_type) != UINT32_C(0))) {
            throwing_function_count += 1U;
        }
    }
    for (index = 0U; index < context->hir_node_count; ++index) {
        const RHirNode *node = &context->hir_nodes[index];

        if (node->kind == R_HIR_THROW) {
            throw_count += 1U;
        } else if (node->kind == R_HIR_DROP) {
            drop_count += 1U;
        } else if ((node->kind == R_HIR_RETURN) && (node->operation == R_TOKEN_KW_RETURN)) {
            explicit_return_count += 1U;
        }
    }
    R_SEMANTIC_CHECK(throwing_function_count == 5U);
    R_SEMANTIC_CHECK(throw_count == 1U);
    R_SEMANTIC_CHECK(drop_count == 1U);
    R_SEMANTIC_CHECK(explicit_return_count == 1U);
    R_SEMANTIC_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    for (index = 0U; index < context->mir_instruction_count; ++index) {
        const RMirInstruction *instruction = &context->mir_instructions[index];

        if (instruction->kind == R_MIR_INSTRUCTION_RETURN) {
            mir_return_count += 1U;
        } else if (instruction->kind == R_MIR_INSTRUCTION_UNREACHABLE) {
            mir_unreachable_count += 1U;
        }
    }
    R_SEMANTIC_CHECK(mir_return_count == 7U);
    R_SEMANTIC_CHECK(mir_unreachable_count == 0U);
    r_frontend_destroy(context);
}

static void r_semantic_test_async_cancel(void) {
    static const char positive_source[] = "module semantic.async_cancel_positive;\n"
                                          "error async_error { i32 code; };\n"
                                          "void cancel_value(task<i32> operation) {\n"
                                          "    std.async::cancel(move operation);\n"
                                          "}\n"
                                          "void cancel_void(task<void> operation) {\n"
                                          "    std.async::cancel(move operation);\n"
                                          "}\n"
                                          "void detach_fallible(\n"
                                          "    task<i32 throws async_error> operation) {\n"
                                          "    std.async::detach(move operation);\n"
                                          "}\n"
                                          "void implicitly_drop_infallible(task<i32> operation) {\n"
                                          "}\n";
    static const struct {
        const char *source;
        const char *code;
        const char *rule_id;
    } negative_cases[] = {
        {
            "module semantic.async_cancel_missing_move;\n"
            "void cancel(task<i32> operation) { std.async::cancel(operation); }\n",
            "R-DIAG-MOVE-001",
            "R-OWN-0003",
        },
        {
            "module semantic.async_cancel_wrong_arity;\n"
            "void cancel() { std.async::cancel(); }\n",
            "R-DIAG-TYPE-001",
            "R-SLIB-ASYNC-0006",
        },
        {
            "module semantic.async_cancel_non_task;\n"
            "void cancel(array<u8> value) { std.async::cancel(move value); }\n",
            "R-DIAG-TYPE-001",
            "R-SLIB-ASYNC-0006",
        },
        {
            "module semantic.async_cancel_reuse;\n"
            "void cancel(task<i32> operation) {\n"
            "    std.async::cancel(move operation);\n"
            "    std.async::cancel(move operation);\n"
            "}\n",
            "R-DIAG-MOVE-002",
            "R-OWN-0004",
        },
    };
    RFrontendContext *context = r_frontend_create(NULL);
    RSemanticTestBuffer hir = {0};
    RSemanticTestBuffer mir = {0};
    RSourceId source_id = R_SOURCE_ID_INVALID;
    const char *cursor;
    size_t operation_count = 0U;
    size_t detach_count = 0U;
    size_t implicit_drop_count = 0U;
    size_t index;

    R_SEMANTIC_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_SEMANTIC_CHECK(r_frontend_add_source(context,
                                           "async-cancel-positive.r",
                                           (const uint8_t *)positive_source,
                                           strlen(positive_source),
                                           &source_id) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    for (index = 0U; index < context->hir_node_count; ++index) {
        if ((context->hir_nodes[index].kind == R_HIR_DROP) &&
            (context->hir_nodes[index].operation == R_TOKEN_INVALID)) {
            implicit_drop_count += 1U;
        }
    }
    R_SEMANTIC_CHECK(implicit_drop_count == 1U);
    R_SEMANTIC_CHECK(r_frontend_dump_hir(context, r_semantic_test_write, &hir) == R_FRONTEND_OK);
    if (hir.bytes != NULL) {
        cursor = hir.bytes;
        while ((cursor = strstr(cursor, "operation=std.async::cancel")) != NULL) {
            operation_count += 1U;
            cursor += sizeof("operation=std.async::cancel") - 1U;
        }
        cursor = hir.bytes;
        while ((cursor = strstr(cursor, "operation=std.async::detach")) != NULL) {
            detach_count += 1U;
            cursor += sizeof("operation=std.async::detach") - 1U;
        }
    }
    R_SEMANTIC_CHECK(operation_count == 2U);
    R_SEMANTIC_CHECK(detach_count == 1U);
    R_SEMANTIC_CHECK((hir.bytes != NULL) && (strstr(hir.bytes, "(task i32)") != NULL) &&
                     (strstr(hir.bytes, "(task void)") != NULL));
    R_SEMANTIC_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    implicit_drop_count = 0U;
    for (index = 0U; index < context->mir_instruction_count; ++index) {
        if (context->mir_instructions[index].kind == R_MIR_INSTRUCTION_DROP) {
            implicit_drop_count += 1U;
        }
    }
    R_SEMANTIC_CHECK(implicit_drop_count == 1U);
    R_SEMANTIC_CHECK(r_frontend_dump_mir(context, r_semantic_test_write, &mir) == R_FRONTEND_OK);
    operation_count = 0U;
    detach_count = 0U;
    if (mir.bytes != NULL) {
        cursor = mir.bytes;
        while ((cursor = strstr(cursor, "operation=std.async::cancel")) != NULL) {
            operation_count += 1U;
            cursor += sizeof("operation=std.async::cancel") - 1U;
        }
        cursor = mir.bytes;
        while ((cursor = strstr(cursor, "operation=std.async::detach")) != NULL) {
            detach_count += 1U;
            cursor += sizeof("operation=std.async::detach") - 1U;
        }
    }
    R_SEMANTIC_CHECK(operation_count == 2U);
    R_SEMANTIC_CHECK(detach_count == 1U);
    free(mir.bytes);
    free(hir.bytes);
    r_frontend_destroy(context);

    for (index = 0U; index < (sizeof(negative_cases) / sizeof(negative_cases[0])); ++index) {
        const RDiagnostic *diagnostic;

        context = r_frontend_create(NULL);
        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        source_id = r_semantic_add_bytes(context,
                                         "async-cancel-negative.r",
                                         (const uint8_t *)negative_cases[index].source,
                                         strlen(negative_cases[index].source));
        (void)source_id;
        R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
        R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 1U);
        diagnostic = r_frontend_diagnostic(context, UINT32_C(0));
        R_SEMANTIC_CHECK((diagnostic != NULL) &&
                         (strcmp(diagnostic->code, negative_cases[index].code) == 0) &&
                         (strcmp(diagnostic->rule_id, negative_cases[index].rule_id) == 0) &&
                         (diagnostic->phase == R_DIAGNOSTIC_PHASE_SEMANTIC));
        r_frontend_destroy(context);
    }
}

static void r_semantic_test_effectful_task_resolution(void) {
    static const char positive_source[] =
        "module semantic.effectful_task_resolution_positive;\n"
        "error task_error { i32 code; };\n"
        "async i32 await_value(task<i32 throws task_error> operation) throws task_error {\n"
        "    i32 value = await move operation;\n"
        "    return value;\n"
        "}\n"
        "void cancel_value(task<i32 throws task_error> operation) {\n"
        "    std.async::cancel(move operation);\n"
        "}\n"
        "void resolve_branches(bool select_cancel,\n"
        "                      task<i32 throws task_error> operation) {\n"
        "    if (select_cancel == true) {\n"
        "        std.async::cancel(move operation);\n"
        "    } else {\n"
        "        std.async::detach(move operation);\n"
        "    }\n"
        "}\n"
        "void resolve_after_terminal_branch(bool stop,\n"
        "                                   task<i32 throws task_error> operation) {\n"
        "    if (stop == true) {\n"
        "        std.async::cancel(move operation);\n"
        "        return;\n"
        "    } else {\n"
        "    }\n"
        "    std.async::detach(move operation);\n"
        "}\n"
        "error caught_error { i32 code; };\n"
        "void fail() throws caught_error { throw {.code = 1}; }\n"
        "void resolve_try_and_catch(task<i32 throws task_error> operation) {\n"
        "    try {\n"
        "        fail();\n"
        "        std.async::cancel(move operation);\n"
        "    } catch (caught_error error) {\n"
        "        error as void;\n"
        "        std.async::detach(move operation);\n"
        "    }\n"
        "}\n"
        "void resolve_before_throw(task<i32 throws task_error> operation) {\n"
        "    try {\n"
        "        std.async::cancel(move operation);\n"
        "        throw caught_error {.code = 2};\n"
        "    } catch (caught_error error) {\n"
        "        error as void;\n"
        "    }\n"
        "}\n"
        "void resolve_in_finally(task<i32 throws task_error> operation) {\n"
        "    try {\n"
        "    } finally {\n"
        "        std.async::cancel(move operation);\n"
        "    }\n"
        "}\n"
        "void resolve_return_in_finally(task<i32 throws task_error> operation) {\n"
        "    try {\n"
        "        return;\n"
        "    } finally {\n"
        "        std.async::detach(move operation);\n"
        "    }\n"
        "}\n"
        "void resolve_catch_in_finally(task<i32 throws task_error> operation) {\n"
        "    try {\n"
        "        fail();\n"
        "    } catch (caught_error error) {\n"
        "        error as void;\n"
        "    } finally {\n"
        "        std.async::cancel(move operation);\n"
        "    }\n"
        "}\n"
        "async i32 resolve_await_catch(\n"
        "    task<i32 throws task_error> operation) {\n"
        "    try {\n"
        "        i32 value = await move operation;\n"
        "        return value;\n"
        "    } catch (task_error error) {\n"
        "        error as void;\n"
        "        return 0;\n"
        "    }\n"
        "}\n";
    static const char *negative_sources[] = {
        "module semantic.effectful_task_fallthrough;\n"
        "error task_error { i32 code; };\n"
        "void unresolved(task<i32 throws task_error> operation) {}\n",
        "module semantic.effectful_task_explicit_drop;\n"
        "error task_error { i32 code; };\n"
        "void unresolved(task<i32 throws task_error> operation) { drop operation; }\n",
        "module semantic.effectful_task_return;\n"
        "error task_error { i32 code; };\n"
        "void unresolved(task<i32 throws task_error> operation) { return; }\n",
        "module semantic.effectful_task_throw;\n"
        "error task_error { i32 code; };\n"
        "error exit_error { i32 code; };\n"
        "void unresolved(task<i32 throws task_error> operation) throws exit_error {\n"
        "    throw {.code = 1};\n"
        "}\n",
        "module semantic.effectful_task_break;\n"
        "error task_error { i32 code; };\n"
        "void unresolved(task<i32 throws task_error> operation) {\n"
        "    while (true) {\n"
        "        task<i32 throws task_error> local = move operation;\n"
        "        break;\n"
        "    }\n"
        "}\n",
        "module semantic.effectful_task_continue;\n"
        "error task_error { i32 code; };\n"
        "void unresolved(task<i32 throws task_error> operation) {\n"
        "    while (true) {\n"
        "        task<i32 throws task_error> local = move operation;\n"
        "        continue;\n"
        "    }\n"
        "}\n",
        "module semantic.effectful_task_branch;\n"
        "error task_error { i32 code; };\n"
        "void unresolved(bool consume, task<i32 throws task_error> operation) {\n"
        "    if (consume == true) {\n"
        "        std.async::cancel(move operation);\n"
        "    } else {\n"
        "    }\n"
        "}\n",
        "module semantic.effectful_task_try_path;\n"
        "error task_error { i32 code; };\n"
        "error caught_error { i32 code; };\n"
        "void fail() throws caught_error { throw {.code = 1}; }\n"
        "void unresolved(task<i32 throws task_error> operation) {\n"
        "    try {\n"
        "        fail();\n"
        "        std.async::cancel(move operation);\n"
        "    } catch (caught_error error) {\n"
        "        error as void;\n"
        "    }\n"
        "}\n",
        "module semantic.effectful_task_catch_path;\n"
        "error task_error { i32 code; };\n"
        "error caught_error { i32 code; };\n"
        "void fail() throws caught_error { throw {.code = 1}; }\n"
        "void unresolved(task<i32 throws task_error> operation) {\n"
        "    try {\n"
        "        fail();\n"
        "    } catch (caught_error error) {\n"
        "        error as void;\n"
        "        std.async::cancel(move operation);\n"
        "    }\n"
        "}\n",
        "module semantic.effectful_task_return_finally;\n"
        "error task_error { i32 code; };\n"
        "void unresolved(task<i32 throws task_error> operation) {\n"
        "    try {\n"
        "        return;\n"
        "    } finally {\n"
        "    }\n"
        "}\n",
        "module semantic.effectful_task_finally_path_merge;\n"
        "error task_error { i32 code; };\n"
        "void unresolved(bool consume, task<i32 throws task_error> operation) {\n"
        "    try {\n"
        "        if (consume == true) {\n"
        "            std.async::cancel(move operation);\n"
        "            return;\n"
        "        } else {\n"
        "            return;\n"
        "        }\n"
        "    } finally {\n"
        "    }\n"
        "}\n",
    };
    RFrontendContext *context = r_frontend_create(NULL);
    RSourceId source_id = R_SOURCE_ID_INVALID;
    size_t case_index;

    R_SEMANTIC_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    source_id = r_semantic_add_bytes(context,
                                     "effectful-task-resolution-positive.r",
                                     (const uint8_t *)positive_source,
                                     strlen(positive_source));
    (void)source_id;
    R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    R_SEMANTIC_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    r_frontend_destroy(context);

    for (case_index = 0U; case_index < (sizeof(negative_sources) / sizeof(negative_sources[0]));
         ++case_index) {
        const RDiagnostic *diagnostic;

        context = r_frontend_create(NULL);
        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        source_id = r_semantic_add_bytes(context,
                                         "effectful-task-resolution-negative.r",
                                         (const uint8_t *)negative_sources[case_index],
                                         strlen(negative_sources[case_index]));
        (void)source_id;
        R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
        R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 1U);
        diagnostic = r_frontend_diagnostic(context, UINT32_C(0));
        R_SEMANTIC_CHECK((diagnostic != NULL) &&
                         (strcmp(diagnostic->code, "R-DIAG-ASYNC-001") == 0) &&
                         (strcmp(diagnostic->rule_id, "R-FUNC-0012") == 0) &&
                         (diagnostic->phase == R_DIAGNOSTIC_PHASE_SEMANTIC));
        r_frontend_destroy(context);
    }
}

static void r_semantic_test_mir_pending_completion(void) {
    static const char source_text[] =
        "module semantic.mir_pending_completion;\n"
        "error pending_error { i32 code; };\n"
        "void checked_source() throws pending_error { throw {.code = 5}; }\n"
        "void normal() { try {} finally {} }\n"
        "i32 returning() { try { return 7; } finally {} }\n"
        "void propagating() throws pending_error {\n"
        "    try { throw {.code = 1}; } finally {}\n"
        "}\n"
        "void loops(bool repeat) {\n"
        "    while (true) {\n"
        "        array<u8> outer = {};\n"
        "        try {\n"
        "            own i32* inner = new i32(3);\n"
        "            if (repeat == true) { continue; } else { break; }\n"
        "        } finally {\n"
        "            const u8[] view = std.array::as_slice(&outer);\n"
        "            view as void;\n"
        "        }\n"
        "    }\n"
        "}\n"
        "void fallthrough_cleanup(i32 value) {\n"
        "    switch (value) {\n"
        "        case 0:\n"
        "            try { own i32* owner = new i32(4); } finally {}\n"
        "            fallthrough;\n"
        "        default:\n"
        "            break;\n"
        "    }\n"
        "}\n"
        "void catching() {\n"
        "    try {\n"
        "        try { throw pending_error {.code = 2}; } finally {}\n"
        "    } catch (pending_error error) { error.code as void; }\n"
        "}\n"
        "async void cancelling(task<void> operation) {\n"
        "    try { await move operation; } finally {}\n"
        "}\n"
        "void nested() {\n"
        "    try { try { return; } finally {} } finally {}\n"
        "}\n"
        "i32 staged_before_drop() {\n"
        "    try {\n"
        "        own i32* owner = new i32(3);\n"
        "        return 8;\n"
        "    } finally {}\n"
        "}\n"
        "i32 keep_move_alive(array<u8> value) {\n"
        "    try {\n"
        "        own i32* inner = new i32(5);\n"
        "        return 9;\n"
        "    } finally {\n"
        "        const u8[] view = std.array::as_slice(&value);\n"
        "        view as void;\n"
        "    }\n"
        "}\n"
        "i32 nested_cleanup(array<u8> outer) {\n"
        "    try {\n"
        "        array<u8> middle = {};\n"
        "        try {\n"
        "            array<u8> inner = {};\n"
        "            return 10;\n"
        "        } finally {\n"
        "            const u8[] middle_view = std.array::as_slice(&middle);\n"
        "            middle_view as void;\n"
        "        }\n"
        "    } finally {\n"
        "        const u8[] outer_view = std.array::as_slice(&outer);\n"
        "        outer_view as void;\n"
        "    }\n"
        "}\n"
        "i32 finally_consumes(array<u8> value) {\n"
        "    try { return 11; } finally { drop value; }\n"
        "}\n"
        "void automatic_cleanup(array<u8> outer) throws pending_error {\n"
        "    try {\n"
        "        own i32* inner = new i32(9);\n"
        "        checked_source();\n"
        "    } finally {\n"
        "        const u8[] view = std.array::as_slice(&outer);\n"
        "        view as void;\n"
        "    }\n"
        "}\n";
    RFrontendContext *context = r_frontend_create(NULL);
    RSemanticTestBuffer mir = {0};
    RSourceId source_id = R_SOURCE_ID_INVALID;
    size_t set_count = 0U;
    size_t resume_count = 0U;
    size_t finally_push_count = 0U;
    size_t finally_enter_count = 0U;
    size_t finally_exit_count = 0U;
    size_t reason_counts[R_MIR_PENDING_COMPLETION_PANIC_UNWIND + 1U] = {0};
    size_t checked_target_count = 0U;
    size_t checked_propagate_count = 0U;
    size_t staged_before_drop_count = 0U;
    size_t automatic_cleanup_order_count = 0U;
    size_t loop_cleanup_order_count = 0U;
    size_t loop_deferred_cleanup_order_count = 0U;
    size_t normal_cleanup_order_count = 0U;
    size_t nested_finally_edge_count = 0U;
    size_t nested_cleanup_order_count = 0U;
    size_t keep_alive_cleanup_order_count = 0U;
    size_t finally_consumes_drop_count = 0U;
    size_t automatic_deferred_cleanup_order_count = 0U;
    size_t caught_route_count = 0U;
    size_t hir_break_cleanup_count = 0U;
    size_t hir_continue_cleanup_count = 0U;
    size_t hir_effect_cleanup_segment_count = 0U;
    size_t hir_nested_return_cleanup_count = 0U;
    size_t hir_suppressed_drop_count = 0U;
    size_t hir_visible_return_cleanup_count = 0U;
    size_t hir_explicit_drop_count = 0U;
    size_t index;

    R_SEMANTIC_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    source_id = r_semantic_add_bytes(
        context, "mir-pending-completion.r", (const uint8_t *)source_text, strlen(source_text));
    (void)source_id;
    R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    for (index = 0U; index < context->hir_node_count; ++index) {
        const RHirNode *node = &context->hir_nodes[index];

        if ((node->kind == R_HIR_BREAK) || (node->kind == R_HIR_CONTINUE)) {
            const RHirNode *inner_cleanup = r_semantic_test_hir_child(context, node, UINT32_C(0));
            const RHirNode *outer_cleanup = r_semantic_test_hir_child(context, node, UINT32_C(1));

            R_SEMANTIC_CHECK(node->child_count == UINT32_C(2));
            R_SEMANTIC_CHECK((inner_cleanup != NULL) && (inner_cleanup->kind == R_HIR_DROP) &&
                             (inner_cleanup->child_count == UINT32_C(1)) &&
                             (inner_cleanup->integer_value == UINT64_C(0)));
            R_SEMANTIC_CHECK((outer_cleanup != NULL) && (outer_cleanup->kind == R_HIR_DROP) &&
                             (outer_cleanup->child_count == UINT32_C(1)) &&
                             (outer_cleanup->integer_value == UINT64_C(1)));
            if (node->kind == R_HIR_BREAK) {
                hir_break_cleanup_count += 1U;
            } else {
                hir_continue_cleanup_count += 1U;
            }
        }
        if (node->kind == R_HIR_RETURN) {
            const RHirNode *drop0 = r_semantic_test_hir_child(context, node, UINT32_C(1));
            const RHirNode *drop1 = r_semantic_test_hir_child(context, node, UINT32_C(2));
            const RHirNode *drop2 = r_semantic_test_hir_child(context, node, UINT32_C(3));

            if ((node->child_count == UINT32_C(3)) && (drop0 != NULL) &&
                (drop0->kind == R_HIR_DROP) && (drop0->integer_value == UINT64_C(0)) &&
                (drop1 != NULL) && (drop1->kind == R_HIR_DROP) &&
                (drop1->integer_value == UINT64_C(1))) {
                hir_visible_return_cleanup_count += 1U;
            }
            if ((node->child_count == UINT32_C(4)) && (drop0 != NULL) &&
                (drop0->kind == R_HIR_DROP) && (drop0->integer_value == UINT64_C(0)) &&
                (drop1 != NULL) && (drop1->kind == R_HIR_DROP) &&
                (drop1->integer_value == UINT64_C(1)) && (drop2 != NULL) &&
                (drop2->kind == R_HIR_DROP) && (drop2->integer_value == UINT64_C(2))) {
                hir_nested_return_cleanup_count += 1U;
            }
        }
        if ((node->kind == R_HIR_DROP) && (node->operation == R_TOKEN_INVALID) &&
            (node->integer_value == UINT64_MAX)) {
            hir_suppressed_drop_count += 1U;
        } else if ((node->kind == R_HIR_DROP) && (node->operation == R_TOKEN_KW_DROP)) {
            hir_explicit_drop_count += 1U;
        }
        if (node->effect_exit_count != UINT32_C(0)) {
            const bool effect_range_valid =
                ((size_t)node->first_effect_exit <= context->hir_effect_exit_count) &&
                ((size_t)node->effect_exit_count <=
                 context->hir_effect_exit_count - (size_t)node->first_effect_exit);
            uint32_t effect_index;

            R_SEMANTIC_CHECK(effect_range_valid);
            if (!effect_range_valid) {
                continue;
            }
            for (effect_index = UINT32_C(0); effect_index < node->effect_exit_count;
                 ++effect_index) {
                const RHirEffectExit *effect_exit =
                    &context->hir_effect_exits[(size_t)node->first_effect_exit + effect_index];
                const RHirNode *cleanup_block =
                    (effect_exit->cleanup_block == R_HIR_NODE_ID_INVALID) ||
                            ((size_t)effect_exit->cleanup_block > context->hir_node_count)
                        ? NULL
                        : &context->hir_nodes[(size_t)effect_exit->cleanup_block - 1U];
                const RHirNode *inner_cleanup =
                    r_semantic_test_hir_child(context, cleanup_block, UINT32_C(0));
                const RHirNode *outer_cleanup =
                    r_semantic_test_hir_child(context, cleanup_block, UINT32_C(1));

                if ((cleanup_block != NULL) && (cleanup_block->kind == R_HIR_BLOCK) &&
                    (cleanup_block->child_count == UINT32_C(2)) && (inner_cleanup != NULL) &&
                    (inner_cleanup->kind == R_HIR_DROP) &&
                    (inner_cleanup->integer_value == UINT64_C(0)) && (outer_cleanup != NULL) &&
                    (outer_cleanup->kind == R_HIR_DROP) &&
                    (outer_cleanup->integer_value == UINT64_C(1))) {
                    hir_effect_cleanup_segment_count += 1U;
                }
            }
        }
    }
    R_SEMANTIC_CHECK(hir_break_cleanup_count == 1U);
    R_SEMANTIC_CHECK(hir_continue_cleanup_count == 1U);
    R_SEMANTIC_CHECK(hir_effect_cleanup_segment_count == 1U);
    R_SEMANTIC_CHECK(hir_nested_return_cleanup_count == 1U);
    R_SEMANTIC_CHECK(hir_visible_return_cleanup_count == 1U);
    R_SEMANTIC_CHECK(hir_suppressed_drop_count == 1U);
    R_SEMANTIC_CHECK(hir_explicit_drop_count == 1U);
    R_SEMANTIC_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    for (index = 0U; index < context->mir_instruction_count; ++index) {
        const RMirInstruction *instruction = &context->mir_instructions[index];

        if ((instruction->kind == R_MIR_INSTRUCTION_PENDING_SET) ||
            (instruction->kind == R_MIR_INSTRUCTION_PENDING_RESUME)) {
            const RMirPendingCompletionReason reason =
                (RMirPendingCompletionReason)instruction->integer_value;

            R_SEMANTIC_CHECK(reason > R_MIR_PENDING_COMPLETION_INVALID);
            R_SEMANTIC_CHECK(reason <= R_MIR_PENDING_COMPLETION_PANIC_UNWIND);
            if (reason <= R_MIR_PENDING_COMPLETION_PANIC_UNWIND) {
                reason_counts[reason] +=
                    instruction->kind == R_MIR_INSTRUCTION_PENDING_SET ? 1U : 0U;
            }
            if (instruction->kind == R_MIR_INSTRUCTION_PENDING_SET) {
                set_count += 1U;
                R_SEMANTIC_CHECK(instruction->target1 != R_MIR_BLOCK_ID_INVALID);
                R_SEMANTIC_CHECK(instruction->aggregate_member != UINT32_C(0));
                if (reason == R_MIR_PENDING_COMPLETION_CHECKED_ERROR) {
                    R_SEMANTIC_CHECK(instruction->type != R_TYPE_ID_INVALID);
                    R_SEMANTIC_CHECK(instruction->operand0 != R_MIR_VALUE_ID_INVALID);
                    if (instruction->target0 == R_MIR_BLOCK_ID_INVALID) {
                        checked_propagate_count += 1U;
                    } else {
                        checked_target_count += 1U;
                    }
                } else if ((reason == R_MIR_PENDING_COMPLETION_NORMAL) ||
                           (reason == R_MIR_PENDING_COMPLETION_BREAK) ||
                           (reason == R_MIR_PENDING_COMPLETION_CONTINUE)) {
                    R_SEMANTIC_CHECK(instruction->target0 != R_MIR_BLOCK_ID_INVALID);
                } else if (reason == R_MIR_PENDING_COMPLETION_CANCEL) {
                    R_SEMANTIC_CHECK(instruction->type == R_TYPE_ID_INVALID);
                    R_SEMANTIC_CHECK(instruction->operand0 == R_MIR_VALUE_ID_INVALID);
                    R_SEMANTIC_CHECK(instruction->target0 == R_MIR_BLOCK_ID_INVALID);
                }
            } else {
                resume_count += 1U;
            }
        } else if (instruction->kind == R_MIR_INSTRUCTION_FINALLY_PUSH) {
            finally_push_count += 1U;
            R_SEMANTIC_CHECK(instruction->integer_value != UINT64_C(0));
            R_SEMANTIC_CHECK(instruction->target0 != R_MIR_BLOCK_ID_INVALID);
        } else if (instruction->kind == R_MIR_INSTRUCTION_FINALLY_ENTER) {
            finally_enter_count += 1U;
            R_SEMANTIC_CHECK(instruction->integer_value != UINT64_C(0));
        } else if (instruction->kind == R_MIR_INSTRUCTION_FINALLY_EXIT) {
            finally_exit_count += 1U;
            R_SEMANTIC_CHECK(instruction->integer_value != UINT64_C(0));
        }
    }
    for (index = 0U; index < context->mir_function_count; ++index) {
        const RMirFunction *function = &context->mir_functions[index];
        const bool is_automatic_cleanup =
            r_semantic_test_symbol_named(context, function->symbol, "automatic_cleanup");
        const bool is_catching =
            r_semantic_test_symbol_named(context, function->symbol, "catching");
        const bool is_finally_consumes =
            r_semantic_test_symbol_named(context, function->symbol, "finally_consumes");
        const bool is_keep_move_alive =
            r_semantic_test_symbol_named(context, function->symbol, "keep_move_alive");
        const bool is_loops = r_semantic_test_symbol_named(context, function->symbol, "loops");
        const bool is_nested_cleanup =
            r_semantic_test_symbol_named(context, function->symbol, "nested_cleanup");
        uint32_t block_index;

        for (block_index = UINT32_C(0); block_index < function->block_count; ++block_index) {
            const RMirBlock *block =
                &context->mir_blocks[(size_t)function->first_block + (size_t)block_index];
            uint32_t instruction_index;

            for (instruction_index = UINT32_C(0); instruction_index < block->instruction_count;
                 ++instruction_index) {
                const RMirInstruction *instruction =
                    &context->mir_instructions[(size_t)block->first_instruction +
                                               (size_t)instruction_index];

                if (instruction->kind == R_MIR_INSTRUCTION_FINALLY_PUSH) {
                    size_t matching_push = 0U;
                    size_t matching_enter = 0U;
                    size_t matching_exit = 0U;
                    uint32_t scan_block;

                    for (scan_block = UINT32_C(0); scan_block < function->block_count;
                         ++scan_block) {
                        const RMirBlock *candidate =
                            &context
                                 ->mir_blocks[(size_t)function->first_block + (size_t)scan_block];
                        uint32_t scan_instruction;

                        for (scan_instruction = UINT32_C(0);
                             scan_instruction < candidate->instruction_count;
                             ++scan_instruction) {
                            const RMirInstruction *marker =
                                &context->mir_instructions[(size_t)candidate->first_instruction +
                                                           (size_t)scan_instruction];

                            if (marker->integer_value != instruction->integer_value) {
                                continue;
                            }
                            matching_push +=
                                marker->kind == R_MIR_INSTRUCTION_FINALLY_PUSH ? 1U : 0U;
                            matching_enter +=
                                marker->kind == R_MIR_INSTRUCTION_FINALLY_ENTER ? 1U : 0U;
                            matching_exit +=
                                marker->kind == R_MIR_INSTRUCTION_FINALLY_EXIT ? 1U : 0U;
                        }
                    }
                    R_SEMANTIC_CHECK(matching_push == 1U);
                    R_SEMANTIC_CHECK(matching_enter == 1U);
                    R_SEMANTIC_CHECK(matching_exit == 1U);
                }
                if (instruction->kind == R_MIR_INSTRUCTION_FINALLY_EXIT) {
                    if (instruction->target0 == R_MIR_BLOCK_ID_INVALID) {
                        R_SEMANTIC_CHECK(instruction->place_ordinal == UINT32_C(0));
                    } else {
                        const size_t target_index =
                            (size_t)function->first_block + (size_t)instruction->target0 - 1U;
                        const RMirBlock *target = target_index < context->mir_block_count
                                                      ? &context->mir_blocks[target_index]
                                                      : NULL;
                        const RMirInstruction *enter =
                            (target == NULL) || (target->instruction_count == UINT32_C(0))
                                ? NULL
                                : &context->mir_instructions[target->first_instruction];

                        R_SEMANTIC_CHECK((enter != NULL) &&
                                         (enter->kind == R_MIR_INSTRUCTION_FINALLY_ENTER));
                        if (enter != NULL) {
                            R_SEMANTIC_CHECK(enter->place_ordinal + UINT32_C(1) ==
                                             instruction->place_ordinal);
                        }
                        nested_finally_edge_count += 1U;
                    }
                }
                if (instruction->kind == R_MIR_INSTRUCTION_PENDING_SET) {
                    const size_t resume_index =
                        (size_t)function->first_block + (size_t)instruction->target1 - 1U;
                    const RMirBlock *resume = resume_index < context->mir_block_count
                                                  ? &context->mir_blocks[resume_index]
                                                  : NULL;
                    const RMirInstruction *resume_marker =
                        (resume == NULL) || (resume->instruction_count == UINT32_C(0))
                            ? NULL
                            : &context->mir_instructions[resume->first_instruction];

                    R_SEMANTIC_CHECK((resume_marker != NULL) &&
                                     (resume_marker->kind == R_MIR_INSTRUCTION_PENDING_RESUME));
                    if (resume_marker != NULL) {
                        R_SEMANTIC_CHECK(resume_marker->integer_value ==
                                         instruction->integer_value);
                        R_SEMANTIC_CHECK(resume_marker->type == instruction->type);
                        R_SEMANTIC_CHECK(resume_marker->operand0 == instruction->operand0);
                        R_SEMANTIC_CHECK(resume_marker->target0 == instruction->target0);
                        R_SEMANTIC_CHECK(resume_marker->target1 == instruction->target1);
                        R_SEMANTIC_CHECK(resume_marker->place_ordinal ==
                                         instruction->place_ordinal);
                        R_SEMANTIC_CHECK(resume_marker->aggregate_member ==
                                         instruction->aggregate_member);
                    }
                }
                if (is_nested_cleanup && (instruction->kind == R_MIR_INSTRUCTION_PENDING_SET) &&
                    (instruction->integer_value == (uint64_t)R_MIR_PENDING_COMPLETION_RETURN) &&
                    (instruction->place_ordinal == UINT32_C(1))) {
                    const RMirBlock *inner_resume =
                        r_semantic_test_mir_target_block(context, function, instruction->target1);
                    const RMirInstruction *inner_resume_marker =
                        r_semantic_test_mir_block_instruction(context, inner_resume, UINT32_C(0));
                    const RMirInstruction *outer_pending =
                        r_semantic_test_mir_block_instruction(context, inner_resume, UINT32_C(1));
                    const RMirBlock *outer_resume =
                        outer_pending == NULL ? NULL
                                              : r_semantic_test_mir_target_block(
                                                    context, function, outer_pending->target1);
                    const RMirInstruction *outer_resume_marker =
                        r_semantic_test_mir_block_instruction(context, outer_resume, UINT32_C(0));

                    if (r_semantic_test_mir_cleanup_segment(context,
                                                            block,
                                                            instruction_index + UINT32_C(1),
                                                            "inner",
                                                            R_MIR_INSTRUCTION_JUMP) &&
                        (inner_resume_marker != NULL) &&
                        (inner_resume_marker->kind == R_MIR_INSTRUCTION_PENDING_RESUME) &&
                        (outer_pending != NULL) &&
                        (outer_pending->kind == R_MIR_INSTRUCTION_PENDING_SET) &&
                        (outer_pending->place_ordinal == UINT32_C(0)) &&
                        r_semantic_test_mir_cleanup_segment(
                            context, inner_resume, UINT32_C(2), "middle", R_MIR_INSTRUCTION_JUMP) &&
                        (outer_resume_marker != NULL) &&
                        (outer_resume_marker->kind == R_MIR_INSTRUCTION_PENDING_RESUME) &&
                        r_semantic_test_mir_cleanup_segment(context,
                                                            outer_resume,
                                                            UINT32_C(1),
                                                            "outer",
                                                            R_MIR_INSTRUCTION_RETURN)) {
                        nested_cleanup_order_count += 1U;
                    }
                }
                if (is_keep_move_alive && (instruction->kind == R_MIR_INSTRUCTION_PENDING_SET) &&
                    (instruction->integer_value == (uint64_t)R_MIR_PENDING_COMPLETION_RETURN)) {
                    const RMirBlock *resume =
                        r_semantic_test_mir_target_block(context, function, instruction->target1);
                    const RMirInstruction *resume_marker =
                        r_semantic_test_mir_block_instruction(context, resume, UINT32_C(0));

                    if (r_semantic_test_mir_cleanup_segment(context,
                                                            block,
                                                            instruction_index + UINT32_C(1),
                                                            "inner",
                                                            R_MIR_INSTRUCTION_JUMP) &&
                        (resume_marker != NULL) &&
                        (resume_marker->kind == R_MIR_INSTRUCTION_PENDING_RESUME) &&
                        r_semantic_test_mir_cleanup_segment(
                            context, resume, UINT32_C(1), "value", R_MIR_INSTRUCTION_RETURN)) {
                        keep_alive_cleanup_order_count += 1U;
                    }
                }
                if (is_loops && (instruction->kind == R_MIR_INSTRUCTION_PENDING_SET) &&
                    ((instruction->integer_value == (uint64_t)R_MIR_PENDING_COMPLETION_BREAK) ||
                     (instruction->integer_value == (uint64_t)R_MIR_PENDING_COMPLETION_CONTINUE))) {
                    const RMirBlock *resume =
                        r_semantic_test_mir_target_block(context, function, instruction->target1);
                    const RMirInstruction *resume_marker =
                        r_semantic_test_mir_block_instruction(context, resume, UINT32_C(0));

                    if (r_semantic_test_mir_cleanup_segment(context,
                                                            block,
                                                            instruction_index + UINT32_C(1),
                                                            "inner",
                                                            R_MIR_INSTRUCTION_JUMP) &&
                        (resume_marker != NULL) &&
                        (resume_marker->kind == R_MIR_INSTRUCTION_PENDING_RESUME) &&
                        r_semantic_test_mir_cleanup_segment(
                            context, resume, UINT32_C(1), "outer", R_MIR_INSTRUCTION_JUMP)) {
                        loop_deferred_cleanup_order_count += 1U;
                    }
                }
                if (is_automatic_cleanup && (instruction->kind == R_MIR_INSTRUCTION_PENDING_SET) &&
                    (instruction->integer_value ==
                     (uint64_t)R_MIR_PENDING_COMPLETION_CHECKED_ERROR)) {
                    const RMirBlock *resume =
                        r_semantic_test_mir_target_block(context, function, instruction->target1);
                    const RMirInstruction *resume_marker =
                        r_semantic_test_mir_block_instruction(context, resume, UINT32_C(0));

                    if (r_semantic_test_mir_cleanup_segment(context,
                                                            block,
                                                            instruction_index + UINT32_C(1),
                                                            "inner",
                                                            R_MIR_INSTRUCTION_JUMP) &&
                        (resume_marker != NULL) &&
                        (resume_marker->kind == R_MIR_INSTRUCTION_PENDING_RESUME) &&
                        r_semantic_test_mir_cleanup_segment(
                            context, resume, UINT32_C(1), "outer", R_MIR_INSTRUCTION_THROW)) {
                        automatic_deferred_cleanup_order_count += 1U;
                    }
                }
                if (is_catching && (instruction->kind == R_MIR_INSTRUCTION_PENDING_SET) &&
                    (instruction->integer_value ==
                     (uint64_t)R_MIR_PENDING_COMPLETION_CHECKED_ERROR) &&
                    (instruction->target0 != R_MIR_BLOCK_ID_INVALID)) {
                    caught_route_count += 1U;
                }
                if (is_finally_consumes && (instruction->kind == R_MIR_INSTRUCTION_DROP) &&
                    r_semantic_test_symbol_named(context, instruction->symbol, "value")) {
                    finally_consumes_drop_count += 1U;
                }
                if ((instruction->kind == R_MIR_INSTRUCTION_PENDING_SET) &&
                    (instruction->integer_value == (uint64_t)R_MIR_PENDING_COMPLETION_RETURN) &&
                    (instruction->operand0 != R_MIR_VALUE_ID_INVALID)) {
                    uint32_t scan_instruction;
                    bool found_drop = false;

                    for (scan_instruction = instruction_index + UINT32_C(1);
                         scan_instruction < block->instruction_count;
                         ++scan_instruction) {
                        const RMirInstruction *candidate =
                            &context->mir_instructions[(size_t)block->first_instruction +
                                                       (size_t)scan_instruction];

                        if (candidate->kind == R_MIR_INSTRUCTION_DROP) {
                            found_drop = true;
                        }
                    }
                    staged_before_drop_count += found_drop ? 1U : 0U;
                }
                if ((instruction->kind == R_MIR_INSTRUCTION_PENDING_SET) &&
                    ((instruction->integer_value == (uint64_t)R_MIR_PENDING_COMPLETION_BREAK) ||
                     (instruction->integer_value == (uint64_t)R_MIR_PENDING_COMPLETION_CONTINUE))) {
                    bool found_drop = false;
                    bool found_jump_after_drop = false;
                    uint32_t scan_instruction;

                    for (scan_instruction = instruction_index + UINT32_C(1);
                         scan_instruction < block->instruction_count;
                         ++scan_instruction) {
                        const RMirInstruction *candidate =
                            &context->mir_instructions[(size_t)block->first_instruction +
                                                       (size_t)scan_instruction];

                        if (candidate->kind == R_MIR_INSTRUCTION_DROP) {
                            found_drop = true;
                        } else if (found_drop && (candidate->kind == R_MIR_INSTRUCTION_JUMP)) {
                            found_jump_after_drop = true;
                        }
                    }
                    loop_cleanup_order_count += found_drop && found_jump_after_drop ? 1U : 0U;
                }
                if ((instruction->kind == R_MIR_INSTRUCTION_PENDING_SET) &&
                    (instruction->integer_value == (uint64_t)R_MIR_PENDING_COMPLETION_NORMAL)) {
                    bool found_drop = false;
                    bool found_jump_after_drop = false;
                    uint32_t scan_instruction;

                    for (scan_instruction = instruction_index + UINT32_C(1);
                         scan_instruction < block->instruction_count;
                         ++scan_instruction) {
                        const RMirInstruction *candidate =
                            &context->mir_instructions[(size_t)block->first_instruction +
                                                       (size_t)scan_instruction];

                        if (candidate->kind == R_MIR_INSTRUCTION_DROP) {
                            found_drop = true;
                        } else if (found_drop && (candidate->kind == R_MIR_INSTRUCTION_JUMP)) {
                            found_jump_after_drop = true;
                        }
                    }
                    normal_cleanup_order_count += found_drop && found_jump_after_drop ? 1U : 0U;
                }
                if (instruction->kind == R_MIR_INSTRUCTION_EFFECT_PAYLOAD) {
                    bool found_pending = false;
                    bool found_drop_after_pending = false;
                    bool found_jump_after_drop = false;
                    uint32_t scan_instruction;

                    for (scan_instruction = instruction_index + UINT32_C(1);
                         scan_instruction < block->instruction_count;
                         ++scan_instruction) {
                        const RMirInstruction *candidate =
                            &context->mir_instructions[(size_t)block->first_instruction +
                                                       (size_t)scan_instruction];

                        if (candidate->kind == R_MIR_INSTRUCTION_PENDING_SET) {
                            found_pending = true;
                        } else if (found_pending && (candidate->kind == R_MIR_INSTRUCTION_DROP)) {
                            found_drop_after_pending = true;
                        } else if (found_drop_after_pending &&
                                   (candidate->kind == R_MIR_INSTRUCTION_JUMP)) {
                            found_jump_after_drop = true;
                        }
                    }
                    automatic_cleanup_order_count +=
                        found_pending && found_drop_after_pending && found_jump_after_drop ? 1U
                                                                                           : 0U;
                }
            }
        }
    }
    R_SEMANTIC_CHECK(set_count == 18U);
    R_SEMANTIC_CHECK(resume_count == set_count);
    R_SEMANTIC_CHECK(finally_push_count == 15U);
    R_SEMANTIC_CHECK(finally_enter_count == finally_push_count);
    R_SEMANTIC_CHECK(finally_exit_count == finally_enter_count);
    R_SEMANTIC_CHECK(reason_counts[R_MIR_PENDING_COMPLETION_NORMAL] == 4U);
    R_SEMANTIC_CHECK(reason_counts[R_MIR_PENDING_COMPLETION_RETURN] == 8U);
    R_SEMANTIC_CHECK(reason_counts[R_MIR_PENDING_COMPLETION_CHECKED_ERROR] == 3U);
    R_SEMANTIC_CHECK(reason_counts[R_MIR_PENDING_COMPLETION_BREAK] == 1U);
    R_SEMANTIC_CHECK(reason_counts[R_MIR_PENDING_COMPLETION_CONTINUE] == 1U);
    R_SEMANTIC_CHECK(reason_counts[R_MIR_PENDING_COMPLETION_CANCEL] == 1U);
    R_SEMANTIC_CHECK(reason_counts[R_MIR_PENDING_COMPLETION_FALLTHROUGH] == 0U);
    R_SEMANTIC_CHECK(reason_counts[R_MIR_PENDING_COMPLETION_PANIC_UNWIND] == 0U);
    R_SEMANTIC_CHECK(checked_target_count == 1U);
    R_SEMANTIC_CHECK(checked_propagate_count == 2U);
    R_SEMANTIC_CHECK(staged_before_drop_count == 4U);
    R_SEMANTIC_CHECK(automatic_cleanup_order_count == 1U);
    R_SEMANTIC_CHECK(loop_cleanup_order_count == 2U);
    R_SEMANTIC_CHECK(loop_deferred_cleanup_order_count == 2U);
    R_SEMANTIC_CHECK(normal_cleanup_order_count == 2U);
    R_SEMANTIC_CHECK(nested_finally_edge_count == 2U);
    R_SEMANTIC_CHECK(nested_cleanup_order_count == 1U);
    R_SEMANTIC_CHECK(keep_alive_cleanup_order_count == 1U);
    R_SEMANTIC_CHECK(finally_consumes_drop_count == 1U);
    R_SEMANTIC_CHECK(automatic_deferred_cleanup_order_count == 1U);
    R_SEMANTIC_CHECK(caught_route_count == 1U);
    R_SEMANTIC_CHECK(r_frontend_dump_mir(context, r_semantic_test_write, &mir) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK((mir.bytes != NULL) &&
                     (strstr(mir.bytes, "(pending_set reason=normal target=bb") != NULL) &&
                     (strstr(mir.bytes, "(pending_set reason=return type=") != NULL) &&
                     (strstr(mir.bytes, " payload=%") != NULL) &&
                     (strstr(mir.bytes, "(pending_set reason=checked_error type=") != NULL) &&
                     (strstr(mir.bytes, "(pending_set reason=break target=bb") != NULL) &&
                     (strstr(mir.bytes, "(pending_set reason=continue target=bb") != NULL) &&
                     (strstr(mir.bytes, "(pending_set reason=cancel resume=bb") != NULL) &&
                     (strstr(mir.bytes, "(pending_resume reason=return type=") != NULL) &&
                     (strstr(mir.bytes, "(finally_push id=") != NULL) &&
                     (strstr(mir.bytes, " stop_depth=") != NULL) &&
                     (strstr(mir.bytes, " first_finally=") != NULL));
    free(mir.bytes);
    r_frontend_destroy(context);
}

static RFrontendContext *
r_semantic_prepare_pending_cleanup_oom_context(RSemanticTestAllocator *allocator) {
    static const char source_text[] =
        "module semantic.pending_cleanup_oom;\n"
        "i32 nested(array<u8> outer) {\n"
        "    try {\n"
        "        array<u8> middle = {};\n"
        "        try {\n"
        "            array<u8> inner = {};\n"
        "            return 1;\n"
        "        } finally {\n"
        "            const u8[] view = std.array::as_slice(&middle);\n"
        "            view as void;\n"
        "        }\n"
        "    } finally {\n"
        "        const u8[] view = std.array::as_slice(&outer);\n"
        "        view as void;\n"
        "    }\n"
        "}\n"
        "i32 consumes(array<u8> value) {\n"
        "    try { return 2; } finally { drop value; }\n"
        "}\n";
    RFrontendOptions options = r_frontend_default_options();
    RFrontendContext *context;
    RSourceId source_id = R_SOURCE_ID_INVALID;
    RAstNodeId ast = R_AST_NODE_ID_INVALID;

    options.allocate = r_semantic_test_allocate;
    options.free = r_semantic_test_free;
    options.allocator_user_data = allocator;
    context = r_frontend_create(&options);
    if (context == NULL) {
        return NULL;
    }
    if ((r_frontend_add_source(context,
                               "pending-cleanup-oom.r",
                               (const uint8_t *)source_text,
                               strlen(source_text),
                               &source_id) != R_FRONTEND_OK) ||
        (r_frontend_lower_ast(context, source_id, &ast) != R_FRONTEND_OK)) {
        r_frontend_destroy(context);
        return NULL;
    }
    return context;
}

static void r_semantic_test_pending_cleanup_oom_sweep(void) {
    RSemanticTestAllocator baseline_allocator = {0};
    RFrontendContext *baseline =
        r_semantic_prepare_pending_cleanup_oom_context(&baseline_allocator);
    size_t before;
    size_t semantic_allocations;
    size_t mir_allocations;
    size_t offset;

    R_SEMANTIC_CHECK(baseline != NULL);
    if (baseline == NULL) {
        return;
    }
    before = baseline_allocator.call_count;
    R_SEMANTIC_CHECK(r_frontend_analyze(baseline) == R_FRONTEND_OK);
    semantic_allocations = baseline_allocator.call_count - before;
    before = baseline_allocator.call_count;
    R_SEMANTIC_CHECK(r_frontend_lower_mir(baseline) == R_FRONTEND_OK);
    mir_allocations = baseline_allocator.call_count - before;
    r_frontend_destroy(baseline);
    R_SEMANTIC_CHECK(baseline_allocator.live_count == 0U);
    R_SEMANTIC_CHECK(semantic_allocations != 0U);
    R_SEMANTIC_CHECK(mir_allocations != 0U);

    for (offset = 1U; offset <= semantic_allocations; ++offset) {
        RSemanticTestAllocator allocator = {0};
        RFrontendContext *context = r_semantic_prepare_pending_cleanup_oom_context(&allocator);
        RFrontendStatus status;

        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        allocator.fail_at = allocator.call_count + offset;
        status = r_frontend_analyze(context);
        R_SEMANTIC_CHECK((status == R_FRONTEND_OUT_OF_MEMORY) ||
                         (status == R_FRONTEND_LIMIT_EXCEEDED));
        R_SEMANTIC_CHECK(r_frontend_hir_root(context) == R_HIR_NODE_ID_INVALID);
        r_frontend_destroy(context);
        R_SEMANTIC_CHECK(allocator.live_count == 0U);
    }
    for (offset = 1U; offset <= mir_allocations; ++offset) {
        RSemanticTestAllocator allocator = {0};
        RFrontendContext *context = r_semantic_prepare_pending_cleanup_oom_context(&allocator);
        RFrontendStatus status;

        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
        allocator.fail_at = allocator.call_count + offset;
        status = r_frontend_lower_mir(context);
        R_SEMANTIC_CHECK((status == R_FRONTEND_OUT_OF_MEMORY) ||
                         (status == R_FRONTEND_LIMIT_EXCEEDED));
        R_SEMANTIC_CHECK(context->mir_function_count == 0U);
        R_SEMANTIC_CHECK(context->mir_block_count == 0U);
        R_SEMANTIC_CHECK(context->mir_instruction_count == 0U);
        R_SEMANTIC_CHECK(context->mir_operand_count == 0U);
        r_frontend_destroy(context);
        R_SEMANTIC_CHECK(allocator.live_count == 0U);
    }
}

static void r_semantic_test_effectful_thread_handle_resolution(void) {
    static const char positive_source[] =
        "module semantic.effectful_thread_handle_resolution_positive;\n"
        "error worker_error { i32 code; };\n"
        "i32 spawn_worker(i32 value, array<u8> payload) throws worker_error {\n"
        "    drop payload;\n"
        "    return value;\n"
        "}\n"
        "void spawn_unscoped(array<u8> payload) throws std.thread::thread_error {\n"
        "    std.thread::join_handle<i32 throws worker_error> handle =\n"
        "        std.thread::spawn(spawn_worker, 7, move payload);\n"
        "    std.thread::detach(move handle);\n"
        "}\n"
        "void spawn_scoped(array<u8> payload)\n"
        "    throws std.thread::thread_error, worker_error {\n"
        "    thread_scope {\n"
        "        std.thread::scoped_join_handle<i32 throws worker_error> handle =\n"
        "            std.thread::spawn_scoped(spawn_worker, 9, move payload);\n"
        "        std.thread::join_result<i32> outcome = std.thread::join(move handle);\n"
        "        drop outcome;\n"
        "    }\n"
        "}\n"
        "void spawn_failure_preserves_move(array<u8> payload) {\n"
        "    try {\n"
        "        std.thread::join_handle<i32 throws worker_error> handle =\n"
        "            std.thread::spawn(spawn_worker, 11, move payload);\n"
        "        std.thread::detach(move handle);\n"
        "    } catch (std.thread::thread_error error) {\n"
        "        drop payload;\n"
        "        error as void;\n"
        "    }\n"
        "}\n"
        "void detach_fallible(\n"
        "    std.thread::join_handle<i32 throws worker_error> handle) {\n"
        "    std.thread::detach(move handle);\n"
        "}\n"
        "void join_caught(\n"
        "    std.thread::join_handle<i32 throws worker_error> handle) {\n"
        "    try {\n"
        "        std.thread::join_result<i32> outcome = std.thread::join(move handle);\n"
        "        drop outcome;\n"
        "    } catch (worker_error error) {\n"
        "        error as void;\n"
        "    }\n"
        "}\n"
        "void join_scoped(\n"
        "    std.thread::scoped_join_handle<i32 throws worker_error> handle)\n"
        "    throws worker_error {\n"
        "    std.thread::join_result<i32> outcome = std.thread::join(move handle);\n"
        "    drop outcome;\n"
        "}\n"
        "void implicitly_detach_effect_free(std.thread::join_handle<i32> handle) {}\n"
        "void implicitly_join_effect_free(\n"
        "    std.thread::scoped_join_handle<i32> handle) {}\n";
    static const struct {
        const char *source;
        const char *code;
        const char *rule_id;
        size_t diagnostic_count;
    } negative_cases[] = {
        {
            "module semantic.effectful_thread_unscoped_fallthrough;\n"
            "error worker_error { i32 code; };\n"
            "void unresolved(\n"
            "    std.thread::join_handle<i32 throws worker_error> handle) {}\n",
            "R-DIAG-MEM-001",
            "R-MEM-0017",
            1U,
        },
        {
            "module semantic.effectful_thread_scoped_fallthrough;\n"
            "error worker_error { i32 code; };\n"
            "void unresolved(\n"
            "    std.thread::scoped_join_handle<i32 throws worker_error> handle) {}\n",
            "R-DIAG-MEM-001",
            "R-MEM-0016",
            1U,
        },
        {
            "module semantic.effectful_thread_unscoped_drop;\n"
            "error worker_error { i32 code; };\n"
            "void unresolved(\n"
            "    std.thread::join_handle<i32 throws worker_error> handle) {\n"
            "    drop handle;\n"
            "}\n",
            "R-DIAG-MEM-001",
            "R-MEM-0017",
            1U,
        },
        {
            "module semantic.effectful_thread_scoped_detach;\n"
            "error worker_error { i32 code; };\n"
            "void invalid(\n"
            "    std.thread::scoped_join_handle<i32 throws worker_error> handle) {\n"
            "    std.thread::detach(move handle);\n"
            "}\n",
            "R-DIAG-TYPE-001",
            "R-LIB-0010",
            1U,
        },
        {
            "module semantic.effectful_thread_join_missing_move;\n"
            "error worker_error { i32 code; };\n"
            "void invalid(\n"
            "    std.thread::join_handle<i32 throws worker_error> handle)\n"
            "    throws worker_error {\n"
            "    std.thread::join_result<i32> outcome = std.thread::join(handle);\n"
            "    drop outcome;\n"
            "}\n",
            "R-DIAG-MOVE-001",
            "R-OWN-0003",
            1U,
        },
        {
            "module semantic.effectful_thread_branch;\n"
            "error worker_error { i32 code; };\n"
            "void unresolved(bool detach,\n"
            "    std.thread::join_handle<i32 throws worker_error> handle) {\n"
            "    if (detach == true) {\n"
            "        std.thread::detach(move handle);\n"
            "    } else {\n"
            "    }\n"
            "}\n",
            "R-DIAG-MEM-001",
            "R-MEM-0017",
            1U,
        },
        {
            "module semantic.thread_spawn_scoped_outside_scope;\n"
            "i32 worker() { return 1; }\n"
            "void invalid() throws std.thread::thread_error {\n"
            "    std.thread::spawn_scoped(worker);\n"
            "}\n",
            "R-DIAG-MEM-001",
            "R-MEM-0015",
            1U,
        },
        {
            "module semantic.thread_spawn_async_entry;\n"
            "async i32 worker() { return 1; }\n"
            "void invalid() throws std.thread::thread_error {\n"
            "    std.thread::spawn(worker);\n"
            "}\n",
            "R-DIAG-TYPE-001",
            "R-MEM-0013",
            1U,
        },
        {
            "module semantic.thread_spawn_wrong_arity;\n"
            "i32 worker(i32 value) { return value; }\n"
            "void invalid() throws std.thread::thread_error {\n"
            "    std.thread::spawn(worker);\n"
            "}\n",
            "R-DIAG-TYPE-001",
            "R-MEM-0013",
            1U,
        },
        {
            "module semantic.thread_spawn_non_send;\n"
            "i32 worker(rc i32 value) { drop value; return 1; }\n"
            "void invalid(rc i32 value) throws std.thread::thread_error {\n"
            "    std.thread::spawn(worker, move value);\n"
            "}\n",
            "R-DIAG-MEM-001",
            "R-MEM-0013",
            1U,
        },
        {
            "module semantic.thread_spawn_unhandled_error;\n"
            "i32 worker() { return 1; }\n"
            "void invalid() { std.thread::spawn(worker); }\n",
            "R-DIAG-EFFECT-001",
            "R-ERR-0001",
            2U,
        },
        {
            "module semantic.thread_spawn_repeated_staged_move;\n"
            "i32 worker(array<u8> first, array<u8> second) {\n"
            "    drop first; drop second; return 1;\n"
            "}\n"
            "void invalid(array<u8> value) throws std.thread::thread_error {\n"
            "    std.thread::spawn(worker, move value, move value);\n"
            "}\n",
            "R-DIAG-BORROW-001",
            "R-FUNC-0010",
            2U,
        },
    };
    RFrontendContext *context = r_frontend_create(NULL);
    RSemanticTestBuffer hir = {0};
    RSemanticTestBuffer mir = {0};
    RSourceId source_id = R_SOURCE_ID_INVALID;
    size_t case_index;

    R_SEMANTIC_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    source_id = r_semantic_add_bytes(context,
                                     "effectful-thread-handle-resolution-positive.r",
                                     (const uint8_t *)positive_source,
                                     strlen(positive_source));
    (void)source_id;
    R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    R_SEMANTIC_CHECK(r_frontend_dump_hir(context, r_semantic_test_write, &hir) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK((hir.bytes != NULL) &&
                     (strstr(hir.bytes, "operation=std.thread::spawn ") != NULL) &&
                     (strstr(hir.bytes, "operation=std.thread::spawn_scoped") != NULL) &&
                     (strstr(hir.bytes, "entry=\"spawn_worker\"") != NULL) &&
                     (strstr(hir.bytes, "region=1") != NULL) &&
                     (strstr(hir.bytes, "operation=std.thread::join") != NULL) &&
                     (strstr(hir.bytes, "operation=std.thread::detach") != NULL));
    R_SEMANTIC_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    R_SEMANTIC_CHECK(r_frontend_dump_mir(context, r_semantic_test_write, &mir) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK((mir.bytes != NULL) &&
                     (strstr(mir.bytes, "operation=std.thread::spawn ") != NULL) &&
                     (strstr(mir.bytes, "operation=std.thread::spawn_scoped") != NULL) &&
                     (strstr(mir.bytes, "entry=\"spawn_worker\"") != NULL) &&
                     (strstr(mir.bytes, "async_staged=true") != NULL) &&
                     (strstr(mir.bytes, "operation=std.thread::join") != NULL) &&
                     (strstr(mir.bytes, "operation=std.thread::detach") != NULL));
    free(hir.bytes);
    free(mir.bytes);
    r_frontend_destroy(context);

    for (case_index = 0U; case_index < (sizeof(negative_cases) / sizeof(negative_cases[0]));
         ++case_index) {
        const RDiagnostic *diagnostic;

        context = r_frontend_create(NULL);
        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        source_id = r_semantic_add_bytes(context,
                                         "effectful-thread-handle-resolution-negative.r",
                                         (const uint8_t *)negative_cases[case_index].source,
                                         strlen(negative_cases[case_index].source));
        (void)source_id;
        R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
        R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) ==
                         negative_cases[case_index].diagnostic_count);
        diagnostic = r_frontend_diagnostic(context, UINT32_C(0));
        R_SEMANTIC_CHECK((diagnostic != NULL) &&
                         (strcmp(diagnostic->code, negative_cases[case_index].code) == 0) &&
                         (strcmp(diagnostic->rule_id, negative_cases[case_index].rule_id) == 0) &&
                         (diagnostic->phase == R_DIAGNOSTIC_PHASE_SEMANTIC));
        r_frontend_destroy(context);
    }
}

static void r_semantic_test_async_stderr(void) {
    static const char positive_source[] = "module semantic.async_stderr_positive;\n"
                                          "async i32 main() {\n"
                                          "    std.io::output error = std.io::stderr();\n"
                                          "    std.io::output moved = move error;\n"
                                          "    return 0;\n"
                                          "}\n";
    static const struct {
        const char *source;
        const char *code;
        const char *rule_id;
    } negative_cases[] = {
        {
            "module semantic.async_stderr_arity;\n"
            "i32 main() { std.io::output error = std.io::stderr(1); return 0; }\n",
            "R-DIAG-TYPE-001",
            "R-SLIB-IO-0002",
        },
        {
            "module semantic.async_stderr_context;\n"
            "i32 main() { std.io::input input = std.io::stderr(); return 0; }\n",
            "R-DIAG-TYPE-001",
            "R-SLIB-IO-0002",
        },
    };
    RFrontendContext *context = r_frontend_create(NULL);
    RSemanticTestBuffer hir = {0};
    RSemanticTestBuffer mir = {0};
    RSourceId source_id = R_SOURCE_ID_INVALID;
    const char *cursor;
    size_t operation_count = 0U;
    size_t index;

    R_SEMANTIC_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_SEMANTIC_CHECK(r_frontend_add_source(context,
                                           "async-stderr-positive.r",
                                           (const uint8_t *)positive_source,
                                           strlen(positive_source),
                                           &source_id) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    R_SEMANTIC_CHECK(r_frontend_dump_hir(context, r_semantic_test_write, &hir) == R_FRONTEND_OK);
    if (hir.bytes != NULL) {
        cursor = hir.bytes;
        while ((cursor = strstr(cursor, "operation=std.io::stderr")) != NULL) {
            operation_count += 1U;
            cursor += sizeof("operation=std.io::stderr") - 1U;
        }
    }
    R_SEMANTIC_CHECK(operation_count == 1U);
    R_SEMANTIC_CHECK((hir.bytes != NULL) &&
                     (strstr(hir.bytes, "(standard \"std.io::output\")") != NULL));
    R_SEMANTIC_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    R_SEMANTIC_CHECK(r_frontend_dump_mir(context, r_semantic_test_write, &mir) == R_FRONTEND_OK);
    operation_count = 0U;
    if (mir.bytes != NULL) {
        cursor = mir.bytes;
        while ((cursor = strstr(cursor, "operation=std.io::stderr")) != NULL) {
            operation_count += 1U;
            cursor += sizeof("operation=std.io::stderr") - 1U;
        }
    }
    R_SEMANTIC_CHECK(operation_count == 1U);
    free(mir.bytes);
    free(hir.bytes);
    r_frontend_destroy(context);

    for (index = 0U; index < (sizeof(negative_cases) / sizeof(negative_cases[0])); ++index) {
        const RDiagnostic *diagnostic;

        context = r_frontend_create(NULL);
        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        source_id = r_semantic_add_bytes(context,
                                         "async-stderr-negative.r",
                                         (const uint8_t *)negative_cases[index].source,
                                         strlen(negative_cases[index].source));
        (void)source_id;
        R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
        R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 1U);
        diagnostic = r_frontend_diagnostic(context, UINT32_C(0));
        R_SEMANTIC_CHECK((diagnostic != NULL) &&
                         (strcmp(diagnostic->code, negative_cases[index].code) == 0) &&
                         (strcmp(diagnostic->rule_id, negative_cases[index].rule_id) == 0) &&
                         (diagnostic->phase == R_DIAGNOSTIC_PHASE_SEMANTIC));
        r_frontend_destroy(context);
    }
}

static void r_semantic_test_stdout(void) {
    static const char positive_source[] = "module semantic.stdout_positive;\n"
                                          "i32 sync_stdout() {\n"
                                          "    std.io::output stream = std.io::stdout();\n"
                                          "    std.io::output selected = move stream;\n"
                                          "    return 0;\n"
                                          "}\n"
                                          "async i32 main() {\n"
                                          "    std.io::output stream = std.io::stdout();\n"
                                          "    std.io::output selected = move stream;\n"
                                          "    return 0;\n"
                                          "}\n";
    static const struct {
        const char *source;
        const char *code;
        const char *rule_id;
    } negative_cases[] = {
        {
            "module semantic.stdout_arity;\n"
            "i32 main() { std.io::output output = std.io::stdout(1); return 0; }\n",
            "R-DIAG-TYPE-001",
            "R-SLIB-IO-0002",
        },
        {
            "module semantic.stdout_context;\n"
            "i32 main() { std.io::input input = std.io::stdout(); return 0; }\n",
            "R-DIAG-TYPE-001",
            "R-SLIB-IO-0002",
        },
    };
    RFrontendContext *context = r_frontend_create(NULL);
    RSemanticTestBuffer hir = {0};
    RSemanticTestBuffer mir = {0};
    RSourceId source_id = R_SOURCE_ID_INVALID;
    const char *cursor;
    size_t operation_count = 0U;
    size_t index;

    R_SEMANTIC_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_SEMANTIC_CHECK(r_frontend_add_source(context,
                                           "stdout-positive.r",
                                           (const uint8_t *)positive_source,
                                           strlen(positive_source),
                                           &source_id) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    R_SEMANTIC_CHECK(r_frontend_dump_hir(context, r_semantic_test_write, &hir) == R_FRONTEND_OK);
    if (hir.bytes != NULL) {
        cursor = hir.bytes;
        while ((cursor = strstr(cursor, "operation=std.io::stdout")) != NULL) {
            operation_count += 1U;
            cursor += sizeof("operation=std.io::stdout") - 1U;
        }
    }
    R_SEMANTIC_CHECK(operation_count == 2U);
    R_SEMANTIC_CHECK((hir.bytes != NULL) &&
                     (strstr(hir.bytes, "(standard \"std.io::output\")") != NULL));
    R_SEMANTIC_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    R_SEMANTIC_CHECK(r_frontend_dump_mir(context, r_semantic_test_write, &mir) == R_FRONTEND_OK);
    operation_count = 0U;
    if (mir.bytes != NULL) {
        cursor = mir.bytes;
        while ((cursor = strstr(cursor, "operation=std.io::stdout")) != NULL) {
            operation_count += 1U;
            cursor += sizeof("operation=std.io::stdout") - 1U;
        }
    }
    R_SEMANTIC_CHECK(operation_count == 2U);
    free(mir.bytes);
    free(hir.bytes);
    r_frontend_destroy(context);

    for (index = 0U; index < (sizeof(negative_cases) / sizeof(negative_cases[0])); ++index) {
        const RDiagnostic *diagnostic;

        context = r_frontend_create(NULL);
        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        source_id = r_semantic_add_bytes(context,
                                         "stdout-negative.r",
                                         (const uint8_t *)negative_cases[index].source,
                                         strlen(negative_cases[index].source));
        (void)source_id;
        R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
        R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 1U);
        diagnostic = r_frontend_diagnostic(context, UINT32_C(0));
        R_SEMANTIC_CHECK((diagnostic != NULL) &&
                         (strcmp(diagnostic->code, negative_cases[index].code) == 0) &&
                         (strcmp(diagnostic->rule_id, negative_cases[index].rule_id) == 0) &&
                         (diagnostic->phase == R_DIAGNOSTIC_PHASE_SEMANTIC));
        r_frontend_destroy(context);
    }
}

static void r_semantic_test_stdin(void) {
    static const char positive_source[] = "module semantic.stdin_positive;\n"
                                          "i32 sync_stdin() {\n"
                                          "    std.io::input stream = std.io::stdin();\n"
                                          "    std.io::input selected = move stream;\n"
                                          "    drop selected;\n"
                                          "    return 0;\n"
                                          "}\n"
                                          "async i32 main() {\n"
                                          "    std.io::input stream = std.io::stdin();\n"
                                          "    std.io::input selected = move stream;\n"
                                          "    drop selected;\n"
                                          "    return 0;\n"
                                          "}\n";
    static const struct {
        const char *source;
        const char *code;
        const char *rule_id;
    } negative_cases[] = {
        {
            "module semantic.stdin_arity;\n"
            "i32 main() { std.io::input input = std.io::stdin(1); return 0; }\n",
            "R-DIAG-TYPE-001",
            "R-SLIB-IO-0002",
        },
        {
            "module semantic.stdin_context;\n"
            "i32 main() { std.io::output output = std.io::stdin(); return 0; }\n",
            "R-DIAG-TYPE-001",
            "R-SLIB-IO-0002",
        },
    };
    RFrontendContext *context = r_frontend_create(NULL);
    RSemanticTestBuffer hir = {0};
    RSemanticTestBuffer mir = {0};
    RSourceId source_id = R_SOURCE_ID_INVALID;
    const char *cursor;
    size_t operation_count = 0U;
    size_t index;

    R_SEMANTIC_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_SEMANTIC_CHECK(r_frontend_add_source(context,
                                           "stdin-positive.r",
                                           (const uint8_t *)positive_source,
                                           strlen(positive_source),
                                           &source_id) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    R_SEMANTIC_CHECK(r_frontend_dump_hir(context, r_semantic_test_write, &hir) == R_FRONTEND_OK);
    if (hir.bytes != NULL) {
        cursor = hir.bytes;
        while ((cursor = strstr(cursor, "operation=std.io::stdin")) != NULL) {
            operation_count += 1U;
            cursor += sizeof("operation=std.io::stdin") - 1U;
        }
    }
    R_SEMANTIC_CHECK(operation_count == 2U);
    R_SEMANTIC_CHECK((hir.bytes != NULL) &&
                     (strstr(hir.bytes, "(standard \"std.io::input\")") != NULL));
    R_SEMANTIC_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    R_SEMANTIC_CHECK(r_frontend_dump_mir(context, r_semantic_test_write, &mir) == R_FRONTEND_OK);
    operation_count = 0U;
    if (mir.bytes != NULL) {
        cursor = mir.bytes;
        while ((cursor = strstr(cursor, "operation=std.io::stdin")) != NULL) {
            operation_count += 1U;
            cursor += sizeof("operation=std.io::stdin") - 1U;
        }
    }
    R_SEMANTIC_CHECK(operation_count == 2U);
    free(mir.bytes);
    free(hir.bytes);
    r_frontend_destroy(context);

    for (index = 0U; index < (sizeof(negative_cases) / sizeof(negative_cases[0])); ++index) {
        const RDiagnostic *diagnostic;

        context = r_frontend_create(NULL);
        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        source_id = r_semantic_add_bytes(context,
                                         "stdin-negative.r",
                                         (const uint8_t *)negative_cases[index].source,
                                         strlen(negative_cases[index].source));
        (void)source_id;
        R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
        R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 1U);
        diagnostic = r_frontend_diagnostic(context, UINT32_C(0));
        R_SEMANTIC_CHECK((diagnostic != NULL) &&
                         (strcmp(diagnostic->code, negative_cases[index].code) == 0) &&
                         (strcmp(diagnostic->rule_id, negative_cases[index].rule_id) == 0) &&
                         (diagnostic->phase == R_DIAGNOSTIC_PHASE_SEMANTIC));
        r_frontend_destroy(context);
    }
}

static void r_semantic_test_async_io_read(void) {
    static const char positive_source[] =
        "module semantic.async_io_read;\n"
        "async i32 read_buffer(std.io::input input, array<u8> buffer) {\n"
        "    try {\n"
        "        task<std.io::read_result> operation =\n"
        "            std.io::read(&input, move buffer, o::none);\n"
        "            std.io::read_result completed = await move operation;\n"
        "            drop completed;\n"
        "            return 0;\n"
        "    } catch (std.async::start_error error) {\n"
        "            array<u8> recovered = move buffer;\n"
        "            drop recovered;\n"
        "            error as void;\n"
        "            return 1;\n"
        "    }\n"
        "}\n";
    static const struct {
        const char *source;
        const char *code;
        const char *rule_id;
    } negative_cases[] = {
        {
            "module semantic.io_read_arity;\n"
            "i32 invalid(std.io::input input, array<u8> buffer)\n"
            "    throws std.async::start_error {\n"
            "    task<std.io::read_result> started =\n"
            "        std.io::read(&input);\n"
            "    return 0;\n"
            "}\n",
            "R-DIAG-TYPE-001",
            "R-SLIB-IO-0003",
        },
        {
            "module semantic.io_read_stream_type;\n"
            "i32 invalid(const std.io::output* output, array<u8> buffer)\n"
            "    throws std.async::start_error {\n"
            "    task<std.io::read_result> started =\n"
            "        std.io::read(output, move buffer, o::none);\n"
            "    return 0;\n"
            "}\n",
            "R-DIAG-TYPE-001",
            "R-SLIB-IO-0003",
        },
        {
            "module semantic.io_read_context_type;\n"
            "i32 invalid(std.io::input input, array<u8> buffer)\n"
            "    throws std.async::start_error {\n"
            "    u32 started = std.io::read(&input, move buffer, o::none);\n"
            "    return 0;\n"
            "}\n",
            "R-DIAG-TYPE-001",
            "R-SLIB-IO-0003",
        },
        {
            "module semantic.io_read_deadline_type;\n"
            "i32 invalid(std.io::input input, array<u8> buffer)\n"
            "    throws std.async::start_error {\n"
            "    task<std.io::read_result> started =\n"
            "        std.io::read(&input, move buffer, 1);\n"
            "    return 0;\n"
            "}\n",
            "R-DIAG-TYPE-001",
            "R-SLIB-IO-0003",
        },
        {
            "module semantic.io_read_missing_move;\n"
            "i32 invalid(std.io::input input, array<u8> buffer)\n"
            "    throws std.async::start_error {\n"
            "    task<std.io::read_result> started =\n"
            "        std.io::read(&input, buffer, o::none);\n"
            "    return 0;\n"
            "}\n",
            "R-DIAG-MOVE-001",
            "R-OWN-0003",
        },
    };
    RFrontendContext *context = r_frontend_create(NULL);
    RSemanticTestBuffer hir_first = {0};
    RSemanticTestBuffer hir_second = {0};
    RSemanticTestBuffer mir_first = {0};
    RSemanticTestBuffer mir_second = {0};
    RMirValueId staged_buffer = R_MIR_VALUE_ID_INVALID;
    RMirValueId call_buffer = R_MIR_VALUE_ID_INVALID;
    size_t hir_call_count = 0U;
    size_t mir_call_count = 0U;
    size_t staged_move_count = 0U;
    size_t index;

    R_SEMANTIC_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    (void)r_semantic_add_bytes(context,
                               "async-io-read-positive.r",
                               (const uint8_t *)positive_source,
                               strlen(positive_source));
    R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    for (index = 0U; index < context->hir_node_count; ++index) {
        const RHirNode *node = &context->hir_nodes[index];

        if ((node->kind == R_HIR_STANDARD_CALL) &&
            (node->standard_operation == R_STANDARD_CALL_IO_READ)) {
            const RSemanticType *task_type = r_semantic_type(context, node->type);
            const RSemanticType *start_type = r_semantic_type(context, node->auxiliary_type);
            const RSemanticType *logical_type =
                task_type == NULL ? NULL : r_semantic_type(context, task_type->base);
            const RTypeId start_error_id =
                (start_type == NULL) ||
                        (r_semantic_effect_count(context, start_type->second) != UINT32_C(1))
                    ? R_TYPE_ID_INVALID
                    : r_semantic_effect_at(context, start_type->second, 0U);
            const RSemanticType *start_error_type = r_semantic_type(context, start_error_id);

            R_SEMANTIC_CHECK(node->child_count == UINT32_C(3));
            R_SEMANTIC_CHECK((start_type != NULL) &&
                             (start_type->kind == R_SEMANTIC_TYPE_EFFECT_CARRIER) &&
                             (start_type->base == node->type));
            R_SEMANTIC_CHECK((task_type != NULL) && (task_type->kind == R_SEMANTIC_TYPE_TASK));
            R_SEMANTIC_CHECK(
                (logical_type != NULL) &&
                r_semantic_test_standard_type_named(context, logical_type, "std.io::read_result"));
            R_SEMANTIC_CHECK((start_error_type != NULL) &&
                             r_semantic_test_standard_type_named(
                                 context, start_error_type, "std.async::start_error"));
            if ((node->child_count == UINT32_C(3)) && (context->hir_child_count >= UINT32_C(3)) &&
                ((size_t)node->first_child <= (context->hir_child_count - UINT32_C(3)))) {
                const RHirNodeId stream_id = context->hir_children[node->first_child];
                const RHirNodeId buffer_id = context->hir_children[node->first_child + 1U];
                const RHirNode *stream = (stream_id == R_HIR_NODE_ID_INVALID) ||
                                                 ((size_t)stream_id > context->hir_node_count)
                                             ? NULL
                                             : &context->hir_nodes[(size_t)stream_id - 1U];
                const RHirNode *buffer = (buffer_id == R_HIR_NODE_ID_INVALID) ||
                                                 ((size_t)buffer_id > context->hir_node_count)
                                             ? NULL
                                             : &context->hir_nodes[(size_t)buffer_id - 1U];
                const RSemanticType *stream_type =
                    stream == NULL ? NULL : r_semantic_type(context, stream->type);
                const RSemanticType *buffer_type =
                    buffer == NULL ? NULL : r_semantic_type(context, buffer->type);
                const RSemanticType *element_type =
                    buffer_type == NULL ? NULL : r_semantic_type(context, buffer_type->base);

                R_SEMANTIC_CHECK((stream != NULL) && (stream->kind == R_HIR_BORROW));
                R_SEMANTIC_CHECK((stream_type != NULL) &&
                                 (stream_type->kind == R_SEMANTIC_TYPE_BORROW) &&
                                 (stream_type->flags == R_SEMANTIC_TYPE_FLAG_SHARED));
                R_SEMANTIC_CHECK((buffer != NULL) && (buffer->kind == R_HIR_MOVE));
                R_SEMANTIC_CHECK((buffer_type != NULL) &&
                                 (buffer_type->kind == R_SEMANTIC_TYPE_ARRAY));
                R_SEMANTIC_CHECK((element_type != NULL) &&
                                 (element_type->kind == R_SEMANTIC_TYPE_U8));
            }
            hir_call_count += 1U;
        }
    }
    R_SEMANTIC_CHECK(hir_call_count == 1U);
    R_SEMANTIC_CHECK(r_frontend_dump_hir(context, r_semantic_test_write, &hir_first) ==
                     R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_dump_hir(context, r_semantic_test_write, &hir_second) ==
                     R_FRONTEND_OK);
    R_SEMANTIC_CHECK(
        (hir_first.bytes != NULL) && (strstr(hir_first.bytes, "operation=std.io::read") != NULL) &&
        (strstr(hir_first.bytes, "(task (standard \"std.io::read_result\"))") != NULL) &&
        (strstr(hir_first.bytes, "std.async::start_error") != NULL));
    R_SEMANTIC_CHECK(r_semantic_test_buffers_equal(&hir_first, &hir_second));
    R_SEMANTIC_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    for (index = 0U; index < context->mir_instruction_count; ++index) {
        const RMirInstruction *instruction = &context->mir_instructions[index];

        if ((instruction->kind == R_MIR_INSTRUCTION_MOVE) && instruction->is_async_staged_move) {
            staged_buffer = instruction->result;
            staged_move_count += 1U;
        } else if ((instruction->kind == R_MIR_INSTRUCTION_STANDARD_CALL) &&
                   (instruction->standard_operation == R_STANDARD_CALL_IO_READ)) {
            const size_t first_operand = (size_t)instruction->first_operand;

            R_SEMANTIC_CHECK(instruction->operand_count == UINT32_C(3));
            R_SEMANTIC_CHECK(instruction->call_borrow_mask.low == UINT64_C(1));
            R_SEMANTIC_CHECK(instruction->call_borrow_mask.high == UINT64_C(0));
            R_SEMANTIC_CHECK((context->mir_operand_count >= UINT32_C(3)) &&
                             (first_operand <= (context->mir_operand_count - UINT32_C(3))));
            if ((context->mir_operand_count >= UINT32_C(3)) &&
                (first_operand <= (context->mir_operand_count - UINT32_C(3)))) {
                call_buffer = context->mir_operands[first_operand + 1U];
            }
            mir_call_count += 1U;
        }
    }
    R_SEMANTIC_CHECK(staged_move_count == 1U);
    R_SEMANTIC_CHECK(mir_call_count == 1U);
    R_SEMANTIC_CHECK((staged_buffer != R_MIR_VALUE_ID_INVALID) && (call_buffer == staged_buffer));
    R_SEMANTIC_CHECK(r_frontend_dump_mir(context, r_semantic_test_write, &mir_first) ==
                     R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_dump_mir(context, r_semantic_test_write, &mir_second) ==
                     R_FRONTEND_OK);
    R_SEMANTIC_CHECK(
        (mir_first.bytes != NULL) &&
        (strstr(mir_first.bytes, "call_bounded_borrows=(stream) staged_moves=(buffer)") != NULL) &&
        (strstr(mir_first.bytes, "async_staged=true") != NULL));
    R_SEMANTIC_CHECK(r_semantic_test_buffers_equal(&mir_first, &mir_second));
    free(mir_second.bytes);
    free(mir_first.bytes);
    free(hir_second.bytes);
    free(hir_first.bytes);
    r_frontend_destroy(context);

    for (index = 0U; index < (sizeof(negative_cases) / sizeof(negative_cases[0])); ++index) {
        const RDiagnostic *diagnostic;

        context = r_frontend_create(NULL);
        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        (void)r_semantic_add_bytes(context,
                                   "async-io-read-negative.r",
                                   (const uint8_t *)negative_cases[index].source,
                                   strlen(negative_cases[index].source));
        R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
        R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 1U);
        diagnostic = r_frontend_diagnostic(context, UINT32_C(0));
        R_SEMANTIC_CHECK((diagnostic != NULL) &&
                         (strcmp(diagnostic->code, negative_cases[index].code) == 0) &&
                         (strcmp(diagnostic->rule_id, negative_cases[index].rule_id) == 0) &&
                         (diagnostic->phase == R_DIAGNOSTIC_PHASE_SEMANTIC));
        r_frontend_destroy(context);
    }
}

static void r_semantic_test_async_io_write_all(void) {
    static const char positive_source[] =
        "module semantic.async_io_write_all;\n"
        "async i32 write_buffer(std.io::output output, array<u8> buffer) {\n"
        "    try {\n"
        "        task<std.io::write_all_result> operation =\n"
        "            std.io::write_all(&output, move buffer, o::none);\n"
        "            std.io::write_all_result completed = await move operation;\n"
        "            drop completed;\n"
        "            return 0;\n"
        "    } catch (std.async::start_error error) {\n"
        "            array<u8> recovered = move buffer;\n"
        "            drop recovered;\n"
        "            error as void;\n"
        "            return 1;\n"
        "    }\n"
        "}\n";
    static const struct {
        const char *source;
        const char *code;
        const char *rule_id;
        const char *message;
    } negative_cases[] = {
        {
            "module semantic.async_io_write_all_missing_move;\n"
            "async i32 write_buffer(std.io::output output, array<u8> buffer) "
            "throws std.async::start_error {\n"
            "    task<std.io::write_all_result> started =\n"
            "        std.io::write_all(&output, buffer, o::none);\n"
            "    return 0;\n"
            "}\n",
            "R-DIAG-MOVE-001",
            "R-OWN-0003",
            NULL,
        },
    };
    RFrontendContext *context = r_frontend_create(NULL);
    RSemanticTestBuffer hir_first = {0};
    RSemanticTestBuffer hir_second = {0};
    RSemanticTestBuffer mir_first = {0};
    RSemanticTestBuffer mir_second = {0};
    RMirValueId staged_buffer = R_MIR_VALUE_ID_INVALID;
    RMirValueId call_buffer = R_MIR_VALUE_ID_INVALID;
    size_t hir_call_count = 0U;
    size_t mir_call_count = 0U;
    size_t staged_move_count = 0U;
    size_t index;

    R_SEMANTIC_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    (void)r_semantic_add_bytes(context,
                               "async-io-write-all-positive.r",
                               (const uint8_t *)positive_source,
                               strlen(positive_source));
    R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    for (index = 0U; index < context->hir_node_count; ++index) {
        const RHirNode *node = &context->hir_nodes[index];

        if ((node->kind == R_HIR_STANDARD_CALL) &&
            (node->standard_operation == R_STANDARD_CALL_IO_WRITE_ALL)) {
            const RSemanticType *task_type = r_semantic_type(context, node->type);
            const RSemanticType *start_type = r_semantic_type(context, node->auxiliary_type);
            const RSemanticType *logical_type =
                task_type == NULL ? NULL : r_semantic_type(context, task_type->base);
            const RTypeId start_error_id =
                (start_type == NULL) ||
                        (r_semantic_effect_count(context, start_type->second) != UINT32_C(1))
                    ? R_TYPE_ID_INVALID
                    : r_semantic_effect_at(context, start_type->second, 0U);
            const RSemanticType *start_error_type = r_semantic_type(context, start_error_id);

            R_SEMANTIC_CHECK(node->child_count == UINT32_C(3));
            R_SEMANTIC_CHECK((start_type != NULL) &&
                             (start_type->kind == R_SEMANTIC_TYPE_EFFECT_CARRIER) &&
                             (start_type->base == node->type));
            R_SEMANTIC_CHECK((task_type != NULL) && (task_type->kind == R_SEMANTIC_TYPE_TASK));
            R_SEMANTIC_CHECK((logical_type != NULL) &&
                             r_semantic_test_standard_type_named(
                                 context, logical_type, "std.io::write_all_result"));
            R_SEMANTIC_CHECK((start_error_type != NULL) &&
                             r_semantic_test_standard_type_named(
                                 context, start_error_type, "std.async::start_error"));
            if ((node->child_count == UINT32_C(3)) && (context->hir_child_count >= UINT32_C(3)) &&
                ((size_t)node->first_child <= (context->hir_child_count - UINT32_C(3)))) {
                const RHirNodeId stream_id = context->hir_children[node->first_child];
                const RHirNodeId buffer_id = context->hir_children[node->first_child + 1U];
                const RHirNode *stream = (stream_id == R_HIR_NODE_ID_INVALID) ||
                                                 ((size_t)stream_id > context->hir_node_count)
                                             ? NULL
                                             : &context->hir_nodes[(size_t)stream_id - 1U];
                const RHirNode *buffer = (buffer_id == R_HIR_NODE_ID_INVALID) ||
                                                 ((size_t)buffer_id > context->hir_node_count)
                                             ? NULL
                                             : &context->hir_nodes[(size_t)buffer_id - 1U];
                const RSemanticType *stream_type =
                    stream == NULL ? NULL : r_semantic_type(context, stream->type);
                const RSemanticType *buffer_type =
                    buffer == NULL ? NULL : r_semantic_type(context, buffer->type);
                const RSemanticType *element_type =
                    buffer_type == NULL ? NULL : r_semantic_type(context, buffer_type->base);

                R_SEMANTIC_CHECK((stream != NULL) && (stream->kind == R_HIR_BORROW));
                R_SEMANTIC_CHECK((stream_type != NULL) &&
                                 (stream_type->kind == R_SEMANTIC_TYPE_BORROW) &&
                                 (stream_type->flags == R_SEMANTIC_TYPE_FLAG_SHARED));
                R_SEMANTIC_CHECK((buffer != NULL) && (buffer->kind == R_HIR_MOVE));
                R_SEMANTIC_CHECK((buffer_type != NULL) &&
                                 (buffer_type->kind == R_SEMANTIC_TYPE_ARRAY));
                R_SEMANTIC_CHECK((element_type != NULL) &&
                                 (element_type->kind == R_SEMANTIC_TYPE_U8));
            }
            hir_call_count += 1U;
        }
    }
    R_SEMANTIC_CHECK(hir_call_count == 1U);
    R_SEMANTIC_CHECK(r_frontend_dump_hir(context, r_semantic_test_write, &hir_first) ==
                     R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_dump_hir(context, r_semantic_test_write, &hir_second) ==
                     R_FRONTEND_OK);
    R_SEMANTIC_CHECK(
        (hir_first.bytes != NULL) &&
        (strstr(hir_first.bytes, "operation=std.io::write_all") != NULL) &&
        (strstr(hir_first.bytes, "(task (standard \"std.io::write_all_result\"))") != NULL) &&
        (strstr(hir_first.bytes, "std.async::start_error") != NULL));
    R_SEMANTIC_CHECK(r_semantic_test_buffers_equal(&hir_first, &hir_second));
    R_SEMANTIC_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    for (index = 0U; index < context->mir_instruction_count; ++index) {
        const RMirInstruction *instruction = &context->mir_instructions[index];

        if ((instruction->kind == R_MIR_INSTRUCTION_MOVE) && instruction->is_async_staged_move) {
            staged_buffer = instruction->result;
            staged_move_count += 1U;
        } else if ((instruction->kind == R_MIR_INSTRUCTION_STANDARD_CALL) &&
                   (instruction->standard_operation == R_STANDARD_CALL_IO_WRITE_ALL)) {
            const size_t first_operand = (size_t)instruction->first_operand;

            R_SEMANTIC_CHECK(instruction->operand_count == UINT32_C(3));
            R_SEMANTIC_CHECK(instruction->call_borrow_mask.low == UINT64_C(1));
            R_SEMANTIC_CHECK(instruction->call_borrow_mask.high == UINT64_C(0));
            R_SEMANTIC_CHECK((context->mir_operand_count >= UINT32_C(3)) &&
                             (first_operand <= (context->mir_operand_count - UINT32_C(3))));
            if ((context->mir_operand_count >= UINT32_C(3)) &&
                (first_operand <= (context->mir_operand_count - UINT32_C(3)))) {
                call_buffer = context->mir_operands[first_operand + 1U];
            }
            mir_call_count += 1U;
        }
    }
    R_SEMANTIC_CHECK(staged_move_count == 1U);
    R_SEMANTIC_CHECK(mir_call_count == 1U);
    R_SEMANTIC_CHECK((staged_buffer != R_MIR_VALUE_ID_INVALID) && (call_buffer == staged_buffer));
    R_SEMANTIC_CHECK(r_frontend_dump_mir(context, r_semantic_test_write, &mir_first) ==
                     R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_dump_mir(context, r_semantic_test_write, &mir_second) ==
                     R_FRONTEND_OK);
    R_SEMANTIC_CHECK(
        (mir_first.bytes != NULL) &&
        (strstr(mir_first.bytes, "call_bounded_borrows=(stream) staged_moves=(buffer)") != NULL) &&
        (strstr(mir_first.bytes, "async_staged=true") != NULL));
    R_SEMANTIC_CHECK(r_semantic_test_buffers_equal(&mir_first, &mir_second));
    free(mir_second.bytes);
    free(mir_first.bytes);
    free(hir_second.bytes);
    free(hir_first.bytes);
    r_frontend_destroy(context);

    for (index = 0U; index < (sizeof(negative_cases) / sizeof(negative_cases[0])); ++index) {
        const RDiagnostic *diagnostic;

        context = r_frontend_create(NULL);
        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        (void)r_semantic_add_bytes(context,
                                   "async-io-write-all-negative.r",
                                   (const uint8_t *)negative_cases[index].source,
                                   strlen(negative_cases[index].source));
        R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
        R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 1U);
        diagnostic = r_frontend_diagnostic(context, UINT32_C(0));
        R_SEMANTIC_CHECK((diagnostic != NULL) &&
                         (strcmp(diagnostic->code, negative_cases[index].code) == 0) &&
                         (strcmp(diagnostic->rule_id, negative_cases[index].rule_id) == 0) &&
                         ((negative_cases[index].message == NULL) ||
                          (strstr(diagnostic->message, negative_cases[index].message) != NULL)) &&
                         (diagnostic->phase == R_DIAGNOSTIC_PHASE_SEMANTIC));
        r_frontend_destroy(context);
    }
}

static void r_semantic_test_standard_outcome_schemas(void) {
    static const struct {
        const char *path;
        const char *code;
        const char *rule_id;
    } negative_cases[] = {
        {
            R_SEMANTIC_STANDARD_OUTCOMES_WRONG_VARIANT_PATH,
            "R-DIAG-TYPE-001",
            "R-STMT-0010",
        },
        {
            R_SEMANTIC_STANDARD_OUTCOMES_MISSING_MOVE_PATH,
            "R-DIAG-MOVE-003",
            "R-STMT-0010",
        },
    };
    uint8_t *source = NULL;
    size_t source_length = 0U;
    RFrontendContext *context;
    RSemanticTestBuffer hir_first = {0};
    RSemanticTestBuffer hir_second = {0};
    RSemanticTestBuffer mir_first = {0};
    RSemanticTestBuffer mir_second = {0};
    size_t explicit_drop_count = 0U;
    size_t case_tag_counts[2] = {0U, 0U};
    size_t enum_constant_count = 0U;
    size_t hir_field_count = 0U;
    size_t mir_tag_count = 0U;
    size_t mir_payload_count = 0U;
    size_t mir_field_count = 0U;
    size_t mir_drop_count = 0U;
    bool found_io_failed = false;
    bool found_fs_failed = false;
    bool found_fs_error_code = false;
    size_t index;

    R_SEMANTIC_CHECK(
        r_semantic_read_file(R_SEMANTIC_STANDARD_OUTCOMES_PATH, &source, &source_length));
    if (source == NULL) {
        return;
    }
    context = r_frontend_create(NULL);
    R_SEMANTIC_CHECK(context != NULL);
    if (context == NULL) {
        free(source);
        return;
    }
    (void)r_semantic_add_bytes(context, "standard-outcomes.r", source, source_length);
    free(source);
    source = NULL;
    R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);

    for (index = 0U; index < context->semantic_aggregate_count; ++index) {
        const RSemanticAggregate *aggregate = &context->semantic_aggregates[index];

        if (r_semantic_test_intern_named(
                context, aggregate->name_intern_id, "std.io::write_all_result::failed")) {
            const RSemanticField *fields = aggregate->first_field >= context->semantic_field_count
                                               ? NULL
                                               : &context->semantic_fields[aggregate->first_field];

            found_io_failed = true;
            R_SEMANTIC_CHECK((aggregate->kind == R_SEMANTIC_AGGREGATE_STRUCT) &&
                             (aggregate->module_source == R_SOURCE_ID_INVALID) &&
                             aggregate->is_protected && !aggregate->is_copy &&
                             (aggregate->field_count == UINT32_C(3)) && (fields != NULL));
            if ((aggregate->field_count == UINT32_C(3)) && (fields != NULL)) {
                const RSemanticType *error_type = r_semantic_type(context, fields[0].type);
                const RSemanticType *written_type = r_semantic_type(context, fields[1].type);
                const RSemanticType *buffer_type = r_semantic_type(context, fields[2].type);
                const RSemanticType *element_type =
                    buffer_type == NULL ? NULL : r_semantic_type(context, buffer_type->base);

                R_SEMANTIC_CHECK(
                    r_semantic_test_intern_named(context, fields[0].name_intern_id, "error") &&
                    r_semantic_test_standard_type_named(context, error_type, "std.io::io_error"));
                R_SEMANTIC_CHECK(
                    r_semantic_test_intern_named(context, fields[1].name_intern_id, "written") &&
                    (written_type != NULL) && (written_type->kind == R_SEMANTIC_TYPE_USIZE));
                R_SEMANTIC_CHECK(
                    r_semantic_test_intern_named(context, fields[2].name_intern_id, "buffer") &&
                    (buffer_type != NULL) && (buffer_type->kind == R_SEMANTIC_TYPE_ARRAY) &&
                    (element_type != NULL) && (element_type->kind == R_SEMANTIC_TYPE_U8));
            }
        } else if (r_semantic_test_intern_named(
                       context, aggregate->name_intern_id, "std.fs::write_file_result::failed")) {
            const RSemanticField *fields = aggregate->first_field >= context->semantic_field_count
                                               ? NULL
                                               : &context->semantic_fields[aggregate->first_field];

            found_fs_failed = true;
            R_SEMANTIC_CHECK((aggregate->kind == R_SEMANTIC_AGGREGATE_STRUCT) &&
                             (aggregate->module_source == R_SOURCE_ID_INVALID) &&
                             aggregate->is_protected && !aggregate->is_copy &&
                             (aggregate->field_count == UINT32_C(2)) && (fields != NULL));
            if ((aggregate->field_count == UINT32_C(2)) && (fields != NULL)) {
                const RSemanticType *error_type = r_semantic_type(context, fields[0].type);
                const RSemanticType *data_type = r_semantic_type(context, fields[1].type);
                const RSemanticType *element_type =
                    data_type == NULL ? NULL : r_semantic_type(context, data_type->base);

                R_SEMANTIC_CHECK(
                    r_semantic_test_intern_named(context, fields[0].name_intern_id, "error") &&
                    r_semantic_test_standard_type_named(context, error_type, "std.fs::fs_error"));
                R_SEMANTIC_CHECK(
                    r_semantic_test_intern_named(context, fields[1].name_intern_id, "data") &&
                    (data_type != NULL) && (data_type->kind == R_SEMANTIC_TYPE_ARRAY) &&
                    (element_type != NULL) && (element_type->kind == R_SEMANTIC_TYPE_U8));
            }
        } else if (r_semantic_test_intern_named(
                       context, aggregate->name_intern_id, "std.fs::error_code")) {
            const size_t variant_index = (size_t)aggregate->first_variant + 4U;
            const RSemanticVariant *variant = variant_index >= context->semantic_variant_count
                                                  ? NULL
                                                  : &context->semantic_variants[variant_index];

            found_fs_error_code = true;
            R_SEMANTIC_CHECK((aggregate->kind == R_SEMANTIC_AGGREGATE_ENUM) &&
                             (aggregate->variant_count == UINT32_C(20)) && (variant != NULL));
            if (variant != NULL) {
                R_SEMANTIC_CHECK(r_semantic_test_intern_named(
                                     context, variant->name_intern_id, "already_exists") &&
                                 (variant->value == UINT64_C(4)));
            }
        }
    }
    R_SEMANTIC_CHECK(found_io_failed && found_fs_failed && found_fs_error_code);

    for (index = 0U; index < context->hir_node_count; ++index) {
        const RHirNode *node = &context->hir_nodes[index];

        if ((node->kind == R_HIR_DROP) && (node->operation == R_TOKEN_KW_DROP)) {
            explicit_drop_count += 1U;
        } else if ((node->kind == R_HIR_CASE) &&
                   (node->case_pattern == R_HIR_CASE_PATTERN_VARIANT) &&
                   (node->integer_value < UINT64_C(2))) {
            case_tag_counts[(size_t)node->integer_value] += 1U;
        } else if ((node->kind == R_HIR_ENUM_CONSTANT) && (node->integer_value == UINT64_C(4)) &&
                   r_semantic_test_standard_type_named(
                       context, r_semantic_type(context, node->type), "std.fs::error_code")) {
            enum_constant_count += 1U;
        } else if (node->kind == R_HIR_FIELD_PLACE) {
            hir_field_count += 1U;
        }
    }
    R_SEMANTIC_CHECK(explicit_drop_count == 4U);
    R_SEMANTIC_CHECK((case_tag_counts[0] == 2U) && (case_tag_counts[1] == 2U));
    R_SEMANTIC_CHECK(enum_constant_count == 1U);
    R_SEMANTIC_CHECK(hir_field_count == 6U);
    R_SEMANTIC_CHECK(r_frontend_dump_hir(context, r_semantic_test_write, &hir_first) ==
                     R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_dump_hir(context, r_semantic_test_write, &hir_second) ==
                     R_FRONTEND_OK);
    R_SEMANTIC_CHECK(
        (hir_first.bytes != NULL) &&
        (strstr(hir_first.bytes, "(standard_payload \"std.io::write_all_result::failed\")") !=
         NULL) &&
        (strstr(hir_first.bytes, "(standard_payload \"std.fs::write_file_result::failed\")") !=
         NULL) &&
        (strstr(hir_first.bytes, "enum_constant type=(standard \"std.fs::error_code\") value=4") !=
         NULL));
    R_SEMANTIC_CHECK(r_semantic_test_buffers_equal(&hir_first, &hir_second));

    R_SEMANTIC_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    for (index = 0U; index < context->mir_instruction_count; ++index) {
        const RMirInstruction *instruction = &context->mir_instructions[index];

        if (instruction->kind == R_MIR_INSTRUCTION_VARIANT_TAG) {
            mir_tag_count += 1U;
        } else if (instruction->kind == R_MIR_INSTRUCTION_VARIANT_PAYLOAD) {
            mir_payload_count += 1U;
        } else if (instruction->kind == R_MIR_INSTRUCTION_FIELD) {
            mir_field_count += 1U;
        } else if (instruction->kind == R_MIR_INSTRUCTION_DROP) {
            mir_drop_count += 1U;
        }
    }
    R_SEMANTIC_CHECK(mir_tag_count == 2U);
    R_SEMANTIC_CHECK(mir_payload_count == 4U);
    R_SEMANTIC_CHECK(mir_field_count == 6U);
    R_SEMANTIC_CHECK(mir_drop_count == 4U);
    R_SEMANTIC_CHECK(r_frontend_dump_mir(context, r_semantic_test_write, &mir_first) ==
                     R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_dump_mir(context, r_semantic_test_write, &mir_second) ==
                     R_FRONTEND_OK);
    R_SEMANTIC_CHECK(
        (mir_first.bytes != NULL) &&
        (strstr(mir_first.bytes, "variant_payload value=%v0 tag=0 type=(array u8)") != NULL) &&
        (strstr(mir_first.bytes, "type=(standard_payload \"std.io::write_all_result::failed\")") !=
         NULL) &&
        (strstr(mir_first.bytes, "type=(standard_payload \"std.fs::write_file_result::failed\")") !=
         NULL));
    R_SEMANTIC_CHECK(r_semantic_test_buffers_equal(&mir_first, &mir_second));
    free(mir_second.bytes);
    free(mir_first.bytes);
    free(hir_second.bytes);
    free(hir_first.bytes);
    r_frontend_destroy(context);

    for (index = 0U; index < (sizeof(negative_cases) / sizeof(negative_cases[0])); ++index) {
        const RDiagnostic *diagnostic;

        R_SEMANTIC_CHECK(r_semantic_read_file(negative_cases[index].path, &source, &source_length));
        if (source == NULL) {
            continue;
        }
        context = r_frontend_create(NULL);
        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL) {
            free(source);
            source = NULL;
            continue;
        }
        (void)r_semantic_add_bytes(context, "standard-outcomes-negative.r", source, source_length);
        free(source);
        source = NULL;
        R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
        R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 1U);
        diagnostic = r_frontend_diagnostic(context, UINT32_C(0));
        R_SEMANTIC_CHECK((diagnostic != NULL) &&
                         (strcmp(diagnostic->code, negative_cases[index].code) == 0) &&
                         (strcmp(diagnostic->rule_id, negative_cases[index].rule_id) == 0) &&
                         (diagnostic->phase == R_DIAGNOSTIC_PHASE_SEMANTIC));
        r_frontend_destroy(context);
    }
}

static void r_semantic_test_async_copy_arguments(void) {
    static const char *const argument_names[] = {"hundreds", "tens", "ones"};
    uint8_t *source = NULL;
    size_t source_length = 0U;
    RFrontendContext *context = NULL;
    RSourceId source_id = R_SOURCE_ID_INVALID;
    RSymbolId source_symbols[3] = {R_SYMBOL_ID_INVALID, R_SYMBOL_ID_INVALID, R_SYMBOL_ID_INVALID};
    size_t source_load_counts[3] = {0U, 0U, 0U};
    size_t async_start_count = 0U;
    bool found_main_mir = false;
    size_t node_index;
    size_t function_index;

    R_SEMANTIC_CHECK(
        r_semantic_read_file(R_SEMANTIC_ASYNC_COPY_ARGS_PATH, &source, &source_length));
    if (source == NULL) {
        return;
    }
    context = r_frontend_create(NULL);
    R_SEMANTIC_CHECK(context != NULL);
    if (context == NULL) {
        free(source);
        return;
    }
    source_id = r_semantic_add_bytes(context, "async-copy-args.r", source, source_length);
    (void)source_id;
    R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);

    for (node_index = 0U; node_index < context->hir_node_count; ++node_index) {
        const RHirNode *node = &context->hir_nodes[node_index];
        const RSemanticType *start_type;
        const RSemanticType *task_type;
        const RSemanticType *error_type;
        size_t argument_index;

        if (node->kind != R_HIR_ASYNC_START) {
            continue;
        }
        async_start_count += 1U;
        task_type = r_semantic_type(context, node->type);
        start_type = r_semantic_type(context, node->auxiliary_type);
        error_type =
            (start_type == NULL) ||
                    (r_semantic_effect_count(context, start_type->second) != UINT32_C(1))
                ? NULL
                : r_semantic_type(context,
                                  r_semantic_effect_at(context, start_type->second, UINT32_C(0)));
        R_SEMANTIC_CHECK((start_type != NULL) &&
                         (start_type->kind == R_SEMANTIC_TYPE_EFFECT_CARRIER) &&
                         (start_type->base == node->type));
        R_SEMANTIC_CHECK((task_type != NULL) && (task_type->kind == R_SEMANTIC_TYPE_TASK) &&
                         (r_semantic_type(context, task_type->base) != NULL) &&
                         (r_semantic_type(context, task_type->base)->kind == R_SEMANTIC_TYPE_I32));
        R_SEMANTIC_CHECK(
            r_semantic_test_standard_type_named(context, error_type, "std.async::start_error"));
        R_SEMANTIC_CHECK(node->child_count == UINT32_C(3));
        R_SEMANTIC_CHECK(r_semantic_test_symbol_named(context, node->symbol, "encode"));

        for (argument_index = 0U; argument_index < 3U; ++argument_index) {
            const size_t child_index = (size_t)node->first_child + argument_index;
            const RHirNode *argument = NULL;
            const RHirNode *place = NULL;

            R_SEMANTIC_CHECK(child_index < context->hir_child_count);
            if (child_index < context->hir_child_count) {
                const RHirNodeId argument_id = context->hir_children[child_index];
                if ((argument_id != R_HIR_NODE_ID_INVALID) &&
                    ((size_t)argument_id <= context->hir_node_count)) {
                    argument = &context->hir_nodes[(size_t)argument_id - 1U];
                }
            }
            R_SEMANTIC_CHECK((argument != NULL) && (argument->kind == R_HIR_LOAD) &&
                             (argument->child_count == UINT32_C(1)));
            if ((argument != NULL) && (argument->child_count == UINT32_C(1)) &&
                ((size_t)argument->first_child < context->hir_child_count)) {
                const RHirNodeId place_id = context->hir_children[(size_t)argument->first_child];
                if ((place_id != R_HIR_NODE_ID_INVALID) &&
                    ((size_t)place_id <= context->hir_node_count)) {
                    place = &context->hir_nodes[(size_t)place_id - 1U];
                }
            }
            R_SEMANTIC_CHECK((place != NULL) && (place->kind == R_HIR_PLACE) &&
                             r_semantic_test_symbol_named(
                                 context, place->symbol, argument_names[argument_index]));
            if (place != NULL) {
                source_symbols[argument_index] = place->symbol;
            }
        }
    }
    R_SEMANTIC_CHECK(async_start_count == 1U);

    for (node_index = 0U; node_index < context->hir_node_count; ++node_index) {
        const RHirNode *node = &context->hir_nodes[node_index];
        const RHirNode *place = NULL;
        size_t argument_index;

        if ((node->kind != R_HIR_LOAD) || (node->child_count != UINT32_C(1)) ||
            ((size_t)node->first_child >= context->hir_child_count)) {
            continue;
        }
        {
            const RHirNodeId place_id = context->hir_children[(size_t)node->first_child];
            if ((place_id != R_HIR_NODE_ID_INVALID) &&
                ((size_t)place_id <= context->hir_node_count)) {
                place = &context->hir_nodes[(size_t)place_id - 1U];
            }
        }
        for (argument_index = 0U; argument_index < 3U; ++argument_index) {
            if ((place != NULL) && (place->symbol == source_symbols[argument_index])) {
                source_load_counts[argument_index] += 1U;
            }
        }
    }
    for (node_index = 0U; node_index < 3U; ++node_index) {
        const RSemanticSymbol *source_symbol =
            source_symbols[node_index] == R_SYMBOL_ID_INVALID
                ? NULL
                : &context->semantic_symbols[(size_t)source_symbols[node_index] - 1U];
        R_SEMANTIC_CHECK(source_load_counts[node_index] == 2U);
        R_SEMANTIC_CHECK((source_symbol != NULL) &&
                         (source_symbol->object_state == R_SEMANTIC_OBJECT_STATE_INITIALIZED));
    }

    R_SEMANTIC_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
    for (function_index = 0U; function_index < context->mir_function_count; ++function_index) {
        const RMirFunction *function = &context->mir_functions[function_index];
        bool later_loads[3] = {false, false, false};
        bool found_start = false;
        uint32_t block_index;

        if ((function->symbol == R_SYMBOL_ID_INVALID) ||
            ((size_t)function->symbol > context->semantic_symbol_count) ||
            !r_semantic_test_symbol_named(context, function->symbol, "main")) {
            continue;
        }
        found_main_mir = true;
        for (block_index = 0U; block_index < function->block_count; ++block_index) {
            const size_t global_block_index = (size_t)function->first_block + (size_t)block_index;
            const RMirBlock *block;
            uint32_t instruction_index;

            R_SEMANTIC_CHECK(global_block_index < context->mir_block_count);
            if (global_block_index >= context->mir_block_count) {
                continue;
            }
            block = &context->mir_blocks[global_block_index];
            for (instruction_index = 0U; instruction_index < block->instruction_count;
                 ++instruction_index) {
                const size_t global_instruction_index =
                    (size_t)block->first_instruction + (size_t)instruction_index;
                const RMirInstruction *instruction;

                R_SEMANTIC_CHECK(global_instruction_index < context->mir_instruction_count);
                if (global_instruction_index >= context->mir_instruction_count) {
                    continue;
                }
                instruction = &context->mir_instructions[global_instruction_index];

                if (instruction->kind == R_MIR_INSTRUCTION_ASYNC_START) {
                    const RSemanticType *start_type = r_semantic_type(context, instruction->type);
                    const RSemanticType *error_type =
                        (start_type == NULL) || (r_semantic_effect_count(
                                                     context, start_type->second) != UINT32_C(1))
                            ? NULL
                            : r_semantic_type(
                                  context,
                                  r_semantic_effect_at(context, start_type->second, UINT32_C(0)));
                    size_t argument_index;

                    R_SEMANTIC_CHECK(!found_start);
                    found_start = true;
                    R_SEMANTIC_CHECK(instruction->operand_count == UINT32_C(3));
                    R_SEMANTIC_CHECK(
                        r_semantic_test_symbol_named(context, instruction->symbol, "encode"));
                    R_SEMANTIC_CHECK((start_type != NULL) &&
                                     (start_type->kind == R_SEMANTIC_TYPE_EFFECT_CARRIER) &&
                                     (start_type->base == instruction->auxiliary_type));
                    R_SEMANTIC_CHECK(r_semantic_test_standard_type_named(
                        context, error_type, "std.async::start_error"));
                    for (argument_index = 0U; argument_index < 3U; ++argument_index) {
                        const size_t operand_index =
                            (size_t)instruction->first_operand + argument_index;
                        RMirValueId operand = R_MIR_VALUE_ID_INVALID;
                        const RMirInstruction *definition = NULL;
                        uint32_t candidate_index;

                        R_SEMANTIC_CHECK(operand_index < context->mir_operand_count);
                        if (operand_index < context->mir_operand_count) {
                            operand = context->mir_operands[operand_index];
                        }
                        for (candidate_index = 0U; candidate_index < instruction_index;
                             ++candidate_index) {
                            const RMirInstruction *candidate =
                                &context->mir_instructions[(size_t)block->first_instruction +
                                                           (size_t)candidate_index];
                            if (candidate->result == operand) {
                                definition = candidate;
                            }
                        }
                        R_SEMANTIC_CHECK((definition != NULL) &&
                                         (definition->kind == R_MIR_INSTRUCTION_LOAD) &&
                                         !definition->place_is_parameter &&
                                         (definition->place_ordinal == (uint32_t)argument_index));
                    }
                } else if (found_start && (instruction->kind == R_MIR_INSTRUCTION_LOAD) &&
                           !instruction->place_is_parameter &&
                           (instruction->place_ordinal < UINT32_C(3))) {
                    later_loads[instruction->place_ordinal] = true;
                } else if ((instruction->kind == R_MIR_INSTRUCTION_MOVE) &&
                           !instruction->place_is_parameter &&
                           (instruction->place_ordinal < UINT32_C(3))) {
                    R_SEMANTIC_CHECK(false);
                }
            }
        }
        R_SEMANTIC_CHECK(found_start);
        R_SEMANTIC_CHECK(later_loads[0] && later_loads[1] && later_loads[2]);
    }
    R_SEMANTIC_CHECK(found_main_mir);

    r_frontend_destroy(context);
    free(source);
}

static void r_semantic_test_async_diagnostics(void) {
    static const struct {
        const char *source;
        const char *code;
        const char *rule_id;
    } cases[] = {
        {"module semantic.async_extern; async extern \"C\" i32 operation();",
         "R-DIAG-ASYNC-001",
         "R-FUNC-0010"},
        {"module semantic.checked_extern; "
         "error operation_error { i32 code; }; "
         "extern \"C\" { i32 operation() throws operation_error; }",
         "R-DIAG-SYN-001",
         "R-GRAM-0005"},
        {"module semantic.await_type; async i32 f(task<i32> work) { u32 value = await move "
         "work; return 0; }",
         "R-DIAG-TYPE-001",
         "R-INIT-0002"},
        {"module semantic.await_non_task; async i32 f(i32 work) { i32 value = await move work; "
         "return 0; }",
         "R-DIAG-ASYNC-001",
         "R-STMT-0012"},
        {"module semantic.await_statement; async void f(task<i32> work) { await move work; }",
         "R-DIAG-ASYNC-001",
         "R-STMT-0012"},
        {"module semantic.await_twice; async i32 f(task<i32> work) { i32 first = await move work; "
         "i32 second = await move work; return first; }",
         "R-DIAG-MOVE-002",
         "R-OWN-0004"},
        {"module semantic.await_borrow; async i32 f(i32 input, task<i32> work) { const i32* "
         "borrowed = &input; i32 value = await move work; value as void; i32 observed = *borrowed; "
         "return observed; }",
         "R-DIAG-ASYNC-001",
         "R-BORROW-0024"},
        {"module semantic.async_capture; async i32 f(const i32* input) { return 0; }",
         "R-DIAG-ASYNC-001",
         "R-BORROW-0024"},
        {"module semantic.async_main_args_after_await; "
         "protected async i32 child() { return 0; } "
         "async i32 main(const str[] args) { "
         "try { task<i32> operation = child(); "
         "i32 value = await move operation; "
         "usize count = len(args); "
         "return value; "
         "} catch (std.async::start_error error) { error as void; return 1; } }",
         "R-DIAG-ASYNC-001",
         "R-BORROW-0024"},
        {"module semantic.async_main_derived_arg_after_await; "
         "protected async i32 child() { return 0; } "
         "async i32 main(const str[] args) { "
         "str argument = args[0]; "
         "try { task<i32> operation = child(); "
         "i32 value = await move operation; "
         "usize count = len(argument); "
         "return value; "
         "} catch (std.async::start_error error) { error as void; return 1; } }",
         "R-DIAG-ASYNC-001",
         "R-BORROW-0024"},
        {"module semantic.async_slice_parts_across_await; "
         "protected async i32 child() { return 0; } "
         "async i32 main() { "
         "i32 storage = 0; "
         "unsafe { "
         "raw const i32* pointer = &storage as raw const i32*; "
         "const i32[] view = core::slice_from_raw_parts(pointer, 1usize); view as void; "
         "try { task<i32> operation = child(); "
         "i32 value = await move operation; usize count = len(view); return value; "
         "} catch (std.async::start_error error) { error as void; return 1; } } }",
         "R-DIAG-ASYNC-001",
         "R-BORROW-0024"},
        {"module semantic.async_main_args_fallthrough; "
         "protected async i32 child() { return 0; } "
         "async i32 main(const str[] args) { "
         "try { task<i32> operation = child(); "
         "switch (0) { "
         "case 0: i32 value = await move operation; fallthrough; "
         "case 1: usize count = len(args); return 2; "
         "default: return 3; "
         "} "
         "return 4; "
         "} catch (std.async::start_error error) { error as void; return 1; } }",
         "R-DIAG-ASYNC-001",
         "R-BORROW-0024"},
        {"module semantic.final_fallthrough; "
         "i32 main() { "
         "switch (0) { case 0: break; default: fallthrough; } "
         "return 0; "
         "}",
         "R-DIAG-FLOW-001",
         "R-STMT-0007"},
        {"module semantic.async_move_argument; async void receive(array<u8> bytes) {} "
         "void launch(array<u8> bytes) throws std.async::start_error { task<void> started = "
         "receive(move bytes); started as void; }",
         "R-DIAG-MOVE-001",
         "R-OWN-0003"},
    };
    size_t case_index;

    for (case_index = 0U; case_index < (sizeof(cases) / sizeof(cases[0])); ++case_index) {
        RFrontendContext *context = r_frontend_create(NULL);
        RSourceId source_id;
        bool found = false;
        size_t found_count = 0U;
        size_t specialized_count = 0U;
        size_t diagnostic_index;

        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        source_id = r_semantic_add_bytes(context,
                                         "semantic-async-negative.r",
                                         (const uint8_t *)cases[case_index].source,
                                         strlen(cases[case_index].source));
        (void)source_id;
        {
            const RFrontendStatus analyze_status = r_frontend_analyze(context);

            R_SEMANTIC_CHECK((analyze_status == R_FRONTEND_OK) ||
                             (analyze_status == R_FRONTEND_INVALID_SOURCE));
            if ((analyze_status == R_FRONTEND_OK) && (r_frontend_diagnostic_count(context) == 0U)) {
                R_SEMANTIC_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_NOT_LOWERABLE);
            }
        }
        for (diagnostic_index = 0U; diagnostic_index < context->diagnostic_count;
             ++diagnostic_index) {
            const RDiagnostic *diagnostic = &context->diagnostics[diagnostic_index];

            if ((strcmp(diagnostic->code, cases[case_index].code) == 0) &&
                (strcmp(diagnostic->rule_id, cases[case_index].rule_id) == 0)) {
                found = true;
                found_count += 1U;
                if (strcmp(diagnostic->message,
                           "async main args or a derived view cannot be used after a suspension") ==
                    0) {
                    specialized_count += 1U;
                }
            }
        }
        R_SEMANTIC_CHECK(found);
        if (strstr(cases[case_index].source, "module semantic.async_main_") != NULL) {
            R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 1U);
            R_SEMANTIC_CHECK(found_count == 1U);
            R_SEMANTIC_CHECK(specialized_count == 1U);
        }
        r_frontend_destroy(context);
    }
}

static void r_semantic_test_poisoned_async_start_recovery(void) {
    static const char source[] = "module semantic.poisoned_async_start;\n"
                                 "protected async void broken(array<u8> bytes) {\n"
                                 "    unknown();\n"
                                 "    return;\n"
                                 "}\n"
                                 "protected async void caller(array<u8> bytes) {\n"
                                 "    try {\n"
                                 "        task<void> operation = broken(move bytes);\n"
                                 "        await move operation;\n"
                                 "    } catch (std.async::start_error error) {\n"
                                 "        error as void;\n"
                                 "    }\n"
                                 "    array<u8> recovered = move bytes;\n"
                                 "    return;\n"
                                 "}\n"
                                 "i32 main() { return 0; }\n";
    RFrontendContext *context = r_frontend_create(NULL);
    size_t diagnostic_index;

    R_SEMANTIC_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    (void)r_semantic_add_bytes(
        context, "poisoned-async-start.r", (const uint8_t *)source, strlen(source));
    R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 1U);
    R_SEMANTIC_CHECK(r_semantic_diagnostic_code_count(context, "R-DIAG-NAME-001") == 1U);
    for (diagnostic_index = 0U; diagnostic_index < context->diagnostic_count; ++diagnostic_index) {
        const RDiagnostic *diagnostic = &context->diagnostics[diagnostic_index];

        R_SEMANTIC_CHECK(strcmp(diagnostic->rule_id, "R-NAME-0003") == 0);
        R_SEMANTIC_CHECK(strstr(diagnostic->message, "transactional async Move") == NULL);
        R_SEMANTIC_CHECK(strcmp(diagnostic->code, "R-DIAG-FLOW-001") != 0);
        R_SEMANTIC_CHECK(strcmp(diagnostic->code, "R-DIAG-BORROW-001") != 0);
        R_SEMANTIC_CHECK(strcmp(diagnostic->code, "R-DIAG-MOVE-002") != 0);
    }
    r_frontend_destroy(context);
}

static void r_semantic_test_async_move_argument_rollback(void) {
    static const struct {
        const char *source;
        const char *path;
        const char *code;
        const char *rule_id;
    } cases[] = {
        {"module semantic.async_move_rollback_type;\n"
         "protected async void receive(array<u8> payload, i32 marker) { return; }\n"
         "protected async void caller(array<u8> bytes) throws std.async::start_error {\n"
         "    task<void> started = "
         "receive(move bytes, false);\n"
         "    std.async::cancel(move started);\n"
         "    return;\n"
         "}\n"
         "i32 main() { return 0; }\n",
         "async-move-rollback-type.r",
         "R-DIAG-TYPE-001",
         "R-EXPR-0020"},
        {"module semantic.async_move_rollback_invalid;\n"
         "protected async void receive(array<u8> payload, i32 marker) { return; }\n"
         "protected async void caller(array<u8> bytes) throws std.async::start_error {\n"
         "    task<void> started = "
         "receive(move bytes, missing);\n"
         "    std.async::cancel(move started);\n"
         "    return;\n"
         "}\n"
         "i32 main() { return 0; }\n",
         "async-move-rollback-invalid.r",
         "R-DIAG-NAME-001",
         "R-NAME-0003"},
    };
    size_t case_index;

    for (case_index = 0U; case_index < (sizeof(cases) / sizeof(cases[0])); ++case_index) {
        RFrontendContext *context = r_frontend_create(NULL);
        const RSemanticSymbol *bytes_symbol = NULL;
        const RSemanticSymbol *started_symbol = NULL;
        RSymbolId bytes_symbol_id = R_SYMBOL_ID_INVALID;
        size_t move_count = 0U;
        size_t symbol_index;
        size_t node_index;
        size_t diagnostic_index;

        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        (void)r_semantic_add_bytes(context,
                                   cases[case_index].path,
                                   (const uint8_t *)cases[case_index].source,
                                   strlen(cases[case_index].source));
        {
            const RFrontendStatus status = r_frontend_analyze(context);
            if (status != R_FRONTEND_INVALID_SOURCE) {
                (void)fprintf(
                    stderr, "negative body case %zu returned status %d\n", case_index, (int)status);
            }
            R_SEMANTIC_CHECK(status == R_FRONTEND_INVALID_SOURCE);
        }
        R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 1U);
        R_SEMANTIC_CHECK(r_semantic_diagnostic_code_count(context, cases[case_index].code) == 1U);
        for (diagnostic_index = 0U; diagnostic_index < context->diagnostic_count;
             ++diagnostic_index) {
            const RDiagnostic *diagnostic = &context->diagnostics[diagnostic_index];

            R_SEMANTIC_CHECK(strcmp(diagnostic->code, cases[case_index].code) == 0);
            R_SEMANTIC_CHECK(strcmp(diagnostic->rule_id, cases[case_index].rule_id) == 0);
            R_SEMANTIC_CHECK(strstr(diagnostic->message, "transactional async Move") == NULL);
            R_SEMANTIC_CHECK(strcmp(diagnostic->code, "R-DIAG-BORROW-001") != 0);
            R_SEMANTIC_CHECK(strcmp(diagnostic->code, "R-DIAG-MOVE-002") != 0);
        }
        for (symbol_index = 0U; symbol_index < context->semantic_symbol_count; ++symbol_index) {
            const RSemanticSymbol *symbol = &context->semantic_symbols[symbol_index];

            if ((symbol->parent == R_SYMBOL_ID_INVALID) ||
                !r_semantic_test_symbol_named(context, symbol->parent, "caller")) {
                continue;
            }
            if ((symbol->kind == R_SEMANTIC_SYMBOL_PARAMETER) &&
                r_semantic_test_symbol_named(context, (RSymbolId)(symbol_index + 1U), "bytes")) {
                bytes_symbol = symbol;
                bytes_symbol_id = (RSymbolId)(symbol_index + 1U);
            } else if ((symbol->kind == R_SEMANTIC_SYMBOL_LOCAL) &&
                       r_semantic_test_symbol_named(
                           context, (RSymbolId)(symbol_index + 1U), "started")) {
                started_symbol = symbol;
            }
        }
        R_SEMANTIC_CHECK((bytes_symbol != NULL) &&
                         (bytes_symbol->object_state == R_SEMANTIC_OBJECT_STATE_INITIALIZED));
        R_SEMANTIC_CHECK((started_symbol != NULL) &&
                         (started_symbol->pending_async_start == R_HIR_NODE_ID_INVALID));
        for (node_index = 0U; node_index < context->hir_node_count; ++node_index) {
            const RHirNode *node = &context->hir_nodes[node_index];

            if ((node->kind == R_HIR_MOVE) && (node->symbol == bytes_symbol_id)) {
                move_count += 1U;
            }
        }
        R_SEMANTIC_CHECK(move_count == 1U);
        r_frontend_destroy(context);
    }
}

static void r_semantic_test_async_move_switch_branch_merge(void) {
    static const char positive_source[] =
        "module semantic.async_move_switch_merge;\n"
        "protected async void consume(array<u8> payload) { return; }\n"
        "protected async void success_reaches(array<u8> bytes) {\n"
        "    try {\n"
        "        task<void> operation = consume(move bytes);\n"
        "        await move operation;\n"
        "    } catch (std.async::start_error error) {\n"
        "        error as void;\n"
        "        return;\n"
        "    }\n"
        "    return;\n"
        "}\n"
        "protected async void failure_reaches(array<u8> bytes) {\n"
        "    try {\n"
        "        task<void> operation = consume(move bytes);\n"
        "        await move operation;\n"
        "        return;\n"
        "    } catch (std.async::start_error error) {\n"
        "        error as void;\n"
        "    }\n"
        "    array<u8> recovered = move bytes;\n"
        "    return;\n"
        "}\n"
        "i32 main() { return 0; }\n";
    static const char normalized_source[] =
        "module semantic.async_move_start_error_cleanup;\n"
        "protected async void consume(array<u8> payload) { return; }\n"
        "protected async void caller(array<u8> bytes) {\n"
        "    try {\n"
        "        task<void> operation = consume(move bytes);\n"
        "        await move operation;\n"
        "    } catch (std.async::start_error error) {\n"
        "        error as void;\n"
        "    }\n"
        "    return;\n"
        "}\n"
        "i32 main() { return 0; }\n";
    RFrontendContext *context = r_frontend_create(NULL);

    R_SEMANTIC_CHECK(context != NULL);
    if (context != NULL) {
        (void)r_semantic_add_bytes(context,
                                   "async-move-switch-merge.r",
                                   (const uint8_t *)positive_source,
                                   strlen(positive_source));
        R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
        R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
        R_SEMANTIC_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
        r_frontend_destroy(context);
    }

    context = r_frontend_create(NULL);
    R_SEMANTIC_CHECK(context != NULL);
    if (context != NULL) {
        (void)r_semantic_add_bytes(context,
                                   "async-move-start-error-cleanup.r",
                                   (const uint8_t *)normalized_source,
                                   strlen(normalized_source));
        R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
        R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
        R_SEMANTIC_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
        r_frontend_destroy(context);
    }
}

static void r_semantic_test_named_move_abi_and_async_arc(void) {
    static const char source[] =
        "module semantic.named_move_abi;\n"
        "struct payload { u32 value; };\n"
        "protected std.fs::directory pass_directory(std.fs::directory value) {\n"
        "    std.fs::directory local = move value;\n"
        "    return move local;\n"
        "}\n"
        "protected std.io::output pass_output(std.io::output value) {\n"
        "    std.io::output local = move value;\n"
        "    return move local;\n"
        "}\n"
        "protected std.io::read_result pass_read_result(std.io::read_result value) {\n"
        "    std.io::read_result local = move value;\n"
        "    return move local;\n"
        "}\n"
        "protected std.io::write_all_result "
        "pass_write_all_result(std.io::write_all_result value) {\n"
        "    std.io::write_all_result local = move value;\n"
        "    return move local;\n"
        "}\n"
        "protected std.fs::write_file_result "
        "pass_write_result(std.fs::write_file_result value) {\n"
        "    std.fs::write_file_result local = move value;\n"
        "    return move local;\n"
        "}\n"
        "protected arc payload pass_arc(arc payload value) {\n"
        "    arc payload local = move value;\n"
        "    return move local;\n"
        "}\n"
        "protected async void consume_arc(arc payload value) { return; }\n"
        "protected async void forward_arc(arc payload value) {\n"
        "    try {\n"
        "        task<void> operation = consume_arc(move value);\n"
        "        await move operation;\n"
        "        return;\n"
        "    } catch (std.async::start_error error) {\n"
        "        error as void;\n"
        "        return;\n"
        "    }\n"
        "}\n"
        "i32 main() { return 0; }\n";
    RFrontendContext *context = r_frontend_create(NULL);

    R_SEMANTIC_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    (void)r_semantic_add_bytes(
        context, "named-move-abi.r", (const uint8_t *)source, strlen(source));
    R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    R_SEMANTIC_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    r_frontend_destroy(context);
}

static void r_semantic_test_send_sync_async_frames(void) {
    static const char positive[] = "module semantic.send_sync_positive;\n"
                                   "struct payload { u32 value; };\n"
                                   "protected async void accept(arc payload value) { return; }\n"
                                   "protected async i32 raw_before_await(task<i32> work) {\n"
                                   "    i32 storage = 0;\n"
                                   "    unsafe {\n"
                                   "        raw i32* pointer = &storage as raw i32*;\n"
                                   "        pointer as void;\n"
                                   "        i32 value = await move work;\n"
                                   "        return value;\n"
                                   "    }\n"
                                   "}\n"
                                   "i32 main() { return 0; }\n";
    static const char non_send_arc[] =
        "module semantic.send_sync_arc_negative;\n"
        "struct payload { rc i32 local; };\n"
        "protected async void reject(arc payload value) { return; }\n"
        "i32 main() { return 0; }\n";
    static const char non_send_local[] = "module semantic.send_sync_local_negative;\n"
                                         "protected async i32 keep_raw(task<i32> work) {\n"
                                         "    i32 storage = 0;\n"
                                         "    unsafe {\n"
                                         "        raw i32* pointer = &storage as raw i32*;\n"
                                         "        i32 value = await move work;\n"
                                         "        pointer as void;\n"
                                         "        return value;\n"
                                         "    }\n"
                                         "}\n"
                                         "i32 main() { return 0; }\n";
    RFrontendContext *context = r_frontend_create(NULL);
    size_t symbol_index;
    bool found_arc = false;
    bool found_task = false;
    bool found_raw = false;

    R_SEMANTIC_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    (void)r_semantic_add_bytes(
        context, "send-sync-positive.r", (const uint8_t *)positive, strlen(positive));
    R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    for (symbol_index = 0U; symbol_index < context->semantic_symbol_count; ++symbol_index) {
        const RSemanticSymbol *symbol = &context->semantic_symbols[symbol_index];

        if ((symbol->kind == R_SEMANTIC_SYMBOL_PARAMETER) &&
            r_semantic_test_symbol_named(context, (RSymbolId)(symbol_index + 1U), "value")) {
            found_arc = true;
            R_SEMANTIC_CHECK(r_semantic_type_is_send(context, symbol->type));
            R_SEMANTIC_CHECK(r_semantic_type_is_sync(context, symbol->type));
        } else if ((symbol->kind == R_SEMANTIC_SYMBOL_PARAMETER) &&
                   r_semantic_test_symbol_named(context, (RSymbolId)(symbol_index + 1U), "work")) {
            found_task = true;
            R_SEMANTIC_CHECK(r_semantic_type_is_send(context, symbol->type));
            R_SEMANTIC_CHECK(!r_semantic_type_is_sync(context, symbol->type));
        } else if ((symbol->kind == R_SEMANTIC_SYMBOL_LOCAL) &&
                   r_semantic_test_symbol_named(
                       context, (RSymbolId)(symbol_index + 1U), "pointer")) {
            found_raw = true;
            R_SEMANTIC_CHECK(!r_semantic_type_is_send(context, symbol->type));
            R_SEMANTIC_CHECK(!r_semantic_type_is_sync(context, symbol->type));
        }
    }
    R_SEMANTIC_CHECK(found_arc && found_task && found_raw);
    R_SEMANTIC_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    r_frontend_destroy(context);

    context = r_frontend_create(NULL);
    R_SEMANTIC_CHECK(context != NULL);
    if (context != NULL) {
        const RDiagnostic *diagnostic;
        const RSemanticSymbol *reject = NULL;

        (void)r_semantic_add_bytes(context,
                                   "send-sync-arc-negative.r",
                                   (const uint8_t *)non_send_arc,
                                   strlen(non_send_arc));
        R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
        R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 1U);
        diagnostic = r_frontend_diagnostic(context, UINT32_C(0));
        R_SEMANTIC_CHECK((diagnostic != NULL) &&
                         (strcmp(diagnostic->code, "R-DIAG-ASYNC-001") == 0) &&
                         (strcmp(diagnostic->rule_id, "R-FUNC-0011") == 0));
        for (symbol_index = 0U; symbol_index < context->semantic_symbol_count; ++symbol_index) {
            const RSemanticSymbol *symbol = &context->semantic_symbols[symbol_index];

            if ((symbol->kind == R_SEMANTIC_SYMBOL_FUNCTION) &&
                r_semantic_test_symbol_named(context, (RSymbolId)(symbol_index + 1U), "reject")) {
                reject = symbol;
                break;
            }
        }
        R_SEMANTIC_CHECK(
            (reject != NULL) && (reject->parameter_count == UINT32_C(1)) &&
            ((size_t)reject->first_parameter_type < context->semantic_parameter_type_count));
        if ((reject != NULL) && (reject->parameter_count == UINT32_C(1)) &&
            ((size_t)reject->first_parameter_type < context->semantic_parameter_type_count)) {
            const RTypeId type = context->semantic_parameter_types[reject->first_parameter_type];

            R_SEMANTIC_CHECK(!r_semantic_type_is_send(context, type));
            R_SEMANTIC_CHECK(!r_semantic_type_is_sync(context, type));
        }
        r_frontend_destroy(context);
    }

    context = r_frontend_create(NULL);
    R_SEMANTIC_CHECK(context != NULL);
    if (context != NULL) {
        const RDiagnostic *diagnostic;

        (void)r_semantic_add_bytes(context,
                                   "send-sync-local-negative.r",
                                   (const uint8_t *)non_send_local,
                                   strlen(non_send_local));
        R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
        R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
        R_SEMANTIC_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_NOT_LOWERABLE);
        R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 1U);
        diagnostic = r_frontend_diagnostic(context, UINT32_C(0));
        R_SEMANTIC_CHECK((diagnostic != NULL) &&
                         (strcmp(diagnostic->code, "R-DIAG-ASYNC-001") == 0) &&
                         (strcmp(diagnostic->rule_id, "R-FUNC-0011") == 0));
        r_frontend_destroy(context);
    }
}

static void r_semantic_test_shared_owner_await_liveness(void) {
    static const char arc_source[] = "module semantic.arc_await_liveness;\n"
                                     "struct Item { i32 value; };\n"
                                     "protected async i32 keep(task<i32> work) {\n"
                                     "    arc Item owner = new arc Item { .value = 5 };\n"
                                     "    i32 awaited = await move work;\n"
                                     "    arc Item clone = std.arc::clone(&owner);\n"
                                     "    return clone->value + awaited;\n"
                                     "}\n"
                                     "i32 main() { return 0; }\n";
    static const char rc_source[] = "module semantic.rc_await_liveness;\n"
                                    "struct Item { i32 value; };\n"
                                    "protected async i32 reject(task<i32> work) {\n"
                                    "    rc Item owner = new rc Item { .value = 7 };\n"
                                    "    i32 awaited = await move work;\n"
                                    "    rc Item clone = std.rc::clone(&owner);\n"
                                    "    return clone->value + awaited;\n"
                                    "}\n"
                                    "i32 main() { return 0; }\n";
    RFrontendContext *context = r_frontend_create(NULL);
    RSourceId source_id = R_SOURCE_ID_INVALID;
    size_t arc_clone_count = 0U;
    size_t await_count = 0U;
    size_t index;

    R_SEMANTIC_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_SEMANTIC_CHECK(r_frontend_add_source(context,
                                           "arc-await-liveness.r",
                                           (const uint8_t *)arc_source,
                                           strlen(arc_source),
                                           &source_id) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    R_SEMANTIC_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    for (index = 0U; index < context->mir_instruction_count; ++index) {
        const RMirInstruction *instruction = &context->mir_instructions[index];

        if ((instruction->kind == R_MIR_INSTRUCTION_STANDARD_CALL) &&
            (instruction->standard_operation == R_STANDARD_CALL_ARC_CLONE)) {
            arc_clone_count += 1U;
        } else if (instruction->kind == R_MIR_INSTRUCTION_AWAIT) {
            await_count += 1U;
        }
    }
    R_SEMANTIC_CHECK(arc_clone_count == 1U);
    R_SEMANTIC_CHECK(await_count == 1U);
    r_frontend_destroy(context);

    context = r_frontend_create(NULL);
    R_SEMANTIC_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    source_id = R_SOURCE_ID_INVALID;
    R_SEMANTIC_CHECK(r_frontend_add_source(context,
                                           "rc-await-liveness.r",
                                           (const uint8_t *)rc_source,
                                           strlen(rc_source),
                                           &source_id) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    R_SEMANTIC_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_NOT_LOWERABLE);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 1U);
    if (r_frontend_diagnostic_count(context) == 1U) {
        const RDiagnostic *diagnostic = r_frontend_diagnostic(context, UINT32_C(0));

        R_SEMANTIC_CHECK((diagnostic != NULL) &&
                         (strcmp(diagnostic->code, "R-DIAG-ASYNC-001") == 0) &&
                         (strcmp(diagnostic->rule_id, "R-FUNC-0011") == 0) &&
                         (diagnostic->phase == R_DIAGNOSTIC_PHASE_SEMANTIC));
    }
    r_frontend_destroy(context);
}

static void r_semantic_test_string_views_and_await_liveness(void) {
    static const char positive[] = "module semantic.string_views;\n"
                                   "const u8[] as_bytes(str text) { return text; }\n"
                                   "protected async i32 child() { return 7; }\n"
                                   "protected async i32 dead_borrow(i32 input, task<i32> work) {\n"
                                   "    const i32* borrowed = &input;\n"
                                   "    i32 observed = *borrowed;\n"
                                   "    i32 value = await move work;\n"
                                   "    return value;\n"
                                   "}\n"
                                   "protected async i32 overwrite(task<i32> work) {\n"
                                   "    constexpr str first = \"a\";\n"
                                   "    constexpr str second = \"b\";\n"
                                   "    str view = first;\n"
                                   "    i32 value = await move work;\n"
                                   "    str current_view = second;\n"
                                   "    usize length = len(current_view);\n"
                                   "    return value;\n"
                                   "}\n"
                                   "async i32 main() {\n"
                                   "    constexpr str text = \"abc\";\n"
                                   "    str view = text;\n"
                                   "    const u8[] bytes = view;\n"
                                   "    usize length = len(bytes);\n"
                                   "    try {\n"
                                   "        task<i32> operation = child();\n"
                                   "            i32 value = await move operation;\n"
                                   "            return value;\n"
                                   "    } catch (std.async::start_error error) {\n"
                                   "        error as void;\n"
                                   "        return 1;\n"
                                   "    }\n"
                                   "}\n";
    static const struct {
        const char *source;
        bool fails_during_mir;
    } negative[] = {
        {"module semantic.string_chain; i32 main() { const u8[] bytes = \"abc\"; return 0; }",
         false},
        {"module semantic.string_reverse; i32 main() { str view = \"abc\"; "
         "constexpr str text = view; return 0; }",
         false},
        {"module semantic.string_from_bytes; i32 main() { u8[1] data = {1}; "
         "const u8[] bytes = &data; str view = bytes; return 0; }",
         false},
        {"module semantic.string_live_after_await; protected async i32 child() { return 0; } "
         "protected async i32 run() throws std.async::start_error { "
         "constexpr str text = \"abc\"; str view = text; task<i32> operation = child(); "
         "i32 value = await move operation; usize length = len(view); return value; } "
         "i32 main() { return 0; }",
         true},
        {"module semantic.string_live_branch; protected async i32 child() { return 0; } "
         "protected async i32 run() throws std.async::start_error { "
         "constexpr str text = \"abc\"; str view = text; task<i32> operation = child(); "
         "i32 value = await move operation; "
         "if (value == 0) { usize length = len(view); } else { value += 1; } return value; "
         "} i32 main() { return 0; }",
         true},
    };
    RFrontendContext *context = r_frontend_create(NULL);
    size_t hir_cast_count = 0U;
    size_t hir_origin_count = 0U;
    size_t mir_cast_count = 0U;
    size_t mir_origin_count = 0U;
    size_t index;

    R_SEMANTIC_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    (void)r_semantic_add_bytes(
        context, "string-views.r", (const uint8_t *)positive, strlen(positive));
    R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    for (index = 0U; index < context->hir_node_count; ++index) {
        const RHirNode *node = &context->hir_nodes[index];

        if ((node->kind == R_HIR_CAST) && (node->operation == R_TOKEN_INVALID)) {
            hir_cast_count += 1U;
            if (node->borrow_origin != R_SYMBOL_ID_INVALID) {
                hir_origin_count += 1U;
            }
        }
    }
    R_SEMANTIC_CHECK(hir_cast_count == 5U);
    R_SEMANTIC_CHECK(hir_origin_count == 1U);
    R_SEMANTIC_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    for (index = 0U; index < context->mir_instruction_count; ++index) {
        const RMirInstruction *instruction = &context->mir_instructions[index];

        if ((instruction->kind == R_MIR_INSTRUCTION_CAST) &&
            (instruction->operation == R_TOKEN_INVALID)) {
            mir_cast_count += 1U;
            if (instruction->borrow_origin != R_SYMBOL_ID_INVALID) {
                mir_origin_count += 1U;
            }
        }
    }
    R_SEMANTIC_CHECK(mir_cast_count == hir_cast_count);
    R_SEMANTIC_CHECK(mir_origin_count == hir_origin_count);
    r_frontend_destroy(context);

    for (index = 0U; index < (sizeof(negative) / sizeof(negative[0])); ++index) {
        const RDiagnostic *diagnostic;

        context = r_frontend_create(NULL);
        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        (void)r_semantic_add_bytes(context,
                                   "string-views-negative.r",
                                   (const uint8_t *)negative[index].source,
                                   strlen(negative[index].source));
        if (negative[index].fails_during_mir) {
            R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
            R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
            R_SEMANTIC_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_NOT_LOWERABLE);
            R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 1U);
            diagnostic = r_frontend_diagnostic(context, UINT32_C(0));
            R_SEMANTIC_CHECK((diagnostic != NULL) &&
                             (strcmp(diagnostic->code, "R-DIAG-ASYNC-001") == 0) &&
                             (strcmp(diagnostic->rule_id, "R-BORROW-0024") == 0));
        } else {
            R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
            R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 1U);
            diagnostic = r_frontend_diagnostic(context, UINT32_C(0));
            R_SEMANTIC_CHECK((diagnostic != NULL) &&
                             (strcmp(diagnostic->code, "R-DIAG-TYPE-001") == 0) &&
                             (strcmp(diagnostic->rule_id, "R-INIT-0002") == 0));
        }
        r_frontend_destroy(context);
    }
}

static void r_semantic_test_sync_main_arguments(void) {
    static const char accepted[] = "module semantic.sync_main_args; "
                                   "i32 main(const str[] args) { "
                                   "usize count = len(args); "
                                   "str argument = args[0]; "
                                   "usize length = len(argument); "
                                   "if (count == 0) { return 1; } else { "
                                   "if (length == 0) { return 2; } else { return 0; } "
                                   "} "
                                   "}";
    /* L10.1: `const str[]` is an ordinary parameter type; a function named main with another
       form is an ordinary function and leaves the program without an entry point. */
    static const struct {
        const char *source;
        size_t entry_points;
    } ordinary[] = {
        {"module semantic.non_main_args; "
         "protected i32 helper(const str[] args) { return 0; } "
         "i32 main() { return 0; }",
         1U},
        {"module semantic.wrong_main_result; "
         "i64 main(const str[] args) { return 0; }",
         0U},
        {"module semantic.protected_main_args; "
         "protected i32 main(const str[] args) { return 0; }",
         0U},
        /* L11.3: `str[]` is an ordinary parameter type as well. */
        {"module semantic.mutable_main_args; "
         "i32 main(str[] args) { return 0; }",
         0U},
    };
    RFrontendContext *context = r_frontend_create(NULL);
    RSourceId source_id = R_SOURCE_ID_INVALID;
    size_t case_index;

    R_SEMANTIC_CHECK(context != NULL);
    if (context != NULL) {
        R_SEMANTIC_CHECK(r_frontend_add_source(context,
                                               "sync-main-args.r",
                                               (const uint8_t *)accepted,
                                               strlen(accepted),
                                               &source_id) == R_FRONTEND_OK);
        R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
        R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
        R_SEMANTIC_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
        r_frontend_destroy(context);
    }

    for (case_index = 0U; case_index < (sizeof(ordinary) / sizeof(ordinary[0])); ++case_index) {
        context = r_frontend_create(NULL);
        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        source_id = R_SOURCE_ID_INVALID;
        R_SEMANTIC_CHECK(r_frontend_add_source(context,
                                               "sync-main-args-ordinary.r",
                                               (const uint8_t *)ordinary[case_index].source,
                                               strlen(ordinary[case_index].source),
                                               &source_id) == R_FRONTEND_OK);
        R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
        R_SEMANTIC_CHECK(r_frontend_entry_point_count(context) ==
                         ordinary[case_index].entry_points);
        r_frontend_destroy(context);
    }
}

static void r_semantic_test_associated_names_do_not_collide(void) {
    static const char source_text[] =
        "module semantic.associated;\n"
        "struct Key { i32 value; };\n"
        "u64 Key::hash(const Key* value) { return 0; }\n"
        "bool Key::equal(const Key* left, const Key* right) { return true; }\n";
    RFrontendContext *context = r_frontend_create(NULL);
    RSourceId source_id;
    size_t function_count = 0U;
    size_t symbol_index;

    R_SEMANTIC_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    source_id = r_semantic_add_bytes(
        context, "semantic-associated.r", (const uint8_t *)source_text, strlen(source_text));
    (void)source_id;
    R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    R_SEMANTIC_CHECK(
        !r_semantic_has_diagnostic(context, "R-DIAG-NAME-002", R_DIAGNOSTIC_PHASE_SEMANTIC));
    for (symbol_index = 0U; symbol_index < context->semantic_symbol_count; ++symbol_index) {
        if (context->semantic_symbols[symbol_index].kind == R_SEMANTIC_SYMBOL_FUNCTION) {
            function_count += 1U;
        }
    }
    R_SEMANTIC_CHECK(function_count == 2U);
    r_frontend_destroy(context);
}

static void r_semantic_test_invalid_ast_is_internal_error(void) {
    static const char source_text[] = "module semantic.invalid_ast;\n"
                                      "i32 declaration();\n";
    RFrontendContext *context = r_frontend_create(NULL);
    RSourceId source_id;
    RAstNodeId ast = R_AST_NODE_ID_INVALID;
    RSource *source;

    R_SEMANTIC_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    source_id = r_semantic_add_bytes(
        context, "invalid-ast.r", (const uint8_t *)source_text, strlen(source_text));
    R_SEMANTIC_CHECK(r_frontend_lower_ast(context, source_id, &ast) == R_FRONTEND_OK);
    source = r_get_source(context, source_id);
    R_SEMANTIC_CHECK(source != NULL);
    if (source != NULL) {
        source->ast_root = (RAstNodeId)(source->ast_node_count + 1U);
        R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_INTERNAL_ERROR);
        R_SEMANTIC_CHECK(r_frontend_hir_root(context) == R_HIR_NODE_ID_INVALID);
    }
    r_frontend_destroy(context);
}

static void r_semantic_test_empty_analyze_does_not_seal(void) {
    static const uint8_t source_text[] = "module semantic.after_empty;\n";
    RFrontendContext *context = r_frontend_create(NULL);
    RSourceId source_id = R_SOURCE_ID_INVALID;

    R_SEMANTIC_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_ARGUMENT);
    R_SEMANTIC_CHECK(
        r_frontend_add_source(
            context, "after-empty.r", source_text, sizeof(source_text) - 1U, &source_id) ==
        R_FRONTEND_OK);
    R_SEMANTIC_CHECK(source_id != R_SOURCE_ID_INVALID);
    r_frontend_destroy(context);
}

static void r_semantic_test_body_semantics(void) {
    static const char positive_source[] =
        "module semantic.body_positive;\n"
        "u32 prior(u32 value);\n"
        "u32 recurse(u32 value) {\n"
        "  if (value == 0) { return 0; } else {\n"
        "    u32 next = value - 1;\n"
        "    u32 result = prior(next);\n"
        "    return result;\n"
        "  }\n"
        "}\n"
        "u32 use(u32 value) {\n"
        "  u32 maximum = 4294967295;\n"
        "  u32 adjusted = 3;\n"
        "  adjusted += 1;\n"
        "  u32 passed = prior(4); passed as void;\n"
        "  return maximum;\n"
        "}\n"
        "u32 prior(u32 value) { return value; }\n"
        "i32 minimum() { return -2147483648; }\n"
        "u32 modulo_minus() { return -1u32; }\n"
        "i32 grouped_condition(i32 value) {\n"
        "  if ((true || false) && ((value < 3) || (value == 7))) { return 1; }\n"
        "  else { return 0; }\n"
        "}\n"
        "i32 literal_true_branch() { if (true) { return 1; } }\n"
        "i32 literal_false_else() { if (false) { } else { return 2; } }\n"
        "i32 constant_comparison() { if (1 < 2) { return 3; } }\n"
        "i32 large_u32_rhs(u32 value) { if (value == 4294967290) { return 1; } else { return 0; } "
        "}\n"
        "i32 large_u32_lhs(u32 value) { if (4294967290 == value) { return 1; } else { return 0; } "
        "}\n"
        "u32 large_u32_add(u32 value) { u32 result = value + 4294967290; return result; }\n"
        "u32 large_u32_sub(u32 value) { u32 result = 4294967290 - value; return result; }\n"
        "u32 contextual_add_local() { u32 result = 4294967290 + 1; return result; }\n"
        "u32 contextual_add_return() { return 4294967290 + 1; }\n"
        "u32 contextual_add_wrap() { u32 result = 4294967295 + 1; return result; }\n"
        "u32 mixed_integer(u32 value) { i32 other = 1; u32 result = value + other; return result; "
        "}\n"
        "bool mixed_comparison(u32 value) { return value == 1i32; }\n"
        "i32 infinite_loop() { while (true) { } }\n"
        "i32 returning_loop() { while (true) { return 4; } }\n";
    struct RBodyNegativeCase {
        const char *source;
        const char *code;
    };
    static const struct RBodyNegativeCase cases[] = {
        {"module semantic.self_init; i32 f() { i32 value = value; return 0; }", "R-DIAG-NAME-001"},
        {"module semantic.duplicate_local; i32 f() { i32 value = 1; i32 value = 2; return value; }",
         "R-DIAG-NAME-002"},
        {"module semantic.shadow_parameter; i32 f(i32 value) { i32 value = 1; return value; }",
         "R-DIAG-NAME-003"},
        {"module semantic.local_len; i32 f() { i32 len = 1; return len; }", "R-DIAG-NAME-003"},
        {"module semantic.forward_call; i32 first() { i32 value = second(); return value; } i32 "
         "second() { return 0; }",
         "R-DIAG-NAME-001"},
        {"module semantic.call_arity; i32 take(i32 value); i32 f() { i32 value = take(); return "
         "value; }",
         "R-DIAG-TYPE-001"},
        {"module semantic.call_type; i32 take(i32 value); i32 f() { bool flag = true; i32 value = "
         "take(flag); return value; }",
         "R-DIAG-TYPE-001"},
        {"module semantic.len_arity; i32 f() { usize size = len(); return 0; }", "R-DIAG-TYPE-001"},
        {"module semantic.len_type; i32 f() { i32 value = 1; usize size = len(value); return 0; }",
         "R-DIAG-TYPE-001"},
        {"module semantic.return_type; i32 f() { return true; }", "R-DIAG-TYPE-001"},
        {"module semantic.logic_type; bool f(i32 value) { return value && true; }",
         "R-DIAG-TYPE-002"},
        {"module semantic.condition_type; i32 f() { if (1 < true) { return 1; } else { return 0; } "
         "}",
         "R-DIAG-TYPE-001"},
        {"module semantic.missing_return; i32 f() { i32 value = 1; }", "R-DIAG-FLOW-001"},
        {"module semantic.missing_async_return; protected async i32 f() {}", "R-DIAG-FLOW-001"},
        {"module semantic.missing_checked_value; error issue { i32 code; }; "
         "i32 f() throws issue {}",
         "R-DIAG-FLOW-001"},
        {"module semantic.missing_async_checked_value; error issue { i32 code; }; "
         "protected async i32 f() throws issue {}",
         "R-DIAG-FLOW-001"},
        {"module semantic.missing_option_value; o<i32> f() {}", "R-DIAG-FLOW-001"},
        {"module semantic.unsupported_promotion; u16 f(u16 left, u16 right) { return left + right; "
         "}",
         "R-DIAG-TYPE-001"},
        {"module semantic.context_overflow; u32 f() { return 4294967296; }", "R-DIAG-CONST-001"},
        {"module semantic.context_expression_overflow; u32 f() { u32 result = 4294967296 + 1; "
         "return 0; }",
         "R-DIAG-CONST-001"},
        {"module semantic.context_negative; u32 f() { return -1; }", "R-DIAG-CONST-001"},
        {"module semantic.static_local; i32 f(i32 input) { static i32 value = input; return 0; }",
         "R-DIAG-INIT-001"},
        {"module semantic.import_cascade; import dependency.api::{provided}; i32 f() { i32 value = "
         "provided(); return value; }",
         "R-DIAG-MOD-001"},
        {"module semantic.object_cascade; i32 stored = 7; i32 f() { return stored; }",
         "R-DIAG-MEM-001"},
        {"module semantic.import_shadow_boundary; import dependency.api::{provided}; i32 f(i32 "
         "provided) { return provided; }",
         "R-DIAG-MOD-001"},
        {"module semantic.parameter_function; i32 helper() { return 1; } i32 f(i32 helper) { "
         "return helper; }",
         "R-DIAG-NAME-003"},
        {"module semantic.path_terminal; i32 f(i32 path_terminal) { return path_terminal; }",
         "R-DIAG-NAME-003"},
        {"module semantic.path_root; i32 f(i32 semantic) { return semantic; }", "R-DIAG-NAME-003"},
        {"module semantic.false_if_flow; i32 f() { if (false) { return 1; } }", "R-DIAG-FLOW-001"},
        {"module semantic.false_while_flow; i32 f() { while (false) { return 1; } }",
         "R-DIAG-FLOW-001"},
    };
    RFrontendContext *context = r_frontend_create(NULL);
    RSourceId source_id;
    size_t hir_index;
    size_t place_count = 0U;
    size_t load_count = 0U;
    size_t compound_count = 0U;
    size_t large_u32_literal_count = 0U;
    size_t maximum_u32_literal_count = 0U;
    size_t contextual_u32_add_count = 0U;
    bool has_minimum_bits = false;
    bool large_literal_has_wrong_type = false;
    size_t case_index;

    R_SEMANTIC_CHECK(context != NULL);
    if (context != NULL) {
        source_id = r_semantic_add_bytes(
            context, "body-positive.r", (const uint8_t *)positive_source, strlen(positive_source));
        (void)source_id;
        R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
        R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
        for (hir_index = 0U; hir_index < context->hir_node_count; ++hir_index) {
            const RHirNode *node = &context->hir_nodes[hir_index];
            if (node->kind == R_HIR_PLACE) {
                place_count += 1U;
            } else if (node->kind == R_HIR_LOAD) {
                load_count += 1U;
            } else if (node->kind == R_HIR_COMPOUND_ASSIGN) {
                compound_count += 1U;
            } else if ((node->kind == R_HIR_LITERAL) && (node->type != R_TYPE_ID_INVALID) &&
                       (node->integer_value == UINT64_C(0x80000000))) {
                const RSemanticType *type = r_semantic_type(context, node->type);
                has_minimum_bits = (type != NULL) && (type->kind == R_SEMANTIC_TYPE_I32);
            } else if ((node->kind == R_HIR_LITERAL) &&
                       (node->integer_value == UINT64_C(4294967290))) {
                const RSemanticType *type = r_semantic_type(context, node->type);
                if ((type != NULL) && (type->kind == R_SEMANTIC_TYPE_U32)) {
                    large_u32_literal_count += 1U;
                } else {
                    large_literal_has_wrong_type = true;
                }
            } else if ((node->kind == R_HIR_LITERAL) &&
                       (node->integer_value == (uint64_t)UINT32_MAX)) {
                const RSemanticType *type = r_semantic_type(context, node->type);
                if ((type != NULL) && (type->kind == R_SEMANTIC_TYPE_U32)) {
                    maximum_u32_literal_count += 1U;
                } else {
                    large_literal_has_wrong_type = true;
                }
            }
            if ((node->kind == R_HIR_BINARY) && (node->operation == R_TOKEN_PLUS)) {
                const RSemanticType *type = r_semantic_type(context, node->type);
                if ((type != NULL) && (type->kind == R_SEMANTIC_TYPE_U32)) {
                    contextual_u32_add_count += 1U;
                }
            }
        }
        R_SEMANTIC_CHECK(place_count != 0U);
        R_SEMANTIC_CHECK(load_count != 0U);
        R_SEMANTIC_CHECK(compound_count == 1U);
        R_SEMANTIC_CHECK(has_minimum_bits);
        R_SEMANTIC_CHECK(large_u32_literal_count == 6U);
        R_SEMANTIC_CHECK(maximum_u32_literal_count == 2U);
        R_SEMANTIC_CHECK(contextual_u32_add_count == 5U);
        R_SEMANTIC_CHECK(!large_literal_has_wrong_type);
        r_frontend_destroy(context);
    }

    for (case_index = 0U; case_index < (sizeof(cases) / sizeof(cases[0])); ++case_index) {
        context = r_frontend_create(NULL);
        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        source_id = r_semantic_add_bytes(context,
                                         "body-negative.r",
                                         (const uint8_t *)cases[case_index].source,
                                         strlen(cases[case_index].source));
        (void)source_id;
        {
            const RFrontendStatus status = r_frontend_analyze(context);
            if (status != R_FRONTEND_INVALID_SOURCE) {
                (void)fprintf(
                    stderr, "negative body case %zu returned status %d\n", case_index, (int)status);
            }
            R_SEMANTIC_CHECK(status == R_FRONTEND_INVALID_SOURCE);
        }
        if (r_semantic_diagnostic_code_count(context, cases[case_index].code) != 1U) {
            size_t diagnostic_index;
            (void)fprintf(stderr,
                          "negative body case %zu expected one %s (got %zu diagnostics total)\n",
                          case_index,
                          cases[case_index].code,
                          r_frontend_diagnostic_count(context));
            for (diagnostic_index = 0U; diagnostic_index < r_frontend_diagnostic_count(context);
                 ++diagnostic_index) {
                const RDiagnostic *diagnostic = r_frontend_diagnostic(context, diagnostic_index);
                if (diagnostic != NULL) {
                    (void)fprintf(stderr, "  %s %s\n", diagnostic->code, diagnostic->message);
                }
            }
        }
        R_SEMANTIC_CHECK(r_semantic_diagnostic_code_count(context, cases[case_index].code) == 1U);
        if (strcmp(cases[case_index].code, "R-DIAG-SLICE-001") == 0) {
            R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 1U);
            R_SEMANTIC_CHECK(r_semantic_diagnostic_code_count(context, "R-DIAG-SLICE-001") == 1U);
            R_SEMANTIC_CHECK(!r_semantic_has_diagnostic(
                context, "R-DIAG-NAME-001", R_DIAGNOSTIC_PHASE_SEMANTIC));
            R_SEMANTIC_CHECK(!r_semantic_has_diagnostic(
                context, "R-DIAG-FLOW-001", R_DIAGNOSTIC_PHASE_SEMANTIC));
        } else if (strcmp(cases[case_index].code, "R-DIAG-CONST-001") == 0) {
            R_SEMANTIC_CHECK(!r_semantic_has_diagnostic(
                context, "R-DIAG-SLICE-001", R_DIAGNOSTIC_PHASE_SEMANTIC));
        }
        r_frontend_destroy(context);
    }

    {
        static const char const_return_source[] =
            "module semantic.const_return; const i32 bad() { return 1; }";
        context = r_frontend_create(NULL);
        R_SEMANTIC_CHECK(context != NULL);
        if (context != NULL) {
            source_id = r_semantic_add_bytes(context,
                                             "const-return.r",
                                             (const uint8_t *)const_return_source,
                                             strlen(const_return_source));
            (void)source_id;
            R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
            R_SEMANTIC_CHECK(r_semantic_diagnostic_code_count(context, "R-DIAG-TYPE-001") == 1U);
            R_SEMANTIC_CHECK(r_semantic_diagnostic_code_count(context, "R-DIAG-SLICE-001") == 0U);
            R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 1U);
            r_frontend_destroy(context);
        }
    }
}

static RFrontendContext *r_semantic_prepare_oom_context(RSemanticTestAllocator *allocator) {
    static const char source_text[] = "module semantic.oom;\n"
                                      "protected i32 first(i32 value);\n"
                                      "protected bool second(bool value);\n";
    RFrontendOptions options = r_frontend_default_options();
    RFrontendContext *context;
    RSourceId source_id = R_SOURCE_ID_INVALID;
    RAstNodeId ast = R_AST_NODE_ID_INVALID;

    options.allocate = r_semantic_test_allocate;
    options.free = r_semantic_test_free;
    options.allocator_user_data = allocator;
    context = r_frontend_create(&options);
    if (context == NULL) {
        return NULL;
    }
    if ((r_frontend_add_source(context,
                               "semantic-oom.r",
                               (const uint8_t *)source_text,
                               strlen(source_text),
                               &source_id) != R_FRONTEND_OK) ||
        (r_frontend_lower_ast(context, source_id, &ast) != R_FRONTEND_OK)) {
        r_frontend_destroy(context);
        return NULL;
    }
    return context;
}

static void r_semantic_test_oom_sweep(void) {
    RSemanticTestAllocator baseline_allocator = {0};
    RFrontendContext *baseline = r_semantic_prepare_oom_context(&baseline_allocator);
    size_t before;
    size_t semantic_allocations;
    size_t offset;

    R_SEMANTIC_CHECK(baseline != NULL);
    if (baseline == NULL) {
        return;
    }
    before = baseline_allocator.call_count;
    R_SEMANTIC_CHECK(r_frontend_analyze(baseline) == R_FRONTEND_OK);
    semantic_allocations = baseline_allocator.call_count - before;
    r_frontend_destroy(baseline);
    R_SEMANTIC_CHECK(baseline_allocator.live_count == 0U);
    R_SEMANTIC_CHECK(semantic_allocations != 0U);

    for (offset = 1U; offset <= semantic_allocations; ++offset) {
        RSemanticTestAllocator allocator = {0};
        RFrontendContext *context = r_semantic_prepare_oom_context(&allocator);
        RFrontendStatus status;
        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        allocator.fail_at = allocator.call_count + offset;
        status = r_frontend_analyze(context);
        R_SEMANTIC_CHECK((status == R_FRONTEND_OUT_OF_MEMORY) ||
                         (status == R_FRONTEND_LIMIT_EXCEEDED));
        R_SEMANTIC_CHECK(r_frontend_hir_root(context) == R_HIR_NODE_ID_INVALID);
        r_frontend_destroy(context);
        R_SEMANTIC_CHECK(allocator.live_count == 0U);
    }
}

static RFrontendContext *r_semantic_prepare_type_growth_context(RSemanticTestAllocator *allocator) {
    static const char source_text[] =
        "module semantic.type_growth;\n"
        "void types(const bool p00, const i8 p01, const i16 p02, const i32 p03, "
        "const i64 p04, const isize p05, const u8 p06, const u16 p07, "
        "const u32 p08, const u64 p09, const usize p10, const f32 p11, "
        "const f64 p12, const char p13, const void p14);\n";
    RFrontendOptions options = r_frontend_default_options();
    RFrontendContext *context;
    RSourceId source_id = R_SOURCE_ID_INVALID;
    RAstNodeId ast = R_AST_NODE_ID_INVALID;

    options.allocate = r_semantic_test_allocate;
    options.free = r_semantic_test_free;
    options.allocator_user_data = allocator;
    context = r_frontend_create(&options);
    if (context == NULL) {
        return NULL;
    }
    if ((r_frontend_add_source(context,
                               "semantic-type-growth.r",
                               (const uint8_t *)source_text,
                               strlen(source_text),
                               &source_id) != R_FRONTEND_OK) ||
        (r_frontend_lower_ast(context, source_id, &ast) != R_FRONTEND_OK)) {
        r_frontend_destroy(context);
        return NULL;
    }
    return context;
}

static void r_semantic_test_late_type_oom_sweep(void) {
    RSemanticTestAllocator baseline_allocator = {0};
    RFrontendContext *baseline = r_semantic_prepare_type_growth_context(&baseline_allocator);
    size_t before;
    size_t semantic_allocations;
    size_t offset;

    R_SEMANTIC_CHECK(baseline != NULL);
    if (baseline == NULL) {
        return;
    }
    before = baseline_allocator.call_count;
    R_SEMANTIC_CHECK(r_frontend_analyze(baseline) == R_FRONTEND_INVALID_SOURCE);
    semantic_allocations = baseline_allocator.call_count - before;
    R_SEMANTIC_CHECK(baseline->semantic_type_count > 32U);
    r_frontend_destroy(baseline);
    R_SEMANTIC_CHECK(baseline_allocator.live_count == 0U);

    for (offset = 1U; offset <= semantic_allocations; ++offset) {
        RSemanticTestAllocator allocator = {0};
        RFrontendContext *context = r_semantic_prepare_type_growth_context(&allocator);
        RFrontendStatus status;
        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        allocator.fail_at = allocator.call_count + offset;
        status = r_frontend_analyze(context);
        R_SEMANTIC_CHECK((status == R_FRONTEND_OUT_OF_MEMORY) ||
                         (status == R_FRONTEND_LIMIT_EXCEEDED));
        r_frontend_destroy(context);
        R_SEMANTIC_CHECK(allocator.live_count == 0U);
    }
}

static void r_semantic_test_switch_type_growth_stability(void) {
    static const char source_prefix[] = "module test.codegen.sync_aggregate_destination;\n"
                                        "struct payload { u8[65_536] bytes; usize length; };\n"
                                        "error failure { i32 code; };\n"
                                        "struct text_holder { constexpr str text; };\n";
    static const char source_suffix[] =
        "protected payload make_payload(bool fail) throws failure {\n"
        "  payload value = {};\n"
        "  if (fail == true) { failure error = { .code = 7 }; throw error; }\n"
        "  value.bytes[0] = 42; value.length = 1; return value;\n"
        "}\n"
        "protected usize empty_text_length() {\n"
        "  text_holder value = {}; usize length = len(value.text); return length;\n"
        "}\n"
        "i32 main() {\n"
        "  usize text_length = empty_text_length();\n"
        "  if (text_length != 0) { return 1; }\n"
        "  try {\n"
        "    payload value = make_payload(false);\n"
        "    if (value.length != 1) { return 2; }\n"
        "    if (value.bytes[0] != 42) { return 3; }\n"
        "    return 0;\n"
        "  } catch (failure error) { return error.code; }\n"
        "}\n";
    RSemanticTestBuffer source = {0};
    RFrontendContext *context = r_frontend_create(NULL);
    size_t shape_index;
    bool source_ready;

    R_SEMANTIC_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    source_ready = r_semantic_test_write(&source, source_prefix, strlen(source_prefix));
    for (shape_index = 1U; source_ready && (shape_index <= 32U); ++shape_index) {
        char declaration[80];
        const int length = snprintf(declaration,
                                    sizeof(declaration),
                                    "struct shape_%zu { u8[%zu] value; };\n",
                                    shape_index,
                                    shape_index);

        source_ready = (length > 0) && ((size_t)length < sizeof(declaration)) &&
                       r_semantic_test_write(&source, declaration, (size_t)length);
    }
    source_ready =
        source_ready && r_semantic_test_write(&source, source_suffix, strlen(source_suffix));
    R_SEMANTIC_CHECK(source_ready);
    if (!source_ready) {
        free(source.bytes);
        r_frontend_destroy(context);
        return;
    }
    (void)r_semantic_add_bytes(
        context, "semantic-switch-type-growth.r", (const uint8_t *)source.bytes, source.length);
    free(source.bytes);
    R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    R_SEMANTIC_CHECK(context->semantic_type_count > 128U);
    r_frontend_destroy(context);
}

static RFrontendContext *r_semantic_prepare_body_oom_context(RSemanticTestAllocator *allocator) {
    static const char source_text[] =
        "module semantic.body_oom;\n"
        "i32 add(i32 left, i32 right) { i32 sum = left + right; return sum; }\n"
        "f32 exact_float() { return 0x1.000002p+0f32; }\n"
        "i32 require(bool condition) { unsafe { core::assume(condition); } return 0; }\n"
        "i32 run(i32 value) { value += 1; if (value > 2) { return value; } else { return -value; } "
        "}\n";
    RFrontendOptions options = r_frontend_default_options();
    RFrontendContext *context;
    RSourceId source_id = R_SOURCE_ID_INVALID;
    RAstNodeId ast = R_AST_NODE_ID_INVALID;

    options.allocate = r_semantic_test_allocate;
    options.free = r_semantic_test_free;
    options.allocator_user_data = allocator;
    context = r_frontend_create(&options);
    if (context == NULL) {
        return NULL;
    }
    if ((r_frontend_add_source(context,
                               "semantic-body-oom.r",
                               (const uint8_t *)source_text,
                               strlen(source_text),
                               &source_id) != R_FRONTEND_OK) ||
        (r_frontend_lower_ast(context, source_id, &ast) != R_FRONTEND_OK)) {
        r_frontend_destroy(context);
        return NULL;
    }
    return context;
}

static void r_semantic_test_body_oom_sweep(void) {
    RSemanticTestAllocator baseline_allocator = {0};
    RFrontendContext *baseline = r_semantic_prepare_body_oom_context(&baseline_allocator);
    size_t before;
    size_t semantic_allocations;
    size_t offset;

    R_SEMANTIC_CHECK(baseline != NULL);
    if (baseline == NULL) {
        return;
    }
    before = baseline_allocator.call_count;
    R_SEMANTIC_CHECK(r_frontend_analyze(baseline) == R_FRONTEND_OK);
    semantic_allocations = baseline_allocator.call_count - before;
    r_frontend_destroy(baseline);
    R_SEMANTIC_CHECK(baseline_allocator.live_count == 0U);
    R_SEMANTIC_CHECK(semantic_allocations != 0U);

    for (offset = 1U; offset <= semantic_allocations; ++offset) {
        RSemanticTestAllocator allocator = {0};
        RFrontendContext *context = r_semantic_prepare_body_oom_context(&allocator);
        RFrontendStatus status;
        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        allocator.fail_at = allocator.call_count + offset;
        status = r_frontend_analyze(context);
        R_SEMANTIC_CHECK((status == R_FRONTEND_OUT_OF_MEMORY) ||
                         (status == R_FRONTEND_LIMIT_EXCEEDED));
        R_SEMANTIC_CHECK(r_frontend_hir_root(context) == R_HIR_NODE_ID_INVALID);
        r_frontend_destroy(context);
        R_SEMANTIC_CHECK(allocator.live_count == 0U);
    }
}

static RFrontendContext *r_semantic_prepare_static_oom_context(RSemanticTestAllocator *allocator) {
    static const char source_text[] =
        "module semantic.static_oom;\n"
        "@if (core::profile is hosted-native-async) { i32 bias() { return 1; } }\n"
        "@else { i32 bias() { return 0; } }\n"
        "trait Measured { i32 measure(const Self* this); };\n"
        "impl Measured for i32 { i32 measure(const i32* this) { return *this; } };\n"
        "@generic<T: copy> struct Box { T value; };\n"
        "@generic<T> T transfer(T value) {\n"
        "  @if ((T is copy && T is send) || T is pod) { Box<T> box = {.value = value}; return "
        "box.value; }\n"
        "  @else { return move value; } }\n"
        "@generic<T> i32 measure(const T* x) {\n"
        "  @if (T is Measured) { i32 y = x->measure(); return y; } @else { return 0; } }\n"
        "i32 main() { i32 x = transfer(41); i32 y = measure(&x); i32 b = bias(); return y + b - "
        "42; }\n";
    RFrontendOptions options = r_frontend_default_options();
    RFrontendContext *context;
    RSourceId source_id = R_SOURCE_ID_INVALID;
    RAstNodeId ast = R_AST_NODE_ID_INVALID;

    options.allocate = r_semantic_test_allocate;
    options.free = r_semantic_test_free;
    options.allocator_user_data = allocator;
    context = r_frontend_create(&options);
    if (context == NULL) {
        return NULL;
    }
    if ((r_frontend_add_source(context,
                               "semantic-static-oom.r",
                               (const uint8_t *)source_text,
                               strlen(source_text),
                               &source_id) != R_FRONTEND_OK) ||
        (r_frontend_lower_ast(context, source_id, &ast) != R_FRONTEND_OK)) {
        r_frontend_destroy(context);
        return NULL;
    }
    return context;
}

static void r_semantic_test_static_oom_sweep(void) {
    RSemanticTestAllocator baseline_allocator = {0};
    RFrontendContext *baseline = r_semantic_prepare_static_oom_context(&baseline_allocator);
    size_t before;
    size_t semantic_allocations;
    size_t offset;

    R_SEMANTIC_CHECK(baseline != NULL);
    if (baseline == NULL) {
        return;
    }
    before = baseline_allocator.call_count;
    R_SEMANTIC_CHECK(r_frontend_analyze(baseline) == R_FRONTEND_OK);
    semantic_allocations = baseline_allocator.call_count - before;
    r_frontend_destroy(baseline);
    R_SEMANTIC_CHECK(baseline_allocator.live_count == 0U);
    R_SEMANTIC_CHECK(semantic_allocations != 0U);

    for (offset = 1U; offset <= semantic_allocations; ++offset) {
        RSemanticTestAllocator allocator = {0};
        RFrontendContext *context = r_semantic_prepare_static_oom_context(&allocator);
        RFrontendStatus status;
        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        allocator.fail_at = allocator.call_count + offset;
        status = r_frontend_analyze(context);
        R_SEMANTIC_CHECK((status == R_FRONTEND_OUT_OF_MEMORY) ||
                         (status == R_FRONTEND_LIMIT_EXCEEDED));
        R_SEMANTIC_CHECK(r_frontend_hir_root(context) == R_HIR_NODE_ID_INVALID);
        r_frontend_destroy(context);
        R_SEMANTIC_CHECK(allocator.live_count == 0U);
    }
}

static RFrontendContext *r_semantic_make_import_program(bool qualified, bool reverse) {
    static const char provider[] =
        "module semantic.import_api;\n"
        "struct pair { i32 left; i32 right; };\n"
        "const i32 OFFSET = 4;\n"
        "const usize WIDTH = 2;\n"
        "i32 add(pair value, i32 offset) { return value.left + value.right + offset; }\n"
        "i32 select(const i32[] values, usize index) {\n"
        "    usize size = len(values);\n"
        "    if (index >= size) { return 0; }\n"
        "    return values[index];\n"
        "}\n";
    static const char selected_consumer[] =
        "module semantic.import_main;\n"
        "import semantic.import_api::{pair, OFFSET, WIDTH, add, select};\n"
        "i32 main() {\n"
        "    pair value = { .left = 1, .right = 2, };\n"
        "    i32[WIDTH] values = {3, 5};\n"
        "    const i32[] view = &values;\n"
        "    i32 sum = add(value, OFFSET);\n"
        "    i32 chosen = select(view, 1);\n"
        "    return sum + chosen;\n"
        "}\n";
    static const char qualified_consumer[] =
        "module semantic.import_main;\n"
        "import semantic.import_api;\n"
        "i32 main() {\n"
        "    semantic.import_api::pair value = { .left = 1, .right = 2, };\n"
        "    i32[2] values = {3, 5};\n"
        "    const i32[] view = &values;\n"
        "    i32 sum = semantic.import_api::add(value, semantic.import_api::OFFSET);\n"
        "    i32 chosen = semantic.import_api::select(view, 1);\n"
        "    return sum + chosen;\n"
        "}\n";
    const char *consumer = qualified ? qualified_consumer : selected_consumer;
    RFrontendContext *context = r_frontend_create(NULL);
    RSourceId source_id = R_SOURCE_ID_INVALID;

    if (context == NULL) {
        return NULL;
    }
    if (reverse) {
        if ((r_frontend_add_source(context,
                                   "import-main.r",
                                   (const uint8_t *)consumer,
                                   strlen(consumer),
                                   &source_id) != R_FRONTEND_OK) ||
            (r_frontend_add_source(context,
                                   "import-api.r",
                                   (const uint8_t *)provider,
                                   strlen(provider),
                                   &source_id) != R_FRONTEND_OK)) {
            r_frontend_destroy(context);
            return NULL;
        }
    } else if ((r_frontend_add_source(context,
                                      "import-api.r",
                                      (const uint8_t *)provider,
                                      strlen(provider),
                                      &source_id) != R_FRONTEND_OK) ||
               (r_frontend_add_source(context,
                                      "import-main.r",
                                      (const uint8_t *)consumer,
                                      strlen(consumer),
                                      &source_id) != R_FRONTEND_OK)) {
        r_frontend_destroy(context);
        return NULL;
    }
    if ((r_frontend_analyze(context) != R_FRONTEND_OK) ||
        (r_frontend_lower_mir(context) != R_FRONTEND_OK)) {
        r_frontend_destroy(context);
        return NULL;
    }
    return context;
}

static void r_semantic_test_cross_module_bindings(void) {
    RFrontendContext *selected = r_semantic_make_import_program(false, true);
    RFrontendContext *qualified = r_semantic_make_import_program(true, false);
    RSemanticTestBuffer selected_c17 = {0};
    RSemanticTestBuffer qualified_c17 = {0};

    R_SEMANTIC_CHECK(selected != NULL);
    R_SEMANTIC_CHECK(qualified != NULL);
    if ((selected != NULL) && (qualified != NULL)) {
        R_SEMANTIC_CHECK(r_frontend_diagnostic_count(selected) == 0U);
        R_SEMANTIC_CHECK(r_frontend_diagnostic_count(qualified) == 0U);
        R_SEMANTIC_CHECK(r_frontend_emit_c17(selected, r_semantic_test_write, &selected_c17) ==
                         R_FRONTEND_OK);
        R_SEMANTIC_CHECK(r_frontend_emit_c17(qualified, r_semantic_test_write, &qualified_c17) ==
                         R_FRONTEND_OK);
        R_SEMANTIC_CHECK((selected_c17.bytes != NULL) && (selected_c17.length != 0U));
        R_SEMANTIC_CHECK((qualified_c17.bytes != NULL) && (qualified_c17.length != 0U));
        /* The imported select() reads values[index] after `index >= size` returned, so the
           index is proven and emitted without its bounds check (index_proofs.inc). */
        R_SEMANTIC_CHECK((selected_c17.bytes != NULL) &&
                         (strstr(selected_c17.bytes, ".r_data[(size_t)") != NULL) &&
                         (strstr(selected_c17.bytes, "R_RUNTIME_PANIC_BOUNDS") == NULL));
        R_SEMANTIC_CHECK((qualified_c17.bytes != NULL) &&
                         (strstr(qualified_c17.bytes, ".r_data[(size_t)") != NULL) &&
                         (strstr(qualified_c17.bytes, "R_RUNTIME_PANIC_BOUNDS") == NULL));
    }
    free(selected_c17.bytes);
    free(qualified_c17.bytes);
    r_frontend_destroy(selected);
    r_frontend_destroy(qualified);
}

static void r_semantic_test_cross_module_binding_diagnostics(void) {
    static const char protected_provider[] =
        "module semantic.protected_api; protected i32 hidden() { return 1; }";
    static const char protected_consumer[] =
        "module semantic.protected_user; import semantic.protected_api::{hidden}; "
        "i32 main() { i32 value = hidden(); return value; }";
    static const char first_provider[] = "module semantic.first_api; i32 duplicate() { return 1; }";
    static const char second_provider[] =
        "module semantic.second_api; i32 duplicate() { return 2; }";
    static const char ambiguous_consumer[] =
        "module semantic.ambiguous_user; import semantic.first_api::{duplicate}; "
        "import semantic.second_api::{duplicate}; "
        "i32 main() { i32 value = duplicate(); return value; }";
    static const char qualified_provider[] =
        "module semantic.qualified_api; i32 value() { return 1; }";
    static const char missing_import_consumer[] =
        "module semantic.qualified_user; "
        "i32 main() { i32 result = semantic.qualified_api::value(); return result; }";
    static const char whole_unqualified_consumer[] =
        "module semantic.whole_unqualified_user; import semantic.qualified_api; "
        "i32 main() { i32 result = value(); return result; }";
    static const char missing_selected_consumer[] =
        "module semantic.missing_selected_user; import semantic.qualified_api::{absent}; "
        "i32 main() { return 0; }";
    static const char protected_type_provider[] =
        "module semantic.protected_type_api; "
        "protected struct hidden { i32 value; }; "
        "i32 inspect(hidden value) { return value.value; }";
    static const char protected_type_consumer[] =
        "module semantic.protected_type_user; "
        "import semantic.protected_type_api::{inspect}; i32 main() { return 0; }";
    static const char shadow_provider[] = "module semantic.shadow_api; i32 clash() { return 1; }";
    static const char shadow_consumer[] =
        "module semantic.shadow_user; import semantic.shadow_api::{clash}; "
        "i32 clash() { return 2; }";
    const char *sources[][3] = {{protected_provider, protected_consumer, NULL},
                                {first_provider, second_provider, ambiguous_consumer},
                                {qualified_provider, missing_import_consumer, NULL},
                                {qualified_provider, whole_unqualified_consumer, NULL},
                                {qualified_provider, missing_selected_consumer, NULL},
                                {protected_type_provider, protected_type_consumer, NULL},
                                {shadow_provider, shadow_consumer, NULL}};
    const char *expected_codes[] = {"R-DIAG-NAME-001",
                                    "R-DIAG-NAME-001",
                                    "R-DIAG-NAME-001",
                                    "R-DIAG-NAME-001",
                                    "R-DIAG-NAME-001",
                                    "R-DIAG-MOD-001",
                                    "R-DIAG-NAME-003"};
    const size_t source_counts[] = {2U, 3U, 2U, 2U, 2U, 2U, 2U};
    size_t case_index;

    for (case_index = 0U; case_index < (sizeof(sources) / sizeof(sources[0])); ++case_index) {
        RFrontendContext *context = r_frontend_create(NULL);
        size_t source_index;
        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        for (source_index = 0U; source_index < source_counts[case_index]; ++source_index) {
            RSourceId source_id = R_SOURCE_ID_INVALID;
            R_SEMANTIC_CHECK(
                r_frontend_add_source(context,
                                      "cross-module-negative.r",
                                      (const uint8_t *)sources[case_index][source_index],
                                      strlen(sources[case_index][source_index]),
                                      &source_id) == R_FRONTEND_OK);
        }
        R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
        if (r_semantic_diagnostic_code_count(context, expected_codes[case_index]) == 0U) {
            size_t diagnostic_index;
            (void)fprintf(stderr, "cross-module case %zu diagnostics:\n", case_index);
            for (diagnostic_index = 0U; diagnostic_index < r_frontend_diagnostic_count(context);
                 ++diagnostic_index) {
                const RDiagnostic *diagnostic = r_frontend_diagnostic(context, diagnostic_index);
                (void)fprintf(stderr,
                              "  %s: %s\n",
                              diagnostic != NULL ? diagnostic->code : "<null>",
                              diagnostic != NULL ? diagnostic->message : "<null>");
            }
        }
        R_SEMANTIC_CHECK(r_semantic_diagnostic_code_count(context, expected_codes[case_index]) !=
                         0U);
        R_SEMANTIC_CHECK(r_semantic_diagnostic_code_count(context, "R-DIAG-SLICE-001") == 0U);
        r_frontend_destroy(context);
    }
}

static void r_semantic_test_borrow_signature_boundary(void) {
    static const char source[] = "module semantic.borrow_boundary;\n"
                                 "struct item { i32 value; };\n"
                                 "i32 shared(const item* value) { return value->value; }\n"
                                 "i32 exclusive(item* value) { return value->value; }\n"
                                 "i32 nullable(const item*? value) { return 0; }\n"
                                 "const item* borrowed(const item* value) { return value; }\n"
                                 "const item* wrapped(const item* value) {\n"
                                 "    const item* result = borrowed(value);\n"
                                 "    return result;\n"
                                 "}\n";
    RFrontendContext *context = r_frontend_create(NULL);
    RSourceId source_id = R_SOURCE_ID_INVALID;
    bool saw_shared = false;
    bool saw_exclusive = false;
    bool saw_supported_shared = false;
    bool saw_supported_exclusive = false;
    bool saw_nullable = false;
    bool saw_supported_nullable = false;
    size_t supported_borrow_returns = 0U;
    size_t symbol_index;

    R_SEMANTIC_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_SEMANTIC_CHECK(
        r_frontend_add_source(
            context, "borrow-boundary.r", (const uint8_t *)source, strlen(source), &source_id) ==
        R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    for (symbol_index = 0U; symbol_index < context->semantic_symbol_count; ++symbol_index) {
        const RSemanticSymbol *symbol = &context->semantic_symbols[symbol_index];
        const RSemanticType *parameter;
        const RSemanticType *return_type;
        if ((symbol->kind != R_SEMANTIC_SYMBOL_FUNCTION) || (symbol->parameter_count != 1U)) {
            continue;
        }
        return_type = r_semantic_type(context, symbol->return_type);
        if ((return_type != NULL) && (return_type->kind == R_SEMANTIC_TYPE_BORROW) &&
            symbol->signature_supported && (symbol->return_borrow_parameter == 1U)) {
            supported_borrow_returns += 1U;
        }
        parameter = r_semantic_type(
            context, context->semantic_parameter_types[(size_t)symbol->first_parameter_type]);
        R_SEMANTIC_CHECK((parameter != NULL) && (parameter->kind == R_SEMANTIC_TYPE_BORROW));
        if (parameter == NULL) {
            continue;
        }
        saw_shared = saw_shared || (((parameter->flags & R_SEMANTIC_TYPE_FLAG_SHARED) != 0U) &&
                                    ((parameter->flags & R_SEMANTIC_TYPE_FLAG_NULLABLE) == 0U));
        saw_supported_shared =
            saw_supported_shared || (symbol->signature_supported &&
                                     ((parameter->flags & R_SEMANTIC_TYPE_FLAG_SHARED) != 0U) &&
                                     ((parameter->flags & R_SEMANTIC_TYPE_FLAG_NULLABLE) == 0U));
        saw_exclusive = saw_exclusive ||
                        ((parameter->flags &
                          (R_SEMANTIC_TYPE_FLAG_SHARED | R_SEMANTIC_TYPE_FLAG_NULLABLE)) == 0U);
        saw_supported_exclusive =
            saw_supported_exclusive ||
            (symbol->signature_supported &&
             ((parameter->flags & (R_SEMANTIC_TYPE_FLAG_SHARED | R_SEMANTIC_TYPE_FLAG_NULLABLE)) ==
              0U));
        saw_nullable =
            saw_nullable ||
            ((parameter->flags & (R_SEMANTIC_TYPE_FLAG_SHARED | R_SEMANTIC_TYPE_FLAG_NULLABLE)) ==
             (R_SEMANTIC_TYPE_FLAG_SHARED | R_SEMANTIC_TYPE_FLAG_NULLABLE));
        saw_supported_nullable =
            saw_supported_nullable ||
            (symbol->signature_supported &&
             ((parameter->flags & (R_SEMANTIC_TYPE_FLAG_SHARED | R_SEMANTIC_TYPE_FLAG_NULLABLE)) ==
              (R_SEMANTIC_TYPE_FLAG_SHARED | R_SEMANTIC_TYPE_FLAG_NULLABLE)));
    }
    R_SEMANTIC_CHECK(saw_shared);
    R_SEMANTIC_CHECK(saw_exclusive);
    R_SEMANTIC_CHECK(saw_supported_shared);
    R_SEMANTIC_CHECK(saw_supported_exclusive);
    R_SEMANTIC_CHECK(saw_nullable);
    R_SEMANTIC_CHECK(saw_supported_nullable);
    R_SEMANTIC_CHECK(supported_borrow_returns == 2U);
    r_frontend_destroy(context);
}

static void r_semantic_test_return_borrow_diagnostics(void) {
    static const char local_escape[] = "module semantic.local_escape;\n"
                                       "const i32* invalid() {\n"
                                       "    i32 local = 1;\n"
                                       "    return &local;\n"
                                       "}\n";
    static const char value_parameter_escape[] = "module semantic.parameter_escape;\n"
                                                 "const i32* invalid(i32 value) {\n"
                                                 "    return &value;\n"
                                                 "}\n";
    static const char source_only[] = "module semantic.borrow_declaration;\n"
                                      "const i32* unresolved(const i32* value);\n";
    const char *sources[] = {local_escape, value_parameter_escape, source_only};
    size_t case_index;

    for (case_index = 0U; case_index < (sizeof(sources) / sizeof(sources[0])); ++case_index) {
        RFrontendContext *context = r_frontend_create(NULL);
        RSourceId source_id = R_SOURCE_ID_INVALID;

        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        R_SEMANTIC_CHECK(r_frontend_add_source(context,
                                               "return-borrow-negative.r",
                                               (const uint8_t *)sources[case_index],
                                               strlen(sources[case_index]),
                                               &source_id) == R_FRONTEND_OK);
        R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
        R_SEMANTIC_CHECK(r_semantic_diagnostic_code_count(context, "R-DIAG-BORROW-002") == 1U);
        R_SEMANTIC_CHECK(r_semantic_diagnostic_code_count(context, "R-DIAG-SLICE-001") == 0U);
        r_frontend_destroy(context);
    }
}

static void r_semantic_test_static_conditions(void) {
    static const struct {
        const char *name;
        const char *source;
        const char *diagnostic;
    } cases[] = {
        {"closed", "i32 f() { @if (i32 is copy) { return 1; } @else { return missing; } }", NULL},
        {"closed-false",
         "i32 f() { @if (i32 is std.string::string) { return missing; } return 1; }",
         NULL},
        {"target",
         "i32 f() { @if (core::target is \"impossible-target\") { return missing; } return 1; }",
         NULL},
        {"module-type", "@if (i32 is copy) { i32 f() { return 1; } }", "R-DIAG-META-001"},
        {"profile-typo",
         "@if (core::profile is imaginary) { i32 f() { return 1; } }",
         "R-DIAG-META-001"},
        {"identity",
         "@generic<T> T f(T x) { @if (T is i32) { return x + 1; } @else { return move x; } }",
         NULL},
        {"copy",
         "@generic<T> T f(T x) { @if (T is copy) { T y = x; return y; } @else { return move x; } }",
         NULL},
        {"pod",
         "@generic<T> T f(T x) { @if (T is pod) { return x; } @else { return move x; } }",
         NULL},
        {"and",
         "@generic<T> T f(T x) { @if (T is copy && T is send) { return x; } @else { return move x; "
         "} }",
         NULL},
        {"or-pod-copy",
         "@generic<T> T f(T x) { @if (T is copy || T is pod) { return x; } @else { return move x; "
         "} }",
         NULL},
        {"or-common",
         "@generic<T> T f(T x) { @if ((T is copy && T is send) || (T is copy && T is sync)) { "
         "return x; } @else { return move x; } }",
         NULL},
        {"negative",
         "@generic<T> T f(T x) { @if (!(T is copy)) { @if (T is copy) { return missing; } return "
         "move x; } @else { return x; } }",
         NULL},
        {"nested-pod",
         "@generic<T> T f(T x) { @if (T is copy) { return x; } @else { @if (T is pod) { return "
         "missing; } return move x; } }",
         NULL},
        {"or-not-copy",
         "@generic<T> T f(T x) { @if (T is copy || T is send) { return x; } @else { return move x; "
         "} }",
         "R-DIAG-MOVE-001"},
        {"scope",
         "@generic<T> T f(T x) { @if (T is copy) { T y = x; } return x; }",
         "R-DIAG-MOVE-001"},
        {"unused-body",
         "@generic<T> T f(T x) { @if (T is copy) { return x + x; } @else { return move x; } }",
         "R-DIAG-TYPE-001"},
        {"contradiction",
         "@generic<T> T f(T x) { @if (T is copy && !(T is copy)) { return x; } @else { return move "
         "x; } }",
         "R-DIAG-META-001"},
        {"identity-conflict",
         "@generic<T> T f(T x) { @if (T is i32 && T is u32) { return x; } @else { return move x; } "
         "}",
         "R-DIAG-META-001"},
        {"trait",
         "trait A { i32 get(const Self* this); }; @generic<T> i32 f(const T* x) { @if (T is A) { "
         "i32 y = x->get(); return y; } @else { return 0; } }",
         NULL},
        {"trait-scope",
         "trait A { i32 get(const Self* this); }; @generic<T> i32 f(const T* x) { @if (T is A) { "
         "i32 y = x->get(); } i32 y = x->get(); return y; }",
         "R-DIAG-NAME-001"},
        {"callable",
         "@generic<F> i32 f(const F* x) { @if (F is fn(i32) -> i32) { i32 y = x(1); return y; } "
         "@else { return 0; } }",
         NULL},
        {"generic-aggregate",
         "@generic<T> struct Box { T value; }; @generic<T> Box<T> f(Box<T> x) { @if (T is copy) { "
         "Box<T> y = x; return y; } @else { return move x; } }",
         NULL},
        {"switch-case",
         "i32 f(i32 x) { switch (x) { case 1: @if (i32 is copy) { x = 2; } @else { x = missing; } "
         "break; default: break; } return x; }",
         NULL},
        /* L20.1 (R-STMT-0007): a clause without a terminator ends with an implicit break. */
        {"case-implicit-break",
         "void f(i32 x) { switch (x) { case 1: @if (i32 is copy) { return; } default: return; } }",
         NULL},
        {"unknown-predicate", "void f() { @if (Unknown is copy) { } }", "R-DIAG-META-001"},
        {"inactive-effects",
         "@noalloc @nonblocking void f() { @if (i32 is std.string::string) { own i32* x = new "
         "i32(1); } }",
         NULL},
        {"active-effects",
         "@noalloc void f() { @if (i32 is copy) { own i32* x = new i32(1); } }",
         "R-DIAG-RESOURCE-001"},
        {"inactive-error",
         "error E {}; void f() { @if (i32 is std.string::string) { throw E {}; } }",
         NULL},
        {"active-error",
         "error E {}; void f() { @if (i32 is copy) { throw E {}; } }",
         "R-DIAG-EFFECT-001"},
        {"inactive-provenance",
         "const i32* f(const i32* x) { @if (i32 is copy) { return x; } @else { i32 local = 1; "
         "return &local; } }",
         NULL},
        {"ownership-join",
         "@generic<T> T f(T x) { @if (T is copy) { T y = x; } @else { T y = move x; } return move "
         "x; }",
         "R-DIAG-MOVE-002"},
        /* R-META-0002, R-META-0003: constant conditions. */
        {"value-body",
         "const usize C = 4usize; i32 f() { @if (C <= 8usize) { return 1; } @else { return "
         "missing; } }",
         NULL},
        {"value-call",
         "bool even(usize v) { return v % 2usize == 0usize; } i32 f() { @if (even(4usize) == true "
         "&& 2usize in 1usize..4usize) { return 1; } @else { return missing; } }",
         NULL},
        {"value-generic",
         "@generic<const usize N> usize f() { @if (N <= 4usize) { return 1usize; } @else { "
         "return 2usize; } } usize g() { return f::<2usize>() + f::<9usize>(); }",
         NULL},
        {"value-generic-layout",
         "@generic<T: copy> usize f(T x) { x as void; @if (sizeof(T) <= 4usize && T is copy) { "
         "return 4usize; } @else { return 8usize; } } usize g() { return f(1u8) + f(1u64); }",
         NULL},
        {"value-module",
         "const usize C = 4usize; @if (C == 4usize) { i32 f() { return 1; } } @else { i32 f() { "
         "return missing; } }",
         NULL},
        {"value-module-chain",
         "usize k() { return 3usize; } @if (k() == 1usize) { const i32 A = missing; } @else @if "
         "(k() == 3usize) { const i32 A = 3; } i32 g() { return A; }",
         NULL},
        {"value-module-nested",
         "const usize C = 4usize; @if (C == 4usize) { const usize D = 5usize; } @if (D == 5usize "
         "&& core::profile is hosted) { i32 f() { return 1; } } @else { i32 f() { return 2; } }",
         NULL},
        {"value-runtime",
         "i32 f(usize n) { @if (n <= 4usize) { return 1; } return 0; }",
         "R-DIAG-CONST-002"},
        {"value-panic",
         "usize bad() { panic(\"bad\"); } i32 f() { @if (bad() == 1usize) { return 1; } return 0; "
         "}",
         "R-DIAG-CONST-003"},
        {"value-type",
         "const usize C = 4usize; i32 f() { @if (C == true) { return 1; } return 0; }",
         "R-DIAG-TYPE-001"},
        {"value-leaf",
         "const usize C = 1usize; i32 f() { @if (C) { return 1; } return 0; }",
         "R-DIAG-SYN-001"},
        {"value-generic-runtime",
         "@generic<const usize N> usize f(usize n) { @if (N <= n) { return 1usize; } @else { "
         "return 2usize; } }",
         "R-DIAG-CONST-002"},
        {"value-generic-panic",
         "usize check(usize v) { if (v == 0usize) { panic(\"zero\"); } return v; } @generic<const "
         "usize N> usize f() { @if (check(N) == 1usize) { return 1usize; } @else { return "
         "2usize; } } usize g() { return f::<0usize>(); }",
         "R-DIAG-CONST-003"},
        {"value-module-runtime",
         "thread_local usize ticks = 0usize; usize now() { return ticks; } @if (now() == 0usize) "
         "{ i32 f() { return 1; } }",
         "R-DIAG-CONST-002"},
        {"value-module-panic",
         "usize bad() { panic(\"bad\"); } @if (bad() == 1usize) { i32 f() { return 1; } }",
         "R-DIAG-CONST-003"},
        {"value-module-own-branch",
         "@if (helper() == 1) { i32 helper() { return 1; } }",
         "R-DIAG-NAME-001"},
        {"value-generic-call",
         "@generic<T> usize size_of() { return sizeof(T); } @generic<T: copy> usize f(T x) { x as "
         "void; const usize BYTES = size_of::<T>(); @if (BYTES <= 2usize && size_of::<T>() > "
         "0usize) { return 1usize; } @else { return 2usize; } } usize g() { return f(1u8) + "
         "f(1u64); }",
         NULL},
        {"value-read-bound",
         "usize three() { return 3usize; } i32 f() { const usize D = three(); u8[D] bytes = {}; "
         "return len(bytes) as i32; }",
         NULL},
        {"value-generic-call-runtime",
         "thread_local usize K = 2usize; @generic<T: copy> usize kind() { @if (T is u8) { return "
         "1usize; } @else { return K; } } @generic<T: copy> usize f(T x) { x as void; @if "
         "(kind::<T>() == 1usize) { return 1usize; } @else { return 2usize; } } usize g() { "
         "return f(1u8) + f(1u32); }",
         "R-DIAG-CONST-002"},
        {"value-generic-call-panic",
         "@generic<const usize N> usize checked() { if (N == 0usize) { panic(\"zero\"); } return "
         "N; } @generic<const usize N> usize f() { @if (checked::<N>() > 1usize) { return 1usize; "
         "} @else { return 2usize; } } usize g() { return f::<4usize>() + f::<0usize>(); }",
         "R-DIAG-CONST-003"},
        {"value-generic-call-argument",
         "@generic<T> usize size_of() { return sizeof(T); } @generic<T: copy> usize f(T x, usize "
         "n) { x as void; @if (size_of::<T>() <= n) { return 1usize; } @else { return 2usize; } "
         "}",
         "R-DIAG-CONST-002"},
    };
    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        RFrontendContext *context = r_frontend_create(NULL);
        char source[2048];
        RFrontendStatus status;
        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        (void)snprintf(
            source, sizeof(source), "module test.static_condition;\n%s\n", cases[index].source);
        (void)r_semantic_add_bytes(
            context, cases[index].name, (const uint8_t *)source, strlen(source));
        status = r_frontend_analyze(context);
        if (status !=
                (cases[index].diagnostic == NULL ? R_FRONTEND_OK : R_FRONTEND_INVALID_SOURCE) ||
            (cases[index].diagnostic != NULL &&
             r_semantic_diagnostic_code_count(context, cases[index].diagnostic) == 0U)) {
            (void)fprintf(stderr, "static condition case failed: %s\n", cases[index].name);
            RSemanticTestBuffer diagnostics = {0};
            (void)r_frontend_dump_diagnostics_json(context, r_semantic_test_write, &diagnostics);
            (void)fprintf(stderr, "%s\n", diagnostics.bytes == NULL ? "" : diagnostics.bytes);
            free(diagnostics.bytes);
            R_SEMANTIC_CHECK(false);
        }
        r_frontend_destroy(context);
    }
}

static void r_semantic_test_closure_modes(void) {
    static const struct {
        const char *name;
        const char *source;
        const char *diagnostic;
    } cases[] = {
        {"once-owner",
         "i32 main() { own i32* p = new i32(42); fn once i32 f() move(p) { return *p; } i32 value "
         "= (move f).call(); return value - 42; }",
         NULL},
        {"once-no-move",
         "void f() { own i32* p = new i32(42); fn once i32 read() move(p) { return *p; } i32 value "
         "= read(); }",
         "R-DIAG-MOVE-001"},
        {"once-reuse",
         "void f() { own i32* p = new i32(42); fn once i32 read() move(p) { return *p; } i32 value "
         "= (move read).call(); i32 again = (move read).call(); }",
         "R-DIAG-MOVE-002"},
        {"once-copy",
         "i32 main() { i32 p = 21; fn once i32 f() move(p) { return p; } i32 a = (move f).call(); "
         "i32 b = (move f).call(); return a + b - 42; }",
         NULL},
        {"mut-copy",
         "i32 main() { i32 p = 1; fn mut i32 f() move(p) { p += 1; return p; } i32 a = f(); i32 b "
         "= f(); return a + b + p - 6; }",
         NULL},
        {"shared-write",
         "void f() { i32 p = 1; fn shared void write() move(p) { p += 1; } }",
         "R-DIAG-BORROW-001"},
        {"shared-mut-constraint",
         "@generic<F: fn() -> i32> i32 apply(const F* work) { i32 value = work(); return value; } "
         "void f() { i32 p = 1; fn mut i32 work() move(p) { p += 1; return p; } i32 result = "
         "apply(&work); }",
         "R-DIAG-TYPE-001"},
        {"const-mut",
         "@generic<F: fn mut() -> i32> i32 f(const F* work) { i32 result = work(); return result; "
         "}",
         "R-DIAG-BORROW-001"},
        {"borrow-value",
         "i32 main() { i32 x = 42; const i32* p = &x; fn shared i32 work() move(p) { return *p; } "
         "i32 result = work(); return result - 42; }",
         NULL},
        {"borrow-value-conflict",
         "void f() { i32 x = 42; const i32* p = &x; fn shared i32 work() move(p) { return *p; } x "
         "= 0; i32 result = work(); }",
         "R-DIAG-BORROW-001"},
        {"borrowed-async",
         "async void pause() { return; } async i32 main() { i32 x = 42; fn i32 work() { return x; "
         "} try { await pause(); i32 value = work(); return value - 42; } catch "
         "(std.async::start_error failure) { return 1; } }",
         "R-DIAG-ASYNC-001"},
        {"once-extract",
         "i32 main() { own i32* p = new i32(42); o<own i32*> pending = o::some(move p); fn once "
         "i32 work() move(pending) { o<own i32*> delivery = core::take(&pending); switch(move "
         "delivery) { case variant o::some(move value): return *value; case variant o::none: "
         "return 0; } } i32 result = (move work).call(); return result - 42; }",
         NULL},
        {"field-move-rejected",
         "void f() { own i32* p = new i32(42); fn once own i32* work() move(p) { return move p; } "
         "}",
         "R-DIAG-MOVE-003"},
        {"mode-type-name",
         "struct mut { i32 value; }; i32 main() { fn mut work() { return mut { .value = 42 }; } "
         "mut result = work(); return result.value - 42; }",
         NULL},
        {"mode-generic-return",
         "@generic<T> struct Box { T value; }; void f() { fn once Box<i32> make() { return "
         "Box<i32> {.value=1}; } Box<i32> value = make(); value as void; }",
         NULL},
        {"mode-generic-type-name",
         "@generic<T> struct mut { T value; }; void f() { fn mut<i32> make() { return mut<i32> "
         "{.value=1}; } mut<i32> value = make(); value as void; }",
         NULL},
        {"shared-read-method",
         "struct S { i32 value; }; i32 S::get(const S* this) { return this->value; } void f() { S "
         "s = {.value=1}; fn shared i32 read() { i32 value = s.get(); return value; } i32 value = "
         "read(); value as void; }",
         NULL},
        {"static-mode",
         "@generic<F> i32 apply(F work) { @if (F is fn once() -> i32) { i32 value = (move "
         "work).call(); return value; } @else { return 0; } } void f() { fn once i32 value() { "
         "return 1; } i32 x = apply(value); x as void; }",
         NULL},
        {"thread-missing-send",
         "@generic<F: fn once() -> i32> i32 run(F work) { i32 x = (move work).call(); return x; } "
         "void f() throws std.thread::thread_error { rc i32 p = new rc i32(1); fn once i32 work() "
         "move(p) { return *p; } std.thread::join_handle<i32> child = std.thread::spawn(run, move "
         "work); }",
         "R-DIAG-ASYNC-001"},
        {"checked-closure",
         "error E {}; void run() throws E { fn void fail() throws E { throw E {}; } fail(); }",
         NULL},
        {"checked-unhandled",
         "error E {}; void run() { fn void fail() throws E { throw E {}; } fail(); }",
         "R-DIAG-EFFECT-001"},
        {"checked-body-unhandled",
         "error E {}; void run() { fn void fail() { throw E {}; } }",
         "R-DIAG-EFFECT-001"},
        {"checked-ordinary-type",
         "struct E {}; void run() { fn void fail() throws E {} }",
         "R-DIAG-EFFECT-002"},
        {"checked-constraint-mismatch",
         "error E {}; @generic<F: fn() -> void> void apply(const F* work) { work(); } void run() "
         "throws E { fn void fail() throws E { throw E {}; } apply(&fail); }",
         "R-DIAG-TYPE-001"},
        {"async-closure",
         "async i32 main() { async fn i32 work(i32 x) { return x; } try { i32 value = await "
         "work(42); return value - 42; } catch (std.async::start_error failure) { return 1; } }",
         NULL},
        {"async-shared", "void run() { async fn shared void work() {} }", "R-DIAG-ASYNC-001"},
        {"async-borrow-capture",
         "void run() { i32 x = 1; async fn i32 work() { return x; } }",
         "R-DIAG-ASYNC-001"},
        {"async-moved-borrow",
         "void run() { i32 x = 1; const i32* p = &x; async fn i32 work() move(p) { return *p; } }",
         "R-DIAG-ASYNC-001"},
        {"async-nonsend-capture",
         "void run() { rc i32 p = new rc i32(1); async fn i32 work() move(p) { return *p; } }",
         "R-DIAG-ASYNC-001"},
        {"async-borrow-param",
         "void run() { async fn i32 work(const i32* p) { return *p; } }",
         "R-DIAG-ASYNC-001"},
        {"async-start-unhandled",
         "void run() { async fn void work() {} task<void> pending = work(); }",
         "R-DIAG-EFFECT-001"},
        {"async-constraint-sync-mismatch",
         "@generic<F: async fn() -> void & send & unborrowed> task<void> apply(F work) throws "
         "std.async::start_error { task<void> pending = (move work).call(); return move pending; } "
         "void run() throws std.async::start_error { fn once void work() {} task<void> pending = "
         "apply(work); }",
         "R-DIAG-TYPE-001"},
        {"generic-lambda-unused-invalid",
         "@generic<T: copy> void run(T value) { fn T bad(T x) { return x + x; } }",
         "R-DIAG-TYPE-001"},
        {"generic-lambda-reuse-move",
         "@generic<T> T run(T value) { fn T bad(T x) { T first = move x; return move x; } T result "
         "= bad(move value); return move result; }",
         "R-DIAG-MOVE-002"},
        {"generic-lambda-copy",
         "@generic<T: copy & unborrowed> T run(T value) { fn T read() { return value; } T result = "
         "read(); return result; } i32 main() { i32 result = run(42); return result - 42; }",
         NULL},
        {"generic-async-missing-send",
         "@generic<T> void run(T value) { async fn T work(T x) { return move x; } }",
         "R-DIAG-ASYNC-001"},
        {"generic-async-body",
         "@generic<T: send & unborrowed> async T run(T value) throws std.async::start_error { "
         "async fn T work(T x) { return move x; } T result = await work(move value); return move "
         "result; }",
         NULL},
    };
    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        RFrontendContext *context = r_frontend_create(NULL);
        char source[2048];
        RFrontendStatus status;
        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        (void)snprintf(
            source, sizeof(source), "module test.closure_modes;\n%s\n", cases[index].source);
        (void)r_semantic_add_bytes(
            context, cases[index].name, (const uint8_t *)source, strlen(source));
        status = r_frontend_analyze(context);
        if (status == R_FRONTEND_OK) {
            status = r_frontend_lower_mir(context);
            if (status == R_FRONTEND_NOT_LOWERABLE)
                status = R_FRONTEND_INVALID_SOURCE;
        }
        if (status !=
                (cases[index].diagnostic == NULL ? R_FRONTEND_OK : R_FRONTEND_INVALID_SOURCE) ||
            (cases[index].diagnostic != NULL &&
             r_semantic_diagnostic_code_count(context, cases[index].diagnostic) == 0U)) {
            (void)fprintf(stderr, "closure mode case failed: %s\n", cases[index].name);
            RSemanticTestBuffer diagnostics = {0};
            (void)r_frontend_dump_diagnostics_json(context, r_semantic_test_write, &diagnostics);
            (void)fprintf(stderr, "%s\n", diagnostics.bytes == NULL ? "" : diagnostics.bytes);
            free(diagnostics.bytes);
            R_SEMANTIC_CHECK(false);
        }
        r_frontend_destroy(context);
    }
}

static void r_semantic_test_const_generics(void) {
    static const struct {
        const char *name;
        const char *source;
        const char *diagnostic;
    } cases[] = {
        {"canonical",
         "@generic<const usize N> struct B { u8[N] data; }; void f() { B<2usize + 2usize> a = {}; "
         "B<4usize> b = a; }",
         NULL},
        {"forward",
         "void f() { B<4usize> b = {}; } @generic<const usize N> struct B { u8[N] data; };",
         NULL},
        {"zero",
         "@generic<const usize N> struct B { usize value; }; void f() { B<0usize> b = "
         "{.value=0usize}; }",
         NULL},
        {"zero-array",
         "@generic<const usize N> struct B { u8[N] data; }; void f() { B<0usize> b = {}; }",
         "R-DIAG-CONST-001"},
        {"negative",
         "@generic<const usize N> struct B { usize value; }; void f() { B<-1usize> b = {}; }",
         "R-DIAG-CONST-001"},
        {"overflow",
         "@generic<const usize N> struct B { usize value; }; void f() { "
         "B<18446744073709551615usize + 1usize> b = {}; }",
         "R-DIAG-CONST-001"},
        {"wrong-kind",
         "@generic<const usize N> struct B { usize value; }; void f() { B<i32> b = {}; }",
         "R-DIAG-CONST-001"},
        {"wrong-number-type",
         "@generic<const usize N> struct B { usize value; }; void f() { B<4u32> b = {}; }",
         "R-DIAG-CONST-001"},
        {"boolean",
         "@generic<const usize N> struct B { usize value; }; void f() { B<true> b = {}; }",
         "R-DIAG-CONST-001"},
        {"type-receives-value",
         "@generic<T> struct B { T value; }; void f() { B<4usize> b = {}; }",
         "R-DIAG-TYPE-001"},
        {"const-as-type", "@generic<const usize N> struct B { N value; };", "R-DIAG-TYPE-001"},
        {"const-unknown-field",
         "@generic<const usize N> usize f(const (u8[N])* data) { return N.value; }",
         "R-DIAG-TYPE-001"},
        {"const-readonly",
         "@generic<const usize N> usize f(const (u8[N])* data) { N = 4usize; return N; }",
         "R-DIAG-TYPE-001"},
        {"const-no-shadow",
         "@generic<const usize N> usize f(const (u8[N])* data) { usize N = 4usize; return N; }",
         "R-DIAG-NAME-003"},
        {"noninferable",
         "@generic<const usize N> usize f() { return N; } usize g() { usize n = f(); return n; }",
         "R-DIAG-TYPE-001"},
        {"noninferable-explicit",
         "@generic<const usize N> usize f() { return N; } usize g() { usize n = f::<4usize>(); "
         "return n; }",
         NULL},
        {"equation-not-inferred",
         "@generic<const usize N> usize f(const (u8[N + 1usize])* data) { return N; } "
         "usize g() { u8[5] data = {}; usize n = f(&data); return n; }",
         "R-DIAG-TYPE-001"},
        {"equation-explicit",
         "@generic<const usize N> usize f(const (u8[N + 1usize])* data) { return N; } "
         "usize g() { u8[5] data = {}; usize n = f::<4usize>(&data); return n; }",
         NULL},
        {"contradiction",
         "@generic<const usize N> usize f(const (u8[N])* a, const (u8[N])* b) { return N; } void "
         "g() { u8[4] a = {}; u8[5] b = {}; usize n = f(&a, &b); }",
         "R-DIAG-TYPE-001"},
        {"local-constant",
         "@generic<const usize N> struct B { u8[N] data; }; void f() { const usize count = 4usize; "
         "B<count> b = {}; }",
         NULL},
        {"module-constant",
         "const usize count = 4usize; @generic<const usize N> struct B { u8[N] data; }; void f() { "
         "B<count> b = {}; }",
         NULL},
        {"runtime-value",
         "@generic<const usize N> struct B { u8[N] data; }; void f(usize count) { B<count> b = {}; "
         "}",
         "R-DIAG-CONST-001"},
        {"nested-expression",
         "@generic<const usize N> struct B { u8[N] data; }; @generic<const usize N> struct W { B<N "
         "+ 1usize> value; }; void f() { W<3usize> a = {}; B<4usize> b = a.value; }",
         NULL},
        {"generic-local",
         "@generic<const usize N> usize f(const (u8[N])* a) { u8[N + 1usize] local = {}; const "
         "usize extra = N + 1usize; u8[extra] second = {}; usize first = len(local); usize last = "
         "len(second); return first + last; } void g() { u8[4] a = {}; usize size = f(&a); size as "
         "void; }",
         NULL},
        {"generic-static",
         "@generic<const usize N> usize f(const (u8[N])* a) { static const usize amount = N; "
         "return amount; } void g() { u8[4] a = {}; usize size = f(&a); size as void; }",
         NULL},
        {"generic-hook",
         "@generic<const usize N> struct B { u8[N] data; }; @generic<const usize N> drop(B<N>* "
         "self) { usize count = N; } void f() { B<4usize> b = {}; }",
         NULL},
        {"generic-error",
         "@generic<const usize N> error E { u8[N] data; }; @generic<const usize N> void fail(const "
         "(u8[N])* a) throws E<N> { throw E<N> {.data=*a}; } void f() { u8[4] a = {}; try { "
         "fail(&a); } catch(E<4usize> e) {} }",
         NULL},
        {"nested-inference",
         "@generic<const usize N> struct B { u8[N] data; }; @generic<const usize N> usize "
         "f(o<B<N>> a) { return N; } void g() { o<B<4usize>> a = o::none; usize n = f(a); n as "
         "void; }",
         NULL},
        {"bad-unused-arithmetic",
         "@generic<const usize N> usize f(const (u8[N])* a) { return N + true; }",
         "R-DIAG-TYPE-001"},
        {"resource",
         "@generic<const usize N> @noalloc @nonblocking usize f(const (u8[N])* a) { u8[N] data = "
         "{}; usize count = len(data); return count; } void g() { u8[4] a = {}; usize n = f(&a); n "
         "as void; }",
         NULL},
        {"nested-array-inference",
         "@generic<const usize N, const usize M> usize f(const (u8[N][M])* a) { return N + M; } "
         "void g() { u8[2][3] a = {}; usize n = f(&a); n as void; }",
         NULL},
        {"open-forward-call",
         "@generic<const usize N> usize inner(const (u8[N])* a) { return N; } @generic<const usize "
         "N> usize outer(const (u8[N])* a) { usize n = inner(a); return n; } void g() { u8[4] a = "
         "{}; usize n = outer(&a); n as void; }",
         NULL},
        {"parameter-shadow",
         "@generic<const usize N> usize f(u8[N] N) { return N; }",
         "R-DIAG-NAME-003"},
        {"negative-dependent",
         "@generic<const usize N> struct B { u8[-N] data; }; void f() { B<1usize> b = {}; }",
         "R-DIAG-CONST-001"},
        {"overflow-dependent",
         "@generic<const usize N> struct B { u8[N + 1usize] data; }; void f() { "
         "B<18446744073709551615usize> b = {}; }",
         "R-DIAG-CONST-001"},
        {"divide-zero-unused",
         "@generic<const usize N> struct B { u8[N / 0usize] data; };",
         "R-DIAG-CONST-001"},
        {"dimension-after-inference",
         "@generic<const usize N> void f(const (u8[N])* a, const (u8[N + 1usize])* b) {} void g() "
         "{ u8[4] a = {}; u8[4] b = {}; f(&a, &b); }",
         "R-DIAG-TYPE-001"},
        {"constant-address",
         "@generic<const usize N> usize f(u8[N] data) { const usize* p = &N; return *p; }",
         "R-DIAG-BORROW-002"},
        {"dependent-literal-index",
         "@generic<const usize N> u8 first(u8[N] values) { return values[0usize]; } void f() { "
         "u8[4] values = {}; u8 value = first(values); value as void; }",
         NULL},
        {"dependent-closed-bounds",
         "@generic<const usize N> u8 last(u8[N] values) { return values[4usize]; } void f() { "
         "u8[4] values = {}; u8 value = last(values); }",
         "R-DIAG-BOUNDS-001"},
        /* R-TYPE-0047: formulas computed by calls over the parameters. */
        {"computed-module-instance",
         "@generic<T> usize size_of() { return sizeof(T); } @generic<T> struct Buf { "
         "u8[size_of::<T>()] bytes; }; struct Frame { Buf<u64> payload; }; usize f(Buf<u16> b) "
         "{ Frame frame = {}; return len(frame.payload.bytes) + len(b.bytes); }",
         NULL},
        {"computed-shared-formula",
         "@generic<T> usize size_of() { return sizeof(T); } @generic<T: copy> "
         "u8[size_of::<T>()] zeros(T x) { x as void; u8[size_of::<T>()] r = {}; return r; } "
         "u8[4] g() { return zeros(1u32); }",
         NULL},
        {"computed-panic",
         "usize checked(usize v) { if (v == 0usize) { panic(\"empty\"); } return v; } "
         "@generic<const usize N> struct Ring { u32[checked(N)] slots; }; struct Owner { "
         "Ring<0usize> ring; };",
         "R-DIAG-CONST-003"},
        {"computed-runtime-callee",
         "thread_local usize K = 4usize; @generic<T: copy> usize width() { @if (T is u8) { "
         "return 1usize; } @else { return K; } } @generic<T: copy> struct Cell { "
         "u8[width::<T>()] bytes; }; void f() { Cell<u8> a = {}; Cell<u32> b = {}; }",
         "R-DIAG-CONST-002"},
        {"computed-unused-definition",
         "@generic<T> struct Bad { u8[missing_size::<T>()] bytes; };",
         "R-DIAG-NAME-001"},
        {"computed-own-layout",
         "@generic<T> usize size_of() { return sizeof(T); } @generic<T> struct Loop { "
         "u8[size_of::<Loop<T>>()] bytes; }; void f() { Loop<u8> loop = {}; }",
         "R-DIAG-CONST-002"},
        {"computed-zero",
         "@generic<T> usize none() { return 0usize; } @generic<T> struct Empty { "
         "u8[none::<T>()] bytes; }; void f() { Empty<u8> e = {}; }",
         "R-DIAG-CONST-001"},
        /* R-TYPE-0047: constant parameters of integer types and bool. */
        {"typed-parameters",
         "@generic<const bool C> struct G { u32 v; }; @generic<const bool C> u32 read(const G<C>* "
         "g) { @if (C == true) { return 1u32; } return g->v; } @generic<const i32 O> i32 "
         "shift(i32 v) { return v + O; } @generic<const u8 W> struct Row { u8[W as usize] c; }; "
         "i32 f() { G<true> a = {.v = 5u32}; Row<3> r = {}; Row<3u8> s = r; s as void; u32 x = "
         "read(&a); x as void; return shift::<-5>(10); }",
         NULL},
        {"typed-argument-type",
         "@generic<const u8 W> struct Row { u8 c; }; void f() { Row<3u16> r = {}; }",
         "R-DIAG-CONST-001"},
        {"typed-argument-bool",
         "@generic<const bool F> struct Flag { u8 c; }; void f() { Flag<1u8> r = {}; }",
         "R-DIAG-CONST-001"},
        {"typed-argument-range",
         "@generic<const u8 W> struct Row { u8 c; }; void f() { Row<300> r = {}; }",
         "R-DIAG-CONST-001"},
        {"typed-identity",
         "@generic<const bool F> struct Flag { u8 c; }; void f() { Flag<true> a = {}; Flag<false> "
         "b = a; }",
         "R-DIAG-TYPE-001"},
        /* R-TYPE-0050: associated constants of traits. */
        {"assoc-basic",
         "trait Shape { const usize SIDES; const bool FLAT = false; usize count(const Self* "
         "this) { return Self::SIDES; } }; struct Tri { u8 id; }; impl Shape for Tri { const "
         "usize SIDES = 3usize; }; @generic<T: Shape> struct Cells { u8[T::SIDES] c; }; "
         "@generic<T: Shape> usize f() { @if (T::FLAT == false) { return T::SIDES; } @else { "
         "return 0usize; } } struct Grid { Cells<Tri> cells; u8[Tri::SIDES] row; }; usize g() { "
         "Grid grid = {}; return len(grid.cells.c) + f::<Tri>() + Tri::SIDES; }",
         NULL},
        {"assoc-argument",
         "trait Shape { const usize SIDES; }; @generic<const usize N> struct Buf { u8[N] b; }; "
         "@generic<const usize N> impl Shape for Buf<N> { const usize SIDES = N; }; "
         "@generic<T: Shape> usize f() { Buf<T::SIDES> b = {}; return len(b.b); } usize g() { "
         "return f::<Buf<3usize>>() + Buf<2usize>::SIDES; }",
         NULL},
        {"assoc-missing",
         "trait Shape { const usize SIDES; }; struct Tri { u8 id; }; impl Shape for Tri {};",
         "R-DIAG-TRAIT-001"},
        {"assoc-extra",
         "trait Shape { const usize SIDES; }; struct Tri { u8 id; }; impl Shape for Tri { const "
         "usize SIDES = 3usize; const usize MORE = 1usize; };",
         "R-DIAG-TRAIT-001"},
        {"assoc-type",
         "trait Shape { const usize SIDES; }; struct Tri { u8 id; }; impl Shape for Tri { const "
         "u32 SIDES = 3u32; };",
         "R-DIAG-TRAIT-001"},
        {"assoc-value-type",
         "trait Shape { const usize SIDES; }; struct Tri { u8 id; }; impl Shape for Tri { const "
         "usize SIDES = true; };",
         "R-DIAG-TYPE-001"},
        {"assoc-duplicate",
         "trait Shape { const usize SIDES = 1usize; const usize SIDES = 2usize; };",
         "R-DIAG-NAME-002"},
        {"assoc-panic",
         "usize fail() { panic(\"no\"); } trait Shape { const usize SIDES; }; struct Tri { u8 "
         "id; }; impl Shape for Tri { const usize SIDES = fail(); };",
         "R-DIAG-CONST-003"},
        {"assoc-cycle",
         "trait Loop { const usize A = Self::B; const usize B = Self::A; }; struct Tri { u8 id; "
         "}; impl Loop for Tri {}; usize f() { return Tri::A; }",
         "R-DIAG-CONST-002"},
        {"assoc-unknown",
         "trait Shape { const usize SIDES; }; @generic<T: Shape> usize f() { return T::NOPE; }",
         "R-DIAG-NAME-001"},
        {"assoc-unbound",
         "trait Shape { const usize SIDES; }; @generic<T> usize f() { return T::SIDES; }",
         "R-DIAG-NAME-001"},
        {"assoc-ambiguous",
         "trait P { const usize SIZE; }; trait Q { const usize SIZE; }; struct Tri { u8 id; }; "
         "impl P for Tri { const usize SIZE = 1usize; }; impl Q for Tri { const usize SIZE = "
         "2usize; }; usize f() { return Tri::SIZE; }",
         "R-DIAG-NAME-001"},
        {"assoc-zero-bound",
         "trait Shape { const usize SIDES; }; struct Tri { u8 id; }; impl Shape for Tri { const "
         "usize SIDES = 0usize; }; @generic<T: Shape> struct Cells { u8[T::SIDES] c; }; void f() "
         "{ Cells<Tri> cells = {}; }",
         "R-DIAG-CONST-001"},
    };
    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        RFrontendContext *context = r_frontend_create(NULL);
        char source[2048];
        RFrontendStatus status;
        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        (void)snprintf(
            source, sizeof(source), "module test.const_generics;\n%s\n", cases[index].source);
        (void)r_semantic_add_bytes(
            context, cases[index].name, (const uint8_t *)source, strlen(source));
        status = r_frontend_analyze(context);
        if (status !=
                (cases[index].diagnostic == NULL ? R_FRONTEND_OK : R_FRONTEND_INVALID_SOURCE) ||
            (cases[index].diagnostic != NULL &&
             r_semantic_diagnostic_code_count(context, cases[index].diagnostic) == 0U)) {
            (void)fprintf(stderr, "const generic case failed: %s\n", cases[index].name);
            RSemanticTestBuffer diagnostics = {0};
            (void)r_frontend_dump_diagnostics_json(context, r_semantic_test_write, &diagnostics);
            (void)fprintf(stderr, "%s\n", diagnostics.bytes == NULL ? "" : diagnostics.bytes);
            free(diagnostics.bytes);
            R_SEMANTIC_CHECK(false);
        }
        r_frontend_destroy(context);
    }
}

/* R-TYPE-0051: dyn interfaces over the closed set of converted types. */
static void r_semantic_test_dyn_interfaces(void) {
    static const struct {
        const char *name;
        const char *source;
        const char *diagnostic;
    } cases[] = {
        {"basic",
         "trait S { u32 get(const Self* this); void set(Self* this, u32 v); }; struct A { u32 v; "
         "}; struct B { u32 w; }; impl S for A { u32 get(const A* this) { return this->v; } void "
         "set(A* this, u32 v) { this->v = v; } }; impl S for B { u32 get(const B* this) { return "
         "this->w; } void set(B* this, u32 v) { this->w = v; } }; u32 read(const dyn(S)* s) { "
         "return s->get(); } void write(dyn(S)* s) { s->set(1u32); const dyn(S)* view = s; "
         "view->get() as void; } u32 f() { A a = {.v = 1u32}; B b = {.w = 2u32}; write(&a); "
         "write(&b); return read(&a) + read(&b); }",
         NULL},
        {"canonical",
         "trait S { u32 get(const Self* this); }; trait T { u32 put(const Self* this); }; struct "
         "A { u32 v; }; impl S for A { u32 get(const A* this) { return 1u32; } }; impl T for A { "
         "u32 put(const A* this) { return 2u32; } }; u32 g(const dyn(T & S)* s) { return "
         "s->get() + s->put(); } u32 f() { A a = {.v = 1u32}; const dyn(S & T)* s = &a; return "
         "g(s); }",
         NULL},
        {"associated",
         "trait R { type Item; o<Self::Item> next(Self* this); }; struct C { u32 n; }; impl R "
         "for C { type Item = u32; o<u32> next(C* this) { return o::none; } }; o<u32> "
         "f(dyn(R & Item = u32)* r) { o<u32> item = r->next(); return item; }",
         NULL},
        {"not-implemented",
         "trait S { u32 get(const Self* this); }; struct A { u32 v; }; void f() { A a = {.v = "
         "1u32}; const dyn(S)* s = &a; }",
         "R-DIAG-TRAIT-001"},
        {"self-result",
         "trait C { Self copy(const Self* this); }; void f(const dyn(C)* c) {}",
         "R-DIAG-TRAIT-001"},
        {"value-receiver",
         "trait C { void eat(Self this); }; void f(const dyn(C)* c) {}",
         "R-DIAG-TRAIT-001"},
        {"value-type",
         "trait S { u32 get(const Self* this); }; void f(dyn(S) s) {}",
         "R-DIAG-TYPE-001"},
        {"capability-only", "void f(const dyn(send)* s) {}", "R-DIAG-TYPE-001"},
        {"callable", "void f(const dyn(fn(i32) -> i32)* s) {}", "R-DIAG-TYPE-001"},
        {"unfixed-associated",
         "trait R { type Item; o<Self::Item> next(Self* this); }; void f(dyn(R)* r) {}",
         "R-DIAG-TRAIT-001"},
        {"dependent",
         "@generic<T> trait R { T read(const Self* this); }; @generic<T: copy> T f(const "
         "dyn(R<T>)* r) { return r->read(); }",
         "R-DIAG-TYPE-001"},
        {"compare",
         "trait S { u32 get(const Self* this); }; bool f(const dyn(S)* a, const dyn(S)* b) { "
         "return a == b; }",
         "R-DIAG-TYPE-001"},
        {"into-other",
         "trait S { u32 get(const Self* this); }; trait T { u32 put(const Self* this); }; void "
         "f(const dyn(S)* s) { const dyn(T)* t = s; }",
         "R-DIAG-TYPE-001"},
        {"shared-to-exclusive",
         "trait S { u32 get(const Self* this); }; struct A { u32 v; }; impl S for A { u32 "
         "get(const A* this) { return 1u32; } }; void f(const A* a) { dyn(S)* s = a; }",
         "R-DIAG-TYPE-001"},
        {"escape",
         "trait S { u32 get(const Self* this); }; struct A { u32 v; }; impl S for A { u32 "
         "get(const A* this) { return 1u32; } }; const dyn(S)* f() { A a = {.v = 1u32}; const "
         "dyn(S)* s = &a; return s; }",
         "R-DIAG-BORROW-002"},
        {"exclusive",
         "trait S { u32 get(const Self* this); }; struct A { u32 v; }; impl S for A { u32 "
         "get(const A* this) { return this->v; } }; u32 f() { A a = {.v = 1u32}; dyn(S)* s = &a; "
         "a.v = 2u32; return s->get(); }",
         "R-DIAG-BORROW-001"},
        {"recursion",
         "trait S { u32 get(const Self* this); }; struct A { u32 v; }; u32 f(const dyn(S)* s) { "
         "return s->get(); } impl S for A { u32 get(const A* this) { return f(this); } }; u32 g() "
         "{ A a = {.v = 1u32}; return f(&a); }",
         "R-DIAG-STACK-001"},
        {"dereference",
         "trait S { u32 get(const Self* this); }; u32 f(const dyn(S)* s) { return (*s).get(); }",
         "R-DIAG-TYPE-001"},
        {"field",
         "trait S { u32 get(const Self* this); }; u32 f(const dyn(S)* s) { return s->v; }",
         "R-DIAG-TYPE-001"},
        {"noalloc-member",
         "trait S { u32 get(const Self* this); }; struct A { u32 v; }; impl S for A { u32 "
         "get(const A* this) { own u32* p = new u32(1u32); return *p; } }; @noalloc u32 f(const "
         "dyn(S)* s) { return s->get(); } u32 g() { A a = {.v = 1u32}; return f(&a); }",
         "R-DIAG-RESOURCE-001"},
    };
    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        RFrontendContext *context = r_frontend_create(NULL);
        char source[2048];
        RFrontendStatus status;
        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        (void)snprintf(
            source, sizeof(source), "module test.interfaces;\n%s\n", cases[index].source);
        (void)r_semantic_add_bytes(
            context, cases[index].name, (const uint8_t *)source, strlen(source));
        status = r_frontend_analyze(context);
        if (status !=
                (cases[index].diagnostic == NULL ? R_FRONTEND_OK : R_FRONTEND_INVALID_SOURCE) ||
            (cases[index].diagnostic != NULL &&
             r_semantic_diagnostic_code_count(context, cases[index].diagnostic) == 0U)) {
            (void)fprintf(stderr, "dyn interface case failed: %s\n", cases[index].name);
            RSemanticTestBuffer diagnostics = {0};
            (void)r_frontend_dump_diagnostics_json(context, r_semantic_test_write, &diagnostics);
            (void)fprintf(stderr, "%s\n", diagnostics.bytes == NULL ? "" : diagnostics.bytes);
            free(diagnostics.bytes);
            R_SEMANTIC_CHECK(false);
        }
        r_frontend_destroy(context);
    }
}

static void r_semantic_test_replacement(void) {
    static const struct {
        const char *name;
        const char *source;
        const char *diagnostic;
    } cases[] = {
        {"copy",
         "@noalloc @nonblocking i32 f(i32* p) { i32 old = core::replace(p, 3); i32 next = "
         "core::take(p); return old + next; }",
         NULL},
        {"generic",
         "@generic<T: unborrowed> T f(T* p, T value) { T old = core::replace(p, move value); "
         "return move old; }",
         NULL},
        {"generic-missing-bound",
         "@generic<T> T f(T* p, T value) { T old = core::replace(p, move value); return move old; "
         "}",
         "R-DIAG-TYPE-001"},
        {"generic-default",
         "@generic<T: pod> T f(T* p) { T old = core::take(p); return old; }",
         "R-DIAG-INIT-002"},
        {"owner",
         "@noalloc own i32* f((own i32*)* p, own i32* value) { own i32* old = core::replace(p, "
         "move "
         "value); return move old; }",
         NULL},
        {"bytes-default",
         "@noalloc bytes f(bytes* p) { bytes old = core::take(p); return move old; }",
         NULL},
        {"borrowed-field",
         "struct S { const i32* p; }; S f(S* p, S value) { S old = core::replace(p, value); return "
         "old; }",
         "R-DIAG-TYPE-001"},
        {"copy-alias",
         "void f() { i32 x = 1; i32 old = core::replace(&x, x); }",
         "R-DIAG-BORROW-001"},
        {"move-alias",
         "void f() { own i32* x = new i32(1); own i32* old = core::replace(&x, move x); }",
         "R-DIAG-BORROW-001"},
        {"active-loan",
         "i32 f() { own i32* x = new i32(1); const i32* loan = &*x; own i32* next = new i32(2); "
         "own i32* old = core::replace(&x, move next); return *loan; }",
         "R-DIAG-BORROW-001"},
        {"const",
         "void f() { const i32 x = 1; i32 old = core::replace(&x, 2); }",
         "R-DIAG-BORROW-001"},
        {"shared",
         "i32 f(const i32* p) { i32 old = core::take(p); return old; }",
         "R-DIAG-BORROW-001"},
        {"nullable",
         "i32 f(i32*? p) { i32 old = core::take(p); return old; }",
         "R-DIAG-BORROW-001"},
        {"not-pointer", "void f() { i32 old = core::take(1); }", "R-DIAG-BORROW-001"},
        {"wrong-type",
         "void f() { i32 x = 1; i32 old = core::replace(&x, true); }",
         "R-DIAG-TYPE-001"},
        {"arity-take", "void f() { i32 x = 1; i32 old = core::take(&x, 2); }", "R-DIAG-TYPE-001"},
        {"arity-replace",
         "void f() { i32 x = 1; i32 old = core::replace(&x); }",
         "R-DIAG-TYPE-001"},
        {"owner-default",
         "void f() { own i32* x = new i32(1); own i32* old = core::take(&x); }",
         "R-DIAG-INIT-002"},
        {"string-default",
         "void f() { std.string::string x = std.string::create(); std.string::string old = "
         "core::take(&x); }",
         "R-DIAG-INIT-002"},
        {"atomic", "void f() { ai32 x = 1; i32 old = core::replace(&x, 2); }", "R-DIAG-TYPE-001"},
        {"uninitialized", "void f() { i32 x; i32 old = core::take(&x); }", "R-DIAG-SYN-001"},
        {"move-consumed",
         "void f() { own i32* x = new i32(1); own i32* next = new i32(2); own i32* old = "
         "core::replace(&x, move next); i32 invalid = *next; }",
         "R-DIAG-MOVE-002"},
        {"noalloc-rhs",
         "@noalloc void f((own i32*)* p) { own i32* old = core::replace(p, new i32(1)); }",
         "R-DIAG-RESOURCE-001"},
        {"nonblocking-drop",
         "@nonblocking void f((own i32*)* p, own i32* next) { own i32* old = core::replace(p, move "
         "next); }",
         "R-DIAG-RESOURCE-002"},
    };
    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        RFrontendContext *context = r_frontend_create(NULL);
        char source[2048];
        RFrontendStatus status;
        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        (void)snprintf(
            source, sizeof(source), "module test.replacement_contract;\n%s\n", cases[index].source);
        (void)r_semantic_add_bytes(
            context, cases[index].name, (const uint8_t *)source, strlen(source));
        status = r_frontend_analyze(context);
        if (status !=
                (cases[index].diagnostic == NULL ? R_FRONTEND_OK : R_FRONTEND_INVALID_SOURCE) ||
            (cases[index].diagnostic != NULL &&
             r_semantic_diagnostic_code_count(context, cases[index].diagnostic) == 0U)) {
            (void)fprintf(stderr, "replacement case failed: %s\n", cases[index].name);
            RSemanticTestBuffer diagnostics = {0};
            (void)r_frontend_dump_diagnostics_json(context, r_semantic_test_write, &diagnostics);
            (void)fprintf(stderr, "%s\n", diagnostics.bytes == NULL ? "" : diagnostics.bytes);
            free(diagnostics.bytes);
            R_SEMANTIC_CHECK(false);
        }
        r_frontend_destroy(context);
    }
}

static void r_semantic_test_resource_contracts(void) {
    static const struct {
        const char *name;
        const char *source;
        const char *diagnostic;
    } cases[] = {
        {"pure", "@noalloc @nonblocking i32 add(i32 a, i32 b) { return a + b; }", NULL},
        {"transitive",
         "i32 leaf(i32 x) { return x; } @noalloc @nonblocking i32 f(i32 x) { i32 result = leaf(x); "
         "return result; }",
         NULL},
        {"borrow", "@noalloc @nonblocking i32 f(const i32* p) { return *p; }", NULL},
        {"hash",
         "@noalloc @nonblocking u32 f(const u8[] source) { return std.hash::crc32(source); }",
         NULL},
        {"owner-cleanup", "@noalloc void f(own i32* p) {}", NULL},
        {"copy-generic",
         "@generic<T: copy> @noalloc @nonblocking T identity(T value) { return move value; }",
         NULL},
        {"async-body", "@noalloc async i32 f() { return 42; }", NULL},
        {"checked-error",
         "error E { i32 value; }; @noalloc @nonblocking void f() throws E { throw E { .value = 1 "
         "}; }",
         NULL},
        {"new", "@noalloc void f() { own i32* value = new i32(1); }", "R-DIAG-RESOURCE-001"},
        {"nested-call",
         "void leaf() { own i32* value = new i32(1); } void middle() { leaf(); } @noalloc void f() "
         "{ middle(); }",
         "R-DIAG-RESOURCE-001"},
        {"drop-call",
         "struct S { i32 value; }; drop(S* self) { own i32* p = new i32(1); } @noalloc void f() { "
         "S value = { .value = 1 }; }",
         "R-DIAG-RESOURCE-001"},
        {"checked-drop",
         "struct S { i32 value; }; @noalloc @nonblocking drop(S* self) { self->value = 0; } "
         "@noalloc @nonblocking void f() { S value = { .value = 1 }; }",
         NULL},
        {"bad-drop-contract",
         "struct S { i32 value; }; @noalloc drop(S* self) { own i32* p = new i32(1); }",
         "R-DIAG-RESOURCE-001"},
        {"nested-drop",
         "struct S { i32 value; }; drop(S* self) { own i32* p = new i32(1); } @noalloc void "
         "f(list<S> values) {}",
         "R-DIAG-RESOURCE-001"},
        {"owner-may-block", "@nonblocking void f(own i32* p) {}", "R-DIAG-RESOURCE-002"},
        {"unknown-generic-drop", "@generic<T> @noalloc void f(T value) {}", "R-DIAG-RESOURCE-001"},
        {"unused-generic-body",
         "@generic<T: copy> @noalloc void f(T value) { own i32* p = new i32(1); }",
         "R-DIAG-RESOURCE-001"},
        {"default-owner", "@noalloc void f() { own i32* p = new i32(0); }", "R-DIAG-RESOURCE-001"},
        {"default-array-owners", "@noalloc void f() { (own i32*?)[2] values = {}; }", NULL},
        {"unreachable-runtime-if",
         "@noalloc void f() { if (false) { own i32* p = new i32(1); } }",
         "R-DIAG-RESOURCE-001"},
        {"finally",
         "error E {}; @noalloc void f() { try { throw E {}; } catch (E e) {} finally { own i32* p "
         "= new i32(1); } }",
         "R-DIAG-RESOURCE-001"},
        {"standard-allocation",
         "@noalloc void f() throws std.alloc::alloc_error { bytes value = "
         "std.bytes::with_capacity(1usize); }",
         "R-DIAG-RESOURCE-001"},
        {"async-start",
         "@noalloc async i32 child() { return 1; } @noalloc void f() throws std.async::start_error "
         "{ task<i32> value = child(); }",
         "R-DIAG-RESOURCE-001"},
        {"async-cleanup", "@nonblocking async i32 child() { return 1; }", "R-DIAG-RESOURCE-002"},
        {"ffi-unknown",
         "@abi(\"c17\") extern \"C\" { @safety(\"RESOURCE\", \"Test boundary\") void "
         "foreign_call(); } @noalloc void f() { unsafe { foreign_call(); } }",
         "R-DIAG-RESOURCE-001"},
        {"ffi-contract",
         "@abi(\"c17\") extern \"C\" { @safety(\"RESOURCE\", \"Test boundary\") @noalloc "
         "@nonblocking void foreign_call(); } @noalloc @nonblocking void f() { unsafe { "
         "foreign_call(); } }",
         NULL},
        {"ffi-independent",
         "@abi(\"c17\") extern \"C\" { @safety(\"RESOURCE\", \"Test boundary\") @noalloc void "
         "foreign_call(); } @nonblocking void f() { unsafe { foreign_call(); } }",
         "R-DIAG-RESOURCE-002"},
        {"callback-entry",
         "@callback @safety(\"RESOURCE\", \"Attached runtime boundary\") @nonblocking extern \"C\" "
         "void callback() {}",
         "R-DIAG-RESOURCE-002"},
        {"raw-signature",
         "@noalloc void f(raw fn() -> void callback) { unsafe { callback(); } }",
         "R-DIAG-RESOURCE-001"},
        {"duplicate", "@noalloc @noalloc void f() {}", "R-DIAG-SYN-002"},
        {"argument", "@nonblocking(1) void f() {}", "R-DIAG-SYN-002"},
        {"aggregate-attribute", "@noalloc struct S { i32 value; };", "R-DIAG-SYN-002"},
        {"object-attribute", "@noalloc i32 value = 0;", "R-DIAG-SYN-002"},
        {"field-attribute", "struct S { @nonblocking i32 value; };", "R-DIAG-SYN-002"},
        {"parameter-attribute", "void f(@noalloc i32 value) {}", "R-DIAG-SYN-001"},
        {"called-closure",
         "@noalloc void f() { fn void work() { own i32* p = new i32(1); } work(); }",
         "R-DIAG-RESOURCE-001"},
        {"unused-closure",
         "@noalloc void f() { fn void work() { own i32* p = new i32(1); } }",
         NULL},
        {"pure-closure",
         "@noalloc @nonblocking i32 f(i32 value) { fn i32 work(i32 x) { return x + value; } i32 "
         "result = work(1); return result; }",
         NULL},
        {"raw-qualified",
         "@noalloc @nonblocking void f(raw fn @noalloc @nonblocking() -> void callback) { unsafe { "
         "callback(); } }",
         NULL},
        {"raw-independent",
         "@nonblocking void f(raw fn @noalloc() -> void callback) { unsafe { callback(); } }",
         "R-DIAG-RESOURCE-002"},
        {"raw-erasure",
         "void f(raw fn @noalloc @nonblocking() -> void callback) { raw fn() -> void plain = "
         "callback; raw fn @noalloc?() -> void nullable = callback; }",
         NULL},
        {"raw-promotion",
         "void f(raw fn() -> void callback) { raw fn @noalloc() -> void qualified = callback; }",
         "R-DIAG-FFI-005"},
        {"raw-lost-nullability",
         "void f(raw fn @noalloc?() -> void callback) { raw fn @noalloc() -> void qualified = "
         "callback; }",
         "R-DIAG-FFI-005"},
        {"raw-contract-wrong-parameter",
         "void f(raw fn @noalloc(c_int) -> void callback) { raw fn @noalloc(c_uint) -> void "
         "qualified = callback; }",
         "R-DIAG-FFI-005"},
        {"raw-duplicate",
         "void f(raw fn @noalloc @noalloc() -> void callback) {}",
         "R-DIAG-SYN-002"},
        {"qualified-closure",
         "@generic<F: fn @noalloc @nonblocking(i32) -> i32> @noalloc @nonblocking i32 invoke(F f) "
         "{ i32 result = f.call(1); return result; } void g() { fn @noalloc @nonblocking i32 "
         "work(i32 x) { return x; } i32 result = invoke(work); result as void; }",
         NULL},
        {"qualified-once",
         "@generic<F: fn @noalloc once() -> i32> @noalloc i32 invoke(F f) { i32 result = (move "
         "f).call(); return result; } void g() { own i32* value = new i32(7); fn @noalloc once i32 "
         "work() move(value) { return *value; } i32 result = invoke(move work); result as void; }",
         NULL},
        {"closure-erasure",
         "@generic<F: fn(i32) -> i32> i32 invoke(F f) { i32 result = f.call(1); return result; } "
         "void g() { fn @noalloc @nonblocking i32 work(i32 x) { return x; } i32 result = "
         "invoke(work); result as void; }",
         NULL},
        {"closure-promotion",
         "@generic<F: fn @noalloc(i32) -> i32> i32 invoke(F f) { i32 result = f.call(1); return "
         "result; } void g() { fn i32 work(i32 x) { return x; } i32 result = invoke(work); }",
         "R-DIAG-TYPE-001"},
        {"closure-invalid-unused",
         "void g() { fn @noalloc void work() { own i32* data = new i32(7); } }",
         "R-DIAG-RESOURCE-001"},
        {"closure-environment-drop",
         "void g() { own i32* data = new i32(7); fn @nonblocking i32 work() move(data) { return "
         "*data; } }",
         "R-DIAG-RESOURCE-002"},
        {"closure-drop-allocates",
         "struct T { i32 value; }; drop(T* self) { own i32* data = new i32(1); } void g() { T "
         "value = {.value=7}; fn @noalloc i32 work() move(value) { return value.value; } }",
         "R-DIAG-RESOURCE-001"},
        {"closure-false-promise",
         "@generic<F: fn @noalloc(i32) -> i32> @nonblocking i32 invoke(F f) { i32 result = "
         "f.call(1); return result; }",
         "R-DIAG-RESOURCE-002"},
        {"prototype-mismatch", "@noalloc void f(); void f() {}", "R-DIAG-NAME-002"},
        {"function-item-unsafe",
         "unsafe i32 f() { return 1; } void g() { auto x = f; }",
         "R-DIAG-TYPE-001"},
        {"function-item-open",
         "@generic<T> T f(T x) { return move x; } void g() { auto x = f; }",
         "R-DIAG-TYPE-001"},
        {"function-item-overloaded",
         "i32 f(i32 x) { return x; } i64 f(i64 x) { return x; } "
         "void g() { auto x = f; }",
         "R-DIAG-TYPE-001"},
        {"function-item-resource",
         "i32 f(i32 x) { return x; } "
         "@generic<F: fn @noalloc(i32) -> i32> i32 invoke(F f) { return f(1); } "
         "i32 g() { return invoke(f); }",
         "R-DIAG-TYPE-001"},
        {"function-item-resource-direct",
         "i32 f() { own i32* x = new i32(1); return *x; } "
         "@noalloc i32 g() { auto item = f; return item(); }",
         "R-DIAG-RESOURCE-001"},
        {"function-item-recursive",
         "i32 f() { auto item = f; return item(); }",
         "R-DIAG-STACK-001"},
        {"function-item-opaque-contract",
         "i32 f() { return 1; } opaque(copy) factory() { return f; } "
         "i32 g() { auto value = factory(); return value.call(); }",
         "R-DIAG-NAME-001"},
        {"function-item-escape",
         "const i32* f(const i32* x) { return x; } "
         "const i32* g() { i32 x = 1; auto item = f; return item(&x); }",
         "R-DIAG-BORROW-002"},
        {"error-set-value", "@generic<E: errors> void bad(E value) {}", "R-DIAG-TYPE-001"},
        {"error-set-pointer", "@generic<E: errors> void bad(E* value) {}", "R-DIAG-TYPE-001"},
        {"error-set-ambiguous",
         "@generic<E: errors, G: errors, F: fn() -> i32 throws(E, G)> "
         "i32 bad(F f) throws E, G { return f(); }",
         "R-DIAG-TYPE-001"},
        {"error-set-catch",
         "@generic<E: errors & unborrowed, F: fn() -> i32 throws(E)> i32 bad(F f) throws E { "
         "try { return f(); } catch (E e) { e as void; return 0; } }",
         "R-DIAG-EFFECT-002"},
        {"error-set-unhandled",
         "@generic<E: errors & unborrowed, F: fn() -> i32 throws(E)> i32 bad(F f) { return f(); }",
         "R-DIAG-EFFECT-001"},
        {"error-set-async-send",
         "@generic<E: errors & unborrowed, F: async fn once() -> i32 throws(E)> "
         "async i32 bad(F f) throws E, std.async::start_error { return await (move f).call(); }",
         "R-DIAG-ASYNC-001"},
        {"error-set-borrow-escape",
         "error Borrowed { const i32* value; }; "
         "@generic<E: errors, F: fn(const i32*) -> i32 throws(E)> "
         "const i32* bad(const F* f, const i32* original) throws E { i32 local = 1; "
         "try { f(&local) as void; return original; } "
         "catch (Borrowed failure) { return failure.value; } }",
         "R-DIAG-BORROW-002"},
        {"reassign-unread", "i32 f(){i32 x=1;x=2;return x;}", "R-DIAG-USE-002"},
        {"reassign-used", "i32 f(){i32 x=1;x as void;x=2;return x;}", "R-DIAG-USE-002"},
        {"reassign-parameter", "i32 f(i32 x){x=2;return x;}", "R-DIAG-USE-002"},
        {"reassign-block", "i32 f(){i32 x=1;{x=2;}return x;}", "R-DIAG-USE-002"},
        {"reassign-constant-if", "i32 f(){i32 x=1;if(true){x=2;}return x;}", "R-DIAG-USE-002"},
        {"reassign-local-if",
         "i32 f(bool b){if(b==true){i32 x=1;x=2;return x;}return 0;}",
         "R-DIAG-USE-002"},
        {"reassign-local-loop",
         "i32 f(bool b){while(b==true){i32 x=1;x=2;return x;}return 0;}",
         "R-DIAG-USE-002"},
        {"reassign-conditional", "i32 f(bool b){i32 x=1;if(b==true){x=2;}return x;}", NULL},
        {"reassign-loop", "i32 f(i32 n){i32 x=0;while(x<n){x=x+1;}return x;}", NULL},
        {"reassign-compound", "i32 f(){i32 x=1;x+=2;return x;}", NULL},
        {"reassign-new-binding", "i32 f(){i32 x=1;i32 y=x+2;return y;}", NULL},
        {"reassign-projection", "struct S{i32 x;};i32 f(){S s={.x=1};s.x=2;return s.x;}", NULL},
        {"reassign-moved", "i32 f(){own i32* x=new i32(1);drop x;x=new i32(2);return *x;}", NULL},
        {"result-ignore", "i32 g(){return 1;}void f(){i32 x=g();}", "R-DIAG-USE-001"},
        {"result-use", "i32 g(){return 1;}i32 f(){i32 x=g();return x;}", NULL},
        {"result-branch",
         "i32 g(){return 1;}void f(bool b){i32 x=g();if(b==true){x as void;}}",
         "R-DIAG-USE-001"},
        {"result-early-return",
         "i32 calculate(){return 1;}i32 f(bool skip){i32 value=calculate();"
         "if(skip==true){return 0;}return value;}",
         "R-DIAG-USE-001"},
        {"result-after-early-return",
         "i32 calculate(){return 1;}i32 f(bool skip){if(skip==true){return 0;}"
         "i32 value=calculate();return value;}",
         NULL},
        {"result-short-circuit-error",
         "error E{};i32 g(){return 1;}void f() throws E{i32 x=g();i32 y=g();"
         "if(x!=1||y!=1){throw E{};}}",
         NULL},
        {"result-short-circuit-return",
         "i32 g(){return 1;}void f(){i32 x=g();i32 y=g();if(x!=1||y!=1){return;}}",
         "R-DIAG-USE-001"},
        {"result-short-circuit-and",
         "error E{};i32 g(){return 1;}void f() throws E{i32 x=g();i32 y=g();"
         "if(x==1&&y==1){}else{throw E{};}}",
         NULL},
        {"result-short-circuit-body-reads",
         "i32 g(){return 1;}void f(){i32 x=g();i32 y=g();if(x!=1||y!=1){y as void;}}",
         NULL},
        {"result-short-circuit-negated-error",
         "error E{};i32 g(){return 1;}void f() throws E{i32 x=g();i32 y=g();"
         "if((!(x==1&&y==1))==true){throw E{};}}",
         NULL},
        {"result-short-circuit-negated-return",
         "i32 g(){return 1;}void f(){i32 x=g();i32 y=g();if((!(x==1&&y==1))==true){return;}}",
         "R-DIAG-USE-001"},
        {"result-component-update",
         "struct S{i32 x;i32 y;};S g(){return S{.x=1,.y=2};}"
         "i32 f(){S s=g();s.x=3;return s.y;}",
         NULL},
        {"result-early-return-discard",
         "i32 calculate(){return 1;}i32 f(bool skip){i32 value=calculate();"
         "if(skip==true){value as void;return 0;}return value;}",
         NULL},
        {"result-both",
         "i32 g(){return 1;}i32 f(bool b){i32 x=g();if(b==true){return x;}return x+1;}",
         NULL},
        {"result-conditional-overwrite",
         "i32 g(){return 1;}i32 f(bool b){i32 x=g();if(b==true){x=g();}return x;}",
         "R-DIAG-USE-001"},
        {"result-conditional-used",
         "i32 g(){return 1;}i32 f(bool b){i32 x=g();x as void;if(b==true){x=g();}return x;}",
         NULL},
        {"result-new-unused",
         "i32 g(){return 1;}void f(bool b){i32 x=g();x as void;if(b==true){x=g();}}",
         "R-DIAG-USE-001"},
        {"result-finally-use",
         "i32 g(){return 1;}void f(){i32 x=g();try{return;}finally{x as void;}}",
         NULL},
        {"result-throw-unused",
         "error E{};i32 g(){return 1;}void f() throws E{i32 x=g();throw E{};}",
         NULL},
        {"result-throw-finally",
         "error E{};i32 g(){return 1;}void f() throws E{i32 x=g();try{throw E{};}finally{x as "
         "void;}}",
         NULL},
        {"result-out-forward", "i32 g(){return 1;}void f(out i32 x){x=g();}", NULL},
        {"result-out-initializer",
         "void g(out i32 x){x=1;}i32 f(){i32 x=0;g(out x);return x;}",
         NULL},
        {"result-discardable-direct", "@discardable i32 g(){return 1;}void f(){g();}", NULL},
        {"result-discardable-stored",
         "@discardable i32 g(){return 1;}void f(){i32 x=g();}",
         "R-DIAG-USE-001"},
        {"result-task-use",
         "async i32 g(){return 1;}async i32 f() throws std.async::start_error{auto t=g();return "
         "await move t;}",
         NULL},
        {"result-task-unused",
         "async i32 g(){return 1;}async void f() throws std.async::start_error{auto t=g();}",
         "R-DIAG-USE-001"},
        {"result-await-unused",
         "async i32 g(){return 1;}async void f() throws std.async::start_error{i32 x=await g();}",
         "R-DIAG-USE-001"},
        {"reassign-generic-definition",
         "@generic<T: copy> T f(T x){x=x;return x;}",
         "R-DIAG-USE-002"},
        {"result-generic-definition",
         "@generic<T: copy,F: fn() -> T> void f(F g){T x=g();}",
         "R-DIAG-USE-001"},
        {"result-generic-used",
         "@generic<T: copy & unborrowed,F: fn() -> T> T f(F g){T x=g();return x;}",
         NULL},
        {"result-error-preserve",
         "error E{};i32 g() throws E{return 1;}i32 f(bool b){i32 "
         "x=0;try{if(b==true){x=g();}}catch(E e){e as void;}return x;}",
         NULL},
        {"result-loop-continue",
         "i32 g(){return 1;}void f(bool b){while(b==true){i32 x=g();if(b==true){continue;}x as "
         "void;}}",
         "R-DIAG-USE-001"},
        {"result-loop-break",
         "i32 g(){return 1;}void f(){while(true){i32 x=g();break;}}",
         "R-DIAG-USE-001"},
        {"result-loop-use",
         "i32 g(){return 1;}i32 f(i32 n){i32 sum=0;for(i32 i=0;i<n;i++){i32 x=g();sum+=x;}return "
         "sum;}",
         NULL},
        {"result-projection-use",
         "struct S{i32 x;};S g(){return S{.x=1};}i32 f(){S x=g();return x.x;}",
         NULL},
        {"result-borrow-use",
         "const i32* g(const i32* x){return x;}i32 f(){i32 x=1;const i32* p=g(&x);return *p;}",
         NULL},
        {"result-borrow-write",
         "struct P{i32* p;};P g(i32* x){return P{.p=move x};}i32 f(){i32 x=1;P "
         "p=g(&x);*p.p=2;return *p.p;}",
         NULL},
        {"result-call-item",
         "i32 g(){return 1;}void f(){auto operation=g;i32 x=operation();}",
         "R-DIAG-USE-001"},
        {"result-must-use-branch",
         "@must_use struct S{i32 x;};void f(bool b){S x={.x=1};if(b==true){x as void;}}",
         "R-DIAG-USE-001"},
        {"result-error-cleanup",
         "error E{};own i32* g(){return new i32(1);}void fail() throws E{throw E{};}i32 f() throws "
         "E{auto p=g();fail();return *p;}",
         NULL},
        {"result-caught-cleanup",
         "error E{};own i32* g(){return new i32(1);}void fail() throws E{throw E{};}i32 "
         "f(){try{auto p=g();fail();return *p;}catch(E e){e as void;return 0;}}",
         NULL},
        {"result-normal-still-unused",
         "error E{};i32 g(){return 1;}void fail() throws E{throw E{};}void f() throws E{i32 "
         "x=g();fail();}",
         "R-DIAG-USE-001"},
        {"result-catch-preserves-live",
         "error E{};i32 g(){return 1;}void fail() throws E{throw E{};}void f(){i32 "
         "x=g();try{fail();x as void;}catch(E e){e as void;}}",
         "R-DIAG-USE-001"},
        {"result-out-error-cleanup",
         "error E{};void g(out i32 x){x=1;}void f() throws E{i32 x=0;g(out x);throw E{};}",
         NULL},
        {"result-out-panic-cleanup",
         "void g(out i32 x){x=1;}void f(){i32 x=0;g(out x);panic(\"stop\");}",
         NULL},
        {"result-error-finally-new-unused",
         "error E{};i32 g(){return 1;}void f() throws E{try{throw E{};}finally{i32 x=g();}}",
         "R-DIAG-USE-001"},
        {"out-borrow-forward", "void make(out i32 x){x=42;}void f(i32* p){make(out *p);}", NULL},
        {"out-copy-resources", "@noalloc @nonblocking void f(out i32 x){x=42;}", NULL},
        {"out-owner-resource-violation",
         "@noalloc void f(out own i32* x){x=new i32(42);}",
         "R-DIAG-RESOURCE-001"},
        {"out-lambda",
         "i32 f(){fn void make(out i32 x){x=42;}i32 x=0;make(out x);return x;}",
         NULL},
        {"out-async-lambda", "void f(){async fn void make(out i32 x){x=42;}}", "R-DIAG-ASYNC-001"},
        {"out-shared-input",
         "void f(const i32* p){p as void;}void test(){i32 x=0;f(out x);}",
         "R-DIAG-TYPE-001"},
        {"out-borrow-escape", "const i32* f(out i32 x){x=42;return &x;}", "R-DIAG-BORROW-002"},
        {"out-overload",
         "i32 f(out i32 x){x=42;return 1;}i32 f(i32* x){return *x;}i32 test(){i32 x=0;i32 v=f(out "
         "x);return x+v;}",
         NULL},
        {"out-use",
         "void make(out i32 x) { x = 42; } i32 f(){ i32 x=0; make(out x); return x; }",
         NULL},
        {"out-ignore",
         "void make(out i32 x) { x = 42; } void f(){ i32 x=0; make(out x); }",
         "R-DIAG-USE-001"},
        {"out-overwrite",
         "void make(out i32 x) { x = 42; } void f(){ i32 x=0; make(out x); x=3; x as void; }",
         "R-DIAG-USE-001"},
        {"out-twice",
         "void make(out i32 x) { x = 42; } void f(){ i32 x=0; make(out x); make(out x); x as void; "
         "}",
         "R-DIAG-USE-001"},
        {"out-branch",
         "void make(out i32 x) { x = 42; } void f(bool b){ i32 x=0; make(out x); if(b == true){x "
         "as void;} }",
         "R-DIAG-USE-001"},
        {"out-branches",
         "void make(out i32 x) { x = 42; } void f(bool b){ i32 x=0; make(out x); if(b == true){x "
         "as void;}else{x as void;} }",
         NULL},
        {"out-loop",
         "void make(out i32 x) { x = 42; } void f(bool b){ i32 x=0; while(b == true){make(out x);} "
         "x as void; }",
         "R-DIAG-USE-001"},
        {"out-loop_use",
         "void make(out i32 x) { x = 42; } void f(bool b){ i32 x=0; while(b == true){make(out x);x "
         "as void;} }",
         NULL},
        {"out-compound",
         "void make(out i32 x) { x = 42; } void f(){ i32 x=0; make(out x); x+=1; }",
         NULL},
        {"out-alias",
         "void make(out i32 x,out i32 y){x=1;y=2;} void f(){i32 x=0;make(out x,out x);x as void;}",
         "R-DIAG-BORROW-001"},
        {"out-input_alias",
         "void make(out i32 x,const i32* y){x=*y;} void f(){i32 x=0;make(out x,&x);x as void;}",
         "R-DIAG-BORROW-001"},
        {"out-readonly",
         "void make(out i32 x) { x = 42; } void f(){const i32 x=0;make(out x);x as void;}",
         "R-DIAG-BORROW-001"},
        {"out-marker",
         "void make(out i32 x) { x = 42; } void f(){i32 x=0;make(&x);x as void;}",
         "R-DIAG-TYPE-001"},
        {"out-wrong_mode",
         "void make(i32* x){*x=42;} void f(){i32 x=0;make(out x);x as void;}",
         "R-DIAG-TYPE-001"},
        {"out-missing", "void make(out i32 x){}", "R-DIAG-FLOW-001"},
        {"out-missing_branch",
         "void make(out i32 x,bool b){if(b == true){x=42;}}",
         "R-DIAG-FLOW-001"},
        {"out-read_before", "void make(out i32 x){i32 y=x;x=y;}", "R-DIAG-USE-001"},
        {"out-finally_init", "void make(out i32 x){try{return;}finally{x=42;}}", NULL},
        {"out-finally_drop",
         "void make(out own i32* x){x=new i32(42);try{return;}finally{drop x;}}",
         "R-DIAG-FLOW-001"},
        {"out-forward", "void make(out i32 x) { x = 42; } void f(out i32 x){make(out x);}", NULL},
        {"out-discardable",
         "@discardable void make(out i32 x){x=42;} void f(){i32 x=0;make(out x);}",
         NULL},
        {"out-mustuse_discard",
         "@must_use struct S{i32 x;}; @discardable void make(out S s){s=S{.x=42};}",
         "R-DIAG-USE-001"},
        {"out-async", "async void make(out i32 x){x=42;}", "R-DIAG-TYPE-001"},
        {"out-borrowed", "void make(out const i32* x,const i32* y){x=y;}", "R-DIAG-TYPE-001"},
        {"out-variadic", "void make(out i32... x){}", "R-DIAG-TYPE-001"},
        {"out-field",
         "void make(out i32 x) { x = 42; } struct S{i32 x;i32 y;};i32 f(){S "
         "s=S{.x=0,.y=0};make(out s.x);return s.x;}",
         NULL},
        {"out-field_ignore",
         "void make(out i32 x) { x = 42; } struct S{i32 x;i32 y;};i32 f(){S "
         "s=S{.x=0,.y=0};make(out s.x);return s.y;}",
         "R-DIAG-USE-001"},
        {"out-index",
         "void make(out i32 x) { x = 42; } i32 f(){i32[2] s={0,0};make(out s[0]);return s[0];}",
         NULL},
        {"out-callable",
         "void make(out i32 x) { x = 42; } @generic<F: fn(out i32)->void> void invoke(F f,out i32 "
         "x){f(out x);} i32 test(){i32 x=0;invoke(make,out x);return x;}",
         NULL},
        {"out-opaque",
         "void make(out i32 x) { x = 42; } opaque(fn(out i32)->void & copy) factory(){return "
         "make;} i32 f(){i32 x=0;auto fun=factory();fun(out x);return x;}",
         NULL},
        {"out-throw_old",
         "error E{}; void make(out i32 x) throws E {x=42;throw E{};} i32 f(){i32 x=3;try{make(out "
         "x);}catch(E e){e as void;}return x;}",
         NULL},
        {"out-throw_missing",
         "error E{}; void make(out i32 x,bool b) throws E {if(b == true){throw E{};}x=42;}",
         NULL},
        {"out-catch_use",
         "void make(out i32 x) { x = 42; } error E{}; void f(bool b) throws E {i32 x=0;make(out "
         "x);try{if(b == true){throw E{};}}catch(E e){e as void;x as void;return;}x as void;}",
         NULL},
        {"out-finally_use",
         "void make(out i32 x) { x = 42; } error E{}; void f(bool b) throws E {i32 x=0;make(out "
         "x);try{if(b == true){throw E{};}return;}finally{x as void;}}",
         NULL},
        {"out-deref", "void make(out i32 x){x=42;} i32 f(i32* p){make(out *p);return *p;}", NULL},
        {"out-discard_item",
         "@discardable void make(out i32 x){x=42;} void f(){i32 x=0;auto fun=make;fun(out x);}",
         "R-DIAG-USE-001"},
        {"out-throw_finally",
         "error E{};void f(out i32 x) throws E{x=42;try{return;}finally{throw E{};}}",
         "R-DIAG-EFFECT-001"},
        {"out-throw_finally_drop",
         "error E{}; void f(out own i32* x) throws E{x=new i32(42);try{throw E{};}finally{drop "
         "x;}}",
         NULL},
        {"out-switch",
         "void make(out i32 x){x=42;} enum E{a,b};i32 f(E e){i32 x=0;make(out x);switch(e){case "
         "E::a:return x;case E::b:return x;}}",
         NULL},
        {"out-out_never", "void f(out never x){}", "R-DIAG-TYPE-001"},
        {"out-out_const", "void f(out const i32 x){x=1;}", "R-DIAG-TYPE-001"},
        {"out-input_copy",
         "void make(out i32 x){x=42;} void f(){i32 x=0;make(out x);i32 y=x;y as void;}",
         NULL},
        {"out-owner_reinit",
         "void make(out own i32* x){x=new i32(42);}i32 f(){own i32* x=new i32(3);drop x;make(out "
         "x);return *x;}",
         NULL},
        {"out-struct_before_init", "struct S{i32 x;};void f(out S s){s.x=42;}", "R-DIAG-FLOW-001"},
        {"out-callee_consume", "void f(out own i32* x){x=new i32(42);drop x;}", "R-DIAG-FLOW-001"},
        {"out-never_return", "error E{};void f(out own i32* x) throws E{throw E{};}", NULL},
        {"out-call_before_read",
         "void make(out i32 x){x=42;} void f(bool b){i32 x=0;make(out x);if(b == true){return;}x "
         "as void;}",
         "R-DIAG-USE-001"},
        {"out-separate_fields",
         "void make(out i32 a,out i32 b){a=20;b=22;}struct S{i32 x;i32 y;};i32 f(){S "
         "s={.x=0,.y=0};make(out s.x,out s.y);return s.x+s.y;}",
         NULL},
        {"out-overlap_field",
         "struct S{i32 x;};void make(out S a,out i32 b){a=S{.x=20};b=22;}void f(){S "
         "s={.x=0};make(out s,out s.x);s as void;}",
         "R-DIAG-BORROW-001"},
        {"out-mode_trait",
         "trait T{void get(const Self* this,out i32 x);};struct S{};impl T for S{void get(const S* "
         "this,i32* x){*x=42;}};",
         "R-DIAG-TRAIT-001"},
        {"out-forward_overwrite",
         "void make(out i32 x){x=42;} void f(out i32 x){make(out x);x=1;}",
         "R-DIAG-USE-001"},
        {"out-forward_finally_overwrite",
         "void make(out i32 x){x=42;} void f(out i32 x){make(out x);try{return;}finally{x=1;}}",
         "R-DIAG-USE-001"},
        {"out-projection_then_init",
         "struct S{i32 x;};void f(out S s){s.x=1;s=S{.x=42};}",
         "R-DIAG-FLOW-001"},
        {"out-loop_break",
         "void make(out i32 x){x=42;} void f(bool b){i32 x=0;while(b == true){make(out x);break;}x "
         "as void;}",
         NULL},
        {"out-loop_continue",
         "void make(out i32 x){x=42;} void f(bool b){i32 x=0;while(b == true){make(out "
         "x);continue;}x as void;}",
         "R-DIAG-USE-001"},
        {"out-for_use",
         "void make(out i32 x){x=42;} void f(){i32 x=0;for(i32 i=0;i<2;i+=1){make(out x);x as "
         "void;}}",
         NULL},
        {"out-error_preserves_obligation",
         "error E{}; void make(out i32 x) throws E {x=42;} void f(){i32 x=0;try{make(out "
         "x);try{make(out x);}catch(E e){e as void;x as void;} x as void;}catch(E e){e as void;}}",
         "R-DIAG-USE-001"},
        {"out-callee_finally_reinit",
         "void f(out own i32* x){x=new i32(1);try{return;}finally{drop x;x=new i32(42);}}",
         NULL},
        {"must-use-function",
         "@must_use i32 compute() { return 1; } void run() { i32 value = compute(); }",
         "R-DIAG-USE-001"},
        {"must-use-expression",
         "@must_use i32 compute() { return 1; } void run() { compute(); }",
         "R-DIAG-USE-001"},
        {"must-use-type",
         "@must_use struct Ticket { i32 value; }; void run() { Ticket unused = {.value=1}; }",
         "R-DIAG-USE-001"},
        {"must-use-enum",
         "@must_use enum Ticket { Accepted, Rejected }; void run() { Ticket unused = "
         "Ticket::Accepted; }",
         "R-DIAG-USE-001"},
        {"must-use-generic-type",
         "@generic<T> @must_use struct Ticket { T value; }; void run() { Ticket<i32> unused = "
         "{.value=1}; }",
         "R-DIAG-USE-001"},
        {"must-use-used",
         "@must_use i32 compute() { return 1; } i32 run() { i32 value = compute(); return value; }",
         NULL},
        {"must-use-field",
         "@must_use struct Ticket { i32 value; }; i32 run() { Ticket used = {.value=1}; return "
         "used.value; }",
         NULL},
        {"must-use-discard-copy",
         "@must_use i32 compute() { return 1; } void run() { i32 value = compute(); value as void; "
         "compute() as void; }",
         NULL},
        {"must-use-discard-move",
         "@must_use struct Ticket { own i32* value; }; void run() { Ticket value = {.value=new "
         "i32(1)}; (move value) as void; }",
         NULL},
        {"must-use-drop",
         "@must_use struct Ticket { own i32* value; }; void run() { Ticket value = {.value=new "
         "i32(1)}; drop value; }",
         NULL},
        {"must-use-duplicate", "@must_use @must_use i32 run() { return 1; }", "R-DIAG-SYN-002"},
        {"must-use-void", "@must_use void run() {}", "R-DIAG-USE-001"},
        {"must-use-field-position", "struct Ticket { @must_use i32 value; };", "R-DIAG-SYN-002"},
        {"must-use-arguments", "@must_use(1) i32 run() { return 1; }", "R-DIAG-SYN-002"},
        {"discardable-call",
         "@discardable i32 compute() { return 1; } void run() { compute(); }",
         NULL},
        {"discardable-method",
         "struct Counter { i32 value; }; @discardable bool Counter::bump(Counter* this) { "
         "this->value += 1; return true; } void run() { Counter c = {.value=0}; c.bump(); }",
         NULL},
        {"discardable-generic",
         "@generic<T> @discardable T identity(T value) { return move value; } void run() { "
         "identity(1); }",
         NULL},
        {"discardable-trait",
         "struct S { i32 v; }; trait Ping { @discardable i32 ping(const Self* this); }; impl Ping "
         "for S { i32 ping(const S* this) { return this->v; } }; void run() { S s = {.v=1}; "
         "s.ping(); }",
         NULL},
        {"discardable-owner",
         "struct Box { own i32* data; }; @discardable Box make() { return Box {.data=new "
         "i32(1)}; } void run() { make(); }",
         NULL},
        {"discardable-await",
         "@discardable async i32 compute() { return 1; } async void run() throws "
         "std.async::start_error { await compute(); }",
         NULL},
        {"discardable-task",
         "@discardable async i32 compute() { return 1; } async void run() throws "
         "std.async::start_error { compute(); }",
         "R-DIAG-TYPE-001"},
        {"discardable-named-task",
         "@discardable async i32 compute() { return 1; } async void run() throws "
         "std.async::start_error { task<i32> pending = compute(); await move pending; }",
         "R-DIAG-ASYNC-001"},
        {"discardable-local-kept",
         "@discardable i32 compute() { return 1; } i32 run() { i32 value = compute(); return "
         "value; }",
         NULL},
        {"discardable-not-a-statement",
         "@discardable i32 compute() { return 1; } void run() { true ? compute() : compute(); }",
         "R-DIAG-TYPE-001"},
        {"discardable-plain-call",
         "@discardable i32 compute() { return 1; } i32 plain() { return 2; } void run() { "
         "compute(); plain(); }",
         "R-DIAG-TYPE-001"},
        {"discardable-void", "@discardable void run() {}", "R-DIAG-USE-001"},
        {"discardable-async-void", "@discardable async void run() {}", "R-DIAG-USE-001"},
        {"discardable-must-use",
         "@discardable @must_use i32 run() { return 1; }",
         "R-DIAG-USE-001"},
        {"discardable-must-use-type",
         "@must_use struct Ticket { i32 value; }; @discardable Ticket make() { return Ticket "
         "{.value=1}; }",
         "R-DIAG-USE-001"},
        {"discardable-duplicate",
         "@discardable @discardable i32 run() { return 1; }",
         "R-DIAG-SYN-002"},
        {"discardable-arguments", "@discardable(1) i32 run() { return 1; }", "R-DIAG-SYN-002"},
        {"discardable-field-position",
         "struct Ticket { @discardable i32 value; };",
         "R-DIAG-SYN-002"},
        {"discardable-aggregate", "@discardable struct Ticket { i32 value; };", "R-DIAG-SYN-002"},
        {"discardable-lambda",
         "void run() { fn @discardable i32 work() { return 1; } }",
         "R-DIAG-SYN-001"},
        {"discardable-redeclaration",
         "@discardable i32 f(); i32 f() { return 1; }",
         "R-DIAG-NAME-002"},
        {"discardable-std-dict",
         "void run() throws std.dict::insert_error<i32, i32> { dict<i32, i32> d = "
         "std.dict::create::<i32, i32>(); std.dict::insert(&d, 1, 2); d.insert(3, 4); i32 key = 1; "
         "std.dict::remove(&d, &key); d.remove(&key); }",
         NULL},
        {"discardable-std-array",
         "void run() throws std.array::push_error<i32> { array<i32> a = "
         "std.array::create::<i32>(); "
         "std.array::push(&a, 1); a.pop(); std.array::push(&a, 2); std.array::remove(&a, 0usize); "
         "}",
         NULL},
        {"discardable-std-list",
         "void run() throws std.list::push_error<i32> { list<i32> l = std.list::create::<i32>(); { "
         "i32* node = std.list::push_back(&l, 5); *node += 1; } l.pop_front(); "
         "std.list::pop_back(&l); }",
         NULL},
        {"discardable-std-replace", "void run() { i32 slot = 1; core::replace(&slot, 2); }", NULL},
        {"discardable-std-atomic",
         "void run() { au32 value = 5; core::atomic_fetch_add(&value, 1u32, "
         "core::memory_order::relaxed); core::atomic_exchange(&value, 9u32, "
         "core::memory_order::acq_rel); }",
         NULL},
        {"discardable-std-compare-exchange",
         "void run() { au32 value = 5; core::atomic_compare_exchange(&value, 5u32, 6u32, "
         "core::memory_order::acq_rel, core::memory_order::relaxed); }",
         "R-DIAG-TYPE-001"},
        {"discardable-std-take",
         "void run() { i32 slot = 1; core::take(&slot); }",
         "R-DIAG-TYPE-001"},
        {"discardable-std-query",
         "void run() { dict<i32, i32> d = std.dict::create::<i32, i32>(); i32 key = 1; "
         "std.dict::contains(&d, &key); }",
         "R-DIAG-TYPE-001"},
        {"discardable-std-borrowed-position",
         "void run() throws std.list::push_error<i32> { list<i32> l = std.list::create::<i32>(); "
         "std.list::push_back(&l, 5); }",
         "R-DIAG-TYPE-001"},
        {"discard-move-needs-move",
         "void run() { own i32* value = new i32(1); value as void; }",
         "R-DIAG-MOVE-001"},
        {"discard-move-reuse",
         "void run() { own i32* value = new i32(1); (move value) as void; i32 x = *value; }",
         "R-DIAG-MOVE-002"},
        {"discard-effectful-task",
         "error Failure {}; async void child() throws Failure {} void run() throws "
         "std.async::start_error { task<void throws Failure> pending = child(); (move pending) as "
         "void; }",
         "R-DIAG-ASYNC-001"},
        {"discard-resource-drop",
         "struct Ticket { own i32* value; }; drop(Ticket* self) { own i32* scratch = new i32(1); } "
         "@noalloc void consume(Ticket value) { (move value) as void; }",
         "R-DIAG-RESOURCE-001"},
    };
    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        RFrontendContext *context = r_frontend_create(NULL);
        char source[2048];
        RFrontendStatus status;
        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        (void)snprintf(
            source, sizeof(source), "module test.resource_contract;\n%s\n", cases[index].source);
        (void)r_semantic_add_bytes(
            context, cases[index].name, (const uint8_t *)source, strlen(source));
        status = r_frontend_analyze(context);
        if (status !=
                (cases[index].diagnostic == NULL ? R_FRONTEND_OK : R_FRONTEND_INVALID_SOURCE) ||
            (cases[index].diagnostic != NULL &&
             r_semantic_diagnostic_code_count(context, cases[index].diagnostic) == 0U)) {
            (void)fprintf(stderr, "resource contract case failed: %s\n", cases[index].name);
            RSemanticTestBuffer diagnostics = {0};
            (void)r_frontend_dump_diagnostics_json(context, r_semantic_test_write, &diagnostics);
            (void)fprintf(stderr, "%s\n", diagnostics.bytes == NULL ? "" : diagnostics.bytes);
            free(diagnostics.bytes);
            R_SEMANTIC_CHECK(false);
        }
        r_frontend_destroy(context);
    }
}

static void r_semantic_test_recursive_borrow_diagnostics(void) {
    static const char *const sources[] = {
        "module semantic.borrow_cycle; const i32* walk(const i32* value, u32 depth) { "
        "if (depth != 0u32) { const i32* result = walk(value, depth - 1u32); return result; } "
        "return value; }",
        "module semantic.borrow_cycle; const i32* walk(const i32* value, u32 depth) { "
        "if (depth == 0u32) { return value; } "
        "const i32* result = walk(value, depth - 1u32); return result; }",
        "module semantic.borrow_cycle; const i32* second(const i32* value); "
        "const i32* first(const i32* value) { "
        "const i32* result = second(value); return result; } "
        "const i32* second(const i32* value) { const i32* result = first(value); return result; }",
    };
    for (size_t index = 0U; index < sizeof(sources) / sizeof(sources[0]); ++index) {
        RFrontendContext *context = r_frontend_create(NULL);
        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        (void)r_semantic_add_bytes(
            context, "borrow-cycle.r", (const uint8_t *)sources[index], strlen(sources[index]));
        R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
        R_SEMANTIC_CHECK(r_semantic_diagnostic_code_count(context, "R-DIAG-STACK-001") == 1U);
        R_SEMANTIC_CHECK(r_semantic_diagnostic_code_count(context, "R-DIAG-SLICE-001") == 0U);
        r_frontend_destroy(context);
    }
}

static void r_semantic_test_imported_return_borrow_without_mapping(void) {
    static const char provider[] = "module semantic.borrow_provider;\n"
                                   "const i32* unresolved(const i32* value);\n";
    static const char consumer[] = "module semantic.borrow_consumer;\n"
                                   "import semantic.borrow_provider::{unresolved};\n"
                                   "const i32* forward(const i32* value) {\n"
                                   "    const i32* result = unresolved(value);\n"
                                   "    return result;\n"
                                   "}\n";
    RFrontendContext *context = r_frontend_create(NULL);
    RSourceId provider_id = R_SOURCE_ID_INVALID;
    RSourceId consumer_id = R_SOURCE_ID_INVALID;

    R_SEMANTIC_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_SEMANTIC_CHECK(r_frontend_add_source(context,
                                           "borrow-consumer.r",
                                           (const uint8_t *)consumer,
                                           strlen(consumer),
                                           &consumer_id) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_add_source(context,
                                           "borrow-provider.r",
                                           (const uint8_t *)provider,
                                           strlen(provider),
                                           &provider_id) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
    if (r_semantic_diagnostic_code_count(context, "R-DIAG-BORROW-002") != 1U) {
        size_t diagnostic_index;
        for (diagnostic_index = 0U; diagnostic_index < r_frontend_diagnostic_count(context);
             ++diagnostic_index) {
            const RDiagnostic *diagnostic = r_frontend_diagnostic(context, diagnostic_index);
            (void)fprintf(stderr,
                          "imported return-borrow diagnostic: %s %s\n",
                          diagnostic != NULL ? diagnostic->code : "<null>",
                          diagnostic != NULL ? diagnostic->message : "<null>");
        }
    }
    R_SEMANTIC_CHECK(r_semantic_diagnostic_code_count(context, "R-DIAG-BORROW-002") == 1U);
    R_SEMANTIC_CHECK(r_semantic_diagnostic_code_count(context, "R-DIAG-SLICE-001") == 0U);
    r_frontend_destroy(context);
}

static void r_semantic_test_imported_return_borrow_mapping(void) {
    static const char consumer[] = "module semantic.borrow_consumer_ok;\n"
                                   "import semantic.borrow_provider_ok::{identity};\n"
                                   "const i32* forward(const i32* value) {\n"
                                   "    const i32* result = identity(value);\n"
                                   "    return result;\n"
                                   "}\n";
    static const char provider[] = "module semantic.borrow_provider_ok;\n"
                                   "const i32* identity(const i32* value) {\n"
                                   "    return value;\n"
                                   "}\n";
    RFrontendContext *context = r_frontend_create(NULL);
    RSourceId consumer_id = R_SOURCE_ID_INVALID;
    RSourceId provider_id = R_SOURCE_ID_INVALID;
    RSemanticTestBuffer consumer_first_mir = {0};
    RSemanticTestBuffer provider_first_mir = {0};
    size_t symbol_index;
    size_t mapped_functions = 0U;

    R_SEMANTIC_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_SEMANTIC_CHECK(r_frontend_add_source(context,
                                           "borrow-consumer-ok.r",
                                           (const uint8_t *)consumer,
                                           strlen(consumer),
                                           &consumer_id) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_add_source(context,
                                           "borrow-provider-ok.r",
                                           (const uint8_t *)provider,
                                           strlen(provider),
                                           &provider_id) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    for (symbol_index = 0U; symbol_index < context->semantic_symbol_count; ++symbol_index) {
        const RSemanticSymbol *symbol = &context->semantic_symbols[symbol_index];
        if ((symbol->kind == R_SEMANTIC_SYMBOL_FUNCTION) &&
            (symbol->return_borrow_parameter == UINT32_C(1))) {
            mapped_functions += 1U;
        }
    }
    R_SEMANTIC_CHECK(mapped_functions == 2U);
    R_SEMANTIC_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_dump_mir(context, r_semantic_test_write, &consumer_first_mir) ==
                     R_FRONTEND_OK);
    r_frontend_destroy(context);

    context = r_frontend_create(NULL);
    R_SEMANTIC_CHECK(context != NULL);
    if (context == NULL) {
        free(consumer_first_mir.bytes);
        return;
    }
    R_SEMANTIC_CHECK(r_frontend_add_source(context,
                                           "borrow-provider-ok.r",
                                           (const uint8_t *)provider,
                                           strlen(provider),
                                           &provider_id) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_add_source(context,
                                           "borrow-consumer-ok.r",
                                           (const uint8_t *)consumer,
                                           strlen(consumer),
                                           &consumer_id) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_dump_mir(context, r_semantic_test_write, &provider_first_mir) ==
                     R_FRONTEND_OK);
    R_SEMANTIC_CHECK(consumer_first_mir.length == provider_first_mir.length);
    R_SEMANTIC_CHECK(r_semantic_test_buffers_equal(&consumer_first_mir, &provider_first_mir));
    R_SEMANTIC_CHECK(strstr(consumer_first_mir.bytes, "borrow_origin=%arg0") != NULL);
    r_frontend_destroy(context);
    free(consumer_first_mir.bytes);
    free(provider_first_mir.bytes);
}

static void r_semantic_test_standard_array_slice_origins(void) {
    static const char consumer[] = "module semantic.array_consumer;\n"
                                   "import semantic.array_provider::{view};\n"
                                   "const u8[] forward(const array<u8>* values) {\n"
                                   "    const u8[] result = view(values);\n"
                                   "    return result;\n"
                                   "}\n";
    static const char provider[] = "module semantic.array_provider;\n"
                                   "struct Image { array<u8> bytes; };\n"
                                   "const u8[] view(const array<u8>* values) {\n"
                                   "    const u8[] result = std.array::as_slice(values);\n"
                                   "    return result;\n"
                                   "}\n"
                                   "const u8[] image_view(const Image* image) {\n"
                                   "    const u8[] result = std.array::as_slice(&image->bytes);\n"
                                   "    return result;\n"
                                   "}\n";
    static const char local_escape[] = "module semantic.array_local_escape;\n"
                                       "const u8[] invalid() {\n"
                                       "    u8[1] bytes = {1};\n"
                                       "    const u8[] result = &bytes;\n"
                                       "    return result;\n"
                                       "}\n";
    RFrontendContext *context = r_frontend_create(NULL);
    RSourceId consumer_id = R_SOURCE_ID_INVALID;
    RSourceId provider_id = R_SOURCE_ID_INVALID;
    size_t node_index;
    size_t standard_slice_count = 0U;
    size_t mir_standard_slice_count = 0U;

    R_SEMANTIC_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_SEMANTIC_CHECK(r_frontend_add_source(context,
                                           "array-consumer.r",
                                           (const uint8_t *)consumer,
                                           strlen(consumer),
                                           &consumer_id) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_add_source(context,
                                           "array-provider.r",
                                           (const uint8_t *)provider,
                                           strlen(provider),
                                           &provider_id) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    for (node_index = 0U; node_index < context->hir_node_count; ++node_index) {
        const RHirNode *node = &context->hir_nodes[node_index];
        if ((node->kind == R_HIR_SLICE) && (node->operation == R_TOKEN_KW_ARRAY)) {
            const RSemanticSymbol *origin =
                node->borrow_origin == R_SYMBOL_ID_INVALID
                    ? NULL
                    : &context->semantic_symbols[(size_t)node->borrow_origin - 1U];
            R_SEMANTIC_CHECK((origin != NULL) && (origin->kind == R_SEMANTIC_SYMBOL_PARAMETER));
            standard_slice_count += 1U;
        }
    }
    R_SEMANTIC_CHECK(standard_slice_count == 2U);
    R_SEMANTIC_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
    for (node_index = 0U; node_index < context->mir_instruction_count; ++node_index) {
        const RMirInstruction *instruction = &context->mir_instructions[node_index];
        if ((instruction->kind == R_MIR_INSTRUCTION_SLICE) &&
            (instruction->operation == R_TOKEN_KW_ARRAY)) {
            R_SEMANTIC_CHECK(instruction->borrow_origin != R_SYMBOL_ID_INVALID);
            mir_standard_slice_count += 1U;
        }
    }
    R_SEMANTIC_CHECK(mir_standard_slice_count == 2U);
    r_frontend_destroy(context);

    context = r_frontend_create(NULL);
    R_SEMANTIC_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_SEMANTIC_CHECK(r_frontend_add_source(context,
                                           "array-local-escape.r",
                                           (const uint8_t *)local_escape,
                                           strlen(local_escape),
                                           &consumer_id) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
    R_SEMANTIC_CHECK(r_semantic_diagnostic_code_count(context, "R-DIAG-BORROW-002") == 1U);
    R_SEMANTIC_CHECK(r_semantic_diagnostic_code_count(context, "R-DIAG-SLICE-001") == 0U);
    r_frontend_destroy(context);
}

static void r_semantic_test_borrow_access_diagnostics(void) {
    static const char conflicting_call[] =
        "module semantic.borrow_conflict;\n"
        "void use(i32* left, const i32* right) { *left += *right; }\n"
        "i32 main() { i32 value = 1; use(&value, &value); return 0; }\n";
    static const char shared_mutation[] = "module semantic.shared_mutation;\n"
                                          "struct Item { i32 value; };\n"
                                          "void mutate(const Item* item) { item->value = 2; }\n";
    static const char local_borrow[] =
        "module semantic.local_borrow;\n"
        "i32 main() { i32 value = 1; i32* pointer = &value; value = 2; return *pointer; }\n";
    static const char arc_mutation[] = "module semantic.arc_mutation;\n"
                                       "struct Item { i32 value; };\n"
                                       "void mutate(arc Item item) { item->value = 2; }\n";
    static const char rc_mutation[] = "module semantic.rc_mutation;\n"
                                      "struct Item { i32 value; };\n"
                                      "void mutate(rc Item item) { item->value = 2; }\n";
    static const char weak_dereference[] = "module semantic.weak_dereference;\n"
                                           "struct Item { i32 value; };\n"
                                           "i32 read(weak arc Item item) { return item->value; }\n";
    static const char nullable_own_dereference[] =
        "module semantic.nullable_own_dereference;\n"
        "struct Item { i32 value; };\n"
        "i32 read(own Item*? item) { return item->value; }\n";
    static const char raw_outside_unsafe[] = "module semantic.raw_outside_unsafe;\n"
                                             "struct Item { i32 value; };\n"
                                             "i32 read(raw Item* item) { return item->value; }\n";
    const char *sources[] = {conflicting_call,
                             shared_mutation,
                             local_borrow,
                             arc_mutation,
                             rc_mutation,
                             weak_dereference,
                             nullable_own_dereference,
                             raw_outside_unsafe};
    const char *expected_codes[] = {"R-DIAG-BORROW-001",
                                    "R-DIAG-BORROW-001",
                                    "R-DIAG-BORROW-001",
                                    "R-DIAG-BORROW-001",
                                    "R-DIAG-BORROW-001",
                                    "R-DIAG-TYPE-001",
                                    "R-DIAG-BORROW-003",
                                    "R-DIAG-UNSAFE-001"};
    size_t index;

    for (index = 0U; index < (sizeof(sources) / sizeof(sources[0])); ++index) {
        RFrontendContext *context = r_frontend_create(NULL);
        RSourceId source_id = R_SOURCE_ID_INVALID;

        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        R_SEMANTIC_CHECK(r_frontend_add_source(context,
                                               "borrow-access.r",
                                               (const uint8_t *)sources[index],
                                               strlen(sources[index]),
                                               &source_id) == R_FRONTEND_OK);
        R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
        R_SEMANTIC_CHECK(r_semantic_diagnostic_code_count(context, expected_codes[index]) != 0U);
        r_frontend_destroy(context);
    }
}

static void r_semantic_test_shared_owner_clone(void) {
    static const char *negative_cases[] = {
        "module semantic.arc_clone_source_inference;\n"
        "struct Source { i32 value; };\n"
        "struct Destination { i32 value; };\n"
        "i32 main() {\n"
        "    arc Source owner = new arc Source { .value = 1 };\n"
        "    arc Destination clone = std.arc::clone(&owner);\n"
        "    return 0;\n"
        "}\n",
        "module semantic.rc_clone_source_inference;\n"
        "struct Source { i32 value; };\n"
        "struct Destination { i32 value; };\n"
        "i32 main() {\n"
        "    rc Source owner = new rc Source { .value = 1 };\n"
        "    rc Destination clone = std.rc::clone(&owner);\n"
        "    return 0;\n"
        "}\n",
    };
    uint8_t *source = NULL;
    size_t source_length = 0U;
    RFrontendContext *context = NULL;
    RSourceId source_id = R_SOURCE_ID_INVALID;
    size_t hir_arc_count = 0U;
    size_t hir_rc_count = 0U;
    size_t mir_arc_count = 0U;
    size_t mir_rc_count = 0U;
    size_t mir_borrow_count = 0U;
    size_t index;

    R_SEMANTIC_CHECK(
        r_semantic_read_file(R_SEMANTIC_SHARED_OWNER_CLONE_PATH, &source, &source_length));
    if (source == NULL) {
        return;
    }
    context = r_frontend_create(NULL);
    R_SEMANTIC_CHECK(context != NULL);
    if (context == NULL) {
        free(source);
        return;
    }
    R_SEMANTIC_CHECK(
        r_frontend_add_source(context, "shared-owner-clone.r", source, source_length, &source_id) ==
        R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    for (index = 0U; index < context->hir_node_count; ++index) {
        const RHirNode *node = &context->hir_nodes[index];
        const RSemanticType *owner_type;
        const RSemanticType *pointee_type;
        const RHirNode *borrow = NULL;
        const RSemanticType *borrow_type;
        RHirNodeId borrow_id = R_HIR_NODE_ID_INVALID;
        RSemanticTypeKind expected_kind;

        if ((node->kind != R_HIR_STANDARD_CALL) ||
            ((node->standard_operation != R_STANDARD_CALL_ARC_CLONE) &&
             (node->standard_operation != R_STANDARD_CALL_RC_CLONE))) {
            continue;
        }
        expected_kind = node->standard_operation == R_STANDARD_CALL_ARC_CLONE ? R_SEMANTIC_TYPE_ARC
                                                                              : R_SEMANTIC_TYPE_RC;
        owner_type = r_semantic_type(context, node->type);
        pointee_type = r_semantic_type(context, node->auxiliary_type);
        if ((node->child_count == UINT32_C(1)) &&
            ((size_t)node->first_child < context->hir_child_count)) {
            borrow_id = context->hir_children[(size_t)node->first_child];
            if ((borrow_id != R_HIR_NODE_ID_INVALID) &&
                ((size_t)borrow_id <= context->hir_node_count)) {
                borrow = &context->hir_nodes[(size_t)borrow_id - 1U];
            }
        }
        borrow_type = borrow == NULL ? NULL : r_semantic_type(context, borrow->type);
        R_SEMANTIC_CHECK((owner_type != NULL) && (owner_type->kind == expected_kind) &&
                         (owner_type->flags == R_SEMANTIC_TYPE_FLAG_NONE) &&
                         (owner_type->base == node->auxiliary_type));
        R_SEMANTIC_CHECK((pointee_type != NULL) && (pointee_type->kind == R_SEMANTIC_TYPE_STRUCT));
        R_SEMANTIC_CHECK((borrow != NULL) && (borrow->kind == R_HIR_BORROW) &&
                         (borrow_type != NULL) && (borrow_type->kind == R_SEMANTIC_TYPE_BORROW) &&
                         (borrow_type->flags == R_SEMANTIC_TYPE_FLAG_SHARED) &&
                         (borrow_type->base == node->type));
        if (node->standard_operation == R_STANDARD_CALL_ARC_CLONE) {
            hir_arc_count += 1U;
        } else {
            hir_rc_count += 1U;
        }
    }
    R_SEMANTIC_CHECK(hir_arc_count == 2U);
    R_SEMANTIC_CHECK(hir_rc_count == 2U);
    R_SEMANTIC_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    for (index = 0U; index < context->mir_instruction_count; ++index) {
        const RMirInstruction *instruction = &context->mir_instructions[index];
        const RSemanticType *owner_type;
        RSemanticTypeKind expected_kind;

        if (instruction->kind == R_MIR_INSTRUCTION_BORROW) {
            mir_borrow_count += 1U;
        }
        if ((instruction->kind != R_MIR_INSTRUCTION_STANDARD_CALL) ||
            ((instruction->standard_operation != R_STANDARD_CALL_ARC_CLONE) &&
             (instruction->standard_operation != R_STANDARD_CALL_RC_CLONE))) {
            continue;
        }
        expected_kind = instruction->standard_operation == R_STANDARD_CALL_ARC_CLONE
                            ? R_SEMANTIC_TYPE_ARC
                            : R_SEMANTIC_TYPE_RC;
        owner_type = r_semantic_type(context, instruction->type);
        R_SEMANTIC_CHECK((owner_type != NULL) && (owner_type->kind == expected_kind) &&
                         (owner_type->flags == R_SEMANTIC_TYPE_FLAG_NONE) &&
                         (owner_type->base == instruction->auxiliary_type));
        R_SEMANTIC_CHECK(instruction->result != R_MIR_VALUE_ID_INVALID);
        R_SEMANTIC_CHECK(instruction->symbol != R_SYMBOL_ID_INVALID);
        R_SEMANTIC_CHECK(instruction->borrow_origin == instruction->symbol);
        R_SEMANTIC_CHECK(instruction->operand_count == UINT32_C(0));
        R_SEMANTIC_CHECK(instruction->operand0 == R_MIR_VALUE_ID_INVALID);
        if (instruction->standard_operation == R_STANDARD_CALL_ARC_CLONE) {
            mir_arc_count += 1U;
        } else {
            mir_rc_count += 1U;
        }
    }
    R_SEMANTIC_CHECK(mir_arc_count == hir_arc_count);
    R_SEMANTIC_CHECK(mir_rc_count == hir_rc_count);
    R_SEMANTIC_CHECK(mir_borrow_count == 0U);
    r_frontend_destroy(context);
    context = NULL;
    free(source);
    source = NULL;

    for (index = 0U; index < (sizeof(negative_cases) / sizeof(negative_cases[0])); ++index) {
        const RDiagnostic *diagnostic;

        context = r_frontend_create(NULL);
        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        source_id = r_semantic_add_bytes(context,
                                         "shared-owner-clone-negative.r",
                                         (const uint8_t *)negative_cases[index],
                                         strlen(negative_cases[index]));
        (void)source_id;
        R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
        R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 1U);
        diagnostic = r_frontend_diagnostic(context, UINT32_C(0));
        R_SEMANTIC_CHECK((diagnostic != NULL) &&
                         (strcmp(diagnostic->code, "R-DIAG-TYPE-001") == 0) &&
                         (strcmp(diagnostic->rule_id, "R-LIB-0012") == 0) &&
                         (diagnostic->phase == R_DIAGNOSTIC_PHASE_SEMANTIC));
        R_SEMANTIC_CHECK(r_semantic_diagnostic_code_count(context, "R-DIAG-SLICE-001") == 0U);
        r_frontend_destroy(context);
        context = NULL;
    }
}

static RFrontendContext *r_semantic_make_move_import_program(bool reverse) {
    static const char provider[] = "module semantic.ownership_api;\n"
                                   "array<u8> forward(array<u8> value) { return move value; }\n"
                                   "void consume(array<u8> value) { return; }\n";
    static const char consumer[] = "module semantic.ownership_user;\n"
                                   "import semantic.ownership_api::{forward, consume};\n"
                                   "array<u8> pass(array<u8> input) {\n"
                                   "    array<u8> output = forward(move input);\n"
                                   "    return move output;\n"
                                   "}\n"
                                   "void sink(array<u8> input) { consume(move input); return; }\n";
    RFrontendContext *context = r_frontend_create(NULL);
    RSourceId source_id = R_SOURCE_ID_INVALID;

    if (context == NULL) {
        return NULL;
    }
    if (reverse) {
        if ((r_frontend_add_source(context,
                                   "ownership-user.r",
                                   (const uint8_t *)consumer,
                                   strlen(consumer),
                                   &source_id) != R_FRONTEND_OK) ||
            (r_frontend_add_source(context,
                                   "ownership-api.r",
                                   (const uint8_t *)provider,
                                   strlen(provider),
                                   &source_id) != R_FRONTEND_OK)) {
            r_frontend_destroy(context);
            return NULL;
        }
    } else if ((r_frontend_add_source(context,
                                      "ownership-api.r",
                                      (const uint8_t *)provider,
                                      strlen(provider),
                                      &source_id) != R_FRONTEND_OK) ||
               (r_frontend_add_source(context,
                                      "ownership-user.r",
                                      (const uint8_t *)consumer,
                                      strlen(consumer),
                                      &source_id) != R_FRONTEND_OK)) {
        r_frontend_destroy(context);
        return NULL;
    }
    if ((r_frontend_analyze(context) != R_FRONTEND_OK) ||
        (r_frontend_lower_mir(context) != R_FRONTEND_OK)) {
        r_frontend_destroy(context);
        return NULL;
    }
    return context;
}

static RFrontendContext *r_semantic_make_call_borrow_program(void) {
    static const char source[] = "module semantic.call_borrow_mask;\n"
                                 "void inspect(const i32* view, i32 copied, array<u8> owned) {\n"
                                 "    drop owned;\n"
                                 "    return;\n"
                                 "}\n"
                                 "void caller(const i32* view, i32 copied, array<u8> owned) {\n"
                                 "    inspect(view, copied, move owned);\n"
                                 "    return;\n"
                                 "}\n";
    RFrontendContext *context = r_frontend_create(NULL);
    RSourceId source_id = R_SOURCE_ID_INVALID;

    if (context == NULL) {
        return NULL;
    }
    if ((r_frontend_add_source(
             context, "call-borrow-mask.r", (const uint8_t *)source, strlen(source), &source_id) !=
         R_FRONTEND_OK) ||
        (r_frontend_analyze(context) != R_FRONTEND_OK)) {
        r_frontend_destroy(context);
        return NULL;
    }
    return context;
}

static RHirNode *r_semantic_find_inspect_call(RFrontendContext *context) {
    size_t node_index;

    for (node_index = 0U; node_index < context->hir_node_count; ++node_index) {
        RHirNode *node = &context->hir_nodes[node_index];

        if ((node->kind == R_HIR_CALL) &&
            r_semantic_test_symbol_named(context, node->symbol, "inspect")) {
            return node;
        }
    }
    return NULL;
}

static RFrontendContext *r_semantic_make_maximum_call_borrow_program(void) {
    static const char header[] = "module semantic.maximum_call_borrow_mask;\n"
                                 "protected void inspect_max(";
    static const char parameter_tail[] = "const i32* p126) {\n";
    static const char main_header[] = "}\n"
                                      "async i32 main() {\n"
                                      "    i32 value = 0;\n"
                                      "    inspect_max(";
    static const char source_tail[] = "&value);\n"
                                      "    return 0;\n"
                                      "}\n";
    RSemanticTestBuffer source = {0};
    RFrontendContext *context = NULL;
    RSourceId source_id = R_SOURCE_ID_INVALID;
    uint32_t parameter_index;
    bool success = false;

    if (!r_semantic_test_write(&source, header, sizeof(header) - 1U)) {
        goto cleanup;
    }
    for (parameter_index = UINT32_C(0); parameter_index < UINT32_C(126); ++parameter_index) {
        char parameter[32];
        const int length =
            snprintf(parameter, sizeof(parameter), "i32 p%u, ", (unsigned int)parameter_index);

        if ((length < 0) || ((size_t)length >= sizeof(parameter)) ||
            !r_semantic_test_write(&source, parameter, (size_t)length)) {
            goto cleanup;
        }
    }
    if (!r_semantic_test_write(&source, parameter_tail, sizeof(parameter_tail) - 1U)) {
        goto cleanup;
    }
    for (parameter_index = UINT32_C(0); parameter_index < UINT32_C(127); ++parameter_index) {
        char discard[40];
        const int length =
            snprintf(discard, sizeof(discard), "    p%u as void;\n", (unsigned int)parameter_index);

        if ((length < 0) || ((size_t)length >= sizeof(discard)) ||
            !r_semantic_test_write(&source, discard, (size_t)length)) {
            goto cleanup;
        }
    }
    if (!r_semantic_test_write(&source, main_header, sizeof(main_header) - 1U)) {
        goto cleanup;
    }
    for (parameter_index = UINT32_C(0); parameter_index < UINT32_C(126); ++parameter_index) {
        static const char argument[] = "value, ";

        if (!r_semantic_test_write(&source, argument, sizeof(argument) - 1U)) {
            goto cleanup;
        }
    }
    if (!r_semantic_test_write(&source, source_tail, sizeof(source_tail) - 1U)) {
        goto cleanup;
    }
    context = r_frontend_create(NULL);
    if ((context == NULL) ||
        (r_frontend_add_source(context,
                               "maximum-call-borrow-mask.r",
                               (const uint8_t *)source.bytes,
                               source.length,
                               &source_id) != R_FRONTEND_OK) ||
        (r_frontend_analyze(context) != R_FRONTEND_OK) ||
        (r_frontend_lower_mir(context) != R_FRONTEND_OK)) {
        goto cleanup;
    }
    success = true;

cleanup:
    free(source.bytes);
    if (!success) {
        r_frontend_destroy(context);
        context = NULL;
    }
    return context;
}

static void r_semantic_test_maximum_call_borrow_mask(void) {
    RFrontendContext *context = r_semantic_make_maximum_call_borrow_program();
    RSemanticTestBuffer dump = {0};
    size_t instruction_index;
    size_t call_count = 0U;

    R_SEMANTIC_CHECK(context != NULL);
    if (context != NULL) {
        R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
        for (instruction_index = 0U; instruction_index < context->mir_instruction_count;
             ++instruction_index) {
            const RMirInstruction *instruction = &context->mir_instructions[instruction_index];

            if ((instruction->kind != R_MIR_INSTRUCTION_CALL) ||
                !r_semantic_test_symbol_named(context, instruction->symbol, "inspect_max")) {
                continue;
            }
            R_SEMANTIC_CHECK(instruction->operand_count == UINT32_C(127));
            R_SEMANTIC_CHECK(instruction->call_borrow_mask.low == UINT64_C(0));
            R_SEMANTIC_CHECK(instruction->call_borrow_mask.high == (UINT64_C(1) << UINT32_C(62)));
            call_count += 1U;
        }
        R_SEMANTIC_CHECK(call_count == 1U);
        R_SEMANTIC_CHECK(r_frontend_dump_mir(context, r_semantic_test_write, &dump) ==
                         R_FRONTEND_OK);
        R_SEMANTIC_CHECK((dump.bytes != NULL) &&
                         (strstr(dump.bytes, "call_bounded_borrows=(arg126)") != NULL));
    }

    free(dump.bytes);
    r_frontend_destroy(context);
}

static void r_semantic_test_call_borrow_masks(void) {
    RFrontendContext *context = r_semantic_make_call_borrow_program();
    RFrontendContext *wrong_arity = NULL;
    RFrontendContext *invalid_parameter_table = NULL;
    RSemanticTestBuffer first_dump = {0};
    RSemanticTestBuffer second_dump = {0};
    RHirNode *call;
    RSymbolId callee = R_SYMBOL_ID_INVALID;
    size_t call_count = 0U;
    size_t instruction_index;

    R_SEMANTIC_CHECK(context != NULL);
    if (context != NULL) {
        call = r_semantic_find_inspect_call(context);
        R_SEMANTIC_CHECK(call != NULL);
        if (call != NULL) {
            const RSemanticSymbol *function = &context->semantic_symbols[(size_t)call->symbol - 1U];
            const size_t first_parameter = (size_t)function->first_parameter_type;
            const RSemanticType *borrow_parameter = r_semantic_type(
                context, context->semantic_parameter_types[first_parameter + UINT32_C(0)]);
            const RSemanticType *copy_parameter = r_semantic_type(
                context, context->semantic_parameter_types[first_parameter + UINT32_C(1)]);
            const RSemanticType *move_parameter = r_semantic_type(
                context, context->semantic_parameter_types[first_parameter + UINT32_C(2)]);

            callee = call->symbol;
            R_SEMANTIC_CHECK(call->child_count == UINT32_C(3));
            R_SEMANTIC_CHECK((borrow_parameter != NULL) &&
                             (borrow_parameter->kind == R_SEMANTIC_TYPE_BORROW));
            R_SEMANTIC_CHECK((copy_parameter != NULL) &&
                             (copy_parameter->kind == R_SEMANTIC_TYPE_I32));
            R_SEMANTIC_CHECK((move_parameter != NULL) &&
                             (move_parameter->kind == R_SEMANTIC_TYPE_ARRAY));
        }
        R_SEMANTIC_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
        R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
        for (instruction_index = 0U; instruction_index < context->mir_instruction_count;
             ++instruction_index) {
            const RMirInstruction *instruction = &context->mir_instructions[instruction_index];

            if ((instruction->kind != R_MIR_INSTRUCTION_CALL) || (instruction->symbol != callee)) {
                continue;
            }
            R_SEMANTIC_CHECK(instruction->operand_count == UINT32_C(3));
            R_SEMANTIC_CHECK(instruction->call_borrow_mask.low == UINT64_C(1));
            R_SEMANTIC_CHECK(instruction->call_borrow_mask.high == UINT64_C(0));
            call_count += 1U;
        }
        R_SEMANTIC_CHECK(call_count == 1U);
        R_SEMANTIC_CHECK(r_frontend_dump_mir(context, r_semantic_test_write, &first_dump) ==
                         R_FRONTEND_OK);
        R_SEMANTIC_CHECK(r_frontend_dump_mir(context, r_semantic_test_write, &second_dump) ==
                         R_FRONTEND_OK);
        R_SEMANTIC_CHECK(
            (first_dump.bytes != NULL) &&
            (strstr(first_dump.bytes, "callee=\"semantic.call_borrow_mask::inspect\" arguments=") !=
             NULL) &&
            (strstr(first_dump.bytes, "call_bounded_borrows=(arg0)") != NULL));
        R_SEMANTIC_CHECK(r_semantic_test_buffers_equal(&first_dump, &second_dump));
    }

    wrong_arity = r_semantic_make_call_borrow_program();
    R_SEMANTIC_CHECK(wrong_arity != NULL);
    if (wrong_arity != NULL) {
        call = r_semantic_find_inspect_call(wrong_arity);
        R_SEMANTIC_CHECK(call != NULL);
        if (call != NULL) {
            call->child_count = UINT32_C(2);
            R_SEMANTIC_CHECK(r_frontend_lower_mir(wrong_arity) == R_FRONTEND_INTERNAL_ERROR);
            R_SEMANTIC_CHECK(wrong_arity->mir_instruction_count == 0U);
        }
    }

    invalid_parameter_table = r_semantic_make_call_borrow_program();
    R_SEMANTIC_CHECK(invalid_parameter_table != NULL);
    if (invalid_parameter_table != NULL) {
        call = r_semantic_find_inspect_call(invalid_parameter_table);
        R_SEMANTIC_CHECK(call != NULL);
        if ((call != NULL) && (call->symbol != R_SYMBOL_ID_INVALID) &&
            ((size_t)call->symbol <= invalid_parameter_table->semantic_symbol_count) &&
            (invalid_parameter_table->semantic_parameter_type_count <= UINT32_MAX)) {
            RSemanticSymbol *function =
                &invalid_parameter_table->semantic_symbols[(size_t)call->symbol - 1U];

            function->first_parameter_type =
                (uint32_t)invalid_parameter_table->semantic_parameter_type_count;
            R_SEMANTIC_CHECK(r_frontend_lower_mir(invalid_parameter_table) ==
                             R_FRONTEND_INTERNAL_ERROR);
            R_SEMANTIC_CHECK(invalid_parameter_table->mir_instruction_count == 0U);
        }
    }

    free(first_dump.bytes);
    free(second_dump.bytes);
    r_frontend_destroy(context);
    r_frontend_destroy(wrong_arity);
    r_frontend_destroy(invalid_parameter_table);
}

static void r_semantic_test_move_values(void) {
    uint8_t *positive_source = NULL;
    uint8_t *negative_source = NULL;
    size_t positive_length = 0U;
    size_t negative_length = 0U;
    RFrontendContext *positive = NULL;
    RFrontendContext *negative = NULL;
    RFrontendContext *forward = NULL;
    RFrontendContext *reverse = NULL;
    RSemanticTestBuffer c17 = {0};
    RSemanticTestBuffer forward_mir = {0};
    RSemanticTestBuffer reverse_mir = {0};
    RSourceId source_id = R_SOURCE_ID_INVALID;
    size_t hir_moves = 0U;
    size_t hir_drops = 0U;
    size_t hir_standard_calls = 0U;
    size_t mir_moves = 0U;
    size_t mir_drops = 0U;
    size_t mir_standard_calls = 0U;
    size_t index;

    R_SEMANTIC_CHECK(
        r_semantic_read_file(R_SEMANTIC_MOVE_PATH, &positive_source, &positive_length));
    R_SEMANTIC_CHECK(
        r_semantic_read_file(R_SEMANTIC_MOVE_NEGATIVE_PATH, &negative_source, &negative_length));
    if ((positive_source == NULL) || (negative_source == NULL)) {
        goto cleanup;
    }

    positive = r_frontend_create(NULL);
    R_SEMANTIC_CHECK(positive != NULL);
    if (positive == NULL) {
        goto cleanup;
    }
    R_SEMANTIC_CHECK(r_frontend_add_source(
                         positive, "move-values.r", positive_source, positive_length, &source_id) ==
                     R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_analyze(positive) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(positive) == 0U);
    for (index = 0U; index < positive->hir_node_count; ++index) {
        const RHirNode *node = &positive->hir_nodes[index];
        hir_moves += node->kind == R_HIR_MOVE ? 1U : 0U;
        hir_drops += node->kind == R_HIR_DROP ? 1U : 0U;
        if ((node->kind == R_HIR_STANDARD_CALL) &&
            ((node->standard_operation == R_STANDARD_CALL_ARRAY_WITH_CAPACITY) ||
             (node->standard_operation == R_STANDARD_CALL_FS_PATH_FROM_UTF8))) {
            hir_standard_calls += 1U;
        }
    }
    R_SEMANTIC_CHECK(hir_moves == 3U);
    R_SEMANTIC_CHECK(hir_drops == 1U);
    R_SEMANTIC_CHECK(hir_standard_calls == 2U);
    R_SEMANTIC_CHECK(r_frontend_lower_mir(positive) == R_FRONTEND_OK);
    for (index = 0U; index < positive->mir_instruction_count; ++index) {
        const RMirInstruction *instruction = &positive->mir_instructions[index];
        mir_moves += instruction->kind == R_MIR_INSTRUCTION_MOVE ? 1U : 0U;
        mir_drops += instruction->kind == R_MIR_INSTRUCTION_DROP ? 1U : 0U;
        if ((instruction->kind == R_MIR_INSTRUCTION_STANDARD_CALL) &&
            ((instruction->standard_operation == R_STANDARD_CALL_ARRAY_WITH_CAPACITY) ||
             (instruction->standard_operation == R_STANDARD_CALL_FS_PATH_FROM_UTF8))) {
            mir_standard_calls += 1U;
        }
    }
    R_SEMANTIC_CHECK(mir_moves == hir_moves);
    R_SEMANTIC_CHECK(mir_drops == hir_drops);
    R_SEMANTIC_CHECK(mir_standard_calls == hir_standard_calls);
    R_SEMANTIC_CHECK(r_frontend_emit_c17(positive, r_semantic_test_write, &c17) ==
                     R_FRONTEND_NOT_LOWERABLE);
    R_SEMANTIC_CHECK(c17.length == 0U);

    negative = r_frontend_create(NULL);
    R_SEMANTIC_CHECK(negative != NULL);
    if (negative == NULL) {
        goto cleanup;
    }
    R_SEMANTIC_CHECK(
        r_frontend_add_source(
            negative, "move-values-negative.r", negative_source, negative_length, &source_id) ==
        R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_analyze(negative) == R_FRONTEND_INVALID_SOURCE);
    R_SEMANTIC_CHECK(r_semantic_diagnostic_code_count(negative, "R-DIAG-MOVE-001") == 1U);
    R_SEMANTIC_CHECK(r_semantic_diagnostic_code_count(negative, "R-DIAG-MOVE-002") == 1U);
    R_SEMANTIC_CHECK(r_semantic_diagnostic_code_count(negative, "R-DIAG-MOVE-003") == 2U);
    R_SEMANTIC_CHECK(r_semantic_diagnostic_code_count(negative, "R-DIAG-BORROW-001") == 1U);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(negative) == 5U);
    {
        bool found_drop_borrow = false;

        for (index = 0U; index < r_frontend_diagnostic_count(negative); ++index) {
            const RDiagnostic *diagnostic = r_frontend_diagnostic(negative, index);

            if ((diagnostic != NULL) && (strcmp(diagnostic->code, "R-DIAG-BORROW-001") == 0) &&
                (strcmp(diagnostic->rule_id, "R-INIT-0011") == 0) &&
                (strstr(diagnostic->message, "dropped Move value") != NULL)) {
                found_drop_borrow = true;
            }
        }
        R_SEMANTIC_CHECK(found_drop_borrow);
    }

    forward = r_semantic_make_move_import_program(false);
    reverse = r_semantic_make_move_import_program(true);
    R_SEMANTIC_CHECK(forward != NULL);
    R_SEMANTIC_CHECK(reverse != NULL);
    if ((forward != NULL) && (reverse != NULL)) {
        R_SEMANTIC_CHECK(r_frontend_dump_mir(forward, r_semantic_test_write, &forward_mir) ==
                         R_FRONTEND_OK);
        R_SEMANTIC_CHECK(r_frontend_dump_mir(reverse, r_semantic_test_write, &reverse_mir) ==
                         R_FRONTEND_OK);
        R_SEMANTIC_CHECK(forward_mir.length == reverse_mir.length);
        R_SEMANTIC_CHECK(r_semantic_test_buffers_equal(&forward_mir, &reverse_mir));
        R_SEMANTIC_CHECK(strstr(forward_mir.bytes, "move source=%arg0") != NULL);
        R_SEMANTIC_CHECK(strstr(forward_mir.bytes, "drop place=%arg0") != NULL);
    }

cleanup:
    free(positive_source);
    free(negative_source);
    free(c17.bytes);
    free(forward_mir.bytes);
    free(reverse_mir.bytes);
    r_frontend_destroy(positive);
    r_frontend_destroy(negative);
    r_frontend_destroy(forward);
    r_frontend_destroy(reverse);
}

typedef struct RSemanticCheckedTypeCase {
    const char *suffix;
    RTokenKind token_kind;
} RSemanticCheckedTypeCase;

static const RSemanticCheckedTypeCase r_semantic_checked_types[] = {
    {"i8", R_TOKEN_KW_I8},
    {"u8", R_TOKEN_KW_U8},
    {"i16", R_TOKEN_KW_I16},
    {"u16", R_TOKEN_KW_U16},
    {"i32", R_TOKEN_KW_I32},
    {"u32", R_TOKEN_KW_U32},
    {"i64", R_TOKEN_KW_I64},
    {"u64", R_TOKEN_KW_U64},
    {"isize", R_TOKEN_KW_ISIZE},
    {"usize", R_TOKEN_KW_USIZE},
    {"f32", R_TOKEN_KW_F32},
    {"f64", R_TOKEN_KW_F64},
    {"c_char", R_TOKEN_KW_C_CHAR},
    {"c_schar", R_TOKEN_KW_C_SCHAR},
    {"c_uchar", R_TOKEN_KW_C_UCHAR},
    {"c_short", R_TOKEN_KW_C_SHORT},
    {"c_ushort", R_TOKEN_KW_C_USHORT},
    {"c_int", R_TOKEN_KW_C_INT},
    {"c_uint", R_TOKEN_KW_C_UINT},
    {"c_long", R_TOKEN_KW_C_LONG},
    {"c_ulong", R_TOKEN_KW_C_ULONG},
    {"c_llong", R_TOKEN_KW_C_LLONG},
    {"c_ullong", R_TOKEN_KW_C_ULLONG},
    {"c_bool", R_TOKEN_KW_C_BOOL},
    {"c_wchar", R_TOKEN_KW_C_WCHAR},
    {"c_wint", R_TOKEN_KW_C_WINT},
    {"c_int8", R_TOKEN_KW_C_INT8},
    {"c_uint8", R_TOKEN_KW_C_UINT8},
    {"c_int16", R_TOKEN_KW_C_INT16},
    {"c_uint16", R_TOKEN_KW_C_UINT16},
    {"c_int32", R_TOKEN_KW_C_INT32},
    {"c_uint32", R_TOKEN_KW_C_UINT32},
    {"c_int64", R_TOKEN_KW_C_INT64},
    {"c_uint64", R_TOKEN_KW_C_UINT64},
    {"c_intptr", R_TOKEN_KW_C_INTPTR},
    {"c_uintptr", R_TOKEN_KW_C_UINTPTR},
    {"c_intmax", R_TOKEN_KW_C_INTMAX},
    {"c_uintmax", R_TOKEN_KW_C_UINTMAX},
    {"c_float", R_TOKEN_KW_C_FLOAT},
    {"c_double", R_TOKEN_KW_C_DOUBLE},
    {"c_long_double", R_TOKEN_KW_C_LONG_DOUBLE},
    {"c_size", R_TOKEN_KW_C_SIZE},
    {"c_ptrdiff", R_TOKEN_KW_C_PTRDIFF},
};

_Static_assert((sizeof(r_semantic_checked_types) / sizeof(r_semantic_checked_types[0])) ==
                   UINT32_C(43),
               "checked compiler test table shall contain exactly 43 numeric types");
_Static_assert((sizeof(r_semantic_checked_types) / sizeof(r_semantic_checked_types[0])) -
                       UINT32_C(12) ==
                   UINT32_C(31),
               "checked compiler test table shall contain exactly 31 C numeric types");

static bool r_semantic_checked_append_function(RSemanticTestBuffer *source,
                                               const char *family,
                                               const char *function_prefix,
                                               size_t destination_tag) {
    const size_t type_count =
        sizeof(r_semantic_checked_types) / sizeof(r_semantic_checked_types[0]);
    const size_t source_tag = (destination_tag + 1U) % type_count;
    const RSemanticCheckedTypeCase *destination = &r_semantic_checked_types[destination_tag];
    const RSemanticCheckedTypeCase *source_type = &r_semantic_checked_types[source_tag];
    char function[512];
    int length;

    length = snprintf(function,
                      sizeof(function),
                      "%s %s_%02zu(%s value) throws std.convert::range_error {\n"
                      "    %s converted = %s::checked_%s(value);\n"
                      "    return converted;\n"
                      "}\n",
                      destination->suffix,
                      function_prefix,
                      destination_tag,
                      source_type->suffix,
                      destination->suffix,
                      family,
                      destination->suffix);
    return (length > 0) && ((size_t)length < sizeof(function)) &&
           r_semantic_test_write(source, function, (size_t)length);
}

static bool r_semantic_checked_standard_type_named(const RFrontendContext *context,
                                                   RTypeId type_id,
                                                   const char *name) {
    const RSemanticType *type = r_semantic_type(context, type_id);
    const RInternEntry *entry;
    const size_t name_length = strlen(name);

    if ((type == NULL) || (type->kind != R_SEMANTIC_TYPE_STANDARD) ||
        (type->length == UINT64_C(0)) || (type->length > (uint64_t)context->intern_count)) {
        return false;
    }
    entry = &context->intern_entries[(size_t)type->length - 1U];
    return (entry->length == name_length) && (memcmp(entry->bytes, name, name_length) == 0);
}

static const RMirInstruction *r_semantic_checked_mir_operand_producer(
    const RFrontendContext *context, size_t call_index, RMirValueId value) {
    size_t instruction_index = call_index;

    while (instruction_index != 0U) {
        const RMirInstruction *instruction;

        instruction_index -= 1U;
        instruction = &context->mir_instructions[instruction_index];
        if (instruction->result == value) {
            return instruction;
        }
    }
    return NULL;
}

static void r_semantic_checked_verify_effect(const RFrontendContext *context,
                                             RTypeId value_type,
                                             RTypeId carrier_type,
                                             RTypeId destination_type) {
    const RSemanticType *carrier = r_semantic_type(context, carrier_type);
    RTypeId error_type = R_TYPE_ID_INVALID;

    R_SEMANTIC_CHECK(value_type == destination_type);
    R_SEMANTIC_CHECK((carrier != NULL) && (carrier->kind == R_SEMANTIC_TYPE_EFFECT_CARRIER) &&
                     (carrier->base == destination_type));
    if ((carrier != NULL) && (carrier->kind == R_SEMANTIC_TYPE_EFFECT_CARRIER) &&
        (r_semantic_effect_count(context, carrier->second) == UINT32_C(1))) {
        error_type = r_semantic_effect_at(context, carrier->second, UINT32_C(0));
    }
    R_SEMANTIC_CHECK(
        r_semantic_checked_standard_type_named(context, error_type, "std.convert::range_error"));
}

static void r_semantic_test_checked_intrinsic_matrix(void) {
    const size_t type_count =
        sizeof(r_semantic_checked_types) / sizeof(r_semantic_checked_types[0]);
    RSemanticTestBuffer source = {0};
    RFrontendContext *context = NULL;
    RTypeId type_ids[43] = {R_TYPE_ID_INVALID};
    bool hir_convert_seen[43] = {false};
    bool hir_c_seen[43] = {false};
    bool mir_convert_seen[43] = {false};
    bool mir_c_seen[43] = {false};
    RSourceId source_id = R_SOURCE_ID_INVALID;
    size_t hir_call_count = 0U;
    size_t mir_call_count = 0U;
    size_t index;

    if (!r_semantic_test_write(&source,
                               "module semantic.checked_matrix;\n",
                               strlen("module semantic.checked_matrix;\n"))) {
        R_SEMANTIC_CHECK(false);
        goto cleanup;
    }
    for (index = 0U; index < type_count; ++index) {
        if (!r_semantic_checked_append_function(&source, "std.convert", "convert", index)) {
            R_SEMANTIC_CHECK(false);
            goto cleanup;
        }
    }
    for (index = UINT32_C(12); index < type_count; ++index) {
        if (!r_semantic_checked_append_function(&source, "std.c", "c", index)) {
            R_SEMANTIC_CHECK(false);
            goto cleanup;
        }
    }

    context = r_frontend_create(NULL);
    R_SEMANTIC_CHECK(context != NULL);
    if (context == NULL) {
        goto cleanup;
    }
    source_id = r_semantic_add_bytes(
        context, "checked-matrix.r", (const uint8_t *)source.bytes, source.length);
    (void)source_id;
    R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    if (r_frontend_diagnostic_count(context) != 0U) {
        goto cleanup;
    }

    for (index = 0U; index < type_count; ++index) {
        const RSemanticType *type;
        size_t previous;

        R_SEMANTIC_CHECK(r_semantic_type_from_token(
            context, r_semantic_checked_types[index].token_kind, &type_ids[index]));
        type = r_semantic_type(context, type_ids[index]);
        R_SEMANTIC_CHECK(type != NULL);
        if (index >= UINT32_C(12)) {
            R_SEMANTIC_CHECK(
                (type != NULL) && (type->kind == R_SEMANTIC_TYPE_C_ABI_NUMERIC) &&
                (type->length == (uint64_t)r_semantic_checked_types[index].token_kind));
        }
        for (previous = 0U; previous < index; ++previous) {
            R_SEMANTIC_CHECK(type_ids[index] != type_ids[previous]);
        }
    }

    for (index = 0U; index < context->hir_node_count; ++index) {
        const RHirNode *node = &context->hir_nodes[index];
        const RHirNode *argument = NULL;
        size_t destination_tag;
        size_t source_tag;
        bool *seen;

        if (node->kind != R_HIR_STANDARD_CALL) {
            continue;
        }
        R_SEMANTIC_CHECK((node->standard_operation == R_STANDARD_CALL_CONVERT_CHECKED) ||
                         (node->standard_operation == R_STANDARD_CALL_C_CHECKED));
        if ((node->standard_operation != R_STANDARD_CALL_CONVERT_CHECKED) &&
            (node->standard_operation != R_STANDARD_CALL_C_CHECKED)) {
            continue;
        }
        R_SEMANTIC_CHECK(node->integer_value < (uint64_t)type_count);
        if (node->integer_value >= (uint64_t)type_count) {
            continue;
        }
        destination_tag = (size_t)node->integer_value;
        source_tag = (destination_tag + 1U) % type_count;
        seen = node->standard_operation == R_STANDARD_CALL_CONVERT_CHECKED ? hir_convert_seen
                                                                           : hir_c_seen;
        R_SEMANTIC_CHECK(!seen[destination_tag]);
        seen[destination_tag] = true;
        R_SEMANTIC_CHECK(node->child_count == UINT32_C(1));
        r_semantic_checked_verify_effect(
            context, node->type, node->auxiliary_type, type_ids[destination_tag]);
        if ((node->child_count == UINT32_C(1)) &&
            ((size_t)node->first_child < context->hir_child_count)) {
            const RHirNodeId argument_id = context->hir_children[(size_t)node->first_child];
            if ((argument_id != R_HIR_NODE_ID_INVALID) &&
                ((size_t)argument_id <= context->hir_node_count)) {
                argument = &context->hir_nodes[(size_t)argument_id - 1U];
            }
        }
        R_SEMANTIC_CHECK((argument != NULL) && (argument->type == type_ids[source_tag]));
        if (node->standard_operation == R_STANDARD_CALL_C_CHECKED) {
            R_SEMANTIC_CHECK(destination_tag >= UINT32_C(12));
        }
        hir_call_count += 1U;
    }
    R_SEMANTIC_CHECK(hir_call_count == UINT32_C(74));
    for (index = 0U; index < type_count; ++index) {
        R_SEMANTIC_CHECK(hir_convert_seen[index]);
        R_SEMANTIC_CHECK(hir_c_seen[index] == (index >= UINT32_C(12)));
    }

    R_SEMANTIC_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
    for (index = 0U; index < context->mir_instruction_count; ++index) {
        const RMirInstruction *instruction = &context->mir_instructions[index];
        const RMirInstruction *argument = NULL;
        size_t destination_tag;
        size_t source_tag;
        bool *seen;

        if (instruction->kind != R_MIR_INSTRUCTION_STANDARD_CALL) {
            continue;
        }
        R_SEMANTIC_CHECK((instruction->standard_operation == R_STANDARD_CALL_CONVERT_CHECKED) ||
                         (instruction->standard_operation == R_STANDARD_CALL_C_CHECKED));
        if ((instruction->standard_operation != R_STANDARD_CALL_CONVERT_CHECKED) &&
            (instruction->standard_operation != R_STANDARD_CALL_C_CHECKED)) {
            continue;
        }
        R_SEMANTIC_CHECK(instruction->integer_value < (uint64_t)type_count);
        if (instruction->integer_value >= (uint64_t)type_count) {
            continue;
        }
        destination_tag = (size_t)instruction->integer_value;
        source_tag = (destination_tag + 1U) % type_count;
        seen = instruction->standard_operation == R_STANDARD_CALL_CONVERT_CHECKED ? mir_convert_seen
                                                                                  : mir_c_seen;
        R_SEMANTIC_CHECK(!seen[destination_tag]);
        seen[destination_tag] = true;
        R_SEMANTIC_CHECK(instruction->operand_count == UINT32_C(1));
        r_semantic_checked_verify_effect(
            context, instruction->auxiliary_type, instruction->type, type_ids[destination_tag]);
        if ((instruction->operand_count == UINT32_C(1)) &&
            ((size_t)instruction->first_operand < context->mir_operand_count)) {
            argument = r_semantic_checked_mir_operand_producer(
                context, index, context->mir_operands[(size_t)instruction->first_operand]);
        }
        R_SEMANTIC_CHECK((argument != NULL) && (argument->type == type_ids[source_tag]));
        if (instruction->standard_operation == R_STANDARD_CALL_C_CHECKED) {
            R_SEMANTIC_CHECK(destination_tag >= UINT32_C(12));
        }
        mir_call_count += 1U;
    }
    R_SEMANTIC_CHECK(mir_call_count == hir_call_count);
    for (index = 0U; index < type_count; ++index) {
        R_SEMANTIC_CHECK(mir_convert_seen[index]);
        R_SEMANTIC_CHECK(mir_c_seen[index] == (index >= UINT32_C(12)));
    }

cleanup:
    r_frontend_destroy(context);
    free(source.bytes);
}

static void r_semantic_test_checked_intrinsic_diagnostics(void) {
    static const struct {
        const char *source;
        const char *rule_id;
    } cases[] = {
        {"module semantic.checked_bad_c_destination;\n"
         "protected i32 probe() throws std.convert::range_error {\n"
         "    i8 value = std.c::checked_i8(1i32);\n"
         "    return 0;\n"
         "}\n",
         "R-SLIB-C-0001"},
        {"module semantic.checked_bad_convert_suffix;\n"
         "protected i32 probe() throws std.convert::range_error {\n"
         "    i8 value = "
         "std.convert::checked_missing(1i32);\n"
         "    return 0;\n"
         "}\n",
         "R-SLIB-CONV-0004"},
        {"module semantic.checked_bad_c_suffix;\n"
         "protected i32 probe() throws std.convert::range_error {\n"
         "    c_int value = std.c::checked_missing(1i32);\n"
         "    return 0;\n"
         "}\n",
         "R-SLIB-C-0001"},
        {"module semantic.checked_bad_convert_arity;\n"
         "protected i32 probe() throws std.convert::range_error {\n"
         "    i8 value = std.convert::checked_i8();\n"
         "    return 0;\n"
         "}\n",
         "R-SLIB-CONV-0004"},
        {"module semantic.checked_bad_c_arity;\n"
         "protected i32 probe() throws std.convert::range_error {\n"
         "    c_int value = "
         "std.c::checked_c_int(1i32, 2i32);\n"
         "    return 0;\n"
         "}\n",
         "R-SLIB-C-0001"},
    };
    size_t case_index;

    for (case_index = 0U; case_index < (sizeof(cases) / sizeof(cases[0])); ++case_index) {
        RFrontendContext *context = r_frontend_create(NULL);
        const RDiagnostic *diagnostic;
        RSourceId source_id = R_SOURCE_ID_INVALID;

        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        source_id = r_semantic_add_bytes(context,
                                         "checked-negative.r",
                                         (const uint8_t *)cases[case_index].source,
                                         strlen(cases[case_index].source));
        (void)source_id;
        R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
        R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 1U);
        diagnostic = r_frontend_diagnostic(context, UINT32_C(0));
        R_SEMANTIC_CHECK((diagnostic != NULL) &&
                         (strcmp(diagnostic->code, "R-DIAG-TYPE-001") == 0) &&
                         (strcmp(diagnostic->rule_id, cases[case_index].rule_id) == 0) &&
                         (diagnostic->phase == R_DIAGNOSTIC_PHASE_SEMANTIC));
        r_frontend_destroy(context);
    }
}

static void r_semantic_test_core_assume(void) {
    static const char positive_source[] = "module semantic.core_assume;\n"
                                          "i32 main() {\n"
                                          "    unsafe {\n"
                                          "        unsafe {\n"
                                          "            core::assume(true);\n"
                                          "        }\n"
                                          "    }\n"
                                          "    return 0;\n"
                                          "}\n";
    static const struct {
        const char *source;
        const char *code;
        const char *rule_id;
    } negative_cases[] = {
        {
            "module semantic.core_assume_outside;\n"
            "i32 main() {\n"
            "    core::assume(true);\n"
            "    return 0;\n"
            "}\n",
            "R-DIAG-UNSAFE-001",
            "R-UNSAFE-0001",
        },
        {
            "module semantic.core_assume_type;\n"
            "i32 main() {\n"
            "    unsafe { core::assume(1i32); }\n"
            "    return 0;\n"
            "}\n",
            "R-DIAG-TYPE-001",
            "R-UNSAFE-0008",
        },
        {
            "module semantic.core_assume_arity;\n"
            "i32 main() {\n"
            "    unsafe { core::assume(); }\n"
            "    return 0;\n"
            "}\n",
            "R-DIAG-TYPE-001",
            "R-UNSAFE-0008",
        },
    };
    RFrontendContext *context = r_frontend_create(NULL);
    RSourceId source_id = R_SOURCE_ID_INVALID;
    size_t hir_count = 0U;
    size_t mir_count = 0U;
    size_t index;

    R_SEMANTIC_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    source_id = r_semantic_add_bytes(context,
                                     "core-assume-positive.r",
                                     (const uint8_t *)positive_source,
                                     strlen(positive_source));
    (void)source_id;
    R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    for (index = 0U; index < context->hir_node_count; ++index) {
        const RHirNode *node = &context->hir_nodes[index];

        if ((node->kind == R_HIR_STANDARD_CALL) &&
            (node->standard_operation == R_STANDARD_CALL_CORE_ASSUME)) {
            const RSemanticType *result_type = r_semantic_type(context, node->type);
            const RSemanticType *argument_type = r_semantic_type(context, node->auxiliary_type);

            R_SEMANTIC_CHECK(node->child_count == UINT32_C(1));
            R_SEMANTIC_CHECK((result_type != NULL) && (result_type->kind == R_SEMANTIC_TYPE_VOID));
            R_SEMANTIC_CHECK((argument_type != NULL) &&
                             (argument_type->kind == R_SEMANTIC_TYPE_BOOL));
            hir_count += 1U;
        }
    }
    R_SEMANTIC_CHECK(hir_count == 1U);
    R_SEMANTIC_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
    for (index = 0U; index < context->mir_instruction_count; ++index) {
        const RMirInstruction *instruction = &context->mir_instructions[index];

        if ((instruction->kind == R_MIR_INSTRUCTION_STANDARD_CALL) &&
            (instruction->standard_operation == R_STANDARD_CALL_CORE_ASSUME)) {
            const RSemanticType *result_type = r_semantic_type(context, instruction->type);
            const RSemanticType *argument_type =
                r_semantic_type(context, instruction->auxiliary_type);

            R_SEMANTIC_CHECK(instruction->operand_count == UINT32_C(1));
            R_SEMANTIC_CHECK(instruction->result == R_MIR_VALUE_ID_INVALID);
            R_SEMANTIC_CHECK((result_type != NULL) && (result_type->kind == R_SEMANTIC_TYPE_VOID));
            R_SEMANTIC_CHECK((argument_type != NULL) &&
                             (argument_type->kind == R_SEMANTIC_TYPE_BOOL));
            mir_count += 1U;
        }
    }
    R_SEMANTIC_CHECK(mir_count == hir_count);
    r_frontend_destroy(context);

    for (index = 0U; index < (sizeof(negative_cases) / sizeof(negative_cases[0])); ++index) {
        const RDiagnostic *diagnostic;

        context = r_frontend_create(NULL);
        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        source_id = r_semantic_add_bytes(context,
                                         "core-assume-negative.r",
                                         (const uint8_t *)negative_cases[index].source,
                                         strlen(negative_cases[index].source));
        (void)source_id;
        R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
        R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 1U);
        diagnostic = r_frontend_diagnostic(context, UINT32_C(0));
        R_SEMANTIC_CHECK((diagnostic != NULL) &&
                         (strcmp(diagnostic->code, negative_cases[index].code) == 0) &&
                         (strcmp(diagnostic->rule_id, negative_cases[index].rule_id) == 0) &&
                         (diagnostic->phase == R_DIAGNOSTIC_PHASE_SEMANTIC));
        r_frontend_destroy(context);
    }
}

static void r_semantic_test_core_slice_from_raw_parts(void) {
    static const char positive_source[] =
        "module semantic.core_slice_from_raw_parts;\n"
        "struct Pair { i32 first; i32 second; };\n"
        "void inspect(raw const Pair*? pointer, usize length) {\n"
        "    unsafe {\n"
        "        const Pair[] items = core::slice_from_raw_parts(pointer, length);\n"
        "        usize count = len(items);\n"
        "    }\n"
        "}\n"
        "void edit(raw Pair*? pointer, usize length) {\n"
        "    unsafe { Pair[] items = core::slice_from_raw_parts_mut(pointer, length); "
        "len(items) as void; }\n"
        "}\n"
        "void preserve_unrelated_origins(const Pair* input) {\n"
        "    const Pair* alias = input;\n"
        "    constexpr str program_text = \"program storage\";\n"
        "}\n"
        "i32 main() {\n"
        "    Pair value = { .first = 1, .second = 2 };\n"
        "    unsafe {\n"
        "        raw const Pair* pointer = &value as raw const Pair*;\n"
        "        const Pair[] empty = core::slice_from_raw_parts(pointer, 0usize);\n"
        "        usize count = len(empty);\n"
        "    }\n"
        "    return 0;\n"
        "}\n";
    static const struct {
        const char *source;
        const char *code;
        const char *rule_id;
    } negative_cases[] = {
        {
            "module semantic.slice_parts_outside;\n"
            "void inspect(raw const i32*? pointer) {\n"
            "    const i32[] items = core::slice_from_raw_parts(pointer, 0usize);\n"
            "}\n",
            "R-DIAG-UNSAFE-001",
            "R-UNSAFE-0001",
        },
        {
            "module semantic.slice_parts_sharedness;\n"
            "void inspect(raw i32*? pointer) {\n"
            "    unsafe { const i32[] items = core::slice_from_raw_parts(pointer, 0usize); }\n"
            "}\n",
            "R-DIAG-TYPE-001",
            "R-UNSAFE-0008",
        },
        {
            "module semantic.slice_parts_mut_sharedness;\n"
            "void inspect(raw const i32*? pointer) {\n"
            "    unsafe { i32[] items = core::slice_from_raw_parts_mut(pointer, 0usize); }\n"
            "}\n",
            "R-DIAG-TYPE-001",
            "R-UNSAFE-0008",
        },
        {
            "module semantic.slice_parts_length;\n"
            "void inspect(raw const i32*? pointer, i32 length) {\n"
            "    unsafe { const i32[] items = core::slice_from_raw_parts(pointer, length); }\n"
            "}\n",
            "R-DIAG-TYPE-001",
            "R-UNSAFE-0008",
        },
        {
            "module semantic.slice_parts_arity;\n"
            "void inspect(raw const i32*? pointer) {\n"
            "    unsafe { const i32[] items = core::slice_from_raw_parts(pointer); }\n"
            "}\n",
            "R-DIAG-TYPE-001",
            "R-UNSAFE-0008",
        },
        {
            "module semantic.slice_parts_escape;\n"
            "const i32[] inspect(raw const i32*? pointer) {\n"
            "    unsafe {\n"
            "        const i32[] items = core::slice_from_raw_parts(pointer, 0usize);\n"
            "        const i32[] alias = items;\n"
            "        return alias;\n"
            "    }\n"
            "}\n",
            "R-DIAG-BORROW-002",
            "R-BORROW-0007",
        },
    };
    static const char direct_return[] =
        "module semantic.slice_parts_direct_return;\n"
        "const i32[] inspect(raw const i32*? pointer) {\n"
        "    unsafe { return core::slice_from_raw_parts(pointer, 0usize); }\n"
        "}\n";
    static const char aggregate_escape[] =
        "module semantic.slice_parts_aggregate_escape;\n"
        "struct Holder { const i32[] values; };\n"
        "Holder inspect(raw const i32*? pointer) {\n"
        "    unsafe {\n"
        "        const i32[] values = core::slice_from_raw_parts(pointer, 0usize);\n"
        "        Holder result = { .values = values };\n"
        "        return result;\n"
        "    }\n"
        "}\n";
    RFrontendContext *context = r_frontend_create(NULL);
    size_t hir_shared = 0U;
    size_t hir_mutable = 0U;
    size_t hir_nullable_pointer = 0U;
    size_t fresh_bindings = 0U;
    size_t preserved_borrow_bindings = 0U;
    size_t program_storage_bindings = 0U;
    size_t mir_shared = 0U;
    size_t mir_mutable = 0U;
    size_t index;

    R_SEMANTIC_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    (void)r_semantic_add_bytes(context,
                               "core-slice-from-raw-parts-positive.r",
                               (const uint8_t *)positive_source,
                               strlen(positive_source));
    R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    for (index = 0U; index < context->hir_node_count; ++index) {
        const RHirNode *node = &context->hir_nodes[index];

        if ((node->kind == R_HIR_STANDARD_CALL) &&
            ((node->standard_operation == R_STANDARD_CALL_CORE_SLICE_FROM_RAW_PARTS) ||
             (node->standard_operation == R_STANDARD_CALL_CORE_SLICE_FROM_RAW_PARTS_MUT))) {
            const bool shared =
                node->standard_operation == R_STANDARD_CALL_CORE_SLICE_FROM_RAW_PARTS;
            const RSemanticType *result_type = r_semantic_type(context, node->type);
            const RSemanticType *element_type = r_semantic_type(context, node->auxiliary_type);
            const RHirNodeId pointer_id = context->hir_children[node->first_child];
            const RHirNodeId length_id = context->hir_children[node->first_child + UINT32_C(1)];
            const RHirNode *pointer = &context->hir_nodes[(size_t)pointer_id - 1U];
            const RHirNode *length = &context->hir_nodes[(size_t)length_id - 1U];
            const RSemanticType *pointer_type = r_semantic_type(context, pointer->type);
            const RSemanticType *length_type = r_semantic_type(context, length->type);

            R_SEMANTIC_CHECK(node->child_count == UINT32_C(2));
            R_SEMANTIC_CHECK(node->borrow_origin == R_SYMBOL_ID_INVALID);
            R_SEMANTIC_CHECK(
                (result_type != NULL) && (result_type->kind == R_SEMANTIC_TYPE_SLICE) &&
                (((result_type->flags & R_SEMANTIC_TYPE_FLAG_SHARED) != 0U) == shared) &&
                (result_type->base == node->auxiliary_type));
            R_SEMANTIC_CHECK((element_type != NULL) &&
                             (element_type->kind == R_SEMANTIC_TYPE_STRUCT));
            R_SEMANTIC_CHECK(
                (pointer_type != NULL) && (pointer_type->kind == R_SEMANTIC_TYPE_RAW) &&
                (pointer_type->base == node->auxiliary_type) &&
                (((pointer_type->flags & R_SEMANTIC_TYPE_FLAG_SHARED) != 0U) == shared));
            R_SEMANTIC_CHECK((length_type != NULL) && (length_type->kind == R_SEMANTIC_TYPE_USIZE));
            if ((pointer_type != NULL) &&
                ((pointer_type->flags & R_SEMANTIC_TYPE_FLAG_NULLABLE) != 0U)) {
                hir_nullable_pointer += 1U;
            }
            if (shared) {
                hir_shared += 1U;
            } else {
                hir_mutable += 1U;
            }
        } else if ((node->kind == R_HIR_LOCAL) && (node->child_count == UINT32_C(1)) &&
                   ((size_t)node->first_child < context->hir_child_count)) {
            const RHirNodeId initializer_id = context->hir_children[node->first_child];
            const RSemanticType *local_type = r_semantic_type(context, node->type);
            const RHirNode *initializer = (initializer_id == R_HIR_NODE_ID_INVALID) ||
                                                  ((size_t)initializer_id > context->hir_node_count)
                                              ? NULL
                                              : &context->hir_nodes[(size_t)initializer_id - 1U];

            if ((initializer != NULL) && (initializer->kind == R_HIR_STANDARD_CALL) &&
                ((initializer->standard_operation == R_STANDARD_CALL_CORE_SLICE_FROM_RAW_PARTS) ||
                 (initializer->standard_operation ==
                  R_STANDARD_CALL_CORE_SLICE_FROM_RAW_PARTS_MUT))) {
                const RSemanticSymbol *symbol =
                    (node->symbol == R_SYMBOL_ID_INVALID) ||
                            ((size_t)node->symbol > context->semantic_symbol_count)
                        ? NULL
                        : &context->semantic_symbols[(size_t)node->symbol - 1U];

                R_SEMANTIC_CHECK(node->borrow_origin == node->symbol);
                R_SEMANTIC_CHECK((symbol != NULL) && (symbol->borrow_origin == node->symbol));
                fresh_bindings += 1U;
            } else if ((local_type != NULL) && (local_type->kind == R_SEMANTIC_TYPE_BORROW)) {
                const RSemanticSymbol *symbol =
                    (node->symbol == R_SYMBOL_ID_INVALID) ||
                            ((size_t)node->symbol > context->semantic_symbol_count)
                        ? NULL
                        : &context->semantic_symbols[(size_t)node->symbol - 1U];

                R_SEMANTIC_CHECK(node->borrow_origin != R_SYMBOL_ID_INVALID);
                R_SEMANTIC_CHECK(node->borrow_origin != node->symbol);
                R_SEMANTIC_CHECK((symbol != NULL) &&
                                 (symbol->borrow_origin == node->borrow_origin));
                preserved_borrow_bindings += 1U;
            } else if ((local_type != NULL) &&
                       (local_type->kind == R_SEMANTIC_TYPE_CONSTEXPR_STR)) {
                const RSemanticSymbol *symbol =
                    (node->symbol == R_SYMBOL_ID_INVALID) ||
                            ((size_t)node->symbol > context->semantic_symbol_count)
                        ? NULL
                        : &context->semantic_symbols[(size_t)node->symbol - 1U];

                R_SEMANTIC_CHECK(node->borrow_origin == R_SYMBOL_ID_INVALID);
                R_SEMANTIC_CHECK((symbol != NULL) &&
                                 (symbol->borrow_origin == R_SYMBOL_ID_INVALID));
                program_storage_bindings += 1U;
            }
        }
    }
    R_SEMANTIC_CHECK(hir_shared == 2U);
    R_SEMANTIC_CHECK(hir_mutable == 1U);
    R_SEMANTIC_CHECK(hir_nullable_pointer == 2U);
    R_SEMANTIC_CHECK(fresh_bindings == 3U);
    R_SEMANTIC_CHECK(preserved_borrow_bindings == 1U);
    R_SEMANTIC_CHECK(program_storage_bindings == 1U);
    R_SEMANTIC_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
    for (index = 0U; index < context->mir_instruction_count; ++index) {
        const RMirInstruction *instruction = &context->mir_instructions[index];

        if ((instruction->kind == R_MIR_INSTRUCTION_STANDARD_CALL) &&
            ((instruction->standard_operation == R_STANDARD_CALL_CORE_SLICE_FROM_RAW_PARTS) ||
             (instruction->standard_operation == R_STANDARD_CALL_CORE_SLICE_FROM_RAW_PARTS_MUT))) {
            const RSemanticType *result_type = r_semantic_type(context, instruction->type);

            R_SEMANTIC_CHECK(instruction->operand_count == UINT32_C(2));
            R_SEMANTIC_CHECK(instruction->result != R_MIR_VALUE_ID_INVALID);
            R_SEMANTIC_CHECK((result_type != NULL) &&
                             (result_type->kind == R_SEMANTIC_TYPE_SLICE) &&
                             (result_type->base == instruction->auxiliary_type));
            if (instruction->standard_operation == R_STANDARD_CALL_CORE_SLICE_FROM_RAW_PARTS) {
                mir_shared += 1U;
            } else {
                mir_mutable += 1U;
            }
        }
    }
    R_SEMANTIC_CHECK(mir_shared == hir_shared);
    R_SEMANTIC_CHECK(mir_mutable == hir_mutable);
    r_frontend_destroy(context);

    for (index = 0U; index < (sizeof(negative_cases) / sizeof(negative_cases[0])); ++index) {
        const RDiagnostic *diagnostic;

        context = r_frontend_create(NULL);
        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        (void)r_semantic_add_bytes(context,
                                   "core-slice-from-raw-parts-negative.r",
                                   (const uint8_t *)negative_cases[index].source,
                                   strlen(negative_cases[index].source));
        R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
        R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 1U);
        diagnostic = r_frontend_diagnostic(context, UINT32_C(0));
        R_SEMANTIC_CHECK((diagnostic != NULL) &&
                         (strcmp(diagnostic->code, negative_cases[index].code) == 0) &&
                         (strcmp(diagnostic->rule_id, negative_cases[index].rule_id) == 0) &&
                         (diagnostic->phase == R_DIAGNOSTIC_PHASE_SEMANTIC));
        r_frontend_destroy(context);
    }

    context = r_frontend_create(NULL);
    R_SEMANTIC_CHECK(context != NULL);
    if (context != NULL) {
        (void)r_semantic_add_bytes(context,
                                   "core-slice-from-raw-parts-direct-return.r",
                                   (const uint8_t *)direct_return,
                                   strlen(direct_return));
        R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
        R_SEMANTIC_CHECK(
            r_semantic_has_diagnostic(context, "R-DIAG-TYPE-001", R_DIAGNOSTIC_PHASE_SEMANTIC));
        r_frontend_destroy(context);
    }

    context = r_frontend_create(NULL);
    R_SEMANTIC_CHECK(context != NULL);
    if (context != NULL) {
        (void)r_semantic_add_bytes(context,
                                   "core-slice-from-raw-parts-aggregate-escape.r",
                                   (const uint8_t *)aggregate_escape,
                                   strlen(aggregate_escape));
        R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
        R_SEMANTIC_CHECK(
            r_semantic_has_diagnostic(context, "R-DIAG-BORROW-002", R_DIAGNOSTIC_PHASE_SEMANTIC));
        r_frontend_destroy(context);
    }
}

/* R-UNSAFE-0008 (L38): an anchored raw-parts slice takes the region of its anchor, so it may be
   returned and stored as the anchor could, while the borrow rules keep the anchor alive, unmoved
   and, for the mutable form, otherwise unused as long as the slice is live. */
static void r_semantic_test_core_slice_from_raw_parts_in(void) {
    static const char positive_source[] =
        "module semantic.core_slice_from_raw_parts_in;\n"
        "struct buffer { atomic raw u8*? data; usize size; };\n"
        "struct holder { const u8[] bytes; };\n"
        "protected raw u8*? address_of(const buffer* source) {\n"
        "    return core::atomic_load(&source->data, core::memory_order::relaxed);\n"
        "}\n"
        "const u8[] buffer::bytes(const buffer* this) {\n"
        "    raw u8*? data = address_of(this);\n"
        "    unsafe { return core::slice_from_raw_parts_in(this, data as raw const u8*?, "
        "this->size); }\n"
        "}\n"
        "u8[] buffer::bytes_mut(buffer* this) {\n"
        "    raw u8*? data = address_of(this);\n"
        "    unsafe { return core::slice_from_raw_parts_in_mut(this, data, this->size); }\n"
        "}\n"
        "holder wrap(const buffer* source) { return holder {.bytes = source->bytes()}; }\n"
        "const u16[] as_words(const u8[] bytes) {\n"
        "    unsafe {\n"
        "        raw const u8* start = &bytes[0] as raw const u8*;\n"
        "        return core::slice_from_raw_parts_in(bytes, start as raw const void* as raw const "
        "u16*,\n"
        "                                             len(bytes) / 2usize);\n"
        "    }\n"
        "}\n"
        "const u8[] text_bytes(str text) {\n"
        "    const u8[] raw_bytes = text;\n"
        "    unsafe {\n"
        "        raw const u8* start = &raw_bytes[0] as raw const u8*;\n"
        "        return core::slice_from_raw_parts_in(text, start, len(raw_bytes));\n"
        "    }\n"
        "}\n"
        "usize local_view(buffer* source) {\n"
        "    raw u8*? data = address_of(source);\n"
        "    unsafe {\n"
        "        const u8[] view = core::slice_from_raw_parts_in(&*source, data as raw const u8*?, "
        "0usize);\n"
        "        return len(view);\n"
        "    }\n"
        "}\n";
    static const struct {
        const char *source;
        const char *code;
        const char *rule_id;
    } negative_cases[] = {
        {
            "module semantic.slice_in_outside;\n"
            "const u8[] bytes(const u8[] anchor, raw const u8* data) {\n"
            "    return core::slice_from_raw_parts_in(anchor, data, 1usize);\n"
            "}\n",
            "R-DIAG-UNSAFE-001",
            "R-UNSAFE-0001",
        },
        {
            "module semantic.slice_in_value_anchor;\n"
            "const u8[] bytes(i32 anchor, raw const u8* data) {\n"
            "    unsafe { return core::slice_from_raw_parts_in(anchor, data, 1usize); }\n"
            "}\n",
            "R-DIAG-TYPE-001",
            "R-UNSAFE-0008",
        },
        {
            "module semantic.slice_in_shared_anchor;\n"
            "u8[] bytes(const u8[] anchor, raw u8* data) {\n"
            "    unsafe { return core::slice_from_raw_parts_in_mut(anchor, data, 1usize); }\n"
            "}\n",
            "R-DIAG-TYPE-001",
            "R-UNSAFE-0008",
        },
        {
            "module semantic.slice_in_call_anchor;\n"
            "const u8[] pick(const u8[] anchor) { return anchor; }\n"
            "const u8[] bytes(const u8[] anchor, raw const u8* data) {\n"
            "    unsafe { return core::slice_from_raw_parts_in(pick(anchor), data, 1usize); }\n"
            "}\n",
            "R-DIAG-TYPE-001",
            "R-UNSAFE-0008",
        },
        {
            "module semantic.slice_in_index_anchor;\n"
            "const u8[] bytes(const u8[] anchor, raw const u8* data) {\n"
            "    unsafe { return core::slice_from_raw_parts_in(&anchor[0usize], data, 1usize); }\n"
            "}\n",
            "R-DIAG-TYPE-001",
            "R-UNSAFE-0008",
        },
        {
            "module semantic.slice_in_arity;\n"
            "const u8[] bytes(const u8[] anchor, raw const u8* data) {\n"
            "    unsafe { return core::slice_from_raw_parts_in(anchor, data); }\n"
            "}\n",
            "R-DIAG-TYPE-001",
            "R-UNSAFE-0008",
        },
        {
            "module semantic.slice_in_local_escape;\n"
            "struct buffer { usize size; };\n"
            "const u8[] bytes(raw const u8* data) {\n"
            "    buffer local = buffer {.size = 1usize};\n"
            "    unsafe { return core::slice_from_raw_parts_in(&local, data, 1usize); }\n"
            "}\n",
            "R-DIAG-BORROW-002",
            "R-BORROW-0007",
        },
        {
            "module semantic.slice_in_move;\n"
            "struct buffer { array<u8> keep; };\n"
            "u8 first(raw const u8* data) {\n"
            "    array<u8> none = [];\n"
            "    buffer local = buffer {.keep = move none};\n"
            "    unsafe {\n"
            "        const u8[] view = core::slice_from_raw_parts_in(&local, data, 1usize);\n"
            "        buffer moved = move local;\n"
            "        drop moved;\n"
            "        return view[0];\n"
            "    }\n"
            "}\n",
            "R-DIAG-BORROW-001",
            "R-OWN-0002",
        },
        {
            "module semantic.slice_in_drop;\n"
            "struct buffer { array<u8> keep; };\n"
            "u8 first(raw const u8* data) {\n"
            "    array<u8> none = [];\n"
            "    buffer local = buffer {.keep = move none};\n"
            "    unsafe {\n"
            "        const u8[] view = core::slice_from_raw_parts_in(&local, data, 1usize);\n"
            "        drop local;\n"
            "        return view[0];\n"
            "    }\n"
            "}\n",
            "R-DIAG-BORROW-001",
            "R-INIT-0011",
        },
        {
            "module semantic.slice_in_assign_shared;\n"
            "struct buffer { usize size; };\n"
            "u8 first(raw const u8* data) {\n"
            "    buffer local = buffer {.size = 1usize};\n"
            "    unsafe {\n"
            "        const u8[] view = core::slice_from_raw_parts_in(&local, data, 1usize);\n"
            "        local.size = 2usize;\n"
            "        return view[0];\n"
            "    }\n"
            "}\n",
            "R-DIAG-BORROW-001",
            "R-BORROW-0002",
        },
        {
            "module semantic.slice_in_read_exclusive;\n"
            "struct buffer { usize size; };\n"
            "usize first(raw u8* data) {\n"
            "    buffer local = buffer {.size = 1usize};\n"
            "    unsafe {\n"
            "        u8[] view = core::slice_from_raw_parts_in_mut(&local, data, 1usize);\n"
            "        usize size = local.size;\n"
            "        view[0] = 1u8;\n"
            "        return size;\n"
            "    }\n"
            "}\n",
            "R-DIAG-BORROW-001",
            "R-BORROW-0002",
        },
        {
            "module semantic.slice_in_method_conflict;\n"
            "struct buffer { atomic raw u8*? data; usize size; };\n"
            "u8[] buffer::bytes_mut(buffer* this) {\n"
            "    raw u8*? data = core::atomic_load(&this->data, core::memory_order::relaxed);\n"
            "    unsafe { return core::slice_from_raw_parts_in_mut(this, data, this->size); }\n"
            "}\n"
            "void buffer::reset(buffer* this) { this->size = 0usize; }\n"
            "u8 first(buffer* source) {\n"
            "    u8[] view = source->bytes_mut();\n"
            "    source->reset();\n"
            "    view[0] = 1u8;\n"
            "    return view[0];\n"
            "}\n",
            "R-DIAG-BORROW-001",
            "R-BORROW-0002",
        },
        {
            "module semantic.raw_cast_nullable_target;\n"
            "raw const u8* narrow(raw u8*? data) {\n"
            "    unsafe { return data as raw const u8*; }\n"
            "}\n",
            "R-DIAG-TYPE-001",
            "R-EXPR-0019",
        },
    };
    RFrontendContext *context = r_frontend_create(NULL);
    size_t anchored_shared = 0U;
    size_t anchored_mutable = 0U;
    size_t mir_anchored = 0U;
    size_t index;

    R_SEMANTIC_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    (void)r_semantic_add_bytes(context,
                               "core-slice-from-raw-parts-in-positive.r",
                               (const uint8_t *)positive_source,
                               strlen(positive_source));
    R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    for (index = 0U; index < context->hir_node_count; ++index) {
        const RHirNode *node = &context->hir_nodes[index];

        if ((node->kind == R_HIR_STANDARD_CALL) && (node->integer_value == UINT64_C(1)) &&
            ((node->standard_operation == R_STANDARD_CALL_CORE_SLICE_FROM_RAW_PARTS) ||
             (node->standard_operation == R_STANDARD_CALL_CORE_SLICE_FROM_RAW_PARTS_MUT))) {
            const RSymbolId origin = node->borrow_origin;
            const RSemanticSymbol *anchor =
                (origin == R_SYMBOL_ID_INVALID) || ((size_t)origin > context->semantic_symbol_count)
                    ? NULL
                    : &context->semantic_symbols[(size_t)origin - 1U];

            /* The pointer and the length are the only operands; the anchor supplies the
               origin, here a parameter or a local of the function. */
            R_SEMANTIC_CHECK(node->child_count == UINT32_C(2));
            R_SEMANTIC_CHECK(anchor != NULL);
            if (node->standard_operation == R_STANDARD_CALL_CORE_SLICE_FROM_RAW_PARTS) {
                anchored_shared += 1U;
            } else {
                anchored_mutable += 1U;
            }
        }
    }
    R_SEMANTIC_CHECK(anchored_shared == 4U);
    R_SEMANTIC_CHECK(anchored_mutable == 1U);
    for (index = 0U; index < context->semantic_symbol_count; ++index) {
        const RSemanticSymbol *symbol = &context->semantic_symbols[index];

        /* bytes and bytes_mut return what their receiver designates. */
        if ((symbol->kind == R_SEMANTIC_SYMBOL_FUNCTION) && symbol->has_definition &&
            (symbol->receiver_kind != R_SEMANTIC_RECEIVER_NONE)) {
            R_SEMANTIC_CHECK(symbol->return_borrow_parameter == UINT32_C(1));
        }
    }
    R_SEMANTIC_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
    for (index = 0U; index < context->mir_instruction_count; ++index) {
        const RMirInstruction *instruction = &context->mir_instructions[index];

        if ((instruction->kind == R_MIR_INSTRUCTION_STANDARD_CALL) &&
            (instruction->integer_value == UINT64_C(1)) &&
            ((instruction->standard_operation == R_STANDARD_CALL_CORE_SLICE_FROM_RAW_PARTS) ||
             (instruction->standard_operation == R_STANDARD_CALL_CORE_SLICE_FROM_RAW_PARTS_MUT))) {
            R_SEMANTIC_CHECK(instruction->operand_count == UINT32_C(2));
            mir_anchored += 1U;
        }
    }
    R_SEMANTIC_CHECK(mir_anchored == anchored_shared + anchored_mutable);
    r_frontend_destroy(context);

    for (index = 0U; index < (sizeof(negative_cases) / sizeof(negative_cases[0])); ++index) {
        const RDiagnostic *diagnostic;

        context = r_frontend_create(NULL);
        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        (void)r_semantic_add_bytes(context,
                                   "core-slice-from-raw-parts-in-negative.r",
                                   (const uint8_t *)negative_cases[index].source,
                                   strlen(negative_cases[index].source));
        R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
        R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 1U);
        diagnostic = r_frontend_diagnostic(context, UINT32_C(0));
        R_SEMANTIC_CHECK((diagnostic != NULL) &&
                         (strcmp(diagnostic->code, negative_cases[index].code) == 0) &&
                         (strcmp(diagnostic->rule_id, negative_cases[index].rule_id) == 0) &&
                         (diagnostic->phase == R_DIAGNOSTIC_PHASE_SEMANTIC));
        r_frontend_destroy(context);
    }
}

static void r_semantic_test_core_volatile(void) {
    static const char positive_source[] =
        "module semantic.core_volatile;\n"
        "i32 main() {\n"
        "    i32 value = 5;\n"
        "    unsafe {\n"
        "        raw i32* write_address = &value as raw i32*;\n"
        "        raw const i32* read_address = &value as raw const i32*;\n"
        "        core::volatile_store(write_address, 9);\n"
        "        i32 observed = core::volatile_load(read_address); observed as void;\n"
        "    }\n"
        "    return 0;\n"
        "}\n";
    static const struct {
        const char *source;
        const char *code;
        const char *rule_id;
    } negative_cases[] = {
        {
            "module semantic.volatile_outside;\n"
            "i32 read(raw const i32* address) {\n"
            "    i32 value = core::volatile_load(address);\n"
            "    return value;\n"
            "}\n"
            "i32 main() { return 0; }\n",
            "R-DIAG-UNSAFE-001",
            "R-UNSAFE-0001",
        },
        {
            "module semantic.volatile_load_mutable;\n"
            "i32 read(raw i32* address) {\n"
            "    unsafe { i32 value = core::volatile_load(address); }\n"
            "    return 0;\n"
            "}\n"
            "i32 main() { return 0; }\n",
            "R-DIAG-TYPE-001",
            "R-UNSAFE-0008",
        },
        {
            "module semantic.volatile_store_const;\n"
            "void write(raw const i32* address) {\n"
            "    unsafe { core::volatile_store(address, 1); }\n"
            "}\n"
            "i32 main() { return 0; }\n",
            "R-DIAG-TYPE-001",
            "R-UNSAFE-0008",
        },
        {
            "module semantic.volatile_nullable;\n"
            "i32 read(raw const i32*? address) {\n"
            "    unsafe { i32 value = core::volatile_load(address); }\n"
            "    return 0;\n"
            "}\n"
            "i32 main() { return 0; }\n",
            "R-DIAG-TYPE-001",
            "R-UNSAFE-0008",
        },
        {
            "module semantic.volatile_string;\n"
            "void read(raw const str* address) {\n"
            "    unsafe { str value = core::volatile_load(address); }\n"
            "}\n"
            "i32 main() { return 0; }\n",
            "R-DIAG-TYPE-001",
            "R-UNSAFE-0008",
        },
        {
            "module semantic.volatile_value_type;\n"
            "void write(raw i32* address) {\n"
            "    unsafe { core::volatile_store(address, 1u32); }\n"
            "}\n"
            "i32 main() { return 0; }\n",
            "R-DIAG-TYPE-001",
            "R-UNSAFE-0008",
        },
        {
            "module semantic.raw_cast_outside;\n"
            "i32 main() {\n"
            "    i32 value = 0;\n"
            "    raw i32* address = &value as raw i32*;\n"
            "    return 0;\n"
            "}\n",
            "R-DIAG-UNSAFE-001",
            "R-UNSAFE-0001",
        },
        {
            "module semantic.raw_cast_permission;\n"
            "i32 main() {\n"
            "    i32 value = 0;\n"
            "    const i32* shared = &value;\n"
            "    unsafe { raw i32* address = shared as raw i32*; }\n"
            "    return 0;\n"
            "}\n",
            "R-DIAG-TYPE-001",
            "R-EXPR-0019",
        },
    };
    RFrontendContext *context = r_frontend_create(NULL);
    size_t hir_loads = 0U;
    size_t hir_stores = 0U;
    size_t mir_loads = 0U;
    size_t mir_stores = 0U;
    size_t raw_addresses = 0U;
    size_t mir_borrows = 0U;
    size_t index;

    R_SEMANTIC_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    (void)r_semantic_add_bytes(context,
                               "core-volatile-positive.r",
                               (const uint8_t *)positive_source,
                               strlen(positive_source));
    R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    for (index = 0U; index < context->hir_node_count; ++index) {
        const RHirNode *node = &context->hir_nodes[index];

        if ((node->kind == R_HIR_STANDARD_CALL) &&
            ((node->standard_operation == R_STANDARD_CALL_CORE_VOLATILE_LOAD) ||
             (node->standard_operation == R_STANDARD_CALL_CORE_VOLATILE_STORE))) {
            const RSemanticType *pointee = r_semantic_type(context, node->auxiliary_type);

            R_SEMANTIC_CHECK((pointee != NULL) && (pointee->kind == R_SEMANTIC_TYPE_I32));
            if (node->standard_operation == R_STANDARD_CALL_CORE_VOLATILE_LOAD) {
                R_SEMANTIC_CHECK(node->child_count == UINT32_C(1));
                R_SEMANTIC_CHECK(node->type == node->auxiliary_type);
                hir_loads += 1U;
            } else {
                const RSemanticType *result_type = r_semantic_type(context, node->type);

                R_SEMANTIC_CHECK(node->child_count == UINT32_C(2));
                R_SEMANTIC_CHECK((result_type != NULL) &&
                                 (result_type->kind == R_SEMANTIC_TYPE_VOID));
                hir_stores += 1U;
            }
        }
    }
    R_SEMANTIC_CHECK(hir_loads == 1U);
    R_SEMANTIC_CHECK(hir_stores == 1U);
    R_SEMANTIC_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
    for (index = 0U; index < context->mir_instruction_count; ++index) {
        const RMirInstruction *instruction = &context->mir_instructions[index];

        if (instruction->kind == R_MIR_INSTRUCTION_RAW_ADDRESS) {
            const RSemanticType *pointer = r_semantic_type(context, instruction->type);

            R_SEMANTIC_CHECK((pointer != NULL) && (pointer->kind == R_SEMANTIC_TYPE_RAW));
            raw_addresses += 1U;
        } else if (instruction->kind == R_MIR_INSTRUCTION_BORROW) {
            mir_borrows += 1U;
        } else if ((instruction->kind == R_MIR_INSTRUCTION_STANDARD_CALL) &&
                   (instruction->standard_operation == R_STANDARD_CALL_CORE_VOLATILE_LOAD)) {
            R_SEMANTIC_CHECK(instruction->operand_count == UINT32_C(1));
            R_SEMANTIC_CHECK(instruction->result != R_MIR_VALUE_ID_INVALID);
            mir_loads += 1U;
        } else if ((instruction->kind == R_MIR_INSTRUCTION_STANDARD_CALL) &&
                   (instruction->standard_operation == R_STANDARD_CALL_CORE_VOLATILE_STORE)) {
            R_SEMANTIC_CHECK(instruction->operand_count == UINT32_C(2));
            R_SEMANTIC_CHECK(instruction->result == R_MIR_VALUE_ID_INVALID);
            mir_stores += 1U;
        }
    }
    R_SEMANTIC_CHECK(raw_addresses == 2U);
    R_SEMANTIC_CHECK(mir_borrows == 0U);
    R_SEMANTIC_CHECK(mir_loads == hir_loads);
    R_SEMANTIC_CHECK(mir_stores == hir_stores);
    r_frontend_destroy(context);

    for (index = 0U; index < (sizeof(negative_cases) / sizeof(negative_cases[0])); ++index) {
        const RDiagnostic *diagnostic;

        context = r_frontend_create(NULL);
        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        (void)r_semantic_add_bytes(context,
                                   "core-volatile-negative.r",
                                   (const uint8_t *)negative_cases[index].source,
                                   strlen(negative_cases[index].source));
        R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
        R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 1U);
        diagnostic = r_frontend_diagnostic(context, UINT32_C(0));
        R_SEMANTIC_CHECK((diagnostic != NULL) &&
                         (strcmp(diagnostic->code, negative_cases[index].code) == 0) &&
                         (strcmp(diagnostic->rule_id, negative_cases[index].rule_id) == 0) &&
                         (diagnostic->phase == R_DIAGNOSTIC_PHASE_SEMANTIC));
        r_frontend_destroy(context);
    }
}

static void r_semantic_test_core_adopt_release(void) {
    static const char positive_source[] =
        "module semantic.core_adopt_release;\n"
        "struct Pair { i32 first; i32 second; };\n"
        "void round_trip(raw Pair* pointer) {\n"
        "    unsafe {\n"
        "        own Pair* owner = core::adopt(pointer);\n"
        "        raw Pair* returned = core::release(move owner); returned as void;\n"
        "    }\n"
        "}\n"
        "void transfer_to_owner(raw Pair* pointer) {\n"
        "    unsafe { own Pair* owner = core::adopt(pointer); drop owner; }\n"
        "}\n"
        "i32 main() { return 0; }\n";
    static const struct {
        const char *source;
        const char *code;
        const char *rule_id;
    } negative_cases[] = {
        {
            "module semantic.adopt_outside;\n"
            "void take(raw i32* pointer) { own i32* owner = core::adopt(pointer); }\n",
            "R-DIAG-UNSAFE-001",
            "R-UNSAFE-0001",
        },
        {
            "module semantic.release_outside;\n"
            "void give(own i32* owner) { raw i32* pointer = core::release(move owner); }\n",
            "R-DIAG-UNSAFE-001",
            "R-UNSAFE-0001",
        },
        {
            "module semantic.adopt_shared;\n"
            "void take(raw const i32* pointer) {\n"
            "    unsafe { own i32* owner = core::adopt(pointer); }\n"
            "}\n",
            "R-DIAG-TYPE-001",
            "R-UNSAFE-0008",
        },
        {
            "module semantic.adopt_nullable;\n"
            "void take(raw i32*? pointer) {\n"
            "    unsafe { own i32* owner = core::adopt(pointer); }\n"
            "}\n",
            "R-DIAG-TYPE-001",
            "R-UNSAFE-0008",
        },
        {
            "module semantic.release_nullable;\n"
            "void give(own i32*? owner) {\n"
            "    unsafe { raw i32* pointer = core::release(move owner); }\n"
            "}\n",
            "R-DIAG-TYPE-001",
            "R-UNSAFE-0008",
        },
        {
            "module semantic.release_raw;\n"
            "void give(raw i32* owner) {\n"
            "    unsafe { raw i32* pointer = core::release(owner); }\n"
            "}\n",
            "R-DIAG-TYPE-001",
            "R-UNSAFE-0008",
        },
        {
            "module semantic.adopt_arity;\n"
            "void take(raw i32* pointer) {\n"
            "    unsafe { own i32* owner = core::adopt(pointer, pointer); }\n"
            "}\n",
            "R-DIAG-TYPE-001",
            "R-UNSAFE-0008",
        },
        {
            "module semantic.release_missing_move;\n"
            "void give(own i32* owner) {\n"
            "    unsafe { raw i32* pointer = core::release(owner); }\n"
            "}\n",
            "R-DIAG-MOVE-001",
            "R-OWN-0003",
        },
        {
            "module semantic.release_twice;\n"
            "void give(own i32* owner) {\n"
            "    unsafe {\n"
            "        raw i32* first = core::release(move owner);\n"
            "        raw i32* second = core::release(move owner);\n"
            "    }\n"
            "}\n",
            "R-DIAG-MOVE-002",
            "R-OWN-0004",
        },
    };
    RFrontendContext *context = r_frontend_create(NULL);
    size_t hir_adopt = 0U;
    size_t hir_release = 0U;
    size_t hir_move = 0U;
    size_t hir_drop = 0U;
    size_t mir_adopt = 0U;
    size_t mir_release = 0U;
    size_t mir_move = 0U;
    size_t mir_drop = 0U;
    size_t index;

    R_SEMANTIC_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    (void)r_semantic_add_bytes(context,
                               "core-adopt-release-positive.r",
                               (const uint8_t *)positive_source,
                               strlen(positive_source));
    R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 0U);
    for (index = 0U; index < context->hir_node_count; ++index) {
        const RHirNode *node = &context->hir_nodes[index];

        if ((node->kind == R_HIR_STANDARD_CALL) &&
            ((node->standard_operation == R_STANDARD_CALL_CORE_ADOPT) ||
             (node->standard_operation == R_STANDARD_CALL_CORE_RELEASE))) {
            const bool is_adopt = node->standard_operation == R_STANDARD_CALL_CORE_ADOPT;
            const RSemanticType *result_type = r_semantic_type(context, node->type);
            const RSemanticType *pointee = r_semantic_type(context, node->auxiliary_type);
            const RHirNodeId argument_id = context->hir_children[node->first_child];
            const RHirNode *argument = &context->hir_nodes[(size_t)argument_id - 1U];
            const RSemanticType *argument_type = r_semantic_type(context, argument->type);

            R_SEMANTIC_CHECK(node->child_count == UINT32_C(1));
            R_SEMANTIC_CHECK((pointee != NULL) && (pointee->kind == R_SEMANTIC_TYPE_STRUCT));
            R_SEMANTIC_CHECK(
                (result_type != NULL) &&
                (result_type->kind == (is_adopt ? R_SEMANTIC_TYPE_OWN : R_SEMANTIC_TYPE_RAW)) &&
                (result_type->base == node->auxiliary_type) &&
                (result_type->flags == R_SEMANTIC_TYPE_FLAG_NONE));
            R_SEMANTIC_CHECK(
                (argument_type != NULL) &&
                (argument_type->kind == (is_adopt ? R_SEMANTIC_TYPE_RAW : R_SEMANTIC_TYPE_OWN)) &&
                (argument_type->base == node->auxiliary_type) &&
                (argument_type->flags == R_SEMANTIC_TYPE_FLAG_NONE));
            if (is_adopt) {
                hir_adopt += 1U;
            } else {
                R_SEMANTIC_CHECK(argument->kind == R_HIR_MOVE);
                hir_release += 1U;
            }
        } else if (node->kind == R_HIR_MOVE) {
            hir_move += 1U;
        } else if (node->kind == R_HIR_DROP) {
            const RHirNodeId place_id = context->hir_children[node->first_child];
            const RHirNode *place = &context->hir_nodes[(size_t)place_id - 1U];
            const RSemanticType *place_type = r_semantic_type(context, place->type);

            R_SEMANTIC_CHECK(node->child_count == UINT32_C(1));
            R_SEMANTIC_CHECK((place_type != NULL) && (place_type->kind == R_SEMANTIC_TYPE_OWN));
            hir_drop += 1U;
        }
    }
    R_SEMANTIC_CHECK(hir_adopt == 2U);
    R_SEMANTIC_CHECK(hir_release == 1U);
    R_SEMANTIC_CHECK(hir_move == 1U);
    R_SEMANTIC_CHECK(hir_drop == 1U);
    R_SEMANTIC_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
    for (index = 0U; index < context->mir_instruction_count; ++index) {
        const RMirInstruction *instruction = &context->mir_instructions[index];

        if ((instruction->kind == R_MIR_INSTRUCTION_STANDARD_CALL) &&
            ((instruction->standard_operation == R_STANDARD_CALL_CORE_ADOPT) ||
             (instruction->standard_operation == R_STANDARD_CALL_CORE_RELEASE))) {
            const bool is_adopt = instruction->standard_operation == R_STANDARD_CALL_CORE_ADOPT;
            const RSemanticType *result_type = r_semantic_type(context, instruction->type);

            R_SEMANTIC_CHECK(instruction->operand_count == UINT32_C(1));
            R_SEMANTIC_CHECK(instruction->result != R_MIR_VALUE_ID_INVALID);
            R_SEMANTIC_CHECK(
                (result_type != NULL) &&
                (result_type->kind == (is_adopt ? R_SEMANTIC_TYPE_OWN : R_SEMANTIC_TYPE_RAW)) &&
                (result_type->base == instruction->auxiliary_type));
            if (is_adopt) {
                mir_adopt += 1U;
            } else {
                mir_release += 1U;
            }
        } else if (instruction->kind == R_MIR_INSTRUCTION_MOVE) {
            const RSemanticType *move_type = r_semantic_type(context, instruction->type);

            R_SEMANTIC_CHECK((move_type != NULL) && (move_type->kind == R_SEMANTIC_TYPE_OWN));
            mir_move += 1U;
        } else if (instruction->kind == R_MIR_INSTRUCTION_DROP) {
            mir_drop += 1U;
        }
    }
    R_SEMANTIC_CHECK(mir_adopt == hir_adopt);
    R_SEMANTIC_CHECK(mir_release == hir_release);
    R_SEMANTIC_CHECK(mir_move == hir_move);
    R_SEMANTIC_CHECK(mir_drop == hir_drop);
    r_frontend_destroy(context);

    for (index = 0U; index < (sizeof(negative_cases) / sizeof(negative_cases[0])); ++index) {
        const RDiagnostic *diagnostic;

        context = r_frontend_create(NULL);
        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        (void)r_semantic_add_bytes(context,
                                   "core-adopt-release-negative.r",
                                   (const uint8_t *)negative_cases[index].source,
                                   strlen(negative_cases[index].source));
        R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
        R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 1U);
        diagnostic = r_frontend_diagnostic(context, UINT32_C(0));
        R_SEMANTIC_CHECK((diagnostic != NULL) &&
                         (strcmp(diagnostic->code, negative_cases[index].code) == 0) &&
                         (strcmp(diagnostic->rule_id, negative_cases[index].rule_id) == 0) &&
                         (diagnostic->phase == R_DIAGNOSTIC_PHASE_SEMANTIC));
        r_frontend_destroy(context);
    }
}

static void r_semantic_test_raw_c_functions(void) {
    static const struct {
        const char *name;
        const char *source;
        const char *code;
        bool mir_liveness;
    } cases[] = {
        {"r-scalar-parameter",
         "void probe(raw fn(i32) -> void callback) {}\n",
         "R-DIAG-FFI-005",
         false},
        {"r-scalar-result", "void probe(raw fn() -> bool callback) {}\n", "R-DIAG-FFI-005", false},
        {"const-function-result",
         "void probe(raw fn() -> const c_int callback) {}\n",
         "R-DIAG-TYPE-001",
         false},
        {"conditional-prototype-mismatch",
         "void probe(bool pick, raw fn(c_int) -> c_int left, raw fn(c_uint) -> c_int right) {\n"
         "    raw fn?(c_int) -> c_int value = pick == true ? left : right;\n"
         "}\n",
         "R-DIAG-TYPE-001",
         false},
        {"borrow-parameter",
         "void probe(raw fn(const c_int*) -> void callback) {}\n",
         "R-DIAG-FFI-005",
         false},
        {"nested-owner",
         "void probe(raw fn(raw (own c_int*)*) -> void callback) {}\n",
         "R-DIAG-FFI-005",
         false},
        {"ordinary-r-function",
         "c_int ordinary(c_int value) { return value; }\n"
         "void probe() { raw fn(c_int) -> c_int callback = ordinary; }\n",
         "R-DIAG-FFI-005",
         false},
        {"missing-callback-attribute",
         "@safety(\"TEST-CALLBACK\", \"The runtime is initialized\")\n"
         "extern \"C\" c_int callback(c_int value) { return value; }\n"
         "void probe() { raw fn(c_int) -> c_int pointer = callback; }\n",
         "R-DIAG-FFI-005",
         false},
        {"callback-parameter-mismatch",
         "@callback @safety(\"TEST-CALLBACK\", \"The runtime is initialized\")\n"
         "extern \"C\" c_int callback(c_int value) { return value; }\n"
         "void probe() { raw fn(c_uint) -> c_int pointer = callback; }\n",
         "R-DIAG-FFI-005",
         false},
        {"callback-result-mismatch",
         "@callback @safety(\"TEST-CALLBACK\", \"The runtime is initialized\")\n"
         "extern \"C\" c_int callback(c_int value) { return value; }\n"
         "void probe() { raw fn(c_int) -> c_uint pointer = callback; }\n",
         "R-DIAG-FFI-005",
         false},
        {"callback-pointer-const-mismatch",
         "@callback @safety(\"TEST-CALLBACK\", \"The runtime is initialized\")\n"
         "extern \"C\" void callback(raw const void* value) {}\n"
         "void probe() { raw fn(raw void*) -> void pointer = callback; }\n",
         "R-DIAG-FFI-005",
         false},
        {"callback-arity-mismatch",
         "@callback @safety(\"TEST-CALLBACK\", \"The runtime is initialized\")\n"
         "extern \"C\" c_int callback(c_int value) { return value; }\n"
         "void probe() { raw fn() -> c_int pointer = callback; }\n",
         "R-DIAG-FFI-005",
         false},
        {"unsafe-indirect-call",
         "void probe(raw fn(c_int) -> c_int callback) { c_int result = callback(1i32 as c_int); "
         "}\n",
         "R-DIAG-UNSAFE-001",
         false},
        {"unsafe-nullable-call",
         "void probe(raw fn?(c_int) -> c_int callback) { c_int result = callback(1i32 as c_int); "
         "}\n",
         "R-DIAG-UNSAFE-001",
         false},
        {"indirect-call-arity",
         "void probe(raw fn(c_int) -> c_int callback) { unsafe { callback(); } }\n",
         "R-DIAG-FFI-005",
         false},
        {"indirect-call-exact-argument",
         "void probe(raw fn(c_int) -> c_int callback) { unsafe { callback(1i32); } }\n",
         "R-DIAG-FFI-005",
         false},
        {"null-nonnullable-pointer",
         "void probe() { raw fn() -> void callback = null; }\n",
         "R-DIAG-TYPE-001",
         false},
        {"nullable-to-nonnullable",
         "void probe(raw fn?() -> void nullable) { raw fn() -> void callback = nullable; }\n",
         "R-DIAG-FFI-005",
         false},
        {"async-function-pointer-parameter",
         "async void probe(raw fn() -> void callback) {}\n",
         "R-DIAG-ASYNC-001",
         false},
        {"async-function-pointer-result",
         "async raw fn?() -> void probe() { return null; }\n",
         "R-DIAG-ASYNC-001",
         false},
        {"async-handle-parameter",
         "async void probe(std.c::handle handle) {}\n",
         "R-DIAG-ASYNC-001",
         false},
        {"async-function-pointer-live-across-await",
         "async void step() {}\n"
         "async void probe() throws std.async::start_error {\n"
         "    raw fn?() -> void callback = null;\n"
         "    await step();\n"
         "    callback as void;\n"
         "}\n",
         "R-DIAG-ASYNC-001",
         true},
        {"async-aggregate-function-pointer-live-across-await",
         "struct State { raw fn?() -> void callback; };\n"
         "async void step() {}\n"
         "async void probe() throws std.async::start_error {\n"
         "    State state = State { .callback = null };\n"
         "    await step();\n"
         "    state.callback as void;\n"
         "}\n",
         "R-DIAG-ASYNC-001",
         true},
        {"adopt-handle-unsafe",
         "void probe(raw void* pointer, raw fn(raw void*) -> void destroy) {\n"
         "    std.c::handle handle = std.c::adopt_handle(pointer, destroy);\n"
         "}\n",
         "R-DIAG-UNSAFE-001",
         false},
        {"adopt-handle-nullable-resource",
         "void probe(raw void*? pointer, raw fn(raw void*) -> void destroy) {\n"
         "    unsafe { std.c::handle handle = std.c::adopt_handle(pointer, destroy); }\n"
         "}\n",
         "R-DIAG-TYPE-001",
         false},
        {"adopt-handle-nullable-destructor",
         "void probe(raw void* pointer, raw fn?(raw void*) -> void destroy) {\n"
         "    unsafe { std.c::handle handle = std.c::adopt_handle(pointer, destroy); }\n"
         "}\n",
         "R-DIAG-FFI-005",
         false},
        {"adopt-handle-wrong-destructor",
         "void probe(raw void* pointer, raw fn(raw void*) -> c_int destroy) {\n"
         "    unsafe { std.c::handle handle = std.c::adopt_handle(pointer, destroy); }\n"
         "}\n",
         "R-DIAG-FFI-005",
         false},
        {"adopt-handle-wrong-arity",
         "void probe(raw void* pointer) {\n"
         "    unsafe { std.c::handle handle = std.c::adopt_handle(pointer); }\n"
         "}\n",
         "R-DIAG-TYPE-001",
         false},
        {"checked-c-export",
         "error Error { Failed, };\n"
         "@callback @safety(\"TEST-CALLBACK\", \"The runtime is initialized\")\n"
         "extern \"C\" void callback() throws Error { throw Error::Failed; }\n",
         "R-DIAG-FFI-001",
         false},
        {"duplicate-decoded-export-name",
         "@callback @export_name(\"r_negative_increment\")\n"
         "@safety(\"TEST-FIRST-CALLBACK\", \"The runtime is initialized\")\n"
         "extern \"C\" void first() {}\n"
         "@callback @export_name(\"r_negative_\\x69ncrement\")\n"
         "@safety(\"TEST-SECOND-CALLBACK\", \"The runtime is initialized\")\n"
         "extern \"C\" void second() {}\n",
         "R-DIAG-FFI-003",
         false},
        {"reserved-library-export-name",
         "@export_name(\"malloc\")\n"
         "@safety(\"TEST-RESERVED-LIBRARY\", \"The runtime is initialized\")\n"
         "extern \"C\" void callback() {}\n",
         "R-DIAG-FFI-003",
         false},
        {"reserved-future-library-export-name",
         "@export_name(\"strcustom\")\n"
         "@safety(\"TEST-RESERVED-FUTURE-LIBRARY\", \"The runtime is initialized\")\n"
         "extern \"C\" void callback() {}\n",
         "R-DIAG-FFI-003",
         false},
        {"reserved-runtime-export-name",
         "@export_name(\"r_runtime_panic\")\n"
         "@safety(\"TEST-RESERVED-RUNTIME\", \"The runtime is initialized\")\n"
         "extern \"C\" void callback() {}\n",
         "R-DIAG-FFI-003",
         false},
    };
    size_t index;

    for (index = 0U; index < (sizeof(cases) / sizeof(cases[0])); ++index) {
        RFrontendContext *context = r_frontend_create(NULL);
        const char prefix[] = "module semantic.raw_c_functions;\n";
        char source[4096];
        int source_length;

        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        source_length = snprintf(source, sizeof(source), "%s%s", prefix, cases[index].source);
        R_SEMANTIC_CHECK((source_length >= 0) && ((size_t)source_length < sizeof(source)));
        if ((source_length < 0) || ((size_t)source_length >= sizeof(source))) {
            r_frontend_destroy(context);
            continue;
        }
        (void)r_semantic_add_bytes(
            context, cases[index].name, (const uint8_t *)source, (size_t)source_length);
        if (cases[index].mir_liveness) {
            R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
            R_SEMANTIC_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_NOT_LOWERABLE);
        } else {
            R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
        }
        if (!r_semantic_has_diagnostic(context, cases[index].code, R_DIAGNOSTIC_PHASE_SEMANTIC)) {
            (void)fprintf(stderr, "raw C function case failed: %s\n", cases[index].name);
        }
        R_SEMANTIC_CHECK(
            r_semantic_has_diagnostic(context, cases[index].code, R_DIAGNOSTIC_PHASE_SEMANTIC));
        r_frontend_destroy(context);
    }
}

static void r_semantic_test_standard_type_registry(void) {
    static const struct {
        const char *name;
        const char *source;
        const char *code;
        const char *rule_id;
    } cases[] = {
        {
            "unknown-standard-type",
            "module semantic.unknown_standard_type;\n"
            "void probe() { unsafe { raw std.missing::type*? value = null; } }\n",
            "R-DIAG-NAME-001",
            "R-NAME-0003",
        },
        {
            "missing-standard-type-argument",
            "module semantic.missing_standard_type_argument;\n"
            "void probe(std.sync::sender value) {}\n",
            "R-DIAG-TYPE-001",
            "R-TYPE-0012",
        },
    };
    size_t index;

    for (index = 0U; index < (sizeof(cases) / sizeof(cases[0])); ++index) {
        RFrontendContext *context = r_frontend_create(NULL);

        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        (void)r_semantic_add_bytes(context,
                                   cases[index].name,
                                   (const uint8_t *)cases[index].source,
                                   strlen(cases[index].source));
        R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
        R_SEMANTIC_CHECK(
            r_semantic_has_diagnostic(context, cases[index].code, R_DIAGNOSTIC_PHASE_SEMANTIC));
        {
            uint32_t diagnostic_index;
            bool found = false;

            for (diagnostic_index = 0U; diagnostic_index < r_frontend_diagnostic_count(context);
                 ++diagnostic_index) {
                const RDiagnostic *diagnostic = r_frontend_diagnostic(context, diagnostic_index);

                if ((diagnostic != NULL) && (strcmp(diagnostic->code, cases[index].code) == 0) &&
                    (strcmp(diagnostic->rule_id, cases[index].rule_id) == 0)) {
                    found = true;
                    break;
                }
            }
            R_SEMANTIC_CHECK(found);
        }
        r_frontend_destroy(context);
    }
}

static void r_semantic_test_std_c_thread_attachment(void) {
    static const struct {
        const char *source;
        const char *code;
        const char *rule_id;
    } cases[] = {
        {
            "module semantic.c_attach_unsafe;\n"
            "void probe() throws std.c::runtime_error {\n"
            "    std.c::thread_attachment value = std.c::attach_thread();\n"
            "    std.c::detach_thread(move value);\n"
            "}\n",
            "R-DIAG-UNSAFE-001",
            "R-UNSAFE-0001",
        },
        {
            "module semantic.c_attach_unhandled;\n"
            "void probe() {\n"
            "    unsafe {\n"
            "        std.c::thread_attachment value = std.c::attach_thread();\n"
            "        std.c::detach_thread(move value);\n"
            "    }\n"
            "}\n",
            "R-DIAG-EFFECT-001",
            "R-ERR-0001",
        },
        {
            "module semantic.c_attach_arity;\n"
            "void probe() throws std.c::runtime_error {\n"
            "    unsafe { std.c::thread_attachment value = std.c::attach_thread(1i32); }\n"
            "}\n",
            "R-DIAG-TYPE-001",
            "R-SLIB-C-0004",
        },
        {
            "module semantic.c_detach_move;\n"
            "void probe(std.c::thread_attachment value) {\n"
            "    std.c::detach_thread(value);\n"
            "}\n",
            "R-DIAG-MOVE-001",
            "R-OWN-0003",
        },
        {
            "module semantic.c_detach_type;\n"
            "void probe() { std.c::detach_thread(1i32); }\n",
            "R-DIAG-TYPE-001",
            "R-SLIB-C-0004",
        },
        {
            "module semantic.c_handle_pointer_type;\n"
            "raw void* probe(std.c::handle value) {\n"
            "    return std.c::handle_pointer(move value);\n"
            "}\n",
            "R-DIAG-TYPE-001",
            "R-SLIB-C-0003",
        },
        {
            "module semantic.c_handle_release_move;\n"
            "raw void* probe(std.c::handle value) {\n"
            "    return std.c::release_handle(value);\n"
            "}\n",
            "R-DIAG-MOVE-001",
            "R-OWN-0003",
        },
    };
    size_t index;

    for (index = 0U; index < (sizeof(cases) / sizeof(cases[0])); ++index) {
        RFrontendContext *context = r_frontend_create(NULL);
        const RDiagnostic *diagnostic;

        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        (void)r_semantic_add_bytes(context,
                                   "std-c-thread-attachment-negative.r",
                                   (const uint8_t *)cases[index].source,
                                   strlen(cases[index].source));
        R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
        R_SEMANTIC_CHECK(r_frontend_diagnostic_count(context) == 1U);
        diagnostic = r_frontend_diagnostic(context, UINT32_C(0));
        R_SEMANTIC_CHECK((diagnostic != NULL) &&
                         (strcmp(diagnostic->code, cases[index].code) == 0) &&
                         (strcmp(diagnostic->rule_id, cases[index].rule_id) == 0) &&
                         (diagnostic->phase == R_DIAGNOSTIC_PHASE_SEMANTIC));
        r_frontend_destroy(context);
    }
}

static void r_semantic_test_implicit_main_errors(void) {
    static const char *const allowed[] = {
#define R_MAIN_ERROR(name, domain, member, base, native) name,
#include "../compiler/semantic/main_errors.def"
#undef R_MAIN_ERROR
    };
    static const char *const excluded[] = {"std.alloc::new_error<i32>",
                                           "std.array::push_error<i32>",
                                           "std.dict::insert_error<i32, i32>",
                                           "std.list::push_error<i32>",
                                           "UserError",
                                           "std.json::error"};
    for (size_t group = 0U; group < 2U; ++group) {
        const size_t count = group == 0U ? sizeof(allowed) / sizeof(allowed[0])
                                         : sizeof(excluded) / sizeof(excluded[0]);
        for (size_t index = 0U; index < count; ++index) {
            for (unsigned form = 0U; form < 4U; ++form) {
                char source[1024];
                const char *type = group == 0U ? allowed[index] : excluded[index];
                const int length =
                    snprintf(source,
                             sizeof(source),
                             "module semantic.main_effects; error UserError {i32 code;}; "
                             "void fail() throws %s; %si32 main(%s) { fail(); return 0; }",
                             type,
                             form >= 2U ? "async " : "",
                             form % 2U ? "const str[] args" : "");
                RFrontendContext *context = r_frontend_create(NULL);
                R_SEMANTIC_CHECK(context != NULL && length > 0 && (size_t)length < sizeof(source));
                if (context == NULL)
                    continue;
                (void)r_semantic_add_bytes(
                    context, "main-effects.r", (const uint8_t *)source, (size_t)length);
                const RFrontendStatus status = r_frontend_analyze(context);
                if (status != (group == 0U ? R_FRONTEND_OK : R_FRONTEND_INVALID_SOURCE))
                    (void)fprintf(stderr, "implicit main error case: %s form=%u\n", type, form);
                R_SEMANTIC_CHECK(status ==
                                 (group == 0U ? R_FRONTEND_OK : R_FRONTEND_INVALID_SOURCE));
                if (group != 0U)
                    R_SEMANTIC_CHECK(r_semantic_has_diagnostic(
                        context, "R-DIAG-EFFECT-001", R_DIAGNOSTIC_PHASE_SEMANTIC));
                r_frontend_destroy(context);
            }
        }
    }
}

int main(int argc, char **argv) {
    if ((argc != 2) || ((strcmp(argv[1], "--unit") != 0) && (strcmp(argv[1], "--golden") != 0) &&
                        (strcmp(argv[1], "--body-golden") != 0))) {
        (void)fprintf(stderr, "usage: semantic_tests --unit|--golden|--body-golden\n");
        return 2;
    }
    if (strcmp(argv[1], "--golden") == 0) {
        r_semantic_test_golden();
    } else if (strcmp(argv[1], "--body-golden") == 0) {
        r_semantic_test_body_golden();
    } else {
        r_semantic_test_implicit_main_errors();
        r_semantic_test_atomic_canonicalization();
        r_semantic_test_float_literals();
        r_semantic_test_diagnostics_and_terminal_status();
        r_semantic_test_atomic_signature_compatibility();
        r_semantic_test_symbol_growth();
        r_semantic_test_signature_names();
        r_semantic_test_modifier_compatibility();
        r_semantic_test_checked_error_type();
        r_semantic_test_unheaded_throw_error_set_arity();
        r_semantic_test_error_declarations();
        r_semantic_test_error_imports();
        r_semantic_test_conditional_expression();
        r_semantic_test_conditional_throw();
        r_semantic_test_format();
        r_semantic_test_json();
        r_semantic_test_await_calls();
        r_semantic_test_async_hir();
        r_semantic_test_implicit_final_return();
        r_semantic_test_async_cancel();
        r_semantic_test_effectful_task_resolution();
        r_semantic_test_mir_pending_completion();
        r_semantic_test_pending_cleanup_oom_sweep();
        r_semantic_test_effectful_thread_handle_resolution();
        r_semantic_test_async_stderr();
        r_semantic_test_stdout();
        r_semantic_test_stdin();
        r_semantic_test_async_io_read();
        r_semantic_test_async_io_write_all();
        r_semantic_test_standard_outcome_schemas();
        r_semantic_test_async_copy_arguments();
        r_semantic_test_async_diagnostics();
        r_semantic_test_poisoned_async_start_recovery();
        r_semantic_test_async_move_argument_rollback();
        r_semantic_test_async_move_switch_branch_merge();
        r_semantic_test_named_move_abi_and_async_arc();
        r_semantic_test_send_sync_async_frames();
        r_semantic_test_shared_owner_await_liveness();
        r_semantic_test_string_views_and_await_liveness();
        r_semantic_test_sync_main_arguments();
        r_semantic_test_associated_names_do_not_collide();
        r_semantic_test_invalid_ast_is_internal_error();
        r_semantic_test_empty_analyze_does_not_seal();
        r_semantic_test_body_semantics();
        r_semantic_test_oom_sweep();
        r_semantic_test_late_type_oom_sweep();
        r_semantic_test_switch_type_growth_stability();
        r_semantic_test_body_oom_sweep();
        r_semantic_test_static_oom_sweep();
        r_semantic_test_cross_module_bindings();
        r_semantic_test_cross_module_binding_diagnostics();
        r_semantic_test_borrow_signature_boundary();
        r_semantic_test_return_borrow_diagnostics();
        r_semantic_test_borrow_component_contracts();
        r_semantic_test_extended_trait_contracts();
        r_semantic_test_opaque_result_contracts();
        r_semantic_test_match_expressions();
        r_semantic_test_scoped_tasks();
        r_semantic_test_scoped_select();
        r_semantic_test_l6_regressions();
        r_semantic_test_l10_regressions();
        r_semantic_test_l11_regressions();
        r_semantic_test_l12_regressions();
        r_semantic_test_l13_regressions();
        r_semantic_test_l14_regressions();
        r_semantic_test_l15_regressions();
        r_semantic_test_l16_regressions();
        r_semantic_test_l17_regressions();
        r_semantic_test_l18_regressions();
        r_semantic_test_l19_regressions();
        r_semantic_test_l20_regressions();
        r_semantic_test_l21_regressions();
        r_semantic_test_l22_regressions();
        r_semantic_test_l23_regressions();
        r_semantic_test_l24_regressions();
        r_semantic_test_l25_regressions();
        r_semantic_test_l25_relays();
        r_semantic_test_l26_regressions();
        r_semantic_test_l27_regressions();
        r_semantic_test_l28_regressions();
        r_semantic_test_l29_regressions();
        r_semantic_test_l30_regressions();
        r_semantic_test_l31_regressions();
        r_semantic_test_l32_regressions();
        r_semantic_test_l33_regressions();
        r_semantic_test_m18_regressions();
        r_semantic_test_m19_regressions();
        r_semantic_test_m20_regressions();
        r_semantic_test_m21_regressions();
        r_semantic_test_m22_regressions();
        r_semantic_test_m23_regressions();
        r_semantic_test_m24_regressions();
        r_semantic_test_l34_regressions();
        r_semantic_test_l35_regressions();
        r_semantic_test_l36_regressions();
        r_semantic_test_l37_regressions();
        r_semantic_test_composition_oom();
        r_semantic_test_recursive_borrow_diagnostics();
        r_semantic_test_resource_contracts();
        r_semantic_test_conditional_positions();
        r_semantic_test_explicit_generics();
        r_semantic_test_replacement();
        r_semantic_test_dyn_interfaces();
        r_semantic_test_const_generics();
        r_semantic_test_closure_modes();
        r_semantic_test_static_conditions();
        r_semantic_test_imported_return_borrow_without_mapping();
        r_semantic_test_imported_return_borrow_mapping();
        r_semantic_test_standard_array_slice_origins();
        r_semantic_test_borrow_access_diagnostics();
        r_semantic_test_shared_owner_clone();
        r_semantic_test_call_borrow_masks();
        r_semantic_test_maximum_call_borrow_mask();
        r_semantic_test_move_values();
        r_semantic_test_checked_intrinsic_matrix();
        r_semantic_test_checked_intrinsic_diagnostics();
        r_semantic_test_core_assume();
        r_semantic_test_core_slice_from_raw_parts();
        r_semantic_test_core_slice_from_raw_parts_in();
        r_semantic_test_core_volatile();
        r_semantic_test_core_adopt_release();
        r_semantic_test_standard_type_registry();
        r_semantic_test_raw_c_functions();
        r_semantic_test_std_c_thread_attachment();
    }
    if (failures != 0) {
        (void)fprintf(stderr, "%d semantic frontend checks failed\n", failures);
        return 1;
    }
    (void)printf("r_frontend_semantic_tests: ok\n");
    return 0;
}

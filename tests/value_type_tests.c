#include "frontend_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef R_VALUE_TYPES_PATH
#error R_VALUE_TYPES_PATH is required
#endif
#ifndef R_VALUE_TYPES_HIR_GOLDEN
#error R_VALUE_TYPES_HIR_GOLDEN is required
#endif
#ifndef R_VALUE_TYPES_MIR_GOLDEN
#error R_VALUE_TYPES_MIR_GOLDEN is required
#endif

typedef struct RValueTypeBuffer {
    char *bytes;
    size_t length;
    size_t capacity;
} RValueTypeBuffer;

typedef struct RValueTypeAllocator {
    size_t calls;
    size_t fail_at;
    size_t live;
} RValueTypeAllocator;

typedef struct RValueTypeNegativeCase {
    const char *name;
    const char *source;
    const char *diagnostic;
} RValueTypeNegativeCase;

static int failures;

#define R_VALUE_CHECK(condition)                                                                   \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            (void)fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);    \
            failures += 1;                                                                         \
        }                                                                                          \
    } while (false)

static void *r_value_allocate(void *user_data, size_t size) {
    RValueTypeAllocator *allocator = user_data;
    void *pointer;

    allocator->calls += 1U;
    if ((allocator->fail_at != 0U) && (allocator->calls == allocator->fail_at)) {
        return NULL;
    }
    pointer = malloc(size);
    if (pointer != NULL) {
        allocator->live += 1U;
    }
    return pointer;
}

static void r_value_free(void *user_data, void *pointer) {
    RValueTypeAllocator *allocator = user_data;
    if (pointer != NULL) {
        R_VALUE_CHECK(allocator->live != 0U);
        if (allocator->live != 0U) {
            allocator->live -= 1U;
        }
        free(pointer);
    }
}

static bool r_value_write(void *user_data, const char *bytes, size_t length) {
    RValueTypeBuffer *buffer = user_data;
    size_t required;
    size_t capacity;
    char *replacement;

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

static void r_value_buffer_destroy(RValueTypeBuffer *buffer) {
    free(buffer->bytes);
    (void)memset(buffer, 0, sizeof(*buffer));
}

static bool r_value_read_file(const char *path, uint8_t **bytes, size_t *length) {
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

static bool r_value_add_source(RFrontendContext *context,
                               const char *name,
                               const uint8_t *bytes,
                               size_t length) {
    RSourceId source_id = R_SOURCE_ID_INVALID;
    return (r_frontend_add_source(context, name, bytes, length, &source_id) == R_FRONTEND_OK) &&
           (source_id != R_SOURCE_ID_INVALID);
}

static size_t r_value_diagnostic_count(const RFrontendContext *context, const char *code) {
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

static RFrontendContext *r_value_prepare_file_context(RValueTypeAllocator *allocator,
                                                      const uint8_t *bytes,
                                                      size_t length,
                                                      bool lower_ast) {
    RFrontendOptions options = r_frontend_default_options();
    RFrontendContext *context;
    RSourceId source_id = R_SOURCE_ID_INVALID;

    if (allocator != NULL) {
        options.allocate = r_value_allocate;
        options.free = r_value_free;
        options.allocator_user_data = allocator;
    }
    context = r_frontend_create(allocator == NULL ? NULL : &options);
    if (context == NULL) {
        return NULL;
    }
    if (!r_value_add_source(context, "codegen_value_types.r", bytes, length)) {
        r_frontend_destroy(context);
        return NULL;
    }
    if (lower_ast) {
        RAstNodeId ast = R_AST_NODE_ID_INVALID;
        source_id = UINT32_C(1);
        if (r_frontend_lower_ast(context, source_id, &ast) != R_FRONTEND_OK) {
            r_frontend_destroy(context);
            return NULL;
        }
    }
    return context;
}

static void r_value_test_positive(void) {
    static const uint64_t expected_character_values[] = {
        UINT64_C(65),
        UINT64_C(233),
        UINT64_C(128578),
        UINT64_C(92),
        UINT64_C(34),
        UINT64_C(39),
        UINT64_C(10),
        UINT64_C(13),
        UINT64_C(9),
        UINT64_C(0),
        UINT64_C(127),
        UINT64_C(128640),
        UINT64_C(1114111),
    };
    uint8_t *source = NULL;
    size_t source_length = 0U;
    RFrontendContext *context;
    RValueTypeBuffer hir = {0};
    RValueTypeBuffer mir = {0};
    uint8_t *expected_hir = NULL;
    size_t expected_hir_length = 0U;
    uint8_t *expected_mir = NULL;
    size_t expected_mir_length = 0U;
    bool hir_character_values[sizeof(expected_character_values) /
                              sizeof(expected_character_values[0])] = {false};
    bool mir_character_values[sizeof(expected_character_values) /
                              sizeof(expected_character_values[0])] = {false};
    size_t index;

    R_VALUE_CHECK(r_value_read_file(R_VALUE_TYPES_PATH, &source, &source_length));
    if (source == NULL) {
        return;
    }
    R_VALUE_CHECK(r_value_read_file(R_VALUE_TYPES_HIR_GOLDEN, &expected_hir, &expected_hir_length));
    R_VALUE_CHECK(r_value_read_file(R_VALUE_TYPES_MIR_GOLDEN, &expected_mir, &expected_mir_length));
    context = r_value_prepare_file_context(NULL, source, source_length, false);
    R_VALUE_CHECK(context != NULL);
    if (context != NULL) {
        R_VALUE_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
        R_VALUE_CHECK(r_frontend_diagnostic_count(context) == 0U);
        for (index = 0U; index < context->hir_node_count; ++index) {
            const RHirNode *node = &context->hir_nodes[index];
            const RSemanticType *type = r_semantic_type(context, node->type);
            size_t character_index;

            if ((node->kind != R_HIR_LITERAL) || (type == NULL) ||
                (type->kind != R_SEMANTIC_TYPE_CHAR)) {
                continue;
            }
            for (character_index = 0U; character_index < (sizeof(expected_character_values) /
                                                          sizeof(expected_character_values[0]));
                 ++character_index) {
                if (node->integer_value == expected_character_values[character_index]) {
                    hir_character_values[character_index] = true;
                }
            }
        }
        R_VALUE_CHECK(r_frontend_dump_hir(context, r_value_write, &hir) == R_FRONTEND_OK);
        R_VALUE_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
        for (index = 0U; index < context->mir_instruction_count; ++index) {
            const RMirInstruction *instruction = &context->mir_instructions[index];
            const RSemanticType *type = r_semantic_type(context, instruction->type);
            size_t character_index;

            if ((instruction->kind != R_MIR_INSTRUCTION_CONSTANT) || (type == NULL) ||
                (type->kind != R_SEMANTIC_TYPE_CHAR)) {
                continue;
            }
            for (character_index = 0U; character_index < (sizeof(expected_character_values) /
                                                          sizeof(expected_character_values[0]));
                 ++character_index) {
                if (instruction->integer_value == expected_character_values[character_index]) {
                    mir_character_values[character_index] = true;
                }
            }
        }
        R_VALUE_CHECK(r_frontend_dump_mir(context, r_value_write, &mir) == R_FRONTEND_OK);
        for (index = 0U;
             index < (sizeof(expected_character_values) / sizeof(expected_character_values[0]));
             ++index) {
            R_VALUE_CHECK(hir_character_values[index]);
            R_VALUE_CHECK(mir_character_values[index]);
        }
        R_VALUE_CHECK(hir.bytes != NULL);
        R_VALUE_CHECK(mir.bytes != NULL);
        if ((hir.bytes != NULL) && (mir.bytes != NULL)) {
            R_VALUE_CHECK(strstr(hir.bytes, "(fixed_array i32 3)") != NULL);
            R_VALUE_CHECK(strstr(hir.bytes, "(fixed_array u16 2)") != NULL);
            R_VALUE_CHECK(strstr(hir.bytes, "(const_slice i32)") != NULL);
            R_VALUE_CHECK(strstr(hir.bytes, "return=i8") != NULL);
            R_VALUE_CHECK(strstr(hir.bytes, "return=i16") != NULL);
            R_VALUE_CHECK(strstr(hir.bytes, "return=i64") != NULL);
            R_VALUE_CHECK(strstr(hir.bytes, "return=isize") != NULL);
            R_VALUE_CHECK(strstr(hir.bytes, "return=u8") != NULL);
            R_VALUE_CHECK(strstr(hir.bytes, "return=u16") != NULL);
            R_VALUE_CHECK(strstr(hir.bytes, "return=u64") != NULL);
            R_VALUE_CHECK(strstr(hir.bytes, "return=usize") != NULL);
            R_VALUE_CHECK(strstr(hir.bytes, "return=f32") != NULL);
            R_VALUE_CHECK(strstr(hir.bytes, "return=f64") != NULL);
            R_VALUE_CHECK(strstr(hir.bytes, "return=char") != NULL);
            R_VALUE_CHECK(strstr(hir.bytes, "return=(option i64)") != NULL);
            R_VALUE_CHECK(strstr(hir.bytes, "return=(option u16)") != NULL);
            R_VALUE_CHECK(strstr(hir.bytes, "throws=(effects") != NULL);
            R_VALUE_CHECK(strstr(hir.bytes, "(throw error=") != NULL);
            R_VALUE_CHECK(strstr(hir.bytes, "(catch symbol=") != NULL);
            R_VALUE_CHECK(strstr(hir.bytes, "(case pattern=variant value=0") != NULL);
            R_VALUE_CHECK(strstr(mir.bytes, "checked=true") != NULL);
            R_VALUE_CHECK(strstr(mir.bytes, "variant_payload") != NULL);
            R_VALUE_CHECK(strstr(mir.bytes, "(branch condition=") != NULL);
            R_VALUE_CHECK(expected_hir != NULL);
            R_VALUE_CHECK(expected_mir != NULL);
            if ((expected_hir != NULL) && (expected_mir != NULL)) {
                if ((hir.length != expected_hir_length) ||
                    (memcmp(hir.bytes, expected_hir, hir.length) != 0)) {
                    size_t mismatch = 0U;
                    const size_t common =
                        hir.length < expected_hir_length ? hir.length : expected_hir_length;
                    while ((mismatch < common) &&
                           ((uint8_t)hir.bytes[mismatch] == expected_hir[mismatch])) {
                        mismatch += 1U;
                    }
                    (void)fprintf(stderr,
                                  "HIR golden mismatch at %zu (actual %zu, expected %zu)\n",
                                  mismatch,
                                  hir.length,
                                  expected_hir_length);
                }
                if ((mir.length != expected_mir_length) ||
                    (memcmp(mir.bytes, expected_mir, mir.length) != 0)) {
                    size_t mismatch = 0U;
                    const size_t common =
                        mir.length < expected_mir_length ? mir.length : expected_mir_length;
                    while ((mismatch < common) &&
                           ((uint8_t)mir.bytes[mismatch] == expected_mir[mismatch])) {
                        mismatch += 1U;
                    }
                    (void)fprintf(stderr,
                                  "MIR golden mismatch at %zu (actual %zu, expected %zu)\n",
                                  mismatch,
                                  mir.length,
                                  expected_mir_length);
                }
                R_VALUE_CHECK((hir.length == expected_hir_length) &&
                              (memcmp(hir.bytes, expected_hir, hir.length) == 0));
                R_VALUE_CHECK((mir.length == expected_mir_length) &&
                              (memcmp(mir.bytes, expected_mir, mir.length) == 0));
            }
        }
    }
    r_value_buffer_destroy(&hir);
    r_value_buffer_destroy(&mir);
    r_frontend_destroy(context);
    free(expected_hir);
    free(expected_mir);
    free(source);
}

static void r_value_test_negative(void) {
    static const RValueTypeNegativeCase cases[] = {
        {"too_many.r",
         "module test.value.negative.too_many;\n"
         "i32 main() { i32[1] values = {1, 2}; return 0; }\n",
         "R-DIAG-INIT-002"},
        {"const_slice_write.r",
         "module test.value.negative.const_slice;\n"
         "i32 main() { i32[1] values = {1}; const i32[] view = &values; "
         "view[0] = 2; return 0; }\n",
         "R-DIAG-TYPE-001"},
        {"incompatible_checked_effect.r",
         "module test.value.negative.attempt;\n"
         "error first_error { i32 code; };\n"
         "error second_error { i32 code; };\n"
         "protected i32 source() throws first_error { return 1; }\n"
         "protected i32 run() throws second_error { "
         "i32 value = source(); return value; }\n"
         "i32 main() { return 0; }\n",
         "R-DIAG-EFFECT-001"},
        {"duplicate_checked_effect.r",
         "module test.value.negative.duplicate_effect;\n"
         "error issue { i32 code; };\n"
         "protected i32 choose() throws issue, issue { return 1; }\n"
         "i32 main() { return 0; }\n",
         "R-DIAG-EFFECT-001"},
        {"variant_payload_without_dereference.r",
         "module test.value.negative.variant_payload_value;\n"
         "i32 main() { o<i32> value = o::some(1); switch (value) { "
         "case variant o::some(payload): return payload; "
         "case variant o::none: return 0; } }\n",
         "R-DIAG-TYPE-001"},
        {"variant_payload_dot_access.r",
         "module test.value.negative.variant_payload_field;\n"
         "error issue { i32 code; };\n"
         "i32 main() { issue item = { .code = 1 }; o<issue> value = o::some(item); "
         "switch (value) { case variant o::some(payload): return payload.code; "
         "case variant o::none: return 0; } }\n",
         "R-DIAG-TYPE-001"},
        {"nonexhaustive.r",
         "module test.value.negative.nonexhaustive;\n"
         "i32 main() { o<i32> value = o::none; switch (value) { "
         "case variant o::none: return 0; } return 1; }\n",
         "R-DIAG-SWITCH-001"},
        {"fixed_array_bounds.r",
         "module test.value.negative.fixed_array_bounds;\n"
         "i32 main() { i32[2] values = {1, 2}; i32 item = values[2]; "
         "return item; }\n",
         "R-DIAG-BOUNDS-001"},
        {"fixed_array_negative_index.r",
         "module test.value.negative.fixed_array_negative_index;\n"
         "i32 main() { i32[2] values = {1, 2}; i32 item = values[-1i32]; "
         "return item; }\n",
         "R-DIAG-BOUNDS-001"},
        {"fixed_array_reverse_range.r",
         "module test.value.negative.fixed_array_reverse_range;\n"
         "i32 main() { i32[2] values = {1, 2}; "
         "const i32[] invalid = values[2..1]; return 0; }\n",
         "R-DIAG-BOUNDS-001"},
        {"fixed_array_range_upper_bound.r",
         "module test.value.negative.fixed_array_range_upper_bound;\n"
         "i32 main() { i32[2] values = {1, 2}; "
         "const i32[] invalid = values[0..3]; return 0; }\n",
         "R-DIAG-BOUNDS-001"},
        {"fixed_array_negative_range.r",
         "module test.value.negative.fixed_array_negative_range;\n"
         "i32 main() { i32[2] values = {1, 2}; "
         "const i32[] invalid = values[-1i32..1i32]; return 0; }\n",
         "R-DIAG-BOUNDS-001"},
        {"mutable_range_from_const.r",
         "module test.value.negative.mutable_range_from_const;\n"
         "i32 main() { const i32[2] values = {1, 2}; "
         "i32[] invalid = values[0..1]; return 0; }\n",
         "R-DIAG-BORROW-001"},
        {"integer_switch_without_default.r",
         "module test.value.negative.integer_switch_without_default;\n"
         "i32 main() { u32 value = 1; switch (value) { case 1: return 0; } return 1; }\n",
         "R-DIAG-SWITCH-001"},
        {"duplicate_integer_switch_case.r",
         "module test.value.negative.duplicate_integer_switch_case;\n"
         "i32 main() { u32 value = 1; switch (value) { "
         "case 1: return 0; case 0 + 1: return 1; default: return 2; } }\n",
         "R-DIAG-SWITCH-001"},
        {"nonconstant_integer_switch_case.r",
         "module test.value.negative.nonconstant_integer_switch_case;\n"
         "i32 main() { u32 value = 1; u32 pattern = 1; switch (value) { "
         "case pattern: return 0; default: return 1; } }\n",
         "R-DIAG-CONST-002"},
        {"bool_switch.r",
         "module test.value.negative.bool_switch;\n"
         "i32 main() { bool value = true; switch (value) { "
         "case 1: return 0; default: return 1; } }\n",
         "R-DIAG-SWITCH-001"},
        {"zero_bound.r",
         "module test.value.negative.zero_bound;\n"
         "i32 main() { i32[0] values = {}; return 0; }\n",
         "R-DIAG-CONST-001"},
        {"narrow_context_literal.r",
         "module test.value.negative.narrow_context_literal;\n"
         "i32 main() { u8 value = 256; return 0; }\n",
         "R-DIAG-CONST-001"},
        {"narrow_suffix_literal.r",
         "module test.value.negative.narrow_suffix_literal;\n"
         "i32 main() { i8 value = 128i8; return 0; }\n",
         "R-DIAG-CONST-001"},
        {"small_integer_promotion.r",
         "module test.value.negative.small_integer_promotion;\n"
         "protected u16 add(u16 left, u16 right) { return left + right; }\n"
         "i32 main() { return 0; }\n",
         "R-DIAG-TYPE-001"},
        {"non_integer_cast.r",
         "module test.value.negative.non_integer_cast;\n"
         "i32 main() { bool flag = true; i32 wide = flag as i32; return wide; }\n",
         "R-DIAG-TYPE-001"},
        {"break_outside_loop.r",
         "module test.value.negative.break_outside_loop;\n"
         "protected void run() { break; }\n"
         "i32 main() { return 0; }\n",
         "R-DIAG-FLOW-001"},
        {"continue_outside_loop.r",
         "module test.value.negative.continue_outside_loop;\n"
         "protected void run() { continue; }\n"
         "i32 main() { return 0; }\n",
         "R-DIAG-FLOW-001"}};
    size_t index;

    for (index = 0U; index < (sizeof(cases) / sizeof(cases[0])); ++index) {
        RFrontendContext *context = r_frontend_create(NULL);
        R_VALUE_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        R_VALUE_CHECK(r_value_add_source(context,
                                         cases[index].name,
                                         (const uint8_t *)cases[index].source,
                                         strlen(cases[index].source)));
        R_VALUE_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
        if (r_value_diagnostic_count(context, cases[index].diagnostic) == 0U) {
            size_t diagnostic_index;
            (void)fprintf(stderr,
                          "%s: expected diagnostic %s; observed:",
                          cases[index].name,
                          cases[index].diagnostic);
            for (diagnostic_index = 0U; diagnostic_index < r_frontend_diagnostic_count(context);
                 ++diagnostic_index) {
                const RDiagnostic *diagnostic = r_frontend_diagnostic(context, diagnostic_index);
                if (diagnostic != NULL) {
                    (void)fprintf(stderr, " %s", diagnostic->code);
                }
            }
            (void)fputc('\n', stderr);
            R_VALUE_CHECK(false);
        }
        r_frontend_destroy(context);
    }
}

static void r_value_test_range_slices(void) {
    static const char source[] =
        "module test.value.range_slices;\n"
        "i32 main() { u8[4] values = {1, 2, 3, 4}; usize lower = 1; usize upper = 3; "
        "u8[] first = values[lower..upper]; const u8[] second = first[1..2]; return 0; }\n";
    RFrontendContext *context = r_frontend_create(NULL);
    size_t hir_range_count = 0U;
    size_t mir_range_count = 0U;
    size_t index;

    R_VALUE_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_VALUE_CHECK(
        r_value_add_source(context, "range_slices.r", (const uint8_t *)source, strlen(source)));
    R_VALUE_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_VALUE_CHECK(r_frontend_diagnostic_count(context) == 0U);
    for (index = 0U; index < context->hir_node_count; ++index) {
        const RHirNode *node = &context->hir_nodes[index];
        if ((node->kind == R_HIR_SLICE) && (node->operation == R_TOKEN_DOT_DOT)) {
            const size_t child_index = (size_t)node->first_child;
            RHirNodeId base_id = R_HIR_NODE_ID_INVALID;
            const RHirNode *base = NULL;

            R_VALUE_CHECK(node->child_count == UINT32_C(3));
            R_VALUE_CHECK(child_index + 2U < context->hir_child_count);
            if (child_index + 2U < context->hir_child_count) {
                base_id = context->hir_children[child_index];
            }
            R_VALUE_CHECK(base_id != R_HIR_NODE_ID_INVALID);
            R_VALUE_CHECK((size_t)base_id <= context->hir_node_count);
            if ((base_id != R_HIR_NODE_ID_INVALID) &&
                ((size_t)base_id <= context->hir_node_count)) {
                base = &context->hir_nodes[(size_t)base_id - 1U];
            }
            R_VALUE_CHECK(base != NULL);
            if (base != NULL) {
                R_VALUE_CHECK(node->symbol == base->symbol);
            }
            hir_range_count += 1U;
        }
    }
    R_VALUE_CHECK(hir_range_count == 2U);
    R_VALUE_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
    for (index = 0U; index < context->mir_instruction_count; ++index) {
        const RMirInstruction *instruction = &context->mir_instructions[index];
        if ((instruction->kind == R_MIR_INSTRUCTION_SLICE) &&
            (instruction->operand1 != R_MIR_VALUE_ID_INVALID)) {
            R_VALUE_CHECK(instruction->operand2 != R_MIR_VALUE_ID_INVALID);
            R_VALUE_CHECK(instruction->operand1 < instruction->operand2);
            R_VALUE_CHECK(instruction->operand2 < instruction->result);
            mir_range_count += 1U;
        }
    }
    R_VALUE_CHECK(mir_range_count == 2U);
    r_frontend_destroy(context);
}

static void r_value_test_integer_switch(void) {
    static const char source[] =
        "module test.value.integer_switch;\n"
        "i32 main() { u32 discriminant = 2; i32 result = 0; switch (discriminant) { "
        "case 0: result = 10; break; case 1: result = 20; break; "
        "case 2: result = 30; break; default: result = 40; break; } return result; }\n";
    RFrontendContext *context = r_frontend_create(NULL);
    size_t constant_case_count = 0U;
    size_t default_case_count = 0U;
    size_t comparison_count = 0U;
    RMirValueId switch_value = R_MIR_VALUE_ID_INVALID;
    bool saw_case_two = false;
    size_t index;

    R_VALUE_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_VALUE_CHECK(
        r_value_add_source(context, "integer_switch.r", (const uint8_t *)source, strlen(source)));
    R_VALUE_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    if (r_frontend_diagnostic_count(context) != 0U) {
        for (index = 0U; index < r_frontend_diagnostic_count(context); ++index) {
            const RDiagnostic *diagnostic = r_frontend_diagnostic(context, index);
            if (diagnostic != NULL) {
                (void)fprintf(stderr,
                              "integer_switch.r: unexpected %s: %s\n",
                              diagnostic->code,
                              diagnostic->message);
            }
        }
    }
    R_VALUE_CHECK(r_frontend_diagnostic_count(context) == 0U);
    for (index = 0U; index < context->hir_node_count; ++index) {
        const RHirNode *node = &context->hir_nodes[index];
        if ((node->kind == R_HIR_CASE) && (node->case_pattern == R_HIR_CASE_PATTERN_CONSTANT)) {
            constant_case_count += 1U;
            saw_case_two = saw_case_two || (node->integer_value == UINT64_C(2));
        } else if ((node->kind == R_HIR_CASE) &&
                   (node->case_pattern == R_HIR_CASE_PATTERN_DEFAULT)) {
            default_case_count += 1U;
        }
    }
    R_VALUE_CHECK(constant_case_count == 3U);
    R_VALUE_CHECK(default_case_count == 1U);
    R_VALUE_CHECK(saw_case_two);
    R_VALUE_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
    for (index = 0U; index < context->mir_instruction_count; ++index) {
        const RMirInstruction *instruction = &context->mir_instructions[index];
        if ((instruction->kind == R_MIR_INSTRUCTION_BINARY) &&
            (instruction->operation == R_TOKEN_EQUAL_EQUAL)) {
            if (switch_value == R_MIR_VALUE_ID_INVALID) {
                switch_value = instruction->operand0;
            }
            R_VALUE_CHECK(instruction->operand0 == switch_value);
            comparison_count += 1U;
        }
    }
    R_VALUE_CHECK(comparison_count == 3U);
    r_frontend_destroy(context);
}

static void r_value_test_integer_casts(void) {
    static const char source[] =
        "module test.value.integer_casts;\n"
        "protected i32 promote(u8 left, u8 right) { return left + right; }\n"
        "i32 main() { u16 source = 40; u8 narrow = source as u8; "
        "i32 sum = promote(narrow, 2); return sum - 42; }\n";
    RFrontendContext *context = r_frontend_create(NULL);
    size_t explicit_hir_count = 0U;
    size_t implicit_hir_count = 0U;
    size_t explicit_mir_count = 0U;
    size_t implicit_mir_count = 0U;
    size_t index;

    R_VALUE_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_VALUE_CHECK(
        r_value_add_source(context, "integer_casts.r", (const uint8_t *)source, strlen(source)));
    R_VALUE_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_VALUE_CHECK(r_frontend_diagnostic_count(context) == 0U);
    for (index = 0U; index < context->hir_node_count; ++index) {
        const RHirNode *node = &context->hir_nodes[index];
        if ((node->kind == R_HIR_CAST) && (node->operation == R_TOKEN_KW_AS)) {
            explicit_hir_count += 1U;
        } else if ((node->kind == R_HIR_CAST) && (node->operation == R_TOKEN_INVALID)) {
            implicit_hir_count += 1U;
        }
    }
    R_VALUE_CHECK(explicit_hir_count == 1U);
    R_VALUE_CHECK(implicit_hir_count == 2U);
    R_VALUE_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
    for (index = 0U; index < context->mir_instruction_count; ++index) {
        const RMirInstruction *instruction = &context->mir_instructions[index];
        if ((instruction->kind == R_MIR_INSTRUCTION_CAST) &&
            (instruction->operation == R_TOKEN_KW_AS)) {
            explicit_mir_count += 1U;
        } else if ((instruction->kind == R_MIR_INSTRUCTION_CAST) &&
                   (instruction->operation == R_TOKEN_INVALID)) {
            implicit_mir_count += 1U;
        }
    }
    R_VALUE_CHECK(explicit_mir_count == 1U);
    R_VALUE_CHECK(implicit_mir_count == 2U);
    r_frontend_destroy(context);
}

static void r_value_test_semantic_allocation_sweep(void) {
    uint8_t *source = NULL;
    size_t source_length = 0U;
    RValueTypeAllocator baseline_allocator = {0};
    RFrontendContext *baseline;
    size_t before;
    size_t allocation_count;
    size_t offset;

    R_VALUE_CHECK(r_value_read_file(R_VALUE_TYPES_PATH, &source, &source_length));
    if (source == NULL) {
        return;
    }
    baseline = r_value_prepare_file_context(&baseline_allocator, source, source_length, true);
    R_VALUE_CHECK(baseline != NULL);
    if (baseline == NULL) {
        free(source);
        return;
    }
    before = baseline_allocator.calls;
    R_VALUE_CHECK(r_frontend_analyze(baseline) == R_FRONTEND_OK);
    allocation_count = baseline_allocator.calls - before;
    R_VALUE_CHECK(allocation_count != 0U);
    r_frontend_destroy(baseline);
    R_VALUE_CHECK(baseline_allocator.live == 0U);

    for (offset = 1U; offset <= allocation_count; ++offset) {
        RValueTypeAllocator allocator = {0};
        RFrontendContext *context =
            r_value_prepare_file_context(&allocator, source, source_length, true);
        RFrontendStatus status;
        R_VALUE_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        allocator.fail_at = allocator.calls + offset;
        status = r_frontend_analyze(context);
        if ((status != R_FRONTEND_OUT_OF_MEMORY) && (status != R_FRONTEND_LIMIT_EXCEEDED)) {
            (void)fprintf(stderr,
                          "semantic allocation failure offset %zu returned status %d\n",
                          offset,
                          (int)status);
        }
        R_VALUE_CHECK((status == R_FRONTEND_OUT_OF_MEMORY) ||
                      (status == R_FRONTEND_LIMIT_EXCEEDED));
        R_VALUE_CHECK(r_frontend_hir_root(context) == R_HIR_NODE_ID_INVALID);
        r_frontend_destroy(context);
        R_VALUE_CHECK(allocator.live == 0U);
    }
    free(source);
}

int main(void) {
    r_value_test_positive();
    r_value_test_negative();
    r_value_test_range_slices();
    r_value_test_integer_switch();
    r_value_test_integer_casts();
    r_value_test_semantic_allocation_sweep();
    if (failures != 0) {
        (void)fprintf(stderr, "value type tests failed: %d\n", failures);
        return EXIT_FAILURE;
    }
    (void)puts("value type tests passed");
    return EXIT_SUCCESS;
}

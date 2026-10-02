#include "frontend_internal.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct RAggregateTestBuffer {
    char *bytes;
    size_t length;
    size_t capacity;
} RAggregateTestBuffer;

typedef struct RAggregateTestAllocator {
    size_t calls;
    size_t fail_at;
    size_t live;
} RAggregateTestAllocator;

static int failures = 0;

#define R_AGGREGATE_CHECK(condition)                                                               \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            (void)fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);    \
            failures += 1;                                                                         \
        }                                                                                          \
    } while (false)

static const char aggregate_api_source[] = "module test.aggregate.api;\n"
                                           "enum state { idle, ready, };\n"
                                           "struct point { i32 x; i32 y; };\n"
                                           "protected struct secret { i32 value; };\n"
                                           "struct guarded { protected i32 hidden; i32 shown; };\n";

static const char aggregate_consumer_source[] = "module test.aggregate.consumer;\n"
                                                "import test.aggregate.api::{point, state};\n"
                                                "protected point bump(point input) {\n"
                                                "  input.x = input.x + 3;\n"
                                                "  return input;\n"
                                                "}\n"
                                                "i32 main() {\n"
                                                "  point value = { .y = 2, .x = 1, };\n"
                                                "  state current = state::ready;\n"
                                                "  point bumped = bump(value);\n"
                                                "  if (bumped.x != 4) { return 1; }\n"
                                                "  if (current == state::ready) {\n"
                                                "    if (bumped.x == 4) { return 0; }\n"
                                                "    return 1;\n"
                                                "  }\n"
                                                "  return 1;\n"
                                                "}\n";

static bool r_aggregate_write(void *user_data, const char *bytes, size_t length) {
    RAggregateTestBuffer *buffer = user_data;
    size_t required;
    size_t capacity;
    char *replacement;

    if (length > (SIZE_MAX - buffer->length - 1U)) {
        return false;
    }
    required = buffer->length + length + 1U;
    if (required > buffer->capacity) {
        capacity = buffer->capacity == 0U ? 512U : buffer->capacity;
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

static void r_aggregate_buffer_destroy(RAggregateTestBuffer *buffer) {
    free(buffer->bytes);
    (void)memset(buffer, 0, sizeof(*buffer));
}

static void *r_aggregate_allocate(void *user_data, size_t size) {
    RAggregateTestAllocator *allocator = user_data;
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

static void r_aggregate_free(void *user_data, void *pointer) {
    RAggregateTestAllocator *allocator = user_data;
    if (pointer != NULL) {
        R_AGGREGATE_CHECK(allocator->live != 0U);
        if (allocator->live != 0U) {
            allocator->live -= 1U;
        }
        free(pointer);
    }
}

static bool
r_aggregate_add_source(RFrontendContext *context, const char *name, const char *source) {
    RSourceId source_id = R_SOURCE_ID_INVALID;
    return r_frontend_add_source(
               context, name, (const uint8_t *)source, strlen(source), &source_id) == R_FRONTEND_OK;
}

static size_t r_aggregate_diagnostic_count(const RFrontendContext *context, const char *code) {
    size_t result = 0U;
    size_t index;
    for (index = 0U; index < r_frontend_diagnostic_count(context); ++index) {
        const RDiagnostic *diagnostic = r_frontend_diagnostic(context, index);
        if ((diagnostic != NULL) && (strcmp(diagnostic->code, code) == 0)) {
            result += 1U;
        }
    }
    return result;
}

static bool r_aggregate_build(bool reverse,
                              RAggregateTestBuffer *mir,
                              RAggregateTestBuffer *interface_output,
                              RAggregateTestBuffer *c17) {
    static const RFrontendArtifactOptions artifact_options = {
        "test.aggregate.consumer::main", "hosted", NULL, 0U, NULL, 0U};
    RFrontendContext *context = r_frontend_create(NULL);
    bool success = false;

    if (context == NULL) {
        return false;
    }
    if (reverse) {
        if (!r_aggregate_add_source(context, "consumer.r", aggregate_consumer_source) ||
            !r_aggregate_add_source(context, "api.r", aggregate_api_source)) {
            goto cleanup;
        }
    } else if (!r_aggregate_add_source(context, "api.r", aggregate_api_source) ||
               !r_aggregate_add_source(context, "consumer.r", aggregate_consumer_source)) {
        goto cleanup;
    }
    if ((r_frontend_analyze(context) != R_FRONTEND_OK) ||
        (context->semantic_aggregate_count != 4U) || (context->semantic_field_count != 5U) ||
        (context->semantic_variant_count != 2U) ||
        (r_frontend_lower_mir(context) != R_FRONTEND_OK) ||
        (r_frontend_dump_mir(context, r_aggregate_write, mir) != R_FRONTEND_OK) ||
        (r_frontend_dump_interface(
             context, &artifact_options, r_aggregate_write, interface_output) != R_FRONTEND_OK) ||
        (r_frontend_emit_c17(context, r_aggregate_write, c17) != R_FRONTEND_OK)) {
        goto cleanup;
    }
    success = true;

cleanup:
    r_frontend_destroy(context);
    return success;
}

static void r_aggregate_test_positive_and_deterministic(void) {
    RAggregateTestBuffer first_mir = {0};
    RAggregateTestBuffer first_interface = {0};
    RAggregateTestBuffer first_c17 = {0};
    RAggregateTestBuffer second_mir = {0};
    RAggregateTestBuffer second_interface = {0};
    RAggregateTestBuffer second_c17 = {0};

    R_AGGREGATE_CHECK(r_aggregate_build(false, &first_mir, &first_interface, &first_c17));
    R_AGGREGATE_CHECK(r_aggregate_build(true, &second_mir, &second_interface, &second_c17));
    R_AGGREGATE_CHECK((first_mir.length == second_mir.length) &&
                      (memcmp(first_mir.bytes, second_mir.bytes, first_mir.length) == 0));
    R_AGGREGATE_CHECK(
        (first_interface.length == second_interface.length) &&
        (memcmp(first_interface.bytes, second_interface.bytes, first_interface.length) == 0));
    R_AGGREGATE_CHECK((first_c17.length == second_c17.length) &&
                      (memcmp(first_c17.bytes, second_c17.bytes, first_c17.length) == 0));
    R_AGGREGATE_CHECK(strstr(first_interface.bytes, "layout=declaration-order") != NULL);
    R_AGGREGATE_CHECK(strstr(first_interface.bytes, "test.aggregate.api::secret") == NULL);
    R_AGGREGATE_CHECK(strstr(first_mir.bytes, "fields=((4 %v0) (3 %v1))") != NULL);
    R_AGGREGATE_CHECK(strstr(first_c17.bytes, ".r_m00000001") != NULL);

    r_aggregate_buffer_destroy(&first_mir);
    r_aggregate_buffer_destroy(&first_interface);
    r_aggregate_buffer_destroy(&first_c17);
    r_aggregate_buffer_destroy(&second_mir);
    r_aggregate_buffer_destroy(&second_interface);
    r_aggregate_buffer_destroy(&second_c17);
}

static void r_aggregate_test_qualified_import(void) {
    static const char qualified_consumer[] =
        "module test.aggregate.qualified_consumer;\n"
        "import test.aggregate.api;\n"
        "i32 main() {\n"
        "  test.aggregate.api::point value = { .x = 1, .y = 2, };\n"
        "  test.aggregate.api::state current = "
        "test.aggregate.api::state::ready;\n"
        "  if (current == test.aggregate.api::state::ready) {\n"
        "    return value.x - 1;\n"
        "  }\n"
        "  return 1;\n"
        "}\n";
    RFrontendContext *context = r_frontend_create(NULL);
    RAggregateTestBuffer c17 = {0};

    R_AGGREGATE_CHECK(context != NULL);
    if (context != NULL) {
        R_AGGREGATE_CHECK(r_aggregate_add_source(context, "api.r", aggregate_api_source));
        R_AGGREGATE_CHECK(r_aggregate_add_source(context, "qualified.r", qualified_consumer));
        R_AGGREGATE_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
        R_AGGREGATE_CHECK(r_frontend_emit_c17(context, r_aggregate_write, &c17) == R_FRONTEND_OK);
        R_AGGREGATE_CHECK(c17.length != 0U);
        r_frontend_destroy(context);
    }
    r_aggregate_buffer_destroy(&c17);
}

static void r_aggregate_expect_diagnostic(const char *source, const char *code) {
    RFrontendContext *context = r_frontend_create(NULL);
    R_AGGREGATE_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_AGGREGATE_CHECK(r_aggregate_add_source(context, "negative.r", source));
    R_AGGREGATE_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
    if (r_aggregate_diagnostic_count(context, code) == 0U) {
        (void)fprintf(stderr, "expected %s for: %s\n", code, source);
        for (size_t index = 0U; index < r_frontend_diagnostic_count(context); ++index) {
            const RDiagnostic *diagnostic = r_frontend_diagnostic(context, index);
            (void)fprintf(stderr, "  %s: %s\n", diagnostic->code, diagnostic->message);
        }
    }
    R_AGGREGATE_CHECK(r_aggregate_diagnostic_count(context, code) != 0U);
    r_frontend_destroy(context);
}

static void r_aggregate_test_negative(void) {
    r_aggregate_expect_diagnostic(
        "module test.duplicate_field; struct item { i32 value; i32 value; }; "
        "i32 main() { return 0; }",
        "R-DIAG-NAME-002");
    r_aggregate_expect_diagnostic("module test.duplicate_variant; enum state { ready, ready, }; "
                                  "i32 main() { return 0; }",
                                  "R-DIAG-NAME-002");
    r_aggregate_expect_diagnostic("module test.unknown_field; struct item { i32 value; }; "
                                  "i32 main() { item x = { .missing = 1, }; return 0; }",
                                  "R-DIAG-INIT-002");
    r_aggregate_expect_diagnostic("module test.repeated_field; struct item { i32 value; }; "
                                  "i32 main() { item x = { .value = 1, .value = 2, }; return 0; }",
                                  "R-DIAG-INIT-002");
    r_aggregate_expect_diagnostic("module test.missing_field; enum state { ready, }; "
                                  "struct item { state value; }; "
                                  "i32 main() { item x = {}; return 0; }",
                                  "R-DIAG-INIT-002");
    r_aggregate_expect_diagnostic("module test.positional; struct item { i32 value; }; "
                                  "i32 main() { item x = { 1, }; return 0; }",
                                  "R-DIAG-INIT-002");
    r_aggregate_expect_diagnostic("module test.payload; enum state : u8 { ready(i32), }; "
                                  "i32 main() { return 0; }",
                                  "R-DIAG-TYPE-001");
    r_aggregate_expect_diagnostic("module test.aggregate_attribute; @repr(\"C\") "
                                  "struct item { i32 value; }; i32 main() { return 0; }",
                                  "R-DIAG-FFI-003");
    r_aggregate_expect_diagnostic(
        "module test.field_attribute; struct item { @repr(\"C\") i32 value; }; "
        "i32 main() { return 0; }",
        "R-DIAG-FFI-003");
    r_aggregate_expect_diagnostic(
        "module test.field_scoped; struct item { @scoped i32 value; }; i32 main() { return 0; }",
        "R-DIAG-SYN-002");
    r_aggregate_expect_diagnostic("module test.recursive; struct item { item value; }; "
                                  "i32 main() { return 0; }",
                                  "R-DIAG-TYPE-001");
    r_aggregate_expect_diagnostic(
        "module test.repr_duplicate; @repr(C) @repr(C) struct item { c_int value; }; "
        "i32 main() { return 0; }",
        "R-DIAG-FFI-003");
    r_aggregate_expect_diagnostic("module test.repr_r_scalar; @repr(C) struct item { i32 value; }; "
                                  "i32 main() { return 0; }",
                                  "R-DIAG-FFI-003");
    r_aggregate_expect_diagnostic(
        "module test.repr_nested; struct inner { c_int value; }; "
        "@repr(C) struct outer { inner value; }; i32 main() { return 0; }",
        "R-DIAG-FFI-003");
    r_aggregate_expect_diagnostic("module test.repr_atomic; @repr(C) struct item { au32 value; }; "
                                  "i32 main() { return 0; }",
                                  "R-DIAG-FFI-003");
    r_aggregate_expect_diagnostic(
        "module test.repr_drop; @repr(C) struct item { c_int value; }; drop(item* self) {} "
        "i32 main() { return 0; }",
        "R-DIAG-FFI-003");
    r_aggregate_expect_diagnostic("module test.repr_enum_underlying; @repr(C) enum item { zero, }; "
                                  "i32 main() { return 0; }",
                                  "R-DIAG-FFI-003");
    r_aggregate_expect_diagnostic(
        "module test.repr_enum_payload; @repr(C) enum item { value(c_int), }; "
        "i32 main() { return 0; }",
        "R-DIAG-FFI-003");
    r_aggregate_expect_diagnostic(
        "module test.char_switch_default; "
        "i32 classify(char value) { switch (value) { case 'a': return 1; } } "
        "i32 main() { return 0; }",
        "R-DIAG-SWITCH-001");
}

static void r_aggregate_test_repr_c(void) {
    static const char source[] =
        "module test.repr_c; "
        "@repr(C) enum status : c_int { ok = 0, failed = 1, }; "
        "@repr(C) struct point { c_int x; c_int y; }; "
        "@repr(C) struct packet { point value; c_uint8[4] tag; raw point*? next; status state; }; "
        "@safety(\"TEST-TRANSLATE\", \"value is initialized\") "
        "extern \"C\" point translate(point value) { return value; } "
        "i32 main() { point value = point { .x = 1i32 as c_int, .y = 2i32 as c_int }; "
        "point result = translate(value); result as void; return 0; }";
    RFrontendArtifactOptions options = {NULL, "hosted", NULL, 0U, NULL, 0U};
    RFrontendContext *context = r_frontend_create(NULL);
    RAggregateTestBuffer interface_output = {0};
    RAggregateTestBuffer c17 = {0};
    size_t aggregate_index;

    R_AGGREGATE_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_AGGREGATE_CHECK(r_aggregate_add_source(context, "repr-c.r", source));
    R_AGGREGATE_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_AGGREGATE_CHECK(context->semantic_aggregate_count == 3U);
    for (aggregate_index = 0U; aggregate_index < context->semantic_aggregate_count;
         ++aggregate_index) {
        R_AGGREGATE_CHECK(context->semantic_aggregates[aggregate_index].is_repr_c);
        R_AGGREGATE_CHECK(!context->semantic_aggregates[aggregate_index].poisoned);
    }
    R_AGGREGATE_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
    R_AGGREGATE_CHECK(
        r_frontend_dump_interface(context, &options, r_aggregate_write, &interface_output) ==
        R_FRONTEND_OK);
    R_AGGREGATE_CHECK((interface_output.bytes != NULL) &&
                      (strstr(interface_output.bytes, "repr_c=true") != NULL));
    R_AGGREGATE_CHECK(r_frontend_emit_c17(context, r_aggregate_write, &c17) == R_FRONTEND_OK);
    R_AGGREGATE_CHECK(strstr(c17.bytes, "translate(") != NULL);

    r_aggregate_buffer_destroy(&interface_output);
    r_aggregate_buffer_destroy(&c17);
    r_frontend_destroy(context);
}

static void r_aggregate_test_enum_constant_values(void) {
    static const char source[] =
        "module test.enum_values; const i32 BASE = 6; "
        "enum A : i8 { Minimum = -128, After, Negative = -1, Zero, Maximum = 127, }; "
        "enum B : i64 { Minimum = -9223372036854775808, After, Maximum = 9223372036854775807, }; "
        "enum C : u64 { Maximum = 18446744073709551615, Half = 1u64 << 63usize, }; "
        "enum D : i16 { Product = BASE * 3, Shift = 1 << 10usize, "
        "Remainder = -7 % 3, Division = -7 / 3, Cast = -3i8 as i16, }; "
        "enum E : c_int { Value = BASE, Next, }; i32 main() { return 0; }";
    static const uint64_t expected[] = {
        UINT64_C(128),
        UINT64_C(129),
        UINT64_C(255),
        UINT64_C(0),
        UINT64_C(127),
        UINT64_C(9223372036854775808),
        UINT64_C(9223372036854775809),
        UINT64_C(9223372036854775807),
        UINT64_MAX,
        UINT64_C(9223372036854775808),
        UINT64_C(18),
        UINT64_C(1024),
        UINT64_C(65535),
        UINT64_C(65534),
        UINT64_C(65533),
        UINT64_C(6),
        UINT64_C(7),
    };
    RFrontendContext *context = r_frontend_create(NULL);
    size_t index;
    R_AGGREGATE_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_AGGREGATE_CHECK(r_aggregate_add_source(context, "enum-values.r", source));
    R_AGGREGATE_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_AGGREGATE_CHECK(context->semantic_variant_count == sizeof(expected) / sizeof(expected[0]));
    for (index = 0U; (index < context->semantic_variant_count) &&
                     (index < sizeof(expected) / sizeof(expected[0]));
         ++index) {
        R_AGGREGATE_CHECK(context->semantic_variants[index].value == expected[index]);
    }
    r_frontend_destroy(context);
}

static void r_aggregate_test_enum_discriminants(void) {
    static const char *const invalid[] = {
        "enum E : u8 { A = 256, };",
        "enum E : i8 { A = -129, };",
        "enum E : i8 { A = 127, B, };",
        "enum E : u64 { A = 18446744073709551615, B, };",
        "enum E : i64 { A = 9223372036854775807, B, };",
        "enum E : i64 { A = -9223372036854775808 - 1, };",
        "enum E : i64 { A = 9223372036854775807 * 2, };",
        "enum E : i64 { A = -9223372036854775808 / -1, };",
        "enum E : i64 { A = -(-9223372036854775808), };",
        "enum E { A = 1 / 0, };",
        "enum E { A = 1 << 32usize, };",
        "enum E { A = 1 << 31usize, };",
        "enum E : i64 { A = 1i64 << 63usize, };",
        "enum E { A = 7, B = 7, };",
        "enum E { A = 7, B, C = 8, };",
        "enum E { A = true, };",
        "enum E { A = 256 as u8, };",
        "enum E { A = -1 as u32, };",
        "enum E { A = 128 as i8, };",
        "enum E : i64 { A = 18446744073709551615 as i64, };",
        "enum E : c_schar { A = 128, };",
        "enum E : c_uintmax { A = 18446744073709551615, B, };",
    };
    size_t index;
    for (index = 0U; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
        char source[512];
        (void)snprintf(source,
                       sizeof(source),
                       "module test.enum_constant; %s i32 main() { return 0; }",
                       invalid[index]);
        r_aggregate_expect_diagnostic(source, "R-DIAG-CONST-001");
    }
    r_aggregate_expect_diagnostic(
        "module test.enum_switch; enum E { A, B, C, }; "
        "i32 main() { E e = E::A; switch(e) { case E::A: return 0; case E::B: return 1; } }",
        "R-DIAG-SWITCH-001");
    r_aggregate_expect_diagnostic("module test.enum_switch; enum E { A, B, }; "
                                  "i32 main() { E e = E::A; switch(e) { case E::A: return 0; case "
                                  "E::A: return 1; default: return 2; } }",
                                  "R-DIAG-SWITCH-001");
    r_aggregate_expect_diagnostic(
        "module test.enum_switch; enum E { A, }; enum F { A, }; "
        "i32 main() { E e = E::A; switch(e) { case F::A: return 0; default: return 1; } }",
        "R-DIAG-SWITCH-001");
}

static void r_aggregate_test_named_fixed_array_bound(void) {
    static const char source[] = "module test.aggregate.named_bound; "
                                 "const usize LIMIT = 4; "
                                 "struct block { u8[LIMIT] bytes; }; "
                                 "i32 main() { return 0; }";
    RFrontendContext *context = r_frontend_create(NULL);
    const RSemanticType *field_type = NULL;
    const RSemanticType *element_type = NULL;

    R_AGGREGATE_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_AGGREGATE_CHECK(r_aggregate_add_source(context, "named-bound.r", source));
    R_AGGREGATE_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_AGGREGATE_CHECK(r_aggregate_diagnostic_count(context, "R-DIAG-SLICE-001") == 0U);
    R_AGGREGATE_CHECK(context->semantic_field_count == 1U);
    if (context->semantic_field_count == 1U) {
        field_type = r_semantic_type(context, context->semantic_fields[0].type);
    }
    R_AGGREGATE_CHECK((field_type != NULL) && (field_type->kind == R_SEMANTIC_TYPE_FIXED_ARRAY) &&
                      (field_type->length == UINT64_C(4)));
    if (field_type != NULL) {
        element_type = r_semantic_type(context, field_type->base);
    }
    R_AGGREGATE_CHECK((element_type != NULL) && (element_type->kind == R_SEMANTIC_TYPE_U8));
    r_frontend_destroy(context);
}

static void r_aggregate_test_protected(void) {
    static const char protected_consumer[] = "module test.aggregate.protected_consumer;\n"
                                             "import test.aggregate.api::{secret};\n"
                                             "secret reveal() { return { .value = 1, }; }\n"
                                             "i32 main() { return 0; }\n";
    static const char field_consumer[] =
        "module test.aggregate.field_consumer;\n"
        "import test.aggregate.api::{guarded};\n"
        "i32 main() { guarded x = { .hidden = 1, .shown = 2, }; return 0; }\n";
    RFrontendContext *context = r_frontend_create(NULL);

    R_AGGREGATE_CHECK(context != NULL);
    if (context != NULL) {
        R_AGGREGATE_CHECK(r_aggregate_add_source(context, "api.r", aggregate_api_source));
        R_AGGREGATE_CHECK(r_aggregate_add_source(context, "protected.r", protected_consumer));
        R_AGGREGATE_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
        R_AGGREGATE_CHECK(r_aggregate_diagnostic_count(context, "R-DIAG-NAME-001") != 0U);
        R_AGGREGATE_CHECK(r_aggregate_diagnostic_count(context, "R-DIAG-SLICE-001") == 0U);
        r_frontend_destroy(context);
    }

    context = r_frontend_create(NULL);
    R_AGGREGATE_CHECK(context != NULL);
    if (context != NULL) {
        R_AGGREGATE_CHECK(r_aggregate_add_source(context, "api.r", aggregate_api_source));
        R_AGGREGATE_CHECK(r_aggregate_add_source(context, "field.r", field_consumer));
        R_AGGREGATE_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
        R_AGGREGATE_CHECK(r_aggregate_diagnostic_count(context, "R-DIAG-NAME-001") != 0U);
        R_AGGREGATE_CHECK(r_aggregate_diagnostic_count(context, "R-DIAG-SLICE-001") == 0U);
        r_frontend_destroy(context);
    }
}

static void r_aggregate_test_payload_and_drop_diagnostics(void) {
    static const struct {
        const char *source;
        const char *diagnostic;
    } cases[] = {
        {"enum E { Data(i32), }; i32 main() { E x = E::Data; return 0; }", "R-DIAG-TYPE-001"},
        {"enum E { Data(i32), }; i32 main() { E x = E::Data(true); return 0; }", "R-DIAG-TYPE-001"},
        {"enum E { Data(i32), }; i32 main() { E x = E::Data(1, 2); return 0; }", "R-DIAG-TYPE-001"},
        {"enum E { Data { i32 x; }, }; i32 main() { E x = E::Data(1); return 0; }",
         "R-DIAG-TYPE-001"},
        {"enum E { Empty, Data(i32), }; i32 main() { E x = E::Empty; switch(x) { case variant "
         "E::Empty: return 0; } }",
         "R-DIAG-SWITCH-001"},
        {"enum E { Empty, Data(i32), }; i32 main() { E x = E::Empty; switch(x) { case variant "
         "E::Empty: break; case variant E::Empty: break; default: return 0; } return 0; }",
         "R-DIAG-SWITCH-001"},
        {"enum E { Empty, Data(i32), }; i32 main() { E x = E::Empty; bool same = x == x; return 0; "
         "}",
         "R-DIAG-TYPE-001"},
        {"enum E { Data(E[1]), }; i32 main() { return 0; }", "R-DIAG-TYPE-001"},
        {"enum E { Data(i32), }; enum F { Data(i32), }; i32 main() { E x = E::Data(1); switch(x) { "
         "case variant F::Data(v): return 0; } }",
         "R-DIAG-SWITCH-001"},
        {"enum E { Data(own i32*), }; i32 main() { E x = E::Data(new i32(1)); switch(x) { case "
         "variant E::Data: drop x; break; } return 0; }",
         "R-DIAG-BORROW-001"},
        {"enum E { Data(own i32*), }; drop(E* self) {} i32 main() { E x = E::Data(new i32(1)); "
         "switch(move x) { case variant E::Data(move v): return 0; } }",
         "R-DIAG-MOVE-003"},
        {"struct S { i32 x; }; drop(S* wrong) {} i32 main() { return 0; }", "R-DIAG-TYPE-001"},
        {"struct S { i32 x; }; drop(const S* self) {} i32 main() { return 0; }", "R-DIAG-TYPE-001"},
        {"struct S { i32 x; }; drop(S* self) {} drop(S* self) {} i32 main() { return 0; }",
         "R-DIAG-NAME-002"},
        {"enum E { Empty, }; drop(E* self) {} i32 main() { return 0; }", "R-DIAG-TYPE-001"},
        {"enum E { Empty, Data(i32), }; void f(E value, own i32* owner) { switch(value) { case "
         "variant E::Empty: drop owner; break; default: break; } } i32 main() { return 0; }",
         "R-DIAG-MOVE-003"},
    };
    size_t index;
    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        char source[1024];
        const int written =
            snprintf(source, sizeof(source), "module test.payload_cases; %s", cases[index].source);
        R_AGGREGATE_CHECK(written > 0 && (size_t)written < sizeof(source));
        r_aggregate_expect_diagnostic(source, cases[index].diagnostic);
    }
}

static void r_aggregate_test_generic_diagnostics(void) {
    static const struct {
        const char *source;
        const char *code;
    } cases[] = {
        {"@generic<T> T bad(T value) { return value; }", "R-DIAG-MOVE-001"},
        {"@generic<T: copy> T bad(T value) { return value + value; }", "R-DIAG-TYPE-001"},
        {"@generic<T: pod> T bad(T value) { return value + value; }", "R-DIAG-TYPE-001"},
        {"@generic<T> T bad(T value) { T moved = move value; return move value; }",
         "R-DIAG-MOVE-002"},
        {"@generic<T> void bad(T value) { drop value; }", "R-DIAG-TYPE-001"},
        {"@generic<T: unknown> T bad(T value) { return move value; }", "R-DIAG-TYPE-001"},
        {"@generic<T, T> T bad(T value) { return move value; }", "R-DIAG-NAME-002"},
        /* R-TYPE-0036: a result-only definition is valid; a call without an expected result
           type leaves its parameter uninferred (L17). */
        {"@generic<T> T bad() { panic(\"unreachable\"); } void use() { bad() as void; }",
         "R-DIAG-TYPE-001"},
        {"@generic<T> async T bad(T value) { return move value; }", "R-DIAG-ASYNC-001"},
        {"@generic<T> extern \"C\" T bad(T value) { return move value; }", "R-DIAG-TYPE-001"},
        {"@generic<T: send> async T bad(T value) { return move value; }", "R-DIAG-ASYNC-001"},
        {"@generic<E> void bad(E value) throws E { throw move value; }", "R-DIAG-EFFECT-002"},
        {"@generic<T: copy> T f(T value) { return value; } void test() { own i32* value = new "
         "i32(1); own i32* bad = f(move value); }",
         "R-DIAG-TYPE-001"},
        {"@generic<T: pod> T f(T value) { return value; } void test() { constexpr str bad = "
         "f(\"text\"); }",
         "R-DIAG-TYPE-001"},
        {"@generic<T> T f(T left, T right) { return move left; } void test() { i32 bad = f(1, "
         "true); }",
         "R-DIAG-TYPE-001"},
        {"@generic<T> struct Box { T value; }; void test() { Box<i32, bool> value = {}; }",
         "R-DIAG-TYPE-001"},
        {"@generic<T> struct Box { T value; }; void test() { Box<const i32> value = {}; }",
         "R-DIAG-TYPE-001"},
        {"@generic<T> struct Box { T value; }; @generic<T: copy> drop(Box<T>* self) {}",
         "R-DIAG-TYPE-001"},
        {"@generic<T> struct Box { T value; }; drop(Box<i32>* self) {}", "R-DIAG-TYPE-001"},
        {"@generic<T> struct Box { T value; }; @generic<T> drop(Box<i32>* self) {}",
         "R-DIAG-TYPE-001"},
        {"@generic<A, B> struct Pair { A left; B right; }; @generic<A, B> drop(Pair<B, A>* self) "
         "{}",
         "R-DIAG-TYPE-001"},
        {"struct K { i32 value; }; u64 K::hash(const K* value) { return 0; }", "R-DIAG-TYPE-001"},
        {"struct K { i32 value; }; u64 impure() { return 0; } u64 K::hash(const K* value) { u64 "
         "result = impure(); return result; } bool K::equal(const K* left, const K* right) { "
         "return true; }",
         "R-DIAG-TYPE-001"},
        {"@generic<T> struct K { T value; }; @generic<T> u64 K<T>::hash(const K<T>* value) { own "
         "i32* owner = new i32(1); return 0; } @generic<T> bool K<T>::equal(const K<T>* left, "
         "const "
         "K<T>* right) { return true; }",
         "R-DIAG-TYPE-001"},
        {"@generic<E: error & send> async void bad(E value) throws E { throw move value; }",
         "R-DIAG-ASYNC-001"},
        {"@generic<T: pod> void use(T value) {} void test(const i32* value) { use(value); }",
         "R-DIAG-TYPE-001"},
        {"@generic<T: pod> void use(T value) {} struct K { i32 value; }; drop(K* self) {} void "
         "test() { K value = K { .value = 1 }; use(move value); }",
         "R-DIAG-TYPE-001"},

        {"@generic<T> @safety(\"caller checks\", \"documented\") unsafe T f(T value) { return move "
         "value; } void test() { i32 result = f(1); }",
         "R-DIAG-UNSAFE-001"},
        {"@generic<T> struct S { u8[sizeof(T) / 0usize] bytes; };", "R-DIAG-CONST-001"},
        {"@generic<T> struct S { u8[sizeof(T) - sizeof(T)] bytes; }; void test() { S<i32> value = "
         "{}; }",
         "R-DIAG-CONST-001"},
        {"@generic<T> T f(T value) { return move value; } const i32* bad() { i32 local=1; const "
         "i32* pointer=&local; const i32* result=f(pointer); return result; }",
         "R-DIAG-BORROW-002"},
        {"@generic<T: pod> void use(T value) {} void test() { own i32* owner=new i32(1); use(move "
         "owner); }",
         "R-DIAG-TYPE-001"},
        {"@generic<T: pod> struct S { T value; }; void test(S<atomic i32> value) {}",
         "R-DIAG-TYPE-001"},
        {"@generic<T: unborrowed> async void f(T value) { return; }", "R-DIAG-ASYNC-001"},
        {"@generic<T: copy> T f(T value) { drop value; return value; }", "R-DIAG-TYPE-001"},
        {"@generic<T> void f(T value) { u8[sizeof(T)] values = {}; u8 bad = values[99]; } void "
         "test() { f(true); }",
         "R-DIAG-BOUNDS-001"},
        {"@generic<T> struct Bad { dict<T, i32> values; };", "R-DIAG-TYPE-001"},
        {"@generic<T> struct Bad { task<T> value; };", "R-DIAG-ASYNC-001"},
        {"@generic<T> void bad(dict<T, i32> values) {}", "R-DIAG-TYPE-001"},
        {"@generic<E: error> struct Bad { task<void throws E> value; };", "R-DIAG-ASYNC-001"},
        {"struct Key { i32 value; }; @generic<T> struct Bad { dict<Key, T> values; };",
         "R-DIAG-TYPE-001"},
    };
    size_t index;
    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        char source[1024];
        const int length = snprintf(source,
                                    sizeof(source),
                                    "module test.generic_cases; %s i32 main() { return 0; }",
                                    cases[index].source);
        R_AGGREGATE_CHECK(length > 0 && (size_t)length < sizeof(source));
        r_aggregate_expect_diagnostic(source, cases[index].code);
    }
}

static void r_aggregate_test_generic_nested_constraints(void) {
    static const char source[] =
        "module test.nested_positive; "
        "@generic<K: key, V> struct Index { dict<K, V> entries; }; "
        "@generic<T: unborrowed> struct Pending { task<T> operation; }; "
        "@generic<E: error & send & unborrowed> struct Failed { task<void throws E> operation; }; "
        "i32 main() { return 0; }";
    RFrontendContext *context = r_frontend_create(NULL);
    R_AGGREGATE_CHECK(context != NULL);
    if (context != NULL) {
        R_AGGREGATE_CHECK(r_aggregate_add_source(context, "nested.r", source));
        R_AGGREGATE_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
        R_AGGREGATE_CHECK(r_frontend_diagnostic_count(context) == 0U);
        r_frontend_destroy(context);
    }
}

static void r_aggregate_test_generic_growth_limit(void) {
    RFrontendOptions options = r_frontend_default_options();
    RFrontendContext *context;
    options.limits.max_nesting = 24U;
    context = r_frontend_create(&options);
    R_AGGREGATE_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_AGGREGATE_CHECK(r_aggregate_add_source(
        context,
        "growing.r",
        "module test.growing; @generic<T> struct Box { T value; }; "
        "@generic<T> void grow(T value) { Box<T> boxed = Box<T> {.value=move value}; "
        "grow(move boxed); } i32 main() { grow(1); return 0; }"));
    R_AGGREGATE_CHECK(r_frontend_analyze(context) == R_FRONTEND_LIMIT_EXCEEDED);
    R_AGGREGATE_CHECK(r_aggregate_diagnostic_count(context, "R-DIAG-LIMIT-001") == 1U);
    if (context->diagnostic_count != 0U) {
        const char *message = context->diagnostics[context->diagnostic_count - 1U].message;
        R_AGGREGATE_CHECK(strstr(message, " <- ") != NULL);
        R_AGGREGATE_CHECK(strstr(message, "grow<i32>") != NULL);
        R_AGGREGATE_CHECK(strlen(message) < 4096U);
    }
    r_frontend_destroy(context);
}

static size_t r_aggregate_allocation_run(size_t fail_at, RFrontendStatus *status, size_t *written) {
    RAggregateTestAllocator allocator = {0};
    RFrontendOptions options = r_frontend_default_options();
    RFrontendContext *context;
    RAggregateTestBuffer output = {0};
    static const char allocation_source[] =
        "module test.aggregate.allocation; "
        "@generic<T> struct Box { T value; }; "
        "@generic<T> T identity(T value) { return move value; } "
        "struct item { i32 value; }; "
        "enum state { empty, ready(item), owned(own i32*), fields { item data; }, }; "
        "drop(state* self) {} "
        "i32 main() { Box<i32> input = Box<i32> {.value=1}; Box<i32> copied = identity(move "
        "input); "
        "state x = state::ready(item {.value=copied.value}); "
        "switch(move x) { case variant state::ready(value): return value->value - 1; default: "
        "return 1; } }";

    allocator.fail_at = fail_at;
    options.allocate = r_aggregate_allocate;
    options.free = r_aggregate_free;
    options.allocator_user_data = &allocator;
    context = r_frontend_create(&options);
    if (context == NULL) {
        *status = R_FRONTEND_OUT_OF_MEMORY;
    } else if (!r_aggregate_add_source(context, "allocation.r", allocation_source)) {
        *status = context->resource_status;
    } else {
        *status = r_frontend_analyze(context);
        if (*status == R_FRONTEND_OK) {
            *status = r_frontend_emit_c17(context, r_aggregate_write, &output);
        }
    }
    *written = output.length;
    r_aggregate_buffer_destroy(&output);
    r_frontend_destroy(context);
    R_AGGREGATE_CHECK(allocator.live == 0U);
    return allocator.calls;
}

static void r_aggregate_test_allocation_failures(void) {
    RFrontendStatus status = R_FRONTEND_INTERNAL_ERROR;
    size_t written = 0U;
    const size_t allocation_count = r_aggregate_allocation_run(0U, &status, &written);
    size_t fail_at;

    R_AGGREGATE_CHECK(status == R_FRONTEND_OK);
    R_AGGREGATE_CHECK(written != 0U);
    for (fail_at = 1U; fail_at <= allocation_count; ++fail_at) {
        written = 0U;
        (void)r_aggregate_allocation_run(fail_at, &status, &written);
        R_AGGREGATE_CHECK(status == R_FRONTEND_OUT_OF_MEMORY);
        R_AGGREGATE_CHECK(written == 0U);
    }
}

int main(void) {
    r_aggregate_test_positive_and_deterministic();
    r_aggregate_test_qualified_import();
    r_aggregate_test_negative();
    r_aggregate_test_repr_c();
    r_aggregate_test_enum_discriminants();
    r_aggregate_test_payload_and_drop_diagnostics();
    r_aggregate_test_generic_diagnostics();
    r_aggregate_test_generic_growth_limit();
    r_aggregate_test_generic_nested_constraints();
    r_aggregate_test_enum_constant_values();
    r_aggregate_test_named_fixed_array_bound();
    r_aggregate_test_protected();
    r_aggregate_test_allocation_failures();
    if (failures != 0) {
        (void)fprintf(stderr, "%d aggregate test failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

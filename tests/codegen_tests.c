#include "frontend_internal.h"
#include "named_standard_copy_abi.h"
#include "named_standard_move_abi.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef R_CODEGEN_MATH_PATH
#error R_CODEGEN_MATH_PATH is required
#endif
#ifndef R_CODEGEN_MAIN_PATH
#error R_CODEGEN_MAIN_PATH is required
#endif
#ifndef R_CODEGEN_RECURSIVE_PATH
#error R_CODEGEN_RECURSIVE_PATH is required
#endif
#ifndef R_CODEGEN_GOLDEN_SOURCE_PATH
#error R_CODEGEN_GOLDEN_SOURCE_PATH is required
#endif
#ifndef R_CODEGEN_GOLDEN_PATH
#error R_CODEGEN_GOLDEN_PATH is required
#endif
#ifndef R_CODEGEN_VALUE_TYPES_PATH
#error R_CODEGEN_VALUE_TYPES_PATH is required
#endif
#ifndef R_CODEGEN_ARRAY_SLICE_PATH
#error R_CODEGEN_ARRAY_SLICE_PATH is required
#endif
#ifndef R_CODEGEN_SYNC_AGGREGATE_DESTINATION_PATH
#error R_CODEGEN_SYNC_AGGREGATE_DESTINATION_PATH is required
#endif
#ifndef R_CODEGEN_CONSTEXPR_STRING_POOL_PATH
#error R_CODEGEN_CONSTEXPR_STRING_POOL_PATH is required
#endif
#ifndef R_CODEGEN_CONSTEXPR_STRING_POOL_SUPPORT_PATH
#error R_CODEGEN_CONSTEXPR_STRING_POOL_SUPPORT_PATH is required
#endif
#ifndef R_CODEGEN_ASYNC_OPTION_PATH_PATH
#error R_CODEGEN_ASYNC_OPTION_PATH_PATH is required
#endif
#ifndef R_CODEGEN_ASYNC_SCALAR_PATH
#error R_CODEGEN_ASYNC_SCALAR_PATH is required
#endif
#ifndef R_CODEGEN_ASYNC_ENUM_PATH
#error R_CODEGEN_ASYNC_ENUM_PATH is required
#endif
#ifndef R_CODEGEN_ASYNC_SHORT_CIRCUIT_PATH
#error R_CODEGEN_ASYNC_SHORT_CIRCUIT_PATH is required
#endif
#ifndef R_CODEGEN_ASYNC_FUSED_DEFAULT_OWN_PATH
#error R_CODEGEN_ASYNC_FUSED_DEFAULT_OWN_PATH is required
#endif
#ifndef R_CODEGEN_ASYNC_IO_WRITE_ALL_PATH
#error R_CODEGEN_ASYNC_IO_WRITE_ALL_PATH is required
#endif
#ifndef R_CODEGEN_ASYNC_IO_READ_PATH
#error R_CODEGEN_ASYNC_IO_READ_PATH is required
#endif
#ifndef R_CODEGEN_STDOUT_PATH
#error R_CODEGEN_STDOUT_PATH is required
#endif
#ifndef R_CODEGEN_ASYNC_STDOUT_PATH
#error R_CODEGEN_ASYNC_STDOUT_PATH is required
#endif
#ifndef R_CODEGEN_STDIN_PATH
#error R_CODEGEN_STDIN_PATH is required
#endif
#ifndef R_CODEGEN_ASYNC_STDIN_PATH
#error R_CODEGEN_ASYNC_STDIN_PATH is required
#endif
#ifndef R_CODEGEN_ASYNC_SYNC_CALL_PATH
#error R_CODEGEN_ASYNC_SYNC_CALL_PATH is required
#endif
#ifndef R_CODEGEN_ASYNC_MAIN_ARGS_PATH
#error R_CODEGEN_ASYNC_MAIN_ARGS_PATH is required
#endif
#ifndef R_CODEGEN_SYNC_MAIN_ARGS_PATH
#error R_CODEGEN_SYNC_MAIN_ARGS_PATH is required
#endif
#ifndef R_CODEGEN_ASYNC_COPY_ARGS_PATH
#error R_CODEGEN_ASYNC_COPY_ARGS_PATH is required
#endif
#ifndef R_CODEGEN_ASYNC_COPY_AGGREGATE_PATH
#error R_CODEGEN_ASYNC_COPY_AGGREGATE_PATH is required
#endif
#ifndef R_CODEGEN_ASYNC_MOVE_ARGS_PATH
#error R_CODEGEN_ASYNC_MOVE_ARGS_PATH is required
#endif
#ifndef R_CODEGEN_CORE_ASSUME_PATH
#error R_CODEGEN_CORE_ASSUME_PATH is required
#endif
#ifndef R_CODEGEN_ASYNC_CORE_ASSUME_PATH
#error R_CODEGEN_ASYNC_CORE_ASSUME_PATH is required
#endif
#ifndef R_CODEGEN_CORE_SLICE_FROM_RAW_PARTS_PATH
#error R_CODEGEN_CORE_SLICE_FROM_RAW_PARTS_PATH is required
#endif
#ifndef R_CODEGEN_ASYNC_CORE_SLICE_FROM_RAW_PARTS_PATH
#error R_CODEGEN_ASYNC_CORE_SLICE_FROM_RAW_PARTS_PATH is required
#endif
#ifndef R_CODEGEN_CORE_VOLATILE_PATH
#error R_CODEGEN_CORE_VOLATILE_PATH is required
#endif
#ifndef R_CODEGEN_ASYNC_CORE_VOLATILE_PATH
#error R_CODEGEN_ASYNC_CORE_VOLATILE_PATH is required
#endif
#ifndef R_CODEGEN_CORE_ADOPT_RELEASE_PATH
#error R_CODEGEN_CORE_ADOPT_RELEASE_PATH is required
#endif
#ifndef R_CODEGEN_ASYNC_CORE_ADOPT_RELEASE_PATH
#error R_CODEGEN_ASYNC_CORE_ADOPT_RELEASE_PATH is required
#endif
#ifndef R_CODEGEN_CORE_ADOPT_NONTRIVIAL_PATH
#error R_CODEGEN_CORE_ADOPT_NONTRIVIAL_PATH is required
#endif
#ifndef R_CODEGEN_ASYNC_CORE_ADOPT_NONTRIVIAL_PATH
#error R_CODEGEN_ASYNC_CORE_ADOPT_NONTRIVIAL_PATH is required
#endif
#ifndef R_CODEGEN_CORE_ADOPT_TYPE_GLUE_PATH
#error R_CODEGEN_CORE_ADOPT_TYPE_GLUE_PATH is required
#endif
#ifndef R_CODEGEN_CORE_ADOPT_RUNTIME_VALUES_PATH
#error R_CODEGEN_CORE_ADOPT_RUNTIME_VALUES_PATH is required
#endif
#ifndef R_CODEGEN_CORE_ADOPT_NAMED_STANDARD_COPY_PATH
#error R_CODEGEN_CORE_ADOPT_NAMED_STANDARD_COPY_PATH is required
#endif
#ifndef R_CODEGEN_CORE_ADOPT_NAMED_STANDARD_LAYOUT_PATH
#error R_CODEGEN_CORE_ADOPT_NAMED_STANDARD_LAYOUT_PATH is required
#endif
#ifndef R_CODEGEN_NAMED_STANDARD_MOVE_ABI_PATH
#error R_CODEGEN_NAMED_STANDARD_MOVE_ABI_PATH is required
#endif
#ifndef R_CODEGEN_TARGET_MANIFEST_PATH
#error R_CODEGEN_TARGET_MANIFEST_PATH is required
#endif

typedef struct RCodegenBuffer {
    char *bytes;
    size_t length;
    size_t capacity;
    size_t call_count;
    size_t attempted_length;
    bool reject;
} RCodegenBuffer;

typedef struct RCodegenAllocator {
    size_t call_count;
    size_t fail_at;
    size_t live_count;
} RCodegenAllocator;

static int failures;

#define R_CODEGEN_CHECK(condition)                                                                 \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            (void)fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);    \
            failures += 1;                                                                         \
        }                                                                                          \
    } while (false)

static size_t r_codegen_count_occurrences(const char *text, const char *needle) {
    const size_t needle_length = strlen(needle);
    size_t count = 0U;

    if ((text == NULL) || (needle_length == 0U)) {
        return 0U;
    }
    while ((text = strstr(text, needle)) != NULL) {
        count += 1U;
        text += needle_length;
    }
    return count;
}

/*
 * A gate forwards to its actual helper. Entry gates (R-FUNC-0004) check the static entry
 * bound once; type-glue gates carry no stack check at all.
 */
static void r_codegen_check_stack_gate_mode(const char *generated,
                                            const char *actual_name,
                                            bool expects_entry) {
    char actual_call[160];
    char gate_call[160];
    char stack_frame[192];
    char legacy_frame[192];
    int written;

    R_CODEGEN_CHECK(generated != NULL);
    R_CODEGEN_CHECK(actual_name != NULL);
    if ((generated == NULL) || (actual_name == NULL)) {
        return;
    }
    written = snprintf(actual_call, sizeof(actual_call), "%s(", actual_name);
    R_CODEGEN_CHECK((written > 0) && ((size_t)written < sizeof(actual_call)));
    if ((written <= 0) || ((size_t)written >= sizeof(actual_call))) {
        return;
    }
    written = snprintf(gate_call, sizeof(gate_call), "%s_gate(", actual_name);
    R_CODEGEN_CHECK((written > 0) && ((size_t)written < sizeof(gate_call)));
    if ((written <= 0) || ((size_t)written >= sizeof(gate_call))) {
        return;
    }
    written = snprintf(stack_frame, sizeof(stack_frame), "R_STACK_ENTRY(%s)", actual_name);
    R_CODEGEN_CHECK((written > 0) && ((size_t)written < sizeof(stack_frame)));
    if ((written <= 0) || ((size_t)written >= sizeof(stack_frame))) {
        return;
    }
    written = snprintf(legacy_frame, sizeof(legacy_frame), "R_STACK_FRAME(%s)", actual_name);
    R_CODEGEN_CHECK((written > 0) && ((size_t)written < sizeof(legacy_frame)));
    if ((written <= 0) || ((size_t)written >= sizeof(legacy_frame))) {
        return;
    }
    R_CODEGEN_CHECK(r_codegen_count_occurrences(generated, actual_call) >= 2U);
    R_CODEGEN_CHECK(r_codegen_count_occurrences(generated, gate_call) >= 1U);
    R_CODEGEN_CHECK((strstr(generated, stack_frame) != NULL) == expects_entry);
    R_CODEGEN_CHECK(strstr(generated, legacy_frame) == NULL);
}

static void r_codegen_check_stack_gate(const char *generated, const char *actual_name) {
    r_codegen_check_stack_gate_mode(generated, actual_name, true);
}

static void r_codegen_check_glue_gate(const char *generated, const char *actual_name) {
    r_codegen_check_stack_gate_mode(generated, actual_name, false);
}

static void r_codegen_check_hosted_stack_bootstrap(const char *generated) {
    const char *main_function;
    const char *initialize;
    const char *stack_panic;
    const char *zero_preflight;
    const char *hosted_start;

    R_CODEGEN_CHECK(generated != NULL);
    if (generated == NULL) {
        return;
    }
    main_function = strstr(generated, "int main(int argc, char *argv[])");
    initialize = main_function == NULL
                     ? NULL
                     : strstr(main_function, "r_runtime_stack_initialize_current_thread()");
    stack_panic =
        initialize == NULL ? NULL : strstr(initialize, "R_RUNTIME_PANIC_STACK_EXHAUSTION");
    zero_preflight =
        stack_panic == NULL ? NULL : strstr(stack_panic, "r_runtime_stack_require(0U,");
    hosted_start = zero_preflight == NULL
                       ? NULL
                       : strstr(zero_preflight, "r_runtime_hosted_start(argc, argv)");

    R_CODEGEN_CHECK(main_function != NULL);
    R_CODEGEN_CHECK(initialize != NULL);
    R_CODEGEN_CHECK(stack_panic != NULL);
    R_CODEGEN_CHECK(zero_preflight != NULL);
    R_CODEGEN_CHECK(hosted_start != NULL);
    if ((main_function != NULL) && (initialize != NULL) && (stack_panic != NULL) &&
        (zero_preflight != NULL) && (hosted_start != NULL)) {
        R_CODEGEN_CHECK(main_function < initialize);
        R_CODEGEN_CHECK(initialize < stack_panic);
        R_CODEGEN_CHECK(stack_panic < zero_preflight);
        R_CODEGEN_CHECK(zero_preflight < hosted_start);
    }
}

static void *r_codegen_allocate(void *user_data, size_t size) {
    RCodegenAllocator *allocator = user_data;
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

static void r_codegen_free(void *user_data, void *pointer) {
    RCodegenAllocator *allocator = user_data;
    if (pointer != NULL) {
        R_CODEGEN_CHECK(allocator->live_count != 0U);
        if (allocator->live_count != 0U) {
            allocator->live_count -= 1U;
        }
        free(pointer);
    }
}

static bool r_codegen_write(void *user_data, const char *bytes, size_t length) {
    RCodegenBuffer *buffer = user_data;
    size_t required;
    size_t capacity;
    char *replacement;

    buffer->call_count += 1U;
    if (length <= (SIZE_MAX - buffer->attempted_length)) {
        buffer->attempted_length += length;
    } else {
        return false;
    }
    if (buffer->reject) {
        return false;
    }
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

static void r_codegen_dispose_buffer(RCodegenBuffer *buffer) {
    free(buffer->bytes);
    (void)memset(buffer, 0, sizeof(*buffer));
}

static bool r_codegen_read_file(const char *path, uint8_t **bytes, size_t *length) {
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

static bool r_codegen_add_file(RFrontendContext *context, const char *path) {
    uint8_t *bytes = NULL;
    size_t length = 0U;
    RSourceId source_id = R_SOURCE_ID_INVALID;
    bool success;

    if (!r_codegen_read_file(path, &bytes, &length)) {
        return false;
    }
    success = r_frontend_add_source(context, path, bytes, length, &source_id) == R_FRONTEND_OK;
    free(bytes);
    return success && (source_id != R_SOURCE_ID_INVALID);
}

static RFrontendContext *r_codegen_create_context(RCodegenAllocator *allocator) {
    RFrontendOptions options = r_frontend_default_options();
    if (allocator == NULL) {
        return r_frontend_create(NULL);
    }
    options.allocate = r_codegen_allocate;
    options.free = r_codegen_free;
    options.allocator_user_data = allocator;
    return r_frontend_create(&options);
}

static RFrontendContext *r_codegen_build_pair(RCodegenAllocator *allocator, bool reverse_order) {
    RFrontendContext *context = r_codegen_create_context(allocator);
    if (context == NULL) {
        return NULL;
    }
    if (reverse_order) {
        if (!r_codegen_add_file(context, R_CODEGEN_MAIN_PATH) ||
            !r_codegen_add_file(context, R_CODEGEN_MATH_PATH)) {
            r_frontend_destroy(context);
            return NULL;
        }
    } else if (!r_codegen_add_file(context, R_CODEGEN_MATH_PATH) ||
               !r_codegen_add_file(context, R_CODEGEN_MAIN_PATH)) {
        r_frontend_destroy(context);
        return NULL;
    }
    if (r_frontend_analyze(context) != R_FRONTEND_OK) {
        r_frontend_destroy(context);
        return NULL;
    }
    return context;
}

static RFrontendContext *r_codegen_build_constexpr_string_pool(bool reverse_order) {
    RFrontendContext *context = r_codegen_create_context(NULL);
    if (context == NULL) {
        return NULL;
    }
    if (reverse_order) {
        if (!r_codegen_add_file(context, R_CODEGEN_CONSTEXPR_STRING_POOL_PATH) ||
            !r_codegen_add_file(context, R_CODEGEN_CONSTEXPR_STRING_POOL_SUPPORT_PATH)) {
            r_frontend_destroy(context);
            return NULL;
        }
    } else if (!r_codegen_add_file(context, R_CODEGEN_CONSTEXPR_STRING_POOL_SUPPORT_PATH) ||
               !r_codegen_add_file(context, R_CODEGEN_CONSTEXPR_STRING_POOL_PATH)) {
        r_frontend_destroy(context);
        return NULL;
    }
    if (r_frontend_analyze(context) != R_FRONTEND_OK) {
        r_frontend_destroy(context);
        return NULL;
    }
    return context;
}

static bool r_codegen_add_text(RFrontendContext *context, const char *name, const char *source) {
    RSourceId source_id = R_SOURCE_ID_INVALID;
    return (r_frontend_add_source(
                context, name, (const uint8_t *)source, strlen(source), &source_id) ==
            R_FRONTEND_OK) &&
           (source_id != R_SOURCE_ID_INVALID);
}

static RFrontendContext *r_codegen_build_value_types(RCodegenAllocator *allocator,
                                                     bool reverse_order) {
    static const char support_source[] = "module test.codegen.value_support;\n"
                                         "protected o<u32> unused() {\n"
                                         "    o<u32> value = o::none;\n"
                                         "    return value;\n"
                                         "}\n";
    RFrontendContext *context = r_codegen_create_context(allocator);
    if (context == NULL) {
        return NULL;
    }
    if (reverse_order) {
        if (!r_codegen_add_file(context, R_CODEGEN_VALUE_TYPES_PATH) ||
            !r_codegen_add_text(context, "value_support.r", support_source)) {
            r_frontend_destroy(context);
            return NULL;
        }
    } else if (!r_codegen_add_text(context, "value_support.r", support_source) ||
               !r_codegen_add_file(context, R_CODEGEN_VALUE_TYPES_PATH)) {
        r_frontend_destroy(context);
        return NULL;
    }
    if (r_frontend_analyze(context) != R_FRONTEND_OK) {
        r_frontend_destroy(context);
        return NULL;
    }
    return context;
}

static bool r_codegen_buffers_equal(const RCodegenBuffer *left, const RCodegenBuffer *right) {
    return (left->length == right->length) &&
           ((left->length == 0U) || (memcmp(left->bytes, right->bytes, left->length) == 0));
}

static void r_codegen_test_golden(void) {
    RFrontendContext *context = r_codegen_create_context(NULL);
    RCodegenBuffer generated = {0};
    uint8_t *expected = NULL;
    size_t expected_length = 0U;

    R_CODEGEN_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_CODEGEN_CHECK(r_codegen_add_file(context, R_CODEGEN_GOLDEN_SOURCE_PATH));
    R_CODEGEN_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &generated) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(generated.call_count == 1U);
    r_codegen_check_hosted_stack_bootstrap(generated.bytes);
    R_CODEGEN_CHECK(r_codegen_read_file(R_CODEGEN_GOLDEN_PATH, &expected, &expected_length));
    if (expected != NULL) {
        R_CODEGEN_CHECK(generated.length == expected_length);
        R_CODEGEN_CHECK((generated.length == expected_length) &&
                        (memcmp(generated.bytes, expected, expected_length) == 0));
    }
    free(expected);
    r_codegen_dispose_buffer(&generated);
    r_frontend_destroy(context);
}

static void r_codegen_test_target_manifest_identity(void) {
    static const char source[] = "module test.codegen.target_manifest;\n"
                                 "i32 main() {\n"
                                 "    return 0;\n"
                                 "}\n";
    RFrontendContext *context = r_codegen_create_context(NULL);
    RFrontendArtifactOptions options;
    RCodegenBuffer generated = {0};
    RCodegenBuffer rejected = {0};
    uint8_t *manifest = NULL;
    uint8_t *corrupted = NULL;
    size_t manifest_length = 0U;

    R_CODEGEN_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_CODEGEN_CHECK(r_codegen_add_text(context, "target_manifest.r", source));
    R_CODEGEN_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(
        r_codegen_read_file(R_CODEGEN_TARGET_MANIFEST_PATH, &manifest, &manifest_length));
    R_CODEGEN_CHECK(manifest_length != 0U);
    if ((manifest == NULL) || (manifest_length == 0U)) {
        goto cleanup;
    }
    (void)memset(&options, 0, sizeof(options));
    options.target_manifest = manifest;
    options.target_manifest_length = manifest_length;
    R_CODEGEN_CHECK(r_frontend_emit_c17_with_options(
                        context, &options, r_codegen_write, &generated) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(generated.call_count == 1U);
    R_CODEGEN_CHECK((generated.bytes != NULL) &&
                    (strstr(generated.bytes, "#include \"r_runtime_target_abi.h\"") != NULL));

    corrupted = malloc(manifest_length);
    R_CODEGEN_CHECK(corrupted != NULL);
    if (corrupted != NULL) {
        (void)memcpy(corrupted, manifest, manifest_length);
        corrupted[manifest_length / 2U] ^= UINT8_C(1);
        options.target_manifest = corrupted;
        R_CODEGEN_CHECK(
            r_frontend_emit_c17_with_options(context, &options, r_codegen_write, &rejected) ==
            R_FRONTEND_INVALID_ARGUMENT);
        R_CODEGEN_CHECK(rejected.call_count == 0U);
    }

    options.target_manifest = NULL;
    options.target_manifest_length = manifest_length;
    R_CODEGEN_CHECK(
        r_frontend_emit_c17_with_options(context, &options, r_codegen_write, &rejected) ==
        R_FRONTEND_INVALID_ARGUMENT);
    R_CODEGEN_CHECK(rejected.call_count == 0U);

cleanup:
    free(corrupted);
    free(manifest);
    r_codegen_dispose_buffer(&generated);
    r_codegen_dispose_buffer(&rejected);
    r_frontend_destroy(context);
}

static void r_codegen_test_determinism(void) {
    RFrontendContext *forward = r_codegen_build_pair(NULL, false);
    RFrontendContext *reverse = r_codegen_build_pair(NULL, true);
    RCodegenBuffer forward_output = {0};
    RCodegenBuffer reverse_output = {0};

    R_CODEGEN_CHECK(forward != NULL);
    R_CODEGEN_CHECK(reverse != NULL);
    if ((forward != NULL) && (reverse != NULL)) {
        const RFrontendStatus forward_status =
            r_frontend_emit_c17(forward, r_codegen_write, &forward_output);
        const RFrontendStatus reverse_status =
            r_frontend_emit_c17(reverse, r_codegen_write, &reverse_output);
        R_CODEGEN_CHECK(forward_status == R_FRONTEND_OK);
        R_CODEGEN_CHECK(reverse_status == R_FRONTEND_OK);
        R_CODEGEN_CHECK(forward_output.call_count == 1U);
        R_CODEGEN_CHECK(reverse_output.call_count == 1U);
        R_CODEGEN_CHECK(forward_output.bytes != NULL);
        R_CODEGEN_CHECK(reverse_output.bytes != NULL);
        if ((forward_status == R_FRONTEND_OK) && (reverse_status == R_FRONTEND_OK) &&
            (forward_output.bytes != NULL) && (reverse_output.bytes != NULL)) {
            R_CODEGEN_CHECK(r_codegen_buffers_equal(&forward_output, &reverse_output));
            R_CODEGEN_CHECK(strstr(forward_output.bytes, "INT32_MIN") != NULL);
            R_CODEGEN_CHECK(strstr(forward_output.bytes, "for (;;) {") != NULL);
        }
    }
    r_codegen_dispose_buffer(&forward_output);
    r_codegen_dispose_buffer(&reverse_output);
    r_frontend_destroy(forward);
    r_frontend_destroy(reverse);
}

static void r_codegen_test_format_determinism(void) {
    static const char *const sources[] = {
        "module fmt.a; std.string::string text(i32 n) throws std.alloc::alloc_error { "
        "std.string::string prefix = std.string::from_str(\"value\"); return f\"{prefix}{n}\"; }",
        "module fmt.b; import fmt.a::{text}; i32 main() { try { "
        "std.string::string s = text(4); std.format::format t = f\"{s} {1}\"; "
        "std.string::string r = t.format(2); drop r; "
        "} catch (std.alloc::alloc_error error) { return 1; } return 0; }"};
    static const char *const paths[] = {"fmt-a.r", "fmt-b.r"};
    RFrontendContext *contexts[2] = {NULL, NULL};
    RCodegenBuffer generated[2] = {{0}, {0}};
    RCodegenBuffer interfaces[2] = {{0}, {0}};
    RCodegenBuffer plans[2] = {{0}, {0}};
    const RFrontendArtifactOptions options = {.entry = "fmt.b", .profile = "hosted"};

    for (size_t order = 0U; order < 2U; ++order) {
        contexts[order] = r_codegen_create_context(NULL);
        R_CODEGEN_CHECK(contexts[order] != NULL);
        if (contexts[order] == NULL) {
            goto cleanup;
        }
        for (size_t index = 0U; index < 2U; ++index) {
            const size_t selected = order == 0U ? index : 1U - index;
            RSourceId source;
            R_CODEGEN_CHECK(r_frontend_add_source(contexts[order],
                                                  paths[selected],
                                                  (const uint8_t *)sources[selected],
                                                  strlen(sources[selected]),
                                                  &source) == R_FRONTEND_OK);
        }
        R_CODEGEN_CHECK(r_frontend_analyze(contexts[order]) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_frontend_lower_mir(contexts[order]) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_frontend_diagnostic_count(contexts[order]) == 0U);
        R_CODEGEN_CHECK(r_frontend_emit_c17(contexts[order], r_codegen_write, &generated[order]) ==
                        R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_frontend_dump_interface(
                            contexts[order], &options, r_codegen_write, &interfaces[order]) ==
                        R_FRONTEND_OK);
        R_CODEGEN_CHECK(
            r_frontend_dump_link_plan(contexts[order], &options, r_codegen_write, &plans[order]) ==
            R_FRONTEND_OK);
    }
    R_CODEGEN_CHECK(r_codegen_buffers_equal(&generated[0], &generated[1]));
    R_CODEGEN_CHECK(r_codegen_buffers_equal(&interfaces[0], &interfaces[1]));
    R_CODEGEN_CHECK(r_codegen_buffers_equal(&plans[0], &plans[1]));
    R_CODEGEN_CHECK(plans[0].bytes != NULL && strstr(plans[0].bytes, "r_std_format") != NULL);
    R_CODEGEN_CHECK(plans[0].bytes != NULL && strstr(plans[0].bytes, "r_std_string") != NULL);

cleanup:
    for (size_t order = 0U; order < 2U; ++order) {
        r_codegen_dispose_buffer(&generated[order]);
        r_codegen_dispose_buffer(&interfaces[order]);
        r_codegen_dispose_buffer(&plans[order]);
        r_frontend_destroy(contexts[order]);
    }
}

static void r_codegen_test_constexpr_string_pool(void) {
    static const char first_definition[] =
        "static const uint8_t r_program_string_00000001[] = \"\\141\";";
    static const char prefix_extension_definition[] =
        "static const uint8_t r_program_string_00000002[] = \"\\141\\141\";";
    static const char later_byte_definition[] =
        "static const uint8_t r_program_string_00000003[] = \"\\142\";";
    static const char shared_definition[] = "static const uint8_t r_program_string_00000004[]";
    static const char shared_octets[] =
        "\"\\160\\157\\157\\154\\055\\163\\150\\141\\162\\145\\144\"";
    static const char dead_octets[] = "\"\\160\\157\\157\\154\\055\\144\\145\\141\\144\"";
    RFrontendContext *forward = r_codegen_build_constexpr_string_pool(false);
    RFrontendContext *reverse = r_codegen_build_constexpr_string_pool(true);
    RCodegenBuffer forward_output = {0};
    RCodegenBuffer reverse_output = {0};
    RCodegenBuffer repeated = {0};

    R_CODEGEN_CHECK(forward != NULL);
    R_CODEGEN_CHECK(reverse != NULL);
    if ((forward != NULL) && (reverse != NULL)) {
        R_CODEGEN_CHECK(r_frontend_diagnostic_count(forward) == 0U);
        R_CODEGEN_CHECK(r_frontend_diagnostic_count(reverse) == 0U);
        R_CODEGEN_CHECK(r_frontend_lower_mir(forward) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_frontend_lower_mir(reverse) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_frontend_diagnostic_count(forward) == 0U);
        R_CODEGEN_CHECK(r_frontend_diagnostic_count(reverse) == 0U);
        R_CODEGEN_CHECK(r_frontend_emit_c17(forward, r_codegen_write, &forward_output) ==
                        R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_frontend_emit_c17(reverse, r_codegen_write, &reverse_output) ==
                        R_FRONTEND_OK);
    }
    R_CODEGEN_CHECK(forward_output.call_count == 1U);
    R_CODEGEN_CHECK(reverse_output.call_count == 1U);
    R_CODEGEN_CHECK(forward_output.bytes != NULL);
    R_CODEGEN_CHECK(reverse_output.bytes != NULL);
    R_CODEGEN_CHECK(r_codegen_buffers_equal(&forward_output, &reverse_output));
    if (forward_output.bytes != NULL) {
        const char *first = strstr(forward_output.bytes, first_definition);
        const char *prefix_extension = strstr(forward_output.bytes, prefix_extension_definition);
        const char *later_byte = strstr(forward_output.bytes, later_byte_definition);
        const char *shared = strstr(forward_output.bytes, shared_definition);
        const char *first_function_definition = strstr(forward_output.bytes, ") {\n");

        R_CODEGEN_CHECK(first != NULL);
        R_CODEGEN_CHECK(prefix_extension != NULL);
        R_CODEGEN_CHECK(later_byte != NULL);
        R_CODEGEN_CHECK(shared != NULL);
        R_CODEGEN_CHECK(first_function_definition != NULL);
        if ((first != NULL) && (prefix_extension != NULL) && (later_byte != NULL) &&
            (shared != NULL) && (first_function_definition != NULL)) {
            R_CODEGEN_CHECK(first < prefix_extension);
            R_CODEGEN_CHECK(prefix_extension < later_byte);
            R_CODEGEN_CHECK(later_byte < shared);
            R_CODEGEN_CHECK(shared < first_function_definition);
        }
        R_CODEGEN_CHECK(r_codegen_count_occurrences(
                            forward_output.bytes, "static const uint8_t r_program_string_") == 4U);
        R_CODEGEN_CHECK(r_codegen_count_occurrences(forward_output.bytes, shared_octets) == 1U);
        R_CODEGEN_CHECK(strstr(forward_output.bytes, dead_octets) == NULL);
        R_CODEGEN_CHECK(r_codegen_count_occurrences(forward_output.bytes,
                                                    ".r_data = r_program_string_00000001") == 1U);
        R_CODEGEN_CHECK(r_codegen_count_occurrences(forward_output.bytes,
                                                    ".r_data = r_program_string_00000002") == 1U);
        R_CODEGEN_CHECK(r_codegen_count_occurrences(forward_output.bytes,
                                                    ".r_data = r_program_string_00000003") == 1U);
        R_CODEGEN_CHECK(r_codegen_count_occurrences(forward_output.bytes,
                                                    ".r_data = r_program_string_00000004") == 4U);
        R_CODEGEN_CHECK(strstr(forward_output.bytes, "static const uint8_t r_s") == NULL);
        R_CODEGEN_CHECK(strstr(forward_output.bytes, "static const uint8_t r_async_s") == NULL);
    }
    if (forward != NULL) {
        R_CODEGEN_CHECK(r_frontend_emit_c17(forward, r_codegen_write, &repeated) == R_FRONTEND_OK);
    }
    R_CODEGEN_CHECK(repeated.call_count == 1U);
    R_CODEGEN_CHECK(r_codegen_buffers_equal(&forward_output, &repeated));
    r_codegen_dispose_buffer(&forward_output);
    r_codegen_dispose_buffer(&reverse_output);
    r_codegen_dispose_buffer(&repeated);
    r_frontend_destroy(forward);
    r_frontend_destroy(reverse);
}

static void r_codegen_test_literal_octets(void) {
    static const char source[] = "module test.codegen.literal_octets;\n"
                                 "i32 main() {\n"
                                 "    constexpr str value = \"ABCD\";\n"
                                 "    usize length = len(value);\n"
                                 "    if (length == 4) {\n"
                                 "        return 0;\n"
                                 "    } else {\n"
                                 "        return 1;\n"
                                 "    }\n"
                                 "}\n";
    static const uint8_t expected[] = "\000\177\200\377";
    RFrontendContext *context = r_codegen_create_context(NULL);
    RCodegenBuffer generated = {0};
    RCodegenBuffer repeated = {0};
    RInternEntry *literal = NULL;
    size_t entry_index;

    R_CODEGEN_CHECK(sizeof(expected) == 5U);
    R_CODEGEN_CHECK(expected[0] == UINT8_C(0));
    R_CODEGEN_CHECK(expected[1] == UINT8_C(0x7f));
    R_CODEGEN_CHECK(expected[2] == UINT8_C(0x80));
    R_CODEGEN_CHECK(expected[3] == UINT8_C(0xff));
    R_CODEGEN_CHECK(expected[4] == UINT8_C(0));
    R_CODEGEN_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_CODEGEN_CHECK(r_codegen_add_text(context, "literal_octets.r", source));
    R_CODEGEN_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    for (entry_index = 0U; entry_index < context->intern_count; ++entry_index) {
        RInternEntry *entry = &context->intern_entries[entry_index];
        if ((entry->length == 4U) && (memcmp(entry->bytes, "ABCD", 4U) == 0)) {
            literal = entry;
            break;
        }
    }
    R_CODEGEN_CHECK(literal != NULL);
    if (literal != NULL) {
        (void)memcpy(literal->bytes, expected, 4U);
        R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &generated) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(generated.bytes != NULL);
        if (generated.bytes != NULL) {
            const char *pool_definition =
                strstr(generated.bytes, "static const uint8_t r_program_string_00000001[]");
            const char *first_function_definition = strstr(generated.bytes, ") {\n");

            R_CODEGEN_CHECK(pool_definition != NULL);
            R_CODEGEN_CHECK(first_function_definition != NULL);
            if ((pool_definition != NULL) && (first_function_definition != NULL)) {
                R_CODEGEN_CHECK(pool_definition < first_function_definition);
            }
            R_CODEGEN_CHECK(r_codegen_count_occurrences(
                                generated.bytes, "static const uint8_t r_program_string_") == 1U);
            R_CODEGEN_CHECK(strstr(generated.bytes, "\"\\000\\177\\200\\377\";") != NULL);
            R_CODEGEN_CHECK(strstr(generated.bytes, ".r_len = (size_t)UINT64_C(4),") != NULL);
        }
        R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &repeated) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(repeated.call_count == 1U);
        R_CODEGEN_CHECK(r_codegen_buffers_equal(&generated, &repeated));
    }
    r_codegen_dispose_buffer(&generated);
    r_codegen_dispose_buffer(&repeated);
    r_frontend_destroy(context);
}

static void r_codegen_test_failure_atomicity(void) {
    RCodegenAllocator allocator = {0};
    RFrontendContext *context = r_codegen_build_pair(&allocator, false);
    RCodegenBuffer baseline = {0};
    size_t live_baseline;
    size_t allocation_start;
    size_t emission_allocations;
    size_t offset;

    R_CODEGEN_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    live_baseline = allocator.live_count;
    allocation_start = allocator.call_count;
    R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &baseline) == R_FRONTEND_OK);
    emission_allocations = allocator.call_count - allocation_start;
    R_CODEGEN_CHECK(emission_allocations != 0U);
    R_CODEGEN_CHECK(baseline.call_count == 1U);
    R_CODEGEN_CHECK(allocator.live_count == live_baseline);

    for (offset = 1U; offset <= emission_allocations; ++offset) {
        RCodegenBuffer failed = {0};
        RCodegenBuffer retry = {0};
        const size_t retry_live_baseline = allocator.live_count;
        allocator.fail_at = allocator.call_count + offset;
        R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &failed) ==
                        R_FRONTEND_OUT_OF_MEMORY);
        R_CODEGEN_CHECK(failed.call_count == 0U);
        R_CODEGEN_CHECK(failed.length == 0U);
        R_CODEGEN_CHECK(allocator.live_count == retry_live_baseline);
        allocator.fail_at = 0U;
        R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &retry) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(retry.call_count == 1U);
        R_CODEGEN_CHECK(r_codegen_buffers_equal(&baseline, &retry));
        R_CODEGEN_CHECK(allocator.live_count == retry_live_baseline);
        r_codegen_dispose_buffer(&failed);
        r_codegen_dispose_buffer(&retry);
    }

    {
        RCodegenBuffer rejected = {0};
        RCodegenBuffer retry = {0};
        rejected.reject = true;
        R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &rejected) ==
                        R_FRONTEND_IO_ERROR);
        R_CODEGEN_CHECK(rejected.call_count == 1U);
        R_CODEGEN_CHECK(rejected.attempted_length == baseline.length);
        R_CODEGEN_CHECK(rejected.length == 0U);
        R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &retry) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_codegen_buffers_equal(&baseline, &retry));
        r_codegen_dispose_buffer(&rejected);
        r_codegen_dispose_buffer(&retry);
    }

    {
        size_t symbol_index;
        RSemanticSymbol *poisoned = NULL;
        RCodegenBuffer output = {0};
        for (symbol_index = 0U; symbol_index < context->semantic_symbol_count; ++symbol_index) {
            if (context->semantic_symbols[symbol_index].kind == R_SEMANTIC_SYMBOL_FUNCTION) {
                poisoned = &context->semantic_symbols[symbol_index];
                break;
            }
        }
        R_CODEGEN_CHECK(poisoned != NULL);
        if (poisoned != NULL) {
            poisoned->poisoned = true;
            R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &output) ==
                            R_FRONTEND_NOT_LOWERABLE);
            R_CODEGEN_CHECK(output.call_count == 0U);
            poisoned->poisoned = false;
        }
        r_codegen_dispose_buffer(&output);
    }

    allocator.fail_at = 0U;
    r_codegen_dispose_buffer(&baseline);
    r_frontend_destroy(context);
    R_CODEGEN_CHECK(allocator.live_count == 0U);
}

static void r_codegen_test_value_types(void) {
    RCodegenAllocator allocator = {0};
    RFrontendContext *forward = r_codegen_build_value_types(&allocator, false);
    RFrontendContext *reverse = r_codegen_build_value_types(NULL, true);
    RCodegenBuffer baseline = {0};
    RCodegenBuffer reversed = {0};
    size_t allocation_start;
    size_t allocation_count;
    size_t live_baseline;
    size_t offset;
    RHirNode *maximum_character_literal = NULL;

    R_CODEGEN_CHECK(forward != NULL);
    R_CODEGEN_CHECK(reverse != NULL);
    if ((forward == NULL) || (reverse == NULL)) {
        r_frontend_destroy(forward);
        r_frontend_destroy(reverse);
        return;
    }
    live_baseline = allocator.live_count;
    allocation_start = allocator.call_count;
    R_CODEGEN_CHECK(r_frontend_emit_c17(forward, r_codegen_write, &baseline) == R_FRONTEND_OK);
    allocation_count = allocator.call_count - allocation_start;
    R_CODEGEN_CHECK(r_frontend_emit_c17(reverse, r_codegen_write, &reversed) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(r_codegen_buffers_equal(&baseline, &reversed));
    R_CODEGEN_CHECK(baseline.bytes != NULL);
    if (baseline.bytes != NULL) {
        R_CODEGEN_CHECK(strstr(baseline.bytes, "R_RUNTIME_PANIC_BOUNDS") != NULL);
        R_CODEGEN_CHECK(strstr(baseline.bytes, ".r_payload.r_error_00000001") != NULL);
        R_CODEGEN_CHECK(strstr(baseline.bytes, ".r_payload.r_some") != NULL);
        R_CODEGEN_CHECK(strstr(baseline.bytes, ".r_len") != NULL);
        R_CODEGEN_CHECK(strstr(baseline.bytes, "switch (") != NULL);
        R_CODEGEN_CHECK(strstr(baseline.bytes, "int8_t ") != NULL);
        R_CODEGEN_CHECK(strstr(baseline.bytes, "int16_t ") != NULL);
        R_CODEGEN_CHECK(strstr(baseline.bytes, "int64_t ") != NULL);
        R_CODEGEN_CHECK(strstr(baseline.bytes, "ptrdiff_t ") != NULL);
        R_CODEGEN_CHECK(strstr(baseline.bytes, "uint8_t ") != NULL);
        R_CODEGEN_CHECK(strstr(baseline.bytes, "uint16_t ") != NULL);
        R_CODEGEN_CHECK(strstr(baseline.bytes, "uint64_t ") != NULL);
        R_CODEGEN_CHECK(strstr(baseline.bytes, "size_t ") != NULL);
        R_CODEGEN_CHECK(strstr(baseline.bytes, "float ") != NULL);
        R_CODEGEN_CHECK(strstr(baseline.bytes, "double ") != NULL);
        R_CODEGEN_CHECK(strstr(baseline.bytes, "INT8_MIN") != NULL);
        R_CODEGEN_CHECK(strstr(baseline.bytes, "INT16_MIN") != NULL);
        R_CODEGEN_CHECK(strstr(baseline.bytes, "INT64_MIN") != NULL);
        R_CODEGEN_CHECK(strstr(baseline.bytes, "PTRDIFF_MIN") != NULL);
        R_CODEGEN_CHECK(strstr(baseline.bytes, "UINT64_C(18446744073709551615)") != NULL);
        R_CODEGEN_CHECK(strstr(baseline.bytes, "UINT32_C(128578)") != NULL);
        R_CODEGEN_CHECK(strstr(baseline.bytes, "UINT32_C(128640)") != NULL);
        R_CODEGEN_CHECK(strstr(baseline.bytes, "UINT32_C(1114111)") != NULL);
        R_CODEGEN_CHECK(strstr(baseline.bytes, "r_a00000004 r_v00000005_00003359 = {0};") != NULL);
    }
    R_CODEGEN_CHECK(allocation_count != 0U);
    R_CODEGEN_CHECK(allocator.live_count == live_baseline);
    for (offset = 1U; offset <= allocation_count; ++offset) {
        RCodegenBuffer failed = {0};
        RCodegenBuffer retry = {0};
        allocator.fail_at = allocator.call_count + offset;
        R_CODEGEN_CHECK(r_frontend_emit_c17(forward, r_codegen_write, &failed) ==
                        R_FRONTEND_OUT_OF_MEMORY);
        R_CODEGEN_CHECK(failed.call_count == 0U);
        R_CODEGEN_CHECK(failed.length == 0U);
        R_CODEGEN_CHECK(allocator.live_count == live_baseline);
        allocator.fail_at = 0U;
        R_CODEGEN_CHECK(r_frontend_emit_c17(forward, r_codegen_write, &retry) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_codegen_buffers_equal(&baseline, &retry));
        R_CODEGEN_CHECK(allocator.live_count == live_baseline);
        r_codegen_dispose_buffer(&failed);
        r_codegen_dispose_buffer(&retry);
    }
    for (offset = 0U; offset < forward->hir_node_count; ++offset) {
        RHirNode *node = &forward->hir_nodes[offset];
        const RSemanticType *type = r_semantic_type(forward, node->type);

        if ((node->kind == R_HIR_LITERAL) && (type != NULL) &&
            (type->kind == R_SEMANTIC_TYPE_CHAR) && (node->integer_value == UINT64_C(1114111))) {
            maximum_character_literal = node;
            break;
        }
    }
    R_CODEGEN_CHECK(maximum_character_literal != NULL);
    if (maximum_character_literal != NULL) {
        RCodegenBuffer rejected = {0};
        const uint64_t valid_value = maximum_character_literal->integer_value;

        maximum_character_literal->integer_value = UINT64_C(0xD800);
        R_CODEGEN_CHECK(r_frontend_emit_c17(forward, r_codegen_write, &rejected) ==
                        R_FRONTEND_INTERNAL_ERROR);
        R_CODEGEN_CHECK(rejected.call_count == 0U);
        R_CODEGEN_CHECK(rejected.length == 0U);
        maximum_character_literal->integer_value = valid_value;
        r_codegen_dispose_buffer(&rejected);
    }
    allocator.fail_at = 0U;
    r_codegen_dispose_buffer(&baseline);
    r_codegen_dispose_buffer(&reversed);
    r_frontend_destroy(forward);
    r_frontend_destroy(reverse);
    R_CODEGEN_CHECK(allocator.live_count == 0U);
}

typedef struct RCodegenArraySliceView {
    RHirNode *slice;
    RHirNode *source;
    RSemanticType *slice_type;
    RSemanticType *input_type;
    RSemanticType *array_type;
} RCodegenArraySliceView;

typedef enum RCodegenArraySliceMutation {
    R_CODEGEN_ARRAY_SLICE_ARITY = 0,
    R_CODEGEN_ARRAY_SLICE_BOUND,
    R_CODEGEN_ARRAY_SLICE_SOURCE_KIND,
    R_CODEGEN_ARRAY_SLICE_SOURCE_TYPE,
    R_CODEGEN_ARRAY_SLICE_INPUT_FLAGS,
    R_CODEGEN_ARRAY_SLICE_ARRAY_KIND,
    R_CODEGEN_ARRAY_SLICE_PROVENANCE,
    R_CODEGEN_ARRAY_SLICE_MUTATION_COUNT
} RCodegenArraySliceMutation;

static RFrontendContext *r_codegen_build_array_slice(void) {
    RFrontendContext *context = r_codegen_create_context(NULL);

    if ((context == NULL) || !r_codegen_add_file(context, R_CODEGEN_ARRAY_SLICE_PATH) ||
        (r_frontend_analyze(context) != R_FRONTEND_OK) ||
        (r_frontend_diagnostic_count(context) != 0U)) {
        r_frontend_destroy(context);
        return NULL;
    }
    return context;
}

static bool
r_codegen_find_array_slice(RFrontendContext *context, bool shared, RCodegenArraySliceView *view) {
    size_t index;

    if ((context == NULL) || (view == NULL)) {
        return false;
    }
    (void)memset(view, 0, sizeof(*view));
    for (index = 0U; index < context->hir_node_count; ++index) {
        RHirNode *node = &context->hir_nodes[index];
        RSemanticType *slice_type;
        RSemanticType *input_type;
        RSemanticType *array_type;
        const size_t child_index = (size_t)node->first_child;
        RHirNodeId source_id;

        if ((node->kind != R_HIR_SLICE) || (node->operation != R_TOKEN_KW_ARRAY) ||
            (node->type == R_TYPE_ID_INVALID) ||
            ((size_t)node->type > context->semantic_type_count)) {
            continue;
        }
        slice_type = &context->semantic_types[(size_t)node->type - 1U];
        if ((slice_type->kind != R_SEMANTIC_TYPE_SLICE) ||
            (((slice_type->flags & R_SEMANTIC_TYPE_FLAG_SHARED) != 0U) != shared)) {
            continue;
        }
        if ((node->child_count != UINT32_C(1)) || (child_index >= context->hir_child_count) ||
            (node->auxiliary_type == R_TYPE_ID_INVALID) ||
            ((size_t)node->auxiliary_type > context->semantic_type_count)) {
            return false;
        }
        source_id = context->hir_children[child_index];
        if ((source_id == R_HIR_NODE_ID_INVALID) || ((size_t)source_id > context->hir_node_count)) {
            return false;
        }
        input_type = &context->semantic_types[(size_t)node->auxiliary_type - 1U];
        if ((input_type->base == R_TYPE_ID_INVALID) ||
            ((size_t)input_type->base > context->semantic_type_count)) {
            return false;
        }
        array_type = &context->semantic_types[(size_t)input_type->base - 1U];
        if (view->slice != NULL) {
            return false;
        }
        view->slice = node;
        view->source = &context->hir_nodes[(size_t)source_id - 1U];
        view->slice_type = slice_type;
        view->input_type = input_type;
        view->array_type = array_type;
    }
    return (view->slice != NULL) && (view->source != NULL) && (view->slice_type != NULL) &&
           (view->input_type != NULL) && (view->array_type != NULL);
}

static void r_codegen_test_array_slice(void) {
    RFrontendContext *context = r_codegen_build_array_slice();
    RCodegenArraySliceView const_view;
    RCodegenArraySliceView mutable_view;
    RCodegenBuffer generated = {0};
    RCodegenBuffer repeated = {0};
    size_t mutation;

    R_CODEGEN_CHECK(context != NULL);
    if (context != NULL) {
        R_CODEGEN_CHECK(r_codegen_find_array_slice(context, true, &const_view));
        R_CODEGEN_CHECK(r_codegen_find_array_slice(context, false, &mutable_view));
        R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &generated) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(generated.call_count == 1U);
        R_CODEGEN_CHECK(generated.bytes != NULL);
        if (generated.bytes != NULL) {
            R_CODEGEN_CHECK(
                r_codegen_count_occurrences(generated.bytes, ".r_data = (const uint8_t *)") == 1U);
            R_CODEGEN_CHECK(
                r_codegen_count_occurrences(generated.bytes, ".r_data = (uint16_t *)") == 1U);
            R_CODEGEN_CHECK(r_codegen_count_occurrences(generated.bytes, "->length;") == 2U);
            R_CODEGEN_CHECK(strstr(generated.bytes, "r_std_array_as_slice") == NULL);
            R_CODEGEN_CHECK(strstr(generated.bytes, "memcpy(") == NULL);
        }
        R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &repeated) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_codegen_buffers_equal(&generated, &repeated));
    }
    r_codegen_dispose_buffer(&generated);
    r_codegen_dispose_buffer(&repeated);
    r_frontend_destroy(context);

    for (mutation = 0U; mutation < (size_t)R_CODEGEN_ARRAY_SLICE_MUTATION_COUNT; ++mutation) {
        RFrontendContext *invalid = r_codegen_build_array_slice();
        RCodegenArraySliceView invalid_view;
        RCodegenBuffer output = {0};

        R_CODEGEN_CHECK(invalid != NULL);
        if ((invalid == NULL) || !r_codegen_find_array_slice(invalid, true, &invalid_view)) {
            r_frontend_destroy(invalid);
            continue;
        }
        switch ((RCodegenArraySliceMutation)mutation) {
        case R_CODEGEN_ARRAY_SLICE_ARITY:
            invalid_view.slice->child_count = UINT32_C(0);
            break;
        case R_CODEGEN_ARRAY_SLICE_BOUND:
            invalid_view.slice->integer_value = UINT64_C(0);
            break;
        case R_CODEGEN_ARRAY_SLICE_SOURCE_KIND:
            invalid_view.source->kind = R_HIR_BINARY;
            break;
        case R_CODEGEN_ARRAY_SLICE_SOURCE_TYPE:
            invalid_view.source->type = invalid_view.slice->type;
            break;
        case R_CODEGEN_ARRAY_SLICE_INPUT_FLAGS:
            invalid_view.input_type->flags = R_SEMANTIC_TYPE_FLAG_NONE;
            break;
        case R_CODEGEN_ARRAY_SLICE_ARRAY_KIND:
            invalid_view.array_type->kind = R_SEMANTIC_TYPE_LIST;
            break;
        case R_CODEGEN_ARRAY_SLICE_PROVENANCE:
            invalid_view.slice->borrow_origin = R_SYMBOL_ID_INVALID;
            break;
        case R_CODEGEN_ARRAY_SLICE_MUTATION_COUNT:
        default:
            R_CODEGEN_CHECK(false);
            break;
        }
        R_CODEGEN_CHECK(r_frontend_emit_c17(invalid, r_codegen_write, &output) ==
                        R_FRONTEND_NOT_LOWERABLE);
        R_CODEGEN_CHECK(output.call_count == 0U);
        R_CODEGEN_CHECK(output.length == 0U);
        r_codegen_dispose_buffer(&output);
        r_frontend_destroy(invalid);
    }
}

static void r_codegen_test_sync_aggregate_destination(void) {
    RFrontendContext *context = r_codegen_create_context(NULL);
    RCodegenBuffer generated = {0};
    RCodegenBuffer repeated = {0};

    R_CODEGEN_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_CODEGEN_CHECK(r_codegen_add_file(context, R_CODEGEN_SYNC_AGGREGATE_DESTINATION_PATH));
    R_CODEGEN_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(r_frontend_diagnostic_count(context) == 0U);
    R_CODEGEN_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(r_frontend_diagnostic_count(context) == 0U);
    R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &generated) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(generated.call_count == 1U);
    R_CODEGEN_CHECK(generated.bytes != NULL);
    if (generated.bytes != NULL) {
        R_CODEGEN_CHECK(strstr(generated.bytes, "r_a00000002 r_v00000003_00000264 = {0};") != NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes, "r_d00000001 r_t") == NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes, "r_a00000002 r_t") == NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes,
                               "r_effect_out->r_payload.r_ok = r_v00000003_00000264;\n"
                               "    r_effect_out->r_tag = UINT32_C(0);") != NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes,
                               "(r_constexpr_str){.r_data = r_empty_program_string, "
                               ".r_len = (size_t)UINT64_C(0)}") != NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes, "r_a00000003 r_v00000001_00000476 = {0};") == NULL);
    }
    R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &repeated) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(r_codegen_buffers_equal(&generated, &repeated));
    r_codegen_dispose_buffer(&generated);
    r_codegen_dispose_buffer(&repeated);
    r_frontend_destroy(context);
}

typedef struct RCodegenAsyncOptionPathView {
    RSemanticType *option_type;
    RTypeId option_type_id;
    RMirInstruction *retained_local;
    RMirInstruction *after_local;
    RMirInstruction *await_instruction;
    RMirInstruction *post_await_move;
} RCodegenAsyncOptionPathView;

typedef enum RCodegenAsyncOptionPathMutation {
    R_CODEGEN_ASYNC_OPTION_PATH_BASE = 0,
    R_CODEGEN_ASYNC_OPTION_PATH_SECOND,
    R_CODEGEN_ASYNC_OPTION_PATH_LENGTH,
    R_CODEGEN_ASYNC_OPTION_PATH_FLAGS,
    R_CODEGEN_ASYNC_OPTION_PATH_BORROWED_PAYLOAD,
    R_CODEGEN_ASYNC_OPTION_PATH_MUTATION_COUNT
} RCodegenAsyncOptionPathMutation;

static RFrontendContext *r_codegen_build_async_option_path(void) {
    RFrontendContext *context = r_codegen_create_context(NULL);

    if ((context == NULL) || !r_codegen_add_file(context, R_CODEGEN_ASYNC_OPTION_PATH_PATH) ||
        (r_frontend_analyze(context) != R_FRONTEND_OK) ||
        (r_frontend_diagnostic_count(context) != 0U) ||
        (r_frontend_lower_mir(context) != R_FRONTEND_OK) ||
        (r_frontend_diagnostic_count(context) != 0U)) {
        r_frontend_destroy(context);
        return NULL;
    }
    return context;
}

static bool r_codegen_find_async_option_path(RFrontendContext *context,
                                             RCodegenAsyncOptionPathView *view) {
    size_t option_count = 0U;
    size_t await_index = SIZE_MAX;
    size_t move_index = SIZE_MAX;
    size_t index;

    if ((context == NULL) || (view == NULL)) {
        return false;
    }
    (void)memset(view, 0, sizeof(*view));
    for (index = 0U; index < context->semantic_type_count; ++index) {
        RSemanticType *type = &context->semantic_types[index];

        if (type->kind == R_SEMANTIC_TYPE_OPTION) {
            option_count += 1U;
            view->option_type = type;
            view->option_type_id = (RTypeId)(index + 1U);
        }
    }
    if ((option_count != 1U) || (view->option_type == NULL)) {
        return false;
    }
    for (index = 0U; index < context->mir_instruction_count; ++index) {
        RMirInstruction *instruction = &context->mir_instructions[index];

        if ((instruction->kind == R_MIR_INSTRUCTION_LOCAL) &&
            (instruction->type == view->option_type_id)) {
            if (instruction->place_ordinal == UINT32_C(3)) {
                view->retained_local = instruction;
            } else if (instruction->place_ordinal == UINT32_C(6)) {
                view->after_local = instruction;
            }
        } else if (instruction->kind == R_MIR_INSTRUCTION_AWAIT) {
            if (view->await_instruction != NULL) {
                return false;
            }
            view->await_instruction = instruction;
            await_index = index;
        } else if ((instruction->kind == R_MIR_INSTRUCTION_MOVE) &&
                   (instruction->type == view->option_type_id)) {
            if (view->post_await_move != NULL) {
                return false;
            }
            view->post_await_move = instruction;
            move_index = index;
        }
    }
    return (view->retained_local != NULL) && (view->after_local != NULL) &&
           (view->await_instruction != NULL) && (view->post_await_move != NULL) &&
           (await_index < move_index) &&
           (view->post_await_move->place_ordinal == view->retained_local->place_ordinal);
}

static void r_codegen_test_async_option_path(void) {
    RFrontendContext *context = r_codegen_build_async_option_path();
    RCodegenAsyncOptionPathView view;
    RCodegenBuffer generated = {0};
    RCodegenBuffer repeated = {0};
    size_t mutation;

    R_CODEGEN_CHECK(context != NULL);
    if (context != NULL) {
        const bool found = r_codegen_find_async_option_path(context, &view);

        R_CODEGEN_CHECK(found);
        if (found) {
            R_CODEGEN_CHECK(view.option_type->base != R_TYPE_ID_INVALID);
            R_CODEGEN_CHECK(view.option_type->second == R_TYPE_ID_INVALID);
            R_CODEGEN_CHECK(view.option_type->length == UINT64_C(0));
            R_CODEGEN_CHECK(view.option_type->flags == R_SEMANTIC_TYPE_FLAG_NONE);
            R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &generated) ==
                            R_FRONTEND_OK);
            R_CODEGEN_CHECK(generated.call_count == 1U);
            R_CODEGEN_CHECK(generated.bytes != NULL);
            if (generated.bytes != NULL) {
                R_CODEGEN_CHECK(strstr(generated.bytes, "RStdFsPath r_some;") != NULL);
                R_CODEGEN_CHECK(strstr(generated.bytes, "r_d00000001 r_l00000003;") != NULL);
                R_CODEGEN_CHECK(strstr(generated.bytes, "_Bool r_l00000003_initialized;") != NULL);
                R_CODEGEN_CHECK(strstr(generated.bytes, "r_stack_r_l00000003") == NULL);
                R_CODEGEN_CHECK(
                    strstr(generated.bytes,
                           "r_type_move_std_fs_path_gate(&destination->r_payload.r_some, "
                           "&source->r_payload.r_some);") != NULL);
                R_CODEGEN_CHECK(strstr(generated.bytes,
                                       "r_type_drop_std_fs_path_gate(&value->r_payload.r_some);") !=
                                NULL);
                R_CODEGEN_CHECK(strstr(generated.bytes,
                                       "r_type_move_d00000001_gate(&frame->r_v00000019, "
                                       "&frame->r_l00000003);") != NULL);
                R_CODEGEN_CHECK(
                    r_codegen_count_occurrences(
                        generated.bytes, "r_type_drop_d00000001_gate(&frame->r_l00000006);") == 2U);
            }
            R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &repeated) ==
                            R_FRONTEND_OK);
            R_CODEGEN_CHECK(r_codegen_buffers_equal(&generated, &repeated));
        }
    }
    r_codegen_dispose_buffer(&generated);
    r_codegen_dispose_buffer(&repeated);
    r_frontend_destroy(context);

    for (mutation = 0U; mutation < (size_t)R_CODEGEN_ASYNC_OPTION_PATH_MUTATION_COUNT; ++mutation) {
        RFrontendContext *invalid = r_codegen_build_async_option_path();
        RCodegenAsyncOptionPathView invalid_view;
        RCodegenBuffer output = {0};

        R_CODEGEN_CHECK(invalid != NULL);
        if ((invalid == NULL) || !r_codegen_find_async_option_path(invalid, &invalid_view)) {
            r_frontend_destroy(invalid);
            continue;
        }
        switch ((RCodegenAsyncOptionPathMutation)mutation) {
        case R_CODEGEN_ASYNC_OPTION_PATH_BASE:
            invalid_view.option_type->base = R_TYPE_ID_INVALID;
            break;
        case R_CODEGEN_ASYNC_OPTION_PATH_SECOND:
            invalid_view.option_type->second = invalid_view.option_type->base;
            break;
        case R_CODEGEN_ASYNC_OPTION_PATH_LENGTH:
            invalid_view.option_type->length = UINT64_C(1);
            break;
        case R_CODEGEN_ASYNC_OPTION_PATH_FLAGS:
            invalid_view.option_type->flags = R_SEMANTIC_TYPE_FLAG_SHARED;
            break;
        case R_CODEGEN_ASYNC_OPTION_PATH_BORROWED_PAYLOAD:
            invalid_view.option_type->base = R_TYPE_ID_INVALID;
            for (size_t index = 0U; index < invalid->semantic_type_count; ++index) {
                if (invalid->semantic_types[index].kind == R_SEMANTIC_TYPE_STR) {
                    invalid_view.option_type->base = (RTypeId)(index + 1U);
                    break;
                }
            }
            R_CODEGEN_CHECK(invalid_view.option_type->base != R_TYPE_ID_INVALID);
            break;
        case R_CODEGEN_ASYNC_OPTION_PATH_MUTATION_COUNT:
        default:
            R_CODEGEN_CHECK(false);
            break;
        }
        R_CODEGEN_CHECK(r_frontend_emit_c17(invalid, r_codegen_write, &output) ==
                        R_FRONTEND_NOT_LOWERABLE);
        R_CODEGEN_CHECK(output.call_count == 0U);
        R_CODEGEN_CHECK(output.length == 0U);
        r_codegen_dispose_buffer(&output);
        r_frontend_destroy(invalid);
    }
}

static void r_codegen_test_rejection_gates(void) {
    static const char invalid_source[] = "module codegen.invalid;\n"
                                         "i32 main() {\n"
                                         "    if (1) {\n"
                                         "        return 0;\n"
                                         "    }\n"
                                         "    return 1;\n"
                                         "}\n";
    RFrontendContext *invalid = r_codegen_create_context(NULL);
    RFrontendContext *recursive = r_codegen_create_context(NULL);
    RCodegenBuffer invalid_output = {0};
    RCodegenBuffer recursive_output = {0};
    RSourceId source_id = R_SOURCE_ID_INVALID;

    R_CODEGEN_CHECK(invalid != NULL);
    R_CODEGEN_CHECK(recursive != NULL);
    if (invalid != NULL) {
        R_CODEGEN_CHECK(r_frontend_add_source(invalid,
                                              "invalid.r",
                                              (const uint8_t *)invalid_source,
                                              strlen(invalid_source),
                                              &source_id) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_frontend_emit_c17(invalid, r_codegen_write, &invalid_output) ==
                        R_FRONTEND_NOT_LOWERABLE);
        R_CODEGEN_CHECK(invalid_output.call_count == 0U);
        R_CODEGEN_CHECK(r_frontend_analyze(invalid) == R_FRONTEND_INVALID_SOURCE);
        R_CODEGEN_CHECK(r_frontend_emit_c17(invalid, r_codegen_write, &invalid_output) ==
                        R_FRONTEND_NOT_LOWERABLE);
        R_CODEGEN_CHECK(invalid_output.call_count == 0U);
    }
    if (recursive != NULL) {
        /* R-FUNC-0004: a recursive call chain is rejected before any C is emitted. */
        R_CODEGEN_CHECK(r_codegen_add_file(recursive, R_CODEGEN_RECURSIVE_PATH));
        R_CODEGEN_CHECK(r_frontend_analyze(recursive) == R_FRONTEND_INVALID_SOURCE);
        R_CODEGEN_CHECK(r_frontend_diagnostic_count(recursive) != 0U);
        R_CODEGEN_CHECK(r_frontend_emit_c17(recursive, r_codegen_write, &recursive_output) ==
                        R_FRONTEND_NOT_LOWERABLE);
        R_CODEGEN_CHECK(recursive_output.call_count == 0U);
    }
    r_codegen_dispose_buffer(&invalid_output);
    r_codegen_dispose_buffer(&recursive_output);
    r_frontend_destroy(invalid);
    r_frontend_destroy(recursive);
}

static void r_codegen_test_async_projected_store(void) {
    static const char source[] = "module codegen.async_projected_store;\n"
                                 "struct Cell { i32 value; };\n"
                                 "protected async i32 update() {\n"
                                 "    Cell cell = { .value = 1 };\n"
                                 "    cell.value = 2;\n"
                                 "    return cell.value;\n"
                                 "}\n"
                                 "async i32 main() {\n"
                                 "    try {\n"
                                 "        i32 result = await update();\n"
                                 "        return result;\n"
                                 "    } catch (std.async::start_error error) {\n"
                                 "        return 1;\n"
                                 "    }\n"
                                 "}\n";
    RFrontendContext *context = r_codegen_create_context(NULL);
    RCodegenBuffer generated = {0};
    RSourceId source_id = R_SOURCE_ID_INVALID;
    bool found_projected_store = false;
    size_t index;

    R_CODEGEN_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_CODEGEN_CHECK(r_frontend_add_source(context,
                                          "async-projected-store.r",
                                          (const uint8_t *)source,
                                          strlen(source),
                                          &source_id) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(r_frontend_diagnostic_count(context) == 0U);
    R_CODEGEN_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(r_frontend_diagnostic_count(context) == 0U);
    for (index = 0U; index < context->mir_instruction_count; ++index) {
        const RMirInstruction *instruction = &context->mir_instructions[index];

        if ((instruction->kind == R_MIR_INSTRUCTION_STORE) &&
            (instruction->operand1 != R_MIR_VALUE_ID_INVALID)) {
            found_projected_store = true;
        }
    }
    R_CODEGEN_CHECK(found_projected_store);
    R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &generated) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(generated.call_count == 1U);
    R_CODEGEN_CHECK(generated.length != 0U);
    R_CODEGEN_CHECK(strstr(generated.bytes, "INT32_C(2)") != NULL);
    r_codegen_dispose_buffer(&generated);
    r_frontend_destroy(context);
}

static void r_codegen_test_implicit_final_return(void) {
    static const char source[] = "module test.codegen.implicit_final_return;\n"
                                 "error issue { i32 code; };\n"
                                 "async void async_success() throws issue {}\n"
                                 "void sync_success() throws issue {}\n"
                                 "i32 main() { return 0; }\n";
    RFrontendContext *context = r_codegen_create_context(NULL);
    RCodegenBuffer generated = {0};
    const char *async_step;
    const char *async_step_end;
    const char *async_success;
    const char *sync_function;
    const char *sync_success;
    const char *sync_return;
    const char *sync_fallback;

    R_CODEGEN_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_CODEGEN_CHECK(r_codegen_add_text(context, "implicit-final-return.r", source));
    R_CODEGEN_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(r_frontend_diagnostic_count(context) == 0U);
    R_CODEGEN_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &generated) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(generated.bytes != NULL);

    async_step =
        generated.bytes == NULL
            ? NULL
            : strstr(generated.bytes, "static RRuntimeTaskStepStatus r_async_step_00000001(");
    async_step_end =
        async_step == NULL
            ? NULL
            : strstr(async_step, "static RRuntimeTaskStepStatus r_async_step_gate_00000001(");
    async_success =
        async_step == NULL ? NULL : strstr(async_step, "completion->r_tag = UINT32_C(0);");
    R_CODEGEN_CHECK(async_step != NULL);
    R_CODEGEN_CHECK(async_step_end != NULL);
    R_CODEGEN_CHECK((async_success != NULL) && (async_success < async_step_end));
    R_CODEGEN_CHECK(
        (async_success != NULL) &&
        (strstr(async_success, "return R_RUNTIME_TASK_STEP_COMPLETED;") != NULL) &&
        (strstr(async_success, "return R_RUNTIME_TASK_STEP_COMPLETED;") < async_step_end));

    sync_function = generated.bytes == NULL
                        ? NULL
                        : strstr(generated.bytes, "void r_f00000003(r_d00000002 *r_effect_out) {");
    sync_success =
        sync_function == NULL ? NULL : strstr(sync_function, "r_effect_out->r_tag = UINT32_C(0);");
    sync_return = sync_success == NULL ? NULL : strstr(sync_success, "return;");
    sync_fallback =
        sync_function == NULL ? NULL : strstr(sync_function, "R_RUNTIME_PANIC_CONTRACT_VIOLATION");
    R_CODEGEN_CHECK(sync_function != NULL);
    R_CODEGEN_CHECK(sync_success != NULL);
    R_CODEGEN_CHECK(sync_return != NULL);
    R_CODEGEN_CHECK(sync_fallback == NULL ||
                    (sync_function != NULL && sync_fallback > strstr(sync_function, "\n}")));
    R_CODEGEN_CHECK((sync_function < sync_success) && (sync_success < sync_return));

    r_codegen_dispose_buffer(&generated);
    r_frontend_destroy(context);
}

static void r_codegen_test_async_state_machine(void) {
    RFrontendContext *context = r_codegen_create_context(NULL);
    RCodegenBuffer generated = {0};
    RCodegenBuffer repeated = {0};
    RMirInstruction *character_literal = NULL;
    size_t instruction_index;

    R_CODEGEN_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_CODEGEN_CHECK(r_codegen_add_file(context, R_CODEGEN_ASYNC_SCALAR_PATH));
    R_CODEGEN_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &generated) ==
                    R_FRONTEND_NOT_LOWERABLE);
    R_CODEGEN_CHECK(generated.call_count == 0U);
    R_CODEGEN_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
    for (instruction_index = 0U; instruction_index < context->mir_instruction_count;
         ++instruction_index) {
        RMirInstruction *instruction = &context->mir_instructions[instruction_index];
        const RSemanticType *type = r_semantic_type(context, instruction->type);

        if ((instruction->kind == R_MIR_INSTRUCTION_CONSTANT) && (type != NULL) &&
            (type->kind == R_SEMANTIC_TYPE_CHAR) &&
            (instruction->integer_value == UINT64_C(128640))) {
            character_literal = instruction;
            break;
        }
    }
    R_CODEGEN_CHECK(character_literal != NULL);
    R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &generated) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(generated.call_count == 1U);
    R_CODEGEN_CHECK(generated.bytes != NULL);
    if (generated.bytes != NULL) {
        const char *hosted_main = strstr(generated.bytes, "int main(int argc, char *argv[])");
        const char *blocking_await = strstr(generated.bytes, "r_runtime_task_await(");
        const char *launch = strstr(generated.bytes,
                                    "const RRuntimeTaskStartResult started =\n"
                                    "        r_async_start_00000001_gate(initialize, context, "
                                    "start_mode);");
        const char *success_payload =
            launch == NULL ? NULL : strstr(launch, "r_effect_out->r_payload.r_ok = started.task;");
        const char *success_tag =
            launch == NULL ? NULL : strstr(launch, "r_effect_out->r_tag = UINT32_C(0);");
        const char *error_payload =
            launch == NULL ? NULL : strstr(launch, "r_effect_out->r_payload.r_error_00000001 =");
        const char *error_tag =
            launch == NULL ? NULL : strstr(launch, "r_effect_out->r_tag = UINT32_C(1);");

        R_CODEGEN_CHECK(strstr(generated.bytes, "#include \"r_std_async.h\"") != NULL);
        r_codegen_check_hosted_stack_bootstrap(generated.bytes);
        R_CODEGEN_CHECK(
            strstr(generated.bytes, "#define R_STACK_FRAME(name) R_STACK_FRAME_##name") != NULL);
        R_CODEGEN_CHECK(
            strstr(generated.bytes,
                   "#error \"R generated C17 requires a measured stack-usage header\"") != NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes, "r_runtime_task_resumable_start_prepare(") != NULL);
        R_CODEGEN_CHECK(launch != NULL);
        R_CODEGEN_CHECK((success_payload != NULL) && (success_tag != NULL) &&
                        (success_payload < success_tag));
        R_CODEGEN_CHECK((error_payload != NULL) && (error_tag != NULL) &&
                        (error_payload < error_tag));
        r_codegen_check_stack_gate(generated.bytes, "r_async_frame_initialize_00000001");
        r_codegen_check_stack_gate(generated.bytes, "r_async_frame_drop_00000001");
        r_codegen_check_stack_gate(generated.bytes, "r_async_start_00000001");
        r_codegen_check_stack_gate(generated.bytes, "r_async_launch_00000001");
        R_CODEGEN_CHECK(strstr(generated.bytes, "r_async_frame_drop_00000001_gate,\n    };") !=
                        NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes,
                               "r_async_launch_00000001_gate(\n"
                               "                &frame->r_v00000000, "
                               "r_async_frame_initialize_00000001_gate, NULL, 0);") != NULL);
        R_CODEGEN_CHECK(
            strstr(
                generated.bytes,
                "r_async_start_00000002_gate(r_async_frame_initialize_00000002_gate, NULL, 0);") !=
            NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes, "R_STACK_ENTRY(r_async_step_00000001)") != NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes, "r_async_step_gate_00000001") != NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes, "r_runtime_task_execution_await(") != NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes, "R_RUNTIME_TASK_STEP_SUSPENDED") != NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes, "r_runtime_task_destroy(") != NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes, "UINT32_C(128640)") != NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes, "r_runtime_hosted_async_root_start_failure()") !=
                        NULL);
        R_CODEGEN_CHECK(hosted_main != NULL);
        R_CODEGEN_CHECK(blocking_await != NULL);
        if ((hosted_main != NULL) && (blocking_await != NULL)) {
            R_CODEGEN_CHECK(blocking_await > hosted_main);
        }
    }
    if (character_literal != NULL) {
        RCodegenBuffer rejected = {0};
        const uint64_t valid_value = character_literal->integer_value;

        character_literal->integer_value = UINT64_C(0xD800);
        R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &rejected) ==
                        R_FRONTEND_INTERNAL_ERROR);
        R_CODEGEN_CHECK(rejected.call_count == 0U);
        R_CODEGEN_CHECK(rejected.length == 0U);
        character_literal->integer_value = valid_value;
        r_codegen_dispose_buffer(&rejected);
    }
    R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &repeated) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(r_codegen_buffers_equal(&generated, &repeated));
    r_codegen_dispose_buffer(&generated);
    r_codegen_dispose_buffer(&repeated);
    r_frontend_destroy(context);
}

static RMirInstruction *r_codegen_find_enum_constant(RFrontendContext *context,
                                                     RSemanticTypeKind underlying_kind) {
    size_t index;

    for (index = 0U; index < context->mir_instruction_count; ++index) {
        RMirInstruction *instruction = &context->mir_instructions[index];
        const RSemanticType *type;
        const RSemanticAggregate *aggregate;
        const RSemanticType *underlying;

        if ((instruction->kind != R_MIR_INSTRUCTION_CONSTANT) ||
            (instruction->aggregate_member == UINT32_C(0))) {
            continue;
        }
        type = r_semantic_type(context, instruction->type);
        if ((type == NULL) || (type->kind != R_SEMANTIC_TYPE_ENUM) ||
            (type->base == R_TYPE_ID_INVALID) ||
            ((size_t)type->base > context->semantic_aggregate_count)) {
            continue;
        }
        aggregate = &context->semantic_aggregates[(size_t)type->base - 1U];
        underlying = r_semantic_type(context, aggregate->enum_underlying_type);
        if ((underlying != NULL) && (underlying->kind == underlying_kind)) {
            return instruction;
        }
    }
    return NULL;
}

static void r_codegen_test_async_enum_constants(void) {
    static const struct RCodegenEnumMutation {
        RSemanticTypeKind underlying_kind;
        uint64_t replacement;
    } mutations[] = {
        {R_SEMANTIC_TYPE_I32, UINT64_C(2147483648)},
        {R_SEMANTIC_TYPE_U32, UINT64_C(4294967296)},
        {R_SEMANTIC_TYPE_I32, UINT64_C(2)},
    };
    RFrontendContext *context = r_codegen_create_context(NULL);
    RCodegenBuffer generated = {0};
    RCodegenBuffer repeated = {0};
    size_t index;

    R_CODEGEN_CHECK(context != NULL);
    if (context != NULL) {
        R_CODEGEN_CHECK(r_codegen_add_file(context, R_CODEGEN_ASYNC_ENUM_PATH));
        R_CODEGEN_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_codegen_find_enum_constant(context, R_SEMANTIC_TYPE_I32) != NULL);
        R_CODEGEN_CHECK(r_codegen_find_enum_constant(context, R_SEMANTIC_TYPE_U32) != NULL);
        R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &generated) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(generated.bytes != NULL);
        if (generated.bytes != NULL) {
            R_CODEGEN_CHECK(
                r_codegen_count_occurrences(generated.bytes, "(r_a00000001)INT32_C(1)") == 2U);
            R_CODEGEN_CHECK(
                r_codegen_count_occurrences(generated.bytes, "(r_a00000002)UINT32_C(1)") == 2U);
        }
        R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &repeated) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_codegen_buffers_equal(&generated, &repeated));
    }
    r_codegen_dispose_buffer(&generated);
    r_codegen_dispose_buffer(&repeated);
    r_frontend_destroy(context);

    for (index = 0U; index < (sizeof(mutations) / sizeof(mutations[0])); ++index) {
        RFrontendContext *invalid = r_codegen_create_context(NULL);
        RCodegenBuffer invalid_output = {0};
        RMirInstruction *constant = NULL;

        R_CODEGEN_CHECK(invalid != NULL);
        if (invalid != NULL) {
            R_CODEGEN_CHECK(r_codegen_add_file(invalid, R_CODEGEN_ASYNC_ENUM_PATH));
            R_CODEGEN_CHECK(r_frontend_analyze(invalid) == R_FRONTEND_OK);
            R_CODEGEN_CHECK(r_frontend_lower_mir(invalid) == R_FRONTEND_OK);
            constant = r_codegen_find_enum_constant(invalid, mutations[index].underlying_kind);
            R_CODEGEN_CHECK(constant != NULL);
            if (constant != NULL) {
                constant->integer_value = mutations[index].replacement;
                R_CODEGEN_CHECK(r_frontend_emit_c17(invalid, r_codegen_write, &invalid_output) ==
                                R_FRONTEND_NOT_LOWERABLE);
                R_CODEGEN_CHECK(invalid_output.call_count == 0U);
                R_CODEGEN_CHECK(invalid_output.length == 0U);
            }
        }
        r_codegen_dispose_buffer(&invalid_output);
        r_frontend_destroy(invalid);
    }
}

typedef struct RCodegenPhiView {
    RMirFunction *function;
    RMirInstruction *instruction;
    RMirBlockId block_id;
} RCodegenPhiView;

static RFrontendContext *r_codegen_build_async_short_circuit(void) {
    RFrontendContext *context = r_codegen_create_context(NULL);

    if ((context == NULL) || !r_codegen_add_file(context, R_CODEGEN_ASYNC_SHORT_CIRCUIT_PATH) ||
        (r_frontend_analyze(context) != R_FRONTEND_OK) ||
        (r_frontend_diagnostic_count(context) != 0U) ||
        (r_frontend_lower_mir(context) != R_FRONTEND_OK) ||
        (r_frontend_diagnostic_count(context) != 0U)) {
        r_frontend_destroy(context);
        return NULL;
    }
    return context;
}

static bool r_codegen_phi_at(RFrontendContext *context, size_t occurrence, RCodegenPhiView *view) {
    size_t function_index;

    if ((context == NULL) || (view == NULL)) {
        return false;
    }
    (void)memset(view, 0, sizeof(*view));
    for (function_index = 0U; function_index < context->mir_function_count; ++function_index) {
        RMirFunction *function = &context->mir_functions[function_index];
        uint32_t block_index;

        for (block_index = UINT32_C(0); block_index < function->block_count; ++block_index) {
            const size_t global_block_index = (size_t)function->first_block + (size_t)block_index;
            RMirBlock *block;
            uint32_t instruction_index;

            if (global_block_index >= context->mir_block_count) {
                return false;
            }
            block = &context->mir_blocks[global_block_index];
            for (instruction_index = UINT32_C(0); instruction_index < block->instruction_count;
                 ++instruction_index) {
                const size_t global_instruction_index =
                    (size_t)block->first_instruction + (size_t)instruction_index;
                RMirInstruction *instruction;

                if (global_instruction_index >= context->mir_instruction_count) {
                    return false;
                }
                instruction = &context->mir_instructions[global_instruction_index];
                if (instruction->kind != R_MIR_INSTRUCTION_PHI) {
                    continue;
                }
                if (occurrence == 0U) {
                    view->function = function;
                    view->instruction = instruction;
                    view->block_id = (RMirBlockId)(block_index + UINT32_C(1));
                    return true;
                }
                occurrence -= 1U;
            }
        }
    }
    return false;
}

static bool r_codegen_assignment_precedes_state(const char *generated,
                                                const char *assignment,
                                                const char *state) {
    const char *cursor = generated == NULL ? NULL : strstr(generated, assignment);

    if (cursor == NULL) {
        return false;
    }
    cursor += strlen(assignment);
    while ((*cursor == ' ') || (*cursor == '\t') || (*cursor == '\r') || (*cursor == '\n')) {
        cursor += 1;
    }
    return strncmp(cursor, state, strlen(state)) == 0;
}

static void r_codegen_test_async_short_circuit_phi(void) {
    RFrontendContext *context = r_codegen_build_async_short_circuit();
    RCodegenBuffer generated = {0};
    RCodegenBuffer repeated = {0};
    size_t phi_index;

    R_CODEGEN_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &generated) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(generated.call_count == 1U);
    R_CODEGEN_CHECK(generated.bytes != NULL);
    for (phi_index = 0U; phi_index < 8U; ++phi_index) {
        RCodegenPhiView view;
        char left_assignment[96];
        char right_assignment[96];
        char assignment_prefix[48];
        char state[64];

        R_CODEGEN_CHECK(r_codegen_phi_at(context, phi_index, &view));
        if (!r_codegen_phi_at(context, phi_index, &view)) {
            continue;
        }
        R_CODEGEN_CHECK(snprintf(left_assignment,
                                 sizeof(left_assignment),
                                 "frame->r_v%08" PRIu32 " = frame->r_v%08" PRIu32 ";",
                                 view.instruction->result - UINT32_C(1),
                                 view.instruction->operand0 - UINT32_C(1)) > 0);
        R_CODEGEN_CHECK(snprintf(right_assignment,
                                 sizeof(right_assignment),
                                 "frame->r_v%08" PRIu32 " = frame->r_v%08" PRIu32 ";",
                                 view.instruction->result - UINT32_C(1),
                                 view.instruction->operand1 - UINT32_C(1)) > 0);
        R_CODEGEN_CHECK(snprintf(assignment_prefix,
                                 sizeof(assignment_prefix),
                                 "frame->r_v%08" PRIu32 " =",
                                 view.instruction->result - UINT32_C(1)) > 0);
        R_CODEGEN_CHECK(snprintf(state,
                                 sizeof(state),
                                 "frame->r_state = UINT32_C(%" PRIu32 ");",
                                 view.block_id) > 0);
        R_CODEGEN_CHECK(
            r_codegen_assignment_precedes_state(generated.bytes, left_assignment, state));
        R_CODEGEN_CHECK(
            r_codegen_assignment_precedes_state(generated.bytes, right_assignment, state));
        R_CODEGEN_CHECK(r_codegen_count_occurrences(generated.bytes, assignment_prefix) == 2U);
    }
    {
        RCodegenPhiView absent;

        R_CODEGEN_CHECK(!r_codegen_phi_at(context, 8U, &absent));
    }
    R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &repeated) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(r_codegen_buffers_equal(&generated, &repeated));
    r_codegen_dispose_buffer(&generated);
    r_codegen_dispose_buffer(&repeated);
    r_frontend_destroy(context);
}

static void r_codegen_test_async_short_circuit_phi_mutations(void) {
    size_t mutation;

    for (mutation = 0U; mutation < 4U; ++mutation) {
        RFrontendContext *context = r_codegen_build_async_short_circuit();
        RCodegenBuffer generated = {0};
        RCodegenPhiView view;

        R_CODEGEN_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        R_CODEGEN_CHECK(r_codegen_phi_at(context, 0U, &view));
        if (view.instruction == NULL) {
            r_frontend_destroy(context);
            continue;
        }
        if (mutation == 0U) {
            RTypeId u32_type = R_TYPE_ID_INVALID;

            R_CODEGEN_CHECK(r_semantic_type_from_token(context, R_TOKEN_KW_U32, &u32_type));
            view.instruction->type = u32_type;
        } else if (mutation == 1U) {
            view.instruction->target0 = R_MIR_BLOCK_ID_INVALID;
        } else if (mutation == 2U) {
            const RMirBlockId target = view.instruction->target0;

            view.instruction->target0 = view.instruction->target1;
            view.instruction->target1 = target;
        } else {
            const size_t predecessor_index =
                (size_t)view.function->first_block + (size_t)view.instruction->target0 - 1U;
            RMirBlock *predecessor = NULL;
            RMirInstruction *terminator = NULL;

            R_CODEGEN_CHECK(predecessor_index < context->mir_block_count);
            if (predecessor_index < context->mir_block_count) {
                predecessor = &context->mir_blocks[predecessor_index];
            }
            R_CODEGEN_CHECK((predecessor != NULL) &&
                            (predecessor->instruction_count != UINT32_C(0)));
            if ((predecessor != NULL) && (predecessor->instruction_count != UINT32_C(0))) {
                const size_t terminator_index = (size_t)predecessor->first_instruction +
                                                (size_t)predecessor->instruction_count - 1U;

                R_CODEGEN_CHECK(terminator_index < context->mir_instruction_count);
                if (terminator_index < context->mir_instruction_count) {
                    terminator = &context->mir_instructions[terminator_index];
                }
            }
            R_CODEGEN_CHECK((terminator != NULL) && (terminator->kind == R_MIR_INSTRUCTION_BRANCH));
            if ((terminator != NULL) && (terminator->kind == R_MIR_INSTRUCTION_BRANCH)) {
                if (terminator->target0 == view.block_id) {
                    terminator->target0 = terminator->target1;
                } else {
                    R_CODEGEN_CHECK(terminator->target1 == view.block_id);
                    terminator->target1 = terminator->target0;
                }
            }
        }
        R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &generated) ==
                        R_FRONTEND_NOT_LOWERABLE);
        R_CODEGEN_CHECK(generated.call_count == 0U);
        R_CODEGEN_CHECK(generated.length == 0U);
        r_codegen_dispose_buffer(&generated);
        r_frontend_destroy(context);
    }
}

typedef struct RCodegenFusedDefaultOwnView {
    RMirFunction *function;
    RMirInstruction *array;
    RMirInstruction *aggregate;
    RMirInstruction *owner;
    RMirInstruction *discard;
    RTypeId task_type;
} RCodegenFusedDefaultOwnView;

typedef enum RCodegenFusedDefaultOwnMutation {
    R_CODEGEN_FUSED_DEFAULT_OWN_EXTRA_USE = 0,
    R_CODEGEN_FUSED_DEFAULT_OWN_LENGTH,
    R_CODEGEN_FUSED_DEFAULT_OWN_BASE,
    R_CODEGEN_FUSED_DEFAULT_OWN_MEMBER,
    R_CODEGEN_FUSED_DEFAULT_OWN_LINK,
    R_CODEGEN_FUSED_DEFAULT_OWN_DROP_BEARING
} RCodegenFusedDefaultOwnMutation;

static RFrontendContext *r_codegen_build_async_fused_default_own(void) {
    RFrontendContext *context = r_codegen_create_context(NULL);

    if ((context == NULL) || !r_codegen_add_file(context, R_CODEGEN_ASYNC_FUSED_DEFAULT_OWN_PATH) ||
        (r_frontend_analyze(context) != R_FRONTEND_OK) ||
        (r_frontend_diagnostic_count(context) != 0U) ||
        (r_frontend_lower_mir(context) != R_FRONTEND_OK) ||
        (r_frontend_diagnostic_count(context) != 0U)) {
        r_frontend_destroy(context);
        return NULL;
    }
    return context;
}

static bool r_codegen_find_async_fused_default_own(RFrontendContext *context,
                                                   RCodegenFusedDefaultOwnView *view) {
    size_t function_index;

    if ((context == NULL) || (view == NULL)) {
        return false;
    }
    (void)memset(view, 0, sizeof(*view));
    view->task_type = R_TYPE_ID_INVALID;
    for (function_index = 0U; function_index < context->mir_function_count; ++function_index) {
        RCodegenFusedDefaultOwnView candidate = {0};
        RMirFunction *function = &context->mir_functions[function_index];
        uint32_t block_index;

        candidate.function = function;
        candidate.task_type = R_TYPE_ID_INVALID;
        for (block_index = UINT32_C(0); block_index < function->block_count; ++block_index) {
            const size_t global_block_index = (size_t)function->first_block + (size_t)block_index;
            RMirBlock *block;
            uint32_t instruction_index;

            if (global_block_index >= context->mir_block_count) {
                return false;
            }
            block = &context->mir_blocks[global_block_index];
            for (instruction_index = UINT32_C(0); instruction_index < block->instruction_count;
                 ++instruction_index) {
                const size_t global_instruction_index =
                    (size_t)block->first_instruction + (size_t)instruction_index;
                RMirInstruction *instruction;

                if (global_instruction_index >= context->mir_instruction_count) {
                    return false;
                }
                instruction = &context->mir_instructions[global_instruction_index];
                if ((instruction->kind == R_MIR_INSTRUCTION_ARRAY) &&
                    (instruction->integer_value == UINT64_C(1048576))) {
                    if (candidate.array != NULL) {
                        return false;
                    }
                    candidate.array = instruction;
                } else if ((instruction->kind == R_MIR_INSTRUCTION_AGGREGATE) &&
                           (candidate.array != NULL) &&
                           (instruction->operand_count == UINT32_C(1)) &&
                           ((size_t)instruction->first_operand < context->mir_operand_count) &&
                           (context->mir_operands[(size_t)instruction->first_operand] ==
                            candidate.array->result)) {
                    if (candidate.aggregate != NULL) {
                        return false;
                    }
                    candidate.aggregate = instruction;
                } else if ((instruction->kind == R_MIR_INSTRUCTION_NEW) &&
                           (instruction->operation == R_TOKEN_KW_OWN) &&
                           (candidate.aggregate != NULL) &&
                           (instruction->operand0 == candidate.aggregate->result)) {
                    if (candidate.owner != NULL) {
                        return false;
                    }
                    candidate.owner = instruction;
                } else if (instruction->kind == R_MIR_INSTRUCTION_DISCARD) {
                    /* The checked start-error catch discards its binding before the
                     * success path discards the awaited value. The latter is the
                     * mutation anchor for the fused default-owner source. */
                    candidate.discard = instruction;
                } else if (instruction->kind == R_MIR_INSTRUCTION_ASYNC_START) {
                    const RSemanticType *type =
                        r_semantic_type(context, instruction->auxiliary_type);

                    if ((type != NULL) && (type->kind == R_SEMANTIC_TYPE_TASK)) {
                        candidate.task_type = instruction->auxiliary_type;
                    }
                }
            }
        }
        if ((candidate.array == NULL) && (candidate.aggregate == NULL) &&
            (candidate.owner == NULL)) {
            continue;
        }
        if ((candidate.array == NULL) || (candidate.aggregate == NULL) ||
            (candidate.owner == NULL) || (candidate.discard == NULL) ||
            (candidate.task_type == R_TYPE_ID_INVALID) || (view->function != NULL)) {
            return false;
        }
        *view = candidate;
    }
    return view->function != NULL;
}

static void r_codegen_test_async_fused_default_own(void) {
    RFrontendContext *context = r_codegen_build_async_fused_default_own();
    RCodegenBuffer generated = {0};
    RCodegenBuffer repeated = {0};
    RCodegenFusedDefaultOwnView view;

    R_CODEGEN_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_CODEGEN_CHECK(r_codegen_find_async_fused_default_own(context, &view));
    R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &generated) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(generated.call_count == 1U);
    R_CODEGEN_CHECK(generated.bytes != NULL);
    if (generated.bytes != NULL) {
        R_CODEGEN_CHECK(r_codegen_count_occurrences(generated.bytes, "#include <string.h>") == 1U);
        R_CODEGEN_CHECK(r_codegen_count_occurrences(
                            generated.bytes,
                            "static void r_async_default_own_initialize_00000001_00000002(") == 1U);
        R_CODEGEN_CHECK(r_codegen_count_occurrences(
                            generated.bytes,
                            "static void r_async_default_own_initialize_00000001_00000002_gate(") ==
                        1U);
        R_CODEGEN_CHECK(strstr(generated.bytes,
                               "R_STACK_ENTRY(r_async_default_own_initialize_00000001_00000002)") !=
                        NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes,
                               "r_async_default_own_initialize_00000001_00000002_gate,") != NULL);
        r_codegen_check_stack_gate(generated.bytes,
                                   "r_async_default_own_initialize_00000001_00000002");
        R_CODEGEN_CHECK(
            r_codegen_count_occurrences(generated.bytes, "r_runtime_own_create_initialize(") == 1U);
        R_CODEGEN_CHECK(r_codegen_count_occurrences(generated.bytes, "(void)memset(") == 1U);
        R_CODEGEN_CHECK(strstr(generated.bytes, "r_new_own(") == NULL);
    }
    R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &repeated) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(r_codegen_buffers_equal(&generated, &repeated));
    r_codegen_dispose_buffer(&generated);
    r_codegen_dispose_buffer(&repeated);
    r_frontend_destroy(context);
}

static void r_codegen_test_async_fused_default_own_mutations(void) {
    size_t mutation;

    for (mutation = 0U; mutation < 6U; ++mutation) {
        RFrontendContext *context = r_codegen_build_async_fused_default_own();
        RCodegenBuffer generated = {0};
        RCodegenFusedDefaultOwnView view;
        RSemanticType *array_type = NULL;

        R_CODEGEN_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        R_CODEGEN_CHECK(r_codegen_find_async_fused_default_own(context, &view));
        if ((view.array == NULL) || (view.aggregate == NULL) || (view.owner == NULL) ||
            (view.discard == NULL) || (view.array->type == R_TYPE_ID_INVALID) ||
            ((size_t)view.array->type > context->semantic_type_count)) {
            r_frontend_destroy(context);
            continue;
        }
        array_type = &context->semantic_types[(size_t)view.array->type - 1U];
        if (mutation == (size_t)R_CODEGEN_FUSED_DEFAULT_OWN_EXTRA_USE) {
            view.discard->operand0 = view.array->result;
        } else if (mutation == (size_t)R_CODEGEN_FUSED_DEFAULT_OWN_LENGTH) {
            view.array->integer_value -= UINT64_C(1);
        } else if (mutation == (size_t)R_CODEGEN_FUSED_DEFAULT_OWN_BASE) {
            RTypeId i32_type = R_TYPE_ID_INVALID;

            R_CODEGEN_CHECK(r_semantic_type_from_token(context, R_TOKEN_KW_I32, &i32_type));
            array_type = &context->semantic_types[(size_t)view.array->type - 1U];
            array_type->base = i32_type;
        } else if (mutation == (size_t)R_CODEGEN_FUSED_DEFAULT_OWN_MEMBER) {
            const size_t operand_index = (size_t)view.aggregate->first_operand;

            R_CODEGEN_CHECK(operand_index < context->mir_operand_count);
            if (operand_index < context->mir_operand_count) {
                context->mir_operand_members[operand_index] = UINT32_C(0);
            }
        } else if (mutation == (size_t)R_CODEGEN_FUSED_DEFAULT_OWN_LINK) {
            view.owner->operand0 = view.array->result;
        } else {
            array_type->base = view.task_type;
        }
        const RFrontendStatus emitted = r_frontend_emit_c17(context, r_codegen_write, &generated);
        if (mutation == (size_t)R_CODEGEN_FUSED_DEFAULT_OWN_EXTRA_USE) {
            /* A Copy array with a second use is valid; it now uses ordinary array lowering
             * instead of the single-use fused initializer. */
            R_CODEGEN_CHECK(emitted == R_FRONTEND_OK);
            R_CODEGEN_CHECK(generated.length != 0U);
            R_CODEGEN_CHECK(strstr(generated.bytes, "r_async_default_own_initialize_") == NULL);
        } else {
            R_CODEGEN_CHECK(emitted == R_FRONTEND_NOT_LOWERABLE);
            R_CODEGEN_CHECK(generated.call_count == 0U);
            R_CODEGEN_CHECK(generated.length == 0U);
        }
        r_codegen_dispose_buffer(&generated);
        r_frontend_destroy(context);
    }
}

static bool r_codegen_symbol_name_equals(const RFrontendContext *context,
                                         RSymbolId symbol_id,
                                         const char *name) {
    const RSemanticSymbol *symbol;
    const RInternEntry *entry;
    const size_t name_length = name == NULL ? 0U : strlen(name);

    if ((context == NULL) || (symbol_id == R_SYMBOL_ID_INVALID) ||
        ((size_t)symbol_id > context->semantic_symbol_count) || (name == NULL)) {
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

static RFrontendContext *r_codegen_build_async_sync_call(void) {
    RFrontendContext *context = r_codegen_create_context(NULL);

    if ((context == NULL) || !r_codegen_add_file(context, R_CODEGEN_ASYNC_SYNC_CALL_PATH) ||
        (r_frontend_analyze(context) != R_FRONTEND_OK) ||
        (r_frontend_lower_mir(context) != R_FRONTEND_OK)) {
        r_frontend_destroy(context);
        return NULL;
    }
    return context;
}

static RMirInstruction *
r_codegen_find_call(RFrontendContext *context, const char *callee_name, size_t occurrence) {
    size_t index;

    if (context == NULL) {
        return NULL;
    }
    for (index = 0U; index < context->mir_instruction_count; ++index) {
        RMirInstruction *instruction = &context->mir_instructions[index];

        if ((instruction->kind != R_MIR_INSTRUCTION_CALL) ||
            !r_codegen_symbol_name_equals(context, instruction->symbol, callee_name)) {
            continue;
        }
        if (occurrence == 0U) {
            return instruction;
        }
        occurrence -= 1U;
    }
    return NULL;
}

static RMirInstruction *r_codegen_find_value_definition(RFrontendContext *context,
                                                        RMirValueId value) {
    size_t index;

    if ((context == NULL) || (value == R_MIR_VALUE_ID_INVALID)) {
        return NULL;
    }
    for (index = 0U; index < context->mir_instruction_count; ++index) {
        if (context->mir_instructions[index].result == value) {
            return &context->mir_instructions[index];
        }
    }
    return NULL;
}

static void r_codegen_test_async_sync_calls(void) {
    enum RCodegenCallMutation {
        R_CODEGEN_CALL_MUTATION_MISSING_BORROW,
        R_CODEGEN_CALL_MUTATION_EXTRA_BORROW,
        R_CODEGEN_CALL_MUTATION_ARITY,
        R_CODEGEN_CALL_MUTATION_MOVE_SHAPE,
    };
    static const enum RCodegenCallMutation mutations[] = {
        R_CODEGEN_CALL_MUTATION_MISSING_BORROW,
        R_CODEGEN_CALL_MUTATION_EXTRA_BORROW,
        R_CODEGEN_CALL_MUTATION_ARITY,
        R_CODEGEN_CALL_MUTATION_MOVE_SHAPE,
    };
    RFrontendContext *context = r_codegen_build_async_sync_call();
    RCodegenBuffer generated = {0};
    RCodegenBuffer repeated = {0};
    size_t mutation_index;

    R_CODEGEN_CHECK(context != NULL);
    if (context != NULL) {
        RMirInstruction *first_add = r_codegen_find_call(context, "add", 0U);
        RMirInstruction *second_add = r_codegen_find_call(context, "add", 1U);
        RMirInstruction *forward = r_codegen_find_call(context, "forward", 0U);
        RMirInstruction *consume = r_codegen_find_call(context, "consume", 0U);

        R_CODEGEN_CHECK(first_add != NULL);
        R_CODEGEN_CHECK(second_add != NULL);
        R_CODEGEN_CHECK(forward != NULL);
        R_CODEGEN_CHECK(consume != NULL);
        R_CODEGEN_CHECK((first_add != NULL) && (first_add->call_borrow_mask.low == UINT64_C(1)) &&
                        (first_add->call_borrow_mask.high == UINT64_C(0)));
        R_CODEGEN_CHECK((second_add != NULL) && (second_add->call_borrow_mask.low == UINT64_C(1)) &&
                        (second_add->call_borrow_mask.high == UINT64_C(0)));
        R_CODEGEN_CHECK((forward != NULL) && (forward->call_borrow_mask.low == UINT64_C(0)) &&
                        (forward->call_borrow_mask.high == UINT64_C(0)));
        R_CODEGEN_CHECK((consume != NULL) && (consume->call_borrow_mask.low == UINT64_C(0)) &&
                        (consume->call_borrow_mask.high == UINT64_C(0)));
        R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &generated) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(generated.bytes != NULL);
        if (generated.bytes != NULL) {
            const char *forward_clear =
                strstr(generated.bytes, "frame->r_v00000006_initialized = 0;");
            const char *forward_call =
                forward_clear == NULL
                    ? NULL
                    : strstr(forward_clear,
                             "frame->r_v00000007 = r_f00000003(frame->r_v00000006);");
            const char *forward_result =
                forward_call == NULL ? NULL
                                     : strstr(forward_call, "frame->r_v00000007_initialized = 1;");
            const char *consume_clear =
                strstr(generated.bytes, "frame->r_v00000034_initialized = 0;");
            const char *consume_call =
                consume_clear == NULL ? NULL
                                      : strstr(consume_clear, "r_f00000002(frame->r_v00000034);");
            const char *async_gate = strstr(
                generated.bytes, "static RRuntimeTaskStepStatus r_async_step_gate_00000004(");
            const char *async_preflight =
                async_gate == NULL ? NULL
                                   : strstr(async_gate, "R_STACK_ENTRY(r_async_step_00000004)");
            const char *async_step_call =
                async_preflight == NULL
                    ? NULL
                    : strstr(async_preflight,
                             "return r_async_step_00000004(execution, payload_pointer, "
                             "result_pointer);");
            const char *async_start_with_gate =
                async_step_call == NULL
                    ? NULL
                    : strstr(async_step_call,
                             "payload_type, result_type, r_async_step_gate_00000004);");

            R_CODEGEN_CHECK(strstr(generated.bytes, "static int32_t r_f00000001(") != NULL);
            R_CODEGEN_CHECK(strstr(generated.bytes, "static void r_f00000002(RRuntimeOwn") != NULL);
            R_CODEGEN_CHECK(strstr(generated.bytes, "static RRuntimeOwn r_f00000003(") != NULL);
            R_CODEGEN_CHECK(r_codegen_count_occurrences(generated.bytes, "r_stack_r_v00000001") ==
                            3U);
            R_CODEGEN_CHECK(r_codegen_count_occurrences(generated.bytes, "r_stack_r_v00000015") ==
                            3U);
            R_CODEGEN_CHECK(strstr(generated.bytes, "R_INTERNAL_ASSERT") == NULL);
            R_CODEGEN_CHECK(strstr(generated.bytes, "r_runtime_assert.h") == NULL);
            R_CODEGEN_CHECK(strstr(generated.bytes, "R_RUNTIME_ASSERTIONS_ENABLED") == NULL);
            R_CODEGEN_CHECK((forward_clear != NULL) && (forward_call != NULL) &&
                            (forward_result != NULL) && (forward_clear < forward_call) &&
                            (forward_call < forward_result));
            R_CODEGEN_CHECK((consume_clear != NULL) && (consume_call != NULL) &&
                            (consume_clear < consume_call));
            R_CODEGEN_CHECK((async_gate != NULL) && (async_preflight != NULL) &&
                            (async_step_call != NULL) && (async_start_with_gate != NULL) &&
                            (async_gate < async_preflight) && (async_preflight < async_step_call) &&
                            (async_step_call < async_start_with_gate));
        }
        R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &repeated) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_codegen_buffers_equal(&generated, &repeated));
    }
    r_codegen_dispose_buffer(&generated);
    r_codegen_dispose_buffer(&repeated);
    r_frontend_destroy(context);

    for (mutation_index = 0U; mutation_index < (sizeof(mutations) / sizeof(mutations[0]));
         ++mutation_index) {
        RFrontendContext *invalid = r_codegen_build_async_sync_call();
        RCodegenBuffer output = {0};
        RMirInstruction *call = NULL;

        R_CODEGEN_CHECK(invalid != NULL);
        if (invalid == NULL) {
            continue;
        }
        if (mutations[mutation_index] == R_CODEGEN_CALL_MUTATION_MOVE_SHAPE) {
            RMirInstruction *definition = NULL;
            RMirInstruction *forward = r_codegen_find_call(invalid, "forward", 0U);

            R_CODEGEN_CHECK(forward != NULL);
            if ((forward != NULL) &&
                ((size_t)forward->first_operand < invalid->mir_operand_count)) {
                definition = r_codegen_find_value_definition(
                    invalid, invalid->mir_operands[forward->first_operand]);
            }
            R_CODEGEN_CHECK(definition != NULL);
            if (definition != NULL) {
                definition->kind = R_MIR_INSTRUCTION_LOAD;
            }
        } else {
            call = r_codegen_find_call(invalid, "add", 0U);
            R_CODEGEN_CHECK(call != NULL);
            if (call != NULL) {
                switch (mutations[mutation_index]) {
                case R_CODEGEN_CALL_MUTATION_MISSING_BORROW:
                    call->call_borrow_mask.low = UINT64_C(0);
                    call->call_borrow_mask.high = UINT64_C(0);
                    break;
                case R_CODEGEN_CALL_MUTATION_EXTRA_BORROW:
                    call->call_borrow_mask.low |= UINT64_C(1) << UINT32_C(1);
                    break;
                case R_CODEGEN_CALL_MUTATION_ARITY:
                    call->operand_count -= UINT32_C(1);
                    break;
                case R_CODEGEN_CALL_MUTATION_MOVE_SHAPE:
                default:
                    R_CODEGEN_CHECK(false);
                    break;
                }
            }
        }
        R_CODEGEN_CHECK(r_frontend_emit_c17(invalid, r_codegen_write, &output) ==
                        R_FRONTEND_NOT_LOWERABLE);
        R_CODEGEN_CHECK(output.call_count == 0U);
        R_CODEGEN_CHECK(output.length == 0U);
        r_codegen_dispose_buffer(&output);
        r_frontend_destroy(invalid);
    }
}

typedef struct RCodegenAsyncMirSliceView {
    RMirInstruction *fixed_range;
    RMirInstruction *full;
    RMirInstruction *dynamic_range;
    RMirInstruction *owner_local;
    RMirInstruction *await_instruction;
    RTypeId mutable_slice_type;
    RTypeId shared_slice_type;
    RTypeId bool_type;
} RCodegenAsyncMirSliceView;

static void r_codegen_test_replacement_preflight(void) {
    static const char source[] =
        "module test.replacement_preflight; async i32 main() { "
        "own i32* current = new i32(1); own i32* next = new i32(2); "
        "own i32* old = core::replace(&current, move next); return *old - 1; }";
    for (uint32_t mutation = 0U; mutation < 7U; ++mutation) {
        RFrontendContext *context = r_codegen_create_context(NULL);
        RCodegenBuffer output = {0};
        RMirInstruction *call = NULL;
        R_CODEGEN_CHECK(context != NULL);
        if (context == NULL)
            continue;
        R_CODEGEN_CHECK(r_codegen_add_text(context, "replacement_preflight.r", source));
        R_CODEGEN_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
        for (size_t index = 0U; index < context->mir_instruction_count; ++index) {
            RMirInstruction *candidate = &context->mir_instructions[index];
            if (candidate->kind == R_MIR_INSTRUCTION_STANDARD_CALL &&
                candidate->standard_operation == R_STANDARD_CALL_CORE_REPLACE) {
                call = candidate;
                break;
            }
        }
        R_CODEGEN_CHECK(call != NULL);
        if (call != NULL) {
            RMirInstruction *right = r_codegen_find_value_definition(
                context, context->mir_operands[call->first_operand + 1U]);
            R_CODEGEN_CHECK(right != NULL);
            switch (mutation) {
            case 0U:
                break;
            case 1U:
                call->operand_count = 1U;
                break;
            case 2U:
                call->auxiliary_type = R_TYPE_ID_INVALID;
                break;
            case 3U:
                call->call_borrow_mask.low = 0U;
                break;
            case 4U:
                call->result = R_MIR_VALUE_ID_INVALID;
                break;
            case 5U:
                if (right != NULL)
                    right->kind = R_MIR_INSTRUCTION_LOAD;
                break;
            case 6U:
                if (right != NULL)
                    right->is_async_staged_move = true;
                break;
            default:
                R_CODEGEN_CHECK(false);
                break;
            }
            R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &output) ==
                            (mutation == 0U ? R_FRONTEND_OK : R_FRONTEND_NOT_LOWERABLE));
            if (mutation != 0U) {
                R_CODEGEN_CHECK(output.call_count == 0U);
                R_CODEGEN_CHECK(output.length == 0U);
            }
        }
        r_codegen_dispose_buffer(&output);
        r_frontend_destroy(context);
    }
}

typedef enum RCodegenAsyncMirSliceMutation {
    R_CODEGEN_ASYNC_MIR_SLICE_FIXED_BOUND = 0,
    R_CODEGEN_ASYNC_MIR_SLICE_MISSING_LOWER,
    R_CODEGEN_ASYNC_MIR_SLICE_BOUND_TYPE,
    R_CODEGEN_ASYNC_MIR_SLICE_MUTABLE_FROM_SHARED,
    R_CODEGEN_ASYNC_MIR_SLICE_PROVENANCE,
    R_CODEGEN_ASYNC_MIR_SLICE_OPERATION,
    R_CODEGEN_ASYNC_MIR_SLICE_BASE_TYPE,
    R_CODEGEN_ASYNC_MIR_SLICE_RESULT,
    R_CODEGEN_ASYNC_MIR_SLICE_MUTATION_COUNT
} RCodegenAsyncMirSliceMutation;

static RFrontendContext *r_codegen_build_async_mir_slice(void) {
    static const char source[] = "module codegen.async_mir_slice_unit; "
                                 "struct Scratch { u8[4] bytes; }; "
                                 "protected async i32 ready() { return 5; } "
                                 "async i32 main() { "
                                 "own Scratch* scratch = new Scratch{}; "
                                 "usize lower = 1; usize upper = 3; "
                                 "{ u8[] mutable_view = scratch->bytes[lower..upper]; "
                                 "const u8[] shared_view = mutable_view; "
                                 "const u8[] tail = shared_view[1..2]; "
                                 "usize length = len(tail); if (length != 1) { return 1; } } "
                                 "try { task<i32> operation = ready(); "
                                 "i32 value = await move operation; return value - 5; "
                                 "} catch (std.async::start_error error) { "
                                 "error as void; return 2; } }";
    RFrontendContext *context = r_codegen_create_context(NULL);

    if ((context == NULL) || !r_codegen_add_text(context, "async_mir_slice_unit.r", source) ||
        (r_frontend_analyze(context) != R_FRONTEND_OK) ||
        (r_frontend_diagnostic_count(context) != 0U) ||
        (r_frontend_lower_mir(context) != R_FRONTEND_OK) ||
        (r_frontend_diagnostic_count(context) != 0U)) {
        r_frontend_destroy(context);
        return NULL;
    }
    return context;
}

static bool r_codegen_find_async_mir_slice(RFrontendContext *context,
                                           RCodegenAsyncMirSliceView *view) {
    size_t slice_count = 0U;
    size_t await_count = 0U;
    size_t index;

    if ((context == NULL) || (view == NULL)) {
        return false;
    }
    (void)memset(view, 0, sizeof(*view));
    for (index = 0U; index < context->semantic_type_count; ++index) {
        const RTypeId type_id = (RTypeId)(index + 1U);
        const RSemanticType *type = &context->semantic_types[index];

        if (type->kind == R_SEMANTIC_TYPE_BOOL) {
            view->bool_type = type_id;
        }
    }
    for (index = 0U; index < context->mir_instruction_count; ++index) {
        RMirInstruction *instruction = &context->mir_instructions[index];

        if (instruction->kind == R_MIR_INSTRUCTION_SLICE) {
            slice_count += 1U;
            if (slice_count == 1U) {
                view->fixed_range = instruction;
            } else if (slice_count == 2U) {
                view->full = instruction;
            } else if (slice_count == 3U) {
                view->dynamic_range = instruction;
            }
        } else if (instruction->kind == R_MIR_INSTRUCTION_AWAIT) {
            await_count += 1U;
            view->await_instruction = instruction;
        } else if (instruction->kind == R_MIR_INSTRUCTION_LOCAL) {
            const RSemanticType *type =
                (instruction->type == R_TYPE_ID_INVALID) ||
                        ((size_t)instruction->type > context->semantic_type_count)
                    ? NULL
                    : &context->semantic_types[(size_t)instruction->type - 1U];

            if ((type != NULL) && (type->kind == R_SEMANTIC_TYPE_OWN)) {
                view->owner_local = instruction;
            }
        }
    }
    if ((view->fixed_range != NULL) && (view->full != NULL) && (view->dynamic_range != NULL)) {
        view->mutable_slice_type = view->fixed_range->type;
        view->shared_slice_type = view->full->type;
    }
    return (slice_count == 3U) && (await_count == 1U) && (view->fixed_range != NULL) &&
           (view->full != NULL) && (view->dynamic_range != NULL) && (view->owner_local != NULL) &&
           (view->await_instruction != NULL) && (view->fixed_range->integer_value == UINT64_C(4)) &&
           (view->fixed_range->operand1 != R_MIR_VALUE_ID_INVALID) &&
           (view->full->operand1 == R_MIR_VALUE_ID_INVALID) &&
           (view->dynamic_range->type == view->shared_slice_type) &&
           (view->dynamic_range->integer_value == UINT64_MAX) &&
           (view->dynamic_range->operand1 != R_MIR_VALUE_ID_INVALID) &&
           (view->mutable_slice_type != R_TYPE_ID_INVALID) &&
           (view->shared_slice_type != R_TYPE_ID_INVALID) && (view->bool_type != R_TYPE_ID_INVALID);
}

static void r_codegen_test_async_mir_slice(void) {
    RFrontendContext *context = r_codegen_build_async_mir_slice();
    RCodegenAsyncMirSliceView view;
    RCodegenBuffer generated = {0};
    RCodegenBuffer repeated = {0};
    size_t mutation;

    R_CODEGEN_CHECK(context != NULL);
    if ((context != NULL) && r_codegen_find_async_mir_slice(context, &view)) {
        const RMirValueId slice_results[] = {
            view.fixed_range->result,
            view.full->result,
            view.dynamic_range->result,
        };
        size_t index;

        R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &generated) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(generated.call_count == 1U);
        R_CODEGEN_CHECK(generated.bytes != NULL);
        for (index = 0U; index < (sizeof(slice_results) / sizeof(slice_results[0])); ++index) {
            char stack_name[64];
            char frame_name[64];
            uint32_t ordinal;

            R_CODEGEN_CHECK(slice_results[index] != R_MIR_VALUE_ID_INVALID);
            if (slice_results[index] == R_MIR_VALUE_ID_INVALID) {
                continue;
            }
            ordinal = slice_results[index] - UINT32_C(1);

            R_CODEGEN_CHECK(
                snprintf(stack_name, sizeof(stack_name), "r_stack_r_v%08" PRIu32, ordinal) > 0);
            R_CODEGEN_CHECK(
                snprintf(frame_name, sizeof(frame_name), "frame->r_v%08" PRIu32, ordinal) > 0);
            R_CODEGEN_CHECK(strstr(generated.bytes, stack_name) != NULL);
            R_CODEGEN_CHECK(strstr(generated.bytes, frame_name) == NULL);
        }
        R_CODEGEN_CHECK(r_codegen_count_occurrences(
                            generated.bytes, "r_runtime_panic(R_RUNTIME_PANIC_BOUNDS,") == 2U);
        R_CODEGEN_CHECK(strstr(generated.bytes, ".r_data[(size_t)") != NULL);
        R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &repeated) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_codegen_buffers_equal(&generated, &repeated));
    } else if (context != NULL) {
        R_CODEGEN_CHECK(false);
    }
    r_codegen_dispose_buffer(&generated);
    r_codegen_dispose_buffer(&repeated);
    r_frontend_destroy(context);

    for (mutation = 0U; mutation < (size_t)R_CODEGEN_ASYNC_MIR_SLICE_MUTATION_COUNT; ++mutation) {
        RFrontendContext *invalid = r_codegen_build_async_mir_slice();
        RCodegenAsyncMirSliceView invalid_view;
        RCodegenBuffer output = {0};

        R_CODEGEN_CHECK(invalid != NULL);
        if ((invalid == NULL) || !r_codegen_find_async_mir_slice(invalid, &invalid_view)) {
            r_frontend_destroy(invalid);
            continue;
        }
        switch ((RCodegenAsyncMirSliceMutation)mutation) {
        case R_CODEGEN_ASYNC_MIR_SLICE_FIXED_BOUND:
            invalid_view.fixed_range->integer_value -= UINT64_C(1);
            break;
        case R_CODEGEN_ASYNC_MIR_SLICE_MISSING_LOWER:
            /* An open upper bound is `a[lo..]`; an upper bound without a lower one is not. */
            invalid_view.fixed_range->operand1 = R_MIR_VALUE_ID_INVALID;
            break;
        case R_CODEGEN_ASYNC_MIR_SLICE_BOUND_TYPE: {
            RMirInstruction *lower =
                r_codegen_find_value_definition(invalid, invalid_view.fixed_range->operand1);

            R_CODEGEN_CHECK(lower != NULL);
            if (lower != NULL) {
                lower->type = invalid_view.bool_type;
            }
            break;
        }
        case R_CODEGEN_ASYNC_MIR_SLICE_MUTABLE_FROM_SHARED:
            invalid_view.dynamic_range->type = invalid_view.mutable_slice_type;
            break;
        case R_CODEGEN_ASYNC_MIR_SLICE_PROVENANCE:
            invalid_view.full->borrow_origin = R_SYMBOL_ID_INVALID;
            break;
        case R_CODEGEN_ASYNC_MIR_SLICE_OPERATION:
            invalid_view.fixed_range->operation = R_TOKEN_DOT_DOT;
            break;
        case R_CODEGEN_ASYNC_MIR_SLICE_BASE_TYPE: {
            RMirInstruction *base =
                r_codegen_find_value_definition(invalid, invalid_view.fixed_range->operand0);

            R_CODEGEN_CHECK(base != NULL);
            if (base != NULL) {
                base->type = invalid_view.bool_type;
            }
            break;
        }
        case R_CODEGEN_ASYNC_MIR_SLICE_RESULT:
            invalid_view.full->result = R_MIR_VALUE_ID_INVALID;
            break;
        case R_CODEGEN_ASYNC_MIR_SLICE_MUTATION_COUNT:
        default:
            R_CODEGEN_CHECK(false);
            break;
        }
        R_CODEGEN_CHECK(r_frontend_emit_c17(invalid, r_codegen_write, &output) ==
                        R_FRONTEND_NOT_LOWERABLE);
        R_CODEGEN_CHECK(output.call_count == 0U);
        R_CODEGEN_CHECK(output.length == 0U);
        r_codegen_dispose_buffer(&output);
        r_frontend_destroy(invalid);
    }
}

typedef struct RCodegenAsyncIoWriteAllView {
    RMirInstruction *call;
    RMirInstruction *stream;
    RMirInstruction *buffer;
    RMirInstruction *deadline;
} RCodegenAsyncIoWriteAllView;

typedef enum RCodegenAsyncIoWriteAllMutation {
    R_CODEGEN_IO_WRITE_ALL_MISSING_BORROW = 0,
    R_CODEGEN_IO_WRITE_ALL_ARITY,
    R_CODEGEN_IO_WRITE_ALL_STREAM_TYPE,
    R_CODEGEN_IO_WRITE_ALL_BUFFER_NOT_STAGED,
    R_CODEGEN_IO_WRITE_ALL_BUFFER_NOT_MOVE,
    R_CODEGEN_IO_WRITE_ALL_DEADLINE_TYPE,
    R_CODEGEN_IO_WRITE_ALL_TASK_TYPE,
    R_CODEGEN_IO_WRITE_ALL_MUTATION_COUNT
} RCodegenAsyncIoWriteAllMutation;

static RFrontendContext *r_codegen_build_async_io_write_all(void) {
    RFrontendContext *context = r_codegen_create_context(NULL);

    if ((context == NULL) || !r_codegen_add_file(context, R_CODEGEN_ASYNC_IO_WRITE_ALL_PATH) ||
        (r_frontend_analyze(context) != R_FRONTEND_OK) ||
        (r_frontend_diagnostic_count(context) != 0U) ||
        (r_frontend_lower_mir(context) != R_FRONTEND_OK) ||
        (r_frontend_diagnostic_count(context) != 0U) || (context->mir_function_count != 1U)) {
        r_frontend_destroy(context);
        return NULL;
    }
    return context;
}

static bool r_codegen_find_async_io_write_all(RFrontendContext *context,
                                              RCodegenAsyncIoWriteAllView *view) {
    size_t index;
    size_t first_operand;

    if ((context == NULL) || (view == NULL)) {
        return false;
    }
    (void)memset(view, 0, sizeof(*view));
    for (index = 0U; index < context->mir_instruction_count; ++index) {
        RMirInstruction *instruction = &context->mir_instructions[index];

        if ((instruction->kind != R_MIR_INSTRUCTION_STANDARD_CALL) ||
            (instruction->standard_operation != R_STANDARD_CALL_IO_WRITE_ALL)) {
            continue;
        }
        if (view->call != NULL) {
            return false;
        }
        view->call = instruction;
    }
    if ((view->call == NULL) || (view->call->operand_count != UINT32_C(3))) {
        return false;
    }
    first_operand = (size_t)view->call->first_operand;
    if ((first_operand > context->mir_operand_count) ||
        ((size_t)view->call->operand_count > context->mir_operand_count - first_operand)) {
        return false;
    }
    view->stream = r_codegen_find_value_definition(context, context->mir_operands[first_operand]);
    view->buffer =
        r_codegen_find_value_definition(context, context->mir_operands[first_operand + 1U]);
    view->deadline =
        r_codegen_find_value_definition(context, context->mir_operands[first_operand + 2U]);
    return (view->stream != NULL) && (view->buffer != NULL) && (view->deadline != NULL);
}

static void r_codegen_test_async_io_write_all(void) {
    RFrontendContext *context = r_codegen_build_async_io_write_all();
    RCodegenAsyncIoWriteAllView view;
    RCodegenBuffer generated = {0};
    RCodegenBuffer repeated = {0};
    size_t mutation;

    R_CODEGEN_CHECK(context != NULL);
    if (context != NULL) {
        R_CODEGEN_CHECK(r_codegen_find_async_io_write_all(context, &view));
        R_CODEGEN_CHECK(view.call != NULL);
        R_CODEGEN_CHECK(view.stream != NULL);
        R_CODEGEN_CHECK(view.buffer != NULL);
        R_CODEGEN_CHECK(view.deadline != NULL);
        if ((view.call != NULL) && (view.buffer != NULL)) {
            R_CODEGEN_CHECK(view.call->call_borrow_mask.low == UINT64_C(1));
            R_CODEGEN_CHECK(view.call->call_borrow_mask.high == UINT64_C(0));
            R_CODEGEN_CHECK(view.buffer->kind == R_MIR_INSTRUCTION_MOVE);
            R_CODEGEN_CHECK(view.buffer->is_async_staged_move);
        }
        R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &generated) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(generated.call_count == 1U);
        R_CODEGEN_CHECK(generated.bytes != NULL);
        if (generated.bytes != NULL) {
            R_CODEGEN_CHECK(r_codegen_count_occurrences(
                                generated.bytes, "const RRuntimeArray r_io_buffer_before_") == 0U);
            R_CODEGEN_CHECK(r_codegen_count_occurrences(generated.bytes, "r_std_io_write_all(") ==
                            1U);
            R_CODEGEN_CHECK(r_codegen_count_occurrences(generated.bytes,
                                                        "RStdIoTaskStartResult r_io_start_") == 1U);
            R_CODEGEN_CHECK(r_codegen_count_occurrences(generated.bytes, ".task = NULL;") >= 1U);
        }
        R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &repeated) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_codegen_buffers_equal(&generated, &repeated));
    }
    r_codegen_dispose_buffer(&generated);
    r_codegen_dispose_buffer(&repeated);
    r_frontend_destroy(context);

    for (mutation = 0U; mutation < (size_t)R_CODEGEN_IO_WRITE_ALL_MUTATION_COUNT; ++mutation) {
        RFrontendContext *invalid = r_codegen_build_async_io_write_all();
        RCodegenAsyncIoWriteAllView invalid_view;
        RCodegenBuffer output = {0};

        R_CODEGEN_CHECK(invalid != NULL);
        if ((invalid == NULL) || !r_codegen_find_async_io_write_all(invalid, &invalid_view) ||
            (invalid_view.call == NULL) || (invalid_view.stream == NULL) ||
            (invalid_view.buffer == NULL) || (invalid_view.deadline == NULL)) {
            r_frontend_destroy(invalid);
            continue;
        }
        switch ((RCodegenAsyncIoWriteAllMutation)mutation) {
        case R_CODEGEN_IO_WRITE_ALL_MISSING_BORROW:
            invalid_view.call->call_borrow_mask.low = UINT64_C(0);
            invalid_view.call->call_borrow_mask.high = UINT64_C(0);
            break;
        case R_CODEGEN_IO_WRITE_ALL_ARITY:
            invalid_view.call->operand_count -= UINT32_C(1);
            break;
        case R_CODEGEN_IO_WRITE_ALL_STREAM_TYPE:
            invalid_view.stream->type = invalid_view.buffer->type;
            break;
        case R_CODEGEN_IO_WRITE_ALL_BUFFER_NOT_STAGED:
            invalid_view.buffer->is_async_staged_move = false;
            break;
        case R_CODEGEN_IO_WRITE_ALL_BUFFER_NOT_MOVE:
            invalid_view.buffer->kind = R_MIR_INSTRUCTION_LOAD;
            break;
        case R_CODEGEN_IO_WRITE_ALL_DEADLINE_TYPE:
            invalid_view.deadline->type = invalid_view.buffer->type;
            break;
        case R_CODEGEN_IO_WRITE_ALL_TASK_TYPE:
            invalid_view.call->auxiliary_type = R_TYPE_ID_INVALID;
            break;
        case R_CODEGEN_IO_WRITE_ALL_MUTATION_COUNT:
        default:
            R_CODEGEN_CHECK(false);
            break;
        }
        R_CODEGEN_CHECK(r_frontend_emit_c17(invalid, r_codegen_write, &output) ==
                        R_FRONTEND_NOT_LOWERABLE);
        R_CODEGEN_CHECK(output.call_count == 0U);
        R_CODEGEN_CHECK(output.length == 0U);
        r_codegen_dispose_buffer(&output);
        r_frontend_destroy(invalid);
    }
}

typedef struct RCodegenAsyncIoReadView {
    RMirInstruction *call;
    RMirInstruction *stream;
    RMirInstruction *buffer;
    RMirInstruction *deadline;
} RCodegenAsyncIoReadView;

typedef enum RCodegenAsyncIoReadMutation {
    R_CODEGEN_IO_READ_MISSING_BORROW = 0,
    R_CODEGEN_IO_READ_ARITY,
    R_CODEGEN_IO_READ_STREAM_TYPE,
    R_CODEGEN_IO_READ_BUFFER_NOT_STAGED,
    R_CODEGEN_IO_READ_BUFFER_NOT_MOVE,
    R_CODEGEN_IO_READ_DEADLINE_TYPE,
    R_CODEGEN_IO_READ_TASK_TYPE,
    R_CODEGEN_IO_READ_MUTATION_COUNT
} RCodegenAsyncIoReadMutation;

static RFrontendContext *r_codegen_build_async_io_read(void) {
    RFrontendContext *context = r_codegen_create_context(NULL);

    if ((context == NULL) || !r_codegen_add_file(context, R_CODEGEN_ASYNC_IO_READ_PATH) ||
        (r_frontend_analyze(context) != R_FRONTEND_OK) ||
        (r_frontend_diagnostic_count(context) != 0U) ||
        (r_frontend_lower_mir(context) != R_FRONTEND_OK) ||
        (r_frontend_diagnostic_count(context) != 0U) || (context->mir_function_count != 1U)) {
        r_frontend_destroy(context);
        return NULL;
    }
    return context;
}

static bool r_codegen_find_async_io_read(RFrontendContext *context, RCodegenAsyncIoReadView *view) {
    size_t index;
    size_t first_operand;

    if ((context == NULL) || (view == NULL)) {
        return false;
    }
    (void)memset(view, 0, sizeof(*view));
    for (index = 0U; index < context->mir_instruction_count; ++index) {
        RMirInstruction *instruction = &context->mir_instructions[index];

        if ((instruction->kind != R_MIR_INSTRUCTION_STANDARD_CALL) ||
            (instruction->standard_operation != R_STANDARD_CALL_IO_READ)) {
            continue;
        }
        if (view->call != NULL) {
            return false;
        }
        view->call = instruction;
    }
    if ((view->call == NULL) || (view->call->operand_count != UINT32_C(3))) {
        return false;
    }
    first_operand = (size_t)view->call->first_operand;
    if ((first_operand > context->mir_operand_count) ||
        ((size_t)view->call->operand_count > context->mir_operand_count - first_operand)) {
        return false;
    }
    view->stream = r_codegen_find_value_definition(context, context->mir_operands[first_operand]);
    view->buffer =
        r_codegen_find_value_definition(context, context->mir_operands[first_operand + 1U]);
    view->deadline =
        r_codegen_find_value_definition(context, context->mir_operands[first_operand + 2U]);
    return (view->stream != NULL) && (view->buffer != NULL) && (view->deadline != NULL);
}

static void r_codegen_test_async_io_read(void) {
    RFrontendContext *context = r_codegen_build_async_io_read();
    RCodegenAsyncIoReadView view;
    RCodegenBuffer generated = {0};
    RCodegenBuffer repeated = {0};
    size_t mutation;

    R_CODEGEN_CHECK(context != NULL);
    if (context != NULL) {
        R_CODEGEN_CHECK(r_codegen_find_async_io_read(context, &view));
        R_CODEGEN_CHECK(view.call != NULL);
        R_CODEGEN_CHECK(view.stream != NULL);
        R_CODEGEN_CHECK(view.buffer != NULL);
        R_CODEGEN_CHECK(view.deadline != NULL);
        if ((view.call != NULL) && (view.buffer != NULL)) {
            R_CODEGEN_CHECK(view.call->call_borrow_mask.low == UINT64_C(1));
            R_CODEGEN_CHECK(view.call->call_borrow_mask.high == UINT64_C(0));
            R_CODEGEN_CHECK(view.buffer->kind == R_MIR_INSTRUCTION_MOVE);
            R_CODEGEN_CHECK(view.buffer->is_async_staged_move);
        }
        R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &generated) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(generated.call_count == 1U);
        R_CODEGEN_CHECK(generated.bytes != NULL);
        if (generated.bytes != NULL) {
            R_CODEGEN_CHECK(r_codegen_count_occurrences(
                                generated.bytes, "const RRuntimeArray r_io_buffer_before_") == 0U);
            R_CODEGEN_CHECK(r_codegen_count_occurrences(generated.bytes, "r_std_io_read(") == 1U);
            R_CODEGEN_CHECK(r_codegen_count_occurrences(generated.bytes,
                                                        "RStdIoTaskStartResult r_io_start_") == 1U);
            R_CODEGEN_CHECK(r_codegen_count_occurrences(generated.bytes, ".task = NULL;") >= 1U);
            R_CODEGEN_CHECK(strstr(generated.bytes, "r_std_io_read_result_destroy(") != NULL);
        }
        R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &repeated) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_codegen_buffers_equal(&generated, &repeated));
    }
    r_codegen_dispose_buffer(&generated);
    r_codegen_dispose_buffer(&repeated);
    r_frontend_destroy(context);

    for (mutation = 0U; mutation < (size_t)R_CODEGEN_IO_READ_MUTATION_COUNT; ++mutation) {
        RFrontendContext *invalid = r_codegen_build_async_io_read();
        RCodegenAsyncIoReadView invalid_view;
        RCodegenBuffer output = {0};

        R_CODEGEN_CHECK(invalid != NULL);
        if ((invalid == NULL) || !r_codegen_find_async_io_read(invalid, &invalid_view) ||
            (invalid_view.call == NULL) || (invalid_view.stream == NULL) ||
            (invalid_view.buffer == NULL) || (invalid_view.deadline == NULL)) {
            r_frontend_destroy(invalid);
            continue;
        }
        switch ((RCodegenAsyncIoReadMutation)mutation) {
        case R_CODEGEN_IO_READ_MISSING_BORROW:
            invalid_view.call->call_borrow_mask.low = UINT64_C(0);
            invalid_view.call->call_borrow_mask.high = UINT64_C(0);
            break;
        case R_CODEGEN_IO_READ_ARITY:
            invalid_view.call->operand_count -= UINT32_C(1);
            break;
        case R_CODEGEN_IO_READ_STREAM_TYPE:
            invalid_view.stream->type = invalid_view.buffer->type;
            break;
        case R_CODEGEN_IO_READ_BUFFER_NOT_STAGED:
            invalid_view.buffer->is_async_staged_move = false;
            break;
        case R_CODEGEN_IO_READ_BUFFER_NOT_MOVE:
            invalid_view.buffer->kind = R_MIR_INSTRUCTION_LOAD;
            break;
        case R_CODEGEN_IO_READ_DEADLINE_TYPE:
            invalid_view.deadline->type = invalid_view.buffer->type;
            break;
        case R_CODEGEN_IO_READ_TASK_TYPE:
            invalid_view.call->auxiliary_type = R_TYPE_ID_INVALID;
            break;
        case R_CODEGEN_IO_READ_MUTATION_COUNT:
        default:
            R_CODEGEN_CHECK(false);
            break;
        }
        R_CODEGEN_CHECK(r_frontend_emit_c17(invalid, r_codegen_write, &output) ==
                        R_FRONTEND_NOT_LOWERABLE);
        R_CODEGEN_CHECK(output.call_count == 0U);
        R_CODEGEN_CHECK(output.length == 0U);
        r_codegen_dispose_buffer(&output);
        r_frontend_destroy(invalid);
    }
}

static RHirNode *r_codegen_find_stdout_call(RFrontendContext *context) {
    RHirNode *result = NULL;
    size_t index;

    if (context == NULL) {
        return NULL;
    }
    for (index = 0U; index < context->hir_node_count; ++index) {
        RHirNode *node = &context->hir_nodes[index];

        if ((node->kind != R_HIR_STANDARD_CALL) ||
            (node->standard_operation != R_STANDARD_CALL_IO_STDOUT)) {
            continue;
        }
        if (result != NULL) {
            return NULL;
        }
        result = node;
    }
    return result;
}

static RMirInstruction *r_codegen_find_stdout_instruction(RFrontendContext *context) {
    RMirInstruction *result = NULL;
    size_t index;

    if (context == NULL) {
        return NULL;
    }
    for (index = 0U; index < context->mir_instruction_count; ++index) {
        RMirInstruction *instruction = &context->mir_instructions[index];

        if ((instruction->kind != R_MIR_INSTRUCTION_STANDARD_CALL) ||
            (instruction->standard_operation != R_STANDARD_CALL_IO_STDOUT)) {
            continue;
        }
        if (result != NULL) {
            return NULL;
        }
        result = instruction;
    }
    return result;
}

static RTypeId r_codegen_append_const_type(RFrontendContext *context, RTypeId base) {
    RSemanticType *type;
    size_t required;

    if ((context == NULL) || (base == R_TYPE_ID_INVALID)) {
        return R_TYPE_ID_INVALID;
    }
    if (context->semantic_type_count >= (size_t)UINT32_MAX) {
        return R_TYPE_ID_INVALID;
    }
    required = context->semantic_type_count + 1U;
    if (!r_grow_array(context,
                      (void **)&context->semantic_types,
                      &context->semantic_type_capacity,
                      sizeof(*context->semantic_types),
                      required)) {
        return R_TYPE_ID_INVALID;
    }
    type = &context->semantic_types[context->semantic_type_count];
    *type = (RSemanticType){
        .kind = R_SEMANTIC_TYPE_CONST,
        .base = base,
        .second = R_TYPE_ID_INVALID,
        .length = UINT64_C(0),
        .flags = R_SEMANTIC_TYPE_FLAG_NONE,
    };
    context->semantic_type_count = required;
    return (RTypeId)required;
}

static RFrontendContext *r_codegen_build_stdout(const char *path) {
    RFrontendContext *context = r_codegen_create_context(NULL);

    if ((context == NULL) || !r_codegen_add_file(context, path) ||
        (r_frontend_analyze(context) != R_FRONTEND_OK) ||
        (r_frontend_diagnostic_count(context) != 0U) ||
        (r_frontend_lower_mir(context) != R_FRONTEND_OK) ||
        (r_frontend_diagnostic_count(context) != 0U)) {
        r_frontend_destroy(context);
        return NULL;
    }
    return context;
}

static void r_codegen_test_stdout(void) {
    typedef enum RCodegenStdoutMutation {
        R_CODEGEN_STDOUT_CHILD = 0,
        R_CODEGEN_STDOUT_AUXILIARY_TYPE,
        R_CODEGEN_STDOUT_CONST_RESULT,
        R_CODEGEN_STDOUT_RESULT_FLAGS,
        R_CODEGEN_STDOUT_RESULT_BASE,
        R_CODEGEN_STDOUT_RESULT_SECOND,
        R_CODEGEN_STDOUT_MUTATION_COUNT
    } RCodegenStdoutMutation;
    static const struct {
        const char *path;
        bool is_async;
    } cases[] = {
        {R_CODEGEN_STDOUT_PATH, false},
        {R_CODEGEN_ASYNC_STDOUT_PATH, true},
    };
    size_t index;

    for (index = 0U; index < (sizeof(cases) / sizeof(cases[0])); ++index) {
        RFrontendContext *context = r_codegen_build_stdout(cases[index].path);
        RCodegenBuffer generated = {0};
        RCodegenBuffer repeated = {0};
        RHirNode *call;

        R_CODEGEN_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        call = r_codegen_find_stdout_call(context);
        R_CODEGEN_CHECK(call != NULL);
        if (call != NULL) {
            R_CODEGEN_CHECK(call->child_count == UINT32_C(0));
            R_CODEGEN_CHECK(call->auxiliary_type == call->type);
        }
        R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &generated) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(generated.call_count == 1U);
        R_CODEGEN_CHECK(r_codegen_count_occurrences(generated.bytes, "r_std_io_stdout(") == 1U);
        R_CODEGEN_CHECK(r_codegen_count_occurrences(generated.bytes, "#include \"r_std_io.h\"") ==
                        1U);
        R_CODEGEN_CHECK(
            r_codegen_count_occurrences(generated.bytes, "r_std_io_output_move_initialize(") == 1U);
        R_CODEGEN_CHECK(r_codegen_count_occurrences(generated.bytes, "r_std_io_output_destroy(") ==
                        1U);
        R_CODEGEN_CHECK(r_codegen_count_occurrences(generated.bytes, "r_std_io_write_all(") ==
                        (cases[index].is_async ? 1U : 0U));
        R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &repeated) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_codegen_buffers_equal(&generated, &repeated));
        r_codegen_dispose_buffer(&generated);
        r_codegen_dispose_buffer(&repeated);
        r_frontend_destroy(context);
    }

    for (index = 0U; index < (size_t)R_CODEGEN_STDOUT_MUTATION_COUNT; ++index) {
        RFrontendContext *invalid = r_codegen_build_stdout(R_CODEGEN_STDOUT_PATH);
        RCodegenBuffer output = {0};
        RHirNode *call = r_codegen_find_stdout_call(invalid);
        RSemanticType *output_type;
        RTypeId i32_type = R_TYPE_ID_INVALID;

        R_CODEGEN_CHECK((invalid != NULL) && (call != NULL));
        if ((invalid == NULL) || (call == NULL)) {
            r_frontend_destroy(invalid);
            continue;
        }
        output_type = &invalid->semantic_types[(size_t)call->type - 1U];
        R_CODEGEN_CHECK(r_semantic_type_from_token(invalid, R_TOKEN_KW_I32, &i32_type));
        switch ((RCodegenStdoutMutation)index) {
        case R_CODEGEN_STDOUT_CHILD:
            call->child_count = UINT32_C(1);
            break;
        case R_CODEGEN_STDOUT_AUXILIARY_TYPE:
            call->auxiliary_type = R_TYPE_ID_INVALID;
            break;
        case R_CODEGEN_STDOUT_CONST_RESULT: {
            const RTypeId const_type = r_codegen_append_const_type(invalid, call->type);

            R_CODEGEN_CHECK(const_type != R_TYPE_ID_INVALID);
            call->type = const_type;
            call->auxiliary_type = const_type;
            break;
        }
        case R_CODEGEN_STDOUT_RESULT_FLAGS:
            output_type->flags = R_SEMANTIC_TYPE_FLAG_SHARED;
            break;
        case R_CODEGEN_STDOUT_RESULT_BASE:
            output_type->base = i32_type;
            break;
        case R_CODEGEN_STDOUT_RESULT_SECOND:
            output_type->second = i32_type;
            break;
        case R_CODEGEN_STDOUT_MUTATION_COUNT:
        default:
            R_CODEGEN_CHECK(false);
            break;
        }
        R_CODEGEN_CHECK(r_frontend_emit_c17(invalid, r_codegen_write, &output) ==
                        R_FRONTEND_NOT_LOWERABLE);
        R_CODEGEN_CHECK(output.call_count == 0U);
        R_CODEGEN_CHECK(output.length == 0U);
        r_codegen_dispose_buffer(&output);
        r_frontend_destroy(invalid);
    }

    {
        RFrontendContext *invalid = r_codegen_build_stdout(R_CODEGEN_ASYNC_STDOUT_PATH);
        RCodegenBuffer output = {0};
        RMirInstruction *instruction = r_codegen_find_stdout_instruction(invalid);

        R_CODEGEN_CHECK((invalid != NULL) && (instruction != NULL));
        if ((invalid != NULL) && (instruction != NULL)) {
            const RTypeId const_type = r_codegen_append_const_type(invalid, instruction->type);

            R_CODEGEN_CHECK(const_type != R_TYPE_ID_INVALID);
            instruction->type = const_type;
            instruction->auxiliary_type = const_type;
            R_CODEGEN_CHECK(r_frontend_emit_c17(invalid, r_codegen_write, &output) ==
                            R_FRONTEND_NOT_LOWERABLE);
            R_CODEGEN_CHECK(output.call_count == 0U);
            R_CODEGEN_CHECK(output.length == 0U);
        }
        r_codegen_dispose_buffer(&output);
        r_frontend_destroy(invalid);
    }
}

static RHirNode *r_codegen_find_stdin_call(RFrontendContext *context) {
    RHirNode *result = NULL;
    size_t index;

    if (context == NULL) {
        return NULL;
    }
    for (index = 0U; index < context->hir_node_count; ++index) {
        RHirNode *node = &context->hir_nodes[index];

        if ((node->kind != R_HIR_STANDARD_CALL) ||
            (node->standard_operation != R_STANDARD_CALL_IO_STDIN)) {
            continue;
        }
        if (result != NULL) {
            return NULL;
        }
        result = node;
    }
    return result;
}

static RMirInstruction *r_codegen_find_stdin_instruction(RFrontendContext *context) {
    RMirInstruction *result = NULL;
    size_t index;

    if (context == NULL) {
        return NULL;
    }
    for (index = 0U; index < context->mir_instruction_count; ++index) {
        RMirInstruction *instruction = &context->mir_instructions[index];

        if ((instruction->kind != R_MIR_INSTRUCTION_STANDARD_CALL) ||
            (instruction->standard_operation != R_STANDARD_CALL_IO_STDIN)) {
            continue;
        }
        if (result != NULL) {
            return NULL;
        }
        result = instruction;
    }
    return result;
}

static RFrontendContext *r_codegen_build_stdin(const char *path) {
    RFrontendContext *context = r_codegen_create_context(NULL);

    if ((context == NULL) || !r_codegen_add_file(context, path) ||
        (r_frontend_analyze(context) != R_FRONTEND_OK) ||
        (r_frontend_diagnostic_count(context) != 0U) ||
        (r_frontend_lower_mir(context) != R_FRONTEND_OK) ||
        (r_frontend_diagnostic_count(context) != 0U)) {
        r_frontend_destroy(context);
        return NULL;
    }
    return context;
}

static void r_codegen_test_stdin(void) {
    typedef enum RCodegenStdinMutation {
        R_CODEGEN_STDIN_CHILD = 0,
        R_CODEGEN_STDIN_AUXILIARY_TYPE,
        R_CODEGEN_STDIN_CONST_RESULT,
        R_CODEGEN_STDIN_RESULT_FLAGS,
        R_CODEGEN_STDIN_RESULT_BASE,
        R_CODEGEN_STDIN_RESULT_SECOND,
        R_CODEGEN_STDIN_MUTATION_COUNT
    } RCodegenStdinMutation;
    static const char *const paths[] = {
        R_CODEGEN_STDIN_PATH,
        R_CODEGEN_ASYNC_STDIN_PATH,
    };
    size_t index;

    for (index = 0U; index < (sizeof(paths) / sizeof(paths[0])); ++index) {
        RFrontendContext *context = r_codegen_build_stdin(paths[index]);
        RCodegenBuffer generated = {0};
        RCodegenBuffer repeated = {0};
        RHirNode *call;

        R_CODEGEN_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        call = r_codegen_find_stdin_call(context);
        R_CODEGEN_CHECK(call != NULL);
        if (call != NULL) {
            R_CODEGEN_CHECK(call->child_count == UINT32_C(0));
            R_CODEGEN_CHECK(call->auxiliary_type == call->type);
        }
        R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &generated) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(generated.call_count == 1U);
        R_CODEGEN_CHECK(r_codegen_count_occurrences(generated.bytes, "r_std_io_stdin(") == 1U);
        R_CODEGEN_CHECK(r_codegen_count_occurrences(generated.bytes, "#include \"r_std_io.h\"") ==
                        1U);
        R_CODEGEN_CHECK(
            r_codegen_count_occurrences(generated.bytes, "r_std_io_input_move_initialize(") == 1U);
        R_CODEGEN_CHECK(r_codegen_count_occurrences(generated.bytes, "r_std_io_input_destroy(") ==
                        1U);
        R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &repeated) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_codegen_buffers_equal(&generated, &repeated));
        r_codegen_dispose_buffer(&generated);
        r_codegen_dispose_buffer(&repeated);
        r_frontend_destroy(context);
    }

    for (index = 0U; index < (size_t)R_CODEGEN_STDIN_MUTATION_COUNT; ++index) {
        RFrontendContext *invalid = r_codegen_build_stdin(R_CODEGEN_STDIN_PATH);
        RCodegenBuffer output = {0};
        RHirNode *call = r_codegen_find_stdin_call(invalid);
        RSemanticType *input_type;
        RTypeId i32_type = R_TYPE_ID_INVALID;

        R_CODEGEN_CHECK((invalid != NULL) && (call != NULL));
        if ((invalid == NULL) || (call == NULL)) {
            r_frontend_destroy(invalid);
            continue;
        }
        input_type = &invalid->semantic_types[(size_t)call->type - 1U];
        R_CODEGEN_CHECK(r_semantic_type_from_token(invalid, R_TOKEN_KW_I32, &i32_type));
        switch ((RCodegenStdinMutation)index) {
        case R_CODEGEN_STDIN_CHILD:
            call->child_count = UINT32_C(1);
            break;
        case R_CODEGEN_STDIN_AUXILIARY_TYPE:
            call->auxiliary_type = R_TYPE_ID_INVALID;
            break;
        case R_CODEGEN_STDIN_CONST_RESULT: {
            const RTypeId const_type = r_codegen_append_const_type(invalid, call->type);

            R_CODEGEN_CHECK(const_type != R_TYPE_ID_INVALID);
            call->type = const_type;
            call->auxiliary_type = const_type;
            break;
        }
        case R_CODEGEN_STDIN_RESULT_FLAGS:
            input_type->flags = R_SEMANTIC_TYPE_FLAG_SHARED;
            break;
        case R_CODEGEN_STDIN_RESULT_BASE:
            input_type->base = i32_type;
            break;
        case R_CODEGEN_STDIN_RESULT_SECOND:
            input_type->second = i32_type;
            break;
        case R_CODEGEN_STDIN_MUTATION_COUNT:
        default:
            R_CODEGEN_CHECK(false);
            break;
        }
        R_CODEGEN_CHECK(r_frontend_emit_c17(invalid, r_codegen_write, &output) ==
                        R_FRONTEND_NOT_LOWERABLE);
        R_CODEGEN_CHECK(output.call_count == 0U);
        R_CODEGEN_CHECK(output.length == 0U);
        r_codegen_dispose_buffer(&output);
        r_frontend_destroy(invalid);
    }

    {
        RFrontendContext *invalid = r_codegen_build_stdin(R_CODEGEN_ASYNC_STDIN_PATH);
        RCodegenBuffer output = {0};
        RMirInstruction *instruction = r_codegen_find_stdin_instruction(invalid);

        R_CODEGEN_CHECK((invalid != NULL) && (instruction != NULL));
        if ((invalid != NULL) && (instruction != NULL)) {
            const RTypeId const_type = r_codegen_append_const_type(invalid, instruction->type);

            R_CODEGEN_CHECK(const_type != R_TYPE_ID_INVALID);
            instruction->type = const_type;
            instruction->auxiliary_type = const_type;
            R_CODEGEN_CHECK(r_frontend_emit_c17(invalid, r_codegen_write, &output) ==
                            R_FRONTEND_NOT_LOWERABLE);
            R_CODEGEN_CHECK(output.call_count == 0U);
            R_CODEGEN_CHECK(output.length == 0U);
        }
        r_codegen_dispose_buffer(&output);
        r_frontend_destroy(invalid);
    }
}

static void r_codegen_test_async_main_arguments(void) {
    RFrontendContext *context = r_codegen_create_context(NULL);
    RCodegenBuffer generated = {0};
    RCodegenBuffer repeated = {0};

    R_CODEGEN_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_CODEGEN_CHECK(r_codegen_add_file(context, R_CODEGEN_ASYNC_MAIN_ARGS_PATH));
    R_CODEGEN_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &generated) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(generated.call_count == 1U);
    R_CODEGEN_CHECK(generated.bytes != NULL);
    if (generated.bytes != NULL) {
        const char *snapshot =
            strstr(generated.bytes, "r_runtime_hosted_argument_snapshot(&r_startup_snapshot)");
        const char *initial_state = strstr(generated.bytes, "if (frame->r_state == UINT32_C(1))");
        const char *slice_data =
            strstr(generated.bytes, "r_startup_args.r_data = r_startup_snapshot.arguments;");
        const char *slice_length =
            strstr(generated.bytes, "r_startup_args.r_len = r_startup_snapshot.count;");
        const char *argument_use = strstr(generated.bytes, "= r_startup_args.r_len;");
        const char *await = strstr(generated.bytes, "r_runtime_task_execution_await(");
        const char *main_frame = strstr(generated.bytes, "typedef struct r_async_frame_00000002 {");
        const char *main_frame_end =
            main_frame == NULL ? NULL : strstr(main_frame, "} r_async_frame_00000002;");
        const char *view_in_or_after_frame =
            main_frame == NULL ? NULL : strstr(main_frame, "RRuntimeStringView");

        r_codegen_check_hosted_stack_bootstrap(generated.bytes);
        R_CODEGEN_CHECK(strstr(generated.bytes, "const RRuntimeStringView *r_data;") != NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes, "r_p00000000") == NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes, "r_async_wrapper_context_00000002") == NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes, "RRuntimeStringView r_stack_r_l00000001") != NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes, "r_stack_r_l00000001.length") != NULL);
        R_CODEGEN_CHECK(main_frame != NULL);
        R_CODEGEN_CHECK(main_frame_end != NULL);
        R_CODEGEN_CHECK((view_in_or_after_frame == NULL) ||
                        ((main_frame_end != NULL) && (view_in_or_after_frame > main_frame_end)));
        R_CODEGEN_CHECK(snapshot != NULL);
        R_CODEGEN_CHECK(initial_state != NULL);
        R_CODEGEN_CHECK(slice_data != NULL);
        R_CODEGEN_CHECK(slice_length != NULL);
        R_CODEGEN_CHECK(argument_use != NULL);
        R_CODEGEN_CHECK(await != NULL);
        if ((initial_state != NULL) && (snapshot != NULL) && (slice_data != NULL) &&
            (slice_length != NULL) && (argument_use != NULL) && (await != NULL)) {
            R_CODEGEN_CHECK(initial_state < snapshot);
            R_CODEGEN_CHECK(snapshot < slice_data);
            R_CODEGEN_CHECK(slice_data < slice_length);
            R_CODEGEN_CHECK(slice_length < argument_use);
            R_CODEGEN_CHECK(argument_use < await);
        }
    }
    R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &repeated) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(r_codegen_buffers_equal(&generated, &repeated));
    r_codegen_dispose_buffer(&generated);
    r_codegen_dispose_buffer(&repeated);
    r_frontend_destroy(context);
}

static void r_codegen_test_sync_main_arguments(void) {
    RFrontendContext *context = r_codegen_create_context(NULL);
    RCodegenBuffer generated = {0};
    RCodegenBuffer repeated = {0};

    R_CODEGEN_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_CODEGEN_CHECK(r_codegen_add_file(context, R_CODEGEN_SYNC_MAIN_ARGS_PATH));
    R_CODEGEN_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &generated) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(generated.call_count == 1U);
    R_CODEGEN_CHECK(generated.bytes != NULL);
    if (generated.bytes != NULL) {
        const char *hosted_start = strstr(generated.bytes, "r_runtime_hosted_start(argc, argv)");
        const char *start_guard = strstr(generated.bytes, "if (!r_start.started)");
        const char *snapshot =
            strstr(generated.bytes, "r_runtime_hosted_argument_snapshot(&r_startup_snapshot)");
        const char *slice_data =
            strstr(generated.bytes, "r_startup_args.r_data = r_startup_snapshot.arguments;");
        const char *slice_length =
            strstr(generated.bytes, "r_startup_args.r_len = r_startup_snapshot.count;");
        const char *entry_call = strstr(generated.bytes, "(&r_outcome, r_startup_args);");
        const char *finish = strstr(generated.bytes, "r_runtime_hosted_finish(r_result)");

        r_codegen_check_hosted_stack_bootstrap(generated.bytes);
        R_CODEGEN_CHECK(
            strstr(generated.bytes, "RRuntimeArgumentSnapshotView r_startup_snapshot") != NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes, "const RRuntimeStringView *r_data;") != NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes, ".r_data[(size_t)") != NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes, "r_async_frame_") == NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes, "r_runtime_task_") == NULL);
        R_CODEGEN_CHECK(hosted_start != NULL);
        R_CODEGEN_CHECK(start_guard != NULL);
        R_CODEGEN_CHECK(snapshot != NULL);
        R_CODEGEN_CHECK(slice_data != NULL);
        R_CODEGEN_CHECK(slice_length != NULL);
        R_CODEGEN_CHECK(entry_call != NULL);
        R_CODEGEN_CHECK(finish != NULL);
        if ((hosted_start != NULL) && (start_guard != NULL) && (snapshot != NULL) &&
            (slice_data != NULL) && (slice_length != NULL) && (entry_call != NULL) &&
            (finish != NULL)) {
            R_CODEGEN_CHECK(hosted_start < start_guard);
            R_CODEGEN_CHECK(start_guard < snapshot);
            R_CODEGEN_CHECK(snapshot < slice_data);
            R_CODEGEN_CHECK(slice_data < slice_length);
            R_CODEGEN_CHECK(slice_length < entry_call);
            R_CODEGEN_CHECK(entry_call < finish);
        }
    }
    R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &repeated) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(r_codegen_buffers_equal(&generated, &repeated));
    r_codegen_dispose_buffer(&generated);
    r_codegen_dispose_buffer(&repeated);
    r_frontend_destroy(context);
}

static void r_codegen_test_async_copy_arguments(void) {
    RFrontendContext *context = r_codegen_create_context(NULL);
    RCodegenBuffer generated = {0};
    RCodegenBuffer repeated = {0};

    R_CODEGEN_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_CODEGEN_CHECK(r_codegen_add_file(context, R_CODEGEN_ASYNC_COPY_ARGS_PATH));
    R_CODEGEN_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &generated) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(generated.call_count == 1U);
    R_CODEGEN_CHECK(generated.bytes != NULL);
    if (generated.bytes != NULL) {
        const char *first_initialize =
            strstr(generated.bytes, "destination->r_p00000000 = source->r_v00000003;");
        const char *second_initialize =
            strstr(generated.bytes, "destination->r_p00000001 = source->r_v00000004;");
        const char *third_initialize =
            strstr(generated.bytes, "destination->r_p00000002 = source->r_v00000005;");
        const char *start = strstr(generated.bytes,
                                   "r_async_launch_00000001_gate("
                                   "\n                &frame->r_v00000006, "
                                   "r_async_call_initialize_00000002_00000006_gate, frame, 0);");

        R_CODEGEN_CHECK(strstr(generated.bytes, "int32_t r_p00000000;") != NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes, "int32_t r_p00000001;") != NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes, "int32_t r_p00000002;") != NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes, "r_async_frame_00000001 r_staged_") == NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes, "r_root_frame") == NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes, "r_runtime_task_start_commit_initialize(") != NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes, "r_async_frame_move_") == NULL);
        R_CODEGEN_CHECK(first_initialize != NULL);
        R_CODEGEN_CHECK(second_initialize != NULL);
        R_CODEGEN_CHECK(third_initialize != NULL);
        R_CODEGEN_CHECK(start != NULL);
        if ((first_initialize != NULL) && (second_initialize != NULL) &&
            (third_initialize != NULL) && (start != NULL)) {
            R_CODEGEN_CHECK(first_initialize < second_initialize);
            R_CODEGEN_CHECK(second_initialize < third_initialize);
            R_CODEGEN_CHECK(third_initialize < start);
        }
    }
    R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &repeated) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(r_codegen_buffers_equal(&generated, &repeated));
    r_codegen_dispose_buffer(&generated);
    r_codegen_dispose_buffer(&repeated);
    r_frontend_destroy(context);
}

static void r_codegen_test_async_copy_aggregate_and_transient_storage(void) {
    RFrontendContext *context = r_codegen_create_context(NULL);
    RCodegenBuffer generated = {0};
    RCodegenBuffer repeated = {0};

    R_CODEGEN_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_CODEGEN_CHECK(r_codegen_add_file(context, R_CODEGEN_ASYNC_COPY_AGGREGATE_PATH));
    R_CODEGEN_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(r_frontend_diagnostic_count(context) == 0U);
    R_CODEGEN_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(r_frontend_diagnostic_count(context) == 0U);
    R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &generated) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(generated.call_count == 1U);
    R_CODEGEN_CHECK(generated.bytes != NULL);
    if (generated.bytes != NULL) {
        const char *inspect_frame =
            strstr(generated.bytes, "typedef struct r_async_frame_00000001 {");
        const char *inspect_frame_end =
            inspect_frame == NULL ? NULL : strstr(inspect_frame, "} r_async_frame_00000001;");
        const char *runtime_str_in_frame =
            inspect_frame == NULL ? NULL : strstr(inspect_frame, "RRuntimeStringView ");
        const char *slice_in_frame =
            inspect_frame == NULL ? NULL : strstr(inspect_frame, "r_d00000001 ");
        const char *borrow_in_frame =
            inspect_frame == NULL ? NULL : strstr(inspect_frame, "r_d00000002 ");

        R_CODEGEN_CHECK(
            strstr(generated.bytes,
                   "destination->r_p00000000 = *(const r_a00000001 *)context->r_p00000000;") !=
            NULL);
        R_CODEGEN_CHECK(
            strstr(generated.bytes,
                   "destination->r_p00000001 = *(const r_a00000002 *)context->r_p00000001;") !=
            NULL);
        R_CODEGEN_CHECK(
            strstr(generated.bytes, "destination->r_p00000000 = source->r_v00000006;") != NULL);
        R_CODEGEN_CHECK(
            strstr(generated.bytes, "destination->r_p00000001 = source->r_v00000007;") != NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes, "RRuntimeStringView r_stack_r_l00000000 = {0};") !=
                        NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes, "r_d00000001 r_stack_r_l00000001 = {0};") != NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes, "r_d00000002 r_stack_r_l00000004 = {0};") != NULL);
        R_CODEGEN_CHECK(inspect_frame != NULL);
        R_CODEGEN_CHECK(inspect_frame_end != NULL);
        R_CODEGEN_CHECK(
            (runtime_str_in_frame == NULL) ||
            ((inspect_frame_end != NULL) && (runtime_str_in_frame > inspect_frame_end)));
        R_CODEGEN_CHECK((slice_in_frame == NULL) ||
                        ((inspect_frame_end != NULL) && (slice_in_frame > inspect_frame_end)));
        R_CODEGEN_CHECK((borrow_in_frame == NULL) ||
                        ((inspect_frame_end != NULL) && (borrow_in_frame > inspect_frame_end)));
    }
    R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &repeated) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(r_codegen_buffers_equal(&generated, &repeated));
    r_codegen_dispose_buffer(&generated);
    r_codegen_dispose_buffer(&repeated);
    r_frontend_destroy(context);
}

static void r_codegen_test_async_move_arguments(void) {
    RFrontendContext *context = r_codegen_create_context(NULL);
    RCodegenBuffer generated = {0};
    RCodegenBuffer repeated = {0};
    size_t staged_move_count = 0U;
    size_t await_block_count = 0U;
    size_t index;

    R_CODEGEN_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_CODEGEN_CHECK(r_codegen_add_file(context, R_CODEGEN_ASYNC_MOVE_ARGS_PATH));
    R_CODEGEN_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(r_frontend_diagnostic_count(context) == 0U);
    R_CODEGEN_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
    for (index = 0U; index < context->mir_instruction_count; ++index) {
        const RMirInstruction *instruction = &context->mir_instructions[index];

        if ((instruction->kind == R_MIR_INSTRUCTION_MOVE) && instruction->is_async_staged_move) {
            staged_move_count += 1U;
        }
    }
    R_CODEGEN_CHECK(staged_move_count == 4U);
    for (index = 0U; index < context->mir_block_count; ++index) {
        const RMirBlock *block = &context->mir_blocks[index];
        uint32_t instruction_index;

        for (instruction_index = 0U; instruction_index < block->instruction_count;
             ++instruction_index) {
            const RMirInstruction *instruction =
                &context->mir_instructions[(size_t)block->first_instruction + instruction_index];

            if (instruction->kind == R_MIR_INSTRUCTION_AWAIT) {
                await_block_count += 1U;
                R_CODEGEN_CHECK(instruction_index == 0U);
                R_CODEGEN_CHECK(block->instruction_count == UINT32_C(1));
            }
        }
    }
    R_CODEGEN_CHECK(await_block_count != 0U);
    R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &generated) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(generated.bytes != NULL);
    if (generated.bytes != NULL) {
        r_codegen_check_stack_gate(generated.bytes, "r_async_wrapper_initialize_00000004");
        r_codegen_check_stack_gate(generated.bytes, "r_async_call_initialize_00000004_00000001");
        R_CODEGEN_CHECK(strstr(generated.bytes,
                               "r_async_launch_00000001_gate("
                               "\n                &frame->r_v00000001, "
                               "r_async_call_initialize_00000004_00000001_gate, frame, 0);") !=
                        NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes,
                               "r_async_launch_00000004_gate(\n        r_effect_out, "
                               "r_async_wrapper_initialize_00000004_gate, &context, 0);") != NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes, "#include \"r_runtime_array.h\"") != NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes, "#include \"r_std_fs.h\"") != NULL);
        R_CODEGEN_CHECK(
            strstr(generated.bytes, "void r_f00000001(r_d00000005 *, RRuntimeArray *);") != NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes,
                               "void r_f00000002(r_d00000003 *, RStdFsPath *, RStdFsPath *);") !=
                        NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes,
                               "r_type_move_array_gate(&destination->r_p00000000, "
                               "context->r_p00000000);") != NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes,
                               "r_type_move_array_gate(&destination->r_p00000000, "
                               "&source->r_p00000000);") != NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes,
                               "r_type_move_std_fs_path_gate(&destination->r_p00000001, "
                               "&source->r_p00000001);") != NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes, "source->r_p00000000_initialized = 0;") != NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes, "source->r_p00000001_initialized = 0;") != NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes,
                               "r_type_move_array_gate(&frame->r_v00000000, "
                               "&frame->r_p00000000);") == NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes,
                               "r_type_move_std_fs_path_gate(&frame->r_v00000000, "
                               "&frame->r_p00000000);") == NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes, "r_staged_") == NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes, "if (frame->r_v00000016_initialized) {") != NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes,
                               "sizeof(r_d00000002),\n        _Alignof(r_d00000002),\n        "
                               "r_type_move_d00000002_gate,\n        "
                               "r_type_drop_d00000002_gate,") != NULL);
        R_CODEGEN_CHECK(
            strstr(generated.bytes, "r_type_move_array_gate(&completion->r_payload.r_ok,") != NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes, "completion->r_payload.r_error_00000001 =") !=
                        NULL);
    }
    R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &repeated) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(r_codegen_buffers_equal(&generated, &repeated));
    r_codegen_dispose_buffer(&generated);
    r_codegen_dispose_buffer(&repeated);
    r_frontend_destroy(context);
}

static bool r_codegen_has_diagnostic_code(const RFrontendContext *context, const char *code) {
    size_t index;

    for (index = 0U; index < r_frontend_diagnostic_count(context); ++index) {
        const RDiagnostic *diagnostic = r_frontend_diagnostic(context, index);

        if ((diagnostic != NULL) && (strcmp(diagnostic->code, code) == 0)) {
            return true;
        }
    }
    return false;
}

static bool r_codegen_has_diagnostic_rule(const RFrontendContext *context,
                                          const char *code,
                                          const char *rule_id) {
    size_t index;

    for (index = 0U; index < r_frontend_diagnostic_count(context); ++index) {
        const RDiagnostic *diagnostic = r_frontend_diagnostic(context, index);

        if ((diagnostic != NULL) && (strcmp(diagnostic->code, code) == 0) &&
            (strcmp(diagnostic->rule_id, rule_id) == 0)) {
            return true;
        }
    }
    return false;
}

static void r_codegen_test_async_copy_aggregate_diagnostics(void) {
    static const struct {
        const char *source;
        bool fails_during_mir;
        const char *rule_id;
    } cases[] = {
        {"module codegen.async_copy_borrow_capture; "
         "protected async void reject(str input) { return; }",
         false,
         "R-BORROW-0024"},
        {"module codegen.async_copy_non_send; "
         "struct RawCapture { raw const i32* pointer; }; "
         "protected async void reject(RawCapture input) { return; }",
         false,
         "R-FUNC-0011"},
        {"module codegen.async_copy_runtime_str_live; "
         "protected async i32 child() { return 0; } "
         "async i32 main() { constexpr str text = \"abc\"; str view = text; "
         "try { task<i32> operation = child(); i32 value = await move operation; "
         "usize length = len(view); return value; "
         "} catch (std.async::start_error error) { error as void; return 1; } }",
         true,
         "R-BORROW-0024"},
        {"module codegen.async_copy_slice_live; "
         "protected async i32 child() { return 0; } "
         "async i32 main() { i32[1] storage = {7}; const i32[] view = &storage; "
         "try { task<i32> operation = child(); i32 value = await move operation; "
         "usize length = len(view); return value; "
         "} catch (std.async::start_error error) { error as void; return 1; } }",
         true,
         "R-BORROW-0024"},
        {"module codegen.async_copy_borrow_live; "
         "protected async i32 child() { return 0; } "
         "async i32 main() { i32 marker = 7; const i32* view = &marker; "
         "try { task<i32> operation = child(); i32 value = await move operation; "
         "i32 observed = *view; return value; "
         "} catch (std.async::start_error error) { error as void; return 1; } }",
         true,
         "R-BORROW-0024"},
    };
    size_t index;

    for (index = 0U; index < (sizeof(cases) / sizeof(cases[0])); ++index) {
        RFrontendContext *context = r_codegen_create_context(NULL);

        R_CODEGEN_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        R_CODEGEN_CHECK(
            r_codegen_add_text(context, "async-copy-aggregate-negative.r", cases[index].source));
        if (cases[index].fails_during_mir) {
            R_CODEGEN_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
            R_CODEGEN_CHECK(r_frontend_diagnostic_count(context) == 0U);
            R_CODEGEN_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_NOT_LOWERABLE);
        } else {
            R_CODEGEN_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
        }
        if (!r_codegen_has_diagnostic_rule(context, "R-DIAG-ASYNC-001", cases[index].rule_id)) {
            size_t diagnostic_index;

            (void)fprintf(stderr, "async Copy aggregate diagnostic case %zu:\n", index);
            for (diagnostic_index = 0U; diagnostic_index < r_frontend_diagnostic_count(context);
                 ++diagnostic_index) {
                const RDiagnostic *diagnostic = r_frontend_diagnostic(context, diagnostic_index);

                if (diagnostic != NULL) {
                    (void)fprintf(stderr,
                                  "  %s [%s]: %s\n",
                                  diagnostic->code,
                                  diagnostic->rule_id,
                                  diagnostic->message);
                }
            }
        }
        R_CODEGEN_CHECK(
            r_codegen_has_diagnostic_rule(context, "R-DIAG-ASYNC-001", cases[index].rule_id));
        r_frontend_destroy(context);
    }
}

static void r_codegen_test_async_move_diagnostics(void) {
    static const struct {
        const char *source;
        const char *code;
    } cases[] = {
        {"module codegen.async_move_nested; "
         "protected async void consume(array<u8> values) { return; } "
         "protected async void launch(array<u8> values) throws std.async::start_error { "
         "task<void> operation = consume(move move values); await move operation; }",
         "R-DIAG-MOVE-003"},
        {"module codegen.async_move_overlap; "
         "protected async void consume(array<u8> first, array<u8> second) { return; } "
         "protected async void launch(array<u8> values) throws std.async::start_error { "
         "task<void> operation = consume(move values, move values); await move operation; }",
         "R-DIAG-BORROW-001"},
    };
    size_t index;

    for (index = 0U; index < (sizeof(cases) / sizeof(cases[0])); ++index) {
        RFrontendContext *context = r_codegen_create_context(NULL);
        RSourceId source_id = R_SOURCE_ID_INVALID;

        R_CODEGEN_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        R_CODEGEN_CHECK(r_frontend_add_source(context,
                                              "async-move-negative.r",
                                              (const uint8_t *)cases[index].source,
                                              strlen(cases[index].source),
                                              &source_id) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
        if (!r_codegen_has_diagnostic_code(context, cases[index].code)) {
            size_t diagnostic_index;

            (void)fprintf(stderr, "async Move diagnostic case %zu:\n", index);
            for (diagnostic_index = 0U; diagnostic_index < r_frontend_diagnostic_count(context);
                 ++diagnostic_index) {
                const RDiagnostic *diagnostic = r_frontend_diagnostic(context, diagnostic_index);

                if (diagnostic != NULL) {
                    (void)fprintf(stderr, "  %s: %s\n", diagnostic->code, diagnostic->message);
                }
            }
        }
        R_CODEGEN_CHECK(r_codegen_has_diagnostic_code(context, cases[index].code));
        r_frontend_destroy(context);
    }
}

static void r_codegen_test_core_assume(void) {
    static const char *const paths[] = {
        R_CODEGEN_CORE_ASSUME_PATH,
        R_CODEGEN_ASYNC_CORE_ASSUME_PATH,
    };
    size_t index;

    for (index = 0U; index < (sizeof(paths) / sizeof(paths[0])); ++index) {
        RFrontendContext *context = r_codegen_create_context(NULL);
        RCodegenBuffer generated = {0};
        RCodegenBuffer repeated = {0};
        const char *assume;

        R_CODEGEN_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        R_CODEGEN_CHECK(r_codegen_add_file(context, paths[index]));
        R_CODEGEN_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &generated) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(generated.bytes != NULL);
        assume = generated.bytes == NULL ? NULL : strstr(generated.bytes, "((void (*)(void))0)();");
        R_CODEGEN_CHECK(assume != NULL);
        R_CODEGEN_CHECK((assume == NULL) || (strstr(assume + strlen("((void (*)(void))0)();"),
                                                    "((void (*)(void))0)();") == NULL));
        R_CODEGEN_CHECK((generated.bytes != NULL) &&
                        (strstr(generated.bytes, "r_core_assume") == NULL));
        R_CODEGEN_CHECK((generated.bytes != NULL) &&
                        ((strstr(generated.bytes, "r_async_frame_") != NULL) == (index == 1U)));
        R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &repeated) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_codegen_buffers_equal(&generated, &repeated));
        r_codegen_dispose_buffer(&generated);
        r_codegen_dispose_buffer(&repeated);
        r_frontend_destroy(context);
    }
}

static void r_codegen_test_core_volatile(void) {
    static const char *const paths[] = {
        R_CODEGEN_CORE_VOLATILE_PATH,
        R_CODEGEN_ASYNC_CORE_VOLATILE_PATH,
    };
    static const char store_access[] = "*(volatile int32_t *)";
    static const char load_access[] = "*(const volatile int32_t *)";
    size_t index;

    for (index = 0U; index < (sizeof(paths) / sizeof(paths[0])); ++index) {
        RFrontendContext *context = r_codegen_create_context(NULL);
        RCodegenBuffer generated = {0};
        RCodegenBuffer repeated = {0};
        const char *store;
        const char *load;

        R_CODEGEN_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        R_CODEGEN_CHECK(r_codegen_add_file(context, paths[index]));
        R_CODEGEN_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &generated) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(generated.bytes != NULL);
        store = generated.bytes == NULL ? NULL : strstr(generated.bytes, store_access);
        load = generated.bytes == NULL ? NULL : strstr(generated.bytes, load_access);
        R_CODEGEN_CHECK(store != NULL);
        R_CODEGEN_CHECK(load != NULL);
        R_CODEGEN_CHECK((store == NULL) ||
                        (strstr(store + strlen(store_access), store_access) == NULL));
        R_CODEGEN_CHECK((load == NULL) ||
                        (strstr(load + strlen(load_access), load_access) == NULL));
        R_CODEGEN_CHECK((generated.bytes != NULL) &&
                        (strstr(generated.bytes, "r_core_volatile") == NULL));
        R_CODEGEN_CHECK((generated.bytes != NULL) &&
                        ((strstr(generated.bytes, "r_async_frame_") != NULL) == (index == 1U)));
        R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &repeated) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_codegen_buffers_equal(&generated, &repeated));
        r_codegen_dispose_buffer(&generated);
        r_codegen_dispose_buffer(&repeated);
        r_frontend_destroy(context);
    }
}

static void r_codegen_test_core_slice_from_raw_parts(void) {
    static const char *const paths[] = {
        R_CODEGEN_CORE_SLICE_FROM_RAW_PARTS_PATH,
        R_CODEGEN_ASYNC_CORE_SLICE_FROM_RAW_PARTS_PATH,
    };
    size_t index;

    for (index = 0U; index < (sizeof(paths) / sizeof(paths[0])); ++index) {
        RFrontendContext *context = r_codegen_create_context(NULL);
        RCodegenBuffer generated = {0};
        RCodegenBuffer repeated = {0};

        R_CODEGEN_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        R_CODEGEN_CHECK(r_codegen_add_file(context, paths[index]));
        R_CODEGEN_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &generated) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(generated.bytes != NULL);
        R_CODEGEN_CHECK((generated.bytes != NULL) &&
                        (strstr(generated.bytes, "int32_t *r_data;") != NULL));
        R_CODEGEN_CHECK((generated.bytes != NULL) &&
                        (strstr(generated.bytes, "const int32_t *r_data;") != NULL));
        R_CODEGEN_CHECK((generated.bytes != NULL) &&
                        (strstr(generated.bytes, ".r_data = ") != NULL));
        R_CODEGEN_CHECK((generated.bytes != NULL) &&
                        (strstr(generated.bytes, ".r_len = (size_t)") != NULL));
        R_CODEGEN_CHECK((generated.bytes != NULL) &&
                        (strstr(generated.bytes, "r_core_slice_from_raw_parts") == NULL));
        R_CODEGEN_CHECK((generated.bytes != NULL) &&
                        ((strstr(generated.bytes, "r_async_frame_") != NULL) == (index == 1U)));
        R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &repeated) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_codegen_buffers_equal(&generated, &repeated));
        r_codegen_dispose_buffer(&generated);
        r_codegen_dispose_buffer(&repeated);
        r_frontend_destroy(context);
    }
}

/* L38: an anchored raw-parts slice emits the same descriptor as the unanchored one; the anchor
   is never evaluated and names no runtime symbol. */
static void r_codegen_test_core_slice_from_raw_parts_in(void) {
    static const char *const paths[] = {
        R_CODEGEN_CORE_SLICE_FROM_RAW_PARTS_IN_PATH,
        R_CODEGEN_ASYNC_CORE_SLICE_FROM_RAW_PARTS_IN_PATH,
    };
    size_t index;

    for (index = 0U; index < (sizeof(paths) / sizeof(paths[0])); ++index) {
        RFrontendContext *context = r_codegen_create_context(NULL);
        RCodegenBuffer generated = {0};
        RCodegenBuffer repeated = {0};

        R_CODEGEN_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        R_CODEGEN_CHECK(r_codegen_add_file(context, paths[index]));
        R_CODEGEN_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &generated) == R_FRONTEND_OK);
        R_CODEGEN_CHECK((generated.bytes != NULL) &&
                        (strstr(generated.bytes, "const uint8_t *r_data;") != NULL));
        R_CODEGEN_CHECK((generated.bytes != NULL) &&
                        (strstr(generated.bytes, ".r_data = ") != NULL));
        R_CODEGEN_CHECK((generated.bytes != NULL) &&
                        (strstr(generated.bytes, ".r_len = (size_t)") != NULL));
        R_CODEGEN_CHECK((generated.bytes != NULL) &&
                        (strstr(generated.bytes, "r_core_slice_from_raw_parts") == NULL));
        R_CODEGEN_CHECK((generated.bytes != NULL) &&
                        ((strstr(generated.bytes, "r_async_frame_") != NULL) == (index == 1U)));
        R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &repeated) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_codegen_buffers_equal(&generated, &repeated));
        r_codegen_dispose_buffer(&generated);
        r_codegen_dispose_buffer(&repeated);
        r_frontend_destroy(context);
    }
}

static void r_codegen_test_core_adopt_release(void) {
    static const char *const paths[] = {
        R_CODEGEN_CORE_ADOPT_RELEASE_PATH,
        R_CODEGEN_ASYNC_CORE_ADOPT_RELEASE_PATH,
    };
    size_t index;

    for (index = 0U; index < (sizeof(paths) / sizeof(paths[0])); ++index) {
        RFrontendContext *context = r_codegen_create_context(NULL);
        RCodegenBuffer generated = {0};
        RCodegenBuffer repeated = {0};
        const char *adopt;
        const char *release;

        R_CODEGEN_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        R_CODEGEN_CHECK(r_codegen_add_file(context, paths[index]));
        R_CODEGEN_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &generated) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(generated.bytes != NULL);
        adopt = generated.bytes == NULL ? NULL : strstr(generated.bytes, "r_runtime_own_adopt(");
        release =
            generated.bytes == NULL ? NULL : strstr(generated.bytes, "r_runtime_own_into_raw(");
        R_CODEGEN_CHECK(adopt != NULL);
        R_CODEGEN_CHECK((adopt != NULL) && (strstr(adopt + strlen("r_runtime_own_adopt("),
                                                   "r_runtime_own_adopt(") != NULL));
        R_CODEGEN_CHECK(release != NULL);
        R_CODEGEN_CHECK((release == NULL) || (strstr(release + strlen("r_runtime_own_into_raw("),
                                                     "r_runtime_own_into_raw(") == NULL));
        R_CODEGEN_CHECK((generated.bytes != NULL) &&
                        (strstr(generated.bytes, "#include \"r_runtime_own.h\"") != NULL));
        R_CODEGEN_CHECK((generated.bytes != NULL) &&
                        (strstr(generated.bytes, ".size = sizeof(int32_t)") != NULL));
        R_CODEGEN_CHECK((generated.bytes != NULL) &&
                        (strstr(generated.bytes, ".alignment = _Alignof(int32_t)") != NULL));
        R_CODEGEN_CHECK((generated.bytes != NULL) &&
                        (strstr(generated.bytes, ".move_initialize = NULL") != NULL));
        R_CODEGEN_CHECK((generated.bytes != NULL) &&
                        (strstr(generated.bytes, ".drop = NULL") != NULL));
        R_CODEGEN_CHECK(
            (generated.bytes != NULL) &&
            (strstr(generated.bytes, "static inline void r_type_drop_own(void *value_pointer)") !=
             NULL) &&
            (strstr(generated.bytes, "r_runtime_own_release(value);") != NULL));
        R_CODEGEN_CHECK((generated.bytes != NULL) &&
                        (strstr(generated.bytes, "r_runtime_allocator_allocate(") == NULL));
        R_CODEGEN_CHECK((generated.bytes != NULL) &&
                        (strstr(generated.bytes, "RRuntimeOwnStatus") == NULL));
        R_CODEGEN_CHECK((generated.bytes != NULL) &&
                        ((strstr(generated.bytes, "r_async_frame_") != NULL) == (index == 1U)));
        if (generated.bytes != NULL) {
            R_CODEGEN_CHECK(index == 0U ? (strstr(generated.bytes, " = (RRuntimeOwn){0};") != NULL)
                                        : (strstr(generated.bytes, "_initialized = 0;") != NULL));
        }
        R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &repeated) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_codegen_buffers_equal(&generated, &repeated));
        r_codegen_dispose_buffer(&generated);
        r_codegen_dispose_buffer(&repeated);
        r_frontend_destroy(context);
    }
    {
        RFrontendContext *context = r_codegen_create_context(NULL);
        RCodegenBuffer generated = {0};
        RCodegenBuffer repeated = {0};
        const char *drop;
        const char *drop_second;
        const char *drop_first;

        R_CODEGEN_CHECK(context != NULL);
        if (context != NULL) {
            R_CODEGEN_CHECK(r_codegen_add_file(context, R_CODEGEN_CORE_ADOPT_NONTRIVIAL_PATH));
            R_CODEGEN_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
            R_CODEGEN_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
            R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &generated) ==
                            R_FRONTEND_OK);
            R_CODEGEN_CHECK(generated.bytes != NULL);
            R_CODEGEN_CHECK((generated.bytes != NULL) &&
                            (strstr(generated.bytes,
                                    ".move_initialize = r_type_move_a00000001_gate,") != NULL));
            R_CODEGEN_CHECK(
                (generated.bytes != NULL) &&
                (strstr(generated.bytes, ".drop = r_type_drop_a00000001_gate,") != NULL));
            r_codegen_check_glue_gate(generated.bytes, "r_type_move_a00000001");
            r_codegen_check_glue_gate(generated.bytes, "r_type_drop_a00000001");
            r_codegen_check_glue_gate(generated.bytes, "r_type_drop_own");
            R_CODEGEN_CHECK((generated.bytes != NULL) &&
                            (strstr(generated.bytes, "r_runtime_own_into_raw(") != NULL));
            R_CODEGEN_CHECK((generated.bytes != NULL) &&
                            (strstr(generated.bytes, "r_runtime_allocator_allocate(") == NULL));
            drop = generated.bytes == NULL
                       ? NULL
                       : strstr(generated.bytes,
                                "static inline void r_type_drop_a00000001(void *value_pointer) {");
            drop_second =
                drop == NULL ? NULL : strstr(drop, "r_type_drop_own_gate(&value->r_m00000002);");
            drop_first =
                drop == NULL ? NULL : strstr(drop, "r_type_drop_own_gate(&value->r_m00000001);");
            R_CODEGEN_CHECK((drop_second != NULL) && (drop_first != NULL) &&
                            (drop_second < drop_first));
            R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &repeated) ==
                            R_FRONTEND_OK);
            R_CODEGEN_CHECK(r_codegen_buffers_equal(&generated, &repeated));
            r_codegen_dispose_buffer(&generated);
            r_codegen_dispose_buffer(&repeated);
            r_frontend_destroy(context);
        }
    }
    {
        static const char *const glue_paths[] = {
            R_CODEGEN_CORE_ADOPT_TYPE_GLUE_PATH,
            R_CODEGEN_ASYNC_CORE_ADOPT_NONTRIVIAL_PATH,
        };
        size_t path_index;

        for (path_index = 0U; path_index < (sizeof(glue_paths) / sizeof(glue_paths[0]));
             ++path_index) {
            RFrontendContext *context = r_codegen_create_context(NULL);
            RCodegenBuffer generated = {0};
            RCodegenBuffer repeated = {0};

            R_CODEGEN_CHECK(context != NULL);
            if (context == NULL) {
                continue;
            }
            R_CODEGEN_CHECK(r_codegen_add_file(context, glue_paths[path_index]));
            R_CODEGEN_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
            R_CODEGEN_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
            R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &generated) ==
                            R_FRONTEND_OK);
            R_CODEGEN_CHECK(generated.bytes != NULL);
            R_CODEGEN_CHECK((generated.bytes != NULL) &&
                            (strstr(generated.bytes,
                                    ".move_initialize = r_type_move_a00000001_gate,") != NULL));
            R_CODEGEN_CHECK(
                (generated.bytes != NULL) &&
                (strstr(generated.bytes, ".drop = r_type_drop_a00000001_gate,") != NULL));
            r_codegen_check_glue_gate(generated.bytes, "r_type_move_a00000001");
            r_codegen_check_glue_gate(generated.bytes, "r_type_drop_a00000001");
            if (path_index == 0U) {
                R_CODEGEN_CHECK((generated.bytes != NULL) &&
                                (strstr(generated.bytes, "r_type_move_task") != NULL));
                R_CODEGEN_CHECK(
                    (generated.bytes != NULL) &&
                    (strstr(generated.bytes, "r_runtime_task_destroy(value);") != NULL));
                R_CODEGEN_CHECK(
                    (generated.bytes != NULL) &&
                    (strstr(generated.bytes, "size_t index = (size_t)UINT64_C(2);") != NULL));
                R_CODEGEN_CHECK((generated.bytes != NULL) &&
                                (r_codegen_count_occurrences(
                                     generated.bytes, "if (value->r_tag == UINT32_C(1))") == 2U));
                R_CODEGEN_CHECK(
                    (generated.bytes != NULL) &&
                    (strstr(generated.bytes, "if (value->r_tag == UINT32_C(0))") == NULL));
            } else {
                R_CODEGEN_CHECK((generated.bytes != NULL) &&
                                (strstr(generated.bytes, "r_async_frame_drop_00000001") != NULL));
                R_CODEGEN_CHECK((generated.bytes != NULL) &&
                                (strstr(generated.bytes,
                                        "r_type_drop_own_gate(&frame->r_l00000002);") != NULL));
                R_CODEGEN_CHECK(
                    (generated.bytes != NULL) &&
                    (strstr(generated.bytes, "frame->r_l00000002_initialized = 0;") != NULL));
            }
            R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &repeated) ==
                            R_FRONTEND_OK);
            R_CODEGEN_CHECK(r_codegen_buffers_equal(&generated, &repeated));
            r_codegen_dispose_buffer(&generated);
            r_codegen_dispose_buffer(&repeated);
            r_frontend_destroy(context);
        }
    }
    {
        RFrontendContext *context = r_codegen_create_context(NULL);
        RCodegenBuffer generated = {0};
        RCodegenBuffer repeated = {0};
        const char *drop;
        const char *drop_atomic;
        const char *drop_dict;
        const char *drop_array;

        R_CODEGEN_CHECK(context != NULL);
        if (context != NULL) {
            R_CODEGEN_CHECK(r_codegen_add_file(context, R_CODEGEN_CORE_ADOPT_RUNTIME_VALUES_PATH));
            R_CODEGEN_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
            R_CODEGEN_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
            R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &generated) ==
                            R_FRONTEND_OK);
            R_CODEGEN_CHECK(generated.bytes != NULL);
            R_CODEGEN_CHECK((generated.bytes != NULL) &&
                            (strstr(generated.bytes, "#include \"r_runtime_array.h\"") != NULL));
            R_CODEGEN_CHECK((generated.bytes != NULL) &&
                            (strstr(generated.bytes, "#include \"r_runtime_list.h\"") != NULL));
            R_CODEGEN_CHECK((generated.bytes != NULL) &&
                            (strstr(generated.bytes, "#include \"r_runtime_dict.h\"") != NULL));
            R_CODEGEN_CHECK((generated.bytes != NULL) &&
                            (strstr(generated.bytes, "#include \"r_runtime_arc.h\"") != NULL));
            R_CODEGEN_CHECK((generated.bytes != NULL) &&
                            (strstr(generated.bytes, "#include \"r_runtime_rc.h\"") != NULL));
            R_CODEGEN_CHECK((generated.bytes != NULL) &&
                            (strstr(generated.bytes, "#include <stdatomic.h>") != NULL));
            R_CODEGEN_CHECK((generated.bytes != NULL) &&
                            (strstr(generated.bytes, "r_runtime_array_destroy(value);") != NULL));
            R_CODEGEN_CHECK((generated.bytes != NULL) &&
                            (strstr(generated.bytes, "r_runtime_list_destroy(value);") != NULL));
            R_CODEGEN_CHECK((generated.bytes != NULL) &&
                            (strstr(generated.bytes, "r_runtime_dict_destroy(value);") != NULL));
            R_CODEGEN_CHECK((generated.bytes != NULL) &&
                            (strstr(generated.bytes, "r_runtime_arc_release(value);") != NULL));
            R_CODEGEN_CHECK((generated.bytes != NULL) &&
                            (strstr(generated.bytes, "r_runtime_rc_release(value);") != NULL));
            R_CODEGEN_CHECK(
                (generated.bytes != NULL) &&
                (strstr(generated.bytes, "r_runtime_weak_arc_release(value);") != NULL));
            R_CODEGEN_CHECK((generated.bytes != NULL) &&
                            (strstr(generated.bytes, "r_runtime_weak_rc_release(value);") != NULL));
            R_CODEGEN_CHECK((generated.bytes != NULL) &&
                            (strstr(generated.bytes,
                                    "atomic_load_explicit(source, memory_order_relaxed)") != NULL));
            R_CODEGEN_CHECK((generated.bytes != NULL) &&
                            (strstr(generated.bytes, "atomic_init(destination,") != NULL));
            R_CODEGEN_CHECK((generated.bytes != NULL) &&
                            (strstr(generated.bytes,
                                    ".move_initialize = r_type_move_a00000001_gate,") != NULL));
            R_CODEGEN_CHECK(
                (generated.bytes != NULL) &&
                (strstr(generated.bytes, ".drop = r_type_drop_a00000001_gate,") != NULL));
            r_codegen_check_glue_gate(generated.bytes, "r_type_move_a00000001");
            r_codegen_check_glue_gate(generated.bytes, "r_type_drop_a00000001");
            drop = generated.bytes == NULL
                       ? NULL
                       : strstr(generated.bytes,
                                "static inline void r_type_drop_a00000001(void *value_pointer) {");
            drop_atomic = drop == NULL ? NULL : strstr(drop, "r_type_drop_atomic_");
            drop_dict = drop == NULL ? NULL : strstr(drop, "r_type_drop_dict_gate(");
            drop_array = drop == NULL ? NULL : strstr(drop, "r_type_drop_array_gate(");
            R_CODEGEN_CHECK((drop_atomic != NULL) && (drop_dict != NULL) && (drop_array != NULL) &&
                            (drop_atomic < drop_dict) && (drop_dict < drop_array));
            R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &repeated) ==
                            R_FRONTEND_OK);
            R_CODEGEN_CHECK(r_codegen_buffers_equal(&generated, &repeated));
            r_codegen_dispose_buffer(&generated);
            r_codegen_dispose_buffer(&repeated);
            r_frontend_destroy(context);
        }
    }
}

/* R-BORROW-0018: an entry reference or container iterator holds its container's region, so no
   owner holds one and the adopt fixtures leave these records out. */
static bool r_codegen_standard_type_holds_region(const char *name) {
    return (strcmp(name, "std.dict::entry_ref") == 0) || (strcmp(name, "std.dict::iter") == 0) ||
           (strcmp(name, "std.list::iter") == 0);
}

static void r_codegen_test_core_adopt_named_standard_copy(void) {
    RFrontendContext *context = r_codegen_create_context(NULL);
    RCodegenBuffer generated = {0};
    RCodegenBuffer repeated = {0};
    const size_t record_count = r_named_standard_copy_abi_count();
    const size_t header_count = r_named_standard_copy_abi_header_count();
    size_t adopted_count = 0U;
    uint64_t adopted_headers = 0U;
    size_t index;

    R_CODEGEN_CHECK(record_count == 91U);
    R_CODEGEN_CHECK(header_count == 23U);
    for (index = 0U; index < record_count; ++index) {
        const RNamedStandardCopyAbi *record = r_named_standard_copy_abi_at(index);
        if ((record != NULL) && !r_codegen_standard_type_holds_region(record->r_name)) {
            adopted_count += 1U;
            adopted_headers |= UINT64_C(1) << record->header_index;
        }
    }
    R_CODEGEN_CHECK(adopted_count == 90U);
    R_CODEGEN_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_CODEGEN_CHECK(r_codegen_add_file(context, R_CODEGEN_CORE_ADOPT_NAMED_STANDARD_COPY_PATH));
    R_CODEGEN_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(r_frontend_diagnostic_count(context) == 0U);
    R_CODEGEN_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &generated) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(generated.bytes != NULL);
    R_CODEGEN_CHECK(r_codegen_count_occurrences(generated.bytes, "r_runtime_own_adopt(") ==
                    adopted_count);
    R_CODEGEN_CHECK(r_codegen_count_occurrences(generated.bytes, "r_runtime_own_into_raw(") ==
                    adopted_count);
    for (index = 0U; index < record_count; ++index) {
        const RNamedStandardCopyAbi *record = r_named_standard_copy_abi_at(index);
        char size_pattern[128];
        char alignment_pattern[128];

        R_CODEGEN_CHECK(record != NULL);
        if ((record == NULL) || r_codegen_standard_type_holds_region(record->r_name)) {
            continue;
        }
        R_CODEGEN_CHECK(r_named_standard_copy_abi_find(record->r_name, record->r_name_length) ==
                        record);
        R_CODEGEN_CHECK(
            record->generic_arity ==
            (strcmp(record->r_name, "std.dict::entry_ref") == 0 ? UINT32_C(2) : UINT32_C(0)));
        R_CODEGEN_CHECK((index == 0U) || (strcmp(r_named_standard_copy_abi_at(index - 1U)->r_name,
                                                 record->r_name) < 0));
        R_CODEGEN_CHECK(
            snprintf(size_pattern, sizeof(size_pattern), ".size = sizeof(%s)", record->c_type) > 0);
        R_CODEGEN_CHECK(snprintf(alignment_pattern,
                                 sizeof(alignment_pattern),
                                 ".alignment = _Alignof(%s)",
                                 record->c_type) > 0);
        R_CODEGEN_CHECK((generated.bytes != NULL) &&
                        (strstr(generated.bytes, size_pattern) != NULL));
        R_CODEGEN_CHECK((generated.bytes != NULL) &&
                        (strstr(generated.bytes, alignment_pattern) != NULL));
    }
    for (index = 0U; index < header_count; ++index) {
        const char *header = r_named_standard_copy_abi_header_at(index);
        char include_pattern[128];

        R_CODEGEN_CHECK(header != NULL);
        if ((header == NULL) || ((adopted_headers & (UINT64_C(1) << index)) == 0U)) {
            continue;
        }
        R_CODEGEN_CHECK(
            snprintf(include_pattern, sizeof(include_pattern), "#include \"%s\"", header) > 0);
        R_CODEGEN_CHECK(r_codegen_count_occurrences(generated.bytes, include_pattern) == 1U);
    }
    R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &repeated) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(r_codegen_buffers_equal(&generated, &repeated));
    r_codegen_dispose_buffer(&generated);
    r_codegen_dispose_buffer(&repeated);
    r_frontend_destroy(context);
}

static void r_codegen_test_core_adopt_named_standard_layout(void) {
    RFrontendContext *context = r_codegen_create_context(NULL);
    RCodegenBuffer generated = {0};
    RCodegenBuffer repeated = {0};
    const size_t record_count = r_named_standard_layout_abi_count();
    const size_t header_count = r_named_standard_layout_abi_header_count();
    size_t index;

    R_CODEGEN_CHECK(record_count == 2U);
    R_CODEGEN_CHECK(header_count == 2U);
    R_CODEGEN_CHECK(r_named_standard_layout_abi_at(record_count) == NULL);
    R_CODEGEN_CHECK(r_named_standard_layout_abi_header_at(header_count) == NULL);
    R_CODEGEN_CHECK(r_named_standard_layout_abi_find(NULL, 0U) == NULL);
    for (index = 0U; index < record_count; ++index) {
        const RNamedStandardLayoutAbi *record = r_named_standard_layout_abi_at(index);

        R_CODEGEN_CHECK(record != NULL);
        if (record == NULL) {
            continue;
        }
        R_CODEGEN_CHECK(r_named_standard_layout_abi_find(record->r_name, record->r_name_length) ==
                        record);
        R_CODEGEN_CHECK(record->record_index == (uint32_t)index);
        R_CODEGEN_CHECK((index == 0U) || (strcmp(r_named_standard_layout_abi_at(index - 1U)->r_name,
                                                 record->r_name) < 0));
        /* Both layout records are container iterators, which no owner may hold. */
        R_CODEGEN_CHECK(r_codegen_standard_type_holds_region(record->r_name));
    }
    R_CODEGEN_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_CODEGEN_CHECK(r_codegen_add_file(context, R_CODEGEN_CORE_ADOPT_NAMED_STANDARD_LAYOUT_PATH));
    R_CODEGEN_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(r_frontend_diagnostic_count(context) == 0U);
    R_CODEGEN_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &generated) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(generated.bytes != NULL);
    R_CODEGEN_CHECK(r_codegen_count_occurrences(generated.bytes, "r_runtime_own_adopt(") == 0U);
    R_CODEGEN_CHECK((generated.bytes != NULL) &&
                    (strstr(generated.bytes, "r_named_standard_layout_drop_") == NULL));
    R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &repeated) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(r_codegen_buffers_equal(&generated, &repeated));
    r_codegen_dispose_buffer(&generated);
    r_codegen_dispose_buffer(&repeated);
    r_frontend_destroy(context);
}

static void r_codegen_test_named_standard_move_abi(void) {
    RFrontendContext *context = r_codegen_create_context(NULL);
    RCodegenBuffer generated = {0};
    RCodegenBuffer repeated = {0};
    const size_t record_count = r_named_standard_move_abi_count();
    const size_t header_count = r_named_standard_move_abi_header_count();
    size_t index;

    R_CODEGEN_CHECK(record_count == 64U);
    R_CODEGEN_CHECK(header_count == 14U);
    R_CODEGEN_CHECK(r_named_standard_move_abi_at(record_count) == NULL);
    R_CODEGEN_CHECK(r_named_standard_move_abi_header_at(header_count) == NULL);
    R_CODEGEN_CHECK(r_named_standard_move_abi_find(NULL, 0U) == NULL);
    R_CODEGEN_CHECK(r_named_standard_move_abi_find("std.fs::missing", 15U) == NULL);
    R_CODEGEN_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_CODEGEN_CHECK(r_codegen_add_file(context, R_CODEGEN_NAMED_STANDARD_MOVE_ABI_PATH));
    R_CODEGEN_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(r_frontend_diagnostic_count(context) == 0U);
    R_CODEGEN_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &generated) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(generated.bytes != NULL);
    for (index = 0U; index < record_count; ++index) {
        const RNamedStandardMoveAbi *record = r_named_standard_move_abi_at(index);
        const bool emits_drop =
            (record != NULL) && ((strcmp(record->r_name, "std.fs::write_file_result") == 0) ||
                                 (strcmp(record->r_name, "std.io::read_result") == 0) ||
                                 (strcmp(record->r_name, "std.io::shared_write_result") == 0) ||
                                 (strcmp(record->r_name, "std.io::write_result") == 0) ||
                                 (strcmp(record->r_name, "std.io::write_all_result") == 0) ||
                                 (strcmp(record->r_name, "std.sync::once") == 0) ||
                                 (strcmp(record->r_name, "std.net::tcp_stream") == 0) ||
                                 (strncmp(record->r_name, "std.json::", 10U) == 0));

        R_CODEGEN_CHECK(record != NULL);
        if (record == NULL) {
            continue;
        }
        R_CODEGEN_CHECK(r_named_standard_move_abi_find(record->r_name, record->r_name_length) ==
                        record);
        R_CODEGEN_CHECK((index == 0U) || (strcmp(r_named_standard_move_abi_at(index - 1U)->r_name,
                                                 record->r_name) < 0));
        R_CODEGEN_CHECK((generated.bytes != NULL) &&
                        (strstr(generated.bytes, record->c_type) != NULL));
        R_CODEGEN_CHECK((generated.bytes != NULL) &&
                        (strstr(generated.bytes, record->move_initialize) != NULL));
        R_CODEGEN_CHECK((generated.bytes != NULL) &&
                        ((strstr(generated.bytes, record->drop) != NULL) == emits_drop));
    }
    for (index = 0U; index < header_count; ++index) {
        const char *header = r_named_standard_move_abi_header_at(index);
        char include_pattern[128];

        R_CODEGEN_CHECK(header != NULL);
        if (header == NULL) {
            continue;
        }
        R_CODEGEN_CHECK(
            snprintf(include_pattern, sizeof(include_pattern), "#include \"%s\"", header) > 0);
        R_CODEGEN_CHECK(r_codegen_count_occurrences(generated.bytes, include_pattern) == 1U);
    }
    R_CODEGEN_CHECK((generated.bytes != NULL) &&
                    (strstr(generated.bytes, "r_type_move_arc") != NULL));
    R_CODEGEN_CHECK((generated.bytes != NULL) &&
                    (strstr(generated.bytes, "r_type_drop_arc") != NULL));
    R_CODEGEN_CHECK((generated.bytes != NULL) &&
                    (strstr(generated.bytes, "r_async_frame_") != NULL));
    R_CODEGEN_CHECK(r_frontend_emit_c17(context, r_codegen_write, &repeated) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(r_codegen_buffers_equal(&generated, &repeated));
    r_codegen_dispose_buffer(&generated);
    r_codegen_dispose_buffer(&repeated);
    r_frontend_destroy(context);
}

int main(void) {
    r_codegen_test_golden();
    r_codegen_test_target_manifest_identity();
    r_codegen_test_determinism();
    r_codegen_test_format_determinism();
    r_codegen_test_constexpr_string_pool();
    r_codegen_test_literal_octets();
    r_codegen_test_failure_atomicity();
    r_codegen_test_value_types();
    r_codegen_test_array_slice();
    r_codegen_test_sync_aggregate_destination();
    r_codegen_test_async_option_path();
    r_codegen_test_rejection_gates();
    r_codegen_test_async_projected_store();
    r_codegen_test_implicit_final_return();
    r_codegen_test_async_state_machine();
    r_codegen_test_async_enum_constants();
    r_codegen_test_async_short_circuit_phi();
    r_codegen_test_replacement_preflight();
    r_codegen_test_async_short_circuit_phi_mutations();
    r_codegen_test_async_fused_default_own();
    r_codegen_test_async_fused_default_own_mutations();
    r_codegen_test_async_sync_calls();
    r_codegen_test_async_mir_slice();
    r_codegen_test_async_io_read();
    r_codegen_test_async_io_write_all();
    r_codegen_test_stdout();
    r_codegen_test_stdin();
    r_codegen_test_sync_main_arguments();
    r_codegen_test_async_main_arguments();
    r_codegen_test_async_copy_arguments();
    r_codegen_test_async_copy_aggregate_and_transient_storage();
    r_codegen_test_async_copy_aggregate_diagnostics();
    r_codegen_test_async_move_arguments();
    r_codegen_test_async_move_diagnostics();
    r_codegen_test_core_assume();
    r_codegen_test_core_slice_from_raw_parts();
    r_codegen_test_core_slice_from_raw_parts_in();
    r_codegen_test_core_volatile();
    r_codegen_test_core_adopt_release();
    r_codegen_test_core_adopt_named_standard_copy();
    r_codegen_test_core_adopt_named_standard_layout();
    r_codegen_test_named_standard_move_abi();
    if (failures != 0) {
        (void)fprintf(stderr, "codegen tests failed: %d\n", failures);
        return EXIT_FAILURE;
    }
    (void)puts("codegen tests passed");
    return EXIT_SUCCESS;
}

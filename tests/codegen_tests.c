#include "frontend_internal.h"
#include "named_standard_copy_abi.h"
#include "named_standard_move_abi.h"

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

/* The definition of one function of the IR, from its `define` line to its closing brace; `name`
   is the quoted or plain symbol with its `@`. */
static bool r_codegen_ir_function(const char *generated,
                                  const char *name,
                                  const char **begin,
                                  const char **end) {
    const size_t name_length = strlen(name);
    const char *cursor = generated;

    *begin = NULL;
    *end = NULL;
    while ((cursor != NULL) && ((cursor = strstr(cursor, "\ndefine ")) != NULL)) {
        const char *line_end = strchr(cursor + 1, '\n');
        const char *symbol = strstr(cursor + 1, name);

        if ((line_end != NULL) && (symbol != NULL) && (symbol < line_end) &&
            (symbol[name_length] == '(')) {
            *begin = cursor + 1;
            *end = strstr(cursor + 1, "\n}\n");
            return *end != NULL;
        }
        cursor += 1;
    }
    return false;
}

/* The calls of a runtime or library function; a static inline function of the C headers is
   called through its `r_shim_` wrapper. */
static size_t r_codegen_count_calls(const char *generated, const char *function) {
    char direct[192];
    char shim[192];
    size_t count = 0U;
    const char *line = generated;

    if ((generated == NULL) ||
        (snprintf(direct, sizeof(direct), "@%s(", function) >= (int)sizeof(direct)) ||
        (snprintf(shim, sizeof(shim), "@r_shim_%s(", function) >= (int)sizeof(shim))) {
        return 0U;
    }
    while ((line != NULL) && (*line != '\0')) {
        const char *line_end = strchr(line, '\n');
        const size_t length = line_end == NULL ? strlen(line) : (size_t)(line_end - line);
        const char *call = strstr(line, "call ");
        const char *callee = strstr(line, direct);

        if (callee == NULL) {
            callee = strstr(line, shim);
        }
        if ((call != NULL) && (callee != NULL) && (call < line + length) &&
            (callee < line + length) && (call < callee)) {
            count += 1U;
        }
        line = line_end == NULL ? NULL : line_end + 1;
    }
    return count;
}

/*
 * The hosted entry (R-FUNC-0004): main initializes the stack monitor of its thread, panics with
 * stack exhaustion (category 12, R_RUNTIME_PANIC_STACK_EXHAUSTION) when that fails, checks a
 * zero-byte budget and only then starts the runtime.
 */
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
    main_function = strstr(generated, "define i32 @main(i32 %0, ptr %1)");
    initialize =
        main_function == NULL
            ? NULL
            : strstr(main_function, "call zeroext i1 @r_runtime_stack_initialize_current_thread()");
    stack_panic =
        initialize == NULL ? NULL : strstr(initialize, "call void @r_runtime_panic(i32 12,");
    zero_preflight = stack_panic == NULL
                         ? NULL
                         : strstr(stack_panic, "call void @r_runtime_stack_require(i64 0,");
    hosted_start = zero_preflight == NULL
                       ? NULL
                       : strstr(zero_preflight, "@r_runtime_hosted_start(i32 %0, ptr %1)");

    R_CODEGEN_CHECK(main_function != NULL);
    R_CODEGEN_CHECK(initialize != NULL);
    R_CODEGEN_CHECK(stack_panic != NULL);
    R_CODEGEN_CHECK(zero_preflight != NULL);
    R_CODEGEN_CHECK(hosted_start != NULL);
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
    if ((r_frontend_analyze(context) != R_FRONTEND_OK) ||
        (r_frontend_lower_mir(context) != R_FRONTEND_OK)) {
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
    if ((r_frontend_analyze(context) != R_FRONTEND_OK) ||
        (r_frontend_lower_mir(context) != R_FRONTEND_OK)) {
        r_frontend_destroy(context);
        return NULL;
    }
    return context;
}

static bool r_codegen_buffers_equal(const RCodegenBuffer *left, const RCodegenBuffer *right) {
    return (left->length == right->length) &&
           ((left->length == 0U) || (memcmp(left->bytes, right->bytes, left->length) == 0));
}

/* The LLVM emitter lowers what the entry reaches; these tests cover functions no entry calls. */
static const RFrontendArtifactOptions r_codegen_every_function = {.all_functions = true};

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
    R_CODEGEN_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(
        r_codegen_read_file(R_CODEGEN_TARGET_MANIFEST_PATH, &manifest, &manifest_length));
    R_CODEGEN_CHECK(manifest_length != 0U);
    if ((manifest == NULL) || (manifest_length == 0U)) {
        goto cleanup;
    }
    /* The build accepts only the committed manifest of its target, by digest. */
    R_CODEGEN_CHECK(r_frontend_target_manifest_supported(manifest, manifest_length, false));
    (void)memset(&options, 0, sizeof(options));
    options.target_manifest = manifest;
    options.target_manifest_length = manifest_length;
    R_CODEGEN_CHECK(
        r_frontend_emit_llvm(context, &options, R_FRONTEND_LLVM_IR, r_codegen_write, &generated) ==
        R_FRONTEND_OK);
    R_CODEGEN_CHECK(generated.call_count == 1U);
    R_CODEGEN_CHECK((generated.bytes != NULL) &&
                    (strstr(generated.bytes, "target triple = \"arm64-apple-") != NULL));

    corrupted = malloc(manifest_length);
    R_CODEGEN_CHECK(corrupted != NULL);
    if (corrupted != NULL) {
        (void)memcpy(corrupted, manifest, manifest_length);
        corrupted[manifest_length / 2U] ^= UINT8_C(1);
        R_CODEGEN_CHECK(!r_frontend_target_manifest_supported(corrupted, manifest_length, false));
    }
    R_CODEGEN_CHECK(!r_frontend_target_manifest_supported(NULL, manifest_length, false));

    options.target_manifest = NULL;
    options.target_manifest_length = manifest_length;
    R_CODEGEN_CHECK(
        r_frontend_emit_llvm(context, &options, R_FRONTEND_LLVM_IR, r_codegen_write, &rejected) ==
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
        const RFrontendStatus forward_status = r_frontend_emit_llvm(
            forward, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &forward_output);
        const RFrontendStatus reverse_status = r_frontend_emit_llvm(
            reverse, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &reverse_output);
        R_CODEGEN_CHECK(forward_status == R_FRONTEND_OK);
        R_CODEGEN_CHECK(reverse_status == R_FRONTEND_OK);
        R_CODEGEN_CHECK(forward_output.call_count == 1U);
        R_CODEGEN_CHECK(reverse_output.call_count == 1U);
        R_CODEGEN_CHECK(forward_output.bytes != NULL);
        R_CODEGEN_CHECK(reverse_output.bytes != NULL);
        if ((forward_status == R_FRONTEND_OK) && (reverse_status == R_FRONTEND_OK) &&
            (forward_output.bytes != NULL) && (reverse_output.bytes != NULL)) {
            R_CODEGEN_CHECK(r_codegen_buffers_equal(&forward_output, &reverse_output));
            R_CODEGEN_CHECK(strstr(forward_output.bytes, "store i32 -2147483648,") != NULL);
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
        R_CODEGEN_CHECK(
            r_frontend_emit_llvm(
                contexts[order], NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &generated[order]) ==
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
        R_CODEGEN_CHECK(r_frontend_emit_llvm(
                            forward, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &forward_output) ==
                        R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_frontend_emit_llvm(
                            reverse, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &reverse_output) ==
                        R_FRONTEND_OK);
    }
    R_CODEGEN_CHECK(forward_output.call_count == 1U);
    R_CODEGEN_CHECK(reverse_output.call_count == 1U);
    R_CODEGEN_CHECK(forward_output.bytes != NULL);
    R_CODEGEN_CHECK(reverse_output.bytes != NULL);
    R_CODEGEN_CHECK(r_codegen_buffers_equal(&forward_output, &reverse_output));
    if (forward_output.bytes != NULL) {
        /* The synchronous and the asynchronous use of one literal share one constant; the
           literal only unreachable code reads is not in the program. */
        R_CODEGEN_CHECK(r_codegen_count_occurrences(forward_output.bytes, "c\"pool-shared\\00\"") ==
                        1U);
        R_CODEGEN_CHECK(strstr(forward_output.bytes, "pool-dead") == NULL);
    }
    if (forward != NULL) {
        R_CODEGEN_CHECK(
            r_frontend_emit_llvm(forward, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &repeated) ==
            R_FRONTEND_OK);
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
    R_CODEGEN_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
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
        R_CODEGEN_CHECK(
            r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &generated) ==
            R_FRONTEND_OK);
        R_CODEGEN_CHECK(generated.bytes != NULL);
        if (generated.bytes != NULL) {
            /* Every octet of the literal reaches the program unchanged, with its length. */
            R_CODEGEN_CHECK(
                r_codegen_count_occurrences(generated.bytes, "c\"\\00\\7F\\80\\FF\\00\"") == 1U);
            R_CODEGEN_CHECK(strstr(generated.bytes, "store i64 4,") != NULL);
        }
        R_CODEGEN_CHECK(
            r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &repeated) ==
            R_FRONTEND_OK);
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
    R_CODEGEN_CHECK(
        r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &baseline) ==
        R_FRONTEND_OK);
    emission_allocations = allocator.call_count - allocation_start;
    R_CODEGEN_CHECK(emission_allocations != 0U);
    R_CODEGEN_CHECK(baseline.call_count == 1U);
    R_CODEGEN_CHECK(allocator.live_count == live_baseline);

    for (offset = 1U; offset <= emission_allocations; ++offset) {
        RCodegenBuffer failed = {0};
        RCodegenBuffer retry = {0};
        const size_t retry_live_baseline = allocator.live_count;
        allocator.fail_at = allocator.call_count + offset;
        R_CODEGEN_CHECK(
            r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &failed) ==
            R_FRONTEND_OUT_OF_MEMORY);
        R_CODEGEN_CHECK(failed.call_count == 0U);
        R_CODEGEN_CHECK(failed.length == 0U);
        R_CODEGEN_CHECK(allocator.live_count == retry_live_baseline);
        allocator.fail_at = 0U;
        R_CODEGEN_CHECK(
            r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &retry) ==
            R_FRONTEND_OK);
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
        R_CODEGEN_CHECK(
            r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &rejected) ==
            R_FRONTEND_IO_ERROR);
        R_CODEGEN_CHECK(rejected.call_count == 1U);
        R_CODEGEN_CHECK(rejected.attempted_length == baseline.length);
        R_CODEGEN_CHECK(rejected.length == 0U);
        R_CODEGEN_CHECK(
            r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &retry) ==
            R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_codegen_buffers_equal(&baseline, &retry));
        r_codegen_dispose_buffer(&rejected);
        r_codegen_dispose_buffer(&retry);
    }

    allocator.fail_at = 0U;
    r_codegen_dispose_buffer(&baseline);
    r_frontend_destroy(context);
    R_CODEGEN_CHECK(allocator.live_count == 0U);
}

static void r_codegen_test_value_types(void) {
    static const char *const signatures[] = {
        "define internal i1 @\"test.codegen.value_types::echo_bool\"(i1 ",
        "define internal i8 @\"test.codegen.value_types::echo_i8\"(i8 ",
        "define internal i16 @\"test.codegen.value_types::echo_i16\"(i16 ",
        "define internal i64 @\"test.codegen.value_types::echo_i64\"(i64 ",
        "define internal i64 @\"test.codegen.value_types::echo_isize\"(i64 ",
        "define internal i8 @\"test.codegen.value_types::echo_u8\"(i8 ",
        "define internal i16 @\"test.codegen.value_types::echo_u16\"(i16 ",
        "define internal i64 @\"test.codegen.value_types::echo_u64\"(i64 ",
        "define internal i64 @\"test.codegen.value_types::echo_usize\"(i64 ",
        "define internal float @\"test.codegen.value_types::echo_f32\"(float ",
        "define internal double @\"test.codegen.value_types::echo_f64\"(double ",
        "define internal i32 @\"test.codegen.value_types::echo_char\"(i32 ",
    };
    static const char *const constants[] = {
        "store i8 -128,",
        "store i16 -32768,",
        "store i64 -9223372036854775808,",
        "store i64 -1,",
        "i32 128578",
        "i32 128640",
        "i32 1114111",
    };
    RCodegenAllocator allocator = {0};
    RFrontendContext *forward = r_codegen_build_value_types(&allocator, false);
    RFrontendContext *reverse = r_codegen_build_value_types(NULL, true);
    RCodegenBuffer baseline = {0};
    RCodegenBuffer reversed = {0};
    size_t allocation_start;
    size_t allocation_count;
    size_t live_baseline;
    size_t offset;
    size_t index;
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
    R_CODEGEN_CHECK(
        r_frontend_emit_llvm(
            forward, &r_codegen_every_function, R_FRONTEND_LLVM_IR, r_codegen_write, &baseline) ==
        R_FRONTEND_OK);
    allocation_count = allocator.call_count - allocation_start;
    R_CODEGEN_CHECK(
        r_frontend_emit_llvm(
            reverse, &r_codegen_every_function, R_FRONTEND_LLVM_IR, r_codegen_write, &reversed) ==
        R_FRONTEND_OK);
    R_CODEGEN_CHECK(r_codegen_buffers_equal(&baseline, &reversed));
    R_CODEGEN_CHECK(baseline.bytes != NULL);
    if (baseline.bytes != NULL) {
        /* R-EXPR-0021: an index is checked; a failed check raises the bounds panic (2). */
        R_CODEGEN_CHECK(r_codegen_count_calls(baseline.bytes, "r_runtime_raise") != 0U);
        R_CODEGEN_CHECK(strstr(baseline.bytes, "@r_runtime_raise(i32 2,") != NULL);
        R_CODEGEN_CHECK(strstr(baseline.bytes, "  switch i32 ") != NULL);
        for (index = 0U; index < (sizeof(signatures) / sizeof(signatures[0])); ++index) {
            R_CODEGEN_CHECK(strstr(baseline.bytes, signatures[index]) != NULL);
        }
        for (index = 0U; index < (sizeof(constants) / sizeof(constants[0])); ++index) {
            R_CODEGEN_CHECK(strstr(baseline.bytes, constants[index]) != NULL);
        }
    }
    R_CODEGEN_CHECK(allocation_count != 0U);
    R_CODEGEN_CHECK(allocator.live_count == live_baseline);
    for (offset = 1U; offset <= allocation_count; ++offset) {
        RCodegenBuffer failed = {0};
        RCodegenBuffer retry = {0};
        allocator.fail_at = allocator.call_count + offset;
        R_CODEGEN_CHECK(
            r_frontend_emit_llvm(
                forward, &r_codegen_every_function, R_FRONTEND_LLVM_IR, r_codegen_write, &failed) ==
            R_FRONTEND_OUT_OF_MEMORY);
        R_CODEGEN_CHECK(failed.call_count == 0U);
        R_CODEGEN_CHECK(failed.length == 0U);
        R_CODEGEN_CHECK(allocator.live_count == live_baseline);
        allocator.fail_at = 0U;
        R_CODEGEN_CHECK(
            r_frontend_emit_llvm(
                forward, &r_codegen_every_function, R_FRONTEND_LLVM_IR, r_codegen_write, &retry) ==
            R_FRONTEND_OK);
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

static RFrontendContext *r_codegen_build_array_slice(void) {
    RFrontendContext *context = r_codegen_create_context(NULL);

    if ((context == NULL) || !r_codegen_add_file(context, R_CODEGEN_ARRAY_SLICE_PATH) ||
        (r_frontend_analyze(context) != R_FRONTEND_OK) ||
        (r_frontend_diagnostic_count(context) != 0U) ||
        (r_frontend_lower_mir(context) != R_FRONTEND_OK)) {
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

    R_CODEGEN_CHECK(context != NULL);
    if (context != NULL) {
        R_CODEGEN_CHECK(r_codegen_find_array_slice(context, true, &const_view));
        R_CODEGEN_CHECK(r_codegen_find_array_slice(context, false, &mutable_view));
        R_CODEGEN_CHECK(r_frontend_emit_llvm(context,
                                             &r_codegen_every_function,
                                             R_FRONTEND_LLVM_IR,
                                             r_codegen_write,
                                             &generated) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(generated.call_count == 1U);
        R_CODEGEN_CHECK(generated.bytes != NULL);
        if (generated.bytes != NULL) {
            /* A whole-array slice is the array's own data and length, read in place. */
            R_CODEGEN_CHECK(
                strstr(generated.bytes,
                       "define internal i64 @\"test.codegen.array_slice::const_length\"(") != NULL);
            R_CODEGEN_CHECK(strstr(generated.bytes,
                                   "define internal i64 @\"test.codegen.array_slice::mutable_"
                                   "length\"(") != NULL);
            R_CODEGEN_CHECK(strstr(generated.bytes, "r_std_array_as_slice") == NULL);
        }
        R_CODEGEN_CHECK(r_frontend_emit_llvm(context,
                                             &r_codegen_every_function,
                                             R_FRONTEND_LLVM_IR,
                                             r_codegen_write,
                                             &repeated) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_codegen_buffers_equal(&generated, &repeated));
    }
    r_codegen_dispose_buffer(&generated);
    r_codegen_dispose_buffer(&repeated);
    r_frontend_destroy(context);
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
    R_CODEGEN_CHECK(
        r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &generated) ==
        R_FRONTEND_OK);
    R_CODEGEN_CHECK(generated.call_count == 1U);
    R_CODEGEN_CHECK(generated.bytes != NULL);
    R_CODEGEN_CHECK(
        (generated.bytes != NULL) &&
        (strstr(generated.bytes,
                "define internal void "
                "@\"test.codegen.sync_aggregate_destination::make_payload\"(") != NULL));
    R_CODEGEN_CHECK(
        r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &repeated) ==
        R_FRONTEND_OK);
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

    R_CODEGEN_CHECK(context != NULL);
    if (context != NULL) {
        const bool found = r_codegen_find_async_option_path(context, &view);

        R_CODEGEN_CHECK(found);
        if (found) {
            R_CODEGEN_CHECK(view.option_type->base != R_TYPE_ID_INVALID);
            R_CODEGEN_CHECK(view.option_type->second == R_TYPE_ID_INVALID);
            R_CODEGEN_CHECK(view.option_type->length == UINT64_C(0));
            R_CODEGEN_CHECK(view.option_type->flags == R_SEMANTIC_TYPE_FLAG_NONE);
            R_CODEGEN_CHECK(r_frontend_emit_llvm(
                                context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &generated) ==
                            R_FRONTEND_OK);
            R_CODEGEN_CHECK(generated.call_count == 1U);
            R_CODEGEN_CHECK(generated.bytes != NULL);
            if (generated.bytes != NULL) {
                /* The path inside the option kept across the await moves and drops with the
                   library's own move and destroy. */
                R_CODEGEN_CHECK(r_codegen_count_calls(generated.bytes, "r_std_fs_path_from_utf8") ==
                                1U);
                R_CODEGEN_CHECK(
                    r_codegen_count_calls(generated.bytes, "r_std_fs_path_move_initialize") != 0U);
                R_CODEGEN_CHECK(r_codegen_count_calls(generated.bytes, "r_std_fs_path_destroy") !=
                                0U);
            }
            R_CODEGEN_CHECK(r_frontend_emit_llvm(
                                context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &repeated) ==
                            R_FRONTEND_OK);
            R_CODEGEN_CHECK(r_codegen_buffers_equal(&generated, &repeated));
        }
    }
    r_codegen_dispose_buffer(&generated);
    r_codegen_dispose_buffer(&repeated);
    r_frontend_destroy(context);
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
    /* The emitter reads only MIR, which a program gets after it passes analysis. */
    if (invalid != NULL) {
        R_CODEGEN_CHECK(r_frontend_add_source(invalid,
                                              "invalid.r",
                                              (const uint8_t *)invalid_source,
                                              strlen(invalid_source),
                                              &source_id) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_frontend_emit_llvm(
                            invalid, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &invalid_output) ==
                        R_FRONTEND_INVALID_ARGUMENT);
        R_CODEGEN_CHECK(invalid_output.call_count == 0U);
        R_CODEGEN_CHECK(r_frontend_analyze(invalid) == R_FRONTEND_INVALID_SOURCE);
        R_CODEGEN_CHECK(r_frontend_lower_mir(invalid) != R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_frontend_emit_llvm(
                            invalid, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &invalid_output) ==
                        R_FRONTEND_INVALID_ARGUMENT);
        R_CODEGEN_CHECK(invalid_output.call_count == 0U);
    }
    if (recursive != NULL) {
        /* R-FUNC-0004: a recursive call chain is rejected before any code is emitted. */
        R_CODEGEN_CHECK(r_codegen_add_file(recursive, R_CODEGEN_RECURSIVE_PATH));
        R_CODEGEN_CHECK(r_frontend_analyze(recursive) == R_FRONTEND_INVALID_SOURCE);
        R_CODEGEN_CHECK(r_frontend_diagnostic_count(recursive) != 0U);
        R_CODEGEN_CHECK(r_frontend_lower_mir(recursive) != R_FRONTEND_OK);
        R_CODEGEN_CHECK(
            r_frontend_emit_llvm(
                recursive, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &recursive_output) ==
            R_FRONTEND_INVALID_ARGUMENT);
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
    R_CODEGEN_CHECK(
        r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &generated) ==
        R_FRONTEND_OK);
    R_CODEGEN_CHECK(generated.call_count == 1U);
    R_CODEGEN_CHECK(generated.length != 0U);
    R_CODEGEN_CHECK((generated.bytes != NULL) && (strstr(generated.bytes, "store i32 2,") != NULL));
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
    const char *async_step = NULL;
    const char *async_step_end = NULL;
    const char *async_success;
    const char *sync_function = NULL;
    const char *sync_function_end = NULL;
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
    R_CODEGEN_CHECK(
        r_frontend_emit_llvm(
            context, &r_codegen_every_function, R_FRONTEND_LLVM_IR, r_codegen_write, &generated) ==
        R_FRONTEND_OK);
    R_CODEGEN_CHECK(generated.bytes != NULL);

    /* Falling off the end of a void body with checked errors is its success: the asynchronous
       step stores the success tag and completes (R_RUNTIME_TASK_STEP_COMPLETED is 0) ... */
    R_CODEGEN_CHECK(
        (generated.bytes != NULL) &&
        r_codegen_ir_function(generated.bytes,
                              "@\"test.codegen.implicit_final_return::async_success$step\"",
                              &async_step,
                              &async_step_end));
    async_success = async_step == NULL ? NULL : strstr(async_step, "store i32 0, ptr %2");
    R_CODEGEN_CHECK((async_success != NULL) && (async_success < async_step_end));
    R_CODEGEN_CHECK((async_success != NULL) && (strstr(async_success, "ret i32 0") != NULL) &&
                    (strstr(async_success, "ret i32 0") < async_step_end));

    /* ... and the synchronous body stores it and returns, with no contract-violation panic. */
    R_CODEGEN_CHECK((generated.bytes != NULL) &&
                    r_codegen_ir_function(generated.bytes,
                                          "@\"test.codegen.implicit_final_return::sync_success\"",
                                          &sync_function,
                                          &sync_function_end));
    sync_success = sync_function == NULL ? NULL : strstr(sync_function, "store i32 0, ptr %0");
    sync_return = sync_success == NULL ? NULL : strstr(sync_success, "ret void");
    sync_fallback = sync_function == NULL ? NULL : strstr(sync_function, "@r_runtime_panic(");
    R_CODEGEN_CHECK((sync_success != NULL) && (sync_success < sync_function_end));
    R_CODEGEN_CHECK((sync_return != NULL) && (sync_return < sync_function_end));
    R_CODEGEN_CHECK((sync_fallback == NULL) || (sync_fallback > sync_function_end));

    r_codegen_dispose_buffer(&generated);
    r_frontend_destroy(context);
}

static void r_codegen_test_async_state_machine(void) {
    static const char *const runtime_calls[] = {
        "r_runtime_task_resumable_start_prepare",
        "r_runtime_task_start_commit_initialize",
        "r_runtime_task_execution_await",
        "r_runtime_task_destroy",
        "r_runtime_hosted_async_root_start_failure",
    };
    RFrontendContext *context = r_codegen_create_context(NULL);
    RCodegenBuffer generated = {0};
    RCodegenBuffer repeated = {0};
    RMirInstruction *character_literal = NULL;
    size_t instruction_index;
    size_t index;

    R_CODEGEN_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_CODEGEN_CHECK(r_codegen_add_file(context, R_CODEGEN_ASYNC_SCALAR_PATH));
    R_CODEGEN_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    /* The emitter reads only MIR. */
    R_CODEGEN_CHECK(
        r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &generated) ==
        R_FRONTEND_INVALID_ARGUMENT);
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
    R_CODEGEN_CHECK(
        r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &generated) ==
        R_FRONTEND_OK);
    R_CODEGEN_CHECK(generated.call_count == 1U);
    R_CODEGEN_CHECK(generated.bytes != NULL);
    if (generated.bytes != NULL) {
        const char *hosted_main = strstr(generated.bytes, "define i32 @main(i32 %0, ptr %1)");
        const char *blocking_await = strstr(generated.bytes, "@r_runtime_task_await(");
        const char *step = NULL;
        const char *step_end = NULL;
        const char *suspended;

        r_codegen_check_hosted_stack_bootstrap(generated.bytes);
        for (index = 0U; index < (sizeof(runtime_calls) / sizeof(runtime_calls[0])); ++index) {
            R_CODEGEN_CHECK(r_codegen_count_calls(generated.bytes, runtime_calls[index]) != 0U);
        }
        /* Each async function is a step function over its frame, with an initializer and a
           drop; the step of main suspends at its await (R_RUNTIME_TASK_STEP_SUSPENDED is 1). */
        R_CODEGEN_CHECK(strstr(generated.bytes,
                               "define internal i32 @\"test.codegen.async_scalar::child$step\"(") !=
                        NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes,
                               "define internal void "
                               "@\"test.codegen.async_scalar::child$initialize\"(") != NULL);
        R_CODEGEN_CHECK(
            strstr(generated.bytes,
                   "define internal void @\"test.codegen.async_scalar::child$drop\"(") != NULL);
        R_CODEGEN_CHECK(r_codegen_ir_function(
            generated.bytes, "@\"test.codegen.async_scalar::main$step\"", &step, &step_end));
        suspended = step == NULL ? NULL : strstr(step, "ret i32 1\n");
        R_CODEGEN_CHECK((suspended != NULL) && (suspended < step_end));
        R_CODEGEN_CHECK(strstr(generated.bytes, "i32 128640") != NULL);
        /* The hosted entry blocks on the root task. */
        R_CODEGEN_CHECK(hosted_main != NULL);
        R_CODEGEN_CHECK(blocking_await != NULL);
        if ((hosted_main != NULL) && (blocking_await != NULL)) {
            R_CODEGEN_CHECK(blocking_await > hosted_main);
        }
    }
    R_CODEGEN_CHECK(
        r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &repeated) ==
        R_FRONTEND_OK);
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
    RFrontendContext *context = r_codegen_create_context(NULL);
    RCodegenBuffer generated = {0};
    RCodegenBuffer repeated = {0};

    R_CODEGEN_CHECK(context != NULL);
    if (context != NULL) {
        R_CODEGEN_CHECK(r_codegen_add_file(context, R_CODEGEN_ASYNC_ENUM_PATH));
        R_CODEGEN_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_codegen_find_enum_constant(context, R_SEMANTIC_TYPE_I32) != NULL);
        R_CODEGEN_CHECK(r_codegen_find_enum_constant(context, R_SEMANTIC_TYPE_U32) != NULL);
        R_CODEGEN_CHECK(
            r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &generated) ==
            R_FRONTEND_OK);
        R_CODEGEN_CHECK(generated.bytes != NULL);
        R_CODEGEN_CHECK(
            r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &repeated) ==
            R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_codegen_buffers_equal(&generated, &repeated));
    }
    r_codegen_dispose_buffer(&generated);
    r_codegen_dispose_buffer(&repeated);
    r_frontend_destroy(context);
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

static void r_codegen_test_async_short_circuit_phi(void) {
    RFrontendContext *context = r_codegen_build_async_short_circuit();
    RCodegenBuffer generated = {0};
    RCodegenBuffer repeated = {0};
    size_t phi_index;

    R_CODEGEN_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_CODEGEN_CHECK(
        r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &generated) ==
        R_FRONTEND_OK);
    R_CODEGEN_CHECK(generated.call_count == 1U);
    R_CODEGEN_CHECK(generated.bytes != NULL);
    /* Eight short-circuit joins are phis of two predecessors each. */
    for (phi_index = 0U; phi_index < 8U; ++phi_index) {
        RCodegenPhiView view;

        R_CODEGEN_CHECK(r_codegen_phi_at(context, phi_index, &view));
        if (!r_codegen_phi_at(context, phi_index, &view)) {
            continue;
        }
        R_CODEGEN_CHECK(view.instruction->operand0 != R_MIR_VALUE_ID_INVALID);
        R_CODEGEN_CHECK(view.instruction->operand1 != R_MIR_VALUE_ID_INVALID);
        R_CODEGEN_CHECK(view.instruction->target0 != R_MIR_BLOCK_ID_INVALID);
        R_CODEGEN_CHECK(view.instruction->target1 != R_MIR_BLOCK_ID_INVALID);
    }
    {
        RCodegenPhiView absent;

        R_CODEGEN_CHECK(!r_codegen_phi_at(context, 8U, &absent));
    }
    R_CODEGEN_CHECK(
        r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &repeated) ==
        R_FRONTEND_OK);
    R_CODEGEN_CHECK(r_codegen_buffers_equal(&generated, &repeated));
    r_codegen_dispose_buffer(&generated);
    r_codegen_dispose_buffer(&repeated);
    r_frontend_destroy(context);
}

typedef struct RCodegenFusedDefaultOwnView {
    RMirFunction *function;
    RMirInstruction *array;
    RMirInstruction *aggregate;
    RMirInstruction *owner;
    RMirInstruction *discard;
    RTypeId task_type;
} RCodegenFusedDefaultOwnView;

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
    R_CODEGEN_CHECK(
        r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &generated) ==
        R_FRONTEND_OK);
    R_CODEGEN_CHECK(generated.call_count == 1U);
    R_CODEGEN_CHECK(generated.bytes != NULL);
    if (generated.bytes != NULL) {
        /* `new Scratch{}` of the all-zero default initializes the owner's storage in place: the
           1 MiB struct is never a temporary of the frame. The owner is released once. */
        R_CODEGEN_CHECK(r_codegen_count_calls(generated.bytes, "r_runtime_own_create_initialize") ==
                        1U);
        R_CODEGEN_CHECK(r_codegen_count_calls(generated.bytes, "r_runtime_own_create") == 0U);
        R_CODEGEN_CHECK(strstr(generated.bytes, "i8 0, i64 1048576,") == NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes, "[1048576 x i8]") == NULL);
        R_CODEGEN_CHECK(r_codegen_count_calls(generated.bytes, "r_runtime_own_release") == 1U);
    }
    R_CODEGEN_CHECK(
        r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &repeated) ==
        R_FRONTEND_OK);
    R_CODEGEN_CHECK(r_codegen_buffers_equal(&generated, &repeated));
    r_codegen_dispose_buffer(&generated);
    r_codegen_dispose_buffer(&repeated);
    r_frontend_destroy(context);
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
    RFrontendContext *context = r_codegen_build_async_sync_call();
    RCodegenBuffer generated = {0};
    RCodegenBuffer repeated = {0};

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
        R_CODEGEN_CHECK(
            r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &generated) ==
            R_FRONTEND_OK);
        R_CODEGEN_CHECK(generated.bytes != NULL);
        if (generated.bytes != NULL) {
            const char *step = NULL;
            const char *step_end = NULL;

            /* The step of main calls the synchronous functions as ordinary functions; the
               borrowed argument of add is a pointer. */
            R_CODEGEN_CHECK(strstr(generated.bytes,
                                   "define internal i32 @\"test.codegen.async_sync_call::add\"(ptr "
                                   "%0, i32 %1)") != NULL);
            R_CODEGEN_CHECK(r_codegen_ir_function(
                generated.bytes, "@\"test.codegen.async_sync_call::main$step\"", &step, &step_end));
            R_CODEGEN_CHECK(r_codegen_count_calls(generated.bytes,
                                                  "\"test.codegen.async_sync_call::add\"") == 2U);
            R_CODEGEN_CHECK(r_codegen_count_calls(generated.bytes,
                                                  "\"test.codegen.async_sync_call::forward\"") ==
                            1U);
            R_CODEGEN_CHECK(r_codegen_count_calls(generated.bytes,
                                                  "\"test.codegen.async_sync_call::consume\"") ==
                            1U);
            if ((step != NULL) && (step_end != NULL)) {
                const char *add_call =
                    strstr(step, "call i32 @\"test.codegen.async_sync_call::add\"(");

                R_CODEGEN_CHECK((add_call != NULL) && (add_call < step_end));
            }
        }
        R_CODEGEN_CHECK(
            r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &repeated) ==
            R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_codegen_buffers_equal(&generated, &repeated));
    }
    r_codegen_dispose_buffer(&generated);
    r_codegen_dispose_buffer(&repeated);
    r_frontend_destroy(context);
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

static void r_codegen_test_core_replace(void) {
    static const char source[] =
        "module test.core_replace; async i32 main() { "
        "own i32* current = new i32(1); own i32* next = new i32(2); "
        "own i32* old = core::replace(&current, move next); return *old - 1; }";
    RFrontendContext *context = r_codegen_create_context(NULL);
    RCodegenBuffer output = {0};
    RMirInstruction *call = NULL;

    R_CODEGEN_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_CODEGEN_CHECK(r_codegen_add_text(context, "core_replace.r", source));
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

        /* R-OWN-0019: the destination is an exclusive borrow, the replacement a move, and the
           old value the result. */
        R_CODEGEN_CHECK(call->operand_count == 2U);
        R_CODEGEN_CHECK(call->auxiliary_type != R_TYPE_ID_INVALID);
        R_CODEGEN_CHECK(call->call_borrow_mask.low != 0U);
        R_CODEGEN_CHECK(call->result != R_MIR_VALUE_ID_INVALID);
        R_CODEGEN_CHECK((right != NULL) && (right->kind == R_MIR_INSTRUCTION_MOVE));
        R_CODEGEN_CHECK(
            r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &output) ==
            R_FRONTEND_OK);
        R_CODEGEN_CHECK(output.call_count == 1U);
    }
    r_codegen_dispose_buffer(&output);
    r_frontend_destroy(context);
}

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

    R_CODEGEN_CHECK(context != NULL);
    if ((context != NULL) && r_codegen_find_async_mir_slice(context, &view)) {
        R_CODEGEN_CHECK(
            r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &generated) ==
            R_FRONTEND_OK);
        R_CODEGEN_CHECK(generated.call_count == 1U);
        R_CODEGEN_CHECK(generated.bytes != NULL);
        /* L39: the two checked ranges of a hosted async body raise the bounds panic (2) and
           unwind (R-ERR-0005). */
        R_CODEGEN_CHECK(r_codegen_count_occurrences(generated.bytes,
                                                    "call void @r_runtime_raise(i32 2,") == 2U);
        R_CODEGEN_CHECK(
            r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &repeated) ==
            R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_codegen_buffers_equal(&generated, &repeated));
    } else if (context != NULL) {
        R_CODEGEN_CHECK(false);
    }
    r_codegen_dispose_buffer(&generated);
    r_codegen_dispose_buffer(&repeated);
    r_frontend_destroy(context);
}

typedef struct RCodegenAsyncIoWriteAllView {
    RMirInstruction *call;
    RMirInstruction *stream;
    RMirInstruction *buffer;
    RMirInstruction *deadline;
} RCodegenAsyncIoWriteAllView;

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
        R_CODEGEN_CHECK(
            r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &generated) ==
            R_FRONTEND_OK);
        R_CODEGEN_CHECK(generated.call_count == 1U);
        R_CODEGEN_CHECK(generated.bytes != NULL);
        if (generated.bytes != NULL) {
            R_CODEGEN_CHECK(r_codegen_count_calls(generated.bytes, "r_std_io_write_all") == 1U);
            R_CODEGEN_CHECK(
                r_codegen_count_calls(generated.bytes, "r_std_io_write_all_result_destroy") != 0U);
        }
        R_CODEGEN_CHECK(
            r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &repeated) ==
            R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_codegen_buffers_equal(&generated, &repeated));
    }
    r_codegen_dispose_buffer(&generated);
    r_codegen_dispose_buffer(&repeated);
    r_frontend_destroy(context);
}

typedef struct RCodegenAsyncIoReadView {
    RMirInstruction *call;
    RMirInstruction *stream;
    RMirInstruction *buffer;
    RMirInstruction *deadline;
} RCodegenAsyncIoReadView;

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
        R_CODEGEN_CHECK(
            r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &generated) ==
            R_FRONTEND_OK);
        R_CODEGEN_CHECK(generated.call_count == 1U);
        R_CODEGEN_CHECK(generated.bytes != NULL);
        if (generated.bytes != NULL) {
            R_CODEGEN_CHECK(r_codegen_count_calls(generated.bytes, "r_std_io_read") == 1U);
            R_CODEGEN_CHECK(
                r_codegen_count_calls(generated.bytes, "r_std_io_read_result_destroy") != 0U);
        }
        R_CODEGEN_CHECK(
            r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &repeated) ==
            R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_codegen_buffers_equal(&generated, &repeated));
    }
    r_codegen_dispose_buffer(&generated);
    r_codegen_dispose_buffer(&repeated);
    r_frontend_destroy(context);
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
        R_CODEGEN_CHECK(
            r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &generated) ==
            R_FRONTEND_OK);
        R_CODEGEN_CHECK(generated.call_count == 1U);
        R_CODEGEN_CHECK(r_codegen_count_calls(generated.bytes, "r_std_io_stdout") == 1U);
        R_CODEGEN_CHECK(r_codegen_count_calls(generated.bytes, "r_std_io_output_move_initialize") ==
                        1U);
        R_CODEGEN_CHECK(r_codegen_count_calls(generated.bytes, "r_std_io_output_destroy") == 1U);
        R_CODEGEN_CHECK(r_codegen_count_calls(generated.bytes, "r_std_io_write_all") ==
                        (cases[index].is_async ? 1U : 0U));
        R_CODEGEN_CHECK(
            r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &repeated) ==
            R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_codegen_buffers_equal(&generated, &repeated));
        r_codegen_dispose_buffer(&generated);
        r_codegen_dispose_buffer(&repeated);
        r_frontend_destroy(context);
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
        R_CODEGEN_CHECK(
            r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &generated) ==
            R_FRONTEND_OK);
        R_CODEGEN_CHECK(generated.call_count == 1U);
        R_CODEGEN_CHECK(r_codegen_count_calls(generated.bytes, "r_std_io_stdin") == 1U);
        R_CODEGEN_CHECK(r_codegen_count_calls(generated.bytes, "r_std_io_input_move_initialize") ==
                        1U);
        R_CODEGEN_CHECK(r_codegen_count_calls(generated.bytes, "r_std_io_input_destroy") == 1U);
        R_CODEGEN_CHECK(
            r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &repeated) ==
            R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_codegen_buffers_equal(&generated, &repeated));
        r_codegen_dispose_buffer(&generated);
        r_codegen_dispose_buffer(&repeated);
        r_frontend_destroy(context);
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
    R_CODEGEN_CHECK(
        r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &generated) ==
        R_FRONTEND_OK);
    R_CODEGEN_CHECK(generated.call_count == 1U);
    R_CODEGEN_CHECK(generated.bytes != NULL);
    if (generated.bytes != NULL) {
        const char *hosted_main = strstr(generated.bytes, "define i32 @main(i32 %0, ptr %1)");
        const char *hosted_start =
            hosted_main == NULL ? NULL
                                : strstr(hosted_main, "@r_runtime_hosted_start(i32 %0, ptr %1)");
        const char *snapshot = hosted_start == NULL
                                   ? NULL
                                   : strstr(hosted_start, "@r_runtime_hosted_argument_snapshot(");
        const char *root_start =
            snapshot == NULL ? NULL : strstr(snapshot, "@r_runtime_task_start_commit_initialize(");
        const char *step = NULL;
        const char *step_end = NULL;
        const char *await;

        /* The hosted entry takes the argument snapshot after the runtime starts and before it
           starts the root task, whose step reads the arguments and awaits its child. */
        r_codegen_check_hosted_stack_bootstrap(generated.bytes);
        R_CODEGEN_CHECK(hosted_start != NULL);
        R_CODEGEN_CHECK(snapshot != NULL);
        R_CODEGEN_CHECK(root_start != NULL);
        R_CODEGEN_CHECK(r_codegen_ir_function(
            generated.bytes, "@\"test.codegen.async_main_args::main$step\"", &step, &step_end));
        await = step == NULL ? NULL : strstr(step, "@r_runtime_task_execution_await(");
        R_CODEGEN_CHECK((await != NULL) && (await < step_end));
    }
    R_CODEGEN_CHECK(
        r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &repeated) ==
        R_FRONTEND_OK);
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
    R_CODEGEN_CHECK(
        r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &generated) ==
        R_FRONTEND_OK);
    R_CODEGEN_CHECK(generated.call_count == 1U);
    R_CODEGEN_CHECK(generated.bytes != NULL);
    if (generated.bytes != NULL) {
        const char *hosted_start =
            strstr(generated.bytes, "@r_runtime_hosted_start(i32 %0, ptr %1)");
        const char *snapshot = hosted_start == NULL
                                   ? NULL
                                   : strstr(hosted_start, "@r_runtime_hosted_argument_snapshot(");
        const char *entry_call =
            snapshot == NULL
                ? NULL
                : strstr(snapshot, "call void @\"test.codegen.sync_main_args::main\"(");
        const char *finish =
            entry_call == NULL ? NULL : strstr(entry_call, "@r_runtime_hosted_finish(");

        /* The arguments are a snapshot taken after the runtime starts, passed to main as a
           slice; a synchronous program has no task. */
        r_codegen_check_hosted_stack_bootstrap(generated.bytes);
        R_CODEGEN_CHECK(strstr(generated.bytes,
                               "define internal void @\"test.codegen.sync_main_args::main\"(ptr "
                               "%0, ptr %1)") != NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes, "$step\"(") == NULL);
        R_CODEGEN_CHECK(strstr(generated.bytes, "@r_runtime_task_") == NULL);
        R_CODEGEN_CHECK(hosted_start != NULL);
        R_CODEGEN_CHECK(snapshot != NULL);
        R_CODEGEN_CHECK(entry_call != NULL);
        R_CODEGEN_CHECK(finish != NULL);
    }
    R_CODEGEN_CHECK(
        r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &repeated) ==
        R_FRONTEND_OK);
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
    R_CODEGEN_CHECK(
        r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &generated) ==
        R_FRONTEND_OK);
    R_CODEGEN_CHECK(generated.call_count == 1U);
    R_CODEGEN_CHECK(generated.bytes != NULL);
    if (generated.bytes != NULL) {
        const char *step = NULL;
        const char *step_end = NULL;
        const char *prepare = NULL;
        const char *commit = NULL;

        /* The step of main prepares the child's frame and commits it with the child's
           initializer, which copies the three Copy arguments into it. */
        R_CODEGEN_CHECK(r_codegen_ir_function(
            generated.bytes, "@\"test.async_copy_args::main$step\"", &step, &step_end));
        if ((step != NULL) && (step_end != NULL)) {
            prepare = strstr(step, "ptr @\"test.async_copy_args::encode$step\")");
            commit = strstr(step, "ptr @\"test.async_copy_args::encode$initialize\",");
        }
        R_CODEGEN_CHECK((prepare != NULL) && (prepare < step_end));
        R_CODEGEN_CHECK((commit != NULL) && (commit < step_end));
        R_CODEGEN_CHECK((prepare != NULL) && (commit != NULL) && (prepare < commit));
        R_CODEGEN_CHECK(strstr(generated.bytes,
                               "define internal void "
                               "@\"test.async_copy_args::encode$initialize\"(ptr %0, ptr %1)") !=
                        NULL);
    }
    R_CODEGEN_CHECK(
        r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &repeated) ==
        R_FRONTEND_OK);
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
    R_CODEGEN_CHECK(
        r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &generated) ==
        R_FRONTEND_OK);
    R_CODEGEN_CHECK(generated.call_count == 1U);
    R_CODEGEN_CHECK(generated.bytes != NULL);
    R_CODEGEN_CHECK(
        r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &repeated) ==
        R_FRONTEND_OK);
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
    R_CODEGEN_CHECK(staged_move_count == 18U);
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
    R_CODEGEN_CHECK(
        r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &generated) ==
        R_FRONTEND_OK);
    R_CODEGEN_CHECK(generated.bytes != NULL);
    R_CODEGEN_CHECK(
        r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &repeated) ==
        R_FRONTEND_OK);
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

        R_CODEGEN_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        R_CODEGEN_CHECK(r_codegen_add_file(context, paths[index]));
        R_CODEGEN_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(
            r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &generated) ==
            R_FRONTEND_OK);
        R_CODEGEN_CHECK(generated.bytes != NULL);
        /* core::assume is an assumption of the optimizer, not a call. */
        R_CODEGEN_CHECK(r_codegen_count_occurrences(generated.bytes, "call void @llvm.assume(") ==
                        1U);
        R_CODEGEN_CHECK((generated.bytes != NULL) &&
                        (strstr(generated.bytes, "r_core_assume") == NULL));
        R_CODEGEN_CHECK((generated.bytes != NULL) &&
                        ((strstr(generated.bytes, "$step\"(") != NULL) == (index == 1U)));
        R_CODEGEN_CHECK(
            r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &repeated) ==
            R_FRONTEND_OK);
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
        R_CODEGEN_CHECK(
            r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &generated) ==
            R_FRONTEND_OK);
        R_CODEGEN_CHECK(generated.bytes != NULL);
        /* One volatile store and one volatile load of the i32, each exactly once. */
        R_CODEGEN_CHECK(r_codegen_count_occurrences(generated.bytes, "store volatile i32 ") == 1U);
        R_CODEGEN_CHECK(r_codegen_count_occurrences(generated.bytes, "load volatile i32, ") == 1U);
        R_CODEGEN_CHECK((generated.bytes != NULL) &&
                        (strstr(generated.bytes, "r_core_volatile") == NULL));
        R_CODEGEN_CHECK((generated.bytes != NULL) &&
                        ((strstr(generated.bytes, "$step\"(") != NULL) == (index == 1U)));
        R_CODEGEN_CHECK(
            r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &repeated) ==
            R_FRONTEND_OK);
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
        R_CODEGEN_CHECK(
            r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &generated) ==
            R_FRONTEND_OK);
        R_CODEGEN_CHECK(generated.bytes != NULL);
        /* The slice is built from the pointer and the length; no runtime function is called. */
        R_CODEGEN_CHECK((generated.bytes != NULL) &&
                        (strstr(generated.bytes, "r_core_slice_from_raw_parts") == NULL));
        R_CODEGEN_CHECK((generated.bytes != NULL) &&
                        ((strstr(generated.bytes, "$step\"(") != NULL) == (index == 1U)));
        R_CODEGEN_CHECK(
            r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &repeated) ==
            R_FRONTEND_OK);
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
        R_CODEGEN_CHECK(
            r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &generated) ==
            R_FRONTEND_OK);
        R_CODEGEN_CHECK((generated.bytes != NULL) &&
                        (strstr(generated.bytes, "r_core_slice_from_raw_parts") == NULL));
        R_CODEGEN_CHECK((generated.bytes != NULL) &&
                        ((strstr(generated.bytes, "$step\"(") != NULL) == (index == 1U)));
        R_CODEGEN_CHECK(
            r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &repeated) ==
            R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_codegen_buffers_equal(&generated, &repeated));
        r_codegen_dispose_buffer(&generated);
        r_codegen_dispose_buffer(&repeated);
        r_frontend_destroy(context);
    }
}

/* The drop glue the type record of the first adopted owner names: `@r_drop.N`. */
static bool r_codegen_adopted_drop_glue(const char *generated, char *name, size_t capacity) {
    const char *store = generated == NULL ? NULL : strstr(generated, "store ptr @r_drop.");
    size_t length = 0U;

    if (store == NULL) {
        return false;
    }
    store += strlen("store ptr ");
    while ((store[length] != ',') && (store[length] != '\0') && (length + 1U < capacity)) {
        name[length] = store[length];
        length += 1U;
    }
    name[length] = '\0';
    return store[length] == ',';
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

        R_CODEGEN_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        R_CODEGEN_CHECK(r_codegen_add_file(context, paths[index]));
        R_CODEGEN_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
        R_CODEGEN_CHECK(
            r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &generated) ==
            R_FRONTEND_OK);
        R_CODEGEN_CHECK(generated.bytes != NULL);
        /* Two adoptions, one release back to raw, and the owner that is not released is
           dropped; adoption allocates nothing. */
        R_CODEGEN_CHECK(r_codegen_count_calls(generated.bytes, "r_runtime_own_adopt") == 2U);
        R_CODEGEN_CHECK(r_codegen_count_calls(generated.bytes, "r_runtime_own_into_raw") == 1U);
        R_CODEGEN_CHECK(r_codegen_count_calls(generated.bytes, "r_runtime_own_release") == 1U);
        R_CODEGEN_CHECK(r_codegen_count_calls(generated.bytes, "r_runtime_own_create") == 0U);
        R_CODEGEN_CHECK((generated.bytes != NULL) &&
                        (strstr(generated.bytes, "r_runtime_allocator_allocate") == NULL));
        /* An i32 pointee has no move or drop glue. */
        R_CODEGEN_CHECK((generated.bytes != NULL) &&
                        (strstr(generated.bytes, "store ptr @r_drop.") == NULL));
        R_CODEGEN_CHECK((generated.bytes != NULL) &&
                        ((strstr(generated.bytes, "$step\"(") != NULL) == (index == 1U)));
        R_CODEGEN_CHECK(
            r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &repeated) ==
            R_FRONTEND_OK);
        R_CODEGEN_CHECK(r_codegen_buffers_equal(&generated, &repeated));
        r_codegen_dispose_buffer(&generated);
        r_codegen_dispose_buffer(&repeated);
        r_frontend_destroy(context);
    }
    {
        RFrontendContext *context = r_codegen_create_context(NULL);
        RCodegenBuffer generated = {0};
        RCodegenBuffer repeated = {0};
        char drop_name[64];
        const char *drop = NULL;
        const char *drop_end = NULL;

        R_CODEGEN_CHECK(context != NULL);
        if (context != NULL) {
            R_CODEGEN_CHECK(r_codegen_add_file(context, R_CODEGEN_CORE_ADOPT_NONTRIVIAL_PATH));
            R_CODEGEN_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
            R_CODEGEN_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
            R_CODEGEN_CHECK(r_frontend_emit_llvm(
                                context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &generated) ==
                            R_FRONTEND_OK);
            R_CODEGEN_CHECK(generated.bytes != NULL);
            /* The adopted Box carries its move and drop glue; the glue drops the fields in
               reverse declaration order, so the first field, at offset 0, goes last
               (R-INIT-0010). */
            R_CODEGEN_CHECK((generated.bytes != NULL) &&
                            (strstr(generated.bytes, "store ptr @r_move.") != NULL));
            R_CODEGEN_CHECK(
                r_codegen_adopted_drop_glue(generated.bytes, drop_name, sizeof(drop_name)));
            R_CODEGEN_CHECK(r_codegen_ir_function(generated.bytes, drop_name, &drop, &drop_end));
            if ((drop != NULL) && (drop_end != NULL)) {
                const char *first_field = strstr(drop, "(ptr %0)\n  ret void");

                R_CODEGEN_CHECK(r_codegen_count_occurrences(drop, "  call void @r_drop.") >= 2U);
                R_CODEGEN_CHECK((first_field != NULL) && (first_field < drop_end));
            }
            R_CODEGEN_CHECK(r_codegen_count_calls(generated.bytes, "r_runtime_own_adopt") == 2U);
            R_CODEGEN_CHECK(r_codegen_count_calls(generated.bytes, "r_runtime_own_into_raw") != 0U);
            R_CODEGEN_CHECK((generated.bytes != NULL) &&
                            (strstr(generated.bytes, "r_runtime_allocator_allocate") == NULL));
            R_CODEGEN_CHECK(r_frontend_emit_llvm(
                                context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &repeated) ==
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
            R_CODEGEN_CHECK(r_frontend_emit_llvm(
                                context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &generated) ==
                            R_FRONTEND_OK);
            R_CODEGEN_CHECK(generated.bytes != NULL);
            R_CODEGEN_CHECK((generated.bytes != NULL) &&
                            (strstr(generated.bytes, "store ptr @r_move.") != NULL));
            R_CODEGEN_CHECK((generated.bytes != NULL) &&
                            (strstr(generated.bytes, "store ptr @r_drop.") != NULL));
            R_CODEGEN_CHECK(r_codegen_count_calls(generated.bytes, "r_runtime_own_release") != 0U);
            if (path_index == 0U) {
                /* The task held in the adopted Box is destroyed with it. */
                R_CODEGEN_CHECK(r_codegen_count_calls(generated.bytes, "r_runtime_task_destroy") !=
                                0U);
            } else {
                /* An adopted owner local to an async body is dropped with its frame. */
                R_CODEGEN_CHECK((generated.bytes != NULL) &&
                                (strstr(generated.bytes, "$drop\"(ptr %0)") != NULL));
            }
            R_CODEGEN_CHECK(r_frontend_emit_llvm(
                                context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &repeated) ==
                            R_FRONTEND_OK);
            R_CODEGEN_CHECK(r_codegen_buffers_equal(&generated, &repeated));
            r_codegen_dispose_buffer(&generated);
            r_codegen_dispose_buffer(&repeated);
            r_frontend_destroy(context);
        }
    }
    {
        static const char *const destroyers[] = {
            "r_runtime_array_destroy",
            "r_runtime_list_destroy",
            "r_runtime_dict_destroy",
            "r_runtime_arc_release",
            "r_runtime_rc_release",
            "r_runtime_weak_arc_release",
            "r_runtime_weak_rc_release",
        };
        RFrontendContext *context = r_codegen_create_context(NULL);
        RCodegenBuffer generated = {0};
        RCodegenBuffer repeated = {0};
        size_t destroyer;

        R_CODEGEN_CHECK(context != NULL);
        if (context != NULL) {
            R_CODEGEN_CHECK(r_codegen_add_file(context, R_CODEGEN_CORE_ADOPT_RUNTIME_VALUES_PATH));
            R_CODEGEN_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
            R_CODEGEN_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
            R_CODEGEN_CHECK(r_frontend_emit_llvm(
                                context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &generated) ==
                            R_FRONTEND_OK);
            R_CODEGEN_CHECK(generated.bytes != NULL);
            /* Every runtime value the adopted struct holds is destroyed by its runtime. */
            for (destroyer = 0U; destroyer < (sizeof(destroyers) / sizeof(destroyers[0]));
                 ++destroyer) {
                R_CODEGEN_CHECK(r_codegen_count_calls(generated.bytes, destroyers[destroyer]) !=
                                0U);
            }
            R_CODEGEN_CHECK((generated.bytes != NULL) &&
                            (strstr(generated.bytes, "store ptr @r_move.") != NULL));
            R_CODEGEN_CHECK((generated.bytes != NULL) &&
                            (strstr(generated.bytes, "store ptr @r_drop.") != NULL));
            R_CODEGEN_CHECK(r_frontend_emit_llvm(
                                context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &repeated) ==
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
    size_t index;

    R_CODEGEN_CHECK(record_count == 91U);
    R_CODEGEN_CHECK(header_count == 23U);
    for (index = 0U; index < record_count; ++index) {
        const RNamedStandardCopyAbi *record = r_named_standard_copy_abi_at(index);
        if ((record != NULL) && !r_codegen_standard_type_holds_region(record->r_name)) {
            adopted_count += 1U;
        }
    }
    R_CODEGEN_CHECK(adopted_count == 90U);
    for (index = 0U; index < record_count; ++index) {
        const RNamedStandardCopyAbi *record = r_named_standard_copy_abi_at(index);

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
    }
    R_CODEGEN_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_CODEGEN_CHECK(r_codegen_add_file(context, R_CODEGEN_CORE_ADOPT_NAMED_STANDARD_COPY_PATH));
    R_CODEGEN_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(r_frontend_diagnostic_count(context) == 0U);
    R_CODEGEN_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
    R_CODEGEN_CHECK(
        r_frontend_emit_llvm(
            context, &r_codegen_every_function, R_FRONTEND_LLVM_IR, r_codegen_write, &generated) ==
        R_FRONTEND_OK);
    R_CODEGEN_CHECK(generated.bytes != NULL);
    /* Each adopted standard Copy type is adopted and released once. */
    R_CODEGEN_CHECK(r_codegen_count_calls(generated.bytes, "r_runtime_own_adopt") == adopted_count);
    R_CODEGEN_CHECK(r_codegen_count_calls(generated.bytes, "r_runtime_own_into_raw") ==
                    adopted_count);
    R_CODEGEN_CHECK(
        r_frontend_emit_llvm(
            context, &r_codegen_every_function, R_FRONTEND_LLVM_IR, r_codegen_write, &repeated) ==
        R_FRONTEND_OK);
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
    R_CODEGEN_CHECK(
        r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &generated) ==
        R_FRONTEND_OK);
    R_CODEGEN_CHECK(generated.bytes != NULL);
    R_CODEGEN_CHECK(r_codegen_count_calls(generated.bytes, "r_runtime_own_adopt") == 0U);
    R_CODEGEN_CHECK(
        r_frontend_emit_llvm(context, NULL, R_FRONTEND_LLVM_IR, r_codegen_write, &repeated) ==
        R_FRONTEND_OK);
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
    R_CODEGEN_CHECK(
        r_frontend_emit_llvm(
            context, &r_codegen_every_function, R_FRONTEND_LLVM_IR, r_codegen_write, &generated) ==
        R_FRONTEND_OK);
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
        char move_pattern[128];
        char drop_pattern[128];

        R_CODEGEN_CHECK(record != NULL);
        if (record == NULL) {
            continue;
        }
        R_CODEGEN_CHECK(r_named_standard_move_abi_find(record->r_name, record->r_name_length) ==
                        record);
        R_CODEGEN_CHECK((index == 0U) || (strcmp(r_named_standard_move_abi_at(index - 1U)->r_name,
                                                 record->r_name) < 0));
        /* Each type moves through the library's move function; only the types whose values
           the program ends call the library's destroy function. */
        R_CODEGEN_CHECK(
            snprintf(move_pattern, sizeof(move_pattern), "%s(", record->move_initialize) > 0);
        R_CODEGEN_CHECK(snprintf(drop_pattern, sizeof(drop_pattern), "%s(", record->drop) > 0);
        R_CODEGEN_CHECK((generated.bytes != NULL) &&
                        (strstr(generated.bytes, move_pattern) != NULL));
        R_CODEGEN_CHECK((generated.bytes != NULL) &&
                        ((strstr(generated.bytes, drop_pattern) != NULL) == emits_drop));
    }
    for (index = 0U; index < header_count; ++index) {
        R_CODEGEN_CHECK(r_named_standard_move_abi_header_at(index) != NULL);
    }
    R_CODEGEN_CHECK(r_codegen_count_calls(generated.bytes, "r_runtime_arc_release") != 0U);
    R_CODEGEN_CHECK((generated.bytes != NULL) && (strstr(generated.bytes, "$step\"(") != NULL));
    R_CODEGEN_CHECK(
        r_frontend_emit_llvm(
            context, &r_codegen_every_function, R_FRONTEND_LLVM_IR, r_codegen_write, &repeated) ==
        R_FRONTEND_OK);
    R_CODEGEN_CHECK(r_codegen_buffers_equal(&generated, &repeated));
    r_codegen_dispose_buffer(&generated);
    r_codegen_dispose_buffer(&repeated);
    r_frontend_destroy(context);
}

int main(void) {
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
    r_codegen_test_core_replace();
    r_codegen_test_async_fused_default_own();
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

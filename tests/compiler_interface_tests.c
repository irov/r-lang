#include "r_frontend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct RCompilerTestBuffer {
    char *bytes;
    size_t length;
    size_t capacity;
    size_t calls;
} RCompilerTestBuffer;

typedef struct RCompilerTestAllocator {
    size_t call_count;
    size_t fail_at;
    size_t live_count;
} RCompilerTestAllocator;

static int failures = 0;

#define R_COMPILER_CHECK(condition)                                                                \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            (void)fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);    \
            failures += 1;                                                                         \
        }                                                                                          \
    } while (false)

static void *r_compiler_test_allocate(void *user_data, size_t size) {
    RCompilerTestAllocator *allocator = user_data;
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

static void r_compiler_test_free(void *user_data, void *pointer) {
    RCompilerTestAllocator *allocator = user_data;

    if (pointer != NULL) {
        R_COMPILER_CHECK(allocator->live_count != 0U);
        if (allocator->live_count != 0U) {
            allocator->live_count -= 1U;
        }
        free(pointer);
    }
}

static bool r_compiler_test_write(void *user_data, const char *bytes, size_t length) {
    RCompilerTestBuffer *buffer = user_data;
    size_t required;
    size_t capacity;
    char *replacement;

    buffer->calls += 1U;
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

static void r_compiler_test_buffer_destroy(RCompilerTestBuffer *buffer) {
    free(buffer->bytes);
    (void)memset(buffer, 0, sizeof(*buffer));
}

static bool r_compiler_prepare_hir(RFrontendContext *context) {
    size_t source_index;

    for (source_index = 0U; source_index < r_frontend_source_count(context); ++source_index) {
        RSourceId source_id = (RSourceId)(source_index + 1U);
        RSyntaxNodeId cst = R_SYNTAX_NODE_ID_INVALID;
        RAstNodeId ast = R_AST_NODE_ID_INVALID;
        if ((r_frontend_lex(context, source_id) != R_FRONTEND_OK) ||
            (r_frontend_parse_cst(context, source_id, &cst) != R_FRONTEND_OK) ||
            (r_frontend_lower_ast(context, source_id, &ast) != R_FRONTEND_OK)) {
            return false;
        }
    }
    return r_frontend_analyze(context) == R_FRONTEND_OK;
}

static bool r_compiler_prepare(RFrontendContext *context) {
    return r_compiler_prepare_hir(context) && (r_frontend_lower_mir(context) == R_FRONTEND_OK);
}

static RFrontendContext *r_compiler_make_program(bool reverse) {
    static const char alpha[] = "module compiler.alpha;\n"
                                "void produce(out i32 value) { value = 42; }\n"
                                "struct Plain { i32 code; };\n"
                                "error Failure { i32 code; };\n"
                                "error Choice { A, B, };\n"
                                "protected error Hidden {};\n"
                                "const usize CAPACITY = 8;\n"
                                "thread_local i32 THREAD_COUNTER = 3;\n"
                                "thread_local bool THREAD_ENABLED = true;\n"
                                "protected const u32 INTERNAL_TAG = 7;\n"
                                "protected i32 helper(i32 value) {\n"
                                "    i32 result = value + 1;\n"
                                "    return result;\n"
                                "}\n"
                                "protected i32 choose(bool flag) {\n"
                                "    if (flag == true) {\n"
                                "        return 1;\n"
                                "    } else {\n"
                                "        return 2;\n"
                                "    }\n"
                                "}\n"
                                "protected void observe() {\n"
                                "}\n"
                                "const i32* choose_borrow(bool first, const i32* left,\n"
                                "                  const i32* right, const i32* unused) {\n"
                                "    unused as void;\n"
                                "    const i32* chosen = (first == true) ? left : right;\n"
                                "    return chosen;\n"
                                "}\n";
    static const char beta[] = "module compiler.beta;\n"
                               "import compiler.alpha;\n"
                               "const u32 MODE = 2;\n"
                               "i32 SHARED_COUNTER = 4;\n"
                               "i32 SHARED_SUM = 2 + 3;\n"
                               "i32 main() {\n"
                               "    i32 output = 0;\n"
                               "    compiler.alpha::produce(out output);\n"
                               "    output as void;\n"
                               "    i32 value = 0;\n"
                               "    bool enabled = true;\n"
                               "    bool accepted = enabled && (value == 0);\n"
                               "    if (accepted == true) {\n"
                               "        value += 1;\n"
                               "    } else {\n"
                               "        value = 2;\n"
                               "    }\n"
                               "    while (value < 3) {\n"
                               "        value += 1;\n"
                               "    }\n"
                               "    return value;\n"
                               "}\n";
    RFrontendContext *context = r_frontend_create(NULL);
    RSourceId source_id = R_SOURCE_ID_INVALID;

    if (context == NULL) {
        return NULL;
    }
    if (reverse) {
        if ((r_frontend_add_source(
                 context, "beta.r", (const uint8_t *)beta, strlen(beta), &source_id) !=
             R_FRONTEND_OK) ||
            (r_frontend_add_source(
                 context, "alpha.r", (const uint8_t *)alpha, strlen(alpha), &source_id) !=
             R_FRONTEND_OK)) {
            r_frontend_destroy(context);
            return NULL;
        }
    } else if ((r_frontend_add_source(
                    context, "alpha.r", (const uint8_t *)alpha, strlen(alpha), &source_id) !=
                R_FRONTEND_OK) ||
               (r_frontend_add_source(
                    context, "beta.r", (const uint8_t *)beta, strlen(beta), &source_id) !=
                R_FRONTEND_OK)) {
        r_frontend_destroy(context);
        return NULL;
    }
    if (!r_compiler_prepare(context)) {
        r_frontend_destroy(context);
        return NULL;
    }
    return context;
}

static void r_compiler_test_mir_and_determinism(void) {
    RFrontendContext *forward = r_compiler_make_program(false);
    RFrontendContext *reverse = r_compiler_make_program(true);
    RCompilerTestBuffer forward_mir = {0};
    RCompilerTestBuffer reverse_mir = {0};
    RCompilerTestBuffer interface = {0};
    RCompilerTestBuffer reverse_interface = {0};
    RCompilerTestBuffer link_plan = {0};
    RCompilerTestBuffer reverse_link_plan = {0};
    RCompilerTestBuffer program = {0};
    RCompilerTestBuffer reverse_program = {0};
    RFrontendArtifactOptions options;

    R_COMPILER_CHECK(forward != NULL);
    R_COMPILER_CHECK(reverse != NULL);
    if ((forward == NULL) || (reverse == NULL)) {
        r_frontend_destroy(forward);
        r_frontend_destroy(reverse);
        return;
    }
    R_COMPILER_CHECK(r_frontend_dump_mir(forward, r_compiler_test_write, &forward_mir) ==
                     R_FRONTEND_OK);
    R_COMPILER_CHECK(r_frontend_dump_mir(reverse, r_compiler_test_write, &reverse_mir) ==
                     R_FRONTEND_OK);
    R_COMPILER_CHECK(forward_mir.length == reverse_mir.length);
    R_COMPILER_CHECK((forward_mir.length == reverse_mir.length) &&
                     (memcmp(forward_mir.bytes, reverse_mir.bytes, forward_mir.length) == 0));
    R_COMPILER_CHECK(strstr(forward_mir.bytes, "(branch condition=") != NULL);
    R_COMPILER_CHECK(strstr(forward_mir.bytes, "(jump target=") != NULL);
    R_COMPILER_CHECK(strstr(forward_mir.bytes, " = phi incoming=(") != NULL);
    R_COMPILER_CHECK(strstr(forward_mir.bytes, "(unreachable)") != NULL);
    R_COMPILER_CHECK(strstr(forward_mir.bytes, "(return)\n") != NULL);
    R_COMPILER_CHECK(strstr(forward_mir.bytes, "(store place=%local") != NULL);
    R_COMPILER_CHECK(strstr(forward_mir.bytes, "symbol=") == NULL);

    R_COMPILER_CHECK(
        r_frontend_emit_llvm(forward, NULL, R_FRONTEND_LLVM_IR, r_compiler_test_write, &program) ==
        R_FRONTEND_OK);
    R_COMPILER_CHECK(
        r_frontend_emit_llvm(
            reverse, NULL, R_FRONTEND_LLVM_IR, r_compiler_test_write, &reverse_program) ==
        R_FRONTEND_OK);
    R_COMPILER_CHECK(program.length == reverse_program.length);
    R_COMPILER_CHECK((program.length == reverse_program.length) &&
                     (memcmp(program.bytes, reverse_program.bytes, program.length) == 0));

    (void)memset(&options, 0, sizeof(options));
    options.profile = "hosted";
    R_COMPILER_CHECK(r_frontend_dump_interface(
                         forward, &options, r_compiler_test_write, &interface) == R_FRONTEND_OK);
    R_COMPILER_CHECK(
        r_frontend_dump_interface(reverse, &options, r_compiler_test_write, &reverse_interface) ==
        R_FRONTEND_OK);
    R_COMPILER_CHECK(interface.length == reverse_interface.length);
    R_COMPILER_CHECK((interface.length == reverse_interface.length) &&
                     (memcmp(interface.bytes, reverse_interface.bytes, interface.length) == 0));
    R_COMPILER_CHECK(strstr(interface.bytes, "compiler.beta::main") != NULL);
    R_COMPILER_CHECK(strstr(interface.bytes, "(interface version=34 ") != NULL);
    R_COMPILER_CHECK(
        strstr(interface.bytes, "compiler.alpha::produce\" return=void parameters=((out i32))") !=
        NULL);
    /* R-FFI-0044: the ABI record fingerprint is part of every interface, absent or present. */
    R_COMPILER_CHECK(strstr(interface.bytes, "(abi-record present=false)") != NULL);
    R_COMPILER_CHECK(strstr(interface.bytes, "copy=true error=false") != NULL);
    R_COMPILER_CHECK(strstr(interface.bytes, "copy=true error=true") != NULL);
    R_COMPILER_CHECK(strstr(interface.bytes, "compiler.alpha::Hidden") == NULL);

    R_COMPILER_CHECK(strstr(interface.bytes, "(profile \"hosted\")") != NULL);
    R_COMPILER_CHECK(strstr(interface.bytes,
                            "(constant name=\"compiler.alpha::CAPACITY\" type=(const usize) "
                            "value=8)") != NULL);
    R_COMPILER_CHECK(strstr(interface.bytes,
                            "(constant name=\"compiler.beta::MODE\" type=(const u32) value=2)") !=
                     NULL);
    R_COMPILER_CHECK(
        strstr(
            interface.bytes,
            "(object name=\"compiler.alpha::THREAD_COUNTER\" type=i32 storage=thread value=3)") !=
        NULL);
    R_COMPILER_CHECK(
        strstr(
            interface.bytes,
            "(object name=\"compiler.alpha::THREAD_ENABLED\" type=bool storage=thread value=1)") !=
        NULL);
    R_COMPILER_CHECK(
        strstr(interface.bytes,
               "(object name=\"compiler.beta::SHARED_COUNTER\" type=i32 storage=static value=4)") !=
        NULL);
    R_COMPILER_CHECK(
        strstr(interface.bytes,
               "(object name=\"compiler.beta::SHARED_SUM\" type=i32 storage=static value=5)") !=
        NULL);
    R_COMPILER_CHECK(strstr(interface.bytes, "INTERNAL_TAG") == NULL);
    R_COMPILER_CHECK(strstr(interface.bytes, "compiler.alpha::helper") == NULL);
    R_COMPILER_CHECK(
        strstr(interface.bytes,
               "compiler.alpha::choose_borrow\" return=(const_borrow i32) parameters=(bool "
               "(const_borrow i32) (const_borrow i32) (const_borrow i32)) "
               "return_borrow_parameters=(1 2)") != NULL);
    options.entry = "compiler.beta::main";
    R_COMPILER_CHECK(r_frontend_dump_link_plan(
                         forward, &options, r_compiler_test_write, &link_plan) == R_FRONTEND_OK);
    R_COMPILER_CHECK(
        r_frontend_dump_link_plan(reverse, &options, r_compiler_test_write, &reverse_link_plan) ==
        R_FRONTEND_OK);
    R_COMPILER_CHECK(link_plan.length == reverse_link_plan.length);
    R_COMPILER_CHECK((link_plan.length == reverse_link_plan.length) &&
                     (memcmp(link_plan.bytes, reverse_link_plan.bytes, link_plan.length) == 0));

    r_compiler_test_buffer_destroy(&forward_mir);
    r_compiler_test_buffer_destroy(&reverse_mir);
    r_compiler_test_buffer_destroy(&interface);
    r_compiler_test_buffer_destroy(&reverse_interface);
    r_compiler_test_buffer_destroy(&link_plan);
    r_compiler_test_buffer_destroy(&reverse_link_plan);
    r_compiler_test_buffer_destroy(&program);
    r_compiler_test_buffer_destroy(&reverse_program);
    r_frontend_destroy(forward);
    r_frontend_destroy(reverse);
}

static void r_compiler_test_artifacts(void) {
    static const uint8_t target_manifest[] = "{\"target\":\"test\"}";
    static const uint8_t link_manifest[] = "{\"links\":[]}";
    RFrontendContext *context = r_compiler_make_program(false);
    RFrontendArtifactOptions options;
    RCompilerTestBuffer link_plan = {0};
    RCompilerTestBuffer bundle = {0};
    RCompilerTestBuffer rejected = {0};

    R_COMPILER_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    (void)memset(&options, 0, sizeof(options));
    options.entry = "compiler.beta";
    options.profile = "hosted";
    options.target_manifest = target_manifest;
    options.target_manifest_length = sizeof(target_manifest) - 1U;
    options.link_manifest = link_manifest;
    options.link_manifest_length = sizeof(link_manifest) - 1U;
    R_COMPILER_CHECK(r_frontend_dump_link_plan(
                         context, &options, r_compiler_test_write, &link_plan) == R_FRONTEND_OK);
    R_COMPILER_CHECK(strstr(link_plan.bytes, "(unit \"compiler.alpha\")") != NULL);
    R_COMPILER_CHECK(strstr(link_plan.bytes, "(unit \"compiler.beta\")") != NULL);
    R_COMPILER_CHECK(strstr(link_plan.bytes, "sha256=\"") != NULL);
    R_COMPILER_CHECK(strstr(link_plan.bytes,
                            "cc14fb50fe9d8a266b6de4c1d7b2d0728fd33149e9c2ecac46339901a0eed5a8") !=
                     NULL);
    R_COMPILER_CHECK(strstr(link_plan.bytes,
                            "c18fbf192f8697e91444b95581c52428956c16e66c17d27767529a3ecee80c7b") !=
                     NULL);
    R_COMPILER_CHECK(strstr(link_plan.bytes, "(unit \"compiler.alpha\")") <
                     strstr(link_plan.bytes, "(unit \"compiler.beta\")"));

    options.entry = "compiler.beta::main";
    R_COMPILER_CHECK(r_frontend_dump_bundle(context, &options, r_compiler_test_write, &bundle) ==
                     R_FRONTEND_OK);
    R_COMPILER_CHECK(strstr(bundle.bytes, "(section interface") != NULL);
    R_COMPILER_CHECK(strstr(bundle.bytes, "(section mir") != NULL);
    R_COMPILER_CHECK(strstr(bundle.bytes, "(section link-plan") != NULL);

    options.profile = "unknown";
    R_COMPILER_CHECK(
        r_frontend_dump_link_plan(context, &options, r_compiler_test_write, &rejected) ==
        R_FRONTEND_INVALID_ARGUMENT);
    R_COMPILER_CHECK(rejected.calls == 0U);
    options.profile = "hosted";
    options.entry = "compiler.beta::missing";
    R_COMPILER_CHECK(
        r_frontend_dump_link_plan(context, &options, r_compiler_test_write, &rejected) ==
        R_FRONTEND_NOT_LOWERABLE);
    R_COMPILER_CHECK(rejected.calls == 0U);

    r_compiler_test_buffer_destroy(&link_plan);
    r_compiler_test_buffer_destroy(&bundle);
    r_compiler_test_buffer_destroy(&rejected);
    r_frontend_destroy(context);
}

static void r_compiler_test_checked_link_libraries(void) {
    static const char source[] = "module compiler.checked;\n"
                                 "i32 convert(i64 value) throws std.convert::range_error {\n"
                                 "    i32 converted = std.convert::checked_i32(value);\n"
                                 "    return converted;\n"
                                 "}\n"
                                 "c_int convert_c(i32 value) throws std.convert::range_error {\n"
                                 "    c_int converted = std.c::checked_c_int(value);\n"
                                 "    return converted;\n"
                                 "}\n"
                                 "i32 main() {\n"
                                 "    return 0;\n"
                                 "}\n";
    static const char convert_library[] =
        "(library module=\"std.convert\" target=\"r_std_convert\")";
    static const char c_library[] = "(library module=\"std.c\" target=\"r_std_c\")";
    RFrontendContext *context = r_frontend_create(NULL);
    RFrontendArtifactOptions options;
    RCompilerTestBuffer link_plan = {0};
    RSourceId source_id = R_SOURCE_ID_INVALID;
    const char *convert_position;
    const char *c_position;
    bool prepared;

    R_COMPILER_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    if (r_frontend_add_source(
            context, "checked.r", (const uint8_t *)source, strlen(source), &source_id) !=
        R_FRONTEND_OK) {
        R_COMPILER_CHECK(false);
        r_frontend_destroy(context);
        return;
    }
    prepared = r_compiler_prepare(context);
    R_COMPILER_CHECK(prepared);
    if (!prepared) {
        r_frontend_destroy(context);
        return;
    }
    (void)memset(&options, 0, sizeof(options));
    options.entry = "compiler.checked::main";
    options.profile = "hosted";
    R_COMPILER_CHECK(r_frontend_dump_link_plan(
                         context, &options, r_compiler_test_write, &link_plan) == R_FRONTEND_OK);
    convert_position = strstr(link_plan.bytes, convert_library);
    c_position = strstr(link_plan.bytes, c_library);
    R_COMPILER_CHECK(convert_position != NULL);
    R_COMPILER_CHECK(c_position != NULL);
    R_COMPILER_CHECK((convert_position != NULL) && (c_position != NULL) &&
                     (convert_position < c_position));
    R_COMPILER_CHECK((convert_position != NULL) &&
                     (strstr(convert_position + strlen(convert_library), convert_library) == NULL));
    R_COMPILER_CHECK((c_position != NULL) &&
                     (strstr(c_position + strlen(c_library), c_library) == NULL));

    r_compiler_test_buffer_destroy(&link_plan);
    r_frontend_destroy(context);
}

static void r_compiler_test_import_index(void) {
    static const char source[] = "module query.root;\n"
                                 "import query.dep::{Item};\n"
                                 "i32 main() { return 0; }\n";
    RFrontendContext *context = r_frontend_create(NULL);
    RSourceId source_id = R_SOURCE_ID_INVALID;

    R_COMPILER_CHECK(context != NULL);
    if (context == NULL) {
        return;
    }
    R_COMPILER_CHECK(r_frontend_add_source(
                         context, "query.r", (const uint8_t *)source, strlen(source), &source_id) ==
                     R_FRONTEND_OK);
    R_COMPILER_CHECK(r_frontend_scan_interface(context, source_id) == R_FRONTEND_OK);
    R_COMPILER_CHECK(strcmp(r_frontend_source_module_name(context, source_id), "query.root") == 0);
    R_COMPILER_CHECK(r_frontend_source_import_count(context, source_id) == 1U);
    R_COMPILER_CHECK(strcmp(r_frontend_source_import_module(context, source_id, 0U), "query.dep") ==
                     0);
    R_COMPILER_CHECK(r_frontend_source_import_module(context, source_id, 1U) == NULL);
    r_frontend_destroy(context);
}

static RFrontendContext *r_compiler_make_oom_program(RCompilerTestAllocator *allocator) {
    static const char source[] = "module compiler.oom;\n"
                                 "i32 main() {\n"
                                 "    i32 value = 0;\n"
                                 "    while (value < 2) {\n"
                                 "        value += 1;\n"
                                 "    }\n"
                                 "    return value;\n"
                                 "}\n";
    RFrontendOptions options = r_frontend_default_options();
    RFrontendContext *context;
    RSourceId source_id = R_SOURCE_ID_INVALID;

    options.allocate = r_compiler_test_allocate;
    options.free = r_compiler_test_free;
    options.allocator_user_data = allocator;
    context = r_frontend_create(&options);
    if (context == NULL) {
        return NULL;
    }
    if ((r_frontend_add_source(
             context, "oom.r", (const uint8_t *)source, strlen(source), &source_id) !=
         R_FRONTEND_OK) ||
        !r_compiler_prepare_hir(context)) {
        r_frontend_destroy(context);
        return NULL;
    }
    return context;
}

static void r_compiler_test_mir_allocation_failures(void) {
    RCompilerTestAllocator baseline_allocator = {0};
    RFrontendContext *baseline = r_compiler_make_oom_program(&baseline_allocator);
    size_t before_lower;
    size_t allocation_count;
    size_t fail_offset;

    R_COMPILER_CHECK(baseline != NULL);
    if (baseline == NULL) {
        return;
    }
    before_lower = baseline_allocator.call_count;
    R_COMPILER_CHECK(r_frontend_lower_mir(baseline) == R_FRONTEND_OK);
    allocation_count = baseline_allocator.call_count - before_lower;
    R_COMPILER_CHECK(allocation_count != 0U);
    r_frontend_destroy(baseline);
    R_COMPILER_CHECK(baseline_allocator.live_count == 0U);

    for (fail_offset = 1U; fail_offset <= allocation_count; ++fail_offset) {
        RCompilerTestAllocator allocator = {0};
        RFrontendContext *context = r_compiler_make_oom_program(&allocator);
        RFrontendStatus status;
        R_COMPILER_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        allocator.fail_at = allocator.call_count + fail_offset;
        status = r_frontend_lower_mir(context);
        R_COMPILER_CHECK(status == R_FRONTEND_OUT_OF_MEMORY);
        r_frontend_destroy(context);
        R_COMPILER_CHECK(allocator.live_count == 0U);
    }
}

static void r_compiler_test_generic_interface_permutation(void) {
    static const char api[] =
        "module generic.api;\n@generic<T: key> struct Box { T value; };\n@generic<T: key> u64 "
        "Box<T>::hash(const Box<T>* value) {\n    u64 result = core::hash(&value->value);\n    "
        "return result;\n}\n@generic<T: key> bool Box<T>::equal(const Box<T>* left, const Box<T>* "
        "right) {\n    bool result = core::key_equal(&left->value, &right->value);\n    return "
        "result;\n}\n@generic<T> error Failure { Value(T), Fields { T value; }, Empty, "
        "};\n@generic<T> T identity(T value) { return move value; }\n";
    static const char client[] =
        "module generic.client;\nimport generic.api::{Box, Failure, identity};\ni32 main() {\n    "
        "i32 value = identity(7);\n    bool flag = identity(true);\n    Box<i32> key = Box<i32> { "
        ".value = value };\n    u64 hash = core::hash(&key);\n    hash as void;\n    flag as "
        "void;\n    Failure<i32> problem = Failure<i32>::Fields { .value = value };\n    problem "
        "as void;\n    return 0;\n}\n";
    RCompilerTestBuffer interfaces[2] = {{0}}, programs[2] = {{0}};
    const RFrontendArtifactOptions options = {.profile = "hosted", .entry = "generic.client::main"};
    size_t order;
    for (order = 0U; order < 2U; ++order) {
        RFrontendContext *context = r_frontend_create(NULL);
        RSourceId id;
        size_t index;
        R_COMPILER_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        for (index = 0U; index < 2U; ++index) {
            const bool first = (index == order);
            const char *source = first ? api : client;
            R_COMPILER_CHECK(r_frontend_add_source(context,
                                                   first ? "api.r" : "client.r",
                                                   (const uint8_t *)source,
                                                   strlen(source),
                                                   &id) == R_FRONTEND_OK);
        }
        R_COMPILER_CHECK(r_compiler_prepare(context));
        R_COMPILER_CHECK(r_frontend_dump_interface(
                             context, &options, r_compiler_test_write, &interfaces[order]) ==
                         R_FRONTEND_OK);
        R_COMPILER_CHECK(
            r_frontend_emit_llvm(
                context, &options, R_FRONTEND_LLVM_IR, r_compiler_test_write, &programs[order]) ==
            R_FRONTEND_OK);
        r_frontend_destroy(context);
    }
    if (interfaces[0].bytes != NULL && interfaces[1].bytes != NULL) {
        R_COMPILER_CHECK(strcmp(interfaces[0].bytes, interfaces[1].bytes) == 0);
        R_COMPILER_CHECK(
            strstr(interfaces[0].bytes, "(function-schema name=\"generic.api::identity\"") != NULL);
        R_COMPILER_CHECK(strstr(interfaces[0].bytes,
                                "(apply \"generic.api\"::\"Box\" (parameter \"T\"))") != NULL);
        R_COMPILER_CHECK(strstr(interfaces[0].bytes, "layout=tagged-union") != NULL);
        R_COMPILER_CHECK(strstr(interfaces[0].bytes, "source_required=true") != NULL);
        R_COMPILER_CHECK(strstr(interfaces[0].bytes, "effects=key-contract") != NULL);
        R_COMPILER_CHECK(strstr(interfaces[0].bytes, "payload=(parameter \"T\")") != NULL);
        R_COMPILER_CHECK(strstr(interfaces[0].bytes, "source-dependency module=\"generic.api\"") !=
                         NULL);
    }
    if (programs[0].bytes != NULL && programs[1].bytes != NULL) {
        R_COMPILER_CHECK(strcmp(programs[0].bytes, programs[1].bytes) == 0);
    }
    for (order = 0U; order < 2U; ++order) {
        r_compiler_test_buffer_destroy(&interfaces[order]);
        r_compiler_test_buffer_destroy(&programs[order]);
    }
}

static void r_compiler_test_json_interface_permutation(void) {
    static const char api[] = "module json.api;\nprotected i32 factory() { return 7; }\n"
                              "@generic<T: json_encode & json_decode> struct Box { T value; "
                              "@json(optional, default = factory) i32 version; "
                              "@json(optional, omitnone) o<i32> extra; };\n";
    static const char client[] =
        "module json.client;\nimport json.api::{Box};\ni32 main() {\n"
        "try { Box<std.string::string> value = "
        "std.json::unmarshal(\"{\\\"value\\\":\\\"owned\\\"}\"); "
        "std.string::string text = std.json::marshal(&value); drop text; return 0; } "
        "catch (std.json::error error) { return 1; } "
        "catch (std.alloc::alloc_error error) { return 2; } }\n";
    RCompilerTestBuffer interfaces[2] = {{0}}, programs[2] = {{0}};
    const RFrontendArtifactOptions options = {.profile = "hosted", .entry = "json.client::main"};
    size_t order;
    for (order = 0U; order < 2U; ++order) {
        RFrontendContext *context = r_frontend_create(NULL);
        RSourceId id;
        size_t index;
        R_COMPILER_CHECK(context != NULL);
        if (context == NULL) {
            continue;
        }
        for (index = 0U; index < 2U; ++index) {
            const bool first = (index == order);
            const char *source = first ? api : client;
            R_COMPILER_CHECK(r_frontend_add_source(context,
                                                   first ? "api.r" : "client.r",
                                                   (const uint8_t *)source,
                                                   strlen(source),
                                                   &id) == R_FRONTEND_OK);
        }
        R_COMPILER_CHECK(r_compiler_prepare(context));
        R_COMPILER_CHECK(r_frontend_dump_interface(
                             context, &options, r_compiler_test_write, &interfaces[order]) ==
                         R_FRONTEND_OK);
        R_COMPILER_CHECK(
            r_frontend_emit_llvm(
                context, &options, R_FRONTEND_LLVM_IR, r_compiler_test_write, &programs[order]) ==
            R_FRONTEND_OK);
        RCompilerTestBuffer link = {0};
        R_COMPILER_CHECK(r_frontend_dump_link_plan(
                             context, &options, r_compiler_test_write, &link) == R_FRONTEND_OK);
        if (link.bytes != NULL) {
            R_COMPILER_CHECK(strstr(link.bytes, "r_std_json") != NULL);
            R_COMPILER_CHECK(strstr(link.bytes, "r_std_fs") == NULL);
            R_COMPILER_CHECK(strstr(link.bytes, "r_std_net") == NULL);
            R_COMPILER_CHECK(strstr(link.bytes, "r_std_io") == NULL);
        }
        r_compiler_test_buffer_destroy(&link);
        r_frontend_destroy(context);
    }
    if (interfaces[0].bytes != NULL && interfaces[1].bytes != NULL) {
        R_COMPILER_CHECK(strcmp(interfaces[0].bytes, interfaces[1].bytes) == 0);
        R_COMPILER_CHECK(strstr(interfaces[0].bytes, "json_definition_sha256=") != NULL);
        R_COMPILER_CHECK(
            strstr(interfaces[0].bytes, "default_factory=(name=\"json.api::factory\"") != NULL);
        R_COMPILER_CHECK(strstr(interfaces[0].bytes, "json_encode json_decode") != NULL);
        R_COMPILER_CHECK(strstr(interfaces[0].bytes, "flags=(optional omitnone)") != NULL);
    }
    if (programs[0].bytes != NULL && programs[1].bytes != NULL) {
        R_COMPILER_CHECK(strcmp(programs[0].bytes, programs[1].bytes) == 0);
    }
    for (order = 0U; order < 2U; ++order) {
        r_compiler_test_buffer_destroy(&interfaces[order]);
        r_compiler_test_buffer_destroy(&programs[order]);
    }
}

static void r_compiler_test_json_dependency_fingerprint(void) {
    static const char *const schemas[] = {
        "struct Child { @json(name = \"old\") i32 value; }; enum Kind { Zero, One, };",
        "struct Child { @json(name = \"new\") i32 value; }; enum Kind { Zero, One, };",
        "struct Child { @json(name = \"old\") i32 value; }; enum Kind { Zero, Two, };",
        "struct Child { @json(name = \"old\") i32 value; }; enum Kind { Zero = 1, One, };",
    };
    char hashes[4][65] = {{0}};
    for (size_t variant = 0U; variant < 4U; ++variant) {
        char source[1024];
        int length = snprintf(source,
                              sizeof(source),
                              "module json.dependency; %s "
                              "struct Parent { @json(embed) Child child; array<Kind> kinds; "
                              "array<Child> children; }; "
                              "i32 main() { return 0; }",
                              schemas[variant]);
        RFrontendContext *context = r_frontend_create(NULL);
        RCompilerTestBuffer interface = {0};
        RSourceId id;
        const RFrontendArtifactOptions options = {.profile = "hosted",
                                                  .entry = "json.dependency::main"};
        R_COMPILER_CHECK(context != NULL && length > 0 && (size_t)length < sizeof(source));
        if (context == NULL)
            continue;
        R_COMPILER_CHECK(
            r_frontend_add_source(
                context, "dependency.r", (const uint8_t *)source, (size_t)length, &id) ==
            R_FRONTEND_OK);
        R_COMPILER_CHECK(r_compiler_prepare(context));
        R_COMPILER_CHECK(
            r_frontend_dump_interface(context, &options, r_compiler_test_write, &interface) ==
            R_FRONTEND_OK);
        const char *parent =
            interface.bytes == NULL
                ? NULL
                : strstr(interface.bytes, "(aggregate name=\"json.dependency::Parent\"");
        const char *fingerprint =
            parent == NULL ? NULL : strstr(parent, "json_definition_sha256=\"");
        R_COMPILER_CHECK(fingerprint != NULL);
        if (fingerprint != NULL) {
            fingerprint += strlen("json_definition_sha256=\"");
            R_COMPILER_CHECK(strlen(fingerprint) >= 64U);
            if (strlen(fingerprint) >= 64U)
                memcpy(hashes[variant], fingerprint, 64U);
        }
        r_compiler_test_buffer_destroy(&interface);
        r_frontend_destroy(context);
    }
    for (size_t i = 1U; i < 4U; ++i)
        R_COMPILER_CHECK(strcmp(hashes[0], hashes[i]) != 0);
}

int main(void) {
    r_compiler_test_json_dependency_fingerprint();
    r_compiler_test_json_interface_permutation();
    r_compiler_test_generic_interface_permutation();
    r_compiler_test_mir_and_determinism();
    r_compiler_test_artifacts();
    r_compiler_test_checked_link_libraries();
    r_compiler_test_import_index();
    r_compiler_test_mir_allocation_failures();
    if (failures != 0) {
        (void)fprintf(stderr, "%d compiler interface checks failed\n", failures);
        return 1;
    }
    (void)printf("compiler_interface_ok\n");
    return 0;
}

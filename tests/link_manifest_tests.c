#include "link_manifest.h"

#include "r_frontend.h"
#include "r_std_c.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define R_TEST_CHECK(expression)                                                                   \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            (void)fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, #expression); \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

typedef struct RTestManifestEntries {
    char names[4][256];
    size_t lengths[4];
    bool available[4];
    size_t count;
} RTestManifestEntries;

typedef struct RTestBuffer {
    char *bytes;
    size_t length;
    size_t capacity;
    size_t calls;
} RTestBuffer;

typedef struct RTestAllocator {
    size_t calls;
    size_t fail_at;
    size_t live;
} RTestAllocator;

static bool r_test_write(void *user_data, const char *bytes, size_t length) {
    RTestBuffer *buffer = user_data;
    size_t required;
    size_t capacity;
    char *replacement;

    buffer->calls += 1U;
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

static void r_test_buffer_destroy(RTestBuffer *buffer) {
    free(buffer->bytes);
    (void)memset(buffer, 0, sizeof(*buffer));
}

static void *r_test_allocate(void *user_data, size_t size) {
    RTestAllocator *allocator = user_data;
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

static void r_test_free(void *user_data, void *pointer) {
    RTestAllocator *allocator = user_data;

    if (pointer != NULL) {
        if (allocator->live != 0U) {
            allocator->live -= 1U;
        }
        free(pointer);
    }
}

static RFrontendContext *r_test_frontend(const char *source, RTestAllocator *allocator) {
    RFrontendOptions options = r_frontend_default_options();
    RFrontendContext *context;
    RSourceId source_id = R_SOURCE_ID_INVALID;

    if (allocator != NULL) {
        options.allocate = r_test_allocate;
        options.free = r_test_free;
        options.allocator_user_data = allocator;
    }
    context = r_frontend_create(allocator == NULL ? NULL : &options);
    if ((context == NULL) ||
        (r_frontend_add_source(
             context, "link_available.r", (const uint8_t *)source, strlen(source), &source_id) !=
         R_FRONTEND_OK) ||
        (source_id == R_SOURCE_ID_INVALID)) {
        r_frontend_destroy(context);
        return NULL;
    }
    return context;
}

static bool
r_test_has_diagnostic(const RFrontendContext *context, const char *code, const char *rule_id) {
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

static bool r_test_collect_entry(void *user_data, RLinkManifestEntryView entry) {
    RTestManifestEntries *entries = user_data;

    if ((entries->count >= 4U) || (entry.logical_name_length >= 256U)) {
        return false;
    }
    (void)memcpy(entries->names[entries->count], entry.logical_name, entry.logical_name_length);
    entries->names[entries->count][entry.logical_name_length] = '\0';
    entries->lengths[entries->count] = entry.logical_name_length;
    entries->available[entries->count] = entry.available;
    entries->count += 1U;
    return true;
}

static int r_test_valid_manifest(void) {
    static const uint8_t manifest[] =
        "{\n"
        "  \"schema\": \"r-link-manifest-0.1\",\n"
        "  \"links\": [\n"
        "    {\"logical_name\": \"apple\\u002edispatch\", \"available\": true},\n"
        "    {\"logical_name\": \"optional-zlib\", \"available\": false},\n"
        "    {\"logical_name\": \"system_libc\", \"kind\": \"system\",\n"
        "     \"an_unknown_manifest_extension_key_longer_than_thirty_two_bytes\": true}\n"
        "  ],\n"
        "  \"metadata\": {\"ignored\": [1, true, null, \"value\"]}\n"
        "}";
    RTestManifestEntries entries = {0};
    size_t count = 0U;

    R_TEST_CHECK(r_link_manifest_visit(
                     manifest, sizeof(manifest) - 1U, r_test_collect_entry, &entries, &count) ==
                 R_LINK_MANIFEST_VISIT_OK);
    R_TEST_CHECK(count == 3U);
    R_TEST_CHECK(entries.count == count);
    R_TEST_CHECK(strcmp(entries.names[0], "apple.dispatch") == 0);
    R_TEST_CHECK(entries.available[0]);
    R_TEST_CHECK(strcmp(entries.names[1], "optional-zlib") == 0);
    R_TEST_CHECK(!entries.available[1]);
    R_TEST_CHECK(strcmp(entries.names[2], "system_libc") == 0);
    R_TEST_CHECK(entries.available[2]);
    return 0;
}

typedef struct RTestManifestSymbols {
    char entries[8][64];
    char identifiers[8][64];
    RLinkSymbolKind kinds[8];
    bool weak[8];
    size_t count;
} RTestManifestSymbols;

static bool r_test_collect_symbol(void *user_data,
                                  const RLinkManifestEntryView *entry,
                                  RLinkManifestSymbolView symbol) {
    RTestManifestSymbols *symbols = user_data;

    if ((symbols->count >= 8U) || (entry->logical_name_length >= 64U) ||
        (symbol.c_identifier_length >= 64U)) {
        return false;
    }
    (void)memcpy(symbols->entries[symbols->count], entry->logical_name, entry->logical_name_length);
    symbols->entries[symbols->count][entry->logical_name_length] = '\0';
    (void)memcpy(
        symbols->identifiers[symbols->count], symbol.c_identifier, symbol.c_identifier_length);
    symbols->identifiers[symbols->count][symbol.c_identifier_length] = '\0';
    symbols->kinds[symbols->count] = symbol.kind;
    symbols->weak[symbols->count] = symbol.weak;
    symbols->count += 1U;
    return true;
}

typedef struct RTestManifestKinds {
    RLinkKind kinds[4];
    bool implicit[4];
    size_t count;
} RTestManifestKinds;

static bool r_test_collect_kind(void *user_data, RLinkManifestEntryView entry) {
    RTestManifestKinds *kinds = user_data;

    if (kinds->count >= 4U) {
        return false;
    }
    kinds->kinds[kinds->count] = entry.kind;
    kinds->implicit[kinds->count] = entry.implicit_c_runtime;
    kinds->count += 1U;
    return true;
}

static int r_test_symbol_inventory(void) {
    static const uint8_t manifest[] =
        "{\"links\":[\n"
        "  {\"symbols\":[{\"c_identifier\":\"probe_increment\"},\n"
        "               {\"kind\":\"data\",\"c_identifier\":\"probe_counter\",\n"
        "                \"binding\":\"weak\",\"provider\":\"probe\"}],\n"
        "   \"logical_name\":\"probe\",\"kind\":\"static\"},\n"
        "  {\"logical_name\":\"system.libc\",\"kind\":\"system\",\"implicit_c_runtime\":true,\n"
        "   \"symbols\":[]},\n"
        "  {\"logical_name\":\"bare\"}\n"
        "]}";
    RTestManifestSymbols symbols = {0};
    RTestManifestKinds kinds = {0};
    size_t count = 0U;
    RLinkKind kind = R_LINK_KIND_UNSPECIFIED;

    /* The entry and symbol visitors share one user_data; use the symbol collector alone. */
    R_TEST_CHECK(
        r_link_manifest_visit_with_symbols(
            manifest, sizeof(manifest) - 1U, NULL, r_test_collect_symbol, &symbols, &count) ==
        R_LINK_MANIFEST_VISIT_OK);
    R_TEST_CHECK(count == 3U);
    R_TEST_CHECK(symbols.count == 2U);
    R_TEST_CHECK(strcmp(symbols.entries[0], "probe") == 0);
    R_TEST_CHECK(strcmp(symbols.identifiers[0], "probe_increment") == 0);
    R_TEST_CHECK(symbols.kinds[0] == R_LINK_SYMBOL_FUNCTION);
    R_TEST_CHECK(!symbols.weak[0]);
    R_TEST_CHECK(strcmp(symbols.identifiers[1], "probe_counter") == 0);
    R_TEST_CHECK(symbols.kinds[1] == R_LINK_SYMBOL_DATA);
    R_TEST_CHECK(symbols.weak[1]);
    R_TEST_CHECK(r_link_manifest_visit(
                     manifest, sizeof(manifest) - 1U, r_test_collect_kind, &kinds, &count) ==
                 R_LINK_MANIFEST_VISIT_OK);
    R_TEST_CHECK(kinds.count == 3U);
    R_TEST_CHECK(kinds.kinds[0] == R_LINK_KIND_STATIC);
    R_TEST_CHECK(!kinds.implicit[0]);
    R_TEST_CHECK(kinds.kinds[1] == R_LINK_KIND_SYSTEM);
    R_TEST_CHECK(kinds.implicit[1]);
    R_TEST_CHECK(kinds.kinds[2] == R_LINK_KIND_UNSPECIFIED);
    R_TEST_CHECK(r_link_manifest_kind_from_text((const uint8_t *)"framework", 9U, &kind));
    R_TEST_CHECK(kind == R_LINK_KIND_FRAMEWORK);
    R_TEST_CHECK(!r_link_manifest_kind_from_text((const uint8_t *)"shared", 6U, &kind));
    R_TEST_CHECK(strcmp(r_link_manifest_kind_name(R_LINK_KIND_DYNAMIC), "dynamic") == 0);
    R_TEST_CHECK(r_link_manifest_logical_name_is_valid((const uint8_t *)"system.libc", 11U));
    R_TEST_CHECK(!r_link_manifest_logical_name_is_valid((const uint8_t *)"System", 6U));
    return 0;
}

typedef struct RTestManifestDefinitions {
    char entries[8][64];
    char definitions[8][64];
    size_t count;
} RTestManifestDefinitions;

static bool r_test_collect_definition(void *user_data,
                                      const RLinkManifestEntryView *entry,
                                      const uint8_t *definition,
                                      size_t definition_length) {
    RTestManifestDefinitions *definitions = user_data;

    if ((definitions->count >= 8U) || (entry->logical_name_length >= 64U) ||
        (definition_length >= 64U)) {
        return false;
    }
    (void)memcpy(
        definitions->entries[definitions->count], entry->logical_name, entry->logical_name_length);
    definitions->entries[definitions->count][entry->logical_name_length] = '\0';
    (void)memcpy(definitions->definitions[definitions->count], definition, definition_length);
    definitions->definitions[definitions->count][definition_length] = '\0';
    definitions->count += 1U;
    return true;
}

static int r_test_feature_definitions(void) {
    static const uint8_t manifest[] =
        "{\"links\":[\n"
        "  {\"logical_name\":\"probe\",\"feature_test_definitions\":[\"PROBE_MODE=1\",\n"
        "   \"_POSIX_C_SOURCE=200809L\",\"PROBE_TRACE\"]},\n"
        "  {\"logical_name\":\"bare\",\"feature_test_definitions\":[]}\n"
        "]}";
    static const char duplicate_definitions[] =
        "{\"links\":[{\"logical_name\":\"probe\",\"feature_test_definitions\":[],"
        "\"feature_test_definitions\":[]}]}";
    static const char *const invalid[] = {
        "{\"links\":[{\"logical_name\":\"probe\",\"feature_test_definitions\":[\"1BAD\"]}]}",
        "{\"links\":[{\"logical_name\":\"probe\",\"feature_test_definitions\":[\"NAME=\"]}]}",
        "{\"links\":[{\"logical_name\":\"probe\",\"feature_test_definitions\":[\"A=\\n\"]}]}",
        "{\"links\":[{\"logical_name\":\"probe\",\"feature_test_definitions\":[1]}]}",
        duplicate_definitions,
    };
    RTestManifestDefinitions definitions = {0};
    RLinkManifestVisitors visitors = {0};
    size_t count = 0U;
    size_t index;

    visitors.definition = r_test_collect_definition;
    R_TEST_CHECK(r_link_manifest_visit_full(
                     manifest, sizeof(manifest) - 1U, &visitors, &definitions, &count) ==
                 R_LINK_MANIFEST_VISIT_OK);
    R_TEST_CHECK(count == 2U);
    R_TEST_CHECK(definitions.count == 3U);
    R_TEST_CHECK(strcmp(definitions.entries[0], "probe") == 0);
    R_TEST_CHECK(strcmp(definitions.definitions[0], "PROBE_MODE=1") == 0);
    R_TEST_CHECK(strcmp(definitions.definitions[1], "_POSIX_C_SOURCE=200809L") == 0);
    R_TEST_CHECK(strcmp(definitions.definitions[2], "PROBE_TRACE") == 0);
    for (index = 0U; index < (sizeof(invalid) / sizeof(invalid[0])); ++index) {
        RTestManifestDefinitions rejected = {0};

        R_TEST_CHECK(r_link_manifest_visit_full((const uint8_t *)invalid[index],
                                                strlen(invalid[index]),
                                                &visitors,
                                                &rejected,
                                                NULL) == R_LINK_MANIFEST_VISIT_INVALID);
    }
    return 0;
}

static int r_test_invalid_symbol_inventories(void) {
    static const char *const invalid[] = {
        "{\"links\":[{\"logical_name\":\"probe\",\"kind\":\"shared\"}]}",
        "{\"links\":[{\"logical_name\":\"probe\",\"implicit_c_runtime\":1}]}",
        "{\"links\":[{\"logical_name\":\"probe\",\"symbols\":[{}]}]}",
        "{\"links\":[{\"logical_name\":\"probe\",\"symbols\":[{\"c_identifier\":\"1bad\"}]}]}",
        "{\"links\":[{\"logical_name\":\"probe\",\"symbols\":[{\"c_identifier\":\"ok\","
        "\"kind\":\"code\"}]}]}",
        "{\"links\":[{\"logical_name\":\"probe\",\"symbols\":[{\"c_identifier\":\"ok\","
        "\"binding\":\"common\"}]}]}",
        "{\"links\":[{\"logical_name\":\"probe\",\"symbols\":[{\"c_identifier\":\"ok\","
        "\"c_identifier\":\"twice\"}]}]}",
        "{\"links\":[{\"logical_name\":\"probe\",\"symbols\":[],\"symbols\":[]}]}",
    };
    size_t index;

    for (index = 0U; index < (sizeof(invalid) / sizeof(invalid[0])); ++index) {
        RTestManifestSymbols symbols = {0};

        R_TEST_CHECK(r_link_manifest_visit_with_symbols((const uint8_t *)invalid[index],
                                                        strlen(invalid[index]),
                                                        NULL,
                                                        r_test_collect_symbol,
                                                        &symbols,
                                                        NULL) == R_LINK_MANIFEST_VISIT_INVALID);
    }
    return 0;
}

static int r_test_invalid_manifests(void) {
    static const char *const invalid[] = {
        "{}",
        "{\"links\":{}}",
        "{\"links\":[{}]}",
        "{\"links\":[{\"logical_name\":\"Invalid\"}]}",
        "{\"links\":[{\"logical_name\":\"a..b\"}]}",
        "{\"links\":[{\"logical_name\":\"ok\",\"available\":null}]}",
        "{\"links\":[],\"links\":[]}",
        "{\"links\":[]} trailing",
        "{\"links\":[],\"bad\":\"\xc0\x80\"}",
    };
    size_t index;

    R_TEST_CHECK(r_link_manifest_visit(NULL, 0U, NULL, NULL, NULL) == R_LINK_MANIFEST_VISIT_OK);
    for (index = 0U; index < (sizeof(invalid) / sizeof(invalid[0])); ++index) {
        R_TEST_CHECK(
            r_link_manifest_visit(
                (const uint8_t *)invalid[index], strlen(invalid[index]), NULL, NULL, NULL) ==
            R_LINK_MANIFEST_VISIT_INVALID);
    }
    return 0;
}

static int r_test_frontend_contract(void) {
    static const char source[] = "module test.link_available;\n"
                                 "bool query(constexpr str logical_name) {\n"
                                 "    bool available = std.c::link_available(logical_name);\n"
                                 "    return available;\n"
                                 "}\n"
                                 "i32 main() {\n"
                                 "    bool available = query(\"system.libc\");\n"
                                 "    if (available == true) {\n"
                                 "        return 0;\n"
                                 "    } else {\n"
                                 "        return 1;\n"
                                 "    }\n"
                                 "}\n";
    static const uint8_t manifest[] = "{\"links\":["
                                      "{\"logical_name\":\"zeta\",\"available\":false},"
                                      "{\"logical_name\":\"alpha\",\"available\":true}]}";
    static const uint8_t reverse_manifest[] = "{\"links\":["
                                              "{\"logical_name\":\"alpha\",\"available\":true},"
                                              "{\"logical_name\":\"zeta\",\"available\":false}]}";
    static const uint8_t duplicate_manifest[] =
        "{\"links\":["
        "{\"logical_name\":\"alpha\",\"available\":true},"
        "{\"logical_name\":\"alpha\",\"available\":false}]}";
    static const uint8_t malformed_manifest[] = "{}";
    RFrontendContext *context = r_test_frontend(source, NULL);
    RFrontendArtifactOptions options = {0};
    RTestBuffer hir = {0};
    RTestBuffer mir = {0};
    RTestBuffer interface = {0};
    RTestBuffer generated = {0};
    RTestBuffer reverse_generated = {0};
    RTestBuffer empty_generated = {0};
    RTestBuffer rejected = {0};
    const char *alpha;
    const char *zeta;

    R_TEST_CHECK(context != NULL);
    R_TEST_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_TEST_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
    R_TEST_CHECK(r_frontend_dump_hir(context, r_test_write, &hir) == R_FRONTEND_OK);
    R_TEST_CHECK(r_frontend_dump_mir(context, r_test_write, &mir) == R_FRONTEND_OK);
    R_TEST_CHECK(strstr(hir.bytes, "operation=std.c::link_available type=bool") != NULL);
    R_TEST_CHECK(strstr(hir.bytes, "input=constexpr_str") != NULL);
    R_TEST_CHECK(strstr(mir.bytes, "operation=std.c::link_available") != NULL);

    options.profile = "hosted";
    options.link_manifest = manifest;
    options.link_manifest_length = sizeof(manifest) - 1U;
    R_TEST_CHECK(r_frontend_dump_interface(context, &options, r_test_write, &interface) ==
                 R_FRONTEND_OK);
    R_TEST_CHECK(strstr(interface.bytes, "test.link_available::query") != NULL);
    R_TEST_CHECK(strstr(interface.bytes, "constexpr_str") != NULL);
    R_TEST_CHECK(strstr(interface.bytes, "RStdCLinkManifest") == NULL);
    R_TEST_CHECK(
        r_frontend_emit_llvm(context, &options, R_FRONTEND_LLVM_IR, r_test_write, &generated) ==
        R_FRONTEND_OK);
    R_TEST_CHECK(generated.calls == 1U);
    R_TEST_CHECK(strstr(generated.bytes, "@r_std_c_link_available(") != NULL);
    /* R-SLIB-C-0005: the resolved facts are a table in the program, sorted by logical name so
       that the order of the manifest does not change the output. */
    R_TEST_CHECK(strstr(generated.bytes,
                        "@r_link_manifest = private constant { ptr, i64 } "
                        "{ ptr @r_link_manifest_entries, i64 2 }") != NULL);
    alpha = strstr(generated.bytes, "c\"alpha\"");
    zeta = strstr(generated.bytes, "c\"zeta\"");
    R_TEST_CHECK((alpha != NULL) && (zeta != NULL) && (alpha < zeta));

    options.link_manifest = reverse_manifest;
    options.link_manifest_length = sizeof(reverse_manifest) - 1U;
    R_TEST_CHECK(r_frontend_emit_llvm(
                     context, &options, R_FRONTEND_LLVM_IR, r_test_write, &reverse_generated) ==
                 R_FRONTEND_OK);
    R_TEST_CHECK(reverse_generated.length == generated.length);
    R_TEST_CHECK((reverse_generated.length == generated.length) &&
                 (memcmp(reverse_generated.bytes, generated.bytes, generated.length) == 0));

    options.link_manifest = NULL;
    options.link_manifest_length = 0U;
    R_TEST_CHECK(r_frontend_emit_llvm(
                     context, &options, R_FRONTEND_LLVM_IR, r_test_write, &empty_generated) ==
                 R_FRONTEND_OK);
    R_TEST_CHECK(strstr(empty_generated.bytes,
                        "@r_link_manifest = private constant { ptr, i64 } zeroinitializer") !=
                 NULL);

    options.link_manifest = duplicate_manifest;
    options.link_manifest_length = sizeof(duplicate_manifest) - 1U;
    R_TEST_CHECK(
        r_frontend_emit_llvm(context, &options, R_FRONTEND_LLVM_IR, r_test_write, &rejected) ==
        R_FRONTEND_INVALID_ARGUMENT);
    R_TEST_CHECK(rejected.calls == 0U);
    options.link_manifest = malformed_manifest;
    options.link_manifest_length = sizeof(malformed_manifest) - 1U;
    R_TEST_CHECK(
        r_frontend_emit_llvm(context, &options, R_FRONTEND_LLVM_IR, r_test_write, &rejected) ==
        R_FRONTEND_INVALID_ARGUMENT);
    R_TEST_CHECK(rejected.calls == 0U);
    options.link_manifest = NULL;
    options.link_manifest_length = 1U;
    R_TEST_CHECK(
        r_frontend_emit_llvm(context, &options, R_FRONTEND_LLVM_IR, r_test_write, &rejected) ==
        R_FRONTEND_INVALID_ARGUMENT);
    R_TEST_CHECK(rejected.calls == 0U);

    r_test_buffer_destroy(&hir);
    r_test_buffer_destroy(&mir);
    r_test_buffer_destroy(&interface);
    r_test_buffer_destroy(&generated);
    r_test_buffer_destroy(&reverse_generated);
    r_test_buffer_destroy(&empty_generated);
    r_test_buffer_destroy(&rejected);
    r_frontend_destroy(context);
    return 0;
}

static int r_test_frontend_negative_contract(void) {
    static const struct {
        const char *source;
        const char *name;
    } cases[] = {
        {
            "module test.link_arity_zero;\n"
            "bool query() {\n"
            "    bool value = std.c::link_available();\n"
            "    return value;\n"
            "}\n",
            "arity_zero",
        },
        {
            "module test.link_arity_two;\n"
            "bool query() {\n"
            "    bool value = std.c::link_available(\"alpha\", \"zeta\");\n"
            "    return value;\n"
            "}\n",
            "arity_two",
        },
        {
            "module test.link_runtime_str;\n"
            "bool query(str logical_name) {\n"
            "    bool value = std.c::link_available(logical_name);\n"
            "    return value;\n"
            "}\n",
            "runtime_str",
        },
        {
            "module test.link_result_type;\n"
            "i32 query() {\n"
            "    i32 value = std.c::link_available(\"alpha\");\n"
            "    return value;\n"
            "}\n",
            "result_type",
        },
    };
    size_t index;

    for (index = 0U; index < (sizeof(cases) / sizeof(cases[0])); ++index) {
        RFrontendContext *context = r_test_frontend(cases[index].source, NULL);

        (void)cases[index].name;
        R_TEST_CHECK(context != NULL);
        R_TEST_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
        R_TEST_CHECK(r_test_has_diagnostic(context, "R-DIAG-TYPE-001", "R-SLIB-C-0005"));
        r_frontend_destroy(context);
    }
    return 0;
}

static int r_test_frontend_allocation_failures(void) {
    static const char source[] = "module test.link_allocation;\n"
                                 "i32 main() {\n"
                                 "    bool available = std.c::link_available(\"alpha\");\n"
                                 "    if (available == true) {\n"
                                 "        return 0;\n"
                                 "    } else {\n"
                                 "        return 1;\n"
                                 "    }\n"
                                 "}\n";
    static const uint8_t manifest[] =
        "{\"links\":[{\"logical_name\":\"alpha\",\"available\":true}]}";
    RTestAllocator allocator = {0};
    RFrontendContext *context = r_test_frontend(source, &allocator);
    RFrontendArtifactOptions options = {0};
    RTestBuffer baseline = {0};
    size_t live_baseline;
    size_t allocation_start;
    size_t emission_allocations;
    size_t offset;

    R_TEST_CHECK(context != NULL);
    R_TEST_CHECK(r_frontend_analyze(context) == R_FRONTEND_OK);
    R_TEST_CHECK(r_frontend_lower_mir(context) == R_FRONTEND_OK);
    options.link_manifest = manifest;
    options.link_manifest_length = sizeof(manifest) - 1U;
    live_baseline = allocator.live;
    allocation_start = allocator.calls;
    R_TEST_CHECK(
        r_frontend_emit_llvm(context, &options, R_FRONTEND_LLVM_IR, r_test_write, &baseline) ==
        R_FRONTEND_OK);
    emission_allocations = allocator.calls - allocation_start;
    R_TEST_CHECK(emission_allocations != 0U);
    R_TEST_CHECK(allocator.live == live_baseline);

    for (offset = 1U; offset <= emission_allocations; ++offset) {
        RTestBuffer failed = {0};

        allocator.fail_at = allocator.calls + offset;
        R_TEST_CHECK(
            r_frontend_emit_llvm(context, &options, R_FRONTEND_LLVM_IR, r_test_write, &failed) ==
            R_FRONTEND_OUT_OF_MEMORY);
        R_TEST_CHECK(failed.calls == 0U);
        R_TEST_CHECK(allocator.live == live_baseline);
        r_test_buffer_destroy(&failed);
    }
    allocator.fail_at = 0U;
    {
        RTestBuffer retry = {0};

        R_TEST_CHECK(
            r_frontend_emit_llvm(context, &options, R_FRONTEND_LLVM_IR, r_test_write, &retry) ==
            R_FRONTEND_OK);
        R_TEST_CHECK(retry.length == baseline.length);
        R_TEST_CHECK((retry.length == baseline.length) &&
                     (memcmp(retry.bytes, baseline.bytes, baseline.length) == 0));
        R_TEST_CHECK(allocator.live == live_baseline);
        r_test_buffer_destroy(&retry);
    }
    r_test_buffer_destroy(&baseline);
    r_frontend_destroy(context);
    R_TEST_CHECK(allocator.live == 0U);
    return 0;
}

int main(void) {
    R_TEST_CHECK(r_test_valid_manifest() == 0);
    R_TEST_CHECK(r_test_invalid_manifests() == 0);
    R_TEST_CHECK(r_test_symbol_inventory() == 0);
    R_TEST_CHECK(r_test_invalid_symbol_inventories() == 0);
    R_TEST_CHECK(r_test_feature_definitions() == 0);
    R_TEST_CHECK(r_test_frontend_contract() == 0);
    R_TEST_CHECK(r_test_frontend_negative_contract() == 0);
    R_TEST_CHECK(r_test_frontend_allocation_failures() == 0);
    (void)fprintf(stdout, "link_manifest_tests: ok\n");
    return 0;
}

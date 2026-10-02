#include "frontend_internal.h"
#include "named_standard_copy_abi.h"

#include <stdio.h>
#include <string.h>

static int r_test_source(const char *source, RFrontendStatus expected) {
    RFrontendContext *context = r_frontend_create(NULL);
    RSourceId source_id = R_SOURCE_ID_INVALID;
    RFrontendStatus status;
    size_t index;
    int failed = 0;

    if (context == NULL) {
        return 1;
    }
    status = r_frontend_add_source(
        context, "hash-type.r", (const uint8_t *)source, strlen(source), &source_id);
    if (status == R_FRONTEND_OK) {
        status = r_frontend_analyze(context);
    }
    if (status != expected) {
        (void)fprintf(stderr, "unexpected status %d for:\n%s\n", (int)status, source);
        for (index = 0U; index < r_frontend_diagnostic_count(context); ++index) {
            const RDiagnostic *diagnostic = r_frontend_diagnostic(context, index);
            (void)fprintf(stderr, "%s: %s\n", diagnostic->code, diagnostic->message);
        }
        failed = 1;
    }
    if (status == R_FRONTEND_OK) {
        for (index = 0U; index < context->hir_node_count; ++index) {
            const RHirNode *node = &context->hir_nodes[index];
            if ((node->kind == R_HIR_STANDARD_CALL) &&
                (node->standard_operation >= R_STANDARD_CALL_HASH_CRC32) &&
                (node->standard_operation <= R_STANDARD_CALL_HASH_SHA512) &&
                (!r_semantic_type_is_send(context, node->type) ||
                 !r_semantic_type_is_sync(context, node->type))) {
                (void)fprintf(stderr, "hash result is not Send+Sync\n");
                failed = 1;
            }
        }
    }
    r_frontend_destroy(context);
    return failed;
}

int main(void) {
    static const char *const algorithms[] = {"crc32", "md5", "sha1", "sha256", "sha512"};
    char source[1024];
    size_t index;
    int failures = 0;

    /* The generated registry is searched by binary search. Every record must remain findable. */
    for (index = 0U; index < r_named_standard_copy_abi_count(); ++index) {
        const RNamedStandardCopyAbi *record = r_named_standard_copy_abi_at(index);
        if (r_named_standard_copy_abi_find(record->r_name, record->r_name_length) != record) {
            (void)fprintf(stderr, "unreachable Copy ABI record: %s\n", record->r_name);
            failures += 1;
        }
    }
    for (index = 0U; index < sizeof(algorithms) / sizeof(algorithms[0]); ++index) {
        const char *algorithm = algorithms[index];

        (void)snprintf(source,
                       sizeof(source),
                       "module test.hash; void test() { std.bytes::%s(\"abc\") as void; }",
                       algorithm);
        failures += r_test_source(source, R_FRONTEND_INVALID_SOURCE);
        (void)snprintf(source,
                       sizeof(source),
                       "module test.hash; void test() { std.hash::%s() as void; }",
                       algorithm);
        failures += r_test_source(source, R_FRONTEND_INVALID_SOURCE);
        (void)snprintf(
            source,
            sizeof(source),
            "module test.hash; void test(i32[2] source) { std.hash::%s(source) as void; }",
            algorithm);
        failures += r_test_source(source, R_FRONTEND_INVALID_SOURCE);
        if (index != 0U) {
            (void)snprintf(
                source,
                sizeof(source),
                "module test.hash; void test(std.bytes::%s_digest source) { source as void; }",
                algorithm);
            failures += r_test_source(source, R_FRONTEND_INVALID_SOURCE);
            (void)snprintf(source,
                           sizeof(source),
                           "module test.hash; void test(const u8[] source) {"
                           " std.hash::%s_digest digest = std.hash::%s(source);"
                           " std.hash::%s_digest copy = digest; digest as void; copy as void; }",
                           algorithm,
                           algorithm,
                           algorithm);
            failures += r_test_source(source, R_FRONTEND_OK);
        }
    }
    failures += r_test_source("module test.hash; u32 test(const u8[] source) { u32 value = "
                              "std.hash::crc32(source); return value; }",
                              R_FRONTEND_OK);
    return failures == 0 ? 0 : 1;
}

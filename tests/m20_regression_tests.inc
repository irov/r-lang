/* M20 (docs/language-completeness-roadmap.ru.md): std.random::fill, the C operation of
   std.random (Library R-SLIB-RANDOM-0001): one mutable byte view and no result. Each case is
   accepted, or is rejected with the diagnostic code, the rule it names and a part of its
   message. */
static void r_semantic_test_m20_regressions(void) {
    static const struct {
        const char *name;
        const char *source;
        const char *diagnostic;
        const char *message;
    } cases[] = {
        {"fill-fixed-array",
         "u8 f() { u8[8] b = {}; std.random::fill(&b); return b[0]; }",
         NULL,
         NULL},
        {"fill-owned-bytes",
         "usize f() throws std.alloc::alloc_error { bytes b = std.alloc::bytes(4usize, 0u8); "
         "std.random::fill(b.as_slice_mut()); return len(b); }",
         NULL,
         NULL},
        {"fill-shared-view",
         "void f(const u8[] b) { std.random::fill(b); }",
         "R-DIAG-TYPE-001 [R-SLIB-RANDOM-0001]",
         NULL},
        {"fill-arity",
         "void f() { u8[2] b = {}; std.random::fill(&b, &b); }",
         "R-DIAG-TYPE-001 [R-SLIB-RANDOM-0001]",
         NULL},
        {"fill-result",
         "u8 f() { u8[2] b = {}; u8 x = std.random::fill(&b); return x; }",
         "R-DIAG-TYPE-001",
         NULL},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char source[8192];
        const int length =
            snprintf(source, sizeof(source), "module test.m20_regressions; %s", cases[i].source);
        RFrontendContext *context = r_frontend_create(NULL);
        R_SEMANTIC_CHECK(context != NULL && length > 0 && (size_t)length < sizeof(source));
        if (context == NULL)
            return;
        (void)r_semantic_add_bytes(context, cases[i].name, (const uint8_t *)source, (size_t)length);
        RFrontendStatus status = r_frontend_analyze(context);
        if (status == R_FRONTEND_OK) {
            status = r_frontend_lower_mir(context);
            if ((status == R_FRONTEND_NOT_LOWERABLE) && (context->diagnostic_count != 0U)) {
                status = R_FRONTEND_INVALID_SOURCE;
            }
        }
        bool message_found = cases[i].message == NULL;
        for (size_t d = 0U; d < context->diagnostic_count; ++d) {
            if ((cases[i].message != NULL) &&
                (strstr(context->diagnostics[d].message, cases[i].message) != NULL)) {
                message_found = true;
            }
        }
        const bool valid =
            status == (cases[i].diagnostic == NULL ? R_FRONTEND_OK : R_FRONTEND_INVALID_SOURCE) &&
            (cases[i].diagnostic == NULL ||
             r_semantic_l14_diagnostic_count(context, cases[i].diagnostic) != 0U) &&
            message_found && r_semantic_diagnostic_code_count(context, "R-DIAG-SLICE-001") == 0U;
        if (!valid) {
            (void)fprintf(stderr, "m20 regression %s status %d\n", cases[i].name, (int)status);
            for (size_t d = 0U; d < context->diagnostic_count; ++d)
                (void)fprintf(stderr,
                              "%s [%s]: %s\n",
                              context->diagnostics[d].code,
                              context->diagnostics[d].rule_id,
                              context->diagnostics[d].message);
        }
        R_SEMANTIC_CHECK(valid);
        r_frontend_destroy(context);
    }
}

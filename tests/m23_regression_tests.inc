/* M23 (docs/language-completeness-roadmap.ru.md): the typing of std.async::task_id
   (Library R-SLIB-ASYNC-0018), which reads the running task without allocating or waiting and
   so keeps @noalloc and @nonblocking provable. Each case is accepted, or is rejected with the
   diagnostic code, the rule it names and a part of its message. The R-source modules std.log,
   std.args and std.config are covered by the codegen cases library_log, library_args and
   library_config, and M23-1 by regression_async_checked_conversion. */
static void r_semantic_test_m23_regressions(void) {
    static const struct {
        const char *name;
        const char *source;
        const char *diagnostic;
        const char *message;
    } cases[] = {
        {"task-id-sync",
         "u64 f() { return std.async::task_id(); }",
         NULL,
         NULL},
        {"task-id-async",
         "async u64 f() { u64 id = std.async::task_id(); return id; }",
         NULL,
         NULL},
        {"task-id-noalloc-nonblocking",
         "@noalloc @nonblocking u64 f() { return std.async::task_id(); }",
         NULL,
         NULL},
        {"task-id-takes-no-argument",
         "u64 f() { return std.async::task_id(1u64); }",
         "R-DIAG-TYPE-001 [R-SLIB-ASYNC-0018]",
         "incorrect argument count"},
        {"task-id-is-u64",
         "u32 f() { return std.async::task_id(); }",
         "R-DIAG-TYPE-001 [R-SLIB-ASYNC-0018]",
         "does not match its destination type"},
        {"task-id-result-is-a-value",
         "void f() { std.async::task_id(); }",
         "R-DIAG-TYPE-001 [R-STMT-0001]",
         "requires a void expression"},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char source[8192];
        const int length =
            snprintf(source, sizeof(source), "module test.m23_regressions; %s", cases[i].source);
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
            (void)fprintf(stderr, "m23 regression %s status %d\n", cases[i].name, (int)status);
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

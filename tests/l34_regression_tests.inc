/* L34 (docs/language-completeness-roadmap.ru.md): budget blocks (R-STMT-0020). Each case is
 * accepted, or is rejected with the diagnostic code and the rule it names. The positive cases need
 * std.alloc::limits from the library map and live in the fixture codegen_budget_blocks.r; these
 * cover the contexts and the type of the block and the contextual keyword. */

static void r_semantic_test_l34_regressions(void) {
    static const struct {
        const char *name;
        const char *source;
        const char *diagnostic;
        const char *hir;
    } cases[] = {
        {"budget-sync",
         "void f() { budget (5) { } }",
         "R-DIAG-ASYNC-001 [R-STMT-0020]",
         NULL},
        {"budget-finally",
         "async void f() { try { } finally { budget (5) { } } }",
         "R-DIAG-ASYNC-001 [R-STMT-0020]",
         NULL},
        {"budget-integer",
         "async void f() { budget (5) { } }",
         "R-DIAG-TYPE-001 [R-STMT-0020]",
         NULL},
        {"budget-other-struct",
         "struct limits { o<usize> bytes; o<usize> tasks; }; async void f() { "
         "budget (limits {.bytes = o::none, .tasks = o::none}) { } }",
         "R-DIAG-TYPE-001 [R-STMT-0020]",
         NULL},
        {"budget-contextual-call",
         "i32 budget(i32 value) { return value; } i32 f() { i32 limit = budget(2); return limit; }",
         NULL,
         NULL},
        {"budget-contextual-local",
         "async i32 f() { i32 budget = 3; return budget; }",
         NULL,
         NULL},
        {"budget-exhausted-variants",
         "i32 f(std.alloc::alloc_error a, std.async::start_error s) { if (a == "
         "std.alloc::alloc_error::budget_exhausted && s == "
         "std.async::start_error::budget_exhausted) { return 1; } return 0; }",
         NULL,
         NULL},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char source[4096];
        const bool named = strncmp(cases[i].source, "module ", 7U) == 0;
        const int length =
            named ? snprintf(source, sizeof(source), "%s", cases[i].source)
                  : snprintf(
                        source, sizeof(source), "module test.l34_regressions; %s", cases[i].source);
        RFrontendContext *context = r_frontend_create(NULL);
        RSemanticTestBuffer hir = {0};
        R_SEMANTIC_CHECK(context != NULL && length > 0 && (size_t)length < sizeof(source));
        if (context == NULL)
            return;
        (void)r_semantic_add_bytes(context, cases[i].name, (const uint8_t *)source, (size_t)length);
        RFrontendStatus status = r_frontend_analyze(context);
        bool hir_found = cases[i].hir == NULL;
        if (status == R_FRONTEND_OK && cases[i].hir != NULL) {
            hir_found =
                r_frontend_dump_hir(context, r_semantic_test_write, &hir) == R_FRONTEND_OK &&
                hir.bytes != NULL && strstr(hir.bytes, cases[i].hir) != NULL;
        }
        if (status == R_FRONTEND_OK) {
            status = r_frontend_lower_mir(context);
            if ((status == R_FRONTEND_NOT_LOWERABLE) && (context->diagnostic_count != 0U)) {
                status = R_FRONTEND_INVALID_SOURCE;
            }
        }
        const bool valid =
            status == (cases[i].diagnostic == NULL ? R_FRONTEND_OK : R_FRONTEND_INVALID_SOURCE) &&
            (cases[i].diagnostic == NULL ||
             r_semantic_l14_diagnostic_count(context, cases[i].diagnostic) != 0U) &&
            hir_found && r_semantic_diagnostic_code_count(context, "R-DIAG-SLICE-001") == 0U;
        if (!valid) {
            (void)fprintf(stderr,
                          "l34 regression %s status %d hir %d\n",
                          cases[i].name,
                          (int)status,
                          (int)hir_found);
            for (size_t d = 0U; d < context->diagnostic_count; ++d)
                (void)fprintf(stderr,
                              "%s [%s]: %s\n",
                              context->diagnostics[d].code,
                              context->diagnostics[d].rule_id,
                              context->diagnostics[d].message);
        }
        R_SEMANTIC_CHECK(valid);
        free(hir.bytes);
        r_frontend_destroy(context);
    }
}

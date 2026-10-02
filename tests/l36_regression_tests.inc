/* L36 (docs/language-completeness-roadmap.ru.md): asynchronous iteration (R-STMT-0021,
 * R-TYPE-0046 core::AsyncIterator). Each case is accepted, or is rejected with the diagnostic
 * code and the rule it names and, when given, a diagnostic whose message contains the text. The
 * cases run in the hosted-native-async profile; the run-time behaviour lives in the fixture
 * codegen_async_for.r. */

static void r_semantic_test_l36_regressions(void) {
    static const char countdown[] =
        "struct Countdown { u32 left; }; "
        "impl core::AsyncIterator for Countdown { type Item = u32; "
        "@scoped async o<u32> next(Countdown* this) throws std.error::fault { "
        "if (this->left == 0u32) { return o::none; } this->left -= 1u32; "
        "return o::some(this->left); } }; ";
    static const struct {
        const char *name;
        bool countdown;
        const char *source;
        const char *diagnostic;
        const char *message;
    } cases[] = {
        {"async-for-iterator",
         true,
         "async u32 f() throws std.error::fault { Countdown c = Countdown {.left = 3u32}; "
         "u32 total = 0u32; task_scope(1) g { for (u32 v in &c) { total += v; } } return total; }",
         NULL,
         NULL},
        {"async-for-receiver",
         false,
         "async u64 f(std.sync::receiver<u64> events) throws std.async::start_error { "
         "u64 sum = 0u64; for (u64 v in &events) { sum += v; } return sum; }",
         NULL,
         NULL},
        {"async-for-auto",
         false,
         "async u64 f(std.sync::receiver<u64> events) throws std.async::start_error { "
         "u64 sum = 0u64; for (auto v in &events) { sum += v; } return sum; }",
         NULL,
         NULL},
        {"async-for-sync-function",
         false,
         "u64 f(std.sync::receiver<u64>* events) { u64 sum = 0u64; "
         "for (u64 v in &*events) { sum += v; } return sum; }",
         "R-DIAG-ASYNC-001 [R-STMT-0021]",
         NULL},
        {"async-for-finally",
         false,
         "async void f(std.sync::receiver<u64> events) throws std.async::start_error { "
         "try { } finally { for (u64 v in &events) { v as void; } } }",
         "R-DIAG-ASYNC-001 [R-STMT-0021]",
         NULL},
        {"async-for-without-group",
         true,
         "async u32 f() throws std.error::fault { Countdown c = Countdown {.left = 3u32}; "
         "u32 total = 0u32; for (u32 v in &c) { total += v; } return total; }",
         "R-DIAG-ASYNC-001 [R-STMT-0017]",
         "task_scope"},
        {"async-for-field",
         false,
         "struct Holder { std.sync::receiver<u64> events; }; "
         "async u64 f(Holder holder) throws std.async::start_error { u64 sum = 0u64; "
         "for (u64 v in &holder.events) { sum += v; } return sum; }",
         "R-DIAG-TYPE-001 [R-STMT-0021]",
         NULL},
        {"async-for-item-type",
         false,
         "async u64 f(std.sync::receiver<u64> events) throws std.async::start_error { "
         "u64 sum = 0u64; for (u32 v in &events) { v as void; } return sum; }",
         "R-DIAG-TYPE-001 [R-STMT-0014]",
         NULL},
        {"async-for-by-value",
         false,
         "async u64 f(std.sync::receiver<u64> events) throws std.async::start_error { "
         "u64 sum = 0u64; for (u64 v in move events) { sum += v; } return sum; }",
         "R-DIAG-TYPE-001 [R-STMT-0014]",
         "std.sync::receiver"},
        {"async-for-start-error-undeclared",
         false,
         "async u64 f(std.sync::receiver<u64> events) { u64 sum = 0u64; "
         "for (u64 v in &events) { sum += v; } return sum; }",
         "R-DIAG-EFFECT-001 [R-ERR-0001]",
         NULL},
        {"async-iterator-not-scoped",
         false,
         "struct C { u32 left; }; impl core::AsyncIterator for C { type Item = u32; "
         "async o<u32> next(C* this) throws std.error::fault { return o::none; } };",
         "R-DIAG-ASYNC-001 [R-BORROW-0024]",
         NULL},
        {"async-iterator-synchronous",
         false,
         "struct C { u32 left; }; impl core::AsyncIterator for C { type Item = u32; "
         "o<u32> next(C* this) throws std.error::fault { return o::none; } };",
         "R-DIAG-TRAIT-001 [R-TYPE-0042]",
         NULL},
        {"async-iterator-other-throws",
         false,
         "error E { }; struct C { u32 left; }; impl core::AsyncIterator for C { type Item = u32; "
         "@scoped async o<u32> next(C* this) throws E { return o::none; } };",
         "R-DIAG-TRAIT-001 [R-TYPE-0042]",
         NULL},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char source[4096];
        const int length = snprintf(source,
                                    sizeof(source),
                                    "module test.l36_regressions; %s%s",
                                    cases[i].countdown ? countdown : "",
                                    cases[i].source);
        RFrontendContext *context = r_frontend_create(NULL);
        R_SEMANTIC_CHECK(context != NULL && length > 0 && (size_t)length < sizeof(source));
        if (context == NULL)
            return;
        R_SEMANTIC_CHECK(r_frontend_set_profile(context, "hosted-native-async") == R_FRONTEND_OK);
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
            message_found = message_found || ((context->diagnostics[d].message != NULL) &&
                                              (strstr(context->diagnostics[d].message,
                                                      cases[i].message) != NULL));
        }
        const bool valid =
            status == (cases[i].diagnostic == NULL ? R_FRONTEND_OK : R_FRONTEND_INVALID_SOURCE) &&
            (cases[i].diagnostic == NULL ||
             r_semantic_l14_diagnostic_count(context, cases[i].diagnostic) != 0U) &&
            message_found && r_semantic_diagnostic_code_count(context, "R-DIAG-SLICE-001") == 0U;
        if (!valid) {
            (void)fprintf(stderr, "l36 regression %s status %d\n", cases[i].name, (int)status);
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

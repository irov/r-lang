/* L24 (docs/language-completeness-roadmap.ru.md): asynchronous channel receive (Library R-LIB-0016)
 * and detached members of a task group (R-STMT-0017). Each case is accepted, or is rejected with
 * the diagnostic code and, where given, the rule it names, in the given profile or in the default
 * hosted-native-async profile; an accepted case may name text its HIR shall contain. */
static void r_semantic_test_l24_regressions(void) {
    static const struct {
        const char *name;
        const char *profile;
        const char *source;
        const char *diagnostic;
        const char *hir;
    } cases[] = {
        {"receive-function",
         NULL,
         "async i32 f(std.sync::receiver<i32> inbox) throws std.async::start_error { "
         "o<i32> value = await std.sync::receive(&inbox); i32 status = 0; switch (value) { "
         "case variant o::some(number): status = *number; break; case variant o::none: break; } "
         "return status; }",
         NULL,
         "operation=std.sync::receive"},
        {"receive-method-start",
         NULL,
         "task<o<i32>> f(const std.sync::receiver<i32>* inbox) throws std.async::start_error { "
         "return inbox->receive(); }",
         NULL,
         "operation=std.sync::receive"},
        {"receive-group-member",
         NULL,
         "async i32 f(std.sync::receiver<i32> inbox) throws std.async::start_error { i32 total = "
         "0; "
         "task_scope(2) group { auto first = inbox.receive(); auto second = inbox.receive(); "
         "o<i32> a = await move first; o<i32> b = await move second; switch (a) { case variant "
         "o::some(value): total += *value; break; case variant o::none: break; } switch (b) { "
         "case variant o::some(value): total += *value; break; case variant o::none: break; } } "
         "return total; }",
         NULL,
         NULL},
        {"receive-select",
         NULL,
         "async i32 f(std.sync::receiver<i32> inbox, std.time::instant limit) throws "
         "std.async::start_error { i32 status = 0; task_scope(1) group { auto next = "
         "inbox.receive(); select (group) { case o<i32> value = await move next: value as void; "
         "status = 1; break; case until (limit): status = 2; break; } } return status; }",
         NULL,
         NULL},
        {"receive-hosted-thread",
         "hosted-thread",
         "void f(const std.sync::receiver<i32>* inbox) { inbox->receive(); }",
         "R-DIAG-PROFILE-001",
         NULL},
        {"receive-borrowed-element",
         NULL,
         "void f(const std.sync::receiver<const i32*>* inbox) throws std.async::start_error { "
         "auto started = std.sync::receive(inbox); std.async::cancel(move started); }",
         "R-DIAG-ASYNC-001 [R-LIB-0016]",
         NULL},
        {"receive-borrowed-result",
         NULL,
         "void f(const std.sync::receiver<const i32*>* inbox) throws std.async::start_error { "
         "task<o<const i32*>> started = std.sync::receive(inbox); "
         "std.async::cancel(move started); }",
         "R-DIAG-ASYNC-001",
         NULL},
        {"receive-sender",
         NULL,
         "void f(const std.sync::sender<i32>* outbox) throws std.async::start_error { "
         "task<o<i32>> started = std.sync::receive(outbox); std.async::cancel(move started); }",
         "R-DIAG-TYPE-001 [R-LIB-0016]",
         NULL},
        {"receive-wrong-result",
         NULL,
         "void f(const std.sync::receiver<i32>* inbox) throws std.async::start_error { "
         "task<i32> started = std.sync::receive(inbox); std.async::cancel(move started); }",
         "R-DIAG-TYPE-001 [R-LIB-0016]",
         NULL},
        {"receive-arity",
         NULL,
         "void f(const std.sync::receiver<i32>* inbox) throws std.async::start_error { "
         "task<o<i32>> started = std.sync::receive(inbox, o::none); "
         "std.async::cancel(move started); }",
         "R-DIAG-TYPE-001 [R-LIB-0016]",
         NULL},
        {"detach-member",
         NULL,
         "async i32 g() { return 1; } async void f() throws std.async::start_error { "
         "task_scope(2) group { auto member = g(); std.async::detach(move member); "
         "await group.all(); } }",
         NULL,
         "operation=std.async::detach"},
        {"detach-member-in-loop",
         NULL,
         "async i32 g(i32 value) { return value; } async void f() throws std.async::start_error { "
         "task_scope(1) group { for (i32 index = 0; index < 3; index += 1) { auto member = "
         "g(index); std.async::detach(move member); await group.all(); } } }",
         NULL,
         NULL},
        {"detach-member-escape",
         NULL,
         "async i32 g() { return 1; } async task<i32> f() throws std.async::start_error { "
         "task_scope(2) group { auto member = g(); return move member; } }",
         "R-DIAG-ASYNC-001 [R-STMT-0017]",
         NULL},
        {"detach-member-inner-borrow",
         NULL,
         "@scoped async i32 g(const i32* value) { return *value; } async void f() throws "
         "std.async::start_error { task_scope(2) group { i32 local = 1; auto member = g(&local); "
         "std.async::detach(move member); } }",
         "R-DIAG-BORROW-002 [R-STMT-0017]",
         NULL},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char source[4096];
        const int length =
            snprintf(source, sizeof(source), "module test.l24_regressions; %s", cases[i].source);
        RFrontendContext *context = r_frontend_create(NULL);
        RSemanticTestBuffer hir = {0};
        R_SEMANTIC_CHECK(context != NULL && length > 0 && (size_t)length < sizeof(source));
        if (context == NULL)
            return;
        R_SEMANTIC_CHECK(cases[i].profile == NULL ||
                         r_frontend_set_profile(context, cases[i].profile) == R_FRONTEND_OK);
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
                          "l24 regression %s status %d hir %d\n",
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

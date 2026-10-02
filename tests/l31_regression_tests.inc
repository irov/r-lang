/* L31 (docs/language-completeness-roadmap.ru.md): std.async::blocking (R-SLIB-ASYNC-0017) takes
   its entry and arguments as an unscoped std.thread::spawn does and starts a task on the blocking
   call pool: the task carries the entry's result and checked errors, and the start throws
   std.async::start_error. Each case is accepted, or is rejected with the diagnostic code, the
   rule it names and a part of its message. */
static void r_semantic_test_l31_regressions(void) {
    static const char prelude[] =
        "struct Cell { i32 v; }; error Bad { i32 code; }; i64 sq(i64 v) { return v * v; } usize "
        "measure(std.string::string text) { return text.len(); } i32 checked(i32 v) throws Bad { "
        "throw (v < 0) Bad {.code = v}; return v; }";
    static const struct {
        const char *name;
        const char *source;
        const char *diagnostic;
        const char *message;
    } cases[] = {
        {"value",
         "async i64 f() throws std.async::start_error { return await std.async::blocking(sq, "
         "3i64); }",
         NULL,
         NULL},
        {"void-entry",
         "void nothing(i32 v) { v as void; } async void f() throws std.async::start_error { "
         "await std.async::blocking(nothing, 1); }",
         NULL,
         NULL},
        {"checked-entry",
         "async i32 f() throws std.async::start_error, Bad { return await "
         "std.async::blocking(checked, 1); }",
         NULL,
         NULL},
        {"move-argument",
         "async usize f() throws std.alloc::alloc_error, std.async::start_error { "
         "std.string::string t = std.string::from_str(\"abc\"); return await "
         "std.async::blocking(measure, move t); }",
         NULL,
         NULL},
        {"generic-entry",
         "@generic<T> T same(T value) { return move value; } async u8 f() throws "
         "std.async::start_error { return await std.async::blocking(same, 7u8); }",
         NULL,
         NULL},
        {"synchronous-start",
         "task<i64> start(i64 v) throws std.async::start_error { return std.async::blocking(sq, "
         "v); }",
         NULL,
         NULL},
        {"group-member",
         "async i64 f() throws std.async::start_error { i64 total = 0i64; task_scope(2) group { "
         "auto a = std.async::blocking(sq, 2i64); auto b = std.async::blocking(sq, 3i64); total "
         "+= (await move a) + (await move b); } return total; }",
         NULL,
         NULL},
        {"no-entry",
         "async void f() throws std.async::start_error { await std.async::blocking(); }",
         "R-DIAG-TYPE-001 [R-SLIB-ASYNC-0017]",
         "requires an entry designator"},
        {"indirect-entry",
         "async i64 f() throws std.async::start_error { return await std.async::blocking((sq), "
         "2i64); }",
         "R-DIAG-TYPE-001 [R-SLIB-ASYNC-0017]",
         "direct module-level R function name"},
        {"async-entry",
         "async i64 g(i64 v) { return v; } async i64 f() throws std.async::start_error { return "
         "await std.async::blocking(g, 2i64); }",
         "R-DIAG-TYPE-001 [R-SLIB-ASYNC-0017]",
         "safe R function"},
        {"unsafe-entry",
         "unsafe i64 g(i64 v) { return v; } async i64 f() throws std.async::start_error { return "
         "await std.async::blocking(g, 2i64); }",
         "R-DIAG-TYPE-001 [R-SLIB-ASYNC-0017]",
         "safe R function"},
        {"argument-count",
         "async i64 f() throws std.async::start_error { return await std.async::blocking(sq, "
         "2i64, 3i64); }",
         "R-DIAG-TYPE-001 [R-SLIB-ASYNC-0017]",
         "argument count"},
        {"non-send-argument",
         "i32 take(rc Cell c) { return c->v; } async i32 f() throws std.async::start_error { rc "
         "Cell c = new rc Cell {.v = 1}; return await std.async::blocking(take, move c); }",
         "R-DIAG-MEM-001 [R-SLIB-ASYNC-0017]",
         "argument type shall be Send"},
        {"non-send-result",
         "rc Cell make(i32 v) { return new rc Cell {.v = v}; } async void f() throws "
         "std.async::start_error { rc Cell c = await std.async::blocking(make, 1); c as void; }",
         "R-DIAG-MEM-001 [R-SLIB-ASYNC-0017]",
         "result type shall be Send"},
        {"local-borrow-argument",
         "i32 read(const i32* p) { return *p; } async i32 f() throws std.async::start_error { "
         "i32 x = 4; return await std.async::blocking(read, &x); }",
         "R-DIAG-BORROW-001 [R-SLIB-ASYNC-0017]",
         "program region"},
        {"borrow-result",
         "str pick() { return \"x\"; } async void f() throws std.async::start_error { str p = "
         "await std.async::blocking(pick); p as void; }",
         "R-DIAG-ASYNC-001 [R-SLIB-ASYNC-0017]",
         "hold no borrow"},
        {"move-temporary",
         "async usize f() throws std.alloc::alloc_error, std.async::start_error { return await "
         "std.async::blocking(measure, std.string::from_str(\"abc\")); }",
         "R-DIAG-MOVE-003 [R-SLIB-ASYNC-0017]",
         "direct form move name"},
        {"task-type-mismatch",
         "task<i32> start() throws std.async::start_error { return std.async::blocking(sq, "
         "1i64); }",
         "R-DIAG-TYPE-001 [R-SLIB-ASYNC-0017]",
         "destination task type"},
        {"start-error-unhandled",
         "async i64 f() { return await std.async::blocking(sq, 2i64); }",
         "R-DIAG-EFFECT-001",
         NULL},
        {"staged-overlap",
         "usize both(std.string::string a, usize n) { return a.len() + n; } async usize f() "
         "throws std.alloc::alloc_error, std.async::start_error { std.string::string t = "
         "std.string::from_str(\"abc\"); return await std.async::blocking(both, move t, t.len()); "
         "}",
         "R-DIAG-BORROW-001",
         NULL},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char source[8192];
        const int length = snprintf(
            source, sizeof(source), "module test.l31_regressions; %s %s", prelude, cases[i].source);
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
            (void)fprintf(stderr, "l31 regression %s status %d\n", cases[i].name, (int)status);
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

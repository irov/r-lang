/* L23 (docs/language-completeness-roadmap.ru.md): deadline blocks (R-STMT-0019) and the omitted
 * trailing deadline argument of standard operations (Library R-SLIB-ASYNC-0008). Each case is
 * accepted, or is rejected with the diagnostic code and, where given, the rule it names; an
 * accepted case may name text its HIR shall contain. A source that begins with `module` keeps its
 * own module name. */
#define R_L23_DEADLINE_OPTION "(variant type=(option (standard \"std.time::instant\")) tag=0)"

static void r_semantic_test_l23_regressions(void) {
    static const struct {
        const char *name;
        const char *source;
        const char *diagnostic;
        const char *hir;
    } cases[] = {
        {"deadline-block",
         "async void f(std.time::instant when) { deadline (when) { } }",
         NULL,
         "operation=std.async::deadline_leave"},
        {"deadline-option",
         "async void f(o<std.time::instant> when) { deadline (when) { } deadline (o::none) { } }",
         NULL,
         "operation=std.async::deadline_enter"},
        {"deadline-await-return",
         "async i32 g() { return 1; } async i32 f(std.time::instant when) throws "
         "std.async::start_error { deadline (when) { i32 value = await g(); deadline (when) { "
         "return value; } } }",
         NULL,
         NULL},
        {"deadline-group",
         "async i32 g() { return 1; } async i32 f(std.time::instant when) "
         "throws std.async::start_error { i32 total = 0; task_scope(1) group { deadline (when) { "
         "auto member = g(); i32 value = await move member; if (value == 1) { total = value; } } "
         "} return total; }",
         NULL,
         NULL},
        {"deadline-in-module-named-deadline",
         "module test.deadline; async void f(std.time::instant when) { deadline (when) { } }",
         NULL,
         NULL},
        {"deadline-sync",
         "void f(std.time::instant when) { deadline (when) { } }",
         "R-DIAG-ASYNC-001 [R-STMT-0019]",
         NULL},
        {"deadline-finally",
         "async void f(std.time::instant when) { try { } finally { deadline (when) { } } }",
         "R-DIAG-ASYNC-001 [R-STMT-0019]",
         NULL},
        {"deadline-integer",
         "async void f() { deadline (5) { } }",
         "R-DIAG-TYPE-001 [R-STMT-0019]",
         NULL},
        {"deadline-duration",
         "async void f() { deadline (std.time::duration_from_seconds(1i64)) { } }",
         "R-DIAG-TYPE-001 [R-STMT-0019]",
         NULL},
        {"deadline-contextual-call",
         "i32 deadline(i32 value) { return value; } i32 f() { i32 limit = deadline(2); "
         "return limit; }",
         NULL,
         NULL},
        {"deadline-contextual-local",
         "async i32 f() { i32 deadline = 3; return deadline; }",
         NULL,
         NULL},
        {"omitted-deadline-function",
         "void f(const std.fs::path* path) throws std.async::start_error { "
         "task<array<u8> throws std.fs::fs_error> started = std.fs::read_file(path, 16usize); "
         "std.async::cancel(move started); }",
         NULL,
         R_L23_DEADLINE_OPTION},
        {"omitted-deadline-method",
         "async void f(std.net::socket_address local) throws std.net::net_error, "
         "std.async::start_error { std.net::udp_socket socket = await local.bind(false); "
         "await (move socket).close(); }",
         NULL,
         R_L23_DEADLINE_OPTION},
        {"omitted-deadline-io",
         "void f(const std.io::output* out) throws std.async::start_error { "
         "task<void throws std.io::io_error> started = std.io::flush(out); "
         "std.async::cancel(move started); }",
         NULL,
         R_L23_DEADLINE_OPTION},
        {"omitted-deadline-and-limit",
         "void f(const std.fs::path* path) throws std.async::start_error { "
         "task<array<u8> throws std.fs::fs_error> started = std.fs::read_file(path); "
         "std.async::cancel(move started); }",
         "R-DIAG-TYPE-001",
         NULL},
        /* L23-1: the fields of a caught filesystem error need no other filesystem operation. */
        {"fs-error-fields",
         "async i32 f(std.fs::path path) throws std.async::start_error { try { array<u8> data = "
         "await std.fs::read_file(&path, 16usize); return len(data) as i32; } catch "
         "(std.fs::fs_error failure) { if (failure.code == std.fs::error_code::timed_out) { "
         "return 1; } return 2; } }",
         NULL,
         NULL},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char source[4096];
        const bool named = strncmp(cases[i].source, "module ", 7U) == 0;
        const int length =
            named ? snprintf(source, sizeof(source), "%s", cases[i].source)
                  : snprintf(
                        source, sizeof(source), "module test.l23_regressions; %s", cases[i].source);
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
                          "l23 regression %s status %d hir %d\n",
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

#undef R_L23_DEADLINE_OPTION

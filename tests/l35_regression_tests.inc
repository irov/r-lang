/* L35 (docs/language-completeness-roadmap.ru.md): bounded recursion (R-FUNC-0026, R-FUNC-0004).
 * Each case is accepted, or is rejected with the diagnostic code and the rule it names and, when
 * given, a diagnostic whose message contains the text. The run-time behaviour lives in the
 * fixture codegen_recursion_bounds.r; these cover the attribute, where it may appear and which
 * call cycles it permits. */

static void r_semantic_test_l35_regressions(void) {
    static const struct {
        const char *name;
        const char *source;
        const char *diagnostic;
        const char *message;
    } cases[] = {
        {"recursion-self",
         "@recursion(depth = 8) u32 f(u32 n) throws core::recursion_error { if (n == 0u32) { "
         "return 0u32; } return f(n - 1u32); }",
         NULL,
         NULL},
        {"recursion-mutual",
         "@recursion(depth = 2) bool odd(u32 n) throws core::recursion_error; "
         "@recursion(depth = 2) bool even(u32 n) throws core::recursion_error { if (n == 0u32) { "
         "return true; } return odd(n - 1u32); } "
         "@recursion(depth = 2) bool odd(u32 n) throws core::recursion_error { if (n == 0u32) { "
         "return false; } return even(n - 1u32); }",
         NULL,
         NULL},
        {"recursion-without-attribute",
         "u32 f(u32 n) { if (n == 0u32) { return 0u32; } return f(n - 1u32); }",
         "R-DIAG-STACK-001 [R-FUNC-0004]",
         "recursive call chain: f -> f"},
        {"recursion-unmarked-member",
         "u32 h(u32 n) throws core::recursion_error; "
         "@recursion(depth = 2) u32 g(u32 n) throws core::recursion_error { if (n == 0u32) { "
         "return 0u32; } u32 a = g(n - 1u32); u32 b = h(n - 1u32); return a + b; } "
         "u32 h(u32 n) throws core::recursion_error { if (n == 0u32) { return 0u32; } "
         "return g(n - 1u32); }",
         "R-DIAG-STACK-001 [R-FUNC-0004]",
         "h"},
        {"recursion-only-marked-cycle-in-forbidden-set",
         "u32 h(u32 n) throws core::recursion_error; "
         "@recursion(depth = 2) u32 b(u32 n) throws core::recursion_error; "
         "@recursion(depth = 2) u32 a(u32 n) throws core::recursion_error { if (n == 0u32) { "
         "return 0u32; } return b(n - 1u32); } "
         "@recursion(depth = 2) u32 b(u32 n) throws core::recursion_error { if (n == 0u32) { "
         "return 0u32; } u32 x = a(n - 1u32); u32 y = h(n - 1u32); return x + y; } "
         "u32 h(u32 n) throws core::recursion_error { if (n == 0u32) { return 0u32; } "
         "return a(n - 1u32); }",
         "R-DIAG-STACK-001 [R-FUNC-0004]",
         "recursive call chain: h -> a -> b -> h"},
        {"recursion-function-value",
         "@recursion(depth = 2) u32 f(u32 n) throws core::recursion_error { if (n == 0u32) { "
         "return 0u32; } fn(u32) -> u32 throws(core::recursion_error) next = f; "
         "return next(n - 1u32); }",
         "R-DIAG-STACK-001 [R-FUNC-0004]",
         NULL},
        {"recursion-async",
         "@recursion(depth = 2) async u32 f(u32 n) throws core::recursion_error { return n; }",
         "R-DIAG-ASYNC-001 [R-FUNC-0026]",
         NULL},
        {"recursion-async-cycle",
         "async u32 f(u32 n) throws std.async::start_error { if (n == 0u32) { return 0u32; } "
         "u32 r = await f(n - 1u32); return r; }",
         "R-DIAG-STACK-001 [R-FUNC-0004]",
         NULL},
        {"recursion-missing-throws",
         "@recursion(depth = 2) u32 f(u32 n) { return n; }",
         "R-DIAG-EFFECT-001 [R-FUNC-0026]",
         "core::recursion_error"},
        {"recursion-depth-zero",
         "@recursion(depth = 0) u32 f(u32 n) throws core::recursion_error { return n; }",
         "R-DIAG-SYN-002 [R-GRAM-0007]",
         NULL},
        {"recursion-depth-too-large",
         "@recursion(depth = 65536) u32 f(u32 n) throws core::recursion_error { return n; }",
         "R-DIAG-SYN-002 [R-GRAM-0007]",
         NULL},
        {"recursion-depth-suffix",
         "@recursion(depth = 4u32) u32 f(u32 n) throws core::recursion_error { return n; }",
         "R-DIAG-SYN-002 [R-GRAM-0007]",
         NULL},
        {"recursion-no-depth",
         "@recursion u32 f(u32 n) throws core::recursion_error { return n; }",
         "R-DIAG-SYN-002 [R-GRAM-0007]",
         NULL},
        {"recursion-other-key",
         "@recursion(levels = 4) u32 f(u32 n) throws core::recursion_error { return n; }",
         "R-DIAG-SYN-002 [R-GRAM-0007]",
         NULL},
        {"recursion-twice",
         "@recursion(depth = 4) @recursion(depth = 4) u32 f(u32 n) throws "
         "core::recursion_error { return n; }",
         "R-DIAG-SYN-002 [R-GRAM-0007]",
         NULL},
        {"recursion-extern-c",
         "extern \"C\" { @recursion(depth = 2) i32 r_native(i32 value); }",
         "R-DIAG-FFI-003 [R-FFI-0050]",
         NULL},
        {"recursion-prototype-mismatch",
         "@recursion(depth = 3) u32 f(u32 n) throws core::recursion_error; "
         "@recursion(depth = 4) u32 f(u32 n) throws core::recursion_error { return n; }",
         "R-DIAG-NAME-002 [R-NAME-0004]",
         NULL},
        {"recursion-noalloc",
         "@noalloc @nonblocking @recursion(depth = 4) u32 f(u32 n) throws "
         "core::recursion_error { if (n == 0u32) { return 0u32; } return f(n - 1u32); }",
         NULL,
         NULL},
        {"recursion-noalloc-member-allocates",
         "struct Cell { i32 value; }; "
         "@noalloc @recursion(depth = 4) u32 odd(u32 n) throws core::recursion_error; "
         "@noalloc @recursion(depth = 4) u32 even(u32 n) throws core::recursion_error { "
         "if (n == 0u32) { return 0u32; } return odd(n - 1u32); } "
         "@noalloc @recursion(depth = 4) u32 odd(u32 n) throws core::recursion_error { "
         "if (n == 0u32) { return 1u32; } own Cell*? cell = new Cell {.value = 5}; drop cell; "
         "return even(n - 1u32); }",
         "R-DIAG-RESOURCE-001 [R-FUNC-0019]",
         NULL},
        {"recursion-error-field",
         "usize f(core::recursion_error refused) { return refused.depth; }",
         NULL,
         NULL},
        {"recursion-error-catch",
         "@recursion(depth = 1) u32 f(u32 n) throws core::recursion_error { if (n == 0u32) { "
         "return 0u32; } return f(n - 1u32); } "
         "usize g() { try { u32 r = f(3u32); r as void; } catch (core::recursion_error e) { "
         "return e.depth; } return 0usize; }",
         NULL,
         NULL},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char source[4096];
        const int length =
            snprintf(source, sizeof(source), "module test.l35_regressions; %s", cases[i].source);
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
            (void)fprintf(stderr, "l35 regression %s status %d\n", cases[i].name, (int)status);
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

static void r_semantic_test_format(void) {
    static const struct {
        const char *body;
        const char *code;
    } cases[] = {
        {"void f(i32 x) throws std.alloc::alloc_error { std.string::string s = f\"{x}\"; drop s; }",
         NULL},
        {"void f() throws std.alloc::alloc_error { std.format::format t = f\"{1}\"; "
         "std.string::string a = t.format(1); std.string::string b = t.format(true); drop b; drop "
         "a; }",
         NULL},
        {"std.string::string f(i32 x) throws std.alloc::alloc_error { return f\"{x}\"; }", NULL},
        {"void f() { std.string::string s = f\"plain\"; }", "R-DIAG-EFFECT-001"},
        {"void f() throws std.alloc::alloc_error { std.string::string s = f\"{missing}\"; }",
         "R-DIAG-NAME-001"},
        {"void f() throws std.alloc::alloc_error { std.string::string s = f\"{0}\"; }",
         "R-DIAG-FORMAT-001"},
        {"void f() throws std.alloc::alloc_error { std.string::string s = f\"{1}\"; }",
         "R-DIAG-FORMAT-001"},
        {"void f() throws std.alloc::alloc_error { std.string::string s = f\"{2}\".format(1, 2); }",
         "R-DIAG-FORMAT-001"},
        {"void f() throws std.alloc::alloc_error { std.string::string s = f\"none\".format(1); }",
         "R-DIAG-FORMAT-001"},
        {"void f() throws std.alloc::alloc_error { std.string::string s = f\"{1:x}\".format(true); "
         "}",
         "R-DIAG-FORMAT-001"},
        {"void f() throws std.alloc::alloc_error { std.string::string s = f\"{1:.2}\".format(1); }",
         "R-DIAG-FORMAT-001"},
        {"void f() throws std.alloc::alloc_error { std.string::string s = f\"{1:d}\".format(1.5); "
         "}",
         "R-DIAG-FORMAT-001"},
        {"void f() throws std.alloc::alloc_error { std.string::string s = f\"{1:q}\".format(1); }",
         "R-DIAG-FORMAT-001"},
        {"void f() throws std.alloc::alloc_error { std.string::string s = "
         "f\"{1:18446744073709551616}\".format(1); }",
         "R-DIAG-FORMAT-001"},
        {"void f() throws std.alloc::alloc_error { std.string::string s = "
         "f\"{1:.18446744073709551616}\".format(1.5); }",
         "R-DIAG-FORMAT-001"},
        {"struct S { i32 x; }; void f(S s) throws std.alloc::alloc_error { std.string::string text "
         "= f\"{s}\"; }",
         "R-DIAG-FORMAT-001"},
        {"@generic<T: copy> void f(T value) throws std.alloc::alloc_error { std.string::string "
         "text "
         "= f\"{value}\"; }",
         "R-DIAG-FORMAT-001"},
        {"void f() throws std.alloc::alloc_error { std.format::format t = f\"plain\"; "
         "std.format::format copied = t; }",
         "R-DIAG-MOVE-001"},
        {"void f() throws std.alloc::alloc_error { std.format::format t = f\"plain\"; "
         "std.format::format moved = move t; std.string::string x = t.format(); }",
         "R-DIAG-MOVE-002"},
        {"void f() throws std.alloc::alloc_error { static std.format::format t = f\"plain\"; }",
         "R-DIAG-FORMAT-001"},
        {"void f(std.format::format value) {}", "R-DIAG-FORMAT-001"},
        {"struct S { std.format::format value; };", "R-DIAG-FORMAT-001"},
        {"void f() throws std.alloc::alloc_error { array<std.format::format> a = {}; }",
         "R-DIAG-FORMAT-001"},
        {"void f() throws std.alloc::alloc_error { std.string::string name = f\"name\"; "
         "std.string::string s = f\"{1}\".format(move name); }",
         "R-DIAG-FORMAT-001"},
        {"void f() throws std.alloc::alloc_error { str s = f\"name\"; }", "R-DIAG-TYPE-001"},
        {"void f() throws std.alloc::alloc_error { constexpr str s = f\"name\"; }",
         "R-DIAG-TYPE-001"},
        {"const i32 constant = 1; void f() throws std.alloc::alloc_error { std.string::string s = "
         "f\"{constant}\"; }",
         "R-DIAG-FORMAT-001"},
        {"void f() throws std.alloc::alloc_error { const std.format::format t = f\"{1}\"; "
         "std.string::string s = t.format(1); drop s; }",
         NULL},
    };
    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        char text[2048];
        int length = snprintf(text, sizeof(text), "module semantic.format; %s", cases[index].body);
        RFrontendContext *context = r_frontend_create(NULL);
        R_SEMANTIC_CHECK(context != NULL && length > 0 && (size_t)length < sizeof(text));
        if (context == NULL) {
            continue;
        }
        (void)r_semantic_add_bytes(context, "format.r", (const uint8_t *)text, (size_t)length);
        RFrontendStatus status = r_frontend_analyze(context);
        if (status == R_FRONTEND_OK) {
            status = r_frontend_lower_mir(context);
        }
        bool found = cases[index].code == NULL && status == R_FRONTEND_OK &&
                     r_frontend_diagnostic_count(context) == 0U;
        for (size_t diagnostic_index = 0U; diagnostic_index < r_frontend_diagnostic_count(context);
             ++diagnostic_index) {
            const RDiagnostic *diagnostic =
                r_frontend_diagnostic(context, (uint32_t)diagnostic_index);
            found = found ||
                    (cases[index].code != NULL && strcmp(diagnostic->code, cases[index].code) == 0);
        }
        if (!found) {
            (void)fprintf(stderr,
                          "format case %zu failed (status %d): %s\n",
                          index,
                          (int)status,
                          cases[index].body);
        }
        R_SEMANTIC_CHECK(found);
        r_frontend_destroy(context);
    }
}

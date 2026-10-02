static void r_test_compositional_syntax(void) {
    static const struct {
        const char *text;
        bool valid;
        const char *node;
    } cases[] = {
        {"async void f() { task_scope /* capacity */ (2) jobs { await jobs.all(); } }",
         true,
         "task_scope_statement"},
        {"async void f() { task_scope(2) jobs { await jobs.first(&a, &b); } }",
         true,
         "await_operation"},
        {"async void f() { task_scope(2 jobs { } }", false, NULL},
        {"async void f() { task_scope() jobs { } }", false, NULL},
        {"i32 f(o<o<i32>> x) { return match /* value */ (x) {"
         "case variant o::some(variant o::some(n)) if (n > 0): n; default: 0; }; }",
         true,
         "match_guard"},
        {"struct P { i32 a; }; i32 f(P x) { return match(x) { case { /* field */ .a=n, }: n; }; }",
         true,
         "match_field"},
        {"@generic<T> enum E { Some(T) }; @generic<T> T f(E<T> x) {"
         "return match(move x) { case variant E<T>::Some(move n): move n; }; }",
         true,
         "match_pattern"},
        {"i32 match(i32 x) { return x; } i32 f() { return match(match(42)); }",
         true,
         "call_suffix"},
        {"@generic<T: copy> trait Read { T read(const Self* this);"
         "T again(const Self* this) { return this->read(); } };"
         "@generic<T: copy> trait Source : Read<T> { type Item: Read<T> & copy; };",
         true,
         "trait_declaration"},
        {"opaque /* result */ (fn(i32) -> i32 & copy) factory(i32 n) {"
         "fn i32 add(i32 x) move(n) { return x+n; } return move add; }",
         true,
         "opaque_result"},
        {"async i32 f() { return add(await service.jobs::read(), await move work); }",
         true,
         "await_operation"},
        {"async void f() { task_scope(2) g { select /* group */ (g) { case i32 v = await move a: "
         "break; case await move b: continue; case until (deadline): return; } } }",
         true,
         "select_statement"},
        {"async void f() { task_scope(1) g { select (g) { case auto v = await move a: break; } } }",
         true,
         "select_clause"},
        {"async void f() { task_scope(1) g { select (g) { case until (d): break; } } }",
         true,
         "select_deadline"},
        {"i32 select(i32 x) { return x; } i32 f() { return select(1); }", true, "call_suffix"},
        {"async void f() { deadline /* until */ (when.later()) { await g(); } }",
         true,
         "deadline_statement"},
        {"async void f() { deadline (o::none) { task_scope(1) g { } } }",
         true,
         "deadline_statement"},
        {"i32 f() { return deadline(1); }", true, "call_suffix"},
        {"void f() { deadline (1); }", true, "expression_statement"},
        {"async void f() { deadline () { } }", false, NULL},
        {"async void f() { deadline (d) return; }", false, NULL},
        {"async void f() { task_scope(1) g { select (g) { default: break; } } }", false, NULL},
        {"async void f() { task_scope(1) g { select (g) { case await move a: } } }",
         true,
         "select_clause"},
        {"async void f() { task_scope(1) g { select (g) { case until (): break; } } }",
         false,
         NULL},
        {"async void f() { task_scope(1) g { select (g) { case i32 v = await move a break; } } }",
         false,
         NULL},
        {"i32 f(i32 x) { return match(x) {}; }", false, NULL},
        {"i32 f(i32 x) { return match(x) { case : 1; }; }", false, NULL},
        {"i32 f(i32 x) { return match(x) { case 0 1; default: 2; }; }", false, NULL},
        {"i32 f(i32 x) { return match(x) { case 0: ; default: 2; }; }", false, NULL},
        {"i32 f(i32 x) { return match(x) { case {.a = }: 1; default: 2; }; }", false, NULL},
        {"i32 f(i32 x) { return match(x) { case {.a n}: 1; default: 2; }; }", false, NULL},
        {"i32 f(i32 x) { return match(x) { case move: 1; default: 2; }; }", false, NULL},
        {"i32 f(i32 x) { return match(x) { case n if n > 0: n; default: 2; }; }", false, NULL},
        {"i32 f(i32 x) { return match(x) { case n if (): n; default: 2; }; }", false, NULL},
        {"opaque() factory() { return 1; }", false, NULL},
        {"opaque(copy &) factory() { return 1; }", false, NULL},
        {"opaque(fn(i32) i32) factory() { return 1; }", false, NULL},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char text[2048];
        const int length = snprintf(text,
                                    sizeof(text),
                                    "module syntax.composition; %s\n"
                                    "i32 after_recovery() { return 42; }\n",
                                    cases[i].text);
        RFrontendContext *context = r_frontend_create(NULL);
        RSyntaxNodeId cst;
        RAstNodeId ast;
        RTestBuffer dump = {0}, reconstructed = {0};
        R_TEST_CHECK(context != NULL && length > 0 && (size_t)length < sizeof(text));
        if (context == NULL)
            continue;
        const RSourceId source =
            r_test_add_source(context, "composition.r", (const uint8_t *)text, (size_t)length);
        const RFrontendStatus status = r_frontend_parse_cst(context, source, &cst);
        if (status != (cases[i].valid ? R_FRONTEND_OK : R_FRONTEND_INVALID_SOURCE)) {
            (void)fprintf(stderr, "composition syntax case %zu: status %d\n", i, (int)status);
        }
        R_TEST_CHECK(status == (cases[i].valid ? R_FRONTEND_OK : R_FRONTEND_INVALID_SOURCE));
        if (cases[i].valid && status == R_FRONTEND_OK) {
            R_TEST_CHECK(r_frontend_lower_ast(context, source, &ast) == R_FRONTEND_OK);
            R_TEST_CHECK(r_frontend_dump_ast(context, source, r_test_write, &dump) ==
                         R_FRONTEND_OK);
            R_TEST_CHECK(dump.bytes != NULL && strstr(dump.bytes, cases[i].node) != NULL);
        } else {
            R_TEST_CHECK(r_test_has_diagnostic(context, "R-DIAG-SYN-001"));
        }
        R_TEST_CHECK(r_frontend_reconstruct_source(context, source, r_test_write, &reconstructed) ==
                     R_FRONTEND_OK);
        R_TEST_CHECK(reconstructed.length == (size_t)length &&
                     memcmp(reconstructed.bytes, text, (size_t)length) == 0);
        free(dump.bytes);
        free(reconstructed.bytes);
        r_frontend_destroy(context);
    }
}

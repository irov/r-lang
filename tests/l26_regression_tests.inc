/* L26 (docs/language-completeness-roadmap.ru.md): core::swap (R-OWN-0019) and core::clone with
   the clone capability and associated clone hooks (R-OWN-0020, R-TYPE-0033). Each case is
   accepted, or is rejected with the diagnostic code and, where given, the rule it names. */
static void r_semantic_test_l26_regressions(void) {
    static const struct {
        const char *name;
        const char *source;
        const char *diagnostic;
    } cases[] = {
        {"swap-copy", "i32 f() { i32 a = 1; i32 b = 2; core::swap(&a, &b); return a - b; }", NULL},
        {"swap-move",
         "usize f() throws std.alloc::alloc_error { std.string::string a = "
         "std.string::from_str(\"a\"); std.string::string b = std.string::from_str(\"bb\"); "
         "core::swap(&a, &b); return a.len() + b.len(); }",
         NULL},
        {"swap-noalloc",
         "@noalloc @nonblocking void f(i32* a, i32* b) { core::swap(a, b); }",
         NULL},
        {"swap-same-place",
         "i32 f() { i32 a = 1; core::swap(&a, &a); return a; }",
         "R-DIAG-BORROW-001"},
        {"swap-two-elements",
         "i32 f() { i32[2] v = {1, 2}; core::swap(&v[0], &v[1]); return v[0]; }",
         "R-DIAG-BORROW-001"},
        {"swap-types",
         "i32 f() { i32 a = 1; i64 b = 2i64; core::swap(&a, &b); return a; }",
         "R-DIAG-TYPE-001 [R-OWN-0019]"},
        {"swap-borrowed",
         "usize f() { str a = \"x\"; str b = \"yy\"; core::swap(&a, &b); return len(a); }",
         "R-DIAG-TYPE-001 [R-OWN-0019]"},
        {"swap-arity",
         "void f() { i32 a = 1; core::swap(&a); }",
         "R-DIAG-TYPE-001 [R-OWN-0019]"},
        {"swap-shared",
         "void f(const i32* a, i32* b) { core::swap(a, b); }",
         "R-DIAG-BORROW-001 [R-OWN-0019]"},
        {"clone-copy-view",
         "str f(str s) { str t = core::clone(&s); return t; }",
         NULL},
        {"clone-copy-noalloc",
         "@noalloc i32 f(const i32* p) { return core::clone(p); }",
         NULL},
        {"clone-string",
         "usize f(const std.string::string* s) throws std.alloc::alloc_error { "
         "std.string::string c = core::clone(s); return c.len(); }",
         NULL},
        {"clone-string-unhandled",
         "usize f(const std.string::string* s) { std.string::string c = core::clone(s); "
         "return c.len(); }",
         "R-DIAG-EFFECT-001"},
        {"clone-arc-cannot-fail",
         "arc i32 f(const (arc i32)* p) { return core::clone(p); }",
         NULL},
        {"clone-no-contract",
         "struct S { own i32* p; }; S f(const S* s) throws std.alloc::alloc_error { "
         "return core::clone(s); }",
         "R-DIAG-TYPE-001 [R-OWN-0020]"},
        {"clone-borrowed-move",
         "array<str> f(const array<str>* s) throws std.alloc::alloc_error { "
         "return core::clone(s); }",
         "R-DIAG-TYPE-001 [R-OWN-0020]"},
        {"clone-task",
         "task<i32> f(const (task<i32>)* t) { return core::clone(t); }",
         "R-DIAG-TYPE-001 [R-OWN-0020]"},
        {"clone-nullable",
         "i32 f(const i32*? p) { return core::clone(p); }",
         "R-DIAG-TYPE-001 [R-OWN-0020]"},
        {"clone-hook",
         "struct S { own i32* p; }; S S::clone(const S* value) { return S {.p = new "
         "i32(*(value->p))}; } S f(const S* s) { return core::clone(s); }",
         NULL},
        {"clone-hook-checked",
         "struct S { std.string::string t; }; S S::clone(const S* value) throws "
         "std.alloc::alloc_error { return S {.t = core::clone(&value->t)}; } S f(const S* s) "
         "throws std.alloc::alloc_error { return core::clone(s); }",
         NULL},
        {"clone-hook-result",
         "struct S { i32 x; }; i32 S::clone(const S* value) { return value->x; }",
         "R-DIAG-TYPE-001 [R-OWN-0020]"},
        {"clone-hook-error",
         "error E { i32 c; }; struct S { own i32* p; }; S S::clone(const S* value) throws E { "
         "throw E {.c = 1}; }",
         "R-DIAG-TYPE-001 [R-OWN-0020]"},
        {"clone-hook-duplicate",
         "struct S { own i32* p; }; S S::clone(const S* value) { return S {.p = new i32(1)}; } "
         "S S::clone(const S* other) { return S {.p = new i32(2)}; }",
         "R-DIAG-NAME-002"},
        {"clone-generic",
         "@generic<T: clone & unborrowed> T f(const T* v) throws std.alloc::alloc_error { "
         "return core::clone(v); } i32 g() throws std.alloc::alloc_error { i32 x = 3; "
         "return f(&x); }",
         NULL},
        {"clone-generic-copy",
         "@generic<T: copy> T f(const T* v) { return core::clone(v); }",
         NULL},
        {"clone-generic-borrowed",
         "@generic<T: clone> T f(const T* v) throws std.alloc::alloc_error { "
         "return core::clone(v); }",
         "R-DIAG-TYPE-001 [R-OWN-0020]"},
        {"clone-generic-argument",
         "@generic<T: clone & unborrowed> T f(const T* v) throws std.alloc::alloc_error { "
         "return core::clone(v); } struct S { own i32* p; }; S g(const S* s) throws "
         "std.alloc::alloc_error { return f(s); }",
         "R-DIAG-TYPE-001"},
        {"clone-noalloc-move",
         "@noalloc usize f(const std.string::string* s) throws std.alloc::alloc_error { "
         "std.string::string c = core::clone(s); return c.len(); }",
         "R-DIAG-RESOURCE-001"},
        {"clone-hook-recursion",
         "struct Node { o<own Node*> next; }; Node Node::clone(const Node* value) throws "
         "std.alloc::alloc_error { return Node {.next = core::clone(&value->next)}; }",
         "R-DIAG-STACK-001"},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char source[4096];
        const int length =
            snprintf(source, sizeof(source), "module test.l26_regressions; %s", cases[i].source);
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
        const bool valid =
            status == (cases[i].diagnostic == NULL ? R_FRONTEND_OK : R_FRONTEND_INVALID_SOURCE) &&
            (cases[i].diagnostic == NULL ||
             r_semantic_l14_diagnostic_count(context, cases[i].diagnostic) != 0U) &&
            r_semantic_diagnostic_code_count(context, "R-DIAG-SLICE-001") == 0U;
        if (!valid) {
            (void)fprintf(stderr, "l26 regression %s status %d\n", cases[i].name, (int)status);
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

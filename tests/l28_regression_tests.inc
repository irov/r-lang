/* L28 (docs/language-completeness-roadmap.ru.md): function types and function values
   (R-TYPE-0054): conversions of function items, calls through values in every position,
   generic constraints, constants, targets in the call graph and resource proofs. Each case
   is accepted, or is rejected with the diagnostic code and the rule it names. */
static void r_semantic_test_l28_regressions(void) {
    static const struct {
        const char *name;
        const char *source;
        const char *diagnostic;
    } cases[] = {
        {"fn-local-call",
         "i32 twice(i32 v) { return v * 2; } i32 f() { fn(i32) -> i32 g = twice; return g(3); "
         "}",
         NULL},
        {"fn-field-call",
         "i32 twice(i32 v) { return v * 2; } struct Op { fn(i32) -> i32 apply; }; i32 f() { Op "
         "op = {.apply = twice}; return op.apply(2); }",
         NULL},
        {"fn-dict-table",
         "i32 add(i32 a, i32 b) { return a + b; } i32 sub(i32 a, i32 b) { return a - b; } i32 "
         "f(str name) throws std.dict::insert_error<str, fn(i32, i32) -> i32> { dict<str, "
         "fn(i32, i32) -> i32> table = std.dict::create::<str, fn(i32, i32) -> i32>(); "
         "std.dict::insert(&table, \"add\", add); std.dict::insert(&table, \"sub\", sub); "
         "switch (std.dict::get(&table, &name)) { case variant o::some(op): return (*op)(4, "
         "3); case variant o::none: return 0; } return 0; }",
         NULL},
        {"fn-method-item",
         "struct C { i32 n; }; i32 C::get(const C* this) { return this->n; } i32 f() { C c = "
         "{.n = 4}; fn(const C*) -> i32 g = C::get; return g(&c); }",
         NULL},
        {"fn-closed-generic",
         "@generic<T> T same(T v) { return move v; } i32 f() { fn(i32) -> i32 g = same::<i32>; "
         "return g(5); }",
         NULL},
        {"fn-module-table",
         "i32 a(i32 v) { return v; } i32 b(i32 v) { return v + 1; } const (fn(i32) -> i32)[2] "
         "table = {a, b}; i32 f() { return table[1](2); }",
         NULL},
        {"fn-generic-constraint",
         "@generic<F: fn(i32) -> i32> i32 run(F g, i32 v) { return g(v); } i32 twice(i32 v) { "
         "return v * 2; } i32 f() { fn(i32) -> i32 g = twice; return run(g, 2); }",
         NULL},
        {"fn-generic-dependent",
         "@generic<T> T apply(fn(T) -> T g, T v) { return g(move v); } i32 twice(i32 v) { "
         "return v * 2; } i32 f() { return apply(twice, 2); }",
         NULL},
        {"fn-equality",
         "i32 a(i32 v) { return v; } i32 b(i32 v) { return v + 1; } bool f() { fn(i32) -> i32 "
         "x = a; fn(i32) -> i32 y = b; return x != y && x == a; }",
         NULL},
        {"fn-forget-guarantee",
         "@noalloc i32 fast(i32 v) { return v; } i32 f() { fn @noalloc (i32) -> i32 x = fast; "
         "fn(i32) -> i32 y = x; return y(1); }",
         NULL},
        {"fn-noalloc-call",
         "@noalloc i32 fast(i32 v) { return v; } @noalloc i32 run(fn @noalloc (i32) -> i32 g) "
         "{ return g(1); } i32 f() { return run(fast); }",
         NULL},
        {"fn-temporary-call",
         "i32 a(i32 v) { return v; } fn(i32) -> i32 pick() { return a; } i32 f() { return "
         "pick()(3); }",
         NULL},
        {"fn-option",
         "i32 a(i32 v) { return v; } i32 f() { o<fn(i32) -> i32> g = o::some(a); switch (g) { "
         "case variant o::some(h): return h(1); case variant o::none: return 0; } return 0; }",
         NULL},
        {"fn-checked-errors",
         "error Bad { i32 code; }; i32 check(i32 v) throws Bad { if (v < 0) { throw Bad {.code "
         "= v}; } return v; } i32 f() { fn(i32) -> i32 throws(Bad) g = check; try { return "
         "g(-1); } catch (Bad failure) { return failure.code; } }",
         NULL},
        {"fn-error-field-first",
         "struct Route { fn(i32) -> i32 throws(Bad) run; }; error Bad { i32 code; }; i32 "
         "check(i32 v) throws Bad { return v; } i32 f() throws Bad { Route r = {.run = check}; "
         "return r.run(1); }",
         NULL},
        {"fn-async-field",
         "async i32 twice(i32 v) { return v * 2; } struct H { async fn(i32) -> i32 run; }; "
         "async i32 f() throws std.async::start_error { H h = {.run = twice}; return await "
         "h.run(2); }",
         NULL},
        {"fn-mismatch-result",
         "i64 wide(i32 v) { return 1i64; } void f() { fn(i32) -> i32 g = wide; }",
         "R-DIAG-TYPE-001 [R-TYPE-0054]"},
        {"fn-mismatch-arity",
         "i32 two(i32 a, i32 b) { return a; } void f() { fn(i32) -> i32 g = two; }",
         "R-DIAG-TYPE-001 [R-TYPE-0054]"},
        {"fn-async-to-sync",
         "async i32 later(i32 v) { return v; } void f() { fn(i32) -> i32 g = later; }",
         "R-DIAG-TYPE-001 [R-TYPE-0054]"},
        {"fn-sync-to-async",
         "i32 now(i32 v) { return v; } void f() { async fn(i32) -> i32 g = now; }",
         "R-DIAG-TYPE-001 [R-TYPE-0054]"},
        {"fn-errors-mismatch",
         "error Bad { i32 code; }; i32 check(i32 v) throws Bad { return v; } void f() { "
         "fn(i32) -> i32 g = check; }",
         "R-DIAG-TYPE-001 [R-TYPE-0054]"},
        {"fn-add-guarantee",
         "i32 slow(i32 v) { return v; } void f() { fn @noalloc (i32) -> i32 g = slow; }",
         "R-DIAG-TYPE-001 [R-TYPE-0054]"},
        {"fn-strengthen-value",
         "i32 slow(i32 v) { return v; } void f() { fn(i32) -> i32 g = slow; fn @noalloc (i32) "
         "-> i32 h = g; }",
         "R-DIAG-TYPE-001 [R-INIT-0002]"},
        {"fn-lambda",
         "void f() { i32 c = 3; fn i32 add(i32 v) move(c) { return v + c; } fn(i32) -> i32 g = "
         "add; }",
         "R-DIAG-TYPE-001 [R-TYPE-0054]"},
        {"fn-open-generic",
         "@generic<T> T same(T v) { return move v; } void f() { fn(i32) -> i32 g = same; }",
         "R-DIAG-TYPE-001 [R-FUNC-0007]"},
        {"fn-mode",
         "i32 a(i32 v) { return v; } void f() { fn shared(i32) -> i32 g = a; }",
         "R-DIAG-TYPE-001 [R-TYPE-0054]"},
        {"fn-pack",
         "i32 a(i32 v) { return v; } void f() { fn(i32...) -> i32 g = a; }",
         "R-DIAG-TYPE-001 [R-TYPE-0054]"},
        {"fn-never-result",
         "void f() { o<fn(i32) -> never> g = o::none; }",
         "R-DIAG-TYPE-001 [R-TYPE-0054]"},
        {"fn-async-borrowed",
         "void f() { o<async fn(const i32*) -> i32> g = o::none; }",
         "R-DIAG-ASYNC-001 [R-TYPE-0054]"},
        {"fn-null",
         "void f() { fn(i32) -> i32 g = null; }",
         "R-DIAG-TYPE-001 [R-OBJ-0007]"},
        {"fn-ordering",
         "i32 a(i32 v) { return v; } bool f() { fn(i32) -> i32 x = a; fn(i32) -> i32 y = a; "
         "return x < y; }",
         "R-DIAG-TYPE-001 [R-EXPR-0009]"},
        {"fn-arithmetic",
         "i32 a(i32 v) { return v; } i32 f() { fn(i32) -> i32 x = a; return x + 1; }",
         "R-DIAG-TYPE-001 [R-EXPR-0003]"},
        {"fn-default-field",
         "struct Op { fn(i32) -> i32 apply; }; void f() { Op op = {}; }",
         "R-DIAG-INIT-002 [R-INIT-0005]"},
        {"fn-dict-key",
         "void f() { dict<fn(i32) -> i32, i32> d = std.dict::create::<fn(i32) -> i32, i32>(); "
         "}",
         "R-DIAG-TYPE-001 [R-LIB-0020]"},
        {"fn-other-method",
         "i32 a(i32 v) { return v; } i32 f() { fn(i32) -> i32 x = a; return x.apply(1); }",
         "R-DIAG-NAME-001 [R-TYPE-0054]"},
        {"fn-recursion",
         "i32 down(i32 n) { if (n == 0) { return 0; } fn(i32) -> i32 next = down; return "
         "next(n - 1); }",
         "R-DIAG-STACK-001 [R-FUNC-0004]"},
        {"fn-noalloc-plain",
         "i32 a(i32 v) { return v; } @noalloc i32 run(fn(i32) -> i32 g) { return g(1); } i32 "
         "f() { return run(a); }",
         "R-DIAG-RESOURCE-001 [R-FUNC-0019]"},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char source[4096];
        const int length =
            snprintf(source, sizeof(source), "module test.l28_regressions; %s", cases[i].source);
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
            (void)fprintf(stderr, "l28 regression %s status %d\n", cases[i].name, (int)status);
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

/* L17 (docs/language-completeness-roadmap.ru.md): inference from the expected type of a result.
 * Each case is accepted, or is rejected with the diagnostic code and, where given, the rule it
 * names; no case may leave a construct outside the implemented slice (R-DIAG-SLICE-001). */
static void r_semantic_test_l17_regressions(void) {
    static const struct {
        const char *name;
        const char *source;
        const char *diagnostic;
    } cases[] = {
        /* L17.1, R-TYPE-0036: generic parameters that the arguments leave open take the expected
           type of the result: an initializer, a conditional assignment, a return, an argument of a
           known parameter type, a field and an element. */
        {"expected-initializer",
         "@generic<T> o<T> nothing() { return o::none; } o<i32> f() { o<i32> n = nothing(); "
         "return n; }",
         NULL},
        {"expected-assignment",
         "@generic<T> o<T> nothing() { return o::none; } o<i32> f(bool flag) { o<i32> n = "
         "o::some(1); if (flag == true) { n as void; n = nothing(); } return n; }",
         NULL},
        {"expected-return",
         "@generic<T> o<T> nothing() { return o::none; } o<u8> f() { return nothing(); }",
         NULL},
        {"expected-argument",
         "@generic<T> o<T> nothing() { return o::none; } i32 take(o<i16> value) { value as void; "
         "return 1; } i32 f() { return take(nothing()); }",
         NULL},
        {"expected-field",
         "@generic<T> o<T> nothing() { return o::none; } struct Holder { o<i64> slot; }; Holder "
         "f() { return Holder { .slot = nothing() }; }",
         NULL},
        {"expected-element",
         "@generic<T> o<T> nothing() { return o::none; } (o<i32>)[2] f() { (o<i32>)[2] pair = "
         "{nothing(), nothing()}; return pair; }",
         NULL},
        {"expected-fills-open-only",
         "@generic<T: copy, U> o<U> tagged(T value) { value as void; return o::none; } o<i64> f() "
         "{ o<i64> x = tagged(1u8); return x; }",
         NULL},
        {"expected-receiver-method",
         "@generic<T: copy> struct Box { T value; }; @generic<T: copy, U> o<U> "
         "Box<T>::none_of(const Box<T>* this) { return o::none; } o<u8> f() { Box<i32> b = "
         "{.value = 1}; o<u8> r = b.none_of(); return r; }",
         NULL},
        {"expected-generic-body",
         "@generic<T> o<T> nothing() { return o::none; } @generic<T: copy> o<T> twice() { o<T> "
         "first = nothing(); first as void; return nothing(); }",
         NULL},
        {"expected-await",
         "@generic<T: send & unborrowed> protected async o<T> later() { return o::none; } "
         "protected async o<i32> f() throws std.async::start_error { o<i32> v = await later(); "
         "return v; }",
         NULL},
        {"expected-task",
         "@generic<T: send & unborrowed> protected async o<T> later() { return o::none; } "
         "protected async o<u16> f() throws std.async::start_error { task<o<u16>> t = later(); "
         "o<u16> v = await move t; return v; }",
         NULL},
        /* Without an expected type, or where the result does not fit it, the open parameter stays
           uninferred; an inferred parameter is never replaced. */
        {"no-expected-type",
         "@generic<T> o<T> nothing() { return o::none; } void f() { nothing() as void; }",
         "R-DIAG-TYPE-001 [R-EXPR-0020]"},
        {"generic-argument-has-no-expected-type",
         "@generic<T> o<T> nothing() { return o::none; } @generic<T: copy> T same(T value) { "
         "return value; } o<i32> f() { o<i32> x = same(nothing()); return x; }",
         "R-DIAG-TYPE-001 [R-EXPR-0020]"},
        {"expected-type-does-not-fit",
         "@generic<T> o<T> nothing() { return o::none; } i32 f() { i32 x = nothing(); return x; }",
         "R-DIAG-TYPE-001 [R-EXPR-0020]"},
        {"expected-type-does-not-override",
         "@generic<T: copy> T same(T value) { return value; } o<i64> f() { o<i32> small = "
         "o::some(1); o<i64> x = same(small); return x; }",
         "R-DIAG-TYPE-001"},
        /* L17.2, R-TYPE-0036: standard constructors written without `::<...>` take their type
           operands from the expected type of their result. */
        {"constructor-array-create",
         "usize f() { array<i32> a = std.array::create(); usize n = len(a); drop a; return n; }",
         NULL},
        {"constructor-array-with-capacity",
         "usize f() throws std.alloc::alloc_error { array<u16> a = "
         "std.array::with_capacity(4usize); usize n = len(a); drop a; return n; }",
         NULL},
        {"constructor-list-create",
         "void f() { list<i8> l = std.list::create(); drop l; }",
         NULL},
        {"constructor-dict-create",
         "void f() throws std.alloc::alloc_error { dict<str, i32> d = std.dict::create(); "
         "dict<u32, bool> e = std.dict::with_capacity(8usize); drop d; drop e; }",
         NULL},
        {"constructor-sync",
         "import std.sync; void f() throws std.alloc::alloc_error { std.sync::channel<i32> c = "
         "std.sync::channel(); std.sync::sync_channel<i64> s = std.sync::sync_channel(2usize); "
         "std.sync::once_lock<u32> lock = std.sync::once_lock(); drop c; drop s; drop lock; }",
         NULL},
        {"constructor-field-and-return",
         "struct Registry { array<i32> values; dict<str, i64> names; }; Registry make() { return "
         "Registry { .values = std.array::create(), .names = std.dict::create() }; } array<u8> "
         "fresh() { return std.array::create(); }",
         NULL},
        {"constructor-generic-body",
         "@generic<T: copy> array<T> make() { array<T> values = std.array::create(); return move "
         "values; }",
         NULL},
        {"constructor-no-expected-type",
         "void f() { std.array::create() as void; }",
         "R-DIAG-TYPE-001 [R-TYPE-0036]"},
        {"constructor-expected-other-kind",
         "void f() { list<i32> l = std.array::create(); drop l; }",
         "R-DIAG-TYPE-001 [R-TYPE-0036]"},
        {"constructor-type-among-values",
         "void f() { array<i32> a = std.array::create(i32); drop a; }",
         "R-DIAG-SYN-001 [R-GRAM-0005]"},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char source[2048];
        const int length =
            snprintf(source, sizeof(source), "module test.l17_regressions; %s", cases[i].source);
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
            (void)fprintf(stderr, "l17 regression %s status %d\n", cases[i].name, (int)status);
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

/* L18 (docs/language-completeness-roadmap.ru.md): tuples and type packs.
 * Each case is accepted, or is rejected with the diagnostic code and, where given, the rule it
 * names; no case may leave a construct outside the implemented slice (R-DIAG-SLICE-001). */
static void r_semantic_test_l18_regressions(void) {
    static const struct {
        const char *name;
        const char *source;
        const char *diagnostic;
    } cases[] = {
        /* L18.1, R-TYPE-0052: tuple types, literals, elements and patterns. */
        {"tuple-type-and-literal", "(i32, bool) make() { return (1, true); }", NULL},
        {"tuple-expected-elements",
         "(u8, i64) make() { (u8, i64) pair = (1, 2); return pair; }",
         NULL},
        {"tuple-element-access",
         "i32 f() { (i32, u8, i32) t = (3, 4u8, 5); return t.0 + t.1 as i32 + t.2; }",
         NULL},
        {"tuple-element-assignment",
         "i32 f() { (i32, i32) t = (1, 2); t.1 = 7; return t.1; }",
         NULL},
        {"tuple-match-destructure",
         "import std.string; std.string::string second((i32, std.string::string) pair) { return "
         "match (move pair) { case {.0 = _, .1 = move text}: move text; }; }",
         NULL},
        /* L18-3: a tuple field type kept the fields of its struct contiguous. */
        {"tuple-nested-and-field",
         "struct Holder { i32 first; (i32, (u8, bool)) inner; i64 last; }; bool f() { Holder h = "
         "{.first = 1, .inner = (1, (2u8, true)), .last = 2i64}; return h.inner.1.1; }",
         NULL},
        {"tuple-in-error-and-variant",
         "error Failed { (i32, i32) where; }; enum Shape { circle((i32, i32)), square }; i32 f() "
         "{ Shape s = Shape::circle((1, 2)); return match (s) { case variant "
         "Shape::circle(c): c.0 + c.1; default: 0; }; }",
         NULL},
        {"tuple-generic-argument",
         "@generic<T: copy> T first((T, T) pair) { return pair.0; } i64 f() { return first((1i64, "
         "2i64)); }",
         NULL},
        {"tuple-index-out-of-range",
         "i32 f() { (i32, i32) t = (1, 2); return t.2; }",
         "R-DIAG-NAME-001 [R-TYPE-0052]"},
        {"tuple-void-element", "(i32, void) f();", "R-DIAG-TYPE-001 [R-TYPE-0052]"},
        {"tuple-arrow-element",
         "i32 f() { (i32, bool) pair = (5, true); const (i32, bool)* view = &pair; return "
         "view->0; }",
         NULL},
        {"tuple-trait-implementation",
         "trait Shown { i32 show(const Self* this); }; impl Shown for (i32, bool) { i32 "
         "show(const (i32, bool)* this) { return this->0; } }; @generic<T: Shown> i32 "
         "display(const T* value) { return value->show(); } i32 f() { (i32, bool) pair = (5, "
         "true); return display(&pair); }",
         NULL},
        {"tuple-container-elements",
         "void f() throws std.alloc::alloc_error, std.array::push_error<(i32, bool)> { array<(i32, "
         "bool)> items = std.array::create(); items.push((1, true)); drop items; }",
         NULL},
        {"tuple-not-json",
         "@generic<T: json_encode> usize enc(T value) { move value as void; return 1usize; } "
         "usize f() { return enc((1, true)); }",
         "R-DIAG-TYPE-001"},
        /* L18-1: a later `.N` use keeps the views a tuple holds live. */
        {"tuple-view-kept-live",
         "i32 f() { i32 a = 1; (i32*, i32) held = (&a, 5); i32 read = a; *held.0 = 4; return "
         "read; }",
         "R-DIAG-BORROW-001 [R-BORROW-0002]"},
        {"tuple-moved-view-kept-live",
         "i32 f() { i32 a = 1; i32* pa = &a; (i32*, i32) held = (move pa, 5); i32 read = a; "
         "*held.0 = 4; return read; }",
         "R-DIAG-BORROW-001 [R-BORROW-0002]"},
        /* L18.2, R-TYPE-0053: pack declarations, element constraints and lengths. */
        {"pack-count",
         "@generic<T...> usize count(T... values) { move values as void; return len(T...); } "
         "usize f() { return count(1, true, 'c') + count(); }",
         NULL},
        {"pack-copy-identity",
         "@generic<T...: copy> (T...) same(T... values) { return values; } i32 f() { (i32, bool) "
         "t = same(1, false); return t.0; }",
         NULL},
        {"pack-element-constraint",
         "import std.string; @generic<T...: copy> usize count(T... values) { values as void; "
         "return len(T...); } usize f() throws std.alloc::alloc_error { return count(1, "
         "std.string::from_str(\"x\")); }",
         "R-DIAG-TYPE-001 [R-TYPE-0001]"},
        {"pack-trait-element-only",
         "trait Shown { i32 show(const Self* this); }; impl Shown for i32 { i32 show(const i32* "
         "this) { return *this; } }; @generic<T...: Shown> i32 bad(T... values) { return "
         "values.show(); }",
         "R-DIAG-NAME-001"},
        {"pack-length-branch",
         "@generic<T...> usize nonempty(T... values) { move values as void; @if (len(T...) == "
         "0usize) { return 0usize; } return 1usize; } usize f() { return nonempty(1) + "
         "nonempty(); }",
         NULL},
        {"pack-not-last",
         "@generic<T..., U> usize bad(U u, T... values) { move u as void; move values as void; "
         "return 0usize; }",
         "R-DIAG-TYPE-001 [R-TYPE-0053]"},
        {"pack-on-struct",
         "@generic<T...> struct Bag { i32 x; };",
         "R-DIAG-TYPE-001 [R-TYPE-0053]"},
        {"pack-bare-name",
         "@generic<T...> usize bad(T values) { move values as void; return 0usize; }",
         "R-DIAG-TYPE-001 [R-TYPE-0053]"},
        {"pack-static-predicate",
         "@generic<T...> usize bad(T... values) { @if (T is copy) { values as void; return "
         "1usize; } move values as void; return 0usize; }",
         "R-DIAG-TYPE-001 [R-TYPE-0053]"},
        {"pack-len-not-pack",
         "@generic<T> usize bad(T value) { move value as void; return len(T...); }",
         "R-DIAG-TYPE-001 [R-TYPE-0053]"},
        {"pack-method",
         "struct Counter { usize base; }; @generic<T...> usize Counter::count(const Counter* "
         "this, T... values) { move values as void; return this->base + len(T...); } usize f() { "
         "Counter c = {.base = 1usize}; return c.count(2, true); }",
         NULL},
        {"pack-explicit-arguments",
         "@generic<T...> usize count(T... values) { move values as void; return len(T...); } "
         "@generic<T...> usize arity() { return len(T...); } usize f() { return count::<u8, "
         "bool>(1, true) + arity::<u8, u16, u32>(); }",
         NULL},
        {"pack-explicit-spread",
         "@generic<T...> usize count(T... values) { move values as void; return len(T...); } "
         "usize f() { (i32, bool) pair = (1, true); return count::<i32, bool>(...pair); }",
         NULL},
        {"pack-tuple-type-argument",
         "@generic<T> struct Box { T value; }; @generic<T...> Box<(T...)> boxed(T... values) { "
         "return Box<(T...)> {.value = move values}; } Box<(i32, bool)> f() { return boxed(1, "
         "true); }",
         NULL},
        {"pack-method-spread",
         "struct Counter { usize base; }; @generic<T...> usize Counter::count(const Counter* "
         "this, T... values) { move values as void; return this->base + len(T...); } "
         "@generic<T...> usize relay(const Counter* c, T... values) { return c->count(1u8, "
         "...move values); } usize f() { Counter c = {.base = 1usize}; (i32, bool) pair = (1, "
         "true); return c.count(...pair) + relay(&c, 'x'); }",
         NULL},
        {"pack-overload",
         "usize g(i32 a) { a as void; return 100usize; } @generic<T...> usize g(T... values) { "
         "move values as void; return len(T...); } usize f() { return g(1, 2) + g(); }",
         NULL},
        {"pack-overload-ambiguous",
         "usize g(i32 a) { a as void; return 100usize; } @generic<T...> usize g(T... values) { "
         "move values as void; return len(T...); } usize f() { return g(1); }",
         "R-DIAG-TYPE-001 [R-FUNC-0004]"},
        /* L18.3: spreads of a tuple into parameters, in evaluation order. */
        {"spread-tuple-ordinary",
         "i32 add3(i32 a, i32 b, i32 c) { return a + b + c; } i32 f() { (i32, i32) pair = (2, 3); "
         "return add3(1, ...pair); }",
         NULL},
        {"spread-tuple-generic",
         "@generic<A: copy, B: copy> B second(A a, B b) { a as void; return b; } bool f() { (u8, "
         "bool) t = (1u8, true); return second(...t); }",
         NULL},
        {"spread-tuple-move",
         "import std.string; usize measure(std.string::string text, i32 extra) { usize n = "
         "std.string::len(&text); move text as void; return n + extra as usize; } usize f() "
         "throws std.alloc::alloc_error { (std.string::string, i32) owned = "
         "(std.string::from_str(\"ab\"), 1); return measure(...move owned); }",
         NULL},
        {"spread-tuple-into-pack-with-prefix",
         "@generic<T...> usize count(T... values) { move values as void; return len(T...); } "
         "usize f() { (i32, bool) pair = (1, true); return count('c', ...pair); }",
         NULL},
        {"spread-tuple-literal",
         "(i32, u8, bool) f() { (u8, bool) tail = (2u8, true); return (1, ...tail); }",
         NULL},
        {"spread-not-tuple",
         "i32 add2(i32 a, i32 b) { return a + b; } i32 f() { i32 x = 1; return add2(1, ...x); }",
         "R-DIAG-TYPE-001 [R-TYPE-0053]"},
        {"spread-count-mismatch",
         "i32 add2(i32 a, i32 b) { return a + b; } i32 f() { (i32, i32) pair = (1, 2); return "
         "add2(1, ...pair); }",
         "R-DIAG-TYPE-001"},
        {"spread-not-last",
         "i32 add2(i32 a, i32 b) { return a + b; } i32 f() { (i32) one = 1; return add2(...one, "
         "2); }",
         "R-DIAG-SYN-001"},
        /* L18.4: packs whose length an instantiation decides. */
        {"pack-forward",
         "@generic<T...> usize count(T... values) { move values as void; return len(T...); } "
         "@generic<T...> usize forward(T... values) { return count(...move values); } usize f() "
         "{ return forward(1, 2, 3); }",
         NULL},
        {"pack-prepend",
         "@generic<T...> usize count(T... values) { move values as void; return len(T...); } "
         "@generic<T...> usize prepend(T... values) { return count(1u8, ...move values); } usize "
         "f() { return prepend('x', true); }",
         NULL},
        {"pack-recursion",
         "@generic<H: copy, T...: copy> usize depth(H head, T... tail) { head as void; @if "
         "(len(T...) != 0usize) { return 1usize + depth(...tail); } tail as void; return 1usize; "
         "} usize f() { return depth(1, true, 'c'); }",
         NULL},
        {"pack-recursion-else-branch",
         "@generic<H: copy, T...: copy> usize depth(H head, T... tail) { head as void; @if "
         "(len(T...) == 0usize) { tail as void; return 1usize; } @else { return 1usize + "
         "depth(...tail); } } usize f() { return depth(1, 2u8); }",
         NULL},
        {"pack-exact-length",
         "@generic<A, B> usize pair(A a, B b) { move a as void; move b as void; return 2usize; } "
         "@generic<T...> usize exact(T... values) { @if (len(T...) == 2usize) { return "
         "pair(...move values); } move values as void; return 0usize; } usize f() { return "
         "exact(1, true) + exact(1); }",
         NULL},
        {"pack-tuple-types",
         "@generic<T...> (i32, T...) prepend(T... values) { return (7, ...move values); } "
         "@generic<T...: copy> usize takes((i32, T...) shaped) { shaped as void; return "
         "len(T...); } usize f() { (i32, bool, u8) t = prepend(true, 3u8); return takes(t) + "
         "takes((1, 'c')); }",
         NULL},
        {"pack-trait-recursion",
         "trait Shown { i32 show(const Self* this); }; impl Shown for i32 { i32 show(const i32* "
         "this) { return *this; } }; impl Shown for bool { i32 show(const bool* this) { return "
         "1; } }; @generic<H: Shown, T...: Shown> i32 total(H head, T... tail) { i32 first = "
         "head.show(); @if (len(T...) != 0usize) { return first + total(...move tail); } move "
         "tail as void; return first; } i32 f() { return total(1, true, 2); }",
         NULL},
        {"pack-spread-needs-length",
         "@generic<H: copy, T...: copy> usize depth(H head, T... tail) { head as void; return "
         "1usize + depth(...tail); }",
         "R-DIAG-TYPE-001 [R-TYPE-0053]"},
        {"pack-spread-needs-exact-length",
         "@generic<A, B> usize pair(A a, B b) { move a as void; move b as void; return 2usize; } "
         "@generic<T...> usize bad(T... values) { @if (len(T...) >= 2usize) { return "
         "pair(...move values); } move values as void; return 0usize; }",
         "R-DIAG-TYPE-001 [R-TYPE-0053]"},
        {"pack-spread-into-ordinary",
         "i32 add2(i32 a, i32 b) { return a + b; } @generic<T...> i32 bad(T... values) { @if "
         "(len(T...) == 2usize) { return add2(...move values); } move values as void; return 0; "
         "}",
         "R-DIAG-TYPE-001 [R-TYPE-0053]"},
        {"pack-element-access",
         "@generic<T...: copy> i32 bad(T... values) { values as void; return values.0; }",
         "R-DIAG-TYPE-001 [R-TYPE-0053]"},
        {"pack-spread-needs-move",
         "@generic<T...> usize count(T... values) { move values as void; return len(T...); } "
         "@generic<T...> usize bad(T... values) { return count(...values); }",
         "R-DIAG-MOVE-001 [R-OWN-0003]"},
        {"pack-view-kept-live",
         "@generic<T...> (T...) keep(T... values) { return move values; } @generic<T...> (T...) "
         "forward(T... values) { return keep(...move values); } i32 f() { i32 a = 1; i32* pa = "
         "&a; (i32*, i32) held = forward(move pa, 5); i32 read = a; *held.0 = 4; return read; }",
         "R-DIAG-BORROW-001 [R-BORROW-0002]"},
        /* L18.4, R-TYPE-0044: callable signatures that end in a pack. */
        {"callable-pack",
         "u8 pick(i32 a, bool b) { if (b == true) { return a as u8; } return 0u8; } "
         "@generic<F: fn(P...) -> R, R, P...> R apply(const F* f, P... args) { return "
         "f(...move args); } u8 g() { auto chosen = pick; return apply(&chosen, 7, true); }",
         NULL},
        {"callable-pack-fixed-first",
         "i32 add3(i32 a, i32 b, i32 c) { return a + b + c; } @generic<F: fn(i32, P...) -> R, R, "
         "P...> R with_first(const F* f, P... args) { return f(10, ...move args); } i32 g() { "
         "auto summed = add3; return with_first(&summed, 20, 30); }",
         NULL},
        {"callable-pack-modes",
         "@generic<F: fn mut(P...) -> R, R, P...> R apply_mut(F* f, P... args) { return "
         "f->call(...move args); } @generic<F: fn once(P...) -> R, R, P...> R apply_once(F f, "
         "P... args) { return (move f).call(...move args); } i32 g() { i32 total = 0; fn mut i32 "
         "counter(i32 step) { total += step; return total; } i32 once_base = 3; fn once i32 "
         "consume(bool keep) move(once_base) { if (keep == true) { return once_base; } return 0; "
         "} return apply_mut(&counter, 2) + apply_once(move consume, true); }",
         NULL},
        {"callable-pack-needs-spread",
         "@generic<F: fn(P...) -> i32, P...> i32 bad(const F* f, P... args) { move args as void; "
         "return f(1, 2); }",
         "R-DIAG-TYPE-001"},
        {"callable-pack-not-last",
         "@generic<F: fn(P..., i32) -> i32, P...> i32 bad(const F* f, P... args) { move args as "
         "void; return 0; }",
         "R-DIAG-SYN-001"},
        /* Async pack functions and spreads into async starts. */
        {"async-pack",
         "@generic<T...: send & unborrowed> protected async usize count(T... values) { move "
         "values as void; return len(T...); } @generic<H: send & unborrowed, T...: send & "
         "unborrowed> protected async usize depth(H head, T... tail) throws "
         "std.async::start_error { move head as void; @if (len(T...) != 0usize) { usize rest = "
         "await depth(...move tail); return rest + 1usize; } move tail as void; return 1usize; } "
         "protected async usize f() throws std.async::start_error { usize a = await count(1, "
         "true); usize b = await depth(1, 2u8, 'c'); return a + b; }",
         NULL},
        /* L18-2: a module with traits reports a call of a later function by name. */
        {"later-function-in-trait-module",
         "trait Shown { i32 show(const Self* this); }; i32 first(i32 v) { i32 total = 1; return "
         "total + second(v); } i32 second(i32 v) { return v; }",
         "R-DIAG-NAME-001 [R-NAME-0003]"},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char source[2048];
        const int length =
            snprintf(source, sizeof(source), "module test.l18_regressions; %s", cases[i].source);
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
            (void)fprintf(stderr, "l18 regression %s status %d\n", cases[i].name, (int)status);
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

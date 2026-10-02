/* L6 acceptance regressions (docs/language-completeness-matrix.ru.md): every former silent
 * failure or slice notice for an invalid program now carries its normative diagnostic, and
 * programs that were wrongly rejected are accepted. */
static void r_semantic_test_l6_regressions(void) {
    static const struct {
        const char *name;
        const char *source;
        const char *diagnostic;
    } cases[] = {
        /* A1: an aggregate literal names its generic type without `::`. */
        {"explicit-type-arguments-aggregate-literal",
         "@generic<T> struct Box { T value; }; i32 f() { Box<i32> b = Box::<i32> { .value = 1 }; "
         "return b.value; }",
         "R-DIAG-SYN-001"},
        {"generic-aggregate-literal",
         "@generic<T> struct Box { T value; }; i32 f() { Box<i32> b = Box<i32> { .value = 1 }; "
         "return b.value; }",
         NULL},
        /* A6: an unresolved type in a local declaration. */
        {"unresolved-local-type", "i32 f() { Nothing p = {.x = 1}; return 0; }", "R-DIAG-NAME-001"},
        /* G1: invalid programs formerly reported as implementation slices. */
        {"field-ffi-attribute",
         "struct S { @repr(C) i32 v; }; i32 f() { return 0; }",
         "R-DIAG-FFI-003"},
        {"field-attribute", "struct S { @scoped i32 v; }; i32 f() { return 0; }", "R-DIAG-SYN-002"},
        {"option-braced-constructor",
         "i32 f() { o<i32> x = o { 1 }; x as void; return 0; }",
         "R-DIAG-TYPE-001"},
        {"format-receiver",
         "struct Inner { i32 v; }; struct Outer { Inner inner; }; "
         "i32 Inner::format(const Inner* this, i32 k) { return this->v + k; } "
         "i32 f() { Outer outer = {.inner = {.v = 1}}; return outer.inner.format(2); }",
         "R-DIAG-FORMAT-001"},
        {"void-option-field", "struct S { o<void> v; }; i32 f() { return 0; }", "R-DIAG-TYPE-001"},
        {"void-owner-field", "struct S { own void* v; }; i32 f() { return 0; }", "R-DIAG-TYPE-001"},
        {"void-module-object", "o<void> X = o::none; i32 f() { return 0; }", "R-DIAG-TYPE-001"},
        {"raw-void-field", "struct S { raw void* v; }; i32 f() { return 0; }", NULL},
        {"thread-local-function",
         "thread_local i32 g() { return 1; } i32 f() { return 0; }",
         "R-DIAG-SYN-001"},
        {"pointer-to-bool",
         "i32 f() { i32 v = 1; unsafe { raw i32* p = &v as raw i32*; bool b = p as bool; b as "
         "void; } return 0; }",
         "R-DIAG-TYPE-003"},
        {"pointer-to-integer",
         "i32 f() { i32 v = 1; unsafe { raw i32* p = &v as raw i32*; usize a = p as usize; a as "
         "void; } return 0; }",
         "R-DIAG-TYPE-003"},
        /* C1: a named Move nested in an aggregate argument of a start. */
        {"nested-move-start",
         "struct T { own i32* v; }; struct P { T left; }; async i32 take(P pair) { return 1; } "
         "async i32 f() throws std.async::start_error { T a = {.v = new i32(1)}; "
         "return await take(P {.left = move a}); }",
         "R-DIAG-MOVE-003"},
        {"nested-call-move-start",
         "struct T { own i32* v; }; T keep(T value) { return move value; } "
         "async i32 take(T item) { return 1; } async i32 f() throws std.async::start_error { "
         "T a = {.v = new i32(1)}; return await take(keep(move a)); }",
         NULL},
        /* E1: reads through projections of any depth acknowledge an out result. */
        {"out-nested-projection",
         "struct In { i32 v; }; struct Out { In inner; i32 tag; }; "
         "void make(out Out r, i32 v) { r = Out {.inner = In {.v = v}, .tag = 0}; } "
         "i32 f() { Out local = {.inner = In {.v = 0}, .tag = 0}; make(out local, 2); "
         "return local.inner.v; }",
         NULL},
        {"out-through-borrow",
         "struct T { i32 v; }; void make(out T r, i32 v) { r = T {.v = v}; } "
         "i32 f() { T x = {.v = 0}; T* p = &x; make(out *p, 1); return p->v; }",
         NULL},
        /* E3: a function name passed to an overloaded generic family. */
        {"overload-family-function-item",
         "i32 one(i32 x) { return x + 1; } @generic<F: fn(i32) -> i32> i32 apply(F f, i32 x) { "
         "return f(x); } @generic<F: fn(i32) -> i32> i32 apply(F f, i32 x, i32 y) { return f(x) "
         "+ y; } i32 probe() { return apply(one, 41) + apply(one, 40, 1); }",
         NULL},
        /* E4: a lambda calls a borrowed callable it captures. */
        {"captured-borrowed-callable",
         "@generic<F: fn(i32) -> i32> i32 outer(const F* f, i32 x) { fn i32 step(i32 v) { "
         "return f(v) + 1; } return step(x); } i32 probe() { fn i32 inc(i32 v) { return v + 1; } "
         "return outer(&inc, 1); }",
         NULL},
        /* E5: a method call through a moved shared borrow keeps the closure shared. */
        {"moved-shared-borrow-method",
         "struct Echo { u32 base; }; u32 Echo::get(const Echo* this, u32 key) { return "
         "this->base + key; } @generic<F: fn(u32) -> u32> u32 run(const F* f) { return "
         "f(1u32); } u32 probe(const Echo* s) { fn u32 read(u32 k) move(s) { return s->get(k); } "
         "return run(&read); }",
         NULL},
        /* E11: shared-receiver overloads on a mutable place. */
        {"mutable-receiver-overloads",
         "struct M { i32 v; }; i32 M::scaled(const M* this, i32 by) { return this->v * by; } "
         "i64 M::scaled(const M* this, i64 by) { return (this->v as i64) * by; } "
         "i32 f() { M m = {.v = 3}; return m.scaled(2); }",
         NULL},
        {"error-set-field",
         "@generic<E: errors> struct S { E v; }; i32 f() { return 0; }",
         "R-DIAG-TYPE-001"},
        /* E7: reflection over a type parameter in a dependent condition and bound. */
        {"dependent-reflection",
         "struct P { i32 a; i32 b; }; @generic<T> struct S { u8[core::field_count::<T>()] v; }; "
         "@generic<T> usize c() { @if (core::field_count::<T>() > 1usize) { return 2usize; } "
         "@else { return 1usize; } } usize probe() { S<P> s = {}; return c::<P>() + len(s.v); }",
         NULL},
        {"dependent-reflection-non-struct",
         "@generic<T> usize c() { @if (core::field_count::<T>() > 1usize) { return 2usize; } "
         "@else { return 1usize; } } usize probe() { return c::<u8>(); }",
         "R-DIAG-TYPE-001"},
        /* E8, E9: a method of a const object and a const array as a slice in constants. */
        {"constant-method-and-slice",
         "struct L { u32 lo; u32 hi; }; u32 L::span(const L* this) { return this->hi - this->lo; "
         "} const L LIM = L { .lo = 1u32, .hi = 5u32 }; const u32 SPAN = LIM.span(); "
         "u32 sum(const u32[] v) { return v[0] + v[1]; } const u32[2] T = {1u32, 2u32}; "
         "const u32 TOTAL = sum(&T); u32 probe() { u8[LIM.span() as usize] a = {}; "
         "return SPAN + TOTAL + len(a) as u32; }",
         NULL},
        /* E10: a static initializer of a generic definition is evaluated by each instance. */
        {"generic-static-runtime-argument",
         "@generic<T> usize scaled(usize by) { return sizeof(T) * by; } @generic<T> usize "
         "padded(usize by) { static const usize S = scaled::<T>(by); return S; } "
         "usize probe() { return padded::<u32>(2usize); }",
         "R-DIAG-CONST-002"},
        {"generic-static-instance-panic",
         "@generic<T> usize checked() { if (sizeof(T) > 4usize) { panic(\"big\"); } return "
         "sizeof(T); } @generic<T> usize padded() { static const usize S = checked::<T>(); "
         "return S; } usize probe() { return padded::<u64>(); }",
         "R-DIAG-CONST-003"},
        /* E2: disjoint fields may be borrowed at the same time; overlapping ones may not. */
        {"disjoint-field-borrows",
         "struct T { i32 v; }; struct H { T a; T b; }; i32 f() { H h = {.a = T {.v = 1}, .b = "
         "T {.v = 2}}; T* b = &h.b; T* a = &h.a; a->v = 3; h.a.v += 1; b->v = 4; return h.a.v "
         "+ h.b.v; }",
         NULL},
        {"overlapping-field-borrow",
         "struct T { i32 v; }; struct H { T a; T b; }; i32 f() { H h = {.a = T {.v = 1}, .b = "
         "T {.v = 2}}; T* a = &h.a; h.a.v = 5; a->v = 1; return h.a.v; }",
         "R-DIAG-BORROW-001"},
        {"whole-object-borrow",
         "struct T { i32 v; }; struct H { T a; T b; }; i32 f() { H h = {.a = T {.v = 1}, .b = "
         "T {.v = 2}}; H* w = &h; h.a.v = 5; w->b.v = 1; return h.a.v; }",
         "R-DIAG-BORROW-001"},
        {"merged-field-borrow",
         "struct T { i32 v; }; struct H { T a; T b; }; i32 f(bool c) { H h = {.a = T {.v = 1}, "
         ".b = T {.v = 2}}; T* p = &h.b; if (c == true) { p = &h.a; } h.a.v = 5; p->v = 1; "
         "return h.a.v; }",
         "R-DIAG-BORROW-001"},
        {"loop-carried-field-borrow",
         "struct T { i32 v; }; struct H { T a; T b; }; i32 f(bool c) { H h = {.a = T {.v = 1}, "
         ".b = T {.v = 2}}; T* p = &h.b; while (c == true) { h.a.v = 5; p = &h.a; } p->v = 1; "
         "return h.a.v; }",
         "R-DIAG-BORROW-001"},
        {"nested-field-borrow",
         "struct T { i32 v; }; struct H { T a; T b; }; i32 f() { H h = {.a = T {.v = 1}, .b = "
         "T {.v = 2}}; i32* v = &h.a.v; h.a = T {.v = 3}; *v = 1; return h.a.v; }",
         "R-DIAG-BORROW-001"},
        /* F1: `@discardable` does not follow a function item into a local. */
        {"discardable-direct-call",
         "@discardable u32 bump(u32* p) { *p += 1u32; return *p; } u32 probe() { u32 x = 0u32; "
         "bump(&x); return x; }",
         NULL},
        {"discardable-item-call",
         "@discardable u32 bump(u32* p) { *p += 1u32; return *p; } u32 probe() { u32 x = 0u32; "
         "auto b = bump; b(&x); return x; }",
         "R-DIAG-TYPE-001"},
        {"discardable-item-method-call",
         "@discardable u32 bump(u32* p) { *p += 1u32; return *p; } u32 probe() { u32 x = 0u32; "
         "auto b = bump; b.call(&x); return x; }",
         "R-DIAG-TYPE-001"},
        /* F2: an overloaded trait method does not form a function item. */
        {"overloaded-trait-method-item",
         "trait Report { i32 base(const Self* this); i32 twice(const Self* this) { return "
         "this->base() * 2; } i32 twice(const Self* this, i32 k) { return this->base() * 2 + k; "
         "} }; struct P { i32 v; }; impl Report for P { i32 base(const P* this) { return "
         "this->v; } }; i32 probe() { P p = {.v = 1}; auto t = P::twice; return t(&p); }",
         "R-DIAG-TYPE-001"},
        {"trait-method-item",
         "trait Report { i32 base(const Self* this); i32 twice(const Self* this) { return "
         "this->base() * 2; } }; struct P { i32 v; }; impl Report for P { i32 base(const P* "
         "this) { return this->v; } }; i32 probe() { P p = {.v = 1}; auto t = P::twice; return "
         "t(&p); }",
         NULL},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char source[2048];
        const int length =
            snprintf(source, sizeof(source), "module test.l6_regressions; %s", cases[i].source);
        RFrontendContext *context = r_frontend_create(NULL);
        R_SEMANTIC_CHECK(context != NULL && length > 0 && (size_t)length < sizeof(source));
        if (context == NULL)
            return;
        (void)r_semantic_add_bytes(context, cases[i].name, (const uint8_t *)source, (size_t)length);
        const RFrontendStatus status = r_frontend_analyze(context);
        const bool valid =
            status == (cases[i].diagnostic == NULL ? R_FRONTEND_OK : R_FRONTEND_INVALID_SOURCE) &&
            (cases[i].diagnostic == NULL ||
             r_semantic_diagnostic_code_count(context, cases[i].diagnostic) != 0U) &&
            r_semantic_diagnostic_code_count(context, "R-DIAG-SLICE-001") == 0U;
        if (!valid) {
            (void)fprintf(stderr, "l6 regression %s status %d\n", cases[i].name, (int)status);
            for (size_t d = 0U; d < context->diagnostic_count; ++d)
                (void)fprintf(stderr,
                              "%s: %s\n",
                              context->diagnostics[d].code,
                              context->diagnostics[d].message);
        }
        R_SEMANTIC_CHECK(valid);
        r_frontend_destroy(context);
    }
    /* F3, F4: programs of two modules. */
    static const struct {
        const char *name;
        const char *first;
        const char *second;
        const char *diagnostic;
    } modules[] = {
        {"import-cycle",
         "module l6.a; import l6.b; i32 from_a() { return 1; } i32 probe() { return "
         "l6.b::from_b(); }",
         "module l6.b; import l6.a; i32 from_b() { return l6.a::from_a() + 1; }",
         "R-DIAG-MOD-001"},
        {"import-chain",
         "module l6.a; import l6.b; i32 probe() { return l6.b::from_b(); }",
         "module l6.b; i32 from_b() { return 2; }",
         NULL},
        {"plain-import-trait",
         "module l6.a; import l6.b; struct S { u32 v; }; impl Shape for S { u32 area(const S* "
         "this) { return this->v; } };",
         "module l6.b; trait Shape { u32 area(const Self* this); };",
         "R-DIAG-TRAIT-001"},
        {"plain-import-constraint",
         "module l6.a; import l6.b; @generic<T: Shape> u32 measure(const T* s) { return "
         "s->area(); }",
         "module l6.b; trait Shape { u32 area(const Self* this); };",
         "R-DIAG-TYPE-001"},
        {"qualified-trait",
         "module l6.a; import l6.b; struct S { u32 v; }; impl l6.b::Shape for S { u32 area(const "
         "S* this) { return this->v; } }; @generic<T: l6.b::Shape> u32 measure(const T* s) { "
         "return s->area(); }",
         "module l6.b; trait Shape { u32 area(const Self* this); };",
         NULL},
        {"selected-trait",
         "module l6.a; import l6.b::{Shape}; struct S { u32 v; }; impl Shape for S { u32 "
         "area(const S* this) { return this->v; } }; @generic<T: Shape> u32 measure(const T* s) "
         "{ return s->area(); }",
         "module l6.b; trait Shape { u32 area(const Self* this); };",
         NULL},
    };
    for (size_t i = 0U; i < sizeof(modules) / sizeof(modules[0]); ++i) {
        RFrontendContext *context = r_frontend_create(NULL);
        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL)
            return;
        (void)r_semantic_add_bytes(
            context, "a.r", (const uint8_t *)modules[i].first, strlen(modules[i].first));
        (void)r_semantic_add_bytes(
            context, "b.r", (const uint8_t *)modules[i].second, strlen(modules[i].second));
        const RFrontendStatus status = r_frontend_analyze(context);
        const bool valid =
            status == (modules[i].diagnostic == NULL ? R_FRONTEND_OK : R_FRONTEND_INVALID_SOURCE) &&
            (modules[i].diagnostic == NULL ||
             r_semantic_diagnostic_code_count(context, modules[i].diagnostic) != 0U) &&
            r_semantic_diagnostic_code_count(context, "R-DIAG-SLICE-001") == 0U;
        if (!valid) {
            (void)fprintf(
                stderr, "l6 module regression %s status %d\n", modules[i].name, (int)status);
            for (size_t d = 0U; d < context->diagnostic_count; ++d)
                (void)fprintf(stderr,
                              "%s: %s\n",
                              context->diagnostics[d].code,
                              context->diagnostics[d].message);
        }
        R_SEMANTIC_CHECK(valid);
        r_frontend_destroy(context);
    }
    /* G3, G7, G8: a rejected program names its actual error, without a misleading companion. */
    static const struct {
        const char *name;
        const char *source;
        const char *diagnostic;
        const char *absent;
    } messages[] = {
        {"async-out-parameter",
         "async void fill(out i32 r) { r = 1; } i32 f() { return 0; }",
         "R-DIAG-TYPE-001",
         "R-DIAG-ASYNC-001"},
        {"out-read-before-initialization",
         "void make(out i32 r) { i32 x = r; r = x; } i32 f() { i32 v = 0; make(out v); return v; "
         "}",
         "R-DIAG-USE-001",
         "R-DIAG-MOVE-002"},
        {"copy-out-some-branches",
         "void make(out i32 r, bool c) { if (c == true) { r = 1; } } i32 f() { i32 v = 0; "
         "make(out v, true); return v; }",
         "R-DIAG-FLOW-001",
         "R-DIAG-MOVE-003"},
        {"copy-out-branch-then-assign",
         "void make(out i32 r, bool c) { if (c == true) { r = 1; } r = 2; } i32 f() { i32 v = 0; "
         "make(out v, true); return v; }",
         NULL,
         NULL},
        {"unresolved-type-behind-borrow",
         "void show(const Missing* p) { p as void; } i32 f() { return 0; }",
         "R-DIAG-NAME-001",
         NULL},
        {"unresolved-error-type",
         "i32 g() throws Missing { return 0; } i32 f() { try { return g(); } catch (Missing m) { "
         "m as void; return 1; } }",
         "R-DIAG-NAME-001",
         "R-DIAG-EFFECT-002"},
        {"unknown-standard-module",
         "import std.nothing; i32 f() { return 0; }",
         "R-DIAG-MOD-001",
         NULL},
        {"qualified-name-not-imported",
         "i32 f() { return elsewhere.api::value(); }",
         "R-DIAG-NAME-001",
         NULL},
        {"callable-associated-type",
         "trait Pipeline { type Step: fn(i32) -> i32 & copy; }; i32 f() { return 0; }",
         "R-DIAG-TRAIT-001",
         "R-DIAG-SYN-001"},
        {"reserved-c-function",
         "@header(\"unistd.h\") extern \"C\" { @safety(\"L6-EXIT\", \"Terminates the "
         "process\") void _exit(c_int status); } i32 f() { return 0; }",
         NULL,
         "R-DIAG-FFI-003"},
    };
    for (size_t i = 0U; i < sizeof(messages) / sizeof(messages[0]); ++i) {
        char source[1024];
        const int length =
            snprintf(source, sizeof(source), "module test.l6_messages; %s", messages[i].source);
        RFrontendContext *context = r_frontend_create(NULL);
        R_SEMANTIC_CHECK(context != NULL && length > 0 && (size_t)length < sizeof(source));
        if (context == NULL)
            return;
        (void)r_semantic_add_bytes(
            context, messages[i].name, (const uint8_t *)source, (size_t)length);
        const RFrontendStatus status = r_frontend_analyze(context);
        const bool valid =
            status ==
                (messages[i].diagnostic == NULL ? R_FRONTEND_OK : R_FRONTEND_INVALID_SOURCE) &&
            (messages[i].diagnostic == NULL ||
             r_semantic_diagnostic_code_count(context, messages[i].diagnostic) != 0U) &&
            (messages[i].absent == NULL ||
             r_semantic_diagnostic_code_count(context, messages[i].absent) == 0U) &&
            r_semantic_diagnostic_code_count(context, "R-DIAG-SLICE-001") == 0U;
        if (!valid) {
            (void)fprintf(stderr, "l6 message %s status %d\n", messages[i].name, (int)status);
            for (size_t d = 0U; d < context->diagnostic_count; ++d)
                (void)fprintf(stderr,
                              "%s: %s\n",
                              context->diagnostics[d].code,
                              context->diagnostics[d].message);
        }
        R_SEMANTIC_CHECK(valid);
        r_frontend_destroy(context);
    }
    /* G3: module-scope values that still grow after the documented discovery passes. */
    {
        char chain[8192];
        size_t used = (size_t)snprintf(chain,
                                       sizeof(chain),
                                       "module test.l6_discovery; i32 one() { return 1; } @if "
                                       "(one() == 1) { const i32 A1 = 1; } ");
        for (int level = 2; level <= 40 && used < sizeof(chain); ++level)
            used += (size_t)snprintf(chain + used,
                                     sizeof(chain) - used,
                                     "@if (A%d + one() == %d) { const i32 A%d = %d; } ",
                                     level - 1,
                                     level,
                                     level,
                                     level);
        R_SEMANTIC_CHECK(used < sizeof(chain));
        RFrontendContext *context = r_frontend_create(NULL);
        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL || used >= sizeof(chain))
            return;
        (void)r_semantic_add_bytes(context, "discovery-limit", (const uint8_t *)chain, used);
        R_SEMANTIC_CHECK(r_frontend_analyze(context) == R_FRONTEND_INVALID_SOURCE);
        R_SEMANTIC_CHECK(r_semantic_diagnostic_code_count(context, "R-DIAG-LIMIT-001") != 0U);
        r_frontend_destroy(context);
    }
}

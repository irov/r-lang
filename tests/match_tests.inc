static void r_semantic_test_match_expressions(void) {
    static const struct {
        const char *source;
        bool accepted;
    } cases[] = {
        {"i32 f(bool x) { return match(x) { case true: 1; case false: 2; }; }", true},
        {"i32 f(bool x) { return match(x) { case true: 1; }; }", false},
        {"i32 f(i32 x) { return match(x) { case 0: 1; default: 2; }; }", true},
        {"i32 f(i32 x) { return match(x) { case 0: 1; case 1: 2; }; }", false},
        {"enum E { A=2, B=7 }; i32 f(E x) { return match(x) { case variant E::A: 1; case variant "
         "E::B: 2; }; }",
         true},
        {"enum E { A, B }; i32 f(E x) { return match(x) { case variant E::A: 1; }; }", false},
        {"i32 f(o<o<i32>> x) { return match(x) { case variant o::none: 0; case variant "
         "o::some(variant o::none): 1; case variant o::some(variant o::some(n)): n; }; }",
         true},
        {"i32 f(o<o<i32>> x) { return match(x) { case variant o::none: 0; case variant "
         "o::some(variant o::some(n)): n; }; }",
         false},
        {"i32 f(o<i32> x) { return match(x) { case variant o::none: 0; case variant o::some(n) if "
         "(n > 0): n; }; }",
         false},
        {"i32 f(o<i32> x) { return match(x) { case variant o::some(n) if (n > 0): n; case _: 0; }; "
         "}",
         true},
        {"struct Pair { bool a; bool b; }; i32 f(Pair x) { return match(x) { case {.a=true}: 1; "
         "case {.a=false, .b=true}: 2; case {.a=false, .b=false}: 3; }; }",
         true},
        {"struct Pair { bool a; bool b; }; i32 f(Pair x) { return match(x) { case "
         "{.a=true,.b=true}: 1; case {.a=false,.b=false}: 2; }; }",
         false},
        {"struct Pair { i32 a; i32 b; }; i32 f(Pair x) { return match(x) { case {.a=n,.b=n}: n; }; "
         "}",
         false},
        {"struct Pair { i32 a; }; i32 f(Pair x) { return match(x) { case {.a=n,.a=m}: n; }; }",
         false},
        {"struct Pair { i32 a; }; i32 f(Pair x) { return match(x) { case {.bad=n}: n; }; }", false},
        {"i32 f(i32 x) { return match(x) { case {.a=n}: n; }; }", false},
        {"i32 f(o<i32> x) { return match(x) { case variant o::none(n): n; default: 0; }; }", false},
        {"i32 f(o<i32> x) { return match(x) { case variant o::some(n): n; default: 0u64; }; }",
         false},
        {"i32 f(o<i32> x) { return match(x) { case variant o::some(n): n; default: 0; }; } i32 g() "
         "{ return n; }",
         false},
        {"i32 f(i32 x) { return match(x) { case x: x; }; }", false},
        {"i32 set(i32* p) { *p=3; return 3; } i32 f(o<i32> x) { return match(x) { case variant "
         "o::some(n): set(&n); default: 0; }; }",
         false},
        {"enum E { A(own i32*) }; own i32* f(E x) { return match(x) { case variant E::A(move p): "
         "move p; }; }",
         false},
        {"enum E { A(own i32*) }; own i32* f(E x) { return match(move x) { case variant E::A(move "
         "p): move p; }; }",
         true},
        {"enum E { A(own i32*) }; drop(E* self) {} own i32* f(E x) { return match(move x) { case "
         "variant E::A(move p): move p; }; }",
         false},
        {"struct P { own i32* p; }; const i32* f(P x) { return match(move x) { case {.p=p}: &*p; "
         "}; }",
         false},
        {"bool consume(own i32* p) { return true; } i32 f(o<own i32*> x) { return match(move x) { "
         "case variant o::some(move p) if (consume(move p) == true): 1; default: 0; }; }",
         false},
        {"bool consume(own i32* p) { return false; } i32 f(bool x, own i32* p) { i32 n=match(x) { "
         "case true if (consume(move p) == true): 1; default: 0; }; return *p+n; }",
         false},
        {"@generic<T: copy> T f(o<T> x,T fallback) { return match(x) { case variant o::some(n): n; "
         "default: fallback; }; }",
         true},
        {"@generic<T> T f(o<T> x,T fallback) { return match(move x) { case variant o::some(move "
         "n): move n; default: move fallback; }; }",
         false},
        {"@generic<T> enum Box { Some(T) }; @generic<T> T f(Box<T> value) { return match(move "
         "value) { case variant Box<T>::Some(move n): move n; }; }",
         true},
        {"i32 f(bool x) { auto n=match(x) { case true: panic(\"bad\"); case false: 2; }; return n; "
         "}",
         true},
        {"i32 f(bool x) { return match(x) { case true: 2; case false: panic(\"bad\"); }; }", true},
        {"i32 f(bool x, own i32* p) { i32 n=match(x) { case true: panic(\"bad\"); case false: *p; "
         "}; return n + *p; }",
         true},
        {"struct P { const i32* a; const i32* b; }; const i32* f(P p) { return match(p) { case "
         "{.b=n}: n; }; }",
         true},
        {"struct P { const i32* a; }; const i32* f() { i32 x=1; P p={.a=&x}; return match(p) { "
         "case {.a=n}: n; }; }",
         false},
        {"@generic<T> enum Box { Some(T) }; @generic<T> T f(Box<T> value) { return match(move "
         "value) { case variant Box<T>::Some(move n): move n; }; } const i32* g(const i32* p) { "
         "Box<const i32*> x=Box<const i32*>::Some(p); return f(move x); }",
         true},
        {"i32 f(i32 x) { return match(x) { case n: move n; }; }", true},
        {"i32 f(i32 x) { return match(x) { case n: n=3; }; }", false},
        {"own i32* f(own i32* x) { return match(move x) { case n: move n; }; }", false},
        {"bool use(own i32* p) { return true; } i32 f(own i32* x) { return match(move x) { case "
         "move n if (use(move n) == true): 1; default: 0; }; }",
         false},
        {"i32 match(i32 x) { return x; } i32 f() { return match(3); }", true},
        {"i32 f(bool x) { i32 chosen = (x == true) ? 1 : panic(\"bad\"); return chosen; }",
         true},
        {"struct P { const i32* a; }; P make(const i32* x) { return P{.a=x}; } const i32* f(const "
         "i32* x) { return match(make(x)) { case {.a=n}: n; }; }",
         true},
        {"struct P { const i32[] a; }; P make(const i32[] x) { return P{.a=x}; } const i32[] "
         "f(const i32[] x) { return match(make(x)) { case {.a=n}: n; }; }",
         true},
        {"enum E { A(const i32*) }; const i32* f(const i32* x) { return match(E::A(x)) { case "
         "variant E::A(n): n; }; }",
         true},
        {"enum E { A(const i32[]) }; const i32[] f(const i32[] x) { return match(E::A(x)) { case "
         "variant E::A(n): n; }; }",
         true},
        {"struct P { i32[2] a; }; const i32[] f() { return match(P{.a={1,2}}) { case {.a=n}: "
         "n[0..2]; }; }",
         false},
        {"struct P { const i32* view; std.string::string text; }; usize f(P p) { return match(move "
         "p) { case {.text=text}: text.len(); }; }",
         true},
        {"struct P { const i32* view; std.string::string text; }; str f(P p) { return match(move "
         "p) { case {.text=text}: text.as_str(); }; }",
         false},
        {"enum P { Data { const i32* view; std.string::string text; } }; usize f(P p) { return "
         "match(move p) { case variant P::Data {.text=text}: text.len(); }; }",
         true},
        {"enum P { Data { const i32* view; std.string::string text; } }; str f(P p) { return "
         "match(move p) { case variant P::Data {.text=text}: text.as_str(); }; }",
         false},
        {"trait Measure { usize size(const Self* this); }; @generic<T: Measure> enum Box { Some(T) "
         "}; @generic<T: Measure> usize measure(Box<T> box) { return match(move box) { case "
         "variant Box<T>::Some(value): value.size(); }; }",
         true},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char source[2048];
        const int length =
            snprintf(source, sizeof(source), "module test.patterns; %s", cases[i].source);
        RFrontendContext *context = r_frontend_create(NULL);
        R_SEMANTIC_CHECK(context != NULL && length > 0 && (size_t)length < sizeof(source));
        if (context == NULL)
            return;
        (void)r_semantic_add_bytes(
            context, "match-contract.r", (const uint8_t *)source, (size_t)length);
        const RFrontendStatus status = r_frontend_analyze(context);
        const bool valid =
            status == (cases[i].accepted ? R_FRONTEND_OK : R_FRONTEND_INVALID_SOURCE);
        if (!valid) {
            (void)fprintf(stderr,
                          "match case %zu status %d expected %d\n",
                          i,
                          (int)status,
                          cases[i].accepted);
            for (size_t d = 0U; d < context->diagnostic_count; ++d)
                (void)fprintf(stderr,
                              "%s: %s\n",
                              context->diagnostics[d].code,
                              context->diagnostics[d].message);
        }
        R_SEMANTIC_CHECK(valid);
        r_frontend_destroy(context);
    }
}

static RFrontendContext *r_semantic_prepare_composition_oom(RSemanticTestAllocator *allocator) {
    static const char source_text[] =
        "module test.composition_oom; "
        "@generic<T: copy> trait Read { T read(const Self* this); "
        "T again(const Self* this) { return this->read(); } }; "
        "struct Number { i32 value; }; "
        "impl Read<i32> for Number { i32 read(const Number* this) { return this->value; } }; "
        "opaque(Read<i32>) make(i32 value) { return Number{.value=value}; } "
        "@generic<T: Read<i32>> i32 read(const T* value) { return value->again(); } "
        "@generic<T> enum Box { Some(T) }; "
        "@generic<T> T bounce(T value) { Box<T> box=Box<T>::Some(move value); "
        "return match(move box) { case variant Box<T>::Some(move item): move item; }; } "
        "enum Packet { Empty, Data { i32 amount; o<i32> extra; } }; "
        "i32 score(Packet packet) { return match(packet) { case variant Packet::Empty: 0; "
        "case variant Packet::Data{.amount=n,.extra=variant o::some(x)} if(x>3): n+x; "
        "case variant Packet::Data{.amount=n}: n; }; } "
        "@scoped async i32 child(const i32* p) { return *p; } "
        "async i32 grouped() throws std.async::start_error { i32 input=21; "
        "task_scope(2) g { auto a=child(&input); auto b=child(&input); "
        "usize selected=await g.first(&a,&b); await g.all(); "
        "return await move a + await move b; } } "
        "i32 main() { auto value=bounce(make(20)); "
        "return read(&value)+score(Packet::Data{.amount=18,.extra=o::some(4)}); }";
    RFrontendOptions options = r_frontend_default_options();
    options.allocate = r_semantic_test_allocate;
    options.free = r_semantic_test_free;
    options.allocator_user_data = allocator;
    RFrontendContext *context = r_frontend_create(&options);
    if (context == NULL)
        return NULL;
    const RSourceId source = r_semantic_add_bytes(
        context, "composition-oom.r", (const uint8_t *)source_text, sizeof(source_text) - 1U);
    RAstNodeId ast;
    if (source == 0U || r_frontend_lower_ast(context, source, &ast) != R_FRONTEND_OK) {
        r_frontend_destroy(context);
        return NULL;
    }
    return context;
}

static void r_semantic_test_composition_oom(void) {
    RSemanticTestAllocator baseline_allocator = {0};
    RFrontendContext *baseline = r_semantic_prepare_composition_oom(&baseline_allocator);
    R_SEMANTIC_CHECK(baseline != NULL);
    if (baseline == NULL)
        return;
    const size_t before = baseline_allocator.call_count;
    R_SEMANTIC_CHECK(r_frontend_analyze(baseline) == R_FRONTEND_OK);
    const size_t allocations = baseline_allocator.call_count - before;
    r_frontend_destroy(baseline);
    R_SEMANTIC_CHECK(baseline_allocator.live_count == 0U && allocations != 0U);
    for (size_t offset = 1U; offset <= allocations; ++offset) {
        RSemanticTestAllocator allocator = {0};
        RFrontendContext *context = r_semantic_prepare_composition_oom(&allocator);
        R_SEMANTIC_CHECK(context != NULL);
        if (context == NULL)
            continue;
        allocator.fail_at = allocator.call_count + offset;
        const RFrontendStatus status = r_frontend_analyze(context);
        if (status != R_FRONTEND_OUT_OF_MEMORY && status != R_FRONTEND_LIMIT_EXCEEDED) {
            (void)fprintf(stderr,
                          "composition allocation %zu/%zu: status %d\n",
                          offset,
                          allocations,
                          (int)status);
        }
        R_SEMANTIC_CHECK(status == R_FRONTEND_OUT_OF_MEMORY || status == R_FRONTEND_LIMIT_EXCEEDED);
        R_SEMANTIC_CHECK(r_frontend_hir_root(context) == R_HIR_NODE_ID_INVALID);
        r_frontend_destroy(context);
        R_SEMANTIC_CHECK(allocator.live_count == 0U);
    }
}

static void r_semantic_test_opaque_result_contracts(void) {
    static const struct {
        const char *source;
        bool accepted;
    } cases[] = {
        {"opaque(copy) make() { return 42; }", true},
        {"opaque(copy) make(bool b) { if (b == true) { return 1; } return 2u64; }", false},
        {"opaque(copy) make() { own i32* p=new i32(1); return move p; }", false},
        {"trait A {}; opaque(A) make() { return 1; }", false},
        {"opaque(copy) make() { return 42; } i32 main() { auto v=make(); return v+1; }", false},
        {"struct P { i32 value; }; opaque(copy) make() { return P {.value=42}; } "
         "i32 main() { auto v=make(); return v.value; }",
         false},
        {"struct P { i32 value; }; opaque(copy) make() { return P {.value=42}; } "
         "i32 use(P p) { return p.value; } i32 main() { auto v=make(); return use(v); }",
         false},
        {"opaque(unborrowed) make() { return 42; } "
         "i32 main() { auto v=make(); auto copy=v; return 0; }",
         false},
        {"opaque(copy) make();", false},
        {"const opaque(copy) make() { return 42; }", false},
        {"i32 use(opaque(copy) value) { return 0; }", false},
        {"struct P { opaque(copy) value; };", false},
        {"i32 main() { opaque(copy) value=42; return 0; }", false},
        {"opaque(copy) escape() { i32 local=42; return &local; }", false},
        {"opaque(fn() -> i32) escape() { i32 local=42; fn i32 read() { return local; } return move "
         "read; }",
         false},
        {"opaque(fn() -> i32 & copy) make() { i32 value=42; "
         "fn i32 read() move(value) { return value; } return read; }",
         true},
        {"@generic<T> opaque(copy) make(T value) { return move value; }", false},
        {"@generic<T: copy> opaque(copy) make(T value) { return value; }", true},
        {"opaque(send & unborrowed) make() { return 42; } "
         "@generic<T: copy> i32 use(T value) { return 0; } "
         "i32 main() { auto value=make(); return use(move value); }",
         false},
        {"trait A { type Item; }; opaque(A) make() { return 42; }", false},
        {"trait A { type Item; }; struct P {}; impl A for P { type Item=i32; }; "
         "opaque(A & Item = u64) make() { return P {}; }",
         false},
        {"trait A { type Item; }; struct P {}; impl A for P { type Item=i32; }; "
         "opaque(A & Item = i32 & Item = i32) make() { return P {}; }",
         false},
        {"trait A { type Item; }; struct P {}; impl A for P { type Item=i32; }; "
         "opaque(A & Item = i32) make() { return P {}; }",
         true},
        {"error E {}; opaque(copy) missing() throws E { throw E {}; }", false},
        {"@generic<T: unborrowed> opaque(unborrowed) make(T value) { "
         "@if (T is copy) { return value; } @else { return move value; } }",
         true},
    };
    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        char source[2048];
        const int length = snprintf(
            source, sizeof(source), "module test.hidden_contract; %s", cases[index].source);
        RFrontendContext *context = r_frontend_create(NULL);
        R_SEMANTIC_CHECK(context != NULL && length > 0 && (size_t)length < sizeof(source));
        if (context == NULL)
            return;
        (void)r_semantic_add_bytes(
            context, "opaque-contract.r", (const uint8_t *)source, (size_t)length);
        const RFrontendStatus status = r_frontend_analyze(context);
        const bool valid =
            status == (cases[index].accepted ? R_FRONTEND_OK : R_FRONTEND_INVALID_SOURCE);
        if (!valid) {
            (void)fprintf(stderr, "opaque contract case %zu: status %d\n", index, (int)status);
            for (size_t diagnostic = 0U; diagnostic < context->diagnostic_count; ++diagnostic)
                (void)fprintf(stderr,
                              "%s: %s\n",
                              context->diagnostics[diagnostic].code,
                              context->diagnostics[diagnostic].message);
        }
        R_SEMANTIC_CHECK(valid);
        r_frontend_destroy(context);
    }
}

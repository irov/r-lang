static void r_semantic_test_extended_trait_contracts(void) {
    static const struct {
        const char *source;
        bool accepted;
    } cases[] = {
        {"trait A { i32 read(const Self* this) { return this->missing; } };", false},
        {"trait A { i32 read(const Self* this) { return 42; } }; struct P {}; impl A for P {};",
         true},
        {"trait A : A {};", false},
        {"trait A : B {}; trait B : A {};", false},
        {"trait A {}; trait B : A {}; struct P {}; impl B for P {};", false},
        {"trait A {}; trait B : A {}; struct P {}; impl A for P {}; impl B for P {};", true},
        {"trait A : copy {}; struct P { own i32* value; }; impl A for P {};", false},
        {"trait A : copy {}; struct P {}; impl A for P {};", true},
        {"trait A { type Item: copy; }; struct P {}; impl A for P { type Item=own i32*; };", false},
        {"trait A { type Item: copy; }; struct P {}; impl A for P { type Item=i32; };", true},
        {"trait Key {}; trait A { type Item: Key; }; struct P {}; impl A for P { type Item=i32; };",
         false},
        {"trait Key {}; trait A { type Item: Key; }; struct P {}; impl Key for i32 {}; "
         "impl A for P { type Item=i32; };",
         true},
        {"trait A { type Item; Self::Item read(const Self* this); "
         "Self::Item twice(const Self* this) { Self::Item v=this->read(); "
         "Self::Item copy=v; return move copy; } };",
         false},
        {"trait A { type Item: copy; Self::Item read(const Self* this); "
         "Self::Item twice(const Self* this) { Self::Item v=this->read(); "
         "Self::Item copy=v; return copy; } };",
         true},
        {"trait A { @noalloc i32 value(const Self* this) { own i32* p=new i32(1); return *p; } };",
         false},
        {"trait A { @noalloc @nonblocking i32 value(const Self* this) { return 42; } };", true},
        {"error E {}; trait A { i32 value(const Self* this) { throw E {}; } };", false},
        {"error E {}; trait A { i32 value(const Self* this) throws E { throw E {}; } };", true},
        {"struct P {}; trait A { @noalloc i32 read(const Self* this); }; "
         "impl A for P { i32 read(const P* this) { own i32* p=new i32(42); return *p; } };",
         false},
        {"@generic<T: copy> trait A {}; struct P {}; impl A<own i32*> for P {};", false},
        {"@generic<T: copy> trait A {}; struct P {}; impl A<i32> for P {};", true},
        {"@generic<T> trait A {}; struct P {}; impl A for P {};", false},
        {"@generic<T> trait A {}; struct P {}; impl A<i32,u32> for P {};", false},
        {"@generic<T> trait A {}; struct P {}; impl A<void> for P {};", false},
        {"@generic<T> trait A { T wrong(const Self* this, T value) { return value+1; } };", false},
        {"@generic<T> trait A {}; struct P {}; impl A<bytes> for P {}; impl A<array<u8>> for P {};",
         false},
        {"@generic<T> trait A {}; struct P {}; impl A<const i32> for P {};", false},
        {"@generic<T> trait A : B<T> {}; @generic<T> trait B : A<T> {};", false},
        {"@generic<T: copy> trait A { T read(const Self* this); }; "
         "@generic<F: A<i32> & A<u64>> i32 bad(const F* value) { return value->read(); }",
         false},
        {"@generic<T: copy> trait A { T read(const Self* this); T again(const Self* this) { return "
         "this->read(); } }; "
         "struct P {}; impl A<i32> for P { i32 read(const P* this) { return 1; } }; "
         "impl A<u64> for P { u64 read(const P* this) { return 1u64; } }; "
         "i32 main() { P p={}; return p.again(); }",
         false},
        {"@generic<I: core::Iterator, U, F: fn(I::Item) -> U> "
         "struct Map { I inner; const F* function; }; "
         "@generic<I: core::Iterator, U, F: fn(I::Item) -> U> "
         "Map<I,U,F> map(I inner, const F* function) { "
         "return Map<I,U,F> {.inner=move inner, .function=function}; }",
         true},
    };
    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        char source[2048];
        const int length =
            snprintf(source, sizeof(source), "module test.trait_contract; %s", cases[index].source);
        RFrontendContext *context = r_frontend_create(NULL);
        R_SEMANTIC_CHECK(context != NULL && length > 0 && (size_t)length < sizeof(source));
        if (context == NULL)
            return;
        (void)r_semantic_add_bytes(
            context, "trait-contract.r", (const uint8_t *)source, (size_t)length);
        const RFrontendStatus status = r_frontend_analyze(context);
        const bool valid =
            status == (cases[index].accepted ? R_FRONTEND_OK : R_FRONTEND_INVALID_SOURCE);
        if (!valid) {
            (void)fprintf(stderr, "trait contract case %zu: status %d\n", index, (int)status);
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

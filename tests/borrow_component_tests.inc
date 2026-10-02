static void r_semantic_test_borrow_component_contracts(void) {
    static const struct {
        const char *source;
        bool accepted;
    } cases[] = {
        {"struct Pair { const i32* first; const i32* second; }; "
         "Pair view(const i32* a, const i32* b) { return Pair {.first=a,.second=b}; } "
         "i32 main() { own i32* a=new i32(20); own i32* b=new i32(22); "
         "Pair p=view(&*a,&*b); i32 n=*p.first; const i32* q=p.second; "
         "drop a; return n+*q-42; }",
         true},
        {"struct Pair { const i32* first; const i32* second; }; "
         "Pair view(const i32* a, const i32* b) { return Pair {.first=a,.second=a}; } "
         "i32 main() { own i32* a=new i32(20); own i32* b=new i32(22); "
         "Pair p=view(&*a,&*b); drop a; return *p.second; }",
         false},
        {"struct Pair { const i32* first; const i32* second; }; "
         "Pair view(bool choose, const i32* a, const i32* b) { "
         "if (choose == true) { return Pair {.first=a,.second=b}; } "
         "return Pair {.first=b,.second=a}; } "
         "i32 main() { own i32* a=new i32(20); own i32* b=new i32(22); "
         "Pair p=view(false,&*a,&*b); drop a; return *p.second; }",
         false},
        {"struct Pair { const i32* first; const i32* second; }; "
         "Pair view(const i32* a) { i32 temporary=22; "
         "return Pair {.first=a,.second=&temporary}; }",
         false},
        {"struct Pair { i32* first; i32* second; }; "
         "Pair view(i32* a, i32* b) { return Pair {.first=move a,.second=move b}; } "
         "struct State { i32 first; i32 second; }; "
         "i32 main() { State s={.first=20,.second=22}; Pair p=view(&s.first,&s.second); "
         "*p.first += 1; *p.second += 1; return *p.first+*p.second-44; }",
         true},
        {"struct Pair { i32* first; i32* second; }; "
         "Pair view(i32* a, i32* b) { return Pair {.first=move a,.second=move b}; } "
         "i32 main() { i32 a=20; Pair p=view(&a,&a); return *p.first; }",
         false},
        {"error Pair { const i32* first; const i32* second; }; "
         "void fail(const i32* a, const i32* b) throws Pair { "
         "throw Pair {.first=a,.second=a}; } "
         "i32 main() { own i32* a=new i32(20); own i32* b=new i32(22); "
         "try { fail(&*a,&*b); } catch(Pair p) { drop a; return *p.second; } return 1; }",
         false},
        {"@generic<T> struct Pair { T first; T second; }; "
         "@generic<T: copy> Pair<T> view(T a, T b) { return Pair<T> {.first=a,.second=a}; } "
         "i32 main() { own i32* a=new i32(20); own i32* b=new i32(22); "
         "const i32* av=&*a; const i32* bv=&*b; Pair<const i32*> p=view(av,bv); "
         "drop a; return *p.second; }",
         false},
        {"struct Pair { const i32* first; const i32* second; }; "
         "const i32* choose(Pair p) { return p.second; } "
         "i32 main() { own i32* a=new i32(20); own i32* b=new i32(22); "
         "Pair p={.first=&*a,.second=&*b}; const i32* q=choose(p); "
         "drop a; return *q-22; }",
         true},
        {"struct Pair { const i32* first; const i32* second; }; "
         "const i32* choose(Pair p) { return p.first; } "
         "i32 main() { own i32* a=new i32(20); own i32* b=new i32(22); "
         "Pair p={.first=&*a,.second=&*b}; const i32* q=choose(p); "
         "drop a; return *q-22; }",
         false},
        {"struct Pair { const i32* first; const i32* second; }; "
         "struct Outer { Pair pair; }; "
         "Outer view(const i32* a, const i32* b) { "
         "return Outer {.pair=Pair {.first=a,.second=b}}; } "
         "i32 main() { own i32* a=new i32(20); own i32* b=new i32(22); "
         "Outer p=view(&*a,&*b); const i32* q=p.pair.second; "
         "drop a; return *q-22; }",
         true},
        {"struct Pair { const i32* first; const i32* second; }; "
         "struct Outer { Pair pair; }; "
         "Outer view(const i32* a, const i32* b) { "
         "return Outer {.pair=Pair {.first=a,.second=a}}; } "
         "i32 main() { own i32* a=new i32(20); own i32* b=new i32(22); "
         "Outer p=view(&*a,&*b); const i32* q=p.pair.second; "
         "drop a; return *q-22; }",
         false},
        {"(const i32*)[2] view(const i32* a, const i32* b) { (const i32*)[2] p={a,b}; return p; } "
         "i32 main() { own i32* a=new i32(20); own i32* b=new i32(22); "
         "(const i32*)[2] p=view(&*a,&*b); const i32* q=p[1]; "
         "drop a; return *q-22; }",
         true},
        {"(const i32*)[2] view(const i32* a, const i32* b) { (const i32*)[2] p={a,a}; return p; } "
         "i32 main() { own i32* a=new i32(20); own i32* b=new i32(22); "
         "(const i32*)[2] p=view(&*a,&*b); const i32* q=p[1]; "
         "drop a; return *q-22; }",
         false},
        {"(const i32*)[2] view(const i32* a) { i32 temporary=22; "
         "(const i32*)[2] p={a,&temporary}; return p; }",
         false},
        {"struct Pair { (const i32*)[2] entries; }; "
         "const i32* choose(Pair p) { return p.entries[1]; } "
         "i32 main() { own i32* a=new i32(20); own i32* b=new i32(22); "
         "Pair p={.entries={&*a,&*b}}; const i32* q=choose(p); "
         "drop a; return *q-22; }",
         true},
        {"struct Pair { const i32* first; const i32* second; }; "
         "struct Outer { Pair pair; }; "
         "Pair extract(Outer value) { return value.pair; } "
         "i32 main() { own i32* a=new i32(20); own i32* b=new i32(22); "
         "Outer p={.pair=Pair {.first=&*a,.second=&*b}}; "
         "Pair inner=extract(p); const i32* q=inner.second; "
         "drop a; return *q-22; }",
         true},
        {"struct Pair { const i32* first; const i32* second; }; "
         "struct Outer { Pair pair; }; "
         "i32 main() { own i32* a=new i32(20); own i32* b=new i32(22); "
         "Outer p={.pair=Pair {.first=&*a,.second=&*b}}; "
         "Pair inner=p.pair; const i32* q=inner.second; "
         "drop a; return *q-22; }",
         true},
        {"@generic<T: copy> struct Pair { T first; T second; }; "
         "@generic<T: copy> struct Outer { Pair<T> pair; }; "
         "@generic<T: copy> Pair<T> extract(Outer<T> value) { return value.pair; } "
         "i32 main() { own i32* a=new i32(20); own i32* b=new i32(22); "
         "Outer<const i32*> p={.pair=Pair<const i32*> {.first=&*a,.second=&*b}}; "
         "Pair<const i32*> inner=extract(p); const i32* q=inner.second; "
         "drop a; return *q-22; }",
         true},
        {"@generic<T: copy> struct Pair { T first; T second; }; "
         "@generic<T: copy> struct Outer { Pair<T> pair; }; "
         "@generic<T: copy> Pair<T> extract(Outer<T> value) { return value.pair; } "
         "i32 main() { own i32* a=new i32(20); own i32* b=new i32(22); "
         "Outer<const i32*> p={.pair=Pair<const i32*> {.first=&*a,.second=&*a}}; "
         "Pair<const i32*> inner=extract(p); const i32* q=inner.second; "
         "drop a; return *q-22; }",
         false},
        {"struct Pair { const i32* first; const i32* second; }; "
         "Pair copy(Pair input) { return Pair {.first=input.first,.second=input.second}; } "
         "i32 main() { own i32* a=new i32(20); own i32* b=new i32(22); "
         "Pair p={.first=&*a,.second=&*b}; Pair copied=copy(p); "
         "const i32* q=copied.second; drop a; return *q-22; }",
         true},
        {"struct View { const i32* pointer; }; "
         "View weaken(i32* pointer) { const i32* shared=pointer; return View {.pointer=shared}; } "
         "i32 main() { i32 value=42; View view=weaken(&value); return *view.pointer-42; }",
         true},
        {"@generic<T: copy> void exchange(T[] items) { "
         "T saved=items[0]; items[0]=items[1]; items[1]=saved; } "
         "i32 main() { i32[2] items={20,22}; exchange(&items); return items[0]-22; }",
         true},
        {"struct Pair { const i32* first; const i32* second; }; "
         "i32 main() { own i32* a=new i32(20); own i32* b=new i32(22); "
         "Pair p={.first=&*a,.second=&*b}; drop a; return *p.second-22; }",
         true},
        {"struct Pair { const i32* first; const i32* second; }; "
         "struct Outer { Pair pair; }; "
         "i32 main() { own i32* a=new i32(20); own i32* b=new i32(22); "
         "Outer p={.pair=Pair {.first=&*a,.second=&*b}}; "
         "drop a; return *p.pair.second-22; }",
         true},
        {"i32 main() { own i32* a=new i32(20); own i32* b=new i32(22); "
         "(const i32*)[2] p={&*a,&*b}; drop a; return *p[1]-22; }",
         true},
        {"struct Pair { const i32* first; const i32* second; }; "
         "struct Outer { Pair pair; }; "
         "i32 main() { own i32* a=new i32(20); own i32* b=new i32(22); "
         "Outer p={.pair=Pair {.first=&*a,.second=&*b}}; "
         "drop a; return *p.pair.first-20; }",
         false},
        {"i32 main() { own i32* a=new i32(20); own i32* b=new i32(22); "
         "(const i32*)[2] p={&*a,&*b}; drop a; return *p[0]-20; }",
         false},
        {"i32 main() { own i32* a=new i32(20); own i32* b=new i32(22); "
         "(const i32*)[2] p={&*a,&*b}; usize index=1usize; drop a; return *p[index]-22; }",
         false},
    };
    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        char source[2048];
        const int length =
            snprintf(source, sizeof(source), "module semantic.components; %s", cases[index].source);
        RFrontendContext *context = r_frontend_create(NULL);
        R_SEMANTIC_CHECK(context != NULL && length > 0 && (size_t)length < sizeof(source));
        if (context == NULL)
            return;
        (void)r_semantic_add_bytes(
            context, "borrow-components.r", (const uint8_t *)source, (size_t)length);
        const RFrontendStatus status = r_frontend_analyze(context);
        bool correct =
            cases[index].accepted
                ? status == R_FRONTEND_OK && r_frontend_lower_mir(context) == R_FRONTEND_OK
                : status == R_FRONTEND_INVALID_SOURCE &&
                      (r_semantic_diagnostic_code_count(context, "R-DIAG-BORROW-001") != 0U ||
                       r_semantic_diagnostic_code_count(context, "R-DIAG-BORROW-002") != 0U);
        if (!correct) {
            (void)fprintf(stderr, "borrow component case %zu: status %d\n", index, (int)status);
            for (size_t diagnostic = 0U; diagnostic < context->diagnostic_count; ++diagnostic)
                (void)fprintf(stderr,
                              "%s: %s\n",
                              context->diagnostics[diagnostic].code,
                              context->diagnostics[diagnostic].message);
        }
        R_SEMANTIC_CHECK(correct);
        r_frontend_destroy(context);
    }
}

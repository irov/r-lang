/* L27 (docs/language-completeness-roadmap.ru.md): derived implementations (R-AGG-0012) without
   std.cmp, conditional key and clone hooks (R-FUNC-0009), implementations over standard type
   constructors (R-TYPE-0042), the borrowed switch of a Move o place (R-STMT-0010, L27-1) and the
   match of an enum without payloads (L27-3). Each case is accepted, or is rejected with the
   diagnostic code and, where given, the rule it names. */
static void r_semantic_test_l27_regressions(void) {
    static const struct {
        const char *name;
        const char *source;
        const char *diagnostic;
    } cases[] = {
        {"derive-key-struct",
         "@derive(key) struct P { i32 x; u8 y; }; "
         "bool f() { P a = {.x = 1, .y = 2u8}; P b = {.x = 1, .y = 2u8}; "
         "return core::key_equal(&a, &b) && core::hash(&a) == core::hash(&b); }",
         NULL},
        {"derive-key-enum",
         "@derive(key) enum E { a(i32), b { u32 w; u32 h; }, c }; "
         "bool f() { E x = E::b {.w = 1u32, .h = 2u32}; return core::key_equal(&x, &x); }",
         NULL},
        {"derive-key-generic",
         "@generic<T> @derive(key) struct Box { T value; }; "
         "bool f() { Box<u32> a = {.value = 1u32}; return core::key_equal(&a, &a); }",
         NULL},
        {"derive-key-generic-instance-without-key",
         "struct Plain { i32 v; }; @generic<T> @derive(key) struct Box { T value; }; "
         "bool f() { Box<Plain> a = {.value = Plain {.v = 1}}; return core::key_equal(&a, &a); }",
         "R-DIAG-TYPE-001 [R-FUNC-0009]"},
        {"derive-key-field",
         "struct Plain { i32 v; }; @derive(key) struct P { Plain inner; };",
         "R-DIAG-TRAIT-001 [R-AGG-0012]"},
        {"derive-clone-structural",
         "@derive(clone) struct P { std.string::string name; array<u8> data; }; "
         "usize f(const P* p) throws std.alloc::alloc_error { P c = core::clone(p); "
         "return c.name.len() + len(c.data); }",
         NULL},
        {"derive-clone-generic",
         "@generic<T> @derive(clone) struct Box { T value; }; "
         "usize f(const (Box<std.string::string>)* p) throws std.alloc::alloc_error { "
         "Box<std.string::string> c = core::clone(p); return c.value.len(); }",
         NULL},
        {"derive-clone-view-field",
         "@derive(clone) struct P { std.string::string name; str alias; };",
         "R-DIAG-TRAIT-001 [R-AGG-0012]"},
        {"derive-clone-and-hook",
         "@derive(clone) struct P { i32 x; }; P P::clone(const P* value) { return *value; }",
         "R-DIAG-NAME-002 [R-AGG-0012]"},
        {"derive-unknown", "@derive(hash) struct P { i32 x; };", "R-DIAG-SYN-002 [R-AGG-0012]"},
        {"derive-repeated",
         "@derive(key, key) struct P { i32 x; };",
         "R-DIAG-SYN-002 [R-AGG-0012]"},
        {"derive-empty", "@derive() struct P { i32 x; };", "R-DIAG-SYN-002 [R-AGG-0012]"},
        {"derive-family-base",
         "@derive(key) error base { i32 code; }; error member : base { u32 extra; };",
         "R-DIAG-TYPE-001 [R-AGG-0012]"},
        {"derive-family-member",
         "error base { i32 code; }; @derive(key, clone) error member : base { u32 extra; }; "
         "bool f() { member a = {.code = 1, .extra = 2u32}; return core::key_equal(&a, &a); }",
         NULL},
        {"hook-condition-clone",
         "@generic<T> struct Box { T value; }; "
         "@generic<T: clone & unborrowed> Box<T> Box<T>::clone(const Box<T>* value) "
         "throws std.alloc::alloc_error { return Box<T> {.value = core::clone(&value->value)}; } "
         "usize f(const (Box<std.string::string>)* p) throws std.alloc::alloc_error { "
         "Box<std.string::string> c = core::clone(p); return c.value.len(); }",
         NULL},
        {"hook-condition-drop-exact",
         "@generic<T> struct Box { T value; }; @generic<T: copy> drop(Box<T>* self) { }",
         "R-DIAG-TYPE-001 [R-INIT-0009]"},
        {"structural-option",
         "trait Same { bool same(const Self* this, const Self* other); }; "
         "impl Same for i32 { bool same(const i32* this, const i32* other) { return *this == "
         "*other; } }; "
         "@generic<T: Same> impl Same for o<T> { bool same(const (o<T>)* this, const (o<T>)* "
         "other) { "
         "switch (*this) { case variant o::some(l): switch (*other) { case variant o::some(r): "
         "return l->same(r); case variant o::none: return false; } case variant o::none: "
         "switch (*other) { case variant o::some(_): return false; case variant o::none: return "
         "true; } } "
         "return false; } }; "
         "@generic<T: Same> bool both(const T* a, const T* b) { return a->same(b); } "
         "bool f() { o<o<i32>> a = o::some(o::some(1)); return both(&a, &a); }",
         NULL},
        {"structural-array",
         "trait Size { usize size(const Self* this); }; "
         "@generic<T, const usize N> impl Size for T[N] { usize size(const (T[N])* this) { return "
         "N; } }; "
         "@generic<T> impl Size for const T[] { usize size(const (const T[])* this) { return "
         "len(*this); } }; "
         "usize f() { i32[3] a = {1, 2, 3}; const i32[] s = a[0usize..2usize]; return a.size() + "
         "s.size(); }",
         NULL},
        {"structural-constraint",
         "trait Same { bool same(const Self* this, const Self* other); }; struct Plain { i32 v; }; "
         "@generic<T: Same> impl Same for o<T> { bool same(const (o<T>)* this, const (o<T>)* "
         "other) { "
         "return true; } }; @generic<T: Same> bool both(const T* a, const T* b) { return "
         "a->same(b); } "
         "bool f() { o<Plain> a = o::none; return both(&a, &a); }",
         "R-DIAG-TYPE-001 [R-TYPE-0043]"},
        {"switch-borrowed-option",
         "usize f(const (o<std.string::string>)* p) { switch (*p) { "
         "case variant o::some(text): return text->len(); case variant o::none: return 0usize; } "
         "return 0usize; }",
         NULL},
        {"switch-borrowed-local-option",
         "usize f() throws std.alloc::alloc_error { o<std.string::string> p = "
         "o::some(std.string::from_str(\"ab\")); usize n = 0usize; switch (p) { "
         "case variant o::some(text): n = text->len(); case variant o::none: n = 1usize; } "
         "switch (move p) { case variant o::some(move owned): n += owned.len(); "
         "case variant o::none: n += 2usize; } return n; }",
         NULL},
        {"switch-borrowed-option-no-move-binding",
         "usize f(const (o<std.string::string>)* p) { switch (*p) { "
         "case variant o::some(move text): return text.len(); case variant o::none: return 0usize; "
         "} "
         "return 0usize; }",
         "R-DIAG-MOVE-003 [R-STMT-0010]"},
        {"match-fieldless-enum",
         "enum Day { monday = 1, sunday = 7 }; u32 f(Day d) { return match (d) { "
         "case variant Day::sunday: 0u32; case variant Day::monday: 1u32; }; }",
         NULL},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char source[4096];
        const int length =
            snprintf(source, sizeof(source), "module test.l27_regressions; %s", cases[i].source);
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
            (void)fprintf(stderr, "l27 regression %s status %d\n", cases[i].name, (int)status);
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

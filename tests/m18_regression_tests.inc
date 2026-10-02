/* M18 (docs/language-completeness-roadmap.ru.md): the language parts of std.stream, std.bufio
   and std.console — implementations that repeat standard method names (R-NAME-0010), interface
   capabilities required by the traits (R-TYPE-0051), braced standard errors (R-SLIB-ERR-0003) —
   and the compiler defects M18-4..M18-8 found while writing them. Each case is accepted, or is
   rejected with the diagnostic code, the rule it names and a part of its message. */
static void r_semantic_test_m18_regressions(void) {
    static const char prelude[] =
        "trait Get : send & sync { u32 get(const Self* this); }; "
        "trait Plain { u32 peek(const Self* this); }; "
        "struct Value { u32 v; }; impl Get for Value { u32 get(const Value* this) { return "
        "this->v; } }; impl Plain for Value { u32 peek(const Value* this) { return this->v; } }; "
        "struct Pair { u32 a; bytes b; }; "
        "void touch(const u32* x, u8[] y) { if (len(y) > 0usize) { y[0] = *x as u8; } } "
        "void views(const u8[] x, u8[] y) { if (len(x) > 0usize && len(y) > 0usize) { y[0] = "
        "x[0]; } }";
    static const struct {
        const char *name;
        const char *source;
        const char *diagnostic;
        const char *message;
    } cases[] = {
        {"implementation-repeats-standard-method",
         "trait Sized { usize capacity(const Self* this); }; impl Sized for bytes { usize "
         "capacity(const bytes* this) { return 7usize; } }; @generic<T: Sized> usize traited("
         "const T* value) { return value->capacity(); } usize f() throws std.alloc::alloc_error "
         "{ bytes b = std.alloc::bytes(3usize, 0u8); usize inherent = b.capacity(); return "
         "inherent + traited(&b); }",
         NULL,
         NULL},
        {"interface-capabilities-from-trait",
         "@scoped async u32 f(const dyn(Get)* source) { return source->get(); }",
         NULL,
         NULL},
        {"interface-spelled-capabilities-same",
         "u32 f(const dyn(Get)* source) { const dyn(Get & send & sync)* same = source; return "
         "same->get(); } u32 g(const Value* value) { return f(value); }",
         NULL,
         NULL},
        {"interface-without-capabilities",
         "@scoped async u32 f(const dyn(Plain)* source) { return source->peek(); }",
         "R-DIAG-ASYNC-001",
         "shall be Send"},
        {"standard-errors-braced",
         "void f(i32 k) throws std.error::fault { throw (k == 1) std.io::io_error {.code = "
         "std.io::error_code::closed, .native_code = 1i64}; throw (k == 2) std.net::net_error "
         "{.code = std.net::error_code::closed, .native_code = 2i64}; throw (k == 3) "
         "std.fs::fs_error {.code = std.fs::error_code::not_found, .native_code = 3i64}; throw "
         "(k == 4) std.process::process_error {.code = std.process::error_code::not_found, "
         ".native_code = 4i64}; throw (k == 5) std.env::env_error {.code = "
         "std.env::error_code::other, .native_code = 5i64}; throw (k == 6) std.time::time_error "
         "{.code = std.time::error_code::other, .native_code = 6i64}; throw (k == 7) "
         "std.math::math_error {.code = std.math::error_code::domain, .native_code = 7i64}; "
         "throw (k == 8) std.error::error {.domain = std.error::domain::io, .code = 1u32, "
         ".native_code = 8i64}; throw (k == 9) std.bits::read_error {.code = "
         "std.bits::read_error_code::unexpected_end, .byte_index = 9usize}; }",
         NULL,
         NULL},
        {"standard-error-unknown-field",
         "void f() throws std.io::io_error { throw std.io::io_error {.code = "
         "std.io::error_code::closed, .reason = 1i64}; }",
         "R-DIAG-INIT-002 [R-INIT-0004]",
         "names an unknown field"},
        {"async-method-exclusive-receiver",
         "@scoped async u32 Value::bump(Value* this, u32 step) { this->v += step; return "
         "this->v; } async u32 f() throws std.async::start_error { Value value = {.v = 1u32}; u32 "
         "total = 0u32; task_scope(1) "
         "io { total += await value.bump(2u32); } return total; }",
         NULL,
         NULL},
        {"async-method-borrowed-receiver-unscoped",
         "async u32 Value::bump(Value* this, u32 step) { this->v += step; return this->v; }",
         "R-DIAG-ASYNC-001",
         NULL},
        {"disjoint-field-views",
         "void f(Pair* p) { touch(&p->a, p->b.as_slice_mut()); } void g() throws "
         "std.alloc::alloc_error { Pair pair = {.a = 1u32, .b = std.alloc::bytes(2usize, "
         "0u8)}; touch(&pair.a, pair.b.as_slice_mut()); }",
         NULL,
         NULL},
        {"overlapping-field-views",
         "void f(Pair* p) { views(p->b.as_slice(), p->b.as_slice_mut()); }",
         "R-DIAG-BORROW-001",
         "conflicting access"},
        {"await-in-loop-condition",
         "@scoped async bool step(Value* value) { value->v += 1u32; return value->v < 3u32; } "
         "async bool more(u32 v) { return v < 3u32; } async u32 f() throws std.async::start_error "
         "{ Value value = {.v = "
         "0u32}; task_scope(1) io { while (await step(&value) == true) {} } u32 n = 0u32; while "
         "(await more(n) == true) { n += 1u32; } return value.v + n; }",
         NULL,
         NULL},
        {"loop-scalar-store-through-parameter",
         "usize f(Pair* p) { usize scanned = 0usize; while (true) { o<usize> found = "
         "std.bytes::find(p->b.as_slice(), 10u8); switch (found) { case variant o::some(at): "
         "usize stop = scanned + *at; p->a = stop as u32; return stop; case variant o::none: "
         "scanned += 1usize; } if (p->a == 0u32) { return 0usize; } p->a -= 1u32; } }",
         NULL,
         NULL},
        {"generic-call-view-argument",
         "@generic<T: copy> struct Holder { T value; }; @generic<T: copy> usize "
         "Holder<T>::fill(const Holder<T>* this, u8[] target) { return len(target); } usize f() "
         "throws std.alloc::alloc_error { Holder<i32> holder = {.value = 1}; bytes b = "
         "std.alloc::bytes(3usize, 0u8); return holder.fill(b.as_slice_mut()); }",
         NULL,
         NULL},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char source[8192];
        const int length = snprintf(
            source, sizeof(source), "module test.m18_regressions; %s %s", prelude, cases[i].source);
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
        bool message_found = cases[i].message == NULL;
        for (size_t d = 0U; d < context->diagnostic_count; ++d) {
            if ((cases[i].message != NULL) &&
                (strstr(context->diagnostics[d].message, cases[i].message) != NULL)) {
                message_found = true;
            }
        }
        const bool valid =
            status == (cases[i].diagnostic == NULL ? R_FRONTEND_OK : R_FRONTEND_INVALID_SOURCE) &&
            (cases[i].diagnostic == NULL ||
             r_semantic_l14_diagnostic_count(context, cases[i].diagnostic) != 0U) &&
            message_found && r_semantic_diagnostic_code_count(context, "R-DIAG-SLICE-001") == 0U;
        if (!valid) {
            (void)fprintf(stderr, "m18 regression %s status %d\n", cases[i].name, (int)status);
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

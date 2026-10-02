/* M24 (docs/language-completeness-roadmap.ru.md): the attribute @test of R-FUNC-0025, the
   typing of the allocation operations of std.test (Library R-SLIB-TEST-0003), owned string keys
   (M24-2), append of call results (M24-5) and methods of standard types named through their type
   (M24-10). Each case is accepted, or is rejected with the diagnostic code, the rule it names and
   a part of its message. Test mode itself runs in the codegen case test_mode and in the
   regression test_mode_main. */
static void r_semantic_test_m24_regressions(void) {
    static const struct {
        const char *name;
        const char *source;
        const char *diagnostic;
        const char *message;
    } cases[] = {
        {"test-plain", "@test void t() { }", NULL, NULL},
        {"test-throws",
         "error E { i32 code; }; @test void t() throws E { throw E {.code = 1}; }",
         NULL,
         NULL},
        {"test-async", "@test async void t() { }", NULL, NULL},
        {"test-protected", "@test protected void t() { }", NULL, NULL},
        {"test-expect",
         "error E { i32 code; }; @test(expect = E) void t() throws E { throw E {.code = 1}; }",
         NULL,
         NULL},
        {"test-expect-member-of-declared-family",
         "@test(expect = std.convert::parse_error) void t() throws std.error::fault { }",
         NULL,
         NULL},
        {"test-expect-family-of-declared-member",
         "@test(expect = std.error::fault) void t() throws std.convert::parse_error { }",
         NULL,
         NULL},
        {"test-allocations",
         "@test(allocations) void t() throws std.alloc::alloc_error { }",
         NULL,
         NULL},
        {"test-called-as-a-function", "@test void t() { } void f() { t(); }", NULL, NULL},
        {"test-parameters",
         "@test void t(i32 value) { value as void; }",
         "R-DIAG-SYN-002 [R-FUNC-0025]",
         "without parameters"},
        {"test-result",
         "@test i32 t() { return 1; }",
         "R-DIAG-SYN-002 [R-FUNC-0025]",
         "whose result is void"},
        {"test-generic",
         "@generic<T> @test void t() { }",
         "R-DIAG-SYN-002 [R-FUNC-0025]",
         "generic parameters"},
        {"test-method",
         "struct S { i32 x; }; @test void S::m(const S* this) { this as void; }",
         "R-DIAG-SYN-002 [R-FUNC-0025]",
         "module-scope function"},
        {"test-drop",
         "struct D { i32 x; }; @test drop(D* self) { self as void; }",
         "R-DIAG-SYN-002 [R-FUNC-0025]",
         "module-scope function"},
        {"test-twice",
         "@test @test void t() { }",
         "R-DIAG-SYN-002 [R-FUNC-0025]",
         "invalid, duplicate, or misplaced @test attribute"},
        {"test-unknown-argument",
         "@test(sometimes) void t() { }",
         "R-DIAG-SYN-002 [R-FUNC-0025]",
         "invalid, duplicate, or misplaced @test attribute"},
        {"test-expect-undeclared",
         "@test(expect = std.convert::parse_error) void t() { }",
         "R-DIAG-SYN-002 [R-FUNC-0025]",
         "catches no error that the function declares"},
        {"test-expect-unrelated",
         "error E { i32 code; }; @test(expect = E) void t() throws std.convert::parse_error { }",
         "R-DIAG-SYN-002 [R-FUNC-0025]",
         "catches no error that the function declares"},
        {"test-expect-not-an-error",
         "@test(expect = i32) void t() throws std.convert::parse_error { }",
         "R-DIAG-SYN-002 [R-FUNC-0025]",
         "catches no error that the function declares"},
        {"test-allocations-async",
         "@test(allocations) async void t() throws std.alloc::alloc_error { }",
         "R-DIAG-SYN-002 [R-FUNC-0025]",
         "synchronous function"},
        {"allocation-operations",
         "u64 f() { std.test::fail_allocation_at(3u64); u64 attempts = "
         "std.test::allocation_attempts(); std.test::fail_allocation_at(0u64); return attempts; }",
         NULL,
         NULL},
        {"allocation-operations-noalloc",
         "@noalloc @nonblocking u64 f() { std.test::fail_allocation_at(0u64); return "
         "std.test::allocation_attempts(); }",
         NULL,
         NULL},
        {"fail-allocation-at-u64",
         "void f() { std.test::fail_allocation_at(1i32); }",
         "R-DIAG-TYPE-001 [R-SLIB-TEST-0003]",
         "the operand shall be u64"},
        {"allocation-attempts-no-argument",
         "u64 f() { return std.test::allocation_attempts(1u64); }",
         "R-DIAG-TYPE-001 [R-SLIB-TEST-0003]",
         "incorrect argument count"},
        {"string-dictionary-key",
         "usize f() { dict<std.string::string, i32> d = std.dict::create(); return len(d); }",
         NULL,
         NULL},
        {"append-function-result",
         "str label() { return \"x\"; } usize f() throws std.alloc::alloc_error { "
         "std.string::string s = std.string::create(); s.append(label()); return s.len(); }",
         NULL,
         NULL},
        {"append-char-function-result",
         "char letter() { return 'x'; } usize f() throws std.alloc::alloc_error { "
         "std.string::string s = std.string::create(); s.append(letter()); return s.len(); }",
         NULL,
         NULL},
        {"append-method-result",
         "struct N { str text; }; str N::get(const N* this) { return this->text; } "
         "usize f(const N* n) throws std.alloc::alloc_error { std.string::string s = "
         "std.string::create(); s.append(n->get()); s.append(N::get(n)); return s.len(); }",
         NULL,
         NULL},
        {"append-member-of-call-result",
         "struct H { str name; }; H h() { return H {.name = \"h\"}; } usize f() throws "
         "std.alloc::alloc_error { std.string::string s = std.string::create(); "
         "s.append(h().name); return s.len(); }",
         NULL,
         NULL},
        {"append-generic-results",
         "@generic<T> T same(T value) { return move value; } usize f() throws "
         "std.alloc::alloc_error, std.format::format_error { std.string::string s = "
         "std.string::create(); s.append(same::<str>(\"g\")); s.append(same('c')); "
         "std.format::builder b = std.format::with_capacity(8usize); b.append(same(42i32), "
         "10u32); b.append(same(1.5f64)); std.string::string t = (move b).finish(); "
         "return s.len() + t.len(); }",
         NULL,
         NULL},
        {"append-generic-result-without-overload",
         "@generic<T> T same(T value) { return move value; } void f() throws "
         "std.alloc::alloc_error { std.string::string s = std.string::create(); "
         "s.append(same(1.5f64)); }",
         "R-DIAG-TYPE-001 [R-FUNC-0004]",
         "no append overload accepts this value type"},
        {"append-unresolved-value",
         "void f() throws std.alloc::alloc_error { std.string::string s = "
         "std.string::create(); s.append(missing); }",
         "R-DIAG-NAME-001 [R-NAME-0003]",
         "unresolved ordinary name"},
        {"standard-method-through-type",
         "usize f() throws std.alloc::alloc_error { std.string::string s = std.string::create(); "
         "std.string::string::append(&s, \"ab\"); std.string::string::append(&s, 'c'); "
         "return std.string::string::len(&s); }",
         NULL,
         NULL},
        {"standard-method-through-type-value-receiver",
         "i64 f() { std.time::duration d = std.time::duration_from_seconds(3i64); "
         "return std.time::duration::seconds(d); }",
         NULL,
         NULL},
        {"standard-method-through-type-receiver-position",
         "u64 f() throws std.bits::read_error { u8[2] data = {1u8, 2u8}; std.bits::lsb_reader "
         "reader = {}; return std.bits::lsb_reader::read(&reader, data, 4u8); }",
         NULL,
         NULL},
        {"standard-method-through-type-wrong-receiver",
         "usize f() { i32 x = 1; return std.string::string::len(&x); }",
         "R-DIAG-TYPE-001 [R-NAME-0010]",
         "not a receiver of the type that owns this method"},
        {"standard-method-through-type-without-receiver",
         "usize f() { return std.string::string::len(); }",
         "R-DIAG-TYPE-001 [R-NAME-0010]",
         "takes the receiver as its first argument"},
        {"switch-after-unresolved-call",
         "enum K { a, b, c }; void f(K k) { switch (k) { case K::a: missing(); case K::b: "
         "break; case K::c: break; } }",
         "R-DIAG-NAME-001 [R-NAME-0003]",
         "called function name is unresolved"},
        {"switch-after-unresolved-value",
         "enum K { a, b, c }; i32 f(K k) { switch (k) { case K::a: i32 x = missing; return x; "
         "case K::b: return 1; case K::c: return 2; } }",
         "R-DIAG-NAME-001 [R-NAME-0003]",
         "unresolved ordinary name"},
        {"integer-switch-after-unresolved-call",
         "i32 f(i32 k) { switch (k) { case 1: missing(); case 2: return 2; default: return 0; } "
         "return 1; }",
         "R-DIAG-NAME-001 [R-NAME-0003]",
         "called function name is unresolved"},
        {"standard-method-through-type-unknown",
         "void f() throws std.alloc::alloc_error { std.string::string s = std.string::create(); "
         "std.string::string::frobnicate(&s); }",
         "R-DIAG-NAME-001 [R-NAME-0010]",
         "the standard type has no method of this name"},
    };
    /* M24-6: a part of a message that no diagnostic of the named case may contain. */
    static const struct {
        const char *name;
        const char *absent;
    } absences[] = {
        {"switch-after-unresolved-call", "switch"},
        {"switch-after-unresolved-value", "switch"},
        {"integer-switch-after-unresolved-call", "repeats a case value"},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char source[8192];
        const char *absent = NULL;
        for (size_t a = 0U; a < sizeof(absences) / sizeof(absences[0]); ++a) {
            if (strcmp(absences[a].name, cases[i].name) == 0) {
                absent = absences[a].absent;
            }
        }
        const int length =
            snprintf(source, sizeof(source), "module test.m24_regressions; %s", cases[i].source);
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
        bool absent_found = false;
        for (size_t d = 0U; d < context->diagnostic_count; ++d) {
            if ((cases[i].message != NULL) &&
                (strstr(context->diagnostics[d].message, cases[i].message) != NULL)) {
                message_found = true;
            }
            if ((absent != NULL) && (strstr(context->diagnostics[d].message, absent) != NULL)) {
                absent_found = true;
            }
        }
        const bool valid =
            status == (cases[i].diagnostic == NULL ? R_FRONTEND_OK : R_FRONTEND_INVALID_SOURCE) &&
            (cases[i].diagnostic == NULL ||
             r_semantic_l14_diagnostic_count(context, cases[i].diagnostic) != 0U) &&
            message_found && !absent_found &&
            r_semantic_diagnostic_code_count(context, "R-DIAG-SLICE-001") == 0U;
        if (!valid) {
            (void)fprintf(stderr, "m24 regression %s status %d\n", cases[i].name, (int)status);
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

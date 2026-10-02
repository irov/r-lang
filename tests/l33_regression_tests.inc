/* L33 (docs/language-completeness-roadmap.ru.md): field initializers (R-INIT-0004), default values
   and `@default` variants (R-INIT-0005), core::take of evaluated defaults (R-OWN-0019) and JSON
   defaults from initializers (R-JSON-0001). Each case is accepted, or is rejected with the
   diagnostic code, the rule it names and a part of its message. */
static void r_semantic_test_l33_regressions(void) {
    static const char prelude[] =
        "u32 make_count() { return 4u32; } std.string::string make_name() throws "
        "std.alloc::alloc_error { return std.string::from_str(\"n\"); } enum Level { low, "
        "@default medium, high };";
    static const struct {
        const char *name;
        const char *source;
        const char *diagnostic;
        const char *message;
    } cases[] = {
        {"literal-and-call",
         "struct S { u32 a = 3; u32 b = make_count(); Level level; }; u32 f() { S s = S {}; "
         "return s.a + s.b; }",
         NULL,
         NULL},
        {"allocating",
         "struct S { std.string::string name = make_name(); }; usize f() throws "
         "std.alloc::alloc_error { S s = S {}; return s.name.len(); }",
         NULL,
         NULL},
        {"explicit-overrides",
         "struct S { u32 a = make_count(); u32 b; }; u32 f() { S s = S {.a = 1u32, .b = 2u32}; "
         "return s.a + s.b; }",
         NULL,
         NULL},
        {"generic",
         "@generic<T> struct Box { o<T> value = o::none; u32 tag = 7; }; u32 f() { Box<i64> b = "
         "Box<i64> {}; return b.tag; }",
         NULL,
         NULL},
        {"nested-and-array",
         "struct Inner { u32 a = 1; }; struct Outer { Inner inner; Inner[2] pair; }; u32 f() { "
         "Outer outer = Outer {}; return outer.inner.a + outer.pair[1].a; }",
         NULL,
         NULL},
        {"enum-default-payload",
         "enum Mode { off, @default on(u32), manual { u32 step = 5; } }; u32 f() { Mode m = "
         "Mode::off; Mode old = core::take(&m); old as void; m as void; return 0u32; }",
         NULL,
         NULL},
        {"take-evaluated",
         "struct S { std.string::string name = make_name(); }; usize f() throws "
         "std.alloc::alloc_error { S s = S {}; S old = core::take(&s); return old.name.len() + "
         "s.name.len(); }",
         NULL,
         NULL},
        {"json-optional-initializers",
         "struct S { @json(optional) u32 retries = 3; @json(optional) std.string::string name = "
         "make_name(); @json(optional) bool on = true; };",
         NULL,
         NULL},
        {"type-mismatch",
         "struct S { u32 v = \"x\"; };",
         "R-DIAG-TYPE-001 [R-INIT-0004]",
         "field initializer type does not match"},
        {"names-a-local",
         "struct S { u32 a = count; }; u32 f() { u32 count = 3u32; S s = S {}; return s.a + "
         "count; }",
         "R-DIAG-NAME-001",
         NULL},
        {"names-a-field",
         "struct S { u32 a = 1; u32 b = a; };",
         "R-DIAG-NAME-001",
         NULL},
        {"undeclared-effect",
         "struct S { std.string::string name = make_name(); }; usize f() { S s = S {}; return "
         "s.name.len(); }",
         "R-DIAG-EFFECT-001 [R-ERR-0001]",
         "neither caught nor declared"},
        {"two-defaults",
         "enum E { @default a, @default b };",
         "R-DIAG-NAME-002 [R-INIT-0005]",
         "at most one @default variant"},
        {"other-variant-attribute",
         "enum E { @json(optional) a, b };",
         "R-DIAG-SYN-002 [R-GRAM-0007]",
         "takes only the attribute @default"},
        {"default-with-argument",
         "enum E { @default(x) a, b };",
         "R-DIAG-SYN-002 [R-GRAM-0007]",
         "takes only the attribute @default"},
        {"default-on-struct",
         "@default struct S { u32 a; };",
         "R-DIAG-SYN-002",
         NULL},
        {"take-without-default",
         "enum E { a, b }; E f() { E e = E::a; return core::take(&e); }",
         "R-DIAG-INIT-002 [R-OWN-0019]",
         "core::take requires a proven R default"},
        {"payload-without-default",
         "enum E { @default a(std.string::string), b }; E f(E e) { E copy = move e; return "
         "core::take(&copy); }",
         "R-DIAG-INIT-002 [R-OWN-0019]",
         "core::take requires a proven R default"},
        {"omitted-without-default",
         "struct S { std.string::string name; u32 a = 1; }; u32 f() { S s = S {}; return s.a; }",
         "R-DIAG-INIT-002 [R-INIT-0005]",
         "omits a non-default field"},
        {"json-initializer-constant",
         "struct S { @json(optional) u32 retries = 1 + 2; };",
         NULL,
         NULL},
        {"json-initializer-form",
         "struct S { @json(optional) u32 retries = make_count() + 1; };",
         "R-DIAG-JSON-001",
         "JSON default requires a scalar literal or a factory"},
        {"json-initializer-call-arguments",
         "u32 scaled(u32 v) { return v * 2u32; } struct S { @json(optional) u32 retries = "
         "scaled(2u32); };",
         "R-DIAG-JSON-001",
         "only as a literal or a call of a zero-argument factory"},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char source[8192];
        const int length = snprintf(
            source, sizeof(source), "module test.l33_regressions; %s %s", prelude, cases[i].source);
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
            (void)fprintf(stderr, "l33 regression %s status %d\n", cases[i].name, (int)status);
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

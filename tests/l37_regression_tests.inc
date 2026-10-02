/* L37 (docs/language-completeness-roadmap.ru.md): expression forms. Destructuring declarations
 * (R-STMT-0022), loop labels (R-STMT-0004), pattern tests in if and while conditions
 * (R-STMT-0002) and struct updates (R-INIT-0004). Each case is accepted, or is rejected with the
 * diagnostic code and the rule it names and, when given, a diagnostic whose message contains the
 * text. The run-time behaviour lives in the fixtures codegen_expression_forms.r and
 * codegen_async_for.r. */

static void r_semantic_test_l37_regressions(void) {
    static const char token[] = "struct T { i32 v; }; drop(T* self) { } ";
    static const struct {
        const char *name;
        bool token;
        const char *source;
        const char *diagnostic;
        const char *message;
    } cases[] = {
        {"destructure", false, "i32 f() { auto (a, b) = (1, 2); return a + b; }", NULL, NULL},
        {"destructure-place",
         false,
         "i32 f() { (i32, i32) pair = (1, 2); auto (a, b) = pair; return a + b + pair.0; }",
         NULL,
         NULL},
        {"destructure-moved",
         true,
         "i32 f() { (T, i32) pair = (T {.v = 1}, 2); auto (t, n) = move pair; drop t; "
         "return n; }",
         NULL,
         NULL},
        {"destructure-arity",
         false,
         "i32 f() { auto (a, b) = (1, 2, 3); return a + b; }",
         "R-DIAG-TYPE-001 [R-STMT-0022]",
         "every element"},
        {"destructure-not-tuple",
         false,
         "i32 f() { auto (a, b) = 5; return a + b; }",
         "R-DIAG-TYPE-001 [R-STMT-0022]",
         "elements of a tuple"},
        {"destructure-duplicate",
         false,
         "i32 f() { auto (a, a) = (1, 2); return a; }",
         "R-DIAG-NAME-002 [R-NAME-0004]",
         NULL},
        {"label-break-continue",
         false,
         "i32 f() { i32 n = 0; outer: for (i32 i = 0; i < 4; i += 1) { "
         "for (i32 j = 0; j < 4; j += 1) { if (j == 1) { continue outer; } "
         "if (i == 3) { break outer; } n += 1; } } return n; }",
         NULL,
         NULL},
        {"label-from-switch",
         false,
         "i32 f(i32 x) { i32 n = 0; scan: while (n < 9) { switch (x) { case 1: break scan; "
         "default: n += 1; } } return n; }",
         NULL,
         NULL},
        {"label-separate-namespace",
         false,
         "void f() { i32 a = 0; a: while (true) { a += 1; break a; } }",
         NULL,
         NULL},
        {"label-unknown",
         false,
         "void f() { while (true) { break nowhere; } }",
         "R-DIAG-FLOW-001 [R-STMT-0004]",
         "no enclosing loop"},
        {"label-after-its-loop",
         false,
         "void f() { a: while (false) { } while (true) { break a; } }",
         "R-DIAG-FLOW-001 [R-STMT-0004]",
         NULL},
        {"label-repeated",
         false,
         "void f() { a: while (true) { a: while (true) { break a; } } }",
         "R-DIAG-NAME-002 [R-STMT-0004]",
         NULL},
        {"label-leaves-finally",
         false,
         "void f() { a: while (true) { try { } finally { "
         "for (i32 i = 0; i < 2; i += 1) { continue a; } } } }",
         "R-DIAG-EFFECT-001 [R-ERR-0003]",
         "finally"},
        {"label-inside-finally",
         false,
         "void f() { try { } finally { a: for (i32 i = 0; i < 2; i += 1) { "
         "for (i32 j = 0; j < 2; j += 1) { break a; } } } }",
         NULL,
         NULL},
        {"label-moved-on-jump",
         true,
         "void f() { outer: for (i32 i = 0; i < 3; i += 1) { T t = T {.v = i}; "
         "for (i32 j = 0; j < 2; j += 1) { if (i == 1) { drop t; continue outer; } } "
         "drop t; } }",
         NULL,
         NULL},
        {"label-on-if",
         false,
         "void f() { a: if (true) { } }",
         "R-DIAG-SYN-001 [R-GRAM-0005]",
         NULL},
        {"pattern-if",
         false,
         "i32 f(o<i32> x) { if (x is variant o::some(v)) { return v; } return 0; }",
         NULL,
         NULL},
        {"pattern-while",
         false,
         "i32 f() { o<i32> c = o::some(3); i32 s = 0; while (c is variant o::some(v)) { "
         "s += v; c = o::none; } return s; }",
         NULL,
         NULL},
        {"pattern-literal", false, "i32 f(i32 x) { if (x is 3) { return 1; } return 0; }", NULL, NULL},
        {"pattern-move-temporary",
         true,
         "o<T> make() { return o::some(T {.v = 1}); } "
         "i32 f() { if (make() is variant o::some(move t)) { i32 v = t.v; drop t; return v; } "
         "return 0; }",
         NULL,
         NULL},
        {"pattern-move-place",
         true,
         "void f(o<T> x) { if (x is variant o::some(move t)) { drop t; } drop x; }",
         "R-DIAG-SWITCH-001 [R-EXPR-0031]",
         NULL},
        {"pattern-in-conjunction",
         false,
         "i32 f(o<i32> x) { if (x is variant o::some(v) && true) { return 1; } return 0; }",
         "R-DIAG-SYN-001 [R-STMT-0002]",
         "whole condition"},
        {"pattern-as-value",
         false,
         "bool f(o<i32> x) { return x is variant o::none; }",
         "R-DIAG-SYN-001 [R-GRAM-0005]",
         NULL},
        {"pattern-binding-in-else",
         false,
         "i32 f(o<i32> x) { if (x is variant o::some(v)) { return v; } else { return v; } }",
         "R-DIAG-NAME-001 [R-NAME-0003]",
         NULL},
        {"pattern-binding-after-while",
         false,
         "i32 f(o<i32> x) { while (x is variant o::some(v)) { return v; } return v; }",
         "R-DIAG-NAME-001 [R-NAME-0003]",
         NULL},
        {"pattern-binding-read-only",
         false,
         "void f(o<i32> x) { if (x is variant o::some(v)) { v = 3; } }",
         "R-DIAG-BORROW-001 [R-BORROW-0002]",
         NULL},
        {"outcome-borrowed",
         false,
         "i32 f(bytes data) { std.string::from_bytes_result r = std.string::from_bytes(move data); "
         "i32 n = 0; switch (r) { case variant std.string::from_bytes_result::valid(text): "
         "n = 1; case variant std.string::from_bytes_result::invalid(failure): "
         "if (failure->index == 1usize) { n = 2; } } drop r; return n; }",
         NULL,
         NULL},
        {"outcome-borrowed-move-binding",
         false,
         "void f(bytes data) { std.string::from_bytes_result r = std.string::from_bytes(move data); "
         "switch (r) { case variant std.string::from_bytes_result::valid(move text): drop text; "
         "case variant std.string::from_bytes_result::invalid(failure): } drop r; }",
         "R-DIAG-MOVE-003 [R-STMT-0010]",
         NULL},
        {"outcome-borrowed-consumed",
         false,
         "void f(bytes data) { std.string::from_bytes_result r = std.string::from_bytes(move data); "
         "switch (r) { case variant std.string::from_bytes_result::valid(text): drop r; "
         "case variant std.string::from_bytes_result::invalid(failure): } }",
         "R-DIAG-BORROW-001 [R-INIT-0011]",
         NULL},
        {"update",
         false,
         "struct V { u32 a; u32 b; }; V f(V v) { return V {.a = 1u32, ...v}; }",
         NULL,
         NULL},
        {"update-other-type",
         false,
         "struct V { u32 a; u32 b; }; struct W { u32 a; u32 b; }; "
         "V f(W w) { return V {.a = 1u32, ...w}; }",
         "R-DIAG-TYPE-001 [R-INIT-0004]",
         "type of the struct"},
        {"update-user-drop",
         true,
         "T f(T t) { return T {.v = 1, ...move t}; }",
         "R-DIAG-INIT-002 [R-INIT-0004]",
         "user drop"},
        {"update-not-last",
         false,
         "struct V { u32 a; u32 b; }; V f(V v) { return V {...v, .a = 1u32}; }",
         "R-DIAG-SYN-001 [R-GRAM-0005]",
         "last initializer item"},
        {"update-array",
         false,
         "u32[2] f(u32[2] v) { u32[2] w = {...v}; return w; }",
         "R-DIAG-INIT-002 [R-INIT-0004]",
         NULL},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char source[4096];
        const int length = snprintf(source,
                                    sizeof(source),
                                    "module test.l37_regressions; %s%s",
                                    cases[i].token ? token : "",
                                    cases[i].source);
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
            message_found = message_found || ((context->diagnostics[d].message != NULL) &&
                                              (strstr(context->diagnostics[d].message,
                                                      cases[i].message) != NULL));
        }
        const bool valid =
            (cases[i].diagnostic == NULL ? status == R_FRONTEND_OK
                                         : status != R_FRONTEND_OK) &&
            (cases[i].diagnostic == NULL ||
             r_semantic_l14_diagnostic_count(context, cases[i].diagnostic) != 0U) &&
            message_found;
        if (!valid) {
            (void)fprintf(stderr, "l37 regression %s status %d\n", cases[i].name, (int)status);
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

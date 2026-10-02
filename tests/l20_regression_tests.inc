/* L20 (docs/language-completeness-roadmap.ru.md): implicit break, throw arms, string labels and
 * case matchers. Each case is accepted, or is rejected with the diagnostic code and, where given,
 * the rule it names; no case may leave a construct outside the implemented slice
 * (R-DIAG-SLICE-001). */
#define R_L20_PARITY                                                                               \
    "struct Parity { i32 value; }; impl core::CaseMatcher for Parity { type Label = i32; bool "    \
    "matches(const Parity* this, i32 label) { return this->value % 2 == label; } }; "

static void r_semantic_test_l20_regressions(void) {
    static const struct {
        const char *name;
        const char *source;
        const char *diagnostic;
    } cases[] = {
        /* L20.1, R-STMT-0007: a clause without a terminator leaves the switch like break. */
        {"switch-implicit-break",
         "enum Color { red, green }; i32 f(Color c) { i32 total = 0; switch (c) { case "
         "Color::red: total = 1; case Color::green: total = 2; } return total; }",
         NULL},
        {"switch-implicit-break-default",
         "i32 f(i32 v) { i32 total = 0; switch (v) { case 1: total = 10; case 2: total = 20; "
         "fallthrough; default: total += 1; } return total; }",
         NULL},
        {"switch-explicit-break-remains",
         "i32 f(i32 v) { i32 total = 0; switch (v) { case 1: total = 1; break; default: total = "
         "2; break; } return total; }",
         NULL},
        {"switch-final-fallthrough",
         "i32 f(i32 v) { i32 total = 0; switch (v) { case 1: total = 1; default: total = 2; "
         "fallthrough; } return total; }",
         "R-DIAG-FLOW-001 [R-STMT-0007]"},
        {"select-implicit-break",
         "protected async u32 one() { return 1u32; } protected async u32 f() throws "
         "std.async::start_error { u32 r = 0u32; task_scope(1) group { auto a = one(); select "
         "(group) { case u32 value = await move a: r = value; } } return r; }",
         NULL},
        /* L20.2, R-EXPR-0031: a match arm may throw. */
        {"match-throw-arm",
         "enum Operation { abs, sqrt }; error Usage { i32 code; }; Operation f(o<Operation> "
         "parsed) throws Usage { return match (parsed) { case variant o::some(value): value; "
         "case variant o::none: throw Usage {.code = 1}; }; }",
         NULL},
        {"match-default-throw",
         "error Usage { i32 code; }; i32 f(i32 v) throws Usage { return match (v) { case 1: 10; "
         "default: throw Usage {.code = v}; }; }",
         NULL},
        {"match-rethrow-arm",
         "error Usage { i32 code; }; i32 f(i32 v) throws Usage { try { throw Usage {.code = v}; "
         "} catch (Usage failure) { i32 kept = match (failure.code) { case 1: 10; default: "
         "throw; }; return kept; } }",
         NULL},
        {"match-throw-arm-uncaught",
         "error Usage { i32 code; }; i32 f(i32 v) { return match (v) { case 1: 10; default: "
         "throw Usage {.code = v}; }; }",
         "R-DIAG-EFFECT-001"},
        {"match-conditional-throw-arm",
         "error Usage { i32 code; }; i32 f(i32 v) throws Usage { return match (v) { case 1: 10; "
         "default: throw (v == 2) Usage {.code = v}; }; }",
         "R-DIAG-SYN-001"},
        /* L20.3: string labels compare byte for byte. */
        {"str-match",
         "i32 f(str kind) { return match (kind) { case \"f32\": 1; case \"f64\": 2; default: 0; "
         "}; }",
         NULL},
        {"str-switch",
         "i32 f(str kind) { i32 code = 0; switch (kind) { case \"a\": code = 1; case \"b\": code "
         "= 2; fallthrough; default: code += 10; } return code; }",
         NULL},
        {"str-switch-computed",
         "import std.string; i32 f() throws std.alloc::alloc_error { std.string::string s = "
         "std.string::from_str(\"a\"); i32 code = 0; switch (s.as_str()) { case \"a\": code = 1; "
         "default: code = 2; } drop s; return code; }",
         NULL},
        {"str-switch-without-default",
         "i32 f(str kind) { i32 code = 0; switch (kind) { case \"a\": code = 1; } return code; }",
         "R-DIAG-SWITCH-001 [R-STMT-0006]"},
        {"str-switch-repeated-label",
         "i32 f(str kind) { i32 code = 0; switch (kind) { case \"a\": code = 1; case \"a\": code "
         "= 2; default: code = 3; } return code; }",
         "R-DIAG-SWITCH-001 [R-STMT-0006]"},
        {"str-match-repeated-label",
         "i32 f(str kind) { return match (kind) { case \"a\": 1; case \"a\": 2; default: 0; }; }",
         "R-DIAG-SWITCH-001 [R-EXPR-0031]"},
        {"str-match-without-default",
         "i32 f(str kind) { return match (kind) { case \"a\": 1; case \"b\": 2; }; }",
         "R-DIAG-SWITCH-001 [R-EXPR-0031]"},
        {"str-label-not-literal",
         "i32 f(str kind, str other) { return match (kind) { case (other): 1; default: 0; }; }",
         "R-DIAG-SWITCH-001"},
        /* L20.4: labels a core::CaseMatcher tests. */
        {"matcher-user-defined",
         R_L20_PARITY "i32 f(i32 v) { Parity p = {.value = v}; i32 code = 0; switch (p) { case "
                      "0: code = 1; case 1: code = 2; default: code = 3; } return code + match (p) "
                      "{ case 1: 10; default: 0; }; }",
         NULL},
        {"matcher-repeated-label",
         R_L20_PARITY "i32 f(i32 v) { Parity p = {.value = v}; return match (p) { case 1: 1; case "
                      "1: 2; default: 0; }; }",
         "R-DIAG-SWITCH-001 [R-EXPR-0031]"},
        {"matcher-without-default",
         R_L20_PARITY "i32 f(i32 v) { Parity p = {.value = v}; return match (p) { case 0: 1; case "
                      "1: 2; }; }",
         "R-DIAG-SWITCH-001 [R-EXPR-0031]"},
        {"matcher-label-type",
         R_L20_PARITY "i32 f(i32 v) { Parity p = {.value = v}; return match (p) { case \"one\": "
                      "1; default: 0; }; }",
         "R-DIAG-TYPE-001"},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char source[2048];
        const int length =
            snprintf(source, sizeof(source), "module test.l20_regressions; %s", cases[i].source);
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
            (void)fprintf(stderr, "l20 regression %s status %d\n", cases[i].name, (int)status);
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

#undef R_L20_PARITY

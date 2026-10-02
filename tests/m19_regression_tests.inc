/* M19 (docs/language-completeness-roadmap.ru.md): associated type constraints in the headers of
   generic functions (Core R-TYPE-0043) and the braced construction of std.convert::parse_error
   (Library R-SLIB-CONV-0001) that the R-source std.encoding uses. Each case is accepted, or is
   rejected with the diagnostic code, the rule it names and a part of its message. */
static void r_semantic_test_m19_regressions(void) {
    static const char prelude[] =
        "trait Ranked { i32 rank(const Self* this); }; "
        "impl Ranked for i32 { i32 rank(const i32* this) { return *this; } }; "
        "struct counter { i32 left; }; "
        "impl core::Iterator for counter { type Item = i32; o<i32> next(counter* this) { if "
        "(this->left == 0) { return o::none; } this->left -= 1; return o::some(this->left); } "
        "}; "
        "@generic<I: core::Iterator, I::Item: Ranked & copy> i32 top(I inner) { i32 best = 0; "
        "for (I::Item value in move inner) { if (value.rank() > best) { best = value.rank(); } "
        "} return best; }";
    static const struct {
        const char *name;
        const char *source;
        const char *diagnostic;
        const char *message;
    } cases[] = {
        {"projection-constraint-body",
         "i32 f() { return top(counter {.left = 4}); }",
         NULL,
         NULL},
        {"projection-through-supertrait",
         "trait Sub : core::Iterator {}; impl Sub for counter {}; @generic<I: Sub, I::Item: "
         "Ranked & copy> i32 through(I inner) { return top(move inner); } i32 f() { return "
         "through(counter {.left = 2}); }",
         NULL,
         NULL},
        {"projection-dependent-missing",
         "@generic<I: core::Iterator, I::Item: copy> i32 outer(I inner) { return top(move "
         "inner); }",
         "R-DIAG-TYPE-001 [R-TYPE-0043]",
         "does not implement the required trait"},
        {"projection-struct-header",
         "@generic<I: core::Iterator, I::Item: copy> struct holder { I inner; };",
         "R-DIAG-TYPE-001 [R-TYPE-0043]",
         "belongs to the header of a generic function"},
        {"projection-undeclared",
         "@generic<I: core::Iterator, I::Size: copy> usize none(I inner) { return 0usize; }",
         "R-DIAG-TRAIT-001 [R-TYPE-0043]",
         "no trait constraint of the parameter declares"},
        {"parse-error-braced",
         "u32 digits(str s) throws std.convert::parse_error { throw (len(s) == 0usize) "
         "std.convert::parse_error {.code = std.convert::parse_error_code::empty, .index = "
         "0usize}; return 1u32; }",
         NULL,
         NULL},
        {"parse-error-unknown-field",
         "std.convert::parse_error f() { return std.convert::parse_error {.code = "
         "std.convert::parse_error_code::empty, .offset = 0usize}; }",
         "R-DIAG-INIT-002 [R-INIT-0004]",
         "names an unknown field"},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char source[8192];
        const int length = snprintf(
            source, sizeof(source), "module test.m19_regressions; %s %s", prelude, cases[i].source);
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
            (void)fprintf(stderr, "m19 regression %s status %d\n", cases[i].name, (int)status);
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

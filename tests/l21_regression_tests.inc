/* L21 (docs/language-completeness-roadmap.ru.md): error inheritance and the root of the standard
 * errors. Each case is accepted, or is rejected with the diagnostic code and, where given, the
 * rule it names; no case may leave a construct outside the implemented slice (R-DIAG-SLICE-001). */
#define R_L21_FAMILY                                                                               \
    "error io_error { i32 code; }; error net_error : io_error { u16 port; }; error tls_error : "   \
    "net_error { i32 session; }; error disk_error : io_error { i32 block; }; "

static void r_semantic_test_l21_regressions(void) {
    static const struct {
        const char *name;
        const char *source;
        const char *diagnostic;
    } cases[] = {
        /* L21.1, R-AGG-0011: a parent's fields precede the error's own, named flat. */
        {"inherit-fields-flat",
         R_L21_FAMILY "tls_error f() { return tls_error {.code = 1, .port = 2u16, .session = 3}; }",
         NULL},
        {"empty-root",
         "error base_failure {}; error net_failure : base_failure { u16 port; }; i32 "
         "f(base_failure "
         "failure) { try { throw move failure; } catch (net_failure n) { return n.port as i32; } "
         "catch (base_failure b) { return 0; } }",
         NULL},
        {"parent-not-error",
         "struct Plain { i32 x; }; error bad_error : Plain { i32 y; };",
         "R-DIAG-TYPE-001 [R-AGG-0011]"},
        {"parent-enum-error",
         "error code_error : u8 { first, second }; error bad_error : code_error { i32 y; };",
         "R-DIAG-TYPE-001 [R-AGG-0011]"},
        {"parent-unresolved", "error bad_error : missing_error { i32 y; };", "R-DIAG-NAME-001"},
        {"parent-cycle",
         "error a_error : b_error { i32 x; }; error b_error : a_error { i32 y; };",
         "R-DIAG-TYPE-001 [R-AGG-0011]"},
        {"parent-self", "error a_error : a_error { i32 x; };", "R-DIAG-TYPE-001 [R-AGG-0011]"},
        {"inherited-field-repeated",
         "error a_error { i32 code; }; error b_error : a_error { i32 code; };",
         "R-DIAG-NAME-002 [R-NAME-0004]"},
        {"family-member-views",
         "error text_error { str text; }; error line_error : text_error { u32 line; };",
         "R-DIAG-TYPE-001 [R-AGG-0011]"},
        /* L21.3: a value of the type of an error with descendants holds any member. */
        {"base-reads-descendant-fields",
         R_L21_FAMILY "i32 f(io_error e) { return e.code; } i32 g() { return f(tls_error {.code = "
                      "1, .port = 2u16, .session = 3}); }",
         NULL},
        {"base-borrow-reads-fields",
         R_L21_FAMILY "i32 f(const io_error* e) { return e->code; }",
         NULL},
        {"widen-descendant-family",
         R_L21_FAMILY "i32 g(io_error e) { return e.code; } i32 f(net_error n) { return g(move n); "
                      "}",
         NULL},
        {"narrowing-rejected",
         R_L21_FAMILY "i32 g(net_error n) { return n.code; } i32 f(io_error e) { return g(move e); "
                      "}",
         "R-DIAG-TYPE-001"},
        {"unrelated-member-rejected",
         R_L21_FAMILY "i32 g(net_error n) { return n.code; } i32 f() { return g(disk_error {.code "
                      "= 1, .block = 2}); }",
         "R-DIAG-TYPE-001"},
        {"base-field-not-assigned",
         R_L21_FAMILY "void f(io_error e) { e.code = 2; }",
         "R-DIAG-BORROW-001"},
        /* L21.2, R-TYPE-0012, R-ERR-0002, R-ERR-0003. */
        {"throws-base-covers-descendant",
         R_L21_FAMILY "void f() throws io_error { throw tls_error {.code = 1, .port = 2u16, "
                      ".session = 3}; }",
         NULL},
        {"throws-ancestor-and-descendant",
         R_L21_FAMILY "void f() throws io_error, net_error { throw io_error {.code = 1}; }",
         "R-DIAG-EFFECT-001 [R-TYPE-0012]"},
        {"nearest-ancestor-any-order",
         R_L21_FAMILY "i32 f() { try { throw tls_error {.code = 1, .port = 2u16, .session = 3}; } "
                      "catch (io_error e) { return 1; } catch (net_error e) { return 2; } }",
         NULL},
        {"catch-repeats-family",
         R_L21_FAMILY "i32 f() throws io_error { try { throw io_error {.code = 1}; } catch "
                      "(io_error a) { return 1; } catch (io_error b) { return 2; } }",
         "R-DIAG-EFFECT-003 [R-ERR-0003]"},
        {"sibling-not-caught",
         R_L21_FAMILY "i32 f() { try { throw net_error {.code = 1, .port = 2u16}; } catch "
                      "(disk_error d) { return 1; } return 0; }",
         "R-DIAG-EFFECT-001"},
        {"rethrow-keeps-family",
         R_L21_FAMILY "i32 f(io_error failure) throws io_error { try { throw move failure; } catch "
                      "(tls_error t) { return t.session; } catch (net_error n) { throw; } }",
         NULL},
        {"family-throw-undeclared",
         R_L21_FAMILY "void f(io_error failure) { throw move failure; }",
         "R-DIAG-EFFECT-001 [R-ERR-0001]"},
        {"family-throw-in-finally",
         R_L21_FAMILY "void f(io_error failure) throws io_error { try { } finally { throw move "
                      "failure; } }",
         "R-DIAG-EFFECT-001 [R-ERR-0003]"},
        {"unheaded-throw-root",
         R_L21_FAMILY "void f() throws io_error { throw {.code = 1}; }",
         NULL},
        {"generic-recover-family",
         R_L21_FAMILY
         "i32 dial(i32 x) throws net_error { throw (x < 0) net_error {.code = x, .port "
         "= 1u16}; return x; } @generic<E: errors & unborrowed, F: fn(i32) -> i32 "
         "throws(E)> i32 recover(F f, i32 x) throws E { try { return f(x); } catch "
         "(io_error failure) { return failure.code; } } i32 g() { try { return "
         "recover(dial, -3); } catch (net_error e) { return 0; } }",
         NULL},
        {"generic-error-parameter-exact",
         R_L21_FAMILY "@generic<T: error> void raise(T failure) throws T { throw move failure; } "
                      "void f() throws io_error { raise(net_error {.code = 1, .port = 2u16}); }",
         NULL},
        {"generic-error-parameter-family",
         R_L21_FAMILY "@generic<T: error> void raise(T failure) throws T { throw move failure; } "
                      "void f(io_error e) throws io_error { raise(move e); }",
         "R-DIAG-TYPE-001 [R-TYPE-0001]"},
        /* L21.5, Library R-SLIB-ERR-0004: the root of the standard errors. */
        {"standard-fault-throws",
         "i32 f(str text) throws std.error::fault { return std.convert::parse_i32(text, 10u32); }",
         NULL},
        {"standard-fault-catch",
         "i32 f(str text) { try { return std.convert::parse_i32(text, 10u32); } catch "
         "(std.convert::parse_error p) { return -1; } catch (std.error::fault other) { "
         "std.error::error portable = std.error::from_fault(other); return portable.code as i32; } "
         "}",
         NULL},
        {"standard-fault-and-member",
         "void f() throws std.error::fault, std.alloc::alloc_error { }",
         "R-DIAG-EFFECT-001 [R-TYPE-0012]"},
        {"standard-fault-no-fields",
         "i32 f(std.error::fault failure) { return failure.code as i32; }",
         "R-DIAG-TYPE-001 [R-AGG-0011]"},
        {"standard-fault-not-a-parent",
         "error app_error : std.error::fault { i32 code; };",
         "R-DIAG-TYPE-001 [R-AGG-0011]"},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char source[2048];
        const int length =
            snprintf(source, sizeof(source), "module test.l21_regressions; %s", cases[i].source);
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
            (void)fprintf(stderr, "l21 regression %s status %d\n", cases[i].name, (int)status);
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

#undef R_L21_FAMILY

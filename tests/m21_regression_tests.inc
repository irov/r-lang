/* M21 (docs/language-completeness-roadmap.ru.md): M21-1, the public-field records of the C
   modules head braced constructors and std.c::target_info takes a typed initializer (Core
   R-AGG-0007, R-INIT-0004); M21-2, values of a natively implemented fieldless enum compare where
   no variant of the enum is named (R-EXPR-0009). Each case is accepted, or is rejected with the
   diagnostic code, the rule it names and a part of its message. */
static void r_semantic_test_m21_regressions(void) {
    static const struct {
        const char *name;
        const char *source;
        const char *diagnostic;
        const char *message;
    } cases[] = {
        {"system-time-constructor",
         "i64 f() { std.time::system_time t = std.time::system_time {.unix_seconds = 5i64, "
         ".nanoseconds = 0u32}; return t.unix_seconds; }",
         NULL,
         NULL},
        {"utc-constructor-argument",
         "i64 f() throws std.time::time_error { std.time::system_time t = std.time::from_utc("
         "std.time::utc_datetime {.year = 1970, .month = 1u8, .day = 2u8, .hour = 0u8, "
         ".minute = 0u8, .second = 0u8, .nanosecond = 0u32}); return t.unix_seconds; }",
         NULL,
         NULL},
        {"process-record-constructors",
         "i32 f() { std.process::stdio s = std.process::stdio {.input = "
         "std.process::pipe_mode::inherit, .output = std.process::pipe_mode::piped, .error = "
         "std.process::pipe_mode::inherit}; std.process::exit_status e = std.process::exit_status "
         "{.kind = std.process::termination_kind::exited, .code = 1, .success = false}; s as void; "
         "return e.code; }",
         NULL,
         NULL},
        {"metadata-constructor",
         "u64 f() { std.fs::metadata m = std.fs::metadata {.kind = std.fs::file_kind::regular, "
         ".size = 2u64, .created = o::none, .modified = o::none, .accessed = o::none}; "
         "return m.size; }",
         NULL,
         NULL},
        {"target-info-typed-and-constructor",
         "u32 f() { std.c::target_info a = {.pointer_bits = 64u32, .c_wint_available = true, "
         ".c_long_double_available = true, .hosted_native_async = true}; std.c::target_info b = "
         "std.c::target_info {.pointer_bits = 32u32, .c_wint_available = false, "
         ".c_long_double_available = false, .hosted_native_async = false}; "
         "return a.pointer_bits + b.pointer_bits; }",
         NULL,
         NULL},
        {"opaque-constructor",
         "void f() { std.time::duration d = std.time::duration {.seconds = 1i64}; d as void; }",
         "R-DIAG-TYPE-001 [R-INIT-0004]",
         "supported struct destination"},
        {"unknown-record-field",
         "void f() { std.time::system_time t = std.time::system_time {.unix_seconds = 1i64, "
         ".extra = 0u32}; t as void; }",
         "R-DIAG-INIT-002 [R-INIT-0004]",
         "unknown field"},
        {"generic-standard-constructor",
         "void f() { auto m = std.async::mutex {.value = 1}; m as void; }",
         "R-DIAG-SYN-001 [R-GRAM-0005]",
         "unknown closed standard-library qualified name"},
        {"enum-parameter-value-compare",
         "bool f(std.time::error_code a, std.time::error_code b) { return a == b; }",
         NULL,
         NULL},
        {"enum-parameter-condition-compare",
         "i32 f(std.math::error_code a, std.math::error_code b) { if (a != b) { return 1; } "
         "return 0; }",
         NULL,
         NULL},
        {"enum-caught-field-compare",
         "i32 f(std.time::error_code expected) { try { std.time::to_utc(std.time::system_time "
         "{.unix_seconds = 0i64, .nanoseconds = 1000000000u32}) as void; return 1; } catch "
         "(std.time::time_error failure) { if (failure.code != expected) { return 2; } return 0; } }",
         NULL,
         NULL},
        {"enum-different-types",
         "bool f(std.time::error_code a, std.math::error_code b) { return a == b; }",
         "R-DIAG-TYPE-001 [R-EXPR-0009]",
         "incompatible"},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char source[8192];
        const int length =
            snprintf(source, sizeof(source), "module test.m21_regressions; %s", cases[i].source);
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
            (void)fprintf(stderr, "m21 regression %s status %d\n", cases[i].name, (int)status);
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

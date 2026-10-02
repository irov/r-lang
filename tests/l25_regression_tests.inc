/* L25 (docs/language-completeness-roadmap.ru.md): variadic parameters of shared views
   (R-FUNC-0018), task group capacities over constant generic parameters (R-STMT-0017) and the
   vacancy waits of a task group (R-STMT-0018). Each case is accepted, or is rejected with the
   diagnostic code and, where given, the rule it names. */
static void r_semantic_test_l25_regressions(void) {
    static const struct {
        const char *name;
        const char *source;
        const char *diagnostic;
    } cases[] = {
        {"variadic-str",
         "str first(str... words) { return words[0]; } usize f(const str[] arguments) { "
         "str word = first(...arguments[1..]); return len(word); }",
         NULL},
        {"variadic-shared-borrow",
         "const i32* pick(const i32*... values) { return values[0]; } i32 f() { i32 x = 1; "
         "const i32* p = pick(&x); return *p; }",
         NULL},
        {"variadic-shared-slice",
         "usize total(const u8[]... parts) { return len(parts); } usize f() { u8[2] a = {1u8, "
         "2u8}; return total(a[..], a[1..]); }",
         NULL},
        {"variadic-view-outlives-source",
         "str first(str... words) { return words[0]; } usize f() throws std.alloc::alloc_error { "
         "std.string::string owned = "
         "std.string::from_str(\"a\"); str word = first(owned.as_str()); drop owned; "
         "return len(word); }",
         "R-DIAG-BORROW-001"},
        {"variadic-result-borrows-pack",
         "const str* at(str... words) { return &words[0]; } usize f() { const str* p = at(\"a\"); "
         "return len(*p); }",
         "R-DIAG-BORROW-002 [R-FUNC-0018]"},
        {"variadic-exclusive-borrow",
         "void bump(i32*... targets) { usize n = len(targets); n as void; }",
         "R-DIAG-TYPE-001 [R-FUNC-0018]"},
        {"variadic-exclusive-slice",
         "void fill(u8[]... parts) { usize n = len(parts); n as void; }",
         "R-DIAG-TYPE-001 [R-FUNC-0018]"},
        {"vacancy-statement",
         "async void f() throws std.async::start_error { task_scope(2) group { "
         "await group.vacancy(); } }",
         NULL},
        {"vacancy-until-value",
         "async i32 f(std.time::instant deadline) throws std.async::start_error { i32 r = 0; "
         "task_scope(2) group { bool free = await group.vacancy_until(deadline); "
         "if (free == true) { r += 1; } } return r; }",
         NULL},
        {"vacancy-until-statement",
         "async void f(std.time::instant deadline) throws std.async::start_error { "
         "task_scope(2) group { await group.vacancy_until(deadline); } }",
         "R-DIAG-USE-001 [R-STMT-0018]"},
        {"vacancy-arguments",
         "async void f(std.time::instant deadline) throws std.async::start_error { "
         "task_scope(2) group { await group.vacancy(deadline); } }",
         "R-DIAG-ASYNC-001 [R-STMT-0018]"},
        {"vacancy-until-without-deadline",
         "async bool f() throws std.async::start_error { bool free = false; task_scope(2) group "
         "{ bool now = await group.vacancy_until(); free = now; } return free; }",
         "R-DIAG-ASYNC-001 [R-STMT-0018]"},
        {"capacity-generic",
         "@generic<const usize N> async void f() throws std.async::start_error { task_scope(N) "
         "group { await group.all(); } } async void g() throws std.async::start_error { await "
         "f::<4usize>(); }",
         NULL},
        {"capacity-generic-formula",
         "@generic<const usize N> async void f() throws std.async::start_error { const usize "
         "size = N * 2usize; task_scope(size) group { await group.all(); } } async void g() "
         "throws std.async::start_error { await f::<3usize>(); }",
         NULL},
        {"capacity-generic-zero",
         "@generic<const usize N> async void f() throws std.async::start_error { task_scope(N) "
         "group { await group.all(); } } async void g() throws std.async::start_error { await "
         "f::<0usize>(); }",
         "R-DIAG-ASYNC-001 [R-STMT-0017]"},
        {"capacity-generic-too-large",
         "@generic<const usize N> async void f() throws std.async::start_error { task_scope(N + "
         "1usize) group { await group.all(); } } async void g() throws std.async::start_error { "
         "await f::<65536usize>(); }",
         "R-DIAG-ASYNC-001 [R-STMT-0017]"},
        {"capacity-runtime-value",
         "async void f(usize n) throws std.async::start_error { task_scope(n) group { await "
         "group.all(); } }",
         "R-DIAG-ASYNC-001 [R-STMT-0017]"},
        {"vacancy-until-deadline-type",
         "async bool f() throws std.async::start_error { bool free = false; task_scope(2) group "
         "{ bool now = await group.vacancy_until(5u32); free = now; } return free; }",
         "R-DIAG-TYPE-001"},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char source[4096];
        const int length =
            snprintf(source, sizeof(source), "module test.l25_regressions; %s", cases[i].source);
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
            (void)fprintf(stderr, "l25 regression %s status %d\n", cases[i].name, (int)status);
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

/* L25.4: the errors of one call that leave through the same cleanup to the same destination share
   one relay throw, and an exactly caught error or a lone member keeps its own path. */
static void r_semantic_test_l25_relays(void) {
    static const struct {
        const char *name;
        const char *source;
        uint32_t relays;
        uint32_t member_payloads;
    } cases[] = {
        {"forward-group",
         "error a { i32 c; }; error b { i32 c; }; void f(i32 k) throws a, b { throw (k == 1) a "
         "{.c = 1}; throw (k == 2) b {.c = 2}; } void g(i32 k) throws b, a { f(k); }",
         1U,
         0U},
        {"exact-catch-own-path",
         "error a { i32 c; }; error b { i32 c; }; void f(i32 k) throws a, b { throw (k == 1) a "
         "{.c = 1}; throw (k == 2) b {.c = 2}; } i32 g(i32 k) throws b { try { f(k); } catch (a "
         "e) { return e.c; } return 0; }",
         0U,
         2U},
        {"family-catch-group",
         "error base { i32 c; }; error x : base { i32 d; }; error y : base { i32 e; }; void f(i32 "
         "k) throws base { throw (k == 1) x {.c = 1, .d = 2}; throw (k == 2) y {.c = 3, .e = 4}; "
         "throw (k == 3) base {.c = 5}; } i32 g(i32 k) { try { f(k); } catch (base e) { return "
         "1; } return 0; }",
         1U,
         0U},
        {"catch-and-forward-groups",
         "error base { i32 c; }; error x : base { i32 d; }; error p { i32 c; }; error q { i32 c; "
         "}; void f(i32 k) throws base, p, q { throw (k == 1) x {.c = 1, .d = 2}; throw (k == 2) "
         "base {.c = 3}; throw (k == 3) p {.c = 4}; throw (k == 4) q {.c = 5}; } i32 g(i32 k) "
         "throws p, q { try { f(k); } catch (base e) { return 1; } return 0; }",
         2U,
         0U},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char source[4096];
        const int length =
            snprintf(source, sizeof(source), "module test.l25_relays; %s", cases[i].source);
        RFrontendContext *context = r_frontend_create(NULL);
        uint32_t relays = 0U;
        uint32_t member_payloads = 0U;
        R_SEMANTIC_CHECK(context != NULL && length > 0 && (size_t)length < sizeof(source));
        if (context == NULL)
            return;
        (void)r_semantic_add_bytes(context, cases[i].name, (const uint8_t *)source, (size_t)length);
        RFrontendStatus status = r_frontend_analyze(context);
        if (status == R_FRONTEND_OK) {
            status = r_frontend_lower_mir(context);
        }
        for (size_t index = 0U;
             (status == R_FRONTEND_OK) && (index < context->mir_instruction_count);
             ++index) {
            const RMirInstruction *instruction = &context->mir_instructions[index];
            if ((instruction->kind == R_MIR_INSTRUCTION_THROW) &&
                (instruction->runtime_type != R_TYPE_ID_INVALID)) {
                relays += 1U;
            }
            if ((instruction->kind == R_MIR_INSTRUCTION_EFFECT_PAYLOAD) &&
                (instruction->operand0 != R_MIR_VALUE_ID_INVALID) &&
                (instruction->integer_value != UINT64_C(0))) {
                member_payloads += 1U;
            }
        }
        const bool valid = (status == R_FRONTEND_OK) && (relays == cases[i].relays) &&
                           (member_payloads == cases[i].member_payloads);
        if (!valid) {
            (void)fprintf(stderr,
                          "l25 relay %s status %d relays %u payloads %u\n",
                          cases[i].name,
                          (int)status,
                          relays,
                          member_payloads);
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

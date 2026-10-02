/* M22 (docs/language-completeness-roadmap.ru.md): M22-1, the receiver form of an operation of a
   natively implemented enum named through a qualified constant (std.signal::kind::user1.raise());
   the typing of std.signal (R-SLIB-SIGNAL-0001..0003), of the socket options
   (R-SLIB-NET-0012..0013) and of the Unix-domain sockets (R-SLIB-NET-0014..0017). Each case is
   accepted, or is rejected with the diagnostic code, the rule it names and a part of its
   message. */
static void r_semantic_test_m22_regressions(void) {
    static const struct {
        const char *name;
        const char *source;
        const char *diagnostic;
        const char *message;
    } cases[] = {
        {"signal-qualified-constant-receiver",
         "void f() throws std.process::process_error { std.signal::kind::user1.raise(); "
         "std.signal::kind::terminate.listen() as void; }",
         NULL,
         NULL},
        {"signal-listen-needs-kind",
         "void f() throws std.process::process_error { std.signal::listen(15) as void; }",
         "R-DIAG-TYPE-001",
         NULL},
        {"signal-next-count",
         "async u64 f(std.signal::listener l) throws std.process::process_error, "
         "std.async::start_error { return await l.next(); }",
         NULL,
         NULL},
        {"tcp-options-fields",
         "u32 f(const std.net::tcp_stream* s) throws std.net::net_error { std.net::tcp_options o2 "
         "= s->get_options(); o2.keepalive = o::none; s->set_options(o2); return o2.hop_limit; }",
         NULL,
         NULL},
        {"tcp-options-wrong-record",
         "void f(const std.net::tcp_stream* s, std.net::udp_options o2) throws std.net::net_error "
         "{ s->set_options(o2); }",
         "R-DIAG-TYPE-001",
         NULL},
        {"multicast-interface-u32",
         "void f(const std.net::udp_socket* s, std.net::ip_address g, i64 index) throws "
         "std.net::net_error { s->join_multicast(g, index); }",
         "R-DIAG-TYPE-001",
         NULL},
        {"unix-stream-read-method",
         "struct N { usize v; }; @scoped async usize f(const std.net::unix_stream* s, u8[] b) "
         "throws std.net::net_error, std.async::start_error { N n = {.v = 0usize}; "
         "task_scope(1) io { n.v = await s->read_into(b); } return n.v; }",
         NULL,
         NULL},
        {"unix-stream-close-method",
         "async void f(std.net::unix_stream s, std.net::unix_listener l) throws "
         "std.net::net_error, std.async::start_error { await (move s).close(); "
         "await (move l).close(); }",
         NULL,
         NULL},
        {"unix-listen-backlog-u32",
         "async void f() throws std.net::net_error, std.async::start_error { "
         "std.net::unix_listener l = await std.net::unix_listen(\"p\", 1i64, true); drop l; }",
         "R-DIAG-TYPE-001 [R-SLIB-NET-0014]",
         "std.net argument type does not match its contract"},
        {"unix-accept-is-stream",
         "async void f(std.net::unix_listener l) throws std.net::net_error, "
         "std.async::start_error { std.net::tcp_stream s = await std.net::unix_accept(&l); "
         "drop s; drop l; }",
         "R-DIAG-TYPE-001",
         NULL},
        {"unix-read-into-needs-scope",
         "@scoped async usize f(const std.net::unix_stream* s, u8[] b) throws "
         "std.net::net_error, std.async::start_error { return await std.net::unix_read_into(s, "
         "b); }",
         "R-DIAG-ASYNC-001 [R-SLIB-ASYNC-0012]",
         "requires an enclosing task_scope"},
        {"unix-peer-credentials",
         "i32 f(const std.net::unix_stream* s) throws std.net::net_error { "
         "std.net::peer_credentials c = s->peer_credentials(); u32 id = c.user_id + c.group_id; "
         "id as void; return c.process_id; }",
         NULL,
         NULL},
        {"unix-listener-has-no-credentials",
         "void f(const std.net::unix_listener* l) throws std.net::net_error { "
         "l->peer_credentials() as void; }",
         "R-DIAG-NAME-001",
         "no method"},
        {"unix-message-fields",
         "usize f(std.net::unix_message m) { if (m.truncated == true) { return 0usize; } "
         "return m.count; }",
         NULL,
         NULL},
        {"unix-message-has-no-sender",
         "void f(std.net::unix_message m) { m.sender as void; }",
         "R-DIAG-NAME-001",
         NULL},
        {"unix-close-consumes",
         "async void f(std.net::unix_datagram d) throws std.net::net_error, "
         "std.async::start_error { await (move d).close(); await std.net::unix_datagram_close("
         "move d); }",
         "R-DIAG-MOVE-002 [R-OWN-0004]",
         NULL},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char source[8192];
        const int length =
            snprintf(source, sizeof(source), "module test.m22_regressions; %s", cases[i].source);
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
            (void)fprintf(stderr, "m22 regression %s status %d\n", cases[i].name, (int)status);
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

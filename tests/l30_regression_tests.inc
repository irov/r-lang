/* L30 (docs/language-completeness-roadmap.ru.md): asynchronous synchronization (R-SLIB-ASYNC-0013
   to R-SLIB-ASYNC-0016, R-LIB-0016). std.async guards and permits cross await, broadcast results
   match by their std.async variant paths, a std.sync guard across await names the std.async lock
   that replaces it (L30.4, R-FUNC-0011), and the operations reject malformed operands. Defects:
   L30-1, the expected type of a whole postfix expression reached its head; L30-2, the address of
   a call result held across await in an async function was not lowered. Each case is accepted,
   or is rejected with the diagnostic code, the rule it names and a part of its message. */
static void r_semantic_test_l30_regressions(void) {
    static const char prelude[] =
        "struct Tally { u64 total; }; struct Cell { i32 v; }; Tally* pick(Tally* t) { return t; "
        "} async void pause() {}";
    static const struct {
        const char *name;
        const char *source;
        const char *diagnostic;
        const char *message;
    } cases[] = {
        {"head-context",
         "i32 f() { std.sync::mutex<Tally> m = std.sync::mutex_new(Tally {.total = 3u64}); "
         "std.sync::lock_result<Tally> r = m.lock(); switch (move r) { case variant "
         "std.sync::lock_result::locked(move guard): u64 seen = (guard.get())->total; return "
         "seen as i32; case variant std.sync::lock_result::poisoned(move guard): return 1; case "
         "variant std.sync::lock_result::would_deadlock: return 2; } return 3; }",
         NULL,
         NULL},
        {"async-call-arrow",
         "async u64 f() throws std.async::start_error { Tally t = {.total = 5u64}; "
         "(pick(&t))->total += 2u64; await pause(); u64 seen = (pick(&t))->total; return seen; }",
         NULL,
         NULL},
        {"mutex-guard-await",
         "async i32 f() throws std.alloc::alloc_error, std.async::start_error { "
         "std.async::mutex<i32> m = std.async::mutex_new(5); std.async::mutex_guard<i32> g = "
         "await m.lock(); await pause(); *(g.get_mut()) += 1; i32 v = *(g.get()); (move "
         "g).unlock(); return v; }",
         NULL,
         NULL},
        {"rw-guard-await",
         "async i32 f() throws std.alloc::alloc_error, std.async::start_error { "
         "std.async::rw_lock<i32> t = std.async::rwlock_new(5); std.async::rw_write_guard<i32> w "
         "= await t.write(); await pause(); *(w.get_mut()) = 6; (move w).unlock(); "
         "std.async::rw_read_guard<i32> r = await t.read(); await pause(); i32 v = *(r.get()); "
         "(move r).unlock(); return v; }",
         NULL,
         NULL},
        {"permit-await",
         "async usize f() throws std.alloc::alloc_error, std.async::start_error { "
         "std.async::semaphore s = std.async::semaphore_new(1usize); std.async::semaphore_permit "
         "p = await s.acquire(); await pause(); usize left = s.available_permits(); (move "
         "p).release(); return left; }",
         NULL,
         NULL},
        {"broadcast-paths",
         "async i64 f() throws std.alloc::alloc_error, std.async::start_error { "
         "std.async::broadcast<i64> hub = std.async::broadcast(1usize); "
         "std.async::broadcast_receiver<i64> r = hub.subscribe(); usize n = hub.send(4i64); "
         "std.async::broadcast_result<i64> got = await r.receive(); i64 v = n as i64; switch "
         "(move got) { case variant std.async::broadcast_result::received(move x): v += x; case "
         "variant std.async::broadcast_result::lagged(move c): v = c as i64; case variant "
         "std.async::broadcast_result::closed: v = 0i64; } return v; }",
         NULL,
         NULL},
        {"reserve-permit",
         "async i32 f() throws std.alloc::alloc_error, std.async::start_error { "
         "std.sync::sync_channel<i32> c = std.sync::sync_channel::<i32>(1usize); "
         "std.sync::sync_sender<i32> tx = std.sync::sync_sender(&c); std.sync::receiver<i32> rx "
         "= std.sync::sync_receiver(move c); std.sync::reserve_result<i32> slot = await "
         "tx.reserve(); switch (move slot) { case variant "
         "std.sync::reserve_result::reserved(move p): await pause(); (move p).send(1); case "
         "variant std.sync::reserve_result::disconnected: break; } (move rx) as void; return 0; }",
         NULL,
         NULL},
        {"sync-guard-await",
         "async i32 f() throws std.async::start_error { std.sync::mutex<i32> m = "
         "std.sync::mutex_new(5); std.sync::lock_result<i32> r = m.lock(); switch (move r) { "
         "case variant std.sync::lock_result::locked(move g): await pause(); return *(g.get()); "
         "case variant std.sync::lock_result::poisoned(move g): return 1; case variant "
         "std.sync::lock_result::would_deadlock: return 2; } return 3; }",
         "R-DIAG-ASYNC-001 [R-FUNC-0011]",
         "std.async::mutex"},
        {"sync-lock-result-await",
         "async i32 f() throws std.async::start_error { std.sync::mutex<i32> m = "
         "std.sync::mutex_new(5); std.sync::lock_result<i32> r = m.lock(); await pause(); switch "
         "(move r) { case variant std.sync::lock_result::locked(move g): return *(g.get()); case "
         "variant std.sync::lock_result::poisoned(move g): return 1; case variant "
         "std.sync::lock_result::would_deadlock: return 2; } return 3; }",
         "R-DIAG-ASYNC-001 [R-FUNC-0011]",
         "std.async::mutex"},
        {"sync-rw-guard-await",
         "async i32 f() throws std.async::start_error { std.sync::rw_lock<i32> t = "
         "std.sync::rwlock_new(5); std.sync::write_lock_result<i32> r = t.write(); await "
         "pause(); switch (move r) { case variant std.sync::write_lock_result::locked(move g): "
         "return *(g.get()); case variant std.sync::write_lock_result::poisoned(move g): return "
         "1; case variant std.sync::write_lock_result::would_deadlock: return 2; } return 3; }",
         "R-DIAG-ASYNC-001 [R-FUNC-0011]",
         "std.async::rw_lock"},
        {"guard-get-mut-shared",
         "async void f() throws std.alloc::alloc_error, std.async::start_error { "
         "std.async::mutex<i32> m = std.async::mutex_new(5); std.async::mutex_guard<i32> g = "
         "await m.lock(); const std.async::mutex_guard<i32>* s = &g; "
         "*(std.async::mutex_guard_mut(s)) = 1; (move g).unlock(); }",
         "R-DIAG-TYPE-001 [R-SLIB-ASYNC-0013]",
         "exclusive borrow of its guard"},
        {"semaphore-count",
         "void f() throws std.alloc::alloc_error { std.async::semaphore s = "
         "std.async::semaphore_new(2i32); s.add_permits(1usize); }",
         "R-DIAG-TYPE-001 [R-SLIB-ASYNC-0014]",
         "usize"},
        {"unlock-without-move",
         "async void f() throws std.alloc::alloc_error, std.async::start_error { "
         "std.async::mutex<i32> m = std.async::mutex_new(5); std.async::mutex_guard<i32> g = "
         "await m.lock(); std.async::unlock(g); }",
         "R-DIAG-MOVE-001",
         NULL},
        {"guard-not-send",
         "async void f() throws std.alloc::alloc_error, std.async::start_error { "
         "std.async::mutex<rc Cell> m = std.async::mutex_new(new rc Cell {.v = 1}); "
         "std.async::mutex_guard<rc Cell> g = await m.lock(); (move g).unlock(); }",
         "R-DIAG-ASYNC-001",
         "Send"},
        {"broadcast-element-type",
         "void f() throws std.alloc::alloc_error { auto hub = std.async::broadcast(1usize); }",
         "R-DIAG-TYPE-001 [R-TYPE-0036]",
         "element type"},
        {"broadcast-not-send",
         "void f() throws std.alloc::alloc_error { std.async::broadcast<rc Cell> hub = "
         "std.async::broadcast(1usize); }",
         "R-DIAG-TYPE-001 [R-SLIB-ASYNC-0016]",
         "cloned structurally"},
        {"broadcast-borrowed",
         "void f() throws std.alloc::alloc_error { std.async::broadcast<str> hub = "
         "std.async::broadcast(1usize); }",
         "R-DIAG-TYPE-001 [R-SLIB-ASYNC-0016]",
         "unborrowed"},
        {"broadcast-capacity",
         "void f() throws std.alloc::alloc_error { std.async::broadcast<i32> hub = "
         "std.async::broadcast::<i32>(1i32); }",
         "R-DIAG-TYPE-001 [R-SLIB-ASYNC-0016]",
         "usize"},
        {"broadcast-send-type",
         "void f() throws std.alloc::alloc_error { std.async::broadcast<i32> hub = "
         "std.async::broadcast(1usize); usize n = hub.send(1i64); n as void; }",
         "R-DIAG-TYPE-001 [R-SLIB-ASYNC-0016]",
         "element type"},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char source[8192];
        const int length = snprintf(
            source, sizeof(source), "module test.l30_regressions; %s %s", prelude, cases[i].source);
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
            (void)fprintf(stderr, "l30 regression %s status %d\n", cases[i].name, (int)status);
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

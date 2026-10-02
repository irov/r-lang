static void r_semantic_test_scoped_tasks(void) {
    static const struct {
        const char *source;
        bool accepted;
    } cases[] = {
        {"struct A { i32 value; }; A create() { return A{.value=42}; } "
         "i32 f() { return create().value; }",
         true},
        {"struct A { const i32* view; }; A create(const i32* p) { return A{.view=p}; } "
         "const i32* f(const i32* p) { return create(p).view; }",
         true},
        {"struct A { i32 value; }; A create() { return A{.value=42}; } "
         "const i32* f() { return &create().value; }",
         false},
        {"struct A { own i32* value; }; A create() { return A{.value=new i32(42)}; } "
         "own i32* f() { return move create().value; }",
         false},

        {"i32 f(core::atomic_compare_exchange_result<i32> value) { return match(value) { "
         "case variant core::atomic_compare_exchange_result::exchanged(x): x; default: 0; }; }",
         false},
        {"struct A { const i32* view; }; const i32* f(bool flag,const i32* p,const i32* q) { "
         "return match(flag) { case true: A{.view=p}; case false: A{.view=q}; }.view; }",
         true},
        {"struct A { i32 value; }; const i32* f(bool flag) { "
         "return &match(flag) { case true: A{.value=1}; case false: A{.value=2}; }.value; }",
         false},
        {"struct A { own i32* value; }; own i32* f() { "
         "return move match(true) { case true: A{.value=new i32(1)}; "
         "case false: A{.value=new i32(2)}; }.value; }",
         false},
        {"struct A { i32 value; }; i32 f(bool flag) { "
         "return match(flag) { case true: A{.value=1}; case false: A{.value=2}; }.value; }",
         true},
        {"@noalloc async void f() { task_scope(1) g {} }", false},
        {"@nonblocking async void f() { task_scope(1) g {} }", false},
        {"async void f() { task_scope(1) g {} }", true},
        {"async i32 child() { return 3; } async void f() throws std.async::start_error { "
         "task_scope(1) g { auto t=child(); own task<i32>* p=new task<i32>(move t); } }",
         false},
        {"async bool child() { return true; } async bool f() throws std.async::start_error { "
         "return await child() && await child(); }",
         true},
        {"void task_scope(i32 n) {} void f() { task_scope(1); }", true},
        {"error E {}; E make() { return E {}; } void f() throws E { throw make(); }", true},
        {"async i32 child() { return 3; } async void f(bool flag) throws std.async::start_error { "
         "task_scope(1) g { auto t=flag==true ? child() : child(); usize n=await g.first(&t); "
         "i32 value=await move t; value as void; } }",
         true},
        {"async i32 child() { return 3; } void pass(task<i32> t) {} async void f(bool flag) throws "
         "std.async::start_error { task_scope(1) g { auto t=flag==true ? child() : child(); "
         "pass(move t); } }",
         false},

        {"void f() { task_scope(1) g {} }", false},
        {"async void f() { task_scope(0) g {} }", false},
        {"async void f() { task_scope(-1) g {} }", false},
        {"async void f() { task_scope(65537) g {} }", false},
        {"async void f(usize n) { task_scope(n) g {} }", false},
        {"@scoped void f() {}", false},
        {"@scoped @scoped async void f() {}", false},
        {"@scoped async i32 read(const i32* p) { return *p; }", true},
        {"@scoped async const i32* read(const i32* p) { return p; }", false},
        {"@scoped async i32 read(const i32* p) { return *p; } "
         "async i32 f() throws std.async::start_error { i32 x=3; return await read(&x); }",
         false},
        {"@scoped async i32 read(const i32* p) { return *p; } "
         "async i32 f() throws std.async::start_error { i32 x=3; task_scope(1) g { "
         "return await read(&x); } }",
         true},
        {"@scoped async i32 read(const i32* p) { return *p; } "
         "async i32 f() throws std.async::start_error { task_scope(1) g { i32 x=3; "
         "return await read(&x); } }",
         false},
        {"@scoped async i32 read(const i32* p) { return *p; } "
         "async void f() throws std.async::start_error { i32 x=3; task_scope(1) g { "
         "auto t=read(&x); x=4; i32 n=await move t; } }",
         false},
        {"@scoped async i32 read(const i32* p) { return *p; } "
         "async void f() throws std.async::start_error { i32 x=3; task_scope(2) g { "
         "auto a=read(&x); auto b=read(&x); i32 y=x; i32 n=await move a; n as void; i32 m=await "
         "move b; m as void; } }",
         true},
        /* A consuming await ends the member's loans; a consuming cancel does not. */
        {"@scoped async void set(i32* p) { *p=4; } "
         "async void f() throws std.async::start_error { i32 x=3; task_scope(1) g { "
         "auto t=set(&x); await move t; x+=1; } }",
         true},
        {"@scoped async usize fill(u8[] t) { t[0]=1u8; return len(t); } "
         "async void f() throws std.async::start_error, std.alloc::alloc_error { "
         "bytes b=std.alloc::bytes(4usize,0u8); task_scope(1) g { usize n=await "
         "fill(b.as_slice_mut()); n as void; "
         "usize m=await fill(b.as_slice_mut()); m as void; const u8[] v=b.as_slice(); } }",
         true},
        {"async void f() throws std.async::start_error, std.alloc::alloc_error, std.io::io_error { "
         "std.io::input input=std.io::stdin(); bytes b=std.alloc::bytes(4usize,0u8); "
         "task_scope(1) g { while (true) { usize n=await input.read_into(b.as_slice_mut(), "
         "o::none); "
         "if (n == 0usize) { break; } } const u8[] v=b.as_slice(); } }",
         true},
        /* Scoped standard operations (R-SLIB-ASYNC-0012): loans of the group, scope required. */
        {"async void f() throws std.async::start_error, std.alloc::alloc_error, std.io::io_error { "
         "std.io::input input=std.io::stdin(); bytes b=std.alloc::bytes(4usize,0u8); "
         "task_scope(1) g { usize n=await std.io::read_into(&input, b.as_slice_mut(), o::none); n "
         "as void; } }",
         true},
        {"async void f() throws std.async::start_error, std.alloc::alloc_error, std.io::io_error { "
         "std.io::input input=std.io::stdin(); bytes b=std.alloc::bytes(4usize,0u8); "
         "usize n=await std.io::read_into(&input, b.as_slice_mut(), o::none); }",
         false},
        {"async void f() throws std.async::start_error, std.alloc::alloc_error, std.io::io_error { "
         "std.io::input input=std.io::stdin(); bytes b=std.alloc::bytes(4usize,0u8); "
         "task_scope(1) g { auto t=input.read_into(b.as_slice_mut(), o::none); "
         "const u8[] v=b.as_slice(); usize n=await move t; } }",
         false},
        {"async void f() throws std.async::start_error, std.alloc::alloc_error, std.io::io_error { "
         "std.io::input input=std.io::stdin(); bytes b=std.alloc::bytes(4usize,0u8); "
         "task_scope(1) g { auto t=input.read_into(b.as_slice_mut(), o::none); "
         "await g.all(); const u8[] v=b.as_slice(); usize n=await move t; n as void; } }",
         true},
        {"async void f() throws std.async::start_error, std.alloc::alloc_error, std.io::io_error { "
         "std.io::output out=std.io::stdout(); bytes b=std.alloc::bytes(4usize,0u8); "
         "task_scope(2) g { auto t=out.write_from(b.as_slice(), o::none); "
         "auto u=std.io::write_all_from(&out, b.as_slice(), o::none); const u8[] v=b.as_slice(); "
         "usize n=await move t; n as void; await move u; } }",
         true},
        {"async void f() throws std.async::start_error, std.alloc::alloc_error, std.io::io_error { "
         "std.io::output out=std.io::stdout(); bytes b=std.alloc::bytes(4usize,0u8); "
         "task_scope(1) g { usize n=await out.write_from(b.as_slice_mut(), o::none); } }",
         false},
        {"async void f() throws std.async::start_error, std.alloc::alloc_error, std.io::io_error { "
         "std.io::input input=std.io::stdin(); task_scope(1) g { bytes "
         "b=std.alloc::bytes(4usize,0u8); "
         "usize n=await std.io::read_into(&input, b.as_slice_mut(), o::none); } }",
         false},
        /* Slice loans: an exclusive u8[] loan excludes parent reads until the barrier. */
        {"@scoped async usize fill(u8[] t) { t[0]=1u8; return len(t); } "
         "async void f() throws std.async::start_error, std.alloc::alloc_error { "
         "bytes b=std.alloc::bytes(4usize,0u8); task_scope(1) g { auto t=fill(b.as_slice_mut()); "
         "usize n=await move t; n as void; } }",
         true},
        {"@scoped async usize fill(u8[] t) { t[0]=1u8; return len(t); } "
         "async void f() throws std.async::start_error, std.alloc::alloc_error { "
         "bytes b=std.alloc::bytes(4usize,0u8); task_scope(1) g { auto t=fill(b.as_slice_mut()); "
         "const u8[] v=b.as_slice(); usize n=await move t; } }",
         false},
        {"@scoped async usize fill(u8[] t) { t[0]=1u8; return len(t); } "
         "async void f() throws std.async::start_error, std.alloc::alloc_error { "
         "bytes b=std.alloc::bytes(4usize,0u8); task_scope(1) g { auto t=fill(b.as_slice_mut()); "
         "await g.all(); const u8[] v=b.as_slice(); usize n=await move t; n as void; } }",
         true},
        {"@scoped async usize count(const u8[] t) { return len(t); } "
         "async void f() throws std.async::start_error, std.alloc::alloc_error { "
         "bytes b=std.alloc::bytes(4usize,0u8); task_scope(2) g { auto t=count(b.as_slice()); "
         "auto u=count(b.as_slice()); const u8[] v=b.as_slice(); usize n=await move t; n as void; "
         "usize m=await move u; m as void; } }",
         true},
        {"@scoped async usize fill(u8[] t) { t[0]=1u8; return len(t); } "
         "async void f() throws std.async::start_error, std.alloc::alloc_error { "
         "task_scope(1) g { bytes b=std.alloc::bytes(4usize,0u8); auto t=fill(b.as_slice_mut()); "
         "usize n=await move t; } }",
         false},
        {"@scoped async void set(i32* p) { *p=4; } "
         "async void f() throws std.async::start_error { i32 x=3; task_scope(1) g { "
         "auto t=set(&x); i32 n=x; await move t; } }",
         false},
        {"@scoped async void set(i32* p) { *p=4; } "
         "async void f() throws std.async::start_error { i32 x=3; task_scope(1) g { "
         "auto t=set(&x); std.async::cancel(move t); x=8; } }",
         false},
        {"@scoped async void set(i32* p) { *p=4; } "
         "async i32 f() throws std.async::start_error { i32 x=3; task_scope(1) g { "
         "auto t=set(&x); await g.all(); x+=1; await move t; } return x; }",
         true},
        {"@scoped async i32 read(const i32* p) { return *p; } "
         "async i32 f() { i32 x=3; task_scope(1) g { try { auto t=read(&x); i32 y=await move t; y "
         "as void; } "
         "catch(std.async::start_error failure) { x=4; } } return x; }",
         true},
        {"@scoped async void set(i32* p) { *p=4; } "
         "async void f(bool flag) throws std.async::start_error { i32 x=3; task_scope(1) g { "
         "if(flag==true) { auto t=set(&x); std.async::cancel(move t); } x=8; } }",
         false},
        {"async i32 child() { return 3; } async task<i32> f() throws std.async::start_error { "
         "task_scope(1) g { return child(); } }",
         false},
        {"async i32 child() { return 3; } struct Box { task<i32> task; }; "
         "async void f() throws std.async::start_error { task_scope(1) g { auto t=child(); "
         "Box box=Box{.task=move t}; } }",
         false},
        {"async i32 child() { return 3; } void pass(task<i32> t) {} "
         "async void f() throws std.async::start_error { task_scope(1) g { auto t=child(); "
         "pass(move t); } }",
         false},
        /* L24: a detached member stays supervised by its group. */
        {"async i32 child() { return 3; } async void f() throws std.async::start_error { "
         "task_scope(1) g { auto t=child(); std.async::detach(move t); } }",
         true},
        {"async i32 child() { return 3; } async void f() throws std.async::start_error { "
         "auto t=child(); task_scope(1) g { usize n=await g.first(&t); } i32 x=await move t; }",
         false},
        {"async void f() { task_scope(1) g { await g.first(); } }", false},
        {"async void f() { task_scope(1) g { await g.all(1); } }", false},
        {"async void f() { task_scope(1) g { await g.bad(); } }", false},
        {"async void f() { try {} finally { task_scope(1) g {} } }", false},
        {"@generic<T: send> @scoped async T pass(T value) { return move value; }", false},
        {"@generic<T: send & unborrowed> @scoped async T read(const T* p) { return move *p; }",
         false},
        {"@generic<T: copy & send & sync> @scoped async void read(const T* p) {}", true},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char source[2048];
        int length = snprintf(source, sizeof(source), "module test.scopes; %s", cases[i].source);
        RFrontendContext *context = r_frontend_create(NULL);
        R_SEMANTIC_CHECK(context != NULL && length > 0 && (size_t)length < sizeof(source));
        if (context == NULL)
            return;
        (void)r_semantic_add_bytes(
            context, "scoped-contract.r", (const uint8_t *)source, (size_t)length);
        RFrontendStatus status = r_frontend_analyze(context);
        bool valid = status == (cases[i].accepted ? R_FRONTEND_OK : R_FRONTEND_INVALID_SOURCE);
        if (!valid) {
            (void)fprintf(stderr,
                          "scoped case %zu status %d expected %d\n",
                          i,
                          (int)status,
                          cases[i].accepted);
            for (size_t d = 0U; d < context->diagnostic_count; ++d)
                (void)fprintf(stderr,
                              "%s: %s\n",
                              context->diagnostics[d].code,
                              context->diagnostics[d].message);
        }
        R_SEMANTIC_CHECK(valid);
        r_frontend_destroy(context);
    }
}

/* R-STMT-0018: bounded group waits, group.cancel_all() and select. */
static void r_semantic_test_scoped_select(void) {
    static const char prelude[] = "@scoped async u32 read(const u32* p) { return *p; } "
                                  "@scoped async void touch(const u32* p) {} ";
    static const struct {
        const char *name;
        const char *source;
        const char *diagnostic;
    } cases[] = {
        {"select",
         "async u32 f() throws std.async::start_error { u32 x=3u32; u32 out=0u32; "
         "task_scope(2) g { auto a=read(&x); auto b=read(&x); select (g) { "
         "case u32 v=await move a: out+=v; break; case u32 w=await move b: out+=w; break; } "
         "g.cancel_all(); await g.all(); } return out; }",
         NULL},
        {"select-deadline",
         "async void f() throws std.async::start_error, std.time::time_error { u32 x=3u32; "
         "std.time::instant d=std.time::monotonic_now(); task_scope(1) g { auto t=touch(&x); "
         "select (g) { case await move t: break; case until (d): break; } } }",
         NULL},
        {"select-partial-cancel",
         "async void f(bool flag) throws std.async::start_error { u32 x=3u32; "
         "task_scope(2) g { auto a=read(&x); auto b=read(&x); select (g) { "
         "case u32 v=await move a: v as void; break; case u32 w=await move b: w as void; "
         "break; } if (flag == true) { g.cancel_all(); } } }",
         NULL},
        {"select-pending-use",
         "async void f() throws std.async::start_error { u32 x=3u32; "
         "task_scope(2) g { auto a=read(&x); auto b=read(&x); select (g) { "
         "case u32 v=await move a: v as void; break; case u32 w=await move b: w as void; "
         "break; } u32 again=await move a; again as void; } }",
         "R-DIAG-MOVE-002"},
        {"cancel-all-consumes",
         "async void f() throws std.async::start_error { u32 x=3u32; task_scope(1) g { "
         "auto a=read(&x); g.cancel_all(); u32 v=await move a; v as void; } }",
         "R-DIAG-MOVE-002"},
        {"cancel-all-keeps-loans",
         "async void f() throws std.async::start_error { u32 x=3u32; task_scope(1) g { "
         "auto a=read(&x); g.cancel_all(); x=4u32; } }",
         "R-DIAG-BORROW-001"},
        {"cancel-all-barrier",
         "async u32 f() throws std.async::start_error { u32 x=3u32; task_scope(1) g { "
         "auto a=read(&x); g.cancel_all(); await g.all(); x+=1u32; } return x; }",
         NULL},
        {"cancel-all-arguments",
         "async void f() { task_scope(1) g { g.cancel_all(1); } }",
         "R-DIAG-ASYNC-001"},
        {"group-method", "async void f() { task_scope(1) g { g.stop(); } }", "R-DIAG-ASYNC-001"},
        {"select-foreign-member",
         "async void f() throws std.async::start_error { u32 x=3u32; task_scope(1) outer { "
         "auto a=read(&x); task_scope(1) inner { select (inner) { case u32 v=await move a: "
         "v as void; break; } } } }",
         "R-DIAG-ASYNC-001"},
        {"select-duplicate",
         "async void f() throws std.async::start_error { u32 x=3u32; task_scope(1) g { "
         "auto a=read(&x); select (g) { case u32 v=await move a: v as void; break; "
         "case u32 w=await move a: w as void; break; } } }",
         "R-DIAG-ASYNC-001"},
        {"select-two-deadlines",
         "async void f() throws std.async::start_error, std.time::time_error { u32 x=3u32; "
         "std.time::instant d=std.time::monotonic_now(); task_scope(1) g { auto a=read(&x); "
         "select (g) { case u32 v=await move a: v as void; break; case until (d): break; "
         "case until (d): break; } } }",
         "R-DIAG-ASYNC-001"},
        {"select-deadline-only",
         "async void f() throws std.time::time_error { std.time::instant "
         "d=std.time::monotonic_now(); task_scope(1) g { select (g) { case until (d): break; } "
         "} }",
         "R-DIAG-ASYNC-001"},
        {"select-deadline-type",
         "async void f() throws std.async::start_error { u32 x=3u32; task_scope(1) g { "
         "auto a=read(&x); select (g) { case u32 v=await move a: v as void; break; "
         "case until (x): break; } } }",
         "R-DIAG-TYPE-001"},
        {"select-fallthrough",
         "async void f() throws std.async::start_error { u32 x=3u32; task_scope(2) g { "
         "auto a=read(&x); auto b=read(&x); select (g) { case u32 v=await move a: v as void; "
         "fallthrough; case u32 w=await move b: w as void; break; } } }",
         "R-DIAG-FLOW-001"},
        {"select-expression-arm",
         "async void f() throws std.async::start_error { u32 x=3u32; task_scope(1) g { "
         "auto a=read(&x); select (g) { case u32 v=await move a + 1u32: v as void; break; } } }",
         "R-DIAG-ASYNC-001"},
        {"select-consumed-member",
         "async void f() throws std.async::start_error { u32 x=3u32; task_scope(1) g { "
         "auto a=read(&x); u32 v=await move a; v as void; select (g) { "
         "case u32 w=await move a: w as void; break; } } }",
         "R-DIAG-MOVE-002"},
        {"select-not-group",
         "async void f() { u32 x=3u32; select (x) { case until (x): break; } }",
         "R-DIAG-ASYNC-001"},
        {"bounded-waits",
         "async void f() throws std.async::start_error, std.time::time_error { u32 x=3u32; "
         "std.time::instant d=std.time::monotonic_now(); task_scope(1) g { auto a=read(&x); "
         "o<usize> r=await g.first_until(d, &a); r as void; bool done=await g.all_until(d); "
         "done as void; u32 v=await move a; v as void; } }",
         NULL},
        {"bounded-statement",
         "async void f() throws std.async::start_error, std.time::time_error { u32 x=3u32; "
         "std.time::instant d=std.time::monotonic_now(); task_scope(1) g { auto a=read(&x); "
         "await g.all_until(d); u32 v=await move a; v as void; } }",
         "R-DIAG-USE-001"},
        {"bounded-deadline-type",
         "async void f() throws std.async::start_error { u32 x=3u32; task_scope(1) g { "
         "auto a=read(&x); bool done=await g.all_until(x); done as void; u32 v=await move a; "
         "v as void; } }",
         "R-DIAG-TYPE-001"},
        {"bounded-members",
         "async void f() throws std.time::time_error { std.time::instant "
         "d=std.time::monotonic_now(); task_scope(1) g { o<usize> r=await g.first_until(d); r as "
         "void; } }",
         "R-DIAG-ASYNC-001"},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char source[2048];
        const int length =
            snprintf(source, sizeof(source), "module test.select; %s%s", prelude, cases[i].source);
        RFrontendContext *context = r_frontend_create(NULL);
        R_SEMANTIC_CHECK(context != NULL && length > 0 && (size_t)length < sizeof(source));
        if (context == NULL)
            return;
        (void)r_semantic_add_bytes(context, cases[i].name, (const uint8_t *)source, (size_t)length);
        const RFrontendStatus status = r_frontend_analyze(context);
        const bool valid =
            status == (cases[i].diagnostic == NULL ? R_FRONTEND_OK : R_FRONTEND_INVALID_SOURCE) &&
            (cases[i].diagnostic == NULL ||
             r_semantic_diagnostic_code_count(context, cases[i].diagnostic) != 0U);
        if (!valid) {
            (void)fprintf(stderr, "select case %s status %d\n", cases[i].name, (int)status);
            for (size_t d = 0U; d < context->diagnostic_count; ++d)
                (void)fprintf(stderr,
                              "%s: %s\n",
                              context->diagnostics[d].code,
                              context->diagnostics[d].message);
        }
        R_SEMANTIC_CHECK(valid);
        r_frontend_destroy(context);
    }
}

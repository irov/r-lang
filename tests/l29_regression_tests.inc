/* L29 (docs/language-completeness-roadmap.ru.md): owners of dyn interfaces (R-TYPE-0055):
   `own dyn(C)*`, `arc dyn(C)`, `rc dyn(C)` and weak forms from owners of members that prove the
   contract, method calls through `->`, containers, options, generic arguments, narrowing and
   Send from the contract. Each case is accepted, or is rejected with the diagnostic code and
   the rule it names. */
static void r_semantic_test_l29_regressions(void) {
    static const char prelude[] =
        "trait Shape { i32 area(const Self* this); void grow(Self* this); }; trait Named { str "
        "name(const Self* this); }; struct Square { i32 side; }; impl Shape for Square { i32 "
        "area(const Square* this) { return this->side; } void grow(Square* this) { this->side += "
        "1; } }; struct Label { str text; }; impl Shape for Label { i32 area(const Label* this) { "
        "return 0; } void grow(Label* this) { } };";
    static const struct {
        const char *name;
        const char *source;
        const char *diagnostic;
    } cases[] = {
        {"p-own-identity",
         "bool f() { own Square* q = new Square {.side = 1}; own dyn(Shape)* s = move q; own "
         "Square* r = new Square {.side = 1}; own dyn(Shape)* t = move r; return s == t; }",
         NULL},
        {"p-exclusive",
         "void f() { own Square* q = new Square {.side = 1}; own dyn(Shape)* s = move q; "
         "s->grow(); }",
         NULL},
        {"p-field",
         "struct Box { own dyn(Shape)* shape; }; i32 f() { own Square* q = new Square {.side = 2};"
         " Box b = {.shape = move q}; b.shape->grow(); return b.shape->area(); }",
         NULL},
        {"p-return",
         "own dyn(Shape)* make(bool square, str t) { if (square == true) { own Square* q = new "
         "Square {.side = 1}; return move q; } own Square* r = new Square {.side = 2}; return move"
         " r; } i32 f() { own dyn(Shape)* s = make(true, \"x\"); return s->area(); }",
         NULL},
        {"p-param",
         "i32 take(own dyn(Shape)* s) { return s->area(); } i32 f() { own Square* q = new Square "
         "{.side = 3}; return take(move q); }",
         NULL},
        {"p-narrow-owner",
         "impl Named for Square { str name(const Square* this) { return \"sq\"; } }; bool f() { "
         "own Square* q = new Square {.side = 1}; own dyn(Shape & Named)* both = move q; own "
         "dyn(Named)* n = move both; return std.bytes::equal(n->name(), \"sq\"); }",
         NULL},
        {"p-narrow-borrow",
         "impl Named for Square { str name(const Square* this) { return \"sq\"; } }; i32 f() { "
         "Square q = {.side = 4}; const dyn(Shape & Named)* both = &q; const dyn(Shape)* s = both;"
         " return s->area(); }",
         NULL},
        {"p-borrow-owner",
         "i32 area(const dyn(Shape)* s) { return s->area(); } i32 f() { own Square* q = new Square"
         " {.side = 5}; own dyn(Shape)* s = move q; return area(&s); }",
         NULL},
        {"p-exclusive-borrow-owner",
         "void grow(dyn(Shape)* s) { s->grow(); } i32 f() { own Square* q = new Square {.side = "
         "5}; own dyn(Shape)* s = move q; grow(&s); return s->area(); }",
         NULL},
        {"p-arc-shared",
         "i32 f() { arc Square q = new arc Square {.side = 6}; arc dyn(Shape) s = move q; arc "
         "dyn(Shape) t = std.arc::clone(&s); if (std.arc::ptr_eq(&s, &t) == false) { return 0; } "
         "return t->area(); }",
         NULL},
        {"p-weak-upgrade",
         "i32 f() { rc Square q = new rc Square {.side = 7}; rc dyn(Shape) s = move q; weak rc "
         "dyn(Shape) w = std.rc::downgrade(&s); switch (std.rc::upgrade(&w)) { case variant "
         "o::some(strong): return (*strong)->area(); case variant o::none: return 0; } return 0; }",
         NULL},
        {"p-option",
         "i32 f() { own Square* q = new Square {.side = 8}; o<own dyn(Shape)*> m = o::some(move "
         "q); switch (move m) { case variant o::some(move s): return s->area(); case variant "
         "o::none: return 0; } return 0; }",
         NULL},
        {"p-array",
         "i32 f() throws std.array::push_error<own dyn(Shape)*> { array<own dyn(Shape)*> items = "
         "std.array::create::<own dyn(Shape)*>(); own Square* q = new Square {.side = 9}; "
         "std.array::push(&items, move q); return items[0]->area(); }",
         NULL},
        {"p-generic",
         "@generic<T> T keep(T v) { return move v; } i32 f() { own Square* q = new Square {.side ="
         " 10}; own dyn(Shape)* s = keep::<own dyn(Shape)*>(move q); return s->area(); }",
         NULL},
        {"p-dict-value",
         "i32 f() throws std.dict::insert_error<i32, arc dyn(Shape)> { dict<i32, arc dyn(Shape)> d"
         " = std.dict::create::<i32, arc dyn(Shape)>(); arc Square q = new arc Square {.side = "
         "11}; std.dict::insert(&d, 1, move q) as void; i32 k = 1; switch (std.dict::get(&d, &k)) "
         "{ case variant o::some(found): return (**found)->area(); case variant o::none: return 0;"
         " } return 0; }",
         NULL},
        {"p-own-send-thread",
         "i32 work(own dyn(Shape & send)* s) { return s->area(); } void f() throws "
         "std.thread::thread_error { own Square* q = new Square {.side = 1}; own dyn(Shape & "
         "send)* s = move q; std.thread::join_handle<i32> h = std.thread::spawn(work, move s); "
         "(move h).join() as void; }",
         NULL},
        {"p-arc-sync-thread",
         "i32 work(arc dyn(Shape & send & sync) s) { return s->area(); } void f() throws "
         "std.thread::thread_error { arc Square q = new arc Square {.side = 1}; arc dyn(Shape & "
         "send & sync) s = move q; std.thread::join_handle<i32> h = std.thread::spawn(work, move "
         "s); (move h).join() as void; }",
         NULL},
        {"p-narrow-supertrait",
         "trait Base { i32 id(const Self* this); }; trait Derived : Base { i32 twice(const Self* "
         "this); }; impl Base for Square { i32 id(const Square* this) { return this->side; } }; "
         "impl Derived for Square { i32 twice(const Square* this) { return this->side * 2; } }; "
         "i32 f() { own Square* q = new Square {.side = 3}; own dyn(Derived)* d = move q; own "
         "dyn(Base)* b = move d; return b->id(); }",
         NULL},
        {"p-narrow-capability",
         "trait Tick { i32 tick(const Self* this); }; impl Tick for Square { i32 tick(const "
         "Square* this) { return 1; } }; i32 f() { arc Square q = new arc Square {.side = 3}; arc "
         "dyn(Tick & send & sync) d = move q; arc dyn(Tick & sync) b = move d; return b->tick(); }",
         NULL},
        {"p-const-owner-shared",
         "i32 area(const (own dyn(Shape)*)* p) { return (*p)->area(); }",
         NULL},
        {"p-weak-convert",
         "i32 f() { arc Square q = new arc Square {.side = 4}; weak arc Square w = "
         "std.arc::downgrade(&q); weak arc dyn(Shape) wd = move w; switch (std.arc::upgrade(&wd)) "
         "{ case variant o::some(strong): return (*strong)->area(); case variant o::none: return "
         "0; } return 0; }",
         NULL},
        {"p-weak-rc-convert",
         "i32 f() { rc Square q = new rc Square {.side = 4}; weak rc Square w = "
         "std.rc::downgrade(&q); weak rc dyn(Shape) wd = move w; i32 n = std.rc::weak_count(&q) as"
         " i32; return n; }",
         NULL},
        {"view-member",
         "void f(str t) { own Label* l = new Label {.text = t}; own dyn(Shape)* s = move l; }",
         "R-DIAG-TYPE-001 [R-TYPE-0055]"},
        {"deref-own",
         "i32 f() { own Square* q = new Square {.side = 1}; own dyn(Shape)* s = move q; return "
         "(*s).area(); }",
         "R-DIAG-TYPE-001 [R-TYPE-0055]"},
        {"shared-exclusive-method",
         "void f() { arc Square q = new arc Square {.side = 1}; arc dyn(Shape) s = move q; "
         "s->grow(); }",
         "R-DIAG-BORROW-001 [R-TYPE-0055]"},
        {"try-unwrap",
         "void f() { arc Square q = new arc Square {.side = 1}; arc dyn(Shape) s = move q; "
         "std.arc::try_unwrap(move s) as void; }",
         "R-DIAG-TYPE-001 [R-TYPE-0055]"},
        {"weak-method",
         "i32 f() { arc Square q = new arc Square {.side = 1}; arc dyn(Shape) s = move q; weak arc"
         " dyn(Shape) w = std.arc::downgrade(&s); return w->area(); }",
         "R-DIAG-TYPE-001 [R-TYPE-0055]"},
        {"unrelated-narrowing",
         "void f() { own Square* q = new Square {.side = 1}; own dyn(Shape)* s = move q; own "
         "dyn(Named)* n = move s; }",
         "R-DIAG-TYPE-001 [R-TYPE-0055]"},
        {"nullable-owner",
         "void f() { own dyn(Shape)*? s = null; }",
         "R-DIAG-TYPE-001 [R-TYPE-0055]"},
        {"contract-not-proved",
         "void f() { own Square* q = new Square {.side = 1}; own dyn(Named)* n = move q; }",
         "R-DIAG-TRAIT-001 [R-TYPE-0055]"},
        {"copy-own",
         "void f() { own Square* q = new Square {.side = 1}; own dyn(Shape)* s = move q; own "
         "dyn(Shape)* t = s; }",
         "R-DIAG-MOVE-001 [R-OWN-0003]"},
        {"get-mut",
         "void f() { arc Square q = new arc Square {.side = 1}; arc dyn(Shape) s = move q; "
         "Square*? m = std.arc::get_mut(&s); }",
         "R-DIAG-TYPE-001 [R-TYPE-0055]"},
        {"from-borrow",
         "void f() { Square q = {.side = 1}; own dyn(Shape)* s = &q; }",
         "R-DIAG-TYPE-001"},
        {"own-to-arc",
         "void f() { own Square* q = new Square {.side = 1}; arc dyn(Shape) s = move q; }",
         "R-DIAG-TYPE-001 [R-INIT-0002]"},
        {"arc-to-rc",
         "void f() { arc Square q = new arc Square {.side = 1}; rc dyn(Shape) s = move q; }",
         "R-DIAG-TYPE-001 [R-INIT-0002]"},
        {"into-raw",
         "void f() { arc Square q = new arc Square {.side = 1}; arc dyn(Shape) s = move q; "
         "std.arc::into_raw(move s) as void; }",
         "R-DIAG-TYPE-001 [R-TYPE-0055]"},
        {"key",
         "void f() { dict<own dyn(Shape)*, i32> d = std.dict::create::<own dyn(Shape)*, i32>(); }",
         "R-DIAG-TYPE-001 [R-LIB-0020]"},
        {"deref-arc",
         "i32 f() { arc Square q = new arc Square {.side = 1}; arc dyn(Shape) s = move q; return "
         "(*s).area(); }",
         "R-DIAG-TYPE-001 [R-TYPE-0055]"},
        {"rc-thread",
         "i32 work(rc dyn(Shape) s) { return s->area(); } void f() throws std.thread::thread_error"
         " { rc Square q = new rc Square {.side = 1}; rc dyn(Shape) s = move q; "
         "std.thread::join_handle<i32> h = std.thread::spawn(work, move s); (move h).join() as "
         "void; }",
         "R-DIAG-MEM-001 [R-MEM-0013]"},
        {"equal-arc",
         "bool f() { arc Square q = new arc Square {.side = 1}; arc dyn(Shape) s = move q; arc "
         "dyn(Shape) t = std.arc::clone(&s); return s == t; }",
         "R-DIAG-MOVE-001 [R-OWN-0003]"},
        {"arc-no-send-thread",
         "i32 work(arc dyn(Shape) s) { return s->area(); } void f() throws "
         "std.thread::thread_error { arc Square q = new arc Square {.side = 1}; arc dyn(Shape) s ="
         " move q; std.thread::join_handle<i32> h = std.thread::spawn(work, move s); (move "
         "h).join() as void; }",
         "R-DIAG-MEM-001 [R-MEM-0013]"},
        {"own-no-send-thread",
         "i32 work(own dyn(Shape)* s) { return s->area(); } void f() throws "
         "std.thread::thread_error { own Square* q = new Square {.side = 1}; own dyn(Shape)* s = "
         "move q; std.thread::join_handle<i32> h = std.thread::spawn(work, move s); (move "
         "h).join() as void; }",
         "R-DIAG-MEM-001 [R-MEM-0013]"},
        {"arc-send-only-thread",
         "i32 work(arc dyn(Shape & send) s) { return s->area(); } void f() throws "
         "std.thread::thread_error { arc Square q = new arc Square {.side = 1}; arc dyn(Shape & "
         "send) s = move q; std.thread::join_handle<i32> h = std.thread::spawn(work, move s); "
         "(move h).join() as void; }",
         "R-DIAG-MEM-001 [R-MEM-0013]"},
        {"arc-not-send",
         "trait Tick { i32 tick(const Self* this); }; struct Cell { i32 v; }; struct Local { rc "
         "Cell n; }; impl Tick for Local { i32 tick(const Local* this) { return 1; } }; void f() {"
         " arc Local l = new arc Local {.n = new rc Cell {.v = 1}}; arc dyn(Tick & send) t = move "
         "l; }",
         "R-DIAG-TRAIT-001 [R-TYPE-0055]"},
        {"own-not-send",
         "trait Tick { i32 tick(const Self* this); }; struct Cell { i32 v; }; struct Local { rc "
         "Cell n; }; impl Tick for Local { i32 tick(const Local* this) { return 1; } }; void f() {"
         " own Local* l = new Local {.n = new rc Cell {.v = 1}}; own dyn(Tick & send)* t = move l;"
         " }",
         "R-DIAG-TRAIT-001 [R-TYPE-0055]"},
        {"widen-capability",
         "trait Tick { i32 tick(const Self* this); }; impl Tick for Square { i32 tick(const "
         "Square* this) { return 1; } }; i32 f() { arc Square q = new arc Square {.side = 3}; arc "
         "dyn(Tick & sync) d = move q; arc dyn(Tick & send & sync) b = move d; return b->tick(); }",
         "R-DIAG-TYPE-001 [R-TYPE-0055]"},
        {"const-owner-exclusive",
         "void grow(const (own dyn(Shape)*)* p) { (*p)->grow(); }",
         "R-DIAG-BORROW-001 [R-BORROW-0002]"},
        {"const-owner-lend",
         "void take(dyn(Shape)* s) { s->grow(); } void grow(const (own dyn(Shape)*)* p) { "
         "take(&*p); }",
         "R-DIAG-BORROW-001 [R-BORROW-0002]"},
        {"weak-borrow",
         "i32 area(const dyn(Shape)* s) { return s->area(); } i32 f() { arc Square q = new arc "
         "Square {.side = 1}; arc dyn(Shape) s = move q; weak arc dyn(Shape) w = "
         "std.arc::downgrade(&s); return area(&w); }",
         "R-DIAG-TYPE-001 [R-TYPE-0055]"},
        {"shared-lend-exclusive",
         "void take(dyn(Shape)* s) { s->grow(); } void f() { arc Square q = new arc Square {.side "
         "= 1}; arc dyn(Shape) s = move q; take(&s); }",
         "R-DIAG-BORROW-001 [R-TYPE-0055]"},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char source[8192];
        const int length = snprintf(source,
                                    sizeof(source),
                                    "module test.l29_regressions; %s %s",
                                    prelude,
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
        const bool valid =
            status == (cases[i].diagnostic == NULL ? R_FRONTEND_OK : R_FRONTEND_INVALID_SOURCE) &&
            (cases[i].diagnostic == NULL ||
             r_semantic_l14_diagnostic_count(context, cases[i].diagnostic) != 0U) &&
            r_semantic_diagnostic_code_count(context, "R-DIAG-SLICE-001") == 0U;
        if (!valid) {
            (void)fprintf(stderr, "l29 regression %s status %d\n", cases[i].name, (int)status);
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

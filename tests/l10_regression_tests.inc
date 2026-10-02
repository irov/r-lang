/* L10 (docs/language-completeness-roadmap.ru.md): borrows in containers and results. Each case
 * is a closed H1/H2 form that is accepted, or a violation of its borrow rules that keeps its
 * normative diagnostic. */
static void r_semantic_test_l10_regressions(void) {
    static const struct {
        const char *name;
        const char *source;
        const char *diagnostic;
    } cases[] = {
        /* L10.1: `const str[]` parameters of ordinary functions, methods and callables. */
        {"string-slice-parameter",
         "usize count(const str[] words) { return len(words); } str first(const str[] words) { "
         "return words[0]; } const str[] rest(const str[] words) { return words[1usize..len("
         "words)]; } usize probe() { str[2] local = {\"a\", \"bc\"}; const str[] view = "
         "local[0usize..2usize]; str head = first(view); return count(rest(view)) + len(head); "
         "}",
         NULL},
        {"string-slice-callable",
         "@generic<F: fn(const str[]) -> usize> usize apply(F f, const str[] w) { return f(w); } "
         "usize measure(const str[] w) { return len(w); } usize probe() { str[1] local = "
         "{\"a\"}; const str[] view = local[0usize..1usize]; return apply(measure, view); }",
         NULL},
        /* The view of a string array keeps the array as its origin. */
        {"string-array-view-write",
         "i32 probe() { str[2] local = {\"a\", \"bc\"}; const str[] view = "
         "local[0usize..2usize]; local[0] = \"zz\"; return len(view[0]) as i32; }",
         "R-DIAG-BORROW-001"},
        {"string-array-view-escape",
         "const str[] escape() { str[2] local = {\"a\", \"b\"}; return local[0usize..2usize]; }",
         "R-DIAG-BORROW-002"},
        {"string-slice-element-origin",
         "str first(const str[] words) { return words[0]; } i32 probe() throws "
         "std.alloc::alloc_error { std.string::string "
         "owned = std.string::from_str(\"hello\"); str[1] holder = {std.string::as_str(&owned)}; "
         "str best = first(holder[0usize..1usize]); std.string::append_str(&owned, \"x\"); return "
         "len(best) as i32; }",
         "R-DIAG-BORROW-001"},
        {"string-slice-async-frame",
         "async usize count(const str[] words) { return len(words); }",
         "R-DIAG-ASYNC-001"},
        /* L10.2: never results; every path of a never function ends without completing. */
        {"never-result",
         "error Bad { i32 code; }; never fail(str m) { panic(m); } never raise(i32 c) throws Bad "
         "{ throw Bad {.code = c}; } i32 check(i32 x) { if (x < 0) { fail(\"n\"); } return x; } "
         "i32 checked(i32 x) throws Bad { if (x > 9) { raise(x); } return x; } i32 pick(i32 x) "
         "{ i32 y = x > 0 ? x : fail(\"z\"); return y; }",
         NULL},
        /* R-EXPR-0015: never converts to the type its context requires. */
        {"never-conversions",
         "never fail(str m) { panic(m); } i32 id(i32 x) { return x; } i32 direct(i32 x) { if (x "
         "> 0) { return x; } return fail(\"d\"); } i32 local(i32 x) { if (x > 0) { return x; } "
         "i32 v = panic(\"l\"); return v; } i32 argument(i32 x) { if (x > 0) { return x; } "
         "return id(fail(\"a\")); } i32 operand(i32 x) { if (x > 0) { return x; } return 1 + "
         "fail(\"o\"); }",
         NULL},
        {"never-values",
         "never fail(str m) { panic(m); } i32 probe(i32 x) { auto stop = fail; if (x < 0) { "
         "stop(\"n\"); } return x; }",
         NULL},
        /* R-TYPE-0044, R-FUNC-0015: a callable signature excludes a never result. */
        {"never-callable-constraint",
         "@generic<F: fn(str) -> never> i32 guard(F stop, i32 x) { return x; }",
         "R-DIAG-TYPE-001"},
        {"never-lambda",
         "i32 probe() { fn never halt(str m) { panic(m); } return 0; }",
         "R-DIAG-TYPE-001"},
        {"never-return", "never bad() { return; }", "R-DIAG-FLOW-001"},
        {"never-end", "never bad() { }", "R-DIAG-FLOW-001"},
        {"never-partial-end",
         "never bad(bool c) { if (c == true) { panic(\"x\"); } }",
         "R-DIAG-FLOW-001"},
        {"never-must-use", "@must_use never bad() { panic(\"x\"); }", "R-DIAG-USE-001"},
        /* R-BORROW-0002: an active exclusive borrow excludes reads of its storage too. */
        {"exclusive-read-local",
         "i32 probe() { i32 a = 1; i32* s = &a; i32 r = a; *s = 2; return r; }",
         "R-DIAG-BORROW-001"},
        {"exclusive-read-slice",
         "i32 probe() { i32[4] v = {1, 2, 3, 4}; i32[] w = v[0usize..2usize]; i32 r = v[1]; "
         "w[0] = 9; return r; }",
         "R-DIAG-BORROW-001"},
        {"exclusive-read-copy",
         "struct Box { i32 v; i32 w; }; i32 probe() { Box b = {.v = 1, .w = 2}; i32* s = "
         "&b.v; Box c = b; *s = 2; return c.w; }",
         "R-DIAG-BORROW-001"},
        {"exclusive-read-parameter",
         "struct Box { i32 v; i32 w; }; void probe(Box* p) { i32* t = &p->v; i32 r = p->v; *t "
         "= r; }",
         "R-DIAG-BORROW-001"},
        {"exclusive-read-suspended-parent",
         "void probe(i32* p) { i32* t = &*p; i32 r = *p; *t = r; }",
         "R-DIAG-BORROW-001"},
        /* Reads through the borrow, of disjoint fields and after the last use stay valid. */
        {"exclusive-read-valid",
         "struct Box { i32 v; i32 w; }; i32 through(i32 a) { i32* s = &a; const i32* t = &*s; "
         "i32 r = *t; *s = 4; return r + a; } void fields(Box* p) { i32* t = &p->v; i32 r = "
         "p->w; p->w = 1; const i32* q = &p->w; *t = r + *q; } i32 local() { Box b = {.v = 1, "
         ".w = 2}; i32* s = &b.v; i32 r = b.w; *s = 2; return r + b.v; }",
         NULL},
        /* A plain value stored into an element in a loop is not a borrow of the container. */
        {"loop-element-assignment",
         "usize probe(u8[] symbols, u32[] freq) { usize produced = 0usize; u8 current = 3; "
         "while (produced < 2usize) { symbols[produced] = current; freq[current as usize] += "
         "1u32; produced += 1usize; } return produced; }",
         NULL},
        /* L10.3: an exclusive borrow or a mutable slice result reborrows its inputs. */
        {"exclusive-results",
         "struct Counter { i32[4] slots; i32 total; }; i32* Counter::slot(Counter* this, usize "
         "i) { return &this->slots[i]; } i32[] Counter::window(Counter* this) { return "
         "this->slots[0usize..2usize]; } i32* pick(i32* l, i32* r, bool left) { if (left == "
         "true) { return l; } return r; } const i32* first(const i32[] v) { return &v[0]; } "
         "i32 probe() { Counter c = {.slots = {1, 2, 3, 4}, .total = 0}; *c.slot(1usize) = 5; "
         "i32* s = c.slot(0usize); *s += 1; i32[] w = c.window(); w[1] += 1; i32 a = 1; i32 b = "
         "2; *pick(&a, &b, true) = 3; const i32[] view = c.slots[0usize..4usize]; return "
         "*first(view) + a; }",
         NULL},
        {"exclusive-result-reads-source",
         "struct Counter { i32[4] slots; i32 total; }; i32* Counter::slot(Counter* this, usize "
         "i) { return &this->slots[i]; } i32 probe() { Counter c = {.slots = {1, 2, 3, 4}, "
         ".total = 0}; i32* s = c.slot(1usize); i32 r = c.total; *s = 2; return r; }",
         "R-DIAG-BORROW-001"},
        {"exclusive-result-twice",
         "struct Counter { i32[4] slots; i32 total; }; i32* Counter::slot(Counter* this, usize "
         "i) { return &this->slots[i]; } void probe() { Counter c = {.slots = {1, 2, 3, 4}, "
         ".total = 0}; i32* s = c.slot(0usize); i32* t = c.slot(1usize); *s = 1; *t = 2; }",
         "R-DIAG-BORROW-001"},
        {"exclusive-result-local", "i32* bad() { i32 x = 1; return &x; }", "R-DIAG-BORROW-002"},
        {"exclusive-result-from-shared", "i32* bad(const i32* p) { return p; }", "R-DIAG-TYPE-001"},
        /* L13.3: an async body holds a temporary borrow in a hidden frame local. */
        {"exclusive-result-async-temporary",
         "struct Counter { i32[4] slots; i32 total; }; i32* Counter::slot(Counter* this, usize "
         "i) { return &this->slots[i]; } async void probe() { Counter c = {.slots = {1, 2, 3, "
         "4}, .total = 0}; *c.slot(0usize) = 1; }",
         NULL},
        /* L10.4: optional borrows as parameters carry the origin of the borrow they hold. */
        {"optional-borrow-parameters",
         "void set(o<i32*> t, i32 v) { switch (move t) { case variant o::some(item): **item = "
         "v; break; case variant o::none: break; } } i32 get(o<const i32*> v) { switch (v) { "
         "case variant o::some(item): return **item; case variant o::none: return 0; } } i32 "
         "probe() { i32 x = 1; set(o::some(&x), 2); return get(o::some(&x)) + get(o::none); }",
         NULL},
        {"optional-borrow-source-write",
         "i32 get(o<const i32*> v) { switch (v) { case variant o::some(item): return **item; "
         "case variant o::none: return 0; } } i32 probe() { i32 x = 1; o<const i32*> r = "
         "o::some(&x); x = 2; return get(r); }",
         "R-DIAG-BORROW-001"},
        {"optional-borrow-async-frame",
         "async i32 later(o<const i32*> v) { return 0; }",
         "R-DIAG-ASYNC-001"},
        /* R-BORROW-0007: moving a borrow parameter does not end the storage it designates. */
        {"moved-borrow-parameter",
         "void write(i32* target, i32 value) { i32* moved = move target; *moved = value; }",
         NULL},
        /* R-BORROW-0019: a borrow stored through a parameter derives from that parameter or,
           recorded in the function's store contract, from another parameter (L15.2). */
        {"store-through-parameter",
         "struct Named { str name; i32 id; }; void set_name(Named* n, str s) { n->name = s; }",
         NULL},
        {"store-through-parameter-valid",
         "struct Named { str name; i32 id; }; void fixed(Named* n) { n->name = \"fixed\"; } "
         "void same(Named* n) { str old = n->name; n->name = old; }",
         NULL},
        /* R-BORROW-0018: a borrow of storage holding borrows is a borrow of that storage;
           values copied out of it carry only the held origins. */
        {"view-storage-borrow-write",
         "struct Named { str name; i32 id; }; i32 probe(str text) { Named n = {.name = text, "
         ".id = 1}; const Named* pn = &n; n.id = 2; return pn->id; }",
         "R-DIAG-BORROW-001"},
        {"view-storage-whole-read",
         "struct Named { str name; i32 id; }; i32 probe(str text) { Named n = {.name = text, "
         ".id = 1}; i32* pid = &n.id; Named m = n; *pid = 3; return m.id; }",
         "R-DIAG-BORROW-001"},
        {"view-storage-value-copies",
         "struct Named { str name; i32 id; }; void touch(Named* n) { n->id += 1; } i32 "
         "field(str text, str other) { Named n = {.name = text, .id = 1}; str s = n.name; "
         "touch(&n); n.name = other; return len(s) as i32; }",
         NULL},
        /* L10.5: containers of strings carry the origins of the strings they hold. */
        {"string-containers",
         "usize probe(str text) throws std.array::push_error<str>, std.list::push_error<str>, "
         "std.dict::insert_error<str, str> { array<str> names = [text, \"b\"]; const str[] "
         "view = std.array::as_slice(&names); str copy = view[0]; std.array::push(&names, "
         "copy); list<str> items = "
         "std.list::create::<str>(); std.list::push_back(&items, text) as void; dict<str, str> "
         "table = std.dict::create::<str, str>(); std.dict::insert(&table, text, copy) as void; "
         "usize total = len(table); for (const str* name in &names) { total += len(*name); } "
         "return total; }",
         NULL},
        {"string-container-source-write",
         "usize probe() throws std.alloc::alloc_error, std.array::push_error<str> { "
         "std.string::string owned = std.string::from_str(\"owned\"); array<str> names = "
         "std.array::create::<str>(); std.array::push(&names, std.string::as_str(&owned)); "
         "std.string::append_str(&owned, \"x\"); return len(names); }",
         "R-DIAG-BORROW-001"},
        {"string-container-pop-escape",
         "o<str> probe() throws std.alloc::alloc_error, std.array::push_error<str> { "
         "std.string::string temp = std.string::from_str(\"t\"); array<str> names = "
         "std.array::create::<str>(); std.array::push(&names, std.string::as_str(&temp)); "
         "return std.array::pop(&names); }",
         "R-DIAG-BORROW-002"},
        {"string-container-parameter-store",
         "void add(array<str>* names, str s) throws std.array::push_error<str> { "
         "std.array::push(names, s); }",
         NULL},
        /* A string stored through a slice of a string array reaches the array itself. */
        {"string-array-slice-store",
         "usize take(str v) { return len(v); } usize probe() throws std.alloc::alloc_error { "
         "str[2] local = {\"a\", \"b\"}; str[] w = local[0usize..2usize]; { "
         "std.string::string temp = std.string::from_str(\"t\"); w[0] = "
         "std.string::as_str(&temp); } return take(local[0]); }",
         "R-DIAG-BORROW-002"},
        /* R-EXPR-0023 discards an exclusive borrow; R-STMT-0014 iterates a borrowed container. */
        {"borrow-discard-and-iteration",
         "usize probe(const list<i32>* values) throws std.list::push_error<i32> { list<i32> "
         "local = std.list::create::<i32>(); std.list::push_back(&local, 7) as void; usize s = "
         "0usize; for (const i32* v in values) { s += *v as usize; } return s + len(local); }",
         NULL},
        /* L10.6: containers of shared borrows and slices carry the origins of their views. */
        {"view-containers",
         "struct Point { i32 x; i32 y; }; i32 probe() { Point a = {.x = 1, .y = 2}; i32[4] "
         "values = {1, 2, 3, 4}; try { list<const Point*> points = std.list::create::<const "
         "Point*>(); std.list::push_back(&points, &a) as void; array<const i32[]> parts = "
         "[values[0usize..2usize]]; std.array::push(&parts, values[2usize..4usize]); return "
         "(len(points) + len(parts)) as i32; } catch (std.list::push_error<const Point*> e) { e "
         "as void; return -1; } catch (std.array::push_error<const i32[]> e) { e as void; return "
         "-2; } }",
         NULL},
        {"view-container-source-write",
         "struct Point { i32 x; i32 y; }; usize probe() { Point a = {.x = 1, .y = 2}; "
         "list<const Point*> points = std.list::create::<const Point*>(); try { "
         "std.list::push_back(&points, &a) as void; } catch (std.list::push_error<const Point*> "
         "e) { e as void; } a.x = 5; return len(points); }",
         "R-DIAG-BORROW-001"},
        {"view-container-dangling",
         "i32 probe() { array<const i32[]> parts = std.array::create::<const i32[]>(); { "
         "i32[2] inner = {1, 2}; try { std.array::push(&parts, inner[0usize..2usize]); } catch "
         "(std.array::push_error<const i32[]> e) { e as void; } } return len(parts) as i32; }",
         "R-DIAG-BORROW-002"},
        /* R-BORROW-0007, R-BORROW-0025: an insertion error carries the staged element and its
           origins, through a call as well. */
        {"insertion-error-escape",
         "usize probe() throws std.array::push_error<const i32[]> { i32[2] local = {1, 2}; "
         "array<const i32[]> parts = std.array::create::<const i32[]>(); "
         "std.array::push(&parts, local[0usize..2usize]); return len(parts); }",
         "R-DIAG-BORROW-002"},
        {"insertion-error-dict-escape",
         "usize probe() throws std.dict::insert_error<i32, const i32*> { i32 local = 1; "
         "dict<i32, const i32*> index = std.dict::create::<i32, const i32*>(); "
         "std.dict::insert(&index, 1, &local) as void; return len(index); }",
         "R-DIAG-BORROW-002"},
        {"insertion-error-literal-escape",
         "usize probe() throws std.array::push_error<const i32[]> { i32[2] local = {1, 2}; "
         "array<const i32[]> parts = [local[0usize..1usize], local[1usize..2usize]]; return "
         "len(parts); }",
         "R-DIAG-BORROW-002"},
        {"insertion-error-outlives-source",
         "i32 probe() { try { i32[1] data = {7}; list<const i32*> items = "
         "std.list::create::<const i32*>(); std.list::push_back(&items, &data[0]) as void; "
         "return len(items) as i32; } catch (std.list::push_error<const i32*> failure) { switch "
         "(move failure) { case variant std.list::push_error::allocation_failed(move payload): "
         "return *payload.value; } } }",
         "R-DIAG-BORROW-002"},
        {"insertion-error-call-outlives-source",
         "usize count(const i32[] view) throws std.array::push_error<const i32[]> { "
         "array<const i32[]> parts = std.array::create::<const i32[]>(); std.array::push(&parts, "
         "view); return len(parts); } i32 probe() { try { i32[2] data = {1, 2}; return "
         "count(data[0usize..2usize]) as i32; } catch (std.array::push_error<const i32[]> "
         "failure) { switch (move failure) { case variant "
         "std.array::push_error::allocation_failed(move payload): return "
         "payload.value[0]; } } }",
         "R-DIAG-BORROW-002"},
        {"insertion-error-parameter-contract",
         "usize count(const i32[] view) throws std.array::push_error<const i32[]> { "
         "array<const i32[]> parts = std.array::create::<const i32[]>(); std.array::push(&parts, "
         "view); return len(parts); } i32 probe() { i32[2] data = {1, 2}; try { return "
         "count(data[0usize..2usize]) as i32; } catch (std.array::push_error<const i32[]> e) { e "
         "as void; return -1; } }",
         NULL},
        /* A string read back from a local container of parameter views designates what the
           container holds, not the container. */
        {"container-view-return",
         "str first(str text) throws std.array::push_error<str> { array<str> names = "
         "std.array::create::<str>(); std.array::push(&names, text); const str[] view = "
         "std.array::as_slice(&names); str copy = view[0]; std.array::push(&names, copy); "
         "return copy; }",
         NULL},
        {"container-view-return-local",
         "str bad() throws std.alloc::alloc_error { std.string::string temp = "
         "std.string::from_str(\"t\"); array<str> names = std.array::create::<str>(); try { "
         "std.array::push(&names, std.string::as_str(&temp)); } catch "
         "(std.array::push_error<str> e) { e as void; } const str[] view = "
         "std.array::as_slice(&names); return view[0]; }",
         "R-DIAG-BORROW-002"},
        /* R-BORROW-0007: the length of a view is a use of it. */
        {"view-length-after-source",
         "usize probe() throws std.alloc::alloc_error { str s = \"a\"; { std.string::string "
         "temp = std.string::from_str(\"t\"); str inner = std.string::as_str(&temp); s = "
         "inner; } return len(s); }",
         "R-DIAG-BORROW-002"},
        /* L10.7: an aggregate holding a borrow is a parameter of a @scoped async function. */
        {"scoped-aggregate-parameters",
         "struct Window { const i32[] values; usize start; }; struct Named { str name; i32 id; "
         "}; @scoped async usize sum(Window w) throws std.async::start_error { return "
         "len(w.values); } @scoped async usize size(const Named* n) throws "
         "std.async::start_error { return len(n->name); }",
         NULL},
        {"ordinary-async-aggregate-parameter",
         "struct Window { const i32[] values; usize start; }; async usize sum(Window w) { "
         "return len(w.values); }",
         "R-DIAG-ASYNC-001"},
        /* L13.6 closed the async never function (R-TYPE-0007). */
        {"never-async-boundary", "async never bad() { panic(\"x\"); }", NULL},
        /* L11.4 closed the container elements that hold borrows in any form. */
        {"borrow-holding-element-boundary",
         "struct Named { str name; i32 id; }; usize probe() { list<Named> items = "
         "std.list::create::<Named>(); return len(items); }",
         NULL},
        {"exclusive-view-holder-local",
         "struct Holder { str name; }; usize probe(str text) { Holder h = {.name = \"a\"}; "
         "Holder* p = &h; p->name = text; return len(h.name); }",
         NULL},
        {"exclusive-view-holder-result",
         "struct Holder { str name; }; Holder* pick(Holder* h) { return h; }",
         NULL},
        {"exclusive-view-element-store",
         "usize probe(str text) { list<str> names = std.list::create::<str>(); try { "
         "*std.list::push_back(&names, \"x\") = text; } catch (std.list::push_error<str> e) { "
         "e as void; } return len(names); }",
         NULL},
        {"exclusive-view-holder-parameter",
         "struct Holder { str name; }; void rename(Holder* h) { h->name = \"fixed\"; }",
         NULL},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char source[2048];
        const int length =
            snprintf(source, sizeof(source), "module test.l10_regressions; %s", cases[i].source);
        RFrontendContext *context = r_frontend_create(NULL);
        R_SEMANTIC_CHECK(context != NULL && length > 0 && (size_t)length < sizeof(source));
        if (context == NULL)
            return;
        (void)r_semantic_add_bytes(context, cases[i].name, (const uint8_t *)source, (size_t)length);
        const RFrontendStatus status = r_frontend_analyze(context);
        const bool valid =
            status == (cases[i].diagnostic == NULL ? R_FRONTEND_OK : R_FRONTEND_INVALID_SOURCE) &&
            (cases[i].diagnostic == NULL ||
             r_semantic_diagnostic_code_count(context, cases[i].diagnostic) != 0U) &&
            ((cases[i].diagnostic != NULL &&
              strcmp(cases[i].diagnostic, "R-DIAG-SLICE-001") == 0) ||
             r_semantic_diagnostic_code_count(context, "R-DIAG-SLICE-001") == 0U);
        if (!valid) {
            (void)fprintf(stderr, "l10 regression %s status %d\n", cases[i].name, (int)status);
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

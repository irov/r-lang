/* L15 (docs/language-completeness-roadmap.ru.md): region contracts without region syntax. Each
 * case is accepted, or is rejected with the diagnostic code and, where given, the rule it names;
 * no case may leave a construct outside the implemented slice (R-DIAG-SLICE-001). */
static void r_semantic_test_l15_regressions(void) {
    static const struct {
        const char *name;
        const char *source;
        const char *diagnostic;
    } cases[] = {
        /* L15.1, R-BORROW-0009: a result taken only from what the storage a borrow or slice
           parameter designates holds depends on what the argument holds, not on the argument, so
           the argument's storage is borrowed again, written, moved or destroyed meanwhile. */
        {"held-result-two-live",
         "struct Decoder { const u8[] input; usize at; }; const u8[] take(Decoder* this, usize n) "
         "{ const u8[] part = this->input[this->at..this->at + n]; this->at += n; return part; } "
         "usize f(const u8[] data) { Decoder d = Decoder {.input = data, .at = 0usize}; const u8[] "
         "head = take(&d, 1usize); const u8[] body = take(&d, 2usize); return len(head) + "
         "len(body); }",
         NULL},
        {"held-result-shared-receiver-write",
         "struct Decoder { const u8[] input; usize at; }; const u8[] peek(const Decoder* this, "
         "usize n) { return this->input[this->at..this->at + n]; } usize f(const u8[] data) { "
         "Decoder d = Decoder {.input = data, .at = 0usize}; const u8[] head = peek(&d, 1usize); "
         "d.at += 1usize; const u8[] body = peek(&d, 2usize); return len(head) + len(body); }",
         NULL},
        {"held-result-outlives-holder",
         "struct Decoder { const u8[] input; usize at; }; const u8[] take(Decoder* this, usize n) "
         "{ const u8[] part = this->input[this->at..this->at + n]; this->at += n; return part; } "
         "const u8[] f(const u8[] data) { Decoder d = Decoder {.input = data, .at = 0usize}; const "
         "u8[] head = take(&d, 1usize); return head; }",
         NULL},
        {"held-result-move-holder",
         "struct Decoder { const u8[] input; usize at; }; const u8[] take(Decoder* this, usize n) "
         "{ const u8[] part = this->input[this->at..this->at + n]; this->at += n; return part; } "
         "usize f(const u8[] data) { Decoder d = Decoder {.input = data, .at = 0usize}; const u8[] "
         "head = take(&d, 1usize); Decoder e = move d; const u8[] next = take(&e, 1usize); return "
         "len(head) + len(next); }",
         NULL},
        {"held-result-transitive",
         "struct Decoder { const u8[] input; usize at; }; const u8[] take(Decoder* this, usize n) "
         "{ const u8[] part = this->input[this->at..this->at + n]; this->at += n; return part; } "
         "const u8[] skip_take(Decoder* this) { const u8[] first = take(this, 1usize); this->at += "
         "1usize; return first; } usize f(const u8[] data) { Decoder d = Decoder {.input = data, "
         ".at = 0usize}; const u8[] a = skip_take(&d); const u8[] b = skip_take(&d); return len(a) "
         "+ len(b); }",
         NULL},
        {"held-result-survives-container-clear",
         "struct Named { str name; i32 id; }; str name_of(const Named* item) { return item->name; "
         "} usize f(str word) throws std.array::push_error<Named> { array<Named> items = "
         "std.array::create::<Named>(); std.array::push(&items, Named {.name = word, .id = 1}); "
         "const Named* first = &items[0usize]; str name = name_of(first); "
         "std.array::clear(&items); return len(name); }",
         NULL},
        {"held-result-loop-view",
         "struct Decoder { const u8[] input; usize at; }; const u8[] take(Decoder* this, usize n) "
         "{ const u8[] part = this->input[this->at..this->at + n]; this->at += n; return part; } "
         "usize f(const u8[] data) { Decoder d = Decoder {.input = data, .at = 0usize}; Decoder* p "
         "= &d; const u8[] last = data[0usize..0usize]; usize i = 0usize; while (i < 3usize) { "
         "const u8[] part = take(p, 1usize); last = part; i += 1usize; } return len(last); }",
         NULL},
        {"held-result-loop-method",
         "struct Decoder { const u8[] input; usize at; }; const u8[] Decoder::take(Decoder* this, "
         "usize n) { const u8[] part = this->input[this->at..this->at + n]; this->at += n; return "
         "part; } const u8[] Decoder::last_of(Decoder* this, usize count) { const u8[] last = "
         "this->input[0usize..0usize]; usize total = 0usize; usize i = 0usize; while (i < count) { "
         "total += len(last); last = this->take(1usize); i += 1usize; } this->at += total; return "
         "last; } usize f(const u8[] data) { Decoder d = Decoder {.input = data, .at = 0usize}; "
         "const u8[] a = d.last_of(2usize); const u8[] b = d.last_of(1usize); return len(a) + "
         "len(b); }",
         NULL},
        /* L15.1: a result that borrows the storage a parameter designates, or a view reached
           from it through an exclusive view, still keeps that storage borrowed; a result taken
           from a local source still ends with it. */
        {"storage-result-keeps-conflict",
         "struct Decoder { const u8[] input; usize at; }; const usize* position(Decoder* this) { "
         "return &this->at; } usize f(const u8[] data) { Decoder d = Decoder {.input = data, .at "
         "= 0usize}; const usize* at = position(&d); d.at += 1usize; return *at; }",
         "R-DIAG-BORROW-001 [R-BORROW-0002]"},
        {"mixed-result-keeps-storage",
         "struct Pair { const i32[] view; i32[2] items; }; const i32[] pick(Pair* this, bool "
         "inner) { if (inner == true) { return this->items[..]; } return this->view; } i32 "
         "f(const i32[] data) { Pair p = Pair {.view = data, .items = {1, 2}}; const i32[] a = "
         "pick(&p, true); p.items[0usize] = 5; return a[0usize]; }",
         "R-DIAG-BORROW-001 [R-BORROW-0002]"},
        {"held-result-local-source-escape",
         "struct Decoder { const u8[] input; usize at; }; const u8[] take(Decoder* this, usize n) "
         "{ const u8[] part = this->input[this->at..this->at + n]; this->at += n; return part; } "
         "const u8[] f() { u8[4] buf = {1u8, 2u8, 3u8, 4u8}; Decoder d = Decoder {.input = "
         "buf[..], .at = 0usize}; const u8[] head = take(&d, 1usize); return head; }",
         "R-DIAG-BORROW-002 [R-BORROW-0007]"},
        {"held-result-source-write",
         "struct Decoder { const u8[] input; usize at; }; const u8[] take(Decoder* this, usize n) "
         "{ const u8[] part = this->input[this->at..this->at + n]; this->at += n; return part; } "
         "usize f() { u8[4] buf = {1u8, 2u8, 3u8, 4u8}; Decoder d = Decoder {.input = buf[..], "
         ".at = 0usize}; const u8[] head = take(&d, 1usize); buf[0usize] = 9u8; return len(head); "
         "}",
         "R-DIAG-BORROW-001 [R-BORROW-0002]"},
        {"held-exclusive-alias",
         "struct Holder { i32* slot; }; i32* get(Holder* this) { i32* p = this->slot; return p; } "
         "i32 f() { i32 x = 1; Holder h = Holder {.slot = &x}; i32* a = get(&h); i32* b = "
         "get(&h); *a = 2; *b = 3; return x; }",
         "R-DIAG-BORROW-001 [R-BORROW-0002]"},
        {"held-result-loop-local-escape",
         "struct Decoder { const u8[] input; usize at; }; const u8[] take(Decoder* this, usize n) "
         "{ const u8[] part = this->input[this->at..this->at + n]; this->at += n; return part; } "
         "usize f(const u8[] data) { const u8[] last = data[0usize..0usize]; usize i = 0usize; "
         "while (i < 3usize) { u8[2] buf = {1u8, 2u8}; Decoder d = Decoder {.input = buf[..], .at "
         "= 0usize}; last = take(&d, 1usize); i += 1usize; } return len(last); }",
         "R-DIAG-BORROW-002 [R-BORROW-0007]"},
        /* L15.1, R-BORROW-0018: a view read out of storage other than the storage a parameter
           designates, such as `*pp` for `pp = &v`, keeps what it designates rather than reading as
           what that parameter holds. */
        {"view-read-from-other-storage",
         "struct Named { str name; i32 id; }; const Named* via_local(const array<Named>* items) { "
         "const Named* v = &(*items)[0usize]; const (const Named*)* pp = &v; return *pp; } usize "
         "f(array<Named>* items) { const Named* e = via_local(items); std.array::clear(items); "
         "return len(e->name); }",
         "R-DIAG-BORROW-001 [R-LIB-0019]"},
        /* L15.2, R-BORROW-0019: a function that stores views of one parameter into the storage
           another view parameter designates records it in its store contract; the caller adds
           those views to what the target argument's storage holds, also through its own
           parameters and through the exclusive views that storage holds. */
        {"store-contract-split-into",
         "void split_into(str text, array<str>* parts) throws std.array::push_error<str> { "
         "std.array::push(parts, text); } usize f(str line) throws std.array::push_error<str> { "
         "array<str> parts = std.array::create::<str>(); split_into(line, &parts); "
         "split_into(line, &parts); return len(parts); }",
         NULL},
        {"store-contract-transitive",
         "struct Parser { array<str> tokens; }; void feed(Parser* this, str chunk) throws "
         "std.array::push_error<str> { std.array::push(&this->tokens, chunk); } void "
         "feed_twice(Parser* this, str a, str b) throws std.array::push_error<str> { feed(this, "
         "a); feed(this, b); } usize f(str a, str b) throws std.array::push_error<str> { Parser p "
         "= Parser {.tokens = std.array::create::<str>()}; feed_twice(&p, a, b); return "
         "len(p.tokens); }",
         NULL},
        {"store-contract-generic-push",
         "@generic<T: copy> struct Stack { array<T> items; }; @generic<T: copy> void "
         "push(Stack<T>* this, T v) throws std.array::push_error<T> { std.array::push(&this->items, "
         "v); } usize f(str a) throws std.array::push_error<str> { Stack<str> s = Stack<str> "
         "{.items = std.array::create::<str>()}; push::<str>(&s, a); return len(s.items); }",
         NULL},
        {"store-contract-held-slot",
         "(str)* first_slot(array<(str)*>* slots) { return (*slots)[0]; } void "
         "put_first(array<(str)*>* slots, str text) { *first_slot(slots) = text; } usize f(str "
         "text) { str x = \"a\"; array<(str)*> arr = std.array::create::<(str)*>(); try { "
         "std.array::push(&arr, &x); } catch (std.array::push_error<(str)*> e) { move e as void; "
         "return 0usize; } put_first(&arr, text); move arr as void; return len(x); }",
         NULL},
        {"store-freeze-read",
         "struct Item { i32 x; }; i32 remember(Item* item, array<const Item*>* out) throws "
         "std.array::push_error<const Item*> { std.array::push(out, &*item); return item->x; }",
         NULL},
        {"store-contract-dangling",
         "import std.string; import std.alloc; struct Parser { array<str> tokens; }; void "
         "feed(Parser* this, str chunk) throws std.array::push_error<str> { "
         "std.array::push(&this->tokens, chunk); } usize f() throws std.array::push_error<str>, "
         "std.alloc::alloc_error { Parser p = Parser {.tokens = std.array::create::<str>()}; { "
         "std.string::string s = std.string::from_str(\"x\"); feed(&p, std.string::as_str(&s)); } "
         "return len(p.tokens[0usize]); }",
         "R-DIAG-BORROW-002 [R-BORROW-0007]"},
        {"store-contract-local-into-parameter",
         "import std.string; import std.alloc; struct Parser { array<str> tokens; }; void "
         "feed(Parser* this, str chunk) throws std.array::push_error<str> { "
         "std.array::push(&this->tokens, chunk); } void f(Parser* p) throws "
         "std.array::push_error<str>, std.alloc::alloc_error { std.string::string s = "
         "std.string::from_str(\"abc\"); feed(p, std.string::as_str(&s)); }",
         "R-DIAG-BORROW-002 [R-BORROW-0019]"},
        {"store-contract-held-slot-dangling",
         "import std.string; import std.alloc; (str)* first_slot(array<(str)*>* slots) { return "
         "(*slots)[0]; } void put_first(array<(str)*>* slots, str text) { *first_slot(slots) = "
         "text; } usize f() throws std.alloc::alloc_error { str x = \"a\"; array<(str)*> arr = "
         "std.array::create::<(str)*>(); try { std.array::push(&arr, &x); } catch "
         "(std.array::push_error<(str)*> e) { move e as void; return 0usize; } { "
         "std.string::string s = std.string::from_str(\"tmp\"); put_first(&arr, "
         "std.string::as_str(&s)); } move arr as void; return len(x); }",
         "R-DIAG-BORROW-002 [R-BORROW-0007]"},
        {"store-freeze-write",
         "struct Item { i32 x; }; void remember(Item* item, array<const Item*>* out) throws "
         "std.array::push_error<const Item*> { std.array::push(out, &*item); item->x = 5; }",
         "R-DIAG-BORROW-001 [R-BORROW-0002]"},
        {"store-by-value-holder-target",
         "struct Holder { array<str>* names; }; void add(Holder h, str s) throws "
         "std.array::push_error<str> { std.array::push(h.names, s); }",
         "R-DIAG-BORROW-002 [R-BORROW-0019]"},
        /* L15-2, R-BORROW-0019: a loop that stores through a parameter reads that parameter as
           the target; the read brings no origin to the store, so a container parameter may take
           its own elements again. */
        {"loop-push-own-elements",
         "void dup(array<str>* names) throws std.array::push_error<str> { for (usize i = 0usize; i "
         "< 2usize; i += 1usize) { std.array::push(names, (*names)[0usize]); } }",
         NULL},
        {"loop-self-view-through-local",
         "struct window { i32[4] buf; const i32[] view; }; void refresh(window* this, const i32[] "
         "other) { const i32[] v = other; for (usize i = 0usize; i < 2usize; i += 1usize) { "
         "this->view = v; v = this->buf[..]; } }",
         "R-DIAG-BORROW-002 [R-BORROW-0019]"},
        /* L15-3, R-BORROW-0025: a store through a view reaches a component that is not known, so
           the target keeps no former component origins alone. */
        {"stale-component-after-view-store",
         "import std.string; import std.alloc; struct Pair { str a; str b; }; usize f(str x, str "
         "y) throws std.alloc::alloc_error { Pair p = Pair {.a = x, .b = y}; { std.string::string s "
         "= std.string::from_str(\"tmp\"); Pair* pp = &p; pp->a = std.string::as_str(&s); } return "
         "len(p.a); }",
         "R-DIAG-BORROW-002 [R-BORROW-0007]"},
        /* L15.3, R-BORROW-0018: an owner carries the regions of the views its payload holds; the
           payload of a unique owner is storage of that owner, and the handles of a shared owner
           share one region fixed at creation. */
        {"owner-list-of-words",
         "struct Node { own Node*? next; str word; }; usize f(str a, str b) { own Node* tail = new "
         "Node {.next = null, .word = b}; own Node* head = new Node {.next = move tail, .word = a}; "
         "return len(head->word); }",
         NULL},
        {"owner-return-parameter-view",
         "struct Node { own Node*? next; str word; }; own Node* make(str word) { return new Node "
         "{.next = null, .word = word}; } usize f(str a) { own Node* n = make(a); return "
         "len(n->word); }",
         NULL},
        {"owner-store-through-holder-parameter",
         "struct Node { own Node*? next; str word; }; struct Holder { own Node* node; }; void "
         "set(Holder* h, str w) { h->node->word = w; } usize f(str a, str b) { Holder h = Holder "
         "{.node = new Node {.next = null, .word = a}}; set(&h, b); return len(h.node->word); }",
         NULL},
        {"shared-owner-clone",
         "struct Item { str name; }; usize f(str a) { rc Item x = new rc Item {.name = a}; rc Item "
         "y = std.rc::clone(&x); move x as void; return len(y->name); }",
         NULL},
        {"shared-owner-get-mut-same-region",
         "struct Item { str name; }; usize f(str a) { rc Item x = new rc Item {.name = a}; Item*? "
         "unique = std.rc::get_mut(&x); if (unique != null) { unique->name = a; } return "
         "len(x->name); }",
         NULL},
        {"owner-payload-dangling",
         "import std.string; import std.alloc; struct Node { own Node*? next; str word; }; usize "
         "f(str a) throws std.alloc::alloc_error { own Node* head = new Node {.next = null, .word "
         "= a}; { std.string::string s = std.string::from_str(\"tmp\"); head->word = "
         "std.string::as_str(&s); } return len(head->word); }",
         "R-DIAG-BORROW-002 [R-BORROW-0007]"},
        {"owner-return-local-view",
         "struct Holder { const i32* item; }; own Holder* make() { i32 x = 41; return new Holder "
         "{.item = &x}; }",
         "R-DIAG-BORROW-002 [R-BORROW-0007]"},
        {"owner-payload-borrow-then-reassign",
         "struct Node { own Node*? next; str word; }; usize f(str a, str b) { own Node* n = new "
         "Node {.next = null, .word = a}; const str* w = &n->word; n = new Node {.next = null, "
         ".word = b}; return len(*w); }",
         "R-DIAG-BORROW-001 [R-BORROW-0002]"},
        {"shared-owner-clone-dangling",
         "import std.string; import std.alloc; struct Item { str name; }; usize f() throws "
         "std.alloc::alloc_error, std.array::push_error<rc Item> { array<rc Item> keeps = "
         "std.array::create::<rc Item>(); { std.string::string s = "
         "std.string::from_str(\"tmp\"); rc Item x = new rc Item {.name = "
         "std.string::as_str(&s)}; std.array::push(&keeps, std.rc::clone(&x)); } return "
         "len(keeps[0usize]->name); }",
         "R-DIAG-BORROW-002 [R-BORROW-0007]"},
        {"shared-owner-get-mut-widen",
         "import std.string; import std.alloc; struct Item { str name; }; usize f(str a) throws "
         "std.alloc::alloc_error { rc Item x = new rc Item {.name = a}; std.string::string s = "
         "std.string::from_str(\"tmp\"); Item*? unique = std.rc::get_mut(&x); if (unique != "
         "null) { unique->name = std.string::as_str(&s); } return len(x->name); }",
         "R-DIAG-BORROW-002 [R-BORROW-0018]"},
        {"shared-owner-unscoped-thread",
         "struct Item { str name; }; i32 worker(arc Item item) { return len(item->name) as i32; } "
         "i32 f(str a) throws std.thread::thread_error { arc Item x = new arc Item {.name = a}; "
         "std.thread::join_handle<i32> h = std.thread::spawn(worker, move x); "
         "std.thread::join_result<i32> outcome = (move h).join(); outcome as void; return 0; }",
         "R-DIAG-ASYNC-001 [R-FUNC-0010]"},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char source[2048];
        const int length =
            snprintf(source, sizeof(source), "module test.l15_regressions; %s", cases[i].source);
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
            (void)fprintf(stderr, "l15 regression %s status %d\n", cases[i].name, (int)status);
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

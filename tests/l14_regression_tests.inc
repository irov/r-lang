/* L14 (docs/language-completeness-roadmap.ru.md): language acceptance. Each case is accepted, or
 * is rejected with the diagnostic code and, where given, the rule it names; no case may leave a
 * construct outside the implemented slice (R-DIAG-SLICE-001). */
static size_t r_semantic_l14_diagnostic_count(const RFrontendContext *context,
                                              const char *expected) {
    char code[64];
    const char *rule = strchr(expected, '[');
    const size_t code_length = rule == NULL ? strlen(expected) : (size_t)(rule - expected - 1);
    size_t count = 0U;

    if (code_length >= sizeof(code)) {
        return 0U;
    }
    (void)memcpy(code, expected, code_length);
    code[code_length] = '\0';
    for (size_t index = 0U; index < r_frontend_diagnostic_count(context); ++index) {
        const RDiagnostic *diagnostic = r_frontend_diagnostic(context, index);
        if ((diagnostic != NULL) && (strcmp(diagnostic->code, code) == 0) &&
            ((rule == NULL) ||
             ((diagnostic->rule_id != NULL) &&
              (strncmp(diagnostic->rule_id, rule + 1, strlen(rule + 1) - 1U) == 0) &&
              (strlen(diagnostic->rule_id) == strlen(rule + 1) - 1U)))) {
            count += 1U;
        }
    }
    return count;
}

static void r_semantic_test_l14_regressions(void) {
    static const struct {
        const char *name;
        const char *source;
        const char *diagnostic;
    } cases[] = {
        /* L14.1-S1, R-BORROW-0018: until L15.3 an owner carried no hidden region, so its payload held
           no view. Since L15.3 an owner carries the regions of its payload's views, as an aggregate
           does, and these forms are accepted; a payload adopted or rebuilt from a raw pointer still
           holds none. */
        {"owner-borrow-payload-escape",
         "struct Holder { const i32* item; }; own Holder* make() { i32 x = 41; return new Holder "
         "{.item = &x}; }",
         "R-DIAG-BORROW-002 [R-BORROW-0007]"},
        {"owner-borrow-payload-parameter",
         "struct Holder { const i32* item; }; own Holder* wrap(const i32* p) { return new Holder "
         "{.item = p}; }",
         NULL},
        {"owner-str-payload",
         "void keep(own str* text) { move text as void; }",
         NULL},
        {"arc-str-payload",
         "i32 share(str text) { arc str shared = new arc str(text); return 0; }",
         NULL},
        {"rc-borrow-struct-payload",
         "struct Holder { const i32* item; }; void keep(rc Holder h) { move h as void; }",
         NULL},
        {"weak-slice-payload",
         "void keep(weak arc (const i32[]) w) { move w as void; }",
         NULL},
        {"owner-iterator-payload",
         "void keep(own std.list::iter<i32>* it) { move it as void; }",
         NULL},
        {"owner-guard-payload",
         "import std.sync; void keep(arc std.sync::mutex_guard<i32> g) { move g as void; }",
         NULL},
        {"adopt-borrow-payload",
         "void take(raw (const i32*)* p) { unsafe { own (const i32*)* owner = core::adopt(p); raw "
         "(const i32*)* back = core::release(move owner); back as void; } }",
         "R-DIAG-TYPE-001 [R-BORROW-0018]"},
        {"arc-from-raw-borrow-payload",
         "struct Holder { const i32* item; }; void take(raw const Holder* p) { unsafe { arc Holder "
         "a = std.arc::from_raw(p); move a as void; } }",
         "R-DIAG-TYPE-001 [R-BORROW-0018]"},
        {"try-new-borrow-payload",
         "struct Holder { const i32* item; }; void keep(const i32* p) throws "
         "std.alloc::new_error<Holder> { auto owner = std.alloc::try_new(Holder {.item = p}); move "
         "owner as void; }",
         NULL},
        {"field-owner-self-reference-str",
         "struct Node { own Node*? next; str word; }; void keep(Node n) { move n as void; }",
         NULL},
        {"field-owner-later-aggregate",
         "struct Outer { own Inner*? inner; }; struct Inner { const i32* item; };",
         NULL},
        {"generic-owner-unproven",
         "@generic<T> own T* boxed(T value) { return new T(move value); }",
         NULL},
        {"generic-owner-field-unproven",
         "@generic<T> struct Box { own T* inner; };",
         NULL},
        {"generic-owner-proven-borrowed-argument",
         "@generic<T: unborrowed> own T* boxed(T value) { return new T(move value); } void "
         "probe(str text) { own str* b = boxed(text); move b as void; }",
         "R-DIAG-TYPE-001"},
        {"owner-region-free-payloads",
         "struct Label { constexpr str name; raw const i32*? spot; own i32* count; }; @generic<T: "
         "unborrowed> struct Box { own T* inner; }; @generic<T: unborrowed> own T* boxed(T value) "
         "{ return new T(move value); } i32 probe() { own Label* l = new Label {.name = \"n\", "
         ".spot = null, .count = new i32(3)}; arc Label shared = new arc Label {.name = \"m\", "
         ".spot = null, .count = new i32(4)}; Box<i64> b = Box<i64> {.inner = boxed(5i64)}; own "
         "(constexpr str)* c = new (constexpr str)(\"x\"); move c as void; return *l->count + "
         "*shared->count + (*b.inner as i32); }",
         NULL},
        /* L14.1-S2, R-TYPE-0033: `unborrowed` is not proven by a guard, lock outcome, container
           iterator or entry reference, which hold their creator's region. */
        {"unborrowed-rejects-iterator",
         "@generic<T: unborrowed> T keep(T v) { return move v; } i32 probe() throws "
         "std.alloc::alloc_error { list<i32> l = std.list::create::<i32>(); std.list::iter<i32> it "
         "= std.list::iter(&l); std.list::iter<i32> back = keep(move it); drop back; return 0; }",
         "R-DIAG-TYPE-001 [R-TYPE-0001]"},
        {"unborrowed-rejects-lock-outcome",
         "import std.sync; @generic<T: unborrowed> T keep(T v) { return move v; } i32 probe() { "
         "std.sync::mutex<i32> m = std.sync::mutex_new(1); std.sync::lock_result<i32> r = "
         "std.sync::lock(&m); std.sync::lock_result<i32> back = keep(move r); drop back; return 0; "
         "}",
         "R-DIAG-TYPE-001 [R-TYPE-0001]"},
        /* L14.1-S3, R-TYPE-0009: an owner, weak owner or raw pointer names a generic instance with
           its own type arguments, not with its first argument. */
        {"generic-owner-instance-type",
         "@generic<T> struct Cell { T value; }; i32 probe() { own Cell<i32>* a = new i32(1); "
         "return *a; }",
         "R-DIAG-TYPE-001 [R-INIT-0002]"},
        {"generic-raw-instance-type",
         "@generic<T> struct Cell { T value; }; i64 probe(raw const Cell<i64>* p) { unsafe { "
         "return *p; } }",
         "R-DIAG-TYPE-001 [R-STMT-0005]"},
        {"generic-weak-instance-type",
         "@generic<T> struct Cell { T value; }; void probe() { arc i64 s = new arc i64(1i64); weak "
         "arc Cell<i64> w = std.arc::downgrade(&s); move w as void; }",
         "R-DIAG-TYPE-001"},
        {"generic-owner-instances",
         "@generic<T> struct Cell { T value; }; @generic<T> enum Choice { Some(T), Nothing }; i32 "
         "probe() { own Cell<i32>* a = new Cell<i32> {.value = 1}; arc Cell<i32> b = new arc "
         "Cell<i32> {.value = 2}; rc Cell<i32> c = new rc Cell<i32> {.value = 3}; weak arc "
         "Cell<i32> w = std.arc::downgrade(&b); own Choice<i64>* d = new "
         "Choice<i64>(Choice<i64>::Some(4i64)); move w as void; move d as void; return a->value + "
         "b->value + c->value; }",
         NULL},
        /* L14.1-F2/F8/F9, R-BORROW-0018: a fixed array, option, guard, lock outcome, iterator or
           entry reference that holds views is an ordinary by-value parameter. */
        {"view-holding-parameters",
         "import std.sync; usize total((str)[2] w) { return len(w[0]) + len(w[1]); } void "
         "bump((i32*)[2] c) { *c[0] += 1; } const i32* larger((const i32*)[2] items) { return "
         "items[1]; } usize width(o<str> n) { switch (n) { case variant o::some(t): return "
         "len(*t); case variant o::none: return 0usize; } } i32 through(o<i32*?> c) { move c as "
         "void; return 0; } usize count(o<const i32[]> s) { move s as void; return 0usize; } i32 "
         "consume(std.sync::mutex_guard<i32> g) { i32 v = *std.sync::mutex_guard_ref(&g); "
         "std.sync::unlock(move g); return v; } void settle(std.sync::lock_result<i32> r) { drop "
         "r; } i32 pair(std.dict::entry_ref<i32, i32> e) { return *e.key + *e.value; } void "
         "walk(std.dict::iter<i32, i32> d, o<std.list::iter<i32>> l) { drop d; drop l; } i32 "
         "rest(std.list::iter<i32> cursor) { i32 sum = 0; while (true) { o<const i32*> item = "
         "std.list::next(&cursor); switch (item) { case variant o::some(value): sum += **value; "
         "break; case variant o::none: return sum; } } } ",
         NULL},
        {"view-holding-parameter-result-conflict",
         "const i32* larger((const i32*)[2] items) { return items[1]; } i32 probe() { i32 a = 1; "
         "i32 b = 2; (const i32*)[2] p = {&a, &b}; const i32* big = larger(p); b = 5; return *big; "
         "}",
         "R-DIAG-BORROW-001"},
        {"view-holding-parameter-exclusive-read-conflict",
         "void bump((i32*)[2] c) { *c[0] += 1; } i32 probe() { i32 a = 1; i32 b = 2; (i32*)[2] c = "
         "{&a, &b}; i32 seen = a; bump(move c); return seen + a; }",
         "R-DIAG-BORROW-001"},
        {"guard-parameter-returned-borrow",
         "import std.sync; const i32* peek(std.sync::mutex_guard<i32> g) { return "
         "std.sync::mutex_guard_ref(&g); }",
         "R-DIAG-BORROW-002"},
        /* L14.1-F13, R-LIB-0020, R-LIB-0022: a cursor yields entries and nodes borrowed from its
           container, not from the cursor object. */
        {"iterator-parameter-yields-container-borrow",
         "o<const i32*> head(std.list::iter<i32> cursor) { return std.list::next(&cursor); } "
         "o<std.dict::entry_ref<i32, i32>> first(std.dict::iter<i32, i32> cursor) { return "
         "std.dict::next(&cursor); } i32 probe(const list<i32>* l) { o<const i32*> h = "
         "head(std.list::iter(l)); switch (h) { case variant o::some(v): return **v; case variant "
         "o::none: return 0; } }",
         NULL},
        {"iterator-local-container-escape",
         "o<const i32*> head() throws std.alloc::alloc_error { list<i32> l = "
         "std.list::create::<i32>(); std.list::iter<i32> it = std.list::iter(&l); return "
         "std.list::next(&it); }",
         "R-DIAG-BORROW-002"},
        {"iterator-yield-outlives-container-mutation",
         "i32 probe() throws std.list::push_error<i32> { list<i32> l = std.list::create::<i32>(); "
         "std.list::push_back(&l, 1) as void; std.list::iter<i32> it = std.list::iter(&l); o<const "
         "i32*> h = std.list::next(&it); std.list::clear(&l); switch (h) { case variant "
         "o::some(v): return **v; case variant o::none: return 0; } }",
         "R-DIAG-BORROW-001"},
        /* L14.1-D5, R-STMT-0010: a Move switch moves a payload that holds views, such as a struct
           with an exclusive borrow or a container iterator, out of a variant. */
        {"view-holding-payload-moves",
         "struct Slot { i32* target; }; enum Pending { Write(Slot), Skip }; i32 "
         "rest(std.list::iter<i32> cursor) { i32 sum = 0; while (true) { o<const i32*> item = "
         "std.list::next(&cursor); switch (item) { case variant o::some(value): sum += **value; "
         "break; case variant o::none: return sum; } } } i32 probe(const list<i32>* l) { i32 v = "
         "1; o<Slot> s = o::some(Slot {.target = &v}); switch (move s) { case variant o::some(move "
         "t): *t.target = 2; break; case variant o::none: break; } Pending p = Pending::Write(Slot "
         "{.target = &v}); switch (move p) { case variant Pending::Write(move t): *t.target += 1; "
         "break; case variant Pending::Skip: break; } o<std.list::iter<i32>> c = "
         "o::some(std.list::iter(l)); switch (move c) { case variant o::some(move i): return "
         "rest(move i) + v; case variant o::none: return v; } }",
         NULL},
        {"view-holding-payload-move-conflict",
         "struct Slot { i32* target; }; i32 probe() { i32 v = 1; o<Slot> s = o::some(Slot {.target "
         "= &v}); switch (move s) { case variant o::some(move t): v = 5; *t.target = 2; break; "
         "case variant o::none: break; } return v; }",
         "R-DIAG-BORROW-001"},
        /* L14.1-F14, R-BORROW-0024, R-MEM-0015: a scoped start stages a Send Move value that holds
           views; an ordinary async frame still rejects it. */
        {"scoped-starts-stage-view-holders",
         "struct Slot { i32* target; }; i32 rest(std.list::iter<i32> cursor) { i32 sum = 0; while "
         "(true) { o<const i32*> item = std.list::next(&cursor); switch (item) { case variant "
         "o::some(value): sum += **value; break; case variant o::none: return sum; } } } async "
         "void pause() {} @scoped async i32 poke(Slot s, std.list::iter<i32> c) throws "
         "std.async::start_error { await pause(); *s.target = 3; return rest(move c); } void "
         "work(Slot s) { *s.target += 1; } async i32 probe() throws std.async::start_error, "
         "std.thread::thread_error, std.alloc::alloc_error { i32 v = 1; list<i32> l = "
         "std.list::create::<i32>(); task_scope(1) group { Slot s = Slot {.target = &v}; "
         "std.list::iter<i32> c = std.list::iter(&l); i32 r = await poke(move s, move c); r as "
         "void; } thread_scope { Slot t = Slot {.target = &v}; "
         "std.thread::scoped_join_handle<void> h = std.thread::spawn_scoped(work, move t); move h "
         "as void; } return v; }",
         NULL},
        {"ordinary-async-view-holder-parameter",
         "struct Slot { i32* target; }; async i32 poke(Slot s) { *s.target = 3; return 0; }",
         "R-DIAG-ASYNC-001 [R-BORROW-0024]"},
        /* L14.1-F1, R-TYPE-0051: an exclusive interface borrow is a function result derived from an
           exclusive input. */
        {"exclusive-dyn-result",
         "trait Tr { i32 get(const Self* this); }; struct A { i32 v; }; impl Tr for A { i32 "
         "get(const A* this) { return this->v; } }; trait Grow { void grow(Self* this); }; impl "
         "Grow for A { void grow(A* this) { this->v += 1; } }; dyn(Grow)* pick(dyn(Grow)* first, "
         "dyn(Grow)* second, bool which) { if (which == true) { return first; } return second; } "
         "i32 probe() { A a = A {.v = 1}; A b = A {.v = 2}; dyn(Grow)* chosen = pick(&a, &b, "
         "true); chosen->grow(); return a.v; }",
         NULL},
        {"exclusive-dyn-result-conflict",
         "trait Tr { i32 get(const Self* this); }; struct A { i32 v; }; impl Tr for A { i32 "
         "get(const A* this) { return this->v; } }; trait Grow { void grow(Self* this); }; impl "
         "Grow for A { void grow(A* this) { this->v += 1; } }; dyn(Grow)* pick(dyn(Grow)* first) { "
         "return first; } i32 probe() { A a = A {.v = 1}; dyn(Grow)* chosen = pick(&a); a.v = 5; "
         "chosen->grow(); return a.v; }",
         "R-DIAG-BORROW-001"},
        /* L14.1-F7, R-TYPE-0051, R-BORROW-0018: an interface borrow is an array, list or dict
           element. */
        {"dyn-borrow-containers",
         "trait Tr { i32 get(const Self* this); }; struct A { i32 v; }; impl Tr for A { i32 "
         "get(const A* this) { return this->v; } }; i32 probe(const A* a) { i32 total = 0; try { "
         "array<const dyn(Tr)*> items = std.array::create::<const dyn(Tr)*>(); "
         "std.array::push(&items, a); for (const (const dyn(Tr)*)* item in &items) { total += "
         "(*item)->get(); } list<const dyn(Tr)*> queue = std.list::create::<const dyn(Tr)*>(); "
         "std.list::push_back(&queue, a) as void; dict<i32, const dyn(Tr)*> named = "
         "std.dict::create::<i32, const dyn(Tr)*>(); std.dict::insert(&named, 1, a) as void; } "
         "catch (std.array::push_error<const dyn(Tr)*> e) { move e as void; } catch "
         "(std.list::push_error<const dyn(Tr)*> e) { move e as void; } catch "
         "(std.dict::insert_error<i32, const dyn(Tr)*> e) { move e as void; } return total; }",
         NULL},
        {"dyn-borrow-container-conflict",
         "trait Tr { i32 get(const Self* this); }; struct A { i32 v; }; impl Tr for A { i32 "
         "get(const A* this) { return this->v; } }; i32 probe() { A a = A {.v = 1}; try { "
         "array<const dyn(Tr)*> items = std.array::create::<const dyn(Tr)*>(); "
         "std.array::push(&items, &a); a.v = 5; return items[0]->get(); } catch "
         "(std.array::push_error<const dyn(Tr)*> e) { move e as void; } return 0; }",
         "R-DIAG-BORROW-001"},
        /* L14.1-F11, R-TYPE-0029: a task type whose result or completion error holds a view is
           diagnosed where it is written. */
        {"task-type-borrowed-result",
         "void f(task<str> t) { move t as void; }",
         "R-DIAG-ASYNC-001 [R-TYPE-0029]"},
        {"task-type-borrowed-error",
         "error Bad { const i32* p; }; void f(task<i32 throws Bad> t) { move t as void; }",
         "R-DIAG-ASYNC-001 [R-TYPE-0029]"},
        {"task-type-program-string",
         "void f(task<constexpr str> t) { move t as void; }",
         NULL},
        /* L14.1-F10, L14.1-D6: a signature, parameter or local already diagnosed by a rule,
           including a use of an aggregate a rule rejected, adds no slice message. */
        {"async-borrowed-parameter-single-diagnostic",
         "async i32 f(std.list::iter<i32> it) { move it as void; return 0; }",
         "R-DIAG-ASYNC-001 [R-BORROW-0024]"},
        {"poisoned-aggregate-parameter",
         "struct A { A inner; }; void f(A a) { move a as void; }",
         "R-DIAG-TYPE-001 [R-AGG-0001]"},
        {"poisoned-aggregate-local",
         "struct Node { own Node*? next; str word; }; void f(str w) { Node n = Node {.next = null, "
         ".word = w}; move n as void; }",
         NULL},
        /* L14.1-D3, L14.1-D4, R-GRAM-0005: a standard type name takes sequence suffixes; a slice
           suffix does not follow an array suffix. */
        {"standard-type-sequences",
         "import std.fs; usize count(const std.fs::seek_origin[] items) { return len(items); } "
         "std.fs::seek_origin last(std.fs::seek_origin[2] items) { return items[1]; } usize "
         "probe() { std.fs::seek_origin[2] items = {std.fs::seek_origin::start, "
         "std.fs::seek_origin::end}; std.fs::seek_origin tail = last(items); tail as void; return "
         "count(items[..]); }",
         NULL},
        {"array-then-slice-suffix",
         "usize count(const i32[3][] rows) { return len(rows); }",
         "R-DIAG-SYN-001"},
        /* L14.1-D7, R-TYPE-0019, R-INIT-0011: an exclusive borrow, exclusive slice or exclusive
           interface borrow is Move-only; an explicit drop ends it early. */
        {"drop-exclusive-views",
         "trait Tr { i32 get(const Self* this); }; struct A { i32 v; }; impl Tr for A { i32 "
         "get(const A* this) { return this->v; } }; trait Grow { void grow(Self* this); }; impl "
         "Grow for A { void grow(A* this) { this->v += 1; } }; i32 probe() { i32 value = 1; i32* p "
         "= &value; *p = 2; drop p; value += 1; i32[3] cells = {1, 2, 3}; i32[] view = "
         "cells[0usize..2usize]; view[0] = 9; drop view; cells[1] = 7; A a = A {.v = 1}; "
         "dyn(Grow)* g = &a; g->grow(); drop g; a.v += 1; return value + cells[0] + a.v; }",
         NULL},
        {"use-after-drop-exclusive-borrow",
         "i32 probe() { i32 value = 1; i32* p = &value; drop p; *p = 3; return value; }",
         "R-DIAG-MOVE-002 [R-OWN-0004]"},
        /* L14.1-S4, R-BORROW-0023, R-MEM-0015: a scoped thread handle carries its thread region; it
           is not returned, owned or observed after its thread_scope. */
        {"scoped-handle-returned",
         "void work(const i32* p) { p as void; } std.thread::scoped_join_handle<void> start(const "
         "i32* p) throws std.thread::thread_error { thread_scope { "
         "std.thread::scoped_join_handle<void> h = std.thread::spawn_scoped(work, p); return move "
         "h; } }",
         "R-DIAG-BORROW-002 [R-BORROW-0007]"},
        {"scoped-handle-observed-after-scope",
         "void work(const i32* p) { p as void; } void probe(bool flag) throws "
         "std.thread::thread_error { i32 x = 1; o<std.thread::scoped_join_handle<void>> keep = "
         "o::none; thread_scope { std.thread::scoped_join_handle<void> h = "
         "std.thread::spawn_scoped(work, &x); if (flag == true) { keep = o::some(move h); } else { "
         "move h as void; } } move keep as void; }",
         "R-DIAG-BORROW-002 [R-BORROW-0007]"},
        {"scoped-handle-owned",
         "void work(const i32* p) { p as void; } void probe() throws std.thread::thread_error { "
         "i32 x = 1; thread_scope { std.thread::scoped_join_handle<void> h = "
         "std.thread::spawn_scoped(work, &x); arc std.thread::scoped_join_handle<void> a = new arc "
         "std.thread::scoped_join_handle<void>(move h); move a as void; } }",
         NULL},
        {"scoped-handle-ordinary-async-parameter",
         "async void join_it(std.thread::scoped_join_handle<i32> h) { std.thread::join_result<i32> "
         "r = std.thread::join(move h); drop r; }",
         "R-DIAG-ASYNC-001 [R-BORROW-0024]"},
        {"scoped-handle-uses-inside-scope",
         "struct Keep { std.thread::scoped_join_handle<void> h; }; void take(Keep k) { move k as "
         "void; } void work(const i32* p) { p as void; } @scoped async void "
         "join_it(std.thread::scoped_join_handle<i32> h) { std.thread::join_result<i32> r = "
         "std.thread::join(move h); drop r; } void probe() throws std.thread::thread_error { i32 x "
         "= 1; thread_scope { std.thread::scoped_join_handle<void> h = "
         "std.thread::spawn_scoped(work, &x); take(Keep {.h = move h}); } }",
         NULL},
        /* L14.1-S5, R-OWN-0004: a field, element or pointee is reached only through an initialized
           object; a write through a moved owner, array, struct, borrow or slice is diagnosed (it
           dereferenced a moved-from owner at run time). */
        {"write-through-moved-owner",
         "struct P { i32 x; }; i32 probe() { own P* a = new P {.x = 1}; own P* b = move a; a->x = "
         "5; return b->x; }",
         "R-DIAG-MOVE-002 [R-OWN-0004]"},
        {"write-element-of-moved-array",
         "i32 probe() throws std.array::push_error<i32> { array<i32> a = "
         "std.array::create::<i32>(); std.array::push(&a, 1); array<i32> b = move a; a[0] = 5; "
         "return b[0]; }",
         "R-DIAG-MOVE-002 [R-OWN-0004]"},
        {"write-field-of-moved-struct",
         "struct P { i32 x; }; struct Q { P p; own i32* box; }; i32 probe() { Q q = Q {.p = P {.x "
         "= 1}, .box = new i32(2)}; Q r = move q; q.p.x = 5; return r.p.x; }",
         "R-DIAG-MOVE-002 [R-OWN-0004]"},
        {"write-through-moved-borrow-field",
         "struct S { i32* p; }; i32 probe() { i32 v = 1; S s = S {.p = &v}; S t = move s; *s.p = "
         "3; return v; }",
         "R-DIAG-MOVE-002 [R-OWN-0004]"},
        {"element-of-dropped-slice",
         "i32 probe() { i32[4] a = {1, 2, 3, 4}; i32[] s = a[..]; drop s; s[0] = 5; return a[0]; }",
         "R-DIAG-MOVE-002 [R-OWN-0004]"},
        {"range-of-dropped-slice",
         "i32 probe() { i32[4] a = {1, 2, 3, 4}; i32[] s = a[..]; drop s; i32[] t = "
         "s[0usize..1usize]; t[0] = 5; return a[0]; }",
         "R-DIAG-MOVE-002 [R-OWN-0004]"},
        {"move-of-expired-view-holder",
         "void probe(bool flag) { i32 base = 0; o<i32*> keep = o::some(&base); { i32 y = 2; if "
         "(flag == true) { keep = o::some(&y); } } move keep as void; }",
         "R-DIAG-BORROW-002 [R-BORROW-0007]"},
        /* L14.4, R-TYPE-0044: a callable result follows the rules of a function result, including
           an exclusive borrow bounded by the input origins and a view of a view holder. */
        {"callable-exclusive-view-holder-result",
         "@generic<F: fn(((str)[2])*) -> (str)*> (str)* apply(const F* f, ((str)[2])* items) { "
         "return f(items); } void probe(str a, str b) { (str)[2] items = {a, b}; fn (str)* "
         "first(((str)[2])* entries) { return &(*entries)[0]; } (str)* got = apply(&first, "
         "&items); *got = b; }",
         NULL},
        {"callable-shared-result",
         "@generic<F: fn(const i32*) -> const i32*> const i32* apply(const F* f, const i32* item) "
         "{ return f(item); } i32 probe() { i32 a = 1; fn const i32* same(const i32* x) { return "
         "x; } const i32* p = apply(&same, &a); return *p; }",
         NULL},
        {"local-function-exclusive-result",
         "i32 probe() { i32 v = 1; fn i32* pick(i32* x) { return x; } i32* p = pick(&v); *p = 2; "
         "return v; }",
         NULL},
        {"local-function-view-holder-result",
         "void probe(str a) { fn (str)* pick(((str)[1])* x) { return &(*x)[0]; } (str)[1] items = "
         "{a}; (str)* p = pick(&items); p as void; }",
         NULL},
        /* L14-F15, R-EXPR-0020: a range argument of a generic call takes its slice type from the
           parameter pattern, dependent or not, inferred or explicit. */
        {"generic-range-argument-dependent",
         "@generic<T: copy> usize count(const T[] items) { return len(items); } usize probe() { "
         "i32[3] a = {1, 2, 3}; return count(a[..]); }",
         NULL},
        {"generic-range-argument-nondependent",
         "@generic<T: copy> usize count(T tag, const i32[] items) { return len(items); } usize "
         "probe() { i32[3] a = {1, 2, 3}; return count(7, a[..]); }",
         NULL},
        {"generic-range-argument-explicit",
         "@generic<T: copy> usize count(const T[] items) { return len(items); } usize probe() { "
         "i32[3] a = {1, 2, 3}; return count::<i32>(a[..]); }",
         NULL},
        {"generic-range-argument-array-base",
         "@generic<T: copy> usize count(const T[] items) { return len(items); } usize probe() "
         "throws std.array::push_error<i32> { array<i32> a = std.array::create::<i32>(); "
         "std.array::push(&a, 1); return count(a[..]); }",
         NULL},
        {"generic-range-argument-view-elements",
         "@generic<T> usize count(const T[] items) { return len(items); } usize probe(str x) { "
         "(str)[2] a = {x, x}; return count(a[0usize..1usize]); }",
         NULL},
        {"generic-range-argument-mutable",
         "@generic<T: copy & unborrowed> void fill(T[] items, T v) { for (usize i = 0usize; i < "
         "len(items); i += 1usize) { items[i] = v; } } i32 probe() { i32[3] a = {1, 2, 3}; "
         "fill(a[1usize..3usize], 9); return a[2]; }",
         NULL},
        {"generic-range-argument-mutable-explicit",
         "@generic<T: copy & unborrowed> void fill(T[] items, T v) { for (usize i = 0usize; i < "
         "len(items); i += 1usize) { items[i] = v; } } i32 probe() { i32[3] a = {1, 2, 3}; "
         "fill::<i32>(a[1usize..3usize], 9); return a[2]; }",
         NULL},
        /* L14-D8 and L14-S6, R-BORROW-0019, R-TYPE-0035: an element written through a slice
           parameter is a store through that parameter, and so is a value of a dependent type. Since
           L15.2 a store of another parameter's view is recorded in the function's store contract,
           which its callers apply; a store of a local's view is still rejected. */
        {"slice-parameter-store-foreign",
         "void f(str[] items, str other) { items[0] = other; }",
         NULL},
        {"slice-parameter-store-local",
         "import std.string; void f(str[] items) throws std.alloc::alloc_error { "
         "std.string::string s = std.string::from_str(\"x\"); items[0] = std.string::as_str(&s); }",
         "R-DIAG-BORROW-002 [R-BORROW-0019]"},
        {"slice-parameter-store-loop-local",
         "void f((const i32*)[] items) { for (usize i = 0usize; i < len(items); i += 1usize) { i32 "
         "local = 1; items[i] = &local; } }",
         "R-DIAG-BORROW-002 [R-BORROW-0019]"},
        {"slice-parameter-store-own-element",
         "void f(str[] items) { items[0] = items[1]; }",
         NULL},
        {"concrete-push-foreign-borrow",
         "void add(array<const i32*>* a, const i32* v) throws std.array::push_error<const i32*> { "
         "std.array::push(a, v); }",
         NULL},
        {"generic-store-through-pointer",
         "@generic<T: copy> void put(T* item, T v) { *item = v; }",
         NULL},
        {"generic-store-through-slice",
         "@generic<T: copy> void put(T[] items, T v) { items[0] = v; }",
         NULL},
        {"generic-store-through-slice-loop",
         "@generic<T: copy> void fill(T[] items, T v) { for (usize i = 0usize; i < len(items); i "
         "+= 1usize) { items[i] = v; } }",
         NULL},
        {"generic-store-through-field",
         "@generic<T: copy> struct Cell { T value; }; @generic<T: copy> void put(Cell<T>* c, T v) "
         "{ c->value = v; }",
         NULL},
        {"generic-array-push-through-parameter",
         "@generic<T: copy> void add(array<T>* a, T v) throws std.array::push_error<T> { "
         "std.array::push(a, v); }",
         NULL},
        {"generic-list-push-through-parameter",
         "@generic<T: copy> void add(list<T>* a, T v) throws std.list::push_error<T> { "
         "std.list::push_back(a, v) as void; }",
         NULL},
        {"generic-dict-insert-through-parameter",
         "@generic<K: key, V: copy> void put(dict<K, V>* d, K k, V v) throws "
         "std.dict::insert_error<K, V> { std.dict::insert(d, move k, v); }",
         NULL},
        {"generic-store-unborrowed-pointer",
         "@generic<T: copy & unborrowed> void set(T* p, T v) { *p = v; } i32 probe() { i32 x = 1; "
         "set(&x, 2); return x; }",
         NULL},
        {"generic-store-unborrowed-slice",
         "@generic<T: copy & unborrowed> void put(T[] items, T v) { items[0] = v; }",
         NULL},
        {"generic-store-unborrowed-slice-loop",
         "@generic<T: copy & unborrowed> void fill(T[] items, T v) { for (usize i = 0usize; i < "
         "2usize; i += 1usize) { items[i] = v; } }",
         NULL},
        {"generic-store-unborrowed-field",
         "@generic<T: copy> struct Cell { T value; }; @generic<T: copy & unborrowed> void "
         "put(Cell<T>* c, T v) { c->value = v; }",
         NULL},
        {"generic-array-push-unborrowed",
         "@generic<T: copy & unborrowed> void add(array<T>* a, T v) throws "
         "std.array::push_error<T> { std.array::push(a, v); } usize probe() throws "
         "std.array::push_error<i32> { array<i32> a = std.array::create::<i32>(); add(&a, 4); "
         "return len(a); }",
         NULL},
        {"generic-list-push-unborrowed",
         "@generic<T: copy & unborrowed> void add(list<T>* a, T v) throws std.list::push_error<T> "
         "{ std.list::push_back(a, v) as void; }",
         NULL},
        {"generic-dict-insert-unborrowed",
         "@generic<K: key & unborrowed, V: copy & unborrowed> void put(dict<K, V>* d, K k, V v) "
         "throws std.dict::insert_error<K, V> { std.dict::insert(d, move k, v); }",
         NULL},
        {"generic-push-local-container",
         "@generic<T: copy> usize count(T v) throws std.array::push_error<T> { array<T> a = "
         "std.array::create::<T>(); std.array::push(&a, v); return len(a); }",
         NULL},
        {"generic-unborrowed-rejects-borrow-argument",
         "@generic<T: copy & unborrowed> void set(T* p, T v) { *p = v; } i32 probe() { i32 g = 1; "
         "const i32* slot = &g; i32 local = 2; set::<const i32*>(&slot, &local); return *slot; }",
         "R-DIAG-TYPE-001 [R-TYPE-0001]"},
        /* L14-F16, R-GRAM-0005: a statement that begins with a closed core type name such as
           `core::memory_order` declares an object, as one beginning with `std.m::name` does. */
        {"core-memory-order-local",
         "void f(core::memory_order v) { core::memory_order w = v; w as void; }",
         NULL},
        {"core-utf8-error-local",
         "void f(core::utf8_error v) { core::utf8_error w = move v; move w as void; }",
         NULL},
        {"core-type-fixed-array-local",
         "void f(core::memory_order v) { core::memory_order[2] w = {v, v}; w as void; }",
         NULL},
        {"core-type-for-initializer",
         "void f(core::memory_order v) { for (core::memory_order m = v; m != "
         "core::memory_order::seq_cst; m = core::memory_order::seq_cst) { } }",
         NULL},
        {"core-unknown-type-local",
         "void f() { core::nothing w = 1; }",
         "R-DIAG-NAME-001 [R-NAME-0003]"},
        /* L14-F17, R-STMT-0006, R-REFL-0001: a fieldless enum that the standard library implements
           natively names its variants, is switched over exhaustively or with a default, and is
           reflected like a source enum. */
        {"std-enum-constants",
         "i32 f() { std.alloc::alloc_error a = std.alloc::alloc_error::size_overflow; "
         "std.async::start_error s = std.async::start_error::scope_full; std.thread::thread_error "
         "t = std.thread::thread_error::unavailable; a as void; s as void; t as void; return 0; }",
         NULL},
        {"std-enum-switch-exhaustive",
         "import std.fs; i32 f(std.fs::seek_origin s) { switch (s) { case "
         "std.fs::seek_origin::start: return 1; case std.fs::seek_origin::current: return 2; case "
         "std.fs::seek_origin::end: return 3; } }",
         NULL},
        {"std-enum-switch-default",
         "i32 f(std.alloc::alloc_error e) { switch (e) { case "
         "std.alloc::alloc_error::out_of_memory: return 1; default: return 2; } }",
         NULL},
        {"std-enum-reflection",
         "import std.process; usize f(std.process::termination_kind k) { constexpr str name = "
         "core::enum_name(k); return len(name) + core::enum_ordinal(k) + "
         "core::enum_count::<std.alloc::alloc_error>(); }",
         NULL},
        {"std-enum-switch-not-exhaustive",
         "import std.fs; i32 f(std.fs::seek_origin s) { switch (s) { case "
         "std.fs::seek_origin::start: return 1; case std.fs::seek_origin::end: return 3; } return "
         "0; }",
         "R-DIAG-SWITCH-001 [R-STMT-0006]"},
        {"std-enum-switch-repeated-variant",
         "import std.fs; i32 f(std.fs::seek_origin s) { switch (s) { case "
         "std.fs::seek_origin::start: return 1; case std.fs::seek_origin::start: return 2; "
         "default: return 3; } }",
         "R-DIAG-SWITCH-001 [R-STMT-0006]"},
        {"std-enum-switch-foreign-variant",
         "import std.fs; i32 f(std.fs::seek_origin s) { switch (s) { case "
         "std.fs::file_kind::regular: return 1; default: return 3; } }",
         "R-DIAG-SWITCH-001 [R-STMT-0006]"},
        {"std-enum-unknown-variant",
         "std.alloc::alloc_error f() { return std.alloc::alloc_error::other; }",
         "R-DIAG-NAME-001 [R-NAME-0003]"},
        {"std-struct-enum-reflection",
         "import std.process; usize f() { return core::enum_count::<std.process::exit_status>(); }",
         "R-DIAG-TYPE-001 [R-REFL-0001]"},
        /* L14-S7, R-TYPE-0045: an associated type does not depend on the region of a call. A method
           implementing a prototype whose result mentions one, `next` of core::Iterator included,
           returns no view of the storage its borrowed receiver designates, directly or through an
           exclusive view, owner or container that storage holds. */
        {"lending-core-iterator",
         "struct window { i32[4] buffer; usize at; }; impl core::Iterator for window { type Item = "
         "const i32*; o<const i32*> next(window* this) { if (this->at >= 4usize) { return o::none; "
         "} const i32* item = &this->buffer[this->at]; this->at += 1usize; return o::some(item); } "
         "};",
         "R-DIAG-BORROW-002 [R-TYPE-0045]"},
        {"lending-user-trait",
         "trait Source { type Item; o<Self::Item> pull(Self* this); }; struct window { i32[4] "
         "buffer; }; impl Source for window { type Item = const i32*; o<const i32*> pull(window* "
         "this) { const i32* item = &this->buffer[0usize]; return o::some(item); } };",
         "R-DIAG-BORROW-002 [R-TYPE-0045]"},
        {"lending-exclusive-held-slice",
         "struct cursor { i32[] items; }; impl core::Iterator for cursor { type Item = i32*; "
         "o<i32*> next(cursor* this) { if (len(this->items) == 0usize) { return o::none; } i32* "
         "item = &this->items[0usize]; return o::some(item); } };",
         "R-DIAG-BORROW-002 [R-TYPE-0045]"},
        {"lending-owned-box",
         "struct boxed { own i32* value; bool done; }; impl core::Iterator for boxed { type Item = "
         "const i32*; o<const i32*> next(boxed* this) { if (this->done == true) { return o::none; "
         "} this->done = true; const i32* item = &*this->value; return o::some(item); } };",
         "R-DIAG-BORROW-002 [R-TYPE-0045]"},
        {"lending-owned-array",
         "struct stock { array<i32> values; usize at; }; impl core::Iterator for stock { type Item "
         "= const i32*; o<const i32*> next(stock* this) { if (this->at >= len(this->values)) { "
         "return o::none; } const i32* item = &this->values[this->at]; this->at += 1usize; return "
         "o::some(item); } };",
         "R-DIAG-BORROW-002 [R-TYPE-0045]"},
        /* L14-S7, R-TYPE-0045: a call of such a method holds what its borrowed receiver holds, so
           the receiver is borrowed again, also by range-for, while earlier items live; what the
           result takes from other arguments, or from a receiver taken by value, still depends on
           them. */
        {"shared-held-slice-items",
         "struct cursor { const i32[] items; usize at; }; impl core::Iterator for cursor { type "
         "Item = const i32*; o<const i32*> next(cursor* this) { if (this->at >= len(this->items)) "
         "{ return o::none; } const i32* item = &this->items[this->at]; this->at += 1usize; return "
         "o::some(item); } }; i32 f(const i32[] data) { cursor c = cursor {.items = data, .at = "
         "0usize}; o<const i32*> a = c.next(); o<const i32*> b = c.next(); i32 total = 0; switch "
         "(move a) { case variant o::some(move x): total += *x; break; case variant o::none: "
         "break; } switch (move b) { case variant o::some(move y): total += *y; break; case "
         "variant o::none: break; } return total; }",
         NULL},
        {"range-for-keeps-items",
         "struct cursor { const i32[] items; usize at; }; impl core::Iterator for cursor { type "
         "Item = const i32*; o<const i32*> next(cursor* this) { if (this->at >= len(this->items)) "
         "{ return o::none; } usize current = this->at; this->at += 1usize; const i32* item = "
         "&this->items[current]; return o::some(item); } }; usize f(const i32[] data) throws "
         "std.array::push_error<const i32*> { array<const i32*> kept = std.array::create::<const "
         "i32*>(); cursor c = cursor {.items = data, .at = 0usize}; for (const i32* x in &c) { "
         "kept.push(x); } return len(kept); }",
         NULL},
        {"generic-last-keeps-items",
         "@generic<I: core::Iterator> o<I::Item> last_of(I inner) { o<I::Item> result = o::none; "
         "while (true) { o<I::Item> item = inner.next(); switch (move item) { case variant "
         "o::some(move value): result = o::some(move value); break; case variant o::none: return "
         "move result; } } return move result; }",
         NULL},
        {"generic-view-receiver-keeps-items",
         "@generic<I: core::Iterator> o<I::Item> last_via(I* source) { o<I::Item> result = "
         "o::none; while (true) { o<I::Item> item = source->next(); switch (move item) { case "
         "variant o::some(move value): result = o::some(move value); break; case variant o::none: "
         "return move result; } } return move result; }",
         NULL},
        {"local-items-escape",
         "struct cursor { const i32[] items; usize at; }; impl core::Iterator for cursor { type "
         "Item = const i32*; o<const i32*> next(cursor* this) { if (this->at >= len(this->items)) "
         "{ return o::none; } usize current = this->at; this->at += 1usize; return "
         "o::some(&this->items[current]); } }; o<const i32*> f() { i32[2] local = {1, 2}; cursor c "
         "= cursor {.items = local[..], .at = 0usize}; return c.next(); }",
         "R-DIAG-BORROW-002 [R-BORROW-0007]"},
        {"parameter-items-return",
         "struct cursor { const i32[] items; usize at; }; impl core::Iterator for cursor { type "
         "Item = const i32*; o<const i32*> next(cursor* this) { if (this->at >= len(this->items)) "
         "{ return o::none; } usize current = this->at; this->at += 1usize; return "
         "o::some(&this->items[current]); } }; o<const i32*> f(const i32[] data) { cursor c = "
         "cursor {.items = data, .at = 0usize}; o<const i32*> first = c.next(); first as void; "
         "return c.next(); }",
         NULL},
        {"associated-other-argument-expires",
         "trait Picker { type Item; o<Self::Item> pick(Self* this, const i32[] data); }; struct "
         "first_picker { usize calls; }; impl Picker for first_picker { type Item = const i32*; "
         "o<const i32*> pick(first_picker* this, const i32[] data) { this->calls += 1usize; const "
         "i32* item = &data[0usize]; return o::some(item); } }; i32 f() { first_picker p = "
         "first_picker {.calls = 0usize}; o<const i32*> got = o::none; if (p.calls == 0usize) { "
         "i32[2] local = {7, 8}; got = p.pick(&local); } switch (move got) { case variant "
         "o::some(move x): return *x; case variant o::none: return 1; } return 2; }",
         "R-DIAG-BORROW-002 [R-BORROW-0007]"},
        {"by-value-receiver-keeps-held",
         "struct G { const i32* p; }; trait First { type Item; o<Self::Item> first(Self this); }; "
         "struct gcursor { const G* target; }; impl First for gcursor { type Item = const G*; "
         "o<const G*> first(gcursor this) { return o::some(this.target); } }; i32 f() { i32 x = 5; "
         "o<const G*> r = o::none; if (x == 5) { G g = G {.p = &x}; gcursor c = gcursor {.target = "
         "&g}; r = c.first(); } switch (move r) { case variant o::some(move gp): return *gp->p; "
         "case variant o::none: return 1; } return 2; }",
         "R-DIAG-BORROW-002 [R-BORROW-0007]"},
        {"held-holder-expires",
         "struct G { const i32* p; }; struct gcursor { const G* target; bool done; }; impl "
         "core::Iterator for gcursor { type Item = const G*; o<const G*> next(gcursor* this) { if "
         "(this->done == true) { return o::none; } this->done = true; return "
         "o::some(this->target); } }; i32 f() { i32 x = 5; o<const G*> r = o::none; if (x == 5) { "
         "G g = G {.p = &x}; gcursor c = gcursor {.target = &g, .done = false}; r = c.next(); } "
         "switch (move r) { case variant o::some(move gp): return *gp->p; case variant o::none: "
         "return 1; } return 2; }",
         "R-DIAG-BORROW-002 [R-BORROW-0007]"},
        /* L14-S7, R-BORROW-0002, R-TYPE-0035: the storage a shared view parameter designates is
           reached only through shared views, so a value derived from it, a dependent one included,
           is no exclusive borrow of it; an exclusive borrow of the parameter variable itself still
           excludes reading it. */
        {"generic-fold-shared-callable",
         "@generic<I: core::Iterator, A, F: fn(A, I::Item) -> A> A fold_of(I inner, A initial, "
         "const F* function) { A accumulator = move initial; while (true) { o<I::Item> item = "
         "inner.next(); switch (move item) { case variant o::some(move value): A next = "
         "function(move accumulator, move value); accumulator = move next; break; case variant "
         "o::none: return move accumulator; } } return move accumulator; }",
         NULL},
        {"generic-apply-twice-shared-callable",
         "@generic<A, F: fn(A) -> A> A twice(A initial, const F* function) { A first = "
         "function(move initial); A second = function(move first); return move second; }",
         NULL},
        {"parameter-slot-exclusive-read",
         "i32 f(const i32* p) { (const i32*)* pp = &p; i32 v = *p; const i32* q = *pp; return v + "
         "*q; }",
         "R-DIAG-BORROW-001 [R-BORROW-0002]"},
        /* L14-S7, R-BORROW-0021: an element or range of a shared slice held in storage lies in the
           region of that slice, not in the storage it is read from; one of an exclusive slice held
           there stays bounded by that storage. */
        {"held-shared-slice-reslice",
         "struct cursor { const i32[] items; }; i32 advance(cursor* c) { const i32* p = "
         "&c->items[0usize]; c->items = c->items[1usize..]; return *p; }",
         NULL},
        {"held-shared-slice-escape",
         "struct cursor { const i32[] items; }; const i32* first(cursor* c) { return "
         "&c->items[0usize]; } const i32* escape() { i32[2] local = {1, 2}; cursor c = cursor "
         "{.items = local[..]}; return first(&c); }",
         "R-DIAG-BORROW-002 [R-BORROW-0007]"},
        {"held-exclusive-slice-write",
         "struct mcursor { i32[] items; }; i32 poke(mcursor* c) { const i32* p = "
         "&c->items[0usize]; c->items[0usize] = 5; return *p; }",
         "R-DIAG-BORROW-001 [R-BORROW-0002]"},
        /* L14-S8, R-BORROW-0019, R-BORROW-0021: a borrow of the storage a parameter designates is
           not stored into that storage, where the caller's object would name itself; a local may,
           and then stays in its frame. */
        {"self-view-through-parameter",
         "struct window { i32[4] buf; const i32[] view; }; void refresh(window* this) { this->view "
         "= this->buf[..]; }",
         "R-DIAG-BORROW-002 [R-BORROW-0019]"},
        {"self-view-local",
         "struct window { i32[4] buf; const i32[] view; }; i32 f() { i32[1] other = {0}; window w "
         "= window {.buf = {1, 2, 3, 4}, .view = other[..]}; w.view = w.buf[..]; return "
         "w.view[0usize]; }",
         NULL},
        {"self-view-local-return",
         "struct window { i32[4] buf; const i32[] view; }; window make(const i32[] initial) { "
         "window w = window {.buf = {1, 2, 3, 4}, .view = initial}; w.view = w.buf[..]; return "
         "move w; }",
         "R-DIAG-BORROW-002 [R-BORROW-0007]"},
        /* L14-S9, R-STMT-0010: a payload borrowed from a scrutinee that holds views reads as what
           the payload holds, not as the automatic storage of the scrutinee. */
        {"payload-holder-return",
         "struct Holder { const i32* item; }; enum Choice { Empty, Value(Holder), }; Holder "
         "read(Choice choice, Holder fallback) { switch (choice) { case variant "
         "Choice::Value(value): return *value; case variant Choice::Empty: return fallback; } }",
         NULL},
        {"generic-payload-return",
         "@generic<T> enum Choice { Empty, Value(T), }; @generic<T: copy> T read(Choice<T> choice, "
         "T fallback) { switch (choice) { case variant Choice<T>::Value(value): return *value; "
         "case variant Choice<T>::Empty: return fallback; } }",
         NULL},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char source[2048];
        const int length =
            snprintf(source, sizeof(source), "module test.l14_regressions; %s", cases[i].source);
        RFrontendContext *context = r_frontend_create(NULL);
        R_SEMANTIC_CHECK(context != NULL && length > 0 && (size_t)length < sizeof(source));
        if (context == NULL)
            return;
        (void)r_semantic_add_bytes(context, cases[i].name, (const uint8_t *)source, (size_t)length);
        /* MIR lowering proves the async frame forms and reports await liveness. */
        RFrontendStatus status = r_frontend_analyze(context);
        if (status == R_FRONTEND_OK) {
            status = r_frontend_lower_mir(context);
            if ((status == R_FRONTEND_NOT_LOWERABLE) && (context->diagnostic_count != 0U)) {
                /* An await liveness diagnostic (R-BORROW-0024) stops the lowering. */
                status = R_FRONTEND_INVALID_SOURCE;
            }
        }
        const bool valid =
            status == (cases[i].diagnostic == NULL ? R_FRONTEND_OK : R_FRONTEND_INVALID_SOURCE) &&
            (cases[i].diagnostic == NULL ||
             r_semantic_l14_diagnostic_count(context, cases[i].diagnostic) != 0U) &&
            r_semantic_diagnostic_code_count(context, "R-DIAG-SLICE-001") == 0U;
        if (!valid) {
            (void)fprintf(stderr, "l14 regression %s status %d\n", cases[i].name, (int)status);
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

/* L13 (docs/language-completeness-roadmap.ru.md): the remaining forms of spec section 25.1. Each
 * case is accepted, or keeps its normative diagnostic. */
static void r_semantic_test_l13_regressions(void) {
    static const struct {
        const char *name;
        const char *source;
        const char *diagnostic;
    } cases[] = {
        /* L13.1, R-EXPR-0021: values[i] designates the live element in the buffer an array<T> owns,
           with the borrow relationship of std.array::get and get_mut. */
        {"array-index-read-write",
         "i32 probe() throws std.array::push_error<i32> { array<i32> values = "
         "std.array::create::<i32>(); std.array::push(&values, 1); std.array::push(&values, 2); "
         "values[0] = values[1] + 10; i32* second = &values[1]; *second += 5; return values[0] + "
         "values[1]; }",
         NULL},
        {"array-index-through-borrows",
         "i32 sum(const array<i32>* values) { i32 total = 0; for (usize i = 0usize; i < "
         "len(*values); i += 1usize) { total += (*values)[i]; } return total; } i32* "
         "first(array<i32>* values) { return &(*values)[0]; }",
         NULL},
        {"array-index-nested-and-views",
         "struct Named { str name; i32 id; }; usize probe(str text) throws "
         "std.array::push_error<Named>, std.array::push_error<array<i32>>, "
         "std.array::push_error<i32> { array<array<i32>> grid = std.array::create::<array<i32>>(); "
         "array<i32> row = std.array::create::<i32>(); std.array::push(&row, 7); "
         "std.array::push(&grid, move row); grid[0][0] += 1; array<Named> names = "
         "std.array::create::<Named>(); std.array::push(&names, Named {.name = text, .id = 1}); "
         "names[0].name = text; return len(names[0].name) + (grid[0][0] as usize); }",
         NULL},
        {"array-index-structural-mutation-conflict",
         "i32 probe() throws std.array::push_error<i32> { array<i32> values = "
         "std.array::create::<i32>(); std.array::push(&values, 1); std.array::push(&values, 2); "
         "i32* p = &values[0]; std.array::push(&values, 3); *p = 4; return values[0]; }",
         "R-DIAG-BORROW-001"},
        {"array-index-clear-conflict",
         "i32 probe() throws std.array::push_error<i32> { array<i32> values = "
         "std.array::create::<i32>(); std.array::push(&values, 1); std.array::push(&values, 2); "
         "const i32* p = &values[0]; std.array::clear(&values); return *p; }",
         "R-DIAG-BORROW-001"},
        {"array-index-shared-path-write",
         "i32 probe(const array<i32>* values) { (*values)[0] = 1; return 0; }",
         "R-DIAG-BORROW-001"},
        {"array-index-write-during-iteration",
         "i32 probe() throws std.array::push_error<i32> { array<i32> values = "
         "std.array::create::<i32>(); std.array::push(&values, 1); std.array::push(&values, 2); "
         "for (const i32* item in &values) { values[0] = *item; } return values[0]; }",
         "R-DIAG-BORROW-001"},
        {"array-index-move-out",
         "struct Named { str name; i32 id; }; Named probe() throws std.array::push_error<Named> { "
         "array<Named> names = std.array::create::<Named>(); std.array::push(&names, Named {.name "
         "= \"a\", .id = 1}); return move names[0]; }",
         "R-DIAG-MOVE-003"},
        {"array-index-local-element-result",
         "i32* probe() throws std.array::push_error<i32> { array<i32> values = "
         "std.array::create::<i32>(); std.array::push(&values, 1); return &values[0]; }",
         "R-DIAG-BORROW-002"},
        {"array-index-dangling-view-store",
         "usize probe(str text) throws std.alloc::alloc_error, std.array::push_error<str> { "
         "array<str> words = std.array::create::<str>(); std.array::push(&words, text); { "
         "std.string::string temp = std.string::from_str(\"t\"); words[0] = "
         "std.string::as_str(&temp); } return len(words[0]); }",
         "R-DIAG-BORROW-002"},
        /* L13.3, R-BORROW-0005: an async frame holds the borrow a call returns in a hidden local
           that the dereference reads; the local does not outlive its expression, and R-BORROW-0024
           still rejects it across an await. */
        {"async-call-deref-forms",
         "struct Owner { i32[4] slots; i32 total; }; i32* Owner::slot(Owner* this, usize index) { "
         "return &this->slots[index]; } const i32* Owner::peek(const Owner* this, usize index) { "
         "return &this->slots[index]; } i32* Owner::total_slot(Owner* this) { return &this->total; "
         "} protected async i32 work(i32 seed) { Owner owner = Owner {.slots = {seed, 2, 3, 4}, "
         ".total = 0}; *owner.slot(1usize) = 7; *owner.slot(2usize) += 5; i32 read = "
         "*owner.slot(1usize); *owner.total_slot() = read + *owner.peek(2usize); const i32* kept = "
         "&*owner.peek(0usize); return owner.total + *kept; }",
         NULL},
        {"async-call-deref-in-loop",
         "struct Owner { i32[4] slots; i32 total; }; i32* Owner::slot(Owner* this, usize index) { "
         "return &this->slots[index]; } const i32* Owner::peek(const Owner* this, usize index) { "
         "return &this->slots[index]; } i32* Owner::total_slot(Owner* this) { return &this->total; "
         "} protected async i32 work() { Owner owner = Owner {.slots = {1, 2, 3, 4}, .total = 0}; "
         "for (usize i = 0usize; i < 4usize; i += 1usize) { *owner.slot(i) *= 2; "
         "*owner.total_slot() += *owner.peek(i); } return owner.total; }",
         NULL},
        {"async-call-deref-across-await",
         "struct Owner { i32[4] slots; i32 total; }; i32* Owner::slot(Owner* this, usize index) { "
         "return &this->slots[index]; } const i32* Owner::peek(const Owner* this, usize index) { "
         "return &this->slots[index]; } i32* Owner::total_slot(Owner* this) { return &this->total; "
         "} protected async i32 compute() { return 11; } protected async i32 work() throws "
         "std.async::start_error { Owner owner = Owner {.slots = {1, 2, 3, 4}, .total = 0}; "
         "*owner.slot(1usize) = await compute(); return owner.slots[1]; }",
         "R-DIAG-ASYNC-001"},
        {"async-call-deref-conflict",
         "struct Owner { i32[4] slots; i32 total; }; i32* Owner::slot(Owner* this, usize index) { "
         "return &this->slots[index]; } const i32* Owner::peek(const Owner* this, usize index) { "
         "return &this->slots[index]; } i32* Owner::total_slot(Owner* this) { return &this->total; "
         "} protected async i32 work() { Owner owner = Owner {.slots = {1, 2, 3, 4}, .total = 0}; "
         "i32* first = owner.slot(0usize); *owner.slot(1usize) = 3; *first = 5; return "
         "owner.slots[0]; }",
         "R-DIAG-BORROW-001"},
        /* L13.4, R-MEM-0015: every borrow a scoped child takes is a loan of the thread region: an
           exclusive borrow or slice suspends every parent access and a shared one every parent write,
           until region completion or the join of the exact handle. */
        {"thread-scoped-exclusive-borrow-and-slice",
         "void bump(i32* value, i32 amount) { *value += amount; } void look(const i32* value) { "
         "*value as void; } void fill(i32[] values, i32 seed) { for (usize i = 0usize; i < "
         "len(values); i += 1usize) { values[i] = seed + (i as i32); } } i32 run() throws "
         "std.thread::thread_error { i32 first = 1; i32 second = 10; i32[3] low = {0, 0, 0}; i32* "
         "named = &second; thread_scope { std.thread::scoped_join_handle<void> a = "
         "std.thread::spawn_scoped(bump, &first, 5); std.thread::scoped_join_handle<void> b = "
         "std.thread::spawn_scoped(bump, named, 7); std.thread::scoped_join_handle<void> c = "
         "std.thread::spawn_scoped(fill, low[0usize..3usize], 100); std.thread::join_result<void> "
         "joined = std.thread::join(move a); drop joined; first += 1; move b as void; move c as "
         "void; } return first + second + low[0]; }",
         NULL},
        {"thread-scoped-join-through-handle-move",
         "void bump(i32* value, i32 amount) { *value += amount; } void look(const i32* value) { "
         "*value as void; } void fill(i32[] values, i32 seed) { for (usize i = 0usize; i < "
         "len(values); i += 1usize) { values[i] = seed + (i as i32); } } i32 run() throws "
         "std.thread::thread_error { i32 x = 1; thread_scope { "
         "std.thread::scoped_join_handle<void> h = std.thread::spawn_scoped(bump, &x, 2); "
         "std.thread::scoped_join_handle<void> k = move h; std.thread::join_result<void> r = "
         "std.thread::join(move k); drop r; x += 1; } return x; }",
         NULL},
        {"thread-scoped-shared-borrow-parent-write",
         "void bump(i32* value, i32 amount) { *value += amount; } void look(const i32* value) { "
         "*value as void; } void fill(i32[] values, i32 seed) { for (usize i = 0usize; i < "
         "len(values); i += 1usize) { values[i] = seed + (i as i32); } } i32 run() throws "
         "std.thread::thread_error { i32 x = 1; thread_scope { "
         "std.thread::scoped_join_handle<void> h = std.thread::spawn_scoped(look, &x); x += 4; "
         "move h as void; } return x; }",
         "R-DIAG-BORROW-001"},
        {"thread-scoped-exclusive-parent-read",
         "void bump(i32* value, i32 amount) { *value += amount; } void look(const i32* value) { "
         "*value as void; } void fill(i32[] values, i32 seed) { for (usize i = 0usize; i < "
         "len(values); i += 1usize) { values[i] = seed + (i as i32); } } i32 run() throws "
         "std.thread::thread_error { i32 x = 1; thread_scope { "
         "std.thread::scoped_join_handle<void> h = std.thread::spawn_scoped(bump, &x, 2); i32 seen "
         "= x; move h as void; return seen; } }",
         "R-DIAG-BORROW-001"},
        {"thread-scoped-dropped-handle-keeps-loan",
         "void bump(i32* value, i32 amount) { *value += amount; } void look(const i32* value) { "
         "*value as void; } void fill(i32[] values, i32 seed) { for (usize i = 0usize; i < "
         "len(values); i += 1usize) { values[i] = seed + (i as i32); } } i32 run() throws "
         "std.thread::thread_error { i32 x = 1; thread_scope { "
         "std.thread::scoped_join_handle<void> h = std.thread::spawn_scoped(bump, &x, 2); move h "
         "as void; x += 1; } return x; }",
         "R-DIAG-BORROW-001"},
        {"thread-scoped-named-borrow-suspended",
         "void bump(i32* value, i32 amount) { *value += amount; } void look(const i32* value) { "
         "*value as void; } void fill(i32[] values, i32 seed) { for (usize i = 0usize; i < "
         "len(values); i += 1usize) { values[i] = seed + (i as i32); } } i32 run() throws "
         "std.thread::thread_error { i32 x = 1; i32* p = &x; thread_scope { "
         "std.thread::scoped_join_handle<void> h = std.thread::spawn_scoped(bump, p, 2); *p += 1; "
         "move h as void; } return x; }",
         "R-DIAG-BORROW-001"},
        {"thread-scoped-exclusive-twice",
         "void bump(i32* value, i32 amount) { *value += amount; } void look(const i32* value) { "
         "*value as void; } void fill(i32[] values, i32 seed) { for (usize i = 0usize; i < "
         "len(values); i += 1usize) { values[i] = seed + (i as i32); } } void run() throws "
         "std.thread::thread_error { i32 x = 1; thread_scope { "
         "std.thread::scoped_join_handle<void> h = std.thread::spawn_scoped(bump, &x, 2); "
         "std.thread::scoped_join_handle<void> k = std.thread::spawn_scoped(look, &x); move h as "
         "void; move k as void; } }",
         "R-DIAG-BORROW-001"},
        {"thread-scoped-borrow-of-inner-local",
         "void bump(i32* value, i32 amount) { *value += amount; } void look(const i32* value) { "
         "*value as void; } void fill(i32[] values, i32 seed) { for (usize i = 0usize; i < "
         "len(values); i += 1usize) { values[i] = seed + (i as i32); } } void run() throws "
         "std.thread::thread_error { thread_scope { i32 y = 1; "
         "std.thread::scoped_join_handle<void> h = std.thread::spawn_scoped(bump, &y, 2); move h "
         "as void; } }",
         "R-DIAG-BORROW-002"},
        {"thread-unscoped-exclusive-borrow",
         "void bump(i32* value, i32 amount) { *value += amount; } void look(const i32* value) { "
         "*value as void; } void fill(i32[] values, i32 seed) { for (usize i = 0usize; i < "
         "len(values); i += 1usize) { values[i] = seed + (i as i32); } } void run() throws "
         "std.thread::thread_error { i32 x = 1; std.thread::join_handle<void> h = "
         "std.thread::spawn(bump, &x, 2); std.thread::detach(move h); }",
         "R-DIAG-BORROW-001"},
        /* L13.5, R-STMT-0007: a clause of a switch over a tagged enum or o<T> falls through into a
           next clause that binds no payload; its own bindings end at the edge. */
        {"tagged-fallthrough-into-payload-free",
         "enum Shape { Circle(i32), Square(i32), Empty, Unknown, }; i32 classify(Shape shape) { "
         "i32 score = 0; switch (shape) { case variant Shape::Circle(radius): score += *radius; "
         "fallthrough; case variant Shape::Empty: score += 100; fallthrough; case variant "
         "Shape::Unknown: score += 1000; break; case variant Shape::Square(side): score += *side; "
         "break; } return score; } i32 pick(o<i32> value) { i32 total = 0; switch (value) { case "
         "variant o::some(inner): total += *inner; fallthrough; case variant o::none: total += 5; "
         "break; } return total; }",
         NULL},
        {"tagged-fallthrough-into-binding",
         "enum Shape { Circle(i32), Square(i32), }; i32 classify(Shape shape) { i32 score = 0; "
         "switch (shape) { case variant Shape::Circle(radius): score += *radius; fallthrough; case "
         "variant Shape::Square(side): score += *side; break; } return score; }",
         "R-DIAG-FLOW-001"},
        {"option-fallthrough-into-binding",
         "i32 pick(o<i32> value) { i32 total = 0; switch (value) { case variant o::none: total += "
         "5; fallthrough; case variant o::some(inner): total += *inner; break; } return total; }",
         "R-DIAG-FLOW-001"},
        /* L13.6, R-TYPE-0007: an async function, a C import, a callback and a raw C function
           pointer have a never result; the call form of await observes an async never call, and a
           task<never> handle is rejected by R-TYPE-0029. */
        {"async-never-await-forms",
         "error Stop { i32 code; }; protected async never stop(i32 code) throws Stop { throw Stop "
         "{.code = code}; } protected async never spin() { while (true) { } } protected async i32 "
         "use(i32 code) throws std.async::start_error { try { i32 value = await stop(code); return "
         "value; } catch (Stop failure) { return failure.code; } } protected async i32 last(i32 "
         "code) throws Stop, std.async::start_error { if (code > 0) { await stop(code); } return 1 "
         "+ await stop(code); } protected async i32 idle() throws std.async::start_error { await "
         "spin(); }",
         NULL},
        {"async-never-task-handle",
         "protected async never spin() { while (true) { } } protected async i32 use() throws "
         "std.async::start_error { task<never> job = spin(); await move job; }",
         "R-DIAG-TYPE-001"},
        {"async-never-reaches-end",
         "protected async never stop(i32 code) { if (code > 0) { panic(\"stop\"); } }",
         "R-DIAG-FLOW-001"},
        {"c-never-import-callback-pointer",
         "@header(\"probe.h\") extern \"C\" { @safety(\"PROBE-EXIT\", \"The process ends\") never "
         "probe_exit(c_int status); @safety(\"PROBE-CALL\", \"fatal is valid\") c_int "
         "probe_call(raw fn(c_int) -> never fatal, c_int code); } @callback "
         "@safety(\"PROBE-FATAL\", \"The runtime is initialized\") extern \"C\" never fatal(c_int "
         "code) { unsafe { probe_exit(code); } } never fire(raw fn(c_int) -> never pointer, i32 "
         "code) { unsafe { pointer(code as c_int); } } i32 run(i32 code) { raw fn(c_int) -> never "
         "pointer = fatal; if (code > 9) { fire(pointer, code); } unsafe { c_int ignored = "
         "probe_call(pointer, code as c_int); ignored as void; i32 value = probe_exit(code as "
         "c_int); return value; } }",
         NULL},
        {"c-never-callback-returns",
         "@callback @safety(\"PROBE-FATAL\", \"The runtime is initialized\") extern \"C\" never "
         "fatal(c_int code) { code as void; }",
         "R-DIAG-FLOW-001"},
        /* L13.7, R-TYPE-0013: `atomic raw T*?` holds a nullable raw object pointer in every
           position; fetch arithmetic, assignment and copies stay rejected, and R-OBJ-0009 lets an
           atomic operation reach a mutable static without unsafe. */
        {"atomic-raw-pointer-forms",
         "struct Point { i32 x; }; struct Slot { atomic raw i32*? current; atomic raw const "
         "Point*? shape; }; enum Holder { Empty, Full(atomic raw i32*?), }; @generic<T> struct "
         "Cell { atomic raw T*? head; }; atomic raw void*? global_slot = null; atomic raw i32*? "
         "fresh() { atomic raw i32*? slot = null; return move slot; } bool probe(i32* target) { "
         "Slot slot = Slot {.current = null, .shape = null}; Cell<Point> cell = Cell<Point> {.head "
         "= null}; (atomic raw i32*?)[2] pair = {null, null}; Holder holder = Holder::Full(null); "
         "atomic raw i32*? made = fresh(); unsafe { raw i32* pointer = target as raw i32*; raw "
         "i32*? old = core::atomic_exchange(&slot.current, pointer, core::memory_order::acq_rel); "
         "old as void; core::atomic_compare_exchange_result<raw i32*?> outcome = "
         "core::atomic_compare_exchange(&made, null, pointer, core::memory_order::seq_cst, "
         "core::memory_order::relaxed); move outcome as void; core::atomic_store(&pair[0], "
         "pointer, core::memory_order::release); return core::atomic_load(&cell.head, "
         "core::memory_order::acquire) == null; } }",
         NULL},
        {"atomic-static-operations-safe",
         "struct Stats { au32 hits; (atomic raw i32*?)[2] slots; }; Stats stats = Stats {.hits = "
         "0, .slots = {null, null}}; au32 counter = 0; atomic raw i32*? latest = null; u32 probe() "
         "{ core::atomic_fetch_add(&counter, 1u32, core::memory_order::relaxed) as void; "
         "core::atomic_fetch_add(&stats.hits, 1u32, core::memory_order::relaxed) as void; "
         "core::atomic_store(&stats.slots[1], null, core::memory_order::release); "
         "core::atomic_store(&latest, null, core::memory_order::release); return "
         "core::atomic_load(&counter, core::memory_order::relaxed); }",
         NULL},
        {"generic-nullable-pointer-arguments",
         "@generic<T> struct Wrap { u8 tag; }; u8 probe() { Wrap<const i32*?> a = Wrap<const "
         "i32*?> {.tag = 1u8}; Wrap<atomic raw i32*?> b = Wrap<atomic raw i32*?> {.tag = 2u8}; "
         "Wrap<own i32*?> c = Wrap<own i32*?> {.tag = 3u8}; b.tag as void; c.tag as void; return "
         "a.tag; }",
         NULL},
        {"atomic-raw-fetch",
         "void probe(i32* target) { atomic raw i32*? slot = null; unsafe { raw i32*? old = "
         "core::atomic_fetch_add(&slot, target as raw i32*, core::memory_order::relaxed); old as "
         "void; } }",
         "R-DIAG-TYPE-001"},
        {"atomic-raw-assignment",
         "void probe(i32* target) { atomic raw i32*? slot = null; unsafe { slot = target as raw "
         "i32*; } }",
         "R-DIAG-TYPE-001"},
        {"atomic-raw-copy",
         "void probe() { atomic raw i32*? slot = null; raw i32*? copy = slot; copy as void; }",
         "R-DIAG-MOVE-001"},
        {"atomic-raw-non-nullable",
         "void probe() { atomic raw i32* slot = null; }",
         "R-DIAG-SYN-001"},
        {"atomic-raw-c-signature",
         "@header(\"probe.h\") extern \"C\" { @safety(\"PROBE-TAKE\", \"slot is valid\") void "
         "probe_take(atomic raw c_int*? slot); }",
         "R-DIAG-FFI-001"},
        {"atomic-static-index-read",
         "(au32)[3] counters = {0, 0, 0}; usize index = 1usize; void probe() { "
         "core::atomic_fetch_add(&counters[index], 1u32, core::memory_order::relaxed) as void; }",
         "R-DIAG-MEM-001"},
        {"atomic-static-borrow-for-call",
         "au32 counter = 0; void bump(const (au32)* c) { core::atomic_fetch_add(c, 1u32, "
         "core::memory_order::relaxed) as void; } void probe() { bump(&counter); }",
         "R-DIAG-MEM-001"},
        /* L13.8, R-STMT-0010: a Move switch or match moves an exclusive borrow or a mutable slice
           out of a variant payload with the origins the payload carries. */
        {"variant-view-move-forms",
         "struct Point { i32 x; }; enum Target { Cell(i32*?), Shape(Point*), Row(i32[]), "
         "Text((str)*), Idle, }; i32* pick(Target target, i32* other) { switch (move target) { "
         "case variant Target::Cell(move cell): if (cell == null) { return other; } *cell += 1; "
         "return other; case variant Target::Shape(move shape): return &shape->x; case variant "
         "Target::Row(move row): return &row[0]; case variant Target::Text(move text): *text = "
         "\"t\"; return other; case variant Target::Idle: return other; } } i32* first(o<i32*> "
         "maybe, i32* other) { return match (move maybe) { case variant o::some(move inner): "
         "inner; case variant o::none: other; }; }",
         NULL},
        {"variant-view-move-conflict",
         "enum Choice { Some(i32*), Nothing, }; i32 f() { i32 a = 1; Choice c = Choice::Some(&a); "
         "switch (move c) { case variant Choice::Some(move p): a += 1; *p += 1; break; default: "
         "break; } return a; }",
         "R-DIAG-BORROW-001"},
        {"variant-view-move-borrowed-switch",
         "enum Choice { Some(i32*), Nothing, }; i32 f() { i32 a = 1; Choice c = Choice::Some(&a); "
         "switch (c) { case variant Choice::Some(move p): *p += 1; break; default: break; } return "
         "a; }",
         "R-DIAG-MOVE-003"},
        {"variant-view-move-user-drop",
         "enum Holder { Some(i32*), Nothing, }; drop(Holder* self) { self as void; } i32 f() { i32 "
         "a = 1; Holder c = Holder::Some(&a); switch (move c) { case variant Holder::Some(move p): "
         "*p += 1; break; default: break; } return a; }",
         "R-DIAG-MOVE-003"},
        {"variant-view-move-use-after-move",
         "enum Choice { Some(i32*), Nothing, }; i32 f() { i32 a = 1; Choice c = Choice::Some(&a); "
         "switch (move c) { case variant Choice::Some(move p): *p += 1; break; default: break; } "
         "switch (c) { case variant Choice::Some(p): p as void; break; default: break; } return a; "
         "}",
         "R-DIAG-MOVE-002"},
        {"variant-view-move-returned-region",
         "enum Choice { Some(i32*), Nothing, }; i32* keep(Choice c, i32* other) { switch (move c) "
         "{ case variant Choice::Some(move p): return p; default: return other; } } i32 f() { i32 "
         "a = 1; i32 b = 2; i32* k = keep(Choice::Some(&a), &b); i32 seen = a; *k += 1; return "
         "seen; }",
         "R-DIAG-BORROW-001"},
        {"variant-view-move-across-await",
         "enum Target { Cell(i32*), Idle, }; protected async i32 tick(i32 value) { return value + "
         "1; } protected async i32 work() throws std.async::start_error { i32 a = 1; Target first "
         "= Target::Cell(&a); switch (move first) { case variant Target::Cell(move cell): i32 seen "
         "= await tick(0); *cell += seen; break; default: return 1; } return a; }",
         "R-DIAG-ASYNC-001"},
        /* L13.9, R-ERR-0002: a caught error that holds a container of borrows is rethrown or
           dropped; its borrows keep the origins of the thrown arguments. */
        {"caught-borrow-error-consumption",
         "error Bad { array<i32*> slots; }; void fail(i32* slot) throws Bad, "
         "std.array::push_error<i32*> { array<i32*> slots = std.array::create::<i32*>(); "
         "std.array::push(&slots, slot); throw Bad {.slots = move slots}; } void "
         "rethrow_named(i32* p) throws Bad, std.array::push_error<i32*> { try { fail(p); } catch "
         "(Bad e) { throw move e; } } void rethrow_bare(i32* p) throws Bad, "
         "std.array::push_error<i32*> { try { fail(p); } catch (Bad e) { throw; } } void "
         "dropped(i32* p) throws std.array::push_error<i32*> { try { fail(p); } catch (Bad e) { "
         "drop e; } }",
         NULL},
        {"caught-borrow-error-read-conflict",
         "error Bad { array<i32*> slots; }; void fail(i32* slot) throws Bad, "
         "std.array::push_error<i32*> { array<i32*> slots = std.array::create::<i32*>(); "
         "std.array::push(&slots, slot); throw Bad {.slots = move slots}; } i32 f() { i32 a = 1; "
         "try { fail(&a); } catch (Bad e) { i32 seen = a; *e.slots[0] += 1; return seen; } catch "
         "(std.array::push_error<i32*> e) { move e as void; } return a; }",
         "R-DIAG-BORROW-001"},
        {"caught-borrow-error-escape",
         "error Bad { array<i32*> slots; }; void fail(i32* slot) throws Bad, "
         "std.array::push_error<i32*> { array<i32*> slots = std.array::create::<i32*>(); "
         "std.array::push(&slots, slot); throw Bad {.slots = move slots}; } i32 f() throws Bad { "
         "i32 a = 1; try { fail(&a); } catch (Bad e) { throw move e; } catch "
         "(std.array::push_error<i32*> e) { move e as void; } return a; }",
         "R-DIAG-BORROW-002"},
        {"caught-borrow-error-rethrow-after-drop",
         "error Bad { array<i32*> slots; }; void fail(i32* slot) throws Bad, "
         "std.array::push_error<i32*> { array<i32*> slots = std.array::create::<i32*>(); "
         "std.array::push(&slots, slot); throw Bad {.slots = move slots}; } void f(i32* p) throws "
         "Bad, std.array::push_error<i32*> { try { fail(p); } catch (Bad e) { drop e; throw; } }",
         "R-DIAG-EFFECT-002"},
        /* L13.10, R-BORROW-0018: a borrow stored through a borrow a call returns reaches the
           storage that exclusive borrows held by the argument designate; a value read through such a
           borrow carries what that storage holds. */
        {"store-through-call-held-borrows",
         "(str)* first_slot(array<(str)*>* slots) { return (*slots)[0]; } array<str>* "
         "first_list(array<array<str>*>* lists) { return (*lists)[0]; } usize probe(str text) { "
         "str a = \"a\"; array<str> words = std.array::create::<str>(); array<(str)*> slots = "
         "std.array::create::<(str)*>(); array<array<str>*> lists = "
         "std.array::create::<array<str>*>(); try { std.array::push(&slots, &a); "
         "*first_slot(&slots) = text; std.array::push(&lists, &words); "
         "std.array::push(first_list(&lists), text); } catch (std.array::push_error<(str)*> e) { "
         "move e as void; } catch (std.array::push_error<array<str>*> e) { move e as void; } catch "
         "(std.array::push_error<str> e) { move e as void; } drop slots; drop lists; return len(a) "
         "+ len(words); }",
         NULL},
        {"store-through-call-same-parameter",
         "(str)* first_slot(array<(str)*>* slots) { return (*slots)[0]; } array<str>* "
         "first_list(array<array<str>*>* lists) { return (*lists)[0]; } (str)* "
         "last_slot(array<(str)*>* slots) { return (*slots)[len(*slots) - 1usize]; } void "
         "rotate(array<(str)*>* slots) { str last = *last_slot(slots); *first_slot(slots) = last; }",
         NULL},
        {"read-through-call-result",
         "str* pick(array<str>* words) { return &(*words)[0]; } usize peek(array<str>* words) { "
         "return len(*words); } usize probe(array<str>* words) { str last = *pick(words); usize n "
         "= peek(words); (*words)[0] = \"z\"; return n + len(last); }",
         NULL},
        {"store-through-call-dangling",
         "(str)* first_slot(array<(str)*>* slots) { return (*slots)[0]; } array<str>* "
         "first_list(array<array<str>*>* lists) { return (*lists)[0]; } usize probe() throws "
         "std.alloc::alloc_error { str a = \"a\"; array<(str)*> slots = "
         "std.array::create::<(str)*>(); try { std.array::push(&slots, &a); } catch "
         "(std.array::push_error<(str)*> e) { move e as void; } { std.string::string temp = "
         "std.string::from_str(\"t\"); *first_slot(&slots) = std.string::as_str(&temp); } drop "
         "slots; return len(a); }",
         "R-DIAG-BORROW-002"},
        {"store-through-call-other-parameter",
         "(str)* first_slot(array<(str)*>* slots) { return (*slots)[0]; } array<str>* "
         "first_list(array<array<str>*>* lists) { return (*lists)[0]; } void probe(array<(str)*>* "
         "slots, str text) { *first_slot(slots) = text; }",
         NULL},
        {"push-through-call-dangling",
         "(str)* first_slot(array<(str)*>* slots) { return (*slots)[0]; } array<str>* "
         "first_list(array<array<str>*>* lists) { return (*lists)[0]; } usize probe() throws "
         "std.alloc::alloc_error { array<str> words = std.array::create::<str>(); "
         "array<array<str>*> lists = std.array::create::<array<str>*>(); try { "
         "std.array::push(&lists, &words); { std.string::string temp = "
         "std.string::from_str(\"t\"); std.array::push(first_list(&lists), "
         "std.string::as_str(&temp)); } } catch (std.array::push_error<array<str>*> e) { move e as "
         "void; } catch (std.array::push_error<str> e) { move e as void; } drop lists; return "
         "len(words); }",
         "R-DIAG-BORROW-002"},
        {"read-through-call-dangling",
         "str* pick(array<str>* words) { return &(*words)[0]; } usize probe() throws "
         "std.alloc::alloc_error { array<str> words = std.array::create::<str>(); str last = "
         "\"x\"; { std.string::string temp = std.string::from_str(\"t\"); try { "
         "std.array::push(&words, std.string::as_str(&temp)); } catch (std.array::push_error<str> "
         "e) { move e as void; } last = *pick(&words); } std.array::clear(&words); return "
         "len(last); }",
         "R-DIAG-BORROW-002"},
        {"read-through-call-exclusive-conflict",
         "str* pick(array<str>* words) { return &(*words)[0]; } usize probe() { array<str> words = "
         "std.array::create::<str>(); try { std.array::push(&words, \"a\"); } catch "
         "(std.array::push_error<str> e) { move e as void; } str* p = pick(&words); str last = "
         "*pick(&words); *p = \"y\"; return len(last); }",
         "R-DIAG-BORROW-001"},
        {"store-through-function-item-call",
         "(str)* first_slot(array<(str)*>* slots) { return (*slots)[0]; } array<str>* "
         "first_list(array<array<str>*>* lists) { return (*lists)[0]; } usize probe(str text) { "
         "str a = \"a\"; array<(str)*> slots = std.array::create::<(str)*>(); try { "
         "std.array::push(&slots, &a); } catch (std.array::push_error<(str)*> e) { move e as void; "
         "} auto pick = first_slot; *pick(&slots) = text; drop slots; return len(a); }",
         NULL},
        {"store-through-function-item-dangling",
         "(str)* first_slot(array<(str)*>* slots) { return (*slots)[0]; } array<str>* "
         "first_list(array<array<str>*>* lists) { return (*lists)[0]; } usize probe() throws "
         "std.alloc::alloc_error { str a = \"a\"; array<(str)*> slots = "
         "std.array::create::<(str)*>(); try { std.array::push(&slots, &a); } catch "
         "(std.array::push_error<(str)*> e) { move e as void; } auto pick = first_slot; { "
         "std.string::string temp = std.string::from_str(\"t\"); *pick(&slots) = "
         "std.string::as_str(&temp); } drop slots; return len(a); }",
         "R-DIAG-BORROW-002"},
        {"dyn-read-then-store-through-method",
         "trait Slots { (str)* first(Self* this); }; struct Box { str a; }; impl Slots for Box { "
         "(str)* first(Box* this) { return &this->a; } }; void probe(dyn(Slots)* s) { str v = "
         "*s->first(); *s->first() = v; }",
         NULL},
        {"dyn-store-foreign-view",
         "trait Slots { (str)* first(Self* this); }; struct Box { str a; }; impl Slots for Box { "
         "(str)* first(Box* this) { return &this->a; } }; void probe(dyn(Slots)* s, str t) { "
         "*s->first() = t; }",
         NULL},
        {"conflicting-argument-borrow-result",
         "str* pick(array<str>* words) { return &(*words)[0]; } usize probe() { array<str> words = "
         "std.array::create::<str>(); str* p = pick(&words); str* q = pick(&words); *p = \"y\"; "
         "return len(*q); }",
         "R-DIAG-BORROW-001"},
        /* L13.11, R-TYPE-0007: a local, a parameter, a struct field and a variant payload of type
           never; containers, shared owners, tasks, checked errors and generic arguments still require
           inhabited types. */
        {"never-object-forms",
         "struct Halt { i32 code; never reason; }; enum Step { Run(i32), Stop(never), }; i32 "
         "reason_of(Halt h) { i32 r = h.reason; return r; } i32 take(never value) { return value; "
         "} i32 run(Step s) { switch (s) { case variant Step::Run(v): return *v; case variant "
         "Step::Stop(n): return 0; } } i32 local_end(i32 x) { if (x > 0) { return x; } never "
         "halted = panic(\"x\"); } i32 make(i32 x) { if (x > 0) { return run(Step::Run(x)); } Halt "
         "h = Halt {.code = 1, .reason = panic(\"h\")}; return reason_of(h) + take(panic(\"t\")) + "
         "run(Step::Stop(panic(\"s\"))); }",
         NULL},
        {"never-local-completing-initializer",
         "void probe() { never halted = 5; }",
         "R-DIAG-TYPE-001"},
        {"never-field-container-element",
         "struct Halt { i32 code; never reason; }; usize probe() { array<Halt> items = "
         "std.array::create::<Halt>(); return len(items); }",
         "R-DIAG-TYPE-001"},
        {"never-field-checked-error",
         "error Refused { i32 code; never reason; }; void probe() throws Refused { }",
         "R-DIAG-EFFECT-002"},
        {"never-field-shared-owner",
         "struct Halt { i32 code; never reason; }; void probe(arc Halt h) { drop h; }",
         "R-DIAG-TYPE-001"},
        {"never-field-task-result",
         "struct Halt { i32 code; never reason; }; void probe(task<Halt> t) { t as void; }",
         "R-DIAG-TYPE-001"},
        {"never-generic-argument",
         "@generic<T> struct Wrap { T value; }; i32 probe(const Wrap<never>* w) { w as void; "
         "return 0; }",
         "R-DIAG-TYPE-001"},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char source[2048];
        const int length =
            snprintf(source, sizeof(source), "module test.l13_regressions; %s", cases[i].source);
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
             r_semantic_diagnostic_code_count(context, cases[i].diagnostic) != 0U) &&
            r_semantic_diagnostic_code_count(context, "R-DIAG-SLICE-001") == 0U;
        if (!valid) {
            (void)fprintf(stderr, "l13 regression %s status %d\n", cases[i].name, (int)status);
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

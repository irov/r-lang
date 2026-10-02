/* L11 (docs/language-completeness-roadmap.ru.md): the remaining H1/H2 forms and the defects found
 * while closing them. Each case is accepted, or keeps its normative diagnostic. */
static void r_semantic_test_l11_regressions(void) {
    static const struct {
        const char *name;
        const char *source;
        const char *diagnostic;
    } cases[] = {
        /* L11-1, R-BORROW-0019: a store through a view reaches the storage the view
           designates, not the sources of what that storage holds. */
        {"view-store-content-parameter",
         "usize probe(str text, str other) { str[2] local = {text, \"b\"}; str[] w = "
         "local[0usize..2usize]; w[1] = other; return len(local[0]) + len(local[1]); }",
         NULL},
        {"view-store-parameter-storage",
         "struct Holder { str name; }; void rename(Holder* holder, str name) { holder->name = "
         "name; }",
         NULL},
        /* L11-2, R-BORROW-0002: an exclusive slice excludes other access to its storage only; a
           view that storage holds stays shared. */
        {"loop-slice-store-shared-source",
         "usize probe() throws std.alloc::alloc_error { str[2] first = {\"a\", \"b\"}; "
         "std.string::string keep = std.string::from_str(\"k\"); str kv = "
         "std.string::as_str(&keep); usize total = len(kv); str[] w = first[0usize..2usize]; "
         "for (usize i = 0usize; i < 2usize; i += 1usize) { w[i] = std.string::as_str(&keep); "
         "} return total + len(first[0]); }",
         NULL},
        {"loop-slice-store-mutated-source",
         "usize probe() throws std.alloc::alloc_error { str[2] first = {\"a\", \"b\"}; "
         "std.string::string keep = std.string::from_str(\"k\"); str kv = "
         "std.string::as_str(&keep); usize total = len(kv); str[] w = first[0usize..2usize]; "
         "for (usize i = 0usize; i < 2usize; i += 1usize) { w[i] = std.string::as_str(&keep); "
         "std.string::append(&keep, \"x\"); } return total + len(first[0]); }",
         "R-DIAG-BORROW-001"},
        /* L11.2, R-BORROW-0002, R-BORROW-0007: an exclusive borrow of a value that holds borrows is
           a local, a result and a parameter; stores through it reach the storage it designates. */
        {"exclusive-holder-local-store",
         "struct Holder { str name; i32 id; }; usize probe(str text) { Holder a = {.name = \"a\", "
         ".id = 1}; Holder* direct = &a; direct->name = text; direct->id = 5; return len(a.name) + "
         "(a.id as usize); }",
         NULL},
        {"exclusive-holder-local-conflict",
         "struct Holder { str name; i32 id; }; usize probe() { Holder h = {.name = \"a\", .id = "
         "1}; Holder* p = &h; h.id = 3; p->id = 4; return 0usize; }",
         "R-DIAG-BORROW-001"},
        {"exclusive-holder-local-escape",
         "struct Holder { str name; i32 id; }; Holder* bad() { Holder h = {.name = \"a\", .id = "
         "1}; return &h; }",
         "R-DIAG-BORROW-002"},
        {"exclusive-holder-dangling-store",
         "struct Holder { str name; i32 id; }; usize probe() throws std.alloc::alloc_error { "
         "Holder h = {.name = \"a\", .id = 1}; Holder* p = &h; { std.string::string temp = "
         "std.string::from_str(\"t\"); p->name = std.string::as_str(&temp); } return len(h.name); "
         "}",
         "R-DIAG-BORROW-002"},
        {"exclusive-holder-result-dangling-store",
         "struct Holder { str name; i32 id; }; Holder* pick(Holder* l, Holder* r, bool left) { if "
         "(left == true) { return l; } return r; } usize probe() throws std.alloc::alloc_error { "
         "Holder a = {.name = \"a\", .id = 1}; Holder b = {.name = \"b\", .id = 2}; { "
         "std.string::string temp = std.string::from_str(\"t\"); Holder* q = pick(&a, &b, true); "
         "q->name = std.string::as_str(&temp); } return len(a.name); }",
         "R-DIAG-BORROW-002"},
        {"exclusive-holder-parameter-foreign-store",
         "struct Holder { str name; i32 id; }; Holder* set(Holder* h, str s) { h->name = s; return "
         "h; }",
         NULL},
        {"exclusive-holder-two-borrows",
         "struct Holder { str name; i32 id; }; usize probe() { Holder h = {.name = \"a\", .id = "
         "1}; Holder* p1 = &h; Holder* p2 = &h; p1->id = 1; p2->id = 2; return 0usize; }",
         "R-DIAG-BORROW-001"},
        {"exclusive-string-variable",
         "usize probe(str text) { str s = \"a\"; (str)* sp = &s; *sp = text; return len(s); }",
         NULL},
        {"exclusive-string-dangling-store",
         "usize probe() throws std.alloc::alloc_error { str s = \"a\"; (str)* sp = &s; { "
         "std.string::string temp = std.string::from_str(\"t\"); *sp = std.string::as_str(&temp); "
         "} return len(s); }",
         "R-DIAG-BORROW-002"},
        {"slice-of-strings-parameter-foreign-store",
         "void fill(str[] w, str other) { w[0] = other; }",
         NULL},
        {"string-borrow-parameter-foreign-store",
         "void fill((str)* sp, str other) { *sp = other; }",
         NULL},
        {"container-through-borrow-local",
         "usize probe(str text) { array<str> names = std.array::create::<str>(); array<str>* p = "
         "&names; try { std.array::push(p, text); } catch (std.array::push_error<str> e) { e as "
         "void; } return len(names); }",
         NULL},
        {"container-through-method-result",
         "struct Owner { array<str> names; }; array<str>* Owner::names_mut(Owner* this) { return "
         "&this->names; } usize probe(str text) { Owner owner = {.names = "
         "std.array::create::<str>()}; try { std.array::push(owner.names_mut(), text); } catch "
         "(std.array::push_error<str> e) { e as void; } return len(owner.names); }",
         NULL},
        {"element-borrow-dangling-store",
         "usize probe() throws std.alloc::alloc_error { list<str> names = "
         "std.list::create::<str>(); { std.string::string temp = std.string::from_str(\"t\"); try "
         "{ *std.list::push_back(&names, \"x\") = std.string::as_str(&temp); } catch "
         "(std.list::push_error<str> e) { e as void; } } return len(names); }",
         "R-DIAG-BORROW-002"},
        {"element-borrow-parameter-foreign-store",
         "void add(list<str>* names, str other) throws std.list::push_error<str> { "
         "*std.list::push_back(names, \"x\") = other; }",
         NULL},
        {"mutable-slice-result",
         "str[] tail(str[] words) { return words[1usize..len(words)]; } usize probe(str text) { "
         "str[3] local = {\"one\", \"two\", \"three\"}; str[] rest = tail(local[0usize..3usize]); "
         "rest[0] = text; return len(local[1]); }",
         NULL},
        {"trait-and-generic-exclusive-view-results",
         "struct Holder { str name; i32 id; }; trait Named { str* name_mut(Self* this); }; impl "
         "Named for Holder { str* name_mut(Holder* this) { return &this->name; } }; "
         "@generic<T> T* first_mut(T[] items) { return &items[0]; } usize probe(str text) { "
         "Holder h = {.name = \"a\", .id = 1}; str* slot = h.name_mut(); *slot = text; str[2] "
         "words = {\"x\", \"y\"}; str* first = first_mut::<str>(words[0usize..2usize]); *first "
         "= text; return len(h.name) + len(words[0]); }",
         NULL},
        {"optional-exclusive-view-holder-parameter",
         "struct Holder { str name; i32 id; }; void maybe_rename(o<Holder*> holder) { switch "
         "(move holder) { case variant o::some(item): (**item).id = 7; break; case variant "
         "o::none: break; } }",
         NULL},
        {"optional-exclusive-string-parameter-foreign-store",
         "void maybe_store(o<(str)*> slot, str text) { switch (move slot) { case variant "
         "o::some(item): **item = text; break; case variant o::none: break; } }",
         "R-DIAG-BORROW-002"},
        /* L11-5, R-BORROW-0002: a borrow of a variable that is itself a view designates the
           variable, so writing the variable conflicts with it. */
        {"string-variable-shared-borrow-write",
         "usize probe(str text, str other) { str s = text; const str* p = &s; if (len(other) > "
         "0usize) { s = other; } return len(*p); }",
         "R-DIAG-BORROW-001"},
        {"string-variable-shared-borrow-read",
         "usize probe(str text) { str s = text; const str* p = &s; return len(*p) + len(text); }",
         NULL},
        /* L11-3, R-BORROW-0007: a view of a local of the loop body stored into a symbol
           declared outside the loop dangles in the next iteration and after the loop. */
        {"loop-carried-dangling-local",
         "usize probe() throws std.alloc::alloc_error { str s = \"a\"; usize total = 0usize; "
         "for (usize i = 0usize; i < 2usize; i += 1usize) { total += len(s); "
         "std.string::string temp = std.string::from_str(\"t\"); str view = "
         "std.string::as_str(&temp); if (i == 0usize) { s = view; } else { total += len(view); "
         "} } return total; }",
         "R-DIAG-BORROW-002"},
        {"loop-carried-dangling-element",
         "usize probe() throws std.alloc::alloc_error { str[2] first = {\"a\", \"b\"}; usize "
         "total = 0usize; for (usize i = 0usize; i < 2usize; i += 1usize) { total += "
         "len(first[0]); std.string::string temp = std.string::from_str(\"t\"); first[0] = "
         "std.string::as_str(&temp); } return total; }",
         "R-DIAG-BORROW-002"},
        {"loop-carried-dangling-field",
         "struct Named { str name; }; usize probe() throws std.alloc::alloc_error { Named n = "
         "{.name = \"a\"}; usize total = 0usize; for (usize i = 0usize; i < 2usize; i += "
         "1usize) { total += len(n.name); std.string::string temp = "
         "std.string::from_str(\"t\"); n.name = std.string::as_str(&temp); } return total; }",
         "R-DIAG-BORROW-002"},
        {"loop-carried-stored-value",
         "usize probe() throws std.alloc::alloc_error { str[2] first = {\"a\", \"b\"}; usize "
         "total = 0usize; { std.string::string outer = std.string::from_str(\"o\"); str view = "
         "std.string::as_str(&outer); total += len(view); str s = \"x\"; for (usize i = 0usize; "
         "i < 2usize; i += 1usize) { first[0] = s; if (i == 0usize) { s = view; } } } return "
         "total + len(first[0]); }",
         "R-DIAG-BORROW-002"},
        {"loop-carried-view-target",
         "usize probe() throws std.alloc::alloc_error { str[2] first = {\"a\", \"b\"}; str[2] "
         "second = {\"c\", \"d\"}; str[] w = first[0usize..2usize]; for (usize i = 0usize; i < "
         "2usize; i += 1usize) { std.string::string temp = std.string::from_str(\"t\"); w[0] = "
         "std.string::as_str(&temp); if (i == 0usize) { w = second[0usize..2usize]; } } return "
         "len(second[0]); }",
         "R-DIAG-BORROW-002"},
        {"loop-same-iteration-view",
         "usize probe() throws std.alloc::alloc_error { str s = \"a\"; usize total = 0usize; "
         "for (usize i = 0usize; i < 3usize; i += 1usize) { std.string::string temp = "
         "std.string::from_str(\"t\"); str view = std.string::as_str(&temp); if (i == 0usize) { "
         "s = view; total += len(s); } total += len(view); } return total; }",
         NULL},
        {"loop-parameter-views",
         "usize probe(const str[] words) throws std.array::push_error<str> { array<str> kept "
         "= std.array::create::<str>(); str longest = \"\"; for (usize i = 0usize; i < "
         "len(words); i += 1usize) { str word = words[i]; if (len(word) > len(longest)) { "
         "longest = word; } std.array::push(&kept, word); } return len(kept) + len(longest); "
         "}",
         NULL},
        {"loop-rotating-fields",
         "struct Pair { str left; str right; }; usize probe(str a, str b) { Pair pair = {.left "
         "= a, .right = b}; usize total = 0usize; usize i = 0usize; while (i < 3usize) { total "
         "+= len(pair.left); str swap = pair.left; pair.left = pair.right; pair.right = swap; i "
         "+= 1usize; } return total + len(pair.right); }",
         NULL},
        /* L11.4, R-BORROW-0018: containers of structs, options and containers whose elements hold
           views; element access keeps the container storage apart from what it holds. */
        {"container-struct-elements",
         "struct Named { str name; i32 id; }; usize probe(str text, str other) throws "
         "std.list::push_error<Named>, std.dict::insert_error<i32, Named> { list<Named> items = "
         "std.list::create::<Named>(); std.list::push_back(&items, Named {.name = text, .id = 1}) "
         "as void; switch (std.list::front_mut(&items)) { case variant o::some(front): "
         "(*front)->name = other; break; case variant o::none: break; } usize total = 0usize; for "
         "(const Named* item in &items) { total += len(item->name); } dict<i32, Named> index = "
         "std.dict::create::<i32, Named>(); std.dict::insert(&index, 1, Named {.name = other, .id "
         "= 2}) as void; i32 key = 1; switch (std.dict::get(&index, &key)) { case variant "
         "o::some(found): total += len((*found)->name); break; case variant o::none: break; } "
         "return total; }",
         NULL},
        {"container-struct-element-dangling",
         "struct Named { str name; i32 id; }; usize probe() throws std.alloc::alloc_error { "
         "list<Named> items = std.list::create::<Named>(); { std.string::string temp = "
         "std.string::from_str(\"t\"); try { std.list::push_back(&items, Named {.name = "
         "std.string::as_str(&temp), .id = 1}) as void; } catch (std.list::push_error<Named> e) { "
         "move e as void; } } usize total = 0usize; for (const Named* item in &items) { total += "
         "len(item->name); } return total; }",
         "R-DIAG-BORROW-002"},
        {"container-struct-element-source-mutated",
         "struct Named { str name; i32 id; }; usize probe() throws std.alloc::alloc_error { "
         "std.string::string temp = std.string::from_str(\"t\"); list<Named> items = "
         "std.list::create::<Named>(); try { std.list::push_back(&items, Named {.name = "
         "std.string::as_str(&temp), .id = 1}) as void; } catch (std.list::push_error<Named> e) { "
         "move e as void; } std.string::append_str(&temp, \"x\"); return len(items); }",
         "R-DIAG-BORROW-001"},
        {"container-parameter-foreign-store",
         "struct Named { str name; i32 id; }; void keep(list<Named>* items) throws "
         "std.alloc::alloc_error, std.list::push_error<Named> { std.string::string temp = "
         "std.string::from_str(\"t\"); std.list::push_back(items, Named {.name = "
         "std.string::as_str(&temp), .id = 1}) as void; }",
         "R-DIAG-BORROW-002"},
        {"container-optional-element-dangling",
         "usize probe() throws std.alloc::alloc_error { array<o<str>> maybe = "
         "std.array::create::<o<str>>(); { std.string::string temp = std.string::from_str(\"t\"); "
         "try { std.array::push(&maybe, o::some(std.string::as_str(&temp))); } catch "
         "(std.array::push_error<o<str>> e) { move e as void; } } return len(maybe); }",
         "R-DIAG-BORROW-002"},
        {"container-nested-dangling",
         "usize probe() throws std.alloc::alloc_error { array<array<str>> groups = "
         "std.array::create::<array<str>>(); { std.string::string temp = "
         "std.string::from_str(\"t\"); array<str> inner = std.array::create::<str>(); try { "
         "std.array::push(&inner, std.string::as_str(&temp)); std.array::push(&groups, move "
         "inner); } catch (std.array::push_error<str> e) { move e as void; } catch "
         "(std.array::push_error<array<str>> e) { move e as void; } } return len(groups); }",
         "R-DIAG-BORROW-002"},
        {"container-dict-value-dangling",
         "struct Named { str name; i32 id; }; usize probe() throws std.alloc::alloc_error { "
         "dict<i32, Named> index = std.dict::create::<i32, Named>(); { std.string::string temp = "
         "std.string::from_str(\"t\"); try { std.dict::insert(&index, 1, Named {.name = "
         "std.string::as_str(&temp), .id = 1}) as void; } catch (std.dict::insert_error<i32, "
         "Named> e) { move e as void; } } return len(index); }",
         "R-DIAG-BORROW-002"},
        {"container-literal-dangling",
         "struct Named { str name; i32 id; }; usize probe() throws std.alloc::alloc_error { "
         "o<array<Named>> kept = o::none; { std.string::string temp = std.string::from_str(\"t\"); "
         "try { array<Named> items = [Named {.name = std.string::as_str(&temp), .id = 1}]; kept = "
         "o::some(move items); } catch (std.array::push_error<Named> e) { move e as void; } } "
         "usize total = 0usize; switch (kept) { case variant o::some(items): total = len(*items); "
         "break; case variant o::none: break; } return total; }",
         "R-DIAG-BORROW-002"},
        {"container-popped-element-dangling",
         "struct Named { str name; i32 id; }; usize probe() throws std.alloc::alloc_error { "
         "o<Named> first = o::none; { std.string::string temp = std.string::from_str(\"t\"); "
         "list<Named> items = std.list::create::<Named>(); try { std.list::push_back(&items, Named "
         "{.name = std.string::as_str(&temp), .id = 1}) as void; } catch "
         "(std.list::push_error<Named> e) { move e as void; } first = std.list::pop_front(&items); "
         "} usize total = 0usize; switch (first) { case variant o::some(value): total = "
         "len(value->name); break; case variant o::none: break; } return total; }",
         "R-DIAG-BORROW-002"},
        {"container-element-store-dangling",
         "struct Named { str name; i32 id; }; usize probe(str text) throws std.alloc::alloc_error, "
         "std.list::push_error<Named> { list<Named> items = std.list::create::<Named>(); "
         "std.list::push_back(&items, Named {.name = text, .id = 1}) as void; { std.string::string "
         "temp = std.string::from_str(\"t\"); switch (std.list::get_mut(&items, 0usize)) { case "
         "variant o::some(entry): (*entry)->name = std.string::as_str(&temp); break; case variant "
         "o::none: break; } } usize total = 0usize; for (const Named* item in &items) { total += "
         "len(item->name); } return total; }",
         "R-DIAG-BORROW-002"},
        {"container-exclusive-iteration-store",
         "usize probe(str text, str other) throws std.array::push_error<str> { array<str> items = "
         "std.array::create::<str>(); std.array::push(&items, text); for (str* item in &items) { "
         "*item = other; } return len(items); }",
         NULL},
        {"container-exclusive-iteration-store-dangling",
         "struct Named { str name; i32 id; }; usize probe(str text) throws std.alloc::alloc_error, "
         "std.array::push_error<Named> { array<Named> items = std.array::create::<Named>(); "
         "std.array::push(&items, Named {.name = text, .id = 1}); { std.string::string temp = "
         "std.string::from_str(\"t\"); for (Named* item in &items) { item->name = "
         "std.string::as_str(&temp); } } usize total = 0usize; for (const Named* item in &items) { "
         "total += len(item->name); } return total; }",
         "R-DIAG-BORROW-002"},
        /* L11-10, R-BORROW-0018: a view read out of an element names what the element holds, not
           the container, in a loop too. */
        {"container-element-read-then-clear",
         "struct Named { str name; i32 id; }; usize probe(str text) throws "
         "std.array::push_error<Named> { array<Named> named = std.array::create::<Named>(); "
         "std.array::push(&named, Named {.name = text, .id = 3}); str first = \"\"; switch "
         "(std.array::get(&named, 0usize)) { case variant o::some(entry): first = (*entry)->name; "
         "break; case variant o::none: break; } str last = \"\"; for (const Named* item in &named) "
         "{ last = item->name; } std.array::clear(&named); return len(first) + len(last); }",
         NULL},
        {"container-element-read-then-owner-mutated",
         "usize probe() throws std.alloc::alloc_error { std.string::string owner = "
         "std.string::from_str(\"t\"); array<str> names = std.array::create::<str>(); try { "
         "std.array::push(&names, std.string::as_str(&owner)); } catch (std.array::push_error<str> "
         "e) { move e as void; } str copy = \"\"; for (const str* entry in &names) { copy = "
         "*entry; } std.array::clear(&names); std.string::append_str(&owner, \"x\"); return "
         "len(copy); }",
         "R-DIAG-BORROW-001"},
        /* L11-7, R-BORROW-0018: a borrow of a place reached through a view also reaches what the
           designated storage holds. */
        {"view-reborrow-held-owner-mutated",
         "struct Named { str name; i32 id; }; usize probe() throws std.alloc::alloc_error { "
         "std.string::string owner = std.string::from_str(\"x\"); Named n = {.name = "
         "std.string::as_str(&owner), .id = 1}; Named* p = &n; str* q = &p->name; "
         "std.string::append_str(&owner, \"y\"); return len(*q); }",
         "R-DIAG-BORROW-001"},
        {"view-shared-reborrow-held-owner-mutated",
         "struct Named { str name; i32 id; }; usize probe() throws std.alloc::alloc_error { "
         "std.string::string owner = std.string::from_str(\"x\"); Named n = {.name = "
         "std.string::as_str(&owner), .id = 1}; const Named* p = &n; const str* q = &p->name; "
         "std.string::append_str(&owner, \"y\"); return len(*q); }",
         "R-DIAG-BORROW-001"},
        /* L11-8, R-BORROW-0018: a store through a payload binding keeps the binding a view of the
           container. */
        {"payload-store-keeps-container",
         "usize probe(str text, str other) throws std.array::push_error<str> { array<str> names = "
         "std.array::create::<str>(); std.array::push(&names, text); usize total = 0usize; switch "
         "(std.array::get_mut(&names, 0usize)) { case variant o::some(e): **e = other; "
         "std.array::clear(&names); **e = text; total = len(**e); break; case variant o::none: "
         "break; } return total; }",
         "R-DIAG-BORROW-001"},
        /* L11-9, R-BORROW-0007: an element read through a borrow of a slice parameter is caller
           storage. */
        {"slice-parameter-element-results",
         "str first(const str[] words) { for (str w in &words) { return w; } return \"\"; } usize "
         "keep(const str[] words) throws std.array::push_error<str> { array<str> made = [*w for "
         "(const str* w in &words)]; return len(made); }",
         NULL},
        {"slice-local-owner-element-result",
         "str leak() throws std.alloc::alloc_error { std.string::string s = "
         "std.string::from_str(\"x\"); str[1] local = {std.string::as_str(&s)}; const str[] w = "
         "local[0usize..1usize]; for (const str* x in &w) { return *x; } return \"\"; }",
         "R-DIAG-BORROW-002"},
        /* L11-4, R-BORROW-0025: a literal component staged through a temporary keeps the origin of
           the value it carries. */
        {"literal-call-components",
         "struct Named { str name; i32 id; }; usize probe() throws std.alloc::alloc_error { "
         "std.string::string temp = std.string::from_str(\"t\"); Named n = {.name = "
         "std.string::as_str(&temp), .id = 1}; str[2] pair = {std.string::as_str(&temp), \"b\"}; "
         "return len(n.name) + len(pair[0]); }",
         NULL},
        {"literal-call-component-owner-mutated",
         "struct Named { str name; i32 id; }; usize probe() throws std.alloc::alloc_error { "
         "std.string::string temp = std.string::from_str(\"t\"); Named n = {.name = "
         "std.string::as_str(&temp), .id = 1}; std.string::append_str(&temp, \"x\"); return "
         "len(n.name); }",
         "R-DIAG-BORROW-001"},
        /* L11.5, R-BORROW-0018: containers of shared borrows and slices of values that hold views.
         */
        {"container-shared-borrows",
         "struct Named { str name; i32 id; }; usize probe(str text) { str a = text; Named n = "
         "{.name = text, .id = 1}; str[2] words = {text, \"b\"}; array<const str*> views = "
         "std.array::create::<const str*>(); list<const Named*> refs = std.list::create::<const "
         "Named*>(); array<const str[]> spans = std.array::create::<const str[]>(); try { "
         "std.array::push(&views, &a); std.list::push_back(&refs, &n) as void; "
         "std.array::push(&spans, words[0usize..2usize]); } catch (std.array::push_error<const "
         "str*> e) { move e as void; } catch (std.list::push_error<const Named*> e) { move e as "
         "void; } catch (std.array::push_error<const str[]> e) { move e as void; } usize total = "
         "0usize; for (const (const str*)* v in &views) { total += len(**v); } for (const (const "
         "Named*)* r in &refs) { total += len((*r)->name); } for (const (const str[])* s in "
         "&spans) { total += len(*s); } return total; }",
         NULL},
        {"container-shared-borrow-holder-write",
         "usize probe(str text, str other) { str a = text; array<const str*> views = "
         "std.array::create::<const str*>(); try { std.array::push(&views, &a); } catch "
         "(std.array::push_error<const str*> e) { move e as void; } a = other; return len(views); "
         "}",
         "R-DIAG-BORROW-001"},
        {"container-shared-borrow-dangling",
         "usize probe(str text) { array<const str*> views = std.array::create::<const str*>(); { "
         "str a = text; try { std.array::push(&views, &a); } catch (std.array::push_error<const "
         "str*> e) { move e as void; } } usize total = 0usize; for (const (const str*)* v in "
         "&views) { total += len(**v); } return total; }",
         "R-DIAG-BORROW-002"},
        {"container-shared-borrow-owner-mutated",
         "struct Named { str name; i32 id; }; usize probe() throws std.alloc::alloc_error { "
         "std.string::string owner = std.string::from_str(\"t\"); Named n = {.name = "
         "std.string::as_str(&owner), .id = 1}; array<const Named*> refs = "
         "std.array::create::<const Named*>(); try { std.array::push(&refs, &n); } catch "
         "(std.array::push_error<const Named*> e) { move e as void; } "
         "std.string::append_str(&owner, \"x\"); usize total = 0usize; for (const (const Named*)* "
         "r in &refs) { total += len((*r)->name); } return total; }",
         "R-DIAG-BORROW-001"},
        /* L11.6, R-BORROW-0002: containers of exclusive borrows and mutable slices hold exclusive
           loans; a shared path attenuates them to shared access. */
        {"container-exclusive-borrows",
         "i32 probe() { i32 a = 1; i32 b = 2; i32[2] x = {1, 2}; i32[2] y = {3, 4}; array<i32*> "
         "slots = std.array::create::<i32*>(); list<i32*> queue = std.list::create::<i32*>(); "
         "array<i32[]> parts = std.array::create::<i32[]>(); try { std.array::push(&slots, &a); "
         "std.list::push_back(&queue, &b) as void; std.array::push(&parts, x[0usize..2usize]); "
         "std.array::push(&parts, y[0usize..2usize]); } catch (std.array::push_error<i32*> e) { "
         "move e as void; } catch (std.list::push_error<i32*> e) { move e as void; } catch "
         "(std.array::push_error<i32[]> e) { move e as void; } for ((i32*)* s in &slots) { **s += "
         "10; } switch (std.list::front_mut(&queue)) { case variant o::some(s): ***s += 10; break; "
         "case variant o::none: break; } for ((i32[])* p in &parts) { (*p)[0] += 10; } o<i32*> "
         "last = std.array::pop(&slots); switch (move last) { case variant o::some(p): **p += 1; "
         "break; case variant o::none: break; } std.list::clear(&queue); std.array::clear(&parts); "
         "return a + b + x[0] + y[0]; }",
         NULL},
        {"container-exclusive-borrow-read-conflict",
         "i32 probe() { i32 a = 1; array<i32*> slots = std.array::create::<i32*>(); try { "
         "std.array::push(&slots, &a); } catch (std.array::push_error<i32*> e) { move e as void; } "
         "i32 seen = a; std.array::clear(&slots); return seen; }",
         "R-DIAG-BORROW-001"},
        {"container-exclusive-borrow-twice",
         "i32 probe() { i32 a = 1; array<i32*> slots = std.array::create::<i32*>(); try { "
         "std.array::push(&slots, &a); std.array::push(&slots, &a); } catch "
         "(std.array::push_error<i32*> e) { move e as void; } std.array::clear(&slots); return a; "
         "}",
         "R-DIAG-BORROW-001"},
        {"container-exclusive-borrow-dangling",
         "i32 probe() { array<i32*> slots = std.array::create::<i32*>(); { i32 a = 1; try { "
         "std.array::push(&slots, &a); } catch (std.array::push_error<i32*> e) { move e as void; } "
         "} i32 total = 0; for (const (i32*)* s in &slots) { total += **s; } return total; }",
         "R-DIAG-BORROW-002"},
        {"container-exclusive-borrow-shared-iteration-write",
         "i32 probe() { i32 a = 1; array<i32*> slots = std.array::create::<i32*>(); try { "
         "std.array::push(&slots, &a); } catch (std.array::push_error<i32*> e) { move e as void; } "
         "for (const (i32*)* s in &slots) { **s = 5; } std.array::clear(&slots); return a; }",
         "R-DIAG-BORROW-001"},
        {"container-exclusive-borrow-shared-get-write",
         "i32 probe() { i32 a = 1; array<i32*> slots = std.array::create::<i32*>(); try { "
         "std.array::push(&slots, &a); } catch (std.array::push_error<i32*> e) { move e as void; } "
         "switch (std.array::get(&slots, 0usize)) { case variant o::some(s): ***s = 5; break; case "
         "variant o::none: break; } std.array::clear(&slots); return a; }",
         "R-DIAG-BORROW-001"},
        {"struct-exclusive-borrow-shared-path-write",
         "struct Holder { i32* p; }; i32 probe() { i32 a = 1; Holder h = {.p = &a}; const Holder* "
         "view = &h; *view->p = 5; return 0; }",
         "R-DIAG-BORROW-001"},
        {"container-overlapping-exclusive-slices",
         "i32 probe() { i32[4] data = {1, 2, 3, 4}; array<i32[]> parts = "
         "std.array::create::<i32[]>(); try { std.array::push(&parts, data[0usize..2usize]); "
         "std.array::push(&parts, data[1usize..3usize]); } catch (std.array::push_error<i32[]> e) "
         "{ move e as void; } std.array::clear(&parts); return data[0]; }",
         "R-DIAG-BORROW-001"},
        {"exclusive-borrow-container-parameters",
         "i32 total(list<i32*> slots) { i32 sum = 0; for (const (i32*)* s in &slots) { sum += **s; "
         "} return sum; } void bump(list<i32*>* slots) { switch (std.list::front_mut(slots)) { "
         "case variant o::some(s): ***s += 1; break; case variant o::none: break; } } i32 probe() "
         "{ i32 a = 1; list<i32*> slots = std.list::create::<i32*>(); try { "
         "std.list::push_back(&slots, &a) as void; } catch (std.list::push_error<i32*> e) { move e "
         "as void; } bump(&slots); i32 t = total(move slots); return t + a; }",
         NULL},
        /* R-BORROW-0019: a container reached through a borrow held in another value; a push target
           returned by a call was checked through the arguments it reborrows. */
        {"container-through-held-borrow",
         "struct Holder { array<str>* names; }; usize probe(str s) throws "
         "std.array::push_error<str> { array<str> kept = std.array::create::<str>(); Holder h = "
         "{.names = &kept}; std.array::push(h.names, s); return len(kept); }",
         NULL},
        {"container-through-held-borrow-dangling",
         "struct Holder { array<str>* names; }; usize probe() throws std.alloc::alloc_error, "
         "std.array::push_error<str> { array<str> kept = std.array::create::<str>(); Holder h = "
         "{.names = &kept}; { std.string::string temp = std.string::from_str(\"t\"); try { "
         "std.array::push(h.names, std.string::as_str(&temp)); } catch (std.array::push_error<str> "
         "e) { move e as void; } } return len(kept); }",
         "R-DIAG-BORROW-002"},
        {"container-through-held-parameter-foreign-store",
         "struct Holder { array<str>* names; }; void add(Holder h, str s) throws "
         "std.array::push_error<str> { std.array::push(h.names, s); }",
         "R-DIAG-BORROW-002"},
        {"push-target-method-result-through-borrow-local",
         "struct Box { array<i32> items; }; array<i32>* Box::numbers(Box* this) { return "
         "&this->items; } trait Store { array<i32>* values(Self* this); }; impl Store for Box { "
         "array<i32>* values(Box* this) { return &this->items; } }; usize probe() throws "
         "std.array::push_error<i32> { Box b = {.items = std.array::create::<i32>()}; Box* s = &b; "
         "std.array::push(s->numbers(), 1); std.array::push(s->numbers(), 2); dyn(Store)* d = s; "
         "std.array::push(d->values(), 3); return 0usize; }",
         NULL},
        {"push-target-method-result-conflict",
         "struct Box { array<i32> items; }; array<i32>* Box::numbers(Box* this) { return "
         "&this->items; } usize probe() throws std.array::push_error<i32> { Box b = {.items = "
         "std.array::create::<i32>()}; Box* s = &b; array<i32>* other = &b.items; "
         "std.array::push(s->numbers(), 1); std.array::push(other, 2); return 0usize; }",
         "R-DIAG-BORROW-001"},
        /* L13.10 closed the borrow returned by a call whose storage is reached through exclusive
           borrows held by an argument (R-BORROW-0018). */
        {"temporary-borrow-through-held-exclusive-borrows",
         "(str)* first_slot(array<(str)*>* slots) { switch (std.array::get_mut(slots, 0usize)) { "
         "case variant o::some(slot): return &***slot; case variant o::none: break; } "
         "panic(\"empty\"); } usize probe(str text) { str a = \"a\"; array<(str)*> slots = "
         "std.array::create::<(str)*>(); try { std.array::push(&slots, &a); } catch "
         "(std.array::push_error<(str)*> e) { move e as void; } *first_slot(&slots) = text; "
         "std.array::clear(&slots); return len(a); }",
         NULL},
        {"container-target-through-held-exclusive-borrows",
         "array<str>* first_list(array<array<str>*>* lists) { switch (std.array::get_mut(lists, "
         "0usize)) { case variant o::some(slot): return &***slot; case variant o::none: break; } "
         "panic(\"empty\"); } usize probe(str text) throws std.array::push_error<str> { array<str> "
         "kept = std.array::create::<str>(); array<array<str>*> lists = "
         "std.array::create::<array<str>*>(); try { std.array::push(&lists, &kept); } catch "
         "(std.array::push_error<array<str>*> e) { move e as void; } "
         "std.array::push(first_list(&lists), text); std.array::clear(&lists); return len(kept); }",
         NULL},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char source[2048];
        const int length =
            snprintf(source, sizeof(source), "module test.l11_regressions; %s", cases[i].source);
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
            (void)fprintf(stderr, "l11 regression %s status %d\n", cases[i].name, (int)status);
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

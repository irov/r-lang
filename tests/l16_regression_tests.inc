/* L16 (docs/language-completeness-roadmap.ru.md): lending trait methods, core::LendingIterator
 * and associated types with type parameters, with the S7 contract defects L16-1..L16-3. Each
 * case is accepted, or is rejected with the diagnostic code and, where given, the rule it
 * names; no case may leave a construct outside the implemented slice (R-DIAG-SLICE-001). */
static void r_semantic_test_l16_regressions(void) {
    static const struct {
        const char *name;
        const char *source;
        const char *diagnostic;
    } cases[] = {
        /* L16.1, R-TYPE-0045: a `@lending` method lends views of the storage its receiver
           designates; the caller holds the whole receiver until the result ends, so elements are
           used one at a time. */
        {"lending-user-trait",
         "import std.string; trait LineSource { type Line; @lending o<Self::Line> next_line(Self* "
         "this); }; struct Lines { std.string::string buffer; usize served; }; impl LineSource "
         "for Lines { type Line = str; o<str> next_line(Lines* this) { if (this->served >= "
         "2usize) { return o::none; } this->served += 1usize; return "
         "o::some(std.string::as_str(&this->buffer)); } }; usize f(Lines* lines) { usize n = "
         "0usize; while (true) { o<str> line = lines->next_line(); switch (move line) { case "
         "variant o::some(move text): n += len(text); break; case variant o::none: return n; } } "
         "return n; }",
         NULL},
        {"lending-generic-loop",
         "trait LineSource { type Line; @lending o<Self::Line> next_line(Self* this); }; "
         "@generic<L: LineSource> usize count(L* source) { usize n = 0usize; while (true) { "
         "o<L::Line> line = source->next_line(); switch (move line) { case variant o::some(move "
         "item): move item as void; n += 1usize; break; case variant o::none: return n; } } "
         "return n; }",
         NULL},
        {"lending-generic-hold-two",
         "trait LineSource { type Line; @lending o<Self::Line> next_line(Self* this); }; "
         "@generic<L: LineSource> usize two(L* source) { o<L::Line> first = source->next_line(); "
         "o<L::Line> second = source->next_line(); move first as void; move second as void; "
         "return 2usize; }",
         "R-DIAG-BORROW-001 [R-BORROW-0002]"},
        {"lending-dyn-one-at-a-time",
         "import std.string; trait LineSource { type Line; @lending o<Self::Line> next_line(Self* "
         "this); }; struct Lines { std.string::string buffer; usize served; }; impl LineSource "
         "for Lines { type Line = str; o<str> next_line(Lines* this) { if (this->served >= "
         "2usize) { return o::none; } this->served += 1usize; return "
         "o::some(std.string::as_str(&this->buffer)); } }; usize f(dyn(LineSource & Line = str)* "
         "source) { usize n = 0usize; while (true) { o<str> line = source->next_line(); switch "
         "(move line) { case variant o::some(move text): n += len(text); break; case variant "
         "o::none: return n; } } return n; }",
         NULL},
        {"lending-dyn-hold-two",
         "import std.string; trait LineSource { type Line; @lending o<Self::Line> next_line(Self* "
         "this); }; struct Lines { std.string::string buffer; usize served; }; impl LineSource "
         "for Lines { type Line = str; o<str> next_line(Lines* this) { if (this->served >= "
         "2usize) { return o::none; } this->served += 1usize; return "
         "o::some(std.string::as_str(&this->buffer)); } }; usize f(dyn(LineSource & Line = str)* "
         "source) { o<str> first = source->next_line(); o<str> second = source->next_line(); move "
         "first as void; move second as void; return 0usize; }",
         "R-DIAG-BORROW-001 [R-BORROW-0002]"},
        {"lending-opaque-one-at-a-time",
         "import std.string; trait LineSource { type Line; @lending o<Self::Line> next_line(Self* "
         "this); }; struct Lines { std.string::string buffer; usize served; }; impl LineSource "
         "for Lines { type Line = str; o<str> next_line(Lines* this) { if (this->served >= "
         "2usize) { return o::none; } this->served += 1usize; return "
         "o::some(std.string::as_str(&this->buffer)); } }; opaque(LineSource & Line = str) "
         "lines(str text) throws std.alloc::alloc_error { return Lines { .buffer = "
         "std.string::from_str(text), .served = 0usize }; } usize f(str input) throws "
         "std.alloc::alloc_error { auto source = lines(input); usize n = 0usize; while (true) { "
         "o<str> line = source.next_line(); switch (move line) { case variant o::some(move text): "
         "n += len(text); break; case variant o::none: return n; } } return n; }",
         NULL},
        {"lending-opaque-hold-two",
         "import std.string; trait LineSource { type Line; @lending o<Self::Line> next_line(Self* "
         "this); }; struct Lines { std.string::string buffer; usize served; }; impl LineSource "
         "for Lines { type Line = str; o<str> next_line(Lines* this) { if (this->served >= "
         "2usize) { return o::none; } this->served += 1usize; return "
         "o::some(std.string::as_str(&this->buffer)); } }; opaque(LineSource & Line = str) "
         "lines(str text) throws std.alloc::alloc_error { return Lines { .buffer = "
         "std.string::from_str(text), .served = 0usize }; } usize f(str input) throws "
         "std.alloc::alloc_error { auto source = lines(input); o<str> first = source.next_line(); "
         "o<str> second = source.next_line(); move first as void; move second as void; return "
         "0usize; }",
         "R-DIAG-BORROW-001 [R-BORROW-0002]"},
        /* Without `@lending` the result of an associated type holds no view of the receiver
           storage. */
        {"lending-unmarked-rejects-view",
         "import std.string; trait LineSource { type Line; o<Self::Line> next_line(Self* this); "
         "}; struct Lines { std.string::string buffer; }; impl LineSource for Lines { type Line = "
         "str; o<str> next_line(Lines* this) { return o::some(std.string::as_str(&this->buffer)); "
         "} };",
         "R-DIAG-BORROW-002 [R-TYPE-0045]"},
        {"lending-free-function",
         "@lending str first(const str[] words) { return words[0usize]; }",
         "R-DIAG-TRAIT-001 [R-TYPE-0045]"},
        {"lending-impl-of-plain-method",
         "trait Source { type Item; o<Self::Item> take(Self* this); }; struct Numbers { i32[2] "
         "values; }; impl Source for Numbers { type Item = i32; @lending o<i32> take(Numbers* "
         "this) { return o::some(this->values[0usize]); } };",
         "R-DIAG-TRAIT-001 [R-TYPE-0045]"},
        /* L16.2, R-STMT-0014: range-for advances a `core::LendingIterator`; each element lives
           until the next advance. */
        {"lending-iterator-range-for",
         "import std.string; struct Lines { std.string::string buffer; usize served; }; impl "
         "core::LendingIterator for Lines { type Item = str; o<str> next(Lines* this) { if "
         "(this->served >= 2usize) { return o::none; } this->served += 1usize; return "
         "o::some(std.string::as_str(&this->buffer)); } }; usize total(Lines lines) { usize sum = "
         "0usize; for (str line in &lines) { sum += len(line); } return sum; }",
         NULL},
        {"lending-iterator-keeps-element",
         "import std.string; struct Lines { std.string::string buffer; usize served; }; impl "
         "core::LendingIterator for Lines { type Item = str; o<str> next(Lines* this) { if "
         "(this->served >= 2usize) { return o::none; } this->served += 1usize; return "
         "o::some(std.string::as_str(&this->buffer)); } }; usize total(Lines lines, str initial) "
         "{ str last = initial; usize sum = 0usize; for (str line in &lines) { sum += len(last); "
         "last = line; } return sum + len(last); }",
         "R-DIAG-BORROW-001 [R-BORROW-0002]"},
        /* L16.3, R-TYPE-0045: associated types with type parameters. An application `P::Name<T>`
           stays abstract in a generic definition and reduces to the binding at instantiation. */
        {"gat-generic-use",
         "trait Wrapper { type Of<T: copy>; Self::Of<i32> wrap_int(Self* this, i32 value); i32 "
         "unwrap_int(Self* this, Self::Of<i32> value); }; struct Maybe { i32 fallback; }; impl "
         "Wrapper for Maybe { type Of<T> = o<T>; o<i32> wrap_int(Maybe* this, i32 value) { return "
         "o::some(value); } i32 unwrap_int(Maybe* this, o<i32> value) { switch (move value) { "
         "case variant o::some(move v): return v; case variant o::none: return this->fallback; } "
         "} }; @generic<W: Wrapper> i32 roundtrip(W* wrapper, i32 value) { W::Of<i32> wrapped = "
         "wrapper->wrap_int(value); return wrapper->unwrap_int(move wrapped); } i32 f() { Maybe m "
         "= Maybe { .fallback = 3 }; return roundtrip(&m, 42); }",
         NULL},
        {"gat-default-body",
         "trait Boxing { type Boxed<T>; Self::Boxed<i32> put(Self* this, i32 value); i32 "
         "take(Self* this, Self::Boxed<i32> boxed); i32 cycle(Self* this, i32 value) { "
         "Self::Boxed<i32> boxed = this->put(value); return this->take(move boxed); } }; struct "
         "Heap { i32 bias; }; impl Boxing for Heap { type Boxed<T> = own T*; own i32* put(Heap* "
         "this, i32 value) { return new i32 (value + this->bias); } i32 take(Heap* this, own i32* "
         "boxed) { i32 value = *boxed; move boxed as void; return value; } }; i32 f() { Heap heap "
         "= Heap { .bias = 2 }; return heap.cycle(5); }",
         NULL},
        {"gat-two-parameters",
         "@generic<A, B> struct Both { A left; B right; }; trait Joiner { type Joined<L, R>; "
         "Self::Joined<i32, bool> join(Self* this, i32 left, bool right); }; struct Plain { i32 "
         "unused; }; impl Joiner for Plain { type Joined<L, R> = Both<R, L>; Both<bool, i32> "
         "join(Plain* this, i32 left, bool right) { return Both<bool, i32> { .left = right, "
         ".right = left }; } }; @generic<J: Joiner> J::Joined<i32, bool> run(J* joiner) { return "
         "joiner->join(4, true); }",
         NULL},
        {"gat-nested-and-generic-impl",
         "trait Wrapper { type Of<T: copy>: copy; Self::Of<i32> wrap_int(Self* this, i32 value); "
         "i32 unwrap_int(Self* this, Self::Of<i32> value); }; struct Maybe { i32 fallback; }; "
         "impl Wrapper for Maybe { type Of<T> = o<T>; o<i32> wrap_int(Maybe* this, i32 value) { "
         "return o::some(value); } i32 unwrap_int(Maybe* this, o<i32> value) { switch (move "
         "value) { case variant o::some(move v): return v; case variant o::none: return "
         "this->fallback; } } }; @generic<X: Wrapper> struct Lift { X inner; }; @generic<X: "
         "Wrapper> impl Wrapper for Lift<X> { type Of<T> = X::Of<T>; X::Of<i32> wrap_int(Lift<X>* "
         "this, i32 value) { return this->inner.wrap_int(value); } i32 unwrap_int(Lift<X>* this, "
         "X::Of<i32> value) { return this->inner.unwrap_int(value); } }; @generic<W: Wrapper> "
         "W::Of<W::Of<i32>> nest(W::Of<W::Of<i32>> value) { return value; } i32 f() { Lift<Maybe> "
         "lift = Lift<Maybe> { .inner = Maybe { .fallback = 1 } }; o<i32> value = "
         "lift.wrap_int(2); o<o<i32>> nested = nest::<Lift<Maybe>>(o::some(value)); nested as "
         "void; return lift.unwrap_int(value); }",
         NULL},
        {"gat-generic-trait",
         "@generic<U: copy> trait Converter { type Out<T: copy>; Self::Out<U> convert(Self* this, "
         "U value); }; @generic<A: copy, B: copy> struct Tagged { A value; B tag; }; struct "
         "Stamper { u8 stamp; }; impl Converter<i32> for Stamper { type Out<T> = Tagged<T, u8>; "
         "Tagged<i32, u8> convert(Stamper* this, i32 value) { return Tagged<i32, u8> { .value = "
         "value, .tag = this->stamp }; } }; @generic<C: Converter<i32>> C::Out<i32> round(C* "
         "converter) { return converter->convert(1); }",
         NULL},
        {"gat-parameter-trait-bound",
         "trait Show { i32 shown(const Self* this); }; struct Point { i32 x; }; impl Show for "
         "Point { i32 shown(const Point* this) { return this->x; } }; @generic<T: Show> struct "
         "Labeled { T value; }; trait Family { type Member<T: Show>; Self::Member<Point> "
         "make(Self* this, Point point); }; struct Labeler { i32 next; }; impl Family for Labeler "
         "{ type Member<T> = Labeled<T>; Self::Member<Point> make(Labeler* this, Point point) { "
         "return Labeled<Point> { .value = point }; } }; @generic<F: Family> i32 run(F* family) { "
         "F::Member<Point> member = family->make(Point { .x = 4 }); move member as void; return "
         "0; }",
         NULL},
        {"gat-bare-use",
         "trait Wrapper { type Of<T: copy>; Self::Of<i32> wrap_int(Self* this, i32 value); i32 "
         "unwrap_int(Self* this, Self::Of<i32> value); }; struct Maybe { i32 fallback; }; impl "
         "Wrapper for Maybe { type Of<T> = o<T>; o<i32> wrap_int(Maybe* this, i32 value) { return "
         "o::some(value); } i32 unwrap_int(Maybe* this, o<i32> value) { switch (move value) { "
         "case variant o::some(move v): return v; case variant o::none: return this->fallback; } "
         "} }; @generic<W: Wrapper> i32 f(W* w) { W::Of v = w->wrap_int(1); move v as void; "
         "return 0; }",
         "R-DIAG-TYPE-001 [R-TYPE-0045]"},
        {"gat-arity",
         "trait Wrapper { type Of<T: copy>; Self::Of<i32> wrap_int(Self* this, i32 value); i32 "
         "unwrap_int(Self* this, Self::Of<i32> value); }; struct Maybe { i32 fallback; }; impl "
         "Wrapper for Maybe { type Of<T> = o<T>; o<i32> wrap_int(Maybe* this, i32 value) { return "
         "o::some(value); } i32 unwrap_int(Maybe* this, o<i32> value) { switch (move value) { "
         "case variant o::some(move v): return v; case variant o::none: return this->fallback; } "
         "} }; @generic<W: Wrapper> i32 f(W* w) { W::Of<i32, i32> v = w->wrap_int(1); move v as "
         "void; return 0; }",
         "R-DIAG-TYPE-001 [R-TYPE-0045]"},
        {"gat-arguments-on-plain-associated-type",
         "trait Source { type Item; }; @generic<S: Source> i32 f(S* s) { S::Item<i32>* p = null; "
         "return 0; }",
         "R-DIAG-TYPE-001 [R-TYPE-0045]"},
        {"gat-binding-parameter-count",
         "trait Wrapper { type Of<T: copy>; Self::Of<i32> wrap_int(Self* this, i32 value); i32 "
         "unwrap_int(Self* this, Self::Of<i32> value); }; struct Maybe { i32 fallback; }; impl "
         "Wrapper for Maybe { type Of<T, U> = o<T>; o<i32> wrap_int(Maybe* this, i32 value) { "
         "return o::some(value); } i32 unwrap_int(Maybe* this, o<i32> value) { return 0; } };",
         "R-DIAG-TRAIT-001 [R-TYPE-0045]"},
        {"gat-argument-constraint",
         "trait Wrapper { type Of<T: copy>; Self::Of<i32> wrap_int(Self* this, i32 value); i32 "
         "unwrap_int(Self* this, Self::Of<i32> value); }; struct Maybe { i32 fallback; }; impl "
         "Wrapper for Maybe { type Of<T> = o<T>; o<i32> wrap_int(Maybe* this, i32 value) { return "
         "o::some(value); } i32 unwrap_int(Maybe* this, o<i32> value) { switch (move value) { "
         "case variant o::some(move v): return v; case variant o::none: return this->fallback; } "
         "} }; @generic<W: Wrapper> i32 f(W* w, W::Of<own i32*> v) { move v as void; return 0; }",
         "R-DIAG-TYPE-001 [R-TYPE-0001]"},
        {"gat-binding-capability",
         "trait Wrapper { type Of<T: copy>: copy; }; struct Maybe { i32 fallback; }; impl Wrapper "
         "for Maybe { type Of<T> = own T*; };",
         "R-DIAG-TRAIT-001 [R-TYPE-0045]"},
        {"gat-constant-argument",
         "trait Wrapper { type Of<T: copy>; Self::Of<i32> wrap_int(Self* this, i32 value); i32 "
         "unwrap_int(Self* this, Self::Of<i32> value); }; struct Maybe { i32 fallback; }; impl "
         "Wrapper for Maybe { type Of<T> = o<T>; o<i32> wrap_int(Maybe* this, i32 value) { return "
         "o::some(value); } i32 unwrap_int(Maybe* this, o<i32> value) { switch (move value) { "
         "case variant o::some(move v): return v; case variant o::none: return this->fallback; } "
         "} }; @generic<W: Wrapper> i32 f(W* w) { W::Of<3> v = w->wrap_int(1); move v as void; "
         "return 0; }",
         "R-DIAG-TYPE-001 [R-TYPE-0012]"},
        {"gat-binding-parameter-constraint",
         "trait Wrapper { type Of<T: copy>; Self::Of<i32> wrap_int(Self* this, i32 value); i32 "
         "unwrap_int(Self* this, Self::Of<i32> value); }; struct Maybe { i32 fallback; }; impl "
         "Wrapper for Maybe { type Of<T: copy> = o<T>; o<i32> wrap_int(Maybe* this, i32 value) { "
         "return o::some(value); } i32 unwrap_int(Maybe* this, o<i32> value) { return 0; } };",
         "R-DIAG-SYN-001 [R-GRAM-0005]"},
        {"gat-duplicate-parameter",
         "trait Wrapper { type Of<T, T>; };",
         "R-DIAG-NAME-002 [R-NAME-0004]"},
        {"gat-shadows-trait-parameter",
         "@generic<T> trait Wrapper { type Of<T>; };",
         "R-DIAG-NAME-002 [R-NAME-0004]"},
        {"gat-shadows-implementation-parameter",
         "trait Wrapper { type Of<T>; }; @generic<T> struct Holder { T value; }; @generic<T> impl "
         "Wrapper for Holder<T> { type Of<T> = o<T>; };",
         "R-DIAG-NAME-002 [R-NAME-0004]"},
        {"gat-dyn-contract",
         "trait Wrapper { type Of<T: copy>; Self::Of<i32> wrap_int(Self* this, i32 value); i32 "
         "unwrap_int(Self* this, Self::Of<i32> value); }; struct Maybe { i32 fallback; }; impl "
         "Wrapper for Maybe { type Of<T> = o<T>; o<i32> wrap_int(Maybe* this, i32 value) { return "
         "o::some(value); } i32 unwrap_int(Maybe* this, o<i32> value) { switch (move value) { "
         "case variant o::some(move v): return v; case variant o::none: return this->fallback; } "
         "} }; i32 f(dyn(Wrapper)* w) { return 0; }",
         "R-DIAG-TRAIT-001 [R-TYPE-0051]"},
        {"gat-opaque-contract",
         "trait Wrapper { type Of<T: copy>; Self::Of<i32> wrap_int(Self* this, i32 value); i32 "
         "unwrap_int(Self* this, Self::Of<i32> value); }; struct Maybe { i32 fallback; }; impl "
         "Wrapper for Maybe { type Of<T> = o<T>; o<i32> wrap_int(Maybe* this, i32 value) { return "
         "o::some(value); } i32 unwrap_int(Maybe* this, o<i32> value) { switch (move value) { "
         "case variant o::some(move v): return v; case variant o::none: return this->fallback; } "
         "} }; opaque(Wrapper) make() { return Maybe { .fallback = 0 }; }",
         "R-DIAG-TRAIT-001 [R-TYPE-0048]"},
        {"gat-signature-mismatch",
         "trait Wrapper { type Of<T: copy>; Self::Of<i32> wrap_int(Self* this, i32 value); i32 "
         "unwrap_int(Self* this, Self::Of<i32> value); }; struct Maybe { i32 fallback; }; impl "
         "Wrapper for Maybe { type Of<T> = o<T>; o<i64> wrap_int(Maybe* this, i32 value) { return "
         "o::some(1i64); } i32 unwrap_int(Maybe* this, o<i32> value) { return 0; } };",
         "R-DIAG-TRAIT-001 [R-TYPE-0042]"},
        {"gat-receiver-view",
         "trait Viewer { type View<T>; Self::View<i32> view(Self* this); }; struct Cell { i32 "
         "value; }; impl Viewer for Cell { type View<T> = const T*; const i32* view(Cell* this) { "
         "return &this->value; } };",
         "R-DIAG-BORROW-002 [R-TYPE-0045]"},
        {"gat-lending-view",
         "trait Viewer { type View<T>; @lending Self::View<i32> view(Self* this); }; struct Cell "
         "{ i32 value; }; impl Viewer for Cell { type View<T> = const T*; const i32* view(Cell* "
         "this) { return &this->value; } }; @generic<V: Viewer> i32 use(V* viewer) { V::View<i32> "
         "first = viewer->view(); move first as void; V::View<i32> second = viewer->view(); move "
         "second as void; return 0; }",
         NULL},
        {"gat-lending-hold-two",
         "trait Viewer { type View<T>; @lending Self::View<i32> view(Self* this); }; @generic<V: "
         "Viewer> i32 use(V* viewer) { V::View<i32> first = viewer->view(); V::View<i32> second = "
         "viewer->view(); move first as void; move second as void; return 0; }",
         "R-DIAG-BORROW-001 [R-BORROW-0002]"},
        /* L16-1: an associated type a subtrait inherits keeps the associated-result contract in
           the subtrait's methods, at the call and in the implementation. */
        {"s7-inherited-associated-result",
         "trait Source { type Item; Self::Item pull(Self* this); i32 consume(Self* this, "
         "Self::Item item); }; trait Doubler: Source { Self::Item doubled(Self* this); }; "
         "@generic<D: Doubler> i32 use(D* d) { D::Item twice = d->doubled(); return "
         "d->consume(move twice); }",
         NULL},
        {"s7-inherited-receiver-view",
         "trait Source { type Item; Self::Item pull(Self* this); }; trait Peek: Source { "
         "Self::Item peek(Self* this); }; struct Cell { i32 value; }; impl Source for Cell { type "
         "Item = i32; i32 pull(Cell* this) { return this->value; } }; struct Other { i32 value; "
         "}; impl Source for Other { type Item = const i32*; const i32* pull(Other* this) { "
         "return null; } }; impl Peek for Other { const i32* peek(Other* this) { return "
         "&this->value; } };",
         "R-DIAG-BORROW-002 [R-TYPE-0045]"},
        /* L16-2: an application of a generic trait keeps the associated-result contract. */
        {"s7-generic-trait-associated-result",
         "@generic<U: copy> trait Converter { type Out; Self::Out convert(Self* this, U value); U "
         "back(Self* this, Self::Out value); }; @generic<C: Converter<i32>> i32 round(C* "
         "converter, i32 value) { C::Out out = converter->convert(value); return "
         "converter->back(move out); }",
         NULL},
        {"s7-generic-trait-receiver-view",
         "@generic<U: copy> trait Source { type Item; Self::Item pull(Self* this, U unused); }; "
         "struct Cell { i32 value; }; impl Source<i32> for Cell { type Item = const i32*; const "
         "i32* pull(Cell* this, i32 unused) { return &this->value; } };",
         "R-DIAG-BORROW-002 [R-TYPE-0045]"},
        /* L16-3: a receiver whose type holds no view gives an associated result nothing to hold. */
        {"s7-value-receiver-unborrowed",
         "trait Source { type Item: copy & unborrowed; Self::Item pull(Self* this); i32 "
         "consume(Self* this, Self::Item item); }; @generic<S: Source & copy & unborrowed> i32 "
         "use(S s) { S local = s; S::Item item = local.pull(); return local.consume(item); }",
         NULL},
        {"s7-value-receiver-holds-view",
         "import std.string; trait Source { type Item; Self::Item pull(Self* this); }; struct "
         "Holder { str text; }; impl Source for Holder { type Item = str; str pull(Holder* this) "
         "{ return this->text; } }; usize f() throws std.alloc::alloc_error { std.string::string "
         "s = std.string::from_str(\"abc\"); Holder h = Holder { .text = std.string::as_str(&s) "
         "}; str item = h.pull(); move s as void; return len(item); }",
         "R-DIAG-BORROW-001 [R-OWN-0002]"},
    };
    for (size_t i = 0U; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char source[2048];
        const int length =
            snprintf(source, sizeof(source), "module test.l16_regressions; %s", cases[i].source);
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
            (void)fprintf(stderr, "l16 regression %s status %d\n", cases[i].name, (int)status);
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

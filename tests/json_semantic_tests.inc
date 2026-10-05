static void r_semantic_test_json(void) {
    static const struct {
        const char *body;
        const char *code;
    } cases[] = {
        {"void f(std.json::reader reader) {}", "R-DIAG-JSON-001"},
        {"void f(std.json::reader<i32> reader) {}", "R-DIAG-JSON-001"},
        {"void f(std.json::detached<std.io::input, i32> reader) {}", "R-DIAG-SYN-001"},
        {"void f(std.io::input input) throws std.json::error, std.alloc::alloc_error { "
         "std.json::reader<std.io::input> r = std.json::new_reader(input); }",
         "R-DIAG-JSON-001"},
        {"void f(std.io::input input) throws std.json::error, std.alloc::alloc_error { "
         "std.json::reader<std.io::input> r = std.json::new_reader(move input); std.io::input "
         "again = move input; }",
         "R-DIAG-MOVE-002"},
        {"void f(std.io::input input) throws std.json::error { try { "
         "std.json::reader<std.io::input> r = std.json::new_reader(move input); drop r; } "
         "catch(std.alloc::alloc_error error) { std.io::input kept = move input; } }",
         NULL},
        {"void f(std.json::reader<std.io::input>* r) throws std.async::start_error { task<o<i32> "
         "throws std.json::error, std.alloc::alloc_error, std.io::io_error> t = "
         "std.json::read_next(r, o::none); std.async::cancel(move t); }",
         NULL},
        {"void f(const std.json::reader<std.io::input>* r) throws std.async::start_error { "
         "task<o<i32> throws std.json::error, std.alloc::alloc_error, std.io::io_error> t = "
         "std.json::read_next(r, o::none); std.async::cancel(move t); }",
         "R-DIAG-JSON-001"},
        {"async void f(std.json::reader<std.io::input> r) throws std.async::start_error, "
         "std.json::error, std.alloc::alloc_error, std.io::io_error { o<i32> value = await "
         "std.json::read_next(&r, o::none); value as void; }",
         NULL},
        {"async void f(std.json::reader<std.io::input> r) throws std.async::start_error { o<i32> "
         "value = await std.json::read_next(&r, o::none); }",
         "R-DIAG-EFFECT-001"},
        {"void f(std.json::reader<std.io::input>* r) { task<o<i32> throws std.json::error, "
         "std.alloc::alloc_error, std.io::io_error> t = std.json::read_next(r, o::none); "
         "std.async::cancel(move t); }",
         "R-DIAG-EFFECT-001"},
        {"async void f(std.json::reader<std.io::input> r) throws std.async::start_error, "
         "std.json::error, std.alloc::alloc_error, std.io::io_error { i32 value = await "
         "std.json::read_next(&r, o::none); }",
         "R-DIAG-JSON-001"},
        {"@generic<T: json_decode> void f(const T* hint, std.json::reader<std.io::input>* r) "
         "throws "
         "std.async::start_error { task<o<T> throws std.json::error, std.alloc::alloc_error, "
         "std.io::io_error> t = std.json::read_next(r, o::none); std.async::cancel(move t); }",
         "R-DIAG-JSON-001"},
        {"@generic<T: json_decode & send & unborrowed> void f(const T* hint, "
         "std.json::reader<std.io::input>* r) throws std.async::start_error { task<o<T> throws "
         "std.json::error, std.alloc::alloc_error, std.io::io_error> t = std.json::read_next(r, "
         "o::none); std.async::cancel(move t); }",
         NULL},
        {"void f(std.json::decoder d) {}", "R-DIAG-JSON-001"},
        {"void f(std.json::decoder<void> d) {}", "R-DIAG-JSON-001"},
        {"void f() throws std.json::error, std.alloc::alloc_error { i32 n = "
         "std.json::new_decoder(); }",
         "R-DIAG-JSON-001"},
        {"void f() throws std.json::error, std.alloc::alloc_error { std.json::decoder<str> d = "
         "std.json::new_decoder(); }",
         "R-DIAG-JSON-001"},
        {"void f(const std.json::decoder<i32>* d) throws std.json::error, std.alloc::alloc_error { "
         "std.json::feed_result p = std.json::feed(d, \"1\", true); }",
         "R-DIAG-JSON-001"},
        {"void f(i32* d) throws std.json::error, std.alloc::alloc_error { i32 n = "
         "std.json::take(d); }",
         "R-DIAG-JSON-001"},
        {"void f(std.json::decoder<i32>* d) throws std.json::error, std.alloc::alloc_error { "
         "bool n = std.json::take(d); }",
         "R-DIAG-TYPE-001"},
        {"@generic<T> std.json::decoder<T> make(const T* value) throws std.json::error, "
         "std.alloc::alloc_error { std.json::decoder<T> d = std.json::new_decoder(); return move "
         "d; }",
         "R-DIAG-JSON-001"},
        {"@generic<T: json_encode> std.json::decoder<T> make(const T* value) throws "
         "std.json::error, "
         "std.alloc::alloc_error { std.json::decoder<T> d = std.json::new_decoder(); return move "
         "d; }",
         "R-DIAG-JSON-001"},
        {"@generic<T: json_decode> std.json::decoder<T> make(const T* value) throws "
         "std.json::error, "
         "std.alloc::alloc_error { std.json::decoder<T> d = std.json::new_decoder(); return move "
         "d; }",
         NULL},
        {"void f(std.json::decoder<i32> d) { std.json::decoder<i32> moved = move d; }", NULL},
        {"struct A { i32 x; }; struct B { @json(embed) A a; i32 x; };", "R-DIAG-JSON-001"},
        {"struct A { @json(embed) std.json::value x; @json(embed) std.json::value y; };",
         "R-DIAG-JSON-001"},
        {"struct A { i32 x; }; std.json::value A::json_marshal(const A* a) { return "
         "std.json::null(); } struct B { @json(embed) A a; };",
         "R-DIAG-JSON-001"},
        {"struct S { @json(name = \"x\") i32 a; @json(name = \"x\") i32 b; };", "R-DIAG-JSON-001"},
        {"struct S { @json(optional) std.json::number n; };", "R-DIAG-JSON-001"},
        {"enum E { A, }; struct S { @json(optional) E n; };", "R-DIAG-JSON-001"},
        {"struct E { i32 n; }; struct S { @json(string) E n; };", "R-DIAG-JSON-001"},
        {"@generic<T> struct S { @json(omitnone) o<T> n; };", NULL},
        {"struct S { @json(string) bool n; };", "R-DIAG-JSON-001"},
        {"struct S { @json(string) array<i32> n; };", "R-DIAG-JSON-001"},
        {"struct S { @json(omitnone) i32 n; };", "R-DIAG-JSON-001"},
        {"struct S { @json(embed) i32 n; };", "R-DIAG-JSON-001"},
        {"struct User { @json(string) u64 id; @json(name = \"display_name\", case = \"ignore\") "
         "std.string::string name; @json(optional, omitempty) std.string::string email; };",
         NULL},
        {"struct S { @json(name = \"\") i32 x; @json(name = \"-\") i32 y; };", NULL},
        {"struct S { @json(skip, default = 3u32) u32 n; };", NULL},
        {"struct S { @json(optional, default = -3) i32 n; };", NULL},
        {"struct S { @json(optional, default = true) bool n; };", NULL},
        {"struct S { @json(optional, default = \"unknown\") std.string::string n; };", NULL},
        {"struct S { @json(optional, default = factory) i32 n; }; i32 factory() { return 1; }",
         NULL},
        {"struct S { @json(optional, default = 1.25) f64 n; };", NULL},
        {"struct S { @json(optional, default = 'A') char n; };", NULL},
        {"struct S { @json(optional, default = false) i32 n; };", "R-DIAG-JSON-001"},
        {"struct S { @json(optional, default = factory) i32 n; }; bool factory() { return true; }",
         "R-DIAG-JSON-001"},
        {"struct S { @json(optional, default = factory) i32 n; }; i32 factory(i32 n) { return n; }",
         "R-DIAG-JSON-001"},
        {"struct S { @json(optional, default = factory) i32 n; }; async i32 factory() { return 1; "
         "}",
         "R-DIAG-JSON-001"},
        {"error E {}; struct S { @json(optional, default = factory) i32 n; }; i32 factory() throws "
         "E { throw E {}; }",
         "R-DIAG-JSON-001"},
        {"struct S {}; i32 S::json_marshal(const S* value) { return 1; }", "R-DIAG-JSON-001"},
        {"struct S {}; bool S::json_is_zero(S* value) { return true; }", "R-DIAG-JSON-001"},
        {"struct S {}; async bool S::json_is_zero(const S* value) { return true; }",
         "R-DIAG-JSON-001"},
        {"error E {}; struct S {}; bool S::json_is_zero(const S* value) throws E { return true; }",
         "R-DIAG-JSON-001"},
        {"struct S {}; S S::json_unmarshal(const S* value) { S result = {}; return result; }",
         "R-DIAG-JSON-001"},
        {"@generic<T> std.string::string encode(const T* value) throws std.json::error, "
         "std.alloc::alloc_error { std.string::string result = std.json::marshal(value); return "
         "move result; }",
         "R-DIAG-JSON-001"},
        {"@generic<T: copy & pod> std.string::string encode(const T* value) throws "
         "std.json::error, "
         "std.alloc::alloc_error { std.string::string result = std.json::marshal(value); return "
         "move result; }",
         "R-DIAG-JSON-001"},
        {"@generic<T: json_decode> std.string::string encode(const T* value) throws "
         "std.json::error, std.alloc::alloc_error { std.string::string result = "
         "std.json::marshal(value); return move result; }",
         "R-DIAG-JSON-001"},
        {"@generic<T: json_encode> std.string::string encode(const T* value) throws "
         "std.json::error, std.alloc::alloc_error { std.string::string result = "
         "std.json::marshal(value); return move result; }",
         NULL},
        {"@generic<T: json_encode> struct Needs { T value; }; struct S { Needs<task<i32>> value; "
         "};",
         "R-DIAG-TYPE-001"},
        {"void f() throws std.json::error, std.alloc::alloc_error { std.json::value tree = "
         "std.json::object(); o<const std.json::value*> found = std.json::find(&tree, \"x\"); "
         "std.json::value child = std.json::null(); std.json::insert(&tree, \"x\", move child); "
         "found as void; }",
         "R-DIAG-BORROW-001"},
        {"void f() throws std.json::error, std.alloc::alloc_error { std.json::value tree = "
         "std.json::object(); std.json::insert(&tree, \"x\", move tree); }",
         "R-DIAG-BORROW-001"},
        {"o<const std.json::value*> f() throws std.json::error, std.alloc::alloc_error { "
         "std.json::value tree = std.json::object(); o<const std.json::value*> found = "
         "std.json::find(&tree, \"x\"); return found; }",
         "R-DIAG-BORROW-002"},
        {"struct S { @json(omitzero, omitempty, omitnone) o<i32> n; };", NULL},
        {"struct S { @json(case = \"strict\") i32 n; };", NULL},
        {"struct S { @json(optional) @json(omitempty) i32 n; };", "R-DIAG-JSON-001"},
        {"struct S { @json(optional, optional) i32 n; };", "R-DIAG-JSON-001"},
        {"struct S { @json(omit_empty) i32 n; };", "R-DIAG-JSON-001"},
        {"struct S { @json(omit_none) o<i32> n; };", "R-DIAG-JSON-001"},
        {"struct S { @json(inline) i32 n; };", "R-DIAG-JSON-001"},
        {"struct S { @json(unknown) i32 n; };", "R-DIAG-JSON-001"},
        {"struct S { @json(format = \"x\") i32 n; };", "R-DIAG-JSON-001"},
        {"struct S { @json(default = 3) i32 n; };", "R-DIAG-JSON-001"},
        {"struct S { @json(skip, optional) i32 n; };", "R-DIAG-JSON-001"},
        {"struct S { @json(embed, omitempty) i32 n; };", "R-DIAG-JSON-001"},
        {"struct S { @json(name) i32 n; };", "R-DIAG-JSON-001"},
        {"struct S { @json(optional = true) i32 n; };", "R-DIAG-JSON-001"},
        {"struct S { @json(name = 1) i32 n; };", "R-DIAG-JSON-001"},
        {"struct S { @json(case = \"fold\") i32 n; };", "R-DIAG-JSON-001"},
        {"struct S { @json(optional, default = 1 + 2) i32 n; };", "R-DIAG-JSON-001"},
        /* M32.6: @json(description) and std.json::schema::<T>() (R-SLIB-JSON-0002). */
        {"struct S { @json(description = \"a count\", optional) i32 n; }; "
         "void f() throws std.json::error, std.alloc::alloc_error { "
         "std.json::value s = std.json::schema::<S>(); drop s; }",
         NULL},
        {"struct S { @json(skip, description = \"hidden\") i32 n = 0; };", "R-DIAG-JSON-001"},
        {"struct S { @json(description = 1) i32 n; };", "R-DIAG-JSON-001"},
        {"struct S { @json(description) i32 n; };", "R-DIAG-JSON-001"},
        {"struct S { @json(embed, description = \"x\") T n; }; struct T { i32 a; };",
         "R-DIAG-JSON-001"},
        {"struct S { i32 n; }; void f() { std.json::value s = std.json::schema::<S>(); drop s; }",
         "R-DIAG-EFFECT-001"},
        {"struct S { i32 n; }; void f() throws std.json::error, std.alloc::alloc_error { "
         "std.json::value s = std.json::schema::<S>(1); drop s; }",
         "R-DIAG-SYN-001"},
        {"@generic<T> void f() throws std.json::error, std.alloc::alloc_error { "
         "std.json::value s = std.json::schema::<T>(); drop s; }",
         "R-DIAG-JSON-001"},
        /* M38: a type parameter with a JSON constraint has the schema of each instantiation. */
        {"struct S { i32 n; }; @generic<T: json_decode> void f() throws std.json::error, "
         "std.alloc::alloc_error { std.json::value s = std.json::schema::<T>(); drop s; } "
         "void g() throws std.json::error, std.alloc::alloc_error { f::<S>(); }",
         NULL},
        {"struct S { i32 n; }; @generic<T: json_encode> void f() throws std.json::error, "
         "std.alloc::alloc_error { std.json::value s = std.json::schema::<array<T>>(); drop s; } "
         "void g() throws std.json::error, std.alloc::alloc_error { f::<S>(); }",
         NULL},
        {"struct S { i32 n; }; std.json::value S::json_marshal(const S* value) throws "
         "std.json::error, std.alloc::alloc_error { return std.json::null(); } "
         "@generic<T: json_encode> void f() throws std.json::error, std.alloc::alloc_error { "
         "std.json::value s = std.json::schema::<T>(); drop s; } "
         "void g() throws std.json::error, std.alloc::alloc_error { f::<S>(); }",
         "R-DIAG-JSON-001"},
        {"struct S { i32 n; }; std.json::value S::json_marshal(const S* value) throws "
         "std.json::error, std.alloc::alloc_error { return std.json::null(); } "
         "void f() throws std.json::error, std.alloc::alloc_error { "
         "std.json::value s = std.json::schema::<S>(); drop s; }",
         "R-DIAG-JSON-001"},
        {"struct S { i32 n; }; void f() throws std.json::error, std.alloc::alloc_error { "
         "std.json::value s = std.json::schema::<std.sync::mutex<S>>(); drop s; }",
         "R-DIAG-JSON-001"},
        /* M32.6: the json_schema hook of a type with converters. */
        {"struct S { i32 n; }; std.json::value S::json_marshal(const S* value) throws "
         "std.json::error, std.alloc::alloc_error { return std.json::null(); } "
         "std.json::value S::json_schema() throws std.json::error, std.alloc::alloc_error { "
         "return std.json::object(); } "
         "struct W { S inner; }; "
         "void f() throws std.json::error, std.alloc::alloc_error { "
         "std.json::value s = std.json::schema::<W>(); drop s; }",
         NULL},
        {"struct S { i32 n; }; std.json::value S::json_schema(i32 n) throws std.json::error, "
         "std.alloc::alloc_error { return std.json::object(); }",
         "R-DIAG-JSON-001"},
        {"struct S { i32 n; }; str S::json_schema() throws std.json::error, "
         "std.alloc::alloc_error { return \"{}\"; }",
         "R-DIAG-JSON-001"},
        {"struct S { i32 n; }; std.json::value S::json_schema() throws std.alloc::alloc_error { "
         "return std.json::null(); }",
         "R-DIAG-JSON-001"},
        {"struct S { i32 n; }; std.json::value S::json_schema() throws std.json::error, "
         "std.alloc::alloc_error { return std.json::object(); } "
         "std.json::value S::json_schema() throws std.json::error, std.alloc::alloc_error { "
         "return std.json::object(); }",
         "R-DIAG-NAME-002"}
    };
    for (size_t index = 0U; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        char text[2048];
        int length = snprintf(text, sizeof(text), "module semantic.json; %s", cases[index].body);
        RFrontendContext *context = r_frontend_create(NULL);
        R_SEMANTIC_CHECK(context != NULL && length > 0 && (size_t)length < sizeof(text));
        if (context == NULL) {
            continue;
        }
        (void)r_semantic_add_bytes(context, "json.r", (const uint8_t *)text, (size_t)length);
        RFrontendStatus status = r_frontend_analyze(context);
        if (status == R_FRONTEND_OK) {
            status = r_frontend_lower_mir(context);
        }
        bool found = cases[index].code == NULL && status == R_FRONTEND_OK &&
                     r_frontend_diagnostic_count(context) == 0U;
        for (size_t diagnostic_index = 0U; diagnostic_index < r_frontend_diagnostic_count(context);
             ++diagnostic_index) {
            const RDiagnostic *diagnostic =
                r_frontend_diagnostic(context, (uint32_t)diagnostic_index);
            found = found ||
                    (cases[index].code != NULL && strcmp(diagnostic->code, cases[index].code) == 0);
        }
        if (!found) {
            (void)fprintf(stderr,
                          "JSON case %zu failed (status %d): %s\n",
                          index,
                          (int)status,
                          cases[index].body);
        }
        if (!found) {
            for (size_t d = 0U; d < r_frontend_diagnostic_count(context); ++d) {
                const RDiagnostic *diag = r_frontend_diagnostic(context, (uint32_t)d);
                (void)fprintf(stderr, "  %s: %s\n", diag->code, diag->message);
            }
        }
        R_SEMANTIC_CHECK(found);
        r_frontend_destroy(context);
    }
}

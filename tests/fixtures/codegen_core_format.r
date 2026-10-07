module test.codegen.core_format;

/* R-TYPE-0046, R-EXPR-0028, R-AGG-0012 (L32): core::Format. Implementations and derived
   implementations of structs, enums, errors and generic types; standard formatting of scalars,
   text, options, fixed arrays, slices, arrays, tuples and network addresses; widths in Unicode
   scalar values; explicit calls on places and borrows; bounded generic code; interfaces over
   core::Format with nominal and standard members, borrowed and owned. */

protected bool matches(const std.string::string* source, const u8[] expected) {
    const u8[] actual = *source;
    usize actual_length = len(actual);
    usize expected_length = len(expected);
    if (actual_length != expected_length) { return false; }
    usize index = 0;
    while (index < actual_length) {
        if (actual[index] != expected[index]) { return false; }
        index += 1;
    }
    return true;
}

struct Point { i32 x; i32 y; };
impl core::Format for Point {
    void format(const Point* this, std.format::builder* out) throws std.alloc::alloc_error {
        std.string::string text = f"({this->x}, {this->y})";
        std.format::append_str(out, text);
    }
};

@generic<T>
struct Box { T value; };

@generic<T: core::Format>
impl core::Format for Box<T> {
    void format(const Box<T>* this, std.format::builder* out) throws std.alloc::alloc_error {
        std.format::append_str(out, "Box(");
        this->value.format(out);
        std.format::append_str(out, ")");
    }
};

enum Shape { circle(f64), square(i32), empty };
impl core::Format for Shape {
    void format(const Shape* this, std.format::builder* out) throws std.alloc::alloc_error {
        switch (*this) {
            case variant Shape::circle(r): {
                std.string::string t = f"circle {r}";
                std.format::append_str(out, t);
            }
            case variant Shape::square(s): {
                std.string::string t = f"square {s}";
                std.format::append_str(out, t);
            }
            case variant Shape::empty: { std.format::append_str(out, "empty"); }
        }
    }
};

@derive(format)
struct Size { u32 width; u32 height; };

@derive(format)
struct Nothing {};

@derive(format, equal)
enum Token { number(i64), word { str text; bool quoted; }, end };

@derive(format)
@generic<T>
struct Tagged { T item; o<T> spare; std.string::string label; };

@derive(format)
error Refused { i32 code; str reason; };

@derive(format)
@generic<T>
enum Outcome { done(T), failed { i32 code; T detail; }, pending };

@generic<T: core::Format>
std.string::string show(const T* value) throws std.alloc::alloc_error {
    return f"<{value}>";
}

@generic<T: core::Format>
void emit(const T* value, std.format::builder* out) throws std.alloc::alloc_error {
    value->format(out);
}

void through(const dyn(core::Format)* value, std.format::builder* out)
    throws std.alloc::alloc_error {
    value->format(out);
}

std.string::string joined(const (array<own dyn(core::Format)*>)* items)
    throws std.alloc::alloc_error {
    std.format::builder out = std.format::create();
    for (usize index = 0usize; index < len(*items); index += 1usize) {
        if (index != 0usize) { std.format::append_str(&out, "; "); }
        (*items)[index]->format(&out);
    }
    return std.format::finish(move out);
}

i32 user_types() throws std.alloc::alloc_error {
    Point p = Point {.x = 1, .y = -2};
    std.string::string a = f"p={p:9} q={p}";
    if (matches(&a, "p=  (1, -2) q=(1, -2)") == false) { return 1; }
    Box<Box<i32>> nested = Box<Box<i32>> {.value = Box<i32> {.value = 7}};
    std.string::string b = show(&nested);
    if (matches(&b, "<Box(Box(7))>") == false) { return 2; }
    Shape[3] shapes = {Shape::circle(1.5), Shape::square(4), Shape::empty};
    std.string::string c = f"{shapes}";
    if (matches(&c, "[circle 1.5, square 4, empty]") == false) { return 3; }
    const Point* view = &p;
    std.string::string d = f"{view}";
    if (matches(&d, "(1, -2)") == false) { return 4; }
    return 0;
}

i32 derived_types() throws std.alloc::alloc_error {
    Size size = Size {.width = 3u32, .height = 4u32};
    std.string::string a = f"{size}";
    if (matches(&a, "Size { width: 3, height: 4 }") == false) { return 11; }
    Nothing nothing = Nothing {};
    std.string::string b = f"{nothing}";
    if (matches(&b, "Nothing {}") == false) { return 12; }
    Token[3] tokens = {Token::number(-5i64), Token::word {.text = "hi", .quoted = true}, Token::end};
    std.string::string c = f"{tokens}";
    if (matches(&c, "[number(-5), word { text: hi, quoted: true }, end]") == false) { return 13; }
    Tagged<Size> tagged =
        Tagged<Size> {.item = size, .spare = o::none, .label = std.string::from_str("z")};
    std.string::string d = f"{tagged}";
    if (matches(&d, "Tagged { item: Size { width: 3, height: 4 }, spare: none, label: z }") ==
        false) {
        return 14;
    }
    Refused refused = Refused {.code = 7, .reason = "busy"};
    std.string::string e = f"{refused}";
    if (matches(&e, "Refused { code: 7, reason: busy }") == false) { return 15; }
    Outcome<i32>[3] outcomes = {
        Outcome<i32>::done(5), Outcome<i32>::failed {.code = 2, .detail = 7}, Outcome<i32>::pending};
    std.string::string f = f"{outcomes}";
    if (matches(&f, "[done(5), failed { code: 2, detail: 7 }, pending]") == false) { return 16; }
    Outcome<Nothing> empty = Outcome<Nothing>::done(Nothing {});
    std.string::string g = f"{empty}";
    if (matches(&g, "done(Nothing {})") == false) { return 17; }
    return 0;
}

i32 standard_types() throws std.alloc::alloc_error, std.net::address_error {
    o<i32> some = o::some(4);
    o<i32> none = o::none;
    std.string::string a = f"{some} {none} {some:9}|";
    if (matches(&a, "some(4) none   some(4)|") == false) { return 21; }
    (i32, str, o<Shape>) triple = (3, "x", o::some(Shape::empty));
    std.string::string b = f"{triple}";
    if (matches(&b, "(3, x, some(empty))") == false) { return 22; }
    array<i32> values = std.array::with_capacity::<i32>(2);
    try {
        values.push(1);
        values.push(-2);
    } catch (std.array::push_error<i32> failed) {
        return 23;
    }
    std.string::string c = f"{values:10}";
    if (matches(&c, "   [1, -2]") == false) { return 24; }
    const i32[] slice = values.as_slice();
    std.string::string d = show(&slice);
    if (matches(&d, "<[1, -2]>") == false) { return 25; }
    std.net::ip_address v6 = std.net::parse_ip("fe80::1");
    std.net::socket_address scoped =
        std.net::socket_address {.address = v6, .port = 8080u16, .scope_id = 3u32};
    std.net::socket_address plain6 =
        std.net::socket_address {.address = v6, .port = 443u16, .scope_id = 0u32};
    std.net::ip_address v4 = std.net::parse_ip("10.0.0.1");
    std.net::socket_address plain4 =
        std.net::socket_address {.address = v4, .port = 80u16, .scope_id = 0u32};
    std.string::string e = f"{v6} {scoped} {plain6} {plain4} {v4}";
    if (matches(&e, "fe80::1 [fe80::1%3]:8080 [fe80::1]:443 10.0.0.1:80 10.0.0.1") == false) {
        return 26;
    }
    f64 ratio = 0.25;
    std.string::string f = show(&ratio);
    if (matches(&f, "<0.25>") == false) { return 27; }
    std.string::string word = std.string::from_str("héllo");
    o<std.string::string> maybe = o::some(move word);
    std.string::string g = show(&maybe);
    if (matches(&g, "<some(héllo)>") == false) { return 28; }
    char accent = 'é';
    bool flag = true;
    str text = "ab";
    /* L32-1: a borrow of a scalar formats the value it designates. */
    i32 answer = 42;
    const i32* answer_view = &answer;
    const bool* flag_view = &flag;
    std.string::string borrowed = f"{answer_view}|{flag_view}";
    if (matches(&borrowed, "42|true") == false) { return 30; }
    std.string::string h = f"[{accent:3}][{flag:6}][{text:4}][{maybe:12}]";
    if (matches(&h, "[  é][  true][  ab][ some(héllo)]") == false) { return 29; }
    return 0;
}

i32 explicit_calls() throws std.alloc::alloc_error {
    Point p = Point {.x = 5, .y = 6};
    o<i32> maybe = o::some(9);
    Size size = Size {.width = 1u32, .height = 2u32};
    std.format::builder out = std.format::create();
    p.format(&out);
    const Point* view = &p;
    view->format(&out);
    maybe.format(&out);
    size.width.format(&out);
    emit(&p, &out);
    emit(&maybe, &out);
    std.string::string text = std.format::finish(move out);
    if (matches(&text, "(5, 6)(5, 6)some(9)1(5, 6)some(9)") == false) { return 31; }
    return 0;
}

i32 interfaces() throws std.alloc::alloc_error {
    Point p = Point {.x = 1, .y = 2};
    i32 n = 3;
    std.format::builder out = std.format::create();
    through(&p, &out);
    through(&n, &out);
    std.string::string text = std.format::finish(move out);
    if (matches(&text, "(1, 2)3") == false) { return 41; }
    array<own dyn(core::Format)*> items = std.array::create::<own dyn(core::Format)*>();
    own Size* first = new Size {.width = 8u32, .height = 9u32};
    own i32* second = new i32(7);
    own f64* third = new f64(0.5);
    try {
        std.array::push(&items, move first);
        std.array::push(&items, move second);
        std.array::push(&items, move third);
    } catch (std.array::push_error<own dyn(core::Format)*> failed) {
        return 43;
    }
    std.string::string line = joined(&items);
    if (matches(&line, "Size { width: 8, height: 9 }; 7; 0.5") == false) { return 42; }
    return 0;
}

i32 main() {
    i32 users = user_types();
    if (users != 0) { return users; }
    i32 derived = derived_types();
    if (derived != 0) { return derived; }
    i32 standard = standard_types();
    if (standard != 0) { return standard; }
    i32 explicit = explicit_calls();
    if (explicit != 0) { return explicit; }
    return interfaces();
}

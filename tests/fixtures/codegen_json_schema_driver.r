module test.codegen.json_schema_driver;
import std.console;

/* M32.6 (R-SLIB-JSON-0002): prints the JSON Schema of each type as `schema TAG JSON` and values
   of the type encoded by marshal as `value TAG JSON`; tests/json_schema_differential.py checks that
   every value validates against its schema and that altered values do not. A self-nesting type
   has a schema with $ref although marshal rejects it (R-FUNC-0004). A type with converters gives
   its schema through its json_schema hook. */

enum Color { red, green, blue };

/* A temperature written as text such as "21C", read back from it. */
struct Celsius { i32 degrees; };

std.json::value Celsius::json_marshal(const Celsius* value) throws std.json::error, std.alloc::alloc_error {
    i32 degrees = value->degrees;
    std.string::string text = f"{degrees}C";
    return std.json::from_string(text.as_bytes());
}

Celsius Celsius::json_unmarshal(const std.json::value* value) throws std.json::error, std.alloc::alloc_error {
    const u8[] text = std.json::text(value);
    throw (len(text) < 2usize || text[len(text) - 1usize] != 67u8)
        std.json::error {.code = std.json::error_code::type, .offset = 0usize, .pointer = std.string::create()};
    try {
        i32 degrees = std.convert::parse_i32(core::validate_utf8(text[0usize..len(text) - 1usize]), 10u32);
        return Celsius {.degrees = degrees};
    } catch (std.convert::parse_error rejected) {
        rejected as void;
    } catch (core::utf8_error rejected) {
        rejected as void;
    }
    throw std.json::error {.code = std.json::error_code::type, .offset = 0usize, .pointer = std.string::create()};
}

/* The schema of the converted form. */
std.json::value Celsius::json_schema() throws std.json::error, std.alloc::alloc_error {
    return std.json::parse("{\"type\":\"string\",\"minLength\":2,\"description\":\"degrees Celsius, such as 21C\"}");
}
struct Size { u32 width; u32 height; };
enum Shape { circle(f64), square, rect(Size) };
struct Node { std.string::string name; array<Node> children; };
@generic<T>
struct Pair { T left; T right; };
struct Base { std.string::string id; @json(optional) u8 revision; };
struct Extended {
    @json(embed) Base base;
    @json(description = "The title shown to people") std.string::string title;
    @json(embed) dict<std.string::string, i32> extra;
};
struct Args {
    @json(name = "query", description = "What to look for") std.string::string text;
    @json(optional, description = "How many \"results\"") u16 limit;
    @json(description = "Only exact matches") o<bool> exact;
    array<i8> weights;
    u8[3] rgb;
    Color color;
    Shape shape;
    dict<std.string::string, f32> scores;
    char initial;
    bytes blob;
    @json(skip) u32 hidden = 0u32;
    @json(string) i64 big;
    Pair<u16> range;
    Celsius outside;
};

protected std.string::string line_of(str kind, str tag, std.string::string text)
    throws std.alloc::alloc_error {
    std.string::string line = std.string::from_str(kind);
    std.string::append_str(&line, " ");
    std.string::append_str(&line, tag);
    std.string::append_str(&line, " ");
    std.string::append_str(&line, text.as_str());
    return move line;
}

protected void push_weight(array<i8>* target, i8 value) throws std.alloc::alloc_error {
    try {
        target->push(value);
    } catch (std.array::push_error<i8> failed) {
        failed as void;
        throw std.alloc::alloc_error::out_of_memory;
    }
}

protected void push_color(array<Color>* target, Color value) throws std.alloc::alloc_error {
    try {
        target->push(value);
    } catch (std.array::push_error<Color> failed) {
        failed as void;
        throw std.alloc::alloc_error::out_of_memory;
    }
}

protected void put_score(dict<std.string::string, f32>* target, str key, f32 value)
    throws std.alloc::alloc_error {
    try {
        o<f32> old = target->insert(std.string::from_str(key), value);
        old as void;
    } catch (std.dict::insert_error<std.string::string, f32> failed) {
        (move failed) as void;
        throw std.alloc::alloc_error::out_of_memory;
    }
}

protected void put_extra(dict<std.string::string, i32>* target, str key, i32 value)
    throws std.alloc::alloc_error {
    try {
        o<i32> old = target->insert(std.string::from_str(key), value);
        old as void;
    } catch (std.dict::insert_error<std.string::string, i32> failed) {
        (move failed) as void;
        throw std.alloc::alloc_error::out_of_memory;
    }
}

async void schema_of_args() throws std.error::fault, std.json::error {
    std.json::value schema = std.json::schema::<Args>();
    await std.console::println(line_of("schema", "args", std.json::stringify(&schema)));
}

async void values_of_args() throws std.error::fault, std.json::error {
    bytes blob = {};
    std.bytes::append_u8(&blob, 0u8);
    std.bytes::append_u8(&blob, 255u8);
    dict<std.string::string, f32> scores = std.dict::create::<std.string::string, f32>();
    put_score(&scores, "a", 1.5f32);
    array<i8> weights = std.array::create::<i8>();
    push_weight(&weights, -128i8);
    push_weight(&weights, 127i8);
    u8[3] bright = {1u8, 2u8, 255u8};
    u8[3] dark = {0u8, 0u8, 0u8};
    u8[3] grey = {9u8, 9u8, 9u8};
    Args first = {.text = std.string::from_str("tea \"green\""), .limit = 65535u16,
                  .exact = o::some(true), .weights = move weights, .rgb = bright,
                  .color = Color::blue, .shape = Shape::rect(Size {.width = 4u32, .height = 4294967295u32}),
                  .scores = move scores,
                  .initial = 'z', .blob = move blob, .big = -9223372036854775807i64,
                  .range = Pair<u16> {.left = 0u16, .right = 65535u16}, .outside = Celsius {.degrees = -40}};
    await std.console::println(line_of("value", "args", std.json::marshal(&first)));
    Args second = {.text = std.string::from_str(""), .limit = 0u16, .exact = o::none,
                   .weights = std.array::create::<i8>(), .rgb = dark,
                   .color = Color::red, .shape = Shape::square,
                   .scores = std.dict::create::<std.string::string, f32>(),
                   .initial = 'Ж', .blob = {}, .big = 0i64,
                   .range = Pair<u16> {.left = 7u16, .right = 8u16}, .outside = Celsius {.degrees = 21}};
    await std.console::println(line_of("value", "args", std.json::marshal(&second)));
    Args third = {.text = std.string::from_str("c"), .limit = 1u16, .exact = o::some(false),
                  .weights = std.array::create::<i8>(), .rgb = grey,
                  .color = Color::green, .shape = Shape::circle(2.5f64),
                  .scores = std.dict::create::<std.string::string, f32>(),
                  .initial = 'a', .blob = {}, .big = 1i64,
                  .range = Pair<u16> {.left = 1u16, .right = 2u16}, .outside = Celsius {.degrees = 0}};
    await std.console::println(line_of("value", "args", std.json::marshal(&third)));
}

async void schema_and_values_of_extended() throws std.error::fault, std.json::error {
    std.json::value schema = std.json::schema::<Extended>();
    await std.console::println(line_of("schema", "extended", std.json::stringify(&schema)));
    dict<std.string::string, i32> extra = std.dict::create::<std.string::string, i32>();
    put_extra(&extra, "x", -5i32);
    Extended value = {.base = Base {.id = std.string::from_str("b1"), .revision = 3u8},
                      .title = std.string::from_str("Title"), .extra = move extra};
    await std.console::println(line_of("value", "extended", std.json::marshal(&value)));
}

async void schema_of_celsius() throws std.error::fault, std.json::error {
    std.json::value schema = std.json::schema::<Celsius>();
    await std.console::println(line_of("schema", "celsius", std.json::stringify(&schema)));
    Celsius cold = {.degrees = -5};
    await std.console::println(line_of("value", "celsius", std.json::marshal(&cold)));
}

async void schema_of_node() throws std.error::fault, std.json::error {
    std.json::value schema = std.json::schema::<Node>();
    await std.console::println(line_of("schema", "node", std.json::stringify(&schema)));
    std.json::value colors = std.json::schema::<array<Color>>();
    await std.console::println(line_of("schema", "colors", std.json::stringify(&colors)));
    array<Color> palette = std.array::create::<Color>();
    push_color(&palette, Color::red);
    push_color(&palette, Color::blue);
    await std.console::println(line_of("value", "colors", std.json::marshal(&palette)));
}

async i32 main() {
    try {
        await schema_of_args();
        await values_of_args();
        await schema_and_values_of_extended();
        await schema_of_node();
        await schema_of_celsius();
    } catch (std.json::error failure) {
        (move failure) as void;
        return 1;
    }
    return 0;
}

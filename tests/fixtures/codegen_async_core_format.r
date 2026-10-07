module test.codegen.async_core_format;

/* R-TYPE-0046, R-EXPR-0028 (L32): core::Format in async functions. Rendered slots, explicit
   calls and builders live across suspension points; a bounded generic async function formats
   each instance by its own type; an interface call dispatches from an async frame. */

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

@derive(format)
struct Reading { u32 sensor; f64 value; };

@generic<T>
struct Box { T value; };

@generic<T: core::Format>
impl core::Format for Box<T> {
    void format(const Box<T>* this, std.format::builder* out) throws std.alloc::alloc_error {
        std.string::string inner = f"Box({this->value})";
        std.format::append_str(out, inner);
    }
};

async i32 tick(i32 value) { return value + 1; }

@generic<T: core::Format & send & unborrowed>
async std.string::string describe(T value) throws std.alloc::alloc_error, std.async::start_error {
    std.string::string before = f"[{value}";
    i32 step = await tick(1);
    std.string::string after = f"{before}:{step}]";
    return move after;
}

async std.string::string collect(i32 seed) throws std.alloc::alloc_error, std.async::start_error {
    Box<i32> boxed = Box<i32> {.value = seed};
    std.format::builder builder = std.format::create();
    boxed.format(&builder);
    i32 next = await tick(seed);
    (i32, bool) pair = (next, true);
    pair.format(&builder);
    const dyn(core::Format)* erased = &boxed;
    erased->format(&builder);
    return std.format::finish(move builder);
}

async i32 main() {
    Reading reading = Reading {.sensor = 1u32, .value = 20.5};
    std.string::string first = await describe(reading);
    if (matches(&first, "[Reading { sensor: 1, value: 20.5 }:2]") == false) { return 1; }
    o<u8> small = o::some(3u8);
    std.string::string second = await describe(small);
    if (matches(&second, "[some(3):2]") == false) { return 2; }
    std.string::string third = await collect(5);
    if (matches(&third, "Box(5)(6, true)Box(5)") == false) { return 3; }
    return 0;
}

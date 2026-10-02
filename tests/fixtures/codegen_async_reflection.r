module test.codegen.async_reflection;

/* R-REFL-0001..0002 inside an async frame: the runtime selections lower through MIR. */

enum Color { red, green, blue, };
enum Level : u8 { low = 1, high = 10, mid = 5, };
enum Shape { dot, circle(f32), square(i32), };

bool same(str left, str right) {
    const u8[] a = left;
    const u8[] b = right;
    usize index = 0;
    if (len(a) != len(b)) { return false; }
    while (index < len(a)) {
        if (a[index] != b[index]) { return false; }
        index += 1;
    }
    return true;
}

async i32 probe(Color color) {
    constexpr str wanted = "high";
    constexpr str name = core::enum_name(color);
    usize ordinal = core::enum_ordinal(color);
    o<Color> at = core::enum_at::<Color>(ordinal);
    at as void;
    o<Level> level = core::enum_from_name::<Level>(wanted);
    level as void;
    Shape shape = Shape::square(4);
    constexpr str shape_name = core::variant_name(&shape);
    shape_name as void;
    bool ok = same(name, "blue");

    if (ok == false) { return 1; }
    if (ordinal != 2) { return 2; }
    switch (at) {
    case variant o::some(value): if (*value != color) { return 3; } break;
    case variant o::none: return 4;
    }
    switch (level) {
    case variant o::some(value): if (*value != Level::high) { return 5; } break;
    case variant o::none: return 6;
    }
    bool ok_2 = same(shape_name, "square");
    if (ok_2 == false) { return 7; }
    return 0;
}

async i32 main() {
    try {
        try {
            task<i32> work = probe(Color::blue);
            i32 result = await move work;
            return result;
        } catch (std.async::start_error failure) {
            throw TestAssertionFailed {.code = 90};
        }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };

module test.codegen.reflection;

/* R-REFL-0001..0004: every reflection form folds at translation time or lowers to an inline
   selection; the program observes the results without any runtime metadata. */

enum Color { red, green, blue, };
enum Level : u8 { low = 1, high = 10, mid = 5, };
enum Shape { dot, circle(f32), square(i32), };
struct Point { i32 x; i32 y; };

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

@generic<T: copy>
usize count_of(T value) {
    usize count = core::enum_count::<T>();
    value as void;
    return count;
}

@generic<T: copy>
constexpr str name_of(T value) {
    constexpr str name = core::type_name::<T>();
    value as void;
    return name;
}

@generic<T: copy>
constexpr str label_of(T value) {
    constexpr str label = core::enum_name(value);
    return label;
}

i32 main() {
    try {
        Color[3] all = core::enum_variants::<Color>();
        Color green = Color::green;
        constexpr str green_name = core::enum_name(green);
        o<Color> third = core::enum_at::<Color>(2);
        o<Color> beyond = core::enum_at::<Color>(3);
        o<Level> mid = core::enum_from_name::<Level>("mid");
        o<Level> unknown = core::enum_from_name::<Level>("none");
        Shape shape = Shape::circle(2.5f32);
        constexpr str shape_name = core::variant_name(&shape);
        constexpr str array_name = core::type_name::<array<Point>>();
        constexpr str borrow_name = core::type_name::<const Level*>();
        constexpr str target = core::target_name();
        constexpr str profile = core::profile_name();
        usize generic_count = count_of(Color::red);
        constexpr str generic_name = name_of(Level::low);
        constexpr str generic_label = label_of(Level::high);
        bool ok = false;

        if (core::enum_count::<Color>() != 3) { throw TestAssertionFailed {.code = 1}; }
        if (core::enum_min::<Level>() != Level::low) { throw TestAssertionFailed {.code = 2}; }
        if (core::enum_max::<Level>() != Level::high) { throw TestAssertionFailed {.code = 3}; }
        if (all[0] != Color::red || all[2] != Color::blue) { throw TestAssertionFailed {.code = 4}; }
        bool ok_2 = same(green_name, "green");
        if (ok_2 == false) { throw TestAssertionFailed {.code = 5}; }
        if (core::enum_ordinal(green) != 1) { throw TestAssertionFailed {.code = 6}; }
        if (core::enum_ordinal(core::enum_max::<Level>()) != 1) { throw TestAssertionFailed {.code = 7}; }
        switch (third) {
        case variant o::some(value): if (*value != Color::blue) { throw TestAssertionFailed {.code = 8}; } break;
        case variant o::none: throw TestAssertionFailed {.code = 9};
        }
        switch (beyond) {
        case variant o::some(value): *value as void; throw TestAssertionFailed {.code = 10};
        case variant o::none: break;
        }
        switch (mid) {
        case variant o::some(value): if (*value != Level::mid) { throw TestAssertionFailed {.code = 11}; } break;
        case variant o::none: throw TestAssertionFailed {.code = 12};
        }
        switch (unknown) {
        case variant o::some(value): *value as void; throw TestAssertionFailed {.code = 13};
        case variant o::none: break;
        }
        if (core::variant_count::<Shape>() != 3) { throw TestAssertionFailed {.code = 14}; }
        bool ok_3 = same(shape_name, "circle");
        if (ok_3 == false) { throw TestAssertionFailed {.code = 15}; }
        if (core::field_count::<Point>() != 2) { throw TestAssertionFailed {.code = 16}; }
        bool ok_4 = same(core::field_name::<Point>(1), "y");
        if (ok_4 == false) { throw TestAssertionFailed {.code = 17}; }
        bool ok_5 = same(array_name, "array<test.codegen.reflection::Point>");
        if (ok_5 == false) { throw TestAssertionFailed {.code = 18}; }
        bool ok_6 = same(borrow_name, "const test.codegen.reflection::Level*");
        if (ok_6 == false) { throw TestAssertionFailed {.code = 19}; }
        bool ok_7 = same(target, "arm64-apple-darwin");
        if (ok_7 == false) { throw TestAssertionFailed {.code = 20}; }
        bool ok_8 = same(profile, "hosted-native-async");
        if (ok_8 == false) { throw TestAssertionFailed {.code = 21}; }
        if (generic_count != 3) { throw TestAssertionFailed {.code = 22}; }
        bool ok_9 = same(generic_name, "test.codegen.reflection::Level");
        if (ok_9 == false) { throw TestAssertionFailed {.code = 23}; }
        bool ok_10 = same(generic_label, "high");
        if (ok_10 == false) { throw TestAssertionFailed {.code = 24}; }
        /* M44.3: the place of the call, module path and line. */
        constexpr str place = core::location();
        bool ok_11 = same(place, "test.codegen.reflection:107");
        if (ok_11 == false) { throw TestAssertionFailed {.code = 25}; }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };

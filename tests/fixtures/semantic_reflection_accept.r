module test.semantic.reflection_accept;

/* R-REFL-0001..0004: every form is accepted over concrete types, and the type-operand
   constants are call-free expressions usable as call arguments and array bounds of locals. */

enum Color { red, green, blue, };
enum Shape { dot, circle(f32), };
struct Point { i32 x; i32 y; };

usize take(usize value, str name) {
    const u8[] bytes = name;
    usize total = value + len(bytes);
    return total;
}

@generic<T: copy>
constexpr str describe(T value) {
    constexpr str name = core::type_name::<T>();
    value as void;
    return name;
}

i32 main() {
    Color[3] all = core::enum_variants::<Color>();
    Color first = core::enum_min::<Color>();
    Color last = core::enum_max::<Color>();
    constexpr str name = core::enum_name(first);
    usize ordinal = core::enum_ordinal(last);
    o<Color> at = core::enum_at::<Color>(1);
    o<Color> by_name = core::enum_from_name::<Color>("green");
    Shape shape = Shape::dot;
    constexpr str variant_label = core::variant_name(&shape);
    constexpr str type_name = core::type_name::<dict<str, Point>>();
    constexpr str target = core::target_name();
    constexpr str profile = core::profile_name();
    constexpr str generic = describe(first);
    usize total = take(core::enum_count::<Color>(), core::field_name::<Point>(0));

    total += take(core::variant_count::<Shape>(), variant_label);
    total += take(core::field_count::<Point>(), name);
    if (all[ordinal] == last) { total += 1; }
    at as void;
    by_name as void;
    type_name as void;
    target as void;
    profile as void;
    generic as void;
    return total as i32;
}

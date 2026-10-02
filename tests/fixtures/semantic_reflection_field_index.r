module test.semantic.reflection_field_index;

/* R-REFL-0003: a field index at or beyond the field count is a constant violation. */

struct Point { i32 x; i32 y; };

i32 main() {
    constexpr str name = core::field_name::<Point>(2);
    name as void;
    return 0;
}

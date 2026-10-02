module test.semantic.reflection_not_enum;

/* R-REFL-0001: enum reflection over a struct type is ill-formed. */

struct Point { i32 x; i32 y; };

i32 main() {
    usize count = core::enum_count::<Point>();
    return count as i32;
}

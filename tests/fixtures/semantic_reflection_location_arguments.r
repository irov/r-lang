module test.semantic.reflection_location_arguments;

/* R-REFL-0004: core::location takes no argument. */

i32 main() {
    constexpr str place = core::location(1);
    place as void;
    return 0;
}

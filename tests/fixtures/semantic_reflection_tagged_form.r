module test.semantic.reflection_tagged_form;

/* R-REFL-0001..0002: a tagged union takes the variant forms, not the fieldless enum forms. */

enum Shape { dot, circle(f32), };

i32 main() {
    Shape shape = Shape::dot;
    constexpr str name = core::enum_name(shape);
    name as void;
    return 0;
}

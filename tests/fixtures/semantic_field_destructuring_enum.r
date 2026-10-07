module test.semantic.field_destructuring_enum;

/* R-STMT-0022: a destructuring declaration takes apart a struct or tuple. */
enum Color { red, green };

i32 main() {
    Color color = Color::red;
    auto {.red} = color;
    return 0;
}

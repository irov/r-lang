module test.semantic.field_destructuring_unknown;

/* R-STMT-0022: a destructuring declaration names fields the value has. */
struct Point { i32 x; i32 y; };

i32 main() {
    Point point = {.x = 1, .y = 2};
    auto {.x, .z} = point;
    return 0;
}

module test.semantic.field_destructuring_twice;

/* R-STMT-0022: a destructuring declaration binds a field once. */
struct Point { i32 x; i32 y; };

i32 main() {
    Point point = {.x = 1, .y = 2};
    auto {.x, .x = again} = point;
    return 0;
}

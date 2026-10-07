module test.semantic.field_destructuring_index;

/* R-STMT-0022: a numbered field selects an element of a tuple, not a field of a struct. */
struct Point { i32 x; i32 y; };

i32 main() {
    Point point = {.x = 1, .y = 2};
    auto {.0 = first} = point;
    return 0;
}

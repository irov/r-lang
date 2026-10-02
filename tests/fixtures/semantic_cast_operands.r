module test.semantic.cast_operands;

/* R-EXPR-0016: an aggregate is not a conversion operand. */
struct Point { i32 x; };

i32 main() {
    Point point = Point { .x = 1 };
    i32 value = point as i32;
    return value;
}

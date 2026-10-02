module test.regression.parenthesized_condition_operand;

/* Annex A, R-STMT-0002: a condition leaf may compare a parenthesized value operand that
   continues after its closing parenthesis; a group followed by `&&`, `||` or `)` stays a nested
   condition. */

struct Point { i32 x; i32 y; };

i32 classify(const Point* point, i32 a, i32 b) {
    if ((*point).x != 41) {
        return 1;
    }
    if (((*point).y == 2) && ((a + b) * 2 > 10)) {
        return 2;
    }
    if (((a) == 1) || ((b - a) % 3 == 0)) {
        return 3;
    }
    if ((a == 1) == (b == 2)) {
        return 4;
    }
    while ((a as i64) + 1i64 < 3i64) {
        a += 1;
    }
    return a;
}

i32 main() {
    Point point = {.x = 41, .y = 2};
    if (classify(&point, 4, 4) != 2) {
        return 1;
    }
    return 0;
}

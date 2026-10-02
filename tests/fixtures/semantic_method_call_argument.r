module test.semantic.method_call_argument;

/* R-EXPR-0020: method calls compose as ordinary value arguments. */
struct Point {
    i32 x;
};

i32 Point::amount(const Point* this) {
    return this->x;
}

i32 consume(i32 value) {
    return value;
}

i32 main() {
    Point point = Point { .x = 1 };
    i32 result = consume(point.amount());
    return result;
}

module test.semantic.method_recursion;

/* R-FUNC-0004: the static call graph including method calls shall be acyclic. */
struct Point {
    i32 x;
};

protected i32 Point::depth(const Point* this) {
    i32 nested = this->depth();
    return nested + 1;
}

i32 main() {
    Point point = Point { .x = 1 };
    i32 value = point.depth();
    return value;
}

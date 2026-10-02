module test.codegen.methods;

/* R-FUNC-0013, R-FUNC-0014: methods with an explicit receiver and associated functions. */
struct Point {
    i32 x;
    i32 y;
};

struct Bag {
    own i32* item;
};

Point Point::origin() {
    return Point { .x = 0, .y = 0 };
}

protected i32 Point::sum(const Point* this) {
    return this->x + this->y;
}

protected void Point::shift(Point* this, i32 dx, i32 dy) {
    this->x += dx;
    this->y += dy;
}

protected i32 Point::scaled(Point this, i32 factor) {
    i32 total = this.x + this.y;
    return total * factor;
}

protected i32 Bag::taken(Bag this) {
    i32 value = *this.item;
    return value;
}

i32 main() {
    Point origin = Point::origin();
    if (origin.x != 0) { return 1; }
    if (origin.y != 0) { return 2; }

    Point point = Point { .x = 1, .y = 2 };
    i32 total = point.sum();
    if (total != 3) { return 3; }

    point.shift(3, 4);
    if (point.x != 4) { return 4; }
    if (point.y != 6) { return 5; }

    i32 scaled = point.scaled(2);
    if (scaled != 20) { return 6; }
    if (point.x != 4) { return 7; }

    const Point* view = &point;
    i32 through_pointer = view->sum();
    if (through_pointer != 10) { return 8; }

    Point* mutable_view = &point;
    mutable_view->shift(1, 1);
    if (point.x != 5) { return 9; }

    i32 qualified = Point::sum(&point);
    if (qualified != 12) { return 10; }

    own i32* item = new i32(9);
    Bag bag = Bag { .item = move item };
    i32 taken = (move bag).taken();
    if (taken != 9) { return 11; }
    return 0;
}

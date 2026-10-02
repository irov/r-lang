module test.codegen.async_variant_view_moves;

/* R-STMT-0010, R-BORROW-0024: an async frame moves an exclusive borrow or a mutable slice out of
   a variant payload and ends its use before the next await. */

struct Point {
    i32 x;
    i32 y;
};

enum Target {
    Cell(i32*),
    Shape(Point*),
    Row(i32[]),
    Idle,
};

protected async i32 tick(i32 value) {
    return value + 1;
}

protected async i32 work() throws std.async::start_error {
    i32 a = 1;
    Point p = Point {.x = 2, .y = 3};
    i32[3] row = {0, 0, 0};
    i32 total = 0;
    Target first = Target::Cell(&a);
    switch (move first) {
        case variant Target::Cell(move cell):
            *cell += 5;
            break;
        default:
            return 1;
    }
    total += await tick(a);
    Target second = Target::Shape(&p);
    switch (move second) {
        case variant Target::Shape(move shape):
            shape->y += 7;
            break;
        default:
            return 2;
    }
    Target third = Target::Row(row[0usize..3usize]);
    i32* last = match (move third) {
        case variant Target::Row(move values): &values[2];
        case variant Target::Cell(move cell): cell;
        case variant Target::Shape(move shape): &shape->x;
        case variant Target::Idle: &a;
    };
    *last = 4;
    total += await tick(p.y + row[2]);
    return total - 22;
}

async i32 main() {
    try {
        return await work();
    } catch (std.async::start_error failure) {
        failure as void;
        return 90;
    }
}

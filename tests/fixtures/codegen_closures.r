module test.codegen.closures;

/* R-FUNC-0015 through R-FUNC-0017, R-TYPE-0044: local lambdas, captures and closure calls. */
struct Point {
    i32 x;
    i32 y;
};

@generic<F: fn(i32) -> i32>
i32 apply(const F* f, i32 value) {
    i32 result = f(value);
    return result;
}

@generic<F: fn(i32) -> i32>
i32 apply_twice(const F* f, i32 value) {
    i32 once = f(value);
    i32 twice = f(once);
    return twice;
}

@generic<F: fn(i32, i32) -> i32>
i32 fold3(const F* f, i32 a, i32 b, i32 c) {
    i32 ab = f(a, b);
    i32 abc = f(ab, c);
    return abc;
}

protected i32 Point::sum(const Point* this) {
    return this->x + this->y;
}

i32 main() {
    i32 offset = 10;
    i32 total = 0;
    Point point = Point { .x = 3, .y = 4 };

    fn i32 add(i32 x) { return x + offset; }
    fn void bump(i32 delta) { total += delta; }
    fn i32 plus(i32 left, i32 right) { return left + right; }
    fn i32 measure() { i32 sum = point.sum(); return sum; }

    i32 direct = add(5);
    if (direct != 15) { return 1; }

    bump(2);
    bump(3);
    if (total != 5) { return 2; }

    i32 applied = apply(&add, 7);
    if (applied != 17) { return 3; }

    i32 doubled = apply_twice(&add, 1);
    if (doubled != 21) { return 4; }

    i32 folded = fold3(&plus, 1, 2, 3);
    if (folded != 6) { return 5; }

    i32 measured = measure();
    if (measured != 7) { return 6; }

    auto seed = 4;
    fn i32 scale(i32 x) { return x * seed; }
    i32 scaled = apply(&scale, 5);
    if (scaled != 20) { return 7; }

    own i32* boxed = new i32(8);
    fn i32 unbox() move(boxed) { i32 value = *boxed; return value; }
    i32 unboxed = unbox();
    if (unboxed != 8) { return 8; }

    fn i32 outer(i32 x) { i32 inner = add(x); return inner + 1; }
    i32 nested = apply(&outer, 1);
    if (nested != 12) { return 9; }
    return 0;
}

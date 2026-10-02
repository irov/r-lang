module test.codegen.library_cmp;

import std.cmp;

struct Point { i32 x; i32 y; };

impl std.cmp::Ordered for Point {
    std.cmp::ordering cmp(const Point* this, const Point* other) {
        std.cmp::ordering first = this->x.cmp(&other->x);
        if (first != std.cmp::ordering::equal) { return first; }
        std.cmp::ordering second = this->y.cmp(&other->y);
        return second;
    }
};

@generic<T: std.cmp::Ordered>
bool sorted_pair(const T* a, const T* b) {
    bool less = std.cmp::is_less(a, b);
    return less;
}

i32 main() {
    i32 a = 3;
    i32 b = 7;
    const i32* m = std.cmp::max(&a, &b);
    if (*m != 7) { return 1; }
    const i32* n = std.cmp::min(&a, &b);
    if (*n != 3) { return 2; }
    Point p = Point { .x = 1, .y = 5 };
    Point q = Point { .x = 1, .y = 9 };
    bool ordered = sorted_pair(&p, &q);
    if (ordered == false) { return 3; }
    f64 nan = 0.0 / 0.0;
    f64 one = 1.0;
    std.cmp::ordering order = one.cmp(&nan);
    if (order != std.cmp::ordering::less) { return 4; }
    i32 c = 10;
    const i32* clamped = std.cmp::clamp(&c, &a, &b);
    if (*clamped != 7) { return 5; }
    return 0;
}

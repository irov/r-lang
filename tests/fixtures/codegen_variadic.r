module test.codegen.variadic;

/* R-FUNC-0018: homogeneous variadic parameters pack the trailing arguments into a hidden
   fixed array of the caller and receive its shared slice; a spread forwards a slice. */
struct Point {
    i32 x;
    i32 y;
};

i32 sum(i32... values) {
    i32 total = 0;
    for (const i32* v in &values) { total += *v; }
    return total;
}

i32 weighted(i32 scale, i32... values) {
    i32 total = 0;
    for (i32 v in &values) { total += v * scale; }
    return total;
}

usize count_points(Point... points) {
    usize n = 0usize;
    for (const Point* p in &points) {
        if (p->x >= 0) { n += 1usize; }
    }
    return n;
}

i32 main() {
    i32 a = sum(1, 2, 3);
    if (a != 6) { return 1; }
    i32 b = sum();
    if (b != 0) { return 2; }
    i32 c = weighted(10, 1, 2);
    if (c != 30) { return 3; }
    i32 d = weighted(5);
    if (d != 0) { return 4; }
    i32[3] fixed = {4, 5, 6};
    const i32[] view = &fixed;
    i32 e = sum(...view);
    if (e != 15) { return 5; }
    usize f = count_points(Point { .x = 1, .y = 2 }, Point { .x = -1, .y = 0 });
    if (f != 1usize) { return 6; }
    i32 seed = 7;
    i32 g = sum(seed, seed + 1);
    if (g != 15) { return 7; }
    return 0;
}

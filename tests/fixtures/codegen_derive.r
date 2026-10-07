module fixture.derive;

import std.string;
import std.slice;
import std.set;
import std.sorted;
import std.heap;

/* An identifier that starts with the prefix of generated names moves them to another prefix. */
i32 derived_total(i32 left, i32 right) { return left + right; }

@derive(clone, equal, ordered, key)
struct Point { i32 x; i32 y; };

/* Variants order by declaration, then by payload; payloads may be derived types. */
@derive(clone, equal, ordered, key)
enum Shape { dot(Point), segment { Point from; Point to; }, empty };

/* A fieldless enum orders by declaration, not by its explicit values. */
@derive(equal, ordered, key)
enum Level { high = 3, low = 1, middle = 2 };

@generic<T>
@derive(clone, equal, ordered, key)
struct Pair { T first; T second; };

/* A generic enum: variant paths name the type with its parameters. */
@generic<T>
@derive(clone, equal, ordered, key)
enum Maybe { nothing, just(T), both { T left; T right; } };

/* Constraints the type already has are kept and not repeated. */
@generic<T: key & copy>
@derive(equal, key)
struct Tagged { T value; u32 tag; };

@derive(clone, equal, ordered)
struct Record {
    std.string::string name;
    o<i32> rank;
    (i32, std.string::string) label;
    i32[3] scores;
    array<std.string::string> notes;
};

/* Views compare what they designate. */
@derive(equal, ordered)
struct View { str name; const i32[] items; };

error base_failure { i32 code; };

/* A member of an error family compares the fields of its parent first. */
@derive(clone, equal, ordered, key)
error range_failure : base_failure { i32 low; i32 high; };

@derive(equal, ordered, key)
error mode : u8 { read, write };

@derive(equal)
protected struct Hidden { i32 v; };

i32 check_points() {
    Point a = {.x = 1, .y = 2};
    Point b = {.x = 1, .y = 3};
    if (a.eq(&b) == true) { return 1; }
    if (a.cmp(&b) != std.cmp::ordering::less) { return 2; }
    if (b.cmp(&a) != std.cmp::ordering::greater) { return 3; }
    Point c = core::clone(&a);
    if (c.eq(&a) == false || core::key_equal(&c, &a) == false) { return 4; }
    if (core::hash(&c) != core::hash(&a)) { return 5; }
    if (core::hash(&a) == core::hash(&b)) { return 6; }
    return 0;
}

i32 check_shapes() {
    Point origin = {.x = 0, .y = 0};
    Point corner = {.x = 4, .y = 5};
    Shape dot = Shape::dot(origin);
    Shape line = Shape::segment {.from = origin, .to = corner};
    Shape longer = Shape::segment {.from = origin, .to = Point {.x = 4, .y = 6}};
    Shape none = Shape::empty;
    if (dot.cmp(&line) != std.cmp::ordering::less) { return 10; }
    if (line.cmp(&none) != std.cmp::ordering::less) { return 11; }
    if (line.cmp(&longer) != std.cmp::ordering::less) { return 12; }
    if (line.eq(&longer) == true) { return 13; }
    Shape copy = core::clone(&line);
    if (copy.eq(&line) == false || core::key_equal(&copy, &line) == false) { return 14; }
    if (none.eq(&none) == false) { return 15; }
    return 0;
}

i32 check_levels() {
    Level low = Level::low;
    Level high = Level::high;
    Level middle = Level::middle;
    if (high.cmp(&low) != std.cmp::ordering::less) { return 20; }
    if (low.cmp(&middle) != std.cmp::ordering::less) { return 21; }
    if (middle.eq(&middle) == false || middle.eq(&low) == true) { return 22; }
    return 0;
}

i32 check_generics() throws std.alloc::alloc_error {
    Pair<i32> p1 = {.first = 1, .second = 2};
    Pair<i32> p2 = {.first = 1, .second = 3};
    if (p1.cmp(&p2) != std.cmp::ordering::less || p1.eq(&p1) == false) { return 30; }
    if (core::key_equal(&p1, &p2) == true) { return 31; }
    Pair<Point> q1 = {.first = Point {.x = 1, .y = 1}, .second = Point {.x = 2, .y = 2}};
    Pair<Point> q2 = core::clone(&q1);
    if (q1.eq(&q2) == false || q1.cmp(&q2) != std.cmp::ordering::equal) { return 32; }
    Pair<std.string::string> names = {
        .first = std.string::from_str("ann"), .second = std.string::from_str("bob")};
    Pair<std.string::string> copied = core::clone(&names);
    if (copied.eq(&names) == false) { return 33; }
    Maybe<i32> just = Maybe<i32>::just(3);
    Maybe<i32> both = Maybe<i32>::both {.left = 1, .right = 2};
    Maybe<i32> nothing = Maybe<i32>::nothing;
    if (just.eq(&just) == false || just.eq(&both) == true) { return 35; }
    if (nothing.cmp(&just) != std.cmp::ordering::less) { return 36; }
    if (just.cmp(&both) != std.cmp::ordering::less) { return 37; }
    Maybe<i32> again = core::clone(&both);
    if (core::key_equal(&again, &both) == false || core::hash(&just) == core::hash(&both)) {
        return 38;
    }
    Tagged<u32> t1 = {.value = 7u32, .tag = 1u32};
    Tagged<u32> t2 = {.value = 7u32, .tag = 1u32};
    if (t1.eq(&t2) == false || core::key_equal(&t1, &t2) == false) { return 34; }
    return 0;
}

i32 check_records() throws std.alloc::alloc_error, std.array::push_error<std.string::string>,
    std.array::push_error<Record> {
    array<std.string::string> notes = std.array::create();
    notes.push(std.string::from_str("n"));
    Record first = {
        .name = std.string::from_str("beta"),
        .rank = o::some(2),
        .label = (1, std.string::from_str("x")),
        .scores = {1, 2, 3},
        .notes = move notes};
    Record second = core::clone(&first);
    if (second.eq(&first) == false) { return 40; }
    Record third = core::clone(&first);
    third.name = std.string::from_str("alpha");
    if (third.cmp(&first) != std.cmp::ordering::less) { return 41; }
    Record fourth = core::clone(&first);
    fourth.rank = o::none;
    if (fourth.cmp(&first) != std.cmp::ordering::less) { return 42; }
    Record fifth = core::clone(&first);
    fifth.scores[2usize] = 4;
    if (fifth.cmp(&first) != std.cmp::ordering::greater) { return 43; }
    array<Record> rows = std.array::create();
    rows.push(move first);
    rows.push(move third);
    rows.push(move fifth);
    std.slice::sort(rows[0usize..len(rows)]);
    if (std.bytes::equal(rows[0usize].name, "alpha") == false) { return 44; }
    if (rows[2usize].scores[2usize] != 4) { return 45; }
    return 0;
}

i32 check_views() {
    i32[3] numbers = {1, 2, 3};
    View left = {.name = "a", .items = numbers[0usize..2usize]};
    View right = {.name = "a", .items = numbers[0usize..3usize]};
    if (left.eq(&right) == true) { return 50; }
    if (left.cmp(&right) != std.cmp::ordering::less) { return 51; }
    return 0;
}

i32 check_errors() {
    range_failure first = {.code = 1, .low = 2, .high = 3};
    range_failure second = {.code = 1, .low = 2, .high = 4};
    if (first.eq(&second) == true || first.cmp(&second) != std.cmp::ordering::less) { return 60; }
    range_failure copy = core::clone(&first);
    if (copy.eq(&first) == false || core::key_equal(&copy, &first) == false) { return 61; }
    mode read = mode::read;
    mode write = mode::write;
    if (read.cmp(&write) != std.cmp::ordering::less || read.eq(&write) == true) { return 63; }
    Hidden hidden = {.v = 1};
    if (hidden.eq(&hidden) == false) { return 64; }
    return 0;
}

i32 check_containers() throws std.alloc::alloc_error, std.dict::insert_error<Point, i32>,
    std.dict::insert_error<Shape, i32>, std.dict::insert_error<Pair<i32>, i32>,
    std.dict::insert_error<Level, i32>, std.dict::insert_error<Tagged<u32>, i32>,
    std.dict::insert_error<range_failure, i32>, std.dict::insert_error<Point, bool>,
    std.array::push_error<Point> {
    dict<Point, i32> points = std.dict::create();
    o<i32> old = points.insert(Point {.x = 1, .y = 2}, 5);
    old as void;
    Point probe = {.x = 1, .y = 2};
    if (points.contains(&probe) == false) { return 70; }
    dict<Shape, i32> shapes = std.dict::create();
    o<i32> previous = shapes.insert(Shape::dot(probe), 1);
    previous as void;
    Shape dot = Shape::dot(probe);
    if (shapes.contains(&dot) == false) { return 71; }
    dict<Pair<i32>, i32> pairs = std.dict::create();
    o<i32> replaced = pairs.insert(Pair<i32> {.first = 1, .second = 2}, 3);
    replaced as void;
    Pair<i32> pair = {.first = 1, .second = 2};
    if (pairs.contains(&pair) == false) { return 72; }
    dict<Level, i32> levels = std.dict::create();
    o<i32> level = levels.insert(Level::middle, 2);
    level as void;
    Level middle = Level::middle;
    if (levels.contains(&middle) == false) { return 73; }
    dict<Tagged<u32>, i32> tags = std.dict::create();
    o<i32> tag = tags.insert(Tagged<u32> {.value = 1u32, .tag = 2u32}, 4);
    tag as void;
    dict<range_failure, i32> failures = std.dict::create();
    o<i32> failure = failures.insert(range_failure {.code = 1, .low = 2, .high = 3}, 1);
    failure as void;
    std.set::set<Point> seen = std.set::set<Point>::create();
    if (seen.insert(probe) == false || seen.insert(probe) == true) { return 74; }
    std.sorted::set<Point> ordered = std.sorted::set<Point>::create();
    ordered.insert(Point {.x = 3, .y = 0});
    ordered.insert(Point {.x = 1, .y = 9});
    const Point[] sorted = ordered.as_slice();
    if (sorted[0usize].x != 1) { return 75; }
    std.heap::heap<Point> heap = std.heap::heap<Point>::create();
    heap.push(Point {.x = 2, .y = 0});
    heap.push(Point {.x = 5, .y = 0});
    o<Point> top = heap.pop();
    switch (top) {
    case variant o::some(value): if (value->x != 5) { return 76; }
    case variant o::none: return 77;
    }
    if (derived_total(1, 2) != 3) { return 78; }
    return 0;
}

i32 main() {
    i32 points = check_points();
    if (points != 0) { return points; }
    i32 shapes = check_shapes();
    if (shapes != 0) { return shapes; }
    i32 levels = check_levels();
    if (levels != 0) { return levels; }
    try {
        i32 generics = check_generics();
        if (generics != 0) { return generics; }
        i32 records = check_records();
        if (records != 0) { return records; }
        i32 containers = check_containers();
        if (containers != 0) { return containers; }
    } catch (std.alloc::alloc_error failure) {
        return 90;
    } catch (std.array::push_error<std.string::string> failure) {
        return 91;
    } catch (std.array::push_error<Record> failure) {
        return 92;
    } catch (std.dict::insert_error<Point, i32> failure) {
        return 93;
    } catch (std.dict::insert_error<Shape, i32> failure) {
        return 94;
    } catch (std.dict::insert_error<Pair<i32>, i32> failure) {
        return 95;
    } catch (std.dict::insert_error<Level, i32> failure) {
        return 96;
    } catch (std.dict::insert_error<Tagged<u32>, i32> failure) {
        return 97;
    } catch (std.dict::insert_error<range_failure, i32> failure) {
        return 98;
    } catch (std.dict::insert_error<Point, bool> failure) {
        return 99;
    } catch (std.array::push_error<Point> failure) {
        return 100;
    }
    i32 views = check_views();
    if (views != 0) { return views; }
    return check_errors();
}

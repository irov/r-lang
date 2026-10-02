module test.codegen.variant_view_moves;

/* R-STMT-0010, R-BORROW-0004: a Move switch or match moves an exclusive borrow or a mutable
   slice out of a variant payload with the origins the payload carries: nullable, aggregate and
   view-holder referents, a generic enum and o<T*>, returned through the caller's region. */

struct Point {
    i32 x;
    i32 y;
};

enum Target {
    Cell(i32*?),
    Shape(Point*),
    Row(i32[]),
    Text((str)*),
    Idle,
};

enum Choice {
    Some(i32*),
    Many(i32[]),
    Nothing,
};

@generic<T>
enum Maybe {
    Has(T),
    Empty,
};

i32 apply(Target target) {
    switch (move target) {
        case variant Target::Cell(move cell):
            if (cell == null) { return -1; }
            *cell += 5;
            return *cell;
        case variant Target::Shape(move shape):
            shape->y += 7;
            return shape->x + shape->y;
        case variant Target::Row(move row):
            row[0] = 9;
            return row[0] + (len(row) as i32);
        case variant Target::Text(move text):
            *text = "moved";
            return len(*text) as i32;
        case variant Target::Idle:
            return 0;
    }
}

i32* pick(Choice choice, i32* fallback) {
    switch (move choice) {
        case variant Choice::Some(move pointer):
            return pointer;
        case variant Choice::Many(move values):
            values[0] += 1;
            return fallback;
        case variant Choice::Nothing:
            return fallback;
    }
}

i32* select(Choice choice, i32* fallback) {
    return match (move choice) {
        case variant Choice::Some(move pointer): pointer;
        case variant Choice::Many(move values): fallback;
        case variant Choice::Nothing: fallback;
    };
}

i32* unwrap(Maybe<i32*> maybe, i32* other) {
    switch (move maybe) {
        case variant Maybe<i32*>::Has(move inner):
            return inner;
        case variant Maybe<i32*>::Empty:
            return other;
    }
}

i32* first(o<i32*> maybe, i32* other) {
    switch (move maybe) {
        case variant o::some(move inner):
            return inner;
        case variant o::none:
            return other;
    }
}

i32 main() {
    i32 a = 1;
    Point p = Point {.x = 2, .y = 3};
    i32[3] row = {0, 0, 0};
    str text = "text";
    if (apply(Target::Cell(&a)) != 6) { return 1; }
    if (apply(Target::Shape(&p)) != 12) { return 2; }
    if (apply(Target::Row(row[0usize..3usize])) != 12) { return 3; }
    if (apply(Target::Text(&text)) != 5) { return 4; }
    if (len(text) != 5usize) { return 5; }
    i32 b = 20;
    i32 c = 30;
    i32* chosen = pick(Choice::Some(&b), &c);
    *chosen += 1;
    i32* other = pick(Choice::Many(row[1usize..3usize]), &c);
    *other += 1;
    i32* selected = select(Choice::Some(&a), &c);
    *selected += 10;
    i32* got = unwrap(Maybe<i32*>::Has(&b), &c);
    *got += 1;
    i32* opt = first(o::some(&c), &a);
    *opt += 2;
    if (a != 16 || b != 22 || c != 33 || p.y != 10 || row[0] != 9 || row[1] != 1) { return 6; }
    return 0;
}

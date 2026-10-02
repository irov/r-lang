module test.codegen.tagged_fallthrough;

/* R-STMT-0007 (L13.5): a clause of a switch over a tagged enum or o<T> falls through into
   a next clause that binds no payload; its own payload binding ends at the edge. */

enum Shape {
    Circle(i32),
    Square(i32),
    Empty,
    Unknown,
};

i32 classify(Shape shape) {
    i32 score = 0;
    switch (shape) {
    case variant Shape::Circle(radius):
        score += *radius;
        fallthrough;
    case variant Shape::Empty:
        score += 100;
        fallthrough;
    case variant Shape::Unknown:
        score += 1000;
        break;
    case variant Shape::Square(side):
        score += *side * 2;
        break;
    }
    return score;
}

i32 pick(o<i32> value) {
    i32 total = 0;
    switch (value) {
    case variant o::some(inner):
        total += *inner;
        fallthrough;
    case variant o::none:
        total += 5;
        break;
    }
    return total;
}

i32 main() {
    if (classify(Shape::Circle(3)) != 1103) {
        return 1;
    }
    if (classify(Shape::Empty) != 1100) {
        return 2;
    }
    if (classify(Shape::Unknown) != 1000) {
        return 3;
    }
    if (classify(Shape::Square(4)) != 8) {
        return 4;
    }
    if ((pick(o::some(7)) != 12) || (pick(o::none) != 5)) {
        return 5;
    }
    return 0;
}

module test.codegen.async_tagged_fallthrough;

/* R-STMT-0007 (L13.5): in an async frame, a clause of a switch over a tagged enum or o<T>
   falls through into a payload-free next clause. */

enum Shape {
    Circle(i32),
    Label(std.string::string),
    Empty,
};

protected async i32 classify(i32 which) throws std.alloc::alloc_error {
    Shape shape = which == 0 ? Shape::Circle(3) : Shape::Empty;
    if (which == 1) {
        drop shape;
        std.string::string text = std.string::from_str("label");
        shape = Shape::Label(move text);
    }
    i32 score = 0;
    switch (shape) {
    case variant Shape::Circle(radius):
        score += *radius;
        fallthrough;
    case variant Shape::Empty:
        score += 100;
        break;
    case variant Shape::Label(text):
        str label = *text;
        score += len(label) as i32;
        break;
    }
    o<i32> maybe = which == 0 ? o::some(7) : o::none;
    switch (maybe) {
    case variant o::some(inner):
        score += *inner;
        fallthrough;
    case variant o::none:
        score += 1000;
        break;
    }
    return score;
}

async i32 main() {
    try {
        if (await classify(0) != 1110) {
            return 1;
        }
        if (await classify(1) != 1005) {
            return 2;
        }
        if (await classify(2) != 1100) {
            return 3;
        }
    } catch (std.async::start_error failure) {
        failure as void;
        return 9;
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 8;
    }
    return 0;
}

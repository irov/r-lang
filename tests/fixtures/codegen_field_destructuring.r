module test.codegen.field_destructuring;

/* R-STMT-0022 (L40): `auto {.field = name, .field} = value;` moves or copies the named fields of
   a struct or tuple into new locals; a field it does not name is dropped at the end of the
   block, and a Copy place stays usable. */

struct Holder {
    std.string::string name;
    arc i32 shared;
    i32 tag;
};

struct Point {
    i32 x;
    i32 y;
};

i32 run() throws std.alloc::alloc_error {
    arc i32 counted = new arc i32(5);
    {
        Holder holder = {.name = std.string::from_str("holder"), .shared = std.arc::clone(&counted), .tag = 7};
        if (std.arc::strong_count(&counted) != 2usize) { return 1; }
        auto {.name = label, .tag} = move holder;
        if (label.len() != 6usize || tag != 7) { return 2; }
        /* The field that is not named still holds its owner until the end of the block. */
        if (std.arc::strong_count(&counted) != 2usize) { return 3; }
    }
    if (std.arc::strong_count(&counted) != 1usize) { return 4; }

    Point point = {.x = 1, .y = 2};
    auto {.x, .y = height} = point;
    if (x != 1 || height != 2 || point.x + point.y != 3) { return 5; }

    (i32, std.string::string) pair = (3, std.string::from_str("b"));
    auto {.1 = text, .0 = number} = move pair;
    if (number != 3 || text.len() != 1usize) { return 6; }

    Holder whole = {.name = std.string::from_str("whole"), .shared = std.arc::clone(&counted), .tag = 9};
    auto {.shared = owner, .name, .tag = mark} = move whole;
    if (*owner != 5 || name.len() != 5usize || mark != 9) { return 7; }
    if (std.arc::strong_count(&counted) != 2usize) { return 8; }
    drop owner;
    if (std.arc::strong_count(&counted) != 1usize) { return 9; }
    return 0;
}

i32 main() {
    try {
        return run();
    } catch (std.alloc::alloc_error failure) {
        failure as void;
    }
    return 99;
}

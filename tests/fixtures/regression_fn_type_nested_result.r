module test.regression.fn_type_nested_result;

/* M32-1: the field of holder names a function type whose result, outcome, carries a structure
   declared later in the order of collection, result_value; checking the function type collects
   both before it looks at them. */
struct holder { fn(u32) -> outcome throws(std.alloc::alloc_error) make; };

enum outcome { found(result_value), missing };

struct result_value { std.string::string text; u32 size; };

outcome make_outcome(u32 size) throws std.alloc::alloc_error {
    if (size == 0u32) { return outcome::missing; }
    return outcome::found(result_value {.text = std.string::from_str("made"), .size = size});
}

i32 main() {
    holder chosen = {.make = make_outcome};
    outcome made = chosen.make(3u32);
    switch (move made) {
    case variant outcome::found(move value):
        if (value.size == 3u32 && std.string::len(&value.text) == 4usize) { return 0; }
    case variant outcome::missing: break;
    }
    return 1;
}

module test.semantic.field_destructuring_move;

/* R-STMT-0022, R-OWN-0003: a place of a Move type is consumed only with move. */
struct Named { std.string::string name; };

i32 run() throws std.alloc::alloc_error {
    Named named = {.name = std.string::from_str("n")};
    auto {.name} = named;
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

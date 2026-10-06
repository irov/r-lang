module test.semantic.string_view_chain;

/* R-EXPR-0015: the string edges are not chained, so a string place is not a byte slice. */
i32 run() throws std.alloc::alloc_error {
    std.string::string word = std.string::from_str("word");
    const u8[] bytes = word;
    return len(bytes) as i32;
}

i32 main() {
    try {
        return run();
    } catch (std.alloc::alloc_error failure) {
        failure as void;
    }
    return 99;
}

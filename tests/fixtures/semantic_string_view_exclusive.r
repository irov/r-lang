module test.semantic.string_view_exclusive;

/* R-EXPR-0015: a string place views only as shared bytes, never as an exclusive byte slice. */
usize clear(u8[] data) { return len(data); }

i32 run() throws std.alloc::alloc_error {
    std.string::string word = std.string::from_str("word");
    return clear(word) as i32;
}

i32 main() {
    try {
        return run();
    } catch (std.alloc::alloc_error failure) {
        failure as void;
    }
    return 99;
}

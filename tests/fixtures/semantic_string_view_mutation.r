module test.semantic.string_view_mutation;

/* R-EXPR-0015: the view borrows its place, which cannot be mutated while the view is live. */
i32 run() throws std.alloc::alloc_error {
    std.string::string word = std.string::from_str("word");
    str view = word;
    word.append("s");
    return len(view) as i32;
}

i32 main() {
    try {
        return run();
    } catch (std.alloc::alloc_error failure) {
        failure as void;
    }
    return 99;
}

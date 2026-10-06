module test.semantic.string_view_move;

/* R-EXPR-0015: the view borrows its place, which cannot be moved while the view is live. */
void consume(std.string::string text) { drop text; }

i32 run() throws std.alloc::alloc_error {
    std.string::string word = std.string::from_str("word");
    str view = word;
    consume(move word);
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

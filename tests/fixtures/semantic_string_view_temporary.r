module test.semantic.string_view_temporary;

/* R-EXPR-0015: only a place of type std.string::string converts to str. */
usize measure(str text) { return len(text); }

i32 run() throws std.alloc::alloc_error {
    return measure(std.string::from_str("temporary")) as i32;
}

i32 main() {
    try {
        return run();
    } catch (std.alloc::alloc_error failure) {
        failure as void;
    }
    return 99;
}

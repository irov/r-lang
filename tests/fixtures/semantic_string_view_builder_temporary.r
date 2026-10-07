module test.semantic.string_view_builder_temporary;

/* R-EXPR-0015: only a place of a builder converts; a builder that is no place has no owner to
   borrow. */
usize count_bytes(const u8[] data) { return len(data); }

std.format::builder heading() throws std.alloc::alloc_error {
    std.format::builder builder = std.format::create();
    builder.append("heading");
    return move builder;
}

i32 run() throws std.alloc::alloc_error {
    return count_bytes(heading()) as i32;
}

i32 main() {
    try {
        return run();
    } catch (std.alloc::alloc_error failure) {
        failure as void;
    }
    return 99;
}

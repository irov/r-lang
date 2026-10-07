module test.semantic.string_view_builder_mutation;

/* R-EXPR-0015: the byte view borrows its builder, which cannot be appended to while the view is
   live. */
i32 run() throws std.alloc::alloc_error {
    std.format::builder builder = std.format::create();
    builder.append("word");
    const u8[] view = builder;
    builder.append("s");
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

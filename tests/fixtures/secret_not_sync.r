module test.semantic.secret_not_sync;

/* R-SLIB-SECRET-0001: buffer is Send but not Sync, so a shared borrow is not Send. */
async usize observe(const std.secret::buffer* source) {
    return std.secret::len(source);
}

i32 main() {
    return 0;
}

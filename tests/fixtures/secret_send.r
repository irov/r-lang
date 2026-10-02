module test.semantic.secret_send;

/* R-SLIB-SECRET-0001: an owned buffer is Send and may cross an async frame boundary. */
async usize consume(std.secret::buffer owned) {
    usize length = std.secret::len(&owned);
    drop owned;
    return length;
}

i32 main() {
    return 0;
}

module test.semantic.consteval_argument;

/* R-EXPR-0032: a required constant needs constant arguments. */
usize twice(usize value) {
    return value * 2usize;
}

void fill(usize count) {
    u8[twice(count)] bytes = {};
    bytes as void;
}

i32 main() {
    fill(3usize);
    return 0;
}

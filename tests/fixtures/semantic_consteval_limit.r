module test.semantic.consteval_limit;

/* R-EXPR-0032: a required constant whose evaluation exceeds the translation limits. */
usize spin(usize rounds) {
    usize total = 0usize;
    for (usize i = 0usize; i < rounds; i++) {
        total += 1usize;
    }
    return total;
}

i32 main() {
    u8[spin(100000000usize)] bytes = {};
    bytes as void;
    return 0;
}

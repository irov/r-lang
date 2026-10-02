module test.semantic.consteval_runtime_panic;

/* R-EXPR-0032: outside a required constant a call that would panic stays a run-time call. */
usize divide(usize value, usize by) {
    return value / by;
}

i32 main() {
    usize half = divide(8usize, 2usize);
    if (half == 5usize) {
        usize broken = divide(1usize, 0usize);
        broken as void;
    }
    return 0;
}

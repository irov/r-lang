module test.semantic.consteval_panic;

/* R-EXPR-0032: a panic while evaluating a required constant is a translation error. */
usize checked_capacity(usize requested) {
    if (requested == 0usize) {
        panic("capacity out of range");
    }
    return requested;
}

const usize CAPACITY = checked_capacity(0usize);

i32 main() {
    return 0;
}

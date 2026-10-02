module test.semantic.stack_recursive_direct;

/* R-FUNC-0004: a function that calls itself has no static stack bound. */
protected i32 countdown(i32 value) {
    if (value == 0) {
        return 0;
    }
    i32 next = value - 1;
    i32 rest = countdown(next);
    return rest + 1;
}

i32 main() {
    i32 outcome = countdown(5);
    return outcome - 5;
}

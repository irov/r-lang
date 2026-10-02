module test.semantic.deny_panic_alloc_misplaced;

@deny_panic_alloc
i32 helper() {
    return 0;
}

i32 main() {
    i32 outcome = helper();
    return outcome;
}

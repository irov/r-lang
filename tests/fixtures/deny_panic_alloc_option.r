module test.semantic.deny_panic_alloc_option;

i32 main() {
    own i32* boxed = new i32(1);
    i32 value = *boxed;
    drop boxed;
    return value - 1;
}

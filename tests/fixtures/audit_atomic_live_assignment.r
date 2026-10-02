module test.audit.atomic_live_assignment;

void replace_live_atomic() {
    au32 value = 0;
    value = 1;
}

i32 main() {
    replace_live_atomic();
    return 0;
}

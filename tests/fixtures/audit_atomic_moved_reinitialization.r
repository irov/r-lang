module test.audit.atomic_moved_reinitialization;

void reinitialize_moved_atomic() {
    au32 source = 0;
    au32 destination = move source;
    source = 1;
    au32 second = move source;
    move destination as void;
    move second as void;
}

i32 main() {
    reinitialize_moved_atomic();
    return 0;
}

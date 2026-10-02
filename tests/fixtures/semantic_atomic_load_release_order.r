module test.semantic.atomic_load_release_order;

u32 read() {
    au32 value = 1;
    return core::atomic_load(&value, core::memory_order::release);
}

i32 main() {
    return 0;
}

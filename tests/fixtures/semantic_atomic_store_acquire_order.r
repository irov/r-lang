module test.semantic.atomic_store_acquire_order;

void write() {
    au32 value = 1;
    core::atomic_store(&value, 2u32, core::memory_order::acquire);
}

i32 main() {
    return 0;
}

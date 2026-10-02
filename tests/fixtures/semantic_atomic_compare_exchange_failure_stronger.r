module test.semantic.atomic_compare_exchange_failure_stronger;

void update() {
    au32 value = 1;
    core::atomic_compare_exchange(
        &value,
        1u32,
        2u32,
        core::memory_order::relaxed,
        core::memory_order::acquire) as void;
}

i32 main() {
    return 0;
}

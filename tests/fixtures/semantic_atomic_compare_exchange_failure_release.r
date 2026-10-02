module test.semantic.atomic_compare_exchange_failure_release;

void update() {
    au32 value = 1;
    core::atomic_compare_exchange(
        &value,
        1u32,
        2u32,
        core::memory_order::seq_cst,
        core::memory_order::release) as void;
}

i32 main() {
    return 0;
}

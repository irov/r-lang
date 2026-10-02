module test.codegen.core_atomic_fetch_add_overflow;

i32 main() {
    ai32 value = 2147483647i32;
    core::atomic_fetch_add(&value, 1i32, core::memory_order::seq_cst) as void;
    return 1;
}

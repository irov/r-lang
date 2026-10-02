module test.codegen.core_atomic_invalid_order;

u32 read(core::memory_order order) {
    au32 value = 7;
    return core::atomic_load(&value, order);
}

i32 main() {
    read(core::memory_order::release) as void;
    return 1;
}

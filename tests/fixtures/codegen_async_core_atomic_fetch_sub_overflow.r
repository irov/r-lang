module test.codegen.async_core_atomic_fetch_sub_overflow;

async i32 main() {
    i32 minimum = core::wrapping_add_i32(2147483647i32, 1i32);
    ai32 value = minimum;
    core::atomic_fetch_sub(&value, 1i32, core::memory_order::acq_rel) as void;
    return 1;
}

module test.codegen.core_atomic_intrinsics;

i32 main() {
    au32 value = 5;
    if (core::atomic_load(&value, core::memory_order::relaxed) != 5u32) { return 1; }
    core::atomic_store(&value, 7u32, core::memory_order::release);
    if (core::atomic_exchange(&value, 9u32, core::memory_order::acq_rel) != 7u32) { return 2; }
    if (core::atomic_fetch_add(&value, 3u32, core::memory_order::seq_cst) != 9u32) { return 3; }
    if (core::atomic_fetch_sub(&value, 2u32, core::memory_order::relaxed) != 12u32) { return 4; }
    if (core::atomic_fetch_and(&value, 7u32, core::memory_order::relaxed) != 10u32) { return 5; }
    if (core::atomic_fetch_or(&value, 8u32, core::memory_order::relaxed) != 2u32) { return 6; }
    if (core::atomic_fetch_xor(&value, 3u32, core::memory_order::relaxed) != 10u32) { return 7; }
    core::atomic_is_lock_free(&value) as void;
    if (core::atomic_load(&value, core::memory_order::acquire) != 9u32) { return 8; }
    core::atomic_compare_exchange_result<u32> exchanged = core::atomic_compare_exchange(
        &value, 9u32, 15u32, core::memory_order::acq_rel, core::memory_order::acquire);
    switch (move exchanged) {
        case variant core::atomic_compare_exchange_result::exchanged(move observed):
            if (observed != 9u32) { return 9; }
            break;
        case variant core::atomic_compare_exchange_result::unchanged(move observed):
            observed as void;
            return 10;
    }
    core::atomic_compare_exchange_result<u32> unchanged = core::atomic_compare_exchange(
        &value, 9u32, 20u32, core::memory_order::seq_cst, core::memory_order::seq_cst);
    switch (move unchanged) {
        case variant core::atomic_compare_exchange_result::exchanged(move observed):
            observed as void;
            return 11;
        case variant core::atomic_compare_exchange_result::unchanged(move observed):
            if (observed != 15u32) { return 12; }
            break;
    }
    if (core::atomic_load(&value, core::memory_order::relaxed) != 15u32) { return 13; }
    au8 wrapping = 255u8;
    if (core::atomic_fetch_add(&wrapping, 1u8, core::memory_order::relaxed) != 255u8) {
        return 14;
    }
    if (core::atomic_load(&wrapping, core::memory_order::relaxed) != 0u8) { return 15; }
    return 0;
}

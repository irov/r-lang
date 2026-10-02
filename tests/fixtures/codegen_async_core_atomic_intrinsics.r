module test.codegen.async_core_atomic_intrinsics;

async i32 main() {
    ai64 value = -1i64;
    core::atomic_store(&value, 41i64, core::memory_order::release);
    if (core::atomic_fetch_add(&value, 1i64, core::memory_order::acq_rel) != 41i64) { return 1; }
    if (core::atomic_load(&value, core::memory_order::acquire) != 42i64) { return 2; }
    core::atomic_compare_exchange_result<i64> exchanged = core::atomic_compare_exchange(
        &value, 42i64, 50i64, core::memory_order::release, core::memory_order::relaxed);
    switch (move exchanged) {
        case variant core::atomic_compare_exchange_result::exchanged(move observed):
            if (observed != 42i64) { return 3; }
            break;
        case variant core::atomic_compare_exchange_result::unchanged(move observed):
            observed as void;
            return 4;
    }
    core::atomic_compare_exchange_result<i64> unchanged = core::atomic_compare_exchange(
        &value, 42i64, 60i64, core::memory_order::acquire, core::memory_order::acquire);
    switch (move unchanged) {
        case variant core::atomic_compare_exchange_result::exchanged(move observed):
            observed as void;
            return 5;
        case variant core::atomic_compare_exchange_result::unchanged(move observed):
            if (observed != 50i64) { return 6; }
            break;
    }
    if (core::atomic_load(&value, core::memory_order::relaxed) != 50i64) { return 7; }
    ai16 decreasing = -5i16;
    if (core::atomic_fetch_sub(&decreasing, 3i16, core::memory_order::acquire) != -5i16) {
        return 8;
    }
    if (core::atomic_load(&decreasing, core::memory_order::relaxed) != -8i16) { return 9; }
    return 0;
}

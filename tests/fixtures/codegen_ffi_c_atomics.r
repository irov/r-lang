module test.codegen.ffi_c_atomics;

/* R-FFI-0017 (B12): C _Atomic members of a header-declared struct, proven against the header and
   updated in place by R through core atomics and by C through the same storage. */
@link(name = "atomics", kind = "static")
@header("c_atomics.h")
@abi("atomics-abi")
extern "C" {
    @repr(C)
    @c_type(name = "atomic_counters", kind = "typedef")
    struct Counters {
        atomic c_uint64 hits;
        atomic c_int level;
        atomic raw void*? slot;
        atomic c_size total;
        c_uchar tag;
    };

    @safety("COUNTERS-INITIALIZE", "The pointer refers to live storage of the struct")
    void atomic_counters_initialize(raw Counters* counters, c_uchar tag);
    @safety("COUNTERS-BUMP", "The pointer refers to a live value")
    c_uint64 atomic_counters_bump(raw Counters* counters, c_uint64 amount);
    @safety("COUNTERS-LEVEL", "The pointer refers to a live value")
    c_int atomic_counters_level(raw const Counters* counters);
    @safety("COUNTERS-SLOT", "The pointer refers to a live value")
    raw void*? atomic_counters_slot(raw const Counters* counters);
}

i32 main() {
    Counters counters = Counters { .hits = 5u64 as c_uint64, .level = 0i32 as c_int, .slot = null,
                                   .total = 0usize as c_size, .tag = 1u8 as c_uchar };
    i32 marker = 7;

    if (sizeof(Counters) != 40usize || alignof(Counters) != 8usize) {
        return 1;
    }
    c_uint64 before =
        core::atomic_fetch_add(&counters.hits, 3u64 as c_uint64, core::memory_order::relaxed);
    core::atomic_store(&counters.level, -4i32 as c_int, core::memory_order::release);
    if (before != 5u64 as c_uint64) {
        return 2;
    }
    unsafe {
        raw void* address = (&marker as raw i32*) as raw void*;
        raw Counters* pointer = &counters as raw Counters*;

        core::atomic_store(&pointer->slot, address as raw void*?, core::memory_order::release);
        if (atomic_counters_bump(pointer, 10u64 as c_uint64) != 8u64 as c_uint64) {
            return 3;
        }
        if (atomic_counters_level(pointer as raw const Counters*) != -4i32 as c_int) {
            return 4;
        }
        if (atomic_counters_slot(pointer as raw const Counters*) != address as raw void*?) {
            return 5;
        }
    }
    if (core::atomic_load(&counters.hits, core::memory_order::acquire) != 18u64 as c_uint64 ||
        core::atomic_load(&counters.total, core::memory_order::acquire) != 10usize as c_size) {
        return 6;
    }
    core::atomic_compare_exchange_result<c_uint64> exchanged = core::atomic_compare_exchange(
        &counters.hits, 18u64 as c_uint64, 20u64 as c_uint64, core::memory_order::acq_rel,
        core::memory_order::acquire);
    switch (move exchanged) {
        case variant core::atomic_compare_exchange_result::exchanged(move observed):
            if (observed != 18u64 as c_uint64) { return 7; }
            break;
        case variant core::atomic_compare_exchange_result::unchanged(move observed):
            observed as void;
            return 8;
    }
    unsafe {
        raw Counters* pointer = &counters as raw Counters*;

        atomic_counters_initialize(pointer, 9u8 as c_uchar);
        if (core::atomic_load(&pointer->hits, core::memory_order::relaxed) != 0u64 as c_uint64 ||
            core::atomic_load(&pointer->slot, core::memory_order::relaxed) != null ||
            pointer->tag != 9u8 as c_uchar) {
            return 9;
        }
    }
    return 0;
}

module test.codegen.async_ffi_c_atomics;

/* R-FFI-0017 (B12): C _Atomic members of a header-declared struct held in an async frame across
   awaits, updated by R through core atomics and by C through the same storage. */
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

protected async c_uint64 settle(c_uint64 value) {
    return value;
}

protected async i32 run() throws std.async::start_error {
    Counters counters = Counters { .hits = 5u64 as c_uint64, .level = 0i32 as c_int, .slot = null,
                                   .total = 0usize as c_size, .tag = 1u8 as c_uchar };
    c_uint64 amount = await settle(3u64 as c_uint64);
    c_uint64 before = core::atomic_fetch_add(&counters.hits, amount, core::memory_order::relaxed);
    if (before != 5u64 as c_uint64) {
        return 1;
    }
    c_uint64 bump = await settle(10u64 as c_uint64);
    unsafe {
        if (atomic_counters_bump(&counters as raw Counters*, bump) != 8u64 as c_uint64) {
            return 2;
        }
    }
    core::atomic_store(&counters.level, -4i32 as c_int, core::memory_order::release);
    c_uint64 expected = await settle(18u64 as c_uint64);
    if (core::atomic_load(&counters.hits, core::memory_order::acquire) != expected ||
        core::atomic_load(&counters.total, core::memory_order::acquire) != 10usize as c_size) {
        return 3;
    }
    unsafe {
        if (atomic_counters_level(&counters as raw const Counters*) != -4i32 as c_int) {
            return 4;
        }
        atomic_counters_initialize(&counters as raw Counters*, 9u8 as c_uchar);
    }
    c_uint64 zero = await settle(0u64 as c_uint64);
    if (core::atomic_load(&counters.hits, core::memory_order::relaxed) != zero ||
        counters.tag != 9u8 as c_uchar) {
        return 5;
    }
    unsafe {
        if (core::atomic_load(&counters.slot, core::memory_order::relaxed) != null) {
            return 6;
        }
    }
    return 0;
}

async i32 main() {
    try {
        return await run();
    } catch (std.async::start_error failure) {
        failure as void;
        return 9;
    }
}

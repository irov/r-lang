module test.codegen.async_pointer_operations;

/* Library R-LIB-0028, Core R-UNSAFE-0008, R-TYPE-0013 (B12): the pointer operations, the C errno
   and atomic C integers and function pointers in async functions. An async body may resume on
   another worker, so errno is read before the next await, never across one. */

struct Pair {
    u32 first;
    u64 second;
};

atomic c_int level = 0i32 as c_int;
atomic raw fn?(c_int) -> c_int handler = null;

@callback
@safety("ASYNC-POINTER-OPERATIONS-TWICE", "None")
extern "C" c_int async_pointer_operations_twice(c_int value) {
    return value * (2i32 as c_int);
}

protected async usize settle(usize value) {
    return value;
}

protected async i32 run() throws std.async::start_error {
    Pair pair = {.first = 7u32, .second = 9u64};
    usize distance = await settle(8usize);
    unsafe {
        raw const Pair* whole = &pair as raw const Pair*;
        raw const void* second = core::offset_bytes(whole, distance);
        if (core::pointer_address(second) != core::pointer_address(whole) + distance) {
            return 1;
        }
        raw u64* writable = core::cast_mutable(second as raw const u64*);
        *writable = 12u64;
        raw const void* back = core::offset_bytes(second, 0isize - (distance as isize));
        if (core::pointer_address(back) != core::pointer_address(whole)) {
            return 2;
        }
    }
    bool again = await settle(distance) == 8usize;
    if ((pair.second != 12u64) || (again == false)) {
        return 3;
    }
    core::set_c_errno(61i32 as c_int);
    bool observed = core::c_errno() == 61i32 as c_int;
    bool later = await settle(1usize) == 1usize;
    if ((observed == false) || (later == false)) {
        return 4;
    }
    bool old = core::atomic_fetch_add(&level, 2i32 as c_int, core::memory_order::relaxed) ==
               0i32 as c_int;
    bool step = await settle(2usize) == 2usize;
    if ((old == false) || (step == false) ||
        (core::atomic_load(&level, core::memory_order::acquire) != 2i32 as c_int)) {
        return 5;
    }
    core::atomic_store(&handler, async_pointer_operations_twice, core::memory_order::release);
    bool last = await settle(3usize) == 3usize;
    raw fn?(c_int) -> c_int loaded = core::atomic_load(&handler, core::memory_order::acquire);
    unsafe {
        if ((loaded == null) || (last == false)) {
            return 6;
        }
        if (loaded(5i32 as c_int) != 10i32 as c_int) {
            return 7;
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

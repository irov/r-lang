module test.codegen.pointer_operations;

/* Library R-LIB-0028, Core R-UNSAFE-0008, R-TYPE-0013 (B12): the address of a raw pointer, byte
   offsets forward and back, the mutable view of a shared raw pointer, the C errno of each thread,
   atomic C integers and an atomic raw function pointer. */

struct Pair {
    u32 first;
    u64 second;
};

atomic c_int level = 0i32 as c_int;
atomic raw fn?(c_int) -> c_int handler = null;

@callback
@safety("POINTER-OPERATIONS-TWICE", "None")
extern "C" c_int pointer_operations_twice(c_int value) {
    return value * (2i32 as c_int);
}

i32 offsets() {
    Pair pair = {.first = 7u32, .second = 9u64};
    unsafe {
        raw const Pair* whole = &pair as raw const Pair*;
        usize base = core::pointer_address(whole);
        raw const void* second = core::offset_bytes(whole, 8usize);
        if (core::pointer_address(second) != base + 8usize) {
            return 1;
        }
        raw const u64* value = second as raw const u64*;
        if (*value != 9u64) {
            return 2;
        }
        raw const void* back = core::offset_bytes(second, -8isize);
        if (core::pointer_address(back) != base) {
            return 3;
        }
        raw u64* writable = core::cast_mutable(second as raw const u64*);
        *writable = 11u64;
    }
    if (pair.second != 11u64) {
        return 4;
    }
    raw const u8*? nothing = null;
    if (core::pointer_address(nothing) != 0usize) {
        return 5;
    }
    return 0;
}

void set_in_thread(c_int value) {
    core::set_c_errno(value);
}

i32 errno_values() throws std.thread::thread_error, std.alloc::alloc_error {
    core::set_c_errno(34i32 as c_int);
    thread_scope {
        std.thread::scoped_join_handle<void> other = std.thread::spawn_scoped(set_in_thread, 7i32 as c_int);
        move other as void;
    }
    if (core::c_errno() != 34i32 as c_int) {
        return 10;
    }
    core::set_c_errno(0i32 as c_int);
    if (core::c_errno() != 0i32 as c_int) {
        return 11;
    }
    return 0;
}

i32 atomics() {
    bool old_seen = core::atomic_fetch_add(&level, 5i32 as c_int, core::memory_order::relaxed) ==
                    0i32 as c_int;
    bool now_seen = core::atomic_load(&level, core::memory_order::acquire) == 5i32 as c_int;
    if ((old_seen == false) || (now_seen == false)) {
        return 20;
    }
    atomic c_size total = 3usize as c_size;
    bool before_seen = core::atomic_fetch_or(&total, 4usize as c_size,
                                             core::memory_order::acq_rel) == 3usize as c_size;
    bool after_seen = core::atomic_load(&total, core::memory_order::relaxed) == 7usize as c_size;
    if ((before_seen == false) || (after_seen == false)) {
        return 21;
    }
    core::atomic_store(&handler, pointer_operations_twice, core::memory_order::release);
    raw fn?(c_int) -> c_int loaded = core::atomic_load(&handler, core::memory_order::acquire);
    unsafe {
        if (loaded == null) {
            return 22;
        }
        if (loaded(21i32 as c_int) != 42i32 as c_int) {
            return 23;
        }
    }
    return 0;
}

i32 main() {
    i32 moved = offsets();
    if (moved != 0) {
        return moved;
    }
    try {
        i32 status = errno_values();
        if (status != 0) {
            return status;
        }
    } catch (std.thread::thread_error failure) {
        return 12;
    } catch (std.alloc::alloc_error failure) {
        return 13;
    }
    return atomics();
}

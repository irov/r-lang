module test.codegen.unwind_skips_rest;

/* Core R-ERR-0005 (B12): a panic leaves every frame at the call that began it, so no statement
   after that call runs, in the function that panicked or in its callers, before the boundary of
   the thread takes the panic. The test after each call first reads the number of unwinding
   threads (r_runtime_unwinding_threads), so this also proves that the runtime counts the panic. */

atomic u32 reached = 0u32;

protected u8 pick(const u8[] values, usize at) {
    u8 value = values[at];
    core::atomic_store(&reached, 1u32, core::memory_order::relaxed);
    return value;
}

/* The recursion keeps real calls between the panic and its callers: the optimizer inlines no
   recursive call, so each caller tests the unwinding state after the call returns. */
@recursion(depth = 8)
protected u8 descend(u32 remaining, const u8[] values, usize at) throws core::recursion_error {
    if (remaining == 0u32) {
        return pick(values, at);
    }
    u8 value = descend(remaining - 1u32, values, at);
    core::atomic_store(&reached, 4u32, core::memory_order::relaxed);
    return value;
}

protected u8 deep(const u8[] values, usize at) {
    try {
        u8 value = descend(3u32, values, at);
        core::atomic_store(&reached, 5u32, core::memory_order::relaxed);
        return value;
    } catch (core::recursion_error failure) {
        failure as void;
        return 0u8;
    }
}

protected u32 middle(usize at) {
    u8[3] values = {1u8, 2u8, 3u8};
    u8 picked = deep(values[0usize..3usize], at);
    core::atomic_store(&reached, 2u32, core::memory_order::relaxed);
    return picked as u32;
}

protected u32 outer(usize at) {
    u32 value = middle(at);
    core::atomic_store(&reached, 3u32, core::memory_order::relaxed);
    return value;
}

i32 run() throws std.thread::thread_error {
    std.thread::join_handle<u32> fine = std.thread::spawn(outer, 1usize);
    std.thread::join_result<u32> fine_result = std.thread::join(move fine);
    switch (move fine_result) {
    case variant std.thread::join_result::returned(move value):
        if ((value != 2u32) ||
            (core::atomic_load(&reached, core::memory_order::relaxed) != 3u32)) {
            return 1;
        }
        break;
    case variant std.thread::join_result::panicked(move report):
        drop report;
        return 2;
    }
    core::atomic_store(&reached, 0u32, core::memory_order::relaxed);
    std.thread::join_handle<u32> broken = std.thread::spawn(outer, 7usize);
    std.thread::join_result<u32> broken_result = std.thread::join(move broken);
    switch (move broken_result) {
    case variant std.thread::join_result::returned(move value):
        value as void;
        return 3;
    case variant std.thread::join_result::panicked(move report):
        drop report;
        break;
    }
    if (core::atomic_load(&reached, core::memory_order::relaxed) != 0u32) {
        return 4;
    }
    return 0;
}

i32 main() {
    try {
        return run();
    } catch (std.thread::thread_error failure) {
        failure as void;
        return 9;
    }
}

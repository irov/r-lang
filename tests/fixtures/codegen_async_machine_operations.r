module test.codegen.async_machine_operations;

/* Library R-LIB-0028, Core R-UNSAFE-0008 (B11): the machine operations in async functions. A
   scoped thread hands a value over through fences to the async frame; fence orders, a table and
   the measured stack pointer live across awaits; prefetches walk the table; core::unreachable
   ends an async path the logic excludes. An async body may resume on another worker, so only the
   alignment of the stack pointer is compared across an await. */

atomic u64 message = 0u64;
atomic u32 published = 0u32;

void produce(u64 value) {
    core::atomic_store(&message, value, core::memory_order::relaxed);
    core::atomic_fence(core::memory_order::release);
    core::atomic_store(&published, 1u32, core::memory_order::relaxed);
}

protected async u32 tick(u32 value) {
    return value + 1u32;
}

protected async i32 handoff() throws std.thread::thread_error, std.async::start_error {
    u64 received = 0u64;
    u32 before = await tick(1u32);
    thread_scope {
        std.thread::scoped_join_handle<void> producer = std.thread::spawn_scoped(produce, 77u64);
        while (core::atomic_load(&published, core::memory_order::relaxed) == 0u32) {
        }
        core::atomic_fence(core::memory_order::acquire);
        received += core::atomic_load(&message, core::memory_order::relaxed);
        move producer as void;
    }
    u32 after = await tick(before);
    if (after != 3u32) {
        return 10;
    }
    if (received != 77u64) {
        return 13;
    }
    return 0;
}

protected async i32 orders() throws std.async::start_error {
    core::memory_order[4] all = {core::memory_order::acquire,
                                 core::memory_order::release,
                                 core::memory_order::acq_rel,
                                 core::memory_order::seq_cst};
    u32 count = 0u32;
    for (usize index = 0usize; index < 4usize; index += 1usize) {
        count = await tick(count);
        core::memory_order order = all[index];
        core::atomic_fence(order);
        core::signal_fence(order);
    }
    if (count != 4u32) {
        return 20;
    }
    return 0;
}

protected async i32 stack() throws std.async::start_error {
    usize sp = core::stack_pointer();
    if ((sp == 0usize) || (sp % 16usize != 0usize)) {
        return 30;
    }
    u32 resumed = await tick(0u32);
    if (resumed != 1u32) {
        return 31;
    }
    usize again = core::stack_pointer();
    usize frame = core::frame_address();
    if ((frame < again) || (again % 16usize != 0usize)) {
        return 32;
    }
    return 0;
}

protected async i32 prefetches() throws std.async::start_error {
    u64[32] table = {};
    for (usize index = 0usize; index < 32usize; index += 1usize) {
        table[index] = (index * 2usize) as u64;
    }
    u32 step = await tick(0u32);
    u64 sum = 0u64;
    for (usize index = 0usize; index < 24usize; index += 1usize) {
        unsafe {
            core::prefetch_read(&table[index + 8usize] as raw const u64*);
            core::prefetch_write(&table[index] as raw u64*);
        }
        sum += table[index];
    }
    if (step != 1u32) {
        return 41;
    }
    if (sum != 552u64) {
        return 40;
    }
    return 0;
}

protected async u32 parity(u32 value) throws std.async::start_error {
    u32 low = await tick(value & 1u32);
    if (low == 1u32) {
        return 0u32;
    }
    if (low == 2u32) {
        return 1u32;
    }
    unsafe {
        core::unreachable();
    }
}

async i32 main() {
    try {
        i32 status = await handoff();
        if (status != 0) {
            return status;
        }
        i32 ordered = await orders();
        if (ordered != 0) {
            return ordered;
        }
        i32 measured = await stack();
        if (measured != 0) {
            return measured;
        }
        i32 fetched = await prefetches();
        if (fetched != 0) {
            return fetched;
        }
        if (((await parity(7u32)) != 1u32) || ((await parity(10u32)) != 0u32)) {
            return 50;
        }
        return 0;
    } catch (std.thread::thread_error failure) {
        return 11;
    } catch (std.async::start_error failure) {
        return 12;
    }
}

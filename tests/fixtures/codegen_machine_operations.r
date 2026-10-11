module test.codegen.machine_operations;

/* Library R-LIB-0028, Core R-UNSAFE-0008 (B11): the machine operations of core. A release fence
   before a relaxed store and an acquire fence after a relaxed load hand a value over between two
   threads; a fence takes its order at run time; the stack pointer and the frame record are
   aligned, ordered and close together; prefetches walk a table without changing it; and
   core::unreachable ends a path the logic before it excludes. The handed value is a relaxed
   atomic: ThreadSanitizer does not model fences and would report a plain one (compiler/README.md,
   *Machine operations*). */

atomic u64 message = 0u64;
atomic u32 published = 0u32;

void produce(u64 value) {
    core::atomic_store(&message, value, core::memory_order::relaxed);
    core::atomic_fence(core::memory_order::release);
    core::atomic_store(&published, 1u32, core::memory_order::relaxed);
}

u64 consume() {
    while (core::atomic_load(&published, core::memory_order::relaxed) == 0u32) {
    }
    core::atomic_fence(core::memory_order::acquire);
    return core::atomic_load(&message, core::memory_order::relaxed);
}

i32 handoff() throws std.thread::thread_error, std.alloc::alloc_error {
    u64 received = 0u64;
    thread_scope {
        std.thread::scoped_join_handle<void> producer = std.thread::spawn_scoped(produce, 41u64);
        received += consume();
        move producer as void;
    }
    if (received != 41u64) {
        return 10;
    }
    return 0;
}

void fence_with(core::memory_order order) {
    core::atomic_fence(order);
    core::signal_fence(order);
}

i32 orders() {
    core::memory_order[4] all = {core::memory_order::acquire,
                                 core::memory_order::release,
                                 core::memory_order::acq_rel,
                                 core::memory_order::seq_cst};
    for (const core::memory_order* order in &all) {
        fence_with(*order);
    }
    core::signal_fence(core::memory_order::seq_cst);
    return 0;
}

usize inner_pointer(u8[] scratch) {
    usize sum = 0usize;
    for (usize index = 0usize; index < len(scratch); index += 1usize) {
        scratch[index] = index as u8;
        sum += scratch[index] as usize;
    }
    if (sum == 0usize) {
        return 0usize;
    }
    return core::stack_pointer();
}

i32 stack() {
    usize sp = core::stack_pointer();
    if ((sp == 0usize) || (sp % 16usize != 0usize)) {
        return 20;
    }
    usize frame = core::frame_address();
    if ((frame % 8usize != 0usize) || (frame < sp) || (frame - sp > 65536usize)) {
        return 21;
    }
    u8[256] scratch = {};
    usize inner = inner_pointer(scratch[0..256]);
    if ((inner == 0usize) || (inner > sp) || (sp - inner > 65536usize)) {
        return 22;
    }
    return 0;
}

i32 prefetches() {
    u64[64] table = {};
    for (usize index = 0usize; index < 64usize; index += 1usize) {
        table[index] = index as u64;
    }
    u64 sum = 0u64;
    for (usize index = 0usize; index < 56usize; index += 1usize) {
        unsafe {
            core::prefetch_read(&table[index + 8usize] as raw const u64*);
            core::prefetch_write(&table[index] as raw u64*);
        }
        sum += table[index];
    }
    if (sum != 1540u64) {
        return 30;
    }
    return 0;
}

u32 parity(u32 value) {
    u32 low = value & 1u32;
    if (low == 0u32) {
        return 0u32;
    }
    if (low == 1u32) {
        return 1u32;
    }
    unsafe {
        core::unreachable();
    }
}

i32 main() {
    try {
        i32 status = handoff();
        if (status != 0) {
            return status;
        }
    } catch (std.thread::thread_error failure) {
        return 11;
    } catch (std.alloc::alloc_error failure) {
        return 12;
    }
    i32 ordered = orders();
    if (ordered != 0) {
        return ordered;
    }
    i32 measured = stack();
    if (measured != 0) {
        return measured;
    }
    i32 fetched = prefetches();
    if (fetched != 0) {
        return fetched;
    }
    if ((parity(7u32) != 1u32) || (parity(10u32) != 0u32)) {
        return 40;
    }
    return 0;
}

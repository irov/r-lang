module example.registers.machine;

// A one-slot mailbox between two threads: the fences order the relaxed accesses of the value
// with those of the state word (0 empty, 1 full).
atomic u64 mailbox_value = 0u64;
atomic u32 mailbox_state = 0u32;

void produce(u32 count) {
    for (u32 index = 1u32; index <= count; index += 1u32) {
        while (core::atomic_load(&mailbox_state, core::memory_order::relaxed) != 0u32) {
        }
        // The consumer has taken the previous value before this one replaces it.
        core::atomic_fence(core::memory_order::acquire);
        core::atomic_store(&mailbox_value, (index as u64) * 3u64, core::memory_order::relaxed);
        core::atomic_fence(core::memory_order::release);
        core::atomic_store(&mailbox_state, 1u32, core::memory_order::relaxed);
    }
}

u64 consume(u32 count) {
    u64 sum = 0u64;
    for (u32 index = 0u32; index < count; index += 1u32) {
        while (core::atomic_load(&mailbox_state, core::memory_order::relaxed) != 1u32) {
        }
        core::atomic_fence(core::memory_order::acquire);
        sum += core::atomic_load(&mailbox_value, core::memory_order::relaxed);
        core::atomic_fence(core::memory_order::release);
        core::atomic_store(&mailbox_state, 0u32, core::memory_order::relaxed);
    }
    return sum;
}

std.string::string handoff(u32 count) throws std.thread::thread_error, std.alloc::alloc_error {
    u64 sum = 0u64;
    thread_scope {
        std.thread::scoped_join_handle<void> producer = std.thread::spawn_scoped(produce, count);
        sum += consume(count);
        move producer as void;
    }
    return f"messages={count} sum={sum}\n";
}

// Each level records whether the stack pointer of the level below it is lower; the comparison
// after the call keeps the recursion from becoming a loop.
@recursion(depth = 256)
bool descends(u32 remaining, usize above) throws core::recursion_error {
    usize here = core::stack_pointer();
    if ((here >= above) || (here % 16usize != 0usize)) {
        return false;
    }
    if (remaining == 0u32) {
        return true;
    }
    bool deeper = descends(remaining - 1u32, here);
    return deeper && (core::stack_pointer() == here);
}

std.string::string stack(u32 depth) throws core::recursion_error, std.alloc::alloc_error {
    usize top = core::stack_pointer();
    usize frame = core::frame_address();
    bool framed = frame >= top;
    bool grows_down = descends(depth, top);
    return f"depth={depth} grows_down={grows_down} frame_above_stack={framed}\n";
}

// A simulated register block in static storage and its address map: the initializer of the map
// holds address constants, so the pointers are part of the program image.
struct Device {
    u32 control;
    u32 status;
    u32 data;
    u32 scratch;
};

Device device = {.control = 0u32, .status = 1u32, .data = 0u32, .scratch = 0u32};
(raw u32*)[4] register_map = {&device.control as raw u32*, &device.status as raw u32*,
                              &device.data as raw u32*, &device.scratch as raw u32*};

// The two top bits of a command word select the register.
usize register_of(u32 word) {
    usize index = (word >> 30u32) as usize;
    if (index < 4usize) {
        return index;
    }
    unsafe {
        core::unreachable();
    }
}

std.string::string mapped(const u32[] words) throws std.alloc::alloc_error {
    str[4] names = {"control", "status", "data", "scratch"};
    std.string::string output = std.string::create();
    for (const u32* word in &words) {
        usize index = register_of(*word);
        u32 value = *word & 0x3fffffffu32;
        str name = names[index];
        unsafe {
            raw u32* address = register_map[index];
            core::prefetch_write(address);
            u32 before = core::volatile_load(address as raw const u32*);
            core::volatile_store(address, value);
            u32 after = core::volatile_load(address as raw const u32*);
            std.string::string row = f"{name} before={before} after={after}\n";
            output.append(row);
        }
    }
    u32[4] current = {};
    for (usize index = 0usize; index < 4usize; index += 1usize) {
        unsafe {
            raw const u32* address = register_map[index] as raw const u32*;
            core::prefetch_read(address);
            current[index] = core::volatile_load(address);
        }
    }
    u32 control = current[0];
    u32 status = current[1];
    u32 data = current[2];
    u32 scratch = current[3];
    std.string::string last = f"control={control} status={status} data={data} scratch={scratch}\n";
    output.append(last);
    return move output;
}

// A watchdog that halts the process when a running total passes its limit: core::trap ends it at
// once with the trap signal, without unwinding or output.
std.string::string watchdog(u32 limit, const u32[] values) throws std.alloc::alloc_error {
    u64 total = 0u64;
    for (const u32* value in &values) {
        total += *value as u64;
        if (total > limit as u64) {
            core::trap();
        }
    }
    return f"total={total} limit={limit}\n";
}

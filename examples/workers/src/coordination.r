module example.workers.coordination;

@noalloc @nonblocking
u64 sum(const u32[] values) {
    u64 total = 0u64;
    for (const u32* value in &values) { total += *value as u64; }
    return total;
}

@generic<F: fn once() -> u64 & send & unborrowed>
async u64 execute(F operation) {
    u64 result = (move operation).call();
    return result;
}

// The reporting stage owns the pending calculation and consumes it before formatting.
async std.string::string report_sum(task<u64> calculation) throws std.alloc::alloc_error {
    u64 total = await move calculation;
    return f"sum={total}\n";
}

std.string::string scoped(const u32[] values) throws std.thread::thread_error, std.alloc::alloc_error {
    u64 total = 0u64;
    usize middle = len(values) / 2usize;
    usize end = len(values);
    thread_scope {
        std.thread::scoped_join_handle<u64> left = std.thread::spawn_scoped(sum, values[0usize..middle]);
        std.thread::scoped_join_handle<u64> right = std.thread::spawn_scoped(sum, values[middle..end]);
        std.thread::join_result<u64> first = (move left).join();
        std.thread::join_result<u64> second = (move right).join();
        switch (move first) {
        case variant std.thread::join_result::returned(move amount): total += amount; break;
        case variant std.thread::join_result::panicked(move report): drop second; return std.string::from_str("left worker panicked\n");
        }
        switch (move second) {
        case variant std.thread::join_result::returned(move amount): total += amount; break;
        case variant std.thread::join_result::panicked(move report): return std.string::from_str("right worker panicked\n");
        }
    }
    return f"sum={total}\n";
}

struct Signal { au32 ready; };

void signal(std.thread::thread parent, arc Signal ready, u64 delay) {
    std.thread::sleep_nanoseconds(delay);
    core::atomic_store(&ready->ready, 1u32, core::memory_order::release);
    parent.unpark();
}

std.string::string parked(u32 milliseconds) throws std.thread::thread_error, std.alloc::alloc_error {
    std.thread::thread current = std.thread::current();
    std.thread::thread target = current.clone();
    arc Signal state = new arc Signal { .ready = 0u32 };
    arc Signal child_state = state.clone();
    u64 delay = milliseconds as u64;
    delay *= 1000000u64;
    std.thread::join_handle<void> worker = std.thread::spawn(signal, move target, move child_state, delay);
    while (core::atomic_load(&state->ready, core::memory_order::acquire) == 0u32) { std.thread::park(); }
    std.thread::join_result<void> completion = (move worker).join();
    switch (move completion) {
    case variant std.thread::join_result::completed: break;
    case variant std.thread::join_result::panicked(move report): return std.string::from_str("signaller panicked\n");
    }
    return std.string::from_str("ready\n");
}

module test.codegen.group_detach;

/* R-STMT-0017 (L24): a detached member stays supervised: it frees its slot when it is terminal,
   `await group.all()` waits for it, and the group exit cancels and drains it if it still runs. */

struct Counter { atomic u32 runs; atomic u32 cleanups; };

async void count(arc Counter counter) {
    const Counter* shared = &*counter;
    core::atomic_fetch_add(&shared->runs, 1u32, core::memory_order::relaxed) as void;
}

async void sleeper(arc Counter counter) throws std.time::time_error, std.async::start_error {
    try {
        await std.time::sleep_for(std.time::duration_from_seconds(30i64));
    } finally {
        const Counter* shared = &*counter;
        core::atomic_fetch_add(&shared->cleanups, 1u32, core::memory_order::relaxed) as void;
    }
}

async i32 main() {
    arc Counter counter = new arc Counter {.runs = 0u32, .cleanups = 0u32};
    // A detached member frees its slot when it is terminal.
    task_scope(1) group {
        for (i32 index = 0; index < 3; index += 1) {
            auto member = count(std.arc::clone(&counter));
            std.async::detach(move member);
            await group.all();
        }
    }
    // The group exit cancels and drains a detached member that still runs.
    task_scope(2) group {
        auto member = sleeper(std.arc::clone(&counter));
        std.async::detach(move member);
        await std.time::sleep_for(std.time::duration_from_parts(0i64, 100000000u32));
    }
    const Counter* shared = &*counter;
    u32 runs = core::atomic_load(&shared->runs, core::memory_order::relaxed);
    u32 cleanups = core::atomic_load(&shared->cleanups, core::memory_order::relaxed);
    i32 status = 0;
    if (runs != 3u32) { status += 1; }
    if (cleanups != 1u32) { status += 2; }
    return status;
}

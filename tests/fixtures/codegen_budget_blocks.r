module test.budget_blocks;
import std.alloc;

/* Core R-STMT-0020: budget blocks. Allocations of the block are charged to its budget and
   refused with alloc_error::budget_exhausted beyond its byte limit; a release returns the bytes;
   a nested budget narrows its parent and the parent the nested one; the task limit refuses a
   start with start_error::budget_exhausted; a started task inherits the budget, and a return
   from the block leaves it. */

/* 0 when the bytes were allocated, 1 when the budget refused them, 2 for another failure. */
protected i32 try_bytes(usize count) {
    try {
        array<u8> data = std.alloc::bytes(count, 1u8);
        drop data;
        return 0;
    } catch (std.alloc::alloc_error failure) {
        if (failure == std.alloc::alloc_error::budget_exhausted) { return 1; }
    }
    return 2;
}

/* 600 bytes fit a budget of 1000 twice in turn, but not at once. */
protected async i32 bytes_in_turn() throws std.error::fault {
    i32 code = 0;
    budget (std.alloc::limits {.bytes = o::some(1000usize)}) {
        array<u8> first = std.alloc::bytes(600usize, 1u8);
        i32 together = try_bytes(600usize);
        if (together != 1) { code = 10 + together; }
        drop first;
        i32 alone = try_bytes(600usize);
        if (alone != 0) { code = 13 + alone; }
    }
    return code;
}

/* The nested budget refuses what its parent would admit, and the parent what the nested one
   would admit. */
protected async i32 nested() throws std.error::fault {
    i32 code = 0;
    budget (std.alloc::limits {.bytes = o::some(1000usize)}) {
        array<u8> held = std.alloc::bytes(900usize, 1u8);
        budget (std.alloc::limits {.bytes = o::some(200usize)}) {
            i32 large = try_bytes(250usize);
            if (large != 1) { code = 20 + large; }
            array<u8> small = std.alloc::bytes(50usize, 3u8);
            i32 more = try_bytes(60usize);
            if (more != 1) { code = 23 + more; }
            drop small;
        }
        drop held;
    }
    return code;
}

protected async u32 work(u32 value) { return value + 1u32; }

/* One task fits a task limit of one; a second start beside it is refused, also when the first
   has ended but nothing has awaited it yet (M42-3). */
protected async i32 tasks() throws std.error::fault {
    i32 code = 0;
    budget (std.alloc::limits {.tasks = o::some(1usize)}) {
        task_scope(2) group {
            auto first = work(1u32);
            await std.time::sleep_for(std.time::duration_from_parts(0i64, 20000000u32));
            bool refused = false;
            try {
                auto second = work(2u32);
                u32 other = await move second;
                other as void;
            } catch (std.async::start_error failure) {
                refused = failure == std.async::start_error::budget_exhausted;
            }
            if (refused == false) { code = 30; }
            u32 value = await move first;
            if (value != 2u32) { code = 31; }
        }
    }
    return code;
}

protected async u32 pause(u32 value) throws std.error::fault {
    await std.time::sleep_for(std.time::duration_from_parts(0i64, 1000u32));
    return value + 1u32;
}

/* A task counts until its await completes: starts that each follow the await of the one before
   never exceed a task limit of one, also when the task ends on another thread than the one that
   awaits it (M42-3). */
protected async i32 sequential() throws std.error::fault {
    i32 code = 0;
    for (u32 round = 0u32; round < 2000u32; round += 1u32) {
        budget (std.alloc::limits {.tasks = o::some(1usize)}) {
            try {
                u32 first = await pause(round);
                u32 second = await pause(first);
                if (second != round + 2u32) { code = 60; }
            } catch (std.async::start_error failure) {
                if (failure == std.async::start_error::budget_exhausted) { code = 61; }
            }
        }
    }
    return code;
}

/* A task started in the block allocates under the budget of the block. */
protected async i32 allocate(usize count) { return try_bytes(count); }

protected async i32 inherited() throws std.error::fault {
    budget (std.alloc::limits {.bytes = o::some(4096usize)}) {
        i32 inside = await allocate(100000usize);
        if (inside != 1) { return 40 + inside; }
    }
    return 0;
}

async i32 main() {
    i32 first = await bytes_in_turn();
    if (first != 0) { return first; }
    i32 second = await nested();
    if (second != 0) { return second; }
    i32 third = await tasks();
    if (third != 0) { return third; }
    i32 ordered = await sequential();
    if (ordered != 0) { return ordered; }
    i32 fourth = await inherited();
    if (fourth != 0) { return fourth; }
    i32 outside = try_bytes(100000usize);
    if (outside != 0) { return 50 + outside; }
    return 0;
}

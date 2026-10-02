module bench.async_task_start;

/* One awaited async call per iteration: two-phase start, frame reservation, publication. */
async u32 step(u32 accumulator, u32 value) {
    return (accumulator ^ value) * 31u32 + 7u32;
}

async i32 main() {
    usize iterations = 2_000_000usize;
    u32 accumulator = 0u32;
    u32 value = 0u32;
    for (usize index = 0usize; index < iterations; index += 1usize) {
        accumulator = await step(accumulator, value);
        value += 1u32;
    }
    return (accumulator % 109u32) as i32;
}

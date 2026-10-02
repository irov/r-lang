module bench.async_scoped_start;

/* One awaited @scoped call per iteration inside one task group: reservation, bind, loan. */
@scoped
async u32 step(const u32* accumulator, u32 value) throws std.async::start_error {
    return (*accumulator ^ value) * 31u32 + 7u32;
}

async i32 main() {
    usize iterations = 2_000_000usize;
    u32 accumulator = 0u32;
    u32 value = 0u32;
    task_scope(1) group {
        for (usize index = 0usize; index < iterations; index += 1usize) {
            accumulator = await step(&accumulator, value);
            value += 1u32;
        }
    }
    return (accumulator % 109u32) as i32;
}

module bench.call_overhead;

/* One tiny R call per iteration; the generated C enters through the stack preflight. */
protected u32 step(u32 accumulator, u32 value) {
    return (accumulator ^ value) * 31u32 + 7u32;
}

i32 main() {
    usize iterations = 200_000_000usize;
    u32 accumulator = 0u32;
    u32 value = 0u32;
    for (usize index = 0usize; index < iterations; index += 1usize) {
        accumulator = step(accumulator, value);
        value += 1u32;
    }
    return (accumulator % 109u32) as i32;
}

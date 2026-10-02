module bench.index_fixed;

/* Bounds-checked indexing of a fixed array inside a hot loop. */
i32 main() {
    usize count = 4096usize;
    usize rounds = 50_000usize;
    u32[4096] data = {};
    u32 state = 12345u32;
    for (usize index = 0usize; index < count; index += 1usize) {
        state = state * 1664525u32 + 1013904223u32;
        data[index] = state;
    }
    u32 accumulator = 0u32;
    for (usize round = 0usize; round < rounds; round += 1usize) {
        for (usize index = 0usize; index < count; index += 1usize) {
            accumulator = accumulator * 3u32 + data[index];
        }
    }
    return (accumulator % 109u32) as i32;
}

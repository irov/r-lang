module bench.index_slice;

/* Bounds-checked indexing through a slice parameter, the shape of a typical R helper. */
protected u32 fold(const u32[] values, u32 seed) {
    u32 accumulator = seed;
    for (usize index = 0usize; index < len(values); index += 1usize) {
        accumulator = accumulator * 3u32 + values[index];
    }
    return accumulator;
}

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
        accumulator = fold(data, accumulator);
    }
    return (accumulator % 109u32) as i32;
}

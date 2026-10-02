module bench.array_push;

/* Append 50 000 000 elements to a growable array, then fold them through a slice. */
i32 main() {
    try {
        usize iterations = 50_000_000usize;
        array<u32> values = std.array::create::<u32>();
        for (usize index = 0usize; index < iterations; index += 1usize) {
            values.push((index as u32) * 2654435761u32);
        }
        const u32[] view = values.as_slice();
        u32 total = 0u32;
        for (usize index = 0usize; index < len(view); index += 1usize) {
            total ^= view[index];
        }
        return (total % 109u32) as i32;
    } catch (std.array::push_error<u32> failure) { return 71; }
}

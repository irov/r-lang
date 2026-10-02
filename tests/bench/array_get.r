module bench.array_get;

/* Bounds-checked element access: 200 000 000 `std.array::get` lookups in a 4096-element array,
   each observed through the option. */
i32 main() {
    try {
        array<u32> values = std.array::create::<u32>();
        for (usize index = 0usize; index < 4096usize; index += 1usize) {
            values.push((index as u32) * 2654435761u32);
        }
        usize iterations = 200_000_000usize;
        u32 total = 0u32;
        for (usize index = 0usize; index < iterations; index += 1usize) {
            o<const u32*> item = std.array::get(&values, index & 4095usize);
            switch (item) {
                case variant o::some(value): total ^= **value + (index as u32); break;
                case variant o::none: return 70;
            }
        }
        drop values;
        return (total % 109u32) as i32;
    } catch (std.array::push_error<u32> failure) { return 71; }
}

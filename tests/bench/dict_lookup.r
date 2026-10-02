module bench.dict_lookup;

/* 65 536 inserts into dict(i32, i32), then 50 000 000 lookups by pseudo-random present keys. */
i32 main() {
    try {
        dict<i32, i32> table = std.dict::create::<i32, i32>();
        for (i32 key = 0; key < 65536; key += 1) {
            o<i32> previous = std.dict::insert(&table, key * 7919, key);
            previous as void;
        }
        usize iterations = 50_000_000usize;
        u64 total = 0u64;
        u64 state = 12345u64;
        for (usize index = 0usize; index < iterations; index += 1usize) {
            state = (state * 1664525u64 + 1013904223u64) & 0xffffffffu64;
            i32 key = (((state >> 16u64) & 65535u64) as i32) * 7919;
            o<const i32*> found = std.dict::get(&table, &key);
            switch (found) {
                case variant o::some(value): total += (**value) as u64; break;
                case variant o::none: return 70;
            }
        }
        drop table;
        return (total % 109u64) as i32;
    } catch (std.dict::insert_error<i32, i32> failure) { return 71; }
}

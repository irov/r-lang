module bench.dict_string;

/* dict<string, u32>: 4096 keys "key-<n>", then 10 000 000 lookups by key text. */
i32 main() {
    dict<std.string::string, u32> table = std.dict::create::<std.string::string, u32>();
    array<std.string::string> keys = std.array::with_capacity::<std.string::string>(4096usize);
    for (u32 i = 0u32; i < 4096u32; i += 1u32) {
        std.string::string key = f"key-{i}";
        std.string::string stored = core::clone(&key);
        try { std.dict::insert(&table, move stored, i * 3u32) as void; }
        catch (std.dict::insert_error<std.string::string, u32> failure) { move failure as void; }
        try { std.array::push(&keys, move key); }
        catch (std.array::push_error<std.string::string> failure) { move failure as void; }
    }
    u32 sum = 0u32;
    const std.string::string[] view = std.array::as_slice(&keys);
    for (usize n = 0usize; n < 10_000_000usize; n += 1usize) {
        const std.string::string* key = &view[(n * 7usize) & 4095usize];
        switch (std.dict::get(&table, key)) {
        case variant o::some(value): sum += **value; break;
        case variant o::none: sum += 1u32; break;
        }
    }
    drop table;
    drop keys;
    return (sum % 109u32) as i32;
}

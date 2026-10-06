module example.tokens.ids;
import std.cmp;
import std.uuid;

/* tokens id v4|v7 COUNT: fresh identifiers, one per line. */
std.string::string fresh(bool timed, u32 count) throws std.time::time_error, std.alloc::alloc_error {
    std.string::string out = std.string::create();
    for (u32 index = 0u32; index < count; index += 1u32) {
        if (timed == true) {
            std.uuid::uuid made = std.uuid::v7();
            std.string::string row = f"{made}\n";
            std.string::append_str(&out, row);
        } else {
            std.uuid::uuid made = std.uuid::v4();
            std.string::string row = f"{made}\n";
            std.string::append_str(&out, row);
        }
    }
    return move out;
}

/* tokens id parse TEXT: the canonical form, the version, and whether it is the nil or the max
   identifier. */
std.string::string described(str text) throws std.convert::parse_error, std.alloc::alloc_error {
    std.uuid::uuid parsed = std.uuid::parse(text);
    u8 version = std.uuid::version(&parsed);
    std.uuid::uuid nil = std.uuid::nil();
    std.uuid::uuid max = std.uuid::max();
    bool is_nil = std.cmp::is_equal(&parsed, &nil);
    bool is_max = std.cmp::is_equal(&parsed, &max);
    return f"{parsed} version {version} nil={is_nil} max={is_max}\n";
}

/* tokens id at MILLISECONDS HEX20: the version 7 identifier of a time and 10 given bytes, and the
   version 4 identifier of the same bytes twice (RFC 9562 layouts). */
std.string::string built(u64 milliseconds, const u8[] random) throws std.alloc::alloc_error {
    u8[10] tail = {};
    u8[16] whole = {};
    for (usize index = 0usize; index < 10usize; index += 1usize) {
        tail[index] = random[index];
        whole[index] = random[index];
    }
    for (usize index = 0usize; index < 6usize; index += 1usize) { whole[10usize + index] = random[index]; }
    std.uuid::uuid seven = std.uuid::from_time_v7(milliseconds, tail);
    std.uuid::uuid four = std.uuid::from_random_v4(whole);
    return f"v7 {seven}\nv4 {four}\n";
}

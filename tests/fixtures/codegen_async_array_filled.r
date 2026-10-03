module test.codegen.async_array_filled;

// R-LIB-0019 (P4.2): std.array::filled in an async function; the arrays and the staged values
// live across an await in the task frame.

struct Pair { u16 left; u32 right; };

async u32 identity(u32 value) {
    return value;
}

async i32 checks() throws std.alloc::alloc_error, std.array::push_error<u32>, std.async::start_error {
    i32 status = 0;
    u32 fill = await identity(4294967295u32);
    array<u32> words = std.array::filled(1000usize, fill);
    u32 extra = await identity(5u32);
    if (len(words) != 1000usize || words[0] != 4294967295u32 || words[999] != 4294967295u32) {
        status += 1;
    }
    std.array::push(&words, extra);
    if (len(words) != 1001usize || words[1000] != 5u32) { status += 2; }
    array<Pair> pairs = std.array::filled(5usize, Pair {.left = 1u16, .right = 2u32});
    u32 later = await identity(3u32);
    u32 sum = later;
    for (usize index = 0usize; index < len(pairs); index += 1usize) {
        sum += pairs[index].left as u32 + pairs[index].right;
    }
    if (sum != 18u32) { status += 4; }
    array<u8> bytes = std.array::filled(3u32, 7u8);
    if (len(bytes) != 3usize || bytes[1] != 7u8) { status += 8; }
    array<u64> wide = std.array::filled(2usize, 6);
    if (wide[0] != 6u64) { status += 16; }
    drop words;
    drop pairs;
    drop bytes;
    drop wide;
    return status;
}

async i32 main() {
    i32 status = 0;
    try {
        status += await checks();
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 101;
    } catch (std.array::push_error<u32> failure) {
        failure as void;
        return 102;
    } catch (std.async::start_error failure) {
        failure as void;
        return 104;
    }
    try {
        array<u64> huge = std.array::filled(18446744073709551615usize, 1u64);
        drop huge;
        status += 100;
    } catch (std.alloc::alloc_error failure) {
        failure as void;
    }
    return status;
}

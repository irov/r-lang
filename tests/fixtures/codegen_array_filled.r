module test.codegen.array_filled;

// R-LIB-0019 (P4.2): std.array::filled makes length copies of one Copy value in one allocation.

struct Pair { u16 left; u32 right; };

// Evaluated during translation (Core R-FUNC-0023): four copies, counted.
usize filled_count() throws std.alloc::alloc_error {
    array<u8> marks = std.array::filled(4usize, 1u8);
    usize total = 0usize;
    for (usize index = 0usize; index < len(marks); index += 1usize) { total += marks[index] as usize; }
    return total;
}

const usize FILLED_COUNT = filled_count();

i32 checks() throws std.alloc::alloc_error, std.array::push_error<u32> {
    i32 status = 0;
    array<u32> words = std.array::filled(1000usize, 4294967295u32);
    if (len(words) != 1000usize || words[0] != 4294967295u32 || words[999] != 4294967295u32) {
        status += 1;
    }
    if (std.array::capacity(&words) < 1000usize) { status += 2; }
    std.array::push(&words, 5u32);
    if (len(words) != 1001usize || words[1000] != 5u32 || words[500] != 4294967295u32) {
        status += 4;
    }
    array<u8> bytes = std.array::filled(3u32, 7u8);
    if (len(bytes) != 3usize || bytes[0] != 7u8 || bytes[2] != 7u8) { status += 8; }
    array<Pair> pairs = std.array::filled(5usize, Pair {.left = 1u16, .right = 2u32});
    u32 sum = 0u32;
    for (usize index = 0usize; index < len(pairs); index += 1usize) {
        sum += pairs[index].left as u32 + pairs[index].right;
    }
    if (sum != 15u32) { status += 16; }
    array<i64> empty = std.array::filled(0usize, 9i64);
    if (len(empty) != 0usize) { status += 32; }
    // The element type comes from the expected array<T>.
    array<u64> wide = std.array::filled(2usize, 6);
    if (len(wide) != 2usize || wide[1] != 6u64) { status += 64; }
    drop words;
    drop bytes;
    drop pairs;
    drop empty;
    drop wide;
    return status;
}

i32 main() {
    i32 status = 0;
    try {
        status += checks();
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 101;
    } catch (std.array::push_error<u32> failure) {
        failure as void;
        return 102;
    }
    // A length whose bytes exceed the address space fails before any allocation.
    try {
        array<u64> huge = std.array::filled(18446744073709551615usize, 1u64);
        drop huge;
        status += 100;
    } catch (std.alloc::alloc_error failure) {
        failure as void;
    }
    if (FILLED_COUNT != 4usize) { status += 103; }
    return status;
}

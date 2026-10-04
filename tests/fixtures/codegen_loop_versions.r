module test.codegen.loop_versions;

/* P4.4: an innermost for loop over `k < bound` checks its affine indices `offset + k * stride`
   once before it and runs a copy without those checks when every iteration stays in bounds
   (Core R-AM-0003, R-EXPR-0021). The program compares each loop with the value the source
   defines, including loops whose single check fails and that therefore run as written: an index
   that wraps, a stride that overflows, a loop that leaves before an index past the end. A line
   marked `versioned` holds an index that the C17 output checks once before its loop; a line
   marked `checked` keeps its check in every iteration (tests/check_loop_versions.py). */

/* Dense product over flat slices, indexed i * n + k and k * n + j. */
u32 product(const u32[] a, const u32[] b, usize n) {
    u32 total = 0u32;
    for (usize i = 0usize; i < n; i += 1usize) {
        for (usize j = 0usize; j < n; j += 1usize) {
            u32 sum = 0u32;
            for (usize k = 0usize; k < n; k += 1usize) {
                sum += a[i * n + k] /* versioned */
                       * b[k * n + j]; /* versioned */
            }
            total += sum * (j as u32 + 1u32);
        }
    }
    return total;
}

/* The last index is exactly len - 1. */
u32 to_the_end(const u32[] data, usize start) {
    u32 total = 0u32;
    for (usize k = start; k < len(data) - start; k += 1usize) {
        total += data[start + k]; /* versioned */
    }
    return total;
}

/* A stride of zero reads one element in every iteration. */
u32 repeated(const u32[] data, usize at, usize count) {
    u32 total = 0u32;
    for (usize k = 0usize; k < count; k += 1usize) {
        total += data[at + k * 0usize]; /* versioned */
    }
    return total;
}

/* An all-ones stride wraps: the index falls by one in each iteration, so the single check
   fails and the loop runs as written, with every index in bounds. */
u32 backwards(const u32[] data, usize from, usize count) {
    u32 total = 0u32;
    usize down = 18446744073709551615usize;
    for (usize k = 0usize; k < count; k += 1usize) {
        total += data[from + k * down] * (k as u32 + 1u32); /* versioned */
    }
    return total;
}

/* The loop leaves before its index passes the end, so the single check fails and the loop
   runs as written without a panic. */
u32 early_exit(const u32[] data, usize stride) {
    u32 total = 0u32;
    for (usize k = 0usize; k < 1000usize; k += 1usize) {
        if (k == 3usize) { break; }
        if (k == 1usize) { continue; }
        total += data[k * stride]; /* versioned */
    }
    return total;
}

/* An empty range runs no iteration whatever its indices. */
u32 empty(const u32[] data, usize from, usize to) {
    u32 total = 0u32;
    for (usize k = from; k < to; k += 1usize) {
        total += data[k * 1000000usize]; /* versioned */
    }
    return total;
}

/* An array<T> whose length the loop does not change. */
u32 from_array(usize n) throws std.alloc::alloc_error {
    array<u32> values = std.array::filled(n * 2usize, 3u32);
    u32 total = 0u32;
    for (usize k = 0usize; k < n; k += 1usize) {
        total += values[2usize * k + 1usize]; /* versioned */
    }
    drop values;
    return total;
}

/* A base that the loop changes keeps its check. */
u32 growing(usize n) throws std.alloc::alloc_error, std.array::push_error<u32> {
    array<u32> values = std.array::filled(1usize, 1u32);
    u32 total = 0u32;
    for (usize k = 0usize; k < n; k += 1usize) {
        std.array::push(&values, k as u32);
        total += values[k]; /* checked */
    }
    drop values;
    return total;
}

/* An induction variable the body changes keeps the check. */
u32 skipping(const u32[] data) {
    u32 total = 0u32;
    for (usize k = 0usize; k < 4usize; k += 1usize) {
        total += data[k + 1usize]; /* checked */
        k += 1usize;
    }
    return total;
}

i32 main() {
    u32[16] storage = {1u32, 2u32, 3u32, 4u32, 5u32, 6u32, 7u32, 8u32,
                       9u32, 10u32, 11u32, 12u32, 13u32, 14u32, 15u32, 16u32};
    const u32[] data = storage[..];
    if (product(data, data, 4usize) != 13040u32) { return 1; }
    if (to_the_end(data, 2usize) != 126u32) { return 2; }
    if (repeated(data, 5usize, 7usize) != 42u32) { return 3; }
    if (backwards(data, 9usize, 4usize) != 80u32) { return 4; }
    if (early_exit(data, 7usize) != 16u32) { return 5; }
    if (empty(data, 5usize, 5usize) != 0u32) { return 6; }
    if (empty(data, 6usize, 2usize) != 0u32) { return 7; }
    try {
        if (from_array(5usize) != 15u32) { return 8; }
        if (growing(4usize) != 4u32) { return 9; }
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 10;
    } catch (std.array::push_error<u32> failure) {
        move failure as void;
        return 11;
    }
    if (skipping(data) != 6u32) { return 12; }
    return 0;
}

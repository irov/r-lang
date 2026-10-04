module test.codegen.loop_versions_bounds;

/* P4.4 with R-EXPR-0021: the single check before the loop fails because the last index is one
   past the end, so the loop runs as written and its last iteration panics. */

u32 overrun(const u32[] data, usize count) {
    u32 total = 0u32;
    for (usize k = 0usize; k < count; k += 1usize) {
        total += data[k + 1usize];
    }
    return total;
}

i32 main() {
    u32[4] storage = {1u32, 2u32, 3u32, 4u32};
    return overrun(storage[..], 4usize) as i32;
}

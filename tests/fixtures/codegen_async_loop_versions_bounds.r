module test.codegen.async_loop_versions_bounds;

/* P4.4 with R-EXPR-0021: in the direct twin of an async function the single check before the
   loop fails because the last index is one past the end, so the loop runs as written and its
   last iteration panics. */

async u32 overrun(u32[4] data, usize count) {
    const u32[] view = data[..];
    u32 total = 0u32;
    for (usize k = 0usize; k < count; k += 1usize) {
        total += view[k + 1usize];
    }
    return total;
}

async i32 main() {
    u32[4] storage = {1u32, 2u32, 3u32, 4u32};
    return (await overrun(storage, 4usize)) as i32;
}

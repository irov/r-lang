module test.codegen.async_loop_versions;

/* P4.4: the direct twin of an async function whose body cannot suspend is an ordinary function,
   so its innermost loops check their affine indices once, as those of a synchronous body do
   (Core R-AM-0003, R-EXPR-0021); a started task runs the step of the same body with every
   check. Both reach the same values. A line marked `versioned` holds an index that the C17
   output checks once before its loop (tests/check_loop_versions.py). */

async u32 strided(u32[16] data, usize stride, usize count) {
    const u32[] view = data[..];
    u32 total = 0u32;
    for (usize k = 0usize; k < count; k += 1usize) {
        total += view[k * stride + 1usize]; /* versioned */
    }
    return total;
}

async i32 main() {
    u32[16] storage = {1u32, 2u32, 3u32, 4u32, 5u32, 6u32, 7u32, 8u32,
                       9u32, 10u32, 11u32, 12u32, 13u32, 14u32, 15u32, 16u32};
    if ((await strided(storage, 3usize, 5usize)) != 40u32) { return 1; }
    task<u32> started = strided(storage, 2usize, 8usize);
    if ((await move started) != 72u32) { return 2; }
    if ((await strided(storage, 2usize, 8usize)) != 72u32) { return 3; }
    return 0;
}

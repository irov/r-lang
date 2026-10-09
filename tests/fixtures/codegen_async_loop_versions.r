module test.codegen.async_loop_versions;

/* The direct twin of an async function whose body cannot suspend is an ordinary function, so its
   loops version their affine indices as those of a synchronous body do (Core R-AM-0003,
   R-EXPR-0021); a started task runs the step of the same body, whose index is versioned the same
   way. Both reach the same values. A line marked `versioned` holds an index whose check tests
   its largest value (tests/check_loop_versions.py). */

async u32 strided(u32[16] data, usize stride, usize count) {
    const u32[] view = data[..];
    u32 total = 0u32;
    for (usize k = 0usize; k < count; k += 1usize) {
        total += view[k * stride + 1usize]; /* versioned */
    }
    return total;
}

async u32 masked(u32[16] data, usize rounds) {
    const u32[] view = data[..];
    u32 total = 0u32;
    for (usize k = 0usize; k < rounds; k += 1usize) {
        total += view[k & 15usize]; /* versioned */
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
    if ((await masked(storage, 20usize)) != 146u32) { return 4; }
    return 0;
}

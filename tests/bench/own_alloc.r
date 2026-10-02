module bench.own_alloc;

/* Allocate and drop one owned u32 per iteration: allocator entry, drop glue, release. */
i32 main() {
    usize iterations = 20_000_000usize;
    u32 total = 0u32;
    for (usize index = 0usize; index < iterations; index += 1usize) {
        own u32* boxed = new u32(index as u32);
        total ^= *boxed;
    }
    return (total % 109u32) as i32;
}

module test.codegen.index_proofs_bounds;

/* R-EXPR-0021 with index proofs: the loop condition bounds the index only until the body
   changes it, so the check stays and the last iteration panics. */

u32 tail_sum(const u8[] data) {
    u32 total = 0u32;
    usize i = 0usize;
    while (i < len(data)) {
        i += 1usize;
        total += data[i] as u32;
    }
    return total;
}

i32 main() {
    u8[3] storage = {1, 2, 3};
    return tail_sum(storage[..]) as i32;
}

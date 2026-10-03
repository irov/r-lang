module test.codegen.async_index_proofs;

/* R-EXPR-0021 with index proofs in an async function: a value range alone (a constant, a mask,
   a narrow unsigned type) proves an index below a fixed-array bound and an explicit integer
   conversion (R-EXPR-0015) into its target; an index that would need the facts of a loop keeps
   its check. tests/check_index_proofs.py reads the marks. */

async i32 main() {
    u8[16] ring = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    u32[256] table = {};
    u32 seed = 0x12345678u32;
    std.time::duration pause = std.time::duration_from_seconds(0i64);
    await pause.sleep_for();
    u32 total = ring[3usize] as u32; /* proven */
    total += ring[(seed & 0xfu32) as usize] as u32; /* proven */
    u8 byte = ring[5usize]; /* proven */
    table[byte as usize] = 7u32; /* proven */
    await pause.sleep_for();
    total += table[byte as usize]; /* proven */
    for (usize i = 0usize; i < 16usize; i += 1usize) {
        total += ring[i] as u32; /* checked */
    }
    u8 low = (seed & 0xffu32) as u8; /* proven conversion */
    u8 half = (seed / 0x200000u32) as u8; /* checked conversion */
    total += (low as u32) + (half as u32);
    if (total != 3u32 + 8u32 + 7u32 + 120u32 + 0x78u32 + 145u32) {
        return 1;
    }
    return 0;
}

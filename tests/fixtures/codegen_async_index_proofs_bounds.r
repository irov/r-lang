module test.codegen.async_index_proofs_bounds;

/* R-EXPR-0021 with index proofs in an async function: a mask that admits values past the
   bound proves nothing, so the check stays and panics. */

async i32 main() {
    u8[4] small = {1, 2, 3, 4};
    u32 seed = 6u32;
    std.time::duration pause = std.time::duration_from_seconds(0i64);
    await pause.sleep_for();
    u32 picked = small[(seed & 7u32) as usize] as u32;
    return picked as i32;
}

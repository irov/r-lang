module bench.checked_arith;

/* Signed i64 accumulation: every + and - carries the integer_overflow check of R-EXPR-0005. */
i32 main() {
    usize iterations = 200_000_000usize;
    u32 state = 12345u32;
    i64 total = 0i64;
    for (usize index = 0usize; index < iterations; index += 1usize) {
        state = state * 1664525u32 + 1013904223u32;
        total = total + ((state >> 24u32) as i64) - 100i64;
    }
    i64 reduced = ((total % 109i64) + 109i64) % 109i64;
    return reduced as i32;
}

module test.semantic.switch_dispatch_constexpr;

/* R-STMT-0023, R-STMT-0024: a plain continue in a labeled switch continues the loop around the
   switch, so inside a translation-time loop it would leave the repetitions (defect L46-5). */
u32 pick() {
    u32 total = 0u32;
    for (u32 round = 0u32; round < 2u32; round += 1u32) {
        for (constexpr u32 index in 0u32..3u32) {
            step: switch (index) {
            case 0u32:
                continue;
            default:
                total += 1u32;
            }
        }
    }
    return total;
}

i32 main() {
    return pick() as i32;
}

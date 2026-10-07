module test.semantic.constexpr_for_limit;

/* R-STMT-0023: a translation-time loop repeats its block at most 1024 times. */
i32 main() {
    usize total = 0usize;
    for (constexpr usize index in 0usize..2000usize) {
        total += index;
    }
    return 0;
}

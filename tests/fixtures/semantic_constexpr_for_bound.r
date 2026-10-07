module test.semantic.constexpr_for_bound;

/* R-STMT-0023: the bounds of a translation-time loop are constant expressions. */
i32 main() {
    usize count = 3usize;
    usize total = 0usize;
    for (constexpr usize index in 0usize..count) {
        total += index;
    }
    return total as i32;
}

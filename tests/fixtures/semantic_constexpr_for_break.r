module test.semantic.constexpr_for_break;

/* R-STMT-0023: break and continue do not leave a translation-time loop. */
i32 main() {
    usize total = 0usize;
    for (constexpr usize index in 0usize..3usize) {
        total += index;
        break;
    }
    return total as i32;
}

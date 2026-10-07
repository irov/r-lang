module test.semantic.constexpr_for_label;

/* R-STMT-0023: a translation-time loop is no target of a jump and takes no label. */
i32 main() {
    usize total = 0usize;
    outer: for (constexpr usize index in 0usize..3usize) {
        total += index;
    }
    return total as i32;
}

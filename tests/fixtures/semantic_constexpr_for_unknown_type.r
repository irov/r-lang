module test.semantic.constexpr_for_unknown_type;

/* R-STMT-0023: the type of a loop constant is resolved like any type name (defect L44-15). */
i32 main() {
    usize total = 0usize;
    for (constexpr Missing index in 0usize..3usize) {
        total += 1usize;
    }
    return total as i32;
}

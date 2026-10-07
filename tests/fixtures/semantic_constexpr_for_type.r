module test.semantic.constexpr_for_type;

/* R-STMT-0023: the constant of a translation-time loop has an integer type. */
i32 main() {
    for (constexpr f64 value in 0.0..2.0) {
        value as void;
    }
    return 0;
}

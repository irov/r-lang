module test.semantic.core_bits_suffix;

/* R-LIB-0027: the suffix names an integer type. */
i32 main() {
    u32 zeros = core::leading_zeros_f64(1.0);
    zeros as void;
    return 0;
}

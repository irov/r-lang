module test.semantic.core_bits_operands;

/* R-LIB-0027: a population count takes exactly one operand. */
i32 main() {
    u32 ones = core::count_ones_u32(1u32, 2u32);
    ones as void;
    return 0;
}

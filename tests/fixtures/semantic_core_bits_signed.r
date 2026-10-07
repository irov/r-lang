module test.semantic.core_bits_signed;

/* R-LIB-0027: the wide integer operations take only unsigned types. */
i32 main() {
    auto (low, high) = core::widening_mul_i64(3i64, 5i64);
    low as void;
    high as void;
    return 0;
}

module test.semantic.core_bits_consteval;

/* R-LIB-0027, R-EXPR-0032: a narrowing division whose quotient does not fit panics at translation,
   which is an error where a constant is required. */
u64 quotient(u64 high) {
    auto (value, rest) = core::narrowing_div_u64(high, 0u64, 4u64);
    rest as void;
    return value;
}

const u64 QUOTIENT = quotient(4u64);

i32 main() {
    return 0;
}

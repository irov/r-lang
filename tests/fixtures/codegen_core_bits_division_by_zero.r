module test.codegen.core_bits_division_by_zero;

/* Library R-LIB-0027 (L45): a narrowing division by zero panics with division_by_zero. */
u64 quotient(u64 high, u64 low, u64 divisor) {
    auto (value, rest) = core::narrowing_div_u64(high, low, divisor);
    rest as void;
    return value;
}

i32 main(const str[] arguments) {
    u64 divisor = len(arguments) as u64 - 1u64;
    return quotient(0u64, 7u64, divisor) as i32;
}

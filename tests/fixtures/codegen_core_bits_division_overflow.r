module test.codegen.core_bits_division_overflow;

/* Library R-LIB-0027 (L45): a narrowing division whose high half is not below the divisor has no
   quotient of the type and panics with integer_overflow. */
u32 quotient(u32 high, u32 low, u32 divisor) {
    auto (value, rest) = core::narrowing_div_u32(high, low, divisor);
    rest as void;
    return value;
}

i32 main(const str[] arguments) {
    u32 high = len(arguments) as u32 + 2u32;
    return quotient(high, 0u32, 3u32) as i32;
}

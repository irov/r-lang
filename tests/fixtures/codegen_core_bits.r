module test.codegen.core_bits;

/* Library R-LIB-0027 (L45): the bit and wide integer operations of core over boundary values,
   with the type suffix and without it; a function evaluated at translation (Core R-FUNC-0023)
   gives the same value as its call at run time. */

/* Every operation folded into one value. */
u64 digest(u64 seed) {
    u64 value = seed;
    value ^= core::leading_zeros_u64(seed) as u64;
    value ^= (core::trailing_zeros(seed) as u64) << 8u32;
    value ^= (core::count_ones_u64(seed) as u64) << 16u32;
    value ^= core::swap_bytes_u64(seed);
    value ^= core::rotate_left(seed, 13u32);
    value ^= core::rotate_right_u64(seed, 7u32);
    value ^= core::count_ones_u8((seed & 255u64) as u8) as u64;
    value ^= core::leading_zeros_u16((seed & 65535u64) as u16) as u64;
    auto (low, high) = core::widening_mul_u64(seed, 11400714819323198485u64);
    value ^= low ^ high;
    auto (sum, carry) = core::carrying_add_u64(seed, core::max_u64, true);
    value ^= sum;
    if (carry == true) {
        value ^= 1u64;
    }
    auto (difference, borrow) = core::borrowing_sub(seed, high, false);
    value ^= difference;
    if (borrow == true) {
        value ^= 2u64;
    }
    auto (quotient, remainder) = core::narrowing_div_u64(seed % 1000u64, low, 1000u64);
    value ^= quotient ^ remainder;
    return value;
}

const u64 AT_TRANSLATION = digest(12345u64);

u32 unsigned_checks(u64 zero, u64 top, u64 mixed) {
    u32 failures = 0u32;
    if (core::leading_zeros_u64(zero) != 64u32) { failures += 1u32; }
    if (core::leading_zeros(top) != 0u32) { failures += 1u32; }
    if (core::trailing_zeros_u64(zero) != 64u32) { failures += 1u32; }
    if (core::trailing_zeros_u64(top) != 0u32) { failures += 1u32; }
    if (core::trailing_zeros_u64(top - 1u64) != 1u32) { failures += 1u32; }
    if (core::count_ones(mixed) != 32u32) { failures += 1u32; }
    if (core::count_ones_u64(zero) != 0u32) { failures += 1u32; }
    if (core::swap_bytes_u64(mixed) != 0xefcdab8967452301u64) { failures += 1u32; }
    if (core::rotate_left_u64(mixed, 68u32) != 0x123456789abcdef0u64) { failures += 1u32; }
    if (core::rotate_right(core::rotate_left_u64(mixed, 9u32), 9u32) != mixed) { failures += 1u32; }
    if (core::leading_zeros_u8((mixed & 255u64) as u8) != 0u32) { failures += 1u32; }
    if (core::leading_zeros_u16((zero & 65535u64) as u16) != 16u32) { failures += 1u32; }
    if (core::leading_zeros_u32(((mixed >> 40u32) & 255u64) as u32) != 25u32) { failures += 1u32; }
    if (core::swap_bytes_u16(0x1234u16) != 0x3412u16) { failures += 1u32; }
    if (core::rotate_left_u8(129u8, 1u32) != 3u8) { failures += 1u32; }
    if (core::count_ones_usize(top as usize) != 64u32) { failures += 1u32; }
    return failures;
}

u32 signed_checks(i32 minus_one, i8 one) {
    u32 failures = 0u32;
    if (core::count_ones_i32(minus_one) != 32u32) { failures += 1u32; }
    if (core::leading_zeros(minus_one) != 0u32) { failures += 1u32; }
    if (core::leading_zeros_i8(one) != 7u32) { failures += 1u32; }
    if (core::trailing_zeros_i64(minus_one as i64) != 0u32) { failures += 1u32; }
    if (core::swap_bytes_i32(1i32) != 16777216i32) { failures += 1u32; }
    if (core::swap_bytes_i16(-2i16) != -257i16) { failures += 1u32; }
    if (core::rotate_right_i8(one, 1u32) != -128i8) { failures += 1u32; }
    if (core::rotate_left_isize(-1isize, 5u32) != -1isize) { failures += 1u32; }
    return failures;
}

u32 wide_checks(u64 top, u32 five) {
    u32 failures = 0u32;
    auto (low, high) = core::widening_mul_u64(top, top);
    bool low_ok = low == 1u64;
    bool high_ok = high == 18446744073709551614u64;
    if (low_ok == false || high_ok == false) { failures += 1u32; }
    auto (small_low, small_high) = core::widening_mul(255u8, 255u8);
    bool small_ok = small_low == 1u8;
    bool small_high_ok = small_high == 254u8;
    if (small_ok == false || small_high_ok == false) { failures += 1u32; }
    auto (sum, carry) = core::carrying_add_u64(top, 0u64, true);
    bool sum_ok = sum == 0u64;
    bool carried = carry;
    if (sum_ok == false || carried == false) { failures += 1u32; }
    auto (difference, borrow) = core::borrowing_sub_u64(0u64, 0u64, true);
    bool difference_ok = difference == top;
    bool borrowed = borrow;
    if (difference_ok == false || borrowed == false) { failures += 1u32; }
    auto (kept, no_borrow) = core::borrowing_sub_u16(9u16, 4u16, true);
    bool kept_ok = kept == 4u16;
    bool kept_borrow = no_borrow;
    if (kept_ok == false || kept_borrow == true) { failures += 1u32; }
    auto (quotient, remainder) = core::narrowing_div_u64(1u64, 0u64, 3u64);
    bool quotient_ok = quotient == 6148914691236517205u64;
    bool remainder_ok = remainder == 1u64;
    if (quotient_ok == false || remainder_ok == false) { failures += 1u32; }
    auto (quotient_32, remainder_32) = core::narrowing_div_u32(five, 7u32, 9u32);
    bool quotient_32_ok = quotient_32 == 2386092943u32;
    bool remainder_32_ok = remainder_32 == 0u32;
    if (quotient_32_ok == false || remainder_32_ok == false) { failures += 1u32; }
    auto (top_quotient, top_remainder) = core::narrowing_div_usize(4usize, 5usize, top as usize);
    bool top_quotient_ok = top_quotient == 4usize;
    bool top_remainder_ok = top_remainder == 9usize;
    if (top_quotient_ok == false || top_remainder_ok == false) { failures += 1u32; }
    return failures;
}

/* Defect L45-3: an element, a pointee or a field read through a shared view selects the operation
   without the suffix by its type. */
struct Limbs {
    u64 low;
    u64 high;
};

u64 places(const u64[] values, const u64* pointer, const Limbs* limbs) {
    u64 total = core::count_ones(values[0usize]) as u64;
    total += core::count_ones(*pointer) as u64;
    total += core::leading_zeros(limbs->high) as u64;
    auto (low, high) = core::widening_mul(values[0usize], values[1usize]);
    return total + low + high * 1000u64 + core::wrapping_add(values[1usize], 1u64);
}

i32 main(const str[] arguments) {
    /* One argument, the program name, so the seed is 12345 only at run time. */
    u64 seed = 12344u64 + len(arguments) as u64;
    if (digest(seed) != AT_TRANSLATION) { return 1; }
    if (unsigned_checks(0u64, core::max_u64, 0x0123456789abcdefu64) != 0u32) { return 2; }
    if (signed_checks(-1i32, 1i8) != 0u32) { return 3; }
    if (wide_checks(core::max_u64, 5u32) != 0u32) { return 4; }
    /* A bound of core is an argument whose type selects an overload (defect L45-1). */
    if (core::wrapping_add(core::max_u32, 1u32) != 0u32) { return 5; }
    if (core::count_ones(core::min_i16) != 1u32) { return 6; }
    u64[2] values = {3u64, 5u64};
    u64 seven = 7u64;
    Limbs limbs = {.low = 0u64, .high = 1u64};
    if (places(values[0usize..2usize], &seven, &limbs) != 89u64) { return 7; }
    return 0;
}

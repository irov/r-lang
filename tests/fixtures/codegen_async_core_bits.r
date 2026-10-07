module test.codegen.async_core_bits;

/* Library R-LIB-0027 (L45) in async bodies: the operands and the parts of a wide result live in
   the frame across awaits. */

async u64 later(u64 value) throws std.error::fault {
    await std.time::sleep_for(std.time::duration_from_parts(0i64, 1000000u32));
    return value;
}

async u32 run(u64 seed) throws std.error::fault {
    u32 failures = 0u32;
    u64 value = await later(seed);
    auto (low, high) = core::widening_mul_u64(value, value);
    u64 low_again = await later(low);
    bool low_ok = low_again == 1u64;
    bool high_ok = high == 18446744073709551614u64;
    if (low_ok == false || high_ok == false) { failures += 1u32; }
    auto (quotient, remainder) = core::narrowing_div_u64(1u64, await later(0u64), 3u64);
    bool quotient_ok = quotient == 6148914691236517205u64;
    bool remainder_ok = remainder == 1u64;
    if (quotient_ok == false || remainder_ok == false) { failures += 1u32; }
    auto (sum, carry) = core::carrying_add_u64(value, await later(1u64), false);
    bool sum_ok = sum == 0u64;
    bool carried = carry;
    if (sum_ok == false || carried == false) { failures += 1u32; }
    if (core::count_ones_u64(await later(value)) != 64u32) { failures += 1u32; }
    u64 byte = await later(255u64);
    if (core::leading_zeros(byte) != 56u32) { failures += 1u32; }
    if (core::swap_bytes_u32(core::rotate_right_u32(1u32, 8u32)) != 1u32) { failures += 1u32; }
    return failures;
}

async i32 main() {
    try {
        u32 failures = await run(core::max_u64);
        if (failures != 0u32) { return 1; }
        return 0;
    } catch (std.error::fault failure) {
        failure as void;
    }
    return 9;
}

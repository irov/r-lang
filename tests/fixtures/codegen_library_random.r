module test.codegen.library_random;

import std.random;

// R-SLIB-RANDOM-0001..0002 (M20): bytes of the operating-system generator, uniform draws, and the
// reproducible generator against reference values of SplitMix64-seeded xoshiro256**
// (tests/m20_reference.py).

protected i32 check_system() {
    u8[64] first = {};
    u8[64] second = {};
    std.random::fill(&first);
    std.random::fill(&second);
    if (std.bytes::equal(first, second) == true) { return 1; }
    u64 one = std.random::next_u64();
    u64 two = std.random::next_u64();
    if (one == two) { return 2; }
    u32 small = std.random::next_u32();
    u32 other = std.random::next_u32();
    if (small == other && one == 0u64) { return 3; }
    u32[10] seen = {};
    for (usize draw = 0usize; draw < 1000usize; draw += 1usize) {
        u64 value = std.random::below(10u64);
        if (value >= 10u64) { return 4; }
        seen[value as usize] += 1u32;
    }
    for (usize index = 0usize; index < 10usize; index += 1usize) {
        if (seen[index] == 0u32) { return 5; }
    }
    for (usize draw = 0usize; draw < 100usize; draw += 1usize) {
        u64 value = std.random::range(5u64, 8u64);
        if (value < 5u64 || value >= 8u64) { return 6; }
        if (std.random::below(1u64) != 0u64) { return 7; }
    }
    u64 wide = std.random::range(0u64, 18446744073709551615u64);
    wide as void;
    return 0;
}

protected i32 check_generator() {
    std.random::generator zero = std.random::generator::seeded(0u64);
    if (zero.next_u64() != 0x99ec5f36cb75f2b4u64) { return 10; }
    if (zero.next_u64() != 0xbf6e1f784956452au64) { return 11; }
    if (zero.next_u64() != 0x1a5f849d4933e6e0u64) { return 12; }
    if (zero.next_u64() != 0x6aa594f1262d2d2cu64) { return 13; }
    std.random::generator values = std.random::generator::seeded(42u64);
    if (values.next_u64() != 1546998764402558742u64) { return 14; }
    if (values.next_u64() != 6990951692964543102u64) { return 15; }
    if (values.next_u64() != 12544586762248559009u64) { return 16; }
    if (values.next_u32() != 3971525959u32 || values.next_u32() != 4259765375u32) { return 17; }
    u64[5] tens = {4u64, 4u64, 7u64, 8u64, 5u64};
    for (usize index = 0usize; index < 5usize; index += 1usize) {
        if (values.below(10u64) != tens[index]) { return 18; }
    }
    u64[3] hundreds = {149u64, 193u64, 110u64};
    for (usize index = 0usize; index < 3usize; index += 1usize) {
        if (values.range(100u64, 200u64) != hundreds[index]) { return 19; }
    }
    u8[11] bytes = {};
    values.fill(&bytes);
    u8[11] expected_bytes = {66u8, 39u8, 180u8, 164u8, 101u8, 8u8, 72u8, 82u8, 205u8, 135u8, 216u8};
    if (std.bytes::equal(bytes, expected_bytes) == false) { return 20; }
    i32[10] cards = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    values.shuffle(&cards);
    i32[10] shuffled = {2, 1, 8, 6, 5, 7, 3, 4, 9, 0};
    for (usize index = 0usize; index < 10usize; index += 1usize) {
        if (cards[index] != shuffled[index]) { return 21; }
    }
    std.random::generator fork = values;
    if (fork.next_u64() != values.next_u64()) { return 22; }
    std.random::generator first = std.random::generator::from_entropy();
    std.random::generator second = std.random::generator::from_entropy();
    if (first.next_u64() == second.next_u64()) { return 23; }
    return 0;
}

i32 main() {
    i32 system = check_system();
    if (system != 0) { return system; }
    return check_generator();
}

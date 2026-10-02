module tests.std.random;
import std.test;
import std.random;

// The tests of the R part of std.random (Library R-SLIB-RANDOM-0001..0002), run in test mode
// (Core R-FUNC-0025). Exact values come from the seeded generator and the reference
// implementation of SplitMix64-seeded xoshiro256** in tests/m20_reference.py; the generator of
// the operating system is checked by properties only.

@test
void reproduces_the_reference_sequence() throws std.test::failure, std.alloc::alloc_error {
    std.random::generator zero = std.random::generator::seeded(0u64);
    std.test::equal(zero.next_u64(), 0x99ec5f36cb75f2b4u64);
    std.test::equal(zero.next_u64(), 0xbf6e1f784956452au64);
    std.test::equal(zero.next_u64(), 0x1a5f849d4933e6e0u64);
    std.test::equal(zero.next_u64(), 0x6aa594f1262d2d2cu64);
    std.random::generator values = std.random::generator::seeded(2026u64);
    std.test::equal(values.next_u64(), 0x92e011592e98ae15u64);
    std.test::equal(values.next_u64(), 0x489f37946d6d18d8u64);
    std.test::equal(values.next_u64(), 0xd0009e279d9cdedau64);
    std.test::equal(values.next_u32(), 3838303399u32);
    std.test::equal(values.next_u32(), 3487665017u32);
    // The same seed gives the same sequence.
    std.random::generator again = std.random::generator::seeded(2026u64);
    std.test::equal(again.next_u64(), 0x92e011592e98ae15u64);
}

@test
void takes_next_u32_from_the_upper_half() throws std.test::failure, std.alloc::alloc_error {
    std.random::generator halves = std.random::generator::seeded(7u64);
    std.random::generator words = std.random::generator::seeded(7u64);
    std.test::equal(halves.next_u32(), 3008953079u32);
    std.test::equal(words.next_u64(), 0xb358faf74ef9765au64);
    for (usize draw = 0usize; draw < 100usize; draw += 1usize) {
        u32 half = halves.next_u32();
        u64 word = words.next_u64();
        std.test::equal(half, (word >> 32u64) as u32);
    }
}

@test
void draws_bounded_values_reproducibly() throws std.test::failure, std.alloc::alloc_error {
    std.random::generator dice = std.random::generator::seeded(2026u64);
    for (usize skip = 0usize; skip < 5usize; skip += 1usize) { dice.next_u64() as void; }
    u64[10] rolls = {5u64, 3u64, 6u64, 4u64, 4u64, 4u64, 1u64, 5u64, 3u64, 2u64};
    for (usize index = 0usize; index < 10usize; index += 1usize) {
        std.test::equal(dice.range(1u64, 7u64), rolls[index]);
    }
    std.test::equal(dice.below(1000u64), 279u64);
    std.test::equal(dice.below(1000u64), 633u64);
    std.test::equal(dice.below(1000u64), 899u64);
    // A bound above 2^63 rejects every draw below (2^64 - bound) mod bound: here 4 of 7 draws.
    u64 bound = 0x8000000000000001u64;
    std.test::equal(dice.below(bound), 4441532456539121010u64);
    std.test::equal(dice.below(bound), 3149670789858323431u64);
    std.test::equal(dice.below(bound), 7369310570053575199u64);
    std.test::equal(dice.below(1u64), 0u64);
    std.test::equal(dice.range(9u64, 10u64), 9u64);
}

@test
void fills_bytes_in_little_endian_order() throws std.test::failure, std.alloc::alloc_error {
    std.random::generator source = std.random::generator::seeded(5u64);
    std.random::generator words = source;
    u8[13] target = {};
    source.fill(&target);
    u8[13] expected = {105u8, 207u8, 84u8, 202u8, 120u8, 81u8, 213u8, 73u8, 220u8, 36u8, 38u8,
                       77u8, 90u8};
    std.test::check(std.bytes::equal(target, expected) == true, "the reference bytes");
    // Each word gives up to eight bytes, least significant first.
    u64 first = words.next_u64();
    u64 second = words.next_u64();
    std.test::equal(target[0], (first & 255u64) as u8);
    std.test::equal(target[8], (second & 255u64) as u8);
    for (usize index = 1usize; index < 8usize; index += 1usize) {
        std.test::equal(target[index], ((first >> ((index * 8usize) as u64)) & 255u64) as u8);
    }
    for (usize index = 1usize; index < 5usize; index += 1usize) {
        std.test::equal(target[index + 8usize],
                        ((second >> ((index * 8usize) as u64)) & 255u64) as u8);
    }
    // An empty target draws nothing.
    std.random::generator unchanged = source;
    u8[4] spare = {7u8, 7u8, 7u8, 7u8};
    u8[] whole = &spare;
    source.fill(whole[0usize..0usize]);
    std.test::equal(source.next_u64(), unchanged.next_u64());
    std.test::equal(spare[0], 7u8);
}

@test
void shuffles_reproducibly() throws std.test::failure, std.alloc::alloc_error {
    // Fisher-Yates from the last element, each swap partner drawn with below.
    std.random::generator mixer = std.random::generator::seeded(8u64);
    i32[8] cards = {0, 1, 2, 3, 4, 5, 6, 7};
    mixer.shuffle(&cards);
    i32[8] expected = {2, 3, 4, 6, 5, 0, 1, 7};
    for (usize index = 0usize; index < 8usize; index += 1usize) {
        std.test::equal(cards[index], expected[index]);
    }
    str[5] words = {"a", "b", "c", "d", "e"};
    std.random::generator other = std.random::generator::seeded(99u64);
    other.shuffle(&words);
    std.string::string order = std.string::create();
    for (usize index = 0usize; index < 5usize; index += 1usize) { order.append(words[index]); }
    std.test::equal_text(order.as_str(), "beacd");
    // A shuffle from any generator keeps every item exactly once.
    u32[16] items = {};
    for (usize index = 0usize; index < 16usize; index += 1usize) { items[index] = index as u32; }
    std.random::generator fresh = std.random::generator::from_entropy();
    fresh.shuffle(&items);
    u32 mask = 0u32;
    for (usize index = 0usize; index < 16usize; index += 1usize) { mask |= 1u32 << items[index]; }
    std.test::equal(mask, 0xffffu32);
}

@test
void continues_copies_on_their_own() throws std.test::failure, std.alloc::alloc_error {
    std.random::generator original = std.random::generator::seeded(42u64);
    std.random::generator copy = original;
    u64 first = original.next_u64();
    u64 second = original.next_u64();
    // The copy starts where the original was copied and is not advanced by it.
    std.test::equal(copy.next_u64(), first);
    std.test::equal(copy.next_u64(), second);
    std.test::equal(first, 1546998764402558742u64);
    std.test::equal(second, 6990951692964543102u64);
    std.random::generator fork = copy;
    std.test::equal(fork.next_u64(), copy.next_u64());
}

@test
void draws_from_the_system_generator() throws std.test::failure, std.alloc::alloc_error {
    u8[64] first = {};
    u8[64] second = {};
    std.random::fill(&first);
    std.random::fill(&second);
    std.test::check(std.bytes::equal(first, second) == false, "two fills differ");
    std.test::not_equal(std.random::next_u64(), std.random::next_u64());
    u32[10] seen = {};
    for (usize draw = 0usize; draw < 1000usize; draw += 1usize) {
        u64 value = std.random::below(10u64);
        std.test::check(value < 10u64, "below stays below the bound");
        seen[value as usize] += 1u32;
    }
    for (usize index = 0usize; index < 10usize; index += 1usize) {
        std.test::check(seen[index] != 0u32, "every value below 10 occurs");
    }
    for (usize draw = 0usize; draw < 100usize; draw += 1usize) {
        u64 value = std.random::range(5u64, 8u64);
        std.test::check(value >= 5u64 && value < 8u64, "range stays in [5, 8)");
        std.test::equal(std.random::below(1u64), 0u64);
    }
    u32 small = std.random::next_u32();
    bool varied = std.random::next_u32() != small;
    for (usize draw = 0usize; draw < 3usize; draw += 1usize) {
        if (std.random::next_u32() != small) { varied = true; }
    }
    std.test::check(varied == true, "next_u32 varies");
    std.random::generator one = std.random::generator::from_entropy();
    std.random::generator two = std.random::generator::from_entropy();
    std.test::not_equal(one.next_u64(), two.next_u64());
}

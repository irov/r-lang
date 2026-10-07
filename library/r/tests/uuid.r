module tests.std.uuid;
import std.test;
import std.cmp;
import std.uuid;

// The tests of std.uuid (Library R-SLIB-UUID-0001..0003), run in test mode (Core R-FUNC-0025).

/* The version 4 example of RFC 9562 appendix A.3, built from its random bytes. */
protected std.uuid::uuid example_v4() {
    u8[16] random = {0x91u8, 0x91u8, 0x08u8, 0xf7u8, 0x52u8, 0xd1u8, 0x03u8, 0x20u8,
                     0x1bu8, 0xacu8, 0xf8u8, 0x47u8, 0xdbu8, 0x41u8, 0x48u8, 0xa8u8};
    return std.uuid::from_random_v4(random);
}

/* The version 7 example of RFC 9562 appendix A.6. */
protected std.uuid::uuid example_v7(u64 unix_milliseconds) {
    u8[10] random = {0x0cu8, 0xc3u8, 0x18u8, 0xc4u8, 0xdcu8, 0x0cu8, 0x0cu8, 0x07u8, 0x39u8,
                     0x8fu8};
    return std.uuid::from_time_v7(unix_milliseconds, random);
}

/* Fails unless parsing the text reports the code at the index. */
protected void expect_error(str text, std.convert::parse_error_code code, usize index)
    throws std.test::failure, std.alloc::alloc_error {
    try {
        std.uuid::uuid parsed = std.uuid::parse(text);
        parsed as void;
        std.string::string message = f"\"{text}\" parsed";
        std.test::fail(message);
    } catch (std.convert::parse_error failure) {
        std.string::string message = f"the error code of \"{text}\"";
        std.test::check(failure.code == code, message);
        std.test::equal(failure.index, index);
    }
}

@test
void builds_the_rfc_examples() throws std.test::failure, std.alloc::alloc_error {
    std.uuid::uuid four = example_v4();
    std.string::string four_text = f"{four}";
    std.test::equal_text(four_text, "919108f7-52d1-4320-9bac-f847db4148a8");
    std.test::equal(std.uuid::version(&four), 4u8);
    std.uuid::uuid seven = example_v7(0x017f22e279b0u64);
    std.string::string seven_text = f"{seven}";
    std.test::equal_text(seven_text, "017f22e2-79b0-7cc3-98c4-dc0c0c07398f");
    std.test::equal(std.uuid::version(&seven), 7u8);
    // Only the lower 48 bits of the time and the unmasked random bits are kept.
    u8[10] noisy = {0xfcu8, 0xc3u8, 0xd8u8, 0xc4u8, 0xdcu8, 0x0cu8, 0x0cu8, 0x07u8, 0x39u8,
                    0x8fu8};
    std.uuid::uuid masked = std.uuid::from_time_v7(0xabcd017f22e279b0u64, noisy);
    std.test::equal(masked, seven);
    std.uuid::uuid nil = std.uuid::nil();
    std.string::string nil_text = f"{nil}";
    std.test::equal_text(nil_text, "00000000-0000-0000-0000-000000000000");
    std.uuid::uuid max = std.uuid::max();
    std.string::string max_text = f"{max}";
    std.test::equal_text(max_text, "ffffffff-ffff-ffff-ffff-ffffffffffff");
    std.test::equal(std.uuid::version(&nil), 0u8);
    std.test::equal(std.uuid::version(&max), 15u8);
}

@test
void parses_the_text_form()
    throws std.test::failure, std.alloc::alloc_error, std.convert::parse_error {
    std.uuid::uuid seven = example_v7(0x017f22e279b0u64);
    std.uuid::uuid upper = std.uuid::parse("017F22E2-79B0-7CC3-98C4-DC0C0C07398F");
    std.test::equal(upper, seven);
    std.uuid::uuid lower = std.uuid::parse("017f22e2-79b0-7cc3-98c4-dc0c0c07398f");
    std.test::equal(lower, seven);
    std.test::equal(lower.bytes[0], 0x01u8);
    std.test::equal(lower.bytes[15], 0x8fu8);
    // The text of an identifier parses back to it.
    std.uuid::uuid four = example_v4();
    std.string::string four_text = f"{four}";
    std.uuid::uuid again = std.uuid::parse(four_text);
    std.test::equal(again, four);
    std.uuid::uuid max = std.uuid::parse("FFFFFFFF-ffff-FFFF-ffff-FFFFFFFFFFFF");
    std.test::equal(max, std.uuid::max());
}

@test
void orders_identifiers_by_their_bytes() throws std.test::failure, std.alloc::alloc_error {
    std.uuid::uuid nil = std.uuid::nil();
    std.uuid::uuid max = std.uuid::max();
    std.uuid::uuid seven = example_v7(0x017f22e279b0u64);
    std.uuid::uuid later = example_v7(0x017f22e279b1u64);
    std.uuid::uuid four = example_v4();
    std.test::check(std.cmp::is_less(&nil, &seven) == true, "nil sorts first");
    std.test::check(std.cmp::is_less(&seven, &max) == true, "max sorts last");
    // Identifiers of version 7 sort by their time.
    std.test::check(std.cmp::is_less(&seven, &later) == true, "a later millisecond");
    std.test::check(std.cmp::is_less(&later, &seven) == false, "not the other way");
    std.test::check(std.cmp::is_less(&seven, &four) == true, "0x01 before 0x91");
    std.test::check(std.cmp::is_equal(&seven, &later) == false, "different times");
    std.uuid::uuid copy = seven;
    std.test::check(std.cmp::is_equal(&copy, &seven) == true, "a copy is equal");
    std.test::not_equal(later, seven);
}

@test
void serves_as_a_dictionary_key()
    throws std.test::failure, std.alloc::alloc_error, std.convert::parse_error {
    dict<std.uuid::uuid, i32> versions = std.dict::create::<std.uuid::uuid, i32>();
    try {
        versions.insert(example_v7(0x017f22e279b0u64), 7) as void;
        versions.insert(example_v4(), 4) as void;
    } catch (std.dict::insert_error<std.uuid::uuid, i32> failure) {
        failure as void;
        std.test::fail("the insertion failed");
    }
    std.uuid::uuid parsed = std.uuid::parse("919108F7-52D1-4320-9BAC-F847DB4148A8");
    o<const i32*> found = versions.get(&parsed);
    switch (found) {
    case variant o::some(value): std.test::equal(**value, 4);
    case variant o::none: std.test::fail("an equal identifier finds the entry");
    }
    std.uuid::uuid nil = std.uuid::nil();
    o<const i32*> missing = versions.get(&nil);
    switch (missing) {
    case variant o::some(value): std.test::fail("nil was not inserted");
    case variant o::none: break;
    }
}

@test
void draws_fresh_identifiers()
    throws std.test::failure, std.alloc::alloc_error, std.time::time_error {
    std.uuid::uuid first = std.uuid::v4();
    std.uuid::uuid second = std.uuid::v4();
    std.test::equal(std.uuid::version(&first), 4u8);
    std.test::equal((first.bytes[8] & 0xc0u8) as u8, 0x80u8);
    std.test::not_equal(first, second);
    std.time::system_time before = std.time::system_now();
    std.uuid::uuid timed = std.uuid::v7();
    std.time::system_time after = std.time::system_now();
    std.test::equal(std.uuid::version(&timed), 7u8);
    std.test::equal((timed.bytes[8] & 0xc0u8) as u8, 0x80u8);
    // Bytes 0..5 hold the milliseconds of the system clock, big-endian.
    u64 milliseconds = 0u64;
    for (usize index = 0usize; index < 6usize; index += 1usize) {
        milliseconds = (milliseconds << 8u64) | (timed.bytes[index] as u64);
    }
    u64 low = (before.unix_seconds as u64) * 1000u64 + (before.nanoseconds / 1000000u32) as u64;
    u64 high = (after.unix_seconds as u64) * 1000u64 + (after.nanoseconds / 1000000u32) as u64;
    std.test::check(milliseconds + 1000u64 >= low, "not before the call");
    std.test::check(milliseconds <= high + 1000u64, "not after the call");
    std.uuid::uuid example = example_v7(0x017f22e279b0u64);
    std.test::check(std.cmp::is_less(&example, &timed) == true, "now is after 2022");
}

@test(expect = std.convert::parse_error)
void rejects_a_short_text() throws std.convert::parse_error {
    std.uuid::uuid parsed = std.uuid::parse("017f22e2-79b0");
    parsed as void;
}

@test
void reports_parse_errors() throws std.test::failure, std.alloc::alloc_error {
    std.convert::parse_error_code digit = std.convert::parse_error_code::invalid_digit;
    std.convert::parse_error_code trailing = std.convert::parse_error_code::trailing_character;
    expect_error("", std.convert::parse_error_code::empty, 0usize);
    expect_error("017f22e2_79b0-7cc3-98c4-dc0c0c07398f", digit, 8usize);
    expect_error("017f22g2-79b0-7cc3-98c4-dc0c0c07398f", digit, 6usize);
    expect_error("017f22e2-79b0-7cc3-98c4-dc0c0c07398", digit, 35usize);
    expect_error("017f", digit, 4usize);
    expect_error("017f22e2-79b0-7cc3-98c4-dc0c0c07398f0", trailing, 36usize);
    // Braces and the URN prefix are not part of the form.
    expect_error("{017f22e2-79b0-7cc3-98c4-dc0c0c07398f}", digit, 0usize);
}

@test(allocations)
void formats_under_allocation_failures() throws std.test::failure, std.alloc::alloc_error {
    std.uuid::uuid four = example_v4();
    std.string::string text = f"id {four}";
    std.test::equal_text(text, "id 919108f7-52d1-4320-9bac-f847db4148a8");
}

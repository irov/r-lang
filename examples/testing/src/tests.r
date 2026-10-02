module example.testing.tests;
import std.test;
import example.testing.version;

// The tests of example.testing.version. `r-front --test` makes this module the entry of a test
// program that runs every @test function below in order and reports each one.

@test
void parses_versions() throws std.test::failure, std.alloc::alloc_error, std.convert::parse_error {
    example.testing.version::version value = example.testing.version::parse("1.20.3");
    std.test::equal(value.major, 1u32);
    std.test::equal(value.minor, 20u32);
    std.test::equal(value.patch, 3u32);
}

/* The test passes only when parse throws the expected error. */
@test(expect = std.convert::parse_error)
void rejects_a_missing_part() throws std.convert::parse_error {
    example.testing.version::version value = example.testing.version::parse("1.2");
    value as void;
}

@test
void reports_the_offending_byte() throws std.test::failure, std.alloc::alloc_error {
    try {
        example.testing.version::version value = example.testing.version::parse("1.x.3");
        value as void;
        std.test::fail("1.x.3 is no version");
    } catch (std.convert::parse_error failure) {
        std.test::equal(failure.index, 2usize);
        std.test::check(failure.code == std.convert::parse_error_code::invalid_digit,
                        "x is an invalid digit");
    }
}

@test
void orders_versions() throws std.test::failure, std.alloc::alloc_error, std.convert::parse_error {
    example.testing.version::version older = example.testing.version::parse("1.9.9");
    example.testing.version::version newer = example.testing.version::parse("1.10.0");
    std.test::equal(example.testing.version::compare(&older, &newer), -1);
    std.test::equal(example.testing.version::compare(&newer, &older), 1);
    std.test::not_equal(example.testing.version::compare(&older, &older), 1);
}

/* Formatting allocates, so the test also runs with each allocation failing in turn: every such
   run must end with std.alloc::alloc_error, never with a wrong text. */
@test(allocations)
void formats_releases() throws std.test::failure, std.alloc::alloc_error {
    example.testing.version::version current = {.major = 1u32, .minor = 4u32, .patch = 2u32};
    example.testing.version::version next =
        example.testing.version::bump(current, example.testing.version::part::minor);
    std.string::string text = example.testing.version::text(&next);
    std.test::equal_text(text.as_str(), "1.5.0");
    std.test::contains(text.as_str(), ".5.");
}

/* An asynchronous test runs as a task of the test entry. */
@test
async void waits_for_a_release() throws std.test::failure, std.error::fault {
    example.testing.version::version current = {.major = 2u32, .minor = 0u32, .patch = 7u32};
    await std.time::sleep_for(std.time::duration_from_parts(0i64, 1000000u32));
    example.testing.version::version next =
        example.testing.version::bump(current, example.testing.version::part::major);
    std.test::check(example.testing.version::compare(&next, &current) == 1,
                    "a major release is newer");
}

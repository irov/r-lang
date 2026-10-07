module tests.std.test;
import std.test;

// The tests of std.test itself (Library R-SLIB-TEST-0001..0003), run in test mode (Core
// R-FUNC-0025): the assertions that hold and those that fail with their messages, the runner
// that the test entry keeps, and the allocation failures of the hosted allocator.

error Custom { i32 code; };

/* How many failures a test caught. */
protected struct Caught { u32 count; };

@test
void holding_assertions_allocate_nothing() throws std.test::failure, std.alloc::alloc_error {
    str left = "same";
    str right = "same";
    std.test::fail_allocation_at(0u64);
    std.test::check(true, "holds");
    std.test::equal(4u64, 4u64);
    std.test::equal(-7i32, -7i32);
    std.test::equal(1.5, 1.5);
    std.test::equal(true, true);
    std.test::equal('x', 'x');
    std.test::equal(left, right);
    std.test::not_equal(3i64, 4i64);
    std.test::not_equal(false, true);
    std.test::equal_text("abc", "abc");
    std.test::equal_text("", "");
    std.test::contains("hello world", "o w");
    std.test::contains("hello", "");
    std.test::contains("hello", "hello");
    u64 attempts = std.test::allocation_attempts();
    std.test::equal(attempts, 0u64);
}

@test
void check_and_fail_throw_their_message() throws std.test::failure, std.alloc::alloc_error {
    Caught caught = {.count = 0u32};
    try {
        std.test::check(false, "the condition is false");
    } catch (std.test::failure failure) {
        caught.count += 1u32;
        std.test::equal_text(failure.message, "the condition is false");
    }
    try {
        std.test::fail("always");
    } catch (std.test::failure failure) {
        caught.count += 1u32;
        std.test::equal_text(failure.message, "always");
    }
    try {
        std.test::fail("");
    } catch (std.test::failure failure) {
        caught.count += 1u32;
        std.test::equal_text(failure.message, "");
    }
    std.test::equal(caught.count, 3u32);
}

@test
void equal_and_not_equal_write_the_values() throws std.test::failure, std.alloc::alloc_error {
    Caught caught = {.count = 0u32};
    str abc = "abc";
    str abd = "abd";
    try {
        std.test::equal(4u64, 5u64);
    } catch (std.test::failure failure) {
        caught.count += 1u32;
        std.test::equal_text(failure.message, "expected 5, got 4");
    }
    try {
        std.test::equal(-1i32, 2i32);
    } catch (std.test::failure failure) {
        caught.count += 1u32;
        std.test::equal_text(failure.message, "expected 2, got -1");
    }
    try {
        std.test::equal(true, false);
    } catch (std.test::failure failure) {
        caught.count += 1u32;
        std.test::equal_text(failure.message, "expected false, got true");
    }
    try {
        std.test::equal('a', 'é');
    } catch (std.test::failure failure) {
        caught.count += 1u32;
        std.test::equal_text(failure.message, "expected é, got a");
    }
    try {
        std.test::equal(2.0, 100000.0);
    } catch (std.test::failure failure) {
        caught.count += 1u32;
        std.test::equal_text(failure.message, "expected 1e5, got 2");
    }
    try {
        std.test::equal(abc, abd);
    } catch (std.test::failure failure) {
        caught.count += 1u32;
        std.test::equal_text(failure.message, "expected abd, got abc");
    }
    try {
        std.test::not_equal(3i32, 3i32);
    } catch (std.test::failure failure) {
        caught.count += 1u32;
        std.test::equal_text(failure.message, "expected a value other than 3");
    }
    try {
        std.test::not_equal(abc, abc);
    } catch (std.test::failure failure) {
        caught.count += 1u32;
        std.test::equal_text(failure.message, "expected a value other than abc");
    }
    try {
        std.test::not_equal(0.25, 0.25);
    } catch (std.test::failure failure) {
        caught.count += 1u32;
        std.test::equal_text(failure.message, "expected a value other than 0.25");
    }
    std.test::equal(caught.count, 9u32);
}

@test
void equal_text_and_contains_quote_the_texts() throws std.test::failure, std.alloc::alloc_error {
    Caught caught = {.count = 0u32};
    try {
        std.test::equal_text("a", "b");
    } catch (std.test::failure failure) {
        caught.count += 1u32;
        std.test::equal_text(failure.message, "expected \"b\", got \"a\"");
    }
    try {
        std.test::equal_text("abc", "ab");
    } catch (std.test::failure failure) {
        caught.count += 1u32;
        std.test::equal_text(failure.message, "expected \"ab\", got \"abc\"");
    }
    try {
        std.test::equal_text("", "x");
    } catch (std.test::failure failure) {
        caught.count += 1u32;
        std.test::equal_text(failure.message, "expected \"x\", got \"\"");
    }
    try {
        std.test::contains("hello", "xyz");
    } catch (std.test::failure failure) {
        caught.count += 1u32;
        std.test::equal_text(failure.message, "expected \"hello\" to contain \"xyz\"");
    }
    try {
        std.test::contains("abc", "ABC");
    } catch (std.test::failure failure) {
        caught.count += 1u32;
        std.test::equal_text(failure.message, "expected \"abc\" to contain \"ABC\"");
    }
    try {
        std.test::contains("", "a");
    } catch (std.test::failure failure) {
        caught.count += 1u32;
        std.test::equal_text(failure.message, "expected \"\" to contain \"a\"");
    }
    std.test::equal(caught.count, 6u32);
}

@test(expect = std.test::failure)
void an_expected_failure_passes() throws std.test::failure, std.alloc::alloc_error {
    i32 two = 1 + 1;
    std.test::check(two == 3, "arithmetic");
}

@test(allocations)
void runner_counts_tests() throws std.test::failure, std.alloc::alloc_error {
    std.test::runner runner = std.test::runner::create();
    std.string::string empty = runner.summary();
    std.test::equal_text(empty, "0 tests: 0 passed, 0 failed\n");
    std.test::equal(runner.status(), 0);
    std.string::string first = runner.start("alpha");
    std.test::equal_text(first, "test alpha ... ");
    std.test::check(runner.passing(), "a started test passes");
    std.string::string passed = runner.finish();
    std.test::equal_text(passed, "ok\n");
    std.string::string second = runner.start("beta");
    std.test::equal_text(second, "test beta ... ");
    try {
        std.test::fail("boom");
    } catch (std.test::failure failure) {
        runner.fail(&failure);
    }
    std.test::check(runner.passing() == false, "a failed test does not pass");
    std.string::string failed = runner.finish();
    std.test::equal_text(failed, "FAILED: boom\n");
    std.string::string counted = runner.summary();
    std.test::equal_text(counted, "2 tests: 1 passed, 1 failed\n");
    std.test::equal(runner.status(), 1);
    /* start begins a test without a problem. */
    std.string::string third = runner.start("gamma");
    std.test::equal_text(third, "test gamma ... ");
    std.test::check(runner.passing(), "the next test starts without a problem");
    std.string::string again = runner.finish();
    std.test::equal_text(again, "ok\n");
    std.string::string last = runner.summary();
    std.test::equal_text(last, "3 tests: 2 passed, 1 failed\n");
    std.test::equal(runner.status(), 1);
}

@test
void runner_records_the_first_problem() throws std.test::failure, std.alloc::alloc_error {
    std.test::runner runner = std.test::runner::create();
    /* The first problem of a test counts. */
    std.string::string first = runner.start("first");
    std.test::equal_text(first, "test first ... ");
    try {
        std.test::fail("one");
    } catch (std.test::failure failure) {
        runner.fail(&failure);
    }
    runner.missing("std.convert::parse_error");
    runner.unexpected(Custom {.code = 1});
    std.string::string only_first = runner.finish();
    std.test::equal_text(only_first, "FAILED: one\n");
    /* A standard error is written with its portable name. */
    std.string::string second = runner.start("fault");
    std.test::equal_text(second, "test fault ... ");
    try {
        u32 value = std.convert::parse_u32("x", 10u32);
        value as void;
    } catch (std.error::fault problem) {
        runner.fault(problem);
    }
    std.string::string portable = runner.finish();
    std.test::equal_text(portable, "FAILED: error invalid_digit\n");
    std.string::string third = runner.start("memory");
    std.test::equal_text(third, "test memory ... ");
    std.test::fail_allocation_at(1u64);
    try {
        std.string::string text = std.string::from_str("abc");
        drop text;
    } catch (std.error::fault problem) {
        std.test::fail_allocation_at(0u64);
        runner.fault(problem);
    }
    std.test::fail_allocation_at(0u64);
    std.string::string memory = runner.finish();
    std.test::equal_text(memory, "FAILED: error out_of_memory\n");
    /* Another error is written with the name of its type. */
    std.string::string fourth = runner.start("custom");
    std.test::equal_text(fourth, "test custom ... ");
    runner.unexpected(Custom {.code = 7});
    std.string::string custom = runner.finish();
    std.test::equal_text(custom, "FAILED: error tests.std.test::Custom\n");
    std.string::string fifth = runner.start("typed");
    std.test::equal_text(fifth, "test typed ... ");
    try {
        u32 value = std.convert::parse_u32("y", 10u32);
        value as void;
    } catch (std.convert::parse_error failure) {
        runner.unexpected(failure);
    }
    std.string::string typed = runner.finish();
    std.test::equal_text(typed, "FAILED: error std.convert::parse_error\n");
    /* A missing error, and an expected one that records nothing. */
    std.string::string sixth = runner.start("missing");
    std.test::equal_text(sixth, "test missing ... ");
    runner.missing("tests.std.test::Custom");
    std.string::string missing = runner.finish();
    std.test::equal_text(missing, "FAILED: expected the error tests.std.test::Custom\n");
    std.string::string seventh = runner.start("expected");
    std.test::equal_text(seventh, "test expected ... ");
    runner.expected(Custom {.code = 2});
    std.test::check(runner.passing(), "an expected error is no problem");
    std.string::string expected = runner.finish();
    std.test::equal_text(expected, "ok\n");
    std.string::string summary = runner.summary();
    std.test::equal_text(summary, "7 tests: 1 passed, 6 failed\n");
    std.test::equal(runner.status(), 1);
}

@test
void runner_marks_allocation_runs() throws std.test::failure, std.alloc::alloc_error {
    std.test::runner runner = std.test::runner::create();
    /* A passing test that inject gave N attempts. */
    std.string::string first = runner.start("allocations");
    std.test::equal_text(first, "test allocations ... ");
    runner.inject(1u64, 3u64);
    runner.inject(2u64, 3u64);
    runner.inject(0u64, 3u64);
    std.test::check(runner.passing(), "the runs passed");
    std.string::string counted = runner.finish();
    std.test::equal_text(counted, "ok (3 allocation failures)\n");
    /* A problem after inject with a nonzero attempt names it. */
    std.string::string second = runner.start("failing");
    std.test::equal_text(second, "test failing ... ");
    runner.inject(2u64, 5u64);
    try {
        std.test::fail("boom");
    } catch (std.test::failure failure) {
        runner.fail(&failure);
    }
    std.string::string failed = runner.finish();
    std.test::equal_text(failed, "FAILED: boom (allocation 2 failing)\n");
    std.string::string third = runner.start("missing");
    std.test::equal_text(third, "test missing ... ");
    runner.inject(1u64, 1u64);
    runner.missing("E");
    std.string::string missing = runner.finish();
    std.test::equal_text(missing, "FAILED: expected the error E (allocation 1 failing)\n");
    /* start forgets the attempts, and attempt zero names none. */
    std.string::string fourth = runner.start("plain");
    std.test::equal_text(fourth, "test plain ... ");
    std.string::string plain = runner.finish();
    std.test::equal_text(plain, "ok\n");
    std.string::string fifth = runner.start("cleared");
    std.test::equal_text(fifth, "test cleared ... ");
    runner.inject(3u64, 4u64);
    runner.inject(0u64, 4u64);
    runner.missing("E");
    std.string::string cleared = runner.finish();
    std.test::equal_text(cleared, "FAILED: expected the error E\n");
    /* One attempt is written the same way. */
    std.string::string sixth = runner.start("single");
    std.test::equal_text(sixth, "test single ... ");
    runner.inject(1u64, 1u64);
    runner.inject(0u64, 1u64);
    std.string::string single = runner.finish();
    std.test::equal_text(single, "ok (1 allocation failures)\n");
    std.string::string summary = runner.summary();
    std.test::equal_text(summary, "6 tests: 3 passed, 3 failed\n");
    std.test::equal(runner.status(), 1);
}

@test
void counts_allocation_attempts() throws std.test::failure, std.alloc::alloc_error {
    std.test::fail_allocation_at(0u64);
    u64 before = std.test::allocation_attempts();
    std.string::string text = std.string::from_str("abc");
    u64 after_one = std.test::allocation_attempts();
    text.append("defghijklmnopqrstuvwxyz0123456789");
    u64 after_two = std.test::allocation_attempts();
    std.test::fail_allocation_at(0u64);
    u64 restarted = std.test::allocation_attempts();
    std.test::equal(before, 0u64);
    std.test::check(after_one >= 1u64, "from_str allocates");
    std.test::check(after_two > after_one, "a growing string allocates");
    std.test::equal(restarted, 0u64);
    std.test::equal_text(text, "abcdefghijklmnopqrstuvwxyz0123456789");
}

@test
void fails_the_chosen_allocation() throws std.test::failure, std.alloc::alloc_error {
    Caught caught = {.count = 0u32};
    std.test::fail_allocation_at(2u64);
    std.string::string first = std.string::from_str("abc");
    try {
        std.string::string second = std.string::from_str("def");
        drop second;
    } catch (std.alloc::alloc_error failure) {
        caught.count += 1u32;
        constexpr str name = std.error::name(std.error::from_alloc(failure));
        std.test::equal_text(name, "out_of_memory");
    }
    std.string::string third = std.string::from_str("ghi");
    u64 attempts = std.test::allocation_attempts();
    std.test::fail_allocation_at(0u64);
    std.test::equal(caught.count, 1u32);
    std.test::equal(attempts, 3u64);
    std.test::equal_text(first, "abc");
    std.test::equal_text(third, "ghi");
    /* Attempt one fails the next allocation. */
    std.test::fail_allocation_at(1u64);
    try {
        std.string::string next = std.string::from_str("jkl");
        drop next;
    } catch (std.alloc::alloc_error failure) {
        caught.count += 1u32;
        failure as void;
    }
    std.test::fail_allocation_at(0u64);
    std.test::equal(caught.count, 2u32);
}

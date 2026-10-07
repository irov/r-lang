module test.codegen.test_mode;
import std.test;

// R-FUNC-0025 and R-SLIB-TEST-0001..0003 (M24): the test entry runs each test function in
// declaration order: passing tests, a failed assertion, a standard error, another error, a test
// without effects, an expected error that is thrown and one that is not, an asynchronous test,
// allocation runs, and a protected test that calls a protected helper.

error Custom { i32 code; };

@test
void adds() throws std.test::failure, std.alloc::alloc_error {
    std.test::equal(2u64 + 2u64, 4u64);
    std.test::equal_text("abc", "abc");
    std.test::contains("hello world", "wor");
    std.test::not_equal(3i32, 4i32);
    std.test::check(true, "never");
}

@test
void compares() throws std.test::failure, std.alloc::alloc_error {
    std.test::equal(2u64 + 2u64, 5u64);
}

@test
void converts() throws std.convert::parse_error {
    u32 value = std.convert::parse_u32("x", 10u32);
    value as void;
}

@test
void customs() throws Custom {
    throw Custom {.code = 7};
}

@test
void quiet() {
}

@test(expect = std.convert::parse_error)
void expects() throws std.convert::parse_error {
    u32 value = std.convert::parse_u32("y", 10u32);
    value as void;
}

@test(expect = std.convert::parse_error)
void misses() throws std.convert::parse_error {
    u32 value = std.convert::parse_u32("12", 10u32);
    value as void;
}

@test
async void awaits() throws std.test::failure, std.error::fault {
    await std.time::sleep_for(std.time::duration_from_parts(0i64, 1000u32));
    std.test::equal_text("a", "b");
}

@test(allocations)
void builds() throws std.alloc::alloc_error, std.test::failure {
    std.string::string text = std.string::from_str("abc");
    text.append("def");
    std.test::equal_text(text, "abcdef");
}

protected u64 helper(u64 value) {
    return value * 2u64;
}

@test
protected void uses_helper() throws std.test::failure, std.alloc::alloc_error {
    std.test::equal(helper(21u64), 42u64);
}

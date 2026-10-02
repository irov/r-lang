module test.codegen.common_error_fields;

// Field access must prepare the public schema even before any erasure call is checked.
i32 inspect(std.error::error error) {
    std.error::domain domain = error.domain;
    if (domain != std.error::domain::conversion) { return 1; }
    if (error.code != 5u32 || error.native_code != 0i64) { return 2; }
    return 0;
}
async i32 inspect_async(std.error::error error) {
    std.error::domain domain = error.domain;
    if (domain != std.error::domain::conversion) { return 3; }
    if (error.code != 5u32 || error.native_code != 0i64) { return 4; }
    return 0;
}
async i32 main() {
    try {
        try {
            try { u8 number = std.convert::parse_u8("256", 10u32);
            number as void; }
            catch (std.convert::parse_error failure) {
                std.error::error error = std.error::from_parse(failure);
                i32 first = inspect(error);
                i32 second = await inspect_async(error);
                return first + second;
            }
        } catch (std.async::start_error failure) { throw TestAssertionFailed {.code = 8}; }
        throw TestAssertionFailed {.code = 9};
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };

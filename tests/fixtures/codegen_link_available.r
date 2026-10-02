module test.codegen.link_available;

protected bool has_link(constexpr str logical_name) {
    bool available = std.c::link_available(logical_name);
    return available;
}

i32 main() {
    try {
        bool system_available = has_link("system.libc");
        bool optional_available = has_link("optional-zlib");
        bool absent_available = has_link("absent");
        bool invalid_available = has_link("Invalid");

        if (system_available == false) {
            throw TestAssertionFailed {.code = 1};
        }
        if (optional_available == true) {
            throw TestAssertionFailed {.code = 2};
        }
        if (absent_available == true) {
            throw TestAssertionFailed {.code = 3};
        }
        if (invalid_available == true) {
            throw TestAssertionFailed {.code = 4};
        }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };

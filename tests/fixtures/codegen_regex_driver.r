module test.codegen.regex_driver;

import std.regex;

/* Compact result encoding for the bounded differential corpus (at most eight input bytes). */
i32 main(const str[] arguments) {
    try {
        if (len(arguments) == 1usize) { return 0; }
        if (len(arguments) != 4usize) { throw TestAssertionFailed {.code = 105}; }
        try {
            std.regex::regex expression = std.regex::compile(arguments[1]);
            str mode = arguments[3];
            const u8[] mode_bytes = mode;
            if (mode_bytes[0] == 102u8) {
                bool matched = std.regex::full_match(&expression, arguments[2]);
                i32 selected = matched == true ? 1 : 0;
                return selected;
            }
            o<std.regex::span> found = std.regex::find(&expression, arguments[2]);
            switch (found) {
            case variant o::some(value):
                return (value->start * 9usize + value->end + 1usize) as i32;
            case variant o::none: return 0;
            }
        } catch (std.regex::error failure) {
            if (failure.code == std.regex::error_code::step_limit) { throw TestAssertionFailed {.code = 108}; }
            throw TestAssertionFailed {.code = 106};
        } catch (std.alloc::alloc_error failure) {
            failure as void;
            throw TestAssertionFailed {.code = 107};
        }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };

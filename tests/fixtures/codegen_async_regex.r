module test.codegen.async_regex;

import test.regex_cases;
import std.regex;

protected async i32 verify(std.regex::regex compiled) throws std.regex::error, std.alloc::alloc_error {
    bool valid = std.regex::full_match(&compiled, "abc123");
    if (valid == false) { return 1; }
    i32 result = test.regex_cases::run();
    return result;
}

/* Two tasks search the same immutable program with independent matcher scratch. */
protected async i32 verify_shared(arc std.regex::regex compiled) {
    try {
        const std.regex::regex* view = &*compiled;
        usize iteration = 0usize;
        while (iteration < 32usize) {
            bool valid = std.regex::full_match(view, "abc123");
            if (valid == false) { return 1; }
            iteration += 1usize;
        }
        return 0;
    } catch (std.regex::error failure) {
        return 2;
    } catch (std.alloc::alloc_error failure) {
        return 3;
    }
}

async i32 main() {
    try {
        try {
            std.regex::regex compiled = std.regex::compile("[a-z]+[0-9]+");
            i32 result = await verify(move compiled);
            if (result != 0) { return result; }
            std.regex::regex reusable = std.regex::compile("[a-z]+[0-9]+");
            arc std.regex::regex shared = new arc std.regex::regex(move reusable);
            arc std.regex::regex peer = std.arc::clone(&shared);
            task<i32> first = verify_shared(move shared);
            task<i32> second = verify_shared(move peer);
            i32 first_result = await move first;
            i32 second_result = await move second;
            return first_result + second_result;
        } catch (std.async::start_error failure) {
            failure as void;
            throw TestAssertionFailed {.code = 220};
        } catch (std.regex::error failure) {
            failure as void;
            throw TestAssertionFailed {.code = 221};
        } catch (std.alloc::alloc_error failure) {
            failure as void;
            throw TestAssertionFailed {.code = 222};
        }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };

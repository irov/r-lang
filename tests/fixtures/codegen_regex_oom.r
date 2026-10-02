module test.codegen.regex_oom;

import std.regex;

// Retain values whose purpose here is type or lifetime coverage.
@generic<T> @noalloc @nonblocking
protected void test_observe(const T* value) { value as void; }


i32 main() {
    try {
        try {
            std.string::string source = std.string::from_str("(ab|cd){2,4}");
            str pattern = std.string::as_str(&source);
            std.regex::regex compiled = std.regex::compile(pattern);
            drop source;
            array<std.regex::span> matches = std.regex::find_all(&compiled, "abcd ababcd cdcd");
            if (len(matches) != 3usize) { throw TestAssertionFailed {.code = 1}; }
            std.string::string output = std.regex::replace_all(&compiled, "abcd ababcd cdcd", "replacement");
            test_observe(&output);
            array<std.string::string> fields = std.regex::split(&compiled, "abcd ababcd cdcd");
            if (len(fields) != 4usize) { throw TestAssertionFailed {.code = 2}; }
            return 0;
        } catch (std.alloc::alloc_error failure) {
            failure as void;
            throw TestAssertionFailed {.code = 99};
        } catch (std.regex::error failure) {
            failure as void;
            throw TestAssertionFailed {.code = 98};
        }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };

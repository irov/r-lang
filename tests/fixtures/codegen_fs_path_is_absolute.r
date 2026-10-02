module test.codegen.fs_path_is_absolute;

protected bool observe(const std.fs::path* source) {
    bool result = std.fs::path_is_absolute(source);
    return result;
}

async i32 main() {
    try {
        try {
            constexpr str absolute_literal = "/tmp/r-path";
            str absolute_text = absolute_literal;
            std.fs::path absolute = std.fs::path_from_utf8(absolute_text);
            bool absolute_direct = std.fs::path_is_absolute(&absolute);
            bool absolute_through_sync = observe(&absolute);
            if ((absolute_direct == false) || (absolute_through_sync == false)) {
                throw TestAssertionFailed {.code = 1};
            }

            constexpr str relative_literal = "tmp/r-path";
            str relative_text = relative_literal;
            std.fs::path relative = std.fs::path_from_utf8(relative_text);
            bool relative_direct = std.fs::path_is_absolute(&relative);
            if (relative_direct == true) {
                throw TestAssertionFailed {.code = 2};
            }

            constexpr str empty_literal = "";
            str empty_text = empty_literal;
            std.fs::path empty = std.fs::path_from_utf8(empty_text);
            bool empty_direct = std.fs::path_is_absolute(&empty);
            if (empty_direct == true) {
                throw TestAssertionFailed {.code = 3};
            }
            return 0;
        } catch (std.fs::path_error error) {
            error as void;
            throw TestAssertionFailed {.code = 4};
        }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };

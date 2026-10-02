module test.codegen.async_fs_open_directory;

// Retain values whose purpose here is type or lifetime coverage.
@generic<T> @noalloc @nonblocking
protected void test_observe(const T* value) { value as void; }


async i32 main(const str[] args) {
    try {
        usize argument_count = len(args);
        if (argument_count != 2) {
            throw TestAssertionFailed {.code = 20};
        }
        str input = args[1];
        try {
            std.fs::path path = std.fs::path_from_utf8(input);
            task<std.fs::directory throws std.fs::fs_error> operation =
                std.fs::open_directory(&path, o::none);
            std.fs::directory directory = await move operation;
            test_observe(&directory);
            return 0;
        } catch (std.fs::path_error error) {
            error as void;
            throw TestAssertionFailed {.code = 21};
        } catch (std.async::start_error error) {
            error as void;
            throw TestAssertionFailed {.code = 22};
        } catch (std.fs::fs_error error) {
            error as void;
            throw TestAssertionFailed {.code = 23};
        }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };

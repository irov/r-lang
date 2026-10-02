module test.codegen.fs_as_error;

protected std.error::error convert(std.fs::fs_error value) {
    std.error::error converted = std.fs::as_error(value);
    i64 retained_native_code = value.native_code;
    retained_native_code as void;
    return converted;
}

async i32 main() {
    try {
        std.fs::fs_error source = {
            .code = std.fs::error_code::closed,
            .native_code = -4294967298,
        };
        std.error::error direct = std.fs::as_error(source);
        if (source.native_code != -4294967298) {
            throw TestAssertionFailed {.code = 1};
        }
        std.error::error through_sync = convert(source);
        if (source.code != std.fs::error_code::closed) {
            throw TestAssertionFailed {.code = 2};
        }
        if (direct.domain != std.error::domain::filesystem) {
            throw TestAssertionFailed {.code = 3};
        }
        if (direct.code != 19) {
            throw TestAssertionFailed {.code = 4};
        }
        if (direct.native_code != -4294967298) {
            throw TestAssertionFailed {.code = 5};
        }
        if (through_sync.domain != std.error::domain::filesystem) {
            throw TestAssertionFailed {.code = 6};
        }
        if (through_sync.code != 19) {
            throw TestAssertionFailed {.code = 7};
        }
        if (through_sync.native_code != -4294967298) {
            throw TestAssertionFailed {.code = 8};
        }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };

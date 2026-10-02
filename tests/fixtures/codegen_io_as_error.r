module test.codegen.io_as_error;

protected std.error::error convert(std.io::io_error value) {
    std.error::error converted = std.io::as_error(value);
    converted as void;
    if (value.code != std.io::error_code::timed_out) {
        std.error::error fallback = {
            .domain = std.error::domain::io,
            .code = 0,
            .native_code = 0,
        };
        return fallback;
    }
    return converted;
}

async i32 main() {
    try {
        std.io::io_error source = {
            .code = std.io::error_code::timed_out,
            .native_code = -4294967297,
        };
        std.error::error direct = std.io::as_error(source);
        if (source.native_code != -4294967297) {
            throw TestAssertionFailed {.code = 1};
        }
        std.error::error through_sync = convert(source);
        if (source.code != std.io::error_code::timed_out) {
            throw TestAssertionFailed {.code = 2};
        }
        if (direct.domain != std.error::domain::io) {
            throw TestAssertionFailed {.code = 3};
        }
        if (direct.code != 6) {
            throw TestAssertionFailed {.code = 4};
        }
        if (direct.native_code != -4294967297) {
            throw TestAssertionFailed {.code = 5};
        }
        if (through_sync.domain != std.error::domain::io) {
            throw TestAssertionFailed {.code = 6};
        }
        if (through_sync.code != 6) {
            throw TestAssertionFailed {.code = 7};
        }
        if (through_sync.native_code != -4294967297) {
            throw TestAssertionFailed {.code = 8};
        }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };

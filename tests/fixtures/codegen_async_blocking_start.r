module test.codegen.async_blocking_start;

/* R-SLIB-ASYNC-0017 (L31): a failed start throws std.async::start_error and leaves the staged
   argument with the caller, who may use it again. The C wrapper fails the first start with
   allocation_failed and the second with runtime_stopping. */

usize measure(std.string::string text) {
    return text.len();
}

async i32 main() {
    std.string::string text = std.string::from_str("kept");
    i32 status = 0;
    try {
        usize first = await std.async::blocking(measure, move text);
        first as void;
        return 1;
    } catch (std.async::start_error failure) {
        if (failure != std.async::start_error::allocation_failed) { status += 2; }
    }
    try {
        usize second = await std.async::blocking(measure, move text);
        second as void;
        return 4;
    } catch (std.async::start_error failure) {
        if (failure != std.async::start_error::runtime_stopping) { status += 8; }
    }
    usize length = await std.async::blocking(measure, move text);
    if (length != 4usize) { status += 16; }
    return status;
}

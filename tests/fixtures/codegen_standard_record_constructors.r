module test.codegen.standard_record_constructors;

// M21-1: the public-field records of the C modules are built with `Type {...}` like a
// program's own structs, in synchronous and asynchronous code and as arguments and results;
// std.c::target_info also by a typed initializer. M21-2: values of a natively implemented
// fieldless enum compare where no variant of the enum is named.

protected std.time::system_time moment(i64 seconds) {
    return std.time::system_time {.unix_seconds = seconds, .nanoseconds = 5u32};
}

protected bool same_code(std.time::error_code left, std.time::error_code right) {
    return left == right;
}

protected i32 caught(std.time::error_code expected) {
    try {
        std.time::to_utc(std.time::system_time {.unix_seconds = 0i64, .nanoseconds = 1000000000u32})
            as void;
        return 1;
    } catch (std.time::time_error failure) {
        if (failure.code != expected) { return 2; }
        if (same_code(failure.code, expected) == false) { return 3; }
        return 0;
    }
}

protected i32 records() throws std.time::time_error {
    std.time::system_time start = moment(784111777i64);
    if (start.unix_seconds != 784111777i64 || start.nanoseconds != 5u32) { return 11; }
    std.time::utc_datetime fields = std.time::utc_datetime {
        .year = 1994, .month = 11u8, .day = 6u8, .hour = 8u8, .minute = 49u8, .second = 37u8,
        .nanosecond = 5u32};
    std.time::system_time back = std.time::from_utc(fields);
    if (back.unix_seconds != 784111777i64) { return 12; }
    std.process::stdio pipes = std.process::stdio {.input = std.process::pipe_mode::inherit,
        .output = std.process::pipe_mode::piped, .error = std.process::pipe_mode::null_device};
    if (pipes.output != std.process::pipe_mode::piped) { return 13; }
    std.process::exit_status status = std.process::exit_status {
        .kind = std.process::termination_kind::exited, .code = 3, .success = false};
    if (status.code != 3 || status.success == true) { return 14; }
    std.c::target_info expected = std.c::target_info {.pointer_bits = 64u32,
        .c_wint_available = true, .c_long_double_available = true, .hosted_native_async = true};
    std.c::target_info typed = {.pointer_bits = 64u32, .c_wint_available = true,
        .c_long_double_available = true, .hosted_native_async = true};
    std.c::target_info actual = std.c::target();
    if (expected.pointer_bits != actual.pointer_bits || typed.pointer_bits != 64u32) { return 15; }
    std.fs::metadata entry = std.fs::metadata {.kind = std.fs::file_kind::regular, .size = 7u64,
        .created = o::none, .modified = o::some(start), .accessed = o::none};
    if (entry.size != 7u64) { return 16; }
    return caught(std.time::error_code::invalid_value);
}

protected async i32 pause() { return 0; }

protected async i32 later(i64 seconds) throws std.async::start_error, std.time::time_error {
    std.time::system_time kept = std.time::system_time {.unix_seconds = seconds, .nanoseconds = 0u32};
    i32 status = await pause();
    std.time::utc_datetime fields = std.time::to_utc(kept);
    status += await pause();
    std.time::system_time rebuilt = std.time::system_time {.unix_seconds = kept.unix_seconds + 1i64,
        .nanoseconds = 9u32};
    status += await pause();
    if (fields.year != 1994 || rebuilt.unix_seconds != seconds + 1i64 || rebuilt.nanoseconds != 9u32) {
        status += 21;
    }
    return status;
}

async i32 main() {
    try {
        i32 sync_status = records();
        if (sync_status != 0) { return sync_status; }
        return await later(784111777i64);
    } catch (std.time::time_error failure) {
        failure as void;
        return 98;
    } catch (std.async::start_error failure) {
        failure as void;
        return 99;
    }
}

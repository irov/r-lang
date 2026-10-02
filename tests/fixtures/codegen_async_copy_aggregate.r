module test.codegen.async_copy_aggregate;

struct Range {
    u32 begin;
    u32 end;
};

struct Report {
    u32 code;
    constexpr str message;
};

protected async u32 one() {
    return 1;
}

async u32 inspect(Range range, Report report) {
    str runtime_message = report.message;
    const u8[] message_bytes = runtime_message;
    usize message_length = len(message_bytes);
    u32 marker = range.begin + range.end + report.code;
    const u32* marker_view = &marker;
    if (message_length != 4) {
        return 0;
    }
    u32 before_wait = *marker_view;
    try {
        task<u32> operation = one();
        u32 awaited = await move operation;
        return before_wait + awaited;
    } catch (std.async::start_error error) {
        error as void;
        return 0;
    }
}

async i32 main() {
    Range range = {
        .begin = 10,
        .end = 20,
    };
    Report report = {
        .code = 7,
        .message = "copy",
    };
    try {
        task<u32> operation = inspect(range, report);
        u32 value = await move operation;
        if (value != 38) {
            return 1;
        }
        return 0;
    } catch (std.async::start_error error) {
        error as void;
        return 2;
    }
}

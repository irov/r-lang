module example.clock.operations;
import std.time;

std.string::string calendar(std.time::system_time instant)
    throws std.time::time_error, std.alloc::alloc_error {
    std.time::utc_datetime utc = instant.to_utc();
    std.time::system_time roundtrip = std.time::from_utc(utc);
    std.string::string output = f"utc={utc.year} {utc.month} {utc.day} {utc.hour} {utc.minute} {utc.second} {utc.nanosecond}\nunix={roundtrip.unix_seconds} {roundtrip.nanoseconds}\n";
    return move output;
}

std.string::string duration(std.time::duration value) throws std.alloc::alloc_error {
    i64 seconds = value.seconds();
    u32 nanoseconds = value.nanoseconds();
    std.string::string output = f"seconds={seconds} nanoseconds={nanoseconds}\n";
    return move output;
}

async std.string::string delay(u32 milliseconds)
    throws std.time::time_error, std.time::duration_error, std.alloc::alloc_error, std.async::start_error {
    std.time::instant started = std.time::monotonic_now();
    i64 seconds = (milliseconds / 1000u32) as i64;
    u32 nanoseconds = (milliseconds % 1000u32) * 1000000u32;
    std.time::duration total = std.time::duration_from_parts(seconds, nanoseconds);
    std.time::instant deadline = started.add(total);
    // The first sleep may complete before the deadline; the second waits for the remaining time.
    std.time::duration begin = std.time::duration_from_seconds(0i64);
    await begin.sleep_for();
    await deadline.sleep_until();
    std.time::instant finished = std.time::monotonic_now();
    std.time::duration elapsed = finished.duration(started);
    std.string::string output = duration(elapsed);
    return move output;
}

/* A text that begins with a digit is an RFC 3339 date-time, any other an HTTP date; each is
   printed as Unix time and in the other form. */
std.string::string parsed(str text)
    throws std.convert::parse_error, std.time::time_error, std.alloc::alloc_error {
    const u8[] bytes = text;
    if (len(bytes) != 0usize && bytes[0] >= 48u8 && bytes[0] <= 57u8) {
        std.time::system_time instant = std.time::parse_rfc3339(text);
        std.string::string http = std.time::format_http_date(instant);
        return f"unix={instant.unix_seconds} {instant.nanoseconds}\nhttp={http}\n";
    }
    std.time::system_time instant = std.time::parse_http_date(text);
    std.string::string exact = std.time::format_rfc3339(instant, 0u32);
    return f"unix={instant.unix_seconds} {instant.nanoseconds}\nrfc3339={exact}\n";
}

/* The numbers of count ticks of an interval and the milliseconds from its start to the last;
   a pause after the first tick makes the next one late, so it completes at once and the ticks
   between are skipped. */
async std.string::string ticked(u32 count, u32 period, u32 pause)
    throws std.time::time_error, std.time::duration_error, std.alloc::alloc_error,
        std.async::start_error {
    std.time::instant started = std.time::monotonic_now();
    std.time::interval ticker = std.time::interval::every(
        std.time::duration_from_parts((period / 1000u32) as i64, (period % 1000u32) * 1000000u32));
    std.string::string output = std.string::from_str("ticks:");
    task_scope(1) scope {
        u64 first = await ticker.tick();
        std.string::string head = f" {first}";
        std.string::append_str(&output, head.as_str());
        if (pause != 0u32) {
            await std.time::sleep_for(std.time::duration_from_parts(
                (pause / 1000u32) as i64, (pause % 1000u32) * 1000000u32));
        }
        for (u32 index = 1u32; index < count; index += 1u32) {
            u64 number = await ticker.tick();
            std.string::string item = f" {number}";
            std.string::append_str(&output, item.as_str());
        }
    }
    std.time::duration elapsed = std.time::monotonic_now().duration(started);
    i64 spent = elapsed.seconds() * 1000i64 + (elapsed.nanoseconds() / 1000000u32) as i64;
    std.string::string tail = f"\nelapsed={spent} ms\n";
    std.string::append_str(&output, tail.as_str());
    return move output;
}

module tests.std.time;
import std.test;
import std.time;

// The tests of the R part of std.time (Library R-SLIB-TIME-0006..0009): the RFC 3339 and HTTP
// date forms written and read back with the code and index of their errors, and the periodic
// interval on the monotonic clock. Run in test mode (Core R-FUNC-0025).

protected std.time::system_time at(i64 seconds, u32 nanoseconds) {
    return std.time::system_time {.unix_seconds = seconds, .nanoseconds = nanoseconds};
}

protected void writes_rfc3339(i64 seconds, u32 nanoseconds, u32 digits, str expected)
    throws std.time::time_error, std.test::failure, std.alloc::alloc_error {
    std.string::string text = std.time::format_rfc3339(at(seconds, nanoseconds), digits);
    std.test::equal_text(text.as_str(), expected);
}

protected void writes_http(i64 seconds, u32 nanoseconds, str expected)
    throws std.time::time_error, std.test::failure, std.alloc::alloc_error {
    std.string::string text = std.time::format_http_date(at(seconds, nanoseconds));
    std.test::equal_text(text.as_str(), expected);
}

protected void refuses(i64 seconds, u32 nanoseconds, u32 digits, bool http,
                       std.time::error_code code) throws std.test::failure, std.alloc::alloc_error {
    try {
        if (http == true) {
            std.time::format_http_date(at(seconds, nanoseconds)) as void;
        } else {
            std.time::format_rfc3339(at(seconds, nanoseconds), digits) as void;
        }
        std.test::fail("the time has no text form");
    } catch (std.time::time_error failure) {
        std.test::check(failure.code == code, "the code of the refusal");
        std.test::equal(failure.native_code, 0i64);
    }
}

protected void reads_rfc3339(str text, i64 seconds, u32 nanoseconds)
    throws std.convert::parse_error, std.test::failure, std.alloc::alloc_error {
    std.time::system_time value = std.time::parse_rfc3339(text);
    std.test::equal(value.unix_seconds, seconds);
    std.test::equal(value.nanoseconds, nanoseconds);
}

protected void reads_http(str text, i64 seconds)
    throws std.convert::parse_error, std.test::failure, std.alloc::alloc_error {
    std.time::system_time value = std.time::parse_http_date(text);
    std.test::equal(value.unix_seconds, seconds);
    std.test::equal(value.nanoseconds, 0u32);
}

protected void rejects(str text, bool http, std.convert::parse_error_code code, usize index)
    throws std.test::failure, std.alloc::alloc_error {
    try {
        if (http == true) {
            std.time::parse_http_date(text) as void;
        } else {
            std.time::parse_rfc3339(text) as void;
        }
        std.string::string message = f"{text} is not a valid time";
        std.test::fail(message.as_str());
    } catch (std.convert::parse_error failure) {
        std.string::string message = f"the code of the error in {text}";
        std.test::check(failure.code == code, message.as_str());
        std.test::equal(failure.index, index);
    }
}

@test(allocations)
void formats_rfc3339()
    throws std.time::time_error, std.test::failure, std.alloc::alloc_error {
    writes_rfc3339(1790685296i64, 0u32, 0u32, "2026-09-29T12:34:56Z");
    writes_rfc3339(1790685296i64, 123456789u32, 3u32, "2026-09-29T12:34:56.123Z");
    writes_rfc3339(1790685296i64, 999999999u32, 2u32, "2026-09-29T12:34:56.99Z");
    writes_rfc3339(1790685296i64, 5000u32, 6u32, "2026-09-29T12:34:56.000005Z");
    writes_rfc3339(0i64, 1u32, 9u32, "1970-01-01T00:00:00.000000001Z");
    writes_rfc3339(-14182940i64, 0u32, 0u32, "1969-07-20T20:17:40Z");
    writes_rfc3339(951782400i64, 0u32, 1u32, "2000-02-29T00:00:00.0Z");
    writes_rfc3339(253402300799i64, 0u32, 0u32, "9999-12-31T23:59:59Z");
}

@test
void formats_http_dates() throws std.time::time_error, std.test::failure, std.alloc::alloc_error {
    writes_http(1790685296i64, 0u32, "Tue, 29 Sep 2026 12:34:56 GMT");
    writes_http(784111777i64, 999999999u32, "Sun, 06 Nov 1994 08:49:37 GMT");
    writes_http(0i64, 0u32, "Thu, 01 Jan 1970 00:00:00 GMT");
    writes_http(-14182940i64, 0u32, "Sun, 20 Jul 1969 20:17:40 GMT");
    writes_http(4107542400i64, 0u32, "Mon, 01 Mar 2100 00:00:00 GMT");
}

@test
void refuses_times_without_a_text_form() throws std.test::failure, std.alloc::alloc_error {
    refuses(0i64, 0u32, 10u32, false, std.time::error_code::invalid_value);
    refuses(0i64, 1000000000u32, 3u32, false, std.time::error_code::invalid_value);
    refuses(0i64, 1000000000u32, 0u32, true, std.time::error_code::invalid_value);
    refuses(253402300800i64, 0u32, 0u32, false, std.time::error_code::overflow);
    refuses(-62167219201i64, 0u32, 0u32, false, std.time::error_code::overflow);
    refuses(253402300800i64, 0u32, 0u32, true, std.time::error_code::overflow);
}

@test
void parses_rfc3339() throws std.convert::parse_error, std.test::failure, std.alloc::alloc_error {
    reads_rfc3339("2026-09-29T12:34:56Z", 1790685296i64, 0u32);
    reads_rfc3339("2026-09-29t14:34:56.5+02:00", 1790685296i64, 500000000u32);
    reads_rfc3339("2026-09-29 02:04:56.123456789123-10:30", 1790685296i64, 123456789u32);
    reads_rfc3339("2026-09-29T12:34:56.000001z", 1790685296i64, 1000u32);
    reads_rfc3339("1970-01-01T00:00:00-00:00", 0i64, 0u32);
    reads_rfc3339("1969-07-20T20:17:40Z", -14182940i64, 0u32);
    reads_rfc3339("2000-02-29T00:00:00Z", 951782400i64, 0u32);
    std.convert::parse_error_code digit = std.convert::parse_error_code::invalid_digit;
    std.convert::parse_error_code high = std.convert::parse_error_code::above_maximum;
    std.convert::parse_error_code low = std.convert::parse_error_code::below_minimum;
    rejects("", false, std.convert::parse_error_code::empty, 0usize);
    rejects("2026-09-29", false, digit, 10usize);
    rejects("2026-09-29T12:34:56", false, digit, 19usize);
    rejects("2026-09-29X12:34:56Z", false, digit, 10usize);
    rejects("2026-9-29T12:34:56Z", false, digit, 6usize);
    rejects("2026-09-29T12:34:56.Z", false, digit, 20usize);
    rejects("2026-09-29T12:34:56Zjunk", false,
            std.convert::parse_error_code::trailing_character, 20usize);
    rejects("2026-00-10T00:00:00Z", false, low, 5usize);
    rejects("2026-13-01T00:00:00Z", false, high, 5usize);
    rejects("2026-02-29T00:00:00Z", false, high, 8usize);
    rejects("2026-09-29T12:34:60Z", false, high, 17usize);
    rejects("2026-09-29T12:34:56+24:00", false, high, 20usize);
}

@test
void parses_http_dates()
    throws std.convert::parse_error, std.test::failure, std.alloc::alloc_error {
    reads_http("Tue, 29 Sep 2026 12:34:56 GMT", 1790685296i64);
    reads_http("Tuesday, 29-Sep-26 12:34:56 GMT", 1790685296i64);
    reads_http("Tue Sep 29 12:34:56 2026", 1790685296i64);
    reads_http("Sun Nov  6 08:49:37 1994", 784111777i64);
    reads_http("Tuesday, 31-Dec-69 23:59:59 GMT", 3155759999i64);
    reads_http("Thursday, 01-Jan-70 00:00:00 GMT", 0i64);
    reads_http("Sun, 20 Jul 1969 20:17:40 GMT", -14182940i64);
    std.convert::parse_error_code digit = std.convert::parse_error_code::invalid_digit;
    rejects("", true, std.convert::parse_error_code::empty, 0usize);
    rejects("Mon, 29 Sep 2026 12:34:56 GMT", true, digit, 0usize);
    rejects("Tue, 29 sep 2026 12:34:56 GMT", true, digit, 8usize);
    rejects("Tue, 29 Sep 2026 12:34:56 UTC", true, digit, 26usize);
    rejects("Tue, 29 Sep 2026 12:34:56 GMT ", true,
            std.convert::parse_error_code::trailing_character, 29usize);
    rejects("Tue, 31 Sep 2026 12:34:56 GMT", true,
            std.convert::parse_error_code::above_maximum, 5usize);
    rejects("Tue Sep 29 12:34", true, digit, 16usize);
}

@test
void round_trips_through_text()
    throws std.time::time_error, std.convert::parse_error, std.test::failure,
           std.alloc::alloc_error {
    std.time::system_time now = std.time::system_now();
    std.string::string exact = std.time::format_rfc3339(now, 9u32);
    std.time::system_time back = std.time::parse_rfc3339(exact.as_str());
    std.test::equal(back.unix_seconds, now.unix_seconds);
    std.test::equal(back.nanoseconds, now.nanoseconds);
    std.string::string http = std.time::format_http_date(now);
    std.time::system_time whole = std.time::parse_http_date(http.as_str());
    std.test::equal(whole.unix_seconds, now.unix_seconds);
    std.test::equal(whole.nanoseconds, 0u32);
    std.string::string again = std.time::format_http_date(whole);
    std.test::equal_text(again.as_str(), http.as_str());
}

protected i64 nanoseconds_since(std.time::instant start) throws std.time::time_error {
    std.time::instant now = std.time::monotonic_now();
    std.time::duration elapsed = std.time::instant_duration(now, start);
    return std.time::duration_seconds(elapsed) * 1000000000i64 +
           std.time::duration_nanoseconds(elapsed) as i64;
}

protected std.time::duration milliseconds(u32 count) throws std.time::duration_error {
    return std.time::duration_from_parts((count / 1000u32) as i64, (count % 1000u32) * 1000000u32);
}

protected void refuses_period(std.time::duration period)
    throws std.test::failure, std.alloc::alloc_error {
    try {
        std.time::interval::every(period) as void;
        std.test::fail("the period is not positive and below 2^63 nanoseconds");
    } catch (std.time::time_error failure) {
        std.test::check(failure.code == std.time::error_code::invalid_value, "invalid_value");
    }
}

@test
async void ticks_on_the_grid_of_its_start()
    throws std.time::time_error, std.time::duration_error, std.async::start_error,
           std.test::failure, std.alloc::alloc_error {
    refuses_period(std.time::duration_from_seconds(0i64));
    refuses_period(std.time::duration_from_seconds(-1i64));
    refuses_period(std.time::duration_from_parts(9223372036i64, 854775808u32));
    std.time::interval::every(std.time::duration_from_parts(9223372036i64, 854775807u32)) as void;
    i64 period = 50000000i64;
    std.time::instant start = std.time::monotonic_now();
    std.time::interval ticker = std.time::interval::every(milliseconds(50u32));
    task_scope(1) scope {
        u64 first = await ticker.tick();
        i64 first_at = nanoseconds_since(start);
        std.test::equal(first, 1u64);
        std.test::check(first_at >= period, "tick 1 comes after one period");
        u64 second = await ticker.tick();
        i64 second_at = nanoseconds_since(start);
        std.test::equal(second, 2u64);
        std.test::check(second_at >= 2i64 * period, "tick 2 comes after two periods");
        await std.time::sleep_for(milliseconds(175u32));
        u64 late = await ticker.tick();
        i64 late_at = nanoseconds_since(start);
        std.test::check(late >= 5u64, "a delay skips the ticks that were due");
        std.test::check((late as i64) * period <= late_at, "the late tick is not early");
        u64 next = await ticker.tick();
        i64 next_at = nanoseconds_since(start);
        std.test::check(next > late, "tick numbers grow");
        std.test::check((next as i64) * period <= next_at, "the next tick keeps the grid");
    }
}

struct Outcome { bool ticked; };

@test
async void a_cancelled_tick_leaves_the_interval()
    throws std.time::time_error, std.time::duration_error, std.async::start_error,
           std.test::failure, std.alloc::alloc_error {
    std.time::interval slow = std.time::interval::every(milliseconds(150u32));
    std.time::instant soon = std.time::monotonic_now().add(milliseconds(10u32));
    Outcome outcome = {.ticked = false};
    task_scope(1) group {
        auto pending = slow.tick();
        select (group) {
        case u64 number = await move pending:
            number as void;
            outcome.ticked = true;
        case until (soon): break;
        }
        group.cancel_all();
        await group.all();
    }
    std.test::check(outcome.ticked == false, "the tick loses to the earlier limit");
    task_scope(1) scope {
        u64 first = await slow.tick();
        std.test::equal(first, 1u64);
    }
}

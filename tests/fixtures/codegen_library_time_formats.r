module test.codegen.library_time_formats;

import std.time;

// R-SLIB-TIME-0006..0008 (M21): RFC 3339 and HTTP dates written and read back: fractions,
// offsets, the calendar, the three HTTP forms, the limits of the writable years and the code
// and index of every kind of error.

protected bool same(str left, str right) { return std.bytes::equal(left, right); }

protected i32 writes_rfc3339(i64 seconds, u32 nanoseconds, u32 digits, str expected)
    throws std.alloc::alloc_error {
    std.time::system_time value =
        std.time::system_time {.unix_seconds = seconds, .nanoseconds = nanoseconds};
    try {
        std.string::string text = std.time::format_rfc3339(value, digits);
        if (same(text, expected) == false) { return 1; }
        return 0;
    } catch (std.time::time_error failure) {
        failure as void;
        return 2;
    }
}

protected i32 writes_http(i64 seconds, str expected) throws std.alloc::alloc_error {
    std.time::system_time value = std.time::system_time {.unix_seconds = seconds, .nanoseconds = 0u32};
    try {
        std.string::string text = std.time::format_http_date(value);
        if (same(text, expected) == false) { return 1; }
        return 0;
    } catch (std.time::time_error failure) {
        failure as void;
        return 2;
    }
}

protected i32 refuses(i64 seconds, u32 nanoseconds, u32 digits, bool http,
    std.time::error_code code) throws std.alloc::alloc_error {
    std.time::system_time value =
        std.time::system_time {.unix_seconds = seconds, .nanoseconds = nanoseconds};
    try {
        if (http == true) {
            std.time::format_http_date(value) as void;
        } else {
            std.time::format_rfc3339(value, digits) as void;
        }
        return 1;
    } catch (std.time::time_error failure) {
        if (failure.code != code) { return 2; }
        return 0;
    }
}

protected i32 reads_rfc3339(str text, i64 seconds, u32 nanoseconds) {
    try {
        std.time::system_time value = std.time::parse_rfc3339(text);
        if (value.unix_seconds != seconds) { return 1; }
        if (value.nanoseconds != nanoseconds) { return 2; }
        return 0;
    } catch (std.convert::parse_error failure) {
        failure as void;
        return 3;
    }
}

protected i32 reads_http(str text, i64 seconds) {
    try {
        std.time::system_time value = std.time::parse_http_date(text);
        if (value.unix_seconds != seconds) { return 1; }
        if (value.nanoseconds != 0u32) { return 2; }
        return 0;
    } catch (std.convert::parse_error failure) {
        failure as void;
        return 3;
    }
}

protected i32 rejects(str text, bool http, std.convert::parse_error_code code, usize index) {
    try {
        if (http == true) {
            std.time::parse_http_date(text) as void;
        } else {
            std.time::parse_rfc3339(text) as void;
        }
        return 1;
    } catch (std.convert::parse_error failure) {
        if (failure.code != code) { return 2; }
        if (failure.index != index) { return 3; }
        return 0;
    }
}

protected i32 check_writing() throws std.alloc::alloc_error {
    if (writes_rfc3339(784111777i64, 123456789u32, 0u32, "1994-11-06T08:49:37Z") != 0) { return 1; }
    if (writes_rfc3339(784111777i64, 123456789u32, 1u32, "1994-11-06T08:49:37.1Z") != 0) { return 2; }
    if (writes_rfc3339(784111777i64, 123456789u32, 3u32, "1994-11-06T08:49:37.123Z") != 0) { return 3; }
    if (writes_rfc3339(784111777i64, 123456789u32, 9u32, "1994-11-06T08:49:37.123456789Z") != 0) {
        return 4;
    }
    if (writes_rfc3339(0i64, 5u32, 9u32, "1970-01-01T00:00:00.000000005Z") != 0) { return 5; }
    if (writes_rfc3339(-1i64, 999999999u32, 2u32, "1969-12-31T23:59:59.99Z") != 0) { return 6; }
    if (writes_rfc3339(-62167219200i64, 0u32, 0u32, "0000-01-01T00:00:00Z") != 0) { return 7; }
    if (writes_rfc3339(253402300799i64, 0u32, 0u32, "9999-12-31T23:59:59Z") != 0) { return 8; }
    if (writes_rfc3339(1709208000i64, 0u32, 0u32, "2024-02-29T12:00:00Z") != 0) { return 9; }
    if (writes_http(784111777i64, "Sun, 06 Nov 1994 08:49:37 GMT") != 0) { return 10; }
    if (writes_http(0i64, "Thu, 01 Jan 1970 00:00:00 GMT") != 0) { return 11; }
    if (writes_http(-1i64, "Wed, 31 Dec 1969 23:59:59 GMT") != 0) { return 12; }
    if (writes_http(951782400i64, "Tue, 29 Feb 2000 00:00:00 GMT") != 0) { return 13; }
    if (writes_http(-62167219200i64, "Sat, 01 Jan 0000 00:00:00 GMT") != 0) { return 14; }
    std.time::error_code overflow = std.time::error_code::overflow;
    std.time::error_code invalid = std.time::error_code::invalid_value;
    if (refuses(253402300800i64, 0u32, 0u32, false, overflow) != 0) { return 15; }
    if (refuses(-62167219201i64, 0u32, 0u32, false, overflow) != 0) { return 16; }
    if (refuses(253402300800i64, 0u32, 0u32, true, overflow) != 0) { return 17; }
    if (refuses(0i64, 0u32, 10u32, false, invalid) != 0) { return 18; }
    if (refuses(0i64, 1000000000u32, 0u32, false, invalid) != 0) { return 19; }
    if (refuses(0i64, 1000000000u32, 0u32, true, invalid) != 0) { return 20; }
    return 0;
}

protected i32 check_rfc3339() {
    if (reads_rfc3339("1994-11-06T08:49:37.123456789Z", 784111777i64, 123456789u32) != 0) { return 31; }
    if (reads_rfc3339("1994-11-06t10:19:37+01:30", 784111777i64, 0u32) != 0) { return 32; }
    if (reads_rfc3339("1994-11-05 23:49:37.5-09:00", 784111777i64, 500000000u32) != 0) { return 33; }
    if (reads_rfc3339("1994-11-06T08:49:37-00:00", 784111777i64, 0u32) != 0) { return 34; }
    if (reads_rfc3339("1970-01-01T00:00:00.9999999999z", 0i64, 999999999u32) != 0) { return 35; }
    if (reads_rfc3339("2024-02-29T12:00:00Z", 1709208000i64, 0u32) != 0) { return 36; }
    if (reads_rfc3339("2000-02-29T00:00:00Z", 951782400i64, 0u32) != 0) { return 37; }
    if (reads_rfc3339("0000-01-01T00:00:00Z", -62167219200i64, 0u32) != 0) { return 38; }
    if (reads_rfc3339("9999-12-31T23:59:59.999999999Z", 253402300799i64, 999999999u32) != 0) {
        return 39;
    }
    std.convert::parse_error_code digit = std.convert::parse_error_code::invalid_digit;
    std.convert::parse_error_code trailing = std.convert::parse_error_code::trailing_character;
    std.convert::parse_error_code low = std.convert::parse_error_code::below_minimum;
    std.convert::parse_error_code high = std.convert::parse_error_code::above_maximum;
    if (rejects("", false, std.convert::parse_error_code::empty, 0usize) != 0) { return 40; }
    if (rejects("199", false, digit, 3usize) != 0) { return 41; }
    if (rejects("1994/11-06T08:49:37Z", false, digit, 4usize) != 0) { return 42; }
    if (rejects("1994-11-06", false, digit, 10usize) != 0) { return 43; }
    if (rejects("1994-11-06X08:49:37Z", false, digit, 10usize) != 0) { return 44; }
    if (rejects("1994-11-06T08:49:37", false, digit, 19usize) != 0) { return 45; }
    if (rejects("1994-11-06T08:49:37.Z", false, digit, 20usize) != 0) { return 46; }
    if (rejects("1994-11-06T08:49:37.", false, digit, 20usize) != 0) { return 47; }
    if (rejects("1994-11-06T08:49:37Zx", false, trailing, 20usize) != 0) { return 48; }
    if (rejects("1994-11-06T08:49:37+0100", false, digit, 22usize) != 0) { return 49; }
    if (rejects("1994-11-06T08:49:37+01:0", false, digit, 24usize) != 0) { return 50; }
    if (rejects("1994-00-06T08:49:37Z", false, low, 5usize) != 0) { return 51; }
    if (rejects("1994-13-06T08:49:37Z", false, high, 5usize) != 0) { return 52; }
    if (rejects("1994-11-00T08:49:37Z", false, low, 8usize) != 0) { return 53; }
    if (rejects("1994-11-31T08:49:37Z", false, high, 8usize) != 0) { return 54; }
    if (rejects("2023-02-29T08:49:37Z", false, high, 8usize) != 0) { return 55; }
    if (rejects("1900-02-29T08:49:37Z", false, high, 8usize) != 0) { return 56; }
    if (rejects("1994-11-06T24:00:00Z", false, high, 11usize) != 0) { return 57; }
    if (rejects("1994-11-06T08:60:00Z", false, high, 14usize) != 0) { return 58; }
    if (rejects("1994-11-06T08:49:60Z", false, high, 17usize) != 0) { return 59; }
    if (rejects("1994-11-06T08:49:37+24:00", false, high, 20usize) != 0) { return 60; }
    if (rejects("1994-11-06T08:49:37-01:60", false, high, 23usize) != 0) { return 61; }
    /* Syntax before ranges, and ranges in text order. */
    if (rejects("1994-13-06T08:49:37Zx", false, trailing, 20usize) != 0) { return 62; }
    if (rejects("1994-02-30T25:00:00Z", false, high, 8usize) != 0) { return 63; }
    return 0;
}

protected i32 check_http() {
    if (reads_http("Sun, 06 Nov 1994 08:49:37 GMT", 784111777i64) != 0) { return 71; }
    if (reads_http("Sunday, 06-Nov-94 08:49:37 GMT", 784111777i64) != 0) { return 72; }
    if (reads_http("Sun Nov  6 08:49:37 1994", 784111777i64) != 0) { return 73; }
    if (reads_http("Wed Nov 16 00:00:00 1994", 784944000i64) != 0) { return 74; }
    if (reads_http("Thursday, 01-Jan-26 00:00:00 GMT", 1767225600i64) != 0) { return 75; }
    if (reads_http("Thursday, 01-Jan-70 00:00:00 GMT", 0i64) != 0) { return 76; }
    if (reads_http("Tuesday, 01-Jan-69 00:00:00 GMT", 3124224000i64) != 0) { return 77; }
    if (reads_http("Thu, 29 Feb 2024 00:00:00 GMT", 1709164800i64) != 0) { return 78; }
    std.convert::parse_error_code digit = std.convert::parse_error_code::invalid_digit;
    std.convert::parse_error_code trailing = std.convert::parse_error_code::trailing_character;
    std.convert::parse_error_code high = std.convert::parse_error_code::above_maximum;
    if (rejects("", true, std.convert::parse_error_code::empty, 0usize) != 0) { return 80; }
    if (rejects("Su", true, digit, 2usize) != 0) { return 81; }
    if (rejects("Sux, 06 Nov 1994 08:49:37 GMT", true, digit, 2usize) != 0) { return 82; }
    if (rejects("sun, 06 Nov 1994 08:49:37 GMT", true, digit, 0usize) != 0) { return 83; }
    if (rejects("Mon, 06 Nov 1994 08:49:37 GMT", true, digit, 0usize) != 0) { return 84; }
    if (rejects("Sun, 06 Nox 1994 08:49:37 GMT", true, digit, 10usize) != 0) { return 85; }
    if (rejects("Sun, 06 Nov 1994 08:49:37 UTC", true, digit, 26usize) != 0) { return 86; }
    if (rejects("Sun, 06 Nov 1994 08:49:37 GMT ", true, trailing, 29usize) != 0) { return 87; }
    if (rejects("Sun, 06 Nov 1994 08:49", true, digit, 22usize) != 0) { return 88; }
    if (rejects("Sun, 31 Nov 1994 08:49:37 GMT", true, high, 5usize) != 0) { return 89; }
    if (rejects("Sun, 06 Nov 1994 08:49:60 GMT", true, high, 23usize) != 0) { return 90; }
    if (rejects("Sundax, 06-Nov-94 08:49:37 GMT", true, digit, 5usize) != 0) { return 91; }
    if (rejects("Sun Nov 6 08:49:37 1994", true, digit, 9usize) != 0) { return 92; }
    if (rejects("Sun Nov  6 08:49:37 1994 GMT", true, trailing, 24usize) != 0) { return 93; }
    return 0;
}

i32 main() {
    try {
        i32 written = check_writing();
        if (written != 0) { return written; }
        i32 rfc3339 = check_rfc3339();
        if (rfc3339 != 0) { return rfc3339; }
        return check_http();
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 99;
    }
}

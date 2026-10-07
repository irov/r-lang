module test.codegen.time_driver;

import std.console;
import std.time;

// The arguments are triples of a mode and two values; the program prints one line per triple
// (M21 differential test, tests/time_differential.py). format writes SECONDS:NANOSECONDS with
// the given number of digits by RFC 3339, http writes SECONDS as an HTTP date, parse and
// parsehttp read a text; a failure prints its code and, for parsing, the byte index.

protected bool same(str left, str right) { return std.bytes::equal(left, right); }

protected str parse_code(std.convert::parse_error_code code) {
    if (code == std.convert::parse_error_code::empty) { return "empty"; }
    if (code == std.convert::parse_error_code::invalid_digit) { return "invalid_digit"; }
    if (code == std.convert::parse_error_code::trailing_character) { return "trailing_character"; }
    if (code == std.convert::parse_error_code::below_minimum) { return "below_minimum"; }
    if (code == std.convert::parse_error_code::above_maximum) { return "above_maximum"; }
    return "other";
}

protected str time_code(std.time::error_code code) {
    if (code == std.time::error_code::invalid_value) { return "invalid_value"; }
    if (code == std.time::error_code::overflow) { return "overflow"; }
    return "other";
}

/* SECONDS:NANOSECONDS as a system time. */
protected std.time::system_time instant(str text) throws std.convert::parse_error, core::utf8_error {
    const u8[] bytes = text;
    usize colon = 0usize;
    while (colon < len(bytes) && bytes[colon] != 58u8) { colon += 1usize; }
    throw (colon == len(bytes))
        std.convert::parse_error {.code = std.convert::parse_error_code::empty, .index = colon};
    i64 seconds = std.convert::parse_i64(core::validate_utf8(bytes[0usize..colon]), 10u32);
    u32 nanoseconds = std.convert::parse_u32(core::validate_utf8(bytes[colon + 1usize..len(bytes)]), 10u32);
    return std.time::system_time {.unix_seconds = seconds, .nanoseconds = nanoseconds};
}

protected std.string::string line(str mode, str first, str second)
    throws std.alloc::alloc_error, std.convert::parse_error, core::utf8_error {
    if (same(mode, "format") == true) {
        std.time::system_time value = instant(first);
        u32 digits = std.convert::parse_u32(second, 10u32);
        try {
            return std.time::format_rfc3339(value, digits);
        } catch (std.time::time_error failure) {
            str name = time_code(failure.code);
            return f"error {name}";
        }
    }
    if (same(mode, "http") == true) {
        std.time::system_time value = instant(first);
        try {
            return std.time::format_http_date(value);
        } catch (std.time::time_error failure) {
            str name = time_code(failure.code);
            return f"error {name}";
        }
    }
    if (same(mode, "parse") == true) {
        try {
            std.time::system_time value = std.time::parse_rfc3339(first);
            i64 seconds = value.unix_seconds;
            u32 nanoseconds = value.nanoseconds;
            return f"{seconds}:{nanoseconds}";
        } catch (std.convert::parse_error failure) {
            str name = parse_code(failure.code);
            usize index = failure.index;
            return f"error {name} {index}";
        }
    }
    try {
        std.time::system_time value = std.time::parse_http_date(first);
        i64 seconds = value.unix_seconds;
        u32 nanoseconds = value.nanoseconds;
        return f"{seconds}:{nanoseconds}";
    } catch (std.convert::parse_error failure) {
        str name = parse_code(failure.code);
        usize index = failure.index;
        return f"error {name} {index}";
    }
}

async i32 main(const str[] arguments) {
    try {
        std.string::string out = std.string::create();
        for (usize at = 1usize; at + 2usize < len(arguments); at += 3usize) {
            std.string::string text = line(arguments[at], arguments[at + 1usize], arguments[at + 2usize]);
            std.string::append_str(&out, text);
            std.string::append_str(&out, "\n");
        }
        await std.console::print(move out);
        return 0;
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 99;
    } catch (std.convert::parse_error failure) {
        failure as void;
        return 98;
    } catch (core::utf8_error failure) {
        failure as void;
        return 96;
    } catch (std.error::fault failure) {
        failure as void;
        return 97;
    }
}

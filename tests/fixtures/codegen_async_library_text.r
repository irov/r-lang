module test.codegen.async_library_text;

import std.bytes;
import std.encoding;
import std.iter;
import std.regex;
import std.string;
import std.text;

// M19 in async functions: owned results of std.text, std.encoding, std.regex and the R parts of
// std.bytes and std.string stay valid across awaits; views and iterators over them are used
// between awaits (Core R-BORROW-0024).

protected bool same(str left, str right) { return std.bytes::equal(left, right); }

protected async i32 half(i32 value) { return value / 2; }

protected async i32 fields(std.string::string line) throws std.async::start_error {
    i32 total = 0;
    for (str field in std.text::split(line, ",")) {
        total += 1;
        if (same(std.text::trim(field), "") == true) { total += 100; }
    }
    i32 step = await half(2);
    return total + step;
}

protected async i32 encoded(bytes data)
    throws std.alloc::alloc_error, std.convert::parse_error, std.async::start_error {
    std.string::string text = std.encoding::encode_base64(data.as_slice());
    i32 pause = await half(0);
    bytes back = std.encoding::decode_base64(text);
    std.string::string hex = std.encoding::encode_hex(back.as_slice());
    i32 again = await half(0);
    i32 status = pause + again;
    if (same(hex, "010203") == false) { status += 1; }
    return status;
}

protected async i32 captured(std.string::string subject)
    throws std.regex::error, std.alloc::alloc_error, std.async::start_error {
    std.regex::regex pair = std.regex::compile("(\\w+)=(\\w+)");
    o<array<o<std.regex::span>>> found = std.regex::captures(&pair, subject);
    i32 status = await half(0);
    switch (found) {
    case variant o::some(groups):
        if (len(*groups) != 3usize) { status += 1; }
    case variant o::none:
        status += 2;
    }
    return status;
}

protected async i32 reduced() throws std.async::start_error {
    fn bool even(const i32* value) { return *value % 2 == 0; }
    i32 pause = await half(0);
    auto evens = std.iter::filter(std.iter::range(0, 10), &even);
    i32 total = std.iter::sum(move evens, pause);
    o<i32> least = std.iter::min(std.iter::range(4, 9));
    i32 low = 0;
    switch (least) {
    case variant o::some(value): low = *value;
    case variant o::none: low = -1;
    }
    return total + low;
}

protected async i32 cursor_and_string() throws std.bytes::bytes_error, std.alloc::alloc_error,
    std.string::boundary_error, std.async::start_error {
    u8[4] buffer = {};
    std.bytes::cursor at = {};
    at.write_u32_be(&buffer, 16909060u32);
    i32 status = await half(0);
    std.bytes::cursor back = {};
    if (back.read_u32_le(buffer) != 67305985u32) { status += 1; }
    std.string::string text = std.string::from_str("ab");
    std.string::insert_str(&text, 1usize, "-");
    status += await half(0);
    std.string::replace_range(&text, 0usize, 1usize, "A");
    if (same(text, "A-b") == false) { status += 2; }
    return status;
}

async i32 main() {
    try {
        i32 count = await fields(std.string::from_str("a, ,b"));
        if (count != 104) { return 1; }
        bytes data = {};
        std.bytes::append_u8(&data, 1u8);
        std.bytes::append_u8(&data, 2u8);
        std.bytes::append_u8(&data, 3u8);
        i32 codes = await encoded(move data);
        if (codes != 0) { return 2; }
        i32 groups = await captured(std.string::from_str("key=value"));
        if (groups != 0) { return 3; }
        i32 sums = await reduced();
        if (sums != 24) { return 4; }
        i32 parts = await cursor_and_string();
        if (parts != 0) { return 5; }
        return 0;
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 90;
    } catch (std.convert::parse_error failure) {
        failure as void;
        return 91;
    } catch (std.regex::error failure) {
        failure as void;
        return 92;
    } catch (std.bytes::bytes_error failure) {
        failure as void;
        return 93;
    } catch (std.string::boundary_error failure) {
        failure as void;
        return 94;
    } catch (std.async::start_error failure) {
        failure as void;
        return 95;
    }
}

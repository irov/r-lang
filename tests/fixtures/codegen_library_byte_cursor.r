module test.codegen.library_byte_cursor;

import std.bytes;
import std.string;

// R-SLIB-BYTES-0009 and R-SLIB-STRING-0004 (M19): the R parts of std.bytes and std.string — a
// cursor reading and writing unsigned integers in both byte orders, and replacing or inserting
// text at scalar boundaries; every failure changes nothing.

protected bool same(str left, str right) { return std.bytes::equal(left, right); }

protected i32 check_cursor() throws std.bytes::bytes_error {
    u8[16] buffer = {};
    std.bytes::cursor writer = {};
    writer.write_u8(&buffer, 7u8);
    writer.write_u16_le(&buffer, 258u16);
    writer.write_u16_be(&buffer, 258u16);
    writer.write_u32_le(&buffer, 16909060u32);
    writer.write_u32_be(&buffer, 16909060u32);
    if (writer.position != 13usize) { return 1; }
    if (buffer[1] != 2u8 || buffer[2] != 1u8 || buffer[3] != 1u8 || buffer[4] != 2u8) { return 2; }
    if (buffer[5] != 4u8 || buffer[8] != 1u8 || buffer[9] != 1u8 || buffer[12] != 4u8) {
        return 3;
    }
    try {
        writer.write_u64_le(&buffer, 1u64);
        return 4;
    } catch (std.bytes::bytes_error failure) {
        if (failure != std.bytes::bytes_error::out_of_bounds) { return 5; }
    }
    if (writer.position != 13usize || buffer[13] != 0u8) { return 6; }
    writer.write_u16_be(&buffer, 65535u16);
    writer.write_u8(&buffer, 128u8);
    if (writer.position != 16usize) { return 7; }
    std.bytes::cursor reader = {};
    if (reader.read_u8(buffer) != 7u8) { return 8; }
    if (reader.read_u16_le(buffer) != 258u16) { return 9; }
    if (reader.read_u16_be(buffer) != 258u16) { return 10; }
    if (reader.read_u32_le(buffer) != 16909060u32) { return 11; }
    if (reader.read_u32_be(buffer) != 16909060u32) { return 12; }
    if (reader.read_u16_le(buffer) != 65535u16 || reader.read_u8(buffer) != 128u8) { return 13; }
    try {
        u8 beyond = reader.read_u8(buffer);
        beyond as void;
        return 14;
    } catch (std.bytes::bytes_error failure) {
        if (reader.position != 16usize) { return 15; }
    }
    u8[16] wide = {};
    std.bytes::cursor eight = {};
    eight.write_u64_le(&wide, 72623859790382856u64);
    eight.write_u64_be(&wide, 72623859790382856u64);
    if (wide[0] != 8u8 || wide[7] != 1u8 || wide[8] != 1u8 || wide[15] != 8u8) { return 16; }
    std.bytes::cursor back = {};
    if (back.read_u64_le(wide) != 72623859790382856u64) { return 17; }
    if (back.read_u64_be(wide) != 72623859790382856u64) { return 18; }
    std.bytes::cursor late = std.bytes::cursor {.position = 20usize};
    try {
        late.read_u8(buffer) as void;
        return 19;
    } catch (std.bytes::bytes_error failure) {
        if (late.position != 20usize) { return 20; }
    }
    return 0;
}

protected i32 check_string() throws std.alloc::alloc_error, std.string::boundary_error {
    std.string::string text = std.string::from_str("héllo");
    std.string::insert_str(&text, 0usize, ">");
    std.string::replace_range(&text, 2usize, 4usize, "E");
    if (same(text, ">hEllo") == false) { return 30; }
    usize end = std.string::len(&text);
    std.string::insert_str(&text, end, "!");
    std.string::replace_range(&text, 1usize, 1usize, "");
    std.string::replace_range(&text, 0usize, 1usize, "");
    if (same(text, "hEllo!") == false) { return 31; }
    try {
        std.string::replace_range(&text, 1usize, 9usize, "x");
        return 32;
    } catch (std.string::boundary_error failure) {
        if (failure != std.string::boundary_error::out_of_bounds) { return 33; }
    }
    try {
        std.string::replace_range(&text, 3usize, 2usize, "x");
        return 34;
    } catch (std.string::boundary_error failure) {
        if (failure != std.string::boundary_error::out_of_bounds) { return 35; }
    }
    std.string::string accent = std.string::from_str("aéb");
    try {
        std.string::insert_str(&accent, 2usize, "x");
        return 36;
    } catch (std.string::boundary_error failure) {
        if (failure != std.string::boundary_error::not_scalar_boundary) { return 37; }
    }
    try {
        std.string::replace_range(&accent, 0usize, 2usize, "x");
        return 38;
    } catch (std.string::boundary_error failure) {
        if (failure != std.string::boundary_error::not_scalar_boundary) { return 39; }
    }
    if (same(accent, "aéb") == false || same(text, "hEllo!") == false) {
        return 40;
    }
    std.string::replace_range(&accent, 1usize, 3usize, "€€");
    if (same(accent, "a€€b") == false) { return 41; }
    return 0;
}

i32 main() {
    try {
        i32 cursor = check_cursor();
        if (cursor != 0) { return cursor; }
        return check_string();
    } catch (std.bytes::bytes_error failure) {
        failure as void;
        return 97;
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 98;
    } catch (std.string::boundary_error failure) {
        failure as void;
        return 99;
    }
}

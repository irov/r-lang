module example.textkit.codes;
import std.bytes;
import std.encoding;
import std.string;

/* textkit encode TEXT: the bytes of TEXT in base64, base64url, hexadecimal and
   percent-encoding. */
std.string::string encoded(str text) throws std.alloc::alloc_error {
    std.string::string standard = std.encoding::encode_base64(text);
    std.string::string url = std.encoding::encode_base64_url(text);
    std.string::string hex = std.encoding::encode_hex(text);
    std.string::string percent = std.encoding::percent_encode(text);
    return f"base64: {standard}\nbase64url: {url}\nhex: {hex}\npercent: {percent}\n";
}

/* The bytes that one of the decoders reads from text. */
bytes decode_with(str kind, str text)
    throws std.convert::parse_error, std.alloc::alloc_error {
    if (std.bytes::equal(kind, "base64") == true) { return std.encoding::decode_base64(text); }
    if (std.bytes::equal(kind, "base64url") == true) {
        return std.encoding::decode_base64_url(text);
    }
    if (std.bytes::equal(kind, "hex") == true) { return std.encoding::decode_hex(text); }
    return std.encoding::percent_decode(text);
}

/* textkit decode base64|base64url|hex|percent TEXT: the decoded bytes, as text when they are
   UTF-8 and in hexadecimal otherwise. */
std.string::string decoded(str kind, str text)
    throws std.convert::parse_error, std.alloc::alloc_error {
    bytes data = decode_with(kind, text);
    usize count = len(data);
    try {
        str readable = core::validate_utf8(data.as_slice());
        return f"{count} bytes: {readable}\n";
    } catch (core::utf8_error failure) {
        failure as void;
        std.string::string hex = std.encoding::encode_hex(data.as_slice());
        return f"{count} bytes: 0x{hex}\n";
    }
}

/* textkit edit TEXT START END REPLACEMENT: TEXT with its bytes START..END replaced. */
std.string::string edited(str text, usize start, usize end, str replacement)
    throws std.string::boundary_error, std.alloc::alloc_error {
    std.string::string result = std.string::from_str(text);
    std.string::replace_range(&result, start, end, replacement);
    return f"{result}\n";
}

/* textkit insert TEXT AT INSERTION: TEXT with INSERTION at byte AT. */
std.string::string inserted(str text, usize at, str insertion)
    throws std.string::boundary_error, std.alloc::alloc_error {
    std.string::string result = std.string::from_str(text);
    std.string::insert_str(&result, at, insertion);
    return f"{result}\n";
}

/* The byte width that pack uses for a value. */
usize width_of(u64 value) {
    if (value < 256u64) { return 1usize; }
    if (value < 65536u64) { return 2usize; }
    if (value < 4294967296u64) { return 4usize; }
    return 8usize;
}

/* textkit pack NUMBER...: each value in the fewest of 1, 2, 4 or 8 bytes, little-endian at even
   and big-endian at odd positions, into a 32-byte record, and the values read back. */
std.string::string packed(const u64[] values)
    throws std.bytes::bytes_error, std.alloc::alloc_error {
    u8[32] record = {};
    std.bytes::cursor writer = {};
    for (usize index = 0usize; index < len(values); index += 1usize) {
        u64 value = values[index];
        bool big = index % 2usize == 1usize;
        switch (width_of(value)) {
        case 1usize: writer.write_u8(&record, value as u8);
        case 2usize:
            if (big == true) { writer.write_u16_be(&record, value as u16); }
            else { writer.write_u16_le(&record, value as u16); }
        case 4usize:
            if (big == true) { writer.write_u32_be(&record, value as u32); }
            else { writer.write_u32_le(&record, value as u32); }
        default:
            if (big == true) { writer.write_u64_be(&record, value); }
            else { writer.write_u64_le(&record, value); }
        }
    }
    usize used = writer.position;
    std.string::string hex = std.encoding::encode_hex(record[0usize..used]);
    std.string::string out = f"{used} bytes: {hex}\n";
    std.bytes::cursor reader = {};
    for (usize index = 0usize; index < len(values); index += 1usize) {
        bool big = index % 2usize == 1usize;
        u64 value = 0u64;
        switch (width_of(values[index])) {
        case 1usize: value = reader.read_u8(record) as u64;
        case 2usize:
            if (big == true) { value = reader.read_u16_be(record) as u64; }
            else { value = reader.read_u16_le(record) as u64; }
        case 4usize:
            if (big == true) { value = reader.read_u32_be(record) as u64; }
            else { value = reader.read_u32_le(record) as u64; }
        default:
            if (big == true) { value = reader.read_u64_be(record); }
            else { value = reader.read_u64_le(record); }
        }
        std.string::string row = f"read {value}\n";
        std.string::append_str(&out, row.as_str());
    }
    return move out;
}

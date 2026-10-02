module example.text.inspect;
import std.text;

std.string::string inspect(str source, str needle) throws std.alloc::alloc_error, std.string::string_error {
    bool ascii = std.text::is_ascii(source);
    usize lines = std.text::count_byte(source, 10u8);
    bool prefix = std.text::starts_with(source, needle);
    bool suffix = std.text::ends_with(source, needle);
    bool contains = std.text::contains(source, needle);
    bool folded = std.text::equal_ignore_ascii_case(source, needle);
    bytes lower_bytes = {};
    const u8[] source_bytes = source;
    for (const u8* byte in &source_bytes) {
        u8 folded_byte = std.text::to_ascii_lower(*byte);
        std.bytes::append_u8(&lower_bytes, folded_byte);
    }
    std.string::string lower = std.string::from_utf8(lower_bytes);
    std.string::string output = f"ascii={ascii} newlines={lines} prefix={prefix} suffix={suffix} contains={contains} folded_equal={folded}\nlower={lower}\n";
    o<usize> first = std.text::find(source, needle);
    switch (first) {
    case variant o::some(value):
        usize offset = *value;
        std.string::string row = f"first={offset}\n";
        str row_text = row.as_str();
        output.append(row_text);
        break;
    case variant o::none: output.append("first=none\n"); break;
    }
    return move output;
}

// Assemble a reusable message buffer, trim at a checked UTF-8 boundary, then reuse its allocation.
std.string::string message(str prefix, str body, usize limit)
    throws std.alloc::alloc_error, std.string::string_error, std.string::boundary_error {
    std.string::string buffer = std.string::with_capacity(32usize);
    usize body_length = len(body);
    buffer.reserve(body_length);
    buffer.append(prefix);
    buffer.append(':');
    buffer.append(' ');
    buffer.append_utf8(body);
    usize length = buffer.len();
    if (limit < length) { buffer.truncate(limit); }
    const u8[] encoded = buffer.as_bytes();
    std.string::string output = std.string::from_utf8(encoded);
    buffer.clear();
    buffer.append("bytes=");
    usize size = output.len();
    usize capacity = buffer.capacity();
    std.string::string stats = f"\nbytes={size} scratch_capacity={capacity}\n";
    str stats_text = stats.as_str();
    output.append(stats_text);
    return move output;
}

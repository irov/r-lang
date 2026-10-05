module std.json;

/* R-SLIB-JSON-0003: the R part of std.json, loaded by `import std.json;`. */

/* The bytes of a text between two scalar boundaries. */
protected void append_piece(std.string::string* target, const u8[] bytes) throws std.alloc::alloc_error {
    try {
        std.string::append_str(target, core::validate_utf8(bytes));
    } catch (core::utf8_error rejected) {
        /* The pieces end before an ASCII byte or before the first byte of a separator. */
        rejected as void;
    }
}

/* R-SLIB-JSON-0003: JSON text with `<`, `>` and `&` written as <, > and & and the
   separators U+2028 and U+2029 as   and  , so that it can stand inside HTML or a
   script. Outside its strings JSON text holds none of them, so the value is unchanged. */
std.string::string escape_html(str text) throws std.alloc::alloc_error {
    const u8[] bytes = text;
    std.string::string escaped = std.string::with_capacity(len(bytes));
    usize start = 0usize;
    usize index = 0usize;
    while (index < len(bytes)) {
        u8 byte = bytes[index];
        usize width = 0usize;
        if (byte == 60u8 || byte == 62u8 || byte == 38u8) { width = 1usize; }
        if (byte == 226u8 && index + 2usize < len(bytes)) {
            if (bytes[index + 1usize] == 128u8 && (bytes[index + 2usize] == 168u8 || bytes[index + 2usize] == 169u8)) {
                width = 3usize;
            }
        }
        if (width == 0usize) {
            index += 1usize;
            continue;
        }
        append_piece(&escaped, bytes[start..index]);
        if (byte == 60u8) { escaped.append("\\u003c"); }
        if (byte == 62u8) { escaped.append("\\u003e"); }
        if (byte == 38u8) { escaped.append("\\u0026"); }
        if (width == 3usize) {
            if (bytes[index + 2usize] == 168u8) { escaped.append("\\u2028"); }
            else { escaped.append("\\u2029"); }
        }
        index += width;
        start = index;
    }
    append_piece(&escaped, bytes[start..len(bytes)]);
    return move escaped;
}

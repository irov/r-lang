module std.string;

/* R-SLIB-STRING-0004: the R part of std.string, loaded by `import std.string;`. */

/* A piece of valid UTF-8 cut at scalar boundaries is valid, so the validation cannot fail. */
protected str piece(str subject, usize start, usize end) {
    const u8[] bytes = subject;
    try {
        return core::validate_utf8(bytes[start..end]);
    } catch (core::utf8_error failure) {
        return "";
    }
}

/* R-SLIB-STRING-0004: replaces the bytes between two scalar boundaries of target with text. An
   end beyond the length or a start after the end reports out_of_bounds, an index inside a
   scalar not_scalar_boundary; every failure leaves target unchanged. */
void replace_range(std.string::string* target, usize start, usize end, str text)
    throws std.string::boundary_error, std.alloc::alloc_error {
    usize length = std.string::len(target);
    throw (start > end || end > length) std.string::boundary_error::out_of_bounds;
    str current = *target;
    const u8[] bytes = current;
    throw ((start < length && (bytes[start] & 192u8) == 128u8) ||
           (end < length && (bytes[end] & 192u8) == 128u8))
        std.string::boundary_error::not_scalar_boundary;
    std.string::string result =
        std.string::with_capacity(start + len(text) + (length - end));
    std.string::append_str(&result, piece(current, 0usize, start));
    std.string::append_str(&result, text);
    std.string::append_str(&result, piece(current, end, length));
    *target = move result;
}

/* R-SLIB-STRING-0004: inserts text at a scalar boundary of target. */
void insert_str(std.string::string* target, usize index, str text)
    throws std.string::boundary_error, std.alloc::alloc_error {
    replace_range(target, index, index, text);
}

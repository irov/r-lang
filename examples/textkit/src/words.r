module example.textkit.words;
import std.text;

/* The byte length of a view. */
usize size_of_text(str text) {
    const u8[] bytes = text;
    return len(bytes);
}

/* textkit fields LINE SEPARATOR: the pieces between the separators with the whitespace that
   trim_start and trim_end remove around each. */
std.string::string split_fields(str line, str separator) throws std.alloc::alloc_error {
    std.string::string out = std.string::create();
    u32 number = 0u32;
    for (str piece in std.text::split(line, separator)) {
        number += 1u32;
        str trimmed = std.text::trim(piece);
        usize lead = size_of_text(piece) - size_of_text(std.text::trim_start(piece));
        usize trail = size_of_text(piece) - size_of_text(std.text::trim_end(piece));
        std.string::string row = f"field {number}: [{trimmed}] lead={lead} trail={trail}\n";
        std.string::append_str(&out, row.as_str());
    }
    return move out;
}

/* textkit lines TEXT: the numbered lines of TEXT. */
std.string::string numbered(str text) throws std.alloc::alloc_error {
    std.string::string out = std.string::create();
    u32 number = 0u32;
    for (str line in std.text::lines(text)) {
        number += 1u32;
        std.string::string row = f"{number:3}| {line}\n";
        std.string::append_str(&out, row.as_str());
    }
    std.string::string total = f"{number} lines\n";
    std.string::append_str(&out, total.as_str());
    return move out;
}

/* textkit scalars TEXT: every scalar with its code point and byte position, and the scalar
   boundaries around the middle byte. */
std.string::string code_points(str text) throws std.alloc::alloc_error {
    std.string::string out = std.string::create();
    usize at = 0usize;
    for (char value in std.text::scalars(text)) {
        u32 code = value as u32;
        std.string::string row = f"{at:3}  U+{code:04x}\n";
        std.string::append_str(&out, row.as_str());
        at = std.text::next_scalar_boundary(text, at);
    }
    usize middle = size_of_text(text) / 2usize;
    bool exact = std.text::is_scalar_boundary(text, middle);
    usize before = std.text::previous_scalar_boundary(text, middle);
    usize after = std.text::next_scalar_boundary(text, middle);
    std.string::string summary =
        f"{at} bytes; byte {middle}: boundary={exact} previous={before} next={after}\n";
    std.string::append_str(&out, summary.as_str());
    return move out;
}

/* textkit fold TEXT: TEXT with ASCII letters in upper case, in lower case, and its first byte
   folded to upper case. */
std.string::string folded(str text) throws std.alloc::alloc_error {
    std.string::string upper = std.text::ascii_uppercase(text);
    std.string::string lower = std.text::ascii_lowercase(text);
    const u8[] bytes = text;
    u32 first = 0u32;
    if (len(bytes) != 0usize) { first = std.text::to_ascii_upper(bytes[0]) as u32; }
    return f"upper: {upper}\nlower: {lower}\nfirst byte: {first}\n";
}

/* textkit join SEPARATOR PART...: the parts joined with the separator. */
std.string::string joined(const str[] parts, str separator) throws std.alloc::alloc_error {
    std.string::string text = std.text::join(parts, separator);
    return f"{text}\n";
}

/* textkit replace TEXT PATTERN REPLACEMENT: every occurrence of the pattern replaced. */
std.string::string replaced(str text, str pattern, str replacement)
    throws std.alloc::alloc_error {
    std.string::string result = std.text::replace(text, pattern, replacement);
    return f"{result}\n";
}

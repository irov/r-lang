module std.text;

/* R-SLIB-TEXT-0001: byte-level helpers over str; every position is a byte index and every
   comparison is exact byte equality, so the results respect UTF-8 boundaries only when the
   arguments do. */
bool starts_with(str subject, str prefix) {
    const u8[] bytes = subject;
    const u8[] head = prefix;
    usize count = len(head);
    if (count > len(bytes)) { return false; }
    usize index = 0usize;
    while (index < count) {
        if (bytes[index] != head[index]) { return false; }
        index += 1usize;
    }
    return true;
}

bool ends_with(str subject, str suffix) {
    const u8[] bytes = subject;
    const u8[] tail = suffix;
    usize count = len(tail);
    usize total = len(bytes);
    if (count > total) { return false; }
    usize offset = total - count;
    usize index = 0usize;
    while (index < count) {
        if (bytes[offset + index] != tail[index]) { return false; }
        index += 1usize;
    }
    return true;
}

/* The byte index of the first occurrence of `needle`, or none. */
o<usize> find(str subject, str needle) {
    const u8[] bytes = subject;
    const u8[] pattern = needle;
    usize count = len(pattern);
    usize total = len(bytes);
    if (count > total) { return o::none; }
    usize last = total - count;
    usize start = 0usize;
    while (start <= last) {
        usize index = 0usize;
        bool matched = true;
        while (index < count) {
            if (bytes[start + index] != pattern[index]) {
                matched = false;
                break;
            }
            index += 1usize;
        }
        if (matched == true) { return o::some(start); }
        start += 1usize;
    }
    return o::none;
}

bool contains(str subject, str needle) {
    o<usize> found = find(subject, needle);
    switch (found) {
    case variant o::some(index):
        return true;
    case variant o::none:
        return false;
    }
}

usize count_byte(str subject, u8 value) {
    const u8[] bytes = subject;
    usize total = 0usize;
    for (const u8* byte in &bytes) {
        if (*byte == value) { total += 1usize; }
    }
    return total;
}

bool is_ascii(str subject) {
    const u8[] bytes = subject;
    for (const u8* byte in &bytes) {
        if (*byte >= 128u8) { return false; }
    }
    return true;
}

/* ASCII case folding of one byte. */
u8 to_ascii_lower(u8 value) {
    if (value >= 65u8 && value <= 90u8) {
        u32 folded = (value as u32) + 32u32;
        return folded as u8;
    }
    return value;
}

bool equal_ignore_ascii_case(str left, str right) {
    const u8[] first = left;
    const u8[] second = right;
    usize count = len(first);
    if (count != len(second)) { return false; }
    usize index = 0usize;
    while (index < count) {
        u8 a = to_ascii_lower(first[index]);
        u8 b = to_ascii_lower(second[index]);
        if (a != b) { return false; }
        index += 1usize;
    }
    return true;
}

/* R-SLIB-TEXT-0001 (L20.4): a case matcher over a view of a subject, whose labels match when they
   equal the subject with the ASCII letters folded. */
struct ascii_caseless { str subject; };

ascii_caseless ignore_ascii_case(str subject) { return ascii_caseless {.subject = subject}; }

impl core::CaseMatcher for ascii_caseless {
    type Label = str;
    bool matches(const ascii_caseless* this, str label) {
        return equal_ignore_ascii_case(this->subject, label);
    }
};

/* R-SLIB-TEXT-0002: the piece of subject between two byte indices at scalar boundaries. A range
   of valid UTF-8 cut at scalar boundaries is valid, so the validation cannot fail. */
protected str piece(str subject, usize start, usize end) {
    const u8[] bytes = subject;
    try {
        return core::validate_utf8(bytes[start..end]);
    } catch (core::utf8_error failure) {
        return "";
    }
}

/* R-SLIB-TEXT-0002: whether a byte index of subject is a scalar boundary: its end, or a byte
   that does not continue a scalar. */
bool is_scalar_boundary(str subject, usize index) {
    const u8[] bytes = subject;
    if (index >= len(bytes)) { return index == len(bytes); }
    return (bytes[index] & 192u8) != 128u8;
}

/* The first scalar boundary after index, or the length of subject. */
usize next_scalar_boundary(str subject, usize index) {
    const u8[] bytes = subject;
    usize total = len(bytes);
    if (index >= total) { return total; }
    usize at = index + 1usize;
    while (at < total && (bytes[at] & 192u8) == 128u8) { at += 1usize; }
    return at;
}

/* The last scalar boundary before index, or zero. */
usize previous_scalar_boundary(str subject, usize index) {
    const u8[] bytes = subject;
    usize at = index;
    if (at > len(bytes)) { at = len(bytes); }
    if (at == 0usize) { return 0usize; }
    at -= 1usize;
    while (at > 0usize && (bytes[at] & 192u8) == 128u8) { at -= 1usize; }
    return at;
}

/* R-SLIB-TEXT-0002: the scalars of a subject in order. */
struct scalar_iter { str subject; usize position; };

scalar_iter scalars(str subject) { return scalar_iter {.subject = subject, .position = 0usize}; }

impl core::Iterator for scalar_iter {
    type Item = char;
    o<char> next(scalar_iter* this) {
        const u8[] bytes = this->subject;
        usize at = this->position;
        if (at >= len(bytes)) { return o::none; }
        u32 lead = bytes[at] as u32;
        u32 value = lead;
        usize width = 1usize;
        if (lead >= 240u32) {
            value = lead & 7u32;
            width = 4usize;
        } else {
            if (lead >= 224u32) {
                value = lead & 15u32;
                width = 3usize;
            } else {
                if (lead >= 192u32) {
                    value = lead & 31u32;
                    width = 2usize;
                }
            }
        }
        for (usize index = 1usize; index < width; index += 1usize) {
            value = (value << 6u32) | ((bytes[at + index] as u32) & 63u32);
        }
        this->position = at + width;
        return o::some(value as char);
    }
};

/* ASCII whitespace: space, tab, line feed, vertical tab, form feed and carriage return. */
protected bool is_ascii_space(u8 value) {
    return value == 32u8 || (value >= 9u8 && value <= 13u8);
}

/* R-SLIB-TEXT-0002: subject without its leading, trailing or both kinds of ASCII whitespace. */
str trim_start(str subject) {
    const u8[] bytes = subject;
    usize start = 0usize;
    while (start < len(bytes) && is_ascii_space(bytes[start]) == true) { start += 1usize; }
    return piece(subject, start, len(bytes));
}

str trim_end(str subject) {
    const u8[] bytes = subject;
    usize end = len(bytes);
    while (end > 0usize && is_ascii_space(bytes[end - 1usize]) == true) { end -= 1usize; }
    return piece(subject, 0usize, end);
}

str trim(str subject) {
    return trim_end(trim_start(subject));
}

/* ASCII case folding of one byte to upper case. */
u8 to_ascii_upper(u8 value) {
    if (value >= 97u8 && value <= 122u8) {
        u32 folded = (value as u32) - 32u32;
        return folded as u8;
    }
    return value;
}

/* Whether pattern occurs in bytes at index; the caller keeps it within bytes. */
protected bool matches_at(const u8[] bytes, usize at, const u8[] pattern) {
    for (usize index = 0usize; index < len(pattern); index += 1usize) {
        if (bytes[at + index] != pattern[index]) { return false; }
    }
    return true;
}

/* R-SLIB-TEXT-0003: the pieces of subject between the occurrences of separator, left to right
   and without overlap; an empty separator yields the whole subject. */
struct split_iter { str subject; str separator; usize position; bool done; };

split_iter split(str subject, str separator) {
    return split_iter {.subject = subject, .separator = separator, .position = 0usize,
                       .done = false};
}

impl core::Iterator for split_iter {
    type Item = str;
    o<str> next(split_iter* this) {
        if (this->done == true) { return o::none; }
        const u8[] bytes = this->subject;
        const u8[] pattern = this->separator;
        usize start = this->position;
        usize count = len(pattern);
        usize total = len(bytes);
        if (count != 0usize && count <= total) {
            usize at = start;
            while (at + count <= total) {
                if (matches_at(bytes, at, pattern) == true) {
                    this->position = at + count;
                    return o::some(piece(this->subject, start, at));
                }
                at += 1usize;
            }
        }
        this->done = true;
        return o::some(piece(this->subject, start, total));
    }
};

/* R-SLIB-TEXT-0003: the lines of subject without their line feed and one carriage return before
   it; a final line feed ends the last line and adds no empty one. */
struct lines_iter { str subject; usize position; };

lines_iter lines(str subject) { return lines_iter {.subject = subject, .position = 0usize}; }

impl core::Iterator for lines_iter {
    type Item = str;
    o<str> next(lines_iter* this) {
        const u8[] bytes = this->subject;
        usize start = this->position;
        usize total = len(bytes);
        if (start >= total) { return o::none; }
        usize at = start;
        while (at < total && bytes[at] != 10u8) { at += 1usize; }
        usize next_start = at;
        if (at < total) { next_start = at + 1usize; }
        this->position = next_start;
        usize end = at;
        if (end > start && bytes[end - 1usize] == 13u8) { end -= 1usize; }
        return o::some(piece(this->subject, start, end));
    }
};

/* Owned strings exist from the hosted profile upwards. */
@if (!(core::profile is freestanding) && !(core::profile is allocation)) {
    /* R-SLIB-TEXT-0004: the parts joined with separator between each two. */
    std.string::string join(const str[] parts, str separator) throws std.alloc::alloc_error {
        std.string::string result = std.string::create();
        usize total = 0usize;
        const u8[] gap = separator;
        for (usize index = 0usize; index < len(parts); index += 1usize) {
            const u8[] part = parts[index];
            total += len(part);
            if (index != 0usize) { total += len(gap); }
        }
        std.string::reserve(&result, total);
        for (usize index = 0usize; index < len(parts); index += 1usize) {
            if (index != 0usize) { std.string::append_str(&result, separator); }
            std.string::append_str(&result, parts[index]);
        }
        return move result;
    }

    /* R-SLIB-TEXT-0004: subject with every occurrence of pattern, left to right and without
       overlap, replaced by replacement; an empty pattern replaces nothing. */
    std.string::string replace(str subject, str pattern, str replacement)
        throws std.alloc::alloc_error {
        std.string::string result = std.string::create();
        std.string::reserve(&result, len(subject));
        bool first = true;
        for (str part in split(subject, pattern)) {
            if (first == false) { std.string::append_str(&result, replacement); }
            std.string::append_str(&result, part);
            first = false;
        }
        return move result;
    }

    /* R-SLIB-TEXT-0004: subject with the ASCII letters in upper or lower case; every other
       scalar is copied. */
    protected std.string::string ascii_case(str subject, bool upper)
        throws std.alloc::alloc_error {
        std.string::string result = std.string::with_capacity(len(subject));
        for (char value in scalars(subject)) {
            u32 code = value as u32;
            if (code < 128u32) {
                u8 byte = code as u8;
                if (upper == true) { byte = to_ascii_upper(byte); }
                else { byte = to_ascii_lower(byte); }
                std.string::push_scalar(&result, byte as char);
            } else {
                std.string::push_scalar(&result, value);
            }
        }
        return move result;
    }

    std.string::string ascii_uppercase(str subject) throws std.alloc::alloc_error {
        return ascii_case(subject, true);
    }

    std.string::string ascii_lowercase(str subject) throws std.alloc::alloc_error {
        return ascii_case(subject, false);
    }
}

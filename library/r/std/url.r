module std.url;
import std.text;

/* R-SLIB-URL-0001: why a text is not a URL, a reference or a percent-encoded text. */
@derive(format)
enum error_code {
    empty,
    missing_scheme,
    invalid_scheme,
    invalid_character,
    invalid_percent_encoding,
    invalid_host,
    invalid_port,
    invalid_utf8,
};

/* R-SLIB-URL-0001: a failure with the byte offset in the text where it was found. */
error url_error { error_code code; usize offset; };

protected url_error failure(error_code code, usize offset) {
    return url_error {.code = code, .offset = offset};
}

/* R-SLIB-URL-0002: an absolute URL of RFC 3986 in its parts. The scheme and a registered name
   or IPv4 host are kept in lower case; an IPv6 host is kept without its brackets; every other
   part keeps its percent-encoded triplets as written. */
struct url {
    protected std.string::string scheme_text;
    protected bool authority;
    protected std.string::string userinfo_text;
    protected bool userinfo_present;
    protected std.string::string host_text;
    protected bool ipv6;
    protected o<u16> port_value;
    protected std.string::string path_text;
    protected o<std.string::string> query_text;
    protected o<std.string::string> fragment_text;
};

protected bool is_alpha(u8 value) {
    return (value >= 65u8 && value <= 90u8) || (value >= 97u8 && value <= 122u8);
}

protected bool is_digit(u8 value) { return value >= 48u8 && value <= 57u8; }

protected bool is_hex(u8 value) {
    return is_digit(value) || (value >= 65u8 && value <= 70u8) || (value >= 97u8 && value <= 102u8);
}

protected u32 hex_value(u8 value) {
    u32 code = value as u32;
    if (is_digit(value) == true) { return code - 48u32; }
    if (code >= 97u32) { return code - 87u32; }
    return code - 55u32;
}

/* The upper-case hexadecimal digit of a value below 16. */
protected u8 hex_digit(u32 value) {
    if (value < 10u32) { return (value + 48u32) as u8; }
    return (value + 55u32) as u8;
}

protected u8 lower(u8 value) {
    if (value >= 65u8 && value <= 90u8) { return ((value as u32) + 32u32) as u8; }
    return value;
}

/* Appends the percent-encoded triplet of a byte. */
protected void push_triplet(std.string::string* out, u8 value) throws std.alloc::alloc_error {
    u32 code = value as u32;
    std.string::push_scalar(out, 37u8 as char);
    std.string::push_scalar(out, hex_digit(code >> 4u32) as char);
    std.string::push_scalar(out, hex_digit(code & 15u32) as char);
}

@generic<T>
protected void append(array<T>* target, T value) throws std.alloc::alloc_error {
    try {
        target->push(move value);
    } catch (std.array::push_error<T> rejected) {
        switch (move rejected) {
        case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
        }
    }
}

/* RFC 3986 unreserved: ALPHA DIGIT - . _ ~ */
protected bool is_unreserved(u8 value) {
    return is_alpha(value) || is_digit(value) || value == 45u8 || value == 46u8 || value == 95u8 ||
           value == 126u8;
}

/* RFC 3986 sub-delims: ! $ & ' ( ) * + , ; = */
protected bool is_sub_delim(u8 value) {
    return value == 33u8 || value == 36u8 || (value >= 38u8 && value <= 44u8) || value == 59u8 ||
           value == 61u8;
}

protected bool is_pchar(u8 value) {
    return is_unreserved(value) || is_sub_delim(value) || value == 58u8 || value == 64u8;
}

/* The piece of text between two byte indices at ASCII positions. */
protected str piece(str text, usize start, usize end) {
    const u8[] bytes = text;
    try {
        return core::validate_utf8(bytes[start..end]);
    } catch (core::utf8_error rejected) {
        rejected as void;
        return "";
    }
}

protected std.string::string owned(str text, usize start, usize end) throws std.alloc::alloc_error {
    return std.string::from_str(piece(text, start, end));
}

/* Checks that bytes[start..end] holds only the characters that allowed accepts and valid
   percent-encoded triplets; extra accepts '/' and '?' for a query or a fragment and '/' for a
   path. */
protected void check_part(const u8[] bytes, usize start, usize end, u32 kind) throws url_error {
    usize index = start;
    while (index < end) {
        u8 value = bytes[index];
        if (value == 37u8) {
            throw (index + 2usize >= end) failure(error_code::invalid_percent_encoding, index);
            throw (is_hex(bytes[index + 1usize]) == false || is_hex(bytes[index + 2usize]) == false)
                failure(error_code::invalid_percent_encoding, index);
            index += 3usize;
            continue;
        }
        bool accepted = false;
        switch (kind) {
        case 0u32: accepted = is_unreserved(value) || is_sub_delim(value) || value == 58u8;
        case 1u32: accepted = is_unreserved(value) || is_sub_delim(value);
        case 2u32: accepted = is_pchar(value) || value == 47u8;
        default: accepted = is_pchar(value) || value == 47u8 || value == 63u8;
        }
        throw (accepted == false) failure(error_code::invalid_character, index);
        index += 1usize;
    }
}

/* The position of the first byte of bytes[start..] that is one of the given two or three
   delimiters, or the end. */
protected usize find_any(const u8[] bytes, usize start, u8 first, u8 second, u8 third) {
    usize index = start;
    while (index < len(bytes)) {
        u8 value = bytes[index];
        if (value == first || value == second || value == third) { return index; }
        index += 1usize;
    }
    return len(bytes);
}

/* The lower-case form of a text of ASCII letters, digits and marks. */
protected std.string::string lowered(str text) throws std.alloc::alloc_error {
    const u8[] bytes = text;
    std.string::string result = std.string::with_capacity(len(bytes));
    for (usize index = 0usize; index < len(bytes); index += 1usize) {
        std.string::push_scalar(&result, lower(bytes[index]) as char);
    }
    return move result;
}

/* The length of a scheme followed by ':' at the start of text, or zero when there is none. */
protected usize scheme_length(const u8[] bytes) {
    if (len(bytes) == 0usize || is_alpha(bytes[0usize]) == false) { return 0usize; }
    usize index = 1usize;
    while (index < len(bytes)) {
        u8 value = bytes[index];
        if (value == 58u8) { return index; }
        if (is_alpha(value) == false && is_digit(value) == false && value != 43u8 &&
            value != 45u8 && value != 46u8) {
            return 0usize;
        }
        index += 1usize;
    }
    return 0usize;
}

/* Parses the authority bytes[start..end] into the parts of result. */
protected void parse_authority(str text, usize start, usize end, url* result)
    throws url_error, std.alloc::alloc_error {
    const u8[] bytes = text;
    usize host_start = start;
    usize at = find_any(bytes[0usize..end], start, 64u8, 64u8, 64u8);
    if (at < end) {
        check_part(bytes, start, at, 0u32);
        result->userinfo_text = owned(text, start, at);
        result->userinfo_present = true;
        host_start = at + 1usize;
    }
    usize host_end = end;
    usize port_start = end;
    if (host_start < end && bytes[host_start] == 91u8) {
        usize close = find_any(bytes[0usize..end], host_start, 93u8, 93u8, 93u8);
        throw (close == end) failure(error_code::invalid_host, host_start);
        for (usize index = host_start + 1usize; index < close; index += 1usize) {
            u8 value = bytes[index];
            throw (is_hex(value) == false && value != 58u8 && value != 46u8)
                failure(error_code::invalid_host, index);
        }
        throw (close == host_start + 1usize) failure(error_code::invalid_host, host_start);
        result->host_text = lowered(piece(text, host_start + 1usize, close));
        result->ipv6 = true;
        host_end = close + 1usize;
        if (host_end < end) {
            throw (bytes[host_end] != 58u8) failure(error_code::invalid_host, host_end);
            port_start = host_end + 1usize;
        }
    } else {
        usize colon = end;
        for (usize index = host_start; index < end; index += 1usize) {
            if (bytes[index] == 58u8) { colon = index; }
        }
        check_part(bytes, host_start, colon, 1u32);
        result->host_text = lowered(piece(text, host_start, colon));
        if (colon < end) { port_start = colon + 1usize; }
    }
    if (port_start < end) {
        u32 port = 0u32;
        for (usize index = port_start; index < end; index += 1usize) {
            u8 value = bytes[index];
            throw (is_digit(value) == false) failure(error_code::invalid_port, index);
            port = port * 10u32 + (value as u32) - 48u32;
            throw (port > 65535u32) failure(error_code::invalid_port, port_start);
        }
        result->port_value = o::some(port as u16);
    }
}

protected url empty_url() {
    return url {.scheme_text = std.string::create(), .authority = false,
                .userinfo_text = std.string::create(), .userinfo_present = false,
                .host_text = std.string::create(), .ipv6 = false, .port_value = o::none,
                .path_text = std.string::create(), .query_text = o::none,
                .fragment_text = o::none};
}

/* A URI reference of RFC 3986 section 4.1; the scheme is empty when the reference is
   relative. */
protected url parse_reference(str text) throws url_error, std.alloc::alloc_error {
    const u8[] bytes = text;
    url result = empty_url();
    usize position = 0usize;
    usize scheme = scheme_length(bytes);
    if (scheme != 0usize) {
        result.scheme_text = lowered(piece(text, 0usize, scheme));
        position = scheme + 1usize;
    }
    if (position + 1usize < len(bytes) && bytes[position] == 47u8 &&
        bytes[position + 1usize] == 47u8) {
        usize start = position + 2usize;
        usize end = find_any(bytes, start, 47u8, 63u8, 35u8);
        result.authority = true;
        parse_authority(text, start, end, &result);
        position = end;
    }
    usize path_end = find_any(bytes, position, 63u8, 35u8, 35u8);
    check_part(bytes, position, path_end, 2u32);
    if (scheme == 0usize && result.authority == false) {
        // A relative path may not have a ':' in its first segment (RFC 3986 section 4.2).
        usize first = find_any(bytes, position, 47u8, 58u8, 58u8);
        throw (first < path_end && bytes[first] == 58u8) failure(error_code::missing_scheme, first);
    }
    result.path_text = owned(text, position, path_end);
    usize rest = path_end;
    if (rest < len(bytes) && bytes[rest] == 63u8) {
        usize end = find_any(bytes, rest + 1usize, 35u8, 35u8, 35u8);
        check_part(bytes, rest + 1usize, end, 3u32);
        result.query_text = o::some(owned(text, rest + 1usize, end));
        rest = end;
    }
    if (rest < len(bytes)) {
        check_part(bytes, rest + 1usize, len(bytes), 3u32);
        result.fragment_text = o::some(owned(text, rest + 1usize, len(bytes)));
    }
    return move result;
}

/* R-SLIB-URL-0002: parses an absolute URL: a scheme, then an authority, a path, a query and a
   fragment as RFC 3986 section 3 allows. */
url parse(str text) throws url_error, std.alloc::alloc_error {
    const u8[] bytes = text;
    throw (len(bytes) == 0usize) failure(error_code::empty, 0usize);
    if (scheme_length(bytes) == 0usize) {
        usize first = find_any(bytes, 0usize, 58u8, 47u8, 63u8);
        throw (first < len(bytes) && bytes[first] == 58u8)
            failure(error_code::invalid_scheme, 0usize);
        throw failure(error_code::missing_scheme, 0usize);
    }
    return parse_reference(text);
}

/* Removes the last segment of output and the '/' before it, if any. */
protected void drop_last_segment(std.string::string* output) {
    const u8[] bytes = std.string::as_bytes(output);
    usize end = len(bytes);
    while (end > 0usize && bytes[end - 1usize] != 47u8) { end -= 1usize; }
    if (end > 0usize) { end -= 1usize; }
    try {
        std.string::truncate(output, end);
    } catch (std.string::boundary_error rejected) {
        // end is before a '/' or zero, a boundary of every text.
        rejected as void;
    }
}

/* Whether input[position..] starts with pattern, and, when whole is true, ends with it. */
protected bool starts(const u8[] input, usize position, str pattern, bool whole) {
    const u8[] expected = pattern;
    usize rest = len(input) - position;
    if (rest < len(expected)) { return false; }
    if (whole == true && rest != len(expected)) { return false; }
    for (usize index = 0usize; index < len(expected); index += 1usize) {
        if (input[position + index] != expected[index]) { return false; }
    }
    return true;
}

/* The rule of RFC 3986 section 5.2.4 step 2 that applies at position: A1 "../", A2 "./",
   B1 "/./", B2 "/." at the end, C1 "/../", C2 "/.." at the end, D "." or ".." as the whole
   rest, E any other segment. */
protected u32 dot_step(const u8[] input, usize position) {
    if (starts(input, position, "../", false) == true) { return 1u32; }
    if (starts(input, position, "./", false) == true) { return 2u32; }
    if (starts(input, position, "/./", false) == true) { return 3u32; }
    if (starts(input, position, "/.", true) == true) { return 4u32; }
    if (starts(input, position, "/../", false) == true) { return 5u32; }
    if (starts(input, position, "/..", true) == true) { return 6u32; }
    if (starts(input, position, ".", true) == true || starts(input, position, "..", true) == true) {
        return 7u32;
    }
    return 8u32;
}

/* RFC 3986 section 5.2.4: the path without its "." and ".." segments. */
protected std.string::string remove_dot_segments(str path) throws std.alloc::alloc_error {
    const u8[] input = path;
    usize total = len(input);
    std.string::string output = std.string::with_capacity(total);
    usize position = 0usize;
    while (position < total) {
        switch (dot_step(input, position)) {
        case 1u32: position += 3usize;
        case 2u32: position += 2usize;
        case 3u32: position += 2usize;
        case 4u32:
            std.string::append_str(&output, "/");
            position = total;
        case 5u32:
            drop_last_segment(&output);
            position += 3usize;
        case 6u32:
            drop_last_segment(&output);
            std.string::append_str(&output, "/");
            position = total;
        case 7u32: position = total;
        default:
            usize end = position + 1usize;
            while (end < total && input[end] != 47u8) { end += 1usize; }
            std.string::append_str(&output, piece(path, position, end));
            position = end;
        }
    }
    return move output;
}

protected std.string::string copy_text(const std.string::string* text) throws std.alloc::alloc_error {
    return std.string::from_str(std.string::as_str(text));
}

protected o<std.string::string> copy_optional(const (o<std.string::string>)* text)
    throws std.alloc::alloc_error {
    switch (*text) {
    case variant o::some(value): return o::some(std.string::from_str(std.string::as_str(value)));
    case variant o::none: return o::none;
    }
}

/* Copies the authority of source into target. */
protected void copy_authority(url* target, const url* source) throws std.alloc::alloc_error {
    target->authority = source->authority;
    target->userinfo_text = copy_text(&source->userinfo_text);
    target->userinfo_present = source->userinfo_present;
    target->host_text = copy_text(&source->host_text);
    target->ipv6 = source->ipv6;
    target->port_value = source->port_value;
}

/* R-SLIB-URL-0003: the target URL of a reference resolved against this URL as its base
   (RFC 3986 section 5.2.2, strict). */
url url::resolve(const url* this, str reference) throws url_error, std.alloc::alloc_error {
    url relative = parse_reference(reference);
    url result = empty_url();
    if (std.string::len(&relative.scheme_text) != 0usize) {
        result.scheme_text = copy_text(&relative.scheme_text);
        copy_authority(&result, &relative);
        result.path_text = remove_dot_segments(relative.path_text.as_str());
        result.query_text = copy_optional(&relative.query_text);
    } else {
        result.scheme_text = copy_text(&this->scheme_text);
        if (relative.authority == true) {
            copy_authority(&result, &relative);
            result.path_text = remove_dot_segments(relative.path_text.as_str());
            result.query_text = copy_optional(&relative.query_text);
        } else {
            copy_authority(&result, this);
            if (std.string::len(&relative.path_text) == 0usize) {
                result.path_text = copy_text(&this->path_text);
                switch (relative.query_text) {
                case variant o::some(value):
                    result.query_text = o::some(std.string::from_str(std.string::as_str(value)));
                case variant o::none: result.query_text = copy_optional(&this->query_text);
                }
            } else {
                const u8[] given = relative.path_text.as_bytes();
                if (given[0usize] == 47u8) {
                    result.path_text = remove_dot_segments(relative.path_text.as_str());
                } else {
                    std.string::string merged = std.string::create();
                    if (this->authority == true && std.string::len(&this->path_text) == 0usize) {
                        std.string::append_str(&merged, "/");
                    } else {
                        const u8[] base = this->path_text.as_bytes();
                        usize end = len(base);
                        while (end > 0usize && base[end - 1usize] != 47u8) { end -= 1usize; }
                        std.string::append_str(&merged, piece(this->path_text.as_str(), 0usize, end));
                    }
                    std.string::append_str(&merged, relative.path_text.as_str());
                    result.path_text = remove_dot_segments(merged.as_str());
                }
                result.query_text = copy_optional(&relative.query_text);
            }
        }
    }
    result.fragment_text = copy_optional(&relative.fragment_text);
    return move result;
}

/* R-SLIB-URL-0002: the parts of a URL. */
str url::scheme(const url* this) { return this->scheme_text.as_str(); }
bool url::has_authority(const url* this) { return this->authority; }
o<str> url::userinfo(const url* this) {
    if (this->userinfo_present == false) { return o::none; }
    return o::some(this->userinfo_text.as_str());
}
str url::host(const url* this) { return this->host_text.as_str(); }
bool url::is_ipv6(const url* this) { return this->ipv6; }
o<u16> url::port(const url* this) { return this->port_value; }
str url::path(const url* this) { return this->path_text.as_str(); }
o<str> url::query(const url* this) {
    switch (this->query_text) {
    case variant o::some(value): return o::some(std.string::as_str(value));
    case variant o::none: return o::none;
    }
}
o<str> url::fragment(const url* this) {
    switch (this->fragment_text) {
    case variant o::some(value): return o::some(std.string::as_str(value));
    case variant o::none: return o::none;
    }
}

/* The default port of the schemes http, https, ws and wss. */
o<u16> default_port(str scheme) {
    if (std.text::equal_ignore_ascii_case(scheme, "http") == true ||
        std.text::equal_ignore_ascii_case(scheme, "ws") == true) {
        return o::some(80u16);
    }
    if (std.text::equal_ignore_ascii_case(scheme, "https") == true ||
        std.text::equal_ignore_ascii_case(scheme, "wss") == true) {
        return o::some(443u16);
    }
    return o::none;
}

/* The explicit port, or the default port of the scheme. */
o<u16> url::effective_port(const url* this) {
    switch (this->port_value) {
    case variant o::some(value): return o::some(*value);
    case variant o::none: return default_port(this->scheme_text.as_str());
    }
}

/* The request target of an HTTP request for this URL: the path, "/" when it is empty, and the
   query. */
std.string::string url::target(const url* this) throws std.alloc::alloc_error {
    std.string::string result = std.string::create();
    if (std.string::len(&this->path_text) == 0usize) {
        std.string::append_str(&result, "/");
    } else {
        std.string::append_str(&result, this->path_text.as_str());
    }
    switch (this->query_text) {
    case variant o::some(value):
        std.string::append_str(&result, "?");
        std.string::append_str(&result, std.string::as_str(value));
    case variant o::none: break;
    }
    return move result;
}

/* The host and the port as a Host header writes them: the port only when it is explicit. */
std.string::string url::authority_text(const url* this) throws std.alloc::alloc_error {
    std.string::string result = std.string::create();
    if (this->ipv6 == true) { std.string::append_str(&result, "["); }
    std.string::append_str(&result, this->host_text.as_str());
    if (this->ipv6 == true) { std.string::append_str(&result, "]"); }
    switch (this->port_value) {
    case variant o::some(value):
        u16 port = *value;
        std.string::string number = f":{port}";
        std.string::append_str(&result, number.as_str());
    case variant o::none: break;
    }
    return move result;
}

/* R-SLIB-URL-0002: the text of the URL recomposed as RFC 3986 section 5.3 describes. */
std.string::string url::text(const url* this) throws std.alloc::alloc_error {
    std.string::string result = std.string::create();
    if (std.string::len(&this->scheme_text) != 0usize) {
        std.string::append_str(&result, this->scheme_text.as_str());
        std.string::append_str(&result, ":");
    }
    if (this->authority == true) {
        std.string::append_str(&result, "//");
        if (this->userinfo_present == true) {
            std.string::append_str(&result, this->userinfo_text.as_str());
            std.string::append_str(&result, "@");
        }
        std.string::string rest = this->authority_text();
        std.string::append_str(&result, rest.as_str());
    }
    std.string::append_str(&result, this->path_text.as_str());
    switch (this->query_text) {
    case variant o::some(value):
        std.string::append_str(&result, "?");
        std.string::append_str(&result, std.string::as_str(value));
    case variant o::none: break;
    }
    switch (this->fragment_text) {
    case variant o::some(value):
        std.string::append_str(&result, "#");
        std.string::append_str(&result, std.string::as_str(value));
    case variant o::none: break;
    }
    return move result;
}

impl core::Format for url {
    void format(const url* this, std.format::builder* out) throws std.alloc::alloc_error {
        std.string::string text = this->text();
        std.format::append_str(out, text.as_str());
    }
};

/* The part with every percent-encoded triplet in upper-case hexadecimal and the triplets of
   unreserved characters decoded (RFC 3986 section 6.2.2). */
protected std.string::string normalize_encoding(str part) throws std.alloc::alloc_error {
    const u8[] bytes = part;
    std.string::string result = std.string::with_capacity(len(bytes));
    usize index = 0usize;
    while (index < len(bytes)) {
        u8 value = bytes[index];
        if (value == 37u8 && index + 2usize < len(bytes) &&
            is_hex(bytes[index + 1usize]) == true && is_hex(bytes[index + 2usize]) == true) {
            u8 decoded = (hex_value(bytes[index + 1usize]) * 16u32 + hex_value(bytes[index + 2usize])) as u8;
            if (is_unreserved(decoded) == true) {
                std.string::push_scalar(&result, decoded as char);
            } else {
                push_triplet(&result, decoded);
            }
            index += 3usize;
        } else {
            std.string::append_str(&result, piece(part, index, index + 1usize));
            index += 1usize;
        }
    }
    return move result;
}

protected o<std.string::string> normalize_optional(const (o<std.string::string>)* part)
    throws std.alloc::alloc_error {
    switch (*part) {
    case variant o::some(value): return o::some(normalize_encoding(std.string::as_str(value)));
    case variant o::none: return o::none;
    }
}

/* R-SLIB-URL-0003: the syntax-based normal form of RFC 3986 section 6.2.2 with the scheme-based
   defaults of section 6.2.3 for http, https, ws and wss. */
url url::normalized(const url* this) throws std.alloc::alloc_error {
    url result = empty_url();
    result.scheme_text = copy_text(&this->scheme_text);
    result.authority = this->authority;
    result.userinfo_text = normalize_encoding(this->userinfo_text.as_str());
    result.userinfo_present = this->userinfo_present;
    result.host_text = normalize_encoding(this->host_text.as_str());
    result.ipv6 = this->ipv6;
    o<u16> default_value = default_port(this->scheme_text.as_str());
    switch (this->port_value) {
    case variant o::some(value):
        bool standard = false;
        switch (default_value) {
        case variant o::some(fallback): standard = *fallback == *value;
        case variant o::none: break;
        }
        if (standard == false) { result.port_value = o::some(*value); }
    case variant o::none: break;
    }
    std.string::string path = normalize_encoding(this->path_text.as_str());
    result.path_text = remove_dot_segments(path.as_str());
    switch (default_value) {
    case variant o::some(fallback):
        fallback as void;
        if (std.string::len(&result.path_text) == 0usize && this->authority == true) {
            std.string::append_str(&result.path_text, "/");
        }
    case variant o::none: break;
    }
    result.query_text = normalize_optional(&this->query_text);
    result.fragment_text = normalize_optional(&this->fragment_text);
    return move result;
}

/* R-SLIB-URL-0004: text with every byte except the unreserved characters percent-encoded in
   upper-case hexadecimal. */
std.string::string percent_encode(str text) throws std.alloc::alloc_error {
    const u8[] bytes = text;
    std.string::string result = std.string::with_capacity(len(bytes));
    for (usize index = 0usize; index < len(bytes); index += 1usize) {
        u8 value = bytes[index];
        if (is_unreserved(value) == true) {
            std.string::push_scalar(&result, value as char);
        } else {
            push_triplet(&result, value);
        }
    }
    return move result;
}

/* Decodes the triplets of text, and '+' as a space when form is true, into UTF-8. */
protected std.string::string decode_with(str text, bool form) throws url_error, std.alloc::alloc_error {
    const u8[] bytes = text;
    array<u8> decoded = std.array::with_capacity::<u8>(len(bytes));
    usize index = 0usize;
    while (index < len(bytes)) {
        u8 value = bytes[index];
        if (value == 37u8) {
            throw (index + 2usize >= len(bytes)) failure(error_code::invalid_percent_encoding, index);
            throw (is_hex(bytes[index + 1usize]) == false || is_hex(bytes[index + 2usize]) == false)
                failure(error_code::invalid_percent_encoding, index);
            value = (hex_value(bytes[index + 1usize]) * 16u32 + hex_value(bytes[index + 2usize])) as u8;
            index += 3usize;
        } else {
            if (form == true && value == 43u8) { value = 32u8; }
            index += 1usize;
        }
        append(&decoded, value);
    }
    std.string::from_bytes_result checked = std.string::from_bytes(move decoded);
    switch (move checked) {
    case variant std.string::from_bytes_result::valid(move text_value): return move text_value;
    case variant std.string::from_bytes_result::invalid(move rejected):
        throw failure(error_code::invalid_utf8, rejected.index);
    }
}

/* R-SLIB-URL-0004: the UTF-8 text that percent-encoded text stands for. */
std.string::string percent_decode(str text) throws url_error, std.alloc::alloc_error {
    return decode_with(text, false);
}

/* R-SLIB-URL-0005: one name and value of a query in the application/x-www-form-urlencoded form. */
struct query_pair { std.string::string name; std.string::string value; };

/* R-SLIB-URL-0005: the pairs of a query separated by '&', each split at its first '=', with
   '+' read as a space and the triplets decoded; empty pieces are skipped. */
array<query_pair> parse_query(str query) throws url_error, std.alloc::alloc_error {
    array<query_pair> pairs = std.array::create::<query_pair>();
    const u8[] bytes = query;
    usize start = 0usize;
    while (start <= len(bytes)) {
        usize end = find_any(bytes, start, 38u8, 38u8, 38u8);
        if (end > start) {
            usize equal = find_any(bytes[0usize..end], start, 61u8, 61u8, 61u8);
            std.string::string name = decode_with(piece(query, start, equal), true);
            usize value_start = end;
            if (equal < end) { value_start = equal + 1usize; }
            std.string::string value = decode_with(piece(query, value_start, end), true);
            append(&pairs, query_pair {.name = move name, .value = move value});
        }
        start = end + 1usize;
    }
    return move pairs;
}

protected void form_encode(std.string::string* out, str text) throws std.alloc::alloc_error {
    const u8[] bytes = text;
    for (usize index = 0usize; index < len(bytes); index += 1usize) {
        u8 value = bytes[index];
        if (is_unreserved(value) == true) {
            std.string::push_scalar(out, value as char);
            continue;
        }
        if (value == 32u8) {
            std.string::push_scalar(out, 43u8 as char);
            continue;
        }
        push_triplet(out, value);
    }
}

/* R-SLIB-URL-0005: appends name=value to a query in the form encoding, after '&' when the query
   is not empty. */
void append_query(std.string::string* query, str name, str value) throws std.alloc::alloc_error {
    if (std.string::len(query) != 0usize) { std.string::append_str(query, "&"); }
    form_encode(query, name);
    std.string::append_str(query, "=");
    form_encode(query, value);
}

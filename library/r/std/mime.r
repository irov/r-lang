module std.mime;
import std.text;

/* R-SLIB-MIME-0001: a media type text that RFC 9110 section 8.3.1 does not allow; offset is the
   byte where it was found. */
error mime_error { usize offset; };

/* R-SLIB-MIME-0001: one parameter of a media type; the name is in lower case and the value
   without quotes and escapes. */
struct parameter { std.string::string name; std.string::string value; };

/* R-SLIB-MIME-0001: a media type: its type and subtype in lower case and its parameters in the
   order of the text. */
struct media_type {
    protected std.string::string kind_text;
    protected std.string::string subtype_text;
    protected array<parameter> parameters;
};

/* RFC 9110 tchar: "!" "#" "$" "%" "&" "'" "*" "+" "-" "." "^" "_" "`" "|" "~", DIGIT, ALPHA. */
bool is_token_byte(u8 value) {
    if ((value >= 48u8 && value <= 57u8) || (value >= 65u8 && value <= 90u8) ||
        (value >= 97u8 && value <= 122u8)) {
        return true;
    }
    return value == 33u8 || (value >= 35u8 && value <= 39u8) || value == 42u8 || value == 43u8 ||
           value == 45u8 || value == 46u8 || value == 94u8 || value == 95u8 || value == 96u8 ||
           value == 124u8 || value == 126u8;
}

/* Whether text is a nonempty token of RFC 9110 section 5.6.2. */
bool is_token(str text) {
    const u8[] bytes = text;
    if (len(bytes) == 0usize) { return false; }
    for (usize index = 0usize; index < len(bytes); index += 1usize) {
        if (is_token_byte(bytes[index]) == false) { return false; }
    }
    return true;
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

protected u8 lower(u8 value) {
    if (value >= 65u8 && value <= 90u8) { return ((value as u32) + 32u32) as u8; }
    return value;
}

protected std.string::string lowered(const u8[] bytes) throws std.alloc::alloc_error {
    std.string::string result = std.string::with_capacity(len(bytes));
    for (usize index = 0usize; index < len(bytes); index += 1usize) {
        std.string::push_scalar(&result, lower(bytes[index]) as char);
    }
    return move result;
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

protected usize skip_space(const u8[] bytes, usize position) {
    usize index = position;
    while (index < len(bytes) && (bytes[index] == 32u8 || bytes[index] == 9u8)) { index += 1usize; }
    return index;
}

protected usize token_end(const u8[] bytes, usize position) {
    usize index = position;
    while (index < len(bytes) && is_token_byte(bytes[index]) == true) { index += 1usize; }
    return index;
}

/* R-SLIB-MIME-0001: parses type "/" subtype and parameters separated by ";" with optional white
   space; a value is a token or a quoted string with backslash escapes. A parameter without a
   value, an empty token or bytes after the last parameter are errors. */
media_type parse(str text) throws mime_error, std.alloc::alloc_error {
    const u8[] bytes = text;
    usize start = skip_space(bytes, 0usize);
    usize kind_end = token_end(bytes, start);
    throw (kind_end == start) mime_error {.offset = start};
    throw (kind_end >= len(bytes) || bytes[kind_end] != 47u8) mime_error {.offset = kind_end};
    usize subtype_end = token_end(bytes, kind_end + 1usize);
    throw (subtype_end == kind_end + 1usize) mime_error {.offset = subtype_end};
    media_type result = media_type {.kind_text = lowered(bytes[start..kind_end]),
                                    .subtype_text = lowered(bytes[kind_end + 1usize..subtype_end]),
                                    .parameters = std.array::create::<parameter>()};
    usize position = skip_space(bytes, subtype_end);
    while (position < len(bytes)) {
        throw (bytes[position] != 59u8) mime_error {.offset = position};
        usize name_start = skip_space(bytes, position + 1usize);
        if (name_start == len(bytes)) {
            // A trailing ";" ends the text.
            position = name_start;
            continue;
        }
        usize name_end = token_end(bytes, name_start);
        throw (name_end == name_start) mime_error {.offset = name_start};
        throw (name_end >= len(bytes) || bytes[name_end] != 61u8) mime_error {.offset = name_end};
        std.string::string name = lowered(bytes[name_start..name_end]);
        usize value_start = name_end + 1usize;
        array<u8> value = std.array::create::<u8>();
        usize value_end = value_start;
        if (value_start < len(bytes) && bytes[value_start] == 34u8) {
            usize index = value_start + 1usize;
            bool closed = false;
            while (index < len(bytes) && closed == false) {
                u8 byte = bytes[index];
                if (byte == 34u8) {
                    closed = true;
                    index += 1usize;
                    continue;
                }
                if (byte == 92u8) {
                    throw (index + 1usize >= len(bytes)) mime_error {.offset = index};
                    append(&value, bytes[index + 1usize]);
                    index += 2usize;
                    continue;
                }
                throw (byte < 32u8 && byte != 9u8) mime_error {.offset = index};
                append(&value, byte);
                index += 1usize;
            }
            throw (closed == false) mime_error {.offset = value_start};
            value_end = index;
        } else {
            value_end = token_end(bytes, value_start);
            throw (value_end == value_start) mime_error {.offset = value_start};
            for (usize index = value_start; index < value_end; index += 1usize) {
                append(&value, bytes[index]);
            }
        }
        std.string::from_bytes_result checked = std.string::from_bytes(move value);
        switch (move checked) {
        case variant std.string::from_bytes_result::valid(move decoded):
            append(&result.parameters, parameter {.name = move name, .value = move decoded});
        case variant std.string::from_bytes_result::invalid(move rejected):
            throw mime_error {.offset = value_start + rejected.index};
        }
        position = skip_space(bytes, value_end);
    }
    return move result;
}

/* R-SLIB-MIME-0001: the type and the subtype in lower case. */
str media_type::kind(const media_type* this) { return this->kind_text; }
str media_type::subtype(const media_type* this) { return this->subtype_text; }

/* R-SLIB-MIME-0001: "type/subtype" in lower case, without parameters. */
std.string::string media_type::essence(const media_type* this) throws std.alloc::alloc_error {
    std.string::string result = std.string::from_str(this->kind_text);
    std.string::append_str(&result, "/");
    std.string::append_str(&result, this->subtype_text);
    return move result;
}

/* R-SLIB-MIME-0001: the value of the first parameter with the name, compared without ASCII
   case. */
o<str> media_type::parameter(const media_type* this, str name) {
    for (usize index = 0usize; index < len(this->parameters); index += 1usize) {
        if (std.text::equal_ignore_ascii_case(this->parameters[index].name, name) == true) {
            return o::some(this->parameters[index].value);
        }
    }
    return o::none;
}

/* The number of parameters, and each by its position. */
usize media_type::parameter_count(const media_type* this) { return len(this->parameters); }

impl core::Format for media_type {
    /* type/subtype, then "; name=value" for each parameter, the value quoted when it is not a
       token. */
    void format(const media_type* this, std.format::builder* out) throws std.alloc::alloc_error {
        std.format::append_str(out, this->kind_text);
        std.format::append_str(out, "/");
        std.format::append_str(out, this->subtype_text);
        for (usize index = 0usize; index < len(this->parameters); index += 1usize) {
            std.format::append_str(out, "; ");
            std.format::append_str(out, this->parameters[index].name);
            std.format::append_str(out, "=");
            str value = this->parameters[index].value;
            if (is_token(value) == true) {
                std.format::append_str(out, value);
                continue;
            }
            std.format::append_str(out, "\"");
            const u8[] bytes = value;
            usize start = 0usize;
            for (usize at = 0usize; at < len(bytes); at += 1usize) {
                if (bytes[at] == 34u8 || bytes[at] == 92u8) {
                    std.format::append_str(out, piece(value, start, at));
                    std.format::append_str(out, "\\");
                    start = at;
                }
            }
            std.format::append_str(out, piece(value, start, len(bytes)));
            std.format::append_str(out, "\"");
        }
    }
};

/* R-SLIB-MIME-0002: the media type of a file name extension without its dot, compared without
   ASCII case. */
o<str> from_extension(str extension) {
    switch (std.text::ignore_ascii_case(extension)) {
    case "html": return o::some("text/html; charset=utf-8");
    case "htm": return o::some("text/html; charset=utf-8");
    case "css": return o::some("text/css; charset=utf-8");
    case "js": return o::some("text/javascript; charset=utf-8");
    case "mjs": return o::some("text/javascript; charset=utf-8");
    case "json": return o::some("application/json");
    case "txt": return o::some("text/plain; charset=utf-8");
    case "csv": return o::some("text/csv; charset=utf-8");
    case "md": return o::some("text/markdown; charset=utf-8");
    case "xml": return o::some("application/xml");
    case "svg": return o::some("image/svg+xml");
    case "png": return o::some("image/png");
    case "jpg": return o::some("image/jpeg");
    case "jpeg": return o::some("image/jpeg");
    case "gif": return o::some("image/gif");
    case "webp": return o::some("image/webp");
    case "ico": return o::some("image/vnd.microsoft.icon");
    case "wasm": return o::some("application/wasm");
    case "pdf": return o::some("application/pdf");
    case "zip": return o::some("application/zip");
    case "gz": return o::some("application/gzip");
    case "tar": return o::some("application/x-tar");
    case "mp3": return o::some("audio/mpeg");
    case "mp4": return o::some("video/mp4");
    case "webm": return o::some("video/webm");
    case "woff": return o::some("font/woff");
    case "woff2": return o::some("font/woff2");
    case "otf": return o::some("font/otf");
    case "ttf": return o::some("font/ttf");
    default: return o::none;
    }
}

/* R-SLIB-MIME-0002: the media type of a path by the extension of its last segment, and
   application/octet-stream when it has none or an unknown one. */
str for_path(str path) {
    const u8[] bytes = path;
    usize dot = len(bytes);
    for (usize index = 0usize; index < len(bytes); index += 1usize) {
        if (bytes[index] == 46u8) { dot = index; }
        if (bytes[index] == 47u8) { dot = len(bytes); }
    }
    if (dot + 1usize < len(bytes)) {
        switch (from_extension(piece(path, dot + 1usize, len(bytes)))) {
        case variant o::some(found): return *found;
        case variant o::none: break;
        }
    }
    return "application/octet-stream";
}

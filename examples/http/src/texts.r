module example.http.texts;
import std.console;
import std.url;
import std.mime;

protected void line(std.string::string* out, std.string::string text) throws std.alloc::alloc_error {
    std.string::append_str(out, text);
    std.string::append_str(out, "\n");
}

/* The parts of a URL, a reference resolved against it, its normal form and its query. */
protected std.string::string url_report(str text, o<str> reference)
    throws std.url::url_error, std.alloc::alloc_error {
    std.string::string out = std.string::create();
    std.url::url value = std.url::parse(text);
    str scheme = value.scheme();
    str host = value.host();
    str path = value.path();
    u16 port = 0u16;
    switch (value.effective_port()) {
    case variant o::some(number): port = *number;
    case variant o::none: break;
    }
    line(&out, f"scheme={scheme} host={host} port={port} path={path}");
    std.url::url normal = value.normalized();
    line(&out, f"normalized={normal}");
    switch (value.query()) {
    case variant o::some(query):
        array<std.url::query_pair> pairs = std.url::parse_query(*query);
        for (usize index = 0usize; index < len(pairs); index += 1usize) {
            str name = pairs[index].name;
            str given = pairs[index].value;
            line(&out, f"query {name}={given}");
        }
    case variant o::none: break;
    }
    switch (reference) {
    case variant o::some(relative):
        std.url::url target = value.resolve(*relative);
        line(&out, f"resolved={target}");
    case variant o::none: break;
    }
    std.string::string rebuilt = std.string::create();
    std.url::append_query(&rebuilt, "path", path);
    std.string::string encoded = std.url::percent_encode(path);
    std.string::string decoded = std.url::percent_decode(encoded);
    bool has_default = false;
    switch (std.url::default_port(scheme)) {
    case variant o::some(number):
        number as void;
        has_default = true;
    case variant o::none: break;
    }
    line(&out, f"form={rebuilt} encoded={encoded} decoded={decoded} default={has_default}");
    return move out;
}

/* The report of a URL, or where it is invalid with status 65. */
protected std.string::string url_output(str text, o<str> reference, i32* status)
    throws std.alloc::alloc_error {
    try {
        return url_report(text, reference);
    } catch (std.url::url_error failure) {
        std.url::error_code code = failure.code;
        usize offset = failure.offset;
        *status = 65;
        return f"invalid url: {code} at {offset}\n";
    }
}

/* Prints the report of a URL, or where the URL is invalid. */
async i32 show_url(std.string::string text, o<std.string::string> reference)
    throws std.error::fault {
    i32 status = 0;
    o<str> relative = o::none;
    switch (reference) {
    case variant o::some(given): relative = o::some(*given);
    case variant o::none: break;
    }
    std.string::string report = url_output(text, relative, &status);
    await std.console::print(move report);
    return status;
}

/* The media type of a text with '/', or of a path. */
protected std.string::string mime_report(str text) throws std.mime::mime_error, std.alloc::alloc_error {
    std.string::string out = std.string::create();
    const u8[] bytes = text;
    bool has_slash = false;
    switch (std.bytes::find(bytes, 47u8)) {
    case variant o::some(at):
        at as void;
        has_slash = true;
    case variant o::none: break;
    }
    if (has_slash == false) {
        str found = std.mime::for_path(text);
        usize dot = len(bytes);
        for (usize index = 0usize; index < len(bytes); index += 1usize) {
            if (bytes[index] == 46u8) { dot = index; }
        }
        str extension = "";
        if (dot < len(bytes)) {
            try {
                extension = core::validate_utf8(bytes[dot + 1usize..len(bytes)]);
            } catch (core::utf8_error rejected) {
                rejected as void;
            }
        }
        bool known = false;
        switch (std.mime::from_extension(extension)) {
        case variant o::some(kind):
            kind as void;
            known = true;
        case variant o::none: break;
        }
        line(&out, f"{found} (extension {known})");
        return move out;
    }
    std.mime::media_type value = std.mime::parse(text);
    std.string::string essence = value.essence();
    usize count = value.parameter_count();
    line(&out, f"essence={essence} parameters={count} text={value}");
    switch (value.parameter("charset")) {
    case variant o::some(charset):
        bool token = std.mime::is_token(*charset);
        const u8[] first = *charset;
        bool starts_token = len(first) > 0usize && std.mime::is_token_byte(first[0usize]) == true;
        std.mime::parameter shown = {.name = std.string::from_str("charset"),
                                     .value = std.string::from_str(*charset)};
        line(&out, f"{shown.name}={shown.value} token={token} {starts_token}");
    case variant o::none: break;
    }
    return move out;
}

/* The report of a media type, or where it is invalid with status 65. */
protected std.string::string mime_output(str text, i32* status) throws std.alloc::alloc_error {
    try {
        return mime_report(text);
    } catch (std.mime::mime_error failure) {
        usize offset = failure.offset;
        *status = 65;
        return f"invalid media type at {offset}\n";
    }
}

/* Prints the report of a media type, or where it is invalid. */
async i32 show_mime(std.string::string text) throws std.error::fault {
    i32 status = 0;
    std.string::string report = mime_output(text, &status);
    await std.console::print(move report);
    return status;
}

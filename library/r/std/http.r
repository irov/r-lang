module std.http;
import std.stream;
import std.bufio;
import std.text;
import std.mime;
import std.url;
import std.net;
import std.tls;
import std.service;
import std.metrics;
import std.time;
import std.deflate;
import std.console;

/* R-SLIB-HTTP-0001: why a message could not be read, written or exchanged. */
@derive(format)
enum error_code {
    invalid_request_line,
    invalid_status_line,
    invalid_header,
    head_too_large,
    too_many_headers,
    body_too_large,
    invalid_length,
    invalid_chunk,
    unsupported_method,
    unsupported_version,
    unsupported_encoding,
    invalid_encoding,
    unexpected_end,
    invalid_url,
    unsupported_scheme,
    too_many_redirects,
    timed_out,
};

/* R-SLIB-HTTP-0001: a failure of HTTP. */
error http_error { error_code code; };

protected http_error failure(error_code code) { return http_error {.code = code}; }

/* R-SLIB-HTTP-0001: the request methods of RFC 9110 section 9. */
@derive(format)
enum method { get, head, post, put, delete, patch, options, trace, connect };

/* R-SLIB-HTTP-0001: the name of a method as a request line writes it. */
str method_name(method value) {
    switch (value) {
    case method::get: return "GET";
    case method::head: return "HEAD";
    case method::post: return "POST";
    case method::put: return "PUT";
    case method::delete: return "DELETE";
    case method::patch: return "PATCH";
    case method::options: return "OPTIONS";
    case method::trace: return "TRACE";
    default: return "CONNECT";
    }
}

/* R-SLIB-HTTP-0001: the method of a name; method names are case-sensitive. */
o<method> parse_method(str name) {
    switch (name) {
    case "GET": return o::some(method::get);
    case "HEAD": return o::some(method::head);
    case "POST": return o::some(method::post);
    case "PUT": return o::some(method::put);
    case "DELETE": return o::some(method::delete);
    case "PATCH": return o::some(method::patch);
    case "OPTIONS": return o::some(method::options);
    case "TRACE": return o::some(method::trace);
    case "CONNECT": return o::some(method::connect);
    default: return o::none;
    }
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

/* R-SLIB-HTTP-0002: one field of a header section. */
struct header { std.string::string name; std.string::string value; };

/* R-SLIB-HTTP-0002: the fields of a header section in the order they were added. */
struct headers { protected array<header> entries; };

headers headers::create() {
    return headers {.entries = std.array::create::<header>()};
}

/* Whether a field value holds only visible characters, spaces and tabs (RFC 9110 section 5.5). */
protected bool valid_value(str value) {
    const u8[] bytes = value;
    for (usize index = 0usize; index < len(bytes); index += 1usize) {
        u8 byte = bytes[index];
        if (byte == 127u8 || (byte < 32u8 && byte != 9u8)) { return false; }
    }
    return true;
}

/* R-SLIB-HTTP-0002: adds a field after the others; the name shall be a token and the value
   shall hold no control character but a tab. */
void headers::add(headers* this, str name, str value) throws http_error, std.alloc::alloc_error {
    throw (std.mime::is_token(name) == false || valid_value(value) == false)
        failure(error_code::invalid_header);
    append(&this->entries, header {.name = std.string::from_str(name),
                                   .value = std.string::from_str(std.text::trim(value))});
}

/* R-SLIB-HTTP-0002: removes the fields with the name, compared without ASCII case, and returns
   their count. */
usize headers::remove(headers* this, str name) throws std.alloc::alloc_error {
    array<header> kept = std.array::create::<header>();
    usize removed = 0usize;
    for (usize index = 0usize; index < len(this->entries); index += 1usize) {
        header entry = core::replace(&this->entries[index],
                                     header {.name = std.string::create(),
                                             .value = std.string::create()});
        if (std.text::equal_ignore_ascii_case(entry.name, name) == true) {
            removed += 1usize;
            drop entry;
            continue;
        }
        append(&kept, move entry);
    }
    array<header> old = core::replace(&this->entries, move kept);
    drop old;
    return removed;
}

/* R-SLIB-HTTP-0002: replaces the fields with the name by one field. */
void headers::set(headers* this, str name, str value) throws http_error, std.alloc::alloc_error {
    usize removed = this->remove(name);
    removed as void;
    this->add(name, value);
}

/* R-SLIB-HTTP-0002: the value of the first field with the name, compared without ASCII case. */
o<str> headers::get(const headers* this, str name) {
    for (usize index = 0usize; index < len(this->entries); index += 1usize) {
        if (std.text::equal_ignore_ascii_case(this->entries[index].name, name) == true) {
            return o::some(this->entries[index].value);
        }
    }
    return o::none;
}

bool headers::contains(const headers* this, str name) {
    switch (this->get(name)) {
    case variant o::some(value):
        value as void;
        return true;
    case variant o::none: return false;
    }
}

/* R-SLIB-HTTP-0002: whether a field with the name lists the token among its comma-separated
   elements, compared without ASCII case (as Connection and Transfer-Encoding do). */
bool headers::has_token(const headers* this, str name, str token) {
    for (usize index = 0usize; index < len(this->entries); index += 1usize) {
        if (std.text::equal_ignore_ascii_case(this->entries[index].name, name) == false) {
            continue;
        }
        for (str element in std.text::split(this->entries[index].value, ",")) {
            if (std.text::equal_ignore_ascii_case(std.text::trim(element), token) == true) {
                return true;
            }
        }
    }
    return false;
}

/* The number of fields and each by its position. */
usize headers::count(const headers* this) { return len(this->entries); }
str headers::name_at(const headers* this, usize index) { return this->entries[index].name; }
str headers::value_at(const headers* this, usize index) { return this->entries[index].value; }

/* A path parameter that a route captured. */
protected struct param { std.string::string name; std.string::string value; };

/* R-SLIB-HTTP-0003: a request: its method, target, minor version of HTTP/1, header section and
   whole body. */
struct request {
    method method;
    std.string::string target;
    u8 minor_version;
    headers headers;
    bytes body;
    protected array<param> params;
};

request request::create(method value, str target) throws std.alloc::alloc_error {
    return request {.method = value, .target = std.string::from_str(target), .minor_version = 1u8,
                    .headers = headers::create(), .body = std.bytes::with_capacity(0usize),
                    .params = std.array::create::<param>()};
}

/* R-SLIB-HTTP-0003: the target before its query, and the query after '?'. */
str request::path(const request* this) {
    const u8[] bytes = this->target;
    switch (std.bytes::find(bytes, 63u8)) {
    case variant o::some(at): return piece(this->target, 0usize, *at);
    case variant o::none: return this->target;
    }
}

o<str> request::query(const request* this) {
    const u8[] bytes = this->target;
    switch (std.bytes::find(bytes, 63u8)) {
    case variant o::some(at): return o::some(piece(this->target, *at + 1usize, len(bytes)));
    case variant o::none: return o::none;
    }
}

/* R-SLIB-HTTP-0006: the value of a path parameter that the route of the request captured. */
o<str> request::param(const request* this, str name) {
    for (usize index = 0usize; index < len(this->params); index += 1usize) {
        if (std.bytes::equal(this->params[index].name, name) == true) {
            return o::some(this->params[index].value);
        }
    }
    return o::none;
}

/* R-SLIB-HTTP-0016 (M42): the value of the cookie with the name in the Cookie fields of the
   request: the first of the `name=value` pairs separated by `;` (RFC 6265 section 5.4) with that
   name, without the double quotes around the value. */
o<str> request::cookie(const request* this, str name) {
    for (usize index = 0usize; index < len(this->headers.entries); index += 1usize) {
        if (std.text::equal_ignore_ascii_case(this->headers.entries[index].name, "Cookie") == false) {
            continue;
        }
        for (str pair in std.text::split(this->headers.entries[index].value, ";")) {
            str item = std.text::trim(pair);
            const u8[] bytes = item;
            switch (std.bytes::find(bytes, 61u8)) {
            case variant o::some(at):
                if (std.bytes::equal(std.text::trim(piece(item, 0usize, *at)), name) == true) {
                    str value = std.text::trim(piece(item, *at + 1usize, len(bytes)));
                    const u8[] text = value;
                    if (len(text) >= 2usize && text[0usize] == 34u8 && text[len(text) - 1usize] == 34u8) {
                        return o::some(piece(value, 1usize, len(text) - 1usize));
                    }
                    return o::some(value);
                }
            case variant o::none: break;
            }
        }
    }
    return o::none;
}

/* R-SLIB-HTTP-0003: a response: its status code, header section and whole body. */
struct response { u16 status; headers headers; bytes body; };

response response::create(u16 status) {
    return response {.status = status, .headers = headers::create(), .body = {}};
}

/* R-SLIB-HTTP-0016 (M42): a cookie that a response sets, with the attributes of RFC 6265
   section 4.1.2. */
struct cookie {
    std.string::string name;
    std.string::string value;
    o<std.string::string> path = o::none;
    o<std.string::string> domain = o::none;
    o<i64> max_age = o::none;
    bool secure = false;
    bool http_only = false;
    o<std.string::string> same_site = o::none;
};

protected void append_attribute(std.string::string* line, str name, const (o<std.string::string>)* value)
    throws std.alloc::alloc_error {
    switch (*value) {
    case variant o::some(text):
        line->append("; ");
        line->append(name);
        line->append("=");
        line->append(*text);
    case variant o::none: break;
    }
}

/* R-SLIB-HTTP-0016 (M42): adds a Set-Cookie field for the cookie; its name shall be a token. */
void response::set_cookie(response* this, const cookie* value) throws http_error, std.alloc::alloc_error {
    throw (std.mime::is_token(value->name) == false) failure(error_code::invalid_header);
    std.string::string line = std.string::from_str(value->name);
    line.append("=");
    line.append(value->value);
    append_attribute(&line, "Path", &value->path);
    append_attribute(&line, "Domain", &value->domain);
    switch (value->max_age) {
    case variant o::some(seconds):
        i64 count = *seconds;
        std.string::string age = f"; Max-Age={count}";
        line.append(age);
    case variant o::none: break;
    }
    if (value->secure == true) { line.append("; Secure"); }
    if (value->http_only == true) { line.append("; HttpOnly"); }
    append_attribute(&line, "SameSite", &value->same_site);
    this->headers.add("Set-Cookie", line);
}

/* One part of a streamed response: its head, then its chunks. */
protected enum part { head(response), chunk(bytes) };

/* R-SLIB-HTTP-0009: the body of a streamed response as its handler writes it: first the head,
   then chunks, each sent to the connection as it arrives; the body ends when the writer is
   dropped. */
struct body_writer {
    protected std.sync::sync_sender<part> sender;
    protected bool started;
};

/* Sends a part, waiting while the connection is behind; false when the connection is gone. */
@scoped
protected async bool send_part(const (std.sync::sync_sender<part>)* sender, part item)
    throws std.error::fault {
    std.sync::reserve_result<part> room = await std.sync::reserve(sender);
    switch (move room) {
    case variant std.sync::reserve_result::reserved(move permit):
        std.sync::send_permit(move permit, move item);
        return true;
    case variant std.sync::reserve_result::disconnected:
        drop item;
        return false;
    }
    return false;
}

/* R-SLIB-HTTP-0009: sends the head of the response; only the first head counts. Returns false
   when the connection is gone. */
@scoped
async bool body_writer::start(body_writer* this, response first) throws std.error::fault {
    if (this->started == true) {
        drop first;
        return true;
    }
    this->started = true;
    task_scope(1) io { return await send_part(&this->sender, part::head(move first)); }
}

/* R-SLIB-HTTP-0009: sends one chunk, after a head of status 200 when none was sent. Returns
   false when the connection is gone, as when the client closed it. */
@scoped
async bool body_writer::send(body_writer* this, bytes chunk) throws std.error::fault {
    if (this->started == false) {
        task_scope(1) head {
            bool alive = await this->start(response::create(200u16));
            if (alive == false) {
                drop chunk;
                return false;
            }
        }
    }
    task_scope(1) io { return await send_part(&this->sender, part::chunk(move chunk)); }
}

/* R-SLIB-HTTP-0009: send with the bytes of a text. */
@scoped
async bool body_writer::send_text(body_writer* this, str text) throws std.error::fault {
    bytes chunk = {};
    std.bytes::append(&chunk, text);
    task_scope(1) io { return await this->send(move chunk); }
}

protected response with_body(u16 status, str kind, str body)
    throws http_error, std.alloc::alloc_error {
    response result = response::create(status);
    result.headers.add("Content-Type", kind);
    std.bytes::append(&result.body, body);
    return move result;
}

/* R-SLIB-HTTP-0003: a response with a UTF-8 text body. */
response response::text(u16 status, str body) throws std.alloc::alloc_error {
    try {
        return with_body(status, "text/plain; charset=utf-8", body);
    } catch (http_error rejected) {
        // The media type is a valid field value.
        rejected as void;
    }
    return response::create(status);
}

/* R-SLIB-HTTP-0003: a response with a JSON body. */
response response::json(u16 status, str body) throws std.alloc::alloc_error {
    try {
        return with_body(status, "application/json", body);
    } catch (http_error rejected) {
        rejected as void;
    }
    return response::create(status);
}

/* R-SLIB-HTTP-0003: the reason phrase of a status code of RFC 9110 section 15, empty for others. */
str reason(u16 status) {
    switch (status) {
    case 100u16: return "Continue";
    case 101u16: return "Switching Protocols";
    case 200u16: return "OK";
    case 201u16: return "Created";
    case 202u16: return "Accepted";
    case 204u16: return "No Content";
    case 206u16: return "Partial Content";
    case 301u16: return "Moved Permanently";
    case 302u16: return "Found";
    case 303u16: return "See Other";
    case 304u16: return "Not Modified";
    case 307u16: return "Temporary Redirect";
    case 308u16: return "Permanent Redirect";
    case 400u16: return "Bad Request";
    case 401u16: return "Unauthorized";
    case 403u16: return "Forbidden";
    case 404u16: return "Not Found";
    case 405u16: return "Method Not Allowed";
    case 408u16: return "Request Timeout";
    case 409u16: return "Conflict";
    case 411u16: return "Length Required";
    case 413u16: return "Content Too Large";
    case 414u16: return "URI Too Long";
    case 415u16: return "Unsupported Media Type";
    case 422u16: return "Unprocessable Content";
    case 429u16: return "Too Many Requests";
    case 431u16: return "Request Header Fields Too Large";
    case 500u16: return "Internal Server Error";
    case 501u16: return "Not Implemented";
    case 502u16: return "Bad Gateway";
    case 503u16: return "Service Unavailable";
    case 504u16: return "Gateway Timeout";
    case 505u16: return "HTTP Version Not Supported";
    default: return "";
    }
}

/* R-SLIB-HTTP-0004: the bounds of reading a message. The head of a message, its lines with their
   line ends, has at most max_head bytes and max_headers fields; a whole body at most max_body
   bytes. A server waits request_timeout for each whole request. */
struct limits {
    usize max_head = 16384usize;
    u32 max_headers = 100u32;
    usize max_body = 8388608usize;
    std.time::duration request_timeout = std.time::duration_from_seconds(10i64);
};

/* R-SLIB-HTTP-0004: how the body of a message ends and how much of it is left. */
struct body_state {
    protected u32 framing;
    protected u64 remaining;
    protected bool finished;
};

/* No body, a body of a known length, a chunked body, or a body until the connection closes. */
protected const u32 framing_none = 0u32;
protected const u32 framing_length = 1u32;
protected const u32 framing_chunked = 2u32;
protected const u32 framing_close = 3u32;

protected body_state state_of(u32 framing, u64 length) {
    return body_state {.framing = framing, .remaining = length,
                       .finished = framing == framing_none || (framing == framing_length && length == 0u64)};
}

/* R-SLIB-HTTP-0004: whether the whole body has been read. */
bool body_state::done(const body_state* this) { return this->finished; }

/* The value of a Content-Length field: decimal digits; several fields shall agree. */
protected o<u64> content_length(const headers* fields) throws http_error {
    o<u64> found = o::none;
    for (usize index = 0usize; index < fields->count(); index += 1usize) {
        if (std.text::equal_ignore_ascii_case(fields->name_at(index), "Content-Length") == false) {
            continue;
        }
        const u8[] digits = fields->value_at(index);
        throw (len(digits) == 0usize || len(digits) > 19usize) failure(error_code::invalid_length);
        u64 value = 0u64;
        for (usize at = 0usize; at < len(digits); at += 1usize) {
            u8 digit = digits[at];
            throw (digit < 48u8 || digit > 57u8) failure(error_code::invalid_length);
            value = value * 10u64 + ((digit as u64) - 48u64);
        }
        switch (found) {
        case variant o::some(previous): throw (*previous != value) failure(error_code::invalid_length);
        case variant o::none: found = o::some(value);
        }
    }
    return found;
}

/* The framing of a message with a body by its header section (RFC 9112 section 6.3): chunked
   when Transfer-Encoding ends with chunked, otherwise the Content-Length, otherwise fallback.
   A message with both fields, or with another transfer coding, is rejected. */
protected body_state framing_of(const headers* fields, u32 fallback) throws http_error {
    if (fields->contains("Transfer-Encoding") == true) {
        throw (fields->contains("Content-Length") == true) failure(error_code::invalid_length);
        switch (fields->get("Transfer-Encoding")) {
        case variant o::some(value):
            throw (std.text::equal_ignore_ascii_case(std.text::trim(*value), "chunked") == false)
                failure(error_code::unsupported_encoding);
        case variant o::none: break;
        }
        return state_of(framing_chunked, 0u64);
    }
    switch (content_length(fields)) {
    case variant o::some(length): return state_of(framing_length, *length);
    case variant o::none: return state_of(fallback, 0u64);
    }
}

/* R-SLIB-HTTP-0004: the framing of the body of a request; a request without Transfer-Encoding
   and Content-Length has no body. */
body_state request::framing(const request* this) throws http_error {
    return framing_of(&this->headers, framing_none);
}

/* The bytes of one line without its line feed and a carriage return before it. */
protected const u8[] line_of(const u8[] line) {
    usize end = len(line);
    if (end > 0usize && line[end - 1usize] == 10u8) { end -= 1usize; }
    if (end > 0usize && line[end - 1usize] == 13u8) { end -= 1usize; }
    return line[0usize..end];
}

/* Reads one line into line and returns its length with its line end; zero at the end of the
   stream. A line longer than the buffer of the reader is a head that is too large. */
@generic<T: std.stream::Stream>
@scoped
protected async usize read_line(std.bufio::reader<T>* input, bytes* line)
    throws http_error, std.error::fault {
    std.array::clear(line);
    try {
        usize count = 0usize;
        task_scope(1) io { count += await input->read_until(10u8, line); }
        return count;
    } catch (std.io::io_error rejected) {
        throw (rejected.code == std.io::error_code::resource_exhausted)
            failure(error_code::head_too_large);
        throw rejected;
    }
}

/* The UTF-8 text of bytes, or the error with the code. */
protected str text_of(const u8[] bytes, error_code code) throws http_error {
    try {
        return core::validate_utf8(bytes);
    } catch (core::utf8_error rejected) {
        rejected as void;
    }
    throw failure(code);
}

/* Parses the field lines of a header section into fields until the empty line; used counts the
   bytes of the head so far. */
@generic<T: std.stream::Stream>
@scoped
protected async void read_fields(std.bufio::reader<T>* input, const limits* bounds, usize used,
                                 headers* fields) throws http_error, std.error::fault {
    bytes line = std.bytes::with_capacity(256usize);
    usize total = used;
    u32 count = 0u32;
    while (true) {
        usize read = 0usize;
        task_scope(1) io { read += await read_line(input, &line); }
        throw (read == 0usize) failure(error_code::unexpected_end);
        total += read;
        throw (total > bounds->max_head) failure(error_code::head_too_large);
        const u8[] text = line_of(line.as_slice());
        if (len(text) == 0usize) { return; }
        // Obsolete line folding and white space before the colon are rejected (RFC 9112 5.1-5.2).
        throw (text[0usize] == 32u8 || text[0usize] == 9u8) failure(error_code::invalid_header);
        count += 1u32;
        throw (count > bounds->max_headers) failure(error_code::too_many_headers);
        usize colon = 0usize;
        while (colon < len(text) && text[colon] != 58u8) { colon += 1usize; }
        throw (colon == 0usize || colon == len(text)) failure(error_code::invalid_header);
        str name = text_of(text[0usize..colon], error_code::invalid_header);
        str value = text_of(text[colon + 1usize..len(text)], error_code::invalid_header);
        fields->add(name, value);
    }
}

/* Parses "HTTP/1.x" and returns x. */
protected u8 version_of(const u8[] text, error_code code) throws http_error {
    throw (len(text) != 8usize || std.bytes::starts_with(text, "HTTP/") == false) failure(code);
    throw (text[5usize] != 49u8 || text[6usize] != 46u8) failure(error_code::unsupported_version);
    throw (text[7usize] != 48u8 && text[7usize] != 49u8) failure(error_code::unsupported_version);
    return ((text[7usize] as u32) - 48u32) as u8;
}

/* R-SLIB-HTTP-0004: reads the request line and the header section of a request; none when the
   stream ends before its first byte. Empty lines before the request line are skipped. */
@generic<T: std.stream::Stream>
@scoped
async o<request> read_request_head(std.bufio::reader<T>* input, const limits* bounds)
    throws http_error, std.error::fault {
    bytes line = std.bytes::with_capacity(256usize);
    usize used = 0usize;
    usize read = 0usize;
    while (true) {
        usize count = 0usize;
        task_scope(1) io { count += await read_line(input, &line); }
        if (count == 0usize) {
            throw (used != 0usize) failure(error_code::unexpected_end);
            return o::none;
        }
        used += count;
        throw (used > bounds->max_head) failure(error_code::head_too_large);
        const u8[] content = line_of(line.as_slice());
        if (len(content) != 0usize) {
            read = count;
            break;
        }
    }
    read as void;
    const u8[] text = line_of(line.as_slice());
    usize first = 0usize;
    while (first < len(text) && text[first] != 32u8) { first += 1usize; }
    usize second = first + 1usize;
    while (second < len(text) && text[second] != 32u8) { second += 1usize; }
    throw (first == 0usize || second >= len(text) || second == first + 1usize)
        failure(error_code::invalid_request_line);
    u8 minor = version_of(text[second + 1usize..len(text)], error_code::invalid_request_line);
    str name = text_of(text[0usize..first], error_code::invalid_request_line);
    str target = text_of(text[first + 1usize..second], error_code::invalid_request_line);
    throw (std.mime::is_token(name) == false) failure(error_code::invalid_request_line);
    const u8[] target_bytes = target;
    for (usize index = 0usize; index < len(target_bytes); index += 1usize) {
        throw (target_bytes[index] <= 32u8 || target_bytes[index] >= 127u8)
            failure(error_code::invalid_request_line);
    }
    method value = method::get;
    switch (parse_method(name)) {
    case variant o::some(known): value = *known;
    case variant o::none: throw failure(error_code::unsupported_method);
    }
    request result = request::create(value, target);
    result.minor_version = minor;
    task_scope(1) io { await read_fields(input, bounds, used, &result.headers); }
    return o::some(move result);
}

/* Reads a chunk-size line and returns the size; the extensions after ';' are ignored. */
@generic<T: std.stream::Stream>
@scoped
protected async u64 read_chunk_size(std.bufio::reader<T>* input) throws http_error, std.error::fault {
    bytes line = std.bytes::with_capacity(32usize);
    usize count = 0usize;
    task_scope(1) io { count += await read_line(input, &line); }
    throw (count == 0usize) failure(error_code::unexpected_end);
    const u8[] text = line_of(line.as_slice());
    u64 size = 0u64;
    usize digits = 0usize;
    while (digits < len(text)) {
        u8 value = text[digits];
        u64 digit = 16u64;
        if (value >= 48u8 && value <= 57u8) { digit = (value as u64) - 48u64; }
        if (value >= 97u8 && value <= 102u8) { digit = (value as u64) - 87u64; }
        if (value >= 65u8 && value <= 70u8) { digit = (value as u64) - 55u64; }
        if (digit == 16u64) { break; }
        throw (digits == 15usize) failure(error_code::invalid_chunk);
        size = size * 16u64 + digit;
        digits += 1usize;
    }
    throw (digits == 0usize) failure(error_code::invalid_chunk);
    throw (digits < len(text) && text[digits] != 59u8 && text[digits] != 32u8 && text[digits] != 9u8)
        failure(error_code::invalid_chunk);
    return size;
}

/* R-SLIB-HTTP-0004: reads the next bytes of a body into target and returns their count, zero at
   the end of the body. A chunked body skips its trailer section. */
@generic<T: std.stream::Stream>
@scoped
async usize read_body(std.bufio::reader<T>* input, body_state* state, u8[] target)
    throws http_error, std.error::fault {
    if (state->finished == true || len(target) == 0usize) { return 0usize; }
    if (state->framing == framing_chunked && state->remaining == 0u64) {
        u64 size = 0u64;
        task_scope(1) io { size += await read_chunk_size(input); }
        if (size == 0u64) {
            // The trailer section ends with an empty line; its fields are not kept.
            limits bounds = {};
            headers trailers = headers::create();
            task_scope(1) io { await read_fields(input, &bounds, 0usize, &trailers); }
            state->finished = true;
            return 0usize;
        }
        state->remaining = size;
    }
    usize wanted = len(target);
    if (state->framing != framing_close && (state->remaining as usize) < wanted &&
        state->remaining < 18446744073709551615u64) {
        wanted = state->remaining as usize;
    }
    usize count = 0usize;
    task_scope(1) io { count += await input->read_into(target[0usize..wanted]); }
    if (count == 0usize) {
        throw (state->framing != framing_close) failure(error_code::unexpected_end);
        state->finished = true;
        return 0usize;
    }
    if (state->framing != framing_close) { state->remaining -= count as u64; }
    if (state->framing == framing_length && state->remaining == 0u64) { state->finished = true; }
    if (state->framing == framing_chunked && state->remaining == 0u64) {
        bytes line = std.bytes::with_capacity(4usize);
        usize end = 0usize;
        task_scope(1) io { end += await read_line(input, &line); }
        const u8[] content = line_of(line.as_slice());
        throw (end == 0usize || len(content) != 0usize) failure(error_code::invalid_chunk);
    }
    return count;
}

/* R-SLIB-HTTP-0004: reads the rest of a body into one buffer of at most limit bytes. */
@generic<T: std.stream::Stream>
@scoped
async bytes read_whole_body(std.bufio::reader<T>* input, body_state* state, usize limit)
    throws http_error, std.error::fault {
    bytes body = std.bytes::with_capacity(0usize);
    u8[4096] chunk = {};
    while (state->finished == false) {
        usize count = 0usize;
        task_scope(1) io { count += await read_body(input, state, &chunk); }
        throw (len(body) + count > limit) failure(error_code::body_too_large);
        std.bytes::append(&body, chunk[0usize..count]);
    }
    return move body;
}

/* R-SLIB-HTTP-0004: reads a whole request, its body at most max_body bytes; none when the
   stream ends before its first byte. */
@generic<T: std.stream::Stream>
@scoped
async o<request> read_request(std.bufio::reader<T>* input, const limits* bounds)
    throws http_error, std.error::fault {
    o<request> head = o::none;
    task_scope(1) io {
        o<request> read = await read_request_head(input, bounds);
        switch (move read) {
        case variant o::some(move value): head = o::some(move value);
        case variant o::none: break;
        }
    }
    switch (move head) {
    case variant o::some(move value):
        body_state state = value.framing();
        throw (state.framing == framing_length && state.remaining > (bounds->max_body as u64))
            failure(error_code::body_too_large);
        task_scope(1) io {
            bytes body = await read_whole_body(input, &state, bounds->max_body);
            value.body = move body;
        }
        return o::some(move value);
    case variant o::none: return o::none;
    }
}

/* Reads the head of the response to a request with the method, skipping interim 1xx
   responses, and leaves the framing of its body in state; when switching, a 101 response ends
   the reading with its head. */
@generic<T: std.stream::Stream>
@scoped
protected async response read_response_head(std.bufio::reader<T>* input, method sent, const limits* bounds,
                                            bool switching, body_state* state)
    throws http_error, std.error::fault {
    bytes line = std.bytes::with_capacity(256usize);
    while (true) {
        usize count = 0usize;
        task_scope(1) io { count += await read_line(input, &line); }
        throw (count == 0usize) failure(error_code::unexpected_end);
        throw (count > bounds->max_head) failure(error_code::head_too_large);
        const u8[] text = line_of(line.as_slice());
        throw (len(text) < 12usize || text[8usize] != 32u8) failure(error_code::invalid_status_line);
        u8 minor = version_of(text[0usize..8usize], error_code::invalid_status_line);
        minor as void;
        u32 status = 0u32;
        for (usize index = 9usize; index < 12usize; index += 1usize) {
            u8 digit = text[index];
            throw (digit < 48u8 || digit > 57u8) failure(error_code::invalid_status_line);
            status = status * 10u32 + (digit as u32) - 48u32;
        }
        throw (status < 100u32 || (len(text) > 12usize && text[12usize] != 32u8))
            failure(error_code::invalid_status_line);
        response result = response::create(status as u16);
        task_scope(1) io { await read_fields(input, bounds, count, &result.headers); }
        if (switching == true && status == 101u32) {
            *state = state_of(framing_none, 0u64);
            return move result;
        }
        if (status < 200u32) {
            drop result;
            continue;
        }
        if (sent == method::head || status == 204u32 || status == 304u32) {
            *state = state_of(framing_none, 0u64);
            return move result;
        }
        *state = framing_of(&result.headers, framing_close);
        return move result;
    }
    throw failure(error_code::unexpected_end);
}

/* read_response; when switching, a 101 response ends the reading with its head. */
@generic<T: std.stream::Stream>
@scoped
protected async response read_response_until(std.bufio::reader<T>* input, method sent,
                                             const limits* bounds, bool switching)
    throws http_error, std.error::fault {
    body_state state = state_of(framing_none, 0u64);
    response result = response::create(0u16);
    task_scope(1) io {
        response head = await read_response_head(input, sent, bounds, switching, &state);
        response old = core::replace(&result, move head);
        drop old;
    }
    throw (state.framing == framing_length && state.remaining > (bounds->max_body as u64))
        failure(error_code::body_too_large);
    task_scope(1) io {
        bytes body = await read_whole_body(input, &state, bounds->max_body);
        result.body = move body;
    }
    return move result;
}

/* R-SLIB-HTTP-0004: reads a whole response to a request with the method. Interim 1xx responses
   are skipped; a response to HEAD and a 204 or 304 response have no body; a response without
   Transfer-Encoding and Content-Length ends with the connection. */
@generic<T: std.stream::Stream>
@scoped
async response read_response(std.bufio::reader<T>* input, method sent, const limits* bounds)
    throws http_error, std.error::fault {
    task_scope(1) io { return await read_response_until(input, sent, bounds, false); }
}

/* Appends "name: value\r\n". */
protected void append_field(std.string::string* head, str name, str value)
    throws std.alloc::alloc_error {
    std.string::append_str(head, name);
    std.string::append_str(head, ": ");
    std.string::append_str(head, value);
    std.string::append_str(head, "\r\n");
}

/* Appends the fields of a header section except those with a name the writer sets itself. */
protected void append_fields(std.string::string* head, const headers* fields)
    throws std.alloc::alloc_error {
    for (usize index = 0usize; index < fields->count(); index += 1usize) {
        str name = fields->name_at(index);
        if (std.text::equal_ignore_ascii_case(name, "Content-Length") == true ||
            std.text::equal_ignore_ascii_case(name, "Transfer-Encoding") == true) {
            continue;
        }
        append_field(head, name, fields->value_at(index));
    }
}

/* R-SLIB-HTTP-0005: writes a request with its body framed by Content-Length. Host is written
   first unless the header section has one. */
@generic<T: std.stream::Stream>
@scoped
async void write_request(const T* output, const request* message, str host)
    throws std.error::fault {
    std.string::string head = std.string::create();
    std.string::append_str(&head, method_name(message->method));
    std.string::append_str(&head, " ");
    std.string::append_str(&head, message->target);
    std.string::append_str(&head, " HTTP/1.1\r\n");
    if (message->headers.contains("Host") == false) { append_field(&head, "Host", host); }
    append_fields(&head, &message->headers);
    usize size = len(message->body);
    if (size != 0usize || message->method == method::post || message->method == method::put ||
        message->method == method::patch) {
        std.string::string length = f"Content-Length: {size}\r\n";
        std.string::append_str(&head, length);
    }
    std.string::append_str(&head, "\r\n");
    task_scope(1) io {
        await output->write_all_from(head);
        if (size != 0usize) { await output->write_all_from(message->body.as_slice()); }
        await output->flush();
    }
}

/* R-SLIB-HTTP-0005: writes a response with its body framed by Content-Length; a response to
   HEAD writes the length but no body, and close adds "Connection: close". */
@generic<T: std.stream::Stream>
@scoped
async void write_response(const T* output, const response* message, bool head, bool close)
    throws std.error::fault {
    std.string::string text = std.string::create();
    u16 status = message->status;
    str phrase = reason(status);
    std.string::string line = f"HTTP/1.1 {status} {phrase}\r\n";
    std.string::append_str(&text, line);
    append_fields(&text, &message->headers);
    if (close == true && message->headers.has_token("Connection", "close") == false) {
        append_field(&text, "Connection", "close");
    }
    usize size = len(message->body);
    if (message->status >= 200u16 && message->status != 204u16 && message->status != 304u16) {
        std.string::string length = f"Content-Length: {size}\r\n";
        std.string::append_str(&text, length);
    }
    std.string::append_str(&text, "\r\n");
    task_scope(1) io {
        await output->write_all_from(text);
        if (head == false && size != 0usize) {
            await output->write_all_from(message->body.as_slice());
        }
        await output->flush();
    }
}

/* R-SLIB-HTTP-0005: writes the head of a response whose body follows in chunks; close adds
   "Connection: close". */
@generic<T: std.stream::Stream>
@scoped
async void write_chunked_head(const T* output, const response* message, bool close)
    throws std.error::fault {
    std.string::string text = std.string::create();
    u16 status = message->status;
    str phrase = reason(status);
    std.string::string line = f"HTTP/1.1 {status} {phrase}\r\n";
    std.string::append_str(&text, line);
    append_fields(&text, &message->headers);
    if (close == true && message->headers.has_token("Connection", "close") == false) {
        append_field(&text, "Connection", "close");
    }
    append_field(&text, "Transfer-Encoding", "chunked");
    std.string::append_str(&text, "\r\n");
    task_scope(1) io { await output->write_all_from(text); }
}

/* R-SLIB-HTTP-0005: writes one chunk of a chunked body; an empty chunk writes nothing. */
@generic<T: std.stream::Stream>
@scoped
async void write_chunk(const T* output, const u8[] data) throws std.error::fault {
    usize size = len(data);
    if (size == 0usize) { return; }
    std.string::string line = std.string::create();
    u8[16] digits = {};
    usize count = 0usize;
    usize value = size;
    while (value != 0usize) {
        u32 digit = (value & 15usize) as u32;
        digits[count] = (digit < 10u32 ? digit + 48u32 : digit + 87u32) as u8;
        count += 1usize;
        value = value >> 4usize;
    }
    while (count > 0usize) {
        count -= 1usize;
        std.string::push_scalar(&line, digits[count] as char);
    }
    std.string::append_str(&line, "\r\n");
    str end_text = "\r\n";
    const u8[] line_end = end_text;
    task_scope(1) io {
        await output->write_all_from(line);
        await output->write_all_from(data);
        await output->write_all_from(line_end);
    }
}

/* R-SLIB-HTTP-0005: ends a chunked body with the last chunk and an empty trailer section. */
@generic<T: std.stream::Stream>
@scoped
async void finish_chunks(const T* output) throws std.error::fault {
    str last_text = "0\r\n\r\n";
    const u8[] last = last_text;
    task_scope(1) io {
        await output->write_all_from(last);
        await output->flush();
    }
}

/* R-SLIB-HTTP-0011: a connection that switched to another protocol: its transport and the
   bytes already read from it that follow the head of the exchange. */
struct upgraded { own dyn(std.stream::Stream)* transport; bytes buffered; };

/* R-SLIB-HTTP-0011: the connection of a request to an upgrade route, before any response. */
struct upgrade { protected own dyn(std.stream::Stream)* transport; protected bytes buffered; };

/* R-SLIB-HTTP-0006: one route of a router: a method, a path pattern and the handler of the
   requests it matches. A pattern is a path whose segments are literal, `{name}` for one
   nonempty segment captured under name, or a final `*` that captures the rest of the path. */
@generic<S: send & sync & unborrowed>
protected struct route {
    method method;
    std.string::string pattern;
    o<async fn(arc S, request) -> response throws(std.error::fault)> handler;
    o<async fn(arc S, request, body_writer) -> void throws(std.error::fault)> streamer;
    o<async fn(arc S, request, upgrade) -> void throws(std.error::fault)> upgrader;
};

/* R-SLIB-HTTP-0006: the routes and hooks of a server with shared state S. A before hook sees
   each request first and may answer it itself; an after hook sees the method, the target and
   the response of each request before it is written. */
@generic<S: send & sync & unborrowed>
struct router {
    protected array<route<S>> routes;
    protected array<fn(const S*, const request*) -> o<response> throws(std.alloc::alloc_error)> before_hooks;
    protected array<fn(method, str, response*) -> void throws(std.alloc::alloc_error)> after_hooks;
    protected o<std.string::string> health_path;
    protected o<std.service::health> health_status;
    protected o<std.string::string> metrics_path;
    protected o<std.metrics::registry> metrics_registry;
};

@generic<S: send & sync & unborrowed>
router<S> router<S>::create() {
    return router<S> {
        .routes = std.array::create::<route<S>>(),
        .before_hooks = std.array::create::<fn(const S*, const request*) -> o<response>
                                                throws(std.alloc::alloc_error)>(),
        .after_hooks = std.array::create::<fn(method, str, response*) -> void
                                               throws(std.alloc::alloc_error)>(),
        .health_path = o::none, .health_status = o::none, .metrics_path = o::none,
        .metrics_registry = o::none};
}

/* R-SLIB-HTTP-0006: adds a route; routes are tried in the order they were added. A pattern shall
   start with '/'. */
@generic<S: send & sync & unborrowed>
void router<S>::add(router<S>* this, method value, str pattern,
                    async fn(arc S, request) -> response throws(std.error::fault) handler)
    throws http_error, std.alloc::alloc_error {
    const u8[] bytes = pattern;
    throw (len(bytes) == 0usize || bytes[0usize] != 47u8) failure(error_code::invalid_url);
    append(&this->routes, route<S> {.method = value, .pattern = std.string::from_str(pattern),
                                    .handler = o::some(handler), .streamer = o::none,
                                    .upgrader = o::none});
}

/* R-SLIB-HTTP-0009: adds a route whose handler streams its response through a body writer; the
   server runs it next to the connection that writes the response. */
@generic<S: send & sync & unborrowed>
void router<S>::add_stream(router<S>* this, method value, str pattern,
                           async fn(arc S, request, body_writer) -> void throws(std.error::fault) streamer)
    throws http_error, std.alloc::alloc_error {
    const u8[] bytes = pattern;
    throw (len(bytes) == 0usize || bytes[0usize] != 47u8) failure(error_code::invalid_url);
    append(&this->routes, route<S> {.method = value, .pattern = std.string::from_str(pattern),
                                    .handler = o::none, .streamer = o::some(streamer),
                                    .upgrader = o::none});
}

/* R-SLIB-HTTP-0011: adds a route whose handler takes over the connection of the request before
   any response: it accepts the upgrade with a 101 response and keeps the connection, or refuses
   it with a response that closes the connection. */
@generic<S: send & sync & unborrowed>
void router<S>::add_upgrade(router<S>* this, method value, str pattern,
                            async fn(arc S, request, upgrade) -> void throws(std.error::fault) upgrader)
    throws http_error, std.alloc::alloc_error {
    const u8[] bytes = pattern;
    throw (len(bytes) == 0usize || bytes[0usize] != 47u8) failure(error_code::invalid_url);
    append(&this->routes, route<S> {.method = value, .pattern = std.string::from_str(pattern),
                                    .handler = o::none, .streamer = o::none,
                                    .upgrader = o::some(upgrader)});
}

/* R-SLIB-HTTP-0013: the server answers GET and HEAD of path itself, before every hook and route:
   200 with "ok" while the service accepts connections, 503 with "stopping" once it stops. */
@generic<S: send & sync & unborrowed>
void router<S>::health(router<S>* this, str path, std.service::health status)
    throws http_error, std.alloc::alloc_error {
    const u8[] bytes = path;
    throw (len(bytes) == 0usize || bytes[0usize] != 47u8) failure(error_code::invalid_url);
    o<std.string::string> old_path = core::replace(&this->health_path, o::some(std.string::from_str(path)));
    drop old_path;
    o<std.service::health> old_status = core::replace(&this->health_status, o::some(move status));
    drop old_status;
}

/* R-SLIB-HTTP-0013: the server answers GET and HEAD of path itself with the registry in the text
   exposition format (R-SLIB-METRICS-0004). */
@generic<S: send & sync & unborrowed>
void router<S>::metrics(router<S>* this, str path, std.metrics::registry registry)
    throws http_error, std.alloc::alloc_error {
    const u8[] bytes = path;
    throw (len(bytes) == 0usize || bytes[0usize] != 47u8) failure(error_code::invalid_url);
    o<std.string::string> old_path = core::replace(&this->metrics_path, o::some(std.string::from_str(path)));
    drop old_path;
    o<std.metrics::registry> old_registry = core::replace(&this->metrics_registry, o::some(move registry));
    drop old_registry;
}

@generic<S: send & sync & unborrowed>
void router<S>::before(router<S>* this,
                       fn(const S*, const request*) -> o<response> throws(std.alloc::alloc_error) hook)
    throws std.alloc::alloc_error {
    append(&this->before_hooks, hook);
}

@generic<S: send & sync & unborrowed>
void router<S>::after(router<S>* this,
                      fn(method, str, response*) -> void throws(std.alloc::alloc_error) hook)
    throws std.alloc::alloc_error {
    append(&this->after_hooks, hook);
}

/* The end of the segment of bytes that starts at position. */
protected usize segment_end(const u8[] bytes, usize position) {
    usize end = position;
    while (end < len(bytes) && bytes[end] != 47u8) { end += 1usize; }
    return end;
}

/* Whether path matches pattern; the captured parameters are added to params. */
protected bool match_path(str pattern, str path, array<param>* params)
    throws std.alloc::alloc_error {
    const u8[] shape = pattern;
    const u8[] given = path;
    usize at = 0usize;
    usize position = 0usize;
    while (at < len(shape) && position < len(given)) {
        if (shape[at] == 47u8 || given[position] == 47u8) {
            if (shape[at] != given[position]) { return false; }
            at += 1usize;
            position += 1usize;
            continue;
        }
        usize shape_end = segment_end(shape, at);
        if (shape_end == at + 1usize && shape[at] == 42u8 && shape_end == len(shape)) {
            append(params, param {.name = std.string::from_str("*"),
                                  .value = std.string::from_str(piece(path, position, len(given)))});
            return true;
        }
        usize given_end = segment_end(given, position);
        if (shape_end - at >= 3usize && shape[at] == 123u8 && shape[shape_end - 1usize] == 125u8) {
            try {
                std.string::string value = std.url::percent_decode(piece(path, position, given_end));
                append(params, param {.name = std.string::from_str(piece(pattern, at + 1usize,
                                                                         shape_end - 1usize)),
                                      .value = move value});
            } catch (std.url::url_error rejected) {
                rejected as void;
                return false;
            }
        } else {
            if (std.bytes::equal(shape[at..shape_end], given[position..given_end]) == false) {
                return false;
            }
        }
        at = shape_end;
        position = given_end;
    }
    if (at + 1usize == len(shape) && shape[at] == 42u8 && position == len(given)) {
        append(params, param {.name = std.string::from_str("*"), .value = std.string::create()});
        return true;
    }
    return at == len(shape) && position == len(given);
}

/* What a server keeps for all its connections: the shared state, the routes, the limits and,
   for HTTPS, the TLS configuration. */
@generic<S: send & sync & unborrowed>
protected struct shared {
    arc S state;
    router<S> routes;
    limits bounds;
    o<arc std.tls::config> tls;
};

/* The response to a request that the server could not read. */
protected response error_response(error_code code) throws std.alloc::alloc_error {
    u16 status = 400u16;
    switch (code) {
    case error_code::head_too_large: status = 431u16;
    case error_code::too_many_headers: status = 431u16;
    case error_code::body_too_large: status = 413u16;
    case error_code::unsupported_method: status = 501u16;
    case error_code::unsupported_encoding: status = 501u16;
    case error_code::unsupported_version: status = 505u16;
    default: break;
    }
    return response::text(status, reason(status));
}

/* The index of the first route whose pattern and method match the request, with its captured
   parameters in captured; allowed collects the methods of the routes whose pattern alone
   matches. */
@generic<S: send & sync & unborrowed>
protected o<usize> find_route(const router<S>* routes, const request* incoming,
                              array<param>* captured, std.string::string* allowed)
    throws std.alloc::alloc_error {
    method sent = incoming->method;
    for (usize index = 0usize; index < len(routes->routes); index += 1usize) {
        array<param> found = std.array::create::<param>();
        if (match_path(routes->routes[index].pattern, incoming->path(), &found) == false) {
            continue;
        }
        method wanted = routes->routes[index].method;
        if (wanted == sent || (sent == method::head && wanted == method::get)) {
            array<param> old = core::replace(captured, move found);
            drop old;
            return o::some(index);
        }
        if (std.string::len(allowed) != 0usize) { std.string::append_str(allowed, ", "); }
        std.string::append_str(allowed, method_name(wanted));
    }
    return o::none;
}

/* Adds a Date field of the current time unless the fields have one; a clock outside the range
   of HTTP-date adds none. */
protected void add_date(headers* fields) throws std.alloc::alloc_error {
    if (fields->contains("Date") == true) { return; }
    try {
        std.time::system_time wall = std.time::system_now();
        std.string::string date = std.time::format_http_date(wall);
        fields->add("Date", date);
    } catch (std.time::time_error rejected) {
        rejected as void;
    } catch (http_error rejected) {
        rejected as void;
    }
}

/* Whether the connection stays open after the response to a request. */
protected bool keeps_alive(const request* incoming) {
    if (incoming->headers.has_token("Connection", "close") == true) { return false; }
    if (incoming->minor_version == 1u8) { return true; }
    return incoming->headers.has_token("Connection", "keep-alive");
}

/* Closes the write direction and reads what the client still sends, at most 64 KiB during at
   most two seconds, so that the unread bytes of a refused request do not reset the connection
   before the client has read the response (a lingering close). */
@generic<T: std.stream::Stream & unborrowed>
@scoped
protected async void linger(std.bufio::reader<T>* input) {
    try {
        std.time::instant now = std.time::monotonic_now();
        std.time::instant limit = now.add(std.time::duration_from_seconds(2i64));
        deadline (limit) {
            u8[4096] scratch = {};
            usize total = 0usize;
            task_scope(1) io {
                await input->source.shutdown();
                while (total < 65536usize) {
                    usize count = await input->read_into(&scratch);
                    if (count == 0usize) { break; }
                    total += count;
                }
            }
        }
    } catch (std.error::fault rejected) {
        rejected as void;
    }
}

/* R-SLIB-HTTP-0011: accepts the upgrade: writes a 101 (Switching Protocols) response with the
   fields and Date, and returns the connection. */
async upgraded upgrade::accept(upgrade this, headers fields) throws std.error::fault {
    response head = response {.status = 101u16, .headers = move fields, .body = {}};
    add_date(&head.headers);
    task_scope(1) io { await write_response(&this.transport, &head, false, false); }
    bytes rest = {};
    std.bytes::append(&rest, this.buffered.as_slice());
    own dyn(std.stream::Stream)* taken =
        match (move this) { case { .transport = move transport }: move transport; };
    return upgraded {.transport = move taken, .buffered = move rest};
}

/* R-SLIB-HTTP-0011: refuses the upgrade: writes the response, which closes the connection. */
async void upgrade::refuse(upgrade this, response answer) throws std.error::fault {
    add_date(&answer.headers);
    own dyn(std.stream::Stream)* taken =
        match (move this) { case { .transport = move transport }: move transport; };
    std.bufio::reader<own dyn(std.stream::Stream)*> input =
        std.bufio::reader<own dyn(std.stream::Stream)*>::create(move taken, 4096usize);
    task_scope(1) io {
        await write_response(&input.source, &answer, false, true);
        await linger(&input);
    }
}

protected bool same_path(const (o<std.string::string>)* wanted, str path) {
    switch (*wanted) {
    case variant o::some(text):
        const u8[] left = *text;
        const u8[] right = path;
        if (len(left) != len(right)) { return false; }
        for (usize index = 0usize; index < len(left); index += 1usize) {
            if (left[index] != right[index]) { return false; }
        }
        return true;
    case variant o::none: break;
    }
    return false;
}

/* R-SLIB-HTTP-0013: the answer of a health or metrics endpoint of the router, if the request asks
   for one. */
@generic<S: send & sync & unborrowed>
protected o<response> endpoint_answer(const router<S>* routes, const request* incoming)
    throws std.alloc::alloc_error {
    if (incoming->method != method::get && incoming->method != method::head) { return o::none; }
    str path = incoming->path();
    if (same_path(&routes->health_path, path) == true) {
        switch (routes->health_status) {
        case variant o::some(status):
            if (status->serving() == true) { return o::some(response::text(200u16, "ok\n")); }
            return o::some(response::text(503u16, "stopping\n"));
        case variant o::none: break;
        }
    }
    if (same_path(&routes->metrics_path, path) == true) {
        switch (routes->metrics_registry) {
        case variant o::some(registry):
            std.string::string text = registry->render();
            response answer = response::create(200u16);
            try {
                answer.headers.add("Content-Type", std.metrics::content_type);
            } catch (http_error rejected) {
                // The media type is a valid field value.
                rejected as void;
            }
            std.bytes::append(&answer.body, text);
            return o::some(move answer);
        case variant o::none: break;
        }
    }
    return o::none;
}

/* What the router makes of a request: an answer of a before hook, 404 or 405, or none and the
   index of the route that answers it, whose parameters are then in the request. */
protected struct routing { o<response> answer; usize index; };

/* R-SLIB-HTTP-0006: the first before hook that answers, otherwise the first route whose pattern
   and method match (HEAD also matches GET routes), otherwise 405 with Allow when only the method
   differs and 404 when no pattern matches. */
@generic<S: send & sync & unborrowed>
protected routing route_request(const shared<S>* context, request* incoming)
    throws std.alloc::alloc_error {
    o<response> served = endpoint_answer(&context->routes, incoming);
    switch (move served) {
    case variant o::some(move value): return routing {.answer = o::some(move value), .index = 0usize};
    case variant o::none: break;
    }
    for (usize index = 0usize; index < len(context->routes.before_hooks); index += 1usize) {
        auto hook = context->routes.before_hooks[index];
        o<response> early = hook(&*context->state, incoming);
        switch (move early) {
        case variant o::some(move value): return routing {.answer = o::some(move value), .index = 0usize};
        case variant o::none: break;
        }
    }
    array<param> captured = std.array::create::<param>();
    std.string::string allowed = std.string::create();
    o<usize> chosen = find_route(&context->routes, incoming, &captured, &allowed);
    switch (chosen) {
    case variant o::some(index):
        array<param> old = core::replace(&incoming->params, move captured);
        drop old;
        drop allowed;
        return routing {.answer = o::none, .index = *index};
    case variant o::none:
        drop captured;
        if (std.string::len(&allowed) == 0usize) {
            return routing {.answer = o::some(response::text(404u16, reason(404u16))), .index = 0usize};
        }
        response refused = response::text(405u16, reason(405u16));
        try {
            refused.headers.add("Allow", allowed);
        } catch (http_error rejected) {
            // Method names are valid field values.
            rejected as void;
        }
        return routing {.answer = o::some(move refused), .index = 0usize};
    }
}

/* Whether a route streams its response. */
@generic<S: send & sync & unborrowed>
protected bool is_streaming(const route<S>* entry) {
    switch (entry->streamer) {
    case variant o::some(value):
        value as void;
        return true;
    case variant o::none: return false;
    }
}

/* The response of a plain handler; a handler that throws is answered with 500. */
@generic<S: send & sync & unborrowed>
protected async response call_handler(arc shared<S> context, usize index, request incoming)
    throws std.error::fault {
    switch (context->routes.routes[index].handler) {
    case variant o::some(handler):
        auto chosen = *handler;
        try {
            return await chosen(std.arc::clone(&context->state), move incoming);
        } catch (std.error::fault rejected) {
            rejected as void;
        }
        return response::text(500u16, reason(500u16));
    case variant o::none:
        drop incoming;
        return response::text(500u16, reason(500u16));
    }
}

/* Lets every after hook see the method, the target and the response, and adds Date. */
@generic<S: send & sync & unborrowed>
protected void finish_hooks(const shared<S>* context, method sent, str target, response* result)
    throws std.alloc::alloc_error {
    for (usize index = 0usize; index < len(context->routes.after_hooks); index += 1usize) {
        auto hook = context->routes.after_hooks[index];
        hook(sent, target, result);
    }
    add_date(&result->headers);
}

/* Writes a whole response and returns whether the connection stays open. */
@generic<T: std.stream::Stream & unborrowed>
@scoped
protected async bool write_plain(std.bufio::reader<T>* input, response result, bool head, bool wanted)
    throws std.error::fault {
    bool keep = wanted == true && result.headers.has_token("Connection", "close") == false;
    task_scope(1) reply {
        await write_response(&input->source, &result, head, keep == false);
        if (keep == false) { await linger(input); }
    }
    return keep;
}

/* What relaying the parts of a streamed response did: whether its head was written and
   whether the connection stays open after it. */
protected struct relayed { bool began; bool keep; };

/* Writes the parts of a streamed response as they arrive: the head with chunked framing, then
   each chunk, until the handler drops its writer. */
@generic<S: send & sync & unborrowed, T: std.stream::Stream>
@scoped
protected async relayed relay_parts(const T* output, std.sync::receiver<part> receiver,
                                    const shared<S>* context, method sent, str target, bool head, bool wanted)
    throws std.error::fault {
    bool began = false;
    bool keep = false;
    bool more = true;
    while (more == true) {
        o<part> next = o::none;
        task_scope(1) io {
            o<part> got = await receiver.receive();
            o<part> old = core::replace(&next, move got);
            drop old;
        }
        switch (move next) {
        case variant o::some(move item):
            switch (move item) {
            case variant part::head(move answer):
                if (began == false) {
                    began = true;
                    finish_hooks(context, sent, target, &answer);
                    keep = wanted == true && answer.headers.has_token("Connection", "close") == false;
                    task_scope(1) io { await write_chunked_head(output, &answer, keep == false); }
                }
            case variant part::chunk(move data):
                if (head == false) {
                    task_scope(1) io { await write_chunk(output, data.as_slice()); }
                }
            }
        case variant o::none: more = false;
        }
    }
    return relayed {.began = began, .keep = keep};
}

/* R-SLIB-HTTP-0009: waits until the peer sends another byte or ends the connection; true when
   the connection ended or failed. A byte that it reads is consumed, so it marks taken: the
   relay may finish as the byte arrives, and then the scope discards this result (M35-2). The
   probe belongs to the caller, so the byte stays there even when this member is cancelled after
   its read took it and taken is never marked (B6-6). */
@generic<T: std.stream::Stream>
@scoped
protected async bool peer_left(const T* source, u8[] probe, const (atomic u32)* taken)
    throws std.error::fault {
    try {
        task_scope(1) io {
            usize count = await source->read_into(probe);
            if (count > 0usize) { core::atomic_store(taken, 1u32, core::memory_order::release); }
            return count == 0usize;
        }
    } catch (std.error::fault rejected) {
        rejected as void;
    }
    return true;
}

/* Runs a streaming handler next to the writer of its response: the head is written with chunked
   framing when it arrives and each chunk as it arrives. A handler that ends without a head is
   answered with 500; a handler that throws after its head leaves the body unfinished and closes
   the connection. A peer that ends the connection meanwhile cancels the handler; a byte that
   arrives meanwhile, such as a pipelined request, closes the connection after the response. */
@generic<S: send & sync & unborrowed, T: std.stream::Stream & unborrowed>
@scoped
protected async bool write_streamed(std.bufio::reader<T>* input, arc shared<S> context,
                                    usize index, request incoming, bool head, bool wanted)
    throws std.error::fault {
    method sent = incoming.method;
    std.string::string target = std.string::from_str(incoming.target);
    auto streamer = context->routes.routes[index].streamer;
    std.sync::sync_channel<part> factory = std.sync::sync_channel::<part>(4usize);
    std.sync::sync_sender<part> sender = std.sync::sync_sender(&factory);
    std.sync::receiver<part> receiver = std.sync::sync_receiver(move factory);
    body_writer writer = body_writer {.sender = move sender, .started = false};
    bool began = false;
    bool keep = false;
    bool failed = false;
    bool gone = false;
    atomic u32 taken = 0u32;
    /* The byte the watcher reads lands here, written by the read itself (B6-6). */
    u8[1] probe = {0u8};
    switch (streamer) {
    case variant o::some(chosen):
        task_scope(3) stream {
            auto producer = (*chosen)(std.arc::clone(&context->state), move incoming, move writer);
            auto relay = relay_parts(&input->source, move receiver, &*context, sent, target, head, wanted);
            auto watcher = peer_left(&input->source, &probe, &taken);
            select (stream) {
            case relayed done = await move relay:
                began = done.began;
                keep = done.keep;
                try {
                    await move producer;
                } catch (std.error::fault rejected) {
                    rejected as void;
                    failed = true;
                }
            case bool left = await move watcher:
                if (left == true) {
                    gone = true;
                    std.async::cancel(move producer);
                } else {
                    relayed done = await move relay;
                    began = done.began;
                    try {
                        await move producer;
                    } catch (std.error::fault rejected) {
                        rejected as void;
                        failed = true;
                    }
                }
            }
            stream.cancel_all();
        }
    case variant o::none:
        drop incoming;
        drop writer;
        drop receiver;
    }
    if (gone == true) {
        drop target;
        return false;
    }
    if (began == false) {
        response refusal = response::text(500u16, reason(500u16));
        finish_hooks(&*context, sent, target, &refusal);
        drop target;
        task_scope(1) plain { return await write_plain(input, move refusal, head, false); }
    }
    drop target;
    if (failed == true) { keep = false; }
    /* A byte of the next request that the watcher consumed cannot be read again: the connection
       ends after this response, as for a pipelined request. The watcher's own count is lost
       when it was cancelled after its read took the byte (Core R-STMT-0018), so the byte in the
       probe, which no valid request begins with as zero, tells as well (B6-6). */
    if (core::atomic_load(&taken, core::memory_order::acquire) == 1u32 || probe[0usize] != 0u8) {
        keep = false;
    }
    task_scope(1) end {
        if (failed == false && head == false) { await finish_chunks(&input->source); }
        if (failed == false && head == true) { await input->source.flush(); }
        if (keep == false) { await linger(input); }
    }
    return keep;
}

/* Whether a route takes over the connection. */
@generic<S: send & sync & unborrowed>
protected bool is_upgrading(const route<S>* entry) {
    switch (entry->upgrader) {
    case variant o::some(value):
        value as void;
        return true;
    case variant o::none: return false;
    }
}

/* A request to an upgrade route and the index of the route. */
protected struct switching { request incoming; usize index; };

/* Serves one request on a connection and returns whether the connection stays open; a request
   to an upgrade route is left in pending, and the connection then serves no other request. */
@generic<S: send & sync & unborrowed, T: std.stream::Stream & unborrowed>
@scoped
protected async bool exchange(std.bufio::reader<T>* input, arc shared<S> context,
                              (o<switching>)* pending)
    throws std.error::fault {
    limits bounds = context->bounds;
    o<request> received = o::none;
    try {
        std.time::instant now = std.time::monotonic_now();
        std.time::instant limit = now.add(bounds.request_timeout);
        deadline (limit) {
            task_scope(1) io {
                o<request> read = await read_request(input, &bounds);
                switch (move read) {
                case variant o::some(move value): received = o::some(move value);
                case variant o::none: break;
                }
            }
        }
    } catch (http_error rejected) {
        response refusal = error_response(rejected.code);
        task_scope(1) io {
            await write_response(&input->source, &refusal, false, true);
            await linger(input);
        }
        return false;
    } catch (std.error::fault rejected) {
        // The client ended the connection or did not send a request in time.
        rejected as void;
        return false;
    }
    switch (move received) {
    case variant o::none: return false;
    case variant o::some(move incoming):
        bool wanted = keeps_alive(&incoming);
        bool head = incoming.method == method::head;
        method sent = incoming.method;
        std.string::string target = std.string::from_str(incoming.target);
        routing plan = route_request(&*context, &incoming);
        o<response> early = core::replace(&plan.answer, o::none);
        switch (move early) {
        case variant o::some(move answer):
            drop incoming;
            finish_hooks(&*context, sent, target, &answer);
            task_scope(1) io { return await write_plain(input, move answer, head, wanted); }
        case variant o::none: break;
        }
        if (is_upgrading(&context->routes.routes[plan.index]) == true) {
            drop target;
            wanted as void;
            head as void;
            sent as void;
            o<switching> old = core::replace(pending, o::some(switching {.incoming = move incoming,
                                                                         .index = plan.index}));
            drop old;
            return false;
        }
        bool streaming = is_streaming(&context->routes.routes[plan.index]);
        if (streaming == true) {
            drop target;
            task_scope(1) io {
                return await write_streamed(input, move context, plan.index, move incoming, head, wanted);
            }
        }
        task_scope(1) io {
            response result = await call_handler(std.arc::clone(&context), plan.index, move incoming);
            finish_hooks(&*context, sent, target, &result);
            return await write_plain(input, move result, head, wanted);
        }
    }
    return false;
}

/* Hands the connection of a request to its upgrade route; a handler that throws ends the
   connection. */
@generic<S: send & sync & unborrowed, T: std.stream::Stream & unborrowed>
protected async void start_upgrade(arc shared<S> context, std.bufio::reader<T> input,
                                   switching chosen)
    throws std.error::fault {
    usize index = chosen.index;
    request incoming = match (move chosen) { case { .incoming = move taken }: move taken; };
    bytes rest = {};
    T source = (move input).into_source(&rest);
    own dyn(std.stream::Stream)* transport = new T(move source);
    switch (context->routes.routes[index].upgrader) {
    case variant o::some(upgrader):
        auto handler = *upgrader;
        upgrade connection = upgrade {.transport = move transport, .buffered = move rest};
        try {
            await handler(std.arc::clone(&context->state), move incoming, move connection);
        } catch (std.error::fault rejected) {
            rejected as void;
        }
    case variant o::none:
        drop incoming;
        drop transport;
        drop rest;
    }
}

/* Serves the requests of one connection until it closes, a response closes it or an upgrade
   route takes it over. */
@generic<S: send & sync & unborrowed, T: std.stream::Stream & unborrowed>
protected async void serve_stream(arc shared<S> context, T transport) throws std.error::fault {
    std.bufio::reader<T> input =
        std.bufio::reader<T>::create(move transport, context->bounds.max_head);
    o<switching> pending = o::none;
    bool open = true;
    while (open == true) {
        task_scope(1) io {
            bool more = await exchange(&input, std.arc::clone(&context), &pending);
            if (more == false) { open = false; }
        }
    }
    switch (move pending) {
    case variant o::some(move chosen): await start_upgrade(move context, move input, move chosen);
    case variant o::none:
        drop input;
        drop context;
    }
}

@generic<S: send & sync & unborrowed>
protected async void serve_tls_connection(arc shared<S> context, std.net::tcp_connection connection)
    throws std.error::fault {
    switch (context->tls) {
    case variant o::some(settings):
        try {
            task_scope(1) io {
                std.tls::stream<std.net::tcp_connection> secured =
                    await std.tls::accept(move connection, &**settings);
                await serve_stream(std.arc::clone(&context), move secured);
            }
        } catch (std.tls::tls_error rejected) {
            // A client that fails the handshake gets no response.
            rejected as void;
        }
    case variant o::none: drop connection;
    }
}

/* R-SLIB-HTTP-0007: serves HTTP/1.1 on the connections of the listener with std.service until
   a stop request: each connection answers its requests in order while it is kept alive, a
   request that cannot be read is answered with 400, 413, 431, 501 or 505 and closes the
   connection, and every response carries Date and Content-Length. */
@generic<S: send & sync & unborrowed>
async std.service::report serve(std.net::tcp_listener listener, std.service::options settings,
                                std.sync::receiver<std.service::stop> stop, arc S state,
                                router<S> routes, limits bounds)
    throws std.async::start_error, std.net::net_error, std.alloc::alloc_error {
    arc shared<S> context = new arc shared<S> {.state = move state, .routes = move routes,
                                               .bounds = bounds, .tls = o::none};
    async fn void adapter(arc shared<S> state, std.net::tcp_connection connection)
        throws std.error::fault {
        await serve_stream(move state, move connection);
    }
    return await std.service::serve_with(move listener, settings, move stop, move context, adapter);
}

/* R-SLIB-HTTP-0007: serve over TLS with the server configuration: HTTPS. */
@generic<S: send & sync & unborrowed>
async std.service::report serve_tls(std.net::tcp_listener listener, std.service::options settings,
                                    std.sync::receiver<std.service::stop> stop, arc S state,
                                    router<S> routes, limits bounds, arc std.tls::config tls)
    throws std.async::start_error, std.net::net_error, std.alloc::alloc_error {
    arc shared<S> context = new arc shared<S> {.state = move state, .routes = move routes,
                                               .bounds = bounds, .tls = o::some(move tls)};
    async fn void adapter(arc shared<S> state, std.net::tcp_connection connection)
        throws std.error::fault {
        await serve_tls_connection(move state, move connection);
    }
    return await std.service::serve_with(move listener, settings, move stop, move context, adapter);
}

/* R-SLIB-HTTP-0014: serves HTTP/1.1 as serve does, on every listener of std.service::serve_all at
   once: TCP, Unix-domain sockets and TLS, with the idle timeout, the signals and the health of
   that service. */
@generic<S: send & sync & unborrowed>
async std.service::report serve_all(array<std.service::listener> listeners, std.service::options settings,
                                    std.sync::receiver<std.service::stop> stop, std.service::health status,
                                    arc S state, router<S> routes, limits bounds)
    throws std.async::start_error, std.net::net_error, std.process::process_error, std.alloc::alloc_error {
    arc shared<S> context = new arc shared<S> {.state = move state, .routes = move routes,
                                               .bounds = bounds, .tls = o::none};
    async fn void adapter(arc shared<S> state, std.service::connection connection)
        throws std.error::fault {
        await serve_stream(move state, move connection);
    }
    return await std.service::serve_all(move listeners, settings, move stop, move status, move context, adapter);
}

/* ---- Applications: middleware, request contexts and isolated handlers (M42) ---- */

/* One named text of the notes of a request. */
protected struct note {
    std.string::string name;
    std.string::string value;
};

/* R-SLIB-HTTP-0015: named texts of one request, such as its identifier or the account that made
   it, that its middleware and handlers write and that outlive a panic of its flow: the panic hook
   of the application reads them. */
struct notes {
    protected arc std.sync::mutex<array<note>> entries;
};

protected notes notes_create() throws std.alloc::alloc_error {
    array<note> none = std.array::create::<note>();
    arc std.sync::mutex<array<note>> entries = new arc std.sync::mutex<array<note>>(std.sync::mutex_new(move none));
    return notes {.entries = move entries};
}

protected notes notes_share(const notes* this) {
    return notes {.entries = std.arc::clone(&this->entries)};
}

protected void put_note(array<note>* entries, str name, str value) throws std.alloc::alloc_error {
    for (usize index = 0usize; index < len(*entries); index += 1usize) {
        if (std.bytes::equal((*entries)[index].name, name) == true) {
            std.string::string old = core::replace(&(*entries)[index].value, std.string::from_str(value));
            drop old;
            return;
        }
    }
    append(entries, note {.name = std.string::from_str(name), .value = std.string::from_str(value)});
}

/* R-SLIB-HTTP-0015: sets the text of a name, replacing the text it had. */
void notes::set(const notes* this, str name, str value) throws std.alloc::alloc_error {
    std.sync::lock_result<array<note>> locked = std.sync::lock(&*this->entries);
    switch (move locked) {
    case variant std.sync::lock_result::locked(move guard): put_note(std.sync::mutex_guard_mut(&guard), name, value);
    case variant std.sync::lock_result::poisoned(move guard): put_note(std.sync::mutex_guard_mut(&guard), name, value);
    case variant std.sync::lock_result::would_deadlock: break;
    }
}

protected usize count_notes(const array<note>* entries) { return len(*entries); }

/* R-SLIB-HTTP-0015: the number of names. */
usize notes::count(const notes* this) {
    std.sync::lock_result<array<note>> locked = std.sync::lock(&*this->entries);
    switch (move locked) {
    case variant std.sync::lock_result::locked(move guard): return count_notes(std.sync::mutex_guard_ref(&guard));
    case variant std.sync::lock_result::poisoned(move guard): return count_notes(std.sync::mutex_guard_ref(&guard));
    case variant std.sync::lock_result::would_deadlock: break;
    }
    return 0usize;
}

/* A copy of the name (part 0) or text (part 1) at an index, or none past the end. */
protected o<std.string::string> note_part(const array<note>* entries, usize index, u32 part)
    throws std.alloc::alloc_error {
    if (index >= len(*entries)) { return o::none; }
    if (part == 0u32) { return o::some(std.string::from_str((*entries)[index].name)); }
    return o::some(std.string::from_str((*entries)[index].value));
}

protected o<std.string::string> notes_part(const notes* this, usize index, u32 part) throws std.alloc::alloc_error {
    std.sync::lock_result<array<note>> locked = std.sync::lock(&*this->entries);
    switch (move locked) {
    case variant std.sync::lock_result::locked(move guard): return note_part(std.sync::mutex_guard_ref(&guard), index, part);
    case variant std.sync::lock_result::poisoned(move guard): return note_part(std.sync::mutex_guard_ref(&guard), index, part);
    case variant std.sync::lock_result::would_deadlock: break;
    }
    return o::none;
}

/* R-SLIB-HTTP-0015: the name and the text at an index in the order the names were first set, or
   none past the last. */
o<std.string::string> notes::name_at(const notes* this, usize index) throws std.alloc::alloc_error {
    return notes_part(this, index, 0u32);
}

o<std.string::string> notes::value_at(const notes* this, usize index) throws std.alloc::alloc_error {
    return notes_part(this, index, 1u32);
}

/* R-SLIB-HTTP-0015: the text of a name, or none. */
o<std.string::string> notes::get(const notes* this, str name) throws std.alloc::alloc_error {
    usize total = this->count();
    for (usize index = 0usize; index < total; index += 1usize) {
        o<std.string::string> found = notes_part(this, index, 0u32);
        switch (move found) {
        case variant o::some(move named):
            if (std.bytes::equal(named, name) == true) { return notes_part(this, index, 1u32); }
            drop named;
        case variant o::none: break;
        }
    }
    return o::none;
}

/* R-SLIB-HTTP-0015: one request on its way through an application: the request, the context
   that the application made for it, its notes, the pattern of the route that received it, the
   response and whether a stage has answered. */
@generic<C: send & sync & unborrowed>
struct flow {
    request request;
    C context;
    notes notes;
    std.string::string route;
    response response;
    bool answered;
};

/* R-SLIB-HTTP-0015: the flow with the response, answered. */
@generic<C: send & sync & unborrowed>
flow<C> flow<C>::with(flow<C> this, response value) {
    response old = core::replace(&this.response, move value);
    drop old;
    this.answered = true;
    return move this;
}

/* R-SLIB-HTTP-0015: a middleware of an application: the requests whose path lies under its
   prefix pass its before hook on the way to the route and its after hook on the way back. */
@generic<S: send & sync & unborrowed, C: send & sync & unborrowed>
protected struct stage {
    std.string::string prefix;
    o<async fn(arc S, flow<C>) -> flow<C> throws(std.error::fault)> before;
    o<async fn(arc S, flow<C>) -> flow<C> throws(std.error::fault)> after;
};

/* R-SLIB-HTTP-0015: a route of an application. */
@generic<S: send & sync & unborrowed, C: send & sync & unborrowed>
protected struct endpoint {
    method method;
    std.string::string pattern;
    async fn(arc S, flow<C>) -> flow<C> throws(std.error::fault) handler;
};

/* R-SLIB-HTTP-0017: cross-origin resource sharing of an application: the origins it allows
   (every origin when the list is empty), the methods and fields a preflight request may ask for
   (when none, the ones it asks for), whether credentials are allowed and how long a client may
   keep the answer of a preflight request. */
struct cors_policy {
    array<std.string::string> origins = [];
    o<std.string::string> methods = o::none;
    o<std.string::string> headers = o::none;
    bool credentials = false;
    u32 max_age = 600u32;
};

/* R-SLIB-HTTP-0015: an application with shared state S and a context C for each request. */
@generic<S: send & sync & unborrowed, C: send & sync & unborrowed>
struct app {
    protected fn(const S*, const request*) -> C context;
    protected array<endpoint<S, C>> endpoints;
    protected array<stage<S, C>> stages;
    protected o<cors_policy> cross_origin;
    protected bool redirect_slash;
    protected bool method_status;
    protected o<fn(const S*, method, str, const notes*, const std.thread::panic_report*) -> void> panic_hook;
};

/* R-SLIB-HTTP-0015: an application whose requests get the context that context makes from the
   state and the request. */
@generic<S: send & sync & unborrowed, C: send & sync & unborrowed>
app<S, C> app<S, C>::create(fn(const S*, const request*) -> C context) {
    return app<S, C> {.context = context, .endpoints = std.array::create::<endpoint<S, C>>(),
                      .stages = std.array::create::<stage<S, C>>(), .cross_origin = o::none,
                      .redirect_slash = false, .method_status = true, .panic_hook = o::none};
}

protected void check_pattern(str pattern) throws http_error {
    const u8[] bytes = pattern;
    throw (len(bytes) == 0usize || bytes[0usize] != 47u8) failure(error_code::invalid_url);
}

/* R-SLIB-HTTP-0015: adds a route; among the routes whose pattern and method match a request
   the most specific answers it, whatever the order of the calls. */
@generic<S: send & sync & unborrowed, C: send & sync & unborrowed>
void app<S, C>::route(app<S, C>* this, method value, str pattern,
                      async fn(arc S, flow<C>) -> flow<C> throws(std.error::fault) handler)
    throws http_error, std.alloc::alloc_error {
    check_pattern(pattern);
    append(&this->endpoints, endpoint<S, C> {.method = value, .pattern = std.string::from_str(pattern),
                                             .handler = handler});
}

/* R-SLIB-HTTP-0015: adds a middleware with a before hook only. */
@generic<S: send & sync & unborrowed, C: send & sync & unborrowed>
void app<S, C>::before(app<S, C>* this, str prefix,
                       async fn(arc S, flow<C>) -> flow<C> throws(std.error::fault) hook)
    throws http_error, std.alloc::alloc_error {
    check_pattern(prefix);
    append(&this->stages, stage<S, C> {.prefix = std.string::from_str(prefix), .before = o::some(hook),
                                       .after = o::none});
}

/* R-SLIB-HTTP-0015: adds a middleware with an after hook only. */
@generic<S: send & sync & unborrowed, C: send & sync & unborrowed>
void app<S, C>::after(app<S, C>* this, str prefix,
                      async fn(arc S, flow<C>) -> flow<C> throws(std.error::fault) hook)
    throws http_error, std.alloc::alloc_error {
    check_pattern(prefix);
    append(&this->stages, stage<S, C> {.prefix = std.string::from_str(prefix), .before = o::none,
                                       .after = o::some(hook)});
}

/* R-SLIB-HTTP-0015: adds a middleware with both hooks. */
@generic<S: send & sync & unborrowed, C: send & sync & unborrowed>
void app<S, C>::around(app<S, C>* this, str prefix,
                       async fn(arc S, flow<C>) -> flow<C> throws(std.error::fault) before,
                       async fn(arc S, flow<C>) -> flow<C> throws(std.error::fault) after)
    throws http_error, std.alloc::alloc_error {
    check_pattern(prefix);
    append(&this->stages, stage<S, C> {.prefix = std.string::from_str(prefix), .before = o::some(before),
                                       .after = o::some(after)});
}

/* R-SLIB-HTTP-0017: answers the preflight requests of the policy and marks the responses to the
   requests of its origins. */
@generic<S: send & sync & unborrowed, C: send & sync & unborrowed>
void app<S, C>::cors(app<S, C>* this, cors_policy policy) {
    o<cors_policy> old = core::replace(&this->cross_origin, o::some(move policy));
    drop old;
}

/* R-SLIB-HTTP-0018: a request whose path matches no route, but would with or without its final
   '/', is redirected there: 301 for GET and HEAD, 308 for the other methods. */
@generic<S: send & sync & unborrowed, C: send & sync & unborrowed>
void app<S, C>::redirect_trailing_slash(app<S, C>* this, bool enabled) {
    this->redirect_slash = enabled;
}

/* R-SLIB-HTTP-0018: whether a path that only routes of other methods match is answered with 405
   and Allow (the default) or with 404. */
@generic<S: send & sync & unborrowed, C: send & sync & unborrowed>
void app<S, C>::method_not_allowed(app<S, C>* this, bool enabled) {
    this->method_status = enabled;
}

/* R-SLIB-HTTP-0015: the hook that receives the report of a panic of the middleware or handler of
   a request, with the state, the method, the target and the notes of the request, instead of the
   standard error line. */
@generic<S: send & sync & unborrowed, C: send & sync & unborrowed>
void app<S, C>::on_panic(app<S, C>* this, fn(const S*, method, str, const notes*, const std.thread::panic_report*) -> void hook) {
    o<fn(const S*, method, str, const notes*, const std.thread::panic_report*) -> void> old = core::replace(&this->panic_hook, o::some(hook));
    old as void;
}

/* The kind of one segment of a pattern: 2 literal, 1 a parameter, 0 the final `*`. */
protected u32 segment_kind(const u8[] segment) {
    if (len(segment) == 1usize && segment[0usize] == 42u8) { return 0u32; }
    if (len(segment) >= 2usize && segment[0usize] == 123u8 && segment[len(segment) - 1usize] == 125u8) {
        return 1u32;
    }
    return 2u32;
}

/* Whether pattern left is more specific than right: at the first segment where they differ in
   kind, a literal beats a parameter and a parameter beats `*`. */
protected bool more_specific(str left, str right) {
    const u8[] a = left;
    const u8[] b = right;
    usize i = 0usize;
    usize j = 0usize;
    while (i < len(a) && j < len(b)) {
        usize end_a = segment_end(a, i + 1usize);
        usize end_b = segment_end(b, j + 1usize);
        u32 kind_a = segment_kind(a[i + 1usize..end_a]);
        u32 kind_b = segment_kind(b[j + 1usize..end_b]);
        if (kind_a != kind_b) { return kind_a > kind_b; }
        i = end_a;
        j = end_b;
    }
    return false;
}

/* The index of the most specific route whose pattern and method match the request, with its
   captured parameters in captured; allowed collects the methods of the routes whose pattern
   alone matches. */
@generic<S: send & sync & unborrowed, C: send & sync & unborrowed>
protected o<usize> find_endpoint(const app<S, C>* routes, const request* incoming,
                                 array<param>* captured, std.string::string* allowed)
    throws std.alloc::alloc_error {
    method sent = incoming->method;
    o<usize> best = o::none;
    for (usize index = 0usize; index < len(routes->endpoints); index += 1usize) {
        array<param> found = std.array::create::<param>();
        if (match_path(routes->endpoints[index].pattern, incoming->path(), &found) == false) {
            continue;
        }
        method wanted = routes->endpoints[index].method;
        if (wanted == sent || (sent == method::head && wanted == method::get)) {
            bool better = true;
            switch (best) {
            case variant o::some(previous):
                better = more_specific(routes->endpoints[index].pattern,
                                       routes->endpoints[*previous].pattern);
            case variant o::none: break;
            }
            if (better == true) {
                array<param> old = core::replace(captured, move found);
                drop old;
                best = o::some(index);
            } else {
                drop found;
            }
            continue;
        }
        if (std.text::contains(*allowed, method_name(wanted)) == false) {
            if (std.string::len(allowed) != 0usize) { std.string::append_str(allowed, ", "); }
            std.string::append_str(allowed, method_name(wanted));
        }
    }
    return best;
}

/* The path of the request with its final '/' removed or added, when a route of its method matches
   that path. */
@generic<S: send & sync & unborrowed, C: send & sync & unborrowed>
protected o<std.string::string> slash_target(const app<S, C>* routes, const request* incoming)
    throws std.alloc::alloc_error {
    str path = incoming->path();
    const u8[] bytes = path;
    std.string::string other = std.string::create();
    if (len(bytes) > 1usize && bytes[len(bytes) - 1usize] == 47u8) {
        std.string::append_str(&other, piece(path, 0usize, len(bytes) - 1usize));
    } else {
        std.string::append_str(&other, path);
        std.string::append_str(&other, "/");
    }
    request probe = request::create(incoming->method, other);
    array<param> found = std.array::create::<param>();
    std.string::string allowed = std.string::create();
    o<usize> chosen = find_endpoint(routes, &probe, &found, &allowed);
    drop found;
    drop allowed;
    drop probe;
    switch (chosen) {
    case variant o::some(index):
        index as void;
        switch (incoming->query()) {
        case variant o::some(text):
            std.string::append_str(&other, "?");
            std.string::append_str(&other, *text);
        case variant o::none: break;
        }
        return o::some(move other);
    case variant o::none: break;
    }
    drop other;
    return o::none;
}

/* Whether the path lies under the prefix: equal to it or below it at a '/'. */
protected bool under_prefix(str prefix, str path) {
    const u8[] p = prefix;
    const u8[] q = path;
    if (len(p) == 1usize) { return true; }
    if (len(q) < len(p) || std.bytes::equal(q[0usize..len(p)], p) == false) { return false; }
    return len(q) == len(p) || q[len(p)] == 47u8 || p[len(p) - 1usize] == 47u8;
}

/* The route of a request that no stage answered: the index of the most specific route, whose
   parameters it then holds, or none and in miss the redirect of the trailing slash, 405 with
   Allow or 404. */
@generic<S: send & sync & unborrowed, C: send & sync & unborrowed>
protected o<usize> choose_endpoint(const app<S, C>* routes, request* incoming, response* miss)
    throws std.alloc::alloc_error {
    array<param> captured = std.array::create::<param>();
    std.string::string allowed = std.string::create();
    o<usize> chosen = find_endpoint(routes, incoming, &captured, &allowed);
    switch (chosen) {
    case variant o::some(index):
        array<param> old = core::replace(&incoming->params, move captured);
        drop old;
        drop allowed;
        return o::some(*index);
    case variant o::none: break;
    }
    drop captured;
    if (routes->redirect_slash == true) {
        o<std.string::string> moved = slash_target(routes, incoming);
        switch (move moved) {
        case variant o::some(move location):
            u16 status = 308u16;
            if (incoming->method == method::get || incoming->method == method::head) {
                status = 301u16;
            }
            response redirect = response::text(status, reason(status));
            try {
                redirect.headers.add("Location", location);
            } catch (http_error rejected) {
                // A path of a request is a valid field value.
                rejected as void;
            }
            response old = core::replace(miss, move redirect);
            drop old;
            drop allowed;
            return o::none;
        case variant o::none: break;
        }
    }
    if (std.string::len(&allowed) != 0usize && routes->method_status == true) {
        response refused = response::text(405u16, reason(405u16));
        try {
            refused.headers.add("Allow", allowed);
        } catch (http_error rejected) {
            // Method names are valid field values.
            rejected as void;
        }
        response old = core::replace(miss, move refused);
        drop old;
        drop allowed;
        return o::none;
    }
    drop allowed;
    response old = core::replace(miss, response::text(404u16, reason(404u16)));
    drop old;
    return o::none;
}

/* The response of the application to a request: its before hooks in order, then the route, then
   the after hooks of the middleware it passed in reverse order. A hook or handler that throws is
   answered with 500. */
@generic<S: send & sync & unborrowed, C: send & sync & unborrowed>
@scoped
protected async response run_flow(const app<S, C>* routes, arc S state, request incoming, notes noted) {
    try {
        auto make = routes->context;
        C context = make(&*state, &incoming);
        flow<C> current = flow<C> {.request = move incoming, .context = move context, .notes = move noted,
                                   .route = std.string::create(), .response = response::create(200u16),
                                   .answered = false};
        array<bool> entered = std.array::create::<bool>();
        for (usize index = 0usize; index < len(routes->stages); index += 1usize) {
            bool matches = under_prefix(routes->stages[index].prefix, current.request.path());
            append(&entered, matches);
            if (matches == false) { continue; }
            switch (routes->stages[index].before) {
            case variant o::some(hook):
                auto chosen = *hook;
                flow<C> next = await chosen(std.arc::clone(&state), move current);
                current = move next;
            case variant o::none: break;
            }
            if (current.answered == true) { break; }
        }
        if (current.answered == false) {
            response miss = response::create(404u16);
            o<usize> chosen = choose_endpoint(routes, &current.request, &miss);
            switch (chosen) {
            case variant o::some(index):
                drop miss;
                current.route.append(routes->endpoints[*index].pattern);
                auto handler = routes->endpoints[*index].handler;
                flow<C> handled = await handler(std.arc::clone(&state), move current);
                current = move handled;
            case variant o::none:
                flow<C> missed = (move current).with(move miss);
                current = move missed;
            }
        }
        usize back = len(entered);
        while (back > 0usize) {
            back -= 1usize;
            if (entered[back] == false) { continue; }
            switch (routes->stages[back].after) {
            case variant o::some(hook):
                auto chosen = *hook;
                flow<C> next = await chosen(std.arc::clone(&state), move current);
                current = move next;
            case variant o::none: break;
            }
        }
        return match (move current) { case { .response = move result }: move result; };
    } catch (std.error::fault rejected) {
        rejected as void;
    }
    return response::create(500u16);
}

/* Whether the origin is one the policy allows. */
protected bool origin_allowed(const cors_policy* policy, str origin) {
    if (len(policy->origins) == 0usize) { return true; }
    for (usize index = 0usize; index < len(policy->origins); index += 1usize) {
        if (std.bytes::equal(policy->origins[index], origin) == true) { return true; }
    }
    return false;
}

/* R-SLIB-HTTP-0017: the fields of a response to a request from an allowed origin. */
protected void mark_origin(const cors_policy* policy, str origin, response* result)
    throws std.alloc::alloc_error {
    try {
        if (len(policy->origins) == 0usize && policy->credentials == false) {
            result->headers.set("Access-Control-Allow-Origin", "*");
        } else {
            result->headers.set("Access-Control-Allow-Origin", origin);
            result->headers.add("Vary", "Origin");
        }
        if (policy->credentials == true) {
            result->headers.set("Access-Control-Allow-Credentials", "true");
        }
    } catch (http_error rejected) {
        // The origin comes from a valid field value.
        rejected as void;
    }
}

/* R-SLIB-HTTP-0017: the answer to a preflight request: OPTIONS with Origin and
   Access-Control-Request-Method, 204 with the allowed methods and fields for an allowed origin
   and 403 for another one. */
protected o<response> preflight(const (o<cors_policy>)* configured, const request* incoming)
    throws std.alloc::alloc_error {
    if (incoming->method != method::options) { return o::none; }
    switch (*configured) {
    case variant o::some(policy):
        o<str> origin = incoming->headers.get("Origin");
        switch (origin) {
        case variant o::some(from):
            o<str> asked = incoming->headers.get("Access-Control-Request-Method");
            switch (asked) {
            case variant o::some(wanted):
                if (origin_allowed(&*policy, *from) == false) {
                    return o::some(response::text(403u16, reason(403u16)));
                }
                response answer = response::create(204u16);
                mark_origin(&*policy, *from, &answer);
                try {
                    switch (policy->methods) {
                    case variant o::some(listed): answer.headers.set("Access-Control-Allow-Methods", *listed);
                    case variant o::none: answer.headers.set("Access-Control-Allow-Methods", *wanted);
                    }
                    switch (policy->headers) {
                    case variant o::some(listed): answer.headers.set("Access-Control-Allow-Headers", *listed);
                    case variant o::none:
                        switch (incoming->headers.get("Access-Control-Request-Headers")) {
                        case variant o::some(fields): answer.headers.set("Access-Control-Allow-Headers", *fields);
                        case variant o::none: break;
                        }
                    }
                    u32 age = policy->max_age;
                    std.string::string seconds = f"{age}";
                    answer.headers.set("Access-Control-Max-Age", seconds);
                } catch (http_error rejected) {
                    // The values come from the policy and from valid field values.
                    rejected as void;
                }
                return o::some(move answer);
            case variant o::none: return o::none;
            }
        case variant o::none: return o::none;
        }
    case variant o::none: return o::none;
    }
    return o::none;
}

/* R-SLIB-HTTP-0015: the response of the application to a request: the preflight answer of its
   CORS policy, or the flow of the request, run as a task of its own whose panic is answered with
   500 and reported to the panic hook of the application or as a line on standard error; then the
   CORS fields for an allowed origin. */
@generic<S: send & sync & unborrowed, C: send & sync & unborrowed>
@scoped
async response app<S, C>::dispatch(const app<S, C>* this, arc S state, request incoming)
    throws std.error::fault {
    std.string::string origin = std.string::create();
    bool has_origin = false;
    switch (incoming.headers.get("Origin")) {
    case variant o::some(from):
        std.string::append_str(&origin, *from);
        has_origin = true;
    case variant o::none: break;
    }
    o<response> early = preflight(&this->cross_origin, &incoming);
    switch (move early) {
    case variant o::some(move answer):
        drop incoming;
        drop state;
        drop origin;
        return move answer;
    case variant o::none: break;
    }
    method sent = incoming.method;
    std.string::string target = std.string::from_str(incoming.target);
    arc S kept = std.arc::clone(&state);
    notes noted = notes_create();
    notes shared_notes = notes_share(&noted);
    o<response> produced = o::none;
    o<std.thread::panic_report> crash = o::none;
    task_scope(1) chain {
        auto flowing = run_flow(this, move state, move incoming, move shared_notes);
        std.thread::join_result<response> joined = await std.async::join(move flowing);
        switch (move joined) {
        case variant std.thread::join_result::returned(move value):
            o<response> old = core::replace(&produced, o::some(move value));
            drop old;
        case variant std.thread::join_result::panicked(move report):
            o<std.thread::panic_report> old = core::replace(&crash, o::some(move report));
            drop old;
        }
    }
    response result = response::create(500u16);
    switch (move produced) {
    case variant o::some(move value):
        response old = core::replace(&result, move value);
        drop old;
    case variant o::none: break;
    }
    switch (move crash) {
    case variant o::some(move report):
        response old = core::replace(&result, response::text(500u16, reason(500u16)));
        drop old;
        switch (this->panic_hook) {
        case variant o::some(hook):
            auto chosen = *hook;
            chosen(&*kept, sent, target, &noted, &report);
        case variant o::none:
            constexpr str category = std.thread::panic_category(&report);
            str text = std.thread::panic_text(&report);
            str verb = method_name(sent);
            str where = target;
            std.string::string line = f"R panic: {category} in {verb} {where}";
            if (len(text) != 0usize) {
                std.string::append_str(&line, ": ");
                std.string::append_str(&line, text);
            }
            await std.console::eprintln(move line);
        }
        drop report;
    case variant o::none: break;
    }
    drop target;
    drop kept;
    drop noted;
    if (has_origin == true) {
        switch (this->cross_origin) {
        case variant o::some(policy):
            if (origin_allowed(&*policy, origin) == true) {
                mark_origin(&*policy, origin, &result);
            }
        case variant o::none: break;
        }
    }
    drop origin;
    return move result;
}

/* What a server of an application keeps for all its connections. */
@generic<S: send & sync & unborrowed, C: send & sync & unborrowed>
protected struct app_shared {
    arc S state;
    app<S, C> routes;
    limits bounds;
};

/* Serves one request of a connection with an application and returns whether the connection
   stays open. */
@generic<S: send & sync & unborrowed, C: send & sync & unborrowed, T: std.stream::Stream & unborrowed>
@scoped
protected async bool app_exchange(std.bufio::reader<T>* input, arc app_shared<S, C> context)
    throws std.error::fault {
    limits bounds = context->bounds;
    o<request> received = o::none;
    try {
        std.time::instant now = std.time::monotonic_now();
        std.time::instant limit = now.add(bounds.request_timeout);
        deadline (limit) {
            task_scope(1) io {
                o<request> read = await read_request(input, &bounds);
                switch (move read) {
                case variant o::some(move value): received = o::some(move value);
                case variant o::none: break;
                }
            }
        }
    } catch (http_error rejected) {
        response refusal = error_response(rejected.code);
        task_scope(1) io {
            await write_response(&input->source, &refusal, false, true);
            await linger(input);
        }
        return false;
    } catch (std.error::fault rejected) {
        // The client ended the connection or did not send a request in time.
        rejected as void;
        return false;
    }
    switch (move received) {
    case variant o::none: return false;
    case variant o::some(move incoming):
        bool wanted = keeps_alive(&incoming);
        bool head = incoming.method == method::head;
        task_scope(1) io {
            response result = await context->routes.dispatch(std.arc::clone(&context->state), move incoming);
            add_date(&result.headers);
            return await write_plain(input, move result, head, wanted);
        }
    }
    return false;
}

/* Serves the requests of one connection with an application until it closes. */
@generic<S: send & sync & unborrowed, C: send & sync & unborrowed, T: std.stream::Stream & unborrowed>
protected async void serve_app_stream(arc app_shared<S, C> context, T transport) throws std.error::fault {
    std.bufio::reader<T> input =
        std.bufio::reader<T>::create(move transport, context->bounds.max_head);
    bool open = true;
    while (open == true) {
        task_scope(1) io {
            bool more = await app_exchange(&input, std.arc::clone(&context));
            if (more == false) { open = false; }
        }
    }
    drop input;
    drop context;
}

/* R-SLIB-HTTP-0015: serves an application on every listener of std.service::serve_all, as
   serve_all does a router: TCP, Unix-domain sockets and TLS, with the idle timeout, the signals
   and the health of that service. */
@generic<S: send & sync & unborrowed, C: send & sync & unborrowed>
async std.service::report serve_app(array<std.service::listener> listeners, std.service::options settings,
                                    std.sync::receiver<std.service::stop> stop, std.service::health status,
                                    arc S state, app<S, C> routes, limits bounds)
    throws std.async::start_error, std.net::net_error, std.process::process_error, std.alloc::alloc_error {
    arc app_shared<S, C> context = new arc app_shared<S, C> {.state = move state, .routes = move routes,
                                                             .bounds = bounds};
    async fn void adapter(arc app_shared<S, C> state, std.service::connection connection)
        throws std.error::fault {
        await serve_app_stream(move state, move connection);
    }
    return await std.service::serve_all(move listeners, settings, move stop, move status, move context, adapter);
}

/* R-SLIB-HTTP-0008: the settings of a client. */
struct client_options {
    u32 max_redirects = 5u32;
    u32 max_idle = 4u32;
    limits bounds = {};
    bool accept_gzip = true;
};

/* R-SLIB-HTTP-0008: an HTTP/1.1 client with a pool of kept-alive connections, each slot of the
   pool with the origin it was opened for. HTTPS needs the TLS client configuration that
   verifies the servers. */
struct client {
    protected client_options settings;
    protected o<arc std.tls::config> tls;
    protected array<std.string::string> origins;
    protected array<o<std.bufio::reader<own dyn(std.stream::Stream)*>>> slots;
};

client client::create(client_options settings) {
    return client {.settings = settings, .tls = o::none,
                   .origins = std.array::create::<std.string::string>(),
                   .slots = std.array::create::<o<std.bufio::reader<own dyn(std.stream::Stream)*>>>()};
}

client client::with_tls(client_options settings, arc std.tls::config tls) {
    return client {.settings = settings, .tls = o::some(move tls),
                   .origins = std.array::create::<std.string::string>(),
                   .slots = std.array::create::<o<std.bufio::reader<own dyn(std.stream::Stream)*>>>()};
}

protected bool occupied(const (o<std.bufio::reader<own dyn(std.stream::Stream)*>>)* slot) {
    switch (*slot) {
    case variant o::some(value):
        value as void;
        return true;
    case variant o::none: return false;
    }
}

/* The number of idle connections in the pool. */
usize client::idle_count(const client* this) {
    usize count = 0usize;
    for (usize index = 0usize; index < len(this->slots); index += 1usize) {
        if (occupied(&this->slots[index]) == true) { count += 1usize; }
    }
    return count;
}

/* The origin of a URL: scheme, host and effective port. */
protected std.string::string origin_of(const std.url::url* address) throws std.alloc::alloc_error {
    u16 port = 0u16;
    switch (address->effective_port()) {
    case variant o::some(value): port = *value;
    case variant o::none: break;
    }
    str scheme = address->scheme();
    str host = address->host();
    return f"{scheme}://{host}:{port}";
}

/* Takes an idle connection to the origin out of the pool. */
protected o<std.bufio::reader<own dyn(std.stream::Stream)*>> take_idle(client* owner, str origin) {
    for (usize index = 0usize; index < len(owner->slots); index += 1usize) {
        if (occupied(&owner->slots[index]) == true &&
            std.bytes::equal(owner->origins[index], origin) == true) {
            return core::replace(&owner->slots[index], o::none);
        }
    }
    return o::none;
}

/* Keeps a connection to the origin for a later request while the pool has room. */
protected void put_idle(client* owner, str origin, std.bufio::reader<own dyn(std.stream::Stream)*> kept)
    throws std.alloc::alloc_error {
    if (owner->idle_count() >= (owner->settings.max_idle as usize)) {
        drop kept;
        return;
    }
    for (usize index = 0usize; index < len(owner->slots); index += 1usize) {
        if (occupied(&owner->slots[index]) == false) {
            std.string::string old = core::replace(&owner->origins[index], std.string::from_str(origin));
            drop old;
            o<std.bufio::reader<own dyn(std.stream::Stream)*>> empty =
                core::replace(&owner->slots[index], o::some(move kept));
            drop empty;
            return;
        }
    }
    append(&owner->origins, std.string::from_str(origin));
    o<std.bufio::reader<own dyn(std.stream::Stream)*>> slot = o::some(move kept);
    append(&owner->slots, move slot);
}

/* Opens a connection, a buffered reader over TCP or TLS over TCP, to the host and port of
   the URL. */
@scoped
protected async std.bufio::reader<own dyn(std.stream::Stream)*>
open_connection(const client* owner, const std.url::url* address,
                                           u16 port)
    throws http_error, std.tls::tls_error, std.error::fault {
    bool secure = std.text::equal_ignore_ascii_case(address->scheme(), "https");
    bool configured = false;
    switch (owner->tls) {
    case variant o::some(settings):
        settings as void;
        configured = true;
    case variant o::none: break;
    }
    throw (secure == true && configured == false) failure(error_code::unsupported_scheme);
    task_scope(1) io {
        std.net::tcp_stream tcp = await std.net::tcp_connect_name(address->host(), port);
        switch (owner->tls) {
        case variant o::some(settings):
            if (secure == true) {
                std.tls::stream<std.net::tcp_stream> secured =
                    await std.tls::connect(move tcp, &**settings, address->host());
                own dyn(std.stream::Stream)* tunnel =
                    new std.tls::stream<std.net::tcp_stream>(move secured);
                return std.bufio::reader<own dyn(std.stream::Stream)*>::create(
                    move tunnel, owner->settings.bounds.max_head);
            }
        case variant o::none: break;
        }
        own dyn(std.stream::Stream)* plain = new std.net::tcp_stream(move tcp);
        return std.bufio::reader<own dyn(std.stream::Stream)*>::create(
            move plain, owner->settings.bounds.max_head);
    }
}

/* Whether a connection may carry another request after this response. */
protected bool reusable(const response* result, method sent) {
    if (result->headers.has_token("Connection", "close") == true) { return false; }
    if (sent == method::head || result->status == 204u16 || result->status == 304u16) {
        return true;
    }
    try {
        body_state state = framing_of(&result->headers, framing_close);
        return state.framing != framing_close;
    } catch (http_error rejected) {
        rejected as void;
    }
    return false;
}

/* Writes the request on the connection and reads its response. */
@scoped
protected async response round_trip(std.bufio::reader<own dyn(std.stream::Stream)*>* channel, const request* pending, const limits* bounds)
    throws http_error, std.error::fault {
    task_scope(1) io {
        await write_request(&channel->source, pending, "");
        return await read_response(channel, pending->method, bounds);
    }
}

/* Exchanges the request on the connection, then keeps the connection in the pool when it may
   carry another request. */
@scoped
protected async response client::use_connection(client* this, std.bufio::reader<own dyn(std.stream::Stream)*> channel, str origin,
                                                const request* pending, const limits* bounds)
    throws http_error, std.error::fault {
    task_scope(1) io {
        response result = await round_trip(&channel, pending, bounds);
        if (reusable(&result, pending->method) == true) {
            put_idle(this, origin, move channel);
        } else {
            drop channel;
        }
        return move result;
    }
}

/* One request to the URL on an idle connection of its origin or on a new one; a failure on an
   idle connection before its response, which the server may have closed, retries once on a new
   connection. */
@scoped
protected async response client::exchange_once(client* this, const std.url::url* address,
                                               request* pending)
    throws http_error, std.tls::tls_error, std.error::fault {
    str scheme = address->scheme();
    throw (std.text::equal_ignore_ascii_case(scheme, "http") == false &&
           std.text::equal_ignore_ascii_case(scheme, "https") == false)
        failure(error_code::unsupported_scheme);
    const u8[] host_bytes = address->host();
    throw (address->has_authority() == false || len(host_bytes) == 0usize)
        failure(error_code::invalid_url);
    u16 port = 0u16;
    switch (address->effective_port()) {
    case variant o::some(value): port = *value;
    case variant o::none: throw failure(error_code::invalid_url);
    }
    std.string::string origin = origin_of(address);
    std.string::string target = address->target();
    std.string::string old_target = core::replace(&pending->target, move target);
    drop old_target;
    std.string::string host = address->authority_text();
    pending->headers.set("Host", host);
    if (this->settings.accept_gzip == true && pending->headers.contains("Accept-Encoding") == false) {
        pending->headers.add("Accept-Encoding", "gzip");
    }
    limits bounds = this->settings.bounds;
    o<std.bufio::reader<own dyn(std.stream::Stream)*>> reused = take_idle(this, origin);
    switch (move reused) {
    case variant o::some(move channel):
        try {
            task_scope(1) io {
                return await this->use_connection(move channel, origin, pending, &bounds);
            }
        } catch (http_error rejected) {
            throw (rejected.code != error_code::unexpected_end) rejected;
        } catch (std.error::fault rejected) {
            rejected as void;
        }
    case variant o::none: break;
    }
    task_scope(1) io {
        std.bufio::reader<own dyn(std.stream::Stream)*> channel = await open_connection(this, address, port);
        return await this->use_connection(move channel, origin, pending, &bounds);
    }
}

protected std.url::url url_of(str address) throws http_error, std.alloc::alloc_error {
    try {
        return std.url::parse(address);
    } catch (std.url::url_error rejected) {
        rejected as void;
    }
    throw failure(error_code::invalid_url);
}

/* Decodes a gzip body under the limit and removes the fields that described the encoding. */
protected void decode_body(response* result, usize limit) throws http_error, std.alloc::alloc_error {
    if (result->headers.has_token("Content-Encoding", "gzip") == false) { return; }
    try {
        bytes decoded = std.deflate::inflate(result->body.as_slice(), std.deflate::format::rfc1952, limit);
        bytes old = core::replace(&result->body, move decoded);
        drop old;
    } catch (std.deflate::error rejected) {
        throw (rejected.code == std.deflate::error_code::output_limit)
            failure(error_code::body_too_large);
        throw failure(error_code::invalid_encoding);
    }
    usize removed = result->headers.remove("Content-Encoding");
    removed as void;
    usize length = result->headers.remove("Content-Length");
    length as void;
}

/* R-SLIB-HTTP-0008: sends the request to the absolute http or https URL and returns the final
   response: redirects 301, 302, 303, 307 and 308 with a Location are followed up to
   max_redirects (303, and 301 or 302 after POST, become GET without a body), and a gzip body is
   decoded when the client asks for it. */
@scoped
async response client::send(client* this, request message, str address)
    throws http_error, std.tls::tls_error, std.error::fault {
    std.url::url current = url_of(address);
    request pending = move message;
    u32 hops = 0u32;
    while (true) {
        response result = response::create(0u16);
        task_scope(1) io {
            response received = await this->exchange_once(&current, &pending);
            response old = core::replace(&result, move received);
            drop old;
        }
        u16 status = result.status;
        bool redirect = status == 301u16 || status == 302u16 || status == 303u16 ||
                        status == 307u16 || status == 308u16;
        o<std.url::url> next = o::none;
        if (redirect == true) {
            switch (result.headers.get("Location")) {
            case variant o::some(location):
                try {
                    next = o::some(current.resolve(*location));
                } catch (std.url::url_error rejected) {
                    rejected as void;
                    throw failure(error_code::invalid_url);
                }
            case variant o::none: break;
            }
        }
        switch (move next) {
        case variant o::none:
            decode_body(&result, this->settings.bounds.max_body);
            return move result;
        case variant o::some(move target):
            drop result;
            throw (hops == this->settings.max_redirects) failure(error_code::too_many_redirects);
            hops += 1u32;
            if (status == 303u16 || ((status == 301u16 || status == 302u16) &&
                                     pending.method == method::post)) {
                pending.method = method::get;
                bytes empty = {};
                bytes old_body = core::replace(&pending.body, move empty);
                drop old_body;
                usize removed = pending.headers.remove("Content-Type");
                removed as void;
            }
            std.url::url old_url = core::replace(&current, move target);
            drop old_url;
        }
    }
    throw failure(error_code::too_many_redirects);
}

/* R-SLIB-HTTP-0008: sends GET to the URL. */
@scoped
async response client::get(client* this, str address)
    throws http_error, std.tls::tls_error, std.error::fault {
    request message = request::create(method::get, "/");
    task_scope(1) io { return await this->send(move message, address); }
}

/* R-SLIB-HTTP-0011: the answer to an upgrade request: its response and, when the status is 101,
   the connection. */
struct handshake { response answer; o<upgraded> connection; };

/* R-SLIB-HTTP-0011: sends the request to the absolute http or https URL on a new connection
   outside the pool and reads the head of the response; a 101 response hands over the
   connection, any other response is read whole and closes it. Redirects are not followed. */
@scoped
async handshake client::upgrade(client* this, request message, str address)
    throws http_error, std.tls::tls_error, std.error::fault {
    std.url::url current = url_of(address);
    str scheme = current.scheme();
    throw (std.text::equal_ignore_ascii_case(scheme, "http") == false &&
           std.text::equal_ignore_ascii_case(scheme, "https") == false)
        failure(error_code::unsupported_scheme);
    const u8[] host_bytes = current.host();
    throw (current.has_authority() == false || len(host_bytes) == 0usize)
        failure(error_code::invalid_url);
    u16 port = 0u16;
    switch (current.effective_port()) {
    case variant o::some(value): port = *value;
    case variant o::none: throw failure(error_code::invalid_url);
    }
    request pending = move message;
    std.string::string target = current.target();
    std.string::string old_target = core::replace(&pending.target, move target);
    drop old_target;
    std.string::string host = current.authority_text();
    pending.headers.set("Host", host);
    limits bounds = this->settings.bounds;
    o<std.bufio::reader<own dyn(std.stream::Stream)*>> opened = o::none;
    task_scope(1) connect {
        std.bufio::reader<own dyn(std.stream::Stream)*> made =
            await open_connection(this, &current, port);
        o<std.bufio::reader<own dyn(std.stream::Stream)*>> none = core::replace(&opened, o::some(move made));
        drop none;
    }
    std.bufio::reader<own dyn(std.stream::Stream)*> channel = match (move opened) {
        case variant o::some(move made): move made;
        case variant o::none: throw failure(error_code::unexpected_end);
    };
    response answer = response::create(0u16);
    task_scope(1) io {
        await write_request(&channel.source, &pending, "");
        response received = await read_response_until(&channel, pending.method, &bounds, true);
        response old = core::replace(&answer, move received);
        drop old;
    }
    if (answer.status != 101u16) {
        drop channel;
        return handshake {.answer = move answer, .connection = o::none};
    }
    bytes rest = {};
    own dyn(std.stream::Stream)* transport = (move channel).into_source(&rest);
    return handshake {.answer = move answer,
                      .connection = o::some(upgraded {.transport = move transport,
                                                      .buffered = move rest})};
}

/* R-SLIB-HTTP-0012: a response whose body is read as it arrives, on a connection of its own:
   the status and header fields of the response, then the pieces of its body. */
struct streamed {
    response head;
    protected std.bufio::reader<own dyn(std.stream::Stream)*> channel;
    protected body_state state;
};

/* R-SLIB-HTTP-0012: sends the request to the absolute http or https URL on a new connection
   outside the pool and reads the head of the response, whose body next then reads as it
   arrives. Redirects are not followed and no content coding is asked for. */
@scoped
async streamed client::open(const client* this, request message, str address)
    throws http_error, std.tls::tls_error, std.error::fault {
    std.url::url current = url_of(address);
    str scheme = current.scheme();
    throw (std.text::equal_ignore_ascii_case(scheme, "http") == false &&
           std.text::equal_ignore_ascii_case(scheme, "https") == false)
        failure(error_code::unsupported_scheme);
    const u8[] host_bytes = current.host();
    throw (current.has_authority() == false || len(host_bytes) == 0usize)
        failure(error_code::invalid_url);
    u16 port = 0u16;
    switch (current.effective_port()) {
    case variant o::some(value): port = *value;
    case variant o::none: throw failure(error_code::invalid_url);
    }
    request pending = move message;
    std.string::string target = current.target();
    std.string::string old_target = core::replace(&pending.target, move target);
    drop old_target;
    std.string::string host = current.authority_text();
    pending.headers.set("Host", host);
    limits bounds = this->settings.bounds;
    o<std.bufio::reader<own dyn(std.stream::Stream)*>> opened = o::none;
    task_scope(1) connect {
        std.bufio::reader<own dyn(std.stream::Stream)*> made =
            await open_connection(this, &current, port);
        o<std.bufio::reader<own dyn(std.stream::Stream)*>> none = core::replace(&opened, o::some(move made));
        drop none;
    }
    std.bufio::reader<own dyn(std.stream::Stream)*> channel = match (move opened) {
        case variant o::some(move made): move made;
        case variant o::none: throw failure(error_code::unexpected_end);
    };
    body_state state = state_of(framing_none, 0u64);
    response answer = response::create(0u16);
    task_scope(1) io {
        await write_request(&channel.source, &pending, "");
        response received = await read_response_head(&channel, pending.method, &bounds, false, &state);
        response old = core::replace(&answer, move received);
        drop old;
    }
    return streamed {.head = move answer, .channel = move channel, .state = state};
}

/* R-SLIB-HTTP-0012: the next piece of the body, at most 16 KiB; none at its end. */
@scoped
async o<bytes> streamed::next(streamed* this) throws http_error, std.error::fault {
    if (this->state.finished == true) { return o::none; }
    u8[16384] chunk = {};
    usize count = 0usize;
    task_scope(1) io { count += await read_body(&this->channel, &this->state, &chunk); }
    if (count == 0usize) { return o::none; }
    bytes piece_of_body = std.bytes::with_capacity(count);
    std.bytes::append(&piece_of_body, chunk[0usize..count]);
    return o::some(move piece_of_body);
}

/* The prefix of text before its first line end. */
protected str first_line(str text) {
    const u8[] bytes = text;
    usize end = 0usize;
    while (end < len(bytes) && bytes[end] != 10u8 && bytes[end] != 13u8) { end += 1usize; }
    return piece(text, 0usize, end);
}

/* R-SLIB-HTTP-0010: the text of one Server-Sent Events message: an "event:" line and an "id:"
   line when they are not empty, cut at their first line end, then a "data:" line for each line
   of data, and an empty line. */
std.string::string sse_event(str event, str id, str data) throws std.alloc::alloc_error {
    std.string::string text = std.string::create();
    str kind = first_line(event);
    const u8[] kind_bytes = kind;
    if (len(kind_bytes) != 0usize) {
        std.string::append_str(&text, "event: ");
        std.string::append_str(&text, kind);
        std.string::append_str(&text, "\n");
    }
    str name = first_line(id);
    const u8[] name_bytes = name;
    if (len(name_bytes) != 0usize) {
        std.string::append_str(&text, "id: ");
        std.string::append_str(&text, name);
        std.string::append_str(&text, "\n");
    }
    const u8[] bytes = data;
    usize start = 0usize;
    usize index = 0usize;
    while (index <= len(bytes)) {
        bool end = index == len(bytes);
        bool line_end = false;
        if (end == false) { line_end = bytes[index] == 10u8 || bytes[index] == 13u8; }
        if (end == true || line_end == true) {
            std.string::append_str(&text, "data: ");
            std.string::append_str(&text, piece(data, start, index));
            std.string::append_str(&text, "\n");
            if (end == true) { break; }
            if (bytes[index] == 13u8 && index + 1usize < len(bytes) && bytes[index + 1usize] == 10u8) {
                index += 1usize;
            }
            start = index + 1usize;
        }
        index += 1usize;
    }
    std.string::append_str(&text, "\n");
    return move text;
}

/* R-SLIB-HTTP-0010: one message of an event stream; the event type is "message" when the
   stream names none. */
struct sse_message { std.string::string event; std.string::string id; std.string::string data; };

/* R-SLIB-HTTP-0010: an incremental parser of text/event-stream (the event stream format of the
   HTML standard): it takes bytes as they arrive and yields each message whose blank line has
   arrived. */
struct sse_parser {
    protected bytes pending;
    protected usize start;
    protected std.string::string event;
    protected std.string::string last_id;
    protected std.string::string data;
    protected bool has_data;
};

sse_parser sse_parser::create() {
    return sse_parser {.pending = {}, .start = 0usize, .event = std.string::create(),
                       .last_id = std.string::create(), .data = std.string::create(),
                       .has_data = false};
}

/* Appends the next bytes of the stream. */
void sse_parser::feed(sse_parser* this, const u8[] input) throws std.alloc::alloc_error {
    if (this->start != 0usize) {
        bytes rest = {};
        const u8[] kept = this->pending.as_slice();
        std.bytes::append(&rest, kept[this->start..len(kept)]);
        bytes old = core::replace(&this->pending, move rest);
        drop old;
        this->start = 0usize;
    }
    std.bytes::append(&this->pending, input);
}

/* Handles one line of the stream and returns whether it was the blank line of a message with
   data. */
protected bool sse_line(sse_parser* parser, str line) throws std.alloc::alloc_error {
    const u8[] bytes = line;
    if (len(bytes) == 0usize) { return parser->has_data; }
    if (bytes[0usize] == 58u8) { return false; }
    usize colon = 0usize;
    while (colon < len(bytes) && bytes[colon] != 58u8) { colon += 1usize; }
    str field = piece(line, 0usize, colon);
    usize value_start = colon;
    if (colon < len(bytes)) { value_start = colon + 1usize; }
    if (value_start < len(bytes) && bytes[value_start] == 32u8) { value_start += 1usize; }
    str value = piece(line, value_start, len(bytes));
    switch (field) {
    case "data":
        std.string::append_str(&parser->data, value);
        std.string::append_str(&parser->data, "\n");
        parser->has_data = true;
    case "event":
        std.string::string kind = std.string::from_str(value);
        std.string::string old = core::replace(&parser->event, move kind);
        drop old;
    case "id":
        const u8[] name = value;
        bool has_zero = false;
        switch (std.bytes::find(name, 0u8)) {
        case variant o::some(at):
            at as void;
            has_zero = true;
        case variant o::none: break;
        }
        if (has_zero == false) {
            std.string::string given = std.string::from_str(value);
            std.string::string old = core::replace(&parser->last_id, move given);
            drop old;
        }
    default: value as void;
    }
    return false;
}

/* R-SLIB-HTTP-0010: the next complete message, none until more bytes arrive. A line ends with
   LF, CRLF or CR; comments and unknown fields are ignored; the data lines are joined with LF.
   Bytes that are not UTF-8 are invalid_encoding. */
/* The next complete line of the stream without its line end, none until it has arrived. */
protected o<std.string::string> take_line(sse_parser* parser) throws http_error, std.alloc::alloc_error {
    const u8[] bytes = parser->pending.as_slice();
    usize end = parser->start;
    while (end < len(bytes) && bytes[end] != 10u8 && bytes[end] != 13u8) { end += 1usize; }
    if (end == len(bytes)) { return o::none; }
    usize next_start = end + 1usize;
    if (bytes[end] == 13u8) {
        // A CR at the end of the bytes may still be followed by its LF.
        if (next_start == len(bytes)) { return o::none; }
        if (bytes[next_start] == 10u8) { next_start += 1usize; }
    }
    std.string::string text_line =
        std.string::from_str(text_of(bytes[parser->start..end], error_code::invalid_encoding));
    parser->start = next_start;
    return o::some(move text_line);
}

o<sse_message> sse_parser::next(sse_parser* this) throws http_error, std.alloc::alloc_error {
    while (true) {
        o<std.string::string> taken = take_line(this);
        switch (move taken) {
        case variant o::none: return o::none;
        case variant o::some(move text_line):
            bool blank = std.string::len(&text_line) == 0usize;
            bool ready = sse_line(this, text_line);
            if (ready == true) {
                usize size = std.string::len(&this->data);
                std.string::string data = core::replace(&this->data, std.string::create());
                try {
                    std.string::truncate(&data, size - 1usize);
                } catch (std.string::boundary_error rejected) {
                    // The data ends with the LF that was appended after its last line.
                    rejected as void;
                }
                std.string::string kind = core::replace(&this->event, std.string::create());
                if (std.string::len(&kind) == 0usize) { std.string::append_str(&kind, "message"); }
                this->has_data = false;
                return o::some(sse_message {.event = move kind,
                                            .id = std.string::from_str(this->last_id),
                                            .data = move data});
            }
            if (blank == true) {
                // A blank line without data resets the event type.
                std.string::string old = core::replace(&this->event, std.string::create());
                drop old;
            }
        }
    }
    return o::none;
}

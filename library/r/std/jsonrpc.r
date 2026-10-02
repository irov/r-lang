module std.jsonrpc;
import std.stream;
import std.bufio;

/* R-SLIB-JSONRPC-0001: the error codes that JSON-RPC 2.0 defines. */
const i64 parse_error = -32700i64;
const i64 invalid_request = -32600i64;
const i64 method_not_found = -32601i64;
const i64 invalid_params = -32602i64;
const i64 internal_error = -32603i64;

/* R-SLIB-JSONRPC-0002: the id of a request: a number, kept in its JSON spelling so that it is
   echoed exactly, or a string. */
@derive(equal)
enum request_id { number(std.string::string), text(std.string::string) };

/* R-SLIB-JSONRPC-0001: a message that is not JSON-RPC: its error code and message, and the id of
   the request when it could be read. */
error rpc_error { i64 code; std.string::string message; o<request_id> id; };

protected rpc_error failure(i64 code, str message, o<request_id> id) throws std.alloc::alloc_error {
    return rpc_error {.code = code, .message = std.string::from_str(message), .id = move id};
}

/* R-SLIB-JSONRPC-0002: the id of an integer. */
request_id request_id::from_integer(i64 value) throws std.alloc::alloc_error {
    return request_id::number(f"{value}");
}

/* R-SLIB-JSONRPC-0002: the id of a string. */
request_id request_id::from_text(str value) throws std.alloc::alloc_error {
    return request_id::text(std.string::from_str(value));
}

/* R-SLIB-JSONRPC-0002: the spelling of a number id or the text of a string id. */
str request_id::spelling(const request_id* this) {
    switch (*this) {
    case variant request_id::number(value): return value->as_str();
    case variant request_id::text(value): return value->as_str();
    }
}

/* R-SLIB-JSONRPC-0002: whether the id is a string. */
bool request_id::is_text(const request_id* this) {
    switch (*this) {
    case variant request_id::number(value):
        value as void;
        return false;
    case variant request_id::text(value):
        value as void;
        return true;
    }
}

/* R-SLIB-JSONRPC-0002: a copy of the id. */
request_id request_id::copy(const request_id* this) throws std.alloc::alloc_error {
    switch (*this) {
    case variant request_id::number(value): return request_id::number(std.string::from_str(value->as_str()));
    case variant request_id::text(value): return request_id::text(std.string::from_str(value->as_str()));
    }
}

/* R-SLIB-JSONRPC-0002: the id as a JSON value. */
std.json::value request_id::to_json(const request_id* this) throws std.json::error, std.alloc::alloc_error {
    switch (*this) {
    case variant request_id::number(value):
        std.json::number parsed = std.json::parse_number(value->as_bytes());
        return std.json::from_number(&parsed);
    case variant request_id::text(value): return std.json::from_string(value->as_bytes());
    }
}

/* The id that a JSON value is: a number or a string; none for any other kind. */
protected o<request_id> id_of(const std.json::value* value) throws std.alloc::alloc_error {
    std.json::value_kind kind = std.json::kind(value);
    if (kind == std.json::value_kind::number) {
        return o::some(request_id::number(std.string::from_str(std.json::text(value))));
    }
    if (kind == std.json::value_kind::string) {
        return o::some(request_id::text(std.string::from_str(std.json::text(value))));
    }
    return o::none;
}

/* R-SLIB-JSONRPC-0002: the id that a JSON value spells, a number or a string; none for a value
   of any other kind. */
o<request_id> request_id::from_json(const std.json::value* value) throws std.alloc::alloc_error {
    return id_of(value);
}

/* R-SLIB-JSONRPC-0002: a request: its id, method and parameters. */
struct request { request_id id; std.string::string method; o<std.json::value> params; };

/* R-SLIB-JSONRPC-0002: a notification: a request without id, which gets no response. */
struct notification { std.string::string method; o<std.json::value> params; };

/* R-SLIB-JSONRPC-0002: a successful response: the id of its request and the result. */
struct result_response { request_id id; std.json::value result; };

/* R-SLIB-JSONRPC-0002: an error response: the id of its request, none when the request could not
   be read, and the code, message and data of the error. */
struct error_response { o<request_id> id; i64 code; std.string::string message; o<std.json::value> data; };

/* R-SLIB-JSONRPC-0002: one message. */
enum message {
    request(request),
    notification(notification),
    result(result_response),
    failure(error_response),
};

/* R-SLIB-JSONRPC-0002: an error response for a failure. */
error_response error_response::of(const rpc_error* problem) throws std.alloc::alloc_error {
    o<request_id> id = o::none;
    switch (problem->id) {
    case variant o::some(value): id = o::some(value->copy());
    case variant o::none: break;
    }
    return error_response {.id = move id, .code = problem->code,
                           .message = std.string::from_str(problem->message.as_str()), .data = o::none};
}

/* A copy of a JSON value. */
protected std.json::value copy_value(const std.json::value* value) throws std.json::error, std.alloc::alloc_error {
    std.string::string text = std.json::stringify(value);
    return std.json::parse(text.as_bytes());
}

/* The member of an object that shall be a string, or none when it is absent or of another kind. */
protected o<str> member_text(const std.json::value* object, str name) {
    switch (std.json::find(object, name)) {
    case variant o::some(member):
        if (std.json::kind(*member) == std.json::value_kind::string) { return o::some(std.json::text(*member)); }
    case variant o::none: break;
    }
    return o::none;
}

/* Whether an object has a member of the name. */
protected bool has_member(const std.json::value* object, str name) {
    switch (std.json::find(object, name)) {
    case variant o::some(member):
        member as void;
        return true;
    case variant o::none: return false;
    }
}

/* The id member of a message object: absent, a valid id, or present with another kind. */
protected struct id_member { bool present; o<request_id> id; };

protected id_member id_member_of(const std.json::value* object) throws std.alloc::alloc_error {
    switch (std.json::find(object, "id")) {
    case variant o::some(member): return id_member {.present = true, .id = id_of(*member)};
    case variant o::none: return id_member {.present = false, .id = o::none};
    }
}

/* Whether an optional id holds an id. */
protected bool id_is_some(const (o<request_id>)* id) {
    switch (*id) {
    case variant o::some(value):
        value as void;
        return true;
    case variant o::none: return false;
    }
}

/* The error response in a message object that has an error member. */
protected error_response error_of(std.json::value* document, o<request_id> id)
    throws rpc_error, std.alloc::alloc_error {
    std.json::value object = std.json::null();
    try {
        std.json::value taken = std.json::take_field(document, "error");
        std.json::value old = core::replace(&object, move taken);
        drop old;
    } catch (std.json::error rejected) {
        (move rejected) as void;
        throw failure(internal_error, "Internal error", move id);
    }
    throw (std.json::kind(&object) != std.json::value_kind::object)
        failure(invalid_request, "Invalid Request", move id);
    i64 code = 0i64;
    bool code_valid = false;
    switch (std.json::find(&object, "code")) {
    case variant o::some(member):
        if (std.json::kind(*member) == std.json::value_kind::number) {
            try {
                code = std.convert::parse_i64(std.json::text(*member), 10u32);
                code_valid = true;
            } catch (std.convert::parse_error rejected) {
                rejected as void;
            }
        }
    case variant o::none: break;
    }
    o<str> text = member_text(&object, "message");
    std.string::string message_text = std.string::create();
    switch (text) {
    case variant o::some(value): std.string::append_str(&message_text, *value);
    case variant o::none: code_valid = false;
    }
    throw (code_valid == false) failure(invalid_request, "Invalid Request", move id);
    o<std.json::value> data = o::none;
    if (has_member(&object, "data") == true) {
        try {
            data = o::some(std.json::take_field(&object, "data"));
        } catch (std.json::error rejected) {
            (move rejected) as void;
            throw failure(internal_error, "Internal error", move id);
        }
    }
    return error_response {.id = move id, .code = code, .message = move message_text, .data = move data};
}

/* The request or notification in a message object that has a method member. */
protected message request_of(std.json::value* document, o<request_id> id)
    throws rpc_error, std.alloc::alloc_error {
    o<str> method_text = member_text(document, "method");
    bool valid = false;
    switch (method_text) {
    case variant o::some(value):
        value as void;
        valid = true;
    case variant o::none: break;
    }
    switch (std.json::find(document, "params")) {
    case variant o::some(value):
        std.json::value_kind kind = std.json::kind(*value);
        if (kind != std.json::value_kind::object && kind != std.json::value_kind::array) { valid = false; }
    case variant o::none: break;
    }
    throw (valid == false) failure(invalid_request, "Invalid Request", move id);
    std.string::string method = std.string::create();
    switch (method_text) {
    case variant o::some(value): std.string::append_str(&method, *value);
    case variant o::none: break;
    }
    o<std.json::value> params = o::none;
    if (has_member(document, "params") == true) {
        try {
            params = o::some(std.json::take_field(document, "params"));
        } catch (std.json::error rejected) {
            (move rejected) as void;
            throw failure(internal_error, "Internal error", move id);
        }
    }
    switch (move id) {
    case variant o::some(move value):
        return message::request(request {.id = move value, .method = move method, .params = move params});
    case variant o::none:
        return message::notification(notification {.method = move method, .params = move params});
    }
}

/* The result response in a message object that has a result member. */
protected message result_of(std.json::value* document, o<request_id> id)
    throws rpc_error, std.alloc::alloc_error {
    std.json::value result = std.json::null();
    try {
        std.json::value taken = std.json::take_field(document, "result");
        std.json::value old = core::replace(&result, move taken);
        drop old;
    } catch (std.json::error rejected) {
        (move rejected) as void;
        throw failure(internal_error, "Internal error", move id);
    }
    switch (move id) {
    case variant o::some(move value):
        return message::result(result_response {.id = move value, .result = move result});
    case variant o::none: throw failure(invalid_request, "Invalid Request", o::none);
    }
}

/* R-SLIB-JSONRPC-0003: reads one message from its JSON text. A text that is not JSON is
   parse_error; a value that is not a message object is invalid_request, with the id when it can
   be read: an array (a batch), a missing or other "jsonrpc", an id that is neither a number nor a
   string, a method that is not a string, params that are neither an object nor an array, and a
   response with both or neither of result and error, or with an error object without an integer
   code and a string message. */
message parse(const u8[] text) throws rpc_error, std.alloc::alloc_error {
    std.json::value document = std.json::null();
    try {
        std.json::value parsed = std.json::parse(text);
        std.json::value old = core::replace(&document, move parsed);
        drop old;
    } catch (std.json::error rejected) {
        (move rejected) as void;
        throw failure(parse_error, "Parse error", o::none);
    }
    throw (std.json::kind(&document) != std.json::value_kind::object)
        failure(invalid_request, "Invalid Request", o::none);
    id_member identity = id_member_of(&document);
    o<request_id> id = o::none;
    switch (identity.id) {
    case variant o::some(value): id = o::some(value->copy());
    case variant o::none: break;
    }
    bool version = false;
    switch (member_text(&document, "jsonrpc")) {
    case variant o::some(value): version = std.bytes::equal(*value, "2.0");
    case variant o::none: break;
    }
    bool id_valid = identity.present == false || id_is_some(&identity.id);
    throw (version == false || id_valid == false) failure(invalid_request, "Invalid Request", move id);
    if (has_member(&document, "method") == true) { return request_of(&document, move id); }
    bool has_result = has_member(&document, "result");
    bool has_error = has_member(&document, "error");
    throw (has_result == has_error) failure(invalid_request, "Invalid Request", move id);
    if (has_result == true) { return result_of(&document, move id); }
    return message::failure(error_of(&document, move id));
}

/* An object with "jsonrpc": "2.0". */
protected std.json::value envelope() throws std.json::error, std.alloc::alloc_error {
    std.json::value object = std.json::object();
    std.json::insert(&object, "jsonrpc", std.json::from_string("2.0"));
    return move object;
}

/* R-SLIB-JSONRPC-0003: the JSON text of a message, on one line. */
std.string::string encode(const message* value) throws std.json::error, std.alloc::alloc_error {
    std.json::value object = envelope();
    switch (*value) {
    case variant message::request(item):
        std.json::insert(&object, "id", item->id.to_json());
        std.json::insert(&object, "method", std.json::from_string(item->method.as_bytes()));
        switch (item->params) {
        case variant o::some(params): std.json::insert(&object, "params", copy_value(params));
        case variant o::none: break;
        }
    case variant message::notification(item):
        std.json::insert(&object, "method", std.json::from_string(item->method.as_bytes()));
        switch (item->params) {
        case variant o::some(params): std.json::insert(&object, "params", copy_value(params));
        case variant o::none: break;
        }
    case variant message::result(item):
        std.json::insert(&object, "id", item->id.to_json());
        std.json::insert(&object, "result", copy_value(&item->result));
    case variant message::failure(item):
        switch (item->id) {
        case variant o::some(id): std.json::insert(&object, "id", id->to_json());
        case variant o::none: break;
        }
        std.json::value body = std.json::object();
        std.string::string code = f"{item->code}";
        std.json::number number = std.json::parse_number(code.as_bytes());
        std.json::insert(&body, "code", std.json::from_number(&number));
        std.json::insert(&body, "message", std.json::from_string(item->message.as_bytes()));
        switch (item->data) {
        case variant o::some(data): std.json::insert(&body, "data", copy_value(data));
        case variant o::none: break;
        }
        std.json::insert(&object, "error", move body);
    }
    return std.json::stringify(&object);
}

/* R-SLIB-JSONRPC-0004: the next line of a stream of messages separated by line feeds, without its
   line feed and one carriage return before it; empty lines are skipped, and none means that the
   stream has ended. A line shall fit in the capacity of the reader (R-SLIB-BUFIO-0002). */
@generic<R: std.stream::Reader>
@scoped
async o<bytes> read_line(std.bufio::reader<R>* input) throws std.error::fault {
    while (true) {
        bytes line = {};
        usize count = 0usize;
        task_scope(1) io { count += await input->read_until(10u8, &line); }
        if (count == 0usize) { return o::none; }
        usize end = len(line);
        if (end > 0usize && line[end - 1usize] == 10u8) { end -= 1usize; }
        if (end > 0usize && line[end - 1usize] == 13u8) { end -= 1usize; }
        if (end == 0usize) { continue; }
        bytes text = {};
        std.bytes::append(&text, line[0usize..end]);
        return o::some(move text);
    }
    return o::none;
}

/* R-SLIB-JSONRPC-0004: writes a message and a line feed, then flushes the writer. */
@generic<W: std.stream::Writer>
@scoped
async void write_message(const W* output, const message* value) throws std.json::error, std.error::fault {
    std.string::string text = encode(value);
    std.string::append_str(&text, "\n");
    task_scope(1) io {
        await output->write_all_from(text.as_bytes());
        await output->flush();
    }
}

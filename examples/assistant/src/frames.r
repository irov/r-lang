module example.assistant.frames;
import std.bufio;
import std.console;
import std.jsonrpc;
import std.mcp;

/* R: the name of an error code of JSON-RPC, of MCP or of the client itself. */
str code_name(i64 code) {
    if (code == std.jsonrpc::parse_error) { return "parse error"; }
    if (code == std.jsonrpc::invalid_request) { return "invalid request"; }
    if (code == std.jsonrpc::method_not_found) { return "method not found"; }
    if (code == std.jsonrpc::invalid_params) { return "invalid params"; }
    if (code == std.jsonrpc::internal_error) { return "internal error"; }
    if (code == std.mcp::header_mismatch) { return "header mismatch"; }
    if (code == std.mcp::missing_required_client_capability) { return "missing capability"; }
    if (code == std.mcp::unsupported_protocol_version) { return "unsupported version"; }
    if (code == std.mcp::request_timeout) { return "timeout"; }
    if (code == std.mcp::connection_closed) { return "connection closed"; }
    if (code == std.mcp::protocol_violation) { return "protocol violation"; }
    if (code == std.mcp::unauthorized) { return "unauthorized"; }
    if (code == std.mcp::forbidden) { return "forbidden"; }
    return "error";
}

/* R: a failure of MCP as one line: its code, the name of the code and the message. */
std.string::string problem_line(const std.mcp::mcp_error* failure) throws std.alloc::alloc_error {
    i64 code = failure->code;
    std.string::string text = f"mcp error {code} (";
    std.string::append_str(&text, code_name(code));
    std.string::append_str(&text, "): ");
    std.string::append_str(&text, failure->message);
    return move text;
}

/* The id of a message as it is written: a number as its digits, a string in quotes. */
protected std.string::string id_text(const std.jsonrpc::request_id* id) throws std.alloc::alloc_error {
    std.string::string text = std.string::create();
    if (id->is_text() == true) { std.string::append_str(&text, "\""); }
    std.string::append_str(&text, id->spelling());
    if (id->is_text() == true) { std.string::append_str(&text, "\""); }
    return move text;
}

protected std.string::string request_text(const std.jsonrpc::request* asked) throws std.alloc::alloc_error {
    std.string::string text = std.string::from_str("request ");
    std.string::string id = id_text(&asked->id);
    std.string::append_str(&text, id);
    std.string::append_str(&text, " ");
    std.string::append_str(&text, asked->method);
    return move text;
}

protected std.string::string notification_text(const std.jsonrpc::notification* told) throws std.alloc::alloc_error {
    std.string::string text = std.string::from_str("notification ");
    std.string::append_str(&text, told->method);
    return move text;
}

protected std.string::string result_text(const std.jsonrpc::result_response* answered) throws std.alloc::alloc_error {
    std.string::string text = std.string::from_str("result ");
    std.string::string id = id_text(&answered->id);
    std.string::append_str(&text, id);
    return move text;
}

protected std.string::string error_text(const std.jsonrpc::error_response* failed) throws std.alloc::alloc_error {
    std.string::string text = std.string::from_str("error ");
    switch (failed->id) {
    case variant o::some(id):
        std.string::string spelled = id_text(id);
        std.string::append_str(&text, spelled);
    case variant o::none: std.string::append_str(&text, "-");
    }
    i64 code = failed->code;
    std.string::string rest = f" {code} ";
    std.string::append_str(&text, rest);
    std.string::append_str(&text, code_name(code));
    std.string::append_str(&text, ": ");
    std.string::append_str(&text, failed->message);
    return move text;
}

/* What a message is: its kind, id and method, or its error. */
protected std.string::string described(const std.jsonrpc::message* value) throws std.alloc::alloc_error {
    switch (*value) {
    case variant std.jsonrpc::message::request(item): return request_text(item);
    case variant std.jsonrpc::message::notification(item): return notification_text(item);
    case variant std.jsonrpc::message::result(item): return result_text(item);
    case variant std.jsonrpc::message::failure(item): return error_text(item);
    }
}

/* The description of a message and whether its line differs from its canonical form. */
protected std.string::string checked(const std.jsonrpc::message* value, const u8[] line)
    throws std.json::error, std.alloc::alloc_error {
    std.string::string text = described(value);
    std.string::string canonical = std.jsonrpc::encode(value);
    if (std.bytes::equal(canonical.as_bytes(), line) == false) { std.string::append_str(&text, " (rewritten)"); }
    return move text;
}

/* The response that a peer sends for a line that is no message. */
protected std.jsonrpc::message refusal_of(const std.jsonrpc::rpc_error* problem) throws std.alloc::alloc_error {
    std.jsonrpc::error_response reply = std.jsonrpc::error_response::of(problem);
    return std.jsonrpc::message::failure(move reply);
}

/* R: reads JSON-RPC messages, one per line, from standard input and writes each in its canonical
   form to standard output, or the error response that a peer sends for a line that is no
   message; standard error describes each line. The status is 1 when a line was no message. */
async i32 frames() throws std.error::fault {
    std.bufio::reader<std.io::input> lines = std.bufio::reader<std.io::input>::create(std.io::stdin(), 1048576usize);
    std.io::output output = std.io::stdout();
    i32 status = 0;
    bool more = true;
    while (more == true) {
        o<bytes> next = o::none;
        task_scope(1) io {
            o<bytes> got = await std.jsonrpc::read_line(&lines);
            o<bytes> old = core::replace(&next, move got);
            drop old;
        }
        switch (move next) {
        case variant o::some(move line):
            std.string::string note = std.string::create();
            o<std.jsonrpc::message> written = o::none;
            try {
                std.jsonrpc::message parsed = std.jsonrpc::parse(line.as_slice());
                std.string::string text = checked(&parsed, line.as_slice());
                std.string::string old = core::replace(&note, move text);
                drop old;
                o<std.jsonrpc::message> none = core::replace(&written, o::some(move parsed));
                drop none;
            } catch (std.jsonrpc::rpc_error problem) {
                std.jsonrpc::message refused = refusal_of(&problem);
                std.string::string text = std.string::from_str("invalid, answered with ");
                std.string::string reply = described(&refused);
                std.string::append_str(&text, reply);
                std.string::string old = core::replace(&note, move text);
                drop old;
                o<std.jsonrpc::message> none = core::replace(&written, o::some(move refused));
                drop none;
                status = 1;
            } catch (std.json::error failure) {
                (move failure) as void;
                status = 1;
            }
            await std.console::eprintln(move note);
            switch (move written) {
            case variant o::some(move message):
                try {
                    task_scope(1) io { await std.jsonrpc::write_message(&output, &message); }
                } catch (std.json::error failure) {
                    (move failure) as void;
                    status = 1;
                }
            case variant o::none: break;
            }
        case variant o::none: more = false;
        }
    }
    drop lines;
    drop output;
    return status;
}

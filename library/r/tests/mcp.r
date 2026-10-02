module tests.std.mcp;
import std.test;
import std.bufio;
import std.stream;
import std.text;
import std.jsonrpc;
import std.http;
import std.service;
import std.mcp;

// The tests of std.mcp (Library R-SLIB-MCP-0001..): a server with tools, resources, a resource
// template, a prompt and completion, driven over the stdio framing (R-SLIB-MCP-0014) through two
// loopback TCP connections, one per direction. Run in test mode (Core R-FUNC-0025).

struct app { std.mcp::notifier changes; };

struct echo_args { @json(description = "The text to echo") std.string::string text; };
struct sum_args { i64 a; i64 b; };
struct sum_result { i64 sum; };
struct confirm_form { @json(description = "Whether to go on") bool ok; };

protected std.string::string arguments_of(const std.mcp::call* request) throws std.alloc::alloc_error {
    try {
        return request->arguments_text();
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    return std.string::from_str("{}");
}

async std.mcp::tool_outcome echo(arc app shared, std.mcp::call request) throws std.error::fault {
    drop shared;
    std.string::string text = arguments_of(&request);
    try {
        echo_args args = std.json::unmarshal(text.as_bytes());
        return std.mcp::tool_outcome::complete(std.mcp::tool_result::of_text(args.text.as_str()));
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    return std.mcp::tool_outcome::complete(std.mcp::tool_result::of_error("echo needs a text"));
}

async std.mcp::tool_outcome sum(arc app shared, std.mcp::call request) throws std.error::fault {
    drop shared;
    std.string::string text = arguments_of(&request);
    try {
        sum_args args = std.json::unmarshal(text.as_bytes());
        sum_result total = {.sum = args.a + args.b};
        return std.mcp::tool_outcome::complete(std.mcp::tool_result::of_structured(&total));
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    return std.mcp::tool_outcome::complete(std.mcp::tool_result::of_error("sum needs a and b"));
}

/* Asks for a form first; the retry carries the answer and the state of the first round. */
async std.mcp::tool_outcome confirm(arc app shared, std.mcp::call request) throws std.error::fault {
    drop shared;
    switch (request.input.get("confirm")) {
    case variant o::some(given):
        if (std.bytes::equal(request.input.state_text(), "round-1") == false) {
            return std.mcp::tool_outcome::complete(std.mcp::tool_result::of_error("lost state"));
        }
        switch ((*given)->action) {
        case std.mcp::answer_action::accept:
            switch ((*given)->field("ok")) {
            case variant o::some(ok):
                if (std.json::boolean(*ok) == true) {
                    return std.mcp::tool_outcome::complete(std.mcp::tool_result::of_text("confirmed"));
                }
            case variant o::none: break;
            }
        default: break;
        }
        return std.mcp::tool_outcome::complete(std.mcp::tool_result::of_text("declined"));
    case variant o::none: break;
    }
    std.mcp::input_required asked = std.mcp::input_required::create();
    try {
        asked.ask_form("confirm", "Proceed?", std.json::schema::<confirm_form>());
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    asked.set_state("round-1");
    return std.mcp::tool_outcome::needs_input(move asked);
}

async std.mcp::tool_outcome visit(arc app shared, std.mcp::call request) throws std.error::fault {
    drop shared;
    drop request;
    std.mcp::input_required asked = std.mcp::input_required::create();
    asked.ask_url("login", "Sign in to continue", "https://example.com/login");
    return std.mcp::tool_outcome::needs_input(move asked);
}

async std.mcp::tool_outcome slow(arc app shared, std.mcp::call request) throws std.error::fault {
    drop shared;
    o<f64> total = o::some(3.0f64);
    bool first = request.progress.report(1.0f64, total, "one");
    bool second = request.progress.report(1.0f64, total, "again");
    bool third = request.progress.report(2.0f64, total, "");
    std.string::string text = f"{first} {second} {third}";
    return std.mcp::tool_outcome::complete(std.mcp::tool_result::of_text(text.as_str()));
}

async std.mcp::tool_outcome touch(arc app shared, std.mcp::call request) throws std.error::fault {
    drop request;
    shared->changes.resource_updated("note://7");
    shared->changes.tools_changed();
    return std.mcp::tool_outcome::complete(std.mcp::tool_result::of_text("touched"));
}

async std.mcp::tool_outcome wait(arc app shared, std.mcp::call request) throws std.error::fault {
    drop shared;
    drop request;
    await std.time::sleep_for(std.time::duration_from_seconds(30i64));
    return std.mcp::tool_outcome::complete(std.mcp::tool_result::of_text("waited"));
}

async std.mcp::tool_outcome region(arc app shared, std.mcp::call request) throws std.error::fault {
    drop shared;
    drop request;
    return std.mcp::tool_outcome::complete(std.mcp::tool_result::of_text("routed"));
}

protected std.json::value region_schema() throws std.json::error, std.alloc::alloc_error {
    return std.json::parse("{\"type\":\"object\",\"properties\":{\"region\":{\"type\":\"string\",\"x-mcp-header\":\"Region\"},\"limit\":{\"type\":\"integer\",\"x-mcp-header\":\"Limit\"}}}");
}

async std.mcp::read_outcome readme(arc app shared, std.mcp::read request) throws std.error::fault {
    drop shared;
    array<std.mcp::contents> items = std.array::create::<std.mcp::contents>();
    try {
        items.push(std.mcp::contents::of_text(request.uri.as_str(), "text/plain", "Read me"));
        bytes logo = {};
        std.bytes::append(&logo, "PNG");
        items.push(std.mcp::contents::of_blob("file:///logo.png", "image/png", move logo));
    } catch (std.array::push_error<std.mcp::contents> rejected) {
        (move rejected) as void;
    }
    return std.mcp::read_outcome::complete(move items);
}

async std.mcp::read_outcome note(arc app shared, std.mcp::read request) throws std.error::fault {
    drop shared;
    str id = "";
    switch (request.variable("id")) {
    case variant o::some(value): id = *value;
    case variant o::none: break;
    }
    if (std.bytes::equal(id, "404") == true) { return std.mcp::read_outcome::not_found; }
    std.string::string text = f"note {id}";
    array<std.mcp::contents> items = std.array::create::<std.mcp::contents>();
    try {
        items.push(std.mcp::contents::of_text(request.uri.as_str(), "text/plain", text.as_str()));
    } catch (std.array::push_error<std.mcp::contents> rejected) {
        (move rejected) as void;
    }
    return std.mcp::read_outcome::complete(move items);
}

async std.mcp::prompt_outcome greet(arc app shared, std.mcp::prompt_call request) throws std.error::fault {
    drop shared;
    str name = "";
    switch (request.argument("name")) {
    case variant o::some(value): name = *value;
    case variant o::none: break;
    }
    std.string::string text = f"Say hello to {name}";
    std.mcp::prompt_result result = std.mcp::prompt_result::create("A greeting");
    result.say(std.mcp::role::user, text.as_str());
    return std.mcp::prompt_outcome::complete(move result);
}

async std.mcp::completion names(arc app shared, std.mcp::completion_request request) throws std.error::fault {
    drop shared;
    std.mcp::completion found = std.mcp::completion::create();
    try {
        if (std.text::starts_with("Ann", request.value.as_str()) == true) { found.values.push(std.string::from_str("Ann")); }
        if (std.text::starts_with("Bob", request.value.as_str()) == true) { found.values.push(std.string::from_str("Bob")); }
    } catch (std.array::push_error<std.string::string> rejected) {
        (move rejected) as void;
    }
    found.total = o::some(2u64);
    return move found;
}

protected std.mcp::server<app> build_server() throws std.test::failure, std.mcp::mcp_error, std.json::error,
                                                      std.alloc::alloc_error {
    std.mcp::notifier changes = std.mcp::notifier::create();
    arc app shared = new arc app {.changes = changes.share()};
    std.mcp::options settings = {.ttl_ms = 60000u64, .page_size = 2usize, .list_changed = true, .subscribe = true};
    std.mcp::server<app> host = std.mcp::server<app>::create(std.mcp::implementation::create("tests", "1.2.3"),
                                                             move shared, move changes, settings);
    host.set_instructions("Test tools.");
    host.add_tool(std.mcp::tool::create("echo", "Echoes its text", std.json::schema::<echo_args>()), echo);
    std.mcp::tool adder = std.mcp::tool::create("sum", "Adds two integers", std.json::schema::<sum_args>());
    adder.output_schema = o::some(std.json::schema::<sum_result>());
    host.add_tool(move adder, sum);
    host.add_tool(std.mcp::tool::create("confirm", "Asks first", std.mcp::no_arguments()), confirm);
    host.add_tool(std.mcp::tool::create("visit", "Needs a browser", std.json::schema::<echo_args>()), visit);
    host.add_tool(std.mcp::tool::create("slow", "Reports progress", std.json::schema::<echo_args>()), slow);
    host.add_tool(std.mcp::tool::create("touch", "Changes things", std.json::schema::<echo_args>()), touch);
    host.add_tool(std.mcp::tool::create("wait", "Waits long", std.mcp::no_arguments()), wait);
    host.add_tool(std.mcp::tool::create("region", "Mirrors arguments", region_schema()), region);
    host.add_resource(std.mcp::resource::create("file:///readme.txt", "readme", "text/plain"), readme);
    host.add_template(std.mcp::resource_template::create("note://{id}", "note", "text/plain"), note);
    std.mcp::prompt hello = std.mcp::prompt::create("greet", "Greets someone");
    hello.argument("name", "Who", true);
    host.add_prompt(move hello, greet);
    host.complete_with(names);
    return move host;
}

protected async std.net::tcp_listener open_listener() throws std.error::fault {
    std.net::socket_address local = {.address = std.net::parse_ip("127.0.0.1"), .port = 0u16,
                                     .scope_id = 0u32};
    std.net::listen_options options = {.backlog = 4u32, .reuse_address = true, .v6_only = false};
    return await local.listen(options);
}

/* The JSON text of the value at a dotted path of member names and array indices, or <missing>. */
protected std.string::string path_text(const std.json::value* root, str path) throws std.json::error, std.alloc::alloc_error {
    const std.json::value* current = root;
    for (str part in std.text::split(path, ".")) {
        o<const std.json::value*> next = o::none;
        if (std.json::kind(current) == std.json::value_kind::array) {
            try {
                next = std.json::get(current, std.convert::parse_usize(part, 10u32));
            } catch (std.convert::parse_error rejected) {
                rejected as void;
            }
        } else {
            next = std.json::find(current, part);
        }
        switch (next) {
        case variant o::some(found): current = *found;
        case variant o::none: return std.string::from_str("<missing>");
        }
    }
    return std.json::stringify(current);
}

protected void expect(const std.json::value* root, str path, str expected)
    throws std.test::failure, std.json::error, std.alloc::alloc_error {
    std.string::string text = path_text(root, path);
    std.test::equal_text(text.as_str(), expected);
}

/* A request line with the _meta of 2026-07-28, the id, method and further parameters. */
protected std.string::string request_line(str id, str method, str params) throws std.alloc::alloc_error {
    std.string::string line = f"{{\"jsonrpc\":\"2.0\",\"id\":{id},\"method\":\"{method}\",\"params\":{{";
    std.string::append_str(&line, params);
    const u8[] more = params;
    if (len(more) != 0usize) { std.string::append_str(&line, ","); }
    std.string::append_str(&line, "\"_meta\":{\"io.modelcontextprotocol/protocolVersion\":\"2026-07-28\",");
    std.string::append_str(&line, "\"io.modelcontextprotocol/clientCapabilities\":{\"elicitation\":{\"form\":{}}},");
    std.string::append_str(&line, "\"progressToken\":\"p1\"}}}\n");
    return move line;
}

@scoped
protected async void send_text(const std.net::tcp_stream* peer, str text) throws std.error::fault {
    task_scope(1) io { await std.net::tcp_write_all_from(peer, text); }
}

@scoped
protected async std.json::value receive(std.bufio::reader<std.net::tcp_stream>* replies)
    throws std.error::fault, std.test::failure, std.json::error {
    o<bytes> next = o::none;
    task_scope(1) io {
        o<bytes> got = await std.jsonrpc::read_line(replies);
        o<bytes> old = core::replace(&next, move got);
        drop old;
    }
    switch (move next) {
    case variant o::some(move line): return std.json::parse(line.as_slice());
    case variant o::none: break;
    }
    std.test::fail("the server closed its output");
    return std.json::null();
}

@scoped
protected async std.json::value call(const std.net::tcp_stream* peer, std.bufio::reader<std.net::tcp_stream>* replies,
                                     str id, str method, str params)
    throws std.error::fault, std.test::failure, std.json::error {
    std.string::string line = request_line(id, method, params);
    task_scope(1) io {
        await send_text(peer, line.as_str());
        return await receive(replies);
    }
}

@scoped
protected async void discovers_and_lists(const std.net::tcp_stream* peer,
                                         std.bufio::reader<std.net::tcp_stream>* replies)
    throws std.error::fault, std.test::failure, std.json::error {
    task_scope(1) io {
        std.json::value found = await call(peer, replies, "1", "server/discover", "");
        expect(&found, "id", "1");
        expect(&found, "result.resultType", "\"complete\"");
        expect(&found, "result.supportedVersions", "[\"2026-07-28\"]");
        expect(&found, "result.capabilities",
               "{\"tools\":{\"listChanged\":true},\"resources\":{\"subscribe\":true,\"listChanged\":true},\"prompts\":{\"listChanged\":true},\"completions\":{}}");
        expect(&found, "result.instructions", "\"Test tools.\"");
        expect(&found, "result.ttlMs", "60000");
        expect(&found, "result.cacheScope", "\"public\"");
        expect(&found, "result._meta", "{\"io.modelcontextprotocol/serverInfo\":{\"name\":\"tests\",\"version\":\"1.2.3\"}}");
        std.json::value tools = await call(peer, replies, "\"t1\"", "tools/list", "");
        expect(&tools, "id", "\"t1\"");
        expect(&tools, "result.tools.0.name", "\"echo\"");
        expect(&tools, "result.tools.0.inputSchema.properties.text.description", "\"The text to echo\"");
        expect(&tools, "result.tools.1.outputSchema.required", "[\"sum\"]");
        expect(&tools, "result.nextCursor", "\"2\"");
        std.json::value page = await call(peer, replies, "2", "tools/list", "\"cursor\":\"2\"");
        expect(&page, "result.tools.0.name", "\"confirm\"");
        std.json::value last = await call(peer, replies, "3", "tools/list", "\"cursor\":\"4\"");
        expect(&last, "result.tools.1.name", "\"touch\"");
        std.json::value tail = await call(peer, replies, "5", "tools/list", "\"cursor\":\"6\"");
        expect(&tail, "result.tools.0.name", "\"wait\"");
        expect(&tail, "result.tools.1.name", "\"region\"");
        expect(&tail, "result.nextCursor", "<missing>");
        std.json::value wrong = await call(peer, replies, "4", "tools/list", "\"cursor\":\"x\"");
        expect(&wrong, "error.code", "-32602");
        expect(&wrong, "error.message", "\"Invalid cursor\"");
    }
}

@scoped
protected async void calls_tools(const std.net::tcp_stream* peer, std.bufio::reader<std.net::tcp_stream>* replies)
    throws std.error::fault, std.test::failure, std.json::error {
    task_scope(1) io {
        std.json::value echoed = await call(peer, replies, "10", "tools/call",
                                            "\"name\":\"echo\",\"arguments\":{\"text\":\"hi\"}");
        expect(&echoed, "result.content", "[{\"type\":\"text\",\"text\":\"hi\"}]");
        expect(&echoed, "result.resultType", "\"complete\"");
        expect(&echoed, "result.isError", "<missing>");
        std.json::value bad = await call(peer, replies, "11", "tools/call", "\"name\":\"echo\",\"arguments\":{}");
        expect(&bad, "result.isError", "true");
        std.json::value added = await call(peer, replies, "12", "tools/call",
                                           "\"name\":\"sum\",\"arguments\":{\"a\":2,\"b\":40}");
        expect(&added, "result.structuredContent", "{\"sum\":42}");
        expect(&added, "result.content.0.text", "\"{\\\"sum\\\":42}\"");
        std.json::value unknown = await call(peer, replies, "13", "tools/call", "\"name\":\"nope\"");
        expect(&unknown, "error.code", "-32602");
        expect(&unknown, "error.message", "\"Unknown tool: nope\"");
        std.json::value progress = await call(peer, replies, "14", "tools/call", "\"name\":\"slow\"");
        expect(&progress, "method", "\"notifications/progress\"");
        expect(&progress, "params", "{\"progressToken\":\"p1\",\"progress\":1,\"total\":3,\"message\":\"one\"}");
        std.json::value second = await receive(replies);
        expect(&second, "params.progress", "2");
        std.json::value done = await receive(replies);
        expect(&done, "id", "14");
        expect(&done, "result.content.0.text", "\"true false true\"");
    }
}

@scoped
protected async void asks_for_input(const std.net::tcp_stream* peer, std.bufio::reader<std.net::tcp_stream>* replies)
    throws std.error::fault, std.test::failure, std.json::error {
    task_scope(1) io {
        std.json::value asked = await call(peer, replies, "20", "tools/call", "\"name\":\"confirm\"");
        expect(&asked, "result.resultType", "\"input_required\"");
        expect(&asked, "result.inputRequests.confirm.method", "\"elicitation/create\"");
        expect(&asked, "result.inputRequests.confirm.params.mode", "\"form\"");
        expect(&asked, "result.inputRequests.confirm.params.requestedSchema.properties.ok.type", "\"boolean\"");
        expect(&asked, "result.requestState", "\"round-1\"");
        std.json::value done = await call(peer, replies, "21", "tools/call",
            "\"name\":\"confirm\",\"inputResponses\":{\"confirm\":{\"action\":\"accept\",\"content\":{\"ok\":true}}},\"requestState\":\"round-1\"");
        expect(&done, "result.content.0.text", "\"confirmed\"");
        std.json::value declined = await call(peer, replies, "22", "tools/call",
            "\"name\":\"confirm\",\"inputResponses\":{\"confirm\":{\"action\":\"decline\"}},\"requestState\":\"round-1\"");
        expect(&declined, "result.content.0.text", "\"declined\"");
        std.json::value broken = await call(peer, replies, "23", "tools/call",
            "\"name\":\"confirm\",\"inputResponses\":{\"confirm\":{\"action\":\"maybe\"}}");
        expect(&broken, "error.code", "-32602");
        std.json::value missing = await call(peer, replies, "24", "tools/call", "\"name\":\"visit\"");
        expect(&missing, "error.code", "-32021");
        expect(&missing, "error.data", "{\"requiredCapabilities\":{\"elicitation\":{\"url\":{}}}}");
    }
}

@scoped
protected async void reads_resources(const std.net::tcp_stream* peer, std.bufio::reader<std.net::tcp_stream>* replies)
    throws std.error::fault, std.test::failure, std.json::error {
    task_scope(1) io {
        std.json::value listed = await call(peer, replies, "30", "resources/list", "");
        expect(&listed, "result.resources", "[{\"uri\":\"file:///readme.txt\",\"name\":\"readme\",\"mimeType\":\"text/plain\"}]");
        std.json::value templates = await call(peer, replies, "31", "resources/templates/list", "");
        expect(&templates, "result.resourceTemplates.0.uriTemplate", "\"note://{id}\"");
        std.json::value read = await call(peer, replies, "32", "resources/read", "\"uri\":\"file:///readme.txt\"");
        expect(&read, "result.contents",
               "[{\"uri\":\"file:///readme.txt\",\"mimeType\":\"text/plain\",\"text\":\"Read me\"},{\"uri\":\"file:///logo.png\",\"mimeType\":\"image/png\",\"blob\":\"UE5H\"}]");
        expect(&read, "result.ttlMs", "60000");
        std.json::value noted = await call(peer, replies, "33", "resources/read", "\"uri\":\"note://a%20b\"");
        expect(&noted, "result.contents.0.text", "\"note a b\"");
        std.json::value gone = await call(peer, replies, "34", "resources/read", "\"uri\":\"note://404\"");
        expect(&gone, "error.code", "-32602");
        expect(&gone, "error.data", "{\"uri\":\"note://404\"}");
        std.json::value nowhere = await call(peer, replies, "35", "resources/read", "\"uri\":\"note://a/b\"");
        expect(&nowhere, "error.message", "\"Resource not found\"");
    }
}

@scoped
protected async void gets_prompts(const std.net::tcp_stream* peer, std.bufio::reader<std.net::tcp_stream>* replies)
    throws std.error::fault, std.test::failure, std.json::error {
    task_scope(1) io {
        std.json::value listed = await call(peer, replies, "40", "prompts/list", "");
        expect(&listed, "result.prompts.0.arguments", "[{\"name\":\"name\",\"description\":\"Who\",\"required\":true}]");
        std.json::value got = await call(peer, replies, "41", "prompts/get", "\"name\":\"greet\",\"arguments\":{\"name\":\"Ada\"}");
        expect(&got, "result.messages", "[{\"role\":\"user\",\"content\":{\"type\":\"text\",\"text\":\"Say hello to Ada\"}}]");
        expect(&got, "result.ttlMs", "<missing>");
        std.json::value missing = await call(peer, replies, "42", "prompts/get", "\"name\":\"greet\"");
        expect(&missing, "error.message", "\"Missing required argument: name\"");
        std.json::value completed = await call(peer, replies, "43", "completion/complete",
            "\"ref\":{\"type\":\"ref/prompt\",\"name\":\"greet\"},\"argument\":{\"name\":\"name\",\"value\":\"A\"}");
        expect(&completed, "result.completion", "{\"values\":[\"Ann\"],\"total\":2}");
        std.json::value unknown = await call(peer, replies, "44", "completion/complete",
            "\"ref\":{\"type\":\"ref/resource\",\"uri\":\"note://{id}\"},\"argument\":{\"name\":\"other\",\"value\":\"\"}");
        expect(&unknown, "error.message", "\"Unknown argument: other\"");
    }
}

@scoped
protected async void refuses_malformed(const std.net::tcp_stream* peer, std.bufio::reader<std.net::tcp_stream>* replies)
    throws std.error::fault, std.test::failure, std.json::error {
    task_scope(1) io {
        await send_text(peer, "{\"jsonrpc\":\"2.0\",\"id\":50,\"method\":\"tools/list\",\"params\":{}}\n");
        std.json::value no_meta = await receive(replies);
        expect(&no_meta, "error.code", "-32602");
        await send_text(peer, "{\"jsonrpc\":\"2.0\",\"id\":51,\"method\":\"tools/list\",\"params\":{\"_meta\":{\"io.modelcontextprotocol/protocolVersion\":\"2025-11-25\",\"io.modelcontextprotocol/clientCapabilities\":{}}}}\n");
        std.json::value old = await receive(replies);
        expect(&old, "error.code", "-32022");
        expect(&old, "error.data", "{\"supported\":[\"2026-07-28\"],\"requested\":\"2025-11-25\"}");
        std.json::value unknown = await call(peer, replies, "52", "initialize", "");
        expect(&unknown, "error.code", "-32601");
        await send_text(peer, "not json\n");
        std.json::value garbage = await receive(replies);
        expect(&garbage, "error.code", "-32700");
        expect(&garbage, "id", "<missing>");
        await send_text(peer, "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/whatever\"}\n");
        await send_text(peer, "[1]\n");
        std.json::value batch = await receive(replies);
        expect(&batch, "error.code", "-32600");
    }
}

/* The text of member name of a message, <missing> when absent. */
protected std.string::string kind_of(const std.json::value* message) throws std.json::error, std.alloc::alloc_error {
    std.string::string method = path_text(message, "method");
    if (std.bytes::equal(method.as_bytes(), "<missing>") == false) { return move method; }
    return path_text(message, "id");
}

@scoped
protected async void listens_and_cancels(const std.net::tcp_stream* peer,
                                         std.bufio::reader<std.net::tcp_stream>* replies)
    throws std.error::fault, std.test::failure, std.json::error {
    std.string::string line = request_line("\"L1\"", "subscriptions/listen",
        "\"notifications\":{\"toolsListChanged\":true,\"promptsListChanged\":false,\"resourceSubscriptions\":[\"note://7\"]}");
    std.string::string touch_line = request_line("60", "tools/call", "\"name\":\"touch\"");
    std.string::string wait_line = request_line("70", "tools/call", "\"name\":\"wait\"");
    task_scope(1) io {
        await send_text(peer, line.as_str());
        std.json::value ack = await receive(replies);
        expect(&ack, "method", "\"notifications/subscriptions/acknowledged\"");
        expect(&ack, "params",
               "{\"notifications\":{\"toolsListChanged\":true,\"resourceSubscriptions\":[\"note://7\"]},\"_meta\":{\"io.modelcontextprotocol/subscriptionId\":\"L1\"}}");
        await send_text(peer, touch_line.as_str());
        u32 seen = 0u32;
        for (u32 round = 0u32; round < 3u32; round += 1u32) {
            std.json::value message = await receive(replies);
            std.string::string kind = kind_of(&message);
            if (std.bytes::equal(kind.as_bytes(), "60") == true) {
                expect(&message, "result.content.0.text", "\"touched\"");
                seen += 1u32;
            }
            if (std.bytes::equal(kind.as_bytes(), "\"notifications/resources/updated\"") == true) {
                expect(&message, "params", "{\"uri\":\"note://7\",\"_meta\":{\"io.modelcontextprotocol/subscriptionId\":\"L1\"}}");
                seen += 10u32;
            }
            if (std.bytes::equal(kind.as_bytes(), "\"notifications/tools/list_changed\"") == true) {
                expect(&message, "params._meta", "{\"io.modelcontextprotocol/subscriptionId\":\"L1\"}");
                seen += 100u32;
            }
        }
        std.test::equal(seen, 111u32);
        await send_text(peer, wait_line.as_str());
        await send_text(peer, "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/cancelled\",\"params\":{\"requestId\":70,\"reason\":\"no longer needed\"}}\n");
        std.json::value next = await call(peer, replies, "71", "tools/call", "\"name\":\"echo\",\"arguments\":{\"text\":\"after\"}");
        expect(&next, "id", "71");
        expect(&next, "result.content.0.text", "\"after\"");
    }
}

/* The end of the input closes the subscription with its result and notifications/cancelled. */
@scoped
protected async void closes_gracefully(const std.net::tcp_stream* peer,
                                       std.bufio::reader<std.net::tcp_stream>* replies)
    throws std.error::fault, std.test::failure, std.json::error {
    task_scope(1) io {
        await std.net::tcp_shutdown(peer, std.net::shutdown_direction::write);
        std.json::value result = await receive(replies);
        expect(&result, "id", "\"L1\"");
        expect(&result, "result.resultType", "\"complete\"");
        expect(&result, "result._meta.io", "<missing>");
        std.string::string meta = path_text(&result, "result._meta");
        std.test::check(std.text::starts_with(meta.as_str(), "{\"io.modelcontextprotocol/subscriptionId\":\"L1\","),
                        "the result names the subscription");
        std.json::value cancelled = await receive(replies);
        expect(&cancelled, "method", "\"notifications/cancelled\"");
        expect(&cancelled, "params", "{\"requestId\":\"L1\"}");
    }
}

@test
async void serves_over_stdio_framing() throws std.error::fault, std.test::failure, std.mcp::mcp_error,
                                              std.json::error {
    std.net::tcp_listener listener = await open_listener();
    std.net::socket_address endpoint = listener.local_address();
    std.net::tcp_stream requests = await endpoint.connect();
    std.net::tcp_connection server_input = await listener.accept();
    std.net::tcp_stream responses = await endpoint.connect();
    std.net::tcp_connection server_output = await listener.accept();
    arc std.mcp::server<app> host = new arc std.mcp::server<app>(build_server());
    std.bufio::reader<std.net::tcp_stream> replies =
        std.bufio::reader<std.net::tcp_stream>::create(move responses, 65536usize);
    task_scope(2) session {
        auto serving = std.mcp::serve_streams(std.arc::clone(&host), move server_input, move server_output);
        await discovers_and_lists(&requests, &replies);
        await calls_tools(&requests, &replies);
        await asks_for_input(&requests, &replies);
        await reads_resources(&requests, &replies);
        await gets_prompts(&requests, &replies);
        await refuses_malformed(&requests, &replies);
        await listens_and_cancels(&requests, &replies);
        await closes_gracefully(&requests, &replies);
        await move serving;
    }
    await (move requests).close();
    await (move listener).close();
}

/* ---- Streamable HTTP ---- */

/* A POST of an MCP message with the headers of a client: Content-Type, Accept, the protocol
   version and the method, and Mcp-Name when name is not empty. */
protected std.http::request mcp_post(str method, str name, str body) throws std.alloc::alloc_error, std.http::http_error {
    std.http::request message = std.http::request::create(std.http::method::post, "/mcp");
    message.headers.add("Content-Type", "application/json");
    message.headers.add("Accept", "application/json, text/event-stream");
    message.headers.add("MCP-Protocol-Version", "2026-07-28");
    message.headers.add("Mcp-Method", method);
    const u8[] named = name;
    if (len(named) != 0usize) { message.headers.add("Mcp-Name", name); }
    std.bytes::append(&message.body, body);
    return move message;
}

@scoped
protected async std.http::response exchange(std.bufio::reader<std.net::tcp_stream>* connection,
                                            std.http::request message)
    throws std.error::fault, std.http::http_error {
    std.http::limits bounds = {};
    task_scope(1) io {
        await std.http::write_request(&connection->source, &message, "127.0.0.1");
        return await std.http::read_response(connection, message.method, &bounds);
    }
}

/* Posts a request line of request_line and returns the response. */
@scoped
protected async std.http::response post_request(std.bufio::reader<std.net::tcp_stream>* connection, str id,
                                                str method, str name, str params)
    throws std.error::fault, std.http::http_error {
    std.string::string line = request_line(id, method, params);
    std.http::request message = mcp_post(method, name, line.as_str());
    task_scope(1) io { return await exchange(connection, move message); }
}

protected std.json::value body_json(const std.http::response* answer) throws std.json::error, std.alloc::alloc_error {
    return std.json::parse(answer->body.as_slice());
}

protected void expect_header(const std.http::response* answer, str name, str value)
    throws std.test::failure, std.alloc::alloc_error {
    switch (answer->headers.get(name)) {
    case variant o::some(found): std.test::equal_text(*found, value);
    case variant o::none: std.test::fail(name);
    }
}

/* The messages of the data of each event of an event stream body, in order. */
protected array<std.json::value> events_of(const std.http::response* answer)
    throws std.http::http_error, std.json::error, std.alloc::alloc_error {
    array<std.json::value> found = std.array::create::<std.json::value>();
    std.http::sse_parser parser = std.http::sse_parser::create();
    parser.feed(answer->body.as_slice());
    bool more = true;
    while (more == true) {
        o<std.http::sse_message> next = parser.next();
        switch (move next) {
        case variant o::some(move event):
            try {
                found.push(std.json::parse(event.data.as_bytes()));
            } catch (std.array::push_error<std.json::value> rejected) {
                (move rejected) as void;
            }
        case variant o::none: more = false;
        }
    }
    return move found;
}

/* Requests that the endpoint answers, and names and methods it refuses. */
@scoped
protected async void posts_messages(std.bufio::reader<std.net::tcp_stream>* connection)
    throws std.error::fault, std.test::failure, std.http::http_error, std.json::error {
    task_scope(1) io {
        std.http::response listed = await post_request(connection, "1", "tools/list", "", "");
        std.test::equal(listed.status, 200u16);
        expect_header(&listed, "Content-Type", "application/json");
        std.json::value tools = body_json(&listed);
        expect(&tools, "result.tools.0.name", "\"echo\"");
        std.http::response echoed = await post_request(connection, "2", "tools/call", "=?base64?ZWNobw==?=",
                                                       "\"name\":\"echo\",\"arguments\":{\"text\":\"over http\"}");
        std.json::value echo_body = body_json(&echoed);
        expect(&echo_body, "result.content.0.text", "\"over http\"");
        std.http::response named_wrong = await post_request(connection, "3", "tools/call", "sum",
                                                            "\"name\":\"echo\",\"arguments\":{\"text\":\"x\"}");
        std.test::equal(named_wrong.status, 400u16);
        std.json::value mismatch_body = body_json(&named_wrong);
        expect(&mismatch_body, "error.code", "-32020");
        std.http::response unnamed = await post_request(connection, "4", "tools/call", "",
                                                        "\"name\":\"echo\"");
        std.test::equal(unnamed.status, 400u16);
        std.http::response unknown = await post_request(connection, "5", "initialize", "", "");
        std.test::equal(unknown.status, 404u16);
        std.json::value unknown_body = body_json(&unknown);
        expect(&unknown_body, "error.code", "-32601");
    }
}

/* The method and version headers against the body. */
@scoped
protected async void checks_versions(std.bufio::reader<std.net::tcp_stream>* connection)
    throws std.error::fault, std.test::failure, std.http::http_error, std.json::error {
    task_scope(1) io {
        std.string::string line = request_line("6", "tools/list", "");
        std.http::request wrong_method = mcp_post("tools/call", "", line.as_str());
        std.http::response method_mismatch = await exchange(connection, move wrong_method);
        std.test::equal(method_mismatch.status, 400u16);
        std.http::request no_version = std.http::request::create(std.http::method::post, "/mcp");
        no_version.headers.add("Mcp-Method", "tools/list");
        std.bytes::append(&no_version.body, line.as_str());
        std.http::response versionless = await exchange(connection, move no_version);
        std.test::equal(versionless.status, 400u16);
        std.json::value versionless_body = body_json(&versionless);
        expect(&versionless_body, "error.code", "-32020");
        std.http::request old = mcp_post("tools/list", "", "{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"tools/list\",\"params\":{\"_meta\":{\"io.modelcontextprotocol/protocolVersion\":\"2025-11-25\",\"io.modelcontextprotocol/clientCapabilities\":{}}}}");
        old.headers.set("MCP-Protocol-Version", "2025-11-25");
        std.http::response unsupported = await exchange(connection, move old);
        std.test::equal(unsupported.status, 400u16);
        std.json::value unsupported_body = body_json(&unsupported);
        expect(&unsupported_body, "error.code", "-32022");
    }
}

/* A notification, an event stream with progress and the Mcp-Param headers. */
@scoped
protected async void streams_and_mirrors(std.bufio::reader<std.net::tcp_stream>* connection)
    throws std.error::fault, std.test::failure, std.http::http_error, std.json::error {
    task_scope(1) io {
        std.http::request notice = mcp_post("notifications/cancelled", "",
                                          "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/cancelled\",\"params\":{\"requestId\":1}}");
        std.http::response accepted = await exchange(connection, move notice);
        std.test::equal(accepted.status, 202u16);
        std.test::equal(len(accepted.body), 0usize);
        std.http::response streamed = await post_request(connection, "8", "tools/call", "slow", "\"name\":\"slow\"");
        std.test::equal(streamed.status, 200u16);
        expect_header(&streamed, "Content-Type", "text/event-stream");
        array<std.json::value> events = events_of(&streamed);
        std.test::equal(len(events), 3usize);
        expect(&events[0usize], "method", "\"notifications/progress\"");
        expect(&events[2usize], "result.content.0.text", "\"true false true\"");
        std.http::response mirrored = await post_request(connection, "9", "tools/call", "region",
            "\"name\":\"region\",\"arguments\":{\"region\":\"us-west1\",\"limit\":42}");
        std.test::equal(mirrored.status, 400u16);
        std.string::string region_line = request_line("10", "tools/call",
            "\"name\":\"region\",\"arguments\":{\"region\":\"Hello, мир\",\"limit\":42}");
        std.http::request with_headers = mcp_post("tools/call", "region", region_line.as_str());
        with_headers.headers.add("Mcp-Param-Region", "=?base64?SGVsbG8sINC80LjRgA==?=");
        with_headers.headers.add("Mcp-Param-Limit", "42.0");
        std.http::response routed = await exchange(connection, move with_headers);
        std.test::equal(routed.status, 200u16);
        std.json::value routed_body = body_json(&routed);
        expect(&routed_body, "result.content.0.text", "\"routed\"");
    }
}

/* Origins, content types and the methods other than POST. */
@scoped
protected async void guards_the_endpoint(std.bufio::reader<std.net::tcp_stream>* connection)
    throws std.error::fault, std.test::failure, std.http::http_error, std.json::error {
    task_scope(1) io {
        std.string::string origin_line = request_line("11", "tools/list", "");
        std.http::request foreign = mcp_post("tools/list", "", origin_line.as_str());
        foreign.headers.add("Origin", "https://evil.example");
        std.http::response forbidden = await exchange(connection, move foreign);
        std.test::equal(forbidden.status, 403u16);
        std.http::request local = mcp_post("tools/list", "", origin_line.as_str());
        local.headers.add("Origin", "http://localhost:3000");
        std.http::response allowed = await exchange(connection, move local);
        std.test::equal(allowed.status, 200u16);
        std.http::request typed = mcp_post("tools/list", "", origin_line.as_str());
        typed.headers.set("Content-Type", "text/plain");
        std.http::response unsupported_type = await exchange(connection, move typed);
        std.test::equal(unsupported_type.status, 415u16);
        std.http::request fetch = std.http::request::create(std.http::method::get, "/mcp");
        std.http::response refused = await exchange(connection, move fetch);
        std.test::equal(refused.status, 405u16);
        expect_header(&refused, "Allow", "POST");
    }
}

/* The HTTP checks over one connection, split into parts so that no step holds all of their
   locals (the frames of an ASan build are many times the measured ones). */
protected async u32 http_clients(std.net::socket_address endpoint, std.sync::sender<std.service::stop> stopper)
    throws std.error::fault, std.test::failure, std.http::http_error, std.json::error {
    std.net::tcp_stream stream = await endpoint.connect();
    std.bufio::reader<std.net::tcp_stream> connection =
        std.bufio::reader<std.net::tcp_stream>::create(move stream, 65536usize);
    u32 checked = 0u32;
    task_scope(1) io {
        await posts_messages(&connection);
        await checks_versions(&connection);
        await streams_and_mirrors(&connection);
        await guards_the_endpoint(&connection);
        checked += 1u32;
    }
    std.sync::send_result<std.service::stop> sent = std.sync::send(&stopper, std.service::stop::drain);
    drop sent;
    return checked;
}

@test
async void serves_streamable_http() throws std.error::fault, std.test::failure, std.mcp::mcp_error,
                                           std.http::http_error, std.json::error {
    std.net::tcp_listener listener = await open_listener();
    std.net::socket_address endpoint = listener.local_address();
    std.sync::channel<std.service::stop> factory = std.sync::channel::<std.service::stop>();
    std.sync::sender<std.service::stop> stopper = std.sync::sender(&factory);
    std.sync::receiver<std.service::stop> stop = std.sync::receiver(move factory);
    arc std.mcp::server<app> host = new arc std.mcp::server<app>(build_server());
    std.http::router<std.mcp::server<app>> routes = std.http::router<std.mcp::server<app>>::create();
    std.mcp::route(&routes, "/mcp");
    std.service::options settings = {};
    std.http::limits bounds = {};
    task_scope(2) group {
        auto serving = std.http::serve(move listener, settings, move stop, std.arc::clone(&host), move routes, bounds);
        auto driver = http_clients(endpoint, move stopper);
        u32 checked = await move driver;
        std.service::report account = await move serving;
        std.test::equal(checked, 1u32);
        std.test::equal(account.failed, 0u64);
    }
}

/* ---- The client ---- */

struct asker { u32 unused; };

/* Accepts every form with {"ok": true}. */
async std.mcp::answer approve(arc asker state, std.mcp::elicitation request) throws std.error::fault {
    drop state;
    drop request;
    try {
        std.json::value content = std.json::parse("{\"ok\":true}");
        return std.mcp::answer {.action = std.mcp::answer_action::accept, .content = o::some(move content)};
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    return std.mcp::answer {.action = std.mcp::answer_action::cancel, .content = o::none};
}

protected str first_text(const std.mcp::tool_result* result) {
    if (len(result->content) == 0usize) { return "<none>"; }
    switch (result->content[0usize]) {
    case variant std.mcp::content::text(text): return text->as_str();
    default: return "<other>";
    }
}

protected std.json::value arguments(str text) throws std.json::error, std.alloc::alloc_error {
    return std.json::parse(text);
}

protected bool present_json(const (o<std.json::value>)* value) {
    switch (*value) {
    case variant o::some(found):
        found as void;
        return true;
    case variant o::none: return false;
    }
}

/* What every transport of the client does against the test server. */
@scoped
protected async void client_calls(const std.mcp::client* link)
    throws std.error::fault, std.test::failure, std.mcp::mcp_error, std.json::error {
    std.mcp::elicitor<asker> answers = {.state = new arc asker {.unused = 0u32}, .handler = approve};
    task_scope(1) io {
        std.mcp::discovery found = await link->discover();
        std.test::equal(len(found.versions), 1usize);
        std.test::equal_text(found.versions[0usize].as_str(), "2026-07-28");
        std.test::equal_text(found.server.name.as_str(), "tests");
        std.test::equal_text(found.instructions.as_str(), "Test tools.");
        std.test::equal(found.ttl_ms, 60000u64);
        array<std.mcp::tool> tools = await link->list_tools();
        std.test::equal(len(tools), 8usize);
        std.test::equal_text(tools[7usize].name.as_str(), "region");
        std.mcp::tool_result echoed = await link->call_tool("echo", arguments("{\"text\":\"hi\"}"));
        std.test::equal_text(first_text(&echoed), "hi");
        std.mcp::tool_result added = await link->call_tool("sum", arguments("{\"a\":1,\"b\":2}"));
        std.test::check(present_json(&added.structured), "structured content");
        std.mcp::tool_result confirmed = await link->call_tool_with("confirm", std.json::object(), &answers);
        std.test::equal_text(first_text(&confirmed), "confirmed");
        try {
            std.mcp::tool_result refused = await link->call_tool("confirm", std.json::object());
            drop refused;
            std.test::fail("a call that needs input without elicitation");
        } catch (std.mcp::mcp_error rejected) {
            std.test::equal(rejected.code, std.mcp::missing_required_client_capability);
        }
        std.mcp::tool_result routed = await link->call_tool("region", arguments("{\"region\":\"Hello, мир\",\"limit\":42}"));
        std.test::equal_text(first_text(&routed), "routed");
        array<std.mcp::resource> resources = await link->list_resources();
        std.test::equal(len(resources), 1usize);
        array<std.mcp::resource_template> templates = await link->list_templates();
        std.test::equal_text(templates[0usize].uri_template.as_str(), "note://{id}");
        array<std.mcp::contents> read = await link->read_resource("note://42");
        std.test::equal_text(read[0usize].text.as_str(), "note 42");
        array<std.mcp::contents> blobs = await link->read_resource("file:///readme.txt");
        std.test::check(blobs[1usize].binary, "a blob");
        std.test::equal(len(blobs[1usize].blob), 3usize);
        try {
            array<std.mcp::contents> none = await link->read_resource("note://404");
            drop none;
            std.test::fail("a resource that does not exist");
        } catch (std.mcp::mcp_error rejected) {
            std.test::equal(rejected.code, -32602i64);
        }
        array<std.mcp::prompt> prompts = await link->list_prompts();
        std.test::equal_text(prompts[0usize].name.as_str(), "greet");
        std.mcp::prompt_result greeting = await link->get_prompt("greet", arguments("{\"name\":\"Ada\"}"));
        std.test::equal(len(greeting.messages), 1usize);
        std.mcp::completion completed = await link->complete(true, "greet", "name", "B");
        std.test::equal(len(completed.values), 1usize);
        std.test::equal_text(completed.values[0usize].as_str(), "Bob");
    }
}


/* A subscription: its acknowledgement, the changes that touch makes, and its end. */
@scoped
protected async void client_listens(const std.mcp::client* link)
    throws std.error::fault, std.test::failure, std.mcp::mcp_error, std.json::error {
    std.mcp::interests wanted = std.mcp::interests::create();
    wanted.tools = true;
    wanted.prompts = false;
    try {
        wanted.uris.push(std.string::from_str("note://7"));
    } catch (std.array::push_error<std.string::string> rejected) {
        (move rejected) as void;
    }
    o<std.mcp::subscription> opened = o::none;
    task_scope(1) io {
        std.mcp::subscription got = await link->listen(&wanted);
        o<std.mcp::subscription> old = core::replace(&opened, o::some(move got));
        drop old;
    }
    switch (move opened) {
    case variant o::some(move watched):
        std.test::check(watched.granted.tools, "tools granted");
        std.test::check(watched.granted.prompts == false, "prompts not asked");
        std.test::equal(len(watched.granted.uris), 1usize);
        task_scope(1) io {
            std.mcp::tool_result touched = await link->call_tool("touch", std.json::object());
            std.test::equal_text(first_text(&touched), "touched");
            u32 seen = 0u32;
            for (u32 round = 0u32; round < 2u32; round += 1u32) {
                o<std.mcp::change> next = await watched.next();
                switch (move next) {
                case variant o::some(move found):
                    switch (move found) {
                    case variant std.mcp::change::tools: seen += 1u32;
                    case variant std.mcp::change::updated(move uri):
                        std.test::equal_text(uri.as_str(), "note://7");
                        seen += 10u32;
                    default: std.test::fail("an unexpected change");
                    }
                case variant o::none: std.test::fail("the subscription ended");
                }
            }
            std.test::equal(seen, 11u32);
            await link->unlisten(move watched);
        }
    case variant o::none: std.test::fail("no subscription");
    }
}

@test
async void the_client_speaks_over_streams() throws std.error::fault, std.test::failure, std.mcp::mcp_error,
                                                  std.json::error {
    std.net::tcp_listener listener = await open_listener();
    std.net::socket_address endpoint = listener.local_address();
    std.net::tcp_stream requests = await endpoint.connect();
    std.net::tcp_connection server_input = await listener.accept();
    std.net::tcp_stream responses = await endpoint.connect();
    std.net::tcp_connection server_output = await listener.accept();
    arc std.mcp::server<app> host = new arc std.mcp::server<app>(build_server());
    std.mcp::client_options settings = {};
    own dyn(std.stream::Reader)* input = new std.net::tcp_stream(move responses);
    own dyn(std.stream::Writer)* output = new std.net::tcp_stream(move requests);
    std.mcp::client link = std.mcp::client::over_streams(move input, move output,
                                                         std.mcp::implementation::create("test-client", "0.1"), settings);
    task_scope(3) session {
        auto serving = std.mcp::serve_streams(std.arc::clone(&host), move server_input, move server_output);
        auto pump = link.run();
        await client_calls(&link);
        await client_listens(&link);
        await link.close();
        await move serving;
        await move pump;
    }
    await (move listener).close();
}

@test
async void the_client_speaks_over_http() throws std.error::fault, std.test::failure, std.mcp::mcp_error,
                                               std.http::http_error, std.json::error {
    std.net::tcp_listener listener = await open_listener();
    std.net::socket_address endpoint = listener.local_address();
    std.sync::channel<std.service::stop> factory = std.sync::channel::<std.service::stop>();
    std.sync::sender<std.service::stop> stopper = std.sync::sender(&factory);
    std.sync::receiver<std.service::stop> stop = std.sync::receiver(move factory);
    arc std.mcp::server<app> host = new arc std.mcp::server<app>(build_server());
    std.http::router<std.mcp::server<app>> routes = std.http::router<std.mcp::server<app>>::create();
    std.mcp::route(&routes, "/mcp");
    std.service::options service_settings = {};
    std.http::limits bounds = {};
    u16 port = endpoint.port;
    std.string::string address = f"http://127.0.0.1:{port}/mcp";
    std.mcp::client_options settings = {};
    std.mcp::client link = std.mcp::client::over_http(address.as_str(), std.mcp::implementation::create("test-client", "0.1"),
                                                      settings, o::none);
    task_scope(2) group {
        auto serving = std.http::serve(move listener, service_settings, move stop, std.arc::clone(&host), move routes, bounds);
        await client_calls(&link);
        await client_listens(&link);
        std.sync::send_result<std.service::stop> sent = std.sync::send(&stopper, std.service::stop::drain);
        drop sent;
        std.service::report account = await move serving;
        std.test::equal(account.failed, 0u64);
    }
}

/* ---- Authorization ---- */

/* A token "good" allows every method, "weak" every method but tools/call; others are invalid. */
std.mcp::access check_token(const app* state, str token, str method) throws std.alloc::alloc_error {
    state as void;
    if (std.bytes::equal(token, "good") == true) { return std.mcp::access::allowed; }
    if (std.bytes::equal(token, "weak") == true) {
        if (std.bytes::equal(method, "tools/call") == true) { return std.mcp::access::insufficient; }
        return std.mcp::access::allowed;
    }
    return std.mcp::access::invalid;
}

/* Requests without a token, with an invalid one and with one that lacks a scope, each answered
   with its challenge. */
@scoped
protected async void challenges_tokens(std.bufio::reader<std.net::tcp_stream>* connection, u16 port)
    throws std.error::fault, std.test::failure, std.http::http_error, std.json::error {
    std.string::string metadata = f"Bearer resource_metadata=\"http://127.0.0.1:{port}/.well-known/oauth-protected-resource/mcp\", scope=\"tools\"";
    std.string::string invalid = std.string::from_str(metadata.as_str());
    std.string::append_str(&invalid, ", error=\"invalid_token\"");
    std.string::string insufficient = std.string::from_str(metadata.as_str());
    std.string::append_str(&insufficient, ", error=\"insufficient_scope\"");
    std.string::string line = request_line("1", "tools/list", "");
    std.string::string call_line = request_line("2", "tools/call", "\"name\":\"echo\",\"arguments\":{\"text\":\"x\"}");
    task_scope(1) io {
        std.http::response missing = await exchange(connection, mcp_post("tools/list", "", line.as_str()));
        std.test::equal(missing.status, 401u16);
        expect_header(&missing, "WWW-Authenticate", metadata.as_str());
        std.http::request bad = mcp_post("tools/list", "", line.as_str());
        bad.headers.add("Authorization", "Bearer bad");
        std.http::response rejected = await exchange(connection, move bad);
        std.test::equal(rejected.status, 401u16);
        expect_header(&rejected, "WWW-Authenticate", invalid.as_str());
        std.http::request weak_list = mcp_post("tools/list", "", line.as_str());
        weak_list.headers.add("Authorization", "Bearer weak");
        std.http::response listed = await exchange(connection, move weak_list);
        std.test::equal(listed.status, 200u16);
        std.http::request weak_call = mcp_post("tools/call", "echo", call_line.as_str());
        weak_call.headers.add("Authorization", "bearer weak");
        std.http::response denied = await exchange(connection, move weak_call);
        std.test::equal(denied.status, 403u16);
        expect_header(&denied, "WWW-Authenticate", insufficient.as_str());
    }
}

/* The Protected Resource Metadata of the server. */
@scoped
protected async void describes_the_resource(std.bufio::reader<std.net::tcp_stream>* connection, u16 port)
    throws std.error::fault, std.test::failure, std.http::http_error, std.json::error {
    task_scope(1) io {
        std.http::request described = std.http::request::create(std.http::method::get,
                                                                "/.well-known/oauth-protected-resource/mcp");
        std.http::response document = await exchange(connection, move described);
        std.test::equal(document.status, 200u16);
        std.json::value found = body_json(&document);
        std.string::string resource = f"\"http://127.0.0.1:{port}/mcp\"";
        expect(&found, "resource", resource.as_str());
        expect(&found, "authorization_servers", "[\"https://auth.example\"]");
        expect(&found, "scopes_supported", "[\"tools\"]");
        expect(&found, "bearer_methods_supported", "[\"header\"]");
    }
}

/* Clients of std.mcp without a token and with one. */
protected async void clients_with_tokens(std.string::string address)
    throws std.error::fault, std.test::failure, std.json::error, std.mcp::mcp_error {
    std.mcp::client_options settings = {};
    std.mcp::client anonymous = std.mcp::client::over_http(address.as_str(),
                                                           std.mcp::implementation::create("anonymous", "0.1"),
                                                           settings, o::none);
    std.mcp::client trusted = std.mcp::client::over_http(address.as_str(),
                                                         std.mcp::implementation::create("trusted", "0.1"),
                                                         settings, o::none);
    trusted.set_token("good");
    task_scope(1) io {
        try {
            std.mcp::discovery none = await anonymous.discover();
            drop none;
            std.test::fail("a request without a token");
        } catch (std.mcp::mcp_error failure) {
            std.test::equal(failure.code, std.mcp::unauthorized);
        }
        std.mcp::tool_result echoed = await trusted.call_tool("echo", arguments("{\"text\":\"with a token\"}"));
        std.test::equal_text(first_text(&echoed), "with a token");
    }
}

/* The checks of a protected endpoint, split into parts so that no step holds all of their
   locals (the frames of an ASan build are many times the measured ones). */
protected async u32 guarded_clients(std.net::socket_address endpoint, std.string::string address,
                                    std.sync::sender<std.service::stop> stopper)
    throws std.error::fault, std.test::failure, std.http::http_error, std.json::error, std.mcp::mcp_error {
    std.net::tcp_stream stream = await endpoint.connect();
    std.bufio::reader<std.net::tcp_stream> connection =
        std.bufio::reader<std.net::tcp_stream>::create(move stream, 65536usize);
    u16 port = endpoint.port;
    u32 checked = 0u32;
    task_scope(1) io {
        await challenges_tokens(&connection, port);
        await describes_the_resource(&connection, port);
        await clients_with_tokens(move address);
        checked += 1u32;
    }
    std.sync::send_result<std.service::stop> sent = std.sync::send(&stopper, std.service::stop::drain);
    drop sent;
    return checked;
}

@test
async void protects_the_http_endpoint() throws std.error::fault, std.test::failure, std.mcp::mcp_error,
                                                std.http::http_error, std.json::error {
    std.net::tcp_listener listener = await open_listener();
    std.net::socket_address endpoint = listener.local_address();
    std.sync::channel<std.service::stop> factory = std.sync::channel::<std.service::stop>();
    std.sync::sender<std.service::stop> stopper = std.sync::sender(&factory);
    std.sync::receiver<std.service::stop> stop = std.sync::receiver(move factory);
    u16 port = endpoint.port;
    std.string::string address = f"http://127.0.0.1:{port}/mcp";
    std.mcp::server<app> built = build_server();
    std.mcp::protection guard = std.mcp::protection::create(address.as_str(), "https://auth.example");
    guard.scope("tools");
    built.protect(move guard, check_token);
    arc std.mcp::server<app> host = new arc std.mcp::server<app>(move built);
    std.http::router<std.mcp::server<app>> routes = std.http::router<std.mcp::server<app>>::create();
    std.mcp::route(&routes, "/mcp");
    std.service::options service_settings = {};
    std.http::limits bounds = {};
    task_scope(2) group {
        auto serving = std.http::serve(move listener, service_settings, move stop, std.arc::clone(&host), move routes, bounds);
        auto driver = guarded_clients(endpoint, std.string::from_str(address.as_str()), move stopper);
        u32 checked = await move driver;
        std.service::report account = await move serving;
        std.test::equal(checked, 1u32);
        std.test::equal(account.failed, 0u64);
    }
}

// The content blocks of a tool result have their own converters, so the schema of a result uses
// the json_schema hook of std.mcp::content (R-SLIB-JSON-0002) for them.
@test
void derives_the_schema_of_results() throws std.test::failure, std.json::error, std.alloc::alloc_error {
    std.json::value schema = std.json::schema::<std.mcp::tool_result>();
    std.string::string text = std.json::stringify(&schema);
    std.test::check(std.text::contains(text.as_str(), "\"$defs\":{\"std.mcp__content\":{\"oneOf\""), text.as_str());
    std.test::check(std.text::contains(text.as_str(), "\"items\":{\"$ref\":\"#/$defs/std.mcp__content\"}"), text.as_str());
}

/* ---- Tasks (R-SLIB-MCP-0021..0024) ---- */

/* A tool in two rounds: it reports progress and asks whether to proceed, then completes. */
async std.mcp::tool_outcome tally(arc app shared, std.mcp::call request) throws std.error::fault {
    drop shared;
    switch (request.input.get("proceed")) {
    case variant o::some(given):
        if (std.bytes::equal(request.input.state_text(), "half") == false) {
            return std.mcp::tool_outcome::complete(std.mcp::tool_result::of_error("lost state"));
        }
        if ((*given)->action == std.mcp::answer_action::accept) {
            return std.mcp::tool_outcome::complete(std.mcp::tool_result::of_text("tallied"));
        }
        return std.mcp::tool_outcome::complete(std.mcp::tool_result::of_text("stopped"));
    case variant o::none: break;
    }
    bool noted = request.progress.report(1.0f64, o::some(2.0f64), "halfway");
    noted as void;
    std.mcp::input_required asked = std.mcp::input_required::create();
    try {
        asked.ask_form("proceed", "Proceed?", std.json::schema::<confirm_form>());
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    asked.set_state("half");
    return std.mcp::tool_outcome::needs_input(move asked);
}

async std.mcp::tool_outcome crash(arc app shared, std.mcp::call request) throws std.error::fault {
    drop shared;
    drop request;
    throw std.io::io_error {.code = std.io::error_code::closed, .native_code = 0i64};
}

protected std.mcp::server<app> build_task_server() throws std.test::failure, std.mcp::mcp_error, std.json::error,
                                                           std.alloc::alloc_error {
    std.mcp::notifier changes = std.mcp::notifier::create();
    arc app shared = new arc app {.changes = changes.share()};
    std.mcp::options settings = {.task_poll_ms = 20u64};
    std.mcp::server<app> host = std.mcp::server<app>::create(std.mcp::implementation::create("tasks", "1.0"),
                                                             move shared, move changes, settings);
    host.add_tool(std.mcp::tool::create("tally", "Counts in two rounds", std.mcp::no_arguments()), tally);
    host.add_tool(std.mcp::tool::create("crash", "Fails", std.mcp::no_arguments()), crash);
    host.add_tool(std.mcp::tool::create("wait", "Waits long", std.mcp::no_arguments()), wait);
    host.add_tool(std.mcp::tool::create("strict", "Runs only as a task", std.mcp::no_arguments()), tally);
    host.run_as_task("tally", false);
    host.run_as_task("crash", false);
    host.run_as_task("wait", false);
    host.run_as_task("strict", true);
    return move host;
}

/* A request line of a client that declares the Tasks extension. */
protected std.string::string task_line(str id, str method, str params) throws std.alloc::alloc_error {
    std.string::string line = f"{{\"jsonrpc\":\"2.0\",\"id\":{id},\"method\":\"{method}\",\"params\":{{";
    std.string::append_str(&line, params);
    const u8[] more = params;
    if (len(more) != 0usize) { std.string::append_str(&line, ","); }
    std.string::append_str(&line, "\"_meta\":{\"io.modelcontextprotocol/protocolVersion\":\"2026-07-28\",");
    std.string::append_str(&line, "\"io.modelcontextprotocol/clientCapabilities\":{\"elicitation\":{\"form\":{}},");
    std.string::append_str(&line, "\"extensions\":{\"io.modelcontextprotocol/tasks\":{}}}}}}\n");
    return move line;
}

@scoped
protected async std.json::value task_call(const std.net::tcp_stream* peer, std.bufio::reader<std.net::tcp_stream>* replies,
                                          str id, str method, str params)
    throws std.error::fault, std.test::failure, std.json::error {
    std.string::string line = task_line(id, method, params);
    task_scope(1) io {
        await send_text(peer, line.as_str());
        return await receive(replies);
    }
}

/* tasks/get until the task has the status, at most a hundred times. */
@scoped
protected async std.json::value polled(const std.net::tcp_stream* peer, std.bufio::reader<std.net::tcp_stream>* replies,
                                       str id, str status)
    throws std.error::fault, std.test::failure, std.json::error {
    std.string::string params = f"\"taskId\":{id}";
    std.string::string wanted = f"\"{status}\"";
    for (u32 round = 0u32; round < 100u32; round += 1u32) {
        std.json::value state = std.json::null();
        task_scope(1) io {
            std.json::value got = await task_call(peer, replies, "90", "tasks/get", params.as_str());
            std.json::value old = core::replace(&state, move got);
            drop old;
        }
        std.string::string seen = path_text(&state, "result.status");
        if (std.bytes::equal(seen.as_bytes(), wanted.as_bytes()) == true) { return move state; }
        drop state;
        await std.time::sleep_for(std.time::duration_from_parts(0i64, 20000000u32));
    }
    drop params;
    drop wanted;
    std.test::fail("the task did not reach the status");
    return std.json::null();
}

/* The negotiation: discover offers the extension, a client that does not declare it gets no
   task and -32021 for the methods of tasks and for a tool that runs only as a task. */
@scoped
protected async void negotiates_tasks(const std.net::tcp_stream* peer, std.bufio::reader<std.net::tcp_stream>* replies)
    throws std.error::fault, std.test::failure, std.json::error {
    task_scope(1) io {
        std.json::value found = await call(peer, replies, "80", "server/discover", "");
        expect(&found, "result.capabilities.extensions", "{\"io.modelcontextprotocol/tasks\":{}}");
        std.json::value strict = await call(peer, replies, "81", "tools/call", "\"name\":\"strict\"");
        expect(&strict, "error.code", "-32021");
        expect(&strict, "error.data", "{\"requiredCapabilities\":{\"extensions\":{\"io.modelcontextprotocol/tasks\":{}}}}");
        std.json::value progress = await call(peer, replies, "82", "tools/call", "\"name\":\"tally\"");
        expect(&progress, "params", "{\"progressToken\":\"p1\",\"progress\":1,\"total\":2,\"message\":\"halfway\"}");
        std.json::value plain = await receive(replies);
        expect(&plain, "result.resultType", "\"input_required\"");
        std.json::value get = await call(peer, replies, "83", "tasks/get", "\"taskId\":\"x\"");
        expect(&get, "error.code", "-32021");
        std.json::value listen = await call(peer, replies, "84", "subscriptions/listen", "\"notifications\":{\"taskIds\":[\"x\"]}");
        expect(&listen, "error.code", "-32021");
        std.json::value unknown = await task_call(peer, replies, "85", "tasks/cancel", "\"taskId\":\"x\"");
        expect(&unknown, "error.code", "-32602");
        expect(&unknown, "error.message", "\"Failed to retrieve task: Task not found\"");
        std.json::value nameless = await task_call(peer, replies, "86", "tasks/get", "");
        expect(&nameless, "error.message", "\"Invalid params: taskId\"");
    }
}

/* A task in its states: created, waiting for input with the key of its round, malformed and
   accepted answers, completed with the result of its tool. */
@scoped
protected async void runs_a_task(const std.net::tcp_stream* peer, std.bufio::reader<std.net::tcp_stream>* replies)
    throws std.error::fault, std.test::failure, std.json::error {
    std.string::string id = std.string::create();
    task_scope(1) io {
        std.json::value created = await task_call(peer, replies, "87", "tools/call", "\"name\":\"tally\"");
        expect(&created, "result.resultType", "\"task\"");
        expect(&created, "result.status", "\"working\"");
        expect(&created, "result.ttlMs", "3600000");
        expect(&created, "result.pollIntervalMs", "20");
        std.string::string stamp = path_text(&created, "result.createdAt");
        std.test::check(std.text::ends_with(stamp.as_str(), "Z\""), stamp.as_str());
        std.string::string found = path_text(&created, "result.taskId");
        std.string::string old = core::replace(&id, move found);
        drop old;
    }
    std.test::equal(std.string::len(&id), 38usize);
    std.string::string bad = f"\"taskId\":{id},\"inputResponses\":[]";
    std.string::string good = f"\"taskId\":{id},\"inputResponses\":{{\"1.proceed\":{{\"action\":\"accept\",\"content\":{{\"ok\":true}}}}}}";
    task_scope(1) io {
        std.json::value asking = await polled(peer, replies, id.as_str(), "input_required");
        expect(&asking, "result.resultType", "\"complete\"");
        expect(&asking, "result.statusMessage", "\"halfway\"");
        std.string::string requests = path_text(&asking, "result.inputRequests");
        std.test::check(std.text::starts_with(requests.as_str(), "{\"1.proceed\":{\"method\":\"elicitation/create\""), requests.as_str());
        std.json::value refused = await task_call(peer, replies, "88", "tasks/update", bad.as_str());
        expect(&refused, "error.code", "-32602");
        std.json::value taken = await task_call(peer, replies, "89", "tasks/update", good.as_str());
        expect(&taken, "result.resultType", "\"complete\"");
        std.json::value done = await polled(peer, replies, id.as_str(), "completed");
        expect(&done, "result.result.content.0.text", "\"tallied\"");
        expect(&done, "result.inputRequests", "<missing>");
    }
}

@test
async void serves_tasks_over_stdio_framing() throws std.error::fault, std.test::failure, std.mcp::mcp_error,
                                                    std.json::error {
    std.net::tcp_listener listener = await open_listener();
    std.net::socket_address endpoint = listener.local_address();
    std.net::tcp_stream requests = await endpoint.connect();
    std.net::tcp_connection server_input = await listener.accept();
    std.net::tcp_stream responses = await endpoint.connect();
    std.net::tcp_connection server_output = await listener.accept();
    arc std.mcp::server<app> host = new arc std.mcp::server<app>(build_task_server());
    std.sync::channel<std.service::stop> factory = std.sync::channel::<std.service::stop>();
    std.sync::sender<std.service::stop> stopper = std.sync::sender(&factory);
    std.sync::receiver<std.service::stop> stop = std.sync::receiver(move factory);
    std.bufio::reader<std.net::tcp_stream> replies =
        std.bufio::reader<std.net::tcp_stream>::create(move responses, 65536usize);
    task_scope(3) session {
        auto runner = std.mcp::run_tasks(std.arc::clone(&host), move stop);
        auto serving = std.mcp::serve_streams(std.arc::clone(&host), move server_input, move server_output);
        await negotiates_tasks(&requests, &replies);
        await runs_a_task(&requests, &replies);
        await std.net::tcp_shutdown(&requests, std.net::shutdown_direction::write);
        await move serving;
        std.sync::send_result<std.service::stop> sent = std.sync::send(&stopper, std.service::stop::drain);
        drop sent;
        await move runner;
    }
    await (move requests).close();
    await (move listener).close();
}

/* The calls of a client that declares the Tasks extension: a task followed to its end with
   input, a task that fails, a task that does not exist. */
@scoped
protected async void client_tasks(const std.mcp::client* link)
    throws std.error::fault, std.test::failure, std.mcp::mcp_error, std.json::error {
    std.mcp::elicitor<asker> answers = {.state = new arc asker {.unused = 0u32}, .handler = approve};
    task_scope(1) io {
        std.mcp::tool_result tallied = await link->call_tool_with("tally", std.json::object(), &answers);
        std.test::equal_text(first_text(&tallied), "tallied");
        try {
            std.mcp::tool_result crashed = await link->call_tool("crash", std.json::object());
            drop crashed;
            std.test::fail("a task that failed");
        } catch (std.mcp::mcp_error rejected) {
            std.test::equal(rejected.code, -32603i64);
        }
        try {
            std.mcp::task_state missing = await link->get_task("no-such-task");
            drop missing;
            std.test::fail("a task that does not exist");
        } catch (std.mcp::mcp_error rejected) {
            std.test::equal(rejected.code, -32602i64);
        }
    }
}

/* The id of a task that start_tool started. */
protected std.string::string started_id(std.mcp::tool_start started) throws std.test::failure, std.alloc::alloc_error {
    switch (move started) {
    case variant std.mcp::tool_start::running(move state):
        std.test::check(state.status == std.mcp::task_status::working, "a new task works");
        return core::replace(&state.id, std.string::create());
    case variant std.mcp::tool_start::finished(move result):
        drop result;
        std.test::fail("the tool ran at once");
    }
    return std.string::create();
}

/* get_task until the task waits for input, at most a hundred times. */
@scoped
protected async std.mcp::task_state input_state(const std.mcp::client* link, str id)
    throws std.error::fault, std.test::failure, std.mcp::mcp_error {
    for (u32 round = 0u32; round < 100u32; round += 1u32) {
        o<std.mcp::task_state> found = o::none;
        task_scope(1) io {
            std.mcp::task_state state = await link->get_task(id);
            if (state.status == std.mcp::task_status::input_required) {
                o<std.mcp::task_state> old = core::replace(&found, o::some(move state));
                drop old;
            } else {
                drop state;
            }
        }
        switch (move found) {
        case variant o::some(move state): return move state;
        case variant o::none: break;
        }
        await std.time::sleep_for(std.time::duration_from_parts(0i64, 20000000u32));
    }
    std.test::fail("the task did not ask for input");
    return std.mcp::task_state {.id = std.string::create(), .status = std.mcp::task_status::failed,
                                .message = std.string::create(), .poll_ms = 0u64,
                                .keys = std.array::create::<std.string::string>(),
                                .requests = std.array::create::<std.mcp::elicitation>(), .result = o::none,
                                .error = o::none};
}

/* Reads the changes of a subscription to a task until it completes: the text of its result. */
@scoped
protected async std.string::string completion_of(std.mcp::subscription* watched)
    throws std.error::fault, std.test::failure, std.mcp::mcp_error {
    for (u32 round = 0u32; round < 8u32; round += 1u32) {
        o<std.mcp::change> next = o::none;
        task_scope(1) io {
            o<std.mcp::change> got = await watched->next();
            o<std.mcp::change> old = core::replace(&next, move got);
            drop old;
        }
        switch (move next) {
        case variant o::some(move found):
            switch (move found) {
            case variant std.mcp::change::state(move state):
                if (state.status == std.mcp::task_status::completed) {
                    switch (state.result) {
                    case variant o::some(result): return std.string::from_str(first_text(result));
                    case variant o::none: std.test::fail("a completed task without its result");
                    }
                }
            default: std.test::fail("an unexpected change");
            }
        case variant o::none: std.test::fail("the subscription ended");
        }
    }
    std.test::fail("the task did not complete");
    return std.string::create();
}

/* A task started without waiting, its input request, its states through a subscription after
   the answer. */
@scoped
protected async void client_watches_a_task(const std.mcp::client* link)
    throws std.error::fault, std.test::failure, std.mcp::mcp_error, std.json::error {
    std.mcp::elicitor<asker> answers = {.state = new arc asker {.unused = 0u32}, .handler = approve};
    std.string::string id = std.string::create();
    task_scope(1) io {
        std.mcp::tool_start started = await link->start_tool("tally", std.json::object(), &answers);
        std.string::string found = started_id(move started);
        std.string::string old = core::replace(&id, move found);
        drop old;
    }
    std.test::equal(std.string::len(&id), 36usize);
    std.mcp::interests wanted = std.mcp::interests::create();
    try {
        wanted.tasks.push(std.string::from_str(id.as_str()));
    } catch (std.array::push_error<std.string::string> rejected) {
        (move rejected) as void;
    }
    std.mcp::answer yes = {.action = std.mcp::answer_action::accept, .content = o::some(std.json::parse("{\"ok\":true}"))};
    o<std.mcp::subscription> opened = o::none;
    task_scope(1) io {
        std.mcp::task_state asking = await input_state(link, id.as_str());
        std.test::equal_text(asking.message.as_str(), "halfway");
        std.test::equal(len(asking.keys), 1usize);
        std.test::equal_text(asking.keys[0usize].as_str(), "1.proceed");
        std.mcp::subscription got = await link->listen(&wanted);
        o<std.mcp::subscription> old = core::replace(&opened, o::some(move got));
        drop old;
    }
    switch (move opened) {
    case variant o::some(move watched):
        std.test::equal(len(watched.granted.tasks), 1usize);
        task_scope(1) io {
            await link->answer_task(id.as_str(), "1.proceed", &yes);
            std.string::string text = await completion_of(&watched);
            std.test::equal_text(text.as_str(), "tallied");
            await link->unlisten(move watched);
        }
    case variant o::none: std.test::fail("no subscription");
    }
}

/* A task cancelled while it runs. */
@scoped
protected async void client_cancels_a_task(const std.mcp::client* link)
    throws std.error::fault, std.test::failure, std.mcp::mcp_error, std.json::error {
    std.mcp::elicitor<asker> answers = {.state = new arc asker {.unused = 0u32}, .handler = approve};
    std.string::string id = std.string::create();
    task_scope(1) io {
        std.mcp::tool_start started = await link->start_tool("wait", std.json::object(), &answers);
        std.string::string found = started_id(move started);
        std.string::string old = core::replace(&id, move found);
        drop old;
    }
    task_scope(1) io {
        await link->cancel_task(id.as_str());
        try {
            std.mcp::tool_result ended = await link->finish_task(id.as_str(), &answers);
            drop ended;
            std.test::fail("a cancelled task");
        } catch (std.mcp::mcp_error rejected) {
            std.test::equal(rejected.code, std.mcp::task_cancelled);
        }
    }
}

@test
async void the_client_follows_tasks() throws std.error::fault, std.test::failure, std.mcp::mcp_error, std.json::error {
    std.net::tcp_listener listener = await open_listener();
    std.net::socket_address endpoint = listener.local_address();
    std.net::tcp_stream requests = await endpoint.connect();
    std.net::tcp_connection server_input = await listener.accept();
    std.net::tcp_stream responses = await endpoint.connect();
    std.net::tcp_connection server_output = await listener.accept();
    arc std.mcp::server<app> host = new arc std.mcp::server<app>(build_task_server());
    std.sync::channel<std.service::stop> factory = std.sync::channel::<std.service::stop>();
    std.sync::sender<std.service::stop> stopper = std.sync::sender(&factory);
    std.sync::receiver<std.service::stop> stop = std.sync::receiver(move factory);
    std.mcp::client_options settings = {.tasks = true};
    own dyn(std.stream::Reader)* input = new std.net::tcp_stream(move responses);
    own dyn(std.stream::Writer)* output = new std.net::tcp_stream(move requests);
    std.mcp::client link = std.mcp::client::over_streams(move input, move output,
                                                         std.mcp::implementation::create("test-client", "0.1"), settings);
    task_scope(4) session {
        auto runner = std.mcp::run_tasks(std.arc::clone(&host), move stop);
        auto serving = std.mcp::serve_streams(std.arc::clone(&host), move server_input, move server_output);
        auto pump = link.run();
        await client_tasks(&link);
        await client_watches_a_task(&link);
        await client_cancels_a_task(&link);
        await link.close();
        await move serving;
        std.sync::send_result<std.service::stop> sent = std.sync::send(&stopper, std.service::stop::cancel);
        drop sent;
        await move runner;
        await move pump;
    }
    await (move listener).close();
}

@test
async void the_client_follows_tasks_over_http() throws std.error::fault, std.test::failure, std.mcp::mcp_error,
                                                       std.http::http_error, std.json::error {
    std.net::tcp_listener listener = await open_listener();
    std.net::socket_address endpoint = listener.local_address();
    std.sync::channel<std.service::stop> factory = std.sync::channel::<std.service::stop>();
    std.sync::sender<std.service::stop> stopper = std.sync::sender(&factory);
    std.sync::receiver<std.service::stop> stop = std.sync::receiver(move factory);
    std.sync::channel<std.service::stop> task_factory = std.sync::channel::<std.service::stop>();
    std.sync::sender<std.service::stop> task_stopper = std.sync::sender(&task_factory);
    std.sync::receiver<std.service::stop> task_stop = std.sync::receiver(move task_factory);
    arc std.mcp::server<app> host = new arc std.mcp::server<app>(build_task_server());
    std.http::router<std.mcp::server<app>> routes = std.http::router<std.mcp::server<app>>::create();
    std.mcp::route(&routes, "/mcp");
    std.service::options service_settings = {};
    std.http::limits bounds = {};
    u16 port = endpoint.port;
    std.string::string address = f"http://127.0.0.1:{port}/mcp";
    std.mcp::client_options settings = {.tasks = true};
    std.mcp::client link = std.mcp::client::over_http(address.as_str(), std.mcp::implementation::create("test-client", "0.1"),
                                                      settings, o::none);
    task_scope(3) group {
        auto runner = std.mcp::run_tasks(std.arc::clone(&host), move task_stop);
        auto serving = std.http::serve(move listener, service_settings, move stop, std.arc::clone(&host), move routes, bounds);
        await client_tasks(&link);
        await client_watches_a_task(&link);
        std.sync::send_result<std.service::stop> sent = std.sync::send(&stopper, std.service::stop::drain);
        drop sent;
        std.service::report account = await move serving;
        std.test::equal(account.failed, 0u64);
        std.sync::send_result<std.service::stop> ended = std.sync::send(&task_stopper, std.service::stop::cancel);
        drop ended;
        await move runner;
    }
}

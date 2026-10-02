module example.assistant.main;
import std.console;
import std.http;
import std.net;
import std.service;
import std.mcp;
import example.assistant.memory;
import example.assistant.demo;
import example.assistant.frames;

protected str usage_text() {
    return "assistant demo\nassistant serve\nassistant serve-http PORT\nassistant replay FILE\n"
           "assistant schema remember|recall|forget\nassistant inspect URL [TOOL JSON]\n"
           "assistant inspect-stdio PROGRAM [ARGUMENT...] [-- TOOL JSON]\nassistant frames\n";
}

/* Serves the memory server on standard input and output until the input ends; the runner of its
   tasks runs next to it, and the tasks that remain at the end are cancelled. */
protected async i32 serve() throws std.error::fault {
    try {
        arc std.mcp::server<example.assistant.memory::memory> host =
            new arc std.mcp::server<example.assistant.memory::memory>(example.assistant.memory::build());
        std.sync::channel<std.service::stop> factory = std.sync::channel::<std.service::stop>();
        std.sync::sender<std.service::stop> stopper = std.sync::sender(&factory);
        std.sync::receiver<std.service::stop> stop = std.sync::receiver(move factory);
        task_scope(2) group {
            auto runner = std.mcp::run_tasks(std.arc::clone(&host), move stop);
            await std.mcp::serve_stdio(move host);
            std.sync::send_result<std.service::stop> sent = std.sync::send(&stopper, std.service::stop::cancel);
            drop sent;
            await move runner;
        }
        return 0;
    } catch (std.mcp::mcp_error failure) {
        (move failure) as void;
    } catch (std.json::error failure) {
        (move failure) as void;
    }
    return 1;
}

/* Serves the requests recorded in a file, one JSON-RPC message per line, and writes the responses
   to standard output as the server finishes them; it returns when every request has finished. */
protected async i32 replay(std.string::string file) throws std.error::fault {
    std.fs::open_file_options reading = std.fs::open_file_options {.access = std.fs::access::read,
        .create = std.fs::create_mode::existing, .truncate = false, .append = false,
        .follow_final_symlink = false};
    try {
        arc std.mcp::server<example.assistant.memory::memory> host =
            new arc std.mcp::server<example.assistant.memory::memory>(example.assistant.memory::build());
        std.fs::path path = std.fs::path_from_utf8(file.as_str());
        std.fs::file recorded = await path.open_file(reading);
        await std.mcp::serve_streams(move host, move recorded, std.io::stdout());
        return 0;
    } catch (std.mcp::mcp_error failure) {
        (move failure) as void;
    } catch (std.json::error failure) {
        (move failure) as void;
    }
    return 1;
}

/* Serves the memory server over Streamable HTTP at http://127.0.0.1:PORT/mcp, with the runner of
   its tasks, until the process ends. */
protected async i32 serve_http(u16 port) throws std.error::fault {
    try {
        std.net::socket_address local = {.address = std.net::parse_ip("127.0.0.1"), .port = port, .scope_id = 0u32};
        std.net::listen_options options = {.backlog = 16u32, .reuse_address = true, .v6_only = false};
        std.net::tcp_listener listener = await local.listen(options);
        arc std.mcp::server<example.assistant.memory::memory> host =
            new arc std.mcp::server<example.assistant.memory::memory>(example.assistant.memory::build());
        std.http::router<std.mcp::server<example.assistant.memory::memory>> routes =
            std.http::router<std.mcp::server<example.assistant.memory::memory>>::create();
        std.mcp::route(&routes, "/mcp");
        std.sync::channel<std.service::stop> factory = std.sync::channel::<std.service::stop>();
        std.sync::sender<std.service::stop> stopper = std.sync::sender(&factory);
        std.sync::receiver<std.service::stop> stop = std.sync::receiver(move factory);
        std.sync::channel<std.service::stop> task_factory = std.sync::channel::<std.service::stop>();
        std.sync::sender<std.service::stop> task_stopper = std.sync::sender(&task_factory);
        std.sync::receiver<std.service::stop> task_stop = std.sync::receiver(move task_factory);
        std.service::options settings = {};
        std.http::limits bounds = {};
        std.string::string announced = std.string::from_str("serving MCP ");
        std.string::append_str(&announced, std.mcp::protocol_version);
        std.string::string where = f" at http://127.0.0.1:{port}/mcp";
        std.string::append_str(&announced, where.as_str());
        await std.console::eprintln(move announced);
        task_scope(2) group {
            auto runner = std.mcp::run_tasks(std.arc::clone(&host), move task_stop);
            std.service::report account = await std.http::serve(move listener, settings, move stop, move host,
                                                                move routes, bounds);
            account as void;
            std.sync::send_result<std.service::stop> sent = std.sync::send(&task_stopper, std.service::stop::cancel);
            drop sent;
            await move runner;
        }
        drop stopper;
        return 0;
    } catch (std.mcp::mcp_error failure) {
        (move failure) as void;
    } catch (std.json::error failure) {
        (move failure) as void;
    } catch (std.http::http_error failure) {
        failure as void;
    }
    return 1;
}

/* Prints the JSON Schema of the arguments of a tool. */
protected async i32 schema(std.string::string tool) throws std.error::fault {
    try {
        std.json::value found = std.json::null();
        if (std.bytes::equal(tool.as_bytes(), "remember") == true) {
            std.json::value derived = std.json::schema::<example.assistant.memory::remember_args>();
            std.json::value old = core::replace(&found, move derived);
            drop old;
        }
        if (std.bytes::equal(tool.as_bytes(), "recall") == true) {
            std.json::value derived = std.json::schema::<example.assistant.memory::recall_args>();
            std.json::value old = core::replace(&found, move derived);
            drop old;
        }
        if (std.bytes::equal(tool.as_bytes(), "forget") == true) {
            std.json::value derived = std.json::schema::<example.assistant.memory::forget_args>();
            std.json::value old = core::replace(&found, move derived);
            drop old;
        }
        await std.console::println(std.json::stringify(&found));
        return 0;
    } catch (std.json::error failure) {
        (move failure) as void;
    }
    return 1;
}

/* What inspect does: the URL or the command line of a server, and the tool to call with its
   JSON arguments when TOOL is not empty. */
struct inspection {
    bool over_http;
    std.string::string address;
    array<std.string::string> command_line;
    std.string::string tool;
    std.string::string given;
};

protected inspection inspection_of(const array<std.string::string>* arguments) throws std.alloc::alloc_error {
    bool over_http = std.bytes::equal((*arguments)[1usize].as_bytes(), "inspect");
    usize end = len(*arguments);
    usize tool_at = 0usize;
    for (usize index = 2usize; index < len(*arguments); index += 1usize) {
        if (std.bytes::equal((*arguments)[index].as_bytes(), "--") == true && index + 2usize < len(*arguments)) {
            end = index;
            tool_at = index + 1usize;
            break;
        }
    }
    if (over_http == true && len(*arguments) == 5usize) {
        end = 3usize;
        tool_at = 3usize;
    }
    inspection plan = {.over_http = over_http, .address = std.string::from_str((*arguments)[2usize].as_str()),
                       .command_line = std.array::create::<std.string::string>(), .tool = std.string::create(),
                       .given = std.string::from_str("{}")};
    if (tool_at != 0usize) {
        std.string::append_str(&plan.tool, (*arguments)[tool_at].as_str());
        std.string::clear(&plan.given);
        std.string::append_str(&plan.given, (*arguments)[tool_at + 1usize].as_str());
    }
    for (usize index = 2usize; index < end; index += 1usize) {
        try {
            plan.command_line.push(std.string::from_str((*arguments)[index].as_str()));
        } catch (std.array::push_error<std.string::string> rejected) {
            (move rejected) as void;
            throw std.alloc::alloc_error::out_of_memory;
        }
    }
    return move plan;
}

protected async void run_inspection(inspection plan) throws std.error::fault, std.mcp::mcp_error, std.json::error {
    if (plan.over_http == true) {
        await example.assistant.demo::inspect_http(core::replace(&plan.address, std.string::create()),
                                                   core::replace(&plan.tool, std.string::create()),
                                                   core::replace(&plan.given, std.string::create()));
    } else {
        await example.assistant.demo::inspect_stdio(core::replace(&plan.command_line, std.array::create::<std.string::string>()),
                                                    core::replace(&plan.tool, std.string::create()),
                                                    core::replace(&plan.given, std.string::create()));
    }
    drop plan;
}

/* Inspects the server at a URL or started from a command line; a call of one tool follows when
   TOOL and JSON are given. */
protected async i32 inspect(array<std.string::string> arguments) throws std.error::fault {
    inspection plan = inspection_of(&arguments);
    drop arguments;
    try {
        await run_inspection(move plan);
        return 0;
    } catch (std.mcp::mcp_error failure) {
        await std.console::eprintln(example.assistant.frames::problem_line(&failure));
    } catch (std.json::error failure) {
        (move failure) as void;
        await std.console::eprintln(std.string::from_str("json error"));
    }
    return 1;
}

/* Runs the conversation over HTTP, then over stdio with this program as the server. */
protected async i32 demo(std.string::string program) throws std.error::fault {
    try {
        await example.assistant.demo::over_http();
        return await example.assistant.demo::over_stdio(move program);
    } catch (std.mcp::mcp_error failure) {
        await std.console::eprintln(example.assistant.frames::problem_line(&failure));
    } catch (std.json::error failure) {
        (move failure) as void;
        await std.console::eprintln(std.string::from_str("json error"));
    }
    return 1;
}

protected o<u16> port_of(str text) {
    try {
        u16 port = std.convert::parse_u16(text, 10u32);
        if (port != 0u16) { return o::some(port); }
    } catch (std.convert::parse_error rejected) {
        rejected as void;
    }
    return o::none;
}

async i32 main() {
    array<std.string::string> arguments = std.env::arguments();
    usize given = len(arguments);
    str command = "";
    if (given >= 2usize) { command = arguments[1].as_str(); }
    bool demo_call = std.bytes::equal(command, "demo") == true && given == 2usize;
    bool serve_call = std.bytes::equal(command, "serve") == true && given == 2usize;
    bool http_call = std.bytes::equal(command, "serve-http") == true && given == 3usize;
    bool replay_call = std.bytes::equal(command, "replay") == true && given == 3usize;
    bool frames_call = std.bytes::equal(command, "frames") == true && given == 2usize;
    bool schema_call = std.bytes::equal(command, "schema") == true && given == 3usize;
    bool inspect_call = std.bytes::equal(command, "inspect") == true && (given == 3usize || given == 5usize);
    bool inspect_stdio_call = std.bytes::equal(command, "inspect-stdio") == true && given >= 3usize;
    u16 port = 0u16;
    if (http_call == true) {
        switch (port_of(arguments[2].as_str())) {
        case variant o::some(value): port = *value;
        case variant o::none: http_call = false;
        }
    }
    if (schema_call == true) {
        str tool = arguments[2].as_str();
        schema_call = std.bytes::equal(tool, "remember") == true || std.bytes::equal(tool, "recall") == true ||
                      std.bytes::equal(tool, "forget") == true;
    }
    if (demo_call == false && serve_call == false && http_call == false && schema_call == false &&
        inspect_call == false && inspect_stdio_call == false && replay_call == false && frames_call == false) {
        drop arguments;
        await std.console::eprint(std.string::from_str(usage_text()));
        if (given == 1usize) { return 0; }
        return 64;
    }
    if (serve_call == true) {
        drop arguments;
        return await serve();
    }
    if (http_call == true) {
        drop arguments;
        return await serve_http(port);
    }
    if (replay_call == true) {
        std.string::string file = core::replace(&arguments[2], std.string::create());
        drop arguments;
        return await replay(move file);
    }
    if (frames_call == true) {
        drop arguments;
        return await example.assistant.frames::frames();
    }
    if (schema_call == true) {
        std.string::string tool = core::replace(&arguments[2], std.string::create());
        drop arguments;
        return await schema(move tool);
    }
    if (inspect_call == true || inspect_stdio_call == true) { return await inspect(move arguments); }
    std.string::string program = core::replace(&arguments[0], std.string::create());
    drop arguments;
    return await demo(move program);
}

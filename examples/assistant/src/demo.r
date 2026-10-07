module example.assistant.demo;
import std.console;
import std.http;
import std.net;
import std.service;
import std.text;
import std.mcp;
import example.assistant.memory;
import example.assistant.frames;

/* The state of the answers of the demo: none. */
struct voice { u32 unused; };

/* Prints the question of a form and confirms it. */
protected async std.mcp::answer confirm_form(std.mcp::form_request asked) throws std.error::fault {
    std.string::string line = std.string::from_str("  asks: ");
    std.string::append_str(&line, asked.message);
    drop asked;
    await std.console::println(move line);
    try {
        std.json::value content = std.json::parse("{\"confirm\":true}");
        return std.mcp::answer {.action = std.mcp::answer_action::accept, .content = o::some(move content)};
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    return std.mcp::answer {.action = std.mcp::answer_action::cancel, .content = o::none};
}

/* Prints the URL that a server asks the user to open and declines it: the demo has no browser. */
protected async std.mcp::answer decline_url(std.mcp::url_request asked) throws std.error::fault {
    std.string::string line = std.string::from_str("  opens: ");
    std.string::append_str(&line, asked.url);
    drop asked;
    await std.console::println(move line);
    return std.mcp::answer {.action = std.mcp::answer_action::decline, .content = o::none};
}

/* Approves every form after printing its question, and declines URLs. */
async std.mcp::answer approve(arc voice state, std.mcp::elicitation request) throws std.error::fault {
    drop state;
    switch (move request) {
    case variant std.mcp::elicitation::form(move asked): return await confirm_form(move asked);
    case variant std.mcp::elicitation::url(move asked): return await decline_url(move asked);
    }
}

protected std.json::value arguments(str text) throws std.json::error, std.alloc::alloc_error {
    return std.json::parse(text);
}

/* An image or audio block: its kind, media type and size. */
protected std.string::string media_text(str kind, const std.mcp::media* item) throws std.alloc::alloc_error {
    std.string::string text = std.string::from_str("<");
    std.string::append_str(&text, kind);
    std.string::append_str(&text, " ");
    std.string::append_str(&text, item->mime_type);
    usize size = len(item->data);
    std.string::string rest = f" {size} bytes>";
    std.string::append_str(&text, rest);
    return move text;
}

/* The structured result of a tool as JSON text, otherwise its first block. */
protected std.string::string shown(const std.mcp::tool_result* result) throws std.alloc::alloc_error {
    switch (result->structured) {
    case variant o::some(value):
        try {
            return std.json::stringify(value);
        } catch (std.json::error rejected) {
            (move rejected) as void;
        }
    case variant o::none: break;
    }
    if (len(result->content) != 0usize) {
        switch (result->content[0usize]) {
        case variant std.mcp::content::text(text): return std.string::from_str(*text);
        case variant std.mcp::content::image(picture): return media_text("image", picture);
        case variant std.mcp::content::audio(sound): return media_text("audio", sound);
        case variant std.mcp::content::link(linked): return std.string::from_str(linked->uri);
        case variant std.mcp::content::embedded(inner): return std.string::from_str(inner->uri);
        }
    }
    return std.string::from_str("<nothing>");
}

protected std.string::string line_of(str label, str text) throws std.alloc::alloc_error {
    std.string::string line = std.string::from_str(label);
    std.string::append_str(&line, " ");
    std.string::append_str(&line, text);
    return move line;
}


@scoped
protected async void call_and_show(const std.mcp::client* link, str label, str tool, str given)
    throws std.error::fault, std.mcp::mcp_error, std.json::error {
    task_scope(1) io {
        std.mcp::tool_result result = await link->call_tool(tool, arguments(given));
        std.string::string text = line_of(tool, "");
        std.string::string value = shown(&result);
        std.string::append_str(&text, value);
        await std.console::println(line_of(label, text));
    }
}

protected std.string::string joined(const array<std.string::string>* items) throws std.alloc::alloc_error {
    std.string::string text = std.string::create();
    for (usize index = 0usize; index < len(*items); index += 1usize) {
        if (index != 0usize) { std.string::append_str(&text, " "); }
        std.string::append_str(&text, (*items)[index]);
    }
    return move text;
}

/* The subscription part: a note while it listens and the changes that it reports. */
@scoped
protected async void watch_changes(const std.mcp::client* link, str label)
    throws std.error::fault, std.mcp::mcp_error, std.json::error {
    std.mcp::interests wanted = std.mcp::interests::create();
    wanted.resources = true;
    try {
        wanted.uris.push(std.string::from_str("memory://notes"));
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
        std.string::string granted = std.string::from_str("listen");
        if (watched.granted.resources == true) { std.string::append_str(&granted, " resources"); }
        for (usize index = 0usize; index < len(watched.granted.uris); index += 1usize) {
            std.string::append_str(&granted, " ");
            std.string::append_str(&granted, watched.granted.uris[index]);
        }
        task_scope(1) io {
            await std.console::println(line_of(label, granted));
            await call_and_show(link, label, "remember", "{\"text\":\"call Bob about the trip\"}");
            for (u32 round = 0u32; round < 2u32; round += 1u32) {
                o<std.mcp::change> next = await watched.next();
                switch (move next) {
                case variant o::some(move found):
                    switch (move found) {
                    case variant std.mcp::change::resources: await std.console::println(line_of(label, "change resources"));
                    case variant std.mcp::change::updated(move uri):
                        std.string::string text = std.string::from_str("change updated ");
                        std.string::append_str(&text, uri);
                        await std.console::println(line_of(label, text));
                    default: await std.console::println(line_of(label, "change other"));
                    }
                case variant o::none: await std.console::println(line_of(label, "change none"));
                }
            }
            await link->unlisten(move watched);
        }
    case variant o::none: break;
    }
}

/* A read of a resource: its text or the error of the server. */
@scoped
protected async void read_and_show(const std.mcp::client* link, str label, str uri)
    throws std.error::fault, std.mcp::mcp_error, std.json::error {
    std.string::string text = line_of("read", uri);
    std.string::append_str(&text, " ");
    try {
        task_scope(1) io {
            array<std.mcp::contents> items = await link->read_resource(uri);
            for (usize index = 0usize; index < len(items); index += 1usize) {
                std.string::append_str(&text, items[index].text);
            }
        }
    } catch (std.mcp::mcp_error failure) {
        i64 code = failure.code;
        std.string::string reason = f"{code} ";
        std.string::append_str(&reason, failure.message);
        std.string::append_str(&text, reason);
    }
    task_scope(1) io { await std.console::println(line_of(label, text)); }
}

/* Whether the capabilities of discover offer the Tasks extension. */
protected bool offers_tasks(const std.json::value* capabilities) {
    switch (std.json::find(capabilities, "extensions")) {
    case variant o::some(extensions):
        switch (std.json::find(*extensions, std.mcp::tasks_extension)) {
        case variant o::some(found):
            found as void;
            return true;
        case variant o::none: break;
        }
    case variant o::none: break;
    }
    return false;
}

protected str status_word(std.mcp::task_status status) {
    switch (status) {
    case std.mcp::task_status::working: return "working";
    case std.mcp::task_status::input_required: return "input_required";
    case std.mcp::task_status::completed: return "completed";
    case std.mcp::task_status::failed: return "failed";
    case std.mcp::task_status::cancelled: return "cancelled";
    }
}

/* R: starts forget without waiting (R-SLIB-MCP-0023) and prints the first state of its task;
   the id of the task, empty when the tool ran at once. */
@scoped
protected async std.string::string start_forget(const std.mcp::client* link, str label, str given,
                                               const std.mcp::elicitor<voice>* answers)
    throws std.error::fault, std.mcp::mcp_error, std.json::error {
    std.string::string id = std.string::create();
    task_scope(1) io {
        std.mcp::tool_start started = await link->start_tool("forget", arguments(given), answers);
        switch (move started) {
        case variant std.mcp::tool_start::running(move state):
            std.string::string line = line_of("task forget", status_word(state.status));
            await std.console::println(line_of(label, line));
            std.string::string found = core::replace(&state.id, std.string::create());
            std.string::string old = core::replace(&id, move found);
            drop old;
        case variant std.mcp::tool_start::finished(move result):
            std.string::string value = shown(&result);
            std.string::string line = line_of("forget", value);
            await std.console::println(line_of(label, line));
        }
    }
    return move id;
}

/* R: polls a task with get_task until it asks for input or ends, and prints its question. */
@scoped
protected async o<std.mcp::task_state> asking_state(const std.mcp::client* link, str label, str id)
    throws std.error::fault, std.mcp::mcp_error, std.json::error {
    for (u32 round = 0u32; round < 200u32; round += 1u32) {
        o<std.mcp::task_state> found = o::none;
        task_scope(1) io {
            std.mcp::task_state state = await link->get_task(id);
            if (state.status != std.mcp::task_status::working) {
                o<std.mcp::task_state> old = core::replace(&found, o::some(move state));
                drop old;
            } else {
                drop state;
            }
        }
        switch (move found) {
        case variant o::some(move state):
            std.string::string line = line_of("task", status_word(state.status));
            if (len(state.requests) != 0usize) {
                switch (state.requests[0usize]) {
                case variant std.mcp::elicitation::form(form):
                    std.string::append_str(&line, " ");
                    std.string::append_str(&line, form->message);
                case variant std.mcp::elicitation::url(page):
                    std.string::append_str(&line, " ");
                    std.string::append_str(&line, page->message);
                }
            }
            task_scope(1) io { await std.console::println(line_of(label, line)); }
            return o::some(move state);
        case variant o::none: break;
        }
        await std.time::sleep_for(std.time::duration_from_parts(0i64, 50000000u32));
    }
    return o::none;
}

/* R: the end of a task as its subscription reports it: its result when it completes. */
@scoped
protected async void await_completion(std.mcp::subscription* watched, str label)
    throws std.error::fault, std.mcp::mcp_error, std.json::error {
    bool done = false;
    for (u32 round = 0u32; round < 8u32 && done == false; round += 1u32) {
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
                    done = true;
                    std.string::string line = std.string::from_str("task completed ");
                    switch (state.result) {
                    case variant o::some(result):
                        std.string::string value = shown(result);
                        std.string::append_str(&line, value);
                    case variant o::none: break;
                    }
                    task_scope(1) io { await std.console::println(line_of(label, line)); }
                }
            default: break;
            }
        case variant o::none: done = true;
        }
    }
}

/* R: forget as a task: its question seen with get_task, the answer given with answer_task, and
   its end seen through a subscription to the task. */
@scoped
protected async void forget_in_task(const std.mcp::client* link, str label, const std.mcp::elicitor<voice>* answers)
    throws std.error::fault, std.mcp::mcp_error, std.json::error {
    std.string::string id = std.string::create();
    o<std.mcp::task_state> asking = o::none;
    task_scope(1) io {
        std.string::string started = await start_forget(link, label, "{\"id\":3}", answers);
        std.string::string old = core::replace(&id, move started);
        drop old;
        if (std.string::len(&id) != 0usize) {
            o<std.mcp::task_state> found = await asking_state(link, label, id);
            o<std.mcp::task_state> previous = core::replace(&asking, move found);
            drop previous;
        }
    }
    std.string::string key = std.string::create();
    switch (move asking) {
    case variant o::some(move state):
        if (len(state.keys) != 0usize) { std.string::append_str(&key, state.keys[0usize]); }
    case variant o::none: break;
    }
    if (std.string::len(&key) == 0usize) { return; }
    std.mcp::interests wanted = std.mcp::interests::create();
    try {
        wanted.tasks.push(std.string::from_str(id));
    } catch (std.array::push_error<std.string::string> rejected) {
        (move rejected) as void;
    }
    std.mcp::answer yes = {.action = std.mcp::answer_action::accept, .content = o::some(arguments("{\"confirm\":true}"))};
    o<std.mcp::subscription> opened = o::none;
    task_scope(1) io {
        std.mcp::subscription got = await link->listen(&wanted);
        o<std.mcp::subscription> old = core::replace(&opened, o::some(move got));
        drop old;
    }
    switch (move opened) {
    case variant o::some(move watched):
        u64 granted = len(watched.granted.tasks) as u64;
        std.string::string line = f"listen tasks {granted}";
        task_scope(1) io {
            await std.console::println(line_of(label, line));
            await link->answer_task(id, key, &yes);
            await await_completion(&watched, label);
            await link->unlisten(move watched);
        }
    case variant o::none: break;
    }
}

/* R: forget as a task cancelled while it asks for input: following it to its end fails with
   std.mcp::task_cancelled. */
@scoped
protected async void cancel_forget(const std.mcp::client* link, str label, const std.mcp::elicitor<voice>* answers)
    throws std.error::fault, std.mcp::mcp_error, std.json::error {
    std.string::string id = std.string::create();
    task_scope(1) io {
        std.string::string started = await start_forget(link, label, "{\"id\":1}", answers);
        std.string::string old = core::replace(&id, move started);
        drop old;
    }
    if (std.string::len(&id) == 0usize) { return; }
    std.string::string text = std.string::from_str("task ");
    try {
        task_scope(1) io {
            o<std.mcp::task_state> asking = await asking_state(link, label, id);
            drop asking;
            await link->cancel_task(id);
            std.mcp::tool_result result = await link->finish_task(id, answers);
            std.string::string value = shown(&result);
            std.string::append_str(&text, value);
        }
    } catch (std.mcp::mcp_error failure) {
        if (failure.code == std.mcp::task_cancelled) {
            std.string::append_str(&text, "cancelled");
        } else {
            std.string::append_str(&text, example.assistant.frames::code_name(failure.code));
        }
    }
    task_scope(1) io { await std.console::println(line_of(label, text)); }
}

/* R: one conversation with the memory server over a client of any transport. */
@scoped
async void converse(const std.mcp::client* link, str label) throws std.error::fault, std.mcp::mcp_error, std.json::error {
    std.mcp::elicitor<voice> answers = {.state = new arc voice {.unused = 0u32}, .handler = approve};
    task_scope(1) io {
        std.mcp::discovery found = await link->discover();
        std.string::string text = line_of("discover", found.server.name);
        std.string::append_str(&text, " ");
        std.string::append_str(&text, found.server.version);
        std.string::append_str(&text, " ");
        std.string::string versions = joined(&found.versions);
        std.string::append_str(&text, versions);
        if (offers_tasks(&found.capabilities) == true) { std.string::append_str(&text, " tasks"); }
        await std.console::println(line_of(label, text));
        array<std.mcp::tool> tools = await link->list_tools();
        std.string::string names = std.string::from_str("tools");
        for (usize index = 0usize; index < len(tools); index += 1usize) {
            std.string::append_str(&names, " ");
            std.string::append_str(&names, tools[index].name);
        }
        await std.console::println(line_of(label, names));
        await call_and_show(link, label, "remember", "{\"text\":\"tea with Ann on Friday\"}");
        await call_and_show(link, label, "remember", "{\"text\":\"green tea\"}");
        await watch_changes(link, label);
        await call_and_show(link, label, "recall", "{\"query\":\"TEA\"}");
        std.mcp::tool_result forgotten = await link->call_tool_with("forget", arguments("{\"id\":2}"), &answers);
        std.string::string forget_line = line_of("forget", "");
        std.string::string result = shown(&forgotten);
        std.string::append_str(&forget_line, result);
        await std.console::println(line_of(label, forget_line));
        await read_and_show(link, label, "memory://notes/1");
        await read_and_show(link, label, "memory://notes/2");
        array<std.mcp::resource> resources = await link->list_resources();
        std.string::string uris = std.string::from_str("resources");
        for (usize index = 0usize; index < len(resources); index += 1usize) {
            std.string::append_str(&uris, " ");
            std.string::append_str(&uris, resources[index].uri);
        }
        await std.console::println(line_of(label, uris));
        std.mcp::prompt_result reflected = await link->get_prompt("reflect", arguments("{\"topic\":\"tea\"}"));
        std.string::string prompt_line = std.string::from_str("prompt");
        for (usize index = 0usize; index < len(reflected.messages); index += 1usize) {
            const std.mcp::prompt_message* said = &reflected.messages[index];
            switch (said->content) {
            case variant std.mcp::content::text(message):
                std.string::append_str(&prompt_line, " ");
                std.string::append_str(&prompt_line, *message);
            default: break;
            }
        }
        await std.console::println(line_of(label, prompt_line));
        std.mcp::completion completed = await link->complete(true, "reflect", "topic", "t");
        std.string::string values = joined(&completed.values);
        std.string::string complete_line = line_of("complete t ->", values);
        await std.console::println(line_of(label, complete_line));
        await call_and_show(link, label, "count", "{}");
        await forget_in_task(link, label, &answers);
        await cancel_forget(link, label, &answers);
    }
}

protected async std.net::tcp_listener listen_loopback(u16 port) throws std.error::fault {
    std.net::socket_address local = {.address = std.net::parse_ip("127.0.0.1"), .port = port, .scope_id = 0u32};
    std.net::listen_options options = {.backlog = 16u32, .reuse_address = true, .v6_only = false};
    return await local.listen(options);
}

protected std.mcp::implementation client_info() throws std.alloc::alloc_error {
    return std.mcp::implementation::create("assistant-demo", "0.1.0");
}

/* R: one request that the protected server refuses, and the name of the refusal: discover
   without a token is unauthorized, a call of a tool with the read token is forbidden. */
@scoped
protected async void try_refused(const std.mcp::client* link, str label, bool calling)
    throws std.error::fault, std.json::error {
    std.string::string text = std.string::create();
    try {
        task_scope(1) io {
            if (calling == true) {
                std.string::append_str(&text, "remember ");
                std.mcp::tool_result result = await link->call_tool("remember", arguments("{\"text\":\"a secret\"}"));
                drop result;
            } else {
                std.string::append_str(&text, "discover ");
                std.mcp::discovery found = await link->discover();
                drop found;
            }
            std.string::append_str(&text, "accepted");
        }
    } catch (std.mcp::mcp_error failure) {
        std.string::append_str(&text, "refused: ");
        std.string::append_str(&text, example.assistant.frames::code_name(failure.code));
    }
    task_scope(1) io { await std.console::println(line_of(label, text)); }
}

/* R: the conversation over Streamable HTTP with a server of this process on a free port, whose
   tasks a runner runs. The server is protected: its clients send a bearer token that check_token
   accepts. The client declares the Tasks extension. */
async void over_http() throws std.error::fault, std.mcp::mcp_error, std.json::error {
    std.net::tcp_listener listener = await listen_loopback(0u16);
    std.net::socket_address endpoint = listener.local_address();
    std.sync::channel<std.service::stop> factory = std.sync::channel::<std.service::stop>();
    std.sync::sender<std.service::stop> stopper = std.sync::sender(&factory);
    std.sync::receiver<std.service::stop> stop = std.sync::receiver(move factory);
    std.sync::channel<std.service::stop> task_factory = std.sync::channel::<std.service::stop>();
    std.sync::sender<std.service::stop> task_stopper = std.sync::sender(&task_factory);
    std.sync::receiver<std.service::stop> task_stop = std.sync::receiver(move task_factory);
    u16 port = endpoint.port;
    std.string::string address = f"http://127.0.0.1:{port}/mcp";
    std.mcp::server<example.assistant.memory::memory> built = example.assistant.memory::build();
    std.mcp::protection guard = std.mcp::protection::create(address, "https://auth.example");
    guard.scope("notes.write");
    built.protect(move guard, example.assistant.memory::check_token);
    arc std.mcp::server<example.assistant.memory::memory> host =
        new arc std.mcp::server<example.assistant.memory::memory>(move built);
    std.http::router<std.mcp::server<example.assistant.memory::memory>> routes =
        std.http::router<std.mcp::server<example.assistant.memory::memory>>::create();
    try {
        std.mcp::route(&routes, "/mcp");
    } catch (std.http::http_error rejected) {
        rejected as void;
    }
    std.mcp::client_options settings = {.tasks = true};
    std.mcp::client link = std.mcp::client::over_http(address, client_info(), settings, o::none);
    std.service::options service_settings = {};
    std.http::limits bounds = {};
    task_scope(3) group {
        auto runner = std.mcp::run_tasks(std.arc::clone(&host), move task_stop);
        auto serving = std.http::serve(move listener, service_settings, move stop, std.arc::clone(&host), move routes, bounds);
        await try_refused(&link, "http", false);
        link.set_token("read-token");
        await try_refused(&link, "http", true);
        link.set_token("demo-token");
        await converse(&link, "http");
        std.sync::send_result<std.service::stop> sent = std.sync::send(&stopper, std.service::stop::drain);
        drop sent;
        std.service::report account = await move serving;
        u64 served = account.accepted;
        std.sync::send_result<std.service::stop> ended = std.sync::send(&task_stopper, std.service::stop::cancel);
        drop ended;
        await move runner;
        await std.console::println(line_of("http", "stopped"));
        served as void;
    }
}

/* R: the conversation over stdio with this program started as `serve`; the client declares the
   Tasks extension. */
async i32 over_stdio(std.string::string program) throws std.error::fault, std.mcp::mcp_error, std.json::error {
    std.fs::path path = std.fs::path_from_utf8(program);
    std.process::command command = std.process::command_create(&path);
    command.arg("serve");
    std.mcp::client_options settings = {.tasks = true};
    std.mcp::launched started = await std.mcp::launch(move command, client_info(), settings);
    o<std.process::child> child = core::replace(&started.child, o::none);
    task_scope(2) session {
        auto pump = started.link.run();
        await converse(&started.link, "stdio");
        await started.link.close();
        await move pump;
    }
    drop started;
    i32 status = 1;
    switch (move child) {
    case variant o::some(move process):
        std.process::wait_result finished = await (move process).wait();
        // Only read, so the switch borrows the outcome place (R-STMT-0010).
        switch (finished) {
        case variant std.process::wait_result::exited(exit): status = exit->code;
        case variant std.process::wait_result::failed(failure): throw failure->error;
        }
        std.string::string text = f"server exited {status}";
        await std.console::println(line_of("stdio", text));
    case variant o::none: break;
    }
    return status;
}

/* Lists what a list method returns, or the error of a server that does not offer it. */
@scoped
protected async void show_tools(const std.mcp::client* link, str label) throws std.error::fault, std.json::error {
    try {
        task_scope(1) io {
            array<std.mcp::tool> tools = await link->list_tools();
            for (usize index = 0usize; index < len(tools); index += 1usize) {
                std.string::string text = line_of("tool", tools[index].name);
                std.string::append_str(&text, ": ");
                std.string::append_str(&text, tools[index].description);
                await std.console::println(line_of(label, text));
            }
        }
    } catch (std.mcp::mcp_error failure) {
        i64 code = failure.code;
        std.string::string problem = f"tools: error {code}";
        await std.console::println(line_of(label, problem));
    }
}

@scoped
protected async void show_resources(const std.mcp::client* link, str label) throws std.error::fault, std.json::error {
    try {
        task_scope(1) io {
            array<std.mcp::resource> resources = await link->list_resources();
            for (usize index = 0usize; index < len(resources); index += 1usize) {
                std.string::string text = line_of("resource", resources[index].uri);
                await std.console::println(line_of(label, text));
            }
        }
    } catch (std.mcp::mcp_error failure) {
        i64 code = failure.code;
        std.string::string problem = f"resources: error {code}";
        await std.console::println(line_of(label, problem));
    }
}

@scoped
protected async void show_prompts(const std.mcp::client* link, str label) throws std.error::fault, std.json::error {
    try {
        task_scope(1) io {
            array<std.mcp::prompt> prompts = await link->list_prompts();
            for (usize index = 0usize; index < len(prompts); index += 1usize) {
                std.string::string text = line_of("prompt", prompts[index].name);
                for (usize at = 0usize; at < len(prompts[index].arguments); at += 1usize) {
                    const std.mcp::prompt_argument* argument = &prompts[index].arguments[at];
                    std.string::append_str(&text, " ");
                    std.string::append_str(&text, argument->name);
                    if (argument->required == true) { std.string::append_str(&text, "*"); }
                }
                await std.console::println(line_of(label, text));
            }
        }
    } catch (std.mcp::mcp_error failure) {
        i64 code = failure.code;
        std.string::string problem = f"prompts: error {code}";
        await std.console::println(line_of(label, problem));
    }
}

/* R: what a server offers: its identity, versions, tools, resources and prompts, and optionally
   the result of calling one tool with JSON arguments. */
@scoped
async void inspect(const std.mcp::client* link, str label, str tool, str given)
    throws std.error::fault, std.mcp::mcp_error, std.json::error {
    task_scope(1) io {
        std.mcp::discovery found = await link->discover();
        std.string::string text = line_of("server", found.server.name);
        std.string::append_str(&text, " ");
        std.string::append_str(&text, found.server.version);
        std.string::append_str(&text, " speaks ");
        std.string::string versions = joined(&found.versions);
        std.string::append_str(&text, versions);
        await std.console::println(line_of(label, text));
        await show_tools(link, label);
        await show_resources(link, label);
        await show_prompts(link, label);
        const u8[] wanted = tool;
        if (len(wanted) != 0usize) { await call_and_show(link, label, tool, given); }
    }
}

/* R: inspect over Streamable HTTP. */
async void inspect_http(std.string::string address, std.string::string tool, std.string::string given)
    throws std.error::fault, std.mcp::mcp_error, std.json::error {
    std.mcp::client_options settings = {};
    std.mcp::client link = std.mcp::client::over_http(address, client_info(), settings, o::none);
    task_scope(1) io { await inspect(&link, "http", tool, given); }
}

/* R: inspect a program started as a stdio server with its arguments. */
async void inspect_stdio(array<std.string::string> command_line, std.string::string tool, std.string::string given)
    throws std.error::fault, std.mcp::mcp_error, std.json::error {
    std.fs::path path = std.fs::path_from_utf8(command_line[0usize]);
    std.process::command command = std.process::command_create(&path);
    for (usize index = 1usize; index < len(command_line); index += 1usize) {
        command.arg(command_line[index]);
    }
    std.mcp::client_options settings = {};
    std.mcp::launched started = await std.mcp::launch(move command, client_info(), settings);
    o<std.process::child> child = core::replace(&started.child, o::none);
    task_scope(2) session {
        auto pump = started.link.run();
        await inspect(&started.link, "stdio", tool, given);
        await started.link.close();
        await move pump;
    }
    drop started;
    switch (move child) {
    case variant o::some(move process):
        std.process::wait_result finished = await (move process).wait();
        drop finished;
    case variant o::none: break;
    }
}

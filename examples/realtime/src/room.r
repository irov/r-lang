module example.realtime.room;
import std.http;
import std.text;
import std.websocket;

/* A chat room: every line published to it reaches every member that has joined. */
struct Room { std.async::broadcast<std.string::string> updates; };

Room create() throws std.alloc::alloc_error {
    return Room {.updates = std.async::broadcast::<std.string::string>(16usize)};
}

/* The name a member gives with the query "name=...", or "guest". */
protected std.string::string name_of(const std.http::request* incoming) throws std.alloc::alloc_error {
    switch (incoming->query()) {
    case variant o::some(query):
        if (std.text::starts_with(*query, "name=") == true) {
            const u8[] bytes_of = *query;
            std.string::string name = std.string::create();
            try {
                std.string::append_utf8(&name, bytes_of[5usize..len(bytes_of)]);
            } catch (std.string::string_error rejected) {
                // A query is text, and so is its part after "name=".
                rejected as void;
            }
            if (std.string::len(&name) != 0usize) { return move name; }
        }
    case variant o::none: break;
    }
    return std.string::from_str("guest");
}

protected void announce(const Room* hall, std.string::string line) throws std.alloc::alloc_error {
    usize reached = hall->updates.send(move line);
    reached as void;
}

/* Sends one line to the member. */
@scoped
protected async void send_line(const std.websocket::websocket* socket, std.string::string line)
    throws std.error::fault, std.websocket::websocket_error {
    task_scope(1) io { await socket->send_text(line.as_str()); }
}

/* Sends every line of the room to the member until the room closes or the member is gone. */
@scoped
protected async void forward(const std.websocket::websocket* socket,
                             std.async::broadcast_receiver<std.string::string> updates)
    throws std.error::fault {
    try {
        while (true) {
            std.async::broadcast_result<std.string::string> next = await updates.receive();
            switch (move next) {
            case variant std.async::broadcast_result::received(move line):
                task_scope(1) io { await send_line(socket, move line); }
            case variant std.async::broadcast_result::lagged(move missed): missed as void;
            case variant std.async::broadcast_result::closed: return;
            }
        }
    } catch (std.websocket::websocket_error rejected) {
        rejected as void;
    }
}

/* Publishes each text message of the member as "name: text" until the member closes. */
@scoped
protected async void relay_lines(const std.websocket::websocket* socket, const Room* hall, str name)
    throws std.error::fault, std.websocket::websocket_error {
    while (true) {
        o<std.websocket::message> next = o::none;
        task_scope(1) io {
            o<std.websocket::message> got = await socket->receive();
            o<std.websocket::message> old = core::replace(&next, move got);
            drop old;
        }
        switch (move next) {
        case variant o::none: return;
        case variant o::some(move item):
            if (item.kind == std.websocket::message_kind::close) { return; }
            if (item.kind == std.websocket::message_kind::text) {
                std.string::string line = std.string::from_str(name);
                std.string::append_str(&line, ": ");
                std.string::append_str(&line, item.text());
                announce(hall, move line);
            }
        }
    }
}

/* relay_lines; a member that breaks the protocol has been answered with a close and leaves. */
@scoped
protected async void listen(const std.websocket::websocket* socket, const Room* hall, str name)
    throws std.error::fault {
    try {
        task_scope(1) io { await relay_lines(socket, hall, name); }
    } catch (std.websocket::websocket_error rejected) {
        rejected as void;
    }
}

/* The upgrade route /chat: the member subscribes before the handshake answers, so it sees every
   line published after its connect returns; then the room reaches the member and the member the
   room until the member leaves. */
@scoped
protected async void member(arc Room state, std.http::request incoming, std.http::upgrade connection)
    throws std.error::fault {
    std.string::string name = name_of(&incoming);
    const Room* hall = &*state;
    std.async::broadcast_receiver<std.string::string> updates = hall->updates.subscribe();
    o<std.websocket::websocket> opened = o::none;
    try {
        task_scope(1) handshake {
            std.websocket::websocket made = await std.websocket::accept(move incoming, move connection, "chat");
            o<std.websocket::websocket> old = core::replace(&opened, o::some(move made));
            drop old;
        }
    } catch (std.websocket::websocket_error rejected) {
        rejected as void;
    }
    switch (move opened) {
    case variant o::some(move socket):
        announce(hall, f"* {name} joined");
        task_scope(2) session {
            auto sending = forward(&socket, move updates);
            await listen(&socket, hall, name.as_str());
            session.cancel_all();
            await session.all();
        }
        announce(hall, f"* {name} left");
    case variant o::none:
        // The handshake was refused; the member never joined.
        drop updates;
        std.string::len(&name) as void;
    }
}

/* The handler of the route: the member stays until it leaves. */
async void join(arc Room state, std.http::request incoming, std.http::upgrade connection)
    throws std.error::fault {
    task_scope(1) stay { await member(move state, move incoming, move connection); }
}

module example.tls.main;
import std.console;
import std.net;
import std.tls;
import example.tls.peers;

enum Command { echo, check };

/* The words after the first six arguments, joined by spaces. */
std.string::string message(const array<std.string::string>* arguments) throws std.alloc::alloc_error {
    std.string::string joined = std.string::create();
    usize count = len(*arguments);
    for (usize index = 6usize; index < count; index += 1usize) {
        if (index > 6usize) { joined.append(" "); }
        joined.append((*arguments)[index].as_str());
    }
    return move joined;
}

/* Prints what the handshake negotiated; echo also sends the message and prints the answer. */
async void talk(std.tls::stream<std.net::tcp_stream> session, Command command, std.string::string text)
    throws std.error::fault {
    str protocol = session.protocol();
    std.tls::version version = session.version();
    await std.console::println(f"trusted: protocol={protocol} version={version}");
    u8[1024] reply = {};
    task_scope(1) io {
        if (command == Command::echo) {
            await session.write_all_from(text.as_bytes());
            usize count = await session.read_into(&reply);
            std.string::string answer = std.string::from_str("");
            answer.append_utf8(reply[0usize..count]);
            await std.console::println(f"reply: {answer}");
        }
        await session.shutdown();
    }
}

/* Connects to a server in the same process over TLS; a rejected certificate is reported with
   its reason and status 65. */
async i32 run(Command command, array<std.string::string> arguments)
    throws std.error::fault, std.tls::tls_error {
    std.tls::config client =
        await example.tls.peers::client(core::replace(&arguments[2], std.string::create()));
    std.tls::config server = await example.tls.peers::server(
        core::replace(&arguments[3], std.string::create()),
        core::replace(&arguments[4], std.string::create()));
    std.net::tcp_listener listener = await example.tls.peers::listen_loopback();
    std.net::socket_address endpoint = listener.local_address();
    std.net::tcp_stream outgoing = await endpoint.connect();
    std.net::tcp_connection incoming = await listener.accept();
    await (move listener).close();
    i32 status = 0;
    task_scope(2) io {
        task<void throws std.error::fault> serving =
            example.tls.peers::serve(move incoming, &server);
        try {
            std.tls::stream<std.net::tcp_stream> session =
                await std.tls::connect(move outgoing, &client, arguments[5].as_str());
            await talk(move session, command, message(&arguments));
        } catch (std.tls::tls_error failure) {
            status = 65;
            await std.console::println(f"rejected: {failure.code}");
        }
        await move serving;
    }
    return status;
}

async i32 main() {
    array<std.string::string> arguments = std.env::arguments();
    usize given = len(arguments);
    o<Command> command = o::none;
    if (given >= 6usize) { command = core::enum_from_name::<Command>(arguments[1].as_str()); }
    switch (command) {
    case variant o::none:
        drop arguments;
        await std.console::eprint(std.string::from_str(
            "tls echo CA CERT KEY NAME WORD...\ntls check CA CERT KEY NAME\n"));
        if (given == 1usize) { return 0; }
        return 64;
    case variant o::some(selected):
        try {
            return await run(*selected, move arguments);
        } catch (std.tls::tls_error failure) {
            /* A file that holds no certificate or no key. */
            await std.console::eprintln(f"tls: {failure.code}");
        }
        return 65;
    }
}

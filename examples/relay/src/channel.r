module example.relay.channel;
import std.stream;
import std.bufio;

/* Writes one line to any writer, through its interface. */
@scoped
async void send_line(const dyn(std.stream::Writer)* sink, const u8[] line)
    throws std.error::fault {
    u8[1] feed = {10u8};
    task_scope(1) io {
        await sink->write_all_from(line);
        await sink->write_all_from(feed[0usize..1usize]);
    }
}

/* The server side of the echo: every line of the connection comes back as its length and the
   line; the end of the client's lines ends the reply. */
async u32 serve(std.net::tcp_connection connection) throws std.error::fault {
    std.bufio::reader<std.net::tcp_connection> lines =
        std.bufio::reader<std.net::tcp_connection>::create(move connection, 128usize);
    std.string::string line = std.string::create();
    u32 served = 0u32;
    task_scope(1) io {
        while (await lines.read_line(&line) == true) {
            usize length = std.string::len(&line);
            std.string::string reply = f"{length} {line}";
            task_scope(1) write { await send_line(&lines.source, reply.as_bytes()); }
            served += 1u32;
        }
        await lines.source.shutdown();
    }
    return served;
}

std.net::socket_address loopback() throws std.net::address_error {
    return std.net::socket_address {.address = std.net::parse_ip("127.0.0.1"), .port = 0u16,
                                    .scope_id = 0u32};
}

/* Sends each word as a line over a loopback TCP connection that the client owns through the
   Stream interface, and reads the replies with a buffered reader of that owner. */
async std.string::string echo_words(array<std.string::string> words) throws std.error::fault {
    std.net::socket_address local = loopback();
    std.net::listen_options options = {.backlog = 4u32, .reuse_address = true, .v6_only = false};
    std.net::tcp_listener listener = await local.listen(options);
    std.net::socket_address endpoint = listener.local_address();
    std.net::tcp_stream client = await endpoint.connect();
    std.net::tcp_connection connection = await listener.accept();
    await (move listener).close();
    own dyn(std.stream::Stream)* link = new std.net::tcp_stream(move client);
    std.string::string output = std.string::create();
    std.string::reserve(&output, 64usize);
    task_scope(1) exchange {
        auto server = serve(move connection);
        task_scope(1) io {
            for (usize index = 0usize; index < len(words); index += 1usize) {
                await send_line(&link, words[index].as_bytes());
            }
            await link->shutdown();
        }
        std.bufio::reader<own dyn(std.stream::Stream)*> replies =
            std.bufio::reader<own dyn(std.stream::Stream)*>::create(move link, 32usize);
        std.string::string reply = std.string::create();
        task_scope(1) io {
            while (await replies.read_line(&reply) == true) {
                std.string::append_str(&output, "echo: ");
                std.string::append_str(&output, reply.as_str());
                std.string::append_str(&output, "\n");
            }
        }
        u32 served = await move server;
        std.string::string summary = f"{served} lines served\n";
        std.string::append_str(&output, summary.as_str());
    }
    return move output;
}

/* Talks to /bin/cat through its two pipes as one stream: the words go in as lines and come back
   one line each; dropping the stream closes both pipes, so cat ends. */
async std.string::string pipe_words(array<std.string::string> words) throws std.error::fault {
    std.fs::path program = std.fs::path_from_utf8("/bin/cat");
    std.process::command command = std.process::command_create(&program);
    std.process::stdio policy = {.input = std.process::pipe_mode::piped,
                                 .output = std.process::pipe_mode::piped,
                                 .error = std.process::pipe_mode::inherit};
    command.set_stdio(policy);
    std.process::spawn_result started = await (move command).spawn();
    switch (move started) {
    case variant std.process::spawn_result::failed(move failure):
        throw failure.error;
    case variant std.process::spawn_result::spawned(move child):
        o<std.io::output> input_pipe = child.take_stdin();
        o<std.io::input> output_pipe = child.take_stdout();
        std.string::string output = std.string::create();
        std.string::reserve(&output, 64usize);
        switch (move input_pipe) {
        case variant o::none:
            drop output_pipe;
            throw std.io::io_error {.code = std.io::error_code::closed, .native_code = 0i64};
        case variant o::some(move writer):
            switch (move output_pipe) {
            case variant o::none:
                drop writer;
                throw std.io::io_error {.code = std.io::error_code::closed, .native_code = 0i64};
            case variant o::some(move reader):
                std.stream::duplex<std.io::input, std.io::output> pipes =
                    {.reader = move reader, .writer = move writer};
                own dyn(std.stream::Stream)* link =
                    new std.stream::duplex<std.io::input, std.io::output>(move pipes);
                task_scope(1) io {
                    for (usize index = 0usize; index < len(words); index += 1usize) {
                        await send_line(&link, words[index].as_bytes());
                    }
                    await link->flush();
                }
                std.bufio::reader<own dyn(std.stream::Stream)*> lines =
                    std.bufio::reader<own dyn(std.stream::Stream)*>::create(move link, 32usize);
                std.string::string line = std.string::create();
                std.string::reserve(&line, 32usize);
                task_scope(1) io {
                    for (usize index = 0usize; index < len(words); index += 1usize) {
                        if (await lines.read_line(&line) == false) { break; }
                        std.string::append_str(&output, "cat: ");
                        std.string::append_str(&output, line.as_str());
                        std.string::append_str(&output, "\n");
                    }
                }
                drop lines;
            }
        }
        std.process::wait_result finished = await (move child).wait();
        switch (move finished) {
        case variant std.process::wait_result::exited(move status):
            std.string::string ending = f"cat exited with {status.code}\n";
            std.string::append_str(&output, ending.as_str());
        case variant std.process::wait_result::failed(move failure):
            throw failure.error;
        }
        return move output;
    }
}

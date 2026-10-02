module example.ipc.stream;
import std.console;
import std.stream;
import std.bufio;

struct Flag { bool value; };
struct Count { usize value; };

// The signals that stop the service between clients.
struct Stops { std.signal::listener terminate; std.signal::listener interrupt; };

/* The newline after a message, written through any writer: a Unix-domain stream is a
   std.stream::Writer (Library R-SLIB-STREAM-0002). */
@scoped
async void end_line(const dyn(std.stream::Writer)* sink) throws std.error::fault {
    u8[1] feed = {10u8};
    task_scope(1) io { await sink->write_all_from(feed[0usize..1usize]); }
}

/* One client: its line up to its half-close, answered with the user identifier the socket
   recorded for it, whether its process is known, the length and the line. */
async void answer(std.net::unix_stream client) throws std.error::fault {
    std.net::peer_credentials peer = std.net::unix_peer_credentials(&client);
    bytes buffer = std.alloc::bytes(4096usize, 0u8);
    Count length = {.value = 0usize};
    Count count = {.value = 1usize};
    task_scope(1) input {
        while (count.value != 0usize && length.value < len(buffer)) {
            u8[] window = buffer.as_slice_mut();
            count.value = await std.net::unix_read_into(&client, window[length.value..len(window)]);
            length.value += count.value;
        }
    }
    const u8[] received = buffer.as_slice();
    Count size = {.value = length.value};
    if (size.value > 0usize && received[size.value - 1usize] == 10u8) { size.value -= 1usize; }
    std.string::string line = std.string::from_utf8(received[0usize..size.value]);
    bool known = peer.process_id > 0;
    std.string::string reply = f"uid={peer.user_id} process_known={known} length={size.value} {line}\n";
    usize total = std.string::len(&reply);
    Count sent = {.value = 0usize};
    task_scope(1) output {
        while (sent.value < total) {
            const u8[] text = reply.as_bytes();
            sent.value += await client.write_from(text[sent.value..total]);
        }
    }
    await std.net::unix_close(move client);
}

/* Serve up to `limit` clients at the path; SIGTERM or SIGINT stops the service between clients.
   The listeners exist before the ready line is printed, so no signal sent after it is missed. */
async u32 serve(std.string::string path, u32 limit) throws std.error::fault {
    Stops stops = {.terminate = std.signal::kind::terminate.listen(),
                   .interrupt = std.signal::listen(std.signal::kind::interrupt)};
    str where = path.as_str();
    std.net::unix_listener listener = await std.net::unix_listen(where, 8u32, true);
    await std.console::print(f"listening {path}\n");
    u32 served = 0u32;
    Flag stopped = {.value = false};
    while (served < limit && stopped.value == false) {
        o<std.net::unix_stream> arrived = o::none;
        task_scope(3) turn {
            auto next = listener.accept();
            auto term = stops.terminate.next();
            auto intr = stops.interrupt.next();
            select (turn) {
            case std.net::unix_stream client = await move next: arrived = o::some(move client); break;
            case u64 signals = await move term: signals as void; stopped.value = true; break;
            case u64 signals = await move intr: signals as void; stopped.value = true; break;
            }
            turn.cancel_all();
            await turn.all();
        }
        switch (move arrived) {
        case variant o::some(move client):
            await answer(move client);
            served += 1u32;
            break;
        case variant o::none: break;
        }
    }
    await std.net::unix_listener_close(move listener);
    return served;
}

/* Send one line and read the answer through std.bufio; the client half-closes after its line. */
async std.string::string call(std.string::string path, std.string::string message)
    throws std.error::fault {
    str where = path.as_str();
    std.net::unix_stream connection = await std.net::unix_connect(where);
    task_scope(1) output {
        await std.net::unix_write_all_from(&connection, message.as_bytes());
    }
    task_scope(1) ending { await end_line(&connection); }
    await connection.shutdown(std.net::shutdown_direction::write);
    std.bufio::reader<std.net::unix_stream> lines =
        std.bufio::reader<std.net::unix_stream>::create(move connection, 4096usize);
    std.string::string reply = std.string::create();
    Flag got = {.value = false};
    task_scope(1) input { got.value = await lines.read_line(&reply); }
    drop lines;
    if (got.value == false) { return std.string::from_str("no answer\n"); }
    return f"{reply}\n";
}

module example.streams.main;
import std.console;

struct CommandStorage1 { std.net::datagram value; };
struct CommandStorage2 { usize value; };
struct CommandStorage3 { usize value; };
struct CommandStorage4 { usize value; };
struct CommandStorage5 { std.net::datagram value; };

// Output and exit status shared by command and error branches.
struct CommandResponse { std.string::string output; };

error Usage { std.string::string message; };

std.net::socket_address loopback() throws std.net::address_error {
    return std.net::socket_address { .address = std.net::parse_ip("127.0.0.1"), .port = 0u16, .scope_id = 0u32 };
}

// Fill the buffer from standard input until it is full or the input ends.
@scoped
async usize collect(bytes* buffer) throws std.io::io_error, std.async::start_error {
    std.io::input input = std.io::stdin();
    usize total = 0usize;
    usize capacity = len(*buffer);
    task_scope(1) reader {
        while (total < capacity) {
            u8[] window = std.array::as_slice_mut(buffer);
            usize count = await input.read_into(window[total..capacity]);
            if (count == 0usize) { break; }
            total += count;
        }
    }
    await (move input).close();
    return total;
}

// Store the payload in a file with one partial write and one complete write, then read it back.
@scoped
async usize through_file(std.fs::path* path, const bytes* payload, usize length, bytes* copy)
    throws std.fs::fs_error, std.io::io_error, std.async::start_error {
    std.fs::open_file_options writing = std.fs::open_file_options { .access = std.fs::access::write,
        .create = std.fs::create_mode::open_or_create, .truncate = true, .append = false,
        .follow_final_symlink = false };
    std.fs::file file = await path->open_file(writing);
    task_scope(1) writer {
        const u8[] head = std.array::as_slice(payload);
        usize written = await file.write_from(head[0usize..length]);
        const u8[] rest = std.array::as_slice(payload);
        await std.fs::write_all_from(&file, rest[written..length]);
    }
    await file.flush();
    await (move file).close();
    std.fs::open_file_options reading = std.fs::open_file_options { .access = std.fs::access::read,
        .create = std.fs::create_mode::existing, .truncate = false, .append = false,
        .follow_final_symlink = false };
    std.fs::file source = await path->open_file(reading);
    usize total = 0usize;
    usize capacity = len(*copy);
    task_scope(1) loader {
        while (total < capacity) {
            u8[] window = std.array::as_slice_mut(copy);
            usize count = await std.fs::read_into(&source, window[total..capacity]);
            if (count == 0usize) { break; }
            total += count;
        }
    }
    await (move source).close();
    return total;
}

// Send the payload through a loopback TCP connection and receive it into the copy buffer.
@scoped
async usize through_tcp(const bytes* payload, usize length, bytes* copy)
    throws std.net::address_error, std.net::net_error, std.async::start_error {
    std.net::socket_address local = loopback();
    std.net::listen_options options = { .backlog = 4u32, .reuse_address = true, .v6_only = false };
    std.net::tcp_listener listener = await local.listen(options);
    std.net::socket_address endpoint = listener.local_address();
    std.net::tcp_stream client = await endpoint.connect();
    std.net::tcp_connection connection = await listener.accept();
    await (move listener).close();
    usize total = 0usize;
    usize capacity = len(*copy);
    task_scope(2) exchange {
        const u8[] head = std.array::as_slice(payload);
        usize sent = await client.write_from(head[0usize..length]);
        const u8[] rest = std.array::as_slice(payload);
        await std.net::tcp_write_all_from(&client, rest[sent..length]);
        await client.shutdown(std.net::shutdown_direction::write);
        while (total < capacity) {
            u8[] window = std.array::as_slice_mut(copy);
            usize count = await std.net::tcp_read_into(&connection.stream, window[total..capacity]);
            if (count == 0usize) { break; }
            total += count;
        }
    }
    await (move client).close();
    drop connection;
    return total;
}

// Send one datagram and receive it into the copy buffer, reporting the peer and truncation.
@scoped
async std.net::datagram through_udp(const bytes* payload, usize length, bytes* copy)
    throws std.net::address_error, std.net::net_error, std.async::start_error {
    std.net::socket_address local = loopback();
    std.net::udp_socket sender = await local.bind(false);
    std.net::udp_socket receiver = await local.bind(false);
    std.net::socket_address destination = receiver.local_address();
    std.net::socket_address origin = sender.local_address();
    CommandStorage1 state_received = {.value = std.net::datagram { .count = 0usize, .peer = origin, .truncated = false }};
    task_scope(2) exchange {
        auto arrival = std.net::udp_receive_into(&receiver, std.array::as_slice_mut(copy));
        const u8[] message = std.array::as_slice(payload);
        await sender.send_from(destination, message[0usize..length]);
        state_received.value = await move arrival;
    }
    await (move sender).close();
    await (move receiver).close();
    return state_received.value;
}

async i32 main(const str[] arguments) {
    try {
        CommandResponse response = {.output = std.string::create()};
        try {
            if (len(arguments) == 1usize) {
                await std.console::print(std.string::from_str("streams FILE CAPACITY\n"));
                return 0;
            }
            throw (len(arguments) != 3usize) Usage { .message = std.string::from_str("streams FILE CAPACITY") };
            usize capacity = std.convert::parse_usize(arguments[2], 10u32);
            throw (capacity == 0usize || capacity > 65536usize) Usage { .message = std.string::from_str("capacity must be in 1..65536") };
            std.fs::path path = std.fs::path_from_utf8(arguments[1]);
            bytes payload = std.alloc::bytes(capacity, 0u8);
            CommandStorage2 state_length = {.value = 0usize};
            // Each stage borrows the buffers through its own group, which returns them on exit.
            task_scope(1) input_stage { state_length.value = await collect(&payload); }
            const u8[] input_view = payload.as_slice();
            u32 input_checksum = std.hash::crc32(input_view[0usize..state_length.value]);
            bytes file_copy = std.alloc::bytes(capacity, 0u8);
            CommandStorage3 state_file_length = {.value = 0usize};
            task_scope(1) file_stage { state_file_length.value = await through_file(&path, &payload, state_length.value, &file_copy); }
            const u8[] file_view = file_copy.as_slice();
            u32 file_checksum = std.hash::crc32(file_view[0usize..state_file_length.value]);
            bytes tcp_copy = std.alloc::bytes(capacity, 0u8);
            CommandStorage4 state_tcp_length = {.value = 0usize};
            task_scope(1) tcp_stage { state_tcp_length.value = await through_tcp(&payload, state_length.value, &tcp_copy); }
            const u8[] tcp_view = tcp_copy.as_slice();
            u32 tcp_checksum = std.hash::crc32(tcp_view[0usize..state_tcp_length.value]);
            bytes udp_copy = std.alloc::bytes(capacity / 2usize + 1usize, 0u8);
            CommandStorage5 state_part = {.value = std.net::datagram { .count = 0usize, .peer = loopback(), .truncated = false }};
            task_scope(1) udp_stage { state_part.value = await through_udp(&payload, state_length.value, &udp_copy); }
            const u8[] udp_view = udp_copy.as_slice();
            u32 udp_checksum = std.hash::crc32(udp_view[0usize..state_part.value.count]);
            usize udp_count = state_part.value.count;
            bool udp_truncated = state_part.value.truncated;
            response.output = f"stdin={state_length.value} crc32={input_checksum}\nfile={state_file_length.value} crc32={file_checksum}\ntcp={state_tcp_length.value} crc32={tcp_checksum}\nudp={udp_count} truncated={udp_truncated} crc32={udp_checksum}\n";
        } catch (Usage failure) { response.output = f"{failure.message}\n"; await std.console::print(core::replace(&response.output, std.string::create())); return 64; }
        await std.console::print(core::replace(&response.output, std.string::create()));
        return 0;
    } catch (std.convert::parse_error failure) { return 65; }
    catch (std.fs::path_error failure) { return 65; }
    catch (std.fs::fs_error failure) { return 66; }
}

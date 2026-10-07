module example.ipc.datagram;
import std.console;

struct Seen { std.net::unix_message value; };

/* One datagram to the socket at the path, from an unnamed socket connected to it. */
async std.string::string notify(std.string::string path, std.string::string message)
    throws std.error::fault {
    str where = path;
    std.net::unix_datagram socket = await std.net::unix_datagram_connect(where);
    const u8[] payload = message;
    usize length = len(payload);
    task_scope(1) send { await socket.send_from(payload); }
    await (move socket).close();
    return f"sent {length}\n";
}

/* Receive `count` datagrams at the path into a buffer of `capacity` bytes; each line reports
   the prefix length, whether the datagram was cut and the CRC-32 of the prefix. */
async std.string::string collect(std.string::string path, u32 count, usize capacity)
    throws std.error::fault {
    str where = path;
    std.net::unix_datagram socket = await std.net::unix_datagram_bind(where, true);
    await std.console::print(f"collecting {path}\n");
    std.string::string output = std.string::create();
    for (u32 index = 0u32; index < count; index += 1u32) {
        bytes buffer = std.alloc::bytes(capacity, 0u8);
        Seen seen = {.value = {.count = 0usize, .truncated = false}};
        task_scope(1) receive {
            seen.value = await std.net::unix_receive_into(&socket, buffer.as_slice_mut());
        }
        const u8[] contents = buffer.as_slice();
        u32 checksum = std.hash::crc32(contents[0usize..seen.value.count]);
        std.string::string line =
            f"count={seen.value.count} truncated={seen.value.truncated} crc32={checksum}\n";
        output.append(line);
    }
    await std.net::unix_datagram_close(move socket);
    return move output;
}

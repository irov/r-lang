module test.codegen.std_bufio;
import std.stream;
import std.bufio;

/* R-SLIB-BUFIO-0001..0003 (M18): buffered readers over standard input, a file, a TCP
   connection and an owner of a stream interface, and buffered writers. The first argument names
   a scratch file; standard input holds "first\r\nsecond\n\nlast". */

std.fs::open_file_options writing() {
    return std.fs::open_file_options { .access = std.fs::access::write,
        .create = std.fs::create_mode::open_or_create, .truncate = true, .append = false,
        .follow_final_symlink = false };
}

std.fs::open_file_options reading() {
    return std.fs::open_file_options { .access = std.fs::access::read,
        .create = std.fs::create_mode::existing, .truncate = false, .append = false,
        .follow_final_symlink = false };
}

bool same(const std.string::string* text, str expected) {
    return std.bytes::equal(text->as_bytes(), expected);
}

/* Lines of standard input: a carriage return before the line feed, an empty line and a last
   line without a line feed. */
async i32 input_lines() throws std.error::fault {
    std.bufio::reader<std.io::input> lines =
        std.bufio::reader<std.io::input>::create(std.io::stdin(), 8usize);
    std.string::string line = std.string::create();
    u32 count = 0u32;
    i32 status = 0;
    task_scope(1) io {
        while (await lines.read_line(&line) == true) {
            count += 1u32;
            if (count == 1u32 && same(&line, "first") == false) { status = 1; }
            if (count == 2u32 && same(&line, "second") == false) { status = 2; }
            if (count == 3u32 && std.string::len(&line) != 0usize) { status = 3; }
            if (count == 4u32 && same(&line, "last") == false) { status = 4; }
        }
        bool again = await lines.read_line(&line);
        if (again == true) { status = 5; }
    }
    if (count != 4u32 && status == 0) { status = 6; }
    return status;
}

/* A writer of capacity 8: small writes gather, a large one goes directly, flush hands on the
   rest; bytes left in a destroyed writer are discarded. */
async i32 write_file(std.string::string name) throws std.error::fault {
    std.fs::path path = std.fs::path_from_utf8(name.as_str());
    std.fs::file file = await path.open_file(writing());
    std.bufio::writer<std.fs::file> out = std.bufio::writer<std.fs::file>::create(move file, 8usize);
    std.string::string large = std.string::from_str("0123456789ab");
    bytes invalid = std.alloc::bytes(2usize, 255u8);
    i32 status = 0;
    task_scope(1) io {
        await out.write_str("HDR1");
        if (out.buffered() != 4usize) { status = 10; }
        await out.write(large.as_bytes());
        if (out.buffered() != 0usize) { status = 11; }
        await out.write_str(";key;value\n");
        await out.write(invalid.as_slice());
        await out.write_str(" bad\n");
        await out.write_str("0123456789ABCDEF\ntail");
        await out.flush();
        if (out.buffered() != 0usize) { status = 12; }
        await out.write_str("lost");
    }
    return status;
}

/* The file written above: an exact header, delimited fields, plain reads, an invalid line, a
   line longer than the capacity and an end in the middle of an exact read. */
async i32 read_file(std.string::string name) throws std.error::fault {
    std.fs::path path = std.fs::path_from_utf8(name.as_str());
    std.fs::file source = await path.open_file(reading());
    std.bufio::reader<std.fs::file> input =
        std.bufio::reader<std.fs::file>::create(move source, 16usize);
    bytes header = std.alloc::bytes(4usize, 0u8);
    bytes field = std.alloc::bytes(0usize, 0u8);
    bytes chunk = std.alloc::bytes(12usize, 0u8);
    std.string::string line = std.string::create();
    i32 status = 0;
    task_scope(1) io {
        bool whole = await input.read_exact(header.as_slice_mut());
        if (whole == false || header[3] != 49u8) { status = 20; }
        usize plain = await input.read_into(chunk.as_slice_mut());
        if (plain != 12usize || chunk[11] != 98u8) { status = 21; }
        if (await input.read_until(59u8, &field) != 1usize) { status = 22; }
        if (await input.read_until(59u8, &field) != 4usize) { status = 22; }
        if (len(field) != 5usize) { status = 22; }
        bool rest = await input.read_line(&line);
        if (rest == false || same(&line, "value") == false) { status = 23; }
        if (input.buffered() == 0usize) { status = 24; }
    }
    try {
        task_scope(1) io {
            bool invalid = await input.read_line(&line);
            invalid as void;
        }
        if (status == 0) { status = 25; }
    } catch (std.string::string_error failure) {
        if (std.string::len(&line) != 0usize && status == 0) { status = 26; }
    }
    try {
        task_scope(1) io {
            bool longer = await input.read_line(&line);
            longer as void;
        }
        if (status == 0) { status = 27; }
    } catch (std.io::io_error failure) {
        if (failure.code != std.io::error_code::resource_exhausted && status == 0) { status = 28; }
    }
    bytes remainder = std.alloc::bytes(32usize, 0u8);
    try {
        task_scope(1) io {
            bool full = await input.read_exact(remainder.as_slice_mut());
            full as void;
        }
        if (status == 0) { status = 29; }
    } catch (std.bits::read_error failure) {
        if ((failure.code != std.bits::read_error_code::unexpected_end ||
             failure.byte_index != 21usize) && status == 0) {
            status = 30;
        }
    }
    return status;
}

std.net::socket_address loopback() throws std.net::address_error {
    return std.net::socket_address { .address = std.net::parse_ip("127.0.0.1"), .port = 0u16,
                                     .scope_id = 0u32 };
}

/* Replies to every line with its length through a buffered writer of the connection. */
async u32 serve(std.net::tcp_connection connection) throws std.error::fault {
    std.bufio::reader<std.net::tcp_connection> lines =
        std.bufio::reader<std.net::tcp_connection>::create(move connection, 32usize);
    std.string::string line = std.string::create();
    u32 served = 0u32;
    task_scope(1) io {
        while (await lines.read_line(&line) == true) {
            usize length = std.string::len(&line);
            std.string::string reply = f"{length}\n";
            task_scope(1) write { await lines.source.write_all_from(reply.as_bytes()); }
            served += 1u32;
        }
    }
    task_scope(1) done { await lines.source.shutdown(); }
    return served;
}

async i32 network() throws std.error::fault {
    std.net::socket_address local = loopback();
    std.net::listen_options options = { .backlog = 4u32, .reuse_address = true, .v6_only = false };
    std.net::tcp_listener listener = await local.listen(options);
    std.net::socket_address endpoint = listener.local_address();
    std.net::tcp_stream client = await endpoint.connect();
    std.net::tcp_connection connection = await listener.accept();
    await (move listener).close();
    own dyn(std.stream::Stream)* channel = new std.net::tcp_stream(move client);
    std.bufio::reader<own dyn(std.stream::Stream)*> replies =
        std.bufio::reader<own dyn(std.stream::Stream)*>::create(move channel, 16usize);
    std.string::string reply = std.string::create();
    std.string::reserve(&reply, 8usize);
    std.string::string all = std.string::create();
    std.string::reserve(&all, 16usize);
    u32 served = 0u32;
    task_scope(1) exchange {
        auto server = serve(move connection);
        std.string::string request = std.string::from_str("ping\nhello world\n\n");
        task_scope(1) io {
            await replies.source.write_all_from(request.as_bytes());
            await replies.source.shutdown();
            while (await replies.read_line(&reply) == true) {
                std.string::append_str(&all, reply.as_str());
                std.string::append_str(&all, ",");
            }
        }
        served += await move server;
    }
    if (served != 3u32) { return 40; }
    if (same(&all, "4,11,0,") == false) { return 41; }
    return 0;
}

async i32 main(const str[] arguments) {
    if (len(arguments) != 2usize) { return 90; }
    std.string::string name = std.string::from_str(arguments[1]);
    std.string::string again = std.string::from_str(arguments[1]);
    i32 written = await write_file(move name);
    if (written != 0) {
        drop again;
        return written;
    }
    i32 read = await read_file(move again);
    if (read != 0) { return read; }
    i32 lines = await input_lines();
    if (lines != 0) { return lines; }
    return await network();
}

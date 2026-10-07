module test.codegen.std_stream;
import std.stream;

/* R-SLIB-STREAM-0001..0004 (M18): the stream traits over program types, standard streams,
   files, TCP streams and connections, a duplex of standard input and output, and owners of
   interfaces. The first argument names a scratch file; standard input holds "stream input". */

/* A writer that accepts at most three bytes a write, so write_all_from repeats write_from. */
struct Chunked { atomic u32 calls; atomic u64 sum; };

impl std.stream::Writer for Chunked {
    @scoped
    async usize write_from(const Chunked* this, const u8[] source) throws std.error::fault {
        usize count = len(source);
        if (count > 3usize) { count = 3usize; }
        u64 sum = 0u64;
        for (usize index = 0usize; index < count; index += 1usize) { sum += source[index] as u64; }
        core::atomic_fetch_add(&this->calls, 1u32, core::memory_order::relaxed) as void;
        core::atomic_fetch_add(&this->sum, sum, core::memory_order::relaxed) as void;
        return count;
    }
};

/* A writer that never accepts a byte: the default write_all_from reports broken_pipe. */
struct Stuck { u32 tag; };

impl std.stream::Writer for Stuck {
    @scoped
    async usize write_from(const Stuck* this, const u8[] source) throws std.error::fault {
        return 0usize;
    }
};

/* A reader of a fixed text that reports a program failure after it, built as a standard error. */
struct Fixed { bytes text; atomic usize position; bool failing; };

impl std.stream::Reader for Fixed {
    @scoped
    async usize read_into(const Fixed* this, u8[] target) throws std.error::fault {
        usize at = core::atomic_load(&this->position, core::memory_order::relaxed);
        usize size = len(this->text);
        throw (at == size && this->failing == true)
            std.io::io_error {.code = std.io::error_code::other, .native_code = 42i64};
        usize count = size - at;
        if (count > len(target)) { count = len(target); }
        std.bytes::copy(target[0usize..count], this->text[at..at + count]) as void;
        core::atomic_store(&this->position, at + count, core::memory_order::relaxed);
        return count;
    }
};

bytes fixed_text() throws std.alloc::alloc_error {
    return (std.string::from_str("fixed text")).into_bytes();
}

/* Every byte of any reader, through a generic constraint. */
@generic<R: std.stream::Reader>
@scoped
async usize drain(const R* source, u8[] buffer) throws std.error::fault {
    usize total = 0usize;
    task_scope(1) io {
        while (true) {
            usize count = await source->read_into(buffer);
            if (count == 0usize) { break; }
            total += count;
        }
    }
    return total;
}

/* Every byte of the reader an interface borrows. */
@scoped
async usize drain_any(const dyn(std.stream::Reader)* source, u8[] buffer)
    throws std.error::fault {
    usize total = 0usize;
    task_scope(1) io {
        while (true) {
            usize count = await source->read_into(buffer);
            if (count == 0usize) { break; }
            total += count;
        }
    }
    return total;
}

/* An interface written with the capabilities its trait requires is the same interface. */
@scoped
async usize drain_spelled(const dyn(std.stream::Reader & send & sync)* source, u8[] buffer)
    throws std.error::fault {
    usize total = 0usize;
    task_scope(1) io { total += await drain_any(source, buffer); }
    return total;
}

@scoped
async void emit(const dyn(std.stream::Writer)* sink, const u8[] data) throws std.error::fault {
    task_scope(1) io {
        await sink->write_all_from(data);
        await sink->flush();
    }
}

/* M18-1: a borrowed file flushed through its method in a function that nothing calls. */
@scoped
async void unused_flush(const std.fs::file* file) throws std.error::fault {
    await file->flush();
}

async i32 program_types() throws std.error::fault {
    Chunked chunked = {.calls = 0u32, .sum = 0u64};
    Stuck stuck = {.tag = 1u32};
    Fixed fixed = {.text = fixed_text(), .position = 0usize, .failing = false};
    Fixed failing = {.text = fixed_text(), .position = 10usize, .failing = true};
    bytes buffer = std.alloc::bytes(4usize, 0u8);
    std.string::string letters = std.string::from_str("abcdefgh");
    std.string::string single = std.string::from_str("x");
    i32 status = 0;
    task_scope(1) io {
        await emit(&chunked, letters);
        await chunked.shutdown();
        usize read = await drain(&fixed, buffer.as_slice_mut());
        if (read != 10usize) { status = 1; }
    }
    if (core::atomic_load(&chunked.calls, core::memory_order::relaxed) != 3u32) { status = 2; }
    if (core::atomic_load(&chunked.sum, core::memory_order::relaxed) != 804u64) { status = 3; }
    try {
        task_scope(1) io { await emit(&stuck, single); }
        if (status == 0) { status = 4; }
    } catch (std.io::io_error failure) {
        if (failure.code != std.io::error_code::broken_pipe || failure.native_code != 0i64) {
            status = 5;
        }
    }
    try {
        task_scope(1) io {
            usize none = await drain(&failing, buffer.as_slice_mut());
            none as void;
        }
        if (status == 0) { status = 6; }
    } catch (std.error::fault failure) {
        std.error::error portable = std.error::from_fault(failure);
        if (portable.domain != std.error::domain::io || portable.native_code != 42i64) {
            status = 7;
        }
    }
    return status;
}

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

async i32 files(std.string::string name) throws std.error::fault {
    std.fs::path path = std.fs::path_from_utf8(name);
    std.fs::file file = await path.open_file(writing());
    bytes buffer = std.alloc::bytes(8usize, 0u8);
    std.string::string text = std.string::from_str("file bytes");
    i32 status = 0;
    task_scope(1) io {
        await emit(&file, text);
        await file.shutdown();
    }
    await (move file).close();
    std.fs::file source = await path.open_file(reading());
    task_scope(1) io {
        usize read = await drain_spelled(&source, buffer.as_slice_mut());
        if (read != 10usize) { status = 10; }
    }
    return status;
}

std.net::socket_address loopback() throws std.net::address_error {
    return std.net::socket_address { .address = std.net::parse_ip("127.0.0.1"), .port = 0u16,
                                     .scope_id = 0u32 };
}

/* The client owns its stream through an interface; the server reads the accepted connection. */
async i32 network() throws std.error::fault {
    std.net::socket_address local = loopback();
    std.net::listen_options options = { .backlog = 4u32, .reuse_address = true, .v6_only = false };
    std.net::tcp_listener listener = await local.listen(options);
    std.net::socket_address endpoint = listener.local_address();
    std.net::tcp_stream client = await endpoint.connect();
    std.net::tcp_connection connection = await listener.accept();
    await (move listener).close();
    own dyn(std.stream::Stream)* channel = new std.net::tcp_stream(move client);
    bytes buffer = std.alloc::bytes(16usize, 0u8);
    std.string::string text = std.string::from_str("over tcp");
    i32 status = 0;
    task_scope(1) io {
        await channel->write_all_from(text);
        await channel->flush();
        await channel->shutdown();
        usize read = await drain(&connection, buffer.as_slice_mut());
        if (read != 8usize) { status = 20; }
        await connection.shutdown();
        usize back = await drain(&channel, buffer.as_slice_mut());
        if (back != 0usize) { status = 21; }
    }
    return status;
}

/* Standard input and output as one stream, and owners of reader and writer interfaces. */
async i32 console() throws std.error::fault {
    std.stream::duplex<std.io::input, std.io::output> terminal = {.reader = std.io::stdin(),
                                                                   .writer = std.io::stdout()};
    bytes buffer = std.alloc::bytes(32usize, 0u8);
    i32 status = 0;
    task_scope(1) io {
        usize read = await drain(&terminal, buffer.as_slice_mut());
        if (read != 12usize) { status = 30; }
        await terminal.write_all_from(buffer[0usize..read]);
        await terminal.flush();
        await terminal.shutdown();
    }
    own dyn(std.stream::Writer)* out = new std.io::output(std.io::stderr());
    own dyn(std.stream::Reader)* fixed =
        new Fixed {.text = fixed_text(), .position = 6usize, .failing = false};
    task_scope(1) io {
        usize read = await drain(&fixed, buffer.as_slice_mut());
        if (read != 4usize) { status = 31; }
        await out.write_all_from(buffer[0usize..read]);
        await out.flush();
    }
    return status;
}

async i32 main(const str[] arguments) {
    if (len(arguments) != 2usize) { return 90; }
    std.string::string name = std.string::from_str(arguments[1]);
    i32 stored = await files(move name);
    if (stored != 0) { return stored; }
    i32 typed = await program_types();
    if (typed != 0) { return typed; }
    i32 connected = await network();
    if (connected != 0) { return connected; }
    return await console();
}

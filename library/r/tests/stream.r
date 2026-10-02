module tests.std.stream;
import std.test;
import std.stream;

// The tests of std.stream (Library R-SLIB-STREAM-0001..0004): the Reader, Writer and Stream
// traits over program types, files, TCP streams and connections, Unix-domain streams, a duplex
// and owners of interfaces, reached through generic constraints and interfaces. Run in test mode
// (Core R-FUNC-0025); the files and sockets are made in the current directory and removed.

struct Tally { atomic u32 calls; atomic u64 sum; };

/* A writer that accepts at most three bytes a write and counts them in a shared tally, so the
   default write_all_from repeats write_from. */
struct Chunked { arc Tally tally; };

impl std.stream::Writer for Chunked {
    @scoped
    async usize write_from(const Chunked* this, const u8[] source) throws std.error::fault {
        usize count = len(source);
        if (count > 3usize) { count = 3usize; }
        u64 sum = 0u64;
        for (usize index = 0usize; index < count; index += 1usize) { sum += source[index] as u64; }
        const Tally* tally = &*this->tally;
        core::atomic_fetch_add(&tally->calls, 1u32, core::memory_order::relaxed) as void;
        core::atomic_fetch_add(&tally->sum, sum, core::memory_order::relaxed) as void;
        return count;
    }
};

/* A writer that never accepts a byte. */
struct Stuck { u32 tag; };

impl std.stream::Writer for Stuck {
    @scoped
    async usize write_from(const Stuck* this, const u8[] source) throws std.error::fault {
        return 0usize;
    }
};

/* A reader of a fixed text; a failing one reports an I/O error with native code 42 at its end. */
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

protected Fixed fixed(str text, bool failing) throws std.alloc::alloc_error {
    std.string::string owned = std.string::from_str(text);
    return Fixed {.text = (move owned).into_bytes(), .position = 0usize, .failing = failing};
}

protected arc Tally new_tally() throws std.alloc::alloc_error {
    return new arc Tally {.calls = 0u32, .sum = 0u64};
}

protected u32 calls_of(const (arc Tally)* shared) {
    const Tally* counts = &**shared;
    return core::atomic_load(&counts->calls, core::memory_order::relaxed);
}

protected u64 sum_of(const (arc Tally)* shared) {
    const Tally* counts = &**shared;
    return core::atomic_load(&counts->sum, core::memory_order::relaxed);
}

/* Every byte of any reader, through a generic constraint, in reads of up to four bytes. */
@generic<R: std.stream::Reader>
@scoped
protected async usize drain(const R* source, bytes* out) throws std.error::fault {
    u8[4] chunk = {};
    usize total = 0usize;
    task_scope(1) io {
        while (true) {
            usize count = await source->read_into(&chunk);
            if (count == 0usize) { break; }
            out->append(chunk[0usize..count]);
            total += count;
        }
    }
    return total;
}

/* Every byte of the reader that an interface borrows. */
@scoped
protected async usize drain_any(const dyn(std.stream::Reader)* source, bytes* out)
    throws std.error::fault {
    u8[4] chunk = {};
    usize total = 0usize;
    task_scope(1) io {
        while (true) {
            usize count = await source->read_into(&chunk);
            if (count == 0usize) { break; }
            out->append(chunk[0usize..count]);
            total += count;
        }
    }
    return total;
}

/* Writes and flushes data through the interface of any writer. */
@scoped
protected async void emit(const dyn(std.stream::Writer)* sink, const u8[] data)
    throws std.error::fault {
    task_scope(1) io {
        await sink->write_all_from(data);
        await sink->flush();
    }
}

/* Writes data through a generic constraint and ends the write direction. */
@generic<W: std.stream::Writer>
@scoped
protected async void send(const W* sink, const u8[] data) throws std.error::fault {
    task_scope(1) io {
        await sink->write_all_from(data);
        await sink->flush();
        await sink->shutdown();
    }
}

/* Writes every byte that a stream reads back to it, then ends its write direction. */
@generic<S: std.stream::Stream>
@scoped
protected async usize echo(const S* link) throws std.error::fault {
    u8[4] chunk = {};
    usize total = 0usize;
    while (true) {
        usize count = 0usize;
        task_scope(1) input { count += await link->read_into(&chunk); }
        if (count == 0usize) { break; }
        task_scope(1) output { await link->write_all_from(chunk[0usize..count]); }
        total += count;
    }
    task_scope(1) done {
        await link->flush();
        await link->shutdown();
    }
    return total;
}

/* Removes a file of the current directory. */
@scoped
protected async void remove_here(str name) throws std.error::fault {
    std.fs::path here = std.fs::path_from_utf8(".");
    std.fs::path entry = std.fs::path_from_utf8(name);
    std.fs::directory root = await std.fs::open_directory(&here);
    await root.remove_file_beneath(&entry);
    await (move root).close();
}

@test
async void write_all_repeats_partial_writes() throws std.error::fault, std.test::failure {
    arc Tally shared = new_tally();
    Chunked chunked = {.tally = std.arc::clone(&shared)};
    std.string::string letters = std.string::from_str("abcdefgh");
    task_scope(1) io {
        await emit(&chunked, letters.as_bytes());
        const u8[] all = letters.as_bytes();
        await chunked.write_all_from(all[0usize..0usize]);
        await chunked.shutdown();
    }
    std.test::equal(calls_of(&shared), 3u32);
    std.test::equal(sum_of(&shared), 804u64);
}

@test
async void a_writer_that_takes_nothing_breaks_the_pipe() throws std.error::fault,
    std.test::failure {
    Stuck stuck = {.tag = 1u32};
    std.string::string single = std.string::from_str("x");
    try {
        task_scope(1) io { await emit(&stuck, single.as_bytes()); }
        std.test::fail("a writer that takes no byte cannot finish");
    } catch (std.io::io_error failure) {
        std.test::check(failure.code == std.io::error_code::broken_pipe, "broken_pipe");
        std.test::equal(failure.native_code, 0i64);
    }
}

@test
async void reads_through_constraints_and_interfaces() throws std.error::fault,
    std.test::failure {
    Fixed text = fixed("fixed text", false);
    bytes first = {};
    bytes rest = {};
    task_scope(1) io {
        usize read = await drain(&text, &first);
        std.test::equal(read, 10usize);
        usize after = await drain_any(&text, &rest);
        std.test::equal(after, 0usize);
    }
    std.test::check(std.bytes::equal(first.as_slice(), "fixed text"), "the text of the reader");
    Fixed other = fixed("through an interface", false);
    bytes second = {};
    task_scope(1) io {
        usize read = await drain_any(&other, &second);
        std.test::equal(read, 20usize);
    }
    std.test::check(std.bytes::equal(second.as_slice(), "through an interface"), "interface");
    Fixed failing = fixed("abc", true);
    bytes partial = {};
    try {
        task_scope(1) io {
            usize none = await drain(&failing, &partial);
            none as void;
        }
        std.test::fail("the reader fails at its end");
    } catch (std.io::io_error failure) {
        std.test::equal(failure.native_code, 42i64);
    }
    std.test::check(std.bytes::equal(partial.as_slice(), "abc"), "the bytes before the failure");
}

@test
async void a_duplex_is_one_stream() throws std.error::fault, std.test::failure {
    arc Tally shared = new_tally();
    std.stream::duplex<Fixed, Chunked> pair = {.reader = fixed("abcdefgh", false),
                                               .writer = Chunked {.tally = std.arc::clone(&shared)}};
    task_scope(1) io {
        usize copied = await echo(&pair);
        std.test::equal(copied, 8usize);
    }
    std.test::equal(calls_of(&shared), 4u32);
    std.test::equal(sum_of(&shared), 804u64);
}

@test
async void owners_of_interfaces_are_streams() throws std.error::fault, std.test::failure {
    arc Tally shared = new_tally();
    own dyn(std.stream::Reader)* source = new Fixed(fixed("owned reader", false));
    own dyn(std.stream::Writer)* sink = new Chunked {.tally = std.arc::clone(&shared)};
    own dyn(std.stream::Writer)* console = new std.io::output(std.io::stdout());
    bytes content = {};
    task_scope(1) io {
        usize read = await drain(&source, &content);
        std.test::equal(read, 12usize);
        await send(&sink, content.as_slice());
        await send(&console, content[0usize..0usize]);
    }
    std.test::equal(calls_of(&shared), 4u32);
    arc Tally again = new_tally();
    std.stream::duplex<Fixed, Chunked> pair = {.reader = fixed("xyz", false),
                                               .writer = Chunked {.tally = std.arc::clone(&again)}};
    own dyn(std.stream::Stream)* joined = new std.stream::duplex<Fixed, Chunked>(move pair);
    task_scope(1) io {
        usize copied = await echo(&joined);
        std.test::equal(copied, 3usize);
    }
    std.test::equal(calls_of(&again), 1u32);
    std.test::equal(sum_of(&again), 363u64);
}

protected std.fs::open_file_options writing() {
    return std.fs::open_file_options {.access = std.fs::access::write,
        .create = std.fs::create_mode::open_or_create, .truncate = true, .append = false,
        .follow_final_symlink = false};
}

protected std.fs::open_file_options reading() {
    return std.fs::open_file_options {.access = std.fs::access::read,
        .create = std.fs::create_mode::existing, .truncate = false, .append = false,
        .follow_final_symlink = false};
}

@test
async void files_are_streams() throws std.error::fault, std.test::failure {
    std.fs::path path = std.fs::path_from_utf8("tests_std_stream_file.tmp");
    std.fs::file file = await path.open_file(writing());
    std.string::string text = std.string::from_str("file bytes");
    task_scope(1) io {
        await emit(&file, text.as_bytes());
        await file.shutdown();
    }
    await (move file).close();
    std.fs::file source = await path.open_file(reading());
    bytes content = {};
    task_scope(1) io {
        usize read = await drain(&source, &content);
        std.test::equal(read, 10usize);
    }
    await (move source).close();
    std.test::check(std.bytes::equal(content.as_slice(), "file bytes"), "the bytes of the file");
    task_scope(1) cleanup { await remove_here("tests_std_stream_file.tmp"); }
}

protected std.net::socket_address loopback() throws std.net::address_error {
    return std.net::socket_address {.address = std.net::parse_ip("127.0.0.1"), .port = 0u16,
                                    .scope_id = 0u32};
}

@test
async void tcp_streams_and_connections_are_streams() throws std.error::fault,
    std.test::failure {
    std.net::socket_address local = loopback();
    std.net::listen_options options = {.backlog = 4u32, .reuse_address = true, .v6_only = false};
    std.net::tcp_listener listener = await local.listen(options);
    std.net::socket_address endpoint = listener.local_address();
    std.net::tcp_stream client = await endpoint.connect();
    std.net::tcp_connection connection = await listener.accept();
    await (move listener).close();
    own dyn(std.stream::Stream)* channel = new std.net::tcp_stream(move client);
    std.string::string request = std.string::from_str("over tcp");
    std.string::string reply = std.string::from_str("reply");
    bytes received = {};
    bytes replied = {};
    task_scope(1) io {
        await send(&channel, request.as_bytes());
        usize read = await drain(&connection, &received);
        std.test::equal(read, 8usize);
        await send(&connection, reply.as_bytes());
        usize back = await drain(&channel, &replied);
        std.test::equal(back, 5usize);
    }
    std.test::check(std.bytes::equal(received.as_slice(), "over tcp"), "the request");
    std.test::check(std.bytes::equal(replied.as_slice(), "reply"), "the reply");
}

@test
async void unix_streams_are_streams() throws std.error::fault, std.test::failure {
    std.net::unix_listener listener =
        await std.net::unix_listen("tests_std_stream.sock", 4u32, true);
    std.net::unix_stream client = await std.net::unix_connect("tests_std_stream.sock");
    std.net::unix_stream server = await listener.accept();
    await (move listener).close();
    std.string::string request = std.string::from_str("over a unix socket");
    bytes received = {};
    bytes replied = {};
    task_scope(1) io {
        await send(&client, request.as_bytes());
        usize echoed = await echo(&server);
        std.test::equal(echoed, 18usize);
        usize back = await drain_any(&client, &replied);
        std.test::equal(back, 18usize);
        usize after = await drain(&server, &received);
        std.test::equal(after, 0usize);
    }
    std.test::check(std.bytes::equal(replied.as_slice(), "over a unix socket"), "the echo");
    task_scope(1) cleanup { await remove_here("tests_std_stream.sock"); }
}

module std.stream;

/* R-SLIB-STREAM-0001: a source of bytes. read_into places at most len(target) bytes at the start
   of target and returns their count; zero means the end of the stream or an empty target. A
   read runs under the deadline of the calling task (Core R-STMT-0019). */
trait Reader : send & sync {
    @scoped
    async usize read_into(const Self* this, u8[] target) throws std.error::fault;
};

/* R-SLIB-STREAM-0001: a sink of bytes. write_from writes a nonempty prefix of a nonempty source
   and returns its length; write_all_from writes every byte; flush hands the written bytes to the
   destination; shutdown ends the write direction. */
trait Writer : send & sync {
    @scoped
    async usize write_from(const Self* this, const u8[] source) throws std.error::fault;

    /* Repeats write_from on the rest of the source; a writer that accepts no byte of a
       nonempty rest reports broken_pipe instead of looping. */
    @scoped
    async void write_all_from(const Self* this, const u8[] source) throws std.error::fault {
        usize written = 0usize;
        task_scope(1) io {
            while (written < len(source)) {
                usize count = await this->write_from(source[written..len(source)]);
                throw (count == 0usize)
                    std.io::io_error {.code = std.io::error_code::broken_pipe, .native_code = 0i64};
                written += count;
            }
        }
    }

    /* A writer without buffers of its own has nothing to hand on. */
    @scoped
    async void flush(const Self* this) throws std.error::fault {}

    /* A writer whose write direction ends only with its handle has nothing to end. */
    @scoped
    async void shutdown(const Self* this) throws std.error::fault {}
};

/* R-SLIB-STREAM-0001: both directions of one connection, such as a TCP stream or a file. */
trait Stream : Reader & Writer {};

/* R-SLIB-STREAM-0002: standard input and the output pipes of a child process. */
impl Reader for std.io::input {
    @scoped
    async usize read_into(const std.io::input* this, u8[] target) throws std.error::fault {
        task_scope(1) io { return await std.io::read_into(this, target); }
    }
};

/* R-SLIB-STREAM-0002: standard output, standard error and the input pipe of a child process;
   their write direction ends when the handle is closed, so shutdown flushes. */
impl Writer for std.io::output {
    @scoped
    async usize write_from(const std.io::output* this, const u8[] source) throws std.error::fault {
        task_scope(1) io { return await std.io::write_from(this, source); }
    }
    @scoped
    async void write_all_from(const std.io::output* this, const u8[] source)
        throws std.error::fault {
        task_scope(1) io { await std.io::write_all_from(this, source); }
    }
    @scoped
    async void flush(const std.io::output* this) throws std.error::fault {
        await std.io::flush(this);
    }
    @scoped
    async void shutdown(const std.io::output* this) throws std.error::fault {
        await std.io::flush(this);
    }
};

/* R-SLIB-STREAM-0002: a file reads and writes at its shared position; shutdown flushes. */
impl Reader for std.fs::file {
    @scoped
    async usize read_into(const std.fs::file* this, u8[] target) throws std.error::fault {
        task_scope(1) io { return await std.fs::read_into(this, target); }
    }
};

impl Writer for std.fs::file {
    @scoped
    async usize write_from(const std.fs::file* this, const u8[] source) throws std.error::fault {
        task_scope(1) io { return await std.fs::write_from(this, source); }
    }
    @scoped
    async void write_all_from(const std.fs::file* this, const u8[] source)
        throws std.error::fault {
        task_scope(1) io { await std.fs::write_all_from(this, source); }
    }
    @scoped
    async void flush(const std.fs::file* this) throws std.error::fault {
        await std.fs::flush(this);
    }
    @scoped
    async void shutdown(const std.fs::file* this) throws std.error::fault {
        await std.fs::flush(this);
    }
};

impl Stream for std.fs::file {};

/* R-SLIB-STREAM-0002: a TCP stream; its bytes leave with each write, so flush has nothing to
   do, and shutdown ends the write direction, which the peer reads as the end of the stream. */
impl Reader for std.net::tcp_stream {
    @scoped
    async usize read_into(const std.net::tcp_stream* this, u8[] target) throws std.error::fault {
        task_scope(1) io { return await std.net::tcp_read_into(this, target); }
    }
};

impl Writer for std.net::tcp_stream {
    @scoped
    async usize write_from(const std.net::tcp_stream* this, const u8[] source)
        throws std.error::fault {
        task_scope(1) io { return await std.net::tcp_write_from(this, source); }
    }
    @scoped
    async void write_all_from(const std.net::tcp_stream* this, const u8[] source)
        throws std.error::fault {
        task_scope(1) io { await std.net::tcp_write_all_from(this, source); }
    }
    @scoped
    async void shutdown(const std.net::tcp_stream* this) throws std.error::fault {
        await std.net::tcp_shutdown(this, std.net::shutdown_direction::write);
    }
};

impl Stream for std.net::tcp_stream {};

/* R-SLIB-STREAM-0002: an accepted connection reads and writes through its stream and keeps
   the address of its peer. */
impl Reader for std.net::tcp_connection {
    @scoped
    async usize read_into(const std.net::tcp_connection* this, u8[] target)
        throws std.error::fault {
        task_scope(1) io { return await std.net::tcp_read_into(&this->stream, target); }
    }
};

impl Writer for std.net::tcp_connection {
    @scoped
    async usize write_from(const std.net::tcp_connection* this, const u8[] source)
        throws std.error::fault {
        task_scope(1) io { return await std.net::tcp_write_from(&this->stream, source); }
    }
    @scoped
    async void write_all_from(const std.net::tcp_connection* this, const u8[] source)
        throws std.error::fault {
        task_scope(1) io { await std.net::tcp_write_all_from(&this->stream, source); }
    }
    @scoped
    async void shutdown(const std.net::tcp_connection* this) throws std.error::fault {
        await std.net::tcp_shutdown(&this->stream, std.net::shutdown_direction::write);
    }
};

impl Stream for std.net::tcp_connection {};

/* R-SLIB-STREAM-0002, R-SLIB-NET-0015: a Unix-domain stream reads and writes through its scoped
   operations and ends its write direction with a half-close. */
impl Reader for std.net::unix_stream {
    @scoped
    async usize read_into(const std.net::unix_stream* this, u8[] target) throws std.error::fault {
        task_scope(1) io { return await std.net::unix_read_into(this, target); }
    }
};

impl Writer for std.net::unix_stream {
    @scoped
    async usize write_from(const std.net::unix_stream* this, const u8[] source)
        throws std.error::fault {
        task_scope(1) io { return await std.net::unix_write_from(this, source); }
    }
    @scoped
    async void write_all_from(const std.net::unix_stream* this, const u8[] source)
        throws std.error::fault {
        task_scope(1) io { await std.net::unix_write_all_from(this, source); }
    }
    @scoped
    async void shutdown(const std.net::unix_stream* this) throws std.error::fault {
        await std.net::unix_shutdown(this, std.net::shutdown_direction::write);
    }
};

impl Stream for std.net::unix_stream {};

/* R-SLIB-STREAM-0003: one stream made of a reader and a writer, such as the output and input
   pipes of a child process or standard input and output. */
@generic<R: Reader, W: Writer>
struct duplex {
    R reader;
    W writer;
};

@generic<R: Reader, W: Writer>
impl Reader for duplex<R, W> {
    @scoped
    async usize read_into(const duplex<R, W>* this, u8[] target) throws std.error::fault {
        task_scope(1) io { return await this->reader.read_into(target); }
    }
};

@generic<R: Reader, W: Writer>
impl Writer for duplex<R, W> {
    @scoped
    async usize write_from(const duplex<R, W>* this, const u8[] source) throws std.error::fault {
        task_scope(1) io { return await this->writer.write_from(source); }
    }
    @scoped
    async void write_all_from(const duplex<R, W>* this, const u8[] source)
        throws std.error::fault {
        task_scope(1) io { await this->writer.write_all_from(source); }
    }
    @scoped
    async void flush(const duplex<R, W>* this) throws std.error::fault {
        task_scope(1) io { await this->writer.flush(); }
    }
    @scoped
    async void shutdown(const duplex<R, W>* this) throws std.error::fault {
        task_scope(1) io { await this->writer.shutdown(); }
    }
};

@generic<R: Reader, W: Writer>
impl Stream for duplex<R, W> {};

/* R-SLIB-STREAM-0004: an owner of an interface reads and writes through the member it owns, so
   a stream chosen when the program runs serves wherever a Reader, Writer or Stream is
   expected, such as the source of std.bufio::reader. */
impl Reader for own dyn(Reader)* {
    @scoped
    async usize read_into(const (own dyn(Reader)*)* this, u8[] target) throws std.error::fault {
        task_scope(1) io { return await (*this)->read_into(target); }
    }
};

impl Writer for own dyn(Writer)* {
    @scoped
    async usize write_from(const (own dyn(Writer)*)* this, const u8[] source)
        throws std.error::fault {
        task_scope(1) io { return await (*this)->write_from(source); }
    }
    @scoped
    async void write_all_from(const (own dyn(Writer)*)* this, const u8[] source)
        throws std.error::fault {
        task_scope(1) io { await (*this)->write_all_from(source); }
    }
    @scoped
    async void flush(const (own dyn(Writer)*)* this) throws std.error::fault {
        task_scope(1) io { await (*this)->flush(); }
    }
    @scoped
    async void shutdown(const (own dyn(Writer)*)* this) throws std.error::fault {
        task_scope(1) io { await (*this)->shutdown(); }
    }
};

impl Reader for own dyn(Stream)* {
    @scoped
    async usize read_into(const (own dyn(Stream)*)* this, u8[] target) throws std.error::fault {
        task_scope(1) io { return await (*this)->read_into(target); }
    }
};

impl Writer for own dyn(Stream)* {
    @scoped
    async usize write_from(const (own dyn(Stream)*)* this, const u8[] source)
        throws std.error::fault {
        task_scope(1) io { return await (*this)->write_from(source); }
    }
    @scoped
    async void write_all_from(const (own dyn(Stream)*)* this, const u8[] source)
        throws std.error::fault {
        task_scope(1) io { await (*this)->write_all_from(source); }
    }
    @scoped
    async void flush(const (own dyn(Stream)*)* this) throws std.error::fault {
        task_scope(1) io { await (*this)->flush(); }
    }
    @scoped
    async void shutdown(const (own dyn(Stream)*)* this) throws std.error::fault {
        task_scope(1) io { await (*this)->shutdown(); }
    }
};

impl Stream for own dyn(Stream)* {};

module std.bufio;
import std.stream;

/* R-SLIB-BUFIO-0001: a reader that takes the bytes of its source in reads of up to its capacity
   and hands them out as lines, delimited sequences, exact counts or plain reads. The unread
   bytes are buffer[start..end]. */
@generic<R: std.stream::Reader>
struct reader {
    R source;
    protected bytes buffer;
    protected usize start;
    protected usize end;
};

@generic<R: std.stream::Reader>
reader<R> reader<R>::create(R source, usize capacity) throws std.alloc::alloc_error {
    bytes buffer = std.alloc::bytes(capacity, 0u8);
    return reader<R> {.source = move source, .buffer = move buffer, .start = 0usize,
                      .end = 0usize};
}

/* The number of bytes read from the source and not yet handed out. */
@generic<R: std.stream::Reader>
usize reader<R>::buffered(const reader<R>* this) {
    return this->end - this->start;
}

/* R-SLIB-BUFIO-0004: ends the reader: appends the bytes read from the source and not yet
   handed out to unread and returns the source. */
@generic<R: std.stream::Reader>
R reader<R>::into_source(reader<R> this, bytes* unread) throws std.alloc::alloc_error {
    std.bytes::append(unread, this.buffer[this.start..this.end]);
    return match (move this) { case { .source = move taken }: move taken; };
}

/* Moves the unread bytes to the front and reads once into the free space after them; returns
   the number of bytes read, zero at the end of the source or when the buffer is full. */
@generic<R: std.stream::Reader>
@scoped
protected async usize reader<R>::fill(reader<R>* this) throws std.error::fault {
    usize unread = this->end - this->start;
    if (this->start != 0usize) {
        std.bytes::copy_within(this->buffer.as_slice_mut(), 0usize, this->start, unread) as void;
        this->start = 0usize;
        this->end = unread;
    }
    usize capacity = len(this->buffer);
    if (this->end == capacity) { return 0usize; }
    task_scope(1) io {
        usize count = await this->source.read_into(this->buffer[this->end..capacity]);
        this->end += count;
        return count;
    }
}

/* R-SLIB-BUFIO-0002: the next line without its terminator, a line feed and one carriage return
   before it. A last line without a line feed counts; false means the source has ended. */
@generic<R: std.stream::Reader>
@scoped
async bool reader<R>::read_line(reader<R>* this, std.string::string* line)
    throws std.error::fault {
    std.string::clear(line);
    usize scanned = 0usize;
    while (true) {
        o<usize> found = std.bytes::find(this->buffer[this->start + scanned..this->end], 10u8);
        switch (found) {
        case variant o::some(offset):
            usize first = this->start;
            usize stop = first + scanned + *offset;
            this->start = stop + 1usize;
            if (stop > first && this->buffer[stop - 1usize] == 13u8) { stop -= 1usize; }
            std.string::append_utf8(line, this->buffer[first..stop]);
            return true;
        case variant o::none:
            scanned = this->end - this->start;
        }
        throw (this->start == 0usize && this->end == len(this->buffer))
            std.io::io_error {.code = std.io::error_code::resource_exhausted, .native_code = 0i64};
        usize count = 0usize;
        task_scope(1) io { count += await this->fill(); }
        if (count == 0usize) {
            if (this->start == this->end) { return false; }
            usize first = this->start;
            this->start = this->end;
            std.string::append_utf8(line, this->buffer[first..this->end]);
            return true;
        }
    }
}

/* R-SLIB-BUFIO-0002: appends to out the bytes up to and including the next delimiter and
   returns their count; the last bytes without a delimiter count, zero means the source has
   ended. */
@generic<R: std.stream::Reader>
@scoped
async usize reader<R>::read_until(reader<R>* this, u8 delimiter, bytes* out)
    throws std.error::fault {
    usize scanned = 0usize;
    while (true) {
        o<usize> found = std.bytes::find(this->buffer[this->start + scanned..this->end], delimiter);
        switch (found) {
        case variant o::some(offset):
            usize first = this->start;
            usize stop = first + scanned + *offset + 1usize;
            this->start = stop;
            out->append(this->buffer[first..stop]);
            return stop - first;
        case variant o::none:
            scanned = this->end - this->start;
        }
        throw (this->start == 0usize && this->end == len(this->buffer))
            std.io::io_error {.code = std.io::error_code::resource_exhausted, .native_code = 0i64};
        usize count = 0usize;
        task_scope(1) io { count += await this->fill(); }
        if (count == 0usize) {
            usize first = this->start;
            this->start = this->end;
            out->append(this->buffer[first..this->end]);
            return this->end - first;
        }
    }
}

/* R-SLIB-BUFIO-0002: fills target completely and returns true; false means that the source
   ended before the first byte, and an end after it throws std.bits::read_error with
   unexpected_end and the number of bytes placed. */
@generic<R: std.stream::Reader>
@scoped
async bool reader<R>::read_exact(reader<R>* this, u8[] target) throws std.error::fault {
    usize placed = 0usize;
    usize wanted = len(target);
    while (placed < wanted) {
        usize count = 0usize;
        task_scope(1) io { count += await this->read_into(target[placed..wanted]); }
        if (count == 0usize) {
            if (placed == 0usize) { return false; }
            throw std.bits::read_error {.code = std.bits::read_error_code::unexpected_end,
                                        .byte_index = placed};
        }
        placed += count;
    }
    return true;
}

/* R-SLIB-BUFIO-0002: places at most len(target) bytes: the buffered ones first, otherwise one
   read of the source, directly into target when it is at least the capacity; returns their
   count, zero at the end of the source or for an empty target. */
@generic<R: std.stream::Reader>
@scoped
async usize reader<R>::read_into(reader<R>* this, u8[] target) throws std.error::fault {
    usize wanted = len(target);
    if (wanted == 0usize) { return 0usize; }
    if (this->start == this->end) {
        if (wanted >= len(this->buffer)) {
            task_scope(1) io { return await this->source.read_into(target); }
        }
        usize count = 0usize;
        task_scope(1) io { count += await this->fill(); }
        if (count == 0usize) { return 0usize; }
    }
    usize available = this->end - this->start;
    usize taken = available;
    if (wanted < taken) { taken = wanted; }
    usize first = this->start;
    this->start = first + taken;
    std.bytes::copy(target[0usize..taken], this->buffer[first..first + taken]) as void;
    return taken;
}

/* R-SLIB-BUFIO-0003: a writer that gathers small writes in a buffer of its capacity and hands
   them to its sink when the buffer is full or on flush; bytes still buffered when the writer
   is destroyed are discarded. The buffered bytes are buffer[0..length]. */
@generic<W: std.stream::Writer>
struct writer {
    W sink;
    protected bytes buffer;
    protected usize length;
};

@generic<W: std.stream::Writer>
writer<W> writer<W>::create(W sink, usize capacity) throws std.alloc::alloc_error {
    bytes buffer = std.alloc::bytes(capacity, 0u8);
    return writer<W> {.sink = move sink, .buffer = move buffer, .length = 0usize};
}

/* The number of bytes written to the writer and not yet handed to its sink. */
@generic<W: std.stream::Writer>
usize writer<W>::buffered(const writer<W>* this) {
    return this->length;
}

/* Hands the buffered bytes to the sink. */
@generic<W: std.stream::Writer>
@scoped
protected async void writer<W>::drain(writer<W>* this) throws std.error::fault {
    if (this->length == 0usize) { return; }
    usize length = this->length;
    task_scope(1) io { await this->sink.write_all_from(this->buffer[0usize..length]); }
    this->length = 0usize;
}

/* R-SLIB-BUFIO-0003: buffers data; a full buffer goes to the sink first, and data of at least
   the capacity goes to the sink directly. */
@generic<W: std.stream::Writer>
@scoped
async void writer<W>::write(writer<W>* this, const u8[] data) throws std.error::fault {
    usize capacity = len(this->buffer);
    usize count = len(data);
    if (count > capacity - this->length || count >= capacity) {
        task_scope(1) io { await this->drain(); }
        if (count >= capacity) {
            task_scope(1) io { await this->sink.write_all_from(data); }
            return;
        }
    }
    usize first = this->length;
    std.bytes::copy(this->buffer[first..first + count], data) as void;
    this->length = first + count;
}

/* R-SLIB-BUFIO-0003: write of the UTF-8 bytes of text. */
@generic<W: std.stream::Writer>
@scoped
async void writer<W>::write_str(writer<W>* this, str text) throws std.error::fault {
    task_scope(1) io { await this->write(text); }
}

/* R-SLIB-BUFIO-0003: hands the buffered bytes to the sink and flushes it. */
@generic<W: std.stream::Writer>
@scoped
async void writer<W>::flush(writer<W>* this) throws std.error::fault {
    task_scope(1) io {
        await this->drain();
        await this->sink.flush();
    }
}

module tests.std.bufio;
import std.test;
import std.stream;
import std.bufio;

// The tests of std.bufio (Library R-SLIB-BUFIO-0001..0003): buffered readers that hand out lines,
// delimited sequences, exact counts and plain reads across many refills of a small buffer, their
// limits and errors, and buffered writers that gather small writes, over program types and a
// file. Run in test mode (Core R-FUNC-0025); the file is made in the current directory and
// removed.

/* A reader of a fixed text that hands out at most step bytes a read. */
struct Trickle { bytes text; atomic usize position; usize step; };

impl std.stream::Reader for Trickle {
    @scoped
    async usize read_into(const Trickle* this, u8[] target) throws std.error::fault {
        usize at = core::atomic_load(&this->position, core::memory_order::relaxed);
        usize count = len(this->text) - at;
        if (count > this->step) { count = this->step; }
        if (count > len(target)) { count = len(target); }
        std.bytes::copy(target[0usize..count], this->text[at..at + count]) as void;
        core::atomic_store(&this->position, at + count, core::memory_order::relaxed);
        return count;
    }
};

protected Trickle trickle(const u8[] text, usize step) throws std.alloc::alloc_error {
    bytes owned = {};
    owned.append(text);
    return Trickle {.text = move owned, .position = 0usize, .step = step};
}

protected std.bufio::reader<Trickle> buffered_text(const u8[] text, usize step, usize capacity)
    throws std.alloc::alloc_error {
    return std.bufio::reader<Trickle>::create(trickle(text, step), capacity);
}

/* The sum of the bytes of data, each weighted by its position counted from 1 after the position
   of the first. */
protected u64 weigh_from(u64 position, const u8[] data) {
    u64 weighted = 0u64;
    for (usize index = 0usize; index < len(data); index += 1usize) {
        weighted += (position + (index as u64) + 1u64) * (data[index] as u64);
    }
    return weighted;
}

/* The weighted sum that a Recorder computes for data written from its start. */
protected u64 weigh(const u8[] data) {
    return weigh_from(0u64, data);
}

struct Log { atomic u32 writes; atomic u32 flushes; atomic u64 count; atomic u64 weighted; };

/* A sink that accepts every byte and records its writes, its flushes and a sum of the bytes it
   received weighted by their positions. */
struct Recorder { arc Log log; };

impl std.stream::Writer for Recorder {
    @scoped
    async usize write_from(const Recorder* this, const u8[] source) throws std.error::fault {
        const Log* log = &*this->log;
        u64 weighted = weigh_from(core::atomic_load(&log->count, core::memory_order::relaxed),
                                  source);
        core::atomic_fetch_add(&log->writes, 1u32, core::memory_order::relaxed) as void;
        core::atomic_fetch_add(&log->count, len(source) as u64, core::memory_order::relaxed) as void;
        core::atomic_fetch_add(&log->weighted, weighted, core::memory_order::relaxed) as void;
        return len(source);
    }
    @scoped
    async void flush(const Recorder* this) throws std.error::fault {
        const Log* log = &*this->log;
        core::atomic_fetch_add(&log->flushes, 1u32, core::memory_order::relaxed) as void;
    }
};

/* A sink that takes every byte and keeps none. */
struct Discard { u32 tag; };

impl std.stream::Writer for Discard {
    @scoped
    async usize write_from(const Discard* this, const u8[] source) throws std.error::fault {
        return len(source);
    }
};

protected arc Log new_log() throws std.alloc::alloc_error {
    return new arc Log {.writes = 0u32, .flushes = 0u32, .count = 0u64, .weighted = 0u64};
}

protected u32 writes_of(const (arc Log)* shared) {
    const Log* log = &**shared;
    return core::atomic_load(&log->writes, core::memory_order::relaxed);
}

protected u32 flushes_of(const (arc Log)* shared) {
    const Log* log = &**shared;
    return core::atomic_load(&log->flushes, core::memory_order::relaxed);
}

protected u64 count_of(const (arc Log)* shared) {
    const Log* log = &**shared;
    return core::atomic_load(&log->count, core::memory_order::relaxed);
}

protected u64 weighted_of(const (arc Log)* shared) {
    const Log* log = &**shared;
    return core::atomic_load(&log->weighted, core::memory_order::relaxed);
}

@test(allocations)
void creates_buffers() throws std.alloc::alloc_error, std.test::failure {
    std.bufio::reader<Trickle> input = buffered_text("text", 2usize, 64usize);
    std.test::equal(input.buffered(), 0usize);
    std.test::equal(input.source.step, 2usize);
    std.bufio::writer<Discard> out =
        std.bufio::writer<Discard>::create(Discard {.tag = 7u32}, 64usize);
    std.test::equal(out.buffered(), 0usize);
    std.test::equal(out.sink.tag, 7u32);
}

@test
async void reads_lines_across_refills() throws std.error::fault, std.test::failure {
    std.bufio::reader<Trickle> input = buffered_text("first\r\nsecond\n\nlast", 3usize, 8usize);
    std.string::string line = std.string::create();
    std.string::string joined = std.string::create();
    u32 count = 0u32;
    task_scope(1) io {
        while (await input.read_line(&line) == true) {
            count += 1u32;
            joined.append(line.as_str());
            joined.append("|");
        }
        bool again = await input.read_line(&line);
        std.test::check(again == false, "the source has ended");
    }
    std.test::equal(count, 4u32);
    std.test::equal_text(joined.as_str(), "first|second||last|");
    std.test::equal(std.string::len(&line), 0usize);
    std.test::equal(input.buffered(), 0usize);
}

@test
async void refuses_overlong_and_invalid_lines() throws std.error::fault, std.test::failure {
    std.bufio::reader<Trickle> input = buffered_text("0123456789\nok\n", 3usize, 8usize);
    std.string::string line = std.string::create();
    try {
        task_scope(1) io {
            bool read = await input.read_line(&line);
            read as void;
        }
        std.test::fail("a line of eleven bytes does not fit in eight");
    } catch (std.io::io_error failure) {
        std.test::check(failure.code == std.io::error_code::resource_exhausted,
                        "resource_exhausted");
        std.test::equal(failure.native_code, 0i64);
    }
    std.test::equal(input.buffered(), 8usize);
    bytes head = std.alloc::bytes(8usize, 0u8);
    std.string::string next = std.string::create();
    task_scope(1) io {
        bool whole = await input.read_exact(head.as_slice_mut());
        std.test::check(whole == true, "the kept bytes are still there");
        bool first = await input.read_line(&line);
        std.test::check(first == true, "the rest of the long line");
        bool second = await input.read_line(&next);
        std.test::check(second == true, "the line after it");
    }
    std.test::check(std.bytes::equal(head.as_slice(), "01234567"), "the kept bytes");
    std.test::equal_text(line.as_str(), "89");
    std.test::equal_text(next.as_str(), "ok");
    u8[9] encoded = {97u8, 255u8, 98u8, 10u8, 111u8, 107u8, 10u8, 122u8, 10u8};
    std.bufio::reader<Trickle> mixed = buffered_text(encoded, 2usize, 8usize);
    try {
        task_scope(1) io {
            bool read = await mixed.read_line(&line);
            read as void;
        }
        std.test::fail("the first line is not valid UTF-8");
    } catch (std.string::string_error failure) {
        failure as void;
    }
    std.test::equal(std.string::len(&line), 0usize);
    task_scope(1) io {
        bool first = await mixed.read_line(&line);
        std.test::check(first == true, "the line after the invalid one");
        bool second = await mixed.read_line(&next);
        std.test::check(second == true, "the last line");
        bool third = await mixed.read_line(&next);
        std.test::check(third == false, "the end");
    }
    std.test::equal_text(line.as_str(), "ok");
    std.test::equal(std.string::len(&next), 0usize);
}

@test
async void reads_delimited_sequences() throws std.error::fault, std.test::failure {
    own dyn(std.stream::Reader)* source = new Trickle(trickle("key;value;;rest", 3usize));
    std.bufio::reader<own dyn(std.stream::Reader)*> input =
        std.bufio::reader<own dyn(std.stream::Reader)*>::create(move source, 8usize);
    bytes out = {};
    task_scope(1) io {
        usize first = await input.read_until(59u8, &out);
        usize second = await input.read_until(59u8, &out);
        usize third = await input.read_until(59u8, &out);
        usize fourth = await input.read_until(59u8, &out);
        usize fifth = await input.read_until(59u8, &out);
        std.test::equal(first, 4usize);
        std.test::equal(second, 6usize);
        std.test::equal(third, 1usize);
        std.test::equal(fourth, 4usize);
        std.test::equal(fifth, 0usize);
    }
    std.test::check(std.bytes::equal(out.as_slice(), "key;value;;rest"), "every byte appended");
    std.bufio::reader<Trickle> long_field = buffered_text("abcdefghij;", 4usize, 8usize);
    bytes field = {};
    try {
        task_scope(1) io {
            usize count = await long_field.read_until(59u8, &field);
            count as void;
        }
        std.test::fail("a field of eleven bytes does not fit in eight");
    } catch (std.io::io_error failure) {
        std.test::check(failure.code == std.io::error_code::resource_exhausted,
                        "resource_exhausted");
    }
    std.test::equal(len(field), 0usize);
    std.test::equal(long_field.buffered(), 8usize);
}

@test
async void reads_exact_counts() throws std.error::fault, std.test::failure {
    std.bufio::reader<Trickle> input = buffered_text("HDR1payload", 3usize, 8usize);
    bytes header = std.alloc::bytes(4usize, 0u8);
    bytes body = std.alloc::bytes(7usize, 0u8);
    bytes more = std.alloc::bytes(4usize, 0u8);
    task_scope(1) io {
        bool first = await input.read_exact(header.as_slice_mut());
        std.test::check(first == true, "the whole header");
        bool second = await input.read_exact(body.as_slice_mut());
        std.test::check(second == true, "the whole body");
        bool third = await input.read_exact(more.as_slice_mut());
        std.test::check(third == false, "the source ended before the first byte");
    }
    std.test::check(std.bytes::equal(header.as_slice(), "HDR1"), "the header");
    std.test::check(std.bytes::equal(body.as_slice(), "payload"), "the body");
    std.bufio::reader<Trickle> short_input = buffered_text("abcdef", 4usize, 8usize);
    bytes piece = std.alloc::bytes(4usize, 0u8);
    bytes rest = std.alloc::bytes(4usize, 0u8);
    try {
        task_scope(1) io {
            bool whole = await short_input.read_exact(piece.as_slice_mut());
            std.test::check(whole == true, "the first four bytes");
            bool partial = await short_input.read_exact(rest.as_slice_mut());
            partial as void;
        }
        std.test::fail("the source ends inside the second read");
    } catch (std.bits::read_error failure) {
        std.test::check(failure.code == std.bits::read_error_code::unexpected_end,
                        "unexpected_end");
        std.test::equal(failure.byte_index, 2usize);
    }
    std.test::check(std.bytes::equal(piece.as_slice(), "abcd"), "the first piece");
    std.test::check(std.bytes::equal(rest[0usize..2usize], "ef"), "the bytes placed before the end");
}

@test
async void read_into_takes_buffered_bytes_first() throws std.error::fault, std.test::failure {
    std.bufio::reader<Trickle> input = buffered_text("0123456789abcdefghij", 5usize, 8usize);
    bytes small = std.alloc::bytes(3usize, 0u8);
    bytes large = std.alloc::bytes(10usize, 0u8);
    task_scope(1) io {
        usize first = await input.read_into(small.as_slice_mut());
        std.test::equal(first, 3usize);
        std.test::equal(input.buffered(), 2usize);
    }
    std.test::check(std.bytes::equal(small.as_slice(), "012"), "one read, three handed out");
    task_scope(1) io {
        usize second = await input.read_into(large.as_slice_mut());
        std.test::equal(second, 2usize);
    }
    std.test::check(std.bytes::equal(large[0usize..2usize], "34"), "the buffered bytes alone");
    task_scope(1) io {
        usize third = await input.read_into(large.as_slice_mut());
        std.test::equal(third, 5usize);
        std.test::equal(input.buffered(), 0usize);
    }
    std.test::check(std.bytes::equal(large[0usize..5usize], "56789"), "a direct read");
    task_scope(1) io {
        usize none = await input.read_into(large[0usize..0usize]);
        std.test::equal(none, 0usize);
    }
    usize total = 10usize;
    task_scope(1) io {
        while (true) {
            usize count = await input.read_into(small.as_slice_mut());
            if (count == 0usize) { break; }
            total += count;
        }
    }
    std.test::equal(total, 20usize);
}

@test
async void writer_gathers_small_writes() throws std.error::fault, std.test::failure {
    arc Log shared = new_log();
    std.bufio::writer<Recorder> out =
        std.bufio::writer<Recorder>::create(Recorder {.log = std.arc::clone(&shared)}, 8usize);
    std.string::string large = std.string::from_str("0123456789");
    task_scope(1) io {
        await out.write_str("abc");
        await out.write_str("defg");
        std.test::equal(out.buffered(), 7usize);
        std.test::equal(writes_of(&shared), 0u32);
        await out.write_str("hi");
        std.test::equal(out.buffered(), 2usize);
        std.test::equal(writes_of(&shared), 1u32);
        await out.write(large.as_bytes());
        std.test::equal(out.buffered(), 0usize);
        std.test::equal(writes_of(&shared), 3u32);
        await out.write_str("xy");
        std.test::equal(flushes_of(&shared), 0u32);
        await out.flush();
        std.test::equal(out.buffered(), 0usize);
        std.test::equal(writes_of(&shared), 4u32);
        std.test::equal(flushes_of(&shared), 1u32);
        await out.write_str("lost");
        std.test::equal(out.buffered(), 4usize);
    }
    drop out;
    std.test::equal(writes_of(&shared), 4u32);
    std.test::equal(count_of(&shared), 21u64);
    std.test::equal(weighted_of(&shared), weigh("abcdefghi0123456789xy"));
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
async void round_trips_lines_through_a_file() throws std.error::fault, std.test::failure {
    std.fs::path path = std.fs::path_from_utf8("tests_std_bufio.tmp");
    std.fs::file file = await path.open_file(writing());
    std.bufio::writer<std.fs::file> out =
        std.bufio::writer<std.fs::file>::create(move file, 16usize);
    for (u32 number = 1u32; number <= 20u32; number += 1u32) {
        std.string::string text = f"line {number}\r\n";
        task_scope(1) write { await out.write(text.as_bytes()); }
    }
    task_scope(1) io {
        await out.flush();
        await out.write_str("discarded\n");
    }
    drop out;
    std.fs::file source = await path.open_file(reading());
    std.bufio::reader<std.fs::file> input =
        std.bufio::reader<std.fs::file>::create(move source, 16usize);
    std.string::string line = std.string::create();
    u32 count = 0u32;
    task_scope(1) io {
        while (await input.read_line(&line) == true) {
            count += 1u32;
            std.string::string expected = f"line {count}";
            std.test::equal_text(line.as_str(), expected.as_str());
        }
    }
    drop input;
    std.test::equal(count, 20u32);
    task_scope(1) cleanup { await remove_here("tests_std_bufio.tmp"); }
}

/* M24-8: data of exactly the capacity goes to the sink at once, like longer data. */
@test
async void exact_capacity_goes_to_the_sink() throws std.error::fault, std.test::failure {
    arc Log shared = new_log();
    std.bufio::writer<Recorder> out =
        std.bufio::writer<Recorder>::create(Recorder {.log = std.arc::clone(&shared)}, 8usize);
    std.string::string eight = std.string::from_str("01234567");
    task_scope(1) io { await out.write(eight.as_bytes()); }
    std.test::equal(writes_of(&shared), 1u32);
    std.test::equal(out.buffered(), 0usize);
    std.string::string nine = std.string::from_str("012345678");
    task_scope(1) io { await out.write(nine.as_bytes()); }
    std.test::equal(writes_of(&shared), 2u32);
    std.test::equal(out.buffered(), 0usize);
}

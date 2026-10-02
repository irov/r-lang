module test.regression.stream;
import std.test;
import std.stream;

// M24-6 (R-NAME-0009): a parameter named like a component of its module path is rejected once.
// The call below borrows for that generic scoped function before its body is checked; the
// borrow has no value then, which implies no type argument and adds no diagnostic of its own.

struct Tally { atomic u32 calls; };

struct Chunked { arc Tally tally; };

impl std.stream::Writer for Chunked {
    @scoped
    async usize write_from(const Chunked* this, const u8[] source) throws std.error::fault {
        const Tally* tally = &*this->tally;
        core::atomic_fetch_add(&tally->calls, 1u32, core::memory_order::relaxed) as void;
        return len(source);
    }
};

struct Fixed { bytes text; atomic usize position; };

impl std.stream::Reader for Fixed {
    @scoped
    async usize read_into(const Fixed* this, u8[] target) throws std.error::fault {
        usize at = core::atomic_load(&this->position, core::memory_order::relaxed);
        usize count = len(this->text) - at;
        if (count > len(target)) { count = len(target); }
        std.bytes::copy(target[0usize..count], this->text[at..at + count]) as void;
        core::atomic_store(&this->position, at + count, core::memory_order::relaxed);
        return count;
    }
};

@generic<S: std.stream::Stream>
@scoped
protected async usize echo(const S* stream) throws std.error::fault {
    u8[4] chunk = {};
    usize total = 0usize;
    while (true) {
        usize count = 0usize;
        task_scope(1) input { count += await stream->read_into(&chunk); }
        if (count == 0usize) { break; }
        task_scope(1) output { await stream->write_all_from(chunk[0usize..count]); }
        total += count;
    }
    task_scope(1) done {
        await stream->flush();
        await stream->shutdown();
    }
    return total;
}

@test
async void echoes() throws std.error::fault, std.test::failure {
    arc Tally shared = new arc Tally {.calls = 0u32};
    std.string::string text = std.string::from_str("abc");
    std.stream::duplex<Fixed, Chunked> pair = {
        .reader = Fixed {.text = (move text).into_bytes(), .position = 0usize},
        .writer = Chunked {.tally = std.arc::clone(&shared)}};
    task_scope(1) io {
        usize copied = await echo(&pair);
        std.test::equal(copied, 3usize);
    }
}

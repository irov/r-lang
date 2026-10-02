module tests.std.arena;
import std.test;
import std.arena;

// The tests of std.arena (Library R-SLIB-ARENA-0001..0005): pieces stored in blocks of a fixed size
// and read back as views and text, a block for data larger than the block size, the limit of the
// arena, invalid pieces and a reset. Run in test mode (Core R-FUNC-0025).

@test
void stores_and_reads_pieces() throws std.test::failure, std.arena::arena_error, std.alloc::alloc_error {
    std.arena::arena place = std.arena::arena::create(16usize, 1024usize);
    std.arena::piece first = place.store_text("key");
    std.arena::piece second = place.store_text("value");
    std.arena::piece third = place.store("\x00\x01");
    std.test::equal(first.block, 0usize);
    std.test::equal(second.block, 0usize);
    std.test::equal(second.start, 3usize);
    std.test::equal_text(place.text(first), "key");
    std.test::equal_text(place.text(second), "value");
    const u8[] raw_bytes = place.bytes(third);
    std.test::equal(len(raw_bytes), 2usize);
    std.test::equal(raw_bytes[1usize], 1u8);
    std.test::equal(place.used(), 10usize);
    std.test::equal(place.reserved(), 16usize);
    // A piece that does not fit the current block starts a new one; a large piece gets its own.
    std.arena::piece fourth = place.store_text("0123456789");
    std.test::equal(fourth.block, 1usize);
    std.arena::piece large = place.store_text("abcdefghijklmnopqrstuvwxyz");
    std.test::equal(large.block, 2usize);
    std.test::equal(place.reserved(), 58usize);
    std.test::equal_text(place.text(large), "abcdefghijklmnopqrstuvwxyz");
    std.test::equal_text(place.text(first), "key");
}

@test
void refuses_beyond_its_limit() throws std.test::failure, std.alloc::alloc_error {
    std.arena::arena place = std.arena::arena::create(8usize, 16usize);
    try {
        std.arena::piece a = place.store_text("12345678");
        std.arena::piece b = place.store_text("abcdefgh");
        std.test::equal(b.block, 1usize);
        a as void;
        std.arena::piece c = place.store_text("x");
        c as void;
        std.test::check(false, "a third block passes the limit");
    } catch (std.arena::arena_error failure) {
        std.test::check(failure.code == std.arena::error_code::limit_reached, "limit_reached");
    }
    std.test::equal(place.reserved(), 16usize);
    place.reset();
    std.test::equal(place.reserved(), 0usize);
    std.test::equal(place.used(), 0usize);
    try {
        const u8[] gone = place.bytes(std.arena::piece {.block = 0usize, .start = 0usize, .length = 1usize});
        gone as void;
        std.test::check(false, "a piece of a reset arena");
    } catch (std.arena::arena_error failure) {
        std.test::check(failure.code == std.arena::error_code::invalid_piece, "invalid_piece");
    }
}

module tests.std.deflate;
import std.test;
import std.deflate;

// The tests of std.deflate (Library R-SLIB-DEFLATE-0001..0005), run in test mode
// (Core R-FUNC-0025). The reference streams were produced by zlib through Python.

/* Fails unless the bytes are the UTF-8 text. */
protected void expect_text(const array<u8>* actual, str expected)
    throws std.test::failure, std.alloc::alloc_error {
    try {
        str text = core::validate_utf8(std.array::as_slice(actual));
        std.test::equal_text(text, expected);
    } catch (core::utf8_error failure) {
        failure as void;
        std.test::fail("the bytes are not UTF-8");
    }
}

/* Text with repetitions that LZ77 finds, slightly varied every 450 bytes. */
protected array<u8> sample(usize count) throws std.alloc::alloc_error {
    array<u8> result = std.alloc::bytes(count, 0u8);
    u8[] view = std.array::as_slice_mut(&result);
    str phrase = "the quick brown fox jumps over the lazy dog; ";
    const u8[] source = phrase;
    for (usize index = 0usize; index < count; index += 1usize) {
        usize shift = (index / 450usize) % 3usize;
        view[index] = ((source[index % len(source)] as usize) + shift) as u8;
    }
    return move result;
}

/* Fails unless one-shot decoding of the stream reports the code. */
protected void expect_inflate_error(str label, const u8[] input, std.deflate::format form,
                                    usize limit, std.deflate::error_code code)
    throws std.test::failure, std.alloc::alloc_error {
    try {
        array<u8> result = std.deflate::inflate(input, form, limit);
        drop result;
        std.string::string message = f"{label}: the stream decoded";
        std.test::fail(message.as_str());
    } catch (std.deflate::error failure) {
        std.string::string message = f"{label}: the error code";
        std.test::check(failure.code == code, message.as_str());
        std.string::string offset = f"{label}: the offset";
        std.test::check(failure.offset <= len(input), offset.as_str());
    }
}

@test
void inflates_reference_streams()
    throws std.test::failure, std.alloc::alloc_error, std.deflate::error {
    u8[11] zlib_abc = {0x78, 0x9c, 0x4b, 0x4c, 0x4a, 0x06, 0x00, 0x02, 0x4d, 0x01, 0x27};
    array<u8> abc = std.deflate::inflate(zlib_abc, std.deflate::format::rfc1950, 1024usize);
    expect_text(&abc, "abc");
    u8[39] gzip_member = {0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x4b, 0xaf,
                          0xca, 0x2c, 0x50, 0xc8, 0x4d, 0xcd, 0x4d, 0x4a, 0x2d, 0x52, 0x28, 0x48,
                          0xac, 0xcc, 0xc9, 0x4f, 0x4c, 0x01, 0x00, 0xf1, 0xf0, 0xb4, 0x3c, 0x13,
                          0x00, 0x00, 0x00};
    array<u8> member = std.deflate::inflate(gzip_member, std.deflate::format::rfc1952, 1024usize);
    expect_text(&member, "gzip member payload");
    // A gzip header with the file name "note.txt".
    u8[48] named = {0x1f, 0x8b, 0x08, 0x08, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x6e, 0x6f, 0x74,
                    0x65, 0x2e, 0x74, 0x78, 0x74, 0x00, 0xcb, 0x48, 0xcd, 0xc9, 0xc9, 0xd7, 0x51,
                    0xc8, 0x4b, 0xcc, 0x4d, 0x4d, 0x51, 0xc8, 0x4d, 0xcd, 0x4d, 0x4a, 0x2d, 0x02,
                    0x00, 0xcc, 0xd3, 0x3d, 0xb9, 0x13, 0x00, 0x00, 0x00};
    array<u8> named_text = std.deflate::inflate(named, std.deflate::format::rfc1952, 1024usize);
    expect_text(&named_text, "hello, named member");
    // A raw stored block and a raw fixed-Huffman block with back-references.
    u8[25] stored = {0x01, 0x14, 0x00, 0xeb, 0xff, 0x73, 0x74, 0x6f, 0x72, 0x65, 0x64, 0x20,
                     0x62, 0x6c, 0x6f, 0x63, 0x6b, 0x20, 0x70, 0x61, 0x79, 0x6c, 0x6f, 0x61,
                     0x64};
    array<u8> plain = std.deflate::inflate(stored, std.deflate::format::rfc1951, 1024usize);
    expect_text(&plain, "stored block payload");
    u8[10] fixed = {0xcb, 0x48, 0xcd, 0xc9, 0xc9, 0x57, 0xc8, 0x40, 0x27, 0x01};
    array<u8> repeated = std.deflate::inflate(fixed, std.deflate::format::rfc1951, 1024usize);
    expect_text(&repeated, "hello hello hello hello");
}

@test
void round_trips_every_format_and_level()
    throws std.test::failure, std.alloc::alloc_error, std.deflate::error {
    array<u8> text = sample(5000usize);
    std.deflate::format[3] forms = {std.deflate::format::rfc1951, std.deflate::format::rfc1950,
                                    std.deflate::format::rfc1952};
    u8[4] levels = {0, 1, 5, 9};
    for (usize form_index = 0usize; form_index < 3usize; form_index += 1usize) {
        for (usize level_index = 0usize; level_index < 4usize; level_index += 1usize) {
            u8 level = levels[level_index];
            array<u8> packed = std.deflate::deflate(text.as_slice(), forms[form_index], level);
            array<u8> restored =
                std.deflate::inflate(packed.as_slice(), forms[form_index], len(text));
            std.string::string context = f"format {form_index} level {level}";
            std.test::check(std.bytes::equal(restored.as_slice(), text.as_slice()) == true,
                            context.as_str());
            // Level 0 stores; the other levels find the repetitions.
            bool smaller = len(packed) * 4usize < len(text);
            std.test::check(smaller == (level != 0), context.as_str());
        }
    }
    // The wrappers begin with their headers.
    array<u8> zlib = std.deflate::deflate(text.as_slice(), std.deflate::format::rfc1950, 6);
    std.test::equal(zlib[0usize], 0x78u8);
    array<u8> gzip = std.deflate::deflate(text.as_slice(), std.deflate::format::rfc1952, 6);
    std.test::equal(gzip[0usize], 0x1fu8);
    std.test::equal(gzip[1usize], 0x8bu8);
    const u8[] all = text.as_slice();
    array<u8> empty = std.deflate::deflate(all[0usize..0usize], std.deflate::format::rfc1952, 6);
    array<u8> nothing = std.deflate::inflate(empty.as_slice(), std.deflate::format::rfc1952, 0usize);
    std.test::equal(len(nothing), 0usize);
}

@test
void streams_through_small_buffers()
    throws std.test::failure, std.alloc::alloc_error, std.deflate::error {
    array<u8> text = sample(3000usize);
    const u8[] input = text.as_slice();
    // Compression in 100-byte pieces of input into a 7-byte output buffer.
    std.deflate::deflater packer =
        std.deflate::deflater::create(std.deflate::format::rfc1950, 6, 1024usize);
    array<u8> packed = std.array::create::<u8>();
    u8[7] cell = {};
    usize fed = 0usize;
    usize steps = 0usize;
    bool done = false;
    while (done == false && steps < 100000usize) {
        steps += 1usize;
        u8[] out_view = &cell;
        usize stop = fed + 100usize;
        if (stop > len(input)) { stop = len(input); }
        std.deflate::flush mode = std.deflate::flush::none;
        if (stop == len(input)) { mode = std.deflate::flush::finish; }
        std.deflate::progress step = packer.deflate(input[fed..stop], out_view, mode);
        fed += step.consumed;
        std.bytes::append(&packed, out_view[0usize..step.produced]);
        if (step.state == std.deflate::state::end) { done = true; }
    }
    std.test::check(done == true, "the compression ends");
    std.test::equal(packer.consumed_bytes(), len(input));
    std.test::equal(packer.produced_bytes(), len(packed));
    // Decompression in 5-byte pieces of input into a 3-byte output buffer.
    const u8[] stream = packed.as_slice();
    std.deflate::inflater unpacker =
        std.deflate::inflater::create(std.deflate::format::rfc1950, 1024usize, len(input));
    array<u8> restored = std.array::create::<u8>();
    u8[3] small = {};
    usize taken = 0usize;
    bool ended = false;
    while (ended == false && steps < 200000usize) {
        steps += 1usize;
        u8[] out_view = &small;
        usize stop = taken + 5usize;
        if (stop > len(stream)) { stop = len(stream); }
        std.deflate::progress step = unpacker.inflate(stream[taken..stop], out_view);
        taken += step.consumed;
        std.bytes::append(&restored, out_view[0usize..step.produced]);
        if (step.state == std.deflate::state::end) { ended = true; }
    }
    std.test::check(ended == true, "the decompression ends");
    std.test::check(std.bytes::equal(restored.as_slice(), input) == true, "the restored bytes");
    std.test::equal(unpacker.consumed_bytes(), len(stream));
    std.test::equal(unpacker.produced_bytes(), len(input));
    // After a reset the inflater decodes the stream again; bytes after its end stay unconsumed.
    unpacker.reset();
    array<u8> tailed = std.array::create::<u8>();
    std.bytes::append(&tailed, stream);
    std.bytes::append_u8(&tailed, 0xaau8);
    array<u8> big = std.alloc::bytes(4096usize, 0u8);
    std.deflate::progress whole = unpacker.inflate(tailed.as_slice(), std.array::as_slice_mut(&big));
    std.test::check(whole.state == std.deflate::state::end, "one step decodes the stream");
    std.test::equal(whole.consumed, len(stream));
    std.test::equal(whole.produced, len(input));
}

@test
void flushes_to_a_byte_boundary()
    throws std.test::failure, std.alloc::alloc_error, std.deflate::error {
    array<u8> text = sample(2000usize);
    const u8[] input = text.as_slice();
    std.deflate::deflater packer =
        std.deflate::deflater::create(std.deflate::format::rfc1951, 6, 32768usize);
    array<u8> sink = std.alloc::bytes(8192usize, 0u8);
    u8[] sink_view = std.array::as_slice_mut(&sink);
    std.deflate::progress first =
        packer.deflate(input[0usize..1000usize], sink_view, std.deflate::flush::sync);
    std.test::check(first.state == std.deflate::state::need_input, "the stream continues");
    std.test::equal(first.consumed, 1000usize);
    // A sync flush ends with an empty stored block: 00 00 ff ff.
    std.test::check(first.produced >= 4usize, "the flush writes bytes");
    std.test::equal(sink_view[first.produced - 4usize], 0x00u8);
    std.test::equal(sink_view[first.produced - 3usize], 0x00u8);
    std.test::equal(sink_view[first.produced - 2usize], 0xffu8);
    std.test::equal(sink_view[first.produced - 1usize], 0xffu8);
    array<u8> synced = std.array::create::<u8>();
    std.bytes::append(&synced, sink_view[0usize..first.produced]);
    // The flushed prefix alone decodes to the first 1000 bytes.
    std.deflate::inflater unpacker =
        std.deflate::inflater::create(std.deflate::format::rfc1951, 32768usize, 4096usize);
    array<u8> out = std.alloc::bytes(4096usize, 0u8);
    u8[] out_view = std.array::as_slice_mut(&out);
    std.deflate::progress partial = unpacker.inflate(synced.as_slice(), out_view);
    std.test::check(partial.state == std.deflate::state::need_input, "more input is needed");
    std.test::equal(partial.produced, 1000usize);
    std.test::check(std.bytes::equal(out_view[0usize..1000usize], input[0usize..1000usize]) == true,
                    "the first half");
    std.deflate::progress second =
        packer.deflate(input[1000usize..2000usize], sink_view, std.deflate::flush::finish);
    std.test::check(second.state == std.deflate::state::end, "the stream ends");
    std.bytes::append(&synced, sink_view[0usize..second.produced]);
    array<u8> restored = std.deflate::inflate(synced.as_slice(), std.deflate::format::rfc1951,
                                              4096usize);
    std.test::check(std.bytes::equal(restored.as_slice(), input) == true, "the whole text");
}

@test(expect = std.deflate::error)
void rejects_a_level_above_nine() throws std.deflate::error, std.alloc::alloc_error {
    std.deflate::deflater packer =
        std.deflate::deflater::create(std.deflate::format::rfc1951, 10, 32768usize);
    drop packer;
}

@test
void reports_stream_errors() throws std.test::failure, std.alloc::alloc_error, std.deflate::error {
    std.deflate::format bare = std.deflate::format::rfc1951;
    std.deflate::format zlib = std.deflate::format::rfc1950;
    std.deflate::format gzip = std.deflate::format::rfc1952;
    u8[12] zlib_abc = {0x78, 0x9c, 0x4b, 0x4c, 0x4a, 0x06, 0x00, 0x02, 0x4d, 0x01, 0x27, 0x00};
    expect_inflate_error("truncated", zlib_abc[0usize..8usize], zlib, 1024usize,
                         std.deflate::error_code::corrupt_stream);
    expect_inflate_error("trailing byte", zlib_abc, zlib, 1024usize,
                         std.deflate::error_code::corrupt_stream);
    u8[11] damaged = {0x78, 0x9c, 0x4b, 0x4c, 0x4a, 0x06, 0x00, 0x02, 0x4d, 0x01, 0x28};
    expect_inflate_error("adler-32", damaged, zlib, 1024usize,
                         std.deflate::error_code::checksum_mismatch);
    u8[11] header_check = {0x78, 0x9d, 0x4b, 0x4c, 0x4a, 0x06, 0x00, 0x02, 0x4d, 0x01, 0x27};
    expect_inflate_error("header check", header_check, zlib, 1024usize,
                         std.deflate::error_code::corrupt_stream);
    u8[8] dictionary = {0x78, 0xbb, 0x00, 0x00, 0x00, 0x01, 0x03, 0x00};
    expect_inflate_error("preset dictionary", dictionary, zlib, 1024usize,
                         std.deflate::error_code::unsupported);
    u8[20] gzip_empty = {0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x03, 0x00,
                         0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00};
    expect_inflate_error("crc-32", gzip_empty, gzip, 1024usize,
                         std.deflate::error_code::checksum_mismatch);
    u8[20] reserved_flag = {0x1f, 0x8b, 0x08, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x03,
                            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    expect_inflate_error("reserved flag", reserved_flag, gzip, 1024usize,
                         std.deflate::error_code::unsupported);
    u8[2] reserved_block = {0x07, 0x00};
    expect_inflate_error("reserved block type", reserved_block, bare, 1024usize,
                         std.deflate::error_code::corrupt_stream);
    u8[6] stored_length = {0x01, 0x01, 0x00, 0xff, 0xff, 0x61};
    expect_inflate_error("stored length", stored_length, bare, 1024usize,
                         std.deflate::error_code::corrupt_stream);
    u8[10] fixed = {0xcb, 0x48, 0xcd, 0xc9, 0xc9, 0x57, 0xc8, 0x40, 0x27, 0x01};
    expect_inflate_error("output limit", fixed, bare, 10usize,
                         std.deflate::error_code::output_limit);
    // A failing step poisons the inflater; a step after the end is finished.
    std.deflate::inflater engine = std.deflate::inflater::create(bare, 1024usize, 1024usize);
    array<u8> space = std.alloc::bytes(64usize, 0u8);
    u8[] space_view = std.array::as_slice_mut(&space);
    try {
        engine.inflate(reserved_block, space_view) as void;
        std.test::fail("a reserved block type decoded");
    } catch (std.deflate::error failure) {
        std.test::check(failure.code == std.deflate::error_code::corrupt_stream, "corrupt");
    }
    try {
        engine.inflate(fixed, space_view) as void;
        std.test::fail("a poisoned inflater decoded");
    } catch (std.deflate::error failure) {
        std.test::check(failure.code == std.deflate::error_code::poisoned, "poisoned");
    }
    engine.reset();
    std.deflate::progress step = engine.inflate(fixed, space_view);
    std.test::check(step.state == std.deflate::state::end, "a reset clears the poison");
    try {
        engine.inflate(fixed, space_view) as void;
        std.test::fail("a finished inflater decoded");
    } catch (std.deflate::error failure) {
        std.test::check(failure.code == std.deflate::error_code::finished, "finished");
    }
    // A window is a power of two from 256 through 32768.
    try {
        std.deflate::inflater odd = std.deflate::inflater::create(bare, 1000usize, 1usize);
        drop odd;
        std.test::fail("a window of 1000 bytes");
    } catch (std.deflate::error failure) {
        std.test::check(failure.code == std.deflate::error_code::invalid_window, "window 1000");
    }
    try {
        std.deflate::deflater wide = std.deflate::deflater::create(bare, 6, 65536usize);
        drop wide;
        std.test::fail("a window of 65536 bytes");
    } catch (std.deflate::error failure) {
        std.test::check(failure.code == std.deflate::error_code::invalid_window, "window 65536");
    }
}

@test(allocations)
void compresses_under_allocation_failures()
    throws std.test::failure, std.alloc::alloc_error, std.deflate::error {
    str message = "allocation failures allocation failures allocation failures";
    array<u8> packed = std.deflate::deflate(message, std.deflate::format::rfc1952, 6);
    array<u8> restored = std.deflate::inflate(packed.as_slice(), std.deflate::format::rfc1952,
                                              1024usize);
    expect_text(&restored, message);
}

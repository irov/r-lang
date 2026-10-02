module test.codegen.library_deflate;

import std.deflate;

// Retain values whose purpose here is type or lifetime coverage.
@generic<T> @noalloc @nonblocking
protected void test_observe(const T* value) { value as void; }


/* Vectors produced by an independent DEFLATE implementation (zlib via Python). */
/* Compresses and decompresses one input; zero when the bytes survive, otherwise base + step. */
i32 roundtrip(const u8[] data, std.deflate::format form, u8 level, usize window, i32 base)
    throws std.deflate::error, std.alloc::alloc_error, std.array::push_error<u8> {
    std.deflate::deflater packer = std.deflate::deflater::create(form, level, window);
    test_observe(&packer);
    array<u8> packed = std.array::create::<u8>();
    test_observe(&packed);
    array<u8> out_cell = std.alloc::bytes(7usize, 0u8);
    test_observe(&out_cell);
    usize fed = 0usize;
    bool done = false;
    usize guard = 0usize;
    while (done == false) {
        guard += 1usize;
        if (guard > 4000000usize) { return base + 13; }
        u8[] out_view = std.array::as_slice_mut(&out_cell);
        const u8[] rest = data[fed..len(data)];
        std.deflate::progress step = packer.deflate(rest, out_view, std.deflate::flush::finish);
        fed += step.consumed;
        for (usize index = 0usize; index < step.produced; index += 1usize) { std.array::push(&packed, out_view[index]); }
        if (step.state == std.deflate::state::end) { done = true; }
        if (step.state == std.deflate::state::need_input) { return base + 1; }
    }
    if (fed != len(data)) { return base + 2; }
    if (packer.consumed_bytes() != len(data)) { return base + 3; }
    if (packer.produced_bytes() != len(packed)) { return base + 4; }
    const u8[] packed_view = std.array::as_slice(&packed);
    array<u8> restored = std.deflate::inflate(packed_view, form, len(data) + 1usize);
    if (len(restored) != len(data)) { return base + 5; }
    const u8[] restored_view = std.array::as_slice(&restored);
    for (usize index = 0usize; index < len(data); index += 1usize) {
        if (restored_view[index] != data[index]) { return base + 6; }
    }
    /* The same stream decoded through a window-bounded inflater, one output byte at a time. */
    std.deflate::inflater unpacker = std.deflate::inflater::create(form, window, len(data) + 1usize);
    test_observe(&unpacker);
    u8[1] cell = { 0 };
    usize taken = 0usize;
    usize produced = 0usize;
    bool ended = false;
    while (ended == false) {
        guard += 1usize;
        if (guard > 8000000usize) { return base + 14; }
        u8[] cell_view = &cell;
        const u8[] chunk = packed_view[taken..len(packed_view)];
        std.deflate::progress step = unpacker.inflate(chunk, cell_view);
        taken += step.consumed;
        if (step.produced == 1usize) {
            if (produced >= len(data)) { return base + 7; }
            if (cell[0] != data[produced]) { return base + 8; }
            produced += 1usize;
        }
        if (step.state == std.deflate::state::need_input) { return base + 9; }
        if (step.state == std.deflate::state::end) { ended = true; }
    }
    if ((produced != len(data)) || (taken != len(packed_view))) { return base + 10; }
    /* A reset deflater compresses the same input to the same bytes. */
    packer.reset();
    array<u8> again = std.deflate::deflate(data, form, level);
    test_observe(&again);
    if (window == 32768usize) {
        if (len(again) != len(packed)) { return base + 11; }
        const u8[] again_view = std.array::as_slice(&again);
        for (usize index = 0usize; index < len(again); index += 1usize) {
            if (again_view[index] != packed_view[index]) { return base + 12; }
        }
    }
    return 0;
}

i32 checks() throws std.deflate::error, std.alloc::alloc_error, std.array::push_error<u8> {
    u8[526] raw_dynamic = { 0x85, 0x96, 0x51, 0x52, 0xc3, 0x30, 0x0c, 0x44, 0xaf, 0x92, 0x5b, 0xf4, 0x3c, 0x81, 0x26, 0xb4, 0x43, 0xd2, 0x32, 0x50, 0x26, 0x33, 0x9c, 0x9e, 0x8e, 0xd7, 0xc1, 0x6f, 0x2d, 0xb5, 0xfc, 0x38, 0x89, 0x25, 0xcb, 0xd2, 0x6a, 0x25, 0x65, 0x3b, 0x5f, 0x8e, 0xd7, 0x6d, 0x78, 0x1b, 0xd7, 0x75, 0x1c, 0x4e, 0xdf, 0xf3, 0xbc, 0x8e, 0x97, 0x61, 0x5c, 0x3e, 0x4e, 0xe3, 0xf0, 0x32, 0xdd, 0xee, 0xcb, 0x72, 0x7d, 0x7d, 0xd7, 0xeb, 0x26, 0xd5, 0xf9, 0x7c, 0x19, 0x97, 0x5d, 0xa5, 0x48, 0x8f, 0xd3, 0x72, 0x17, 0xe3, 0xd0, 0x6e, 0x67, 0x7f, 0x96, 0x4d, 0x69, 0xc1, 0xa8, 0xdf, 0x26, 0xb3, 0xd0, 0xe4, 0x3d, 0x7a, 0xd7, 0xea, 0xc7, 0xec, 0xee, 0x62, 0x56, 0xa1, 0x7c, 0xdd, 0x3e, 0xa7, 0x71, 0xfd, 0x53, 0xd6, 0x26, 0x82, 0x91, 0xad, 0xaa, 0xc5, 0x83, 0x90, 0x6a, 0xd5, 0x0d, 0x35, 0xf8, 0x1e, 0x93, 0xe8, 0xa4, 0xd4, 0x97, 0x9f, 0xc3, 0xa1, 0x0b, 0xb3, 0x5a, 0x28, 0x12, 0xa9, 0x96, 0xd7, 0xba, 0x5d, 0x3d, 0xd1, 0x69, 0x79, 0x02, 0xc0, 0x12, 0x6f, 0x79, 0xb8, 0xbc, 0x57, 0x29, 0x70, 0x4c, 0xb0, 0x96, 0xe5, 0x8d, 0x49, 0x2f, 0x87, 0x1f, 0x66, 0x5e, 0xe6, 0xea, 0x01, 0x7f, 0x20, 0x08, 0xbc, 0xb6, 0x9b, 0xab, 0x43, 0x6d, 0x57, 0xc6, 0xcd, 0x4f, 0x7a, 0xde, 0x01, 0x25, 0x6d, 0x86, 0x29, 0x7f, 0x11, 0x60, 0x11, 0x92, 0x07, 0xd5, 0x12, 0xf1, 0xeb, 0xb9, 0xd8, 0xdc, 0x49, 0xc2, 0x57, 0xcc, 0x66, 0x25, 0x15, 0x75, 0xae, 0xee, 0x9f, 0xcc, 0x1e, 0xae, 0xa0, 0x3b, 0xe4, 0x2b, 0xa0, 0x33, 0xd2, 0xd6, 0x87, 0x94, 0x32, 0x2f, 0x2c, 0x01, 0x96, 0x21, 0xf2, 0x9c, 0xdc, 0x4c, 0xf8, 0xf8, 0xe8, 0xd9, 0x90, 0xcd, 0x2a, 0x0d, 0x05, 0x5a, 0x74, 0x10, 0x6c, 0xd6, 0x1f, 0x5a, 0xda, 0x19, 0xe7, 0x3f, 0x4d, 0xa5, 0xef, 0x01, 0x4e, 0xde, 0x8a, 0x8e, 0x1d, 0x64, 0x1d, 0x34, 0x02, 0x96, 0xcf, 0x6e, 0xd9, 0x6b, 0x68, 0xf2, 0x44, 0x79, 0x15, 0x32, 0xb2, 0xe2, 0xaa, 0x21, 0xc0, 0x14, 0x44, 0x25, 0xe3, 0x09, 0xeb, 0x20, 0x39, 0x57, 0x3f, 0x68, 0x97, 0xab, 0x89, 0xd9, 0x5d, 0x9c, 0xd9, 0xdc, 0x8b, 0xbd, 0x41, 0x5e, 0x59, 0xe9, 0x11, 0x08, 0x1a, 0x27, 0x8a, 0x5e, 0xea, 0x7d, 0x03, 0x07, 0x01, 0xac, 0x3d, 0x62, 0x9f, 0xe4, 0x8c, 0xa5, 0xdc, 0x92, 0xe4, 0x51, 0x00, 0xf9, 0xae, 0xc6, 0x4c, 0xd9, 0x8b, 0xb6, 0xaf, 0x34, 0xad, 0x2c, 0x1f, 0x34, 0x1b, 0x6e, 0xc4, 0x36, 0x6c, 0xfc, 0xc4, 0x54, 0x20, 0x8a, 0xe8, 0x8e, 0xee, 0x69, 0xac, 0x70, 0x03, 0xd9, 0x78, 0x90, 0xe0, 0x6e, 0x72, 0xe7, 0xbc, 0xac, 0xc5, 0x09, 0xe2, 0xba, 0x36, 0xc6, 0xc2, 0x3b, 0x49, 0x1a, 0x90, 0x20, 0x58, 0x5c, 0x79, 0x57, 0x0b, 0xdb, 0xdc, 0x89, 0xbc, 0x45, 0x19, 0x86, 0xea, 0x31, 0x28, 0x02, 0x9e, 0x68, 0x53, 0x90, 0x31, 0x41, 0x8c, 0x9b, 0xef, 0x66, 0x17, 0x66, 0x82, 0x5d, 0xd6, 0x49, 0x92, 0x91, 0x84, 0x80, 0x91, 0x79, 0xb1, 0x2a, 0x92, 0x3f, 0x9d, 0xd8, 0x69, 0x8c, 0x63, 0x36, 0x64, 0x50, 0x49, 0xf1, 0x36, 0x8e, 0x8f, 0x6c, 0x1a, 0x3c, 0x1d, 0x46, 0x49, 0x1d, 0x5b, 0x63, 0xb0, 0x24, 0xb6, 0x36, 0x29, 0x61, 0x5f, 0x82, 0x40, 0xdd, 0xa0, 0x6b, 0xd5, 0xcc, 0x9f, 0x3c, 0xf4, 0xbe, 0x64, 0xa2, 0xa5, 0x43, 0xf6, 0xd9, 0x5f, 0x1b, 0x2e, 0x0f, 0x49, 0x78, 0x36, 0x31, 0xa7, 0x94, 0x6f, 0xcc, 0x0e, 0x5b, 0x6b, 0x10, 0x02, 0x0e, 0x83, 0xa1, 0x73, 0x99, 0xe3, 0x2d, 0xfe, 0x1b, 0x83, 0x04, 0x89, 0xab, 0xc6, 0x97, 0x84, 0x93, 0x91, 0xd5, 0xd9, 0x24, 0xb4, 0x8e, 0xc3, 0x7e, 0x45, 0x77, 0x60, 0xf6, 0x17 };
    u8[25] stored = { 0x01, 0x14, 0x00, 0xeb, 0xff, 0x73, 0x74, 0x6f, 0x72, 0x65, 0x64, 0x20, 0x62, 0x6c, 0x6f, 0x63, 0x6b, 0x20, 0x70, 0x61, 0x79, 0x6c, 0x6f, 0x61, 0x64 };
    u8[11] zlib_abc = { 0x78, 0x9c, 0x4b, 0x4c, 0x4a, 0x06, 0x00, 0x02, 0x4d, 0x01, 0x27 };
    u8[39] gzip_payload = { 0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x4b, 0xaf, 0xca, 0x2c, 0x50, 0xc8, 0x4d, 0xcd, 0x4d, 0x4a, 0x2d, 0x52, 0x28, 0x48, 0xac, 0xcc, 0xc9, 0x4f, 0x4c, 0x01, 0x00, 0xf1, 0xf0, 0xb4, 0x3c, 0x13, 0x00, 0x00, 0x00 };
    const u8[] zlib_view = &zlib_abc;
    array<u8> abc = std.deflate::inflate(zlib_view, std.deflate::format::rfc1950, 1024usize);
    if (len(abc) != 3usize) { return 1; }
    const u8[] abc_view = std.array::as_slice(&abc);
    if ((abc_view[0] != 97) || (abc_view[1] != 98) || (abc_view[2] != 99)) { return 2; }

    const u8[] gzip_view = &gzip_payload;
    array<u8> member = std.deflate::inflate(gzip_view, std.deflate::format::rfc1952, 1024usize);
    if (len(member) != 19usize) { return 3; }
    const u8[] member_view = std.array::as_slice(&member);
    u32 member_crc = std.hash::crc32(member_view);
    if (member_crc != 1018491121u32) { return 4; }

    const u8[] stored_view = &stored;
    array<u8> plain = std.deflate::inflate(stored_view, std.deflate::format::rfc1951, 1024usize);
    if (len(plain) != 20usize) { return 5; }
    const u8[] plain_view = std.array::as_slice(&plain);
    u32 plain_crc = std.hash::crc32(plain_view);
    if (plain_crc != 1527857339u32) { return 6; }

    const u8[] dynamic_view = &raw_dynamic;
    array<u8> text = std.deflate::inflate(dynamic_view, std.deflate::format::rfc1951, 65536usize);
    if (len(text) != 3066usize) { return 7; }
    const u8[] text_view = std.array::as_slice(&text);
    u32 text_crc = std.hash::crc32(text_view);
    if (text_crc != 2532538106u32) { return 8; }

    /* Streaming: one input byte and one output byte per step, then a reset and a replay. */
    std.deflate::inflater engine = std.deflate::inflater::create(std.deflate::format::rfc1951, 4096usize, 65536usize);
    test_observe(&engine);
    array<u8> streamed = std.array::create::<u8>();
    test_observe(&streamed);
    u8[1] cell = { 0 };
    usize fed = 0usize;
    bool ended = false;
    while (ended == false) {
        u8[] cell_view = &cell;
        usize next = (fed < len(dynamic_view)) ? fed + 1usize : fed;
        const u8[] chunk = dynamic_view[fed..next];
        std.deflate::progress step = engine.inflate(chunk, cell_view);
        if (step.produced == 1usize) { std.array::push(&streamed, cell[0]); }
        if (step.state == std.deflate::state::need_input) {
            if (step.consumed != len(chunk)) { return 9; }
            if (fed >= len(dynamic_view)) { return 10; }
            fed += step.consumed;
        }
        if (step.state == std.deflate::state::need_output) {
            if (step.produced != 1usize) { return 11; }
            fed += step.consumed;
        }
        if (step.state == std.deflate::state::end) { ended = true; fed += step.consumed; }
    }
    if (fed != len(dynamic_view)) { return 12; }
    if (len(streamed) != len(text)) { return 13; }
    const u8[] streamed_view = std.array::as_slice(&streamed);
    u32 streamed_crc = std.hash::crc32(streamed_view);
    if (streamed_crc != text_crc) { return 14; }
    if (engine.produced_bytes() != len(text)) { return 15; }

    engine.reset();
    array<u8> replay = std.alloc::bytes(8192usize, 0u8);
    u8[] replay_view = std.array::as_slice_mut(&replay);
    u8[2] tail = { 0xaa, 0xbb };
    array<u8> with_tail = std.array::create::<u8>();
    for (usize index = 0usize; index < len(dynamic_view); index += 1usize) { std.array::push(&with_tail, dynamic_view[index]); }
    std.array::push(&with_tail, tail[0]);
    std.array::push(&with_tail, tail[1]);
    const u8[] tailed_view = std.array::as_slice(&with_tail);
    std.deflate::progress whole = engine.inflate(tailed_view, replay_view);
    if (whole.state != std.deflate::state::end) { return 16; }
    if (whole.consumed != len(dynamic_view)) { return 17; }
    if (whole.produced != len(text)) { return 18; }

    /* The output limit is a checked error, not memory exhaustion. */
    bool limited = false;
    try {
        array<u8> rejected = std.deflate::inflate(dynamic_view, std.deflate::format::rfc1951, 10usize);
        (move rejected) as void;
    } catch (std.deflate::error failure) {
        limited = failure.code == std.deflate::error_code::output_limit;
    }
    if (limited == false) { return 19; }

    /* A damaged trailer is a checksum mismatch; a reserved block type is a corrupt stream. */
    u8[11] damaged = zlib_abc;
    damaged[10] = 0x28;
    const u8[] damaged_view = &damaged;
    bool mismatched = false;
    try {
        array<u8> rejected = std.deflate::inflate(damaged_view, std.deflate::format::rfc1950, 1024usize);
        (move rejected) as void;
    } catch (std.deflate::error failure) {
        mismatched = failure.code == std.deflate::error_code::checksum_mismatch;
    }
    if (mismatched == false) { return 20; }
    u8[2] reserved = { 0x07, 0x00 };
    const u8[] reserved_view = &reserved;
    bool corrupt = false;
    try {
        array<u8> rejected = std.deflate::inflate(reserved_view, std.deflate::format::rfc1951, 1024usize);
        (move rejected) as void;
    } catch (std.deflate::error failure) {
        corrupt = (failure.code == std.deflate::error_code::corrupt_stream) && (failure.offset <= 2usize);
    }
    if (corrupt == false) { return 21; }
    bool invalid = false;
    try {
        std.deflate::inflater odd = std.deflate::inflater::create(std.deflate::format::rfc1951, 1000usize, 1usize);
        (move odd) as void;
    } catch (std.deflate::error failure) {
        invalid = failure.code == std.deflate::error_code::invalid_window;
    }
    if (invalid == false) { return 22; }

    /* Compression round trips over formats, levels and windows. */
    array<u8> empty_owner = std.array::create::<u8>();
    const u8[] empty_view = std.array::as_slice(&empty_owner);
    u8[3] abc_bytes = { 97, 98, 99 };
    const u8[] abc_bytes_view = &abc_bytes;
    array<u8> repetitive = std.alloc::bytes(70000usize, 0u8);
    u8[] repetitive_mut = std.array::as_slice_mut(&repetitive);
    for (usize index = 0usize; index < len(repetitive_mut); index += 1usize) {
        repetitive_mut[index] = ((index % 7usize) * 3usize + (index / 1000usize)) as u8;
    }
    const u8[] repetitive_view = std.array::as_slice(&repetitive);
    array<u8> noisy = std.alloc::bytes(5000usize, 0u8);
    u8[] noisy_mut = std.array::as_slice_mut(&noisy);
    u32 seed = 12345u32;
    for (usize index = 0usize; index < len(noisy_mut); index += 1usize) {
        seed = core::wrapping_mul_u32(seed, 1103515245u32);
        seed = core::wrapping_add_u32(seed, 12345u32);
        noisy_mut[index] = ((seed >> 16usize) & 0xffu32) as u8;
    }
    const u8[] noisy_view = std.array::as_slice(&noisy);
    i32 verdict = 0;
    i32 verdict_2 = roundtrip(empty_view, std.deflate::format::rfc1951, 6, 32768usize, 200); if (verdict_2 != 0) { return verdict_2; }
    i32 verdict_3 = roundtrip(empty_view, std.deflate::format::rfc1952, 0, 32768usize, 220); if (verdict_3 != 0) { return verdict_3; }
    i32 verdict_4 = roundtrip(abc_bytes_view, std.deflate::format::rfc1950, 9, 32768usize, 240); if (verdict_4 != 0) { return verdict_4; }
    i32 verdict_5 = roundtrip(text_view, std.deflate::format::rfc1951, 1, 32768usize, 260); if (verdict_5 != 0) { return verdict_5; }
    i32 verdict_6 = roundtrip(text_view, std.deflate::format::rfc1950, 6, 32768usize, 280); if (verdict_6 != 0) { return verdict_6; }
    i32 verdict_7 = roundtrip(text_view, std.deflate::format::rfc1952, 9, 32768usize, 300); if (verdict_7 != 0) { return verdict_7; }
    i32 verdict_8 = roundtrip(text_view, std.deflate::format::rfc1951, 0, 32768usize, 320); if (verdict_8 != 0) { return verdict_8; }
    i32 verdict_9 = roundtrip(text_view, std.deflate::format::rfc1950, 4, 256usize, 340); if (verdict_9 != 0) { return verdict_9; }
    i32 verdict_10 = roundtrip(repetitive_view, std.deflate::format::rfc1951, 6, 32768usize, 360); if (verdict_10 != 0) { return verdict_10; }
    i32 verdict_11 = roundtrip(repetitive_view, std.deflate::format::rfc1952, 3, 1024usize, 380); if (verdict_11 != 0) { return verdict_11; }
    i32 verdict_12 = roundtrip(noisy_view, std.deflate::format::rfc1950, 9, 32768usize, 400); if (verdict_12 != 0) { return verdict_12; }
    i32 verdict_13 = roundtrip(noisy_view, std.deflate::format::rfc1951, 2, 4096usize, 420); if (verdict_13 != 0) { return verdict_13; }
    array<u8> shrunk = std.deflate::deflate(repetitive_view, std.deflate::format::rfc1951, 6);
    if (len(shrunk) * 20usize > len(repetitive_view)) { return 440; }

    /* A sync flush terminates on a byte boundary and the stream stays decodable after finish. */
    std.deflate::deflater syncing = std.deflate::deflater::create(std.deflate::format::rfc1951, 6, 32768usize);
    array<u8> synced = std.array::create::<u8>();
    test_observe(&synced);
    array<u8> sink = std.alloc::bytes(65536usize, 0u8);
    u8[] sink_view = std.array::as_slice_mut(&sink);
    std.deflate::progress first_half = syncing.deflate(text_view[0usize..1000usize], sink_view, std.deflate::flush::sync);
    if ((first_half.state != std.deflate::state::need_input) || (first_half.consumed != 1000usize)) { return 441; }
    if (first_half.produced < 5usize) { return 442; }
    if ((sink_view[first_half.produced - 4usize] != 0) || (sink_view[first_half.produced - 3usize] != 0) ||
        (sink_view[first_half.produced - 2usize] != 0xff) || (sink_view[first_half.produced - 1usize] != 0xff)) { return 443; }
    for (usize index = 0usize; index < first_half.produced; index += 1usize) { std.array::push(&synced, sink_view[index]); }
    std.deflate::progress second_half = syncing.deflate(text_view[1000usize..len(text_view)], sink_view, std.deflate::flush::finish);
    if (second_half.state != std.deflate::state::end) { return 444; }
    for (usize index = 0usize; index < second_half.produced; index += 1usize) { std.array::push(&synced, sink_view[index]); }
    const u8[] synced_view = std.array::as_slice(&synced);
    array<u8> unsynced = std.deflate::inflate(synced_view, std.deflate::format::rfc1951, 65536usize);
    if (len(unsynced) != len(text_view)) { return 445; }
    const u8[] unsynced_view = std.array::as_slice(&unsynced);
    if (std.hash::crc32(unsynced_view) != text_crc) { return 446; }
    bool finished_error = false;
    try {
        std.deflate::progress late = syncing.deflate(empty_view, sink_view, std.deflate::flush::finish);
        late as void;
    } catch (std.deflate::error failure) { finished_error = failure.code == std.deflate::error_code::finished; }
    if (finished_error == false) { return 447; }
    bool bad_level = false;
    try {
        std.deflate::deflater loud = std.deflate::deflater::create(std.deflate::format::rfc1951, 10, 32768usize);
        (move loud) as void;
    } catch (std.deflate::error failure) { bad_level = failure.code == std.deflate::error_code::invalid_level; }
    if (bad_level == false) { return 448; }
    return 0;
}

i32 main() {
    try {
        try {
            i32 status = checks();
            return status;
        } catch (std.deflate::error failure) { return 100 + (core::enum_ordinal(failure.code) as i32); }
        catch (std.alloc::alloc_error failure) { failure as void; throw TestAssertionFailed {.code = 90}; }
        catch (std.array::push_error<u8> failure) { failure as void; throw TestAssertionFailed {.code = 91}; }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };

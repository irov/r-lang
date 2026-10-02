module bench.deflate_roundtrip;

import std.deflate;

/* Compress and decompress a 64 KiB pseudo-text payload 200 times through std.deflate
   (zlib framing, level 6); std.deflate is written in R, the C mirror is the system zlib. */
i32 main() {
    try {
        bytes payload = std.alloc::bytes(65536usize, 0u8);
        {
            u8[] fill = payload.as_slice_mut();
            for (usize index = 0usize; index < 65536usize; index += 1usize) {
                fill[index] = (((index * 31usize + index / 7usize) % 96usize) + 32usize) as u8;
            }
        }
        usize iterations = 200usize;
        usize total = 0usize;
        for (usize round = 0usize; round < iterations; round += 1usize) {
            const u8[] view = payload.as_slice();
            array<u8> packed = std.deflate::deflate(view, std.deflate::format::rfc1950, 6u8);
            const u8[] packed_view = packed.as_slice();
            array<u8> restored = std.deflate::inflate(packed_view, std.deflate::format::rfc1950, 65537usize);
            const u8[] restored_view = restored.as_slice();
            if (len(restored_view) != 65536usize) { return 70; }
            total += len(restored_view) + (restored_view[(round * 1000usize) % 65536usize] as usize);
        }
        return (total % 109usize) as i32;
    } catch (std.deflate::error failure) { return 65; }
}

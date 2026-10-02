module test.codegen.async_consteval;

/* R-EXPR-0032: translation-time values inside asynchronous functions, lowered through MIR. */

struct Header { u32 magic; u16 length; u8 flags; };

usize frame_bytes(usize payload) {
    return sizeof(Header) + payload + sizeof(u32);
}

u32 mix(u32 seed, u32 rounds) {
    u32 value = seed;
    for (u32 i = 0u32; i < rounds; i++) {
        value = (value * 31u32) ^ (value >> 3u32);
    }
    return value;
}

u32[8] mixes() {
    u32[8] table = {};
    for (u32 i in 0u32..8u32) {
        table[i] = mix(i + 1u32, 4u32);
    }
    return table;
}

const usize FRAME = frame_bytes(32usize);
const u32[8] MIXES = mixes();

struct Frame { Header header; u8[FRAME] bytes; };

async u32 checksum(u32 extra) {
    u32 total = mix(7u32, 3u32);
    for (usize i = 0usize; i < 8usize; i++) {
        total ^= MIXES[i];
    }
    return total ^ extra;
}

async i32 main() {
    Frame frame = {};
    u8[frame_bytes(8usize)] local = {};
    bool frame_ok = len(frame.bytes) == 44usize;
    bool local_ok = len(local) == 20usize;
    try {
        task<u32> first = checksum(0u32);
        u32 value = await move first;
        bool value_ok = value != 0u32;
        task<u32> second = checksum(value);
        u32 cleared = await move second;
        bool cleared_ok = cleared == 0u32;
        if (frame_ok == false || local_ok == false || cleared_ok == false || value_ok == false) {
            return 1;
        }
        return 0;
    } catch (std.async::start_error failure) {
        failure as void;
        return 2;
    }
}

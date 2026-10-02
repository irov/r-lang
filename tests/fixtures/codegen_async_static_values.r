module test.codegen.async_static_values;

/* R-META-0002, R-META-0003: constant `@if` conditions in asynchronous functions, lowered
   through MIR. Inactive branches name missing declarations and are never checked. */

struct Header { u32 magic; u16 length; u8 flags; };

usize frame_bytes(usize payload) {
    return sizeof(Header) + payload + sizeof(u32);
}

const usize FRAME = frame_bytes(32usize);

@if (FRAME in 1usize..64usize) {
    struct Frame { Header header; u8[FRAME] bytes; };
} @else {
    struct Frame { Unknown bytes; };
}

async u32 checksum(u32 seed) {
    @if (frame_bytes(0usize) == 12usize) {
        return seed ^ 0x5Au32;
    } @else {
        return missing_checksum;
    }
}

@generic<const usize N>
async usize fill(u8[N] data) {
    @if (N <= 8usize) {
        return len(data);
    } @else {
        return N * 2usize;
    }
}

async i32 main() {
    Frame frame = {};
    bool frame_ok = len(frame.bytes) == 44usize;
    try {
        task<u32> first = checksum(0x5Au32);
        u32 value = await move first;
        bool value_ok = value == 0u32;
        u8[4] small = {};
        u8[16] large = {};
        task<usize> short_fill = fill(small);
        usize short_length = await move short_fill;
        bool short_ok = short_length == 4usize;
        task<usize> long_fill = fill(large);
        usize long_length = await move long_fill;
        bool long_ok = long_length == 32usize;
        if (frame_ok == false || value_ok == false || short_ok == false || long_ok == false) {
            return 1;
        }
        return 0;
    } catch (std.async::start_error failure) {
        failure as void;
        return 2;
    }
}

module test.codegen.consteval;

/* R-FUNC-0023, R-EXPR-0032: ordinary functions evaluated during translation. */

enum Level : u8 { low = 1, mid = 5, high = 9, };
struct Header { u32 magic; u16 length; u8 flags; };
struct Limits { u32 low; u32 high; };

u32 crc_entry(u32 index) {
    u32 c = index;
    for (i32 k = 0; k < 8; k++) {
        if ((c & 1u32) != 0u32) {
            c = 0xEDB88320u32 ^ (c >> 1u32);
        } else {
            c = c >> 1u32;
        }
    }
    return c;
}

u32[256] crc_table() {
    u32[256] table = {};
    for (u32 i in 0u32..256u32) {
        table[i] = crc_entry(i);
    }
    return table;
}

usize frame_bytes(usize payload) {
    return sizeof(Header) + payload + sizeof(u32);
}

usize checked_capacity(usize requested) {
    if (requested == 0usize || requested > 65536usize) {
        panic("capacity out of range");
    }
    return requested;
}

u8 code_of(u8 value) {
    u8 code = value;
    code += 1u8;
    return code;
}

u32 Limits::span(const Limits* this) {
    return this->high - this->low;
}

Limits make_limits(u32 low) {
    Limits limits = Limits { .low = low, .high = low + 100u32 };
    return limits;
}

u32 sum(const u32[] values) {
    u32 total = 0u32;
    for (usize i = 0usize; i < len(values); i++) {
        total += values[i];
    }
    return total;
}

u32 table_sum() {
    u32[4] values = {1u32, 2u32, 3u32, 4u32};
    return sum(&values);
}

u8 weight(Level level) {
    switch (level) {
    case Level::low:
        return 1u8;
    case Level::mid:
        return 2u8;
    default:
        return 3u8;
    }
}

constexpr str level_name(Level level) {
    return core::enum_name(level);
}

u32 wrapped(u32 value) {
    return core::wrapping_add_u32(value, 0xFFFFFFFFu32);
}

@generic<T: copy>
T second(T first, T value) {
    first as void;
    return value;
}

/* A run-time failure path: the call below has constant arguments but is never executed. */
i32 stop(str message) {
    panic(message);
}

const u32[256] CRC = crc_table();
const usize FRAME = frame_bytes(64usize);
const usize CAPACITY = checked_capacity(1024usize);
const Limits LIMITS = Limits { .low = 10u32, .high = 250u32 };

/* Module-scope types that need translation-time values. */
struct Frame {
    Header header;
    u8[FRAME] bytes;
    u8[frame_bytes(0usize)] trailer;
};

enum Code : u8 { first = code_of(1u8), second = code_of(5u8), };

u32 crc(const u8[] bytes) {
    u32 c = 0xFFFFFFFFu32;
    for (usize i = 0usize; i < len(bytes); i++) {
        c = CRC[((c ^ (bytes[i] as u32)) & 0xFFu32) as usize] ^ (c >> 8u32);
    }
    return c ^ 0xFFFFFFFFu32;
}

i32 classify(u8 code) {
    switch (code) {
    case code_of(1u8):
        return 1;
    case code_of(5u8):
        return 2;
    default:
        return 0;
    }
}

i32 main() {
    Frame frame = {};
    u8[frame_bytes(4usize)] local = {};
    u8[core::enum_count::<Level>()] per_level = {};
    static const u32[4] seeds = {crc_entry(1u32), crc_entry(2u32), 0u32, 0u32};
    u8[5] text = {104u8, 101u8, 108u8, 108u8, 111u8};
    bool layout_ok = len(frame.bytes) == 76usize;
    bool trailer_ok = len(frame.trailer) == 12usize;
    bool local_ok = len(local) == 16usize;
    bool levels_ok = len(per_level) == 3usize;
    bool seeds_ok = seeds[1] == 0xEE0E612Cu32;
    bool crc_ok = crc(&text) == 0x3610A686u32;
    bool span_ok = LIMITS.span() == 240u32;
    Limits made = make_limits(5u32);
    bool made_ok = made.high - made.low == 100u32 && made.low == 5u32;
    bool sum_ok = table_sum() == 10u32;
    bool weight_ok = weight(Level::mid) == 2u8;
    constexpr str high_name = level_name(Level::high);
    bool name_ok = len(high_name) == 4usize;
    bool wrap_ok = wrapped(1u32) == 0u32;
    bool generic_ok = second(3, 9) == 9;
    bool label_ok = classify(6u8) == 2;
    constexpr str code_name = core::enum_name(Code::second);
    bool code_ok = len(code_name) == 6usize;
    bool constants_ok = FRAME == 76usize && CAPACITY == 1024usize;
    if (layout_ok == false || trailer_ok == false || local_ok == false || levels_ok == false ||
        seeds_ok == false || crc_ok == false || span_ok == false || sum_ok == false ||
        weight_ok == false || name_ok == false || wrap_ok == false || generic_ok == false ||
        label_ok == false || code_ok == false || constants_ok == false || made_ok == false) {
        return 1;
    }
    if (len(frame.bytes) == 0usize) {
        i32 stopped = stop("frame without payload");
        return stopped;
    }
    return 0;
}

module example.binary.codec;
import std.bytes;

// A byte is written as two hexadecimal digits, including its leading zero.
void append_hex(std.string::string* output, const u8[] data) throws std.alloc::alloc_error {
    str digits = "0123456789abcdef";
    const u8[] alphabet = digits;
    for (const u8* byte in &data) {
        char high = alphabet[*byte >> 4u8] as char;
        char low = alphabet[*byte & 15u8] as char;
        output->append(high);
        output->append(low);
    }
}

std.string::string checksums(str source) throws std.alloc::alloc_error {
    std.string::string output = std.string::create();
    u32 checksum = std.hash::crc32(source);
    std.string::string crc = f"crc32 {checksum}\nmd5 ";
    str crc_text = crc;
    output.append(crc_text);
    std.hash::md5_digest md5 = std.hash::md5(source);
    append_hex(&output, md5.bytes);
    output.append("\nsha1 ");
    std.hash::sha1_digest sha1 = std.hash::sha1(source);
    append_hex(&output, sha1.bytes);
    output.append("\nsha256 ");
    std.hash::sha256_digest sha256 = std.hash::sha256(source);
    append_hex(&output, sha256.bytes);
    output.append("\nsha512 ");
    std.hash::sha512_digest sha512 = std.hash::sha512(source);
    append_hex(&output, sha512.bytes);
    output.append("\n");
    return move output;
}

// The fixed part of a packet: one flag byte, u16 version, u32 payload length, u64 sequence.
struct Header {
    u8 flags;
    u16 version;
    u32 length;
    u64 sequence;
};

// A field type that writes and reads itself in little-endian order.
trait Wire {
    void put(const Self* this, bytes* wire) throws std.alloc::alloc_error;
    void take(Self* this, std.bytes::cursor* at, const u8[] wire) throws std.bytes::bytes_error;
};

impl Wire for u8 {
    void put(const Self* this, bytes* wire) throws std.alloc::alloc_error { std.bytes::append_u8(wire, *this); }
    void take(Self* this, std.bytes::cursor* at, const u8[] wire) throws std.bytes::bytes_error { *this = at->read_u8(wire); }
};

impl Wire for u16 {
    void put(const Self* this, bytes* wire) throws std.alloc::alloc_error { wire->append_u16_le(*this); }
    void take(Self* this, std.bytes::cursor* at, const u8[] wire) throws std.bytes::bytes_error { *this = at->read_u16_le(wire); }
};

impl Wire for u32 {
    void put(const Self* this, bytes* wire) throws std.alloc::alloc_error { wire->append_u32_le(*this); }
    void take(Self* this, std.bytes::cursor* at, const u8[] wire) throws std.bytes::bytes_error { *this = at->read_u32_le(wire); }
};

impl Wire for u64 {
    void put(const Self* this, bytes* wire) throws std.alloc::alloc_error { wire->append_u64_le(*this); }
    void take(Self* this, std.bytes::cursor* at, const u8[] wire) throws std.bytes::bytes_error { *this = at->read_u64_le(wire); }
};

// The header needs no hand-written encoder: a translation-time loop repeats its block for each
// field of T, and core::field borrows the field of each repetition with that field's own type.
@generic<T: fields(Wire)>
void put_fields(const T* value, bytes* wire) throws std.alloc::alloc_error {
    for (constexpr usize index in 0usize..core::field_count::<T>()) {
        core::field(value, index)->put(wire);
    }
}

@generic<T: fields(Wire)>
void take_fields(T* value, std.bytes::cursor* at, const u8[] wire) throws std.bytes::bytes_error {
    for (constexpr usize index in 0usize..core::field_count::<T>()) {
        core::field_mut(value, index)->take(at, wire);
    }
}

// Wire format: the header, then the payload. The flags are read bit by bit, the header field by
// field.
std.string::string packet(str payload, u64 sequence)
    throws std.alloc::alloc_error, std.bits::read_error, std.bytes::bytes_error, std.convert::range_error {
    usize size = len(payload);
    Header header = {.flags = 5u8, .version = 1u16, .length = std.convert::checked_u32(size), .sequence = sequence};
    bytes wire = std.bytes::with_capacity(15usize);
    put_fields(&header, &wire);
    wire.append(payload);
    std.bits::lsb_reader reader = {};
    u64 urgent = std.bits::read(wire, &reader, 1u8);
    u64 encoding = std.bits::read(wire, &reader, 2u8);
    reader.align_byte();
    Header decoded = {.flags = 0u8, .version = 0u16, .length = 0u32, .sequence = 0u64};
    std.bytes::cursor at = {};
    take_fields(&decoded, &at, wire);
    std.string::string output = f"urgent={urgent} encoding={encoding} version={decoded.version} length={decoded.length} sequence={decoded.sequence}\n";
    append_hex(&output, wire);
    output.append("\n");
    return move output;
}

// The bit operations of one value: its zero and one bit counts, its bytes reversed and its
// rotations by one byte.
std.string::string bits(u64 value) throws std.alloc::alloc_error {
    u32 leading = core::leading_zeros_u64(value);
    u32 trailing = core::trailing_zeros_u64(value);
    u32 ones = core::count_ones_u64(value);
    u64 swapped = core::swap_bytes_u64(value);
    u64 left = core::rotate_left_u64(value, 8u32);
    u64 right = core::rotate_right_u64(value, 8u32);
    return f"leading_zeros={leading} trailing_zeros={trailing} count_ones={ones}\nswap_bytes={swapped:016x} rotate_left={left:016x} rotate_right={right:016x}\n";
}

// Arithmetic on 64-bit limbs: the 128-bit product, the sum and difference with their carry and
// borrow, and the product divided back by the second factor.
std.string::string wide(u64 left, u64 right) throws std.alloc::alloc_error {
    auto (low, high) = core::widening_mul_u64(left, right);
    auto (sum, carry) = core::carrying_add_u64(left, right, false);
    auto (difference, borrow) = core::borrowing_sub_u64(left, right, false);
    std.string::string output = f"product high={high:016x} low={low:016x}\nsum={sum:016x} carry={carry}\ndifference={difference:016x} borrow={borrow}\n";
    if (right == 0u64) {
        output.append("quotient none\n");
        return move output;
    }
    auto (quotient, remainder) = core::narrowing_div_u64(high, low, right);
    std.string::string division = f"quotient={quotient} remainder={remainder}\n";
    output.append(division);
    return move output;
}

// A fixed-width device label: pad with spaces, then prepend a one-byte status.
std.string::string label(str source) throws std.alloc::alloc_error, std.bytes::bytes_error {
    u8[16] storage = {};
    u8[] writable = &storage;
    std.bytes::fill(writable, 32u8);
    const u8[] bytes = source;
    usize length = len(bytes);
    if (length > 15usize) { length = 15usize; }
    const u8[] shortened = bytes[0usize..length];
    usize copied = std.bytes::copy(writable, shortened);
    usize shifted = std.bytes::copy_within(writable, 1usize, 0usize, 15usize);
    writable[0usize] = 43u8;
    std.string::string output = f"copied={copied} shifted={shifted}\n";
    append_hex(&output, storage);
    output.append("\n");
    return move output;
}

std.string::string compare(str source, str pattern) throws std.alloc::alloc_error {
    bool equal = std.bytes::equal(source, pattern);
    i32 order = std.bytes::compare(source, pattern);
    bool prefix = std.bytes::starts_with(source, pattern);
    bool suffix = std.bytes::ends_with(source, pattern);
    bool utf8 = std.utf8::is_valid(source);
    std.string::string output = f"equal={equal} order={order} prefix={prefix} suffix={suffix} utf8={utf8}\n";
    o<usize> found = std.bytes::find_slice(source, pattern);
    switch (found) {
    case variant o::some(position):
        usize offset = *position;
        std.string::string line = f"pattern={offset}\n";
        str text = line;
        output.append(text);
        break;
    case variant o::none: output.append("pattern=none\n"); break;
    }
    o<usize> newline = std.bytes::find(source, 10u8);
    switch (newline) {
    case variant o::some(position):
        usize offset = *position;
        std.string::string line = f"newline={offset}\n";
        str text = line;
        output.append(text);
        break;
    case variant o::none: output.append("newline=none\n"); break;
    }
    return move output;
}

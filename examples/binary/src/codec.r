module example.binary.codec;

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

// Wire format: one flag byte, u16 version, u32 payload length, u64 sequence, payload.
std.string::string packet(str payload, u64 sequence)
    throws std.alloc::alloc_error, std.bits::read_error, std.convert::range_error {
    usize size = len(payload);
    u32 length = std.convert::checked_u32(size);
    bytes wire = std.bytes::with_capacity(15usize);
    std.bytes::append_u8(&wire, 5u8);
    wire.append_u16_le(1u16);
    wire.append_u32_le(length);
    wire.append_u64_le(sequence);
    wire.append(payload);
    std.bits::lsb_reader reader = {};
    u64 urgent = std.bits::read(wire, &reader, 1u8);
    u64 encoding = std.bits::read(wire, &reader, 2u8);
    reader.align_byte();
    u64 version = std.bits::read(wire, &reader, 16u8);
    u64 decoded_length = std.bits::read(wire, &reader, 32u8);
    u64 decoded_sequence = std.bits::read(wire, &reader, 64u8);
    std.string::string output = f"urgent={urgent} encoding={encoding} version={version} length={decoded_length} sequence={decoded_sequence}\n";
    append_hex(&output, wire);
    output.append("\n");
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

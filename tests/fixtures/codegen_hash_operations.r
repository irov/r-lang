module test.codegen.hash_operations;

protected u8[3] sample() {
    u8[3] value = {0x61, 0x62, 0x63};
    return value;
}

protected i32 sync_checks() throws std.alloc::alloc_error {
    constexpr str literal = "abc";
    str text = literal;
    u8[3] fixed = sample();
    bytes owned = {};
    array<u8> array_value = {};
    std.bytes::append(&owned, text);
    std.bytes::append(&array_value, fixed);
    {
        const u8[] shared = &fixed;
        u32 crc = std.hash::crc32(shared);
        if (crc != 0x3524_41c2) { return 1; }
        std.hash::md5_digest md5 = std.hash::md5(shared);
        std.hash::md5_digest md5_copy = md5;
        if ((md5.bytes[0] != 0x90) || (md5_copy.bytes[15] != 0x72)) { return 2; }
        std.hash::sha1_digest sha1 = std.hash::sha1(shared);
        std.hash::sha1_digest sha1_copy = sha1;
        if ((sha1.bytes[0] != 0xa9) || (sha1_copy.bytes[19] != 0x9d)) { return 3; }
        std.hash::sha256_digest sha256 = std.hash::sha256(shared);
        std.hash::sha256_digest sha256_copy = sha256;
        if ((sha256.bytes[0] != 0xba) || (sha256_copy.bytes[31] != 0xad)) { return 4; }
        std.hash::sha512_digest sha512 = std.hash::sha512(shared);
        std.hash::sha512_digest sha512_copy = sha512;
        if ((sha512.bytes[0] != 0xdd) || (sha512_copy.bytes[63] != 0x9f)) { return 5; }
    }
    {
        u8[] mutable = &fixed;
        u32 crc = std.hash::crc32(mutable);
        if (crc != 0x3524_41c2) { return 1; }
        std.hash::md5_digest md5 = std.hash::md5(mutable);
        std.hash::md5_digest md5_copy = md5;
        if ((md5.bytes[0] != 0x90) || (md5_copy.bytes[15] != 0x72)) { return 2; }
        std.hash::sha1_digest sha1 = std.hash::sha1(mutable);
        std.hash::sha1_digest sha1_copy = sha1;
        if ((sha1.bytes[0] != 0xa9) || (sha1_copy.bytes[19] != 0x9d)) { return 3; }
        std.hash::sha256_digest sha256 = std.hash::sha256(mutable);
        std.hash::sha256_digest sha256_copy = sha256;
        if ((sha256.bytes[0] != 0xba) || (sha256_copy.bytes[31] != 0xad)) { return 4; }
        std.hash::sha512_digest sha512 = std.hash::sha512(mutable);
        std.hash::sha512_digest sha512_copy = sha512;
        if ((sha512.bytes[0] != 0xdd) || (sha512_copy.bytes[63] != 0x9f)) { return 5; }
    }
    {
        u32 crc = std.hash::crc32(fixed);
        if (crc != 0x3524_41c2) { return 1; }
        std.hash::md5_digest md5 = std.hash::md5(fixed);
        std.hash::md5_digest md5_copy = md5;
        if ((md5.bytes[0] != 0x90) || (md5_copy.bytes[15] != 0x72)) { return 2; }
        std.hash::sha1_digest sha1 = std.hash::sha1(fixed);
        std.hash::sha1_digest sha1_copy = sha1;
        if ((sha1.bytes[0] != 0xa9) || (sha1_copy.bytes[19] != 0x9d)) { return 3; }
        std.hash::sha256_digest sha256 = std.hash::sha256(fixed);
        std.hash::sha256_digest sha256_copy = sha256;
        if ((sha256.bytes[0] != 0xba) || (sha256_copy.bytes[31] != 0xad)) { return 4; }
        std.hash::sha512_digest sha512 = std.hash::sha512(fixed);
        std.hash::sha512_digest sha512_copy = sha512;
        if ((sha512.bytes[0] != 0xdd) || (sha512_copy.bytes[63] != 0x9f)) { return 5; }
    }
    {
        u32 crc = std.hash::crc32(owned);
        if (crc != 0x3524_41c2) { return 1; }
        std.hash::md5_digest md5 = std.hash::md5(owned);
        std.hash::md5_digest md5_copy = md5;
        if ((md5.bytes[0] != 0x90) || (md5_copy.bytes[15] != 0x72)) { return 2; }
        std.hash::sha1_digest sha1 = std.hash::sha1(owned);
        std.hash::sha1_digest sha1_copy = sha1;
        if ((sha1.bytes[0] != 0xa9) || (sha1_copy.bytes[19] != 0x9d)) { return 3; }
        std.hash::sha256_digest sha256 = std.hash::sha256(owned);
        std.hash::sha256_digest sha256_copy = sha256;
        if ((sha256.bytes[0] != 0xba) || (sha256_copy.bytes[31] != 0xad)) { return 4; }
        std.hash::sha512_digest sha512 = std.hash::sha512(owned);
        std.hash::sha512_digest sha512_copy = sha512;
        if ((sha512.bytes[0] != 0xdd) || (sha512_copy.bytes[63] != 0x9f)) { return 5; }
    }
    {
        u32 crc = std.hash::crc32(array_value);
        if (crc != 0x3524_41c2) { return 1; }
        std.hash::md5_digest md5 = std.hash::md5(array_value);
        std.hash::md5_digest md5_copy = md5;
        if ((md5.bytes[0] != 0x90) || (md5_copy.bytes[15] != 0x72)) { return 2; }
        std.hash::sha1_digest sha1 = std.hash::sha1(array_value);
        std.hash::sha1_digest sha1_copy = sha1;
        if ((sha1.bytes[0] != 0xa9) || (sha1_copy.bytes[19] != 0x9d)) { return 3; }
        std.hash::sha256_digest sha256 = std.hash::sha256(array_value);
        std.hash::sha256_digest sha256_copy = sha256;
        if ((sha256.bytes[0] != 0xba) || (sha256_copy.bytes[31] != 0xad)) { return 4; }
        std.hash::sha512_digest sha512 = std.hash::sha512(array_value);
        std.hash::sha512_digest sha512_copy = sha512;
        if ((sha512.bytes[0] != 0xdd) || (sha512_copy.bytes[63] != 0x9f)) { return 5; }
    }
    {
        u32 crc = std.hash::crc32(text);
        if (crc != 0x3524_41c2) { return 1; }
        std.hash::md5_digest md5 = std.hash::md5(text);
        std.hash::md5_digest md5_copy = md5;
        if ((md5.bytes[0] != 0x90) || (md5_copy.bytes[15] != 0x72)) { return 2; }
        std.hash::sha1_digest sha1 = std.hash::sha1(text);
        std.hash::sha1_digest sha1_copy = sha1;
        if ((sha1.bytes[0] != 0xa9) || (sha1_copy.bytes[19] != 0x9d)) { return 3; }
        std.hash::sha256_digest sha256 = std.hash::sha256(text);
        std.hash::sha256_digest sha256_copy = sha256;
        if ((sha256.bytes[0] != 0xba) || (sha256_copy.bytes[31] != 0xad)) { return 4; }
        std.hash::sha512_digest sha512 = std.hash::sha512(text);
        std.hash::sha512_digest sha512_copy = sha512;
        if ((sha512.bytes[0] != 0xdd) || (sha512_copy.bytes[63] != 0x9f)) { return 5; }
    }
    {
        u32 crc = std.hash::crc32(literal);
        if (crc != 0x3524_41c2) { return 1; }
        std.hash::md5_digest md5 = std.hash::md5(literal);
        std.hash::md5_digest md5_copy = md5;
        if ((md5.bytes[0] != 0x90) || (md5_copy.bytes[15] != 0x72)) { return 2; }
        std.hash::sha1_digest sha1 = std.hash::sha1(literal);
        std.hash::sha1_digest sha1_copy = sha1;
        if ((sha1.bytes[0] != 0xa9) || (sha1_copy.bytes[19] != 0x9d)) { return 3; }
        std.hash::sha256_digest sha256 = std.hash::sha256(literal);
        std.hash::sha256_digest sha256_copy = sha256;
        if ((sha256.bytes[0] != 0xba) || (sha256_copy.bytes[31] != 0xad)) { return 4; }
        std.hash::sha512_digest sha512 = std.hash::sha512(literal);
        std.hash::sha512_digest sha512_copy = sha512;
        if ((sha512.bytes[0] != 0xdd) || (sha512_copy.bytes[63] != 0x9f)) { return 5; }
    }
    if ((fixed[0] != 0x61) || (fixed[1] != 0x62) || (fixed[2] != 0x63)) { return 6; }
    std.bytes::append_u8(&owned, 0x64);
    std.bytes::append_u8(&array_value, 0x64);
    bytes moved = move owned;
    array<u8> moved_array = move array_value;
    if ((len(moved) != 4) || (len(moved_array) != 4)) { return 7; }
    const u8[] moved_view = std.array::as_slice(&moved);
    const u8[] moved_array_view = std.array::as_slice(&moved_array);
    if ((moved_view[0] != 0x61) || (moved_view[1] != 0x62) || (moved_view[2] != 0x63) || (moved_view[3] != 0x64)) { return 8; }
    if ((moved_array_view[0] != 0x61) || (moved_array_view[1] != 0x62) || (moved_array_view[2] != 0x63) || (moved_array_view[3] != 0x64)) { return 9; }
    return 0;
}

protected async i32 async_checks() throws std.alloc::alloc_error {
    constexpr str literal = "abc";
    str text = literal;
    u8[3] fixed = sample();
    bytes owned = {};
    array<u8> array_value = {};
    std.bytes::append(&owned, text);
    std.bytes::append(&array_value, fixed);
    {
        const u8[] shared = &fixed;
        u32 crc = std.hash::crc32(shared);
        if (crc != 0x3524_41c2) { return 1; }
        std.hash::md5_digest md5 = std.hash::md5(shared);
        std.hash::md5_digest md5_copy = md5;
        if ((md5.bytes[0] != 0x90) || (md5_copy.bytes[15] != 0x72)) { return 2; }
        std.hash::sha1_digest sha1 = std.hash::sha1(shared);
        std.hash::sha1_digest sha1_copy = sha1;
        if ((sha1.bytes[0] != 0xa9) || (sha1_copy.bytes[19] != 0x9d)) { return 3; }
        std.hash::sha256_digest sha256 = std.hash::sha256(shared);
        std.hash::sha256_digest sha256_copy = sha256;
        if ((sha256.bytes[0] != 0xba) || (sha256_copy.bytes[31] != 0xad)) { return 4; }
        std.hash::sha512_digest sha512 = std.hash::sha512(shared);
        std.hash::sha512_digest sha512_copy = sha512;
        if ((sha512.bytes[0] != 0xdd) || (sha512_copy.bytes[63] != 0x9f)) { return 5; }
    }
    {
        u8[] mutable = &fixed;
        u32 crc = std.hash::crc32(mutable);
        if (crc != 0x3524_41c2) { return 1; }
        std.hash::md5_digest md5 = std.hash::md5(mutable);
        std.hash::md5_digest md5_copy = md5;
        if ((md5.bytes[0] != 0x90) || (md5_copy.bytes[15] != 0x72)) { return 2; }
        std.hash::sha1_digest sha1 = std.hash::sha1(mutable);
        std.hash::sha1_digest sha1_copy = sha1;
        if ((sha1.bytes[0] != 0xa9) || (sha1_copy.bytes[19] != 0x9d)) { return 3; }
        std.hash::sha256_digest sha256 = std.hash::sha256(mutable);
        std.hash::sha256_digest sha256_copy = sha256;
        if ((sha256.bytes[0] != 0xba) || (sha256_copy.bytes[31] != 0xad)) { return 4; }
        std.hash::sha512_digest sha512 = std.hash::sha512(mutable);
        std.hash::sha512_digest sha512_copy = sha512;
        if ((sha512.bytes[0] != 0xdd) || (sha512_copy.bytes[63] != 0x9f)) { return 5; }
    }
    {
        u32 crc = std.hash::crc32(fixed);
        if (crc != 0x3524_41c2) { return 1; }
        std.hash::md5_digest md5 = std.hash::md5(fixed);
        std.hash::md5_digest md5_copy = md5;
        if ((md5.bytes[0] != 0x90) || (md5_copy.bytes[15] != 0x72)) { return 2; }
        std.hash::sha1_digest sha1 = std.hash::sha1(fixed);
        std.hash::sha1_digest sha1_copy = sha1;
        if ((sha1.bytes[0] != 0xa9) || (sha1_copy.bytes[19] != 0x9d)) { return 3; }
        std.hash::sha256_digest sha256 = std.hash::sha256(fixed);
        std.hash::sha256_digest sha256_copy = sha256;
        if ((sha256.bytes[0] != 0xba) || (sha256_copy.bytes[31] != 0xad)) { return 4; }
        std.hash::sha512_digest sha512 = std.hash::sha512(fixed);
        std.hash::sha512_digest sha512_copy = sha512;
        if ((sha512.bytes[0] != 0xdd) || (sha512_copy.bytes[63] != 0x9f)) { return 5; }
    }
    {
        u32 crc = std.hash::crc32(owned);
        if (crc != 0x3524_41c2) { return 1; }
        std.hash::md5_digest md5 = std.hash::md5(owned);
        std.hash::md5_digest md5_copy = md5;
        if ((md5.bytes[0] != 0x90) || (md5_copy.bytes[15] != 0x72)) { return 2; }
        std.hash::sha1_digest sha1 = std.hash::sha1(owned);
        std.hash::sha1_digest sha1_copy = sha1;
        if ((sha1.bytes[0] != 0xa9) || (sha1_copy.bytes[19] != 0x9d)) { return 3; }
        std.hash::sha256_digest sha256 = std.hash::sha256(owned);
        std.hash::sha256_digest sha256_copy = sha256;
        if ((sha256.bytes[0] != 0xba) || (sha256_copy.bytes[31] != 0xad)) { return 4; }
        std.hash::sha512_digest sha512 = std.hash::sha512(owned);
        std.hash::sha512_digest sha512_copy = sha512;
        if ((sha512.bytes[0] != 0xdd) || (sha512_copy.bytes[63] != 0x9f)) { return 5; }
    }
    {
        u32 crc = std.hash::crc32(array_value);
        if (crc != 0x3524_41c2) { return 1; }
        std.hash::md5_digest md5 = std.hash::md5(array_value);
        std.hash::md5_digest md5_copy = md5;
        if ((md5.bytes[0] != 0x90) || (md5_copy.bytes[15] != 0x72)) { return 2; }
        std.hash::sha1_digest sha1 = std.hash::sha1(array_value);
        std.hash::sha1_digest sha1_copy = sha1;
        if ((sha1.bytes[0] != 0xa9) || (sha1_copy.bytes[19] != 0x9d)) { return 3; }
        std.hash::sha256_digest sha256 = std.hash::sha256(array_value);
        std.hash::sha256_digest sha256_copy = sha256;
        if ((sha256.bytes[0] != 0xba) || (sha256_copy.bytes[31] != 0xad)) { return 4; }
        std.hash::sha512_digest sha512 = std.hash::sha512(array_value);
        std.hash::sha512_digest sha512_copy = sha512;
        if ((sha512.bytes[0] != 0xdd) || (sha512_copy.bytes[63] != 0x9f)) { return 5; }
    }
    {
        u32 crc = std.hash::crc32(text);
        if (crc != 0x3524_41c2) { return 1; }
        std.hash::md5_digest md5 = std.hash::md5(text);
        std.hash::md5_digest md5_copy = md5;
        if ((md5.bytes[0] != 0x90) || (md5_copy.bytes[15] != 0x72)) { return 2; }
        std.hash::sha1_digest sha1 = std.hash::sha1(text);
        std.hash::sha1_digest sha1_copy = sha1;
        if ((sha1.bytes[0] != 0xa9) || (sha1_copy.bytes[19] != 0x9d)) { return 3; }
        std.hash::sha256_digest sha256 = std.hash::sha256(text);
        std.hash::sha256_digest sha256_copy = sha256;
        if ((sha256.bytes[0] != 0xba) || (sha256_copy.bytes[31] != 0xad)) { return 4; }
        std.hash::sha512_digest sha512 = std.hash::sha512(text);
        std.hash::sha512_digest sha512_copy = sha512;
        if ((sha512.bytes[0] != 0xdd) || (sha512_copy.bytes[63] != 0x9f)) { return 5; }
    }
    {
        u32 crc = std.hash::crc32(literal);
        if (crc != 0x3524_41c2) { return 1; }
        std.hash::md5_digest md5 = std.hash::md5(literal);
        std.hash::md5_digest md5_copy = md5;
        if ((md5.bytes[0] != 0x90) || (md5_copy.bytes[15] != 0x72)) { return 2; }
        std.hash::sha1_digest sha1 = std.hash::sha1(literal);
        std.hash::sha1_digest sha1_copy = sha1;
        if ((sha1.bytes[0] != 0xa9) || (sha1_copy.bytes[19] != 0x9d)) { return 3; }
        std.hash::sha256_digest sha256 = std.hash::sha256(literal);
        std.hash::sha256_digest sha256_copy = sha256;
        if ((sha256.bytes[0] != 0xba) || (sha256_copy.bytes[31] != 0xad)) { return 4; }
        std.hash::sha512_digest sha512 = std.hash::sha512(literal);
        std.hash::sha512_digest sha512_copy = sha512;
        if ((sha512.bytes[0] != 0xdd) || (sha512_copy.bytes[63] != 0x9f)) { return 5; }
    }
    if ((fixed[0] != 0x61) || (fixed[1] != 0x62) || (fixed[2] != 0x63)) { return 6; }
    std.bytes::append_u8(&owned, 0x64);
    std.bytes::append_u8(&array_value, 0x64);
    bytes moved = move owned;
    array<u8> moved_array = move array_value;
    if ((len(moved) != 4) || (len(moved_array) != 4)) { return 7; }
    const u8[] moved_view = std.array::as_slice(&moved);
    const u8[] moved_array_view = std.array::as_slice(&moved_array);
    if ((moved_view[0] != 0x61) || (moved_view[1] != 0x62) || (moved_view[2] != 0x63) || (moved_view[3] != 0x64)) { return 8; }
    if ((moved_array_view[0] != 0x61) || (moved_array_view[1] != 0x62) || (moved_array_view[2] != 0x63) || (moved_array_view[3] != 0x64)) { return 9; }
    return 0;
}

async i32 main() {
    try {
        i32 sync_status = sync_checks();
        task<i32 throws std.alloc::alloc_error> operation = async_checks();
        i32 async_status = await move operation;
        if (sync_status != async_status) { return 10; }
        return sync_status;
    } catch (std.alloc::alloc_error error) {
        error as void;
        return 11;
    } catch (std.async::start_error error) {
        error as void;
        return 12;
    }
}

module test.codegen.hash_independent;

i32 main() {
    u8[3] fixed = {};
    fixed[0] = 0x61;
    fixed[1] = 0x62;
    fixed[2] = 0x63;
    constexpr str literal = "abc";
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
    return 0;
}

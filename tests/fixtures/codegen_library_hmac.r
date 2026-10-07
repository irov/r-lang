module test.codegen.library_hmac;

import std.encoding;
import std.hash;

// R-SLIB-BYTES-0010..0011 (M20): SHA-256 and SHA-512 over data in pieces against the one-shot
// digests, and HMAC-SHA-256 and HMAC-SHA-512 against the test cases of RFC 4231.

protected bool same(str left, str right) { return std.bytes::equal(left, right); }

protected i32 vector(const u8[] key, const u8[] message, str sha256, str sha512)
    throws std.alloc::alloc_error {
    std.hash::sha256_digest short_code = std.hash::hmac_sha256(key, message);
    std.hash::sha512_digest long_code = std.hash::hmac_sha512(key, message);
    std.string::string short_hex = std.encoding::encode_hex(short_code.bytes);
    std.string::string long_hex = std.encoding::encode_hex(long_code.bytes);
    const u8[] short_bytes = short_hex;
    const u8[] long_bytes = long_hex;
    const u8[] short_expected = sha256;
    const u8[] long_expected = sha512;
    // Test case 5 compares the first 128 bits only.
    i32 status = 0;
    if (std.bytes::equal(short_bytes[0usize..len(short_expected)], short_expected) == false) {
        status = 1;
    }
    if (std.bytes::equal(long_bytes[0usize..len(long_expected)], long_expected) == false) {
        status = 2;
    }
    return status;
}

protected i32 check_hmac() throws std.alloc::alloc_error {
    u8[20] key_1 = {};
    for (usize index = 0usize; index < 20usize; index += 1usize) { key_1[index] = 0x0bu8; }
    if (vector(key_1, "Hi There",
               "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7",
               "87aa7cdea5ef619d4ff0b4241a1d6cb02379f4e2ce4ec2787ad0b30545e17cdedaa833b7d6b8a702038b274eaea3f4e4be9d914eeb61f1702e696c203a126854") != 0) {
        return 1;
    }
    if (vector("Jefe", "what do ya want for nothing?",
               "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843",
               "164b7a7bfcf819e2e395fbe73b56e0a387bd64222e831fd610270cd7ea2505549758bf75c05a994a6d034f65f8f0e6fdcaeab1a34d4a6b4b636e070a38bce737") != 0) {
        return 2;
    }
    u8[20] key_3 = {};
    u8[50] data_3 = {};
    for (usize index = 0usize; index < 20usize; index += 1usize) { key_3[index] = 0xaau8; }
    for (usize index = 0usize; index < 50usize; index += 1usize) { data_3[index] = 0xddu8; }
    if (vector(key_3, data_3,
               "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe",
               "fa73b0089d56a284efb0f0756c890be9b1b5dbdd8ee81a3655f83e33b2279d39bf3e848279a722c806b485a47e67c807b946a337bee8942674278859e13292fb") != 0) {
        return 3;
    }
    u8[25] key_4 = {};
    u8[50] data_4 = {};
    for (usize index = 0usize; index < 25usize; index += 1usize) { key_4[index] = (index + 1usize) as u8; }
    for (usize index = 0usize; index < 50usize; index += 1usize) { data_4[index] = 0xcdu8; }
    if (vector(key_4, data_4,
               "82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b",
               "b0ba465637458c6990e5a8c5f61d4af7e576d97ff94b872de76f8050361ee3dba91ca5c11aa25eb4d679275cc5788063a5f19741120c4f2de2adebeb10a298dd") != 0) {
        return 4;
    }
    u8[20] key_5 = {};
    for (usize index = 0usize; index < 20usize; index += 1usize) { key_5[index] = 0x0cu8; }
    if (vector(key_5, "Test With Truncation", "a3b6167473100ee06e0c796c2955552b",
               "415fad6271580a531d4179bc891d87a6") != 0) {
        return 5;
    }
    u8[131] key_6 = {};
    for (usize index = 0usize; index < 131usize; index += 1usize) { key_6[index] = 0xaau8; }
    if (vector(key_6, "Test Using Larger Than Block-Size Key - Hash Key First",
               "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54",
               "80b24263c7c1a3ebb71493c1dd7be8b49b46d1f41b4aeec1121b013783f8f3526b56d037e05f2598bd0fd2215d6a1e5295e64f73f63f0aec8b915a985d786598") != 0) {
        return 6;
    }
    if (vector(key_6, "This is a test using a larger than block-size key and a larger than block-size data. The key needs to be hashed before being used by the HMAC algorithm.",
               "9b09ffa71b942fcb27635fbcd5b0e944bfdc63644f0713938a7f51535c3a35e2",
               "e37b6a775dc87dbaa4dfa9f96e5e3ffddebd71f8867289865df5a32d20cdc944b6022cac3c4982b10d5eeb55c3e4de15134676fb6de0446065c97440fa8c6a58") != 0) {
        return 7;
    }
    std.hash::sha256_digest code = std.hash::hmac_sha256("key", "message");
    std.hash::sha256_digest again = std.hash::hmac_sha256("key", "message");
    if (std.secret::constant_time_equal(code.bytes, again.bytes) == false) { return 8; }
    return 0;
}

protected i32 check_pieces() {
    u8[300] data = {};
    for (usize index = 0usize; index < 300usize; index += 1usize) {
        data[index] = ((index * 7usize + 3usize) % 251usize) as u8;
    }
    usize[14] lengths = {0usize, 1usize, 55usize, 56usize, 63usize, 64usize, 65usize, 111usize,
                         112usize, 127usize, 128usize, 129usize, 255usize, 300usize};
    for (usize case_index = 0usize; case_index < 14usize; case_index += 1usize) {
        usize length = lengths[case_index];
        const u8[] message = data[0usize..length];
        std.hash::sha256_digest whole_256_digest = std.hash::sha256(message);
        std.hash::sha512_digest whole_512_digest = std.hash::sha512(message);
        u8[32] whole_256 = whole_256_digest.bytes;
        u8[64] whole_512 = whole_512_digest.bytes;
        for (usize piece = 1usize; piece <= 70usize; piece += 23usize) {
            std.hash::sha256_state short_state = std.hash::sha256_state::create();
            std.hash::sha512_state long_state = std.hash::sha512_state::create();
            usize at = 0usize;
            while (at < length) {
                usize end = at + piece;
                if (end > length) { end = length; }
                short_state.update(message[at..end]);
                long_state.update(message[at..end]);
                at = end;
            }
            std.hash::sha256_digest pieces_256 = short_state.finish();
            std.hash::sha512_digest pieces_512 = long_state.finish();
            i32 status = 0;
            if (std.bytes::equal(pieces_256.bytes, whole_256) == false) { status = 20; }
            if (std.bytes::equal(pieces_512.bytes, whole_512) == false) { status = 21; }
            if (status != 0) { return status; }
        }
    }
    std.hash::sha256_state prefix = std.hash::sha256_state::create();
    prefix.update("common prefix ");
    std.hash::sha256_state left = prefix;
    std.hash::sha256_state right = prefix;
    left.update("left");
    right.update("right");
    std.hash::sha256_digest left_digest = left.finish();
    std.hash::sha256_digest expected_left = std.hash::sha256("common prefix left");
    std.hash::sha256_digest right_digest = right.finish();
    std.hash::sha256_digest expected_right = std.hash::sha256("common prefix right");
    i32 status = 0;
    if (std.bytes::equal(left_digest.bytes, expected_left.bytes) == false) { status = 22; }
    if (std.bytes::equal(right_digest.bytes, expected_right.bytes) == false) { status = 23; }
    if (status != 0) { return status; }
    return 0;
}

i32 main() {
    try {
        i32 codes = check_hmac();
        if (codes != 0) { return codes; }
        return check_pieces();
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 99;
    }
}

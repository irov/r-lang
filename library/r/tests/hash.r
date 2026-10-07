module tests.std.hash;
import std.test;
import std.encoding;
import std.hash;

// The tests of the R part of std.hash (Library R-SLIB-BYTES-0010..0011): SHA-256 and SHA-512
// over data in pieces and HMAC of RFC 2104, run in test mode (Core R-FUNC-0025). The expected
// digests are the vectors of FIPS PUB 180-4 examples, RFC 4231 and Python's hashlib.

/* Fails unless the bytes are those of the lowercase hexadecimal text. */
protected void expect_hex(const u8[] digest, str expected)
    throws std.test::failure, std.alloc::alloc_error {
    std.string::string digits = std.encoding::encode_hex(digest);
    std.test::equal_text(digits, expected);
}

/* SHA-256 of the data given in pieces of `piece` bytes. */
protected std.hash::sha256_digest sha256_in_pieces(const u8[] data, usize piece) {
    std.hash::sha256_state state = std.hash::sha256_state::create();
    usize at = 0usize;
    while (at < len(data)) {
        usize end = at + piece;
        if (end > len(data)) { end = len(data); }
        state.update(data[at..end]);
        at = end;
    }
    return state.finish();
}

protected std.hash::sha512_digest sha512_in_pieces(const u8[] data, usize piece) {
    std.hash::sha512_state state = std.hash::sha512_state::create();
    usize at = 0usize;
    while (at < len(data)) {
        usize end = at + piece;
        if (end > len(data)) { end = len(data); }
        state.update(data[at..end]);
        at = end;
    }
    return state.finish();
}

@test
void digests_the_standard_vectors() throws std.test::failure, std.alloc::alloc_error {
    std.hash::sha256_digest empty = std.hash::sha256_state::create().finish();
    expect_hex(empty.bytes, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    std.hash::sha256_digest abc = sha256_in_pieces("abc", 1usize);
    expect_hex(abc.bytes, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    // Two blocks: the padding of a 56-byte message does not fit into the first one.
    std.hash::sha256_digest two_blocks =
        sha256_in_pieces("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 5usize);
    expect_hex(two_blocks.bytes,
               "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    std.hash::sha512_digest empty_512 = std.hash::sha512_state::create().finish();
    expect_hex(empty_512.bytes,
               "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce"
               "47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e");
    std.hash::sha512_digest abc_512 = sha512_in_pieces("abc", 2usize);
    expect_hex(abc_512.bytes,
               "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
               "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");
    std.hash::sha512_digest two_blocks_512 = sha512_in_pieces(
        "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqr"
        "lmnopqrsmnopqrstnopqrstu", 13usize);
    expect_hex(two_blocks_512.bytes,
               "8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018"
               "501d289e4900f7e4331b99dec4b5433ac7d329eeb6dd26545e96e55b874be909");
}

@test
void matches_the_one_shot_digests_at_block_boundaries() throws std.test::failure, std.alloc::alloc_error {
    u8[300] data = {};
    for (usize index = 0usize; index < 300usize; index += 1usize) {
        data[index] = ((index * 7usize + 3usize) % 251usize) as u8;
    }
    usize[16] lengths = {0usize, 1usize, 55usize, 56usize, 63usize, 64usize, 65usize, 111usize,
                         112usize, 119usize, 120usize, 127usize, 128usize, 129usize, 256usize,
                         300usize};
    usize[4] pieces = {1usize, 7usize, 64usize, 200usize};
    for (usize case_index = 0usize; case_index < 16usize; case_index += 1usize) {
        usize length = lengths[case_index];
        const u8[] message = data[0usize..length];
        for (usize piece_index = 0usize; piece_index < 4usize; piece_index += 1usize) {
            usize piece = pieces[piece_index];
            std.hash::sha256_digest whole_256 = std.hash::sha256(message);
            std.hash::sha512_digest whole_512 = std.hash::sha512(message);
            std.hash::sha256_digest split_256 = sha256_in_pieces(message, piece);
            std.hash::sha512_digest split_512 = sha512_in_pieces(message, piece);
            std.string::string context = f"length {length} in pieces of {piece}";
            std.test::check(std.bytes::equal(split_256.bytes, whole_256.bytes) == true,
                            context);
            std.test::check(std.bytes::equal(split_512.bytes, whole_512.bytes) == true,
                            context);
        }
    }
}

@test
void hashes_a_million_bytes_in_pieces() throws std.test::failure, std.alloc::alloc_error {
    u8[1000] block = {};
    for (usize index = 0usize; index < 1000usize; index += 1usize) { block[index] = 97u8; }
    std.hash::sha256_state short_state = std.hash::sha256_state::create();
    std.hash::sha512_state long_state = std.hash::sha512_state::create();
    for (usize round = 0usize; round < 1000usize; round += 1usize) {
        short_state.update(block);
        long_state.update(block);
    }
    std.hash::sha256_digest short_digest = short_state.finish();
    expect_hex(short_digest.bytes,
               "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    std.hash::sha512_digest long_digest = long_state.finish();
    expect_hex(long_digest.bytes,
               "e718483d0ce769644e2e42c7bc15b4638e1f98b13b2044285632a803afa973eb"
               "de0ff244877ea60a4cb0432ce577c31beb009c5c2c49aa2e4eadb217ad8cc09b");
}

@test
void continues_copies_on_their_own() throws std.test::failure, std.alloc::alloc_error {
    std.hash::sha256_state prefix = std.hash::sha256_state::create();
    prefix.update("common prefix ");
    std.hash::sha256_state left = prefix;
    std.hash::sha256_state right = prefix;
    left.update("left");
    right.update("right");
    std.hash::sha256_digest left_digest = left.finish();
    std.hash::sha256_digest right_digest = right.finish();
    std.hash::sha256_digest prefix_digest = prefix.finish();
    std.hash::sha256_digest left_expected = std.hash::sha256("common prefix left");
    std.hash::sha256_digest right_expected = std.hash::sha256("common prefix right");
    std.hash::sha256_digest prefix_expected = std.hash::sha256("common prefix ");
    std.test::check(std.bytes::equal(left_digest.bytes, left_expected.bytes) == true, "left");
    std.test::check(std.bytes::equal(right_digest.bytes, right_expected.bytes) == true, "right");
    std.test::check(std.bytes::equal(prefix_digest.bytes, prefix_expected.bytes) == true,
                    "the original is unchanged");
    std.hash::sha512_state long_prefix = std.hash::sha512_state::create();
    long_prefix.update("common prefix ");
    std.hash::sha512_state long_left = long_prefix;
    long_left.update("left");
    std.hash::sha512_digest long_digest = long_left.finish();
    std.hash::sha512_digest long_expected = std.hash::sha512("common prefix left");
    std.test::check(std.bytes::equal(long_digest.bytes, long_expected.bytes) == true, "sha512");
}

@test
void computes_hmac_sha256() throws std.test::failure, std.alloc::alloc_error {
    // RFC 4231 test cases 1, 2, 3, 5 (truncated to 128 bits) and 6.
    u8[20] key_1 = {};
    for (usize index = 0usize; index < 20usize; index += 1usize) { key_1[index] = 0x0bu8; }
    std.hash::sha256_digest case_1 = std.hash::hmac_sha256(key_1, "Hi There");
    expect_hex(case_1.bytes, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
    std.hash::sha256_digest case_2 = std.hash::hmac_sha256("Jefe", "what do ya want for nothing?");
    expect_hex(case_2.bytes, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
    u8[20] key_3 = {};
    u8[50] data_3 = {};
    for (usize index = 0usize; index < 20usize; index += 1usize) { key_3[index] = 0xaau8; }
    for (usize index = 0usize; index < 50usize; index += 1usize) { data_3[index] = 0xddu8; }
    std.hash::sha256_digest case_3 = std.hash::hmac_sha256(key_3, data_3);
    expect_hex(case_3.bytes, "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe");
    u8[20] key_5 = {};
    for (usize index = 0usize; index < 20usize; index += 1usize) { key_5[index] = 0x0cu8; }
    std.hash::sha256_digest case_5 = std.hash::hmac_sha256(key_5, "Test With Truncation");
    expect_hex(case_5.bytes[0usize..16usize], "a3b6167473100ee06e0c796c2955552b");
    u8[131] key_6 = {};
    for (usize index = 0usize; index < 131usize; index += 1usize) { key_6[index] = 0xaau8; }
    std.hash::sha256_digest case_6 =
        std.hash::hmac_sha256(key_6, "Test Using Larger Than Block-Size Key - Hash Key First");
    expect_hex(case_6.bytes, "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
    std.hash::sha256_digest empty = std.hash::hmac_sha256("", "");
    expect_hex(empty.bytes, "b613679a0814d9ec772f95d778c35fc5ff1697c493715653c6c712144292c5ad");
}

@test
void computes_hmac_sha512() throws std.test::failure, std.alloc::alloc_error {
    // RFC 4231 test cases 2, 4 and 7.
    std.hash::sha512_digest case_2 = std.hash::hmac_sha512("Jefe", "what do ya want for nothing?");
    expect_hex(case_2.bytes,
               "164b7a7bfcf819e2e395fbe73b56e0a387bd64222e831fd610270cd7ea250554"
               "9758bf75c05a994a6d034f65f8f0e6fdcaeab1a34d4a6b4b636e070a38bce737");
    u8[25] key_4 = {};
    u8[50] data_4 = {};
    for (usize index = 0usize; index < 25usize; index += 1usize) { key_4[index] = (index + 1usize) as u8; }
    for (usize index = 0usize; index < 50usize; index += 1usize) { data_4[index] = 0xcdu8; }
    std.hash::sha512_digest case_4 = std.hash::hmac_sha512(key_4, data_4);
    expect_hex(case_4.bytes,
               "b0ba465637458c6990e5a8c5f61d4af7e576d97ff94b872de76f8050361ee3db"
               "a91ca5c11aa25eb4d679275cc5788063a5f19741120c4f2de2adebeb10a298dd");
    u8[131] key_7 = {};
    for (usize index = 0usize; index < 131usize; index += 1usize) { key_7[index] = 0xaau8; }
    std.hash::sha512_digest case_7 = std.hash::hmac_sha512(key_7,
        "This is a test using a larger than block-size key and a larger than block-size data. "
        "The key needs to be hashed before being used by the HMAC algorithm.");
    expect_hex(case_7.bytes,
               "e37b6a775dc87dbaa4dfa9f96e5e3ffddebd71f8867289865df5a32d20cdc944"
               "b6022cac3c4982b10d5eeb55c3e4de15134676fb6de0446065c97440fa8c6a58");
    std.hash::sha512_digest fox =
        std.hash::hmac_sha512("key", "The quick brown fox jumps over the lazy dog");
    expect_hex(fox.bytes,
               "b42af09057bac1e2d41708e48a902e09b5ff7f12ab428a4fe86653c73dd248fb"
               "82f948a549f7b791a5b41915ee4d1ec3935357e4e2317250d0372afa2ebeeb3a");
}

@test
void hashes_keys_longer_than_the_block() throws std.test::failure, std.alloc::alloc_error {
    // A key of up to 64 (SHA-256) or 128 (SHA-512) bytes is used as it is; a longer key is
    // replaced by its digest.
    u8[65] key = {};
    for (usize index = 0usize; index < 65usize; index += 1usize) { key[index] = index as u8; }
    std.hash::sha256_digest block_key = std.hash::hmac_sha256(key[0usize..64usize], "msg");
    expect_hex(block_key.bytes, "a5347accf12c5c3edf87ddb44e5b3a1c6957591c7092e527c2e63a5e48f8ebc5");
    std.hash::sha256_digest long_key = std.hash::hmac_sha256(key, "msg");
    expect_hex(long_key.bytes, "0b4330970a8c3347598324cd9a56100c0c95ad46a30ae4de0011a39cc6c31dfe");
    std.hash::sha256_digest reduced = std.hash::sha256(key);
    std.hash::sha256_digest via_digest = std.hash::hmac_sha256(reduced.bytes, "msg");
    std.test::check(std.secret::constant_time_equal(long_key.bytes, via_digest.bytes) == true,
                    "a long SHA-256 key is hashed first");
    u8[129] long_512 = {};
    for (usize index = 0usize; index < 129usize; index += 1usize) { long_512[index] = 0x5au8; }
    std.hash::sha512_digest code_512 = std.hash::hmac_sha512(long_512, "msg");
    std.hash::sha512_digest reduced_512 = std.hash::sha512(long_512);
    std.hash::sha512_digest via_digest_512 = std.hash::hmac_sha512(reduced_512.bytes, "msg");
    std.test::check(std.secret::constant_time_equal(code_512.bytes, via_digest_512.bytes) == true,
                    "a long SHA-512 key is hashed first");
    std.hash::sha512_digest block_512 = std.hash::hmac_sha512(long_512[0usize..128usize], "msg");
    std.test::check(std.secret::constant_time_equal(block_512.bytes, code_512.bytes) == false,
                    "a 128-byte key is used as it is");
}

module example.tokens.codes;
import std.bufio;
import std.encoding;
import std.hash;

/* tokens sign KEY MESSAGE: HMAC-SHA-256 and HMAC-SHA-512 of the message, in hexadecimal and in
   base64url. */
std.string::string signed(str key, str message) throws std.alloc::alloc_error {
    std.hash::sha256_digest short_code = std.hash::hmac_sha256(key, message);
    std.hash::sha512_digest long_code = std.hash::hmac_sha512(key, message);
    std.string::string short_hex = std.encoding::encode_hex(short_code.bytes);
    std.string::string short_url = std.encoding::encode_base64_url(short_code.bytes);
    std.string::string long_hex = std.encoding::encode_hex(long_code.bytes);
    return f"hmac-sha256 {short_hex}\nhmac-sha256-url {short_url}\nhmac-sha512 {long_hex}\n";
}

/* tokens verify KEY MESSAGE HEX: whether HEX is the HMAC-SHA-256 of the message; the comparison
   takes the same time for every code of the right length. */
bool verified(str key, str message, str code)
    throws std.convert::parse_error, std.alloc::alloc_error {
    bytes given = std.encoding::decode_hex(code);
    std.hash::sha256_digest expected = std.hash::hmac_sha256(key, message);
    return std.secret::constant_time_equal(given.as_slice(), expected.bytes);
}

/* tokens digest: SHA-256 and SHA-512 of standard input, read in pieces of up to 4096 bytes. */
async std.string::string digested() throws std.error::fault {
    std.bufio::reader<std.io::input> input =
        std.bufio::reader<std.io::input>::create(std.io::stdin(), 4096usize);
    std.hash::sha256_state short_state = std.hash::sha256_state::create();
    std.hash::sha512_state long_state = std.hash::sha512_state::create();
    u8[4096] piece = {};
    u64 total = 0u64;
    task_scope(1) io {
        while (true) {
            usize count = await input.read_into(&piece);
            if (count == 0usize) { break; }
            short_state.update(piece[0usize..count]);
            long_state.update(piece[0usize..count]);
            total += count as u64;
        }
    }
    std.hash::sha256_digest short_digest = short_state.finish();
    std.hash::sha512_digest long_digest = long_state.finish();
    std.string::string short_hex = std.encoding::encode_hex(short_digest.bytes);
    std.string::string long_hex = std.encoding::encode_hex(long_digest.bytes);
    return f"{total} bytes\nsha256 {short_hex}\nsha512 {long_hex}\n";
}
